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
#include <linux/auxiliary_bus.h>

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
MODULE_DESCRIPTION(BNXT_RE_DESC);
MODULE_LICENSE("Dual BSD/GPL");

/* globals */
static DEFINE_MUTEX(bnxt_re_mutex);

static void bnxt_re_stop_irq(void *handle);
static void bnxt_re_dev_stop(struct bnxt_re_dev *rdev);
static int bnxt_re_netdev_event(struct notifier_block *notifier,
				unsigned long event, void *ptr);
static struct bnxt_re_dev *bnxt_re_from_netdev(struct net_device *netdev);
static void bnxt_re_dev_uninit(struct bnxt_re_dev *rdev);
static int bnxt_re_hwrm_qcaps(struct bnxt_re_dev *rdev);

static int bnxt_re_hwrm_qcfg(struct bnxt_re_dev *rdev, u32 *db_len,
			     u32 *offset);
static void bnxt_re_set_db_offset(struct bnxt_re_dev *rdev)
{
	struct bnxt_qplib_chip_ctx *cctx;
	struct bnxt_en_dev *en_dev;
	struct bnxt_qplib_res *res;
	u32 l2db_len = 0;
	u32 offset = 0;
	u32 barlen;
	int rc;

	res = &rdev->qplib_res;
	en_dev = rdev->en_dev;
	cctx = rdev->chip_ctx;

	/* Issue qcfg */
	rc = bnxt_re_hwrm_qcfg(rdev, &l2db_len, &offset);
	if (rc)
		dev_info(rdev_to_dev(rdev),
			 "Couldn't get DB bar size, Low latency framework is disabled\n");
	/* set register offsets for both UC and WC */
	res->dpi_tbl.ucreg.offset = res->is_vf ? BNXT_QPLIB_DBR_VF_DB_OFFSET :
						 BNXT_QPLIB_DBR_PF_DB_OFFSET;
	res->dpi_tbl.wcreg.offset = res->dpi_tbl.ucreg.offset;

	/* If WC mapping is disabled by L2 driver then en_dev->l2_db_size
	 * is equal to the DB-Bar actual size. This indicates that L2
	 * is mapping entire bar as UC-. RoCE driver can't enable WC mapping
	 * in such cases and DB-push will be disabled.
	 */
	barlen = pci_resource_len(res->pdev, RCFW_DBR_PCI_BAR_REGION);
	if (cctx->modes.db_push && l2db_len && en_dev->l2_db_size != barlen) {
		res->dpi_tbl.wcreg.offset = en_dev->l2_db_size;
		dev_info(rdev_to_dev(rdev),  "Low latency framework is enabled\n");
	}
}

static void bnxt_re_set_drv_mode(struct bnxt_re_dev *rdev, u8 mode)
{
	struct bnxt_qplib_chip_ctx *cctx;

	cctx = rdev->chip_ctx;
	cctx->modes.wqe_mode = bnxt_qplib_is_chip_gen_p5(rdev->chip_ctx) ?
			       mode : BNXT_QPLIB_WQE_MODE_STATIC;
	if (bnxt_re_hwrm_qcaps(rdev))
		dev_err(rdev_to_dev(rdev),
			"Failed to query hwrm qcaps\n");
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
	int rc;

	en_dev = rdev->en_dev;

	chip_ctx = kzalloc(sizeof(*chip_ctx), GFP_KERNEL);
	if (!chip_ctx)
		return -ENOMEM;
	chip_ctx->chip_num = en_dev->chip_num;
	chip_ctx->hw_stats_size = en_dev->hw_ring_stats_size;

	rdev->chip_ctx = chip_ctx;
	/* rest members to follow eventually */

	rdev->qplib_res.cctx = rdev->chip_ctx;
	rdev->rcfw.res = &rdev->qplib_res;
	rdev->qplib_res.dattr = &rdev->dev_attr;
	rdev->qplib_res.is_vf = BNXT_EN_VF(en_dev);

	bnxt_re_set_drv_mode(rdev, wqe_mode);

	bnxt_re_set_db_offset(rdev);
	rc = bnxt_qplib_map_db_bar(&rdev->qplib_res);
	if (rc)
		return rc;

	if (bnxt_qplib_determine_atomics(en_dev->pdev))
		ibdev_info(&rdev->ibdev,
			   "platform doesn't support global atomics.");
	return 0;
}

/* SR-IOV helper functions */

