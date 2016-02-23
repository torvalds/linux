/*
 *  linux/arch/arm/mach-clps711x/p720t.c
 *
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/sizes.h>
#include <linux/backlight.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand-gpio.h>

#include <mach/hardware.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <video/platform_lcd.h>

#include "common.h"
#include "devices.h"

#define P720T_USERLED		CLPS711X_GPIO(3, 0)
#define P720T_NAND_CLE		CLPS711X_GPIO(4, 0)
#define P720T_NAND_ALE		CLPS711X_GPIO(4, 1)
#define P720T_NAND_NCE		CLPS711X_GPIO(4, 2)

#define P720T_NAND_BASE		(CLPS711X_SDRAM1_BASE)

#define P720T_MMGPIO_BASE	(CLPS711X_NR_GPIO)

#define SYSPLD_PHYS_BASE	IOMEM(CS1_PHYS_BASE)

#define PLD_INT			(SYSPLD_PHYS_BASE + 0x000000)
#define PLD_INT_MMGPIO_BASE	(P720T_MMGPIO_BASE + 0)
#define PLD_INT_PENIRQ		(PLD_INT_MMGPIO_BASE + 5)
#define PLD_INT_UCB_IRQ		(PLD_INT_MMGPIO_BASE + 1)
#define PLD_INT_KBD_ATN		(PLD_INT_MMGPIO_BASE + 0) /* EINT1 */

#define PLD_PWR			(SYSPLD_PHYS_BASE + 0x000004)
#define PLD_PWR_MMGPIO_BASE	(P720T_MMGPIO_BASE + 8)
#define PLD_PWR_EXT		(PLD_PWR_MMGPIO_BASE + 5)
#define PLD_PWR_MODE		(PLD_PWR_MMGPIO_BASE + 4) /* 1 = PWM, 0 = PFM */
#define PLD_S4_ON		(PLD_PWR_MMGPIO_BASE + 3) /* LCD bias voltage enable */
#define PLD_S3_ON		(PLD_PWR_MMGPIO_BASE + 2) /* LCD backlight enable */
#define PLD_S2_ON		(PLD_PWR_MMGPIO_BASE + 1) /* LCD 3V3 supply enable */
#define PLD_S1_ON		(PLD_PWR_MMGPIO_BASE + 0) /* LCD 3V supply enable */

#define PLD_KBD			(SYSPLD_PHYS_BASE + 0x000008)
#define PLD_KBD_MMGPIO_BASE	(P720T_MMGPIO_BASE + 16)
#define PLD_KBD_WAKE		(PLD_KBD_MMGPIO_BASE + 1)
#define PLD_KBD_EN		(PLD_KBD_MMGPIO_BASE + 0)

#define PLD_SPI			(SYSPLD_PHYS_BASE + 0x00000c)
#define PLD_SPI_MMGPIO_BASE	(P720T_MMGPIO_BASE + 24)
#define PLD_SPI_EN		(PLD_SPI_MMGPIO_BASE + 0)

#define PLD_IO			(SYSPLD_PHYS_BASE + 0x000010)
#define PLD_IO_MMGPIO_BASE	(P720T_MMGPIO_BASE + 32)
#define PLD_IO_BOOTSEL		(PLD_IO_MMGPIO_BASE + 6) /* Boot sel switch */
#define PLD_IO_USER		(PLD_IO_MMGPIO_BASE + 5) /* User defined switch */
#define PLD_IO_LED3		(PLD_IO_MMGPIO_BASE + 4)
#define PLD_IO_LED2		(PLD_IO_MMGPIO_BASE + 3)
#define PLD_IO_LED1		(PLD_IO_MMGPIO_BASE + 2)
#define PLD_IO_LED0		(PLD_IO_MMGPIO_BASE + 1)
#define PLD_IO_LEDEN		(PLD_IO_MMGPIO_BASE + 0)

#define PLD_IRDA		(SYSPLD_PHYS_BASE + 0x000014)
#define PLD_IRDA_MMGPIO_BASE	(P720T_MMGPIO_BASE + 40)
#define PLD_IRDA_EN		(PLD_IRDA_MMGPIO_BASE + 0)

#define PLD_COM2		(SYSPLD_PHYS_BASE + 0x000018)
#define PLD_COM2_MMGPIO_BASE	(P720T_MMGPIO_BASE + 48)
#define PLD_COM2_EN		(PLD_COM2_MMGPIO_BASE + 0)

#define PLD_COM1		(SYSPLD_PHYS_BASE + 0x00001c)
#define PLD_COM1_MMGPIO_BASE	(P720T_MMGPIO_BASE + 56)
#define PLD_COM1_EN		(PLD_COM1_MMGPIO_BASE + 0)

