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
 * Intel MIC Host driver.
 *
 */
#ifndef _MIC_DEVICE_H_
#define _MIC_DEVICE_H_

#include <linux/cdev.h>
#include <linux/idr.h>
#include <linux/notifier.h>
#include <linux/irqreturn.h>
#include <linux/dmaengine.h>
#include <linux/miscdevice.h>
#include <linux/mic_bus.h>
#include "../bus/scif_bus.h"
#include "../bus/vop_bus.h"
#include "../bus/cosm_bus.h"
#include "mic_intr.h"

/**
 * enum mic_stepping - MIC stepping ids.
 */
enum mic_stepping {
	MIC_A0_STEP = 0x0,
	MIC_B0_STEP = 0x10,
	MIC_B1_STEP = 0x11,
	MIC_C0_STEP = 0x20,
};

extern struct cosm_hw_ops cosm_hw_ops;

/**
 * struct mic_device -  MIC device information for each card.
 *
 * @mmio: MMIO bar information.
 * @aper: Aperture bar information.
 * @family: The MIC family to which this device belongs.
 * @ops: MIC HW specific operations.
 * @id: The unique device id for this MIC device.
 * @stepping: Stepping ID.
 * @pdev: Underlying PCI device.
 * @mic_mutex: Mutex for synchronizing access to mic_device.
 * @intr_ops: HW specific interrupt operations.
 * @smpt_ops: Hardware specific SMPT operations.
 * @smpt: MIC SMPT information.
 * @intr_info: H/W specific interrupt information.
 * @irq_info: The OS specific irq information
 * @dbg_dir: debugfs directory of this MIC device.
 * @bootaddr: MIC boot address.
 * @dp: virtio device page
 * @dp_dma_addr: virtio device page DMA address.
 * @dma_mbdev: MIC BUS DMA device.
 * @dma_ch - Array of DMA channels
 * @num_dma_ch - Number of DMA channels available
 * @scdev: SCIF device on the SCIF virtual bus.
 * @vpdev: Virtio over PCIe device on the VOP virtual bus.
 * @cosm_dev: COSM device
 */
struct mic_device {
	struct mic_mw mmio;
	struct mic_mw aper;
	enum mic_hw_family family;
	struct mic_hw_ops *ops;
	int id;
	enum mic_stepping stepping;
	struct pci_dev *pdev;
	struct mutex mic_mutex;
	struct mic_hw_intr_ops *intr_ops;
	struct mic_smpt_ops *smpt_ops;
	struct mic_smpt_info *smpt;
	struct mic_intr_info *intr_info;
	struct mic_irq_info irq_info;
	struct dentry *dbg_dir;
	u32 bootaddr;
	void *dp;
	dma_addr_t dp_dma_addr;
	struct mbus_device *dma_mbdev;
	struct dma_chan *dma_ch[MIC_MAX_DMA_CHAN];
	int num_dma_ch;
	struct scif_hw_dev *scdev;
	struct vop_device *vpdev;
	struct cosm_device *cosm_dev;
};

/**
 * struct mic_hw_ops - MIC HW specific operations.
 * @aper_bar: Aperture bar resource number.
 * @mmio_bar: MMIO bar resource number.
 * @read_spad: Read from scratch pad register.
 * @write_spad: Write to scratch pad register.
 * @send_intr: Send an interrupt for a particular doorbell on the card.
 * @ack_interrupt: Hardware specific operations to ack the h/w on
 * receipt of an interrupt.
 * @intr_workarounds: Hardware specific workarounds needed after
 * handling an interrupt.
 * @reset: Reset the remote processor.
 * @reset_fw_ready: Reset firmware ready field.
 * @is_fw_ready: Check if firmware is ready for OS download.
 * @send_firmware_intr: Send an interrupt to the card firmware.
 * @load_mic_fw: Load firmware segments required to boot the card
 * into card memory. This includes the kernel, command line, ramdisk etc.
 * @get_postcode: Get post code status from firmware.
 * @dma_filter: DMA filter function to be used.
 */
struct mic_hw_ops {
	u8 aper_bar;
	u8 mmio_bar;
	u32 (*read_spad)(struct mic_device *mdev, unsigned int idx);
	void (*write_spad)(struct mic_device *mdev, unsigned int idx, u32 val);
	void (*send_intr)(struct mic_device *mdev, int doorbell);
	u32 (*ack_interrupt)(struct mic_device *mdev);
	void (*intr_workarounds)(struct mic_device *mdev);
	void (*reset)(struct mic_device *mdev);
	void (*reset_fw_ready)(struct mic_device *mdev);
	bool (*is_fw_ready)(struct mic_device *mdev);
	void (*send_firmware_intr)(struct mic_device *mdev);
	int (*load_mic_fw)(struct mic_device *mdev, const char *buf);
	u32 (*get_postcode)(struct mic_device *mdev);
	bool (*dma_filter)(struct dma_chan *chan, void *param);
};

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

void mic_bootparam_init(struct mic_device *mdev);
void mic_create_debug_dir(struct mic_device *dev);
void mic_delete_debug_dir(struct mic_device *dev);
void __init mic_init_debugfs(void);
void mic_exit_debugfs(void);
#endif
