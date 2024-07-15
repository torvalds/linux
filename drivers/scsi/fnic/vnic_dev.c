// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/if_ether.h>
#include <linux/slab.h>
#include "vnic_resource.h"
#include "vnic_devcmd.h"
#include "vnic_dev.h"
#include "vnic_stats.h"
#include "vnic_wq.h"

struct devcmd2_controller {
	struct vnic_wq_ctrl *wq_ctrl;
	struct vnic_dev_ring results_ring;
	struct vnic_wq wq;
	struct vnic_devcmd2 *cmd_ring;
	struct devcmd2_result *result;
	u16 next_result;
	u16 result_size;
	int color;
};

enum vnic_proxy_type {
	PROXY_NONE,
	PROXY_BY_BDF,
	PROXY_BY_INDEX,
};

struct vnic_res {
	void __iomem *vaddr;
	unsigned int count;
};

struct vnic_dev {
	void *priv;
	struct pci_dev *pdev;
	struct vnic_res res[RES_TYPE_MAX];
	enum vnic_dev_intr_mode intr_mode;
	struct vnic_devcmd __iomem *devcmd;
	struct vnic_devcmd_notify *notify;
	struct vnic_devcmd_notify notify_copy;
	dma_addr_t notify_pa;
	u32 *linkstatus;
	dma_addr_t linkstatus_pa;
	struct vnic_stats *stats;
	dma_addr_t stats_pa;
	struct vnic_devcmd_fw_info *fw_info;
	dma_addr_t fw_info_pa;
	enum vnic_proxy_type proxy;
	u32 proxy_index;
	u64 args[VNIC_DEVCMD_NARGS];
	struct devcmd2_controller *devcmd2;
	int (*devcmd_rtn)(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd,
			int wait);
};

#define VNIC_MAX_RES_HDR_SIZE \
	(sizeof(struct vnic_resource_header) + \
	sizeof(struct vnic_resource) * RES_TYPE_MAX)
#define VNIC_RES_STRIDE	128

void *vnic_dev_priv(struct vnic_dev *vdev)
{
	return vdev->priv;
}

static int vnic_dev_discover_res(struct vnic_dev *vdev,
	struct vnic_dev_bar *bar)
{
	struct vnic_resource_header __iomem *rh;
	struct vnic_resource __iomem *r;
	u8 type;

	if (bar->len < VNIC_MAX_RES_HDR_SIZE) {
		printk(KERN_ERR "vNIC BAR0 res hdr length error\n");
		return -EINVAL;
	}

	rh = bar->vaddr;
	if (!rh) {
		printk(KERN_ERR "vNIC BAR0 res hdr not mem-mapped\n");
		return -EINVAL;
	}

	if (ioread32(&rh->magic) != VNIC_RES_MAGIC ||
	    ioread32(&rh->version) != VNIC_RES_VERSION) {
		printk(KERN_ERR "vNIC BAR0 res magic/version error "
			"exp (%lx/%lx) curr (%x/%x)\n",
			VNIC_RES_MAGIC, VNIC_RES_VERSION,
			ioread32(&rh->magic), ioread32(&rh->version));
		return -EINVAL;
	}

	r = (struct vnic_resource __iomem *)(rh + 1);

	while ((type = ioread8(&r->type)) != RES_TYPE_EOL) {

		u8 bar_num = ioread8(&r->bar);
		u32 bar_offset = ioread32(&r->bar_offset);
		u32 count = ioread32(&r->count);
		u32 len;

		r++;

		if (bar_num != 0)  /* only mapping in BAR0 resources */
			continue;

		switch (type) {
		case RES_TYPE_WQ:
		case RES_TYPE_RQ:
		case RES_TYPE_CQ:
		case RES_TYPE_INTR_CTRL:
			/* each count is stride bytes long */
			len = count * VNIC_RES_STRIDE;
			if (len + bar_offset > bar->len) {
				printk(KERN_ERR "vNIC BAR0 resource %d "
					"out-of-bounds, offset 0x%x + "
					"size 0x%x > bar len 0x%lx\n",
					type, bar_offset,
					len,
					bar->len);
				return -EINVAL;
			}
			break;
		case RES_TYPE_INTR_PBA_LEGACY:
		case RES_TYPE_DEVCMD2:
		case RES_TYPE_DEVCMD:
			len = count;
			break;
		default:
			continue;
		}

		vdev->res[type].count = count;
		vdev->res[type].vaddr = (char __iomem *)bar->vaddr + bar_offset;
	}

