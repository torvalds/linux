/**
 * Copyright (c) 2017 Redpine Signals Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __RSI_HAL_H__
#define __RSI_HAL_H__

#define FLASH_WRITE_CHUNK_SIZE		(4 * 1024)
#define FLASH_SECTOR_SIZE		(4 * 1024)

#define FLASH_SIZE_ADDR			0x04000016
#define PING_BUFFER_ADDRESS		0x19000
#define PONG_BUFFER_ADDRESS		0x1a000
#define SWBL_REGIN			0x41050034
#define SWBL_REGOUT			0x4105003c
#define PING_WRITE			0x1
#define PONG_WRITE			0x2

#define BL_CMD_TIMEOUT			2000
#define BL_BURN_TIMEOUT			(50 * 1000)

#define REGIN_VALID			0xA
#define REGIN_INPUT			0xA0
#define REGOUT_VALID			0xAB
#define REGOUT_INVALID			(~0xAB)
#define CMD_PASS			0xAA
#define CMD_FAIL			0xCC

#define LOAD_HOSTED_FW			'A'
#define BURN_HOSTED_FW			'B'
#define PING_VALID			'I'
#define PONG_VALID			'O'
#define PING_AVAIL			'I'
#define PONG_AVAIL			'O'
#define EOF_REACHED			'E'
#define CHECK_CRC			'K'
#define POLLING_MODE			'P'
#define CONFIG_AUTO_READ_MODE		'R'
#define JUMP_TO_ZERO_PC			'J'
#define FW_LOADING_SUCCESSFUL		'S'
#define LOADING_INITIATED		'1'

#define RSI_ULP_RESET_REG		0x161
#define RSI_WATCH_DOG_TIMER_1		0x16c
#define RSI_WATCH_DOG_TIMER_2		0x16d
#define RSI_WATCH_DOG_DELAY_TIMER_1		0x16e
#define RSI_WATCH_DOG_DELAY_TIMER_2		0x16f
#define RSI_WATCH_DOG_TIMER_ENABLE		0x170

#define RSI_ULP_WRITE_0			00
#define RSI_ULP_WRITE_2			02
#define RSI_ULP_WRITE_50		50

#define RSI_RESTART_WDT			BIT(11)
#define RSI_BYPASS_ULP_ON_WDT		BIT(1)

#define RSI_ULP_TIMER_ENABLE		((0xaa000) | RSI_RESTART_WDT |	\
					 RSI_BYPASS_ULP_ON_WDT)
#define RSI_RF_SPI_PROG_REG_BASE_ADDR	0x40080000

#define RSI_GSPI_CTRL_REG0		(RSI_RF_SPI_PROG_REG_BASE_ADDR)
#define RSI_GSPI_CTRL_REG1		(RSI_RF_SPI_PROG_REG_BASE_ADDR + 0x2)
#define RSI_GSPI_DATA_REG0		(RSI_RF_SPI_PROG_REG_BASE_ADDR + 0x4)
#define RSI_GSPI_DATA_REG1		(RSI_RF_SPI_PROG_REG_BASE_ADDR + 0x6)
#define RSI_GSPI_DATA_REG2		(RSI_RF_SPI_PROG_REG_BASE_ADDR + 0x8)

#define RSI_GSPI_CTRL_REG0_VALUE		0x340

#define RSI_GSPI_DMA_MODE			BIT(13)

#define RSI_GSPI_2_ULP			BIT(12)
#define RSI_GSPI_TRIG			BIT(7)
#define RSI_GSPI_READ			BIT(6)
#define RSI_GSPI_RF_SPI_ACTIVE		BIT(8)

/* Boot loader commands */
#define SEND_RPS_FILE			'2'

#define FW_IMAGE_MIN_ADDRESS		(68 * 1024)
#define MAX_FLASH_FILE_SIZE		(400 * 1024) //400K
#define FLASH_START_ADDRESS		16

#define COMMON_HAL_CARD_READY_IND	0x0

#define COMMAN_HAL_WAIT_FOR_CARD_READY	1

#define RSI_DEV_OPMODE_WIFI_ALONE	1
#define RSI_DEV_COEX_MODE_WIFI_ALONE	1

#define BBP_INFO_40MHZ 0x6

struct bl_header {
	__le32 flags;
	__le32 image_no;
	__le32 check_sum;
	__le32 flash_start_address;
	__le32 flash_len;
} __packed;

struct ta_metadata {
	char *name;
	unsigned int address;
};

struct rsi_mgmt_desc {
	__le16 len_qno;
	u8 frame_type;
	u8 misc_flags;
	u8 xtend_desc_size;
	u8 header_len;
	__le16 frame_info;
	u8 rate_info;
	u8 reserved1;
	__le16 bbp_info;
	__le16 seq_ctrl;
	u8 reserved2;
	u8 vap_info;
} __packed;

struct rsi_data_desc {
	__le16 len_qno;
	u8 cfm_frame_type;
	u8 misc_flags;
	u8 xtend_desc_size;
	u8 header_len;
	__le16 frame_info;
	__le16 rate_info;
	__le16 bbp_info;
	__le16 mac_flags;
	u8 qid_tid;
	u8 sta_id;
} __packed;

int rsi_hal_device_init(struct rsi_hw *adapter);

#endif
