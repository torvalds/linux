// SPDX-License-Identifier: GPL-2.0
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
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/include/libcfs/curproc.h
 *
 * Lustre curproc API declaration
 *
 * Author: Nikita Danilov <nikita@clusterfs.com>
 */

#ifndef __LIBCFS_CURPROC_H__
#define __LIBCFS_CURPROC_H__

/*
 * Plus, platform-specific constant
 *
 * CFS_CURPROC_COMM_MAX,
 *
 * and opaque scalar type
 *
 * kernel_cap_t
 */

/* check if task is running in compat mode.*/
#define current_pid()		(current->pid)
#define current_comm()		(current->comm)

typedef u32 cfs_cap_t;

#define CFS_CAP_FS_MASK (BIT(CAP_CHOWN) |		\
			 BIT(CAP_DAC_OVERRIDE) |	\
			 BIT(CAP_DAC_READ_SEARCH) |	\
			 BIT(CAP_FOWNER) |		\
			 BIT(CAP_FSETID) |		\
			 BIT(CAP_LINUX_IMMUTABLE) | \
			 BIT(CAP_SYS_ADMIN) |	\
			 BIT(CAP_SYS_BOOT) |	\
			 BIT(CAP_SYS_RESOURCE))

static inline cfs_cap_t cfs_curproc_cap_pack(void)
{
	/* cfs_cap_t is only the first word of kernel_cap_t */
	return (cfs_cap_t)(current_cap().cap[0]);
}

/* __LIBCFS_CURPROC_H__ */
#endif
/*
 * Local variables:
 * c-indentation-style: "K&R"
 * c-basic-offset: 8
 * tab-width: 8
 * fill-column: 80
 * scroll-step: 1
 * End:
 */
