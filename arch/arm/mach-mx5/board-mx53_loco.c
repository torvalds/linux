/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/init.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/iomux-mx53.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include "crm_regs.h"
#include "devices-imx53.h"

#define MX53_LOCO_POWER			IMX_GPIO_NR(1, 8)
#define MX53_LOCO_UI1			IMX_GPIO_NR(2, 14)
#define MX53_LOCO_UI2			IMX_GPIO_NR(2, 15)
#define LOCO_FEC_PHY_RST		IMX_GPIO_NR(7, 6)

static iomux_v3_cfg_t mx53_loco_pads[] = {
	/* FEC */
	MX53_PAD_FEC_MDC__FEC_MDC,
	MX53_PAD_FEC_MDIO__FEC_MDIO,
	MX53_PAD_FEC_REF_CLK__FEC_TX_CLK,
	MX53_PAD_FEC_RX_ER__FEC_RX_ER,
	MX53_PAD_FEC_CRS_DV__FEC_RX_DV,
	MX53_PAD_FEC_RXD1__FEC_RDATA_1,
	MX53_PAD_FEC_RXD0__FEC_RDATA_0,
	MX53_PAD_FEC_TX_EN__FEC_TX_EN,
	MX53_PAD_FEC_TXD1__FEC_TDATA_1,
	MX53_PAD_FEC_TXD0__FEC_TDATA_0,
	/* FEC_nRST */
	MX53_PAD_PATA_DA_0__GPIO7_6,
	/* FEC_nINT */
	MX53_PAD_PATA_DATA4__GPIO2_4,
	/* AUDMUX5 */
	MX53_PAD_KEY_COL0__AUDMUX_AUD5_TXC,
	MX53_PAD_KEY_ROW0__AUDMUX_AUD5_TXD,
	MX53_PAD_KEY_COL1__AUDMUX_AUD5_TXFS,
	MX53_PAD_KEY_ROW1__AUDMUX_AUD5_RXD,
	/* I2C2 */
	MX53_PAD_KEY_COL3__I2C2_SCL,
	MX53_PAD_KEY_ROW3__I2C2_SDA,
	/* SD1 */
	MX53_PAD_SD1_CMD__ESDHC1_CMD,
	MX53_PAD_SD1_CLK__ESDHC1_CLK,
	MX53_PAD_SD1_DATA0__ESDHC1_DAT0,
	MX53_PAD_SD1_DATA1__ESDHC1_DAT1,
	MX53_PAD_SD1_DATA2__ESDHC1_DAT2,
	MX53_PAD_SD1_DATA3__ESDHC1_DAT3,
	/* SD3 */
	MX53_PAD_PATA_DATA8__ESDHC3_DAT0,
	MX53_PAD_PATA_DATA9__ESDHC3_DAT1,
	MX53_PAD_PATA_DATA10__ESDHC3_DAT2,
	MX53_PAD_PATA_DATA11__ESDHC3_DAT3,
	MX53_PAD_PATA_DATA0__ESDHC3_DAT4,
	MX53_PAD_PATA_DATA1__ESDHC3_DAT5,
	MX53_PAD_PATA_DATA2__ESDHC3_DAT6,
	MX53_PAD_PATA_DATA3__ESDHC3_DAT7,
	MX53_PAD_PATA_IORDY__ESDHC3_CLK,
	MX53_PAD_PATA_RESET_B__ESDHC3_CMD,
	/* SD3_CD */
	MX53_PAD_EIM_DA11__GPIO3_11,
	/* SD3_WP */
	MX53_PAD_EIM_DA12__GPIO3_12,
	/* VGA */
	MX53_PAD_EIM_OE__IPU_DI1_PIN7,
	MX53_PAD_EIM_RW__IPU_DI1_PIN8,
	/* DISPLB */
	MX53_PAD_EIM_D20__IPU_SER_DISP0_CS,
	MX53_PAD_EIM_D21__IPU_DISPB0_SER_CLK,
	MX53_PAD_EIM_D22__IPU_DISPB0_SER_DIN,
	MX53_PAD_EIM_D23__IPU_DI0_D0_CS,
	/* DISP0_POWER_EN */
	MX53_PAD_EIM_D24__GPIO3_24,
	/* DISP0 DET INT */
	MX53_PAD_EIM_D31__GPIO3_31,
	/* LVDS */
	MX53_PAD_LVDS0_TX3_P__LDB_LVDS0_TX3,
	MX53_PAD_LVDS0_CLK_P__LDB_LVDS0_CLK,
	MX53_PAD_LVDS0_TX2_P__LDB_LVDS0_TX2,
	MX53_PAD_LVDS0_TX1_P__LDB_LVDS0_TX1,
	MX53_PAD_LVDS0_TX0_P__LDB_LVDS0_TX0,
	MX53_PAD_LVDS1_TX3_P__LDB_LVDS1_TX3,
	MX53_PAD_LVDS1_TX2_P__LDB_LVDS1_TX2,
	MX53_PAD_LVDS1_CLK_P__LDB_LVDS1_CLK,
	MX53_PAD_LVDS1_TX1_P__LDB_LVDS1_TX1,
	MX53_PAD_LVDS1_TX0_P__LDB_LVDS1_TX0,
	/* I2C1 */
	MX53_PAD_CSI0_DAT8__I2C1_SDA,
	MX53_PAD_CSI0_DAT9__I2C1_SCL,
	/* UART1 */
	MX53_PAD_CSI0_DAT10__UART1_TXD_MUX,
	MX53_PAD_CSI0_DAT11__UART1_RXD_MUX,
	/* CSI0 */
	MX53_PAD_CSI0_DAT12__IPU_CSI0_D_12,
	MX53_PAD_CSI0_DAT13__IPU_CSI0_D_13,
	MX53_PAD_CSI0_DAT14__IPU_CSI0_D_14,
	MX53_PAD_CSI0_DAT15__IPU_CSI0_D_15,
	MX53_PAD_CSI0_DAT16__IPU_CSI0_D_16,
	MX53_PAD_CSI0_DAT17__IPU_CSI0_D_17,
	MX53_PAD_CSI0_DAT18__IPU_CSI0_D_18,
	MX53_PAD_CSI0_DAT19__IPU_CSI0_D_19,
	MX53_PAD_CSI0_VSYNC__IPU_CSI0_VSYNC,
	MX53_PAD_CSI0_MCLK__IPU_CSI0_HSYNC,
	MX53_PAD_CSI0_PIXCLK__IPU_CSI0_PIXCLK,
	/* DISPLAY */
	MX53_PAD_DI0_DISP_CLK__IPU_DI0_DISP_CLK,
	MX53_PAD_DI0_PIN15__IPU_DI0_PIN15,
	MX53_PAD_DI0_PIN2__IPU_DI0_PIN2,
	MX53_PAD_DI0_PIN3__IPU_DI0_PIN3,
	MX53_PAD_DISP0_DAT0__IPU_DISP0_DAT_0,
	MX53_PAD_DISP0_DAT1__IPU_DISP0_DAT_1,
	MX53_PAD_DISP0_DAT2__IPU_DISP0_DAT_2,
	MX53_PAD_DISP0_DAT3__IPU_DISP0_DAT_3,
	MX53_PAD_DISP0_DAT4__IPU_DISP0_DAT_4,
	MX53_PAD_DISP0_DAT5__IPU_DISP0_DAT_5,
	MX53_PAD_DISP0_DAT6__IPU_DISP0_DAT_6,
	MX53_PAD_DISP0_DAT7__IPU_DISP0_DAT_7,
	MX53_PAD_DISP0_DAT8__IPU_DISP0_DAT_8,
	MX53_PAD_DISP0_DAT9__IPU_DISP0_DAT_9,
	MX53_PAD_DISP0_DAT10__IPU_DISP0_DAT_10,
	MX53_PAD_DISP0_DAT11__IPU_DISP0_DAT_11,
	MX53_PAD_DISP0_DAT12__IPU_DISP0_DAT_12,
	MX53_PAD_DISP0_DAT13__IPU_DISP0_DAT_13,
	MX53_PAD_DISP0_DAT14__IPU_DISP0_DAT_14,
	MX53_PAD_DISP0_DAT15__IPU_DISP0_DAT_15,
	MX53_PAD_DISP0_DAT16__IPU_DISP0_DAT_16,
	MX53_PAD_DISP0_DAT17__IPU_DISP0_DAT_17,
	MX53_PAD_DISP0_DAT18__IPU_DISP0_DAT_18,
	MX53_PAD_DISP0_DAT19__IPU_DISP0_DAT_19,
	MX53_PAD_DISP0_DAT20__IPU_DISP0_DAT_20,
	MX53_PAD_DISP0_DAT21__IPU_DISP0_DAT_21,
	MX53_PAD_DISP0_DAT22__IPU_DISP0_DAT_22,
	MX53_PAD_DISP0_DAT23__IPU_DISP0_DAT_23,
	/* Audio CLK*/
	MX53_PAD_GPIO_0__CCM_SSI_EXT1_CLK,
	/* PWM */
	MX53_PAD_GPIO_1__PWM2_PWMO,
	/* SPDIF */
	MX53_PAD_GPIO_7__SPDIF_PLOCK,
	MX53_PAD_GPIO_17__SPDIF_OUT1,
	/* GPIO */
	MX53_PAD_PATA_DA_1__GPIO7_7,
	MX53_PAD_PATA_DA_2__GPIO7_8,
	MX53_PAD_PATA_DATA5__GPIO2_5,
	MX53_PAD_PATA_DATA6__GPIO2_6,
	MX53_PAD_PATA_DATA14__GPIO2_14,
	MX53_PAD_PATA_DATA15__GPIO2_15,
	MX53_PAD_PATA_INTRQ__GPIO7_2,
	MX53_PAD_EIM_WAIT__GPIO5_0,
	MX53_PAD_NANDF_WP_B__GPIO6_9,
	MX53_PAD_NANDF_RB0__GPIO6_10,
	MX53_PAD_NANDF_CS1__GPIO6_14,
	MX53_PAD_NANDF_CS2__GPIO6_15,
	MX53_PAD_NANDF_CS3__GPIO6_16,
	MX53_PAD_GPIO_5__GPIO1_5,
	MX53_PAD_GPIO_16__GPIO7_11,
	MX53_PAD_GPIO_8__GPIO1_8,
};

