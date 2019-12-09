/*
 * Copyright (c) 2016 Hisilicon Limited.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
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
#include <linux/acpi.h>
#include <linux/of_platform.h>
#include <linux/module.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_cache.h>
#include "hns_roce_common.h"
#include "hns_roce_device.h"
#include <rdma/hns-abi.h>
#include "hns_roce_hem.h"

/**
 * hns_get_gid_index - Get gid index.
 * @hr_dev: pointer to structure hns_roce_dev.
 * @port:  port, value range: 0 ~ MAX
 * @gid_index:  gid_index, value range: 0 ~ MAX
 * Description:
 *    N ports shared gids, allocation method as follow:
 *		GID[0][0], GID[1][0],.....GID[N - 1][0],
 *		GID[0][0], GID[1][0],.....GID[N - 1][0],
 *		And so on
 */
int hns_get_gid_index(struct hns_roce_dev *hr_dev, u8 port, int gid_index)
{
	return gid_index * hr_dev->caps.num_ports + port;
}

static int hns_roce_set_mac(struct hns_roce_dev *hr_dev, u8 port, u8 *addr)
{
	u8 phy_port;
	u32 i = 0;

	if (!memcmp(hr_dev->dev_addr[port], addr, ETH_ALEN))
		return 0;

	for (i = 0; i < ETH_ALEN; i++)
		hr_dev->dev_addr[port][i] = addr[i];

	phy_port = hr_dev->iboe.phy_port[port];
	return hr_dev->hw->set_mac(hr_dev, phy_port, addr);
}

static int hns_roce_add_gid(const struct ib_gid_attr *attr, void **context)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(attr->device);
	u8 port = attr->port_num - 1;
	int ret;

	if (port >= hr_dev->caps.num_ports)
		return -EINVAL;

	ret = hr_dev->hw->set_gid(hr_dev, port, attr->index, &attr->gid, attr);

	return ret;
}

static int hns_roce_del_gid(const struct ib_gid_attr *attr, void **context)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(attr->device);
	struct ib_gid_attr zattr = { };
	u8 port = attr->port_num - 1;
	int ret;

	if (port >= hr_dev->caps.num_ports)
		return -EINVAL;

	ret = hr_dev->hw->set_gid(hr_dev, port, attr->index, &zgid, &zattr);

	return ret;
}

static int handle_en_event(struct hns_roce_dev *hr_dev, u8 port,
			   unsigned long event)
{
	struct device *dev = hr_dev->dev;
	struct net_device *netdev;
	int ret = 0;

	netdev = hr_dev->iboe.netdevs[port];
	if (!netdev) {
		dev_err(dev, "Can't find netdev on port(%u)!\n", port);
		return -ENODEV;
	}

	switch (event) {
	case NETDEV_UP:
	case NETDEV_CHANGE:
	case NETDEV_REGISTER:
	case NETDEV_CHANGEADDR:
		ret = hns_roce_set_mac(hr_dev, port, netdev->dev_addr);
		break;
	case NETDEV_DOWN:
		/*
		 * In v1 engine, only support all ports closed together.
		 */
		break;
	default:
		dev_dbg(dev, "NETDEV event = 0x%x!\n", (u32)(event));
		break;
	}

	return ret;
}

static int hns_roce_netdev_event(struct notifier_block *self,
				 unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct hns_roce_ib_iboe *iboe = NULL;
	struct hns_roce_dev *hr_dev = NULL;
	u8 port = 0;
	int ret = 0;

	hr_dev = container_of(self, struct hns_roce_dev, iboe.nb);
	iboe = &hr_dev->iboe;

	for (port = 0; port < hr_dev->caps.num_ports; port++) {
		if (dev == iboe->netdevs[port]) {
			ret = handle_en_event(hr_dev, port, event);
			if (ret)
				return NOTIFY_DONE;
			break;
		}
	}

	return NOTIFY_DONE;
}

static int hns_roce_setup_mtu_mac(struct hns_roce_dev *hr_dev)
{
	int ret;
	u8 i;

	for (i = 0; i < hr_dev->caps.num_ports; i++) {
		if (hr_dev->hw->set_mtu)
			hr_dev->hw->set_mtu(hr_dev, hr_dev->iboe.phy_port[i],
					    hr_dev->caps.max_mtu);
		ret = hns_roce_set_mac(hr_dev, i,
				       hr_dev->iboe.netdevs[i]->dev_addr);
		if (ret)
			return ret;
	}

	return 0;
}

