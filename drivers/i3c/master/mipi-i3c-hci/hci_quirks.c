// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * I3C HCI Quirks
 *
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * Authors: Shyam Sundar S K <Shyam-sundar.S-k@amd.com>
 *	    Guruvendra Punugupati <Guruvendra.Punugupati@amd.com>
 */

#include <linux/i3c/master.h>
#include "hci.h"

/* Timing registers */
#define HCI_SCL_I3C_OD_TIMING          0x214
#define HCI_SCL_I3C_PP_TIMING          0x218
#define HCI_SDA_HOLD_SWITCH_DLY_TIMING 0x230

/* Timing values to configure 9MHz frequency */
#define AMD_SCL_I3C_OD_TIMING          0x00cf00cf
#define AMD_SCL_I3C_PP_TIMING          0x00160016

#define QUEUE_THLD_CTRL                0xD0

void amd_set_od_pp_timing(struct i3c_hci *hci)
{
	u32 data;

	reg_write(HCI_SCL_I3C_OD_TIMING, AMD_SCL_I3C_OD_TIMING);
	reg_write(HCI_SCL_I3C_PP_TIMING, AMD_SCL_I3C_PP_TIMING);
	data = reg_read(HCI_SDA_HOLD_SWITCH_DLY_TIMING);
	/* Configure maximum TX hold time */
	data |= W0_MASK(18, 16);
	reg_write(HCI_SDA_HOLD_SWITCH_DLY_TIMING, data);
}

void amd_set_resp_buf_thld(struct i3c_hci *hci)
{
	u32 data;

	data = reg_read(QUEUE_THLD_CTRL);
	data = data & ~W0_MASK(15, 8);
	reg_write(QUEUE_THLD_CTRL, data);
}
