/* cnic.c: Broadcom CNIC core network driver.
 *
 * Copyright (c) 2006-2009 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Original skeleton written by: John(Zongxi) Chen (zongxi@broadcom.com)
 * Modified and maintained by: Michael Chan <mchan@broadcom.com>
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/uio_driver.h>
#include <linux/in.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/if_vlan.h>
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
#define BCM_VLAN 1
#endif
#include <net/ip.h>
#include <net/tcp.h>
#include <net/route.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <scsi/iscsi_if.h>

#include "cnic_if.h"
#include "bnx2.h"
#include "cnic.h"
#include "cnic_defs.h"

#define DRV_MODULE_NAME		"cnic"
#define PFX DRV_MODULE_NAME	": "

static char version[] __devinitdata =
	"Broadcom NetXtreme II CNIC Driver " DRV_MODULE_NAME " v" CNIC_MODULE_VERSION " (" CNIC_MODULE_RELDATE ")\n";

MODULE_AUTHOR("Michael Chan <mchan@broadcom.com> and John(Zongxi) "
	      "Chen (zongxi@broadcom.com");
MODULE_DESCRIPTION("Broadcom NetXtreme II CNIC Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(CNIC_MODULE_VERSION);

static LIST_HEAD(cnic_dev_list);
static DEFINE_RWLOCK(cnic_dev_lock);
static DEFINE_MUTEX(cnic_lock);

static struct cnic_ulp_ops *cnic_ulp_tbl[MAX_CNIC_ULP_TYPE];

static int cnic_service_bnx2(void *, void *);
static int cnic_ctl(void *, struct cnic_ctl_info *);

static struct cnic_ops cnic_bnx2_ops = {
	.cnic_owner	= THIS_MODULE,
	.cnic_handler	= cnic_service_bnx2,
	.cnic_ctl	= cnic_ctl,
};

static void cnic_shutdown_bnx2_rx_ring(struct cnic_dev *);
static void cnic_init_bnx2_tx_ring(struct cnic_dev *);
static void cnic_init_bnx2_rx_ring(struct cnic_dev *);
static int cnic_cm_set_pg(struct cnic_sock *);

static int cnic_uio_open(struct uio_info *uinfo, struct inode *inode)
{
	struct cnic_dev *dev = uinfo->priv;
	struct cnic_local *cp = dev->cnic_priv;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (cp->uio_dev != -1)
		return -EBUSY;

	cp->uio_dev = iminor(inode);

	cnic_shutdown_bnx2_rx_ring(dev);

	cnic_init_bnx2_tx_ring(dev);
	cnic_init_bnx2_rx_ring(dev);

	return 0;
}

static int cnic_uio_close(struct uio_info *uinfo, struct inode *inode)
{
	struct cnic_dev *dev = uinfo->priv;
	struct cnic_local *cp = dev->cnic_priv;

	cp->uio_dev = -1;
	return 0;
}

static inline void cnic_hold(struct cnic_dev *dev)
{
	atomic_inc(&dev->ref_count);
}

static inline void cnic_put(struct cnic_dev *dev)
{
	atomic_dec(&dev->ref_count);
}

static inline void csk_hold(struct cnic_sock *csk)
{
	atomic_inc(&csk->ref_count);
}

static inline void csk_put(struct cnic_sock *csk)
{
	atomic_dec(&csk->ref_count);
}

static struct cnic_dev *cnic_from_netdev(struct net_device *netdev)
{
	struct cnic_dev *cdev;

	read_lock(&cnic_dev_lock);
	list_for_each_entry(cdev, &cnic_dev_list, list) {
		if (netdev == cdev->netdev) {
			cnic_hold(cdev);
			read_unlock(&cnic_dev_lock);
			return cdev;
		}
	}
	read_unlock(&cnic_dev_lock);
	return NULL;
}

static void cnic_ctx_wr(struct cnic_dev *dev, u32 cid_addr, u32 off, u32 val)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	struct drv_ctl_info info;
	struct drv_ctl_io *io = &info.data.io;

	info.cmd = DRV_CTL_CTX_WR_CMD;
	io->cid_addr = cid_addr;
	io->offset = off;
	io->data = val;
	ethdev->drv_ctl(dev->netdev, &info);
}

static void cnic_reg_wr_ind(struct cnic_dev *dev, u32 off, u32 val)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	struct drv_ctl_info info;
	struct drv_ctl_io *io = &info.data.io;

	info.cmd = DRV_CTL_IO_WR_CMD;
	io->offset = off;
	io->data = val;
	ethdev->drv_ctl(dev->netdev, &info);
}

static u32 cnic_reg_rd_ind(struct cnic_dev *dev, u32 off)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	struct drv_ctl_info info;
	struct drv_ctl_io *io = &info.data.io;

	info.cmd = DRV_CTL_IO_RD_CMD;
	io->offset = off;
	ethdev->drv_ctl(dev->netdev, &info);
	return io->data;
}

static int cnic_in_use(struct cnic_sock *csk)
{
	return test_bit(SK_F_INUSE, &csk->flags);
}

static void cnic_kwq_completion(struct cnic_dev *dev, u32 count)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	struct drv_ctl_info info;

	info.cmd = DRV_CTL_COMPLETION_CMD;
	info.data.comp.comp_count = count;
	ethdev->drv_ctl(dev->netdev, &info);
}

static int cnic_send_nlmsg(struct cnic_local *cp, u32 type,
			   struct cnic_sock *csk)
{
	struct iscsi_path path_req;
	char *buf = NULL;
	u16 len = 0;
	u32 msg_type = ISCSI_KEVENT_IF_DOWN;
	struct cnic_ulp_ops *ulp_ops;

	if (cp->uio_dev == -1)
		return -ENODEV;

	if (csk) {
		len = sizeof(path_req);
		buf = (char *) &path_req;
		memset(&path_req, 0, len);

		msg_type = ISCSI_KEVENT_PATH_REQ;
		path_req.handle = (u64) csk->l5_cid;
		if (test_bit(SK_F_IPV6, &csk->flags)) {
			memcpy(&path_req.dst.v6_addr, &csk->dst_ip[0],
			       sizeof(struct in6_addr));
			path_req.ip_addr_len = 16;
		} else {
			memcpy(&path_req.dst.v4_addr, &csk->dst_ip[0],
			       sizeof(struct in_addr));
			path_req.ip_addr_len = 4;
		}
		path_req.vlan_id = csk->vlan_id;
		path_req.pmtu = csk->mtu;
	}

	rcu_read_lock();
	ulp_ops = rcu_dereference(cp->ulp_ops[CNIC_ULP_ISCSI]);
	if (ulp_ops)
		ulp_ops->iscsi_nl_send_msg(cp->dev, msg_type, buf, len);
	rcu_read_unlock();
	return 0;
}

static int cnic_iscsi_nl_msg_recv(struct cnic_dev *dev, u32 msg_type,
				  char *buf, u16 len)
{
	int rc = -EINVAL;

	switch (msg_type) {
	case ISCSI_UEVENT_PATH_UPDATE: {
		struct cnic_local *cp;
		u32 l5_cid;
		struct cnic_sock *csk;
		struct iscsi_path *path_resp;

		if (len < sizeof(*path_resp))
			break;

		path_resp = (struct iscsi_path *) buf;
		cp = dev->cnic_priv;
		l5_cid = (u32) path_resp->handle;
		if (l5_cid >= MAX_CM_SK_TBL_SZ)
			break;

		csk = &cp->csk_tbl[l5_cid];
		csk_hold(csk);
		if (cnic_in_use(csk)) {
			memcpy(csk->ha, path_resp->mac_addr, 6);
			if (test_bit(SK_F_IPV6, &csk->flags))
				memcpy(&csk->src_ip[0], &path_resp->src.v6_addr,
				       sizeof(struct in6_addr));
			else
				memcpy(&csk->src_ip[0], &path_resp->src.v4_addr,
				       sizeof(struct in_addr));
			if (is_valid_ether_addr(csk->ha))
				cnic_cm_set_pg(csk);
		}
		csk_put(csk);
		rc = 0;
	}
	}

	return rc;
}

static int cnic_offld_prep(struct cnic_sock *csk)
{
	if (test_and_set_bit(SK_F_OFFLD_SCHED, &csk->flags))
		return 0;

	if (!test_bit(SK_F_CONNECT_START, &csk->flags)) {
		clear_bit(SK_F_OFFLD_SCHED, &csk->flags);
		return 0;
	}

	return 1;
}

static int cnic_close_prep(struct cnic_sock *csk)
{
	clear_bit(SK_F_CONNECT_START, &csk->flags);
	smp_mb__after_clear_bit();

	if (test_and_clear_bit(SK_F_OFFLD_COMPLETE, &csk->flags)) {
		while (test_and_set_bit(SK_F_OFFLD_SCHED, &csk->flags))
			msleep(1);

		return 1;
	}
	return 0;
}

static int cnic_abort_prep(struct cnic_sock *csk)
{
	clear_bit(SK_F_CONNECT_START, &csk->flags);
	smp_mb__after_clear_bit();

	while (test_and_set_bit(SK_F_OFFLD_SCHED, &csk->flags))
		msleep(1);

	if (test_and_clear_bit(SK_F_OFFLD_COMPLETE, &csk->flags)) {
		csk->state = L4_KCQE_OPCODE_VALUE_RESET_COMP;
		return 1;
	}

	return 0;
}

int cnic_register_driver(int ulp_type, struct cnic_ulp_ops *ulp_ops)
{
	struct cnic_dev *dev;

	if (ulp_type >= MAX_CNIC_ULP_TYPE) {
		printk(KERN_ERR PFX "cnic_register_driver: Bad type %d\n",
		       ulp_type);
		return -EINVAL;
	}
	mutex_lock(&cnic_lock);
	if (cnic_ulp_tbl[ulp_type]) {
		printk(KERN_ERR PFX "cnic_register_driver: Type %d has already "
				    "been registered\n", ulp_type);
		mutex_unlock(&cnic_lock);
		return -EBUSY;
	}

	read_lock(&cnic_dev_lock);
	list_for_each_entry(dev, &cnic_dev_list, list) {
		struct cnic_local *cp = dev->cnic_priv;

		clear_bit(ULP_F_INIT, &cp->ulp_flags[ulp_type]);
	}
	read_unlock(&cnic_dev_lock);

	rcu_assign_pointer(cnic_ulp_tbl[ulp_type], ulp_ops);
	mutex_unlock(&cnic_lock);

	/* Prevent race conditions with netdev_event */
	rtnl_lock();
	read_lock(&cnic_dev_lock);
	list_for_each_entry(dev, &cnic_dev_list, list) {
		struct cnic_local *cp = dev->cnic_priv;

		if (!test_and_set_bit(ULP_F_INIT, &cp->ulp_flags[ulp_type]))
			ulp_ops->cnic_init(dev);
	}
	read_unlock(&cnic_dev_lock);
	rtnl_unlock();

	return 0;
}

int cnic_unregister_driver(int ulp_type)
{
	struct cnic_dev *dev;

	if (ulp_type >= MAX_CNIC_ULP_TYPE) {
		printk(KERN_ERR PFX "cnic_unregister_driver: Bad type %d\n",
		       ulp_type);
		return -EINVAL;
	}
	mutex_lock(&cnic_lock);
	if (!cnic_ulp_tbl[ulp_type]) {
		printk(KERN_ERR PFX "cnic_unregister_driver: Type %d has not "
				    "been registered\n", ulp_type);
		goto out_unlock;
	}
	read_lock(&cnic_dev_lock);
	list_for_each_entry(dev, &cnic_dev_list, list) {
		struct cnic_local *cp = dev->cnic_priv;

		if (rcu_dereference(cp->ulp_ops[ulp_type])) {
			printk(KERN_ERR PFX "cnic_unregister_driver: Type %d "
			       "still has devices registered\n", ulp_type);
			read_unlock(&cnic_dev_lock);
			goto out_unlock;
		}
	}
	read_unlock(&cnic_dev_lock);

	rcu_assign_pointer(cnic_ulp_tbl[ulp_type], NULL);

	mutex_unlock(&cnic_lock);
	synchronize_rcu();
	return 0;

out_unlock:
	mutex_unlock(&cnic_lock);
	return -EINVAL;
}

