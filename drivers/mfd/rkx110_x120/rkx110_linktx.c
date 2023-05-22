// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Zhang Yubing <yubing.zhang@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/mfd/core.h>
#include <dt-bindings/mfd/rockchip-serdes.h>

#include "hal/cru_api.h"
#include "hal/pinctrl_api.h"
#include "rkx110_x120.h"
#include "rkx110_reg.h"

#define LINK_REG(x)			((x) + RKX110_SER_RKLINK_BASE)

#define RKLINK_TX_SERDES_CTRL		LINK_REG(0x0000)

#define VIDEO_CH0_EN_MASK		BIT(31)
#define VIDEO_CH1_EN_MASK		BIT(30)
#define STOP_AUDIO			BIT(18)
#define STOP_VIDEO_CH1			BIT(17)
#define STOP_VIDEO_CH0			BIT(16)
#define CH1_LVDS_SEL_EN			BIT(15)
#define TRAIN_CLK_SEL_MASK		GENMASK(14, 13)
#define TRAIN_CLK_SEL_CH0		UPDATE(0, 14, 13)
#define TRAIN_CLK_SEL_CH1		UPDATE(1, 14, 13)
#define TRAIN_CLK_SEL_I2S		UPDATE(2, 14, 13)

#define STREAM_TYPE_MASK		BIT(12)

#define SER_STREAM_CAMERA		UPDATE(0, 12, 12)
#define SER_STREAM_DISPLAY		UPDATE(1, 12, 12)

#define VIDEO_EN			BIT(11)
#define SERDES_LANE_SWAP_EN		BIT(10)
#define SERDES_MIRROR_EN		BIT(9)

#define SER_CH1_EN			BIT(8)

#define CH1_DSOURCE_ID(x)		UPDATE(x, 7, 6)
#define CH0_DSOURCE_ID(x)		UPDATE(x, 5, 4)

#define SERDES_DATA_WIDTH_MASK		GENMASK(3, 2)
#define SERDES_DATA_WIDTH_8BIT		UPDATE(0, 3, 2)
#define SERDES_DATA_WIDTH_16BIT		UPDATE(1, 3, 2)
#define SERDES_DATA_WIDTH_24BIT		UPDATE(2, 3, 2)
#define SERDES_DATA_WIDTH_32BIT		UPDATE(3, 3, 2)

#define SERDES_DUAL_LANE_EN		BIT(1)
#define SER_EN				BIT(0)

#define RKLINK_TX_VIDEO_CTRL		LINK_REG(0x0004)
#define VIDEO_REPKT_LENGTH_MASK		GENMASK(29, 16)
#define VIDEO_REPKT_LENGTH(x)		UPDATE(x, 29, 16)
#define DUAL_LVDS_CYCLE_DIFF(x)		UPDATE(x, 13, 4)
#define PIXEL_VSYNC_SEL			BIT(3)
#define SOURCE_ID_MASK			UPDATE(7, 2, 0)
#define CAMERA_SRC_CSI			UPDATE(0, 2, 0)
#define CAMERA_SRC_LVDS			UPDATE(1, 2, 0)
#define CAMERA_SRC_DVP			UPDATE(2, 2, 0)
#define DISPLAY_SRC_DSI			UPDATE(0, 2, 0)
#define DISPLAY_SRC_DUAL_LDVS		UPDATE(1, 2, 0)
#define DISPLAY_SRC_LVDS0		UPDATE(2, 2, 0)
#define DISPLAY_SRC_LVDS1		UPDATE(3, 2, 0)
#define DISPLAY_SRC_RGB			UPDATE(5, 2, 0)

#define SER_RKLINK_DSI_REC0(x)		LINK_REG(0x0008 + 0x10 * x)
 #define DSI_REC_START			BIT(31)
 #define DSI_CMD_TYPE			BIT(30)
 #define DSI_HACT(x)			UPDATE(x, 29, 16)
 #define DSI_VACT(x)			UPDATE(x, 13, 0)

#define SER_RKLINK_DSI_REC1(x)		LINK_REG(0x000C + 0x10 * x)
 #define DSI_SPLIT_LR_SWAP		BIT(30)
#define DSI_FRAME_MODE(x)		UPDATE(x, 29, 28)
 #define DSI_HFP(x)			UPDATE(x, 27, 16)
 #define DSI_HBP(x)			UPDATE(x, 11, 0)

#define SER_RKLINK_DSI_REC2(x)		LINK_REG(0x0010 + 0x10 * x)
#define DSI_0_DST_MASK			GENMASK(31, 30)
#define DSI_0_DST(x)			UPDATE(x, 31, 30)
 #define DSI_CHANNEL_SWAP		UPDATE(2, 31, 30)
 #define DSI0_SPLIT_MODE		UPDATE(0, 31, 30)
 #define DSI1_SPLIT_MODE		UPDATE(3, 31, 30)
#define DSI_DST_VPORCH_MASK		GENMASK(29, 0)
 #define DSI_VFP(x)			UPDATE(x, 29, 20)
 #define DSI_VBP(x)			UPDATE(x, 19, 10)
 #define DSI_VSA(x)			UPDATE(x, 9, 0)

#define SER_RKLINK_DSI_REC3(x)		LINK_REG(0x0014 + 0x10 * x)
 #define DSI_DELAY_LENGTH_MASK		GENMASK(31, 12)
 #define DSI_DELAY_LENGTH(x)		UPDATE(x, 31, 12)
 #define DSI_HSA(x)			UPDATE(x, 11, 0)

#define SER_RKLINK_AUDIO_CRTL		LINK_REG(0x0028)
#define SER_RKLINK_AUDIO_CTRL		LINK_REG(0x0028)
#define SER_RKLINK_AUDIO_FDIV		LINK_REG(0x002C)
#define SER_RKLINK_AUDIO_LRCK		LINK_REG(0x0030)
#define SER_RKLINK_AUDIO_RECOVER	LINK_REG(0x0034)
#define SER_RKLINK_AUDIO_FM_STATUS	LINK_REG(0x0038)
#define SER_RKLINK_FIFO_STATUS		LINK_REG(0x003C)
 #define CH1_CMD_FIFO_UNDERRUN		BIT(24)
 #define CH0_CMD_FIFO_UNDERRUN		BIT(23)
 #define DATA1_FIFO_UNDERRUN		BIT(22)
 #define DATA0_FIFO_UNDERRUN		BIT(21)
 #define AUDIO_FIFO_UNDERRUN		BIT(20)
 #define LVDS1_FIFO_UNDERRUN		BIT(19)
 #define LVDS0_FIFO_UNDERRUN		BIT(18)
 #define DSI_CH1_FIFO_UNDERRUN		BIT(17)
 #define DSI_CH0_FIFO_UNDERRUN		BIT(16)
 #define CH1_CMD_FIFO_OVERFLOW		BIT(8)
 #define CH0_CMD_FIFO_OVERFLOW		BIT(7)
 #define DATA0_FIFO_OVERFLOW		BIT(6)
 #define DATA1_FIFO_OVERFLOW		BIT(5)
 #define AUDIO_FIFO_OVERFLOW		BIT(4)
 #define LVDS1_FIFO_OVERFLOW		BIT(3)
 #define LVDS0_FIFO_OVERFLOW		BIT(2)
 #define DSI_CH1_FIFO_OVERFLOW		BIT(1)
 #define DSI_CH0_FIFO_OVERFLOW		BIT(0)