#define PLD_AUD			(SYSPLD_PHYS_BASE + 0x000020)
#define PLD_AUD_MMGPIO_BASE	(P720T_MMGPIO_BASE + 64)
#define PLD_AUD_DIV1		(PLD_AUD_MMGPIO_BASE + 6)
#define PLD_AUD_DIV0		(PLD_AUD_MMGPIO_BASE + 5)
#define PLD_AUD_CLK_SEL1	(PLD_AUD_MMGPIO_BASE + 4)
#define PLD_AUD_CLK_SEL0	(PLD_AUD_MMGPIO_BASE + 3)
#define PLD_AUD_MIC_PWR		(PLD_AUD_MMGPIO_BASE + 2)
#define PLD_AUD_MIC_GAIN	(PLD_AUD_MMGPIO_BASE + 1)
#define PLD_AUD_CODEC_EN	(PLD_AUD_MMGPIO_BASE + 0)

#define PLD_CF			(SYSPLD_PHYS_BASE + 0x000024)
#define PLD_CF_MMGPIO_BASE	(P720T_MMGPIO_BASE + 72)
#define PLD_CF2_SLEEP		(PLD_CF_MMGPIO_BASE + 5)
#define PLD_CF1_SLEEP		(PLD_CF_MMGPIO_BASE + 4)
#define PLD_CF2_nPDREQ		(PLD_CF_MMGPIO_BASE + 3)
#define PLD_CF1_nPDREQ		(PLD_CF_MMGPIO_BASE + 2)
#define PLD_CF2_nIRQ		(PLD_CF_MMGPIO_BASE + 1)
#define PLD_CF1_nIRQ		(PLD_CF_MMGPIO_BASE + 0)

#define PLD_SDC			(SYSPLD_PHYS_BASE + 0x000028)
#define PLD_SDC_MMGPIO_BASE	(P720T_MMGPIO_BASE + 80)
#define PLD_SDC_INT_EN		(PLD_SDC_MMGPIO_BASE + 2)
#define PLD_SDC_WP		(PLD_SDC_MMGPIO_BASE + 1)
#define PLD_SDC_CD		(PLD_SDC_MMGPIO_BASE + 0)

#define PLD_CODEC		(SYSPLD_PHYS_BASE + 0x400000)
#define PLD_CODEC_MMGPIO_BASE	(P720T_MMGPIO_BASE + 88)
#define PLD_CODEC_IRQ3		(PLD_CODEC_MMGPIO_BASE + 4)
#define PLD_CODEC_IRQ2		(PLD_CODEC_MMGPIO_BASE + 3)
#define PLD_CODEC_IRQ1		(PLD_CODEC_MMGPIO_BASE + 2)
#define PLD_CODEC_EN		(PLD_CODEC_MMGPIO_BASE + 0)

#define PLD_BRITE		(SYSPLD_PHYS_BASE + 0x400004)
#define PLD_BRITE_MMGPIO_BASE	(P720T_MMGPIO_BASE + 96)
#define PLD_BRITE_UP		(PLD_BRITE_MMGPIO_BASE + 1)
#define PLD_BRITE_DN		(PLD_BRITE_MMGPIO_BASE + 0)

#define PLD_LCDEN		(SYSPLD_PHYS_BASE + 0x400008)
#define PLD_LCDEN_MMGPIO_BASE	(P720T_MMGPIO_BASE + 104)
#define PLD_LCDEN_EN		(PLD_LCDEN_MMGPIO_BASE + 0)

#define PLD_TCH			(SYSPLD_PHYS_BASE + 0x400010)
#define PLD_TCH_MMGPIO_BASE	(P720T_MMGPIO_BASE + 112)
#define PLD_TCH_PENIRQ		(PLD_TCH_MMGPIO_BASE + 1)
#define PLD_TCH_EN		(PLD_TCH_MMGPIO_BASE + 0)

#define PLD_GPIO		(SYSPLD_PHYS_BASE + 0x400014)
#define PLD_GPIO_MMGPIO_BASE	(P720T_MMGPIO_BASE + 120)
#define PLD_GPIO2		(PLD_GPIO_MMGPIO_BASE + 2)
#define PLD_GPIO1		(PLD_GPIO_MMGPIO_BASE + 1)
#define PLD_GPIO0		(PLD_GPIO_MMGPIO_BASE + 0)

