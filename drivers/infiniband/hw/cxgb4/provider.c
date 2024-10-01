/*
 * Copyright (c) 2009-2010 Chelsio, Inc. All rights reserved.
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
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/rtnetlink.h>
#include <linux/inetdevice.h>
#include <net/addrconf.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/byteorder.h>

#include <rdma/iw_cm.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>

#include "iw_cxgb4.h"

static int fastreg_support = 1;
module_param(fastreg_support, int, 0644);
MODULE_PARM_DESC(fastreg_support, "Advertise fastreg support (default=1)");

static void c4iw_dealloc_ucontext(struct ib_ucontext *context)
{
	struct c4iw_ucontext *ucontext = to_c4iw_ucontext(context);
	struct c4iw_dev *rhp;
	struct c4iw_mm_entry *mm, *tmp;

	pr_debug("context %p\n", context);
	rhp = to_c4iw_dev(ucontext->ibucontext.device);

	list_for_each_entry_safe(mm, tmp, &ucontext->mmaps, entry)
		kfree(mm);
	c4iw_release_dev_ucontext(&rhp->rdev, &ucontext->uctx);
}

static int c4iw_alloc_ucontext(struct ib_ucontext *ucontext,
			       struct ib_udata *udata)
{
	struct ib_device *ibdev = ucontext->device;
	struct c4iw_ucontext *context = to_c4iw_ucontext(ucontext);
	struct c4iw_dev *rhp = to_c4iw_dev(ibdev);
	struct c4iw_alloc_ucontext_resp uresp;
	int ret = 0;
	struct c4iw_mm_entry *mm = NULL;

	pr_debug("ibdev %p\n", ibdev);
	c4iw_init_dev_ucontext(&rhp->rdev, &context->uctx);
	INIT_LIST_HEAD(&context->mmaps);
	spin_lock_init(&context->mmap_lock);

	if (udata->outlen < sizeof(uresp) - sizeof(uresp.reserved)) {
		pr_err_once("Warning - downlevel libcxgb4 (non-fatal), device status page disabled\n");
		rhp->rdev.flags |= T4_STATUS_PAGE_DISABLED;
	} else {
		mm = kmalloc(sizeof(*mm), GFP_KERNEL);
		if (!mm) {
			ret = -ENOMEM;
			goto err;
		}

		uresp.status_page_size = PAGE_SIZE;

		spin_lock(&context->mmap_lock);
		uresp.status_page_key = context->key;
		context->key += PAGE_SIZE;
		spin_unlock(&context->mmap_lock);

		ret = ib_copy_to_udata(udata, &uresp,
				       sizeof(uresp) - sizeof(uresp.reserved));
		if (ret)
			goto err_mm;

		mm->key = uresp.status_page_key;
		mm->addr = virt_to_phys(rhp->rdev.status_page);
		mm->len = PAGE_SIZE;
		mm->vaddr = NULL;
		mm->dma_addr = 0;
		insert_flag_to_mmap(&rhp->rdev, mm, mm->addr);
		insert_mmap(context, mm);
	}
	return 0;
err_mm:
	kfree(mm);
err:
	return ret;
}

static int c4iw_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	int len = vma->vm_end - vma->vm_start;
	u32 key = vma->vm_pgoff << PAGE_SHIFT;
	struct c4iw_rdev *rdev;
	int ret = 0;
	struct c4iw_mm_entry *mm;
	struct c4iw_ucontext *ucontext;
	u64 addr;
	u8 mmap_flag;
	size_t size;
	void *vaddr;
	unsigned long vm_pgoff;
	dma_addr_t dma_addr;

	pr_debug("pgoff 0x%lx key 0x%x len %d\n", vma->vm_pgoff,
		 key, len);

	if (vma->vm_start & (PAGE_SIZE-1))
		return -EINVAL;

	rdev = &(to_c4iw_dev(context->device)->rdev);
	ucontext = to_c4iw_ucontext(context);

	mm = remove_mmap(ucontext, key, len);
	if (!mm)
		return -EINVAL;
	addr = mm->addr;
	vaddr = mm->vaddr;
	dma_addr = mm->dma_addr;
	size = mm->len;
	mmap_flag = mm->mmap_flag;
	kfree(mm);

	switch (mmap_flag) {
	case CXGB4_MMAP_BAR:
		ret = io_remap_pfn_range(vma, vma->vm_start, addr >> PAGE_SHIFT,
					 len,
					 pgprot_noncached(vma->vm_page_prot));
		break;
	case CXGB4_MMAP_BAR_WC:
		ret = io_remap_pfn_range(vma, vma->vm_start,
					 addr >> PAGE_SHIFT,
					 len, t4_pgprot_wc(vma->vm_page_prot));
		break;
	case CXGB4_MMAP_CONTIG:
		ret = io_remap_pfn_range(vma, vma->vm_start,
					 addr >> PAGE_SHIFT,
					 len, vma->vm_page_prot);
		break;
	case CXGB4_MMAP_NON_CONTIG:
		vm_pgoff = vma->vm_pgoff;
		vma->vm_pgoff = 0;
		ret = dma_mmap_coherent(&rdev->lldi.pdev->dev, vma,
					vaddr, dma_addr, size);
		vma->vm_pgoff = vm_pgoff;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int c4iw_deallocate_pd(struct ib_pd *pd, struct ib_udata *udata)
{
	struct c4iw_dev *rhp;
	struct c4iw_pd *php;

	php = to_c4iw_pd(pd);
	rhp = php->rhp;
	pr_debug("ibpd %p pdid 0x%x\n", pd, php->pdid);
	c4iw_put_resource(&rhp->rdev.resource.pdid_table, php->pdid);
	mutex_lock(&rhp->rdev.stats.lock);
	rhp->rdev.stats.pd.cur--;
	mutex_unlock(&rhp->rdev.stats.lock);
	return 0;
}

static int c4iw_allocate_pd(struct ib_pd *pd, struct ib_udata *udata)
{
	struct c4iw_pd *php = to_c4iw_pd(pd);
	struct ib_device *ibdev = pd->device;
	u32 pdid;
	struct c4iw_dev *rhp;

	pr_debug("ibdev %p\n", ibdev);
	rhp = (struct c4iw_dev *) ibdev;
	pdid =  c4iw_get_resource(&rhp->rdev.resource.pdid_table);
	if (!pdid)
		return -EINVAL;

	php->pdid = pdid;
	php->rhp = rhp;
	if (udata) {
		struct c4iw_alloc_pd_resp uresp = {.pdid = php->pdid};

		if (ib_copy_to_udata(udata, &uresp, sizeof(uresp))) {
			c4iw_deallocate_pd(&php->ibpd, udata);
			return -EFAULT;
		}
	}
	mutex_lock(&rhp->rdev.stats.lock);
	rhp->rdev.stats.pd.cur++;
	if (rhp->rdev.stats.pd.cur > rhp->rdev.stats.pd.max)
		rhp->rdev.stats.pd.max = rhp->rdev.stats.pd.cur;
	mutex_unlock(&rhp->rdev.stats.lock);
	pr_debug("pdid 0x%0x ptr 0x%p\n", pdid, php);
	return 0;
}

static int c4iw_query_gid(struct ib_device *ibdev, u32 port, int index,
			  union ib_gid *gid)
{
	struct c4iw_dev *dev;

	pr_debug("ibdev %p, port %u, index %d, gid %p\n",
		 ibdev, port, index, gid);
	if (!port)
		return -EINVAL;
	dev = to_c4iw_dev(ibdev);
	memset(&(gid->raw[0]), 0, sizeof(gid->raw));
	memcpy(&(gid->raw[0]), dev->rdev.lldi.ports[port-1]->dev_addr, 6);
	return 0;
}

static int c4iw_query_device(struct ib_device *ibdev, struct ib_device_attr *props,
			     struct ib_udata *uhw)
{

	struct c4iw_dev *dev;

	pr_debug("ibdev %p\n", ibdev);

	if (uhw->inlen || uhw->outlen)
		return -EINVAL;

	dev = to_c4iw_dev(ibdev);
	addrconf_addr_eui48((u8 *)&props->sys_image_guid,
			    dev->rdev.lldi.ports[0]->dev_addr);
	props->hw_ver = CHELSIO_CHIP_RELEASE(dev->rdev.lldi.adapter_type);
	props->fw_ver = dev->rdev.lldi.fw_vers;
	props->device_cap_flags = IB_DEVICE_MEM_WINDOW;
	props->kernel_cap_flags = IBK_LOCAL_DMA_LKEY;
	if (fastreg_support)
		props->device_cap_flags |= IB_DEVICE_MEM_MGT_EXTENSIONS;
	props->page_size_cap = T4_PAGESIZE_MASK;
	props->vendor_id = (u32)dev->rdev.lldi.pdev->vendor;
	props->vendor_part_id = (u32)dev->rdev.lldi.pdev->device;
	props->max_mr_size = T4_MAX_MR_SIZE;
	props->max_qp = dev->rdev.lldi.vr->qp.size / 2;
	props->max_srq = dev->rdev.lldi.vr->srq.size;
	props->max_qp_wr = dev->rdev.hw_queue.t4_max_qp_depth;
	props->max_srq_wr = dev->rdev.hw_queue.t4_max_qp_depth;
	props->max_send_sge = min(T4_MAX_SEND_SGE, T4_MAX_WRITE_SGE);
	props->max_recv_sge = T4_MAX_RECV_SGE;
	props->max_srq_sge = T4_MAX_RECV_SGE;
	props->max_sge_rd = 1;
	props->max_res_rd_atom = dev->rdev.lldi.max_ird_adapter;
	props->max_qp_rd_atom = min(dev->rdev.lldi.max_ordird_qp,
				    c4iw_max_read_depth);
	props->max_qp_init_rd_atom = props->max_qp_rd_atom;
	props->max_cq = dev->rdev.lldi.vr->qp.size;
	props->max_cqe = dev->rdev.hw_queue.t4_max_cq_depth;
	props->max_mr = c4iw_num_stags(&dev->rdev);
	props->max_pd = T4_MAX_NUM_PD;
	props->local_ca_ack_delay = 0;
	props->max_fast_reg_page_list_len =
		t4_max_fr_depth(dev->rdev.lldi.ulptx_memwrite_dsgl && use_dsgl);

	return 0;
}

static int c4iw_query_port(struct ib_device *ibdev, u32 port,
			   struct ib_port_attr *props)
{
	int ret = 0;
	pr_debug("ibdev %p\n", ibdev);
	ret = ib_get_eth_speed(ibdev, port, &props->active_speed,
			       &props->active_width);

	props->port_cap_flags =
	    IB_PORT_CM_SUP |
	    IB_PORT_SNMP_TUNNEL_SUP |
	    IB_PORT_REINIT_SUP |
	    IB_PORT_DEVICE_MGMT_SUP |
	    IB_PORT_VENDOR_CLASS_SUP | IB_PORT_BOOT_MGMT_SUP;
	props->gid_tbl_len = 1;
	props->max_msg_sz = -1;

	return ret;
}

static ssize_t hw_rev_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct c4iw_dev *c4iw_dev =
			rdma_device_to_drv_device(dev, struct c4iw_dev, ibdev);

	pr_debug("dev 0x%p\n", dev);
	return sysfs_emit(
		buf, "%d\n",
		CHELSIO_CHIP_RELEASE(c4iw_dev->rdev.lldi.adapter_type));
}
static DEVICE_ATTR_RO(hw_rev);

static ssize_t hca_type_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct c4iw_dev *c4iw_dev =
			rdma_device_to_drv_device(dev, struct c4iw_dev, ibdev);
	struct ethtool_drvinfo info;
	struct net_device *lldev = c4iw_dev->rdev.lldi.ports[0];

	pr_debug("dev 0x%p\n", dev);
	lldev->ethtool_ops->get_drvinfo(lldev, &info);
	return sysfs_emit(buf, "%s\n", info.driver);
}
static DEVICE_ATTR_RO(hca_type);

static ssize_t board_id_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct c4iw_dev *c4iw_dev =
			rdma_device_to_drv_device(dev, struct c4iw_dev, ibdev);

	pr_debug("dev 0x%p\n", dev);
	return sysfs_emit(buf, "%x.%x\n", c4iw_dev->rdev.lldi.pdev->vendor,
			  c4iw_dev->rdev.lldi.pdev->device);
}
static DEVICE_ATTR_RO(board_id);

enum counters {
	IP4INSEGS,
	IP4OUTSEGS,
	IP4RETRANSSEGS,
	IP4OUTRSTS,
	IP6INSEGS,
	IP6OUTSEGS,
	IP6RETRANSSEGS,
	IP6OUTRSTS,
	NR_COUNTERS
};

static const struct rdma_stat_desc cxgb4_descs[] = {
	[IP4INSEGS].name = "ip4InSegs",
	[IP4OUTSEGS].name = "ip4OutSegs",
	[IP4RETRANSSEGS].name = "ip4RetransSegs",
	[IP4OUTRSTS].name = "ip4OutRsts",
	[IP6INSEGS].name = "ip6InSegs",
	[IP6OUTSEGS].name = "ip6OutSegs",
	[IP6RETRANSSEGS].name = "ip6RetransSegs",
	[IP6OUTRSTS].name = "ip6OutRsts"
};

static struct rdma_hw_stats *c4iw_alloc_device_stats(struct ib_device *ibdev)
{
	BUILD_BUG_ON(ARRAY_SIZE(cxgb4_descs) != NR_COUNTERS);

	/* FIXME: these look like port stats */
	return rdma_alloc_hw_stats_struct(cxgb4_descs, NR_COUNTERS,
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}

