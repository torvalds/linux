/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Header file for Host Controller UHS2 related registers.
 *
 *  Copyright (C) 2014 Intel Corp, All Rights Reserved.
 */
#ifndef __SDHCI_UHS2_H
#define __SDHCI_UHS2_H

#include <linux/bits.h>

/* SDHCI Category C registers : UHS2 usage */

#define  SDHCI_UHS2_CM_TRAN_RESP		0x10
#define  SDHCI_UHS2_SD_TRAN_RESP		0x18
#define  SDHCI_UHS2_SD_TRAN_RESP_1		0x1C

/* SDHCI Category B registers : UHS2 only */

#define SDHCI_UHS2_BLOCK_SIZE			0x80
#define  SDHCI_UHS2_MAKE_BLKSZ(dma, blksz)	((((dma) & 0x7) << 12) | ((blksz) & 0xFFF))

#define SDHCI_UHS2_BLOCK_COUNT			0x84

#define SDHCI_UHS2_CMD_PACKET			0x88
#define  SDHCI_UHS2_CMD_PACK_MAX_LEN		20

#define SDHCI_UHS2_TRANS_MODE			0x9C
#define  SDHCI_UHS2_TRNS_DMA			BIT(0)
#define  SDHCI_UHS2_TRNS_BLK_CNT_EN		BIT(1)
#define  SDHCI_UHS2_TRNS_DATA_TRNS_WRT		BIT(4)
#define  SDHCI_UHS2_TRNS_BLK_BYTE_MODE		BIT(5)
#define  SDHCI_UHS2_TRNS_RES_R5			BIT(6)
#define  SDHCI_UHS2_TRNS_RES_ERR_CHECK_EN	BIT(7)
#define  SDHCI_UHS2_TRNS_RES_INT_DIS		BIT(8)
#define  SDHCI_UHS2_TRNS_WAIT_EBSY		BIT(14)
#define  SDHCI_UHS2_TRNS_2L_HD			BIT(15)

#define SDHCI_UHS2_CMD				0x9E
#define  SDHCI_UHS2_CMD_SUB_CMD			BIT(2)
#define  SDHCI_UHS2_CMD_DATA			BIT(5)
#define  SDHCI_UHS2_CMD_TRNS_ABORT		BIT(6)
#define  SDHCI_UHS2_CMD_CMD12			BIT(7)
#define  SDHCI_UHS2_CMD_DORMANT			GENMASK(7, 6)
#define  SDHCI_UHS2_CMD_PACK_LEN_MASK		GENMASK(12, 8)

#define SDHCI_UHS2_RESPONSE			0xA0
#define  SDHCI_UHS2_RESPONSE_MAX_LEN		20

#define SDHCI_UHS2_MSG_SELECT			0xB4
#define SDHCI_UHS2_MSG_SELECT_CURR		0x0
#define SDHCI_UHS2_MSG_SELECT_ONE		0x1
#define SDHCI_UHS2_MSG_SELECT_TWO		0x2
#define SDHCI_UHS2_MSG_SELECT_THREE		0x3

#define SDHCI_UHS2_MSG				0xB8

#define SDHCI_UHS2_DEV_INT_STATUS		0xBC

#define SDHCI_UHS2_DEV_SELECT			0xBE
#define SDHCI_UHS2_DEV_SEL_MASK			GENMASK(3, 0)
#define SDHCI_UHS2_DEV_SEL_INT_MSG_EN		BIT(7)

#define SDHCI_UHS2_DEV_INT_CODE			0xBF

#define SDHCI_UHS2_SW_RESET			0xC0
#define SDHCI_UHS2_SW_RESET_FULL		BIT(0)
#define SDHCI_UHS2_SW_RESET_SD			BIT(1)

#define SDHCI_UHS2_TIMER_CTRL			0xC2
#define SDHCI_UHS2_TIMER_CTRL_DEADLOCK_MASK	GENMASK(7, 4)

