// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/mach-sa1100/assabet.c
 *
 * Author: Nicolas Pitre
 *
 * This file contains all Assabet-specific tweaks.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio/gpio-reg.h>
#include <linux/gpio/machine.h>
#include <linux/gpio_keys.h>
#include <linux/ioport.h>
#include <linux/platform_data/sa11x0-serial.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
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
#include <asm/tlbflush.h>

#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/map.h>
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

static struct gpio_chip *assabet_bcr_gc;

static const char *assabet_names[] = {
	"cf_pwr", "cf_gfx_reset", "nsoft_reset", "irda_fsel",
	"irda_md0", "irda_md1", "stereo_loopback", "ncf_bus_on",
	"audio_pwr_on", "light_pwr_on", "lcd16data", "lcd_pwr_on",
	"rs232_on", "nred_led", "ngreen_led", "vib_on",
	"com_dtr", "com_rts", "radio_wake_mod", "i2c_enab",
	"tvir_enab", "qmute", "radio_pwr_on", "spkr_off",
	"rs232_valid", "com_dcd", "com_cts", "com_dsr",
	"radio_cts", "radio_dsr", "radio_dcd", "radio_ri",
};

/* The old deprecated interface */
void ASSABET_BCR_frob(unsigned int mask, unsigned int val)
{
	unsigned long m = mask, v = val;

	assabet_bcr_gc->set_multiple(assabet_bcr_gc, &m, &v);
}
EXPORT_SYMBOL(ASSABET_BCR_frob);

static void __init assabet_init_gpio(void __iomem *reg, u32 def_val)
{
	struct gpio_chip *gc;

	writel_relaxed(def_val, reg);

	gc = gpio_reg_init(NULL, reg, -1, 32, "assabet", 0xff000000, def_val,
			   assabet_names, NULL, NULL);

	if (IS_ERR(gc))
		return;

	assabet_bcr_gc = gc;
}

/*
 * The codec reset goes to three devices, so we need to release
 * the rest when any one of these requests it.  However, that
 * causes the ADV7171 to consume around 100mA - more than half
 * the LCD-blanked power.
 *
 * With the ADV7171, LCD and backlight enabled, we go over
 * budget on the MAX846 Li-Ion charger, and if no Li-Ion battery
 * is connected, the Assabet crashes.
 */
#define RST_UCB1X00 (1 << 0)
#define RST_UDA1341 (1 << 1)
#define RST_ADV7171 (1 << 2)

#define SDA GPIO_GPIO(15)
#define SCK GPIO_GPIO(18)
#define MOD GPIO_GPIO(17)

static void adv7171_start(void)
{
	GPSR = SCK;
	udelay(1);
	GPSR = SDA;
	udelay(2);
	GPCR = SDA;
}

static void adv7171_stop(void)
{
	GPSR = SCK;
	udelay(2);
	GPSR = SDA;
	udelay(1);
}

static void adv7171_send(unsigned byte)
{
	unsigned i;

	for (i = 0; i < 8; i++, byte <<= 1) {
		GPCR = SCK;
		udelay(1);
		if (byte & 0x80)
			GPSR = SDA;
		else
			GPCR = SDA;
		udelay(1);
		GPSR = SCK;
		udelay(1);
	}
	GPCR = SCK;
	udelay(1);
	GPSR = SDA;
	udelay(1);
	GPDR &= ~SDA;
	GPSR = SCK;
	udelay(1);
	if (GPLR & SDA)
		printk(KERN_WARNING "No ACK from ADV7171\n");
	udelay(1);
	GPCR = SCK | SDA;
	udelay(1);
	GPDR |= SDA;
	udelay(1);
}

static void adv7171_write(unsigned reg, unsigned val)
{
	unsigned gpdr = GPDR;
	unsigned gplr = GPLR;

	ASSABET_BCR_frob(ASSABET_BCR_AUDIO_ON, ASSABET_BCR_AUDIO_ON);
	udelay(100);

	GPCR = SDA | SCK | MOD; /* clear L3 mode to ensure UDA1341 doesn't respond */
	GPDR = (GPDR | SCK | MOD) & ~SDA;
	udelay(10);
	if (!(GPLR & SDA))
		printk(KERN_WARNING "Something dragging SDA down?\n");
	GPDR |= SDA;

	adv7171_start();
	adv7171_send(0x54);
	adv7171_send(reg);
	adv7171_send(val);
	adv7171_stop();

	/* Restore GPIO state for L3 bus */
	GPSR = gplr & (SDA | SCK | MOD);
	GPCR = (~gplr) & (SDA | SCK | MOD);
	GPDR = gpdr;
}

