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
 #define ENGINE_CFG_MASK		GENMASK(23, 20)
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
 #define LANE1_PKT_LOSE_NUM_CLR		BIT(9)
 #define LANE0_PKT_LOSE_NUM_CLR		BIT(8)
 #define LANE_CFG_MASK			GENMASK(5, 4)
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
 #define E1_STREAM_CFG_MASK		GENMASK(23, 20)
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
 #define E0_STREAM_CFG_MASK		GENMASK(19, 16)
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
 #define LANE_ID_CFG_MASK		GENMASK(7, 0)
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
#define RKLINK_DES_FIFO_STATUS		LINK_REG(0x0084)
 #define AUDIO_FIFO_UNDERRUN		BIT(29)
 #define AUDIO_ORDER_UNDERRUN		BIT(28)
 #define VIDEO_DATA_FIFO_UNDERRUN	BIT(27)
 #define VIDEO_ORDER_UNDERRUN		BIT(26)
 #define CMD_FIFO_UNDERRUN		BIT(25)
 #define E1_ORDER_MIS			BIT(15)
 #define E0_ORDER_MIS			BIT(14)
 #define AUDIO_FIFO_OVERFLOW		BIT(13)
 #define AUDIO_ORDER_OVERFLOW		BIT(12)
 #define VIDEO_DATA_FIFO_OVERFLOW	GENMASK(11, 8)
 #define VIDEO_ORDER_OVERFLOW		GENMASK(7, 4)
 #define CMD_FIFO_OVERFLOW		GENMASK(3, 0)
#define RKLINK_DES_SINK_IRQ_EN		LINK_REG(0x0088)
 #define COMP_NOT_ENOUGH_IRQ_FLAG	BIT(26)
 #define VIDEO_FM_IRQ_FLAG		BIT(25)
 #define AUDIO_FM_IRQ_FLAG		BIT(24)
 #define ORDER_MIS_IRQ_FLAG		BIT(23)
 #define FIFO_UNDERRUN_IRQ_FLAG		BIT(22)
 #define FIFO_OVERFLOW_IRQ_FLAG		BIT(21)
 #define PKT_LOSE_IRQ_FLAG		BIT(20)
 #define LAST_ERROR_IRQ_FLAG		BIT(19)
 #define ECC2BIT_ERROR_IRQ_FLAG		BIT(18)
 #define ECC1BIT_ERROR_IRQ_FLAG		BIT(17)
 #define CRC_ERROR_IRQ_FLAG		BIT(16)
 #define COMP_NOT_ENOUGH_IRQ_OUTPUT_EN	BIT(10)
 #define VIDEO_FM_IRQ_OUTPUT_EN		BIT(9)
 #define AUDIO_FM_IRQ_OUTPUT_EN		BIT(8)
 #define ORDER_MIS_IRQ_OUTPUT_EN	BIT(7)
 #define FIFO_UNDERRUN_IRQ_OUTPUT_EN	BIT(6)
 #define FIFO_OVERFLOW_IRQ_OUTPUT_EN	BIT(5)
 #define PKT_LOSE_IRQ_OUTPUT_EN		BIT(4)
 #define LAST_ERROR_IRQ_OUTPUT_EN	BIT(3)
 #define ECC2BIT_ERROR_IRQ_OUTPUT_EN	BIT(2)
 #define ECC1BIT_ERROR_IRQ_OUTPUT_EN	BIT(1)
 #define CRC_ERROR_IRQ_OUTPUT_EN	BIT(0)

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
 #define DES_PCS_INI_EN(x)		HIWORD_UPDATE(x, GENMASK(15, 0), 0)
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

#define DES_PMA_IRQ_EN(id)		PMA_REG(id, 0xF0)
 #define FORCE_INITIAL_IRQ_EN		HIWORD_UPDATE(1, BIT(6), 6)
 #define RX_RDY_NEG_IRQ_EN		HIWORD_UPDATE(1, BIT(5), 5)
 #define RX_LOS_IRQ_EN			HIWORD_UPDATE(1, BIT(4), 4)
 #define RX_RDY_TIMEOUT_IRQ_EN		HIWORD_UPDATE(1, BIT(2), 2)
 #define PLL_LOCK_TIMEOUT_IRQ_EN	HIWORD_UPDATE(1, BIT(0), 0)
