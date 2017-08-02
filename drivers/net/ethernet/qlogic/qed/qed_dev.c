/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
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
 *        disclaimer in the documentation and /or other materials
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

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/etherdevice.h>
#include <linux/qed/qed_chain.h>
#include <linux/qed/qed_if.h>
#include "qed.h"
#include "qed_cxt.h"
#include "qed_dcbx.h"
#include "qed_dev_api.h"
#include "qed_fcoe.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_init_ops.h"
#include "qed_int.h"
#include "qed_iscsi.h"
#include "qed_ll2.h"
#include "qed_mcp.h"
#include "qed_ooo.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"
#include "qed_sriov.h"
#include "qed_vf.h"
#include "qed_rdma.h"

static DEFINE_SPINLOCK(qm_lock);

#define QED_MIN_DPIS            (4)
#define QED_MIN_PWM_REGION      (QED_WID_SIZE * QED_MIN_DPIS)

static u32 qed_hw_bar_size(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt, enum BAR_ID bar_id)
{
	u32 bar_reg = (bar_id == BAR_ID_0 ?
		       PGLUE_B_REG_PF_BAR0_SIZE : PGLUE_B_REG_PF_BAR1_SIZE);
	u32 val;

	if (IS_VF(p_hwfn->cdev))
		return qed_vf_hw_bar_size(p_hwfn, bar_id);

	val = qed_rd(p_hwfn, p_ptt, bar_reg);
	if (val)
		return 1 << (val + 15);

	/* Old MFW initialized above registered only conditionally */
	if (p_hwfn->cdev->num_hwfns > 1) {
		DP_INFO(p_hwfn,
			"BAR size not configured. Assuming BAR size of 256kB for GRC and 512kB for DB\n");
			return BAR_ID_0 ? 256 * 1024 : 512 * 1024;
	} else {
		DP_INFO(p_hwfn,
			"BAR size not configured. Assuming BAR size of 512kB for GRC and 512kB for DB\n");
			return 512 * 1024;
	}
}

void qed_init_dp(struct qed_dev *cdev, u32 dp_module, u8 dp_level)
{
	u32 i;

	cdev->dp_level = dp_level;
	cdev->dp_module = dp_module;
	for (i = 0; i < MAX_HWFNS_PER_DEVICE; i++) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		p_hwfn->dp_level = dp_level;
		p_hwfn->dp_module = dp_module;
	}
}

void qed_init_struct(struct qed_dev *cdev)
{
	u8 i;

	for (i = 0; i < MAX_HWFNS_PER_DEVICE; i++) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		p_hwfn->cdev = cdev;
		p_hwfn->my_id = i;
		p_hwfn->b_active = false;

		mutex_init(&p_hwfn->dmae_info.mutex);
	}

	/* hwfn 0 is always active */
	cdev->hwfns[0].b_active = true;

	/* set the default cache alignment to 128 */
	cdev->cache_shift = 7;
}

static void qed_qm_info_free(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;

	kfree(qm_info->qm_pq_params);
	qm_info->qm_pq_params = NULL;
	kfree(qm_info->qm_vport_params);
	qm_info->qm_vport_params = NULL;
	kfree(qm_info->qm_port_params);
	qm_info->qm_port_params = NULL;
	kfree(qm_info->wfq_data);
	qm_info->wfq_data = NULL;
}

void qed_resc_free(struct qed_dev *cdev)
{
	int i;

	if (IS_VF(cdev)) {
		for_each_hwfn(cdev, i)
			qed_l2_free(&cdev->hwfns[i]);
		return;
	}

	kfree(cdev->fw_data);
	cdev->fw_data = NULL;

	kfree(cdev->reset_stats);
	cdev->reset_stats = NULL;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		qed_cxt_mngr_free(p_hwfn);
		qed_qm_info_free(p_hwfn);
		qed_spq_free(p_hwfn);
		qed_eq_free(p_hwfn);
		qed_consq_free(p_hwfn);
		qed_int_free(p_hwfn);
#ifdef CONFIG_QED_LL2
		qed_ll2_free(p_hwfn);
#endif
		if (p_hwfn->hw_info.personality == QED_PCI_FCOE)
			qed_fcoe_free(p_hwfn);

		if (p_hwfn->hw_info.personality == QED_PCI_ISCSI) {
			qed_iscsi_free(p_hwfn);
			qed_ooo_free(p_hwfn);
		}
		qed_iov_free(p_hwfn);
		qed_l2_free(p_hwfn);
		qed_dmae_info_free(p_hwfn);
		qed_dcbx_info_free(p_hwfn);
	}
}

/******************** QM initialization *******************/
#define ACTIVE_TCS_BMAP 0x9f
#define ACTIVE_TCS_BMAP_4PORT_K2 0xf

/* determines the physical queue flags for a given PF. */
static u32 qed_get_pq_flags(struct qed_hwfn *p_hwfn)
{
	u32 flags;

	/* common flags */
	flags = PQ_FLAGS_LB;

	/* feature flags */
	if (IS_QED_SRIOV(p_hwfn->cdev))
		flags |= PQ_FLAGS_VFS;

	/* protocol flags */
	switch (p_hwfn->hw_info.personality) {
	case QED_PCI_ETH:
		flags |= PQ_FLAGS_MCOS;
		break;
	case QED_PCI_FCOE:
		flags |= PQ_FLAGS_OFLD;
		break;
	case QED_PCI_ISCSI:
		flags |= PQ_FLAGS_ACK | PQ_FLAGS_OOO | PQ_FLAGS_OFLD;
		break;
	case QED_PCI_ETH_ROCE:
		flags |= PQ_FLAGS_MCOS | PQ_FLAGS_OFLD | PQ_FLAGS_LLT;
		break;
	case QED_PCI_ETH_IWARP:
		flags |= PQ_FLAGS_MCOS | PQ_FLAGS_ACK | PQ_FLAGS_OOO |
		    PQ_FLAGS_OFLD;
		break;
	default:
		DP_ERR(p_hwfn,
		       "unknown personality %d\n", p_hwfn->hw_info.personality);
		return 0;
	}

	return flags;
}

/* Getters for resource amounts necessary for qm initialization */
u8 qed_init_qm_get_num_tcs(struct qed_hwfn *p_hwfn)
{
	return p_hwfn->hw_info.num_hw_tc;
}

u16 qed_init_qm_get_num_vfs(struct qed_hwfn *p_hwfn)
{
	return IS_QED_SRIOV(p_hwfn->cdev) ?
	       p_hwfn->cdev->p_iov_info->total_vfs : 0;
}

#define NUM_DEFAULT_RLS 1

u16 qed_init_qm_get_num_pf_rls(struct qed_hwfn *p_hwfn)
{
	u16 num_pf_rls, num_vfs = qed_init_qm_get_num_vfs(p_hwfn);

	/* num RLs can't exceed resource amount of rls or vports */
	num_pf_rls = (u16) min_t(u32, RESC_NUM(p_hwfn, QED_RL),
				 RESC_NUM(p_hwfn, QED_VPORT));

	/* Make sure after we reserve there's something left */
	if (num_pf_rls < num_vfs + NUM_DEFAULT_RLS)
		return 0;

	/* subtract rls necessary for VFs and one default one for the PF */
	num_pf_rls -= num_vfs + NUM_DEFAULT_RLS;

	return num_pf_rls;
}

u16 qed_init_qm_get_num_vports(struct qed_hwfn *p_hwfn)
{
	u32 pq_flags = qed_get_pq_flags(p_hwfn);

	/* all pqs share the same vport, except for vfs and pf_rl pqs */
	return (!!(PQ_FLAGS_RLS & pq_flags)) *
	       qed_init_qm_get_num_pf_rls(p_hwfn) +
	       (!!(PQ_FLAGS_VFS & pq_flags)) *
	       qed_init_qm_get_num_vfs(p_hwfn) + 1;
}

/* calc amount of PQs according to the requested flags */
u16 qed_init_qm_get_num_pqs(struct qed_hwfn *p_hwfn)
{
	u32 pq_flags = qed_get_pq_flags(p_hwfn);

	return (!!(PQ_FLAGS_RLS & pq_flags)) *
	       qed_init_qm_get_num_pf_rls(p_hwfn) +
	       (!!(PQ_FLAGS_MCOS & pq_flags)) *
	       qed_init_qm_get_num_tcs(p_hwfn) +
	       (!!(PQ_FLAGS_LB & pq_flags)) + (!!(PQ_FLAGS_OOO & pq_flags)) +
	       (!!(PQ_FLAGS_ACK & pq_flags)) + (!!(PQ_FLAGS_OFLD & pq_flags)) +
	       (!!(PQ_FLAGS_LLT & pq_flags)) +
	       (!!(PQ_FLAGS_VFS & pq_flags)) * qed_init_qm_get_num_vfs(p_hwfn);
}

/* initialize the top level QM params */
static void qed_init_qm_params(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;
	bool four_port;

	/* pq and vport bases for this PF */
	qm_info->start_pq = (u16) RESC_START(p_hwfn, QED_PQ);
	qm_info->start_vport = (u8) RESC_START(p_hwfn, QED_VPORT);

	/* rate limiting and weighted fair queueing are always enabled */
	qm_info->vport_rl_en = 1;
	qm_info->vport_wfq_en = 1;

	/* TC config is different for AH 4 port */
	four_port = p_hwfn->cdev->num_ports_in_engine == MAX_NUM_PORTS_K2;

	/* in AH 4 port we have fewer TCs per port */
	qm_info->max_phys_tcs_per_port = four_port ? NUM_PHYS_TCS_4PORT_K2 :
						     NUM_OF_PHYS_TCS;

	/* unless MFW indicated otherwise, ooo_tc == 3 for
	 * AH 4-port and 4 otherwise.
	 */
	if (!qm_info->ooo_tc)
		qm_info->ooo_tc = four_port ? DCBX_TCP_OOO_K2_4PORT_TC :
					      DCBX_TCP_OOO_TC;
}

/* initialize qm vport params */
static void qed_init_qm_vport_params(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;
	u8 i;

	/* all vports participate in weighted fair queueing */
	for (i = 0; i < qed_init_qm_get_num_vports(p_hwfn); i++)
		qm_info->qm_vport_params[i].vport_wfq = 1;
}

/* initialize qm port params */
static void qed_init_qm_port_params(struct qed_hwfn *p_hwfn)
{
	/* Initialize qm port parameters */
	u8 i, active_phys_tcs, num_ports = p_hwfn->cdev->num_ports_in_engine;

	/* indicate how ooo and high pri traffic is dealt with */
	active_phys_tcs = num_ports == MAX_NUM_PORTS_K2 ?
			  ACTIVE_TCS_BMAP_4PORT_K2 :
			  ACTIVE_TCS_BMAP;

	for (i = 0; i < num_ports; i++) {
		struct init_qm_port_params *p_qm_port =
		    &p_hwfn->qm_info.qm_port_params[i];

		p_qm_port->active = 1;
		p_qm_port->active_phys_tcs = active_phys_tcs;
		p_qm_port->num_pbf_cmd_lines = PBF_MAX_CMD_LINES / num_ports;
		p_qm_port->num_btb_blocks = BTB_MAX_BLOCKS / num_ports;
	}
}

/* Reset the params which must be reset for qm init. QM init may be called as
 * a result of flows other than driver load (e.g. dcbx renegotiation). Other
 * params may be affected by the init but would simply recalculate to the same
 * values. The allocations made for QM init, ports, vports, pqs and vfqs are not
 * affected as these amounts stay the same.
 */
static void qed_init_qm_reset_params(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;

	qm_info->num_pqs = 0;
	qm_info->num_vports = 0;
	qm_info->num_pf_rls = 0;
	qm_info->num_vf_pqs = 0;
	qm_info->first_vf_pq = 0;
	qm_info->first_mcos_pq = 0;
	qm_info->first_rl_pq = 0;
}

static void qed_init_qm_advance_vport(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;

	qm_info->num_vports++;

	if (qm_info->num_vports > qed_init_qm_get_num_vports(p_hwfn))
		DP_ERR(p_hwfn,
		       "vport overflow! qm_info->num_vports %d, qm_init_get_num_vports() %d\n",
		       qm_info->num_vports, qed_init_qm_get_num_vports(p_hwfn));
}

/* initialize a single pq and manage qm_info resources accounting.
 * The pq_init_flags param determines whether the PQ is rate limited
 * (for VF or PF) and whether a new vport is allocated to the pq or not
 * (i.e. vport will be shared).
 */

/* flags for pq init */
#define PQ_INIT_SHARE_VPORT     (1 << 0)
#define PQ_INIT_PF_RL           (1 << 1)
#define PQ_INIT_VF_RL           (1 << 2)

/* defines for pq init */
#define PQ_INIT_DEFAULT_WRR_GROUP       1
#define PQ_INIT_DEFAULT_TC              0
#define PQ_INIT_OFLD_TC                 (p_hwfn->hw_info.offload_tc)

static void qed_init_qm_pq(struct qed_hwfn *p_hwfn,
			   struct qed_qm_info *qm_info,
			   u8 tc, u32 pq_init_flags)
{
	u16 pq_idx = qm_info->num_pqs, max_pq = qed_init_qm_get_num_pqs(p_hwfn);

	if (pq_idx > max_pq)
		DP_ERR(p_hwfn,
		       "pq overflow! pq %d, max pq %d\n", pq_idx, max_pq);

	/* init pq params */
	qm_info->qm_pq_params[pq_idx].vport_id = qm_info->start_vport +
	    qm_info->num_vports;
	qm_info->qm_pq_params[pq_idx].tc_id = tc;
	qm_info->qm_pq_params[pq_idx].wrr_group = PQ_INIT_DEFAULT_WRR_GROUP;
	qm_info->qm_pq_params[pq_idx].rl_valid =
	    (pq_init_flags & PQ_INIT_PF_RL || pq_init_flags & PQ_INIT_VF_RL);

	/* qm params accounting */
	qm_info->num_pqs++;
	if (!(pq_init_flags & PQ_INIT_SHARE_VPORT))
		qm_info->num_vports++;

	if (pq_init_flags & PQ_INIT_PF_RL)
		qm_info->num_pf_rls++;

	if (qm_info->num_vports > qed_init_qm_get_num_vports(p_hwfn))
		DP_ERR(p_hwfn,
		       "vport overflow! qm_info->num_vports %d, qm_init_get_num_vports() %d\n",
		       qm_info->num_vports, qed_init_qm_get_num_vports(p_hwfn));

	if (qm_info->num_pf_rls > qed_init_qm_get_num_pf_rls(p_hwfn))
		DP_ERR(p_hwfn,
		       "rl overflow! qm_info->num_pf_rls %d, qm_init_get_num_pf_rls() %d\n",
		       qm_info->num_pf_rls, qed_init_qm_get_num_pf_rls(p_hwfn));
}

/* get pq index according to PQ_FLAGS */
static u16 *qed_init_qm_get_idx_from_flags(struct qed_hwfn *p_hwfn,
					   u32 pq_flags)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;

	/* Can't have multiple flags set here */
	if (bitmap_weight((unsigned long *)&pq_flags, sizeof(pq_flags)) > 1)
		goto err;

	switch (pq_flags) {
	case PQ_FLAGS_RLS:
		return &qm_info->first_rl_pq;
	case PQ_FLAGS_MCOS:
		return &qm_info->first_mcos_pq;
	case PQ_FLAGS_LB:
		return &qm_info->pure_lb_pq;
	case PQ_FLAGS_OOO:
		return &qm_info->ooo_pq;
	case PQ_FLAGS_ACK:
		return &qm_info->pure_ack_pq;
	case PQ_FLAGS_OFLD:
		return &qm_info->offload_pq;
	case PQ_FLAGS_LLT:
		return &qm_info->low_latency_pq;
	case PQ_FLAGS_VFS:
		return &qm_info->first_vf_pq;
	default:
		goto err;
	}

err:
	DP_ERR(p_hwfn, "BAD pq flags %d\n", pq_flags);
	return NULL;
}

/* save pq index in qm info */
static void qed_init_qm_set_idx(struct qed_hwfn *p_hwfn,
				u32 pq_flags, u16 pq_val)
{
	u16 *base_pq_idx = qed_init_qm_get_idx_from_flags(p_hwfn, pq_flags);

	*base_pq_idx = p_hwfn->qm_info.start_pq + pq_val;
}

/* get tx pq index, with the PQ TX base already set (ready for context init) */
u16 qed_get_cm_pq_idx(struct qed_hwfn *p_hwfn, u32 pq_flags)
{
	u16 *base_pq_idx = qed_init_qm_get_idx_from_flags(p_hwfn, pq_flags);

	return *base_pq_idx + CM_TX_PQ_BASE;
}

u16 qed_get_cm_pq_idx_mcos(struct qed_hwfn *p_hwfn, u8 tc)
{
	u8 max_tc = qed_init_qm_get_num_tcs(p_hwfn);

	if (tc > max_tc)
		DP_ERR(p_hwfn, "tc %d must be smaller than %d\n", tc, max_tc);

	return qed_get_cm_pq_idx(p_hwfn, PQ_FLAGS_MCOS) + tc;
}

u16 qed_get_cm_pq_idx_vf(struct qed_hwfn *p_hwfn, u16 vf)
{
	u16 max_vf = qed_init_qm_get_num_vfs(p_hwfn);

	if (vf > max_vf)
		DP_ERR(p_hwfn, "vf %d must be smaller than %d\n", vf, max_vf);

	return qed_get_cm_pq_idx(p_hwfn, PQ_FLAGS_VFS) + vf;
}

u16 qed_get_cm_pq_idx_rl(struct qed_hwfn *p_hwfn, u8 rl)
{
	u16 max_rl = qed_init_qm_get_num_pf_rls(p_hwfn);

	if (rl > max_rl)
		DP_ERR(p_hwfn, "rl %d must be smaller than %d\n", rl, max_rl);

	return qed_get_cm_pq_idx(p_hwfn, PQ_FLAGS_RLS) + rl;
}

/* Functions for creating specific types of pqs */
static void qed_init_qm_lb_pq(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;

	if (!(qed_get_pq_flags(p_hwfn) & PQ_FLAGS_LB))
		return;

	qed_init_qm_set_idx(p_hwfn, PQ_FLAGS_LB, qm_info->num_pqs);
	qed_init_qm_pq(p_hwfn, qm_info, PURE_LB_TC, PQ_INIT_SHARE_VPORT);
}

static void qed_init_qm_ooo_pq(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;

	if (!(qed_get_pq_flags(p_hwfn) & PQ_FLAGS_OOO))
		return;

	qed_init_qm_set_idx(p_hwfn, PQ_FLAGS_OOO, qm_info->num_pqs);
	qed_init_qm_pq(p_hwfn, qm_info, qm_info->ooo_tc, PQ_INIT_SHARE_VPORT);
}

