/*
 * Copyright (c) 2005 Ammasso, Inc. All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
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
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/if_arp.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>

#include <rdma/ib_smi.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>
#include "c2.h"
#include "c2_provider.h"
#include "c2_user.h"

static int c2_query_device(struct ib_device *ibdev,
			   struct ib_device_attr *props)
{
	struct c2_dev *c2dev = to_c2dev(ibdev);

	pr_debug("%s:%u\n", __func__, __LINE__);

	*props = c2dev->props;
	return 0;
}

static int c2_query_port(struct ib_device *ibdev,
			 u8 port, struct ib_port_attr *props)
{
	pr_debug("%s:%u\n", __func__, __LINE__);

	props->max_mtu = IB_MTU_4096;
	props->lid = 0;
	props->lmc = 0;
	props->sm_lid = 0;
	props->sm_sl = 0;
	props->state = IB_PORT_ACTIVE;
	props->phys_state = 0;
	props->port_cap_flags =
	    IB_PORT_CM_SUP |
	    IB_PORT_REINIT_SUP |
	    IB_PORT_VENDOR_CLASS_SUP | IB_PORT_BOOT_MGMT_SUP;
	props->gid_tbl_len = 1;
	props->pkey_tbl_len = 1;
	props->qkey_viol_cntr = 0;
	props->active_width = 1;
	props->active_speed = 1;

	return 0;
}

static int c2_query_pkey(struct ib_device *ibdev,
			 u8 port, u16 index, u16 * pkey)
{
	pr_debug("%s:%u\n", __func__, __LINE__);
	*pkey = 0;
	return 0;
}

static int c2_query_gid(struct ib_device *ibdev, u8 port,
			int index, union ib_gid *gid)
{
	struct c2_dev *c2dev = to_c2dev(ibdev);

	pr_debug("%s:%u\n", __func__, __LINE__);
	memset(&(gid->raw[0]), 0, sizeof(gid->raw));
	memcpy(&(gid->raw[0]), c2dev->pseudo_netdev->dev_addr, 6);

	return 0;
}

/* Allocate the user context data structure. This keeps track
 * of all objects associated with a particular user-mode client.
 */
static struct ib_ucontext *c2_alloc_ucontext(struct ib_device *ibdev,
					     struct ib_udata *udata)
{
	struct c2_ucontext *context;

	pr_debug("%s:%u\n", __func__, __LINE__);
	context = kmalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return ERR_PTR(-ENOMEM);

	return &context->ibucontext;
}

static int c2_dealloc_ucontext(struct ib_ucontext *context)
{
	pr_debug("%s:%u\n", __func__, __LINE__);
	kfree(context);
	return 0;
}

static int c2_mmap_uar(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	pr_debug("%s:%u\n", __func__, __LINE__);
	return -ENOSYS;
}

static struct ib_pd *c2_alloc_pd(struct ib_device *ibdev,
				 struct ib_ucontext *context,
				 struct ib_udata *udata)
{
	struct c2_pd *pd;
	int err;

	pr_debug("%s:%u\n", __func__, __LINE__);

	pd = kmalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	err = c2_pd_alloc(to_c2dev(ibdev), !context, pd);
	if (err) {
		kfree(pd);
		return ERR_PTR(err);
	}

	if (context) {
		if (ib_copy_to_udata(udata, &pd->pd_id, sizeof(__u32))) {
			c2_pd_free(to_c2dev(ibdev), pd);
			kfree(pd);
			return ERR_PTR(-EFAULT);
		}
	}

	return &pd->ibpd;
}

static int c2_dealloc_pd(struct ib_pd *pd)
{
	pr_debug("%s:%u\n", __func__, __LINE__);
	c2_pd_free(to_c2dev(pd->device), to_c2pd(pd));
	kfree(pd);

	return 0;
}

static struct ib_ah *c2_ah_create(struct ib_pd *pd, struct ib_ah_attr *ah_attr)
{
	pr_debug("%s:%u\n", __func__, __LINE__);
	return ERR_PTR(-ENOSYS);
}

