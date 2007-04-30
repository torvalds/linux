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
	int ret = 0;
	struct ehca_shca *shca = container_of(ibdev, struct ehca_shca,
					      ib_device);
	struct hipz_query_hca *rblock;

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
	props->fw_ver          = rblock->hw_ver;
	props->max_mr_size     = rblock->max_mr_size;
	props->vendor_id       = rblock->vendor_id >> 8;
	props->vendor_part_id  = rblock->vendor_part_id >> 16;
	props->hw_ver          = rblock->hw_ver;
	props->max_qp          = min_t(int, rblock->max_qp, INT_MAX);
	props->max_qp_wr       = min_t(int, rblock->max_wqes_wq, INT_MAX);
	props->max_sge         = min_t(int, rblock->max_sge, INT_MAX);
	props->max_sge_rd      = min_t(int, rblock->max_sge_rd, INT_MAX);
	props->max_cq          = min_t(int, rblock->max_cq, INT_MAX);
	props->max_cqe         = min_t(int, rblock->max_cqe, INT_MAX);
	props->max_mr          = min_t(int, rblock->max_mr, INT_MAX);
	props->max_mw          = min_t(int, rblock->max_mw, INT_MAX);
	props->max_pd          = min_t(int, rblock->max_pd, INT_MAX);
	props->max_ah          = min_t(int, rblock->max_ah, INT_MAX);
	props->max_fmr         = min_t(int, rblock->max_mr, INT_MAX);
	props->max_srq         = 0;
	props->max_srq_wr      = 0;
	props->max_srq_sge     = 0;
	props->max_pkeys       = 16;
	props->local_ca_ack_delay
		= rblock->local_ca_ack_delay;
	props->max_raw_ipv6_qp
		= min_t(int, rblock->max_raw_ipv6_qp, INT_MAX);
	props->max_raw_ethy_qp
		= min_t(int, rblock->max_raw_ethy_qp, INT_MAX);
	props->max_mcast_grp
		= min_t(int, rblock->max_mcast_grp, INT_MAX);
	props->max_mcast_qp_attach
		= min_t(int, rblock->max_mcast_qp_attach, INT_MAX);
	props->max_total_mcast_qp_attach
		= min_t(int, rblock->max_total_mcast_qp_attach, INT_MAX);

query_device1:
	ehca_free_fw_ctrlblock(rblock);

	return ret;
}

int ehca_query_port(struct ib_device *ibdev,
		    u8 port, struct ib_port_attr *props)
{
	int ret = 0;
	struct ehca_shca *shca = container_of(ibdev, struct ehca_shca,
					      ib_device);
	struct hipz_query_port *rblock;

	rblock = ehca_alloc_fw_ctrlblock(GFP_KERNEL);
	if (!rblock) {
		ehca_err(&shca->ib_device, "Can't allocate rblock memory.");
		return -ENOMEM;
	}

	if (hipz_h_query_port(shca->ipz_hca_handle, port, rblock) != H_SUCCESS) {
		ehca_err(&shca->ib_device, "Can't query port properties");
		ret = -EINVAL;
		goto query_port1;
	}

	memset(props, 0, sizeof(struct ib_port_attr));
	props->state = rblock->state;

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

	props->active_width    = IB_WIDTH_12X;
	props->active_speed    = 0x1;

	/* at the moment (logical) link state is always LINK_UP */
	props->phys_state      = 0x5;

query_port1:
	ehca_free_fw_ctrlblock(rblock);

	return ret;
}

int ehca_query_pkey(struct ib_device *ibdev, u8 port, u16 index, u16 *pkey)
{
	int ret = 0;
	struct ehca_shca *shca = container_of(ibdev, struct ehca_shca, ib_device);
	struct hipz_query_port *rblock;

	if (index > 16) {
		ehca_err(&shca->ib_device, "Invalid index: %x.", index);
		return -EINVAL;
	}

	rblock = ehca_alloc_fw_ctrlblock(GFP_KERNEL);
	if (!rblock) {
		ehca_err(&shca->ib_device,  "Can't allocate rblock memory.");
		return -ENOMEM;
	}

	if (hipz_h_query_port(shca->ipz_hca_handle, port, rblock) != H_SUCCESS) {
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

	if (hipz_h_query_port(shca->ipz_hca_handle, port, rblock) != H_SUCCESS) {
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
	struct ehca_shca *shca = container_of(ibdev, struct ehca_shca, ib_device);
	struct hipz_query_port *rblock;
	u32 cap;
	u64 hret;

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

	if (hipz_h_query_port(shca->ipz_hca_handle, port, rblock) != H_SUCCESS) {
		ehca_err(&shca->ib_device, "Can't query port properties");
		ret = -EINVAL;
		goto modify_port2;
	}

	cap = (rblock->capability_mask | props->set_port_cap_mask)
		& ~props->clr_port_cap_mask;

	hret = hipz_h_modify_port(shca->ipz_hca_handle, port,
				  cap, props->init_type, port_modify_mask);
	if (hret != H_SUCCESS) {
		ehca_err(&shca->ib_device, "Modify port failed  hret=%lx", hret);
		ret = -EINVAL;
	}

modify_port2:
	ehca_free_fw_ctrlblock(rblock);

modify_port1:
        mutex_unlock(&shca->modify_mutex);

	return ret;
}
