// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2020, MIPI Alliance, Inc.
 *
 * Author: Nicolas Pitre <npitre@baylibre.com>
 *
 * Core driver code with main interface to the I3C subsystem.
 */

#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/i3c/master.h>
#include <linux/i3c/target.h>
#include <linux/i3c/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "hci.h"
#include "ext_caps.h"
#include "cmd.h"
#include "dat.h"


/*
 * Host Controller Capabilities and Operation Registers
 */

#define reg_read(r)		readl(hci->base_regs + (r))
#define reg_write(r, v)		writel(v, hci->base_regs + (r))
#define reg_set(r, v)		reg_write(r, reg_read(r) | (v))
#define reg_clear(r, v)		reg_write(r, reg_read(r) & ~(v))

#define HCI_VERSION			0x00	/* HCI Version (in BCD) */

#define HC_CONTROL			0x04
#define HC_CONTROL_BUS_ENABLE		BIT(31)
#define HC_CONTROL_RESUME		BIT(30)
#define HC_CONTROL_ABORT		BIT(29)
#define HC_CONTROL_HALT_ON_CMD_TIMEOUT	BIT(12)
#define HC_CONTROL_HOT_JOIN_CTRL	BIT(8)	/* Hot-Join ACK/NACK Control */
#define HC_CONTROL_I2C_TARGET_PRESENT	BIT(7)
#define HC_CONTROL_PIO_MODE		BIT(6)	/* DMA/PIO Mode Selector */
#define HC_CONTROL_DATA_BIG_ENDIAN	BIT(4)
#define HC_CONTROL_IBA_INCLUDE		BIT(0)	/* Include I3C Broadcast Address */

#define MASTER_DEVICE_ADDR		0x08	/* Master Device Address */
#define MASTER_DYNAMIC_ADDR_VALID	BIT(31)	/* Dynamic Address is Valid */
#define MASTER_DYNAMIC_ADDR(v)		FIELD_PREP(GENMASK(22, 16), v)

#define HC_CAPABILITIES			0x0c
#define HC_CAP_SG_DC_EN			BIT(30)
#define HC_CAP_SG_IBI_EN		BIT(29)
#define HC_CAP_SG_CR_EN			BIT(28)
#define HC_CAP_MAX_DATA_LENGTH		GENMASK(24, 22)
#define HC_CAP_CMD_SIZE			GENMASK(21, 20)
#define HC_CAP_DIRECT_COMMANDS_EN	BIT(18)
#define HC_CAP_MULTI_LANE_EN		BIT(15)
#define HC_CAP_CMD_CCC_DEFBYTE		BIT(10)
#define HC_CAP_HDR_BT_EN		BIT(8)
#define HC_CAP_HDR_TS_EN		BIT(7)
#define HC_CAP_HDR_DDR_EN		BIT(6)
#define HC_CAP_NON_CURRENT_MASTER_CAP	BIT(5)	/* master handoff capable */
#define HC_CAP_DATA_BYTE_CFG_EN		BIT(4)	/* endian selection possible */
#define HC_CAP_AUTO_COMMAND		BIT(3)
#define HC_CAP_COMBO_COMMAND		BIT(2)

#define RESET_CONTROL			0x10
#define BUS_RESET			BIT(31)
#define BUS_RESET_TYPE			GENMASK(30, 29)
#define IBI_QUEUE_RST			BIT(5)
#define RX_FIFO_RST			BIT(4)
#define TX_FIFO_RST			BIT(3)
#define RESP_QUEUE_RST			BIT(2)
#define CMD_QUEUE_RST			BIT(1)
#define SOFT_RST			BIT(0)	/* Core Reset */

#define PRESENT_STATE			0x14
#define STATE_CURRENT_MASTER		BIT(2)

#define INTR_STATUS			0x20
#define INTR_STATUS_ENABLE		0x24
#define INTR_SIGNAL_ENABLE		0x28
#define INTR_FORCE			0x2c
#define INTR_HC_CMD_SEQ_UFLOW_STAT	BIT(12)	/* Cmd Sequence Underflow */
#define INTR_HC_RESET_CANCEL		BIT(11)	/* HC Cancelled Reset */
#define INTR_HC_INTERNAL_ERR		BIT(10)	/* HC Internal Error */
#define INTR_HC_PIO			BIT(8)	/* cascaded PIO interrupt */
#define INTR_HC_RINGS			GENMASK(7, 0)

#define DAT_SECTION			0x30	/* Device Address Table */
#define DAT_ENTRY_SIZE			GENMASK(31, 28)
#define DAT_TABLE_SIZE			GENMASK(18, 12)
#define DAT_TABLE_OFFSET		GENMASK(11, 0)

#define DCT_SECTION			0x34	/* Device Characteristics Table */
#define DCT_ENTRY_SIZE			GENMASK(31, 28)
#define DCT_TABLE_INDEX			GENMASK(23, 19)
#define DCT_TABLE_SIZE			GENMASK(18, 12)
#define DCT_TABLE_OFFSET		GENMASK(11, 0)

#define RING_HEADERS_SECTION		0x38
#define RING_HEADERS_OFFSET		GENMASK(15, 0)

#define PIO_SECTION			0x3c
#define PIO_REGS_OFFSET			GENMASK(15, 0)	/* PIO Offset */

#define EXT_CAPS_SECTION		0x40
#define EXT_CAPS_OFFSET			GENMASK(15, 0)
/* Aspeed in-house register */
#define ASPEED_I3C_CTRL			0x0
#define ASPEED_I3C_CTRL_STOP_QUEUE_PT	BIT(31) //Stop the queue read pointer.
#define ASPEED_I3C_CTRL_INIT		BIT(4)
#define ASPEED_I3C_CTRL_INIT_MODE	GENMASK(1, 0)
#define INIT_MST_MODE 0
#define INIT_SEC_MST_MODE 1
#define INIT_SLV_MODE 2

#define ASPEED_I3C_STS	0x4
#define ASPEED_I3C_STS_SLV_DYNAMIC_ADDRESS_VALID	BIT(23)
#define ASPEED_I3C_STS_SLV_DYNAMIC_ADDRESS		GENMASK(22, 16)
#define ASPEED_I3C_STS_MODE_PURE_SLV			BIT(8)
#define ASPEED_I3C_STS_MODE_SECONDARY_SLV_TO_MST	BIT(7)
#define ASPEED_I3C_STS_MODE_SECONDARY_MST_TO_SLV	BIT(6)
#define ASPEED_I3C_STS_MODE_SECONDARY_SLV		BIT(5)
#define ASPEED_I3C_STS_MODE_SECONDARY_MST		BIT(4)
#define ASPEED_I3C_STS_MODE_PRIMARY_SLV_TO_MST		BIT(3)
#define ASPEED_I3C_STS_MODE_PRIMARY_MST_TO_SLV		BIT(2)
#define ASPEED_I3C_STS_MODE_PRIMARY_SLV			BIT(1)
#define ASPEED_I3C_STS_MODE_PRIMARY_MST			BIT(0)

#define ASPEED_I3C_DAA_INDEX0	0x10
#define ASPEED_I3C_DAA_INDEX1	0x14
#define ASPEED_I3C_DAA_INDEX2	0x18
#define ASPEED_I3C_DAA_INDEX3	0x1C

#define ASPEED_I3C_AUTOCMD_0	0x20
#define ASPEED_I3C_AUTOCMD_1	0x24
#define ASPEED_I3C_AUTOCMD_2	0x28
#define ASPEED_I3C_AUTOCMD_3	0x2C
#define ASPEED_I3C_AUTOCMD_4	0x30
#define ASPEED_I3C_AUTOCMD_5	0x34
#define ASPEED_I3C_AUTOCMD_6	0x38
#define ASPEED_I3C_AUTOCMD_7	0x3C

#define ASPEED_I3C_AUTOCMD_SEL_0_7	0x40
#define ASPEED_I3C_AUTOCMD_SEL_8_15	0x44
#define ASPEED_I3C_AUTOCMD_SEL_16_23	0x48
#define ASPEED_I3C_AUTOCMD_SEL_24_31	0x4C
#define ASPEED_I3C_AUTOCMD_SEL_32_39	0x50
#define ASPEED_I3C_AUTOCMD_SEL_40_47	0x54
#define ASPEED_I3C_AUTOCMD_SEL_48_55	0x58
#define ASPEED_I3C_AUTOCMD_SEL_56_63	0x5C
#define ASPEED_I3C_AUTOCMD_SEL_64_71	0x60
#define ASPEED_I3C_AUTOCMD_SEL_72_79	0x64
#define ASPEED_I3C_AUTOCMD_SEL_80_87	0x68
#define ASPEED_I3C_AUTOCMD_SEL_88_95	0x6C
#define ASPEED_I3C_AUTOCMD_SEL_96_103	0x70
#define ASPEED_I3C_AUTOCMD_SEL_104_111	0x74
#define ASPEED_I3C_AUTOCMD_SEL_112_119	0x78
#define ASPEED_I3C_AUTOCMD_SEL_120_127	0x7C

