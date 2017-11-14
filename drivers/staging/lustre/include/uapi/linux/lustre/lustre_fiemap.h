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
 * Copyright (c) 2014, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * FIEMAP data structures and flags. This header file will be used until
 * fiemap.h is available in the upstream kernel.
 *
 * Author: Kalpak Shah <kalpak.shah@sun.com>
 * Author: Andreas Dilger <adilger@sun.com>
 */

#ifndef _LUSTRE_FIEMAP_H
#define _LUSTRE_FIEMAP_H

#include <stddef.h>
#include <linux/fiemap.h>

/* XXX: We use fiemap_extent::fe_reserved[0] */
#define fe_device	fe_reserved[0]

static inline size_t fiemap_count_to_size(size_t extent_count)
{
	return sizeof(struct fiemap) + extent_count *
				       sizeof(struct fiemap_extent);
}

static inline unsigned fiemap_size_to_count(size_t array_size)
{
	return (array_size - sizeof(struct fiemap)) /
		sizeof(struct fiemap_extent);
}

#define FIEMAP_FLAG_DEVICE_ORDER 0x40000000 /* return device ordered mapping */

#ifdef FIEMAP_FLAGS_COMPAT
#undef FIEMAP_FLAGS_COMPAT
#endif

/* Lustre specific flags - use a high bit, don't conflict with upstream flag */
#define FIEMAP_EXTENT_NO_DIRECT	 0x40000000 /* Data mapping undefined */
#define FIEMAP_EXTENT_NET	 0x80000000 /* Data stored remotely.
					     * Sets NO_DIRECT flag
					     */

#endif /* _LUSTRE_FIEMAP_H */
