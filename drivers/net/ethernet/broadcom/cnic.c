/* cnic.c: QLogic CNIC core network driver.
 *
 * Copyright (c) 2006-2014 Broadcom Corporation
 * Copyright (c) 2014 QLogic Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Original skeleton written by: John(Zongxi) Chen (zongxi@broadcom.com)
 * Previously modified and maintained by: Michael Chan <mchan@broadcom.com>
 * Maintained By: Dept-HSGLinuxNICDev@qlogic.com
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

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
#include <linux/prefetch.h>
#include <linux/random.h>
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
#define BCM_VLAN 1
#endif
#include <net/ip.h>
#include <net/tcp.h>
#include <net/route.h>
#include <net/ipv6.h>
#include <net/ip6_route.h>
#include <net/ip6_checksum.h>
#include <scsi/iscsi_if.h>

#define BCM_CNIC	1
#include "cnic_if.h"
#include "bnx2.h"
#include "bnx2x/bnx2x.h"
#include "bnx2x/bnx2x_reg.h"
#include "bnx2x/bnx2x_fw_defs.h"
#include "bnx2x/bnx2x_hsi.h"
#include "../../../scsi/bnx2i/57xx_iscsi_constants.h"
#include "../../../scsi/bnx2i/57xx_iscsi_hsi.h"
#include "../../../scsi/bnx2fc/bnx2fc_constants.h"
#include "cnic.h"
#include "cnic_defs.h"

#define CNIC_MODULE_NAME	"cnic"

static char version[] =
	"QLogic NetXtreme II CNIC Driver " CNIC_MODULE_NAME " v" CNIC_MODULE_VERSION " (" CNIC_MODULE_RELDATE ")\n";

MODULE_AUTHOR("Michael Chan <mchan@broadcom.com> and John(Zongxi) "
	      "Chen (zongxi@broadcom.com");
MODULE_DESCRIPTION("QLogic NetXtreme II CNIC Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(CNIC_MODULE_VERSION);

/* cnic_dev_list modifications are protected by both rtnl and cnic_dev_lock */
static LIST_HEAD(cnic_dev_list);
static LIST_HEAD(cnic_udev_list);
static DEFINE_RWLOCK(cnic_dev_lock);
static DEFINE_MUTEX(cnic_lock);

static struct cnic_ulp_ops __rcu *cnic_ulp_tbl[MAX_CNIC_ULP_TYPE];

/* helper function, assuming cnic_lock is held */
static inline struct cnic_ulp_ops *cnic_ulp_tbl_prot(int type)
{
	return rcu_dereference_protected(cnic_ulp_tbl[type],
					 lockdep_is_held(&cnic_lock));
}

static int cnic_service_bnx2(void *, void *);
static int cnic_service_bnx2x(void *, void *);
static int cnic_ctl(void *, struct cnic_ctl_info *);

static struct cnic_ops cnic_bnx2_ops = {
	.cnic_owner	= THIS_MODULE,
	.cnic_handler	= cnic_service_bnx2,
	.cnic_ctl	= cnic_ctl,
};

static struct cnic_ops cnic_bnx2x_ops = {
	.cnic_owner	= THIS_MODULE,
	.cnic_handler	= cnic_service_bnx2x,
	.cnic_ctl	= cnic_ctl,
};

static struct workqueue_struct *cnic_wq;

static void cnic_shutdown_rings(struct cnic_dev *);
static void cnic_init_rings(struct cnic_dev *);
static int cnic_cm_set_pg(struct cnic_sock *);

static int cnic_uio_open(struct uio_info *uinfo, struct inode *inode)
{
	struct cnic_uio_dev *udev = uinfo->priv;
	struct cnic_dev *dev;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (udev->uio_dev != -1)
		return -EBUSY;

	rtnl_lock();
	dev = udev->dev;

	if (!dev || !test_bit(CNIC_F_CNIC_UP, &dev->flags)) {
		rtnl_unlock();
		return -ENODEV;
	}

	udev->uio_dev = iminor(inode);

	cnic_shutdown_rings(dev);
	cnic_init_rings(dev);
	rtnl_unlock();

	return 0;
}

static int cnic_uio_close(struct uio_info *uinfo, struct inode *inode)
{
	struct cnic_uio_dev *udev = uinfo->priv;

	udev->uio_dev = -1;
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

static inline void ulp_get(struct cnic_ulp_ops *ulp_ops)
{
	atomic_inc(&ulp_ops->ref_count);
}

static inline void ulp_put(struct cnic_ulp_ops *ulp_ops)
{
	atomic_dec(&ulp_ops->ref_count);
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

static void cnic_ctx_tbl_wr(struct cnic_dev *dev, u32 off, dma_addr_t addr)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	struct drv_ctl_info info;
	struct drv_ctl_io *io = &info.data.io;

	info.cmd = DRV_CTL_CTXTBL_WR_CMD;
	io->offset = off;
	io->dma_addr = addr;
	ethdev->drv_ctl(dev->netdev, &info);
}

static void cnic_ring_ctl(struct cnic_dev *dev, u32 cid, u32 cl_id, int start)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	struct drv_ctl_info info;
	struct drv_ctl_l2_ring *ring = &info.data.ring;

	if (start)
		info.cmd = DRV_CTL_START_L2_CMD;
	else
		info.cmd = DRV_CTL_STOP_L2_CMD;

	ring->cid = cid;
	ring->client_id = cl_id;
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

static void cnic_ulp_ctl(struct cnic_dev *dev, int ulp_type, bool reg)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	struct drv_ctl_info info;
	struct fcoe_capabilities *fcoe_cap =
		&info.data.register_data.fcoe_features;

	if (reg) {
		info.cmd = DRV_CTL_ULP_REGISTER_CMD;
		if (ulp_type == CNIC_ULP_FCOE && dev->fcoe_cap)
			memcpy(fcoe_cap, dev->fcoe_cap, sizeof(*fcoe_cap));
	} else {
		info.cmd = DRV_CTL_ULP_UNREGISTER_CMD;
	}

	info.data.ulp_type = ulp_type;
	ethdev->drv_ctl(dev->netdev, &info);
}

static int cnic_in_use(struct cnic_sock *csk)
{
	return test_bit(SK_F_INUSE, &csk->flags);
}

static void cnic_spq_completion(struct cnic_dev *dev, int cmd, u32 count)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	struct drv_ctl_info info;

	info.cmd = cmd;
	info.data.credit.credit_count = count;
	ethdev->drv_ctl(dev->netdev, &info);
}

static int cnic_get_l5_cid(struct cnic_local *cp, u32 cid, u32 *l5_cid)
{
	u32 i;

	if (!cp->ctx_tbl)
		return -EINVAL;

	for (i = 0; i < cp->max_cid_space; i++) {
		if (cp->ctx_tbl[i].cid == cid) {
			*l5_cid = i;
			return 0;
		}
	}
	return -EINVAL;
}

static int cnic_send_nlmsg(struct cnic_local *cp, u32 type,
			   struct cnic_sock *csk)
{
	struct iscsi_path path_req;
	char *buf = NULL;
	u16 len = 0;
	u32 msg_type = ISCSI_KEVENT_IF_DOWN;
	struct cnic_ulp_ops *ulp_ops;
	struct cnic_uio_dev *udev = cp->udev;
	int rc = 0, retry = 0;

	if (!udev || udev->uio_dev == -1)
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

	while (retry < 3) {
		rc = 0;
		rcu_read_lock();
		ulp_ops = rcu_dereference(cp->ulp_ops[CNIC_ULP_ISCSI]);
		if (ulp_ops)
			rc = ulp_ops->iscsi_nl_send_msg(
				cp->ulp_handle[CNIC_ULP_ISCSI],
				msg_type, buf, len);
		rcu_read_unlock();
		if (rc == 0 || msg_type != ISCSI_KEVENT_PATH_REQ)
			break;

		msleep(100);
		retry++;
	}
	return rc;
}

static void cnic_cm_upcall(struct cnic_local *, struct cnic_sock *, u8);

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

		rcu_read_lock();
		if (!rcu_dereference(cp->ulp_ops[CNIC_ULP_L4])) {
			rc = -ENODEV;
			rcu_read_unlock();
			break;
		}
		csk = &cp->csk_tbl[l5_cid];
		csk_hold(csk);
		if (cnic_in_use(csk) &&
		    test_bit(SK_F_CONNECT_START, &csk->flags)) {

			csk->vlan_id = path_resp->vlan_id;

			memcpy(csk->ha, path_resp->mac_addr, ETH_ALEN);
			if (test_bit(SK_F_IPV6, &csk->flags))
				memcpy(&csk->src_ip[0], &path_resp->src.v6_addr,
				       sizeof(struct in6_addr));
			else
				memcpy(&csk->src_ip[0], &path_resp->src.v4_addr,
				       sizeof(struct in_addr));

			if (is_valid_ether_addr(csk->ha)) {
				cnic_cm_set_pg(csk);
			} else if (!test_bit(SK_F_OFFLD_SCHED, &csk->flags) &&
				!test_bit(SK_F_OFFLD_COMPLETE, &csk->flags)) {

				cnic_cm_upcall(cp, csk,
					L4_KCQE_OPCODE_VALUE_CONNECT_COMPLETE);
				clear_bit(SK_F_CONNECT_START, &csk->flags);
			}
		}
		csk_put(csk);
		rcu_read_unlock();
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
	smp_mb__after_atomic();

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
	smp_mb__after_atomic();

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

	if (ulp_type < 0 || ulp_type >= MAX_CNIC_ULP_TYPE) {
		pr_err("%s: Bad type %d\n", __func__, ulp_type);
		return -EINVAL;
	}
	mutex_lock(&cnic_lock);
	if (cnic_ulp_tbl_prot(ulp_type)) {
		pr_err("%s: Type %d has already been registered\n",
		       __func__, ulp_type);
		mutex_unlock(&cnic_lock);
		return -EBUSY;
	}

	read_lock(&cnic_dev_lock);
	list_for_each_entry(dev, &cnic_dev_list, list) {
		struct cnic_local *cp = dev->cnic_priv;

		clear_bit(ULP_F_INIT, &cp->ulp_flags[ulp_type]);
	}
	read_unlock(&cnic_dev_lock);

	atomic_set(&ulp_ops->ref_count, 0);
	rcu_assign_pointer(cnic_ulp_tbl[ulp_type], ulp_ops);
	mutex_unlock(&cnic_lock);

	/* Prevent race conditions with netdev_event */
	rtnl_lock();
	list_for_each_entry(dev, &cnic_dev_list, list) {
		struct cnic_local *cp = dev->cnic_priv;

		if (!test_and_set_bit(ULP_F_INIT, &cp->ulp_flags[ulp_type]))
			ulp_ops->cnic_init(dev);
	}
	rtnl_unlock();

	return 0;
}

int cnic_unregister_driver(int ulp_type)
{
	struct cnic_dev *dev;
	struct cnic_ulp_ops *ulp_ops;
	int i = 0;

	if (ulp_type < 0 || ulp_type >= MAX_CNIC_ULP_TYPE) {
		pr_err("%s: Bad type %d\n", __func__, ulp_type);
		return -EINVAL;
	}
	mutex_lock(&cnic_lock);
	ulp_ops = cnic_ulp_tbl_prot(ulp_type);
	if (!ulp_ops) {
		pr_err("%s: Type %d has not been registered\n",
		       __func__, ulp_type);
		goto out_unlock;
	}
	read_lock(&cnic_dev_lock);
	list_for_each_entry(dev, &cnic_dev_list, list) {
		struct cnic_local *cp = dev->cnic_priv;

		if (rcu_dereference(cp->ulp_ops[ulp_type])) {
			pr_err("%s: Type %d still has devices registered\n",
			       __func__, ulp_type);
			read_unlock(&cnic_dev_lock);
			goto out_unlock;
		}
	}
	read_unlock(&cnic_dev_lock);

	RCU_INIT_POINTER(cnic_ulp_tbl[ulp_type], NULL);

	mutex_unlock(&cnic_lock);
	synchronize_rcu();
	while ((atomic_read(&ulp_ops->ref_count) != 0) && (i < 20)) {
		msleep(100);
		i++;
	}

	if (atomic_read(&ulp_ops->ref_count) != 0)
		pr_warn("%s: Failed waiting for ref count to go to zero\n",
			__func__);
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

	if (ulp_type < 0 || ulp_type >= MAX_CNIC_ULP_TYPE) {
		pr_err("%s: Bad type %d\n", __func__, ulp_type);
		return -EINVAL;
	}
	mutex_lock(&cnic_lock);
	if (cnic_ulp_tbl_prot(ulp_type) == NULL) {
		pr_err("%s: Driver with type %d has not been registered\n",
		       __func__, ulp_type);
		mutex_unlock(&cnic_lock);
		return -EAGAIN;
	}
	if (rcu_dereference(cp->ulp_ops[ulp_type])) {
		pr_err("%s: Type %d has already been registered to this device\n",
		       __func__, ulp_type);
		mutex_unlock(&cnic_lock);
		return -EBUSY;
	}

	clear_bit(ULP_F_START, &cp->ulp_flags[ulp_type]);
	cp->ulp_handle[ulp_type] = ulp_ctx;
	ulp_ops = cnic_ulp_tbl_prot(ulp_type);
	rcu_assign_pointer(cp->ulp_ops[ulp_type], ulp_ops);
	cnic_hold(dev);

	if (test_bit(CNIC_F_CNIC_UP, &dev->flags))
		if (!test_and_set_bit(ULP_F_START, &cp->ulp_flags[ulp_type]))
			ulp_ops->cnic_start(cp->ulp_handle[ulp_type]);

	mutex_unlock(&cnic_lock);

	cnic_ulp_ctl(dev, ulp_type, true);

	return 0;

}
EXPORT_SYMBOL(cnic_register_driver);

static int cnic_unregister_device(struct cnic_dev *dev, int ulp_type)
{
	struct cnic_local *cp = dev->cnic_priv;
	int i = 0;

	if (ulp_type < 0 || ulp_type >= MAX_CNIC_ULP_TYPE) {
		pr_err("%s: Bad type %d\n", __func__, ulp_type);
		return -EINVAL;
	}

	if (ulp_type == CNIC_ULP_ISCSI)
		cnic_send_nlmsg(cp, ISCSI_KEVENT_IF_DOWN, NULL);

	mutex_lock(&cnic_lock);
	if (rcu_dereference(cp->ulp_ops[ulp_type])) {
		RCU_INIT_POINTER(cp->ulp_ops[ulp_type], NULL);
		cnic_put(dev);
	} else {
		pr_err("%s: device not registered to this ulp type %d\n",
		       __func__, ulp_type);
		mutex_unlock(&cnic_lock);
		return -EINVAL;
	}
	mutex_unlock(&cnic_lock);

	if (ulp_type == CNIC_ULP_FCOE)
		dev->fcoe_cap = NULL;

	synchronize_rcu();

	while (test_bit(ULP_F_CALL_PENDING, &cp->ulp_flags[ulp_type]) &&
	       i < 20) {
		msleep(100);
		i++;
	}
	if (test_bit(ULP_F_CALL_PENDING, &cp->ulp_flags[ulp_type]))
		netdev_warn(dev->netdev, "Failed waiting for ULP up call to complete\n");

	cnic_ulp_ctl(dev, ulp_type, false);

	return 0;
}
EXPORT_SYMBOL(cnic_unregister_driver);

static int cnic_init_id_tbl(struct cnic_id_tbl *id_tbl, u32 size, u32 start_id,
			    u32 next)
{
	id_tbl->start = start_id;
	id_tbl->max = size;
	id_tbl->next = next;
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
			dma_free_coherent(&dev->pcidev->dev, CNIC_PAGE_SIZE,
					  dma->pg_arr[i], dma->pg_map_arr[i]);
			dma->pg_arr[i] = NULL;
		}
	}
	if (dma->pgtbl) {
		dma_free_coherent(&dev->pcidev->dev, dma->pgtbl_size,
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
	__le32 *page_table = (__le32 *) dma->pgtbl;

	for (i = 0; i < dma->num_pages; i++) {
		/* Each entry needs to be in big endian format. */
		*page_table = cpu_to_le32((u64) dma->pg_map_arr[i] >> 32);
		page_table++;
		*page_table = cpu_to_le32(dma->pg_map_arr[i] & 0xffffffff);
		page_table++;
	}
}

static void cnic_setup_page_tbl_le(struct cnic_dev *dev, struct cnic_dma *dma)
{
	int i;
	__le32 *page_table = (__le32 *) dma->pgtbl;

	for (i = 0; i < dma->num_pages; i++) {
		/* Each entry needs to be in little endian format. */
		*page_table = cpu_to_le32(dma->pg_map_arr[i] & 0xffffffff);
		page_table++;
		*page_table = cpu_to_le32((u64) dma->pg_map_arr[i] >> 32);
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
		dma->pg_arr[i] = dma_alloc_coherent(&dev->pcidev->dev,
						    CNIC_PAGE_SIZE,
						    &dma->pg_map_arr[i],
						    GFP_ATOMIC);
		if (dma->pg_arr[i] == NULL)
			goto error;
	}
	if (!use_pg_tbl)
		return 0;

	dma->pgtbl_size = ((pages * 8) + CNIC_PAGE_SIZE - 1) &
			  ~(CNIC_PAGE_SIZE - 1);
	dma->pgtbl = dma_alloc_coherent(&dev->pcidev->dev, dma->pgtbl_size,
					&dma->pgtbl_map, GFP_ATOMIC);
	if (dma->pgtbl == NULL)
		goto error;

	cp->setup_pgtbl(dev, dma);

	return 0;

error:
	cnic_free_dma(dev, dma);
	return -ENOMEM;
}

static void cnic_free_context(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	int i;

	for (i = 0; i < cp->ctx_blks; i++) {
		if (cp->ctx_arr[i].ctx) {
			dma_free_coherent(&dev->pcidev->dev, cp->ctx_blk_size,
					  cp->ctx_arr[i].ctx,
					  cp->ctx_arr[i].mapping);
			cp->ctx_arr[i].ctx = NULL;
		}
	}
}

static void __cnic_free_uio_rings(struct cnic_uio_dev *udev)
{
	if (udev->l2_buf) {
		dma_free_coherent(&udev->pdev->dev, udev->l2_buf_size,
				  udev->l2_buf, udev->l2_buf_map);
		udev->l2_buf = NULL;
	}

	if (udev->l2_ring) {
		dma_free_coherent(&udev->pdev->dev, udev->l2_ring_size,
				  udev->l2_ring, udev->l2_ring_map);
		udev->l2_ring = NULL;
	}

}

static void __cnic_free_uio(struct cnic_uio_dev *udev)
{
	uio_unregister_device(&udev->cnic_uinfo);

	__cnic_free_uio_rings(udev);

	pci_dev_put(udev->pdev);
	kfree(udev);
}

static void cnic_free_uio(struct cnic_uio_dev *udev)
{
	if (!udev)
		return;

	write_lock(&cnic_dev_lock);
	list_del_init(&udev->list);
	write_unlock(&cnic_dev_lock);
	__cnic_free_uio(udev);
}

static void cnic_free_resc(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_uio_dev *udev = cp->udev;

	if (udev) {
		udev->dev = NULL;
		cp->udev = NULL;
		if (udev->uio_dev == -1)
			__cnic_free_uio_rings(udev);
	}

	cnic_free_context(dev);
	kfree(cp->ctx_arr);
	cp->ctx_arr = NULL;
	cp->ctx_blks = 0;

	cnic_free_dma(dev, &cp->gbl_buf_info);
	cnic_free_dma(dev, &cp->kwq_info);
	cnic_free_dma(dev, &cp->kwq_16_data_info);
	cnic_free_dma(dev, &cp->kcq2.dma);
	cnic_free_dma(dev, &cp->kcq1.dma);
	kfree(cp->iscsi_tbl);
	cp->iscsi_tbl = NULL;
	kfree(cp->ctx_tbl);
	cp->ctx_tbl = NULL;

	cnic_free_id_tbl(&cp->fcoe_cid_tbl);
	cnic_free_id_tbl(&cp->cid_tbl);
}

static int cnic_alloc_context(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;

	if (BNX2_CHIP(cp) == BNX2_CHIP_5709) {
		int i, k, arr_size;

		cp->ctx_blk_size = CNIC_PAGE_SIZE;
		cp->cids_per_blk = CNIC_PAGE_SIZE / 128;
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
				dma_alloc_coherent(&dev->pcidev->dev,
						   CNIC_PAGE_SIZE,
						   &cp->ctx_arr[i].mapping,
						   GFP_KERNEL);
			if (cp->ctx_arr[i].ctx == NULL)
				return -ENOMEM;
		}
	}
	return 0;
}

static u16 cnic_bnx2_next_idx(u16 idx)
{
	return idx + 1;
}

static u16 cnic_bnx2_hw_idx(u16 idx)
{
	return idx;
}

static u16 cnic_bnx2x_next_idx(u16 idx)
{
	idx++;
	if ((idx & MAX_KCQE_CNT) == MAX_KCQE_CNT)
		idx++;

	return idx;
}

static u16 cnic_bnx2x_hw_idx(u16 idx)
{
	if ((idx & MAX_KCQE_CNT) == MAX_KCQE_CNT)
		idx++;
	return idx;
}

static int cnic_alloc_kcq(struct cnic_dev *dev, struct kcq_info *info,
			  bool use_pg_tbl)
{
	int err, i, use_page_tbl = 0;
	struct kcqe **kcq;

	if (use_pg_tbl)
		use_page_tbl = 1;

	err = cnic_alloc_dma(dev, &info->dma, KCQ_PAGE_CNT, use_page_tbl);
	if (err)
		return err;

	kcq = (struct kcqe **) info->dma.pg_arr;
	info->kcq = kcq;

	info->next_idx = cnic_bnx2_next_idx;
	info->hw_idx = cnic_bnx2_hw_idx;
	if (use_pg_tbl)
		return 0;

	info->next_idx = cnic_bnx2x_next_idx;
	info->hw_idx = cnic_bnx2x_hw_idx;

	for (i = 0; i < KCQ_PAGE_CNT; i++) {
		struct bnx2x_bd_chain_next *next =
			(struct bnx2x_bd_chain_next *) &kcq[i][MAX_KCQE_CNT];
		int j = i + 1;

		if (j >= KCQ_PAGE_CNT)
			j = 0;
		next->addr_hi = (u64) info->dma.pg_map_arr[j] >> 32;
		next->addr_lo = info->dma.pg_map_arr[j] & 0xffffffff;
	}
	return 0;
}

static int __cnic_alloc_uio_rings(struct cnic_uio_dev *udev, int pages)
{
	struct cnic_local *cp = udev->dev->cnic_priv;

	if (udev->l2_ring)
		return 0;

	udev->l2_ring_size = pages * CNIC_PAGE_SIZE;
	udev->l2_ring = dma_alloc_coherent(&udev->pdev->dev, udev->l2_ring_size,
					   &udev->l2_ring_map,
					   GFP_KERNEL | __GFP_COMP);
	if (!udev->l2_ring)
		return -ENOMEM;

	udev->l2_buf_size = (cp->l2_rx_ring_size + 1) * cp->l2_single_buf_size;
	udev->l2_buf_size = CNIC_PAGE_ALIGN(udev->l2_buf_size);
	udev->l2_buf = dma_alloc_coherent(&udev->pdev->dev, udev->l2_buf_size,
					  &udev->l2_buf_map,
					  GFP_KERNEL | __GFP_COMP);
	if (!udev->l2_buf) {
		__cnic_free_uio_rings(udev);
		return -ENOMEM;
	}

	return 0;

}

