/*
 * arch/ppc/platforms/chestnut.c
 *
 * Board setup routines for IBM Chestnut
 *
 * Author: <source@mvista.com>
 *
 * <2004> (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/console.h>
#include <linux/root_dev.h>
#include <linux/initrd.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/ide.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/mtd/physmap.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/time.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/hw_irq.h>
#include <asm/machdep.h>
#include <asm/kgdb.h>
#include <asm/bootinfo.h>
#include <asm/mv64x60.h>
#include <platforms/chestnut.h>

static void __iomem *sram_base; /* Virtual addr of Internal SRAM */
static void __iomem *cpld_base; /* Virtual addr of CPLD Regs */

static mv64x60_handle_t	bh;

extern void gen550_progress(char *, unsigned short);
extern void gen550_init(int, struct uart_port *);
extern void mv64360_pcibios_fixup(mv64x60_handle_t *bh);

#define BIT(x) (1<<x)
#define CHESTNUT_PRESERVE_MASK (BIT(MV64x60_CPU2DEV_0_WIN) | \
				BIT(MV64x60_CPU2DEV_1_WIN) | \
				BIT(MV64x60_CPU2DEV_2_WIN) | \
				BIT(MV64x60_CPU2DEV_3_WIN) | \
				BIT(MV64x60_CPU2BOOT_WIN))
/**************************************************************************
 * FUNCTION: chestnut_calibrate_decr
 *
 * DESCRIPTION: initialize decrementer interrupt frequency (used as system
 *              timer)
 *
 ****/
static void __init
chestnut_calibrate_decr(void)
{
	ulong freq;

	freq = CHESTNUT_BUS_SPEED / 4;

	printk("time_init: decrementer frequency = %lu.%.6lu MHz\n",
		freq/1000000, freq%1000000);

	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);
}

static int
chestnut_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: IBM\n");
	seq_printf(m, "machine\t\t: 750FX/GX Eval Board (Chestnut/Buckeye)\n");

	return 0;
}

/**************************************************************************
 * FUNCTION: chestnut_find_end_of_memory
 *
 * DESCRIPTION: ppc_md memory size callback
 *
 ****/
unsigned long __init
chestnut_find_end_of_memory(void)
{
   	static int  mem_size = 0;

   	if (mem_size == 0) {
      		mem_size = mv64x60_get_mem_size(CONFIG_MV64X60_NEW_BASE,
				MV64x60_TYPE_MV64460);
   	}
   	return mem_size;
}

#if defined(CONFIG_SERIAL_8250)
static void __init
chestnut_early_serial_map(void)
{
	struct uart_port port;

	/* Setup serial port access */
	memset(&port, 0, sizeof(port));
	port.uartclk = BASE_BAUD * 16;
	port.irq = UART0_INT;
	port.flags = STD_COM_FLAGS | UPF_IOREMAP;
	port.iotype = UPIO_MEM;
	port.mapbase = CHESTNUT_UART0_IO_BASE;
	port.regshift = 0;

	if (early_serial_setup(&port) != 0)
		printk("Early serial init of port 0 failed\n");

	/* Assume early_serial_setup() doesn't modify serial_req */
	port.line = 1;
	port.irq = UART1_INT;
	port.mapbase = CHESTNUT_UART1_IO_BASE;

	if (early_serial_setup(&port) != 0)
		printk("Early serial init of port 1 failed\n");
}
#endif

/**************************************************************************
 * FUNCTION: chestnut_map_irq
 *
 * DESCRIPTION: 0 return since PCI IRQs not needed
 *
 ****/