static void adv7171_sleep(void)
{
	/* Put the ADV7171 into sleep mode */
	adv7171_write(0x04, 0x40);
}

static unsigned codec_nreset;

static void assabet_codec_reset(unsigned mask, int set)
{
	unsigned long flags;
	bool old;

	local_irq_save(flags);
	old = !codec_nreset;
	if (set)
		codec_nreset &= ~mask;
	else
		codec_nreset |= mask;

	if (old != !codec_nreset) {
		if (codec_nreset) {
			ASSABET_BCR_set(ASSABET_BCR_NCODEC_RST);
			adv7171_sleep();
		} else {
			ASSABET_BCR_clear(ASSABET_BCR_NCODEC_RST);
		}
	}
	local_irq_restore(flags);
}

static void assabet_ucb1x00_reset(enum ucb1x00_reset state)
{
	int set = state == UCB_RST_REMOVE || state == UCB_RST_SUSPEND ||
		state == UCB_RST_PROBE_FAIL;
	assabet_codec_reset(RST_UCB1X00, set);
}

void assabet_uda1341_reset(int set)
{
	assabet_codec_reset(RST_UDA1341, set);
}
EXPORT_SYMBOL(assabet_uda1341_reset);


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


static struct ucb1x00_plat_data assabet_ucb1x00_data = {
	.reset		= assabet_ucb1x00_reset,
	.gpio_base	= -1,
	.can_wakeup	= 1,
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

static struct gpiod_lookup_table assabet_cf_gpio_table = {
	.dev_id = "sa11x0-pcmcia.1",
	.table = {
		GPIO_LOOKUP("gpio", 21, "ready", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio", 22, "detect", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("gpio", 24, "bvd2", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("gpio", 25, "bvd1", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("assabet", 1, "reset", GPIO_ACTIVE_HIGH),
		GPIO_LOOKUP("assabet", 7, "bus-enable", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct regulator_consumer_supply assabet_cf_vcc_consumers[] = {
	REGULATOR_SUPPLY("vcc", "sa11x0-pcmcia.1"),
};

static struct fixed_voltage_config assabet_cf_vcc_pdata __initdata = {
	.supply_name = "cf-power",
	.microvolts = 3300000,
};

static struct gpiod_lookup_table assabet_cf_vcc_gpio_table = {
	.dev_id = "reg-fixed-voltage.0",
	.table = {
		GPIO_LOOKUP("assabet", 0, NULL, GPIO_ACTIVE_HIGH),
		{ },
	},
};

static struct gpiod_lookup_table assabet_leds_gpio_table = {
	.dev_id = "leds-gpio",
	.table = {
		GPIO_LOOKUP("assabet", 13, NULL, GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("assabet", 14, NULL, GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct gpio_led assabet_leds[] __initdata = {
	{
		.name = "assabet:red",
		.default_trigger = "cpu0",
		.default_state = LEDS_GPIO_DEFSTATE_KEEP,
	}, {
		.name = "assabet:green",
		.default_trigger = "heartbeat",
		.default_state = LEDS_GPIO_DEFSTATE_KEEP,
	},
};

static const struct gpio_led_platform_data assabet_leds_pdata __initconst = {
	.num_leds = ARRAY_SIZE(assabet_leds),
	.leds = assabet_leds,
};

static struct gpio_keys_button assabet_keys_buttons[] = {
	{
		.gpio = 0,
		.irq = IRQ_GPIO0,
		.desc = "gpio0",
		.wakeup = 1,
		.can_disable = 1,
		.debounce_interval = 5,
	}, {
		.gpio = 1,
		.irq = IRQ_GPIO1,
		.desc = "gpio1",
		.wakeup = 1,
		.can_disable = 1,
		.debounce_interval = 5,
	},
};

static const struct gpio_keys_platform_data assabet_keys_pdata = {
	.buttons = assabet_keys_buttons,
	.nbuttons = ARRAY_SIZE(assabet_keys_buttons),
	.rep = 0,
};

static struct gpiod_lookup_table assabet_uart1_gpio_table = {
	.dev_id = "sa11x0-uart.1",
	.table = {
		GPIO_LOOKUP("assabet", 16, "dtr", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("assabet", 17, "rts", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("assabet", 25, "dcd", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("assabet", 26, "cts", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("assabet", 27, "dsr", GPIO_ACTIVE_LOW),
		{ },
	},
};

static struct gpiod_lookup_table assabet_uart3_gpio_table = {
	.dev_id = "sa11x0-uart.3",
	.table = {
		GPIO_LOOKUP("assabet", 28, "cts", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("assabet", 29, "dsr", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("assabet", 30, "dcd", GPIO_ACTIVE_LOW),
		GPIO_LOOKUP("assabet", 31, "rng", GPIO_ACTIVE_LOW),
		{ },
	},
};

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
#ifndef CONFIG_ASSABET_NEPONSET
		printk( "Warning: Neponset detected but full support "
			"hasn't been configured in the kernel\n" );
#else
		platform_device_register_simple("neponset", 0,
			neponset_resources, ARRAY_SIZE(neponset_resources));
#endif
	} else {
		gpiod_add_lookup_table(&assabet_uart1_gpio_table);
		gpiod_add_lookup_table(&assabet_uart3_gpio_table);
		gpiod_add_lookup_table(&assabet_cf_vcc_gpio_table);

		sa11x0_register_fixed_regulator(0, &assabet_cf_vcc_pdata,
					assabet_cf_vcc_consumers,
					ARRAY_SIZE(assabet_cf_vcc_consumers),
					true);

	}

	platform_device_register_resndata(NULL, "gpio-keys", 0,
					  NULL, 0,
					  &assabet_keys_pdata,
					  sizeof(assabet_keys_pdata));

	gpiod_add_lookup_table(&assabet_leds_gpio_table);
	gpio_led_register_device(-1, &assabet_leds_pdata);

#ifndef ASSABET_PAL_VIDEO
	sa11x0_register_lcd(&lq039q2ds54_info);
#else
	sa11x0_register_lcd(&pal_video);
#endif
	sa11x0_register_mtd(&assabet_flash_data, assabet_flash_resources,
			    ARRAY_SIZE(assabet_flash_resources));
	sa11x0_register_mcp(&assabet_mcp_data);

	if (!machine_has_neponset())
		sa11x0_register_pcmcia(1, &assabet_cf_gpio_table);
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

	pmd = pmd_off_k(virt);
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
	for(i = 100; i--; )		/* Read GPIO 9:2 */
		scr = GPLR;
	GPDR |= 0x3fc;			/*  restore correct pin direction */
	scr &= 0x3fc;			/* save as system configuration byte. */
	SCR_value = scr;
}

static void __init
fixup_assabet(struct tag *tags, char **cmdline)
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
			ASSABET_BCR_clear(ASSABET_BCR_RS232EN);
		else
			ASSABET_BCR_set(ASSABET_BCR_RS232EN);
	}
}

static struct sa1100_port_fns assabet_port_fns __initdata = {
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
	MSC1 = (MSC1 & ~0xffff) |
		MSC_NonBrst | MSC_32BitStMem |
		MSC_RdAcc(2) | MSC_WrAcc(2) | MSC_Rec(0);

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

void __init assabet_init_irq(void)
{
	u32 def_val;

	sa1100_init_irq();

	if (machine_has_neponset())
		def_val = ASSABET_BCR_DB1111;
	else
		def_val = ASSABET_BCR_DB1110;

	/*
	 * Angel sets this, but other bootloaders may not.
	 *
	 * This must precede any driver calls to BCR_set() or BCR_clear().
	 */
	assabet_init_gpio((void *)&ASSABET_BCR, def_val);
}

MACHINE_START(ASSABET, "Intel-Assabet")
	.atag_offset	= 0x100,
	.fixup		= fixup_assabet,
	.map_io		= assabet_map_io,
	.nr_irqs	= SA1100_NR_IRQS,
	.init_irq	= assabet_init_irq,
	.init_time	= sa1100_timer_init,
	.init_machine	= assabet_init,
	.init_late	= sa11x0_init_late,
#ifdef CONFIG_SA1111
	.dma_zone_size	= SZ_1M,
#endif
	.restart	= sa11x0_restart,
MACHINE_END
