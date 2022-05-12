// SPDX-License-Identifier: GPL-2.0-only
/*
 * ASPEED AST2600 new register set I2C controller driver
 *
 * Copyright (C) ASPEED Technology Inc.
 */
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/of_device.h>
#include <linux/dma-mapping.h>
#include <linux/i2c-smbus.h>
#include "i2c-ast2600-global.h"

/* 0x00 : I2CC Master/Slave Function Control Register  */
#define AST2600_I2CC_FUN_CTRL		0x00
#define AST2600_I2CC_SLAVE_ADDR_RX_EN		BIT(20)
#define AST2600_I2CC_MASTER_RETRY_MASK		GENMASK(19, 18)
#define AST2600_I2CC_MASTER_RETRY(x)		(((x) & GENMASK(1, 0)) << 18)
#define AST2600_I2CC_BUS_AUTO_RELEASE		BIT(17)
#define AST2600_I2CC_M_SDA_LOCK_EN			BIT(16)
#define AST2600_I2CC_MULTI_MASTER_DIS		BIT(15)
#define AST2600_I2CC_M_SCL_DRIVE_EN			BIT(14)
#define AST2600_I2CC_MSB_STS				BIT(9)
#define AST2600_I2CC_SDA_DRIVE_1T_EN		BIT(8)
#define AST2600_I2CC_M_SDA_DRIVE_1T_EN		BIT(7)
#define AST2600_I2CC_M_HIGH_SPEED_EN		BIT(6)
/* reserver 5 : 2 */
#define AST2600_I2CC_SLAVE_EN			BIT(1)
#define AST2600_I2CC_MASTER_EN			BIT(0)

/* 0x04 : I2CC Master/Slave Clock and AC Timing Control Register #1 */
#define AST2600_I2CC_AC_TIMING		0x04
#define AST2600_I2CC_TTIMEOUT(x)			(((x) & GENMASK(4, 0)) << 24)
#define AST2600_I2CC_TCKHIGHMIN(x)			(((x) & GENMASK(3, 0)) << 20)
#define AST2600_I2CC_TCKHIGH(x)			(((x) & GENMASK(3, 0)) << 16)
#define AST2600_I2CC_TCKLOW(x)			(((x) & GENMASK(3, 0)) << 12)
#define AST2600_I2CC_THDDAT(x)			(((x) & GENMASK(1, 0)) << 10)
#define AST2600_I2CC_TOUTBASECLK(x)			(((x) & GENMASK(1, 0)) << 8)
#define AST2600_I2CC_TBASECLK(x)			((x) & GENMASK(3, 0))

/* 0x08 : I2CC Master/Slave Transmit/Receive Byte Buffer Register */
#define AST2600_I2CC_STS_AND_BUFF		0x08
#define AST2600_I2CC_TX_DIR_MASK			GENMASK(31, 29)
#define AST2600_I2CC_SDA_OE				BIT(28)
#define AST2600_I2CC_SDA_O				BIT(27)
#define AST2600_I2CC_SCL_OE				BIT(26)
#define AST2600_I2CC_SCL_O				BIT(25)

#define AST2600_I2CC_SCL_LINE_STS			BIT(18)
#define AST2600_I2CC_SDA_LINE_STS			BIT(17)
#define AST2600_I2CC_BUS_BUSY_STS			BIT(16)

#define AST2600_I2CC_GET_RX_BUFF(x)			(((x) >> 8) & GENMASK(7, 0))

/* 0x0C : I2CC Master/Slave Pool Buffer Control Register  */
#define AST2600_I2CC_BUFF_CTRL		0x0C
#define AST2600_I2CC_GET_RX_BUF_LEN(x)		(((x) >> 24) & GENMASK(5, 0))
#define AST2600_I2CC_SET_RX_BUF_LEN(x)		((((x) - 1) & GENMASK(4, 0)) << 16)
#define AST2600_I2CC_SET_TX_BUF_LEN(x)		((((x) - 1) & GENMASK(4, 0)) << 8)
#define AST2600_I2CC_GET_TX_BUF_LEN(x)		((((x) >> 8) & GENMASK(4, 0)) + 1)

/* 0x10 : I2CM Master Interrupt Control Register */
#define AST2600_I2CM_IER			0x10
/* 0x14 : I2CM Master Interrupt Status Register   : WC */
#define AST2600_I2CM_ISR			0x14

#define AST2600_I2CM_PKT_TIMEOUT			BIT(18)
#define AST2600_I2CM_PKT_ERROR			BIT(17)
#define AST2600_I2CM_PKT_DONE			BIT(16)

#define AST2600_I2CM_BUS_RECOVER_FAIL		BIT(15)
#define AST2600_I2CM_SDA_DL_TO			BIT(14)
#define AST2600_I2CM_BUS_RECOVER			BIT(13)
#define AST2600_I2CM_SMBUS_ALT			BIT(12)

#define AST2600_I2CM_SCL_LOW_TO			BIT(6)
#define AST2600_I2CM_ABNORMAL			BIT(5)
#define AST2600_I2CM_NORMAL_STOP			BIT(4)
#define AST2600_I2CM_ARBIT_LOSS			BIT(3)
#define AST2600_I2CM_RX_DONE			BIT(2)
#define AST2600_I2CM_TX_NAK				BIT(1)
#define AST2600_I2CM_TX_ACK				BIT(0)

/* 0x18 : I2CM Master Command/Status Register   */
#define AST2600_I2CM_CMD_STS		0x18
#define AST2600_I2CM_PKT_ADDR(x)			(((x) & GENMASK(6, 0)) << 24)
#define AST2600_I2CM_PKT_EN				BIT(16)
#define AST2600_I2CM_SDA_OE_OUT_DIR			BIT(15)
#define AST2600_I2CM_SDA_O_OUT_DIR			BIT(14)
#define AST2600_I2CM_SCL_OE_OUT_DIR			BIT(13)
#define AST2600_I2CM_SCL_O_OUT_DIR			BIT(12)
#define AST2600_I2CM_RECOVER_CMD_EN			BIT(11)

#define AST2600_I2CM_RX_DMA_EN			BIT(9)
#define AST2600_I2CM_TX_DMA_EN			BIT(8)
/* Command Bit */
#define AST2600_I2CM_RX_BUFF_EN			BIT(7)
#define AST2600_I2CM_TX_BUFF_EN			BIT(6)
#define AST2600_I2CM_STOP_CMD			BIT(5)
#define AST2600_I2CM_RX_CMD_LAST			BIT(4)
#define AST2600_I2CM_RX_CMD				BIT(3)

#define AST2600_I2CM_TX_CMD				BIT(1)
#define AST2600_I2CM_START_CMD			BIT(0)

/* 0x1C : I2CM Master DMA Transfer Length Register	 */
#define AST2600_I2CM_DMA_LEN		0x1C
/* Tx Rx support length 1 ~ 4096 */
#define AST2600_I2CM_SET_RX_DMA_LEN(x)	((((x) & GENMASK(11, 0)) << 16) | BIT(31))
#define AST2600_I2CM_SET_TX_DMA_LEN(x)	(((x) & GENMASK(11, 0)) | BIT(15))

/* 0x20 : I2CS Slave Interrupt Control Register   */
#define AST2600_I2CS_IER			0x20
/* 0x24 : I2CS Slave Interrupt Status Register	 */
#define AST2600_I2CS_ISR			0x24

#define AST2600_I2CS_ADDR_INDICATE_MASK	GENMASK(31, 30)
#define AST2600_I2CS_SLAVE_PENDING			BIT(29)

#define AST2600_I2CS_WAIT_TX_DMA			BIT(25)
#define AST2600_I2CS_WAIT_RX_DMA			BIT(24)

#define AST2600_I2CS_ADDR3_NAK			BIT(22)
#define AST2600_I2CS_ADDR2_NAK			BIT(21)
#define AST2600_I2CS_ADDR1_NAK			BIT(20)

#define AST2600_I2CS_ADDR_MASK			GENMASK(19, 18)
#define AST2600_I2CS_PKT_ERROR			BIT(17)
#define AST2600_I2CS_PKT_DONE			BIT(16)
#define AST2600_I2CS_INACTIVE_TO			BIT(15)

#define AST2600_I2CS_SLAVE_MATCH			BIT(7)
#define AST2600_I2CS_ABNOR_STOP			BIT(5)
#define AST2600_I2CS_STOP				BIT(4)
#define AST2600_I2CS_RX_DONE_NAK			BIT(3)
#define AST2600_I2CS_RX_DONE			BIT(2)
#define AST2600_I2CS_TX_NAK				BIT(1)
#define AST2600_I2CS_TX_ACK				BIT(0)

/* 0x28 : I2CS Slave CMD/Status Register   */
#define AST2600_I2CS_CMD_STS		0x28
#define AST2600_I2CS_ACTIVE_ALL			GENMASK(18, 17)
#define AST2600_I2CS_PKT_MODE_EN			BIT(16)
#define AST2600_I2CS_AUTO_NAK_NOADDR		BIT(15)
#define AST2600_I2CS_AUTO_NAK_EN			BIT(14)

#define AST2600_I2CS_ALT_EN				BIT(10)
#define AST2600_I2CS_RX_DMA_EN			BIT(9)
#define AST2600_I2CS_TX_DMA_EN			BIT(8)
#define AST2600_I2CS_RX_BUFF_EN			BIT(7)
#define AST2600_I2CS_TX_BUFF_EN			BIT(6)
#define AST2600_I2CS_RX_CMD_LAST			BIT(4)

#define AST2600_I2CS_TX_CMD				BIT(2)

#define AST2600_I2CS_DMA_LEN		0x2C
#define AST2600_I2CS_SET_RX_DMA_LEN(x)	(((((x) - 1) & GENMASK(11, 0)) << 16) | BIT(31))
#define AST2600_I2CS_RX_DMA_LEN_MASK	(GENMASK(11, 0) << 16)

#define AST2600_I2CS_SET_TX_DMA_LEN(x)	((((x) - 1) & GENMASK(11, 0)) | BIT(15))
#define AST2600_I2CS_TX_DMA_LEN_MASK	GENMASK(11, 0)

/* I2CM Master DMA Tx Buffer Register   */
#define AST2600_I2CM_TX_DMA			0x30
/* I2CM Master DMA Rx Buffer Register	*/
#define AST2600_I2CM_RX_DMA			0x34
/* I2CS Slave DMA Tx Buffer Register   */
#define AST2600_I2CS_TX_DMA			0x38
/* I2CS Slave DMA Rx Buffer Register   */
#define AST2600_I2CS_RX_DMA			0x3C

#define AST2600_I2CS_ADDR_CTRL		0x40

#define	AST2600_I2CS_ADDR3_MASK		GENMASK(22, 16)
#define	AST2600_I2CS_ADDR2_MASK		GENMASK(14, 8)
#define	AST2600_I2CS_ADDR1_MASK		GENMASK(6, 0)

#define AST2600_I2CM_DMA_LEN_STS		0x48
#define AST2600_I2CS_DMA_LEN_STS		0x4C

#define AST2600_I2C_GET_TX_DMA_LEN(x)		((x) & GENMASK(12, 0))
#define AST2600_I2C_GET_RX_DMA_LEN(x)		(((x) >> 16) & GENMASK(12, 0))

/* 0x40 : Slave Device Address Register */
#define AST2600_I2CS_ADDR3_ENABLE			BIT(23)
#define AST2600_I2CS_ADDR3(x)			((x) << 16)

#define AST2600_I2CS_ADDR2_ENABLE			BIT(15)
#define AST2600_I2CS_ADDR2(x)			((x) << 8)
#define AST2600_I2CS_ADDR1_ENABLE			BIT(7)
#define AST2600_I2CS_ADDR1(x)			(x)

