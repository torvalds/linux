/*
 * Copyright 2008-2010 Cisco Systems, Inc.  All rights reserved.
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
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/if_ether.h>

#include "vnic_resource.h"
#include "vnic_devcmd.h"
#include "vnic_dev.h"
#include "vnic_wq.h"
#include "vnic_stats.h"
#include "enic.h"

#define VNIC_MAX_RES_HDR_SIZE \
	(sizeof(struct vnic_resource_header) + \
	sizeof(struct vnic_resource) * RES_TYPE_MAX)
#define VNIC_RES_STRIDE	128

void *vnic_dev_priv(struct vnic_dev *vdev)
{
	return vdev->priv;
}

static int vnic_dev_discover_res(struct vnic_dev *vdev,
	struct vnic_dev_bar *bar, unsigned int num_bars)
{
	struct vnic_resource_header __iomem *rh;
	struct mgmt_barmap_hdr __iomem *mrh;
	struct vnic_resource __iomem *r;
	u8 type;

	if (num_bars == 0)
		return -EINVAL;

	if (bar->len < VNIC_MAX_RES_HDR_SIZE) {
		vdev_err(vdev, "vNIC BAR0 res hdr length error\n");
		return -EINVAL;
	}

	rh  = bar->vaddr;
	mrh = bar->vaddr;
	if (!rh) {
		vdev_err(vdev, "vNIC BAR0 res hdr not mem-mapped\n");
		return -EINVAL;
	}

	/* Check for mgmt vnic in addition to normal vnic */
	if ((ioread32(&rh->magic) != VNIC_RES_MAGIC) ||
		(ioread32(&rh->version) != VNIC_RES_VERSION)) {
		if ((ioread32(&mrh->magic) != MGMTVNIC_MAGIC) ||
			(ioread32(&mrh->version) != MGMTVNIC_VERSION)) {
			vdev_err(vdev, "vNIC BAR0 res magic/version error exp (%lx/%lx) or (%lx/%lx), curr (%x/%x)\n",
				 VNIC_RES_MAGIC, VNIC_RES_VERSION,
				 MGMTVNIC_MAGIC, MGMTVNIC_VERSION,
				 ioread32(&rh->magic), ioread32(&rh->version));
			return -EINVAL;
		}
	}

	if (ioread32(&mrh->magic) == MGMTVNIC_MAGIC)
		r = (struct vnic_resource __iomem *)(mrh + 1);
	else
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
			if (len + bar_offset > bar[bar_num].len) {
				vdev_err(vdev, "vNIC BAR0 resource %d out-of-bounds, offset 0x%x + size 0x%x > bar len 0x%lx\n",
					 type, bar_offset, len,
					 bar[bar_num].len);
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
		vdev->res[type].vaddr = (char __iomem *)bar[bar_num].vaddr +
			bar_offset;
		vdev->res[type].bus_addr = bar[bar_num].bus_addr + bar_offset;
	}

	return 0;
}

unsigned int vnic_dev_get_res_count(struct vnic_dev *vdev,
	enum vnic_res_type type)
{
	return vdev->res[type].count;
}
EXPORT_SYMBOL(vnic_dev_get_res_count);

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
EXPORT_SYMBOL(vnic_dev_get_res);

static unsigned int vnic_dev_desc_ring_size(struct vnic_dev_ring *ring,
	unsigned int desc_count, unsigned int desc_size)
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

	ring->descs_unaligned = pci_alloc_consistent(vdev->pdev,
		ring->size_unaligned,
		&ring->base_addr_unaligned);

	if (!ring->descs_unaligned) {
		vdev_err(vdev, "Failed to allocate ring (size=%d), aborting\n",
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
		pci_free_consistent(vdev->pdev,
			ring->size_unaligned,
			ring->descs_unaligned,
			ring->base_addr_unaligned);
		ring->descs = NULL;
	}
}