#define DES_PMA_IRQ_STATUS(id)		PMA_REG(id, 0xF4)
 #define FORCE_INITIAL_IRQ_STATUS	BIT(6)
 #define RX_RDY_NEG_IRQ_STATUS		BIT(5)
 #define RX_LOS_IRQ_STATUS		BIT(4)
 #define RX_RDY_TIMEOUT_IRQ_STATUS	BIT(2)
 #define PLL_LOCK_TIMEOUT_IRQ_STATUS	BIT(0)

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

static int rk_des_get_stream_source(u32 stream_type, u32 port, u8 engine_id)
{
	if (stream_type == STREAM_DISPLAY) {
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

static int rk120_linkrx_des_enable(struct rk_serdes *serdes, u8 dev_id, bool enable)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_update_bits(client, RKLINK_DES_LANE_ENGINE_CFG, DES_EN, enable ? DES_EN : 0);

	return 0;
}

static int rk120_linkrx_video_fm_enable(struct rk_serdes *serdes, u8 dev_id, bool enable)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_update_bits(client, RKLINK_DES_LANE_ENGINE_CFG, VIDEO_FREQ_AUTO_EN,
				enable ? VIDEO_FREQ_AUTO_EN : 0);

	return 0;
}

static int rk120_linkrx_engine_lane_enable(struct rk_serdes *serdes, u8 dev_id,
					   bool dual_channels, bool dual_lanes)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val = 0;

	/*
	 * config engine and lane as fallow:
	 * 1.linkrx receive 1 channel data in 1 lane, enable engine0 and engine0 use 1 lane.
	 * 2.linkrx receive 1 channel data in 2 lane, enable engine0 and engine0 user 2 lanes.
	 * 3.linkrx receive 2 channel data in 1 lane, enable engine0, enagine1. engine0 use
	 *   1 lane, engine1 use 1 lane.
	 * 4.linkrx receive 2 channel data in 2 lane, enable engine0, enagine1. engine0 use
	 *   1 lane, engine1 use 1 lane.
	 */
	if (dual_channels) {
		val |= ENGINE0_EN | ENGINE1_EN;
	} else {
		val |= ENGINE0_EN;
		if (dual_lanes)
			val |= ENGINE0_2_LANE;
	}

	serdes->i2c_update_bits(client, RKLINK_DES_LANE_ENGINE_CFG, ENGINE_CFG_MASK, val);

	return 0;
}

static int rk120_linkrx_lane_enable(struct rk_serdes *serdes, u8 dev_id, u32 lanes)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val;

	/*
	 * when 1 lane connect to linkrx, enable lane0;
	 * when 2 lane connect to linkrx, enable lane0 and lane1;
	 */

	if (lanes == 1)
		val = LANE0_EN;
	else if (lanes == 2)
		val = LANE0_EN | LANE1_EN;
	else
		val = 0;

	serdes->i2c_update_bits(client, RKLINK_DES_LANE_ENGINE_CFG, LANE_CFG_MASK, val);

	return 0;
}

static int rk120_linkrx_lane_engine_dst_cfg(struct rk_serdes *serdes, u8 dev_id,
					     bool dual_channels, bool dual_lanes)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 mask, val;

	/*
	 * config lane dst engine as fallow:
	 * 1. 1 channel 1 lane: lane0 data send to engine0
	 * 2. 1 channel 2 lane: lane0 data send to engine0, lane1 data send to engine0
	 * 3. 2 channel 1 lane: lane0 data send to engine0, lane0 data send to engine1
	 * 4. 2 channel 2 lane: lane0 data send to engine0, lane1 data send to engine1
	 */
	if (dual_channels) {
		if (dual_lanes) {
			mask = LANE0_ENGINE_CFG_MASK | LANE1_ENGINE_CFG_MASK;
			val = LANE0_ENGINE0 | LANE1_ENGINE1;
		} else {
			mask = LANE0_ENGINE_CFG_MASK | LANE1_ENGINE_CFG_MASK;
			val = LANE0_ENGINE0 | LANE0_ENGINE1;
		}
	} else {

		if (dual_lanes) {
			mask = LANE0_ENGINE_CFG_MASK | LANE1_ENGINE_CFG_MASK;
			val = LANE0_ENGINE0 | LANE1_ENGINE0;
		} else {
			mask = LANE0_ENGINE_CFG_MASK | LANE1_ENGINE_CFG_MASK;
			val = LANE0_ENGINE0 | LANE1_ENGINE1;
		}
	}
	serdes->i2c_update_bits(client, RKLINK_DES_LANE_ENGINE_DST, mask, val);

	return 0;
}

