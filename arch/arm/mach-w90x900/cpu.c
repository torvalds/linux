/*
 * linux/arch/arm/mach-w90x900/cpu.c
 *
 * Copyright (c) 2009 Nuvoton corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * NUC900 series cpu common support
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
#include <asm/system_misc.h>

#include <mach/hardware.h>
#include <mach/regs-serial.h>
#include <mach/regs-clock.h>
#include <mach/regs-ebi.h>
#include <mach/regs-timer.h>

#include "cpu.h"
#include "clock.h"
#include "nuc9xx.h"

/* Initial IO mappings */

static struct map_desc nuc900_iodesc[] __initdata = {
	IODESC_ENT(IRQ),
	IODESC_ENT(GCR),
	IODESC_ENT(UART),
	IODESC_ENT(TIMER),
	IODESC_ENT(EBI),
	IODESC_ENT(GPIO),
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
static DEFINE_CLK(g2d, 10);
static DEFINE_CLK(pwm, 18);
static DEFINE_CLK(ps2, 24);
static DEFINE_CLK(kpi, 25);
static DEFINE_CLK(wdt, 26);
static DEFINE_CLK(gdma, 27);
static DEFINE_CLK(adc, 28);
static DEFINE_CLK(usi, 29);
static DEFINE_CLK(ext, 0);
static DEFINE_CLK(timer0, 19);
static DEFINE_CLK(timer1, 20);
static DEFINE_CLK(timer2, 21);
static DEFINE_CLK(timer3, 22);
static DEFINE_CLK(timer4, 23);

static struct clk_lookup nuc900_clkregs[] = {
	DEF_CLKLOOK(&clk_lcd, "nuc900-lcd", NULL),
	DEF_CLKLOOK(&clk_audio, "nuc900-ac97", NULL),
	DEF_CLKLOOK(&clk_fmi, "nuc900-fmi", NULL),
	DEF_CLKLOOK(&clk_ms, "nuc900-fmi", "MS"),
	DEF_CLKLOOK(&clk_sd, "nuc900-fmi", "SD"),
	DEF_CLKLOOK(&clk_dmac, "nuc900-dmac", NULL),
	DEF_CLKLOOK(&clk_atapi, "nuc900-atapi", NULL),
	DEF_CLKLOOK(&clk_emc, "nuc900-emc", NULL),
	DEF_CLKLOOK(&clk_rmii, "nuc900-emc", "RMII"),
	DEF_CLKLOOK(&clk_usbd, "nuc900-usbd", NULL),
	DEF_CLKLOOK(&clk_usbh, "nuc900-usbh", NULL),
	DEF_CLKLOOK(&clk_g2d, "nuc900-g2d", NULL),
	DEF_CLKLOOK(&clk_pwm, "nuc900-pwm", NULL),
	DEF_CLKLOOK(&clk_ps2, "nuc900-ps2", NULL),
	DEF_CLKLOOK(&clk_kpi, "nuc900-kpi", NULL),
	DEF_CLKLOOK(&clk_wdt, "nuc900-wdt", NULL),
	DEF_CLKLOOK(&clk_gdma, "nuc900-gdma", NULL),
	DEF_CLKLOOK(&clk_adc, "nuc900-ts", NULL),
	DEF_CLKLOOK(&clk_usi, "nuc900-spi", NULL),
	DEF_CLKLOOK(&clk_ext, NULL, "ext"),
	DEF_CLKLOOK(&clk_timer0, NULL, "timer0"),
	DEF_CLKLOOK(&clk_timer1, NULL, "timer1"),
	DEF_CLKLOOK(&clk_timer2, NULL, "timer2"),
	DEF_CLKLOOK(&clk_timer3, NULL, "timer3"),
	DEF_CLKLOOK(&clk_timer4, NULL, "timer4"),
};

/* Initial serial platform data */

struct plat_serial8250_port nuc900_uart_data[] = {
	NUC900_8250PORT(UART0),
	{},
};

struct platform_device nuc900_serial_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= nuc900_uart_data,
	},
};

/*Set NUC900 series cpu frequence*/
static int __init nuc900_set_clkval(unsigned int cpufreq)
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
static int __init nuc900_set_cpufreq(char *str)
{
	unsigned long cpufreq, val;

	if (!*str)
		return 0;

	strict_strtoul(str, 0, &cpufreq);

	nuc900_clock_source(NULL, "ext");

	nuc900_set_clkval(cpufreq);

	mdelay(1);

	val = __raw_readl(REG_CKSKEW);
	val &= ~0xff;
	val |= DEFAULTSKEW;
	__raw_writel(val, REG_CKSKEW);

	nuc900_clock_source(NULL, "pll0");

	return 1;
}

__setup("cpufreq=", nuc900_set_cpufreq);

/*Init NUC900 evb io*/

void __init nuc900_map_io(struct map_desc *mach_desc, int mach_size)
{
	unsigned long idcode = 0x0;

	iotable_init(mach_desc, mach_size);
	iotable_init(nuc900_iodesc, ARRAY_SIZE(nuc900_iodesc));

	idcode = __raw_readl(NUC900PDID);
	if (idcode == NUC910_CPUID)
		printk(KERN_INFO "CPU type 0x%08lx is NUC910\n", idcode);
	else if (idcode == NUC920_CPUID)
		printk(KERN_INFO "CPU type 0x%08lx is NUC920\n", idcode);
	else if (idcode == NUC950_CPUID)
		printk(KERN_INFO "CPU type 0x%08lx is NUC950\n", idcode);
	else if (idcode == NUC960_CPUID)
		printk(KERN_INFO "CPU type 0x%08lx is NUC960\n", idcode);
}

/*Init NUC900 clock*/

void __init nuc900_init_clocks(void)
{
	clkdev_add_table(nuc900_clkregs, ARRAY_SIZE(nuc900_clkregs));
}

#define	WTCR	(TMR_BA + 0x1C)
#define	WTCLK	(1 << 10)
#define	WTE	(1 << 7)
#define	WTRE	(1 << 1)

void nuc9xx_restart(char mode, const char *cmd)
{
	if (mode == 's') {
		/* Jump into ROM at address 0 */
		soft_restart(0);
	} else {
		__raw_writel(WTE | WTRE | WTCLK, WTCR);
	}
}
