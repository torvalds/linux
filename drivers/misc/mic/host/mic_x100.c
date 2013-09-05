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

struct mic_hw_ops mic_x100_ops = {
	.aper_bar = MIC_X100_APER_BAR,
	.mmio_bar = MIC_X100_MMIO_BAR,
	.read_spad = mic_x100_read_spad,
	.write_spad = mic_x100_write_spad,
};