#define GPIO_BUTTON(gpio_num, ev_code, act_low, descr, wake)	\
{								\
	.gpio		= gpio_num,				\
	.type		= EV_KEY,				\
	.code		= ev_code,				\
	.active_low	= act_low,				\
	.desc		= "btn " descr,				\
	.wakeup		= wake,					\
}

static struct gpio_keys_button loco_buttons[] = {
	GPIO_BUTTON(MX53_LOCO_POWER, KEY_POWER, 1, "power", 0),
	GPIO_BUTTON(MX53_LOCO_UI1, KEY_VOLUMEUP, 1, "volume-up", 0),
	GPIO_BUTTON(MX53_LOCO_UI2, KEY_VOLUMEDOWN, 1, "volume-down", 0),
};

static const struct gpio_keys_platform_data loco_button_data __initconst = {
	.buttons        = loco_buttons,
	.nbuttons       = ARRAY_SIZE(loco_buttons),
};

static inline void mx53_loco_fec_reset(void)
{
	int ret;

	/* reset FEC PHY */
	ret = gpio_request(LOCO_FEC_PHY_RST, "fec-phy-reset");
	if (ret) {
		printk(KERN_ERR"failed to get GPIO_FEC_PHY_RESET: %d\n", ret);
		return;
	}
	gpio_direction_output(LOCO_FEC_PHY_RST, 0);
	msleep(1);
	gpio_set_value(LOCO_FEC_PHY_RST, 1);
}

