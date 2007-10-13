/*
 * Board setup routines for the Artesyn Katana cPCI boards.
 *
 * Author: Tim Montgomery <timm@artesyncp.com>
 * Maintained by: Mark A. Greer <mgreer@mvista.com>
 *
 * Based on code done by Rabeeh Khoury - rabeeh@galileo.co.il
 * Based on code done by - Mark A. Greer <mgreer@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */
/*
 * Supports the Artesyn 750i, 752i, and 3750.  The 752i is virtually identical
 * to the 750i except that it has an mv64460 bridge.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/console.h>
#include <linux/initrd.h>
#include <linux/root_dev.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/mtd/physmap.h>
#include <linux/mv643xx.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/unistd.h>
#include <asm/page.h>
#include <asm/time.h>
#include <asm/smp.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>
#include <asm/ppcboot.h>
#include <asm/mv64x60.h>
#include <platforms/katana.h>
#include <asm/machdep.h>

static struct mv64x60_handle	bh;
static katana_id_t		katana_id;
static void __iomem		*cpld_base;
static void __iomem		*sram_base;
static u32			katana_flash_size_0;
static u32			katana_flash_size_1;
static u32			katana_bus_frequency;
static struct pci_controller	katana_hose_a;

unsigned char	__res[sizeof(bd_t)];

/* PCI Interrupt routing */
static int __init
katana_irq_lookup_750i(unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] = {
		/*
		 * PCI IDSEL/INTPIN->INTLINE
		 *       A   B   C   D
		 */
		/* IDSEL 4  (PMC 1) */
		{ KATANA_PCI_INTB_IRQ_750i, KATANA_PCI_INTC_IRQ_750i,
			KATANA_PCI_INTD_IRQ_750i, KATANA_PCI_INTA_IRQ_750i },
		/* IDSEL 5  (PMC 2) */
		{ KATANA_PCI_INTC_IRQ_750i, KATANA_PCI_INTD_IRQ_750i,
			KATANA_PCI_INTA_IRQ_750i, KATANA_PCI_INTB_IRQ_750i },
		/* IDSEL 6 (T8110) */
		{KATANA_PCI_INTD_IRQ_750i, 0, 0, 0 },
		/* IDSEL 7 (unused) */
		{0, 0, 0, 0 },
		/* IDSEL 8 (Intel 82544) (752i only but doesn't harm 750i) */
		{KATANA_PCI_INTD_IRQ_750i, 0, 0, 0 },
	};
	const long min_idsel = 4, max_idsel = 8, irqs_per_slot = 4;

	return PCI_IRQ_TABLE_LOOKUP;
}

static int __init
katana_irq_lookup_3750(unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] = {
		/*
		 * PCI IDSEL/INTPIN->INTLINE
		 *       A   B   C   D
		 */
		{ KATANA_PCI_INTA_IRQ_3750, 0, 0, 0 }, /* IDSEL 3 (BCM5691) */
		{ KATANA_PCI_INTB_IRQ_3750, 0, 0, 0 }, /* IDSEL 4 (MV64360 #2)*/
		{ KATANA_PCI_INTC_IRQ_3750, 0, 0, 0 }, /* IDSEL 5 (MV64360 #3)*/
	};
	const long min_idsel = 3, max_idsel = 5, irqs_per_slot = 4;

	return PCI_IRQ_TABLE_LOOKUP;
}

static int __init
katana_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	switch (katana_id) {
	case KATANA_ID_750I:
	case KATANA_ID_752I:
		return katana_irq_lookup_750i(idsel, pin);

	case KATANA_ID_3750:
		return katana_irq_lookup_3750(idsel, pin);

	default:
		printk(KERN_ERR "Bogus board ID\n");
		return 0;
	}
}

/* Board info retrieval routines */
void __init
katana_get_board_id(void)
{
	switch (in_8(cpld_base + KATANA_CPLD_PRODUCT_ID)) {
	case KATANA_PRODUCT_ID_3750:
		katana_id = KATANA_ID_3750;
		break;

	case KATANA_PRODUCT_ID_750i:
		katana_id = KATANA_ID_750I;
		break;

	case KATANA_PRODUCT_ID_752i:
		katana_id = KATANA_ID_752I;
		break;

	default:
		printk(KERN_ERR "Unsupported board\n");
	}
}