static void qed_init_qm_pure_ack_pq(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;

	if (!(qed_get_pq_flags(p_hwfn) & PQ_FLAGS_ACK))
		return;

	qed_init_qm_set_idx(p_hwfn, PQ_FLAGS_ACK, qm_info->num_pqs);
	qed_init_qm_pq(p_hwfn, qm_info, PQ_INIT_OFLD_TC, PQ_INIT_SHARE_VPORT);
}

static void qed_init_qm_offload_pq(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;

	if (!(qed_get_pq_flags(p_hwfn) & PQ_FLAGS_OFLD))
		return;

	qed_init_qm_set_idx(p_hwfn, PQ_FLAGS_OFLD, qm_info->num_pqs);
	qed_init_qm_pq(p_hwfn, qm_info, PQ_INIT_OFLD_TC, PQ_INIT_SHARE_VPORT);
}

static void qed_init_qm_low_latency_pq(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;

	if (!(qed_get_pq_flags(p_hwfn) & PQ_FLAGS_LLT))
		return;

	qed_init_qm_set_idx(p_hwfn, PQ_FLAGS_LLT, qm_info->num_pqs);
	qed_init_qm_pq(p_hwfn, qm_info, PQ_INIT_OFLD_TC, PQ_INIT_SHARE_VPORT);
}

static void qed_init_qm_mcos_pqs(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;
	u8 tc_idx;

	if (!(qed_get_pq_flags(p_hwfn) & PQ_FLAGS_MCOS))
		return;

	qed_init_qm_set_idx(p_hwfn, PQ_FLAGS_MCOS, qm_info->num_pqs);
	for (tc_idx = 0; tc_idx < qed_init_qm_get_num_tcs(p_hwfn); tc_idx++)
		qed_init_qm_pq(p_hwfn, qm_info, tc_idx, PQ_INIT_SHARE_VPORT);
}

static void qed_init_qm_vf_pqs(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;
	u16 vf_idx, num_vfs = qed_init_qm_get_num_vfs(p_hwfn);

	if (!(qed_get_pq_flags(p_hwfn) & PQ_FLAGS_VFS))
		return;

	qed_init_qm_set_idx(p_hwfn, PQ_FLAGS_VFS, qm_info->num_pqs);
	qm_info->num_vf_pqs = num_vfs;
	for (vf_idx = 0; vf_idx < num_vfs; vf_idx++)
		qed_init_qm_pq(p_hwfn,
			       qm_info, PQ_INIT_DEFAULT_TC, PQ_INIT_VF_RL);
}

static void qed_init_qm_rl_pqs(struct qed_hwfn *p_hwfn)
{
	u16 pf_rls_idx, num_pf_rls = qed_init_qm_get_num_pf_rls(p_hwfn);
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;

	if (!(qed_get_pq_flags(p_hwfn) & PQ_FLAGS_RLS))
		return;

	qed_init_qm_set_idx(p_hwfn, PQ_FLAGS_RLS, qm_info->num_pqs);
	for (pf_rls_idx = 0; pf_rls_idx < num_pf_rls; pf_rls_idx++)
		qed_init_qm_pq(p_hwfn, qm_info, PQ_INIT_OFLD_TC, PQ_INIT_PF_RL);
}

static void qed_init_qm_pq_params(struct qed_hwfn *p_hwfn)
{
	/* rate limited pqs, must come first (FW assumption) */
	qed_init_qm_rl_pqs(p_hwfn);

	/* pqs for multi cos */
	qed_init_qm_mcos_pqs(p_hwfn);

	/* pure loopback pq */
	qed_init_qm_lb_pq(p_hwfn);

	/* out of order pq */
	qed_init_qm_ooo_pq(p_hwfn);

	/* pure ack pq */
	qed_init_qm_pure_ack_pq(p_hwfn);

	/* pq for offloaded protocol */
	qed_init_qm_offload_pq(p_hwfn);

	/* low latency pq */
	qed_init_qm_low_latency_pq(p_hwfn);

	/* done sharing vports */
	qed_init_qm_advance_vport(p_hwfn);

	/* pqs for vfs */
	qed_init_qm_vf_pqs(p_hwfn);
}

/* compare values of getters against resources amounts */
static int qed_init_qm_sanity(struct qed_hwfn *p_hwfn)
{
	if (qed_init_qm_get_num_vports(p_hwfn) > RESC_NUM(p_hwfn, QED_VPORT)) {
		DP_ERR(p_hwfn, "requested amount of vports exceeds resource\n");
		return -EINVAL;
	}

	if (qed_init_qm_get_num_pqs(p_hwfn) > RESC_NUM(p_hwfn, QED_PQ)) {
		DP_ERR(p_hwfn, "requested amount of pqs exceeds resource\n");
		return -EINVAL;
	}

	return 0;
}

static void qed_dp_init_qm_params(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;
	struct init_qm_vport_params *vport;
	struct init_qm_port_params *port;
	struct init_qm_pq_params *pq;
	int i, tc;

	/* top level params */
	DP_VERBOSE(p_hwfn,
		   NETIF_MSG_HW,
		   "qm init top level params: start_pq %d, start_vport %d, pure_lb_pq %d, offload_pq %d, pure_ack_pq %d\n",
		   qm_info->start_pq,
		   qm_info->start_vport,
		   qm_info->pure_lb_pq,
		   qm_info->offload_pq, qm_info->pure_ack_pq);
	DP_VERBOSE(p_hwfn,
		   NETIF_MSG_HW,
		   "ooo_pq %d, first_vf_pq %d, num_pqs %d, num_vf_pqs %d, num_vports %d, max_phys_tcs_per_port %d\n",
		   qm_info->ooo_pq,
		   qm_info->first_vf_pq,
		   qm_info->num_pqs,
		   qm_info->num_vf_pqs,
		   qm_info->num_vports, qm_info->max_phys_tcs_per_port);
	DP_VERBOSE(p_hwfn,
		   NETIF_MSG_HW,
		   "pf_rl_en %d, pf_wfq_en %d, vport_rl_en %d, vport_wfq_en %d, pf_wfq %d, pf_rl %d, num_pf_rls %d, pq_flags %x\n",
		   qm_info->pf_rl_en,
		   qm_info->pf_wfq_en,
		   qm_info->vport_rl_en,
		   qm_info->vport_wfq_en,
		   qm_info->pf_wfq,
		   qm_info->pf_rl,
		   qm_info->num_pf_rls, qed_get_pq_flags(p_hwfn));

	/* port table */
	for (i = 0; i < p_hwfn->cdev->num_ports_in_engine; i++) {
		port = &(qm_info->qm_port_params[i]);
		DP_VERBOSE(p_hwfn,
			   NETIF_MSG_HW,
			   "port idx %d, active %d, active_phys_tcs %d, num_pbf_cmd_lines %d, num_btb_blocks %d, reserved %d\n",
			   i,
			   port->active,
			   port->active_phys_tcs,
			   port->num_pbf_cmd_lines,
			   port->num_btb_blocks, port->reserved);
	}

	/* vport table */
	for (i = 0; i < qm_info->num_vports; i++) {
		vport = &(qm_info->qm_vport_params[i]);
		DP_VERBOSE(p_hwfn,
			   NETIF_MSG_HW,
			   "vport idx %d, vport_rl %d, wfq %d, first_tx_pq_id [ ",
			   qm_info->start_vport + i,
			   vport->vport_rl, vport->vport_wfq);
		for (tc = 0; tc < NUM_OF_TCS; tc++)
			DP_VERBOSE(p_hwfn,
				   NETIF_MSG_HW,
				   "%d ", vport->first_tx_pq_id[tc]);
		DP_VERBOSE(p_hwfn, NETIF_MSG_HW, "]\n");
	}

	/* pq table */
	for (i = 0; i < qm_info->num_pqs; i++) {
		pq = &(qm_info->qm_pq_params[i]);
		DP_VERBOSE(p_hwfn,
			   NETIF_MSG_HW,
			   "pq idx %d, vport_id %d, tc %d, wrr_grp %d, rl_valid %d\n",
			   qm_info->start_pq + i,
			   pq->vport_id,
			   pq->tc_id, pq->wrr_group, pq->rl_valid);
	}
}

static void qed_init_qm_info(struct qed_hwfn *p_hwfn)
{
	/* reset params required for init run */
	qed_init_qm_reset_params(p_hwfn);

	/* init QM top level params */
	qed_init_qm_params(p_hwfn);

	/* init QM port params */
	qed_init_qm_port_params(p_hwfn);

	/* init QM vport params */
	qed_init_qm_vport_params(p_hwfn);

	/* init QM physical queue params */
	qed_init_qm_pq_params(p_hwfn);

	/* display all that init */
	qed_dp_init_qm_params(p_hwfn);
}

/* This function reconfigures the QM pf on the fly.
 * For this purpose we:
 * 1. reconfigure the QM database
 * 2. set new values to runtime arrat
 * 3. send an sdm_qm_cmd through the rbc interface to stop the QM
 * 4. activate init tool in QM_PF stage
 * 5. send an sdm_qm_cmd through rbc interface to release the QM
 */
int qed_qm_reconf(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;
	bool b_rc;
	int rc;

	/* initialize qed's qm data structure */
	qed_init_qm_info(p_hwfn);

	/* stop PF's qm queues */
	spin_lock_bh(&qm_lock);
	b_rc = qed_send_qm_stop_cmd(p_hwfn, p_ptt, false, true,
				    qm_info->start_pq, qm_info->num_pqs);
	spin_unlock_bh(&qm_lock);
	if (!b_rc)
		return -EINVAL;

	/* clear the QM_PF runtime phase leftovers from previous init */
	qed_init_clear_rt_data(p_hwfn);

	/* prepare QM portion of runtime array */
	qed_qm_init_pf(p_hwfn, p_ptt);

	/* activate init tool on runtime array */
	rc = qed_init_run(p_hwfn, p_ptt, PHASE_QM_PF, p_hwfn->rel_pf_id,
			  p_hwfn->hw_info.hw_mode);
	if (rc)
		return rc;

	/* start PF's qm queues */
	spin_lock_bh(&qm_lock);
	b_rc = qed_send_qm_stop_cmd(p_hwfn, p_ptt, true, true,
				    qm_info->start_pq, qm_info->num_pqs);
	spin_unlock_bh(&qm_lock);
	if (!b_rc)
		return -EINVAL;

	return 0;
}

static int qed_alloc_qm_data(struct qed_hwfn *p_hwfn)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;
	int rc;

	rc = qed_init_qm_sanity(p_hwfn);
	if (rc)
		goto alloc_err;

	qm_info->qm_pq_params = kzalloc(sizeof(*qm_info->qm_pq_params) *
					qed_init_qm_get_num_pqs(p_hwfn),
					GFP_KERNEL);
	if (!qm_info->qm_pq_params)
		goto alloc_err;

	qm_info->qm_vport_params = kzalloc(sizeof(*qm_info->qm_vport_params) *
					   qed_init_qm_get_num_vports(p_hwfn),
					   GFP_KERNEL);
	if (!qm_info->qm_vport_params)
		goto alloc_err;

	qm_info->qm_port_params = kzalloc(sizeof(*qm_info->qm_port_params) *
					  p_hwfn->cdev->num_ports_in_engine,
					  GFP_KERNEL);
	if (!qm_info->qm_port_params)
		goto alloc_err;

	qm_info->wfq_data = kzalloc(sizeof(*qm_info->wfq_data) *
				    qed_init_qm_get_num_vports(p_hwfn),
				    GFP_KERNEL);
	if (!qm_info->wfq_data)
		goto alloc_err;

	return 0;

alloc_err:
	DP_NOTICE(p_hwfn, "Failed to allocate memory for QM params\n");
	qed_qm_info_free(p_hwfn);
	return -ENOMEM;
}

int qed_resc_alloc(struct qed_dev *cdev)
{
	u32 rdma_tasks, excess_tasks;
	u32 line_count;
	int i, rc = 0;

	if (IS_VF(cdev)) {
		for_each_hwfn(cdev, i) {
			rc = qed_l2_alloc(&cdev->hwfns[i]);
			if (rc)
				return rc;
		}
		return rc;
	}

	cdev->fw_data = kzalloc(sizeof(*cdev->fw_data), GFP_KERNEL);
	if (!cdev->fw_data)
		return -ENOMEM;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
		u32 n_eqes, num_cons;

		/* First allocate the context manager structure */
		rc = qed_cxt_mngr_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		/* Set the HW cid/tid numbers (in the contest manager)
		 * Must be done prior to any further computations.
		 */
		rc = qed_cxt_set_pf_params(p_hwfn, RDMA_MAX_TIDS);
		if (rc)
			goto alloc_err;

		rc = qed_alloc_qm_data(p_hwfn);
		if (rc)
			goto alloc_err;

		/* init qm info */
		qed_init_qm_info(p_hwfn);

		/* Compute the ILT client partition */
		rc = qed_cxt_cfg_ilt_compute(p_hwfn, &line_count);
		if (rc) {
			DP_NOTICE(p_hwfn,
				  "too many ILT lines; re-computing with less lines\n");
			/* In case there are not enough ILT lines we reduce the
			 * number of RDMA tasks and re-compute.
			 */
			excess_tasks =
			    qed_cxt_cfg_ilt_compute_excess(p_hwfn, line_count);
			if (!excess_tasks)
				goto alloc_err;

			rdma_tasks = RDMA_MAX_TIDS - excess_tasks;
			rc = qed_cxt_set_pf_params(p_hwfn, rdma_tasks);
			if (rc)
				goto alloc_err;

			rc = qed_cxt_cfg_ilt_compute(p_hwfn, &line_count);
			if (rc) {
				DP_ERR(p_hwfn,
				       "failed ILT compute. Requested too many lines: %u\n",
				       line_count);

				goto alloc_err;
			}
		}

		/* CID map / ILT shadow table / T2
		 * The talbes sizes are determined by the computations above
		 */
		rc = qed_cxt_tables_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		/* SPQ, must follow ILT because initializes SPQ context */
		rc = qed_spq_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		/* SP status block allocation */
		p_hwfn->p_dpc_ptt = qed_get_reserved_ptt(p_hwfn,
							 RESERVED_PTT_DPC);

		rc = qed_int_alloc(p_hwfn, p_hwfn->p_main_ptt);
		if (rc)
			goto alloc_err;

		rc = qed_iov_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		/* EQ */
		n_eqes = qed_chain_get_capacity(&p_hwfn->p_spq->chain);
		if (QED_IS_RDMA_PERSONALITY(p_hwfn)) {
			enum protocol_type rdma_proto;

			if (QED_IS_ROCE_PERSONALITY(p_hwfn))
				rdma_proto = PROTOCOLID_ROCE;
			else
				rdma_proto = PROTOCOLID_IWARP;

			num_cons = qed_cxt_get_proto_cid_count(p_hwfn,
							       rdma_proto,
							       NULL) * 2;
			n_eqes += num_cons + 2 * MAX_NUM_VFS_BB;
		} else if (p_hwfn->hw_info.personality == QED_PCI_ISCSI) {
			num_cons =
			    qed_cxt_get_proto_cid_count(p_hwfn,
							PROTOCOLID_ISCSI,
							NULL);
			n_eqes += 2 * num_cons;
		}

		if (n_eqes > 0xFFFF) {
			DP_ERR(p_hwfn,
			       "Cannot allocate 0x%x EQ elements. The maximum of a u16 chain is 0x%x\n",
			       n_eqes, 0xFFFF);
			goto alloc_no_mem;
		}

		rc = qed_eq_alloc(p_hwfn, (u16) n_eqes);
		if (rc)
			goto alloc_err;

		rc = qed_consq_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		rc = qed_l2_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

#ifdef CONFIG_QED_LL2
		if (p_hwfn->using_ll2) {
			rc = qed_ll2_alloc(p_hwfn);
			if (rc)
				goto alloc_err;
		}
#endif

		if (p_hwfn->hw_info.personality == QED_PCI_FCOE) {
			rc = qed_fcoe_alloc(p_hwfn);
			if (rc)
				goto alloc_err;
		}

		if (p_hwfn->hw_info.personality == QED_PCI_ISCSI) {
			rc = qed_iscsi_alloc(p_hwfn);
			if (rc)
				goto alloc_err;
			rc = qed_ooo_alloc(p_hwfn);
			if (rc)
				goto alloc_err;
		}

		/* DMA info initialization */
		rc = qed_dmae_info_alloc(p_hwfn);
		if (rc)
			goto alloc_err;

		/* DCBX initialization */
		rc = qed_dcbx_info_alloc(p_hwfn);
		if (rc)
			goto alloc_err;
	}

	cdev->reset_stats = kzalloc(sizeof(*cdev->reset_stats), GFP_KERNEL);
	if (!cdev->reset_stats)
		goto alloc_no_mem;

	return 0;

alloc_no_mem:
	rc = -ENOMEM;
alloc_err:
	qed_resc_free(cdev);
	return rc;
}

void qed_resc_setup(struct qed_dev *cdev)
{
	int i;

	if (IS_VF(cdev)) {
		for_each_hwfn(cdev, i)
			qed_l2_setup(&cdev->hwfns[i]);
		return;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		qed_cxt_mngr_setup(p_hwfn);
		qed_spq_setup(p_hwfn);
		qed_eq_setup(p_hwfn);
		qed_consq_setup(p_hwfn);

		/* Read shadow of current MFW mailbox */
		qed_mcp_read_mb(p_hwfn, p_hwfn->p_main_ptt);
		memcpy(p_hwfn->mcp_info->mfw_mb_shadow,
		       p_hwfn->mcp_info->mfw_mb_cur,
		       p_hwfn->mcp_info->mfw_mb_length);

		qed_int_setup(p_hwfn, p_hwfn->p_main_ptt);

		qed_l2_setup(p_hwfn);
		qed_iov_setup(p_hwfn);
#ifdef CONFIG_QED_LL2
		if (p_hwfn->using_ll2)
			qed_ll2_setup(p_hwfn);
#endif
		if (p_hwfn->hw_info.personality == QED_PCI_FCOE)
			qed_fcoe_setup(p_hwfn);

		if (p_hwfn->hw_info.personality == QED_PCI_ISCSI) {
			qed_iscsi_setup(p_hwfn);
			qed_ooo_setup(p_hwfn);
		}
	}
}