static struct gpio p720t_gpios[] __initconst = {
	{ PLD_S1_ON,	GPIOF_OUT_INIT_LOW,	"PLD_S1_ON" },
	{ PLD_S2_ON,	GPIOF_OUT_INIT_LOW,	"PLD_S2_ON" },
	{ PLD_S3_ON,	GPIOF_OUT_INIT_LOW,	"PLD_S3_ON" },
	{ PLD_S4_ON,	GPIOF_OUT_INIT_LOW,	"PLD_S4_ON" },
	{ PLD_KBD_EN,	GPIOF_OUT_INIT_LOW,	"PLD_KBD_EN" },
	{ PLD_SPI_EN,	GPIOF_OUT_INIT_LOW,	"PLD_SPI_EN" },
	{ PLD_IO_USER,	GPIOF_OUT_INIT_LOW,	"PLD_IO_USER" },
	{ PLD_IO_LED0,	GPIOF_OUT_INIT_LOW,	"PLD_IO_LED0" },
	{ PLD_IO_LED1,	GPIOF_OUT_INIT_LOW,	"PLD_IO_LED1" },
	{ PLD_IO_LED2,	GPIOF_OUT_INIT_LOW,	"PLD_IO_LED2" },
	{ PLD_IO_LED3,	GPIOF_OUT_INIT_LOW,	"PLD_IO_LED3" },
	{ PLD_IO_LEDEN,	GPIOF_OUT_INIT_LOW,	"PLD_IO_LEDEN" },
	{ PLD_IRDA_EN,	GPIOF_OUT_INIT_LOW,	"PLD_IRDA_EN" },
	{ PLD_COM1_EN,	GPIOF_OUT_INIT_HIGH,	"PLD_COM1_EN" },
	{ PLD_COM2_EN,	GPIOF_OUT_INIT_HIGH,	"PLD_COM2_EN" },
	{ PLD_CODEC_EN,	GPIOF_OUT_INIT_LOW,	"PLD_CODEC_EN" },
	{ PLD_LCDEN_EN,	GPIOF_OUT_INIT_LOW,	"PLD_LCDEN_EN" },
	{ PLD_TCH_EN,	GPIOF_OUT_INIT_LOW,	"PLD_TCH_EN" },
	{ P720T_USERLED,GPIOF_OUT_INIT_LOW,	"USER_LED" },
};

static struct resource p720t_mmgpio_resource[] __initdata = {
	DEFINE_RES_MEM_NAMED(0, 4, "dat"),
};

static struct bgpio_pdata p720t_mmgpio_pdata = {
	.ngpio	= 8,
};

static struct platform_device p720t_mmgpio __initdata = {
	.name		= "basic-mmio-gpio",
	.id		= -1,
	.resource	= p720t_mmgpio_resource,
	.num_resources	= ARRAY_SIZE(p720t_mmgpio_resource),
	.dev		= {
		.platform_data	= &p720t_mmgpio_pdata,
	},
};

static void __init p720t_mmgpio_init(void __iomem *addrbase, int gpiobase)
{
	p720t_mmgpio_resource[0].start = (unsigned long)addrbase;
	p720t_mmgpio_pdata.base = gpiobase;

	platform_device_register(&p720t_mmgpio);
}

static struct {
	void __iomem	*addrbase;
	int		gpiobase;
} mmgpios[] __initconst = {
	{ PLD_INT,	PLD_INT_MMGPIO_BASE },
	{ PLD_PWR,	PLD_PWR_MMGPIO_BASE },
	{ PLD_KBD,	PLD_KBD_MMGPIO_BASE },
	{ PLD_SPI,	PLD_SPI_MMGPIO_BASE },
	{ PLD_IO,	PLD_IO_MMGPIO_BASE },
	{ PLD_IRDA,	PLD_IRDA_MMGPIO_BASE },
	{ PLD_COM2,	PLD_COM2_MMGPIO_BASE },
	{ PLD_COM1,	PLD_COM1_MMGPIO_BASE },
	{ PLD_AUD,	PLD_AUD_MMGPIO_BASE },
	{ PLD_CF,	PLD_CF_MMGPIO_BASE },
	{ PLD_SDC,	PLD_SDC_MMGPIO_BASE },
	{ PLD_CODEC,	PLD_CODEC_MMGPIO_BASE },
	{ PLD_BRITE,	PLD_BRITE_MMGPIO_BASE },
	{ PLD_LCDEN,	PLD_LCDEN_MMGPIO_BASE },
	{ PLD_TCH,	PLD_TCH_MMGPIO_BASE },
	{ PLD_GPIO,	PLD_GPIO_MMGPIO_BASE },
};

static struct resource p720t_nand_resource[] __initdata = {
	DEFINE_RES_MEM(P720T_NAND_BASE, SZ_4),
};

static struct mtd_partition p720t_nand_parts[] __initdata = {
	{
		.name	= "Flash partition 1",
		.offset	= 0,
		.size	= SZ_2M,
	},
	{
		.name	= "Flash partition 2",
		.offset	= MTDPART_OFS_APPEND,
		.size	= MTDPART_SIZ_FULL,
	},
};