#define ASPEED_I3C_SLV_CHAR_CTRL	0xA0
#define ASPEED_I3C_SLV_CHAR_CTRL_DCR	GENMASK(23, 16)
#define ASPEED_I3C_SLV_CHAR_CTRL_BCR	GENMASK(15, 8)
#define     SLV_BCR_DEVICE_ROLE		GENMASK(7, 6)
#define ASPEED_I3C_SLV_CHAR_CTRL_STATIC_ADDR_EN	BIT(7)
#define ASPEED_I3C_SLV_CHAR_CTRL_STATIC_ADDR	GENMASK(6, 0)
#define SLV_PID_HI(x)			(((x) >> 32) & GENMASK(15, 0))
#define SLV_PID_LO(x)			((x) & GENMASK(31, 0))
#define ASPEED_I3C_SLV_PID_LO	0xA4
#define ASPEED_I3C_SLV_PID_HI	0xA8
#define ASPEED_I3C_SLV_FSM	0xAC
#define ASPEED_I3C_SLV_CAP_CTRL	0xB0
#define ASPEED_I3C_SLV_CAP_CTRL_PEC_EN		BIT(31)
#define ASPEED_I3C_SLV_CAP_CTRL_HAIT_IF_IBI_ERR	BIT(30)
#define ASPEED_I3C_SLV_CAP_CTRL_ACCEPT_CR	BIT(16)
#define ASPEED_I3C_SLV_CAP_CTRL_HJ_REQ		BIT(10)
#define ASPEED_I3C_SLV_CAP_CTRL_MR_REQ		BIT(9)
#define ASPEED_I3C_SLV_CAP_CTRL_IBI_REQ		BIT(8)
#define ASPEED_I3C_SLV_CAP_CTRL_HJ_WAIT		BIT(6)
#define ASPEED_I3C_SLV_CAP_CTRL_MR_WAIT		BIT(5)
#define ASPEED_I3C_SLV_CAP_CTRL_IBI_WAIT	BIT(4)
#define ASPEED_I3C_SLV_CAP_CTRL_NOTSUP_DEF_BYTE	BIT(1)
#define ASPEED_I3C_SLV_CAP_CTRL_I2C_DEV		BIT(0)
/* CCC related registers */
#define ASPEED_I3C_SLV_STS1			0xB4
#define ASPEED_I3C_SLV_STS1_IBI_PAYLOAD_SIZE	GENMASK(31, 24)
#define ASPEED_I3C_SLV_STS1_RSTACT		GENMASK(22, 16)
/* the parameters for the HDR-DDR Data Transfer Early Termination procedure*/
#define ASPEED_I3C_SLV_STS1_ETP_ACK_CAP		BIT(15)
#define ASPEED_I3C_SLV_STS1_ETP_W_REQ		BIT(14)
#define ASPEED_I3C_SLV_STS1_ETP_CRC		GENMASK(13, 12)
#define ASPEED_I3C_SLV_STS1_ENDXFER_CONFIRM	BIT(11)
#define ASPEED_I3C_SLV_STS1_ENTER_TEST_MDOE	BIT(8)
#define ASPEED_I3C_SLV_STS1_HJ_EN		BIT(6)
#define ASPEED_I3C_SLV_STS1_CR_EN		BIT(5)
#define ASPEED_I3C_SLV_STS1_IBI_EN		BIT(4)
#define ASPEED_I3C_SLV_STS1_HJ_DONE		BIT(2)
#define ASPEED_I3C_SLV_STS1_CR_DONE		BIT(1)
#define ASPEED_I3C_SLV_STS1_IBI_DONE		BIT(0)
#define ASPEED_I3C_SLV_STS2			0xB8
#define ASPEED_I3C_SLV_STS2_MWL			GENMASK(31, 16)
#define ASPEED_I3C_SLV_STS2_MRL			GENMASK(15, 0)
#define ASPEED_I3C_SLV_STS3_GROUP_ADDR		0xBC
#define ASPEED_I3C_SLV_STS3_GROUP3_VALID	BIT(31)
#define ASPEED_I3C_SLV_STS3_GROUP3_ADDR		GENMASK(30, 24)
#define ASPEED_I3C_SLV_STS3_GROUP2_VALID	BIT(23)
#define ASPEED_I3C_SLV_STS3_GROUP2_ADDR		GENMASK(22, 16)
#define ASPEED_I3C_SLV_STS3_GROUP1_VALID	BIT(15)
#define ASPEED_I3C_SLV_STS3_GROUP1_ADDR		GENMASK(14, 8)
#define ASPEED_I3C_SLV_STS3_GROUP0_VALID	BIT(7)
#define ASPEED_I3C_SLV_STS3_GROUP0_ADDR		GENMASK(6, 0)
#define ASPEED_I3C_SLV_STS4_RSTACT_TIME		0xC0

#define ASPEED_I3C_INTR_STATUS		0xE0
#define ASPEED_I3C_INTR_STATUS_ENABLE	0xE4
#define ASPEED_I3C_INTR_SIGNAL_ENABLE	0xE8
#define ASPEED_I3C_INTR_FORCE		0xEC
#define ASPEED_I3C_INTR_I2C_SDA_STUCK_LOW	BIT(14)
#define ASPEED_I3C_INTR_I3C_SDA_STUCK_HIGH	BIT(13)
#define ASPEED_I3C_INTR_I3C_SDA_STUCK_LOW	BIT(12)
#define ASPEED_I3C_INTR_MST_INTERNAL_DONE	BIT(10)
#define ASPEED_I3C_INTR_MST_DDR_READ_DONE	BIT(9)
#define ASPEED_I3C_INTR_MST_DDR_WRITE_DONE	BIT(8)
#define ASPEED_I3C_INTR_MST_IBI_DONE		BIT(7)
#define ASPEED_I3C_INTR_MST_READ_DONE		BIT(6)
#define ASPEED_I3C_INTR_MST_WRITE_DONE		BIT(5)
#define ASPEED_I3C_INTR_MST_DAA_DONE		BIT(4)
#define ASPEED_I3C_INTR_SLV_SCL_STUCK		BIT(1)
#define ASPEED_I3C_INTR_TGRST			BIT(0)

#define ASPEED_I3C_INTR_SUM_STATUS	0xF0
#define ASPEED_INTR_SUM_INHOUSE		BIT(3)
#define ASPEED_INTR_SUM_RHS		BIT(2)
#define ASPEED_INTR_SUM_PIO		BIT(1)
#define ASPEED_INTR_SUM_CAP		BIT(0)

#define ASPEED_I3C_INTR_RENEW		0xF4

#define ast_inhouse_read(r)		readl(hci->EXTCAPS_regs + (r))
#define ast_inhouse_write(r, v)		writel(v, hci->EXTCAPS_regs + (r))

#define ASPEED_PHY_REGS_OFFSET		0xE00
#define ast_phy_read(r)			readl(hci->PHY_regs + (r))
#define ast_phy_write(r, v)		writel(v, hci->PHY_regs + (r))

/* I2C FM */
#define PHY_I2C_FM_CTRL0		0x8
#define PHY_I2C_FM_CTRL0_CAS		GENMASK(25, 16)
#define PHY_I2C_FM_CTRL0_SU_STO		GENMASK(9, 0)
#define PHY_I2C_FM_CTRL1		0xC
#define PHY_I2C_FM_CTRL1_SCL_H		GENMASK(25, 16)
#define PHY_I2C_FM_CTRL1_SCL_L		GENMASK(9, 0)
#define PHY_I2C_FM_CTRL2		0x10
#define PHY_I2C_FM_CTRL2_ACK_H		GENMASK(25, 16)
#define PHY_I2C_FM_CTRL2_ACK_L		GENMASK(9, 0)
#define PHY_I2C_FM_CTRL3		0x14
#define PHY_I2C_FM_CTRL3_HD_DAT		GENMASK(25, 16)
#define PHY_I2C_FM_CTRL3_AHD_DAT	GENMASK(9, 0)

#define PHY_I2C_FM_DEFAULT_CAS_NS	1130
#define PHY_I2C_FM_DEFAULT_SU_STO_NS	1370
#define PHY_I2C_FM_DEFAULT_SCL_H_NS	1130
#define PHY_I2C_FM_DEFAULT_SCL_L_NS	1370
#define PHY_I2C_FM_DEFAULT_HD_DAT	10
#define PHY_I2C_FM_DEFAULT_AHD_DAT	10

