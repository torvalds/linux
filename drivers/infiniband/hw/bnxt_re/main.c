/*
 * Broadcom NetXtreme-E RoCE driver.
 *
 * Copyright (c) 2016 - 2017, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Description: Main component of the bnxt_re driver
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <net/dcbnl.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <linux/if_ether.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_addr.h>

#include "bnxt_ulp.h"
#include "roce_hsi.h"
#include "qplib_res.h"
#include "qplib_sp.h"
#include "qplib_fp.h"
#include "qplib_rcfw.h"
#include "bnxt_re.h"
#include "ib_verbs.h"
#include <rdma/bnxt_re-abi.h>
#include "bnxt.h"
#include "hw_counters.h"

static char version[] =
		BNXT_RE_DESC "\n";

MODULE_AUTHOR("Eddie Wai <eddie.wai@broadcom.com>");
MODULE_DESCRIPTION(BNXT_RE_DESC " Driver");
MODULE_LICENSE("Dual BSD/GPL");

/* globals */
static struct list_head bnxt_re_dev_list = LIST_HEAD_INIT(bnxt_re_dev_list);
/* Mutex to protect the list of bnxt_re devices added */
static DEFINE_MUTEX(bnxt_re_dev_lock);
static struct workqueue_struct *bnxt_re_wq;
static void bnxt_re_remove_device(struct bnxt_re_dev *rdev);
static void bnxt_re_dealloc_driver(struct ib_device *ib_dev);
static void bnxt_re_stop_irq(void *handle);
static void bnxt_re_dev_stop(struct bnxt_re_dev *rdev);

static void bnxt_re_set_drv_mode(struct bnxt_re_dev *rdev, u8 mode)
{
	struct bnxt_qplib_chip_ctx *cctx;

	cctx = rdev->chip_ctx;
	cctx->modes.wqe_mode = bnxt_qplib_is_chip_gen_p5(rdev->chip_ctx) ?
			       mode : BNXT_QPLIB_WQE_MODE_STATIC;
}

static void bnxt_re_destroy_chip_ctx(struct bnxt_re_dev *rdev)
{
	struct bnxt_qplib_chip_ctx *chip_ctx;

	if (!rdev->chip_ctx)
		return;
	chip_ctx = rdev->chip_ctx;
	rdev->chip_ctx = NULL;
	rdev->rcfw.res = NULL;
	rdev->qplib_res.cctx = NULL;
	rdev->qplib_res.pdev = NULL;
	rdev->qplib_res.netdev = NULL;
	kfree(chip_ctx);
}

static int bnxt_re_setup_chip_ctx(struct bnxt_re_dev *rdev, u8 wqe_mode)
{
	struct bnxt_qplib_chip_ctx *chip_ctx;
	struct bnxt_en_dev *en_dev;
	struct bnxt *bp;

	en_dev = rdev->en_dev;
	bp = netdev_priv(en_dev->net);

	chip_ctx = kzalloc(sizeof(*chip_ctx), GFP_KERNEL);
	if (!chip_ctx)
		return -ENOMEM;
	chip_ctx->chip_num = bp->chip_num;
	chip_ctx->hw_stats_size = bp->hw_ring_stats_size;

	rdev->chip_ctx = chip_ctx;
	/* rest members to follow eventually */

	rdev->qplib_res.cctx = rdev->chip_ctx;
	rdev->rcfw.res = &rdev->qplib_res;
	rdev->qplib_res.dattr = &rdev->dev_attr;
	rdev->qplib_res.is_vf = BNXT_VF(bp);

	bnxt_re_set_drv_mode(rdev, wqe_mode);
	if (bnxt_qplib_determine_atomics(en_dev->pdev))
		ibdev_info(&rdev->ibdev,
			   "platform doesn't support global atomics.");
	return 0;
}

/* SR-IOV helper functions */

static void bnxt_re_get_sriov_func_type(struct bnxt_re_dev *rdev)
{
	struct bnxt *bp;

	bp = netdev_priv(rdev->en_dev->net);
	if (BNXT_VF(bp))
		rdev->is_virtfn = 1;
}

/* Set the maximum number of each resource that the driver actually wants
 * to allocate. This may be up to the maximum number the firmware has
 * reserved for the function. The driver may choose to allocate fewer
 * resources than the firmware maximum.
 */
static void bnxt_re_limit_pf_res(struct bnxt_re_dev *rdev)
{
	struct bnxt_qplib_dev_attr *attr;
	struct bnxt_qplib_ctx *ctx;
	int i;

	attr = &rdev->dev_attr;
	ctx = &rdev->qplib_ctx;

	ctx->qpc_count = min_t(u32, BNXT_RE_MAX_QPC_COUNT,
			       attr->max_qp);
	ctx->mrw_count = BNXT_RE_MAX_MRW_COUNT_256K;
	/* Use max_mr from fw since max_mrw does not get set */
	ctx->mrw_count = min_t(u32, ctx->mrw_count, attr->max_mr);
	ctx->srqc_count = min_t(u32, BNXT_RE_MAX_SRQC_COUNT,
				attr->max_srq);
	ctx->cq_count = min_t(u32, BNXT_RE_MAX_CQ_COUNT, attr->max_cq);
	if (!bnxt_qplib_is_chip_gen_p5(rdev->chip_ctx))
		for (i = 0; i < MAX_TQM_ALLOC_REQ; i++)
			rdev->qplib_ctx.tqm_ctx.qcount[i] =
			rdev->dev_attr.tqm_alloc_reqs[i];
}

static void bnxt_re_limit_vf_res(struct bnxt_qplib_ctx *qplib_ctx, u32 num_vf)
{
	struct bnxt_qplib_vf_res *vf_res;
	u32 mrws = 0;
	u32 vf_pct;
	u32 nvfs;

	vf_res = &qplib_ctx->vf_res;
	/*
	 * Reserve a set of resources for the PF. Divide the remaining
	 * resources among the VFs
	 */
	vf_pct = 100 - BNXT_RE_PCT_RSVD_FOR_PF;
	nvfs = num_vf;
	num_vf = 100 * num_vf;
	vf_res->max_qp_per_vf = (qplib_ctx->qpc_count * vf_pct) / num_vf;
	vf_res->max_srq_per_vf = (qplib_ctx->srqc_count * vf_pct) / num_vf;
	vf_res->max_cq_per_vf = (qplib_ctx->cq_count * vf_pct) / num_vf;
	/*
	 * The driver allows many more MRs than other resources. If the
	 * firmware does also, then reserve a fixed amount for the PF and
	 * divide the rest among VFs. VFs may use many MRs for NFS
	 * mounts, ISER, NVME applications, etc. If the firmware severely
	 * restricts the number of MRs, then let PF have half and divide
	 * the rest among VFs, as for the other resource types.
	 */
	if (qplib_ctx->mrw_count < BNXT_RE_MAX_MRW_COUNT_64K) {
		mrws = qplib_ctx->mrw_count * vf_pct;
		nvfs = num_vf;
	} else {
		mrws = qplib_ctx->mrw_count - BNXT_RE_RESVD_MR_FOR_PF;
	}
	vf_res->max_mrw_per_vf = (mrws / nvfs);
	vf_res->max_gid_per_vf = BNXT_RE_MAX_GID_PER_VF;
}

static void bnxt_re_set_resource_limits(struct bnxt_re_dev *rdev)
{
	u32 num_vfs;

	memset(&rdev->qplib_ctx.vf_res, 0, sizeof(struct bnxt_qplib_vf_res));
	bnxt_re_limit_pf_res(rdev);

	num_vfs =  bnxt_qplib_is_chip_gen_p5(rdev->chip_ctx) ?
			BNXT_RE_GEN_P5_MAX_VF : rdev->num_vfs;
	if (num_vfs)
		bnxt_re_limit_vf_res(&rdev->qplib_ctx, num_vfs);
}