int __init
katana_get_proc_num(void)
{
	u16		val;
	u8		save_exclude;
	static int	proc = -1;
	static u8	first_time = 1;

	if (first_time) {
		if (katana_id != KATANA_ID_3750)
			proc = 0;
		else {
			save_exclude = mv64x60_pci_exclude_bridge;
			mv64x60_pci_exclude_bridge = 0;

			early_read_config_word(bh.hose_b, 0,
				PCI_DEVFN(0,0), PCI_DEVICE_ID, &val);

			mv64x60_pci_exclude_bridge = save_exclude;

			switch(val) {
			case PCI_DEVICE_ID_KATANA_3750_PROC0:
				proc = 0;
				break;

			case PCI_DEVICE_ID_KATANA_3750_PROC1:
				proc = 1;
				break;

			case PCI_DEVICE_ID_KATANA_3750_PROC2:
				proc = 2;
				break;

			default:
				printk(KERN_ERR "Bogus Device ID\n");
			}
		}

		first_time = 0;
	}

	return proc;
}

static inline int
katana_is_monarch(void)
{
	return in_8(cpld_base + KATANA_CPLD_BD_CFG_3) &
		KATANA_CPLD_BD_CFG_3_MONARCH;
}

static void __init
katana_setup_bridge(void)
{
	struct pci_controller hose;
	struct mv64x60_setup_info si;
	void __iomem *vaddr;
	int i;
	u32 v;
	u16 val, type;
	u8 save_exclude;

	/*
	 * Some versions of the Katana firmware mistakenly change the vendor
	 * & device id fields in the bridge's pci device (visible via pci
	 * config accesses).  This breaks mv64x60_init() because those values
	 * are used to identify the type of bridge that's there.  Artesyn
	 * claims that the subsystem vendor/device id's will have the correct
	 * Marvell values so this code puts back the correct values from there.
	 */
	memset(&hose, 0, sizeof(hose));
	vaddr = ioremap(CONFIG_MV64X60_NEW_BASE, MV64x60_INTERNAL_SPACE_SIZE);
	setup_indirect_pci_nomap(&hose, vaddr + MV64x60_PCI0_CONFIG_ADDR,
		vaddr + MV64x60_PCI0_CONFIG_DATA);
	save_exclude = mv64x60_pci_exclude_bridge;
	mv64x60_pci_exclude_bridge = 0;

	early_read_config_word(&hose, 0, PCI_DEVFN(0, 0), PCI_VENDOR_ID, &val);

	if (val != PCI_VENDOR_ID_MARVELL) {
		early_read_config_word(&hose, 0, PCI_DEVFN(0, 0),
			PCI_SUBSYSTEM_VENDOR_ID, &val);
		early_write_config_word(&hose, 0, PCI_DEVFN(0, 0),
			PCI_VENDOR_ID, val);
		early_read_config_word(&hose, 0, PCI_DEVFN(0, 0),
			PCI_SUBSYSTEM_ID, &val);
		early_write_config_word(&hose, 0, PCI_DEVFN(0, 0),
			PCI_DEVICE_ID, val);
	}

	/*
	 * While we're in here, set the hotswap register correctly.
	 * Turn off blue LED; mask ENUM#, clear insertion & extraction bits.
	 */
	early_read_config_dword(&hose, 0, PCI_DEVFN(0, 0),
		MV64360_PCICFG_CPCI_HOTSWAP, &v);
	v &= ~(1<<19);
	v |= ((1<<17) | (1<<22) | (1<<23));
	early_write_config_dword(&hose, 0, PCI_DEVFN(0, 0),
		MV64360_PCICFG_CPCI_HOTSWAP, v);

	/* While we're at it, grab the bridge type for later */
	early_read_config_word(&hose, 0, PCI_DEVFN(0, 0), PCI_DEVICE_ID, &type);

	mv64x60_pci_exclude_bridge = save_exclude;
	iounmap(vaddr);

	memset(&si, 0, sizeof(si));

	si.phys_reg_base = CONFIG_MV64X60_NEW_BASE;

	si.pci_1.enable_bus = 1;
	si.pci_1.pci_io.cpu_base = KATANA_PCI1_IO_START_PROC_ADDR;
	si.pci_1.pci_io.pci_base_hi = 0;
	si.pci_1.pci_io.pci_base_lo = KATANA_PCI1_IO_START_PCI_ADDR;
	si.pci_1.pci_io.size = KATANA_PCI1_IO_SIZE;
	si.pci_1.pci_io.swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_1.pci_mem[0].cpu_base = KATANA_PCI1_MEM_START_PROC_ADDR;
	si.pci_1.pci_mem[0].pci_base_hi = KATANA_PCI1_MEM_START_PCI_HI_ADDR;
	si.pci_1.pci_mem[0].pci_base_lo = KATANA_PCI1_MEM_START_PCI_LO_ADDR;
	si.pci_1.pci_mem[0].size = KATANA_PCI1_MEM_SIZE;
	si.pci_1.pci_mem[0].swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_1.pci_cmd_bits = 0;
	si.pci_1.latency_timer = 0x80;

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
		si.enet_options[i] = MV64360_ENET2MEM_SNOOP_WB;
		si.mpsc_options[i] = MV64360_MPSC2MEM_SNOOP_WB;
		si.idma_options[i] = MV64360_IDMA2MEM_SNOOP_WB;

		si.pci_1.acc_cntl_options[i] =
			MV64360_PCI_ACC_CNTL_SNOOP_WB |
			MV64360_PCI_ACC_CNTL_SWAP_NONE |
			MV64360_PCI_ACC_CNTL_MBURST_32_BYTES |
			((type == PCI_DEVICE_ID_MARVELL_MV64360) ?
				MV64360_PCI_ACC_CNTL_RDSIZE_32_BYTES :
				MV64360_PCI_ACC_CNTL_RDSIZE_256_BYTES);
#endif
	}

	/* Lookup PCI host bridges */
	if (mv64x60_init(&bh, &si))
		printk(KERN_WARNING "Bridge initialization failed.\n");

	pci_dram_offset = 0; /* sys mem at same addr on PCI & cpu bus */
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = katana_map_irq;
	ppc_md.pci_exclude_device = mv64x60_pci_exclude_device;

	mv64x60_set_bus(&bh, 1, 0);
	bh.hose_b->first_busno = 0;
	bh.hose_b->last_busno = 0xff;

	/*
	 * Need to access hotswap reg which is in the pci config area of the
	 * bridge's hose 0.  Note that pcibios_alloc_controller() can't be used
	 * to alloc hose_a b/c that would make hose 0 known to the generic
	 * pci code which we don't want.
	 */
	bh.hose_a = &katana_hose_a;
	setup_indirect_pci_nomap(bh.hose_a,
		bh.v_base + MV64x60_PCI0_CONFIG_ADDR,
		bh.v_base + MV64x60_PCI0_CONFIG_DATA);
}

