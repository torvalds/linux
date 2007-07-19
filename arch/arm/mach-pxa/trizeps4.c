/*
 *  linux/arch/arm/mach-pxa/trizeps4.c
 *
 *  Support for the Keith und Koep Trizeps4 Module Platform.
 *
 *  Author:	Jürgen Schindele
 *  Created:	20 02, 2006
 *  Copyright:	Jürgen Schindele
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/bitops.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/serial_8250.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/physmap.h>
#include <linux/mtd/partitions.h>

#include <asm/types.h>
#include <asm/setup.h>
#include <asm/memory.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/sizes.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/mach/flash.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/trizeps4.h>
#include <asm/arch/audio.h>
#include <asm/arch/pxafb.h>
#include <asm/arch/mmc.h>
#include <asm/arch/irda.h>
#include <asm/arch/ohci.h>

#include "generic.h"
#include "devices.h"

/********************************************************************************************
 * ONBOARD FLASH
 ********************************************************************************************/
static struct mtd_partition trizeps4_partitions[] = {
	{
		.name =		"Bootloader",
		.offset =	0x00000000,
		.size =		0x00040000,
		.mask_flags =	MTD_WRITEABLE  /* force read-only */
	},{
		.name =		"Backup",
		.offset =	0x00040000,
		.size =		0x00040000,
	},{
		.name =		"Image",
		.offset =	0x00080000,
		.size =		0x01080000,
	},{
		.name =		"IPSM",
		.offset =	0x01100000,
		.size =		0x00e00000,
	},{
		.name =		"Registry",
		.offset =	0x01f00000,
		.size =		MTDPART_SIZ_FULL,
	}
};

static struct physmap_flash_data trizeps4_flash_data[] = {
	{
		.width		= 4,			/* bankwidth in bytes */
		.parts		= trizeps4_partitions,
		.nr_parts	= ARRAY_SIZE(trizeps4_partitions)
	}
};

static struct resource flash_resource = {
	.start	= PXA_CS0_PHYS,
	.end	= PXA_CS0_PHYS + SZ_32M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct platform_device flash_device = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev = {
		.platform_data = trizeps4_flash_data,
	},
	.resource = &flash_resource,
	.num_resources = 1,
};

/********************************************************************************************
 * DAVICOM DM9000 Ethernet
 ********************************************************************************************/
static struct resource dm9000_resources[] = {
	[0] = {
		.start	= TRIZEPS4_ETH_PHYS+0x300,
		.end	= TRIZEPS4_ETH_PHYS+0x400-1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= TRIZEPS4_ETH_PHYS+0x8300,
		.end	= TRIZEPS4_ETH_PHYS+0x8400-1,
		.flags	= IORESOURCE_MEM,
	},
	[2] = {
		.start	= TRIZEPS4_ETH_IRQ,
		.end	= TRIZEPS4_ETH_IRQ,
		.flags	= (IORESOURCE_IRQ | IRQT_RISING),
	},
};

static struct platform_device dm9000_device = {
	.name		= "dm9000",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(dm9000_resources),
	.resource	= dm9000_resources,
};

/********************************************************************************************
 * PXA270 serial ports
 ********************************************************************************************/
static struct plat_serial8250_port tri_serial_ports[] = {
#ifdef CONFIG_SERIAL_PXA
	/* this uses the own PXA driver */
	{
		0,
	},
#else
	/* this uses the generic 8520 driver */
	[0] = {
		.membase	= (void *)&FFUART,
		.irq		= IRQ_FFUART,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM32,
		.regshift	= 2,
		.uartclk	= (921600*16),
	},
	[1] = {
		.membase	= (void *)&BTUART,
		.irq		= IRQ_BTUART,
		.flags		= UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM32,
		.regshift	= 2,
		.uartclk	= (921600*16),
	},
	{
		0,
	},
#endif
};

static struct platform_device uart_devices = {
	.name		= "serial8250",
	.id		= 0,
	.dev		= {
		.platform_data	= tri_serial_ports,
	},
	.num_resources	= 0,
	.resource	= NULL,
};

/********************************************************************************************
 * PXA270 ac97 sound codec
 ********************************************************************************************/
static struct platform_device ac97_audio_device = {
	.name		= "pxa2xx-ac97",
	.id		= -1,
};