/* for handling bnxt_en callbacks later */
static void bnxt_re_stop(void *p)
{
	struct bnxt_re_dev *rdev = p;
	struct bnxt *bp;

	if (!rdev)
		return;
	ASSERT_RTNL();

	/* L2 driver invokes this callback during device error/crash or device
	 * reset. Current RoCE driver doesn't recover the device in case of
	 * error. Handle the error by dispatching fatal events to all qps
	 * ie. by calling bnxt_re_dev_stop and release the MSIx vectors as
	 * L2 driver want to modify the MSIx table.
	 */
	bp = netdev_priv(rdev->netdev);

	ibdev_info(&rdev->ibdev, "Handle device stop call from L2 driver");
	/* Check the current device state from L2 structure and move the
	 * device to detached state if FW_FATAL_COND is set.
	 * This prevents more commands to HW during clean-up,
	 * in case the device is already in error.
	 */
	if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state))
		set_bit(ERR_DEVICE_DETACHED, &rdev->rcfw.cmdq.flags);

	bnxt_re_dev_stop(rdev);
	bnxt_re_stop_irq(rdev);
	/* Move the device states to detached and  avoid sending any more
	 * commands to HW
	 */
	set_bit(BNXT_RE_FLAG_ERR_DEVICE_DETACHED, &rdev->flags);
	set_bit(ERR_DEVICE_DETACHED, &rdev->rcfw.cmdq.flags);
}

static void bnxt_re_start(void *p)
{
}

static void bnxt_re_sriov_config(void *p, int num_vfs)
{
	struct bnxt_re_dev *rdev = p;

	if (!rdev)
		return;

	if (test_bit(BNXT_RE_FLAG_ERR_DEVICE_DETACHED, &rdev->flags))
		return;
	rdev->num_vfs = num_vfs;
	if (!bnxt_qplib_is_chip_gen_p5(rdev->chip_ctx)) {
		bnxt_re_set_resource_limits(rdev);
		bnxt_qplib_set_func_resources(&rdev->qplib_res, &rdev->rcfw,
					      &rdev->qplib_ctx);
	}
}

static void bnxt_re_shutdown(void *p)
{
	struct bnxt_re_dev *rdev = p;

	if (!rdev)
		return;
	ASSERT_RTNL();
	/* Release the MSIx vectors before queuing unregister */
	bnxt_re_stop_irq(rdev);
	ib_unregister_device_queued(&rdev->ibdev);
}

static void bnxt_re_stop_irq(void *handle)
{
	struct bnxt_re_dev *rdev = (struct bnxt_re_dev *)handle;
	struct bnxt_qplib_rcfw *rcfw = &rdev->rcfw;
	struct bnxt_qplib_nq *nq;
	int indx;

	for (indx = BNXT_RE_NQ_IDX; indx < rdev->num_msix; indx++) {
		nq = &rdev->nq[indx - 1];
		bnxt_qplib_nq_stop_irq(nq, false);
	}

	bnxt_qplib_rcfw_stop_irq(rcfw, false);
}

static void bnxt_re_start_irq(void *handle, struct bnxt_msix_entry *ent)
{
	struct bnxt_re_dev *rdev = (struct bnxt_re_dev *)handle;
	struct bnxt_msix_entry *msix_ent = rdev->msix_entries;
	struct bnxt_qplib_rcfw *rcfw = &rdev->rcfw;
	struct bnxt_qplib_nq *nq;
	int indx, rc;

	if (!ent) {
		/* Not setting the f/w timeout bit in rcfw.
		 * During the driver unload the first command
		 * to f/w will timeout and that will set the
		 * timeout bit.
		 */
		ibdev_err(&rdev->ibdev, "Failed to re-start IRQs\n");
		return;
	}

	/* Vectors may change after restart, so update with new vectors
	 * in device sctructure.
	 */
	for (indx = 0; indx < rdev->num_msix; indx++)
		rdev->msix_entries[indx].vector = ent[indx].vector;

	bnxt_qplib_rcfw_start_irq(rcfw, msix_ent[BNXT_RE_AEQ_IDX].vector,
				  false);
	for (indx = BNXT_RE_NQ_IDX ; indx < rdev->num_msix; indx++) {
		nq = &rdev->nq[indx - 1];
		rc = bnxt_qplib_nq_start_irq(nq, indx - 1,
					     msix_ent[indx].vector, false);
		if (rc)
			ibdev_warn(&rdev->ibdev, "Failed to reinit NQ index %d\n",
				   indx - 1);
	}
}

static struct bnxt_ulp_ops bnxt_re_ulp_ops = {
	.ulp_async_notifier = NULL,
	.ulp_stop = bnxt_re_stop,
	.ulp_start = bnxt_re_start,
	.ulp_sriov_config = bnxt_re_sriov_config,
	.ulp_shutdown = bnxt_re_shutdown,
	.ulp_irq_stop = bnxt_re_stop_irq,
	.ulp_irq_restart = bnxt_re_start_irq
};

/* RoCE -> Net driver */

/* Driver registration routines used to let the networking driver (bnxt_en)
 * to know that the RoCE driver is now installed
 */
static int bnxt_re_unregister_netdev(struct bnxt_re_dev *rdev)
{
	struct bnxt_en_dev *en_dev;
	int rc;

	if (!rdev)
		return -EINVAL;

	en_dev = rdev->en_dev;

	rc = en_dev->en_ops->bnxt_unregister_device(rdev->en_dev,
						    BNXT_ROCE_ULP);
	return rc;
}

static int bnxt_re_register_netdev(struct bnxt_re_dev *rdev)
{
	struct bnxt_en_dev *en_dev;
	int rc = 0;

	if (!rdev)
		return -EINVAL;

	en_dev = rdev->en_dev;

	rc = en_dev->en_ops->bnxt_register_device(en_dev, BNXT_ROCE_ULP,
						  &bnxt_re_ulp_ops, rdev);
	rdev->qplib_res.pdev = rdev->en_dev->pdev;
	return rc;
}

static int bnxt_re_free_msix(struct bnxt_re_dev *rdev)
{
	struct bnxt_en_dev *en_dev;
	int rc;

	if (!rdev)
		return -EINVAL;

	en_dev = rdev->en_dev;


	rc = en_dev->en_ops->bnxt_free_msix(rdev->en_dev, BNXT_ROCE_ULP);

	return rc;
}

static int bnxt_re_request_msix(struct bnxt_re_dev *rdev)
{
	int rc = 0, num_msix_want = BNXT_RE_MAX_MSIX, num_msix_got;
	struct bnxt_en_dev *en_dev;

	if (!rdev)
		return -EINVAL;

	en_dev = rdev->en_dev;

	num_msix_want = min_t(u32, BNXT_RE_MAX_MSIX, num_online_cpus());

	num_msix_got = en_dev->en_ops->bnxt_request_msix(en_dev, BNXT_ROCE_ULP,
							 rdev->msix_entries,
							 num_msix_want);
	if (num_msix_got < BNXT_RE_MIN_MSIX) {
		rc = -EINVAL;
		goto done;
	}
	if (num_msix_got != num_msix_want) {
		ibdev_warn(&rdev->ibdev,
			   "Requested %d MSI-X vectors, got %d\n",
			   num_msix_want, num_msix_got);
	}
	rdev->num_msix = num_msix_got;
done:
	return rc;
}

static void bnxt_re_init_hwrm_hdr(struct bnxt_re_dev *rdev, struct input *hdr,
				  u16 opcd, u16 crid, u16 trid)
{
	hdr->req_type = cpu_to_le16(opcd);
	hdr->cmpl_ring = cpu_to_le16(crid);
	hdr->target_id = cpu_to_le16(trid);
}

static void bnxt_re_fill_fw_msg(struct bnxt_fw_msg *fw_msg, void *msg,
				int msg_len, void *resp, int resp_max_len,
				int timeout)
{
	fw_msg->msg = msg;
	fw_msg->msg_len = msg_len;
	fw_msg->resp = resp;
	fw_msg->resp_max_len = resp_max_len;
	fw_msg->timeout = timeout;
}

static int bnxt_re_net_ring_free(struct bnxt_re_dev *rdev,
				 u16 fw_ring_id, int type)
{
	struct bnxt_en_dev *en_dev = rdev->en_dev;
	struct hwrm_ring_free_input req = {0};
	struct hwrm_ring_free_output resp;
	struct bnxt_fw_msg fw_msg;
	int rc = -EINVAL;

	if (!en_dev)
		return rc;

	if (test_bit(BNXT_RE_FLAG_ERR_DEVICE_DETACHED, &rdev->flags))
		return 0;

	memset(&fw_msg, 0, sizeof(fw_msg));

	bnxt_re_init_hwrm_hdr(rdev, (void *)&req, HWRM_RING_FREE, -1, -1);
	req.ring_type = type;
	req.ring_id = cpu_to_le16(fw_ring_id);
	bnxt_re_fill_fw_msg(&fw_msg, (void *)&req, sizeof(req), (void *)&resp,
			    sizeof(resp), DFLT_HWRM_CMD_TIMEOUT);
	rc = en_dev->en_ops->bnxt_send_fw_msg(en_dev, BNXT_ROCE_ULP, &fw_msg);
	if (rc)
		ibdev_err(&rdev->ibdev, "Failed to free HW ring:%d :%#x",
			  req.ring_id, rc);
	return rc;
}