/* Bridge & platform setup routines */
void __init
katana_intr_setup(void)
{
	if (bh.type == MV64x60_TYPE_MV64460) /* As per instns from Marvell */
		mv64x60_clr_bits(&bh, MV64x60_CPU_MASTER_CNTL, 1 << 15);

	/* MPP 8, 9, and 10 */
	mv64x60_clr_bits(&bh, MV64x60_MPP_CNTL_1, 0xfff);

	/* MPP 14 */
	if ((katana_id == KATANA_ID_750I) || (katana_id == KATANA_ID_752I))
		mv64x60_clr_bits(&bh, MV64x60_MPP_CNTL_1, 0x0f000000);

	/*
	 * Define GPP 8,9,and 10 interrupt polarity as active low
	 * input signal and level triggered
	 */
	mv64x60_set_bits(&bh, MV64x60_GPP_LEVEL_CNTL, 0x700);
	mv64x60_clr_bits(&bh, MV64x60_GPP_IO_CNTL, 0x700);

	if ((katana_id == KATANA_ID_750I) || (katana_id == KATANA_ID_752I)) {
		mv64x60_set_bits(&bh, MV64x60_GPP_LEVEL_CNTL, (1<<14));
		mv64x60_clr_bits(&bh, MV64x60_GPP_IO_CNTL, (1<<14));
	}

	/* Config GPP intr ctlr to respond to level trigger */
	mv64x60_set_bits(&bh, MV64x60_COMM_ARBITER_CNTL, (1<<10));

	if (bh.type == MV64x60_TYPE_MV64360) {
		/* Erratum FEr PCI-#9 */
		mv64x60_clr_bits(&bh, MV64x60_PCI1_CMD,
				(1<<4) | (1<<5) | (1<<6) | (1<<7));
		mv64x60_set_bits(&bh, MV64x60_PCI1_CMD, (1<<8) | (1<<9));
	} else {
		mv64x60_clr_bits(&bh, MV64x60_PCI1_CMD, (1<<6) | (1<<7));
		mv64x60_set_bits(&bh, MV64x60_PCI1_CMD,
				(1<<4) | (1<<5) | (1<<8) | (1<<9));
	}

	/*
	 * Dismiss and then enable interrupt on GPP interrupt cause
	 * for CPU #0
	 */
	mv64x60_write(&bh, MV64x60_GPP_INTR_CAUSE, ~0x700);
	mv64x60_set_bits(&bh, MV64x60_GPP_INTR_MASK, 0x700);

	if ((katana_id == KATANA_ID_750I) || (katana_id == KATANA_ID_752I)) {
		mv64x60_write(&bh, MV64x60_GPP_INTR_CAUSE, ~(1<<14));
		mv64x60_set_bits(&bh, MV64x60_GPP_INTR_MASK, (1<<14));
	}

	/*
	 * Dismiss and then enable interrupt on CPU #0 high cause reg
	 * BIT25 summarizes GPP interrupts 8-15
	 */
	mv64x60_set_bits(&bh, MV64360_IC_CPU0_INTR_MASK_HI, (1<<25));
}