	pr_info("res_type_wq: %d res_type_rq: %d res_type_cq: %d res_type_intr_ctrl: %d\n",
		vdev->res[RES_TYPE_WQ].count, vdev->res[RES_TYPE_RQ].count,
		vdev->res[RES_TYPE_CQ].count, vdev->res[RES_TYPE_INTR_CTRL].count);

	return 0;
}

unsigned int vnic_dev_get_res_count(struct vnic_dev *vdev,
	enum vnic_res_type type)
{
	return vdev->res[type].count;
}

void __iomem *vnic_dev_get_res(struct vnic_dev *vdev, enum vnic_res_type type,
	unsigned int index)
{
	if (!vdev->res[type].vaddr)
		return NULL;

	switch (type) {
	case RES_TYPE_WQ:
	case RES_TYPE_RQ:
	case RES_TYPE_CQ:
	case RES_TYPE_INTR_CTRL:
		return (char __iomem *)vdev->res[type].vaddr +
					index * VNIC_RES_STRIDE;
	default:
		return (char __iomem *)vdev->res[type].vaddr;
	}
}

unsigned int vnic_dev_desc_ring_size(struct vnic_dev_ring *ring,
				     unsigned int desc_count,
				     unsigned int desc_size)
{
	/* The base address of the desc rings must be 512 byte aligned.
	 * Descriptor count is aligned to groups of 32 descriptors.  A
	 * count of 0 means the maximum 4096 descriptors.  Descriptor
	 * size is aligned to 16 bytes.
	 */

	unsigned int count_align = 32;
	unsigned int desc_align = 16;

	ring->base_align = 512;

	if (desc_count == 0)
		desc_count = 4096;

	ring->desc_count = ALIGN(desc_count, count_align);

	ring->desc_size = ALIGN(desc_size, desc_align);

	ring->size = ring->desc_count * ring->desc_size;
	ring->size_unaligned = ring->size + ring->base_align;

	return ring->size_unaligned;
}

void vnic_dev_clear_desc_ring(struct vnic_dev_ring *ring)
{
	memset(ring->descs, 0, ring->size);
}

int vnic_dev_alloc_desc_ring(struct vnic_dev *vdev, struct vnic_dev_ring *ring,
	unsigned int desc_count, unsigned int desc_size)
{
	vnic_dev_desc_ring_size(ring, desc_count, desc_size);

	ring->descs_unaligned = dma_alloc_coherent(&vdev->pdev->dev,
		ring->size_unaligned,
		&ring->base_addr_unaligned, GFP_KERNEL);

	if (!ring->descs_unaligned) {
		printk(KERN_ERR
		  "Failed to allocate ring (size=%d), aborting\n",
			(int)ring->size);
		return -ENOMEM;
	}

	ring->base_addr = ALIGN(ring->base_addr_unaligned,
		ring->base_align);
	ring->descs = (u8 *)ring->descs_unaligned +
		(ring->base_addr - ring->base_addr_unaligned);

	vnic_dev_clear_desc_ring(ring);

	ring->desc_avail = ring->desc_count - 1;

	return 0;
}

void vnic_dev_free_desc_ring(struct vnic_dev *vdev, struct vnic_dev_ring *ring)
{
	if (ring->descs) {
		dma_free_coherent(&vdev->pdev->dev,
			ring->size_unaligned,
			ring->descs_unaligned,
			ring->base_addr_unaligned);
		ring->descs = NULL;
	}
}