static int bnxt_re_net_ring_alloc(struct bnxt_re_dev *rdev,
				  struct bnxt_re_ring_attr *ring_attr,
				  u16 *fw_ring_id)
{
	struct bnxt_en_dev *en_dev = rdev->en_dev;
	struct hwrm_ring_alloc_input req = {0};
	struct hwrm_ring_alloc_output resp;
	struct bnxt_fw_msg fw_msg;
	int rc = -EINVAL;

	if (!en_dev)
		return rc;

	memset(&fw_msg, 0, sizeof(fw_msg));
	bnxt_re_init_hwrm_hdr(rdev, (void *)&req, HWRM_RING_ALLOC, -1, -1);
	req.enables = 0;
	req.page_tbl_addr =  cpu_to_le64(ring_attr->dma_arr[0]);
	if (ring_attr->pages > 1) {
		/* Page size is in log2 units */
		req.page_size = BNXT_PAGE_SHIFT;
		req.page_tbl_depth = 1;
	}
	req.fbo = 0;
	/* Association of ring index with doorbell index and MSIX number */
	req.logical_id = cpu_to_le16(ring_attr->lrid);
	req.length = cpu_to_le32(ring_attr->depth + 1);
	req.ring_type = ring_attr->type;
	req.int_mode = ring_attr->mode;
	bnxt_re_fill_fw_msg(&fw_msg, (void *)&req, sizeof(req), (void *)&resp,
			    sizeof(resp), DFLT_HWRM_CMD_TIMEOUT);
	rc = en_dev->en_ops->bnxt_send_fw_msg(en_dev, BNXT_ROCE_ULP, &fw_msg);
	if (!rc)
		*fw_ring_id = le16_to_cpu(resp.ring_id);

	return rc;
}

static int bnxt_re_net_stats_ctx_free(struct bnxt_re_dev *rdev,
				      u32 fw_stats_ctx_id)
{
	struct bnxt_en_dev *en_dev = rdev->en_dev;
	struct hwrm_stat_ctx_free_input req = {};
	struct hwrm_stat_ctx_free_output resp = {};
	struct bnxt_fw_msg fw_msg;
	int rc = -EINVAL;

	if (!en_dev)
		return rc;

	if (test_bit(BNXT_RE_FLAG_ERR_DEVICE_DETACHED, &rdev->flags))
		return 0;

	memset(&fw_msg, 0, sizeof(fw_msg));

	bnxt_re_init_hwrm_hdr(rdev, (void *)&req, HWRM_STAT_CTX_FREE, -1, -1);
	req.stat_ctx_id = cpu_to_le32(fw_stats_ctx_id);
	bnxt_re_fill_fw_msg(&fw_msg, (void *)&req, sizeof(req), (void *)&resp,
			    sizeof(resp), DFLT_HWRM_CMD_TIMEOUT);
	rc = en_dev->en_ops->bnxt_send_fw_msg(en_dev, BNXT_ROCE_ULP, &fw_msg);
	if (rc)
		ibdev_err(&rdev->ibdev, "Failed to free HW stats context %#x",
			  rc);

	return rc;
}

static int bnxt_re_net_stats_ctx_alloc(struct bnxt_re_dev *rdev,
				       dma_addr_t dma_map,
				       u32 *fw_stats_ctx_id)
{
	struct bnxt_qplib_chip_ctx *chip_ctx = rdev->chip_ctx;
	struct hwrm_stat_ctx_alloc_output resp = {0};
	struct hwrm_stat_ctx_alloc_input req = {0};
	struct bnxt_en_dev *en_dev = rdev->en_dev;
	struct bnxt_fw_msg fw_msg;
	int rc = -EINVAL;

	*fw_stats_ctx_id = INVALID_STATS_CTX_ID;

	if (!en_dev)
		return rc;

	memset(&fw_msg, 0, sizeof(fw_msg));

	bnxt_re_init_hwrm_hdr(rdev, (void *)&req, HWRM_STAT_CTX_ALLOC, -1, -1);
	req.update_period_ms = cpu_to_le32(1000);
	req.stats_dma_addr = cpu_to_le64(dma_map);
	req.stats_dma_length = cpu_to_le16(chip_ctx->hw_stats_size);
	req.stat_ctx_flags = STAT_CTX_ALLOC_REQ_STAT_CTX_FLAGS_ROCE;
	bnxt_re_fill_fw_msg(&fw_msg, (void *)&req, sizeof(req), (void *)&resp,
			    sizeof(resp), DFLT_HWRM_CMD_TIMEOUT);
	rc = en_dev->en_ops->bnxt_send_fw_msg(en_dev, BNXT_ROCE_ULP, &fw_msg);
	if (!rc)
		*fw_stats_ctx_id = le32_to_cpu(resp.stat_ctx_id);

	return rc;
}

/* Device */

static bool is_bnxt_re_dev(struct net_device *netdev)
{
	struct ethtool_drvinfo drvinfo;

	if (netdev->ethtool_ops && netdev->ethtool_ops->get_drvinfo) {
		memset(&drvinfo, 0, sizeof(drvinfo));
		netdev->ethtool_ops->get_drvinfo(netdev, &drvinfo);

		if (strcmp(drvinfo.driver, "bnxt_en"))
			return false;
		return true;
	}
	return false;
}

static struct bnxt_re_dev *bnxt_re_from_netdev(struct net_device *netdev)
{
	struct ib_device *ibdev =
		ib_device_get_by_netdev(netdev, RDMA_DRIVER_BNXT_RE);
	if (!ibdev)
		return NULL;

	return container_of(ibdev, struct bnxt_re_dev, ibdev);
}

static struct bnxt_en_dev *bnxt_re_dev_probe(struct net_device *netdev)
{
	struct bnxt_en_dev *en_dev;
	struct pci_dev *pdev;

	en_dev = bnxt_ulp_probe(netdev);
	if (IS_ERR(en_dev))
		return en_dev;

	pdev = en_dev->pdev;
	if (!pdev)
		return ERR_PTR(-EINVAL);

	if (!(en_dev->flags & BNXT_EN_FLAG_ROCE_CAP)) {
		dev_info(&pdev->dev,
			"%s: probe error: RoCE is not supported on this device",
			ROCE_DRV_MODULE_NAME);
		return ERR_PTR(-ENODEV);
	}

	dev_hold(netdev);

	return en_dev;
}

static ssize_t hw_rev_show(struct device *device, struct device_attribute *attr,
			   char *buf)
{
	struct bnxt_re_dev *rdev =
		rdma_device_to_drv_device(device, struct bnxt_re_dev, ibdev);

	return sysfs_emit(buf, "0x%x\n", rdev->en_dev->pdev->vendor);
}
static DEVICE_ATTR_RO(hw_rev);

static ssize_t hca_type_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct bnxt_re_dev *rdev =
		rdma_device_to_drv_device(device, struct bnxt_re_dev, ibdev);

	return sysfs_emit(buf, "%s\n", rdev->ibdev.node_desc);
}
static DEVICE_ATTR_RO(hca_type);

static struct attribute *bnxt_re_attributes[] = {
	&dev_attr_hw_rev.attr,
	&dev_attr_hca_type.attr,
	NULL
};

static const struct attribute_group bnxt_re_dev_attr_group = {
	.attrs = bnxt_re_attributes,
};