static struct fec_platform_data mx53_loco_fec_data = {
	.phy = PHY_INTERFACE_MODE_RMII,
};

static const struct imxi2c_platform_data mx53_loco_i2c_data __initconst = {
	.bitrate = 100000,
};

static void __init mx53_loco_board_init(void)
{
	mxc_iomux_v3_setup_multiple_pads(mx53_loco_pads,
					ARRAY_SIZE(mx53_loco_pads));
	imx53_add_imx_uart(0, NULL);
	mx53_loco_fec_reset();
	imx53_add_fec(&mx53_loco_fec_data);
	imx53_add_imx2_wdt(0, NULL);
	imx53_add_imx_i2c(0, &mx53_loco_i2c_data);
	imx53_add_imx_i2c(1, &mx53_loco_i2c_data);
	imx53_add_sdhci_esdhc_imx(0, NULL);
	imx53_add_sdhci_esdhc_imx(2, NULL);
	imx_add_gpio_keys(&loco_button_data);
}

static void __init mx53_loco_timer_init(void)
{
	mx53_clocks_init(32768, 24000000, 0, 0);
}

static struct sys_timer mx53_loco_timer = {
	.init	= mx53_loco_timer_init,
};

MACHINE_START(MX53_LOCO, "Freescale MX53 LOCO Board")
	.map_io = mx53_map_io,
	.init_early = imx53_init_early,
	.init_irq = mx53_init_irq,
	.timer = &mx53_loco_timer,
	.init_machine = mx53_loco_board_init,
MACHINE_END