static void bnxt_re_get_sriov_func_type(struct bnxt_re_dev *rdev)
{
	if (BNXT_EN_VF(rdev->en_dev))
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

static void bnxt_re_vf_res_config(struct bnxt_re_dev *rdev)
{

	if (test_bit(BNXT_RE_FLAG_ERR_DEVICE_DETACHED, &rdev->flags))
		return;
	rdev->num_vfs = pci_sriov_get_totalvfs(rdev->en_dev->pdev);
	if (!bnxt_qplib_is_chip_gen_p5(rdev->chip_ctx)) {
		bnxt_re_set_resource_limits(rdev);
		bnxt_qplib_set_func_resources(&rdev->qplib_res, &rdev->rcfw,
					      &rdev->qplib_ctx);
	}
}

static void bnxt_re_shutdown(struct auxiliary_device *adev)
{
	struct bnxt_re_dev *rdev = auxiliary_get_drvdata(adev);

	if (!rdev)
		return;
	ib_unregister_device(&rdev->ibdev);
	bnxt_re_dev_uninit(rdev);
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
	struct bnxt_msix_entry *msix_ent = rdev->en_dev->msix_entries;
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
		rdev->en_dev->msix_entries[indx].vector = ent[indx].vector;

	rc = bnxt_qplib_rcfw_start_irq(rcfw, msix_ent[BNXT_RE_AEQ_IDX].vector,
				       false);
	if (rc) {
		ibdev_warn(&rdev->ibdev, "Failed to reinit CREQ\n");
		return;
	}
	for (indx = BNXT_RE_NQ_IDX ; indx < rdev->num_msix; indx++) {
		nq = &rdev->nq[indx - 1];
		rc = bnxt_qplib_nq_start_irq(nq, indx - 1,
					     msix_ent[indx].vector, false);
		if (rc) {
			ibdev_warn(&rdev->ibdev, "Failed to reinit NQ index %d\n",
				   indx - 1);
			return;
		}
	}
}

static struct bnxt_ulp_ops bnxt_re_ulp_ops = {
	.ulp_irq_stop = bnxt_re_stop_irq,
	.ulp_irq_restart = bnxt_re_start_irq
};

/* RoCE -> Net driver */

static int bnxt_re_register_netdev(struct bnxt_re_dev *rdev)
{
	struct bnxt_en_dev *en_dev;
	int rc;

	en_dev = rdev->en_dev;

	rc = bnxt_register_dev(en_dev, &bnxt_re_ulp_ops, rdev);
	if (!rc)
		rdev->qplib_res.pdev = rdev->en_dev->pdev;
	return rc;
}

static void bnxt_re_init_hwrm_hdr(struct input *hdr, u16 opcd)
{
	hdr->req_type = cpu_to_le16(opcd);
	hdr->cmpl_ring = cpu_to_le16(-1);
	hdr->target_id = cpu_to_le16(-1);
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

/* Query device config using common hwrm */
static int bnxt_re_hwrm_qcfg(struct bnxt_re_dev *rdev, u32 *db_len,
			     u32 *offset)
{
	struct bnxt_en_dev *en_dev = rdev->en_dev;
	struct hwrm_func_qcfg_output resp = {0};
	struct hwrm_func_qcfg_input req = {0};
	struct bnxt_fw_msg fw_msg = {};
	int rc;

	bnxt_re_init_hwrm_hdr((void *)&req, HWRM_FUNC_QCFG);
	req.fid = cpu_to_le16(0xffff);
	bnxt_re_fill_fw_msg(&fw_msg, (void *)&req, sizeof(req), (void *)&resp,
			    sizeof(resp), DFLT_HWRM_CMD_TIMEOUT);
	rc = bnxt_send_msg(en_dev, &fw_msg);
	if (!rc) {
		*db_len = PAGE_ALIGN(le16_to_cpu(resp.l2_doorbell_bar_size_kb) * 1024);
		*offset = PAGE_ALIGN(le16_to_cpu(resp.legacy_l2_db_size_kb) * 1024);
	}
	return rc;
}

/* Query function capabilities using common hwrm */
int bnxt_re_hwrm_qcaps(struct bnxt_re_dev *rdev)
{
	struct bnxt_en_dev *en_dev = rdev->en_dev;
	struct hwrm_func_qcaps_output resp = {};
	struct hwrm_func_qcaps_input req = {};
	struct bnxt_qplib_chip_ctx *cctx;
	struct bnxt_fw_msg fw_msg = {};
	int rc;

	cctx = rdev->chip_ctx;
	bnxt_re_init_hwrm_hdr((void *)&req, HWRM_FUNC_QCAPS);
	req.fid = cpu_to_le16(0xffff);
	bnxt_re_fill_fw_msg(&fw_msg, (void *)&req, sizeof(req), (void *)&resp,
			    sizeof(resp), DFLT_HWRM_CMD_TIMEOUT);

	rc = bnxt_send_msg(en_dev, &fw_msg);
	if (rc)
		return rc;
	cctx->modes.db_push = le32_to_cpu(resp.flags) & FUNC_QCAPS_RESP_FLAGS_WCB_PUSH_MODE;

	cctx->modes.dbr_pacing =
		le32_to_cpu(resp.flags_ext2) &
		FUNC_QCAPS_RESP_FLAGS_EXT2_DBR_PACING_EXT_SUPPORTED;
	return 0;
}

static int bnxt_re_hwrm_dbr_pacing_qcfg(struct bnxt_re_dev *rdev)
{
	struct hwrm_func_dbr_pacing_qcfg_output resp = {};
	struct hwrm_func_dbr_pacing_qcfg_input req = {};
	struct bnxt_en_dev *en_dev = rdev->en_dev;
	struct bnxt_qplib_chip_ctx *cctx;
	struct bnxt_fw_msg fw_msg = {};
	int rc;

	cctx = rdev->chip_ctx;
	bnxt_re_init_hwrm_hdr((void *)&req, HWRM_FUNC_DBR_PACING_QCFG);
	bnxt_re_fill_fw_msg(&fw_msg, (void *)&req, sizeof(req), (void *)&resp,
			    sizeof(resp), DFLT_HWRM_CMD_TIMEOUT);
	rc = bnxt_send_msg(en_dev, &fw_msg);
	if (rc)
		return rc;

	if ((le32_to_cpu(resp.dbr_stat_db_fifo_reg) &
	    FUNC_DBR_PACING_QCFG_RESP_DBR_STAT_DB_FIFO_REG_ADDR_SPACE_MASK) ==
		FUNC_DBR_PACING_QCFG_RESP_DBR_STAT_DB_FIFO_REG_ADDR_SPACE_GRC)
		cctx->dbr_stat_db_fifo =
			le32_to_cpu(resp.dbr_stat_db_fifo_reg) &
			~FUNC_DBR_PACING_QCFG_RESP_DBR_STAT_DB_FIFO_REG_ADDR_SPACE_MASK;
	return 0;
}

/* Update the pacing tunable parameters to the default values */
static void bnxt_re_set_default_pacing_data(struct bnxt_re_dev *rdev)
{
	struct bnxt_qplib_db_pacing_data *pacing_data = rdev->qplib_res.pacing_data;

	pacing_data->do_pacing = rdev->pacing.dbr_def_do_pacing;
	pacing_data->pacing_th = rdev->pacing.pacing_algo_th;
	pacing_data->alarm_th =
		pacing_data->pacing_th * BNXT_RE_PACING_ALARM_TH_MULTIPLE;
}

static void __wait_for_fifo_occupancy_below_th(struct bnxt_re_dev *rdev)
{
	u32 read_val, fifo_occup;

	/* loop shouldn't run infintely as the occupancy usually goes
	 * below pacing algo threshold as soon as pacing kicks in.
	 */
	while (1) {
		read_val = readl(rdev->en_dev->bar0 + rdev->pacing.dbr_db_fifo_reg_off);
		fifo_occup = BNXT_RE_MAX_FIFO_DEPTH -
			((read_val & BNXT_RE_DB_FIFO_ROOM_MASK) >>
			 BNXT_RE_DB_FIFO_ROOM_SHIFT);
		/* Fifo occupancy cannot be greater the MAX FIFO depth */
		if (fifo_occup > BNXT_RE_MAX_FIFO_DEPTH)
			break;

		if (fifo_occup < rdev->qplib_res.pacing_data->pacing_th)
			break;
	}
}

static void bnxt_re_db_fifo_check(struct work_struct *work)
{
	struct bnxt_re_dev *rdev = container_of(work, struct bnxt_re_dev,
			dbq_fifo_check_work);
	struct bnxt_qplib_db_pacing_data *pacing_data;
	u32 pacing_save;

	if (!mutex_trylock(&rdev->pacing.dbq_lock))
		return;
	pacing_data = rdev->qplib_res.pacing_data;
	pacing_save = rdev->pacing.do_pacing_save;
	__wait_for_fifo_occupancy_below_th(rdev);
	cancel_delayed_work_sync(&rdev->dbq_pacing_work);
	if (pacing_save > rdev->pacing.dbr_def_do_pacing) {
		/* Double the do_pacing value during the congestion */
		pacing_save = pacing_save << 1;
	} else {
		/*
		 * when a new congestion is detected increase the do_pacing
		 * by 8 times. And also increase the pacing_th by 4 times. The
		 * reason to increase pacing_th is to give more space for the
		 * queue to oscillate down without getting empty, but also more
		 * room for the queue to increase without causing another alarm.
		 */
		pacing_save = pacing_save << 3;
		pacing_data->pacing_th = rdev->pacing.pacing_algo_th * 4;
	}

	if (pacing_save > BNXT_RE_MAX_DBR_DO_PACING)
		pacing_save = BNXT_RE_MAX_DBR_DO_PACING;

	pacing_data->do_pacing = pacing_save;
	rdev->pacing.do_pacing_save = pacing_data->do_pacing;
	pacing_data->alarm_th =
		pacing_data->pacing_th * BNXT_RE_PACING_ALARM_TH_MULTIPLE;
	schedule_delayed_work(&rdev->dbq_pacing_work,
			      msecs_to_jiffies(rdev->pacing.dbq_pacing_time));
	rdev->stats.pacing.alerts++;
	mutex_unlock(&rdev->pacing.dbq_lock);
}

static void bnxt_re_pacing_timer_exp(struct work_struct *work)
{
	struct bnxt_re_dev *rdev = container_of(work, struct bnxt_re_dev,
			dbq_pacing_work.work);
	struct bnxt_qplib_db_pacing_data *pacing_data;
	u32 read_val, fifo_occup;

	if (!mutex_trylock(&rdev->pacing.dbq_lock))
		return;

	pacing_data = rdev->qplib_res.pacing_data;
	read_val = readl(rdev->en_dev->bar0 + rdev->pacing.dbr_db_fifo_reg_off);
	fifo_occup = BNXT_RE_MAX_FIFO_DEPTH -
		((read_val & BNXT_RE_DB_FIFO_ROOM_MASK) >>
		 BNXT_RE_DB_FIFO_ROOM_SHIFT);

	if (fifo_occup > pacing_data->pacing_th)
		goto restart_timer;

	/*
	 * Instead of immediately going back to the default do_pacing
	 * reduce it by 1/8 times and restart the timer.
	 */
	pacing_data->do_pacing = pacing_data->do_pacing - (pacing_data->do_pacing >> 3);
	pacing_data->do_pacing = max_t(u32, rdev->pacing.dbr_def_do_pacing, pacing_data->do_pacing);
	if (pacing_data->do_pacing <= rdev->pacing.dbr_def_do_pacing) {
		bnxt_re_set_default_pacing_data(rdev);
		rdev->stats.pacing.complete++;
		goto dbq_unlock;
	}

restart_timer:
	schedule_delayed_work(&rdev->dbq_pacing_work,
			      msecs_to_jiffies(rdev->pacing.dbq_pacing_time));
	rdev->stats.pacing.resched++;
dbq_unlock:
	rdev->pacing.do_pacing_save = pacing_data->do_pacing;
	mutex_unlock(&rdev->pacing.dbq_lock);
}

void bnxt_re_pacing_alert(struct bnxt_re_dev *rdev)
{
	struct bnxt_qplib_db_pacing_data *pacing_data;

	if (!rdev->pacing.dbr_pacing)
		return;
	mutex_lock(&rdev->pacing.dbq_lock);
	pacing_data = rdev->qplib_res.pacing_data;

	/*
	 * Increase the alarm_th to max so that other user lib instances do not
	 * keep alerting the driver.
	 */
	pacing_data->alarm_th = BNXT_RE_MAX_FIFO_DEPTH;
	pacing_data->do_pacing = BNXT_RE_MAX_DBR_DO_PACING;
	cancel_work_sync(&rdev->dbq_fifo_check_work);
	schedule_work(&rdev->dbq_fifo_check_work);
	mutex_unlock(&rdev->pacing.dbq_lock);
}

static int bnxt_re_initialize_dbr_pacing(struct bnxt_re_dev *rdev)
{
	if (bnxt_re_hwrm_dbr_pacing_qcfg(rdev))
		return -EIO;

	/* Allocate a page for app use */
	rdev->pacing.dbr_page = (void *)__get_free_page(GFP_KERNEL);
	if (!rdev->pacing.dbr_page)
		return -ENOMEM;

	memset((u8 *)rdev->pacing.dbr_page, 0, PAGE_SIZE);
	rdev->qplib_res.pacing_data = (struct bnxt_qplib_db_pacing_data *)rdev->pacing.dbr_page;

	/* MAP HW window 2 for reading db fifo depth */
	writel(rdev->chip_ctx->dbr_stat_db_fifo & BNXT_GRC_BASE_MASK,
	       rdev->en_dev->bar0 + BNXT_GRCPF_REG_WINDOW_BASE_OUT + 4);
	rdev->pacing.dbr_db_fifo_reg_off =
		(rdev->chip_ctx->dbr_stat_db_fifo & BNXT_GRC_OFFSET_MASK) +
		 BNXT_RE_GRC_FIFO_REG_BASE;
	rdev->pacing.dbr_bar_addr =
		pci_resource_start(rdev->qplib_res.pdev, 0) + rdev->pacing.dbr_db_fifo_reg_off;

	rdev->pacing.pacing_algo_th = BNXT_RE_PACING_ALGO_THRESHOLD;
	rdev->pacing.dbq_pacing_time = BNXT_RE_DBR_PACING_TIME;
	rdev->pacing.dbr_def_do_pacing = BNXT_RE_DBR_DO_PACING_NO_CONGESTION;
	rdev->pacing.do_pacing_save = rdev->pacing.dbr_def_do_pacing;
	rdev->qplib_res.pacing_data->fifo_max_depth = BNXT_RE_MAX_FIFO_DEPTH;
	rdev->qplib_res.pacing_data->fifo_room_mask = BNXT_RE_DB_FIFO_ROOM_MASK;
	rdev->qplib_res.pacing_data->fifo_room_shift = BNXT_RE_DB_FIFO_ROOM_SHIFT;
	rdev->qplib_res.pacing_data->grc_reg_offset = rdev->pacing.dbr_db_fifo_reg_off;
	bnxt_re_set_default_pacing_data(rdev);
	/* Initialize worker for DBR Pacing */
	INIT_WORK(&rdev->dbq_fifo_check_work, bnxt_re_db_fifo_check);
	INIT_DELAYED_WORK(&rdev->dbq_pacing_work, bnxt_re_pacing_timer_exp);
	return 0;
}

static void bnxt_re_deinitialize_dbr_pacing(struct bnxt_re_dev *rdev)
{
	cancel_work_sync(&rdev->dbq_fifo_check_work);
	cancel_delayed_work_sync(&rdev->dbq_pacing_work);
	if (rdev->pacing.dbr_page)
		free_page((u64)rdev->pacing.dbr_page);

	rdev->pacing.dbr_page = NULL;
	rdev->pacing.dbr_pacing = false;
}

static int bnxt_re_net_ring_free(struct bnxt_re_dev *rdev,
				 u16 fw_ring_id, int type)
{
	struct bnxt_en_dev *en_dev;
	struct hwrm_ring_free_input req = {};
	struct hwrm_ring_free_output resp;
	struct bnxt_fw_msg fw_msg = {};
	int rc = -EINVAL;

	if (!rdev)
		return rc;

	en_dev = rdev->en_dev;

	if (!en_dev)
		return rc;

	if (test_bit(BNXT_RE_FLAG_ERR_DEVICE_DETACHED, &rdev->flags))
		return 0;

	bnxt_re_init_hwrm_hdr((void *)&req, HWRM_RING_FREE);
	req.ring_type = type;
	req.ring_id = cpu_to_le16(fw_ring_id);
	bnxt_re_fill_fw_msg(&fw_msg, (void *)&req, sizeof(req), (void *)&resp,
			    sizeof(resp), DFLT_HWRM_CMD_TIMEOUT);
	rc = bnxt_send_msg(en_dev, &fw_msg);
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
	struct hwrm_ring_alloc_input req = {};
	struct hwrm_ring_alloc_output resp;
	struct bnxt_fw_msg fw_msg = {};
	int rc = -EINVAL;

	if (!en_dev)
		return rc;

	bnxt_re_init_hwrm_hdr((void *)&req, HWRM_RING_ALLOC);
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
	rc = bnxt_send_msg(en_dev, &fw_msg);
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
	struct bnxt_fw_msg fw_msg = {};
	int rc = -EINVAL;

	if (!en_dev)
		return rc;

	if (test_bit(BNXT_RE_FLAG_ERR_DEVICE_DETACHED, &rdev->flags))
		return 0;

	bnxt_re_init_hwrm_hdr((void *)&req, HWRM_STAT_CTX_FREE);
	req.stat_ctx_id = cpu_to_le32(fw_stats_ctx_id);
	bnxt_re_fill_fw_msg(&fw_msg, (void *)&req, sizeof(req), (void *)&resp,
			    sizeof(resp), DFLT_HWRM_CMD_TIMEOUT);
	rc = bnxt_send_msg(en_dev, &fw_msg);
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
	struct hwrm_stat_ctx_alloc_output resp = {};
	struct hwrm_stat_ctx_alloc_input req = {};
	struct bnxt_en_dev *en_dev = rdev->en_dev;
	struct bnxt_fw_msg fw_msg = {};
	int rc = -EINVAL;

	*fw_stats_ctx_id = INVALID_STATS_CTX_ID;

	if (!en_dev)
		return rc;

	bnxt_re_init_hwrm_hdr((void *)&req, HWRM_STAT_CTX_ALLOC);
	req.update_period_ms = cpu_to_le32(1000);
	req.stats_dma_addr = cpu_to_le64(dma_map);
	req.stats_dma_length = cpu_to_le16(chip_ctx->hw_stats_size);
	req.stat_ctx_flags = STAT_CTX_ALLOC_REQ_STAT_CTX_FLAGS_ROCE;
	bnxt_re_fill_fw_msg(&fw_msg, (void *)&req, sizeof(req), (void *)&resp,
			    sizeof(resp), DFLT_HWRM_CMD_TIMEOUT);
	rc = bnxt_send_msg(en_dev, &fw_msg);
	if (!rc)
		*fw_stats_ctx_id = le32_to_cpu(resp.stat_ctx_id);

	return rc;
}

static void bnxt_re_disassociate_ucontext(struct ib_ucontext *ibcontext)
{
}

/* Device */

static struct bnxt_re_dev *bnxt_re_from_netdev(struct net_device *netdev)
{
	struct ib_device *ibdev =
		ib_device_get_by_netdev(netdev, RDMA_DRIVER_BNXT_RE);
	if (!ibdev)
		return NULL;

	return container_of(ibdev, struct bnxt_re_dev, ibdev);
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
	.dealloc_pd = bnxt_re_dealloc_pd,
	.dealloc_ucontext = bnxt_re_dealloc_ucontext,
	.del_gid = bnxt_re_del_gid,
	.dereg_mr = bnxt_re_dereg_mr,
	.destroy_ah = bnxt_re_destroy_ah,
	.destroy_cq = bnxt_re_destroy_cq,
	.destroy_qp = bnxt_re_destroy_qp,
	.destroy_srq = bnxt_re_destroy_srq,
	.device_group = &bnxt_re_dev_attr_group,
	.disassociate_ucontext = bnxt_re_disassociate_ucontext,
	.get_dev_fw_str = bnxt_re_query_fw_str,
	.get_dma_mr = bnxt_re_get_dma_mr,
	.get_hw_stats = bnxt_re_ib_get_hw_stats,
	.get_link_layer = bnxt_re_get_link_layer,
	.get_port_immutable = bnxt_re_get_port_immutable,
	.map_mr_sg = bnxt_re_map_mr_sg,
	.mmap = bnxt_re_mmap,
	.mmap_free = bnxt_re_mmap_free,
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
	.reg_user_mr_dmabuf = bnxt_re_reg_user_mr_dmabuf,
	.req_notify_cq = bnxt_re_req_notify_cq,
	.resize_cq = bnxt_re_resize_cq,
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
	strscpy(ibdev->node_desc, BNXT_RE_DESC " HCA",
		strlen(BNXT_RE_DESC) + 5);
	ibdev->phys_port_cnt = 1;

	addrconf_addr_eui48((u8 *)&ibdev->node_guid, rdev->netdev->dev_addr);

	ibdev->num_comp_vectors	= rdev->num_msix - 1;
	ibdev->dev.parent = &rdev->en_dev->pdev->dev;
	ibdev->local_dma_lkey = BNXT_QPLIB_RSVD_LKEY;

	if (IS_ENABLED(CONFIG_INFINIBAND_USER_ACCESS))
		ibdev->driver_def = bnxt_re_uapi_defs;

	ib_set_device_ops(ibdev, &bnxt_re_dev_ops);
	ret = ib_device_set_netdev(&rdev->ibdev, rdev->netdev, 1);
	if (ret)
		return ret;

	dma_set_max_seg_size(&rdev->en_dev->pdev->dev, UINT_MAX);
	ibdev->uverbs_cmd_mask |= BIT_ULL(IB_USER_VERBS_CMD_POLL_CQ);
	return ib_register_device(ibdev, "bnxt_re%d", &rdev->en_dev->pdev->dev);
}

static struct bnxt_re_dev *bnxt_re_dev_add(struct bnxt_aux_priv *aux_priv,
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
	rdev->nb.notifier_call = NULL;
	rdev->netdev = en_dev->net;
	rdev->en_dev = en_dev;
	rdev->id = rdev->en_dev->pdev->devfn;
	INIT_LIST_HEAD(&rdev->qp_list);
	mutex_init(&rdev->qp_lock);
	mutex_init(&rdev->pacing.dbq_lock);
	atomic_set(&rdev->stats.res.qp_count, 0);
	atomic_set(&rdev->stats.res.cq_count, 0);
	atomic_set(&rdev->stats.res.srq_count, 0);
	atomic_set(&rdev->stats.res.mr_count, 0);
	atomic_set(&rdev->stats.res.mw_count, 0);
	atomic_set(&rdev->stats.res.ah_count, 0);
	atomic_set(&rdev->stats.res.pd_count, 0);
	rdev->cosq[0] = 0xFFFF;
	rdev->cosq[1] = 0xFFFF;

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
	struct bnxt_re_srq *srq = container_of(qp->qplib_qp.srq, struct bnxt_re_srq,
					       qplib_srq);
	struct creq_qp_error_notification *err_event;
	struct ib_event event = {};
	unsigned int flags;

	if (qp->qplib_qp.state == CMDQ_MODIFY_QP_NEW_STATE_ERR &&
	    rdma_is_kernel_res(&qp->ib_qp.res)) {
		flags = bnxt_re_lock_cqs(qp);
		bnxt_qplib_add_flush_qp(&qp->qplib_qp);
		bnxt_re_unlock_cqs(qp, flags);
	}

	event.device = &qp->rdev->ibdev;
	event.element.qp = &qp->ib_qp;
	event.event = IB_EVENT_QP_FATAL;

	err_event = (struct creq_qp_error_notification *)qp_event;

	switch (err_event->req_err_state_reason) {
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_OPCODE_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_TIMEOUT_RETRY_LIMIT:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_RNR_TIMEOUT_RETRY_LIMIT:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_NAK_ARRIVAL_2:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_NAK_ARRIVAL_3:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_INVALID_READ_RESP:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_ILLEGAL_BIND:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_ILLEGAL_FAST_REG:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_ILLEGAL_INVALIDATE:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_RETRAN_LOCAL_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_AV_DOMAIN_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_PROD_WQE_MSMTCH_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_PSN_RANGE_CHECK_ERROR:
		event.event = IB_EVENT_QP_ACCESS_ERR;
		break;
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_NAK_ARRIVAL_1:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_NAK_ARRIVAL_4:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_READ_RESP_LENGTH:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_WQE_FORMAT_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_ORRQ_FORMAT_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_INVALID_AVID_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_SERV_TYPE_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_INVALID_OP_ERROR:
		event.event = IB_EVENT_QP_REQ_ERR;
		break;
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_RX_MEMORY_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_TX_MEMORY_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_CMP_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_CQ_LOAD_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_TX_PCI_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_RX_PCI_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_REQ_ERR_STATE_REASON_REQ_RETX_SETUP_ERROR:
		event.event = IB_EVENT_QP_FATAL;
		break;

	default:
		break;
	}

	switch (err_event->res_err_state_reason) {
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_EXCEED_MAX:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_PAYLOAD_LENGTH_MISMATCH:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_PSN_SEQ_ERROR_RETRY_LIMIT:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_RX_INVALID_R_KEY:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_RX_DOMAIN_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_RX_NO_PERMISSION:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_RX_RANGE_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_TX_INVALID_R_KEY:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_TX_DOMAIN_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_TX_NO_PERMISSION:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_TX_RANGE_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_UNALIGN_ATOMIC:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_PSN_NOT_FOUND:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_INVALID_DUP_RKEY:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_IRRQ_FORMAT_ERROR:
		event.event = IB_EVENT_QP_ACCESS_ERR;
		break;
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_EXCEEDS_WQE:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_WQE_FORMAT_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_UNSUPPORTED_OPCODE:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_REM_INVALIDATE:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_OPCODE_ERROR:
		event.event = IB_EVENT_QP_REQ_ERR;
		break;
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_IRRQ_OFLOW:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_CMP_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_CQ_LOAD_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_TX_PCI_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_RX_PCI_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_MEMORY_ERROR:
		event.event = IB_EVENT_QP_FATAL;
		break;
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_SRQ_LOAD_ERROR:
	case CREQ_QP_ERROR_NOTIFICATION_RES_ERR_STATE_REASON_RES_SRQ_ERROR:
		if (srq)
			event.event = IB_EVENT_SRQ_ERR;
		break;
	default:
		break;
	}

	if (err_event->res_err_state_reason || err_event->req_err_state_reason) {
		ibdev_dbg(&qp->rdev->ibdev,
			  "%s %s qp_id: %d cons (%d %d) req (%d %d) res (%d %d)\n",
			   __func__, rdma_is_kernel_res(&qp->ib_qp.res) ? "kernel" : "user",
			   qp->qplib_qp.id,
			   err_event->sq_cons_idx,
			   err_event->rq_cons_idx,
			   err_event->req_slow_path_state,
			   err_event->req_err_state_reason,
			   err_event->res_slow_path_state,
			   err_event->res_err_state_reason);
	} else {
		if (srq)
			event.event = IB_EVENT_QP_LAST_WQE_REACHED;
	}

	if (event.event == IB_EVENT_SRQ_ERR && srq->ib_srq.event_handler)  {
		(*srq->ib_srq.event_handler)(&event,
				srq->ib_srq.srq_context);
	} else if (event.device && qp->ib_qp.event_handler) {
		qp->ib_qp.event_handler(&event, qp->ib_qp.qp_context);
	}

	return 0;
}

static int bnxt_re_handle_cq_async_error(void *event, struct bnxt_re_cq *cq)
{
	struct creq_cq_error_notification *cqerr;
	struct ib_event ibevent = {};

	cqerr = event;
	switch (cqerr->cq_err_reason) {
	case CREQ_CQ_ERROR_NOTIFICATION_CQ_ERR_REASON_REQ_CQ_INVALID_ERROR:
	case CREQ_CQ_ERROR_NOTIFICATION_CQ_ERR_REASON_REQ_CQ_OVERFLOW_ERROR:
	case CREQ_CQ_ERROR_NOTIFICATION_CQ_ERR_REASON_REQ_CQ_LOAD_ERROR:
	case CREQ_CQ_ERROR_NOTIFICATION_CQ_ERR_REASON_RES_CQ_INVALID_ERROR:
	case CREQ_CQ_ERROR_NOTIFICATION_CQ_ERR_REASON_RES_CQ_OVERFLOW_ERROR:
	case CREQ_CQ_ERROR_NOTIFICATION_CQ_ERR_REASON_RES_CQ_LOAD_ERROR:
		ibevent.event = IB_EVENT_CQ_ERR;
		break;
	default:
		break;
	}

	if (ibevent.event == IB_EVENT_CQ_ERR && cq->ib_cq.event_handler) {
		ibevent.element.cq = &cq->ib_cq;
		ibevent.device = &cq->rdev->ibdev;

		ibdev_dbg(&cq->rdev->ibdev,
			  "%s err reason %d\n", __func__, cqerr->cq_err_reason);
		cq->ib_cq.event_handler(&ibevent, cq->ib_cq.cq_context);
	}

	return 0;
}

static int bnxt_re_handle_affi_async_event(struct creq_qp_event *affi_async,
					   void *obj)
{
	struct bnxt_qplib_qp *lib_qp;
	struct bnxt_qplib_cq *lib_cq;
	struct bnxt_re_qp *qp;
	struct bnxt_re_cq *cq;
	int rc = 0;
	u8 event;

	if (!obj)
		return rc; /* QP was already dead, still return success */

	event = affi_async->event;
	switch (event) {
	case CREQ_QP_EVENT_EVENT_QP_ERROR_NOTIFICATION:
		lib_qp = obj;
		qp = container_of(lib_qp, struct bnxt_re_qp, qplib_qp);
		rc = bnxt_re_handle_qp_async_event(affi_async, qp);
		break;
	case CREQ_QP_EVENT_EVENT_CQ_ERROR_NOTIFICATION:
		lib_cq = obj;
		cq = container_of(lib_cq, struct bnxt_re_cq, qplib_cq);
		rc = bnxt_re_handle_cq_async_error(affi_async, cq);
		break;
	default:
		rc = -EINVAL;
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

	ib_event.device = &srq->rdev->ibdev;
	ib_event.element.srq = &srq->ib_srq;

	if (srq->ib_srq.event_handler) {
		if (event == NQ_SRQ_EVENT_EVENT_SRQ_THRESHOLD_EVENT)
			ib_event.event = IB_EVENT_SRQ_LIMIT_REACHED;
		(*srq->ib_srq.event_handler)(&ib_event,
					     srq->ib_srq.srq_context);
	}
	return 0;
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
				   rdev->en_dev->msix_entries[indx].db_offset;
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
					  i - 1, rdev->en_dev->msix_entries[i].vector,
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
	int rc, i;
	u8 type;

	/* Configure and allocate resources for qplib */
	rdev->qplib_res.rcfw = &rdev->rcfw;
	rc = bnxt_qplib_get_dev_attr(&rdev->rcfw, &rdev->dev_attr);
	if (rc)
		goto fail;

	rc = bnxt_qplib_alloc_res(&rdev->qplib_res, rdev->en_dev->pdev,
				  rdev->netdev, &rdev->dev_attr);
	if (rc)
		goto fail;

	rc = bnxt_qplib_alloc_dpi(&rdev->qplib_res,
				  &rdev->dpi_privileged,
				  rdev, BNXT_QPLIB_DPI_TYPE_KERNEL);
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
		rattr.lrid = rdev->en_dev->msix_entries[i + 1].ring_idx;
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

	ib_device_put(&rdev->ibdev);
	return rc;
}

static u32 bnxt_re_get_priority_mask(struct bnxt_re_dev *rdev)
{
	u32 prio_map = 0, tmp_map = 0;
	struct net_device *netdev;
	struct dcb_app app = {};

	netdev = rdev->netdev;

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

static int bnxt_re_setup_qos(struct bnxt_re_dev *rdev)
{
	u8 prio_map = 0;

	/* Get priority for roce */
	prio_map = bnxt_re_get_priority_mask(rdev);

	if (prio_map == rdev->cur_prio_map)
		return 0;
	rdev->cur_prio_map = prio_map;
	/* Actual priorities are not programmed as they are already
	 * done by L2 driver; just enable or disable priority vlan tagging
	 */
	if ((prio_map == 0 && rdev->qplib_res.prio) ||
	    (prio_map != 0 && !rdev->qplib_res.prio)) {
		rdev->qplib_res.prio = prio_map;
		bnxt_re_update_gid(rdev);
	}

	return 0;
}

static void bnxt_re_query_hwrm_intf_version(struct bnxt_re_dev *rdev)
{
	struct bnxt_en_dev *en_dev = rdev->en_dev;
	struct hwrm_ver_get_output resp = {};
	struct hwrm_ver_get_input req = {};
	struct bnxt_qplib_chip_ctx *cctx;
	struct bnxt_fw_msg fw_msg = {};
	int rc;

	bnxt_re_init_hwrm_hdr((void *)&req, HWRM_VER_GET);
	req.hwrm_intf_maj = HWRM_VERSION_MAJOR;
	req.hwrm_intf_min = HWRM_VERSION_MINOR;
	req.hwrm_intf_upd = HWRM_VERSION_UPDATE;
	bnxt_re_fill_fw_msg(&fw_msg, (void *)&req, sizeof(req), (void *)&resp,
			    sizeof(resp), DFLT_HWRM_CMD_TIMEOUT);
	rc = bnxt_send_msg(en_dev, &fw_msg);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to query HW version, rc = 0x%x",
			  rc);
		return;
	}

	cctx = rdev->chip_ctx;
	cctx->hwrm_intf_ver =
		(u64)le16_to_cpu(resp.hwrm_intf_major) << 48 |
		(u64)le16_to_cpu(resp.hwrm_intf_minor) << 32 |
		(u64)le16_to_cpu(resp.hwrm_intf_build) << 16 |
		le16_to_cpu(resp.hwrm_intf_patch);

	cctx->hwrm_cmd_max_timeout = le16_to_cpu(resp.max_req_timeout);

	if (!cctx->hwrm_cmd_max_timeout)
		cctx->hwrm_cmd_max_timeout = RCFW_FW_STALL_MAX_TIMEOUT;
}

static int bnxt_re_ib_init(struct bnxt_re_dev *rdev)
{
	int rc;
	u32 event;

	/* Register ib dev */
	rc = bnxt_re_register_ib(rdev);
	if (rc) {
		pr_err("Failed to register with IB: %#x\n", rc);
		return rc;
	}
	dev_info(rdev_to_dev(rdev), "Device registered with IB successfully");
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

	rdev->num_msix = 0;

	if (rdev->pacing.dbr_pacing)
		bnxt_re_deinitialize_dbr_pacing(rdev);

	bnxt_re_destroy_chip_ctx(rdev);
	if (test_and_clear_bit(BNXT_RE_FLAG_NETDEV_REGISTERED, &rdev->flags))
		bnxt_unregister_dev(rdev->en_dev);
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
	struct bnxt_re_ring_attr rattr = {};
	struct bnxt_qplib_creq_ctx *creq;
	u32 db_offt;
	int vid;
	u8 type;
	int rc;

	/* Registered a new RoCE device instance to netdev */
	rc = bnxt_re_register_netdev(rdev);
	if (rc) {
		ibdev_err(&rdev->ibdev,
			  "Failed to register with netedev: %#x\n", rc);
		return -EINVAL;
	}
	set_bit(BNXT_RE_FLAG_NETDEV_REGISTERED, &rdev->flags);

	rc = bnxt_re_setup_chip_ctx(rdev, wqe_mode);
	if (rc) {
		bnxt_unregister_dev(rdev->en_dev);
		clear_bit(BNXT_RE_FLAG_NETDEV_REGISTERED, &rdev->flags);
		ibdev_err(&rdev->ibdev, "Failed to get chip context\n");
		return -EINVAL;
	}

	/* Check whether VF or PF */
	bnxt_re_get_sriov_func_type(rdev);

	if (!rdev->en_dev->ulp_tbl->msix_requested) {
		ibdev_err(&rdev->ibdev,
			  "Failed to get MSI-X vectors: %#x\n", rc);
		rc = -EINVAL;
		goto fail;
	}
	ibdev_dbg(&rdev->ibdev, "Got %d MSI-X vectors\n",
		  rdev->en_dev->ulp_tbl->msix_requested);
	rdev->num_msix = rdev->en_dev->ulp_tbl->msix_requested;

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
	rattr.lrid = rdev->en_dev->msix_entries[BNXT_RE_AEQ_IDX].ring_idx;
	rc = bnxt_re_net_ring_alloc(rdev, &rattr, &creq->ring_id);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to allocate CREQ: %#x\n", rc);
		goto free_rcfw;
	}
	db_offt = bnxt_re_get_nqdb_offset(rdev, BNXT_RE_AEQ_IDX);
	vid = rdev->en_dev->msix_entries[BNXT_RE_AEQ_IDX].vector;
	rc = bnxt_qplib_enable_rcfw_channel(&rdev->rcfw,
					    vid, db_offt,
					    &bnxt_re_aeq_handler);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to enable RCFW channel: %#x\n",
			  rc);
		goto free_ring;
	}

	if (bnxt_qplib_dbr_pacing_en(rdev->chip_ctx)) {
		rc = bnxt_re_initialize_dbr_pacing(rdev);
		if (!rc) {
			rdev->pacing.dbr_pacing = true;
		} else {
			ibdev_err(&rdev->ibdev,
				  "DBR pacing disabled with error : %d\n", rc);
			rdev->pacing.dbr_pacing = false;
		}
	}
	rc = bnxt_qplib_get_dev_attr(&rdev->rcfw, &rdev->dev_attr);
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
		/*
		 * Use the total VF count since the actual VF count may not be
		 * available at this point.
		 */
		bnxt_re_vf_res_config(rdev);
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