static int __init
chestnut_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] = {
		{CHESTNUT_PCI_SLOT0_IRQ, CHESTNUT_PCI_SLOT0_IRQ,
		 CHESTNUT_PCI_SLOT0_IRQ, CHESTNUT_PCI_SLOT0_IRQ},
		{CHESTNUT_PCI_SLOT1_IRQ, CHESTNUT_PCI_SLOT1_IRQ,
		 CHESTNUT_PCI_SLOT1_IRQ, CHESTNUT_PCI_SLOT1_IRQ},
		{CHESTNUT_PCI_SLOT2_IRQ, CHESTNUT_PCI_SLOT2_IRQ,
		 CHESTNUT_PCI_SLOT2_IRQ, CHESTNUT_PCI_SLOT2_IRQ},
		{CHESTNUT_PCI_SLOT3_IRQ, CHESTNUT_PCI_SLOT3_IRQ,
		 CHESTNUT_PCI_SLOT3_IRQ, CHESTNUT_PCI_SLOT3_IRQ},
	};
	const long min_idsel = 1, max_idsel = 4, irqs_per_slot = 4;

	return PCI_IRQ_TABLE_LOOKUP;
}


/**************************************************************************
 * FUNCTION: chestnut_setup_bridge
 *
 * DESCRIPTION: initalize board-specific settings on the MV64360
 *
 ****/
static void __init
chestnut_setup_bridge(void)
{
	struct mv64x60_setup_info	si;
	int i;

   	if ( ppc_md.progress )
		ppc_md.progress("chestnut_setup_bridge: enter", 0);

	memset(&si, 0, sizeof(si));

	si.phys_reg_base = CONFIG_MV64X60_NEW_BASE;

	/* setup only PCI bus 0 (bus 1 not used) */
	si.pci_0.enable_bus = 1;
	si.pci_0.pci_io.cpu_base = CHESTNUT_PCI0_IO_PROC_ADDR;
	si.pci_0.pci_io.pci_base_hi = 0;
	si.pci_0.pci_io.pci_base_lo = CHESTNUT_PCI0_IO_PCI_ADDR;
	si.pci_0.pci_io.size = CHESTNUT_PCI0_IO_SIZE;
	si.pci_0.pci_io.swap = MV64x60_CPU2PCI_SWAP_NONE; /* no swapping */
	si.pci_0.pci_mem[0].cpu_base = CHESTNUT_PCI0_MEM_PROC_ADDR;
	si.pci_0.pci_mem[0].pci_base_hi = CHESTNUT_PCI0_MEM_PCI_HI_ADDR;
	si.pci_0.pci_mem[0].pci_base_lo = CHESTNUT_PCI0_MEM_PCI_LO_ADDR;
	si.pci_0.pci_mem[0].size = CHESTNUT_PCI0_MEM_SIZE;
	si.pci_0.pci_mem[0].swap = MV64x60_CPU2PCI_SWAP_NONE; /* no swapping */
	si.pci_0.pci_cmd_bits = 0;
	si.pci_0.latency_timer = 0x80;

	for (i=0; i<MV64x60_CPU2MEM_WINDOWS; i++) {
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

   	/* Lookup host bridge - on CPU 0 - no SMP support */
   	if (mv64x60_init(&bh, &si)) {
        	printk("\n\nPCI Bridge initialization failed!\n");
   	}

	pci_dram_offset = 0;
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = chestnut_map_irq;
	ppc_md.pci_exclude_device = mv64x60_pci_exclude_device;

	mv64x60_set_bus(&bh, 0, 0);
	bh.hose_a->first_busno = 0;
	bh.hose_a->last_busno = 0xff;
	bh.hose_a->last_busno = pciauto_bus_scan(bh.hose_a, 0);
}

void __init
chestnut_setup_peripherals(void)
{
   	mv64x60_set_32bit_window(&bh, MV64x60_CPU2BOOT_WIN,
			CHESTNUT_BOOT_8BIT_BASE, CHESTNUT_BOOT_8BIT_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2BOOT_WIN);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_0_WIN,
			CHESTNUT_32BIT_BASE, CHESTNUT_32BIT_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_0_WIN);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_1_WIN,
			CHESTNUT_CPLD_BASE, CHESTNUT_CPLD_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_1_WIN);
	cpld_base = ioremap(CHESTNUT_CPLD_BASE, CHESTNUT_CPLD_SIZE);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_2_WIN,
			CHESTNUT_UART_BASE, CHESTNUT_UART_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_2_WIN);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_3_WIN,
			CHESTNUT_FRAM_BASE, CHESTNUT_FRAM_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_3_WIN);

   	mv64x60_set_32bit_window(&bh, MV64x60_CPU2SRAM_WIN,
			CHESTNUT_INTERNAL_SRAM_BASE, MV64360_SRAM_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2SRAM_WIN);