static int cnic_alloc_uio_rings(struct cnic_dev *dev, int pages)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_uio_dev *udev;

	list_for_each_entry(udev, &cnic_udev_list, list) {
		if (udev->pdev == dev->pcidev) {
			udev->dev = dev;
			if (__cnic_alloc_uio_rings(udev, pages)) {
				udev->dev = NULL;
				return -ENOMEM;
			}
			cp->udev = udev;
			return 0;
		}
	}

	udev = kzalloc(sizeof(struct cnic_uio_dev), GFP_ATOMIC);
	if (!udev)
		return -ENOMEM;

	udev->uio_dev = -1;

	udev->dev = dev;
	udev->pdev = dev->pcidev;

	if (__cnic_alloc_uio_rings(udev, pages))
		goto err_udev;

	list_add(&udev->list, &cnic_udev_list);

	pci_dev_get(udev->pdev);

	cp->udev = udev;

	return 0;

 err_udev:
	kfree(udev);
	return -ENOMEM;
}

static int cnic_init_uio(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_uio_dev *udev = cp->udev;
	struct uio_info *uinfo;
	int ret = 0;

	if (!udev)
		return -ENOMEM;

	uinfo = &udev->cnic_uinfo;

	uinfo->mem[0].addr = pci_resource_start(dev->pcidev, 0);
	uinfo->mem[0].internal_addr = dev->regview;
	uinfo->mem[0].memtype = UIO_MEM_PHYS;

	if (test_bit(CNIC_F_BNX2_CLASS, &dev->flags)) {
		uinfo->mem[0].size = MB_GET_CID_ADDR(TX_TSS_CID +
						     TX_MAX_TSS_RINGS + 1);
		uinfo->mem[1].addr = (unsigned long) cp->status_blk.gen &
					CNIC_PAGE_MASK;
		if (cp->ethdev->drv_state & CNIC_DRV_STATE_USING_MSIX)
			uinfo->mem[1].size = BNX2_SBLK_MSIX_ALIGN_SIZE * 9;
		else
			uinfo->mem[1].size = BNX2_SBLK_MSIX_ALIGN_SIZE;

		uinfo->name = "bnx2_cnic";
	} else if (test_bit(CNIC_F_BNX2X_CLASS, &dev->flags)) {
		uinfo->mem[0].size = pci_resource_len(dev->pcidev, 0);

		uinfo->mem[1].addr = (unsigned long) cp->bnx2x_def_status_blk &
			CNIC_PAGE_MASK;
		uinfo->mem[1].size = sizeof(*cp->bnx2x_def_status_blk);

		uinfo->name = "bnx2x_cnic";
	}

	uinfo->mem[1].memtype = UIO_MEM_LOGICAL;

	uinfo->mem[2].addr = (unsigned long) udev->l2_ring;
	uinfo->mem[2].size = udev->l2_ring_size;
	uinfo->mem[2].memtype = UIO_MEM_LOGICAL;

	uinfo->mem[3].addr = (unsigned long) udev->l2_buf;
	uinfo->mem[3].size = udev->l2_buf_size;
	uinfo->mem[3].memtype = UIO_MEM_LOGICAL;

	uinfo->version = CNIC_MODULE_VERSION;
	uinfo->irq = UIO_IRQ_CUSTOM;

	uinfo->open = cnic_uio_open;
	uinfo->release = cnic_uio_close;

	if (udev->uio_dev == -1) {
		if (!uinfo->priv) {
			uinfo->priv = udev;

			ret = uio_register_device(&udev->pdev->dev, uinfo);
		}
	} else {
		cnic_init_rings(dev);
	}

	return ret;
}

static int cnic_alloc_bnx2_resc(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	int ret;

	ret = cnic_alloc_dma(dev, &cp->kwq_info, KWQ_PAGE_CNT, 1);
	if (ret)
		goto error;
	cp->kwq = (struct kwqe **) cp->kwq_info.pg_arr;

	ret = cnic_alloc_kcq(dev, &cp->kcq1, true);
	if (ret)
		goto error;

	ret = cnic_alloc_context(dev);
	if (ret)
		goto error;

	ret = cnic_alloc_uio_rings(dev, 2);
	if (ret)
		goto error;

	ret = cnic_init_uio(dev);
	if (ret)
		goto error;

	return 0;

error:
	cnic_free_resc(dev);
	return ret;
}

static int cnic_alloc_bnx2x_context(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	int ctx_blk_size = cp->ethdev->ctx_blk_size;
	int total_mem, blks, i;

	total_mem = BNX2X_CONTEXT_MEM_SIZE * cp->max_cid_space;
	blks = total_mem / ctx_blk_size;
	if (total_mem % ctx_blk_size)
		blks++;

	if (blks > cp->ethdev->ctx_tbl_len)
		return -ENOMEM;

	cp->ctx_arr = kcalloc(blks, sizeof(struct cnic_ctx), GFP_KERNEL);
	if (cp->ctx_arr == NULL)
		return -ENOMEM;

	cp->ctx_blks = blks;
	cp->ctx_blk_size = ctx_blk_size;
	if (!CHIP_IS_E1(bp))
		cp->ctx_align = 0;
	else
		cp->ctx_align = ctx_blk_size;

	cp->cids_per_blk = ctx_blk_size / BNX2X_CONTEXT_MEM_SIZE;

	for (i = 0; i < blks; i++) {
		cp->ctx_arr[i].ctx =
			dma_alloc_coherent(&dev->pcidev->dev, cp->ctx_blk_size,
					   &cp->ctx_arr[i].mapping,
					   GFP_KERNEL);
		if (cp->ctx_arr[i].ctx == NULL)
			return -ENOMEM;

		if (cp->ctx_align && cp->ctx_blk_size == ctx_blk_size) {
			if (cp->ctx_arr[i].mapping & (cp->ctx_align - 1)) {
				cnic_free_context(dev);
				cp->ctx_blk_size += cp->ctx_align;
				i = -1;
				continue;
			}
		}
	}
	return 0;
}

static int cnic_alloc_bnx2x_resc(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	struct cnic_eth_dev *ethdev = cp->ethdev;
	u32 start_cid = ethdev->starting_cid;
	int i, j, n, ret, pages;
	struct cnic_dma *kwq_16_dma = &cp->kwq_16_data_info;

	cp->max_cid_space = MAX_ISCSI_TBL_SZ;
	cp->iscsi_start_cid = start_cid;
	cp->fcoe_start_cid = start_cid + MAX_ISCSI_TBL_SZ;

	if (BNX2X_CHIP_IS_E2_PLUS(bp)) {
		cp->max_cid_space += dev->max_fcoe_conn;
		cp->fcoe_init_cid = ethdev->fcoe_init_cid;
		if (!cp->fcoe_init_cid)
			cp->fcoe_init_cid = 0x10;
	}

	cp->iscsi_tbl = kzalloc(sizeof(struct cnic_iscsi) * MAX_ISCSI_TBL_SZ,
				GFP_KERNEL);
	if (!cp->iscsi_tbl)
		goto error;

	cp->ctx_tbl = kzalloc(sizeof(struct cnic_context) *
				cp->max_cid_space, GFP_KERNEL);
	if (!cp->ctx_tbl)
		goto error;

	for (i = 0; i < MAX_ISCSI_TBL_SZ; i++) {
		cp->ctx_tbl[i].proto.iscsi = &cp->iscsi_tbl[i];
		cp->ctx_tbl[i].ulp_proto_id = CNIC_ULP_ISCSI;
	}

	for (i = MAX_ISCSI_TBL_SZ; i < cp->max_cid_space; i++)
		cp->ctx_tbl[i].ulp_proto_id = CNIC_ULP_FCOE;

	pages = CNIC_PAGE_ALIGN(cp->max_cid_space * CNIC_KWQ16_DATA_SIZE) /
		CNIC_PAGE_SIZE;

	ret = cnic_alloc_dma(dev, kwq_16_dma, pages, 0);
	if (ret)
		return -ENOMEM;

	n = CNIC_PAGE_SIZE / CNIC_KWQ16_DATA_SIZE;
	for (i = 0, j = 0; i < cp->max_cid_space; i++) {
		long off = CNIC_KWQ16_DATA_SIZE * (i % n);

		cp->ctx_tbl[i].kwqe_data = kwq_16_dma->pg_arr[j] + off;
		cp->ctx_tbl[i].kwqe_data_mapping = kwq_16_dma->pg_map_arr[j] +
						   off;

		if ((i % n) == (n - 1))
			j++;
	}

	ret = cnic_alloc_kcq(dev, &cp->kcq1, false);
	if (ret)
		goto error;

	if (CNIC_SUPPORTS_FCOE(bp)) {
		ret = cnic_alloc_kcq(dev, &cp->kcq2, true);
		if (ret)
			goto error;
	}

	pages = CNIC_PAGE_ALIGN(BNX2X_ISCSI_GLB_BUF_SIZE) / CNIC_PAGE_SIZE;
	ret = cnic_alloc_dma(dev, &cp->gbl_buf_info, pages, 0);
	if (ret)
		goto error;

	ret = cnic_alloc_bnx2x_context(dev);
	if (ret)
		goto error;

	if (cp->ethdev->drv_state & CNIC_DRV_STATE_NO_ISCSI)
		return 0;

	cp->bnx2x_def_status_blk = cp->ethdev->irq_arr[1].status_blk;

	cp->l2_rx_ring_size = 15;

	ret = cnic_alloc_uio_rings(dev, 4);
	if (ret)
		goto error;

	ret = cnic_init_uio(dev);
	if (ret)
		goto error;

	return 0;

error:
	cnic_free_resc(dev);
	return -ENOMEM;
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
	    !test_bit(CNIC_LCL_FL_KWQ_INIT, &cp->cnic_local_flags)) {
		spin_unlock_bh(&cp->cnic_ulp_lock);
		return -EAGAIN;
	}

	clear_bit(CNIC_LCL_FL_KWQ_INIT, &cp->cnic_local_flags);

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

static void *cnic_get_kwqe_16_data(struct cnic_local *cp, u32 l5_cid,
				   union l5cm_specific_data *l5_data)
{
	struct cnic_context *ctx = &cp->ctx_tbl[l5_cid];
	dma_addr_t map;

	map = ctx->kwqe_data_mapping;
	l5_data->phy_address.lo = (u64) map & 0xffffffff;
	l5_data->phy_address.hi = (u64) map >> 32;
	return ctx->kwqe_data;
}

static int cnic_submit_kwqe_16(struct cnic_dev *dev, u32 cmd, u32 cid,
				u32 type, union l5cm_specific_data *l5_data)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	struct l5cm_spe kwqe;
	struct kwqe_16 *kwq[1];
	u16 type_16;
	int ret;

	kwqe.hdr.conn_and_cmd_data =
		cpu_to_le32(((cmd << SPE_HDR_CMD_ID_SHIFT) |
			     BNX2X_HW_CID(bp, cid)));

	type_16 = (type << SPE_HDR_CONN_TYPE_SHIFT) & SPE_HDR_CONN_TYPE;
	type_16 |= (bp->pfid << SPE_HDR_FUNCTION_ID_SHIFT) &
		   SPE_HDR_FUNCTION_ID;

	kwqe.hdr.type = cpu_to_le16(type_16);
	kwqe.hdr.reserved1 = 0;
	kwqe.data.phy_address.lo = cpu_to_le32(l5_data->phy_address.lo);
	kwqe.data.phy_address.hi = cpu_to_le32(l5_data->phy_address.hi);

	kwq[0] = (struct kwqe_16 *) &kwqe;

	spin_lock_bh(&cp->cnic_ulp_lock);
	ret = cp->ethdev->drv_submit_kwqes_16(dev->netdev, kwq, 1);
	spin_unlock_bh(&cp->cnic_ulp_lock);

	if (ret == 1)
		return 0;

	return ret;
}

static void cnic_reply_bnx2x_kcqes(struct cnic_dev *dev, int ulp_type,
				   struct kcqe *cqes[], u32 num_cqes)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_ulp_ops *ulp_ops;

	rcu_read_lock();
	ulp_ops = rcu_dereference(cp->ulp_ops[ulp_type]);
	if (likely(ulp_ops)) {
		ulp_ops->indicate_kcqes(cp->ulp_handle[ulp_type],
					  cqes, num_cqes);
	}
	rcu_read_unlock();
}

static void cnic_bnx2x_set_tcp_options(struct cnic_dev *dev, int time_stamps,
				       int en_tcp_dack)
{
	struct bnx2x *bp = netdev_priv(dev->netdev);
	u8 xstorm_flags = XSTORM_L5CM_TCP_FLAGS_WND_SCL_EN;
	u16 tstorm_flags = 0;

	if (time_stamps) {
		xstorm_flags |= XSTORM_L5CM_TCP_FLAGS_TS_ENABLED;
		tstorm_flags |= TSTORM_L5CM_TCP_FLAGS_TS_ENABLED;
	}
	if (en_tcp_dack)
		tstorm_flags |= TSTORM_L5CM_TCP_FLAGS_DELAYED_ACK_EN;

	CNIC_WR8(dev, BAR_XSTRORM_INTMEM +
		 XSTORM_ISCSI_TCP_VARS_FLAGS_OFFSET(bp->pfid), xstorm_flags);

	CNIC_WR16(dev, BAR_TSTRORM_INTMEM +
		  TSTORM_ISCSI_TCP_VARS_FLAGS_OFFSET(bp->pfid), tstorm_flags);
}

static int cnic_bnx2x_iscsi_init1(struct cnic_dev *dev, struct kwqe *kwqe)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	struct iscsi_kwqe_init1 *req1 = (struct iscsi_kwqe_init1 *) kwqe;
	int hq_bds, pages;
	u32 pfid = bp->pfid;

	cp->num_iscsi_tasks = req1->num_tasks_per_conn;
	cp->num_ccells = req1->num_ccells_per_conn;
	cp->task_array_size = BNX2X_ISCSI_TASK_CONTEXT_SIZE *
			      cp->num_iscsi_tasks;
	cp->r2tq_size = cp->num_iscsi_tasks * BNX2X_ISCSI_MAX_PENDING_R2TS *
			BNX2X_ISCSI_R2TQE_SIZE;
	cp->hq_size = cp->num_ccells * BNX2X_ISCSI_HQ_BD_SIZE;
	pages = CNIC_PAGE_ALIGN(cp->hq_size) / CNIC_PAGE_SIZE;
	hq_bds = pages * (CNIC_PAGE_SIZE / BNX2X_ISCSI_HQ_BD_SIZE);
	cp->num_cqs = req1->num_cqs;

	if (!dev->max_iscsi_conn)
		return 0;

	/* init Tstorm RAM */
	CNIC_WR16(dev, BAR_TSTRORM_INTMEM + TSTORM_ISCSI_RQ_SIZE_OFFSET(pfid),
		  req1->rq_num_wqes);
	CNIC_WR16(dev, BAR_TSTRORM_INTMEM + TSTORM_ISCSI_PAGE_SIZE_OFFSET(pfid),
		  CNIC_PAGE_SIZE);
	CNIC_WR8(dev, BAR_TSTRORM_INTMEM +
		 TSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfid), CNIC_PAGE_BITS);
	CNIC_WR16(dev, BAR_TSTRORM_INTMEM +
		  TSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfid),
		  req1->num_tasks_per_conn);

	/* init Ustorm RAM */
	CNIC_WR16(dev, BAR_USTRORM_INTMEM +
		  USTORM_ISCSI_RQ_BUFFER_SIZE_OFFSET(pfid),
		  req1->rq_buffer_size);
	CNIC_WR16(dev, BAR_USTRORM_INTMEM + USTORM_ISCSI_PAGE_SIZE_OFFSET(pfid),
		  CNIC_PAGE_SIZE);
	CNIC_WR8(dev, BAR_USTRORM_INTMEM +
		 USTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfid), CNIC_PAGE_BITS);
	CNIC_WR16(dev, BAR_USTRORM_INTMEM +
		  USTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfid),
		  req1->num_tasks_per_conn);
	CNIC_WR16(dev, BAR_USTRORM_INTMEM + USTORM_ISCSI_RQ_SIZE_OFFSET(pfid),
		  req1->rq_num_wqes);
	CNIC_WR16(dev, BAR_USTRORM_INTMEM + USTORM_ISCSI_CQ_SIZE_OFFSET(pfid),
		  req1->cq_num_wqes);
	CNIC_WR16(dev, BAR_USTRORM_INTMEM + USTORM_ISCSI_R2TQ_SIZE_OFFSET(pfid),
		  cp->num_iscsi_tasks * BNX2X_ISCSI_MAX_PENDING_R2TS);

	/* init Xstorm RAM */
	CNIC_WR16(dev, BAR_XSTRORM_INTMEM + XSTORM_ISCSI_PAGE_SIZE_OFFSET(pfid),
		  CNIC_PAGE_SIZE);
	CNIC_WR8(dev, BAR_XSTRORM_INTMEM +
		 XSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfid), CNIC_PAGE_BITS);
	CNIC_WR16(dev, BAR_XSTRORM_INTMEM +
		  XSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfid),
		  req1->num_tasks_per_conn);
	CNIC_WR16(dev, BAR_XSTRORM_INTMEM + XSTORM_ISCSI_HQ_SIZE_OFFSET(pfid),
		  hq_bds);
	CNIC_WR16(dev, BAR_XSTRORM_INTMEM + XSTORM_ISCSI_SQ_SIZE_OFFSET(pfid),
		  req1->num_tasks_per_conn);
	CNIC_WR16(dev, BAR_XSTRORM_INTMEM + XSTORM_ISCSI_R2TQ_SIZE_OFFSET(pfid),
		  cp->num_iscsi_tasks * BNX2X_ISCSI_MAX_PENDING_R2TS);

	/* init Cstorm RAM */
	CNIC_WR16(dev, BAR_CSTRORM_INTMEM + CSTORM_ISCSI_PAGE_SIZE_OFFSET(pfid),
		  CNIC_PAGE_SIZE);
	CNIC_WR8(dev, BAR_CSTRORM_INTMEM +
		 CSTORM_ISCSI_PAGE_SIZE_LOG_OFFSET(pfid), CNIC_PAGE_BITS);
	CNIC_WR16(dev, BAR_CSTRORM_INTMEM +
		  CSTORM_ISCSI_NUM_OF_TASKS_OFFSET(pfid),
		  req1->num_tasks_per_conn);
	CNIC_WR16(dev, BAR_CSTRORM_INTMEM + CSTORM_ISCSI_CQ_SIZE_OFFSET(pfid),
		  req1->cq_num_wqes);
	CNIC_WR16(dev, BAR_CSTRORM_INTMEM + CSTORM_ISCSI_HQ_SIZE_OFFSET(pfid),
		  hq_bds);

	cnic_bnx2x_set_tcp_options(dev,
			req1->flags & ISCSI_KWQE_INIT1_TIME_STAMPS_ENABLE,
			req1->flags & ISCSI_KWQE_INIT1_DELAYED_ACK_ENABLE);

	return 0;
}

static int cnic_bnx2x_iscsi_init2(struct cnic_dev *dev, struct kwqe *kwqe)
{
	struct iscsi_kwqe_init2 *req2 = (struct iscsi_kwqe_init2 *) kwqe;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	u32 pfid = bp->pfid;
	struct iscsi_kcqe kcqe;
	struct kcqe *cqes[1];

	memset(&kcqe, 0, sizeof(kcqe));
	if (!dev->max_iscsi_conn) {
		kcqe.completion_status =
			ISCSI_KCQE_COMPLETION_STATUS_ISCSI_NOT_SUPPORTED;
		goto done;
	}

	CNIC_WR(dev, BAR_TSTRORM_INTMEM +
		TSTORM_ISCSI_ERROR_BITMAP_OFFSET(pfid), req2->error_bit_map[0]);
	CNIC_WR(dev, BAR_TSTRORM_INTMEM +
		TSTORM_ISCSI_ERROR_BITMAP_OFFSET(pfid) + 4,
		req2->error_bit_map[1]);

	CNIC_WR16(dev, BAR_USTRORM_INTMEM +
		  USTORM_ISCSI_CQ_SQN_SIZE_OFFSET(pfid), req2->max_cq_sqn);
	CNIC_WR(dev, BAR_USTRORM_INTMEM +
		USTORM_ISCSI_ERROR_BITMAP_OFFSET(pfid), req2->error_bit_map[0]);
	CNIC_WR(dev, BAR_USTRORM_INTMEM +
		USTORM_ISCSI_ERROR_BITMAP_OFFSET(pfid) + 4,
		req2->error_bit_map[1]);

	CNIC_WR16(dev, BAR_CSTRORM_INTMEM +
		  CSTORM_ISCSI_CQ_SQN_SIZE_OFFSET(pfid), req2->max_cq_sqn);

	kcqe.completion_status = ISCSI_KCQE_COMPLETION_STATUS_SUCCESS;

done:
	kcqe.op_code = ISCSI_KCQE_OPCODE_INIT;
	cqes[0] = (struct kcqe *) &kcqe;
	cnic_reply_bnx2x_kcqes(dev, CNIC_ULP_ISCSI, cqes, 1);

	return 0;
}

static void cnic_free_bnx2x_conn_resc(struct cnic_dev *dev, u32 l5_cid)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_context *ctx = &cp->ctx_tbl[l5_cid];

	if (ctx->ulp_proto_id == CNIC_ULP_ISCSI) {
		struct cnic_iscsi *iscsi = ctx->proto.iscsi;

		cnic_free_dma(dev, &iscsi->hq_info);
		cnic_free_dma(dev, &iscsi->r2tq_info);
		cnic_free_dma(dev, &iscsi->task_array_info);
		cnic_free_id(&cp->cid_tbl, ctx->cid);
	} else {
		cnic_free_id(&cp->fcoe_cid_tbl, ctx->cid);
	}

	ctx->cid = 0;
}

static int cnic_alloc_bnx2x_conn_resc(struct cnic_dev *dev, u32 l5_cid)
{
	u32 cid;
	int ret, pages;
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_context *ctx = &cp->ctx_tbl[l5_cid];
	struct cnic_iscsi *iscsi = ctx->proto.iscsi;

	if (ctx->ulp_proto_id == CNIC_ULP_FCOE) {
		cid = cnic_alloc_new_id(&cp->fcoe_cid_tbl);
		if (cid == -1) {
			ret = -ENOMEM;
			goto error;
		}
		ctx->cid = cid;
		return 0;
	}

	cid = cnic_alloc_new_id(&cp->cid_tbl);
	if (cid == -1) {
		ret = -ENOMEM;
		goto error;
	}

	ctx->cid = cid;
	pages = CNIC_PAGE_ALIGN(cp->task_array_size) / CNIC_PAGE_SIZE;

	ret = cnic_alloc_dma(dev, &iscsi->task_array_info, pages, 1);
	if (ret)
		goto error;

	pages = CNIC_PAGE_ALIGN(cp->r2tq_size) / CNIC_PAGE_SIZE;
	ret = cnic_alloc_dma(dev, &iscsi->r2tq_info, pages, 1);
	if (ret)
		goto error;

	pages = CNIC_PAGE_ALIGN(cp->hq_size) / CNIC_PAGE_SIZE;
	ret = cnic_alloc_dma(dev, &iscsi->hq_info, pages, 1);
	if (ret)
		goto error;

	return 0;

error:
	cnic_free_bnx2x_conn_resc(dev, l5_cid);
	return ret;
}

