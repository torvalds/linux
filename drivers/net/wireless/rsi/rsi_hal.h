/*
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

/* Device Operating modes */
#define DEV_OPMODE_WIFI_ALONE		1
#define DEV_OPMODE_BT_ALONE		4
#define DEV_OPMODE_BT_LE_ALONE		8
#define DEV_OPMODE_BT_DUAL		12
#define DEV_OPMODE_STA_BT		5
#define DEV_OPMODE_STA_BT_LE		9
#define DEV_OPMODE_STA_BT_DUAL		13
#define DEV_OPMODE_AP_BT		6
#define DEV_OPMODE_AP_BT_DUAL		14

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

/* Watchdog timer addresses for 9116 */
#define NWP_AHB_BASE_ADDR		0x41300000
#define NWP_WWD_INTERRUPT_TIMER		(NWP_AHB_BASE_ADDR + 0x300)
#define NWP_WWD_SYSTEM_RESET_TIMER	(NWP_AHB_BASE_ADDR + 0x304)
#define NWP_WWD_WINDOW_TIMER		(NWP_AHB_BASE_ADDR + 0x308)
#define NWP_WWD_TIMER_SETTINGS		(NWP_AHB_BASE_ADDR + 0x30C)
#define NWP_WWD_MODE_AND_RSTART		(NWP_AHB_BASE_ADDR + 0x310)
#define NWP_WWD_RESET_BYPASS		(NWP_AHB_BASE_ADDR + 0x314)
#define NWP_FSM_INTR_MASK_REG		(NWP_AHB_BASE_ADDR + 0x104)

/* Watchdog timer values */
#define NWP_WWD_INT_TIMER_CLKS		5
#define NWP_WWD_SYS_RESET_TIMER_CLKS	4
#define NWP_WWD_TIMER_DISABLE		0xAA0001

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

#define FW_FLASH_OFFSET			0x820
#define LMAC_VER_OFFSET_9113		(FW_FLASH_OFFSET + 0x200)
#define LMAC_VER_OFFSET_9116		0x22C2
#define MAX_DWORD_ALIGN_BYTES		64
#define RSI_COMMON_REG_SIZE		2
#define RSI_9116_REG_SIZE		4
#define FW_ALIGN_SIZE			4
#define RSI_9116_FW_MAGIC_WORD		0x5aa5

#define MEM_ACCESS_CTRL_FROM_HOST	0x41300000
#define RAM_384K_ACCESS_FROM_TA		(BIT(2) | BIT(3) | BIT(4) | BIT(5) | \
					 BIT(20) | BIT(21) | BIT(22) | \
					 BIT(23) | BIT(24) | BIT(25))

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

#define RSI_BL_CTRL_LEN_MASK			0xFFFFFF
#define RSI_BL_CTRL_SPI_32BIT_MODE		BIT(27)
#define RSI_BL_CTRL_REL_TA_SOFTRESET		BIT(28)
#define RSI_BL_CTRL_START_FROM_ROM_PC		BIT(29)
#define RSI_BL_CTRL_SPI_8BIT_MODE		BIT(30)
#define RSI_BL_CTRL_LAST_ENTRY			BIT(31)
struct bootload_entry {
	__le32 control;
	__le32 dst_addr;
} __packed;

struct bootload_ds {
	__le16 fixed_pattern;
	__le16 offset;
	__le32 reserved;
	struct bootload_entry bl_entry[7];
} __packed;

struct rsi_mgmt_desc {
	__le16 len_qno;
	u8 frame_type;
	u8 misc_flags;
	u8 xtend_desc_size;
	u8 header_len;
	__le16 frame_info;
	__le16 rate_info;
	__le16 bbp_info;
	__le16 seq_ctrl;
	u8 reserved2;
	u8 sta_id;
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

struct rsi_bt_desc {
	__le16 len_qno;
	__le16 reserved1;
	__le32 reserved2;
	__le32 reserved3;
	__le16 reserved4;
	__le16 bt_pkt_type;
} __packed;

int rsi_hal_device_init(struct rsi_hw *adapter);
int rsi_prepare_mgmt_desc(struct rsi_common *common, struct sk_buff *skb);
int rsi_prepare_data_desc(struct rsi_common *common, struct sk_buff *skb);
int rsi_prepare_beacon(struct rsi_common *common, struct sk_buff *skb);
int rsi_send_pkt_to_bus(struct rsi_common *common, struct sk_buff *skb);
int rsi_send_bt_pkt(struct rsi_common *common, struct sk_buff *skb);

#endif
