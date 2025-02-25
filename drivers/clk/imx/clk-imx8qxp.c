// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2018-2021 NXP
 *	Dong Aisheng <aisheng.dong@nxp.com>
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "clk-scu.h"

#include <dt-bindings/firmware/imx/rsrc.h>

static const char *dc0_sels[] = {
	"clk_dummy",
	"clk_dummy",
	"dc0_pll0_clk",
	"dc0_pll1_clk",
	"dc0_bypass0_clk",
};

static const char * const dc1_sels[] = {
	"clk_dummy",
	"clk_dummy",
	"dc1_pll0_clk",
	"dc1_pll1_clk",
	"dc1_bypass0_clk",
};

static const char * const enet0_rgmii_txc_sels[] = {
	"enet0_ref_div",
	"clk_dummy",
};

static const char * const enet1_rgmii_txc_sels[] = {
	"enet1_ref_div",
	"clk_dummy",
};

static const char * const hdmi_sels[] = {
	"clk_dummy",
	"hdmi_dig_pll_clk",
	"clk_dummy",
	"clk_dummy",
	"hdmi_av_pll_clk",
};

static const char * const hdmi_rx_sels[] = {
	"clk_dummy",
	"hdmi_rx_dig_pll_clk",
	"clk_dummy",
	"clk_dummy",
	"hdmi_rx_bypass_clk",
};

static const char * const lcd_pxl_sels[] = {
	"clk_dummy",
	"clk_dummy",
	"clk_dummy",
	"clk_dummy",
	"lcd_pxl_bypass_div_clk",
};

static const char *const lvds0_sels[] = {
	"clk_dummy",
	"clk_dummy",
	"clk_dummy",
	"clk_dummy",
	"mipi0_lvds_bypass_clk",
};

static const char *const lvds1_sels[] = {
	"clk_dummy",
	"clk_dummy",
	"clk_dummy",
	"clk_dummy",
	"mipi1_lvds_bypass_clk",
};

static const char * const mipi_sels[] = {
	"clk_dummy",
	"clk_dummy",
	"mipi_pll_div2_clk",
	"clk_dummy",
	"clk_dummy",
};

static const char * const lcd_sels[] = {
	"clk_dummy",
	"clk_dummy",
	"clk_dummy",
	"clk_dummy",
	"elcdif_pll",
};

static const char * const pi_pll0_sels[] = {
	"clk_dummy",
	"pi_dpll_clk",
	"clk_dummy",
	"clk_dummy",
	"clk_dummy",
};