static int cnic_start_hw(struct cnic_dev *);
static void cnic_stop_hw(struct cnic_dev *);

static int cnic_register_device(struct cnic_dev *dev, int ulp_type,
				void *ulp_ctx)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_ulp_ops *ulp_ops;

	if (ulp_type >= MAX_CNIC_ULP_TYPE) {
		printk(KERN_ERR PFX "cnic_register_device: Bad type %d\n",
		       ulp_type);
		return -EINVAL;
	}
	mutex_lock(&cnic_lock);
	if (cnic_ulp_tbl[ulp_type] == NULL) {
		printk(KERN_ERR PFX "cnic_register_device: Driver with type %d "
				    "has not been registered\n", ulp_type);
		mutex_unlock(&cnic_lock);
		return -EAGAIN;
	}
	if (rcu_dereference(cp->ulp_ops[ulp_type])) {
		printk(KERN_ERR PFX "cnic_register_device: Type %d has already "
		       "been registered to this device\n", ulp_type);
		mutex_unlock(&cnic_lock);
		return -EBUSY;
	}

	clear_bit(ULP_F_START, &cp->ulp_flags[ulp_type]);
	cp->ulp_handle[ulp_type] = ulp_ctx;
	ulp_ops = cnic_ulp_tbl[ulp_type];
	rcu_assign_pointer(cp->ulp_ops[ulp_type], ulp_ops);
	cnic_hold(dev);

	if (test_bit(CNIC_F_CNIC_UP, &dev->flags))
		if (!test_and_set_bit(ULP_F_START, &cp->ulp_flags[ulp_type]))
			ulp_ops->cnic_start(cp->ulp_handle[ulp_type]);

	mutex_unlock(&cnic_lock);

	return 0;

}
EXPORT_SYMBOL(cnic_register_driver);

static int cnic_unregister_device(struct cnic_dev *dev, int ulp_type)
{
	struct cnic_local *cp = dev->cnic_priv;

	if (ulp_type >= MAX_CNIC_ULP_TYPE) {
		printk(KERN_ERR PFX "cnic_unregister_device: Bad type %d\n",
		       ulp_type);
		return -EINVAL;
	}
	mutex_lock(&cnic_lock);
	if (rcu_dereference(cp->ulp_ops[ulp_type])) {
		rcu_assign_pointer(cp->ulp_ops[ulp_type], NULL);
		cnic_put(dev);
	} else {
		printk(KERN_ERR PFX "cnic_unregister_device: device not "
		       "registered to this ulp type %d\n", ulp_type);
		mutex_unlock(&cnic_lock);
		return -EINVAL;
	}
	mutex_unlock(&cnic_lock);

	synchronize_rcu();

	return 0;
}
EXPORT_SYMBOL(cnic_unregister_driver);

static int cnic_init_id_tbl(struct cnic_id_tbl *id_tbl, u32 size, u32 start_id)
{
	id_tbl->start = start_id;
	id_tbl->max = size;
	id_tbl->next = 0;
	spin_lock_init(&id_tbl->lock);
	id_tbl->table = kzalloc(DIV_ROUND_UP(size, 32) * 4, GFP_KERNEL);
	if (!id_tbl->table)
		return -ENOMEM;

	return 0;
}

static void cnic_free_id_tbl(struct cnic_id_tbl *id_tbl)
{
	kfree(id_tbl->table);
	id_tbl->table = NULL;
}

static int cnic_alloc_id(struct cnic_id_tbl *id_tbl, u32 id)
{
	int ret = -1;

	id -= id_tbl->start;
	if (id >= id_tbl->max)
		return ret;

	spin_lock(&id_tbl->lock);
	if (!test_bit(id, id_tbl->table)) {
		set_bit(id, id_tbl->table);
		ret = 0;
	}
	spin_unlock(&id_tbl->lock);
	return ret;
}

/* Returns -1 if not successful */
static u32 cnic_alloc_new_id(struct cnic_id_tbl *id_tbl)
{
	u32 id;

	spin_lock(&id_tbl->lock);
	id = find_next_zero_bit(id_tbl->table, id_tbl->max, id_tbl->next);
	if (id >= id_tbl->max) {
		id = -1;
		if (id_tbl->next != 0) {
			id = find_first_zero_bit(id_tbl->table, id_tbl->next);
			if (id >= id_tbl->next)
				id = -1;
		}
	}

	if (id < id_tbl->max) {
		set_bit(id, id_tbl->table);
		id_tbl->next = (id + 1) & (id_tbl->max - 1);
		id += id_tbl->start;
	}

	spin_unlock(&id_tbl->lock);

	return id;
}

static void cnic_free_id(struct cnic_id_tbl *id_tbl, u32 id)
{
	if (id == -1)
		return;

	id -= id_tbl->start;
	if (id >= id_tbl->max)
		return;

	clear_bit(id, id_tbl->table);
}

static void cnic_free_dma(struct cnic_dev *dev, struct cnic_dma *dma)
{
	int i;

	if (!dma->pg_arr)
		return;

	for (i = 0; i < dma->num_pages; i++) {
		if (dma->pg_arr[i]) {
			pci_free_consistent(dev->pcidev, BCM_PAGE_SIZE,
					    dma->pg_arr[i], dma->pg_map_arr[i]);
			dma->pg_arr[i] = NULL;
		}
	}
	if (dma->pgtbl) {
		pci_free_consistent(dev->pcidev, dma->pgtbl_size,
				    dma->pgtbl, dma->pgtbl_map);
		dma->pgtbl = NULL;
	}
	kfree(dma->pg_arr);
	dma->pg_arr = NULL;
	dma->num_pages = 0;
}

static void cnic_setup_page_tbl(struct cnic_dev *dev, struct cnic_dma *dma)
{
	int i;
	u32 *page_table = dma->pgtbl;

	for (i = 0; i < dma->num_pages; i++) {
		/* Each entry needs to be in big endian format. */
		*page_table = (u32) ((u64) dma->pg_map_arr[i] >> 32);
		page_table++;
		*page_table = (u32) dma->pg_map_arr[i];
		page_table++;
	}
}

static int cnic_alloc_dma(struct cnic_dev *dev, struct cnic_dma *dma,
			  int pages, int use_pg_tbl)
{
	int i, size;
	struct cnic_local *cp = dev->cnic_priv;

	size = pages * (sizeof(void *) + sizeof(dma_addr_t));
	dma->pg_arr = kzalloc(size, GFP_ATOMIC);
	if (dma->pg_arr == NULL)
		return -ENOMEM;

	dma->pg_map_arr = (dma_addr_t *) (dma->pg_arr + pages);
	dma->num_pages = pages;

	for (i = 0; i < pages; i++) {
		dma->pg_arr[i] = pci_alloc_consistent(dev->pcidev,
						      BCM_PAGE_SIZE,
						      &dma->pg_map_arr[i]);
		if (dma->pg_arr[i] == NULL)
			goto error;
	}
	if (!use_pg_tbl)
		return 0;

	dma->pgtbl_size = ((pages * 8) + BCM_PAGE_SIZE - 1) &
			  ~(BCM_PAGE_SIZE - 1);
	dma->pgtbl = pci_alloc_consistent(dev->pcidev, dma->pgtbl_size,
					  &dma->pgtbl_map);
	if (dma->pgtbl == NULL)
		goto error;

	cp->setup_pgtbl(dev, dma);

	return 0;

error:
	cnic_free_dma(dev, dma);
	return -ENOMEM;
}

static void cnic_free_resc(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	int i = 0;

	if (cp->cnic_uinfo) {
		cnic_send_nlmsg(cp, ISCSI_KEVENT_IF_DOWN, NULL);
		while (cp->uio_dev != -1 && i < 15) {
			msleep(100);
			i++;
		}
		uio_unregister_device(cp->cnic_uinfo);
		kfree(cp->cnic_uinfo);
		cp->cnic_uinfo = NULL;
	}

	if (cp->l2_buf) {
		pci_free_consistent(dev->pcidev, cp->l2_buf_size,
				    cp->l2_buf, cp->l2_buf_map);
		cp->l2_buf = NULL;
	}

	if (cp->l2_ring) {
		pci_free_consistent(dev->pcidev, cp->l2_ring_size,
				    cp->l2_ring, cp->l2_ring_map);
		cp->l2_ring = NULL;
	}

	for (i = 0; i < cp->ctx_blks; i++) {
		if (cp->ctx_arr[i].ctx) {
			pci_free_consistent(dev->pcidev, cp->ctx_blk_size,
					    cp->ctx_arr[i].ctx,
					    cp->ctx_arr[i].mapping);
			cp->ctx_arr[i].ctx = NULL;
		}
	}
	kfree(cp->ctx_arr);
	cp->ctx_arr = NULL;
	cp->ctx_blks = 0;

	cnic_free_dma(dev, &cp->gbl_buf_info);
	cnic_free_dma(dev, &cp->conn_buf_info);
	cnic_free_dma(dev, &cp->kwq_info);
	cnic_free_dma(dev, &cp->kcq_info);
	kfree(cp->iscsi_tbl);
	cp->iscsi_tbl = NULL;
	kfree(cp->ctx_tbl);
	cp->ctx_tbl = NULL;

	cnic_free_id_tbl(&cp->cid_tbl);
}

static int cnic_alloc_context(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;

	if (CHIP_NUM(cp) == CHIP_NUM_5709) {
		int i, k, arr_size;

		cp->ctx_blk_size = BCM_PAGE_SIZE;
		cp->cids_per_blk = BCM_PAGE_SIZE / 128;
		arr_size = BNX2_MAX_CID / cp->cids_per_blk *
			   sizeof(struct cnic_ctx);
		cp->ctx_arr = kzalloc(arr_size, GFP_KERNEL);
		if (cp->ctx_arr == NULL)
			return -ENOMEM;

		k = 0;
		for (i = 0; i < 2; i++) {
			u32 j, reg, off, lo, hi;

			if (i == 0)
				off = BNX2_PG_CTX_MAP;
			else
				off = BNX2_ISCSI_CTX_MAP;

			reg = cnic_reg_rd_ind(dev, off);
			lo = reg >> 16;
			hi = reg & 0xffff;
			for (j = lo; j < hi; j += cp->cids_per_blk, k++)
				cp->ctx_arr[k].cid = j;
		}

		cp->ctx_blks = k;
		if (cp->ctx_blks >= (BNX2_MAX_CID / cp->cids_per_blk)) {
			cp->ctx_blks = 0;
			return -ENOMEM;
		}

		for (i = 0; i < cp->ctx_blks; i++) {
			cp->ctx_arr[i].ctx =
				pci_alloc_consistent(dev->pcidev, BCM_PAGE_SIZE,
						     &cp->ctx_arr[i].mapping);
			if (cp->ctx_arr[i].ctx == NULL)
				return -ENOMEM;
		}
	}
	return 0;
}