static void *cnic_get_bnx2x_ctx(struct cnic_dev *dev, u32 cid, int init,
				struct regpair *ctx_addr)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	int blk = (cid - ethdev->starting_cid) / cp->cids_per_blk;
	int off = (cid - ethdev->starting_cid) % cp->cids_per_blk;
	unsigned long align_off = 0;
	dma_addr_t ctx_map;
	void *ctx;

	if (cp->ctx_align) {
		unsigned long mask = cp->ctx_align - 1;

		if (cp->ctx_arr[blk].mapping & mask)
			align_off = cp->ctx_align -
				    (cp->ctx_arr[blk].mapping & mask);
	}
	ctx_map = cp->ctx_arr[blk].mapping + align_off +
		(off * BNX2X_CONTEXT_MEM_SIZE);
	ctx = cp->ctx_arr[blk].ctx + align_off +
	      (off * BNX2X_CONTEXT_MEM_SIZE);
	if (init)
		memset(ctx, 0, BNX2X_CONTEXT_MEM_SIZE);

	ctx_addr->lo = ctx_map & 0xffffffff;
	ctx_addr->hi = (u64) ctx_map >> 32;
	return ctx;
}

static int cnic_setup_bnx2x_ctx(struct cnic_dev *dev, struct kwqe *wqes[],
				u32 num)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	struct iscsi_kwqe_conn_offload1 *req1 =
			(struct iscsi_kwqe_conn_offload1 *) wqes[0];
	struct iscsi_kwqe_conn_offload2 *req2 =
			(struct iscsi_kwqe_conn_offload2 *) wqes[1];
	struct iscsi_kwqe_conn_offload3 *req3;
	struct cnic_context *ctx = &cp->ctx_tbl[req1->iscsi_conn_id];
	struct cnic_iscsi *iscsi = ctx->proto.iscsi;
	u32 cid = ctx->cid;
	u32 hw_cid = BNX2X_HW_CID(bp, cid);
	struct iscsi_context *ictx;
	struct regpair context_addr;
	int i, j, n = 2, n_max;
	u8 port = BP_PORT(bp);

	ctx->ctx_flags = 0;
	if (!req2->num_additional_wqes)
		return -EINVAL;

	n_max = req2->num_additional_wqes + 2;

	ictx = cnic_get_bnx2x_ctx(dev, cid, 1, &context_addr);
	if (ictx == NULL)
		return -ENOMEM;

	req3 = (struct iscsi_kwqe_conn_offload3 *) wqes[n++];

	ictx->xstorm_ag_context.hq_prod = 1;

	ictx->xstorm_st_context.iscsi.first_burst_length =
		ISCSI_DEF_FIRST_BURST_LEN;
	ictx->xstorm_st_context.iscsi.max_send_pdu_length =
		ISCSI_DEF_MAX_RECV_SEG_LEN;
	ictx->xstorm_st_context.iscsi.sq_pbl_base.lo =
		req1->sq_page_table_addr_lo;
	ictx->xstorm_st_context.iscsi.sq_pbl_base.hi =
		req1->sq_page_table_addr_hi;
	ictx->xstorm_st_context.iscsi.sq_curr_pbe.lo = req2->sq_first_pte.hi;
	ictx->xstorm_st_context.iscsi.sq_curr_pbe.hi = req2->sq_first_pte.lo;
	ictx->xstorm_st_context.iscsi.hq_pbl_base.lo =
		iscsi->hq_info.pgtbl_map & 0xffffffff;
	ictx->xstorm_st_context.iscsi.hq_pbl_base.hi =
		(u64) iscsi->hq_info.pgtbl_map >> 32;
	ictx->xstorm_st_context.iscsi.hq_curr_pbe_base.lo =
		iscsi->hq_info.pgtbl[0];
	ictx->xstorm_st_context.iscsi.hq_curr_pbe_base.hi =
		iscsi->hq_info.pgtbl[1];
	ictx->xstorm_st_context.iscsi.r2tq_pbl_base.lo =
		iscsi->r2tq_info.pgtbl_map & 0xffffffff;
	ictx->xstorm_st_context.iscsi.r2tq_pbl_base.hi =
		(u64) iscsi->r2tq_info.pgtbl_map >> 32;
	ictx->xstorm_st_context.iscsi.r2tq_curr_pbe_base.lo =
		iscsi->r2tq_info.pgtbl[0];
	ictx->xstorm_st_context.iscsi.r2tq_curr_pbe_base.hi =
		iscsi->r2tq_info.pgtbl[1];
	ictx->xstorm_st_context.iscsi.task_pbl_base.lo =
		iscsi->task_array_info.pgtbl_map & 0xffffffff;
	ictx->xstorm_st_context.iscsi.task_pbl_base.hi =
		(u64) iscsi->task_array_info.pgtbl_map >> 32;
	ictx->xstorm_st_context.iscsi.task_pbl_cache_idx =
		BNX2X_ISCSI_PBL_NOT_CACHED;
	ictx->xstorm_st_context.iscsi.flags.flags |=
		XSTORM_ISCSI_CONTEXT_FLAGS_B_IMMEDIATE_DATA;
	ictx->xstorm_st_context.iscsi.flags.flags |=
		XSTORM_ISCSI_CONTEXT_FLAGS_B_INITIAL_R2T;
	ictx->xstorm_st_context.common.ethernet.reserved_vlan_type =
		ETH_P_8021Q;
	if (BNX2X_CHIP_IS_E2_PLUS(bp) &&
	    bp->common.chip_port_mode == CHIP_2_PORT_MODE) {

		port = 0;
	}
	ictx->xstorm_st_context.common.flags =
		1 << XSTORM_COMMON_CONTEXT_SECTION_PHYSQ_INITIALIZED_SHIFT;
	ictx->xstorm_st_context.common.flags =
		port << XSTORM_COMMON_CONTEXT_SECTION_PBF_PORT_SHIFT;

	ictx->tstorm_st_context.iscsi.hdr_bytes_2_fetch = ISCSI_HEADER_SIZE;
	/* TSTORM requires the base address of RQ DB & not PTE */
	ictx->tstorm_st_context.iscsi.rq_db_phy_addr.lo =
		req2->rq_page_table_addr_lo & CNIC_PAGE_MASK;
	ictx->tstorm_st_context.iscsi.rq_db_phy_addr.hi =
		req2->rq_page_table_addr_hi;
	ictx->tstorm_st_context.iscsi.iscsi_conn_id = req1->iscsi_conn_id;
	ictx->tstorm_st_context.tcp.cwnd = 0x5A8;
	ictx->tstorm_st_context.tcp.flags2 |=
		TSTORM_TCP_ST_CONTEXT_SECTION_DA_EN;
	ictx->tstorm_st_context.tcp.ooo_support_mode =
		TCP_TSTORM_OOO_DROP_AND_PROC_ACK;

	ictx->timers_context.flags |= TIMERS_BLOCK_CONTEXT_CONN_VALID_FLG;

	ictx->ustorm_st_context.ring.rq.pbl_base.lo =
		req2->rq_page_table_addr_lo;
	ictx->ustorm_st_context.ring.rq.pbl_base.hi =
		req2->rq_page_table_addr_hi;
	ictx->ustorm_st_context.ring.rq.curr_pbe.lo = req3->qp_first_pte[0].hi;
	ictx->ustorm_st_context.ring.rq.curr_pbe.hi = req3->qp_first_pte[0].lo;
	ictx->ustorm_st_context.ring.r2tq.pbl_base.lo =
		iscsi->r2tq_info.pgtbl_map & 0xffffffff;
	ictx->ustorm_st_context.ring.r2tq.pbl_base.hi =
		(u64) iscsi->r2tq_info.pgtbl_map >> 32;
	ictx->ustorm_st_context.ring.r2tq.curr_pbe.lo =
		iscsi->r2tq_info.pgtbl[0];
	ictx->ustorm_st_context.ring.r2tq.curr_pbe.hi =
		iscsi->r2tq_info.pgtbl[1];
	ictx->ustorm_st_context.ring.cq_pbl_base.lo =
		req1->cq_page_table_addr_lo;
	ictx->ustorm_st_context.ring.cq_pbl_base.hi =
		req1->cq_page_table_addr_hi;
	ictx->ustorm_st_context.ring.cq[0].cq_sn = ISCSI_INITIAL_SN;
	ictx->ustorm_st_context.ring.cq[0].curr_pbe.lo = req2->cq_first_pte.hi;
	ictx->ustorm_st_context.ring.cq[0].curr_pbe.hi = req2->cq_first_pte.lo;
	ictx->ustorm_st_context.task_pbe_cache_index =
		BNX2X_ISCSI_PBL_NOT_CACHED;
	ictx->ustorm_st_context.task_pdu_cache_index =
		BNX2X_ISCSI_PDU_HEADER_NOT_CACHED;

	for (i = 1, j = 1; i < cp->num_cqs; i++, j++) {
		if (j == 3) {
			if (n >= n_max)
				break;
			req3 = (struct iscsi_kwqe_conn_offload3 *) wqes[n++];
			j = 0;
		}
		ictx->ustorm_st_context.ring.cq[i].cq_sn = ISCSI_INITIAL_SN;
		ictx->ustorm_st_context.ring.cq[i].curr_pbe.lo =
			req3->qp_first_pte[j].hi;
		ictx->ustorm_st_context.ring.cq[i].curr_pbe.hi =
			req3->qp_first_pte[j].lo;
	}

	ictx->ustorm_st_context.task_pbl_base.lo =
		iscsi->task_array_info.pgtbl_map & 0xffffffff;
	ictx->ustorm_st_context.task_pbl_base.hi =
		(u64) iscsi->task_array_info.pgtbl_map >> 32;
	ictx->ustorm_st_context.tce_phy_addr.lo =
		iscsi->task_array_info.pgtbl[0];
	ictx->ustorm_st_context.tce_phy_addr.hi =
		iscsi->task_array_info.pgtbl[1];
	ictx->ustorm_st_context.iscsi_conn_id = req1->iscsi_conn_id;
	ictx->ustorm_st_context.num_cqs = cp->num_cqs;
	ictx->ustorm_st_context.negotiated_rx |= ISCSI_DEF_MAX_RECV_SEG_LEN;
	ictx->ustorm_st_context.negotiated_rx_and_flags |=
		ISCSI_DEF_MAX_BURST_LEN;
	ictx->ustorm_st_context.negotiated_rx |=
		ISCSI_DEFAULT_MAX_OUTSTANDING_R2T <<
		USTORM_ISCSI_ST_CONTEXT_MAX_OUTSTANDING_R2TS_SHIFT;

	ictx->cstorm_st_context.hq_pbl_base.lo =
		iscsi->hq_info.pgtbl_map & 0xffffffff;
	ictx->cstorm_st_context.hq_pbl_base.hi =
		(u64) iscsi->hq_info.pgtbl_map >> 32;
	ictx->cstorm_st_context.hq_curr_pbe.lo = iscsi->hq_info.pgtbl[0];
	ictx->cstorm_st_context.hq_curr_pbe.hi = iscsi->hq_info.pgtbl[1];
	ictx->cstorm_st_context.task_pbl_base.lo =
		iscsi->task_array_info.pgtbl_map & 0xffffffff;
	ictx->cstorm_st_context.task_pbl_base.hi =
		(u64) iscsi->task_array_info.pgtbl_map >> 32;
	/* CSTORM and USTORM initialization is different, CSTORM requires
	 * CQ DB base & not PTE addr */
	ictx->cstorm_st_context.cq_db_base.lo =
		req1->cq_page_table_addr_lo & CNIC_PAGE_MASK;
	ictx->cstorm_st_context.cq_db_base.hi = req1->cq_page_table_addr_hi;
	ictx->cstorm_st_context.iscsi_conn_id = req1->iscsi_conn_id;
	ictx->cstorm_st_context.cq_proc_en_bit_map = (1 << cp->num_cqs) - 1;
	for (i = 0; i < cp->num_cqs; i++) {
		ictx->cstorm_st_context.cq_c_prod_sqn_arr.sqn[i] =
			ISCSI_INITIAL_SN;
		ictx->cstorm_st_context.cq_c_sqn_2_notify_arr.sqn[i] =
			ISCSI_INITIAL_SN;
	}

	ictx->xstorm_ag_context.cdu_reserved =
		CDU_RSRVD_VALUE_TYPE_A(hw_cid, CDU_REGION_NUMBER_XCM_AG,
				       ISCSI_CONNECTION_TYPE);
	ictx->ustorm_ag_context.cdu_usage =
		CDU_RSRVD_VALUE_TYPE_A(hw_cid, CDU_REGION_NUMBER_UCM_AG,
				       ISCSI_CONNECTION_TYPE);
	return 0;

}

static int cnic_bnx2x_iscsi_ofld1(struct cnic_dev *dev, struct kwqe *wqes[],
				   u32 num, int *work)
{
	struct iscsi_kwqe_conn_offload1 *req1;
	struct iscsi_kwqe_conn_offload2 *req2;
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	struct cnic_context *ctx;
	struct iscsi_kcqe kcqe;
	struct kcqe *cqes[1];
	u32 l5_cid;
	int ret = 0;

	if (num < 2) {
		*work = num;
		return -EINVAL;
	}

	req1 = (struct iscsi_kwqe_conn_offload1 *) wqes[0];
	req2 = (struct iscsi_kwqe_conn_offload2 *) wqes[1];
	if ((num - 2) < req2->num_additional_wqes) {
		*work = num;
		return -EINVAL;
	}
	*work = 2 + req2->num_additional_wqes;

	l5_cid = req1->iscsi_conn_id;
	if (l5_cid >= MAX_ISCSI_TBL_SZ)
		return -EINVAL;

	memset(&kcqe, 0, sizeof(kcqe));
	kcqe.op_code = ISCSI_KCQE_OPCODE_OFFLOAD_CONN;
	kcqe.iscsi_conn_id = l5_cid;
	kcqe.completion_status = ISCSI_KCQE_COMPLETION_STATUS_CTX_ALLOC_FAILURE;

	ctx = &cp->ctx_tbl[l5_cid];
	if (test_bit(CTX_FL_OFFLD_START, &ctx->ctx_flags)) {
		kcqe.completion_status =
			ISCSI_KCQE_COMPLETION_STATUS_CID_BUSY;
		goto done;
	}

	if (atomic_inc_return(&cp->iscsi_conn) > dev->max_iscsi_conn) {
		atomic_dec(&cp->iscsi_conn);
		goto done;
	}
	ret = cnic_alloc_bnx2x_conn_resc(dev, l5_cid);
	if (ret) {
		atomic_dec(&cp->iscsi_conn);
		ret = 0;
		goto done;
	}
	ret = cnic_setup_bnx2x_ctx(dev, wqes, num);
	if (ret < 0) {
		cnic_free_bnx2x_conn_resc(dev, l5_cid);
		atomic_dec(&cp->iscsi_conn);
		goto done;
	}

	kcqe.completion_status = ISCSI_KCQE_COMPLETION_STATUS_SUCCESS;
	kcqe.iscsi_conn_context_id = BNX2X_HW_CID(bp, cp->ctx_tbl[l5_cid].cid);

done:
	cqes[0] = (struct kcqe *) &kcqe;
	cnic_reply_bnx2x_kcqes(dev, CNIC_ULP_ISCSI, cqes, 1);
	return 0;
}


static int cnic_bnx2x_iscsi_update(struct cnic_dev *dev, struct kwqe *kwqe)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct iscsi_kwqe_conn_update *req =
		(struct iscsi_kwqe_conn_update *) kwqe;
	void *data;
	union l5cm_specific_data l5_data;
	u32 l5_cid, cid = BNX2X_SW_CID(req->context_id);
	int ret;

	if (cnic_get_l5_cid(cp, cid, &l5_cid) != 0)
		return -EINVAL;

	data = cnic_get_kwqe_16_data(cp, l5_cid, &l5_data);
	if (!data)
		return -ENOMEM;

	memcpy(data, kwqe, sizeof(struct kwqe));

	ret = cnic_submit_kwqe_16(dev, ISCSI_RAMROD_CMD_ID_UPDATE_CONN,
			req->context_id, ISCSI_CONNECTION_TYPE, &l5_data);
	return ret;
}

static int cnic_bnx2x_destroy_ramrod(struct cnic_dev *dev, u32 l5_cid)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	struct cnic_context *ctx = &cp->ctx_tbl[l5_cid];
	union l5cm_specific_data l5_data;
	int ret;
	u32 hw_cid;

	init_waitqueue_head(&ctx->waitq);
	ctx->wait_cond = 0;
	memset(&l5_data, 0, sizeof(l5_data));
	hw_cid = BNX2X_HW_CID(bp, ctx->cid);

	ret = cnic_submit_kwqe_16(dev, RAMROD_CMD_ID_COMMON_CFC_DEL,
				  hw_cid, NONE_CONNECTION_TYPE, &l5_data);

	if (ret == 0) {
		wait_event_timeout(ctx->waitq, ctx->wait_cond, CNIC_RAMROD_TMO);
		if (unlikely(test_bit(CTX_FL_CID_ERROR, &ctx->ctx_flags)))
			return -EBUSY;
	}

	return 0;
}

static int cnic_bnx2x_iscsi_destroy(struct cnic_dev *dev, struct kwqe *kwqe)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct iscsi_kwqe_conn_destroy *req =
		(struct iscsi_kwqe_conn_destroy *) kwqe;
	u32 l5_cid = req->reserved0;
	struct cnic_context *ctx = &cp->ctx_tbl[l5_cid];
	int ret = 0;
	struct iscsi_kcqe kcqe;
	struct kcqe *cqes[1];

	if (!test_bit(CTX_FL_OFFLD_START, &ctx->ctx_flags))
		goto skip_cfc_delete;

	if (!time_after(jiffies, ctx->timestamp + (2 * HZ))) {
		unsigned long delta = ctx->timestamp + (2 * HZ) - jiffies;

		if (delta > (2 * HZ))
			delta = 0;

		set_bit(CTX_FL_DELETE_WAIT, &ctx->ctx_flags);
		queue_delayed_work(cnic_wq, &cp->delete_task, delta);
		goto destroy_reply;
	}

	ret = cnic_bnx2x_destroy_ramrod(dev, l5_cid);

skip_cfc_delete:
	cnic_free_bnx2x_conn_resc(dev, l5_cid);

	if (!ret) {
		atomic_dec(&cp->iscsi_conn);
		clear_bit(CTX_FL_OFFLD_START, &ctx->ctx_flags);
	}

destroy_reply:
	memset(&kcqe, 0, sizeof(kcqe));
	kcqe.op_code = ISCSI_KCQE_OPCODE_DESTROY_CONN;
	kcqe.iscsi_conn_id = l5_cid;
	kcqe.completion_status = ISCSI_KCQE_COMPLETION_STATUS_SUCCESS;
	kcqe.iscsi_conn_context_id = req->context_id;

	cqes[0] = (struct kcqe *) &kcqe;
	cnic_reply_bnx2x_kcqes(dev, CNIC_ULP_ISCSI, cqes, 1);

	return 0;
}

static void cnic_init_storm_conn_bufs(struct cnic_dev *dev,
				      struct l4_kwq_connect_req1 *kwqe1,
				      struct l4_kwq_connect_req3 *kwqe3,
				      struct l5cm_active_conn_buffer *conn_buf)
{
	struct l5cm_conn_addr_params *conn_addr = &conn_buf->conn_addr_buf;
	struct l5cm_xstorm_conn_buffer *xstorm_buf =
		&conn_buf->xstorm_conn_buffer;
	struct l5cm_tstorm_conn_buffer *tstorm_buf =
		&conn_buf->tstorm_conn_buffer;
	struct regpair context_addr;
	u32 cid = BNX2X_SW_CID(kwqe1->cid);
	struct in6_addr src_ip, dst_ip;
	int i;
	u32 *addrp;

	addrp = (u32 *) &conn_addr->local_ip_addr;
	for (i = 0; i < 4; i++, addrp++)
		src_ip.in6_u.u6_addr32[i] = cpu_to_be32(*addrp);

	addrp = (u32 *) &conn_addr->remote_ip_addr;
	for (i = 0; i < 4; i++, addrp++)
		dst_ip.in6_u.u6_addr32[i] = cpu_to_be32(*addrp);

	cnic_get_bnx2x_ctx(dev, cid, 0, &context_addr);

	xstorm_buf->context_addr.hi = context_addr.hi;
	xstorm_buf->context_addr.lo = context_addr.lo;
	xstorm_buf->mss = 0xffff;
	xstorm_buf->rcv_buf = kwqe3->rcv_buf;
	if (kwqe1->tcp_flags & L4_KWQ_CONNECT_REQ1_NAGLE_ENABLE)
		xstorm_buf->params |= L5CM_XSTORM_CONN_BUFFER_NAGLE_ENABLE;
	xstorm_buf->pseudo_header_checksum =
		swab16(~csum_ipv6_magic(&src_ip, &dst_ip, 0, IPPROTO_TCP, 0));

	if (kwqe3->ka_timeout) {
		tstorm_buf->ka_enable = 1;
		tstorm_buf->ka_timeout = kwqe3->ka_timeout;
		tstorm_buf->ka_interval = kwqe3->ka_interval;
		tstorm_buf->ka_max_probe_count = kwqe3->ka_max_probe_count;
	}
	tstorm_buf->max_rt_time = 0xffffffff;
}

