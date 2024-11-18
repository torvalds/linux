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
 * Description: IB Verbs interpreter
 */

#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <net/addrconf.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_mad.h>
#include <rdma/ib_cache.h>
#include <rdma/uverbs_ioctl.h>
#include <linux/hashtable.h>

#include "bnxt_ulp.h"

#include "roce_hsi.h"
#include "qplib_res.h"
#include "qplib_sp.h"
#include "qplib_fp.h"
#include "qplib_rcfw.h"

#include "bnxt_re.h"
#include "ib_verbs.h"

#include <rdma/uverbs_types.h>
#include <rdma/uverbs_std_types.h>

#include <rdma/ib_user_ioctl_cmds.h>

#define UVERBS_MODULE_NAME bnxt_re
#include <rdma/uverbs_named_ioctl.h>

#include <rdma/bnxt_re-abi.h>

static int __from_ib_access_flags(int iflags)
{
	int qflags = 0;

	if (iflags & IB_ACCESS_LOCAL_WRITE)
		qflags |= BNXT_QPLIB_ACCESS_LOCAL_WRITE;
	if (iflags & IB_ACCESS_REMOTE_READ)
		qflags |= BNXT_QPLIB_ACCESS_REMOTE_READ;
	if (iflags & IB_ACCESS_REMOTE_WRITE)
		qflags |= BNXT_QPLIB_ACCESS_REMOTE_WRITE;
	if (iflags & IB_ACCESS_REMOTE_ATOMIC)
		qflags |= BNXT_QPLIB_ACCESS_REMOTE_ATOMIC;
	if (iflags & IB_ACCESS_MW_BIND)
		qflags |= BNXT_QPLIB_ACCESS_MW_BIND;
	if (iflags & IB_ZERO_BASED)
		qflags |= BNXT_QPLIB_ACCESS_ZERO_BASED;
	if (iflags & IB_ACCESS_ON_DEMAND)
		qflags |= BNXT_QPLIB_ACCESS_ON_DEMAND;
	return qflags;
};

static enum ib_access_flags __to_ib_access_flags(int qflags)
{
	enum ib_access_flags iflags = 0;

	if (qflags & BNXT_QPLIB_ACCESS_LOCAL_WRITE)
		iflags |= IB_ACCESS_LOCAL_WRITE;
	if (qflags & BNXT_QPLIB_ACCESS_REMOTE_WRITE)
		iflags |= IB_ACCESS_REMOTE_WRITE;
	if (qflags & BNXT_QPLIB_ACCESS_REMOTE_READ)
		iflags |= IB_ACCESS_REMOTE_READ;
	if (qflags & BNXT_QPLIB_ACCESS_REMOTE_ATOMIC)
		iflags |= IB_ACCESS_REMOTE_ATOMIC;
	if (qflags & BNXT_QPLIB_ACCESS_MW_BIND)
		iflags |= IB_ACCESS_MW_BIND;
	if (qflags & BNXT_QPLIB_ACCESS_ZERO_BASED)
		iflags |= IB_ZERO_BASED;
	if (qflags & BNXT_QPLIB_ACCESS_ON_DEMAND)
		iflags |= IB_ACCESS_ON_DEMAND;
	return iflags;
};

static void bnxt_re_check_and_set_relaxed_ordering(struct bnxt_re_dev *rdev,
						   struct bnxt_qplib_mrw *qplib_mr)
{
	if (_is_relaxed_ordering_supported(rdev->dev_attr.dev_cap_flags2) &&
	    pcie_relaxed_ordering_enabled(rdev->en_dev->pdev))
		qplib_mr->flags |= CMDQ_REGISTER_MR_FLAGS_ENABLE_RO;
}

static int bnxt_re_build_sgl(struct ib_sge *ib_sg_list,
			     struct bnxt_qplib_sge *sg_list, int num)
{
	int i, total = 0;

	for (i = 0; i < num; i++) {
		sg_list[i].addr = ib_sg_list[i].addr;
		sg_list[i].lkey = ib_sg_list[i].lkey;
		sg_list[i].size = ib_sg_list[i].length;
		total += sg_list[i].size;
	}
	return total;
}

/* Device */
int bnxt_re_query_device(struct ib_device *ibdev,
			 struct ib_device_attr *ib_attr,
			 struct ib_udata *udata)
{
	struct bnxt_re_dev *rdev = to_bnxt_re_dev(ibdev, ibdev);
	struct bnxt_qplib_dev_attr *dev_attr = &rdev->dev_attr;

	memset(ib_attr, 0, sizeof(*ib_attr));
	memcpy(&ib_attr->fw_ver, dev_attr->fw_ver,
	       min(sizeof(dev_attr->fw_ver),
		   sizeof(ib_attr->fw_ver)));
	addrconf_addr_eui48((u8 *)&ib_attr->sys_image_guid,
			    rdev->netdev->dev_addr);
	ib_attr->max_mr_size = BNXT_RE_MAX_MR_SIZE;
	ib_attr->page_size_cap = BNXT_RE_PAGE_SIZE_SUPPORTED;

	ib_attr->vendor_id = rdev->en_dev->pdev->vendor;
	ib_attr->vendor_part_id = rdev->en_dev->pdev->device;
	ib_attr->hw_ver = rdev->en_dev->pdev->subsystem_device;
	ib_attr->max_qp = dev_attr->max_qp;
	ib_attr->max_qp_wr = dev_attr->max_qp_wqes;
	ib_attr->device_cap_flags =
				    IB_DEVICE_CURR_QP_STATE_MOD
				    | IB_DEVICE_RC_RNR_NAK_GEN
				    | IB_DEVICE_SHUTDOWN_PORT
				    | IB_DEVICE_SYS_IMAGE_GUID
				    | IB_DEVICE_RESIZE_MAX_WR
				    | IB_DEVICE_PORT_ACTIVE_EVENT
				    | IB_DEVICE_N_NOTIFY_CQ
				    | IB_DEVICE_MEM_WINDOW
				    | IB_DEVICE_MEM_WINDOW_TYPE_2B
				    | IB_DEVICE_MEM_MGT_EXTENSIONS;
	ib_attr->kernel_cap_flags = IBK_LOCAL_DMA_LKEY;
	ib_attr->max_send_sge = dev_attr->max_qp_sges;
	ib_attr->max_recv_sge = dev_attr->max_qp_sges;
	ib_attr->max_sge_rd = dev_attr->max_qp_sges;
	ib_attr->max_cq = dev_attr->max_cq;
	ib_attr->max_cqe = dev_attr->max_cq_wqes;
	ib_attr->max_mr = dev_attr->max_mr;
	ib_attr->max_pd = dev_attr->max_pd;
	ib_attr->max_qp_rd_atom = dev_attr->max_qp_rd_atom;
	ib_attr->max_qp_init_rd_atom = dev_attr->max_qp_init_rd_atom;
	ib_attr->atomic_cap = IB_ATOMIC_NONE;
	ib_attr->masked_atomic_cap = IB_ATOMIC_NONE;
	if (dev_attr->is_atomic) {
		ib_attr->atomic_cap = IB_ATOMIC_GLOB;
		ib_attr->masked_atomic_cap = IB_ATOMIC_GLOB;
	}

	ib_attr->max_ee_rd_atom = 0;
	ib_attr->max_res_rd_atom = 0;
	ib_attr->max_ee_init_rd_atom = 0;
	ib_attr->max_ee = 0;
	ib_attr->max_rdd = 0;
	ib_attr->max_mw = dev_attr->max_mw;
	ib_attr->max_raw_ipv6_qp = 0;
	ib_attr->max_raw_ethy_qp = dev_attr->max_raw_ethy_qp;
	ib_attr->max_mcast_grp = 0;
	ib_attr->max_mcast_qp_attach = 0;
	ib_attr->max_total_mcast_qp_attach = 0;
	ib_attr->max_ah = dev_attr->max_ah;

	ib_attr->max_srq = dev_attr->max_srq;
	ib_attr->max_srq_wr = dev_attr->max_srq_wqes;
	ib_attr->max_srq_sge = dev_attr->max_srq_sges;

	ib_attr->max_fast_reg_page_list_len = MAX_PBL_LVL_1_PGS;

	ib_attr->max_pkeys = 1;
	ib_attr->local_ca_ack_delay = BNXT_RE_DEFAULT_ACK_DELAY;
	return 0;
}

/* Port */
int bnxt_re_query_port(struct ib_device *ibdev, u32 port_num,
		       struct ib_port_attr *port_attr)
{
	struct bnxt_re_dev *rdev = to_bnxt_re_dev(ibdev, ibdev);
	struct bnxt_qplib_dev_attr *dev_attr = &rdev->dev_attr;
	int rc;

	memset(port_attr, 0, sizeof(*port_attr));

	if (netif_running(rdev->netdev) && netif_carrier_ok(rdev->netdev)) {
		port_attr->state = IB_PORT_ACTIVE;
		port_attr->phys_state = IB_PORT_PHYS_STATE_LINK_UP;
	} else {
		port_attr->state = IB_PORT_DOWN;
		port_attr->phys_state = IB_PORT_PHYS_STATE_DISABLED;
	}
	port_attr->max_mtu = IB_MTU_4096;
	port_attr->active_mtu = iboe_get_mtu(rdev->netdev->mtu);
	port_attr->gid_tbl_len = dev_attr->max_sgid;
	port_attr->port_cap_flags = IB_PORT_CM_SUP | IB_PORT_REINIT_SUP |
				    IB_PORT_DEVICE_MGMT_SUP |
				    IB_PORT_VENDOR_CLASS_SUP;
	port_attr->ip_gids = true;

	port_attr->max_msg_sz = (u32)BNXT_RE_MAX_MR_SIZE_LOW;
	port_attr->bad_pkey_cntr = 0;
	port_attr->qkey_viol_cntr = 0;
	port_attr->pkey_tbl_len = dev_attr->max_pkey;
	port_attr->lid = 0;
	port_attr->sm_lid = 0;
	port_attr->lmc = 0;
	port_attr->max_vl_num = 4;
	port_attr->sm_sl = 0;
	port_attr->subnet_timeout = 0;
	port_attr->init_type_reply = 0;
	rc = ib_get_eth_speed(&rdev->ibdev, port_num, &port_attr->active_speed,
			      &port_attr->active_width);

	return rc;
}

int bnxt_re_get_port_immutable(struct ib_device *ibdev, u32 port_num,
			       struct ib_port_immutable *immutable)
{
	struct ib_port_attr port_attr;

	if (bnxt_re_query_port(ibdev, port_num, &port_attr))
		return -EINVAL;

	immutable->pkey_tbl_len = port_attr.pkey_tbl_len;
	immutable->gid_tbl_len = port_attr.gid_tbl_len;
	immutable->core_cap_flags = RDMA_CORE_PORT_IBA_ROCE;
	immutable->core_cap_flags |= RDMA_CORE_CAP_PROT_ROCE_UDP_ENCAP;
	immutable->max_mad_size = IB_MGMT_MAD_SIZE;
	return 0;
}

void bnxt_re_query_fw_str(struct ib_device *ibdev, char *str)
{
	struct bnxt_re_dev *rdev = to_bnxt_re_dev(ibdev, ibdev);

	snprintf(str, IB_FW_VERSION_NAME_MAX, "%d.%d.%d.%d",
		 rdev->dev_attr.fw_ver[0], rdev->dev_attr.fw_ver[1],
		 rdev->dev_attr.fw_ver[2], rdev->dev_attr.fw_ver[3]);
}

int bnxt_re_query_pkey(struct ib_device *ibdev, u32 port_num,
		       u16 index, u16 *pkey)
{
	if (index > 0)
		return -EINVAL;

	*pkey = IB_DEFAULT_PKEY_FULL;

	return 0;
}

int bnxt_re_query_gid(struct ib_device *ibdev, u32 port_num,
		      int index, union ib_gid *gid)
{
	struct bnxt_re_dev *rdev = to_bnxt_re_dev(ibdev, ibdev);
	int rc;

	/* Ignore port_num */
	memset(gid, 0, sizeof(*gid));
	rc = bnxt_qplib_get_sgid(&rdev->qplib_res,
				 &rdev->qplib_res.sgid_tbl, index,
				 (struct bnxt_qplib_gid *)gid);
	return rc;
}

int bnxt_re_del_gid(const struct ib_gid_attr *attr, void **context)
{
	int rc = 0;
	struct bnxt_re_gid_ctx *ctx, **ctx_tbl;
	struct bnxt_re_dev *rdev = to_bnxt_re_dev(attr->device, ibdev);
	struct bnxt_qplib_sgid_tbl *sgid_tbl = &rdev->qplib_res.sgid_tbl;
	struct bnxt_qplib_gid *gid_to_del;
	u16 vlan_id = 0xFFFF;

	/* Delete the entry from the hardware */
	ctx = *context;
	if (!ctx)
		return -EINVAL;

	if (sgid_tbl && sgid_tbl->active) {
		if (ctx->idx >= sgid_tbl->max)
			return -EINVAL;
		gid_to_del = &sgid_tbl->tbl[ctx->idx].gid;
		vlan_id = sgid_tbl->tbl[ctx->idx].vlan_id;
		/* DEL_GID is called in WQ context(netdevice_event_work_handler)
		 * or via the ib_unregister_device path. In the former case QP1
		 * may not be destroyed yet, in which case just return as FW
		 * needs that entry to be present and will fail it's deletion.
		 * We could get invoked again after QP1 is destroyed OR get an
		 * ADD_GID call with a different GID value for the same index
		 * where we issue MODIFY_GID cmd to update the GID entry -- TBD
		 */
		if (ctx->idx == 0 &&
		    rdma_link_local_addr((struct in6_addr *)gid_to_del) &&
		    ctx->refcnt == 1 && rdev->gsi_ctx.gsi_sqp) {
			ibdev_dbg(&rdev->ibdev,
				  "Trying to delete GID0 while QP1 is alive\n");
			return -EFAULT;
		}
		ctx->refcnt--;
		if (!ctx->refcnt) {
			rc = bnxt_qplib_del_sgid(sgid_tbl, gid_to_del,
						 vlan_id,  true);
			if (rc) {
				ibdev_err(&rdev->ibdev,
					  "Failed to remove GID: %#x", rc);
			} else {
				ctx_tbl = sgid_tbl->ctx;
				ctx_tbl[ctx->idx] = NULL;
				kfree(ctx);
			}
		}
	} else {
		return -EINVAL;
	}
	return rc;
}

int bnxt_re_add_gid(const struct ib_gid_attr *attr, void **context)
{
	int rc;
	u32 tbl_idx = 0;
	u16 vlan_id = 0xFFFF;
	struct bnxt_re_gid_ctx *ctx, **ctx_tbl;
	struct bnxt_re_dev *rdev = to_bnxt_re_dev(attr->device, ibdev);
	struct bnxt_qplib_sgid_tbl *sgid_tbl = &rdev->qplib_res.sgid_tbl;

	rc = rdma_read_gid_l2_fields(attr, &vlan_id, NULL);
	if (rc)
		return rc;

	rc = bnxt_qplib_add_sgid(sgid_tbl, (struct bnxt_qplib_gid *)&attr->gid,
				 rdev->qplib_res.netdev->dev_addr,
				 vlan_id, true, &tbl_idx);
	if (rc == -EALREADY) {
		ctx_tbl = sgid_tbl->ctx;
		ctx_tbl[tbl_idx]->refcnt++;
		*context = ctx_tbl[tbl_idx];
		return 0;
	}

	if (rc < 0) {
		ibdev_err(&rdev->ibdev, "Failed to add GID: %#x", rc);
		return rc;
	}

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	ctx_tbl = sgid_tbl->ctx;
	ctx->idx = tbl_idx;
	ctx->refcnt = 1;
	ctx_tbl[tbl_idx] = ctx;
	*context = ctx;

	return rc;
}

enum rdma_link_layer bnxt_re_get_link_layer(struct ib_device *ibdev,
					    u32 port_num)
{
	return IB_LINK_LAYER_ETHERNET;
}

#define	BNXT_RE_FENCE_PBL_SIZE	DIV_ROUND_UP(BNXT_RE_FENCE_BYTES, PAGE_SIZE)

static void bnxt_re_create_fence_wqe(struct bnxt_re_pd *pd)
{
	struct bnxt_re_fence_data *fence = &pd->fence;
	struct ib_mr *ib_mr = &fence->mr->ib_mr;
	struct bnxt_qplib_swqe *wqe = &fence->bind_wqe;
	struct bnxt_re_dev *rdev = pd->rdev;

	if (bnxt_qplib_is_chip_gen_p5_p7(rdev->chip_ctx))
		return;

	memset(wqe, 0, sizeof(*wqe));
	wqe->type = BNXT_QPLIB_SWQE_TYPE_BIND_MW;
	wqe->wr_id = BNXT_QPLIB_FENCE_WRID;
	wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_SIGNAL_COMP;
	wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_UC_FENCE;
	wqe->bind.zero_based = false;
	wqe->bind.parent_l_key = ib_mr->lkey;
	wqe->bind.va = (u64)(unsigned long)fence->va;
	wqe->bind.length = fence->size;
	wqe->bind.access_cntl = __from_ib_access_flags(IB_ACCESS_REMOTE_READ);
	wqe->bind.mw_type = SQ_BIND_MW_TYPE_TYPE1;

	/* Save the initial rkey in fence structure for now;
	 * wqe->bind.r_key will be set at (re)bind time.
	 */
	fence->bind_rkey = ib_inc_rkey(fence->mw->rkey);
}

static int bnxt_re_bind_fence_mw(struct bnxt_qplib_qp *qplib_qp)
{
	struct bnxt_re_qp *qp = container_of(qplib_qp, struct bnxt_re_qp,
					     qplib_qp);
	struct ib_pd *ib_pd = qp->ib_qp.pd;
	struct bnxt_re_pd *pd = container_of(ib_pd, struct bnxt_re_pd, ib_pd);
	struct bnxt_re_fence_data *fence = &pd->fence;
	struct bnxt_qplib_swqe *fence_wqe = &fence->bind_wqe;
	struct bnxt_qplib_swqe wqe;
	int rc;

	memcpy(&wqe, fence_wqe, sizeof(wqe));
	wqe.bind.r_key = fence->bind_rkey;
	fence->bind_rkey = ib_inc_rkey(fence->bind_rkey);

	ibdev_dbg(&qp->rdev->ibdev,
		  "Posting bind fence-WQE: rkey: %#x QP: %d PD: %p\n",
		wqe.bind.r_key, qp->qplib_qp.id, pd);
	rc = bnxt_qplib_post_send(&qp->qplib_qp, &wqe);
	if (rc) {
		ibdev_err(&qp->rdev->ibdev, "Failed to bind fence-WQE\n");
		return rc;
	}
	bnxt_qplib_post_send_db(&qp->qplib_qp);

	return rc;
}

static void bnxt_re_destroy_fence_mr(struct bnxt_re_pd *pd)
{
	struct bnxt_re_fence_data *fence = &pd->fence;
	struct bnxt_re_dev *rdev = pd->rdev;
	struct device *dev = &rdev->en_dev->pdev->dev;
	struct bnxt_re_mr *mr = fence->mr;

	if (bnxt_qplib_is_chip_gen_p5_p7(rdev->chip_ctx))
		return;

	if (fence->mw) {
		bnxt_re_dealloc_mw(fence->mw);
		fence->mw = NULL;
	}
	if (mr) {
		if (mr->ib_mr.rkey)
			bnxt_qplib_dereg_mrw(&rdev->qplib_res, &mr->qplib_mr,
					     true);
		if (mr->ib_mr.lkey)
			bnxt_qplib_free_mrw(&rdev->qplib_res, &mr->qplib_mr);
		kfree(mr);
		fence->mr = NULL;
	}
	if (fence->dma_addr) {
		dma_unmap_single(dev, fence->dma_addr, BNXT_RE_FENCE_BYTES,
				 DMA_BIDIRECTIONAL);
		fence->dma_addr = 0;
	}
}

static int bnxt_re_create_fence_mr(struct bnxt_re_pd *pd)
{
	int mr_access_flags = IB_ACCESS_LOCAL_WRITE | IB_ACCESS_MW_BIND;
	struct bnxt_re_fence_data *fence = &pd->fence;
	struct bnxt_re_dev *rdev = pd->rdev;
	struct device *dev = &rdev->en_dev->pdev->dev;
	struct bnxt_re_mr *mr = NULL;
	dma_addr_t dma_addr = 0;
	struct ib_mw *mw;
	int rc;

	if (bnxt_qplib_is_chip_gen_p5_p7(rdev->chip_ctx))
		return 0;

	dma_addr = dma_map_single(dev, fence->va, BNXT_RE_FENCE_BYTES,
				  DMA_BIDIRECTIONAL);
	rc = dma_mapping_error(dev, dma_addr);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to dma-map fence-MR-mem\n");
		rc = -EIO;
		fence->dma_addr = 0;
		goto fail;
	}
	fence->dma_addr = dma_addr;

	/* Allocate a MR */
	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		rc = -ENOMEM;
		goto fail;
	}
	fence->mr = mr;
	mr->rdev = rdev;
	mr->qplib_mr.pd = &pd->qplib_pd;
	mr->qplib_mr.type = CMDQ_ALLOCATE_MRW_MRW_FLAGS_PMR;
	mr->qplib_mr.access_flags = __from_ib_access_flags(mr_access_flags);
	if (!_is_alloc_mr_unified(rdev->dev_attr.dev_cap_flags)) {
		rc = bnxt_qplib_alloc_mrw(&rdev->qplib_res, &mr->qplib_mr);
		if (rc) {
			ibdev_err(&rdev->ibdev, "Failed to alloc fence-HW-MR\n");
			goto fail;
		}

		/* Register MR */
		mr->ib_mr.lkey = mr->qplib_mr.lkey;
	} else {
		mr->qplib_mr.flags = CMDQ_REGISTER_MR_FLAGS_ALLOC_MR;
	}
	mr->qplib_mr.va = (u64)(unsigned long)fence->va;
	mr->qplib_mr.total_size = BNXT_RE_FENCE_BYTES;
	rc = bnxt_qplib_reg_mr(&rdev->qplib_res, &mr->qplib_mr, NULL,
			       BNXT_RE_FENCE_PBL_SIZE, PAGE_SIZE);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to register fence-MR\n");
		goto fail;
	}
	mr->ib_mr.rkey = mr->qplib_mr.rkey;

	/* Create a fence MW only for kernel consumers */
	mw = bnxt_re_alloc_mw(&pd->ib_pd, IB_MW_TYPE_1, NULL);
	if (IS_ERR(mw)) {
		ibdev_err(&rdev->ibdev,
			  "Failed to create fence-MW for PD: %p\n", pd);
		rc = PTR_ERR(mw);
		goto fail;
	}
	fence->mw = mw;

	bnxt_re_create_fence_wqe(pd);
	return 0;

fail:
	bnxt_re_destroy_fence_mr(pd);
	return rc;
}

static struct bnxt_re_user_mmap_entry*
bnxt_re_mmap_entry_insert(struct bnxt_re_ucontext *uctx, u64 mem_offset,
			  enum bnxt_re_mmap_flag mmap_flag, u64 *offset)
{
	struct bnxt_re_user_mmap_entry *entry;
	int ret;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return NULL;

	entry->mem_offset = mem_offset;
	entry->mmap_flag = mmap_flag;
	entry->uctx = uctx;

	switch (mmap_flag) {
	case BNXT_RE_MMAP_SH_PAGE:
		ret = rdma_user_mmap_entry_insert_exact(&uctx->ib_uctx,
							&entry->rdma_entry, PAGE_SIZE, 0);
		break;
	case BNXT_RE_MMAP_UC_DB:
	case BNXT_RE_MMAP_WC_DB:
	case BNXT_RE_MMAP_DBR_BAR:
	case BNXT_RE_MMAP_DBR_PAGE:
	case BNXT_RE_MMAP_TOGGLE_PAGE:
		ret = rdma_user_mmap_entry_insert(&uctx->ib_uctx,
						  &entry->rdma_entry, PAGE_SIZE);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret) {
		kfree(entry);
		return NULL;
	}
	if (offset)
		*offset = rdma_user_mmap_get_offset(&entry->rdma_entry);

	return entry;
}

/* Protection Domains */
int bnxt_re_dealloc_pd(struct ib_pd *ib_pd, struct ib_udata *udata)
{
	struct bnxt_re_pd *pd = container_of(ib_pd, struct bnxt_re_pd, ib_pd);
	struct bnxt_re_dev *rdev = pd->rdev;

	if (udata) {
		rdma_user_mmap_entry_remove(pd->pd_db_mmap);
		pd->pd_db_mmap = NULL;
	}

	bnxt_re_destroy_fence_mr(pd);

	if (pd->qplib_pd.id) {
		if (!bnxt_qplib_dealloc_pd(&rdev->qplib_res,
					   &rdev->qplib_res.pd_tbl,
					   &pd->qplib_pd))
			atomic_dec(&rdev->stats.res.pd_count);
	}
	return 0;
}

