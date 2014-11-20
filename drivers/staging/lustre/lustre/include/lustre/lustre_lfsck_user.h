/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.  A copy is
 * included in the COPYING file that accompanied this code.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * lustre/include/lustre/lustre_lfsck_user.h
 *
 * Lustre LFSCK userspace interfaces.
 *
 * Author: Fan Yong <yong.fan@whamcloud.com>
 */

#ifndef _LUSTRE_LFSCK_USER_H
# define _LUSTRE_LFSCK_USER_H

enum lfsck_param_flags {
	/* Reset LFSCK iterator position to the device beginning. */
	LPF_RESET       = 0x0001,

	/* Exit when fail. */
	LPF_FAILOUT     = 0x0002,

	/* Dryrun mode, only check without modification */
	LPF_DRYRUN      = 0x0004,
};

enum lfsck_type {
	/* For MDT-OST consistency check/repair. */
	LT_LAYOUT	= 0x0001,

	/* For MDT-MDT consistency check/repair. */
	LT_DNE		= 0x0002,

	/* For FID-in-dirent and linkEA consistency check/repair. */
	LT_NAMESPACE	= 0x0004,
};

#define LFSCK_VERSION_V1	1
#define LFSCK_VERSION_V2	2

#define LFSCK_TYPES_ALL		((__u16)(~0))
#define LFSCK_TYPES_DEF		((__u16)0)
#define LFSCK_TYPES_SUPPORTED	LT_NAMESPACE

#define LFSCK_SPEED_NO_LIMIT	0
#define LFSCK_SPEED_LIMIT_DEF	LFSCK_SPEED_NO_LIMIT

enum lfsck_start_valid {
	LSV_SPEED_LIMIT		= 0x00000001,
	LSV_ERROR_HANDLE	= 0x00000002,
	LSV_DRYRUN		= 0x00000004,
};

/* Arguments for starting lfsck. */
struct lfsck_start {
	/* Which arguments are valid, see 'enum lfsck_start_valid'. */
	__u32   ls_valid;

	/* How many items can be scanned at most per second. */
	__u32   ls_speed_limit;

	/* For compatibility between user space tools and kernel service. */
	__u16   ls_version;

	/* Which LFSCK components to be (have been) started. */
	__u16   ls_active;

	/* Flags for the LFSCK, see 'enum lfsck_param_flags'. */
	__u16   ls_flags;

	/* For 64-bits aligned. */
	__u16   ls_padding;
};

#endif /* _LUSTRE_LFSCK_USER_H */