static void cnic_init_bnx2x_mac(struct cnic_dev *dev)
{
	struct bnx2x *bp = netdev_priv(dev->netdev);
	u32 pfid = bp->pfid;
	u8 *mac = dev->mac_addr;

	CNIC_WR8(dev, BAR_XSTRORM_INTMEM +
		 XSTORM_ISCSI_LOCAL_MAC_ADDR0_OFFSET(pfid), mac[0]);
	CNIC_WR8(dev, BAR_XSTRORM_INTMEM +
		 XSTORM_ISCSI_LOCAL_MAC_ADDR1_OFFSET(pfid), mac[1]);
	CNIC_WR8(dev, BAR_XSTRORM_INTMEM +
		 XSTORM_ISCSI_LOCAL_MAC_ADDR2_OFFSET(pfid), mac[2]);
	CNIC_WR8(dev, BAR_XSTRORM_INTMEM +
		 XSTORM_ISCSI_LOCAL_MAC_ADDR3_OFFSET(pfid), mac[3]);
	CNIC_WR8(dev, BAR_XSTRORM_INTMEM +
		 XSTORM_ISCSI_LOCAL_MAC_ADDR4_OFFSET(pfid), mac[4]);
	CNIC_WR8(dev, BAR_XSTRORM_INTMEM +
		 XSTORM_ISCSI_LOCAL_MAC_ADDR5_OFFSET(pfid), mac[5]);

	CNIC_WR8(dev, BAR_TSTRORM_INTMEM +
		 TSTORM_ISCSI_TCP_VARS_LSB_LOCAL_MAC_ADDR_OFFSET(pfid), mac[5]);
	CNIC_WR8(dev, BAR_TSTRORM_INTMEM +
		 TSTORM_ISCSI_TCP_VARS_LSB_LOCAL_MAC_ADDR_OFFSET(pfid) + 1,
		 mac[4]);
	CNIC_WR8(dev, BAR_TSTRORM_INTMEM +
		 TSTORM_ISCSI_TCP_VARS_MID_LOCAL_MAC_ADDR_OFFSET(pfid), mac[3]);
	CNIC_WR8(dev, BAR_TSTRORM_INTMEM +
		 TSTORM_ISCSI_TCP_VARS_MID_LOCAL_MAC_ADDR_OFFSET(pfid) + 1,
		 mac[2]);
	CNIC_WR8(dev, BAR_TSTRORM_INTMEM +
		 TSTORM_ISCSI_TCP_VARS_MSB_LOCAL_MAC_ADDR_OFFSET(pfid), mac[1]);
	CNIC_WR8(dev, BAR_TSTRORM_INTMEM +
		 TSTORM_ISCSI_TCP_VARS_MSB_LOCAL_MAC_ADDR_OFFSET(pfid) + 1,
		 mac[0]);
}

static int cnic_bnx2x_connect(struct cnic_dev *dev, struct kwqe *wqes[],
			      u32 num, int *work)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	struct l4_kwq_connect_req1 *kwqe1 =
		(struct l4_kwq_connect_req1 *) wqes[0];
	struct l4_kwq_connect_req3 *kwqe3;
	struct l5cm_active_conn_buffer *conn_buf;
	struct l5cm_conn_addr_params *conn_addr;
	union l5cm_specific_data l5_data;
	u32 l5_cid = kwqe1->pg_cid;
	struct cnic_sock *csk = &cp->csk_tbl[l5_cid];
	struct cnic_context *ctx = &cp->ctx_tbl[l5_cid];
	int ret;

	if (num < 2) {
		*work = num;
		return -EINVAL;
	}

	if (kwqe1->conn_flags & L4_KWQ_CONNECT_REQ1_IP_V6)
		*work = 3;
	else
		*work = 2;

	if (num < *work) {
		*work = num;
		return -EINVAL;
	}

	if (sizeof(*conn_buf) > CNIC_KWQ16_DATA_SIZE) {
		netdev_err(dev->netdev, "conn_buf size too big\n");
		return -ENOMEM;
	}
	conn_buf = cnic_get_kwqe_16_data(cp, l5_cid, &l5_data);
	if (!conn_buf)
		return -ENOMEM;

	memset(conn_buf, 0, sizeof(*conn_buf));

	conn_addr = &conn_buf->conn_addr_buf;
	conn_addr->remote_addr_0 = csk->ha[0];
	conn_addr->remote_addr_1 = csk->ha[1];
	conn_addr->remote_addr_2 = csk->ha[2];
	conn_addr->remote_addr_3 = csk->ha[3];
	conn_addr->remote_addr_4 = csk->ha[4];
	conn_addr->remote_addr_5 = csk->ha[5];

	if (kwqe1->conn_flags & L4_KWQ_CONNECT_REQ1_IP_V6) {
		struct l4_kwq_connect_req2 *kwqe2 =
			(struct l4_kwq_connect_req2 *) wqes[1];

		conn_addr->local_ip_addr.ip_addr_hi_hi = kwqe2->src_ip_v6_4;
		conn_addr->local_ip_addr.ip_addr_hi_lo = kwqe2->src_ip_v6_3;
		conn_addr->local_ip_addr.ip_addr_lo_hi = kwqe2->src_ip_v6_2;

		conn_addr->remote_ip_addr.ip_addr_hi_hi = kwqe2->dst_ip_v6_4;
		conn_addr->remote_ip_addr.ip_addr_hi_lo = kwqe2->dst_ip_v6_3;
		conn_addr->remote_ip_addr.ip_addr_lo_hi = kwqe2->dst_ip_v6_2;
		conn_addr->params |= L5CM_CONN_ADDR_PARAMS_IP_VERSION;
	}
	kwqe3 = (struct l4_kwq_connect_req3 *) wqes[*work - 1];

	conn_addr->local_ip_addr.ip_addr_lo_lo = kwqe1->src_ip;
	conn_addr->remote_ip_addr.ip_addr_lo_lo = kwqe1->dst_ip;
	conn_addr->local_tcp_port = kwqe1->src_port;
	conn_addr->remote_tcp_port = kwqe1->dst_port;

	conn_addr->pmtu = kwqe3->pmtu;
	cnic_init_storm_conn_bufs(dev, kwqe1, kwqe3, conn_buf);

	CNIC_WR16(dev, BAR_XSTRORM_INTMEM +
		  XSTORM_ISCSI_LOCAL_VLAN_OFFSET(bp->pfid), csk->vlan_id);

	ret = cnic_submit_kwqe_16(dev, L5CM_RAMROD_CMD_ID_TCP_CONNECT,
			kwqe1->cid, ISCSI_CONNECTION_TYPE, &l5_data);
	if (!ret)
		set_bit(CTX_FL_OFFLD_START, &ctx->ctx_flags);

	return ret;
}

static int cnic_bnx2x_close(struct cnic_dev *dev, struct kwqe *kwqe)
{
	struct l4_kwq_close_req *req = (struct l4_kwq_close_req *) kwqe;
	union l5cm_specific_data l5_data;
	int ret;

	memset(&l5_data, 0, sizeof(l5_data));
	ret = cnic_submit_kwqe_16(dev, L5CM_RAMROD_CMD_ID_CLOSE,
			req->cid, ISCSI_CONNECTION_TYPE, &l5_data);
	return ret;
}

static int cnic_bnx2x_reset(struct cnic_dev *dev, struct kwqe *kwqe)
{
	struct l4_kwq_reset_req *req = (struct l4_kwq_reset_req *) kwqe;
	union l5cm_specific_data l5_data;
	int ret;

	memset(&l5_data, 0, sizeof(l5_data));
	ret = cnic_submit_kwqe_16(dev, L5CM_RAMROD_CMD_ID_ABORT,
			req->cid, ISCSI_CONNECTION_TYPE, &l5_data);
	return ret;
}
static int cnic_bnx2x_offload_pg(struct cnic_dev *dev, struct kwqe *kwqe)
{
	struct l4_kwq_offload_pg *req = (struct l4_kwq_offload_pg *) kwqe;
	struct l4_kcq kcqe;
	struct kcqe *cqes[1];

	memset(&kcqe, 0, sizeof(kcqe));
	kcqe.pg_host_opaque = req->host_opaque;
	kcqe.pg_cid = req->host_opaque;
	kcqe.op_code = L4_KCQE_OPCODE_VALUE_OFFLOAD_PG;
	cqes[0] = (struct kcqe *) &kcqe;
	cnic_reply_bnx2x_kcqes(dev, CNIC_ULP_L4, cqes, 1);
	return 0;
}

static int cnic_bnx2x_update_pg(struct cnic_dev *dev, struct kwqe *kwqe)
{
	struct l4_kwq_update_pg *req = (struct l4_kwq_update_pg *) kwqe;
	struct l4_kcq kcqe;
	struct kcqe *cqes[1];

	memset(&kcqe, 0, sizeof(kcqe));
	kcqe.pg_host_opaque = req->pg_host_opaque;
	kcqe.pg_cid = req->pg_cid;
	kcqe.op_code = L4_KCQE_OPCODE_VALUE_UPDATE_PG;
	cqes[0] = (struct kcqe *) &kcqe;
	cnic_reply_bnx2x_kcqes(dev, CNIC_ULP_L4, cqes, 1);
	return 0;
}

static int cnic_bnx2x_fcoe_stat(struct cnic_dev *dev, struct kwqe *kwqe)
{
	struct fcoe_kwqe_stat *req;
	struct fcoe_stat_ramrod_params *fcoe_stat;
	union l5cm_specific_data l5_data;
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	int ret;
	u32 cid;

	req = (struct fcoe_kwqe_stat *) kwqe;
	cid = BNX2X_HW_CID(bp, cp->fcoe_init_cid);

	fcoe_stat = cnic_get_kwqe_16_data(cp, BNX2X_FCOE_L5_CID_BASE, &l5_data);
	if (!fcoe_stat)
		return -ENOMEM;

	memset(fcoe_stat, 0, sizeof(*fcoe_stat));
	memcpy(&fcoe_stat->stat_kwqe, req, sizeof(*req));

	ret = cnic_submit_kwqe_16(dev, FCOE_RAMROD_CMD_ID_STAT_FUNC, cid,
				  FCOE_CONNECTION_TYPE, &l5_data);
	return ret;
}

static int cnic_bnx2x_fcoe_init1(struct cnic_dev *dev, struct kwqe *wqes[],
				 u32 num, int *work)
{
	int ret;
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	u32 cid;
	struct fcoe_init_ramrod_params *fcoe_init;
	struct fcoe_kwqe_init1 *req1;
	struct fcoe_kwqe_init2 *req2;
	struct fcoe_kwqe_init3 *req3;
	union l5cm_specific_data l5_data;

	if (num < 3) {
		*work = num;
		return -EINVAL;
	}
	req1 = (struct fcoe_kwqe_init1 *) wqes[0];
	req2 = (struct fcoe_kwqe_init2 *) wqes[1];
	req3 = (struct fcoe_kwqe_init3 *) wqes[2];
	if (req2->hdr.op_code != FCOE_KWQE_OPCODE_INIT2) {
		*work = 1;
		return -EINVAL;
	}
	if (req3->hdr.op_code != FCOE_KWQE_OPCODE_INIT3) {
		*work = 2;
		return -EINVAL;
	}

	if (sizeof(*fcoe_init) > CNIC_KWQ16_DATA_SIZE) {
		netdev_err(dev->netdev, "fcoe_init size too big\n");
		return -ENOMEM;
	}
	fcoe_init = cnic_get_kwqe_16_data(cp, BNX2X_FCOE_L5_CID_BASE, &l5_data);
	if (!fcoe_init)
		return -ENOMEM;

	memset(fcoe_init, 0, sizeof(*fcoe_init));
	memcpy(&fcoe_init->init_kwqe1, req1, sizeof(*req1));
	memcpy(&fcoe_init->init_kwqe2, req2, sizeof(*req2));
	memcpy(&fcoe_init->init_kwqe3, req3, sizeof(*req3));
	fcoe_init->eq_pbl_base.lo = cp->kcq2.dma.pgtbl_map & 0xffffffff;
	fcoe_init->eq_pbl_base.hi = (u64) cp->kcq2.dma.pgtbl_map >> 32;
	fcoe_init->eq_pbl_size = cp->kcq2.dma.num_pages;

	fcoe_init->sb_num = cp->status_blk_num;
	fcoe_init->eq_prod = MAX_KCQ_IDX;
	fcoe_init->sb_id = HC_INDEX_FCOE_EQ_CONS;
	cp->kcq2.sw_prod_idx = 0;

	cid = BNX2X_HW_CID(bp, cp->fcoe_init_cid);
	ret = cnic_submit_kwqe_16(dev, FCOE_RAMROD_CMD_ID_INIT_FUNC, cid,
				  FCOE_CONNECTION_TYPE, &l5_data);
	*work = 3;
	return ret;
}

static int cnic_bnx2x_fcoe_ofld1(struct cnic_dev *dev, struct kwqe *wqes[],
				 u32 num, int *work)
{
	int ret = 0;
	u32 cid = -1, l5_cid;
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	struct fcoe_kwqe_conn_offload1 *req1;
	struct fcoe_kwqe_conn_offload2 *req2;
	struct fcoe_kwqe_conn_offload3 *req3;
	struct fcoe_kwqe_conn_offload4 *req4;
	struct fcoe_conn_offload_ramrod_params *fcoe_offload;
	struct cnic_context *ctx;
	struct fcoe_context *fctx;
	struct regpair ctx_addr;
	union l5cm_specific_data l5_data;
	struct fcoe_kcqe kcqe;
	struct kcqe *cqes[1];

	if (num < 4) {
		*work = num;
		return -EINVAL;
	}
	req1 = (struct fcoe_kwqe_conn_offload1 *) wqes[0];
	req2 = (struct fcoe_kwqe_conn_offload2 *) wqes[1];
	req3 = (struct fcoe_kwqe_conn_offload3 *) wqes[2];
	req4 = (struct fcoe_kwqe_conn_offload4 *) wqes[3];

	*work = 4;

	l5_cid = req1->fcoe_conn_id;
	if (l5_cid >= dev->max_fcoe_conn)
		goto err_reply;

	l5_cid += BNX2X_FCOE_L5_CID_BASE;

	ctx = &cp->ctx_tbl[l5_cid];
	if (test_bit(CTX_FL_OFFLD_START, &ctx->ctx_flags))
		goto err_reply;

	ret = cnic_alloc_bnx2x_conn_resc(dev, l5_cid);
	if (ret) {
		ret = 0;
		goto err_reply;
	}
	cid = ctx->cid;

	fctx = cnic_get_bnx2x_ctx(dev, cid, 1, &ctx_addr);
	if (fctx) {
		u32 hw_cid = BNX2X_HW_CID(bp, cid);
		u32 val;

		val = CDU_RSRVD_VALUE_TYPE_A(hw_cid, CDU_REGION_NUMBER_XCM_AG,
					     FCOE_CONNECTION_TYPE);
		fctx->xstorm_ag_context.cdu_reserved = val;
		val = CDU_RSRVD_VALUE_TYPE_A(hw_cid, CDU_REGION_NUMBER_UCM_AG,
					     FCOE_CONNECTION_TYPE);
		fctx->ustorm_ag_context.cdu_usage = val;
	}
	if (sizeof(*fcoe_offload) > CNIC_KWQ16_DATA_SIZE) {
		netdev_err(dev->netdev, "fcoe_offload size too big\n");
		goto err_reply;
	}
	fcoe_offload = cnic_get_kwqe_16_data(cp, l5_cid, &l5_data);
	if (!fcoe_offload)
		goto err_reply;

	memset(fcoe_offload, 0, sizeof(*fcoe_offload));
	memcpy(&fcoe_offload->offload_kwqe1, req1, sizeof(*req1));
	memcpy(&fcoe_offload->offload_kwqe2, req2, sizeof(*req2));
	memcpy(&fcoe_offload->offload_kwqe3, req3, sizeof(*req3));
	memcpy(&fcoe_offload->offload_kwqe4, req4, sizeof(*req4));

	cid = BNX2X_HW_CID(bp, cid);
	ret = cnic_submit_kwqe_16(dev, FCOE_RAMROD_CMD_ID_OFFLOAD_CONN, cid,
				  FCOE_CONNECTION_TYPE, &l5_data);
	if (!ret)
		set_bit(CTX_FL_OFFLD_START, &ctx->ctx_flags);

	return ret;

err_reply:
	if (cid != -1)
		cnic_free_bnx2x_conn_resc(dev, l5_cid);

	memset(&kcqe, 0, sizeof(kcqe));
	kcqe.op_code = FCOE_KCQE_OPCODE_OFFLOAD_CONN;
	kcqe.fcoe_conn_id = req1->fcoe_conn_id;
	kcqe.completion_status = FCOE_KCQE_COMPLETION_STATUS_CTX_ALLOC_FAILURE;

	cqes[0] = (struct kcqe *) &kcqe;
	cnic_reply_bnx2x_kcqes(dev, CNIC_ULP_FCOE, cqes, 1);
	return ret;
}

static int cnic_bnx2x_fcoe_enable(struct cnic_dev *dev, struct kwqe *kwqe)
{
	struct fcoe_kwqe_conn_enable_disable *req;
	struct fcoe_conn_enable_disable_ramrod_params *fcoe_enable;
	union l5cm_specific_data l5_data;
	int ret;
	u32 cid, l5_cid;
	struct cnic_local *cp = dev->cnic_priv;

	req = (struct fcoe_kwqe_conn_enable_disable *) kwqe;
	cid = req->context_id;
	l5_cid = req->conn_id + BNX2X_FCOE_L5_CID_BASE;

	if (sizeof(*fcoe_enable) > CNIC_KWQ16_DATA_SIZE) {
		netdev_err(dev->netdev, "fcoe_enable size too big\n");
		return -ENOMEM;
	}
	fcoe_enable = cnic_get_kwqe_16_data(cp, l5_cid, &l5_data);
	if (!fcoe_enable)
		return -ENOMEM;

	memset(fcoe_enable, 0, sizeof(*fcoe_enable));
	memcpy(&fcoe_enable->enable_disable_kwqe, req, sizeof(*req));
	ret = cnic_submit_kwqe_16(dev, FCOE_RAMROD_CMD_ID_ENABLE_CONN, cid,
				  FCOE_CONNECTION_TYPE, &l5_data);
	return ret;
}

static int cnic_bnx2x_fcoe_disable(struct cnic_dev *dev, struct kwqe *kwqe)
{
	struct fcoe_kwqe_conn_enable_disable *req;
	struct fcoe_conn_enable_disable_ramrod_params *fcoe_disable;
	union l5cm_specific_data l5_data;
	int ret;
	u32 cid, l5_cid;
	struct cnic_local *cp = dev->cnic_priv;

	req = (struct fcoe_kwqe_conn_enable_disable *) kwqe;
	cid = req->context_id;
	l5_cid = req->conn_id;
	if (l5_cid >= dev->max_fcoe_conn)
		return -EINVAL;

	l5_cid += BNX2X_FCOE_L5_CID_BASE;

	if (sizeof(*fcoe_disable) > CNIC_KWQ16_DATA_SIZE) {
		netdev_err(dev->netdev, "fcoe_disable size too big\n");
		return -ENOMEM;
	}
	fcoe_disable = cnic_get_kwqe_16_data(cp, l5_cid, &l5_data);
	if (!fcoe_disable)
		return -ENOMEM;

	memset(fcoe_disable, 0, sizeof(*fcoe_disable));
	memcpy(&fcoe_disable->enable_disable_kwqe, req, sizeof(*req));
	ret = cnic_submit_kwqe_16(dev, FCOE_RAMROD_CMD_ID_DISABLE_CONN, cid,
				  FCOE_CONNECTION_TYPE, &l5_data);
	return ret;
}

static int cnic_bnx2x_fcoe_destroy(struct cnic_dev *dev, struct kwqe *kwqe)
{
	struct fcoe_kwqe_conn_destroy *req;
	union l5cm_specific_data l5_data;
	int ret;
	u32 cid, l5_cid;
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_context *ctx;
	struct fcoe_kcqe kcqe;
	struct kcqe *cqes[1];

	req = (struct fcoe_kwqe_conn_destroy *) kwqe;
	cid = req->context_id;
	l5_cid = req->conn_id;
	if (l5_cid >= dev->max_fcoe_conn)
		return -EINVAL;

	l5_cid += BNX2X_FCOE_L5_CID_BASE;

	ctx = &cp->ctx_tbl[l5_cid];

	init_waitqueue_head(&ctx->waitq);
	ctx->wait_cond = 0;

	memset(&kcqe, 0, sizeof(kcqe));
	kcqe.completion_status = FCOE_KCQE_COMPLETION_STATUS_ERROR;
	memset(&l5_data, 0, sizeof(l5_data));
	ret = cnic_submit_kwqe_16(dev, FCOE_RAMROD_CMD_ID_TERMINATE_CONN, cid,
				  FCOE_CONNECTION_TYPE, &l5_data);
	if (ret == 0) {
		wait_event_timeout(ctx->waitq, ctx->wait_cond, CNIC_RAMROD_TMO);
		if (ctx->wait_cond)
			kcqe.completion_status = 0;
	}

	set_bit(CTX_FL_DELETE_WAIT, &ctx->ctx_flags);
	queue_delayed_work(cnic_wq, &cp->delete_task, msecs_to_jiffies(2000));

	kcqe.op_code = FCOE_KCQE_OPCODE_DESTROY_CONN;
	kcqe.fcoe_conn_id = req->conn_id;
	kcqe.fcoe_conn_context_id = cid;

	cqes[0] = (struct kcqe *) &kcqe;
	cnic_reply_bnx2x_kcqes(dev, CNIC_ULP_FCOE, cqes, 1);
	return ret;
}

static void cnic_bnx2x_delete_wait(struct cnic_dev *dev, u32 start_cid)
{
	struct cnic_local *cp = dev->cnic_priv;
	u32 i;

	for (i = start_cid; i < cp->max_cid_space; i++) {
		struct cnic_context *ctx = &cp->ctx_tbl[i];
		int j;

		while (test_bit(CTX_FL_DELETE_WAIT, &ctx->ctx_flags))
			msleep(10);

		for (j = 0; j < 5; j++) {
			if (!test_bit(CTX_FL_OFFLD_START, &ctx->ctx_flags))
				break;
			msleep(20);
		}

		if (test_bit(CTX_FL_OFFLD_START, &ctx->ctx_flags))
			netdev_warn(dev->netdev, "CID %x not deleted\n",
				   ctx->cid);
	}
}

static int cnic_bnx2x_fcoe_fw_destroy(struct cnic_dev *dev, struct kwqe *kwqe)
{
	struct fcoe_kwqe_destroy *req;
	union l5cm_specific_data l5_data;
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	int ret;
	u32 cid;

	cnic_bnx2x_delete_wait(dev, MAX_ISCSI_TBL_SZ);

	req = (struct fcoe_kwqe_destroy *) kwqe;
	cid = BNX2X_HW_CID(bp, cp->fcoe_init_cid);

	memset(&l5_data, 0, sizeof(l5_data));
	ret = cnic_submit_kwqe_16(dev, FCOE_RAMROD_CMD_ID_DESTROY_FUNC, cid,
				  FCOE_CONNECTION_TYPE, &l5_data);
	return ret;
}

