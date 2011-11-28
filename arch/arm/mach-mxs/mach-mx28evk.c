/*
 * Copyright 2010 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include <mach/common.h>
#include <mach/iomux-mx28.h>

#include "devices-mx28.h"

#define MX28EVK_FLEXCAN_SWITCH	MXS_GPIO_NR(2, 13)
#define MX28EVK_FEC_PHY_POWER	MXS_GPIO_NR(2, 15)
#define MX28EVK_GPIO_LED	MXS_GPIO_NR(3, 5)
#define MX28EVK_BL_ENABLE	MXS_GPIO_NR(3, 18)
#define MX28EVK_LCD_ENABLE	MXS_GPIO_NR(3, 30)
#define MX28EVK_FEC_PHY_RESET	MXS_GPIO_NR(4, 13)

#define MX28EVK_MMC0_WRITE_PROTECT	MXS_GPIO_NR(2, 12)
#define MX28EVK_MMC1_WRITE_PROTECT	MXS_GPIO_NR(0, 28)
#define MX28EVK_MMC0_SLOT_POWER		MXS_GPIO_NR(3, 28)
#define MX28EVK_MMC1_SLOT_POWER		MXS_GPIO_NR(3, 29)

static const iomux_cfg_t mx28evk_pads[] __initconst = {
	/* duart */
	MX28_PAD_PWM0__DUART_RX | MXS_PAD_CTRL,
	MX28_PAD_PWM1__DUART_TX | MXS_PAD_CTRL,

	/* auart0 */
	MX28_PAD_AUART0_RX__AUART0_RX | MXS_PAD_CTRL,
	MX28_PAD_AUART0_TX__AUART0_TX | MXS_PAD_CTRL,
	MX28_PAD_AUART0_CTS__AUART0_CTS | MXS_PAD_CTRL,
	MX28_PAD_AUART0_RTS__AUART0_RTS | MXS_PAD_CTRL,
	/* auart3 */
	MX28_PAD_AUART3_RX__AUART3_RX | MXS_PAD_CTRL,
	MX28_PAD_AUART3_TX__AUART3_TX | MXS_PAD_CTRL,
	MX28_PAD_AUART3_CTS__AUART3_CTS | MXS_PAD_CTRL,
	MX28_PAD_AUART3_RTS__AUART3_RTS | MXS_PAD_CTRL,

