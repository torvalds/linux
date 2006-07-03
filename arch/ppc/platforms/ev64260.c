/*
 * Board setup routines for the Marvell/Galileo EV-64260-BP Evaluation Board.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2001-2003 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * The EV-64260-BP port is the result of hard work from many people from
 * many companies.  In particular, employees of Marvell/Galileo, Mission
 * Critical Linux, Xyterra, and MontaVista Software were heavily involved.
 *
 * Note: I have not been able to get *all* PCI slots to work reliably
 *	at 66 MHz.  I recommend setting jumpers J15 & J16 to short pins 1&2
 *	so that 33 MHz is used. --MAG
 * Note: The 750CXe and 7450 are not stable with a 125MHz or 133MHz TCLK/SYSCLK.
 * 	At 100MHz, they are solid.
 */

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/irq.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/console.h>
#include <linux/initrd.h>
#include <linux/root_dev.h>
#include <linux/platform_device.h>
#if !defined(CONFIG_SERIAL_MPSC_CONSOLE)
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_core.h>
#else
#include <linux/mv643xx.h>
#endif
#include <asm/bootinfo.h>
#include <asm/machdep.h>
#include <asm/mv64x60.h>
#include <asm/todc.h>
#include <asm/time.h>

#include <platforms/ev64260.h>

#define BOARD_VENDOR	"Marvell/Galileo"
#define BOARD_MACHINE	"EV-64260-BP"

static struct mv64x60_handle	bh;

#if !defined(CONFIG_SERIAL_MPSC_CONSOLE)
extern void gen550_progress(char *, unsigned short);
extern void gen550_init(int, struct uart_port *);
#endif

static const unsigned int cpu_7xx[16] = { /* 7xx & 74xx (but not 745x) */
	18, 15, 14, 2, 4, 13, 5, 9, 6, 11, 8, 10, 16, 12, 7, 0
};
static const unsigned int cpu_745x[2][16] = { /* PLL_EXT 0 & 1 */
	{ 1, 15, 14,  2,  4, 13,  5,  9,  6, 11,  8, 10, 16, 12,  7,  0 },
	{ 0, 30,  0,  2,  0, 26,  0, 18,  0, 22, 20, 24, 28, 32,  0,  0 }
};


TODC_ALLOC();

static int
ev64260_get_bus_speed(void)
{
	return 100000000;
}

static int
ev64260_get_cpu_speed(void)
{
	unsigned long	pvr, hid1, pll_ext;

	pvr = PVR_VER(mfspr(SPRN_PVR));

	if (pvr != PVR_VER(PVR_7450)) {
		hid1 = mfspr(SPRN_HID1) >> 28;
		return ev64260_get_bus_speed() * cpu_7xx[hid1]/2;
	}
	else {
		hid1 = (mfspr(SPRN_HID1) & 0x0001e000) >> 13;
		pll_ext = 0; /* No way to read; must get from schematic */
		return ev64260_get_bus_speed() * cpu_745x[pll_ext][hid1]/2;
	}
}

unsigned long __init
ev64260_find_end_of_memory(void)
{
	return mv64x60_get_mem_size(CONFIG_MV64X60_NEW_BASE,
		MV64x60_TYPE_GT64260A);
}

/*
 * Marvell/Galileo EV-64260-BP Evaluation Board PCI interrupt routing.
 * Note: By playing with J8 and JP1-4, you can get 2 IRQ's from the first
 *	PCI bus (in which cast, INTPIN B would be EV64260_PCI_1_IRQ).
 *	This is the most IRQs you can get from one bus with this board, though.
 */
