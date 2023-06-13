// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Zhang Yubing <yubing.zhang@rock-chips.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <dt-bindings/mfd/rockchip-serdes.h>

#include "hal/cru_api.h"
#include "hal/pinctrl_api.h"
#include "rkx110_x120.h"
#include "rkx120_reg.h"

#define LINK_REG(x)			 ((x) + RKX120_DES_RKLINK_BASE)

#define RKLINK_DES_LANE_ENGINE_CFG	LINK_REG(0x0000)
 #define TRAIN_CLK_SEL_MASK		GENMASK(31, 30)
 #define TRAIN_CLK_SEL_E0		UPDATE(0, 31, 30)
 #define TRAIN_CLK_SEL_E1		UPDATE(1, 31, 30)
 #define TRAIN_CLK_SEL_I2S		UPDATE(2, 31, 30)
 #define DUAL_LVDS_CHANNEL_SWAP		BIT(29)
 #define VIDEO_FREQ_AUTO_EN		BIT(28)
 #define ENGINE1_2_LANE			BIT(23)
 #define ENGINE1_EN			BIT(22)
 #define ENGINE0_2_LANE			BIT(21)
 #define ENGINE0_EN			BIT(20)
 #define LANE1_DATA_WIDTH_8BIT		UPDATE(0, 15, 14)
 #define LANE1_DATA_WIDTH_16BIT		UPDATE(1, 15, 14)
 #define LANE1_DATA_WIDTH_24BIT		UPDATE(2, 15, 14)
 #define LANE1_DATA_WIDTH_32BIT		UPDATE(3, 15, 14)
 #define LANE0_DATA_WIDTH_8BIT		UPDATE(0, 13, 12)
 #define LANE0_DATA_WIDTH_16BIT		UPDATE(1, 13, 12)
 #define LANE0_DATA_WIDTH_24BIT		UPDATE(2, 13, 12)
 #define LANE0_DATA_WIDTH_32BIT		UPDATE(3, 13, 12)
 #define LANE0_EN			BIT(4)
 #define LANE1_EN			BIT(5)
 #define DES_EN				BIT(0)

#define RKLINK_DES_LANE_ENGINE_DST	LINK_REG(0x0004)
#define LANE0_ENGINE_CFG_MASK		GENMASK(3, 0)
#define LANE0_ENGINE0			BIT(0)
#define LANE0_ENGINE1			BIT(1)
#define LANE1_ENGINE_CFG_MASK		GENMASK(7, 4)
#define LANE1_ENGINE0			BIT(4)
#define LANE1_ENGINE1			BIT(5)

#define RKLINK_DES_DATA_ID_CFG		LINK_REG(0x0008)
#define DATA_FIFO3_RD_ID_MASK		GENMASK(30, 28)
#define DATA_FIFO3_RD_ID(x)		UPDATE(x, 30, 28)
#define DATA_FIFO2_RD_ID_MASK		GENMASK(26, 24)
#define DATA_FIFO2_RD_ID(x)		UPDATE(x, 26, 24)
#define DATA_FIFO1_RD_ID_MASK		GENMASK(22, 20)
 #define DATA_FIFO1_RD_ID(x)		UPDATE(x, 22, 20)
#define DATA_FIFO0_RD_ID_MASK		GENMASK(18, 16)
 #define DATA_FIFO0_RD_ID(x)		UPDATE(x, 18, 16)
#define DATA_FIFO3_WR_ID_MASK		GENMASK(14, 12)
#define DATA_FIFO3_WR_ID(x)		UPDATE(x, 14, 12)
#define DATA_FIFO2_WR_ID_MASK		GENMASK(10, 8)
#define DATA_FIFO2_WR_ID(x)		UPDATE(x, 10, 8)
#define DATA_FIFO1_WR_ID_MASK		GENMASK(6, 4)
 #define DATA_FIFO1_WR_ID(x)		UPDATE(x, 6, 4)
#define DATA_FIFO0_WR_ID_MASK		GENMASK(2, 0)
 #define DATA_FIFO0_WR_ID(x)		UPDATE(x, 2, 0)

#define RKLINK_DES_ORDER_ID_CFG		LINK_REG(0x000C)
#define ORDER_FIFO1_RD_ID_MASK		GENMASK(22, 20)
 #define ORDER_FIFO1_RD_ID(x)		UPDATE(x, 22, 20)
#define ORDER_FIFO0_RD_ID_MASK		GENMASK(18, 16)
 #define ORDER_FIFO0_RD_ID(x)		UPDATE(x, 18, 16)
#define ORDER_FIFO1_WR_ID_MASK		GENMASK(6, 4)
 #define ORDER_FIFO1_WR_ID(x)		UPDATE(x, 6, 4)
#define ORDER_FIFO0_WR_ID_MASK		GENMASK(2, 0)
 #define ORDER_FIFO0_WR_ID(x)		UPDATE(x, 2, 0)

