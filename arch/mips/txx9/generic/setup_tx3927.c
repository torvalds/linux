/*
 * TX3927 setup routines
 * Based on linux/arch/mips/txx9/jmr3927/setup.c
 *
 * Copyright 2001 MontaVista Software Inc.
 * Copyright (C) 2000-2001 Toshiba Corporation
 * Copyright (C) 2007 Ralf Baechle (ralf@linux-mips.org)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/param.h>
#include <linux/io.h>
#include <asm/mipsregs.h>
#include <asm/txx9irq.h>
#include <asm/txx9tmr.h>
#include <asm/txx9pio.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/tx3927.h>

void __init tx3927_wdt_init(void)
{
	txx9_wdt_init(TX3927_TMR_REG(2));
}

void __init tx3927_setup(void)
{
	int i;
	unsigned int conf;

	/* don't enable - see errata */
	txx9_ccfg_toeon = 0;
	if (strstr(prom_getcmdline(), "toeon") != NULL)
		txx9_ccfg_toeon = 1;

	txx9_reg_res_init(TX3927_REV_PCODE(), TX3927_REG_BASE,
			  TX3927_REG_SIZE);

	/* SDRAMC,ROMC are configured by PROM */
	for (i = 0; i < 8; i++) {
		if (!(tx3927_romcptr->cr[i] & 0x8))
			continue;	/* disabled */
		txx9_ce_res[i].start = (unsigned long)TX3927_ROMC_BA(i);
		txx9_ce_res[i].end =
			txx9_ce_res[i].start + TX3927_ROMC_SIZE(i) - 1;
		request_resource(&iomem_resource, &txx9_ce_res[i]);
	}

	/* clocks */
	txx9_gbus_clock = txx9_cpu_clock / 2;
	/* change default value to udelay/mdelay take reasonable time */
	loops_per_jiffy = txx9_cpu_clock / HZ / 2;

	/* CCFG */
	/* enable Timeout BusError */
	if (txx9_ccfg_toeon)
		tx3927_ccfgptr->ccfg |= TX3927_CCFG_TOE;

	/* clear BusErrorOnWrite flag */
	tx3927_ccfgptr->ccfg &= ~TX3927_CCFG_BEOW;
	if (read_c0_conf() & TX39_CONF_WBON)
		/* Disable PCI snoop */
		tx3927_ccfgptr->ccfg &= ~TX3927_CCFG_PSNP;
	else
		/* Enable PCI SNOOP - with write through only */
		tx3927_ccfgptr->ccfg |= TX3927_CCFG_PSNP;
	/* do reset on watchdog */
	tx3927_ccfgptr->ccfg |= TX3927_CCFG_WR;

	printk(KERN_INFO "TX3927 -- CRIR:%08lx CCFG:%08lx PCFG:%08lx\n",
	       tx3927_ccfgptr->crir,
	       tx3927_ccfgptr->ccfg, tx3927_ccfgptr->pcfg);

	/* TMR */
	for (i = 0; i < TX3927_NR_TMR; i++)
		txx9_tmr_init(TX3927_TMR_REG(i));

	/* DMA */
	tx3927_dmaptr->mcr = 0;
	for (i = 0; i < ARRAY_SIZE(tx3927_dmaptr->ch); i++) {
		/* reset channel */
		tx3927_dmaptr->ch[i].ccr = TX3927_DMA_CCR_CHRST;
		tx3927_dmaptr->ch[i].ccr = 0;
	}
	/* enable DMA */
#ifdef __BIG_ENDIAN
	tx3927_dmaptr->mcr = TX3927_DMA_MCR_MSTEN;
#else
	tx3927_dmaptr->mcr = TX3927_DMA_MCR_MSTEN | TX3927_DMA_MCR_LE;
#endif

	/* PIO */
	__raw_writel(0, &tx3927_pioptr->maskcpu);
	__raw_writel(0, &tx3927_pioptr->maskext);
	txx9_gpio_init(TX3927_PIO_REG, 0, 16);

	conf = read_c0_conf();
	if (conf & TX39_CONF_DCE) {
		if (!(conf & TX39_CONF_WBON))
			pr_info("TX3927 D-Cache WriteThrough.\n");
		else if (!(conf & TX39_CONF_CWFON))
			pr_info("TX3927 D-Cache WriteBack.\n");
		else
			pr_info("TX3927 D-Cache WriteBack (CWF) .\n");
	}
}

void __init tx3927_time_init(unsigned int evt_tmrnr, unsigned int src_tmrnr)
{
	txx9_clockevent_init(TX3927_TMR_REG(evt_tmrnr),
			     TXX9_IRQ_BASE + TX3927_IR_TMR(evt_tmrnr),
			     TXX9_IMCLK);
	txx9_clocksource_init(TX3927_TMR_REG(src_tmrnr), TXX9_IMCLK);
}

void __init tx3927_sio_init(unsigned int sclk, unsigned int cts_mask)
{
	int i;

	for (i = 0; i < 2; i++)
		txx9_sio_init(TX3927_SIO_REG(i),
			      TXX9_IRQ_BASE + TX3927_IR_SIO(i),
			      i, sclk, (1 << i) & cts_mask);
}