#define FINAL_CLEANUP_POLL_CNT          (100)
#define FINAL_CLEANUP_POLL_TIME         (10)
int qed_final_cleanup(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt, u16 id, bool is_vf)
{
	u32 command = 0, addr, count = FINAL_CLEANUP_POLL_CNT;
	int rc = -EBUSY;

	addr = GTT_BAR0_MAP_REG_USDM_RAM +
		USTORM_FLR_FINAL_ACK_OFFSET(p_hwfn->rel_pf_id);

	if (is_vf)
		id += 0x10;

	command |= X_FINAL_CLEANUP_AGG_INT <<
		SDM_AGG_INT_COMP_PARAMS_AGG_INT_INDEX_SHIFT;
	command |= 1 << SDM_AGG_INT_COMP_PARAMS_AGG_VECTOR_ENABLE_SHIFT;
	command |= id << SDM_AGG_INT_COMP_PARAMS_AGG_VECTOR_BIT_SHIFT;
	command |= SDM_COMP_TYPE_AGG_INT << SDM_OP_GEN_COMP_TYPE_SHIFT;

	/* Make sure notification is not set before initiating final cleanup */
	if (REG_RD(p_hwfn, addr)) {
		DP_NOTICE(p_hwfn,
			  "Unexpected; Found final cleanup notification before initiating final cleanup\n");
		REG_WR(p_hwfn, addr, 0);
	}

	DP_VERBOSE(p_hwfn, QED_MSG_IOV,
		   "Sending final cleanup for PFVF[%d] [Command %08x\n]",
		   id, command);

	qed_wr(p_hwfn, p_ptt, XSDM_REG_OPERATION_GEN, command);

	/* Poll until completion */
	while (!REG_RD(p_hwfn, addr) && count--)
		msleep(FINAL_CLEANUP_POLL_TIME);

	if (REG_RD(p_hwfn, addr))
		rc = 0;
	else
		DP_NOTICE(p_hwfn,
			  "Failed to receive FW final cleanup notification\n");

	/* Cleanup afterwards */
	REG_WR(p_hwfn, addr, 0);

	return rc;
}

static int qed_calc_hw_mode(struct qed_hwfn *p_hwfn)
{
	int hw_mode = 0;

	if (QED_IS_BB_B0(p_hwfn->cdev)) {
		hw_mode |= 1 << MODE_BB;
	} else if (QED_IS_AH(p_hwfn->cdev)) {
		hw_mode |= 1 << MODE_K2;
	} else {
		DP_NOTICE(p_hwfn, "Unknown chip type %#x\n",
			  p_hwfn->cdev->type);
		return -EINVAL;
	}

	switch (p_hwfn->cdev->num_ports_in_engine) {
	case 1:
		hw_mode |= 1 << MODE_PORTS_PER_ENG_1;
		break;
	case 2:
		hw_mode |= 1 << MODE_PORTS_PER_ENG_2;
		break;
	case 4:
		hw_mode |= 1 << MODE_PORTS_PER_ENG_4;
		break;
	default:
		DP_NOTICE(p_hwfn, "num_ports_in_engine = %d not supported\n",
			  p_hwfn->cdev->num_ports_in_engine);
		return -EINVAL;
	}

	switch (p_hwfn->cdev->mf_mode) {
	case QED_MF_DEFAULT:
	case QED_MF_NPAR:
		hw_mode |= 1 << MODE_MF_SI;
		break;
	case QED_MF_OVLAN:
		hw_mode |= 1 << MODE_MF_SD;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unsupported MF mode, init as DEFAULT\n");
		hw_mode |= 1 << MODE_MF_SI;
	}

	hw_mode |= 1 << MODE_ASIC;

	if (p_hwfn->cdev->num_hwfns > 1)
		hw_mode |= 1 << MODE_100G;

	p_hwfn->hw_info.hw_mode = hw_mode;

	DP_VERBOSE(p_hwfn, (NETIF_MSG_PROBE | NETIF_MSG_IFUP),
		   "Configuring function for hw_mode: 0x%08x\n",
		   p_hwfn->hw_info.hw_mode);

	return 0;
}

/* Init run time data for all PFs on an engine. */
static void qed_init_cau_rt_data(struct qed_dev *cdev)
{
	u32 offset = CAU_REG_SB_VAR_MEMORY_RT_OFFSET;
	int i, igu_sb_id;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
		struct qed_igu_info *p_igu_info;
		struct qed_igu_block *p_block;
		struct cau_sb_entry sb_entry;

		p_igu_info = p_hwfn->hw_info.p_igu_info;

		for (igu_sb_id = 0;
		     igu_sb_id < QED_MAPPING_MEMORY_SIZE(cdev); igu_sb_id++) {
			p_block = &p_igu_info->entry[igu_sb_id];

			if (!p_block->is_pf)
				continue;

			qed_init_cau_sb_entry(p_hwfn, &sb_entry,
					      p_block->function_id, 0, 0);
			STORE_RT_REG_AGG(p_hwfn, offset + igu_sb_id * 2,
					 sb_entry);
		}
	}
}

static void qed_init_cache_line_size(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt)
{
	u32 val, wr_mbs, cache_line_size;

	val = qed_rd(p_hwfn, p_ptt, PSWRQ2_REG_WR_MBS0);
	switch (val) {
	case 0:
		wr_mbs = 128;
		break;
	case 1:
		wr_mbs = 256;
		break;
	case 2:
		wr_mbs = 512;
		break;
	default:
		DP_INFO(p_hwfn,
			"Unexpected value of PSWRQ2_REG_WR_MBS0 [0x%x]. Avoid configuring PGLUE_B_REG_CACHE_LINE_SIZE.\n",
			val);
		return;
	}

	cache_line_size = min_t(u32, L1_CACHE_BYTES, wr_mbs);
	switch (cache_line_size) {
	case 32:
		val = 0;
		break;
	case 64:
		val = 1;
		break;
	case 128:
		val = 2;
		break;
	case 256:
		val = 3;
		break;
	default:
		DP_INFO(p_hwfn,
			"Unexpected value of cache line size [0x%x]. Avoid configuring PGLUE_B_REG_CACHE_LINE_SIZE.\n",
			cache_line_size);
	}

	if (L1_CACHE_BYTES > wr_mbs)
		DP_INFO(p_hwfn,
			"The cache line size for padding is suboptimal for performance [OS cache line size 0x%x, wr mbs 0x%x]\n",
			L1_CACHE_BYTES, wr_mbs);

	STORE_RT_REG(p_hwfn, PGLUE_REG_B_CACHE_LINE_SIZE_RT_OFFSET, val);
	if (val > 0) {
		STORE_RT_REG(p_hwfn, PSWRQ2_REG_DRAM_ALIGN_WR_RT_OFFSET, val);
		STORE_RT_REG(p_hwfn, PSWRQ2_REG_DRAM_ALIGN_RD_RT_OFFSET, val);
	}
}

static int qed_hw_init_common(struct qed_hwfn *p_hwfn,
			      struct qed_ptt *p_ptt, int hw_mode)
{
	struct qed_qm_info *qm_info = &p_hwfn->qm_info;
	struct qed_qm_common_rt_init_params params;
	struct qed_dev *cdev = p_hwfn->cdev;
	u8 vf_id, max_num_vfs;
	u16 num_pfs, pf_id;
	u32 concrete_fid;
	int rc = 0;

	qed_init_cau_rt_data(cdev);

	/* Program GTT windows */
	qed_gtt_init(p_hwfn);

	if (p_hwfn->mcp_info) {
		if (p_hwfn->mcp_info->func_info.bandwidth_max)
			qm_info->pf_rl_en = 1;
		if (p_hwfn->mcp_info->func_info.bandwidth_min)
			qm_info->pf_wfq_en = 1;
	}

	memset(&params, 0, sizeof(params));
	params.max_ports_per_engine = p_hwfn->cdev->num_ports_in_engine;
	params.max_phys_tcs_per_port = qm_info->max_phys_tcs_per_port;
	params.pf_rl_en = qm_info->pf_rl_en;
	params.pf_wfq_en = qm_info->pf_wfq_en;
	params.vport_rl_en = qm_info->vport_rl_en;
	params.vport_wfq_en = qm_info->vport_wfq_en;
	params.port_params = qm_info->qm_port_params;

	qed_qm_common_rt_init(p_hwfn, &params);

	qed_cxt_hw_init_common(p_hwfn);

	qed_init_cache_line_size(p_hwfn, p_ptt);

	rc = qed_init_run(p_hwfn, p_ptt, PHASE_ENGINE, ANY_PHASE_ID, hw_mode);
	if (rc)
		return rc;

	qed_wr(p_hwfn, p_ptt, PSWRQ2_REG_L2P_VALIDATE_VFID, 0);
	qed_wr(p_hwfn, p_ptt, PGLUE_B_REG_USE_CLIENTID_IN_TAG, 1);

	if (QED_IS_BB(p_hwfn->cdev)) {
		num_pfs = NUM_OF_ENG_PFS(p_hwfn->cdev);
		for (pf_id = 0; pf_id < num_pfs; pf_id++) {
			qed_fid_pretend(p_hwfn, p_ptt, pf_id);
			qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_ROCE, 0x0);
			qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_TCP, 0x0);
		}
		/* pretend to original PF */
		qed_fid_pretend(p_hwfn, p_ptt, p_hwfn->rel_pf_id);
	}

	max_num_vfs = QED_IS_AH(cdev) ? MAX_NUM_VFS_K2 : MAX_NUM_VFS_BB;
	for (vf_id = 0; vf_id < max_num_vfs; vf_id++) {
		concrete_fid = qed_vfid_to_concrete(p_hwfn, vf_id);
		qed_fid_pretend(p_hwfn, p_ptt, (u16) concrete_fid);
		qed_wr(p_hwfn, p_ptt, CCFC_REG_STRONG_ENABLE_VF, 0x1);
		qed_wr(p_hwfn, p_ptt, CCFC_REG_WEAK_ENABLE_VF, 0x0);
		qed_wr(p_hwfn, p_ptt, TCFC_REG_STRONG_ENABLE_VF, 0x1);
		qed_wr(p_hwfn, p_ptt, TCFC_REG_WEAK_ENABLE_VF, 0x0);
	}
	/* pretend to original PF */
	qed_fid_pretend(p_hwfn, p_ptt, p_hwfn->rel_pf_id);

	return rc;
}

static int
qed_hw_init_dpi_size(struct qed_hwfn *p_hwfn,
		     struct qed_ptt *p_ptt, u32 pwm_region_size, u32 n_cpus)
{
	u32 dpi_bit_shift, dpi_count, dpi_page_size;
	u32 min_dpis;
	u32 n_wids;

	/* Calculate DPI size */
	n_wids = max_t(u32, QED_MIN_WIDS, n_cpus);
	dpi_page_size = QED_WID_SIZE * roundup_pow_of_two(n_wids);
	dpi_page_size = (dpi_page_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	dpi_bit_shift = ilog2(dpi_page_size / 4096);
	dpi_count = pwm_region_size / dpi_page_size;

	min_dpis = p_hwfn->pf_params.rdma_pf_params.min_dpis;
	min_dpis = max_t(u32, QED_MIN_DPIS, min_dpis);

	p_hwfn->dpi_size = dpi_page_size;
	p_hwfn->dpi_count = dpi_count;

	qed_wr(p_hwfn, p_ptt, DORQ_REG_PF_DPI_BIT_SHIFT, dpi_bit_shift);

	if (dpi_count < min_dpis)
		return -EINVAL;

	return 0;
}

enum QED_ROCE_EDPM_MODE {
	QED_ROCE_EDPM_MODE_ENABLE = 0,
	QED_ROCE_EDPM_MODE_FORCE_ON = 1,
	QED_ROCE_EDPM_MODE_DISABLE = 2,
};

static int
qed_hw_init_pf_doorbell_bar(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 pwm_regsize, norm_regsize;
	u32 non_pwm_conn, min_addr_reg1;
	u32 db_bar_size, n_cpus = 1;
	u32 roce_edpm_mode;
	u32 pf_dems_shift;
	int rc = 0;
	u8 cond;

	db_bar_size = qed_hw_bar_size(p_hwfn, p_ptt, BAR_ID_1);
	if (p_hwfn->cdev->num_hwfns > 1)
		db_bar_size /= 2;

	/* Calculate doorbell regions */
	non_pwm_conn = qed_cxt_get_proto_cid_start(p_hwfn, PROTOCOLID_CORE) +
		       qed_cxt_get_proto_cid_count(p_hwfn, PROTOCOLID_CORE,
						   NULL) +
		       qed_cxt_get_proto_cid_count(p_hwfn, PROTOCOLID_ETH,
						   NULL);
	norm_regsize = roundup(QED_PF_DEMS_SIZE * non_pwm_conn, PAGE_SIZE);
	min_addr_reg1 = norm_regsize / 4096;
	pwm_regsize = db_bar_size - norm_regsize;

	/* Check that the normal and PWM sizes are valid */
	if (db_bar_size < norm_regsize) {
		DP_ERR(p_hwfn->cdev,
		       "Doorbell BAR size 0x%x is too small (normal region is 0x%0x )\n",
		       db_bar_size, norm_regsize);
		return -EINVAL;
	}

	if (pwm_regsize < QED_MIN_PWM_REGION) {
		DP_ERR(p_hwfn->cdev,
		       "PWM region size 0x%0x is too small. Should be at least 0x%0x (Doorbell BAR size is 0x%x and normal region size is 0x%0x)\n",
		       pwm_regsize,
		       QED_MIN_PWM_REGION, db_bar_size, norm_regsize);
		return -EINVAL;
	}

	/* Calculate number of DPIs */
	roce_edpm_mode = p_hwfn->pf_params.rdma_pf_params.roce_edpm_mode;
	if ((roce_edpm_mode == QED_ROCE_EDPM_MODE_ENABLE) ||
	    ((roce_edpm_mode == QED_ROCE_EDPM_MODE_FORCE_ON))) {
		/* Either EDPM is mandatory, or we are attempting to allocate a
		 * WID per CPU.
		 */
		n_cpus = num_present_cpus();
		rc = qed_hw_init_dpi_size(p_hwfn, p_ptt, pwm_regsize, n_cpus);
	}

	cond = (rc && (roce_edpm_mode == QED_ROCE_EDPM_MODE_ENABLE)) ||
	       (roce_edpm_mode == QED_ROCE_EDPM_MODE_DISABLE);
	if (cond || p_hwfn->dcbx_no_edpm) {
		/* Either EDPM is disabled from user configuration, or it is
		 * disabled via DCBx, or it is not mandatory and we failed to
		 * allocated a WID per CPU.
		 */
		n_cpus = 1;
		rc = qed_hw_init_dpi_size(p_hwfn, p_ptt, pwm_regsize, n_cpus);

		if (cond)
			qed_rdma_dpm_bar(p_hwfn, p_ptt);
	}

	p_hwfn->wid_count = (u16) n_cpus;

	DP_INFO(p_hwfn,
		"doorbell bar: normal_region_size=%d, pwm_region_size=%d, dpi_size=%d, dpi_count=%d, roce_edpm=%s\n",
		norm_regsize,
		pwm_regsize,
		p_hwfn->dpi_size,
		p_hwfn->dpi_count,
		((p_hwfn->dcbx_no_edpm) || (p_hwfn->db_bar_no_edpm)) ?
		"disabled" : "enabled");

	if (rc) {
		DP_ERR(p_hwfn,
		       "Failed to allocate enough DPIs. Allocated %d but the current minimum is %d.\n",
		       p_hwfn->dpi_count,
		       p_hwfn->pf_params.rdma_pf_params.min_dpis);
		return -EINVAL;
	}

	p_hwfn->dpi_start_offset = norm_regsize;

	/* DEMS size is configured log2 of DWORDs, hence the division by 4 */
	pf_dems_shift = ilog2(QED_PF_DEMS_SIZE / 4);
	qed_wr(p_hwfn, p_ptt, DORQ_REG_PF_ICID_BIT_SHIFT_NORM, pf_dems_shift);
	qed_wr(p_hwfn, p_ptt, DORQ_REG_PF_MIN_ADDR_REG1, min_addr_reg1);

	return 0;
}

static int qed_hw_init_port(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt, int hw_mode)
{
	int rc = 0;

	rc = qed_init_run(p_hwfn, p_ptt, PHASE_PORT, p_hwfn->port_id, hw_mode);
	if (rc)
		return rc;

	qed_wr(p_hwfn, p_ptt, PGLUE_B_REG_MASTER_WRITE_PAD_ENABLE, 0);

	return 0;
}

static int qed_hw_init_pf(struct qed_hwfn *p_hwfn,
			  struct qed_ptt *p_ptt,
			  struct qed_tunnel_info *p_tunn,
			  int hw_mode,
			  bool b_hw_start,
			  enum qed_int_mode int_mode,
			  bool allow_npar_tx_switch)
{
	u8 rel_pf_id = p_hwfn->rel_pf_id;
	int rc = 0;

	if (p_hwfn->mcp_info) {
		struct qed_mcp_function_info *p_info;

		p_info = &p_hwfn->mcp_info->func_info;
		if (p_info->bandwidth_min)
			p_hwfn->qm_info.pf_wfq = p_info->bandwidth_min;

		/* Update rate limit once we'll actually have a link */
		p_hwfn->qm_info.pf_rl = 100000;
	}

	qed_cxt_hw_init_pf(p_hwfn, p_ptt);

	qed_int_igu_init_rt(p_hwfn);

	/* Set VLAN in NIG if needed */
	if (hw_mode & BIT(MODE_MF_SD)) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_HW, "Configuring LLH_FUNC_TAG\n");
		STORE_RT_REG(p_hwfn, NIG_REG_LLH_FUNC_TAG_EN_RT_OFFSET, 1);
		STORE_RT_REG(p_hwfn, NIG_REG_LLH_FUNC_TAG_VALUE_RT_OFFSET,
			     p_hwfn->hw_info.ovlan);
	}

	/* Enable classification by MAC if needed */
	if (hw_mode & BIT(MODE_MF_SI)) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
			   "Configuring TAGMAC_CLS_TYPE\n");
		STORE_RT_REG(p_hwfn,
			     NIG_REG_LLH_FUNC_TAGMAC_CLS_TYPE_RT_OFFSET, 1);
	}

	/* Protocl Configuration  */
	STORE_RT_REG(p_hwfn, PRS_REG_SEARCH_TCP_RT_OFFSET,
		     (p_hwfn->hw_info.personality == QED_PCI_ISCSI) ? 1 : 0);
	STORE_RT_REG(p_hwfn, PRS_REG_SEARCH_FCOE_RT_OFFSET,
		     (p_hwfn->hw_info.personality == QED_PCI_FCOE) ? 1 : 0);
	STORE_RT_REG(p_hwfn, PRS_REG_SEARCH_ROCE_RT_OFFSET, 0);

	/* Cleanup chip from previous driver if such remains exist */
	rc = qed_final_cleanup(p_hwfn, p_ptt, rel_pf_id, false);
	if (rc)
		return rc;

	/* PF Init sequence */
	rc = qed_init_run(p_hwfn, p_ptt, PHASE_PF, rel_pf_id, hw_mode);
	if (rc)
		return rc;

	/* QM_PF Init sequence (may be invoked separately e.g. for DCB) */
	rc = qed_init_run(p_hwfn, p_ptt, PHASE_QM_PF, rel_pf_id, hw_mode);
	if (rc)
		return rc;

	/* Pure runtime initializations - directly to the HW  */
	qed_int_igu_init_pure_rt(p_hwfn, p_ptt, true, true);

	rc = qed_hw_init_pf_doorbell_bar(p_hwfn, p_ptt);
	if (rc)
		return rc;

	if (b_hw_start) {
		/* enable interrupts */
		qed_int_igu_enable(p_hwfn, p_ptt, int_mode);

		/* send function start command */
		rc = qed_sp_pf_start(p_hwfn, p_ptt, p_tunn,
				     p_hwfn->cdev->mf_mode,
				     allow_npar_tx_switch);
		if (rc) {
			DP_NOTICE(p_hwfn, "Function start ramrod failed\n");
			return rc;
		}
		if (p_hwfn->hw_info.personality == QED_PCI_FCOE) {
			qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_TAG1, BIT(2));
			qed_wr(p_hwfn, p_ptt,
			       PRS_REG_PKT_LEN_STAT_TAGS_NOT_COUNTED_FIRST,
			       0x100);
		}
	}
	return rc;
}