#define RKLINK_DES_SOURCE_CFG		LINK_REG(0x0024)
 #define E1_CAMERA_SRC_CSI		UPDATE(0, 23, 21)
 #define E1_CAMERA_SRC_LVDS		UPDATE(1, 23, 21)
 #define E1_CAMERA_SRC_DVP		UPDATE(2, 23, 21)
 #define E1_DISPLAY_SRC_DSI		UPDATE(0, 23, 21)
 #define E1_DISPLAY_SRC_DUAL_LDVS	UPDATE(1, 23, 21)
 #define E1_DISPLAY_SRC_LVDS0		UPDATE(2, 23, 21)
 #define E1_DISPLAY_SRC_LVDS1		UPDATE(3, 23, 21)
 #define E1_DISPLAY_SRC_RGB		UPDATE(5, 23, 21)
 #define E1_STREAM_CAMERA		UPDATE(0, 20, 20)
 #define E1_STREAM_DISPLAY		UPDATE(1, 20, 20)
 #define E0_CAMERA_SRC_CSI		UPDATE(0, 19, 17)
 #define E0_CAMERA_SRC_LVDS		UPDATE(1, 19, 17)
 #define E0_CAMERA_SRC_DVP		UPDATE(2, 19, 17)
 #define E0_DISPLAY_SRC_DSI		UPDATE(0, 19, 17)
 #define E0_DISPLAY_SRC_DUAL_LDVS	UPDATE(1, 19, 17)
 #define E0_DISPLAY_SRC_LVDS0		UPDATE(2, 19, 17)
 #define E0_DISPLAY_SRC_LVDS1		UPDATE(3, 19, 17)
 #define E0_DISPLAY_SRC_RGB		UPDATE(5, 19, 17)
 #define E0_STREAM_CAMERA		UPDATE(0, 16, 16)
 #define E0_STREAM_DISPLAY		UPDATE(1, 16, 16)
 #define LANE1_ENGINE_ID(x)		UPDATE(x, 7, 6)
 #define LANE1_LANE_ID(x)		UPDATE(x, 5, 5)
 #define LNAE1_ID_SEL(x)		UPDATE(x, 4, 4)
 #define LANE0_ENGINE_ID(x)		UPDATE(x, 3, 2)
 #define LANE0_LANE_ID(x)		UPDATE(x, 1, 1)
 #define LNAE0_ID_SEL(x)		UPDATE(x, 0, 0)

#define DES_RKLINK_REC01_PKT_LENGTH	LINK_REG(0x0028)
#define E1_REPKT_LENGTH(x)		UPDATE(x, 29, 16)
#define E0_REPKT_LENGTH(x)		UPDATE(x, 13, 0)
#define RKLINK_DES_REG01_ENGIN_DEL	0x0030
#define E1_ENGINE_DELAY(x)		UPDATE(x, 31, 16)
#define E0_ENGINE_DELAY(x)		UPDATE(x, 15, 0)
#define RKLINK_DES_REG_PATCH		0X0050
#define E3_FIRST_FRAME_DEL		BIT(7)
#define E2_FIRST_FRAME_DEL		BIT(6)
#define E1_FIRST_FRAME_DEL		BIT(5)
#define E0_FIRST_FRAME_DEL		BIT(4)

#define DES_RKLINK_STOP_CFG		LINK_REG(0x009C)
 #define STOP_AUDIO			BIT(4)
 #define STOP_E1			BIT(1)
 #define STOP_E0			BIT(0)

#define RKLINK_DES_SPI_CFG		LINK_REG(0x00C4)
#define RKLINK_DES_UART_CFG		LINK_REG(0x00C8)
#define RKLINK_DES_GPIO_CFG		LINK_REG(0x00CC)
 #define GPIO_GROUP1_EN			BIT(17)
 #define GPIO_GROUP0_EN			BIT(16)

#define PCS_REG(id, x)			((x) + RKX120_DES_PCS0_BASE + (id) * RKX120_DES_PMA_OFFSET)

#define PCS_REG00(id)			PCS_REG(id, 0x00)
 #define DES_PCS_DUAL_LANE_MODE_EN	HIWORD_UPDATE(1, GENMASK(8, 8), 8)
 #define DES_PCS_AUTO_START_EN		HIWORD_UPDATE(1, GENMASK(4, 4), 4)
 #define DES_PCS_ECU_MODE		HIWORD_UPDATE(0, GENMASK(1, 1), 1)
 #define DES_PCS_EN_MASK		HIWORD_MASK(0, 0)
 #define DES_PCS_EN			HIWORD_UPDATE(1, GENMASK(0, 0), 0)
 #define DES_PCS_DISABLE		HIWORD_UPDATE(0, GENMASK(0, 0), 0)

#define PCS_REG04(id)			PCS_REG(id, 0x04)
#define PCS_REG08(id)			PCS_REG(id, 0x08)
#define PCS_REG10(id)			PCS_REG(id, 0x10)
#define PCS_REG14(id)			PCS_REG(id, 0x14)
#define PCS_REG18(id)			PCS_REG(id, 0x18)
#define PCS_REG1C(id)			PCS_REG(id, 0x1C)
#define PCS_REG20(id)			PCS_REG(id, 0x20)
#define PCS_REG24(id)			PCS_REG(id, 0x24)
#define PCS_REG28(id)			PCS_REG(id, 0x28)
#define PCS_REG30(id)			PCS_REG(id, 0x30)
#define PCS_REG34(id)			PCS_REG(id, 0x34)
#define PCS_REG40(id)			PCS_REG(id, 0x40)

