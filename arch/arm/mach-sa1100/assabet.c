/*
 * linux/arch/arm/mach-sa1100/assabet.c
 *
 * Author: Nicolas Pitre
 *
 * This file contains all Assabet-specific tweaks.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/serial_core.h>
#include <linux/mfd/ucb1x00.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/leds.h>
#include <linux/slab.h>

#include <video/sa1100fb.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/irda.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>
#include <mach/assabet.h>
#include <linux/platform_data/mfd-mcp-sa11x0.h>
#include <mach/irqs.h>

#include "generic.h"

#define ASSABET_BCR_DB1110 \
	(ASSABET_BCR_SPK_OFF    | \
	 ASSABET_BCR_LED_GREEN  | ASSABET_BCR_LED_RED   | \
	 ASSABET_BCR_RS232EN    | ASSABET_BCR_LCD_12RGB | \
	 ASSABET_BCR_IRDA_MD0)

#define ASSABET_BCR_DB1111 \
	(ASSABET_BCR_SPK_OFF    | \
	 ASSABET_BCR_LED_GREEN  | ASSABET_BCR_LED_RED   | \
	 ASSABET_BCR_RS232EN    | ASSABET_BCR_LCD_12RGB | \
	 ASSABET_BCR_CF_BUS_OFF | ASSABET_BCR_STEREO_LB | \
	 ASSABET_BCR_IRDA_MD0   | ASSABET_BCR_CF_RST)

unsigned long SCR_value = ASSABET_SCR_INIT;
EXPORT_SYMBOL(SCR_value);

static unsigned long BCR_value = ASSABET_BCR_DB1110;

void ASSABET_BCR_frob(unsigned int mask, unsigned int val)
{
	unsigned long flags;

	local_irq_save(flags);
	BCR_value = (BCR_value & ~mask) | val;
	ASSABET_BCR = BCR_value;
	local_irq_restore(flags);
}

EXPORT_SYMBOL(ASSABET_BCR_frob);

static void assabet_ucb1x00_reset(enum ucb1x00_reset state)
{
	if (state == UCB_RST_PROBE)
		ASSABET_BCR_set(ASSABET_BCR_CODEC_RST);
}


/*
 * Assabet flash support code.
 */

#ifdef ASSABET_REV_4
/*
 * Phase 4 Assabet has two 28F160B3 flash parts in bank 0:
 */
static struct mtd_partition assabet_partitions[] = {
	{
		.name		= "bootloader",
		.size		= 0x00020000,
		.offset		= 0,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "bootloader params",
		.size		= 0x00020000,
		.offset		= MTDPART_OFS_APPEND,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "jffs",
		.size		= MTDPART_SIZ_FULL,
		.offset		= MTDPART_OFS_APPEND,
	}
};
#else
/*
 * Phase 5 Assabet has two 28F128J3A flash parts in bank 0:
 */
static struct mtd_partition assabet_partitions[] = {
	{
		.name		= "bootloader",
		.size		= 0x00040000,
		.offset		= 0,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "bootloader params",
		.size		= 0x00040000,
		.offset		= MTDPART_OFS_APPEND,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "jffs",
		.size		= MTDPART_SIZ_FULL,
		.offset		= MTDPART_OFS_APPEND,
	}
};
#endif

static struct flash_platform_data assabet_flash_data = {
	.map_name	= "cfi_probe",
	.parts		= assabet_partitions,
	.nr_parts	= ARRAY_SIZE(assabet_partitions),
};

static struct resource assabet_flash_resources[] = {
	DEFINE_RES_MEM(SA1100_CS0_PHYS, SZ_32M),
	DEFINE_RES_MEM(SA1100_CS1_PHYS, SZ_32M),
};


/*
 * Assabet IrDA support code.
 */

static int assabet_irda_set_power(struct device *dev, unsigned int state)
{
	static unsigned int bcr_state[4] = {
		ASSABET_BCR_IRDA_MD0,
		ASSABET_BCR_IRDA_MD1|ASSABET_BCR_IRDA_MD0,
		ASSABET_BCR_IRDA_MD1,
		0
	};

	if (state < 4) {
		state = bcr_state[state];
		ASSABET_BCR_clear(state ^ (ASSABET_BCR_IRDA_MD1|
					   ASSABET_BCR_IRDA_MD0));
		ASSABET_BCR_set(state);
	}
	return 0;
}