static int hns_roce_query_device(struct ib_device *ib_dev,
				 struct ib_device_attr *props,
				 struct ib_udata *uhw)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_dev);

	memset(props, 0, sizeof(*props));

	props->fw_ver = hr_dev->caps.fw_ver;
	props->sys_image_guid = cpu_to_be64(hr_dev->sys_image_guid);
	props->max_mr_size = (u64)(~(0ULL));
	props->page_size_cap = hr_dev->caps.page_size_cap;
	props->vendor_id = hr_dev->vendor_id;
	props->vendor_part_id = hr_dev->vendor_part_id;
	props->hw_ver = hr_dev->hw_rev;
	props->max_qp = hr_dev->caps.num_qps;
	props->max_qp_wr = hr_dev->caps.max_wqes;
	props->device_cap_flags = IB_DEVICE_PORT_ACTIVE_EVENT |
				  IB_DEVICE_RC_RNR_NAK_GEN;
	props->max_send_sge = hr_dev->caps.max_sq_sg;
	props->max_recv_sge = hr_dev->caps.max_rq_sg;
	props->max_sge_rd = 1;
	props->max_cq = hr_dev->caps.num_cqs;
	props->max_cqe = hr_dev->caps.max_cqes;
	props->max_mr = hr_dev->caps.num_mtpts;
	props->max_pd = hr_dev->caps.num_pds;
	props->max_qp_rd_atom = hr_dev->caps.max_qp_dest_rdma;
	props->max_qp_init_rd_atom = hr_dev->caps.max_qp_init_rdma;
	props->atomic_cap = hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_ATOMIC ?
			    IB_ATOMIC_HCA : IB_ATOMIC_NONE;
	props->max_pkeys = 1;
	props->local_ca_ack_delay = hr_dev->caps.local_ca_ack_delay;
	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SRQ) {
		props->max_srq = hr_dev->caps.max_srqs;
		props->max_srq_wr = hr_dev->caps.max_srq_wrs;
		props->max_srq_sge = hr_dev->caps.max_srq_sges;
	}

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_FRMR) {
		props->device_cap_flags |= IB_DEVICE_MEM_MGT_EXTENSIONS;
		props->max_fast_reg_page_list_len = HNS_ROCE_FRMR_MAX_PA;
	}

	return 0;
}

static int hns_roce_query_port(struct ib_device *ib_dev, u8 port_num,
			       struct ib_port_attr *props)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_dev);
	struct device *dev = hr_dev->dev;
	struct net_device *net_dev;
	unsigned long flags;
	enum ib_mtu mtu;
	u8 port;

	assert(port_num > 0);
	port = port_num - 1;

	/* props being zeroed by the caller, avoid zeroing it here */

	props->max_mtu = hr_dev->caps.max_mtu;
	props->gid_tbl_len = hr_dev->caps.gid_table_len[port];
	props->port_cap_flags = IB_PORT_CM_SUP | IB_PORT_REINIT_SUP |
				IB_PORT_VENDOR_CLASS_SUP |
				IB_PORT_BOOT_MGMT_SUP;
	props->max_msg_sz = HNS_ROCE_MAX_MSG_LEN;
	props->pkey_tbl_len = 1;
	props->active_width = IB_WIDTH_4X;
	props->active_speed = 1;

	spin_lock_irqsave(&hr_dev->iboe.lock, flags);

	net_dev = hr_dev->iboe.netdevs[port];
	if (!net_dev) {
		spin_unlock_irqrestore(&hr_dev->iboe.lock, flags);
		dev_err(dev, "Find netdev %u failed!\n", port);
		return -EINVAL;
	}

	mtu = iboe_get_mtu(net_dev->mtu);
	props->active_mtu = mtu ? min(props->max_mtu, mtu) : IB_MTU_256;
	props->state = (netif_running(net_dev) && netif_carrier_ok(net_dev)) ?
			IB_PORT_ACTIVE : IB_PORT_DOWN;
	props->phys_state = (props->state == IB_PORT_ACTIVE) ?
			     IB_PORT_PHYS_STATE_LINK_UP :
			     IB_PORT_PHYS_STATE_DISABLED;

	spin_unlock_irqrestore(&hr_dev->iboe.lock, flags);

	return 0;
}

static enum rdma_link_layer hns_roce_get_link_layer(struct ib_device *device,
						    u8 port_num)
{
	return IB_LINK_LAYER_ETHERNET;
}

