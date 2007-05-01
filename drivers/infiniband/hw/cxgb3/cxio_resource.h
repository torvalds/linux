/*
 * Copyright (c) 2006 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef __CXIO_RESOURCE_H__
#define __CXIO_RESOURCE_H__

#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/genalloc.h>
#include "cxio_hal.h"

extern int cxio_hal_init_rhdl_resource(u32 nr_rhdl);
extern void cxio_hal_destroy_rhdl_resource(void);
extern int cxio_hal_init_resource(struct cxio_rdev *rdev_p,
				  u32 nr_tpt, u32 nr_pbl,
				  u32 nr_rqt, u32 nr_qpid, u32 nr_cqid,
				  u32 nr_pdid);
extern u32 cxio_hal_get_stag(struct cxio_hal_resource *rscp);
extern void cxio_hal_put_stag(struct cxio_hal_resource *rscp, u32 stag);
extern u32 cxio_hal_get_qpid(struct cxio_hal_resource *rscp);
extern void cxio_hal_put_qpid(struct cxio_hal_resource *rscp, u32 qpid);
extern u32 cxio_hal_get_cqid(struct cxio_hal_resource *rscp);
extern void cxio_hal_put_cqid(struct cxio_hal_resource *rscp, u32 cqid);
extern void cxio_hal_destroy_resource(struct cxio_hal_resource *rscp);

#define PBL_OFF(rdev_p, a) ( (a) - (rdev_p)->rnic_info.pbl_base )
extern int cxio_hal_pblpool_create(struct cxio_rdev *rdev_p);
extern void cxio_hal_pblpool_destroy(struct cxio_rdev *rdev_p);
extern u32 cxio_hal_pblpool_alloc(struct cxio_rdev *rdev_p, int size);
extern void cxio_hal_pblpool_free(struct cxio_rdev *rdev_p, u32 addr, int size);

#define RQT_OFF(rdev_p, a) ( (a) - (rdev_p)->rnic_info.rqt_base )
extern int cxio_hal_rqtpool_create(struct cxio_rdev *rdev_p);
extern void cxio_hal_rqtpool_destroy(struct cxio_rdev *rdev_p);
extern u32 cxio_hal_rqtpool_alloc(struct cxio_rdev *rdev_p, int size);
extern void cxio_hal_rqtpool_free(struct cxio_rdev *rdev_p, u32 addr, int size);
#endif
