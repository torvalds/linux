/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2010 - 2017 Novatek, Inc.
 * Copyright (C) 2020 AngeloGioacchino Del Regno <kholk11@gmail.com>
 */

#ifndef NT36XXX_H
#define NT36XXX_H

#define NT36XXX_INPUT_DEVICE_NAME	"Novatek NT36XXX Touch Sensor"

/* These chips have this fixed address when in bootloader :( */
#define NT36XXX_BLDR_ADDR 0x01

/* Input device info */
#define NVT_TS_NAME "NVTCapacitiveTouchScreen"

/* Number of bytes for chip identification */
#define NT36XXX_ID_LEN_MAX	6

/* Touch info */
#define TOUCH_DEFAULT_MAX_WIDTH  1080
#define TOUCH_DEFAULT_MAX_HEIGHT 2246
#define TOUCH_MAX_FINGER_NUM	 10
#define TOUCH_MAX_PRESSURE	 1000

/* Point data length */
#define POINT_DATA_LEN		65

/* Global pages */
#define NT36XXX_PAGE_CHIP_INFO	0x0001f64e
#define NT36XXX_PAGE_CRC	0x0003f135

/* Misc */
#define NT36XXX_NUM_SUPPLIES	 2
#define NT36XXX_MAX_RETRIES	 5
#define NT36XXX_MAX_FW_RST_RETRY 50

struct nt36xxx_abs_object {
	u16 x;
	u16 y;
	u16 z;
	u8 tm;
};

struct nt36xxx_fw_info {
	u8 fw_ver;
	u8 x_num;
	u8 y_num;
	u8 max_buttons;
	u16 abs_x_max;
	u16 abs_y_max;
	u16 nvt_pid;
};

struct nt36xxx_mem_map {
	u32 evtbuf_addr;
	u32 pipe0_addr;
	u32 pipe1_addr;
	u32 flash_csum_addr;
	u32 flash_data_addr;
};

struct nt36xxx_i2c {
	struct i2c_client *client;
	struct input_dev *input;
	struct regulator_bulk_data *supplies;
	struct gpio_desc *reset_gpio;

	struct work_struct ts_work;
	struct workqueue_struct *ts_workq;
	struct mutex lock;

	struct nt36xxx_fw_info fw_info;
	struct nt36xxx_abs_object abs_obj;

	const struct nt36xxx_mem_map *mmap;
	u8 max_fingers;
};

enum nt36xxx_chips {
	NT36525_IC = 0,
	NT36672A_IC,
	NT36676F_IC,
	NT36772_IC,
	NT36870_IC,
	NTMAX_IC,
};

struct nt36xxx_trim_table {
	u8 id[NT36XXX_ID_LEN_MAX];
	u8 mask[NT36XXX_ID_LEN_MAX];
	enum nt36xxx_chips mapid;
};

enum nt36xxx_cmds {
	NT36XXX_CMD_ENTER_SLEEP = 0x11,
	NT36XXX_CMD_ENTER_WKUP_GESTURE = 0x13,
	NT36XXX_CMD_UNLOCK = 0x35,
	NT36XXX_CMD_BOOTLOADER_RESET = 0x69,
	NT36XXX_CMD_SW_RESET = 0xa5,
	NT36XXX_CMD_SET_PAGE = 0xff,
};

enum nt36xxx_fw_state {
	NT36XXX_STATE_INIT = 0xa0,	/* IC reset */
	NT36XXX_STATE_REK,		/* ReK baseline */
	NT36XXX_STATE_REK_FINISH,	/* Baseline is ready */
	NT36XXX_STATE_NORMAL_RUN,	/* Normal run */
	NT36XXX_STATE_MAX = 0xaf
};

enum nt36xxx_i2c_events {
	NT36XXX_EVT_CHIPID = 0x4e,
	NT36XXX_EVT_HOST_CMD = 0x50,
	NT36XXX_EVT_HS_OR_SUBCMD = 0x51,   /* Handshake or subcommand byte */
	NT36XXX_EVT_RESET_COMPLETE = 0x60,
	NT36XXX_EVT_FWINFO = 0x78,
	NT36XXX_EVT_PROJECTID = 0x9a,
};

#endif
