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
#include <linux/fs.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/firmware.h>
#include <linux/delay.h>

#include "../common/mic_dev.h"
#include "mic_device.h"
#include "mic_x100.h"
#include "mic_smpt.h"

/**
 * mic_x100_write_spad - write to the scratchpad register
 * @mdev: pointer to mic_device instance
 * @idx: index to the scratchpad register, 0 based
 * @val: the data value to put into the register
 *
 * This function allows writing of a 32bit value to the indexed scratchpad
 * register.
 *
 * RETURNS: none.
 */
static void
mic_x100_write_spad(struct mic_device *mdev, unsigned int idx, u32 val)
{
	dev_dbg(mdev->sdev->parent, "Writing 0x%x to scratch pad index %d\n",
		val, idx);
	mic_mmio_write(&mdev->mmio, val,
		       MIC_X100_SBOX_BASE_ADDRESS +
		       MIC_X100_SBOX_SPAD0 + idx * 4);
}

/**
 * mic_x100_read_spad - read from the scratchpad register
 * @mdev: pointer to mic_device instance
 * @idx: index to scratchpad register, 0 based
 *
 * This function allows reading of the 32bit scratchpad register.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
static u32
mic_x100_read_spad(struct mic_device *mdev, unsigned int idx)
{
	u32 val = mic_mmio_read(&mdev->mmio,
		MIC_X100_SBOX_BASE_ADDRESS +
		MIC_X100_SBOX_SPAD0 + idx * 4);

	dev_dbg(mdev->sdev->parent,
		"Reading 0x%x from scratch pad index %d\n", val, idx);
	return val;
}

/**
 * mic_x100_enable_interrupts - Enable interrupts.
 * @mdev: pointer to mic_device instance
 */
static void mic_x100_enable_interrupts(struct mic_device *mdev)
{
	u32 reg;
	struct mic_mw *mw = &mdev->mmio;
	u32 sice0 = MIC_X100_SBOX_BASE_ADDRESS + MIC_X100_SBOX_SICE0;
	u32 siac0 = MIC_X100_SBOX_BASE_ADDRESS + MIC_X100_SBOX_SIAC0;

	reg = mic_mmio_read(mw, sice0);
	reg |= MIC_X100_SBOX_DBR_BITS(0xf) | MIC_X100_SBOX_DMA_BITS(0xff);
	mic_mmio_write(mw, reg, sice0);

	/*
	 * Enable auto-clear when enabling interrupts. Applicable only for
	 * MSI-x. Legacy and MSI mode cannot have auto-clear enabled.
	 */
	if (mdev->irq_info.num_vectors > 1) {
		reg = mic_mmio_read(mw, siac0);
		reg |= MIC_X100_SBOX_DBR_BITS(0xf) |
			MIC_X100_SBOX_DMA_BITS(0xff);
		mic_mmio_write(mw, reg, siac0);
	}
}

/**
 * mic_x100_disable_interrupts - Disable interrupts.
 * @mdev: pointer to mic_device instance
 */
static void mic_x100_disable_interrupts(struct mic_device *mdev)
{
	u32 reg;
	struct mic_mw *mw = &mdev->mmio;
	u32 sice0 = MIC_X100_SBOX_BASE_ADDRESS + MIC_X100_SBOX_SICE0;
	u32 siac0 = MIC_X100_SBOX_BASE_ADDRESS + MIC_X100_SBOX_SIAC0;
	u32 sicc0 = MIC_X100_SBOX_BASE_ADDRESS + MIC_X100_SBOX_SICC0;

	reg = mic_mmio_read(mw, sice0);
	mic_mmio_write(mw, reg, sicc0);

	if (mdev->irq_info.num_vectors > 1) {
		reg = mic_mmio_read(mw, siac0);
		reg &= ~(MIC_X100_SBOX_DBR_BITS(0xf) |
			MIC_X100_SBOX_DMA_BITS(0xff));
		mic_mmio_write(mw, reg, siac0);
	}
}

/**
 * mic_x100_send_sbox_intr - Send an MIC_X100_SBOX interrupt to MIC.
 * @mdev: pointer to mic_device instance
 */
