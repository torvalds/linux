/*
 * kota2 board support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
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
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/smsc911x.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/input/sh_keysc.h>
#include <linux/gpio_keys.h>
#include <linux/leds.h>
#include <linux/platform_data/leds-renesas-tpu.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/sh73a0.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/traps.h>

/* Dummy supplies, where voltage doesn't matter */
static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x"),
};

/* SMSC 9220 */
static struct resource smsc9220_resources[] = {
	[0] = {
		.start		= 0x14000000, /* CS5A */
		.end		= 0x140000ff, /* A1->A7 */
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= SH73A0_PINT0_IRQ(2), /* PINTA2 */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc9220_platdata = {
	.flags		= SMSC911X_USE_32BIT, /* 32-bit SW on 16-bit HW bus */
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

/* KEYSC */
static struct sh_keysc_info keysc_platdata = {
	.mode		= SH_KEYSC_MODE_6,
	.scan_timing	= 3,
	.delay		= 100,
	.keycodes	= {
		KEY_NUMERIC_STAR, KEY_NUMERIC_0, KEY_NUMERIC_POUND,
		0, 0, 0, 0, 0,
		KEY_NUMERIC_7, KEY_NUMERIC_8, KEY_NUMERIC_9,
		0, KEY_DOWN, 0, 0, 0,
		KEY_NUMERIC_4, KEY_NUMERIC_5, KEY_NUMERIC_6,
		KEY_LEFT, KEY_ENTER, KEY_RIGHT, 0, 0,
		KEY_NUMERIC_1, KEY_NUMERIC_2, KEY_NUMERIC_3,
		0, KEY_UP, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0,
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

/* GPIO KEY */
#define GPIO_KEY(c, g, d) { .code = c, .gpio = g, .desc = d, .active_low = 1 }

static struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_VOLUMEUP, GPIO_PORT56, "+"), /* S2: VOL+ [IRQ9] */
	GPIO_KEY(KEY_VOLUMEDOWN, GPIO_PORT54, "-"), /* S3: VOL- [IRQ10] */
	GPIO_KEY(KEY_MENU, GPIO_PORT27, "Menu"), /* S4: MENU [IRQ30] */
	GPIO_KEY(KEY_HOMEPAGE, GPIO_PORT26, "Home"), /* S5: HOME [IRQ31] */
	GPIO_KEY(KEY_BACK, GPIO_PORT11, "Back"), /* S6: BACK [IRQ0] */
	GPIO_KEY(KEY_PHONE, GPIO_PORT238, "Tel"), /* S7: TEL [IRQ11] */
	GPIO_KEY(KEY_POWER, GPIO_PORT239, "C1"), /* S8: CAM [IRQ13] */
	GPIO_KEY(KEY_MAIL, GPIO_PORT224, "Mail"), /* S9: MAIL [IRQ3] */
	/* Omitted button "C3?": GPIO_PORT223 - S10: CUST [IRQ8] */
	GPIO_KEY(KEY_CAMERA, GPIO_PORT164, "C2"), /* S11: CAM_HALF [IRQ25] */
	/* Omitted button "?": GPIO_PORT152 - S12: CAM_FULL [No IRQ] */
};

static struct gpio_keys_platform_data gpio_key_info = {
	.buttons        = gpio_buttons,
	.nbuttons       = ARRAY_SIZE(gpio_buttons),
};

static struct platform_device gpio_keys_device = {
	.name   = "gpio-keys",
	.id     = -1,
	.dev    = {
		.platform_data  = &gpio_key_info,
	},
};

/* GPIO LED */
#define GPIO_LED(n, g) { .name = n, .gpio = g }

static struct gpio_led gpio_leds[] = {
	GPIO_LED("G", GPIO_PORT20), /* PORT20 [GPO0] -> LED7 -> "G" */
	GPIO_LED("H", GPIO_PORT21), /* PORT21 [GPO1] -> LED8 -> "H" */
	GPIO_LED("J", GPIO_PORT22), /* PORT22 [GPO2] -> LED9 -> "J" */
};

static struct gpio_led_platform_data gpio_leds_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device gpio_leds_device = {
	.name   = "leds-gpio",
	.id     = -1,
	.dev    = {
		.platform_data  = &gpio_leds_info,
	},
};

/* TPU LED */
static struct led_renesas_tpu_config led_renesas_tpu12_pdata = {
	.name		= "V2513",
	.pin_gpio_fn	= GPIO_FN_TPU1TO2,
	.pin_gpio	= GPIO_PORT153,
	.channel_offset = 0x90,
	.timer_bit = 2,
	.max_brightness = 1000,
};

static struct resource tpu12_resources[] = {
	[0] = {
		.name	= "TPU12",
		.start	= 0xe6610090,
		.end	= 0xe66100b5,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device leds_tpu12_device = {
	.name = "leds-renesas-tpu",
	.id = 12,
	.dev = {
		.platform_data  = &led_renesas_tpu12_pdata,
	},
	.num_resources	= ARRAY_SIZE(tpu12_resources),
	.resource	= tpu12_resources,
};

static struct led_renesas_tpu_config led_renesas_tpu41_pdata = {
	.name		= "V2514",
	.pin_gpio_fn	= GPIO_FN_TPU4TO1,
	.pin_gpio	= GPIO_PORT199,
	.channel_offset = 0x50,
	.timer_bit = 1,
	.max_brightness = 1000,
};

static struct resource tpu41_resources[] = {
	[0] = {
		.name	= "TPU41",
		.start	= 0xe6640050,
		.end	= 0xe6640075,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device leds_tpu41_device = {
	.name = "leds-renesas-tpu",
	.id = 41,
	.dev = {
		.platform_data  = &led_renesas_tpu41_pdata,
	},
	.num_resources	= ARRAY_SIZE(tpu41_resources),
	.resource	= tpu41_resources,
};

static struct led_renesas_tpu_config led_renesas_tpu21_pdata = {
	.name		= "V2515",
	.pin_gpio_fn	= GPIO_FN_TPU2TO1,
	.pin_gpio	= GPIO_PORT197,
	.channel_offset = 0x50,
	.timer_bit = 1,
	.max_brightness = 1000,
};

static struct resource tpu21_resources[] = {
	[0] = {
		.name	= "TPU21",
		.start	= 0xe6620050,
		.end	= 0xe6620075,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device leds_tpu21_device = {
	.name = "leds-renesas-tpu",
	.id = 21,
	.dev = {
		.platform_data  = &led_renesas_tpu21_pdata,
	},
	.num_resources	= ARRAY_SIZE(tpu21_resources),
	.resource	= tpu21_resources,
};

static struct led_renesas_tpu_config led_renesas_tpu30_pdata = {
	.name		= "KEYLED",
	.pin_gpio_fn	= GPIO_FN_TPU3TO0,
	.pin_gpio	= GPIO_PORT163,
	.channel_offset = 0x10,
	.timer_bit = 0,
	.max_brightness = 1000,
};

static struct resource tpu30_resources[] = {
	[0] = {
		.name	= "TPU30",
		.start	= 0xe6630010,
		.end	= 0xe6630035,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device leds_tpu30_device = {
	.name = "leds-renesas-tpu",
	.id = 30,
	.dev = {
		.platform_data  = &led_renesas_tpu30_pdata,
	},
	.num_resources	= ARRAY_SIZE(tpu30_resources),
	.resource	= tpu30_resources,
};

/* Fixed 1.8V regulator to be used by MMCIF */
static struct regulator_consumer_supply fixed1v8_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mmcif.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mmcif.0"),
};

/* MMCIF */
static struct resource mmcif_resources[] = {
	[0] = {
		.name   = "MMCIF",
		.start  = 0xe6bd0000,
		.end    = 0xe6bd00ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = gic_spi(140),
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		.start  = gic_spi(141),
		.flags  = IORESOURCE_IRQ,
	},
};

static struct sh_mmcif_plat_data mmcif_info = {
	.ocr            = MMC_VDD_165_195,
	.caps           = MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE,
};

static struct platform_device mmcif_device = {
	.name           = "sh_mmcif",
	.id             = 0,
	.dev            = {
		.platform_data          = &mmcif_info,
	},
	.num_resources  = ARRAY_SIZE(mmcif_resources),
	.resource       = mmcif_resources,
};

/* Fixed 3.3V regulator to be used by SDHI0 and SDHI1 */
static struct regulator_consumer_supply fixed3v3_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.0"),
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.1"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.1"),
};

/* SDHI0 */
static struct sh_mobile_sdhi_info sdhi0_info = {
	.tmio_caps      = MMC_CAP_SD_HIGHSPEED,
	.tmio_flags     = TMIO_MMC_WRPROTECT_DISABLE | TMIO_MMC_HAS_IDLE_WAIT,
};

static struct resource sdhi0_resources[] = {
	[0] = {
		.name   = "SDHI0",
		.start  = 0xee100000,
		.end    = 0xee1000ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = gic_spi(83),
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		.start  = gic_spi(84),
		.flags  = IORESOURCE_IRQ,
	},
	[3] = {
		.start	= gic_spi(85),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi0_device = {
	.name           = "sh_mobile_sdhi",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(sdhi0_resources),
	.resource       = sdhi0_resources,
	.dev    = {
		.platform_data  = &sdhi0_info,
	},
};

/* SDHI1 */
static struct sh_mobile_sdhi_info sdhi1_info = {
	.tmio_caps      = MMC_CAP_NONREMOVABLE | MMC_CAP_SDIO_IRQ,
	.tmio_flags     = TMIO_MMC_WRPROTECT_DISABLE | TMIO_MMC_HAS_IDLE_WAIT,
};

static struct resource sdhi1_resources[] = {
	[0] = {
		.name   = "SDHI1",
		.start  = 0xee120000,
		.end    = 0xee1200ff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = gic_spi(87),
		.flags  = IORESOURCE_IRQ,
	},
	[2] = {
		.start  = gic_spi(88),
		.flags  = IORESOURCE_IRQ,
	},
	[3] = {
		.start	= gic_spi(89),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device sdhi1_device = {
	.name           = "sh_mobile_sdhi",
	.id             = 1,
	.num_resources  = ARRAY_SIZE(sdhi1_resources),
	.resource       = sdhi1_resources,
	.dev    = {
		.platform_data  = &sdhi1_info,
	},
};

static struct platform_device *kota2_devices[] __initdata = {
	&eth_device,
	&keysc_device,
	&gpio_keys_device,
	&gpio_leds_device,
	&leds_tpu12_device,
	&leds_tpu41_device,
	&leds_tpu21_device,
	&leds_tpu30_device,
	&mmcif_device,
	&sdhi0_device,
	&sdhi1_device,
};

static void __init kota2_init(void)
{
	regulator_register_always_on(0, "fixed-1.8V", fixed1v8_power_consumers,
				     ARRAY_SIZE(fixed1v8_power_consumers), 1800000);
	regulator_register_always_on(1, "fixed-3.3V", fixed3v3_power_consumers,
				     ARRAY_SIZE(fixed3v3_power_consumers), 3300000);
	regulator_register_fixed(2, dummy_supplies, ARRAY_SIZE(dummy_supplies));

	sh73a0_pinmux_init();

	/* SCIFA2 (UART2) */
	gpio_request(GPIO_FN_SCIFA2_TXD1, NULL);
	gpio_request(GPIO_FN_SCIFA2_RXD1, NULL);
	gpio_request(GPIO_FN_SCIFA2_RTS1_, NULL);
	gpio_request(GPIO_FN_SCIFA2_CTS1_, NULL);

	/* SCIFA4 (UART1) */
	gpio_request(GPIO_FN_SCIFA4_TXD, NULL);
	gpio_request(GPIO_FN_SCIFA4_RXD, NULL);
	gpio_request(GPIO_FN_SCIFA4_RTS_, NULL);
	gpio_request(GPIO_FN_SCIFA4_CTS_, NULL);

	/* SMSC911X */
	gpio_request(GPIO_FN_D0_NAF0, NULL);
	gpio_request(GPIO_FN_D1_NAF1, NULL);
	gpio_request(GPIO_FN_D2_NAF2, NULL);
	gpio_request(GPIO_FN_D3_NAF3, NULL);
	gpio_request(GPIO_FN_D4_NAF4, NULL);
	gpio_request(GPIO_FN_D5_NAF5, NULL);
	gpio_request(GPIO_FN_D6_NAF6, NULL);
	gpio_request(GPIO_FN_D7_NAF7, NULL);
	gpio_request(GPIO_FN_D8_NAF8, NULL);
	gpio_request(GPIO_FN_D9_NAF9, NULL);
	gpio_request(GPIO_FN_D10_NAF10, NULL);
	gpio_request(GPIO_FN_D11_NAF11, NULL);
	gpio_request(GPIO_FN_D12_NAF12, NULL);
	gpio_request(GPIO_FN_D13_NAF13, NULL);
	gpio_request(GPIO_FN_D14_NAF14, NULL);
	gpio_request(GPIO_FN_D15_NAF15, NULL);
	gpio_request(GPIO_FN_CS5A_, NULL);
	gpio_request(GPIO_FN_WE0__FWE, NULL);
	gpio_request(GPIO_PORT144, NULL); /* PINTA2 */
	gpio_direction_input(GPIO_PORT144);
	gpio_request(GPIO_PORT145, NULL); /* RESET */
	gpio_direction_output(GPIO_PORT145, 1);

	/* KEYSC */
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

	/* MMCIF */
	gpio_request(GPIO_FN_MMCCLK0, NULL);
	gpio_request(GPIO_FN_MMCD0_0, NULL);
	gpio_request(GPIO_FN_MMCD0_1, NULL);
	gpio_request(GPIO_FN_MMCD0_2, NULL);
	gpio_request(GPIO_FN_MMCD0_3, NULL);
	gpio_request(GPIO_FN_MMCD0_4, NULL);
	gpio_request(GPIO_FN_MMCD0_5, NULL);
	gpio_request(GPIO_FN_MMCD0_6, NULL);
	gpio_request(GPIO_FN_MMCD0_7, NULL);
	gpio_request(GPIO_FN_MMCCMD0, NULL);
	gpio_request(GPIO_PORT208, NULL); /* Reset */
	gpio_direction_output(GPIO_PORT208, 1);

	/* SDHI0 (microSD) */
	gpio_request(GPIO_FN_SDHICD0_PU, NULL);
	gpio_request(GPIO_FN_SDHICMD0_PU, NULL);
	gpio_request(GPIO_FN_SDHICLK0, NULL);
	gpio_request(GPIO_FN_SDHID0_3_PU, NULL);
	gpio_request(GPIO_FN_SDHID0_2_PU, NULL);
	gpio_request(GPIO_FN_SDHID0_1_PU, NULL);
	gpio_request(GPIO_FN_SDHID0_0_PU, NULL);

	/* SCIFB (BT) */
	gpio_request(GPIO_FN_PORT159_SCIFB_SCK, NULL);
	gpio_request(GPIO_FN_PORT160_SCIFB_TXD, NULL);
	gpio_request(GPIO_FN_PORT161_SCIFB_CTS_, NULL);
	gpio_request(GPIO_FN_PORT162_SCIFB_RXD, NULL);
	gpio_request(GPIO_FN_PORT163_SCIFB_RTS_, NULL);

	/* SDHI1 (BCM4330) */
	gpio_request(GPIO_FN_SDHICLK1, NULL);
	gpio_request(GPIO_FN_SDHICMD1_PU, NULL);
	gpio_request(GPIO_FN_SDHID1_3_PU, NULL);
	gpio_request(GPIO_FN_SDHID1_2_PU, NULL);
	gpio_request(GPIO_FN_SDHID1_1_PU, NULL);
	gpio_request(GPIO_FN_SDHID1_0_PU, NULL);

#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, Shared attribute override enable, 64K*8way */
	l2x0_init(IOMEM(0xf0100000), 0x40460000, 0x82000fff);
#endif
	sh73a0_add_standard_devices();
	platform_add_devices(kota2_devices, ARRAY_SIZE(kota2_devices));
}

MACHINE_START(KOTA2, "kota2")
	.smp		= smp_ops(sh73a0_smp_ops),
	.map_io		= sh73a0_map_io,
	.init_early	= sh73a0_add_early_devices,
	.nr_irqs	= NR_IRQS_LEGACY,
	.init_irq	= sh73a0_init_irq,
	.handle_irq	= gic_handle_irq,
	.init_machine	= kota2_init,
	.init_late	= shmobile_init_late,
	.timer		= &shmobile_timer,
MACHINE_END
