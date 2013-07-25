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
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/fid/fid_internal.h
 *
 * Author: Yury Umanets <umka@clusterfs.com>
 */
#ifndef __FID_INTERNAL_H
#define __FID_INTERNAL_H

#include <lustre/lustre_idl.h>
#include <dt_object.h>

#include <linux/libcfs/libcfs.h>

struct seq_thread_info {
	struct req_capsule     *sti_pill;
	struct lu_seq_range     sti_space;
	struct lu_buf	   sti_buf;
};

enum {
	SEQ_TXN_STORE_CREDITS = 20
};

extern struct lu_context_key seq_thread_key;

int seq_client_alloc_super(struct lu_client_seq *seq,
			   const struct lu_env *env);
/* Store API functions. */
int seq_store_init(struct lu_server_seq *seq,
		   const struct lu_env *env,
		   struct dt_device *dt);

void seq_store_fini(struct lu_server_seq *seq,
		    const struct lu_env *env);

int seq_store_read(struct lu_server_seq *seq,
		   const struct lu_env *env);

int seq_store_update(const struct lu_env *env, struct lu_server_seq *seq,
		     struct lu_seq_range *out, int sync);

#ifdef LPROCFS
extern struct lprocfs_vars seq_server_proc_list[];
extern struct lprocfs_vars seq_client_proc_list[];
#endif


extern proc_dir_entry_t *seq_type_proc_dir;

#endif /* __FID_INTERNAL_H */