static int qed_change_pci_hwfn(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt,
			       u8 enable)
{
	u32 delay_idx = 0, val, set_val = enable ? 1 : 0;

	/* Change PF in PXP */
	qed_wr(p_hwfn, p_ptt,
	       PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER, set_val);

	/* wait until value is set - try for 1 second every 50us */
	for (delay_idx = 0; delay_idx < 20000; delay_idx++) {
		val = qed_rd(p_hwfn, p_ptt,
			     PGLUE_B_REG_INTERNAL_PFID_ENABLE_MASTER);
		if (val == set_val)
			break;

		usleep_range(50, 60);
	}

	if (val != set_val) {
		DP_NOTICE(p_hwfn,
			  "PFID_ENABLE_MASTER wasn't changed after a second\n");
		return -EAGAIN;
	}

	return 0;
}

static void qed_reset_mb_shadow(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_main_ptt)
{
	/* Read shadow of current MFW mailbox */
	qed_mcp_read_mb(p_hwfn, p_main_ptt);
	memcpy(p_hwfn->mcp_info->mfw_mb_shadow,
	       p_hwfn->mcp_info->mfw_mb_cur, p_hwfn->mcp_info->mfw_mb_length);
}

static void
qed_fill_load_req_params(struct qed_load_req_params *p_load_req,
			 struct qed_drv_load_params *p_drv_load)
{
	memset(p_load_req, 0, sizeof(*p_load_req));

	p_load_req->drv_role = p_drv_load->is_crash_kernel ?
			       QED_DRV_ROLE_KDUMP : QED_DRV_ROLE_OS;
	p_load_req->timeout_val = p_drv_load->mfw_timeout_val;
	p_load_req->avoid_eng_reset = p_drv_load->avoid_eng_reset;
	p_load_req->override_force_load = p_drv_load->override_force_load;
}

static int qed_vf_start(struct qed_hwfn *p_hwfn,
			struct qed_hw_init_params *p_params)
{
	if (p_params->p_tunn) {
		qed_vf_set_vf_start_tunn_update_param(p_params->p_tunn);
		qed_vf_pf_tunnel_param_update(p_hwfn, p_params->p_tunn);
	}

	p_hwfn->b_int_enabled = 1;

	return 0;
}

int qed_hw_init(struct qed_dev *cdev, struct qed_hw_init_params *p_params)
{
	struct qed_load_req_params load_req_params;
	u32 load_code, param, drv_mb_param;
	bool b_default_mtu = true;
	struct qed_hwfn *p_hwfn;
	int rc = 0, mfw_rc, i;

	if ((p_params->int_mode == QED_INT_MODE_MSI) && (cdev->num_hwfns > 1)) {
		DP_NOTICE(cdev, "MSI mode is not supported for CMT devices\n");
		return -EINVAL;
	}

	if (IS_PF(cdev)) {
		rc = qed_init_fw_data(cdev, p_params->bin_fw_data);
		if (rc)
			return rc;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		/* If management didn't provide a default, set one of our own */
		if (!p_hwfn->hw_info.mtu) {
			p_hwfn->hw_info.mtu = 1500;
			b_default_mtu = false;
		}

		if (IS_VF(cdev)) {
			qed_vf_start(p_hwfn, p_params);
			continue;
		}

		/* Enable DMAE in PXP */
		rc = qed_change_pci_hwfn(p_hwfn, p_hwfn->p_main_ptt, true);

		rc = qed_calc_hw_mode(p_hwfn);
		if (rc)
			return rc;

		qed_fill_load_req_params(&load_req_params,
					 p_params->p_drv_load_params);
		rc = qed_mcp_load_req(p_hwfn, p_hwfn->p_main_ptt,
				      &load_req_params);
		if (rc) {
			DP_NOTICE(p_hwfn, "Failed sending a LOAD_REQ command\n");
			return rc;
		}

		load_code = load_req_params.load_code;
		DP_VERBOSE(p_hwfn, QED_MSG_SP,
			   "Load request was sent. Load code: 0x%x\n",
			   load_code);

		qed_reset_mb_shadow(p_hwfn, p_hwfn->p_main_ptt);

		p_hwfn->first_on_engine = (load_code ==
					   FW_MSG_CODE_DRV_LOAD_ENGINE);

		switch (load_code) {
		case FW_MSG_CODE_DRV_LOAD_ENGINE:
			rc = qed_hw_init_common(p_hwfn, p_hwfn->p_main_ptt,
						p_hwfn->hw_info.hw_mode);
			if (rc)
				break;
		/* Fall into */
		case FW_MSG_CODE_DRV_LOAD_PORT:
			rc = qed_hw_init_port(p_hwfn, p_hwfn->p_main_ptt,
					      p_hwfn->hw_info.hw_mode);
			if (rc)
				break;

		/* Fall into */
		case FW_MSG_CODE_DRV_LOAD_FUNCTION:
			rc = qed_hw_init_pf(p_hwfn, p_hwfn->p_main_ptt,
					    p_params->p_tunn,
					    p_hwfn->hw_info.hw_mode,
					    p_params->b_hw_start,
					    p_params->int_mode,
					    p_params->allow_npar_tx_switch);
			break;
		default:
			DP_NOTICE(p_hwfn,
				  "Unexpected load code [0x%08x]", load_code);
			rc = -EINVAL;
			break;
		}

		if (rc)
			DP_NOTICE(p_hwfn,
				  "init phase failed for loadcode 0x%x (rc %d)\n",
				   load_code, rc);

		/* ACK mfw regardless of success or failure of initialization */
		mfw_rc = qed_mcp_cmd(p_hwfn, p_hwfn->p_main_ptt,
				     DRV_MSG_CODE_LOAD_DONE,
				     0, &load_code, &param);
		if (rc)
			return rc;
		if (mfw_rc) {
			DP_NOTICE(p_hwfn, "Failed sending LOAD_DONE command\n");
			return mfw_rc;
		}

		/* Check if there is a DID mismatch between nvm-cfg/efuse */
		if (param & FW_MB_PARAM_LOAD_DONE_DID_EFUSE_ERROR)
			DP_NOTICE(p_hwfn,
				  "warning: device configuration is not supported on this board type. The device may not function as expected.\n");

		/* send DCBX attention request command */
		DP_VERBOSE(p_hwfn,
			   QED_MSG_DCB,
			   "sending phony dcbx set command to trigger DCBx attention handling\n");
		mfw_rc = qed_mcp_cmd(p_hwfn, p_hwfn->p_main_ptt,
				     DRV_MSG_CODE_SET_DCBX,
				     1 << DRV_MB_PARAM_DCBX_NOTIFY_SHIFT,
				     &load_code, &param);
		if (mfw_rc) {
			DP_NOTICE(p_hwfn,
				  "Failed to send DCBX attention request\n");
			return mfw_rc;
		}

		p_hwfn->hw_init_done = true;
	}

	if (IS_PF(cdev)) {
		p_hwfn = QED_LEADING_HWFN(cdev);
		drv_mb_param = STORM_FW_VERSION;
		rc = qed_mcp_cmd(p_hwfn, p_hwfn->p_main_ptt,
				 DRV_MSG_CODE_OV_UPDATE_STORM_FW_VER,
				 drv_mb_param, &load_code, &param);
		if (rc)
			DP_INFO(p_hwfn, "Failed to update firmware version\n");

		if (!b_default_mtu) {
			rc = qed_mcp_ov_update_mtu(p_hwfn, p_hwfn->p_main_ptt,
						   p_hwfn->hw_info.mtu);
			if (rc)
				DP_INFO(p_hwfn,
					"Failed to update default mtu\n");
		}

		rc = qed_mcp_ov_update_driver_state(p_hwfn,
						    p_hwfn->p_main_ptt,
						  QED_OV_DRIVER_STATE_DISABLED);
		if (rc)
			DP_INFO(p_hwfn, "Failed to update driver state\n");

		rc = qed_mcp_ov_update_eswitch(p_hwfn, p_hwfn->p_main_ptt,
					       QED_OV_ESWITCH_VEB);
		if (rc)
			DP_INFO(p_hwfn, "Failed to update eswitch mode\n");
	}

	return 0;
}

#define QED_HW_STOP_RETRY_LIMIT (10)
static void qed_hw_timers_stop(struct qed_dev *cdev,
			       struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	int i;

	/* close timers */
	qed_wr(p_hwfn, p_ptt, TM_REG_PF_ENABLE_CONN, 0x0);
	qed_wr(p_hwfn, p_ptt, TM_REG_PF_ENABLE_TASK, 0x0);

	for (i = 0; i < QED_HW_STOP_RETRY_LIMIT; i++) {
		if ((!qed_rd(p_hwfn, p_ptt,
			     TM_REG_PF_SCAN_ACTIVE_CONN)) &&
		    (!qed_rd(p_hwfn, p_ptt, TM_REG_PF_SCAN_ACTIVE_TASK)))
			break;

		/* Dependent on number of connection/tasks, possibly
		 * 1ms sleep is required between polls
		 */
		usleep_range(1000, 2000);
	}

	if (i < QED_HW_STOP_RETRY_LIMIT)
		return;

	DP_NOTICE(p_hwfn,
		  "Timers linear scans are not over [Connection %02x Tasks %02x]\n",
		  (u8)qed_rd(p_hwfn, p_ptt, TM_REG_PF_SCAN_ACTIVE_CONN),
		  (u8)qed_rd(p_hwfn, p_ptt, TM_REG_PF_SCAN_ACTIVE_TASK));
}

void qed_hw_timers_stop_all(struct qed_dev *cdev)
{
	int j;

	for_each_hwfn(cdev, j) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[j];
		struct qed_ptt *p_ptt = p_hwfn->p_main_ptt;

		qed_hw_timers_stop(cdev, p_hwfn, p_ptt);
	}
}

int qed_hw_stop(struct qed_dev *cdev)
{
	struct qed_hwfn *p_hwfn;
	struct qed_ptt *p_ptt;
	int rc, rc2 = 0;
	int j;

	for_each_hwfn(cdev, j) {
		p_hwfn = &cdev->hwfns[j];
		p_ptt = p_hwfn->p_main_ptt;

		DP_VERBOSE(p_hwfn, NETIF_MSG_IFDOWN, "Stopping hw/fw\n");

		if (IS_VF(cdev)) {
			qed_vf_pf_int_cleanup(p_hwfn);
			rc = qed_vf_pf_reset(p_hwfn);
			if (rc) {
				DP_NOTICE(p_hwfn,
					  "qed_vf_pf_reset failed. rc = %d.\n",
					  rc);
				rc2 = -EINVAL;
			}
			continue;
		}

		/* mark the hw as uninitialized... */
		p_hwfn->hw_init_done = false;

		/* Send unload command to MCP */
		rc = qed_mcp_unload_req(p_hwfn, p_ptt);
		if (rc) {
			DP_NOTICE(p_hwfn,
				  "Failed sending a UNLOAD_REQ command. rc = %d.\n",
				  rc);
			rc2 = -EINVAL;
		}

		qed_slowpath_irq_sync(p_hwfn);

		/* After this point no MFW attentions are expected, e.g. prevent
		 * race between pf stop and dcbx pf update.
		 */
		rc = qed_sp_pf_stop(p_hwfn);
		if (rc) {
			DP_NOTICE(p_hwfn,
				  "Failed to close PF against FW [rc = %d]. Continue to stop HW to prevent illegal host access by the device.\n",
				  rc);
			rc2 = -EINVAL;
		}

		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_RX_LLH_BRB_GATE_DNTFWD_PERPF, 0x1);

		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_TCP, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_UDP, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_FCOE, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_ROCE, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_OPENFLOW, 0x0);

		qed_hw_timers_stop(cdev, p_hwfn, p_ptt);

		/* Disable Attention Generation */
		qed_int_igu_disable_int(p_hwfn, p_ptt);

		qed_wr(p_hwfn, p_ptt, IGU_REG_LEADING_EDGE_LATCH, 0);
		qed_wr(p_hwfn, p_ptt, IGU_REG_TRAILING_EDGE_LATCH, 0);

		qed_int_igu_init_pure_rt(p_hwfn, p_ptt, false, true);

		/* Need to wait 1ms to guarantee SBs are cleared */
		usleep_range(1000, 2000);

		/* Disable PF in HW blocks */
		qed_wr(p_hwfn, p_ptt, DORQ_REG_PF_DB_ENABLE, 0);
		qed_wr(p_hwfn, p_ptt, QM_REG_PF_EN, 0);

		qed_mcp_unload_done(p_hwfn, p_ptt);
		if (rc) {
			DP_NOTICE(p_hwfn,
				  "Failed sending a UNLOAD_DONE command. rc = %d.\n",
				  rc);
			rc2 = -EINVAL;
		}
	}

	if (IS_PF(cdev)) {
		p_hwfn = QED_LEADING_HWFN(cdev);
		p_ptt = QED_LEADING_HWFN(cdev)->p_main_ptt;

		/* Disable DMAE in PXP - in CMT, this should only be done for
		 * first hw-function, and only after all transactions have
		 * stopped for all active hw-functions.
		 */
		rc = qed_change_pci_hwfn(p_hwfn, p_ptt, false);
		if (rc) {
			DP_NOTICE(p_hwfn,
				  "qed_change_pci_hwfn failed. rc = %d.\n", rc);
			rc2 = -EINVAL;
		}
	}

	return rc2;
}

int qed_hw_stop_fastpath(struct qed_dev *cdev)
{
	int j;

	for_each_hwfn(cdev, j) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[j];
		struct qed_ptt *p_ptt;

		if (IS_VF(cdev)) {
			qed_vf_pf_int_cleanup(p_hwfn);
			continue;
		}
		p_ptt = qed_ptt_acquire(p_hwfn);
		if (!p_ptt)
			return -EAGAIN;

		DP_VERBOSE(p_hwfn,
			   NETIF_MSG_IFDOWN, "Shutting down the fastpath\n");

		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_RX_LLH_BRB_GATE_DNTFWD_PERPF, 0x1);

		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_TCP, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_UDP, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_FCOE, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_ROCE, 0x0);
		qed_wr(p_hwfn, p_ptt, PRS_REG_SEARCH_OPENFLOW, 0x0);

		qed_int_igu_init_pure_rt(p_hwfn, p_ptt, false, false);

		/* Need to wait 1ms to guarantee SBs are cleared */
		usleep_range(1000, 2000);
		qed_ptt_release(p_hwfn, p_ptt);
	}

	return 0;
}

int qed_hw_start_fastpath(struct qed_hwfn *p_hwfn)
{
	struct qed_ptt *p_ptt;

	if (IS_VF(p_hwfn->cdev))
		return 0;

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return -EAGAIN;

	/* If roce info is allocated it means roce is initialized and should
	 * be enabled in searcher.
	 */
	if (p_hwfn->p_rdma_info &&
	    p_hwfn->b_rdma_enabled_in_prs)
		qed_wr(p_hwfn, p_ptt, p_hwfn->rdma_prs_search_reg, 0x1);

	/* Re-open incoming traffic */
	qed_wr(p_hwfn, p_ptt, NIG_REG_RX_LLH_BRB_GATE_DNTFWD_PERPF, 0x0);
	qed_ptt_release(p_hwfn, p_ptt);

	return 0;
}

/* Free hwfn memory and resources acquired in hw_hwfn_prepare */
static void qed_hw_hwfn_free(struct qed_hwfn *p_hwfn)
{
	qed_ptt_pool_free(p_hwfn);
	kfree(p_hwfn->hw_info.p_igu_info);
	p_hwfn->hw_info.p_igu_info = NULL;
}

/* Setup bar access */
static void qed_hw_hwfn_prepare(struct qed_hwfn *p_hwfn)
{
	/* clear indirect access */
	if (QED_IS_AH(p_hwfn->cdev)) {
		qed_wr(p_hwfn, p_hwfn->p_main_ptt,
		       PGLUE_B_REG_PGL_ADDR_E8_F0_K2, 0);
		qed_wr(p_hwfn, p_hwfn->p_main_ptt,
		       PGLUE_B_REG_PGL_ADDR_EC_F0_K2, 0);
		qed_wr(p_hwfn, p_hwfn->p_main_ptt,
		       PGLUE_B_REG_PGL_ADDR_F0_F0_K2, 0);
		qed_wr(p_hwfn, p_hwfn->p_main_ptt,
		       PGLUE_B_REG_PGL_ADDR_F4_F0_K2, 0);
	} else {
		qed_wr(p_hwfn, p_hwfn->p_main_ptt,
		       PGLUE_B_REG_PGL_ADDR_88_F0_BB, 0);
		qed_wr(p_hwfn, p_hwfn->p_main_ptt,
		       PGLUE_B_REG_PGL_ADDR_8C_F0_BB, 0);
		qed_wr(p_hwfn, p_hwfn->p_main_ptt,
		       PGLUE_B_REG_PGL_ADDR_90_F0_BB, 0);
		qed_wr(p_hwfn, p_hwfn->p_main_ptt,
		       PGLUE_B_REG_PGL_ADDR_94_F0_BB, 0);
	}

	/* Clean Previous errors if such exist */
	qed_wr(p_hwfn, p_hwfn->p_main_ptt,
	       PGLUE_B_REG_WAS_ERROR_PF_31_0_CLR, 1 << p_hwfn->abs_pf_id);

	/* enable internal target-read */
	qed_wr(p_hwfn, p_hwfn->p_main_ptt,
	       PGLUE_B_REG_INTERNAL_PFID_ENABLE_TARGET_READ, 1);
}

