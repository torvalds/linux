/* linux/arch/arm/mach-s3c2410/mach-h1940.c
 *
 * Copyright (c) 2003-2005 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * http://www.handhelds.org/projects/h1940.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/sysdev.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mach-types.h>

#include <asm/plat-s3c/regs-serial.h>
#include <asm/arch/regs-lcd.h>
#include <asm/arch/regs-gpio.h>
#include <asm/arch/regs-clock.h>

#include <asm/arch/h1940.h>
#include <asm/arch/h1940-latch.h>
#include <asm/arch/fb.h>
#include <asm/plat-s3c24xx/udc.h>

#include <asm/plat-s3c24xx/clock.h>
#include <asm/plat-s3c24xx/devs.h>
#include <asm/plat-s3c24xx/cpu.h>
#include <asm/plat-s3c24xx/pm.h>

static struct map_desc h1940_iodesc[] __initdata = {
	[0] = {
		.virtual	= (unsigned long)H1940_LATCH,
		.pfn		= __phys_to_pfn(H1940_PA_LATCH),
		.length		= SZ_16K,
		.type		= MT_DEVICE
	},
};

#define UCON S3C2410_UCON_DEFAULT | S3C2410_UCON_UCLK
#define ULCON S3C2410_LCON_CS8 | S3C2410_LCON_PNONE | S3C2410_LCON_STOPB
#define UFCON S3C2410_UFCON_RXTRIG8 | S3C2410_UFCON_FIFOMODE

static struct s3c2410_uartcfg h1940_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x03,
		.ufcon	     = 0x51,
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = 0x245,
		.ulcon	     = 0x03,
		.ufcon	     = 0x00,
	},
	/* IR port */
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.uart_flags  = UPF_CONS_FLOW,
		.ucon	     = 0x3c5,
		.ulcon	     = 0x43,
		.ufcon	     = 0x51,
	}
};

/* Board control latch control */

static unsigned int latch_state = H1940_LATCH_DEFAULT;

void h1940_latch_control(unsigned int clear, unsigned int set)
{
	unsigned long flags;

	local_irq_save(flags);

	latch_state &= ~clear;
	latch_state |= set;

	__raw_writel(latch_state, H1940_LATCH);

	local_irq_restore(flags);
}

EXPORT_SYMBOL_GPL(h1940_latch_control);

static void h1940_udc_pullup(enum s3c2410_udc_cmd_e cmd)
{
	printk(KERN_DEBUG "udc: pullup(%d)\n",cmd);

	switch (cmd)
	{
		case S3C2410_UDC_P_ENABLE :
			h1940_latch_control(0, H1940_LATCH_USB_DP);
			break;
		case S3C2410_UDC_P_DISABLE :
			h1940_latch_control(H1940_LATCH_USB_DP, 0);
			break;
		case S3C2410_UDC_P_RESET :
			break;
		default:
			break;
	}
}

static struct s3c2410_udc_mach_info h1940_udc_cfg __initdata = {
	.udc_command		= h1940_udc_pullup,
	.vbus_pin		= S3C2410_GPG5,
	.vbus_pin_inverted	= 1,
};


/**
 * Set lcd on or off
 **/
static struct s3c2410fb_display h1940_lcd __initdata = {
	.lcdcon1=	S3C2410_LCDCON1_TFT16BPP | \
			S3C2410_LCDCON1_TFT | \
			S3C2410_LCDCON1_CLKVAL(0x0C),

	.lcdcon5=	S3C2410_LCDCON5_FRM565 | \
			S3C2410_LCDCON5_INVVLINE | \
			S3C2410_LCDCON5_HWSWP,

	.type =		S3C2410_LCDCON1_TFT,
	.width =	240,
	.height =	320,
	.pixclock =	260000,
	.xres =		240,
	.yres =		320,
	.bpp =		16,
	.left_margin =	20,
	.right_margin =	8,
	.hsync_len =	4,
	.upper_margin =	8,
	.lower_margin = 7,
	.vsync_len =	1,
};

static struct s3c2410fb_mach_info h1940_fb_info __initdata = {
	.displays = &h1940_lcd,
	.num_displays = 1,
	.default_display = 0,

	.lpcsel=	0x02,
	.gpccon=	0xaa940659,
	.gpccon_mask=	0xffffffff,
	.gpcup=		0x0000ffff,
	.gpcup_mask=	0xffffffff,
	.gpdcon=	0xaa84aaa0,
	.gpdcon_mask=	0xffffffff,
	.gpdup=		0x0000faff,
	.gpdup_mask=	0xffffffff,
};

static struct platform_device s3c_device_leds = {
	.name             = "h1940-leds",
	.id               = -1,
};

static struct platform_device s3c_device_bluetooth = {
	.name             = "h1940-bt",
	.id               = -1,
};

static struct platform_device *h1940_devices[] __initdata = {
	&s3c_device_usb,
	&s3c_device_lcd,
	&s3c_device_wdt,
	&s3c_device_i2c,
	&s3c_device_iis,
	&s3c_device_usbgadget,
	&s3c_device_leds,
	&s3c_device_bluetooth,
};

static void __init h1940_map_io(void)
{
	s3c24xx_init_io(h1940_iodesc, ARRAY_SIZE(h1940_iodesc));
	s3c24xx_init_clocks(0);
	s3c24xx_init_uarts(h1940_uartcfgs, ARRAY_SIZE(h1940_uartcfgs));

	/* setup PM */

#ifdef CONFIG_PM_H1940
	memcpy(phys_to_virt(H1940_SUSPEND_RESUMEAT), h1940_pm_return, 1024);
#endif
	s3c2410_pm_init();
}

static void __init h1940_init_irq(void)
{
	s3c24xx_init_irq();
}

static void __init h1940_init(void)
{
	u32 tmp;

	s3c24xx_fb_set_platdata(&h1940_fb_info);
 	s3c24xx_udc_set_platdata(&h1940_udc_cfg);

	/* Turn off suspend on both USB ports, and switch the
	 * selectable USB port to USB device mode. */

	s3c2410_modify_misccr(S3C2410_MISCCR_USBHOST |
			      S3C2410_MISCCR_USBSUSPND0 |
			      S3C2410_MISCCR_USBSUSPND1, 0x0);

	tmp = (
		 0x78 << S3C2410_PLLCON_MDIVSHIFT)
	      | (0x02 << S3C2410_PLLCON_PDIVSHIFT)
	      | (0x03 << S3C2410_PLLCON_SDIVSHIFT);
	writel(tmp, S3C2410_UPLLCON);

	platform_add_devices(h1940_devices, ARRAY_SIZE(h1940_devices));
}

MACHINE_START(H1940, "IPAQ-H1940")
	/* Maintainer: Ben Dooks <ben@fluff.org> */
	.phys_io	= S3C2410_PA_UART,
	.io_pg_offst	= (((u32)S3C24XX_VA_UART) >> 18) & 0xfffc,
	.boot_params	= S3C2410_SDRAM_PA + 0x100,
	.map_io		= h1940_map_io,
	.init_irq	= h1940_init_irq,
	.init_machine	= h1940_init,
	.timer		= &s3c24xx_timer,
MACHINE_END