static int c2_ah_destroy(struct ib_ah *ah)
{
	pr_debug("%s:%u\n", __func__, __LINE__);
	return -ENOSYS;
}

static void c2_add_ref(struct ib_qp *ibqp)
{
	struct c2_qp *qp;
	BUG_ON(!ibqp);
	qp = to_c2qp(ibqp);
	atomic_inc(&qp->refcount);
}

static void c2_rem_ref(struct ib_qp *ibqp)
{
	struct c2_qp *qp;
	BUG_ON(!ibqp);
	qp = to_c2qp(ibqp);
	if (atomic_dec_and_test(&qp->refcount))
		wake_up(&qp->wait);
}

struct ib_qp *c2_get_qp(struct ib_device *device, int qpn)
{
	struct c2_dev* c2dev = to_c2dev(device);
	struct c2_qp *qp;

	qp = c2_find_qpn(c2dev, qpn);
	pr_debug("%s Returning QP=%p for QPN=%d, device=%p, refcount=%d\n",
		__func__, qp, qpn, device,
		(qp?atomic_read(&qp->refcount):0));

	return (qp?&qp->ibqp:NULL);
}

static struct ib_qp *c2_create_qp(struct ib_pd *pd,
				  struct ib_qp_init_attr *init_attr,
				  struct ib_udata *udata)
{
	struct c2_qp *qp;
	int err;

	pr_debug("%s:%u\n", __func__, __LINE__);

	if (init_attr->create_flags)
		return ERR_PTR(-EINVAL);

	switch (init_attr->qp_type) {
	case IB_QPT_RC:
		qp = kzalloc(sizeof(*qp), GFP_KERNEL);
		if (!qp) {
			pr_debug("%s: Unable to allocate QP\n", __func__);
			return ERR_PTR(-ENOMEM);
		}
		spin_lock_init(&qp->lock);
		if (pd->uobject) {
			/* userspace specific */
		}

		err = c2_alloc_qp(to_c2dev(pd->device),
				  to_c2pd(pd), init_attr, qp);

		if (err && pd->uobject) {
			/* userspace specific */
		}

		break;
	default:
		pr_debug("%s: Invalid QP type: %d\n", __func__,
			init_attr->qp_type);
		return ERR_PTR(-EINVAL);
	}

	if (err) {
		kfree(qp);
		return ERR_PTR(err);
	}

	return &qp->ibqp;
}

static int c2_destroy_qp(struct ib_qp *ib_qp)
{
	struct c2_qp *qp = to_c2qp(ib_qp);

	pr_debug("%s:%u qp=%p,qp->state=%d\n",
		__func__, __LINE__, ib_qp, qp->state);
	c2_free_qp(to_c2dev(ib_qp->device), qp);
	kfree(qp);
	return 0;
}

static struct ib_cq *c2_create_cq(struct ib_device *ibdev, int entries, int vector,
				  struct ib_ucontext *context,
				  struct ib_udata *udata)
{
	struct c2_cq *cq;
	int err;