static void mic_x100_send_sbox_intr(struct mic_device *mdev,
			int doorbell)
{
	struct mic_mw *mw = &mdev->mmio;
	u64 apic_icr_offset = MIC_X100_SBOX_APICICR0 + doorbell * 8;
	u32 apicicr_low = mic_mmio_read(mw, MIC_X100_SBOX_BASE_ADDRESS +
					apic_icr_offset);

	/* for MIC we need to make sure we "hit" the send_icr bit (13) */
	apicicr_low = (apicicr_low | (1 << 13));

	/* Ensure that the interrupt is ordered w.r.t. previous stores. */
	wmb();
	mic_mmio_write(mw, apicicr_low,
		       MIC_X100_SBOX_BASE_ADDRESS + apic_icr_offset);
}

/**
 * mic_x100_send_rdmasr_intr - Send an RDMASR interrupt to MIC.
 * @mdev: pointer to mic_device instance
 */
static void mic_x100_send_rdmasr_intr(struct mic_device *mdev,
			int doorbell)
{
	int rdmasr_offset = MIC_X100_SBOX_RDMASR0 + (doorbell << 2);
	/* Ensure that the interrupt is ordered w.r.t. previous stores. */
	wmb();
	mic_mmio_write(&mdev->mmio, 0,
		       MIC_X100_SBOX_BASE_ADDRESS + rdmasr_offset);
}

/**
 * __mic_x100_send_intr - Send interrupt to MIC.
 * @mdev: pointer to mic_device instance
 * @doorbell: doorbell number.
 */
static void mic_x100_send_intr(struct mic_device *mdev, int doorbell)
{
	int rdmasr_db;
	if (doorbell < MIC_X100_NUM_SBOX_IRQ) {
		mic_x100_send_sbox_intr(mdev, doorbell);
	} else {
		rdmasr_db = doorbell - MIC_X100_NUM_SBOX_IRQ;
		mic_x100_send_rdmasr_intr(mdev, rdmasr_db);
	}
}

/**
 * mic_x100_ack_interrupt - Read the interrupt sources register and
 * clear it. This function will be called in the MSI/INTx case.
 * @mdev: Pointer to mic_device instance.
 *
 * Returns: bitmask of interrupt sources triggered.
 */
static u32 mic_x100_ack_interrupt(struct mic_device *mdev)
{
	u32 sicr0 = MIC_X100_SBOX_BASE_ADDRESS + MIC_X100_SBOX_SICR0;
	u32 reg = mic_mmio_read(&mdev->mmio, sicr0);
	mic_mmio_write(&mdev->mmio, reg, sicr0);
	return reg;
}

/**
 * mic_x100_intr_workarounds - These hardware specific workarounds are
 * to be invoked everytime an interrupt is handled.
 * @mdev: Pointer to mic_device instance.
 *
 * Returns: none
 */
static void mic_x100_intr_workarounds(struct mic_device *mdev)
{
	struct mic_mw *mw = &mdev->mmio;

	/* Clear pending bit array. */
	if (MIC_A0_STEP == mdev->stepping)
		mic_mmio_write(mw, 1, MIC_X100_SBOX_BASE_ADDRESS +
			MIC_X100_SBOX_MSIXPBACR);

	if (mdev->stepping >= MIC_B0_STEP)
		mdev->intr_ops->enable_interrupts(mdev);
}

/**
 * mic_x100_hw_intr_init - Initialize h/w specific interrupt
 * information.
 * @mdev: pointer to mic_device instance
 */
static void mic_x100_hw_intr_init(struct mic_device *mdev)
{
	mdev->intr_info = (struct mic_intr_info *)mic_x100_intr_init;
}

/**
 * mic_x100_read_msi_to_src_map - read from the MSI mapping registers
 * @mdev: pointer to mic_device instance
 * @idx: index to the mapping register, 0 based
 *
 * This function allows reading of the 32bit MSI mapping register.
 *
 * RETURNS: The value in the register.
 */
static u32
mic_x100_read_msi_to_src_map(struct mic_device *mdev, int idx)
{
	return mic_mmio_read(&mdev->mmio,
		MIC_X100_SBOX_BASE_ADDRESS +
		MIC_X100_SBOX_MXAR0 + idx * 4);
}