static int rk120_linkrx_config_pkt_length(struct rk_serdes *serdes, u8 dev_id, u32 length)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_write_reg(client, DES_RKLINK_REC01_PKT_LENGTH, E0_REPKT_LENGTH(length) |
			      E1_REPKT_LENGTH(length));

	return 0;
}

static int rk120_linkrx_lane_id_cfg(struct rk_serdes *serdes, u8 dev_id,
				     bool dual_channels, bool dual_lanes)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val;

	if (dual_channels) {
		if (dual_lanes) {
			val = LANE0_ENGINE_ID(0) | LANE0_LANE_ID(0) | LNAE0_ID_SEL(1) |
			      LANE1_ENGINE_ID(1) | LANE1_LANE_ID(0) | LNAE1_ID_SEL(1);
		} else {
			val = LANE0_ENGINE_ID(0) | LANE0_LANE_ID(0) | LANE1_ENGINE_ID(1) |
			      LANE1_LANE_ID(0);
		}
	} else {
		if (dual_lanes) {
			val = LANE0_ENGINE_ID(0) | LANE0_LANE_ID(0) | LNAE0_ID_SEL(1) |
			      LANE1_ENGINE_ID(0) | LANE1_LANE_ID(1) | LNAE1_ID_SEL(1);
		} else {
			val = LNAE0_ID_SEL(1);
		}
	}

	serdes->i2c_update_bits(client, RKLINK_DES_SOURCE_CFG, LANE_ID_CFG_MASK, val);

	return 0;
}

static int rk120_linkrx_stream_type_cfg(struct rk_serdes *serdes, u32 stream_type,
					 u8 dev_id, u32 port, u32 engine_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val, mask, rx_src;

	mask = engine_id ? E1_STREAM_CFG_MASK : E0_STREAM_CFG_MASK;
	if (stream_type == STREAM_DISPLAY)
		val =  engine_id ? E1_STREAM_DISPLAY : E0_STREAM_DISPLAY;
	else
		val =  engine_id ? E1_STREAM_CAMERA : E0_STREAM_CAMERA;

	rx_src = rk_des_get_stream_source(stream_type, port, engine_id);
	val |= rx_src;
	serdes->i2c_update_bits(client, RKLINK_DES_SOURCE_CFG, mask, val);

	return 0;
}