static int vnic_dev_cmd1(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd, int wait)
{
	struct vnic_devcmd __iomem *devcmd = vdev->devcmd;
	int delay;
	u32 status;
	static const int dev_cmd_err[] = {
		/* convert from fw's version of error.h to host's version */
		0,	/* ERR_SUCCESS */
		EINVAL,	/* ERR_EINVAL */
		EFAULT,	/* ERR_EFAULT */
		EPERM,	/* ERR_EPERM */
		EBUSY,  /* ERR_EBUSY */
	};
	int err;
	u64 *a0 = &vdev->args[0];
	u64 *a1 = &vdev->args[1];

	status = ioread32(&devcmd->status);
	if (status & STAT_BUSY) {
		printk(KERN_ERR "Busy devcmd %d\n", _CMD_N(cmd));
		return -EBUSY;
	}

	if (_CMD_DIR(cmd) & _CMD_DIR_WRITE) {
		writeq(*a0, &devcmd->args[0]);
		writeq(*a1, &devcmd->args[1]);
		wmb();
	}

	iowrite32(cmd, &devcmd->cmd);

	if ((_CMD_FLAGS(cmd) & _CMD_FLAGS_NOWAIT))
			return 0;

	for (delay = 0; delay < wait; delay++) {

		udelay(100);

		status = ioread32(&devcmd->status);
		if (!(status & STAT_BUSY)) {

			if (status & STAT_ERROR) {
				err = dev_cmd_err[(int)readq(&devcmd->args[0])];
				printk(KERN_ERR "Error %d devcmd %d\n",
					err, _CMD_N(cmd));
				return -err;
			}

			if (_CMD_DIR(cmd) & _CMD_DIR_READ) {
				rmb();
				*a0 = readq(&devcmd->args[0]);
				*a1 = readq(&devcmd->args[1]);
			}

			return 0;
		}
	}

	printk(KERN_ERR "Timedout devcmd %d\n", _CMD_N(cmd));
	return -ETIMEDOUT;
}

static int vnic_dev_cmd2(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd,
		int wait)
{
	struct devcmd2_controller *dc2c = vdev->devcmd2;
	struct devcmd2_result *result;
	u8 color;
	unsigned int i;
	int delay;
	int err;
	u32 fetch_index;
	u32 posted;
	u32 new_posted;

	posted = ioread32(&dc2c->wq_ctrl->posted_index);
	fetch_index = ioread32(&dc2c->wq_ctrl->fetch_index);

	if (posted == 0xFFFFFFFF || fetch_index == 0xFFFFFFFF) {
		/* Hardware surprise removal: return error */
		pr_err("%s: devcmd2 invalid posted or fetch index on cmd %d\n",
				pci_name(vdev->pdev), _CMD_N(cmd));
		pr_err("%s: fetch index: %u, posted index: %u\n",
				pci_name(vdev->pdev), fetch_index, posted);

		return -ENODEV;

	}

	new_posted = (posted + 1) % DEVCMD2_RING_SIZE;

	if (new_posted == fetch_index) {
		pr_err("%s: devcmd2 wq full while issuing cmd %d\n",
				pci_name(vdev->pdev), _CMD_N(cmd));
		pr_err("%s: fetch index: %u, posted index: %u\n",
				pci_name(vdev->pdev), fetch_index, posted);
		return -EBUSY;

	}
	dc2c->cmd_ring[posted].cmd = cmd;
	dc2c->cmd_ring[posted].flags = 0;

	if ((_CMD_FLAGS(cmd) & _CMD_FLAGS_NOWAIT))
		dc2c->cmd_ring[posted].flags |= DEVCMD2_FNORESULT;
	if (_CMD_DIR(cmd) & _CMD_DIR_WRITE) {
		for (i = 0; i < VNIC_DEVCMD_NARGS; i++)
			dc2c->cmd_ring[posted].args[i] = vdev->args[i];

	}

	/* Adding write memory barrier prevents compiler and/or CPU
	 * reordering, thus avoiding descriptor posting before
	 * descriptor is initialized. Otherwise, hardware can read
	 * stale descriptor fields.
	 */
	wmb();
	iowrite32(new_posted, &dc2c->wq_ctrl->posted_index);

	if (dc2c->cmd_ring[posted].flags & DEVCMD2_FNORESULT)
		return 0;

	result = dc2c->result + dc2c->next_result;
	color = dc2c->color;

	dc2c->next_result++;
	if (dc2c->next_result == dc2c->result_size) {
		dc2c->next_result = 0;
		dc2c->color = dc2c->color ? 0 : 1;
	}

	for (delay = 0; delay < wait; delay++) {
		udelay(100);
		if (result->color == color) {
			if (result->error) {
				err = -(int) result->error;
				if (err != ERR_ECMDUNKNOWN ||
						cmd != CMD_CAPABILITY)
					pr_err("%s:Error %d devcmd %d\n",
						pci_name(vdev->pdev),
						err, _CMD_N(cmd));
				return err;
			}
			if (_CMD_DIR(cmd) & _CMD_DIR_READ) {
				rmb(); /*prevent reorder while reding result*/
				for (i = 0; i < VNIC_DEVCMD_NARGS; i++)
					vdev->args[i] = result->results[i];
			}
			return 0;
		}
	}

	pr_err("%s:Timed out devcmd %d\n", pci_name(vdev->pdev), _CMD_N(cmd));

	return -ETIMEDOUT;
}