static int bnxt_re_add_device(struct auxiliary_device *adev, u8 wqe_mode)
{
	struct bnxt_aux_priv *aux_priv =
		container_of(adev, struct bnxt_aux_priv, aux_dev);
	struct bnxt_en_dev *en_dev;
	struct bnxt_re_dev *rdev;
	int rc;

	/* en_dev should never be NULL as long as adev and aux_dev are valid. */
	en_dev = aux_priv->edev;

	rdev = bnxt_re_dev_add(aux_priv, en_dev);
	if (!rdev || !rdev_to_dev(rdev)) {
		rc = -ENOMEM;
		goto exit;
	}

	rc = bnxt_re_dev_init(rdev, wqe_mode);
	if (rc)
		goto re_dev_dealloc;

	rc = bnxt_re_ib_init(rdev);
	if (rc) {
		pr_err("Failed to register with IB: %s",
			aux_priv->aux_dev.name);
		goto re_dev_uninit;
	}
	auxiliary_set_drvdata(adev, rdev);

	return 0;

re_dev_uninit:
	bnxt_re_dev_uninit(rdev);
re_dev_dealloc:
	ib_dealloc_device(&rdev->ibdev);
exit:
	return rc;
}

static void bnxt_re_setup_cc(struct bnxt_re_dev *rdev, bool enable)
{
	struct bnxt_qplib_cc_param cc_param = {};

	/* Do not enable congestion control on VFs */
	if (rdev->is_virtfn)
		return;

	/* Currently enabling only for GenP5 adapters */
	if (!bnxt_qplib_is_chip_gen_p5(rdev->chip_ctx))
		return;

	if (enable) {
		cc_param.enable  = 1;
		cc_param.cc_mode = CMDQ_MODIFY_ROCE_CC_CC_MODE_PROBABILISTIC_CC_MODE;
	}

	cc_param.mask = (CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_CC_MODE |
			 CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_ENABLE_CC |
			 CMDQ_MODIFY_ROCE_CC_MODIFY_MASK_TOS_ECN);

	if (bnxt_qplib_modify_cc(&rdev->qplib_res, &cc_param))
		ibdev_err(&rdev->ibdev, "Failed to setup CC enable = %d\n", enable);
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
	struct bnxt_re_dev *rdev;

	real_dev = rdma_vlan_dev_real_dev(netdev);
	if (!real_dev)
		real_dev = netdev;

	if (real_dev != netdev)
		goto exit;

	rdev = bnxt_re_from_netdev(real_dev);
	if (!rdev)
		return NOTIFY_DONE;


	switch (event) {
	case NETDEV_UP:
	case NETDEV_DOWN:
	case NETDEV_CHANGE:
		bnxt_re_dispatch_event(&rdev->ibdev, NULL, 1,
					netif_carrier_ok(real_dev) ?
					IB_EVENT_PORT_ACTIVE :
					IB_EVENT_PORT_ERR);
		break;
	default:
		break;
	}
	ib_device_put(&rdev->ibdev);
exit:
	return NOTIFY_DONE;
}