	cq = kmalloc(sizeof(*cq), GFP_KERNEL);
	if (!cq) {
		pr_debug("%s: Unable to allocate CQ\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	err = c2_init_cq(to_c2dev(ibdev), entries, NULL, cq);
	if (err) {
		pr_debug("%s: error initializing CQ\n", __func__);
		kfree(cq);
		return ERR_PTR(err);
	}

	return &cq->ibcq;
}

static int c2_destroy_cq(struct ib_cq *ib_cq)
{
	struct c2_cq *cq = to_c2cq(ib_cq);

	pr_debug("%s:%u\n", __func__, __LINE__);

	c2_free_cq(to_c2dev(ib_cq->device), cq);
	kfree(cq);

	return 0;
}

static inline u32 c2_convert_access(int acc)
{
	return (acc & IB_ACCESS_REMOTE_WRITE ? C2_ACF_REMOTE_WRITE : 0) |
	    (acc & IB_ACCESS_REMOTE_READ ? C2_ACF_REMOTE_READ : 0) |
	    (acc & IB_ACCESS_LOCAL_WRITE ? C2_ACF_LOCAL_WRITE : 0) |
	    C2_ACF_LOCAL_READ | C2_ACF_WINDOW_BIND;
}

static struct ib_mr *c2_reg_phys_mr(struct ib_pd *ib_pd,
				    struct ib_phys_buf *buffer_list,
				    int num_phys_buf, int acc, u64 * iova_start)
{
	struct c2_mr *mr;
	u64 *page_list;
	u32 total_len;
	int err, i, j, k, page_shift, pbl_depth;

	pbl_depth = 0;
	total_len = 0;

	page_shift = PAGE_SHIFT;
	/*
	 * If there is only 1 buffer we assume this could
	 * be a map of all phy mem...use a 32k page_shift.
	 */
	if (num_phys_buf == 1)
		page_shift += 3;

	for (i = 0; i < num_phys_buf; i++) {

		if (buffer_list[i].addr & ~PAGE_MASK) {
			pr_debug("Unaligned Memory Buffer: 0x%x\n",
				(unsigned int) buffer_list[i].addr);
			return ERR_PTR(-EINVAL);
		}

		if (!buffer_list[i].size) {
			pr_debug("Invalid Buffer Size\n");
			return ERR_PTR(-EINVAL);
		}

		total_len += buffer_list[i].size;
		pbl_depth += ALIGN(buffer_list[i].size,
				   (1 << page_shift)) >> page_shift;
	}

	page_list = vmalloc(sizeof(u64) * pbl_depth);
	if (!page_list) {
		pr_debug("couldn't vmalloc page_list of size %zd\n",
			(sizeof(u64) * pbl_depth));
		return ERR_PTR(-ENOMEM);
	}

	for (i = 0, j = 0; i < num_phys_buf; i++) {

		int naddrs;

 		naddrs = ALIGN(buffer_list[i].size,
			       (1 << page_shift)) >> page_shift;
		for (k = 0; k < naddrs; k++)
			page_list[j++] = (buffer_list[i].addr +
						     (k << page_shift));
	}

	mr = kmalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr) {
		vfree(page_list);
		return ERR_PTR(-ENOMEM);
	}

	mr->pd = to_c2pd(ib_pd);
	mr->umem = NULL;
	pr_debug("%s - page shift %d, pbl_depth %d, total_len %u, "
		"*iova_start %llx, first pa %llx, last pa %llx\n",
		__func__, page_shift, pbl_depth, total_len,
		(unsigned long long) *iova_start,
	       	(unsigned long long) page_list[0],
	       	(unsigned long long) page_list[pbl_depth-1]);
  	err = c2_nsmr_register_phys_kern(to_c2dev(ib_pd->device), page_list,
 					 (1 << page_shift), pbl_depth,
					 total_len, 0, iova_start,
					 c2_convert_access(acc), mr);
	vfree(page_list);
	if (err) {
		kfree(mr);
		return ERR_PTR(err);
	}

	return &mr->ibmr;
}

static struct ib_mr *c2_get_dma_mr(struct ib_pd *pd, int acc)
{
	struct ib_phys_buf bl;
	u64 kva = 0;

	pr_debug("%s:%u\n", __func__, __LINE__);

	/* AMSO1100 limit */
	bl.size = 0xffffffff;
	bl.addr = 0;
	return c2_reg_phys_mr(pd, &bl, 1, acc, &kva);
}

static struct ib_mr *c2_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				    u64 virt, int acc, struct ib_udata *udata)
{
	u64 *pages;
	u64 kva = 0;
	int shift, n, len;
	int i, j, k;
	int err = 0;
	struct ib_umem_chunk *chunk;
	struct c2_pd *c2pd = to_c2pd(pd);
	struct c2_mr *c2mr;

	pr_debug("%s:%u\n", __func__, __LINE__);