/* I2C FMP */
#define PHY_I2C_FMP_CTRL0		0x18
#define PHY_I2C_FMP_CTRL0_CAS		GENMASK(25, 16)
#define PHY_I2C_FMP_CTRL0_SU_STO	GENMASK(9, 0)
#define PHY_I2C_FMP_CTRL1		0x1C
#define PHY_I2C_FMP_CTRL1_SCL_H		GENMASK(25, 16)
#define PHY_I2C_FMP_CTRL1_SCL_L		GENMASK(9, 0)
#define PHY_I2C_FMP_CTRL2		0x20
#define PHY_I2C_FMP_CTRL2_ACK_H		GENMASK(25, 16)
#define PHY_I2C_FMP_CTRL2_ACK_L		GENMASK(9, 0)
#define PHY_I2C_FMP_CTRL3		0x24
#define PHY_I2C_FMP_CTRL3_HD_DAT	GENMASK(25, 16)
#define PHY_I2C_FMP_CTRL3_AHD_DAT	GENMASK(9, 0)

#define PHY_I2C_FMP_DEFAULT_CAS_NS	380
#define PHY_I2C_FMP_DEFAULT_SU_STO_NS	620
#define PHY_I2C_FMP_DEFAULT_SCL_H_NS	380
#define PHY_I2C_FMP_DEFAULT_SCL_L_NS	620
#define PHY_I2C_FMP_DEFAULT_HD_DAT	10
#define PHY_I2C_FMP_DEFAULT_AHD_DAT	10

/* I3C OD */
#define PHY_I3C_OD_CTRL0		0x28
#define PHY_I3C_OD_CTRL0_CAS		GENMASK(25, 16)
#define PHY_I3C_OD_CTRL0_SU_STO		GENMASK(9, 0)
#define PHY_I3C_OD_CTRL1		0x2C
#define PHY_I3C_OD_CTRL1_SCL_H		GENMASK(25, 16)
#define PHY_I3C_OD_CTRL1_SCL_L		GENMASK(9, 0)
#define PHY_I3C_OD_CTRL2		0x30
#define PHY_I3C_OD_CTRL2_ACK_H		GENMASK(25, 16)
#define PHY_I3C_OD_CTRL2_ACK_L		GENMASK(9, 0)
#define PHY_I3C_OD_CTRL3		0x34
#define PHY_I3C_OD_CTRL3_HD_DAT		GENMASK(25, 16)
#define PHY_I3C_OD_CTRL3_AHD_DAT	GENMASK(9, 0)

#define PHY_I3C_OD_DEFAULT_CAS_NS	40
#define PHY_I3C_OD_DEFAULT_SU_STO_NS	40
#define PHY_I3C_OD_DEFAULT_SCL_H_NS	380
#define PHY_I3C_OD_DEFAULT_SCL_L_NS	620
#define PHY_I3C_OD_DEFAULT_HD_DAT	10
#define PHY_I3C_OD_DEFAULT_AHD_DAT	10

/* I3C PP SDR0 */
#define PHY_I3C_SDR0_CTRL0			0x38
#define PHY_I3C_SDR0_CTRL0_SCL_H		GENMASK(25, 16)
#define PHY_I3C_SDR0_CTRL0_SCL_L		GENMASK(9, 0)
#define PHY_I3C_SDR0_CTRL1			0x3C
#define PHY_I3C_SDR0_CTRL1_TBIT_H		GENMASK(25, 16)
#define PHY_I3C_SDR0_CTRL1_TBIT_L		GENMASK(9, 0)
#define PHY_I3C_SDR0_CTRL2			0x40
#define PHY_I3C_SDR0_CTRL2_HD_PP		GENMASK(25, 16)
#define PHY_I3C_SDR0_CTRL2_TBIT_HD_PP		GENMASK(9, 0)

/* 1MHz */
#define PHY_I3C_SDR0_DEFAULT_SCL_H_NS		380
#define PHY_I3C_SDR0_DEFAULT_SCL_L_NS		620
#define PHY_I3C_SDR0_DEFAULT_TBIT_H_NS		380
#define PHY_I3C_SDR0_DEFAULT_TBIT_L_NS		620
#define PHY_I3C_SDR0_DEFAULT_HD_PP_NS		10
#define PHY_I3C_SDR0_DEFAULT_TBIT_HD_PP_NS	10

/* I3C PP SDR1 */
#define PHY_I3C_SDR1_CTRL0			0x44
#define PHY_I3C_SDR1_CTRL0_SCL_H		GENMASK(25, 16)
#define PHY_I3C_SDR1_CTRL0_SCL_L		GENMASK(9, 0)
#define PHY_I3C_SDR1_CTRL1			0x48
#define PHY_I3C_SDR1_CTRL1_TBIT_H		GENMASK(25, 16)
#define PHY_I3C_SDR1_CTRL1_TBIT_L		GENMASK(9, 0)
#define PHY_I3C_SDR1_CTRL2			0x4C
#define PHY_I3C_SDR1_CTRL2_HD_PP		GENMASK(25, 16)
#define PHY_I3C_SDR1_CTRL2_TBIT_HD_PP		GENMASK(9, 0)
/* I3C PP SDR2 */
#define PHY_I3C_SDR2_CTRL0			0x50
#define PHY_I3C_SDR2_CTRL0_SCL_H		GENMASK(25, 16)
#define PHY_I3C_SDR2_CTRL0_SCL_L		GENMASK(9, 0)
#define PHY_I3C_SDR2_CTRL1			0x54
#define PHY_I3C_SDR2_CTRL1_TBIT_H		GENMASK(25, 16)
#define PHY_I3C_SDR2_CTRL1_TBIT_L		GENMASK(9, 0)
#define PHY_I3C_SDR2_CTRL2			0x58
#define PHY_I3C_SDR2_CTRL2_HD_PP		GENMASK(25, 16)
#define PHY_I3C_SDR2_CTRL2_TBIT_HD_PP		GENMASK(9, 0)
/* I3C PP SDR3 */
#define PHY_I3C_SDR3_CTRL0			0x5C
#define PHY_I3C_SDR3_CTRL0_SCL_H		GENMASK(25, 16)
#define PHY_I3C_SDR3_CTRL0_SCL_L		GENMASK(9, 0)
#define PHY_I3C_SDR3_CTRL1			0x60
#define PHY_I3C_SDR3_CTRL1_TBIT_H		GENMASK(25, 16)
#define PHY_I3C_SDR3_CTRL1_TBIT_L		GENMASK(9, 0)
#define PHY_I3C_SDR3_CTRL2			0x64
#define PHY_I3C_SDR3_CTRL2_HD_PP		GENMASK(25, 16)
#define PHY_I3C_SDR3_CTRL2_TBIT_HD_PP		GENMASK(9, 0)
/* I3C PP SDR4 */
#define PHY_I3C_SDR5_CTRL0			0x68
#define PHY_I3C_SDR5_CTRL0_SCL_H		GENMASK(25, 16)
#define PHY_I3C_SDR5_CTRL0_SCL_L		GENMASK(9, 0)
#define PHY_I3C_SDR5_CTRL1			0x6C
#define PHY_I3C_SDR5_CTRL1_TBIT_H		GENMASK(25, 16)
#define PHY_I3C_SDR5_CTRL1_TBIT_L		GENMASK(9, 0)
#define PHY_I3C_SDR5_CTRL2			0x70
#define PHY_I3C_SDR5_CTRL2_HD_PP		GENMASK(25, 16)
#define PHY_I3C_SDR5_CTRL2_TBIT_HD_PP		GENMASK(9, 0)
/* I3C PP DDR */
#define PHY_I3C_DDR_CTRL0			0x74
#define PHY_I3C_DDR_CTRL0_SCL_H			GENMASK(25, 16)
#define PHY_I3C_DDR_CTRL0_SCL_L			GENMASK(9, 0)
#define PHY_I3C_DDR_CTRL1			0x78
#define PHY_I3C_DDR_CTRL1_TBIT_H		GENMASK(25, 16)
#define PHY_I3C_DDR_CTRL1_TBIT_L		GENMASK(9, 0)
#define PHY_I3C_DDR_CTRL2			0x7C
#define PHY_I3C_DDR_CTRL2_HD_PP			GENMASK(25, 16)
#define PHY_I3C_DDR_CTRL2_TBIT_HD_PP		GENMASK(9, 0)

#define PHY_I3C_SR_P_PREPARE_CTRL		0x80
#define PHY_I3C_SR_P_PREPARE_CTRL_HD		GENMASK(25, 16)
#define PHY_I3C_SR_P_PREPARE_CTRL_SCL_L		GENMASK(9, 0)
#define PHY_I3C_SR_P_DEFAULT_HD_NS	10
#define PHY_I3C_SR_P_DEFAULT_SCL_L_NS	40

