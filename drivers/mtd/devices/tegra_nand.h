/*
 * drivers/mtd/devices/tegra_nand.h
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Dima Zavin <dima@android.com>
 *         Colin Cross <ccross@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MTD_DEV_TEGRA_NAND_H
#define __MTD_DEV_TEGRA_NAND_H

#include <mach/io.h>

#define __BITMASK0(len)				((1 << (len)) - 1)
#define __BITMASK(start, len)			(__BITMASK0(len) << (start))
#define REG_BIT(bit)				(1 << (bit))
#define REG_FIELD(val, start, len)		(((val) & __BITMASK0(len)) << (start))
#define REG_FIELD_MASK(start, len)		(~(__BITMASK((start), (len))))
#define REG_GET_FIELD(val, start, len)		(((val) >> (start)) & __BITMASK0(len))

/* tegra nand registers... */
#define TEGRA_NAND_PHYS				0x70008000
#define TEGRA_NAND_BASE				IO_TO_VIRT(TEGRA_NAND_PHYS)
#define COMMAND_REG				(TEGRA_NAND_BASE + 0x00)
#define STATUS_REG				(TEGRA_NAND_BASE + 0x04)
#define ISR_REG					(TEGRA_NAND_BASE + 0x08)
#define IER_REG					(TEGRA_NAND_BASE + 0x0c)
#define CONFIG_REG				(TEGRA_NAND_BASE + 0x10)
#define TIMING_REG				(TEGRA_NAND_BASE + 0x14)
#define RESP_REG				(TEGRA_NAND_BASE + 0x18)
#define TIMING2_REG				(TEGRA_NAND_BASE + 0x1c)
#define CMD_REG1				(TEGRA_NAND_BASE + 0x20)
#define CMD_REG2				(TEGRA_NAND_BASE + 0x24)
#define ADDR_REG1				(TEGRA_NAND_BASE + 0x28)
#define ADDR_REG2				(TEGRA_NAND_BASE + 0x2c)
#define DMA_MST_CTRL_REG			(TEGRA_NAND_BASE + 0x30)
#define DMA_CFG_A_REG				(TEGRA_NAND_BASE + 0x34)
#define DMA_CFG_B_REG				(TEGRA_NAND_BASE + 0x38)
#define FIFO_CTRL_REG				(TEGRA_NAND_BASE + 0x3c)
#define DATA_BLOCK_PTR_REG			(TEGRA_NAND_BASE + 0x40)
#define TAG_PTR_REG				(TEGRA_NAND_BASE + 0x44)
#define ECC_PTR_REG				(TEGRA_NAND_BASE + 0x48)
#define DEC_STATUS_REG				(TEGRA_NAND_BASE + 0x4c)
#define HWSTATUS_CMD_REG			(TEGRA_NAND_BASE + 0x50)
#define HWSTATUS_MASK_REG			(TEGRA_NAND_BASE + 0x54)
#define LL_CONFIG_REG				(TEGRA_NAND_BASE + 0x58)
#define LL_PTR_REG				(TEGRA_NAND_BASE + 0x5c)
#define LL_STATUS_REG				(TEGRA_NAND_BASE + 0x60)

/* nand_command bits */
#define COMMAND_GO				REG_BIT(31)
#define COMMAND_CLE				REG_BIT(30)
#define COMMAND_ALE				REG_BIT(29)
#define COMMAND_PIO				REG_BIT(28)
#define COMMAND_TX				REG_BIT(27)
#define COMMAND_RX				REG_BIT(26)
#define COMMAND_SEC_CMD				REG_BIT(25)
#define COMMAND_AFT_DAT				REG_BIT(24)
#define COMMAND_TRANS_SIZE(val)			REG_FIELD((val), 20, 4)
#define COMMAND_A_VALID				REG_BIT(19)
#define COMMAND_B_VALID				REG_BIT(18)
#define COMMAND_RD_STATUS_CHK			REG_BIT(17)
#define COMMAND_RBSY_CHK			REG_BIT(16)
#define COMMAND_CE(val)				REG_BIT(8 + ((val) & 0x7))
#define COMMAND_CLE_BYTE_SIZE(val)		REG_FIELD((val), 4, 2)
#define COMMAND_ALE_BYTE_SIZE(val)		REG_FIELD((val), 0, 4)