static int hns_roce_query_pkey(struct ib_device *ib_dev, u8 port, u16 index,
			       u16 *pkey)
{
	*pkey = PKEY_ID;

	return 0;
}

static int hns_roce_modify_device(struct ib_device *ib_dev, int mask,
				  struct ib_device_modify *props)
{
	unsigned long flags;

	if (mask & ~IB_DEVICE_MODIFY_NODE_DESC)
		return -EOPNOTSUPP;

	if (mask & IB_DEVICE_MODIFY_NODE_DESC) {
		spin_lock_irqsave(&to_hr_dev(ib_dev)->sm_lock, flags);
		memcpy(ib_dev->node_desc, props->node_desc, NODE_DESC_SIZE);
		spin_unlock_irqrestore(&to_hr_dev(ib_dev)->sm_lock, flags);
	}

	return 0;
}

static int hns_roce_alloc_ucontext(struct ib_ucontext *uctx,
				   struct ib_udata *udata)
{
	int ret;
	struct hns_roce_ucontext *context = to_hr_ucontext(uctx);
	struct hns_roce_ib_alloc_ucontext_resp resp = {};
	struct hns_roce_dev *hr_dev = to_hr_dev(uctx->device);

	if (!hr_dev->active)
		return -EAGAIN;

	resp.qp_tab_size = hr_dev->caps.num_qps;

	ret = hns_roce_uar_alloc(hr_dev, &context->uar);
	if (ret)
		goto error_fail_uar_alloc;

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RECORD_DB) {
		INIT_LIST_HEAD(&context->page_list);
		mutex_init(&context->page_mutex);
	}

	ret = ib_copy_to_udata(udata, &resp, sizeof(resp));
	if (ret)
		goto error_fail_copy_to_udata;

	return 0;

error_fail_copy_to_udata:
	hns_roce_uar_free(hr_dev, &context->uar);

error_fail_uar_alloc:
	return ret;
}

static void hns_roce_dealloc_ucontext(struct ib_ucontext *ibcontext)
{
	struct hns_roce_ucontext *context = to_hr_ucontext(ibcontext);

	hns_roce_uar_free(to_hr_dev(ibcontext->device), &context->uar);
}

static int hns_roce_mmap(struct ib_ucontext *context,
			 struct vm_area_struct *vma)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(context->device);

	switch (vma->vm_pgoff) {
	case 0:
		return rdma_user_mmap_io(context, vma,
					 to_hr_ucontext(context)->uar.pfn,
					 PAGE_SIZE,
					 pgprot_noncached(vma->vm_page_prot),
					 NULL);

	/* vm_pgoff: 1 -- TPTR */
	case 1:
		if (!hr_dev->tptr_dma_addr || !hr_dev->tptr_size)
			return -EINVAL;
		/*
		 * FIXME: using io_remap_pfn_range on the dma address returned
		 * by dma_alloc_coherent is totally wrong.
		 */
		return rdma_user_mmap_io(context, vma,
					 hr_dev->tptr_dma_addr >> PAGE_SHIFT,
					 hr_dev->tptr_size,
					 vma->vm_page_prot,
					 NULL);

	default:
		return -EINVAL;
	}
}

static int hns_roce_port_immutable(struct ib_device *ib_dev, u8 port_num,
				   struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int ret;

	ret = ib_query_port(ib_dev, port_num, &attr);
	if (ret)
		return ret;

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;

	immutable->max_mad_size = IB_MGMT_MAD_SIZE;
	immutable->core_cap_flags = RDMA_CORE_PORT_IBA_ROCE;
	if (to_hr_dev(ib_dev)->caps.flags & HNS_ROCE_CAP_FLAG_ROCE_V1_V2)
		immutable->core_cap_flags |= RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP;

	return 0;
}

static void hns_roce_disassociate_ucontext(struct ib_ucontext *ibcontext)
{
}

static void hns_roce_unregister_device(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_ib_iboe *iboe = &hr_dev->iboe;

	hr_dev->active = false;
	unregister_netdevice_notifier(&iboe->nb);
	ib_unregister_device(&hr_dev->ib_dev);
}

