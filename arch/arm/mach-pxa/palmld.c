// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hardware definitions for Palm LifeDrive
 *
 * Author:     Marek Vasut <marek.vasut@gmail.com>
 *
 * Based on work of:
 *		Alex Osborne <ato@meshy.org>
 *
 * (find more info at www.hackndev.com)
 */

#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/pda_power.h>
#include <linux/pwm_backlight.h>
#include <linux/gpio.h>
#include <linux/wm97xx.h>
#include <linux/power_supply.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/physmap.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "pxa27x.h"
#include "palmld.h"
#include <linux/platform_data/asoc-pxa.h>
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/video-pxafb.h>
#include <linux/platform_data/irda-pxaficp.h>
#include <linux/platform_data/keypad-pxa27x.h>
#include <linux/platform_data/asoc-palm27x.h>
#include "palm27x.h"

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * Pin configuration
 ******************************************************************************/
static unsigned long palmld_pin_config[] __initdata = {
	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
	GPIO14_GPIO,	/* SD detect */
	GPIO114_GPIO,	/* SD power */
	GPIO116_GPIO,	/* SD r/o switch */

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO89_AC97_SYSCLK,
	GPIO95_AC97_nRESET,

	/* IrDA */
	GPIO108_GPIO,	/* ir disable */
	GPIO46_FICP_RXD,
	GPIO47_FICP_TXD,

	/* MATRIX KEYPAD */
	GPIO100_KP_MKIN_0 | WAKEUP_ON_LEVEL_HIGH,
	GPIO101_KP_MKIN_1 | WAKEUP_ON_LEVEL_HIGH,
	GPIO102_KP_MKIN_2 | WAKEUP_ON_LEVEL_HIGH,
	GPIO97_KP_MKIN_3 | WAKEUP_ON_LEVEL_HIGH,
	GPIO103_KP_MKOUT_0,
	GPIO104_KP_MKOUT_1,
	GPIO105_KP_MKOUT_2,

	/* LCD */
	GPIOxx_LCD_TFT_16BPP,

	/* PWM */
	GPIO16_PWM0_OUT,

	/* GPIO KEYS */
	GPIO10_GPIO,	/* hotsync button */
	GPIO12_GPIO,	/* power switch */
	GPIO15_GPIO,	/* lock switch */

	/* LEDs */
	GPIO52_GPIO,	/* green led */
	GPIO94_GPIO,	/* orange led */

	/* PCMCIA */
	GPIO48_nPOE,
	GPIO49_nPWE,
	GPIO50_nPIOR,
	GPIO51_nPIOW,
	GPIO85_nPCE_1,
	GPIO54_nPCE_2,
	GPIO79_PSKTSEL,
	GPIO55_nPREG,
	GPIO56_nPWAIT,
	GPIO57_nIOIS16,
	GPIO36_GPIO,	/* wifi power */
	GPIO38_GPIO,	/* wifi ready */
	GPIO81_GPIO,	/* wifi reset */

	/* FFUART */
	GPIO34_FFUART_RXD,
	GPIO39_FFUART_TXD,

	/* HDD */
	GPIO98_GPIO,	/* HDD reset */
	GPIO115_GPIO,	/* HDD power */

	/* MISC */
	GPIO13_GPIO,	/* earphone detect */
};

/******************************************************************************
 * NOR Flash
 ******************************************************************************/
#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct mtd_partition palmld_partitions[] = {
	{
		.name		= "Flash",
		.offset		= 0x00000000,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0
	}
};

static struct physmap_flash_data palmld_flash_data[] = {
	{
		.width		= 2,			/* bankwidth in bytes */
		.parts		= palmld_partitions,
		.nr_parts	= ARRAY_SIZE(palmld_partitions)
	}
};

static struct resource palmld_flash_resource = {
	.start	= PXA_CS0_PHYS,
	.end	= PXA_CS0_PHYS + SZ_4M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device palmld_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.resource	= &palmld_flash_resource,
	.num_resources	= 1,
	.dev 		= {
		.platform_data = palmld_flash_data,
	},
};

static void __init palmld_nor_init(void)
{
	platform_device_register(&palmld_flash);
}
#else
static inline void palmld_nor_init(void) {}
#endif

/******************************************************************************
 * GPIO keyboard
 ******************************************************************************/
#if defined(CONFIG_KEYBOARD_PXA27x) || defined(CONFIG_KEYBOARD_PXA27x_MODULE)
static const unsigned int palmld_matrix_keys[] = {
	KEY(0, 1, KEY_F2),
	KEY(0, 2, KEY_UP),

	KEY(1, 0, KEY_F3),
	KEY(1, 1, KEY_F4),
	KEY(1, 2, KEY_RIGHT),

	KEY(2, 0, KEY_F1),
	KEY(2, 1, KEY_F5),
	KEY(2, 2, KEY_DOWN),

	KEY(3, 0, KEY_F6),
	KEY(3, 1, KEY_ENTER),
	KEY(3, 2, KEY_LEFT),
};

static struct matrix_keymap_data palmld_matrix_keymap_data = {
	.keymap			= palmld_matrix_keys,
	.keymap_size		= ARRAY_SIZE(palmld_matrix_keys),
};

static struct pxa27x_keypad_platform_data palmld_keypad_platform_data = {
	.matrix_key_rows	= 4,
	.matrix_key_cols	= 3,
	.matrix_keymap_data	= &palmld_matrix_keymap_data,

	.debounce_interval	= 30,
};

static void __init palmld_kpc_init(void)
{
	pxa_set_keypad_info(&palmld_keypad_platform_data);
}
#else
static inline void palmld_kpc_init(void) {}
#endif