	c2mr = kmalloc(sizeof(*c2mr), GFP_KERNEL);
	if (!c2mr)
		return ERR_PTR(-ENOMEM);
	c2mr->pd = c2pd;

	c2mr->umem = ib_umem_get(pd->uobject->context, start, length, acc, 0);
	if (IS_ERR(c2mr->umem)) {
		err = PTR_ERR(c2mr->umem);
		kfree(c2mr);
		return ERR_PTR(err);
	}

	shift = ffs(c2mr->umem->page_size) - 1;

	n = 0;
	list_for_each_entry(chunk, &c2mr->umem->chunk_list, list)
		n += chunk->nents;

	pages = kmalloc(n * sizeof(u64), GFP_KERNEL);
	if (!pages) {
		err = -ENOMEM;
		goto err;
	}

	i = 0;
	list_for_each_entry(chunk, &c2mr->umem->chunk_list, list) {
		for (j = 0; j < chunk->nmap; ++j) {
			len = sg_dma_len(&chunk->page_list[j]) >> shift;
			for (k = 0; k < len; ++k) {
				pages[i++] =
					sg_dma_address(&chunk->page_list[j]) +
					(c2mr->umem->page_size * k);
			}
		}
	}

	kva = virt;
  	err = c2_nsmr_register_phys_kern(to_c2dev(pd->device),
					 pages,
					 c2mr->umem->page_size,
					 i,
					 length,
					 c2mr->umem->offset,
					 &kva,
					 c2_convert_access(acc),
					 c2mr);
	kfree(pages);
	if (err)
		goto err;
	return &c2mr->ibmr;

err:
	ib_umem_release(c2mr->umem);
	kfree(c2mr);
	return ERR_PTR(err);
}

static int c2_dereg_mr(struct ib_mr *ib_mr)
{
	struct c2_mr *mr = to_c2mr(ib_mr);
	int err;

	pr_debug("%s:%u\n", __func__, __LINE__);

	err = c2_stag_dealloc(to_c2dev(ib_mr->device), ib_mr->lkey);
	if (err)
		pr_debug("c2_stag_dealloc failed: %d\n", err);
	else {
		if (mr->umem)
			ib_umem_release(mr->umem);
		kfree(mr);
	}

	return err;
}

static ssize_t show_rev(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct c2_dev *c2dev = container_of(dev, struct c2_dev, ibdev.dev);
	pr_debug("%s:%u\n", __func__, __LINE__);
	return sprintf(buf, "%x\n", c2dev->props.hw_ver);
}

static ssize_t show_fw_ver(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct c2_dev *c2dev = container_of(dev, struct c2_dev, ibdev.dev);
	pr_debug("%s:%u\n", __func__, __LINE__);
	return sprintf(buf, "%x.%x.%x\n",
		       (int) (c2dev->props.fw_ver >> 32),
		       (int) (c2dev->props.fw_ver >> 16) & 0xffff,
		       (int) (c2dev->props.fw_ver & 0xffff));
}

static ssize_t show_hca(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	pr_debug("%s:%u\n", __func__, __LINE__);
	return sprintf(buf, "AMSO1100\n");
}

static ssize_t show_board(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	pr_debug("%s:%u\n", __func__, __LINE__);
	return sprintf(buf, "%.*s\n", 32, "AMSO1100 Board ID");
}

static DEVICE_ATTR(hw_rev, S_IRUGO, show_rev, NULL);
static DEVICE_ATTR(fw_ver, S_IRUGO, show_fw_ver, NULL);
static DEVICE_ATTR(hca_type, S_IRUGO, show_hca, NULL);
static DEVICE_ATTR(board_id, S_IRUGO, show_board, NULL);

static struct device_attribute *c2_dev_attributes[] = {
	&dev_attr_hw_rev,
	&dev_attr_fw_ver,
	&dev_attr_hca_type,
	&dev_attr_board_id
};

static int c2_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
			int attr_mask, struct ib_udata *udata)
{
	int err;

	err =
	    c2_qp_modify(to_c2dev(ibqp->device), to_c2qp(ibqp), attr,
			 attr_mask);

	return err;
}