#define PMA_REG(id, x)			((x) + RKX120_DES_PMA0_BASE + (id) * RKX120_DES_PMA_OFFSET)

#define DES_PMA_STATUS(id)		PMA_REG(id, 0x00)
 #define DES_PMA_FORCE_INIT_STA		BIT(23)
 #define DES_PMA_RX_LOST		BIT(2)
 #define DES_PMA_RX_PLL_LOCK		BIT(1)
 #define DES_PMA_RX_RDY			BIT(0)

#define DES_PMA_CTRL(id)		PMA_REG(id, 0x04)
 #define DES_PMA_FORCE_INIT_MASK	HIWORD_MASK(8, 8)
 #define DES_PMA_FORCE_INIT_EN		HIWORD_UPDATE(1, BIT(8), 8)
 #define DES_PMA_FORCE_INIT_DISABLE	HIWORD_UPDATE(0, BIT(8), 8)
 #define DES_PMA_DUAL_CHANNEL		HIWORD_UPDATE(1, BIT(3), 3)
 #define DES_PMA_INIT_CNT_CLR_MASK	HIWORD_MASK(2, 2)
 #define DES_PMA_INIT_CNT_CLR		HIWORD_UPDATE(1, BIT(2), 2)

#define DES_PMA_LOAD00(id)		PMA_REG(id, 0x10)
 #define PMA_RX_POL			BIT(0)
 #define PMA_RX_WIDTH			BIT(1)
 #define PMA_RX_MSBF_EN			BIT(2)
 #define PMA_PLL_PWRDN			BIT(3)

#define DES_PMA_LOAD01(id)		PMA_REG(id, 0x14)
 #define DES_PMA_PLL_FORCE_LK(x)	HIWORD_UPDATE(x, GENMASK(13, 13), 13)
 #define DES_PMA_LOS_VTH(x)		HIWORD_UPDATE(x, GENMASK(12, 11), 11)
 #define DES_PMA_PD_CP_PD(x)		HIWORD_UPDATE(x, GENMASK(10, 10), 10)
 #define DES_PMA_PD_CP_FP(x)		HIWORD_UPDATE(x, GENMASK(9, 9), 9)
 #define DES_PMA_PD_LOOP_DIV(x)		HIWORD_UPDATE(x, GENMASK(8, 8), 8)
 #define DES_PMA_PD_PFD(x)		HIWORD_UPDATE(x, GENMASK(7, 7), 7)
 #define DES_PMA_PD_VBIAS(x)		HIWORD_UPDATE(x, GENMASK(6, 6), 6)
 #define DES_PMA_AFE_VOS_EN(x)		HIWORD_UPDATE(x, GENMASK(5, 5), 5)
 #define DES_PMA_PD_AFE(x)		HIWORD_UPDATE(x, GENMASK(4, 4), 4)
 #define DES_PMA_RX_RTERM(x)		HIWORD_UPDATE(x, GENMASK(3, 0), 0)

#define DES_PMA_LOAD02(id)		PMA_REG(id, 0x18)
#define DES_PMA_LOAD03(id)		PMA_REG(id, 0x1C)
#define DES_PMA_LOAD04(id)		PMA_REG(id, 0x20)

#define DES_PMA_LOAD05(id)		PMA_REG(id, 0x24)
 #define DES_PMA_PLL_REFCLK_DIV_MASK	HIWORD_MASK(15, 12)
 #define DES_PMA_PLL_REFCLK_DIV(x)	HIWORD_UPDATE(x, GENMASK(15, 12), 12)

#define DES_PMA_LOAD06(id)		PMA_REG(id, 0x28)
 #define DES_PMA_MDATA_AMP_SEL(x)	HIWORD_UPDATE(x, GENMASK(15, 14), 14)
 #define DES_PMA_RX_TSEQ(x)		HIWORD_UPDATE(x, GENMASK(13, 13), 13)
 #define DES_PMA_FREZ_ADPT_EQ(x)	HIWORD_UPDATE(x, GENMASK(12, 12), 12)
 #define DES_PMA__ADPT_EQ_TRIM(x)	HIWORD_UPDATE(x, GENMASK(11, 0), 0)

#define DES_PMA_LOAD07(id)		PMA_REG(id, 0x2C)
#define DES_PMA_LOAD08(id)		PMA_REG(id, 0x30)
 #define DES_PMA_RX(x)			HIWORD_UPDATE(x, GENMASK(15, 0), 0)

#define DES_PMA_LOAD09(id)		PMA_REG(id, 0x34)
 #define DES_PMA_PLL_DIV_MASK		HIWORD_MASK(14, 0)
 #define DES_PLL_I_POST_DIV(x)		HIWORD_UPDATE(x, GENMASK(14, 10), 10)
 #define DES_PLL_F_POST_DIV(x)		HIWORD_UPDATE(x, GENMASK(9, 0), 0)
 #define DES_PMA_PLL_DIV(x)		HIWORD_UPDATE(x, GENMASK(14, 0), 0)

