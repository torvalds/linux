/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 */
#ifndef ssc_h
#define ssc_h

/* Simulator system calls: */

#define SSC_CONSOLE_INIT		20
#define SSC_GETCHAR			21
#define SSC_PUTCHAR			31
#define SSC_OPEN			50
#define SSC_CLOSE			51
#define SSC_READ			52
#define SSC_WRITE			53
#define SSC_GET_COMPLETION		54
#define SSC_WAIT_COMPLETION		55
#define SSC_CONNECT_INTERRUPT		58
#define SSC_GENERATE_INTERRUPT		59
#define SSC_SET_PERIODIC_INTERRUPT	60
#define SSC_GET_RTC			65
#define SSC_EXIT			66
#define SSC_LOAD_SYMBOLS		69
#define SSC_GET_TOD			74

#define SSC_GET_ARGS			75

/*
 * Simulator system call.
 */
extern long ssc (long arg0, long arg1, long arg2, long arg3, int nr);

#endif /* ssc_h */