static void assabet_irda_set_speed(struct device *dev, unsigned int speed)
{
	if (speed < 4000000)
		ASSABET_BCR_clear(ASSABET_BCR_IRDA_FSEL);
	else
		ASSABET_BCR_set(ASSABET_BCR_IRDA_FSEL);
}

static struct irda_platform_data assabet_irda_data = {
	.set_power	= assabet_irda_set_power,
	.set_speed	= assabet_irda_set_speed,
};

static struct ucb1x00_plat_data assabet_ucb1x00_data = {
	.reset		= assabet_ucb1x00_reset,
	.gpio_base	= -1,
};

static struct mcp_plat_data assabet_mcp_data = {
	.mccr0		= MCCR0_ADM,
	.sclk_rate	= 11981000,
	.codec_pdata	= &assabet_ucb1x00_data,
};

static void assabet_lcd_set_visual(u32 visual)
{
	u_int is_true_color = visual == FB_VISUAL_TRUECOLOR;

	if (machine_is_assabet()) {
#if 1		// phase 4 or newer Assabet's
		if (is_true_color)
			ASSABET_BCR_set(ASSABET_BCR_LCD_12RGB);
		else
			ASSABET_BCR_clear(ASSABET_BCR_LCD_12RGB);
#else
		// older Assabet's
		if (is_true_color)
			ASSABET_BCR_clear(ASSABET_BCR_LCD_12RGB);
		else
			ASSABET_BCR_set(ASSABET_BCR_LCD_12RGB);
#endif
	}
}

#ifndef ASSABET_PAL_VIDEO
static void assabet_lcd_backlight_power(int on)
{
	if (on)
		ASSABET_BCR_set(ASSABET_BCR_LIGHT_ON);
	else
		ASSABET_BCR_clear(ASSABET_BCR_LIGHT_ON);
}

/*
 * Turn on/off the backlight.  When turning the backlight on, we wait
 * 500us after turning it on so we don't cause the supplies to droop
 * when we enable the LCD controller (and cause a hard reset.)
 */
static void assabet_lcd_power(int on)
{
	if (on) {
		ASSABET_BCR_set(ASSABET_BCR_LCD_ON);
		udelay(500);
	} else
		ASSABET_BCR_clear(ASSABET_BCR_LCD_ON);
}

/*
 * The assabet uses a sharp LQ039Q2DS54 LCD module.  It is actually
 * takes an RGB666 signal, but we provide it with an RGB565 signal
 * instead (def_rgb_16).
 */
static struct sa1100fb_mach_info lq039q2ds54_info = {
	.pixclock	= 171521,	.bpp		= 16,
	.xres		= 320,		.yres		= 240,

	.hsync_len	= 5,		.vsync_len	= 1,
	.left_margin	= 61,		.upper_margin	= 3,
	.right_margin	= 9,		.lower_margin	= 0,

	.sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,

	.lccr0		= LCCR0_Color | LCCR0_Sngl | LCCR0_Act,
	.lccr3		= LCCR3_OutEnH | LCCR3_PixRsEdg | LCCR3_ACBsDiv(2),

	.backlight_power = assabet_lcd_backlight_power,
	.lcd_power = assabet_lcd_power,
	.set_visual = assabet_lcd_set_visual,
};
#else
static void assabet_pal_backlight_power(int on)
{
	ASSABET_BCR_clear(ASSABET_BCR_LIGHT_ON);
}

static void assabet_pal_power(int on)
{
	ASSABET_BCR_clear(ASSABET_BCR_LCD_ON);
}

static struct sa1100fb_mach_info pal_info = {
	.pixclock	= 67797,	.bpp		= 16,
	.xres		= 640,		.yres		= 512,

	.hsync_len	= 64,		.vsync_len	= 6,
	.left_margin	= 125,		.upper_margin	= 70,
	.right_margin	= 115,		.lower_margin	= 36,