static int c4iw_get_mib(struct ib_device *ibdev,
			struct rdma_hw_stats *stats,
			u32 port, int index)
{
	struct tp_tcp_stats v4, v6;
	struct c4iw_dev *c4iw_dev = to_c4iw_dev(ibdev);

	cxgb4_get_tcp_stats(c4iw_dev->rdev.lldi.pdev, &v4, &v6);
	stats->value[IP4INSEGS] = v4.tcp_in_segs;
	stats->value[IP4OUTSEGS] = v4.tcp_out_segs;
	stats->value[IP4RETRANSSEGS] = v4.tcp_retrans_segs;
	stats->value[IP4OUTRSTS] = v4.tcp_out_rsts;
	stats->value[IP6INSEGS] = v6.tcp_in_segs;
	stats->value[IP6OUTSEGS] = v6.tcp_out_segs;
	stats->value[IP6RETRANSSEGS] = v6.tcp_retrans_segs;
	stats->value[IP6OUTRSTS] = v6.tcp_out_rsts;

	return stats->num_counters;
}

static struct attribute *c4iw_class_attributes[] = {
	&dev_attr_hw_rev.attr,
	&dev_attr_hca_type.attr,
	&dev_attr_board_id.attr,
	NULL
};

static const struct attribute_group c4iw_attr_group = {
	.attrs = c4iw_class_attributes,
};

