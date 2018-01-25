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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __LLOG_INTERNAL_H__
#define __LLOG_INTERNAL_H__

#include <lustre_log.h>

struct llog_process_info {
	struct llog_handle *lpi_loghandle;
	llog_cb_t	   lpi_cb;
	void	       *lpi_cbdata;
	void	       *lpi_catdata;
	int		 lpi_rc;
	struct completion	lpi_completion;
	const struct lu_env	*lpi_env;

};

struct llog_thread_info {
	struct lu_attr			 lgi_attr;
	struct lu_fid			 lgi_fid;
	struct lu_buf			 lgi_buf;
	loff_t				 lgi_off;
	struct llog_rec_hdr		 lgi_lrh;
	struct llog_rec_tail		 lgi_tail;
};

extern struct lu_context_key llog_thread_key;

int llog_info_init(void);
void llog_info_fini(void);

void llog_handle_get(struct llog_handle *loghandle);
void llog_handle_put(struct llog_handle *loghandle);
int class_config_dump_handler(const struct lu_env *env,
			      struct llog_handle *handle,
			      struct llog_rec_hdr *rec, void *data);
int llog_process_or_fork(const struct lu_env *env,
			 struct llog_handle *loghandle,
			 llog_cb_t cb, void *data, void *catdata, bool fork);
int llog_cat_cleanup(const struct lu_env *env, struct llog_handle *cathandle,
		     struct llog_handle *loghandle, int index);

static inline struct llog_rec_hdr *llog_rec_hdr_next(struct llog_rec_hdr *rec)
{
	return (struct llog_rec_hdr *)((char *)rec + rec->lrh_len);
}
#endif