static int _vnic_dev_cmd(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd,
	int wait)
{
	struct vnic_devcmd __iomem *devcmd = vdev->devcmd;
	unsigned int i;
	int delay;
	u32 status;
	int err;

	status = ioread32(&devcmd->status);
	if (status == 0xFFFFFFFF) {
		/* PCI-e target device is gone */
		return -ENODEV;
	}
	if (status & STAT_BUSY) {
		vdev_neterr(vdev, "Busy devcmd %d\n", _CMD_N(cmd));
		return -EBUSY;
	}

	if (_CMD_DIR(cmd) & _CMD_DIR_WRITE) {
		for (i = 0; i < VNIC_DEVCMD_NARGS; i++)
			writeq(vdev->args[i], &devcmd->args[i]);
		wmb();
	}

	iowrite32(cmd, &devcmd->cmd);

	if ((_CMD_FLAGS(cmd) & _CMD_FLAGS_NOWAIT))
		return 0;

	for (delay = 0; delay < wait; delay++) {

		udelay(100);

		status = ioread32(&devcmd->status);
		if (status == 0xFFFFFFFF) {
			/* PCI-e target device is gone */
			return -ENODEV;
		}

		if (!(status & STAT_BUSY)) {

			if (status & STAT_ERROR) {
				err = (int)readq(&devcmd->args[0]);
				if (err == ERR_EINVAL &&
				    cmd == CMD_CAPABILITY)
					return -err;
				if (err != ERR_ECMDUNKNOWN ||
				    cmd != CMD_CAPABILITY)
					vdev_neterr(vdev, "Error %d devcmd %d\n",
						    err, _CMD_N(cmd));
				return -err;
			}

			if (_CMD_DIR(cmd) & _CMD_DIR_READ) {
				rmb();
				for (i = 0; i < VNIC_DEVCMD_NARGS; i++)
					vdev->args[i] = readq(&devcmd->args[i]);
			}

			return 0;
		}
	}

	vdev_neterr(vdev, "Timedout devcmd %d\n", _CMD_N(cmd));
	return -ETIMEDOUT;
}

static int _vnic_dev_cmd2(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd,
			  int wait)
{
	struct devcmd2_controller *dc2c = vdev->devcmd2;
	struct devcmd2_result *result;
	u8 color;
	unsigned int i;
	int delay, err;
	u32 fetch_index, new_posted;
	u32 posted = dc2c->posted;

	fetch_index = ioread32(&dc2c->wq_ctrl->fetch_index);

	if (fetch_index == 0xFFFFFFFF)
		return -ENODEV;

	new_posted = (posted + 1) % DEVCMD2_RING_SIZE;

	if (new_posted == fetch_index) {
		vdev_neterr(vdev, "devcmd2 %d: wq is full. fetch index: %u, posted index: %u\n",
			    _CMD_N(cmd), fetch_index, posted);
		return -EBUSY;
	}
	dc2c->cmd_ring[posted].cmd = cmd;
	dc2c->cmd_ring[posted].flags = 0;

	if ((_CMD_FLAGS(cmd) & _CMD_FLAGS_NOWAIT))
		dc2c->cmd_ring[posted].flags |= DEVCMD2_FNORESULT;
	if (_CMD_DIR(cmd) & _CMD_DIR_WRITE)
		for (i = 0; i < VNIC_DEVCMD_NARGS; i++)
			dc2c->cmd_ring[posted].args[i] = vdev->args[i];

	/* Adding write memory barrier prevents compiler and/or CPU reordering,
	 * thus avoiding descriptor posting before descriptor is initialized.
	 * Otherwise, hardware can read stale descriptor fields.
	 */
	wmb();
	iowrite32(new_posted, &dc2c->wq_ctrl->posted_index);
	dc2c->posted = new_posted;

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
		if (result->color == color) {
			if (result->error) {
				err = result->error;
				if (err != ERR_ECMDUNKNOWN ||
				    cmd != CMD_CAPABILITY)
					vdev_neterr(vdev, "Error %d devcmd %d\n",
						    err, _CMD_N(cmd));
				return -err;
			}
			if (_CMD_DIR(cmd) & _CMD_DIR_READ)
				for (i = 0; i < VNIC_DEVCMD2_NARGS; i++)
					vdev->args[i] = result->results[i];

			return 0;
		}
		udelay(100);
	}

	vdev_neterr(vdev, "devcmd %d timed out\n", _CMD_N(cmd));

	return -ETIMEDOUT;
}

