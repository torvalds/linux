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
#include <linux/module.h>
#include <linux/pci.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_cache.h>
#include "hns_roce_common.h"
#include "hns_roce_device.h"
#include "hns_roce_hem.h"

static int hns_roce_set_mac(struct hns_roce_dev *hr_dev, u32 port,
			    const u8 *addr)
{
	u8 phy_port;
	u32 i;

	if (hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP09)
		return 0;

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
	u32 port = attr->port_num - 1;
	int ret;

	if (port >= hr_dev->caps.num_ports)
		return -EINVAL;

	ret = hr_dev->hw->set_gid(hr_dev, attr->index, &attr->gid, attr);

	return ret;
}

static int hns_roce_del_gid(const struct ib_gid_attr *attr, void **context)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(attr->device);
	u32 port = attr->port_num - 1;
	int ret;

	if (port >= hr_dev->caps.num_ports)
		return -EINVAL;

	ret = hr_dev->hw->set_gid(hr_dev, attr->index, NULL, NULL);

	return ret;
}

static int handle_en_event(struct hns_roce_dev *hr_dev, u32 port,
			   unsigned long event)
{
	struct device *dev = hr_dev->dev;
	struct net_device *netdev;
	int ret = 0;

	netdev = hr_dev->iboe.netdevs[port];
	if (!netdev) {
		dev_err(dev, "can't find netdev on port(%u)!\n", port);
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
	int ret;
	u32 port;

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
		props->max_srq = hr_dev->caps.num_srqs;
		props->max_srq_wr = hr_dev->caps.max_srq_wrs;
		props->max_srq_sge = hr_dev->caps.max_srq_sges;
	}

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_FRMR &&
	    hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP09) {
		props->device_cap_flags |= IB_DEVICE_MEM_MGT_EXTENSIONS;
		props->max_fast_reg_page_list_len = HNS_ROCE_FRMR_MAX_PA;
	}

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_XRC)
		props->device_cap_flags |= IB_DEVICE_XRC;

	return 0;
}

static int hns_roce_query_port(struct ib_device *ib_dev, u32 port_num,
			       struct ib_port_attr *props)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_dev);
	struct device *dev = hr_dev->dev;
	struct net_device *net_dev;
	unsigned long flags;
	enum ib_mtu mtu;
	u32 port;

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
		dev_err(dev, "find netdev %u failed!\n", port);
		return -EINVAL;
	}

	mtu = iboe_get_mtu(net_dev->mtu);
	props->active_mtu = mtu ? min(props->max_mtu, mtu) : IB_MTU_256;
	props->state = netif_running(net_dev) && netif_carrier_ok(net_dev) ?
			       IB_PORT_ACTIVE :
			       IB_PORT_DOWN;
	props->phys_state = props->state == IB_PORT_ACTIVE ?
				    IB_PORT_PHYS_STATE_LINK_UP :
				    IB_PORT_PHYS_STATE_DISABLED;

	spin_unlock_irqrestore(&hr_dev->iboe.lock, flags);

	return 0;
}

static enum rdma_link_layer hns_roce_get_link_layer(struct ib_device *device,
						    u32 port_num)
{
	return IB_LINK_LAYER_ETHERNET;
}