static int imx8qxp_clk_probe(struct platform_device *pdev)
{
	struct device_node *ccm_node = pdev->dev.of_node;
	const struct imx_clk_scu_rsrc_table *rsrc_table;
	int ret;

	rsrc_table = of_device_get_match_data(&pdev->dev);
	ret = imx_clk_scu_init(ccm_node, rsrc_table);
	if (ret)
		return ret;

	/* ARM core */
	imx_clk_scu("a35_clk", IMX_SC_R_A35, IMX_SC_PM_CLK_CPU);
	imx_clk_scu("a53_clk", IMX_SC_R_A53, IMX_SC_PM_CLK_CPU);
	imx_clk_scu("a72_clk", IMX_SC_R_A72, IMX_SC_PM_CLK_CPU);

	/* LSIO SS */
	imx_clk_scu("pwm0_clk", IMX_SC_R_PWM_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("pwm1_clk", IMX_SC_R_PWM_1, IMX_SC_PM_CLK_PER);
	imx_clk_scu("pwm2_clk", IMX_SC_R_PWM_2, IMX_SC_PM_CLK_PER);
	imx_clk_scu("pwm3_clk", IMX_SC_R_PWM_3, IMX_SC_PM_CLK_PER);
	imx_clk_scu("pwm4_clk", IMX_SC_R_PWM_4, IMX_SC_PM_CLK_PER);
	imx_clk_scu("pwm5_clk", IMX_SC_R_PWM_5, IMX_SC_PM_CLK_PER);
	imx_clk_scu("pwm6_clk", IMX_SC_R_PWM_6, IMX_SC_PM_CLK_PER);
	imx_clk_scu("pwm7_clk", IMX_SC_R_PWM_7, IMX_SC_PM_CLK_PER);
	imx_clk_scu("gpt0_clk", IMX_SC_R_GPT_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("gpt1_clk", IMX_SC_R_GPT_1, IMX_SC_PM_CLK_PER);
	imx_clk_scu("gpt2_clk", IMX_SC_R_GPT_2, IMX_SC_PM_CLK_PER);
	imx_clk_scu("gpt3_clk", IMX_SC_R_GPT_3, IMX_SC_PM_CLK_PER);
	imx_clk_scu("gpt4_clk", IMX_SC_R_GPT_4, IMX_SC_PM_CLK_PER);
	imx_clk_scu("fspi0_clk", IMX_SC_R_FSPI_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("fspi1_clk", IMX_SC_R_FSPI_1, IMX_SC_PM_CLK_PER);

	/* DMA SS */
	imx_clk_scu("uart0_clk", IMX_SC_R_UART_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("uart1_clk", IMX_SC_R_UART_1, IMX_SC_PM_CLK_PER);
	imx_clk_scu("uart2_clk", IMX_SC_R_UART_2, IMX_SC_PM_CLK_PER);
	imx_clk_scu("uart3_clk", IMX_SC_R_UART_3, IMX_SC_PM_CLK_PER);
	imx_clk_scu("uart4_clk", IMX_SC_R_UART_4, IMX_SC_PM_CLK_PER);
	imx_clk_scu("sim0_clk",  IMX_SC_R_EMVSIM_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("spi0_clk",  IMX_SC_R_SPI_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("spi1_clk",  IMX_SC_R_SPI_1, IMX_SC_PM_CLK_PER);
	imx_clk_scu("spi2_clk",  IMX_SC_R_SPI_2, IMX_SC_PM_CLK_PER);
	imx_clk_scu("spi3_clk",  IMX_SC_R_SPI_3, IMX_SC_PM_CLK_PER);
	imx_clk_scu("can0_clk",  IMX_SC_R_CAN_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("can1_clk",  IMX_SC_R_CAN_1, IMX_SC_PM_CLK_PER);
	imx_clk_scu("can2_clk",  IMX_SC_R_CAN_2, IMX_SC_PM_CLK_PER);
	imx_clk_scu("i2c0_clk",  IMX_SC_R_I2C_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("i2c1_clk",  IMX_SC_R_I2C_1, IMX_SC_PM_CLK_PER);
	imx_clk_scu("i2c2_clk",  IMX_SC_R_I2C_2, IMX_SC_PM_CLK_PER);
	imx_clk_scu("i2c3_clk",  IMX_SC_R_I2C_3, IMX_SC_PM_CLK_PER);
	imx_clk_scu("i2c4_clk",  IMX_SC_R_I2C_4, IMX_SC_PM_CLK_PER);
	imx_clk_scu("ftm0_clk",  IMX_SC_R_FTM_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("ftm1_clk",  IMX_SC_R_FTM_1, IMX_SC_PM_CLK_PER);
	imx_clk_scu("adc0_clk",  IMX_SC_R_ADC_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("adc1_clk",  IMX_SC_R_ADC_1, IMX_SC_PM_CLK_PER);
	imx_clk_scu("pwm_clk",   IMX_SC_R_LCD_0_PWM_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("elcdif_pll", IMX_SC_R_ELCDIF_PLL, IMX_SC_PM_CLK_PLL);
	imx_clk_scu2("lcd_clk", lcd_sels, ARRAY_SIZE(lcd_sels), IMX_SC_R_LCD_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("lcd_pxl_bypass_div_clk", IMX_SC_R_LCD_0, IMX_SC_PM_CLK_BYPASS);
	imx_clk_scu2("lcd_pxl_clk", lcd_pxl_sels, ARRAY_SIZE(lcd_pxl_sels), IMX_SC_R_LCD_0, IMX_SC_PM_CLK_MISC0);

	/* Audio SS */
	imx_clk_scu("audio_pll0_clk", IMX_SC_R_AUDIO_PLL_0, IMX_SC_PM_CLK_PLL);
	imx_clk_scu("audio_pll1_clk", IMX_SC_R_AUDIO_PLL_1, IMX_SC_PM_CLK_PLL);
	imx_clk_scu("audio_pll_div_clk0_clk", IMX_SC_R_AUDIO_PLL_0, IMX_SC_PM_CLK_MISC0);
	imx_clk_scu("audio_pll_div_clk1_clk", IMX_SC_R_AUDIO_PLL_1, IMX_SC_PM_CLK_MISC0);
	imx_clk_scu("audio_rec_clk0_clk", IMX_SC_R_AUDIO_PLL_0, IMX_SC_PM_CLK_MISC1);
	imx_clk_scu("audio_rec_clk1_clk", IMX_SC_R_AUDIO_PLL_1, IMX_SC_PM_CLK_MISC1);

	/* Connectivity */
	imx_clk_scu("sdhc0_clk", IMX_SC_R_SDHC_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("sdhc1_clk", IMX_SC_R_SDHC_1, IMX_SC_PM_CLK_PER);
	imx_clk_scu("sdhc2_clk", IMX_SC_R_SDHC_2, IMX_SC_PM_CLK_PER);
	imx_clk_scu("enet0_root_clk", IMX_SC_R_ENET_0, IMX_SC_PM_CLK_PER);
	imx_clk_divider_gpr_scu("enet0_ref_div", "enet0_root_clk", IMX_SC_R_ENET_0, IMX_SC_C_CLKDIV);
	imx_clk_mux_gpr_scu("enet0_rgmii_txc_sel", enet0_rgmii_txc_sels, ARRAY_SIZE(enet0_rgmii_txc_sels), IMX_SC_R_ENET_0, IMX_SC_C_TXCLK);
	imx_clk_scu("enet0_bypass_clk", IMX_SC_R_ENET_0, IMX_SC_PM_CLK_BYPASS);
	imx_clk_gate_gpr_scu("enet0_ref_50_clk", "clk_dummy", IMX_SC_R_ENET_0, IMX_SC_C_DISABLE_50, true);
	imx_clk_scu("enet0_rgmii_rx_clk", IMX_SC_R_ENET_0, IMX_SC_PM_CLK_MISC0);
	imx_clk_scu("enet1_root_clk", IMX_SC_R_ENET_1, IMX_SC_PM_CLK_PER);
	imx_clk_divider_gpr_scu("enet1_ref_div", "enet1_root_clk", IMX_SC_R_ENET_1, IMX_SC_C_CLKDIV);
	imx_clk_mux_gpr_scu("enet1_rgmii_txc_sel", enet1_rgmii_txc_sels, ARRAY_SIZE(enet1_rgmii_txc_sels), IMX_SC_R_ENET_1, IMX_SC_C_TXCLK);
	imx_clk_scu("enet1_bypass_clk", IMX_SC_R_ENET_1, IMX_SC_PM_CLK_BYPASS);
	imx_clk_gate_gpr_scu("enet1_ref_50_clk", "clk_dummy", IMX_SC_R_ENET_1, IMX_SC_C_DISABLE_50, true);
	imx_clk_scu("enet1_rgmii_rx_clk", IMX_SC_R_ENET_1, IMX_SC_PM_CLK_MISC0);
	imx_clk_scu("gpmi_io_clk", IMX_SC_R_NAND, IMX_SC_PM_CLK_MST_BUS);
	imx_clk_scu("gpmi_bch_clk", IMX_SC_R_NAND, IMX_SC_PM_CLK_PER);
	imx_clk_scu("usb3_aclk_div", IMX_SC_R_USB_2, IMX_SC_PM_CLK_PER);
	imx_clk_scu("usb3_bus_div", IMX_SC_R_USB_2, IMX_SC_PM_CLK_MST_BUS);
	imx_clk_scu("usb3_lpm_div", IMX_SC_R_USB_2, IMX_SC_PM_CLK_MISC);

	/* Display controller SS */
	imx_clk_scu("dc0_pll0_clk", IMX_SC_R_DC_0_PLL_0, IMX_SC_PM_CLK_PLL);
	imx_clk_scu("dc0_pll1_clk", IMX_SC_R_DC_0_PLL_1, IMX_SC_PM_CLK_PLL);
	imx_clk_scu("dc0_bypass0_clk", IMX_SC_R_DC_0_VIDEO0, IMX_SC_PM_CLK_BYPASS);
	imx_clk_scu2("dc0_disp0_clk", dc0_sels, ARRAY_SIZE(dc0_sels), IMX_SC_R_DC_0, IMX_SC_PM_CLK_MISC0);
	imx_clk_scu2("dc0_disp1_clk", dc0_sels, ARRAY_SIZE(dc0_sels), IMX_SC_R_DC_0, IMX_SC_PM_CLK_MISC1);
	imx_clk_scu("dc0_bypass1_clk", IMX_SC_R_DC_0_VIDEO1, IMX_SC_PM_CLK_BYPASS);

	imx_clk_scu("dc1_pll0_clk", IMX_SC_R_DC_1_PLL_0, IMX_SC_PM_CLK_PLL);
	imx_clk_scu("dc1_pll1_clk", IMX_SC_R_DC_1_PLL_1, IMX_SC_PM_CLK_PLL);
	imx_clk_scu("dc1_bypass0_clk", IMX_SC_R_DC_1_VIDEO0, IMX_SC_PM_CLK_BYPASS);
	imx_clk_scu2("dc1_disp0_clk", dc1_sels, ARRAY_SIZE(dc1_sels), IMX_SC_R_DC_1, IMX_SC_PM_CLK_MISC0);
	imx_clk_scu2("dc1_disp1_clk", dc1_sels, ARRAY_SIZE(dc1_sels), IMX_SC_R_DC_1, IMX_SC_PM_CLK_MISC1);
	imx_clk_scu("dc1_bypass1_clk", IMX_SC_R_DC_1_VIDEO1, IMX_SC_PM_CLK_BYPASS);

	/* MIPI-LVDS SS */
	imx_clk_scu("mipi0_bypass_clk", IMX_SC_R_MIPI_0, IMX_SC_PM_CLK_BYPASS);
	imx_clk_scu("mipi0_pixel_clk", IMX_SC_R_MIPI_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("mipi0_lvds_bypass_clk", IMX_SC_R_LVDS_0, IMX_SC_PM_CLK_BYPASS);
	imx_clk_scu2("mipi0_lvds_pixel_clk", lvds0_sels, ARRAY_SIZE(lvds0_sels), IMX_SC_R_LVDS_0, IMX_SC_PM_CLK_MISC2);
	imx_clk_scu2("mipi0_lvds_phy_clk", lvds0_sels, ARRAY_SIZE(lvds0_sels), IMX_SC_R_LVDS_0, IMX_SC_PM_CLK_MISC3);
	imx_clk_scu2("mipi0_dsi_tx_esc_clk", mipi_sels, ARRAY_SIZE(mipi_sels), IMX_SC_R_MIPI_0, IMX_SC_PM_CLK_MST_BUS);
	imx_clk_scu2("mipi0_dsi_rx_esc_clk", mipi_sels, ARRAY_SIZE(mipi_sels), IMX_SC_R_MIPI_0, IMX_SC_PM_CLK_SLV_BUS);
	imx_clk_scu2("mipi0_dsi_phy_clk", mipi_sels, ARRAY_SIZE(mipi_sels), IMX_SC_R_MIPI_0, IMX_SC_PM_CLK_PHY);
	imx_clk_scu("mipi0_i2c0_clk", IMX_SC_R_MIPI_0_I2C_0, IMX_SC_PM_CLK_MISC2);
	imx_clk_scu("mipi0_i2c1_clk", IMX_SC_R_MIPI_0_I2C_1, IMX_SC_PM_CLK_MISC2);
	imx_clk_scu("mipi0_pwm0_clk", IMX_SC_R_MIPI_0_PWM_0, IMX_SC_PM_CLK_PER);

	imx_clk_scu("mipi1_bypass_clk", IMX_SC_R_MIPI_1, IMX_SC_PM_CLK_BYPASS);
	imx_clk_scu("mipi1_pixel_clk", IMX_SC_R_MIPI_1, IMX_SC_PM_CLK_PER);
	imx_clk_scu("mipi1_lvds_bypass_clk", IMX_SC_R_LVDS_1, IMX_SC_PM_CLK_BYPASS);
	imx_clk_scu2("mipi1_lvds_pixel_clk", lvds1_sels, ARRAY_SIZE(lvds1_sels), IMX_SC_R_LVDS_1, IMX_SC_PM_CLK_MISC2);
	imx_clk_scu2("mipi1_lvds_phy_clk", lvds1_sels, ARRAY_SIZE(lvds1_sels), IMX_SC_R_LVDS_1, IMX_SC_PM_CLK_MISC3);

	imx_clk_scu2("mipi1_dsi_tx_esc_clk", mipi_sels, ARRAY_SIZE(mipi_sels), IMX_SC_R_MIPI_1, IMX_SC_PM_CLK_MST_BUS);
	imx_clk_scu2("mipi1_dsi_rx_esc_clk", mipi_sels, ARRAY_SIZE(mipi_sels), IMX_SC_R_MIPI_1, IMX_SC_PM_CLK_SLV_BUS);
	imx_clk_scu2("mipi1_dsi_phy_clk", mipi_sels, ARRAY_SIZE(mipi_sels), IMX_SC_R_MIPI_1, IMX_SC_PM_CLK_PHY);
	imx_clk_scu("mipi1_i2c0_clk", IMX_SC_R_MIPI_1_I2C_0, IMX_SC_PM_CLK_MISC2);
	imx_clk_scu("mipi1_i2c1_clk", IMX_SC_R_MIPI_1_I2C_1, IMX_SC_PM_CLK_MISC2);
	imx_clk_scu("mipi1_pwm0_clk", IMX_SC_R_MIPI_1_PWM_0, IMX_SC_PM_CLK_PER);

	imx_clk_scu("lvds0_i2c0_clk", IMX_SC_R_LVDS_0_I2C_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("lvds0_i2c1_clk", IMX_SC_R_LVDS_0_I2C_1, IMX_SC_PM_CLK_PER);
	imx_clk_scu("lvds0_pwm0_clk", IMX_SC_R_LVDS_0_PWM_0, IMX_SC_PM_CLK_PER);

	imx_clk_scu("lvds1_i2c0_clk", IMX_SC_R_LVDS_1_I2C_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("lvds1_i2c1_clk", IMX_SC_R_LVDS_1_I2C_1, IMX_SC_PM_CLK_PER);
	imx_clk_scu("lvds1_pwm0_clk", IMX_SC_R_LVDS_1_PWM_0, IMX_SC_PM_CLK_PER);

	/* MIPI CSI SS */
	imx_clk_scu("mipi_csi0_core_clk", IMX_SC_R_CSI_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("mipi_csi0_esc_clk",  IMX_SC_R_CSI_0, IMX_SC_PM_CLK_MISC);
	imx_clk_scu("mipi_csi0_i2c0_clk", IMX_SC_R_CSI_0_I2C_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("mipi_csi0_pwm0_clk", IMX_SC_R_CSI_0_PWM_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("mipi_csi1_core_clk", IMX_SC_R_CSI_1, IMX_SC_PM_CLK_PER);
	imx_clk_scu("mipi_csi1_esc_clk",  IMX_SC_R_CSI_1, IMX_SC_PM_CLK_MISC);
	imx_clk_scu("mipi_csi1_i2c0_clk", IMX_SC_R_CSI_1_I2C_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("mipi_csi1_pwm0_clk", IMX_SC_R_CSI_1_PWM_0, IMX_SC_PM_CLK_PER);

	/* Parallel Interface SS */
	imx_clk_scu("pi_dpll_clk", IMX_SC_R_PI_0_PLL, IMX_SC_PM_CLK_PLL);
	imx_clk_scu2("pi_per_div_clk", pi_pll0_sels, ARRAY_SIZE(pi_pll0_sels), IMX_SC_R_PI_0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("pi_mclk_div_clk", IMX_SC_R_PI_0, IMX_SC_PM_CLK_MISC0);
	imx_clk_scu("pi_i2c0_div_clk", IMX_SC_R_PI_0_I2C_0, IMX_SC_PM_CLK_PER);

	/* GPU SS */
	imx_clk_scu("gpu_core0_clk",	 IMX_SC_R_GPU_0_PID0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("gpu_shader0_clk", IMX_SC_R_GPU_0_PID0, IMX_SC_PM_CLK_MISC);

	imx_clk_scu("gpu_core1_clk",	 IMX_SC_R_GPU_1_PID0, IMX_SC_PM_CLK_PER);
	imx_clk_scu("gpu_shader1_clk", IMX_SC_R_GPU_1_PID0, IMX_SC_PM_CLK_MISC);

	 /* CM40 SS */
	imx_clk_scu("cm40_i2c_div", IMX_SC_R_M4_0_I2C, IMX_SC_PM_CLK_PER);
	imx_clk_scu("cm40_lpuart_div", IMX_SC_R_M4_0_UART, IMX_SC_PM_CLK_PER);

	 /* CM41 SS */
	imx_clk_scu("cm41_i2c_div", IMX_SC_R_M4_1_I2C, IMX_SC_PM_CLK_PER);

	/* HDMI TX SS */
	imx_clk_scu("hdmi_dig_pll_clk",  IMX_SC_R_HDMI_PLL_0, IMX_SC_PM_CLK_PLL);
	imx_clk_scu("hdmi_av_pll_clk", IMX_SC_R_HDMI_PLL_1, IMX_SC_PM_CLK_PLL);
	imx_clk_scu2("hdmi_pixel_mux_clk", hdmi_sels, ARRAY_SIZE(hdmi_sels), IMX_SC_R_HDMI, IMX_SC_PM_CLK_MISC0);
	imx_clk_scu2("hdmi_pixel_link_clk", hdmi_sels, ARRAY_SIZE(hdmi_sels), IMX_SC_R_HDMI, IMX_SC_PM_CLK_MISC1);
	imx_clk_scu("hdmi_ipg_clk", IMX_SC_R_HDMI, IMX_SC_PM_CLK_MISC4);
	imx_clk_scu("hdmi_i2c0_clk", IMX_SC_R_HDMI_I2C_0, IMX_SC_PM_CLK_MISC2);
	imx_clk_scu("hdmi_hdp_core_clk", IMX_SC_R_HDMI, IMX_SC_PM_CLK_MISC2);
	imx_clk_scu2("hdmi_pxl_clk", hdmi_sels, ARRAY_SIZE(hdmi_sels), IMX_SC_R_HDMI, IMX_SC_PM_CLK_MISC3);
	imx_clk_scu("hdmi_i2s_bypass_clk", IMX_SC_R_HDMI_I2S, IMX_SC_PM_CLK_BYPASS);
	imx_clk_scu("hdmi_i2s_clk", IMX_SC_R_HDMI_I2S, IMX_SC_PM_CLK_MISC0);

	/* HDMI RX SS */
	imx_clk_scu("hdmi_rx_i2s_bypass_clk", IMX_SC_R_HDMI_RX_BYPASS, IMX_SC_PM_CLK_MISC0);
	imx_clk_scu("hdmi_rx_spdif_bypass_clk", IMX_SC_R_HDMI_RX_BYPASS, IMX_SC_PM_CLK_MISC1);
	imx_clk_scu("hdmi_rx_bypass_clk", IMX_SC_R_HDMI_RX_BYPASS, IMX_SC_PM_CLK_MISC2);
	imx_clk_scu("hdmi_rx_i2c0_clk", IMX_SC_R_HDMI_RX_I2C_0, IMX_SC_PM_CLK_MISC2);
	imx_clk_scu("hdmi_rx_pwm_clk", IMX_SC_R_HDMI_RX_PWM_0, IMX_SC_PM_CLK_MISC2);
	imx_clk_scu("hdmi_rx_spdif_clk", IMX_SC_R_HDMI_RX, IMX_SC_PM_CLK_MISC0);
	imx_clk_scu2("hdmi_rx_hd_ref_clk", hdmi_rx_sels, ARRAY_SIZE(hdmi_rx_sels), IMX_SC_R_HDMI_RX, IMX_SC_PM_CLK_MISC1);
	imx_clk_scu2("hdmi_rx_hd_core_clk", hdmi_rx_sels, ARRAY_SIZE(hdmi_rx_sels), IMX_SC_R_HDMI_RX, IMX_SC_PM_CLK_MISC2);
	imx_clk_scu2("hdmi_rx_pxl_clk", hdmi_rx_sels, ARRAY_SIZE(hdmi_rx_sels), IMX_SC_R_HDMI_RX, IMX_SC_PM_CLK_MISC3);
	imx_clk_scu("hdmi_rx_i2s_clk", IMX_SC_R_HDMI_RX, IMX_SC_PM_CLK_MISC4);

	ret = of_clk_add_hw_provider(ccm_node, imx_scu_of_clk_src_get, imx_scu_clks);
	if (ret)
		imx_clk_scu_unregister();

	return ret;
}

static const struct of_device_id imx8qxp_match[] = {
	{ .compatible = "fsl,scu-clk", },
	{ .compatible = "fsl,imx8dxl-clk", &imx_clk_scu_rsrc_imx8dxl, },
	{ .compatible = "fsl,imx8qxp-clk", &imx_clk_scu_rsrc_imx8qxp, },
	{ .compatible = "fsl,imx8qm-clk", &imx_clk_scu_rsrc_imx8qm, },
	{ /* sentinel */ }
};

static struct platform_driver imx8qxp_clk_driver = {
	.driver = {
		.name = "imx8qxp-clk",
		.of_match_table = imx8qxp_match,
		.suppress_bind_attrs = true,
	},
	.probe = imx8qxp_clk_probe,
};
module_platform_driver(imx8qxp_clk_driver);

MODULE_AUTHOR("Aisheng Dong <aisheng.dong@nxp.com>");
MODULE_DESCRIPTION("NXP i.MX8QXP clock driver");
MODULE_LICENSE("GPL v2");
