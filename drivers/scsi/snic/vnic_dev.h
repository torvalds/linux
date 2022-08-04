/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2014 Cisco Systems, Inc.  All rights reserved. */

#ifndef _VNIC_DEV_H_
#define _VNIC_DEV_H_

#include "vnic_resource.h"
#include "vnic_devcmd.h"

#ifndef VNIC_PADDR_TARGET
#define VNIC_PADDR_TARGET	0x0000000000000000ULL
#endif

#ifndef readq
static inline u64 readq(void __iomem *reg)
{
	return ((u64)readl(reg + 0x4UL) << 32) | (u64)readl(reg);
}

static inline void writeq(u64 val, void __iomem *reg)
{
	writel(lower_32_bits(val), reg);
	writel(upper_32_bits(val), reg + 0x4UL);
}
#endif

enum vnic_dev_intr_mode {
	VNIC_DEV_INTR_MODE_UNKNOWN,
	VNIC_DEV_INTR_MODE_INTX,
	VNIC_DEV_INTR_MODE_MSI,
	VNIC_DEV_INTR_MODE_MSIX,
};

struct vnic_dev_bar {
	void __iomem *vaddr;
	dma_addr_t bus_addr;
	unsigned long len;
};

struct vnic_dev_ring {
	void *descs;
	size_t size;
	dma_addr_t base_addr;
	size_t base_align;
	void *descs_unaligned;
	size_t size_unaligned;
	dma_addr_t base_addr_unaligned;
	unsigned int desc_size;
	unsigned int desc_count;
	unsigned int desc_avail;
};

struct vnic_dev;
struct vnic_stats;

void *svnic_dev_priv(struct vnic_dev *vdev);
unsigned int svnic_dev_get_res_count(struct vnic_dev *vdev,
				    enum vnic_res_type type);
void __iomem *svnic_dev_get_res(struct vnic_dev *vdev, enum vnic_res_type type,
			       unsigned int index);
unsigned int svnic_dev_desc_ring_size(struct vnic_dev_ring *ring,
				     unsigned int desc_count,
				     unsigned int desc_size);
void svnic_dev_clear_desc_ring(struct vnic_dev_ring *ring);
int svnic_dev_alloc_desc_ring(struct vnic_dev *vdev, struct vnic_dev_ring *ring,
			     unsigned int desc_count, unsigned int desc_size);
void svnic_dev_free_desc_ring(struct vnic_dev *vdev,
			     struct vnic_dev_ring *ring);
int svnic_dev_cmd(struct vnic_dev *vdev, enum vnic_devcmd_cmd cmd,
		 u64 *a0, u64 *a1, int wait);
int svnic_dev_fw_info(struct vnic_dev *vdev,
		     struct vnic_devcmd_fw_info **fw_info);
int svnic_dev_spec(struct vnic_dev *vdev, unsigned int offset,
		  unsigned int size, void *value);
int svnic_dev_stats_clear(struct vnic_dev *vdev);
int svnic_dev_stats_dump(struct vnic_dev *vdev, struct vnic_stats **stats);
int svnic_dev_notify_set(struct vnic_dev *vdev, u16 intr);
void svnic_dev_notify_unset(struct vnic_dev *vdev);
int svnic_dev_link_status(struct vnic_dev *vdev);
u32 svnic_dev_link_down_cnt(struct vnic_dev *vdev);
int svnic_dev_close(struct vnic_dev *vdev);
int svnic_dev_enable_wait(struct vnic_dev *vdev);
int svnic_dev_disable(struct vnic_dev *vdev);
int svnic_dev_open(struct vnic_dev *vdev, int arg);
int svnic_dev_open_done(struct vnic_dev *vdev, int *done);
int svnic_dev_init(struct vnic_dev *vdev, int arg);
struct vnic_dev *svnic_dev_alloc_discover(struct vnic_dev *vdev,
					 void *priv, struct pci_dev *pdev,
					 struct vnic_dev_bar *bar,
					 unsigned int num_bars);
void svnic_dev_set_intr_mode(struct vnic_dev *vdev,
			    enum vnic_dev_intr_mode intr_mode);
enum vnic_dev_intr_mode svnic_dev_get_intr_mode(struct vnic_dev *vdev);
void svnic_dev_unregister(struct vnic_dev *vdev);
int svnic_dev_cmd_init(struct vnic_dev *vdev, int fallback);
#endif /* _VNIC_DEV_H_ */