static const struct ib_device_ops bnxt_re_dev_ops = {
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_BNXT_RE,
	.uverbs_abi_ver = BNXT_RE_ABI_VERSION,

	.add_gid = bnxt_re_add_gid,
	.alloc_hw_port_stats = bnxt_re_ib_alloc_hw_port_stats,
	.alloc_mr = bnxt_re_alloc_mr,
	.alloc_pd = bnxt_re_alloc_pd,
	.alloc_ucontext = bnxt_re_alloc_ucontext,
	.create_ah = bnxt_re_create_ah,
	.create_cq = bnxt_re_create_cq,
	.create_qp = bnxt_re_create_qp,
	.create_srq = bnxt_re_create_srq,
	.create_user_ah = bnxt_re_create_ah,
	.dealloc_driver = bnxt_re_dealloc_driver,
	.dealloc_pd = bnxt_re_dealloc_pd,
	.dealloc_ucontext = bnxt_re_dealloc_ucontext,
	.del_gid = bnxt_re_del_gid,
	.dereg_mr = bnxt_re_dereg_mr,
	.destroy_ah = bnxt_re_destroy_ah,
	.destroy_cq = bnxt_re_destroy_cq,
	.destroy_qp = bnxt_re_destroy_qp,
	.destroy_srq = bnxt_re_destroy_srq,
	.device_group = &bnxt_re_dev_attr_group,
	.get_dev_fw_str = bnxt_re_query_fw_str,
	.get_dma_mr = bnxt_re_get_dma_mr,
	.get_hw_stats = bnxt_re_ib_get_hw_stats,
	.get_link_layer = bnxt_re_get_link_layer,
	.get_port_immutable = bnxt_re_get_port_immutable,
	.map_mr_sg = bnxt_re_map_mr_sg,
	.mmap = bnxt_re_mmap,
	.modify_ah = bnxt_re_modify_ah,
	.modify_qp = bnxt_re_modify_qp,
	.modify_srq = bnxt_re_modify_srq,
	.poll_cq = bnxt_re_poll_cq,
	.post_recv = bnxt_re_post_recv,
	.post_send = bnxt_re_post_send,
	.post_srq_recv = bnxt_re_post_srq_recv,
	.query_ah = bnxt_re_query_ah,
	.query_device = bnxt_re_query_device,
	.query_pkey = bnxt_re_query_pkey,
	.query_port = bnxt_re_query_port,
	.query_qp = bnxt_re_query_qp,
	.query_srq = bnxt_re_query_srq,
	.reg_user_mr = bnxt_re_reg_user_mr,
	.req_notify_cq = bnxt_re_req_notify_cq,
	INIT_RDMA_OBJ_SIZE(ib_ah, bnxt_re_ah, ib_ah),
	INIT_RDMA_OBJ_SIZE(ib_cq, bnxt_re_cq, ib_cq),
	INIT_RDMA_OBJ_SIZE(ib_pd, bnxt_re_pd, ib_pd),
	INIT_RDMA_OBJ_SIZE(ib_qp, bnxt_re_qp, ib_qp),
	INIT_RDMA_OBJ_SIZE(ib_srq, bnxt_re_srq, ib_srq),
	INIT_RDMA_OBJ_SIZE(ib_ucontext, bnxt_re_ucontext, ib_uctx),
};

static int bnxt_re_register_ib(struct bnxt_re_dev *rdev)
{
	struct ib_device *ibdev = &rdev->ibdev;
	int ret;

	/* ib device init */
	ibdev->node_type = RDMA_NODE_IB_CA;
	strlcpy(ibdev->node_desc, BNXT_RE_DESC " HCA",
		strlen(BNXT_RE_DESC) + 5);
	ibdev->phys_port_cnt = 1;

	addrconf_addr_eui48((u8 *)&ibdev->node_guid, rdev->netdev->dev_addr);

	ibdev->num_comp_vectors	= rdev->num_msix - 1;
	ibdev->dev.parent = &rdev->en_dev->pdev->dev;
	ibdev->local_dma_lkey = BNXT_QPLIB_RSVD_LKEY;

	ib_set_device_ops(ibdev, &bnxt_re_dev_ops);
	ret = ib_device_set_netdev(&rdev->ibdev, rdev->netdev, 1);
	if (ret)
		return ret;

	dma_set_max_seg_size(&rdev->en_dev->pdev->dev, UINT_MAX);
	return ib_register_device(ibdev, "bnxt_re%d", &rdev->en_dev->pdev->dev);
}

static void bnxt_re_dev_remove(struct bnxt_re_dev *rdev)
{
	dev_put(rdev->netdev);
	rdev->netdev = NULL;
	mutex_lock(&bnxt_re_dev_lock);
	list_del_rcu(&rdev->list);
	mutex_unlock(&bnxt_re_dev_lock);

	synchronize_rcu();
}

static struct bnxt_re_dev *bnxt_re_dev_add(struct net_device *netdev,
					   struct bnxt_en_dev *en_dev)
{
	struct bnxt_re_dev *rdev;

	/* Allocate bnxt_re_dev instance here */
	rdev = ib_alloc_device(bnxt_re_dev, ibdev);
	if (!rdev) {
		ibdev_err(NULL, "%s: bnxt_re_dev allocation failure!",
			  ROCE_DRV_MODULE_NAME);
		return NULL;
	}
	/* Default values */
	rdev->netdev = netdev;
	dev_hold(rdev->netdev);
	rdev->en_dev = en_dev;
	rdev->id = rdev->en_dev->pdev->devfn;
	INIT_LIST_HEAD(&rdev->qp_list);
	mutex_init(&rdev->qp_lock);
	atomic_set(&rdev->qp_count, 0);
	atomic_set(&rdev->cq_count, 0);
	atomic_set(&rdev->srq_count, 0);
	atomic_set(&rdev->mr_count, 0);
	atomic_set(&rdev->mw_count, 0);
	atomic_set(&rdev->ah_count, 0);
	atomic_set(&rdev->pd_count, 0);
	rdev->cosq[0] = 0xFFFF;
	rdev->cosq[1] = 0xFFFF;

	mutex_lock(&bnxt_re_dev_lock);
	list_add_tail_rcu(&rdev->list, &bnxt_re_dev_list);
	mutex_unlock(&bnxt_re_dev_lock);
	return rdev;
}