static const struct ib_device_ops hns_roce_dev_ops = {
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_HNS,
	.uverbs_abi_ver = 1,
	.uverbs_no_driver_id_binding = 1,

	.add_gid = hns_roce_add_gid,
	.alloc_pd = hns_roce_alloc_pd,
	.alloc_ucontext = hns_roce_alloc_ucontext,
	.create_ah = hns_roce_create_ah,
	.create_cq = hns_roce_create_cq,
	.create_qp = hns_roce_create_qp,
	.dealloc_pd = hns_roce_dealloc_pd,
	.dealloc_ucontext = hns_roce_dealloc_ucontext,
	.del_gid = hns_roce_del_gid,
	.dereg_mr = hns_roce_dereg_mr,
	.destroy_ah = hns_roce_destroy_ah,
	.destroy_cq = hns_roce_destroy_cq,
	.disassociate_ucontext = hns_roce_disassociate_ucontext,
	.fill_res_entry = hns_roce_fill_res_entry,
	.get_dma_mr = hns_roce_get_dma_mr,
	.get_link_layer = hns_roce_get_link_layer,
	.get_port_immutable = hns_roce_port_immutable,
	.mmap = hns_roce_mmap,
	.modify_device = hns_roce_modify_device,
	.modify_qp = hns_roce_modify_qp,
	.query_ah = hns_roce_query_ah,
	.query_device = hns_roce_query_device,
	.query_pkey = hns_roce_query_pkey,
	.query_port = hns_roce_query_port,
	.reg_user_mr = hns_roce_reg_user_mr,

	INIT_RDMA_OBJ_SIZE(ib_ah, hns_roce_ah, ibah),
	INIT_RDMA_OBJ_SIZE(ib_cq, hns_roce_cq, ib_cq),
	INIT_RDMA_OBJ_SIZE(ib_pd, hns_roce_pd, ibpd),
	INIT_RDMA_OBJ_SIZE(ib_ucontext, hns_roce_ucontext, ibucontext),
};

static const struct ib_device_ops hns_roce_dev_mr_ops = {
	.rereg_user_mr = hns_roce_rereg_user_mr,
};

static const struct ib_device_ops hns_roce_dev_mw_ops = {
	.alloc_mw = hns_roce_alloc_mw,
	.dealloc_mw = hns_roce_dealloc_mw,
};

static const struct ib_device_ops hns_roce_dev_frmr_ops = {
	.alloc_mr = hns_roce_alloc_mr,
	.map_mr_sg = hns_roce_map_mr_sg,
};

static const struct ib_device_ops hns_roce_dev_srq_ops = {
	.create_srq = hns_roce_create_srq,
	.destroy_srq = hns_roce_destroy_srq,

	INIT_RDMA_OBJ_SIZE(ib_srq, hns_roce_srq, ibsrq),
};

