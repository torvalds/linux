/*
 * CDDL HEADER SART
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
 * Copyright (c) 2013 Martin Matuska <mm@FreeBSD.org>. All rights reserved.
 */

#ifndef	_LIBZFS_COMPAT_H
#define	_LIBZFS_COMPAT_H

#include <zfs_ioctl_compat.h>

#ifdef	__cplusplus
extern "C" {
#endif

int get_zfs_ioctl_version(void);
int zcmd_ioctl(int fd, int request, zfs_cmd_t *zc);

#define	ioctl(fd, ioc, zc)	zcmd_ioctl((fd), (ioc), (zc))

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBZFS_COMPAT_H */