	.lccr0		= LCCR0_Color | LCCR0_Sngl | LCCR0_Act,
	.lccr3		= LCCR3_OutEnH | LCCR3_PixRsEdg | LCCR3_ACBsDiv(512),

	.backlight_power = assabet_pal_backlight_power,
	.lcd_power = assabet_pal_power,
	.set_visual = assabet_lcd_set_visual,
};
#endif

#ifdef CONFIG_ASSABET_NEPONSET
static struct resource neponset_resources[] = {
	DEFINE_RES_MEM(0x10000000, 0x08000000),
	DEFINE_RES_MEM(0x18000000, 0x04000000),
	DEFINE_RES_MEM(0x40000000, SZ_8K),
	DEFINE_RES_IRQ(IRQ_GPIO25),
};
#endif

static void __init assabet_init(void)
{
	/*
	 * Ensure that the power supply is in "high power" mode.
	 */
	GPSR = GPIO_GPIO16;
	GPDR |= GPIO_GPIO16;

	/*
	 * Ensure that these pins are set as outputs and are driving
	 * logic 0.  This ensures that we won't inadvertently toggle
	 * the WS latch in the CPLD, and we don't float causing
	 * excessive power drain.  --rmk
	 */
	GPCR = GPIO_SSP_TXD | GPIO_SSP_SCLK | GPIO_SSP_SFRM;
	GPDR |= GPIO_SSP_TXD | GPIO_SSP_SCLK | GPIO_SSP_SFRM;

	/*
	 * Also set GPIO27 as an output; this is used to clock UART3
	 * via the FPGA and as otherwise has no pullups or pulldowns,
	 * so stop it floating.
	 */
	GPCR = GPIO_GPIO27;
	GPDR |= GPIO_GPIO27;

	/*
	 * Set up registers for sleep mode.
	 */
	PWER = PWER_GPIO0;
	PGSR = 0;
	PCFR = 0;
	PSDR = 0;
	PPDR |= PPC_TXD3 | PPC_TXD1;
	PPSR |= PPC_TXD3 | PPC_TXD1;

	sa11x0_ppc_configure_mcp();

	if (machine_has_neponset()) {
		/*
		 * Angel sets this, but other bootloaders may not.
		 *
		 * This must precede any driver calls to BCR_set()
		 * or BCR_clear().
		 */
		ASSABET_BCR = BCR_value = ASSABET_BCR_DB1111;

#ifndef CONFIG_ASSABET_NEPONSET
		printk( "Warning: Neponset detected but full support "
			"hasn't been configured in the kernel\n" );
#else
		platform_device_register_simple("neponset", 0,
			neponset_resources, ARRAY_SIZE(neponset_resources));
#endif
	}

#ifndef ASSABET_PAL_VIDEO
	sa11x0_register_lcd(&lq039q2ds54_info);
#else
	sa11x0_register_lcd(&pal_video);
#endif
	sa11x0_register_mtd(&assabet_flash_data, assabet_flash_resources,
			    ARRAY_SIZE(assabet_flash_resources));
	sa11x0_register_irda(&assabet_irda_data);
	sa11x0_register_mcp(&assabet_mcp_data);
}

/*
 * On Assabet, we must probe for the Neponset board _before_
 * paging_init() has occurred to actually determine the amount
 * of RAM available.  To do so, we map the appropriate IO section
 * in the page table here in order to access GPIO registers.
 */
static void __init map_sa1100_gpio_regs( void )
{
	unsigned long phys = __PREG(GPLR) & PMD_MASK;
	unsigned long virt = (unsigned long)io_p2v(phys);
	int prot = PMD_TYPE_SECT | PMD_SECT_AP_WRITE | PMD_DOMAIN(DOMAIN_IO);
	pmd_t *pmd;

	pmd = pmd_offset(pud_offset(pgd_offset_k(virt), virt), virt);
	*pmd = __pmd(phys | prot);
	flush_pmd_entry(pmd);
}