#ifdef CONFIG_NOT_COHERENT_CACHE
   	mv64x60_write(&bh, MV64360_SRAM_CONFIG, 0x001600b0);
#else
   	mv64x60_write(&bh, MV64360_SRAM_CONFIG, 0x001600b2);
#endif
	sram_base = ioremap(CHESTNUT_INTERNAL_SRAM_BASE, MV64360_SRAM_SIZE);
   	memset(sram_base, 0, MV64360_SRAM_SIZE);

	/*
	 * Configure MPP pins for PCI DMA
	 *
	 * PCI Slot	GNT pin		REQ pin
	 *	0	MPP16		MPP17
	 *	1	MPP18		MPP19
	 *	2	MPP20		MPP21
	 *	3	MPP22		MPP23
	 */
	mv64x60_write(&bh, MV64x60_MPP_CNTL_2,
			(0x1 << 0)  |	/* MPPSel16 PCI0_GNT[0] */
			(0x1 << 4)  |	/* MPPSel17 PCI0_REQ[0] */
			(0x1 << 8)  |	/* MPPSel18 PCI0_GNT[1] */
			(0x1 << 12) |	/* MPPSel19 PCI0_REQ[1] */
			(0x1 << 16) |	/* MPPSel20 PCI0_GNT[2] */
			(0x1 << 20) |	/* MPPSel21 PCI0_REQ[2] */
			(0x1 << 24) |	/* MPPSel22 PCI0_GNT[3] */
			(0x1 << 28));	/* MPPSel23 PCI0_REQ[3] */
	/*
	 * Set unused MPP pins for output, as per schematic note
	 *
	 * Unused Pins: MPP01, MPP02, MPP04, MPP05, MPP06
	 *		MPP09, MPP10, MPP13, MPP14, MPP15
	 */
	mv64x60_clr_bits(&bh, MV64x60_MPP_CNTL_0,
			(0xf << 4)  |	/* MPPSel01 GPIO[1] */
			(0xf << 8)  |	/* MPPSel02 GPIO[2] */
			(0xf << 16) |	/* MPPSel04 GPIO[4] */
			(0xf << 20) |	/* MPPSel05 GPIO[5] */
			(0xf << 24));	/* MPPSel06 GPIO[6] */
	mv64x60_clr_bits(&bh, MV64x60_MPP_CNTL_1,
			(0xf << 4)  |	/* MPPSel09 GPIO[9] */
			(0xf << 8)  |	/* MPPSel10 GPIO[10] */
			(0xf << 20) |	/* MPPSel13 GPIO[13] */
			(0xf << 24) |	/* MPPSel14 GPIO[14] */
			(0xf << 28));	/* MPPSel15 GPIO[15] */
	mv64x60_set_bits(&bh, MV64x60_GPP_IO_CNTL, /* Output */
			BIT(1)  | BIT(2)  | BIT(4)  | BIT(5)  | BIT(6)  |
			BIT(9)  | BIT(10) | BIT(13) | BIT(14) | BIT(15));

   	/*
    	 * Configure the following MPP pins to indicate a level
    	 * triggered interrupt
    	 *
       	 * MPP24 - Board Reset (just map the MPP & GPP for chestnut_reset)
       	 * MPP25 - UART A  (high)
       	 * MPP26 - UART B  (high)
	 * MPP28 - PCI Slot 3 (low)
	 * MPP29 - PCI Slot 2 (low)
	 * MPP30 - PCI Slot 1 (low)
	 * MPP31 - PCI Slot 0 (low)
    	 */
        mv64x60_clr_bits(&bh, MV64x60_MPP_CNTL_3,
                        BIT(3) | BIT(2) | BIT(1) | BIT(0)	 | /* MPP 24 */
                        BIT(7) | BIT(6) | BIT(5) | BIT(4)	 | /* MPP 25 */
                        BIT(11) | BIT(10) | BIT(9) | BIT(8)	 | /* MPP 26 */
			BIT(19) | BIT(18) | BIT(17) | BIT(16)	 | /* MPP 28 */
			BIT(23) | BIT(22) | BIT(21) | BIT(20)	 | /* MPP 29 */
			BIT(27) | BIT(26) | BIT(25) | BIT(24)	 | /* MPP 30 */
			BIT(31) | BIT(30) | BIT(29) | BIT(28));    /* MPP 31 */

   	/*
	 * Define GPP 25 (high), 26 (high), 28 (low), 29 (low), 30 (low),
	 * 31 (low) interrupt polarity input signal and level triggered
    	 */
   	mv64x60_clr_bits(&bh, MV64x60_GPP_LEVEL_CNTL, BIT(25) | BIT(26));
   	mv64x60_set_bits(&bh, MV64x60_GPP_LEVEL_CNTL,
			BIT(28) | BIT(29) | BIT(30) | BIT(31));
   	mv64x60_clr_bits(&bh, MV64x60_GPP_IO_CNTL,
			BIT(25) | BIT(26) | BIT(28) | BIT(29) | BIT(30) |
			BIT(31));

   	/* Config GPP interrupt controller to respond to level trigger */
   	mv64x60_set_bits(&bh, MV64360_COMM_ARBITER_CNTL, BIT(10));

   	/*
    	 * Dismiss and then enable interrupt on GPP interrupt cause for CPU #0
    	 */
   	mv64x60_write(&bh, MV64x60_GPP_INTR_CAUSE,
			~(BIT(25) | BIT(26) | BIT(28) | BIT(29) | BIT(30) |
			  BIT(31)));
   	mv64x60_set_bits(&bh, MV64x60_GPP_INTR_MASK,
			BIT(25) | BIT(26) | BIT(28) | BIT(29) | BIT(30) |
			BIT(31));

   	/*
    	 * Dismiss and then enable interrupt on CPU #0 high cause register
    	 * BIT27 summarizes GPP interrupts 24-31
    	 */
   	mv64x60_set_bits(&bh, MV64360_IC_CPU0_INTR_MASK_HI, BIT(27));

   	if (ppc_md.progress)
		ppc_md.progress("chestnut_setup_bridge: exit", 0);
}

