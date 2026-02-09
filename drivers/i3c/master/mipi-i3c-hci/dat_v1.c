// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2020, MIPI Alliance, Inc.
 *
 * Author: Nicolas Pitre <npitre@baylibre.com>
 */

#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/i3c/master.h>
#include <linux/io.h>

#include "hci.h"
#include "dat.h"

/*
 * Device Address Table Structure
 */

#define DAT_1_AUTOCMD_HDR_CODE		W1_MASK(58, 51)
#define DAT_1_AUTOCMD_MODE		W1_MASK(50, 48)
#define DAT_1_AUTOCMD_VALUE		W1_MASK(47, 40)
#define DAT_1_AUTOCMD_MASK		W1_MASK(39, 32)
/*	DAT_0_I2C_DEVICE		W0_BIT_(31) */
#define DAT_0_DEV_NACK_RETRY_CNT	W0_MASK(30, 29)
#define DAT_0_RING_ID			W0_MASK(28, 26)
#define DAT_0_DYNADDR_PARITY		W0_BIT_(23)
#define DAT_0_DYNAMIC_ADDRESS		W0_MASK(22, 16)
#define DAT_0_TS			W0_BIT_(15)
#define DAT_0_MR_REJECT			W0_BIT_(14)
/*	DAT_0_SIR_REJECT		W0_BIT_(13) */
/*	DAT_0_IBI_PAYLOAD		W0_BIT_(12) */
#define DAT_0_STATIC_ADDRESS		W0_MASK(6, 0)

#define dat_w0_read(i)		hci->DAT[i].w0
#define dat_w1_read(i)		hci->DAT[i].w1
#define dat_w0_write(i, v)	hci_dat_w0_write(hci, i, v)
#define dat_w1_write(i, v)	hci_dat_w1_write(hci, i, v)

static inline void hci_dat_w0_write(struct i3c_hci *hci, int i, u32 v)
{
	hci->DAT[i].w0 = v;
	writel(v, hci->DAT_regs + i * 8);
}

static inline void hci_dat_w1_write(struct i3c_hci *hci, int i, u32 v)
{
	hci->DAT[i].w1 = v;
	writel(v, hci->DAT_regs + i * 8 + 4);
}

static int hci_dat_v1_init(struct i3c_hci *hci)
{
	struct device *dev = hci->master.dev.parent;
	unsigned int dat_idx;

	if (!hci->DAT_regs) {
		dev_err(&hci->master.dev,
			"only DAT in register space is supported at the moment\n");
		return -EOPNOTSUPP;
	}
	if (hci->DAT_entry_size != 8) {
		dev_err(&hci->master.dev,
			"only 8-bytes DAT entries are supported at the moment\n");
		return -EOPNOTSUPP;
	}

	if (!hci->DAT) {
		hci->DAT = devm_kcalloc(dev, hci->DAT_entries, hci->DAT_entry_size, GFP_KERNEL);
		if (!hci->DAT)
			return -ENOMEM;
	}

	if (!hci->DAT_data) {
		/* use a bitmap for faster free slot search */
		hci->DAT_data = devm_bitmap_zalloc(dev, hci->DAT_entries, GFP_KERNEL);
		if (!hci->DAT_data)
			return -ENOMEM;

		/* clear them */
		for (dat_idx = 0; dat_idx < hci->DAT_entries; dat_idx++) {
			dat_w0_write(dat_idx, 0);
			dat_w1_write(dat_idx, 0);
		}
	}

	return 0;
}

static int hci_dat_v1_alloc_entry(struct i3c_hci *hci)
{
	unsigned int dat_idx;
	int ret;

	if (!hci->DAT_data) {
		ret = hci_dat_v1_init(hci);
		if (ret)
			return ret;
	}
	dat_idx = find_first_zero_bit(hci->DAT_data, hci->DAT_entries);
	if (dat_idx >= hci->DAT_entries)
		return -ENOENT;
	__set_bit(dat_idx, hci->DAT_data);

	/* default flags */
	dat_w0_write(dat_idx, DAT_0_SIR_REJECT | DAT_0_MR_REJECT);

	return dat_idx;
}