static struct platform_device * trizeps4_devices[] __initdata = {
	&flash_device,
	&uart_devices,
	&dm9000_device,
	&ac97_audio_device,
};

#ifdef CONFIG_MACH_TRIZEPS4_CONXS
static short trizeps_conxs_bcr;

/* PCCARD power switching supports only 3,3V */
void board_pcmcia_power(int power)
{
	if (power) {
		/* switch power on, put in reset and enable buffers */
		trizeps_conxs_bcr |= power;
		trizeps_conxs_bcr |= ConXS_BCR_CF_RESET;
		trizeps_conxs_bcr &= ~(ConXS_BCR_CF_BUF_EN);
		ConXS_BCR = trizeps_conxs_bcr;
		/* wait a little */
		udelay(2000);
		/* take reset away */
		trizeps_conxs_bcr &= ~(ConXS_BCR_CF_RESET);
		ConXS_BCR = trizeps_conxs_bcr;
		udelay(2000);
	} else {
		/* put in reset */
		trizeps_conxs_bcr |= ConXS_BCR_CF_RESET;
		ConXS_BCR = trizeps_conxs_bcr;
		udelay(1000);
		/* switch power off */
		trizeps_conxs_bcr &= ~(0xf);
		ConXS_BCR = trizeps_conxs_bcr;

	}
	pr_debug("%s: o%s 0x%x\n", __FUNCTION__, power ? "n": "ff", trizeps_conxs_bcr);
}

/* backlight power switching for LCD panel */
static void board_backlight_power(int on)
{
	if (on) {
		trizeps_conxs_bcr |= ConXS_BCR_L_DISP;
	} else {
		trizeps_conxs_bcr &= ~ConXS_BCR_L_DISP;
	}
	pr_debug("%s: o%s 0x%x\n", __FUNCTION__, on ? "n" : "ff", trizeps_conxs_bcr);
	ConXS_BCR = trizeps_conxs_bcr;
}

/* Powersupply for MMC/SD cardslot */
static void board_mci_power(struct device *dev, unsigned int vdd)
{
	struct pxamci_platform_data* p_d = dev->platform_data;

	if (( 1 << vdd) & p_d->ocr_mask) {
		pr_debug("%s: on\n", __FUNCTION__);
		/* FIXME fill in values here */
	} else {
		pr_debug("%s: off\n", __FUNCTION__);
		/* FIXME fill in values here */
	}
}

static short trizeps_conxs_ircr;

/* Switch modes and Power for IRDA receiver */
static void board_irda_mode(struct device *dev, int mode)
{
	unsigned long flags;

	local_irq_save(flags);
	if (mode & IR_SIRMODE) {
		/* Slow mode */
		trizeps_conxs_ircr &= ~ConXS_IRCR_MODE;
	} else if (mode & IR_FIRMODE) {
		/* Fast mode */
		trizeps_conxs_ircr |= ConXS_IRCR_MODE;
	}
	if (mode & IR_OFF) {
		trizeps_conxs_ircr |= ConXS_IRCR_SD;
	} else {
		trizeps_conxs_ircr &= ~ConXS_IRCR_SD;
	}
	/* FIXME write values to register */
	local_irq_restore(flags);
}

#else
/* for other baseboards define dummies */
void board_pcmcia_power(int power)	{;}
#define board_backlight_power		NULL
#define board_mci_power			NULL
#define board_irda_mode			NULL

#endif		/* CONFIG_MACH_TRIZEPS4_CONXS */
EXPORT_SYMBOL(board_pcmcia_power);

static int trizeps4_mci_init(struct device *dev, irq_handler_t mci_detect_int, void *data)
{
	int err;
	/* setup GPIO for PXA27x MMC controller */
	pxa_gpio_mode(GPIO32_MMCCLK_MD);
	pxa_gpio_mode(GPIO112_MMCCMD_MD);
	pxa_gpio_mode(GPIO92_MMCDAT0_MD);
	pxa_gpio_mode(GPIO109_MMCDAT1_MD);
	pxa_gpio_mode(GPIO110_MMCDAT2_MD);
	pxa_gpio_mode(GPIO111_MMCDAT3_MD);

	pxa_gpio_mode(GPIO_MMC_DET | GPIO_IN);

	err = request_irq(TRIZEPS4_MMC_IRQ, mci_detect_int,
			  IRQF_DISABLED | IRQF_TRIGGER_RISING,
			  "MMC card detect", data);
	if (err) {
		printk(KERN_ERR "trizeps4_mci_init: MMC/SD: can't request MMC card detect IRQ\n");
		return -1;
	}
	return 0;
}

