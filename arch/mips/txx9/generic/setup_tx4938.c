/*
 * TX4938/4937 setup routines
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
#include <linux/mtd/physmap.h>
#include <asm/txx9irq.h>
#include <asm/txx9tmr.h>
#include <asm/txx9pio.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/tx4938.h>

static void __init tx4938_wdr_init(void)
{
	/* clear WatchDogReset (W1C) */
	tx4938_ccfg_set(TX4938_CCFG_WDRST);
	/* do reset on watchdog */
	tx4938_ccfg_set(TX4938_CCFG_WR);
}

void __init tx4938_wdt_init(void)
{
	txx9_wdt_init(TX4938_TMR_REG(2) & 0xfffffffffULL);
}

static struct resource tx4938_sdram_resource[4];
static struct resource tx4938_sram_resource;

#define TX4938_SRAM_SIZE 0x800

void __init tx4938_setup(void)
{
	int i;
	__u32 divmode;
	int cpuclk = 0;
	u64 ccfg;

	txx9_reg_res_init(TX4938_REV_PCODE(), TX4938_REG_BASE,
			  TX4938_REG_SIZE);
	set_c0_config(TX49_CONF_CWFON);

	/* SDRAMC,EBUSC are configured by PROM */
	for (i = 0; i < 8; i++) {
		if (!(TX4938_EBUSC_CR(i) & 0x8))
			continue;	/* disabled */
		txx9_ce_res[i].start = (unsigned long)TX4938_EBUSC_BA(i);
		txx9_ce_res[i].end =
			txx9_ce_res[i].start + TX4938_EBUSC_SIZE(i) - 1;
		request_resource(&iomem_resource, &txx9_ce_res[i]);
	}

	/* clocks */
	ccfg = ____raw_readq(&tx4938_ccfgptr->ccfg);
	if (txx9_master_clock) {
		/* calculate gbus_clock and cpu_clock from master_clock */
		divmode = (__u32)ccfg & TX4938_CCFG_DIVMODE_MASK;
		switch (divmode) {
		case TX4938_CCFG_DIVMODE_8:
		case TX4938_CCFG_DIVMODE_10:
		case TX4938_CCFG_DIVMODE_12:
		case TX4938_CCFG_DIVMODE_16:
		case TX4938_CCFG_DIVMODE_18:
			txx9_gbus_clock = txx9_master_clock * 4; break;
		default:
			txx9_gbus_clock = txx9_master_clock;
		}
		switch (divmode) {
		case TX4938_CCFG_DIVMODE_2:
		case TX4938_CCFG_DIVMODE_8:
			cpuclk = txx9_gbus_clock * 2; break;
		case TX4938_CCFG_DIVMODE_2_5:
		case TX4938_CCFG_DIVMODE_10:
			cpuclk = txx9_gbus_clock * 5 / 2; break;
		case TX4938_CCFG_DIVMODE_3:
		case TX4938_CCFG_DIVMODE_12:
			cpuclk = txx9_gbus_clock * 3; break;
		case TX4938_CCFG_DIVMODE_4:
		case TX4938_CCFG_DIVMODE_16:
			cpuclk = txx9_gbus_clock * 4; break;
		case TX4938_CCFG_DIVMODE_4_5:
		case TX4938_CCFG_DIVMODE_18:
			cpuclk = txx9_gbus_clock * 9 / 2; break;
		}
		txx9_cpu_clock = cpuclk;
	} else {
		if (txx9_cpu_clock == 0)
			txx9_cpu_clock = 300000000;	/* 300MHz */
		/* calculate gbus_clock and master_clock from cpu_clock */
		cpuclk = txx9_cpu_clock;
		divmode = (__u32)ccfg & TX4938_CCFG_DIVMODE_MASK;
		switch (divmode) {
		case TX4938_CCFG_DIVMODE_2:
		case TX4938_CCFG_DIVMODE_8:
			txx9_gbus_clock = cpuclk / 2; break;
		case TX4938_CCFG_DIVMODE_2_5:
		case TX4938_CCFG_DIVMODE_10:
			txx9_gbus_clock = cpuclk * 2 / 5; break;
		case TX4938_CCFG_DIVMODE_3:
		case TX4938_CCFG_DIVMODE_12:
			txx9_gbus_clock = cpuclk / 3; break;
		case TX4938_CCFG_DIVMODE_4:
		case TX4938_CCFG_DIVMODE_16:
			txx9_gbus_clock = cpuclk / 4; break;
		case TX4938_CCFG_DIVMODE_4_5:
		case TX4938_CCFG_DIVMODE_18:
			txx9_gbus_clock = cpuclk * 2 / 9; break;
		}
		switch (divmode) {
		case TX4938_CCFG_DIVMODE_8:
		case TX4938_CCFG_DIVMODE_10:
		case TX4938_CCFG_DIVMODE_12:
		case TX4938_CCFG_DIVMODE_16:
		case TX4938_CCFG_DIVMODE_18:
			txx9_master_clock = txx9_gbus_clock / 4; break;
		default:
			txx9_master_clock = txx9_gbus_clock;
		}
	}
	/* change default value to udelay/mdelay take reasonable time */
	loops_per_jiffy = txx9_cpu_clock / HZ / 2;

	/* CCFG */
	tx4938_wdr_init();
	/* clear BusErrorOnWrite flag (W1C) */
	tx4938_ccfg_set(TX4938_CCFG_BEOW);
	/* enable Timeout BusError */
	if (txx9_ccfg_toeon)
		tx4938_ccfg_set(TX4938_CCFG_TOE);

	/* DMA selection */
	txx9_clear64(&tx4938_ccfgptr->pcfg, TX4938_PCFG_DMASEL_ALL);

	/* Use external clock for external arbiter */
	if (!(____raw_readq(&tx4938_ccfgptr->ccfg) & TX4938_CCFG_PCIARB))
		txx9_clear64(&tx4938_ccfgptr->pcfg, TX4938_PCFG_PCICLKEN_ALL);

	printk(KERN_INFO "%s -- %dMHz(M%dMHz) CRIR:%08x CCFG:%llx PCFG:%llx\n",
	       txx9_pcode_str,
	       (cpuclk + 500000) / 1000000,
	       (txx9_master_clock + 500000) / 1000000,
	       (__u32)____raw_readq(&tx4938_ccfgptr->crir),
	       (unsigned long long)____raw_readq(&tx4938_ccfgptr->ccfg),
	       (unsigned long long)____raw_readq(&tx4938_ccfgptr->pcfg));

	printk(KERN_INFO "%s SDRAMC --", txx9_pcode_str);
	for (i = 0; i < 4; i++) {
		__u64 cr = TX4938_SDRAMC_CR(i);
		unsigned long base, size;
		if (!((__u32)cr & 0x00000400))
			continue;	/* disabled */
		base = (unsigned long)(cr >> 49) << 21;
		size = (((unsigned long)(cr >> 33) & 0x7fff) + 1) << 21;
		printk(" CR%d:%016llx", i, (unsigned long long)cr);
		tx4938_sdram_resource[i].name = "SDRAM";
		tx4938_sdram_resource[i].start = base;
		tx4938_sdram_resource[i].end = base + size - 1;
		tx4938_sdram_resource[i].flags = IORESOURCE_MEM;
		request_resource(&iomem_resource, &tx4938_sdram_resource[i]);
	}
	printk(" TR:%09llx\n",
	       (unsigned long long)____raw_readq(&tx4938_sdramcptr->tr));

	/* SRAM */
	if (txx9_pcode == 0x4938 && ____raw_readq(&tx4938_sramcptr->cr) & 1) {
		unsigned int size = TX4938_SRAM_SIZE;
		tx4938_sram_resource.name = "SRAM";
		tx4938_sram_resource.start =
			(____raw_readq(&tx4938_sramcptr->cr) >> (39-11))
			& ~(size - 1);
		tx4938_sram_resource.end =
			tx4938_sram_resource.start + TX4938_SRAM_SIZE - 1;
		tx4938_sram_resource.flags = IORESOURCE_MEM;
		request_resource(&iomem_resource, &tx4938_sram_resource);
	}

	/* TMR */
	/* disable all timers */
	for (i = 0; i < TX4938_NR_TMR; i++)
		txx9_tmr_init(TX4938_TMR_REG(i) & 0xfffffffffULL);

	/* DMA */
	for (i = 0; i < 2; i++)
		____raw_writeq(TX4938_DMA_MCR_MSTEN,
			       (void __iomem *)(TX4938_DMA_REG(i) + 0x50));

	/* PIO */
	txx9_gpio_init(TX4938_PIO_REG & 0xfffffffffULL, 0, TX4938_NUM_PIO);
	__raw_writel(0, &tx4938_pioptr->maskcpu);
	__raw_writel(0, &tx4938_pioptr->maskext);

	if (txx9_pcode == 0x4938) {
		__u64 pcfg = ____raw_readq(&tx4938_ccfgptr->pcfg);
		/* set PCIC1 reset */
		txx9_set64(&tx4938_ccfgptr->clkctr, TX4938_CLKCTR_PCIC1RST);
		if (pcfg & (TX4938_PCFG_ETH0_SEL | TX4938_PCFG_ETH1_SEL)) {
			mdelay(1);	/* at least 128 cpu clock */
			/* clear PCIC1 reset */
			txx9_clear64(&tx4938_ccfgptr->clkctr,
				     TX4938_CLKCTR_PCIC1RST);
		} else {
			printk(KERN_INFO "%s: stop PCIC1\n", txx9_pcode_str);
			/* stop PCIC1 */
			txx9_set64(&tx4938_ccfgptr->clkctr,
				   TX4938_CLKCTR_PCIC1CKD);
		}
		if (!(pcfg & TX4938_PCFG_ETH0_SEL)) {
			printk(KERN_INFO "%s: stop ETH0\n", txx9_pcode_str);
			txx9_set64(&tx4938_ccfgptr->clkctr,
				   TX4938_CLKCTR_ETH0RST);
			txx9_set64(&tx4938_ccfgptr->clkctr,
				   TX4938_CLKCTR_ETH0CKD);
		}
		if (!(pcfg & TX4938_PCFG_ETH1_SEL)) {
			printk(KERN_INFO "%s: stop ETH1\n", txx9_pcode_str);
			txx9_set64(&tx4938_ccfgptr->clkctr,
				   TX4938_CLKCTR_ETH1RST);
			txx9_set64(&tx4938_ccfgptr->clkctr,
				   TX4938_CLKCTR_ETH1CKD);
		}
	}
}

