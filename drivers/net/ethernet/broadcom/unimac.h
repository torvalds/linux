/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __UNIMAC_H
#define __UNIMAC_H

#define UMAC_HD_BKP_CTRL		0x004
#define  HD_FC_EN			(1 << 0)
#define  HD_FC_BKOFF_OK			(1 << 1)
#define  IPG_CONFIG_RX_SHIFT		2
#define  IPG_CONFIG_RX_MASK		0x1F
#define UMAC_CMD			0x008
#define  CMD_TX_EN			(1 << 0)
#define  CMD_RX_EN			(1 << 1)
#define  CMD_SPEED_10			0
#define  CMD_SPEED_100			1
#define  CMD_SPEED_1000			2
#define  CMD_SPEED_2500			3
#define  CMD_SPEED_SHIFT		2
#define  CMD_SPEED_MASK			3
#define  CMD_PROMISC			(1 << 4)
#define  CMD_PAD_EN			(1 << 5)
#define  CMD_CRC_FWD			(1 << 6)
#define  CMD_PAUSE_FWD			(1 << 7)
#define  CMD_RX_PAUSE_IGNORE		(1 << 8)
#define  CMD_TX_ADDR_INS		(1 << 9)
#define  CMD_HD_EN			(1 << 10)
#define  CMD_SW_RESET_OLD		(1 << 11)
#define  CMD_SW_RESET			(1 << 13)
#define  CMD_LCL_LOOP_EN		(1 << 15)
#define  CMD_AUTO_CONFIG		(1 << 22)
#define  CMD_CNTL_FRM_EN		(1 << 23)
#define  CMD_NO_LEN_CHK			(1 << 24)
#define  CMD_RMT_LOOP_EN		(1 << 25)
#define  CMD_RX_ERR_DISC		(1 << 26)
#define  CMD_PRBL_EN			(1 << 27)
#define  CMD_TX_PAUSE_IGNORE		(1 << 28)
#define  CMD_TX_RX_EN			(1 << 29)
#define  CMD_RUNT_FILTER_DIS		(1 << 30)
#define UMAC_MAC0			0x00c
#define UMAC_MAC1			0x010
#define UMAC_MAX_FRAME_LEN		0x014
#define UMAC_PAUSE_QUANTA		0x018
#define UMAC_MODE			0x044
#define  MODE_LINK_STATUS		(1 << 5)
#define UMAC_FRM_TAG0			0x048		/* outer tag */
#define UMAC_FRM_TAG1			0x04c		/* inner tag */
#define UMAC_TX_IPG_LEN			0x05c
#define UMAC_EEE_CTRL			0x064
#define  EN_LPI_RX_PAUSE		(1 << 0)
#define  EN_LPI_TX_PFC			(1 << 1)
#define  EN_LPI_TX_PAUSE		(1 << 2)
#define  EEE_EN				(1 << 3)
#define  RX_FIFO_CHECK			(1 << 4)
#define  EEE_TX_CLK_DIS			(1 << 5)
#define  DIS_EEE_10M			(1 << 6)
#define  LP_IDLE_PREDICTION_MODE	(1 << 7)
#define UMAC_EEE_LPI_TIMER		0x068
#define UMAC_EEE_WAKE_TIMER		0x06C
#define UMAC_EEE_REF_COUNT		0x070
#define  EEE_REFERENCE_COUNT_MASK	0xffff
#define UMAC_RX_IPG_INV			0x078
#define UMAC_MACSEC_PROG_TX_CRC		0x310
#define UMAC_MACSEC_CTRL		0x314
#define UMAC_PAUSE_CTRL			0x330
#define UMAC_TX_FLUSH			0x334
#define UMAC_RX_FIFO_STATUS		0x338
#define UMAC_TX_FIFO_STATUS		0x33c

#endif
