/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright (c) 2020, MIPI Alliance, Inc.
 *
 * Author: Nicolas Pitre <npitre@baylibre.com>
 *
 * Common DAT related stuff
 */

#ifndef DAT_H
#define DAT_H

/* Global DAT flags */
#define DAT_0_I2C_DEVICE		W0_BIT_(31)
#define DAT_0_SIR_REJECT		W0_BIT_(13)
#define DAT_0_IBI_PAYLOAD		W0_BIT_(12)

struct hci_dat_ops {
	int (*init)(struct i3c_hci *hci);
	void (*cleanup)(struct i3c_hci *hci);
#ifdef CONFIG_ARCH_ASPEED
	int (*alloc_entry)(struct i3c_hci *hci, unsigned int address);
#else
	int (*alloc_entry)(struct i3c_hci *hci);
#endif
	void (*free_entry)(struct i3c_hci *hci, unsigned int dat_idx);
	void (*set_dynamic_addr)(struct i3c_hci *hci, unsigned int dat_idx, u8 addr);
	void (*set_static_addr)(struct i3c_hci *hci, unsigned int dat_idx, u8 addr);
	void (*set_flags)(struct i3c_hci *hci, unsigned int dat_idx, u32 w0, u32 w1);
	void (*clear_flags)(struct i3c_hci *hci, unsigned int dat_idx, u32 w0, u32 w1);
	int (*get_index)(struct i3c_hci *hci, u8 address);
};

extern const struct hci_dat_ops mipi_i3c_hci_dat_v1;

#endif