int bnxt_re_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct ib_device *ibdev = ibpd->device;
	struct bnxt_re_dev *rdev = to_bnxt_re_dev(ibdev, ibdev);
	struct bnxt_re_ucontext *ucntx = rdma_udata_to_drv_context(
		udata, struct bnxt_re_ucontext, ib_uctx);
	struct bnxt_re_pd *pd = container_of(ibpd, struct bnxt_re_pd, ib_pd);
	struct bnxt_re_user_mmap_entry *entry = NULL;
	u32 active_pds;
	int rc = 0;

	pd->rdev = rdev;
	if (bnxt_qplib_alloc_pd(&rdev->qplib_res, &pd->qplib_pd)) {
		ibdev_err(&rdev->ibdev, "Failed to allocate HW PD");
		rc = -ENOMEM;
		goto fail;
	}

	if (udata) {
		struct bnxt_re_pd_resp resp = {};

		if (!ucntx->dpi.dbr) {
			/* Allocate DPI in alloc_pd to avoid failing of
			 * ibv_devinfo and family of application when DPIs
			 * are depleted.
			 */
			if (bnxt_qplib_alloc_dpi(&rdev->qplib_res,
						 &ucntx->dpi, ucntx, BNXT_QPLIB_DPI_TYPE_UC)) {
				rc = -ENOMEM;
				goto dbfail;
			}
		}

		resp.pdid = pd->qplib_pd.id;
		/* Still allow mapping this DBR to the new user PD. */
		resp.dpi = ucntx->dpi.dpi;

		entry = bnxt_re_mmap_entry_insert(ucntx, (u64)ucntx->dpi.umdbr,
						  BNXT_RE_MMAP_UC_DB, &resp.dbr);

		if (!entry) {
			rc = -ENOMEM;
			goto dbfail;
		}

		pd->pd_db_mmap = &entry->rdma_entry;

		rc = ib_copy_to_udata(udata, &resp, min(sizeof(resp), udata->outlen));
		if (rc) {
			rdma_user_mmap_entry_remove(pd->pd_db_mmap);
			rc = -EFAULT;
			goto dbfail;
		}
	}

	if (!udata)
		if (bnxt_re_create_fence_mr(pd))
			ibdev_warn(&rdev->ibdev,
				   "Failed to create Fence-MR\n");
	active_pds = atomic_inc_return(&rdev->stats.res.pd_count);
	if (active_pds > rdev->stats.res.pd_watermark)
		rdev->stats.res.pd_watermark = active_pds;

	return 0;
dbfail:
	bnxt_qplib_dealloc_pd(&rdev->qplib_res, &rdev->qplib_res.pd_tbl,
			      &pd->qplib_pd);
fail:
	return rc;
}

/* Address Handles */
int bnxt_re_destroy_ah(struct ib_ah *ib_ah, u32 flags)
{
	struct bnxt_re_ah *ah = container_of(ib_ah, struct bnxt_re_ah, ib_ah);
	struct bnxt_re_dev *rdev = ah->rdev;
	bool block = true;
	int rc;

	block = !(flags & RDMA_DESTROY_AH_SLEEPABLE);
	rc = bnxt_qplib_destroy_ah(&rdev->qplib_res, &ah->qplib_ah, block);
	if (BNXT_RE_CHECK_RC(rc)) {
		if (rc == -ETIMEDOUT)
			rc = 0;
		else
			goto fail;
	}
	atomic_dec(&rdev->stats.res.ah_count);
fail:
	return rc;
}

static u8 bnxt_re_stack_to_dev_nw_type(enum rdma_network_type ntype)
{
	u8 nw_type;

	switch (ntype) {
	case RDMA_NETWORK_IPV4:
		nw_type = CMDQ_CREATE_AH_TYPE_V2IPV4;
		break;
	case RDMA_NETWORK_IPV6:
		nw_type = CMDQ_CREATE_AH_TYPE_V2IPV6;
		break;
	default:
		nw_type = CMDQ_CREATE_AH_TYPE_V1;
		break;
	}
	return nw_type;
}

int bnxt_re_create_ah(struct ib_ah *ib_ah, struct rdma_ah_init_attr *init_attr,
		      struct ib_udata *udata)
{
	struct ib_pd *ib_pd = ib_ah->pd;
	struct bnxt_re_pd *pd = container_of(ib_pd, struct bnxt_re_pd, ib_pd);
	struct rdma_ah_attr *ah_attr = init_attr->ah_attr;
	const struct ib_global_route *grh = rdma_ah_read_grh(ah_attr);
	struct bnxt_re_dev *rdev = pd->rdev;
	const struct ib_gid_attr *sgid_attr;
	struct bnxt_re_gid_ctx *ctx;
	struct bnxt_re_ah *ah = container_of(ib_ah, struct bnxt_re_ah, ib_ah);
	u32 active_ahs;
	u8 nw_type;
	int rc;

	if (!(rdma_ah_get_ah_flags(ah_attr) & IB_AH_GRH)) {
		ibdev_err(&rdev->ibdev, "Failed to alloc AH: GRH not set");
		return -EINVAL;
	}

	ah->rdev = rdev;
	ah->qplib_ah.pd = &pd->qplib_pd;

	/* Supply the configuration for the HW */
	memcpy(ah->qplib_ah.dgid.data, grh->dgid.raw,
	       sizeof(union ib_gid));
	sgid_attr = grh->sgid_attr;
	/* Get the HW context of the GID. The reference
	 * of GID table entry is already taken by the caller.
	 */
	ctx = rdma_read_gid_hw_context(sgid_attr);
	ah->qplib_ah.sgid_index = ctx->idx;
	ah->qplib_ah.host_sgid_index = grh->sgid_index;
	ah->qplib_ah.traffic_class = grh->traffic_class;
	ah->qplib_ah.flow_label = grh->flow_label;
	ah->qplib_ah.hop_limit = grh->hop_limit;
	ah->qplib_ah.sl = rdma_ah_get_sl(ah_attr);

	/* Get network header type for this GID */
	nw_type = rdma_gid_attr_network_type(sgid_attr);
	ah->qplib_ah.nw_type = bnxt_re_stack_to_dev_nw_type(nw_type);

	memcpy(ah->qplib_ah.dmac, ah_attr->roce.dmac, ETH_ALEN);
	rc = bnxt_qplib_create_ah(&rdev->qplib_res, &ah->qplib_ah,
				  !(init_attr->flags &
				    RDMA_CREATE_AH_SLEEPABLE));
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to allocate HW AH");
		return rc;
	}

	/* Write AVID to shared page. */
	if (udata) {
		struct bnxt_re_ucontext *uctx = rdma_udata_to_drv_context(
			udata, struct bnxt_re_ucontext, ib_uctx);
		unsigned long flag;
		u32 *wrptr;

		spin_lock_irqsave(&uctx->sh_lock, flag);
		wrptr = (u32 *)(uctx->shpg + BNXT_RE_AVID_OFFT);
		*wrptr = ah->qplib_ah.id;
		wmb(); /* make sure cache is updated. */
		spin_unlock_irqrestore(&uctx->sh_lock, flag);
	}
	active_ahs = atomic_inc_return(&rdev->stats.res.ah_count);
	if (active_ahs > rdev->stats.res.ah_watermark)
		rdev->stats.res.ah_watermark = active_ahs;

	return 0;
}

int bnxt_re_query_ah(struct ib_ah *ib_ah, struct rdma_ah_attr *ah_attr)
{
	struct bnxt_re_ah *ah = container_of(ib_ah, struct bnxt_re_ah, ib_ah);

	ah_attr->type = ib_ah->type;
	rdma_ah_set_sl(ah_attr, ah->qplib_ah.sl);
	memcpy(ah_attr->roce.dmac, ah->qplib_ah.dmac, ETH_ALEN);
	rdma_ah_set_grh(ah_attr, NULL, 0,
			ah->qplib_ah.host_sgid_index,
			0, ah->qplib_ah.traffic_class);
	rdma_ah_set_dgid_raw(ah_attr, ah->qplib_ah.dgid.data);
	rdma_ah_set_port_num(ah_attr, 1);
	rdma_ah_set_static_rate(ah_attr, 0);
	return 0;
}

unsigned long bnxt_re_lock_cqs(struct bnxt_re_qp *qp)
	__acquires(&qp->scq->cq_lock) __acquires(&qp->rcq->cq_lock)
{
	unsigned long flags;

	spin_lock_irqsave(&qp->scq->cq_lock, flags);
	if (qp->rcq != qp->scq)
		spin_lock(&qp->rcq->cq_lock);
	else
		__acquire(&qp->rcq->cq_lock);

	return flags;
}

void bnxt_re_unlock_cqs(struct bnxt_re_qp *qp,
			unsigned long flags)
	__releases(&qp->scq->cq_lock) __releases(&qp->rcq->cq_lock)
{
	if (qp->rcq != qp->scq)
		spin_unlock(&qp->rcq->cq_lock);
	else
		__release(&qp->rcq->cq_lock);
	spin_unlock_irqrestore(&qp->scq->cq_lock, flags);
}

static int bnxt_re_destroy_gsi_sqp(struct bnxt_re_qp *qp)
{
	struct bnxt_re_qp *gsi_sqp;
	struct bnxt_re_ah *gsi_sah;
	struct bnxt_re_dev *rdev;
	int rc;

	rdev = qp->rdev;
	gsi_sqp = rdev->gsi_ctx.gsi_sqp;
	gsi_sah = rdev->gsi_ctx.gsi_sah;

	ibdev_dbg(&rdev->ibdev, "Destroy the shadow AH\n");
	bnxt_qplib_destroy_ah(&rdev->qplib_res,
			      &gsi_sah->qplib_ah,
			      true);
	atomic_dec(&rdev->stats.res.ah_count);
	bnxt_qplib_clean_qp(&qp->qplib_qp);

	ibdev_dbg(&rdev->ibdev, "Destroy the shadow QP\n");
	rc = bnxt_qplib_destroy_qp(&rdev->qplib_res, &gsi_sqp->qplib_qp);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Destroy Shadow QP failed");
		goto fail;
	}
	bnxt_qplib_free_qp_res(&rdev->qplib_res, &gsi_sqp->qplib_qp);

	/* remove from active qp list */
	mutex_lock(&rdev->qp_lock);
	list_del(&gsi_sqp->list);
	mutex_unlock(&rdev->qp_lock);
	atomic_dec(&rdev->stats.res.qp_count);

	kfree(rdev->gsi_ctx.sqp_tbl);
	kfree(gsi_sah);
	kfree(gsi_sqp);
	rdev->gsi_ctx.gsi_sqp = NULL;
	rdev->gsi_ctx.gsi_sah = NULL;
	rdev->gsi_ctx.sqp_tbl = NULL;

	return 0;
fail:
	return rc;
}

/* Queue Pairs */
int bnxt_re_destroy_qp(struct ib_qp *ib_qp, struct ib_udata *udata)
{
	struct bnxt_re_qp *qp = container_of(ib_qp, struct bnxt_re_qp, ib_qp);
	struct bnxt_qplib_qp *qplib_qp = &qp->qplib_qp;
	struct bnxt_re_dev *rdev = qp->rdev;
	struct bnxt_qplib_nq *scq_nq = NULL;
	struct bnxt_qplib_nq *rcq_nq = NULL;
	unsigned int flags;
	int rc;

	bnxt_qplib_flush_cqn_wq(&qp->qplib_qp);

	rc = bnxt_qplib_destroy_qp(&rdev->qplib_res, &qp->qplib_qp);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to destroy HW QP");
		return rc;
	}

	if (rdma_is_kernel_res(&qp->ib_qp.res)) {
		flags = bnxt_re_lock_cqs(qp);
		bnxt_qplib_clean_qp(&qp->qplib_qp);
		bnxt_re_unlock_cqs(qp, flags);
	}

	bnxt_qplib_free_qp_res(&rdev->qplib_res, &qp->qplib_qp);

	if (ib_qp->qp_type == IB_QPT_GSI && rdev->gsi_ctx.gsi_sqp) {
		rc = bnxt_re_destroy_gsi_sqp(qp);
		if (rc)
			return rc;
	}

	mutex_lock(&rdev->qp_lock);
	list_del(&qp->list);
	mutex_unlock(&rdev->qp_lock);
	atomic_dec(&rdev->stats.res.qp_count);
	if (qp->qplib_qp.type == CMDQ_CREATE_QP_TYPE_RC)
		atomic_dec(&rdev->stats.res.rc_qp_count);
	else if (qp->qplib_qp.type == CMDQ_CREATE_QP_TYPE_UD)
		atomic_dec(&rdev->stats.res.ud_qp_count);

	ib_umem_release(qp->rumem);
	ib_umem_release(qp->sumem);

	/* Flush all the entries of notification queue associated with
	 * given qp.
	 */
	scq_nq = qplib_qp->scq->nq;
	rcq_nq = qplib_qp->rcq->nq;
	bnxt_re_synchronize_nq(scq_nq);
	if (scq_nq != rcq_nq)
		bnxt_re_synchronize_nq(rcq_nq);

	return 0;
}

static u8 __from_ib_qp_type(enum ib_qp_type type)
{
	switch (type) {
	case IB_QPT_GSI:
		return CMDQ_CREATE_QP1_TYPE_GSI;
	case IB_QPT_RC:
		return CMDQ_CREATE_QP_TYPE_RC;
	case IB_QPT_UD:
		return CMDQ_CREATE_QP_TYPE_UD;
	default:
		return IB_QPT_MAX;
	}
}

static u16 bnxt_re_setup_rwqe_size(struct bnxt_qplib_qp *qplqp,
				   int rsge, int max)
{
	if (qplqp->wqe_mode == BNXT_QPLIB_WQE_MODE_STATIC)
		rsge = max;
	return bnxt_re_get_rwqe_size(rsge);
}

static u16 bnxt_re_get_wqe_size(int ilsize, int nsge)
{
	u16 wqe_size, calc_ils;

	wqe_size = bnxt_re_get_swqe_size(nsge);
	if (ilsize) {
		calc_ils = sizeof(struct sq_send_hdr) + ilsize;
		wqe_size = max_t(u16, calc_ils, wqe_size);
		wqe_size = ALIGN(wqe_size, sizeof(struct sq_send_hdr));
	}
	return wqe_size;
}

static int bnxt_re_setup_swqe_size(struct bnxt_re_qp *qp,
				   struct ib_qp_init_attr *init_attr)
{
	struct bnxt_qplib_dev_attr *dev_attr;
	struct bnxt_qplib_qp *qplqp;
	struct bnxt_re_dev *rdev;
	struct bnxt_qplib_q *sq;
	int align, ilsize;

	rdev = qp->rdev;
	qplqp = &qp->qplib_qp;
	sq = &qplqp->sq;
	dev_attr = &rdev->dev_attr;

	align = sizeof(struct sq_send_hdr);
	ilsize = ALIGN(init_attr->cap.max_inline_data, align);

	/* For gen p4 and gen p5 fixed wqe compatibility mode
	 * wqe size is fixed to 128 bytes - ie 6 SGEs
	 */
	if (qplqp->wqe_mode == BNXT_QPLIB_WQE_MODE_STATIC) {
		sq->wqe_size = bnxt_re_get_swqe_size(BNXT_STATIC_MAX_SGE);
		sq->max_sge = BNXT_STATIC_MAX_SGE;
	} else {
		sq->wqe_size = bnxt_re_get_wqe_size(ilsize, sq->max_sge);
		if (sq->wqe_size > bnxt_re_get_swqe_size(dev_attr->max_qp_sges))
			return -EINVAL;
	}

	if (init_attr->cap.max_inline_data) {
		qplqp->max_inline_data = sq->wqe_size -
			sizeof(struct sq_send_hdr);
		init_attr->cap.max_inline_data = qplqp->max_inline_data;
	}

	return 0;
}

static int bnxt_re_init_user_qp(struct bnxt_re_dev *rdev, struct bnxt_re_pd *pd,
				struct bnxt_re_qp *qp, struct bnxt_re_ucontext *cntx,
				struct bnxt_re_qp_req *ureq)
{
	struct bnxt_qplib_qp *qplib_qp;
	int bytes = 0, psn_sz;
	struct ib_umem *umem;
	int psn_nume;

	qplib_qp = &qp->qplib_qp;

	bytes = (qplib_qp->sq.max_wqe * qplib_qp->sq.wqe_size);
	/* Consider mapping PSN search memory only for RC QPs. */
	if (qplib_qp->type == CMDQ_CREATE_QP_TYPE_RC) {
		psn_sz = bnxt_qplib_is_chip_gen_p5_p7(rdev->chip_ctx) ?
						   sizeof(struct sq_psn_search_ext) :
						   sizeof(struct sq_psn_search);
		if (cntx && bnxt_re_is_var_size_supported(rdev, cntx)) {
			psn_nume = ureq->sq_slots;
		} else {
			psn_nume = (qplib_qp->wqe_mode == BNXT_QPLIB_WQE_MODE_STATIC) ?
			qplib_qp->sq.max_wqe : ((qplib_qp->sq.max_wqe * qplib_qp->sq.wqe_size) /
				 sizeof(struct bnxt_qplib_sge));
		}
		if (_is_host_msn_table(rdev->qplib_res.dattr->dev_cap_flags2))
			psn_nume = roundup_pow_of_two(psn_nume);
		bytes += (psn_nume * psn_sz);
	}

	bytes = PAGE_ALIGN(bytes);
	umem = ib_umem_get(&rdev->ibdev, ureq->qpsva, bytes,
			   IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(umem))
		return PTR_ERR(umem);

	qp->sumem = umem;
	qplib_qp->sq.sg_info.umem = umem;
	qplib_qp->sq.sg_info.pgsize = PAGE_SIZE;
	qplib_qp->sq.sg_info.pgshft = PAGE_SHIFT;
	qplib_qp->qp_handle = ureq->qp_handle;

	if (!qp->qplib_qp.srq) {
		bytes = (qplib_qp->rq.max_wqe * qplib_qp->rq.wqe_size);
		bytes = PAGE_ALIGN(bytes);
		umem = ib_umem_get(&rdev->ibdev, ureq->qprva, bytes,
				   IB_ACCESS_LOCAL_WRITE);
		if (IS_ERR(umem))
			goto rqfail;
		qp->rumem = umem;
		qplib_qp->rq.sg_info.umem = umem;
		qplib_qp->rq.sg_info.pgsize = PAGE_SIZE;
		qplib_qp->rq.sg_info.pgshft = PAGE_SHIFT;
	}

	qplib_qp->dpi = &cntx->dpi;
	return 0;
rqfail:
	ib_umem_release(qp->sumem);
	qp->sumem = NULL;
	memset(&qplib_qp->sq.sg_info, 0, sizeof(qplib_qp->sq.sg_info));

	return PTR_ERR(umem);
}

static struct bnxt_re_ah *bnxt_re_create_shadow_qp_ah
				(struct bnxt_re_pd *pd,
				 struct bnxt_qplib_res *qp1_res,
				 struct bnxt_qplib_qp *qp1_qp)
{
	struct bnxt_re_dev *rdev = pd->rdev;
	struct bnxt_re_ah *ah;
	union ib_gid sgid;
	int rc;

	ah = kzalloc(sizeof(*ah), GFP_KERNEL);
	if (!ah)
		return NULL;

	ah->rdev = rdev;
	ah->qplib_ah.pd = &pd->qplib_pd;

	rc = bnxt_re_query_gid(&rdev->ibdev, 1, 0, &sgid);
	if (rc)
		goto fail;

	/* supply the dgid data same as sgid */
	memcpy(ah->qplib_ah.dgid.data, &sgid.raw,
	       sizeof(union ib_gid));
	ah->qplib_ah.sgid_index = 0;

	ah->qplib_ah.traffic_class = 0;
	ah->qplib_ah.flow_label = 0;
	ah->qplib_ah.hop_limit = 1;
	ah->qplib_ah.sl = 0;
	/* Have DMAC same as SMAC */
	ether_addr_copy(ah->qplib_ah.dmac, rdev->netdev->dev_addr);

	rc = bnxt_qplib_create_ah(&rdev->qplib_res, &ah->qplib_ah, false);
	if (rc) {
		ibdev_err(&rdev->ibdev,
			  "Failed to allocate HW AH for Shadow QP");
		goto fail;
	}
	atomic_inc(&rdev->stats.res.ah_count);

	return ah;

fail:
	kfree(ah);
	return NULL;
}

static struct bnxt_re_qp *bnxt_re_create_shadow_qp
				(struct bnxt_re_pd *pd,
				 struct bnxt_qplib_res *qp1_res,
				 struct bnxt_qplib_qp *qp1_qp)
{
	struct bnxt_re_dev *rdev = pd->rdev;
	struct bnxt_re_qp *qp;
	int rc;

	qp = kzalloc(sizeof(*qp), GFP_KERNEL);
	if (!qp)
		return NULL;

	qp->rdev = rdev;

	/* Initialize the shadow QP structure from the QP1 values */
	ether_addr_copy(qp->qplib_qp.smac, rdev->netdev->dev_addr);

	qp->qplib_qp.pd = &pd->qplib_pd;
	qp->qplib_qp.qp_handle = (u64)(unsigned long)(&qp->qplib_qp);
	qp->qplib_qp.type = IB_QPT_UD;

	qp->qplib_qp.max_inline_data = 0;
	qp->qplib_qp.sig_type = true;

	/* Shadow QP SQ depth should be same as QP1 RQ depth */
	qp->qplib_qp.sq.wqe_size = bnxt_re_get_wqe_size(0, 6);
	qp->qplib_qp.sq.max_wqe = qp1_qp->rq.max_wqe;
	qp->qplib_qp.sq.max_sw_wqe = qp1_qp->rq.max_wqe;
	qp->qplib_qp.sq.max_sge = 2;
	/* Q full delta can be 1 since it is internal QP */
	qp->qplib_qp.sq.q_full_delta = 1;
	qp->qplib_qp.sq.sg_info.pgsize = PAGE_SIZE;
	qp->qplib_qp.sq.sg_info.pgshft = PAGE_SHIFT;

	qp->qplib_qp.scq = qp1_qp->scq;
	qp->qplib_qp.rcq = qp1_qp->rcq;

	qp->qplib_qp.rq.wqe_size = bnxt_re_get_rwqe_size(6);
	qp->qplib_qp.rq.max_wqe = qp1_qp->rq.max_wqe;
	qp->qplib_qp.rq.max_sw_wqe = qp1_qp->rq.max_wqe;
	qp->qplib_qp.rq.max_sge = qp1_qp->rq.max_sge;
	/* Q full delta can be 1 since it is internal QP */
	qp->qplib_qp.rq.q_full_delta = 1;
	qp->qplib_qp.rq.sg_info.pgsize = PAGE_SIZE;
	qp->qplib_qp.rq.sg_info.pgshft = PAGE_SHIFT;

	qp->qplib_qp.mtu = qp1_qp->mtu;

	qp->qplib_qp.sq_hdr_buf_size = 0;
	qp->qplib_qp.rq_hdr_buf_size = BNXT_QPLIB_MAX_GRH_HDR_SIZE_IPV6;
	qp->qplib_qp.dpi = &rdev->dpi_privileged;

	rc = bnxt_qplib_create_qp(qp1_res, &qp->qplib_qp);
	if (rc)
		goto fail;

	spin_lock_init(&qp->sq_lock);
	INIT_LIST_HEAD(&qp->list);
	mutex_lock(&rdev->qp_lock);
	list_add_tail(&qp->list, &rdev->qp_list);
	atomic_inc(&rdev->stats.res.qp_count);
	mutex_unlock(&rdev->qp_lock);
	return qp;
fail:
	kfree(qp);
	return NULL;
}

static int bnxt_re_init_rq_attr(struct bnxt_re_qp *qp,
				struct ib_qp_init_attr *init_attr,
				struct bnxt_re_ucontext *uctx)
{
	struct bnxt_qplib_dev_attr *dev_attr;
	struct bnxt_qplib_qp *qplqp;
	struct bnxt_re_dev *rdev;
	struct bnxt_qplib_q *rq;
	int entries;

	rdev = qp->rdev;
	qplqp = &qp->qplib_qp;
	rq = &qplqp->rq;
	dev_attr = &rdev->dev_attr;

