/*
 * arch/arm/mach-shmobile/board-ag5evm.c
 *
 * Copyright (C) 2010  Takashi Yoshii <yoshii.takashi.zj@renesas.com>
 * Copyright (C) 2009  Yoshihiro Shimoda <shimoda.yoshihiro@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/serial_sci.h>
#include <linux/smsc911x.h>
#include <linux/gpio.h>
#include <linux/videodev2.h>
#include <linux/input.h>
#include <linux/input/sh_keysc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/mfd/tmio.h>
#include <linux/sh_clk.h>
#include <video/sh_mobile_lcdc.h>
#include <video/sh_mipi_dsi.h>
#include <sound/sh_fsi.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/sh73a0.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/traps.h>

/* Dummy supplies, where voltage doesn't matter */
static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x"),
};

static struct resource smsc9220_resources[] = {
	[0] = {
		.start		= 0x14000000,
		.end		= 0x14000000 + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= SH73A0_PINT0_IRQ(2), /* PINTA2 */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc9220_platdata = {
	.flags		= SMSC911X_USE_32BIT | SMSC911X_SAVE_MAC_ADDRESS,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct platform_device eth_device = {
	.name		= "smsc911x",
	.id		= 0,
	.dev  = {
		.platform_data = &smsc9220_platdata,
	},
	.resource	= smsc9220_resources,
	.num_resources	= ARRAY_SIZE(smsc9220_resources),
};

static struct sh_keysc_info keysc_platdata = {
	.mode		= SH_KEYSC_MODE_6,
	.scan_timing	= 3,
	.delay		= 100,
	.keycodes	= {
		KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G,
		KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N,
		KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U,
		KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z, KEY_HOME, KEY_SLEEP,
		KEY_SPACE, KEY_9, KEY_6, KEY_3, KEY_WAKEUP, KEY_RIGHT, \
		KEY_COFFEE,
		KEY_0, KEY_8, KEY_5, KEY_2, KEY_DOWN, KEY_ENTER, KEY_UP,
		KEY_KPASTERISK, KEY_7, KEY_4, KEY_1, KEY_STOP, KEY_LEFT, \
		KEY_COMPUTER,
	},
};

static struct resource keysc_resources[] = {
	[0] = {
		.name	= "KEYSC",
		.start	= 0xe61b0000,
		.end	= 0xe61b0098 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(71),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device keysc_device = {
	.name		= "sh_keysc",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(keysc_resources),
	.resource	= keysc_resources,
	.dev		= {
		.platform_data	= &keysc_platdata,
	},
};

/* FSI A */
static struct resource fsi_resources[] = {
	[0] = {
		.name	= "FSI",
		.start	= 0xEC230000,
		.end	= 0xEC230400 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start  = gic_spi(146),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device fsi_device = {
	.name		= "sh_fsi2",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(fsi_resources),
	.resource	= fsi_resources,
};

/* Fixed 1.8V regulator to be used by MMCIF */
static struct regulator_consumer_supply fixed1v8_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mmcif.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mmcif.0"),
};

static struct resource sh_mmcif_resources[] = {
	[0] = {
		.name	= "MMCIF",
		.start	= 0xe6bd0000,
		.end	= 0xe6bd00ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(141),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.start	= gic_spi(140),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct sh_mmcif_plat_data sh_mmcif_platdata = {
	.sup_pclk	= 0,
	.ocr		= MMC_VDD_165_195,
	.caps		= MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE,
	.slave_id_tx	= SHDMA_SLAVE_MMCIF_TX,
	.slave_id_rx	= SHDMA_SLAVE_MMCIF_RX,
};

static struct platform_device mmc_device = {
	.name		= "sh_mmcif",
	.id		= 0,
	.dev		= {
		.dma_mask		= NULL,
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &sh_mmcif_platdata,
	},
	.num_resources	= ARRAY_SIZE(sh_mmcif_resources),
	.resource	= sh_mmcif_resources,
};

/* IrDA */
static struct resource irda_resources[] = {
	[0] = {
		.start	= 0xE6D00000,
		.end	= 0xE6D01FD4 - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_spi(95),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device irda_device = {
	.name           = "sh_irda",
	.id		= 0,
	.resource       = irda_resources,
	.num_resources  = ARRAY_SIZE(irda_resources),
};

static unsigned char lcd_backlight_seq[3][2] = {
	{ 0x04, 0x07 },
	{ 0x23, 0x80 },
	{ 0x03, 0x01 },
};

static void lcd_backlight_on(void)
{
	struct i2c_adapter *a;
	struct i2c_msg msg;
	int k;

	a = i2c_get_adapter(1);
	for (k = 0; a && k < 3; k++) {
		msg.addr = 0x6d;
		msg.buf = &lcd_backlight_seq[k][0];
		msg.len = 2;
		msg.flags = 0;
		if (i2c_transfer(a, &msg, 1) != 1)
			break;
	}
}

static void lcd_backlight_reset(void)
{
	gpio_set_value(GPIO_PORT235, 0);
	mdelay(24);
	gpio_set_value(GPIO_PORT235, 1);
}

/* LCDC0 */
static const struct fb_videomode lcdc0_modes[] = {
	{
		.name		= "R63302(QHD)",
		.xres		= 544,
		.yres		= 961,
		.left_margin	= 72,
		.right_margin	= 600,
		.hsync_len	= 16,
		.upper_margin	= 8,
		.lower_margin	= 8,
		.vsync_len	= 2,
		.sync		= FB_SYNC_VERT_HIGH_ACT | FB_SYNC_HOR_HIGH_ACT,
	},
};

static struct sh_mobile_lcdc_info lcdc0_info = {
	.clock_source = LCDC_CLK_PERIPHERAL,
	.ch[0] = {
		.chan = LCDC_CHAN_MAINLCD,
		.interface_type = RGB24,
		.clock_divider = 1,
		.flags = LCDC_FLAGS_DWPOL,
		.fourcc = V4L2_PIX_FMT_RGB565,
		.lcd_modes = lcdc0_modes,
		.num_modes = ARRAY_SIZE(lcdc0_modes),
		.panel_cfg = {
			.width = 44,
			.height = 79,
			.display_on = lcd_backlight_on,
			.display_off = lcd_backlight_reset,
		},
	}
};

static struct resource lcdc0_resources[] = {
	[0] = {
		.name	= "LCDC0",
		.start	= 0xfe940000, /* P4-only space */
		.end	= 0xfe943fff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x580),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device lcdc0_device = {
	.name		= "sh_mobile_lcdc_fb",
	.num_resources	= ARRAY_SIZE(lcdc0_resources),
	.resource	= lcdc0_resources,
	.id             = 0,
	.dev	= {
		.platform_data	= &lcdc0_info,
		.coherent_dma_mask = ~0,
	},
};

/* MIPI-DSI */
static struct resource mipidsi0_resources[] = {
	[0] = {
		.name	= "DSI0",
		.start  = 0xfeab0000,
		.end    = 0xfeab3fff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.name	= "DSI0",
		.start  = 0xfeab4000,
		.end    = 0xfeab7fff,
		.flags  = IORESOURCE_MEM,
	},
};

static int sh_mipi_set_dot_clock(struct platform_device *pdev,
				 void __iomem *base,
				 int enable)
{
	struct clk *pck, *phy;
	int ret;

	pck = clk_get(&pdev->dev, "dsip_clk");
	if (IS_ERR(pck)) {
		ret = PTR_ERR(pck);
		goto sh_mipi_set_dot_clock_pck_err;
	}

	phy = clk_get(&pdev->dev, "dsiphy_clk");
	if (IS_ERR(phy)) {
		ret = PTR_ERR(phy);
		goto sh_mipi_set_dot_clock_phy_err;
	}

	if (enable) {
		clk_set_rate(pck, clk_round_rate(pck,  24000000));
		clk_set_rate(phy, clk_round_rate(pck, 510000000));
		clk_enable(pck);
		clk_enable(phy);
	} else {
		clk_disable(pck);
		clk_disable(phy);
	}

	ret = 0;

	clk_put(phy);
sh_mipi_set_dot_clock_phy_err:
	clk_put(pck);
sh_mipi_set_dot_clock_pck_err:
	return ret;
}

static struct sh_mipi_dsi_info mipidsi0_info = {
	.data_format	= MIPI_RGB888,
	.lcd_chan	= &lcdc0_info.ch[0],
	.lane		= 2,
	.vsynw_offset	= 20,
	.clksrc		= 1,
	.flags		= SH_MIPI_DSI_HSABM		|
			  SH_MIPI_DSI_SYNC_PULSES_MODE	|
			  SH_MIPI_DSI_HSbyteCLK,
	.set_dot_clock	= sh_mipi_set_dot_clock,
};

static struct platform_device mipidsi0_device = {
	.name           = "sh-mipi-dsi",
	.num_resources  = ARRAY_SIZE(mipidsi0_resources),
	.resource       = mipidsi0_resources,
	.id             = 0,
	.dev	= {
		.platform_data	= &mipidsi0_info,
	},
};

/* Fixed 2.8V regulators to be used by SDHI0 */
static struct regulator_consumer_supply fixed2v8_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.0"),
};

/* SDHI0 */
static struct sh_mobile_sdhi_info sdhi0_info = {
	.dma_slave_tx	= SHDMA_SLAVE_SDHI0_TX,
	.dma_slave_rx	= SHDMA_SLAVE_SDHI0_RX,
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_USE_GPIO_CD,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED,
	.tmio_ocr_mask	= MMC_VDD_27_28 | MMC_VDD_28_29,
	.cd_gpio	= GPIO_PORT251,
};

static struct resource sdhi0_resources[] = {
	[0] = {
		.name	= "SDHI0",
		.start	= 0xee100000,
		.end	= 0xee1000ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= SH_MOBILE_SDHI_IRQ_CARD_DETECT,
		.start	= gic_spi(83),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.name	= SH_MOBILE_SDHI_IRQ_SDCARD,
		.start	= gic_spi(84),
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.name	= SH_MOBILE_SDHI_IRQ_SDIO,
		.start	= gic_spi(85),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi0_device = {
	.name		= "sh_mobile_sdhi",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(sdhi0_resources),
	.resource	= sdhi0_resources,
	.dev	= {
		.platform_data	= &sdhi0_info,
	},
};

/* Fixed 3.3V regulator to be used by SDHI1 */
static struct regulator_consumer_supply cn4_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.1"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.1"),
};

static struct regulator_init_data cn4_power_init_data = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = ARRAY_SIZE(cn4_power_consumers),
	.consumer_supplies      = cn4_power_consumers,
};

static struct fixed_voltage_config cn4_power_info = {
	.supply_name = "CN4 SD/MMC Vdd",
	.microvolts = 3300000,
	.gpio = GPIO_PORT114,
	.enable_high = 1,
	.init_data = &cn4_power_init_data,
};

static struct platform_device cn4_power = {
	.name = "reg-fixed-voltage",
	.id   = 2,
	.dev  = {
		.platform_data = &cn4_power_info,
	},
};

static void ag5evm_sdhi1_set_pwr(struct platform_device *pdev, int state)
{
	static int power_gpio = -EINVAL;

	if (power_gpio < 0) {
		int ret = gpio_request(GPIO_PORT114, "sdhi1_power");
		if (!ret) {
			power_gpio = GPIO_PORT114;
			gpio_direction_output(power_gpio, 0);
		}
	}

	/*
	 * If requesting the GPIO above failed, it means, that the regulator got
	 * probed and grabbed the GPIO, but we don't know, whether the sdhi
	 * driver already uses the regulator. If it doesn't, we have to toggle
	 * the GPIO ourselves, even though it is now owned by the fixed
	 * regulator driver. We have to live with the race in case the driver
	 * gets unloaded and the GPIO freed between these two steps.
	 */
	gpio_set_value(GPIO_PORT114, state);
}

static struct sh_mobile_sdhi_info sh_sdhi1_info = {
	.tmio_flags	= TMIO_MMC_WRPROTECT_DISABLE | TMIO_MMC_HAS_IDLE_WAIT,
	.tmio_caps	= MMC_CAP_NONREMOVABLE | MMC_CAP_SDIO_IRQ,
	.tmio_ocr_mask	= MMC_VDD_32_33 | MMC_VDD_33_34,
	.set_pwr	= ag5evm_sdhi1_set_pwr,
};

static struct resource sdhi1_resources[] = {
	[0] = {
		.name	= "SDHI1",
		.start	= 0xee120000,
		.end	= 0xee1200ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= SH_MOBILE_SDHI_IRQ_CARD_DETECT,
		.start	= gic_spi(87),
		.flags	= IORESOURCE_IRQ,
	},
	[2] = {
		.name	= SH_MOBILE_SDHI_IRQ_SDCARD,
		.start	= gic_spi(88),
		.flags	= IORESOURCE_IRQ,
	},
	[3] = {
		.name	= SH_MOBILE_SDHI_IRQ_SDIO,
		.start	= gic_spi(89),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi1_device = {
	.name		= "sh_mobile_sdhi",
	.id		= 1,
	.dev		= {
		.platform_data	= &sh_sdhi1_info,
	},
	.num_resources	= ARRAY_SIZE(sdhi1_resources),
	.resource	= sdhi1_resources,
};

static struct platform_device *ag5evm_devices[] __initdata = {
	&cn4_power,
	&eth_device,
	&keysc_device,
	&fsi_device,
	&mmc_device,
	&irda_device,
	&lcdc0_device,
	&mipidsi0_device,
	&sdhi0_device,
	&sdhi1_device,
};

static void __init ag5evm_init(void)
{
	regulator_register_always_on(0, "fixed-1.8V", fixed1v8_power_consumers,
				     ARRAY_SIZE(fixed1v8_power_consumers), 1800000);
	regulator_register_always_on(1, "fixed-2.8V", fixed2v8_power_consumers,
				     ARRAY_SIZE(fixed2v8_power_consumers), 3300000);
	regulator_register_fixed(3, dummy_supplies, ARRAY_SIZE(dummy_supplies));

	sh73a0_pinmux_init();

	/* enable SCIFA2 */
	gpio_request(GPIO_FN_SCIFA2_TXD1, NULL);
	gpio_request(GPIO_FN_SCIFA2_RXD1, NULL);
	gpio_request(GPIO_FN_SCIFA2_RTS1_, NULL);
	gpio_request(GPIO_FN_SCIFA2_CTS1_, NULL);

	/* enable KEYSC */
	gpio_request(GPIO_FN_KEYIN0_PU, NULL);
	gpio_request(GPIO_FN_KEYIN1_PU, NULL);
	gpio_request(GPIO_FN_KEYIN2_PU, NULL);
	gpio_request(GPIO_FN_KEYIN3_PU, NULL);
	gpio_request(GPIO_FN_KEYIN4_PU, NULL);
	gpio_request(GPIO_FN_KEYIN5_PU, NULL);
	gpio_request(GPIO_FN_KEYIN6_PU, NULL);
	gpio_request(GPIO_FN_KEYIN7_PU, NULL);
	gpio_request(GPIO_FN_KEYOUT0, NULL);
	gpio_request(GPIO_FN_KEYOUT1, NULL);
	gpio_request(GPIO_FN_KEYOUT2, NULL);
	gpio_request(GPIO_FN_KEYOUT3, NULL);
	gpio_request(GPIO_FN_KEYOUT4, NULL);
	gpio_request(GPIO_FN_KEYOUT5, NULL);
	gpio_request(GPIO_FN_PORT59_KEYOUT6, NULL);
	gpio_request(GPIO_FN_PORT58_KEYOUT7, NULL);
	gpio_request(GPIO_FN_KEYOUT8, NULL);
	gpio_request(GPIO_FN_PORT149_KEYOUT9, NULL);

	/* enable I2C channel 2 and 3 */
	gpio_request(GPIO_FN_PORT236_I2C_SDA2, NULL);
	gpio_request(GPIO_FN_PORT237_I2C_SCL2, NULL);
	gpio_request(GPIO_FN_PORT248_I2C_SCL3, NULL);
	gpio_request(GPIO_FN_PORT249_I2C_SDA3, NULL);

	/* enable MMCIF */
	gpio_request(GPIO_FN_MMCCLK0, NULL);
	gpio_request(GPIO_FN_MMCCMD0_PU, NULL);
	gpio_request(GPIO_FN_MMCD0_0_PU, NULL);
	gpio_request(GPIO_FN_MMCD0_1_PU, NULL);
	gpio_request(GPIO_FN_MMCD0_2_PU, NULL);
	gpio_request(GPIO_FN_MMCD0_3_PU, NULL);
	gpio_request(GPIO_FN_MMCD0_4_PU, NULL);
	gpio_request(GPIO_FN_MMCD0_5_PU, NULL);
	gpio_request(GPIO_FN_MMCD0_6_PU, NULL);
	gpio_request(GPIO_FN_MMCD0_7_PU, NULL);
	gpio_request(GPIO_PORT208, NULL); /* Reset */
	gpio_direction_output(GPIO_PORT208, 1);

	/* enable SMSC911X */
	gpio_request(GPIO_PORT144, NULL); /* PINTA2 */
	gpio_direction_input(GPIO_PORT144);
	gpio_request(GPIO_PORT145, NULL); /* RESET */
	gpio_direction_output(GPIO_PORT145, 1);

	/* FSI A */
	gpio_request(GPIO_FN_FSIACK, NULL);
	gpio_request(GPIO_FN_FSIAILR, NULL);
	gpio_request(GPIO_FN_FSIAIBT, NULL);
	gpio_request(GPIO_FN_FSIAISLD, NULL);
	gpio_request(GPIO_FN_FSIAOSLD, NULL);

	/* IrDA */
	gpio_request(GPIO_FN_PORT241_IRDA_OUT, NULL);
	gpio_request(GPIO_FN_PORT242_IRDA_IN,  NULL);
	gpio_request(GPIO_FN_PORT243_IRDA_FIRSEL, NULL);

	/* LCD panel */
	gpio_request(GPIO_PORT217, NULL); /* RESET */
	gpio_direction_output(GPIO_PORT217, 0);
	mdelay(1);
	gpio_set_value(GPIO_PORT217, 1);
	mdelay(100);

	/* LCD backlight controller */
	gpio_request(GPIO_PORT235, NULL); /* RESET */
	gpio_direction_output(GPIO_PORT235, 0);
	lcd_backlight_reset();

	/* enable SDHI0 on CN15 [SD I/F] */
	gpio_request(GPIO_FN_SDHIWP0, NULL);
	gpio_request(GPIO_FN_SDHICMD0, NULL);
	gpio_request(GPIO_FN_SDHICLK0, NULL);
	gpio_request(GPIO_FN_SDHID0_3, NULL);
	gpio_request(GPIO_FN_SDHID0_2, NULL);
	gpio_request(GPIO_FN_SDHID0_1, NULL);
	gpio_request(GPIO_FN_SDHID0_0, NULL);

	/* enable SDHI1 on CN4 [WLAN I/F] */
	gpio_request(GPIO_FN_SDHICLK1, NULL);
	gpio_request(GPIO_FN_SDHICMD1_PU, NULL);
	gpio_request(GPIO_FN_SDHID1_3_PU, NULL);
	gpio_request(GPIO_FN_SDHID1_2_PU, NULL);
	gpio_request(GPIO_FN_SDHID1_1_PU, NULL);
	gpio_request(GPIO_FN_SDHID1_0_PU, NULL);

#ifdef CONFIG_CACHE_L2X0
	/* Shared attribute override enable, 64K*8way */
	l2x0_init(IOMEM(0xf0100000), 0x00460000, 0xc2000fff);
#endif
	sh73a0_add_standard_devices();
	platform_add_devices(ag5evm_devices, ARRAY_SIZE(ag5evm_devices));
}

MACHINE_START(AG5EVM, "ag5evm")
	.smp		= smp_ops(sh73a0_smp_ops),
	.map_io		= sh73a0_map_io,
	.init_early	= sh73a0_add_early_devices,
	.nr_irqs	= NR_IRQS_LEGACY,
	.init_irq	= sh73a0_init_irq,
	.handle_irq	= gic_handle_irq,
	.init_machine	= ag5evm_init,
	.init_late	= shmobile_init_late,
	.timer		= &shmobile_timer,
MACHINE_END