static int hns_roce_register_device(struct hns_roce_dev *hr_dev)
{
	int ret;
	struct hns_roce_ib_iboe *iboe = NULL;
	struct ib_device *ib_dev = NULL;
	struct device *dev = hr_dev->dev;
	unsigned int i;

	iboe = &hr_dev->iboe;
	spin_lock_init(&iboe->lock);

	ib_dev = &hr_dev->ib_dev;

	ib_dev->node_type		= RDMA_NODE_IB_CA;
	ib_dev->dev.parent		= dev;

	ib_dev->phys_port_cnt		= hr_dev->caps.num_ports;
	ib_dev->local_dma_lkey		= hr_dev->caps.reserved_lkey;
	ib_dev->num_comp_vectors	= hr_dev->caps.num_comp_vectors;
	ib_dev->uverbs_cmd_mask		=
		(1ULL << IB_USER_VERBS_CMD_GET_CONTEXT) |
		(1ULL << IB_USER_VERBS_CMD_QUERY_DEVICE) |
		(1ULL << IB_USER_VERBS_CMD_QUERY_PORT) |
		(1ULL << IB_USER_VERBS_CMD_ALLOC_PD) |
		(1ULL << IB_USER_VERBS_CMD_DEALLOC_PD) |
		(1ULL << IB_USER_VERBS_CMD_REG_MR) |
		(1ULL << IB_USER_VERBS_CMD_DEREG_MR) |
		(1ULL << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL) |
		(1ULL << IB_USER_VERBS_CMD_CREATE_CQ) |
		(1ULL << IB_USER_VERBS_CMD_DESTROY_CQ) |
		(1ULL << IB_USER_VERBS_CMD_CREATE_QP) |
		(1ULL << IB_USER_VERBS_CMD_MODIFY_QP) |
		(1ULL << IB_USER_VERBS_CMD_QUERY_QP) |
		(1ULL << IB_USER_VERBS_CMD_DESTROY_QP);

	ib_dev->uverbs_ex_cmd_mask |=
		(1ULL << IB_USER_VERBS_EX_CMD_MODIFY_CQ);

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_REREG_MR) {
		ib_dev->uverbs_cmd_mask |= (1ULL << IB_USER_VERBS_CMD_REREG_MR);
		ib_set_device_ops(ib_dev, &hns_roce_dev_mr_ops);
	}

	/* MW */
	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_MW) {
		ib_dev->uverbs_cmd_mask |=
					(1ULL << IB_USER_VERBS_CMD_ALLOC_MW) |
					(1ULL << IB_USER_VERBS_CMD_DEALLOC_MW);
		ib_set_device_ops(ib_dev, &hns_roce_dev_mw_ops);
	}

	/* FRMR */
	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_FRMR)
		ib_set_device_ops(ib_dev, &hns_roce_dev_frmr_ops);

	/* SRQ */
	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SRQ) {
		ib_dev->uverbs_cmd_mask |=
				(1ULL << IB_USER_VERBS_CMD_CREATE_SRQ) |
				(1ULL << IB_USER_VERBS_CMD_MODIFY_SRQ) |
				(1ULL << IB_USER_VERBS_CMD_QUERY_SRQ) |
				(1ULL << IB_USER_VERBS_CMD_DESTROY_SRQ) |
				(1ULL << IB_USER_VERBS_CMD_POST_SRQ_RECV);
		ib_set_device_ops(ib_dev, &hns_roce_dev_srq_ops);
		ib_set_device_ops(ib_dev, hr_dev->hw->hns_roce_dev_srq_ops);
	}

	ib_set_device_ops(ib_dev, hr_dev->hw->hns_roce_dev_ops);
	ib_set_device_ops(ib_dev, &hns_roce_dev_ops);
	for (i = 0; i < hr_dev->caps.num_ports; i++) {
		if (!hr_dev->iboe.netdevs[i])
			continue;

		ret = ib_device_set_netdev(ib_dev, hr_dev->iboe.netdevs[i],
					   i + 1);
		if (ret)
			return ret;
	}
	ret = ib_register_device(ib_dev, "hns_%d");
	if (ret) {
		dev_err(dev, "ib_register_device failed!\n");
		return ret;
	}

	ret = hns_roce_setup_mtu_mac(hr_dev);
	if (ret) {
		dev_err(dev, "setup_mtu_mac failed!\n");
		goto error_failed_setup_mtu_mac;
	}

	iboe->nb.notifier_call = hns_roce_netdev_event;
	ret = register_netdevice_notifier(&iboe->nb);
	if (ret) {
		dev_err(dev, "register_netdevice_notifier failed!\n");
		goto error_failed_setup_mtu_mac;
	}

	hr_dev->active = true;
	return 0;

error_failed_setup_mtu_mac:
	ib_unregister_device(ib_dev);

	return ret;
}

