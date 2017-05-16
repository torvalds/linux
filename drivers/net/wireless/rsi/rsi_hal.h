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

/* Boot loader commands */
#define SEND_RPS_FILE			'2'

#define FW_IMAGE_MIN_ADDRESS		(68 * 1024)
#define MAX_FLASH_FILE_SIZE		(400 * 1024) //400K
#define FLASH_START_ADDRESS		16

#define COMMON_HAL_CARD_READY_IND	0x0

#define COMMAN_HAL_WAIT_FOR_CARD_READY	1

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

int rsi_hal_device_init(struct rsi_hw *adapter);

#endif