static int vnic_dev_init_devcmd1(struct vnic_dev *vdev)
{
	vdev->devcmd = vnic_dev_get_res(vdev, RES_TYPE_DEVCMD, 0);
	if (!vdev->devcmd)
		return -ENODEV;
	vdev->devcmd_rtn = _vnic_dev_cmd;

	return 0;
}

static int vnic_dev_init_devcmd2(struct vnic_dev *vdev)
{
	int err;
	unsigned int fetch_index;

	if (vdev->devcmd2)
		return 0;

	vdev->devcmd2 = kzalloc(sizeof(*vdev->devcmd2), GFP_KERNEL);
	if (!vdev->devcmd2)
		return -ENOMEM;

	vdev->devcmd2->color = 1;
	vdev->devcmd2->result_size = DEVCMD2_RING_SIZE;
	err = enic_wq_devcmd2_alloc(vdev, &vdev->devcmd2->wq, DEVCMD2_RING_SIZE,
				    DEVCMD2_DESC_SIZE);
	if (err)
		goto err_free_devcmd2;

	fetch_index = ioread32(&vdev->devcmd2->wq.ctrl->fetch_index);
	if (fetch_index == 0xFFFFFFFF) { /* check for hardware gone  */
		vdev_err(vdev, "Fatal error in devcmd2 init - hardware surprise removal\n");

		return -ENODEV;
	}

	enic_wq_init_start(&vdev->devcmd2->wq, 0, fetch_index, fetch_index, 0,
			   0);
	vdev->devcmd2->posted = fetch_index;
	vnic_wq_enable(&vdev->devcmd2->wq);

	err = vnic_dev_alloc_desc_ring(vdev, &vdev->devcmd2->results_ring,
				       DEVCMD2_RING_SIZE, DEVCMD2_DESC_SIZE);
	if (err)
		goto err_free_wq;

	vdev->devcmd2->result = vdev->devcmd2->results_ring.descs;
	vdev->devcmd2->cmd_ring = vdev->devcmd2->wq.ring.descs;
	vdev->devcmd2->wq_ctrl = vdev->devcmd2->wq.ctrl;
	vdev->args[0] = (u64)vdev->devcmd2->results_ring.base_addr |
			VNIC_PADDR_TARGET;
	vdev->args[1] = DEVCMD2_RING_SIZE;

	err = _vnic_dev_cmd2(vdev, CMD_INITIALIZE_DEVCMD2, 1000);
	if (err)
		goto err_free_desc_ring;

	vdev->devcmd_rtn = _vnic_dev_cmd2;

	return 0;

err_free_desc_ring:
	vnic_dev_free_desc_ring(vdev, &vdev->devcmd2->results_ring);
err_free_wq:
	vnic_wq_disable(&vdev->devcmd2->wq);
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
}

static int vnic_dev_cmd_proxy(struct vnic_dev *vdev,
	enum vnic_devcmd_cmd proxy_cmd, enum vnic_devcmd_cmd cmd,
	u64 *a0, u64 *a1, int wait)
{
	u32 status;
	int err;

	memset(vdev->args, 0, sizeof(vdev->args));

	vdev->args[0] = vdev->proxy_index;
	vdev->args[1] = cmd;
	vdev->args[2] = *a0;
	vdev->args[3] = *a1;

	err = vdev->devcmd_rtn(vdev, proxy_cmd, wait);
	if (err)
		return err;

	status = (u32)vdev->args[0];
	if (status & STAT_ERROR) {
		err = (int)vdev->args[1];
		if (err != ERR_ECMDUNKNOWN ||
		    cmd != CMD_CAPABILITY)
			vdev_neterr(vdev, "Error %d proxy devcmd %d\n",
				    err, _CMD_N(cmd));
		return err;
	}

	*a0 = vdev->args[1];
	*a1 = vdev->args[2];

	return 0;
}

static int vnic_dev_cmd_no_proxy(struct vnic_dev *vdev,
	enum vnic_devcmd_cmd cmd, u64 *a0, u64 *a1, int wait)
{
	int err;

	vdev->args[0] = *a0;
	vdev->args[1] = *a1;

