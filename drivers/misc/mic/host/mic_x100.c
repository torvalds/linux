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

#include "../common/mic_device.h"
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
	u32 apicicr_low = mic_mmio_read(mw,
			MIC_X100_SBOX_BASE_ADDRESS + apic_icr_offset);

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
		rdmasr_db = doorbell - MIC_X100_NUM_SBOX_IRQ +
			MIC_X100_RDMASR_IRQ_BASE;
		mic_x100_send_rdmasr_intr(mdev, rdmasr_db);
	}
}

/**
 * mic_ack_interrupt - Device specific interrupt handling.
 * @mdev: pointer to mic_device instance
 *
 * Returns: bitmask of doorbell events triggered.
 */
static u32 mic_x100_ack_interrupt(struct mic_device *mdev)
{
	u32 reg = 0;
	struct mic_mw *mw = &mdev->mmio;
	u32 sicr0 = MIC_X100_SBOX_BASE_ADDRESS + MIC_X100_SBOX_SICR0;

	/* Clear pending bit array. */
	if (MIC_A0_STEP == mdev->stepping)
		mic_mmio_write(mw, 1, MIC_X100_SBOX_BASE_ADDRESS +
			MIC_X100_SBOX_MSIXPBACR);

	if (mdev->irq_info.num_vectors <= 1) {
		reg = mic_mmio_read(mw, sicr0);

		if (unlikely(!reg))
			goto done;

		mic_mmio_write(mw, reg, sicr0);
	}

	if (mdev->stepping >= MIC_B0_STEP)
		mdev->intr_ops->enable_interrupts(mdev);
done:
	return reg;
}

/**
 * mic_x100_hw_intr_init - Initialize h/w specific interrupt
 * information.
 * @mdev: pointer to mic_device instance
 */
static void mic_x100_hw_intr_init(struct mic_device *mdev)
{
	mdev->intr_info = (struct mic_intr_info *) mic_x100_intr_init;
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

struct mic_hw_ops mic_x100_ops = {
	.aper_bar = MIC_X100_APER_BAR,
	.mmio_bar = MIC_X100_MMIO_BAR,
	.read_spad = mic_x100_read_spad,
	.write_spad = mic_x100_write_spad,
	.send_intr = mic_x100_send_intr,
	.ack_interrupt = mic_x100_ack_interrupt,
};

struct mic_hw_intr_ops mic_x100_intr_ops = {
	.intr_init = mic_x100_hw_intr_init,
	.enable_interrupts = mic_x100_enable_interrupts,
	.disable_interrupts = mic_x100_disable_interrupts,
	.program_msi_to_src_map = mic_x100_program_msi_to_src_map,
	.read_msi_to_src_map = mic_x100_read_msi_to_src_map,
};