#define I2C_SLAVE_MSG_BUF_SIZE		256

#define AST2600_I2C_DMA_SIZE		4096

#define MASTER_TRIGGER_LAST_STOP	(AST2600_I2CM_RX_CMD_LAST | AST2600_I2CM_STOP_CMD)
#define SLAVE_TRIGGER_CMD	(AST2600_I2CS_ACTIVE_ALL | AST2600_I2CS_PKT_MODE_EN)

/* i2c timeout counter: use base clk4 1Mhz
 * 1/(1000/4096) = 4.096ms * 8 = 32.768ms
 */
#define AST_I2C_TIMEOUT_CLK		0x2
#define AST_I2C_TIMEOUT_COUNT	0x8

struct ast2600_i2c_timing_table {
	u32 divisor;
	u32 timing;
};

static struct ast2600_i2c_timing_table i2c_legacy_timing_table[] = {
	/* Divisor : Base Clock : tCKHighMin : tCK High : tCK Low  */
	/* Divisor :	  [3:0] : [23: 20]   :   [19:16]:   [15:12] */
	{ 6, 0x00000300 | (0x0) | (0x2 << 20) | (0x2 << 16) | (0x2 << 12) },
	{ 7, 0x00000300 | (0x0) | (0x3 << 20) | (0x3 << 16) | (0x2 << 12) },
	{ 8, 0x00000300 | (0x0) | (0x3 << 20) | (0x3 << 16) | (0x3 << 12) },
	{ 9, 0x00000300 | (0x0) | (0x4 << 20) | (0x4 << 16) | (0x3 << 12) },
	{ 10, 0x00000300 | (0x0) | (0x4 << 20) | (0x4 << 16) | (0x4 << 12) },
	{ 11, 0x00000300 | (0x0) | (0x5 << 20) | (0x5 << 16) | (0x4 << 12) },
	{ 12, 0x00000300 | (0x0) | (0x5 << 20) | (0x5 << 16) | (0x5 << 12) },
	{ 13, 0x00000300 | (0x0) | (0x6 << 20) | (0x6 << 16) | (0x5 << 12) },
	{ 14, 0x00000300 | (0x0) | (0x6 << 20) | (0x6 << 16) | (0x6 << 12) },
	{ 15, 0x00000300 | (0x0) | (0x7 << 20) | (0x7 << 16) | (0x6 << 12) },
	{ 16, 0x00000300 | (0x0) | (0x7 << 20) | (0x7 << 16) | (0x7 << 12) },
	{ 17, 0x00000300 | (0x0) | (0x8 << 20) | (0x8 << 16) | (0x7 << 12) },
	{ 18, 0x00000300 | (0x0) | (0x8 << 20) | (0x8 << 16) | (0x8 << 12) },
	{ 19, 0x00000300 | (0x0) | (0x9 << 20) | (0x9 << 16) | (0x8 << 12) },
	{ 20, 0x00000300 | (0x0) | (0x9 << 20) | (0x9 << 16) | (0x9 << 12) },
	{ 21, 0x00000300 | (0x0) | (0xa << 20) | (0xa << 16) | (0x9 << 12) },
	{ 22, 0x00000300 | (0x0) | (0xa << 20) | (0xa << 16) | (0xa << 12) },
	{ 23, 0x00000300 | (0x0) | (0xb << 20) | (0xb << 16) | (0xa << 12) },
	{ 24, 0x00000300 | (0x0) | (0xb << 20) | (0xb << 16) | (0xb << 12) },
	{ 25, 0x00000300 | (0x0) | (0xc << 20) | (0xc << 16) | (0xb << 12) },
	{ 26, 0x00000300 | (0x0) | (0xc << 20) | (0xc << 16) | (0xc << 12) },
	{ 27, 0x00000300 | (0x0) | (0xd << 20) | (0xd << 16) | (0xc << 12) },
	{ 28, 0x00000300 | (0x0) | (0xd << 20) | (0xd << 16) | (0xd << 12) },
	{ 29, 0x00000300 | (0x0) | (0xe << 20) | (0xe << 16) | (0xd << 12) },
	{ 30, 0x00000300 | (0x0) | (0xe << 20) | (0xe << 16) | (0xe << 12) },
	{ 31, 0x00000300 | (0x0) | (0xf << 20) | (0xf << 16) | (0xe << 12) },
	{ 32, 0x00000300 | (0x0) | (0xf << 20) | (0xf << 16) | (0xf << 12) },

	{ 34, 0x00000300 | (0x1) | (0x8 << 20) | (0x8 << 16) | (0x7 << 12) },
	{ 36, 0x00000300 | (0x1) | (0x8 << 20) | (0x8 << 16) | (0x8 << 12) },
	{ 38, 0x00000300 | (0x1) | (0x9 << 20) | (0x9 << 16) | (0x8 << 12) },
	{ 40, 0x00000300 | (0x1) | (0x9 << 20) | (0x9 << 16) | (0x9 << 12) },
	{ 42, 0x00000300 | (0x1) | (0xa << 20) | (0xa << 16) | (0x9 << 12) },
	{ 44, 0x00000300 | (0x1) | (0xa << 20) | (0xa << 16) | (0xa << 12) },
	{ 46, 0x00000300 | (0x1) | (0xb << 20) | (0xb << 16) | (0xa << 12) },
	{ 48, 0x00000300 | (0x1) | (0xb << 20) | (0xb << 16) | (0xb << 12) },
	{ 50, 0x00000300 | (0x1) | (0xc << 20) | (0xc << 16) | (0xb << 12) },
	{ 52, 0x00000300 | (0x1) | (0xc << 20) | (0xc << 16) | (0xc << 12) },
	{ 54, 0x00000300 | (0x1) | (0xd << 20) | (0xd << 16) | (0xc << 12) },
	{ 56, 0x00000300 | (0x1) | (0xd << 20) | (0xd << 16) | (0xd << 12) },
	{ 58, 0x00000300 | (0x1) | (0xe << 20) | (0xe << 16) | (0xd << 12) },
	{ 60, 0x00000300 | (0x1) | (0xe << 20) | (0xe << 16) | (0xe << 12) },
	{ 62, 0x00000300 | (0x1) | (0xf << 20) | (0xf << 16) | (0xe << 12) },
	{ 64, 0x00000300 | (0x1) | (0xf << 20) | (0xf << 16) | (0xf << 12) },

	{ 68, 0x00000300 | (0x2) | (0x8 << 20) | (0x8 << 16) | (0x7 << 12) },
	{ 72, 0x00000300 | (0x2) | (0x8 << 20) | (0x8 << 16) | (0x8 << 12) },
	{ 76, 0x00000300 | (0x2) | (0x9 << 20) | (0x9 << 16) | (0x8 << 12) },
	{ 80, 0x00000300 | (0x2) | (0x9 << 20) | (0x9 << 16) | (0x9 << 12) },
	{ 84, 0x00000300 | (0x2) | (0xa << 20) | (0xa << 16) | (0x9 << 12) },
	{ 88, 0x00000300 | (0x2) | (0xa << 20) | (0xa << 16) | (0xa << 12) },
	{ 92, 0x00000300 | (0x2) | (0xb << 20) | (0xb << 16) | (0xa << 12) },
	{ 96, 0x00000300 | (0x2) | (0xb << 20) | (0xb << 16) | (0xb << 12) },
	{ 100, 0x00000300 | (0x2) | (0xc << 20) | (0xc << 16) | (0xb << 12) },
	{ 104, 0x00000300 | (0x2) | (0xc << 20) | (0xc << 16) | (0xc << 12) },
	{ 108, 0x00000300 | (0x2) | (0xd << 20) | (0xd << 16) | (0xc << 12) },
	{ 112, 0x00000300 | (0x2) | (0xd << 20) | (0xd << 16) | (0xd << 12) },
	{ 116, 0x00000300 | (0x2) | (0xe << 20) | (0xe << 16) | (0xd << 12) },
	{ 120, 0x00000300 | (0x2) | (0xe << 20) | (0xe << 16) | (0xe << 12) },
	{ 124, 0x00000300 | (0x2) | (0xf << 20) | (0xf << 16) | (0xe << 12) },
	{ 128, 0x00000300 | (0x2) | (0xf << 20) | (0xf << 16) | (0xf << 12) },

	{ 136, 0x00000300 | (0x3) | (0x8 << 20) | (0x8 << 16) | (0x7 << 12) },
	{ 144, 0x00000300 | (0x3) | (0x8 << 20) | (0x8 << 16) | (0x8 << 12) },
	{ 152, 0x00000300 | (0x3) | (0x9 << 20) | (0x9 << 16) | (0x8 << 12) },
	{ 160, 0x00000300 | (0x3) | (0x9 << 20) | (0x9 << 16) | (0x9 << 12) },
	{ 168, 0x00000300 | (0x3) | (0xa << 20) | (0xa << 16) | (0x9 << 12) },
	{ 176, 0x00000300 | (0x3) | (0xa << 20) | (0xa << 16) | (0xa << 12) },
	{ 184, 0x00000300 | (0x3) | (0xb << 20) | (0xb << 16) | (0xa << 12) },
	{ 192, 0x00000300 | (0x3) | (0xb << 20) | (0xb << 16) | (0xb << 12) },
	{ 200, 0x00000300 | (0x3) | (0xc << 20) | (0xc << 16) | (0xb << 12) },
	{ 208, 0x00000300 | (0x3) | (0xc << 20) | (0xc << 16) | (0xc << 12) },
	{ 216, 0x00000300 | (0x3) | (0xd << 20) | (0xd << 16) | (0xc << 12) },
	{ 224, 0x00000300 | (0x3) | (0xd << 20) | (0xd << 16) | (0xd << 12) },
	{ 232, 0x00000300 | (0x3) | (0xe << 20) | (0xe << 16) | (0xd << 12) },
	{ 240, 0x00000300 | (0x3) | (0xe << 20) | (0xe << 16) | (0xe << 12) },
	{ 248, 0x00000300 | (0x3) | (0xf << 20) | (0xf << 16) | (0xe << 12) },
	{ 256, 0x00000300 | (0x3) | (0xf << 20) | (0xf << 16) | (0xf << 12) },

	{ 272, 0x00000300 | (0x4) | (0x8 << 20) | (0x8 << 16) | (0x7 << 12) },
	{ 288, 0x00000300 | (0x4) | (0x8 << 20) | (0x8 << 16) | (0x8 << 12) },
	{ 304, 0x00000300 | (0x4) | (0x9 << 20) | (0x9 << 16) | (0x8 << 12) },
	{ 320, 0x00000300 | (0x4) | (0x9 << 20) | (0x9 << 16) | (0x9 << 12) },
	{ 336, 0x00000300 | (0x4) | (0xa << 20) | (0xa << 16) | (0x9 << 12) },
	{ 352, 0x00000300 | (0x4) | (0xa << 20) | (0xa << 16) | (0xa << 12) },
	{ 368, 0x00000300 | (0x4) | (0xb << 20) | (0xb << 16) | (0xa << 12) },
	{ 384, 0x00000300 | (0x4) | (0xb << 20) | (0xb << 16) | (0xb << 12) },
	{ 400, 0x00000300 | (0x4) | (0xc << 20) | (0xc << 16) | (0xb << 12) },
	{ 416, 0x00000300 | (0x4) | (0xc << 20) | (0xc << 16) | (0xc << 12) },
	{ 432, 0x00000300 | (0x4) | (0xd << 20) | (0xd << 16) | (0xc << 12) },
	{ 448, 0x00000300 | (0x4) | (0xd << 20) | (0xd << 16) | (0xd << 12) },
	{ 464, 0x00000300 | (0x4) | (0xe << 20) | (0xe << 16) | (0xd << 12) },
	{ 480, 0x00000300 | (0x4) | (0xe << 20) | (0xe << 16) | (0xe << 12) },
	{ 496, 0x00000300 | (0x4) | (0xf << 20) | (0xf << 16) | (0xe << 12) },
	{ 512, 0x00000300 | (0x4) | (0xf << 20) | (0xf << 16) | (0xf << 12) },