static void cnic_bnx2x_kwqe_err(struct cnic_dev *dev, struct kwqe *kwqe)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct kcqe kcqe;
	struct kcqe *cqes[1];
	u32 cid;
	u32 opcode = KWQE_OPCODE(kwqe->kwqe_op_flag);
	u32 layer_code = kwqe->kwqe_op_flag & KWQE_LAYER_MASK;
	u32 kcqe_op;
	int ulp_type;

	cid = kwqe->kwqe_info0;
	memset(&kcqe, 0, sizeof(kcqe));

	if (layer_code == KWQE_FLAGS_LAYER_MASK_L5_FCOE) {
		u32 l5_cid = 0;

		ulp_type = CNIC_ULP_FCOE;
		if (opcode == FCOE_KWQE_OPCODE_DISABLE_CONN) {
			struct fcoe_kwqe_conn_enable_disable *req;

			req = (struct fcoe_kwqe_conn_enable_disable *) kwqe;
			kcqe_op = FCOE_KCQE_OPCODE_DISABLE_CONN;
			cid = req->context_id;
			l5_cid = req->conn_id;
		} else if (opcode == FCOE_KWQE_OPCODE_DESTROY) {
			kcqe_op = FCOE_KCQE_OPCODE_DESTROY_FUNC;
		} else {
			return;
		}
		kcqe.kcqe_op_flag = kcqe_op << KCQE_FLAGS_OPCODE_SHIFT;
		kcqe.kcqe_op_flag |= KCQE_FLAGS_LAYER_MASK_L5_FCOE;
		kcqe.kcqe_info1 = FCOE_KCQE_COMPLETION_STATUS_PARITY_ERROR;
		kcqe.kcqe_info2 = cid;
		kcqe.kcqe_info0 = l5_cid;

	} else if (layer_code == KWQE_FLAGS_LAYER_MASK_L5_ISCSI) {
		ulp_type = CNIC_ULP_ISCSI;
		if (opcode == ISCSI_KWQE_OPCODE_UPDATE_CONN)
			cid = kwqe->kwqe_info1;

		kcqe.kcqe_op_flag = (opcode + 0x10) << KCQE_FLAGS_OPCODE_SHIFT;
		kcqe.kcqe_op_flag |= KCQE_FLAGS_LAYER_MASK_L5_ISCSI;
		kcqe.kcqe_info1 = ISCSI_KCQE_COMPLETION_STATUS_PARITY_ERR;
		kcqe.kcqe_info2 = cid;
		cnic_get_l5_cid(cp, BNX2X_SW_CID(cid), &kcqe.kcqe_info0);

	} else if (layer_code == KWQE_FLAGS_LAYER_MASK_L4) {
		struct l4_kcq *l4kcqe = (struct l4_kcq *) &kcqe;

		ulp_type = CNIC_ULP_L4;
		if (opcode == L4_KWQE_OPCODE_VALUE_CONNECT1)
			kcqe_op = L4_KCQE_OPCODE_VALUE_CONNECT_COMPLETE;
		else if (opcode == L4_KWQE_OPCODE_VALUE_RESET)
			kcqe_op = L4_KCQE_OPCODE_VALUE_RESET_COMP;
		else if (opcode == L4_KWQE_OPCODE_VALUE_CLOSE)
			kcqe_op = L4_KCQE_OPCODE_VALUE_CLOSE_COMP;
		else
			return;

		kcqe.kcqe_op_flag = (kcqe_op << KCQE_FLAGS_OPCODE_SHIFT) |
				    KCQE_FLAGS_LAYER_MASK_L4;
		l4kcqe->status = L4_KCQE_COMPLETION_STATUS_PARITY_ERROR;
		l4kcqe->cid = cid;
		cnic_get_l5_cid(cp, BNX2X_SW_CID(cid), &l4kcqe->conn_id);
	} else {
		return;
	}

	cqes[0] = &kcqe;
	cnic_reply_bnx2x_kcqes(dev, ulp_type, cqes, 1);
}

static int cnic_submit_bnx2x_iscsi_kwqes(struct cnic_dev *dev,
					 struct kwqe *wqes[], u32 num_wqes)
{
	int i, work, ret;
	u32 opcode;
	struct kwqe *kwqe;

	if (!test_bit(CNIC_F_CNIC_UP, &dev->flags))
		return -EAGAIN;		/* bnx2 is down */

	for (i = 0; i < num_wqes; ) {
		kwqe = wqes[i];
		opcode = KWQE_OPCODE(kwqe->kwqe_op_flag);
		work = 1;

		switch (opcode) {
		case ISCSI_KWQE_OPCODE_INIT1:
			ret = cnic_bnx2x_iscsi_init1(dev, kwqe);
			break;
		case ISCSI_KWQE_OPCODE_INIT2:
			ret = cnic_bnx2x_iscsi_init2(dev, kwqe);
			break;
		case ISCSI_KWQE_OPCODE_OFFLOAD_CONN1:
			ret = cnic_bnx2x_iscsi_ofld1(dev, &wqes[i],
						     num_wqes - i, &work);
			break;
		case ISCSI_KWQE_OPCODE_UPDATE_CONN:
			ret = cnic_bnx2x_iscsi_update(dev, kwqe);
			break;
		case ISCSI_KWQE_OPCODE_DESTROY_CONN:
			ret = cnic_bnx2x_iscsi_destroy(dev, kwqe);
			break;
		case L4_KWQE_OPCODE_VALUE_CONNECT1:
			ret = cnic_bnx2x_connect(dev, &wqes[i], num_wqes - i,
						 &work);
			break;
		case L4_KWQE_OPCODE_VALUE_CLOSE:
			ret = cnic_bnx2x_close(dev, kwqe);
			break;
		case L4_KWQE_OPCODE_VALUE_RESET:
			ret = cnic_bnx2x_reset(dev, kwqe);
			break;
		case L4_KWQE_OPCODE_VALUE_OFFLOAD_PG:
			ret = cnic_bnx2x_offload_pg(dev, kwqe);
			break;
		case L4_KWQE_OPCODE_VALUE_UPDATE_PG:
			ret = cnic_bnx2x_update_pg(dev, kwqe);
			break;
		case L4_KWQE_OPCODE_VALUE_UPLOAD_PG:
			ret = 0;
			break;
		default:
			ret = 0;
			netdev_err(dev->netdev, "Unknown type of KWQE(0x%x)\n",
				   opcode);
			break;
		}
		if (ret < 0) {
			netdev_err(dev->netdev, "KWQE(0x%x) failed\n",
				   opcode);

			/* Possibly bnx2x parity error, send completion
			 * to ulp drivers with error code to speed up
			 * cleanup and reset recovery.
			 */
			if (ret == -EIO || ret == -EAGAIN)
				cnic_bnx2x_kwqe_err(dev, kwqe);
		}
		i += work;
	}
	return 0;
}

static int cnic_submit_bnx2x_fcoe_kwqes(struct cnic_dev *dev,
					struct kwqe *wqes[], u32 num_wqes)
{
	struct bnx2x *bp = netdev_priv(dev->netdev);
	int i, work, ret;
	u32 opcode;
	struct kwqe *kwqe;

	if (!test_bit(CNIC_F_CNIC_UP, &dev->flags))
		return -EAGAIN;		/* bnx2 is down */

	if (!BNX2X_CHIP_IS_E2_PLUS(bp))
		return -EINVAL;

	for (i = 0; i < num_wqes; ) {
		kwqe = wqes[i];
		opcode = KWQE_OPCODE(kwqe->kwqe_op_flag);
		work = 1;

		switch (opcode) {
		case FCOE_KWQE_OPCODE_INIT1:
			ret = cnic_bnx2x_fcoe_init1(dev, &wqes[i],
						    num_wqes - i, &work);
			break;
		case FCOE_KWQE_OPCODE_OFFLOAD_CONN1:
			ret = cnic_bnx2x_fcoe_ofld1(dev, &wqes[i],
						    num_wqes - i, &work);
			break;
		case FCOE_KWQE_OPCODE_ENABLE_CONN:
			ret = cnic_bnx2x_fcoe_enable(dev, kwqe);
			break;
		case FCOE_KWQE_OPCODE_DISABLE_CONN:
			ret = cnic_bnx2x_fcoe_disable(dev, kwqe);
			break;
		case FCOE_KWQE_OPCODE_DESTROY_CONN:
			ret = cnic_bnx2x_fcoe_destroy(dev, kwqe);
			break;
		case FCOE_KWQE_OPCODE_DESTROY:
			ret = cnic_bnx2x_fcoe_fw_destroy(dev, kwqe);
			break;
		case FCOE_KWQE_OPCODE_STAT:
			ret = cnic_bnx2x_fcoe_stat(dev, kwqe);
			break;
		default:
			ret = 0;
			netdev_err(dev->netdev, "Unknown type of KWQE(0x%x)\n",
				   opcode);
			break;
		}
		if (ret < 0) {
			netdev_err(dev->netdev, "KWQE(0x%x) failed\n",
				   opcode);

			/* Possibly bnx2x parity error, send completion
			 * to ulp drivers with error code to speed up
			 * cleanup and reset recovery.
			 */
			if (ret == -EIO || ret == -EAGAIN)
				cnic_bnx2x_kwqe_err(dev, kwqe);
		}
		i += work;
	}
	return 0;
}

static int cnic_submit_bnx2x_kwqes(struct cnic_dev *dev, struct kwqe *wqes[],
				   u32 num_wqes)
{
	int ret = -EINVAL;
	u32 layer_code;

	if (!test_bit(CNIC_F_CNIC_UP, &dev->flags))
		return -EAGAIN;		/* bnx2x is down */

	if (!num_wqes)
		return 0;

	layer_code = wqes[0]->kwqe_op_flag & KWQE_LAYER_MASK;
	switch (layer_code) {
	case KWQE_FLAGS_LAYER_MASK_L5_ISCSI:
	case KWQE_FLAGS_LAYER_MASK_L4:
	case KWQE_FLAGS_LAYER_MASK_L2:
		ret = cnic_submit_bnx2x_iscsi_kwqes(dev, wqes, num_wqes);
		break;

	case KWQE_FLAGS_LAYER_MASK_L5_FCOE:
		ret = cnic_submit_bnx2x_fcoe_kwqes(dev, wqes, num_wqes);
		break;
	}
	return ret;
}

static inline u32 cnic_get_kcqe_layer_mask(u32 opflag)
{
	if (unlikely(KCQE_OPCODE(opflag) == FCOE_RAMROD_CMD_ID_TERMINATE_CONN))
		return KCQE_FLAGS_LAYER_MASK_L4;

	return opflag & KCQE_FLAGS_LAYER_MASK;
}

