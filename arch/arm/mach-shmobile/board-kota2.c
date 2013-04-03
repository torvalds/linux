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
#include <linux/pinctrl/machine.h>
#include <linux/pinctrl/pinconf-generic.h>
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
#include <linux/irqchip/arm-gic.h>
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
	GPIO_KEY(KEY_VOLUMEUP, 56, "+"), /* S2: VOL+ [IRQ9] */
	GPIO_KEY(KEY_VOLUMEDOWN, 54, "-"), /* S3: VOL- [IRQ10] */
	GPIO_KEY(KEY_MENU, 27, "Menu"), /* S4: MENU [IRQ30] */
	GPIO_KEY(KEY_HOMEPAGE, 26, "Home"), /* S5: HOME [IRQ31] */
	GPIO_KEY(KEY_BACK, 11, "Back"), /* S6: BACK [IRQ0] */
	GPIO_KEY(KEY_PHONE, 238, "Tel"), /* S7: TEL [IRQ11] */
	GPIO_KEY(KEY_POWER, 239, "C1"), /* S8: CAM [IRQ13] */
	GPIO_KEY(KEY_MAIL, 224, "Mail"), /* S9: MAIL [IRQ3] */
	/* Omitted button "C3?": 223 - S10: CUST [IRQ8] */
	GPIO_KEY(KEY_CAMERA, 164, "C2"), /* S11: CAM_HALF [IRQ25] */
	/* Omitted button "?": 152 - S12: CAM_FULL [No IRQ] */
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
	GPIO_LED("G", 20), /* PORT20 [GPO0] -> LED7 -> "G" */
	GPIO_LED("H", 21), /* PORT21 [GPO1] -> LED8 -> "H" */
	GPIO_LED("J", 22), /* PORT22 [GPO2] -> LED9 -> "J" */
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
	.pin_gpio	= 153,
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
	.pin_gpio	= 199,
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
	.pin_gpio	= 197,
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
	.pin_gpio	= 163,
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

static unsigned long pin_pullup_conf[] = {
	PIN_CONF_PACKED(PIN_CONFIG_BIAS_PULL_UP, 0),
};

static const struct pinctrl_map kota2_pinctrl_map[] = {
	/* KEYSC */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_keysc.0", "pfc-sh73a0",
				  "keysc_in8", "keysc"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_keysc.0", "pfc-sh73a0",
				  "keysc_out04", "keysc"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_keysc.0", "pfc-sh73a0",
				  "keysc_out5", "keysc"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_keysc.0", "pfc-sh73a0",
				  "keysc_out6_0", "keysc"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_keysc.0", "pfc-sh73a0",
				  "keysc_out7_0", "keysc"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_keysc.0", "pfc-sh73a0",
				  "keysc_out8_0", "keysc"),
	PIN_MAP_CONFIGS_GROUP_DEFAULT("sh_keysc.0", "pfc-sh73a0",
				      "keysc_in8", pin_pullup_conf),
	/* MMCIF */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.0", "pfc-sh73a0",
				  "mmc0_data8_0", "mmc0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.0", "pfc-sh73a0",
				  "mmc0_ctrl_0", "mmc0"),
	PIN_MAP_CONFIGS_PIN_DEFAULT("sh_mmcif.0", "pfc-sh73a0",
				    "PORT279", pin_pullup_conf),
	PIN_MAP_CONFIGS_GROUP_DEFAULT("sh_mmcif.0", "pfc-sh73a0",
				      "mmc0_data8_0", pin_pullup_conf),
	/* SCIFA2 (UART2) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.2", "pfc-sh73a0",
				  "scifa2_data_0", "scifa2"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.2", "pfc-sh73a0",
				  "scifa2_ctrl_0", "scifa2"),
	/* SCIFA4 (UART1) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.4", "pfc-sh73a0",
				  "scifa4_data", "scifa4"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.4", "pfc-sh73a0",
				  "scifa4_ctrl", "scifa4"),
	/* SCIFB (BT) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.8", "pfc-sh73a0",
				  "scifb_data_0", "scifb"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.8", "pfc-sh73a0",
				  "scifb_clk_0", "scifb"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.8", "pfc-sh73a0",
				  "scifb_ctrl_0", "scifb"),
	/* SDHI0 (microSD) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh73a0",
				  "sdhi0_data4", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh73a0",
				  "sdhi0_ctrl", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh73a0",
				  "sdhi0_cd", "sdhi0"),
	PIN_MAP_CONFIGS_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-sh73a0",
				      "sdhi0_data4", pin_pullup_conf),
	PIN_MAP_CONFIGS_PIN_DEFAULT("sh_mobile_sdhi.0", "pfc-sh73a0",
				    "PORT256", pin_pullup_conf),
	PIN_MAP_CONFIGS_PIN_DEFAULT("sh_mobile_sdhi.0", "pfc-sh73a0",
				    "PORT251", pin_pullup_conf),
	/* SDHI1 (BCM4330) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-sh73a0",
				  "sdhi1_data4", "sdhi1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-sh73a0",
				  "sdhi1_ctrl", "sdhi1"),
	PIN_MAP_CONFIGS_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-sh73a0",
				      "sdhi1_data4", pin_pullup_conf),
	PIN_MAP_CONFIGS_PIN_DEFAULT("sh_mobile_sdhi.1", "pfc-sh73a0",
				    "PORT263", pin_pullup_conf),
	/* SMSC911X */
	PIN_MAP_MUX_GROUP_DEFAULT("smsc911x.0", "pfc-sh73a0",
				  "bsc_data_0_7", "bsc"),
	PIN_MAP_MUX_GROUP_DEFAULT("smsc911x.0", "pfc-sh73a0",
				  "bsc_data_8_15", "bsc"),
	PIN_MAP_MUX_GROUP_DEFAULT("smsc911x.0", "pfc-sh73a0",
				  "bsc_cs5_a", "bsc"),
	PIN_MAP_MUX_GROUP_DEFAULT("smsc911x.0", "pfc-sh73a0",
				  "bsc_we0", "bsc"),
};

static void __init kota2_init(void)
{
	regulator_register_always_on(0, "fixed-1.8V", fixed1v8_power_consumers,
				     ARRAY_SIZE(fixed1v8_power_consumers), 1800000);
	regulator_register_always_on(1, "fixed-3.3V", fixed3v3_power_consumers,
				     ARRAY_SIZE(fixed3v3_power_consumers), 3300000);
	regulator_register_fixed(2, dummy_supplies, ARRAY_SIZE(dummy_supplies));

	pinctrl_register_mappings(kota2_pinctrl_map,
				  ARRAY_SIZE(kota2_pinctrl_map));
	sh73a0_pinmux_init();

	/* SMSC911X */
	gpio_request_one(144, GPIOF_IN, NULL); /* PINTA2 */
	gpio_request_one(145, GPIOF_OUT_INIT_HIGH, NULL); /* RESET */

	/* MMCIF */
	gpio_request_one(208, GPIOF_OUT_INIT_HIGH, NULL); /* Reset */

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
	.init_machine	= kota2_init,
	.init_late	= shmobile_init_late,
	.init_time	= sh73a0_earlytimer_init,
MACHINE_END