static int cnic_alloc_bnx2_resc(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct uio_info *uinfo;
	int ret;

	ret = cnic_alloc_dma(dev, &cp->kwq_info, KWQ_PAGE_CNT, 1);
	if (ret)
		goto error;
	cp->kwq = (struct kwqe **) cp->kwq_info.pg_arr;

	ret = cnic_alloc_dma(dev, &cp->kcq_info, KCQ_PAGE_CNT, 1);
	if (ret)
		goto error;
	cp->kcq = (struct kcqe **) cp->kcq_info.pg_arr;

	ret = cnic_alloc_context(dev);
	if (ret)
		goto error;

	cp->l2_ring_size = 2 * BCM_PAGE_SIZE;
	cp->l2_ring = pci_alloc_consistent(dev->pcidev, cp->l2_ring_size,
					   &cp->l2_ring_map);
	if (!cp->l2_ring)
		goto error;

	cp->l2_buf_size = (cp->l2_rx_ring_size + 1) * cp->l2_single_buf_size;
	cp->l2_buf_size = PAGE_ALIGN(cp->l2_buf_size);
	cp->l2_buf = pci_alloc_consistent(dev->pcidev, cp->l2_buf_size,
					   &cp->l2_buf_map);
	if (!cp->l2_buf)
		goto error;

	uinfo = kzalloc(sizeof(*uinfo), GFP_ATOMIC);
	if (!uinfo)
		goto error;

	uinfo->mem[0].addr = dev->netdev->base_addr;
	uinfo->mem[0].internal_addr = dev->regview;
	uinfo->mem[0].size = dev->netdev->mem_end - dev->netdev->mem_start;
	uinfo->mem[0].memtype = UIO_MEM_PHYS;

	uinfo->mem[1].addr = (unsigned long) cp->status_blk & PAGE_MASK;
	if (cp->ethdev->drv_state & CNIC_DRV_STATE_USING_MSIX)
		uinfo->mem[1].size = BNX2_SBLK_MSIX_ALIGN_SIZE * 9;
	else
		uinfo->mem[1].size = BNX2_SBLK_MSIX_ALIGN_SIZE;
	uinfo->mem[1].memtype = UIO_MEM_LOGICAL;

	uinfo->mem[2].addr = (unsigned long) cp->l2_ring;
	uinfo->mem[2].size = cp->l2_ring_size;
	uinfo->mem[2].memtype = UIO_MEM_LOGICAL;

	uinfo->mem[3].addr = (unsigned long) cp->l2_buf;
	uinfo->mem[3].size = cp->l2_buf_size;
	uinfo->mem[3].memtype = UIO_MEM_LOGICAL;

	uinfo->name = "bnx2_cnic";
	uinfo->version = CNIC_MODULE_VERSION;
	uinfo->irq = UIO_IRQ_CUSTOM;

	uinfo->open = cnic_uio_open;
	uinfo->release = cnic_uio_close;

	uinfo->priv = dev;

	ret = uio_register_device(&dev->pcidev->dev, uinfo);
	if (ret) {
		kfree(uinfo);
		goto error;
	}

	cp->cnic_uinfo = uinfo;

	return 0;

error:
	cnic_free_resc(dev);
	return ret;
}

static inline u32 cnic_kwq_avail(struct cnic_local *cp)
{
	return cp->max_kwq_idx -
		((cp->kwq_prod_idx - cp->kwq_con_idx) & cp->max_kwq_idx);
}

static int cnic_submit_bnx2_kwqes(struct cnic_dev *dev, struct kwqe *wqes[],
				  u32 num_wqes)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct kwqe *prod_qe;
	u16 prod, sw_prod, i;

	if (!test_bit(CNIC_F_CNIC_UP, &dev->flags))
		return -EAGAIN;		/* bnx2 is down */

	spin_lock_bh(&cp->cnic_ulp_lock);
	if (num_wqes > cnic_kwq_avail(cp) &&
	    !(cp->cnic_local_flags & CNIC_LCL_FL_KWQ_INIT)) {
		spin_unlock_bh(&cp->cnic_ulp_lock);
		return -EAGAIN;
	}

	cp->cnic_local_flags &= ~CNIC_LCL_FL_KWQ_INIT;

	prod = cp->kwq_prod_idx;
	sw_prod = prod & MAX_KWQ_IDX;
	for (i = 0; i < num_wqes; i++) {
		prod_qe = &cp->kwq[KWQ_PG(sw_prod)][KWQ_IDX(sw_prod)];
		memcpy(prod_qe, wqes[i], sizeof(struct kwqe));
		prod++;
		sw_prod = prod & MAX_KWQ_IDX;
	}
	cp->kwq_prod_idx = prod;

	CNIC_WR16(dev, cp->kwq_io_addr, cp->kwq_prod_idx);

	spin_unlock_bh(&cp->cnic_ulp_lock);
	return 0;
}

static void service_kcqes(struct cnic_dev *dev, int num_cqes)
{
	struct cnic_local *cp = dev->cnic_priv;
	int i, j;

	i = 0;
	j = 1;
	while (num_cqes) {
		struct cnic_ulp_ops *ulp_ops;
		int ulp_type;
		u32 kcqe_op_flag = cp->completed_kcq[i]->kcqe_op_flag;
		u32 kcqe_layer = kcqe_op_flag & KCQE_FLAGS_LAYER_MASK;

		if (unlikely(kcqe_op_flag & KCQE_RAMROD_COMPLETION))
			cnic_kwq_completion(dev, 1);

		while (j < num_cqes) {
			u32 next_op = cp->completed_kcq[i + j]->kcqe_op_flag;

			if ((next_op & KCQE_FLAGS_LAYER_MASK) != kcqe_layer)
				break;

			if (unlikely(next_op & KCQE_RAMROD_COMPLETION))
				cnic_kwq_completion(dev, 1);
			j++;
		}

		if (kcqe_layer == KCQE_FLAGS_LAYER_MASK_L5_RDMA)
			ulp_type = CNIC_ULP_RDMA;
		else if (kcqe_layer == KCQE_FLAGS_LAYER_MASK_L5_ISCSI)
			ulp_type = CNIC_ULP_ISCSI;
		else if (kcqe_layer == KCQE_FLAGS_LAYER_MASK_L4)
			ulp_type = CNIC_ULP_L4;
		else if (kcqe_layer == KCQE_FLAGS_LAYER_MASK_L2)
			goto end;
		else {
			printk(KERN_ERR PFX "%s: Unknown type of KCQE(0x%x)\n",
			       dev->netdev->name, kcqe_op_flag);
			goto end;
		}

		rcu_read_lock();
		ulp_ops = rcu_dereference(cp->ulp_ops[ulp_type]);
		if (likely(ulp_ops)) {
			ulp_ops->indicate_kcqes(cp->ulp_handle[ulp_type],
						  cp->completed_kcq + i, j);
		}
		rcu_read_unlock();
end:
		num_cqes -= j;
		i += j;
		j = 1;
	}
	return;
}

static u16 cnic_bnx2_next_idx(u16 idx)
{
	return idx + 1;
}

static u16 cnic_bnx2_hw_idx(u16 idx)
{
	return idx;
}

static int cnic_get_kcqes(struct cnic_dev *dev, u16 hw_prod, u16 *sw_prod)
{
	struct cnic_local *cp = dev->cnic_priv;
	u16 i, ri, last;
	struct kcqe *kcqe;
	int kcqe_cnt = 0, last_cnt = 0;

	i = ri = last = *sw_prod;
	ri &= MAX_KCQ_IDX;

	while ((i != hw_prod) && (kcqe_cnt < MAX_COMPLETED_KCQE)) {
		kcqe = &cp->kcq[KCQ_PG(ri)][KCQ_IDX(ri)];
		cp->completed_kcq[kcqe_cnt++] = kcqe;
		i = cp->next_idx(i);
		ri = i & MAX_KCQ_IDX;
		if (likely(!(kcqe->kcqe_op_flag & KCQE_FLAGS_NEXT))) {
			last_cnt = kcqe_cnt;
			last = i;
		}
	}

	*sw_prod = last;
	return last_cnt;
}

static void cnic_chk_bnx2_pkt_rings(struct cnic_local *cp)
{
	u16 rx_cons = *cp->rx_cons_ptr;
	u16 tx_cons = *cp->tx_cons_ptr;

	if (cp->tx_cons != tx_cons || cp->rx_cons != rx_cons) {
		cp->tx_cons = tx_cons;
		cp->rx_cons = rx_cons;
		uio_event_notify(cp->cnic_uinfo);
	}
}

static int cnic_service_bnx2(void *data, void *status_blk)
{
	struct cnic_dev *dev = data;
	struct status_block *sblk = status_blk;
	struct cnic_local *cp = dev->cnic_priv;
	u32 status_idx = sblk->status_idx;
	u16 hw_prod, sw_prod;
	int kcqe_cnt;

	if (unlikely(!test_bit(CNIC_F_CNIC_UP, &dev->flags)))
		return status_idx;

	cp->kwq_con_idx = *cp->kwq_con_idx_ptr;

	hw_prod = sblk->status_completion_producer_index;
	sw_prod = cp->kcq_prod_idx;
	while (sw_prod != hw_prod) {
		kcqe_cnt = cnic_get_kcqes(dev, hw_prod, &sw_prod);
		if (kcqe_cnt == 0)
			goto done;

		service_kcqes(dev, kcqe_cnt);

		/* Tell compiler that status_blk fields can change. */
		barrier();
		if (status_idx != sblk->status_idx) {
			status_idx = sblk->status_idx;
			cp->kwq_con_idx = *cp->kwq_con_idx_ptr;
			hw_prod = sblk->status_completion_producer_index;
		} else
			break;
	}

done:
	CNIC_WR16(dev, cp->kcq_io_addr, sw_prod);

	cp->kcq_prod_idx = sw_prod;

	cnic_chk_bnx2_pkt_rings(cp);
	return status_idx;
}

static void cnic_service_bnx2_msix(unsigned long data)
{
	struct cnic_dev *dev = (struct cnic_dev *) data;
	struct cnic_local *cp = dev->cnic_priv;
	struct status_block_msix *status_blk = cp->bnx2_status_blk;
	u32 status_idx = status_blk->status_idx;
	u16 hw_prod, sw_prod;
	int kcqe_cnt;

	cp->kwq_con_idx = status_blk->status_cmd_consumer_index;

	hw_prod = status_blk->status_completion_producer_index;
	sw_prod = cp->kcq_prod_idx;
	while (sw_prod != hw_prod) {
		kcqe_cnt = cnic_get_kcqes(dev, hw_prod, &sw_prod);
		if (kcqe_cnt == 0)
			goto done;

		service_kcqes(dev, kcqe_cnt);

		/* Tell compiler that status_blk fields can change. */
		barrier();
		if (status_idx != status_blk->status_idx) {
			status_idx = status_blk->status_idx;
			cp->kwq_con_idx = status_blk->status_cmd_consumer_index;
			hw_prod = status_blk->status_completion_producer_index;
		} else
			break;
	}

done:
	CNIC_WR16(dev, cp->kcq_io_addr, sw_prod);
	cp->kcq_prod_idx = sw_prod;

	cnic_chk_bnx2_pkt_rings(cp);

	cp->last_status_idx = status_idx;
	CNIC_WR(dev, BNX2_PCICFG_INT_ACK_CMD, cp->int_num |
		BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID | cp->last_status_idx);
}

static irqreturn_t cnic_irq(int irq, void *dev_instance)
{
	struct cnic_dev *dev = dev_instance;
	struct cnic_local *cp = dev->cnic_priv;
	u16 prod = cp->kcq_prod_idx & MAX_KCQ_IDX;

	if (cp->ack_int)
		cp->ack_int(dev);

	prefetch(cp->status_blk);
	prefetch(&cp->kcq[KCQ_PG(prod)][KCQ_IDX(prod)]);

	if (likely(test_bit(CNIC_F_CNIC_UP, &dev->flags)))
		tasklet_schedule(&cp->cnic_irq_task);

	return IRQ_HANDLED;
}

static void cnic_ulp_stop(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	int if_type;

	rcu_read_lock();
	for (if_type = 0; if_type < MAX_CNIC_ULP_TYPE; if_type++) {
		struct cnic_ulp_ops *ulp_ops;

		ulp_ops = rcu_dereference(cp->ulp_ops[if_type]);
		if (!ulp_ops)
			continue;

		if (test_and_clear_bit(ULP_F_START, &cp->ulp_flags[if_type]))
			ulp_ops->cnic_stop(cp->ulp_handle[if_type]);
	}
	rcu_read_unlock();
}

static void cnic_ulp_start(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	int if_type;

	rcu_read_lock();
	for (if_type = 0; if_type < MAX_CNIC_ULP_TYPE; if_type++) {
		struct cnic_ulp_ops *ulp_ops;

		ulp_ops = rcu_dereference(cp->ulp_ops[if_type]);
		if (!ulp_ops || !ulp_ops->cnic_start)
			continue;

		if (!test_and_set_bit(ULP_F_START, &cp->ulp_flags[if_type]))
			ulp_ops->cnic_start(cp->ulp_handle[if_type]);
	}
	rcu_read_unlock();
}

