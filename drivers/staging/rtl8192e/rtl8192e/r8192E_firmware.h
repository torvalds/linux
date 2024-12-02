/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef __INC_FIRMWARE_H
#define __INC_FIRMWARE_H

#define RTL8192E_BOOT_IMG_FW	"RTL8192E/boot.img"
#define RTL8192E_MAIN_IMG_FW	"RTL8192E/main.img"
#define RTL8192E_DATA_IMG_FW	"RTL8192E/data.img"

enum firmware_init_step {
	FW_INIT_STEP0_BOOT = 0,
	FW_INIT_STEP1_MAIN = 1,
	FW_INIT_STEP2_DATA = 2,
};

enum opt_rst_type {
	OPT_SYSTEM_RESET = 0,
	OPT_FIRMWARE_RESET = 1,
};

enum desc_packet_type {
	DESC_PACKET_TYPE_INIT = 0,
	DESC_PACKET_TYPE_NORMAL = 1,
};

enum firmware_status {
	FW_STATUS_0_INIT = 0,
	FW_STATUS_1_MOVE_BOOT_CODE = 1,
	FW_STATUS_2_MOVE_MAIN_CODE = 2,
	FW_STATUS_3_TURNON_CPU = 3,
	FW_STATUS_4_MOVE_DATA_CODE = 4,
	FW_STATUS_5_READY = 5,
};

#define MAX_FW_SIZE 64000
struct rt_fw_blob {
	u16 size;
	u8 data[MAX_FW_SIZE];
};

#define FW_BLOBS 3
struct rt_firmware {
	enum firmware_status status;
	struct rt_fw_blob blobs[FW_BLOBS];
};

bool rtl92e_init_fw(struct net_device *dev);
#endif
