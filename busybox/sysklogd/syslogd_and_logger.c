/* vi: set sw=4 ts=4: */
/*
 * prioritynames[] and facilitynames[]
 *
 * Copyright (C) 2008 by Denys Vlasenko <vda.linux@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "common_bufsiz.h"
#define SYSLOG_NAMES
#define SYSLOG_NAMES_CONST
#include <syslog.h>

#if 0
/* For the record: with SYSLOG_NAMES <syslog.h> defines
 * (not declares) the following:
 */
typedef struct _code {
	/*const*/ char *c_name;
	int c_val;
} CODE;
/*const*/ CODE prioritynames[] = {
    { "alert", LOG_ALERT },
...
    { NULL, -1 }
};
/* same for facilitynames[] */

/* This MUST occur only once per entire executable,
 * therefore we can't just do it in syslogd.c and logger.c -
 * there will be two copies of it.
 *
 * We cannot even do it in separate file and then just reference
 * prioritynames[] from syslogd.c and logger.c - bare <syslog.h>
 * will not emit extern decls for prioritynames[]! Attempts to
 * emit "matching" struct _code declaration defeat the whole purpose
 * of <syslog.h>.
 *
 * For now, syslogd.c and logger.c are simply compiled into
 * one object file.
 */
#endif

/* musl decided to be funny and it implements these as giant defines
 * of the form: ((CODE *)(const CODE []){ ... })
 * Which works, but causes _every_ function using them
 * to have a copy on stack (at least with gcc-6.3.0).
 * If we reference them just once, this saves 150 bytes.
 * The pointers themselves are optimized out
 * (no size change on uclibc).
 */
static const CODE *const bb_prioritynames = prioritynames;
static const CODE *const bb_facilitynames = facilitynames;


#if ENABLE_SYSLOGD
#include "syslogd.c"
#endif

#if ENABLE_LOGGER
#include "logger.c"
#endif
