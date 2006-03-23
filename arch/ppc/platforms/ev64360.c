/*
 * Board setup routines for the Marvell EV-64360-BP Evaluation Board.
 *
 * Author: Lee Nicks <allinux@gmail.com>
 *
 * Based on code done by Rabeeh Khoury - rabeeh@galileo.co.il
 * Based on code done by - Mark A. Greer <mgreer@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/console.h>
#include <linux/initrd.h>
#include <linux/root_dev.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/bootmem.h>
#include <linux/mtd/physmap.h>
#include <linux/mv643xx.h>
#include <linux/platform_device.h>
#ifdef CONFIG_BOOTIMG
#include <linux/bootimg.h>
#endif
#include <asm/page.h>
#include <asm/time.h>
#include <asm/smp.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>
#include <asm/ppcboot.h>
#include <asm/mv64x60.h>
#include <asm/machdep.h>
#include <platforms/ev64360.h>

#define BOARD_VENDOR    "Marvell"
#define BOARD_MACHINE   "EV-64360-BP"

static struct		mv64x60_handle bh;
static void __iomem	*sram_base;

static u32		ev64360_flash_size_0;
static u32		ev64360_flash_size_1;

static u32		ev64360_bus_frequency;

unsigned char	__res[sizeof(bd_t)];

TODC_ALLOC();

static int __init
ev64360_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	return 0;
}

static void __init
ev64360_setup_bridge(void)
{
	struct mv64x60_setup_info si;
	int i;

	memset(&si, 0, sizeof(si));

	si.phys_reg_base = CONFIG_MV64X60_NEW_BASE;

	#ifdef CONFIG_PCI
	si.pci_1.enable_bus = 1;
	si.pci_1.pci_io.cpu_base = EV64360_PCI1_IO_START_PROC_ADDR;
	si.pci_1.pci_io.pci_base_hi = 0;
	si.pci_1.pci_io.pci_base_lo = EV64360_PCI1_IO_START_PCI_ADDR;
	si.pci_1.pci_io.size = EV64360_PCI1_IO_SIZE;
	si.pci_1.pci_io.swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_1.pci_mem[0].cpu_base = EV64360_PCI1_MEM_START_PROC_ADDR;
	si.pci_1.pci_mem[0].pci_base_hi = EV64360_PCI1_MEM_START_PCI_HI_ADDR;
	si.pci_1.pci_mem[0].pci_base_lo = EV64360_PCI1_MEM_START_PCI_LO_ADDR;
	si.pci_1.pci_mem[0].size = EV64360_PCI1_MEM_SIZE;
	si.pci_1.pci_mem[0].swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_1.pci_cmd_bits = 0;
	si.pci_1.latency_timer = 0x80;
	#else
	si.pci_0.enable_bus = 0;
	si.pci_1.enable_bus = 0;
	#endif

	for (i = 0; i < MV64x60_CPU2MEM_WINDOWS; i++) {
#if defined(CONFIG_NOT_COHERENT_CACHE)
		si.cpu_prot_options[i] = 0;
		si.enet_options[i] = MV64360_ENET2MEM_SNOOP_NONE;
		si.mpsc_options[i] = MV64360_MPSC2MEM_SNOOP_NONE;
		si.idma_options[i] = MV64360_IDMA2MEM_SNOOP_NONE;

		si.pci_1.acc_cntl_options[i] =
		    MV64360_PCI_ACC_CNTL_SNOOP_NONE |
		    MV64360_PCI_ACC_CNTL_SWAP_NONE |
		    MV64360_PCI_ACC_CNTL_MBURST_128_BYTES |
		    MV64360_PCI_ACC_CNTL_RDSIZE_256_BYTES;
#else
		si.cpu_prot_options[i] = 0;
		si.enet_options[i] = MV64360_ENET2MEM_SNOOP_NONE; /* errata */
		si.mpsc_options[i] = MV64360_MPSC2MEM_SNOOP_NONE; /* errata */
		si.idma_options[i] = MV64360_IDMA2MEM_SNOOP_NONE; /* errata */

		si.pci_1.acc_cntl_options[i] =
		    MV64360_PCI_ACC_CNTL_SNOOP_WB |
		    MV64360_PCI_ACC_CNTL_SWAP_NONE |
		    MV64360_PCI_ACC_CNTL_MBURST_32_BYTES |
		    MV64360_PCI_ACC_CNTL_RDSIZE_32_BYTES;
#endif
	}

	if (mv64x60_init(&bh, &si))
		printk(KERN_WARNING "Bridge initialization failed.\n");

	#ifdef CONFIG_PCI
	pci_dram_offset = 0; /* sys mem at same addr on PCI & cpu bus */
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = ev64360_map_irq;
	ppc_md.pci_exclude_device = mv64x60_pci_exclude_device;

	mv64x60_set_bus(&bh, 1, 0);
	bh.hose_b->first_busno = 0;
	bh.hose_b->last_busno = 0xff;
	#endif
}

