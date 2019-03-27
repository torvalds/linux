#ifndef JEMALLOC_INTERNAL_TICKER_H
#define JEMALLOC_INTERNAL_TICKER_H

#include "jemalloc/internal/util.h"

/**
 * A ticker makes it easy to count-down events until some limit.  You
 * ticker_init the ticker to trigger every nticks events.  You then notify it
 * that an event has occurred with calls to ticker_tick (or that nticks events
 * have occurred with a call to ticker_ticks), which will return true (and reset
 * the counter) if the countdown hit zero.
 */

typedef struct {
	int32_t tick;
	int32_t nticks;
} ticker_t;

static inline void
ticker_init(ticker_t *ticker, int32_t nticks) {
	ticker->tick = nticks;
	ticker->nticks = nticks;
}

static inline void
ticker_copy(ticker_t *ticker, const ticker_t *other) {
	*ticker = *other;
}

static inline int32_t
ticker_read(const ticker_t *ticker) {
	return ticker->tick;
}

/*
 * Not intended to be a public API.  Unfortunately, on x86, neither gcc nor
 * clang seems smart enough to turn
 *   ticker->tick -= nticks;
 *   if (unlikely(ticker->tick < 0)) {
 *     fixup ticker
 *     return true;
 *   }
 *   return false;
 * into
 *   subq %nticks_reg, (%ticker_reg)
 *   js fixup ticker
 *
 * unless we force "fixup ticker" out of line.  In that case, gcc gets it right,
 * but clang now does worse than before.  So, on x86 with gcc, we force it out
 * of line, but otherwise let the inlining occur.  Ordinarily this wouldn't be
 * worth the hassle, but this is on the fast path of both malloc and free (via
 * tcache_event).
 */
#if defined(__GNUC__) && !defined(__clang__)				\
    && (defined(__x86_64__) || defined(__i386__))
JEMALLOC_NOINLINE
#endif
static bool
ticker_fixup(ticker_t *ticker) {
	ticker->tick = ticker->nticks;
	return true;
}

static inline bool
ticker_ticks(ticker_t *ticker, int32_t nticks) {
	ticker->tick -= nticks;
	if (unlikely(ticker->tick < 0)) {
		return ticker_fixup(ticker);
	}
	return false;
}

static inline bool
ticker_tick(ticker_t *ticker) {
	return ticker_ticks(ticker, 1);
}

#endif /* JEMALLOC_INTERNAL_TICKER_H */