#define SER_RKLINK_SOURCE_IRQ_EN	LINK_REG(0x0040)
 #define TRAIN_DONE_IRQ_FLAG		BIT(19)
 #define FIFO_UNDERRUN_IRQ_FLAG		BIT(18)
 #define FIFO_OVERFLOW_IRQ_FLAG		BIT(17)
 #define AUDIO_FM_IRQ_OUTPUT_FLAG	BIT(16)
 #define TRAIN_DONE_IRQ_OUTPUT_EN	BIT(3)
 #define FIFO_UNDERRUN_IRQ_OUTPUT_EN	BIT(2)
 #define FIFO_OVERFLOW_IRQ_OUTPUT_EN	BIT(1)
 #define AUDIO_FM_IRQ_OUTPUT_EN		BIT(0)

#define SER_RKLINK_TRAIN_CTRL		LINK_REG(0x0044)
#define SER_RKLINK_I2C_CFG		LINK_REG(0x00C0)
#define SER_RKLINK_SPI_CFG		LINK_REG(0x00C4)
#define SER_RKLINK_UART_CFG		LINK_REG(0x00C8)

#define SER_RKLINK_GPIO_CFG		LINK_REG(0x00CC)
 #define GPIO_GROUP1_EN			BIT(17)
 #define GPIO_GROUP0_EN			BIT(16)

#define SER_RKLINK_IO_DEF_INV_CFG	LINK_REG(0x00D0)

#define OTHER_LANE_ACTIVE(x)		HIWORD_UPDATE(x, 12, 12)
#define FORWARD_NON_ACK(x)		HIWORD_UPDATE(x, 11, 11)

#define SER_RKLINK_DISP_DSI_SPLIT	BIT(1)
#define SER_RKLINK_DISP_MIRROR		BIT(2)
#define SER_RKLINK_DISP_DUAL_CHANNEL	BIT(3)

#define PCS_REG(id, x)			((x) + RKX110_SER_PCS0_BASE + (id) * RKX110_SER_PCS_OFFSET)

#define PCS_REG00(id)			PCS_REG(id, 0x00)
 #define PCS_DUAL_LANE_MODE_EN		HIWORD_UPDATE(1, GENMASK(12, 12), 12)
 #define PCS_AUTO_START_EN		HIWORD_UPDATE(1, GENMASK(3, 3), 3)
 #define PCS_EN_MASK			HIWORD_MASK(2, 2)
 #define PCS_EN				HIWORD_UPDATE(1, GENMASK(2, 2), 2)
 #define PCS_DISABLE			HIWORD_UPDATE(0, GENMASK(2, 2), 2)
 #define PCS_ECU_MODE			HIWORD_UPDATE(0, GENMASK(2, 2), 1)
 #define PCS_REMOTE_MODE		HIWORD_UPDATE(1, GENMASK(2, 2), 1)
 #define PCS_SAFE_MODE_EN		HIWORD_UPDATE(1, GENMASK(0, 0), 0)

#define PCS_REG04(id)			PCS_REG(id, 0x04)
#define PCS_REG08(id)			PCS_REG(id, 0x08)
 #define VIDEO_SUS_EN(x)		HIWORD_UPDATE(x, GENMASK(15, 15), 15)
#define PCS_REG10(id)			PCS_REG(id, 0x10)
#define PCS_REG14(id)			PCS_REG(id, 0x14)
#define PCS_REG18(id)			PCS_REG(id, 0x18)
#define PCS_REG1C(id)			PCS_REG(id, 0x1C)
#define PCS_REG20(id)			PCS_REG(id, 0x20)
#define PCS_REG24(id)			PCS_REG(id, 0x24)
#define PCS_REG28(id)			PCS_REG(id, 0x28)
#define PCS_REG30(id)			PCS_REG(id, 0x30)
#define PCS_INT_STARTUP(x)		HIWORD_UPDATE(x, GENMASK(15, 0), 0)
#define PCS_REG34(id)			PCS_REG(id, 0x34)
#define PCS_INT_REMOTE_MODE(x)		HIWORD_UPDATE(x, GENMASK(15, 0), 0)
#define PCS_REG40(id)			PCS_REG(id, 0x40)

#define PMA_REG(id, x)			((x) + RKX110_SER_PMA0_BASE + (id) * RKX110_SER_PMA_OFFSET)

#define SER_PMA_STATUS(id)		PMA_REG(id, 0x00)
 #define SER_PMA_FORCE_INIT_STA		BIT(8)

#define SER_PMA_CTRL(id)		PMA_REG(id, 0x04)
 #define SER_PMA_FORCE_INIT_MASK	HIWORD_MASK(8, 8)
 #define SER_PMA_FORCE_INIT_EN		HIWORD_UPDATE(1, BIT(8), 8)
 #define SER_PMA_FORCE_INIT_DISABLE	HIWORD_UPDATE(0, BIT(8), 8)
 #define SER_PMA_DUAL_CHANNEL		HIWORD_UPDATE(1, BIT(3), 3)
 #define SER_PMA_INIT_CNT_CLR_MASK	HIWORD_MASK(2, 2)
 #define SER_PMA_INIT_CNT_CLR		HIWORD_UPDATE(1, BIT(2), 2)

#define SER_PMA_LOAD00(id)		PMA_REG(id, 0x10)
 #define PMA_RATE_MODE_MASK		HIWORD_MASK(2, 0)
 #define PMA_FDR_MODE			HIWORD_UPDATE(1, GENMASK(2, 0), 0)
 #define PMA_HDR_MODE			HIWORD_UPDATE(2, GENMASK(2, 0), 0)
 #define PMA_QDR_MODE			HIWORD_UPDATE(4, GENMASK(2, 0), 0)

#define SER_PMA_LOAD01(id)		PMA_REG(id, 0x14)
#define SER_PMA_LOAD02(id)		PMA_REG(id, 0x18)
#define SER_PMA_LOAD03(id)		PMA_REG(id, 0x1C)
#define SER_PMA_LOAD04(id)		PMA_REG(id, 0x20)
 #define PMA_PLL_DIV_MASK		HIWORD_MASK(14, 0)
 #define PLL_I_POST_DIV(x)		HIWORD_UPDATE(x, GENMASK(14, 10), 10)
 #define PLL_F_POST_DIV(x)		HIWORD_UPDATE(x, GENMASK(9, 0), 0)
 #define PMA_PLL_DIV(x)			HIWORD_UPDATE(x, GENMASK(14, 0), 0)