/* Bridge & platform setup routines */
void __init
ev64360_intr_setup(void)
{
	/* MPP 8, 9, and 10 */
	mv64x60_clr_bits(&bh, MV64x60_MPP_CNTL_1, 0xfff);

	/*
	 * Define GPP 8,9,and 10 interrupt polarity as active low
	 * input signal and level triggered
	 */
	mv64x60_set_bits(&bh, MV64x60_GPP_LEVEL_CNTL, 0x700);
	mv64x60_clr_bits(&bh, MV64x60_GPP_IO_CNTL, 0x700);

	/* Config GPP intr ctlr to respond to level trigger */
	mv64x60_set_bits(&bh, MV64x60_COMM_ARBITER_CNTL, (1<<10));

	/* Erranum FEr PCI-#8 */
	mv64x60_clr_bits(&bh, MV64x60_PCI0_CMD, (1<<5) | (1<<9));
	mv64x60_clr_bits(&bh, MV64x60_PCI1_CMD, (1<<5) | (1<<9));

	/*
	 * Dismiss and then enable interrupt on GPP interrupt cause
	 * for CPU #0
	 */
	mv64x60_write(&bh, MV64x60_GPP_INTR_CAUSE, ~0x700);
	mv64x60_set_bits(&bh, MV64x60_GPP_INTR_MASK, 0x700);

	/*
	 * Dismiss and then enable interrupt on CPU #0 high cause reg
	 * BIT25 summarizes GPP interrupts 8-15
	 */
	mv64x60_set_bits(&bh, MV64360_IC_CPU0_INTR_MASK_HI, (1<<25));
}

void __init
ev64360_setup_peripherals(void)
{
	u32 base;

	/* Set up window for boot CS */
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2BOOT_WIN,
		 EV64360_BOOT_WINDOW_BASE, EV64360_BOOT_WINDOW_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2BOOT_WIN);

	/* We only use the 32-bit flash */
	mv64x60_get_32bit_window(&bh, MV64x60_CPU2BOOT_WIN, &base,
		&ev64360_flash_size_0);
	ev64360_flash_size_1 = 0;

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_1_WIN,
		 EV64360_RTC_WINDOW_BASE, EV64360_RTC_WINDOW_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_1_WIN);

	TODC_INIT(TODC_TYPE_DS1501, 0, 0,
		ioremap(EV64360_RTC_WINDOW_BASE, EV64360_RTC_WINDOW_SIZE), 8);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2SRAM_WIN,
		 EV64360_INTERNAL_SRAM_BASE, MV64360_SRAM_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2SRAM_WIN);
	sram_base = ioremap(EV64360_INTERNAL_SRAM_BASE, MV64360_SRAM_SIZE);

	/* Set up Enet->SRAM window */
	mv64x60_set_32bit_window(&bh, MV64x60_ENET2MEM_4_WIN,
		EV64360_INTERNAL_SRAM_BASE, MV64360_SRAM_SIZE, 0x2);
	bh.ci->enable_window_32bit(&bh, MV64x60_ENET2MEM_4_WIN);

	/* Give enet r/w access to memory region */
	mv64x60_set_bits(&bh, MV64360_ENET2MEM_ACC_PROT_0, (0x3 << (4 << 1)));
	mv64x60_set_bits(&bh, MV64360_ENET2MEM_ACC_PROT_1, (0x3 << (4 << 1)));
	mv64x60_set_bits(&bh, MV64360_ENET2MEM_ACC_PROT_2, (0x3 << (4 << 1)));

	mv64x60_clr_bits(&bh, MV64x60_PCI1_PCI_DECODE_CNTL, (1 << 3));
	mv64x60_clr_bits(&bh, MV64x60_TIMR_CNTR_0_3_CNTL,
			 ((1 << 0) | (1 << 8) | (1 << 16) | (1 << 24)));

#if defined(CONFIG_NOT_COHERENT_CACHE)
	mv64x60_write(&bh, MV64360_SRAM_CONFIG, 0x00160000);
#else
	mv64x60_write(&bh, MV64360_SRAM_CONFIG, 0x001600b2);
#endif

	/*
	 * Setting the SRAM to 0. Note that this generates parity errors on
	 * internal data path in SRAM since it's first time accessing it
	 * while after reset it's not configured.
	 */
	memset(sram_base, 0, MV64360_SRAM_SIZE);

	/* set up PCI interrupt controller */
	ev64360_intr_setup();
}