#define DES_PMA_LOAD0A(id)		PMA_REG(id, 0x38)
 #define DES_PMA_CLK_2X_DIV_MASK	HIWORD_MASK(7, 0)
 #define DES_PMA_CLK_2X_DIV(x)		HIWORD_UPDATE(x, GENMASK(7, 0), 0)

#define DES_PMA_LOAD0B(id)		PMA_REG(id, 0x3C)
#define DES_PMA_LOAD0C(id)		PMA_REG(id, 0x40)
 #define DES_PMA_FCK_VCO_MASK		HIWORD_MASK(15, 15)
 #define DES_PMA_FCK_VCO		HIWORD_UPDATE(1, BIT(15), 15)
 #define DES_PMA_FCK_VCO_DIV2		HIWORD_UPDATE(0, BIT(15), 15)

#define DES_PMA_LOAD0D(id)		PMA_REG(id, 0x44)
 #define DES_PMA_PLL_DIV4_MASK		HIWORD_MASK(12, 12)
 #define DES_PMA_PLL_DIV4		HIWORD_UPDATE(1, GENMASK(12, 12), 12)
 #define DES_PMA_PLL_DIV8		HIWORD_UPDATE(0, GENMASK(12, 12), 12)

#define DES_PMA_LOAD0E(id)		PMA_REG(id, 0x48)
#define DES_PMA_REG100(id)		PMA_REG(id, 0x100)

static const struct rk_serdes_pt des_pt[] = {
	{
		/* gpi_gpo_0 */
		.en_reg = RKLINK_DES_GPIO_CFG,
		.en_mask = 0x10001,
		.en_val = 0x10001,
		.dir_reg = RKLINK_DES_GPIO_CFG,
		.dir_mask = 0x10,
		.dir_val = 0x10,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_DES_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_A5,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC6,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC5,
			},
		},
	}, {
		/* gpi_gpo_1 */
		.en_reg = RKLINK_DES_GPIO_CFG,
		.en_mask = 0x10002,
		.en_val = 0x10002,
		.dir_reg = RKLINK_DES_GPIO_CFG,
		.dir_mask = 0x20,
		.dir_val = 0x20,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_DES_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_A6,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC6,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC5,
			},
		},
	}, {
		/* gpi_gpo_2 */
		.en_reg = RKLINK_DES_GPIO_CFG,
		.en_mask = 0x10004,
		.en_val = 0x10004,
		.dir_reg = RKLINK_DES_GPIO_CFG,
		.dir_mask = 0x40,
		.dir_val = 0x40,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_DES_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_A7,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC6,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC5,
			},
		},
	}, {
		/* gpi_gpo_3 */
		.en_reg = RKLINK_DES_GPIO_CFG,
		.en_mask = 0x10008,
		.en_val = 0x10008,
		.dir_reg = RKLINK_DES_GPIO_CFG,
		.dir_mask = 0x80,
		.dir_val = 0x80,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_DES_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_B0,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC6,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC5,
			},
		},
	}, {
		/* gpi_gpo_4 */
		.en_reg = RKLINK_DES_GPIO_CFG,
		.en_mask = 0x20100,
		.en_val = 0x20100,
		.dir_reg = RKLINK_DES_GPIO_CFG,
		.dir_mask = 0x1000,
		.dir_val = 0x1000,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_DES_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_B3,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC2,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC3,
			},
		},
	}, {
		/* gpi_gpo_5 */
		.en_reg = RKLINK_DES_GPIO_CFG,
		.en_mask = 0x20200,
		.en_val = 0x20200,
		.dir_reg = RKLINK_DES_GPIO_CFG,
		.dir_mask = 0x2000,
		.dir_val = 0x2000,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_DES_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_B4,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC2,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC3,
			},
		},
	}, {
		/* gpi_gpo_6 */
		.en_reg = RKLINK_DES_GPIO_CFG,
		.en_mask = 0x20400,
		.en_val = 0x20400,
		.dir_reg = RKLINK_DES_GPIO_CFG,
		.dir_mask = 0x4000,
		.dir_val = 0x4000,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_DES_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_B5,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC2,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC3,
			},
		},
	}, {
		/* passthrough irq */
		.en_reg = RKLINK_DES_GPIO_CFG,
		.en_mask = 0x20800,
		.en_val = 0x20800,
		.dir_reg = RKLINK_DES_GPIO_CFG,
		.dir_mask = 0x8000,
		.dir_val = 0x8000,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_DES_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_A4,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC1,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC2,
			},
		},
	}, {
		/* passthrough uart0 */
		.en_reg = RKLINK_DES_UART_CFG,
		.en_mask = 0x1,
		.en_val = 0x1,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_DES_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_A5 | RK_SERDES_GPIO_PIN_A6,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC4,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC3,
			},
		},
	}, {
		/* passthrough uart1 */
		.en_reg = RKLINK_DES_UART_CFG,
		.en_mask = 0x2,
		.en_val = 0x2,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_DES_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_A7 | RK_SERDES_GPIO_PIN_B0,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC4,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC3,
			},
		},
	}, {
		/* passthrough spi */
		.en_reg = RKLINK_DES_SPI_CFG,
		.en_mask = 0x4,
		.en_val = 0x4,
		.dir_reg = RKLINK_DES_SPI_CFG,
		.dir_mask = 0x1,
		.dir_val = 0,
		.configs = 1,
		{
			{
				.bank = RK_SERDES_DES_GPIO_BANK0,
				.pin = RK_SERDES_GPIO_PIN_A5 | RK_SERDES_GPIO_PIN_A6 |
				       RK_SERDES_GPIO_PIN_A7 | RK_SERDES_GPIO_PIN_B0,
				.incfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC2,
				.outcfgs = RK_SERDES_PIN_CONFIG_MUX_FUNC1,
			},
		},
	},
};

