/*
 * TX4939 setup routines
 * Based on linux/arch/mips/txx9/generic/setup_tx4938.c,
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
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/device.h>
#include <linux/ethtool.h>
#include <linux/param.h>
#include <linux/ptrace.h>
#include <linux/mtd/physmap.h>
#include <linux/platform_device.h>
#include <asm/bootinfo.h>
#include <asm/reboot.h>
#include <asm/traps.h>
#include <asm/txx9irq.h>
#include <asm/txx9tmr.h>
#include <asm/txx9/generic.h>
#include <asm/txx9/ndfmc.h>
#include <asm/txx9/dmac.h>
#include <asm/txx9/tx4939.h>

static void __init tx4939_wdr_init(void)
{
	/* report watchdog reset status */
	if (____raw_readq(&tx4939_ccfgptr->ccfg) & TX4939_CCFG_WDRST)
		pr_warn("Watchdog reset detected at 0x%lx\n",
			read_c0_errorepc());
	/* clear WatchDogReset (W1C) */
	tx4939_ccfg_set(TX4939_CCFG_WDRST);
	/* do reset on watchdog */
	tx4939_ccfg_set(TX4939_CCFG_WR);
}

void __init tx4939_wdt_init(void)
{
	txx9_wdt_init(TX4939_TMR_REG(2) & 0xfffffffffULL);
}

static void tx4939_machine_restart(char *command)
{
	local_irq_disable();
	pr_emerg("Rebooting (with %s watchdog reset)...\n",
		 (____raw_readq(&tx4939_ccfgptr->ccfg) & TX4939_CCFG_WDREXEN) ?
		 "external" : "internal");
	/* clear watchdog status */
	tx4939_ccfg_set(TX4939_CCFG_WDRST);	/* W1C */
	txx9_wdt_now(TX4939_TMR_REG(2) & 0xfffffffffULL);
	while (!(____raw_readq(&tx4939_ccfgptr->ccfg) & TX4939_CCFG_WDRST))
		;
	mdelay(10);
	if (____raw_readq(&tx4939_ccfgptr->ccfg) & TX4939_CCFG_WDREXEN) {
		pr_emerg("Rebooting (with internal watchdog reset)...\n");
		/* External WDRST failed.  Do internal watchdog reset */
		tx4939_ccfg_clear(TX4939_CCFG_WDREXEN);
	}
	/* fallback */
	(*_machine_halt)();
}

void show_registers(struct pt_regs *regs);
static int tx4939_be_handler(struct pt_regs *regs, int is_fixup)
{
	int data = regs->cp0_cause & 4;
	console_verbose();
	pr_err("%cBE exception at %#lx\n",
	       data ? 'D' : 'I', regs->cp0_epc);
	pr_err("ccfg:%llx, toea:%llx\n",
	       (unsigned long long)____raw_readq(&tx4939_ccfgptr->ccfg),
	       (unsigned long long)____raw_readq(&tx4939_ccfgptr->toea));
#ifdef CONFIG_PCI
	tx4927_report_pcic_status();
#endif
	show_registers(regs);
	panic("BusError!");
}
static void __init tx4939_be_init(void)
{
	board_be_handler = tx4939_be_handler;
}

static struct resource tx4939_sdram_resource[4];
static struct resource tx4939_sram_resource;
#define TX4939_SRAM_SIZE 0x800

void __init tx4939_add_memory_regions(void)
{
	int i;
	unsigned long start, size;
	u64 win;

	for (i = 0; i < 4; i++) {
		if (!((__u32)____raw_readq(&tx4939_ddrcptr->winen) & (1 << i)))
			continue;
		win = ____raw_readq(&tx4939_ddrcptr->win[i]);
		start = (unsigned long)(win >> 48);
		size = (((unsigned long)(win >> 32) & 0xffff) + 1) - start;
		add_memory_region(start << 20, size << 20, BOOT_MEM_RAM);
	}
}

