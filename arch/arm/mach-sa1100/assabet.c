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
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/serial_core.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/delay.h>
#include <linux/mm.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/irda.h>
#include <asm/mach/map.h>
#include <asm/mach/serial_sa1100.h>
#include <asm/arch/assabet.h>
#include <asm/arch/mcp.h>

#include "generic.h"

#define ASSABET_BCR_DB1110 \
	(ASSABET_BCR_SPK_OFF    | ASSABET_BCR_QMUTE     | \
	 ASSABET_BCR_LED_GREEN  | ASSABET_BCR_LED_RED   | \
	 ASSABET_BCR_RS232EN    | ASSABET_BCR_LCD_12RGB | \
	 ASSABET_BCR_IRDA_MD0)

#define ASSABET_BCR_DB1111 \
	(ASSABET_BCR_SPK_OFF    | ASSABET_BCR_QMUTE     | \
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

static void assabet_backlight_power(int on)
{
#ifndef ASSABET_PAL_VIDEO
	if (on)
		ASSABET_BCR_set(ASSABET_BCR_LIGHT_ON);
	else
#endif
		ASSABET_BCR_clear(ASSABET_BCR_LIGHT_ON);
}

/*
 * Turn on/off the backlight.  When turning the backlight on,
 * we wait 500us after turning it on so we don't cause the
 * supplies to droop when we enable the LCD controller (and
 * cause a hard reset.)
 */
static void assabet_lcd_power(int on)
{
#ifndef ASSABET_PAL_VIDEO
	if (on) {
		ASSABET_BCR_set(ASSABET_BCR_LCD_ON);
		udelay(500);
	} else
#endif
		ASSABET_BCR_clear(ASSABET_BCR_LCD_ON);
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
	{
		.start	= SA1100_CS0_PHYS,
		.end	= SA1100_CS0_PHYS + SZ_32M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= SA1100_CS1_PHYS,
		.end	= SA1100_CS1_PHYS + SZ_32M - 1,
		.flags	= IORESOURCE_MEM,
	}
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

static struct mcp_plat_data assabet_mcp_data = {
	.mccr0		= MCCR0_ADM,
	.sclk_rate	= 11981000,
};

static void __init assabet_init(void)
{
	/*
	 * Ensure that the power supply is in "high power" mode.
	 */
	GPDR |= GPIO_GPIO16;
	GPSR = GPIO_GPIO16;

	/*
	 * Ensure that these pins are set as outputs and are driving
	 * logic 0.  This ensures that we won't inadvertently toggle
	 * the WS latch in the CPLD, and we don't float causing
	 * excessive power drain.  --rmk
	 */
	GPDR |= GPIO_SSP_TXD | GPIO_SSP_SCLK | GPIO_SSP_SFRM;
	GPCR = GPIO_SSP_TXD | GPIO_SSP_SCLK | GPIO_SSP_SFRM;

	/*
	 * Set up registers for sleep mode.
	 */
	PWER = PWER_GPIO0;
	PGSR = 0;
	PCFR = 0;
	PSDR = 0;
	PPDR |= PPC_TXD3 | PPC_TXD1;
	PPSR |= PPC_TXD3 | PPC_TXD1;

	sa1100fb_lcd_power = assabet_lcd_power;
	sa1100fb_backlight_power = assabet_backlight_power;

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
#endif
	}

	sa11x0_set_flash_data(&assabet_flash_data, assabet_flash_resources,
			      ARRAY_SIZE(assabet_flash_resources));
	sa11x0_set_irda_data(&assabet_irda_data);
	sa11x0_set_mcp_data(&assabet_mcp_data);
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
	unsigned long virt = io_p2v(phys);
	int prot = PMD_TYPE_SECT | PMD_SECT_AP_WRITE | PMD_DOMAIN(DOMAIN_IO);
	pmd_t *pmd;

	pmd = pmd_offset(pgd_offset_k(virt), virt);
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
	unsigned long scr, i;

	GPDR |= 0x3fc;			/* Configure GPIO 9:2 as outputs */
	GPSR = 0x3fc;			/* Write 0xFF to GPIO 9:2 */
	GPDR &= ~(0x3fc);		/* Configure GPIO 9:2 as inputs */
	for(i = 100; i--; scr = GPLR);	/* Read GPIO 9:2 */
	GPDR |= 0x3fc;			/*  restore correct pin direction */
	scr &= 0x3fc;			/* save as system configuration byte. */
	SCR_value = scr;
}

static void __init
fixup_assabet(struct machine_desc *desc, struct tag *tags,
	      char **cmdline, struct meminfo *mi)
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
 /* virtual     physical    length      type */
  { 0xf1000000, 0x12000000, 0x00100000, MT_DEVICE }, /* Board Control Register */
  { 0xf2800000, 0x4b800000, 0x00800000, MT_DEVICE }  /* MQ200 */
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

	if (machine_has_neponset()) {
#ifdef CONFIG_ASSABET_NEPONSET
		extern void neponset_map_io(void);

		/*
		 * We map Neponset registers even if it isn't present since
		 * many drivers will try to probe their stuff (and fail).
		 * This is still more friendly than a kernel paging request
		 * crash.
		 */
		neponset_map_io();
#endif
	} else {
		sa1100_register_uart_fns(&assabet_port_fns);
	}

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


MACHINE_START(ASSABET, "Intel-Assabet")
	.phys_ram	= 0xc0000000,
	.phys_io	= 0x80000000,
	.io_pg_offst	= ((0xf8000000) >> 18) & 0xfffc,
	.boot_params	= 0xc0000100,
	.fixup		= fixup_assabet,
	.map_io		= assabet_map_io,
	.init_irq	= sa1100_init_irq,
	.timer		= &sa1100_timer,
	.init_machine	= assabet_init,
MACHINE_END
