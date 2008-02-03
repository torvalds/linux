/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  HCA query functions
 *
 *  Authors: Heiko J Schick <schickhj@de.ibm.com>
 *           Christoph Raisch <raisch@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "ehca_tools.h"
#include "ehca_iverbs.h"
#include "hcp_if.h"

int ehca_query_device(struct ib_device *ibdev, struct ib_device_attr *props)
{
	int i, ret = 0;
	struct ehca_shca *shca = container_of(ibdev, struct ehca_shca,
					      ib_device);
	struct hipz_query_hca *rblock;

	static const u32 cap_mapping[] = {
		IB_DEVICE_RESIZE_MAX_WR,      HCA_CAP_WQE_RESIZE,
		IB_DEVICE_BAD_PKEY_CNTR,      HCA_CAP_BAD_P_KEY_CTR,
		IB_DEVICE_BAD_QKEY_CNTR,      HCA_CAP_Q_KEY_VIOL_CTR,
		IB_DEVICE_RAW_MULTI,          HCA_CAP_RAW_PACKET_MCAST,
		IB_DEVICE_AUTO_PATH_MIG,      HCA_CAP_AUTO_PATH_MIG,
		IB_DEVICE_CHANGE_PHY_PORT,    HCA_CAP_SQD_RTS_PORT_CHANGE,
		IB_DEVICE_UD_AV_PORT_ENFORCE, HCA_CAP_AH_PORT_NR_CHECK,
		IB_DEVICE_CURR_QP_STATE_MOD,  HCA_CAP_CUR_QP_STATE_MOD,
		IB_DEVICE_SHUTDOWN_PORT,      HCA_CAP_SHUTDOWN_PORT,
		IB_DEVICE_INIT_TYPE,          HCA_CAP_INIT_TYPE,
		IB_DEVICE_PORT_ACTIVE_EVENT,  HCA_CAP_PORT_ACTIVE_EVENT,
	};

	rblock = ehca_alloc_fw_ctrlblock(GFP_KERNEL);
	if (!rblock) {
		ehca_err(&shca->ib_device, "Can't allocate rblock memory.");
		return -ENOMEM;
	}

	if (hipz_h_query_hca(shca->ipz_hca_handle, rblock) != H_SUCCESS) {
		ehca_err(&shca->ib_device, "Can't query device properties");
		ret = -EINVAL;
		goto query_device1;
	}

	memset(props, 0, sizeof(struct ib_device_attr));
	props->page_size_cap   = shca->hca_cap_mr_pgsize;
	props->fw_ver          = rblock->hw_ver;
	props->max_mr_size     = rblock->max_mr_size;
	props->vendor_id       = rblock->vendor_id >> 8;
	props->vendor_part_id  = rblock->vendor_part_id >> 16;
	props->hw_ver          = rblock->hw_ver;
	props->max_qp          = min_t(unsigned, rblock->max_qp, INT_MAX);
	props->max_qp_wr       = min_t(unsigned, rblock->max_wqes_wq, INT_MAX);
	props->max_sge         = min_t(unsigned, rblock->max_sge, INT_MAX);
	props->max_sge_rd      = min_t(unsigned, rblock->max_sge_rd, INT_MAX);
	props->max_cq          = min_t(unsigned, rblock->max_cq, INT_MAX);
	props->max_cqe         = min_t(unsigned, rblock->max_cqe, INT_MAX);
	props->max_mr          = min_t(unsigned, rblock->max_mr, INT_MAX);
	props->max_mw          = min_t(unsigned, rblock->max_mw, INT_MAX);
	props->max_pd          = min_t(unsigned, rblock->max_pd, INT_MAX);
	props->max_ah          = min_t(unsigned, rblock->max_ah, INT_MAX);
	props->max_fmr         = min_t(unsigned, rblock->max_mr, INT_MAX);

	if (EHCA_BMASK_GET(HCA_CAP_SRQ, shca->hca_cap)) {
		props->max_srq         = props->max_qp;
		props->max_srq_wr      = props->max_qp_wr;
		props->max_srq_sge     = 3;
	}

	props->max_pkeys       = 16;
	props->local_ca_ack_delay
		= rblock->local_ca_ack_delay;
	props->max_raw_ipv6_qp
		= min_t(unsigned, rblock->max_raw_ipv6_qp, INT_MAX);
	props->max_raw_ethy_qp
		= min_t(unsigned, rblock->max_raw_ethy_qp, INT_MAX);
	props->max_mcast_grp
		= min_t(unsigned, rblock->max_mcast_grp, INT_MAX);
	props->max_mcast_qp_attach
		= min_t(unsigned, rblock->max_mcast_qp_attach, INT_MAX);
	props->max_total_mcast_qp_attach
		= min_t(unsigned, rblock->max_total_mcast_qp_attach, INT_MAX);

	/* translate device capabilities */
	props->device_cap_flags = IB_DEVICE_SYS_IMAGE_GUID |
		IB_DEVICE_RC_RNR_NAK_GEN | IB_DEVICE_N_NOTIFY_CQ;
	for (i = 0; i < ARRAY_SIZE(cap_mapping); i += 2)
		if (rblock->hca_cap_indicators & cap_mapping[i + 1])
			props->device_cap_flags |= cap_mapping[i];

query_device1:
	ehca_free_fw_ctrlblock(rblock);

	return ret;
}