	if (init_attr->srq) {
		struct bnxt_re_srq *srq;

		srq = container_of(init_attr->srq, struct bnxt_re_srq, ib_srq);
		qplqp->srq = &srq->qplib_srq;
		rq->max_wqe = 0;
	} else {
		rq->max_sge = init_attr->cap.max_recv_sge;
		if (rq->max_sge > dev_attr->max_qp_sges)
			rq->max_sge = dev_attr->max_qp_sges;
		init_attr->cap.max_recv_sge = rq->max_sge;
		rq->wqe_size = bnxt_re_setup_rwqe_size(qplqp, rq->max_sge,
						       dev_attr->max_qp_sges);
		/* Allocate 1 more than what's provided so posting max doesn't
		 * mean empty.
		 */
		entries = bnxt_re_init_depth(init_attr->cap.max_recv_wr + 1, uctx);
		rq->max_wqe = min_t(u32, entries, dev_attr->max_qp_wqes + 1);
		rq->max_sw_wqe = rq->max_wqe;
		rq->q_full_delta = 0;
		rq->sg_info.pgsize = PAGE_SIZE;
		rq->sg_info.pgshft = PAGE_SHIFT;
	}

	return 0;
}

static void bnxt_re_adjust_gsi_rq_attr(struct bnxt_re_qp *qp)
{
	struct bnxt_qplib_dev_attr *dev_attr;
	struct bnxt_qplib_qp *qplqp;
	struct bnxt_re_dev *rdev;

	rdev = qp->rdev;
	qplqp = &qp->qplib_qp;
	dev_attr = &rdev->dev_attr;

	if (!bnxt_qplib_is_chip_gen_p5_p7(rdev->chip_ctx)) {
		qplqp->rq.max_sge = dev_attr->max_qp_sges;
		if (qplqp->rq.max_sge > dev_attr->max_qp_sges)
			qplqp->rq.max_sge = dev_attr->max_qp_sges;
		qplqp->rq.max_sge = 6;
	}
}

static int bnxt_re_init_sq_attr(struct bnxt_re_qp *qp,
				struct ib_qp_init_attr *init_attr,
				struct bnxt_re_ucontext *uctx,
				struct bnxt_re_qp_req *ureq)
{
	struct bnxt_qplib_dev_attr *dev_attr;
	struct bnxt_qplib_qp *qplqp;
	struct bnxt_re_dev *rdev;
	struct bnxt_qplib_q *sq;
	int diff = 0;
	int entries;
	int rc;

	rdev = qp->rdev;
	qplqp = &qp->qplib_qp;
	sq = &qplqp->sq;
	dev_attr = &rdev->dev_attr;

	sq->max_sge = init_attr->cap.max_send_sge;
	entries = init_attr->cap.max_send_wr;
	if (uctx && qplqp->wqe_mode == BNXT_QPLIB_WQE_MODE_VARIABLE) {
		sq->max_wqe = ureq->sq_slots;
		sq->max_sw_wqe = ureq->sq_slots;
		sq->wqe_size = sizeof(struct sq_sge);
	} else {
		if (sq->max_sge > dev_attr->max_qp_sges) {
			sq->max_sge = dev_attr->max_qp_sges;
			init_attr->cap.max_send_sge = sq->max_sge;
		}

		rc = bnxt_re_setup_swqe_size(qp, init_attr);
		if (rc)
			return rc;

		/* Allocate 128 + 1 more than what's provided */
		diff = (qplqp->wqe_mode == BNXT_QPLIB_WQE_MODE_VARIABLE) ?
			0 : BNXT_QPLIB_RESERVED_QP_WRS;
		entries = bnxt_re_init_depth(entries + diff + 1, uctx);
		sq->max_wqe = min_t(u32, entries, dev_attr->max_qp_wqes + diff + 1);
		sq->max_sw_wqe = bnxt_qplib_get_depth(sq, qplqp->wqe_mode, true);
	}
	sq->q_full_delta = diff + 1;
	/*
	 * Reserving one slot for Phantom WQE. Application can
	 * post one extra entry in this case. But allowing this to avoid
	 * unexpected Queue full condition
	 */
	qplqp->sq.q_full_delta -= 1;
	qplqp->sq.sg_info.pgsize = PAGE_SIZE;
	qplqp->sq.sg_info.pgshft = PAGE_SHIFT;

	return 0;
}

static void bnxt_re_adjust_gsi_sq_attr(struct bnxt_re_qp *qp,
				       struct ib_qp_init_attr *init_attr,
				       struct bnxt_re_ucontext *uctx)
{
	struct bnxt_qplib_dev_attr *dev_attr;
	struct bnxt_qplib_qp *qplqp;
	struct bnxt_re_dev *rdev;
	int entries;

	rdev = qp->rdev;
	qplqp = &qp->qplib_qp;
	dev_attr = &rdev->dev_attr;

	if (!bnxt_qplib_is_chip_gen_p5_p7(rdev->chip_ctx)) {
		entries = bnxt_re_init_depth(init_attr->cap.max_send_wr + 1, uctx);
		qplqp->sq.max_wqe = min_t(u32, entries,
					  dev_attr->max_qp_wqes + 1);
		qplqp->sq.q_full_delta = qplqp->sq.max_wqe -
			init_attr->cap.max_send_wr;
		qplqp->sq.max_sge++; /* Need one extra sge to put UD header */
		if (qplqp->sq.max_sge > dev_attr->max_qp_sges)
			qplqp->sq.max_sge = dev_attr->max_qp_sges;
	}
}

static int bnxt_re_init_qp_type(struct bnxt_re_dev *rdev,
				struct ib_qp_init_attr *init_attr)
{
	struct bnxt_qplib_chip_ctx *chip_ctx;
	int qptype;

	chip_ctx = rdev->chip_ctx;

	qptype = __from_ib_qp_type(init_attr->qp_type);
	if (qptype == IB_QPT_MAX) {
		ibdev_err(&rdev->ibdev, "QP type 0x%x not supported", qptype);
		qptype = -EOPNOTSUPP;
		goto out;
	}

	if (bnxt_qplib_is_chip_gen_p5_p7(chip_ctx) &&
	    init_attr->qp_type == IB_QPT_GSI)
		qptype = CMDQ_CREATE_QP_TYPE_GSI;
out:
	return qptype;
}

static int bnxt_re_init_qp_attr(struct bnxt_re_qp *qp, struct bnxt_re_pd *pd,
				struct ib_qp_init_attr *init_attr,
				struct bnxt_re_ucontext *uctx,
				struct bnxt_re_qp_req *ureq)
{
	struct bnxt_qplib_dev_attr *dev_attr;
	struct bnxt_qplib_qp *qplqp;
	struct bnxt_re_dev *rdev;
	struct bnxt_re_cq *cq;
	int rc = 0, qptype;

	rdev = qp->rdev;
	qplqp = &qp->qplib_qp;
	dev_attr = &rdev->dev_attr;

	/* Setup misc params */
	ether_addr_copy(qplqp->smac, rdev->netdev->dev_addr);
	qplqp->pd = &pd->qplib_pd;
	qplqp->qp_handle = (u64)qplqp;
	qplqp->max_inline_data = init_attr->cap.max_inline_data;
	qplqp->sig_type = init_attr->sq_sig_type == IB_SIGNAL_ALL_WR;
	qptype = bnxt_re_init_qp_type(rdev, init_attr);
	if (qptype < 0) {
		rc = qptype;
		goto out;
	}
	qplqp->type = (u8)qptype;
	qplqp->wqe_mode = bnxt_re_is_var_size_supported(rdev, uctx);
	if (init_attr->qp_type == IB_QPT_RC) {
		qplqp->max_rd_atomic = dev_attr->max_qp_rd_atom;
		qplqp->max_dest_rd_atomic = dev_attr->max_qp_init_rd_atom;
	}
	qplqp->mtu = ib_mtu_enum_to_int(iboe_get_mtu(rdev->netdev->mtu));
	qplqp->dpi = &rdev->dpi_privileged; /* Doorbell page */
	if (init_attr->create_flags) {
		ibdev_dbg(&rdev->ibdev,
			  "QP create flags 0x%x not supported",
			  init_attr->create_flags);
		return -EOPNOTSUPP;
	}

	/* Setup CQs */
	if (init_attr->send_cq) {
		cq = container_of(init_attr->send_cq, struct bnxt_re_cq, ib_cq);
		qplqp->scq = &cq->qplib_cq;
		qp->scq = cq;
	}

	if (init_attr->recv_cq) {
		cq = container_of(init_attr->recv_cq, struct bnxt_re_cq, ib_cq);
		qplqp->rcq = &cq->qplib_cq;
		qp->rcq = cq;
	}

	/* Setup RQ/SRQ */
	rc = bnxt_re_init_rq_attr(qp, init_attr, uctx);
	if (rc)
		goto out;
	if (init_attr->qp_type == IB_QPT_GSI)
		bnxt_re_adjust_gsi_rq_attr(qp);

	/* Setup SQ */
	rc = bnxt_re_init_sq_attr(qp, init_attr, uctx, ureq);
	if (rc)
		goto out;
	if (init_attr->qp_type == IB_QPT_GSI)
		bnxt_re_adjust_gsi_sq_attr(qp, init_attr, uctx);

	if (uctx) /* This will update DPI and qp_handle */
		rc = bnxt_re_init_user_qp(rdev, pd, qp, uctx, ureq);
out:
	return rc;
}

static int bnxt_re_create_shadow_gsi(struct bnxt_re_qp *qp,
				     struct bnxt_re_pd *pd)
{
	struct bnxt_re_sqp_entries *sqp_tbl;
	struct bnxt_re_dev *rdev;
	struct bnxt_re_qp *sqp;
	struct bnxt_re_ah *sah;
	int rc = 0;

	rdev = qp->rdev;
	/* Create a shadow QP to handle the QP1 traffic */
	sqp_tbl = kcalloc(BNXT_RE_MAX_GSI_SQP_ENTRIES, sizeof(*sqp_tbl),
			  GFP_KERNEL);
	if (!sqp_tbl)
		return -ENOMEM;
	rdev->gsi_ctx.sqp_tbl = sqp_tbl;

	sqp = bnxt_re_create_shadow_qp(pd, &rdev->qplib_res, &qp->qplib_qp);
	if (!sqp) {
		rc = -ENODEV;
		ibdev_err(&rdev->ibdev, "Failed to create Shadow QP for QP1");
		goto out;
	}
	rdev->gsi_ctx.gsi_sqp = sqp;

	sqp->rcq = qp->rcq;
	sqp->scq = qp->scq;
	sah = bnxt_re_create_shadow_qp_ah(pd, &rdev->qplib_res,
					  &qp->qplib_qp);
	if (!sah) {
		bnxt_qplib_destroy_qp(&rdev->qplib_res,
				      &sqp->qplib_qp);
		rc = -ENODEV;
		ibdev_err(&rdev->ibdev,
			  "Failed to create AH entry for ShadowQP");
		goto out;
	}
	rdev->gsi_ctx.gsi_sah = sah;

	return 0;
out:
	kfree(sqp_tbl);
	return rc;
}

static int bnxt_re_create_gsi_qp(struct bnxt_re_qp *qp, struct bnxt_re_pd *pd,
				 struct ib_qp_init_attr *init_attr)
{
	struct bnxt_re_dev *rdev;
	struct bnxt_qplib_qp *qplqp;
	int rc;

	rdev = qp->rdev;
	qplqp = &qp->qplib_qp;

	qplqp->rq_hdr_buf_size = BNXT_QPLIB_MAX_QP1_RQ_HDR_SIZE_V2;
	qplqp->sq_hdr_buf_size = BNXT_QPLIB_MAX_QP1_SQ_HDR_SIZE_V2;

	rc = bnxt_qplib_create_qp1(&rdev->qplib_res, qplqp);
	if (rc) {
		ibdev_err(&rdev->ibdev, "create HW QP1 failed!");
		goto out;
	}

	rc = bnxt_re_create_shadow_gsi(qp, pd);
out:
	return rc;
}

static bool bnxt_re_test_qp_limits(struct bnxt_re_dev *rdev,
				   struct ib_qp_init_attr *init_attr,
				   struct bnxt_qplib_dev_attr *dev_attr)
{
	bool rc = true;

	if (init_attr->cap.max_send_wr > dev_attr->max_qp_wqes ||
	    init_attr->cap.max_recv_wr > dev_attr->max_qp_wqes ||
	    init_attr->cap.max_send_sge > dev_attr->max_qp_sges ||
	    init_attr->cap.max_recv_sge > dev_attr->max_qp_sges ||
	    init_attr->cap.max_inline_data > dev_attr->max_inline_data) {
		ibdev_err(&rdev->ibdev,
			  "Create QP failed - max exceeded! 0x%x/0x%x 0x%x/0x%x 0x%x/0x%x 0x%x/0x%x 0x%x/0x%x",
			  init_attr->cap.max_send_wr, dev_attr->max_qp_wqes,
			  init_attr->cap.max_recv_wr, dev_attr->max_qp_wqes,
			  init_attr->cap.max_send_sge, dev_attr->max_qp_sges,
			  init_attr->cap.max_recv_sge, dev_attr->max_qp_sges,
			  init_attr->cap.max_inline_data,
			  dev_attr->max_inline_data);
		rc = false;
	}
	return rc;
}

int bnxt_re_create_qp(struct ib_qp *ib_qp, struct ib_qp_init_attr *qp_init_attr,
		      struct ib_udata *udata)
{
	struct bnxt_qplib_dev_attr *dev_attr;
	struct bnxt_re_ucontext *uctx;
	struct bnxt_re_qp_req ureq;
	struct bnxt_re_dev *rdev;
	struct bnxt_re_pd *pd;
	struct bnxt_re_qp *qp;
	struct ib_pd *ib_pd;
	u32 active_qps;
	int rc;

	ib_pd = ib_qp->pd;
	pd = container_of(ib_pd, struct bnxt_re_pd, ib_pd);
	rdev = pd->rdev;
	dev_attr = &rdev->dev_attr;
	qp = container_of(ib_qp, struct bnxt_re_qp, ib_qp);

	uctx = rdma_udata_to_drv_context(udata, struct bnxt_re_ucontext, ib_uctx);
	if (udata)
		if (ib_copy_from_udata(&ureq, udata,  min(udata->inlen, sizeof(ureq))))
			return -EFAULT;

	rc = bnxt_re_test_qp_limits(rdev, qp_init_attr, dev_attr);
	if (!rc) {
		rc = -EINVAL;
		goto fail;
	}

	qp->rdev = rdev;
	rc = bnxt_re_init_qp_attr(qp, pd, qp_init_attr, uctx, &ureq);
	if (rc)
		goto fail;

	if (qp_init_attr->qp_type == IB_QPT_GSI &&
	    !(bnxt_qplib_is_chip_gen_p5_p7(rdev->chip_ctx))) {
		rc = bnxt_re_create_gsi_qp(qp, pd, qp_init_attr);
		if (rc == -ENODEV)
			goto qp_destroy;
		if (rc)
			goto fail;
	} else {
		rc = bnxt_qplib_create_qp(&rdev->qplib_res, &qp->qplib_qp);
		if (rc) {
			ibdev_err(&rdev->ibdev, "Failed to create HW QP");
			goto free_umem;
		}
		if (udata) {
			struct bnxt_re_qp_resp resp;

			resp.qpid = qp->qplib_qp.id;
			resp.rsvd = 0;
			rc = ib_copy_to_udata(udata, &resp, sizeof(resp));
			if (rc) {
				ibdev_err(&rdev->ibdev, "Failed to copy QP udata");
				goto qp_destroy;
			}
		}
	}

	qp->ib_qp.qp_num = qp->qplib_qp.id;
	if (qp_init_attr->qp_type == IB_QPT_GSI)
		rdev->gsi_ctx.gsi_qp = qp;
	spin_lock_init(&qp->sq_lock);
	spin_lock_init(&qp->rq_lock);
	INIT_LIST_HEAD(&qp->list);
	mutex_lock(&rdev->qp_lock);
	list_add_tail(&qp->list, &rdev->qp_list);
	mutex_unlock(&rdev->qp_lock);
	active_qps = atomic_inc_return(&rdev->stats.res.qp_count);
	if (active_qps > rdev->stats.res.qp_watermark)
		rdev->stats.res.qp_watermark = active_qps;
	if (qp_init_attr->qp_type == IB_QPT_RC) {
		active_qps = atomic_inc_return(&rdev->stats.res.rc_qp_count);
		if (active_qps > rdev->stats.res.rc_qp_watermark)
			rdev->stats.res.rc_qp_watermark = active_qps;
	} else if (qp_init_attr->qp_type == IB_QPT_UD) {
		active_qps = atomic_inc_return(&rdev->stats.res.ud_qp_count);
		if (active_qps > rdev->stats.res.ud_qp_watermark)
			rdev->stats.res.ud_qp_watermark = active_qps;
	}

	return 0;
qp_destroy:
	bnxt_qplib_destroy_qp(&rdev->qplib_res, &qp->qplib_qp);
free_umem:
	ib_umem_release(qp->rumem);
	ib_umem_release(qp->sumem);
fail:
	return rc;
}

static u8 __from_ib_qp_state(enum ib_qp_state state)
{
	switch (state) {
	case IB_QPS_RESET:
		return CMDQ_MODIFY_QP_NEW_STATE_RESET;
	case IB_QPS_INIT:
		return CMDQ_MODIFY_QP_NEW_STATE_INIT;
	case IB_QPS_RTR:
		return CMDQ_MODIFY_QP_NEW_STATE_RTR;
	case IB_QPS_RTS:
		return CMDQ_MODIFY_QP_NEW_STATE_RTS;
	case IB_QPS_SQD:
		return CMDQ_MODIFY_QP_NEW_STATE_SQD;
	case IB_QPS_SQE:
		return CMDQ_MODIFY_QP_NEW_STATE_SQE;
	case IB_QPS_ERR:
	default:
		return CMDQ_MODIFY_QP_NEW_STATE_ERR;
	}
}

static enum ib_qp_state __to_ib_qp_state(u8 state)
{
	switch (state) {
	case CMDQ_MODIFY_QP_NEW_STATE_RESET:
		return IB_QPS_RESET;
	case CMDQ_MODIFY_QP_NEW_STATE_INIT:
		return IB_QPS_INIT;
	case CMDQ_MODIFY_QP_NEW_STATE_RTR:
		return IB_QPS_RTR;
	case CMDQ_MODIFY_QP_NEW_STATE_RTS:
		return IB_QPS_RTS;
	case CMDQ_MODIFY_QP_NEW_STATE_SQD:
		return IB_QPS_SQD;
	case CMDQ_MODIFY_QP_NEW_STATE_SQE:
		return IB_QPS_SQE;
	case CMDQ_MODIFY_QP_NEW_STATE_ERR:
	default:
		return IB_QPS_ERR;
	}
}

static u32 __from_ib_mtu(enum ib_mtu mtu)
{
	switch (mtu) {
	case IB_MTU_256:
		return CMDQ_MODIFY_QP_PATH_MTU_MTU_256;
	case IB_MTU_512:
		return CMDQ_MODIFY_QP_PATH_MTU_MTU_512;
	case IB_MTU_1024:
		return CMDQ_MODIFY_QP_PATH_MTU_MTU_1024;
	case IB_MTU_2048:
		return CMDQ_MODIFY_QP_PATH_MTU_MTU_2048;
	case IB_MTU_4096:
		return CMDQ_MODIFY_QP_PATH_MTU_MTU_4096;
	default:
		return CMDQ_MODIFY_QP_PATH_MTU_MTU_2048;
	}
}

static enum ib_mtu __to_ib_mtu(u32 mtu)
{
	switch (mtu & CREQ_QUERY_QP_RESP_SB_PATH_MTU_MASK) {
	case CMDQ_MODIFY_QP_PATH_MTU_MTU_256:
		return IB_MTU_256;
	case CMDQ_MODIFY_QP_PATH_MTU_MTU_512:
		return IB_MTU_512;
	case CMDQ_MODIFY_QP_PATH_MTU_MTU_1024:
		return IB_MTU_1024;
	case CMDQ_MODIFY_QP_PATH_MTU_MTU_2048:
		return IB_MTU_2048;
	case CMDQ_MODIFY_QP_PATH_MTU_MTU_4096:
		return IB_MTU_4096;
	default:
		return IB_MTU_2048;
	}
}

/* Shared Receive Queues */
int bnxt_re_destroy_srq(struct ib_srq *ib_srq, struct ib_udata *udata)
{
	struct bnxt_re_srq *srq = container_of(ib_srq, struct bnxt_re_srq,
					       ib_srq);
	struct bnxt_re_dev *rdev = srq->rdev;
	struct bnxt_qplib_srq *qplib_srq = &srq->qplib_srq;
	struct bnxt_qplib_nq *nq = NULL;

	if (qplib_srq->cq)
		nq = qplib_srq->cq->nq;
	if (rdev->chip_ctx->modes.toggle_bits & BNXT_QPLIB_SRQ_TOGGLE_BIT) {
		free_page((unsigned long)srq->uctx_srq_page);
		hash_del(&srq->hash_entry);
	}
	bnxt_qplib_destroy_srq(&rdev->qplib_res, qplib_srq);
	ib_umem_release(srq->umem);
	atomic_dec(&rdev->stats.res.srq_count);
	if (nq)
		nq->budget--;
	return 0;
}

static int bnxt_re_init_user_srq(struct bnxt_re_dev *rdev,
				 struct bnxt_re_pd *pd,
				 struct bnxt_re_srq *srq,
				 struct ib_udata *udata)
{
	struct bnxt_re_srq_req ureq;
	struct bnxt_qplib_srq *qplib_srq = &srq->qplib_srq;
	struct ib_umem *umem;
	int bytes = 0;
	struct bnxt_re_ucontext *cntx = rdma_udata_to_drv_context(
		udata, struct bnxt_re_ucontext, ib_uctx);

	if (ib_copy_from_udata(&ureq, udata, sizeof(ureq)))
		return -EFAULT;

	bytes = (qplib_srq->max_wqe * qplib_srq->wqe_size);
	bytes = PAGE_ALIGN(bytes);
	umem = ib_umem_get(&rdev->ibdev, ureq.srqva, bytes,
			   IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(umem))
		return PTR_ERR(umem);

	srq->umem = umem;
	qplib_srq->sg_info.umem = umem;
	qplib_srq->sg_info.pgsize = PAGE_SIZE;
	qplib_srq->sg_info.pgshft = PAGE_SHIFT;
	qplib_srq->srq_handle = ureq.srq_handle;
	qplib_srq->dpi = &cntx->dpi;

	return 0;
}

int bnxt_re_create_srq(struct ib_srq *ib_srq,
		       struct ib_srq_init_attr *srq_init_attr,
		       struct ib_udata *udata)
{
	struct bnxt_qplib_dev_attr *dev_attr;
	struct bnxt_qplib_nq *nq = NULL;
	struct bnxt_re_ucontext *uctx;
	struct bnxt_re_dev *rdev;
	struct bnxt_re_srq *srq;
	struct bnxt_re_pd *pd;
	struct ib_pd *ib_pd;
	u32 active_srqs;
	int rc, entries;

	ib_pd = ib_srq->pd;
	pd = container_of(ib_pd, struct bnxt_re_pd, ib_pd);
	rdev = pd->rdev;
	dev_attr = &rdev->dev_attr;
	srq = container_of(ib_srq, struct bnxt_re_srq, ib_srq);

	if (srq_init_attr->attr.max_wr >= dev_attr->max_srq_wqes) {
		ibdev_err(&rdev->ibdev, "Create CQ failed - max exceeded");
		rc = -EINVAL;
		goto exit;
	}

	if (srq_init_attr->srq_type != IB_SRQT_BASIC) {
		rc = -EOPNOTSUPP;
		goto exit;
	}

	uctx = rdma_udata_to_drv_context(udata, struct bnxt_re_ucontext, ib_uctx);
	srq->rdev = rdev;
	srq->qplib_srq.pd = &pd->qplib_pd;
	srq->qplib_srq.dpi = &rdev->dpi_privileged;
	/* Allocate 1 more than what's provided so posting max doesn't
	 * mean empty
	 */
	entries = bnxt_re_init_depth(srq_init_attr->attr.max_wr + 1, uctx);
	if (entries > dev_attr->max_srq_wqes + 1)
		entries = dev_attr->max_srq_wqes + 1;
	srq->qplib_srq.max_wqe = entries;

	srq->qplib_srq.max_sge = srq_init_attr->attr.max_sge;
	 /* 128 byte wqe size for SRQ . So use max sges */
	srq->qplib_srq.wqe_size = bnxt_re_get_rwqe_size(dev_attr->max_srq_sges);
	srq->qplib_srq.threshold = srq_init_attr->attr.srq_limit;
	srq->srq_limit = srq_init_attr->attr.srq_limit;
	srq->qplib_srq.eventq_hw_ring_id = rdev->nq[0].ring_id;
	nq = &rdev->nq[0];

	if (udata) {
		rc = bnxt_re_init_user_srq(rdev, pd, srq, udata);
		if (rc)
			goto fail;
	}

