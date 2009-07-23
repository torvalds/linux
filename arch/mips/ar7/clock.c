/*
 * Copyright (C) 2007 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2007 Eugene Konev <ejka@openwrt.org>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/gcd.h>
#include <linux/io.h>

#include <asm/addrspace.h>
#include <asm/mach-ar7/ar7.h>

#define BOOT_PLL_SOURCE_MASK	0x3
#define CPU_PLL_SOURCE_SHIFT	16
#define BUS_PLL_SOURCE_SHIFT	14
#define USB_PLL_SOURCE_SHIFT	18
#define DSP_PLL_SOURCE_SHIFT	22
#define BOOT_PLL_SOURCE_AFE	0
#define BOOT_PLL_SOURCE_BUS	0
#define BOOT_PLL_SOURCE_REF	1
#define BOOT_PLL_SOURCE_XTAL	2
#define BOOT_PLL_SOURCE_CPU	3
#define BOOT_PLL_BYPASS		0x00000020
#define BOOT_PLL_ASYNC_MODE	0x02000000
#define BOOT_PLL_2TO1_MODE	0x00008000

#define TNETD7200_CLOCK_ID_CPU	0
#define TNETD7200_CLOCK_ID_DSP	1
#define TNETD7200_CLOCK_ID_USB	2

#define TNETD7200_DEF_CPU_CLK	211000000
#define TNETD7200_DEF_DSP_CLK	125000000
#define TNETD7200_DEF_USB_CLK	48000000

struct tnetd7300_clock {
	u32 ctrl;
#define PREDIV_MASK	0x001f0000
#define PREDIV_SHIFT	16
#define POSTDIV_MASK	0x0000001f
	u32 unused1[3];
	u32 pll;
#define MUL_MASK	0x0000f000
#define MUL_SHIFT	12
#define PLL_MODE_MASK	0x00000001
#define PLL_NDIV	0x00000800
#define PLL_DIV		0x00000002
#define PLL_STATUS	0x00000001
	u32 unused2[3];
};

struct tnetd7300_clocks {
	struct tnetd7300_clock bus;
	struct tnetd7300_clock cpu;
	struct tnetd7300_clock usb;
	struct tnetd7300_clock dsp;
};

struct tnetd7200_clock {
	u32 ctrl;
	u32 unused1[3];
#define DIVISOR_ENABLE_MASK 0x00008000
	u32 mul;
	u32 prediv;
	u32 postdiv;
	u32 postdiv2;
	u32 unused2[6];
	u32 cmd;
	u32 status;
	u32 cmden;
	u32 padding[15];
};

struct tnetd7200_clocks {
	struct tnetd7200_clock cpu;
	struct tnetd7200_clock dsp;
	struct tnetd7200_clock usb;
};

int ar7_cpu_clock = 150000000;
EXPORT_SYMBOL(ar7_cpu_clock);
int ar7_bus_clock = 125000000;
EXPORT_SYMBOL(ar7_bus_clock);
int ar7_dsp_clock;
EXPORT_SYMBOL(ar7_dsp_clock);

static void approximate(int base, int target, int *prediv,
			int *postdiv, int *mul)
{
	int i, j, k, freq, res = target;
	for (i = 1; i <= 16; i++)
		for (j = 1; j <= 32; j++)
			for (k = 1; k <= 32; k++) {
				freq = abs(base / j * i / k - target);
				if (freq < res) {
					res = freq;
					*mul = i;
					*prediv = j;
					*postdiv = k;
				}
			}
}

static void calculate(int base, int target, int *prediv, int *postdiv,
	int *mul)
{
	int tmp_gcd, tmp_base, tmp_freq;

	for (*prediv = 1; *prediv <= 32; (*prediv)++) {
		tmp_base = base / *prediv;
		tmp_gcd = gcd(target, tmp_base);
		*mul = target / tmp_gcd;
		*postdiv = tmp_base / tmp_gcd;
		if ((*mul < 1) || (*mul >= 16))
			continue;
		if ((*postdiv > 0) & (*postdiv <= 32))
			break;
	}

	if (base / *prediv * *mul / *postdiv != target) {
		approximate(base, target, prediv, postdiv, mul);
		tmp_freq = base / *prediv * *mul / *postdiv;
		printk(KERN_WARNING
		       "Adjusted requested frequency %d to %d\n",
		       target, tmp_freq);
	}

	printk(KERN_DEBUG "Clocks: prediv: %d, postdiv: %d, mul: %d\n",
	       *prediv, *postdiv, *mul);
}

static int tnetd7300_dsp_clock(void)
{
	u32 didr1, didr2;
	u8 rev = ar7_chip_rev();
	didr1 = readl((void *)KSEG1ADDR(AR7_REGS_GPIO + 0x18));
	didr2 = readl((void *)KSEG1ADDR(AR7_REGS_GPIO + 0x1c));
	if (didr2 & (1 << 23))
		return 0;
	if ((rev >= 0x23) && (rev != 0x57))
		return 250000000;
	if ((((didr2 & 0x1fff) << 10) | ((didr1 & 0xffc00000) >> 22))
	    > 4208000)
		return 250000000;
	return 0;
}

static int tnetd7300_get_clock(u32 shift, struct tnetd7300_clock *clock,
	u32 *bootcr, u32 bus_clock)
{
	int product;
	int base_clock = AR7_REF_CLOCK;
	u32 ctrl = readl(&clock->ctrl);
	u32 pll = readl(&clock->pll);
	int prediv = ((ctrl & PREDIV_MASK) >> PREDIV_SHIFT) + 1;
	int postdiv = (ctrl & POSTDIV_MASK) + 1;
	int divisor = prediv * postdiv;
	int mul = ((pll & MUL_MASK) >> MUL_SHIFT) + 1;

	switch ((*bootcr & (BOOT_PLL_SOURCE_MASK << shift)) >> shift) {
	case BOOT_PLL_SOURCE_BUS:
		base_clock = bus_clock;
		break;
	case BOOT_PLL_SOURCE_REF:
		base_clock = AR7_REF_CLOCK;
		break;
	case BOOT_PLL_SOURCE_XTAL:
		base_clock = AR7_XTAL_CLOCK;
		break;
	case BOOT_PLL_SOURCE_CPU:
		base_clock = ar7_cpu_clock;
		break;
	}

	if (*bootcr & BOOT_PLL_BYPASS)
		return base_clock / divisor;

	if ((pll & PLL_MODE_MASK) == 0)
		return (base_clock >> (mul / 16 + 1)) / divisor;

	if ((pll & (PLL_NDIV | PLL_DIV)) == (PLL_NDIV | PLL_DIV)) {
		product = (mul & 1) ?
			(base_clock * mul) >> 1 :
			(base_clock * (mul - 1)) >> 2;
		return product / divisor;
	}

	if (mul == 16)
		return base_clock / divisor;

	return base_clock * mul / divisor;
}

static void tnetd7300_set_clock(u32 shift, struct tnetd7300_clock *clock,
	u32 *bootcr, u32 frequency)
{
	int prediv, postdiv, mul;
	int base_clock = ar7_bus_clock;

	switch ((*bootcr & (BOOT_PLL_SOURCE_MASK << shift)) >> shift) {
	case BOOT_PLL_SOURCE_BUS:
		base_clock = ar7_bus_clock;
		break;
	case BOOT_PLL_SOURCE_REF:
		base_clock = AR7_REF_CLOCK;
		break;
	case BOOT_PLL_SOURCE_XTAL:
		base_clock = AR7_XTAL_CLOCK;
		break;
	case BOOT_PLL_SOURCE_CPU:
		base_clock = ar7_cpu_clock;
		break;
	}

	calculate(base_clock, frequency, &prediv, &postdiv, &mul);

	writel(((prediv - 1) << PREDIV_SHIFT) | (postdiv - 1), &clock->ctrl);
	msleep(1);
	writel(4, &clock->pll);
	while (readl(&clock->pll) & PLL_STATUS)
		;
	writel(((mul - 1) << MUL_SHIFT) | (0xff << 3) | 0x0e, &clock->pll);
	msleep(75);
}

static void __init tnetd7300_init_clocks(void)
{
	u32 *bootcr = (u32 *)ioremap_nocache(AR7_REGS_DCL, 4);
	struct tnetd7300_clocks *clocks =
					ioremap_nocache(UR8_REGS_CLOCKS,
					sizeof(struct tnetd7300_clocks));

	ar7_bus_clock = tnetd7300_get_clock(BUS_PLL_SOURCE_SHIFT,
		&clocks->bus, bootcr, AR7_AFE_CLOCK);

	if (*bootcr & BOOT_PLL_ASYNC_MODE)
		ar7_cpu_clock = tnetd7300_get_clock(CPU_PLL_SOURCE_SHIFT,
			&clocks->cpu, bootcr, AR7_AFE_CLOCK);
	else
		ar7_cpu_clock = ar7_bus_clock;

	if (ar7_dsp_clock == 250000000)
		tnetd7300_set_clock(DSP_PLL_SOURCE_SHIFT, &clocks->dsp,
			bootcr, ar7_dsp_clock);

	iounmap(clocks);
	iounmap(bootcr);
}

static int tnetd7200_get_clock(int base, struct tnetd7200_clock *clock,
	u32 *bootcr, u32 bus_clock)
{
	int divisor = ((readl(&clock->prediv) & 0x1f) + 1) *
		((readl(&clock->postdiv) & 0x1f) + 1);

	if (*bootcr & BOOT_PLL_BYPASS)
		return base / divisor;

	return base * ((readl(&clock->mul) & 0xf) + 1) / divisor;
}


static void tnetd7200_set_clock(int base, struct tnetd7200_clock *clock,
	int prediv, int postdiv, int postdiv2, int mul, u32 frequency)
{
	printk(KERN_INFO
		"Clocks: base = %d, frequency = %u, prediv = %d, "
		"postdiv = %d, postdiv2 = %d, mul = %d\n",
		base, frequency, prediv, postdiv, postdiv2, mul);

	writel(0, &clock->ctrl);
	writel(DIVISOR_ENABLE_MASK | ((prediv - 1) & 0x1F), &clock->prediv);
	writel((mul - 1) & 0xF, &clock->mul);

	while (readl(&clock->status) & 0x1)
		; /* nop */

	writel(DIVISOR_ENABLE_MASK | ((postdiv - 1) & 0x1F), &clock->postdiv);

	writel(readl(&clock->cmden) | 1, &clock->cmden);
	writel(readl(&clock->cmd) | 1, &clock->cmd);

	while (readl(&clock->status) & 0x1)
		; /* nop */

	writel(DIVISOR_ENABLE_MASK | ((postdiv2 - 1) & 0x1F), &clock->postdiv2);

	writel(readl(&clock->cmden) | 1, &clock->cmden);
	writel(readl(&clock->cmd) | 1, &clock->cmd);

	while (readl(&clock->status) & 0x1)
		; /* nop */

	writel(readl(&clock->ctrl) | 1, &clock->ctrl);
}