static int cnic_ctl(void *data, struct cnic_ctl_info *info)
{
	struct cnic_dev *dev = data;

	switch (info->cmd) {
	case CNIC_CTL_STOP_CMD:
		cnic_hold(dev);
		mutex_lock(&cnic_lock);

		cnic_ulp_stop(dev);
		cnic_stop_hw(dev);

		mutex_unlock(&cnic_lock);
		cnic_put(dev);
		break;
	case CNIC_CTL_START_CMD:
		cnic_hold(dev);
		mutex_lock(&cnic_lock);

		if (!cnic_start_hw(dev))
			cnic_ulp_start(dev);

		mutex_unlock(&cnic_lock);
		cnic_put(dev);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void cnic_ulp_init(struct cnic_dev *dev)
{
	int i;
	struct cnic_local *cp = dev->cnic_priv;

	rcu_read_lock();
	for (i = 0; i < MAX_CNIC_ULP_TYPE_EXT; i++) {
		struct cnic_ulp_ops *ulp_ops;

		ulp_ops = rcu_dereference(cnic_ulp_tbl[i]);
		if (!ulp_ops || !ulp_ops->cnic_init)
			continue;

		if (!test_and_set_bit(ULP_F_INIT, &cp->ulp_flags[i]))
			ulp_ops->cnic_init(dev);

	}
	rcu_read_unlock();
}

static void cnic_ulp_exit(struct cnic_dev *dev)
{
	int i;
	struct cnic_local *cp = dev->cnic_priv;

	rcu_read_lock();
	for (i = 0; i < MAX_CNIC_ULP_TYPE_EXT; i++) {
		struct cnic_ulp_ops *ulp_ops;

		ulp_ops = rcu_dereference(cnic_ulp_tbl[i]);
		if (!ulp_ops || !ulp_ops->cnic_exit)
			continue;

		if (test_and_clear_bit(ULP_F_INIT, &cp->ulp_flags[i]))
			ulp_ops->cnic_exit(dev);

	}
	rcu_read_unlock();
}

static int cnic_cm_offload_pg(struct cnic_sock *csk)
{
	struct cnic_dev *dev = csk->dev;
	struct l4_kwq_offload_pg *l4kwqe;
	struct kwqe *wqes[1];

	l4kwqe = (struct l4_kwq_offload_pg *) &csk->kwqe1;
	memset(l4kwqe, 0, sizeof(*l4kwqe));
	wqes[0] = (struct kwqe *) l4kwqe;

	l4kwqe->op_code = L4_KWQE_OPCODE_VALUE_OFFLOAD_PG;
	l4kwqe->flags =
		L4_LAYER_CODE << L4_KWQ_OFFLOAD_PG_LAYER_CODE_SHIFT;
	l4kwqe->l2hdr_nbytes = ETH_HLEN;

	l4kwqe->da0 = csk->ha[0];
	l4kwqe->da1 = csk->ha[1];
	l4kwqe->da2 = csk->ha[2];
	l4kwqe->da3 = csk->ha[3];
	l4kwqe->da4 = csk->ha[4];
	l4kwqe->da5 = csk->ha[5];

	l4kwqe->sa0 = dev->mac_addr[0];
	l4kwqe->sa1 = dev->mac_addr[1];
	l4kwqe->sa2 = dev->mac_addr[2];
	l4kwqe->sa3 = dev->mac_addr[3];
	l4kwqe->sa4 = dev->mac_addr[4];
	l4kwqe->sa5 = dev->mac_addr[5];

	l4kwqe->etype = ETH_P_IP;
	l4kwqe->ipid_count = DEF_IPID_COUNT;
	l4kwqe->host_opaque = csk->l5_cid;

	if (csk->vlan_id) {
		l4kwqe->pg_flags |= L4_KWQ_OFFLOAD_PG_VLAN_TAGGING;
		l4kwqe->vlan_tag = csk->vlan_id;
		l4kwqe->l2hdr_nbytes += 4;
	}

	return dev->submit_kwqes(dev, wqes, 1);
}

static int cnic_cm_update_pg(struct cnic_sock *csk)
{
	struct cnic_dev *dev = csk->dev;
	struct l4_kwq_update_pg *l4kwqe;
	struct kwqe *wqes[1];

	l4kwqe = (struct l4_kwq_update_pg *) &csk->kwqe1;
	memset(l4kwqe, 0, sizeof(*l4kwqe));
	wqes[0] = (struct kwqe *) l4kwqe;

	l4kwqe->opcode = L4_KWQE_OPCODE_VALUE_UPDATE_PG;
	l4kwqe->flags =
		L4_LAYER_CODE << L4_KWQ_UPDATE_PG_LAYER_CODE_SHIFT;
	l4kwqe->pg_cid = csk->pg_cid;

	l4kwqe->da0 = csk->ha[0];
	l4kwqe->da1 = csk->ha[1];
	l4kwqe->da2 = csk->ha[2];
	l4kwqe->da3 = csk->ha[3];
	l4kwqe->da4 = csk->ha[4];
	l4kwqe->da5 = csk->ha[5];

	l4kwqe->pg_host_opaque = csk->l5_cid;
	l4kwqe->pg_valids = L4_KWQ_UPDATE_PG_VALIDS_DA;

	return dev->submit_kwqes(dev, wqes, 1);
}

static int cnic_cm_upload_pg(struct cnic_sock *csk)
{
	struct cnic_dev *dev = csk->dev;
	struct l4_kwq_upload *l4kwqe;
	struct kwqe *wqes[1];

	l4kwqe = (struct l4_kwq_upload *) &csk->kwqe1;
	memset(l4kwqe, 0, sizeof(*l4kwqe));
	wqes[0] = (struct kwqe *) l4kwqe;

	l4kwqe->opcode = L4_KWQE_OPCODE_VALUE_UPLOAD_PG;
	l4kwqe->flags =
		L4_LAYER_CODE << L4_KWQ_UPLOAD_LAYER_CODE_SHIFT;
	l4kwqe->cid = csk->pg_cid;

	return dev->submit_kwqes(dev, wqes, 1);
}

static int cnic_cm_conn_req(struct cnic_sock *csk)
{
	struct cnic_dev *dev = csk->dev;
	struct l4_kwq_connect_req1 *l4kwqe1;
	struct l4_kwq_connect_req2 *l4kwqe2;
	struct l4_kwq_connect_req3 *l4kwqe3;
	struct kwqe *wqes[3];
	u8 tcp_flags = 0;
	int num_wqes = 2;

	l4kwqe1 = (struct l4_kwq_connect_req1 *) &csk->kwqe1;
	l4kwqe2 = (struct l4_kwq_connect_req2 *) &csk->kwqe2;
	l4kwqe3 = (struct l4_kwq_connect_req3 *) &csk->kwqe3;
	memset(l4kwqe1, 0, sizeof(*l4kwqe1));
	memset(l4kwqe2, 0, sizeof(*l4kwqe2));
	memset(l4kwqe3, 0, sizeof(*l4kwqe3));

	l4kwqe3->op_code = L4_KWQE_OPCODE_VALUE_CONNECT3;
	l4kwqe3->flags =
		L4_LAYER_CODE << L4_KWQ_CONNECT_REQ3_LAYER_CODE_SHIFT;
	l4kwqe3->ka_timeout = csk->ka_timeout;
	l4kwqe3->ka_interval = csk->ka_interval;
	l4kwqe3->ka_max_probe_count = csk->ka_max_probe_count;
	l4kwqe3->tos = csk->tos;
	l4kwqe3->ttl = csk->ttl;
	l4kwqe3->snd_seq_scale = csk->snd_seq_scale;
	l4kwqe3->pmtu = csk->mtu;
	l4kwqe3->rcv_buf = csk->rcv_buf;
	l4kwqe3->snd_buf = csk->snd_buf;
	l4kwqe3->seed = csk->seed;

	wqes[0] = (struct kwqe *) l4kwqe1;
	if (test_bit(SK_F_IPV6, &csk->flags)) {
		wqes[1] = (struct kwqe *) l4kwqe2;
		wqes[2] = (struct kwqe *) l4kwqe3;
		num_wqes = 3;

		l4kwqe1->conn_flags = L4_KWQ_CONNECT_REQ1_IP_V6;
		l4kwqe2->op_code = L4_KWQE_OPCODE_VALUE_CONNECT2;
		l4kwqe2->flags =
			L4_KWQ_CONNECT_REQ2_LINKED_WITH_NEXT |
			L4_LAYER_CODE << L4_KWQ_CONNECT_REQ2_LAYER_CODE_SHIFT;
		l4kwqe2->src_ip_v6_2 = be32_to_cpu(csk->src_ip[1]);
		l4kwqe2->src_ip_v6_3 = be32_to_cpu(csk->src_ip[2]);
		l4kwqe2->src_ip_v6_4 = be32_to_cpu(csk->src_ip[3]);
		l4kwqe2->dst_ip_v6_2 = be32_to_cpu(csk->dst_ip[1]);
		l4kwqe2->dst_ip_v6_3 = be32_to_cpu(csk->dst_ip[2]);
		l4kwqe2->dst_ip_v6_4 = be32_to_cpu(csk->dst_ip[3]);
		l4kwqe3->mss = l4kwqe3->pmtu - sizeof(struct ipv6hdr) -
			       sizeof(struct tcphdr);
	} else {
		wqes[1] = (struct kwqe *) l4kwqe3;
		l4kwqe3->mss = l4kwqe3->pmtu - sizeof(struct iphdr) -
			       sizeof(struct tcphdr);
	}

	l4kwqe1->op_code = L4_KWQE_OPCODE_VALUE_CONNECT1;
	l4kwqe1->flags =
		(L4_LAYER_CODE << L4_KWQ_CONNECT_REQ1_LAYER_CODE_SHIFT) |
		 L4_KWQ_CONNECT_REQ3_LINKED_WITH_NEXT;
	l4kwqe1->cid = csk->cid;
	l4kwqe1->pg_cid = csk->pg_cid;
	l4kwqe1->src_ip = be32_to_cpu(csk->src_ip[0]);
	l4kwqe1->dst_ip = be32_to_cpu(csk->dst_ip[0]);
	l4kwqe1->src_port = be16_to_cpu(csk->src_port);
	l4kwqe1->dst_port = be16_to_cpu(csk->dst_port);
	if (csk->tcp_flags & SK_TCP_NO_DELAY_ACK)
		tcp_flags |= L4_KWQ_CONNECT_REQ1_NO_DELAY_ACK;
	if (csk->tcp_flags & SK_TCP_KEEP_ALIVE)
		tcp_flags |= L4_KWQ_CONNECT_REQ1_KEEP_ALIVE;
	if (csk->tcp_flags & SK_TCP_NAGLE)
		tcp_flags |= L4_KWQ_CONNECT_REQ1_NAGLE_ENABLE;
	if (csk->tcp_flags & SK_TCP_TIMESTAMP)
		tcp_flags |= L4_KWQ_CONNECT_REQ1_TIME_STAMP;
	if (csk->tcp_flags & SK_TCP_SACK)
		tcp_flags |= L4_KWQ_CONNECT_REQ1_SACK;
	if (csk->tcp_flags & SK_TCP_SEG_SCALING)
		tcp_flags |= L4_KWQ_CONNECT_REQ1_SEG_SCALING;

	l4kwqe1->tcp_flags = tcp_flags;

	return dev->submit_kwqes(dev, wqes, num_wqes);
}

static int cnic_cm_close_req(struct cnic_sock *csk)
{
	struct cnic_dev *dev = csk->dev;
	struct l4_kwq_close_req *l4kwqe;
	struct kwqe *wqes[1];

	l4kwqe = (struct l4_kwq_close_req *) &csk->kwqe2;
	memset(l4kwqe, 0, sizeof(*l4kwqe));
	wqes[0] = (struct kwqe *) l4kwqe;

	l4kwqe->op_code = L4_KWQE_OPCODE_VALUE_CLOSE;
	l4kwqe->flags = L4_LAYER_CODE << L4_KWQ_CLOSE_REQ_LAYER_CODE_SHIFT;
	l4kwqe->cid = csk->cid;

	return dev->submit_kwqes(dev, wqes, 1);
}

static int cnic_cm_abort_req(struct cnic_sock *csk)
{
	struct cnic_dev *dev = csk->dev;
	struct l4_kwq_reset_req *l4kwqe;
	struct kwqe *wqes[1];

	l4kwqe = (struct l4_kwq_reset_req *) &csk->kwqe2;
	memset(l4kwqe, 0, sizeof(*l4kwqe));
	wqes[0] = (struct kwqe *) l4kwqe;

	l4kwqe->op_code = L4_KWQE_OPCODE_VALUE_RESET;
	l4kwqe->flags = L4_LAYER_CODE << L4_KWQ_RESET_REQ_LAYER_CODE_SHIFT;
	l4kwqe->cid = csk->cid;

	return dev->submit_kwqes(dev, wqes, 1);
}

static int cnic_cm_create(struct cnic_dev *dev, int ulp_type, u32 cid,
			  u32 l5_cid, struct cnic_sock **csk, void *context)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_sock *csk1;

	if (l5_cid >= MAX_CM_SK_TBL_SZ)
		return -EINVAL;

	csk1 = &cp->csk_tbl[l5_cid];
	if (atomic_read(&csk1->ref_count))
		return -EAGAIN;

	if (test_and_set_bit(SK_F_INUSE, &csk1->flags))
		return -EBUSY;

	csk1->dev = dev;
	csk1->cid = cid;
	csk1->l5_cid = l5_cid;
	csk1->ulp_type = ulp_type;
	csk1->context = context;

	csk1->ka_timeout = DEF_KA_TIMEOUT;
	csk1->ka_interval = DEF_KA_INTERVAL;
	csk1->ka_max_probe_count = DEF_KA_MAX_PROBE_COUNT;
	csk1->tos = DEF_TOS;
	csk1->ttl = DEF_TTL;
	csk1->snd_seq_scale = DEF_SND_SEQ_SCALE;
	csk1->rcv_buf = DEF_RCV_BUF;
	csk1->snd_buf = DEF_SND_BUF;
	csk1->seed = DEF_SEED;

	*csk = csk1;
	return 0;
}

static void cnic_cm_cleanup(struct cnic_sock *csk)
{
	if (csk->src_port) {
		struct cnic_dev *dev = csk->dev;
		struct cnic_local *cp = dev->cnic_priv;

		cnic_free_id(&cp->csk_port_tbl, csk->src_port);
		csk->src_port = 0;
	}
}

static void cnic_close_conn(struct cnic_sock *csk)
{
	if (test_bit(SK_F_PG_OFFLD_COMPLETE, &csk->flags)) {
		cnic_cm_upload_pg(csk);
		clear_bit(SK_F_PG_OFFLD_COMPLETE, &csk->flags);
	}
	cnic_cm_cleanup(csk);
}

static int cnic_cm_destroy(struct cnic_sock *csk)
{
	if (!cnic_in_use(csk))
		return -EINVAL;

	csk_hold(csk);
	clear_bit(SK_F_INUSE, &csk->flags);
	smp_mb__after_clear_bit();
	while (atomic_read(&csk->ref_count) != 1)
		msleep(1);
	cnic_cm_cleanup(csk);

	csk->flags = 0;
	csk_put(csk);
	return 0;
}

static inline u16 cnic_get_vlan(struct net_device *dev,
				struct net_device **vlan_dev)
{
	if (dev->priv_flags & IFF_802_1Q_VLAN) {
		*vlan_dev = vlan_dev_real_dev(dev);
		return vlan_dev_vlan_id(dev);
	}
	*vlan_dev = dev;
	return 0;
}

static int cnic_get_v4_route(struct sockaddr_in *dst_addr,
			     struct dst_entry **dst)
{
#if defined(CONFIG_INET)
	struct flowi fl;
	int err;
	struct rtable *rt;

	memset(&fl, 0, sizeof(fl));
	fl.nl_u.ip4_u.daddr = dst_addr->sin_addr.s_addr;

	err = ip_route_output_key(&init_net, &rt, &fl);
	if (!err)
		*dst = &rt->u.dst;
	return err;
#else
	return -ENETUNREACH;
#endif
}

static int cnic_get_v6_route(struct sockaddr_in6 *dst_addr,
			     struct dst_entry **dst)
{
#if defined(CONFIG_IPV6) || (defined(CONFIG_IPV6_MODULE) && defined(MODULE))
	struct flowi fl;

	memset(&fl, 0, sizeof(fl));
	ipv6_addr_copy(&fl.fl6_dst, &dst_addr->sin6_addr);
	if (ipv6_addr_type(&fl.fl6_dst) & IPV6_ADDR_LINKLOCAL)
		fl.oif = dst_addr->sin6_scope_id;

	*dst = ip6_route_output(&init_net, NULL, &fl);
	if (*dst)
		return 0;
#endif

	return -ENETUNREACH;
}

static struct cnic_dev *cnic_cm_select_dev(struct sockaddr_in *dst_addr,
					   int ulp_type)
{
	struct cnic_dev *dev = NULL;
	struct dst_entry *dst;
	struct net_device *netdev = NULL;
	int err = -ENETUNREACH;

	if (dst_addr->sin_family == AF_INET)
		err = cnic_get_v4_route(dst_addr, &dst);
	else if (dst_addr->sin_family == AF_INET6) {
		struct sockaddr_in6 *dst_addr6 =
			(struct sockaddr_in6 *) dst_addr;

		err = cnic_get_v6_route(dst_addr6, &dst);
	} else
		return NULL;

	if (err)
		return NULL;

	if (!dst->dev)
		goto done;

	cnic_get_vlan(dst->dev, &netdev);

	dev = cnic_from_netdev(netdev);

done:
	dst_release(dst);
	if (dev)
		cnic_put(dev);
	return dev;
}

static int cnic_resolve_addr(struct cnic_sock *csk, struct cnic_sockaddr *saddr)
{
	struct cnic_dev *dev = csk->dev;
	struct cnic_local *cp = dev->cnic_priv;

	return cnic_send_nlmsg(cp, ISCSI_KEVENT_PATH_REQ, csk);
}

static int cnic_get_route(struct cnic_sock *csk, struct cnic_sockaddr *saddr)
{
	struct cnic_dev *dev = csk->dev;
	struct cnic_local *cp = dev->cnic_priv;
	int is_v6, err, rc = -ENETUNREACH;
	struct dst_entry *dst;
	struct net_device *realdev;
	u32 local_port;

	if (saddr->local.v6.sin6_family == AF_INET6 &&
	    saddr->remote.v6.sin6_family == AF_INET6)
		is_v6 = 1;
	else if (saddr->local.v4.sin_family == AF_INET &&
		 saddr->remote.v4.sin_family == AF_INET)
		is_v6 = 0;
	else
		return -EINVAL;

	clear_bit(SK_F_IPV6, &csk->flags);

	if (is_v6) {
#if defined(CONFIG_IPV6) || (defined(CONFIG_IPV6_MODULE) && defined(MODULE))
		set_bit(SK_F_IPV6, &csk->flags);
		err = cnic_get_v6_route(&saddr->remote.v6, &dst);
		if (err)
			return err;

		if (!dst || dst->error || !dst->dev)
			goto err_out;

		memcpy(&csk->dst_ip[0], &saddr->remote.v6.sin6_addr,
		       sizeof(struct in6_addr));
		csk->dst_port = saddr->remote.v6.sin6_port;
		local_port = saddr->local.v6.sin6_port;
#else
		return rc;
#endif

	} else {
		err = cnic_get_v4_route(&saddr->remote.v4, &dst);
		if (err)
			return err;

		if (!dst || dst->error || !dst->dev)
			goto err_out;

		csk->dst_ip[0] = saddr->remote.v4.sin_addr.s_addr;
		csk->dst_port = saddr->remote.v4.sin_port;
		local_port = saddr->local.v4.sin_port;
	}

	csk->vlan_id = cnic_get_vlan(dst->dev, &realdev);
	if (realdev != dev->netdev)
		goto err_out;

	if (local_port >= CNIC_LOCAL_PORT_MIN &&
	    local_port < CNIC_LOCAL_PORT_MAX) {
		if (cnic_alloc_id(&cp->csk_port_tbl, local_port))
			local_port = 0;
	} else
		local_port = 0;

	if (!local_port) {
		local_port = cnic_alloc_new_id(&cp->csk_port_tbl);
		if (local_port == -1) {
			rc = -ENOMEM;
			goto err_out;
		}
	}
	csk->src_port = local_port;

	csk->mtu = dst_mtu(dst);
	rc = 0;

err_out:
	dst_release(dst);
	return rc;
}

static void cnic_init_csk_state(struct cnic_sock *csk)
{
	csk->state = 0;
	clear_bit(SK_F_OFFLD_SCHED, &csk->flags);
	clear_bit(SK_F_CLOSING, &csk->flags);
}

static int cnic_cm_connect(struct cnic_sock *csk, struct cnic_sockaddr *saddr)
{
	int err = 0;

	if (!cnic_in_use(csk))
		return -EINVAL;

	if (test_and_set_bit(SK_F_CONNECT_START, &csk->flags))
		return -EINVAL;

	cnic_init_csk_state(csk);

	err = cnic_get_route(csk, saddr);
	if (err)
		goto err_out;

	err = cnic_resolve_addr(csk, saddr);
	if (!err)
		return 0;

err_out:
	clear_bit(SK_F_CONNECT_START, &csk->flags);
	return err;
}

static int cnic_cm_abort(struct cnic_sock *csk)
{
	struct cnic_local *cp = csk->dev->cnic_priv;
	u32 opcode;

	if (!cnic_in_use(csk))
		return -EINVAL;

	if (cnic_abort_prep(csk))
		return cnic_cm_abort_req(csk);

	/* Getting here means that we haven't started connect, or
	 * connect was not successful.
	 */

	csk->state = L4_KCQE_OPCODE_VALUE_RESET_COMP;
	if (test_bit(SK_F_PG_OFFLD_COMPLETE, &csk->flags))
		opcode = csk->state;
	else
		opcode = L5CM_RAMROD_CMD_ID_TERMINATE_OFFLOAD;
	cp->close_conn(csk, opcode);

	return 0;
}

static int cnic_cm_close(struct cnic_sock *csk)
{
	if (!cnic_in_use(csk))
		return -EINVAL;

	if (cnic_close_prep(csk)) {
		csk->state = L4_KCQE_OPCODE_VALUE_CLOSE_COMP;
		return cnic_cm_close_req(csk);
	}
	return 0;
}

static void cnic_cm_upcall(struct cnic_local *cp, struct cnic_sock *csk,
			   u8 opcode)
{
	struct cnic_ulp_ops *ulp_ops;
	int ulp_type = csk->ulp_type;

	rcu_read_lock();
	ulp_ops = rcu_dereference(cp->ulp_ops[ulp_type]);
	if (ulp_ops) {
		if (opcode == L4_KCQE_OPCODE_VALUE_CONNECT_COMPLETE)
			ulp_ops->cm_connect_complete(csk);
		else if (opcode == L4_KCQE_OPCODE_VALUE_CLOSE_COMP)
			ulp_ops->cm_close_complete(csk);
		else if (opcode == L4_KCQE_OPCODE_VALUE_RESET_RECEIVED)
			ulp_ops->cm_remote_abort(csk);
		else if (opcode == L4_KCQE_OPCODE_VALUE_RESET_COMP)
			ulp_ops->cm_abort_complete(csk);
		else if (opcode == L4_KCQE_OPCODE_VALUE_CLOSE_RECEIVED)
			ulp_ops->cm_remote_close(csk);
	}
	rcu_read_unlock();
}

static int cnic_cm_set_pg(struct cnic_sock *csk)
{
	if (cnic_offld_prep(csk)) {
		if (test_bit(SK_F_PG_OFFLD_COMPLETE, &csk->flags))
			cnic_cm_update_pg(csk);
		else
			cnic_cm_offload_pg(csk);
	}
	return 0;
}

static void cnic_cm_process_offld_pg(struct cnic_dev *dev, struct l4_kcq *kcqe)
{
	struct cnic_local *cp = dev->cnic_priv;
	u32 l5_cid = kcqe->pg_host_opaque;
	u8 opcode = kcqe->op_code;
	struct cnic_sock *csk = &cp->csk_tbl[l5_cid];

	csk_hold(csk);
	if (!cnic_in_use(csk))
		goto done;

	if (opcode == L4_KCQE_OPCODE_VALUE_UPDATE_PG) {
		clear_bit(SK_F_OFFLD_SCHED, &csk->flags);
		goto done;
	}
	csk->pg_cid = kcqe->pg_cid;
	set_bit(SK_F_PG_OFFLD_COMPLETE, &csk->flags);
	cnic_cm_conn_req(csk);

done:
	csk_put(csk);
}

static void cnic_cm_process_kcqe(struct cnic_dev *dev, struct kcqe *kcqe)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct l4_kcq *l4kcqe = (struct l4_kcq *) kcqe;
	u8 opcode = l4kcqe->op_code;
	u32 l5_cid;
	struct cnic_sock *csk;

	if (opcode == L4_KCQE_OPCODE_VALUE_OFFLOAD_PG ||
	    opcode == L4_KCQE_OPCODE_VALUE_UPDATE_PG) {
		cnic_cm_process_offld_pg(dev, l4kcqe);
		return;
	}

	l5_cid = l4kcqe->conn_id;
	if (opcode & 0x80)
		l5_cid = l4kcqe->cid;
	if (l5_cid >= MAX_CM_SK_TBL_SZ)
		return;

	csk = &cp->csk_tbl[l5_cid];
	csk_hold(csk);

	if (!cnic_in_use(csk)) {
		csk_put(csk);
		return;
	}

	switch (opcode) {
	case L4_KCQE_OPCODE_VALUE_CONNECT_COMPLETE:
		if (l4kcqe->status == 0)
			set_bit(SK_F_OFFLD_COMPLETE, &csk->flags);

		smp_mb__before_clear_bit();
		clear_bit(SK_F_OFFLD_SCHED, &csk->flags);
		cnic_cm_upcall(cp, csk, opcode);
		break;

	case L4_KCQE_OPCODE_VALUE_RESET_RECEIVED:
		if (test_and_clear_bit(SK_F_OFFLD_COMPLETE, &csk->flags))
			csk->state = opcode;
		/* fall through */
	case L4_KCQE_OPCODE_VALUE_CLOSE_COMP:
	case L4_KCQE_OPCODE_VALUE_RESET_COMP:
		cp->close_conn(csk, opcode);
		break;

	case L4_KCQE_OPCODE_VALUE_CLOSE_RECEIVED:
		cnic_cm_upcall(cp, csk, opcode);
		break;
	}
	csk_put(csk);
}