static int c4iw_port_immutable(struct ib_device *ibdev, u32 port_num,
			       struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	immutable->core_cap_flags = RDMA_CORE_PORT_IWARP;

	err = ib_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->gid_tbl_len = attr.gid_tbl_len;

	return 0;
}

static void get_dev_fw_str(struct ib_device *dev, char *str)
{
	struct c4iw_dev *c4iw_dev = container_of(dev, struct c4iw_dev,
						 ibdev);
	pr_debug("dev 0x%p\n", dev);

	snprintf(str, IB_FW_VERSION_NAME_MAX, "%u.%u.%u.%u",
		 FW_HDR_FW_VER_MAJOR_G(c4iw_dev->rdev.lldi.fw_vers),
		 FW_HDR_FW_VER_MINOR_G(c4iw_dev->rdev.lldi.fw_vers),
		 FW_HDR_FW_VER_MICRO_G(c4iw_dev->rdev.lldi.fw_vers),
		 FW_HDR_FW_VER_BUILD_G(c4iw_dev->rdev.lldi.fw_vers));
}

static const struct ib_device_ops c4iw_dev_ops = {
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_CXGB4,
	.uverbs_abi_ver = C4IW_UVERBS_ABI_VERSION,

	.alloc_hw_device_stats = c4iw_alloc_device_stats,
	.alloc_mr = c4iw_alloc_mr,
	.alloc_pd = c4iw_allocate_pd,
	.alloc_ucontext = c4iw_alloc_ucontext,
	.create_cq = c4iw_create_cq,
	.create_qp = c4iw_create_qp,
	.create_srq = c4iw_create_srq,
	.dealloc_pd = c4iw_deallocate_pd,
	.dealloc_ucontext = c4iw_dealloc_ucontext,
	.dereg_mr = c4iw_dereg_mr,
	.destroy_cq = c4iw_destroy_cq,
	.destroy_qp = c4iw_destroy_qp,
	.destroy_srq = c4iw_destroy_srq,
	.device_group = &c4iw_attr_group,
	.fill_res_cq_entry = c4iw_fill_res_cq_entry,
	.fill_res_cm_id_entry = c4iw_fill_res_cm_id_entry,
	.fill_res_mr_entry = c4iw_fill_res_mr_entry,
	.get_dev_fw_str = get_dev_fw_str,
	.get_dma_mr = c4iw_get_dma_mr,
	.get_hw_stats = c4iw_get_mib,
	.get_port_immutable = c4iw_port_immutable,
	.iw_accept = c4iw_accept_cr,
	.iw_add_ref = c4iw_qp_add_ref,
	.iw_connect = c4iw_connect,
	.iw_create_listen = c4iw_create_listen,
	.iw_destroy_listen = c4iw_destroy_listen,
	.iw_get_qp = c4iw_get_qp,
	.iw_reject = c4iw_reject_cr,
	.iw_rem_ref = c4iw_qp_rem_ref,
	.map_mr_sg = c4iw_map_mr_sg,
	.mmap = c4iw_mmap,
	.modify_qp = c4iw_ib_modify_qp,
	.modify_srq = c4iw_modify_srq,
	.poll_cq = c4iw_poll_cq,
	.post_recv = c4iw_post_receive,
	.post_send = c4iw_post_send,
	.post_srq_recv = c4iw_post_srq_recv,
	.query_device = c4iw_query_device,
	.query_gid = c4iw_query_gid,
	.query_port = c4iw_query_port,
	.query_qp = c4iw_ib_query_qp,
	.reg_user_mr = c4iw_reg_user_mr,
	.req_notify_cq = c4iw_arm_cq,

	INIT_RDMA_OBJ_SIZE(ib_cq, c4iw_cq, ibcq),
	INIT_RDMA_OBJ_SIZE(ib_mw, c4iw_mw, ibmw),
	INIT_RDMA_OBJ_SIZE(ib_pd, c4iw_pd, ibpd),
	INIT_RDMA_OBJ_SIZE(ib_qp, c4iw_qp, ibqp),
	INIT_RDMA_OBJ_SIZE(ib_srq, c4iw_srq, ibsrq),
	INIT_RDMA_OBJ_SIZE(ib_ucontext, c4iw_ucontext, ibucontext),
};