static int bnxt_re_handle_unaffi_async_event(struct creq_func_event
					     *unaffi_async)
{
	switch (unaffi_async->event) {
	case CREQ_FUNC_EVENT_EVENT_TX_WQE_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_TX_DATA_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_RX_WQE_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_RX_DATA_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_CQ_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_TQM_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_CFCQ_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_CFCS_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_CFCC_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_CFCM_ERROR:
		break;
	case CREQ_FUNC_EVENT_EVENT_TIM_ERROR:
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int bnxt_re_handle_qp_async_event(struct creq_qp_event *qp_event,
					 struct bnxt_re_qp *qp)
{
	struct ib_event event;
	unsigned int flags;

	if (qp->qplib_qp.state == CMDQ_MODIFY_QP_NEW_STATE_ERR &&
	    rdma_is_kernel_res(&qp->ib_qp.res)) {
		flags = bnxt_re_lock_cqs(qp);
		bnxt_qplib_add_flush_qp(&qp->qplib_qp);
		bnxt_re_unlock_cqs(qp, flags);
	}

	memset(&event, 0, sizeof(event));
	if (qp->qplib_qp.srq) {
		event.device = &qp->rdev->ibdev;
		event.element.qp = &qp->ib_qp;
		event.event = IB_EVENT_QP_LAST_WQE_REACHED;
	}

	if (event.device && qp->ib_qp.event_handler)
		qp->ib_qp.event_handler(&event, qp->ib_qp.qp_context);

	return 0;
}

static int bnxt_re_handle_affi_async_event(struct creq_qp_event *affi_async,
					   void *obj)
{
	int rc = 0;
	u8 event;

	if (!obj)
		return rc; /* QP was already dead, still return success */

	event = affi_async->event;
	if (event == CREQ_QP_EVENT_EVENT_QP_ERROR_NOTIFICATION) {
		struct bnxt_qplib_qp *lib_qp = obj;
		struct bnxt_re_qp *qp = container_of(lib_qp, struct bnxt_re_qp,
						     qplib_qp);
		rc = bnxt_re_handle_qp_async_event(affi_async, qp);
	}
	return rc;
}

static int bnxt_re_aeq_handler(struct bnxt_qplib_rcfw *rcfw,
			       void *aeqe, void *obj)
{
	struct creq_qp_event *affi_async;
	struct creq_func_event *unaffi_async;
	u8 type;
	int rc;

	type = ((struct creq_base *)aeqe)->type;
	if (type == CREQ_BASE_TYPE_FUNC_EVENT) {
		unaffi_async = aeqe;
		rc = bnxt_re_handle_unaffi_async_event(unaffi_async);
	} else {
		affi_async = aeqe;
		rc = bnxt_re_handle_affi_async_event(affi_async, obj);
	}

	return rc;
}

static int bnxt_re_srqn_handler(struct bnxt_qplib_nq *nq,
				struct bnxt_qplib_srq *handle, u8 event)
{
	struct bnxt_re_srq *srq = container_of(handle, struct bnxt_re_srq,
					       qplib_srq);
	struct ib_event ib_event;
	int rc = 0;

	ib_event.device = &srq->rdev->ibdev;
	ib_event.element.srq = &srq->ib_srq;
	if (event == NQ_SRQ_EVENT_EVENT_SRQ_THRESHOLD_EVENT)
		ib_event.event = IB_EVENT_SRQ_LIMIT_REACHED;
	else
		ib_event.event = IB_EVENT_SRQ_ERR;

	if (srq->ib_srq.event_handler) {
		/* Lock event_handler? */
		(*srq->ib_srq.event_handler)(&ib_event,
					     srq->ib_srq.srq_context);
	}
	return rc;
}

static int bnxt_re_cqn_handler(struct bnxt_qplib_nq *nq,
			       struct bnxt_qplib_cq *handle)
{
	struct bnxt_re_cq *cq = container_of(handle, struct bnxt_re_cq,
					     qplib_cq);

	if (cq->ib_cq.comp_handler) {
		/* Lock comp_handler? */
		(*cq->ib_cq.comp_handler)(&cq->ib_cq, cq->ib_cq.cq_context);
	}

	return 0;
}

#define BNXT_RE_GEN_P5_PF_NQ_DB		0x10000
#define BNXT_RE_GEN_P5_VF_NQ_DB		0x4000
static u32 bnxt_re_get_nqdb_offset(struct bnxt_re_dev *rdev, u16 indx)
{
	return bnxt_qplib_is_chip_gen_p5(rdev->chip_ctx) ?
		(rdev->is_virtfn ? BNXT_RE_GEN_P5_VF_NQ_DB :
				   BNXT_RE_GEN_P5_PF_NQ_DB) :
				   rdev->msix_entries[indx].db_offset;
}

static void bnxt_re_cleanup_res(struct bnxt_re_dev *rdev)
{
	int i;

	for (i = 1; i < rdev->num_msix; i++)
		bnxt_qplib_disable_nq(&rdev->nq[i - 1]);

	if (rdev->qplib_res.rcfw)
		bnxt_qplib_cleanup_res(&rdev->qplib_res);
}

static int bnxt_re_init_res(struct bnxt_re_dev *rdev)
{
	int num_vec_enabled = 0;
	int rc = 0, i;
	u32 db_offt;

	bnxt_qplib_init_res(&rdev->qplib_res);

	for (i = 1; i < rdev->num_msix ; i++) {
		db_offt = bnxt_re_get_nqdb_offset(rdev, i);
		rc = bnxt_qplib_enable_nq(rdev->en_dev->pdev, &rdev->nq[i - 1],
					  i - 1, rdev->msix_entries[i].vector,
					  db_offt, &bnxt_re_cqn_handler,
					  &bnxt_re_srqn_handler);
		if (rc) {
			ibdev_err(&rdev->ibdev,
				  "Failed to enable NQ with rc = 0x%x", rc);
			goto fail;
		}
		num_vec_enabled++;
	}
	return 0;
fail:
	for (i = num_vec_enabled; i >= 0; i--)
		bnxt_qplib_disable_nq(&rdev->nq[i]);
	return rc;
}

static void bnxt_re_free_nq_res(struct bnxt_re_dev *rdev)
{
	u8 type;
	int i;

	for (i = 0; i < rdev->num_msix - 1; i++) {
		type = bnxt_qplib_get_ring_type(rdev->chip_ctx);
		bnxt_re_net_ring_free(rdev, rdev->nq[i].ring_id, type);
		bnxt_qplib_free_nq(&rdev->nq[i]);
		rdev->nq[i].res = NULL;
	}
}

static void bnxt_re_free_res(struct bnxt_re_dev *rdev)
{
	bnxt_re_free_nq_res(rdev);

	if (rdev->qplib_res.dpi_tbl.max) {
		bnxt_qplib_dealloc_dpi(&rdev->qplib_res,
				       &rdev->qplib_res.dpi_tbl,
				       &rdev->dpi_privileged);
	}
	if (rdev->qplib_res.rcfw) {
		bnxt_qplib_free_res(&rdev->qplib_res);
		rdev->qplib_res.rcfw = NULL;
	}
}

static int bnxt_re_alloc_res(struct bnxt_re_dev *rdev)
{
	struct bnxt_re_ring_attr rattr = {};
	int num_vec_created = 0;
	int rc = 0, i;
	u8 type;

	/* Configure and allocate resources for qplib */
	rdev->qplib_res.rcfw = &rdev->rcfw;
	rc = bnxt_qplib_get_dev_attr(&rdev->rcfw, &rdev->dev_attr,
				     rdev->is_virtfn);
	if (rc)
		goto fail;

	rc = bnxt_qplib_alloc_res(&rdev->qplib_res, rdev->en_dev->pdev,
				  rdev->netdev, &rdev->dev_attr);
	if (rc)
		goto fail;

	rc = bnxt_qplib_alloc_dpi(&rdev->qplib_res.dpi_tbl,
				  &rdev->dpi_privileged,
				  rdev);
	if (rc)
		goto dealloc_res;

	for (i = 0; i < rdev->num_msix - 1; i++) {
		struct bnxt_qplib_nq *nq;

		nq = &rdev->nq[i];
		nq->hwq.max_elements = BNXT_QPLIB_NQE_MAX_CNT;
		rc = bnxt_qplib_alloc_nq(&rdev->qplib_res, &rdev->nq[i]);
		if (rc) {
			ibdev_err(&rdev->ibdev, "Alloc Failed NQ%d rc:%#x",
				  i, rc);
			goto free_nq;
		}
		type = bnxt_qplib_get_ring_type(rdev->chip_ctx);
		rattr.dma_arr = nq->hwq.pbl[PBL_LVL_0].pg_map_arr;
		rattr.pages = nq->hwq.pbl[rdev->nq[i].hwq.level].pg_count;
		rattr.type = type;
		rattr.mode = RING_ALLOC_REQ_INT_MODE_MSIX;
		rattr.depth = BNXT_QPLIB_NQE_MAX_CNT - 1;
		rattr.lrid = rdev->msix_entries[i + 1].ring_idx;
		rc = bnxt_re_net_ring_alloc(rdev, &rattr, &nq->ring_id);
		if (rc) {
			ibdev_err(&rdev->ibdev,
				  "Failed to allocate NQ fw id with rc = 0x%x",
				  rc);
			bnxt_qplib_free_nq(&rdev->nq[i]);
			goto free_nq;
		}
		num_vec_created++;
	}
	return 0;
free_nq:
	for (i = num_vec_created - 1; i >= 0; i--) {
		type = bnxt_qplib_get_ring_type(rdev->chip_ctx);
		bnxt_re_net_ring_free(rdev, rdev->nq[i].ring_id, type);
		bnxt_qplib_free_nq(&rdev->nq[i]);
	}
	bnxt_qplib_dealloc_dpi(&rdev->qplib_res,
			       &rdev->qplib_res.dpi_tbl,
			       &rdev->dpi_privileged);
dealloc_res:
	bnxt_qplib_free_res(&rdev->qplib_res);

fail:
	rdev->qplib_res.rcfw = NULL;
	return rc;
}

static void bnxt_re_dispatch_event(struct ib_device *ibdev, struct ib_qp *qp,
				   u8 port_num, enum ib_event_type event)
{
	struct ib_event ib_event;

	ib_event.device = ibdev;
	if (qp) {
		ib_event.element.qp = qp;
		ib_event.event = event;
		if (qp->event_handler)
			qp->event_handler(&ib_event, qp->qp_context);

	} else {
		ib_event.element.port_num = port_num;
		ib_event.event = event;
		ib_dispatch_event(&ib_event);
	}
}

#define HWRM_QUEUE_PRI2COS_QCFG_INPUT_FLAGS_IVLAN      0x02
static int bnxt_re_query_hwrm_pri2cos(struct bnxt_re_dev *rdev, u8 dir,
				      u64 *cid_map)
{
	struct hwrm_queue_pri2cos_qcfg_input req = {0};
	struct bnxt *bp = netdev_priv(rdev->netdev);
	struct hwrm_queue_pri2cos_qcfg_output resp;
	struct bnxt_en_dev *en_dev = rdev->en_dev;
	struct bnxt_fw_msg fw_msg;
	u32 flags = 0;
	u8 *qcfgmap, *tmp_map;
	int rc = 0, i;

	if (!cid_map)
		return -EINVAL;

	memset(&fw_msg, 0, sizeof(fw_msg));
	bnxt_re_init_hwrm_hdr(rdev, (void *)&req,
			      HWRM_QUEUE_PRI2COS_QCFG, -1, -1);
	flags |= (dir & 0x01);
	flags |= HWRM_QUEUE_PRI2COS_QCFG_INPUT_FLAGS_IVLAN;
	req.flags = cpu_to_le32(flags);
	req.port_id = bp->pf.port_id;

	bnxt_re_fill_fw_msg(&fw_msg, (void *)&req, sizeof(req), (void *)&resp,
			    sizeof(resp), DFLT_HWRM_CMD_TIMEOUT);
	rc = en_dev->en_ops->bnxt_send_fw_msg(en_dev, BNXT_ROCE_ULP, &fw_msg);
	if (rc)
		return rc;

	if (resp.queue_cfg_info) {
		ibdev_warn(&rdev->ibdev,
			   "Asymmetric cos queue configuration detected");
		ibdev_warn(&rdev->ibdev,
			   " on device, QoS may not be fully functional\n");
	}
	qcfgmap = &resp.pri0_cos_queue_id;
	tmp_map = (u8 *)cid_map;
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++)
		tmp_map[i] = qcfgmap[i];

	return rc;
}

static bool bnxt_re_is_qp1_or_shadow_qp(struct bnxt_re_dev *rdev,
					struct bnxt_re_qp *qp)
{
	return (qp->ib_qp.qp_type == IB_QPT_GSI) ||
	       (qp == rdev->gsi_ctx.gsi_sqp);
}

static void bnxt_re_dev_stop(struct bnxt_re_dev *rdev)
{
	int mask = IB_QP_STATE;
	struct ib_qp_attr qp_attr;
	struct bnxt_re_qp *qp;

	qp_attr.qp_state = IB_QPS_ERR;
	mutex_lock(&rdev->qp_lock);
	list_for_each_entry(qp, &rdev->qp_list, list) {
		/* Modify the state of all QPs except QP1/Shadow QP */
		if (!bnxt_re_is_qp1_or_shadow_qp(rdev, qp)) {
			if (qp->qplib_qp.state !=
			    CMDQ_MODIFY_QP_NEW_STATE_RESET &&
			    qp->qplib_qp.state !=
			    CMDQ_MODIFY_QP_NEW_STATE_ERR) {
				bnxt_re_dispatch_event(&rdev->ibdev, &qp->ib_qp,
						       1, IB_EVENT_QP_FATAL);
				bnxt_re_modify_qp(&qp->ib_qp, &qp_attr, mask,
						  NULL);
			}
		}
	}
	mutex_unlock(&rdev->qp_lock);
}

static int bnxt_re_update_gid(struct bnxt_re_dev *rdev)
{
	struct bnxt_qplib_sgid_tbl *sgid_tbl = &rdev->qplib_res.sgid_tbl;
	struct bnxt_qplib_gid gid;
	u16 gid_idx, index;
	int rc = 0;

	if (!ib_device_try_get(&rdev->ibdev))
		return 0;

	if (!sgid_tbl) {
		ibdev_err(&rdev->ibdev, "QPLIB: SGID table not allocated");
		rc = -EINVAL;
		goto out;
	}

	for (index = 0; index < sgid_tbl->active; index++) {
		gid_idx = sgid_tbl->hw_id[index];

		if (!memcmp(&sgid_tbl->tbl[index], &bnxt_qplib_gid_zero,
			    sizeof(bnxt_qplib_gid_zero)))
			continue;
		/* need to modify the VLAN enable setting of non VLAN GID only
		 * as setting is done for VLAN GID while adding GID
		 */
		if (sgid_tbl->vlan[index])
			continue;

		memcpy(&gid, &sgid_tbl->tbl[index], sizeof(gid));

		rc = bnxt_qplib_update_sgid(sgid_tbl, &gid, gid_idx,
					    rdev->qplib_res.netdev->dev_addr);
	}
out:
	ib_device_put(&rdev->ibdev);
	return rc;
}

static u32 bnxt_re_get_priority_mask(struct bnxt_re_dev *rdev)
{
	u32 prio_map = 0, tmp_map = 0;
	struct net_device *netdev;
	struct dcb_app app;

	netdev = rdev->netdev;

	memset(&app, 0, sizeof(app));
	app.selector = IEEE_8021QAZ_APP_SEL_ETHERTYPE;
	app.protocol = ETH_P_IBOE;
	tmp_map = dcb_ieee_getapp_mask(netdev, &app);
	prio_map = tmp_map;

	app.selector = IEEE_8021QAZ_APP_SEL_DGRAM;
	app.protocol = ROCE_V2_UDP_DPORT;
	tmp_map = dcb_ieee_getapp_mask(netdev, &app);
	prio_map |= tmp_map;

	return prio_map;
}

static void bnxt_re_parse_cid_map(u8 prio_map, u8 *cid_map, u16 *cosq)
{
	u16 prio;
	u8 id;

	for (prio = 0, id = 0; prio < 8; prio++) {
		if (prio_map & (1 << prio)) {
			cosq[id] = cid_map[prio];
			id++;
			if (id == 2) /* Max 2 tcs supported */
				break;
		}
	}
}

static int bnxt_re_setup_qos(struct bnxt_re_dev *rdev)
{
	u8 prio_map = 0;
	u64 cid_map;
	int rc;

	/* Get priority for roce */
	prio_map = bnxt_re_get_priority_mask(rdev);

	if (prio_map == rdev->cur_prio_map)
		return 0;
	rdev->cur_prio_map = prio_map;
	/* Get cosq id for this priority */
	rc = bnxt_re_query_hwrm_pri2cos(rdev, 0, &cid_map);
	if (rc) {
		ibdev_warn(&rdev->ibdev, "no cos for p_mask %x\n", prio_map);
		return rc;
	}
	/* Parse CoS IDs for app priority */
	bnxt_re_parse_cid_map(prio_map, (u8 *)&cid_map, rdev->cosq);

	/* Config BONO. */
	rc = bnxt_qplib_map_tc2cos(&rdev->qplib_res, rdev->cosq);
	if (rc) {
		ibdev_warn(&rdev->ibdev, "no tc for cos{%x, %x}\n",
			   rdev->cosq[0], rdev->cosq[1]);
		return rc;
	}

	/* Actual priorities are not programmed as they are already
	 * done by L2 driver; just enable or disable priority vlan tagging
	 */
	if ((prio_map == 0 && rdev->qplib_res.prio) ||
	    (prio_map != 0 && !rdev->qplib_res.prio)) {
		rdev->qplib_res.prio = prio_map ? true : false;

		bnxt_re_update_gid(rdev);
	}

	return 0;
}

static void bnxt_re_query_hwrm_intf_version(struct bnxt_re_dev *rdev)
{
	struct bnxt_en_dev *en_dev = rdev->en_dev;
	struct hwrm_ver_get_output resp = {0};
	struct hwrm_ver_get_input req = {0};
	struct bnxt_fw_msg fw_msg;
	int rc = 0;

	memset(&fw_msg, 0, sizeof(fw_msg));
	bnxt_re_init_hwrm_hdr(rdev, (void *)&req,
			      HWRM_VER_GET, -1, -1);
	req.hwrm_intf_maj = HWRM_VERSION_MAJOR;
	req.hwrm_intf_min = HWRM_VERSION_MINOR;
	req.hwrm_intf_upd = HWRM_VERSION_UPDATE;
	bnxt_re_fill_fw_msg(&fw_msg, (void *)&req, sizeof(req), (void *)&resp,
			    sizeof(resp), DFLT_HWRM_CMD_TIMEOUT);
	rc = en_dev->en_ops->bnxt_send_fw_msg(en_dev, BNXT_ROCE_ULP, &fw_msg);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to query HW version, rc = 0x%x",
			  rc);
		return;
	}
	rdev->qplib_ctx.hwrm_intf_ver =
		(u64)le16_to_cpu(resp.hwrm_intf_major) << 48 |
		(u64)le16_to_cpu(resp.hwrm_intf_minor) << 32 |
		(u64)le16_to_cpu(resp.hwrm_intf_build) << 16 |
		le16_to_cpu(resp.hwrm_intf_patch);
}