	err = vdev->devcmd_rtn(vdev, cmd, wait);

	*a0 = vdev->args[0];
	*a1 = vdev->args[1];

	return err;
}

void vnic_dev_cmd_proxy_by_index_start(struct vnic_dev *vdev, u16 index)
{
	vdev->proxy = PROXY_BY_INDEX;
	vdev->proxy_index = index;
}

void vnic_dev_cmd_proxy_end(struct vnic_dev *vdev)
{
	vdev->proxy = PROXY_NONE;
	vdev->proxy_index = 0;
}

int vnic_dev_cmd(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd,
	u64 *a0, u64 *a1, int wait)
{
	memset(vdev->args, 0, sizeof(vdev->args));

	switch (vdev->proxy) {
	case PROXY_BY_INDEX:
		return vnic_dev_cmd_proxy(vdev, CMD_PROXY_BY_INDEX, cmd,
				a0, a1, wait);
	case PROXY_BY_BDF:
		return vnic_dev_cmd_proxy(vdev, CMD_PROXY_BY_BDF, cmd,
				a0, a1, wait);
	case PROXY_NONE:
	default:
		return vnic_dev_cmd_no_proxy(vdev, cmd, a0, a1, wait);
	}
}

static int vnic_dev_capable(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd)
{
	u64 a0 = (u32)cmd, a1 = 0;
	int wait = 1000;
	int err;

	err = vnic_dev_cmd(vdev, CMD_CAPABILITY, &a0, &a1, wait);

	return !(err || a0);
}

int vnic_dev_fw_info(struct vnic_dev *vdev,
	struct vnic_devcmd_fw_info **fw_info)
{
	u64 a0, a1 = 0;
	int wait = 1000;
	int err = 0;

	if (!vdev->fw_info) {
		vdev->fw_info = pci_zalloc_consistent(vdev->pdev,
						      sizeof(struct vnic_devcmd_fw_info),
						      &vdev->fw_info_pa);
		if (!vdev->fw_info)
			return -ENOMEM;

		a0 = vdev->fw_info_pa;
		a1 = sizeof(struct vnic_devcmd_fw_info);

		/* only get fw_info once and cache it */
		if (vnic_dev_capable(vdev, CMD_MCPU_FW_INFO))
			err = vnic_dev_cmd(vdev, CMD_MCPU_FW_INFO,
				&a0, &a1, wait);
		else
			err = vnic_dev_cmd(vdev, CMD_MCPU_FW_INFO_OLD,
				&a0, &a1, wait);
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
	case 1: *(u8 *)value = (u8)a0; break;
	case 2: *(u16 *)value = (u16)a0; break;
	case 4: *(u32 *)value = (u32)a0; break;
	case 8: *(u64 *)value = a0; break;
	default: BUG(); break;
	}

	return err;
}