static int __init
ev64260_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	struct pci_controller	*hose = pci_bus_to_hose(dev->bus->number);

	if (hose->index == 0) {
		static char pci_irq_table[][4] =
		/*
		 *	PCI IDSEL/INTPIN->INTLINE
		 * 	   A   B   C   D
		 */
		{
			{EV64260_PCI_0_IRQ,0,0,0}, /* IDSEL 7 - PCI bus 0 */
			{EV64260_PCI_0_IRQ,0,0,0}, /* IDSEL 8 - PCI bus 0 */
		};

		const long min_idsel = 7, max_idsel = 8, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	}
	else {
		static char pci_irq_table[][4] =
		/*
		 *	PCI IDSEL/INTPIN->INTLINE
		 * 	   A   B   C   D
		 */
		{
			{ EV64260_PCI_1_IRQ,0,0,0}, /* IDSEL 7 - PCI bus 1 */
			{ EV64260_PCI_1_IRQ,0,0,0}, /* IDSEL 8 - PCI bus 1 */
		};

		const long min_idsel = 7, max_idsel = 8, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	}
}

static void __init
ev64260_setup_peripherals(void)
{
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2BOOT_WIN,
		EV64260_EMB_FLASH_BASE, EV64260_EMB_FLASH_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2BOOT_WIN);
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_0_WIN,
		EV64260_EXT_SRAM_BASE, EV64260_EXT_SRAM_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_0_WIN);
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_1_WIN,
		EV64260_TODC_BASE, EV64260_TODC_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_1_WIN);
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_2_WIN,
		EV64260_UART_BASE, EV64260_UART_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_2_WIN);
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_3_WIN,
		EV64260_EXT_FLASH_BASE, EV64260_EXT_FLASH_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_3_WIN);

	TODC_INIT(TODC_TYPE_DS1501, 0, 0,
			ioremap(EV64260_TODC_BASE, EV64260_TODC_SIZE), 8);

	mv64x60_clr_bits(&bh, MV64x60_CPU_CONFIG,((1<<12) | (1<<28) | (1<<29)));
	mv64x60_set_bits(&bh, MV64x60_CPU_CONFIG, (1<<27));

	if (ev64260_get_bus_speed() > 100000000)
		mv64x60_set_bits(&bh, MV64x60_CPU_CONFIG, (1<<23));

	mv64x60_set_bits(&bh, MV64x60_PCI0_PCI_DECODE_CNTL, ((1<<0) | (1<<3)));
	mv64x60_set_bits(&bh, MV64x60_PCI1_PCI_DECODE_CNTL, ((1<<0) | (1<<3)));

        /*
         * Enabling of PCI internal-vs-external arbitration
         * is a platform- and errata-dependent decision.
         */
        if (bh.type == MV64x60_TYPE_GT64260A )  {
                mv64x60_set_bits(&bh, MV64x60_PCI0_ARBITER_CNTL, (1<<31));
                mv64x60_set_bits(&bh, MV64x60_PCI1_ARBITER_CNTL, (1<<31));
        }

        mv64x60_set_bits(&bh, MV64x60_CPU_MASTER_CNTL, (1<<9)); /* Only 1 cpu */

	/*
	 * Turn off timer/counters.  Not turning off watchdog timer because
	 * can't read its reg on the 64260A so don't know if we'll be enabling
	 * or disabling.
	 */
	mv64x60_clr_bits(&bh, MV64x60_TIMR_CNTR_0_3_CNTL,
			((1<<0) | (1<<8) | (1<<16) | (1<<24)));
	mv64x60_clr_bits(&bh, GT64260_TIMR_CNTR_4_7_CNTL,
			((1<<0) | (1<<8) | (1<<16) | (1<<24)));

	/*
	 * Set MPSC Multiplex RMII
	 * NOTE: ethernet driver modifies bit 0 and 1
	 */
	mv64x60_write(&bh, GT64260_MPP_SERIAL_PORTS_MULTIPLEX, 0x00001102);

	/*
	 * The EV-64260-BP uses several Multi-Purpose Pins (MPP) on the 64260
	 * bridge as interrupt inputs (via the General Purpose Ports (GPP)
	 * register).  Need to route the MPP inputs to the GPP and set the
	 * polarity correctly.
	 *
	 * In MPP Control 2 Register
	 *   MPP 21 -> GPP 21 (DUART channel A intr) bits 20-23 -> 0
	 *   MPP 22 -> GPP 22 (DUART channel B intr) bits 24-27 -> 0
	 */
	mv64x60_clr_bits(&bh, MV64x60_MPP_CNTL_2, (0xf<<20) | (0xf<<24) );

	/*
	 * In MPP Control 3 Register
	 *   MPP 26 -> GPP 26 (RTC INT)		bits  8-11 -> 0
	 *   MPP 27 -> GPP 27 (PCI 0 INTA)	bits 12-15 -> 0
	 *   MPP 29 -> GPP 29 (PCI 1 INTA)	bits 20-23 -> 0
	 */
	mv64x60_clr_bits(&bh, MV64x60_MPP_CNTL_3, (0xf<<8)|(0xf<<12)|(0xf<<20));