#define SER_PMA_LOAD05(id)		PMA_REG(id, 0x2C)
 #define PMA_PLL_REFCLK_DIV_MASK	HIWORD_MASK(3, 0)
 #define PMA_PLL_REFCLK_DIV(x)		HIWORD_UPDATE(x, GENMASK(3, 0), 0)

#define SER_PMA_LOAD06(id)		PMA_REG(id, 0x28)
#define SER_PMA_LOAD07(id)		PMA_REG(id, 0x2C)
#define SER_PMA_LOAD08(id)		PMA_REG(id, 0x30)
 #define PMA_FCK_VCO_MASK		HIWORD_MASK(15, 15)
 #define PMA_FCK_VCO			HIWORD_UPDATE(1, BIT(15), 15)
 #define PMA_FCK_VCO_DIV2		HIWORD_UPDATE(0, BIT(15), 15)

#define SER_PMA_LOAD09(id)		PMA_REG(id, 0x34)
 #define PMA_PLL_DIV4_MASK		HIWORD_MASK(12, 12)
 #define PMA_PLL_DIV4			HIWORD_UPDATE(1, GENMASK(12, 12), 12)
 #define PMA_PLL_DIV8			HIWORD_UPDATE(0, GENMASK(12, 12), 12)

#define SER_PMA_LOAD0A(id)		PMA_REG(id, 0x38)
 #define PMA_CLK_8X_DIV_MASK		HIWORD_MASK(7, 1)
 #define PMA_CLK_8X_DIV(x)		HIWORD_UPDATE(x, GENMASK(7, 1), 1)

#define SER_PMA_IRQ_EN(id)		PMA_REG(id, 0xF0)
 #define FORCE_INITIAL_IRQ_EN		HIWORD_UPDATE(1, BIT(3), 3)
 #define RTERM_ONCE_TIMEOUT_IRQ_EN	HIWORD_UPDATE(1, BIT(1), 1)
 #define PLL_LOCK_TIMEOUT_IRQ_EN	HIWORD_UPDATE(1, BIT(0), 0)
#define SER_PMA_IRQ_STATUS(id)		PMA_REG(id, 0xF4)
 #define FORCE_INITIAL_PULSE_STATUS	BIT(3)
 #define RTERM_ONCE_TIMEOUT_STATUS	BIT(1)
 #define PLL_LOCK_TIMEOUT_STATUS	BIT(0)

enum {
	SER_LINK_CH_ID0 = 0,
	SER_LINK_CH_ID1,
	SER_LINK_CH_ID2,
	SER_LINK_CH_ID3,
};

static const struct rk_serdes_pt ser_pt[] = {
	{
		/* gpi_gpo_0 */
		.en_reg = SER_RKLINK_GPIO_CFG,
		.en_mask = 0x10001,
		.en_val = 0x10001,
		.dir_reg = SER_RKLINK_GPIO_CFG,
		.dir_mask = 0x10,
		.dir_val = 0x10,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_SER_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_A5,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC6,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC5,
			},
		},
	}, {
		/* gpi_gpo_1 */
		.en_reg = SER_RKLINK_GPIO_CFG,
		.en_mask = 0x10002,
		.en_val = 0x10002,
		.dir_reg = SER_RKLINK_GPIO_CFG,
		.dir_mask = 0x20,
		.dir_val = 0x20,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_SER_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_A6,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC6,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC5,
			},
		},
	}, {
		/* gpi_gpo_2 */
		.en_reg = SER_RKLINK_GPIO_CFG,
		.en_mask = 0x10004,
		.en_val = 0x10004,
		.dir_reg = SER_RKLINK_GPIO_CFG,
		.dir_mask = 0x40,
		.dir_val = 0x40,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_SER_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_A7,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC6,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC5,
			},
		},
	}, {
		/* gpi_gpo_3 */
		.en_reg = SER_RKLINK_GPIO_CFG,
		.en_mask = 0x10008,
		.en_val = 0x10008,
		.dir_reg = SER_RKLINK_GPIO_CFG,
		.dir_mask = 0x80,
		.dir_val = 0x80,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_SER_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_B0,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC6,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC5,
			},
		},
	}, {
		/* gpi_gpo_4 */
		.en_reg = SER_RKLINK_GPIO_CFG,
		.en_mask = 0x20100,
		.en_val = 0x20100,
		.dir_reg = SER_RKLINK_GPIO_CFG,
		.dir_mask = 0x1000,
		.dir_val = 0x1000,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_SER_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_B3,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC2,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC3,
			},
		},
	}, {
		/* gpi_gpo_5 */
		.en_reg = SER_RKLINK_GPIO_CFG,
		.en_mask = 0x20200,
		.en_val = 0x20200,
		.dir_reg = SER_RKLINK_GPIO_CFG,
		.dir_mask = 0x2000,
		.dir_val = 0x2000,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_SER_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_B4,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC2,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC3,
			},
		},
	}, {
		/* gpi_gpo_6 */
		.en_reg = SER_RKLINK_GPIO_CFG,
		.en_mask = 0x20400,
		.en_val = 0x20400,
		.dir_reg = SER_RKLINK_GPIO_CFG,
		.dir_mask = 0x4000,
		.dir_val = 0x4000,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_SER_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_B5,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC2,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC3,
			},
		},
	}, {
		/* passthrough irq */
		.en_reg = SER_RKLINK_GPIO_CFG,
		.en_mask = 0x20800,
		.en_val = 0x20800,
		.dir_reg = SER_RKLINK_GPIO_CFG,
		.dir_mask = 0x8000,
		.dir_val = 0x8000,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_SER_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_A4,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC1,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC2,
			},
		},
	}, {
		/* passthrough uart0 */
		.en_reg = SER_RKLINK_UART_CFG,
		.en_mask = 0x1,
		.en_val = 0x1,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_SER_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_A5 | RK_SERDES_GPIO_PIN_A6,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC4,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC3,
			},
		},
	}, {
		/* passthrough uart1 */
		.en_reg = SER_RKLINK_UART_CFG,
		.en_mask = 0x2,
		.en_val = 0x2,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_SER_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_A7 | RK_SERDES_GPIO_PIN_B0,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC4,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC3,
			},
		},
	}, {
		/* passthrough spi */
		.en_reg = SER_RKLINK_SPI_CFG,
		.en_mask = 0x4,
		.en_val = 0x4,
		.dir_reg = SER_RKLINK_SPI_CFG,
		.dir_mask = 0x1,
		.dir_val = 0x0,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_SER_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_A5 | RK_SERDES_GPIO_PIN_A6 |
				       RK_SERDES_GPIO_PIN_A7 | RK_SERDES_GPIO_PIN_B0,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC2,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC1,
			},
		},
	},
};