static int rk_des_get_stream_source(struct rk_serdes_route *route, u32 port, u8 engine_id)
{
	if (route->stream_type == STREAM_DISPLAY) {
		if (port & RK_SERDES_RGB_TX)
			return engine_id ? E1_DISPLAY_SRC_RGB : E0_DISPLAY_SRC_RGB;
		else if (port & RK_SERDES_LVDS_TX0)
			return engine_id ? E1_DISPLAY_SRC_LVDS0 : E0_DISPLAY_SRC_LVDS0;
		else if (port & RK_SERDES_LVDS_TX1)
			return engine_id ? E1_DISPLAY_SRC_LVDS1 : E0_DISPLAY_SRC_LVDS1;
		else if (port & RK_SERDES_DUAL_LVDS_TX)
			return engine_id ? E1_DISPLAY_SRC_DUAL_LDVS : E0_DISPLAY_SRC_DUAL_LDVS;
		else if (port & RK_SERDES_DSI_TX0)
			return engine_id ? E1_DISPLAY_SRC_DSI : E0_DISPLAY_SRC_DSI;
		else if (port & RK_SERDES_DSI_TX1)
			return engine_id ? E1_DISPLAY_SRC_DSI : E0_DISPLAY_SRC_DSI;
	} else {
		return engine_id ? E1_CAMERA_SRC_CSI : E0_CAMERA_SRC_CSI;
	}
	return 0;
}

static void rk_serdes_link_rx_rgb_enable(struct rk_serdes *serdes,
					 struct rk_serdes_route *route,
					 u8 remote_id)
{
	struct i2c_client *client = serdes->chip[remote_id].client;

	serdes->i2c_write_reg(client, RKLINK_DES_REG01_ENGIN_DEL,
			      E1_ENGINE_DELAY(2800) | E0_ENGINE_DELAY(2800));

	serdes->i2c_write_reg(client, RKLINK_DES_REG_PATCH,
			      E3_FIRST_FRAME_DEL | E2_FIRST_FRAME_DEL |
			      E1_FIRST_FRAME_DEL | E0_FIRST_FRAME_DEL);
}

static void rk_serdes_link_rx_lvds_enable(struct rk_serdes *serdes,
					  struct rk_serdes_route *route,
					  u8 remote_id)
{
	struct i2c_client *client = serdes->chip[remote_id].client;

	serdes->i2c_write_reg(client, RKLINK_DES_REG01_ENGIN_DEL,
			      E1_ENGINE_DELAY(4096) | E0_ENGINE_DELAY(4096));

	serdes->i2c_write_reg(client, RKLINK_DES_REG_PATCH,
			      E3_FIRST_FRAME_DEL | E2_FIRST_FRAME_DEL |
			      E1_FIRST_FRAME_DEL | E0_FIRST_FRAME_DEL);
}

static void rk_serdes_link_rx_dsi_enable(struct rk_serdes *serdes,
					struct rk_serdes_route *route,
					u8 remote_id)
{
	struct i2c_client *client = serdes->chip[remote_id].client;

	serdes->i2c_write_reg(client, RKLINK_DES_REG01_ENGIN_DEL,
			      E1_ENGINE_DELAY(4096) | E0_ENGINE_DELAY(4096));

	serdes->i2c_write_reg(client, RKLINK_DES_REG_PATCH,
			      E3_FIRST_FRAME_DEL | E2_FIRST_FRAME_DEL|
			      E1_FIRST_FRAME_DEL | E0_FIRST_FRAME_DEL);
}

