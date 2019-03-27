#include "jemalloc/internal/jemalloc_preamble.h"
#include "jemalloc/internal/jemalloc_internal_includes.h"

#include "jemalloc/internal/log.h"

char log_var_names[JEMALLOC_LOG_VAR_BUFSIZE];
atomic_b_t log_init_done = ATOMIC_INIT(false);

/*
 * Returns true if we were able to pick out a segment.  Fills in r_segment_end
 * with a pointer to the first character after the end of the string.
 */
static const char *
log_var_extract_segment(const char* segment_begin) {
	const char *end;
	for (end = segment_begin; *end != '\0' && *end != '|'; end++) {
	}
	return end;
}

static bool
log_var_matches_segment(const char *segment_begin, const char *segment_end,
    const char *log_var_begin, const char *log_var_end) {
	assert(segment_begin <= segment_end);
	assert(log_var_begin < log_var_end);

	ptrdiff_t segment_len = segment_end - segment_begin;
	ptrdiff_t log_var_len = log_var_end - log_var_begin;
	/* The special '.' segment matches everything. */
	if (segment_len == 1 && *segment_begin == '.') {
		return true;
	}
        if (segment_len == log_var_len) {
		return strncmp(segment_begin, log_var_begin, segment_len) == 0;
	} else if (segment_len < log_var_len) {
		return strncmp(segment_begin, log_var_begin, segment_len) == 0
		    && log_var_begin[segment_len] == '.';
        } else {
		return false;
	}
}

unsigned
log_var_update_state(log_var_t *log_var) {
	const char *log_var_begin = log_var->name;
	const char *log_var_end = log_var->name + strlen(log_var->name);

	/* Pointer to one before the beginning of the current segment. */
	const char *segment_begin = log_var_names;

	/*
	 * If log_init done is false, we haven't parsed the malloc conf yet.  To
	 * avoid log-spew, we default to not displaying anything.
	 */
	if (!atomic_load_b(&log_init_done, ATOMIC_ACQUIRE)) {
		return LOG_INITIALIZED_NOT_ENABLED;
	}

	while (true) {
		const char *segment_end = log_var_extract_segment(
		    segment_begin);
		assert(segment_end < log_var_names + JEMALLOC_LOG_VAR_BUFSIZE);
		if (log_var_matches_segment(segment_begin, segment_end,
		    log_var_begin, log_var_end)) {
			atomic_store_u(&log_var->state, LOG_ENABLED,
			    ATOMIC_RELAXED);
			return LOG_ENABLED;
		}
		if (*segment_end == '\0') {
			/* Hit the end of the segment string with no match. */
			atomic_store_u(&log_var->state,
			    LOG_INITIALIZED_NOT_ENABLED, ATOMIC_RELAXED);
			return LOG_INITIALIZED_NOT_ENABLED;
		}
		/* Otherwise, skip the delimiter and continue. */
		segment_begin = segment_end + 1;
	}
}