static int rk_serdes_get_stream_source(struct rk_serdes *serdes, int local_port)
{
	if (serdes->stream_type == STREAM_DISPLAY) {
		if (local_port & RK_SERDES_RGB_RX)
			return DISPLAY_SRC_RGB;
		else if (local_port & RK_SERDES_LVDS_RX0)
			return DISPLAY_SRC_LVDS0;
		else if (local_port & RK_SERDES_LVDS_RX1)
			return DISPLAY_SRC_LVDS1;
		else if (local_port & RK_SERDES_DUAL_LVDS_RX)
			return DISPLAY_SRC_DUAL_LDVS;
		else if (local_port & RK_SERDES_DSI_RX0)
			return DISPLAY_SRC_DSI;
		else if (local_port & RK_SERDES_DSI_RX1)
			return DISPLAY_SRC_DSI;
	} else {
		return CAMERA_SRC_CSI;
	}

	return 0;
}

void rkx110_set_stream_source(struct rk_serdes *serdes, int local_port, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val, rx_src;

	serdes->i2c_read_reg(client, RKLINK_TX_VIDEO_CTRL, &val);
	val &= ~SOURCE_ID_MASK;
	rx_src = rk_serdes_get_stream_source(serdes, local_port);
	val |= rx_src;
	serdes->i2c_write_reg(client, RKLINK_TX_VIDEO_CTRL, val);
}

static int rkx110_linktx_ser_enable(struct rk_serdes *serdes, u8 dev_id, bool enable)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_update_bits(client, RKLINK_TX_SERDES_CTRL, SER_EN, enable ? SER_EN : 0);

	return 0;
}

static int rk110_linktx_dual_lane_enable(struct rk_serdes *serdes, u8 dev_id, bool enable)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_update_bits(client, RKLINK_TX_SERDES_CTRL, SERDES_DUAL_LANE_EN,
				enable ? SERDES_DUAL_LANE_EN : 0);

	return 0;
}

void rkx110_linktx_video_enable(struct rk_serdes *serdes, u8 dev_id, bool enable)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_update_bits(client, RKLINK_TX_SERDES_CTRL, VIDEO_EN, enable ? VIDEO_EN : 0);
}

static int rk110_linktx_dual_channel_enable(struct rk_serdes *serdes, u8 dev_id, bool enable)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_update_bits(client, RKLINK_TX_SERDES_CTRL, SER_CH1_EN,
				enable ? SER_CH1_EN : 0);

	return 0;
}

static int rk110_linktx_config_pkg_length(struct rk_serdes *serdes, u8 dev_id, u32 length)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_update_bits(client, RKLINK_TX_VIDEO_CTRL, VIDEO_REPKT_LENGTH_MASK,
				VIDEO_REPKT_LENGTH(length));

	return 0;
}

static int rk110_linktx_stream_type_cfg(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	if (serdes->stream_type == STREAM_DISPLAY)
		serdes->i2c_update_bits(client, RKLINK_TX_SERDES_CTRL, STREAM_TYPE_MASK,
					SER_STREAM_DISPLAY);
	else
		serdes->i2c_update_bits(client, RKLINK_TX_SERDES_CTRL, STREAM_TYPE_MASK,
					SER_STREAM_CAMERA);

	return 0;
}

static int rk110_linktx_replicate_enable(struct rk_serdes *serdes, u8 dev_id, bool enable)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_update_bits(client, RKLINK_TX_SERDES_CTRL, SERDES_MIRROR_EN,
				enable ? SERDES_MIRROR_EN : 0);

	return 0;
}

static int rkx110_linktx_dual_input_cfg(struct rk_serdes *serdes, u8 dev_id, bool is_lvds)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_update_bits(client, RKLINK_TX_SERDES_CTRL, CH1_LVDS_SEL_EN,
				is_lvds ? CH1_LVDS_SEL_EN : 0);

	return 0;
}

static int rkx110_linktx_input_port_cfg(struct rk_serdes *serdes, u8 dev_id, u32 port)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val;

	val = rk_serdes_get_stream_source(serdes, port);
	serdes->i2c_update_bits(client, RKLINK_TX_VIDEO_CTRL, SOURCE_ID_MASK, val);

	return 0;
}

int rkx110_linktx_dsi_rec_start(struct rk_serdes *serdes, u8 dev_id, u8 dsi_id, bool enable)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_update_bits(client, SER_RKLINK_DSI_REC0(dsi_id), DSI_REC_START,
				enable ? DSI_REC_START : 0);

	return 0;
}

int rkx110_linktx_dsi_type_select(struct rk_serdes *serdes, u8 dev_id, u8 dsi_id, bool is_cmd)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_update_bits(client, SER_RKLINK_DSI_REC0(dsi_id), DSI_CMD_TYPE,
				is_cmd ? DSI_CMD_TYPE : 0);

	return 0;
}

int rkx110_linktx_dsi_deley_length_config(struct rk_serdes *serdes, u8 dev_id, u8 dsi_id,
					  u32 length)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_update_bits(client, SER_RKLINK_DSI_REC3(dsi_id), DSI_DELAY_LENGTH_MASK,
				DSI_DELAY_LENGTH(length));

	return 0;
}