static void cnic_cm_indicate_kcqe(void *data, struct kcqe *kcqe[], u32 num)
{
	struct cnic_dev *dev = data;
	int i;

	for (i = 0; i < num; i++)
		cnic_cm_process_kcqe(dev, kcqe[i]);
}

static struct cnic_ulp_ops cm_ulp_ops = {
	.indicate_kcqes		= cnic_cm_indicate_kcqe,
};

static void cnic_cm_free_mem(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;

	kfree(cp->csk_tbl);
	cp->csk_tbl = NULL;
	cnic_free_id_tbl(&cp->csk_port_tbl);
}

static int cnic_cm_alloc_mem(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;

	cp->csk_tbl = kzalloc(sizeof(struct cnic_sock) * MAX_CM_SK_TBL_SZ,
			      GFP_KERNEL);
	if (!cp->csk_tbl)
		return -ENOMEM;

	if (cnic_init_id_tbl(&cp->csk_port_tbl, CNIC_LOCAL_PORT_RANGE,
			     CNIC_LOCAL_PORT_MIN)) {
		cnic_cm_free_mem(dev);
		return -ENOMEM;
	}
	return 0;
}

static int cnic_ready_to_close(struct cnic_sock *csk, u32 opcode)
{
	if ((opcode == csk->state) ||
	    (opcode == L4_KCQE_OPCODE_VALUE_RESET_RECEIVED &&
	     csk->state == L4_KCQE_OPCODE_VALUE_CLOSE_COMP)) {
		if (!test_and_set_bit(SK_F_CLOSING, &csk->flags))
			return 1;
	}
	return 0;
}

