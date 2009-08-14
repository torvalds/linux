/*
 * linux/arch/arm/mach-w90x900/w90p910.c
 *
 * Based on linux/arch/arm/plat-s3c24xx/s3c244x.c by Ben Dooks
 *
 * Copyright (c) 2008 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * W90P910 cpu support
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

static struct map_desc w90p910_iodesc[] __initdata = {
	IODESC_ENT(IRQ),
	IODESC_ENT(GCR),
	IODESC_ENT(UART),
	IODESC_ENT(TIMER),
	IODESC_ENT(EBI),
	IODESC_ENT(USBEHCIHOST),
	IODESC_ENT(USBOHCIHOST),
	IODESC_ENT(ADC),
	IODESC_ENT(RTC),
	IODESC_ENT(KPI),
	IODESC_ENT(USBDEV),
	/*IODESC_ENT(LCD),*/
};

/* Initial clock declarations. */
static DEFINE_CLK(lcd, 0);
static DEFINE_CLK(audio, 1);
static DEFINE_CLK(fmi, 4);
static DEFINE_SUBCLK(ms, 0);
static DEFINE_SUBCLK(sd, 1);
static DEFINE_CLK(dmac, 5);
static DEFINE_CLK(atapi, 6);
static DEFINE_CLK(emc, 7);
static DEFINE_SUBCLK(rmii, 2);
static DEFINE_CLK(usbd, 8);
static DEFINE_CLK(usbh, 9);
static DEFINE_CLK(g2d, 10);;
static DEFINE_CLK(pwm, 18);
static DEFINE_CLK(ps2, 24);
static DEFINE_CLK(kpi, 25);
static DEFINE_CLK(wdt, 26);
static DEFINE_CLK(gdma, 27);
static DEFINE_CLK(adc, 28);
static DEFINE_CLK(usi, 29);
static DEFINE_CLK(ext, 0);

static struct clk_lookup w90p910_clkregs[] = {
	DEF_CLKLOOK(&clk_lcd, "w90p910-lcd", NULL),
	DEF_CLKLOOK(&clk_audio, "w90p910-audio", NULL),
	DEF_CLKLOOK(&clk_fmi, "w90p910-fmi", NULL),
	DEF_CLKLOOK(&clk_ms, "w90p910-fmi", "MS"),
	DEF_CLKLOOK(&clk_sd, "w90p910-fmi", "SD"),
	DEF_CLKLOOK(&clk_dmac, "w90p910-dmac", NULL),
	DEF_CLKLOOK(&clk_atapi, "w90p910-atapi", NULL),
	DEF_CLKLOOK(&clk_emc, "w90p910-emc", NULL),
	DEF_CLKLOOK(&clk_rmii, "w90p910-emc", "RMII"),
	DEF_CLKLOOK(&clk_usbd, "w90p910-usbd", NULL),
	DEF_CLKLOOK(&clk_usbh, "w90p910-usbh", NULL),
	DEF_CLKLOOK(&clk_g2d, "w90p910-g2d", NULL),
	DEF_CLKLOOK(&clk_pwm, "w90p910-pwm", NULL),
	DEF_CLKLOOK(&clk_ps2, "w90p910-ps2", NULL),
	DEF_CLKLOOK(&clk_kpi, "w90p910-kpi", NULL),
	DEF_CLKLOOK(&clk_wdt, "w90p910-wdt", NULL),
	DEF_CLKLOOK(&clk_gdma, "w90p910-gdma", NULL),
	DEF_CLKLOOK(&clk_adc, "w90p910-adc", NULL),
	DEF_CLKLOOK(&clk_usi, "w90p910-spi", NULL),
	DEF_CLKLOOK(&clk_ext, NULL, "ext"),
};

/* Initial serial platform data */

struct plat_serial8250_port w90p910_uart_data[] = {
	W90X900_8250PORT(UART0),
};

struct platform_device w90p910_serial_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= w90p910_uart_data,
	},
};

/*Init W90P910 evb io*/

void __init w90p910_map_io(struct map_desc *mach_desc, int mach_size)
{
	unsigned long idcode = 0x0;

	iotable_init(w90p910_iodesc, ARRAY_SIZE(w90p910_iodesc));

	idcode = __raw_readl(W90X900PDID);
	if (idcode != W90P910_CPUID)
		printk(KERN_ERR "CPU type 0x%08lx is not W90P910\n", idcode);
}

/*Set W90P910 cpu frequence*/
static int __init w90p910_set_clkval(unsigned int cpufreq)
{
	unsigned int pllclk, ahbclk, apbclk, val;

	pllclk = 0;
	ahbclk = 0;
	apbclk = 0;

	switch (cpufreq) {
	case 66:
		pllclk = PLL_66MHZ;
		ahbclk = AHB_CPUCLK_1_1;
		apbclk = APB_AHB_1_2;
		break;

	case 100:
		pllclk = PLL_100MHZ;
		ahbclk = AHB_CPUCLK_1_1;
		apbclk = APB_AHB_1_2;
		break;

	case 120:
		pllclk = PLL_120MHZ;
		ahbclk = AHB_CPUCLK_1_2;
		apbclk = APB_AHB_1_2;
		break;

	case 166:
		pllclk = PLL_166MHZ;
		ahbclk = AHB_CPUCLK_1_2;
		apbclk = APB_AHB_1_2;
		break;

	case 200:
		pllclk = PLL_200MHZ;
		ahbclk = AHB_CPUCLK_1_2;
		apbclk = APB_AHB_1_2;
		break;
	}

	__raw_writel(pllclk, REG_PLLCON0);

	val = __raw_readl(REG_CLKDIV);
	val &= ~(0x03 << 24 | 0x03 << 26);
	val |= (ahbclk << 24 | apbclk << 26);
	__raw_writel(val, REG_CLKDIV);

	return 	0;
}
static int __init w90p910_set_cpufreq(char *str)
{
	unsigned long cpufreq, val;

	if (!*str)
		return 0;

	strict_strtoul(str, 0, &cpufreq);

	w90p910_clock_source(NULL, "ext");

	w90p910_set_clkval(cpufreq);

	mdelay(1);

	val = __raw_readl(REG_CKSKEW);
	val &= ~0xff;
	val |= DEFAULTSKEW;
	__raw_writel(val, REG_CKSKEW);

	w90p910_clock_source(NULL, "pll0");

	return 1;
}

__setup("cpufreq=", w90p910_set_cpufreq);

/*Init W90P910 clock*/

void __init w90p910_init_clocks(void)
{
	clks_register(w90p910_clkregs, ARRAY_SIZE(w90p910_clkregs));
}

static int __init w90p910_init_cpu(void)
{
	return 0;
}

static int __init w90x900_arch_init(void)
{
	return w90p910_init_cpu();
}
arch_initcall(w90x900_arch_init);