static int rk_serdes_link_tx_dsi_enable(struct rk_serdes *serdes, struct rk_serdes_route *route,
					int id)
{
	struct videomode *vm = &route->vm;
	struct i2c_client *client = serdes->chip[DEVICE_LOCAL].client;
	struct hwclk *hwclk = serdes->chip[DEVICE_LOCAL].hwclk;
	struct rk_serdes_panel *sd_panel = container_of(route, struct rk_serdes_panel, route);
	struct rkx110_dsi_rx *dsi = &sd_panel->dsi_rx;
	int delay_length;
	u32 value, type;

	if (id) {
		hwclk_set_rate(hwclk, RKX110_CPS_DCLK_D_DSI_1_REC_RKLINK_TX,
			       route->vm.pixelclock);
		dev_info(serdes->dev, "RKX110_CPS_DCLK_D_DSI_1_REC_RKLINK_TX:%d\n",
			 hwclk_get_rate(hwclk, RKX110_CPS_DCLK_D_DSI_1_REC_RKLINK_TX));

	} else {
		hwclk_set_rate(hwclk, RKX110_CPS_DCLK_D_DSI_0_REC_RKLINK_TX,
			       route->vm.pixelclock);
		dev_info(serdes->dev, "RKX110_CPS_DCLK_D_DSI_0_REC_RKLINK_TX:%d\n",
			 hwclk_get_rate(hwclk, RKX110_CPS_DCLK_D_DSI_0_REC_RKLINK_TX));
	}

	/* config SER_RKLINK_DSI_REC1 */
	value = DSI_FRAME_MODE(route->frame_mode);
	value |= DSI_HFP(vm->hfront_porch);
	value |= DSI_HBP(vm->hback_porch);
	serdes->i2c_write_reg(client, SER_RKLINK_DSI_REC1(id), value);

	/* config SER_RKLINK_DSI_REC2 */
	value = DSI_VFP(vm->vfront_porch);
	value |= DSI_VBP(vm->vback_porch);
	value |= DSI_VSA(vm->vsync_len);
	serdes->i2c_update_bits(client, SER_RKLINK_DSI_REC2(id), DSI_DST_VPORCH_MASK, value);

	if (route->local_port0) {
		if (serdes->route_nr == 2) {
			type = id ? DSI_0_DST(2) : DSI_0_DST(1);
		} else {
			if (id)
				type = (serdes->channel_nr == 2) ? DSI_0_DST(0) : DSI_0_DST(2);
			else
				type = (serdes->channel_nr == 2) ? DSI_0_DST(3) : DSI_0_DST(1);
		}
	} else {
		type = id ? DSI_0_DST(1) : DSI_0_DST(2);
	}

	serdes->i2c_update_bits(client, SER_RKLINK_DSI_REC2(0), DSI_0_DST_MASK, type);

	/* config SER_RKLINK_DSI_REC3 */
	if (dsi->mode_flags & SERDES_MIPI_DSI_MODE_VIDEO)
		delay_length = vm->hsync_len + vm->hback_porch +
			       vm->hactive + vm->hfront_porch;
	else
		delay_length = (vm->vfront_porch + 1) * (vm->hsync_len +
				vm->hback_porch + vm->hactive + vm->hfront_porch);

	value = DSI_DELAY_LENGTH(delay_length);
	value |= DSI_HSA(vm->hsync_len);

	serdes->i2c_write_reg(client, SER_RKLINK_DSI_REC3(id), value);

	/* config SER_RKLINK_DSI_REC0 */
	value = DSI_REC_START;
	if (!(dsi->mode_flags & SERDES_MIPI_DSI_MODE_VIDEO))
		value |= DSI_CMD_TYPE;

	value |= DSI_HACT(vm->hactive);
	value |= DSI_VACT(vm->vactive);
	serdes->i2c_write_reg(client, SER_RKLINK_DSI_REC0(id), value);

	return 0;
}

static int rk110_ser_pcs_cfg(struct rk_serdes *serdes, struct rk_serdes_route *route, u8 pcs_id)
{
	struct i2c_client *client;
	u8 remote_id = 0;

	if (route->stream_type == STREAM_DISPLAY)
		client = serdes->chip[DEVICE_LOCAL].client;
	else
		client = serdes->chip[remote_id].client;

	if (serdes->version == SERDES_V1)
		serdes->i2c_write_reg(client, PCS_REG08(pcs_id), VIDEO_SUS_EN(0));

	return 0;
}

static int rk110_ser_pma_cfg(struct rk_serdes *serdes, struct rk_serdes_route *route, u8 pcs_id)
{
	return 0;
}

static int rkx110_display_linktx_ctrl_enable(struct rk_serdes *serdes,
					     struct rk_serdes_route *route,
					     u8 dev_id)
{
	struct hwclk *hwclk = serdes->chip[dev_id].hwclk;
	bool enable;
	bool is_lvds = false;

	rk110_linktx_stream_type_cfg(serdes, dev_id);

	enable = (serdes->lane_nr == 2) ? true : false;
	rk110_linktx_dual_lane_enable(serdes, dev_id, enable);

	enable = (serdes->channel_nr == 2) ? true : false;
	rk110_linktx_dual_channel_enable(serdes, dev_id, enable);

	enable = (route->route_flag & ROUTE_MULTI_MIRROR) ? true : false;
	rk110_linktx_replicate_enable(serdes, dev_id, enable);

	if (((route->local_port0 == RK_SERDES_LVDS_RX0) ||
	     (route->local_port0 == RK_SERDES_LVDS_RX1) ||
	     (route->local_port1 == RK_SERDES_LVDS_RX0) ||
	     (route->local_port1 == RK_SERDES_LVDS_RX1)) &&
	    (serdes->route_nr == 2))
		is_lvds = true;
	rkx110_linktx_dual_input_cfg(serdes, dev_id, is_lvds);

	if (route->local_port0) {
		rkx110_linktx_input_port_cfg(serdes, dev_id, route->local_port0);
	} else {
		if (route->local_port1 == RK_SERDES_LVDS_RX0)
			rkx110_linktx_input_port_cfg(serdes, dev_id, RK_SERDES_LVDS_RX1);
		else if (route->local_port1 == RK_SERDES_LVDS_RX1)
			rkx110_linktx_input_port_cfg(serdes, dev_id, RK_SERDES_LVDS_RX0);
		else if (route->local_port1 == RK_SERDES_DSI_RX0)
			rkx110_linktx_input_port_cfg(serdes, dev_id, RK_SERDES_DSI_RX1);
		else if (route->local_port1 == RK_SERDES_DSI_RX1)
			rkx110_linktx_input_port_cfg(serdes, dev_id, RK_SERDES_DSI_RX0);

	}

	if (route->local_port0 & RK_SERDES_DUAL_LVDS_RX) {
		hwclk_set_rate(hwclk, RKX110_CPS_CLK_2X_LVDS_RKLINK_TX, route->vm.pixelclock);
		dev_info(serdes->dev, "RKX110_CPS_CLK_2X_LVDS_RKLINK_TX:%d\n",
			 hwclk_get_rate(hwclk, RKX110_CPS_CLK_2X_LVDS_RKLINK_TX));
	}

	if (serdes->version == SERDES_V1) {
		/*
		 * The serdes v1 have a bug when enable video suspend function, which
		 * is used to enhance the i2c frequency. A workaround ways to do it is
		 * reducing the video packet length:
		 * length = ((hactive x 24 / 32 / 16) + 15) / 16 * 16
		 */
		u32 length;

		length = route->vm.hactive * 24 / 32 / 16;
		length = (length + 15) / 16 * 16;
		rk110_linktx_config_pkg_length(serdes, dev_id, length);
	}

	rkx110_linktx_ser_enable(serdes, dev_id, true);

	return 0;
}

int rkx110_display_linktx_enable(struct rk_serdes *serdes, struct rk_serdes_route *route)
{
	rkx110_display_linktx_ctrl_enable(serdes, route, DEVICE_LOCAL);

	if (route->local_port0 & RK_SERDES_DSI_RX0)
		rk_serdes_link_tx_dsi_enable(serdes, route, 0);

	if (route->local_port0 & RK_SERDES_DSI_RX1)
		rk_serdes_link_tx_dsi_enable(serdes, route, 1);

	if (route->local_port1 & RK_SERDES_DSI_RX0)
		rk_serdes_link_tx_dsi_enable(serdes, route, 0);

	if (route->local_port1 & RK_SERDES_DSI_RX1)
		rk_serdes_link_tx_dsi_enable(serdes, route, 1);

	rk110_ser_pcs_cfg(serdes, route, 0);
	rk110_ser_pma_cfg(serdes, route, 0);
	if (serdes->lane_nr == 2) {
		rk110_ser_pcs_cfg(serdes, route, 1);
		rk110_ser_pma_cfg(serdes, route, 1);
	}

	return 0;
}