static int rk120_linkrx_data_and_order_id_cfg(struct rk_serdes *serdes, u8 dev_id,
					       bool dual_channels, bool dual_lanes)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 lane0_dsource_id, lane1_dsource_id;
	u32 data_id_mask;
	u32 order_id_mask;
	u32 val;

	data_id_mask = DATA_FIFO0_WR_ID_MASK | DATA_FIFO1_WR_ID_MASK |
		       DATA_FIFO2_WR_ID_MASK | DATA_FIFO3_WR_ID_MASK |
		       DATA_FIFO0_RD_ID_MASK | DATA_FIFO1_RD_ID_MASK |
		       DATA_FIFO2_RD_ID_MASK | DATA_FIFO3_RD_ID_MASK;
	order_id_mask = ORDER_FIFO0_WR_ID_MASK | ORDER_FIFO1_WR_ID_MASK |
			ORDER_FIFO0_RD_ID_MASK | ORDER_FIFO1_RD_ID_MASK;

	if (dual_channels) {
		lane0_dsource_id = (0 << 1) | 0;
		lane1_dsource_id = (1 << 1) | 0;
	} else {
		if (dual_lanes) {
			lane0_dsource_id = (0 << 1) | 0;
			lane1_dsource_id = (0 << 1) | 1;
		} else {
			lane0_dsource_id = (0 << 1) | 0;
			lane1_dsource_id = (1 << 1) | 0;
		}
	}

	val = DATA_FIFO0_WR_ID(lane0_dsource_id) | DATA_FIFO1_WR_ID(lane0_dsource_id) |
	      DATA_FIFO0_RD_ID(lane0_dsource_id) | DATA_FIFO1_RD_ID(lane0_dsource_id) |
	      DATA_FIFO2_WR_ID(lane1_dsource_id) | DATA_FIFO3_WR_ID(lane1_dsource_id) |
	      DATA_FIFO2_RD_ID(lane1_dsource_id) | DATA_FIFO3_RD_ID(lane1_dsource_id);
	serdes->i2c_update_bits(client, RKLINK_DES_DATA_ID_CFG, data_id_mask, val);

	val = ORDER_FIFO0_WR_ID(lane0_dsource_id) | ORDER_FIFO1_WR_ID(lane1_dsource_id) |
	      ORDER_FIFO0_RD_ID(lane0_dsource_id) | ORDER_FIFO1_RD_ID(lane1_dsource_id);

	serdes->i2c_update_bits(client, RKLINK_DES_ORDER_ID_CFG, order_id_mask, val);

	return 0;
}

static int rk120_display_linkrx_cfg(struct rk_serdes *serdes,
				    struct rk_serdes_route *route, u8 dev_id)
{
	struct hwclk *hwclk = serdes->chip[dev_id].hwclk;
	bool is_rx_dual_lanes = false;
	bool is_rx_dual_channels = false;

	if (serdes->route_nr == 1) {
		is_rx_dual_lanes = (serdes->lane_nr == 2) &&
				   !(route->route_flag & ROUTE_MULTI_REMOTE);
		is_rx_dual_channels = (route->route_flag & ROUTE_MULTI_CHANNEL) &&
				       !(route->route_flag & ROUTE_MULTI_REMOTE);
	} else {
		is_rx_dual_lanes = (serdes->lane_nr == 2) && (serdes->remote_nr == 1);
		is_rx_dual_channels = (serdes->channel_nr == 2) && (serdes->remote_nr == 1);
	}

	rk120_linkrx_video_fm_enable(serdes, dev_id, true);
	rk120_linkrx_engine_lane_enable(serdes, dev_id, is_rx_dual_channels, is_rx_dual_lanes);
	rk120_linkrx_lane_enable(serdes, dev_id, is_rx_dual_lanes ? 2 : 1);

	rk120_linkrx_lane_engine_dst_cfg(serdes, dev_id, is_rx_dual_channels, is_rx_dual_lanes);
	rk120_linkrx_lane_id_cfg(serdes, dev_id, is_rx_dual_channels, is_rx_dual_lanes);
	if (route->local_port0) {
		if (dev_id == DEVICE_REMOTE0) {
			rk120_linkrx_stream_type_cfg(serdes, route->stream_type, dev_id,
						     route->remote0_port0, 0);
			if (is_rx_dual_channels)
				rk120_linkrx_stream_type_cfg(serdes, route->stream_type, dev_id,
						     route->remote0_port1, 1);
		} else {
			rk120_linkrx_stream_type_cfg(serdes, route->stream_type, dev_id,
						     route->remote1_port0, 0);
		}
	} else {
		rk120_linkrx_stream_type_cfg(serdes, route->stream_type, dev_id,
					     route->remote1_port0, 0);
	}

