/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
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
		case RES_TYPE_DEVCMD:
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

int vnic_dev_cmd(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd,
	u64 *a0, u64 *a1, int wait)
{
	struct vnic_devcmd __iomem *devcmd = vdev->devcmd;
	int delay;
	u32 status;
	int dev_cmd_err[] = {
		/* convert from fw's version of error.h to host's version */
		0,	/* ERR_SUCCESS */
		EINVAL,	/* ERR_EINVAL */
		EFAULT,	/* ERR_EFAULT */
		EPERM,	/* ERR_EPERM */
		EBUSY,  /* ERR_EBUSY */
	};
	int err;

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
	u64 a0, a1;
	int wait = 1000;
	return vnic_dev_cmd(vdev, CMD_HANG_NOTIFY, &a0, &a1, wait);
}

int vnic_dev_mac_addr(struct vnic_dev *vdev, u8 *mac_addr)
{
	u64 a0, a1;
	int wait = 1000;
	int err, i;

	for (i = 0; i < ETH_ALEN; i++)
		mac_addr[i] = 0;

	err = vnic_dev_cmd(vdev, CMD_MAC_ADDR, &a0, &a1, wait);
	if (err)
		return err;

	for (i = 0; i < ETH_ALEN; i++)
		mac_addr[i] = ((u8 *)&a0)[i];

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
	u64 a0 = 0, a1 = 0;
	int wait = 1000;
	int err;
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		((u8 *)&a0)[i] = addr[i];

	err = vnic_dev_cmd(vdev, CMD_ADDR_ADD, &a0, &a1, wait);
	if (err)
		pr_err("Can't add addr [%pM], %d\n", addr, err);
}

void vnic_dev_del_addr(struct vnic_dev *vdev, u8 *addr)
{
	u64 a0 = 0, a1 = 0;
	int wait = 1000;
	int err;
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		((u8 *)&a0)[i] = addr[i];

	err = vnic_dev_cmd(vdev, CMD_ADDR_DEL, &a0, &a1, wait);
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

	vdev->devcmd = vnic_dev_get_res(vdev, RES_TYPE_DEVCMD, 0);
	if (!vdev->devcmd)
		goto err_out;

	return vdev;

err_out:
	vnic_dev_unregister(vdev);
	return NULL;
}