	rc = bnxt_qplib_create_srq(&rdev->qplib_res, &srq->qplib_srq);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Create HW SRQ failed!");
		goto fail;
	}

	if (udata) {
		struct bnxt_re_srq_resp resp = {};

		resp.srqid = srq->qplib_srq.id;
		if (rdev->chip_ctx->modes.toggle_bits & BNXT_QPLIB_SRQ_TOGGLE_BIT) {
			hash_add(rdev->srq_hash, &srq->hash_entry, srq->qplib_srq.id);
			srq->uctx_srq_page = (void *)get_zeroed_page(GFP_KERNEL);
			if (!srq->uctx_srq_page) {
				rc = -ENOMEM;
				goto fail;
			}
			resp.comp_mask |= BNXT_RE_SRQ_TOGGLE_PAGE_SUPPORT;
		}
		rc = ib_copy_to_udata(udata, &resp, sizeof(resp));
		if (rc) {
			ibdev_err(&rdev->ibdev, "SRQ copy to udata failed!");
			bnxt_qplib_destroy_srq(&rdev->qplib_res,
					       &srq->qplib_srq);
			goto fail;
		}
	}
	if (nq)
		nq->budget++;
	active_srqs = atomic_inc_return(&rdev->stats.res.srq_count);
	if (active_srqs > rdev->stats.res.srq_watermark)
		rdev->stats.res.srq_watermark = active_srqs;
	spin_lock_init(&srq->lock);

	return 0;

fail:
	ib_umem_release(srq->umem);
exit:
	return rc;
}

int bnxt_re_modify_srq(struct ib_srq *ib_srq, struct ib_srq_attr *srq_attr,
		       enum ib_srq_attr_mask srq_attr_mask,
		       struct ib_udata *udata)
{
	struct bnxt_re_srq *srq = container_of(ib_srq, struct bnxt_re_srq,
					       ib_srq);
	struct bnxt_re_dev *rdev = srq->rdev;
	int rc;

	switch (srq_attr_mask) {
	case IB_SRQ_MAX_WR:
		/* SRQ resize is not supported */
		return -EINVAL;
	case IB_SRQ_LIMIT:
		/* Change the SRQ threshold */
		if (srq_attr->srq_limit > srq->qplib_srq.max_wqe)
			return -EINVAL;

		srq->qplib_srq.threshold = srq_attr->srq_limit;
		rc = bnxt_qplib_modify_srq(&rdev->qplib_res, &srq->qplib_srq);
		if (rc) {
			ibdev_err(&rdev->ibdev, "Modify HW SRQ failed!");
			return rc;
		}
		/* On success, update the shadow */
		srq->srq_limit = srq_attr->srq_limit;
		/* No need to Build and send response back to udata */
		return 0;
	default:
		ibdev_err(&rdev->ibdev,
			  "Unsupported srq_attr_mask 0x%x", srq_attr_mask);
		return -EINVAL;
	}
}

int bnxt_re_query_srq(struct ib_srq *ib_srq, struct ib_srq_attr *srq_attr)
{
	struct bnxt_re_srq *srq = container_of(ib_srq, struct bnxt_re_srq,
					       ib_srq);
	struct bnxt_re_srq tsrq;
	struct bnxt_re_dev *rdev = srq->rdev;
	int rc;

	/* Get live SRQ attr */
	tsrq.qplib_srq.id = srq->qplib_srq.id;
	rc = bnxt_qplib_query_srq(&rdev->qplib_res, &tsrq.qplib_srq);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Query HW SRQ failed!");
		return rc;
	}
	srq_attr->max_wr = srq->qplib_srq.max_wqe;
	srq_attr->max_sge = srq->qplib_srq.max_sge;
	srq_attr->srq_limit = tsrq.qplib_srq.threshold;

	return 0;
}

int bnxt_re_post_srq_recv(struct ib_srq *ib_srq, const struct ib_recv_wr *wr,
			  const struct ib_recv_wr **bad_wr)
{
	struct bnxt_re_srq *srq = container_of(ib_srq, struct bnxt_re_srq,
					       ib_srq);
	struct bnxt_qplib_swqe wqe;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&srq->lock, flags);
	while (wr) {
		/* Transcribe each ib_recv_wr to qplib_swqe */
		wqe.num_sge = wr->num_sge;
		bnxt_re_build_sgl(wr->sg_list, wqe.sg_list, wr->num_sge);
		wqe.wr_id = wr->wr_id;
		wqe.type = BNXT_QPLIB_SWQE_TYPE_RECV;

		rc = bnxt_qplib_post_srq_recv(&srq->qplib_srq, &wqe);
		if (rc) {
			*bad_wr = wr;
			break;
		}
		wr = wr->next;
	}
	spin_unlock_irqrestore(&srq->lock, flags);

	return rc;
}
static int bnxt_re_modify_shadow_qp(struct bnxt_re_dev *rdev,
				    struct bnxt_re_qp *qp1_qp,
				    int qp_attr_mask)
{
	struct bnxt_re_qp *qp = rdev->gsi_ctx.gsi_sqp;
	int rc;

	if (qp_attr_mask & IB_QP_STATE) {
		qp->qplib_qp.modify_flags |= CMDQ_MODIFY_QP_MODIFY_MASK_STATE;
		qp->qplib_qp.state = qp1_qp->qplib_qp.state;
	}
	if (qp_attr_mask & IB_QP_PKEY_INDEX) {
		qp->qplib_qp.modify_flags |= CMDQ_MODIFY_QP_MODIFY_MASK_PKEY;
		qp->qplib_qp.pkey_index = qp1_qp->qplib_qp.pkey_index;
	}

	if (qp_attr_mask & IB_QP_QKEY) {
		qp->qplib_qp.modify_flags |= CMDQ_MODIFY_QP_MODIFY_MASK_QKEY;
		/* Using a Random  QKEY */
		qp->qplib_qp.qkey = 0x81818181;
	}
	if (qp_attr_mask & IB_QP_SQ_PSN) {
		qp->qplib_qp.modify_flags |= CMDQ_MODIFY_QP_MODIFY_MASK_SQ_PSN;
		qp->qplib_qp.sq.psn = qp1_qp->qplib_qp.sq.psn;
	}

	rc = bnxt_qplib_modify_qp(&rdev->qplib_res, &qp->qplib_qp);
	if (rc)
		ibdev_err(&rdev->ibdev, "Failed to modify Shadow QP for QP1");
	return rc;
}

int bnxt_re_modify_qp(struct ib_qp *ib_qp, struct ib_qp_attr *qp_attr,
		      int qp_attr_mask, struct ib_udata *udata)
{
	struct bnxt_re_qp *qp = container_of(ib_qp, struct bnxt_re_qp, ib_qp);
	struct bnxt_re_dev *rdev = qp->rdev;
	struct bnxt_qplib_dev_attr *dev_attr = &rdev->dev_attr;
	enum ib_qp_state curr_qp_state, new_qp_state;
	int rc, entries;
	unsigned int flags;
	u8 nw_type;

	if (qp_attr_mask & ~IB_QP_ATTR_STANDARD_BITS)
		return -EOPNOTSUPP;

	qp->qplib_qp.modify_flags = 0;
	if (qp_attr_mask & IB_QP_STATE) {
		curr_qp_state = __to_ib_qp_state(qp->qplib_qp.cur_qp_state);
		new_qp_state = qp_attr->qp_state;
		if (!ib_modify_qp_is_ok(curr_qp_state, new_qp_state,
					ib_qp->qp_type, qp_attr_mask)) {
			ibdev_err(&rdev->ibdev,
				  "Invalid attribute mask: %#x specified ",
				  qp_attr_mask);
			ibdev_err(&rdev->ibdev,
				  "for qpn: %#x type: %#x",
				  ib_qp->qp_num, ib_qp->qp_type);
			ibdev_err(&rdev->ibdev,
				  "curr_qp_state=0x%x, new_qp_state=0x%x\n",
				  curr_qp_state, new_qp_state);
			return -EINVAL;
		}
		qp->qplib_qp.modify_flags |= CMDQ_MODIFY_QP_MODIFY_MASK_STATE;
		qp->qplib_qp.state = __from_ib_qp_state(qp_attr->qp_state);

		if (!qp->sumem &&
		    qp->qplib_qp.state == CMDQ_MODIFY_QP_NEW_STATE_ERR) {
			ibdev_dbg(&rdev->ibdev,
				  "Move QP = %p to flush list\n", qp);
			flags = bnxt_re_lock_cqs(qp);
			bnxt_qplib_add_flush_qp(&qp->qplib_qp);
			bnxt_re_unlock_cqs(qp, flags);
		}
		if (!qp->sumem &&
		    qp->qplib_qp.state == CMDQ_MODIFY_QP_NEW_STATE_RESET) {
			ibdev_dbg(&rdev->ibdev,
				  "Move QP = %p out of flush list\n", qp);
			flags = bnxt_re_lock_cqs(qp);
			bnxt_qplib_clean_qp(&qp->qplib_qp);
			bnxt_re_unlock_cqs(qp, flags);
		}
	}
	if (qp_attr_mask & IB_QP_EN_SQD_ASYNC_NOTIFY) {
		qp->qplib_qp.modify_flags |=
				CMDQ_MODIFY_QP_MODIFY_MASK_EN_SQD_ASYNC_NOTIFY;
		qp->qplib_qp.en_sqd_async_notify = true;
	}
	if (qp_attr_mask & IB_QP_ACCESS_FLAGS) {
		qp->qplib_qp.modify_flags |= CMDQ_MODIFY_QP_MODIFY_MASK_ACCESS;
		qp->qplib_qp.access =
			__from_ib_access_flags(qp_attr->qp_access_flags);
		/* LOCAL_WRITE access must be set to allow RC receive */
		qp->qplib_qp.access |= BNXT_QPLIB_ACCESS_LOCAL_WRITE;
		/* Temp: Set all params on QP as of now */
		qp->qplib_qp.access |= CMDQ_MODIFY_QP_ACCESS_REMOTE_WRITE;
		qp->qplib_qp.access |= CMDQ_MODIFY_QP_ACCESS_REMOTE_READ;
	}
	if (qp_attr_mask & IB_QP_PKEY_INDEX) {
		qp->qplib_qp.modify_flags |= CMDQ_MODIFY_QP_MODIFY_MASK_PKEY;
		qp->qplib_qp.pkey_index = qp_attr->pkey_index;
	}
	if (qp_attr_mask & IB_QP_QKEY) {
		qp->qplib_qp.modify_flags |= CMDQ_MODIFY_QP_MODIFY_MASK_QKEY;
		qp->qplib_qp.qkey = qp_attr->qkey;
	}
	if (qp_attr_mask & IB_QP_AV) {
		const struct ib_global_route *grh =
			rdma_ah_read_grh(&qp_attr->ah_attr);
		const struct ib_gid_attr *sgid_attr;
		struct bnxt_re_gid_ctx *ctx;

		qp->qplib_qp.modify_flags |= CMDQ_MODIFY_QP_MODIFY_MASK_DGID |
				     CMDQ_MODIFY_QP_MODIFY_MASK_FLOW_LABEL |
				     CMDQ_MODIFY_QP_MODIFY_MASK_SGID_INDEX |
				     CMDQ_MODIFY_QP_MODIFY_MASK_HOP_LIMIT |
				     CMDQ_MODIFY_QP_MODIFY_MASK_TRAFFIC_CLASS |
				     CMDQ_MODIFY_QP_MODIFY_MASK_DEST_MAC |
				     CMDQ_MODIFY_QP_MODIFY_MASK_VLAN_ID;
		memcpy(qp->qplib_qp.ah.dgid.data, grh->dgid.raw,
		       sizeof(qp->qplib_qp.ah.dgid.data));
		qp->qplib_qp.ah.flow_label = grh->flow_label;
		sgid_attr = grh->sgid_attr;
		/* Get the HW context of the GID. The reference
		 * of GID table entry is already taken by the caller.
		 */
		ctx = rdma_read_gid_hw_context(sgid_attr);
		qp->qplib_qp.ah.sgid_index = ctx->idx;
		qp->qplib_qp.ah.host_sgid_index = grh->sgid_index;
		qp->qplib_qp.ah.hop_limit = grh->hop_limit;
		qp->qplib_qp.ah.traffic_class = grh->traffic_class;
		qp->qplib_qp.ah.sl = rdma_ah_get_sl(&qp_attr->ah_attr);
		ether_addr_copy(qp->qplib_qp.ah.dmac,
				qp_attr->ah_attr.roce.dmac);

		rc = rdma_read_gid_l2_fields(sgid_attr, NULL,
					     &qp->qplib_qp.smac[0]);
		if (rc)
			return rc;

		nw_type = rdma_gid_attr_network_type(sgid_attr);
		switch (nw_type) {
		case RDMA_NETWORK_IPV4:
			qp->qplib_qp.nw_type =
				CMDQ_MODIFY_QP_NETWORK_TYPE_ROCEV2_IPV4;
			break;
		case RDMA_NETWORK_IPV6:
			qp->qplib_qp.nw_type =
				CMDQ_MODIFY_QP_NETWORK_TYPE_ROCEV2_IPV6;
			break;
		default:
			qp->qplib_qp.nw_type =
				CMDQ_MODIFY_QP_NETWORK_TYPE_ROCEV1;
			break;
		}
	}

	if (qp_attr_mask & IB_QP_PATH_MTU) {
		qp->qplib_qp.modify_flags |=
				CMDQ_MODIFY_QP_MODIFY_MASK_PATH_MTU;
		qp->qplib_qp.path_mtu = __from_ib_mtu(qp_attr->path_mtu);
		qp->qplib_qp.mtu = ib_mtu_enum_to_int(qp_attr->path_mtu);
	} else if (qp_attr->qp_state == IB_QPS_RTR) {
		qp->qplib_qp.modify_flags |=
			CMDQ_MODIFY_QP_MODIFY_MASK_PATH_MTU;
		qp->qplib_qp.path_mtu =
			__from_ib_mtu(iboe_get_mtu(rdev->netdev->mtu));
		qp->qplib_qp.mtu =
			ib_mtu_enum_to_int(iboe_get_mtu(rdev->netdev->mtu));
	}

	if (qp_attr_mask & IB_QP_TIMEOUT) {
		qp->qplib_qp.modify_flags |= CMDQ_MODIFY_QP_MODIFY_MASK_TIMEOUT;
		qp->qplib_qp.timeout = qp_attr->timeout;
	}
	if (qp_attr_mask & IB_QP_RETRY_CNT) {
		qp->qplib_qp.modify_flags |=
				CMDQ_MODIFY_QP_MODIFY_MASK_RETRY_CNT;
		qp->qplib_qp.retry_cnt = qp_attr->retry_cnt;
	}
	if (qp_attr_mask & IB_QP_RNR_RETRY) {
		qp->qplib_qp.modify_flags |=
				CMDQ_MODIFY_QP_MODIFY_MASK_RNR_RETRY;
		qp->qplib_qp.rnr_retry = qp_attr->rnr_retry;
	}
	if (qp_attr_mask & IB_QP_MIN_RNR_TIMER) {
		qp->qplib_qp.modify_flags |=
				CMDQ_MODIFY_QP_MODIFY_MASK_MIN_RNR_TIMER;
		qp->qplib_qp.min_rnr_timer = qp_attr->min_rnr_timer;
	}
	if (qp_attr_mask & IB_QP_RQ_PSN) {
		qp->qplib_qp.modify_flags |= CMDQ_MODIFY_QP_MODIFY_MASK_RQ_PSN;
		qp->qplib_qp.rq.psn = qp_attr->rq_psn;
	}
	if (qp_attr_mask & IB_QP_MAX_QP_RD_ATOMIC) {
		qp->qplib_qp.modify_flags |=
				CMDQ_MODIFY_QP_MODIFY_MASK_MAX_RD_ATOMIC;
		/* Cap the max_rd_atomic to device max */
		qp->qplib_qp.max_rd_atomic = min_t(u32, qp_attr->max_rd_atomic,
						   dev_attr->max_qp_rd_atom);
	}
	if (qp_attr_mask & IB_QP_SQ_PSN) {
		qp->qplib_qp.modify_flags |= CMDQ_MODIFY_QP_MODIFY_MASK_SQ_PSN;
		qp->qplib_qp.sq.psn = qp_attr->sq_psn;
	}
	if (qp_attr_mask & IB_QP_MAX_DEST_RD_ATOMIC) {
		if (qp_attr->max_dest_rd_atomic >
		    dev_attr->max_qp_init_rd_atom) {
			ibdev_err(&rdev->ibdev,
				  "max_dest_rd_atomic requested%d is > dev_max%d",
				  qp_attr->max_dest_rd_atomic,
				  dev_attr->max_qp_init_rd_atom);
			return -EINVAL;
		}

		qp->qplib_qp.modify_flags |=
				CMDQ_MODIFY_QP_MODIFY_MASK_MAX_DEST_RD_ATOMIC;
		qp->qplib_qp.max_dest_rd_atomic = qp_attr->max_dest_rd_atomic;
	}
	if (qp_attr_mask & IB_QP_CAP) {
		struct bnxt_re_ucontext *uctx =
			rdma_udata_to_drv_context(udata, struct bnxt_re_ucontext, ib_uctx);

		qp->qplib_qp.modify_flags |=
				CMDQ_MODIFY_QP_MODIFY_MASK_SQ_SIZE |
				CMDQ_MODIFY_QP_MODIFY_MASK_RQ_SIZE |
				CMDQ_MODIFY_QP_MODIFY_MASK_SQ_SGE |
				CMDQ_MODIFY_QP_MODIFY_MASK_RQ_SGE |
				CMDQ_MODIFY_QP_MODIFY_MASK_MAX_INLINE_DATA;
		if ((qp_attr->cap.max_send_wr >= dev_attr->max_qp_wqes) ||
		    (qp_attr->cap.max_recv_wr >= dev_attr->max_qp_wqes) ||
		    (qp_attr->cap.max_send_sge >= dev_attr->max_qp_sges) ||
		    (qp_attr->cap.max_recv_sge >= dev_attr->max_qp_sges) ||
		    (qp_attr->cap.max_inline_data >=
						dev_attr->max_inline_data)) {
			ibdev_err(&rdev->ibdev,
				  "Create QP failed - max exceeded");
			return -EINVAL;
		}
		entries = bnxt_re_init_depth(qp_attr->cap.max_send_wr, uctx);
		qp->qplib_qp.sq.max_wqe = min_t(u32, entries,
						dev_attr->max_qp_wqes + 1);
		qp->qplib_qp.sq.q_full_delta = qp->qplib_qp.sq.max_wqe -
						qp_attr->cap.max_send_wr;
		/*
		 * Reserving one slot for Phantom WQE. Some application can
		 * post one extra entry in this case. Allowing this to avoid
		 * unexpected Queue full condition
		 */
		qp->qplib_qp.sq.q_full_delta -= 1;
		qp->qplib_qp.sq.max_sge = qp_attr->cap.max_send_sge;
		if (qp->qplib_qp.rq.max_wqe) {
			entries = bnxt_re_init_depth(qp_attr->cap.max_recv_wr, uctx);
			qp->qplib_qp.rq.max_wqe =
				min_t(u32, entries, dev_attr->max_qp_wqes + 1);
			qp->qplib_qp.rq.max_sw_wqe = qp->qplib_qp.rq.max_wqe;
			qp->qplib_qp.rq.q_full_delta = qp->qplib_qp.rq.max_wqe -
						       qp_attr->cap.max_recv_wr;
			qp->qplib_qp.rq.max_sge = qp_attr->cap.max_recv_sge;
		} else {
			/* SRQ was used prior, just ignore the RQ caps */
		}
	}
	if (qp_attr_mask & IB_QP_DEST_QPN) {
		qp->qplib_qp.modify_flags |=
				CMDQ_MODIFY_QP_MODIFY_MASK_DEST_QP_ID;
		qp->qplib_qp.dest_qpn = qp_attr->dest_qp_num;
	}
	rc = bnxt_qplib_modify_qp(&rdev->qplib_res, &qp->qplib_qp);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to modify HW QP");
		return rc;
	}
	if (ib_qp->qp_type == IB_QPT_GSI && rdev->gsi_ctx.gsi_sqp)
		rc = bnxt_re_modify_shadow_qp(rdev, qp, qp_attr_mask);
	return rc;
}

int bnxt_re_query_qp(struct ib_qp *ib_qp, struct ib_qp_attr *qp_attr,
		     int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr)
{
	struct bnxt_re_qp *qp = container_of(ib_qp, struct bnxt_re_qp, ib_qp);
	struct bnxt_re_dev *rdev = qp->rdev;
	struct bnxt_qplib_qp *qplib_qp;
	int rc;

	qplib_qp = kzalloc(sizeof(*qplib_qp), GFP_KERNEL);
	if (!qplib_qp)
		return -ENOMEM;

	qplib_qp->id = qp->qplib_qp.id;
	qplib_qp->ah.host_sgid_index = qp->qplib_qp.ah.host_sgid_index;

	rc = bnxt_qplib_query_qp(&rdev->qplib_res, qplib_qp);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to query HW QP");
		goto out;
	}
	qp_attr->qp_state = __to_ib_qp_state(qplib_qp->state);
	qp_attr->cur_qp_state = __to_ib_qp_state(qplib_qp->cur_qp_state);
	qp_attr->en_sqd_async_notify = qplib_qp->en_sqd_async_notify ? 1 : 0;
	qp_attr->qp_access_flags = __to_ib_access_flags(qplib_qp->access);
	qp_attr->pkey_index = qplib_qp->pkey_index;
	qp_attr->qkey = qplib_qp->qkey;
	qp_attr->ah_attr.type = RDMA_AH_ATTR_TYPE_ROCE;
	rdma_ah_set_grh(&qp_attr->ah_attr, NULL, qplib_qp->ah.flow_label,
			qplib_qp->ah.host_sgid_index,
			qplib_qp->ah.hop_limit,
			qplib_qp->ah.traffic_class);
	rdma_ah_set_dgid_raw(&qp_attr->ah_attr, qplib_qp->ah.dgid.data);
	rdma_ah_set_sl(&qp_attr->ah_attr, qplib_qp->ah.sl);
	ether_addr_copy(qp_attr->ah_attr.roce.dmac, qplib_qp->ah.dmac);
	qp_attr->path_mtu = __to_ib_mtu(qplib_qp->path_mtu);
	qp_attr->timeout = qplib_qp->timeout;
	qp_attr->retry_cnt = qplib_qp->retry_cnt;
	qp_attr->rnr_retry = qplib_qp->rnr_retry;
	qp_attr->min_rnr_timer = qplib_qp->min_rnr_timer;
	qp_attr->rq_psn = qplib_qp->rq.psn;
	qp_attr->max_rd_atomic = qplib_qp->max_rd_atomic;
	qp_attr->sq_psn = qplib_qp->sq.psn;
	qp_attr->max_dest_rd_atomic = qplib_qp->max_dest_rd_atomic;
	qp_init_attr->sq_sig_type = qplib_qp->sig_type ? IB_SIGNAL_ALL_WR :
							 IB_SIGNAL_REQ_WR;
	qp_attr->dest_qp_num = qplib_qp->dest_qpn;

	qp_attr->cap.max_send_wr = qp->qplib_qp.sq.max_wqe;
	qp_attr->cap.max_send_sge = qp->qplib_qp.sq.max_sge;
	qp_attr->cap.max_recv_wr = qp->qplib_qp.rq.max_wqe;
	qp_attr->cap.max_recv_sge = qp->qplib_qp.rq.max_sge;
	qp_attr->cap.max_inline_data = qp->qplib_qp.max_inline_data;
	qp_init_attr->cap = qp_attr->cap;

out:
	kfree(qplib_qp);
	return rc;
}

/* Routine for sending QP1 packets for RoCE V1 an V2
 */
