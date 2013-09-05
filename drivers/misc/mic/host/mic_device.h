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
};

/**
 * struct mic_hw_ops - MIC HW specific operations.
 * @aper_bar: Aperture bar resource number.
 * @mmio_bar: MMIO bar resource number.
 * @read_spad: Read from scratch pad register.
 * @write_spad: Write to scratch pad register.
 */
struct mic_hw_ops {
	u8 aper_bar;
	u8 mmio_bar;
	u32 (*read_spad)(struct mic_device *mdev, unsigned int idx);
	void (*write_spad)(struct mic_device *mdev, unsigned int idx, u32 val);
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
#endif