static int hns_roce_init_hem(struct hns_roce_dev *hr_dev)
{
	int ret;
	struct device *dev = hr_dev->dev;

	ret = hns_roce_init_hem_table(hr_dev, &hr_dev->mr_table.mtt_table,
				      HEM_TYPE_MTT, hr_dev->caps.mtt_entry_sz,
				      hr_dev->caps.num_mtt_segs, 1);
	if (ret) {
		dev_err(dev, "Failed to init MTT context memory, aborting.\n");
		return ret;
	}

	if (hns_roce_check_whether_mhop(hr_dev, HEM_TYPE_CQE)) {
		ret = hns_roce_init_hem_table(hr_dev,
				      &hr_dev->mr_table.mtt_cqe_table,
				      HEM_TYPE_CQE, hr_dev->caps.mtt_entry_sz,
				      hr_dev->caps.num_cqe_segs, 1);
		if (ret) {
			dev_err(dev, "Failed to init MTT CQE context memory, aborting.\n");
			goto err_unmap_cqe;
		}
	}

	ret = hns_roce_init_hem_table(hr_dev, &hr_dev->mr_table.mtpt_table,
				      HEM_TYPE_MTPT, hr_dev->caps.mtpt_entry_sz,
				      hr_dev->caps.num_mtpts, 1);
	if (ret) {
		dev_err(dev, "Failed to init MTPT context memory, aborting.\n");
		goto err_unmap_mtt;
	}

	ret = hns_roce_init_hem_table(hr_dev, &hr_dev->qp_table.qp_table,
				      HEM_TYPE_QPC, hr_dev->caps.qpc_entry_sz,
				      hr_dev->caps.num_qps, 1);
	if (ret) {
		dev_err(dev, "Failed to init QP context memory, aborting.\n");
		goto err_unmap_dmpt;
	}

	ret = hns_roce_init_hem_table(hr_dev, &hr_dev->qp_table.irrl_table,
				      HEM_TYPE_IRRL,
				      hr_dev->caps.irrl_entry_sz *
				      hr_dev->caps.max_qp_init_rdma,
				      hr_dev->caps.num_qps, 1);
	if (ret) {
		dev_err(dev, "Failed to init irrl_table memory, aborting.\n");
		goto err_unmap_qp;
	}

	if (hr_dev->caps.trrl_entry_sz) {
		ret = hns_roce_init_hem_table(hr_dev,
					      &hr_dev->qp_table.trrl_table,
					      HEM_TYPE_TRRL,
					      hr_dev->caps.trrl_entry_sz *
					      hr_dev->caps.max_qp_dest_rdma,
					      hr_dev->caps.num_qps, 1);
		if (ret) {
			dev_err(dev,
			       "Failed to init trrl_table memory, aborting.\n");
			goto err_unmap_irrl;
		}
	}

	ret = hns_roce_init_hem_table(hr_dev, &hr_dev->cq_table.table,
				      HEM_TYPE_CQC, hr_dev->caps.cqc_entry_sz,
				      hr_dev->caps.num_cqs, 1);
	if (ret) {
		dev_err(dev, "Failed to init CQ context memory, aborting.\n");
		goto err_unmap_trrl;
	}

	if (hr_dev->caps.srqc_entry_sz) {
		ret = hns_roce_init_hem_table(hr_dev, &hr_dev->srq_table.table,
					      HEM_TYPE_SRQC,
					      hr_dev->caps.srqc_entry_sz,
					      hr_dev->caps.num_srqs, 1);
		if (ret) {
			dev_err(dev,
			      "Failed to init SRQ context memory, aborting.\n");
			goto err_unmap_cq;
		}
	}

	if (hr_dev->caps.num_srqwqe_segs) {
		ret = hns_roce_init_hem_table(hr_dev,
					     &hr_dev->mr_table.mtt_srqwqe_table,
					     HEM_TYPE_SRQWQE,
					     hr_dev->caps.mtt_entry_sz,
					     hr_dev->caps.num_srqwqe_segs, 1);
		if (ret) {
			dev_err(dev,
				"Failed to init MTT srqwqe memory, aborting.\n");
			goto err_unmap_srq;
		}
	}

	if (hr_dev->caps.num_idx_segs) {
		ret = hns_roce_init_hem_table(hr_dev,
					      &hr_dev->mr_table.mtt_idx_table,
					      HEM_TYPE_IDX,
					      hr_dev->caps.idx_entry_sz,
					      hr_dev->caps.num_idx_segs, 1);
		if (ret) {
			dev_err(dev,
				"Failed to init MTT idx memory, aborting.\n");
			goto err_unmap_srqwqe;
		}
	}

	if (hr_dev->caps.sccc_entry_sz) {
		ret = hns_roce_init_hem_table(hr_dev,
					      &hr_dev->qp_table.sccc_table,
					      HEM_TYPE_SCCC,
					      hr_dev->caps.sccc_entry_sz,
					      hr_dev->caps.num_qps, 1);
		if (ret) {
			dev_err(dev,
			      "Failed to init SCC context memory, aborting.\n");
			goto err_unmap_idx;
		}
	}

	if (hr_dev->caps.qpc_timer_entry_sz) {
		ret = hns_roce_init_hem_table(hr_dev,
					      &hr_dev->qpc_timer_table,
					      HEM_TYPE_QPC_TIMER,
					      hr_dev->caps.qpc_timer_entry_sz,
					      hr_dev->caps.num_qpc_timer, 1);
		if (ret) {
			dev_err(dev,
			      "Failed to init QPC timer memory, aborting.\n");
			goto err_unmap_ctx;
		}
	}

	if (hr_dev->caps.cqc_timer_entry_sz) {
		ret = hns_roce_init_hem_table(hr_dev,
					      &hr_dev->cqc_timer_table,
					      HEM_TYPE_CQC_TIMER,
					      hr_dev->caps.cqc_timer_entry_sz,
					      hr_dev->caps.num_cqc_timer, 1);
		if (ret) {
			dev_err(dev,
			      "Failed to init CQC timer memory, aborting.\n");
			goto err_unmap_qpc_timer;
		}
	}

	return 0;

err_unmap_qpc_timer:
	if (hr_dev->caps.qpc_timer_entry_sz)
		hns_roce_cleanup_hem_table(hr_dev,
					   &hr_dev->qpc_timer_table);

err_unmap_ctx:
	if (hr_dev->caps.sccc_entry_sz)
		hns_roce_cleanup_hem_table(hr_dev,
					   &hr_dev->qp_table.sccc_table);

err_unmap_idx:
	if (hr_dev->caps.num_idx_segs)
		hns_roce_cleanup_hem_table(hr_dev,
					   &hr_dev->mr_table.mtt_idx_table);

err_unmap_srqwqe:
	if (hr_dev->caps.num_srqwqe_segs)
		hns_roce_cleanup_hem_table(hr_dev,
					   &hr_dev->mr_table.mtt_srqwqe_table);

err_unmap_srq:
	if (hr_dev->caps.srqc_entry_sz)
		hns_roce_cleanup_hem_table(hr_dev, &hr_dev->srq_table.table);

err_unmap_cq:
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->cq_table.table);