static int vnic_dev_init_devcmd1(struct vnic_dev *vdev)
{
	vdev->devcmd = vnic_dev_get_res(vdev, RES_TYPE_DEVCMD, 0);
	if (!vdev->devcmd)
		return -ENODEV;

	vdev->devcmd_rtn = &vnic_dev_cmd1;
	return 0;
}


static int vnic_dev_init_devcmd2(struct vnic_dev *vdev)
{
	int err;
	unsigned int fetch_index;

	if (vdev->devcmd2)
		return 0;

	vdev->devcmd2 = kzalloc(sizeof(*vdev->devcmd2), GFP_ATOMIC);
	if (!vdev->devcmd2)
		return -ENOMEM;

	vdev->devcmd2->color = 1;
	vdev->devcmd2->result_size = DEVCMD2_RING_SIZE;
	err = vnic_wq_devcmd2_alloc(vdev, &vdev->devcmd2->wq,
				DEVCMD2_RING_SIZE, DEVCMD2_DESC_SIZE);
	if (err)
		goto err_free_devcmd2;

	fetch_index = ioread32(&vdev->devcmd2->wq.ctrl->fetch_index);
	if (fetch_index == 0xFFFFFFFF) { /* check for hardware gone  */
		pr_err("error in devcmd2 init");
		err = -ENODEV;
		goto err_free_wq;
	}

	/*
	 * Don't change fetch_index ever and
	 * set posted_index same as fetch_index
	 * when setting up the WQ for devcmd2.
	 */
	vnic_wq_init_start(&vdev->devcmd2->wq, 0, fetch_index,
			fetch_index, 0, 0);

	vnic_wq_enable(&vdev->devcmd2->wq);

	err = vnic_dev_alloc_desc_ring(vdev, &vdev->devcmd2->results_ring,
			DEVCMD2_RING_SIZE, DEVCMD2_DESC_SIZE);
	if (err)
		goto err_disable_wq;

	vdev->devcmd2->result =
		(struct devcmd2_result *) vdev->devcmd2->results_ring.descs;
	vdev->devcmd2->cmd_ring =
		(struct vnic_devcmd2 *) vdev->devcmd2->wq.ring.descs;
	vdev->devcmd2->wq_ctrl = vdev->devcmd2->wq.ctrl;
	vdev->args[0] = (u64) vdev->devcmd2->results_ring.base_addr |
				VNIC_PADDR_TARGET;
	vdev->args[1] = DEVCMD2_RING_SIZE;

	err = vnic_dev_cmd2(vdev, CMD_INITIALIZE_DEVCMD2, 1000);
	if (err)
		goto err_free_desc_ring;

	vdev->devcmd_rtn = &vnic_dev_cmd2;

	return 0;

err_free_desc_ring:
	vnic_dev_free_desc_ring(vdev, &vdev->devcmd2->results_ring);
err_disable_wq:
	vnic_wq_disable(&vdev->devcmd2->wq);
err_free_wq:
	vnic_wq_free(&vdev->devcmd2->wq);
err_free_devcmd2:
	kfree(vdev->devcmd2);
	vdev->devcmd2 = NULL;

	return err;
}


static void vnic_dev_deinit_devcmd2(struct vnic_dev *vdev)
{
	vnic_dev_free_desc_ring(vdev, &vdev->devcmd2->results_ring);
	vnic_wq_disable(&vdev->devcmd2->wq);
	vnic_wq_free(&vdev->devcmd2->wq);
	kfree(vdev->devcmd2);
	vdev->devcmd2 = NULL;
	vdev->devcmd_rtn = &vnic_dev_cmd1;
}


static int vnic_dev_cmd_no_proxy(struct vnic_dev *vdev,
	enum vnic_devcmd_cmd cmd, u64 *a0, u64 *a1, int wait)
{
	int err;

	vdev->args[0] = *a0;
	vdev->args[1] = *a1;

	err = (*vdev->devcmd_rtn)(vdev, cmd, wait);

	*a0 = vdev->args[0];
	*a1 = vdev->args[1];

	return err;
}