static void get_function_id(struct qed_hwfn *p_hwfn)
{
	/* ME Register */
	p_hwfn->hw_info.opaque_fid = (u16) REG_RD(p_hwfn,
						  PXP_PF_ME_OPAQUE_ADDR);

	p_hwfn->hw_info.concrete_fid = REG_RD(p_hwfn, PXP_PF_ME_CONCRETE_ADDR);

	p_hwfn->abs_pf_id = (p_hwfn->hw_info.concrete_fid >> 16) & 0xf;
	p_hwfn->rel_pf_id = GET_FIELD(p_hwfn->hw_info.concrete_fid,
				      PXP_CONCRETE_FID_PFID);
	p_hwfn->port_id = GET_FIELD(p_hwfn->hw_info.concrete_fid,
				    PXP_CONCRETE_FID_PORT);

	DP_VERBOSE(p_hwfn, NETIF_MSG_PROBE,
		   "Read ME register: Concrete 0x%08x Opaque 0x%04x\n",
		   p_hwfn->hw_info.concrete_fid, p_hwfn->hw_info.opaque_fid);
}

static void qed_hw_set_feat(struct qed_hwfn *p_hwfn)
{
	u32 *feat_num = p_hwfn->hw_info.feat_num;
	struct qed_sb_cnt_info sb_cnt;
	u32 non_l2_sbs = 0;

	memset(&sb_cnt, 0, sizeof(sb_cnt));
	qed_int_get_num_sbs(p_hwfn, &sb_cnt);

	if (IS_ENABLED(CONFIG_QED_RDMA) &&
	    QED_IS_RDMA_PERSONALITY(p_hwfn)) {
		/* Roce CNQ each requires: 1 status block + 1 CNQ. We divide
		 * the status blocks equally between L2 / RoCE but with
		 * consideration as to how many l2 queues / cnqs we have.
		 */
		feat_num[QED_RDMA_CNQ] =
			min_t(u32, sb_cnt.cnt / 2,
			      RESC_NUM(p_hwfn, QED_RDMA_CNQ_RAM));

		non_l2_sbs = feat_num[QED_RDMA_CNQ];
	}
	if (QED_IS_L2_PERSONALITY(p_hwfn)) {
		/* Start by allocating VF queues, then PF's */
		feat_num[QED_VF_L2_QUE] = min_t(u32,
						RESC_NUM(p_hwfn, QED_L2_QUEUE),
						sb_cnt.iov_cnt);
		feat_num[QED_PF_L2_QUE] = min_t(u32,
						sb_cnt.cnt - non_l2_sbs,
						RESC_NUM(p_hwfn,
							 QED_L2_QUEUE) -
						FEAT_NUM(p_hwfn,
							 QED_VF_L2_QUE));
	}

	if (QED_IS_FCOE_PERSONALITY(p_hwfn))
		feat_num[QED_FCOE_CQ] =  min_t(u32, sb_cnt.cnt,
					       RESC_NUM(p_hwfn,
							QED_CMDQS_CQS));

	if (QED_IS_ISCSI_PERSONALITY(p_hwfn))
		feat_num[QED_ISCSI_CQ] = min_t(u32, sb_cnt.cnt,
					       RESC_NUM(p_hwfn,
							QED_CMDQS_CQS));
	DP_VERBOSE(p_hwfn,
		   NETIF_MSG_PROBE,
		   "#PF_L2_QUEUES=%d VF_L2_QUEUES=%d #ROCE_CNQ=%d FCOE_CQ=%d ISCSI_CQ=%d #SBS=%d\n",
		   (int)FEAT_NUM(p_hwfn, QED_PF_L2_QUE),
		   (int)FEAT_NUM(p_hwfn, QED_VF_L2_QUE),
		   (int)FEAT_NUM(p_hwfn, QED_RDMA_CNQ),
		   (int)FEAT_NUM(p_hwfn, QED_FCOE_CQ),
		   (int)FEAT_NUM(p_hwfn, QED_ISCSI_CQ),
		   (int)sb_cnt.cnt);
}

const char *qed_hw_get_resc_name(enum qed_resources res_id)
{
	switch (res_id) {
	case QED_L2_QUEUE:
		return "L2_QUEUE";
	case QED_VPORT:
		return "VPORT";
	case QED_RSS_ENG:
		return "RSS_ENG";
	case QED_PQ:
		return "PQ";
	case QED_RL:
		return "RL";
	case QED_MAC:
		return "MAC";
	case QED_VLAN:
		return "VLAN";
	case QED_RDMA_CNQ_RAM:
		return "RDMA_CNQ_RAM";
	case QED_ILT:
		return "ILT";
	case QED_LL2_QUEUE:
		return "LL2_QUEUE";
	case QED_CMDQS_CQS:
		return "CMDQS_CQS";
	case QED_RDMA_STATS_QUEUE:
		return "RDMA_STATS_QUEUE";
	case QED_BDQ:
		return "BDQ";
	case QED_SB:
		return "SB";
	default:
		return "UNKNOWN_RESOURCE";
	}
}

static int
__qed_hw_set_soft_resc_size(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    enum qed_resources res_id,
			    u32 resc_max_val, u32 *p_mcp_resp)
{
	int rc;

	rc = qed_mcp_set_resc_max_val(p_hwfn, p_ptt, res_id,
				      resc_max_val, p_mcp_resp);
	if (rc) {
		DP_NOTICE(p_hwfn,
			  "MFW response failure for a max value setting of resource %d [%s]\n",
			  res_id, qed_hw_get_resc_name(res_id));
		return rc;
	}

	if (*p_mcp_resp != FW_MSG_CODE_RESOURCE_ALLOC_OK)
		DP_INFO(p_hwfn,
			"Failed to set the max value of resource %d [%s]. mcp_resp = 0x%08x.\n",
			res_id, qed_hw_get_resc_name(res_id), *p_mcp_resp);

	return 0;
}

static int
qed_hw_set_soft_resc_size(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	bool b_ah = QED_IS_AH(p_hwfn->cdev);
	u32 resc_max_val, mcp_resp;
	u8 res_id;
	int rc;

	for (res_id = 0; res_id < QED_MAX_RESC; res_id++) {
		switch (res_id) {
		case QED_LL2_QUEUE:
			resc_max_val = MAX_NUM_LL2_RX_QUEUES;
			break;
		case QED_RDMA_CNQ_RAM:
			/* No need for a case for QED_CMDQS_CQS since
			 * CNQ/CMDQS are the same resource.
			 */
			resc_max_val = NUM_OF_CMDQS_CQS;
			break;
		case QED_RDMA_STATS_QUEUE:
			resc_max_val = b_ah ? RDMA_NUM_STATISTIC_COUNTERS_K2
			    : RDMA_NUM_STATISTIC_COUNTERS_BB;
			break;
		case QED_BDQ:
			resc_max_val = BDQ_NUM_RESOURCES;
			break;
		default:
			continue;
		}

		rc = __qed_hw_set_soft_resc_size(p_hwfn, p_ptt, res_id,
						 resc_max_val, &mcp_resp);
		if (rc)
			return rc;

		/* There's no point to continue to the next resource if the
		 * command is not supported by the MFW.
		 * We do continue if the command is supported but the resource
		 * is unknown to the MFW. Such a resource will be later
		 * configured with the default allocation values.
		 */
		if (mcp_resp == FW_MSG_CODE_UNSUPPORTED)
			return -EINVAL;
	}

	return 0;
}

static
int qed_hw_get_dflt_resc(struct qed_hwfn *p_hwfn,
			 enum qed_resources res_id,
			 u32 *p_resc_num, u32 *p_resc_start)
{
	u8 num_funcs = p_hwfn->num_funcs_on_engine;
	bool b_ah = QED_IS_AH(p_hwfn->cdev);

	switch (res_id) {
	case QED_L2_QUEUE:
		*p_resc_num = (b_ah ? MAX_NUM_L2_QUEUES_K2 :
			       MAX_NUM_L2_QUEUES_BB) / num_funcs;
		break;
	case QED_VPORT:
		*p_resc_num = (b_ah ? MAX_NUM_VPORTS_K2 :
			       MAX_NUM_VPORTS_BB) / num_funcs;
		break;
	case QED_RSS_ENG:
		*p_resc_num = (b_ah ? ETH_RSS_ENGINE_NUM_K2 :
			       ETH_RSS_ENGINE_NUM_BB) / num_funcs;
		break;
	case QED_PQ:
		*p_resc_num = (b_ah ? MAX_QM_TX_QUEUES_K2 :
			       MAX_QM_TX_QUEUES_BB) / num_funcs;
		*p_resc_num &= ~0x7;	/* The granularity of the PQs is 8 */
		break;
	case QED_RL:
		*p_resc_num = MAX_QM_GLOBAL_RLS / num_funcs;
		break;
	case QED_MAC:
	case QED_VLAN:
		/* Each VFC resource can accommodate both a MAC and a VLAN */
		*p_resc_num = ETH_NUM_MAC_FILTERS / num_funcs;
		break;
	case QED_ILT:
		*p_resc_num = (b_ah ? PXP_NUM_ILT_RECORDS_K2 :
			       PXP_NUM_ILT_RECORDS_BB) / num_funcs;
		break;
	case QED_LL2_QUEUE:
		*p_resc_num = MAX_NUM_LL2_RX_QUEUES / num_funcs;
		break;
	case QED_RDMA_CNQ_RAM:
	case QED_CMDQS_CQS:
		/* CNQ/CMDQS are the same resource */
		*p_resc_num = NUM_OF_CMDQS_CQS / num_funcs;
		break;
	case QED_RDMA_STATS_QUEUE:
		*p_resc_num = (b_ah ? RDMA_NUM_STATISTIC_COUNTERS_K2 :
			       RDMA_NUM_STATISTIC_COUNTERS_BB) / num_funcs;
		break;
	case QED_BDQ:
		if (p_hwfn->hw_info.personality != QED_PCI_ISCSI &&
		    p_hwfn->hw_info.personality != QED_PCI_FCOE)
			*p_resc_num = 0;
		else
			*p_resc_num = 1;
		break;
	case QED_SB:
		/* Since we want its value to reflect whether MFW supports
		 * the new scheme, have a default of 0.
		 */
		*p_resc_num = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (res_id) {
	case QED_BDQ:
		if (!*p_resc_num)
			*p_resc_start = 0;
		else if (p_hwfn->cdev->num_ports_in_engine == 4)
			*p_resc_start = p_hwfn->port_id;
		else if (p_hwfn->hw_info.personality == QED_PCI_ISCSI)
			*p_resc_start = p_hwfn->port_id;
		else if (p_hwfn->hw_info.personality == QED_PCI_FCOE)
			*p_resc_start = p_hwfn->port_id + 2;
		break;
	default:
		*p_resc_start = *p_resc_num * p_hwfn->enabled_func_idx;
		break;
	}

	return 0;
}

static int __qed_hw_set_resc_info(struct qed_hwfn *p_hwfn,
				  enum qed_resources res_id)
{
	u32 dflt_resc_num = 0, dflt_resc_start = 0;
	u32 mcp_resp, *p_resc_num, *p_resc_start;
	int rc;

	p_resc_num = &RESC_NUM(p_hwfn, res_id);
	p_resc_start = &RESC_START(p_hwfn, res_id);

	rc = qed_hw_get_dflt_resc(p_hwfn, res_id, &dflt_resc_num,
				  &dflt_resc_start);
	if (rc) {
		DP_ERR(p_hwfn,
		       "Failed to get default amount for resource %d [%s]\n",
		       res_id, qed_hw_get_resc_name(res_id));
		return rc;
	}

	rc = qed_mcp_get_resc_info(p_hwfn, p_hwfn->p_main_ptt, res_id,
				   &mcp_resp, p_resc_num, p_resc_start);
	if (rc) {
		DP_NOTICE(p_hwfn,
			  "MFW response failure for an allocation request for resource %d [%s]\n",
			  res_id, qed_hw_get_resc_name(res_id));
		return rc;
	}

	/* Default driver values are applied in the following cases:
	 * - The resource allocation MB command is not supported by the MFW
	 * - There is an internal error in the MFW while processing the request
	 * - The resource ID is unknown to the MFW
	 */
	if (mcp_resp != FW_MSG_CODE_RESOURCE_ALLOC_OK) {
		DP_INFO(p_hwfn,
			"Failed to receive allocation info for resource %d [%s]. mcp_resp = 0x%x. Applying default values [%d,%d].\n",
			res_id,
			qed_hw_get_resc_name(res_id),
			mcp_resp, dflt_resc_num, dflt_resc_start);
		*p_resc_num = dflt_resc_num;
		*p_resc_start = dflt_resc_start;
		goto out;
	}

out:
	/* PQs have to divide by 8 [that's the HW granularity].
	 * Reduce number so it would fit.
	 */
	if ((res_id == QED_PQ) && ((*p_resc_num % 8) || (*p_resc_start % 8))) {
		DP_INFO(p_hwfn,
			"PQs need to align by 8; Number %08x --> %08x, Start %08x --> %08x\n",
			*p_resc_num,
			(*p_resc_num) & ~0x7,
			*p_resc_start, (*p_resc_start) & ~0x7);
		*p_resc_num &= ~0x7;
		*p_resc_start &= ~0x7;
	}

	return 0;
}

static int qed_hw_set_resc_info(struct qed_hwfn *p_hwfn)
{
	int rc;
	u8 res_id;

	for (res_id = 0; res_id < QED_MAX_RESC; res_id++) {
		rc = __qed_hw_set_resc_info(p_hwfn, res_id);
		if (rc)
			return rc;
	}

	return 0;
}

static int qed_hw_get_resc(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_resc_unlock_params resc_unlock_params;
	struct qed_resc_lock_params resc_lock_params;
	bool b_ah = QED_IS_AH(p_hwfn->cdev);
	u8 res_id;
	int rc;

	/* Setting the max values of the soft resources and the following
	 * resources allocation queries should be atomic. Since several PFs can
	 * run in parallel - a resource lock is needed.
	 * If either the resource lock or resource set value commands are not
	 * supported - skip the the max values setting, release the lock if
	 * needed, and proceed to the queries. Other failures, including a
	 * failure to acquire the lock, will cause this function to fail.
	 */
	qed_mcp_resc_lock_default_init(&resc_lock_params, &resc_unlock_params,
				       QED_RESC_LOCK_RESC_ALLOC, false);

	rc = qed_mcp_resc_lock(p_hwfn, p_ptt, &resc_lock_params);
	if (rc && rc != -EINVAL) {
		return rc;
	} else if (rc == -EINVAL) {
		DP_INFO(p_hwfn,
			"Skip the max values setting of the soft resources since the resource lock is not supported by the MFW\n");
	} else if (!rc && !resc_lock_params.b_granted) {
		DP_NOTICE(p_hwfn,
			  "Failed to acquire the resource lock for the resource allocation commands\n");
		return -EBUSY;
	} else {
		rc = qed_hw_set_soft_resc_size(p_hwfn, p_ptt);
		if (rc && rc != -EINVAL) {
			DP_NOTICE(p_hwfn,
				  "Failed to set the max values of the soft resources\n");
			goto unlock_and_exit;
		} else if (rc == -EINVAL) {
			DP_INFO(p_hwfn,
				"Skip the max values setting of the soft resources since it is not supported by the MFW\n");
			rc = qed_mcp_resc_unlock(p_hwfn, p_ptt,
						 &resc_unlock_params);
			if (rc)
				DP_INFO(p_hwfn,
					"Failed to release the resource lock for the resource allocation commands\n");
		}
	}

	rc = qed_hw_set_resc_info(p_hwfn);
	if (rc)
		goto unlock_and_exit;

	if (resc_lock_params.b_granted && !resc_unlock_params.b_released) {
		rc = qed_mcp_resc_unlock(p_hwfn, p_ptt, &resc_unlock_params);
		if (rc)
			DP_INFO(p_hwfn,
				"Failed to release the resource lock for the resource allocation commands\n");
	}

	/* Sanity for ILT */
	if ((b_ah && (RESC_END(p_hwfn, QED_ILT) > PXP_NUM_ILT_RECORDS_K2)) ||
	    (!b_ah && (RESC_END(p_hwfn, QED_ILT) > PXP_NUM_ILT_RECORDS_BB))) {
		DP_NOTICE(p_hwfn, "Can't assign ILT pages [%08x,...,%08x]\n",
			  RESC_START(p_hwfn, QED_ILT),
			  RESC_END(p_hwfn, QED_ILT) - 1);
		return -EINVAL;
	}

	/* This will also learn the number of SBs from MFW */
	if (qed_int_igu_reset_cam(p_hwfn, p_ptt))
		return -EINVAL;

	qed_hw_set_feat(p_hwfn);

	for (res_id = 0; res_id < QED_MAX_RESC; res_id++)
		DP_VERBOSE(p_hwfn, NETIF_MSG_PROBE, "%s = %d start = %d\n",
			   qed_hw_get_resc_name(res_id),
			   RESC_NUM(p_hwfn, res_id),
			   RESC_START(p_hwfn, res_id));

	return 0;

unlock_and_exit:
	if (resc_lock_params.b_granted && !resc_unlock_params.b_released)
		qed_mcp_resc_unlock(p_hwfn, p_ptt, &resc_unlock_params);
	return rc;
}

static int qed_hw_get_nvm_info(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u32 port_cfg_addr, link_temp, nvm_cfg_addr, device_capabilities;
	u32 nvm_cfg1_offset, mf_mode, addr, generic_cont0, core_cfg;
	struct qed_mcp_link_params *link;

	/* Read global nvm_cfg address */
	nvm_cfg_addr = qed_rd(p_hwfn, p_ptt, MISC_REG_GEN_PURP_CR0);

	/* Verify MCP has initialized it */
	if (!nvm_cfg_addr) {
		DP_NOTICE(p_hwfn, "Shared memory not initialized\n");
		return -EINVAL;
	}

	/* Read nvm_cfg1  (Notice this is just offset, and not offsize (TBD) */
	nvm_cfg1_offset = qed_rd(p_hwfn, p_ptt, nvm_cfg_addr + 4);

	addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
	       offsetof(struct nvm_cfg1, glob) +
	       offsetof(struct nvm_cfg1_glob, core_cfg);

	core_cfg = qed_rd(p_hwfn, p_ptt, addr);

	switch ((core_cfg & NVM_CFG1_GLOB_NETWORK_PORT_MODE_MASK) >>
		NVM_CFG1_GLOB_NETWORK_PORT_MODE_OFFSET) {
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_2X40G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_2X40G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X50G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_2X50G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_1X100G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_1X100G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_4X10G_F:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_4X10G_F;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_4X10G_E:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_4X10G_E;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_BB_4X20G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_4X20G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_1X40G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_1X40G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X25G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_2X25G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_2X10G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_2X10G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_1X25G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_1X25G;
		break;
	case NVM_CFG1_GLOB_NETWORK_PORT_MODE_4X25G:
		p_hwfn->hw_info.port_mode = QED_PORT_MODE_DE_4X25G;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unknown port mode in 0x%08x\n", core_cfg);
		break;
	}

	/* Read default link configuration */
	link = &p_hwfn->mcp_info->link_input;
	port_cfg_addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
			offsetof(struct nvm_cfg1, port[MFW_PORT(p_hwfn)]);
	link_temp = qed_rd(p_hwfn, p_ptt,
			   port_cfg_addr +
			   offsetof(struct nvm_cfg1_port, speed_cap_mask));
	link_temp &= NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_MASK;
	link->speed.advertised_speeds = link_temp;

	link_temp = link->speed.advertised_speeds;
	p_hwfn->mcp_info->link_capabilities.speed_capabilities = link_temp;

	link_temp = qed_rd(p_hwfn, p_ptt,
			   port_cfg_addr +
			   offsetof(struct nvm_cfg1_port, link_settings));
	switch ((link_temp & NVM_CFG1_PORT_DRV_LINK_SPEED_MASK) >>
		NVM_CFG1_PORT_DRV_LINK_SPEED_OFFSET) {
	case NVM_CFG1_PORT_DRV_LINK_SPEED_AUTONEG:
		link->speed.autoneg = true;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_1G:
		link->speed.forced_speed = 1000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_10G:
		link->speed.forced_speed = 10000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_25G:
		link->speed.forced_speed = 25000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_40G:
		link->speed.forced_speed = 40000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_50G:
		link->speed.forced_speed = 50000;
		break;
	case NVM_CFG1_PORT_DRV_LINK_SPEED_BB_100G:
		link->speed.forced_speed = 100000;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unknown Speed in 0x%08x\n", link_temp);
	}

	p_hwfn->mcp_info->link_capabilities.default_speed_autoneg =
		link->speed.autoneg;

	link_temp &= NVM_CFG1_PORT_DRV_FLOW_CONTROL_MASK;
	link_temp >>= NVM_CFG1_PORT_DRV_FLOW_CONTROL_OFFSET;
	link->pause.autoneg = !!(link_temp &
				 NVM_CFG1_PORT_DRV_FLOW_CONTROL_AUTONEG);
	link->pause.forced_rx = !!(link_temp &
				   NVM_CFG1_PORT_DRV_FLOW_CONTROL_RX);
	link->pause.forced_tx = !!(link_temp &
				   NVM_CFG1_PORT_DRV_FLOW_CONTROL_TX);
	link->loopback_mode = 0;

	DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
		   "Read default link: Speed 0x%08x, Adv. Speed 0x%08x, AN: 0x%02x, PAUSE AN: 0x%02x\n",
		   link->speed.forced_speed, link->speed.advertised_speeds,
		   link->speed.autoneg, link->pause.autoneg);

	/* Read Multi-function information from shmem */
	addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
	       offsetof(struct nvm_cfg1, glob) +
	       offsetof(struct nvm_cfg1_glob, generic_cont0);

	generic_cont0 = qed_rd(p_hwfn, p_ptt, addr);

	mf_mode = (generic_cont0 & NVM_CFG1_GLOB_MF_MODE_MASK) >>
		  NVM_CFG1_GLOB_MF_MODE_OFFSET;

	switch (mf_mode) {
	case NVM_CFG1_GLOB_MF_MODE_MF_ALLOWED:
		p_hwfn->cdev->mf_mode = QED_MF_OVLAN;
		break;
	case NVM_CFG1_GLOB_MF_MODE_NPAR1_0:
		p_hwfn->cdev->mf_mode = QED_MF_NPAR;
		break;
	case NVM_CFG1_GLOB_MF_MODE_DEFAULT:
		p_hwfn->cdev->mf_mode = QED_MF_DEFAULT;
		break;
	}
	DP_INFO(p_hwfn, "Multi function mode is %08x\n",
		p_hwfn->cdev->mf_mode);

	/* Read Multi-function information from shmem */
	addr = MCP_REG_SCRATCH + nvm_cfg1_offset +
		offsetof(struct nvm_cfg1, glob) +
		offsetof(struct nvm_cfg1_glob, device_capabilities);

	device_capabilities = qed_rd(p_hwfn, p_ptt, addr);
	if (device_capabilities & NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ETHERNET)
		__set_bit(QED_DEV_CAP_ETH,
			  &p_hwfn->hw_info.device_capabilities);
	if (device_capabilities & NVM_CFG1_GLOB_DEVICE_CAPABILITIES_FCOE)
		__set_bit(QED_DEV_CAP_FCOE,
			  &p_hwfn->hw_info.device_capabilities);
	if (device_capabilities & NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ISCSI)
		__set_bit(QED_DEV_CAP_ISCSI,
			  &p_hwfn->hw_info.device_capabilities);
	if (device_capabilities & NVM_CFG1_GLOB_DEVICE_CAPABILITIES_ROCE)
		__set_bit(QED_DEV_CAP_ROCE,
			  &p_hwfn->hw_info.device_capabilities);

	return qed_mcp_fill_shmem_func_info(p_hwfn, p_ptt);
}