void __init tx4939_setup(void)
{
	int i;
	__u32 divmode;
	__u64 pcfg;
	unsigned int cpuclk = 0;

	txx9_reg_res_init(TX4939_REV_PCODE(), TX4939_REG_BASE,
			  TX4939_REG_SIZE);
	set_c0_config(TX49_CONF_CWFON);

	/* SDRAMC,EBUSC are configured by PROM */
	for (i = 0; i < 4; i++) {
		if (!(TX4939_EBUSC_CR(i) & 0x8))
			continue;	/* disabled */
		txx9_ce_res[i].start = (unsigned long)TX4939_EBUSC_BA(i);
		txx9_ce_res[i].end =
			txx9_ce_res[i].start + TX4939_EBUSC_SIZE(i) - 1;
		request_resource(&iomem_resource, &txx9_ce_res[i]);
	}

	/* clocks */
	if (txx9_master_clock) {
		/* calculate cpu_clock from master_clock */
		divmode = (__u32)____raw_readq(&tx4939_ccfgptr->ccfg) &
			TX4939_CCFG_MULCLK_MASK;
		cpuclk = txx9_master_clock * 20 / 2;
		switch (divmode) {
		case TX4939_CCFG_MULCLK_8:
			cpuclk = cpuclk / 3 * 4 /* / 6 *  8 */; break;
		case TX4939_CCFG_MULCLK_9:
			cpuclk = cpuclk / 2 * 3 /* / 6 *  9 */; break;
		case TX4939_CCFG_MULCLK_10:
			cpuclk = cpuclk / 3 * 5 /* / 6 * 10 */; break;
		case TX4939_CCFG_MULCLK_11:
			cpuclk = cpuclk / 6 * 11; break;
		case TX4939_CCFG_MULCLK_12:
			cpuclk = cpuclk * 2 /* / 6 * 12 */; break;
		case TX4939_CCFG_MULCLK_13:
			cpuclk = cpuclk / 6 * 13; break;
		case TX4939_CCFG_MULCLK_14:
			cpuclk = cpuclk / 3 * 7 /* / 6 * 14 */; break;
		case TX4939_CCFG_MULCLK_15:
			cpuclk = cpuclk / 2 * 5 /* / 6 * 15 */; break;
		}
		txx9_cpu_clock = cpuclk;
	} else {
		if (txx9_cpu_clock == 0)
			txx9_cpu_clock = 400000000;	/* 400MHz */
		/* calculate master_clock from cpu_clock */
		cpuclk = txx9_cpu_clock;
		divmode = (__u32)____raw_readq(&tx4939_ccfgptr->ccfg) &
			TX4939_CCFG_MULCLK_MASK;
		switch (divmode) {
		case TX4939_CCFG_MULCLK_8:
			txx9_master_clock = cpuclk * 6 / 8; break;
		case TX4939_CCFG_MULCLK_9:
			txx9_master_clock = cpuclk * 6 / 9; break;
		case TX4939_CCFG_MULCLK_10:
			txx9_master_clock = cpuclk * 6 / 10; break;
		case TX4939_CCFG_MULCLK_11:
			txx9_master_clock = cpuclk * 6 / 11; break;
		case TX4939_CCFG_MULCLK_12:
			txx9_master_clock = cpuclk * 6 / 12; break;
		case TX4939_CCFG_MULCLK_13:
			txx9_master_clock = cpuclk * 6 / 13; break;
		case TX4939_CCFG_MULCLK_14:
			txx9_master_clock = cpuclk * 6 / 14; break;
		case TX4939_CCFG_MULCLK_15:
			txx9_master_clock = cpuclk * 6 / 15; break;
		}
		txx9_master_clock /= 10; /* * 2 / 20 */
	}
	/* calculate gbus_clock from cpu_clock */
	divmode = (__u32)____raw_readq(&tx4939_ccfgptr->ccfg) &
		TX4939_CCFG_YDIVMODE_MASK;
	txx9_gbus_clock = txx9_cpu_clock;
	switch (divmode) {
	case TX4939_CCFG_YDIVMODE_2:
		txx9_gbus_clock /= 2; break;
	case TX4939_CCFG_YDIVMODE_3:
		txx9_gbus_clock /= 3; break;
	case TX4939_CCFG_YDIVMODE_5:
		txx9_gbus_clock /= 5; break;
	case TX4939_CCFG_YDIVMODE_6:
		txx9_gbus_clock /= 6; break;
	}
	/* change default value to udelay/mdelay take reasonable time */
	loops_per_jiffy = txx9_cpu_clock / HZ / 2;

	/* CCFG */
	tx4939_wdr_init();
	/* clear BusErrorOnWrite flag (W1C) */
	tx4939_ccfg_set(TX4939_CCFG_WDRST | TX4939_CCFG_BEOW);
	/* enable Timeout BusError */
	if (txx9_ccfg_toeon)
		tx4939_ccfg_set(TX4939_CCFG_TOE);

	/* DMA selection */
	txx9_clear64(&tx4939_ccfgptr->pcfg, TX4939_PCFG_DMASEL_ALL);

	/* Use external clock for external arbiter */
	if (!(____raw_readq(&tx4939_ccfgptr->ccfg) & TX4939_CCFG_PCIARB))
		txx9_clear64(&tx4939_ccfgptr->pcfg, TX4939_PCFG_PCICLKEN_ALL);

	pr_info("%s -- %dMHz(M%dMHz,G%dMHz) CRIR:%08x CCFG:%llx PCFG:%llx\n",
		txx9_pcode_str,
		(cpuclk + 500000) / 1000000,
		(txx9_master_clock + 500000) / 1000000,
		(txx9_gbus_clock + 500000) / 1000000,
		(__u32)____raw_readq(&tx4939_ccfgptr->crir),
		(unsigned long long)____raw_readq(&tx4939_ccfgptr->ccfg),
		(unsigned long long)____raw_readq(&tx4939_ccfgptr->pcfg));

	pr_info("%s DDRC -- EN:%08x", txx9_pcode_str,
		(__u32)____raw_readq(&tx4939_ddrcptr->winen));
	for (i = 0; i < 4; i++) {
		__u64 win = ____raw_readq(&tx4939_ddrcptr->win[i]);
		if (!((__u32)____raw_readq(&tx4939_ddrcptr->winen) & (1 << i)))
			continue;	/* disabled */
		printk(KERN_CONT " #%d:%016llx", i, (unsigned long long)win);
		tx4939_sdram_resource[i].name = "DDR SDRAM";
		tx4939_sdram_resource[i].start =
			(unsigned long)(win >> 48) << 20;
		tx4939_sdram_resource[i].end =
			((((unsigned long)(win >> 32) & 0xffff) + 1) <<
			 20) - 1;
		tx4939_sdram_resource[i].flags = IORESOURCE_MEM;
		request_resource(&iomem_resource, &tx4939_sdram_resource[i]);
	}
	printk(KERN_CONT "\n");

	/* SRAM */
	if (____raw_readq(&tx4939_sramcptr->cr) & 1) {
		unsigned int size = TX4939_SRAM_SIZE;
		tx4939_sram_resource.name = "SRAM";
		tx4939_sram_resource.start =
			(____raw_readq(&tx4939_sramcptr->cr) >> (39-11))
			& ~(size - 1);
		tx4939_sram_resource.end =
			tx4939_sram_resource.start + TX4939_SRAM_SIZE - 1;
		tx4939_sram_resource.flags = IORESOURCE_MEM;
		request_resource(&iomem_resource, &tx4939_sram_resource);
	}

	/* TMR */
	/* disable all timers */
	for (i = 0; i < TX4939_NR_TMR; i++)
		txx9_tmr_init(TX4939_TMR_REG(i) & 0xfffffffffULL);

	/* set PCIC1 reset (required to prevent hangup on BIST) */
	txx9_set64(&tx4939_ccfgptr->clkctr, TX4939_CLKCTR_PCI1RST);
	pcfg = ____raw_readq(&tx4939_ccfgptr->pcfg);
	if (pcfg & (TX4939_PCFG_ET0MODE | TX4939_PCFG_ET1MODE)) {
		mdelay(1);	/* at least 128 cpu clock */
		/* clear PCIC1 reset */
		txx9_clear64(&tx4939_ccfgptr->clkctr, TX4939_CLKCTR_PCI1RST);
	} else {
		pr_info("%s: stop PCIC1\n", txx9_pcode_str);
		/* stop PCIC1 */
		txx9_set64(&tx4939_ccfgptr->clkctr, TX4939_CLKCTR_PCI1CKD);
	}
	if (!(pcfg & TX4939_PCFG_ET0MODE)) {
		pr_info("%s: stop ETH0\n", txx9_pcode_str);
		txx9_set64(&tx4939_ccfgptr->clkctr, TX4939_CLKCTR_ETH0RST);
		txx9_set64(&tx4939_ccfgptr->clkctr, TX4939_CLKCTR_ETH0CKD);
	}
	if (!(pcfg & TX4939_PCFG_ET1MODE)) {
		pr_info("%s: stop ETH1\n", txx9_pcode_str);
		txx9_set64(&tx4939_ccfgptr->clkctr, TX4939_CLKCTR_ETH1RST);
		txx9_set64(&tx4939_ccfgptr->clkctr, TX4939_CLKCTR_ETH1CKD);
	}

	_machine_restart = tx4939_machine_restart;
	board_be_init = tx4939_be_init;
}