static int bnxt_re_ib_init(struct bnxt_re_dev *rdev)
{
	int rc = 0;
	u32 event;

	/* Register ib dev */
	rc = bnxt_re_register_ib(rdev);
	if (rc) {
		pr_err("Failed to register with IB: %#x\n", rc);
		return rc;
	}
	dev_info(rdev_to_dev(rdev), "Device registered successfully");
	ib_get_eth_speed(&rdev->ibdev, 1, &rdev->active_speed,
			 &rdev->active_width);
	set_bit(BNXT_RE_FLAG_ISSUE_ROCE_STATS, &rdev->flags);

	event = netif_running(rdev->netdev) && netif_carrier_ok(rdev->netdev) ?
		IB_EVENT_PORT_ACTIVE : IB_EVENT_PORT_ERR;

	bnxt_re_dispatch_event(&rdev->ibdev, NULL, 1, event);

	return rc;
}

static void bnxt_re_dev_uninit(struct bnxt_re_dev *rdev)
{
	u8 type;
	int rc;

	if (test_and_clear_bit(BNXT_RE_FLAG_QOS_WORK_REG, &rdev->flags))
		cancel_delayed_work_sync(&rdev->worker);

	if (test_and_clear_bit(BNXT_RE_FLAG_RESOURCES_INITIALIZED,
			       &rdev->flags))
		bnxt_re_cleanup_res(rdev);
	if (test_and_clear_bit(BNXT_RE_FLAG_RESOURCES_ALLOCATED, &rdev->flags))
		bnxt_re_free_res(rdev);

	if (test_and_clear_bit(BNXT_RE_FLAG_RCFW_CHANNEL_EN, &rdev->flags)) {
		rc = bnxt_qplib_deinit_rcfw(&rdev->rcfw);
		if (rc)
			ibdev_warn(&rdev->ibdev,
				   "Failed to deinitialize RCFW: %#x", rc);
		bnxt_re_net_stats_ctx_free(rdev, rdev->qplib_ctx.stats.fw_id);
		bnxt_qplib_free_ctx(&rdev->qplib_res, &rdev->qplib_ctx);
		bnxt_qplib_disable_rcfw_channel(&rdev->rcfw);
		type = bnxt_qplib_get_ring_type(rdev->chip_ctx);
		bnxt_re_net_ring_free(rdev, rdev->rcfw.creq.ring_id, type);
		bnxt_qplib_free_rcfw_channel(&rdev->rcfw);
	}
	if (test_and_clear_bit(BNXT_RE_FLAG_GOT_MSIX, &rdev->flags)) {
		rc = bnxt_re_free_msix(rdev);
		if (rc)
			ibdev_warn(&rdev->ibdev,
				   "Failed to free MSI-X vectors: %#x", rc);
	}

	bnxt_re_destroy_chip_ctx(rdev);
	if (test_and_clear_bit(BNXT_RE_FLAG_NETDEV_REGISTERED, &rdev->flags)) {
		rc = bnxt_re_unregister_netdev(rdev);
		if (rc)
			ibdev_warn(&rdev->ibdev,
				   "Failed to unregister with netdev: %#x", rc);
	}
}

