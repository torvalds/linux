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
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Internal cl interfaces.
 *
 *   Author: Nikita Danilov <nikita.danilov@sun.com>
 */
#ifndef _CL_INTERNAL_H
#define _CL_INTERNAL_H

#define CLT_PVEC_SIZE (14)

/**
 * Possible levels of the nesting. Currently this is 2: there are "top"
 * entities (files, extent locks), and "sub" entities (stripes and stripe
 * locks). This is used only for debugging counters right now.
 */
enum clt_nesting_level {
	CNL_TOP,
	CNL_SUB,
	CNL_NR
};

/**
 * Thread local state internal for generic cl-code.
 */
struct cl_thread_info {
	/*
	 * Common fields.
	 */
	struct cl_io	 clt_io;
	struct cl_2queue     clt_queue;

	/*
	 * Fields used by cl_lock.c
	 */
	struct cl_lock_descr clt_descr;
	struct cl_page_list  clt_list;
	/** @} debugging */

	/*
	 * Fields used by cl_page.c
	 */
	struct cl_page      *clt_pvec[CLT_PVEC_SIZE];

	/*
	 * Fields used by cl_io.c
	 */
	/**
	 * Pointer to the topmost ongoing IO in this thread.
	 */
	struct cl_io	*clt_current_io;
	/**
	 * Used for submitting a sync io.
	 */
	struct cl_sync_io    clt_anchor;
	/**
	 * Fields used by cl_lock_discard_pages().
	 */
	pgoff_t	      clt_next_index;
	pgoff_t	      clt_fn_index; /* first non-overlapped index */
};

struct cl_thread_info *cl_env_info(const struct lu_env *env);

#endif /* _CL_INTERNAL_H */