#define MXS_PAD_FEC	(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP)
	/* fec0 */
	MX28_PAD_ENET0_MDC__ENET0_MDC | MXS_PAD_FEC,
	MX28_PAD_ENET0_MDIO__ENET0_MDIO | MXS_PAD_FEC,
	MX28_PAD_ENET0_RX_EN__ENET0_RX_EN | MXS_PAD_FEC,
	MX28_PAD_ENET0_RXD0__ENET0_RXD0 | MXS_PAD_FEC,
	MX28_PAD_ENET0_RXD1__ENET0_RXD1 | MXS_PAD_FEC,
	MX28_PAD_ENET0_TX_EN__ENET0_TX_EN | MXS_PAD_FEC,
	MX28_PAD_ENET0_TXD0__ENET0_TXD0 | MXS_PAD_FEC,
	MX28_PAD_ENET0_TXD1__ENET0_TXD1 | MXS_PAD_FEC,
	MX28_PAD_ENET_CLK__CLKCTRL_ENET | MXS_PAD_FEC,
	/* fec1 */
	MX28_PAD_ENET0_CRS__ENET1_RX_EN | MXS_PAD_FEC,
	MX28_PAD_ENET0_RXD2__ENET1_RXD0 | MXS_PAD_FEC,
	MX28_PAD_ENET0_RXD3__ENET1_RXD1 | MXS_PAD_FEC,
	MX28_PAD_ENET0_COL__ENET1_TX_EN | MXS_PAD_FEC,
	MX28_PAD_ENET0_TXD2__ENET1_TXD0 | MXS_PAD_FEC,
	MX28_PAD_ENET0_TXD3__ENET1_TXD1 | MXS_PAD_FEC,
	/* phy power line */
	MX28_PAD_SSP1_DATA3__GPIO_2_15 | MXS_PAD_CTRL,
	/* phy reset line */
	MX28_PAD_ENET0_RX_CLK__GPIO_4_13 | MXS_PAD_CTRL,

	/* flexcan0 */
	MX28_PAD_GPMI_RDY2__CAN0_TX,
	MX28_PAD_GPMI_RDY3__CAN0_RX,
	/* flexcan1 */
	MX28_PAD_GPMI_CE2N__CAN1_TX,
	MX28_PAD_GPMI_CE3N__CAN1_RX,
	/* transceiver power control */
	MX28_PAD_SSP1_CMD__GPIO_2_13,

	/* mxsfb (lcdif) */
	MX28_PAD_LCD_D00__LCD_D0 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D01__LCD_D1 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D02__LCD_D2 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D03__LCD_D3 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D04__LCD_D4 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D05__LCD_D5 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D06__LCD_D6 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D07__LCD_D7 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D08__LCD_D8 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D09__LCD_D9 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D10__LCD_D10 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D11__LCD_D11 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D12__LCD_D12 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D13__LCD_D13 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D14__LCD_D14 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D15__LCD_D15 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D16__LCD_D16 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D17__LCD_D17 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D18__LCD_D18 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D19__LCD_D19 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D20__LCD_D20 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D21__LCD_D21 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D22__LCD_D22 | MXS_PAD_CTRL,
	MX28_PAD_LCD_D23__LCD_D23 | MXS_PAD_CTRL,
	MX28_PAD_LCD_RD_E__LCD_VSYNC | MXS_PAD_CTRL,
	MX28_PAD_LCD_WR_RWN__LCD_HSYNC | MXS_PAD_CTRL,
	MX28_PAD_LCD_RS__LCD_DOTCLK | MXS_PAD_CTRL,
	MX28_PAD_LCD_CS__LCD_ENABLE | MXS_PAD_CTRL,
	/* LCD panel enable */
	MX28_PAD_LCD_RESET__GPIO_3_30 | MXS_PAD_CTRL,
	/* backlight control */
	MX28_PAD_PWM2__GPIO_3_18 | MXS_PAD_CTRL,
	/* mmc0 */
	MX28_PAD_SSP0_DATA0__SSP0_D0 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DATA1__SSP0_D1 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DATA2__SSP0_D2 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DATA3__SSP0_D3 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DATA4__SSP0_D4 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DATA5__SSP0_D5 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DATA6__SSP0_D6 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DATA7__SSP0_D7 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_CMD__SSP0_CMD |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SSP0_DETECT__SSP0_CARD_DETECT |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),
	MX28_PAD_SSP0_SCK__SSP0_SCK |
		(MXS_PAD_12MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),
	/* write protect */
	MX28_PAD_SSP1_SCK__GPIO_2_12 |
		(MXS_PAD_4MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),
	/* slot power enable */
	MX28_PAD_PWM3__GPIO_3_28 |
		(MXS_PAD_4MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),

	/* mmc1 */
	MX28_PAD_GPMI_D00__SSP1_D0 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_GPMI_D01__SSP1_D1 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_GPMI_D02__SSP1_D2 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_GPMI_D03__SSP1_D3 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_GPMI_D04__SSP1_D4 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_GPMI_D05__SSP1_D5 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_GPMI_D06__SSP1_D6 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_GPMI_D07__SSP1_D7 |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_GPMI_RDY1__SSP1_CMD |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_GPMI_RDY0__SSP1_CARD_DETECT |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),
	MX28_PAD_GPMI_WRN__SSP1_SCK |
		(MXS_PAD_12MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),
	/* write protect */
	MX28_PAD_GPMI_RESETN__GPIO_0_28 |
		(MXS_PAD_4MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),
	/* slot power enable */
	MX28_PAD_PWM4__GPIO_3_29 |
		(MXS_PAD_4MA | MXS_PAD_3V3 | MXS_PAD_NOPULL),

	/* led */
	MX28_PAD_AUART1_TX__GPIO_3_5 | MXS_PAD_CTRL,

	/* I2C */
	MX28_PAD_I2C0_SCL__I2C0_SCL |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_I2C0_SDA__I2C0_SDA |
		(MXS_PAD_8MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),

	/* saif0 & saif1 */
	MX28_PAD_SAIF0_MCLK__SAIF0_MCLK |
		(MXS_PAD_12MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SAIF0_LRCLK__SAIF0_LRCLK |
		(MXS_PAD_12MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SAIF0_BITCLK__SAIF0_BITCLK |
		(MXS_PAD_12MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SAIF0_SDATA0__SAIF0_SDATA0 |
		(MXS_PAD_12MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
	MX28_PAD_SAIF1_SDATA0__SAIF1_SDATA0 |
		(MXS_PAD_12MA | MXS_PAD_3V3 | MXS_PAD_PULLUP),
};

/* led */
static const struct gpio_led mx28evk_leds[] __initconst = {
	{
		.name = "GPIO-LED",
		.default_trigger = "heartbeat",
		.gpio = MX28EVK_GPIO_LED,
	},
};

static const struct gpio_led_platform_data mx28evk_led_data __initconst = {
	.leds = mx28evk_leds,
	.num_leds = ARRAY_SIZE(mx28evk_leds),
};

/* fec */
static void __init mx28evk_fec_reset(void)
{
	int ret;
	struct clk *clk;

	/* Enable fec phy clock */
	clk = clk_get_sys("pll2", NULL);
	if (!IS_ERR(clk))
		clk_enable(clk);

	/* Power up fec phy */
	ret = gpio_request(MX28EVK_FEC_PHY_POWER, "fec-phy-power");
	if (ret) {
		pr_err("Failed to request gpio fec-phy-%s: %d\n", "power", ret);
		return;
	}

	ret = gpio_direction_output(MX28EVK_FEC_PHY_POWER, 0);
	if (ret) {
		pr_err("Failed to drive gpio fec-phy-%s: %d\n", "power", ret);
		return;
	}

	/* Reset fec phy */
	ret = gpio_request(MX28EVK_FEC_PHY_RESET, "fec-phy-reset");
	if (ret) {
		pr_err("Failed to request gpio fec-phy-%s: %d\n", "reset", ret);
		return;
	}

	gpio_direction_output(MX28EVK_FEC_PHY_RESET, 0);
	if (ret) {
		pr_err("Failed to drive gpio fec-phy-%s: %d\n", "reset", ret);
		return;
	}

	mdelay(1);
	gpio_set_value(MX28EVK_FEC_PHY_RESET, 1);
}

static struct fec_platform_data mx28_fec_pdata[] __initdata = {
	{
		/* fec0 */
		.phy = PHY_INTERFACE_MODE_RMII,
	}, {
		/* fec1 */
		.phy = PHY_INTERFACE_MODE_RMII,
	},
};

static int __init mx28evk_fec_get_mac(void)
{
	int i;
	u32 val;
	const u32 *ocotp = mxs_get_ocotp();

	if (!ocotp)
		goto error;

	/*
	 * OCOTP only stores the last 4 octets for each mac address,
	 * so hard-code Freescale OUI (00:04:9f) here.
	 */
	for (i = 0; i < 2; i++) {
		val = ocotp[i * 4];
		mx28_fec_pdata[i].mac[0] = 0x00;
		mx28_fec_pdata[i].mac[1] = 0x04;
		mx28_fec_pdata[i].mac[2] = 0x9f;
		mx28_fec_pdata[i].mac[3] = (val >> 16) & 0xff;
		mx28_fec_pdata[i].mac[4] = (val >> 8) & 0xff;
		mx28_fec_pdata[i].mac[5] = (val >> 0) & 0xff;
	}

	return 0;

error:
	pr_err("%s: timeout when reading fec mac from OCOTP\n", __func__);
	return -ETIMEDOUT;
}

/*
 * MX28EVK_FLEXCAN_SWITCH is shared between both flexcan controllers
 */
static int flexcan0_en, flexcan1_en;

static void mx28evk_flexcan_switch(void)
{
	if (flexcan0_en || flexcan1_en)
		gpio_set_value(MX28EVK_FLEXCAN_SWITCH, 1);
	else
		gpio_set_value(MX28EVK_FLEXCAN_SWITCH, 0);
}

static void mx28evk_flexcan0_switch(int enable)
{
	flexcan0_en = enable;
	mx28evk_flexcan_switch();
}

static void mx28evk_flexcan1_switch(int enable)
{
	flexcan1_en = enable;
	mx28evk_flexcan_switch();
}

static const struct flexcan_platform_data
		mx28evk_flexcan_pdata[] __initconst = {
	{
		.transceiver_switch = mx28evk_flexcan0_switch,
	}, {
		.transceiver_switch = mx28evk_flexcan1_switch,
	}
};

/* mxsfb (lcdif) */
static struct fb_videomode mx28evk_video_modes[] = {
	{
		.name		= "Seiko-43WVF1G",
		.refresh	= 60,
		.xres		= 800,
		.yres		= 480,
		.pixclock	= 29851, /* picosecond (33.5 MHz) */
		.left_margin	= 89,
		.right_margin	= 164,
		.upper_margin	= 23,
		.lower_margin	= 10,
		.hsync_len	= 10,
		.vsync_len	= 10,
		.sync		= FB_SYNC_DATA_ENABLE_HIGH_ACT |
				  FB_SYNC_DOTCLK_FAILING_ACT,
	},
};

static const struct mxsfb_platform_data mx28evk_mxsfb_pdata __initconst = {
	.mode_list	= mx28evk_video_modes,
	.mode_count	= ARRAY_SIZE(mx28evk_video_modes),
	.default_bpp	= 32,
	.ld_intf_width	= STMLCDIF_24BIT,
};

static struct mxs_mmc_platform_data mx28evk_mmc_pdata[] __initdata = {
	{
		/* mmc0 */
		.wp_gpio = MX28EVK_MMC0_WRITE_PROTECT,
		.flags = SLOTF_8_BIT_CAPABLE,
	}, {
		/* mmc1 */
		.wp_gpio = MX28EVK_MMC1_WRITE_PROTECT,
		.flags = SLOTF_8_BIT_CAPABLE,
	},
};

static struct i2c_board_info mxs_i2c0_board_info[] __initdata = {
	{
		I2C_BOARD_INFO("sgtl5000", 0x0a),
	},
};

#if defined(CONFIG_REGULATOR_FIXED_VOLTAGE) || defined(CONFIG_REGULATOR_FIXED_VOLTAGE_MODULE)
static struct regulator_consumer_supply mx28evk_audio_consumer_supplies[] = {
	REGULATOR_SUPPLY("VDDA", "0-000a"),
	REGULATOR_SUPPLY("VDDIO", "0-000a"),
};

static struct regulator_init_data mx28evk_vdd_reg_init_data = {
	.constraints	= {
		.name	= "3V3",
		.always_on = 1,
	},
	.consumer_supplies = mx28evk_audio_consumer_supplies,
	.num_consumer_supplies = ARRAY_SIZE(mx28evk_audio_consumer_supplies),
};

static struct fixed_voltage_config mx28evk_vdd_pdata = {
	.supply_name	= "board-3V3",
	.microvolts	= 3300000,
	.gpio		= -EINVAL,
	.enabled_at_boot = 1,
	.init_data	= &mx28evk_vdd_reg_init_data,
};
static struct platform_device mx28evk_voltage_regulator = {
	.name		= "reg-fixed-voltage",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &mx28evk_vdd_pdata,
	},
};
static void __init mx28evk_add_regulators(void)
{
	platform_device_register(&mx28evk_voltage_regulator);
}
#else
static void __init mx28evk_add_regulators(void) {}
#endif

static struct gpio mx28evk_lcd_gpios[] = {
	{ MX28EVK_LCD_ENABLE, GPIOF_OUT_INIT_HIGH, "lcd-enable" },
	{ MX28EVK_BL_ENABLE, GPIOF_OUT_INIT_HIGH, "bl-enable" },
};

static void __init mx28evk_init(void)
{
	int ret;

	mxs_iomux_setup_multiple_pads(mx28evk_pads, ARRAY_SIZE(mx28evk_pads));

	mx28_add_duart();
	mx28_add_auart0();
	mx28_add_auart3();

	if (mx28evk_fec_get_mac())
		pr_warn("%s: failed on fec mac setup\n", __func__);

	mx28evk_fec_reset();
	mx28_add_fec(0, &mx28_fec_pdata[0]);
	mx28_add_fec(1, &mx28_fec_pdata[1]);

	ret = gpio_request_one(MX28EVK_FLEXCAN_SWITCH, GPIOF_DIR_OUT,
				"flexcan-switch");
	if (ret) {
		pr_err("failed to request gpio flexcan-switch: %d\n", ret);
	} else {
		mx28_add_flexcan(0, &mx28evk_flexcan_pdata[0]);
		mx28_add_flexcan(1, &mx28evk_flexcan_pdata[1]);
	}

	ret = gpio_request_array(mx28evk_lcd_gpios,
				 ARRAY_SIZE(mx28evk_lcd_gpios));
	if (ret)
		pr_warn("failed to request gpio pins for lcd: %d\n", ret);
	else
		mx28_add_mxsfb(&mx28evk_mxsfb_pdata);

	mx28_add_saif(0);
	mx28_add_saif(1);

	mx28_add_mxs_i2c(0);
	i2c_register_board_info(0, mxs_i2c0_board_info,
				ARRAY_SIZE(mxs_i2c0_board_info));

	mx28evk_add_regulators();

	mxs_add_platform_device("mxs-sgtl5000", 0, NULL, 0,
			NULL, 0);

	/* power on mmc slot by writing 0 to the gpio */
	ret = gpio_request_one(MX28EVK_MMC0_SLOT_POWER, GPIOF_OUT_INIT_LOW,
			       "mmc0-slot-power");
	if (ret)
		pr_warn("failed to request gpio mmc0-slot-power: %d\n", ret);
	else
		mx28_add_mxs_mmc(0, &mx28evk_mmc_pdata[0]);

	ret = gpio_request_one(MX28EVK_MMC1_SLOT_POWER, GPIOF_OUT_INIT_LOW,
			       "mmc1-slot-power");
	if (ret)
		pr_warn("failed to request gpio mmc1-slot-power: %d\n", ret);
	else
		mx28_add_mxs_mmc(1, &mx28evk_mmc_pdata[1]);

	mx28_add_rtc_stmp3xxx();

	gpio_led_register_device(0, &mx28evk_led_data);
}

static void __init mx28evk_timer_init(void)
{
	mx28_clocks_init();
}

static struct sys_timer mx28evk_timer = {
	.init	= mx28evk_timer_init,
};

MACHINE_START(MX28EVK, "Freescale MX28 EVK")
	/* Maintainer: Freescale Semiconductor, Inc. */
	.map_io		= mx28_map_io,
	.init_irq	= mx28_init_irq,
	.timer		= &mx28evk_timer,
	.init_machine	= mx28evk_init,
MACHINE_END
