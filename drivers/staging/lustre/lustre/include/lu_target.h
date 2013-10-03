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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef _LUSTRE_LU_TARGET_H
#define _LUSTRE_LU_TARGET_H

#include <dt_object.h>
#include <lustre_disk.h>

struct lu_target {
	struct obd_device       *lut_obd;
	struct dt_device	*lut_bottom;
	/** last_rcvd file */
	struct dt_object	*lut_last_rcvd;
	/* transaction callbacks */
	struct dt_txn_callback   lut_txn_cb;
	/** server data in last_rcvd file */
	struct lr_server_data    lut_lsd;
	/** Server last transaction number */
	__u64		    lut_last_transno;
	/** Lock protecting last transaction number */
	spinlock_t		 lut_translock;
	/** Lock protecting client bitmap */
	spinlock_t		 lut_client_bitmap_lock;
	/** Bitmap of known clients */
	unsigned long	   *lut_client_bitmap;
};

typedef void (*tgt_cb_t)(struct lu_target *lut, __u64 transno,
			 void *data, int err);
struct tgt_commit_cb {
	tgt_cb_t  tgt_cb_func;
	void     *tgt_cb_data;
};

void tgt_boot_epoch_update(struct lu_target *lut);
int tgt_last_commit_cb_add(struct thandle *th, struct lu_target *lut,
			   struct obd_export *exp, __u64 transno);
int tgt_new_client_cb_add(struct thandle *th, struct obd_export *exp);
int tgt_init(const struct lu_env *env, struct lu_target *lut,
	     struct obd_device *obd, struct dt_device *dt);
void tgt_fini(const struct lu_env *env, struct lu_target *lut);
int tgt_client_alloc(struct obd_export *exp);
void tgt_client_free(struct obd_export *exp);
int tgt_client_del(const struct lu_env *env, struct obd_export *exp);
int tgt_client_add(const struct lu_env *env, struct obd_export *exp, int);
int tgt_client_new(const struct lu_env *env, struct obd_export *exp);
int tgt_client_data_read(const struct lu_env *env, struct lu_target *tg,
			 struct lsd_client_data *lcd, loff_t *off, int index);
int tgt_client_data_write(const struct lu_env *env, struct lu_target *tg,
			  struct lsd_client_data *lcd, loff_t *off, struct thandle *th);
int tgt_server_data_read(const struct lu_env *env, struct lu_target *tg);
int tgt_server_data_write(const struct lu_env *env, struct lu_target *tg,
			  struct thandle *th);
int tgt_server_data_update(const struct lu_env *env, struct lu_target *tg, int sync);
int tgt_truncate_last_rcvd(const struct lu_env *env, struct lu_target *tg, loff_t off);

#endif /* __LUSTRE_LU_TARGET_H */