static void service_kcqes(struct cnic_dev *dev, int num_cqes)
{
	struct cnic_local *cp = dev->cnic_priv;
	int i, j, comp = 0;

	i = 0;
	j = 1;
	while (num_cqes) {
		struct cnic_ulp_ops *ulp_ops;
		int ulp_type;
		u32 kcqe_op_flag = cp->completed_kcq[i]->kcqe_op_flag;
		u32 kcqe_layer = cnic_get_kcqe_layer_mask(kcqe_op_flag);

		if (unlikely(kcqe_op_flag & KCQE_RAMROD_COMPLETION))
			comp++;

		while (j < num_cqes) {
			u32 next_op = cp->completed_kcq[i + j]->kcqe_op_flag;

			if (cnic_get_kcqe_layer_mask(next_op) != kcqe_layer)
				break;

			if (unlikely(next_op & KCQE_RAMROD_COMPLETION))
				comp++;
			j++;
		}

		if (kcqe_layer == KCQE_FLAGS_LAYER_MASK_L5_RDMA)
			ulp_type = CNIC_ULP_RDMA;
		else if (kcqe_layer == KCQE_FLAGS_LAYER_MASK_L5_ISCSI)
			ulp_type = CNIC_ULP_ISCSI;
		else if (kcqe_layer == KCQE_FLAGS_LAYER_MASK_L5_FCOE)
			ulp_type = CNIC_ULP_FCOE;
		else if (kcqe_layer == KCQE_FLAGS_LAYER_MASK_L4)
			ulp_type = CNIC_ULP_L4;
		else if (kcqe_layer == KCQE_FLAGS_LAYER_MASK_L2)
			goto end;
		else {
			netdev_err(dev->netdev, "Unknown type of KCQE(0x%x)\n",
				   kcqe_op_flag);
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
	if (unlikely(comp))
		cnic_spq_completion(dev, DRV_CTL_RET_L5_SPQ_CREDIT_CMD, comp);
}

static int cnic_get_kcqes(struct cnic_dev *dev, struct kcq_info *info)
{
	struct cnic_local *cp = dev->cnic_priv;
	u16 i, ri, hw_prod, last;
	struct kcqe *kcqe;
	int kcqe_cnt = 0, last_cnt = 0;

	i = ri = last = info->sw_prod_idx;
	ri &= MAX_KCQ_IDX;
	hw_prod = *info->hw_prod_idx_ptr;
	hw_prod = info->hw_idx(hw_prod);

	while ((i != hw_prod) && (kcqe_cnt < MAX_COMPLETED_KCQE)) {
		kcqe = &info->kcq[KCQ_PG(ri)][KCQ_IDX(ri)];
		cp->completed_kcq[kcqe_cnt++] = kcqe;
		i = info->next_idx(i);
		ri = i & MAX_KCQ_IDX;
		if (likely(!(kcqe->kcqe_op_flag & KCQE_FLAGS_NEXT))) {
			last_cnt = kcqe_cnt;
			last = i;
		}
	}

	info->sw_prod_idx = last;
	return last_cnt;
}

static int cnic_l2_completion(struct cnic_local *cp)
{
	u16 hw_cons, sw_cons;
	struct cnic_uio_dev *udev = cp->udev;
	union eth_rx_cqe *cqe, *cqe_ring = (union eth_rx_cqe *)
					(udev->l2_ring + (2 * CNIC_PAGE_SIZE));
	u32 cmd;
	int comp = 0;

	if (!test_bit(CNIC_F_BNX2X_CLASS, &cp->dev->flags))
		return 0;

	hw_cons = *cp->rx_cons_ptr;
	if ((hw_cons & BNX2X_MAX_RCQ_DESC_CNT) == BNX2X_MAX_RCQ_DESC_CNT)
		hw_cons++;

	sw_cons = cp->rx_cons;
	while (sw_cons != hw_cons) {
		u8 cqe_fp_flags;

		cqe = &cqe_ring[sw_cons & BNX2X_MAX_RCQ_DESC_CNT];
		cqe_fp_flags = cqe->fast_path_cqe.type_error_flags;
		if (cqe_fp_flags & ETH_FAST_PATH_RX_CQE_TYPE) {
			cmd = le32_to_cpu(cqe->ramrod_cqe.conn_and_cmd_data);
			cmd >>= COMMON_RAMROD_ETH_RX_CQE_CMD_ID_SHIFT;
			if (cmd == RAMROD_CMD_ID_ETH_CLIENT_SETUP ||
			    cmd == RAMROD_CMD_ID_ETH_HALT)
				comp++;
		}
		sw_cons = BNX2X_NEXT_RCQE(sw_cons);
	}
	return comp;
}

static void cnic_chk_pkt_rings(struct cnic_local *cp)
{
	u16 rx_cons, tx_cons;
	int comp = 0;

	if (!test_bit(CNIC_LCL_FL_RINGS_INITED, &cp->cnic_local_flags))
		return;

	rx_cons = *cp->rx_cons_ptr;
	tx_cons = *cp->tx_cons_ptr;
	if (cp->tx_cons != tx_cons || cp->rx_cons != rx_cons) {
		if (test_bit(CNIC_LCL_FL_L2_WAIT, &cp->cnic_local_flags))
			comp = cnic_l2_completion(cp);

		cp->tx_cons = tx_cons;
		cp->rx_cons = rx_cons;

		if (cp->udev)
			uio_event_notify(&cp->udev->cnic_uinfo);
	}
	if (comp)
		clear_bit(CNIC_LCL_FL_L2_WAIT, &cp->cnic_local_flags);
}

static u32 cnic_service_bnx2_queues(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	u32 status_idx = (u16) *cp->kcq1.status_idx_ptr;
	int kcqe_cnt;

	/* status block index must be read before reading other fields */
	rmb();
	cp->kwq_con_idx = *cp->kwq_con_idx_ptr;

	while ((kcqe_cnt = cnic_get_kcqes(dev, &cp->kcq1))) {

		service_kcqes(dev, kcqe_cnt);

		/* Tell compiler that status_blk fields can change. */
		barrier();
		status_idx = (u16) *cp->kcq1.status_idx_ptr;
		/* status block index must be read first */
		rmb();
		cp->kwq_con_idx = *cp->kwq_con_idx_ptr;
	}

	CNIC_WR16(dev, cp->kcq1.io_addr, cp->kcq1.sw_prod_idx);

	cnic_chk_pkt_rings(cp);

	return status_idx;
}

static int cnic_service_bnx2(void *data, void *status_blk)
{
	struct cnic_dev *dev = data;

	if (unlikely(!test_bit(CNIC_F_CNIC_UP, &dev->flags))) {
		struct status_block *sblk = status_blk;

		return sblk->status_idx;
	}

	return cnic_service_bnx2_queues(dev);
}

static void cnic_service_bnx2_msix(unsigned long data)
{
	struct cnic_dev *dev = (struct cnic_dev *) data;
	struct cnic_local *cp = dev->cnic_priv;

	cp->last_status_idx = cnic_service_bnx2_queues(dev);

	CNIC_WR(dev, BNX2_PCICFG_INT_ACK_CMD, cp->int_num |
		BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID | cp->last_status_idx);
}

static void cnic_doirq(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;

	if (likely(test_bit(CNIC_F_CNIC_UP, &dev->flags))) {
		u16 prod = cp->kcq1.sw_prod_idx & MAX_KCQ_IDX;

		prefetch(cp->status_blk.gen);
		prefetch(&cp->kcq1.kcq[KCQ_PG(prod)][KCQ_IDX(prod)]);

		tasklet_schedule(&cp->cnic_irq_task);
	}
}

static irqreturn_t cnic_irq(int irq, void *dev_instance)
{
	struct cnic_dev *dev = dev_instance;
	struct cnic_local *cp = dev->cnic_priv;

	if (cp->ack_int)
		cp->ack_int(dev);

	cnic_doirq(dev);

	return IRQ_HANDLED;
}

static inline void cnic_ack_bnx2x_int(struct cnic_dev *dev, u8 id, u8 storm,
				      u16 index, u8 op, u8 update)
{
	struct bnx2x *bp = netdev_priv(dev->netdev);
	u32 hc_addr = (HC_REG_COMMAND_REG + BP_PORT(bp) * 32 +
		       COMMAND_REG_INT_ACK);
	struct igu_ack_register igu_ack;

	igu_ack.status_block_index = index;
	igu_ack.sb_id_and_flags =
			((id << IGU_ACK_REGISTER_STATUS_BLOCK_ID_SHIFT) |
			 (storm << IGU_ACK_REGISTER_STORM_ID_SHIFT) |
			 (update << IGU_ACK_REGISTER_UPDATE_INDEX_SHIFT) |
			 (op << IGU_ACK_REGISTER_INTERRUPT_MODE_SHIFT));

	CNIC_WR(dev, hc_addr, (*(u32 *)&igu_ack));
}

static void cnic_ack_igu_sb(struct cnic_dev *dev, u8 igu_sb_id, u8 segment,
			    u16 index, u8 op, u8 update)
{
	struct igu_regular cmd_data;
	u32 igu_addr = BAR_IGU_INTMEM + (IGU_CMD_INT_ACK_BASE + igu_sb_id) * 8;

	cmd_data.sb_id_and_flags =
		(index << IGU_REGULAR_SB_INDEX_SHIFT) |
		(segment << IGU_REGULAR_SEGMENT_ACCESS_SHIFT) |
		(update << IGU_REGULAR_BUPDATE_SHIFT) |
		(op << IGU_REGULAR_ENABLE_INT_SHIFT);


	CNIC_WR(dev, igu_addr, cmd_data.sb_id_and_flags);
}

static void cnic_ack_bnx2x_msix(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;

	cnic_ack_bnx2x_int(dev, cp->bnx2x_igu_sb_id, CSTORM_ID, 0,
			   IGU_INT_DISABLE, 0);
}

static void cnic_ack_bnx2x_e2_msix(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;

	cnic_ack_igu_sb(dev, cp->bnx2x_igu_sb_id, IGU_SEG_ACCESS_DEF, 0,
			IGU_INT_DISABLE, 0);
}

static void cnic_arm_bnx2x_msix(struct cnic_dev *dev, u32 idx)
{
	struct cnic_local *cp = dev->cnic_priv;

	cnic_ack_bnx2x_int(dev, cp->bnx2x_igu_sb_id, CSTORM_ID, idx,
			   IGU_INT_ENABLE, 1);
}

static void cnic_arm_bnx2x_e2_msix(struct cnic_dev *dev, u32 idx)
{
	struct cnic_local *cp = dev->cnic_priv;

	cnic_ack_igu_sb(dev, cp->bnx2x_igu_sb_id, IGU_SEG_ACCESS_DEF, idx,
			IGU_INT_ENABLE, 1);
}

static u32 cnic_service_bnx2x_kcq(struct cnic_dev *dev, struct kcq_info *info)
{
	u32 last_status = *info->status_idx_ptr;
	int kcqe_cnt;

	/* status block index must be read before reading the KCQ */
	rmb();
	while ((kcqe_cnt = cnic_get_kcqes(dev, info))) {

		service_kcqes(dev, kcqe_cnt);

		/* Tell compiler that sblk fields can change. */
		barrier();

		last_status = *info->status_idx_ptr;
		/* status block index must be read before reading the KCQ */
		rmb();
	}
	return last_status;
}

static void cnic_service_bnx2x_bh(unsigned long data)
{
	struct cnic_dev *dev = (struct cnic_dev *) data;
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	u32 status_idx, new_status_idx;

	if (unlikely(!test_bit(CNIC_F_CNIC_UP, &dev->flags)))
		return;

	while (1) {
		status_idx = cnic_service_bnx2x_kcq(dev, &cp->kcq1);

		CNIC_WR16(dev, cp->kcq1.io_addr,
			  cp->kcq1.sw_prod_idx + MAX_KCQ_IDX);

		if (!CNIC_SUPPORTS_FCOE(bp)) {
			cp->arm_int(dev, status_idx);
			break;
		}

		new_status_idx = cnic_service_bnx2x_kcq(dev, &cp->kcq2);

		if (new_status_idx != status_idx)
			continue;

		CNIC_WR16(dev, cp->kcq2.io_addr, cp->kcq2.sw_prod_idx +
			  MAX_KCQ_IDX);

		cnic_ack_igu_sb(dev, cp->bnx2x_igu_sb_id, IGU_SEG_ACCESS_DEF,
				status_idx, IGU_INT_ENABLE, 1);

		break;
	}
}

static int cnic_service_bnx2x(void *data, void *status_blk)
{
	struct cnic_dev *dev = data;
	struct cnic_local *cp = dev->cnic_priv;

	if (!(cp->ethdev->drv_state & CNIC_DRV_STATE_USING_MSIX))
		cnic_doirq(dev);

	cnic_chk_pkt_rings(cp);

	return 0;
}

static void cnic_ulp_stop_one(struct cnic_local *cp, int if_type)
{
	struct cnic_ulp_ops *ulp_ops;

	if (if_type == CNIC_ULP_ISCSI)
		cnic_send_nlmsg(cp, ISCSI_KEVENT_IF_DOWN, NULL);

	mutex_lock(&cnic_lock);
	ulp_ops = rcu_dereference_protected(cp->ulp_ops[if_type],
					    lockdep_is_held(&cnic_lock));
	if (!ulp_ops) {
		mutex_unlock(&cnic_lock);
		return;
	}
	set_bit(ULP_F_CALL_PENDING, &cp->ulp_flags[if_type]);
	mutex_unlock(&cnic_lock);

	if (test_and_clear_bit(ULP_F_START, &cp->ulp_flags[if_type]))
		ulp_ops->cnic_stop(cp->ulp_handle[if_type]);

	clear_bit(ULP_F_CALL_PENDING, &cp->ulp_flags[if_type]);
}

static void cnic_ulp_stop(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	int if_type;

	for (if_type = 0; if_type < MAX_CNIC_ULP_TYPE; if_type++)
		cnic_ulp_stop_one(cp, if_type);
}

static void cnic_ulp_start(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	int if_type;

	for (if_type = 0; if_type < MAX_CNIC_ULP_TYPE; if_type++) {
		struct cnic_ulp_ops *ulp_ops;

		mutex_lock(&cnic_lock);
		ulp_ops = rcu_dereference_protected(cp->ulp_ops[if_type],
						    lockdep_is_held(&cnic_lock));
		if (!ulp_ops || !ulp_ops->cnic_start) {
			mutex_unlock(&cnic_lock);
			continue;
		}
		set_bit(ULP_F_CALL_PENDING, &cp->ulp_flags[if_type]);
		mutex_unlock(&cnic_lock);

		if (!test_and_set_bit(ULP_F_START, &cp->ulp_flags[if_type]))
			ulp_ops->cnic_start(cp->ulp_handle[if_type]);

		clear_bit(ULP_F_CALL_PENDING, &cp->ulp_flags[if_type]);
	}
}

static int cnic_copy_ulp_stats(struct cnic_dev *dev, int ulp_type)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_ulp_ops *ulp_ops;
	int rc;

	mutex_lock(&cnic_lock);
	ulp_ops = rcu_dereference_protected(cp->ulp_ops[ulp_type],
					    lockdep_is_held(&cnic_lock));
	if (ulp_ops && ulp_ops->cnic_get_stats)
		rc = ulp_ops->cnic_get_stats(cp->ulp_handle[ulp_type]);
	else
		rc = -ENODEV;
	mutex_unlock(&cnic_lock);
	return rc;
}

static int cnic_ctl(void *data, struct cnic_ctl_info *info)
{
	struct cnic_dev *dev = data;
	int ulp_type = CNIC_ULP_ISCSI;

	switch (info->cmd) {
	case CNIC_CTL_STOP_CMD:
		cnic_hold(dev);

		cnic_ulp_stop(dev);
		cnic_stop_hw(dev);

		cnic_put(dev);
		break;
	case CNIC_CTL_START_CMD:
		cnic_hold(dev);

		if (!cnic_start_hw(dev))
			cnic_ulp_start(dev);

		cnic_put(dev);
		break;
	case CNIC_CTL_STOP_ISCSI_CMD: {
		struct cnic_local *cp = dev->cnic_priv;
		set_bit(CNIC_LCL_FL_STOP_ISCSI, &cp->cnic_local_flags);
		queue_delayed_work(cnic_wq, &cp->delete_task, 0);
		break;
	}
	case CNIC_CTL_COMPLETION_CMD: {
		struct cnic_ctl_completion *comp = &info->data.comp;
		u32 cid = BNX2X_SW_CID(comp->cid);
		u32 l5_cid;
		struct cnic_local *cp = dev->cnic_priv;

		if (!test_bit(CNIC_F_CNIC_UP, &dev->flags))
			break;

		if (cnic_get_l5_cid(cp, cid, &l5_cid) == 0) {
			struct cnic_context *ctx = &cp->ctx_tbl[l5_cid];

			if (unlikely(comp->error)) {
				set_bit(CTX_FL_CID_ERROR, &ctx->ctx_flags);
				netdev_err(dev->netdev,
					   "CID %x CFC delete comp error %x\n",
					   cid, comp->error);
			}

			ctx->wait_cond = 1;
			wake_up(&ctx->waitq);
		}
		break;
	}
	case CNIC_CTL_FCOE_STATS_GET_CMD:
		ulp_type = CNIC_ULP_FCOE;
		/* fall through */
	case CNIC_CTL_ISCSI_STATS_GET_CMD:
		cnic_hold(dev);
		cnic_copy_ulp_stats(dev, ulp_type);
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

	for (i = 0; i < MAX_CNIC_ULP_TYPE_EXT; i++) {
		struct cnic_ulp_ops *ulp_ops;

		mutex_lock(&cnic_lock);
		ulp_ops = cnic_ulp_tbl_prot(i);
		if (!ulp_ops || !ulp_ops->cnic_init) {
			mutex_unlock(&cnic_lock);
			continue;
		}
		ulp_get(ulp_ops);
		mutex_unlock(&cnic_lock);

		if (!test_and_set_bit(ULP_F_INIT, &cp->ulp_flags[i]))
			ulp_ops->cnic_init(dev);

		ulp_put(ulp_ops);
	}
}

static void cnic_ulp_exit(struct cnic_dev *dev)
{
	int i;
	struct cnic_local *cp = dev->cnic_priv;

	for (i = 0; i < MAX_CNIC_ULP_TYPE_EXT; i++) {
		struct cnic_ulp_ops *ulp_ops;

		mutex_lock(&cnic_lock);
		ulp_ops = cnic_ulp_tbl_prot(i);
		if (!ulp_ops || !ulp_ops->cnic_exit) {
			mutex_unlock(&cnic_lock);
			continue;
		}
		ulp_get(ulp_ops);
		mutex_unlock(&cnic_lock);

		if (test_and_clear_bit(ULP_F_INIT, &cp->ulp_flags[i]))
			ulp_ops->cnic_exit(dev);

		ulp_put(ulp_ops);
	}
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
	l4kwqe->ipid_start = DEF_IPID_START;
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

	if (cp->ctx_tbl) {
		struct cnic_context *ctx = &cp->ctx_tbl[l5_cid];

		if (test_bit(CTX_FL_OFFLD_START, &ctx->ctx_flags))
			return -EAGAIN;
	}

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
	csk1->tcp_flags = 0;

	*csk = csk1;
	return 0;
}

static void cnic_cm_cleanup(struct cnic_sock *csk)
{
	if (csk->src_port) {
		struct cnic_dev *dev = csk->dev;
		struct cnic_local *cp = dev->cnic_priv;

		cnic_free_id(&cp->csk_port_tbl, be16_to_cpu(csk->src_port));
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
	smp_mb__after_atomic();
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
	struct rtable *rt;

	rt = ip_route_output(&init_net, dst_addr->sin_addr.s_addr, 0, 0, 0);
	if (!IS_ERR(rt)) {
		*dst = &rt->dst;
		return 0;
	}
	return PTR_ERR(rt);
#else
	return -ENETUNREACH;
#endif
}

static int cnic_get_v6_route(struct sockaddr_in6 *dst_addr,
			     struct dst_entry **dst)
{
#if defined(CONFIG_IPV6) || (defined(CONFIG_IPV6_MODULE) && defined(MODULE))
	struct flowi6 fl6;

	memset(&fl6, 0, sizeof(fl6));
	fl6.daddr = dst_addr->sin6_addr;
	if (ipv6_addr_type(&fl6.daddr) & IPV6_ADDR_LINKLOCAL)
		fl6.flowi6_oif = dst_addr->sin6_scope_id;

	*dst = ip6_route_output(&init_net, NULL, &fl6);
	if ((*dst)->error) {
		dst_release(*dst);
		*dst = NULL;
		return -ENETUNREACH;
	} else
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
	int is_v6, rc = 0;
	struct dst_entry *dst = NULL;
	struct net_device *realdev;
	__be16 local_port;
	u32 port_id;

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
		set_bit(SK_F_IPV6, &csk->flags);
		cnic_get_v6_route(&saddr->remote.v6, &dst);

		memcpy(&csk->dst_ip[0], &saddr->remote.v6.sin6_addr,
		       sizeof(struct in6_addr));
		csk->dst_port = saddr->remote.v6.sin6_port;
		local_port = saddr->local.v6.sin6_port;

	} else {
		cnic_get_v4_route(&saddr->remote.v4, &dst);

		csk->dst_ip[0] = saddr->remote.v4.sin_addr.s_addr;
		csk->dst_port = saddr->remote.v4.sin_port;
		local_port = saddr->local.v4.sin_port;
	}

	csk->vlan_id = 0;
	csk->mtu = dev->netdev->mtu;
	if (dst && dst->dev) {
		u16 vlan = cnic_get_vlan(dst->dev, &realdev);
		if (realdev == dev->netdev) {
			csk->vlan_id = vlan;
			csk->mtu = dst_mtu(dst);
		}
	}

	port_id = be16_to_cpu(local_port);
	if (port_id >= CNIC_LOCAL_PORT_MIN &&
	    port_id < CNIC_LOCAL_PORT_MAX) {
		if (cnic_alloc_id(&cp->csk_port_tbl, port_id))
			port_id = 0;
	} else
		port_id = 0;

	if (!port_id) {
		port_id = cnic_alloc_new_id(&cp->csk_port_tbl);
		if (port_id == -1) {
			rc = -ENOMEM;
			goto err_out;
		}
		local_port = cpu_to_be16(port_id);
	}
	csk->src_port = local_port;

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
	struct cnic_local *cp = csk->dev->cnic_priv;
	int err = 0;

	if (cp->ethdev->drv_state & CNIC_DRV_STATE_NO_ISCSI)
		return -EOPNOTSUPP;

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
	u32 opcode = L4_KCQE_OPCODE_VALUE_RESET_COMP;

	if (!cnic_in_use(csk))
		return -EINVAL;

	if (cnic_abort_prep(csk))
		return cnic_cm_abort_req(csk);

	/* Getting here means that we haven't started connect, or
	 * connect was not successful, or it has been reset by the target.
	 */

	cp->close_conn(csk, opcode);
	if (csk->state != opcode) {
		/* Wait for remote reset sequence to complete */
		while (test_bit(SK_F_PG_OFFLD_COMPLETE, &csk->flags))
			msleep(1);

		return -EALREADY;
	}

	return 0;
}

static int cnic_cm_close(struct cnic_sock *csk)
{
	if (!cnic_in_use(csk))
		return -EINVAL;

	if (cnic_close_prep(csk)) {
		csk->state = L4_KCQE_OPCODE_VALUE_CLOSE_COMP;
		return cnic_cm_close_req(csk);
	} else {
		/* Wait for remote reset sequence to complete */
		while (test_bit(SK_F_PG_OFFLD_COMPLETE, &csk->flags))
			msleep(1);

		return -EALREADY;
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
	/* Possible PG kcqe status:  SUCCESS, OFFLOADED_PG, or CTX_ALLOC_FAIL */
	if (kcqe->status == L4_KCQE_COMPLETION_STATUS_CTX_ALLOC_FAIL) {
		clear_bit(SK_F_OFFLD_SCHED, &csk->flags);
		cnic_cm_upcall(cp, csk,
			       L4_KCQE_OPCODE_VALUE_CONNECT_COMPLETE);
		goto done;
	}

	csk->pg_cid = kcqe->pg_cid;
	set_bit(SK_F_PG_OFFLD_COMPLETE, &csk->flags);
	cnic_cm_conn_req(csk);

done:
	csk_put(csk);
}

static void cnic_process_fcoe_term_conn(struct cnic_dev *dev, struct kcqe *kcqe)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct fcoe_kcqe *fc_kcqe = (struct fcoe_kcqe *) kcqe;
	u32 l5_cid = fc_kcqe->fcoe_conn_id + BNX2X_FCOE_L5_CID_BASE;
	struct cnic_context *ctx = &cp->ctx_tbl[l5_cid];

	ctx->timestamp = jiffies;
	ctx->wait_cond = 1;
	wake_up(&ctx->waitq);
}

static void cnic_cm_process_kcqe(struct cnic_dev *dev, struct kcqe *kcqe)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct l4_kcq *l4kcqe = (struct l4_kcq *) kcqe;
	u8 opcode = l4kcqe->op_code;
	u32 l5_cid;
	struct cnic_sock *csk;

	if (opcode == FCOE_RAMROD_CMD_ID_TERMINATE_CONN) {
		cnic_process_fcoe_term_conn(dev, kcqe);
		return;
	}
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
	case L5CM_RAMROD_CMD_ID_TCP_CONNECT:
		if (l4kcqe->status != 0) {
			clear_bit(SK_F_OFFLD_SCHED, &csk->flags);
			cnic_cm_upcall(cp, csk,
				       L4_KCQE_OPCODE_VALUE_CONNECT_COMPLETE);
		}
		break;
	case L4_KCQE_OPCODE_VALUE_CONNECT_COMPLETE:
		if (l4kcqe->status == 0)
			set_bit(SK_F_OFFLD_COMPLETE, &csk->flags);
		else if (l4kcqe->status ==
			 L4_KCQE_COMPLETION_STATUS_PARITY_ERROR)
			set_bit(SK_F_HW_ERR, &csk->flags);

		smp_mb__before_atomic();
		clear_bit(SK_F_OFFLD_SCHED, &csk->flags);
		cnic_cm_upcall(cp, csk, opcode);
		break;

	case L5CM_RAMROD_CMD_ID_CLOSE: {
		struct iscsi_kcqe *l5kcqe = (struct iscsi_kcqe *) kcqe;

		if (l4kcqe->status != 0 || l5kcqe->completion_status != 0) {
			netdev_warn(dev->netdev, "RAMROD CLOSE compl with status 0x%x completion status 0x%x\n",
				    l4kcqe->status, l5kcqe->completion_status);
			opcode = L4_KCQE_OPCODE_VALUE_CLOSE_COMP;
			/* Fall through */
		} else {
			break;
		}
	}
	case L4_KCQE_OPCODE_VALUE_RESET_RECEIVED:
	case L4_KCQE_OPCODE_VALUE_CLOSE_COMP:
	case L4_KCQE_OPCODE_VALUE_RESET_COMP:
	case L5CM_RAMROD_CMD_ID_SEARCHER_DELETE:
	case L5CM_RAMROD_CMD_ID_TERMINATE_OFFLOAD:
		if (l4kcqe->status == L4_KCQE_COMPLETION_STATUS_PARITY_ERROR)
			set_bit(SK_F_HW_ERR, &csk->flags);

		cp->close_conn(csk, opcode);
		break;

	case L4_KCQE_OPCODE_VALUE_CLOSE_RECEIVED:
		/* after we already sent CLOSE_REQ */
		if (test_bit(CNIC_F_BNX2X_CLASS, &dev->flags) &&
		    !test_bit(SK_F_OFFLD_COMPLETE, &csk->flags) &&
		    csk->state == L4_KCQE_OPCODE_VALUE_CLOSE_COMP)
			cp->close_conn(csk, L4_KCQE_OPCODE_VALUE_RESET_COMP);
		else
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
	u32 port_id;

	cp->csk_tbl = kzalloc(sizeof(struct cnic_sock) * MAX_CM_SK_TBL_SZ,
			      GFP_KERNEL);
	if (!cp->csk_tbl)
		return -ENOMEM;

	port_id = prandom_u32();
	port_id %= CNIC_LOCAL_PORT_RANGE;
	if (cnic_init_id_tbl(&cp->csk_port_tbl, CNIC_LOCAL_PORT_RANGE,
			     CNIC_LOCAL_PORT_MIN, port_id)) {
		cnic_cm_free_mem(dev);
		return -ENOMEM;
	}
	return 0;
}

static int cnic_ready_to_close(struct cnic_sock *csk, u32 opcode)
{
	if (test_and_clear_bit(SK_F_OFFLD_COMPLETE, &csk->flags)) {
		/* Unsolicited RESET_COMP or RESET_RECEIVED */
		opcode = L4_KCQE_OPCODE_VALUE_RESET_RECEIVED;
		csk->state = opcode;
	}

	/* 1. If event opcode matches the expected event in csk->state
	 * 2. If the expected event is CLOSE_COMP or RESET_COMP, we accept any
	 *    event
	 * 3. If the expected event is 0, meaning the connection was never
	 *    never established, we accept the opcode from cm_abort.
	 */
	if (opcode == csk->state || csk->state == 0 ||
	    csk->state == L4_KCQE_OPCODE_VALUE_CLOSE_COMP ||
	    csk->state == L4_KCQE_OPCODE_VALUE_RESET_COMP) {
		if (!test_and_set_bit(SK_F_CLOSING, &csk->flags)) {
			if (csk->state == 0)
				csk->state = opcode;
			return 1;
		}
	}
	return 0;
}

static void cnic_close_bnx2_conn(struct cnic_sock *csk, u32 opcode)
{
	struct cnic_dev *dev = csk->dev;
	struct cnic_local *cp = dev->cnic_priv;

	if (opcode == L4_KCQE_OPCODE_VALUE_RESET_RECEIVED) {
		cnic_cm_upcall(cp, csk, opcode);
		return;
	}

	clear_bit(SK_F_CONNECT_START, &csk->flags);
	cnic_close_conn(csk);
	csk->state = opcode;
	cnic_cm_upcall(cp, csk, opcode);
}

static void cnic_cm_stop_bnx2_hw(struct cnic_dev *dev)
{
}

static int cnic_cm_init_bnx2_hw(struct cnic_dev *dev)
{
	u32 seed;

	seed = prandom_u32();
	cnic_ctx_wr(dev, 45, 0, seed);
	return 0;
}

static void cnic_close_bnx2x_conn(struct cnic_sock *csk, u32 opcode)
{
	struct cnic_dev *dev = csk->dev;
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_context *ctx = &cp->ctx_tbl[csk->l5_cid];
	union l5cm_specific_data l5_data;
	u32 cmd = 0;
	int close_complete = 0;

	switch (opcode) {
	case L4_KCQE_OPCODE_VALUE_RESET_RECEIVED:
	case L4_KCQE_OPCODE_VALUE_CLOSE_COMP:
	case L4_KCQE_OPCODE_VALUE_RESET_COMP:
		if (cnic_ready_to_close(csk, opcode)) {
			if (test_bit(SK_F_HW_ERR, &csk->flags))
				close_complete = 1;
			else if (test_bit(SK_F_PG_OFFLD_COMPLETE, &csk->flags))
				cmd = L5CM_RAMROD_CMD_ID_SEARCHER_DELETE;
			else
				close_complete = 1;
		}
		break;
	case L5CM_RAMROD_CMD_ID_SEARCHER_DELETE:
		cmd = L5CM_RAMROD_CMD_ID_TERMINATE_OFFLOAD;
		break;
	case L5CM_RAMROD_CMD_ID_TERMINATE_OFFLOAD:
		close_complete = 1;
		break;
	}
	if (cmd) {
		memset(&l5_data, 0, sizeof(l5_data));

		cnic_submit_kwqe_16(dev, cmd, csk->cid, ISCSI_CONNECTION_TYPE,
				    &l5_data);
	} else if (close_complete) {
		ctx->timestamp = jiffies;
		cnic_close_conn(csk);
		cnic_cm_upcall(cp, csk, csk->state);
	}
}

static void cnic_cm_stop_bnx2x_hw(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;

	if (!cp->ctx_tbl)
		return;

	if (!netif_running(dev->netdev))
		return;

	cnic_bnx2x_delete_wait(dev, 0);

	cancel_delayed_work(&cp->delete_task);
	flush_workqueue(cnic_wq);

	if (atomic_read(&cp->iscsi_conn) != 0)
		netdev_warn(dev->netdev, "%d iSCSI connections not destroyed\n",
			    atomic_read(&cp->iscsi_conn));
}

static int cnic_cm_init_bnx2x_hw(struct cnic_dev *dev)
{
	struct bnx2x *bp = netdev_priv(dev->netdev);
	u32 pfid = bp->pfid;
	u32 port = BP_PORT(bp);

	cnic_init_bnx2x_mac(dev);
	cnic_bnx2x_set_tcp_options(dev, 0, 1);

	CNIC_WR16(dev, BAR_XSTRORM_INTMEM +
		  XSTORM_ISCSI_LOCAL_VLAN_OFFSET(pfid), 0);

	CNIC_WR(dev, BAR_XSTRORM_INTMEM +
		XSTORM_TCP_GLOBAL_DEL_ACK_COUNTER_ENABLED_OFFSET(port), 1);
	CNIC_WR(dev, BAR_XSTRORM_INTMEM +
		XSTORM_TCP_GLOBAL_DEL_ACK_COUNTER_MAX_COUNT_OFFSET(port),
		DEF_MAX_DA_COUNT);

	CNIC_WR8(dev, BAR_XSTRORM_INTMEM +
		 XSTORM_ISCSI_TCP_VARS_TTL_OFFSET(pfid), DEF_TTL);
	CNIC_WR8(dev, BAR_XSTRORM_INTMEM +
		 XSTORM_ISCSI_TCP_VARS_TOS_OFFSET(pfid), DEF_TOS);
	CNIC_WR8(dev, BAR_XSTRORM_INTMEM +
		 XSTORM_ISCSI_TCP_VARS_ADV_WND_SCL_OFFSET(pfid), 2);
	CNIC_WR(dev, BAR_XSTRORM_INTMEM +
		XSTORM_TCP_TX_SWS_TIMER_VAL_OFFSET(pfid), DEF_SWS_TIMER);

	CNIC_WR(dev, BAR_TSTRORM_INTMEM + TSTORM_TCP_MAX_CWND_OFFSET(pfid),
		DEF_MAX_CWND);
	return 0;
}

static void cnic_delete_task(struct work_struct *work)
{
	struct cnic_local *cp;
	struct cnic_dev *dev;
	u32 i;
	int need_resched = 0;

	cp = container_of(work, struct cnic_local, delete_task.work);
	dev = cp->dev;

	if (test_and_clear_bit(CNIC_LCL_FL_STOP_ISCSI, &cp->cnic_local_flags)) {
		struct drv_ctl_info info;

		cnic_ulp_stop_one(cp, CNIC_ULP_ISCSI);

		info.cmd = DRV_CTL_ISCSI_STOPPED_CMD;
		cp->ethdev->drv_ctl(dev->netdev, &info);
	}

	for (i = 0; i < cp->max_cid_space; i++) {
		struct cnic_context *ctx = &cp->ctx_tbl[i];
		int err;

		if (!test_bit(CTX_FL_OFFLD_START, &ctx->ctx_flags) ||
		    !test_bit(CTX_FL_DELETE_WAIT, &ctx->ctx_flags))
			continue;

		if (!time_after(jiffies, ctx->timestamp + (2 * HZ))) {
			need_resched = 1;
			continue;
		}

		if (!test_and_clear_bit(CTX_FL_DELETE_WAIT, &ctx->ctx_flags))
			continue;

		err = cnic_bnx2x_destroy_ramrod(dev, i);

		cnic_free_bnx2x_conn_resc(dev, i);
		if (!err) {
			if (ctx->ulp_proto_id == CNIC_ULP_ISCSI)
				atomic_dec(&cp->iscsi_conn);

			clear_bit(CTX_FL_OFFLD_START, &ctx->ctx_flags);
		}
	}

	if (need_resched)
		queue_delayed_work(cnic_wq, &cp->delete_task,
				   msecs_to_jiffies(10));

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

	INIT_DELAYED_WORK(&cp->delete_task, cnic_delete_task);

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
	u32 cid_addr;
	int i;

	cid_addr = GET_CID_ADDR(cid);

	for (i = 0; i < CTX_SIZE; i += 4)
		cnic_ctx_wr(dev, cid_addr, i, 0);
}

static int cnic_setup_5709_context(struct cnic_dev *dev, int valid)
{
	struct cnic_local *cp = dev->cnic_priv;
	int ret = 0, i;
	u32 valid_bit = valid ? BNX2_CTX_HOST_PAGE_TBL_DATA0_VALID : 0;

	if (BNX2_CHIP(cp) != BNX2_CHIP_5709)
		return 0;

	for (i = 0; i < cp->ctx_blks; i++) {
		int j;
		u32 idx = cp->ctx_arr[i].cid / cp->cids_per_blk;
		u32 val;

		memset(cp->ctx_arr[i].ctx, 0, CNIC_PAGE_SIZE);

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
		tasklet_kill(&cp->cnic_irq_task);
		free_irq(ethdev->irq_arr[0].vector, dev);
	}
}

static int cnic_request_irq(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	int err;

	err = request_irq(ethdev->irq_arr[0].vector, cnic_irq, 0, "cnic", dev);
	if (err)
		tasklet_disable(&cp->cnic_irq_task);

	return err;
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

		cp->last_status_idx = cp->status_blk.bnx2->status_idx;
		tasklet_init(&cp->cnic_irq_task, cnic_service_bnx2_msix,
			     (unsigned long) dev);
		err = cnic_request_irq(dev);
		if (err)
			return err;

		while (cp->status_blk.bnx2->status_completion_producer_index &&
		       i < 10) {
			CNIC_WR(dev, BNX2_HC_COALESCE_NOW,
				1 << (11 + sblk_num));
			udelay(10);
			i++;
			barrier();
		}
		if (cp->status_blk.bnx2->status_completion_producer_index) {
			cnic_free_irq(dev);
			goto failed;
		}

	} else {
		struct status_block *sblk = cp->status_blk.gen;
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
	netdev_err(dev->netdev, "KCQ index not resetting to 0\n");
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
	struct cnic_uio_dev *udev = cp->udev;
	u32 cid_addr, tx_cid, sb_id;
	u32 val, offset0, offset1, offset2, offset3;
	int i;
	struct bnx2_tx_bd *txbd;
	dma_addr_t buf_map, ring_map = udev->l2_ring_map;
	struct status_block *s_blk = cp->status_blk.gen;

	sb_id = cp->status_blk_num;
	tx_cid = 20;
	cp->tx_cons_ptr = &s_blk->status_tx_quick_consumer_index2;
	if (ethdev->drv_state & CNIC_DRV_STATE_USING_MSIX) {
		struct status_block_msix *sblk = cp->status_blk.bnx2;

		tx_cid = TX_TSS_CID + sb_id - 1;
		CNIC_WR(dev, BNX2_TSCH_TSS_CFG, (sb_id << 24) |
			(TX_TSS_CID << 7));
		cp->tx_cons_ptr = &sblk->status_tx_quick_consumer_index;
	}
	cp->tx_cons = *cp->tx_cons_ptr;

	cid_addr = GET_CID_ADDR(tx_cid);
	if (BNX2_CHIP(cp) == BNX2_CHIP_5709) {
		u32 cid_addr2 = GET_CID_ADDR(tx_cid + 4) + 0x40;

		for (i = 0; i < PHY_CTX_SIZE; i += 4)
			cnic_ctx_wr(dev, cid_addr2, i, 0);

		offset0 = BNX2_L2CTX_TYPE_XI;
		offset1 = BNX2_L2CTX_CMD_TYPE_XI;
		offset2 = BNX2_L2CTX_TBDR_BHADDR_HI_XI;
		offset3 = BNX2_L2CTX_TBDR_BHADDR_LO_XI;
	} else {
		cnic_init_context(dev, tx_cid);
		cnic_init_context(dev, tx_cid + 1);

		offset0 = BNX2_L2CTX_TYPE;
		offset1 = BNX2_L2CTX_CMD_TYPE;
		offset2 = BNX2_L2CTX_TBDR_BHADDR_HI;
		offset3 = BNX2_L2CTX_TBDR_BHADDR_LO;
	}
	val = BNX2_L2CTX_TYPE_TYPE_L2 | BNX2_L2CTX_TYPE_SIZE_L2;
	cnic_ctx_wr(dev, cid_addr, offset0, val);

	val = BNX2_L2CTX_CMD_TYPE_TYPE_L2 | (8 << 16);
	cnic_ctx_wr(dev, cid_addr, offset1, val);

	txbd = udev->l2_ring;

	buf_map = udev->l2_buf_map;
	for (i = 0; i < BNX2_MAX_TX_DESC_CNT; i++, txbd++) {
		txbd->tx_bd_haddr_hi = (u64) buf_map >> 32;
		txbd->tx_bd_haddr_lo = (u64) buf_map & 0xffffffff;
	}
	val = (u64) ring_map >> 32;
	cnic_ctx_wr(dev, cid_addr, offset2, val);
	txbd->tx_bd_haddr_hi = val;

	val = (u64) ring_map & 0xffffffff;
	cnic_ctx_wr(dev, cid_addr, offset3, val);
	txbd->tx_bd_haddr_lo = val;
}

static void cnic_init_bnx2_rx_ring(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	struct cnic_uio_dev *udev = cp->udev;
	u32 cid_addr, sb_id, val, coal_reg, coal_val;
	int i;
	struct bnx2_rx_bd *rxbd;
	struct status_block *s_blk = cp->status_blk.gen;
	dma_addr_t ring_map = udev->l2_ring_map;

	sb_id = cp->status_blk_num;
	cnic_init_context(dev, 2);
	cp->rx_cons_ptr = &s_blk->status_rx_quick_consumer_index2;
	coal_reg = BNX2_HC_COMMAND;
	coal_val = CNIC_RD(dev, coal_reg);
	if (ethdev->drv_state & CNIC_DRV_STATE_USING_MSIX) {
		struct status_block_msix *sblk = cp->status_blk.bnx2;

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
		val = 2 << BNX2_L2CTX_L2_STATUSB_NUM_SHIFT;
	else
		val = BNX2_L2CTX_L2_STATUSB_NUM(sb_id);
	cnic_ctx_wr(dev, cid_addr, BNX2_L2CTX_HOST_BDIDX, val);

	rxbd = udev->l2_ring + CNIC_PAGE_SIZE;
	for (i = 0; i < BNX2_MAX_RX_DESC_CNT; i++, rxbd++) {
		dma_addr_t buf_map;
		int n = (i % cp->l2_rx_ring_size) + 1;

		buf_map = udev->l2_buf_map + (n * cp->l2_single_buf_size);
		rxbd->rx_bd_len = cp->l2_single_buf_size;
		rxbd->rx_bd_flags = RX_BD_FLAGS_START | RX_BD_FLAGS_END;
		rxbd->rx_bd_haddr_hi = (u64) buf_map >> 32;
		rxbd->rx_bd_haddr_lo = (u64) buf_map & 0xffffffff;
	}
	val = (u64) (ring_map + CNIC_PAGE_SIZE) >> 32;
	cnic_ctx_wr(dev, cid_addr, BNX2_L2CTX_NX_BDHADDR_HI, val);
	rxbd->rx_bd_haddr_hi = val;

	val = (u64) (ring_map + CNIC_PAGE_SIZE) & 0xffffffff;
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
	l2kwqe.kwqe_op_flag = (L2_LAYER_CODE << KWQE_LAYER_SHIFT) |
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
	if (BNX2_CHIP(cp) != BNX2_CHIP_5709)
		val |= BNX2_RPM_SORT_USER2_PROM_VLAN;

	CNIC_WR(dev, BNX2_RPM_SORT_USER2, 0x0);
	CNIC_WR(dev, BNX2_RPM_SORT_USER2, val);
	CNIC_WR(dev, BNX2_RPM_SORT_USER2, val | BNX2_RPM_SORT_USER2_ENA);
}

static int cnic_start_bnx2_hw(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	struct status_block *sblk = cp->status_blk.gen;
	u32 val, kcq_cid_addr, kwq_cid_addr;
	int err;

	cnic_set_bnx2_mac(dev);

	val = CNIC_RD(dev, BNX2_MQ_CONFIG);
	val &= ~BNX2_MQ_CONFIG_KNL_BYP_BLK_SIZE;
	if (CNIC_PAGE_BITS > 12)
		val |= (12 - 8)  << 4;
	else
		val |= (CNIC_PAGE_BITS - 8)  << 4;

	CNIC_WR(dev, BNX2_MQ_CONFIG, val);

	CNIC_WR(dev, BNX2_HC_COMP_PROD_TRIP, (2 << 16) | 8);
	CNIC_WR(dev, BNX2_HC_COM_TICKS, (64 << 16) | 220);
	CNIC_WR(dev, BNX2_HC_CMD_TICKS, (64 << 16) | 220);

	err = cnic_setup_5709_context(dev, 1);
	if (err)
		return err;

	cnic_init_context(dev, KWQ_CID);
	cnic_init_context(dev, KCQ_CID);

	kwq_cid_addr = GET_CID_ADDR(KWQ_CID);
	cp->kwq_io_addr = MB_GET_CID_ADDR(KWQ_CID) + L5_KRNLQ_HOST_QIDX;

	cp->max_kwq_idx = MAX_KWQ_IDX;
	cp->kwq_prod_idx = 0;
	cp->kwq_con_idx = 0;
	set_bit(CNIC_LCL_FL_KWQ_INIT, &cp->cnic_local_flags);

	if (BNX2_CHIP(cp) == BNX2_CHIP_5706 || BNX2_CHIP(cp) == BNX2_CHIP_5708)
		cp->kwq_con_idx_ptr = &sblk->status_rx_quick_consumer_index15;
	else
		cp->kwq_con_idx_ptr = &sblk->status_cmd_consumer_index;

	/* Initialize the kernel work queue context. */
	val = KRNLQ_TYPE_TYPE_KRNLQ | KRNLQ_SIZE_TYPE_SIZE |
	      (CNIC_PAGE_BITS - 8) | KRNLQ_FLAGS_QE_SELF_SEQ;
	cnic_ctx_wr(dev, kwq_cid_addr, L5_KRNLQ_TYPE, val);

	val = (CNIC_PAGE_SIZE / sizeof(struct kwqe) - 1) << 16;
	cnic_ctx_wr(dev, kwq_cid_addr, L5_KRNLQ_QE_SELF_SEQ_MAX, val);

	val = ((CNIC_PAGE_SIZE / sizeof(struct kwqe)) << 16) | KWQ_PAGE_CNT;
	cnic_ctx_wr(dev, kwq_cid_addr, L5_KRNLQ_PGTBL_NPAGES, val);

	val = (u32) ((u64) cp->kwq_info.pgtbl_map >> 32);
	cnic_ctx_wr(dev, kwq_cid_addr, L5_KRNLQ_PGTBL_HADDR_HI, val);

	val = (u32) cp->kwq_info.pgtbl_map;
	cnic_ctx_wr(dev, kwq_cid_addr, L5_KRNLQ_PGTBL_HADDR_LO, val);

	kcq_cid_addr = GET_CID_ADDR(KCQ_CID);
	cp->kcq1.io_addr = MB_GET_CID_ADDR(KCQ_CID) + L5_KRNLQ_HOST_QIDX;

	cp->kcq1.sw_prod_idx = 0;
	cp->kcq1.hw_prod_idx_ptr =
		&sblk->status_completion_producer_index;

	cp->kcq1.status_idx_ptr = &sblk->status_idx;

	/* Initialize the kernel complete queue context. */
	val = KRNLQ_TYPE_TYPE_KRNLQ | KRNLQ_SIZE_TYPE_SIZE |
	      (CNIC_PAGE_BITS - 8) | KRNLQ_FLAGS_QE_SELF_SEQ;
	cnic_ctx_wr(dev, kcq_cid_addr, L5_KRNLQ_TYPE, val);

	val = (CNIC_PAGE_SIZE / sizeof(struct kcqe) - 1) << 16;
	cnic_ctx_wr(dev, kcq_cid_addr, L5_KRNLQ_QE_SELF_SEQ_MAX, val);

	val = ((CNIC_PAGE_SIZE / sizeof(struct kcqe)) << 16) | KCQ_PAGE_CNT;
	cnic_ctx_wr(dev, kcq_cid_addr, L5_KRNLQ_PGTBL_NPAGES, val);

	val = (u32) ((u64) cp->kcq1.dma.pgtbl_map >> 32);
	cnic_ctx_wr(dev, kcq_cid_addr, L5_KRNLQ_PGTBL_HADDR_HI, val);

	val = (u32) cp->kcq1.dma.pgtbl_map;
	cnic_ctx_wr(dev, kcq_cid_addr, L5_KRNLQ_PGTBL_HADDR_LO, val);

	cp->int_num = 0;
	if (ethdev->drv_state & CNIC_DRV_STATE_USING_MSIX) {
		struct status_block_msix *msblk = cp->status_blk.bnx2;
		u32 sb_id = cp->status_blk_num;
		u32 sb = BNX2_L2CTX_L5_STATUSB_NUM(sb_id);

		cp->kcq1.hw_prod_idx_ptr =
			&msblk->status_completion_producer_index;
		cp->kcq1.status_idx_ptr = &msblk->status_idx;
		cp->kwq_con_idx_ptr = &msblk->status_cmd_consumer_index;
		cp->int_num = sb_id << BNX2_PCICFG_INT_ACK_CMD_INT_NUM_SHIFT;
		cnic_ctx_wr(dev, kwq_cid_addr, L5_KRNLQ_HOST_QIDX, sb);
		cnic_ctx_wr(dev, kcq_cid_addr, L5_KRNLQ_HOST_QIDX, sb);
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
		netdev_err(dev->netdev, "cnic_init_irq failed\n");
		cnic_reg_wr_ind(dev, BNX2_CP_SCRATCH + 0x20, 0);
		cnic_reg_wr_ind(dev, BNX2_COM_SCRATCH + 0x20, 0);
		return err;
	}

	ethdev->drv_state |= CNIC_DRV_STATE_HANDLES_IRQ;

	return 0;
}

static void cnic_setup_bnx2x_context(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	u32 start_offset = ethdev->ctx_tbl_offset;
	int i;

	for (i = 0; i < cp->ctx_blks; i++) {
		struct cnic_ctx *ctx = &cp->ctx_arr[i];
		dma_addr_t map = ctx->mapping;

		if (cp->ctx_align) {
			unsigned long mask = cp->ctx_align - 1;

			map = (map + mask) & ~mask;
		}

		cnic_ctx_tbl_wr(dev, start_offset + i, map);
	}
}

static int cnic_init_bnx2x_irq(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	int err = 0;

	tasklet_init(&cp->cnic_irq_task, cnic_service_bnx2x_bh,
		     (unsigned long) dev);
	if (ethdev->drv_state & CNIC_DRV_STATE_USING_MSIX)
		err = cnic_request_irq(dev);

	return err;
}

static inline void cnic_storm_memset_hc_disable(struct cnic_dev *dev,
						u16 sb_id, u8 sb_index,
						u8 disable)
{
	struct bnx2x *bp = netdev_priv(dev->netdev);

	u32 addr = BAR_CSTRORM_INTMEM +
			CSTORM_STATUS_BLOCK_DATA_OFFSET(sb_id) +
			offsetof(struct hc_status_block_data_e1x, index_data) +
			sizeof(struct hc_index_data)*sb_index +
			offsetof(struct hc_index_data, flags);
	u16 flags = CNIC_RD16(dev, addr);
	/* clear and set */
	flags &= ~HC_INDEX_DATA_HC_ENABLED;
	flags |= (((~disable) << HC_INDEX_DATA_HC_ENABLED_SHIFT) &
		  HC_INDEX_DATA_HC_ENABLED);
	CNIC_WR16(dev, addr, flags);
}

static void cnic_enable_bnx2x_int(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	u8 sb_id = cp->status_blk_num;

	CNIC_WR8(dev, BAR_CSTRORM_INTMEM +
			CSTORM_STATUS_BLOCK_DATA_OFFSET(sb_id) +
			offsetof(struct hc_status_block_data_e1x, index_data) +
			sizeof(struct hc_index_data)*HC_INDEX_ISCSI_EQ_CONS +
			offsetof(struct hc_index_data, timeout), 64 / 4);
	cnic_storm_memset_hc_disable(dev, sb_id, HC_INDEX_ISCSI_EQ_CONS, 0);
}

static void cnic_disable_bnx2x_int_sync(struct cnic_dev *dev)
{
}

static void cnic_init_bnx2x_tx_ring(struct cnic_dev *dev,
				    struct client_init_ramrod_data *data)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	struct cnic_uio_dev *udev = cp->udev;
	union eth_tx_bd_types *txbd = (union eth_tx_bd_types *) udev->l2_ring;
	dma_addr_t buf_map, ring_map = udev->l2_ring_map;
	struct host_sp_status_block *sb = cp->bnx2x_def_status_blk;
	int i;
	u32 cli = cp->ethdev->iscsi_l2_client_id;
	u32 val;

	memset(txbd, 0, CNIC_PAGE_SIZE);

	buf_map = udev->l2_buf_map;
	for (i = 0; i < BNX2_MAX_TX_DESC_CNT; i += 3, txbd += 3) {
		struct eth_tx_start_bd *start_bd = &txbd->start_bd;
		struct eth_tx_parse_bd_e1x *pbd_e1x =
			&((txbd + 1)->parse_bd_e1x);
		struct eth_tx_parse_bd_e2 *pbd_e2 = &((txbd + 1)->parse_bd_e2);
		struct eth_tx_bd *reg_bd = &((txbd + 2)->reg_bd);

		start_bd->addr_hi = cpu_to_le32((u64) buf_map >> 32);
		start_bd->addr_lo = cpu_to_le32(buf_map & 0xffffffff);
		reg_bd->addr_hi = start_bd->addr_hi;
		reg_bd->addr_lo = start_bd->addr_lo + 0x10;
		start_bd->nbytes = cpu_to_le16(0x10);
		start_bd->nbd = cpu_to_le16(3);
		start_bd->bd_flags.as_bitfield = ETH_TX_BD_FLAGS_START_BD;
		start_bd->general_data &= ~ETH_TX_START_BD_PARSE_NBDS;
		start_bd->general_data |= (1 << ETH_TX_START_BD_HDR_NBDS_SHIFT);

		if (BNX2X_CHIP_IS_E2_PLUS(bp))
			pbd_e2->parsing_data = (UNICAST_ADDRESS <<
				ETH_TX_PARSE_BD_E2_ETH_ADDR_TYPE_SHIFT);
		else
			pbd_e1x->global_data = (UNICAST_ADDRESS <<
				ETH_TX_PARSE_BD_E1X_ETH_ADDR_TYPE_SHIFT);
	}

	val = (u64) ring_map >> 32;
	txbd->next_bd.addr_hi = cpu_to_le32(val);

	data->tx.tx_bd_page_base.hi = cpu_to_le32(val);

	val = (u64) ring_map & 0xffffffff;
	txbd->next_bd.addr_lo = cpu_to_le32(val);

	data->tx.tx_bd_page_base.lo = cpu_to_le32(val);

	/* Other ramrod params */
	data->tx.tx_sb_index_number = HC_SP_INDEX_ETH_ISCSI_CQ_CONS;
	data->tx.tx_status_block_id = BNX2X_DEF_SB_ID;

	/* reset xstorm per client statistics */
	if (cli < MAX_STAT_COUNTER_ID) {
		data->general.statistics_zero_flg = 1;
		data->general.statistics_en_flg = 1;
		data->general.statistics_counter_id = cli;
	}

	cp->tx_cons_ptr =
		&sb->sp_sb.index_values[HC_SP_INDEX_ETH_ISCSI_CQ_CONS];
}

static void cnic_init_bnx2x_rx_ring(struct cnic_dev *dev,
				    struct client_init_ramrod_data *data)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	struct cnic_uio_dev *udev = cp->udev;
	struct eth_rx_bd *rxbd = (struct eth_rx_bd *) (udev->l2_ring +
				CNIC_PAGE_SIZE);
	struct eth_rx_cqe_next_page *rxcqe = (struct eth_rx_cqe_next_page *)
				(udev->l2_ring + (2 * CNIC_PAGE_SIZE));
	struct host_sp_status_block *sb = cp->bnx2x_def_status_blk;
	int i;
	u32 cli = cp->ethdev->iscsi_l2_client_id;
	int cl_qzone_id = BNX2X_CL_QZONE_ID(bp, cli);
	u32 val;
	dma_addr_t ring_map = udev->l2_ring_map;

	/* General data */
	data->general.client_id = cli;
	data->general.activate_flg = 1;
	data->general.sp_client_id = cli;
	data->general.mtu = cpu_to_le16(cp->l2_single_buf_size - 14);
	data->general.func_id = bp->pfid;

	for (i = 0; i < BNX2X_MAX_RX_DESC_CNT; i++, rxbd++) {
		dma_addr_t buf_map;
		int n = (i % cp->l2_rx_ring_size) + 1;

		buf_map = udev->l2_buf_map + (n * cp->l2_single_buf_size);
		rxbd->addr_hi = cpu_to_le32((u64) buf_map >> 32);
		rxbd->addr_lo = cpu_to_le32(buf_map & 0xffffffff);
	}

	val = (u64) (ring_map + CNIC_PAGE_SIZE) >> 32;
	rxbd->addr_hi = cpu_to_le32(val);
	data->rx.bd_page_base.hi = cpu_to_le32(val);

	val = (u64) (ring_map + CNIC_PAGE_SIZE) & 0xffffffff;
	rxbd->addr_lo = cpu_to_le32(val);
	data->rx.bd_page_base.lo = cpu_to_le32(val);

	rxcqe += BNX2X_MAX_RCQ_DESC_CNT;
	val = (u64) (ring_map + (2 * CNIC_PAGE_SIZE)) >> 32;
	rxcqe->addr_hi = cpu_to_le32(val);
	data->rx.cqe_page_base.hi = cpu_to_le32(val);

	val = (u64) (ring_map + (2 * CNIC_PAGE_SIZE)) & 0xffffffff;
	rxcqe->addr_lo = cpu_to_le32(val);
	data->rx.cqe_page_base.lo = cpu_to_le32(val);

	/* Other ramrod params */
	data->rx.client_qzone_id = cl_qzone_id;
	data->rx.rx_sb_index_number = HC_SP_INDEX_ETH_ISCSI_RX_CQ_CONS;
	data->rx.status_block_id = BNX2X_DEF_SB_ID;

	data->rx.cache_line_alignment_log_size = L1_CACHE_SHIFT;

	data->rx.max_bytes_on_bd = cpu_to_le16(cp->l2_single_buf_size);
	data->rx.outer_vlan_removal_enable_flg = 1;
	data->rx.silent_vlan_removal_flg = 1;
	data->rx.silent_vlan_value = 0;
	data->rx.silent_vlan_mask = 0xffff;

	cp->rx_cons_ptr =
		&sb->sp_sb.index_values[HC_SP_INDEX_ETH_ISCSI_RX_CQ_CONS];
	cp->rx_cons = *cp->rx_cons_ptr;
}

