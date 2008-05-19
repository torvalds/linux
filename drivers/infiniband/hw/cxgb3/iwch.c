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
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <rdma/ib_verbs.h>

#include "cxgb3_offload.h"
#include "iwch_provider.h"
#include "iwch_user.h"
#include "iwch.h"
#include "iwch_cm.h"

#define DRV_VERSION "1.1"

MODULE_AUTHOR("Boyd Faulkner, Steve Wise");
MODULE_DESCRIPTION("Chelsio T3 RDMA Driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRV_VERSION);

cxgb3_cpl_handler_func t3c_handlers[NUM_CPL_CMDS];

static void open_rnic_dev(struct t3cdev *);
static void close_rnic_dev(struct t3cdev *);

struct cxgb3_client t3c_client = {
	.name = "iw_cxgb3",
	.add = open_rnic_dev,
	.remove = close_rnic_dev,
	.handlers = t3c_handlers,
	.redirect = iwch_ep_redirect
};

static LIST_HEAD(dev_list);
static DEFINE_MUTEX(dev_mutex);

static void rnic_init(struct iwch_dev *rnicp)
{
	PDBG("%s iwch_dev %p\n", __func__,  rnicp);
	idr_init(&rnicp->cqidr);
	idr_init(&rnicp->qpidr);
	idr_init(&rnicp->mmidr);
	spin_lock_init(&rnicp->lock);

	rnicp->attr.vendor_id = 0x168;
	rnicp->attr.vendor_part_id = 7;
	rnicp->attr.max_qps = T3_MAX_NUM_QP - 32;
	rnicp->attr.max_wrs = (1UL << 24) - 1;
	rnicp->attr.max_sge_per_wr = T3_MAX_SGE;
	rnicp->attr.max_sge_per_rdma_write_wr = T3_MAX_SGE;
	rnicp->attr.max_cqs = T3_MAX_NUM_CQ - 1;
	rnicp->attr.max_cqes_per_cq = (1UL << 24) - 1;
	rnicp->attr.max_mem_regs = cxio_num_stags(&rnicp->rdev);
	rnicp->attr.max_phys_buf_entries = T3_MAX_PBL_SIZE;
	rnicp->attr.max_pds = T3_MAX_NUM_PD - 1;
	rnicp->attr.mem_pgsizes_bitmask = 0x7FFF;	/* 4KB-128MB */
	rnicp->attr.max_mr_size = T3_MAX_MR_SIZE;
	rnicp->attr.can_resize_wq = 0;
	rnicp->attr.max_rdma_reads_per_qp = 8;
	rnicp->attr.max_rdma_read_resources =
	    rnicp->attr.max_rdma_reads_per_qp * rnicp->attr.max_qps;
	rnicp->attr.max_rdma_read_qp_depth = 8;	/* IRD */
	rnicp->attr.max_rdma_read_depth =
	    rnicp->attr.max_rdma_read_qp_depth * rnicp->attr.max_qps;
	rnicp->attr.rq_overflow_handled = 0;
	rnicp->attr.can_modify_ird = 0;
	rnicp->attr.can_modify_ord = 0;
	rnicp->attr.max_mem_windows = rnicp->attr.max_mem_regs - 1;
	rnicp->attr.stag0_value = 1;
	rnicp->attr.zbva_support = 1;
	rnicp->attr.local_invalidate_fence = 1;
	rnicp->attr.cq_overflow_detection = 1;
	return;
}

static void open_rnic_dev(struct t3cdev *tdev)
{
	struct iwch_dev *rnicp;
	static int vers_printed;

	PDBG("%s t3cdev %p\n", __func__,  tdev);
	if (!vers_printed++)
		printk(KERN_INFO MOD "Chelsio T3 RDMA Driver - version %s\n",
		       DRV_VERSION);
	rnicp = (struct iwch_dev *)ib_alloc_device(sizeof(*rnicp));
	if (!rnicp) {
		printk(KERN_ERR MOD "Cannot allocate ib device\n");
		return;
	}
	rnicp->rdev.ulp = rnicp;
	rnicp->rdev.t3cdev_p = tdev;

	mutex_lock(&dev_mutex);

	if (cxio_rdev_open(&rnicp->rdev)) {
		mutex_unlock(&dev_mutex);
		printk(KERN_ERR MOD "Unable to open CXIO rdev\n");
		ib_dealloc_device(&rnicp->ibdev);
		return;
	}

	rnic_init(rnicp);

	list_add_tail(&rnicp->entry, &dev_list);
	mutex_unlock(&dev_mutex);

	if (iwch_register_device(rnicp)) {
		printk(KERN_ERR MOD "Unable to register device\n");
		close_rnic_dev(tdev);
	}
	printk(KERN_INFO MOD "Initialized device %s\n",
	       pci_name(rnicp->rdev.rnic_info.pdev));
	return;
}

static void close_rnic_dev(struct t3cdev *tdev)
{
	struct iwch_dev *dev, *tmp;
	PDBG("%s t3cdev %p\n", __func__,  tdev);
	mutex_lock(&dev_mutex);
	list_for_each_entry_safe(dev, tmp, &dev_list, entry) {
		if (dev->rdev.t3cdev_p == tdev) {
			list_del(&dev->entry);
			iwch_unregister_device(dev);
			cxio_rdev_close(&dev->rdev);
			idr_destroy(&dev->cqidr);
			idr_destroy(&dev->qpidr);
			idr_destroy(&dev->mmidr);
			ib_dealloc_device(&dev->ibdev);
			break;
		}
	}
	mutex_unlock(&dev_mutex);
}

static int __init iwch_init_module(void)
{
	int err;

	err = cxio_hal_init();
	if (err)
		return err;
	err = iwch_cm_init();
	if (err)
		return err;
	cxio_register_ev_cb(iwch_ev_dispatch);
	cxgb3_register_client(&t3c_client);
	return 0;
}

static void __exit iwch_exit_module(void)
{
	cxgb3_unregister_client(&t3c_client);
	cxio_unregister_ev_cb(iwch_ev_dispatch);
	iwch_cm_term();
	cxio_hal_exit();
}

module_init(iwch_init_module);
module_exit(iwch_exit_module);