int ehca_query_port(struct ib_device *ibdev,
		    u8 port, struct ib_port_attr *props)
{
	int ret = 0;
	u64 h_ret;
	struct ehca_shca *shca = container_of(ibdev, struct ehca_shca,
					      ib_device);
	struct hipz_query_port *rblock;

	rblock = ehca_alloc_fw_ctrlblock(GFP_KERNEL);
	if (!rblock) {
		ehca_err(&shca->ib_device, "Can't allocate rblock memory.");
		return -ENOMEM;
	}

	h_ret = hipz_h_query_port(shca->ipz_hca_handle, port, rblock);
	if (h_ret != H_SUCCESS) {
		ehca_err(&shca->ib_device, "Can't query port properties");
		ret = -EINVAL;
		goto query_port1;
	}

	memset(props, 0, sizeof(struct ib_port_attr));

	switch (rblock->max_mtu) {
	case 0x1:
		props->active_mtu = props->max_mtu = IB_MTU_256;
		break;
	case 0x2:
		props->active_mtu = props->max_mtu = IB_MTU_512;
		break;
	case 0x3:
		props->active_mtu = props->max_mtu = IB_MTU_1024;
		break;
	case 0x4:
		props->active_mtu = props->max_mtu = IB_MTU_2048;
		break;
	case 0x5:
		props->active_mtu = props->max_mtu = IB_MTU_4096;
		break;
	default:
		ehca_err(&shca->ib_device, "Unknown MTU size: %x.",
			 rblock->max_mtu);
		break;
	}

	props->port_cap_flags  = rblock->capability_mask;
	props->gid_tbl_len     = rblock->gid_tbl_len;
	props->max_msg_sz      = rblock->max_msg_sz;
	props->bad_pkey_cntr   = rblock->bad_pkey_cntr;
	props->qkey_viol_cntr  = rblock->qkey_viol_cntr;
	props->pkey_tbl_len    = rblock->pkey_tbl_len;
	props->lid             = rblock->lid;
	props->sm_lid          = rblock->sm_lid;
	props->lmc             = rblock->lmc;
	props->sm_sl           = rblock->sm_sl;
	props->subnet_timeout  = rblock->subnet_timeout;
	props->init_type_reply = rblock->init_type_reply;

	if (rblock->state && rblock->phys_width) {
		props->phys_state      = rblock->phys_pstate;
		props->state           = rblock->phys_state;
		props->active_width    = rblock->phys_width;
		props->active_speed    = rblock->phys_speed;
	} else {
		/* old firmware releases don't report physical
		 * port info, so use default values
		 */
		props->phys_state      = 5;
		props->state           = rblock->state;
		props->active_width    = IB_WIDTH_12X;
		props->active_speed    = 0x1;
	}

query_port1:
	ehca_free_fw_ctrlblock(rblock);

	return ret;
}

int ehca_query_sma_attr(struct ehca_shca *shca,
			u8 port, struct ehca_sma_attr *attr)
{
	int ret = 0;
	u64 h_ret;
	struct hipz_query_port *rblock;

	rblock = ehca_alloc_fw_ctrlblock(GFP_ATOMIC);
	if (!rblock) {
		ehca_err(&shca->ib_device, "Can't allocate rblock memory.");
		return -ENOMEM;
	}

	h_ret = hipz_h_query_port(shca->ipz_hca_handle, port, rblock);
	if (h_ret != H_SUCCESS) {
		ehca_err(&shca->ib_device, "Can't query port properties");
		ret = -EINVAL;
		goto query_sma_attr1;
	}

	memset(attr, 0, sizeof(struct ehca_sma_attr));

	attr->lid    = rblock->lid;
	attr->lmc    = rblock->lmc;
	attr->sm_sl  = rblock->sm_sl;
	attr->sm_lid = rblock->sm_lid;

	attr->pkey_tbl_len = rblock->pkey_tbl_len;
	memcpy(attr->pkeys, rblock->pkey_entries, sizeof(attr->pkeys));

query_sma_attr1:
	ehca_free_fw_ctrlblock(rblock);

	return ret;
}