static void hci_dat_v1_free_entry(struct i3c_hci *hci, unsigned int dat_idx)
{
	dat_w0_write(dat_idx, 0);
	dat_w1_write(dat_idx, 0);
	if (hci->DAT_data)
		__clear_bit(dat_idx, hci->DAT_data);
}

static void hci_dat_v1_set_dynamic_addr(struct i3c_hci *hci,
					unsigned int dat_idx, u8 address)
{
	u32 dat_w0;

	dat_w0 = dat_w0_read(dat_idx);
	dat_w0 &= ~(DAT_0_DYNAMIC_ADDRESS | DAT_0_DYNADDR_PARITY);
	dat_w0 |= FIELD_PREP(DAT_0_DYNAMIC_ADDRESS, address) |
		  (parity8(address) ? 0 : DAT_0_DYNADDR_PARITY);
	dat_w0_write(dat_idx, dat_w0);
}

static void hci_dat_v1_set_static_addr(struct i3c_hci *hci,
				       unsigned int dat_idx, u8 address)
{
	u32 dat_w0;

	dat_w0 = dat_w0_read(dat_idx);
	dat_w0 &= ~DAT_0_STATIC_ADDRESS;
	dat_w0 |= FIELD_PREP(DAT_0_STATIC_ADDRESS, address);
	dat_w0_write(dat_idx, dat_w0);
}

static void hci_dat_v1_set_flags(struct i3c_hci *hci, unsigned int dat_idx,
				 u32 w0_flags, u32 w1_flags)
{
	u32 dat_w0, dat_w1;

	dat_w0 = dat_w0_read(dat_idx);
	dat_w1 = dat_w1_read(dat_idx);
	dat_w0 |= w0_flags;
	dat_w1 |= w1_flags;
	dat_w0_write(dat_idx, dat_w0);
	dat_w1_write(dat_idx, dat_w1);
}

static void hci_dat_v1_clear_flags(struct i3c_hci *hci, unsigned int dat_idx,
				   u32 w0_flags, u32 w1_flags)
{
	u32 dat_w0, dat_w1;

	dat_w0 = dat_w0_read(dat_idx);
	dat_w1 = dat_w1_read(dat_idx);
	dat_w0 &= ~w0_flags;
	dat_w1 &= ~w1_flags;
	dat_w0_write(dat_idx, dat_w0);
	dat_w1_write(dat_idx, dat_w1);
}

static int hci_dat_v1_get_index(struct i3c_hci *hci, u8 dev_addr)
{
	unsigned int dat_idx;
	u32 dat_w0;

	for_each_set_bit(dat_idx, hci->DAT_data, hci->DAT_entries) {
		dat_w0 = dat_w0_read(dat_idx);
		if (FIELD_GET(DAT_0_DYNAMIC_ADDRESS, dat_w0) == dev_addr)
			return dat_idx;
	}

	return -ENODEV;
}

static void hci_dat_v1_restore(struct i3c_hci *hci)
{
	for (int i = 0; i < hci->DAT_entries; i++) {
		writel(hci->DAT[i].w0, hci->DAT_regs + i * 8);
		writel(hci->DAT[i].w1, hci->DAT_regs + i * 8 + 4);
	}
}

const struct hci_dat_ops mipi_i3c_hci_dat_v1 = {
	.init			= hci_dat_v1_init,
	.alloc_entry		= hci_dat_v1_alloc_entry,
	.free_entry		= hci_dat_v1_free_entry,
	.set_dynamic_addr	= hci_dat_v1_set_dynamic_addr,
	.set_static_addr	= hci_dat_v1_set_static_addr,
	.set_flags		= hci_dat_v1_set_flags,
	.clear_flags		= hci_dat_v1_clear_flags,
	.get_index		= hci_dat_v1_get_index,
	.restore		= hci_dat_v1_restore,
};