int vnic_dev_cmd(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd,
	u64 *a0, u64 *a1, int wait)
{
	memset(vdev->args, 0, sizeof(vdev->args));

	switch (vdev->proxy) {
	case PROXY_NONE:
	default:
		return vnic_dev_cmd_no_proxy(vdev, cmd, a0, a1, wait);
	}
}


int vnic_dev_fw_info(struct vnic_dev *vdev,
	struct vnic_devcmd_fw_info **fw_info)
{
	u64 a0, a1 = 0;
	int wait = 1000;
	int err = 0;

	if (!vdev->fw_info) {
		vdev->fw_info = dma_alloc_coherent(&vdev->pdev->dev,
			sizeof(struct vnic_devcmd_fw_info),
			&vdev->fw_info_pa, GFP_KERNEL);
		if (!vdev->fw_info)
			return -ENOMEM;

		a0 = vdev->fw_info_pa;

		/* only get fw_info once and cache it */
		err = vnic_dev_cmd(vdev, CMD_MCPU_FW_INFO, &a0, &a1, wait);
	}

	*fw_info = vdev->fw_info;

	return err;
}

int vnic_dev_spec(struct vnic_dev *vdev, unsigned int offset, unsigned int size,
	void *value)
{
	u64 a0, a1;
	int wait = 1000;
	int err;

	a0 = offset;
	a1 = size;

	err = vnic_dev_cmd(vdev, CMD_DEV_SPEC, &a0, &a1, wait);

	switch (size) {
	case 1:
		*(u8 *)value = (u8)a0;
		break;
	case 2:
		*(u16 *)value = (u16)a0;
		break;
	case 4:
		*(u32 *)value = (u32)a0;
		break;
	case 8:
		*(u64 *)value = a0;
		break;
	default:
		BUG();
		break;
	}

	return err;
}

int vnic_dev_stats_clear(struct vnic_dev *vdev)
{
	u64 a0 = 0, a1 = 0;
	int wait = 1000;
	return vnic_dev_cmd(vdev, CMD_STATS_CLEAR, &a0, &a1, wait);
}

int vnic_dev_stats_dump(struct vnic_dev *vdev, struct vnic_stats **stats)
{
	u64 a0, a1;
	int wait = 1000;

	if (!vdev->stats) {
		vdev->stats = dma_alloc_coherent(&vdev->pdev->dev,
			sizeof(struct vnic_stats), &vdev->stats_pa, GFP_KERNEL);
		if (!vdev->stats)
			return -ENOMEM;
	}

	*stats = vdev->stats;
	a0 = vdev->stats_pa;
	a1 = sizeof(struct vnic_stats);

	return vnic_dev_cmd(vdev, CMD_STATS_DUMP, &a0, &a1, wait);
}

int vnic_dev_close(struct vnic_dev *vdev)
{
	u64 a0 = 0, a1 = 0;
	int wait = 1000;
	return vnic_dev_cmd(vdev, CMD_CLOSE, &a0, &a1, wait);
}

int vnic_dev_enable(struct vnic_dev *vdev)
{
	u64 a0 = 0, a1 = 0;
	int wait = 1000;
	return vnic_dev_cmd(vdev, CMD_ENABLE, &a0, &a1, wait);
}

int vnic_dev_disable(struct vnic_dev *vdev)
{
	u64 a0 = 0, a1 = 0;
	int wait = 1000;
	return vnic_dev_cmd(vdev, CMD_DISABLE, &a0, &a1, wait);
}

int vnic_dev_open(struct vnic_dev *vdev, int arg)
{
	u64 a0 = (u32)arg, a1 = 0;
	int wait = 1000;
	return vnic_dev_cmd(vdev, CMD_OPEN, &a0, &a1, wait);
}

int vnic_dev_open_done(struct vnic_dev *vdev, int *done)
{
	u64 a0 = 0, a1 = 0;
	int wait = 1000;
	int err;

	*done = 0;

	err = vnic_dev_cmd(vdev, CMD_OPEN_STATUS, &a0, &a1, wait);
	if (err)
		return err;

	*done = (a0 == 0);

	return 0;
}

int vnic_dev_soft_reset(struct vnic_dev *vdev, int arg)
{
	u64 a0 = (u32)arg, a1 = 0;
	int wait = 1000;
	return vnic_dev_cmd(vdev, CMD_SOFT_RESET, &a0, &a1, wait);
}