#define BNXT_ADEV_NAME "bnxt_en"

static void bnxt_re_remove(struct auxiliary_device *adev)
{
	struct bnxt_re_dev *rdev = auxiliary_get_drvdata(adev);

	if (!rdev)
		return;

	mutex_lock(&bnxt_re_mutex);
	if (rdev->nb.notifier_call) {
		unregister_netdevice_notifier(&rdev->nb);
		rdev->nb.notifier_call = NULL;
	} else {
		/* If notifier is null, we should have already done a
		 * clean up before coming here.
		 */
		goto skip_remove;
	}
	bnxt_re_setup_cc(rdev, false);
	ib_unregister_device(&rdev->ibdev);
	bnxt_re_dev_uninit(rdev);
	ib_dealloc_device(&rdev->ibdev);
skip_remove:
	mutex_unlock(&bnxt_re_mutex);
}

static int bnxt_re_probe(struct auxiliary_device *adev,
			 const struct auxiliary_device_id *id)
{
	struct bnxt_re_dev *rdev;
	int rc;

	mutex_lock(&bnxt_re_mutex);
	rc = bnxt_re_add_device(adev, BNXT_QPLIB_WQE_MODE_STATIC);
	if (rc) {
		mutex_unlock(&bnxt_re_mutex);
		return rc;
	}

	rdev = auxiliary_get_drvdata(adev);

	rdev->nb.notifier_call = bnxt_re_netdev_event;
	rc = register_netdevice_notifier(&rdev->nb);
	if (rc) {
		rdev->nb.notifier_call = NULL;
		pr_err("%s: Cannot register to netdevice_notifier",
		       ROCE_DRV_MODULE_NAME);
		goto err;
	}

	bnxt_re_setup_cc(rdev, true);
	mutex_unlock(&bnxt_re_mutex);
	return 0;

err:
	mutex_unlock(&bnxt_re_mutex);
	bnxt_re_remove(adev);

	return rc;
}