int vnic_dev_stats_dump(struct vnic_dev *vdev, struct vnic_stats **stats)
{
	u64 a0, a1;
	int wait = 1000;

	if (!vdev->stats) {
		vdev->stats = pci_alloc_consistent(vdev->pdev,
			sizeof(struct vnic_stats), &vdev->stats_pa);
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

int vnic_dev_enable_wait(struct vnic_dev *vdev)
{
	u64 a0 = 0, a1 = 0;
	int wait = 1000;

	if (vnic_dev_capable(vdev, CMD_ENABLE_WAIT))
		return vnic_dev_cmd(vdev, CMD_ENABLE_WAIT, &a0, &a1, wait);
	else
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

int vnic_dev_hang_reset(struct vnic_dev *vdev, int arg)
{
	u64 a0 = (u32)arg, a1 = 0;
	int wait = 1000;
	int err;

	if (vnic_dev_capable(vdev, CMD_HANG_RESET)) {
		return vnic_dev_cmd(vdev, CMD_HANG_RESET,
				&a0, &a1, wait);
	} else {
		err = vnic_dev_soft_reset(vdev, arg);
		if (err)
			return err;
		return vnic_dev_init(vdev, 0);
	}
}

int vnic_dev_hang_reset_done(struct vnic_dev *vdev, int *done)
{
	u64 a0 = 0, a1 = 0;
	int wait = 1000;
	int err;

	*done = 0;

	if (vnic_dev_capable(vdev, CMD_HANG_RESET_STATUS)) {
		err = vnic_dev_cmd(vdev, CMD_HANG_RESET_STATUS,
				&a0, &a1, wait);
		if (err)
			return err;
	} else {
		return vnic_dev_soft_reset_done(vdev, done);
	}

	*done = (a0 == 0);

	return 0;
}

int vnic_dev_hang_notify(struct vnic_dev *vdev)
{
	u64 a0, a1;
	int wait = 1000;
	return vnic_dev_cmd(vdev, CMD_HANG_NOTIFY, &a0, &a1, wait);
}

int vnic_dev_get_mac_addr(struct vnic_dev *vdev, u8 *mac_addr)
{
	u64 a0, a1;
	int wait = 1000;
	int err, i;

	for (i = 0; i < ETH_ALEN; i++)
		mac_addr[i] = 0;

	err = vnic_dev_cmd(vdev, CMD_GET_MAC_ADDR, &a0, &a1, wait);
	if (err)
		return err;

	for (i = 0; i < ETH_ALEN; i++)
		mac_addr[i] = ((u8 *)&a0)[i];

	return 0;
}

int vnic_dev_packet_filter(struct vnic_dev *vdev, int directed, int multicast,
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
		vdev_neterr(vdev, "Can't set packet filter\n");

	return err;
}

int vnic_dev_add_addr(struct vnic_dev *vdev, const u8 *addr)
{
	u64 a0 = 0, a1 = 0;
	int wait = 1000;
	int err;
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		((u8 *)&a0)[i] = addr[i];

	err = vnic_dev_cmd(vdev, CMD_ADDR_ADD, &a0, &a1, wait);
	if (err)
		vdev_neterr(vdev, "Can't add addr [%pM], %d\n", addr, err);

	return err;
}

int vnic_dev_del_addr(struct vnic_dev *vdev, const u8 *addr)
{
	u64 a0 = 0, a1 = 0;
	int wait = 1000;
	int err;
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		((u8 *)&a0)[i] = addr[i];

	err = vnic_dev_cmd(vdev, CMD_ADDR_DEL, &a0, &a1, wait);
	if (err)
		vdev_neterr(vdev, "Can't del addr [%pM], %d\n", addr, err);

	return err;
}

int vnic_dev_set_ig_vlan_rewrite_mode(struct vnic_dev *vdev,
	u8 ig_vlan_rewrite_mode)
{
	u64 a0 = ig_vlan_rewrite_mode, a1 = 0;
	int wait = 1000;

	if (vnic_dev_capable(vdev, CMD_IG_VLAN_REWRITE_MODE))
		return vnic_dev_cmd(vdev, CMD_IG_VLAN_REWRITE_MODE,
				&a0, &a1, wait);
	else
		return 0;
}

static int vnic_dev_notify_setcmd(struct vnic_dev *vdev,
	void *notify_addr, dma_addr_t notify_pa, u16 intr)
{
	u64 a0, a1;
	int wait = 1000;
	int r;

	memset(notify_addr, 0, sizeof(struct vnic_devcmd_notify));
	vdev->notify = notify_addr;
	vdev->notify_pa = notify_pa;

	a0 = (u64)notify_pa;
	a1 = ((u64)intr << 32) & 0x0000ffff00000000ULL;
	a1 += sizeof(struct vnic_devcmd_notify);

	r = vnic_dev_cmd(vdev, CMD_NOTIFY, &a0, &a1, wait);
	vdev->notify_sz = (r == 0) ? (u32)a1 : 0;
	return r;
}

int vnic_dev_notify_set(struct vnic_dev *vdev, u16 intr)
{
	void *notify_addr;
	dma_addr_t notify_pa;

	if (vdev->notify || vdev->notify_pa) {
		vdev_neterr(vdev, "notify block %p still allocated\n",
			    vdev->notify);
		return -EINVAL;
	}

	notify_addr = pci_alloc_consistent(vdev->pdev,
			sizeof(struct vnic_devcmd_notify),
			&notify_pa);
	if (!notify_addr)
		return -ENOMEM;

	return vnic_dev_notify_setcmd(vdev, notify_addr, notify_pa, intr);
}

static int vnic_dev_notify_unsetcmd(struct vnic_dev *vdev)
{
	u64 a0, a1;
	int wait = 1000;
	int err;

	a0 = 0;  /* paddr = 0 to unset notify buffer */
	a1 = 0x0000ffff00000000ULL; /* intr num = -1 to unreg for intr */
	a1 += sizeof(struct vnic_devcmd_notify);

	err = vnic_dev_cmd(vdev, CMD_NOTIFY, &a0, &a1, wait);
	vdev->notify = NULL;
	vdev->notify_pa = 0;
	vdev->notify_sz = 0;

	return err;
}

int vnic_dev_notify_unset(struct vnic_dev *vdev)
{
	if (vdev->notify) {
		pci_free_consistent(vdev->pdev,
			sizeof(struct vnic_devcmd_notify),
			vdev->notify,
			vdev->notify_pa);
	}

	return vnic_dev_notify_unsetcmd(vdev);
}

static int vnic_dev_notify_ready(struct vnic_dev *vdev)
{
	u32 *words;
	unsigned int nwords = vdev->notify_sz / 4;
	unsigned int i;
	u32 csum;

	if (!vdev->notify || !vdev->notify_sz)
		return 0;

	do {
		csum = 0;
		memcpy(&vdev->notify_copy, vdev->notify, vdev->notify_sz);
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
	int r = 0;

	if (vnic_dev_capable(vdev, CMD_INIT))
		r = vnic_dev_cmd(vdev, CMD_INIT, &a0, &a1, wait);
	else {
		vnic_dev_cmd(vdev, CMD_INIT_v1, &a0, &a1, wait);
		if (a0 & CMD_INITF_DEFAULT_MAC) {
			/* Emulate these for old CMD_INIT_v1 which
			 * didn't pass a0 so no CMD_INITF_*.
			 */
			vnic_dev_cmd(vdev, CMD_GET_MAC_ADDR, &a0, &a1, wait);
			vnic_dev_cmd(vdev, CMD_ADDR_ADD, &a0, &a1, wait);
		}
	}
	return r;
}

int vnic_dev_deinit(struct vnic_dev *vdev)
{
	u64 a0 = 0, a1 = 0;
	int wait = 1000;

	return vnic_dev_cmd(vdev, CMD_DEINIT, &a0, &a1, wait);
}

void vnic_dev_intr_coal_timer_info_default(struct vnic_dev *vdev)
{
	/* Default: hardware intr coal timer is in units of 1.5 usecs */
	vdev->intr_coal_timer_info.mul = 2;
	vdev->intr_coal_timer_info.div = 3;
	vdev->intr_coal_timer_info.max_usec =
		vnic_dev_intr_coal_timer_hw_to_usec(vdev, 0xffff);
}

int vnic_dev_intr_coal_timer_info(struct vnic_dev *vdev)
{
	int wait = 1000;
	int err;

	memset(vdev->args, 0, sizeof(vdev->args));

	if (vnic_dev_capable(vdev, CMD_INTR_COAL_CONVERT))
		err = vdev->devcmd_rtn(vdev, CMD_INTR_COAL_CONVERT, wait);
	else
		err = ERR_ECMDUNKNOWN;

	/* Use defaults when firmware doesn't support the devcmd at all or
	 * supports it for only specific hardware
	 */
	if ((err == ERR_ECMDUNKNOWN) ||
		(!err && !(vdev->args[0] && vdev->args[1] && vdev->args[2]))) {
		vdev_netwarn(vdev, "Using default conversion factor for interrupt coalesce timer\n");
		vnic_dev_intr_coal_timer_info_default(vdev);
		return 0;
	}

	if (!err) {
		vdev->intr_coal_timer_info.mul = (u32) vdev->args[0];
		vdev->intr_coal_timer_info.div = (u32) vdev->args[1];
		vdev->intr_coal_timer_info.max_usec = (u32) vdev->args[2];
	}

	return err;
}

int vnic_dev_link_status(struct vnic_dev *vdev)
{
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

u32 vnic_dev_intr_coal_timer_usec_to_hw(struct vnic_dev *vdev, u32 usec)
{
	return (usec * vdev->intr_coal_timer_info.mul) /
		vdev->intr_coal_timer_info.div;
}

u32 vnic_dev_intr_coal_timer_hw_to_usec(struct vnic_dev *vdev, u32 hw_cycles)
{
	return (hw_cycles * vdev->intr_coal_timer_info.div) /
		vdev->intr_coal_timer_info.mul;
}

u32 vnic_dev_get_intr_coal_timer_max(struct vnic_dev *vdev)
{
	return vdev->intr_coal_timer_info.max_usec;
}

void vnic_dev_unregister(struct vnic_dev *vdev)
{
	if (vdev) {
		if (vdev->notify)
			pci_free_consistent(vdev->pdev,
				sizeof(struct vnic_devcmd_notify),
				vdev->notify,
				vdev->notify_pa);
		if (vdev->stats)
			pci_free_consistent(vdev->pdev,
				sizeof(struct vnic_stats),
				vdev->stats, vdev->stats_pa);
		if (vdev->fw_info)
			pci_free_consistent(vdev->pdev,
				sizeof(struct vnic_devcmd_fw_info),
				vdev->fw_info, vdev->fw_info_pa);
		if (vdev->devcmd2)
			vnic_dev_deinit_devcmd2(vdev);

		kfree(vdev);
	}
}
EXPORT_SYMBOL(vnic_dev_unregister);

struct vnic_dev *vnic_dev_register(struct vnic_dev *vdev,
	void *priv, struct pci_dev *pdev, struct vnic_dev_bar *bar,
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
	vnic_dev_unregister(vdev);
	return NULL;
}
EXPORT_SYMBOL(vnic_dev_register);

struct pci_dev *vnic_dev_get_pdev(struct vnic_dev *vdev)
{
	return vdev->pdev;
}
EXPORT_SYMBOL(vnic_dev_get_pdev);

int vnic_devcmd_init(struct vnic_dev *vdev)
{
	void __iomem *res;
	int err;

	res = vnic_dev_get_res(vdev, RES_TYPE_DEVCMD2, 0);
	if (res) {
		err = vnic_dev_init_devcmd2(vdev);
		if (err)
			vdev_warn(vdev, "DEVCMD2 init failed: %d, Using DEVCMD1\n",
				  err);
		else
			return 0;
	} else {
		vdev_warn(vdev, "DEVCMD2 resource not found (old firmware?) Using DEVCMD1\n");
	}
	err = vnic_dev_init_devcmd1(vdev);
	if (err)
		vdev_err(vdev, "DEVCMD1 initialization failed: %d\n", err);

	return err;
}

int vnic_dev_init_prov2(struct vnic_dev *vdev, u8 *buf, u32 len)
{
	u64 a0, a1 = len;
	int wait = 1000;
	dma_addr_t prov_pa;
	void *prov_buf;
	int ret;

	prov_buf = pci_alloc_consistent(vdev->pdev, len, &prov_pa);
	if (!prov_buf)
		return -ENOMEM;

	memcpy(prov_buf, buf, len);

	a0 = prov_pa;

	ret = vnic_dev_cmd(vdev, CMD_INIT_PROV_INFO2, &a0, &a1, wait);

	pci_free_consistent(vdev->pdev, len, prov_buf, prov_pa);

	return ret;
}

int vnic_dev_enable2(struct vnic_dev *vdev, int active)
{
	u64 a0, a1 = 0;
	int wait = 1000;

	a0 = (active ? CMD_ENABLE2_ACTIVE : 0);

	return vnic_dev_cmd(vdev, CMD_ENABLE2, &a0, &a1, wait);
}

static int vnic_dev_cmd_status(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd,
	int *status)
{
	u64 a0 = cmd, a1 = 0;
	int wait = 1000;
	int ret;

	ret = vnic_dev_cmd(vdev, CMD_STATUS, &a0, &a1, wait);
	if (!ret)
		*status = (int)a0;

	return ret;
}

int vnic_dev_enable2_done(struct vnic_dev *vdev, int *status)
{
	return vnic_dev_cmd_status(vdev, CMD_ENABLE2, status);
}

int vnic_dev_deinit_done(struct vnic_dev *vdev, int *status)
{
	return vnic_dev_cmd_status(vdev, CMD_DEINIT, status);
}

int vnic_dev_set_mac_addr(struct vnic_dev *vdev, u8 *mac_addr)
{
	u64 a0, a1;
	int wait = 1000;
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		((u8 *)&a0)[i] = mac_addr[i];

	return vnic_dev_cmd(vdev, CMD_SET_MAC_ADDR, &a0, &a1, wait);
}

/* vnic_dev_classifier: Add/Delete classifier entries
 * @vdev: vdev of the device
 * @cmd: CLSF_ADD for Add filter
 *	 CLSF_DEL for Delete filter
 * @entry: In case of ADD filter, the caller passes the RQ number in this
 *	   variable.
 *
 *	   This function stores the filter_id returned by the firmware in the
 *	   same variable before return;
 *
 *	   In case of DEL filter, the caller passes the RQ number. Return
 *	   value is irrelevant.
 * @data: filter data
 */
int vnic_dev_classifier(struct vnic_dev *vdev, u8 cmd, u16 *entry,
			struct filter *data)
{
	u64 a0, a1;
	int wait = 1000;
	dma_addr_t tlv_pa;
	int ret = -EINVAL;
	struct filter_tlv *tlv, *tlv_va;
	struct filter_action *action;
	u64 tlv_size;

	if (cmd == CLSF_ADD) {
		tlv_size = sizeof(struct filter) +
			   sizeof(struct filter_action) +
			   2 * sizeof(struct filter_tlv);
		tlv_va = pci_alloc_consistent(vdev->pdev, tlv_size, &tlv_pa);
		if (!tlv_va)
			return -ENOMEM;
		tlv = tlv_va;
		a0 = tlv_pa;
		a1 = tlv_size;
		memset(tlv, 0, tlv_size);
		tlv->type = CLSF_TLV_FILTER;
		tlv->length = sizeof(struct filter);
		*(struct filter *)&tlv->val = *data;

		tlv = (struct filter_tlv *)((char *)tlv +
					    sizeof(struct filter_tlv) +
					    sizeof(struct filter));

		tlv->type = CLSF_TLV_ACTION;
		tlv->length = sizeof(struct filter_action);
		action = (struct filter_action *)&tlv->val;
		action->type = FILTER_ACTION_RQ_STEERING;
		action->u.rq_idx = *entry;

		ret = vnic_dev_cmd(vdev, CMD_ADD_FILTER, &a0, &a1, wait);
		*entry = (u16)a0;
		pci_free_consistent(vdev->pdev, tlv_size, tlv_va, tlv_pa);
	} else if (cmd == CLSF_DEL) {
		a0 = *entry;
		ret = vnic_dev_cmd(vdev, CMD_DEL_FILTER, &a0, &a1, wait);
	}

	return ret;
}

int vnic_dev_overlay_offload_ctrl(struct vnic_dev *vdev, u8 overlay, u8 config)
{
	u64 a0 = overlay;
	u64 a1 = config;
	int wait = 1000;

	return vnic_dev_cmd(vdev, CMD_OVERLAY_OFFLOAD_CTRL, &a0, &a1, wait);
}

int vnic_dev_overlay_offload_cfg(struct vnic_dev *vdev, u8 overlay,
				 u16 vxlan_udp_port_number)
{
	u64 a1 = vxlan_udp_port_number;
	u64 a0 = overlay;
	int wait = 1000;

	return vnic_dev_cmd(vdev, CMD_OVERLAY_OFFLOAD_CFG, &a0, &a1, wait);
}

int vnic_dev_get_supported_feature_ver(struct vnic_dev *vdev, u8 feature,
				       u64 *supported_versions)
{
	u64 a0 = feature;
	int wait = 1000;
	u64 a1 = 0;
	int ret;

	ret = vnic_dev_cmd(vdev, CMD_GET_SUPP_FEATURE_VER, &a0, &a1, wait);
	if (!ret)
		*supported_versions = a0;

	return ret;
}
