#ifndef JEMALLOC_INTERNAL_LOG_H
#define JEMALLOC_INTERNAL_LOG_H

#include "jemalloc/internal/atomic.h"
#include "jemalloc/internal/malloc_io.h"
#include "jemalloc/internal/mutex.h"

#ifdef JEMALLOC_LOG
#  define JEMALLOC_LOG_VAR_BUFSIZE 1000
#else
#  define JEMALLOC_LOG_VAR_BUFSIZE 1
#endif

#define JEMALLOC_LOG_BUFSIZE 4096

/*
 * The log malloc_conf option is a '|'-delimited list of log_var name segments
 * which should be logged.  The names are themselves hierarchical, with '.' as
 * the delimiter (a "segment" is just a prefix in the log namespace).  So, if
 * you have:
 *
 * log("arena", "log msg for arena"); // 1
 * log("arena.a", "log msg for arena.a"); // 2
 * log("arena.b", "log msg for arena.b"); // 3
 * log("arena.a.a", "log msg for arena.a.a"); // 4
 * log("extent.a", "log msg for extent.a"); // 5
 * log("extent.b", "log msg for extent.b"); // 6
 *
 * And your malloc_conf option is "log=arena.a|extent", then lines 2, 4, 5, and
 * 6 will print at runtime.  You can enable logging from all log vars by
 * writing "log=.".
 *
 * None of this should be regarded as a stable API for right now.  It's intended
 * as a debugging interface, to let us keep around some of our printf-debugging
 * statements.
 */

extern char log_var_names[JEMALLOC_LOG_VAR_BUFSIZE];
extern atomic_b_t log_init_done;

typedef struct log_var_s log_var_t;
struct log_var_s {
	/*
	 * Lowest bit is "inited", second lowest is "enabled".  Putting them in
	 * a single word lets us avoid any fences on weak architectures.
	 */
	atomic_u_t state;
	const char *name;
};

#define LOG_NOT_INITIALIZED 0U
#define LOG_INITIALIZED_NOT_ENABLED 1U
#define LOG_ENABLED 2U

#define LOG_VAR_INIT(name_str) {ATOMIC_INIT(LOG_NOT_INITIALIZED), name_str}

/*
 * Returns the value we should assume for state (which is not necessarily
 * accurate; if logging is done before logging has finished initializing, then
 * we default to doing the safe thing by logging everything).
 */
unsigned log_var_update_state(log_var_t *log_var);

/* We factor out the metadata management to allow us to test more easily. */
#define log_do_begin(log_var)						\
if (config_log) {							\
	unsigned log_state = atomic_load_u(&(log_var).state,		\
	    ATOMIC_RELAXED);						\
	if (unlikely(log_state == LOG_NOT_INITIALIZED)) {		\
		log_state = log_var_update_state(&(log_var));		\
		assert(log_state != LOG_NOT_INITIALIZED);		\
	}								\
	if (log_state == LOG_ENABLED) {					\
		{
			/* User code executes here. */
#define log_do_end(log_var)						\
		}							\
	}								\
}

/*
 * MSVC has some preprocessor bugs in its expansion of __VA_ARGS__ during
 * preprocessing.  To work around this, we take all potential extra arguments in
 * a var-args functions.  Since a varargs macro needs at least one argument in
 * the "...", we accept the format string there, and require that the first
 * argument in this "..." is a const char *.
 */
static inline void
log_impl_varargs(const char *name, ...) {
	char buf[JEMALLOC_LOG_BUFSIZE];
	va_list ap;

	va_start(ap, name);
	const char *format = va_arg(ap, const char *);
	size_t dst_offset = 0;
	dst_offset += malloc_snprintf(buf, JEMALLOC_LOG_BUFSIZE, "%s: ", name);
	dst_offset += malloc_vsnprintf(buf + dst_offset,
	    JEMALLOC_LOG_BUFSIZE - dst_offset, format, ap);
	dst_offset += malloc_snprintf(buf + dst_offset,
	    JEMALLOC_LOG_BUFSIZE - dst_offset, "\n");
	va_end(ap);

	malloc_write(buf);
}

/* Call as log("log.var.str", "format_string %d", arg_for_format_string); */
#define LOG(log_var_str, ...)						\
do {									\
	static log_var_t log_var = LOG_VAR_INIT(log_var_str);		\
	log_do_begin(log_var)						\
		log_impl_varargs((log_var).name, __VA_ARGS__);		\
	log_do_end(log_var)						\
} while (0)

#endif /* JEMALLOC_INTERNAL_LOG_H */