void __init tx4939_time_init(unsigned int tmrnr)
{
	if (____raw_readq(&tx4939_ccfgptr->ccfg) & TX4939_CCFG_TINTDIS)
		txx9_clockevent_init(TX4939_TMR_REG(tmrnr) & 0xfffffffffULL,
				     TXX9_IRQ_BASE + TX4939_IR_TMR(tmrnr),
				     TXX9_IMCLK);
}

void __init tx4939_sio_init(unsigned int sclk, unsigned int cts_mask)
{
	int i;
	unsigned int ch_mask = 0;
	__u64 pcfg = __raw_readq(&tx4939_ccfgptr->pcfg);

	cts_mask |= ~1; /* only SIO0 have RTS/CTS */
	if ((pcfg & TX4939_PCFG_SIO2MODE_MASK) != TX4939_PCFG_SIO2MODE_SIO0)
		cts_mask |= 1 << 0; /* disable SIO0 RTS/CTS by PCFG setting */
	if ((pcfg & TX4939_PCFG_SIO2MODE_MASK) != TX4939_PCFG_SIO2MODE_SIO2)
		ch_mask |= 1 << 2; /* disable SIO2 by PCFG setting */
	if (pcfg & TX4939_PCFG_SIO3MODE)
		ch_mask |= 1 << 3; /* disable SIO3 by PCFG setting */
	for (i = 0; i < 4; i++) {
		if ((1 << i) & ch_mask)
			continue;
		txx9_sio_init(TX4939_SIO_REG(i) & 0xfffffffffULL,
			      TXX9_IRQ_BASE + TX4939_IR_SIO(i),
			      i, sclk, (1 << i) & cts_mask);
	}
}