/* worker thread for polling periodic events. Now used for QoS programming*/
static void bnxt_re_worker(struct work_struct *work)
{
	struct bnxt_re_dev *rdev = container_of(work, struct bnxt_re_dev,
						worker.work);

	bnxt_re_setup_qos(rdev);
	schedule_delayed_work(&rdev->worker, msecs_to_jiffies(30000));
}

static int bnxt_re_dev_init(struct bnxt_re_dev *rdev, u8 wqe_mode)
{
	struct bnxt_qplib_creq_ctx *creq;
	struct bnxt_re_ring_attr rattr;
	u32 db_offt;
	int vid;
	u8 type;
	int rc;

	/* Registered a new RoCE device instance to netdev */
	memset(&rattr, 0, sizeof(rattr));
	rc = bnxt_re_register_netdev(rdev);
	if (rc) {
		ibdev_err(&rdev->ibdev,
			  "Failed to register with netedev: %#x\n", rc);
		return -EINVAL;
	}
	set_bit(BNXT_RE_FLAG_NETDEV_REGISTERED, &rdev->flags);

	rc = bnxt_re_setup_chip_ctx(rdev, wqe_mode);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to get chip context\n");
		return -EINVAL;
	}

	/* Check whether VF or PF */
	bnxt_re_get_sriov_func_type(rdev);

	rc = bnxt_re_request_msix(rdev);
	if (rc) {
		ibdev_err(&rdev->ibdev,
			  "Failed to get MSI-X vectors: %#x\n", rc);
		rc = -EINVAL;
		goto fail;
	}
	set_bit(BNXT_RE_FLAG_GOT_MSIX, &rdev->flags);

	bnxt_re_query_hwrm_intf_version(rdev);

	/* Establish RCFW Communication Channel to initialize the context
	 * memory for the function and all child VFs
	 */
	rc = bnxt_qplib_alloc_rcfw_channel(&rdev->qplib_res, &rdev->rcfw,
					   &rdev->qplib_ctx,
					   BNXT_RE_MAX_QPC_COUNT);
	if (rc) {
		ibdev_err(&rdev->ibdev,
			  "Failed to allocate RCFW Channel: %#x\n", rc);
		goto fail;
	}

	type = bnxt_qplib_get_ring_type(rdev->chip_ctx);
	creq = &rdev->rcfw.creq;
	rattr.dma_arr = creq->hwq.pbl[PBL_LVL_0].pg_map_arr;
	rattr.pages = creq->hwq.pbl[creq->hwq.level].pg_count;
	rattr.type = type;
	rattr.mode = RING_ALLOC_REQ_INT_MODE_MSIX;
	rattr.depth = BNXT_QPLIB_CREQE_MAX_CNT - 1;
	rattr.lrid = rdev->msix_entries[BNXT_RE_AEQ_IDX].ring_idx;
	rc = bnxt_re_net_ring_alloc(rdev, &rattr, &creq->ring_id);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to allocate CREQ: %#x\n", rc);
		goto free_rcfw;
	}
	db_offt = bnxt_re_get_nqdb_offset(rdev, BNXT_RE_AEQ_IDX);
	vid = rdev->msix_entries[BNXT_RE_AEQ_IDX].vector;
	rc = bnxt_qplib_enable_rcfw_channel(&rdev->rcfw,
					    vid, db_offt, rdev->is_virtfn,
					    &bnxt_re_aeq_handler);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to enable RCFW channel: %#x\n",
			  rc);
		goto free_ring;
	}

	rc = bnxt_qplib_get_dev_attr(&rdev->rcfw, &rdev->dev_attr,
				     rdev->is_virtfn);
	if (rc)
		goto disable_rcfw;

	bnxt_re_set_resource_limits(rdev);

	rc = bnxt_qplib_alloc_ctx(&rdev->qplib_res, &rdev->qplib_ctx, 0,
				  bnxt_qplib_is_chip_gen_p5(rdev->chip_ctx));
	if (rc) {
		ibdev_err(&rdev->ibdev,
			  "Failed to allocate QPLIB context: %#x\n", rc);
		goto disable_rcfw;
	}
	rc = bnxt_re_net_stats_ctx_alloc(rdev,
					 rdev->qplib_ctx.stats.dma_map,
					 &rdev->qplib_ctx.stats.fw_id);
	if (rc) {
		ibdev_err(&rdev->ibdev,
			  "Failed to allocate stats context: %#x\n", rc);
		goto free_ctx;
	}

	rc = bnxt_qplib_init_rcfw(&rdev->rcfw, &rdev->qplib_ctx,
				  rdev->is_virtfn);
	if (rc) {
		ibdev_err(&rdev->ibdev,
			  "Failed to initialize RCFW: %#x\n", rc);
		goto free_sctx;
	}
	set_bit(BNXT_RE_FLAG_RCFW_CHANNEL_EN, &rdev->flags);

	/* Resources based on the 'new' device caps */
	rc = bnxt_re_alloc_res(rdev);
	if (rc) {
		ibdev_err(&rdev->ibdev,
			  "Failed to allocate resources: %#x\n", rc);
		goto fail;
	}
	set_bit(BNXT_RE_FLAG_RESOURCES_ALLOCATED, &rdev->flags);
	rc = bnxt_re_init_res(rdev);
	if (rc) {
		ibdev_err(&rdev->ibdev,
			  "Failed to initialize resources: %#x\n", rc);
		goto fail;
	}

	set_bit(BNXT_RE_FLAG_RESOURCES_INITIALIZED, &rdev->flags);

	if (!rdev->is_virtfn) {
		rc = bnxt_re_setup_qos(rdev);
		if (rc)
			ibdev_info(&rdev->ibdev,
				   "RoCE priority not yet configured\n");

		INIT_DELAYED_WORK(&rdev->worker, bnxt_re_worker);
		set_bit(BNXT_RE_FLAG_QOS_WORK_REG, &rdev->flags);
		schedule_delayed_work(&rdev->worker, msecs_to_jiffies(30000));
	}

	return 0;
free_sctx:
	bnxt_re_net_stats_ctx_free(rdev, rdev->qplib_ctx.stats.fw_id);