/**************************************************************************
 * FUNCTION: chestnut_setup_arch
 *
 * DESCRIPTION: ppc_md machine configuration callback
 *
 ****/
static void __init
chestnut_setup_arch(void)
{
	if (ppc_md.progress)
      		ppc_md.progress("chestnut_setup_arch: enter", 0);

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000 / HZ;

   	/* if the time base value is greater than bus freq/4 (the TB and
    	* decrementer tick rate) + signed integer rollover value, we
    	* can spend a fair amount of time waiting for the rollover to
    	* happen.  To get around this, initialize the time base register
    	* to a "safe" value.
    	*/
   	set_tb(0, 0);

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_SDA2;
#endif

   	/*
    	* Set up the L2CR register.
    	*/
 	_set_L2CR(_get_L2CR() | L2CR_L2E);

	chestnut_setup_bridge();
	chestnut_setup_peripherals();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

#if defined(CONFIG_SERIAL_8250)
	chestnut_early_serial_map();
#endif

	/* Identify the system */
	printk(KERN_INFO "System Identification: IBM 750FX/GX Eval Board\n");
	printk(KERN_INFO "IBM 750FX/GX port (C) 2004 MontaVista Software, Inc."
		" (source@mvista.com)\n");

	if (ppc_md.progress)
      		ppc_md.progress("chestnut_setup_arch: exit", 0);
}

#ifdef CONFIG_MTD_PHYSMAP
static struct mtd_partition ptbl;