static void qed_get_num_funcs(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	u8 num_funcs, enabled_func_idx = p_hwfn->rel_pf_id;
	u32 reg_function_hide, tmp, eng_mask, low_pfs_mask;
	struct qed_dev *cdev = p_hwfn->cdev;

	num_funcs = QED_IS_AH(cdev) ? MAX_NUM_PFS_K2 : MAX_NUM_PFS_BB;

	/* Bit 0 of MISCS_REG_FUNCTION_HIDE indicates whether the bypass values
	 * in the other bits are selected.
	 * Bits 1-15 are for functions 1-15, respectively, and their value is
	 * '0' only for enabled functions (function 0 always exists and
	 * enabled).
	 * In case of CMT, only the "even" functions are enabled, and thus the
	 * number of functions for both hwfns is learnt from the same bits.
	 */
	reg_function_hide = qed_rd(p_hwfn, p_ptt, MISCS_REG_FUNCTION_HIDE);

	if (reg_function_hide & 0x1) {
		if (QED_IS_BB(cdev)) {
			if (QED_PATH_ID(p_hwfn) && cdev->num_hwfns == 1) {
				num_funcs = 0;
				eng_mask = 0xaaaa;
			} else {
				num_funcs = 1;
				eng_mask = 0x5554;
			}
		} else {
			num_funcs = 1;
			eng_mask = 0xfffe;
		}

		/* Get the number of the enabled functions on the engine */
		tmp = (reg_function_hide ^ 0xffffffff) & eng_mask;
		while (tmp) {
			if (tmp & 0x1)
				num_funcs++;
			tmp >>= 0x1;
		}

		/* Get the PF index within the enabled functions */
		low_pfs_mask = (0x1 << p_hwfn->abs_pf_id) - 1;
		tmp = reg_function_hide & eng_mask & low_pfs_mask;
		while (tmp) {
			if (tmp & 0x1)
				enabled_func_idx--;
			tmp >>= 0x1;
		}
	}

	p_hwfn->num_funcs_on_engine = num_funcs;
	p_hwfn->enabled_func_idx = enabled_func_idx;

	DP_VERBOSE(p_hwfn,
		   NETIF_MSG_PROBE,
		   "PF [rel_id %d, abs_id %d] occupies index %d within the %d enabled functions on the engine\n",
		   p_hwfn->rel_pf_id,
		   p_hwfn->abs_pf_id,
		   p_hwfn->enabled_func_idx, p_hwfn->num_funcs_on_engine);
}

static void qed_hw_info_port_num_bb(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt)
{
	u32 port_mode;

	port_mode = qed_rd(p_hwfn, p_ptt, CNIG_REG_NW_PORT_MODE_BB_B0);

	if (port_mode < 3) {
		p_hwfn->cdev->num_ports_in_engine = 1;
	} else if (port_mode <= 5) {
		p_hwfn->cdev->num_ports_in_engine = 2;
	} else {
		DP_NOTICE(p_hwfn, "PORT MODE: %d not supported\n",
			  p_hwfn->cdev->num_ports_in_engine);

		/* Default num_ports_in_engine to something */
		p_hwfn->cdev->num_ports_in_engine = 1;
	}
}

static void qed_hw_info_port_num_ah(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt)
{
	u32 port;
	int i;

	p_hwfn->cdev->num_ports_in_engine = 0;

	for (i = 0; i < MAX_NUM_PORTS_K2; i++) {
		port = qed_rd(p_hwfn, p_ptt,
			      CNIG_REG_NIG_PORT0_CONF_K2 + (i * 4));
		if (port & 1)
			p_hwfn->cdev->num_ports_in_engine++;
	}

	if (!p_hwfn->cdev->num_ports_in_engine) {
		DP_NOTICE(p_hwfn, "All NIG ports are inactive\n");

		/* Default num_ports_in_engine to something */
		p_hwfn->cdev->num_ports_in_engine = 1;
	}
}

static void qed_hw_info_port_num(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	if (QED_IS_BB(p_hwfn->cdev))
		qed_hw_info_port_num_bb(p_hwfn, p_ptt);
	else
		qed_hw_info_port_num_ah(p_hwfn, p_ptt);
}

static int
qed_get_hw_info(struct qed_hwfn *p_hwfn,
		struct qed_ptt *p_ptt,
		enum qed_pci_personality personality)
{
	int rc;

	/* Since all information is common, only first hwfns should do this */
	if (IS_LEAD_HWFN(p_hwfn)) {
		rc = qed_iov_hw_info(p_hwfn);
		if (rc)
			return rc;
	}

	qed_hw_info_port_num(p_hwfn, p_ptt);

	qed_hw_get_nvm_info(p_hwfn, p_ptt);

	rc = qed_int_igu_read_cam(p_hwfn, p_ptt);
	if (rc)
		return rc;

	if (qed_mcp_is_init(p_hwfn))
		ether_addr_copy(p_hwfn->hw_info.hw_mac_addr,
				p_hwfn->mcp_info->func_info.mac);
	else
		eth_random_addr(p_hwfn->hw_info.hw_mac_addr);

	if (qed_mcp_is_init(p_hwfn)) {
		if (p_hwfn->mcp_info->func_info.ovlan != QED_MCP_VLAN_UNSET)
			p_hwfn->hw_info.ovlan =
				p_hwfn->mcp_info->func_info.ovlan;

		qed_mcp_cmd_port_init(p_hwfn, p_ptt);
	}

	if (qed_mcp_is_init(p_hwfn)) {
		enum qed_pci_personality protocol;

		protocol = p_hwfn->mcp_info->func_info.protocol;
		p_hwfn->hw_info.personality = protocol;
	}

	p_hwfn->hw_info.num_hw_tc = NUM_PHYS_TCS_4PORT_K2;
	p_hwfn->hw_info.num_active_tc = 1;

	qed_get_num_funcs(p_hwfn, p_ptt);

	if (qed_mcp_is_init(p_hwfn))
		p_hwfn->hw_info.mtu = p_hwfn->mcp_info->func_info.mtu;

	return qed_hw_get_resc(p_hwfn, p_ptt);
}

static int qed_get_dev_info(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_dev *cdev = p_hwfn->cdev;
	u16 device_id_mask;
	u32 tmp;

	/* Read Vendor Id / Device Id */
	pci_read_config_word(cdev->pdev, PCI_VENDOR_ID, &cdev->vendor_id);
	pci_read_config_word(cdev->pdev, PCI_DEVICE_ID, &cdev->device_id);

	/* Determine type */
	device_id_mask = cdev->device_id & QED_DEV_ID_MASK;
	switch (device_id_mask) {
	case QED_DEV_ID_MASK_BB:
		cdev->type = QED_DEV_TYPE_BB;
		break;
	case QED_DEV_ID_MASK_AH:
		cdev->type = QED_DEV_TYPE_AH;
		break;
	default:
		DP_NOTICE(p_hwfn, "Unknown device id 0x%x\n", cdev->device_id);
		return -EBUSY;
	}

	cdev->chip_num = (u16)qed_rd(p_hwfn, p_ptt, MISCS_REG_CHIP_NUM);
	cdev->chip_rev = (u16)qed_rd(p_hwfn, p_ptt, MISCS_REG_CHIP_REV);

	MASK_FIELD(CHIP_REV, cdev->chip_rev);

	/* Learn number of HW-functions */
	tmp = qed_rd(p_hwfn, p_ptt, MISCS_REG_CMT_ENABLED_FOR_PAIR);

	if (tmp & (1 << p_hwfn->rel_pf_id)) {
		DP_NOTICE(cdev->hwfns, "device in CMT mode\n");
		cdev->num_hwfns = 2;
	} else {
		cdev->num_hwfns = 1;
	}

	cdev->chip_bond_id = qed_rd(p_hwfn, p_ptt,
				    MISCS_REG_CHIP_TEST_REG) >> 4;
	MASK_FIELD(CHIP_BOND_ID, cdev->chip_bond_id);
	cdev->chip_metal = (u16)qed_rd(p_hwfn, p_ptt, MISCS_REG_CHIP_METAL);
	MASK_FIELD(CHIP_METAL, cdev->chip_metal);

	DP_INFO(cdev->hwfns,
		"Chip details - %s %c%d, Num: %04x Rev: %04x Bond id: %04x Metal: %04x\n",
		QED_IS_BB(cdev) ? "BB" : "AH",
		'A' + cdev->chip_rev,
		(int)cdev->chip_metal,
		cdev->chip_num, cdev->chip_rev,
		cdev->chip_bond_id, cdev->chip_metal);

	return 0;
}

static int qed_hw_prepare_single(struct qed_hwfn *p_hwfn,
				 void __iomem *p_regview,
				 void __iomem *p_doorbells,
				 enum qed_pci_personality personality)
{
	int rc = 0;

	/* Split PCI bars evenly between hwfns */
	p_hwfn->regview = p_regview;
	p_hwfn->doorbells = p_doorbells;

	if (IS_VF(p_hwfn->cdev))
		return qed_vf_hw_prepare(p_hwfn);

	/* Validate that chip access is feasible */
	if (REG_RD(p_hwfn, PXP_PF_ME_OPAQUE_ADDR) == 0xffffffff) {
		DP_ERR(p_hwfn,
		       "Reading the ME register returns all Fs; Preventing further chip access\n");
		return -EINVAL;
	}

	get_function_id(p_hwfn);

	/* Allocate PTT pool */
	rc = qed_ptt_pool_alloc(p_hwfn);
	if (rc)
		goto err0;

	/* Allocate the main PTT */
	p_hwfn->p_main_ptt = qed_get_reserved_ptt(p_hwfn, RESERVED_PTT_MAIN);

	/* First hwfn learns basic information, e.g., number of hwfns */
	if (!p_hwfn->my_id) {
		rc = qed_get_dev_info(p_hwfn, p_hwfn->p_main_ptt);
		if (rc)
			goto err1;
	}

	qed_hw_hwfn_prepare(p_hwfn);

	/* Initialize MCP structure */
	rc = qed_mcp_cmd_init(p_hwfn, p_hwfn->p_main_ptt);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed initializing mcp command\n");
		goto err1;
	}

	/* Read the device configuration information from the HW and SHMEM */
	rc = qed_get_hw_info(p_hwfn, p_hwfn->p_main_ptt, personality);
	if (rc) {
		DP_NOTICE(p_hwfn, "Failed to get HW information\n");
		goto err2;
	}

	/* Sending a mailbox to the MFW should be done after qed_get_hw_info()
	 * is called as it sets the ports number in an engine.
	 */
	if (IS_LEAD_HWFN(p_hwfn)) {
		rc = qed_mcp_initiate_pf_flr(p_hwfn, p_hwfn->p_main_ptt);
		if (rc)
			DP_NOTICE(p_hwfn, "Failed to initiate PF FLR\n");
	}

	/* Allocate the init RT array and initialize the init-ops engine */
	rc = qed_init_alloc(p_hwfn);
	if (rc)
		goto err2;

	return rc;
err2:
	if (IS_LEAD_HWFN(p_hwfn))
		qed_iov_free_hw_info(p_hwfn->cdev);
	qed_mcp_free(p_hwfn);
err1:
	qed_hw_hwfn_free(p_hwfn);
err0:
	return rc;
}

int qed_hw_prepare(struct qed_dev *cdev,
		   int personality)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	int rc;

	/* Store the precompiled init data ptrs */
	if (IS_PF(cdev))
		qed_init_iro_array(cdev);

	/* Initialize the first hwfn - will learn number of hwfns */
	rc = qed_hw_prepare_single(p_hwfn,
				   cdev->regview,
				   cdev->doorbells, personality);
	if (rc)
		return rc;

	personality = p_hwfn->hw_info.personality;

	/* Initialize the rest of the hwfns */
	if (cdev->num_hwfns > 1) {
		void __iomem *p_regview, *p_doorbell;
		u8 __iomem *addr;

		/* adjust bar offset for second engine */
		addr = cdev->regview +
		       qed_hw_bar_size(p_hwfn, p_hwfn->p_main_ptt,
				       BAR_ID_0) / 2;
		p_regview = addr;

		addr = cdev->doorbells +
		       qed_hw_bar_size(p_hwfn, p_hwfn->p_main_ptt,
				       BAR_ID_1) / 2;
		p_doorbell = addr;

		/* prepare second hw function */
		rc = qed_hw_prepare_single(&cdev->hwfns[1], p_regview,
					   p_doorbell, personality);

		/* in case of error, need to free the previously
		 * initiliazed hwfn 0.
		 */
		if (rc) {
			if (IS_PF(cdev)) {
				qed_init_free(p_hwfn);
				qed_mcp_free(p_hwfn);
				qed_hw_hwfn_free(p_hwfn);
			}
		}
	}

	return rc;
}

void qed_hw_remove(struct qed_dev *cdev)
{
	struct qed_hwfn *p_hwfn = QED_LEADING_HWFN(cdev);
	int i;

	if (IS_PF(cdev))
		qed_mcp_ov_update_driver_state(p_hwfn, p_hwfn->p_main_ptt,
					       QED_OV_DRIVER_STATE_NOT_LOADED);

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		if (IS_VF(cdev)) {
			qed_vf_pf_release(p_hwfn);
			continue;
		}

		qed_init_free(p_hwfn);
		qed_hw_hwfn_free(p_hwfn);
		qed_mcp_free(p_hwfn);
	}

	qed_iov_free_hw_info(cdev);
}

static void qed_chain_free_next_ptr(struct qed_dev *cdev,
				    struct qed_chain *p_chain)
{
	void *p_virt = p_chain->p_virt_addr, *p_virt_next = NULL;
	dma_addr_t p_phys = p_chain->p_phys_addr, p_phys_next = 0;
	struct qed_chain_next *p_next;
	u32 size, i;

	if (!p_virt)
		return;

	size = p_chain->elem_size * p_chain->usable_per_page;

	for (i = 0; i < p_chain->page_cnt; i++) {
		if (!p_virt)
			break;

		p_next = (struct qed_chain_next *)((u8 *)p_virt + size);
		p_virt_next = p_next->next_virt;
		p_phys_next = HILO_DMA_REGPAIR(p_next->next_phys);

		dma_free_coherent(&cdev->pdev->dev,
				  QED_CHAIN_PAGE_SIZE, p_virt, p_phys);

		p_virt = p_virt_next;
		p_phys = p_phys_next;
	}
}