static int c2_multicast_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	pr_debug("%s:%u\n", __func__, __LINE__);
	return -ENOSYS;
}

static int c2_multicast_detach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	pr_debug("%s:%u\n", __func__, __LINE__);
	return -ENOSYS;
}

static int c2_process_mad(struct ib_device *ibdev,
			  int mad_flags,
			  u8 port_num,
			  struct ib_wc *in_wc,
			  struct ib_grh *in_grh,
			  struct ib_mad *in_mad, struct ib_mad *out_mad)
{
	pr_debug("%s:%u\n", __func__, __LINE__);
	return -ENOSYS;
}

static int c2_connect(struct iw_cm_id *cm_id, struct iw_cm_conn_param *iw_param)
{
	pr_debug("%s:%u\n", __func__, __LINE__);

	/* Request a connection */
	return c2_llp_connect(cm_id, iw_param);
}

static int c2_accept(struct iw_cm_id *cm_id, struct iw_cm_conn_param *iw_param)
{
	pr_debug("%s:%u\n", __func__, __LINE__);

	/* Accept the new connection */
	return c2_llp_accept(cm_id, iw_param);
}

static int c2_reject(struct iw_cm_id *cm_id, const void *pdata, u8 pdata_len)
{
	int err;

	pr_debug("%s:%u\n", __func__, __LINE__);

	err = c2_llp_reject(cm_id, pdata, pdata_len);
	return err;
}

static int c2_service_create(struct iw_cm_id *cm_id, int backlog)
{
	int err;

	pr_debug("%s:%u\n", __func__, __LINE__);
	err = c2_llp_service_create(cm_id, backlog);
	pr_debug("%s:%u err=%d\n",
		__func__, __LINE__,
		err);
	return err;
}

static int c2_service_destroy(struct iw_cm_id *cm_id)
{
	int err;
	pr_debug("%s:%u\n", __func__, __LINE__);

	err = c2_llp_service_destroy(cm_id);

	return err;
}

static int c2_pseudo_up(struct net_device *netdev)
{
	struct in_device *ind;
	struct c2_dev *c2dev = netdev->ml_priv;

	ind = in_dev_get(netdev);
	if (!ind)
		return 0;

	pr_debug("adding...\n");
	for_ifa(ind) {
#ifdef DEBUG
		u8 *ip = (u8 *) & ifa->ifa_address;

		pr_debug("%s: %d.%d.%d.%d\n",
		       ifa->ifa_label, ip[0], ip[1], ip[2], ip[3]);
#endif
		c2_add_addr(c2dev, ifa->ifa_address, ifa->ifa_mask);
	}
	endfor_ifa(ind);
	in_dev_put(ind);

	return 0;
}

static int c2_pseudo_down(struct net_device *netdev)
{
	struct in_device *ind;
	struct c2_dev *c2dev = netdev->ml_priv;

	ind = in_dev_get(netdev);
	if (!ind)
		return 0;

	pr_debug("deleting...\n");
	for_ifa(ind) {
#ifdef DEBUG
		u8 *ip = (u8 *) & ifa->ifa_address;

		pr_debug("%s: %d.%d.%d.%d\n",
		       ifa->ifa_label, ip[0], ip[1], ip[2], ip[3]);
#endif
		c2_del_addr(c2dev, ifa->ifa_address, ifa->ifa_mask);
	}
	endfor_ifa(ind);
	in_dev_put(ind);

	return 0;
}

static int c2_pseudo_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int c2_pseudo_change_mtu(struct net_device *netdev, int new_mtu)
{
	if (new_mtu < ETH_ZLEN || new_mtu > ETH_JUMBO_MTU)
		return -EINVAL;

	netdev->mtu = new_mtu;

	/* TODO: Tell rnic about new rmda interface mtu */
	return 0;
}

