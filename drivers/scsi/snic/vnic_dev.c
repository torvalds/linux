// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2014 Cisco Systems, Inc.  All rights reserved.

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

#define VNIC_DVCMD_TMO	10000	/* Devcmd Timeout value */
#define VNIC_NOTIFY_INTR_MASK 0x0000ffff00000000ULL

struct devcmd2_controller {
	struct vnic_wq_ctrl __iomem *wq_ctrl;
	struct vnic_dev_ring results_ring;
	struct vnic_wq wq;
	struct vnic_devcmd2 *cmd_ring;
	struct devcmd2_result *result;
	u16 next_result;
	u16 result_size;
	int color;
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
	u64 args[VNIC_DEVCMD_NARGS];
	struct devcmd2_controller *devcmd2;

	int (*devcmd_rtn)(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd,
			  int wait);
};

#define VNIC_MAX_RES_HDR_SIZE \
	(sizeof(struct vnic_resource_header) + \
	sizeof(struct vnic_resource) * RES_TYPE_MAX)
#define VNIC_RES_STRIDE	128

void *svnic_dev_priv(struct vnic_dev *vdev)
{
	return vdev->priv;
}

static int vnic_dev_discover_res(struct vnic_dev *vdev,
	struct vnic_dev_bar *bar, unsigned int num_bars)
{
	struct vnic_resource_header __iomem *rh;
	struct vnic_resource __iomem *r;
	u8 type;

	if (num_bars == 0)
		return -EINVAL;

	if (bar->len < VNIC_MAX_RES_HDR_SIZE) {
		pr_err("vNIC BAR0 res hdr length error\n");

		return -EINVAL;
	}

	rh = bar->vaddr;
	if (!rh) {
		pr_err("vNIC BAR0 res hdr not mem-mapped\n");

		return -EINVAL;
	}

