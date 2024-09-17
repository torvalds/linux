// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2020, MIPI Alliance, Inc.
 *
 * Author: Nicolas Pitre <npitre@baylibre.com>
 */

#include <linux/device.h>
#include <linux/bitfield.h>
#include <linux/i3c/master.h>
#include <linux/io.h>

#include "hci.h"
#include "dct.h"

/*
 * Device Characteristic Table
 */

void i3c_hci_dct_get_val(struct i3c_hci *hci, unsigned int dct_idx,
			 u64 *pid, unsigned int *dcr, unsigned int *bcr)
{
	void __iomem *reg = hci->DCT_regs + dct_idx * 4 * 4;
	u32 dct_entry_data[4];
	unsigned int i;

	for (i = 0; i < 4; i++) {
		dct_entry_data[i] = readl(reg);
		reg += 4;
	}

	*pid = ((u64)dct_entry_data[0]) << (47 - 32 + 1) |
	       FIELD_GET(W1_MASK(47, 32), dct_entry_data[1]);
	*dcr = FIELD_GET(W2_MASK(71, 64), dct_entry_data[2]);
	*bcr = FIELD_GET(W2_MASK(79, 72), dct_entry_data[2]);
}