int vnic_dev_soft_reset_done(struct vnic_dev *vdev, int *done)
{
	u64 a0 = 0, a1 = 0;
	int wait = 1000;
	int err;

	*done = 0;

	err = vnic_dev_cmd(vdev, CMD_SOFT_RESET_STATUS, &a0, &a1, wait);
	if (err)
		return err;

	*done = (a0 == 0);

	return 0;
}

int vnic_dev_hang_notify(struct vnic_dev *vdev)
{
	u64 a0 = 0, a1 = 0;
	int wait = 1000;
	return vnic_dev_cmd(vdev, CMD_HANG_NOTIFY, &a0, &a1, wait);
}

int vnic_dev_mac_addr(struct vnic_dev *vdev, u8 *mac_addr)
{
	u64 a[2] = {};
	int wait = 1000;
	int err, i;

	for (i = 0; i < ETH_ALEN; i++)
		mac_addr[i] = 0;

	err = vnic_dev_cmd(vdev, CMD_MAC_ADDR, &a[0], &a[1], wait);
	if (err)
		return err;

	for (i = 0; i < ETH_ALEN; i++)
		mac_addr[i] = ((u8 *)&a)[i];

	return 0;
}

void vnic_dev_packet_filter(struct vnic_dev *vdev, int directed, int multicast,
	int broadcast, int promisc, int allmulti)
{
	u64 a0, a1 = 0;
	int wait = 1000;
	int err;

	a0 = (directed ? CMD_PFILTER_DIRECTED : 0) |
	     (multicast ? CMD_PFILTER_MULTICAST : 0) |
	     (broadcast ? CMD_PFILTER_BROADCAST : 0) |
	     (promisc ? CMD_PFILTER_PROMISCUOUS : 0) |
	     (allmulti ? CMD_PFILTER_ALL_MULTICAST : 0);

	err = vnic_dev_cmd(vdev, CMD_PACKET_FILTER, &a0, &a1, wait);
	if (err)
		printk(KERN_ERR "Can't set packet filter\n");
}

void vnic_dev_add_addr(struct vnic_dev *vdev, u8 *addr)
{
	u64 a[2] = {};
	int wait = 1000;
	int err;
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		((u8 *)&a)[i] = addr[i];

	err = vnic_dev_cmd(vdev, CMD_ADDR_ADD, &a[0], &a[1], wait);
	if (err)
		pr_err("Can't add addr [%pM], %d\n", addr, err);
}

void vnic_dev_del_addr(struct vnic_dev *vdev, u8 *addr)
{
	u64 a[2] = {};
	int wait = 1000;
	int err;
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		((u8 *)&a)[i] = addr[i];

	err = vnic_dev_cmd(vdev, CMD_ADDR_DEL, &a[0], &a[1], wait);
	if (err)
		pr_err("Can't del addr [%pM], %d\n", addr, err);
}

int vnic_dev_notify_set(struct vnic_dev *vdev, u16 intr)
{
	u64 a0, a1;
	int wait = 1000;

	if (!vdev->notify) {
		vdev->notify = dma_alloc_coherent(&vdev->pdev->dev,
			sizeof(struct vnic_devcmd_notify),
			&vdev->notify_pa, GFP_KERNEL);
		if (!vdev->notify)
			return -ENOMEM;
	}

	a0 = vdev->notify_pa;
	a1 = ((u64)intr << 32) & 0x0000ffff00000000ULL;
	a1 += sizeof(struct vnic_devcmd_notify);

	return vnic_dev_cmd(vdev, CMD_NOTIFY, &a0, &a1, wait);
}

void vnic_dev_notify_unset(struct vnic_dev *vdev)
{
	u64 a0, a1;
	int wait = 1000;

	a0 = 0;  /* paddr = 0 to unset notify buffer */
	a1 = 0x0000ffff00000000ULL; /* intr num = -1 to unreg for intr */
	a1 += sizeof(struct vnic_devcmd_notify);

	vnic_dev_cmd(vdev, CMD_NOTIFY, &a0, &a1, wait);
}

static int vnic_dev_notify_ready(struct vnic_dev *vdev)
{
	u32 *words;
	unsigned int nwords = sizeof(struct vnic_devcmd_notify) / 4;
	unsigned int i;
	u32 csum;

	if (!vdev->notify)
		return 0;

	do {
		csum = 0;
		memcpy(&vdev->notify_copy, vdev->notify,
			sizeof(struct vnic_devcmd_notify));
		words = (u32 *)&vdev->notify_copy;
		for (i = 1; i < nwords; i++)
			csum += words[i];
	} while (csum != words[0]);

	return 1;
}