/**
 * mic_x100_program_msi_to_src_map - program the MSI mapping registers
 * @mdev: pointer to mic_device instance
 * @idx: index to the mapping register, 0 based
 * @offset: The bit offset in the register that needs to be updated.
 * @set: boolean specifying if the bit in the specified offset needs
 * to be set or cleared.
 *
 * RETURNS: None.
 */
static void
mic_x100_program_msi_to_src_map(struct mic_device *mdev,
				int idx, int offset, bool set)
{
	unsigned long reg;
	struct mic_mw *mw = &mdev->mmio;
	u32 mxar = MIC_X100_SBOX_BASE_ADDRESS +
		MIC_X100_SBOX_MXAR0 + idx * 4;

	reg = mic_mmio_read(mw, mxar);
	if (set)
		__set_bit(offset, &reg);
	else
		__clear_bit(offset, &reg);
	mic_mmio_write(mw, reg, mxar);
}

/*
 * mic_x100_reset_fw_ready - Reset Firmware ready status field.
 * @mdev: pointer to mic_device instance
 */
static void mic_x100_reset_fw_ready(struct mic_device *mdev)
{
	mdev->ops->write_spad(mdev, MIC_X100_DOWNLOAD_INFO, 0);
}

/*
 * mic_x100_is_fw_ready - Check if firmware is ready.
 * @mdev: pointer to mic_device instance
 */
static bool mic_x100_is_fw_ready(struct mic_device *mdev)
{
	u32 scratch2 = mdev->ops->read_spad(mdev, MIC_X100_DOWNLOAD_INFO);
	return MIC_X100_SPAD2_DOWNLOAD_STATUS(scratch2) ? true : false;
}

/**
 * mic_x100_get_apic_id - Get bootstrap APIC ID.
 * @mdev: pointer to mic_device instance
 */
static u32 mic_x100_get_apic_id(struct mic_device *mdev)
{
	u32 scratch2 = 0;

	scratch2 = mdev->ops->read_spad(mdev, MIC_X100_DOWNLOAD_INFO);
	return MIC_X100_SPAD2_APIC_ID(scratch2);
}

/**
 * mic_x100_send_firmware_intr - Send an interrupt to the firmware on MIC.
 * @mdev: pointer to mic_device instance
 */
static void mic_x100_send_firmware_intr(struct mic_device *mdev)
{
	u32 apicicr_low;
	u64 apic_icr_offset = MIC_X100_SBOX_APICICR7;
	int vector = MIC_X100_BSP_INTERRUPT_VECTOR;
	struct mic_mw *mw = &mdev->mmio;

	/*
	 * For MIC we need to make sure we "hit"
	 * the send_icr bit (13).
	 */
	apicicr_low = (vector | (1 << 13));

	mic_mmio_write(mw, mic_x100_get_apic_id(mdev),
		       MIC_X100_SBOX_BASE_ADDRESS + apic_icr_offset + 4);

	/* Ensure that the interrupt is ordered w.r.t. previous stores. */
	wmb();
	mic_mmio_write(mw, apicicr_low,
		       MIC_X100_SBOX_BASE_ADDRESS + apic_icr_offset);
}

/**
 * mic_x100_hw_reset - Reset the MIC device.
 * @mdev: pointer to mic_device instance
 */
static void mic_x100_hw_reset(struct mic_device *mdev)
{
	u32 reset_reg;
	u32 rgcr = MIC_X100_SBOX_BASE_ADDRESS + MIC_X100_SBOX_RGCR;
	struct mic_mw *mw = &mdev->mmio;

	/* Ensure that the reset is ordered w.r.t. previous loads and stores */
	mb();
	/* Trigger reset */
	reset_reg = mic_mmio_read(mw, rgcr);
	reset_reg |= 0x1;
	mic_mmio_write(mw, reset_reg, rgcr);
	/*
	 * It seems we really want to delay at least 1 second
	 * after touching reset to prevent a lot of problems.
	 */
	msleep(1000);
}