static void cnic_init_bnx2x_kcq(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	u32 pfid = bp->pfid;

	cp->kcq1.io_addr = BAR_CSTRORM_INTMEM +
			   CSTORM_ISCSI_EQ_PROD_OFFSET(pfid, 0);
	cp->kcq1.sw_prod_idx = 0;

	if (BNX2X_CHIP_IS_E2_PLUS(bp)) {
		struct host_hc_status_block_e2 *sb = cp->status_blk.gen;

		cp->kcq1.hw_prod_idx_ptr =
			&sb->sb.index_values[HC_INDEX_ISCSI_EQ_CONS];
		cp->kcq1.status_idx_ptr =
			&sb->sb.running_index[SM_RX_ID];
	} else {
		struct host_hc_status_block_e1x *sb = cp->status_blk.gen;

		cp->kcq1.hw_prod_idx_ptr =
			&sb->sb.index_values[HC_INDEX_ISCSI_EQ_CONS];
		cp->kcq1.status_idx_ptr =
			&sb->sb.running_index[SM_RX_ID];
	}

	if (BNX2X_CHIP_IS_E2_PLUS(bp)) {
		struct host_hc_status_block_e2 *sb = cp->status_blk.gen;

		cp->kcq2.io_addr = BAR_USTRORM_INTMEM +
					USTORM_FCOE_EQ_PROD_OFFSET(pfid);
		cp->kcq2.sw_prod_idx = 0;
		cp->kcq2.hw_prod_idx_ptr =
			&sb->sb.index_values[HC_INDEX_FCOE_EQ_CONS];
		cp->kcq2.status_idx_ptr =
			&sb->sb.running_index[SM_RX_ID];
	}
}

