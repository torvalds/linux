/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc.
 *
 * Author: Sean Wang <sean.wang@mediatek.com>
 */

#ifndef __MT76S_H
#define __MT76S_H

#define MT_PSE_PAGE_SZ			128

#define MCR_WCIR			0x0000
#define MCR_WHLPCR			0x0004
#define WHLPCR_FW_OWN_REQ_CLR		BIT(9)
#define WHLPCR_FW_OWN_REQ_SET		BIT(8)
#define WHLPCR_IS_DRIVER_OWN		BIT(8)
#define WHLPCR_INT_EN_CLR		BIT(1)
#define WHLPCR_INT_EN_SET		BIT(0)

#define MCR_WSDIOCSR			0x0008
#define MCR_WHCR			0x000C
#define W_INT_CLR_CTRL			BIT(1)
#define RECV_MAILBOX_RD_CLR_EN		BIT(2)
#define MAX_HIF_RX_LEN_NUM		GENMASK(13, 8)
#define RX_ENHANCE_MODE			BIT(16)

#define MCR_WHISR			0x0010
#define MCR_WHIER			0x0014
#define WHIER_D2H_SW_INT		GENMASK(31, 8)
#define WHIER_FW_OWN_BACK_INT_EN	BIT(7)
#define WHIER_ABNORMAL_INT_EN		BIT(6)
#define WHIER_RX1_DONE_INT_EN		BIT(2)
#define WHIER_RX0_DONE_INT_EN		BIT(1)
#define WHIER_TX_DONE_INT_EN		BIT(0)
#define WHIER_DEFAULT			(WHIER_RX0_DONE_INT_EN	| \
					 WHIER_RX1_DONE_INT_EN	| \
					 WHIER_TX_DONE_INT_EN	| \
					 WHIER_ABNORMAL_INT_EN	| \
					 WHIER_D2H_SW_INT)

#define MCR_WASR			0x0020
#define MCR_WSICR			0x0024
#define MCR_WTSR0			0x0028
#define TQ0_CNT				GENMASK(7, 0)
#define TQ1_CNT				GENMASK(15, 8)
#define TQ2_CNT				GENMASK(23, 16)
#define TQ3_CNT				GENMASK(31, 24)

#define MCR_WTSR1			0x002c
#define TQ4_CNT				GENMASK(7, 0)
#define TQ5_CNT				GENMASK(15, 8)
#define TQ6_CNT				GENMASK(23, 16)
#define TQ7_CNT				GENMASK(31, 24)

#define MCR_WTDR1			0x0034
#define MCR_WRDR0			0x0050
#define MCR_WRDR1			0x0054
#define MCR_WRDR(p)			(0x0050 + 4 * (p))
#define MCR_H2DSM0R			0x0070
#define H2D_SW_INT_READ			BIT(16)
#define H2D_SW_INT_WRITE		BIT(17)

#define MCR_H2DSM1R			0x0074
#define MCR_D2HRM0R			0x0078
#define MCR_D2HRM1R			0x007c
#define MCR_D2HRM2R			0x0080
#define MCR_WRPLR			0x0090
#define RX0_PACKET_LENGTH		GENMASK(15, 0)
#define RX1_PACKET_LENGTH		GENMASK(31, 16)

#define MCR_WTMDR			0x00b0
#define MCR_WTMCR			0x00b4
#define MCR_WTMDPCR0			0x00b8
#define MCR_WTMDPCR1			0x00bc
#define MCR_WPLRCR			0x00d4
#define MCR_WSR				0x00D8
#define MCR_CLKIOCR			0x0100
#define MCR_CMDIOCR			0x0104
#define MCR_DAT0IOCR			0x0108
#define MCR_DAT1IOCR			0x010C
#define MCR_DAT2IOCR			0x0110
#define MCR_DAT3IOCR			0x0114
#define MCR_CLKDLYCR			0x0118
#define MCR_CMDDLYCR			0x011C
#define MCR_ODATDLYCR			0x0120
#define MCR_IDATDLYCR1			0x0124
#define MCR_IDATDLYCR2			0x0128
#define MCR_ILCHCR			0x012C
#define MCR_WTQCR0			0x0130
#define MCR_WTQCR1			0x0134
#define MCR_WTQCR2			0x0138
#define MCR_WTQCR3			0x013C
#define MCR_WTQCR4			0x0140
#define MCR_WTQCR5			0x0144
#define MCR_WTQCR6			0x0148
#define MCR_WTQCR7			0x014C
#define MCR_WTQCR(x)                   (0x130 + 4 * (x))
#define TXQ_CNT_L			GENMASK(15, 0)
#define TXQ_CNT_H			GENMASK(31, 16)

#define MCR_SWPCDBGR			0x0154

struct mt76s_intr {
	u32 isr;
	struct {
		u32 wtqcr[8];
	} tx;
	struct {
		u16 num[2];
		u16 len[2][16];
	} rx;
	u32 rec_mb[2];
} __packed;

#endif