static void trizeps4_mci_exit(struct device *dev, void *data)
{
	free_irq(TRIZEPS4_MMC_IRQ, data);
}

static struct pxamci_platform_data trizeps4_mci_platform_data = {
	.ocr_mask	= MMC_VDD_32_33|MMC_VDD_33_34,
	.init 		= trizeps4_mci_init,
	.exit		= trizeps4_mci_exit,
	.setpower 	= board_mci_power,
};

static struct pxaficp_platform_data trizeps4_ficp_platform_data = {
	.transceiver_cap  = IR_SIRMODE | IR_FIRMODE | IR_OFF,
	.transceiver_mode = board_irda_mode,
};

static int trizeps4_ohci_init(struct device *dev)
{
	/* setup Port1 GPIO pin. */
	pxa_gpio_mode( 88 | GPIO_ALT_FN_1_IN);	/* USBHPWR1 */
	pxa_gpio_mode( 89 | GPIO_ALT_FN_2_OUT);	/* USBHPEN1 */

	/* Set the Power Control Polarity Low and Power Sense
	   Polarity Low to active low. */
	UHCHR = (UHCHR | UHCHR_PCPL | UHCHR_PSPL) &
		~(UHCHR_SSEP1 | UHCHR_SSEP2 | UHCHR_SSEP3 | UHCHR_SSE);

	return 0;
}

static void trizeps4_ohci_exit(struct device *dev)
{
	;
}

static struct pxaohci_platform_data trizeps4_ohci_platform_data = {
	.port_mode	= PMM_PERPORT_MODE,
	.init		= trizeps4_ohci_init,
	.exit		= trizeps4_ohci_exit,
};

static struct map_desc trizeps4_io_desc[] __initdata = {
	{ 	/* ConXS CFSR */
		.virtual	= TRIZEPS4_CFSR_VIRT,
		.pfn		= __phys_to_pfn(TRIZEPS4_CFSR_PHYS),
		.length		= 0x00001000,
		.type		= MT_DEVICE
	},
	{	/* ConXS BCR */
		.virtual	= TRIZEPS4_BOCR_VIRT,
		.pfn		= __phys_to_pfn(TRIZEPS4_BOCR_PHYS),
		.length		= 0x00001000,
		.type		= MT_DEVICE
	},
	{ 	/* ConXS IRCR */
		.virtual	= TRIZEPS4_IRCR_VIRT,
		.pfn		= __phys_to_pfn(TRIZEPS4_IRCR_PHYS),
		.length		= 0x00001000,
		.type		= MT_DEVICE
	},
	{	/* ConXS DCR */
		.virtual	= TRIZEPS4_DICR_VIRT,
		.pfn		= __phys_to_pfn(TRIZEPS4_DICR_PHYS),
		.length		= 0x00001000,
		.type		= MT_DEVICE
	},
	{	/* ConXS UPSR */
		.virtual	= TRIZEPS4_UPSR_VIRT,
		.pfn		= __phys_to_pfn(TRIZEPS4_UPSR_PHYS),
		.length		= 0x00001000,
		.type		= MT_DEVICE
	}
};

static struct pxafb_mode_info sharp_lcd_mode = {
    .pixclock		= 78000,
    .xres		= 640,
    .yres		= 480,
    .bpp		= 8,
    .hsync_len		= 4,
    .left_margin	= 4,
    .right_margin	= 4,
    .vsync_len		= 2,
    .upper_margin	= 0,
    .lower_margin	= 0,
    .sync		= FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,
    .cmap_greyscale	= 0,
};

static struct pxafb_mach_info sharp_lcd = {
    .modes		= &sharp_lcd_mode,
    .num_modes	= 1,
    .cmap_inverse	= 0,
    .cmap_static	= 0,
    .lccr0		= LCCR0_Color | LCCR0_Pas | LCCR0_Dual,
    .lccr3		= 0x0340ff02,
    .pxafb_backlight_power = board_backlight_power,
};