/* nand isr bits */
#define ISR_UND					REG_BIT(7)
#define ISR_OVR					REG_BIT(6)
#define ISR_CMD_DONE				REG_BIT(5)
#define ISR_ECC_ERR				REG_BIT(4)

/* nand ier bits */
#define IER_ERR_TRIG_VAL(val)			REG_FIELD((val), 16, 4)
#define IER_UND					REG_BIT(7)
#define IER_OVR					REG_BIT(6)
#define IER_CMD_DONE				REG_BIT(5)
#define IER_ECC_ERR				REG_BIT(4)
#define IER_GIE					REG_BIT(0)

/* nand config bits */
#define CONFIG_HW_ECC				REG_BIT(31)
#define CONFIG_ECC_SEL				REG_BIT(30)
#define CONFIG_HW_ERR_CORRECTION		REG_BIT(29)
#define CONFIG_PIPELINE_EN			REG_BIT(28)
#define CONFIG_ECC_EN_TAG			REG_BIT(27)
#define CONFIG_TVALUE(val)			REG_FIELD((val), 24, 2)
#define CONFIG_SKIP_SPARE			REG_BIT(23)
#define CONFIG_COM_BSY				REG_BIT(22)
#define CONFIG_BUS_WIDTH			REG_BIT(21)
#define CONFIG_PAGE_SIZE_SEL(val)		REG_FIELD((val), 16, 3)
#define CONFIG_SKIP_SPARE_SEL(val)		REG_FIELD((val), 14, 2)
#define CONFIG_TAG_BYTE_SIZE(val)		REG_FIELD((val), 0, 8)

/* nand timing bits */
#define TIMING_TRP_RESP(val)			REG_FIELD((val), 28, 4)
#define TIMING_TWB(val)				REG_FIELD((val), 24, 4)
#define TIMING_TCR_TAR_TRR(val)			REG_FIELD((val), 20, 4)
#define TIMING_TWHR(val)			REG_FIELD((val), 16, 4)
#define TIMING_TCS(val)				REG_FIELD((val), 14, 2)
#define TIMING_TWH(val)				REG_FIELD((val), 12, 2)
#define TIMING_TWP(val)				REG_FIELD((val), 8, 4)
#define TIMING_TRH(val)				REG_FIELD((val), 4, 2)
#define TIMING_TRP(val)				REG_FIELD((val), 0, 4)

/* nand timing2 bits */
#define TIMING2_TADL(val)			REG_FIELD((val), 0, 4)

/* nand dma_mst_ctrl bits */
#define DMA_CTRL_DMA_GO				REG_BIT(31)
#define DMA_CTRL_DIR				REG_BIT(30)
#define DMA_CTRL_DMA_PERF_EN			REG_BIT(29)
#define DMA_CTRL_IE_DMA_DONE			REG_BIT(28)
#define DMA_CTRL_REUSE_BUFFER			REG_BIT(27)
#define DMA_CTRL_BURST_SIZE(val)		REG_FIELD((val), 24, 3)
#define DMA_CTRL_IS_DMA_DONE			REG_BIT(20)
#define DMA_CTRL_DMA_EN_A			REG_BIT(2)
#define DMA_CTRL_DMA_EN_B			REG_BIT(1)

/* nand dma_cfg_a/cfg_b bits */
#define DMA_CFG_BLOCK_SIZE(val)			REG_FIELD((val), 0, 16)

/* nand dec_status bits */
#define DEC_STATUS_ERR_PAGE_NUM(val)		REG_GET_FIELD((val), 24, 8)
#define DEC_STATUS_ERR_CNT(val)			REG_GET_FIELD((val), 16, 8)
#define DEC_STATUS_ECC_FAIL_A			REG_BIT(1)
#define DEC_STATUS_ECC_FAIL_B			REG_BIT(0)

/* nand hwstatus_mask bits */
#define HWSTATUS_RDSTATUS_MASK(val)		REG_FIELD((val), 24, 8)
#define HWSTATUS_RDSTATUS_EXP_VAL(val)		REG_FIELD((val), 16, 8)
#define HWSTATUS_RBSY_MASK(val)			REG_FIELD((val), 8, 8)
#define HWSTATUS_RBSY_EXP_VAL(val)		REG_FIELD((val), 0, 8)

#endif