/**
 * mic_x100_load_command_line - Load command line to MIC.
 * @mdev: pointer to mic_device instance
 * @fw: the firmware image
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
static int
mic_x100_load_command_line(struct mic_device *mdev, const struct firmware *fw)
{
	u32 len = 0;
	u32 boot_mem;
	char *buf;
	void __iomem *cmd_line_va = mdev->aper.va + mdev->bootaddr + fw->size;
#define CMDLINE_SIZE 2048

	boot_mem = mdev->aper.len >> 20;
	buf = kzalloc(CMDLINE_SIZE, GFP_KERNEL);
	if (!buf) {
		dev_err(mdev->sdev->parent,
			"%s %d allocation failed\n", __func__, __LINE__);
		return -ENOMEM;
	}
	len += snprintf(buf, CMDLINE_SIZE - len,
		" mem=%dM", boot_mem);
	if (mdev->cmdline)
		snprintf(buf + len, CMDLINE_SIZE - len, " %s", mdev->cmdline);
	memcpy_toio(cmd_line_va, buf, strlen(buf) + 1);
	kfree(buf);
	return 0;
}

/**
 * mic_x100_load_ramdisk - Load ramdisk to MIC.
 * @mdev: pointer to mic_device instance
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
static int
mic_x100_load_ramdisk(struct mic_device *mdev)
{
	const struct firmware *fw;
	int rc;
	struct boot_params __iomem *bp = mdev->aper.va + mdev->bootaddr;

	rc = request_firmware(&fw,
			mdev->ramdisk, mdev->sdev->parent);
	if (rc < 0) {
		dev_err(mdev->sdev->parent,
			"ramdisk request_firmware failed: %d %s\n",
			rc, mdev->ramdisk);
		goto error;
	}
	/*
	 * Typically the bootaddr for card OS is 64M
	 * so copy over the ramdisk @ 128M.
	 */
	memcpy_toio(mdev->aper.va + (mdev->bootaddr << 1), fw->data, fw->size);
	iowrite32(mdev->bootaddr << 1, &bp->hdr.ramdisk_image);
	iowrite32(fw->size, &bp->hdr.ramdisk_size);
	release_firmware(fw);
error:
	return rc;
}

/**
 * mic_x100_get_boot_addr - Get MIC boot address.
 * @mdev: pointer to mic_device instance
 *
 * This function is called during firmware load to determine
 * the address at which the OS should be downloaded in card
 * memory i.e. GDDR.
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
static int
mic_x100_get_boot_addr(struct mic_device *mdev)
{
	u32 scratch2, boot_addr;
	int rc = 0;

	scratch2 = mdev->ops->read_spad(mdev, MIC_X100_DOWNLOAD_INFO);
	boot_addr = MIC_X100_SPAD2_DOWNLOAD_ADDR(scratch2);
	dev_dbg(mdev->sdev->parent, "%s %d boot_addr 0x%x\n",
		__func__, __LINE__, boot_addr);
	if (boot_addr > (1 << 31)) {
		dev_err(mdev->sdev->parent,
			"incorrect bootaddr 0x%x\n",
			boot_addr);
		rc = -EINVAL;
		goto error;
	}
	mdev->bootaddr = boot_addr;
error:
	return rc;
}

/**
 * mic_x100_load_firmware - Load firmware to MIC.
 * @mdev: pointer to mic_device instance
 * @buf: buffer containing boot string including firmware/ramdisk path.
 *
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
static int
mic_x100_load_firmware(struct mic_device *mdev, const char *buf)
{
	int rc;
	const struct firmware *fw;

	rc = mic_x100_get_boot_addr(mdev);
	if (rc)
		goto error;
	/* load OS */
	rc = request_firmware(&fw, mdev->firmware, mdev->sdev->parent);
	if (rc < 0) {
		dev_err(mdev->sdev->parent,
			"ramdisk request_firmware failed: %d %s\n",
			rc, mdev->firmware);
		goto error;
	}
	if (mdev->bootaddr > mdev->aper.len - fw->size) {
		rc = -EINVAL;
		dev_err(mdev->sdev->parent, "%s %d rc %d bootaddr 0x%x\n",
			__func__, __LINE__, rc, mdev->bootaddr);
		release_firmware(fw);
		goto error;
	}
	memcpy_toio(mdev->aper.va + mdev->bootaddr, fw->data, fw->size);
	mdev->ops->write_spad(mdev, MIC_X100_FW_SIZE, fw->size);
	if (!strcmp(mdev->bootmode, "elf"))
		goto done;
	/* load command line */
	rc = mic_x100_load_command_line(mdev, fw);
	if (rc) {
		dev_err(mdev->sdev->parent, "%s %d rc %d\n",
			__func__, __LINE__, rc);
		goto error;
	}
	release_firmware(fw);
	/* load ramdisk */
	if (mdev->ramdisk)
		rc = mic_x100_load_ramdisk(mdev);