/*
 * Read System Configuration "Register"
 * (taken from "Intel StrongARM SA-1110 Microprocessor Development Board
 * User's Guide", section 4.4.1)
 *
 * This same scan is performed in arch/arm/boot/compressed/head-sa1100.S
 * to set up the serial port for decompression status messages. We
 * repeat it here because the kernel may not be loaded as a zImage, and
 * also because it's a hassle to communicate the SCR value to the kernel
 * from the decompressor.
 *
 * Note that IRQs are guaranteed to be disabled.
 */
static void __init get_assabet_scr(void)
{
	unsigned long uninitialized_var(scr), i;

	GPDR |= 0x3fc;			/* Configure GPIO 9:2 as outputs */
	GPSR = 0x3fc;			/* Write 0xFF to GPIO 9:2 */
	GPDR &= ~(0x3fc);		/* Configure GPIO 9:2 as inputs */
	for(i = 100; i--; )		/* Read GPIO 9:2 */
		scr = GPLR;
	GPDR |= 0x3fc;			/*  restore correct pin direction */
	scr &= 0x3fc;			/* save as system configuration byte. */
	SCR_value = scr;
}

static void __init
fixup_assabet(struct tag *tags, char **cmdline, struct meminfo *mi)
{
	/* This must be done before any call to machine_has_neponset() */
	map_sa1100_gpio_regs();
	get_assabet_scr();

	if (machine_has_neponset())
		printk("Neponset expansion board detected\n");
}


static void assabet_uart_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	if (port->mapbase == _Ser1UTCR0) {
		if (state)
			ASSABET_BCR_clear(ASSABET_BCR_RS232EN |
					  ASSABET_BCR_COM_RTS |
					  ASSABET_BCR_COM_DTR);
		else
			ASSABET_BCR_set(ASSABET_BCR_RS232EN |
					ASSABET_BCR_COM_RTS |
					ASSABET_BCR_COM_DTR);
	}
}

/*
 * Assabet uses COM_RTS and COM_DTR for both UART1 (com port)
 * and UART3 (radio module).  We only handle them for UART1 here.
 */
static void assabet_set_mctrl(struct uart_port *port, u_int mctrl)
{
	if (port->mapbase == _Ser1UTCR0) {
		u_int set = 0, clear = 0;

		if (mctrl & TIOCM_RTS)
			clear |= ASSABET_BCR_COM_RTS;
		else
			set |= ASSABET_BCR_COM_RTS;

		if (mctrl & TIOCM_DTR)
			clear |= ASSABET_BCR_COM_DTR;
		else
			set |= ASSABET_BCR_COM_DTR;

		ASSABET_BCR_clear(clear);
		ASSABET_BCR_set(set);
	}
}

static u_int assabet_get_mctrl(struct uart_port *port)
{
	u_int ret = 0;
	u_int bsr = ASSABET_BSR;

	/* need 2 reads to read current value */
	bsr = ASSABET_BSR;

	if (port->mapbase == _Ser1UTCR0) {
		if (bsr & ASSABET_BSR_COM_DCD)
			ret |= TIOCM_CD;
		if (bsr & ASSABET_BSR_COM_CTS)
			ret |= TIOCM_CTS;
		if (bsr & ASSABET_BSR_COM_DSR)
			ret |= TIOCM_DSR;
	} else if (port->mapbase == _Ser3UTCR0) {
		if (bsr & ASSABET_BSR_RAD_DCD)
			ret |= TIOCM_CD;
		if (bsr & ASSABET_BSR_RAD_CTS)
			ret |= TIOCM_CTS;
		if (bsr & ASSABET_BSR_RAD_DSR)
			ret |= TIOCM_DSR;
		if (bsr & ASSABET_BSR_RAD_RI)
			ret |= TIOCM_RI;
	} else {
		ret = TIOCM_CD | TIOCM_CTS | TIOCM_DSR;
	}

	return ret;
}

static struct sa1100_port_fns assabet_port_fns __initdata = {
	.set_mctrl	= assabet_set_mctrl,
	.get_mctrl	= assabet_get_mctrl,
	.pm		= assabet_uart_pm,
};