static int tnetd7200_get_clock_base(int clock_id, u32 *bootcr)
{
	if (*bootcr & BOOT_PLL_ASYNC_MODE)
		/* Async */
		switch (clock_id) {
		case TNETD7200_CLOCK_ID_DSP:
			return AR7_REF_CLOCK;
		default:
			return AR7_AFE_CLOCK;
		}
	else
		/* Sync */
		if (*bootcr & BOOT_PLL_2TO1_MODE)
			/* 2:1 */
			switch (clock_id) {
			case TNETD7200_CLOCK_ID_DSP:
				return AR7_REF_CLOCK;
			default:
				return AR7_AFE_CLOCK;
			}
		else
			/* 1:1 */
			return AR7_REF_CLOCK;
}


static void __init tnetd7200_init_clocks(void)
{
	u32 *bootcr = (u32 *)ioremap_nocache(AR7_REGS_DCL, 4);
	struct tnetd7200_clocks *clocks =
					ioremap_nocache(AR7_REGS_CLOCKS,
					sizeof(struct tnetd7200_clocks));
	int cpu_base, cpu_mul, cpu_prediv, cpu_postdiv;
	int dsp_base, dsp_mul, dsp_prediv, dsp_postdiv;
	int usb_base, usb_mul, usb_prediv, usb_postdiv;

	cpu_base = tnetd7200_get_clock_base(TNETD7200_CLOCK_ID_CPU, bootcr);
	dsp_base = tnetd7200_get_clock_base(TNETD7200_CLOCK_ID_DSP, bootcr);

	if (*bootcr & BOOT_PLL_ASYNC_MODE) {
		printk(KERN_INFO "Clocks: Async mode\n");

		printk(KERN_INFO "Clocks: Setting DSP clock\n");
		calculate(dsp_base, TNETD7200_DEF_DSP_CLK,
			&dsp_prediv, &dsp_postdiv, &dsp_mul);
		ar7_bus_clock =
			((dsp_base / dsp_prediv) * dsp_mul) / dsp_postdiv;
		tnetd7200_set_clock(dsp_base, &clocks->dsp,
			dsp_prediv, dsp_postdiv * 2, dsp_postdiv, dsp_mul * 2,
			ar7_bus_clock);

		printk(KERN_INFO "Clocks: Setting CPU clock\n");
		calculate(cpu_base, TNETD7200_DEF_CPU_CLK, &cpu_prediv,
			&cpu_postdiv, &cpu_mul);
		ar7_cpu_clock =
			((cpu_base / cpu_prediv) * cpu_mul) / cpu_postdiv;
		tnetd7200_set_clock(cpu_base, &clocks->cpu,
			cpu_prediv, cpu_postdiv, -1, cpu_mul,
			ar7_cpu_clock);

	} else
		if (*bootcr & BOOT_PLL_2TO1_MODE) {
			printk(KERN_INFO "Clocks: Sync 2:1 mode\n");

			printk(KERN_INFO "Clocks: Setting CPU clock\n");
			calculate(cpu_base, TNETD7200_DEF_CPU_CLK, &cpu_prediv,
				&cpu_postdiv, &cpu_mul);
			ar7_cpu_clock = ((cpu_base / cpu_prediv) * cpu_mul)
								/ cpu_postdiv;
			tnetd7200_set_clock(cpu_base, &clocks->cpu,
				cpu_prediv, cpu_postdiv, -1, cpu_mul,
				ar7_cpu_clock);

			printk(KERN_INFO "Clocks: Setting DSP clock\n");
			calculate(dsp_base, TNETD7200_DEF_DSP_CLK, &dsp_prediv,
				&dsp_postdiv, &dsp_mul);
			ar7_bus_clock = ar7_cpu_clock / 2;
			tnetd7200_set_clock(dsp_base, &clocks->dsp,
				dsp_prediv, dsp_postdiv * 2, dsp_postdiv,
				dsp_mul * 2, ar7_bus_clock);
		} else {
			printk(KERN_INFO "Clocks: Sync 1:1 mode\n");

			printk(KERN_INFO "Clocks: Setting DSP clock\n");
			calculate(dsp_base, TNETD7200_DEF_DSP_CLK, &dsp_prediv,
				&dsp_postdiv, &dsp_mul);
			ar7_bus_clock = ((dsp_base / dsp_prediv) * dsp_mul)
								/ dsp_postdiv;
			tnetd7200_set_clock(dsp_base, &clocks->dsp,
				dsp_prediv, dsp_postdiv * 2, dsp_postdiv,
				dsp_mul * 2, ar7_bus_clock);

			ar7_cpu_clock = ar7_bus_clock;
		}

	printk(KERN_INFO "Clocks: Setting USB clock\n");
	usb_base = ar7_bus_clock;
	calculate(usb_base, TNETD7200_DEF_USB_CLK, &usb_prediv,
		&usb_postdiv, &usb_mul);
	tnetd7200_set_clock(usb_base, &clocks->usb,
		usb_prediv, usb_postdiv, -1, usb_mul,
		TNETD7200_DEF_USB_CLK);

	ar7_dsp_clock = ar7_cpu_clock;

	iounmap(clocks);
	iounmap(bootcr);
}

int __init ar7_init_clocks(void)
{
	switch (ar7_chip_id()) {
	case AR7_CHIP_7100:
	case AR7_CHIP_7200:
		tnetd7200_init_clocks();
		break;
	case AR7_CHIP_7300:
		ar7_dsp_clock = tnetd7300_dsp_clock();
		tnetd7300_init_clocks();
		break;
	default:
		break;
	}

	return 0;
}
arch_initcall(ar7_init_clocks);
