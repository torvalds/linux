/*
 * iwmc3200top - Intel Wireless MultiCom 3200 Top Driver
 * drivers/misc/iwmc3200top/iwmc3200top.h
 *
 * Copyright (C) 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * Author Name: Maxim Grabarnik <maxim.grabarnink@intel.com>
 *  -
 *
 */

#ifndef __IWMC3200TOP_H__
#define __IWMC3200TOP_H__

#include <linux/workqueue.h>

#define DRV_NAME "iwmc3200top"
#define FW_API_VER 1
#define _FW_NAME(api) DRV_NAME "." #api ".fw"
#define FW_NAME(api) _FW_NAME(api)

#define IWMC_SDIO_BLK_SIZE			256
#define IWMC_DEFAULT_TR_BLK			64
#define IWMC_SDIO_DATA_ADDR			0x0
#define IWMC_SDIO_INTR_ENABLE_ADDR		0x14
#define IWMC_SDIO_INTR_STATUS_ADDR		0x13
#define IWMC_SDIO_INTR_CLEAR_ADDR		0x13
#define IWMC_SDIO_INTR_GET_SIZE_ADDR		0x2C

#define COMM_HUB_HEADER_LENGTH 16
#define LOGGER_HEADER_LENGTH   10


#define BARKER_DNLOAD_BT_POS		0
#define BARKER_DNLOAD_BT_MSK		BIT(BARKER_DNLOAD_BT_POS)
#define BARKER_DNLOAD_GPS_POS		1
#define BARKER_DNLOAD_GPS_MSK		BIT(BARKER_DNLOAD_GPS_POS)
#define BARKER_DNLOAD_TOP_POS		2
#define BARKER_DNLOAD_TOP_MSK		BIT(BARKER_DNLOAD_TOP_POS)
#define BARKER_DNLOAD_RESERVED1_POS	3
#define BARKER_DNLOAD_RESERVED1_MSK	BIT(BARKER_DNLOAD_RESERVED1_POS)
#define BARKER_DNLOAD_JUMP_POS		4
#define BARKER_DNLOAD_JUMP_MSK		BIT(BARKER_DNLOAD_JUMP_POS)
#define BARKER_DNLOAD_SYNC_POS		5
#define BARKER_DNLOAD_SYNC_MSK		BIT(BARKER_DNLOAD_SYNC_POS)
#define BARKER_DNLOAD_RESERVED2_POS	6
#define BARKER_DNLOAD_RESERVED2_MSK	(0x3 << BARKER_DNLOAD_RESERVED2_POS)
#define BARKER_DNLOAD_BARKER_POS	8
#define BARKER_DNLOAD_BARKER_MSK	(0xffffff << BARKER_DNLOAD_BARKER_POS)

#define IWMC_BARKER_REBOOT 	(0xdeadbe << BARKER_DNLOAD_BARKER_POS)
/* whole field barker */
#define IWMC_BARKER_ACK 	0xfeedbabe

#define IWMC_CMD_SIGNATURE 	0xcbbc

#define CMD_HDR_OPCODE_POS		0
#define CMD_HDR_OPCODE_MSK_MSK		(0xf << CMD_HDR_OPCODE_MSK_POS)
#define CMD_HDR_RESPONSE_CODE_POS	4
#define CMD_HDR_RESPONSE_CODE_MSK	(0xf << CMD_HDR_RESPONSE_CODE_POS)
#define CMD_HDR_USE_CHECKSUM_POS	8
#define CMD_HDR_USE_CHECKSUM_MSK	BIT(CMD_HDR_USE_CHECKSUM_POS)
#define CMD_HDR_RESPONSE_REQUIRED_POS	9
#define CMD_HDR_RESPONSE_REQUIRED_MSK	BIT(CMD_HDR_RESPONSE_REQUIRED_POS)
#define CMD_HDR_DIRECT_ACCESS_POS	10
#define CMD_HDR_DIRECT_ACCESS_MSK	BIT(CMD_HDR_DIRECT_ACCESS_POS)
#define CMD_HDR_RESERVED_POS		11
#define CMD_HDR_RESERVED_MSK		BIT(0x1f << CMD_HDR_RESERVED_POS)
#define CMD_HDR_SIGNATURE_POS		16
#define CMD_HDR_SIGNATURE_MSK		BIT(0xffff << CMD_HDR_SIGNATURE_POS)