static int hns_roce_query_pkey(struct ib_device *ib_dev, u32 port, u16 index,
			       u16 *pkey)
{
	if (index > 0)
		return -EINVAL;

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

struct hns_user_mmap_entry *
hns_roce_user_mmap_entry_insert(struct ib_ucontext *ucontext, u64 address,
				size_t length,
				enum hns_roce_mmap_type mmap_type)
{
	struct hns_user_mmap_entry *entry;
	int ret;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return NULL;

	entry->address = address;
	entry->mmap_type = mmap_type;

	switch (mmap_type) {
	/* pgoff 0 must be used by DB for compatibility */
	case HNS_ROCE_MMAP_TYPE_DB:
		ret = rdma_user_mmap_entry_insert_exact(
				ucontext, &entry->rdma_entry, length, 0);
		break;
	case HNS_ROCE_MMAP_TYPE_DWQE:
		ret = rdma_user_mmap_entry_insert_range(
				ucontext, &entry->rdma_entry, length, 1,
				U32_MAX);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if (ret) {
		kfree(entry);
		return NULL;
	}

	return entry;
}

static void hns_roce_dealloc_uar_entry(struct hns_roce_ucontext *context)
{
	if (context->db_mmap_entry)
		rdma_user_mmap_entry_remove(
			&context->db_mmap_entry->rdma_entry);
}

static int hns_roce_alloc_uar_entry(struct ib_ucontext *uctx)
{
	struct hns_roce_ucontext *context = to_hr_ucontext(uctx);
	u64 address;

	address = context->uar.pfn << PAGE_SHIFT;
	context->db_mmap_entry = hns_roce_user_mmap_entry_insert(
		uctx, address, PAGE_SIZE, HNS_ROCE_MMAP_TYPE_DB);
	if (!context->db_mmap_entry)
		return -ENOMEM;

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
	resp.srq_tab_size = hr_dev->caps.num_srqs;

	ret = hns_roce_uar_alloc(hr_dev, &context->uar);
	if (ret)
		goto error_fail_uar_alloc;

	ret = hns_roce_alloc_uar_entry(uctx);
	if (ret)
		goto error_fail_uar_entry;

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_CQ_RECORD_DB ||
	    hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_QP_RECORD_DB) {
		INIT_LIST_HEAD(&context->page_list);
		mutex_init(&context->page_mutex);
	}

	resp.cqe_size = hr_dev->caps.cqe_sz;

	ret = ib_copy_to_udata(udata, &resp,
			       min(udata->outlen, sizeof(resp)));
	if (ret)
		goto error_fail_copy_to_udata;

	return 0;

error_fail_copy_to_udata:
	hns_roce_dealloc_uar_entry(context);

error_fail_uar_entry:
	ida_free(&hr_dev->uar_ida.ida, (int)context->uar.logic_idx);

error_fail_uar_alloc:
	return ret;
}

static void hns_roce_dealloc_ucontext(struct ib_ucontext *ibcontext)
{
	struct hns_roce_ucontext *context = to_hr_ucontext(ibcontext);
	struct hns_roce_dev *hr_dev = to_hr_dev(ibcontext->device);

	hns_roce_dealloc_uar_entry(context);

	ida_free(&hr_dev->uar_ida.ida, (int)context->uar.logic_idx);
}

static int hns_roce_mmap(struct ib_ucontext *uctx, struct vm_area_struct *vma)
{
	struct rdma_user_mmap_entry *rdma_entry;
	struct hns_user_mmap_entry *entry;
	phys_addr_t pfn;
	pgprot_t prot;
	int ret;

	rdma_entry = rdma_user_mmap_entry_get_pgoff(uctx, vma->vm_pgoff);
	if (!rdma_entry)
		return -EINVAL;

	entry = to_hns_mmap(rdma_entry);
	pfn = entry->address >> PAGE_SHIFT;

	switch (entry->mmap_type) {
	case HNS_ROCE_MMAP_TYPE_DB:
	case HNS_ROCE_MMAP_TYPE_DWQE:
		prot = pgprot_device(vma->vm_page_prot);
		break;
	default:
		return -EINVAL;
	}

	ret = rdma_user_mmap_io(uctx, vma, pfn, rdma_entry->npages * PAGE_SIZE,
				prot, rdma_entry);

	rdma_user_mmap_entry_put(rdma_entry);

	return ret;
}

static void hns_roce_free_mmap(struct rdma_user_mmap_entry *rdma_entry)
{
	struct hns_user_mmap_entry *entry = to_hns_mmap(rdma_entry);

	kfree(entry);
}

static int hns_roce_port_immutable(struct ib_device *ib_dev, u32 port_num,
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

static void hns_roce_get_fw_ver(struct ib_device *device, char *str)
{
	u64 fw_ver = to_hr_dev(device)->caps.fw_ver;
	unsigned int major, minor, sub_minor;

	major = upper_32_bits(fw_ver);
	minor = high_16_bits(lower_32_bits(fw_ver));
	sub_minor = low_16_bits(fw_ver);

	snprintf(str, IB_FW_VERSION_NAME_MAX, "%u.%u.%04u", major, minor,
		 sub_minor);
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

	.get_dev_fw_str = hns_roce_get_fw_ver,
	.add_gid = hns_roce_add_gid,
	.alloc_pd = hns_roce_alloc_pd,
	.alloc_ucontext = hns_roce_alloc_ucontext,
	.create_ah = hns_roce_create_ah,
	.create_user_ah = hns_roce_create_ah,
	.create_cq = hns_roce_create_cq,
	.create_qp = hns_roce_create_qp,
	.dealloc_pd = hns_roce_dealloc_pd,
	.dealloc_ucontext = hns_roce_dealloc_ucontext,
	.del_gid = hns_roce_del_gid,
	.dereg_mr = hns_roce_dereg_mr,
	.destroy_ah = hns_roce_destroy_ah,
	.destroy_cq = hns_roce_destroy_cq,
	.disassociate_ucontext = hns_roce_disassociate_ucontext,
	.get_dma_mr = hns_roce_get_dma_mr,
	.get_link_layer = hns_roce_get_link_layer,
	.get_port_immutable = hns_roce_port_immutable,
	.mmap = hns_roce_mmap,
	.mmap_free = hns_roce_free_mmap,
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
	INIT_RDMA_OBJ_SIZE(ib_qp, hns_roce_qp, ibqp),
	INIT_RDMA_OBJ_SIZE(ib_ucontext, hns_roce_ucontext, ibucontext),
};

static const struct ib_device_ops hns_roce_dev_mr_ops = {
	.rereg_user_mr = hns_roce_rereg_user_mr,
};

static const struct ib_device_ops hns_roce_dev_mw_ops = {
	.alloc_mw = hns_roce_alloc_mw,
	.dealloc_mw = hns_roce_dealloc_mw,

	INIT_RDMA_OBJ_SIZE(ib_mw, hns_roce_mw, ibmw),
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

static const struct ib_device_ops hns_roce_dev_xrcd_ops = {
	.alloc_xrcd = hns_roce_alloc_xrcd,
	.dealloc_xrcd = hns_roce_dealloc_xrcd,

	INIT_RDMA_OBJ_SIZE(ib_xrcd, hns_roce_xrcd, ibxrcd),
};

static const struct ib_device_ops hns_roce_dev_restrack_ops = {
	.fill_res_cq_entry = hns_roce_fill_res_cq_entry,
	.fill_res_cq_entry_raw = hns_roce_fill_res_cq_entry_raw,
	.fill_res_qp_entry = hns_roce_fill_res_qp_entry,
	.fill_res_qp_entry_raw = hns_roce_fill_res_qp_entry_raw,
	.fill_res_mr_entry = hns_roce_fill_res_mr_entry,
	.fill_res_mr_entry_raw = hns_roce_fill_res_mr_entry_raw,
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

	ib_dev->node_type = RDMA_NODE_IB_CA;
	ib_dev->dev.parent = dev;

	ib_dev->phys_port_cnt = hr_dev->caps.num_ports;
	ib_dev->local_dma_lkey = hr_dev->caps.reserved_lkey;
	ib_dev->num_comp_vectors = hr_dev->caps.num_comp_vectors;

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_REREG_MR)
		ib_set_device_ops(ib_dev, &hns_roce_dev_mr_ops);

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_MW)
		ib_set_device_ops(ib_dev, &hns_roce_dev_mw_ops);

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_FRMR)
		ib_set_device_ops(ib_dev, &hns_roce_dev_frmr_ops);

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SRQ) {
		ib_set_device_ops(ib_dev, &hns_roce_dev_srq_ops);
		ib_set_device_ops(ib_dev, hr_dev->hw->hns_roce_dev_srq_ops);
	}

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_XRC)
		ib_set_device_ops(ib_dev, &hns_roce_dev_xrcd_ops);

	ib_set_device_ops(ib_dev, hr_dev->hw->hns_roce_dev_ops);
	ib_set_device_ops(ib_dev, &hns_roce_dev_ops);
	ib_set_device_ops(ib_dev, &hns_roce_dev_restrack_ops);
	for (i = 0; i < hr_dev->caps.num_ports; i++) {
		if (!hr_dev->iboe.netdevs[i])
			continue;

		ret = ib_device_set_netdev(ib_dev, hr_dev->iboe.netdevs[i],
					   i + 1);
		if (ret)
			return ret;
	}
	dma_set_max_seg_size(dev, UINT_MAX);
	ret = ib_register_device(ib_dev, "hns_%d", dev);
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
	struct device *dev = hr_dev->dev;
	int ret;

	ret = hns_roce_init_hem_table(hr_dev, &hr_dev->mr_table.mtpt_table,
				      HEM_TYPE_MTPT, hr_dev->caps.mtpt_entry_sz,
				      hr_dev->caps.num_mtpts);
	if (ret) {
		dev_err(dev, "failed to init MTPT context memory, aborting.\n");
		return ret;
	}

	ret = hns_roce_init_hem_table(hr_dev, &hr_dev->qp_table.qp_table,
				      HEM_TYPE_QPC, hr_dev->caps.qpc_sz,
				      hr_dev->caps.num_qps);
	if (ret) {
		dev_err(dev, "failed to init QP context memory, aborting.\n");
		goto err_unmap_dmpt;
	}

	ret = hns_roce_init_hem_table(hr_dev, &hr_dev->qp_table.irrl_table,
				      HEM_TYPE_IRRL,
				      hr_dev->caps.irrl_entry_sz *
				      hr_dev->caps.max_qp_init_rdma,
				      hr_dev->caps.num_qps);
	if (ret) {
		dev_err(dev, "failed to init irrl_table memory, aborting.\n");
		goto err_unmap_qp;
	}

	if (hr_dev->caps.trrl_entry_sz) {
		ret = hns_roce_init_hem_table(hr_dev,
					      &hr_dev->qp_table.trrl_table,
					      HEM_TYPE_TRRL,
					      hr_dev->caps.trrl_entry_sz *
					      hr_dev->caps.max_qp_dest_rdma,
					      hr_dev->caps.num_qps);
		if (ret) {
			dev_err(dev,
				"failed to init trrl_table memory, aborting.\n");
			goto err_unmap_irrl;
		}
	}

	ret = hns_roce_init_hem_table(hr_dev, &hr_dev->cq_table.table,
				      HEM_TYPE_CQC, hr_dev->caps.cqc_entry_sz,
				      hr_dev->caps.num_cqs);
	if (ret) {
		dev_err(dev, "failed to init CQ context memory, aborting.\n");
		goto err_unmap_trrl;
	}

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SRQ) {
		ret = hns_roce_init_hem_table(hr_dev, &hr_dev->srq_table.table,
					      HEM_TYPE_SRQC,
					      hr_dev->caps.srqc_entry_sz,
					      hr_dev->caps.num_srqs);
		if (ret) {
			dev_err(dev,
				"failed to init SRQ context memory, aborting.\n");
			goto err_unmap_cq;
		}
	}

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_QP_FLOW_CTRL) {
		ret = hns_roce_init_hem_table(hr_dev,
					      &hr_dev->qp_table.sccc_table,
					      HEM_TYPE_SCCC,
					      hr_dev->caps.sccc_sz,
					      hr_dev->caps.num_qps);
		if (ret) {
			dev_err(dev,
				"failed to init SCC context memory, aborting.\n");
			goto err_unmap_srq;
		}
	}

	if (hr_dev->caps.qpc_timer_entry_sz) {
		ret = hns_roce_init_hem_table(hr_dev, &hr_dev->qpc_timer_table,
					      HEM_TYPE_QPC_TIMER,
					      hr_dev->caps.qpc_timer_entry_sz,
					      hr_dev->caps.qpc_timer_bt_num);
		if (ret) {
			dev_err(dev,
				"failed to init QPC timer memory, aborting.\n");
			goto err_unmap_ctx;
		}
	}

	if (hr_dev->caps.cqc_timer_entry_sz) {
		ret = hns_roce_init_hem_table(hr_dev, &hr_dev->cqc_timer_table,
					      HEM_TYPE_CQC_TIMER,
					      hr_dev->caps.cqc_timer_entry_sz,
					      hr_dev->caps.cqc_timer_bt_num);
		if (ret) {
			dev_err(dev,
				"failed to init CQC timer memory, aborting.\n");
			goto err_unmap_qpc_timer;
		}
	}

	if (hr_dev->caps.gmv_entry_sz) {
		ret = hns_roce_init_hem_table(hr_dev, &hr_dev->gmv_table,
					      HEM_TYPE_GMV,
					      hr_dev->caps.gmv_entry_sz,
					      hr_dev->caps.gmv_entry_num);
		if (ret) {
			dev_err(dev,
				"failed to init gmv table memory, ret = %d\n",
				ret);
			goto err_unmap_cqc_timer;
		}
	}

	return 0;

