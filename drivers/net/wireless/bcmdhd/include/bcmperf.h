/*
 * Performance counters software interface.
 *
 * Copyright (C) 1999-2015, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: bcmperf.h 241182 2011-02-17 21:50:03Z $
 */
/* essai */
#ifndef _BCMPERF_H_
#define _BCMPERF_H_
/* get cache hits and misses */
#define BCMPERF_ENABLE_INSTRCOUNT()
#define BCMPERF_ENABLE_ICACHE_MISS()
#define BCMPERF_ENABLE_ICACHE_HIT()
#define	BCMPERF_GETICACHE_MISS(x)	((x) = 0)
#define	BCMPERF_GETICACHE_HIT(x)	((x) = 0)
#define	BCMPERF_GETINSTRCOUNT(x)	((x) = 0)
#endif /* _BCMPERF_H_ */
