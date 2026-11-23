// turboav1 profiling: per-stage CPU-time accounting.
// Header-only. In each instrumented .c file, add:
//     #include "svt_prof.h"
//     SVT_PROF_DEFINE(my_stage);
// and wrap the kernel body:
//     uint64_t _t0 = svt_prof_start();
//     ... existing body ...
//     svt_prof_stop(my_stage, _t0);
// At the end of main() call:  svt_prof_report();
//
// Build with -DSVT_PROF_ENABLED=1 to compile in (zero overhead otherwise).
#ifndef SVT_PROF_H
#define SVT_PROF_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SVT_PROF_ENABLED
#define SVT_PROF_ENABLED 0
#endif

#include <stdint.h>
#if SVT_PROF_ENABLED
#include <time.h>
#include <stdio.h>

// Each instrumented translation unit defines its own stage counter.
// svt_prof_report() walks a registry of (name, counter) pairs.

#define SVT_PROF_MAX_STAGES 16

typedef struct {
    const char* name;
    volatile uint64_t* ns;
} svt_prof_entry;

extern __attribute__((visibility("default"))) svt_prof_entry svt_prof_registry[SVT_PROF_MAX_STAGES];
extern __attribute__((visibility("default"))) volatile int   svt_prof_registry_count;

// Define in one .c file (the one that calls svt_prof_report):
#define SVT_PROF_DEFINE_GLOBALS                                                          \
    __attribute__((visibility("default"))) svt_prof_entry svt_prof_registry[SVT_PROF_MAX_STAGES]; \
    __attribute__((visibility("default"))) volatile int svt_prof_registry_count = 0

// Define in each instrumented .c file:
#define SVT_PROF_DEFINE(stage_name)                                   \
    static volatile uint64_t stage_name##_ns = 0;                      \
    __attribute__((constructor)) static void stage_name##_register(void) { \
        int _i = __atomic_fetch_add(&svt_prof_registry_count, 1, __ATOMIC_RELAXED); \
        if (_i < SVT_PROF_MAX_STAGES) {                               \
            svt_prof_registry[_i].name = #stage_name;                 \
            svt_prof_registry[_i].ns   = &stage_name##_ns;            \
        }                                                             \
    }

static inline uint64_t svt_prof_clock_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#define svt_prof_start() svt_prof_clock_ns()
#define svt_prof_stop(stage, t0)                                       \
    do {                                                               \
        uint64_t _t1 = svt_prof_clock_ns();                            \
        __atomic_fetch_add(&stage##_ns, _t1 - (t0), __ATOMIC_RELAXED); \
    } while (0)

static inline void svt_prof_report(const char* label) {
    int n = __atomic_load_n(&svt_prof_registry_count, __ATOMIC_RELAXED);
    uint64_t total = 0;
    for (int i = 0; i < n; i++)
        total += __atomic_load_n(svt_prof_registry[i].ns, __ATOMIC_RELAXED);
    if (total == 0) return;
    fprintf(stderr, "\n=== SVT PROF (%s) — per-stage CPU time ===\n", label);
    for (int i = 0; i < n; i++) {
        uint64_t v = __atomic_load_n(svt_prof_registry[i].ns, __ATOMIC_RELAXED);
        fprintf(stderr, "  %5.2f%%  %12.3f ms  %s\n",
                100.0 * (double)v / (double)total, (double)v / 1e6,
                svt_prof_registry[i].name);
    }
    fprintf(stderr, "  %5.2f%%  %12.3f ms  (instrumented total)\n", 100.0, (double)total / 1e6);
}

#else  // !SVT_PROF_ENABLED
#define SVT_PROF_DEFINE_GLOBALS
#define SVT_PROF_DEFINE(stage_name)
#define svt_prof_start() 0
#define svt_prof_stop(stage, t0) ((void)0)
static inline void svt_prof_report(const char* label) { (void)label; }
#endif

#ifdef __cplusplus
}
#endif
#endif  // SVT_PROF_H