err_unmap_cqc_timer:
	if (hr_dev->caps.cqc_timer_entry_sz)
		hns_roce_cleanup_hem_table(hr_dev, &hr_dev->cqc_timer_table);

err_unmap_qpc_timer:
	if (hr_dev->caps.qpc_timer_entry_sz)
		hns_roce_cleanup_hem_table(hr_dev, &hr_dev->qpc_timer_table);

err_unmap_ctx:
	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_QP_FLOW_CTRL)
		hns_roce_cleanup_hem_table(hr_dev,
					   &hr_dev->qp_table.sccc_table);
err_unmap_srq:
	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SRQ)
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

	return ret;
}

/**
 * hns_roce_setup_hca - setup host channel adapter
 * @hr_dev: pointer to hns roce device
 * Return : int
 */
static int hns_roce_setup_hca(struct hns_roce_dev *hr_dev)
{
	struct device *dev = hr_dev->dev;
	int ret;

	spin_lock_init(&hr_dev->sm_lock);

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_CQ_RECORD_DB ||
	    hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_QP_RECORD_DB) {
		INIT_LIST_HEAD(&hr_dev->pgdir_list);
		mutex_init(&hr_dev->pgdir_mutex);
	}

	hns_roce_init_uar_table(hr_dev);

	ret = hns_roce_uar_alloc(hr_dev, &hr_dev->priv_uar);
	if (ret) {
		dev_err(dev, "failed to allocate priv_uar.\n");
		goto err_uar_table_free;
	}

	ret = hns_roce_init_qp_table(hr_dev);
	if (ret) {
		dev_err(dev, "failed to init qp_table.\n");
		goto err_uar_table_free;
	}

	hns_roce_init_pd_table(hr_dev);

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_XRC)
		hns_roce_init_xrcd_table(hr_dev);

	hns_roce_init_mr_table(hr_dev);

	hns_roce_init_cq_table(hr_dev);

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_SRQ)
		hns_roce_init_srq_table(hr_dev);

	return 0;