static int cnic_start_bnx2x_hw(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	struct cnic_eth_dev *ethdev = cp->ethdev;
	int func, ret;
	u32 pfid;

	dev->stats_addr = ethdev->addr_drv_info_to_mcp;
	cp->func = bp->pf_num;

	func = CNIC_FUNC(cp);
	pfid = bp->pfid;

	ret = cnic_init_id_tbl(&cp->cid_tbl, MAX_ISCSI_TBL_SZ,
			       cp->iscsi_start_cid, 0);

	if (ret)
		return -ENOMEM;

	if (BNX2X_CHIP_IS_E2_PLUS(bp)) {
		ret = cnic_init_id_tbl(&cp->fcoe_cid_tbl, dev->max_fcoe_conn,
					cp->fcoe_start_cid, 0);

		if (ret)
			return -ENOMEM;
	}

	cp->bnx2x_igu_sb_id = ethdev->irq_arr[0].status_blk_num2;

	cnic_init_bnx2x_kcq(dev);

	/* Only 1 EQ */
	CNIC_WR16(dev, cp->kcq1.io_addr, MAX_KCQ_IDX);
	CNIC_WR(dev, BAR_CSTRORM_INTMEM +
		CSTORM_ISCSI_EQ_CONS_OFFSET(pfid, 0), 0);
	CNIC_WR(dev, BAR_CSTRORM_INTMEM +
		CSTORM_ISCSI_EQ_NEXT_PAGE_ADDR_OFFSET(pfid, 0),
		cp->kcq1.dma.pg_map_arr[1] & 0xffffffff);
	CNIC_WR(dev, BAR_CSTRORM_INTMEM +
		CSTORM_ISCSI_EQ_NEXT_PAGE_ADDR_OFFSET(pfid, 0) + 4,
		(u64) cp->kcq1.dma.pg_map_arr[1] >> 32);
	CNIC_WR(dev, BAR_CSTRORM_INTMEM +
		CSTORM_ISCSI_EQ_NEXT_EQE_ADDR_OFFSET(pfid, 0),
		cp->kcq1.dma.pg_map_arr[0] & 0xffffffff);
	CNIC_WR(dev, BAR_CSTRORM_INTMEM +
		CSTORM_ISCSI_EQ_NEXT_EQE_ADDR_OFFSET(pfid, 0) + 4,
		(u64) cp->kcq1.dma.pg_map_arr[0] >> 32);
	CNIC_WR8(dev, BAR_CSTRORM_INTMEM +
		CSTORM_ISCSI_EQ_NEXT_PAGE_ADDR_VALID_OFFSET(pfid, 0), 1);
	CNIC_WR16(dev, BAR_CSTRORM_INTMEM +
		CSTORM_ISCSI_EQ_SB_NUM_OFFSET(pfid, 0), cp->status_blk_num);
	CNIC_WR8(dev, BAR_CSTRORM_INTMEM +
		CSTORM_ISCSI_EQ_SB_INDEX_OFFSET(pfid, 0),
		HC_INDEX_ISCSI_EQ_CONS);

	CNIC_WR(dev, BAR_USTRORM_INTMEM +
		USTORM_ISCSI_GLOBAL_BUF_PHYS_ADDR_OFFSET(pfid),
		cp->gbl_buf_info.pg_map_arr[0] & 0xffffffff);
	CNIC_WR(dev, BAR_USTRORM_INTMEM +
		USTORM_ISCSI_GLOBAL_BUF_PHYS_ADDR_OFFSET(pfid) + 4,
		(u64) cp->gbl_buf_info.pg_map_arr[0] >> 32);

	CNIC_WR(dev, BAR_TSTRORM_INTMEM +
		TSTORM_ISCSI_TCP_LOCAL_ADV_WND_OFFSET(pfid), DEF_RCV_BUF);

	cnic_setup_bnx2x_context(dev);

	ret = cnic_init_bnx2x_irq(dev);
	if (ret)
		return ret;

	ethdev->drv_state |= CNIC_DRV_STATE_HANDLES_IRQ;
	return 0;
}

static void cnic_init_rings(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	struct cnic_uio_dev *udev = cp->udev;

	if (test_bit(CNIC_LCL_FL_RINGS_INITED, &cp->cnic_local_flags))
		return;

	if (test_bit(CNIC_F_BNX2_CLASS, &dev->flags)) {
		cnic_init_bnx2_tx_ring(dev);
		cnic_init_bnx2_rx_ring(dev);
		set_bit(CNIC_LCL_FL_RINGS_INITED, &cp->cnic_local_flags);
	} else if (test_bit(CNIC_F_BNX2X_CLASS, &dev->flags)) {
		u32 cli = cp->ethdev->iscsi_l2_client_id;
		u32 cid = cp->ethdev->iscsi_l2_cid;
		u32 cl_qzone_id;
		struct client_init_ramrod_data *data;
		union l5cm_specific_data l5_data;
		struct ustorm_eth_rx_producers rx_prods = {0};
		u32 off, i, *cid_ptr;

		rx_prods.bd_prod = 0;
		rx_prods.cqe_prod = BNX2X_MAX_RCQ_DESC_CNT;
		barrier();

		cl_qzone_id = BNX2X_CL_QZONE_ID(bp, cli);

		off = BAR_USTRORM_INTMEM +
			(BNX2X_CHIP_IS_E2_PLUS(bp) ?
			 USTORM_RX_PRODS_E2_OFFSET(cl_qzone_id) :
			 USTORM_RX_PRODS_E1X_OFFSET(BP_PORT(bp), cli));

		for (i = 0; i < sizeof(struct ustorm_eth_rx_producers) / 4; i++)
			CNIC_WR(dev, off + i * 4, ((u32 *) &rx_prods)[i]);

		set_bit(CNIC_LCL_FL_L2_WAIT, &cp->cnic_local_flags);

		data = udev->l2_buf;
		cid_ptr = udev->l2_buf + 12;

		memset(data, 0, sizeof(*data));

		cnic_init_bnx2x_tx_ring(dev, data);
		cnic_init_bnx2x_rx_ring(dev, data);

		l5_data.phy_address.lo = udev->l2_buf_map & 0xffffffff;
		l5_data.phy_address.hi = (u64) udev->l2_buf_map >> 32;

		set_bit(CNIC_LCL_FL_RINGS_INITED, &cp->cnic_local_flags);

		cnic_submit_kwqe_16(dev, RAMROD_CMD_ID_ETH_CLIENT_SETUP,
			cid, ETH_CONNECTION_TYPE, &l5_data);

		i = 0;
		while (test_bit(CNIC_LCL_FL_L2_WAIT, &cp->cnic_local_flags) &&
		       ++i < 10)
			msleep(1);

		if (test_bit(CNIC_LCL_FL_L2_WAIT, &cp->cnic_local_flags))
			netdev_err(dev->netdev,
				"iSCSI CLIENT_SETUP did not complete\n");
		cnic_spq_completion(dev, DRV_CTL_RET_L2_SPQ_CREDIT_CMD, 1);
		cnic_ring_ctl(dev, cid, cli, 1);
		*cid_ptr = cid >> 4;
		*(cid_ptr + 1) = cid * bp->db_size;
		*(cid_ptr + 2) = UIO_USE_TX_DOORBELL;
	}
}

static void cnic_shutdown_rings(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_uio_dev *udev = cp->udev;
	void *rx_ring;

	if (!test_bit(CNIC_LCL_FL_RINGS_INITED, &cp->cnic_local_flags))
		return;

	if (test_bit(CNIC_F_BNX2_CLASS, &dev->flags)) {
		cnic_shutdown_bnx2_rx_ring(dev);
	} else if (test_bit(CNIC_F_BNX2X_CLASS, &dev->flags)) {
		u32 cli = cp->ethdev->iscsi_l2_client_id;
		u32 cid = cp->ethdev->iscsi_l2_cid;
		union l5cm_specific_data l5_data;
		int i;

		cnic_ring_ctl(dev, cid, cli, 0);

		set_bit(CNIC_LCL_FL_L2_WAIT, &cp->cnic_local_flags);

		l5_data.phy_address.lo = cli;
		l5_data.phy_address.hi = 0;
		cnic_submit_kwqe_16(dev, RAMROD_CMD_ID_ETH_HALT,
			cid, ETH_CONNECTION_TYPE, &l5_data);
		i = 0;
		while (test_bit(CNIC_LCL_FL_L2_WAIT, &cp->cnic_local_flags) &&
		       ++i < 10)
			msleep(1);

		if (test_bit(CNIC_LCL_FL_L2_WAIT, &cp->cnic_local_flags))
			netdev_err(dev->netdev,
				"iSCSI CLIENT_HALT did not complete\n");
		cnic_spq_completion(dev, DRV_CTL_RET_L2_SPQ_CREDIT_CMD, 1);

		memset(&l5_data, 0, sizeof(l5_data));
		cnic_submit_kwqe_16(dev, RAMROD_CMD_ID_COMMON_CFC_DEL,
			cid, NONE_CONNECTION_TYPE, &l5_data);
		msleep(10);
	}
	clear_bit(CNIC_LCL_FL_RINGS_INITED, &cp->cnic_local_flags);
	rx_ring = udev->l2_ring + CNIC_PAGE_SIZE;
	memset(rx_ring, 0, CNIC_PAGE_SIZE);
}

static int cnic_register_netdev(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	int err;

	if (!ethdev)
		return -ENODEV;

	if (ethdev->drv_state & CNIC_DRV_STATE_REGD)
		return 0;

	err = ethdev->drv_register_cnic(dev->netdev, cp->cnic_ops, dev);
	if (err)
		netdev_err(dev->netdev, "register_cnic failed\n");

	/* Read iSCSI config again.  On some bnx2x device, iSCSI config
	 * can change after firmware is downloaded.
	 */
	dev->max_iscsi_conn = ethdev->max_iscsi_conn;
	if (ethdev->drv_state & CNIC_DRV_STATE_NO_ISCSI)
		dev->max_iscsi_conn = 0;

	return err;
}

static void cnic_unregister_netdev(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;

	if (!ethdev)
		return;

	ethdev->drv_unregister_cnic(dev->netdev);
}

static int cnic_start_hw(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct cnic_eth_dev *ethdev = cp->ethdev;
	int err;

	if (test_bit(CNIC_F_CNIC_UP, &dev->flags))
		return -EALREADY;

	dev->regview = ethdev->io_base;
	pci_dev_get(dev->pcidev);
	cp->func = PCI_FUNC(dev->pcidev->devfn);
	cp->status_blk.gen = ethdev->irq_arr[0].status_blk;
	cp->status_blk_num = ethdev->irq_arr[0].status_blk_num;

	err = cp->alloc_resc(dev);
	if (err) {
		netdev_err(dev->netdev, "allocate resource failure\n");
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
	cp->free_resc(dev);
	pci_dev_put(dev->pcidev);
	return err;
}

static void cnic_stop_bnx2_hw(struct cnic_dev *dev)
{
	cnic_disable_bnx2_int_sync(dev);

	cnic_reg_wr_ind(dev, BNX2_CP_SCRATCH + 0x20, 0);
	cnic_reg_wr_ind(dev, BNX2_COM_SCRATCH + 0x20, 0);

	cnic_init_context(dev, KWQ_CID);
	cnic_init_context(dev, KCQ_CID);

	cnic_setup_5709_context(dev, 0);
	cnic_free_irq(dev);

	cnic_free_resc(dev);
}


static void cnic_stop_bnx2x_hw(struct cnic_dev *dev)
{
	struct cnic_local *cp = dev->cnic_priv;
	struct bnx2x *bp = netdev_priv(dev->netdev);
	u32 hc_index = HC_INDEX_ISCSI_EQ_CONS;
	u32 sb_id = cp->status_blk_num;
	u32 idx_off, syn_off;

	cnic_free_irq(dev);

	if (BNX2X_CHIP_IS_E2_PLUS(bp)) {
		idx_off = offsetof(struct hc_status_block_e2, index_values) +
			  (hc_index * sizeof(u16));

		syn_off = CSTORM_HC_SYNC_LINE_INDEX_E2_OFFSET(hc_index, sb_id);
	} else {
		idx_off = offsetof(struct hc_status_block_e1x, index_values) +
			  (hc_index * sizeof(u16));

		syn_off = CSTORM_HC_SYNC_LINE_INDEX_E1X_OFFSET(hc_index, sb_id);
	}
	CNIC_WR16(dev, BAR_CSTRORM_INTMEM + syn_off, 0);
	CNIC_WR16(dev, BAR_CSTRORM_INTMEM + CSTORM_STATUS_BLOCK_OFFSET(sb_id) +
		  idx_off, 0);

	*cp->kcq1.hw_prod_idx_ptr = 0;
	CNIC_WR(dev, BAR_CSTRORM_INTMEM +
		CSTORM_ISCSI_EQ_CONS_OFFSET(bp->pfid, 0), 0);
	CNIC_WR16(dev, cp->kcq1.io_addr, 0);
	cnic_free_resc(dev);
}

static void cnic_stop_hw(struct cnic_dev *dev)
{
	if (test_bit(CNIC_F_CNIC_UP, &dev->flags)) {
		struct cnic_local *cp = dev->cnic_priv;
		int i = 0;

		/* Need to wait for the ring shutdown event to complete
		 * before clearing the CNIC_UP flag.
		 */
		while (cp->udev && cp->udev->uio_dev != -1 && i < 15) {
			msleep(100);
			i++;
		}
		cnic_shutdown_rings(dev);
		cp->stop_cm(dev);
		cp->ethdev->drv_state &= ~CNIC_DRV_STATE_HANDLES_IRQ;
		clear_bit(CNIC_F_CNIC_UP, &dev->flags);
		RCU_INIT_POINTER(cp->ulp_ops[CNIC_ULP_L4], NULL);
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
		netdev_err(dev->netdev, "Failed waiting for ref count to go to zero\n");

	netdev_info(dev->netdev, "Removed CNIC device\n");
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

	cdev = kzalloc(alloc_size, GFP_KERNEL);
	if (cdev == NULL)
		return NULL;

	cdev->netdev = dev;
	cdev->cnic_priv = (char *)cdev + sizeof(struct cnic_dev);
	cdev->register_device = cnic_register_device;
	cdev->unregister_device = cnic_unregister_device;
	cdev->iscsi_nl_msg_recv = cnic_iscsi_nl_msg_recv;

	cp = cdev->cnic_priv;
	cp->dev = cdev;
	cp->l2_single_buf_size = 0x400;
	cp->l2_rx_ring_size = 3;

	spin_lock_init(&cp->cnic_ulp_lock);

	netdev_info(dev, "Added CNIC device\n");

	return cdev;
}

static struct cnic_dev *init_bnx2_cnic(struct net_device *dev)
{
	struct pci_dev *pdev;
	struct cnic_dev *cdev;
	struct cnic_local *cp;
	struct bnx2 *bp = netdev_priv(dev);
	struct cnic_eth_dev *ethdev = NULL;

	if (bp->cnic_probe)
		ethdev = (bp->cnic_probe)(dev);

	if (!ethdev)
		return NULL;

	pdev = ethdev->pdev;
	if (!pdev)
		return NULL;

	dev_hold(dev);
	pci_dev_get(pdev);
	if ((pdev->device == PCI_DEVICE_ID_NX2_5709 ||
	     pdev->device == PCI_DEVICE_ID_NX2_5709S) &&
	    (pdev->revision < 0x10)) {
		pci_dev_put(pdev);
		goto cnic_err;
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
	cp->chip_id = ethdev->chip_id;

	cdev->max_iscsi_conn = ethdev->max_iscsi_conn;

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
	return cdev;

cnic_err:
	dev_put(dev);
	return NULL;
}

static struct cnic_dev *init_bnx2x_cnic(struct net_device *dev)
{
	struct pci_dev *pdev;
	struct cnic_dev *cdev;
	struct cnic_local *cp;
	struct bnx2x *bp = netdev_priv(dev);
	struct cnic_eth_dev *ethdev = NULL;

	if (bp->cnic_probe)
		ethdev = bp->cnic_probe(dev);

	if (!ethdev)
		return NULL;

	pdev = ethdev->pdev;
	if (!pdev)
		return NULL;

	dev_hold(dev);
	cdev = cnic_alloc_dev(dev, pdev);
	if (cdev == NULL) {
		dev_put(dev);
		return NULL;
	}

	set_bit(CNIC_F_BNX2X_CLASS, &cdev->flags);
	cdev->submit_kwqes = cnic_submit_bnx2x_kwqes;

	cp = cdev->cnic_priv;
	cp->ethdev = ethdev;
	cdev->pcidev = pdev;
	cp->chip_id = ethdev->chip_id;

	cdev->stats_addr = ethdev->addr_drv_info_to_mcp;

	if (!(ethdev->drv_state & CNIC_DRV_STATE_NO_ISCSI))
		cdev->max_iscsi_conn = ethdev->max_iscsi_conn;
	if (CNIC_SUPPORTS_FCOE(bp)) {
		cdev->max_fcoe_conn = ethdev->max_fcoe_conn;
		cdev->max_fcoe_exchanges = ethdev->max_fcoe_exchanges;
	}

	if (cdev->max_fcoe_conn > BNX2X_FCOE_NUM_CONNECTIONS)
		cdev->max_fcoe_conn = BNX2X_FCOE_NUM_CONNECTIONS;

	memcpy(cdev->mac_addr, ethdev->iscsi_mac, ETH_ALEN);

	cp->cnic_ops = &cnic_bnx2x_ops;
	cp->start_hw = cnic_start_bnx2x_hw;
	cp->stop_hw = cnic_stop_bnx2x_hw;
	cp->setup_pgtbl = cnic_setup_page_tbl_le;
	cp->alloc_resc = cnic_alloc_bnx2x_resc;
	cp->free_resc = cnic_free_resc;
	cp->start_cm = cnic_cm_init_bnx2x_hw;
	cp->stop_cm = cnic_cm_stop_bnx2x_hw;
	cp->enable_int = cnic_enable_bnx2x_int;
	cp->disable_int_sync = cnic_disable_bnx2x_int_sync;
	if (BNX2X_CHIP_IS_E2_PLUS(bp)) {
		cp->ack_int = cnic_ack_bnx2x_e2_msix;
		cp->arm_int = cnic_arm_bnx2x_e2_msix;
	} else {
		cp->ack_int = cnic_ack_bnx2x_msix;
		cp->arm_int = cnic_arm_bnx2x_msix;
	}
	cp->close_conn = cnic_close_bnx2x_conn;
	return cdev;
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
		if (!strcmp(drvinfo.driver, "bnx2x"))
			cdev = init_bnx2x_cnic(dev);
		if (cdev) {
			write_lock(&cnic_dev_lock);
			list_add(&cdev->list, &cnic_dev_list);
			write_unlock(&cnic_dev_lock);
		}
	}
	return cdev;
}

static void cnic_rcv_netevent(struct cnic_local *cp, unsigned long event,
			      u16 vlan_id)
{
	int if_type;

	for (if_type = 0; if_type < MAX_CNIC_ULP_TYPE; if_type++) {
		struct cnic_ulp_ops *ulp_ops;
		void *ctx;

		mutex_lock(&cnic_lock);
		ulp_ops = rcu_dereference_protected(cp->ulp_ops[if_type],
						lockdep_is_held(&cnic_lock));
		if (!ulp_ops || !ulp_ops->indicate_netevent) {
			mutex_unlock(&cnic_lock);
			continue;
		}

		ctx = cp->ulp_handle[if_type];

		set_bit(ULP_F_CALL_PENDING, &cp->ulp_flags[if_type]);
		mutex_unlock(&cnic_lock);

		ulp_ops->indicate_netevent(ctx, event, vlan_id);

		clear_bit(ULP_F_CALL_PENDING, &cp->ulp_flags[if_type]);
	}
}

/* netdev event handler */
static int cnic_netdev_event(struct notifier_block *this, unsigned long event,
							 void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct cnic_dev *dev;
	int new_dev = 0;

	dev = cnic_from_netdev(netdev);

	if (!dev && event == NETDEV_REGISTER) {
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

		if (event == NETDEV_UP) {
			if (cnic_register_netdev(dev) != 0) {
				cnic_put(dev);
				goto done;
			}
			if (!cnic_start_hw(dev))
				cnic_ulp_start(dev);
		}

		cnic_rcv_netevent(cp, event, 0);

		if (event == NETDEV_GOING_DOWN) {
			cnic_ulp_stop(dev);
			cnic_stop_hw(dev);
			cnic_unregister_netdev(dev);
		} else if (event == NETDEV_UNREGISTER) {
			write_lock(&cnic_dev_lock);
			list_del_init(&dev->list);
			write_unlock(&cnic_dev_lock);

			cnic_put(dev);
			cnic_free_dev(dev);
			goto done;
		}
		cnic_put(dev);
	} else {
		struct net_device *realdev;
		u16 vid;

		vid = cnic_get_vlan(netdev, &realdev);
		if (realdev) {
			dev = cnic_from_netdev(realdev);
			if (dev) {
				vid |= VLAN_TAG_PRESENT;
				cnic_rcv_netevent(dev->cnic_priv, event, vid);
				cnic_put(dev);
			}
		}
	}
done:
	return NOTIFY_DONE;
}

static struct notifier_block cnic_netdev_notifier = {
	.notifier_call = cnic_netdev_event
};

static void cnic_release(void)
{
	struct cnic_uio_dev *udev;

	while (!list_empty(&cnic_udev_list)) {
		udev = list_entry(cnic_udev_list.next, struct cnic_uio_dev,
				  list);
		cnic_free_uio(udev);
	}
}

static int __init cnic_init(void)
{
	int rc = 0;

	pr_info("%s", version);

	rc = register_netdevice_notifier(&cnic_netdev_notifier);
	if (rc) {
		cnic_release();
		return rc;
	}

	cnic_wq = create_singlethread_workqueue("cnic_wq");
	if (!cnic_wq) {
		cnic_release();
		unregister_netdevice_notifier(&cnic_netdev_notifier);
		return -ENOMEM;
	}

	return 0;
}

static void __exit cnic_exit(void)
{
	unregister_netdevice_notifier(&cnic_netdev_notifier);
	cnic_release();
	destroy_workqueue(cnic_wq);
}

module_init(cnic_init);
module_exit(cnic_exit);
