#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/nstime.h"

#include "jemalloc/internal/assert.h"

#define BILLION	UINT64_C(1000000000)
#define MILLION	UINT64_C(1000000)

void
nstime_init(nstime_t *time, uint64_t ns) {
	time->ns = ns;
}

void
nstime_init2(nstime_t *time, uint64_t sec, uint64_t nsec) {
	time->ns = sec * BILLION + nsec;
}

uint64_t
nstime_ns(const nstime_t *time) {
	return time->ns;
}

uint64_t
nstime_msec(const nstime_t *time) {
	return time->ns / MILLION;
}

uint64_t
nstime_sec(const nstime_t *time) {
	return time->ns / BILLION;
}

uint64_t
nstime_nsec(const nstime_t *time) {
	return time->ns % BILLION;
}

void
nstime_copy(nstime_t *time, const nstime_t *source) {
	*time = *source;
}

int
nstime_compare(const nstime_t *a, const nstime_t *b) {
	return (a->ns > b->ns) - (a->ns < b->ns);
}

void
nstime_add(nstime_t *time, const nstime_t *addend) {
	assert(UINT64_MAX - time->ns >= addend->ns);

	time->ns += addend->ns;
}

void
nstime_iadd(nstime_t *time, uint64_t addend) {
	assert(UINT64_MAX - time->ns >= addend);

	time->ns += addend;
}

void
nstime_subtract(nstime_t *time, const nstime_t *subtrahend) {
	assert(nstime_compare(time, subtrahend) >= 0);

	time->ns -= subtrahend->ns;
}

void
nstime_isubtract(nstime_t *time, uint64_t subtrahend) {
	assert(time->ns >= subtrahend);

	time->ns -= subtrahend;
}

void
nstime_imultiply(nstime_t *time, uint64_t multiplier) {
	assert((((time->ns | multiplier) & (UINT64_MAX << (sizeof(uint64_t) <<
	    2))) == 0) || ((time->ns * multiplier) / multiplier == time->ns));

	time->ns *= multiplier;
}

void
nstime_idivide(nstime_t *time, uint64_t divisor) {
	assert(divisor != 0);

	time->ns /= divisor;
}

uint64_t
nstime_divide(const nstime_t *time, const nstime_t *divisor) {
	assert(divisor->ns != 0);

	return time->ns / divisor->ns;
}

#ifdef _WIN32
#  define NSTIME_MONOTONIC true
static void
nstime_get(nstime_t *time) {
	FILETIME ft;
	uint64_t ticks_100ns;

	GetSystemTimeAsFileTime(&ft);
	ticks_100ns = (((uint64_t)ft.dwHighDateTime) << 32) | ft.dwLowDateTime;

	nstime_init(time, ticks_100ns * 100);
}
#elif defined(JEMALLOC_HAVE_CLOCK_MONOTONIC_COARSE)
#  define NSTIME_MONOTONIC true
static void
nstime_get(nstime_t *time) {
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
	nstime_init2(time, ts.tv_sec, ts.tv_nsec);
}
#elif defined(JEMALLOC_HAVE_CLOCK_MONOTONIC)
#  define NSTIME_MONOTONIC true
static void
nstime_get(nstime_t *time) {
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	nstime_init2(time, ts.tv_sec, ts.tv_nsec);
}
#elif defined(JEMALLOC_HAVE_MACH_ABSOLUTE_TIME)
#  define NSTIME_MONOTONIC true
static void
nstime_get(nstime_t *time) {
	nstime_init(time, mach_absolute_time());
}
#else
#  define NSTIME_MONOTONIC false
static void
nstime_get(nstime_t *time) {
	struct timeval tv;

	gettimeofday(&tv, NULL);
	nstime_init2(time, tv.tv_sec, tv.tv_usec * 1000);
}
#endif

static bool
nstime_monotonic_impl(void) {
	return NSTIME_MONOTONIC;
#undef NSTIME_MONOTONIC
}
nstime_monotonic_t *JET_MUTABLE nstime_monotonic = nstime_monotonic_impl;

static bool
nstime_update_impl(nstime_t *time) {
	nstime_t old_time;

	nstime_copy(&old_time, time);
	nstime_get(time);

	/* Handle non-monotonic clocks. */
	if (unlikely(nstime_compare(&old_time, time) > 0)) {
		nstime_copy(time, &old_time);
		return true;
	}

	return false;
}
nstime_update_t *JET_MUTABLE nstime_update = nstime_update_impl;