static int set_netdevs(struct ib_device *ib_dev, struct c4iw_rdev *rdev)
{
	int ret;
	int i;

	for (i = 0; i < rdev->lldi.nports; i++) {
		ret = ib_device_set_netdev(ib_dev, rdev->lldi.ports[i],
					   i + 1);
		if (ret)
			return ret;
	}
	return 0;
}

void c4iw_register_device(struct work_struct *work)
{
	int ret;
	struct uld_ctx *ctx = container_of(work, struct uld_ctx, reg_work);
	struct c4iw_dev *dev = ctx->dev;

	pr_debug("c4iw_dev %p\n", dev);
	addrconf_addr_eui48((u8 *)&dev->ibdev.node_guid,
			    dev->rdev.lldi.ports[0]->dev_addr);
	dev->ibdev.local_dma_lkey = 0;
	dev->ibdev.node_type = RDMA_NODE_RNIC;
	BUILD_BUG_ON(sizeof(C4IW_NODE_DESC) > IB_DEVICE_NODE_DESC_MAX);
	memcpy(dev->ibdev.node_desc, C4IW_NODE_DESC, sizeof(C4IW_NODE_DESC));
	dev->ibdev.phys_port_cnt = dev->rdev.lldi.nports;
	dev->ibdev.num_comp_vectors =  dev->rdev.lldi.nciq;
	dev->ibdev.dev.parent = &dev->rdev.lldi.pdev->dev;

	memcpy(dev->ibdev.iw_ifname, dev->rdev.lldi.ports[0]->name,
	       sizeof(dev->ibdev.iw_ifname));

	ib_set_device_ops(&dev->ibdev, &c4iw_dev_ops);
	ret = set_netdevs(&dev->ibdev, &dev->rdev);
	if (ret)
		goto err_dealloc_ctx;
	dma_set_max_seg_size(&dev->rdev.lldi.pdev->dev, UINT_MAX);
	ret = ib_register_device(&dev->ibdev, "cxgb4_%d",
				 &dev->rdev.lldi.pdev->dev);
	if (ret)
		goto err_dealloc_ctx;
	return;

err_dealloc_ctx:
	pr_err("%s - Failed registering iwarp device: %d\n",
	       pci_name(ctx->lldi.pdev), ret);
	c4iw_dealloc(ctx);
	return;
}

void c4iw_unregister_device(struct c4iw_dev *dev)
{
	pr_debug("c4iw_dev %p\n", dev);
	ib_unregister_device(&dev->ibdev);
	return;
}