int ehca_query_pkey(struct ib_device *ibdev, u8 port, u16 index, u16 *pkey)
{
	int ret = 0;
	u64 h_ret;
	struct ehca_shca *shca;
	struct hipz_query_port *rblock;

	shca = container_of(ibdev, struct ehca_shca, ib_device);
	if (index > 16) {
		ehca_err(&shca->ib_device, "Invalid index: %x.", index);
		return -EINVAL;
	}

	rblock = ehca_alloc_fw_ctrlblock(GFP_KERNEL);
	if (!rblock) {
		ehca_err(&shca->ib_device,  "Can't allocate rblock memory.");
		return -ENOMEM;
	}

	h_ret = hipz_h_query_port(shca->ipz_hca_handle, port, rblock);
	if (h_ret != H_SUCCESS) {
		ehca_err(&shca->ib_device, "Can't query port properties");
		ret = -EINVAL;
		goto query_pkey1;
	}

	memcpy(pkey, &rblock->pkey_entries + index, sizeof(u16));

query_pkey1:
	ehca_free_fw_ctrlblock(rblock);

	return ret;
}

int ehca_query_gid(struct ib_device *ibdev, u8 port,
		   int index, union ib_gid *gid)
{
	int ret = 0;
	u64 h_ret;
	struct ehca_shca *shca = container_of(ibdev, struct ehca_shca,
					      ib_device);
	struct hipz_query_port *rblock;

	if (index > 255) {
		ehca_err(&shca->ib_device, "Invalid index: %x.", index);
		return -EINVAL;
	}

	rblock = ehca_alloc_fw_ctrlblock(GFP_KERNEL);
	if (!rblock) {
		ehca_err(&shca->ib_device, "Can't allocate rblock memory.");
		return -ENOMEM;
	}

	h_ret = hipz_h_query_port(shca->ipz_hca_handle, port, rblock);
	if (h_ret != H_SUCCESS) {
		ehca_err(&shca->ib_device, "Can't query port properties");
		ret = -EINVAL;
		goto query_gid1;
	}

	memcpy(&gid->raw[0], &rblock->gid_prefix, sizeof(u64));
	memcpy(&gid->raw[8], &rblock->guid_entries[index], sizeof(u64));

query_gid1:
	ehca_free_fw_ctrlblock(rblock);

	return ret;
}

const u32 allowed_port_caps = (
	IB_PORT_SM | IB_PORT_LED_INFO_SUP | IB_PORT_CM_SUP |
	IB_PORT_SNMP_TUNNEL_SUP | IB_PORT_DEVICE_MGMT_SUP |
	IB_PORT_VENDOR_CLASS_SUP);

int ehca_modify_port(struct ib_device *ibdev,
		     u8 port, int port_modify_mask,
		     struct ib_port_modify *props)
{
	int ret = 0;
	struct ehca_shca *shca;
	struct hipz_query_port *rblock;
	u32 cap;
	u64 hret;

	shca = container_of(ibdev, struct ehca_shca, ib_device);
	if ((props->set_port_cap_mask | props->clr_port_cap_mask)
	    & ~allowed_port_caps) {
		ehca_err(&shca->ib_device, "Non-changeable bits set in masks  "
			 "set=%x  clr=%x  allowed=%x", props->set_port_cap_mask,
			 props->clr_port_cap_mask, allowed_port_caps);
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&shca->modify_mutex))
		return -ERESTARTSYS;

	rblock = ehca_alloc_fw_ctrlblock(GFP_KERNEL);
	if (!rblock) {
		ehca_err(&shca->ib_device,  "Can't allocate rblock memory.");
		ret = -ENOMEM;
		goto modify_port1;
	}

	hret = hipz_h_query_port(shca->ipz_hca_handle, port, rblock);
	if (hret != H_SUCCESS) {
		ehca_err(&shca->ib_device, "Can't query port properties");
		ret = -EINVAL;
		goto modify_port2;
	}

	cap = (rblock->capability_mask | props->set_port_cap_mask)
		& ~props->clr_port_cap_mask;

	hret = hipz_h_modify_port(shca->ipz_hca_handle, port,
				  cap, props->init_type, port_modify_mask);
	if (hret != H_SUCCESS) {
		ehca_err(&shca->ib_device, "Modify port failed  h_ret=%li",
			 hret);
		ret = -EINVAL;
	}

modify_port2:
	ehca_free_fw_ctrlblock(rblock);

modify_port1:
	mutex_unlock(&shca->modify_mutex);

	return ret;
}