static int bnxt_re_build_qp1_send_v2(struct bnxt_re_qp *qp,
				     const struct ib_send_wr *wr,
				     struct bnxt_qplib_swqe *wqe,
				     int payload_size)
{
	struct bnxt_re_ah *ah = container_of(ud_wr(wr)->ah, struct bnxt_re_ah,
					     ib_ah);
	struct bnxt_qplib_ah *qplib_ah = &ah->qplib_ah;
	const struct ib_gid_attr *sgid_attr = ah->ib_ah.sgid_attr;
	struct bnxt_qplib_sge sge;
	u8 nw_type;
	u16 ether_type;
	union ib_gid dgid;
	bool is_eth = false;
	bool is_vlan = false;
	bool is_grh = false;
	bool is_udp = false;
	u8 ip_version = 0;
	u16 vlan_id = 0xFFFF;
	void *buf;
	int i, rc;

	memset(&qp->qp1_hdr, 0, sizeof(qp->qp1_hdr));

	rc = rdma_read_gid_l2_fields(sgid_attr, &vlan_id, NULL);
	if (rc)
		return rc;

	/* Get network header type for this GID */
	nw_type = rdma_gid_attr_network_type(sgid_attr);
	switch (nw_type) {
	case RDMA_NETWORK_IPV4:
		nw_type = BNXT_RE_ROCEV2_IPV4_PACKET;
		break;
	case RDMA_NETWORK_IPV6:
		nw_type = BNXT_RE_ROCEV2_IPV6_PACKET;
		break;
	default:
		nw_type = BNXT_RE_ROCE_V1_PACKET;
		break;
	}
	memcpy(&dgid.raw, &qplib_ah->dgid, 16);
	is_udp = sgid_attr->gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP;
	if (is_udp) {
		if (ipv6_addr_v4mapped((struct in6_addr *)&sgid_attr->gid)) {
			ip_version = 4;
			ether_type = ETH_P_IP;
		} else {
			ip_version = 6;
			ether_type = ETH_P_IPV6;
		}
		is_grh = false;
	} else {
		ether_type = ETH_P_IBOE;
		is_grh = true;
	}

	is_eth = true;
	is_vlan = vlan_id && (vlan_id < 0x1000);

	ib_ud_header_init(payload_size, !is_eth, is_eth, is_vlan, is_grh,
			  ip_version, is_udp, 0, &qp->qp1_hdr);

	/* ETH */
	ether_addr_copy(qp->qp1_hdr.eth.dmac_h, ah->qplib_ah.dmac);
	ether_addr_copy(qp->qp1_hdr.eth.smac_h, qp->qplib_qp.smac);

	/* For vlan, check the sgid for vlan existence */

	if (!is_vlan) {
		qp->qp1_hdr.eth.type = cpu_to_be16(ether_type);
	} else {
		qp->qp1_hdr.vlan.type = cpu_to_be16(ether_type);
		qp->qp1_hdr.vlan.tag = cpu_to_be16(vlan_id);
	}

	if (is_grh || (ip_version == 6)) {
		memcpy(qp->qp1_hdr.grh.source_gid.raw, sgid_attr->gid.raw,
		       sizeof(sgid_attr->gid));
		memcpy(qp->qp1_hdr.grh.destination_gid.raw, qplib_ah->dgid.data,
		       sizeof(sgid_attr->gid));
		qp->qp1_hdr.grh.hop_limit     = qplib_ah->hop_limit;
	}

	if (ip_version == 4) {
		qp->qp1_hdr.ip4.tos = 0;
		qp->qp1_hdr.ip4.id = 0;
		qp->qp1_hdr.ip4.frag_off = htons(IP_DF);
		qp->qp1_hdr.ip4.ttl = qplib_ah->hop_limit;

		memcpy(&qp->qp1_hdr.ip4.saddr, sgid_attr->gid.raw + 12, 4);
		memcpy(&qp->qp1_hdr.ip4.daddr, qplib_ah->dgid.data + 12, 4);
		qp->qp1_hdr.ip4.check = ib_ud_ip4_csum(&qp->qp1_hdr);
	}

	if (is_udp) {
		qp->qp1_hdr.udp.dport = htons(ROCE_V2_UDP_DPORT);
		qp->qp1_hdr.udp.sport = htons(0x8CD1);
		qp->qp1_hdr.udp.csum = 0;
	}

	/* BTH */
	if (wr->opcode == IB_WR_SEND_WITH_IMM) {
		qp->qp1_hdr.bth.opcode = IB_OPCODE_UD_SEND_ONLY_WITH_IMMEDIATE;
		qp->qp1_hdr.immediate_present = 1;
	} else {
		qp->qp1_hdr.bth.opcode = IB_OPCODE_UD_SEND_ONLY;
	}
	if (wr->send_flags & IB_SEND_SOLICITED)
		qp->qp1_hdr.bth.solicited_event = 1;
	/* pad_count */
	qp->qp1_hdr.bth.pad_count = (4 - payload_size) & 3;

	/* P_key for QP1 is for all members */
	qp->qp1_hdr.bth.pkey = cpu_to_be16(0xFFFF);
	qp->qp1_hdr.bth.destination_qpn = IB_QP1;
	qp->qp1_hdr.bth.ack_req = 0;
	qp->send_psn++;
	qp->send_psn &= BTH_PSN_MASK;
	qp->qp1_hdr.bth.psn = cpu_to_be32(qp->send_psn);
	/* DETH */
	/* Use the priviledged Q_Key for QP1 */
	qp->qp1_hdr.deth.qkey = cpu_to_be32(IB_QP1_QKEY);
	qp->qp1_hdr.deth.source_qpn = IB_QP1;

	/* Pack the QP1 to the transmit buffer */
	buf = bnxt_qplib_get_qp1_sq_buf(&qp->qplib_qp, &sge);
	if (buf) {
		ib_ud_header_pack(&qp->qp1_hdr, buf);
		for (i = wqe->num_sge; i; i--) {
			wqe->sg_list[i].addr = wqe->sg_list[i - 1].addr;
			wqe->sg_list[i].lkey = wqe->sg_list[i - 1].lkey;
			wqe->sg_list[i].size = wqe->sg_list[i - 1].size;
		}

		/*
		 * Max Header buf size for IPV6 RoCE V2 is 86,
		 * which is same as the QP1 SQ header buffer.
		 * Header buf size for IPV4 RoCE V2 can be 66.
		 * ETH(14) + VLAN(4)+ IP(20) + UDP (8) + BTH(20).
		 * Subtract 20 bytes from QP1 SQ header buf size
		 */
		if (is_udp && ip_version == 4)
			sge.size -= 20;
		/*
		 * Max Header buf size for RoCE V1 is 78.
		 * ETH(14) + VLAN(4) + GRH(40) + BTH(20).
		 * Subtract 8 bytes from QP1 SQ header buf size
		 */
		if (!is_udp)
			sge.size -= 8;

		/* Subtract 4 bytes for non vlan packets */
		if (!is_vlan)
			sge.size -= 4;

		wqe->sg_list[0].addr = sge.addr;
		wqe->sg_list[0].lkey = sge.lkey;
		wqe->sg_list[0].size = sge.size;
		wqe->num_sge++;

	} else {
		ibdev_err(&qp->rdev->ibdev, "QP1 buffer is empty!");
		rc = -ENOMEM;
	}
	return rc;
}

/* For the MAD layer, it only provides the recv SGE the size of
 * ib_grh + MAD datagram.  No Ethernet headers, Ethertype, BTH, DETH,
 * nor RoCE iCRC.  The Cu+ solution must provide buffer for the entire
 * receive packet (334 bytes) with no VLAN and then copy the GRH
 * and the MAD datagram out to the provided SGE.
 */
static int bnxt_re_build_qp1_shadow_qp_recv(struct bnxt_re_qp *qp,
					    const struct ib_recv_wr *wr,
					    struct bnxt_qplib_swqe *wqe,
					    int payload_size)
{
	struct bnxt_re_sqp_entries *sqp_entry;
	struct bnxt_qplib_sge ref, sge;
	struct bnxt_re_dev *rdev;
	u32 rq_prod_index;

	rdev = qp->rdev;

	rq_prod_index = bnxt_qplib_get_rq_prod_index(&qp->qplib_qp);

	if (!bnxt_qplib_get_qp1_rq_buf(&qp->qplib_qp, &sge))
		return -ENOMEM;

	/* Create 1 SGE to receive the entire
	 * ethernet packet
	 */
	/* Save the reference from ULP */
	ref.addr = wqe->sg_list[0].addr;
	ref.lkey = wqe->sg_list[0].lkey;
	ref.size = wqe->sg_list[0].size;

	sqp_entry = &rdev->gsi_ctx.sqp_tbl[rq_prod_index];

	/* SGE 1 */
	wqe->sg_list[0].addr = sge.addr;
	wqe->sg_list[0].lkey = sge.lkey;
	wqe->sg_list[0].size = BNXT_QPLIB_MAX_QP1_RQ_HDR_SIZE_V2;
	sge.size -= wqe->sg_list[0].size;

	sqp_entry->sge.addr = ref.addr;
	sqp_entry->sge.lkey = ref.lkey;
	sqp_entry->sge.size = ref.size;
	/* Store the wrid for reporting completion */
	sqp_entry->wrid = wqe->wr_id;
	/* change the wqe->wrid to table index */
	wqe->wr_id = rq_prod_index;
	return 0;
}

static int is_ud_qp(struct bnxt_re_qp *qp)
{
	return (qp->qplib_qp.type == CMDQ_CREATE_QP_TYPE_UD ||
		qp->qplib_qp.type == CMDQ_CREATE_QP_TYPE_GSI);
}

static int bnxt_re_build_send_wqe(struct bnxt_re_qp *qp,
				  const struct ib_send_wr *wr,
				  struct bnxt_qplib_swqe *wqe)
{
	struct bnxt_re_ah *ah = NULL;

	if (is_ud_qp(qp)) {
		ah = container_of(ud_wr(wr)->ah, struct bnxt_re_ah, ib_ah);
		wqe->send.q_key = ud_wr(wr)->remote_qkey;
		wqe->send.dst_qp = ud_wr(wr)->remote_qpn;
		wqe->send.avid = ah->qplib_ah.id;
	}
	switch (wr->opcode) {
	case IB_WR_SEND:
		wqe->type = BNXT_QPLIB_SWQE_TYPE_SEND;
		break;
	case IB_WR_SEND_WITH_IMM:
		wqe->type = BNXT_QPLIB_SWQE_TYPE_SEND_WITH_IMM;
		wqe->send.imm_data = be32_to_cpu(wr->ex.imm_data);
		break;
	case IB_WR_SEND_WITH_INV:
		wqe->type = BNXT_QPLIB_SWQE_TYPE_SEND_WITH_INV;
		wqe->send.inv_key = wr->ex.invalidate_rkey;
		break;
	default:
		return -EINVAL;
	}
	if (wr->send_flags & IB_SEND_SIGNALED)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_SIGNAL_COMP;
	if (wr->send_flags & IB_SEND_FENCE)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_UC_FENCE;
	if (wr->send_flags & IB_SEND_SOLICITED)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_SOLICIT_EVENT;
	if (wr->send_flags & IB_SEND_INLINE)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_INLINE;

	return 0;
}

static int bnxt_re_build_rdma_wqe(const struct ib_send_wr *wr,
				  struct bnxt_qplib_swqe *wqe)
{
	switch (wr->opcode) {
	case IB_WR_RDMA_WRITE:
		wqe->type = BNXT_QPLIB_SWQE_TYPE_RDMA_WRITE;
		break;
	case IB_WR_RDMA_WRITE_WITH_IMM:
		wqe->type = BNXT_QPLIB_SWQE_TYPE_RDMA_WRITE_WITH_IMM;
		wqe->rdma.imm_data = be32_to_cpu(wr->ex.imm_data);
		break;
	case IB_WR_RDMA_READ:
		wqe->type = BNXT_QPLIB_SWQE_TYPE_RDMA_READ;
		wqe->rdma.inv_key = wr->ex.invalidate_rkey;
		break;
	default:
		return -EINVAL;
	}
	wqe->rdma.remote_va = rdma_wr(wr)->remote_addr;
	wqe->rdma.r_key = rdma_wr(wr)->rkey;
	if (wr->send_flags & IB_SEND_SIGNALED)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_SIGNAL_COMP;
	if (wr->send_flags & IB_SEND_FENCE)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_UC_FENCE;
	if (wr->send_flags & IB_SEND_SOLICITED)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_SOLICIT_EVENT;
	if (wr->send_flags & IB_SEND_INLINE)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_INLINE;

	return 0;
}

static int bnxt_re_build_atomic_wqe(const struct ib_send_wr *wr,
				    struct bnxt_qplib_swqe *wqe)
{
	switch (wr->opcode) {
	case IB_WR_ATOMIC_CMP_AND_SWP:
		wqe->type = BNXT_QPLIB_SWQE_TYPE_ATOMIC_CMP_AND_SWP;
		wqe->atomic.cmp_data = atomic_wr(wr)->compare_add;
		wqe->atomic.swap_data = atomic_wr(wr)->swap;
		break;
	case IB_WR_ATOMIC_FETCH_AND_ADD:
		wqe->type = BNXT_QPLIB_SWQE_TYPE_ATOMIC_FETCH_AND_ADD;
		wqe->atomic.cmp_data = atomic_wr(wr)->compare_add;
		break;
	default:
		return -EINVAL;
	}
	wqe->atomic.remote_va = atomic_wr(wr)->remote_addr;
	wqe->atomic.r_key = atomic_wr(wr)->rkey;
	if (wr->send_flags & IB_SEND_SIGNALED)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_SIGNAL_COMP;
	if (wr->send_flags & IB_SEND_FENCE)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_UC_FENCE;
	if (wr->send_flags & IB_SEND_SOLICITED)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_SOLICIT_EVENT;
	return 0;
}

static int bnxt_re_build_inv_wqe(const struct ib_send_wr *wr,
				 struct bnxt_qplib_swqe *wqe)
{
	wqe->type = BNXT_QPLIB_SWQE_TYPE_LOCAL_INV;
	wqe->local_inv.inv_l_key = wr->ex.invalidate_rkey;

	if (wr->send_flags & IB_SEND_SIGNALED)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_SIGNAL_COMP;
	if (wr->send_flags & IB_SEND_SOLICITED)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_SOLICIT_EVENT;

	return 0;
}

static int bnxt_re_build_reg_wqe(const struct ib_reg_wr *wr,
				 struct bnxt_qplib_swqe *wqe)
{
	struct bnxt_re_mr *mr = container_of(wr->mr, struct bnxt_re_mr, ib_mr);
	struct bnxt_qplib_frpl *qplib_frpl = &mr->qplib_frpl;
	int access = wr->access;

	wqe->frmr.pbl_ptr = (__le64 *)qplib_frpl->hwq.pbl_ptr[0];
	wqe->frmr.pbl_dma_ptr = qplib_frpl->hwq.pbl_dma_ptr[0];
	wqe->frmr.page_list = mr->pages;
	wqe->frmr.page_list_len = mr->npages;
	wqe->frmr.levels = qplib_frpl->hwq.level;
	wqe->type = BNXT_QPLIB_SWQE_TYPE_REG_MR;

	if (wr->wr.send_flags & IB_SEND_SIGNALED)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_SIGNAL_COMP;

	if (access & IB_ACCESS_LOCAL_WRITE)
		wqe->frmr.access_cntl |= SQ_FR_PMR_ACCESS_CNTL_LOCAL_WRITE;
	if (access & IB_ACCESS_REMOTE_READ)
		wqe->frmr.access_cntl |= SQ_FR_PMR_ACCESS_CNTL_REMOTE_READ;
	if (access & IB_ACCESS_REMOTE_WRITE)
		wqe->frmr.access_cntl |= SQ_FR_PMR_ACCESS_CNTL_REMOTE_WRITE;
	if (access & IB_ACCESS_REMOTE_ATOMIC)
		wqe->frmr.access_cntl |= SQ_FR_PMR_ACCESS_CNTL_REMOTE_ATOMIC;
	if (access & IB_ACCESS_MW_BIND)
		wqe->frmr.access_cntl |= SQ_FR_PMR_ACCESS_CNTL_WINDOW_BIND;

	wqe->frmr.l_key = wr->key;
	wqe->frmr.length = wr->mr->length;
	wqe->frmr.pbl_pg_sz_log = ilog2(PAGE_SIZE >> PAGE_SHIFT_4K);
	wqe->frmr.pg_sz_log = ilog2(wr->mr->page_size >> PAGE_SHIFT_4K);
	wqe->frmr.va = wr->mr->iova;
	return 0;
}

static int bnxt_re_copy_inline_data(struct bnxt_re_dev *rdev,
				    const struct ib_send_wr *wr,
				    struct bnxt_qplib_swqe *wqe)
{
	/*  Copy the inline data to the data  field */
	u8 *in_data;
	u32 i, sge_len;
	void *sge_addr;

	in_data = wqe->inline_data;
	for (i = 0; i < wr->num_sge; i++) {
		sge_addr = (void *)(unsigned long)
				wr->sg_list[i].addr;
		sge_len = wr->sg_list[i].length;

		if ((sge_len + wqe->inline_len) >
		    BNXT_QPLIB_SWQE_MAX_INLINE_LENGTH) {
			ibdev_err(&rdev->ibdev,
				  "Inline data size requested > supported value");
			return -EINVAL;
		}
		sge_len = wr->sg_list[i].length;

		memcpy(in_data, sge_addr, sge_len);
		in_data += wr->sg_list[i].length;
		wqe->inline_len += wr->sg_list[i].length;
	}
	return wqe->inline_len;
}

static int bnxt_re_copy_wr_payload(struct bnxt_re_dev *rdev,
				   const struct ib_send_wr *wr,
				   struct bnxt_qplib_swqe *wqe)
{
	int payload_sz = 0;

	if (wr->send_flags & IB_SEND_INLINE)
		payload_sz = bnxt_re_copy_inline_data(rdev, wr, wqe);
	else
		payload_sz = bnxt_re_build_sgl(wr->sg_list, wqe->sg_list,
					       wqe->num_sge);

	return payload_sz;
}

static void bnxt_ud_qp_hw_stall_workaround(struct bnxt_re_qp *qp)
{
	if ((qp->ib_qp.qp_type == IB_QPT_UD ||
	     qp->ib_qp.qp_type == IB_QPT_GSI ||
	     qp->ib_qp.qp_type == IB_QPT_RAW_ETHERTYPE) &&
	     qp->qplib_qp.wqe_cnt == BNXT_RE_UD_QP_HW_STALL) {
		int qp_attr_mask;
		struct ib_qp_attr qp_attr;

		qp_attr_mask = IB_QP_STATE;
		qp_attr.qp_state = IB_QPS_RTS;
		bnxt_re_modify_qp(&qp->ib_qp, &qp_attr, qp_attr_mask, NULL);
		qp->qplib_qp.wqe_cnt = 0;
	}
}

static int bnxt_re_post_send_shadow_qp(struct bnxt_re_dev *rdev,
				       struct bnxt_re_qp *qp,
				       const struct ib_send_wr *wr)
{
	int rc = 0, payload_sz = 0;
	unsigned long flags;

	spin_lock_irqsave(&qp->sq_lock, flags);
	while (wr) {
		struct bnxt_qplib_swqe wqe = {};

		/* Common */
		wqe.num_sge = wr->num_sge;
		if (wr->num_sge > qp->qplib_qp.sq.max_sge) {
			ibdev_err(&rdev->ibdev,
				  "Limit exceeded for Send SGEs");
			rc = -EINVAL;
			goto bad;
		}

		payload_sz = bnxt_re_copy_wr_payload(qp->rdev, wr, &wqe);
		if (payload_sz < 0) {
			rc = -EINVAL;
			goto bad;
		}
		wqe.wr_id = wr->wr_id;

		wqe.type = BNXT_QPLIB_SWQE_TYPE_SEND;

		rc = bnxt_re_build_send_wqe(qp, wr, &wqe);
		if (!rc)
			rc = bnxt_qplib_post_send(&qp->qplib_qp, &wqe);
bad:
		if (rc) {
			ibdev_err(&rdev->ibdev,
				  "Post send failed opcode = %#x rc = %d",
				  wr->opcode, rc);
			break;
		}
		wr = wr->next;
	}
	bnxt_qplib_post_send_db(&qp->qplib_qp);
	bnxt_ud_qp_hw_stall_workaround(qp);
	spin_unlock_irqrestore(&qp->sq_lock, flags);
	return rc;
}

static void bnxt_re_legacy_set_uc_fence(struct bnxt_qplib_swqe *wqe)
{
	/* Need unconditional fence for non-wire memory opcode
	 * to work as expected.
	 */
	if (wqe->type == BNXT_QPLIB_SWQE_TYPE_LOCAL_INV ||
	    wqe->type == BNXT_QPLIB_SWQE_TYPE_FAST_REG_MR ||
	    wqe->type == BNXT_QPLIB_SWQE_TYPE_REG_MR ||
	    wqe->type == BNXT_QPLIB_SWQE_TYPE_BIND_MW)
		wqe->flags |= BNXT_QPLIB_SWQE_FLAGS_UC_FENCE;
}

int bnxt_re_post_send(struct ib_qp *ib_qp, const struct ib_send_wr *wr,
		      const struct ib_send_wr **bad_wr)
{
	struct bnxt_re_qp *qp = container_of(ib_qp, struct bnxt_re_qp, ib_qp);
	struct bnxt_qplib_swqe wqe;
	int rc = 0, payload_sz = 0;
	unsigned long flags;

	spin_lock_irqsave(&qp->sq_lock, flags);
	while (wr) {
		/* House keeping */
		memset(&wqe, 0, sizeof(wqe));

		/* Common */
		wqe.num_sge = wr->num_sge;
		if (wr->num_sge > qp->qplib_qp.sq.max_sge) {
			ibdev_err(&qp->rdev->ibdev,
				  "Limit exceeded for Send SGEs");
			rc = -EINVAL;
			goto bad;
		}

		payload_sz = bnxt_re_copy_wr_payload(qp->rdev, wr, &wqe);
		if (payload_sz < 0) {
			rc = -EINVAL;
			goto bad;
		}
		wqe.wr_id = wr->wr_id;

		switch (wr->opcode) {
		case IB_WR_SEND:
		case IB_WR_SEND_WITH_IMM:
			if (qp->qplib_qp.type == CMDQ_CREATE_QP1_TYPE_GSI) {
				rc = bnxt_re_build_qp1_send_v2(qp, wr, &wqe,
							       payload_sz);
				if (rc)
					goto bad;
				wqe.rawqp1.lflags |=
					SQ_SEND_RAWETH_QP1_LFLAGS_ROCE_CRC;
			}
			switch (wr->send_flags) {
			case IB_SEND_IP_CSUM:
				wqe.rawqp1.lflags |=
					SQ_SEND_RAWETH_QP1_LFLAGS_IP_CHKSUM;
				break;
			default:
				break;
			}
			fallthrough;
		case IB_WR_SEND_WITH_INV:
			rc = bnxt_re_build_send_wqe(qp, wr, &wqe);
			break;
		case IB_WR_RDMA_WRITE:
		case IB_WR_RDMA_WRITE_WITH_IMM:
		case IB_WR_RDMA_READ:
			rc = bnxt_re_build_rdma_wqe(wr, &wqe);
			break;
		case IB_WR_ATOMIC_CMP_AND_SWP:
		case IB_WR_ATOMIC_FETCH_AND_ADD:
			rc = bnxt_re_build_atomic_wqe(wr, &wqe);
			break;
		case IB_WR_RDMA_READ_WITH_INV:
			ibdev_err(&qp->rdev->ibdev,
				  "RDMA Read with Invalidate is not supported");
			rc = -EINVAL;
			goto bad;
		case IB_WR_LOCAL_INV:
			rc = bnxt_re_build_inv_wqe(wr, &wqe);
			break;
		case IB_WR_REG_MR:
			rc = bnxt_re_build_reg_wqe(reg_wr(wr), &wqe);
			break;
		default:
			/* Unsupported WRs */
			ibdev_err(&qp->rdev->ibdev,
				  "WR (%#x) is not supported", wr->opcode);
			rc = -EINVAL;
			goto bad;
		}
		if (!rc) {
			if (!bnxt_qplib_is_chip_gen_p5_p7(qp->rdev->chip_ctx))
				bnxt_re_legacy_set_uc_fence(&wqe);
			rc = bnxt_qplib_post_send(&qp->qplib_qp, &wqe);
		}
bad:
		if (rc) {
			ibdev_err(&qp->rdev->ibdev,
				  "post_send failed op:%#x qps = %#x rc = %d\n",
				  wr->opcode, qp->qplib_qp.state, rc);
			*bad_wr = wr;
			break;
		}
		wr = wr->next;
	}
	bnxt_qplib_post_send_db(&qp->qplib_qp);
	bnxt_ud_qp_hw_stall_workaround(qp);
	spin_unlock_irqrestore(&qp->sq_lock, flags);

	return rc;
}

static int bnxt_re_post_recv_shadow_qp(struct bnxt_re_dev *rdev,
				       struct bnxt_re_qp *qp,
				       const struct ib_recv_wr *wr)
{
	struct bnxt_qplib_swqe wqe;
	int rc = 0;

	while (wr) {
		/* House keeping */
		memset(&wqe, 0, sizeof(wqe));

		/* Common */
		wqe.num_sge = wr->num_sge;
		if (wr->num_sge > qp->qplib_qp.rq.max_sge) {
			ibdev_err(&rdev->ibdev,
				  "Limit exceeded for Receive SGEs");
			rc = -EINVAL;
			break;
		}
		bnxt_re_build_sgl(wr->sg_list, wqe.sg_list, wr->num_sge);
		wqe.wr_id = wr->wr_id;
		wqe.type = BNXT_QPLIB_SWQE_TYPE_RECV;

		rc = bnxt_qplib_post_recv(&qp->qplib_qp, &wqe);
		if (rc)
			break;

		wr = wr->next;
	}
	if (!rc)
		bnxt_qplib_post_recv_db(&qp->qplib_qp);
	return rc;
}

