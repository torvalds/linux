/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Common routines for acquiring snapshots of kstats for
 * iostat, mpstat, and vmstat.
 */

#ifndef	_STATCOMMON_H
#define	_STATCOMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <sys/types.h>
#include <time.h>

#define	NODATE	0	/* Default:  No time stamp */
#define	DDATE	1	/* Standard date format */
#define	UDATE	2	/* Internal representation of Unix time */

/* Print a timestamp in either Unix or standard format. */
void print_timestamp(uint_t);

#ifdef __cplusplus
}
#endif

#endif /* _STATCOMMON_H */