#if IS_ENABLED(CONFIG_TC35815)
static u32 tx4939_get_eth_speed(struct net_device *dev)
{
	struct ethtool_cmd cmd;
	if (__ethtool_get_settings(dev, &cmd))
		return 100;	/* default 100Mbps */

	return ethtool_cmd_speed(&cmd);
}

static int tx4939_netdev_event(struct notifier_block *this,
			       unsigned long event,
			       void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);

	if (event == NETDEV_CHANGE && netif_carrier_ok(dev)) {
		__u64 bit = 0;
		if (dev->irq == TXX9_IRQ_BASE + TX4939_IR_ETH(0))
			bit = TX4939_PCFG_SPEED0;
		else if (dev->irq == TXX9_IRQ_BASE + TX4939_IR_ETH(1))
			bit = TX4939_PCFG_SPEED1;
		if (bit) {
			if (tx4939_get_eth_speed(dev) == 100)
				txx9_set64(&tx4939_ccfgptr->pcfg, bit);
			else
				txx9_clear64(&tx4939_ccfgptr->pcfg, bit);
		}
	}
	return NOTIFY_DONE;
}

static struct notifier_block tx4939_netdev_notifier = {
	.notifier_call = tx4939_netdev_event,
	.priority = 1,
};

void __init tx4939_ethaddr_init(unsigned char *addr0, unsigned char *addr1)
{
	u64 pcfg = __raw_readq(&tx4939_ccfgptr->pcfg);

	if (addr0 && (pcfg & TX4939_PCFG_ET0MODE))
		txx9_ethaddr_init(TXX9_IRQ_BASE + TX4939_IR_ETH(0), addr0);
	if (addr1 && (pcfg & TX4939_PCFG_ET1MODE))
		txx9_ethaddr_init(TXX9_IRQ_BASE + TX4939_IR_ETH(1), addr1);
	register_netdevice_notifier(&tx4939_netdev_notifier);
}
#else
void __init tx4939_ethaddr_init(unsigned char *addr0, unsigned char *addr1)
{
}
#endif

