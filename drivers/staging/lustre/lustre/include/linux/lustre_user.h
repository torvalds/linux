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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/include/linux/lustre_user.h
 *
 * Lustre public user-space interface definitions.
 */

#ifndef _LINUX_LUSTRE_USER_H
#define _LINUX_LUSTRE_USER_H

# include <linux/quota.h>

/*
 * asm-x86_64/processor.h on some SLES 9 distros seems to use
 * kernel-only typedefs.  fortunately skipping it altogether is ok
 * (for now).
 */
#define __ASM_X86_64_PROCESSOR_H

#include <linux/string.h>

/*
 * We need to always use 64bit version because the structure
 * is shared across entire cluster where 32bit and 64bit machines
 * are co-existing.
 */
#if __BITS_PER_LONG != 64 || defined(__ARCH_WANT_STAT64)
typedef struct stat64   lstat_t;
#define lstat_f	 lstat64
#else
typedef struct stat     lstat_t;
#define lstat_f	 lstat
#endif

#define HAVE_LOV_USER_MDS_DATA

#endif /* _LUSTRE_USER_H */