void rkx110_linktx_channel_enable(struct rk_serdes *serdes, u8 ch_id, u8 dev_id, bool enable)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	if (ch_id)
		serdes->i2c_update_bits(client, RKLINK_TX_SERDES_CTRL, STOP_VIDEO_CH1,
					enable ? 0 : STOP_VIDEO_CH1);
	else
		serdes->i2c_update_bits(client, RKLINK_TX_SERDES_CTRL, STOP_VIDEO_CH0,
					enable ? 0 : STOP_VIDEO_CH0);
}

void rkx110_linktx_passthrough_cfg(struct rk_serdes *serdes, u32 client_id, u32 func_id,
				   bool is_rx)
{
	struct i2c_client *client = serdes->chip[client_id].client;
	const struct rk_serdes_pt_pin *pt_pin = ser_pt[func_id].pt_pins;
	int i;

	/* config link passthrough */
	serdes->i2c_update_bits(client, ser_pt[func_id].en_reg, ser_pt[func_id].en_mask,
				ser_pt[func_id].en_val);
	if (ser_pt[func_id].en_reg)
		serdes->i2c_update_bits(client, ser_pt[func_id].dir_reg, ser_pt[func_id].dir_mask,
					is_rx ? ser_pt[func_id].dir_val : ~ser_pt[func_id].dir_val);

	/* config passthrough pinctrl */
	for (i = 0; i < ser_pt[func_id].configs; i++) {
		serdes->set_hwpin(serdes, client, PIN_RKX110, pt_pin[i].bank, pt_pin[i].pin,
				  is_rx ? pt_pin[i].incfgs : pt_pin[i].outcfgs);
	}
}

int rkx110_linktx_wait_link_ready(struct rk_serdes *serdes, u8 id)
{
	struct i2c_client *client = serdes->chip[DEVICE_LOCAL].client;
	u32 val;
	int ret;
	int sta;

	if (id)
		sta = SER_PCS1_READY;
	else
		sta = SER_PCS0_READY;

	ret = read_poll_timeout(serdes->i2c_read_reg, ret,
				!(ret < 0) && (val & sta),
				1000, USEC_PER_SEC, false, client,
				SER_GRF_SOC_STATUS0, &val);
	if (ret < 0)
		dev_err(&client->dev, "wait link ready timeout: 0x%08x\n", val);
	else
		dev_info(&client->dev, "link success: 0x%08x\n", val);

	return ret;
}

void rkx110_pma_set_rate(struct rk_serdes *serdes, struct rk_serdes_pma_pll *pll,
			 u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val;

	serdes->i2c_read_reg(client, SER_PMA_STATUS(pcs_id), &val);
	if (val & SER_PMA_FORCE_INIT_STA)
		serdes->i2c_update_bits(client, SER_PMA_CTRL(pcs_id), SER_PMA_INIT_CNT_CLR_MASK,
					SER_PMA_INIT_CNT_CLR);

	if (pll->force_init_en)
		serdes->i2c_update_bits(client, SER_PMA_CTRL(pcs_id), SER_PMA_FORCE_INIT_MASK,
					SER_PMA_FORCE_INIT_EN);
	else
		serdes->i2c_update_bits(client, SER_PMA_CTRL(pcs_id), SER_PMA_FORCE_INIT_MASK,
					SER_PMA_FORCE_INIT_DISABLE);

	if (pll->rate_mode == FDR_RATE_MODE)
		serdes->i2c_update_bits(client, SER_PMA_LOAD00(pcs_id), PMA_RATE_MODE_MASK,
					PMA_FDR_MODE);
	else if (pll->rate_mode == HDR_RATE_MODE)
		serdes->i2c_update_bits(client, SER_PMA_LOAD00(pcs_id), PMA_RATE_MODE_MASK,
					PMA_HDR_MODE);
	else
		serdes->i2c_update_bits(client, SER_PMA_LOAD00(pcs_id), PMA_RATE_MODE_MASK,
					PMA_QDR_MODE);

	serdes->i2c_update_bits(client, SER_PMA_LOAD04(pcs_id), PMA_PLL_DIV_MASK,
				PMA_PLL_DIV(pll->pll_div));
	serdes->i2c_update_bits(client, SER_PMA_LOAD05(pcs_id), PMA_PLL_REFCLK_DIV_MASK,
				PMA_PLL_REFCLK_DIV(pll->pll_refclk_div));

	if (pll->pll_fck_vco_div2)
		serdes->i2c_update_bits(client, SER_PMA_LOAD08(pcs_id), PMA_FCK_VCO_MASK,
					PMA_FCK_VCO_DIV2);
	else
		serdes->i2c_update_bits(client, SER_PMA_LOAD08(pcs_id), PMA_FCK_VCO_MASK,
					PMA_FCK_VCO);

	if (pll->pll_div4)
		serdes->i2c_update_bits(client, SER_PMA_LOAD09(pcs_id), PMA_PLL_DIV4_MASK,
					PMA_PLL_DIV4);
	else
		serdes->i2c_update_bits(client, SER_PMA_LOAD09(pcs_id), PMA_PLL_DIV4_MASK,
					PMA_PLL_DIV8);

	serdes->i2c_update_bits(client, SER_PMA_LOAD0A(pcs_id), PMA_CLK_8X_DIV_MASK,
				PMA_CLK_8X_DIV(pll->clk_div));
}

void rkx110_pcs_enable(struct rk_serdes *serdes, bool enable, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	dev_info(serdes->dev, "%s: %d\n", __func__, enable);

	if (enable)
		serdes->i2c_update_bits(client, PCS_REG00(pcs_id), PCS_EN_MASK, PCS_EN);
	else
		serdes->i2c_update_bits(client, PCS_REG00(pcs_id), PCS_EN_MASK, PCS_DISABLE);
}

void rkx110_ser_pma_enable(struct rk_serdes *serdes, bool enable, u8 pma_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 mask, val;

	if (pma_id) {
		mask = PMA1_EN_MASK;
		val = enable ? PMA1_EN : PMA1_DISABLE;
	} else {
		mask = PMA0_EN_MASK;
		val = enable ? PMA0_EN : PMA0_DISABLE;
	}

	serdes->i2c_update_bits(client, SER_GRF_SOC_CON7, mask, val);
}