#define GPP_EXTERNAL_INTERRUPTS \
		((1<<21) | (1<<22) | (1<<26) | (1<<27) | (1<<29))
	/* DUART & PCI interrupts are inputs */
	mv64x60_clr_bits(&bh, MV64x60_GPP_IO_CNTL, GPP_EXTERNAL_INTERRUPTS);
	/* DUART & PCI interrupts are active low */
	mv64x60_set_bits(&bh, MV64x60_GPP_LEVEL_CNTL, GPP_EXTERNAL_INTERRUPTS);

	/* Clear any pending interrupts for these inputs and enable them. */
	mv64x60_write(&bh, MV64x60_GPP_INTR_CAUSE, ~GPP_EXTERNAL_INTERRUPTS);
	mv64x60_set_bits(&bh, MV64x60_GPP_INTR_MASK, GPP_EXTERNAL_INTERRUPTS);

	return;
}

static void __init
ev64260_setup_bridge(void)
{
	struct mv64x60_setup_info	si;
	int				i;

	memset(&si, 0, sizeof(si));

	si.phys_reg_base = CONFIG_MV64X60_NEW_BASE;

	si.pci_0.enable_bus = 1;
	si.pci_0.pci_io.cpu_base = EV64260_PCI0_IO_CPU_BASE;
	si.pci_0.pci_io.pci_base_hi = 0;
	si.pci_0.pci_io.pci_base_lo = EV64260_PCI0_IO_PCI_BASE;
	si.pci_0.pci_io.size = EV64260_PCI0_IO_SIZE;
	si.pci_0.pci_io.swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_0.pci_mem[0].cpu_base = EV64260_PCI0_MEM_CPU_BASE;
	si.pci_0.pci_mem[0].pci_base_hi = 0;
	si.pci_0.pci_mem[0].pci_base_lo = EV64260_PCI0_MEM_PCI_BASE;
	si.pci_0.pci_mem[0].size = EV64260_PCI0_MEM_SIZE;
	si.pci_0.pci_mem[0].swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_0.pci_cmd_bits = 0;
	si.pci_0.latency_timer = 0x8;

	si.pci_1.enable_bus = 1;
	si.pci_1.pci_io.cpu_base = EV64260_PCI1_IO_CPU_BASE;
	si.pci_1.pci_io.pci_base_hi = 0;
	si.pci_1.pci_io.pci_base_lo = EV64260_PCI1_IO_PCI_BASE;
	si.pci_1.pci_io.size = EV64260_PCI1_IO_SIZE;
	si.pci_1.pci_io.swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_1.pci_mem[0].cpu_base = EV64260_PCI1_MEM_CPU_BASE;
	si.pci_1.pci_mem[0].pci_base_hi = 0;
	si.pci_1.pci_mem[0].pci_base_lo = EV64260_PCI1_MEM_PCI_BASE;
	si.pci_1.pci_mem[0].size = EV64260_PCI1_MEM_SIZE;
	si.pci_1.pci_mem[0].swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_1.pci_cmd_bits = 0;
	si.pci_1.latency_timer = 0x8;

	for (i=0; i<MV64x60_CPU2MEM_WINDOWS; i++) {
		si.cpu_prot_options[i] = 0;
		si.cpu_snoop_options[i] = GT64260_CPU_SNOOP_WB;
		si.pci_0.acc_cntl_options[i] =
			GT64260_PCI_ACC_CNTL_DREADEN |
			GT64260_PCI_ACC_CNTL_RDPREFETCH |
			GT64260_PCI_ACC_CNTL_RDLINEPREFETCH |
			GT64260_PCI_ACC_CNTL_RDMULPREFETCH |
			GT64260_PCI_ACC_CNTL_SWAP_NONE |
			GT64260_PCI_ACC_CNTL_MBURST_32_BTYES;
		si.pci_0.snoop_options[i] = GT64260_PCI_SNOOP_WB;
		si.pci_1.acc_cntl_options[i] =
			GT64260_PCI_ACC_CNTL_DREADEN |
			GT64260_PCI_ACC_CNTL_RDPREFETCH |
			GT64260_PCI_ACC_CNTL_RDLINEPREFETCH |
			GT64260_PCI_ACC_CNTL_RDMULPREFETCH |
			GT64260_PCI_ACC_CNTL_SWAP_NONE |
			GT64260_PCI_ACC_CNTL_MBURST_32_BTYES;
		si.pci_1.snoop_options[i] = GT64260_PCI_SNOOP_WB;
	}

        /* Lookup PCI host bridges */
        if (mv64x60_init(&bh, &si))
                printk(KERN_ERR "Bridge initialization failed.\n");

	pci_dram_offset = 0; /* System mem at same addr on PCI & cpu bus */
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = ev64260_map_irq;
	ppc_md.pci_exclude_device = mv64x60_pci_exclude_device;

	mv64x60_set_bus(&bh, 0, 0);
	bh.hose_a->first_busno = 0;
	bh.hose_a->last_busno = 0xff;
	bh.hose_a->last_busno = pciauto_bus_scan(bh.hose_a, 0);

	bh.hose_b->first_busno = bh.hose_a->last_busno + 1;
	mv64x60_set_bus(&bh, 1, bh.hose_b->first_busno);
	bh.hose_b->last_busno = 0xff;
	bh.hose_b->last_busno = pciauto_bus_scan(bh.hose_b,
		bh.hose_b->first_busno);

	return;
}