static int bnxt_re_suspend(struct auxiliary_device *adev, pm_message_t state)
{
	struct bnxt_re_dev *rdev = auxiliary_get_drvdata(adev);

	if (!rdev)
		return 0;

	mutex_lock(&bnxt_re_mutex);
	/* L2 driver may invoke this callback during device error/crash or device
	 * reset. Current RoCE driver doesn't recover the device in case of
	 * error. Handle the error by dispatching fatal events to all qps
	 * ie. by calling bnxt_re_dev_stop and release the MSIx vectors as
	 * L2 driver want to modify the MSIx table.
	 */

	ibdev_info(&rdev->ibdev, "Handle device suspend call");
	/* Check the current device state from bnxt_en_dev and move the
	 * device to detached state if FW_FATAL_COND is set.
	 * This prevents more commands to HW during clean-up,
	 * in case the device is already in error.
	 */
	if (test_bit(BNXT_STATE_FW_FATAL_COND, &rdev->en_dev->en_state))
		set_bit(ERR_DEVICE_DETACHED, &rdev->rcfw.cmdq.flags);

	bnxt_re_dev_stop(rdev);
	bnxt_re_stop_irq(rdev);
	/* Move the device states to detached and  avoid sending any more
	 * commands to HW
	 */
	set_bit(BNXT_RE_FLAG_ERR_DEVICE_DETACHED, &rdev->flags);
	set_bit(ERR_DEVICE_DETACHED, &rdev->rcfw.cmdq.flags);
	wake_up_all(&rdev->rcfw.cmdq.waitq);
	mutex_unlock(&bnxt_re_mutex);

	return 0;
}