int bnxt_re_post_recv(struct ib_qp *ib_qp, const struct ib_recv_wr *wr,
		      const struct ib_recv_wr **bad_wr)
{
	struct bnxt_re_qp *qp = container_of(ib_qp, struct bnxt_re_qp, ib_qp);
	struct bnxt_qplib_swqe wqe;
	int rc = 0, payload_sz = 0;
	unsigned long flags;
	u32 count = 0;

	spin_lock_irqsave(&qp->rq_lock, flags);
	while (wr) {
		/* House keeping */
		memset(&wqe, 0, sizeof(wqe));

		/* Common */
		wqe.num_sge = wr->num_sge;
		if (wr->num_sge > qp->qplib_qp.rq.max_sge) {
			ibdev_err(&qp->rdev->ibdev,
				  "Limit exceeded for Receive SGEs");
			rc = -EINVAL;
			*bad_wr = wr;
			break;
		}

		payload_sz = bnxt_re_build_sgl(wr->sg_list, wqe.sg_list,
					       wr->num_sge);
		wqe.wr_id = wr->wr_id;
		wqe.type = BNXT_QPLIB_SWQE_TYPE_RECV;

		if (ib_qp->qp_type == IB_QPT_GSI &&
		    qp->qplib_qp.type != CMDQ_CREATE_QP_TYPE_GSI)
			rc = bnxt_re_build_qp1_shadow_qp_recv(qp, wr, &wqe,
							      payload_sz);
		if (!rc)
			rc = bnxt_qplib_post_recv(&qp->qplib_qp, &wqe);
		if (rc) {
			*bad_wr = wr;
			break;
		}

		/* Ring DB if the RQEs posted reaches a threshold value */
		if (++count >= BNXT_RE_RQ_WQE_THRESHOLD) {
			bnxt_qplib_post_recv_db(&qp->qplib_qp);
			count = 0;
		}

		wr = wr->next;
	}

	if (count)
		bnxt_qplib_post_recv_db(&qp->qplib_qp);

	spin_unlock_irqrestore(&qp->rq_lock, flags);

	return rc;
}

/* Completion Queues */
int bnxt_re_destroy_cq(struct ib_cq *ib_cq, struct ib_udata *udata)
{
	struct bnxt_qplib_chip_ctx *cctx;
	struct bnxt_qplib_nq *nq;
	struct bnxt_re_dev *rdev;
	struct bnxt_re_cq *cq;

	cq = container_of(ib_cq, struct bnxt_re_cq, ib_cq);
	rdev = cq->rdev;
	nq = cq->qplib_cq.nq;
	cctx = rdev->chip_ctx;

	if (cctx->modes.toggle_bits & BNXT_QPLIB_CQ_TOGGLE_BIT) {
		free_page((unsigned long)cq->uctx_cq_page);
		hash_del(&cq->hash_entry);
	}
	bnxt_qplib_destroy_cq(&rdev->qplib_res, &cq->qplib_cq);
	ib_umem_release(cq->umem);

	atomic_dec(&rdev->stats.res.cq_count);
	nq->budget--;
	kfree(cq->cql);
	return 0;
}

int bnxt_re_create_cq(struct ib_cq *ibcq, const struct ib_cq_init_attr *attr,
		      struct uverbs_attr_bundle *attrs)
{
	struct bnxt_re_cq *cq = container_of(ibcq, struct bnxt_re_cq, ib_cq);
	struct bnxt_re_dev *rdev = to_bnxt_re_dev(ibcq->device, ibdev);
	struct ib_udata *udata = &attrs->driver_udata;
	struct bnxt_re_ucontext *uctx =
		rdma_udata_to_drv_context(udata, struct bnxt_re_ucontext, ib_uctx);
	struct bnxt_qplib_dev_attr *dev_attr = &rdev->dev_attr;
	struct bnxt_qplib_chip_ctx *cctx;
	struct bnxt_qplib_nq *nq = NULL;
	unsigned int nq_alloc_cnt;
	int cqe = attr->cqe;
	int rc, entries;
	u32 active_cqs;

	if (attr->flags)
		return -EOPNOTSUPP;

	/* Validate CQ fields */
	if (cqe < 1 || cqe > dev_attr->max_cq_wqes) {
		ibdev_err(&rdev->ibdev, "Failed to create CQ -max exceeded");
		return -EINVAL;
	}

	cq->rdev = rdev;
	cctx = rdev->chip_ctx;
	cq->qplib_cq.cq_handle = (u64)(unsigned long)(&cq->qplib_cq);

	entries = bnxt_re_init_depth(cqe + 1, uctx);
	if (entries > dev_attr->max_cq_wqes + 1)
		entries = dev_attr->max_cq_wqes + 1;

	cq->qplib_cq.sg_info.pgsize = PAGE_SIZE;
	cq->qplib_cq.sg_info.pgshft = PAGE_SHIFT;
	if (udata) {
		struct bnxt_re_cq_req req;
		if (ib_copy_from_udata(&req, udata, sizeof(req))) {
			rc = -EFAULT;
			goto fail;
		}

		cq->umem = ib_umem_get(&rdev->ibdev, req.cq_va,
				       entries * sizeof(struct cq_base),
				       IB_ACCESS_LOCAL_WRITE);
		if (IS_ERR(cq->umem)) {
			rc = PTR_ERR(cq->umem);
			goto fail;
		}
		cq->qplib_cq.sg_info.umem = cq->umem;
		cq->qplib_cq.dpi = &uctx->dpi;
	} else {
		cq->max_cql = min_t(u32, entries, MAX_CQL_PER_POLL);
		cq->cql = kcalloc(cq->max_cql, sizeof(struct bnxt_qplib_cqe),
				  GFP_KERNEL);
		if (!cq->cql) {
			rc = -ENOMEM;
			goto fail;
		}

		cq->qplib_cq.dpi = &rdev->dpi_privileged;
	}
	/*
	 * Allocating the NQ in a round robin fashion. nq_alloc_cnt is a
	 * used for getting the NQ index.
	 */
	nq_alloc_cnt = atomic_inc_return(&rdev->nq_alloc_cnt);
	nq = &rdev->nq[nq_alloc_cnt % (rdev->num_msix - 1)];
	cq->qplib_cq.max_wqe = entries;
	cq->qplib_cq.cnq_hw_ring_id = nq->ring_id;
	cq->qplib_cq.nq	= nq;

	rc = bnxt_qplib_create_cq(&rdev->qplib_res, &cq->qplib_cq);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to create HW CQ");
		goto fail;
	}

	cq->ib_cq.cqe = entries;
	cq->cq_period = cq->qplib_cq.period;
	nq->budget++;

	active_cqs = atomic_inc_return(&rdev->stats.res.cq_count);
	if (active_cqs > rdev->stats.res.cq_watermark)
		rdev->stats.res.cq_watermark = active_cqs;
	spin_lock_init(&cq->cq_lock);

	if (udata) {
		struct bnxt_re_cq_resp resp = {};

		if (cctx->modes.toggle_bits & BNXT_QPLIB_CQ_TOGGLE_BIT) {
			hash_add(rdev->cq_hash, &cq->hash_entry, cq->qplib_cq.id);
			/* Allocate a page */
			cq->uctx_cq_page = (void *)get_zeroed_page(GFP_KERNEL);
			if (!cq->uctx_cq_page) {
				rc = -ENOMEM;
				goto c2fail;
			}
			resp.comp_mask |= BNXT_RE_CQ_TOGGLE_PAGE_SUPPORT;
		}
		resp.cqid = cq->qplib_cq.id;
		resp.tail = cq->qplib_cq.hwq.cons;
		resp.phase = cq->qplib_cq.period;
		resp.rsvd = 0;
		rc = ib_copy_to_udata(udata, &resp, min(sizeof(resp), udata->outlen));
		if (rc) {
			ibdev_err(&rdev->ibdev, "Failed to copy CQ udata");
			bnxt_qplib_destroy_cq(&rdev->qplib_res, &cq->qplib_cq);
			goto free_mem;
		}
	}

	return 0;

free_mem:
	free_page((unsigned long)cq->uctx_cq_page);
c2fail:
	ib_umem_release(cq->umem);
fail:
	kfree(cq->cql);
	return rc;
}

static void bnxt_re_resize_cq_complete(struct bnxt_re_cq *cq)
{
	struct bnxt_re_dev *rdev = cq->rdev;

	bnxt_qplib_resize_cq_complete(&rdev->qplib_res, &cq->qplib_cq);

	cq->qplib_cq.max_wqe = cq->resize_cqe;
	if (cq->resize_umem) {
		ib_umem_release(cq->umem);
		cq->umem = cq->resize_umem;
		cq->resize_umem = NULL;
		cq->resize_cqe = 0;
	}
}

int bnxt_re_resize_cq(struct ib_cq *ibcq, int cqe, struct ib_udata *udata)
{
	struct bnxt_qplib_sg_info sg_info = {};
	struct bnxt_qplib_dpi *orig_dpi = NULL;
	struct bnxt_qplib_dev_attr *dev_attr;
	struct bnxt_re_ucontext *uctx = NULL;
	struct bnxt_re_resize_cq_req req;
	struct bnxt_re_dev *rdev;
	struct bnxt_re_cq *cq;
	int rc, entries;

	cq =  container_of(ibcq, struct bnxt_re_cq, ib_cq);
	rdev = cq->rdev;
	dev_attr = &rdev->dev_attr;
	if (!ibcq->uobject) {
		ibdev_err(&rdev->ibdev, "Kernel CQ Resize not supported");
		return -EOPNOTSUPP;
	}

	if (cq->resize_umem) {
		ibdev_err(&rdev->ibdev, "Resize CQ %#x failed - Busy",
			  cq->qplib_cq.id);
		return -EBUSY;
	}

	/* Check the requested cq depth out of supported depth */
	if (cqe < 1 || cqe > dev_attr->max_cq_wqes) {
		ibdev_err(&rdev->ibdev, "Resize CQ %#x failed - out of range cqe %d",
			  cq->qplib_cq.id, cqe);
		return -EINVAL;
	}

	uctx = rdma_udata_to_drv_context(udata, struct bnxt_re_ucontext, ib_uctx);
	entries = bnxt_re_init_depth(cqe + 1, uctx);
	if (entries > dev_attr->max_cq_wqes + 1)
		entries = dev_attr->max_cq_wqes + 1;

	/* uverbs consumer */
	if (ib_copy_from_udata(&req, udata, sizeof(req))) {
		rc = -EFAULT;
		goto fail;
	}

	cq->resize_umem = ib_umem_get(&rdev->ibdev, req.cq_va,
				      entries * sizeof(struct cq_base),
				      IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(cq->resize_umem)) {
		rc = PTR_ERR(cq->resize_umem);
		cq->resize_umem = NULL;
		ibdev_err(&rdev->ibdev, "%s: ib_umem_get failed! rc = %d\n",
			  __func__, rc);
		goto fail;
	}
	cq->resize_cqe = entries;
	memcpy(&sg_info, &cq->qplib_cq.sg_info, sizeof(sg_info));
	orig_dpi = cq->qplib_cq.dpi;

	cq->qplib_cq.sg_info.umem = cq->resize_umem;
	cq->qplib_cq.sg_info.pgsize = PAGE_SIZE;
	cq->qplib_cq.sg_info.pgshft = PAGE_SHIFT;
	cq->qplib_cq.dpi = &uctx->dpi;

	rc = bnxt_qplib_resize_cq(&rdev->qplib_res, &cq->qplib_cq, entries);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Resize HW CQ %#x failed!",
			  cq->qplib_cq.id);
		goto fail;
	}

	cq->ib_cq.cqe = cq->resize_cqe;
	atomic_inc(&rdev->stats.res.resize_count);

	return 0;

fail:
	if (cq->resize_umem) {
		ib_umem_release(cq->resize_umem);
		cq->resize_umem = NULL;
		cq->resize_cqe = 0;
		memcpy(&cq->qplib_cq.sg_info, &sg_info, sizeof(sg_info));
		cq->qplib_cq.dpi = orig_dpi;
	}
	return rc;
}

static u8 __req_to_ib_wc_status(u8 qstatus)
{
	switch (qstatus) {
	case CQ_REQ_STATUS_OK:
		return IB_WC_SUCCESS;
	case CQ_REQ_STATUS_BAD_RESPONSE_ERR:
		return IB_WC_BAD_RESP_ERR;
	case CQ_REQ_STATUS_LOCAL_LENGTH_ERR:
		return IB_WC_LOC_LEN_ERR;
	case CQ_REQ_STATUS_LOCAL_QP_OPERATION_ERR:
		return IB_WC_LOC_QP_OP_ERR;
	case CQ_REQ_STATUS_LOCAL_PROTECTION_ERR:
		return IB_WC_LOC_PROT_ERR;
	case CQ_REQ_STATUS_MEMORY_MGT_OPERATION_ERR:
		return IB_WC_GENERAL_ERR;
	case CQ_REQ_STATUS_REMOTE_INVALID_REQUEST_ERR:
		return IB_WC_REM_INV_REQ_ERR;
	case CQ_REQ_STATUS_REMOTE_ACCESS_ERR:
		return IB_WC_REM_ACCESS_ERR;
	case CQ_REQ_STATUS_REMOTE_OPERATION_ERR:
		return IB_WC_REM_OP_ERR;
	case CQ_REQ_STATUS_RNR_NAK_RETRY_CNT_ERR:
		return IB_WC_RNR_RETRY_EXC_ERR;
	case CQ_REQ_STATUS_TRANSPORT_RETRY_CNT_ERR:
		return IB_WC_RETRY_EXC_ERR;
	case CQ_REQ_STATUS_WORK_REQUEST_FLUSHED_ERR:
		return IB_WC_WR_FLUSH_ERR;
	default:
		return IB_WC_GENERAL_ERR;
	}
	return 0;
}

static u8 __rawqp1_to_ib_wc_status(u8 qstatus)
{
	switch (qstatus) {
	case CQ_RES_RAWETH_QP1_STATUS_OK:
		return IB_WC_SUCCESS;
	case CQ_RES_RAWETH_QP1_STATUS_LOCAL_ACCESS_ERROR:
		return IB_WC_LOC_ACCESS_ERR;
	case CQ_RES_RAWETH_QP1_STATUS_HW_LOCAL_LENGTH_ERR:
		return IB_WC_LOC_LEN_ERR;
	case CQ_RES_RAWETH_QP1_STATUS_LOCAL_PROTECTION_ERR:
		return IB_WC_LOC_PROT_ERR;
	case CQ_RES_RAWETH_QP1_STATUS_LOCAL_QP_OPERATION_ERR:
		return IB_WC_LOC_QP_OP_ERR;
	case CQ_RES_RAWETH_QP1_STATUS_MEMORY_MGT_OPERATION_ERR:
		return IB_WC_GENERAL_ERR;
	case CQ_RES_RAWETH_QP1_STATUS_WORK_REQUEST_FLUSHED_ERR:
		return IB_WC_WR_FLUSH_ERR;
	case CQ_RES_RAWETH_QP1_STATUS_HW_FLUSH_ERR:
		return IB_WC_WR_FLUSH_ERR;
	default:
		return IB_WC_GENERAL_ERR;
	}
}

static u8 __rc_to_ib_wc_status(u8 qstatus)
{
	switch (qstatus) {
	case CQ_RES_RC_STATUS_OK:
		return IB_WC_SUCCESS;
	case CQ_RES_RC_STATUS_LOCAL_ACCESS_ERROR:
		return IB_WC_LOC_ACCESS_ERR;
	case CQ_RES_RC_STATUS_LOCAL_LENGTH_ERR:
		return IB_WC_LOC_LEN_ERR;
	case CQ_RES_RC_STATUS_LOCAL_PROTECTION_ERR:
		return IB_WC_LOC_PROT_ERR;
	case CQ_RES_RC_STATUS_LOCAL_QP_OPERATION_ERR:
		return IB_WC_LOC_QP_OP_ERR;
	case CQ_RES_RC_STATUS_MEMORY_MGT_OPERATION_ERR:
		return IB_WC_GENERAL_ERR;
	case CQ_RES_RC_STATUS_REMOTE_INVALID_REQUEST_ERR:
		return IB_WC_REM_INV_REQ_ERR;
	case CQ_RES_RC_STATUS_WORK_REQUEST_FLUSHED_ERR:
		return IB_WC_WR_FLUSH_ERR;
	case CQ_RES_RC_STATUS_HW_FLUSH_ERR:
		return IB_WC_WR_FLUSH_ERR;
	default:
		return IB_WC_GENERAL_ERR;
	}
}

static void bnxt_re_process_req_wc(struct ib_wc *wc, struct bnxt_qplib_cqe *cqe)
{
	switch (cqe->type) {
	case BNXT_QPLIB_SWQE_TYPE_SEND:
		wc->opcode = IB_WC_SEND;
		break;
	case BNXT_QPLIB_SWQE_TYPE_SEND_WITH_IMM:
		wc->opcode = IB_WC_SEND;
		wc->wc_flags |= IB_WC_WITH_IMM;
		break;
	case BNXT_QPLIB_SWQE_TYPE_SEND_WITH_INV:
		wc->opcode = IB_WC_SEND;
		wc->wc_flags |= IB_WC_WITH_INVALIDATE;
		break;
	case BNXT_QPLIB_SWQE_TYPE_RDMA_WRITE:
		wc->opcode = IB_WC_RDMA_WRITE;
		break;
	case BNXT_QPLIB_SWQE_TYPE_RDMA_WRITE_WITH_IMM:
		wc->opcode = IB_WC_RDMA_WRITE;
		wc->wc_flags |= IB_WC_WITH_IMM;
		break;
	case BNXT_QPLIB_SWQE_TYPE_RDMA_READ:
		wc->opcode = IB_WC_RDMA_READ;
		break;
	case BNXT_QPLIB_SWQE_TYPE_ATOMIC_CMP_AND_SWP:
		wc->opcode = IB_WC_COMP_SWAP;
		break;
	case BNXT_QPLIB_SWQE_TYPE_ATOMIC_FETCH_AND_ADD:
		wc->opcode = IB_WC_FETCH_ADD;
		break;
	case BNXT_QPLIB_SWQE_TYPE_LOCAL_INV:
		wc->opcode = IB_WC_LOCAL_INV;
		break;
	case BNXT_QPLIB_SWQE_TYPE_REG_MR:
		wc->opcode = IB_WC_REG_MR;
		break;
	default:
		wc->opcode = IB_WC_SEND;
		break;
	}

	wc->status = __req_to_ib_wc_status(cqe->status);
}

static int bnxt_re_check_packet_type(u16 raweth_qp1_flags,
				     u16 raweth_qp1_flags2)
{
	bool is_ipv6 = false, is_ipv4 = false;

	/* raweth_qp1_flags Bit 9-6 indicates itype */
	if ((raweth_qp1_flags & CQ_RES_RAWETH_QP1_RAWETH_QP1_FLAGS_ITYPE_ROCE)
	    != CQ_RES_RAWETH_QP1_RAWETH_QP1_FLAGS_ITYPE_ROCE)
		return -1;

	if (raweth_qp1_flags2 &
	    CQ_RES_RAWETH_QP1_RAWETH_QP1_FLAGS2_IP_CS_CALC &&
	    raweth_qp1_flags2 &
	    CQ_RES_RAWETH_QP1_RAWETH_QP1_FLAGS2_L4_CS_CALC) {
		/* raweth_qp1_flags2 Bit 8 indicates ip_type. 0-v4 1 - v6 */
		(raweth_qp1_flags2 &
		 CQ_RES_RAWETH_QP1_RAWETH_QP1_FLAGS2_IP_TYPE) ?
			(is_ipv6 = true) : (is_ipv4 = true);
		return ((is_ipv6) ?
			 BNXT_RE_ROCEV2_IPV6_PACKET :
			 BNXT_RE_ROCEV2_IPV4_PACKET);
	} else {
		return BNXT_RE_ROCE_V1_PACKET;
	}
}

static int bnxt_re_to_ib_nw_type(int nw_type)
{
	u8 nw_hdr_type = 0xFF;

	switch (nw_type) {
	case BNXT_RE_ROCE_V1_PACKET:
		nw_hdr_type = RDMA_NETWORK_ROCE_V1;
		break;
	case BNXT_RE_ROCEV2_IPV4_PACKET:
		nw_hdr_type = RDMA_NETWORK_IPV4;
		break;
	case BNXT_RE_ROCEV2_IPV6_PACKET:
		nw_hdr_type = RDMA_NETWORK_IPV6;
		break;
	}
	return nw_hdr_type;
}

static bool bnxt_re_is_loopback_packet(struct bnxt_re_dev *rdev,
				       void *rq_hdr_buf)
{
	u8 *tmp_buf = NULL;
	struct ethhdr *eth_hdr;
	u16 eth_type;
	bool rc = false;

	tmp_buf = (u8 *)rq_hdr_buf;
	/*
	 * If dest mac is not same as I/F mac, this could be a
	 * loopback address or multicast address, check whether
	 * it is a loopback packet
	 */
	if (!ether_addr_equal(tmp_buf, rdev->netdev->dev_addr)) {
		tmp_buf += 4;
		/* Check the  ether type */
		eth_hdr = (struct ethhdr *)tmp_buf;
		eth_type = ntohs(eth_hdr->h_proto);
		switch (eth_type) {
		case ETH_P_IBOE:
			rc = true;
			break;
		case ETH_P_IP:
		case ETH_P_IPV6: {
			u32 len;
			struct udphdr *udp_hdr;

			len = (eth_type == ETH_P_IP ? sizeof(struct iphdr) :
						      sizeof(struct ipv6hdr));
			tmp_buf += sizeof(struct ethhdr) + len;
			udp_hdr = (struct udphdr *)tmp_buf;
			if (ntohs(udp_hdr->dest) ==
				    ROCE_V2_UDP_DPORT)
				rc = true;
			break;
			}
		default:
			break;
		}
	}

	return rc;
}

static int bnxt_re_process_raw_qp_pkt_rx(struct bnxt_re_qp *gsi_qp,
					 struct bnxt_qplib_cqe *cqe)
{
	struct bnxt_re_dev *rdev = gsi_qp->rdev;
	struct bnxt_re_sqp_entries *sqp_entry = NULL;
	struct bnxt_re_qp *gsi_sqp = rdev->gsi_ctx.gsi_sqp;
	dma_addr_t shrq_hdr_buf_map;
	struct ib_sge s_sge[2] = {};
	struct ib_sge r_sge[2] = {};
	struct bnxt_re_ah *gsi_sah;
	struct ib_recv_wr rwr = {};
	dma_addr_t rq_hdr_buf_map;
	struct ib_ud_wr udwr = {};
	struct ib_send_wr *swr;
	u32 skip_bytes = 0;
	int pkt_type = 0;
	void *rq_hdr_buf;
	u32 offset = 0;
	u32 tbl_idx;
	int rc;

	swr = &udwr.wr;
	tbl_idx = cqe->wr_id;

	rq_hdr_buf = gsi_qp->qplib_qp.rq_hdr_buf +
			(tbl_idx * gsi_qp->qplib_qp.rq_hdr_buf_size);
	rq_hdr_buf_map = bnxt_qplib_get_qp_buf_from_index(&gsi_qp->qplib_qp,
							  tbl_idx);

	/* Shadow QP header buffer */
	shrq_hdr_buf_map = bnxt_qplib_get_qp_buf_from_index(&gsi_qp->qplib_qp,
							    tbl_idx);
	sqp_entry = &rdev->gsi_ctx.sqp_tbl[tbl_idx];

	/* Store this cqe */
	memcpy(&sqp_entry->cqe, cqe, sizeof(struct bnxt_qplib_cqe));
	sqp_entry->qp1_qp = gsi_qp;

	/* Find packet type from the cqe */

	pkt_type = bnxt_re_check_packet_type(cqe->raweth_qp1_flags,
					     cqe->raweth_qp1_flags2);
	if (pkt_type < 0) {
		ibdev_err(&rdev->ibdev, "Invalid packet\n");
		return -EINVAL;
	}

	/* Adjust the offset for the user buffer and post in the rq */

	if (pkt_type == BNXT_RE_ROCEV2_IPV4_PACKET)
		offset = 20;

	/*
	 * QP1 loopback packet has 4 bytes of internal header before
	 * ether header. Skip these four bytes.
	 */
	if (bnxt_re_is_loopback_packet(rdev, rq_hdr_buf))
		skip_bytes = 4;