void __init
katana_setup_peripherals(void)
{
	u32 base;

	/* Set up windows for boot CS, soldered & socketed flash, and CPLD */
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2BOOT_WIN,
		 KATANA_BOOT_WINDOW_BASE, KATANA_BOOT_WINDOW_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2BOOT_WIN);

	/* Assume firmware set up window sizes correctly for dev 0 & 1 */
	mv64x60_get_32bit_window(&bh, MV64x60_CPU2DEV_0_WIN, &base,
		&katana_flash_size_0);

	if (katana_flash_size_0 > 0) {
		mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_0_WIN,
			 KATANA_SOLDERED_FLASH_BASE, katana_flash_size_0, 0);
		bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_0_WIN);
	}

	mv64x60_get_32bit_window(&bh, MV64x60_CPU2DEV_1_WIN, &base,
		&katana_flash_size_1);

	if (katana_flash_size_1 > 0) {
		mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_1_WIN,
			 (KATANA_SOLDERED_FLASH_BASE + katana_flash_size_0),
			 katana_flash_size_1, 0);
		bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_1_WIN);
	}

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_2_WIN,
		 KATANA_SOCKET_BASE, KATANA_SOCKETED_FLASH_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_2_WIN);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_3_WIN,
		 KATANA_CPLD_BASE, KATANA_CPLD_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_3_WIN);
	cpld_base = ioremap(KATANA_CPLD_BASE, KATANA_CPLD_SIZE);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2SRAM_WIN,
		 KATANA_INTERNAL_SRAM_BASE, MV64360_SRAM_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2SRAM_WIN);
	sram_base = ioremap(KATANA_INTERNAL_SRAM_BASE, MV64360_SRAM_SIZE);

	/* Set up Enet->SRAM window */
	mv64x60_set_32bit_window(&bh, MV64x60_ENET2MEM_4_WIN,
		KATANA_INTERNAL_SRAM_BASE, MV64360_SRAM_SIZE, 0x2);
	bh.ci->enable_window_32bit(&bh, MV64x60_ENET2MEM_4_WIN);

	/* Give enet r/w access to memory region */
	mv64x60_set_bits(&bh, MV64360_ENET2MEM_ACC_PROT_0, (0x3 << (4 << 1)));
	mv64x60_set_bits(&bh, MV64360_ENET2MEM_ACC_PROT_1, (0x3 << (4 << 1)));
	mv64x60_set_bits(&bh, MV64360_ENET2MEM_ACC_PROT_2, (0x3 << (4 << 1)));

	mv64x60_clr_bits(&bh, MV64x60_PCI1_PCI_DECODE_CNTL, (1 << 3));
	mv64x60_clr_bits(&bh, MV64x60_TIMR_CNTR_0_3_CNTL,
			 ((1 << 0) | (1 << 8) | (1 << 16) | (1 << 24)));

	/* Must wait until window set up before retrieving board id */
	katana_get_board_id();

	/* Enumerate pci bus (must know board id before getting proc number) */
	if (katana_get_proc_num() == 0)
		bh.hose_b->last_busno = pciauto_bus_scan(bh.hose_b, 0);

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

	/* Only processor zero [on 3750] is an PCI interrupt controller */
	if (katana_get_proc_num() == 0)
		katana_intr_setup();
}