static int bnxt_re_resume(struct auxiliary_device *adev)
{
	struct bnxt_re_dev *rdev = auxiliary_get_drvdata(adev);

	if (!rdev)
		return 0;

	mutex_lock(&bnxt_re_mutex);
	/* L2 driver may invoke this callback during device recovery, resume.
	 * reset. Current RoCE driver doesn't recover the device in case of
	 * error. Handle the error by dispatching fatal events to all qps
	 * ie. by calling bnxt_re_dev_stop and release the MSIx vectors as
	 * L2 driver want to modify the MSIx table.
	 */

	ibdev_info(&rdev->ibdev, "Handle device resume call");
	mutex_unlock(&bnxt_re_mutex);

	return 0;
}

static const struct auxiliary_device_id bnxt_re_id_table[] = {
	{ .name = BNXT_ADEV_NAME ".rdma", },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, bnxt_re_id_table);

static struct auxiliary_driver bnxt_re_driver = {
	.name = "rdma",
	.probe = bnxt_re_probe,
	.remove = bnxt_re_remove,
	.shutdown = bnxt_re_shutdown,
	.suspend = bnxt_re_suspend,
	.resume = bnxt_re_resume,
	.id_table = bnxt_re_id_table,
};

static int __init bnxt_re_mod_init(void)
{
	int rc;

	pr_info("%s: %s", ROCE_DRV_MODULE_NAME, version);
	rc = auxiliary_driver_register(&bnxt_re_driver);
	if (rc) {
		pr_err("%s: Failed to register auxiliary driver\n",
			ROCE_DRV_MODULE_NAME);
		return rc;
	}
	return 0;
}

static void __exit bnxt_re_mod_exit(void)
{
	auxiliary_driver_unregister(&bnxt_re_driver);
}

module_init(bnxt_re_mod_init);
module_exit(bnxt_re_mod_exit);