err_unmap_trrl:
	if (hr_dev->caps.trrl_entry_sz)
		hns_roce_cleanup_hem_table(hr_dev,
					   &hr_dev->qp_table.trrl_table);

err_unmap_irrl:
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->qp_table.irrl_table);

err_unmap_qp:
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->qp_table.qp_table);

err_unmap_dmpt:
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->mr_table.mtpt_table);

err_unmap_mtt:
	if (hns_roce_check_whether_mhop(hr_dev, HEM_TYPE_CQE))
		hns_roce_cleanup_hem_table(hr_dev,
					   &hr_dev->mr_table.mtt_cqe_table);

err_unmap_cqe:
	hns_roce_cleanup_hem_table(hr_dev, &hr_dev->mr_table.mtt_table);

	return ret;
}

/**
 * hns_roce_setup_hca - setup host channel adapter
 * @hr_dev: pointer to hns roce device
 * Return : int
 */
static int hns_roce_setup_hca(struct hns_roce_dev *hr_dev)
{
	int ret;
	struct device *dev = hr_dev->dev;

	spin_lock_init(&hr_dev->sm_lock);
	spin_lock_init(&hr_dev->bt_cmd_lock);

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RECORD_DB) {
		INIT_LIST_HEAD(&hr_dev->pgdir_list);
		mutex_init(&hr_dev->pgdir_mutex);
	}

	ret = hns_roce_init_uar_table(hr_dev);
	if (ret) {
		dev_err(dev, "Failed to initialize uar table. aborting\n");
		return ret;
	}

	ret = hns_roce_uar_alloc(hr_dev, &hr_dev->priv_uar);
	if (ret) {
		dev_err(dev, "Failed to allocate priv_uar.\n");
		goto err_uar_table_free;
	}

	ret = hns_roce_init_pd_table(hr_dev);
	if (ret) {
		dev_err(dev, "Failed to init protected domain table.\n");
		goto err_uar_alloc_free;
	}

	ret = hns_roce_init_mr_table(hr_dev);
	if (ret) {
		dev_err(dev, "Failed to init memory region table.\n");
		goto err_pd_table_free;
	}

	ret = hns_roce_init_cq_table(hr_dev);
	if (ret) {
		dev_err(dev, "Failed to init completion queue table.\n");
		goto err_mr_table_free;
	}

	ret = hns_roce_init_qp_table(hr_dev);
	if (ret) {
		dev_err(dev, "Failed to init queue pair table.\n");
		goto err_cq_table_free;
	}

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SRQ) {
		ret = hns_roce_init_srq_table(hr_dev);
		if (ret) {
			dev_err(dev,
				"Failed to init share receive queue table.\n");
			goto err_qp_table_free;
		}
	}

	return 0;

err_qp_table_free:
	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SRQ)
		hns_roce_cleanup_qp_table(hr_dev);

err_cq_table_free:
	hns_roce_cleanup_cq_table(hr_dev);

err_mr_table_free:
	hns_roce_cleanup_mr_table(hr_dev);

err_pd_table_free:
	hns_roce_cleanup_pd_table(hr_dev);

err_uar_alloc_free:
	hns_roce_uar_free(hr_dev, &hr_dev->priv_uar);