static void __init
ev64360_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("ev64360_setup_arch: enter", 0);

	set_tb(0, 0);

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef   CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_SDA2;
#endif

	/*
	 * Set up the L2CR register.
	 */
	_set_L2CR(L2CR_L2E | L2CR_L2PE);

	if (ppc_md.progress)
		ppc_md.progress("ev64360_setup_arch: calling setup_bridge", 0);

	ev64360_setup_bridge();
	ev64360_setup_peripherals();
	ev64360_bus_frequency = ev64360_bus_freq();

	printk(KERN_INFO "%s %s port (C) 2005 Lee Nicks "
		"(allinux@gmail.com)\n", BOARD_VENDOR, BOARD_MACHINE);
	if (ppc_md.progress)
		ppc_md.progress("ev64360_setup_arch: exit", 0);
}

/* Platform device data fixup routines. */
#if defined(CONFIG_SERIAL_MPSC)
static void __init
ev64360_fixup_mpsc_pdata(struct platform_device *pdev)
{
	struct mpsc_pdata *pdata;

	pdata = (struct mpsc_pdata *)pdev->dev.platform_data;

	pdata->max_idle = 40;
	pdata->default_baud = EV64360_DEFAULT_BAUD;
	pdata->brg_clk_src = EV64360_MPSC_CLK_SRC;
	/*
	 * TCLK (not SysCLk) is routed to BRG, then to the MPSC.  On most parts,
	 * TCLK == SysCLK but on 64460, they are separate pins.
	 * SysCLK can go up to 200 MHz but TCLK can only go up to 133 MHz.
	 */
	pdata->brg_clk_freq = min(ev64360_bus_frequency, MV64x60_TCLK_FREQ_MAX);
}
#endif

#if defined(CONFIG_MV643XX_ETH)
static void __init
ev64360_fixup_eth_pdata(struct platform_device *pdev)
{
	struct mv643xx_eth_platform_data *eth_pd;
	static u16 phy_addr[] = {
		EV64360_ETH0_PHY_ADDR,
		EV64360_ETH1_PHY_ADDR,
		EV64360_ETH2_PHY_ADDR,
	};

	eth_pd = pdev->dev.platform_data;
	eth_pd->force_phy_addr = 1;
	eth_pd->phy_addr = phy_addr[pdev->id];
	eth_pd->tx_queue_size = EV64360_ETH_TX_QUEUE_SIZE;
	eth_pd->rx_queue_size = EV64360_ETH_RX_QUEUE_SIZE;
}
#endif

static int
ev64360_platform_notify(struct device *dev)
{
	static struct {
		char	*bus_id;
		void	((*rtn)(struct platform_device *pdev));
	} dev_map[] = {
#if defined(CONFIG_SERIAL_MPSC)
		{ MPSC_CTLR_NAME ".0", ev64360_fixup_mpsc_pdata },
		{ MPSC_CTLR_NAME ".1", ev64360_fixup_mpsc_pdata },
#endif
#if defined(CONFIG_MV643XX_ETH)
		{ MV643XX_ETH_NAME ".0", ev64360_fixup_eth_pdata },
		{ MV643XX_ETH_NAME ".1", ev64360_fixup_eth_pdata },
		{ MV643XX_ETH_NAME ".2", ev64360_fixup_eth_pdata },
#endif
	};
	struct platform_device	*pdev;
	int	i;

	if (dev && dev->bus_id)
		for (i=0; i<ARRAY_SIZE(dev_map); i++)
			if (!strncmp(dev->bus_id, dev_map[i].bus_id,
				BUS_ID_SIZE)) {

				pdev = container_of(dev,
					struct platform_device, dev);
				dev_map[i].rtn(pdev);
			}

	return 0;
}

#ifdef CONFIG_MTD_PHYSMAP

#ifndef MB
#define MB	(1 << 20)
#endif

/*
 * MTD Layout.
 *
 * FLASH Amount:	0xff000000 - 0xffffffff
 * -------------	-----------------------
 * Reserved:		0xff000000 - 0xff03ffff
 * JFFS2 file system:	0xff040000 - 0xffefffff
 * U-boot:		0xfff00000 - 0xffffffff
 */
static int __init
ev64360_setup_mtd(void)
{
	u32	size;
	int	ptbl_entries;
	static struct mtd_partition	*ptbl;

	size = ev64360_flash_size_0 + ev64360_flash_size_1;
	if (!size)
		return -ENOMEM;

	ptbl_entries = 3;

	if ((ptbl = kmalloc(ptbl_entries * sizeof(struct mtd_partition),
		GFP_KERNEL)) == NULL) {

		printk(KERN_WARNING "Can't alloc MTD partition table\n");
		return -ENOMEM;
	}
	memset(ptbl, 0, ptbl_entries * sizeof(struct mtd_partition));

	ptbl[0].name = "reserved";
	ptbl[0].offset = 0;
	ptbl[0].size = EV64360_MTD_RESERVED_SIZE;
	ptbl[1].name = "jffs2";
	ptbl[1].offset = EV64360_MTD_RESERVED_SIZE;
	ptbl[1].size = EV64360_MTD_JFFS2_SIZE;
	ptbl[2].name = "U-BOOT";
	ptbl[2].offset = EV64360_MTD_RESERVED_SIZE + EV64360_MTD_JFFS2_SIZE;
	ptbl[2].size = EV64360_MTD_UBOOT_SIZE;

	physmap_map.size = size;
	physmap_set_partitions(ptbl, ptbl_entries);
	return 0;
}