static int __init
chestnut_setup_mtd(void)
{
	memset(&ptbl, 0, sizeof(ptbl));

	ptbl.name = "User FS";
	ptbl.size = CHESTNUT_32BIT_SIZE;

	physmap_map.size = CHESTNUT_32BIT_SIZE;
	physmap_set_partitions(&ptbl, 1);
	return 0;
}

arch_initcall(chestnut_setup_mtd);
#endif

/**************************************************************************
 * FUNCTION: chestnut_restart
 *
 * DESCRIPTION: ppc_md machine reset callback
 *              reset the board via the CPLD command register
 *
 ****/
static void
chestnut_restart(char *cmd)
{
	volatile ulong i = 10000000;

	local_irq_disable();

        /*
         * Set CPLD Reg 3 bit 0 to 1 to allow MPP signals on reset to work
         *
         * MPP24 - board reset
         */
   	writeb(0x1, cpld_base + 3);

	/* GPP pin tied to MPP earlier */
        mv64x60_set_bits(&bh, MV64x60_GPP_VALUE_SET, BIT(24));

   	while (i-- > 0);
   	panic("restart failed\n");
}

static void
chestnut_halt(void)
{
	local_irq_disable();
	for (;;);
	/* NOTREACHED */
}

static void
chestnut_power_off(void)
{
	chestnut_halt();
	/* NOTREACHED */
}

/**************************************************************************
 * FUNCTION: chestnut_map_io
 *
 * DESCRIPTION: configure fixed memory-mapped IO
 *
 ****/
static void __init
chestnut_map_io(void)
{
#if defined(CONFIG_SERIAL_TEXT_DEBUG) || defined(CONFIG_KGDB)
	io_block_mapping(CHESTNUT_UART_BASE, CHESTNUT_UART_BASE, 0x100000,
		_PAGE_IO);
#endif
}

/**************************************************************************
 * FUNCTION: chestnut_set_bat
 *
 * DESCRIPTION: configures a (temporary) bat mapping for early access to
 *              device I/O
 *
 ****/
static __inline__ void
chestnut_set_bat(void)
{
        mb();
        mtspr(SPRN_DBAT3U, 0xf0001ffe);
        mtspr(SPRN_DBAT3L, 0xf000002a);
        mb();
}

/**************************************************************************
 * FUNCTION: platform_init
 *
 * DESCRIPTION: main entry point for configuring board-specific machine
 *              callbacks
 *
 ****/
void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

        /* Copy the kernel command line arguments to a safe place. */

        if (r6) {
                *(char *) (r7 + KERNELBASE) = 0;
                strcpy(cmd_line, (char *) (r6 + KERNELBASE));
        }

	isa_mem_base = 0;

	ppc_md.setup_arch = chestnut_setup_arch;
	ppc_md.show_cpuinfo = chestnut_show_cpuinfo;
	ppc_md.init_IRQ = mv64360_init_irq;
	ppc_md.get_irq = mv64360_get_irq;
	ppc_md.init = NULL;

	ppc_md.find_end_of_memory = chestnut_find_end_of_memory;
	ppc_md.setup_io_mappings  = chestnut_map_io;

	ppc_md.restart = chestnut_restart;
   	ppc_md.power_off = chestnut_power_off;
   	ppc_md.halt = chestnut_halt;

	ppc_md.time_init = NULL;
	ppc_md.set_rtc_time = NULL;
	ppc_md.get_rtc_time = NULL;
	ppc_md.calibrate_decr = chestnut_calibrate_decr;

	ppc_md.nvram_read_val = NULL;
	ppc_md.nvram_write_val = NULL;

	ppc_md.heartbeat = NULL;

	bh.p_base = CONFIG_MV64X60_NEW_BASE;

	chestnut_set_bat();

#if defined(CONFIG_SERIAL_TEXT_DEBUG)
	ppc_md.progress = gen550_progress;
#endif
#if defined(CONFIG_KGDB)
	ppc_md.kgdb_map_scc = gen550_kgdb_map_scc;
#endif

	if (ppc_md.progress)
                ppc_md.progress("chestnut_init(): exit", 0);
}