	rk120_linkrx_data_and_order_id_cfg(serdes, dev_id, is_rx_dual_channels,
					    is_rx_dual_lanes);
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
		rk120_linkrx_config_pkt_length(serdes, dev_id, length);
	}

	rk120_linkrx_des_enable(serdes, dev_id, true);

	hwclk_set_rate(hwclk, RKX120_CPS_E0_CLK_RKLINK_RX_PRE, route->vm.pixelclock);
	dev_info(serdes->dev, "RKX120_CPS_E0_CLK_RKLINK_RX_PRE:%d\n",
		 hwclk_get_rate(hwclk, RKX120_CPS_E0_CLK_RKLINK_RX_PRE));
	if (is_rx_dual_channels) {
		hwclk_set_rate(hwclk, RKX120_CPS_E1_CLK_RKLINK_RX_PRE, route->vm.pixelclock);
		dev_info(serdes->dev, "RKX120_CPS_E1_CLK_RKLINK_RX_PRE:%d\n",
			 hwclk_get_rate(hwclk, RKX120_CPS_E1_CLK_RKLINK_RX_PRE));
	}

	if (route->remote0_port0 == RK_SERDES_RGB_TX || route->remote1_port0 == RK_SERDES_RGB_TX)
		rk_serdes_link_rx_rgb_enable(serdes, route, dev_id);

	if (route->remote0_port0 == RK_SERDES_LVDS_TX0 ||
	    route->remote1_port0 == RK_SERDES_LVDS_TX0 ||
	    route->remote0_port0 == RK_SERDES_LVDS_TX1 ||
	    route->remote1_port0 == RK_SERDES_LVDS_TX1 ||
	    route->remote0_port0 == RK_SERDES_DUAL_LVDS_TX)
		rk_serdes_link_rx_lvds_enable(serdes, route, dev_id);

	if (route->remote0_port0 == RK_SERDES_DSI_TX0 || route->remote1_port0 == RK_SERDES_DSI_TX0)
		rk_serdes_link_rx_dsi_enable(serdes, route, dev_id);

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

int rkx120_display_linkrx_enable(struct rk_serdes *serdes,
				 struct rk_serdes_route *route, u8 dev_id)
{
	rk120_display_linkrx_cfg(serdes, route, dev_id);