err_uar_table_free:
	ida_destroy(&hr_dev->uar_ida.ida);
	return ret;
}

static void check_and_get_armed_cq(struct list_head *cq_list, struct ib_cq *cq)
{
	struct hns_roce_cq *hr_cq = to_hr_cq(cq);
	unsigned long flags;

	spin_lock_irqsave(&hr_cq->lock, flags);
	if (cq->comp_handler) {
		if (!hr_cq->is_armed) {
			hr_cq->is_armed = 1;
			list_add_tail(&hr_cq->node, cq_list);
		}
	}
	spin_unlock_irqrestore(&hr_cq->lock, flags);
}

void hns_roce_handle_device_err(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_qp *hr_qp;
	struct hns_roce_cq *hr_cq;
	struct list_head cq_list;
	unsigned long flags_qp;
	unsigned long flags;

	INIT_LIST_HEAD(&cq_list);

	spin_lock_irqsave(&hr_dev->qp_list_lock, flags);
	list_for_each_entry(hr_qp, &hr_dev->qp_list, node) {
		spin_lock_irqsave(&hr_qp->sq.lock, flags_qp);
		if (hr_qp->sq.tail != hr_qp->sq.head)
			check_and_get_armed_cq(&cq_list, hr_qp->ibqp.send_cq);
		spin_unlock_irqrestore(&hr_qp->sq.lock, flags_qp);

		spin_lock_irqsave(&hr_qp->rq.lock, flags_qp);
		if ((!hr_qp->ibqp.srq) && (hr_qp->rq.tail != hr_qp->rq.head))
			check_and_get_armed_cq(&cq_list, hr_qp->ibqp.recv_cq);
		spin_unlock_irqrestore(&hr_qp->rq.lock, flags_qp);
	}

	list_for_each_entry(hr_cq, &cq_list, node)
		hns_roce_cq_completion(hr_dev, hr_cq->cqn);

	spin_unlock_irqrestore(&hr_dev->qp_list_lock, flags);
}

int hns_roce_init(struct hns_roce_dev *hr_dev)
{
	struct device *dev = hr_dev->dev;
	int ret;

	hr_dev->is_reset = false;

	if (hr_dev->hw->cmq_init) {
		ret = hr_dev->hw->cmq_init(hr_dev);
		if (ret) {
			dev_err(dev, "init RoCE Command Queue failed!\n");
			return ret;
		}
	}

	ret = hr_dev->hw->hw_profile(hr_dev);
	if (ret) {
		dev_err(dev, "get RoCE engine profile failed!\n");
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
		if (ret)
			dev_warn(dev,
				 "Cmd event  mode failed, set back to poll!\n");
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

	INIT_LIST_HEAD(&hr_dev->qp_list);
	spin_lock_init(&hr_dev->qp_list_lock);
	INIT_LIST_HEAD(&hr_dev->dip_list);
	spin_lock_init(&hr_dev->dip_list_lock);

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
}

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Wei Hu <xavier.huwei@huawei.com>");
MODULE_AUTHOR("Nenglong Zhao <zhaonenglong@hisilicon.com>");
MODULE_AUTHOR("Lijun Ou <oulijun@huawei.com>");
MODULE_DESCRIPTION("HNS RoCE Driver");