#define SDHCI_UHS2_INT_STATUS			0xC4
#define SDHCI_UHS2_INT_STATUS_ENABLE		0xC8
#define SDHCI_UHS2_INT_SIGNAL_ENABLE		0xCC
#define SDHCI_UHS2_INT_HEADER_ERR		BIT(0)
#define SDHCI_UHS2_INT_RES_ERR			BIT(1)
#define SDHCI_UHS2_INT_RETRY_EXP		BIT(2)
#define SDHCI_UHS2_INT_CRC			BIT(3)
#define SDHCI_UHS2_INT_FRAME_ERR		BIT(4)
#define SDHCI_UHS2_INT_TID_ERR			BIT(5)
#define SDHCI_UHS2_INT_UNRECOVER		BIT(7)
#define SDHCI_UHS2_INT_EBUSY_ERR		BIT(8)
#define SDHCI_UHS2_INT_ADMA_ERROR		BIT(15)
#define SDHCI_UHS2_INT_CMD_TIMEOUT		BIT(16)
#define SDHCI_UHS2_INT_DEADLOCK_TIMEOUT		BIT(17)
#define SDHCI_UHS2_INT_VENDOR_ERR		BIT(27)
#define SDHCI_UHS2_INT_ERROR_MASK	       ( \
		SDHCI_UHS2_INT_HEADER_ERR      | \
		SDHCI_UHS2_INT_RES_ERR	       | \
		SDHCI_UHS2_INT_RETRY_EXP       | \
		SDHCI_UHS2_INT_CRC	       | \
		SDHCI_UHS2_INT_FRAME_ERR       | \
		SDHCI_UHS2_INT_TID_ERR	       | \
		SDHCI_UHS2_INT_UNRECOVER       | \
		SDHCI_UHS2_INT_EBUSY_ERR       | \
		SDHCI_UHS2_INT_ADMA_ERROR      | \
		SDHCI_UHS2_INT_CMD_TIMEOUT     | \
		SDHCI_UHS2_INT_DEADLOCK_TIMEOUT)
#define SDHCI_UHS2_INT_CMD_ERR_MASK	  ( \
		SDHCI_UHS2_INT_HEADER_ERR | \
		SDHCI_UHS2_INT_RES_ERR	  | \
		SDHCI_UHS2_INT_FRAME_ERR  | \
		SDHCI_UHS2_INT_TID_ERR	  | \
		SDHCI_UHS2_INT_CMD_TIMEOUT)
/* CRC Error occurs during a packet receiving */
#define SDHCI_UHS2_INT_DATA_ERR_MASK	       ( \
		SDHCI_UHS2_INT_RETRY_EXP       | \
		SDHCI_UHS2_INT_CRC	       | \
		SDHCI_UHS2_INT_UNRECOVER       | \
		SDHCI_UHS2_INT_EBUSY_ERR       | \
		SDHCI_UHS2_INT_ADMA_ERROR      | \
		SDHCI_UHS2_INT_DEADLOCK_TIMEOUT)

#define SDHCI_UHS2_SETTINGS_PTR			0xE0
#define   SDHCI_UHS2_GEN_SETTINGS_POWER_LOW	BIT(0)
#define   SDHCI_UHS2_GEN_SETTINGS_N_LANES_MASK	GENMASK(11, 8)
#define   SDHCI_UHS2_FD_OR_2L_HD		0x0 /* 2 lanes */
#define   SDHCI_UHS2_2D1U_FD			0x2 /* 3 lanes, 2 down, 1 up, full duplex */
#define   SDHCI_UHS2_1D2U_FD			0x3 /* 3 lanes, 1 down, 2 up, full duplex */
#define   SDHCI_UHS2_2D2U_FD			0x4 /* 4 lanes, 2 down, 2 up, full duplex */

#define   SDHCI_UHS2_PHY_SET_SPEED_B		BIT(6)
#define   SDHCI_UHS2_PHY_HIBERNATE_EN		BIT(12)
#define   SDHCI_UHS2_PHY_N_LSS_SYN_MASK		GENMASK(19, 16)
#define   SDHCI_UHS2_PHY_N_LSS_DIR_MASK		GENMASK(23, 20)

#define   SDHCI_UHS2_TRAN_N_FCU_MASK		GENMASK(15, 8)
#define   SDHCI_UHS2_TRAN_RETRY_CNT_MASK	GENMASK(17, 16)
#define   SDHCI_UHS2_TRAN_1_N_DAT_GAP_MASK	GENMASK(7, 0)