/******************************************************************************
 * GPIO keys
 ******************************************************************************/
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button palmld_pxa_buttons[] = {
	{KEY_F8, GPIO_NR_PALMLD_HOTSYNC_BUTTON_N, 1, "HotSync Button" },
	{KEY_F9, GPIO_NR_PALMLD_LOCK_SWITCH, 0, "Lock Switch" },
	{KEY_POWER, GPIO_NR_PALMLD_POWER_SWITCH, 0, "Power Switch" },
};

static struct gpio_keys_platform_data palmld_pxa_keys_data = {
	.buttons	= palmld_pxa_buttons,
	.nbuttons	= ARRAY_SIZE(palmld_pxa_buttons),
};

static struct platform_device palmld_pxa_keys = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data = &palmld_pxa_keys_data,
	},
};

static void __init palmld_keys_init(void)
{
	platform_device_register(&palmld_pxa_keys);
}
#else
static inline void palmld_keys_init(void) {}
#endif

/******************************************************************************
 * LEDs
 ******************************************************************************/
#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
struct gpio_led gpio_leds[] = {
{
	.name			= "palmld:green:led",
	.default_trigger	= "none",
	.gpio			= GPIO_NR_PALMLD_LED_GREEN,
}, {
	.name			= "palmld:amber:led",
	.default_trigger	= "none",
	.gpio			= GPIO_NR_PALMLD_LED_AMBER,
},
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device palmld_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_led_info,
	}
};

static void __init palmld_leds_init(void)
{
	platform_device_register(&palmld_leds);
}
#else
static inline void palmld_leds_init(void) {}
#endif

/******************************************************************************
 * HDD
 ******************************************************************************/
#if defined(CONFIG_PATA_PALMLD) || defined(CONFIG_PATA_PALMLD_MODULE)
static struct resource palmld_ide_resources[] = {
	DEFINE_RES_MEM(PALMLD_IDE_PHYS, 0x1000),
};

static struct platform_device palmld_ide_device = {
	.name		= "pata_palmld",
	.id		= -1,
	.resource	= palmld_ide_resources,
	.num_resources	= ARRAY_SIZE(palmld_ide_resources),
};

static struct gpiod_lookup_table palmld_ide_gpio_table = {
	.dev_id = "pata_palmld",
	.table = {
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMLD_IDE_PWEN,
			    "power", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMLD_IDE_RESET,
			    "reset", GPIO_ACTIVE_LOW),
		{ },
	},
};

static void __init palmld_ide_init(void)
{
	gpiod_add_lookup_table(&palmld_ide_gpio_table);
	platform_device_register(&palmld_ide_device);
}
#else
static inline void palmld_ide_init(void) {}
#endif

/******************************************************************************
 * Machine init
 ******************************************************************************/
static struct map_desc palmld_io_desc[] __initdata = {
{
	.virtual	= PALMLD_IDE_VIRT,
	.pfn		= __phys_to_pfn(PALMLD_IDE_PHYS),
	.length		= PALMLD_IDE_SIZE,
	.type		= MT_DEVICE
},
{
	.virtual	= PALMLD_USB_VIRT,
	.pfn		= __phys_to_pfn(PALMLD_USB_PHYS),
	.length		= PALMLD_USB_SIZE,
	.type		= MT_DEVICE
},
};

static void __init palmld_map_io(void)
{
	pxa27x_map_io();
	iotable_init(palmld_io_desc, ARRAY_SIZE(palmld_io_desc));
}

static struct gpiod_lookup_table palmld_mci_gpio_table = {
	.dev_id = "pxa2xx-mci.0",
	.table = {
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMLD_SD_DETECT_N,
			    "cd", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMLD_SD_READONLY,
			    "wp", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMLD_SD_POWER,
			    "power", GPIO_ACTIVE_HIGH),
		{ },
	},
};

static struct gpiod_lookup_table palmld_wm97xx_touch_gpio_table = {
	.dev_id = "wm97xx-touch",
	.table = {
		GPIO_LOOKUP("gpio-pxa", 27, "touch", GPIO_ACTIVE_HIGH),
		{ },
	},
};

static void __init palmld_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(palmld_pin_config));
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	palm27x_mmc_init(&palmld_mci_gpio_table);
	gpiod_add_lookup_table(&palmld_wm97xx_touch_gpio_table);
	palm27x_pm_init(PALMLD_STR_BASE);
	palm27x_lcd_init(-1, &palm_320x480_lcd_mode);
	palm27x_irda_init(GPIO_NR_PALMLD_IR_DISABLE);
	palm27x_ac97_init(PALMLD_BAT_MIN_VOLTAGE, PALMLD_BAT_MAX_VOLTAGE,
			GPIO_NR_PALMLD_EARPHONE_DETECT, 95);
	palm27x_pwm_init(GPIO_NR_PALMLD_BL_POWER, GPIO_NR_PALMLD_LCD_POWER);
	palm27x_power_init(GPIO_NR_PALMLD_POWER_DETECT,
			GPIO_NR_PALMLD_USB_DETECT_N);
	palm27x_pmic_init();
	palmld_kpc_init();
	palmld_keys_init();
	palmld_nor_init();
	palmld_leds_init();
	palmld_ide_init();
}

MACHINE_START(PALMLD, "Palm LifeDrive")
	.atag_offset	= 0x100,
	.map_io		= palmld_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa27x_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.init_time	= pxa_timer_init,
	.init_machine	= palmld_init,
	.restart	= pxa_restart,
MACHINE_END