static void qed_chain_free_single(struct qed_dev *cdev,
				  struct qed_chain *p_chain)
{
	if (!p_chain->p_virt_addr)
		return;

	dma_free_coherent(&cdev->pdev->dev,
			  QED_CHAIN_PAGE_SIZE,
			  p_chain->p_virt_addr, p_chain->p_phys_addr);
}

static void qed_chain_free_pbl(struct qed_dev *cdev, struct qed_chain *p_chain)
{
	void **pp_virt_addr_tbl = p_chain->pbl.pp_virt_addr_tbl;
	u32 page_cnt = p_chain->page_cnt, i, pbl_size;
	u8 *p_pbl_virt = p_chain->pbl_sp.p_virt_table;

	if (!pp_virt_addr_tbl)
		return;

	if (!p_pbl_virt)
		goto out;

	for (i = 0; i < page_cnt; i++) {
		if (!pp_virt_addr_tbl[i])
			break;

		dma_free_coherent(&cdev->pdev->dev,
				  QED_CHAIN_PAGE_SIZE,
				  pp_virt_addr_tbl[i],
				  *(dma_addr_t *)p_pbl_virt);

		p_pbl_virt += QED_CHAIN_PBL_ENTRY_SIZE;
	}

	pbl_size = page_cnt * QED_CHAIN_PBL_ENTRY_SIZE;

	if (!p_chain->b_external_pbl)
		dma_free_coherent(&cdev->pdev->dev,
				  pbl_size,
				  p_chain->pbl_sp.p_virt_table,
				  p_chain->pbl_sp.p_phys_table);
out:
	vfree(p_chain->pbl.pp_virt_addr_tbl);
	p_chain->pbl.pp_virt_addr_tbl = NULL;
}

void qed_chain_free(struct qed_dev *cdev, struct qed_chain *p_chain)
{
	switch (p_chain->mode) {
	case QED_CHAIN_MODE_NEXT_PTR:
		qed_chain_free_next_ptr(cdev, p_chain);
		break;
	case QED_CHAIN_MODE_SINGLE:
		qed_chain_free_single(cdev, p_chain);
		break;
	case QED_CHAIN_MODE_PBL:
		qed_chain_free_pbl(cdev, p_chain);
		break;
	}
}

static int
qed_chain_alloc_sanity_check(struct qed_dev *cdev,
			     enum qed_chain_cnt_type cnt_type,
			     size_t elem_size, u32 page_cnt)
{
	u64 chain_size = ELEMS_PER_PAGE(elem_size) * page_cnt;

	/* The actual chain size can be larger than the maximal possible value
	 * after rounding up the requested elements number to pages, and after
	 * taking into acount the unusuable elements (next-ptr elements).
	 * The size of a "u16" chain can be (U16_MAX + 1) since the chain
	 * size/capacity fields are of a u32 type.
	 */
	if ((cnt_type == QED_CHAIN_CNT_TYPE_U16 &&
	     chain_size > ((u32)U16_MAX + 1)) ||
	    (cnt_type == QED_CHAIN_CNT_TYPE_U32 && chain_size > U32_MAX)) {
		DP_NOTICE(cdev,
			  "The actual chain size (0x%llx) is larger than the maximal possible value\n",
			  chain_size);
		return -EINVAL;
	}

	return 0;
}

static int
qed_chain_alloc_next_ptr(struct qed_dev *cdev, struct qed_chain *p_chain)
{
	void *p_virt = NULL, *p_virt_prev = NULL;
	dma_addr_t p_phys = 0;
	u32 i;

	for (i = 0; i < p_chain->page_cnt; i++) {
		p_virt = dma_alloc_coherent(&cdev->pdev->dev,
					    QED_CHAIN_PAGE_SIZE,
					    &p_phys, GFP_KERNEL);
		if (!p_virt)
			return -ENOMEM;

		if (i == 0) {
			qed_chain_init_mem(p_chain, p_virt, p_phys);
			qed_chain_reset(p_chain);
		} else {
			qed_chain_init_next_ptr_elem(p_chain, p_virt_prev,
						     p_virt, p_phys);
		}

		p_virt_prev = p_virt;
	}
	/* Last page's next element should point to the beginning of the
	 * chain.
	 */
	qed_chain_init_next_ptr_elem(p_chain, p_virt_prev,
				     p_chain->p_virt_addr,
				     p_chain->p_phys_addr);

	return 0;
}

static int
qed_chain_alloc_single(struct qed_dev *cdev, struct qed_chain *p_chain)
{
	dma_addr_t p_phys = 0;
	void *p_virt = NULL;

	p_virt = dma_alloc_coherent(&cdev->pdev->dev,
				    QED_CHAIN_PAGE_SIZE, &p_phys, GFP_KERNEL);
	if (!p_virt)
		return -ENOMEM;

	qed_chain_init_mem(p_chain, p_virt, p_phys);
	qed_chain_reset(p_chain);

	return 0;
}

static int
qed_chain_alloc_pbl(struct qed_dev *cdev,
		    struct qed_chain *p_chain,
		    struct qed_chain_ext_pbl *ext_pbl)
{
	u32 page_cnt = p_chain->page_cnt, size, i;
	dma_addr_t p_phys = 0, p_pbl_phys = 0;
	void **pp_virt_addr_tbl = NULL;
	u8 *p_pbl_virt = NULL;
	void *p_virt = NULL;

	size = page_cnt * sizeof(*pp_virt_addr_tbl);
	pp_virt_addr_tbl = vzalloc(size);
	if (!pp_virt_addr_tbl)
		return -ENOMEM;

	/* The allocation of the PBL table is done with its full size, since it
	 * is expected to be successive.
	 * qed_chain_init_pbl_mem() is called even in a case of an allocation
	 * failure, since pp_virt_addr_tbl was previously allocated, and it
	 * should be saved to allow its freeing during the error flow.
	 */
	size = page_cnt * QED_CHAIN_PBL_ENTRY_SIZE;

	if (!ext_pbl) {
		p_pbl_virt = dma_alloc_coherent(&cdev->pdev->dev,
						size, &p_pbl_phys, GFP_KERNEL);
	} else {
		p_pbl_virt = ext_pbl->p_pbl_virt;
		p_pbl_phys = ext_pbl->p_pbl_phys;
		p_chain->b_external_pbl = true;
	}

	qed_chain_init_pbl_mem(p_chain, p_pbl_virt, p_pbl_phys,
			       pp_virt_addr_tbl);
	if (!p_pbl_virt)
		return -ENOMEM;

	for (i = 0; i < page_cnt; i++) {
		p_virt = dma_alloc_coherent(&cdev->pdev->dev,
					    QED_CHAIN_PAGE_SIZE,
					    &p_phys, GFP_KERNEL);
		if (!p_virt)
			return -ENOMEM;

		if (i == 0) {
			qed_chain_init_mem(p_chain, p_virt, p_phys);
			qed_chain_reset(p_chain);
		}

		/* Fill the PBL table with the physical address of the page */
		*(dma_addr_t *)p_pbl_virt = p_phys;
		/* Keep the virtual address of the page */
		p_chain->pbl.pp_virt_addr_tbl[i] = p_virt;

		p_pbl_virt += QED_CHAIN_PBL_ENTRY_SIZE;
	}

	return 0;
}

int qed_chain_alloc(struct qed_dev *cdev,
		    enum qed_chain_use_mode intended_use,
		    enum qed_chain_mode mode,
		    enum qed_chain_cnt_type cnt_type,
		    u32 num_elems,
		    size_t elem_size,
		    struct qed_chain *p_chain,
		    struct qed_chain_ext_pbl *ext_pbl)
{
	u32 page_cnt;
	int rc = 0;

	if (mode == QED_CHAIN_MODE_SINGLE)
		page_cnt = 1;
	else
		page_cnt = QED_CHAIN_PAGE_CNT(num_elems, elem_size, mode);

	rc = qed_chain_alloc_sanity_check(cdev, cnt_type, elem_size, page_cnt);
	if (rc) {
		DP_NOTICE(cdev,
			  "Cannot allocate a chain with the given arguments:\n");
		DP_NOTICE(cdev,
			  "[use_mode %d, mode %d, cnt_type %d, num_elems %d, elem_size %zu]\n",
			  intended_use, mode, cnt_type, num_elems, elem_size);
		return rc;
	}

	qed_chain_init_params(p_chain, page_cnt, (u8) elem_size, intended_use,
			      mode, cnt_type);

	switch (mode) {
	case QED_CHAIN_MODE_NEXT_PTR:
		rc = qed_chain_alloc_next_ptr(cdev, p_chain);
		break;
	case QED_CHAIN_MODE_SINGLE:
		rc = qed_chain_alloc_single(cdev, p_chain);
		break;
	case QED_CHAIN_MODE_PBL:
		rc = qed_chain_alloc_pbl(cdev, p_chain, ext_pbl);
		break;
	}
	if (rc)
		goto nomem;

	return 0;

nomem:
	qed_chain_free(cdev, p_chain);
	return rc;
}

int qed_fw_l2_queue(struct qed_hwfn *p_hwfn, u16 src_id, u16 *dst_id)
{
	if (src_id >= RESC_NUM(p_hwfn, QED_L2_QUEUE)) {
		u16 min, max;

		min = (u16) RESC_START(p_hwfn, QED_L2_QUEUE);
		max = min + RESC_NUM(p_hwfn, QED_L2_QUEUE);
		DP_NOTICE(p_hwfn,
			  "l2_queue id [%d] is not valid, available indices [%d - %d]\n",
			  src_id, min, max);

		return -EINVAL;
	}

	*dst_id = RESC_START(p_hwfn, QED_L2_QUEUE) + src_id;

	return 0;
}

int qed_fw_vport(struct qed_hwfn *p_hwfn, u8 src_id, u8 *dst_id)
{
	if (src_id >= RESC_NUM(p_hwfn, QED_VPORT)) {
		u8 min, max;

		min = (u8)RESC_START(p_hwfn, QED_VPORT);
		max = min + RESC_NUM(p_hwfn, QED_VPORT);
		DP_NOTICE(p_hwfn,
			  "vport id [%d] is not valid, available indices [%d - %d]\n",
			  src_id, min, max);

		return -EINVAL;
	}

	*dst_id = RESC_START(p_hwfn, QED_VPORT) + src_id;

	return 0;
}

int qed_fw_rss_eng(struct qed_hwfn *p_hwfn, u8 src_id, u8 *dst_id)
{
	if (src_id >= RESC_NUM(p_hwfn, QED_RSS_ENG)) {
		u8 min, max;

		min = (u8)RESC_START(p_hwfn, QED_RSS_ENG);
		max = min + RESC_NUM(p_hwfn, QED_RSS_ENG);
		DP_NOTICE(p_hwfn,
			  "rss_eng id [%d] is not valid, available indices [%d - %d]\n",
			  src_id, min, max);

		return -EINVAL;
	}

	*dst_id = RESC_START(p_hwfn, QED_RSS_ENG) + src_id;

	return 0;
}

static void qed_llh_mac_to_filter(u32 *p_high, u32 *p_low,
				  u8 *p_filter)
{
	*p_high = p_filter[1] | (p_filter[0] << 8);
	*p_low = p_filter[5] | (p_filter[4] << 8) |
		 (p_filter[3] << 16) | (p_filter[2] << 24);
}

int qed_llh_add_mac_filter(struct qed_hwfn *p_hwfn,
			   struct qed_ptt *p_ptt, u8 *p_filter)
{
	u32 high = 0, low = 0, en;
	int i;

	if (!(IS_MF_SI(p_hwfn) || IS_MF_DEFAULT(p_hwfn)))
		return 0;

	qed_llh_mac_to_filter(&high, &low, p_filter);

	/* Find a free entry and utilize it */
	for (i = 0; i < NIG_REG_LLH_FUNC_FILTER_EN_SIZE; i++) {
		en = qed_rd(p_hwfn, p_ptt,
			    NIG_REG_LLH_FUNC_FILTER_EN + i * sizeof(u32));
		if (en)
			continue;
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_VALUE +
		       2 * i * sizeof(u32), low);
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_VALUE +
		       (2 * i + 1) * sizeof(u32), high);
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_MODE + i * sizeof(u32), 0);
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_PROTOCOL_TYPE +
		       i * sizeof(u32), 0);
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_EN + i * sizeof(u32), 1);
		break;
	}
	if (i >= NIG_REG_LLH_FUNC_FILTER_EN_SIZE) {
		DP_NOTICE(p_hwfn,
			  "Failed to find an empty LLH filter to utilize\n");
		return -EINVAL;
	}

	DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
		   "mac: %pM is added at %d\n",
		   p_filter, i);

	return 0;
}

void qed_llh_remove_mac_filter(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt, u8 *p_filter)
{
	u32 high = 0, low = 0;
	int i;

	if (!(IS_MF_SI(p_hwfn) || IS_MF_DEFAULT(p_hwfn)))
		return;

	qed_llh_mac_to_filter(&high, &low, p_filter);

	/* Find the entry and clean it */
	for (i = 0; i < NIG_REG_LLH_FUNC_FILTER_EN_SIZE; i++) {
		if (qed_rd(p_hwfn, p_ptt,
			   NIG_REG_LLH_FUNC_FILTER_VALUE +
			   2 * i * sizeof(u32)) != low)
			continue;
		if (qed_rd(p_hwfn, p_ptt,
			   NIG_REG_LLH_FUNC_FILTER_VALUE +
			   (2 * i + 1) * sizeof(u32)) != high)
			continue;

		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_EN + i * sizeof(u32), 0);
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_VALUE + 2 * i * sizeof(u32), 0);
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_VALUE +
		       (2 * i + 1) * sizeof(u32), 0);

		DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
			   "mac: %pM is removed from %d\n",
			   p_filter, i);
		break;
	}
	if (i >= NIG_REG_LLH_FUNC_FILTER_EN_SIZE)
		DP_NOTICE(p_hwfn, "Tried to remove a non-configured filter\n");
}

int
qed_llh_add_protocol_filter(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    u16 source_port_or_eth_type,
			    u16 dest_port, enum qed_llh_port_filter_type_t type)
{
	u32 high = 0, low = 0, en;
	int i;

	if (!(IS_MF_SI(p_hwfn) || IS_MF_DEFAULT(p_hwfn)))
		return 0;

	switch (type) {
	case QED_LLH_FILTER_ETHERTYPE:
		high = source_port_or_eth_type;
		break;
	case QED_LLH_FILTER_TCP_SRC_PORT:
	case QED_LLH_FILTER_UDP_SRC_PORT:
		low = source_port_or_eth_type << 16;
		break;
	case QED_LLH_FILTER_TCP_DEST_PORT:
	case QED_LLH_FILTER_UDP_DEST_PORT:
		low = dest_port;
		break;
	case QED_LLH_FILTER_TCP_SRC_AND_DEST_PORT:
	case QED_LLH_FILTER_UDP_SRC_AND_DEST_PORT:
		low = (source_port_or_eth_type << 16) | dest_port;
		break;
	default:
		DP_NOTICE(p_hwfn,
			  "Non valid LLH protocol filter type %d\n", type);
		return -EINVAL;
	}
	/* Find a free entry and utilize it */
	for (i = 0; i < NIG_REG_LLH_FUNC_FILTER_EN_SIZE; i++) {
		en = qed_rd(p_hwfn, p_ptt,
			    NIG_REG_LLH_FUNC_FILTER_EN + i * sizeof(u32));
		if (en)
			continue;
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_VALUE +
		       2 * i * sizeof(u32), low);
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_VALUE +
		       (2 * i + 1) * sizeof(u32), high);
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_MODE + i * sizeof(u32), 1);
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_PROTOCOL_TYPE +
		       i * sizeof(u32), 1 << type);
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_EN + i * sizeof(u32), 1);
		break;
	}
	if (i >= NIG_REG_LLH_FUNC_FILTER_EN_SIZE) {
		DP_NOTICE(p_hwfn,
			  "Failed to find an empty LLH filter to utilize\n");
		return -EINVAL;
	}
	switch (type) {
	case QED_LLH_FILTER_ETHERTYPE:
		DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
			   "ETH type %x is added at %d\n",
			   source_port_or_eth_type, i);
		break;
	case QED_LLH_FILTER_TCP_SRC_PORT:
		DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
			   "TCP src port %x is added at %d\n",
			   source_port_or_eth_type, i);
		break;
	case QED_LLH_FILTER_UDP_SRC_PORT:
		DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
			   "UDP src port %x is added at %d\n",
			   source_port_or_eth_type, i);
		break;
	case QED_LLH_FILTER_TCP_DEST_PORT:
		DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
			   "TCP dst port %x is added at %d\n", dest_port, i);
		break;
	case QED_LLH_FILTER_UDP_DEST_PORT:
		DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
			   "UDP dst port %x is added at %d\n", dest_port, i);
		break;
	case QED_LLH_FILTER_TCP_SRC_AND_DEST_PORT:
		DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
			   "TCP src/dst ports %x/%x are added at %d\n",
			   source_port_or_eth_type, dest_port, i);
		break;
	case QED_LLH_FILTER_UDP_SRC_AND_DEST_PORT:
		DP_VERBOSE(p_hwfn, NETIF_MSG_HW,
			   "UDP src/dst ports %x/%x are added at %d\n",
			   source_port_or_eth_type, dest_port, i);
		break;
	}
	return 0;
}

void
qed_llh_remove_protocol_filter(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt,
			       u16 source_port_or_eth_type,
			       u16 dest_port,
			       enum qed_llh_port_filter_type_t type)
{
	u32 high = 0, low = 0;
	int i;

	if (!(IS_MF_SI(p_hwfn) || IS_MF_DEFAULT(p_hwfn)))
		return;

	switch (type) {
	case QED_LLH_FILTER_ETHERTYPE:
		high = source_port_or_eth_type;
		break;
	case QED_LLH_FILTER_TCP_SRC_PORT:
	case QED_LLH_FILTER_UDP_SRC_PORT:
		low = source_port_or_eth_type << 16;
		break;
	case QED_LLH_FILTER_TCP_DEST_PORT:
	case QED_LLH_FILTER_UDP_DEST_PORT:
		low = dest_port;
		break;
	case QED_LLH_FILTER_TCP_SRC_AND_DEST_PORT:
	case QED_LLH_FILTER_UDP_SRC_AND_DEST_PORT:
		low = (source_port_or_eth_type << 16) | dest_port;
		break;
	default:
		DP_NOTICE(p_hwfn,
			  "Non valid LLH protocol filter type %d\n", type);
		return;
	}

