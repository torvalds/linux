// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hardware definitions for PalmTX
 *
 * Author:     Marek Vasut <marek.vasut@gmail.com>
 *
 * Based on work of:
 *		Alex Osborne <ato@meshy.org>
 *		Cristiano P. <cristianop@users.sourceforge.net>
 *		Jan Herman <2hp@seznam.cz>
 *		Michal Hrusecky
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
#include <linux/mtd/platnand.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/physmap.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "pxa27x.h"
#include <mach/audio.h>
#include <mach/palmtx.h>
#include <linux/platform_data/mmc-pxamci.h>
#include <linux/platform_data/video-pxafb.h>
#include <linux/platform_data/irda-pxaficp.h>
#include <linux/platform_data/keypad-pxa27x.h>
#include "udc.h"
#include <linux/platform_data/asoc-palm27x.h>
#include "palm27x.h"

#include "generic.h"
#include "devices.h"

/******************************************************************************
 * Pin configuration
 ******************************************************************************/
static unsigned long palmtx_pin_config[] __initdata = {
	/* MMC */
	GPIO32_MMC_CLK,
	GPIO92_MMC_DAT_0,
	GPIO109_MMC_DAT_1,
	GPIO110_MMC_DAT_2,
	GPIO111_MMC_DAT_3,
	GPIO112_MMC_CMD,
	GPIO14_GPIO,	/* SD detect */
	GPIO114_GPIO,	/* SD power */
	GPIO115_GPIO,	/* SD r/o switch */

	/* AC97 */
	GPIO28_AC97_BITCLK,
	GPIO29_AC97_SDATA_IN_0,
	GPIO30_AC97_SDATA_OUT,
	GPIO31_AC97_SYNC,
	GPIO89_AC97_SYSCLK,
	GPIO95_AC97_nRESET,

	/* IrDA */
	GPIO40_GPIO,	/* ir disable */
	GPIO46_FICP_RXD,
	GPIO47_FICP_TXD,

	/* PWM */
	GPIO16_PWM0_OUT,

	/* USB */
	GPIO13_GPIO,	/* usb detect */
	GPIO93_GPIO,	/* usb power */

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
	GPIO94_GPIO,	/* wifi power 1 */
	GPIO108_GPIO,	/* wifi power 2 */
	GPIO116_GPIO,	/* wifi ready */

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

	/* FFUART */
	GPIO34_FFUART_RXD,
	GPIO39_FFUART_TXD,

	/* NAND */
	GPIO15_nCS_1,
	GPIO18_RDY,

	/* MISC. */
	GPIO10_GPIO,	/* hotsync button */
	GPIO12_GPIO,	/* power detect */
	GPIO107_GPIO,	/* earphone detect */
};

/******************************************************************************
 * NOR Flash
 ******************************************************************************/
#if defined(CONFIG_MTD_PHYSMAP) || defined(CONFIG_MTD_PHYSMAP_MODULE)
static struct mtd_partition palmtx_partitions[] = {
	{
		.name		= "Flash",
		.offset		= 0x00000000,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0
	}
};

static struct physmap_flash_data palmtx_flash_data[] = {
	{
		.width		= 2,			/* bankwidth in bytes */
		.parts		= palmtx_partitions,
		.nr_parts	= ARRAY_SIZE(palmtx_partitions)
	}
};

static struct resource palmtx_flash_resource = {
	.start	= PXA_CS0_PHYS,
	.end	= PXA_CS0_PHYS + SZ_8M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device palmtx_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.resource	= &palmtx_flash_resource,
	.num_resources	= 1,
	.dev 		= {
		.platform_data = palmtx_flash_data,
	},
};

static void __init palmtx_nor_init(void)
{
	platform_device_register(&palmtx_flash);
}
#else
static inline void palmtx_nor_init(void) {}
#endif

/******************************************************************************
 * GPIO keyboard
 ******************************************************************************/
#if defined(CONFIG_KEYBOARD_PXA27x) || defined(CONFIG_KEYBOARD_PXA27x_MODULE)
static const unsigned int palmtx_matrix_keys[] = {
	KEY(0, 0, KEY_POWER),
	KEY(0, 1, KEY_F1),
	KEY(0, 2, KEY_ENTER),

	KEY(1, 0, KEY_F2),
	KEY(1, 1, KEY_F3),
	KEY(1, 2, KEY_F4),

	KEY(2, 0, KEY_UP),
	KEY(2, 2, KEY_DOWN),

	KEY(3, 0, KEY_RIGHT),
	KEY(3, 2, KEY_LEFT),
};

static struct matrix_keymap_data palmtx_matrix_keymap_data = {
	.keymap			= palmtx_matrix_keys,
	.keymap_size		= ARRAY_SIZE(palmtx_matrix_keys),
};

static struct pxa27x_keypad_platform_data palmtx_keypad_platform_data = {
	.matrix_key_rows	= 4,
	.matrix_key_cols	= 3,
	.matrix_keymap_data	= &palmtx_matrix_keymap_data,

	.debounce_interval	= 30,
};

static void __init palmtx_kpc_init(void)
{
	pxa_set_keypad_info(&palmtx_keypad_platform_data);
}
#else
static inline void palmtx_kpc_init(void) {}
#endif

/******************************************************************************
 * GPIO keys
 ******************************************************************************/
#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)
static struct gpio_keys_button palmtx_pxa_buttons[] = {
	{KEY_F8, GPIO_NR_PALMTX_HOTSYNC_BUTTON_N, 1, "HotSync Button" },
};

static struct gpio_keys_platform_data palmtx_pxa_keys_data = {
	.buttons	= palmtx_pxa_buttons,
	.nbuttons	= ARRAY_SIZE(palmtx_pxa_buttons),
};

