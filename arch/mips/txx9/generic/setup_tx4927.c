/*
 * TX4927 setup routines
 * Based on linux/arch/mips/txx9/rbtx4938/setup.c,
 *	    and RBTX49xx patch from CELF patch archive.
 *
 * 2003-2005 (c) MontaVista Software, Inc.
 * (C) Copyright TOSHIBA CORPORATION 2000-2001, 2004-2007
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/param.h>
#include <asm/txx9irq.h>
#include <asm/txx9tmr.h>
#include <asm/txx9pio.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/tx4927.h>

static void __init tx4927_wdr_init(void)
{
	/* clear WatchDogReset (W1C) */
	tx4927_ccfg_set(TX4927_CCFG_WDRST);
	/* do reset on watchdog */
	tx4927_ccfg_set(TX4927_CCFG_WR);
}

void __init tx4927_wdt_init(void)
{
	txx9_wdt_init(TX4927_TMR_REG(2) & 0xfffffffffULL);
}

static struct resource tx4927_sdram_resource[4];

void __init tx4927_setup(void)
{
	int i;
	__u32 divmode;
	int cpuclk = 0;
	u64 ccfg;

	txx9_reg_res_init(TX4927_REV_PCODE(), TX4927_REG_BASE,
			  TX4927_REG_SIZE);
	set_c0_config(TX49_CONF_CWFON);

	/* SDRAMC,EBUSC are configured by PROM */
	for (i = 0; i < 8; i++) {
		if (!(TX4927_EBUSC_CR(i) & 0x8))
			continue;	/* disabled */
		txx9_ce_res[i].start = (unsigned long)TX4927_EBUSC_BA(i);
		txx9_ce_res[i].end =
			txx9_ce_res[i].start + TX4927_EBUSC_SIZE(i) - 1;
		request_resource(&iomem_resource, &txx9_ce_res[i]);
	}

	/* clocks */
	ccfg = ____raw_readq(&tx4927_ccfgptr->ccfg);
	if (txx9_master_clock) {
		/* calculate gbus_clock and cpu_clock from master_clock */
		divmode = (__u32)ccfg & TX4927_CCFG_DIVMODE_MASK;
		switch (divmode) {
		case TX4927_CCFG_DIVMODE_8:
		case TX4927_CCFG_DIVMODE_10:
		case TX4927_CCFG_DIVMODE_12:
		case TX4927_CCFG_DIVMODE_16:
			txx9_gbus_clock = txx9_master_clock * 4; break;
		default:
			txx9_gbus_clock = txx9_master_clock;
		}
		switch (divmode) {
		case TX4927_CCFG_DIVMODE_2:
		case TX4927_CCFG_DIVMODE_8:
			cpuclk = txx9_gbus_clock * 2; break;
		case TX4927_CCFG_DIVMODE_2_5:
		case TX4927_CCFG_DIVMODE_10:
			cpuclk = txx9_gbus_clock * 5 / 2; break;
		case TX4927_CCFG_DIVMODE_3:
		case TX4927_CCFG_DIVMODE_12:
			cpuclk = txx9_gbus_clock * 3; break;
		case TX4927_CCFG_DIVMODE_4:
		case TX4927_CCFG_DIVMODE_16:
			cpuclk = txx9_gbus_clock * 4; break;
		}
		txx9_cpu_clock = cpuclk;
	} else {
		if (txx9_cpu_clock == 0)
			txx9_cpu_clock = 200000000;	/* 200MHz */
		/* calculate gbus_clock and master_clock from cpu_clock */
		cpuclk = txx9_cpu_clock;
		divmode = (__u32)ccfg & TX4927_CCFG_DIVMODE_MASK;
		switch (divmode) {
		case TX4927_CCFG_DIVMODE_2:
		case TX4927_CCFG_DIVMODE_8:
			txx9_gbus_clock = cpuclk / 2; break;
		case TX4927_CCFG_DIVMODE_2_5:
		case TX4927_CCFG_DIVMODE_10:
			txx9_gbus_clock = cpuclk * 2 / 5; break;
		case TX4927_CCFG_DIVMODE_3:
		case TX4927_CCFG_DIVMODE_12:
			txx9_gbus_clock = cpuclk / 3; break;
		case TX4927_CCFG_DIVMODE_4:
		case TX4927_CCFG_DIVMODE_16:
			txx9_gbus_clock = cpuclk / 4; break;
		}
		switch (divmode) {
		case TX4927_CCFG_DIVMODE_8:
		case TX4927_CCFG_DIVMODE_10:
		case TX4927_CCFG_DIVMODE_12:
		case TX4927_CCFG_DIVMODE_16:
			txx9_master_clock = txx9_gbus_clock / 4; break;
		default:
			txx9_master_clock = txx9_gbus_clock;
		}
	}
	/* change default value to udelay/mdelay take reasonable time */
	loops_per_jiffy = txx9_cpu_clock / HZ / 2;

	/* CCFG */
	tx4927_wdr_init();
	/* clear BusErrorOnWrite flag (W1C) */
	tx4927_ccfg_set(TX4927_CCFG_BEOW);
	/* enable Timeout BusError */
	if (txx9_ccfg_toeon)
		tx4927_ccfg_set(TX4927_CCFG_TOE);

	/* DMA selection */
	txx9_clear64(&tx4927_ccfgptr->pcfg, TX4927_PCFG_DMASEL_ALL);

	/* Use external clock for external arbiter */
	if (!(____raw_readq(&tx4927_ccfgptr->ccfg) & TX4927_CCFG_PCIARB))
		txx9_clear64(&tx4927_ccfgptr->pcfg, TX4927_PCFG_PCICLKEN_ALL);

	printk(KERN_INFO "%s -- %dMHz(M%dMHz) CRIR:%08x CCFG:%llx PCFG:%llx\n",
	       txx9_pcode_str,
	       (cpuclk + 500000) / 1000000,
	       (txx9_master_clock + 500000) / 1000000,
	       (__u32)____raw_readq(&tx4927_ccfgptr->crir),
	       (unsigned long long)____raw_readq(&tx4927_ccfgptr->ccfg),
	       (unsigned long long)____raw_readq(&tx4927_ccfgptr->pcfg));

	printk(KERN_INFO "%s SDRAMC --", txx9_pcode_str);
	for (i = 0; i < 4; i++) {
		__u64 cr = TX4927_SDRAMC_CR(i);
		unsigned long base, size;
		if (!((__u32)cr & 0x00000400))
			continue;	/* disabled */
		base = (unsigned long)(cr >> 49) << 21;
		size = (((unsigned long)(cr >> 33) & 0x7fff) + 1) << 21;
		printk(" CR%d:%016llx", i, (unsigned long long)cr);
		tx4927_sdram_resource[i].name = "SDRAM";
		tx4927_sdram_resource[i].start = base;
		tx4927_sdram_resource[i].end = base + size - 1;
		tx4927_sdram_resource[i].flags = IORESOURCE_MEM;
		request_resource(&iomem_resource, &tx4927_sdram_resource[i]);
	}
	printk(" TR:%09llx\n",
	       (unsigned long long)____raw_readq(&tx4927_sdramcptr->tr));

	/* TMR */
	/* disable all timers */
	for (i = 0; i < TX4927_NR_TMR; i++)
		txx9_tmr_init(TX4927_TMR_REG(i) & 0xfffffffffULL);

	/* PIO */
	txx9_gpio_init(TX4927_PIO_REG & 0xfffffffffULL, 0, TX4927_NUM_PIO);
	__raw_writel(0, &tx4927_pioptr->maskcpu);
	__raw_writel(0, &tx4927_pioptr->maskext);
}

void __init tx4927_time_init(unsigned int tmrnr)
{
	if (____raw_readq(&tx4927_ccfgptr->ccfg) & TX4927_CCFG_TINTDIS)
		txx9_clockevent_init(TX4927_TMR_REG(tmrnr) & 0xfffffffffULL,
				     TXX9_IRQ_BASE + TX4927_IR_TMR(tmrnr),
				     TXX9_IMCLK);
}

void __init tx4927_sio_init(unsigned int sclk, unsigned int cts_mask)
{
	int i;

	for (i = 0; i < 2; i++)
		txx9_sio_init(TX4927_SIO_REG(i) & 0xfffffffffULL,
			      TXX9_IRQ_BASE + TX4927_IR_SIO(i),
			      i, sclk, (1 << i) & cts_mask);
}