arch_initcall(ev64360_setup_mtd);
#endif

static void
ev64360_restart(char *cmd)
{
	ulong	i = 0xffffffff;
	volatile unsigned char * rtc_base = ioremap(EV64360_RTC_WINDOW_BASE,0x4000);

	/* issue hard reset */
	rtc_base[0xf] = 0x80;
	rtc_base[0xc] = 0x00;
	rtc_base[0xd] = 0x01;
	rtc_base[0xf] = 0x83;

	while (i-- > 0) ;
	panic("restart failed\n");
}

static void
ev64360_halt(void)
{
	while (1) ;
	/* NOTREACHED */
}

static void
ev64360_power_off(void)
{
	ev64360_halt();
	/* NOTREACHED */
}

static int
ev64360_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: " BOARD_VENDOR "\n");
	seq_printf(m, "machine\t\t: " BOARD_MACHINE "\n");
	seq_printf(m, "bus speed\t: %dMHz\n", ev64360_bus_frequency/1000/1000);

	return 0;
}

static void __init
ev64360_calibrate_decr(void)
{
	u32 freq;

	freq = ev64360_bus_frequency / 4;

	printk(KERN_INFO "time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       (long)freq / 1000000, (long)freq % 1000000);

	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);
}

unsigned long __init
ev64360_find_end_of_memory(void)
{
	return mv64x60_get_mem_size(CONFIG_MV64X60_NEW_BASE,
		MV64x60_TYPE_MV64360);
}

static inline void
ev64360_set_bat(void)
{
	mb();
	mtspr(SPRN_DBAT2U, 0xf0001ffe);
	mtspr(SPRN_DBAT2L, 0xf000002a);
	mb();
}

#if defined(CONFIG_SERIAL_TEXT_DEBUG) && defined(CONFIG_SERIAL_MPSC_CONSOLE)
static void __init
ev64360_map_io(void)
{
	io_block_mapping(CONFIG_MV64X60_NEW_BASE, \
			 CONFIG_MV64X60_NEW_BASE, \
			 0x00020000, _PAGE_IO);
}
#endif

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	/* ASSUMPTION:  If both r3 (bd_t pointer) and r6 (cmdline pointer)
	 * are non-zero, then we should use the board info from the bd_t
	 * structure and the cmdline pointed to by r6 instead of the
	 * information from birecs, if any.  Otherwise, use the information
	 * from birecs as discovered by the preceeding call to
	 * parse_bootinfo().  This rule should work with both PPCBoot, which
	 * uses a bd_t board info structure, and the kernel boot wrapper,
	 * which uses birecs.
	 */
	if (r3 && r6) {
		/* copy board info structure */
		memcpy( (void *)__res,(void *)(r3+KERNELBASE), sizeof(bd_t) );
		/* copy command line */
		*(char *)(r7+KERNELBASE) = 0;
		strcpy(cmd_line, (char *)(r6+KERNELBASE));
	}
	#ifdef CONFIG_ISA
	isa_mem_base = 0;
	#endif

	ppc_md.setup_arch = ev64360_setup_arch;
	ppc_md.show_cpuinfo = ev64360_show_cpuinfo;
	ppc_md.init_IRQ = mv64360_init_irq;
	ppc_md.get_irq = mv64360_get_irq;
	ppc_md.restart = ev64360_restart;
	ppc_md.power_off = ev64360_power_off;
	ppc_md.halt = ev64360_halt;
	ppc_md.find_end_of_memory = ev64360_find_end_of_memory;
	ppc_md.init = NULL;

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;
	ppc_md.calibrate_decr = ev64360_calibrate_decr;

#if defined(CONFIG_SERIAL_TEXT_DEBUG) && defined(CONFIG_SERIAL_MPSC_CONSOLE)
	ppc_md.setup_io_mappings = ev64360_map_io;
	ppc_md.progress = mv64x60_mpsc_progress;
	mv64x60_progress_init(CONFIG_MV64X60_NEW_BASE);
#endif

#if defined(CONFIG_SERIAL_MPSC) || defined(CONFIG_MV643XX_ETH)
	platform_notify = ev64360_platform_notify;
#endif

	ev64360_set_bat(); /* Need for ev64360_find_end_of_memory and progress */
}