	{ 544, 0x00000300 | (0x5) | (0x8 << 20) | (0x8 << 16) | (0x7 << 12) },
	{ 576, 0x00000300 | (0x5) | (0x8 << 20) | (0x8 << 16) | (0x8 << 12) },
	{ 608, 0x00000300 | (0x5) | (0x9 << 20) | (0x9 << 16) | (0x8 << 12) },
	{ 640, 0x00000300 | (0x5) | (0x9 << 20) | (0x9 << 16) | (0x9 << 12) },
	{ 672, 0x00000300 | (0x5) | (0xa << 20) | (0xa << 16) | (0x9 << 12) },
	{ 704, 0x00000300 | (0x5) | (0xa << 20) | (0xa << 16) | (0xa << 12) },
	{ 736, 0x00000300 | (0x5) | (0xb << 20) | (0xb << 16) | (0xa << 12) },
	{ 768, 0x00000300 | (0x5) | (0xb << 20) | (0xb << 16) | (0xb << 12) },
	{ 800, 0x00000300 | (0x5) | (0xc << 20) | (0xc << 16) | (0xb << 12) },
	{ 832, 0x00000300 | (0x5) | (0xc << 20) | (0xc << 16) | (0xc << 12) },
	{ 864, 0x00000300 | (0x5) | (0xd << 20) | (0xd << 16) | (0xc << 12) },
	{ 896, 0x00000300 | (0x5) | (0xd << 20) | (0xd << 16) | (0xd << 12) },
	{ 928, 0x00000300 | (0x5) | (0xe << 20) | (0xe << 16) | (0xd << 12) },
	{ 960, 0x00000300 | (0x5) | (0xe << 20) | (0xe << 16) | (0xe << 12) },
	{ 992, 0x00000300 | (0x5) | (0xf << 20) | (0xf << 16) | (0xe << 12) },
	{ 1024, 0x00000300 | (0x5) | (0xf << 20) | (0xf << 16) | (0xf << 12) },

	{ 1088, 0x00000300 | (0x6) | (0x8 << 20) | (0x8 << 16) | (0x7 << 12) },
	{ 1152, 0x00000300 | (0x6) | (0x8 << 20) | (0x8 << 16) | (0x8 << 12) },
	{ 1216, 0x00000300 | (0x6) | (0x9 << 20) | (0x9 << 16) | (0x8 << 12) },
	{ 1280, 0x00000300 | (0x6) | (0x9 << 20) | (0x9 << 16) | (0x9 << 12) },
	{ 1344, 0x00000300 | (0x6) | (0xa << 20) | (0xa << 16) | (0x9 << 12) },
	{ 1408, 0x00000300 | (0x6) | (0xa << 20) | (0xa << 16) | (0xa << 12) },
	{ 1472, 0x00000300 | (0x6) | (0xb << 20) | (0xb << 16) | (0xa << 12) },
	{ 1536, 0x00000300 | (0x6) | (0xb << 20) | (0xb << 16) | (0xb << 12) },
	{ 1600, 0x00000300 | (0x6) | (0xc << 20) | (0xc << 16) | (0xb << 12) },
	{ 1664, 0x00000300 | (0x6) | (0xc << 20) | (0xc << 16) | (0xc << 12) },
	{ 1728, 0x00000300 | (0x6) | (0xd << 20) | (0xd << 16) | (0xc << 12) },
	{ 1792, 0x00000300 | (0x6) | (0xd << 20) | (0xd << 16) | (0xd << 12) },
	{ 1856, 0x00000300 | (0x6) | (0xe << 20) | (0xe << 16) | (0xd << 12) },
	{ 1920, 0x00000300 | (0x6) | (0xe << 20) | (0xe << 16) | (0xe << 12) },
	{ 1984, 0x00000300 | (0x6) | (0xf << 20) | (0xf << 16) | (0xe << 12) },
	{ 2048, 0x00000300 | (0x6) | (0xf << 20) | (0xf << 16) | (0xf << 12) },

	{ 2176, 0x00000300 | (0x7) | (0x8 << 20) | (0x8 << 16) | (0x7 << 12) },
	{ 2304, 0x00000300 | (0x7) | (0x8 << 20) | (0x8 << 16) | (0x8 << 12) },
	{ 2432, 0x00000300 | (0x7) | (0x9 << 20) | (0x9 << 16) | (0x8 << 12) },
	{ 2560, 0x00000300 | (0x7) | (0x9 << 20) | (0x9 << 16) | (0x9 << 12) },
	{ 2688, 0x00000300 | (0x7) | (0xa << 20) | (0xa << 16) | (0x9 << 12) },
	{ 2816, 0x00000300 | (0x7) | (0xa << 20) | (0xa << 16) | (0xa << 12) },
	{ 2944, 0x00000300 | (0x7) | (0xb << 20) | (0xb << 16) | (0xa << 12) },
	{ 3072, 0x00000300 | (0x7) | (0xb << 20) | (0xb << 16) | (0xb << 12) },
};

enum xfer_mode {
	BYTE_MODE = 0,
	BUFF_MODE,
	DMA_MODE,
};

struct ast2600_i2c_bus {
	struct i2c_adapter	adap;
	struct device		*dev;
	void __iomem		*reg_base;
	struct regmap		*global_reg;
	int					irq;
	/* 0: dma, 1: pool, 2:byte */
	enum xfer_mode		mode;
	/* 0: old mode, 1: new mode */
	int					clk_div_mode;
	struct clk			*clk;
	u32					apb_clk;
	u32					bus_frequency;
	/*I2C xfer mode state matchine */
	u32					state;
	int					timeout_enable;
	/* smbus alert */
	int					alert_enable;
	struct i2c_smbus_alert_setup alert_data;
	struct i2c_client *ara;
	/* Multi-master */
	bool				multi_master;
	/* master structure */
	int					cmd_err;
	struct completion	cmd_complete;
	struct i2c_msg		*msgs;	/* cur xfer msgs */
	size_t				buf_index;	/* buffer mode idx */
	/* cur xfer msgs index*/
	int					msgs_index;
	int					msgs_count;	/* total msgs */
	dma_addr_t			master_dma_addr;
	/*total xfer count */
	int					master_xfer_cnt;
	/* Buffer mode */
	void __iomem		*buf_base;
	size_t				buf_size;
	/* Slave structure */
	int					slave_xfer_len;
	int					slave_xfer_cnt;
#ifdef CONFIG_I2C_SLAVE
	unsigned char		*slave_dma_buf;
	dma_addr_t			slave_dma_addr;
	struct i2c_client	*slave;
#endif
};

static u32 ast2600_select_i2c_clock(struct ast2600_i2c_bus *i2c_bus)
{
	unsigned long base_clk1;
	unsigned long base_clk2;
	unsigned long base_clk3;
	unsigned long base_clk4;
	int baseclk_idx;
	u32 clk_div_reg;
	u32 scl_low;
	u32 scl_high;
	int divisor;
	int inc = 0;
	u32 data;
	int i;

	if (i2c_bus->clk_div_mode) {
		regmap_read(i2c_bus->global_reg, AST2600_I2CG_CLK_DIV_CTRL, &clk_div_reg);
		base_clk1 = (i2c_bus->apb_clk * 10) / ((((clk_div_reg & 0xff) + 2) * 10) / 2);
		base_clk2 = (i2c_bus->apb_clk * 10) /
				(((((clk_div_reg >> 8) & 0xff) + 2) * 10) / 2);
		base_clk3 = (i2c_bus->apb_clk * 10) /
				(((((clk_div_reg >> 16) & 0xff) + 2) * 10) / 2);
		base_clk4 = (i2c_bus->apb_clk * 10) /
				(((((clk_div_reg >> 24) & 0xff) + 2) * 10) / 2);

		if ((i2c_bus->apb_clk / i2c_bus->bus_frequency) <= 32) {
			baseclk_idx = 0;
			divisor = DIV_ROUND_UP(i2c_bus->apb_clk, i2c_bus->bus_frequency);
		} else if ((base_clk1 / i2c_bus->bus_frequency) <= 32) {
			baseclk_idx = 1;
			divisor = DIV_ROUND_UP(base_clk1, i2c_bus->bus_frequency);
		} else if ((base_clk2 / i2c_bus->bus_frequency) <= 32) {
			baseclk_idx = 2;
			divisor = DIV_ROUND_UP(base_clk2, i2c_bus->bus_frequency);
		} else if ((base_clk3 / i2c_bus->bus_frequency) <= 32) {
			baseclk_idx = 3;
			divisor = DIV_ROUND_UP(base_clk3, i2c_bus->bus_frequency);
		} else {
			baseclk_idx = 4;
			divisor = DIV_ROUND_UP(base_clk4, i2c_bus->bus_frequency);
			inc = 0;
			while ((divisor + inc) > 32) {
				inc |= divisor & 0x1;
				divisor >>= 1;
				baseclk_idx++;
			}
			divisor += inc;
		}
		divisor = min_t(int, divisor, 32);
		baseclk_idx &= 0xf;
		scl_low = ((divisor * 9) / 16) - 1;
		scl_low = min_t(u32, scl_low, 0xf);
		scl_high = (divisor - scl_low - 2) & 0xf;
		/* Divisor : Base Clock : tCKHighMin : tCK High : tCK Low  */
		data = ((scl_high - 1) << 20) | (scl_high << 16) | (scl_low << 12) | (baseclk_idx);
		if (i2c_bus->timeout_enable) {
			data |= AST2600_I2CC_TOUTBASECLK(AST_I2C_TIMEOUT_CLK);
			data |= AST2600_I2CC_TTIMEOUT(AST_I2C_TIMEOUT_COUNT);
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(i2c_legacy_timing_table); i++) {
			if ((i2c_bus->apb_clk / i2c_legacy_timing_table[i].divisor) <
			    i2c_bus->bus_frequency) {
				break;
			}
		}
		data = i2c_legacy_timing_table[i].timing;
	}
	return data;
}