#define PHY_PULLUP_EN		0x98
#define PHY_PULLUP_EN_SCL	GENMASK(14, 12)
#define PHY_PULLUP_EN_SDA	GENMASK(10, 8)
#define PHY_PULLUP_EN_DDR_SCL	GENMASK(6, 4)
#define PHY_PULLUP_EN_DDR_SDA	GENMASK(2, 0)

#define IBI_NOTIFY_CTRL			0x58	/* IBI Notify Control */
#define IBI_NOTIFY_SIR_REJECTED		BIT(3)	/* Rejected Target Interrupt Request */
#define IBI_NOTIFY_MR_REJECTED		BIT(1)	/* Rejected Master Request Control */
#define IBI_NOTIFY_HJ_REJECTED		BIT(0)	/* Rejected Hot-Join Control */

#define DEV_CTX_BASE_LO			0x60
#define DEV_CTX_BASE_HI			0x64

static inline struct i3c_hci *to_i3c_hci(struct i3c_master_controller *m)
{
	return container_of(m, struct i3c_hci, master);
}

static int i3c_hci_bus_init(struct i3c_master_controller *m)
{
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_device_info info;
	int ret;

	DBG("");
	dev_info(&hci->master.dev, "Master Mode");

#ifdef CONFIG_ARCH_ASPEED
	ast_inhouse_write(ASPEED_I3C_CTRL,
			  ASPEED_I3C_CTRL_INIT |
				  FIELD_PREP(ASPEED_I3C_CTRL_INIT_MODE,
					     INIT_MST_MODE));
#endif

	if (hci->cmd == &mipi_i3c_hci_cmd_v1) {
		ret = mipi_i3c_hci_dat_v1.init(hci);
		if (ret)
			return ret;
	}

	ret = i3c_master_get_free_addr(m, 0);
	if (ret < 0)
		return ret;
	reg_write(MASTER_DEVICE_ADDR,
		  MASTER_DYNAMIC_ADDR(ret) | MASTER_DYNAMIC_ADDR_VALID);
	memset(&info, 0, sizeof(info));
	info.dyn_addr = ret;
	ret = i3c_master_set_info(m, &info);
	if (ret)
		return ret;

	ret = hci->io->init(hci);
	if (ret)
		return ret;

	reg_set(HC_CONTROL, HC_CONTROL_BUS_ENABLE);
	DBG("HC_CONTROL = %#x", reg_read(HC_CONTROL));

	return 0;
}

static void i3c_hci_bus_cleanup(struct i3c_master_controller *m)
{
	struct i3c_hci *hci = to_i3c_hci(m);

	DBG("");

	reg_clear(HC_CONTROL, HC_CONTROL_BUS_ENABLE);
	hci->io->cleanup(hci);
	if (hci->cmd == &mipi_i3c_hci_cmd_v1)
		mipi_i3c_hci_dat_v1.cleanup(hci);
}

void mipi_i3c_hci_resume(struct i3c_hci *hci)
{
	/* the HC_CONTROL_RESUME bit is R/W1C so just read and write back */
	reg_write(HC_CONTROL, reg_read(HC_CONTROL));
}

/* located here rather than pio.c because needed bits are in core reg space */
void mipi_i3c_hci_pio_reset(struct i3c_hci *hci)
{
	reg_write(RESET_CONTROL, RX_FIFO_RST | TX_FIFO_RST | RESP_QUEUE_RST);
}

/* located here rather than dct.c because needed bits are in core reg space */
void mipi_i3c_hci_dct_index_reset(struct i3c_hci *hci)
{
	reg_write(DCT_SECTION, FIELD_PREP(DCT_TABLE_INDEX, 0));
}

static int i3c_hci_send_ccc_cmd(struct i3c_master_controller *m,
				struct i3c_ccc_cmd *ccc)
{
	struct i3c_hci *hci = to_i3c_hci(m);
	struct hci_xfer *xfer;
	bool raw = !!(hci->quirks & HCI_QUIRK_RAW_CCC);
	bool prefixed = raw && !!(ccc->id & I3C_CCC_DIRECT);
	unsigned int nxfers = ccc->ndests + prefixed;
	DECLARE_COMPLETION_ONSTACK(done);
	int i, last, ret = 0;

	DBG("cmd=%#x rnw=%d ndests=%d data[0].len=%d",
	    ccc->id, ccc->rnw, ccc->ndests, ccc->dests[0].payload.len);

	xfer = hci_alloc_xfer(nxfers);
	if (!xfer)
		return -ENOMEM;

	if (prefixed) {
		xfer->data = NULL;
		xfer->data_len = 0;
		xfer->rnw = false;
		hci->cmd->prep_ccc(hci, xfer, I3C_BROADCAST_ADDR,
				   ccc->id, true);
		xfer++;
	}

	for (i = 0; i < nxfers - prefixed; i++) {
		xfer[i].data = ccc->dests[i].payload.data;
		xfer[i].data_len = ccc->dests[i].payload.len;
		xfer[i].rnw = ccc->rnw;
		ret = hci->cmd->prep_ccc(hci, &xfer[i], ccc->dests[i].addr,
					 ccc->id, raw);
		if (ret)
			goto out;
		xfer[i].cmd_desc[0] |= CMD_0_ROC;
	}
	last = i - 1;
	xfer[last].cmd_desc[0] |= CMD_0_TOC;
	xfer[last].completion = &done;

	if (prefixed)
		xfer--;

	ret = hci->io->queue_xfer(hci, xfer, nxfers);
	if (ret)
		goto out;
	if (!wait_for_completion_timeout(&done, HZ) &&
	    hci->io->dequeue_xfer(hci, xfer, nxfers)) {
		ret = -ETIME;
		goto out;
	}
	for (i = prefixed; i < nxfers; i++) {
		if (ccc->rnw)
			ccc->dests[i - prefixed].payload.len =
				RESP_DATA_LENGTH(xfer[i].response);
		if (RESP_STATUS(xfer[i].response) != RESP_SUCCESS) {
			DBG("resp status = %lx", RESP_STATUS(xfer[i].response));
			if (RESP_STATUS(xfer[i].response) ==
			    RESP_ERR_ADDR_HEADER)
				ret = I3C_ERROR_M2;
			else
				ret = -EIO;
			goto out;
		}
	}

	if (ccc->rnw)
		DBG("got: %*ph",
		    ccc->dests[0].payload.len, ccc->dests[0].payload.data);

out:
	hci_free_xfer(xfer, nxfers);
	return ret;
}

static int i3c_hci_daa(struct i3c_master_controller *m)
{
	struct i3c_hci *hci = to_i3c_hci(m);

	DBG("");

	return hci->cmd->perform_daa(hci);
}

static int i3c_hci_priv_xfers(struct i3c_dev_desc *dev,
			      struct i3c_priv_xfer *i3c_xfers,
			      int nxfers)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct hci_xfer *xfer;
	DECLARE_COMPLETION_ONSTACK(done);
	unsigned int size_limit;
	int i, last, ret = 0;

	DBG("nxfers = %d", nxfers);

	xfer = hci_alloc_xfer(nxfers);
	if (!xfer)
		return -ENOMEM;

	size_limit = 1U << (16 + FIELD_GET(HC_CAP_MAX_DATA_LENGTH, hci->caps));

	for (i = 0; i < nxfers; i++) {
		xfer[i].data_len = i3c_xfers[i].len;
		ret = -EFBIG;
		if (xfer[i].data_len >= size_limit)
			goto out;
		xfer[i].rnw = i3c_xfers[i].rnw;
		if (i3c_xfers[i].rnw) {
			xfer[i].data = i3c_xfers[i].data.in;
		} else {
			/* silence the const qualifier warning with a cast */
			xfer[i].data = (void *) i3c_xfers[i].data.out;
		}
		hci->cmd->prep_i3c_xfer(hci, dev, &xfer[i]);
		xfer[i].cmd_desc[0] |= CMD_0_ROC;
	}
	last = i - 1;
	xfer[last].cmd_desc[0] |= CMD_0_TOC;
	xfer[last].completion = &done;

	ret = hci->io->queue_xfer(hci, xfer, nxfers);
	if (ret)
		goto out;
	if (!wait_for_completion_timeout(&done, HZ) &&
	    hci->io->dequeue_xfer(hci, xfer, nxfers)) {
		ret = -ETIME;
		goto out;
	}
	for (i = 0; i < nxfers; i++) {
		if (i3c_xfers[i].rnw)
			i3c_xfers[i].len = RESP_DATA_LENGTH(xfer[i].response);
		if (RESP_STATUS(xfer[i].response) != RESP_SUCCESS) {
			ret = -EIO;
			goto out;
		}
	}

out:
	hci_free_xfer(xfer, nxfers);
	return ret;
}