error:
	dev_dbg(mdev->sdev->parent, "%s %d rc %d\n", __func__, __LINE__, rc);
done:
	return rc;
}

/**
 * mic_x100_get_postcode - Get postcode status from firmware.
 * @mdev: pointer to mic_device instance
 *
 * RETURNS: postcode.
 */
static u32 mic_x100_get_postcode(struct mic_device *mdev)
{
	return mic_mmio_read(&mdev->mmio, MIC_X100_POSTCODE);
}

/**
 * mic_x100_smpt_set - Update an SMPT entry with a DMA address.
 * @mdev: pointer to mic_device instance
 *
 * RETURNS: none.
 */
static void
mic_x100_smpt_set(struct mic_device *mdev, dma_addr_t dma_addr, u8 index)
{
#define SNOOP_ON	(0 << 0)
#define SNOOP_OFF	(1 << 0)
/*
 * Sbox Smpt Reg Bits:
 * Bits	31:2	Host address
 * Bits	1	RSVD
 * Bits	0	No snoop
 */
#define BUILD_SMPT(NO_SNOOP, HOST_ADDR)  \
	(u32)(((HOST_ADDR) << 2) | ((NO_SNOOP) & 0x01))

	uint32_t smpt_reg_val = BUILD_SMPT(SNOOP_ON,
			dma_addr >> mdev->smpt->info.page_shift);
	mic_mmio_write(&mdev->mmio, smpt_reg_val,
		       MIC_X100_SBOX_BASE_ADDRESS +
		       MIC_X100_SBOX_SMPT00 + (4 * index));
}

/**
 * mic_x100_smpt_hw_init - Initialize SMPT X100 specific fields.
 * @mdev: pointer to mic_device instance
 *
 * RETURNS: none.
 */
static void mic_x100_smpt_hw_init(struct mic_device *mdev)
{
	struct mic_smpt_hw_info *info = &mdev->smpt->info;

	info->num_reg = 32;
	info->page_shift = 34;
	info->page_size = (1ULL << info->page_shift);
	info->base = 0x8000000000ULL;
}

struct mic_smpt_ops mic_x100_smpt_ops = {
	.init = mic_x100_smpt_hw_init,
	.set = mic_x100_smpt_set,
};

static bool mic_x100_dma_filter(struct dma_chan *chan, void *param)
{
	if (chan->device->dev->parent == (struct device *)param)
		return true;
	return false;
}

struct mic_hw_ops mic_x100_ops = {
	.aper_bar = MIC_X100_APER_BAR,
	.mmio_bar = MIC_X100_MMIO_BAR,
	.read_spad = mic_x100_read_spad,
	.write_spad = mic_x100_write_spad,
	.send_intr = mic_x100_send_intr,
	.ack_interrupt = mic_x100_ack_interrupt,
	.intr_workarounds = mic_x100_intr_workarounds,
	.reset = mic_x100_hw_reset,
	.reset_fw_ready = mic_x100_reset_fw_ready,
	.is_fw_ready = mic_x100_is_fw_ready,
	.send_firmware_intr = mic_x100_send_firmware_intr,
	.load_mic_fw = mic_x100_load_firmware,
	.get_postcode = mic_x100_get_postcode,
	.dma_filter = mic_x100_dma_filter,
};

struct mic_hw_intr_ops mic_x100_intr_ops = {
	.intr_init = mic_x100_hw_intr_init,
	.enable_interrupts = mic_x100_enable_interrupts,
	.disable_interrupts = mic_x100_disable_interrupts,
	.program_msi_to_src_map = mic_x100_program_msi_to_src_map,
	.read_msi_to_src_map = mic_x100_read_msi_to_src_map,
};