static void cnic_close_bnx2_conn(struct cnic_sock *csk, u32 opcode)
{
	struct cnic_dev *dev = csk->dev;
	struct cnic_local *cp = dev->cnic_priv;

	clear_bit(SK_F_CONNECT_START, &csk->flags);
	if (cnic_ready_to_close(csk, opcode)) {
		cnic_close_conn(csk);
		cnic_cm_upcall(cp, csk, opcode);
	}
}

static void cnic_cm_stop_bnx2_hw(struct cnic_dev *dev)
{
}

static int cnic_cm_init_bnx2_hw(struct cnic_dev *dev)
{
	u32 seed;

	get_random_bytes(&seed, 4);
	cnic_ctx_wr(dev, 45, 0, seed);
	return 0;
}

static int cnic_cm_open(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	int err;

	err = cnic_cm_alloc_mem(dev);
	if (err)
		return err;

	err = cp->start_cm(dev);

	if (err)
		goto err_out;

	dev->cm_create = cnic_cm_create;
	dev->cm_destroy = cnic_cm_destroy;
	dev->cm_connect = cnic_cm_connect;
	dev->cm_abort = cnic_cm_abort;
	dev->cm_close = cnic_cm_close;
	dev->cm_select_dev = cnic_cm_select_dev;

	cp->ulp_handle[CNIC_ULP_L4] = dev;
	rcu_assign_pointer(cp->ulp_ops[CNIC_ULP_L4], &cm_ulp_ops);
	return 0;

err_out:
	cnic_cm_free_mem(dev);
	return err;
}

static int cnic_cm_shutdown(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	int i;

	cp->stop_cm(dev);

	if (!cp->csk_tbl)
		return 0;

	for (i = 0; i < MAX_CM_SK_TBL_SZ; i++) {
		struct cnic_sock *csk = &cp->csk_tbl[i];

		clear_bit(SK_F_INUSE, &csk->flags);
		cnic_cm_cleanup(csk);
	}
	cnic_cm_free_mem(dev);

	return 0;
}

static void cnic_init_context(struct cnic_dev *dev, u32 cid)
{
	struct cnic_local *cp = dev->cnic_priv;
	u32 cid_addr;
	int i;

	if (CHIP_NUM(cp) == CHIP_NUM_5709)
		return;

	cid_addr = GET_CID_ADDR(cid);

	for (i = 0; i < CTX_SIZE; i += 4)
		cnic_ctx_wr(dev, cid_addr, i, 0);
}

static int cnic_setup_5709_context(struct cnic_dev *dev, int valid)
{
	struct cnic_local *cp = dev->cnic_priv;
	int ret = 0, i;
	u32 valid_bit = valid ? BNX2_CTX_HOST_PAGE_TBL_DATA0_VALID : 0;

	if (CHIP_NUM(cp) != CHIP_NUM_5709)
		return 0;

	for (i = 0; i < cp->ctx_blks; i++) {
		int j;
		u32 idx = cp->ctx_arr[i].cid / cp->cids_per_blk;
		u32 val;

		memset(cp->ctx_arr[i].ctx, 0, BCM_PAGE_SIZE);

		CNIC_WR(dev, BNX2_CTX_HOST_PAGE_TBL_DATA0,
			(cp->ctx_arr[i].mapping & 0xffffffff) | valid_bit);
		CNIC_WR(dev, BNX2_CTX_HOST_PAGE_TBL_DATA1,
			(u64) cp->ctx_arr[i].mapping >> 32);
		CNIC_WR(dev, BNX2_CTX_HOST_PAGE_TBL_CTRL, idx |
			BNX2_CTX_HOST_PAGE_TBL_CTRL_WRITE_REQ);
		for (j = 0; j < 10; j++) {

			val = CNIC_RD(dev, BNX2_CTX_HOST_PAGE_TBL_CTRL);
			if (!(val & BNX2_CTX_HOST_PAGE_TBL_CTRL_WRITE_REQ))
				break;
			udelay(5);
		}
		if (val & BNX2_CTX_HOST_PAGE_TBL_CTRL_WRITE_REQ) {
			ret = -EBUSY;
			break;
		}
	}
	return ret;
}

static void cnic_free_irq(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;

	if (ethdev->drv_state & CNIC_DRV_STATE_USING_MSIX) {
		cp->disable_int_sync(dev);
		tasklet_disable(&cp->cnic_irq_task);
		free_irq(ethdev->irq_arr[0].vector, dev);
	}
}

static int cnic_init_bnx2_irq(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;

	if (ethdev->drv_state & CNIC_DRV_STATE_USING_MSIX) {
		int err, i = 0;
		int sblk_num = cp->status_blk_num;
		u32 base = ((sblk_num - 1) * BNX2_HC_SB_CONFIG_SIZE) +
			   BNX2_HC_SB_CONFIG_1;

		CNIC_WR(dev, base, BNX2_HC_SB_CONFIG_1_ONE_SHOT);

		CNIC_WR(dev, base + BNX2_HC_COMP_PROD_TRIP_OFF, (2 << 16) | 8);
		CNIC_WR(dev, base + BNX2_HC_COM_TICKS_OFF, (64 << 16) | 220);
		CNIC_WR(dev, base + BNX2_HC_CMD_TICKS_OFF, (64 << 16) | 220);

		cp->bnx2_status_blk = cp->status_blk;
		cp->last_status_idx = cp->bnx2_status_blk->status_idx;
		tasklet_init(&cp->cnic_irq_task, &cnic_service_bnx2_msix,
			     (unsigned long) dev);
		err = request_irq(ethdev->irq_arr[0].vector, cnic_irq, 0,
				  "cnic", dev);
		if (err) {
			tasklet_disable(&cp->cnic_irq_task);
			return err;
		}
		while (cp->bnx2_status_blk->status_completion_producer_index &&
		       i < 10) {
			CNIC_WR(dev, BNX2_HC_COALESCE_NOW,
				1 << (11 + sblk_num));
			udelay(10);
			i++;
			barrier();
		}
		if (cp->bnx2_status_blk->status_completion_producer_index) {
			cnic_free_irq(dev);
			goto failed;
		}

	} else {
		struct status_block *sblk = cp->status_blk;
		u32 hc_cmd = CNIC_RD(dev, BNX2_HC_COMMAND);
		int i = 0;

		while (sblk->status_completion_producer_index && i < 10) {
			CNIC_WR(dev, BNX2_HC_COMMAND,
				hc_cmd | BNX2_HC_COMMAND_COAL_NOW_WO_INT);
			udelay(10);
			i++;
			barrier();
		}
		if (sblk->status_completion_producer_index)
			goto failed;

	}
	return 0;

failed:
	printk(KERN_ERR PFX "%s: " "KCQ index not resetting to 0.\n",
	       dev->netdev->name);
	return -EBUSY;
}

static void cnic_enable_bnx2_int(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;

	if (!(ethdev->drv_state & CNIC_DRV_STATE_USING_MSIX))
		return;

	CNIC_WR(dev, BNX2_PCICFG_INT_ACK_CMD, cp->int_num |
		BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID | cp->last_status_idx);
}

static void cnic_disable_bnx2_int_sync(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;

	if (!(ethdev->drv_state & CNIC_DRV_STATE_USING_MSIX))
		return;

	CNIC_WR(dev, BNX2_PCICFG_INT_ACK_CMD, cp->int_num |
		BNX2_PCICFG_INT_ACK_CMD_MASK_INT);
	CNIC_RD(dev, BNX2_PCICFG_INT_ACK_CMD);
	synchronize_irq(ethdev->irq_arr[0].vector);
}