static void __init
katana_enable_ipmi(void)
{
	u8 reset_out;

	/* Enable access to IPMI ctlr by clearing IPMI PORTSEL bit in CPLD */
	reset_out = in_8(cpld_base + KATANA_CPLD_RESET_OUT);
	reset_out &= ~KATANA_CPLD_RESET_OUT_PORTSEL;
	out_8(cpld_base + KATANA_CPLD_RESET_OUT, reset_out);
}

static void __init
katana_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("katana_setup_arch: enter", 0);

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
	 *
	 * 750FX has only L2E, L2PE (bits 2-8 are reserved)
	 * DD2.0 has bug that requires the L2 to be in WRT mode
	 * avoid dirty data in cache
	 */
	if (PVR_REV(mfspr(SPRN_PVR)) == 0x0200) {
		printk(KERN_INFO "DD2.0 detected. Setting L2 cache"
			"to Writethrough mode\n");
		_set_L2CR(L2CR_L2E | L2CR_L2PE | L2CR_L2WT);
	} else
		_set_L2CR(L2CR_L2E | L2CR_L2PE);

	if (ppc_md.progress)
		ppc_md.progress("katana_setup_arch: calling setup_bridge", 0);

	katana_setup_bridge();
	katana_setup_peripherals();
	katana_enable_ipmi();

	katana_bus_frequency = katana_bus_freq(cpld_base);

	printk(KERN_INFO "Artesyn Communication Products, LLC - Katana(TM)\n");
	if (ppc_md.progress)
		ppc_md.progress("katana_setup_arch: exit", 0);
}

void
katana_fixup_resources(struct pci_dev *dev)
{
	u16	v16;

	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, L1_CACHE_BYTES>>2);

	pci_read_config_word(dev, PCI_COMMAND, &v16);
	v16 |= PCI_COMMAND_INVALIDATE | PCI_COMMAND_FAST_BACK;
	pci_write_config_word(dev, PCI_COMMAND, v16);
}

static const unsigned int cpu_750xx[32] = { /* 750FX & 750GX */
	 0,  0,  2,  2,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,/* 0-15*/
	16, 17, 18, 19, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40,  0 /*16-31*/
};

static int
katana_get_cpu_freq(void)
{
	unsigned long	pll_cfg;

	pll_cfg = (mfspr(SPRN_HID1) & 0xf8000000) >> 27;
	return katana_bus_frequency * cpu_750xx[pll_cfg]/2;
}

/* Platform device data fixup routines. */
#if defined(CONFIG_SERIAL_MPSC)
static void __init
katana_fixup_mpsc_pdata(struct platform_device *pdev)
{
	struct mpsc_pdata *pdata = (struct mpsc_pdata *)pdev->dev.platform_data;
	bd_t *bdp = (bd_t *)__res;

	if (bdp->bi_baudrate)
		pdata->default_baud = bdp->bi_baudrate;
	else
		pdata->default_baud = KATANA_DEFAULT_BAUD;

	pdata->max_idle = 40;
	pdata->brg_clk_src = KATANA_MPSC_CLK_SRC;
	/*
	 * TCLK (not SysCLk) is routed to BRG, then to the MPSC.  On most parts,
	 * TCLK == SysCLK but on 64460, they are separate pins.
	 * SysCLK can go up to 200 MHz but TCLK can only go up to 133 MHz.
	 */
	pdata->brg_clk_freq = min(katana_bus_frequency, MV64x60_TCLK_FREQ_MAX);
}
#endif

