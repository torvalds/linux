/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Disclaimer: The codes contained in these modules may be specific to
 * the Intel Software Development Platform codenamed: Knights Ferry, and
 * the Intel product codenamed: Knights Corner, and are not backward
 * compatible with other Intel products. Additionally, Intel will NOT
 * support the codes or instruction set in future products.
 *
 * Intel MIC Card driver.
 *
 */
#ifndef _MIC_CARD_DEVICE_H_
#define _MIC_CARD_DEVICE_H_

#include <linux/workqueue.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mic_bus.h>
#include "../bus/scif_bus.h"
#include "../bus/vop_bus.h"

/**
 * struct mic_intr_info - Contains h/w specific interrupt sources info
 *
 * @num_intr: The number of irqs available
 */
struct mic_intr_info {
	u32 num_intr;
};

/**
 * struct mic_irq_info - OS specific irq information
 *
 * @irq_usage_count: usage count array tracking the number of sources
 * assigned for each irq.
 */
struct mic_irq_info {
	int *irq_usage_count;
};

/**
 * struct mic_device -  MIC device information.
 *
 * @mmio: MMIO bar information.
 */
struct mic_device {
	struct mic_mw mmio;
};

/**
 * struct mic_driver - MIC card driver information.
 *
 * @name: Name for MIC driver.
 * @dbg_dir: debugfs directory of this MIC device.
 * @dev: The device backing this MIC.
 * @dp: The pointer to the virtio device page.
 * @mdev: MIC device information for the host.
 * @hotplug_work: Hot plug work for adding/removing virtio devices.
 * @irq_info: The OS specific irq information
 * @intr_info: H/W specific interrupt information.
 * @dma_mbdev: dma device on the MIC virtual bus.
 * @dma_ch - Array of DMA channels
 * @num_dma_ch - Number of DMA channels available
 * @scdev: SCIF device on the SCIF virtual bus.
 * @vpdev: Virtio over PCIe device on the VOP virtual bus.
 */
struct mic_driver {
	char name[20];
	struct dentry *dbg_dir;
	struct device *dev;
	void __iomem *dp;
	struct mic_device mdev;
	struct work_struct hotplug_work;
	struct mic_irq_info irq_info;
	struct mic_intr_info intr_info;
	struct mbus_device *dma_mbdev;
	struct dma_chan *dma_ch[MIC_MAX_DMA_CHAN];
	int num_dma_ch;
	struct scif_hw_dev *scdev;
	struct vop_device *vpdev;
};

/**
 * struct mic_irq - opaque pointer used as cookie
 */
struct mic_irq;

/**
 * mic_mmio_read - read from an MMIO register.
 * @mw: MMIO register base virtual address.
 * @offset: register offset.
 *
 * RETURNS: register value.
 */
static inline u32 mic_mmio_read(struct mic_mw *mw, u32 offset)
{
	return ioread32(mw->va + offset);
}

/**
 * mic_mmio_write - write to an MMIO register.
 * @mw: MMIO register base virtual address.
 * @val: the data value to put into the register
 * @offset: register offset.
 *
 * RETURNS: none.
 */
static inline void
mic_mmio_write(struct mic_mw *mw, u32 val, u32 offset)
{
	iowrite32(val, mw->va + offset);
}

int mic_driver_init(struct mic_driver *mdrv);
void mic_driver_uninit(struct mic_driver *mdrv);
int mic_next_card_db(void);
struct mic_irq *
mic_request_card_irq(irq_handler_t handler, irq_handler_t thread_fn,
		     const char *name, void *data, int db);
void mic_free_card_irq(struct mic_irq *cookie, void *data);
u32 mic_read_spad(struct mic_device *mdev, unsigned int idx);
void mic_send_intr(struct mic_device *mdev, int doorbell);
void mic_send_p2p_intr(int doorbell, struct mic_mw *mw);
int mic_db_to_irq(struct mic_driver *mdrv, int db);
u32 mic_ack_interrupt(struct mic_device *mdev);
void mic_hw_intr_init(struct mic_driver *mdrv);
void __iomem *
mic_card_map(struct mic_device *mdev, dma_addr_t addr, size_t size);
void mic_card_unmap(struct mic_device *mdev, void __iomem *addr);
void __init mic_create_card_debug_dir(struct mic_driver *mdrv);
void mic_delete_card_debug_dir(struct mic_driver *mdrv);
void __init mic_init_card_debugfs(void);
void mic_exit_card_debugfs(void);
#endif
