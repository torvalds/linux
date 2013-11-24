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

#include "mic_intr.h"

/* The maximum number of MIC devices supported in a single host system. */
#define MIC_MAX_NUM_DEVS 256

/**
 * enum mic_hw_family - The hardware family to which a device belongs.
 */
enum mic_hw_family {
	MIC_FAMILY_X100 = 0,
	MIC_FAMILY_UNKNOWN
};

/**
 * enum mic_stepping - MIC stepping ids.
 */
enum mic_stepping {
	MIC_A0_STEP = 0x0,
	MIC_B0_STEP = 0x10,
	MIC_B1_STEP = 0x11,
	MIC_C0_STEP = 0x20,
};

/**
 * struct mic_device -  MIC device information for each card.
 *
 * @mmio: MMIO bar information.
 * @aper: Aperture bar information.
 * @family: The MIC family to which this device belongs.
 * @ops: MIC HW specific operations.
 * @id: The unique device id for this MIC device.
 * @stepping: Stepping ID.
 * @attr_group: Pointer to list of sysfs attribute groups.
 * @sdev: Device for sysfs entries.
 * @mic_mutex: Mutex for synchronizing access to mic_device.
 * @intr_ops: HW specific interrupt operations.
 * @smpt_ops: Hardware specific SMPT operations.
 * @smpt: MIC SMPT information.
 * @intr_info: H/W specific interrupt information.
 * @irq_info: The OS specific irq information
 * @dbg_dir: debugfs directory of this MIC device.
 * @cmdline: Kernel command line.
 * @firmware: Firmware file name.
 * @ramdisk: Ramdisk file name.
 * @bootmode: Boot mode i.e. "linux" or "elf" for flash updates.
 * @bootaddr: MIC boot address.
 * @reset_trigger_work: Work for triggering reset requests.
 * @shutdown_work: Work for handling shutdown interrupts.
 * @state: MIC state.
 * @shutdown_status: MIC status reported by card for shutdown/crashes.
 * @state_sysfs: Sysfs dirent for notifying ring 3 about MIC state changes.
 * @reset_wait: Waitqueue for sleeping while reset completes.
 * @log_buf_addr: Log buffer address for MIC.
 * @log_buf_len: Log buffer length address for MIC.
 * @dp: virtio device page
 * @dp_dma_addr: virtio device page DMA address.
 * @shutdown_db: shutdown doorbell.
 * @shutdown_cookie: shutdown cookie.
 * @cdev: Character device for MIC.
 * @vdev_list: list of virtio devices.
 * @pm_notifier: Handles PM notifications from the OS.
 */
struct mic_device {
	struct mic_mw mmio;
	struct mic_mw aper;
	enum mic_hw_family family;
	struct mic_hw_ops *ops;
	int id;
	enum mic_stepping stepping;
	const struct attribute_group **attr_group;
	struct device *sdev;
	struct mutex mic_mutex;
	struct mic_hw_intr_ops *intr_ops;
	struct mic_smpt_ops *smpt_ops;
	struct mic_smpt_info *smpt;
	struct mic_intr_info *intr_info;
	struct mic_irq_info irq_info;
	struct dentry *dbg_dir;
	char *cmdline;
	char *firmware;
	char *ramdisk;
	char *bootmode;
	u32 bootaddr;
	struct work_struct reset_trigger_work;
	struct work_struct shutdown_work;
	u8 state;
	u8 shutdown_status;
	struct sysfs_dirent *state_sysfs;
	struct completion reset_wait;
	void *log_buf_addr;
	int *log_buf_len;
	void *dp;
	dma_addr_t dp_dma_addr;
	int shutdown_db;
	struct mic_irq *shutdown_cookie;
	struct cdev cdev;
	struct list_head vdev_list;
	struct notifier_block pm_notifier;
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
 * @reset: Reset the remote processor.
 * @reset_fw_ready: Reset firmware ready field.
 * @is_fw_ready: Check if firmware is ready for OS download.
 * @send_firmware_intr: Send an interrupt to the card firmware.
 * @load_mic_fw: Load firmware segments required to boot the card
 * into card memory. This includes the kernel, command line, ramdisk etc.
 * @get_postcode: Get post code status from firmware.
 */
struct mic_hw_ops {
	u8 aper_bar;
	u8 mmio_bar;
	u32 (*read_spad)(struct mic_device *mdev, unsigned int idx);
	void (*write_spad)(struct mic_device *mdev, unsigned int idx, u32 val);
	void (*send_intr)(struct mic_device *mdev, int doorbell);
	u32 (*ack_interrupt)(struct mic_device *mdev);
	void (*reset)(struct mic_device *mdev);
	void (*reset_fw_ready)(struct mic_device *mdev);
	bool (*is_fw_ready)(struct mic_device *mdev);
	void (*send_firmware_intr)(struct mic_device *mdev);
	int (*load_mic_fw)(struct mic_device *mdev, const char *buf);
	u32 (*get_postcode)(struct mic_device *mdev);
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

void mic_sysfs_init(struct mic_device *mdev);
int mic_start(struct mic_device *mdev, const char *buf);
void mic_stop(struct mic_device *mdev, bool force);
void mic_shutdown(struct mic_device *mdev);
void mic_reset_delayed_work(struct work_struct *work);
void mic_reset_trigger_work(struct work_struct *work);
void mic_shutdown_work(struct work_struct *work);
void mic_bootparam_init(struct mic_device *mdev);
void mic_set_state(struct mic_device *mdev, u8 state);
void mic_set_shutdown_status(struct mic_device *mdev, u8 status);
void mic_create_debug_dir(struct mic_device *dev);
void mic_delete_debug_dir(struct mic_device *dev);
void __init mic_init_debugfs(void);
void mic_exit_debugfs(void);
void mic_prepare_suspend(struct mic_device *mdev);
void mic_complete_resume(struct mic_device *mdev);
void mic_suspend(struct mic_device *mdev);
#endif