static void rkx110_linktx_irq_enable(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_write_reg(client, SER_GRF_IRQ_EN, SER_IRQ_LINK_EN);

	serdes->i2c_write_reg(client, SER_RKLINK_SOURCE_IRQ_EN, TRAIN_DONE_IRQ_OUTPUT_EN |
			      FIFO_UNDERRUN_IRQ_OUTPUT_EN | FIFO_OVERFLOW_IRQ_OUTPUT_EN);
}

static void rkx110_linktx_irq_disable(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val = 0;

	serdes->i2c_write_reg(client, SER_GRF_IRQ_EN, SER_IRQ_LINK_DIS);
	serdes->i2c_read_reg(client, SER_RKLINK_SOURCE_IRQ_EN, &val);
	val &= ~(SER_RKLINK_SOURCE_IRQ_EN | TRAIN_DONE_IRQ_OUTPUT_EN |
		 FIFO_UNDERRUN_IRQ_OUTPUT_EN | FIFO_OVERFLOW_IRQ_OUTPUT_EN);
	serdes->i2c_write_reg(client, SER_RKLINK_SOURCE_IRQ_EN, val);
}

static void rkx110_linktx_fifo_handler(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 value;

	serdes->i2c_read_reg(client, SER_RKLINK_FIFO_STATUS, &value);
	dev_err(serdes->dev, "ser rklink fifo status:0x%x\n", value);

	if (value & CH1_CMD_FIFO_UNDERRUN)
		dev_err(serdes->dev, "linktx ch1 cmd fifo underrun\n");
	if (value & CH0_CMD_FIFO_UNDERRUN)
		dev_err(serdes->dev, "linktx ch0 cmd fifo underrun\n");
	if (value & DATA1_FIFO_UNDERRUN)
		dev_err(serdes->dev, "linktx data1 fifo underrun\n");
	if (value & DATA0_FIFO_UNDERRUN)
		dev_err(serdes->dev, "linktx data0 fifo underrun\n");
	if (value & AUDIO_FIFO_UNDERRUN)
		dev_err(serdes->dev, "linktx audio fifo underrun\n");
	if (value & LVDS1_FIFO_UNDERRUN)
		dev_err(serdes->dev, "linktx lvds1 fifo underrun\n");
	if (value & LVDS0_FIFO_UNDERRUN)
		dev_err(serdes->dev, "linktx lvds0 fifo underrun\n");
	if (value & DSI_CH1_FIFO_UNDERRUN)
		dev_err(serdes->dev, "linktx dsi ch1 fifo underrun\n");
	if (value & DSI_CH0_FIFO_UNDERRUN)
		dev_err(serdes->dev, "linktx dsi ch0 fifo underrun\n");
	if (value & CH1_CMD_FIFO_OVERFLOW)
		dev_err(serdes->dev, "linktx ch1 cmd fifo overflow\n");
	if (value & CH0_CMD_FIFO_OVERFLOW)
		dev_err(serdes->dev, "linktx ch0 cmd fifo overflow\n");
	if (value & DATA1_FIFO_OVERFLOW)
		dev_err(serdes->dev, "linktx data1 fifo overflow\n");
	if (value & DATA0_FIFO_OVERFLOW)
		dev_err(serdes->dev, "linktx data0 fifo overflow\n");
	if (value & AUDIO_FIFO_OVERFLOW)
		dev_err(serdes->dev, "linktx audio fifo overflow\n");
	if (value & LVDS1_FIFO_OVERFLOW)
		dev_err(serdes->dev, "linktx lvds1 fifo overflow\n");
	if (value & LVDS0_FIFO_OVERFLOW)
		dev_err(serdes->dev, "linktx lvds0 fifo overflow\n");
	if (value & DSI_CH1_FIFO_OVERFLOW)
		dev_err(serdes->dev, "linktx dsi ch1 fifo overflow\n");
	if (value & DSI_CH0_FIFO_OVERFLOW)
		dev_err(serdes->dev, "linktx dsi ch0 fifo overflow\n");

	/* clear fifo status */
	serdes->i2c_write_reg(client, SER_RKLINK_FIFO_STATUS, value);
}

static void rkx110_linktx_irq_handler(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 flag, value;
	int i = 0;

	serdes->i2c_read_reg(client, SER_RKLINK_SOURCE_IRQ_EN, &flag);
	flag &= TRAIN_DONE_IRQ_FLAG | FIFO_UNDERRUN_IRQ_FLAG | FIFO_OVERFLOW_IRQ_FLAG |
		AUDIO_FM_IRQ_OUTPUT_FLAG;
	dev_info(serdes->dev, "linktx irq flag:0x%08x\n", flag);
	while (flag) {
		switch (flag & BIT(i)) {
		case TRAIN_DONE_IRQ_FLAG:
			serdes->i2c_read_reg(client, SER_RKLINK_TRAIN_CTRL, &value);
			dev_info(serdes->dev, "linktx train done, status:0x%08x\n", value);
			/* write any thing to train ctrl will clear the train done irq flag */
			serdes->i2c_write_reg(client, SER_RKLINK_TRAIN_CTRL, value);
			break;
		case FIFO_UNDERRUN_IRQ_FLAG:
		case FIFO_OVERFLOW_IRQ_FLAG:
			flag &= ~(FIFO_UNDERRUN_IRQ_FLAG | FIFO_OVERFLOW_IRQ_FLAG);
			rkx110_linktx_fifo_handler(serdes, dev_id);
			break;
		case AUDIO_FM_IRQ_OUTPUT_FLAG:
			break;
		default:
			break;
		}
		flag &= ~BIT(i);
		i++;
	}
}

static void rkx110_pcs_irq_enable(struct rk_serdes *serdes, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val;

	val = pcs_id ? SER_IRQ_PCS1_EN : SER_IRQ_PCS0_EN;
	serdes->i2c_write_reg(client, SER_GRF_IRQ_EN, val);

	serdes->i2c_write_reg(client, PCS_REG30(pcs_id), PCS_INT_STARTUP(0xffff));
	serdes->i2c_write_reg(client, PCS_REG34(pcs_id), PCS_INT_REMOTE_MODE(0xffff));
}

static void rkx110_pcs_irq_disable(struct rk_serdes *serdes, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val;

	val = pcs_id ? SER_IRQ_PCS1_DIS : SER_IRQ_PCS0_DIS;
	serdes->i2c_write_reg(client, SER_GRF_IRQ_EN, val);

	serdes->i2c_write_reg(client, PCS_REG30(pcs_id), PCS_INT_STARTUP(0));
	serdes->i2c_write_reg(client, PCS_REG34(pcs_id), PCS_INT_REMOTE_MODE(0));
}