#define SDHCI_UHS2_CAPS_PTR			0xE2
#define   SDHCI_UHS2_CAPS_OFFSET		0
#define   SDHCI_UHS2_CAPS_DAP_MASK		GENMASK(3, 0)
#define   SDHCI_UHS2_CAPS_GAP_MASK		GENMASK(7, 4)
#define   SDHCI_UHS2_CAPS_GAP(gap)		((gap) * 360)
#define   SDHCI_UHS2_CAPS_LANE_MASK		GENMASK(13, 8)
#define   SDHCI_UHS2_CAPS_2L_HD_FD		1
#define   SDHCI_UHS2_CAPS_2D1U_FD		2
#define   SDHCI_UHS2_CAPS_1D2U_FD		4
#define   SDHCI_UHS2_CAPS_2D2U_FD		8
#define   SDHCI_UHS2_CAPS_ADDR_64		BIT(14)
#define   SDHCI_UHS2_CAPS_BOOT			BIT(15)
#define   SDHCI_UHS2_CAPS_DEV_TYPE_MASK		GENMASK(17, 16)
#define   SDHCI_UHS2_CAPS_DEV_TYPE_RMV		0
#define   SDHCI_UHS2_CAPS_DEV_TYPE_EMB		1
#define   SDHCI_UHS2_CAPS_DEV_TYPE_EMB_RMV	2
#define   SDHCI_UHS2_CAPS_NUM_DEV_MASK		GENMASK(21, 18)
#define   SDHCI_UHS2_CAPS_BUS_TOPO_MASK		GENMASK(23, 22)
#define   SDHCI_UHS2_CAPS_BUS_TOPO_SHIFT	22
#define   SDHCI_UHS2_CAPS_BUS_TOPO_P2P		0
#define   SDHCI_UHS2_CAPS_BUS_TOPO_RING		1
#define   SDHCI_UHS2_CAPS_BUS_TOPO_HUB		2
#define   SDHCI_UHS2_CAPS_BUS_TOPO_HUB_RING	3

#define  SDHCI_UHS2_CAPS_PHY_OFFSET		4
#define   SDHCI_UHS2_CAPS_PHY_REV_MASK		GENMASK(5, 0)
#define   SDHCI_UHS2_CAPS_PHY_RANGE_MASK	GENMASK(7, 6)
#define   SDHCI_UHS2_CAPS_PHY_RANGE_A		0
#define   SDHCI_UHS2_CAPS_PHY_RANGE_B		1
#define   SDHCI_UHS2_CAPS_PHY_N_LSS_SYN_MASK	GENMASK(19, 16)
#define   SDHCI_UHS2_CAPS_PHY_N_LSS_DIR_MASK	GENMASK(23, 20)
#define  SDHCI_UHS2_CAPS_TRAN_OFFSET		8
#define   SDHCI_UHS2_CAPS_TRAN_LINK_REV_MASK	GENMASK(5, 0)
#define   SDHCI_UHS2_CAPS_TRAN_N_FCU_MASK	GENMASK(15, 8)
#define   SDHCI_UHS2_CAPS_TRAN_HOST_TYPE_MASK	GENMASK(18, 16)
#define   SDHCI_UHS2_CAPS_TRAN_BLK_LEN_MASK	GENMASK(31, 20)

#define  SDHCI_UHS2_CAPS_TRAN_1_OFFSET		12
#define  SDHCI_UHS2_CAPS_TRAN_1_N_DATA_GAP_MASK	GENMASK(7, 0)

#define SDHCI_UHS2_EMBED_CTRL_PTR		0xE6
#define SDHCI_UHS2_VENDOR_PTR			0xE8

struct sdhci_host;
struct mmc_command;
struct mmc_request;

void sdhci_uhs2_dump_regs(struct sdhci_host *host);
void sdhci_uhs2_reset(struct sdhci_host *host, u16 mask);
void sdhci_uhs2_set_power(struct sdhci_host *host, unsigned char mode, unsigned short vdd);
void sdhci_uhs2_set_timeout(struct sdhci_host *host, struct mmc_command *cmd);
int sdhci_uhs2_add_host(struct sdhci_host *host);
void sdhci_uhs2_remove_host(struct sdhci_host *host, int dead);
void sdhci_uhs2_clear_set_irqs(struct sdhci_host *host, u32 clear, u32 set);
u32 sdhci_uhs2_irq(struct sdhci_host *host, u32 intmask);

#endif /* __SDHCI_UHS2_H */