#if defined(CONFIG_MV643XX_ETH)
static void __init
katana_fixup_eth_pdata(struct platform_device *pdev)
{
	struct mv643xx_eth_platform_data *eth_pd;
	static u16 phy_addr[] = {
		KATANA_ETH0_PHY_ADDR,
		KATANA_ETH1_PHY_ADDR,
		KATANA_ETH2_PHY_ADDR,
	};

	eth_pd = pdev->dev.platform_data;
	eth_pd->force_phy_addr = 1;
	eth_pd->phy_addr = phy_addr[pdev->id];
	eth_pd->tx_queue_size = KATANA_ETH_TX_QUEUE_SIZE;
	eth_pd->rx_queue_size = KATANA_ETH_RX_QUEUE_SIZE;
}
#endif

#if defined(CONFIG_SYSFS)
static void __init
katana_fixup_mv64xxx_pdata(struct platform_device *pdev)
{
	struct mv64xxx_pdata *pdata = (struct mv64xxx_pdata *)
		pdev->dev.platform_data;

	/* Katana supports the mv64xxx hotswap register */
	pdata->hs_reg_valid = 1;
}
#endif

static int
katana_platform_notify(struct device *dev)
{
	static struct {
		char	*bus_id;
		void	((*rtn)(struct platform_device *pdev));
	} dev_map[] = {
#if defined(CONFIG_SERIAL_MPSC)
		{ MPSC_CTLR_NAME ".0", katana_fixup_mpsc_pdata },
		{ MPSC_CTLR_NAME ".1", katana_fixup_mpsc_pdata },
#endif
#if defined(CONFIG_MV643XX_ETH)
		{ MV643XX_ETH_NAME ".0", katana_fixup_eth_pdata },
		{ MV643XX_ETH_NAME ".1", katana_fixup_eth_pdata },
		{ MV643XX_ETH_NAME ".2", katana_fixup_eth_pdata },
#endif
#if defined(CONFIG_SYSFS)
		{ MV64XXX_DEV_NAME ".0", katana_fixup_mv64xxx_pdata },
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
 * MTD Layout depends on amount of soldered FLASH in system. Sizes in MB.
 *
 * FLASH Amount:	128	64	32	16
 * -------------	---	--	--	--
 * Monitor:		1	1	1	1
 * Primary Kernel:	1.5	1.5	1.5	1.5
 * Primary fs:		30	30	<end>	<end>
 * Secondary Kernel:	1.5	1.5	N/A	N/A
 * Secondary fs:	<end>	<end>	N/A	N/A
 * User: 		<overlays entire FLASH except for "Monitor" section>
 */
static int __init
katana_setup_mtd(void)
{
	u32	size;
	int	ptbl_entries;
	static struct mtd_partition	*ptbl;

	size = katana_flash_size_0 + katana_flash_size_1;
	if (!size)
		return -ENOMEM;

	ptbl_entries = (size >= (64*MB)) ? 6 : 4;

	if ((ptbl = kcalloc(ptbl_entries, sizeof(struct mtd_partition),
			GFP_KERNEL)) == NULL) {
		printk(KERN_WARNING "Can't alloc MTD partition table\n");
		return -ENOMEM;
	}

	ptbl[0].name = "Monitor";
	ptbl[0].size = KATANA_MTD_MONITOR_SIZE;
	ptbl[1].name = "Primary Kernel";
	ptbl[1].offset = MTDPART_OFS_NXTBLK;
	ptbl[1].size = 0x00180000; /* 1.5 MB */
	ptbl[2].name = "Primary Filesystem";
	ptbl[2].offset = MTDPART_OFS_APPEND;
	ptbl[2].size = MTDPART_SIZ_FULL; /* Correct for 16 & 32 MB */
	ptbl[ptbl_entries-1].name = "User FLASH";
	ptbl[ptbl_entries-1].offset = KATANA_MTD_MONITOR_SIZE;
	ptbl[ptbl_entries-1].size = MTDPART_SIZ_FULL;

	if (size >= (64*MB)) {
		ptbl[2].size = 30*MB;
		ptbl[3].name = "Secondary Kernel";
		ptbl[3].offset = MTDPART_OFS_NXTBLK;
		ptbl[3].size = 0x00180000; /* 1.5 MB */
		ptbl[4].name = "Secondary Filesystem";
		ptbl[4].offset = MTDPART_OFS_APPEND;
		ptbl[4].size = MTDPART_SIZ_FULL;
	}

	physmap_map.size = size;
	physmap_set_partitions(ptbl, ptbl_entries);
	return 0;
}
arch_initcall(katana_setup_mtd);
#endif

static void
katana_restart(char *cmd)
{
	ulong	i = 10000000;

	/* issue hard reset to the reset command register */
	out_8(cpld_base + KATANA_CPLD_RST_CMD, KATANA_CPLD_RST_CMD_HR);

	while (i-- > 0) ;
	panic("restart failed\n");
}

static void
katana_halt(void)
{
	u8	v;

	/* Turn on blue LED to indicate its okay to remove */
	if (katana_id == KATANA_ID_750I) {
		u32	v;
		u8	save_exclude;

		/* Set LOO bit in cPCI HotSwap reg of hose 0 to turn on LED. */
		save_exclude = mv64x60_pci_exclude_bridge;
		mv64x60_pci_exclude_bridge = 0;
		early_read_config_dword(bh.hose_a, 0, PCI_DEVFN(0, 0),
			MV64360_PCICFG_CPCI_HOTSWAP, &v);
		v &= 0xff;
		v |= (1 << 19);
		early_write_config_dword(bh.hose_a, 0, PCI_DEVFN(0, 0),
			MV64360_PCICFG_CPCI_HOTSWAP, v);
		mv64x60_pci_exclude_bridge = save_exclude;
	} else if (katana_id == KATANA_ID_752I) {
		   v = in_8(cpld_base + HSL_PLD_BASE + HSL_PLD_HOT_SWAP_OFF);
		   v |= HSL_PLD_HOT_SWAP_LED_BIT;
		   out_8(cpld_base + HSL_PLD_BASE + HSL_PLD_HOT_SWAP_OFF, v);
	}

	while (1) ;
	/* NOTREACHED */
}

static void
katana_power_off(void)
{
	katana_halt();
	/* NOTREACHED */
}

static int
katana_show_cpuinfo(struct seq_file *m)
{
	char	*s;

	seq_printf(m, "cpu freq\t: %dMHz\n",
		(katana_get_cpu_freq() + 500000) / 1000000);
	seq_printf(m, "bus freq\t: %ldMHz\n",
		((long)katana_bus_frequency + 500000) / 1000000);
	seq_printf(m, "vendor\t\t: Artesyn Communication Products, LLC\n");

	seq_printf(m, "board\t\t: ");
	switch (katana_id) {
	case KATANA_ID_3750:
		seq_printf(m, "Katana 3750");
		break;

	case KATANA_ID_750I:
		seq_printf(m, "Katana 750i");
		break;

	case KATANA_ID_752I:
		seq_printf(m, "Katana 752i");
		break;

	default:
		seq_printf(m, "Unknown");
		break;
	}
	seq_printf(m, " (product id: 0x%x)\n",
		   in_8(cpld_base + KATANA_CPLD_PRODUCT_ID));

	seq_printf(m, "pci mode\t: %sMonarch\n",
		katana_is_monarch()? "" : "Non-");
	seq_printf(m, "hardware rev\t: 0x%x\n",
		   in_8(cpld_base+KATANA_CPLD_HARDWARE_VER));
	seq_printf(m, "pld rev\t\t: 0x%x\n",
		   in_8(cpld_base + KATANA_CPLD_PLD_VER));

	switch(bh.type) {
	case MV64x60_TYPE_GT64260A:
		s = "gt64260a";
		break;
	case MV64x60_TYPE_GT64260B:
		s = "gt64260b";
		break;
	case MV64x60_TYPE_MV64360:
		s = "mv64360";
		break;
	case MV64x60_TYPE_MV64460:
		s = "mv64460";
		break;
	default:
		s = "Unknown";
	}
	seq_printf(m, "bridge type\t: %s\n", s);
	seq_printf(m, "bridge rev\t: 0x%x\n", bh.rev);
#if defined(CONFIG_NOT_COHERENT_CACHE)
	seq_printf(m, "coherency\t: %s\n", "off");
#else
	seq_printf(m, "coherency\t: %s\n", "on");
#endif

	return 0;
}

static void __init
katana_calibrate_decr(void)
{
	u32 freq;

	freq = katana_bus_frequency / 4;

	printk(KERN_INFO "time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       (long)freq / 1000000, (long)freq % 1000000);

	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);
}

/*
 * The katana supports both uImage and zImage.  If uImage, get the mem size
 * from the bd info.  If zImage, the bootwrapper adds a BI_MEMSIZE entry in
 * the bi_rec data which is sucked out and put into boot_mem_size by
 * parse_bootinfo().  MMU_init() will then use the boot_mem_size for the mem
 * size and not call this routine.  The only way this will fail is when a uImage
 * is used but the fw doesn't pass in a valid bi_memsize.  This should never
 * happen, though.
 */
unsigned long __init
katana_find_end_of_memory(void)
{
	bd_t *bdp = (bd_t *)__res;
	return bdp->bi_memsize;
}

#if defined(CONFIG_I2C_MV64XXX) && defined(CONFIG_SENSORS_M41T00)
extern ulong	m41t00_get_rtc_time(void);
extern int	m41t00_set_rtc_time(ulong);

static int __init
katana_rtc_hookup(void)
{
	struct timespec	tv;

	ppc_md.get_rtc_time = m41t00_get_rtc_time;
	ppc_md.set_rtc_time = m41t00_set_rtc_time;

	tv.tv_nsec = 0;
	tv.tv_sec = (ppc_md.get_rtc_time)();
	do_settimeofday(&tv);

	return 0;
}
late_initcall(katana_rtc_hookup);
#endif

#if defined(CONFIG_SERIAL_TEXT_DEBUG) && defined(CONFIG_SERIAL_MPSC_CONSOLE)
static void __init
katana_map_io(void)
{
	io_block_mapping(0xf8100000, 0xf8100000, 0x00020000, _PAGE_IO);
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
	 * from birecs as discovered by the preceding call to
	 * parse_bootinfo().  This rule should work with both PPCBoot, which
	 * uses a bd_t board info structure, and the kernel boot wrapper,
	 * which uses birecs.
	 */
	if (r3 && r6) {
		/* copy board info structure */
		memcpy((void *)__res, (void *)(r3+KERNELBASE), sizeof(bd_t));
		/* copy command line */
		*(char *)(r7+KERNELBASE) = 0;
		strcpy(cmd_line, (char *)(r6+KERNELBASE));
	}

#ifdef CONFIG_BLK_DEV_INITRD
	/* take care of initrd if we have one */
	if (r4) {
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	isa_mem_base = 0;

	ppc_md.setup_arch = katana_setup_arch;
	ppc_md.pcibios_fixup_resources = katana_fixup_resources;
	ppc_md.show_cpuinfo = katana_show_cpuinfo;
	ppc_md.init_IRQ = mv64360_init_irq;
	ppc_md.get_irq = mv64360_get_irq;
	ppc_md.restart = katana_restart;
	ppc_md.power_off = katana_power_off;
	ppc_md.halt = katana_halt;
	ppc_md.find_end_of_memory = katana_find_end_of_memory;
	ppc_md.calibrate_decr = katana_calibrate_decr;

#if defined(CONFIG_SERIAL_TEXT_DEBUG) && defined(CONFIG_SERIAL_MPSC_CONSOLE)
	ppc_md.setup_io_mappings = katana_map_io;
	ppc_md.progress = mv64x60_mpsc_progress;
	mv64x60_progress_init(CONFIG_MV64X60_NEW_BASE);
#endif

#if defined(CONFIG_SERIAL_MPSC) || defined(CONFIG_MV643XX_ETH)
	platform_notify = katana_platform_notify;
#endif
}
