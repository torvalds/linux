/*
 * linux/arch/arm/mach-nuc93x/cpu.c
 *
 * Copyright (c) 2009 Nuvoton corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * NUC93x series cpu common support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/serial_8250.h>
#include <linux/delay.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>
#include <asm/irq.h>

#include <mach/hardware.h>
#include <mach/regs-serial.h>
#include <mach/regs-clock.h>
#include <mach/regs-ebi.h>

#include "cpu.h"
#include "clock.h"

/* Initial IO mappings */

static struct map_desc nuc93x_iodesc[] __initdata = {
	IODESC_ENT(IRQ),
	IODESC_ENT(GCR),
	IODESC_ENT(UART),
	IODESC_ENT(TIMER),
	IODESC_ENT(EBI),
};

/* Initial nuc932 clock declarations. */
static DEFINE_CLK(audio, 2);
static DEFINE_CLK(sd, 3);
static DEFINE_CLK(jpg, 4);
static DEFINE_CLK(video, 5);
static DEFINE_CLK(vpost, 6);
static DEFINE_CLK(2d, 7);
static DEFINE_CLK(gpu, 8);
static DEFINE_CLK(gdma, 9);
static DEFINE_CLK(adc, 10);
static DEFINE_CLK(uart, 11);
static DEFINE_CLK(spi, 12);
static DEFINE_CLK(pwm, 13);
static DEFINE_CLK(timer, 14);
static DEFINE_CLK(wdt, 15);
static DEFINE_CLK(ac97, 16);
static DEFINE_CLK(i2s, 16);
static DEFINE_CLK(usbck, 17);
static DEFINE_CLK(usb48, 18);
static DEFINE_CLK(usbh, 19);
static DEFINE_CLK(i2c, 20);
static DEFINE_CLK(ext, 0);

static struct clk_lookup nuc932_clkregs[] = {
       DEF_CLKLOOK(&clk_audio, "nuc932-audio", NULL),
       DEF_CLKLOOK(&clk_sd, "nuc932-sd", NULL),
       DEF_CLKLOOK(&clk_jpg, "nuc932-jpg", "NULL"),
       DEF_CLKLOOK(&clk_video, "nuc932-video", "NULL"),
       DEF_CLKLOOK(&clk_vpost, "nuc932-vpost", NULL),
       DEF_CLKLOOK(&clk_2d, "nuc932-2d", NULL),
       DEF_CLKLOOK(&clk_gpu, "nuc932-gpu", NULL),
       DEF_CLKLOOK(&clk_gdma, "nuc932-gdma", "NULL"),
       DEF_CLKLOOK(&clk_adc, "nuc932-adc", NULL),
       DEF_CLKLOOK(&clk_uart, NULL, "uart"),
       DEF_CLKLOOK(&clk_spi, "nuc932-spi", NULL),
       DEF_CLKLOOK(&clk_pwm, "nuc932-pwm", NULL),
       DEF_CLKLOOK(&clk_timer, NULL, "timer"),
       DEF_CLKLOOK(&clk_wdt, "nuc932-wdt", NULL),
       DEF_CLKLOOK(&clk_ac97, "nuc932-ac97", NULL),
       DEF_CLKLOOK(&clk_i2s, "nuc932-i2s", NULL),
       DEF_CLKLOOK(&clk_usbck, "nuc932-usbck", NULL),
       DEF_CLKLOOK(&clk_usb48, "nuc932-usb48", NULL),
       DEF_CLKLOOK(&clk_usbh, "nuc932-usbh", NULL),
       DEF_CLKLOOK(&clk_i2c, "nuc932-i2c", NULL),
       DEF_CLKLOOK(&clk_ext, NULL, "ext"),
};

/* Initial serial platform data */

struct plat_serial8250_port nuc93x_uart_data[] = {
	NUC93X_8250PORT(UART0),
	{},
};

struct platform_device nuc93x_serial_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= nuc93x_uart_data,
	},
};

/*Init NUC93x evb io*/

void __init nuc93x_map_io(struct map_desc *mach_desc, int mach_size)
{
	unsigned long idcode = 0x0;

	iotable_init(mach_desc, mach_size);
	iotable_init(nuc93x_iodesc, ARRAY_SIZE(nuc93x_iodesc));

	idcode = __raw_readl(NUC93XPDID);
	if (idcode == NUC932_CPUID)
		printk(KERN_INFO "CPU type 0x%08lx is NUC910\n", idcode);
	else
		printk(KERN_ERR "CPU type detect error!\n");

}

/*Init NUC93x clock*/

void __init nuc93x_init_clocks(void)
{
	clks_register(nuc932_clkregs, ARRAY_SIZE(nuc932_clkregs));
}