	/* First send SGE . Skip the ether header*/
	s_sge[0].addr = rq_hdr_buf_map + BNXT_QPLIB_MAX_QP1_RQ_ETH_HDR_SIZE
			+ skip_bytes;
	s_sge[0].lkey = 0xFFFFFFFF;
	s_sge[0].length = offset ? BNXT_QPLIB_MAX_GRH_HDR_SIZE_IPV4 :
				BNXT_QPLIB_MAX_GRH_HDR_SIZE_IPV6;

	/* Second Send SGE */
	s_sge[1].addr = s_sge[0].addr + s_sge[0].length +
			BNXT_QPLIB_MAX_QP1_RQ_BDETH_HDR_SIZE;
	if (pkt_type != BNXT_RE_ROCE_V1_PACKET)
		s_sge[1].addr += 8;
	s_sge[1].lkey = 0xFFFFFFFF;
	s_sge[1].length = 256;

	/* First recv SGE */

	r_sge[0].addr = shrq_hdr_buf_map;
	r_sge[0].lkey = 0xFFFFFFFF;
	r_sge[0].length = 40;

	r_sge[1].addr = sqp_entry->sge.addr + offset;
	r_sge[1].lkey = sqp_entry->sge.lkey;
	r_sge[1].length = BNXT_QPLIB_MAX_GRH_HDR_SIZE_IPV6 + 256 - offset;

	/* Create receive work request */
	rwr.num_sge = 2;
	rwr.sg_list = r_sge;
	rwr.wr_id = tbl_idx;
	rwr.next = NULL;

	rc = bnxt_re_post_recv_shadow_qp(rdev, gsi_sqp, &rwr);
	if (rc) {
		ibdev_err(&rdev->ibdev,
			  "Failed to post Rx buffers to shadow QP");
		return -ENOMEM;
	}

	swr->num_sge = 2;
	swr->sg_list = s_sge;
	swr->wr_id = tbl_idx;
	swr->opcode = IB_WR_SEND;
	swr->next = NULL;
	gsi_sah = rdev->gsi_ctx.gsi_sah;
	udwr.ah = &gsi_sah->ib_ah;
	udwr.remote_qpn = gsi_sqp->qplib_qp.id;
	udwr.remote_qkey = gsi_sqp->qplib_qp.qkey;

	/* post data received  in the send queue */
	return bnxt_re_post_send_shadow_qp(rdev, gsi_sqp, swr);
}

static void bnxt_re_process_res_rawqp1_wc(struct ib_wc *wc,
					  struct bnxt_qplib_cqe *cqe)
{
	wc->opcode = IB_WC_RECV;
	wc->status = __rawqp1_to_ib_wc_status(cqe->status);
	wc->wc_flags |= IB_WC_GRH;
}

static bool bnxt_re_check_if_vlan_valid(struct bnxt_re_dev *rdev,
					u16 vlan_id)
{
	/*
	 * Check if the vlan is configured in the host.  If not configured, it
	 * can be a transparent VLAN. So dont report the vlan id.
	 */
	if (!__vlan_find_dev_deep_rcu(rdev->netdev,
				      htons(ETH_P_8021Q), vlan_id))
		return false;
	return true;
}

static bool bnxt_re_is_vlan_pkt(struct bnxt_qplib_cqe *orig_cqe,
				u16 *vid, u8 *sl)
{
	bool ret = false;
	u32 metadata;
	u16 tpid;

	metadata = orig_cqe->raweth_qp1_metadata;
	if (orig_cqe->raweth_qp1_flags2 &
		CQ_RES_RAWETH_QP1_RAWETH_QP1_FLAGS2_META_FORMAT_VLAN) {
		tpid = ((metadata &
			 CQ_RES_RAWETH_QP1_RAWETH_QP1_METADATA_TPID_MASK) >>
			 CQ_RES_RAWETH_QP1_RAWETH_QP1_METADATA_TPID_SFT);
		if (tpid == ETH_P_8021Q) {
			*vid = metadata &
			       CQ_RES_RAWETH_QP1_RAWETH_QP1_METADATA_VID_MASK;
			*sl = (metadata &
			       CQ_RES_RAWETH_QP1_RAWETH_QP1_METADATA_PRI_MASK) >>
			       CQ_RES_RAWETH_QP1_RAWETH_QP1_METADATA_PRI_SFT;
			ret = true;
		}
	}

	return ret;
}

static void bnxt_re_process_res_rc_wc(struct ib_wc *wc,
				      struct bnxt_qplib_cqe *cqe)
{
	wc->opcode = IB_WC_RECV;
	wc->status = __rc_to_ib_wc_status(cqe->status);

	if (cqe->flags & CQ_RES_RC_FLAGS_IMM)
		wc->wc_flags |= IB_WC_WITH_IMM;
	if (cqe->flags & CQ_RES_RC_FLAGS_INV)
		wc->wc_flags |= IB_WC_WITH_INVALIDATE;
	if ((cqe->flags & (CQ_RES_RC_FLAGS_RDMA | CQ_RES_RC_FLAGS_IMM)) ==
	    (CQ_RES_RC_FLAGS_RDMA | CQ_RES_RC_FLAGS_IMM))
		wc->opcode = IB_WC_RECV_RDMA_WITH_IMM;
}

static void bnxt_re_process_res_shadow_qp_wc(struct bnxt_re_qp *gsi_sqp,
					     struct ib_wc *wc,
					     struct bnxt_qplib_cqe *cqe)
{
	struct bnxt_re_dev *rdev = gsi_sqp->rdev;
	struct bnxt_re_qp *gsi_qp = NULL;
	struct bnxt_qplib_cqe *orig_cqe = NULL;
	struct bnxt_re_sqp_entries *sqp_entry = NULL;
	int nw_type;
	u32 tbl_idx;
	u16 vlan_id;
	u8 sl;

	tbl_idx = cqe->wr_id;

	sqp_entry = &rdev->gsi_ctx.sqp_tbl[tbl_idx];
	gsi_qp = sqp_entry->qp1_qp;
	orig_cqe = &sqp_entry->cqe;

	wc->wr_id = sqp_entry->wrid;
	wc->byte_len = orig_cqe->length;
	wc->qp = &gsi_qp->ib_qp;

	wc->ex.imm_data = cpu_to_be32(le32_to_cpu(orig_cqe->immdata));
	wc->src_qp = orig_cqe->src_qp;
	memcpy(wc->smac, orig_cqe->smac, ETH_ALEN);
	if (bnxt_re_is_vlan_pkt(orig_cqe, &vlan_id, &sl)) {
		if (bnxt_re_check_if_vlan_valid(rdev, vlan_id)) {
			wc->vlan_id = vlan_id;
			wc->sl = sl;
			wc->wc_flags |= IB_WC_WITH_VLAN;
		}
	}
	wc->port_num = 1;
	wc->vendor_err = orig_cqe->status;

	wc->opcode = IB_WC_RECV;
	wc->status = __rawqp1_to_ib_wc_status(orig_cqe->status);
	wc->wc_flags |= IB_WC_GRH;

	nw_type = bnxt_re_check_packet_type(orig_cqe->raweth_qp1_flags,
					    orig_cqe->raweth_qp1_flags2);
	if (nw_type >= 0) {
		wc->network_hdr_type = bnxt_re_to_ib_nw_type(nw_type);
		wc->wc_flags |= IB_WC_WITH_NETWORK_HDR_TYPE;
	}
}

static void bnxt_re_process_res_ud_wc(struct bnxt_re_qp *qp,
				      struct ib_wc *wc,
				      struct bnxt_qplib_cqe *cqe)
{
	struct bnxt_re_dev *rdev;
	u16 vlan_id = 0;
	u8 nw_type;

	rdev = qp->rdev;
	wc->opcode = IB_WC_RECV;
	wc->status = __rc_to_ib_wc_status(cqe->status);

	if (cqe->flags & CQ_RES_UD_FLAGS_IMM)
		wc->wc_flags |= IB_WC_WITH_IMM;
	/* report only on GSI QP for Thor */
	if (qp->qplib_qp.type == CMDQ_CREATE_QP_TYPE_GSI) {
		wc->wc_flags |= IB_WC_GRH;
		memcpy(wc->smac, cqe->smac, ETH_ALEN);
		wc->wc_flags |= IB_WC_WITH_SMAC;
		if (cqe->flags & CQ_RES_UD_FLAGS_META_FORMAT_VLAN) {
			vlan_id = (cqe->cfa_meta & 0xFFF);
		}
		/* Mark only if vlan_id is non zero */
		if (vlan_id && bnxt_re_check_if_vlan_valid(rdev, vlan_id)) {
			wc->vlan_id = vlan_id;
			wc->wc_flags |= IB_WC_WITH_VLAN;
		}
		nw_type = (cqe->flags & CQ_RES_UD_FLAGS_ROCE_IP_VER_MASK) >>
			   CQ_RES_UD_FLAGS_ROCE_IP_VER_SFT;
		wc->network_hdr_type = bnxt_re_to_ib_nw_type(nw_type);
		wc->wc_flags |= IB_WC_WITH_NETWORK_HDR_TYPE;
	}

}

static int send_phantom_wqe(struct bnxt_re_qp *qp)
{
	struct bnxt_qplib_qp *lib_qp = &qp->qplib_qp;
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&qp->sq_lock, flags);

	rc = bnxt_re_bind_fence_mw(lib_qp);
	if (!rc) {
		lib_qp->sq.phantom_wqe_cnt++;
		ibdev_dbg(&qp->rdev->ibdev,
			  "qp %#x sq->prod %#x sw_prod %#x phantom_wqe_cnt %d\n",
			  lib_qp->id, lib_qp->sq.hwq.prod,
			  HWQ_CMP(lib_qp->sq.hwq.prod, &lib_qp->sq.hwq),
			  lib_qp->sq.phantom_wqe_cnt);
	}

	spin_unlock_irqrestore(&qp->sq_lock, flags);
	return rc;
}

int bnxt_re_poll_cq(struct ib_cq *ib_cq, int num_entries, struct ib_wc *wc)
{
	struct bnxt_re_cq *cq = container_of(ib_cq, struct bnxt_re_cq, ib_cq);
	struct bnxt_re_qp *qp, *sh_qp;
	struct bnxt_qplib_cqe *cqe;
	int i, ncqe, budget;
	struct bnxt_qplib_q *sq;
	struct bnxt_qplib_qp *lib_qp;
	u32 tbl_idx;
	struct bnxt_re_sqp_entries *sqp_entry = NULL;
	unsigned long flags;

	/* User CQ; the only processing we do is to
	 * complete any pending CQ resize operation.
	 */
	if (cq->umem) {
		if (cq->resize_umem)
			bnxt_re_resize_cq_complete(cq);
		return 0;
	}

	spin_lock_irqsave(&cq->cq_lock, flags);
	budget = min_t(u32, num_entries, cq->max_cql);
	num_entries = budget;
	if (!cq->cql) {
		ibdev_err(&cq->rdev->ibdev, "POLL CQ : no CQL to use");
		goto exit;
	}
	cqe = &cq->cql[0];
	while (budget) {
		lib_qp = NULL;
		ncqe = bnxt_qplib_poll_cq(&cq->qplib_cq, cqe, budget, &lib_qp);
		if (lib_qp) {
			sq = &lib_qp->sq;
			if (sq->send_phantom) {
				qp = container_of(lib_qp,
						  struct bnxt_re_qp, qplib_qp);
				if (send_phantom_wqe(qp) == -ENOMEM)
					ibdev_err(&cq->rdev->ibdev,
						  "Phantom failed! Scheduled to send again\n");
				else
					sq->send_phantom = false;
			}
		}
		if (ncqe < budget)
			ncqe += bnxt_qplib_process_flush_list(&cq->qplib_cq,
							      cqe + ncqe,
							      budget - ncqe);

		if (!ncqe)
			break;

		for (i = 0; i < ncqe; i++, cqe++) {
			/* Transcribe each qplib_wqe back to ib_wc */
			memset(wc, 0, sizeof(*wc));

			wc->wr_id = cqe->wr_id;
			wc->byte_len = cqe->length;
			qp = container_of
				((struct bnxt_qplib_qp *)
				 (unsigned long)(cqe->qp_handle),
				 struct bnxt_re_qp, qplib_qp);
			wc->qp = &qp->ib_qp;
			wc->ex.imm_data = cpu_to_be32(le32_to_cpu(cqe->immdata));
			wc->src_qp = cqe->src_qp;
			memcpy(wc->smac, cqe->smac, ETH_ALEN);
			wc->port_num = 1;
			wc->vendor_err = cqe->status;

			switch (cqe->opcode) {
			case CQ_BASE_CQE_TYPE_REQ:
				sh_qp = qp->rdev->gsi_ctx.gsi_sqp;
				if (sh_qp &&
				    qp->qplib_qp.id == sh_qp->qplib_qp.id) {
					/* Handle this completion with
					 * the stored completion
					 */
					memset(wc, 0, sizeof(*wc));
					continue;
				}
				bnxt_re_process_req_wc(wc, cqe);
				break;
			case CQ_BASE_CQE_TYPE_RES_RAWETH_QP1:
				if (!cqe->status) {
					int rc = 0;

					rc = bnxt_re_process_raw_qp_pkt_rx
								(qp, cqe);
					if (!rc) {
						memset(wc, 0, sizeof(*wc));
						continue;
					}
					cqe->status = -1;
				}
				/* Errors need not be looped back.
				 * But change the wr_id to the one
				 * stored in the table
				 */
				tbl_idx = cqe->wr_id;
				sqp_entry = &cq->rdev->gsi_ctx.sqp_tbl[tbl_idx];
				wc->wr_id = sqp_entry->wrid;
				bnxt_re_process_res_rawqp1_wc(wc, cqe);
				break;
			case CQ_BASE_CQE_TYPE_RES_RC:
				bnxt_re_process_res_rc_wc(wc, cqe);
				break;
			case CQ_BASE_CQE_TYPE_RES_UD:
				sh_qp = qp->rdev->gsi_ctx.gsi_sqp;
				if (sh_qp &&
				    qp->qplib_qp.id == sh_qp->qplib_qp.id) {
					/* Handle this completion with
					 * the stored completion
					 */
					if (cqe->status) {
						continue;
					} else {
						bnxt_re_process_res_shadow_qp_wc
								(qp, wc, cqe);
						break;
					}
				}
				bnxt_re_process_res_ud_wc(qp, wc, cqe);
				break;
			default:
				ibdev_err(&cq->rdev->ibdev,
					  "POLL CQ : type 0x%x not handled",
					  cqe->opcode);
				continue;
			}
			wc++;
			budget--;
		}
	}
exit:
	spin_unlock_irqrestore(&cq->cq_lock, flags);
	return num_entries - budget;
}

int bnxt_re_req_notify_cq(struct ib_cq *ib_cq,
			  enum ib_cq_notify_flags ib_cqn_flags)
{
	struct bnxt_re_cq *cq = container_of(ib_cq, struct bnxt_re_cq, ib_cq);
	int type = 0, rc = 0;
	unsigned long flags;

	spin_lock_irqsave(&cq->cq_lock, flags);
	/* Trigger on the very next completion */
	if (ib_cqn_flags & IB_CQ_NEXT_COMP)
		type = DBC_DBC_TYPE_CQ_ARMALL;
	/* Trigger on the next solicited completion */
	else if (ib_cqn_flags & IB_CQ_SOLICITED)
		type = DBC_DBC_TYPE_CQ_ARMSE;

	/* Poll to see if there are missed events */
	if ((ib_cqn_flags & IB_CQ_REPORT_MISSED_EVENTS) &&
	    !(bnxt_qplib_is_cq_empty(&cq->qplib_cq))) {
		rc = 1;
		goto exit;
	}
	bnxt_qplib_req_notify_cq(&cq->qplib_cq, type);

exit:
	spin_unlock_irqrestore(&cq->cq_lock, flags);
	return rc;
}

/* Memory Regions */
struct ib_mr *bnxt_re_get_dma_mr(struct ib_pd *ib_pd, int mr_access_flags)
{
	struct bnxt_re_pd *pd = container_of(ib_pd, struct bnxt_re_pd, ib_pd);
	struct bnxt_re_dev *rdev = pd->rdev;
	struct bnxt_re_mr *mr;
	u32 active_mrs;
	int rc;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->rdev = rdev;
	mr->qplib_mr.pd = &pd->qplib_pd;
	mr->qplib_mr.access_flags = __from_ib_access_flags(mr_access_flags);
	mr->qplib_mr.type = CMDQ_ALLOCATE_MRW_MRW_FLAGS_PMR;

	if (mr_access_flags & IB_ACCESS_RELAXED_ORDERING)
		bnxt_re_check_and_set_relaxed_ordering(rdev, &mr->qplib_mr);

	/* Allocate and register 0 as the address */
	rc = bnxt_qplib_alloc_mrw(&rdev->qplib_res, &mr->qplib_mr);
	if (rc)
		goto fail;

	mr->qplib_mr.hwq.level = PBL_LVL_MAX;
	mr->qplib_mr.total_size = -1; /* Infinte length */
	rc = bnxt_qplib_reg_mr(&rdev->qplib_res, &mr->qplib_mr, NULL, 0,
			       PAGE_SIZE);
	if (rc)
		goto fail_mr;

	mr->ib_mr.lkey = mr->qplib_mr.lkey;
	if (mr_access_flags & (IB_ACCESS_REMOTE_WRITE | IB_ACCESS_REMOTE_READ |
			       IB_ACCESS_REMOTE_ATOMIC))
		mr->ib_mr.rkey = mr->ib_mr.lkey;
	active_mrs = atomic_inc_return(&rdev->stats.res.mr_count);
	if (active_mrs > rdev->stats.res.mr_watermark)
		rdev->stats.res.mr_watermark = active_mrs;

	return &mr->ib_mr;

fail_mr:
	bnxt_qplib_free_mrw(&rdev->qplib_res, &mr->qplib_mr);
fail:
	kfree(mr);
	return ERR_PTR(rc);
}

int bnxt_re_dereg_mr(struct ib_mr *ib_mr, struct ib_udata *udata)
{
	struct bnxt_re_mr *mr = container_of(ib_mr, struct bnxt_re_mr, ib_mr);
	struct bnxt_re_dev *rdev = mr->rdev;
	int rc;

	rc = bnxt_qplib_free_mrw(&rdev->qplib_res, &mr->qplib_mr);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Dereg MR failed: %#x\n", rc);
		return rc;
	}

	if (mr->pages) {
		rc = bnxt_qplib_free_fast_reg_page_list(&rdev->qplib_res,
							&mr->qplib_frpl);
		kfree(mr->pages);
		mr->npages = 0;
		mr->pages = NULL;
	}
	ib_umem_release(mr->ib_umem);

	kfree(mr);
	atomic_dec(&rdev->stats.res.mr_count);
	return rc;
}

static int bnxt_re_set_page(struct ib_mr *ib_mr, u64 addr)
{
	struct bnxt_re_mr *mr = container_of(ib_mr, struct bnxt_re_mr, ib_mr);

	if (unlikely(mr->npages == mr->qplib_frpl.max_pg_ptrs))
		return -ENOMEM;

	mr->pages[mr->npages++] = addr;
	return 0;
}

int bnxt_re_map_mr_sg(struct ib_mr *ib_mr, struct scatterlist *sg, int sg_nents,
		      unsigned int *sg_offset)
{
	struct bnxt_re_mr *mr = container_of(ib_mr, struct bnxt_re_mr, ib_mr);

	mr->npages = 0;
	return ib_sg_to_pages(ib_mr, sg, sg_nents, sg_offset, bnxt_re_set_page);
}

struct ib_mr *bnxt_re_alloc_mr(struct ib_pd *ib_pd, enum ib_mr_type type,
			       u32 max_num_sg)
{
	struct bnxt_re_pd *pd = container_of(ib_pd, struct bnxt_re_pd, ib_pd);
	struct bnxt_re_dev *rdev = pd->rdev;
	struct bnxt_re_mr *mr = NULL;
	u32 active_mrs;
	int rc;

	if (type != IB_MR_TYPE_MEM_REG) {
		ibdev_dbg(&rdev->ibdev, "MR type 0x%x not supported", type);
		return ERR_PTR(-EINVAL);
	}
	if (max_num_sg > MAX_PBL_LVL_1_PGS)
		return ERR_PTR(-EINVAL);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->rdev = rdev;
	mr->qplib_mr.pd = &pd->qplib_pd;
	mr->qplib_mr.access_flags = BNXT_QPLIB_FR_PMR;
	mr->qplib_mr.type = CMDQ_ALLOCATE_MRW_MRW_FLAGS_PMR;

	rc = bnxt_qplib_alloc_mrw(&rdev->qplib_res, &mr->qplib_mr);
	if (rc)
		goto bail;

	mr->ib_mr.lkey = mr->qplib_mr.lkey;
	mr->ib_mr.rkey = mr->ib_mr.lkey;

	mr->pages = kcalloc(max_num_sg, sizeof(u64), GFP_KERNEL);
	if (!mr->pages) {
		rc = -ENOMEM;
		goto fail;
	}
	rc = bnxt_qplib_alloc_fast_reg_page_list(&rdev->qplib_res,
						 &mr->qplib_frpl, max_num_sg);
	if (rc) {
		ibdev_err(&rdev->ibdev,
			  "Failed to allocate HW FR page list");
		goto fail_mr;
	}

	active_mrs = atomic_inc_return(&rdev->stats.res.mr_count);
	if (active_mrs > rdev->stats.res.mr_watermark)
		rdev->stats.res.mr_watermark = active_mrs;
	return &mr->ib_mr;

fail_mr:
	kfree(mr->pages);
fail:
	bnxt_qplib_free_mrw(&rdev->qplib_res, &mr->qplib_mr);
bail:
	kfree(mr);
	return ERR_PTR(rc);
}

struct ib_mw *bnxt_re_alloc_mw(struct ib_pd *ib_pd, enum ib_mw_type type,
			       struct ib_udata *udata)
{
	struct bnxt_re_pd *pd = container_of(ib_pd, struct bnxt_re_pd, ib_pd);
	struct bnxt_re_dev *rdev = pd->rdev;
	struct bnxt_re_mw *mw;
	u32 active_mws;
	int rc;

	mw = kzalloc(sizeof(*mw), GFP_KERNEL);
	if (!mw)
		return ERR_PTR(-ENOMEM);
	mw->rdev = rdev;
	mw->qplib_mw.pd = &pd->qplib_pd;

	mw->qplib_mw.type = (type == IB_MW_TYPE_1 ?
			       CMDQ_ALLOCATE_MRW_MRW_FLAGS_MW_TYPE1 :
			       CMDQ_ALLOCATE_MRW_MRW_FLAGS_MW_TYPE2B);
	rc = bnxt_qplib_alloc_mrw(&rdev->qplib_res, &mw->qplib_mw);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Allocate MW failed!");
		goto fail;
	}
	mw->ib_mw.rkey = mw->qplib_mw.rkey;

	active_mws = atomic_inc_return(&rdev->stats.res.mw_count);
	if (active_mws > rdev->stats.res.mw_watermark)
		rdev->stats.res.mw_watermark = active_mws;
	return &mw->ib_mw;

fail:
	kfree(mw);
	return ERR_PTR(rc);
}

int bnxt_re_dealloc_mw(struct ib_mw *ib_mw)
{
	struct bnxt_re_mw *mw = container_of(ib_mw, struct bnxt_re_mw, ib_mw);
	struct bnxt_re_dev *rdev = mw->rdev;
	int rc;

	rc = bnxt_qplib_free_mrw(&rdev->qplib_res, &mw->qplib_mw);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Free MW failed: %#x\n", rc);
		return rc;
	}

	kfree(mw);
	atomic_dec(&rdev->stats.res.mw_count);
	return rc;
}