void __init tx4939_mtd_init(int ch)
{
	struct physmap_flash_data pdata = {
		.width = TX4939_EBUSC_WIDTH(ch) / 8,
	};
	unsigned long start = txx9_ce_res[ch].start;
	unsigned long size = txx9_ce_res[ch].end - start + 1;

	if (!(TX4939_EBUSC_CR(ch) & 0x8))
		return; /* disabled */
	txx9_physmap_flash_init(ch, start, size, &pdata);
}

#define TX4939_ATA_REG_PHYS(ch) (TX4939_ATA_REG(ch) & 0xfffffffffULL)
void __init tx4939_ata_init(void)
{
	static struct resource ata0_res[] = {
		{
			.start = TX4939_ATA_REG_PHYS(0),
			.end = TX4939_ATA_REG_PHYS(0) + 0x1000 - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = TXX9_IRQ_BASE + TX4939_IR_ATA(0),
			.flags = IORESOURCE_IRQ,
		},
	};
	static struct resource ata1_res[] = {
		{
			.start = TX4939_ATA_REG_PHYS(1),
			.end = TX4939_ATA_REG_PHYS(1) + 0x1000 - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = TXX9_IRQ_BASE + TX4939_IR_ATA(1),
			.flags = IORESOURCE_IRQ,
		},
	};
	static struct platform_device ata0_dev = {
		.name = "tx4939ide",
		.id = 0,
		.num_resources = ARRAY_SIZE(ata0_res),
		.resource = ata0_res,
	};
	static struct platform_device ata1_dev = {
		.name = "tx4939ide",
		.id = 1,
		.num_resources = ARRAY_SIZE(ata1_res),
		.resource = ata1_res,
	};
	__u64 pcfg = __raw_readq(&tx4939_ccfgptr->pcfg);

	if (pcfg & TX4939_PCFG_ATA0MODE)
		platform_device_register(&ata0_dev);
	if ((pcfg & (TX4939_PCFG_ATA1MODE |
		     TX4939_PCFG_ET1MODE |
		     TX4939_PCFG_ET0MODE)) == TX4939_PCFG_ATA1MODE)
		platform_device_register(&ata1_dev);
}

void __init tx4939_rtc_init(void)
{
	static struct resource res[] = {
		{
			.start = TX4939_RTC_REG & 0xfffffffffULL,
			.end = (TX4939_RTC_REG & 0xfffffffffULL) + 0x100 - 1,
			.flags = IORESOURCE_MEM,
		}, {
			.start = TXX9_IRQ_BASE + TX4939_IR_RTC,
			.flags = IORESOURCE_IRQ,
		},
	};
	static struct platform_device rtc_dev = {
		.name = "tx4939rtc",
		.id = -1,
		.num_resources = ARRAY_SIZE(res),
		.resource = res,
	};

	platform_device_register(&rtc_dev);
}

void __init tx4939_ndfmc_init(unsigned int hold, unsigned int spw,
			      unsigned char ch_mask, unsigned char wide_mask)
{
	struct txx9ndfmc_platform_data plat_data = {
		.shift = 1,
		.gbus_clock = txx9_gbus_clock,
		.hold = hold,
		.spw = spw,
		.flags = NDFMC_PLAT_FLAG_NO_RSTR | NDFMC_PLAT_FLAG_HOLDADD |
			 NDFMC_PLAT_FLAG_DUMMYWRITE,
		.ch_mask = ch_mask,
		.wide_mask = wide_mask,
	};
	txx9_ndfmc_init(TX4939_NDFMC_REG & 0xfffffffffULL, &plat_data);
}

void __init tx4939_dmac_init(int memcpy_chan0, int memcpy_chan1)
{
	struct txx9dmac_platform_data plat_data = {
		.have_64bit_regs = true,
	};
	int i;

	for (i = 0; i < 2; i++) {
		plat_data.memcpy_chan = i ? memcpy_chan1 : memcpy_chan0;
		txx9_dmac_init(i, TX4939_DMA_REG(i) & 0xfffffffffULL,
			       TXX9_IRQ_BASE + TX4939_IR_DMA(i, 0),
			       &plat_data);
	}
}