static int rk120_link_rx_cfg(struct rk_serdes *serdes, struct rk_serdes_route *route, u8 remote_id)
{
	struct hwclk *hwclk = serdes->chip[remote_id].hwclk;
	struct i2c_client *client;
	struct videomode *vm = &route->vm;
	u32 stream_type;
	u32 rx_src;
	u32 ctrl_val, mask, val;
	u32 lane0_dsource_id, lane1_dsource_id;
	bool is_rx_dual_lanes;
	bool is_rx_dual_channels;
	u32 length;

	if (route->stream_type == STREAM_DISPLAY) {
		client = serdes->chip[remote_id].client;
		stream_type = E0_STREAM_DISPLAY;
	} else {
		client = serdes->chip[DEVICE_LOCAL].client;
		stream_type = E0_STREAM_CAMERA;
	}

	is_rx_dual_lanes = (serdes->route_flag & ROUTE_MULTI_LANE) &&
			   !(serdes->route_flag & ROUTE_MULTI_REMOTE);
	is_rx_dual_channels = (serdes->route_flag & ROUTE_MULTI_CHANNEL) &&
			       !(serdes->route_flag & ROUTE_MULTI_REMOTE);

	serdes->i2c_read_reg(client, RKLINK_DES_LANE_ENGINE_CFG, &ctrl_val);

	ctrl_val &= ~LANE1_EN;
	ctrl_val |= LANE0_EN;
	ctrl_val |= ENGINE0_EN;
	if (is_rx_dual_lanes) {
		ctrl_val |= LANE1_EN;
		if (is_rx_dual_channels)
			ctrl_val |= ENGINE1_EN;
		else
			ctrl_val |= ENGINE0_2_LANE;
	} else {
		if (is_rx_dual_channels)
			ctrl_val |= ENGINE1_EN;
	}
	serdes->i2c_write_reg(client, RKLINK_DES_LANE_ENGINE_CFG, ctrl_val);

	mask = LANE0_ENGINE_CFG_MASK;
	val = LANE0_ENGINE0;
	if (is_rx_dual_lanes) {
		if (is_rx_dual_channels) {
			mask |= LANE1_ENGINE_CFG_MASK;
			val |= LANE1_ENGINE1;
		} else {
			mask |= LANE1_ENGINE_CFG_MASK;
			val |= LANE1_ENGINE0;
		}
	} else {
		if (is_rx_dual_channels)
			val |= LANE0_ENGINE1;
	}

	serdes->i2c_update_bits(client, RKLINK_DES_LANE_ENGINE_DST, mask, val);

	if (serdes->version == SERDES_V1) {
		/*
		 * The serdes v1 have a bug when enable video suspend function, which
		 * is used to enhance the i2c frequency. A workaround ways to do it is
		 * reducing the video packet length:
		 * length = ((hactive x 24 / 32 / 16) + 15) / 16 * 16
		 */
		length = vm->hactive * 24 / 32 / 16;
		length = (length + 15) / 16 * 16;
		serdes->i2c_write_reg(client, DES_RKLINK_REC01_PKT_LENGTH, E0_REPKT_LENGTH(length) |
				      E1_REPKT_LENGTH(length));
	}

	serdes->i2c_read_reg(client, RKLINK_DES_SOURCE_CFG, &val);

	val &= ~(LANE0_ENGINE_ID(1) | LANE0_LANE_ID(1) | LANE1_ENGINE_ID(1) |
		 LANE1_LANE_ID(1) | LNAE0_ID_SEL(1) | LNAE1_ID_SEL(1));

	if (is_rx_dual_lanes) {
		if (is_rx_dual_channels) {
			val |= LANE0_ENGINE_ID(0);
			val |= LANE0_LANE_ID(0);
			val |= LNAE0_ID_SEL(1);
			val |= LANE1_ENGINE_ID(1);
			val |= LANE1_LANE_ID(0);
			val |= LNAE1_ID_SEL(1);
			stream_type |= E1_STREAM_DISPLAY;
		} else {
			val |= LANE0_ENGINE_ID(0);
			val |= LANE0_LANE_ID(0);
			val |= LNAE0_ID_SEL(1);
			val |= LANE1_ENGINE_ID(0);
			val |= LANE1_LANE_ID(1);
			val |= LNAE0_ID_SEL(1);
		}
	} else {
		if (is_rx_dual_channels) {
			val |= LANE0_ENGINE_ID(0);
			val |= LANE0_LANE_ID(0);
			val |= LANE1_ENGINE_ID(1);
			val |= LANE1_LANE_ID(0);
			stream_type |= E1_STREAM_DISPLAY;
		} else {
			val |= LNAE0_ID_SEL(1);
		}
	}
	val |= stream_type;
	if (remote_id == DEVICE_REMOTE0)
		rx_src = rk_des_get_stream_source(route, route->remote0_port0, 0);
	else
		rx_src = rk_des_get_stream_source(route, route->remote1_port0, 0);
	val |= rx_src;
	if (is_rx_dual_channels) {
		rx_src = rk_des_get_stream_source(route, route->remote0_port1, 1);
		val |= rx_src;
	}
	serdes->i2c_write_reg(client, RKLINK_DES_SOURCE_CFG, val);

	if (is_rx_dual_lanes || is_rx_dual_channels) {
		mask = DATA_FIFO0_WR_ID_MASK | DATA_FIFO1_WR_ID_MASK | DATA_FIFO2_WR_ID_MASK |
			DATA_FIFO3_WR_ID_MASK;
		mask |= DATA_FIFO0_RD_ID_MASK | DATA_FIFO1_RD_ID_MASK | DATA_FIFO2_RD_ID_MASK |
			DATA_FIFO3_RD_ID_MASK;
		if (is_rx_dual_channels) {
			lane0_dsource_id = (0 << 1) | 0;
			lane1_dsource_id = (1 << 1) | 0;
		} else {
			lane0_dsource_id = (0 << 1) | 0;
			lane1_dsource_id = (0 << 1) | 1;
		}
		val = DATA_FIFO0_WR_ID(lane0_dsource_id) | DATA_FIFO1_WR_ID(lane0_dsource_id);
		val |= DATA_FIFO0_RD_ID(lane0_dsource_id) | DATA_FIFO1_RD_ID(lane0_dsource_id);

		val |= DATA_FIFO2_WR_ID(lane1_dsource_id) | DATA_FIFO3_WR_ID(lane1_dsource_id);
		val |= DATA_FIFO2_RD_ID(lane1_dsource_id) | DATA_FIFO3_RD_ID(lane1_dsource_id);

		serdes->i2c_update_bits(client, RKLINK_DES_DATA_ID_CFG, mask, val);

		mask = ORDER_FIFO0_WR_ID_MASK | ORDER_FIFO1_WR_ID_MASK |
			ORDER_FIFO0_RD_ID_MASK | ORDER_FIFO1_RD_ID_MASK;
		val = ORDER_FIFO0_WR_ID(lane0_dsource_id) | ORDER_FIFO1_WR_ID(lane1_dsource_id) |
			ORDER_FIFO0_RD_ID(lane0_dsource_id) | ORDER_FIFO1_RD_ID(lane1_dsource_id);

		serdes->i2c_update_bits(client, RKLINK_DES_ORDER_ID_CFG, mask, val);
	}

	ctrl_val |= DES_EN;
	serdes->i2c_write_reg(client, RKLINK_DES_LANE_ENGINE_CFG, ctrl_val);

	hwclk_set_rate(hwclk, RKX120_CPS_E0_CLK_RKLINK_RX_PRE, route->vm.pixelclock);
	dev_info(serdes->dev, "RKX120_CPS_E0_CLK_RKLINK_RX_PRE:%d\n",
		 hwclk_get_rate(hwclk, RKX120_CPS_E0_CLK_RKLINK_RX_PRE));
	if (is_rx_dual_channels) {
		hwclk_set_rate(hwclk, RKX120_CPS_E1_CLK_RKLINK_RX_PRE, route->vm.pixelclock);
		dev_info(serdes->dev, "RKX120_CPS_E1_CLK_RKLINK_RX_PRE:%d\n",
			 hwclk_get_rate(hwclk, RKX120_CPS_E1_CLK_RKLINK_RX_PRE));
	}

	if (route->remote0_port0 == RK_SERDES_RGB_TX || route->remote1_port0 == RK_SERDES_RGB_TX)
		rk_serdes_link_rx_rgb_enable(serdes, route, remote_id);

	if (route->remote0_port0 == RK_SERDES_LVDS_TX0 ||
	    route->remote1_port0 == RK_SERDES_LVDS_TX0 ||
	    route->remote0_port0 == RK_SERDES_LVDS_TX1 ||
	    route->remote1_port0 == RK_SERDES_LVDS_TX1 ||
	    route->remote0_port0 == RK_SERDES_DUAL_LVDS_TX)
		rk_serdes_link_rx_lvds_enable(serdes, route, remote_id);

	if (route->remote0_port0 == RK_SERDES_DSI_TX0 || route->remote1_port0 == RK_SERDES_DSI_TX0)
		rk_serdes_link_rx_dsi_enable(serdes, route, remote_id);

	return 0;
}