static void cnic_init_bnx2_tx_ring(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	u32 cid_addr, tx_cid, sb_id;
	u32 val, offset0, offset1, offset2, offset3;
	int i;
	struct tx_bd *txbd;
	dma_addr_t buf_map;
	struct status_block *s_blk = cp->status_blk;

	sb_id = cp->status_blk_num;
	tx_cid = 20;
	cnic_init_context(dev, tx_cid);
	cnic_init_context(dev, tx_cid + 1);
	cp->tx_cons_ptr = &s_blk->status_tx_quick_consumer_index2;
	if (ethdev->drv_state & CNIC_DRV_STATE_USING_MSIX) {
		struct status_block_msix *sblk = cp->status_blk;

		tx_cid = TX_TSS_CID + sb_id - 1;
		cnic_init_context(dev, tx_cid);
		CNIC_WR(dev, BNX2_TSCH_TSS_CFG, (sb_id << 24) |
			(TX_TSS_CID << 7));
		cp->tx_cons_ptr = &sblk->status_tx_quick_consumer_index;
	}
	cp->tx_cons = *cp->tx_cons_ptr;

	cid_addr = GET_CID_ADDR(tx_cid);
	if (CHIP_NUM(cp) == CHIP_NUM_5709) {
		u32 cid_addr2 = GET_CID_ADDR(tx_cid + 4) + 0x40;

		for (i = 0; i < PHY_CTX_SIZE; i += 4)
			cnic_ctx_wr(dev, cid_addr2, i, 0);

		offset0 = BNX2_L2CTX_TYPE_XI;
		offset1 = BNX2_L2CTX_CMD_TYPE_XI;
		offset2 = BNX2_L2CTX_TBDR_BHADDR_HI_XI;
		offset3 = BNX2_L2CTX_TBDR_BHADDR_LO_XI;
	} else {
		offset0 = BNX2_L2CTX_TYPE;
		offset1 = BNX2_L2CTX_CMD_TYPE;
		offset2 = BNX2_L2CTX_TBDR_BHADDR_HI;
		offset3 = BNX2_L2CTX_TBDR_BHADDR_LO;
	}
	val = BNX2_L2CTX_TYPE_TYPE_L2 | BNX2_L2CTX_TYPE_SIZE_L2;
	cnic_ctx_wr(dev, cid_addr, offset0, val);

	val = BNX2_L2CTX_CMD_TYPE_TYPE_L2 | (8 << 16);
	cnic_ctx_wr(dev, cid_addr, offset1, val);

	txbd = (struct tx_bd *) cp->l2_ring;

	buf_map = cp->l2_buf_map;
	for (i = 0; i < MAX_TX_DESC_CNT; i++, txbd++) {
		txbd->tx_bd_haddr_hi = (u64) buf_map >> 32;
		txbd->tx_bd_haddr_lo = (u64) buf_map & 0xffffffff;
	}
	val = (u64) cp->l2_ring_map >> 32;
	cnic_ctx_wr(dev, cid_addr, offset2, val);
	txbd->tx_bd_haddr_hi = val;

	val = (u64) cp->l2_ring_map & 0xffffffff;
	cnic_ctx_wr(dev, cid_addr, offset3, val);
	txbd->tx_bd_haddr_lo = val;
}

static void cnic_init_bnx2_rx_ring(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	u32 cid_addr, sb_id, val, coal_reg, coal_val;
	int i;
	struct rx_bd *rxbd;
	struct status_block *s_blk = cp->status_blk;

	sb_id = cp->status_blk_num;
	cnic_init_context(dev, 2);
	cp->rx_cons_ptr = &s_blk->status_rx_quick_consumer_index2;
	coal_reg = BNX2_HC_COMMAND;
	coal_val = CNIC_RD(dev, coal_reg);
	if (ethdev->drv_state & CNIC_DRV_STATE_USING_MSIX) {
		struct status_block_msix *sblk = cp->status_blk;

		cp->rx_cons_ptr = &sblk->status_rx_quick_consumer_index;
		coal_reg = BNX2_HC_COALESCE_NOW;
		coal_val = 1 << (11 + sb_id);
	}
	i = 0;
	while (!(*cp->rx_cons_ptr != 0) && i < 10) {
		CNIC_WR(dev, coal_reg, coal_val);
		udelay(10);
		i++;
		barrier();
	}
	cp->rx_cons = *cp->rx_cons_ptr;

	cid_addr = GET_CID_ADDR(2);
	val = BNX2_L2CTX_CTX_TYPE_CTX_BD_CHN_TYPE_VALUE |
	      BNX2_L2CTX_CTX_TYPE_SIZE_L2 | (0x02 << 8);
	cnic_ctx_wr(dev, cid_addr, BNX2_L2CTX_CTX_TYPE, val);

	if (sb_id == 0)
		val = 2 << BNX2_L2CTX_STATUSB_NUM_SHIFT;
	else
		val = BNX2_L2CTX_STATUSB_NUM(sb_id);
	cnic_ctx_wr(dev, cid_addr, BNX2_L2CTX_HOST_BDIDX, val);

	rxbd = (struct rx_bd *) (cp->l2_ring + BCM_PAGE_SIZE);
	for (i = 0; i < MAX_RX_DESC_CNT; i++, rxbd++) {
		dma_addr_t buf_map;
		int n = (i % cp->l2_rx_ring_size) + 1;

		buf_map = cp->l2_buf_map + (n * cp->l2_single_buf_size);
		rxbd->rx_bd_len = cp->l2_single_buf_size;
		rxbd->rx_bd_flags = RX_BD_FLAGS_START | RX_BD_FLAGS_END;
		rxbd->rx_bd_haddr_hi = (u64) buf_map >> 32;
		rxbd->rx_bd_haddr_lo = (u64) buf_map & 0xffffffff;
	}
	val = (u64) (cp->l2_ring_map + BCM_PAGE_SIZE) >> 32;
	cnic_ctx_wr(dev, cid_addr, BNX2_L2CTX_NX_BDHADDR_HI, val);
	rxbd->rx_bd_haddr_hi = val;

	val = (u64) (cp->l2_ring_map + BCM_PAGE_SIZE) & 0xffffffff;
	cnic_ctx_wr(dev, cid_addr, BNX2_L2CTX_NX_BDHADDR_LO, val);
	rxbd->rx_bd_haddr_lo = val;

	val = cnic_reg_rd_ind(dev, BNX2_RXP_SCRATCH_RXP_FLOOD);
	cnic_reg_wr_ind(dev, BNX2_RXP_SCRATCH_RXP_FLOOD, val | (1 << 2));
}

static void cnic_shutdown_bnx2_rx_ring(struct cnic_dev *dev)
{
	struct kwqe *wqes[1], l2kwqe;

	memset(&l2kwqe, 0, sizeof(l2kwqe));
	wqes[0] = &l2kwqe;
	l2kwqe.kwqe_op_flag = (L2_LAYER_CODE << KWQE_FLAGS_LAYER_SHIFT) |
			      (L2_KWQE_OPCODE_VALUE_FLUSH <<
			       KWQE_OPCODE_SHIFT) | 2;
	dev->submit_kwqes(dev, wqes, 1);
}

static void cnic_set_bnx2_mac(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	u32 val;

	val = cp->func << 2;

	cp->shmem_base = cnic_reg_rd_ind(dev, BNX2_SHM_HDR_ADDR_0 + val);

	val = cnic_reg_rd_ind(dev, cp->shmem_base +
			      BNX2_PORT_HW_CFG_ISCSI_MAC_UPPER);
	dev->mac_addr[0] = (u8) (val >> 8);
	dev->mac_addr[1] = (u8) val;

	CNIC_WR(dev, BNX2_EMAC_MAC_MATCH4, val);

	val = cnic_reg_rd_ind(dev, cp->shmem_base +
			      BNX2_PORT_HW_CFG_ISCSI_MAC_LOWER);
	dev->mac_addr[2] = (u8) (val >> 24);
	dev->mac_addr[3] = (u8) (val >> 16);
	dev->mac_addr[4] = (u8) (val >> 8);
	dev->mac_addr[5] = (u8) val;

	CNIC_WR(dev, BNX2_EMAC_MAC_MATCH5, val);

	val = 4 | BNX2_RPM_SORT_USER2_BC_EN;
	if (CHIP_NUM(cp) != CHIP_NUM_5709)
		val |= BNX2_RPM_SORT_USER2_PROM_VLAN;

	CNIC_WR(dev, BNX2_RPM_SORT_USER2, 0x0);
	CNIC_WR(dev, BNX2_RPM_SORT_USER2, val);
	CNIC_WR(dev, BNX2_RPM_SORT_USER2, val | BNX2_RPM_SORT_USER2_ENA);
}

static int cnic_start_bnx2_hw(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	struct status_block *sblk = cp->status_blk;
	u32 val;
	int err;

	cnic_set_bnx2_mac(dev);

	val = CNIC_RD(dev, BNX2_MQ_CONFIG);
	val &= ~BNX2_MQ_CONFIG_KNL_BYP_BLK_SIZE;
	if (BCM_PAGE_BITS > 12)
		val |= (12 - 8)  << 4;
	else
		val |= (BCM_PAGE_BITS - 8)  << 4;

	CNIC_WR(dev, BNX2_MQ_CONFIG, val);

	CNIC_WR(dev, BNX2_HC_COMP_PROD_TRIP, (2 << 16) | 8);
	CNIC_WR(dev, BNX2_HC_COM_TICKS, (64 << 16) | 220);
	CNIC_WR(dev, BNX2_HC_CMD_TICKS, (64 << 16) | 220);

	err = cnic_setup_5709_context(dev, 1);
	if (err)
		return err;

	cnic_init_context(dev, KWQ_CID);
	cnic_init_context(dev, KCQ_CID);

	cp->kwq_cid_addr = GET_CID_ADDR(KWQ_CID);
	cp->kwq_io_addr = MB_GET_CID_ADDR(KWQ_CID) + L5_KRNLQ_HOST_QIDX;

	cp->max_kwq_idx = MAX_KWQ_IDX;
	cp->kwq_prod_idx = 0;
	cp->kwq_con_idx = 0;
	cp->cnic_local_flags |= CNIC_LCL_FL_KWQ_INIT;

	if (CHIP_NUM(cp) == CHIP_NUM_5706 || CHIP_NUM(cp) == CHIP_NUM_5708)
		cp->kwq_con_idx_ptr = &sblk->status_rx_quick_consumer_index15;
	else
		cp->kwq_con_idx_ptr = &sblk->status_cmd_consumer_index;

	/* Initialize the kernel work queue context. */
	val = KRNLQ_TYPE_TYPE_KRNLQ | KRNLQ_SIZE_TYPE_SIZE |
	      (BCM_PAGE_BITS - 8) | KRNLQ_FLAGS_QE_SELF_SEQ;
	cnic_ctx_wr(dev, cp->kwq_cid_addr, L5_KRNLQ_TYPE, val);

	val = (BCM_PAGE_SIZE / sizeof(struct kwqe) - 1) << 16;
	cnic_ctx_wr(dev, cp->kwq_cid_addr, L5_KRNLQ_QE_SELF_SEQ_MAX, val);

	val = ((BCM_PAGE_SIZE / sizeof(struct kwqe)) << 16) | KWQ_PAGE_CNT;
	cnic_ctx_wr(dev, cp->kwq_cid_addr, L5_KRNLQ_PGTBL_NPAGES, val);

	val = (u32) ((u64) cp->kwq_info.pgtbl_map >> 32);
	cnic_ctx_wr(dev, cp->kwq_cid_addr, L5_KRNLQ_PGTBL_HADDR_HI, val);

	val = (u32) cp->kwq_info.pgtbl_map;
	cnic_ctx_wr(dev, cp->kwq_cid_addr, L5_KRNLQ_PGTBL_HADDR_LO, val);

	cp->kcq_cid_addr = GET_CID_ADDR(KCQ_CID);
	cp->kcq_io_addr = MB_GET_CID_ADDR(KCQ_CID) + L5_KRNLQ_HOST_QIDX;

	cp->kcq_prod_idx = 0;

	/* Initialize the kernel complete queue context. */
	val = KRNLQ_TYPE_TYPE_KRNLQ | KRNLQ_SIZE_TYPE_SIZE |
	      (BCM_PAGE_BITS - 8) | KRNLQ_FLAGS_QE_SELF_SEQ;
	cnic_ctx_wr(dev, cp->kcq_cid_addr, L5_KRNLQ_TYPE, val);

	val = (BCM_PAGE_SIZE / sizeof(struct kcqe) - 1) << 16;
	cnic_ctx_wr(dev, cp->kcq_cid_addr, L5_KRNLQ_QE_SELF_SEQ_MAX, val);

	val = ((BCM_PAGE_SIZE / sizeof(struct kcqe)) << 16) | KCQ_PAGE_CNT;
	cnic_ctx_wr(dev, cp->kcq_cid_addr, L5_KRNLQ_PGTBL_NPAGES, val);

	val = (u32) ((u64) cp->kcq_info.pgtbl_map >> 32);
	cnic_ctx_wr(dev, cp->kcq_cid_addr, L5_KRNLQ_PGTBL_HADDR_HI, val);

	val = (u32) cp->kcq_info.pgtbl_map;
	cnic_ctx_wr(dev, cp->kcq_cid_addr, L5_KRNLQ_PGTBL_HADDR_LO, val);

	cp->int_num = 0;
	if (ethdev->drv_state & CNIC_DRV_STATE_USING_MSIX) {
		u32 sb_id = cp->status_blk_num;
		u32 sb = BNX2_L2CTX_STATUSB_NUM(sb_id);

		cp->int_num = sb_id << BNX2_PCICFG_INT_ACK_CMD_INT_NUM_SHIFT;
		cnic_ctx_wr(dev, cp->kwq_cid_addr, L5_KRNLQ_HOST_QIDX, sb);
		cnic_ctx_wr(dev, cp->kcq_cid_addr, L5_KRNLQ_HOST_QIDX, sb);
	}

	/* Enable Commnad Scheduler notification when we write to the
	 * host producer index of the kernel contexts. */
	CNIC_WR(dev, BNX2_MQ_KNL_CMD_MASK1, 2);

	/* Enable Command Scheduler notification when we write to either
	 * the Send Queue or Receive Queue producer indexes of the kernel
	 * bypass contexts. */
	CNIC_WR(dev, BNX2_MQ_KNL_BYP_CMD_MASK1, 7);
	CNIC_WR(dev, BNX2_MQ_KNL_BYP_WRITE_MASK1, 7);

	/* Notify COM when the driver post an application buffer. */
	CNIC_WR(dev, BNX2_MQ_KNL_RX_V2P_MASK2, 0x2000);

	/* Set the CP and COM doorbells.  These two processors polls the
	 * doorbell for a non zero value before running.  This must be done
	 * after setting up the kernel queue contexts. */
	cnic_reg_wr_ind(dev, BNX2_CP_SCRATCH + 0x20, 1);
	cnic_reg_wr_ind(dev, BNX2_COM_SCRATCH + 0x20, 1);

	cnic_init_bnx2_tx_ring(dev);
	cnic_init_bnx2_rx_ring(dev);

	err = cnic_init_bnx2_irq(dev);
	if (err) {
		printk(KERN_ERR PFX "%s: cnic_init_irq failed\n",
		       dev->netdev->name);
		cnic_reg_wr_ind(dev, BNX2_CP_SCRATCH + 0x20, 0);
		cnic_reg_wr_ind(dev, BNX2_COM_SCRATCH + 0x20, 0);
		return err;
	}

	return 0;
}