static void rkx110_pcs_irq_handler(struct rk_serdes *serdes, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 value;

	serdes->i2c_read_reg(client, PCS_REG24(pcs_id), &value);
	dev_info(serdes->dev, "ser pcs%d startup fatal status:0x%08x\n", pcs_id, value);

	serdes->i2c_read_reg(client, PCS_REG28(pcs_id), &value);
	dev_info(serdes->dev, "ser pcs%d remote mode fatal status:0x%08x\n", pcs_id, value);

	/* clear startup fatal status */
	serdes->i2c_write_reg(client, PCS_REG1C(pcs_id), 0xffffffff);
	serdes->i2c_write_reg(client, PCS_REG1C(pcs_id), 0xffff0000);

	/* clear remote fatal status */
	serdes->i2c_write_reg(client, PCS_REG14(pcs_id), 0xffffffff);
	serdes->i2c_write_reg(client, PCS_REG14(pcs_id), 0xffff0000);
}

static void rkx110_pma_irq_enable(struct rk_serdes *serdes, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val;

	val = pcs_id ? SER_IRQ_PMA_ADAPT1_EN : SER_IRQ_PMA_ADAPT0_EN;
	serdes->i2c_write_reg(client, SER_GRF_IRQ_EN, val);

	serdes->i2c_write_reg(client, SER_PMA_IRQ_EN(pcs_id), FORCE_INITIAL_IRQ_EN |
			      RTERM_ONCE_TIMEOUT_IRQ_EN | PLL_LOCK_TIMEOUT_IRQ_EN);
}

static void rkx110_pma_irq_disable(struct rk_serdes *serdes, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val;

	val = pcs_id ? SER_IRQ_PMA_ADAPT1_DIS : SER_IRQ_PMA_ADAPT0_DIS;
	serdes->i2c_write_reg(client, SER_GRF_IRQ_EN, val);

	serdes->i2c_write_reg(client, SER_PMA_IRQ_EN(pcs_id), 0);
}

static void rkx110_pma_irq_handler(struct rk_serdes *serdes, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 value;

	serdes->i2c_read_reg(client, SER_PMA_IRQ_STATUS(pcs_id), &value);
	dev_info(serdes->dev, "ser pma%d irq status:0x%08x\n", pcs_id, value);

	if (value & FORCE_INITIAL_PULSE_STATUS)
		dev_info(serdes->dev, "ser pma trig force initial pulse status\n");
	else if (value & RTERM_ONCE_TIMEOUT_STATUS)
		dev_info(serdes->dev, "ser pma trig rterm once timeout status\n");
	else if (value & PLL_LOCK_TIMEOUT_STATUS)
		dev_info(serdes->dev, "ser pma trig pll lock timeout status\n");

	/* clear pma irq status */
	serdes->i2c_write_reg(client, SER_PMA_IRQ_STATUS(pcs_id), value);
}

static void rkx110_remote_irq_enable(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	if (serdes->stream_type == STREAM_DISPLAY) {
		serdes->i2c_write_reg(client, SER_GRF_IRQ_EN, SER_IRQ_REMOTE_EN);
		rkx120_irq_enable(serdes, DEVICE_REMOTE0);
	}
}


static void rkx110_remote_irq_disable(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	if (serdes->stream_type == STREAM_DISPLAY) {
		serdes->i2c_write_reg(client, SER_GRF_IRQ_EN, SER_IRQ_REMOTE_DIS);
		rkx120_irq_disable(serdes, DEVICE_REMOTE0);
	}
}

static void rkx110_remote_irq_handler(struct rk_serdes *serdes, u8 dev_id)
{
	if (serdes->stream_type == STREAM_DISPLAY)
		rkx120_irq_handler(serdes, DEVICE_REMOTE0);
}

void rkx110_irq_enable(struct rk_serdes *serdes, u8 dev_id)
{
	/* enable pcs irq */
	rkx110_pcs_irq_enable(serdes, 0, dev_id);

	/* enable dsirx irq */

	/* enable gpio irq */

	/* enable csihost irq */

	/* enable pma_adapt irq */
	rkx110_pma_irq_enable(serdes, 0, dev_id);

	/* enable efuse irq */

	/* enable vicap irq */

	/* enable remote irq */
	rkx110_remote_irq_enable(serdes, dev_id);

	/* enable ext irq */

	/* enable link irq */
	rkx110_linktx_irq_enable(serdes, dev_id);
}

void rkx110_irq_disable(struct rk_serdes *serdes, u8 dev_id)
{
	/* disable pcs irq */
	rkx110_pcs_irq_disable(serdes, 0, dev_id);

	/* disable dsirx irq */

	/* disable gpio irq */

	/* disable csihost irq */

	/* disable pma_adapt irq */
	rkx110_pma_irq_disable(serdes, 0, dev_id);

	/* disable efuse irq */

	/* disable vicap irq */

	/* disable remote irq and other lane irq*/
	rkx110_remote_irq_disable(serdes, dev_id);

	/* disable ext irq */

	/* disable link irq */
	rkx110_linktx_irq_disable(serdes, dev_id);
}

int rkx110_irq_handler(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 status, mask;
	u32 i = 0;

	serdes->i2c_read_reg(client, SER_GRF_IRQ_EN, &mask);
	serdes->i2c_read_reg(client, SER_GRF_IRQ_STATUS, &status);
	dev_info(serdes->dev, "dev%d get the ser irq status:0x%08x\n", dev_id, status);

	status &= mask;

	while (status) {
		switch (status & BIT(i)) {
		case SER_IRQ_PCS0:
			rkx110_pcs_irq_handler(serdes, 0, dev_id);
			break;
		case SER_IRQ_PCS1:
			rkx110_pcs_irq_handler(serdes, 1, dev_id);
			break;
		case SER_IRQ_DSIRX0:
			/* TBD */
			break;
		case SER_IRQ_DSIRX1:
			/* TBD */
			break;
		case SER_IRQ_GPIO0:
			/* TBD */
			break;
		case SER_IRQ_GPIO1:
			/* TBD */
			break;
		case SER_IRQ_CSIHOST0:
			/* TBD */
			break;
		case SER_IRQ_CSIHOST1:
			/* TBD */
			break;
		case SER_IRQ_PMA_ADAPT0:
			rkx110_pma_irq_handler(serdes, 0, dev_id);
			break;
		case SER_IRQ_PMA_ADAPT1:
			rkx110_pma_irq_handler(serdes, 1, dev_id);
			break;
		case SER_IRQ_EFUSE:
			/* TBD */
			break;
		case SER_IRQ_VICAP:
			/* TBD */
			break;
		case SER_IRQ_REMOTE:
			rkx110_remote_irq_handler(serdes, dev_id);
			break;
		case SER_IRQ_EXT:
			/* TBD */
			break;
		case SER_IRQ_LINK:
			rkx110_linktx_irq_handler(serdes, dev_id);
			break;
		case SER_IRQ_OTHER_LANE:
			/* TBD */
			break;
		default:
			break;
		}
		status &= ~BIT(i);
		i++;
	}

	return 0;
}