	if (ioread32(&rh->magic) != VNIC_RES_MAGIC ||
	    ioread32(&rh->version) != VNIC_RES_VERSION) {
		pr_err("vNIC BAR0 res magic/version error exp (%lx/%lx) curr (%x/%x)\n",
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

		if (bar_num >= num_bars)
			continue;

		if (!bar[bar_num].len || !bar[bar_num].vaddr)
			continue;

		switch (type) {
		case RES_TYPE_WQ:
		case RES_TYPE_RQ:
		case RES_TYPE_CQ:
		case RES_TYPE_INTR_CTRL:
			/* each count is stride bytes long */
			len = count * VNIC_RES_STRIDE;
			if (len + bar_offset > bar->len) {
				pr_err("vNIC BAR0 resource %d out-of-bounds, offset 0x%x + size 0x%x > bar len 0x%lx\n",
					type, bar_offset,
					len,
					bar->len);

				return -EINVAL;
			}
			break;

		case RES_TYPE_INTR_PBA_LEGACY:
		case RES_TYPE_DEVCMD:
		case RES_TYPE_DEVCMD2:
			len = count;
			break;

		default:
			continue;
		}

		vdev->res[type].count = count;
		vdev->res[type].vaddr = (char __iomem *)bar->vaddr + bar_offset;
	}

	return 0;
}

unsigned int svnic_dev_get_res_count(struct vnic_dev *vdev,
	enum vnic_res_type type)
{
	return vdev->res[type].count;
}

void __iomem *svnic_dev_get_res(struct vnic_dev *vdev, enum vnic_res_type type,
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

unsigned int svnic_dev_desc_ring_size(struct vnic_dev_ring *ring,
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

void svnic_dev_clear_desc_ring(struct vnic_dev_ring *ring)
{
	memset(ring->descs, 0, ring->size);
}

int svnic_dev_alloc_desc_ring(struct vnic_dev *vdev, struct vnic_dev_ring *ring,
	unsigned int desc_count, unsigned int desc_size)
{
	svnic_dev_desc_ring_size(ring, desc_count, desc_size);

	ring->descs_unaligned = dma_alloc_coherent(&vdev->pdev->dev,
			ring->size_unaligned, &ring->base_addr_unaligned,
			GFP_KERNEL);
	if (!ring->descs_unaligned) {
		pr_err("Failed to allocate ring (size=%d), aborting\n",
			(int)ring->size);

		return -ENOMEM;
	}

	ring->base_addr = ALIGN(ring->base_addr_unaligned,
		ring->base_align);
	ring->descs = (u8 *)ring->descs_unaligned +
		(ring->base_addr - ring->base_addr_unaligned);

	svnic_dev_clear_desc_ring(ring);

	ring->desc_avail = ring->desc_count - 1;

	return 0;
}

void svnic_dev_free_desc_ring(struct vnic_dev *vdev, struct vnic_dev_ring *ring)
{
	if (ring->descs) {
		dma_free_coherent(&vdev->pdev->dev,
			ring->size_unaligned,
			ring->descs_unaligned,
			ring->base_addr_unaligned);
		ring->descs = NULL;
	}
}

static int _svnic_dev_cmd2(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd,
	int wait)
{
	struct devcmd2_controller *dc2c = vdev->devcmd2;
	struct devcmd2_result *result = NULL;
	unsigned int i;
	int delay;
	int err;
	u32 posted;
	u32 fetch_idx;
	u32 new_posted;
	u8 color;

	fetch_idx = ioread32(&dc2c->wq_ctrl->fetch_index);
	if (fetch_idx == 0xFFFFFFFF) { /* check for hardware gone  */
		/* Hardware surprise removal: return error */
		return -ENODEV;
	}

	posted = ioread32(&dc2c->wq_ctrl->posted_index);

	if (posted == 0xFFFFFFFF) { /* check for hardware gone  */
		/* Hardware surprise removal: return error */
		return -ENODEV;
	}

	new_posted = (posted + 1) % DEVCMD2_RING_SIZE;
	if (new_posted == fetch_idx) {
		pr_err("%s: wq is full while issuing devcmd2 command %d, fetch index: %u, posted index: %u\n",
			pci_name(vdev->pdev), _CMD_N(cmd), fetch_idx, posted);

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

	/*
	 * Increment next_result, after posting the devcmd, irrespective of
	 * devcmd result, and it should be done only once.
	 */
	dc2c->next_result++;
	if (dc2c->next_result == dc2c->result_size) {
		dc2c->next_result = 0;
		dc2c->color = dc2c->color ? 0 : 1;
	}

	for (delay = 0; delay < wait; delay++) {
		udelay(100);
		if (result->color == color) {
			if (result->error) {
				err = (int) result->error;
				if (err != ERR_ECMDUNKNOWN ||
				    cmd != CMD_CAPABILITY)
					pr_err("Error %d devcmd %d\n",
						err, _CMD_N(cmd));

				return err;
			}
			if (_CMD_DIR(cmd) & _CMD_DIR_READ) {
				for (i = 0; i < VNIC_DEVCMD_NARGS; i++)
					vdev->args[i] = result->results[i];
			}

			return 0;
		}
	}

	pr_err("Timed out devcmd %d\n", _CMD_N(cmd));

	return -ETIMEDOUT;
}

static int svnic_dev_init_devcmd2(struct vnic_dev *vdev)
{
	struct devcmd2_controller *dc2c = NULL;
	unsigned int fetch_idx;
	int ret;
	void __iomem *p;

	if (vdev->devcmd2)
		return 0;

	p = svnic_dev_get_res(vdev, RES_TYPE_DEVCMD2, 0);
	if (!p)
		return -ENODEV;

	dc2c = kzalloc(sizeof(*dc2c), GFP_ATOMIC);
	if (!dc2c)
		return -ENOMEM;

	vdev->devcmd2 = dc2c;

	dc2c->color = 1;
	dc2c->result_size = DEVCMD2_RING_SIZE;

	ret  = vnic_wq_devcmd2_alloc(vdev,
				     &dc2c->wq,
				     DEVCMD2_RING_SIZE,
				     DEVCMD2_DESC_SIZE);
	if (ret)
		goto err_free_devcmd2;

	fetch_idx = ioread32(&dc2c->wq.ctrl->fetch_index);
	if (fetch_idx == 0xFFFFFFFF) { /* check for hardware gone  */
		/* Hardware surprise removal: reset fetch_index */
		fetch_idx = 0;
	}

	/*
	 * Don't change fetch_index ever and
	 * set posted_index same as fetch_index
	 * when setting up the WQ for devcmd2.
	 */
	vnic_wq_init_start(&dc2c->wq, 0, fetch_idx, fetch_idx, 0, 0);
	svnic_wq_enable(&dc2c->wq);
	ret = svnic_dev_alloc_desc_ring(vdev,
					&dc2c->results_ring,
					DEVCMD2_RING_SIZE,
					DEVCMD2_DESC_SIZE);
	if (ret)
		goto err_free_wq;

	dc2c->result = (struct devcmd2_result *) dc2c->results_ring.descs;
	dc2c->cmd_ring = (struct vnic_devcmd2 *) dc2c->wq.ring.descs;
	dc2c->wq_ctrl = dc2c->wq.ctrl;
	vdev->args[0] = (u64) dc2c->results_ring.base_addr | VNIC_PADDR_TARGET;
	vdev->args[1] = DEVCMD2_RING_SIZE;

	ret = _svnic_dev_cmd2(vdev, CMD_INITIALIZE_DEVCMD2, VNIC_DVCMD_TMO);
	if (ret < 0)
		goto err_free_desc_ring;

	vdev->devcmd_rtn = &_svnic_dev_cmd2;
	pr_info("DEVCMD2 Initialized.\n");

	return ret;

err_free_desc_ring:
	svnic_dev_free_desc_ring(vdev, &dc2c->results_ring);

err_free_wq:
	svnic_wq_disable(&dc2c->wq);
	svnic_wq_free(&dc2c->wq);

err_free_devcmd2:
	kfree(dc2c);
	vdev->devcmd2 = NULL;

	return ret;
} /* end of svnic_dev_init_devcmd2 */

static void vnic_dev_deinit_devcmd2(struct vnic_dev *vdev)
{
	struct devcmd2_controller *dc2c = vdev->devcmd2;

	vdev->devcmd2 = NULL;
	vdev->devcmd_rtn = NULL;

	svnic_dev_free_desc_ring(vdev, &dc2c->results_ring);
	svnic_wq_disable(&dc2c->wq);
	svnic_wq_free(&dc2c->wq);
	kfree(dc2c);
}

int svnic_dev_cmd(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd,
	u64 *a0, u64 *a1, int wait)
{
	int err;

	memset(vdev->args, 0, sizeof(vdev->args));
	vdev->args[0] = *a0;
	vdev->args[1] = *a1;

	err = (*vdev->devcmd_rtn)(vdev, cmd, wait);

	*a0 = vdev->args[0];
	*a1 = vdev->args[1];

	return  err;
}

int svnic_dev_fw_info(struct vnic_dev *vdev,
	struct vnic_devcmd_fw_info **fw_info)
{
	u64 a0, a1 = 0;
	int wait = VNIC_DVCMD_TMO;
	int err = 0;

	if (!vdev->fw_info) {
		vdev->fw_info = dma_alloc_coherent(&vdev->pdev->dev,
			sizeof(struct vnic_devcmd_fw_info),
			&vdev->fw_info_pa, GFP_KERNEL);
		if (!vdev->fw_info)
			return -ENOMEM;

		a0 = vdev->fw_info_pa;

		/* only get fw_info once and cache it */
		err = svnic_dev_cmd(vdev, CMD_MCPU_FW_INFO, &a0, &a1, wait);
	}

	*fw_info = vdev->fw_info;

	return err;
}

int svnic_dev_spec(struct vnic_dev *vdev, unsigned int offset,
	unsigned int size, void *value)
{
	u64 a0, a1;
	int wait = VNIC_DVCMD_TMO;
	int err;

	a0 = offset;
	a1 = size;

	err = svnic_dev_cmd(vdev, CMD_DEV_SPEC, &a0, &a1, wait);

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

int svnic_dev_stats_clear(struct vnic_dev *vdev)
{
	u64 a0 = 0, a1 = 0;
	int wait = VNIC_DVCMD_TMO;

	return svnic_dev_cmd(vdev, CMD_STATS_CLEAR, &a0, &a1, wait);
}

int svnic_dev_stats_dump(struct vnic_dev *vdev, struct vnic_stats **stats)
{
	u64 a0, a1;
	int wait = VNIC_DVCMD_TMO;

	if (!vdev->stats) {
		vdev->stats = dma_alloc_coherent(&vdev->pdev->dev,
			sizeof(struct vnic_stats), &vdev->stats_pa, GFP_KERNEL);
		if (!vdev->stats)
			return -ENOMEM;
	}

	*stats = vdev->stats;
	a0 = vdev->stats_pa;
	a1 = sizeof(struct vnic_stats);

	return svnic_dev_cmd(vdev, CMD_STATS_DUMP, &a0, &a1, wait);
}

int svnic_dev_close(struct vnic_dev *vdev)
{
	u64 a0 = 0, a1 = 0;
	int wait = VNIC_DVCMD_TMO;

	return svnic_dev_cmd(vdev, CMD_CLOSE, &a0, &a1, wait);
}

int svnic_dev_enable_wait(struct vnic_dev *vdev)
{
	u64 a0 = 0, a1 = 0;
	int wait = VNIC_DVCMD_TMO;
	int err = 0;

	err = svnic_dev_cmd(vdev, CMD_ENABLE_WAIT, &a0, &a1, wait);
	if (err == ERR_ECMDUNKNOWN)
		return svnic_dev_cmd(vdev, CMD_ENABLE, &a0, &a1, wait);

	return err;
}

int svnic_dev_disable(struct vnic_dev *vdev)
{
	u64 a0 = 0, a1 = 0;
	int wait = VNIC_DVCMD_TMO;

	return svnic_dev_cmd(vdev, CMD_DISABLE, &a0, &a1, wait);
}

int svnic_dev_open(struct vnic_dev *vdev, int arg)
{
	u64 a0 = (u32)arg, a1 = 0;
	int wait = VNIC_DVCMD_TMO;

	return svnic_dev_cmd(vdev, CMD_OPEN, &a0, &a1, wait);
}

int svnic_dev_open_done(struct vnic_dev *vdev, int *done)
{
	u64 a0 = 0, a1 = 0;
	int wait = VNIC_DVCMD_TMO;
	int err;

	*done = 0;

	err = svnic_dev_cmd(vdev, CMD_OPEN_STATUS, &a0, &a1, wait);
	if (err)
		return err;

	*done = (a0 == 0);

	return 0;
}

int svnic_dev_notify_set(struct vnic_dev *vdev, u16 intr)
{
	u64 a0, a1;
	int wait = VNIC_DVCMD_TMO;

	if (!vdev->notify) {
		vdev->notify = dma_alloc_coherent(&vdev->pdev->dev,
			sizeof(struct vnic_devcmd_notify),
			&vdev->notify_pa, GFP_KERNEL);
		if (!vdev->notify)
			return -ENOMEM;
	}

	a0 = vdev->notify_pa;
	a1 = ((u64)intr << 32) & VNIC_NOTIFY_INTR_MASK;
	a1 += sizeof(struct vnic_devcmd_notify);

	return svnic_dev_cmd(vdev, CMD_NOTIFY, &a0, &a1, wait);
}

void svnic_dev_notify_unset(struct vnic_dev *vdev)
{
	u64 a0, a1;
	int wait = VNIC_DVCMD_TMO;

	a0 = 0;  /* paddr = 0 to unset notify buffer */
	a1 = VNIC_NOTIFY_INTR_MASK; /* intr num = -1 to unreg for intr */
	a1 += sizeof(struct vnic_devcmd_notify);

	svnic_dev_cmd(vdev, CMD_NOTIFY, &a0, &a1, wait);
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

int svnic_dev_init(struct vnic_dev *vdev, int arg)
{
	u64 a0 = (u32)arg, a1 = 0;
	int wait = VNIC_DVCMD_TMO;

	return svnic_dev_cmd(vdev, CMD_INIT, &a0, &a1, wait);
}

int svnic_dev_link_status(struct vnic_dev *vdev)
{
	if (vdev->linkstatus)
		return *vdev->linkstatus;

	if (!vnic_dev_notify_ready(vdev))
		return 0;

	return vdev->notify_copy.link_state;
}

u32 svnic_dev_link_down_cnt(struct vnic_dev *vdev)
{
	if (!vnic_dev_notify_ready(vdev))
		return 0;

	return vdev->notify_copy.link_down_cnt;
}

void svnic_dev_set_intr_mode(struct vnic_dev *vdev,
	enum vnic_dev_intr_mode intr_mode)
{
	vdev->intr_mode = intr_mode;
}

enum vnic_dev_intr_mode svnic_dev_get_intr_mode(struct vnic_dev *vdev)
{
	return vdev->intr_mode;
}

void svnic_dev_unregister(struct vnic_dev *vdev)
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

struct vnic_dev *svnic_dev_alloc_discover(struct vnic_dev *vdev,
					  void *priv,
					  struct pci_dev *pdev,
					  struct vnic_dev_bar *bar,
					  unsigned int num_bars)
{
	if (!vdev) {
		vdev = kzalloc(sizeof(struct vnic_dev), GFP_ATOMIC);
		if (!vdev)
			return NULL;
	}

	vdev->priv = priv;
	vdev->pdev = pdev;

	if (vnic_dev_discover_res(vdev, bar, num_bars))
		goto err_out;

	return vdev;

err_out:
	svnic_dev_unregister(vdev);

	return NULL;
} /* end of svnic_dev_alloc_discover */

/*
 * fallback option is left to keep the interface common for other vnics.
 */
int svnic_dev_cmd_init(struct vnic_dev *vdev, int fallback)
{
	int err = -ENODEV;
	void __iomem *p;

	p = svnic_dev_get_res(vdev, RES_TYPE_DEVCMD2, 0);
	if (p)
		err = svnic_dev_init_devcmd2(vdev);
	else
		pr_err("DEVCMD2 resource not found.\n");

	return err;
} /* end of svnic_dev_cmd_init */
