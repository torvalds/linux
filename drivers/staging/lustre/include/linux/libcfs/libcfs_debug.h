/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/libcfs_debug.h
 *
 * Debug messages and assertions
 *
 */

#ifndef __LIBCFS_DEBUG_H__
#define __LIBCFS_DEBUG_H__

#include <uapi/linux/lnet/libcfs_debug.h>

/*
 *  Debugging
 */
extern unsigned int libcfs_subsystem_debug;
extern unsigned int libcfs_stack;
extern unsigned int libcfs_debug;
extern unsigned int libcfs_printk;
extern unsigned int libcfs_console_ratelimit;
extern unsigned int libcfs_console_max_delay;
extern unsigned int libcfs_console_min_delay;
extern unsigned int libcfs_console_backoff;
extern unsigned int libcfs_debug_binary;
extern char libcfs_debug_file_path_arr[PATH_MAX];

int libcfs_debug_mask2str(char *str, int size, int mask, int is_subsys);
int libcfs_debug_str2mask(int *mask, const char *str, int is_subsys);

/* Has there been an LBUG? */
extern unsigned int libcfs_catastrophe;
extern unsigned int libcfs_panic_on_lbug;

#ifndef DEBUG_SUBSYSTEM
# define DEBUG_SUBSYSTEM S_UNDEFINED
#endif

#define CDEBUG_DEFAULT_MAX_DELAY (cfs_time_seconds(600))	 /* jiffies */
#define CDEBUG_DEFAULT_MIN_DELAY ((cfs_time_seconds(1) + 1) / 2) /* jiffies */
#define CDEBUG_DEFAULT_BACKOFF   2
struct cfs_debug_limit_state {
	unsigned long   cdls_next;
	unsigned int cdls_delay;
	int	     cdls_count;
};

struct libcfs_debug_msg_data {
	const char *msg_file;
	const char *msg_fn;
	int	    msg_subsys;
	int	    msg_line;
	int	    msg_mask;
	struct cfs_debug_limit_state *msg_cdls;
};

#define LIBCFS_DEBUG_MSG_DATA_INIT(data, mask, cdls)		\
do {								\
	(data)->msg_subsys = DEBUG_SUBSYSTEM;			\
	(data)->msg_file   = __FILE__;				\
	(data)->msg_fn     = __func__;				\
	(data)->msg_line   = __LINE__;				\
	(data)->msg_cdls   = (cdls);				\
	(data)->msg_mask   = (mask);				\
} while (0)

#define LIBCFS_DEBUG_MSG_DATA_DECL(dataname, mask, cdls)	\
	static struct libcfs_debug_msg_data dataname = {	\
	       .msg_subsys = DEBUG_SUBSYSTEM,			\
	       .msg_file   = __FILE__,				\
	       .msg_fn     = __func__,				\
	       .msg_line   = __LINE__,				\
	       .msg_cdls   = (cdls)	 };			\
	dataname.msg_mask   = (mask)

/**
 * Filters out logging messages based on mask and subsystem.
 */
static inline int cfs_cdebug_show(unsigned int mask, unsigned int subsystem)
{
	return mask & D_CANTMASK ||
		((libcfs_debug & mask) && (libcfs_subsystem_debug & subsystem));
}

#define __CDEBUG(cdls, mask, format, ...)				\
do {									\
	static struct libcfs_debug_msg_data msgdata;			\
									\
	CFS_CHECK_STACK(&msgdata, mask, cdls);				\
									\
	if (cfs_cdebug_show(mask, DEBUG_SUBSYSTEM)) {			\
		LIBCFS_DEBUG_MSG_DATA_INIT(&msgdata, mask, cdls);	\
		libcfs_debug_msg(&msgdata, format, ## __VA_ARGS__);	\
	}								\
} while (0)

#define CDEBUG(mask, format, ...) __CDEBUG(NULL, mask, format, ## __VA_ARGS__)

#define CDEBUG_LIMIT(mask, format, ...)					\
do {									\
	static struct cfs_debug_limit_state cdls;			\
									\
	__CDEBUG(&cdls, mask, format, ## __VA_ARGS__);			\
} while (0)

#define CWARN(format, ...)	CDEBUG_LIMIT(D_WARNING, format, ## __VA_ARGS__)
#define CERROR(format, ...)	CDEBUG_LIMIT(D_ERROR, format, ## __VA_ARGS__)
#define CNETERR(format, a...)	CDEBUG_LIMIT(D_NETERROR, format, ## a)
#define CEMERG(format, ...)	CDEBUG_LIMIT(D_EMERG, format, ## __VA_ARGS__)

#define LCONSOLE(mask, format, ...) CDEBUG(D_CONSOLE | (mask), format, ## __VA_ARGS__)
#define LCONSOLE_INFO(format, ...)  CDEBUG_LIMIT(D_CONSOLE, format, ## __VA_ARGS__)
#define LCONSOLE_WARN(format, ...)  CDEBUG_LIMIT(D_CONSOLE | D_WARNING, format, ## __VA_ARGS__)
#define LCONSOLE_ERROR_MSG(errnum, format, ...) CDEBUG_LIMIT(D_CONSOLE | D_ERROR, \
			   "%x-%x: " format, errnum, LERRCHKSUM(errnum), ## __VA_ARGS__)
#define LCONSOLE_ERROR(format, ...) LCONSOLE_ERROR_MSG(0x00, format, ## __VA_ARGS__)

#define LCONSOLE_EMERG(format, ...) CDEBUG(D_CONSOLE | D_EMERG, format, ## __VA_ARGS__)

int libcfs_debug_msg(struct libcfs_debug_msg_data *msgdata,
		     const char *format1, ...)
	__printf(2, 3);

int libcfs_debug_vmsg2(struct libcfs_debug_msg_data *msgdata,
		       const char *format1,
		       va_list args, const char *format2, ...)
	__printf(4, 5);

/* other external symbols that tracefile provides: */
int cfs_trace_copyin_string(char *knl_buffer, int knl_buffer_nob,
			    const char __user *usr_buffer, int usr_buffer_nob);
int cfs_trace_copyout_string(char __user *usr_buffer, int usr_buffer_nob,
			     const char *knl_buffer, char *append);

#define LIBCFS_DEBUG_FILE_PATH_DEFAULT "/tmp/lustre-log"

#endif	/* __LIBCFS_DEBUG_H__ */