static struct pxafb_mode_info toshiba_lcd_mode = {
    .pixclock		= 39720,
    .xres		= 640,
    .yres		= 480,
    .bpp		= 8,
    .hsync_len		= 63,
    .left_margin	= 12,
    .right_margin	= 12,
    .vsync_len		= 4,
    .upper_margin	= 32,
    .lower_margin	= 10,
    .sync		= 0,
    .cmap_greyscale	= 0,
};

static struct pxafb_mach_info toshiba_lcd = {
    .modes		= &toshiba_lcd_mode,
    .num_modes	= 1,
    .cmap_inverse	= 0,
    .cmap_static	= 0,
    .lccr0		= LCCR0_Color | LCCR0_Act,
    .lccr3		= 0x03400002,
    .pxafb_backlight_power = board_backlight_power,
};

static void __init trizeps4_init(void)
{
	platform_add_devices(trizeps4_devices, ARRAY_SIZE(trizeps4_devices));

/*	set_pxa_fb_info(&sharp_lcd); */
	set_pxa_fb_info(&toshiba_lcd);

	pxa_set_mci_info(&trizeps4_mci_platform_data);
	pxa_set_ficp_info(&trizeps4_ficp_platform_data);
	pxa_set_ohci_info(&trizeps4_ohci_platform_data);
}

static void __init trizeps4_map_io(void)
{
	pxa_map_io();
	iotable_init(trizeps4_io_desc, ARRAY_SIZE(trizeps4_io_desc));

	/* for DiskOnChip */
	pxa_gpio_mode(GPIO15_nCS_1_MD);

	/* for off-module PIC on ConXS board */
	pxa_gpio_mode(GPIO_PIC | GPIO_IN);

	/* UCB1400 irq */
	pxa_gpio_mode(GPIO_UCB1400 | GPIO_IN);

	/* for DM9000 LAN */
	pxa_gpio_mode(GPIO78_nCS_2_MD);
	pxa_gpio_mode(GPIO_DM9000 | GPIO_IN);

	/* for PCMCIA device */
	pxa_gpio_mode(GPIO_PCD | GPIO_IN);
	pxa_gpio_mode(GPIO_PRDY | GPIO_IN);

	/* for I2C adapter */
	pxa_gpio_mode(GPIO117_I2CSCL_MD);
	pxa_gpio_mode(GPIO118_I2CSDA_MD);

	/* MMC_DET s.o. */
	pxa_gpio_mode(GPIO_MMC_DET | GPIO_IN);

	/* whats that for ??? */
	pxa_gpio_mode(GPIO79_nCS_3_MD);

#ifdef CONFIG_LEDS
	pxa_gpio_mode( GPIO_SYS_BUSY_LED  | GPIO_OUT);		/* LED1 */
	pxa_gpio_mode( GPIO_HEARTBEAT_LED | GPIO_OUT);		/* LED2 */
#endif
#ifdef CONFIG_MACH_TRIZEPS4_CONXS
#ifdef CONFIG_IDE_PXA_CF
	/* if boot direct from compact flash dont disable power */
	trizeps_conxs_bcr = 0x0009;
#else
	/* this is the reset value */
	trizeps_conxs_bcr = 0x00A0;
#endif
	ConXS_BCR = trizeps_conxs_bcr;
#endif

	PWER  = 0x00000002;
	PFER  = 0x00000000;
	PRER  = 0x00000002;
	PGSR0 = 0x0158C000;
	PGSR1 = 0x00FF0080;
	PGSR2 = 0x0001C004;
	/* Stop 3.6MHz and drive HIGH to PCMCIA and CS */
	PCFR |= PCFR_OPDE;
}

MACHINE_START(TRIZEPS4, "Keith und Koep Trizeps IV module")
	/* MAINTAINER("Jürgen Schindele") */
	.phys_io	= 0x40000000,
	.io_pg_offst	= (io_p2v(0x40000000) >> 18) & 0xfffc,
	.boot_params	= TRIZEPS4_SDRAM_BASE + 0x100,
	.init_machine	= trizeps4_init,
	.map_io		= trizeps4_map_io,
	.init_irq	= pxa27x_init_irq,
	.timer		= &pxa_timer,
MACHINE_END

