/*
 * Copyright (c) 2003, 2004, 2005, 2006 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _VERBS_DEBUG_H
#define _VERBS_DEBUG_H

/*
 * This file contains tracing code for the ib_ipath kernel module.
 */
#ifndef _VERBS_DEBUGGING	/* tracing enabled or not */
#define _VERBS_DEBUGGING 1
#endif

extern unsigned ib_ipath_debug;

#define _VERBS_ERROR(fmt,...) \
	do { \
		printk(KERN_ERR "%s: " fmt, "ib_ipath", ##__VA_ARGS__); \
	} while(0)

#define _VERBS_UNIT_ERROR(unit,fmt,...) \
	do { \
		printk(KERN_ERR "%s: " fmt, "ib_ipath", ##__VA_ARGS__); \
	} while(0)

#if _VERBS_DEBUGGING

/*
 * Mask values for debugging.  The scheme allows us to compile out any
 * of the debug tracing stuff, and if compiled in, to enable or
 * disable dynamically.
 * This can be set at modprobe time also:
 *      modprobe ib_path ib_ipath_debug=3
 */

#define __VERBS_INFO        0x1	/* generic low verbosity stuff */
#define __VERBS_DBG         0x2	/* generic debug */
#define __VERBS_VDBG        0x4	/* verbose debug */
#define __VERBS_SMADBG      0x8000	/* sma packet debug */

#define _VERBS_INFO(fmt,...) \
	do { \
		if (unlikely(ib_ipath_debug&__VERBS_INFO)) \
			printk(KERN_INFO "%s: " fmt,"ib_ipath", \
			       ##__VA_ARGS__); \
	} while(0)

#define _VERBS_DBG(fmt,...) \
	do { \
		if (unlikely(ib_ipath_debug&__VERBS_DBG)) \
			printk(KERN_DEBUG "%s: " fmt, __func__, \
			       ##__VA_ARGS__); \
	} while(0)

#define _VERBS_VDBG(fmt,...) \
	do { \
		if (unlikely(ib_ipath_debug&__VERBS_VDBG)) \
			printk(KERN_DEBUG "%s: " fmt, __func__, \
			       ##__VA_ARGS__); \
	} while(0)

#define _VERBS_SMADBG(fmt,...) \
	do { \
		if (unlikely(ib_ipath_debug&__VERBS_SMADBG)) \
			printk(KERN_DEBUG "%s: " fmt, __func__, \
			       ##__VA_ARGS__); \
	} while(0)

#else /* ! _VERBS_DEBUGGING */

#define _VERBS_INFO(fmt,...)
#define _VERBS_DBG(fmt,...)
#define _VERBS_VDBG(fmt,...)
#define _VERBS_SMADBG(fmt,...)

#endif /* _VERBS_DEBUGGING */

#endif /* _VERBS_DEBUG_H */