static int rk120_des_pcs_cfg(struct rk_serdes *serdes, struct rk_serdes_route *route,
			     u8 remote_id, u8 pcs_id)
{
	return 0;
}

static int rk120_des_pma_cfg(struct rk_serdes *serdes, struct rk_serdes_route *route, u8 remote_id,
			     u8 pcs_id)
{
	return 0;
}

int rkx120_linkrx_enable(struct rk_serdes *serdes, struct rk_serdes_route *route, u8 remote_id)
{
	rk120_link_rx_cfg(serdes, route, remote_id);

	rk120_des_pcs_cfg(serdes, route, remote_id, 0);
	rk120_des_pma_cfg(serdes, route, remote_id, 0);
	if ((serdes->route_flag & ROUTE_MULTI_LANE) &&
	    !(serdes->route_flag & ROUTE_MULTI_REMOTE)) {
		rk120_des_pcs_cfg(serdes, route, remote_id, 1);
		rk120_des_pma_cfg(serdes, route, remote_id, 1);
	}

	return 0;
}

void rkx120_linkrx_engine_enable(struct rk_serdes *serdes, u8 en_id, u8 dev_id, bool enable)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	if (en_id)
		serdes->i2c_update_bits(client, DES_RKLINK_STOP_CFG, STOP_E1,
					enable ? 0 : STOP_E1);
	else
		serdes->i2c_update_bits(client, DES_RKLINK_STOP_CFG, STOP_E0,
					enable ? 0 : STOP_E0);
}