int vnic_dev_init(struct vnic_dev *vdev, int arg)
{
	u64 a0 = (u32)arg, a1 = 0;
	int wait = 1000;
	return vnic_dev_cmd(vdev, CMD_INIT, &a0, &a1, wait);
}

u16 vnic_dev_set_default_vlan(struct vnic_dev *vdev, u16 new_default_vlan)
{
	u64 a0 = new_default_vlan, a1 = 0;
	int wait = 1000;
	int old_vlan = 0;

	old_vlan = vnic_dev_cmd(vdev, CMD_SET_DEFAULT_VLAN, &a0, &a1, wait);
	return (u16)old_vlan;
}

int vnic_dev_link_status(struct vnic_dev *vdev)
{
	if (vdev->linkstatus)
		return *vdev->linkstatus;

	if (!vnic_dev_notify_ready(vdev))
		return 0;

	return vdev->notify_copy.link_state;
}

u32 vnic_dev_port_speed(struct vnic_dev *vdev)
{
	if (!vnic_dev_notify_ready(vdev))
		return 0;

	return vdev->notify_copy.port_speed;
}

u32 vnic_dev_msg_lvl(struct vnic_dev *vdev)
{
	if (!vnic_dev_notify_ready(vdev))
		return 0;

	return vdev->notify_copy.msglvl;
}

u32 vnic_dev_mtu(struct vnic_dev *vdev)
{
	if (!vnic_dev_notify_ready(vdev))
		return 0;

	return vdev->notify_copy.mtu;
}

u32 vnic_dev_link_down_cnt(struct vnic_dev *vdev)
{
	if (!vnic_dev_notify_ready(vdev))
		return 0;

	return vdev->notify_copy.link_down_cnt;
}

void vnic_dev_set_intr_mode(struct vnic_dev *vdev,
	enum vnic_dev_intr_mode intr_mode)
{
	vdev->intr_mode = intr_mode;
}

enum vnic_dev_intr_mode vnic_dev_get_intr_mode(
	struct vnic_dev *vdev)
{
	return vdev->intr_mode;
}

void vnic_dev_unregister(struct vnic_dev *vdev)
{
	if (vdev) {
		if (vdev->notify)
			dma_free_coherent(&vdev->pdev->dev,
				sizeof(struct vnic_devcmd_notify),
				vdev->notify,
				vdev->notify_pa);
		if (vdev->linkstatus)
			dma_free_coherent(&vdev->pdev->dev,
				sizeof(u32),
				vdev->linkstatus,
				vdev->linkstatus_pa);
		if (vdev->stats)
			dma_free_coherent(&vdev->pdev->dev,
				sizeof(struct vnic_stats),
				vdev->stats, vdev->stats_pa);
		if (vdev->fw_info)
			dma_free_coherent(&vdev->pdev->dev,
				sizeof(struct vnic_devcmd_fw_info),
				vdev->fw_info, vdev->fw_info_pa);
		if (vdev->devcmd2)
			vnic_dev_deinit_devcmd2(vdev);
		kfree(vdev);
	}
}

struct vnic_dev *vnic_dev_register(struct vnic_dev *vdev,
	void *priv, struct pci_dev *pdev, struct vnic_dev_bar *bar)
{
	if (!vdev) {
		vdev = kzalloc(sizeof(struct vnic_dev), GFP_KERNEL);
		if (!vdev)
			return NULL;
	}

	vdev->priv = priv;
	vdev->pdev = pdev;

	if (vnic_dev_discover_res(vdev, bar))
		goto err_out;

	return vdev;

err_out:
	vnic_dev_unregister(vdev);
	return NULL;
}

int vnic_dev_cmd_init(struct vnic_dev *vdev)
{
	int err;
	void *p;

	p = vnic_dev_get_res(vdev, RES_TYPE_DEVCMD2, 0);
	if (p) {
		pr_err("fnic: DEVCMD2 resource found!\n");
		err = vnic_dev_init_devcmd2(vdev);
	} else {
		pr_err("fnic: DEVCMD2 not found, fall back to Devcmd\n");
		err = vnic_dev_init_devcmd1(vdev);
	}

	return err;
}