#if defined(CONFIG_SERIAL_8250) && !defined(CONFIG_SERIAL_MPSC_CONSOLE)
static void __init
ev64260_early_serial_map(void)
{
	struct uart_port	port;
	static char		first_time = 1;

	if (first_time) {
		memset(&port, 0, sizeof(port));

		port.membase = ioremap(EV64260_SERIAL_0, EV64260_UART_SIZE);
		port.irq = EV64260_UART_0_IRQ;
		port.uartclk = BASE_BAUD * 16;
		port.regshift = 2;
		port.iotype = UPIO_MEM;
		port.flags = STD_COM_FLAGS;

#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
		gen550_init(0, &port);
#endif

		if (early_serial_setup(&port) != 0)
			printk(KERN_WARNING "Early serial init of port 0"
				"failed\n");

		first_time = 0;
	}

	return;
}
#elif defined(CONFIG_SERIAL_MPSC_CONSOLE)
static void __init
ev64260_early_serial_map(void)
{
}
#endif

static void __init
ev64260_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("ev64260_setup_arch: enter", 0);

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef	CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_SDA2;
#endif

	if (ppc_md.progress)
		ppc_md.progress("ev64260_setup_arch: Enabling L2 cache", 0);

	/* Enable L2 and L3 caches (if 745x) */
	_set_L2CR(_get_L2CR() | L2CR_L2E);
	_set_L3CR(_get_L3CR() | L3CR_L3E);

	if (ppc_md.progress)
		ppc_md.progress("ev64260_setup_arch: Initializing bridge", 0);

	ev64260_setup_bridge();		/* set up PCI bridge(s) */
	ev64260_setup_peripherals();	/* set up chip selects/GPP/MPP etc */

	if (ppc_md.progress)
		ppc_md.progress("ev64260_setup_arch: bridge init complete", 0);

#if defined(CONFIG_SERIAL_8250) || defined(CONFIG_SERIAL_MPSC_CONSOLE)
	ev64260_early_serial_map();