static const struct net_device_ops c2_pseudo_netdev_ops = {
	.ndo_open 		= c2_pseudo_up,
	.ndo_stop 		= c2_pseudo_down,
	.ndo_start_xmit 	= c2_pseudo_xmit_frame,
	.ndo_change_mtu 	= c2_pseudo_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
};

static void setup(struct net_device *netdev)
{
	netdev->netdev_ops = &c2_pseudo_netdev_ops;

	netdev->watchdog_timeo = 0;
	netdev->type = ARPHRD_ETHER;
	netdev->mtu = 1500;
	netdev->hard_header_len = ETH_HLEN;
	netdev->addr_len = ETH_ALEN;
	netdev->tx_queue_len = 0;
	netdev->flags |= IFF_NOARP;
}

static struct net_device *c2_pseudo_netdev_init(struct c2_dev *c2dev)
{
	char name[IFNAMSIZ];
	struct net_device *netdev;

	/* change ethxxx to iwxxx */
	strcpy(name, "iw");
	strcat(name, &c2dev->netdev->name[3]);
	netdev = alloc_netdev(0, name, setup);
	if (!netdev) {
		printk(KERN_ERR PFX "%s -  etherdev alloc failed",
			__func__);
		return NULL;
	}

	netdev->ml_priv = c2dev;

	SET_NETDEV_DEV(netdev, &c2dev->pcidev->dev);

	memcpy_fromio(netdev->dev_addr, c2dev->kva + C2_REGS_RDMA_ENADDR, 6);

	/* Print out the MAC address */
	pr_debug("%s: MAC %pM\n", netdev->name, netdev->dev_addr);

#if 0
	/* Disable network packets */
	netif_stop_queue(netdev);
#endif
	return netdev;
}