enum {
	IWMC_OPCODE_PING = 0,
	IWMC_OPCODE_READ = 1,
	IWMC_OPCODE_WRITE = 2,
	IWMC_OPCODE_JUMP = 3,
	IWMC_OPCODE_REBOOT = 4,
	IWMC_OPCODE_PERSISTENT_WRITE = 5,
	IWMC_OPCODE_PERSISTENT_READ = 6,
	IWMC_OPCODE_READ_MODIFY_WRITE = 7,
	IWMC_OPCODE_LAST_COMMAND = 15
};

struct iwmct_fw_load_hdr {
	__le32 cmd;
	__le32 target_addr;
	__le32 data_size;
	__le32 block_chksm;
	u8 data[0];
};

/**
 * struct iwmct_fw_hdr
 * holds all sw components versions
 */
struct iwmct_fw_hdr {
	u8 top_major;
	u8 top_minor;
	u8 top_revision;
	u8 gps_major;
	u8 gps_minor;
	u8 gps_revision;
	u8 bt_major;
	u8 bt_minor;
	u8 bt_revision;
	u8 tic_name[31];
};

/**
 * struct iwmct_fw_sec_hdr
 * @type: function type
 * @data_size: section's data size
 * @target_addr: download address
 */
struct iwmct_fw_sec_hdr {
	u8 type[4];
	__le32 data_size;
	__le32 target_addr;
};

/**
 * struct iwmct_parser
 * @file: fw image
 * @file_size: fw size
 * @cur_pos: position in file
 * @buf: temp buf for download
 * @buf_size: size of buf
 * @entry_point: address to jump in fw kick-off
 */
struct iwmct_parser {
	const u8 *file;
	size_t file_size;
	size_t cur_pos;
	u8 *buf;
	size_t buf_size;
	u32 entry_point;
	struct iwmct_fw_hdr versions;
};


struct iwmct_work_struct {
	struct list_head list;
	ssize_t iosize;
};

struct iwmct_dbg {
	int blocks;
	bool dump;
	bool jump;
	bool direct;
	bool checksum;
	bool fw_download;
	int block_size;
	int download_trans_blks;

	char label_fw[256];
};

struct iwmct_debugfs;

struct iwmct_priv {
	struct sdio_func *func;
	struct iwmct_debugfs *dbgfs;
	struct iwmct_parser parser;
	atomic_t reset;
	atomic_t dev_sync;
	u32 trans_len;
	u32 barker;
	struct iwmct_dbg dbg;

	/* drivers work queue */
	struct workqueue_struct *wq;
	struct workqueue_struct *bus_rescan_wq;
	struct work_struct bus_rescan_worker;
	struct work_struct isr_worker;

	/* drivers wait queue */
	wait_queue_head_t wait_q;

	/* rx request list */
	struct list_head read_req_list;
};

extern int iwmct_tx(struct iwmct_priv *priv, unsigned int addr,
		void *src, int count);

extern int iwmct_fw_load(struct iwmct_priv *priv);

extern void iwmct_dbg_init_params(struct iwmct_priv *drv);
extern void iwmct_dbg_init_drv_attrs(struct device_driver *drv);
extern void iwmct_dbg_remove_drv_attrs(struct device_driver *drv);
extern int iwmct_send_hcmd(struct iwmct_priv *priv, u8 *cmd, u16 len);

#endif  /*  __IWMC3200TOP_H__  */