#endif

	printk(KERN_INFO "%s %s port (C) 2001 MontaVista Software, Inc."
		"(source@mvista.com)\n", BOARD_VENDOR, BOARD_MACHINE);

	if (ppc_md.progress)
		ppc_md.progress("ev64260_setup_arch: exit", 0);

	return;
}

/* Platform device data fixup routines. */
#if defined(CONFIG_SERIAL_MPSC)
static void __init
ev64260_fixup_mpsc_pdata(struct platform_device *pdev)
{
	struct mpsc_pdata *pdata;

	pdata = (struct mpsc_pdata *)pdev->dev.platform_data;

	pdata->max_idle = 40;
	pdata->default_baud = EV64260_DEFAULT_BAUD;
	pdata->brg_clk_src = EV64260_MPSC_CLK_SRC;
	pdata->brg_clk_freq = EV64260_MPSC_CLK_FREQ;

	return;
}

static int
ev64260_platform_notify(struct device *dev)
{
	static struct {
		char	*bus_id;
		void	((*rtn)(struct platform_device *pdev));
	} dev_map[] = {
		{ MPSC_CTLR_NAME ".0", ev64260_fixup_mpsc_pdata },
		{ MPSC_CTLR_NAME ".1", ev64260_fixup_mpsc_pdata },
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
#endif

static void
ev64260_reset_board(void *addr)
{
	local_irq_disable();

	/* disable and invalidate the L2 cache */
	_set_L2CR(0);
	_set_L2CR(0x200000);

	/* flush and disable L1 I/D cache */
	__asm__ __volatile__
	("mfspr   3,1008\n\t"
	 "ori	5,5,0xcc00\n\t"
	 "ori	4,3,0xc00\n\t"
	 "andc	5,3,5\n\t"
	 "sync\n\t"
	 "mtspr	1008,4\n\t"
	 "isync\n\t"
	 "sync\n\t"
	 "mtspr	1008,5\n\t"
	 "isync\n\t"
	 "sync\n\t");

	/* unmap any other random cs's that might overlap with bootcs */
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_0_WIN, 0, 0, 0);
	bh.ci->disable_window_32bit(&bh, MV64x60_CPU2DEV_0_WIN);
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_1_WIN, 0, 0, 0);
	bh.ci->disable_window_32bit(&bh, MV64x60_CPU2DEV_1_WIN);
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_2_WIN, 0, 0, 0);
	bh.ci->disable_window_32bit(&bh, MV64x60_CPU2DEV_2_WIN);
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_3_WIN, 0, 0, 0);
	bh.ci->disable_window_32bit(&bh, MV64x60_CPU2DEV_3_WIN);

	/* map bootrom back in to gt @ reset defaults */
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2BOOT_WIN,
						0xff800000, 8*1024*1024, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2BOOT_WIN);

	/* move reg base back to default, setup default pci0 */
	mv64x60_write(&bh, MV64x60_INTERNAL_SPACE_DECODE,
		(1<<24) | CONFIG_MV64X60_BASE >> 20);

	/* NOTE: FROM NOW ON no more GT_REGS accesses.. 0x1 is not mapped
	 * via BAT or MMU, and MSR IR/DR is ON */
	/* SRR0 has system reset vector, SRR1 has default MSR value */
	/* rfi restores MSR from SRR1 and sets the PC to the SRR0 value */
	/* NOTE: assumes reset vector is at 0xfff00100 */
	__asm__ __volatile__
	("mtspr   26, %0\n\t"
	 "li      4,(1<<6)\n\t"
	 "mtspr   27,4\n\t"
	 "rfi\n\t"
	 :: "r" (addr):"r4");

	return;
}

static void
ev64260_restart(char *cmd)
{
	volatile ulong	i = 10000000;

	ev64260_reset_board((void *)0xfff00100);

	while (i-- > 0);
	panic("restart failed\n");
}

static void
ev64260_halt(void)
{
	local_irq_disable();
	while (1);
	/* NOTREACHED */
}