static struct platform_device palmtx_pxa_keys = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data = &palmtx_pxa_keys_data,
	},
};

static void __init palmtx_keys_init(void)
{
	platform_device_register(&palmtx_pxa_keys);
}
#else
static inline void palmtx_keys_init(void) {}
#endif

/******************************************************************************
 * NAND Flash
 ******************************************************************************/
#if defined(CONFIG_MTD_NAND_PLATFORM) || \
	defined(CONFIG_MTD_NAND_PLATFORM_MODULE)
static void palmtx_nand_cmd_ctl(struct nand_chip *this, int cmd,
				unsigned int ctrl)
{
	char __iomem *nandaddr = this->legacy.IO_ADDR_W;

	if (cmd == NAND_CMD_NONE)
		return;

	if (ctrl & NAND_CLE)
		writeb(cmd, PALMTX_NAND_CLE_VIRT);
	else if (ctrl & NAND_ALE)
		writeb(cmd, PALMTX_NAND_ALE_VIRT);
	else
		writeb(cmd, nandaddr);
}

static struct mtd_partition palmtx_partition_info[] = {
	[0] = {
		.name	= "palmtx-0",
		.offset	= 0,
		.size	= MTDPART_SIZ_FULL
	},
};

struct platform_nand_data palmtx_nand_platdata = {
	.chip	= {
		.nr_chips		= 1,
		.chip_offset		= 0,
		.nr_partitions		= ARRAY_SIZE(palmtx_partition_info),
		.partitions		= palmtx_partition_info,
		.chip_delay		= 20,
	},
	.ctrl	= {
		.cmd_ctrl	= palmtx_nand_cmd_ctl,
	},
};

static struct resource palmtx_nand_resource[] = {
	[0]	= {
		.start	= PXA_CS1_PHYS,
		.end	= PXA_CS1_PHYS + SZ_1M - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device palmtx_nand = {
	.name		= "gen_nand",
	.num_resources	= ARRAY_SIZE(palmtx_nand_resource),
	.resource	= palmtx_nand_resource,
	.id		= -1,
	.dev		= {
		.platform_data	= &palmtx_nand_platdata,
	}
};

static void __init palmtx_nand_init(void)
{
	platform_device_register(&palmtx_nand);
}
#else
static inline void palmtx_nand_init(void) {}
#endif

/******************************************************************************
 * Machine init
 ******************************************************************************/
static struct map_desc palmtx_io_desc[] __initdata = {
{
	.virtual	= (unsigned long)PALMTX_PCMCIA_VIRT,
	.pfn		= __phys_to_pfn(PALMTX_PCMCIA_PHYS),
	.length		= PALMTX_PCMCIA_SIZE,
	.type		= MT_DEVICE,
}, {
	.virtual	= (unsigned long)PALMTX_NAND_ALE_VIRT,
	.pfn		= __phys_to_pfn(PALMTX_NAND_ALE_PHYS),
	.length		= SZ_1M,
	.type		= MT_DEVICE,
}, {
	.virtual	= (unsigned long)PALMTX_NAND_CLE_VIRT,
	.pfn		= __phys_to_pfn(PALMTX_NAND_CLE_PHYS),
	.length		= SZ_1M,
	.type		= MT_DEVICE,
}
};

static void __init palmtx_map_io(void)
{
	pxa27x_map_io();
	iotable_init(palmtx_io_desc, ARRAY_SIZE(palmtx_io_desc));
}

static struct gpiod_lookup_table palmtx_mci_gpio_table = {
	.dev_id = "pxa2xx-mci.0",
	.table = {
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMTX_SD_DETECT_N,
			    "cd", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMTX_SD_READONLY,
			    "wp", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("gpio-pxa", GPIO_NR_PALMTX_SD_POWER,
			    "power", GPIO_ACTIVE_HIGH),
		{ },
	},
};

static void __init palmtx_init(void)
{
	pxa2xx_mfp_config(ARRAY_AND_SIZE(palmtx_pin_config));
	pxa_set_ffuart_info(NULL);
	pxa_set_btuart_info(NULL);
	pxa_set_stuart_info(NULL);

	palm27x_mmc_init(&palmtx_mci_gpio_table);
	palm27x_pm_init(PALMTX_STR_BASE);
	palm27x_lcd_init(-1, &palm_320x480_lcd_mode);
	palm27x_udc_init(GPIO_NR_PALMTX_USB_DETECT_N,
			GPIO_NR_PALMTX_USB_PULLUP, 1);
	palm27x_irda_init(GPIO_NR_PALMTX_IR_DISABLE);
	palm27x_ac97_init(PALMTX_BAT_MIN_VOLTAGE, PALMTX_BAT_MAX_VOLTAGE,
			GPIO_NR_PALMTX_EARPHONE_DETECT, 95);
	palm27x_pwm_init(GPIO_NR_PALMTX_BL_POWER, GPIO_NR_PALMTX_LCD_POWER);
	palm27x_power_init(GPIO_NR_PALMTX_POWER_DETECT, -1);
	palm27x_pmic_init();
	palmtx_kpc_init();
	palmtx_keys_init();
	palmtx_nor_init();
	palmtx_nand_init();
}

MACHINE_START(PALMTX, "Palm T|X")
	.atag_offset	= 0x100,
	.map_io		= palmtx_map_io,
	.nr_irqs	= PXA_NR_IRQS,
	.init_irq	= pxa27x_init_irq,
	.handle_irq	= pxa27x_handle_irq,
	.init_time	= pxa_timer_init,
	.init_machine	= palmtx_init,
	.restart	= pxa_restart,
MACHINE_END