static struct ib_mr *__bnxt_re_user_reg_mr(struct ib_pd *ib_pd, u64 length, u64 virt_addr,
					   int mr_access_flags, struct ib_umem *umem)
{
	struct bnxt_re_pd *pd = container_of(ib_pd, struct bnxt_re_pd, ib_pd);
	struct bnxt_re_dev *rdev = pd->rdev;
	unsigned long page_size;
	struct bnxt_re_mr *mr;
	int umem_pgs, rc;
	u32 active_mrs;

	if (length > BNXT_RE_MAX_MR_SIZE) {
		ibdev_err(&rdev->ibdev, "MR Size: %lld > Max supported:%lld\n",
			  length, BNXT_RE_MAX_MR_SIZE);
		return ERR_PTR(-ENOMEM);
	}

	page_size = ib_umem_find_best_pgsz(umem, BNXT_RE_PAGE_SIZE_SUPPORTED, virt_addr);
	if (!page_size) {
		ibdev_err(&rdev->ibdev, "umem page size unsupported!");
		return ERR_PTR(-EINVAL);
	}

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->rdev = rdev;
	mr->qplib_mr.pd = &pd->qplib_pd;
	mr->qplib_mr.access_flags = __from_ib_access_flags(mr_access_flags);
	mr->qplib_mr.type = CMDQ_ALLOCATE_MRW_MRW_FLAGS_MR;

	if (!_is_alloc_mr_unified(rdev->dev_attr.dev_cap_flags)) {
		rc = bnxt_qplib_alloc_mrw(&rdev->qplib_res, &mr->qplib_mr);
		if (rc) {
			ibdev_err(&rdev->ibdev, "Failed to allocate MR rc = %d", rc);
			rc = -EIO;
			goto free_mr;
		}
		/* The fixed portion of the rkey is the same as the lkey */
		mr->ib_mr.rkey = mr->qplib_mr.rkey;
	} else {
		mr->qplib_mr.flags = CMDQ_REGISTER_MR_FLAGS_ALLOC_MR;
	}
	mr->ib_umem = umem;
	mr->qplib_mr.va = virt_addr;
	mr->qplib_mr.total_size = length;

	if (mr_access_flags & IB_ACCESS_RELAXED_ORDERING)
		bnxt_re_check_and_set_relaxed_ordering(rdev, &mr->qplib_mr);

	umem_pgs = ib_umem_num_dma_blocks(umem, page_size);
	rc = bnxt_qplib_reg_mr(&rdev->qplib_res, &mr->qplib_mr, umem,
			       umem_pgs, page_size);
	if (rc) {
		ibdev_err(&rdev->ibdev, "Failed to register user MR - rc = %d\n", rc);
		rc = -EIO;
		goto free_mrw;
	}

	mr->ib_mr.lkey = mr->qplib_mr.lkey;
	mr->ib_mr.rkey = mr->qplib_mr.lkey;
	active_mrs = atomic_inc_return(&rdev->stats.res.mr_count);
	if (active_mrs > rdev->stats.res.mr_watermark)
		rdev->stats.res.mr_watermark = active_mrs;

	return &mr->ib_mr;

free_mrw:
	bnxt_qplib_free_mrw(&rdev->qplib_res, &mr->qplib_mr);
free_mr:
	kfree(mr);
	return ERR_PTR(rc);
}

struct ib_mr *bnxt_re_reg_user_mr(struct ib_pd *ib_pd, u64 start, u64 length,
				  u64 virt_addr, int mr_access_flags,
				  struct ib_udata *udata)
{
	struct bnxt_re_pd *pd = container_of(ib_pd, struct bnxt_re_pd, ib_pd);
	struct bnxt_re_dev *rdev = pd->rdev;
	struct ib_umem *umem;
	struct ib_mr *ib_mr;

	umem = ib_umem_get(&rdev->ibdev, start, length, mr_access_flags);
	if (IS_ERR(umem))
		return ERR_CAST(umem);

	ib_mr = __bnxt_re_user_reg_mr(ib_pd, length, virt_addr, mr_access_flags, umem);
	if (IS_ERR(ib_mr))
		ib_umem_release(umem);
	return ib_mr;
}

struct ib_mr *bnxt_re_reg_user_mr_dmabuf(struct ib_pd *ib_pd, u64 start,
					 u64 length, u64 virt_addr, int fd,
					 int mr_access_flags,
					 struct uverbs_attr_bundle *attrs)
{
	struct bnxt_re_pd *pd = container_of(ib_pd, struct bnxt_re_pd, ib_pd);
	struct bnxt_re_dev *rdev = pd->rdev;
	struct ib_umem_dmabuf *umem_dmabuf;
	struct ib_umem *umem;
	struct ib_mr *ib_mr;

	umem_dmabuf = ib_umem_dmabuf_get_pinned(&rdev->ibdev, start, length,
						fd, mr_access_flags);
	if (IS_ERR(umem_dmabuf))
		return ERR_CAST(umem_dmabuf);

	umem = &umem_dmabuf->umem;

	ib_mr = __bnxt_re_user_reg_mr(ib_pd, length, virt_addr, mr_access_flags, umem);
	if (IS_ERR(ib_mr))
		ib_umem_release(umem);
	return ib_mr;
}

int bnxt_re_alloc_ucontext(struct ib_ucontext *ctx, struct ib_udata *udata)
{
	struct ib_device *ibdev = ctx->device;
	struct bnxt_re_ucontext *uctx =
		container_of(ctx, struct bnxt_re_ucontext, ib_uctx);
	struct bnxt_re_dev *rdev = to_bnxt_re_dev(ibdev, ibdev);
	struct bnxt_qplib_dev_attr *dev_attr = &rdev->dev_attr;
	struct bnxt_re_user_mmap_entry *entry;
	struct bnxt_re_uctx_resp resp = {};
	struct bnxt_re_uctx_req ureq = {};
	u32 chip_met_rev_num = 0;
	int rc;

	ibdev_dbg(ibdev, "ABI version requested %u", ibdev->ops.uverbs_abi_ver);

	if (ibdev->ops.uverbs_abi_ver != BNXT_RE_ABI_VERSION) {
		ibdev_dbg(ibdev, " is different from the device %d ",
			  BNXT_RE_ABI_VERSION);
		return -EPERM;
	}

	uctx->rdev = rdev;

	uctx->shpg = (void *)__get_free_page(GFP_KERNEL);
	if (!uctx->shpg) {
		rc = -ENOMEM;
		goto fail;
	}
	spin_lock_init(&uctx->sh_lock);

	resp.comp_mask = BNXT_RE_UCNTX_CMASK_HAVE_CCTX;
	chip_met_rev_num = rdev->chip_ctx->chip_num;
	chip_met_rev_num |= ((u32)rdev->chip_ctx->chip_rev & 0xFF) <<
			     BNXT_RE_CHIP_ID0_CHIP_REV_SFT;
	chip_met_rev_num |= ((u32)rdev->chip_ctx->chip_metal & 0xFF) <<
			     BNXT_RE_CHIP_ID0_CHIP_MET_SFT;
	resp.chip_id0 = chip_met_rev_num;
	/*Temp, Use xa_alloc instead */
	resp.dev_id = rdev->en_dev->pdev->devfn;
	resp.max_qp = rdev->qplib_ctx.qpc_count;
	resp.pg_size = PAGE_SIZE;
	resp.cqe_sz = sizeof(struct cq_base);
	resp.max_cqd = dev_attr->max_cq_wqes;

	if (rdev->chip_ctx->modes.db_push)
		resp.comp_mask |= BNXT_RE_UCNTX_CMASK_WC_DPI_ENABLED;

	entry = bnxt_re_mmap_entry_insert(uctx, 0, BNXT_RE_MMAP_SH_PAGE, NULL);
	if (!entry) {
		rc = -ENOMEM;
		goto cfail;
	}
	uctx->shpage_mmap = &entry->rdma_entry;
	if (rdev->pacing.dbr_pacing)
		resp.comp_mask |= BNXT_RE_UCNTX_CMASK_DBR_PACING_ENABLED;

	if (_is_host_msn_table(rdev->qplib_res.dattr->dev_cap_flags2))
		resp.comp_mask |= BNXT_RE_UCNTX_CMASK_MSN_TABLE_ENABLED;

	if (udata->inlen >= sizeof(ureq)) {
		rc = ib_copy_from_udata(&ureq, udata, min(udata->inlen, sizeof(ureq)));
		if (rc)
			goto cfail;
		if (ureq.comp_mask & BNXT_RE_COMP_MASK_REQ_UCNTX_POW2_SUPPORT) {
			resp.comp_mask |= BNXT_RE_UCNTX_CMASK_POW2_DISABLED;
			uctx->cmask |= BNXT_RE_UCNTX_CAP_POW2_DISABLED;
		}
		if (ureq.comp_mask & BNXT_RE_COMP_MASK_REQ_UCNTX_VAR_WQE_SUPPORT) {
			resp.comp_mask |= BNXT_RE_UCNTX_CMASK_HAVE_MODE;
			resp.mode = rdev->chip_ctx->modes.wqe_mode;
			if (resp.mode == BNXT_QPLIB_WQE_MODE_VARIABLE)
				uctx->cmask |= BNXT_RE_UCNTX_CAP_VAR_WQE_ENABLED;
		}
	}

	rc = ib_copy_to_udata(udata, &resp, min(udata->outlen, sizeof(resp)));
	if (rc) {
		ibdev_err(ibdev, "Failed to copy user context");
		rc = -EFAULT;
		goto cfail;
	}

	return 0;
cfail:
	free_page((unsigned long)uctx->shpg);
	uctx->shpg = NULL;
fail:
	return rc;
}

void bnxt_re_dealloc_ucontext(struct ib_ucontext *ib_uctx)
{
	struct bnxt_re_ucontext *uctx = container_of(ib_uctx,
						   struct bnxt_re_ucontext,
						   ib_uctx);

	struct bnxt_re_dev *rdev = uctx->rdev;

	rdma_user_mmap_entry_remove(uctx->shpage_mmap);
	uctx->shpage_mmap = NULL;
	if (uctx->shpg)
		free_page((unsigned long)uctx->shpg);

	if (uctx->dpi.dbr) {
		/* Free DPI only if this is the first PD allocated by the
		 * application and mark the context dpi as NULL
		 */
		bnxt_qplib_dealloc_dpi(&rdev->qplib_res, &uctx->dpi);
		uctx->dpi.dbr = NULL;
	}
}

static struct bnxt_re_cq *bnxt_re_search_for_cq(struct bnxt_re_dev *rdev, u32 cq_id)
{
	struct bnxt_re_cq *cq = NULL, *tmp_cq;

	hash_for_each_possible(rdev->cq_hash, tmp_cq, hash_entry, cq_id) {
		if (tmp_cq->qplib_cq.id == cq_id) {
			cq = tmp_cq;
			break;
		}
	}
	return cq;
}

static struct bnxt_re_srq *bnxt_re_search_for_srq(struct bnxt_re_dev *rdev, u32 srq_id)
{
	struct bnxt_re_srq *srq = NULL, *tmp_srq;

	hash_for_each_possible(rdev->srq_hash, tmp_srq, hash_entry, srq_id) {
		if (tmp_srq->qplib_srq.id == srq_id) {
			srq = tmp_srq;
			break;
		}
	}
	return srq;
}

/* Helper function to mmap the virtual memory from user app */
int bnxt_re_mmap(struct ib_ucontext *ib_uctx, struct vm_area_struct *vma)
{
	struct bnxt_re_ucontext *uctx = container_of(ib_uctx,
						   struct bnxt_re_ucontext,
						   ib_uctx);
	struct bnxt_re_user_mmap_entry *bnxt_entry;
	struct rdma_user_mmap_entry *rdma_entry;
	int ret = 0;
	u64 pfn;

	rdma_entry = rdma_user_mmap_entry_get(&uctx->ib_uctx, vma);
	if (!rdma_entry)
		return -EINVAL;

	bnxt_entry = container_of(rdma_entry, struct bnxt_re_user_mmap_entry,
				  rdma_entry);

	switch (bnxt_entry->mmap_flag) {
	case BNXT_RE_MMAP_WC_DB:
		pfn = bnxt_entry->mem_offset >> PAGE_SHIFT;
		ret = rdma_user_mmap_io(ib_uctx, vma, pfn, PAGE_SIZE,
					pgprot_writecombine(vma->vm_page_prot),
					rdma_entry);
		break;
	case BNXT_RE_MMAP_UC_DB:
		pfn = bnxt_entry->mem_offset >> PAGE_SHIFT;
		ret = rdma_user_mmap_io(ib_uctx, vma, pfn, PAGE_SIZE,
					pgprot_noncached(vma->vm_page_prot),
				rdma_entry);
		break;
	case BNXT_RE_MMAP_SH_PAGE:
		ret = vm_insert_page(vma, vma->vm_start, virt_to_page(uctx->shpg));
		break;
	case BNXT_RE_MMAP_DBR_BAR:
		pfn = bnxt_entry->mem_offset >> PAGE_SHIFT;
		ret = rdma_user_mmap_io(ib_uctx, vma, pfn, PAGE_SIZE,
					pgprot_noncached(vma->vm_page_prot),
					rdma_entry);
		break;
	case BNXT_RE_MMAP_DBR_PAGE:
	case BNXT_RE_MMAP_TOGGLE_PAGE:
		/* Driver doesn't expect write access for user space */
		if (vma->vm_flags & VM_WRITE)
			return -EFAULT;
		ret = vm_insert_page(vma, vma->vm_start,
				     virt_to_page((void *)bnxt_entry->mem_offset));
		break;
	default:
		ret = -EINVAL;
		break;
	}

	rdma_user_mmap_entry_put(rdma_entry);
	return ret;
}

void bnxt_re_mmap_free(struct rdma_user_mmap_entry *rdma_entry)
{
	struct bnxt_re_user_mmap_entry *bnxt_entry;

	bnxt_entry = container_of(rdma_entry, struct bnxt_re_user_mmap_entry,
				  rdma_entry);

	kfree(bnxt_entry);
}

static int UVERBS_HANDLER(BNXT_RE_METHOD_NOTIFY_DRV)(struct uverbs_attr_bundle *attrs)
{
	struct bnxt_re_ucontext *uctx;

	uctx = container_of(ib_uverbs_get_ucontext(attrs), struct bnxt_re_ucontext, ib_uctx);
	bnxt_re_pacing_alert(uctx->rdev);
	return 0;
}

static int UVERBS_HANDLER(BNXT_RE_METHOD_ALLOC_PAGE)(struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = uverbs_attr_get_uobject(attrs, BNXT_RE_ALLOC_PAGE_HANDLE);
	enum bnxt_re_alloc_page_type alloc_type;
	struct bnxt_re_user_mmap_entry *entry;
	enum bnxt_re_mmap_flag mmap_flag;
	struct bnxt_qplib_chip_ctx *cctx;
	struct bnxt_re_ucontext *uctx;
	struct bnxt_re_dev *rdev;
	u64 mmap_offset;
	u32 length;
	u32 dpi;
	u64 addr;
	int err;

	uctx = container_of(ib_uverbs_get_ucontext(attrs), struct bnxt_re_ucontext, ib_uctx);
	if (IS_ERR(uctx))
		return PTR_ERR(uctx);

	err = uverbs_get_const(&alloc_type, attrs, BNXT_RE_ALLOC_PAGE_TYPE);
	if (err)
		return err;

	rdev = uctx->rdev;
	cctx = rdev->chip_ctx;

	switch (alloc_type) {
	case BNXT_RE_ALLOC_WC_PAGE:
		if (cctx->modes.db_push)  {
			if (bnxt_qplib_alloc_dpi(&rdev->qplib_res, &uctx->wcdpi,
						 uctx, BNXT_QPLIB_DPI_TYPE_WC))
				return -ENOMEM;
			length = PAGE_SIZE;
			dpi = uctx->wcdpi.dpi;
			addr = (u64)uctx->wcdpi.umdbr;
			mmap_flag = BNXT_RE_MMAP_WC_DB;
		} else {
			return -EINVAL;
		}

		break;
	case BNXT_RE_ALLOC_DBR_BAR_PAGE:
		length = PAGE_SIZE;
		addr = (u64)rdev->pacing.dbr_bar_addr;
		mmap_flag = BNXT_RE_MMAP_DBR_BAR;
		break;

	case BNXT_RE_ALLOC_DBR_PAGE:
		length = PAGE_SIZE;
		addr = (u64)rdev->pacing.dbr_page;
		mmap_flag = BNXT_RE_MMAP_DBR_PAGE;
		break;

	default:
		return -EOPNOTSUPP;
	}

	entry = bnxt_re_mmap_entry_insert(uctx, addr, mmap_flag, &mmap_offset);
	if (!entry)
		return -ENOMEM;

	uobj->object = entry;
	uverbs_finalize_uobj_create(attrs, BNXT_RE_ALLOC_PAGE_HANDLE);
	err = uverbs_copy_to(attrs, BNXT_RE_ALLOC_PAGE_MMAP_OFFSET,
			     &mmap_offset, sizeof(mmap_offset));
	if (err)
		return err;

	err = uverbs_copy_to(attrs, BNXT_RE_ALLOC_PAGE_MMAP_LENGTH,
			     &length, sizeof(length));
	if (err)
		return err;

	err = uverbs_copy_to(attrs, BNXT_RE_ALLOC_PAGE_DPI,
			     &dpi, sizeof(length));
	if (err)
		return err;

	return 0;
}

static int alloc_page_obj_cleanup(struct ib_uobject *uobject,
				  enum rdma_remove_reason why,
			    struct uverbs_attr_bundle *attrs)
{
	struct  bnxt_re_user_mmap_entry *entry = uobject->object;
	struct bnxt_re_ucontext *uctx = entry->uctx;

	switch (entry->mmap_flag) {
	case BNXT_RE_MMAP_WC_DB:
		if (uctx && uctx->wcdpi.dbr) {
			struct bnxt_re_dev *rdev = uctx->rdev;

			bnxt_qplib_dealloc_dpi(&rdev->qplib_res, &uctx->wcdpi);
			uctx->wcdpi.dbr = NULL;
		}
		break;
	case BNXT_RE_MMAP_DBR_BAR:
	case BNXT_RE_MMAP_DBR_PAGE:
		break;
	default:
		goto exit;
	}
	rdma_user_mmap_entry_remove(&entry->rdma_entry);
exit:
	return 0;
}

DECLARE_UVERBS_NAMED_METHOD(BNXT_RE_METHOD_ALLOC_PAGE,
			    UVERBS_ATTR_IDR(BNXT_RE_ALLOC_PAGE_HANDLE,
					    BNXT_RE_OBJECT_ALLOC_PAGE,
					    UVERBS_ACCESS_NEW,
					    UA_MANDATORY),
			    UVERBS_ATTR_CONST_IN(BNXT_RE_ALLOC_PAGE_TYPE,
						 enum bnxt_re_alloc_page_type,
						 UA_MANDATORY),
			    UVERBS_ATTR_PTR_OUT(BNXT_RE_ALLOC_PAGE_MMAP_OFFSET,
						UVERBS_ATTR_TYPE(u64),
						UA_MANDATORY),
			    UVERBS_ATTR_PTR_OUT(BNXT_RE_ALLOC_PAGE_MMAP_LENGTH,
						UVERBS_ATTR_TYPE(u32),
						UA_MANDATORY),
			    UVERBS_ATTR_PTR_OUT(BNXT_RE_ALLOC_PAGE_DPI,
						UVERBS_ATTR_TYPE(u32),
						UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(BNXT_RE_METHOD_DESTROY_PAGE,
				    UVERBS_ATTR_IDR(BNXT_RE_DESTROY_PAGE_HANDLE,
						    BNXT_RE_OBJECT_ALLOC_PAGE,
						    UVERBS_ACCESS_DESTROY,
						    UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(BNXT_RE_OBJECT_ALLOC_PAGE,
			    UVERBS_TYPE_ALLOC_IDR(alloc_page_obj_cleanup),
			    &UVERBS_METHOD(BNXT_RE_METHOD_ALLOC_PAGE),
			    &UVERBS_METHOD(BNXT_RE_METHOD_DESTROY_PAGE));

DECLARE_UVERBS_NAMED_METHOD(BNXT_RE_METHOD_NOTIFY_DRV);

DECLARE_UVERBS_GLOBAL_METHODS(BNXT_RE_OBJECT_NOTIFY_DRV,
			      &UVERBS_METHOD(BNXT_RE_METHOD_NOTIFY_DRV));

/* Toggle MEM */
static int UVERBS_HANDLER(BNXT_RE_METHOD_GET_TOGGLE_MEM)(struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = uverbs_attr_get_uobject(attrs, BNXT_RE_TOGGLE_MEM_HANDLE);
	enum bnxt_re_mmap_flag mmap_flag = BNXT_RE_MMAP_TOGGLE_PAGE;
	enum bnxt_re_get_toggle_mem_type res_type;
	struct bnxt_re_user_mmap_entry *entry;
	struct bnxt_re_ucontext *uctx;
	struct ib_ucontext *ib_uctx;
	struct bnxt_re_dev *rdev;
	struct bnxt_re_srq *srq;
	u32 length = PAGE_SIZE;
	struct bnxt_re_cq *cq;
	u64 mem_offset;
	u32 offset = 0;
	u64 addr = 0;
	u32 res_id;
	int err;

	ib_uctx = ib_uverbs_get_ucontext(attrs);
	if (IS_ERR(ib_uctx))
		return PTR_ERR(ib_uctx);

	err = uverbs_get_const(&res_type, attrs, BNXT_RE_TOGGLE_MEM_TYPE);
	if (err)
		return err;

	uctx = container_of(ib_uctx, struct bnxt_re_ucontext, ib_uctx);
	rdev = uctx->rdev;
	err = uverbs_copy_from(&res_id, attrs, BNXT_RE_TOGGLE_MEM_RES_ID);
	if (err)
		return err;

	switch (res_type) {
	case BNXT_RE_CQ_TOGGLE_MEM:
		cq = bnxt_re_search_for_cq(rdev, res_id);
		if (!cq)
			return -EINVAL;

		addr = (u64)cq->uctx_cq_page;
		break;
	case BNXT_RE_SRQ_TOGGLE_MEM:
		srq = bnxt_re_search_for_srq(rdev, res_id);
		if (!srq)
			return -EINVAL;

		addr = (u64)srq->uctx_srq_page;
		break;

	default:
		return -EOPNOTSUPP;
	}

	entry = bnxt_re_mmap_entry_insert(uctx, addr, mmap_flag, &mem_offset);
	if (!entry)
		return -ENOMEM;

	uobj->object = entry;
	uverbs_finalize_uobj_create(attrs, BNXT_RE_TOGGLE_MEM_HANDLE);
	err = uverbs_copy_to(attrs, BNXT_RE_TOGGLE_MEM_MMAP_PAGE,
			     &mem_offset, sizeof(mem_offset));
	if (err)
		return err;

	err = uverbs_copy_to(attrs, BNXT_RE_TOGGLE_MEM_MMAP_LENGTH,
			     &length, sizeof(length));
	if (err)
		return err;

	err = uverbs_copy_to(attrs, BNXT_RE_TOGGLE_MEM_MMAP_OFFSET,
			     &offset, sizeof(length));
	if (err)
		return err;

	return 0;
}

static int get_toggle_mem_obj_cleanup(struct ib_uobject *uobject,
				      enum rdma_remove_reason why,
				      struct uverbs_attr_bundle *attrs)
{
	struct  bnxt_re_user_mmap_entry *entry = uobject->object;

	rdma_user_mmap_entry_remove(&entry->rdma_entry);
	return 0;
}

DECLARE_UVERBS_NAMED_METHOD(BNXT_RE_METHOD_GET_TOGGLE_MEM,
			    UVERBS_ATTR_IDR(BNXT_RE_TOGGLE_MEM_HANDLE,
					    BNXT_RE_OBJECT_GET_TOGGLE_MEM,
					    UVERBS_ACCESS_NEW,
					    UA_MANDATORY),
			    UVERBS_ATTR_CONST_IN(BNXT_RE_TOGGLE_MEM_TYPE,
						 enum bnxt_re_get_toggle_mem_type,
						 UA_MANDATORY),
			    UVERBS_ATTR_PTR_IN(BNXT_RE_TOGGLE_MEM_RES_ID,
					       UVERBS_ATTR_TYPE(u32),
					       UA_MANDATORY),
			    UVERBS_ATTR_PTR_OUT(BNXT_RE_TOGGLE_MEM_MMAP_PAGE,
						UVERBS_ATTR_TYPE(u64),
						UA_MANDATORY),
			    UVERBS_ATTR_PTR_OUT(BNXT_RE_TOGGLE_MEM_MMAP_OFFSET,
						UVERBS_ATTR_TYPE(u32),
						UA_MANDATORY),
			    UVERBS_ATTR_PTR_OUT(BNXT_RE_TOGGLE_MEM_MMAP_LENGTH,
						UVERBS_ATTR_TYPE(u32),
						UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(BNXT_RE_METHOD_RELEASE_TOGGLE_MEM,
				    UVERBS_ATTR_IDR(BNXT_RE_RELEASE_TOGGLE_MEM_HANDLE,
						    BNXT_RE_OBJECT_GET_TOGGLE_MEM,
						    UVERBS_ACCESS_DESTROY,
						    UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(BNXT_RE_OBJECT_GET_TOGGLE_MEM,
			    UVERBS_TYPE_ALLOC_IDR(get_toggle_mem_obj_cleanup),
			    &UVERBS_METHOD(BNXT_RE_METHOD_GET_TOGGLE_MEM),
			    &UVERBS_METHOD(BNXT_RE_METHOD_RELEASE_TOGGLE_MEM));

const struct uapi_definition bnxt_re_uapi_defs[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(BNXT_RE_OBJECT_ALLOC_PAGE),
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(BNXT_RE_OBJECT_NOTIFY_DRV),
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(BNXT_RE_OBJECT_GET_TOGGLE_MEM),
	{}
};