static int i3c_hci_i2c_xfers(struct i2c_dev_desc *dev,
			     const struct i2c_msg *i2c_xfers, int nxfers)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct hci_xfer *xfer;
	DECLARE_COMPLETION_ONSTACK(done);
	int i, last, ret = 0;

	DBG("nxfers = %d", nxfers);

	xfer = hci_alloc_xfer(nxfers);
	if (!xfer)
		return -ENOMEM;

	for (i = 0; i < nxfers; i++) {
		xfer[i].data = i2c_xfers[i].buf;
		xfer[i].data_len = i2c_xfers[i].len;
		xfer[i].rnw = i2c_xfers[i].flags & I2C_M_RD;
		hci->cmd->prep_i2c_xfer(hci, dev, &xfer[i]);
		xfer[i].cmd_desc[0] |= CMD_0_ROC;
	}
	last = i - 1;
	xfer[last].cmd_desc[0] |= CMD_0_TOC;
	xfer[last].completion = &done;

	ret = hci->io->queue_xfer(hci, xfer, nxfers);
	if (ret)
		goto out;
	if (!wait_for_completion_timeout(&done, HZ) &&
	    hci->io->dequeue_xfer(hci, xfer, nxfers)) {
		ret = -ETIME;
		goto out;
	}
	for (i = 0; i < nxfers; i++) {
		if (RESP_STATUS(xfer[i].response) != RESP_SUCCESS) {
			ret = -EIO;
			goto out;
		}
	}

out:
	hci_free_xfer(xfer, nxfers);
	return ret;
}

static int i3c_hci_attach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data;
	int ret;

	DBG("");

	dev_data = kzalloc(sizeof(*dev_data), GFP_KERNEL);
	if (!dev_data)
		return -ENOMEM;
	if (hci->cmd == &mipi_i3c_hci_cmd_v1) {
#ifdef CONFIG_ARCH_ASPEED
		ret = mipi_i3c_hci_dat_v1.alloc_entry(hci, dev->info.dyn_addr);
#else
		ret = mipi_i3c_hci_dat_v1.alloc_entry(hci);
#endif
		if (ret < 0) {
			kfree(dev_data);
			return ret;
		}
		mipi_i3c_hci_dat_v1.set_dynamic_addr(hci, ret, dev->info.dyn_addr);
		dev_data->dat_idx = ret;
	}
	i3c_dev_set_master_data(dev, dev_data);
	return 0;
}

static int i3c_hci_reattach_i3c_dev(struct i3c_dev_desc *dev, u8 old_dyn_addr)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);

	DBG("");

	if (hci->cmd == &mipi_i3c_hci_cmd_v1)
		mipi_i3c_hci_dat_v1.set_dynamic_addr(hci, dev_data->dat_idx,
					     dev->info.dyn_addr);
	return 0;
}

static void i3c_hci_detach_i3c_dev(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);

	DBG("");

	i3c_dev_set_master_data(dev, NULL);
	if (hci->cmd == &mipi_i3c_hci_cmd_v1)
		mipi_i3c_hci_dat_v1.free_entry(hci, dev_data->dat_idx);
	kfree(dev_data);
}

static int i3c_hci_attach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data;
	int ret;

	DBG("");

	if (hci->cmd != &mipi_i3c_hci_cmd_v1)
		return 0;
	dev_data = kzalloc(sizeof(*dev_data), GFP_KERNEL);
	if (!dev_data)
		return -ENOMEM;
	#ifdef CONFIG_ARCH_ASPEED
		ret = mipi_i3c_hci_dat_v1.alloc_entry(hci, dev->addr);
	#else
		ret = mipi_i3c_hci_dat_v1.alloc_entry(hci);
	#endif
	if (ret < 0) {
		kfree(dev_data);
		return ret;
	}
	mipi_i3c_hci_dat_v1.set_static_addr(hci, ret, dev->addr);
	mipi_i3c_hci_dat_v1.set_flags(hci, ret, DAT_0_I2C_DEVICE, 0);
	dev_data->dat_idx = ret;
	i2c_dev_set_master_data(dev, dev_data);
	return 0;
}

static void i3c_hci_detach_i2c_dev(struct i2c_dev_desc *dev)
{
	struct i3c_master_controller *m = i2c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data = i2c_dev_get_master_data(dev);

	DBG("");

	if (dev_data) {
		i2c_dev_set_master_data(dev, NULL);
		if (hci->cmd == &mipi_i3c_hci_cmd_v1)
			mipi_i3c_hci_dat_v1.free_entry(hci, dev_data->dat_idx);
		kfree(dev_data);
	}
}

static int i3c_hci_request_ibi(struct i3c_dev_desc *dev,
			       const struct i3c_ibi_setup *req)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);
	unsigned int dat_idx = dev_data->dat_idx;

	if (req->max_payload_len != 0)
		mipi_i3c_hci_dat_v1.set_flags(hci, dat_idx, DAT_0_IBI_PAYLOAD, 0);
	else
		mipi_i3c_hci_dat_v1.clear_flags(hci, dat_idx, DAT_0_IBI_PAYLOAD, 0);
	return hci->io->request_ibi(hci, dev, req);
}

static void i3c_hci_free_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);

	hci->io->free_ibi(hci, dev);
}

static int i3c_hci_enable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);

	mipi_i3c_hci_dat_v1.clear_flags(hci, dev_data->dat_idx, DAT_0_SIR_REJECT, 0);
	return i3c_master_enec_locked(m, dev->info.dyn_addr, I3C_CCC_EVENT_SIR);
}

static int i3c_hci_disable_ibi(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_hci_dev_data *dev_data = i3c_dev_get_master_data(dev);

	mipi_i3c_hci_dat_v1.set_flags(hci, dev_data->dat_idx, DAT_0_SIR_REJECT, 0);
	return i3c_master_disec_locked(m, dev->info.dyn_addr, I3C_CCC_EVENT_SIR);
}

static void i3c_hci_recycle_ibi_slot(struct i3c_dev_desc *dev,
				     struct i3c_ibi_slot *slot)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);

	hci->io->recycle_ibi_slot(hci, dev, slot);
}

static const struct i3c_master_controller_ops i3c_hci_ops = {
	.bus_init		= i3c_hci_bus_init,
	.bus_cleanup		= i3c_hci_bus_cleanup,
	.do_daa			= i3c_hci_daa,
	.send_ccc_cmd		= i3c_hci_send_ccc_cmd,
	.priv_xfers		= i3c_hci_priv_xfers,
	.i2c_xfers		= i3c_hci_i2c_xfers,
	.attach_i3c_dev		= i3c_hci_attach_i3c_dev,
	.reattach_i3c_dev	= i3c_hci_reattach_i3c_dev,
	.detach_i3c_dev		= i3c_hci_detach_i3c_dev,
	.attach_i2c_dev		= i3c_hci_attach_i2c_dev,
	.detach_i2c_dev		= i3c_hci_detach_i2c_dev,
	.request_ibi		= i3c_hci_request_ibi,
	.free_ibi		= i3c_hci_free_ibi,
	.enable_ibi		= i3c_hci_enable_ibi,
	.disable_ibi		= i3c_hci_disable_ibi,
	.recycle_ibi_slot	= i3c_hci_recycle_ibi_slot,
};

static int ast2700_i3c_target_bus_init(struct i3c_master_controller *m)
{
	struct i3c_hci *hci = to_i3c_hci(m);
	struct i3c_dev_desc *desc = hci->master.this;
	u32 reg;
	int ret;

	dev_info(&hci->master.dev, "Secondary master Mode");

	ast_inhouse_write(ASPEED_I3C_SLV_PID_LO, SLV_PID_LO(desc->info.pid));
	ast_inhouse_write(ASPEED_I3C_SLV_PID_HI, SLV_PID_HI(desc->info.pid));

	desc->info.bcr = I3C_BCR_DEVICE_ROLE(I3C_BCR_I3C_MASTER) |
			 I3C_BCR_HDR_CAP | I3C_BCR_IBI_PAYLOAD |
			 I3C_BCR_IBI_REQ_CAP;
	reg = FIELD_PREP(ASPEED_I3C_SLV_CHAR_CTRL_DCR, desc->info.dcr) |
	      FIELD_PREP(ASPEED_I3C_SLV_CHAR_CTRL_BCR, desc->info.bcr);
	if (desc->info.static_addr) {
		reg |= ASPEED_I3C_SLV_CHAR_CTRL_STATIC_ADDR_EN |
		       FIELD_PREP(ASPEED_I3C_SLV_CHAR_CTRL_STATIC_ADDR,
				  desc->info.static_addr);
	}
	ast_inhouse_write(ASPEED_I3C_SLV_CHAR_CTRL, reg);
	reg = ast_inhouse_read(ASPEED_I3C_SLV_CAP_CTRL);
	/* Make slave will sned the ibi when bus idle */
	ast_inhouse_write(ASPEED_I3C_SLV_CAP_CTRL,
			  reg | ASPEED_I3C_SLV_CAP_CTRL_IBI_WAIT);

	ast_inhouse_write(ASPEED_I3C_CTRL,
			  ASPEED_I3C_CTRL_INIT |
				  FIELD_PREP(ASPEED_I3C_CTRL_INIT_MODE,
					     INIT_SEC_MST_MODE));

	ret = hci->io->init(hci);
	if (ret)
		return ret;

	return 0;
}