err_uar_table_free:
	hns_roce_cleanup_uar_table(hr_dev);
	return ret;
}

int hns_roce_init(struct hns_roce_dev *hr_dev)
{
	int ret;
	struct device *dev = hr_dev->dev;

	if (hr_dev->hw->reset) {
		ret = hr_dev->hw->reset(hr_dev, true);
		if (ret) {
			dev_err(dev, "Reset RoCE engine failed!\n");
			return ret;
		}
	}
	hr_dev->is_reset = false;

	if (hr_dev->hw->cmq_init) {
		ret = hr_dev->hw->cmq_init(hr_dev);
		if (ret) {
			dev_err(dev, "Init RoCE Command Queue failed!\n");
			goto error_failed_cmq_init;
		}
	}

	ret = hr_dev->hw->hw_profile(hr_dev);
	if (ret) {
		dev_err(dev, "Get RoCE engine profile failed!\n");
		goto error_failed_cmd_init;
	}

	ret = hns_roce_cmd_init(hr_dev);
	if (ret) {
		dev_err(dev, "cmd init failed!\n");
		goto error_failed_cmd_init;
	}

	/* EQ depends on poll mode, event mode depends on EQ */
	ret = hr_dev->hw->init_eq(hr_dev);
	if (ret) {
		dev_err(dev, "eq init failed!\n");
		goto error_failed_eq_table;
	}

	if (hr_dev->cmd_mod) {
		ret = hns_roce_cmd_use_events(hr_dev);
		if (ret) {
			dev_warn(dev,
				 "Cmd event  mode failed, set back to poll!\n");
			hns_roce_cmd_use_polling(hr_dev);
		}
	}

	ret = hns_roce_init_hem(hr_dev);
	if (ret) {
		dev_err(dev, "init HEM(Hardware Entry Memory) failed!\n");
		goto error_failed_init_hem;
	}

	ret = hns_roce_setup_hca(hr_dev);
	if (ret) {
		dev_err(dev, "setup hca failed!\n");
		goto error_failed_setup_hca;
	}

	if (hr_dev->hw->hw_init) {
		ret = hr_dev->hw->hw_init(hr_dev);
		if (ret) {
			dev_err(dev, "hw_init failed!\n");
			goto error_failed_engine_init;
		}
	}

	ret = hns_roce_register_device(hr_dev);
	if (ret)
		goto error_failed_register_device;

	return 0;

error_failed_register_device:
	if (hr_dev->hw->hw_exit)
		hr_dev->hw->hw_exit(hr_dev);

error_failed_engine_init:
	hns_roce_cleanup_bitmap(hr_dev);

error_failed_setup_hca:
	hns_roce_cleanup_hem(hr_dev);

error_failed_init_hem:
	if (hr_dev->cmd_mod)
		hns_roce_cmd_use_polling(hr_dev);
	hr_dev->hw->cleanup_eq(hr_dev);

error_failed_eq_table:
	hns_roce_cmd_cleanup(hr_dev);

error_failed_cmd_init:
	if (hr_dev->hw->cmq_exit)
		hr_dev->hw->cmq_exit(hr_dev);

error_failed_cmq_init:
	if (hr_dev->hw->reset) {
		if (hr_dev->hw->reset(hr_dev, false))
			dev_err(dev, "Dereset RoCE engine failed!\n");
	}

	return ret;
}

void hns_roce_exit(struct hns_roce_dev *hr_dev)
{
	hns_roce_unregister_device(hr_dev);

	if (hr_dev->hw->hw_exit)
		hr_dev->hw->hw_exit(hr_dev);
	hns_roce_cleanup_bitmap(hr_dev);
	hns_roce_cleanup_hem(hr_dev);

	if (hr_dev->cmd_mod)
		hns_roce_cmd_use_polling(hr_dev);

	hr_dev->hw->cleanup_eq(hr_dev);
	hns_roce_cmd_cleanup(hr_dev);
	if (hr_dev->hw->cmq_exit)
		hr_dev->hw->cmq_exit(hr_dev);
	if (hr_dev->hw->reset)
		hr_dev->hw->reset(hr_dev, false);
}

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Wei Hu <xavier.huwei@huawei.com>");
MODULE_AUTHOR("Nenglong Zhao <zhaonenglong@hisilicon.com>");
MODULE_AUTHOR("Lijun Ou <oulijun@huawei.com>");
MODULE_DESCRIPTION("HNS RoCE Driver");
