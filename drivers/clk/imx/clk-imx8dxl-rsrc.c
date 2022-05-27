// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2019~2020 NXP
 */

#include <dt-bindings/firmware/imx/rsrc.h>

#include "clk-scu.h"

/* Keep sorted in the ascending order */
static u32 imx8dxl_clk_scu_rsrc_table[] = {
	IMX_SC_R_SPI_0,
	IMX_SC_R_SPI_1,
	IMX_SC_R_SPI_2,
	IMX_SC_R_SPI_3,
	IMX_SC_R_UART_0,
	IMX_SC_R_UART_1,
	IMX_SC_R_UART_2,
	IMX_SC_R_UART_3,
	IMX_SC_R_I2C_0,
	IMX_SC_R_I2C_1,
	IMX_SC_R_I2C_2,
	IMX_SC_R_I2C_3,
	IMX_SC_R_ADC_0,
	IMX_SC_R_FTM_0,
	IMX_SC_R_FTM_1,
	IMX_SC_R_CAN_0,
	IMX_SC_R_LCD_0,
	IMX_SC_R_LCD_0_PWM_0,
	IMX_SC_R_PWM_0,
	IMX_SC_R_PWM_1,
	IMX_SC_R_PWM_2,
	IMX_SC_R_PWM_3,
	IMX_SC_R_PWM_4,
	IMX_SC_R_PWM_5,
	IMX_SC_R_PWM_6,
	IMX_SC_R_PWM_7,
	IMX_SC_R_GPT_0,
	IMX_SC_R_GPT_1,
	IMX_SC_R_GPT_2,
	IMX_SC_R_GPT_3,
	IMX_SC_R_GPT_4,
	IMX_SC_R_FSPI_0,
	IMX_SC_R_FSPI_1,
	IMX_SC_R_SDHC_0,
	IMX_SC_R_SDHC_1,
	IMX_SC_R_SDHC_2,
	IMX_SC_R_ENET_0,
	IMX_SC_R_ENET_1,
	IMX_SC_R_MLB_0,
	IMX_SC_R_USB_1,
	IMX_SC_R_NAND,
	IMX_SC_R_M4_0_I2C,
	IMX_SC_R_M4_0_UART,
	IMX_SC_R_ELCDIF_PLL,
	IMX_SC_R_AUDIO_PLL_0,
	IMX_SC_R_AUDIO_PLL_1,
	IMX_SC_R_AUDIO_CLK_0,
	IMX_SC_R_AUDIO_CLK_1,
	IMX_SC_R_A35
};

const struct imx_clk_scu_rsrc_table imx_clk_scu_rsrc_imx8dxl = {
	.rsrc = imx8dxl_clk_scu_rsrc_table,
	.num = ARRAY_SIZE(imx8dxl_clk_scu_rsrc_table),
};