static void ast2700_i3c_target_bus_cleanup(struct i3c_master_controller *m)
{
	struct i3c_hci *hci = to_i3c_hci(m);

	DBG("");

	reg_clear(HC_CONTROL, HC_CONTROL_BUS_ENABLE);
	hci->io->cleanup(hci);
	kfree(hci->target_rx.buf);
}

static int ast2700_i3c_target_priv_xfers(struct i3c_dev_desc *dev,
					 struct i3c_priv_xfer *i3c_xfers,
					 int nxfers)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	struct hci_xfer *xfer;
	unsigned int size_limit;
	int i, ret = 0;

	DBG("nxfers = %d", nxfers);

	xfer = hci_alloc_xfer(nxfers);
	if (!xfer)
		return -ENOMEM;

	size_limit = 1U << (16 + FIELD_GET(HC_CAP_MAX_DATA_LENGTH, hci->caps));

	for (i = 0; i < nxfers; i++) {
		if (!i3c_xfers[i].rnw) {
			xfer[i].data_len = i3c_xfers[i].len;
			xfer[i].rnw = i3c_xfers[i].rnw;
			xfer[i].data = (void *)i3c_xfers[i].data.out;
			hci->cmd->prep_i3c_xfer(hci, dev, &xfer[i]);
		} else {
			dev_err(&hci->master.dev,
				"target mode can't do priv_read command\n");
		}
	}
	ret = hci->io->queue_xfer(hci, xfer, nxfers);
	if (ret)
		goto out;

out:
	hci_free_xfer(xfer, nxfers);

	return ret;
}

static int ast2700_i3c_target_generate_ibi(struct i3c_dev_desc *dev, const u8 *data, int len)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	u32 reg;

	if (data || len != 0)
		return -EOPNOTSUPP;

	DBG("");

	reg = ast_inhouse_read(ASPEED_I3C_SLV_STS1);
	if ((reg & ASPEED_I3C_SLV_STS1_IBI_EN) == 0)
		return -EPERM;

	init_completion(&hci->ibi_comp);
	reg = ast_inhouse_read(ASPEED_I3C_SLV_CAP_CTRL);
	ast_inhouse_write(ASPEED_I3C_SLV_CAP_CTRL,
			  reg | ASPEED_I3C_SLV_CAP_CTRL_IBI_REQ);

	if (!wait_for_completion_timeout(&hci->ibi_comp,
					 msecs_to_jiffies(1000))) {
		pr_warn("timeout waiting for completion\n");
		return -EINVAL;
	}

	return 0;
}

static int
ast2700_i3c_target_pending_read_notify(struct i3c_dev_desc *dev,
				       struct i3c_priv_xfer *pending_read,
				       struct i3c_priv_xfer *ibi_notify)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	u32 reg;

	if (!pending_read || !ibi_notify)
		return -EINVAL;

	reg = ast_inhouse_read(ASPEED_I3C_SLV_STS1);
	if ((reg & ASPEED_I3C_SLV_STS1_IBI_EN) == 0)
		return -EPERM;

	ast2700_i3c_target_priv_xfers(dev, ibi_notify, 1);
	ast2700_i3c_target_priv_xfers(dev, pending_read, 1);
	ast2700_i3c_target_generate_ibi(dev, NULL, 0);

	return 0;
}

static bool ast2700_i3c_target_is_ibi_enabled(struct i3c_dev_desc *dev)
{
	struct i3c_master_controller *m = i3c_dev_get_master(dev);
	struct i3c_hci *hci = to_i3c_hci(m);
	u32 reg;

	reg = ast_inhouse_read(ASPEED_I3C_SLV_STS1);
	return !!(reg & ASPEED_I3C_SLV_STS1_IBI_EN);
}

static const struct i3c_target_ops ast2700_i3c_target_ops = {
	.bus_init = ast2700_i3c_target_bus_init,
	.bus_cleanup = ast2700_i3c_target_bus_cleanup,
	.priv_xfers = ast2700_i3c_target_priv_xfers,
	.generate_ibi = ast2700_i3c_target_generate_ibi,
	.pending_read_notify = ast2700_i3c_target_pending_read_notify,
	.is_ibi_enabled = ast2700_i3c_target_is_ibi_enabled,
};

static irqreturn_t i3c_hci_irq_handler(int irq, void *dev_id)
{
	struct i3c_hci *hci = dev_id;
	irqreturn_t result = IRQ_NONE;
	u32 val;

	val = reg_read(INTR_STATUS);
	DBG("INTR_STATUS = %#x", val);

	if (val) {
		reg_write(INTR_STATUS, val);
	} else {
		/* v1.0 does not have PIO cascaded notification bits */
		val |= INTR_HC_PIO;
	}

	if (val & INTR_HC_RESET_CANCEL) {
		DBG("cancelled reset");
		val &= ~INTR_HC_RESET_CANCEL;
	}
	if (val & INTR_HC_INTERNAL_ERR) {
		dev_err(&hci->master.dev, "Host Controller Internal Error\n");
		val &= ~INTR_HC_INTERNAL_ERR;
	}
	if (val)
		dev_err(&hci->master.dev, "unexpected INTR_STATUS %#x\n", val);
	else
		result = IRQ_HANDLED;

	return result;
}

static irqreturn_t i3c_aspeed_irq_handler(int irqn, void *dev_id)
{
	struct i3c_hci *hci = dev_id;
	u32 val, inhouse_val;
	int result = -1;

	val = ast_inhouse_read(ASPEED_I3C_INTR_SUM_STATUS);
	dev_dbg(&hci->master.dev, "Global INTR_STATUS = %#x\n", val);

	if (val & ASPEED_INTR_SUM_CAP) {
		i3c_hci_irq_handler(irqn, dev_id);
		val &= ~ASPEED_INTR_SUM_CAP;
	}
	if (val & ASPEED_INTR_SUM_PIO) {
		hci->io->irq_handler(hci, 0);
		val &= ~ASPEED_INTR_SUM_PIO;
	}
	if (val & ASPEED_INTR_SUM_RHS) {
		hci->io->irq_handler(hci, 0);
		val &= ~ASPEED_INTR_SUM_RHS;
	}
	if (val & ASPEED_INTR_SUM_INHOUSE) {
		inhouse_val = ast_inhouse_read(ASPEED_I3C_INTR_STATUS);
		dev_dbg(&hci->master.dev, "Inhouse INTR_STATUS = %#x/%#x\n",
			inhouse_val,
			ast_inhouse_read(ASPEED_I3C_INTR_SIGNAL_ENABLE));
		ast_inhouse_write(ASPEED_I3C_INTR_STATUS, inhouse_val);
		val &= ~ASPEED_INTR_SUM_INHOUSE;
	}

	if (val)
		dev_err(&hci->master.dev, "unexpected INTR_SUN_STATUS %#x\n",
			val);
	else
		result = IRQ_HANDLED;

	return result;
}