void __init tx4938_time_init(unsigned int tmrnr)
{
	if (____raw_readq(&tx4938_ccfgptr->ccfg) & TX4938_CCFG_TINTDIS)
		txx9_clockevent_init(TX4938_TMR_REG(tmrnr) & 0xfffffffffULL,
				     TXX9_IRQ_BASE + TX4938_IR_TMR(tmrnr),
				     TXX9_IMCLK);
}

void __init tx4938_sio_init(unsigned int sclk, unsigned int cts_mask)
{
	int i;
	unsigned int ch_mask = 0;

	if (__raw_readq(&tx4938_ccfgptr->pcfg) & TX4938_PCFG_ETH0_SEL)
		ch_mask |= 1 << 1; /* disable SIO1 by PCFG setting */
	for (i = 0; i < 2; i++) {
		if ((1 << i) & ch_mask)
			continue;
		txx9_sio_init(TX4938_SIO_REG(i) & 0xfffffffffULL,
			      TXX9_IRQ_BASE + TX4938_IR_SIO(i),
			      i, sclk, (1 << i) & cts_mask);
	}
}

void __init tx4938_spi_init(int busid)
{
	txx9_spi_init(busid, TX4938_SPI_REG & 0xfffffffffULL,
		      TXX9_IRQ_BASE + TX4938_IR_SPI);
}

void __init tx4938_ethaddr_init(unsigned char *addr0, unsigned char *addr1)
{
	u64 pcfg = __raw_readq(&tx4938_ccfgptr->pcfg);

	if (addr0 && (pcfg & TX4938_PCFG_ETH0_SEL))
		txx9_ethaddr_init(TXX9_IRQ_BASE + TX4938_IR_ETH0, addr0);
	if (addr1 && (pcfg & TX4938_PCFG_ETH1_SEL))
		txx9_ethaddr_init(TXX9_IRQ_BASE + TX4938_IR_ETH1, addr1);
}

void __init tx4938_mtd_init(int ch)
{
	struct physmap_flash_data pdata = {
		.width = TX4938_EBUSC_WIDTH(ch) / 8,
	};
	unsigned long start = txx9_ce_res[ch].start;
	unsigned long size = txx9_ce_res[ch].end - start + 1;

	if (!(TX4938_EBUSC_CR(ch) & 0x8))
		return;	/* disabled */
	txx9_physmap_flash_init(ch, start, size, &pdata);
}