static struct gpio_nand_platdata p720t_nand_pdata __initdata = {
	.gpio_rdy	= -1,
	.gpio_nce	= P720T_NAND_NCE,
	.gpio_ale	= P720T_NAND_ALE,
	.gpio_cle	= P720T_NAND_CLE,
	.gpio_nwp	= -1,
	.chip_delay	= 15,
	.parts		= p720t_nand_parts,
	.num_parts	= ARRAY_SIZE(p720t_nand_parts),
};

static struct platform_device p720t_nand_pdev __initdata = {
	.name		= "gpio-nand",
	.id		= -1,
	.resource	= p720t_nand_resource,
	.num_resources	= ARRAY_SIZE(p720t_nand_resource),
	.dev		= {
		.platform_data = &p720t_nand_pdata,
	},
};

static void p720t_lcd_power_set(struct plat_lcd_data *pd, unsigned int power)
{
	if (power) {
		gpio_set_value(PLD_LCDEN_EN, 1);
		gpio_set_value(PLD_S1_ON, 1);
		gpio_set_value(PLD_S2_ON, 1);
		gpio_set_value(PLD_S4_ON, 1);
	} else {
		gpio_set_value(PLD_S1_ON, 0);
		gpio_set_value(PLD_S2_ON, 0);
		gpio_set_value(PLD_S4_ON, 0);
		gpio_set_value(PLD_LCDEN_EN, 0);
	}
}

static struct plat_lcd_data p720t_lcd_power_pdata = {
	.set_power	= p720t_lcd_power_set,
};

static void p720t_lcd_backlight_set_intensity(int intensity)
{
	gpio_set_value(PLD_S3_ON, intensity);
}

static struct generic_bl_info p720t_lcd_backlight_pdata = {
	.name			= "lcd-backlight.0",
	.default_intensity	= 0x01,
	.max_intensity		= 0x01,
	.set_bl_intensity	= p720t_lcd_backlight_set_intensity,
};

static void __init
fixup_p720t(struct tag *tag, char **cmdline)
{
	/*
	 * Our bootloader doesn't setup any tags (yet).
	 */
	if (tag->hdr.tag != ATAG_CORE) {
		tag->hdr.tag = ATAG_CORE;
		tag->hdr.size = tag_size(tag_core);
		tag->u.core.flags = 0;
		tag->u.core.pagesize = PAGE_SIZE;
		tag->u.core.rootdev = 0x0100;

		tag = tag_next(tag);
		tag->hdr.tag = ATAG_MEM;
		tag->hdr.size = tag_size(tag_mem32);
		tag->u.mem.size = 4096;
		tag->u.mem.start = PHYS_OFFSET;

		tag = tag_next(tag);
		tag->hdr.tag = ATAG_NONE;
		tag->hdr.size = 0;
	}
}

static struct gpio_led p720t_gpio_leds[] = {
	{
		.name			= "User LED",
		.default_trigger	= "heartbeat",
		.gpio			= P720T_USERLED,
	},
};

static struct gpio_led_platform_data p720t_gpio_led_pdata __initdata = {
	.leds		= p720t_gpio_leds,
	.num_leds	= ARRAY_SIZE(p720t_gpio_leds),
};

static void __init p720t_init(void)
{
	int i;

	clps711x_devices_init();

	for (i = 0; i < ARRAY_SIZE(mmgpios); i++)
		p720t_mmgpio_init(mmgpios[i].addrbase, mmgpios[i].gpiobase);

	platform_device_register(&p720t_nand_pdev);
}

static void __init p720t_init_late(void)
{
	WARN_ON(gpio_request_array(p720t_gpios, ARRAY_SIZE(p720t_gpios)));

	platform_device_register_data(NULL, "platform-lcd", 0,
				      &p720t_lcd_power_pdata,
				      sizeof(p720t_lcd_power_pdata));
	platform_device_register_data(NULL, "generic-bl", 0,
				      &p720t_lcd_backlight_pdata,
				      sizeof(p720t_lcd_backlight_pdata));
	platform_device_register_simple("video-clps711x", 0, NULL, 0);
	platform_device_register_data(NULL, "leds-gpio", 0,
				      &p720t_gpio_led_pdata,
				      sizeof(p720t_gpio_led_pdata));
}

MACHINE_START(P720T, "ARM-Prospector720T")
	/* Maintainer: ARM Ltd/Deep Blue Solutions Ltd */
	.atag_offset	= 0x100,
	.fixup		= fixup_p720t,
	.map_io		= clps711x_map_io,
	.init_irq	= clps711x_init_irq,
	.init_time	= clps711x_timer_init,
	.init_machine	= p720t_init,
	.init_late	= p720t_init_late,
	.restart	= clps711x_restart,
MACHINE_END
