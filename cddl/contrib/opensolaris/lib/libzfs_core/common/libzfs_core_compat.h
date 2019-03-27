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
 * Copyright (c) 2013 by Martin Matuska <mm@FreeBSD.org>. All rights reserved.
 */

#ifndef	_LIBZFS_CORE_COMPAT_H
#define	_LIBZFS_CORE_COMPAT_H

#include <libnvpair.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ioctl.h>

#ifdef	__cplusplus
extern "C" {
#endif

int lzc_compat_pre(zfs_cmd_t *, zfs_ioc_t *, nvlist_t **);
void lzc_compat_post(zfs_cmd_t *, const zfs_ioc_t);
int lzc_compat_outnvl(zfs_cmd_t *, const zfs_ioc_t, nvlist_t **);

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBZFS_CORE_COMPAT_H */