static int cnic_start_hw(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	int err;

	if (test_bit(CNIC_F_CNIC_UP, &dev->flags))
		return -EALREADY;

	err = ethdev->drv_register_cnic(dev->netdev, cp->cnic_ops, dev);
	if (err) {
		printk(KERN_ERR PFX "%s: register_cnic failed\n",
		       dev->netdev->name);
		goto err2;
	}

	dev->regview = ethdev->io_base;
	cp->chip_id = ethdev->chip_id;
	pci_dev_get(dev->pcidev);
	cp->func = PCI_FUNC(dev->pcidev->devfn);
	cp->status_blk = ethdev->irq_arr[0].status_blk;
	cp->status_blk_num = ethdev->irq_arr[0].status_blk_num;

	err = cp->alloc_resc(dev);
	if (err) {
		printk(KERN_ERR PFX "%s: allocate resource failure\n",
		       dev->netdev->name);
		goto err1;
	}

	err = cp->start_hw(dev);
	if (err)
		goto err1;

	err = cnic_cm_open(dev);
	if (err)
		goto err1;

	set_bit(CNIC_F_CNIC_UP, &dev->flags);

	cp->enable_int(dev);

	return 0;

err1:
	ethdev->drv_unregister_cnic(dev->netdev);
	cp->free_resc(dev);
	pci_dev_put(dev->pcidev);
err2:
	return err;
}

static void cnic_stop_bnx2_hw(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;

	cnic_disable_bnx2_int_sync(dev);

	cnic_reg_wr_ind(dev, BNX2_CP_SCRATCH + 0x20, 0);
	cnic_reg_wr_ind(dev, BNX2_COM_SCRATCH + 0x20, 0);

	cnic_init_context(dev, KWQ_CID);
	cnic_init_context(dev, KCQ_CID);

	cnic_setup_5709_context(dev, 0);
	cnic_free_irq(dev);

	ethdev->drv_unregister_cnic(dev->netdev);

	cnic_free_resc(dev);
}

static void cnic_stop_hw(struct cnic_dev *dev)
{
	if (test_bit(CNIC_F_CNIC_UP, &dev->flags)) {
		struct cnic_local *cp = dev->cnic_priv;

		clear_bit(CNIC_F_CNIC_UP, &dev->flags);
		rcu_assign_pointer(cp->ulp_ops[CNIC_ULP_L4], NULL);
		synchronize_rcu();
		cnic_cm_shutdown(dev);
		cp->stop_hw(dev);
		pci_dev_put(dev->pcidev);
	}
}

static void cnic_free_dev(struct cnic_dev *dev)
{
	int i = 0;

	while ((atomic_read(&dev->ref_count) != 0) && i < 10) {
		msleep(100);
		i++;
	}
	if (atomic_read(&dev->ref_count) != 0)
		printk(KERN_ERR PFX "%s: Failed waiting for ref count to go"
				    " to zero.\n", dev->netdev->name);

	printk(KERN_INFO PFX "Removed CNIC device: %s\n", dev->netdev->name);
	dev_put(dev->netdev);
	kfree(dev);
}

static struct cnic_dev *cnic_alloc_dev(struct net_device *dev,
				       struct pci_dev *pdev)
{
	struct cnic_dev *cdev;
	struct cnic_local *cp;
	int alloc_size;

	alloc_size = sizeof(struct cnic_dev) + sizeof(struct cnic_local);

	cdev = kzalloc(alloc_size , GFP_KERNEL);
	if (cdev == NULL) {
		printk(KERN_ERR PFX "%s: allocate dev struct failure\n",
		       dev->name);
		return NULL;
	}

	cdev->netdev = dev;
	cdev->cnic_priv = (char *)cdev + sizeof(struct cnic_dev);
	cdev->register_device = cnic_register_device;
	cdev->unregister_device = cnic_unregister_device;
	cdev->iscsi_nl_msg_recv = cnic_iscsi_nl_msg_recv;

	cp = cdev->cnic_priv;
	cp->dev = cdev;
	cp->uio_dev = -1;
	cp->l2_single_buf_size = 0x400;
	cp->l2_rx_ring_size = 3;

	spin_lock_init(&cp->cnic_ulp_lock);

	printk(KERN_INFO PFX "Added CNIC device: %s\n", dev->name);

	return cdev;
}

static struct cnic_dev *init_bnx2_cnic(struct net_device *dev)
{
	struct pci_dev *pdev;
	struct cnic_dev *cdev;
	struct cnic_local *cp;
	struct cnic_eth_dev *ethdev = NULL;
	struct cnic_eth_dev *(*probe)(void *) = NULL;

	probe = __symbol_get("bnx2_cnic_probe");
	if (probe) {
		ethdev = (*probe)(dev);
		symbol_put_addr(probe);
	}
	if (!ethdev)
		return NULL;

	pdev = ethdev->pdev;
	if (!pdev)
		return NULL;

	dev_hold(dev);
	pci_dev_get(pdev);
	if (pdev->device == PCI_DEVICE_ID_NX2_5709 ||
	    pdev->device == PCI_DEVICE_ID_NX2_5709S) {
		u8 rev;

		pci_read_config_byte(pdev, PCI_REVISION_ID, &rev);
		if (rev < 0x10) {
			pci_dev_put(pdev);
			goto cnic_err;
		}
	}
	pci_dev_put(pdev);

	cdev = cnic_alloc_dev(dev, pdev);
	if (cdev == NULL)
		goto cnic_err;

	set_bit(CNIC_F_BNX2_CLASS, &cdev->flags);
	cdev->submit_kwqes = cnic_submit_bnx2_kwqes;

	cp = cdev->cnic_priv;
	cp->ethdev = ethdev;
	cdev->pcidev = pdev;

	cp->cnic_ops = &cnic_bnx2_ops;
	cp->start_hw = cnic_start_bnx2_hw;
	cp->stop_hw = cnic_stop_bnx2_hw;
	cp->setup_pgtbl = cnic_setup_page_tbl;
	cp->alloc_resc = cnic_alloc_bnx2_resc;
	cp->free_resc = cnic_free_resc;
	cp->start_cm = cnic_cm_init_bnx2_hw;
	cp->stop_cm = cnic_cm_stop_bnx2_hw;
	cp->enable_int = cnic_enable_bnx2_int;
	cp->disable_int_sync = cnic_disable_bnx2_int_sync;
	cp->close_conn = cnic_close_bnx2_conn;
	cp->next_idx = cnic_bnx2_next_idx;
	cp->hw_idx = cnic_bnx2_hw_idx;
	return cdev;

cnic_err:
	dev_put(dev);
	return NULL;
}

static struct cnic_dev *is_cnic_dev(struct net_device *dev)
{
	struct ethtool_drvinfo drvinfo;
	struct cnic_dev *cdev = NULL;

	if (dev->ethtool_ops && dev->ethtool_ops->get_drvinfo) {
		memset(&drvinfo, 0, sizeof(drvinfo));
		dev->ethtool_ops->get_drvinfo(dev, &drvinfo);

		if (!strcmp(drvinfo.driver, "bnx2"))
			cdev = init_bnx2_cnic(dev);
		if (cdev) {
			write_lock(&cnic_dev_lock);
			list_add(&cdev->list, &cnic_dev_list);
			write_unlock(&cnic_dev_lock);
		}
	}
	return cdev;
}

/**
 * netdev event handler
 */
static int cnic_netdev_event(struct notifier_block *this, unsigned long event,
							 void *ptr)
{
	struct net_device *netdev = ptr;
	struct cnic_dev *dev;
	int if_type;
	int new_dev = 0;

	dev = cnic_from_netdev(netdev);

	if (!dev && (event == NETDEV_REGISTER || event == NETDEV_UP)) {
		/* Check for the hot-plug device */
		dev = is_cnic_dev(netdev);
		if (dev) {
			new_dev = 1;
			cnic_hold(dev);
		}
	}
	if (dev) {
		struct cnic_local *cp = dev->cnic_priv;

		if (new_dev)
			cnic_ulp_init(dev);
		else if (event == NETDEV_UNREGISTER)
			cnic_ulp_exit(dev);
		else if (event == NETDEV_UP) {
			mutex_lock(&cnic_lock);
			if (!cnic_start_hw(dev))
				cnic_ulp_start(dev);
			mutex_unlock(&cnic_lock);
		}

		rcu_read_lock();
		for (if_type = 0; if_type < MAX_CNIC_ULP_TYPE; if_type++) {
			struct cnic_ulp_ops *ulp_ops;
			void *ctx;

			ulp_ops = rcu_dereference(cp->ulp_ops[if_type]);
			if (!ulp_ops || !ulp_ops->indicate_netevent)
				continue;

			ctx = cp->ulp_handle[if_type];

			ulp_ops->indicate_netevent(ctx, event);
		}
		rcu_read_unlock();

		if (event == NETDEV_GOING_DOWN) {
			mutex_lock(&cnic_lock);
			cnic_ulp_stop(dev);
			cnic_stop_hw(dev);
			mutex_unlock(&cnic_lock);
		} else if (event == NETDEV_UNREGISTER) {
			write_lock(&cnic_dev_lock);
			list_del_init(&dev->list);
			write_unlock(&cnic_dev_lock);

			cnic_put(dev);
			cnic_free_dev(dev);
			goto done;
		}
		cnic_put(dev);
	}
done:
	return NOTIFY_DONE;
}

static struct notifier_block cnic_netdev_notifier = {
	.notifier_call = cnic_netdev_event
};

static void cnic_release(void)
{
	struct cnic_dev *dev;

	while (!list_empty(&cnic_dev_list)) {
		dev = list_entry(cnic_dev_list.next, struct cnic_dev, list);
		if (test_bit(CNIC_F_CNIC_UP, &dev->flags)) {
			cnic_ulp_stop(dev);
			cnic_stop_hw(dev);
		}

		cnic_ulp_exit(dev);
		list_del_init(&dev->list);
		cnic_free_dev(dev);
	}
}

static int __init cnic_init(void)
{
	int rc = 0;

	printk(KERN_INFO "%s", version);

	rc = register_netdevice_notifier(&cnic_netdev_notifier);
	if (rc) {
		cnic_release();
		return rc;
	}

	return 0;
}

static void __exit cnic_exit(void)
{
	unregister_netdevice_notifier(&cnic_netdev_notifier);
	cnic_release();
	return;
}

module_init(cnic_init);
module_exit(cnic_exit);