static u8 ast2600_i2c_recover_bus(struct ast2600_i2c_bus *i2c_bus)
{
	int ret = 0;
	u32 ctrl;
	u32 state;
	int r;

	dev_dbg(i2c_bus->dev, "%d-bus recovery bus [%x]\n", i2c_bus->adap.nr,
		readl(i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF));

	ctrl = readl(i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);

	writel(ctrl & ~(AST2600_I2CC_MASTER_EN | AST2600_I2CC_SLAVE_EN),
	       i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);

	writel(readl(i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL) | AST2600_I2CC_MASTER_EN,
	       i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);

	reinit_completion(&i2c_bus->cmd_complete);
	i2c_bus->cmd_err = 0;

	//Check 0x14's SDA and SCL status
	state = readl(i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF);
	if (!(state & AST2600_I2CC_SDA_LINE_STS) && (state & AST2600_I2CC_SCL_LINE_STS)) {
		writel(AST2600_I2CM_RECOVER_CMD_EN, i2c_bus->reg_base + AST2600_I2CM_CMD_STS);
		r = wait_for_completion_timeout(&i2c_bus->cmd_complete, i2c_bus->adap.timeout);
		if (r == 0) {
			dev_dbg(i2c_bus->dev, "recovery timed out\n");
			ret = -ETIMEDOUT;
		} else {
			if (i2c_bus->cmd_err) {
				dev_dbg(i2c_bus->dev, "recovery error\n");
				ret = -EPROTO;
			}
		}
	}

	dev_dbg(i2c_bus->dev, "Recovery done [%x]\n",
		readl(i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF));
	if (readl(i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF) & AST2600_I2CC_BUS_BUSY_STS) {
		dev_dbg(i2c_bus->dev, "Can't recovery bus [%x]\n",
			readl(i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF));
	}

	writel(ctrl, i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);
#ifdef CONFIG_I2C_SLAVE
	if (ctrl & AST2600_I2CC_SLAVE_EN) {
		u32 cmd = SLAVE_TRIGGER_CMD;

		if (i2c_bus->mode == DMA_MODE) {
			cmd |= AST2600_I2CS_RX_DMA_EN;
			writel(i2c_bus->slave_dma_addr, i2c_bus->reg_base + AST2600_I2CS_RX_DMA);
			writel(i2c_bus->slave_dma_addr, i2c_bus->reg_base + AST2600_I2CS_TX_DMA);
			writel(AST2600_I2CS_SET_RX_DMA_LEN(I2C_SLAVE_MSG_BUF_SIZE),
			       i2c_bus->reg_base + AST2600_I2CS_DMA_LEN);
		} else if (i2c_bus->mode == BUFF_MODE) {
			cmd |= AST2600_I2CS_RX_BUFF_EN;
			writel(AST2600_I2CC_SET_RX_BUF_LEN(i2c_bus->buf_size),
			       i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
		} else {
			cmd &= ~AST2600_I2CS_PKT_MODE_EN;
		}
		writel(cmd, i2c_bus->reg_base + AST2600_I2CS_CMD_STS);
	}
#endif
	return ret;
}

#ifdef CONFIG_I2C_SLAVE
static void ast2600_i2c_slave_packet_irq(struct ast2600_i2c_bus *i2c_bus, u32 sts)
{
	int slave_rx_len;
	u8 byte_data;
	u32 cmd = 0;
	u8 value;
	int i = 0;

	//clear irq fisrt
	writel(AST2600_I2CS_PKT_DONE, i2c_bus->reg_base + AST2600_I2CS_ISR);
	readl(i2c_bus->reg_base + AST2600_I2CS_ISR);

	/* Handle i2c slave timeout condition */
	if (AST2600_I2CS_INACTIVE_TO & sts) {
		cmd = SLAVE_TRIGGER_CMD;
		/* Set slave mode */
		if (i2c_bus->mode == DMA_MODE) {
			cmd |= AST2600_I2CS_RX_DMA_EN;
			writel(AST2600_I2CS_SET_RX_DMA_LEN(I2C_SLAVE_MSG_BUF_SIZE),
			       i2c_bus->reg_base + AST2600_I2CS_DMA_LEN);
		} else if (i2c_bus->mode == BUFF_MODE) {
			cmd |= AST2600_I2CS_RX_BUFF_EN;
			writel(AST2600_I2CC_SET_RX_BUF_LEN(i2c_bus->buf_size),
			       i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
		} else {
			cmd &= ~AST2600_I2CS_PKT_MODE_EN;
		}
		writel(cmd, i2c_bus->reg_base + AST2600_I2CS_CMD_STS);
		i2c_slave_event(i2c_bus->slave, I2C_SLAVE_STOP, &value);
		return;
	}

	sts &= ~(AST2600_I2CS_PKT_DONE | AST2600_I2CS_PKT_ERROR);

	switch (sts) {
	case AST2600_I2CS_SLAVE_MATCH:
	case AST2600_I2CS_SLAVE_MATCH | AST2600_I2CS_RX_DONE:
		i2c_slave_event(i2c_bus->slave, I2C_SLAVE_WRITE_REQUESTED, &value);
		break;
	case AST2600_I2CS_SLAVE_MATCH | AST2600_I2CS_RX_DONE | AST2600_I2CS_WAIT_RX_DMA:
	case AST2600_I2CS_SLAVE_MATCH | AST2600_I2CS_WAIT_RX_DMA:
		i2c_slave_event(i2c_bus->slave, I2C_SLAVE_WRITE_REQUESTED, &value);
		cmd = SLAVE_TRIGGER_CMD;
		if (i2c_bus->mode == DMA_MODE) {
			cmd |= AST2600_I2CS_RX_DMA_EN;
			writel(AST2600_I2CS_SET_RX_DMA_LEN(I2C_SLAVE_MSG_BUF_SIZE),
			       i2c_bus->reg_base + AST2600_I2CS_DMA_LEN);
		} else if (i2c_bus->mode == BUFF_MODE) {
			cmd |= AST2600_I2CS_RX_BUFF_EN;
			writel(AST2600_I2CC_SET_RX_BUF_LEN(i2c_bus->buf_size),
			       i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
		} else {
			cmd &= ~AST2600_I2CS_PKT_MODE_EN;
		}
		writel(cmd, i2c_bus->reg_base + AST2600_I2CS_CMD_STS);
		break;
	case AST2600_I2CS_SLAVE_MATCH | AST2600_I2CS_STOP:
		i2c_slave_event(i2c_bus->slave, I2C_SLAVE_STOP, &value);
		cmd = SLAVE_TRIGGER_CMD;
		if (i2c_bus->mode == DMA_MODE) {
			cmd |= AST2600_I2CS_RX_DMA_EN;
			writel(AST2600_I2CS_SET_RX_DMA_LEN(I2C_SLAVE_MSG_BUF_SIZE),
			       i2c_bus->reg_base + AST2600_I2CS_DMA_LEN);
		} else if (i2c_bus->mode == BUFF_MODE) {
			cmd |= AST2600_I2CS_RX_BUFF_EN;
			writel(AST2600_I2CC_SET_RX_BUF_LEN(i2c_bus->buf_size),
			       i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
		} else {
			cmd &= ~AST2600_I2CS_PKT_MODE_EN;
		}
		writel(cmd, i2c_bus->reg_base + AST2600_I2CS_CMD_STS);
		break;
	case AST2600_I2CS_SLAVE_MATCH | AST2600_I2CS_RX_DONE_NAK | AST2600_I2CS_RX_DONE | AST2600_I2CS_STOP:
	case AST2600_I2CS_RX_DONE_NAK | AST2600_I2CS_RX_DONE | AST2600_I2CS_STOP:
	case AST2600_I2CS_RX_DONE | AST2600_I2CS_WAIT_RX_DMA | AST2600_I2CS_STOP:
	case AST2600_I2CS_RX_DONE | AST2600_I2CS_STOP:
	case AST2600_I2CS_WAIT_RX_DMA | AST2600_I2CS_SLAVE_MATCH | AST2600_I2CS_RX_DONE | AST2600_I2CS_STOP:
	case AST2600_I2CS_SLAVE_MATCH | AST2600_I2CS_RX_DONE | AST2600_I2CS_STOP:
		cmd = SLAVE_TRIGGER_CMD;
		if (sts & AST2600_I2CS_SLAVE_MATCH)
			i2c_slave_event(i2c_bus->slave, I2C_SLAVE_WRITE_REQUESTED, &value);

		if (i2c_bus->mode == DMA_MODE) {
			cmd |= AST2600_I2CS_RX_DMA_EN;
			slave_rx_len = AST2600_I2C_GET_RX_DMA_LEN(readl(i2c_bus->reg_base +
							      AST2600_I2CS_DMA_LEN_STS));
			for (i = 0; i < slave_rx_len; i++) {
				i2c_slave_event(i2c_bus->slave, I2C_SLAVE_WRITE_RECEIVED,
						&i2c_bus->slave_dma_buf[i]);
			}
			writel(AST2600_I2CS_SET_RX_DMA_LEN(I2C_SLAVE_MSG_BUF_SIZE),
			       i2c_bus->reg_base + AST2600_I2CS_DMA_LEN);
		} else if (i2c_bus->mode == BUFF_MODE) {
			cmd |= AST2600_I2CS_RX_BUFF_EN;
			slave_rx_len = AST2600_I2CC_GET_RX_BUF_LEN(readl(i2c_bus->reg_base +
							       AST2600_I2CC_BUFF_CTRL));
			for (i = 0; i < slave_rx_len; i++) {
				value = readb(i2c_bus->buf_base + i);
				i2c_slave_event(i2c_bus->slave, I2C_SLAVE_WRITE_RECEIVED, &value);
			}
			writel(AST2600_I2CC_SET_RX_BUF_LEN(i2c_bus->buf_size),
			       i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
		} else {
			cmd &= ~AST2600_I2CS_PKT_MODE_EN;
			byte_data = AST2600_I2CC_GET_RX_BUFF(readl(i2c_bus->reg_base +
							 AST2600_I2CC_STS_AND_BUFF));
			i2c_slave_event(i2c_bus->slave, I2C_SLAVE_WRITE_RECEIVED, &byte_data);
		}
		if (sts & AST2600_I2CS_STOP)
			i2c_slave_event(i2c_bus->slave, I2C_SLAVE_STOP, &value);
		writel(cmd, i2c_bus->reg_base + AST2600_I2CS_CMD_STS);
		break;

	/* it is Mw data Mr coming -> it need send tx */
	case AST2600_I2CS_RX_DONE | AST2600_I2CS_WAIT_TX_DMA:
	case AST2600_I2CS_SLAVE_MATCH | AST2600_I2CS_RX_DONE | AST2600_I2CS_WAIT_TX_DMA:
		/* it should be repeat start read */
		if (sts & AST2600_I2CS_SLAVE_MATCH)
			i2c_slave_event(i2c_bus->slave, I2C_SLAVE_WRITE_REQUESTED, &value);

		cmd = SLAVE_TRIGGER_CMD;
		if (i2c_bus->mode == DMA_MODE) {
			cmd |= AST2600_I2CS_TX_DMA_EN;
			slave_rx_len = AST2600_I2C_GET_RX_DMA_LEN(readl(i2c_bus->reg_base +
							      AST2600_I2CS_DMA_LEN_STS));
			for (i = 0; i < slave_rx_len; i++) {
				i2c_slave_event(i2c_bus->slave, I2C_SLAVE_WRITE_RECEIVED,
						&i2c_bus->slave_dma_buf[i]);
			}
			i2c_slave_event(i2c_bus->slave, I2C_SLAVE_READ_REQUESTED,
					&i2c_bus->slave_dma_buf[0]);
			writel(0, i2c_bus->reg_base + AST2600_I2CS_DMA_LEN_STS);
			writel(AST2600_I2CS_SET_TX_DMA_LEN(1),
					i2c_bus->reg_base + AST2600_I2CS_DMA_LEN);
		} else if (i2c_bus->mode == BUFF_MODE) {
			cmd |= AST2600_I2CS_TX_BUFF_EN;
			slave_rx_len = AST2600_I2CC_GET_RX_BUF_LEN(
							readl(i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL));
			for (i = 0; i < slave_rx_len; i++) {
				value = readb(i2c_bus->buf_base + i);
				i2c_slave_event(i2c_bus->slave, I2C_SLAVE_WRITE_RECEIVED, &value);
			}
			i2c_slave_event(i2c_bus->slave, I2C_SLAVE_READ_REQUESTED, &value);
			writeb(value, i2c_bus->buf_base);
			writel(AST2600_I2CC_SET_TX_BUF_LEN(1),
					i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
		} else {
			cmd &= ~AST2600_I2CS_PKT_MODE_EN;
			cmd |= AST2600_I2CS_TX_CMD;
			byte_data = AST2600_I2CC_GET_RX_BUFF(readl(i2c_bus->reg_base +
							 AST2600_I2CC_STS_AND_BUFF));
			i2c_slave_event(i2c_bus->slave, I2C_SLAVE_WRITE_RECEIVED, &byte_data);
			i2c_slave_event(i2c_bus->slave, I2C_SLAVE_READ_REQUESTED, &byte_data);
			writel(byte_data, i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF);
		}
		writel(cmd, i2c_bus->reg_base + AST2600_I2CS_CMD_STS);
		break;

	case AST2600_I2CS_SLAVE_MATCH | AST2600_I2CS_WAIT_TX_DMA:
		/* First Start read */
		cmd = SLAVE_TRIGGER_CMD;
		if (i2c_bus->mode == DMA_MODE) {
			cmd |= AST2600_I2CS_TX_DMA_EN;
			i2c_slave_event(i2c_bus->slave, I2C_SLAVE_READ_REQUESTED,
					&i2c_bus->slave_dma_buf[0]);
			writel(AST2600_I2CS_SET_TX_DMA_LEN(1),
					i2c_bus->reg_base + AST2600_I2CS_DMA_LEN);
		} else if (i2c_bus->mode == BUFF_MODE) {
			cmd |= AST2600_I2CS_TX_BUFF_EN;
			i2c_slave_event(i2c_bus->slave, I2C_SLAVE_READ_REQUESTED, &byte_data);
			writeb(byte_data, i2c_bus->buf_base);
			writel(AST2600_I2CC_SET_TX_BUF_LEN(1),
					i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
		} else {
			cmd &= ~AST2600_I2CS_PKT_MODE_EN;
			cmd |= AST2600_I2CS_TX_CMD;
			i2c_slave_event(i2c_bus->slave, I2C_SLAVE_READ_REQUESTED, &byte_data);
			writel(byte_data, i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF);
		}
		writel(cmd, i2c_bus->reg_base + AST2600_I2CS_CMD_STS);
		break;

	case AST2600_I2CS_WAIT_TX_DMA:
		/* it should be next start read */
		cmd = SLAVE_TRIGGER_CMD;
		if (i2c_bus->mode == DMA_MODE) {
			cmd |= AST2600_I2CS_TX_DMA_EN;
			i2c_slave_event(i2c_bus->slave, I2C_SLAVE_READ_PROCESSED,
					&i2c_bus->slave_dma_buf[0]);
			writel(0, i2c_bus->reg_base + AST2600_I2CS_DMA_LEN_STS);
			writel(AST2600_I2CS_SET_TX_DMA_LEN(1),
					i2c_bus->reg_base + AST2600_I2CS_DMA_LEN);
		} else if (i2c_bus->mode == BUFF_MODE) {
			cmd |= AST2600_I2CS_TX_BUFF_EN;
			i2c_slave_event(i2c_bus->slave, I2C_SLAVE_READ_PROCESSED, &value);
			writeb(value, i2c_bus->buf_base);
			writel(AST2600_I2CC_SET_TX_BUF_LEN(1),
					i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
		} else {
			cmd &= ~AST2600_I2CS_PKT_MODE_EN;
			cmd |= AST2600_I2CS_TX_CMD;
			i2c_slave_event(i2c_bus->slave, I2C_SLAVE_READ_PROCESSED, &byte_data);
			writel(byte_data, i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF);
		}
		writel(cmd, i2c_bus->reg_base + AST2600_I2CS_CMD_STS);
		break;

	case AST2600_I2CS_TX_NAK | AST2600_I2CS_STOP:
		/* it just tx complete */
		cmd = SLAVE_TRIGGER_CMD;
		i2c_slave_event(i2c_bus->slave, I2C_SLAVE_STOP, &value);
		if (i2c_bus->mode == DMA_MODE) {
			cmd |= AST2600_I2CS_RX_DMA_EN;
			writel(0, i2c_bus->reg_base + AST2600_I2CS_DMA_LEN_STS);
			writel(AST2600_I2CS_SET_RX_DMA_LEN(I2C_SLAVE_MSG_BUF_SIZE),
			       i2c_bus->reg_base + AST2600_I2CS_DMA_LEN);
		} else if (i2c_bus->mode == BUFF_MODE) {
			cmd |= AST2600_I2CS_RX_BUFF_EN;
			writel(AST2600_I2CC_SET_RX_BUF_LEN(i2c_bus->buf_size),
			       i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
		} else {
			cmd &= ~AST2600_I2CS_PKT_MODE_EN;
		}
		writel(cmd, i2c_bus->reg_base + AST2600_I2CS_CMD_STS);
		break;

	default:
		dev_dbg(i2c_bus->dev, "todo slave isr case %x, sts %x\n", sts,
			readl(i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF));
		break;
	}

}

static void ast2600_i2c_slave_byte_irq(struct ast2600_i2c_bus *i2c_bus, u32 sts)
{
	u32 i2c_buff = readl(i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF);
	u32 cmd = AST2600_I2CS_ACTIVE_ALL;
	u8 byte_data;
	u8 value;

	switch (sts) {
	case AST2600_I2CS_SLAVE_MATCH | AST2600_I2CS_RX_DONE | AST2600_I2CS_WAIT_RX_DMA:
		dev_dbg(i2c_bus->dev, "S : Sw|D\n");
		i2c_slave_event(i2c_bus->slave, I2C_SLAVE_WRITE_REQUESTED, &value);
		/* first address match is address */
		byte_data = AST2600_I2CC_GET_RX_BUFF(i2c_buff);
		dev_dbg(i2c_bus->dev, "addr [%x]", byte_data);
		break;
	case AST2600_I2CS_RX_DONE | AST2600_I2CS_WAIT_RX_DMA:
		dev_dbg(i2c_bus->dev, "S : D\n");
		byte_data = AST2600_I2CC_GET_RX_BUFF(i2c_buff);
		dev_dbg(i2c_bus->dev, "rx [%x]", byte_data);
		i2c_slave_event(i2c_bus->slave, I2C_SLAVE_WRITE_RECEIVED, &byte_data);
		break;
	case AST2600_I2CS_SLAVE_MATCH | AST2600_I2CS_RX_DONE | AST2600_I2CS_WAIT_TX_DMA:
		cmd |= AST2600_I2CS_TX_CMD;
		dev_dbg(i2c_bus->dev, "S : Sr|D\n");
		byte_data = AST2600_I2CC_GET_RX_BUFF(i2c_buff);
		dev_dbg(i2c_bus->dev, "addr : [%02x]", byte_data);
		i2c_slave_event(i2c_bus->slave, I2C_SLAVE_READ_REQUESTED, &byte_data);
		dev_dbg(i2c_bus->dev, "tx: [%02x]\n", byte_data);
		writel(byte_data, i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF);
		break;
	case AST2600_I2CS_TX_ACK | AST2600_I2CS_WAIT_TX_DMA:
		cmd |= AST2600_I2CS_TX_CMD;
		dev_dbg(i2c_bus->dev, "S : D\n");
		i2c_slave_event(i2c_bus->slave, I2C_SLAVE_READ_PROCESSED, &byte_data);
		dev_dbg(i2c_bus->dev, "tx: [%02x]\n", byte_data);
		writel(byte_data, i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF);
		break;
	case AST2600_I2CS_STOP:
	case AST2600_I2CS_STOP | AST2600_I2CS_TX_NAK:
		dev_dbg(i2c_bus->dev, "S : P\n");
		i2c_slave_event(i2c_bus->slave, I2C_SLAVE_STOP, &value);
		break;
	default:
		dev_dbg(i2c_bus->dev, "todo no pkt isr %x\n", sts);
		break;
	}
	writel(cmd, i2c_bus->reg_base + AST2600_I2CS_CMD_STS);
	writel(sts, i2c_bus->reg_base + AST2600_I2CS_ISR);
	readl(i2c_bus->reg_base + AST2600_I2CS_ISR);
}

static int ast2600_i2c_slave_irq(struct ast2600_i2c_bus *i2c_bus)
{
	u32 ier = readl(i2c_bus->reg_base + AST2600_I2CS_IER);
	u32 isr = readl(i2c_bus->reg_base + AST2600_I2CS_ISR);

	if (!(isr & ier))
		return 0;
	dev_dbg(i2c_bus->dev, "isr %x\n", isr);

	isr &= ~(AST2600_I2CS_ADDR_INDICATE_MASK | AST2600_I2CS_SLAVE_PENDING);

	if (AST2600_I2CS_ADDR1_NAK & isr)
		isr &= ~AST2600_I2CS_ADDR1_NAK;

	if (AST2600_I2CS_ADDR2_NAK & isr)
		isr &= ~AST2600_I2CS_ADDR2_NAK;

	if (AST2600_I2CS_ADDR3_NAK & isr)
		isr &= ~AST2600_I2CS_ADDR3_NAK;

	if (AST2600_I2CS_ADDR_MASK & isr)
		isr &= ~AST2600_I2CS_ADDR_MASK;

	if (AST2600_I2CS_PKT_DONE & isr)
		ast2600_i2c_slave_packet_irq(i2c_bus, isr);
	else
		ast2600_i2c_slave_byte_irq(i2c_bus, isr);

	return 1;
}
#endif

static void ast2600_i2c_do_start(struct ast2600_i2c_bus *i2c_bus)
{
	struct i2c_msg *msg = &i2c_bus->msgs[i2c_bus->msgs_index];
	int xfer_len = 0;
	int i = 0;
	u32 cmd;

	cmd = AST2600_I2CM_PKT_EN | AST2600_I2CM_PKT_ADDR(msg->addr) | AST2600_I2CM_START_CMD;

	/* send start */
	dev_dbg(i2c_bus->dev, "[%d] %sing %d byte%s %s 0x%02x\n",
		i2c_bus->msgs_index, msg->flags & I2C_M_RD ? "read" : "write",
		msg->len, msg->len > 1 ? "s" : "",
		msg->flags & I2C_M_RD ? "from" : "to", msg->addr);

	i2c_bus->master_xfer_cnt = 0;
	i2c_bus->buf_index = 0;

	if (msg->flags & I2C_M_RD) {
		cmd |= AST2600_I2CM_RX_CMD;
		if (i2c_bus->mode == DMA_MODE) {
			/* dma mode */
			cmd |= AST2600_I2CM_RX_DMA_EN;

			if (msg->flags & I2C_M_RECV_LEN) {
				dev_dbg(i2c_bus->dev, "smbus read\n");
				xfer_len = 1;
			} else {
				if (msg->len > AST2600_I2C_DMA_SIZE) {
					xfer_len = AST2600_I2C_DMA_SIZE;
				} else {
					xfer_len = msg->len;
					if (i2c_bus->msgs_index + 1 == i2c_bus->msgs_count) {
						dev_dbg(i2c_bus->dev, "last stop\n");
						cmd |= MASTER_TRIGGER_LAST_STOP;
					}
				}
			}
			writel(AST2600_I2CM_SET_RX_DMA_LEN(xfer_len - 1),
			       i2c_bus->reg_base + AST2600_I2CM_DMA_LEN);
			i2c_bus->master_dma_addr =
				dma_map_single(i2c_bus->dev, msg->buf, msg->len, DMA_FROM_DEVICE);
			writel(i2c_bus->master_dma_addr, i2c_bus->reg_base + AST2600_I2CM_RX_DMA);
		} else if (i2c_bus->mode == BUFF_MODE) {
			/* buff mode */
			cmd |= AST2600_I2CM_RX_BUFF_EN;
			if (msg->flags & I2C_M_RECV_LEN) {
				dev_dbg(i2c_bus->dev, "smbus read\n");
				xfer_len = 1;
			} else {
				if (msg->len > i2c_bus->buf_size) {
					xfer_len = i2c_bus->buf_size;
				} else {
					xfer_len = msg->len;
					if (i2c_bus->msgs_index + 1 == i2c_bus->msgs_count) {
						dev_dbg(i2c_bus->dev, "last stop\n");
						cmd |= MASTER_TRIGGER_LAST_STOP;
					}
				}
			}
			writel(AST2600_I2CC_SET_RX_BUF_LEN(xfer_len),
			       i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
		} else {
			/* byte mode */
			xfer_len = 1;
			if (msg->flags & I2C_M_RECV_LEN) {
				dev_dbg(i2c_bus->dev, "smbus read\n");
			} else {
				if (i2c_bus->msgs_index + 1 == i2c_bus->msgs_count) {
					if (msg->len == 1) {
						dev_dbg(i2c_bus->dev, "last stop\n");
						cmd |= MASTER_TRIGGER_LAST_STOP;
					}
				}
			}
		}
	} else {
		if (i2c_bus->mode == DMA_MODE) {
			/* dma mode */
			if (msg->len > AST2600_I2C_DMA_SIZE) {
				xfer_len = AST2600_I2C_DMA_SIZE;
			} else {
				if (i2c_bus->msgs_index + 1 == i2c_bus->msgs_count) {
					dev_dbg(i2c_bus->dev, "with stop\n");
					cmd |= AST2600_I2CM_STOP_CMD;
				}
				xfer_len = msg->len;
			}

			if (xfer_len) {
				cmd |= AST2600_I2CM_TX_DMA_EN | AST2600_I2CM_TX_CMD;
				writel(AST2600_I2CM_SET_TX_DMA_LEN(xfer_len - 1),
				       i2c_bus->reg_base + AST2600_I2CM_DMA_LEN);
				i2c_bus->master_dma_addr =
					dma_map_single(i2c_bus->dev, msg->buf, msg->len,
						       DMA_TO_DEVICE);
				writel(i2c_bus->master_dma_addr,
				       i2c_bus->reg_base + AST2600_I2CM_TX_DMA);
			}
		} else if (i2c_bus->mode == BUFF_MODE) {
			u8 wbuf[4];
			/* buff mode */
			if (msg->len > i2c_bus->buf_size) {
				xfer_len = i2c_bus->buf_size;
			} else {
				if (i2c_bus->msgs_index + 1 == i2c_bus->msgs_count) {
					dev_dbg(i2c_bus->dev, "with stop\n");
					cmd |= AST2600_I2CM_STOP_CMD;
				}
				xfer_len = msg->len;
			}
			if (xfer_len) {
				cmd |= AST2600_I2CM_TX_BUFF_EN | AST2600_I2CM_TX_CMD;
				writel(AST2600_I2CC_SET_TX_BUF_LEN(xfer_len),
				       i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
				for (i = 0; i < xfer_len; i++) {
					wbuf[i % 4] = msg->buf[i];
					if (i % 4 == 3)
						writel(*(u32 *)wbuf, i2c_bus->buf_base + i - 3);
					dev_dbg(i2c_bus->dev, "[%02x]\n", msg->buf[i]);
				}
				if (--i % 4 != 3)
					writel(*(u32 *)wbuf, i2c_bus->buf_base + i - (i % 4));
			}
		} else {
			/* byte mode */
			if ((i2c_bus->msgs_index + 1 == i2c_bus->msgs_count) && (msg->len <= 1)) {
				dev_dbg(i2c_bus->dev, "with stop\n");
				cmd |= AST2600_I2CM_STOP_CMD;
			}

			if (msg->len) {
				cmd |= AST2600_I2CM_TX_CMD;
				xfer_len = 1;
				dev_dbg(i2c_bus->dev, "w [0] : %02x\n", msg->buf[0]);
				writel(msg->buf[0], i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF);
			} else {
				xfer_len = 0;
			}
		}
	}
	dev_dbg(i2c_bus->dev, "len %d , cmd %x\n", xfer_len, cmd);
	writel(cmd, i2c_bus->reg_base + AST2600_I2CM_CMD_STS);
}

static int ast2600_i2c_is_irq_error(u32 irq_status)
{
	if (irq_status & AST2600_I2CM_ARBIT_LOSS)
		return -EAGAIN;
	if (irq_status & (AST2600_I2CM_SDA_DL_TO | AST2600_I2CM_SCL_LOW_TO))
		return -EBUSY;
	if (irq_status & (AST2600_I2CM_ABNORMAL))
		return -EPROTO;

	return 0;
}

static void ast2600_i2c_master_package_irq(struct ast2600_i2c_bus *i2c_bus, u32 sts)
{
	struct i2c_msg *msg = &i2c_bus->msgs[i2c_bus->msgs_index];
	u32 cmd = AST2600_I2CM_PKT_EN;
	int xfer_len;
	int i;

	sts &= ~AST2600_I2CM_PKT_DONE;
	writel(AST2600_I2CM_PKT_DONE, i2c_bus->reg_base + AST2600_I2CM_ISR);
	switch (sts) {
	case AST2600_I2CM_PKT_ERROR:
		dev_dbg(i2c_bus->dev, "M : ERROR only\n");
		i2c_bus->cmd_err = -EAGAIN;
		complete(&i2c_bus->cmd_complete);
		break;
	case AST2600_I2CM_PKT_ERROR | AST2600_I2CM_TX_NAK: /* a0 fix for issue */
		fallthrough;
	case AST2600_I2CM_PKT_ERROR | AST2600_I2CM_TX_NAK | AST2600_I2CM_NORMAL_STOP:
		dev_dbg(i2c_bus->dev, "M : TX NAK | NORMAL STOP\n");
		i2c_bus->cmd_err = -ENXIO;
		complete(&i2c_bus->cmd_complete);
		break;
	case AST2600_I2CM_NORMAL_STOP:
		/* write 0 byte only have stop isr */
		dev_dbg(i2c_bus->dev, "M clear isr: AST2600_I2CM_NORMAL_STOP = %x\n", sts);
		i2c_bus->msgs_index++;
		if (i2c_bus->msgs_index < i2c_bus->msgs_count) {
			ast2600_i2c_do_start(i2c_bus);
		} else {
			i2c_bus->cmd_err = i2c_bus->msgs_index;
			complete(&i2c_bus->cmd_complete);
		}
		break;
	case AST2600_I2CM_TX_ACK:
		//dev_dbg(i2c_bus->dev, "M : AST2600_I2CM_TX_ACK = %x\n", sts);
	case AST2600_I2CM_TX_ACK | AST2600_I2CM_NORMAL_STOP:
		dev_dbg(i2c_bus->dev,
			"M : AST2600_I2CM_TX_ACK | AST2600_I2CM_NORMAL_STOP= %x\n", sts);

		if (i2c_bus->mode == DMA_MODE)
			xfer_len = AST2600_I2C_GET_TX_DMA_LEN(readl(i2c_bus->reg_base +
							  AST2600_I2CM_DMA_LEN_STS));
		else if (i2c_bus->mode == BUFF_MODE)
			xfer_len = AST2600_I2CC_GET_TX_BUF_LEN(readl(i2c_bus->reg_base +
							   AST2600_I2CC_BUFF_CTRL));
		else
			xfer_len = 1;
		i2c_bus->master_xfer_cnt += xfer_len;

		if (i2c_bus->master_xfer_cnt == msg->len) {
			if (i2c_bus->mode == DMA_MODE)
				dma_unmap_single(i2c_bus->dev, i2c_bus->master_dma_addr, msg->len,
						 DMA_TO_DEVICE);

			i2c_bus->msgs_index++;
			if (i2c_bus->msgs_index == i2c_bus->msgs_count) {
				i2c_bus->cmd_err = i2c_bus->msgs_index;
				complete(&i2c_bus->cmd_complete);
			} else {
				ast2600_i2c_do_start(i2c_bus);
			}
		} else {
			/* do next tx */
			cmd |= AST2600_I2CM_TX_CMD;
			if (i2c_bus->mode == DMA_MODE) {
				cmd |= AST2600_I2CM_TX_DMA_EN;
				xfer_len = msg->len - i2c_bus->master_xfer_cnt;
				if (xfer_len > AST2600_I2C_DMA_SIZE) {
					xfer_len = AST2600_I2C_DMA_SIZE;
				} else {
					if (i2c_bus->msgs_index + 1 == i2c_bus->msgs_count) {
						dev_dbg(i2c_bus->dev, "M: STOP\n");
						cmd |= AST2600_I2CM_STOP_CMD;
					}
				}
				writel(AST2600_I2CM_SET_TX_DMA_LEN(xfer_len - 1),
				       i2c_bus->reg_base + AST2600_I2CM_DMA_LEN);
				dev_dbg(i2c_bus->dev, "next tx xfer_len: %d, offset %d\n",
					xfer_len, i2c_bus->master_xfer_cnt);
				writel(i2c_bus->master_dma_addr + i2c_bus->master_xfer_cnt,
				       i2c_bus->reg_base + AST2600_I2CM_TX_DMA);
			} else if (i2c_bus->mode == BUFF_MODE) {
				u8 wbuf[4];

				cmd |= AST2600_I2CM_RX_BUFF_EN;
				xfer_len = msg->len - i2c_bus->master_xfer_cnt;
				if (xfer_len > i2c_bus->buf_size) {
					xfer_len = i2c_bus->buf_size;
				} else {
					if (i2c_bus->msgs_index + 1 == i2c_bus->msgs_count) {
						dev_dbg(i2c_bus->dev, "M: STOP\n");
						cmd |= AST2600_I2CM_STOP_CMD;
					}
				}
				for (i = 0; i < xfer_len; i++) {
					wbuf[i % 4] = msg->buf[i2c_bus->master_xfer_cnt + i];
					if (i % 4 == 3)
						writel(*(u32 *)wbuf, i2c_bus->buf_base + i - 3);
					dev_dbg(i2c_bus->dev, "[%02x]\n",
						msg->buf[i2c_bus->master_xfer_cnt + i]);
				}
				if (--i % 4 != 3)
					writel(*(u32 *)wbuf, i2c_bus->buf_base + i - (i % 4));

				writel(AST2600_I2CC_SET_TX_BUF_LEN(xfer_len),
				       i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
			} else {
				/* byte */
				if ((i2c_bus->msgs_index + 1 == i2c_bus->msgs_count) &&
				    ((i2c_bus->master_xfer_cnt + 1) == msg->len)) {
					dev_dbg(i2c_bus->dev, "M: STOP\n");
					cmd |= AST2600_I2CM_STOP_CMD;
				}
				dev_dbg(i2c_bus->dev, "tx buff[%x]\n",
					msg->buf[i2c_bus->master_xfer_cnt]);
				writel(msg->buf[i2c_bus->master_xfer_cnt],
				       i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF);
			}
			dev_dbg(i2c_bus->dev, "next tx cmd: %x\n", cmd);
			writel(cmd, i2c_bus->reg_base + AST2600_I2CM_CMD_STS);
		}
		break;
	case AST2600_I2CM_RX_DONE:
#ifdef CONFIG_I2C_SLAVE
		/* Workaround for master/slave package mode enable rx done stuck issue
		 * When master go for first read (RX_DONE), slave mode will also effect
		 * Then controller will send nack, not operate anymore.
		 */
		if (readl(i2c_bus->reg_base + AST2600_I2CS_CMD_STS) & AST2600_I2CS_PKT_MODE_EN) {
			u32 slave_cmd = readl(i2c_bus->reg_base + AST2600_I2CS_CMD_STS);

			writel(0, i2c_bus->reg_base + AST2600_I2CS_CMD_STS);
			writel(slave_cmd, i2c_bus->reg_base + AST2600_I2CS_CMD_STS);
		}
		fallthrough;
#endif
	case AST2600_I2CM_RX_DONE | AST2600_I2CM_NORMAL_STOP:
		dev_dbg(i2c_bus->dev,
			"M : AST2600_I2CM_RX_DONE | AST2600_I2CM_NORMAL_STOP = %x\n", sts);
		/* do next rx */
		if (i2c_bus->mode == DMA_MODE) {
			xfer_len = AST2600_I2C_GET_RX_DMA_LEN(readl(i2c_bus->reg_base +
							  AST2600_I2CM_DMA_LEN_STS));
		} else if (i2c_bus->mode == BUFF_MODE) {
			xfer_len = AST2600_I2CC_GET_RX_BUF_LEN(
						readl(i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL));
			for (i = 0; i < xfer_len; i++)
				msg->buf[i2c_bus->master_xfer_cnt + i] =
					readb(i2c_bus->buf_base + i);
		} else {
			xfer_len = 1;
			msg->buf[i2c_bus->master_xfer_cnt] =
				AST2600_I2CC_GET_RX_BUFF(readl(i2c_bus->reg_base +
						     AST2600_I2CC_STS_AND_BUFF));
		}

		if (msg->flags & I2C_M_RECV_LEN) {
			dev_dbg(i2c_bus->dev, "smbus first len = %x\n", msg->buf[0]);
			msg->len = msg->buf[0] + ((msg->flags & I2C_CLIENT_PEC) ? 2 : 1);
			msg->flags &= ~I2C_M_RECV_LEN;
		}
		i2c_bus->master_xfer_cnt += xfer_len;
		dev_dbg(i2c_bus->dev, "master_xfer_cnt [%d/%d]\n", i2c_bus->master_xfer_cnt,
			msg->len);

		if (i2c_bus->master_xfer_cnt == msg->len) {
			if (i2c_bus->mode == DMA_MODE)
				dma_unmap_single(i2c_bus->dev, i2c_bus->master_dma_addr, msg->len,
						 DMA_FROM_DEVICE);

			for (i = 0; i < msg->len; i++)
				dev_dbg(i2c_bus->dev, "M: r %d:[%x]\n", i, msg->buf[i]);
			i2c_bus->msgs_index++;
			if (i2c_bus->msgs_index == i2c_bus->msgs_count) {
				i2c_bus->cmd_err = i2c_bus->msgs_index;
				complete(&i2c_bus->cmd_complete);
			} else {
				ast2600_i2c_do_start(i2c_bus);
			}
		} else {
			/* next rx */
			cmd |= AST2600_I2CM_RX_CMD;
			if (i2c_bus->mode == DMA_MODE) {
				cmd |= AST2600_I2CM_RX_DMA_EN;
				xfer_len = msg->len - i2c_bus->master_xfer_cnt;
				if (xfer_len > AST2600_I2C_DMA_SIZE) {
					xfer_len = AST2600_I2C_DMA_SIZE;
				} else {
					if (i2c_bus->msgs_index + 1 == i2c_bus->msgs_count) {
						dev_dbg(i2c_bus->dev, "last stop\n");
						cmd |= MASTER_TRIGGER_LAST_STOP;
					}
				}
				dev_dbg(i2c_bus->dev, "M: next rx len [%d/%d] , cmd %x\n", xfer_len,
					msg->len, cmd);
				writel(AST2600_I2CM_SET_RX_DMA_LEN(xfer_len - 1),
				       i2c_bus->reg_base + AST2600_I2CM_DMA_LEN);
				writel(i2c_bus->master_dma_addr + i2c_bus->master_xfer_cnt,
				       i2c_bus->reg_base + AST2600_I2CM_RX_DMA);
			} else if (i2c_bus->mode == BUFF_MODE) {
				cmd |= AST2600_I2CM_RX_BUFF_EN;
				xfer_len = msg->len - i2c_bus->master_xfer_cnt;
				if (xfer_len > i2c_bus->buf_size) {
					xfer_len = i2c_bus->buf_size;
				} else {
					if (i2c_bus->msgs_index + 1 == i2c_bus->msgs_count) {
						dev_dbg(i2c_bus->dev, "last stop\n");
						cmd |= MASTER_TRIGGER_LAST_STOP;
					}
				}
				writel(AST2600_I2CC_SET_RX_BUF_LEN(xfer_len),
				       i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
			} else {
				if ((i2c_bus->msgs_index + 1 == i2c_bus->msgs_count) &&
				    ((i2c_bus->master_xfer_cnt + 1) == msg->len)) {
					dev_dbg(i2c_bus->dev, "last stop\n");
					cmd |= MASTER_TRIGGER_LAST_STOP;
				}
			}
			dev_dbg(i2c_bus->dev, "M: next rx len %d, cmd %x\n", xfer_len, cmd);
			writel(cmd, i2c_bus->reg_base + AST2600_I2CM_CMD_STS);
		}
		break;
	default:
		dev_dbg(i2c_bus->dev, "todo care sts %x\n", sts);
		break;
	}
}

static int ast2600_i2c_master_irq(struct ast2600_i2c_bus *i2c_bus)
{
	u32 sts = readl(i2c_bus->reg_base + AST2600_I2CM_ISR);
	u32 ier = readl(i2c_bus->reg_base + AST2600_I2CM_IER);
	u32 ctrl = 0;

	dev_dbg(i2c_bus->dev, "M sts %x\n", sts);
	if (!i2c_bus->alert_enable)
		sts &= ~AST2600_I2CM_SMBUS_ALT;

	if (AST2600_I2CM_BUS_RECOVER_FAIL & sts) {
		dev_dbg(i2c_bus->dev, "M clear isr: AST2600_I2CM_BUS_RECOVER_FAIL= %x\n", sts);
		writel(AST2600_I2CM_BUS_RECOVER_FAIL, i2c_bus->reg_base + AST2600_I2CM_ISR);
		ctrl = readl(i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);
		writel(0, i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);
		writel(ctrl, i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);
		i2c_bus->cmd_err = -EPROTO;
		complete(&i2c_bus->cmd_complete);
		return 1;
	}

	if (AST2600_I2CM_BUS_RECOVER & sts) {
		dev_dbg(i2c_bus->dev, "M clear isr: AST2600_I2CM_BUS_RECOVER= %x\n", sts);
		writel(AST2600_I2CM_BUS_RECOVER, i2c_bus->reg_base + AST2600_I2CM_ISR);
		i2c_bus->cmd_err = 0;
		complete(&i2c_bus->cmd_complete);
		return 1;
	}

	if (AST2600_I2CM_SMBUS_ALT & sts) {
		if (ier & AST2600_I2CM_SMBUS_ALT) {
			dev_dbg(i2c_bus->dev, "M clear isr: AST2600_I2CM_SMBUS_ALT= %x\n", sts);
			/* Disable ALT INT */
			writel(ier & ~AST2600_I2CM_SMBUS_ALT, i2c_bus->reg_base + AST2600_I2CM_IER);
			i2c_handle_smbus_alert(i2c_bus->ara);
			writel(AST2600_I2CM_SMBUS_ALT, i2c_bus->reg_base + AST2600_I2CM_ISR);
			dev_err(i2c_bus->dev,
				"ast2600_master_alert_recv bus id %d, Disable Alt, Please Imple\n",
				i2c_bus->adap.nr);
			return 1;
		}
	}

	i2c_bus->cmd_err = ast2600_i2c_is_irq_error(sts);
	if (i2c_bus->cmd_err) {
		dev_dbg(i2c_bus->dev, "received error interrupt: 0x%02x\n", sts);
		writel(AST2600_I2CM_PKT_DONE, i2c_bus->reg_base + AST2600_I2CM_ISR);
		complete(&i2c_bus->cmd_complete);
		return 1;
	}

	if (AST2600_I2CM_PKT_DONE & sts) {
		ast2600_i2c_master_package_irq(i2c_bus, sts);
		return 1;
	}

	if (readl(i2c_bus->reg_base + AST2600_I2CM_ISR)) {
		dev_dbg(i2c_bus->dev, "master TODO care sts %x\n",
			readl(i2c_bus->reg_base + AST2600_I2CM_ISR));
		writel(readl(i2c_bus->reg_base + AST2600_I2CM_ISR),
				i2c_bus->reg_base + AST2600_I2CM_ISR);
	}

	return 0;
}

static irqreturn_t ast2600_i2c_bus_irq(int irq, void *dev_id)
{
	struct ast2600_i2c_bus *i2c_bus = dev_id;

#ifdef CONFIG_I2C_SLAVE
	if (readl(i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL) & AST2600_I2CC_SLAVE_EN) {
		if (ast2600_i2c_slave_irq(i2c_bus)) {
//			dev_dbg(i2c_bus->dev, "bus-%d.slave handle\n", i2c_bus->adap.nr);
			return IRQ_HANDLED;
		}
	}
#endif
	return ast2600_i2c_master_irq(i2c_bus) ? IRQ_HANDLED : IRQ_NONE;
}

static int ast2600_i2c_master_xfer(struct i2c_adapter *adap,
				      struct i2c_msg *msgs, int num)
{
	struct ast2600_i2c_bus *i2c_bus = i2c_get_adapdata(adap);
	unsigned long timeout;

	/* If bus is busy in a single master environment, attempt recovery. */
	if (!i2c_bus->multi_master &&
	    (readl(i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF) & AST2600_I2CC_BUS_BUSY_STS)) {
		int ret;

		ret = ast2600_i2c_recover_bus(i2c_bus);
		if (ret)
			return ret;
	}

	i2c_bus->cmd_err = 0;
	i2c_bus->msgs = msgs;
	i2c_bus->msgs_index = 0;
	i2c_bus->msgs_count = num;
	dev_dbg(i2c_bus->dev, "master_xfer msg cnt %d sts[%x]\n", num, readl(i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF));
	reinit_completion(&i2c_bus->cmd_complete);
	ast2600_i2c_do_start(i2c_bus);
	timeout = wait_for_completion_timeout(&i2c_bus->cmd_complete, i2c_bus->adap.timeout);
	if (timeout == 0) {
		u32 isr = readl(i2c_bus->reg_base + AST2600_I2CM_ISR);
		u32 i2c_status = readl(i2c_bus->reg_base + AST2600_I2CC_STS_AND_BUFF);

		dev_dbg(i2c_bus->dev, "timeout isr[%x], sts[%x]\n", isr, i2c_status);
		if (isr || (i2c_status & AST2600_I2CC_TX_DIR_MASK)) {
			u32 ctrl = readl(i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);

			writel(0, i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);
			writel(ctrl, i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);
#ifdef CONFIG_I2C_SLAVE
			if (ctrl & AST2600_I2CC_SLAVE_EN) {
				u32 cmd = SLAVE_TRIGGER_CMD;

				if (i2c_bus->mode == DMA_MODE) {
					cmd |= AST2600_I2CS_RX_DMA_EN;
					writel(i2c_bus->slave_dma_addr,
						   i2c_bus->reg_base + AST2600_I2CS_RX_DMA);
					writel(i2c_bus->slave_dma_addr,
						   i2c_bus->reg_base + AST2600_I2CS_TX_DMA);
					writel(AST2600_I2CS_SET_RX_DMA_LEN(I2C_SLAVE_MSG_BUF_SIZE),
						   i2c_bus->reg_base + AST2600_I2CS_DMA_LEN);
				} else if (i2c_bus->mode == BUFF_MODE) {
					cmd |= AST2600_I2CS_RX_BUFF_EN;
					writel(AST2600_I2CC_SET_RX_BUF_LEN(i2c_bus->buf_size),
						   i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
				} else {
					cmd &= ~AST2600_I2CS_PKT_MODE_EN;
				}
				dev_dbg(i2c_bus->dev, "slave trigger [%x]\n", cmd);
				writel(cmd, i2c_bus->reg_base + AST2600_I2CS_CMD_STS);
			}
#endif
		}
		return -ETIMEDOUT;
	}

	dev_dbg(i2c_bus->dev, "bus%d-m: %d end\n", i2c_bus->adap.nr, i2c_bus->cmd_err);

	return i2c_bus->cmd_err;
}

static void ast2600_i2c_init(struct ast2600_i2c_bus *i2c_bus)
{
	struct platform_device *pdev = to_platform_device(i2c_bus->dev);
	u32 fun_ctrl = AST2600_I2CC_BUS_AUTO_RELEASE | AST2600_I2CC_MASTER_EN;

	/* I2C Reset */
	writel(0, i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);

	if (of_property_read_bool(pdev->dev.of_node, "multi-master"))
		i2c_bus->multi_master = true;
	else
		fun_ctrl |= AST2600_I2CC_MULTI_MASTER_DIS;

	/* Enable Master Mode */
	writel(fun_ctrl, i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);
	/* disable slave address */
	writel(0, i2c_bus->reg_base + AST2600_I2CS_ADDR_CTRL);

	/* Set AC Timing */
	writel(ast2600_select_i2c_clock(i2c_bus), i2c_bus->reg_base + AST2600_I2CC_AC_TIMING);

	/* Clear Interrupt */
	writel(0xfffffff, i2c_bus->reg_base + AST2600_I2CM_ISR);

#ifdef CONFIG_I2C_SLAVE
	/* for memory buffer initial */
	if (i2c_bus->mode == DMA_MODE) {
		i2c_bus->slave_dma_buf = dma_alloc_coherent(i2c_bus->dev, I2C_SLAVE_MSG_BUF_SIZE,
							    &i2c_bus->slave_dma_addr, GFP_KERNEL);
		if (!i2c_bus->slave_dma_buf)
			return;
	}

	writel(0xfffffff, i2c_bus->reg_base + AST2600_I2CS_ISR);

	if (i2c_bus->mode == BYTE_MODE) {
		writel(0xffff, i2c_bus->reg_base + AST2600_I2CS_IER);
	} else {
		/* Set interrupt generation of I2C slave controller */
		writel(AST2600_I2CS_PKT_DONE, i2c_bus->reg_base + AST2600_I2CS_IER);
	}
#endif
}

#ifdef CONFIG_I2C_SLAVE
static int ast2600_i2c_reg_slave(struct i2c_client *client)
{
	struct ast2600_i2c_bus *i2c_bus = i2c_get_adapdata(client->adapter);
	u32 cmd = SLAVE_TRIGGER_CMD;

	if (i2c_bus->slave)
		return -EINVAL;

	dev_dbg(i2c_bus->dev, "slave addr %x\n", client->addr);

	writel(0, i2c_bus->reg_base + AST2600_I2CS_ADDR_CTRL);
	writel(AST2600_I2CC_SLAVE_EN | readl(i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL),
	       i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);

	/* trigger rx buffer */
	if (i2c_bus->mode == DMA_MODE) {
		cmd |= AST2600_I2CS_RX_DMA_EN;
		writel(i2c_bus->slave_dma_addr, i2c_bus->reg_base + AST2600_I2CS_RX_DMA);
		writel(i2c_bus->slave_dma_addr, i2c_bus->reg_base + AST2600_I2CS_TX_DMA);
		writel(AST2600_I2CS_SET_RX_DMA_LEN(I2C_SLAVE_MSG_BUF_SIZE),
		       i2c_bus->reg_base + AST2600_I2CS_DMA_LEN);
	} else if (i2c_bus->mode == BUFF_MODE) {
		cmd |= AST2600_I2CS_RX_BUFF_EN;
		writel(AST2600_I2CC_SET_RX_BUF_LEN(i2c_bus->buf_size),
		       i2c_bus->reg_base + AST2600_I2CC_BUFF_CTRL);
	} else {
		cmd &= ~AST2600_I2CS_PKT_MODE_EN;
	}

	writel(cmd, i2c_bus->reg_base + AST2600_I2CS_CMD_STS);
	i2c_bus->slave = client;
	/* Set slave addr. */
	writel(client->addr | AST2600_I2CS_ADDR1_ENABLE,
			i2c_bus->reg_base + AST2600_I2CS_ADDR_CTRL);

	return 0;
}

static int ast2600_i2c_unreg_slave(struct i2c_client *slave)
{
	struct ast2600_i2c_bus *i2c_bus = i2c_get_adapdata(slave->adapter);

	WARN_ON(!i2c_bus->slave);

	/* Turn off slave mode. */
	writel(~AST2600_I2CC_SLAVE_EN & readl(i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL),
	       i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);
	writel(readl(i2c_bus->reg_base + AST2600_I2CS_ADDR_CTRL) & ~AST2600_I2CS_ADDR1_MASK,
	       i2c_bus->reg_base + AST2600_I2CS_ADDR_CTRL);

	i2c_bus->slave = NULL;

	return 0;
}
#endif

static u32 ast2600_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL | I2C_FUNC_SMBUS_BLOCK_DATA;
}

static const struct i2c_algorithm i2c_ast2600_algorithm = {
	.master_xfer = ast2600_i2c_master_xfer,
#if IS_ENABLED(CONFIG_I2C_SLAVE)
	.reg_slave = ast2600_i2c_reg_slave,
	.unreg_slave = ast2600_i2c_unreg_slave,
#endif
	.functionality = ast2600_i2c_functionality,
};

static const struct of_device_id ast2600_i2c_bus_of_table[] = {
	{
		.compatible = "aspeed,ast2600-i2c-bus",
	},
	{}
};

MODULE_DEVICE_TABLE(of, ast2600_i2c_bus_of_table);

static int ast2600_i2c_probe(struct platform_device *pdev)
{
	struct ast2600_i2c_bus *i2c_bus;
	const struct of_device_id *match;
	struct resource *res;
	u32 global_ctrl;
	int ret = 0;

	i2c_bus = devm_kzalloc(&pdev->dev, sizeof(*i2c_bus), GFP_KERNEL);
	if (!i2c_bus)
		return -ENOMEM;

	i2c_bus->global_reg = syscon_regmap_lookup_by_compatible("aspeed,ast2600-i2c-global");
	if (IS_ERR(i2c_bus->global_reg)) {
		dev_err(&pdev->dev, "failed to find ast2600 i2c global regmap\n");
		ret = -ENOMEM;
		goto free_mem;
	}

	/* get global control register */
	regmap_read(i2c_bus->global_reg, AST2600_I2CG_CTRL, &global_ctrl);

	if (global_ctrl & AST2600_I2CG_CTRL_NEW_CLK_DIV)
		i2c_bus->clk_div_mode = 1;

	if (!(global_ctrl & AST2600_I2CG_CTRL_NEW_REG)) {
		ret = -ENOENT;
		/* this driver only supports new reg mode. */
		dev_err(&pdev->dev, "Expect I2CG0C[2] = 1 (new reg mode)\n");
		goto free_mem;
	}

	i2c_bus->mode = DMA_MODE;

	if (of_property_read_bool(pdev->dev.of_node, "byte-mode"))
		i2c_bus->mode = BYTE_MODE;

	if (of_property_read_bool(pdev->dev.of_node, "buff-mode")) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
		if (res && resource_size(res) >= 2)
			i2c_bus->buf_base = devm_ioremap_resource(&pdev->dev, res);

		if (!IS_ERR_OR_NULL(i2c_bus->buf_base))
			i2c_bus->buf_size = resource_size(res);

		i2c_bus->mode = BUFF_MODE;
	}

	if (of_property_read_bool(pdev->dev.of_node, "timeout"))
		i2c_bus->timeout_enable = 1;

	i2c_bus->dev = &pdev->dev;
	init_completion(&i2c_bus->cmd_complete);

	i2c_bus->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(i2c_bus->reg_base)) {
		ret = PTR_ERR(i2c_bus->reg_base);
		goto free_mem;
	}

	i2c_bus->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (i2c_bus->irq < 0) {
		dev_err(&pdev->dev, "no irq specified\n");
		ret = -i2c_bus->irq;
		goto free_irq;
	}

	match = of_match_node(ast2600_i2c_bus_of_table, pdev->dev.of_node);
	if (!match) {
		ret = -ENOENT;
		goto free_irq;
	}

	platform_set_drvdata(pdev, i2c_bus);

	i2c_bus->clk = devm_clk_get(i2c_bus->dev, NULL);
	if (IS_ERR(i2c_bus->clk)) {
		dev_err(i2c_bus->dev, "no clock defined\n");
		ret = -ENODEV;
		goto free_irq;
	}
	i2c_bus->apb_clk = clk_get_rate(i2c_bus->clk);
	dev_dbg(i2c_bus->dev, "i2c_bus->apb_clk %d\n", i2c_bus->apb_clk);

	ret = of_property_read_u32(pdev->dev.of_node, "bus-frequency", &i2c_bus->bus_frequency);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not read bus-frequency property\n");
		i2c_bus->bus_frequency = 100000;
	}

	/* Initialize the I2C adapter */
	i2c_bus->adap.owner = THIS_MODULE;
	i2c_bus->adap.algo = &i2c_ast2600_algorithm;
	i2c_bus->adap.retries = 0;
	i2c_bus->adap.dev.parent = i2c_bus->dev;
	i2c_bus->adap.dev.of_node = pdev->dev.of_node;
	i2c_bus->adap.algo_data = i2c_bus;
	strscpy(i2c_bus->adap.name, pdev->name, sizeof(i2c_bus->adap.name));
	i2c_set_adapdata(&i2c_bus->adap, i2c_bus);

	ast2600_i2c_init(i2c_bus);

	ret = devm_request_irq(&pdev->dev, i2c_bus->irq, ast2600_i2c_bus_irq, 0,
			       dev_name(&pdev->dev), i2c_bus);
	if (ret < 0)
		goto unmap;

	if (of_property_read_bool(pdev->dev.of_node, "smbus-alert")) {
		i2c_bus->alert_enable = 1;
		i2c_bus->ara = i2c_new_smbus_alert_device(&i2c_bus->adap, &i2c_bus->alert_data);
		if (!i2c_bus->ara)
			dev_warn(i2c_bus->dev, "Failed to register ARA client\n");

		writel(AST2600_I2CM_PKT_DONE | AST2600_I2CM_BUS_RECOVER | AST2600_I2CM_SMBUS_ALT,
		       i2c_bus->reg_base + AST2600_I2CM_IER);
	} else {
		i2c_bus->alert_enable = 0;
		/* Set interrupt generation of I2C master controller */
		writel(AST2600_I2CM_PKT_DONE | AST2600_I2CM_BUS_RECOVER,
				i2c_bus->reg_base + AST2600_I2CM_IER);
	}

	ret = i2c_add_adapter(&i2c_bus->adap);
	if (ret < 0)
		goto unmap;

	dev_info(i2c_bus->dev, "%s [%d]: adapter [%d khz] mode [%d]\n",
		 pdev->dev.of_node->name, i2c_bus->adap.nr, i2c_bus->bus_frequency / 1000,
		 i2c_bus->mode);

	return 0;

unmap:
	free_irq(i2c_bus->irq, i2c_bus);
free_irq:
	devm_iounmap(&pdev->dev, i2c_bus->reg_base);
free_mem:
	kfree(i2c_bus);
	return ret;
}

static int ast2600_i2c_remove(struct platform_device *pdev)
{
	struct ast2600_i2c_bus *i2c_bus = platform_get_drvdata(pdev);

	/* Disable everything. */
	writel(0, i2c_bus->reg_base + AST2600_I2CC_FUN_CTRL);
	writel(0, i2c_bus->reg_base + AST2600_I2CM_IER);

	free_irq(i2c_bus->irq, i2c_bus);

	platform_set_drvdata(pdev, NULL);
	i2c_del_adapter(&i2c_bus->adap);

	kfree(i2c_bus);

	return 0;
}

static struct platform_driver ast2600_i2c_bus_driver = {
	.probe = ast2600_i2c_probe,
	.remove = ast2600_i2c_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = ast2600_i2c_bus_of_table,
	},
};
module_platform_driver(ast2600_i2c_bus_driver);

MODULE_AUTHOR("Ryan Chen <ryan_chen@aspeedtech.com>");
MODULE_DESCRIPTION("ASPEED AST2600 I2C Controller Driver");
MODULE_LICENSE("GPL");