void rkx120_linkrx_passthrough_cfg(struct rk_serdes *serdes, u32 client_id, u32 func_id,
				   bool is_rx)
{
	struct i2c_client *client = serdes->chip[client_id].client;
	const struct rk_serdes_pt_pin *pt_pin = des_pt[func_id].pt_pins;
	int i;

	/* config link passthrough */
	serdes->i2c_update_bits(client, des_pt[func_id].en_reg, des_pt[func_id].en_mask,
				des_pt[func_id].en_val);
	if (des_pt[func_id].en_reg)
		serdes->i2c_update_bits(client, des_pt[func_id].dir_reg, des_pt[func_id].dir_mask,
					is_rx ? des_pt[func_id].dir_val : ~des_pt[func_id].dir_val);

	/* config passthrough pinctrl */
	for (i = 0; i < des_pt[func_id].configs; i++) {
		serdes->set_hwpin(serdes, client, PIN_RKX120, pt_pin[i].bank, pt_pin[i].pin,
				  is_rx ? pt_pin[i].incfgs : pt_pin[i].outcfgs);
	}
}

void rkx120_linkrx_wait_link_ready(struct rk_serdes *serdes, u8 id)
{
	struct i2c_client *client = serdes->chip[DEVICE_LOCAL].client;
	u32 val;
	int ret;
	int sta;

	if (id)
		sta = DES_PCS1_READY;
	else
		sta = DES_PCS0_READY;

	ret = read_poll_timeout(serdes->i2c_read_reg, ret,
				!(ret < 0) && (val & sta),
				1000, USEC_PER_SEC, false, client,
				DES_GRF_SOC_STATUS0, &val);
	if (ret < 0)
		dev_err(&client->dev, "wait link ready timeout: 0x%08x\n", val);
	else
		dev_info(&client->dev, "link success: 0x%08x\n", val);
}

static void rkx120_pma_link_config(struct rk_serdes *serdes, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_write_reg(client, DES_PMA_LOAD08(pcs_id), DES_PMA_RX(0x23b1));
	serdes->i2c_write_reg(client, DES_PMA_LOAD01(pcs_id), DES_PMA_LOS_VTH(0) |
							      DES_PMA_RX_RTERM(0x8));
	serdes->i2c_write_reg(client, DES_PMA_LOAD06(pcs_id), DES_PMA_MDATA_AMP_SEL(0x3));
	serdes->i2c_write_reg(client, DES_PMA_REG100(pcs_id), 0xffff0000);
}

void rkx120_pma_set_rate(struct rk_serdes *serdes, struct rk_serdes_pma_pll *pll,
			 u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val;

	serdes->i2c_read_reg(client, DES_PMA_STATUS(pcs_id), &val);
	if (val & DES_PMA_FORCE_INIT_STA)
		serdes->i2c_update_bits(client, DES_PMA_CTRL(pcs_id), DES_PMA_INIT_CNT_CLR_MASK,
					DES_PMA_INIT_CNT_CLR);

	if (pll->force_init_en)
		serdes->i2c_update_bits(client, DES_PMA_CTRL(pcs_id), DES_PMA_FORCE_INIT_MASK,
					DES_PMA_FORCE_INIT_EN);
	else
		serdes->i2c_update_bits(client, DES_PMA_CTRL(pcs_id), DES_PMA_FORCE_INIT_MASK,
					DES_PMA_FORCE_INIT_DISABLE);

	serdes->i2c_update_bits(client, DES_PMA_LOAD09(pcs_id), DES_PMA_PLL_DIV_MASK,
				DES_PMA_PLL_DIV(pll->pll_div));
	serdes->i2c_update_bits(client, DES_PMA_LOAD05(pcs_id), DES_PMA_PLL_REFCLK_DIV_MASK,
				DES_PMA_PLL_REFCLK_DIV(pll->pll_refclk_div));

	if (pll->pll_fck_vco_div2)
		serdes->i2c_update_bits(client, DES_PMA_LOAD0C(pcs_id), DES_PMA_FCK_VCO_MASK,
					DES_PMA_FCK_VCO_DIV2);
	else
		serdes->i2c_update_bits(client, DES_PMA_LOAD0C(pcs_id), DES_PMA_FCK_VCO_MASK,
					DES_PMA_FCK_VCO);

	if (pll->pll_div4)
		serdes->i2c_update_bits(client, DES_PMA_LOAD0D(pcs_id), DES_PMA_PLL_DIV4_MASK,
					DES_PMA_PLL_DIV4);
	else
		serdes->i2c_update_bits(client, DES_PMA_LOAD0D(pcs_id), DES_PMA_PLL_DIV4_MASK,
					DES_PMA_PLL_DIV8);

	serdes->i2c_update_bits(client, DES_PMA_LOAD0A(pcs_id), DES_PMA_CLK_2X_DIV_MASK,
				DES_PMA_CLK_2X_DIV(pll->clk_div));

	rkx120_pma_link_config(serdes, pcs_id, dev_id);
}

void rkx120_pcs_enable(struct rk_serdes *serdes, bool enable, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	dev_info(serdes->dev, "%s: %d\n", __func__, enable);

	if (enable)
		serdes->i2c_update_bits(client, PCS_REG00(pcs_id), DES_PCS_EN_MASK, DES_PCS_EN);
	else
		serdes->i2c_update_bits(client, PCS_REG00(pcs_id), DES_PCS_EN_MASK,
					DES_PCS_DISABLE);
}

void rkx120_des_pma_enable(struct rk_serdes *serdes, bool enable, u8 pma_id, u8 dev_id)
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

	serdes->i2c_update_bits(client, DES_GRF_SOC_CON4, mask, val);
}