int c2_register_device(struct c2_dev *dev)
{
	int ret = -ENOMEM;
	int i;

	/* Register pseudo network device */
	dev->pseudo_netdev = c2_pseudo_netdev_init(dev);
	if (!dev->pseudo_netdev)
		goto out;

	ret = register_netdev(dev->pseudo_netdev);
	if (ret)
		goto out_free_netdev;

	pr_debug("%s:%u\n", __func__, __LINE__);
	strlcpy(dev->ibdev.name, "amso%d", IB_DEVICE_NAME_MAX);
	dev->ibdev.owner = THIS_MODULE;
	dev->ibdev.uverbs_cmd_mask =
	    (1ull << IB_USER_VERBS_CMD_GET_CONTEXT) |
	    (1ull << IB_USER_VERBS_CMD_QUERY_DEVICE) |
	    (1ull << IB_USER_VERBS_CMD_QUERY_PORT) |
	    (1ull << IB_USER_VERBS_CMD_ALLOC_PD) |
	    (1ull << IB_USER_VERBS_CMD_DEALLOC_PD) |
	    (1ull << IB_USER_VERBS_CMD_REG_MR) |
	    (1ull << IB_USER_VERBS_CMD_DEREG_MR) |
	    (1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL) |
	    (1ull << IB_USER_VERBS_CMD_CREATE_CQ) |
	    (1ull << IB_USER_VERBS_CMD_DESTROY_CQ) |
	    (1ull << IB_USER_VERBS_CMD_REQ_NOTIFY_CQ) |
	    (1ull << IB_USER_VERBS_CMD_CREATE_QP) |
	    (1ull << IB_USER_VERBS_CMD_MODIFY_QP) |
	    (1ull << IB_USER_VERBS_CMD_POLL_CQ) |
	    (1ull << IB_USER_VERBS_CMD_DESTROY_QP) |
	    (1ull << IB_USER_VERBS_CMD_POST_SEND) |
	    (1ull << IB_USER_VERBS_CMD_POST_RECV);

	dev->ibdev.node_type = RDMA_NODE_RNIC;
	memset(&dev->ibdev.node_guid, 0, sizeof(dev->ibdev.node_guid));
	memcpy(&dev->ibdev.node_guid, dev->pseudo_netdev->dev_addr, 6);
	dev->ibdev.phys_port_cnt = 1;
	dev->ibdev.num_comp_vectors = 1;
	dev->ibdev.dma_device = &dev->pcidev->dev;
	dev->ibdev.query_device = c2_query_device;
	dev->ibdev.query_port = c2_query_port;
	dev->ibdev.query_pkey = c2_query_pkey;
	dev->ibdev.query_gid = c2_query_gid;
	dev->ibdev.alloc_ucontext = c2_alloc_ucontext;
	dev->ibdev.dealloc_ucontext = c2_dealloc_ucontext;
	dev->ibdev.mmap = c2_mmap_uar;
	dev->ibdev.alloc_pd = c2_alloc_pd;
	dev->ibdev.dealloc_pd = c2_dealloc_pd;
	dev->ibdev.create_ah = c2_ah_create;
	dev->ibdev.destroy_ah = c2_ah_destroy;
	dev->ibdev.create_qp = c2_create_qp;
	dev->ibdev.modify_qp = c2_modify_qp;
	dev->ibdev.destroy_qp = c2_destroy_qp;
	dev->ibdev.create_cq = c2_create_cq;
	dev->ibdev.destroy_cq = c2_destroy_cq;
	dev->ibdev.poll_cq = c2_poll_cq;
	dev->ibdev.get_dma_mr = c2_get_dma_mr;
	dev->ibdev.reg_phys_mr = c2_reg_phys_mr;
	dev->ibdev.reg_user_mr = c2_reg_user_mr;
	dev->ibdev.dereg_mr = c2_dereg_mr;

	dev->ibdev.alloc_fmr = NULL;
	dev->ibdev.unmap_fmr = NULL;
	dev->ibdev.dealloc_fmr = NULL;
	dev->ibdev.map_phys_fmr = NULL;

	dev->ibdev.attach_mcast = c2_multicast_attach;
	dev->ibdev.detach_mcast = c2_multicast_detach;
	dev->ibdev.process_mad = c2_process_mad;

	dev->ibdev.req_notify_cq = c2_arm_cq;
	dev->ibdev.post_send = c2_post_send;
	dev->ibdev.post_recv = c2_post_receive;

	dev->ibdev.iwcm = kmalloc(sizeof(*dev->ibdev.iwcm), GFP_KERNEL);
	if (dev->ibdev.iwcm == NULL) {
		ret = -ENOMEM;
		goto out_unregister_netdev;
	}
	dev->ibdev.iwcm->add_ref = c2_add_ref;
	dev->ibdev.iwcm->rem_ref = c2_rem_ref;
	dev->ibdev.iwcm->get_qp = c2_get_qp;
	dev->ibdev.iwcm->connect = c2_connect;
	dev->ibdev.iwcm->accept = c2_accept;
	dev->ibdev.iwcm->reject = c2_reject;
	dev->ibdev.iwcm->create_listen = c2_service_create;
	dev->ibdev.iwcm->destroy_listen = c2_service_destroy;

	ret = ib_register_device(&dev->ibdev, NULL);
	if (ret)
		goto out_free_iwcm;

	for (i = 0; i < ARRAY_SIZE(c2_dev_attributes); ++i) {
		ret = device_create_file(&dev->ibdev.dev,
					       c2_dev_attributes[i]);
		if (ret)
			goto out_unregister_ibdev;
	}
	goto out;

out_unregister_ibdev:
	ib_unregister_device(&dev->ibdev);
out_free_iwcm:
	kfree(dev->ibdev.iwcm);
out_unregister_netdev:
	unregister_netdev(dev->pseudo_netdev);
out_free_netdev:
	free_netdev(dev->pseudo_netdev);
out:
	pr_debug("%s:%u ret=%d\n", __func__, __LINE__, ret);
	return ret;
}

void c2_unregister_device(struct c2_dev *dev)
{
	pr_debug("%s:%u\n", __func__, __LINE__);
	unregister_netdev(dev->pseudo_netdev);
	free_netdev(dev->pseudo_netdev);
	ib_unregister_device(&dev->ibdev);
}