static void
ev64260_power_off(void)
{
	ev64260_halt();
	/* NOTREACHED */
}

static int
ev64260_show_cpuinfo(struct seq_file *m)
{
	uint pvid;

	pvid = mfspr(SPRN_PVR);
	seq_printf(m, "vendor\t\t: " BOARD_VENDOR "\n");
	seq_printf(m, "machine\t\t: " BOARD_MACHINE "\n");
	seq_printf(m, "cpu MHz\t\t: %d\n", ev64260_get_cpu_speed()/1000/1000);
	seq_printf(m, "bus MHz\t\t: %d\n", ev64260_get_bus_speed()/1000/1000);

	return 0;
}

/* DS1501 RTC has too much variation to use RTC for calibration */
static void __init
ev64260_calibrate_decr(void)
{
	ulong freq;

	freq = ev64260_get_bus_speed()/4;

	printk(KERN_INFO "time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       freq/1000000, freq%1000000);

	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);

	return;
}

/*
 * Set BAT 3 to map 0xfb000000 to 0xfc000000 of physical memory space.
 */
static __inline__ void
ev64260_set_bat(void)
{
	mb();
	mtspr(SPRN_DBAT1U, 0xfb0001fe);
	mtspr(SPRN_DBAT1L, 0xfb00002a);
	mb();

	return;
}

#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
static void __init
ev64260_map_io(void)
{
	io_block_mapping(0xfb000000, 0xfb000000, 0x01000000, _PAGE_IO);
}
#endif

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
#ifdef CONFIG_BLK_DEV_INITRD
	extern int	initrd_below_start_ok;

	initrd_start=initrd_end=0;
	initrd_below_start_ok=0;
#endif /* CONFIG_BLK_DEV_INITRD */

	parse_bootinfo(find_bootinfo());

	isa_mem_base = 0;
	isa_io_base = EV64260_PCI0_IO_CPU_BASE;
	pci_dram_offset = EV64260_PCI0_MEM_CPU_BASE;

	loops_per_jiffy = ev64260_get_cpu_speed() / HZ;

	ppc_md.setup_arch = ev64260_setup_arch;
	ppc_md.show_cpuinfo = ev64260_show_cpuinfo;
	ppc_md.init_IRQ = gt64260_init_irq;
	ppc_md.get_irq = gt64260_get_irq;

	ppc_md.restart = ev64260_restart;
	ppc_md.power_off = ev64260_power_off;
	ppc_md.halt = ev64260_halt;

	ppc_md.find_end_of_memory = ev64260_find_end_of_memory;

	ppc_md.init = NULL;

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.nvram_read_val = todc_direct_read_val;
	ppc_md.nvram_write_val = todc_direct_write_val;
	ppc_md.calibrate_decr = ev64260_calibrate_decr;

	bh.p_base = CONFIG_MV64X60_NEW_BASE;

	ev64260_set_bat();

#ifdef	CONFIG_SERIAL_8250
#if defined(CONFIG_SERIAL_TEXT_DEBUG)
	ppc_md.setup_io_mappings = ev64260_map_io;
	ppc_md.progress = gen550_progress;
#endif
#if defined(CONFIG_KGDB)
	ppc_md.setup_io_mappings = ev64260_map_io;
	ppc_md.early_serial_map = ev64260_early_serial_map;
#endif
#elif defined(CONFIG_SERIAL_MPSC_CONSOLE)
#ifdef	CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.setup_io_mappings = ev64260_map_io;
	ppc_md.progress = mv64x60_mpsc_progress;
	mv64x60_progress_init(CONFIG_MV64X60_NEW_BASE);
#endif	/* CONFIG_SERIAL_TEXT_DEBUG */
#ifdef	CONFIG_KGDB
	ppc_md.setup_io_mappings = ev64260_map_io;
	ppc_md.early_serial_map = ev64260_early_serial_map;
#endif	/* CONFIG_KGDB */

#endif

#if defined(CONFIG_SERIAL_MPSC)
	platform_notify = ev64260_platform_notify;
#endif

	return;
}