static struct map_desc assabet_io_desc[] __initdata = {
  	{	/* Board Control Register */
		.virtual	=  0xf1000000,
		.pfn		= __phys_to_pfn(0x12000000),
		.length		= 0x00100000,
		.type		= MT_DEVICE
	}, {	/* MQ200 */
		.virtual	=  0xf2800000,
		.pfn		= __phys_to_pfn(0x4b800000),
		.length		= 0x00800000,
		.type		= MT_DEVICE
	}
};

static void __init assabet_map_io(void)
{
	sa1100_map_io();
	iotable_init(assabet_io_desc, ARRAY_SIZE(assabet_io_desc));

	/*
	 * Set SUS bit in SDCR0 so serial port 1 functions.
	 * Its called GPCLKR0 in my SA1110 manual.
	 */
	Ser1SDCR0 |= SDCR0_SUS;

	if (!machine_has_neponset())
		sa1100_register_uart_fns(&assabet_port_fns);

	/*
	 * When Neponset is attached, the first UART should be
	 * UART3.  That's what Angel is doing and many documents
	 * are stating this.
	 *
	 * We do the Neponset mapping even if Neponset support
	 * isn't compiled in so the user will still get something on
	 * the expected physical serial port.
	 *
	 * We no longer do this; not all boot loaders support it,
	 * and UART3 appears to be somewhat unreliable with blob.
	 */
	sa1100_register_uart(0, 1);
	sa1100_register_uart(2, 3);
}

/* LEDs */
#if defined(CONFIG_NEW_LEDS) && defined(CONFIG_LEDS_CLASS)
struct assabet_led {
	struct led_classdev cdev;
	u32 mask;
};

/*
 * The triggers lines up below will only be used if the
 * LED triggers are compiled in.
 */
static const struct {
	const char *name;
	const char *trigger;
} assabet_leds[] = {
	{ "assabet:red", "cpu0",},
	{ "assabet:green", "heartbeat", },
};

/*
 * The LED control in Assabet is reversed:
 *  - setting bit means turn off LED
 *  - clearing bit means turn on LED
 */
static void assabet_led_set(struct led_classdev *cdev,
		enum led_brightness b)
{
	struct assabet_led *led = container_of(cdev,
			struct assabet_led, cdev);

	if (b != LED_OFF)
		ASSABET_BCR_clear(led->mask);
	else
		ASSABET_BCR_set(led->mask);
}

static enum led_brightness assabet_led_get(struct led_classdev *cdev)
{
	struct assabet_led *led = container_of(cdev,
			struct assabet_led, cdev);

	return (ASSABET_BCR & led->mask) ? LED_OFF : LED_FULL;
}

static int __init assabet_leds_init(void)
{
	int i;

	if (!machine_is_assabet())
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(assabet_leds); i++) {
		struct assabet_led *led;

		led = kzalloc(sizeof(*led), GFP_KERNEL);
		if (!led)
			break;

		led->cdev.name = assabet_leds[i].name;
		led->cdev.brightness_set = assabet_led_set;
		led->cdev.brightness_get = assabet_led_get;
		led->cdev.default_trigger = assabet_leds[i].trigger;

		if (!i)
			led->mask = ASSABET_BCR_LED_RED;
		else
			led->mask = ASSABET_BCR_LED_GREEN;

		if (led_classdev_register(NULL, &led->cdev) < 0) {
			kfree(led);
			break;
		}
	}

	return 0;
}

/*
 * Since we may have triggers on any subsystem, defer registration
 * until after subsystem_init.
 */
fs_initcall(assabet_leds_init);
#endif

MACHINE_START(ASSABET, "Intel-Assabet")
	.atag_offset	= 0x100,
	.fixup		= fixup_assabet,
	.map_io		= assabet_map_io,
	.nr_irqs	= SA1100_NR_IRQS,
	.init_irq	= sa1100_init_irq,
	.timer		= &sa1100_timer,
	.init_machine	= assabet_init,
	.init_late	= sa11x0_init_late,
#ifdef CONFIG_SA1111
	.dma_zone_size	= SZ_1M,
#endif
	.restart	= sa11x0_restart,
MACHINE_END
