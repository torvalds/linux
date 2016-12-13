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

#define CFS_CAP_CHOWN		   0
#define CFS_CAP_DAC_OVERRIDE	    1
#define CFS_CAP_DAC_READ_SEARCH	 2
#define CFS_CAP_FOWNER		  3
#define CFS_CAP_FSETID		  4
#define CFS_CAP_LINUX_IMMUTABLE	 9
#define CFS_CAP_SYS_ADMIN	      21
#define CFS_CAP_SYS_BOOT	       23
#define CFS_CAP_SYS_RESOURCE	   24

#define CFS_CAP_FS_MASK (BIT(CFS_CAP_CHOWN) |		\
			 BIT(CFS_CAP_DAC_OVERRIDE) |	\
			 BIT(CFS_CAP_DAC_READ_SEARCH) |	\
			 BIT(CFS_CAP_FOWNER) |		\
			 BIT(CFS_CAP_FSETID) |		\
			 BIT(CFS_CAP_LINUX_IMMUTABLE) | \
			 BIT(CFS_CAP_SYS_ADMIN) |	\
			 BIT(CFS_CAP_SYS_BOOT) |	\
			 BIT(CFS_CAP_SYS_RESOURCE))

void cfs_cap_raise(cfs_cap_t cap);
void cfs_cap_lower(cfs_cap_t cap);
int cfs_cap_raised(cfs_cap_t cap);
cfs_cap_t cfs_curproc_cap_pack(void);

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