static int i3c_hci_init(struct i3c_hci *hci)
{
	u32 regval, offset;
	int ret;

	/* Validate HCI hardware version */
	regval = reg_read(HCI_VERSION);
	hci->version_major = (regval >> 8) & 0xf;
	hci->version_minor = (regval >> 4) & 0xf;
	hci->revision = regval & 0xf;
	dev_notice(&hci->master.dev, "MIPI I3C HCI v%u.%u r%02u\n",
		   hci->version_major, hci->version_minor, hci->revision);
	/* known versions */
	switch (regval & ~0xf) {
	case 0x100:	/* version 1.0 */
	case 0x110:	/* version 1.1 */
	case 0x200:	/* version 2.0 */
		break;
	default:
		dev_err(&hci->master.dev, "unsupported HCI version\n");
		return -EPROTONOSUPPORT;
	}

	hci->caps = reg_read(HC_CAPABILITIES);
	DBG("caps = %#x", hci->caps);

	regval = reg_read(DAT_SECTION);
	offset = FIELD_GET(DAT_TABLE_OFFSET, regval);
	hci->DAT_regs = offset ? hci->base_regs + offset : NULL;
	hci->DAT_entries = FIELD_GET(DAT_TABLE_SIZE, regval);
	hci->DAT_entry_size = FIELD_GET(DAT_ENTRY_SIZE, regval) ? 0 : 8;
	dev_info(&hci->master.dev, "DAT: %u %u-bytes entries at offset %#x\n",
		 hci->DAT_entries, hci->DAT_entry_size * 4, offset);

	regval = reg_read(DCT_SECTION);
	offset = FIELD_GET(DCT_TABLE_OFFSET, regval);
	hci->DCT_regs = offset ? hci->base_regs + offset : NULL;
	hci->DCT_entries = FIELD_GET(DCT_TABLE_SIZE, regval);
	hci->DCT_entry_size = FIELD_GET(DCT_ENTRY_SIZE, regval) ? 0 : 16;
	dev_info(&hci->master.dev, "DCT: %u %u-bytes entries at offset %#x\n",
		 hci->DCT_entries, hci->DCT_entry_size * 4, offset);
#ifdef CONFIG_ARCH_ASPEED
	/* Currently, doesn't support dma mode*/
	hci->RHS_regs = NULL;
#else
	regval = reg_read(RING_HEADERS_SECTION);
	offset = FIELD_GET(RING_HEADERS_OFFSET, regval);
	hci->RHS_regs = offset ? hci->base_regs + offset : NULL;
	dev_info(&hci->master.dev, "Ring Headers at offset %#x\n", offset);
#endif
	regval = reg_read(PIO_SECTION);
	offset = FIELD_GET(PIO_REGS_OFFSET, regval);
	hci->PIO_regs = offset ? hci->base_regs + offset : NULL;
	dev_info(&hci->master.dev, "PIO section at offset %#x\n", offset);

	regval = reg_read(EXT_CAPS_SECTION);
	offset = FIELD_GET(EXT_CAPS_OFFSET, regval);
	hci->EXTCAPS_regs = offset ? hci->base_regs + offset : NULL;
	dev_info(&hci->master.dev, "Extended Caps at offset %#x\n", offset);

#ifdef CONFIG_ARCH_ASPEED
	hci->PHY_regs = hci->base_regs + ASPEED_PHY_REGS_OFFSET;
	dev_info(&hci->master.dev, "PHY control at offset %#x\n", ASPEED_PHY_REGS_OFFSET);
#else
	ret = i3c_hci_parse_ext_caps(hci);
	if (ret)
		return ret;
#endif

	/*
	 * Now let's reset the hardware.
	 * SOFT_RST must be clear before we write to it.
	 * Then we must wait until it clears again.
	 */
	ret = readx_poll_timeout(reg_read, RESET_CONTROL, regval,
				 !(regval & SOFT_RST), 1, 10000);
	if (ret)
		return -ENXIO;
	reg_write(RESET_CONTROL, SOFT_RST);
	ret = readx_poll_timeout(reg_read, RESET_CONTROL, regval,
				 !(regval & SOFT_RST), 1, 10000);
	if (ret)
		return -ENXIO;

	/* Disable all interrupts and allow all signal updates */
	reg_write(INTR_SIGNAL_ENABLE, 0x0);
	reg_write(INTR_STATUS_ENABLE, 0xffffffff);
#ifdef CONFIG_ARCH_ASPEED
	ast_inhouse_write(ASPEED_I3C_INTR_SIGNAL_ENABLE, 0);
	ast_inhouse_write(ASPEED_I3C_INTR_STATUS_ENABLE, 0xffffffff);
#endif

	/* Make sure our data ordering fits the host's */
	regval = reg_read(HC_CONTROL);
	if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN)) {
		if (!(regval & HC_CONTROL_DATA_BIG_ENDIAN)) {
			regval |= HC_CONTROL_DATA_BIG_ENDIAN;
			reg_write(HC_CONTROL, regval);
			regval = reg_read(HC_CONTROL);
			if (!(regval & HC_CONTROL_DATA_BIG_ENDIAN)) {
				dev_err(&hci->master.dev, "cannot set BE mode\n");
				return -EOPNOTSUPP;
			}
		}
	} else {
		if (regval & HC_CONTROL_DATA_BIG_ENDIAN) {
			regval &= ~HC_CONTROL_DATA_BIG_ENDIAN;
			reg_write(HC_CONTROL, regval);
			regval = reg_read(HC_CONTROL);
			if (regval & HC_CONTROL_DATA_BIG_ENDIAN) {
				dev_err(&hci->master.dev, "cannot clear BE mode\n");
				return -EOPNOTSUPP;
			}
		}
	}

	/* Select our command descriptor model */
	switch (FIELD_GET(HC_CAP_CMD_SIZE, hci->caps)) {
	case 0:
		hci->cmd = &mipi_i3c_hci_cmd_v1;
		break;
	case 1:
		hci->cmd = &mipi_i3c_hci_cmd_v2;
		break;
	default:
		dev_err(&hci->master.dev, "wrong CMD_SIZE capability value\n");
		return -EINVAL;
	}

	/* Try activating DMA operations first */
	if (hci->RHS_regs) {
		reg_clear(HC_CONTROL, HC_CONTROL_PIO_MODE);
		if (reg_read(HC_CONTROL) & HC_CONTROL_PIO_MODE) {
			dev_err(&hci->master.dev, "PIO mode is stuck\n");
			ret = -EIO;
		} else {
			hci->io = &mipi_i3c_hci_dma;
			dev_info(&hci->master.dev, "Using DMA\n");
		}
	}

	/* If no DMA, try PIO */
	if (!hci->io && hci->PIO_regs) {
		reg_set(HC_CONTROL, HC_CONTROL_PIO_MODE);
		if (!(reg_read(HC_CONTROL) & HC_CONTROL_PIO_MODE)) {
			dev_err(&hci->master.dev, "DMA mode is stuck\n");
			ret = -EIO;
		} else {
			hci->io = &mipi_i3c_hci_pio;
			dev_info(&hci->master.dev, "Using PIO\n");
		}
	}

	if (!hci->io) {
		dev_err(&hci->master.dev, "neither DMA nor PIO can be used\n");
		if (!ret)
			ret = -EINVAL;
		return ret;
	}

	return 0;
}

#ifdef CONFIG_ARCH_ASPEED
static int aspeed_i3c_of_populate_bus_timing(struct i3c_hci *hci,
					     struct device_node *np)
{
	u16 hcnt, lcnt;
	unsigned long core_rate, core_period;

	core_rate = clk_get_rate(hci->clk);
	/* core_period is in nanosecond */
	core_period = DIV_ROUND_UP(1000000000, core_rate);

	dev_info(&hci->master.dev, "core rate = %ld core period = %ld ns",
		 core_rate, core_period);

	hcnt = DIV_ROUND_CLOSEST(PHY_I2C_FM_DEFAULT_CAS_NS, core_period) - 1;
	lcnt = DIV_ROUND_CLOSEST(PHY_I2C_FM_DEFAULT_SU_STO_NS, core_period) - 1;
	ast_phy_write(PHY_I2C_FM_CTRL0,
		      FIELD_PREP(PHY_I2C_FM_CTRL0_CAS, hcnt) |
			      FIELD_PREP(PHY_I2C_FM_CTRL0_SU_STO, lcnt));

	hcnt = DIV_ROUND_CLOSEST(PHY_I2C_FM_DEFAULT_SCL_H_NS, core_period) - 1;
	lcnt = DIV_ROUND_CLOSEST(PHY_I2C_FM_DEFAULT_SCL_L_NS, core_period) - 1;
	ast_phy_write(PHY_I2C_FM_CTRL1,
		      FIELD_PREP(PHY_I2C_FM_CTRL1_SCL_H, hcnt) |
			      FIELD_PREP(PHY_I2C_FM_CTRL1_SCL_L, lcnt));
	ast_phy_write(PHY_I2C_FM_CTRL2,
		      FIELD_PREP(PHY_I2C_FM_CTRL2_ACK_H, hcnt) |
			      FIELD_PREP(PHY_I2C_FM_CTRL2_ACK_L, hcnt));
	hcnt = DIV_ROUND_CLOSEST(PHY_I2C_FM_DEFAULT_HD_DAT, core_period) - 1;
	lcnt = DIV_ROUND_CLOSEST(PHY_I2C_FM_DEFAULT_AHD_DAT, core_period) - 1;
	ast_phy_write(PHY_I2C_FM_CTRL3,
		      FIELD_PREP(PHY_I2C_FM_CTRL3_HD_DAT, hcnt) |
			      FIELD_PREP(PHY_I2C_FM_CTRL3_AHD_DAT, lcnt));

	hcnt = DIV_ROUND_CLOSEST(PHY_I2C_FMP_DEFAULT_CAS_NS, core_period) - 1;
	lcnt = DIV_ROUND_CLOSEST(PHY_I2C_FMP_DEFAULT_SU_STO_NS, core_period) - 1;
	ast_phy_write(PHY_I2C_FMP_CTRL0,
		      FIELD_PREP(PHY_I2C_FMP_CTRL0_CAS, hcnt) |
			      FIELD_PREP(PHY_I2C_FMP_CTRL0_SU_STO, lcnt));