free_ctx:
	bnxt_qplib_free_ctx(&rdev->qplib_res, &rdev->qplib_ctx);
disable_rcfw:
	bnxt_qplib_disable_rcfw_channel(&rdev->rcfw);
free_ring:
	type = bnxt_qplib_get_ring_type(rdev->chip_ctx);
	bnxt_re_net_ring_free(rdev, rdev->rcfw.creq.ring_id, type);
free_rcfw:
	bnxt_qplib_free_rcfw_channel(&rdev->rcfw);
fail:
	bnxt_re_dev_uninit(rdev);

	return rc;
}

static void bnxt_re_dev_unreg(struct bnxt_re_dev *rdev)
{
	struct net_device *netdev = rdev->netdev;

	bnxt_re_dev_remove(rdev);

	if (netdev)
		dev_put(netdev);
}

static int bnxt_re_dev_reg(struct bnxt_re_dev **rdev, struct net_device *netdev)
{
	struct bnxt_en_dev *en_dev;
	int rc = 0;

	if (!is_bnxt_re_dev(netdev))
		return -ENODEV;

	en_dev = bnxt_re_dev_probe(netdev);
	if (IS_ERR(en_dev)) {
		if (en_dev != ERR_PTR(-ENODEV))
			ibdev_err(&(*rdev)->ibdev, "%s: Failed to probe\n",
				  ROCE_DRV_MODULE_NAME);
		rc = PTR_ERR(en_dev);
		goto exit;
	}
	*rdev = bnxt_re_dev_add(netdev, en_dev);
	if (!*rdev) {
		rc = -ENOMEM;
		dev_put(netdev);
		goto exit;
	}
exit:
	return rc;
}

static void bnxt_re_remove_device(struct bnxt_re_dev *rdev)
{
	bnxt_re_dev_uninit(rdev);
	pci_dev_put(rdev->en_dev->pdev);
	bnxt_re_dev_unreg(rdev);
}

static int bnxt_re_add_device(struct bnxt_re_dev **rdev,
			      struct net_device *netdev, u8 wqe_mode)
{
	int rc;

	rc = bnxt_re_dev_reg(rdev, netdev);
	if (rc == -ENODEV)
		return rc;
	if (rc) {
		pr_err("Failed to register with the device %s: %#x\n",
		       netdev->name, rc);
		return rc;
	}

	pci_dev_get((*rdev)->en_dev->pdev);
	rc = bnxt_re_dev_init(*rdev, wqe_mode);
	if (rc) {
		pci_dev_put((*rdev)->en_dev->pdev);
		bnxt_re_dev_unreg(*rdev);
	}

	return rc;
}

static void bnxt_re_dealloc_driver(struct ib_device *ib_dev)
{
	struct bnxt_re_dev *rdev =
		container_of(ib_dev, struct bnxt_re_dev, ibdev);

	dev_info(rdev_to_dev(rdev), "Unregistering Device");

	rtnl_lock();
	bnxt_re_remove_device(rdev);
	rtnl_unlock();
}

/* Handle all deferred netevents tasks */
static void bnxt_re_task(struct work_struct *work)
{
	struct bnxt_re_work *re_work;
	struct bnxt_re_dev *rdev;
	int rc = 0;

	re_work = container_of(work, struct bnxt_re_work, work);
	rdev = re_work->rdev;

	if (re_work->event == NETDEV_REGISTER) {
		rc = bnxt_re_ib_init(rdev);
		if (rc) {
			ibdev_err(&rdev->ibdev,
				  "Failed to register with IB: %#x", rc);
			rtnl_lock();
			bnxt_re_remove_device(rdev);
			rtnl_unlock();
			goto exit;
		}
		goto exit;
	}

	if (!ib_device_try_get(&rdev->ibdev))
		goto exit;

	switch (re_work->event) {
	case NETDEV_UP:
		bnxt_re_dispatch_event(&rdev->ibdev, NULL, 1,
				       IB_EVENT_PORT_ACTIVE);
		break;
	case NETDEV_DOWN:
		bnxt_re_dev_stop(rdev);
		break;
	case NETDEV_CHANGE:
		if (!netif_carrier_ok(rdev->netdev))
			bnxt_re_dev_stop(rdev);
		else if (netif_carrier_ok(rdev->netdev))
			bnxt_re_dispatch_event(&rdev->ibdev, NULL, 1,
					       IB_EVENT_PORT_ACTIVE);
		ib_get_eth_speed(&rdev->ibdev, 1, &rdev->active_speed,
				 &rdev->active_width);
		break;
	default:
		break;
	}
	ib_device_put(&rdev->ibdev);
exit:
	put_device(&rdev->ibdev.dev);
	kfree(re_work);
}

/*
 * "Notifier chain callback can be invoked for the same chain from
 * different CPUs at the same time".
 *
 * For cases when the netdev is already present, our call to the
 * register_netdevice_notifier() will actually get the rtnl_lock()
 * before sending NETDEV_REGISTER and (if up) NETDEV_UP
 * events.
 *
 * But for cases when the netdev is not already present, the notifier
 * chain is subjected to be invoked from different CPUs simultaneously.
 *
 * This is protected by the netdev_mutex.
 */
static int bnxt_re_netdev_event(struct notifier_block *notifier,
				unsigned long event, void *ptr)
{
	struct net_device *real_dev, *netdev = netdev_notifier_info_to_dev(ptr);
	struct bnxt_re_work *re_work;
	struct bnxt_re_dev *rdev;
	int rc = 0;
	bool sch_work = false;
	bool release = true;

	real_dev = rdma_vlan_dev_real_dev(netdev);
	if (!real_dev)
		real_dev = netdev;

	rdev = bnxt_re_from_netdev(real_dev);
	if (!rdev && event != NETDEV_REGISTER)
		return NOTIFY_OK;

	if (real_dev != netdev)
		goto exit;

	switch (event) {
	case NETDEV_REGISTER:
		if (rdev)
			break;
		rc = bnxt_re_add_device(&rdev, real_dev,
					BNXT_QPLIB_WQE_MODE_STATIC);
		if (!rc)
			sch_work = true;
		release = false;
		break;

	case NETDEV_UNREGISTER:
		ib_unregister_device_queued(&rdev->ibdev);
		break;

	default:
		sch_work = true;
		break;
	}
	if (sch_work) {
		/* Allocate for the deferred task */
		re_work = kzalloc(sizeof(*re_work), GFP_KERNEL);
		if (re_work) {
			get_device(&rdev->ibdev.dev);
			re_work->rdev = rdev;
			re_work->event = event;
			re_work->vlan_dev = (real_dev == netdev ?
					     NULL : netdev);
			INIT_WORK(&re_work->work, bnxt_re_task);
			queue_work(bnxt_re_wq, &re_work->work);
		}
	}

exit:
	if (rdev && release)
		ib_device_put(&rdev->ibdev);
	return NOTIFY_DONE;
}

static struct notifier_block bnxt_re_netdev_notifier = {
	.notifier_call = bnxt_re_netdev_event
};

static int __init bnxt_re_mod_init(void)
{
	int rc = 0;

	pr_info("%s: %s", ROCE_DRV_MODULE_NAME, version);

	bnxt_re_wq = create_singlethread_workqueue("bnxt_re");
	if (!bnxt_re_wq)
		return -ENOMEM;

	INIT_LIST_HEAD(&bnxt_re_dev_list);

	rc = register_netdevice_notifier(&bnxt_re_netdev_notifier);
	if (rc) {
		pr_err("%s: Cannot register to netdevice_notifier",
		       ROCE_DRV_MODULE_NAME);
		goto err_netdev;
	}
	return 0;

err_netdev:
	destroy_workqueue(bnxt_re_wq);

	return rc;
}

static void __exit bnxt_re_mod_exit(void)
{
	struct bnxt_re_dev *rdev;

	unregister_netdevice_notifier(&bnxt_re_netdev_notifier);
	if (bnxt_re_wq)
		destroy_workqueue(bnxt_re_wq);
	list_for_each_entry(rdev, &bnxt_re_dev_list, list) {
		/* VF device removal should be called before the removal
		 * of PF device. Queue VFs unregister first, so that VFs
		 * shall be removed before the PF during the call of
		 * ib_unregister_driver.
		 */
		if (rdev->is_virtfn)
			ib_unregister_device(&rdev->ibdev);
	}
	ib_unregister_driver(RDMA_DRIVER_BNXT_RE);
}

module_init(bnxt_re_mod_init);
module_exit(bnxt_re_mod_exit);