	for (i = 0; i < NIG_REG_LLH_FUNC_FILTER_EN_SIZE; i++) {
		if (!qed_rd(p_hwfn, p_ptt,
			    NIG_REG_LLH_FUNC_FILTER_EN + i * sizeof(u32)))
			continue;
		if (!qed_rd(p_hwfn, p_ptt,
			    NIG_REG_LLH_FUNC_FILTER_MODE + i * sizeof(u32)))
			continue;
		if (!(qed_rd(p_hwfn, p_ptt,
			     NIG_REG_LLH_FUNC_FILTER_PROTOCOL_TYPE +
			     i * sizeof(u32)) & BIT(type)))
			continue;
		if (qed_rd(p_hwfn, p_ptt,
			   NIG_REG_LLH_FUNC_FILTER_VALUE +
			   2 * i * sizeof(u32)) != low)
			continue;
		if (qed_rd(p_hwfn, p_ptt,
			   NIG_REG_LLH_FUNC_FILTER_VALUE +
			   (2 * i + 1) * sizeof(u32)) != high)
			continue;

		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_EN + i * sizeof(u32), 0);
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_MODE + i * sizeof(u32), 0);
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_PROTOCOL_TYPE +
		       i * sizeof(u32), 0);
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_VALUE + 2 * i * sizeof(u32), 0);
		qed_wr(p_hwfn, p_ptt,
		       NIG_REG_LLH_FUNC_FILTER_VALUE +
		       (2 * i + 1) * sizeof(u32), 0);
		break;
	}

	if (i >= NIG_REG_LLH_FUNC_FILTER_EN_SIZE)
		DP_NOTICE(p_hwfn, "Tried to remove a non-configured filter\n");
}

static int qed_set_coalesce(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			    u32 hw_addr, void *p_eth_qzone,
			    size_t eth_qzone_size, u8 timeset)
{
	struct coalescing_timeset *p_coal_timeset;

	if (p_hwfn->cdev->int_coalescing_mode != QED_COAL_MODE_ENABLE) {
		DP_NOTICE(p_hwfn, "Coalescing configuration not enabled\n");
		return -EINVAL;
	}

	p_coal_timeset = p_eth_qzone;
	memset(p_coal_timeset, 0, eth_qzone_size);
	SET_FIELD(p_coal_timeset->value, COALESCING_TIMESET_TIMESET, timeset);
	SET_FIELD(p_coal_timeset->value, COALESCING_TIMESET_VALID, 1);
	qed_memcpy_to(p_hwfn, p_ptt, hw_addr, p_eth_qzone, eth_qzone_size);

	return 0;
}

int qed_set_rxq_coalesce(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			 u16 coalesce, u16 qid, u16 sb_id)
{
	struct ustorm_eth_queue_zone eth_qzone;
	u8 timeset, timer_res;
	u16 fw_qid = 0;
	u32 address;
	int rc;

	/* Coalesce = (timeset << timer-resolution), timeset is 7bit wide */
	if (coalesce <= 0x7F) {
		timer_res = 0;
	} else if (coalesce <= 0xFF) {
		timer_res = 1;
	} else if (coalesce <= 0x1FF) {
		timer_res = 2;
	} else {
		DP_ERR(p_hwfn, "Invalid coalesce value - %d\n", coalesce);
		return -EINVAL;
	}
	timeset = (u8)(coalesce >> timer_res);

	rc = qed_fw_l2_queue(p_hwfn, qid, &fw_qid);
	if (rc)
		return rc;

	rc = qed_int_set_timer_res(p_hwfn, p_ptt, timer_res, sb_id, false);
	if (rc)
		goto out;

	address = BAR0_MAP_REG_USDM_RAM + USTORM_ETH_QUEUE_ZONE_OFFSET(fw_qid);

	rc = qed_set_coalesce(p_hwfn, p_ptt, address, &eth_qzone,
			      sizeof(struct ustorm_eth_queue_zone), timeset);
	if (rc)
		goto out;

	p_hwfn->cdev->rx_coalesce_usecs = coalesce;
out:
	return rc;
}

int qed_set_txq_coalesce(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			 u16 coalesce, u16 qid, u16 sb_id)
{
	struct xstorm_eth_queue_zone eth_qzone;
	u8 timeset, timer_res;
	u16 fw_qid = 0;
	u32 address;
	int rc;

	/* Coalesce = (timeset << timer-resolution), timeset is 7bit wide */
	if (coalesce <= 0x7F) {
		timer_res = 0;
	} else if (coalesce <= 0xFF) {
		timer_res = 1;
	} else if (coalesce <= 0x1FF) {
		timer_res = 2;
	} else {
		DP_ERR(p_hwfn, "Invalid coalesce value - %d\n", coalesce);
		return -EINVAL;
	}
	timeset = (u8)(coalesce >> timer_res);

	rc = qed_fw_l2_queue(p_hwfn, qid, &fw_qid);
	if (rc)
		return rc;

	rc = qed_int_set_timer_res(p_hwfn, p_ptt, timer_res, sb_id, true);
	if (rc)
		goto out;

	address = BAR0_MAP_REG_XSDM_RAM + XSTORM_ETH_QUEUE_ZONE_OFFSET(fw_qid);

	rc = qed_set_coalesce(p_hwfn, p_ptt, address, &eth_qzone,
			      sizeof(struct xstorm_eth_queue_zone), timeset);
	if (rc)
		goto out;

	p_hwfn->cdev->tx_coalesce_usecs = coalesce;
out:
	return rc;
}

/* Calculate final WFQ values for all vports and configure them.
 * After this configuration each vport will have
 * approx min rate =  min_pf_rate * (vport_wfq / QED_WFQ_UNIT)
 */
static void qed_configure_wfq_for_all_vports(struct qed_hwfn *p_hwfn,
					     struct qed_ptt *p_ptt,
					     u32 min_pf_rate)
{
	struct init_qm_vport_params *vport_params;
	int i;

	vport_params = p_hwfn->qm_info.qm_vport_params;

	for (i = 0; i < p_hwfn->qm_info.num_vports; i++) {
		u32 wfq_speed = p_hwfn->qm_info.wfq_data[i].min_speed;

		vport_params[i].vport_wfq = (wfq_speed * QED_WFQ_UNIT) /
						min_pf_rate;
		qed_init_vport_wfq(p_hwfn, p_ptt,
				   vport_params[i].first_tx_pq_id,
				   vport_params[i].vport_wfq);
	}
}

static void qed_init_wfq_default_param(struct qed_hwfn *p_hwfn,
				       u32 min_pf_rate)

{
	int i;

	for (i = 0; i < p_hwfn->qm_info.num_vports; i++)
		p_hwfn->qm_info.qm_vport_params[i].vport_wfq = 1;
}

static void qed_disable_wfq_for_all_vports(struct qed_hwfn *p_hwfn,
					   struct qed_ptt *p_ptt,
					   u32 min_pf_rate)
{
	struct init_qm_vport_params *vport_params;
	int i;

	vport_params = p_hwfn->qm_info.qm_vport_params;

	for (i = 0; i < p_hwfn->qm_info.num_vports; i++) {
		qed_init_wfq_default_param(p_hwfn, min_pf_rate);
		qed_init_vport_wfq(p_hwfn, p_ptt,
				   vport_params[i].first_tx_pq_id,
				   vport_params[i].vport_wfq);
	}
}

/* This function performs several validations for WFQ
 * configuration and required min rate for a given vport
 * 1. req_rate must be greater than one percent of min_pf_rate.
 * 2. req_rate should not cause other vports [not configured for WFQ explicitly]
 *    rates to get less than one percent of min_pf_rate.
 * 3. total_req_min_rate [all vports min rate sum] shouldn't exceed min_pf_rate.
 */
static int qed_init_wfq_param(struct qed_hwfn *p_hwfn,
			      u16 vport_id, u32 req_rate, u32 min_pf_rate)
{
	u32 total_req_min_rate = 0, total_left_rate = 0, left_rate_per_vp = 0;
	int non_requested_count = 0, req_count = 0, i, num_vports;

	num_vports = p_hwfn->qm_info.num_vports;

	/* Accounting for the vports which are configured for WFQ explicitly */
	for (i = 0; i < num_vports; i++) {
		u32 tmp_speed;

		if ((i != vport_id) &&
		    p_hwfn->qm_info.wfq_data[i].configured) {
			req_count++;
			tmp_speed = p_hwfn->qm_info.wfq_data[i].min_speed;
			total_req_min_rate += tmp_speed;
		}
	}

	/* Include current vport data as well */
	req_count++;
	total_req_min_rate += req_rate;
	non_requested_count = num_vports - req_count;

	if (req_rate < min_pf_rate / QED_WFQ_UNIT) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Vport [%d] - Requested rate[%d Mbps] is less than one percent of configured PF min rate[%d Mbps]\n",
			   vport_id, req_rate, min_pf_rate);
		return -EINVAL;
	}

	if (num_vports > QED_WFQ_UNIT) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Number of vports is greater than %d\n",
			   QED_WFQ_UNIT);
		return -EINVAL;
	}

	if (total_req_min_rate > min_pf_rate) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Total requested min rate for all vports[%d Mbps] is greater than configured PF min rate[%d Mbps]\n",
			   total_req_min_rate, min_pf_rate);
		return -EINVAL;
	}

	total_left_rate	= min_pf_rate - total_req_min_rate;

	left_rate_per_vp = total_left_rate / non_requested_count;
	if (left_rate_per_vp <  min_pf_rate / QED_WFQ_UNIT) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
			   "Non WFQ configured vports rate [%d Mbps] is less than one percent of configured PF min rate[%d Mbps]\n",
			   left_rate_per_vp, min_pf_rate);
		return -EINVAL;
	}

	p_hwfn->qm_info.wfq_data[vport_id].min_speed = req_rate;
	p_hwfn->qm_info.wfq_data[vport_id].configured = true;

	for (i = 0; i < num_vports; i++) {
		if (p_hwfn->qm_info.wfq_data[i].configured)
			continue;

		p_hwfn->qm_info.wfq_data[i].min_speed = left_rate_per_vp;
	}

	return 0;
}

static int __qed_configure_vport_wfq(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt, u16 vp_id, u32 rate)
{
	struct qed_mcp_link_state *p_link;
	int rc = 0;

	p_link = &p_hwfn->cdev->hwfns[0].mcp_info->link_output;

	if (!p_link->min_pf_rate) {
		p_hwfn->qm_info.wfq_data[vp_id].min_speed = rate;
		p_hwfn->qm_info.wfq_data[vp_id].configured = true;
		return rc;
	}

	rc = qed_init_wfq_param(p_hwfn, vp_id, rate, p_link->min_pf_rate);

	if (!rc)
		qed_configure_wfq_for_all_vports(p_hwfn, p_ptt,
						 p_link->min_pf_rate);
	else
		DP_NOTICE(p_hwfn,
			  "Validation failed while configuring min rate\n");

	return rc;
}

static int __qed_configure_vp_wfq_on_link_change(struct qed_hwfn *p_hwfn,
						 struct qed_ptt *p_ptt,
						 u32 min_pf_rate)
{
	bool use_wfq = false;
	int rc = 0;
	u16 i;

	/* Validate all pre configured vports for wfq */
	for (i = 0; i < p_hwfn->qm_info.num_vports; i++) {
		u32 rate;

		if (!p_hwfn->qm_info.wfq_data[i].configured)
			continue;

		rate = p_hwfn->qm_info.wfq_data[i].min_speed;
		use_wfq = true;

		rc = qed_init_wfq_param(p_hwfn, i, rate, min_pf_rate);
		if (rc) {
			DP_NOTICE(p_hwfn,
				  "WFQ validation failed while configuring min rate\n");
			break;
		}
	}

	if (!rc && use_wfq)
		qed_configure_wfq_for_all_vports(p_hwfn, p_ptt, min_pf_rate);
	else
		qed_disable_wfq_for_all_vports(p_hwfn, p_ptt, min_pf_rate);

	return rc;
}

/* Main API for qed clients to configure vport min rate.
 * vp_id - vport id in PF Range[0 - (total_num_vports_per_pf - 1)]
 * rate - Speed in Mbps needs to be assigned to a given vport.
 */
int qed_configure_vport_wfq(struct qed_dev *cdev, u16 vp_id, u32 rate)
{
	int i, rc = -EINVAL;

	/* Currently not supported; Might change in future */
	if (cdev->num_hwfns > 1) {
		DP_NOTICE(cdev,
			  "WFQ configuration is not supported for this device\n");
		return rc;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
		struct qed_ptt *p_ptt;

		p_ptt = qed_ptt_acquire(p_hwfn);
		if (!p_ptt)
			return -EBUSY;

		rc = __qed_configure_vport_wfq(p_hwfn, p_ptt, vp_id, rate);

		if (rc) {
			qed_ptt_release(p_hwfn, p_ptt);
			return rc;
		}

		qed_ptt_release(p_hwfn, p_ptt);
	}

	return rc;
}

/* API to configure WFQ from mcp link change */
void qed_configure_vp_wfq_on_link_change(struct qed_dev *cdev,
					 struct qed_ptt *p_ptt, u32 min_pf_rate)
{
	int i;

	if (cdev->num_hwfns > 1) {
		DP_VERBOSE(cdev,
			   NETIF_MSG_LINK,
			   "WFQ configuration is not supported for this device\n");
		return;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		__qed_configure_vp_wfq_on_link_change(p_hwfn, p_ptt,
						      min_pf_rate);
	}
}

int __qed_configure_pf_max_bandwidth(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct qed_mcp_link_state *p_link,
				     u8 max_bw)
{
	int rc = 0;

	p_hwfn->mcp_info->func_info.bandwidth_max = max_bw;

	if (!p_link->line_speed && (max_bw != 100))
		return rc;

	p_link->speed = (p_link->line_speed * max_bw) / 100;
	p_hwfn->qm_info.pf_rl = p_link->speed;

	/* Since the limiter also affects Tx-switched traffic, we don't want it
	 * to limit such traffic in case there's no actual limit.
	 * In that case, set limit to imaginary high boundary.
	 */
	if (max_bw == 100)
		p_hwfn->qm_info.pf_rl = 100000;

	rc = qed_init_pf_rl(p_hwfn, p_ptt, p_hwfn->rel_pf_id,
			    p_hwfn->qm_info.pf_rl);

	DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
		   "Configured MAX bandwidth to be %08x Mb/sec\n",
		   p_link->speed);

	return rc;
}

/* Main API to configure PF max bandwidth where bw range is [1 - 100] */
int qed_configure_pf_max_bandwidth(struct qed_dev *cdev, u8 max_bw)
{
	int i, rc = -EINVAL;

	if (max_bw < 1 || max_bw > 100) {
		DP_NOTICE(cdev, "PF max bw valid range is [1-100]\n");
		return rc;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn	*p_hwfn = &cdev->hwfns[i];
		struct qed_hwfn *p_lead = QED_LEADING_HWFN(cdev);
		struct qed_mcp_link_state *p_link;
		struct qed_ptt *p_ptt;

		p_link = &p_lead->mcp_info->link_output;

		p_ptt = qed_ptt_acquire(p_hwfn);
		if (!p_ptt)
			return -EBUSY;

		rc = __qed_configure_pf_max_bandwidth(p_hwfn, p_ptt,
						      p_link, max_bw);

		qed_ptt_release(p_hwfn, p_ptt);

		if (rc)
			break;
	}

	return rc;
}

int __qed_configure_pf_min_bandwidth(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct qed_mcp_link_state *p_link,
				     u8 min_bw)
{
	int rc = 0;

	p_hwfn->mcp_info->func_info.bandwidth_min = min_bw;
	p_hwfn->qm_info.pf_wfq = min_bw;

	if (!p_link->line_speed)
		return rc;

	p_link->min_pf_rate = (p_link->line_speed * min_bw) / 100;

	rc = qed_init_pf_wfq(p_hwfn, p_ptt, p_hwfn->rel_pf_id, min_bw);

	DP_VERBOSE(p_hwfn, NETIF_MSG_LINK,
		   "Configured MIN bandwidth to be %d Mb/sec\n",
		   p_link->min_pf_rate);

	return rc;
}

/* Main API to configure PF min bandwidth where bw range is [1-100] */
int qed_configure_pf_min_bandwidth(struct qed_dev *cdev, u8 min_bw)
{
	int i, rc = -EINVAL;

	if (min_bw < 1 || min_bw > 100) {
		DP_NOTICE(cdev, "PF min bw valid range is [1-100]\n");
		return rc;
	}

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];
		struct qed_hwfn *p_lead = QED_LEADING_HWFN(cdev);
		struct qed_mcp_link_state *p_link;
		struct qed_ptt *p_ptt;

		p_link = &p_lead->mcp_info->link_output;

		p_ptt = qed_ptt_acquire(p_hwfn);
		if (!p_ptt)
			return -EBUSY;

		rc = __qed_configure_pf_min_bandwidth(p_hwfn, p_ptt,
						      p_link, min_bw);
		if (rc) {
			qed_ptt_release(p_hwfn, p_ptt);
			return rc;
		}

		if (p_link->min_pf_rate) {
			u32 min_rate = p_link->min_pf_rate;

			rc = __qed_configure_vp_wfq_on_link_change(p_hwfn,
								   p_ptt,
								   min_rate);
		}

		qed_ptt_release(p_hwfn, p_ptt);
	}

	return rc;
}

void qed_clean_wfq_db(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt)
{
	struct qed_mcp_link_state *p_link;

	p_link = &p_hwfn->mcp_info->link_output;

	if (p_link->min_pf_rate)
		qed_disable_wfq_for_all_vports(p_hwfn, p_ptt,
					       p_link->min_pf_rate);

	memset(p_hwfn->qm_info.wfq_data, 0,
	       sizeof(*p_hwfn->qm_info.wfq_data) * p_hwfn->qm_info.num_vports);
}

int qed_device_num_engines(struct qed_dev *cdev)
{
	return QED_IS_BB(cdev) ? 2 : 1;
}

static int qed_device_num_ports(struct qed_dev *cdev)
{
	/* in CMT always only one port */
	if (cdev->num_hwfns > 1)
		return 1;

	return cdev->num_ports_in_engine * qed_device_num_engines(cdev);
}

int qed_device_get_port_id(struct qed_dev *cdev)
{
	return (QED_LEADING_HWFN(cdev)->abs_pf_id) % qed_device_num_ports(cdev);
}

void qed_set_fw_mac_addr(__le16 *fw_msb,
			 __le16 *fw_mid, __le16 *fw_lsb, u8 *mac)
{
	((u8 *)fw_msb)[0] = mac[1];
	((u8 *)fw_msb)[1] = mac[0];
	((u8 *)fw_mid)[0] = mac[3];
	((u8 *)fw_mid)[1] = mac[2];
	((u8 *)fw_lsb)[0] = mac[5];
	((u8 *)fw_lsb)[1] = mac[4];
}