	rk120_des_pcs_cfg(serdes, route, dev_id, 0);
	rk120_des_pma_cfg(serdes, route, dev_id, 0);
	if ((serdes->lane_nr == 2) && (serdes->remote_nr == 1)) {
		rk120_des_pcs_cfg(serdes, route, dev_id, 1);
		rk120_des_pma_cfg(serdes, route, dev_id, 1);
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

int rkx120_linkrx_wait_link_ready(struct rk_serdes *serdes, u8 id)
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

	return ret;
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


static void rkx120_linkrx_irq_enable(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	serdes->i2c_write_reg(client, DES_GRF_IRQ_EN, DES_IRQ_LINK_EN);

	serdes->i2c_write_reg(client, RKLINK_DES_SINK_IRQ_EN, FIFO_UNDERRUN_IRQ_OUTPUT_EN |
			      FIFO_OVERFLOW_IRQ_OUTPUT_EN);
}

static void rkx120_linkrx_irq_disable(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val = 0;

	serdes->i2c_write_reg(client, DES_GRF_IRQ_EN, DES_IRQ_LINK_DIS);

	serdes->i2c_read_reg(client, RKLINK_DES_SINK_IRQ_EN, &val);
	val &= ~(FIFO_UNDERRUN_IRQ_OUTPUT_EN | FIFO_OVERFLOW_IRQ_OUTPUT_EN);
	serdes->i2c_write_reg(client, RKLINK_DES_SINK_IRQ_EN, val);
}

static void rkx120_linkrx_fifo_handler(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 value;

	serdes->i2c_read_reg(client, RKLINK_DES_FIFO_STATUS, &value);
	dev_err(serdes->dev, "des rklink fifo status:0x%x\n", value);

	if (value & AUDIO_FIFO_UNDERRUN)
		dev_err(serdes->dev, "linkrx audio fifo underrun\n");
	if (value & AUDIO_ORDER_UNDERRUN)
		dev_err(serdes->dev, "linkrx audio order underrun\n");
	if (value & VIDEO_DATA_FIFO_UNDERRUN)
		dev_err(serdes->dev, "linkrx video data fifo underrun\n");
	if (value & VIDEO_ORDER_UNDERRUN)
		dev_err(serdes->dev, "linkrx video order underrun\n");
	if (value & CMD_FIFO_UNDERRUN)
		dev_err(serdes->dev, "linkrx cmd fifo underrun\n");
	if (value & E1_ORDER_MIS)
		dev_err(serdes->dev, "linkrx e1 order miss\n");
	if (value & E0_ORDER_MIS)
		dev_err(serdes->dev, "linkrx e0 order miss\n");
	if (value & AUDIO_FIFO_OVERFLOW)
		dev_err(serdes->dev, "linkrx audio fifo overflow\n");
	if (value & AUDIO_ORDER_OVERFLOW)
		dev_err(serdes->dev, "linkrx audio order overflow\n");
	if (value & VIDEO_DATA_FIFO_OVERFLOW)
		dev_err(serdes->dev, "linkrx video data fifo overflow\n");
	if (value & VIDEO_ORDER_OVERFLOW)
		dev_err(serdes->dev, "linkrx video order overflow\n");
	if (value & CMD_FIFO_OVERFLOW)
		dev_err(serdes->dev, "linkrx cmd fifo overflow\n");

	serdes->i2c_write_reg(client, RKLINK_DES_FIFO_STATUS, value);
}

static void rkx120_linkrx_irq_handler(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 flag, value;
	int i = 0;

	serdes->i2c_read_reg(client, RKLINK_DES_SINK_IRQ_EN, &flag);
	flag &= COMP_NOT_ENOUGH_IRQ_FLAG | VIDEO_FM_IRQ_FLAG | AUDIO_FM_IRQ_FLAG |
		ORDER_MIS_IRQ_FLAG | FIFO_UNDERRUN_IRQ_FLAG | FIFO_OVERFLOW_IRQ_FLAG |
		PKT_LOSE_IRQ_FLAG | LAST_ERROR_IRQ_FLAG | ECC2BIT_ERROR_IRQ_FLAG |
		ECC1BIT_ERROR_IRQ_FLAG | CRC_ERROR_IRQ_FLAG;
	dev_info(serdes->dev, "linkrx irq flag:0x%08x\n", flag);
	while (flag) {
		switch (flag & BIT(i)) {
		case COMP_NOT_ENOUGH_IRQ_FLAG:
			break;
		case VIDEO_FM_IRQ_FLAG:
			break;
		case AUDIO_FM_IRQ_FLAG:
			break;
		case ORDER_MIS_IRQ_FLAG:
		case FIFO_UNDERRUN_IRQ_FLAG:
		case FIFO_OVERFLOW_IRQ_FLAG:
			flag &= ~(ORDER_MIS_IRQ_FLAG | FIFO_UNDERRUN_IRQ_FLAG |
				FIFO_OVERFLOW_IRQ_FLAG);
			rkx120_linkrx_fifo_handler(serdes, dev_id);
			break;
		case PKT_LOSE_IRQ_FLAG:
			/* clear pkt lost irq flag */
			serdes->i2c_read_reg(client, RKLINK_DES_LANE_ENGINE_CFG, &value);
			value |= LANE0_PKT_LOSE_NUM_CLR | LANE1_PKT_LOSE_NUM_CLR;
			serdes->i2c_write_reg(client, RKLINK_DES_LANE_ENGINE_CFG, value);
			break;
		case LAST_ERROR_IRQ_FLAG:
		case ECC2BIT_ERROR_IRQ_FLAG:
		case ECC1BIT_ERROR_IRQ_FLAG:
		case CRC_ERROR_IRQ_FLAG:
			flag &= ~(LAST_ERROR_IRQ_FLAG | ECC2BIT_ERROR_IRQ_FLAG |
				ECC1BIT_ERROR_IRQ_FLAG | CRC_ERROR_IRQ_FLAG);
			serdes->i2c_read_reg(client, RKLINK_DES_SINK_IRQ_EN, &value);
			dev_info(serdes->dev, "linkrx ecc crc result:0x%08x\n", value);
			/* clear ecc crc irq flag */
			serdes->i2c_write_reg(client, RKLINK_DES_SINK_IRQ_EN, value);
			break;
		default:
			break;
		}
		flag &= ~BIT(i);
		i++;
	}
}
static void rkx120_pcs_irq_enable(struct rk_serdes *serdes, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val = 0;

	val = pcs_id ? DES_IRQ_PCS1_EN : DES_IRQ_PCS0_EN;
	serdes->i2c_write_reg(client, DES_GRF_IRQ_EN, val);

	serdes->i2c_write_reg(client, PCS_REG30(pcs_id), DES_PCS_INI_EN(0xffff));
}

static void rkx120_pcs_irq_disable(struct rk_serdes *serdes, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val = 0;

	val = pcs_id ? DES_IRQ_PCS1_DIS : DES_IRQ_PCS0_DIS;
	serdes->i2c_write_reg(client, DES_GRF_IRQ_EN, val);

	serdes->i2c_write_reg(client, PCS_REG30(pcs_id), DES_PCS_INI_EN(0));
}

static void rkx120_pcs_irq_handler(struct rk_serdes *serdes, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 value;

	serdes->i2c_read_reg(client, PCS_REG20(pcs_id), &value);
	dev_info(serdes->dev, "des pcs%d fatal status:0x%08x\n", pcs_id, value);

	/* clear fatal status */
	serdes->i2c_write_reg(client, PCS_REG10(pcs_id), 0xffffffff);
	serdes->i2c_write_reg(client, PCS_REG10(pcs_id), 0xffff0000);
}

static void rkx120_pma_irq_enable(struct rk_serdes *serdes, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val = 0;

	val = pcs_id ? DES_IRQ_PMA_ADAPT1_EN : DES_IRQ_PMA_ADAPT0_EN;
	serdes->i2c_write_reg(client, DES_GRF_IRQ_EN, val);

	serdes->i2c_write_reg(client, DES_PMA_IRQ_EN(pcs_id), FORCE_INITIAL_IRQ_EN |
			      RX_RDY_NEG_IRQ_EN | RX_LOS_IRQ_EN | RX_RDY_TIMEOUT_IRQ_EN |
			      PLL_LOCK_TIMEOUT_IRQ_EN);
}

static void rkx120_pma_irq_disable(struct rk_serdes *serdes, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 val = 0;

	val = pcs_id ? DES_IRQ_PMA_ADAPT1_DIS : DES_IRQ_PMA_ADAPT0_DIS;
	serdes->i2c_write_reg(client, DES_GRF_IRQ_EN, val);

	serdes->i2c_write_reg(client, DES_PMA_IRQ_EN(pcs_id), 0);
}

static void rkx120_pma_irq_handler(struct rk_serdes *serdes, u8 pcs_id, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 value;

	serdes->i2c_read_reg(client, DES_PMA_IRQ_STATUS(pcs_id), &value);
	dev_info(serdes->dev, "des pma%d irq status:0x%08x\n", pcs_id, value);

	if (value & FORCE_INITIAL_IRQ_STATUS)
		dev_info(serdes->dev, "des pma trig force initial pulse status\n");
	else if (value & RX_RDY_NEG_IRQ_STATUS)
		dev_info(serdes->dev, "des pma trig rx rdy neg status\n");
	else if (value & RX_LOS_IRQ_STATUS)
		dev_info(serdes->dev, "des pma trig rx los status\n");
	else if (value & RX_RDY_TIMEOUT_IRQ_STATUS)
		dev_info(serdes->dev, "des pma trig rx rdy timeout status\n");
	else if (value & PLL_LOCK_TIMEOUT_IRQ_STATUS)
		dev_info(serdes->dev, "des pma trig pll lock timeout status\n");

	/* clear pma irq status */
	serdes->i2c_write_reg(client, DES_PMA_IRQ_STATUS(pcs_id), value);
}

static void rkx120_remote_irq_enable(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	if (serdes->stream_type == STREAM_CAMERA) {
		serdes->i2c_write_reg(client, DES_GRF_IRQ_EN, DES_IRQ_REMOTE_EN);
		rkx110_irq_enable(serdes, DEVICE_REMOTE0);
	}
}

static void rkx120_remote_irq_disable(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;

	if (serdes->stream_type == STREAM_CAMERA) {
		serdes->i2c_write_reg(client, DES_GRF_IRQ_EN, DES_IRQ_REMOTE_DIS);
		rkx110_irq_disable(serdes, DEVICE_REMOTE0);
	}
}

static void rkx120_remote_irq_handler(struct rk_serdes *serdes, u8 dev_id)
{
	if (serdes->stream_type == STREAM_CAMERA)
		rkx110_irq_handler(serdes, DEVICE_REMOTE0);
}

void rkx120_irq_enable(struct rk_serdes *serdes, u8 dev_id)
{
	/* enable pcs irq */
	rkx120_pcs_irq_enable(serdes, 0, dev_id);

	/* enable efuse irq */

	/* enable gpio irq */

	/* enable csitx irq */

	/* enable mipi dsi host irq */

	/* enable pma adapt irq */
	rkx120_pma_irq_enable(serdes, 0, dev_id);

	/* enable remote irq and other lane irq */
	rkx120_remote_irq_enable(serdes, dev_id);

	/* enable pwm irq */

	/* enable dvp tx irq */

	/* enable link irq */
	rkx120_linkrx_irq_enable(serdes, dev_id);

	/* enable ext irq */

	/* enable ext irq */
}

void rkx120_irq_disable(struct rk_serdes *serdes, u8 dev_id)
{
	/* disable pcs irq */
	rkx120_pcs_irq_disable(serdes, 0, dev_id);

	/* disable efuse irq */

	/* disable gpio irq */

	/* disable csitx irq */

	/* disable mipi dsi host irq */

	/* disable pma adapt irq */
	rkx120_pma_irq_disable(serdes, 0, dev_id);

	/* disable remote irq */
	rkx120_remote_irq_disable(serdes, dev_id);

	/* disable pwm irq */

	/* disable dvp tx irq */

	/* disable link irq */
	rkx120_linkrx_irq_disable(serdes, dev_id);

	/* disable ext irq */
}

int rkx120_irq_handler(struct rk_serdes *serdes, u8 dev_id)
{
	struct i2c_client *client = serdes->chip[dev_id].client;
	u32 status = 0;
	u32 mask = 0;
	u32 i = 0;

	serdes->i2c_read_reg(client, DES_GRF_IRQ_EN, &mask);
	serdes->i2c_read_reg(client, DES_GRF_IRQ_STATUS, &status);
	dev_info(serdes->dev, "dev%d get the des irq status:0x%08x\n", dev_id, status);
	status &= mask;

	while (status) {
		switch (status & BIT(i)) {
		case DES_IRQ_PCS0:
			rkx120_pcs_irq_handler(serdes, 0, dev_id);
			break;
		case DES_IRQ_PCS1:
			rkx120_pcs_irq_handler(serdes, 1, dev_id);
			break;
		case DES_IRQ_EFUSE:
			/* TBD */
			break;
		case DES_IRQ_GPIO0:
			/* TBD */
			break;
		case DES_IRQ_GPIO1:
			/* TBD */
			break;
		case DES_IRQ_CSITX0:
			/* TBD */
			break;
		case DES_IRQ_CSITX1:
			/* TBD */
			break;
		case DES_IRQ_MIPI_DSI_HOST:
			/* TBD */
			break;
		case DES_IRQ_PMA_ADAPT0:
			rkx120_pma_irq_handler(serdes, 0, dev_id);
			break;
		case DES_IRQ_PMA_ADAPT1:
			rkx120_pma_irq_handler(serdes, 1, dev_id);
			break;
		case DES_IRQ_REMOTE:
			rkx120_remote_irq_handler(serdes, dev_id);
			break;
		case DES_IRQ_PWM:
			/* TBD */
			break;
		case DES_IRQ_DVP_TX:
			/* TBD */
			break;
		case DES_IRQ_LINK:
			rkx120_linkrx_irq_handler(serdes, dev_id);
			break;
		case DES_IRQ_EXT:
			/* TBD */
			break;
		case DES_IRQ_OTHER_LANE:
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

