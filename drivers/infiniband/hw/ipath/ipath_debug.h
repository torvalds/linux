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

#ifndef _IPATH_DEBUG_H
#define _IPATH_DEBUG_H

#ifndef _IPATH_DEBUGGING	/* debugging enabled or not */
#define _IPATH_DEBUGGING 1
#endif

#if _IPATH_DEBUGGING

/*
 * Mask values for debugging.  The scheme allows us to compile out any
 * of the debug tracing stuff, and if compiled in, to enable or disable
 * dynamically.  This can be set at modprobe time also:
 *      modprobe infinipath.ko infinipath_debug=7
 */

#define __IPATH_INFO        0x1	/* generic low verbosity stuff */
#define __IPATH_DBG         0x2	/* generic debug */
#define __IPATH_TRSAMPLE    0x8	/* generate trace buffer sample entries */
/* leave some low verbosity spots open */
#define __IPATH_VERBDBG     0x40	/* very verbose debug */
#define __IPATH_PKTDBG      0x80	/* print packet data */
/* print process startup (init)/exit messages */
#define __IPATH_PROCDBG     0x100
/* print mmap/nopage stuff, not using VDBG any more */
#define __IPATH_MMDBG       0x200
#define __IPATH_USER_SEND   0x1000	/* use user mode send */
#define __IPATH_KERNEL_SEND 0x2000	/* use kernel mode send */
#define __IPATH_EPKTDBG     0x4000	/* print ethernet packet data */
#define __IPATH_SMADBG      0x8000	/* sma packet debug */
#define __IPATH_IPATHDBG    0x10000	/* Ethernet (IPATH) gen debug */
#define __IPATH_IPATHWARN   0x20000	/* Ethernet (IPATH) warnings */
#define __IPATH_IPATHERR    0x40000	/* Ethernet (IPATH) errors */
#define __IPATH_IPATHPD     0x80000	/* Ethernet (IPATH) packet dump */
#define __IPATH_IPATHTABLE  0x100000	/* Ethernet (IPATH) table dump */

#else				/* _IPATH_DEBUGGING */

/*
 * define all of these even with debugging off, for the few places that do
 * if(infinipath_debug & _IPATH_xyzzy), but in a way that will make the
 * compiler eliminate the code
 */

#define __IPATH_INFO      0x0	/* generic low verbosity stuff */
#define __IPATH_DBG       0x0	/* generic debug */
#define __IPATH_TRSAMPLE  0x0	/* generate trace buffer sample entries */
#define __IPATH_VERBDBG   0x0	/* very verbose debug */
#define __IPATH_PKTDBG    0x0	/* print packet data */
#define __IPATH_PROCDBG   0x0	/* process startup (init)/exit messages */
/* print mmap/nopage stuff, not using VDBG any more */
#define __IPATH_MMDBG     0x0
#define __IPATH_EPKTDBG   0x0	/* print ethernet packet data */
#define __IPATH_SMADBG    0x0   /* process startup (init)/exit messages */
#define __IPATH_IPATHDBG  0x0	/* Ethernet (IPATH) table dump on */
#define __IPATH_IPATHWARN 0x0	/* Ethernet (IPATH) warnings on   */
#define __IPATH_IPATHERR  0x0	/* Ethernet (IPATH) errors on   */
#define __IPATH_IPATHPD   0x0	/* Ethernet (IPATH) packet dump on   */
#define __IPATH_IPATHTABLE 0x0	/* Ethernet (IPATH) packet dump on   */

#endif				/* _IPATH_DEBUGGING */

#define __IPATH_VERBOSEDBG __IPATH_VERBDBG

#endif				/* _IPATH_DEBUG_H */
