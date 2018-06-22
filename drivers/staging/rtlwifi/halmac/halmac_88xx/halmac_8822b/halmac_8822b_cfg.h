/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef _HALMAC_8822B_CFG_H_
#define _HALMAC_8822B_CFG_H_

#include "halmac_8822b_pwr_seq.h"
#include "halmac_api_8822b.h"
#include "halmac_api_8822b_usb.h"
#include "halmac_api_8822b_sdio.h"
#include "halmac_api_8822b_pcie.h"
#include "../../halmac_bit2.h"
#include "../../halmac_reg2.h"
#include "../../halmac_api.h"

#define HALMAC_TX_FIFO_SIZE_8822B 262144 /* 256k */
#define HALMAC_TX_FIFO_SIZE_LA_8822B 131072 /* 128k */
#define HALMAC_RX_FIFO_SIZE_8822B 24576 /* 24k */
#define HALMAC_TX_PAGE_SIZE_8822B 128 /* PageSize 128Byte */
#define HALMAC_TX_ALIGN_SIZE_8822B 8
#define HALMAC_TX_PAGE_SIZE_2_POWER_8822B 7 /* 128 = 2^7 */
#define HALMAC_SECURITY_CAM_ENTRY_NUM_8822B 64 /* CAM Entry size */
#define HALMAC_TX_AGG_ALIGNMENT_SIZE_8822B 8
#define HALMAC_TX_DESC_SIZE_8822B 48
#define HALMAC_RX_DESC_SIZE_8822B 24
#define HALMAC_RX_DESC_DUMMY_SIZE_MAX_8822B 120
#define HALMAC_C2H_PKT_BUF_8822B 256
#define HALMAC_RX_FIFO_EXPANDING_MODE_PKT_SIZE_MAX_8822B 80 /* align 8 Byte*/
#define HALMAC_RX_FIFO_EXPANDING_UNIT_8822B                                    \
	(HALMAC_RX_DESC_SIZE_8822B + HALMAC_RX_DESC_DUMMY_SIZE_MAX_8822B +     \
	 HALMAC_RX_FIFO_EXPANDING_MODE_PKT_SIZE) /* align 8 Byte*/
#define HALMAC_RX_FIFO_EXPANDING_UNIT_MAX_8822B                                \
	(HALMAC_RX_DESC_SIZE_8822B + HALMAC_RX_DESC_DUMMY_SIZE_MAX_8822B +     \
	 HALMAC_RX_FIFO_EXPANDING_MODE_PKT_SIZE_MAX_8822B) /* align 8 Byte*/

#define HALMAC_TX_FIFO_SIZE_EX_1_BLK_8822B 196608 /* 192k */
#define HALMAC_RX_FIFO_SIZE_EX_1_BLK_8822B                                     \
	((((HALMAC_RX_FIFO_EXPANDING_UNIT_8822B << 8) - 1) >> 10)              \
	 << 10) /* < 56k*/
#define HALMAC_RX_FIFO_SIZE_EX_1_BLK_MAX_8822B                                 \
	((((HALMAC_RX_FIFO_EXPANDING_UNIT_MAX_8822B << 8) - 1) >> 10)          \
	 << 10) /* 55k*/
#define HALMAC_TX_FIFO_SIZE_EX_2_BLK_8822B 131072 /* 128k */
#define HALMAC_RX_FIFO_SIZE_EX_2_BLK_8822B 155648 /* 152k */
#define HALMAC_TX_FIFO_SIZE_EX_3_BLK_8822B 65536 /* 64k */
#define HALMAC_RX_FIFO_SIZE_EX_3_BLK_8822B 221184 /* 216k */

/* TXFIFO LAYOUT
 * HIGH_QUEUE
 * NORMAL_QUEUE
 * LOW_QUEUE
 * EXTRA_QUEUE
 * PUBLIC_QUEUE -- decided after all other queue are defined
 * GAP_QUEUE -- Used to separate AC queue and Rsvd page
 *
 * RSVD_DRIVER -- Driver used rsvd page area
 * RSVD_H2C_EXTRAINFO -- Extra Information for h2c
 * RSVD_H2C_QUEUE -- h2c queue in rsvd page
 * RSVD_CPU_INSTRUCTION -- extend fw code
 * RSVD_FW_TXBUFF -- fw used this area to send packet
 *
 * Symbol: HALMAC_MODE_QUEUE_UNIT_CHIP, ex: HALMAC_LB_2BULKOUT_FWCMD_PGNUM_8822B
 */
#define HALMAC_EXTRA_INFO_BUFF_SIZE_FULL_FIFO_8822B                            \
	16384 /*16K, only used in init case*/

#define HALMAC_RSVD_DRV_PGNUM_8822B 16 /*2048*/
#define HALMAC_RSVD_H2C_EXTRAINFO_PGNUM_8822B 32 /*4096*/
#define HALMAC_RSVD_H2C_QUEUE_PGNUM_8822B 8 /*1024*/
#define HALMAC_RSVD_CPU_INSTRUCTION_PGNUM_8822B 0 /*0*/
#define HALMAC_RSVD_FW_TXBUFF_PGNUM_8822B 4 /*512*/

#define HALMAC_EFUSE_SIZE_8822B 1024 /* 0x400 */
#define HALMAC_BT_EFUSE_SIZE_8822B 128 /* 0x80 */
#define HALMAC_EEPROM_SIZE_8822B 0x300
#define HALMAC_CR_TRX_ENABLE_8822B                                             \
	(BIT_HCI_TXDMA_EN | BIT_HCI_RXDMA_EN | BIT_TXDMA_EN | BIT_RXDMA_EN |   \
	 BIT_PROTOCOL_EN | BIT_SCHEDULE_EN | BIT_MACTXEN | BIT_MACRXEN)

#define HALMAC_BLK_DESC_NUM_8822B 0x3 /* Only for USB */

/* AMPDU max time (unit : 32us) */
#define HALMAC_AMPDU_MAX_TIME_8822B 0x70

/* Protect mode control */
#define HALMAC_PROT_RTS_LEN_TH_8822B 0xFF
#define HALMAC_PROT_RTS_TX_TIME_TH_8822B 0x08
#define HALMAC_PROT_MAX_AGG_PKT_LIMIT_8822B 0x20
#define HALMAC_PROT_RTS_MAX_AGG_PKT_LIMIT_8822B 0x20

/* Fast EDCA setting */
#define HALMAC_FAST_EDCA_VO_TH_8822B 0x06
#define HALMAC_FAST_EDCA_VI_TH_8822B 0x06
#define HALMAC_FAST_EDCA_BE_TH_8822B 0x06
#define HALMAC_FAST_EDCA_BK_TH_8822B 0x06

/* BAR setting */
#define HALMAC_BAR_RETRY_LIMIT_8822B 0x01
#define HALMAC_RA_TRY_RATE_AGG_LIMIT_8822B 0x08

enum halmac_normal_rxagg_th_to_8822b {
	HALMAC_NORMAL_RXAGG_THRESHOLD_8822B = 0xFF,
	HALMAC_NORMAL_RXAGG_TIMEOUT_8822B = 0x01,
};

enum halmac_loopback_rxagg_th_to_8822b {
	HALMAC_LOOPBACK_RXAGG_THRESHOLD_8822B = 0xFF,
	HALMAC_LOOPBACK_RXAGG_TIMEOUT_8822B = 0x01,
};

#endif