void __init tx4939_aclc_init(void)
{
	u64 pcfg = __raw_readq(&tx4939_ccfgptr->pcfg);

	if ((pcfg & TX4939_PCFG_I2SMODE_MASK) == TX4939_PCFG_I2SMODE_ACLC)
		txx9_aclc_init(TX4939_ACLC_REG & 0xfffffffffULL,
			       TXX9_IRQ_BASE + TX4939_IR_ACLC, 1, 0, 1);
}

void __init tx4939_sramc_init(void)
{
	if (tx4939_sram_resource.start)
		txx9_sramc_init(&tx4939_sram_resource);
}

void __init tx4939_rng_init(void)
{
	static struct resource res = {
		.start = TX4939_RNG_REG & 0xfffffffffULL,
		.end = (TX4939_RNG_REG & 0xfffffffffULL) + 0x30 - 1,
		.flags = IORESOURCE_MEM,
	};
	static struct platform_device pdev = {
		.name = "tx4939-rng",
		.id = -1,
		.num_resources = 1,
		.resource = &res,
	};

	platform_device_register(&pdev);
}

static void __init tx4939_stop_unused_modules(void)
{
	__u64 pcfg, rst = 0, ckd = 0;
	char buf[128];

	buf[0] = '\0';
	local_irq_disable();
	pcfg = ____raw_readq(&tx4939_ccfgptr->pcfg);
	if ((pcfg & TX4939_PCFG_I2SMODE_MASK) !=
	    TX4939_PCFG_I2SMODE_ACLC) {
		rst |= TX4939_CLKCTR_ACLRST;
		ckd |= TX4939_CLKCTR_ACLCKD;
		strcat(buf, " ACLC");
	}
	if ((pcfg & TX4939_PCFG_I2SMODE_MASK) !=
	    TX4939_PCFG_I2SMODE_I2S &&
	    (pcfg & TX4939_PCFG_I2SMODE_MASK) !=
	    TX4939_PCFG_I2SMODE_I2S_ALT) {
		rst |= TX4939_CLKCTR_I2SRST;
		ckd |= TX4939_CLKCTR_I2SCKD;
		strcat(buf, " I2S");
	}
	if (!(pcfg & TX4939_PCFG_ATA0MODE)) {
		rst |= TX4939_CLKCTR_ATA0RST;
		ckd |= TX4939_CLKCTR_ATA0CKD;
		strcat(buf, " ATA0");
	}
	if (!(pcfg & TX4939_PCFG_ATA1MODE)) {
		rst |= TX4939_CLKCTR_ATA1RST;
		ckd |= TX4939_CLKCTR_ATA1CKD;
		strcat(buf, " ATA1");
	}
	if (pcfg & TX4939_PCFG_SPIMODE) {
		rst |= TX4939_CLKCTR_SPIRST;
		ckd |= TX4939_CLKCTR_SPICKD;
		strcat(buf, " SPI");
	}
	if (!(pcfg & (TX4939_PCFG_VSSMODE | TX4939_PCFG_VPSMODE))) {
		rst |= TX4939_CLKCTR_VPCRST;
		ckd |= TX4939_CLKCTR_VPCCKD;
		strcat(buf, " VPC");
	}
	if ((pcfg & TX4939_PCFG_SIO2MODE_MASK) != TX4939_PCFG_SIO2MODE_SIO2) {
		rst |= TX4939_CLKCTR_SIO2RST;
		ckd |= TX4939_CLKCTR_SIO2CKD;
		strcat(buf, " SIO2");
	}
	if (pcfg & TX4939_PCFG_SIO3MODE) {
		rst |= TX4939_CLKCTR_SIO3RST;
		ckd |= TX4939_CLKCTR_SIO3CKD;
		strcat(buf, " SIO3");
	}
	if (rst | ckd) {
		txx9_set64(&tx4939_ccfgptr->clkctr, rst);
		txx9_set64(&tx4939_ccfgptr->clkctr, ckd);
	}
	local_irq_enable();
	if (buf[0])
		pr_info("%s: stop%s\n", txx9_pcode_str, buf);
}

static int __init tx4939_late_init(void)
{
	if (txx9_pcode != 0x4939)
		return -ENODEV;
	tx4939_stop_unused_modules();
	return 0;
}
late_initcall(tx4939_late_init);