	hcnt = DIV_ROUND_CLOSEST(PHY_I2C_FMP_DEFAULT_SCL_H_NS, core_period) - 1;
	lcnt = DIV_ROUND_CLOSEST(PHY_I2C_FMP_DEFAULT_SCL_L_NS, core_period) - 1;
	ast_phy_write(PHY_I2C_FMP_CTRL1,
		      FIELD_PREP(PHY_I2C_FMP_CTRL1_SCL_H, hcnt) |
			      FIELD_PREP(PHY_I2C_FMP_CTRL1_SCL_L, lcnt));
	ast_phy_write(PHY_I2C_FMP_CTRL2,
		      FIELD_PREP(PHY_I2C_FMP_CTRL2_ACK_H, hcnt) |
			      FIELD_PREP(PHY_I2C_FMP_CTRL2_ACK_L, hcnt));
	hcnt = DIV_ROUND_CLOSEST(PHY_I2C_FMP_DEFAULT_HD_DAT, core_period) - 1;
	lcnt = DIV_ROUND_CLOSEST(PHY_I2C_FMP_DEFAULT_AHD_DAT, core_period) - 1;
	ast_phy_write(PHY_I2C_FMP_CTRL3,
		      FIELD_PREP(PHY_I2C_FMP_CTRL3_HD_DAT, hcnt) |
			      FIELD_PREP(PHY_I2C_FMP_CTRL3_AHD_DAT, lcnt));

	hcnt = DIV_ROUND_CLOSEST(PHY_I3C_OD_DEFAULT_CAS_NS, core_period) - 1;
	lcnt = DIV_ROUND_CLOSEST(PHY_I3C_OD_DEFAULT_SU_STO_NS, core_period) - 1;
	ast_phy_write(PHY_I3C_OD_CTRL0,
		      FIELD_PREP(PHY_I3C_OD_CTRL0_CAS, hcnt) |
			      FIELD_PREP(PHY_I3C_OD_CTRL0_SU_STO, lcnt));

	hcnt = DIV_ROUND_CLOSEST(PHY_I3C_OD_DEFAULT_SCL_H_NS, core_period) - 1;
	lcnt = DIV_ROUND_CLOSEST(PHY_I3C_OD_DEFAULT_SCL_L_NS, core_period) - 1;
	ast_phy_write(PHY_I3C_OD_CTRL1,
		      FIELD_PREP(PHY_I3C_OD_CTRL1_SCL_H, hcnt) |
			      FIELD_PREP(PHY_I3C_OD_CTRL1_SCL_L, lcnt));
	ast_phy_write(PHY_I3C_OD_CTRL2,
		      FIELD_PREP(PHY_I3C_OD_CTRL2_ACK_H, hcnt) |
			      FIELD_PREP(PHY_I3C_OD_CTRL2_ACK_L, hcnt));
	hcnt = DIV_ROUND_CLOSEST(PHY_I3C_OD_DEFAULT_HD_DAT, core_period) - 1;
	lcnt = DIV_ROUND_CLOSEST(PHY_I3C_OD_DEFAULT_AHD_DAT, core_period) - 1;
	ast_phy_write(PHY_I3C_OD_CTRL3,
		      FIELD_PREP(PHY_I3C_OD_CTRL3_HD_DAT, hcnt) |
			      FIELD_PREP(PHY_I3C_OD_CTRL3_AHD_DAT, lcnt));

	hcnt = DIV_ROUND_CLOSEST(PHY_I3C_SDR0_DEFAULT_SCL_H_NS, core_period) - 1;
	lcnt = DIV_ROUND_CLOSEST(PHY_I3C_SDR0_DEFAULT_SCL_L_NS, core_period) - 1;
	ast_phy_write(PHY_I3C_SDR0_CTRL0,
		      FIELD_PREP(PHY_I3C_SDR0_CTRL0_SCL_H, hcnt) |
			      FIELD_PREP(PHY_I3C_SDR0_CTRL0_SCL_L, lcnt));
	hcnt = DIV_ROUND_CLOSEST(PHY_I3C_SDR0_DEFAULT_TBIT_H_NS, core_period) - 1;
	lcnt = DIV_ROUND_CLOSEST(PHY_I3C_SDR0_DEFAULT_TBIT_L_NS, core_period) - 1;
	ast_phy_write(PHY_I3C_SDR0_CTRL1,
		      FIELD_PREP(PHY_I3C_SDR0_CTRL1_TBIT_H, hcnt) |
			      FIELD_PREP(PHY_I3C_SDR0_CTRL1_TBIT_L, lcnt));
	hcnt = DIV_ROUND_CLOSEST(PHY_I3C_SDR0_DEFAULT_HD_PP_NS, core_period) -
	       1;
	lcnt = DIV_ROUND_CLOSEST(PHY_I3C_SDR0_DEFAULT_TBIT_HD_PP_NS,
				 core_period) -
	       1;
	ast_phy_write(PHY_I3C_SDR0_CTRL2,
		      FIELD_PREP(PHY_I3C_SDR0_CTRL2_HD_PP, hcnt) |
			      FIELD_PREP(PHY_I3C_SDR0_CTRL2_TBIT_HD_PP, lcnt));

	hcnt = DIV_ROUND_CLOSEST(PHY_I3C_SR_P_DEFAULT_HD_NS, core_period) - 1;
	lcnt = DIV_ROUND_CLOSEST(PHY_I3C_SR_P_DEFAULT_SCL_L_NS, core_period) - 1;
	ast_phy_write(PHY_I3C_SR_P_PREPARE_CTRL,
		      FIELD_PREP(PHY_I3C_SR_P_PREPARE_CTRL_HD, hcnt) |
			      FIELD_PREP(PHY_I3C_SR_P_PREPARE_CTRL_SCL_L,
					 lcnt));
	ast_phy_write(PHY_PULLUP_EN, 0x0);

	return 0;
}
#endif

static int i3c_hci_probe(struct platform_device *pdev)
{
	struct i3c_hci *hci;
	int irq, ret;

	hci = devm_kzalloc(&pdev->dev, sizeof(*hci), GFP_KERNEL);
	if (!hci)
		return -ENOMEM;
	hci->base_regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hci->base_regs))
		return PTR_ERR(hci->base_regs);

	platform_set_drvdata(pdev, hci);
	/* temporary for dev_printk's, to be replaced in i3c_master_register */
	hci->master.dev.init_name = dev_name(&pdev->dev);

	hci->rst = devm_reset_control_get_optional_exclusive(&pdev->dev, NULL);
	if (IS_ERR(hci->rst)) {
		dev_err(&pdev->dev,
			"missing or invalid reset controller device tree entry");
		return PTR_ERR(hci->rst);
	}
	reset_control_deassert(hci->rst);

	hci->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(hci->clk)) {
		dev_err(&pdev->dev,
			"missing or invalid clock controller device tree entry");
		return PTR_ERR(hci->clk);
	}

	ret = clk_prepare_enable(hci->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable i3c clock.\n");
		return ret;
	}

	ret = i3c_hci_init(hci);
	if (ret)
		return ret;

#ifdef CONFIG_ARCH_ASPEED
	ret = aspeed_i3c_of_populate_bus_timing(hci, pdev->dev.of_node);
	if (ret)
		return ret;
#endif

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(&pdev->dev, irq, i3c_aspeed_irq_handler,
			       0, NULL, hci);
	if (ret)
		return ret;

	ret = i3c_register(&hci->master, &pdev->dev, &i3c_hci_ops,
			   &ast2700_i3c_target_ops, false);
	if (ret)
		return ret;

	return 0;
}

static void i3c_hci_remove(struct platform_device *pdev)
{
	struct i3c_hci *hci = platform_get_drvdata(pdev);

	i3c_master_unregister(&hci->master);
}

static const __maybe_unused struct of_device_id i3c_hci_of_match[] = {
	{ .compatible = "mipi-i3c-hci", },
	{ .compatible = "aspeed-i3c-hci", },
	{},
};
MODULE_DEVICE_TABLE(of, i3c_hci_of_match);

static struct platform_driver i3c_hci_driver = {
	.probe = i3c_hci_probe,
	.remove_new = i3c_hci_remove,
	.driver = {
		.name = "mipi-i3c-hci",
		.of_match_table = of_match_ptr(i3c_hci_of_match),
	},
};
module_platform_driver(i3c_hci_driver);

MODULE_AUTHOR("Nicolas Pitre <npitre@baylibre.com>");
MODULE_DESCRIPTION("MIPI I3C HCI driver");
MODULE_LICENSE("Dual BSD/GPL");
