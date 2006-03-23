/*
 * Board setup routines for the Sky Computers HDPU Compute Blade.
 *
 * Written by Brian Waite <waite@skycomputers.com>
 *
 * Based on code done by - Mark A. Greer <mgreer@mvista.com>
 *                         Rabeeh Khoury - rabeeh@galileo.co.il
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/config.h>

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/ide.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>

#include <linux/initrd.h>
#include <linux/root_dev.h>
#include <linux/smp.h>

#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/todc.h>
#include <asm/mv64x60.h>
#include <asm/ppcboot.h>
#include <platforms/hdpu.h>
#include <linux/mv643xx.h>
#include <linux/hdpu_features.h>
#include <linux/device.h>
#include <linux/mtd/physmap.h>

#define BOARD_VENDOR	"Sky Computers"
#define BOARD_MACHINE	"HDPU-CB-A"

bd_t ppcboot_bd;
int ppcboot_bd_valid = 0;

static mv64x60_handle_t bh;

extern char cmd_line[];

unsigned long hdpu_find_end_of_memory(void);
void hdpu_mpsc_progress(char *s, unsigned short hex);
void hdpu_heartbeat(void);

static void parse_bootinfo(unsigned long r3,
			   unsigned long r4, unsigned long r5,
			   unsigned long r6, unsigned long r7);
static void hdpu_set_l1pe(void);
static void hdpu_cpustate_set(unsigned char new_state);
#ifdef CONFIG_SMP
static DEFINE_SPINLOCK(timebase_lock);
static unsigned int timebase_upper = 0, timebase_lower = 0;
extern int smp_tb_synchronized;

void __devinit hdpu_tben_give(void);
void __devinit hdpu_tben_take(void);
#endif

static int __init
hdpu_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	struct pci_controller *hose = pci_bus_to_hose(dev->bus->number);

	if (hose->index == 0) {
		static char pci_irq_table[][4] = {
			{HDPU_PCI_0_IRQ, 0, 0, 0},
			{HDPU_PCI_0_IRQ, 0, 0, 0},
		};

		const long min_idsel = 1, max_idsel = 2, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	} else {
		static char pci_irq_table[][4] = {
			{HDPU_PCI_1_IRQ, 0, 0, 0},
		};

		const long min_idsel = 1, max_idsel = 1, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	}
}

static void __init hdpu_intr_setup(void)
{
	mv64x60_write(&bh, MV64x60_GPP_IO_CNTL,
		      (1 | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) |
		       (1 << 6) | (1 << 7) | (1 << 12) | (1 << 16) |
		       (1 << 18) | (1 << 19) | (1 << 20) | (1 << 21) |
		       (1 << 22) | (1 << 23) | (1 << 24) | (1 << 25) |
		       (1 << 26) | (1 << 27) | (1 << 28) | (1 << 29)));

	/* XXXX Erranum FEr PCI-#8 */
	mv64x60_clr_bits(&bh, MV64x60_PCI0_CMD, (1 << 5) | (1 << 9));
	mv64x60_clr_bits(&bh, MV64x60_PCI1_CMD, (1 << 5) | (1 << 9));

	/*
	 * Dismiss and then enable interrupt on GPP interrupt cause
	 * for CPU #0
	 */
	mv64x60_write(&bh, MV64x60_GPP_INTR_CAUSE, ~((1 << 8) | (1 << 13)));
	mv64x60_set_bits(&bh, MV64x60_GPP_INTR_MASK, (1 << 8) | (1 << 13));

	/*
	 * Dismiss and then enable interrupt on CPU #0 high cause reg
	 * BIT25 summarizes GPP interrupts 8-15
	 */
	mv64x60_set_bits(&bh, MV64360_IC_CPU0_INTR_MASK_HI, (1 << 25));
}

static void __init hdpu_setup_peripherals(void)
{
	unsigned int val;

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2BOOT_WIN,
				 HDPU_EMB_FLASH_BASE, HDPU_EMB_FLASH_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2BOOT_WIN);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_0_WIN,
				 HDPU_TBEN_BASE, HDPU_TBEN_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_0_WIN);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2DEV_1_WIN,
				 HDPU_NEXUS_ID_BASE, HDPU_NEXUS_ID_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2DEV_1_WIN);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2SRAM_WIN,
				 HDPU_INTERNAL_SRAM_BASE,
				 HDPU_INTERNAL_SRAM_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2SRAM_WIN);

	bh.ci->disable_window_32bit(&bh, MV64x60_ENET2MEM_4_WIN);
	mv64x60_set_32bit_window(&bh, MV64x60_ENET2MEM_4_WIN, 0, 0, 0);

	mv64x60_clr_bits(&bh, MV64x60_PCI0_PCI_DECODE_CNTL, (1 << 3));
	mv64x60_clr_bits(&bh, MV64x60_PCI1_PCI_DECODE_CNTL, (1 << 3));
	mv64x60_clr_bits(&bh, MV64x60_TIMR_CNTR_0_3_CNTL,
			 ((1 << 0) | (1 << 8) | (1 << 16) | (1 << 24)));

	/* Enable pipelining */
	mv64x60_set_bits(&bh, MV64x60_CPU_CONFIG, (1 << 13));
	/* Enable Snoop Pipelineing */
	mv64x60_set_bits(&bh, MV64360_D_UNIT_CONTROL_HIGH, (1 << 24));

	/*
	 * Change DRAM read buffer assignment.
	 * Assign read buffer 0 dedicated only for CPU,
	 * and the rest read buffer 1.
	 */
	val = mv64x60_read(&bh, MV64360_SDRAM_CONFIG);
	val = val & 0x03ffffff;
	val = val | 0xf8000000;
	mv64x60_write(&bh, MV64360_SDRAM_CONFIG, val);

	/*
	 * Configure internal SRAM -
	 * Cache coherent write back, if CONFIG_MV64360_SRAM_CACHE_COHERENT set
	 * Parity enabled.
	 * Parity error propagation
	 * Arbitration not parked for CPU only
	 * Other bits are reserved.
	 */
#ifdef CONFIG_MV64360_SRAM_CACHE_COHERENT
	mv64x60_write(&bh, MV64360_SRAM_CONFIG, 0x001600b2);
#else
	mv64x60_write(&bh, MV64360_SRAM_CONFIG, 0x001600b0);
#endif

	hdpu_intr_setup();
}

static void __init hdpu_setup_bridge(void)
{
	struct mv64x60_setup_info si;
	int i;

	memset(&si, 0, sizeof(si));

	si.phys_reg_base = HDPU_BRIDGE_REG_BASE;
	si.pci_0.enable_bus = 1;
	si.pci_0.pci_io.cpu_base = HDPU_PCI0_IO_START_PROC_ADDR;
	si.pci_0.pci_io.pci_base_hi = 0;
	si.pci_0.pci_io.pci_base_lo = HDPU_PCI0_IO_START_PCI_ADDR;
	si.pci_0.pci_io.size = HDPU_PCI0_IO_SIZE;
	si.pci_0.pci_io.swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_0.pci_mem[0].cpu_base = HDPU_PCI0_MEM_START_PROC_ADDR;
	si.pci_0.pci_mem[0].pci_base_hi = HDPU_PCI0_MEM_START_PCI_HI_ADDR;
	si.pci_0.pci_mem[0].pci_base_lo = HDPU_PCI0_MEM_START_PCI_LO_ADDR;
	si.pci_0.pci_mem[0].size = HDPU_PCI0_MEM_SIZE;
	si.pci_0.pci_mem[0].swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_0.pci_cmd_bits = 0;
	si.pci_0.latency_timer = 0x80;

	si.pci_1.enable_bus = 1;
	si.pci_1.pci_io.cpu_base = HDPU_PCI1_IO_START_PROC_ADDR;
	si.pci_1.pci_io.pci_base_hi = 0;
	si.pci_1.pci_io.pci_base_lo = HDPU_PCI1_IO_START_PCI_ADDR;
	si.pci_1.pci_io.size = HDPU_PCI1_IO_SIZE;
	si.pci_1.pci_io.swap = MV64x60_CPU2PCI_SWAP_NONE;
	si.pci_1.pci_mem[0].cpu_base = HDPU_PCI1_MEM_START_PROC_ADDR;
	si.pci_1.pci_mem[0].pci_base_hi = HDPU_PCI1_MEM_START_PCI_HI_ADDR;
	si.pci_1.pci_mem[0].pci_base_lo = HDPU_PCI1_MEM_START_PCI_LO_ADDR;
	si.pci_1.pci_mem[0].size = HDPU_PCI1_MEM_SIZE;
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

		si.pci_0.acc_cntl_options[i] =
		    MV64360_PCI_ACC_CNTL_SNOOP_NONE |
		    MV64360_PCI_ACC_CNTL_SWAP_NONE |
		    MV64360_PCI_ACC_CNTL_MBURST_128_BYTES |
		    MV64360_PCI_ACC_CNTL_RDSIZE_256_BYTES;

#else
		si.cpu_prot_options[i] = 0;
		si.enet_options[i] = MV64360_ENET2MEM_SNOOP_WB;	/* errata */
		si.mpsc_options[i] = MV64360_MPSC2MEM_SNOOP_WB;	/* errata */
		si.idma_options[i] = MV64360_IDMA2MEM_SNOOP_WB;	/* errata */

		si.pci_0.acc_cntl_options[i] =
		    MV64360_PCI_ACC_CNTL_SNOOP_WB |
		    MV64360_PCI_ACC_CNTL_SWAP_NONE |
		    MV64360_PCI_ACC_CNTL_MBURST_32_BYTES |
		    MV64360_PCI_ACC_CNTL_RDSIZE_256_BYTES;

		si.pci_1.acc_cntl_options[i] =
		    MV64360_PCI_ACC_CNTL_SNOOP_WB |
		    MV64360_PCI_ACC_CNTL_SWAP_NONE |
		    MV64360_PCI_ACC_CNTL_MBURST_32_BYTES |
		    MV64360_PCI_ACC_CNTL_RDSIZE_256_BYTES;
#endif
	}

	hdpu_cpustate_set(CPUSTATE_KERNEL_MAJOR | CPUSTATE_KERNEL_INIT_PCI);

	/* Lookup PCI host bridges */
	mv64x60_init(&bh, &si);
	pci_dram_offset = 0;	/* System mem at same addr on PCI & cpu bus */
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = hdpu_map_irq;

	mv64x60_set_bus(&bh, 0, 0);
	bh.hose_a->first_busno = 0;
	bh.hose_a->last_busno = 0xff;
	bh.hose_a->last_busno = pciauto_bus_scan(bh.hose_a, 0);

	bh.hose_b->first_busno = bh.hose_a->last_busno + 1;
	mv64x60_set_bus(&bh, 1, bh.hose_b->first_busno);
	bh.hose_b->last_busno = 0xff;
	bh.hose_b->last_busno = pciauto_bus_scan(bh.hose_b,
		bh.hose_b->first_busno);

	ppc_md.pci_exclude_device = mv64x60_pci_exclude_device;

	hdpu_cpustate_set(CPUSTATE_KERNEL_MAJOR | CPUSTATE_KERNEL_INIT_REG);
	/*
	 * Enabling of PCI internal-vs-external arbitration
	 * is a platform- and errata-dependent decision.
	 */
	return;
}

#if defined(CONFIG_SERIAL_MPSC_CONSOLE)
static void __init hdpu_early_serial_map(void)
{
#ifdef	CONFIG_KGDB
	static char first_time = 1;

#if defined(CONFIG_KGDB_TTYS0)
#define KGDB_PORT 0
#elif defined(CONFIG_KGDB_TTYS1)
#define KGDB_PORT 1
#else
#error "Invalid kgdb_tty port"
#endif

	if (first_time) {
		gt_early_mpsc_init(KGDB_PORT,
				   B9600 | CS8 | CREAD | HUPCL | CLOCAL);
		first_time = 0;
	}

	return;
#endif
}
#endif

static void hdpu_init2(void)
{
	return;
}

#if defined(CONFIG_MV643XX_ETH)
static void __init hdpu_fixup_eth_pdata(struct platform_device *pd)
{

	struct mv643xx_eth_platform_data *eth_pd;
	eth_pd = pd->dev.platform_data;

	eth_pd->force_phy_addr = 1;
	eth_pd->phy_addr = pd->id;
	eth_pd->speed = SPEED_100;
	eth_pd->duplex = DUPLEX_FULL;
	eth_pd->tx_queue_size = 400;
	eth_pd->rx_queue_size = 800;
}
#endif

static void __init hdpu_fixup_mpsc_pdata(struct platform_device *pd)
{

	struct mpsc_pdata *pdata;

	pdata = (struct mpsc_pdata *)pd->dev.platform_data;

	pdata->max_idle = 40;
	if (ppcboot_bd_valid)
		pdata->default_baud = ppcboot_bd.bi_baudrate;
	else
		pdata->default_baud = HDPU_DEFAULT_BAUD;
	pdata->brg_clk_src = HDPU_MPSC_CLK_SRC;
	pdata->brg_clk_freq = HDPU_MPSC_CLK_FREQ;
}

#if defined(CONFIG_HDPU_FEATURES)
static void __init hdpu_fixup_cpustate_pdata(struct platform_device *pd)
{
	struct platform_device *pds[1];
	pds[0] = pd;
	mv64x60_pd_fixup(&bh, pds, 1);
}
#endif

static int hdpu_platform_notify(struct device *dev)
{
	static struct {
		char *bus_id;
		void ((*rtn) (struct platform_device * pdev));
	} dev_map[] = {
		{
		MPSC_CTLR_NAME ".0", hdpu_fixup_mpsc_pdata},
#if defined(CONFIG_MV643XX_ETH)
		{
		MV643XX_ETH_NAME ".0", hdpu_fixup_eth_pdata},
#endif
#if defined(CONFIG_HDPU_FEATURES)
		{
		HDPU_CPUSTATE_NAME ".0", hdpu_fixup_cpustate_pdata},
#endif
	};
	struct platform_device *pdev;
	int i;

	if (dev && dev->bus_id)
		for (i = 0; i < ARRAY_SIZE(dev_map); i++)
			if (!strncmp(dev->bus_id, dev_map[i].bus_id,
				     BUS_ID_SIZE)) {

				pdev = container_of(dev,
						    struct platform_device,
						    dev);
				dev_map[i].rtn(pdev);
			}

	return 0;
}

static void __init hdpu_setup_arch(void)
{
	if (ppc_md.progress)
		ppc_md.progress("hdpu_setup_arch: enter", 0);
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

	ppc_md.heartbeat = hdpu_heartbeat;

	ppc_md.heartbeat_reset = HZ;
	ppc_md.heartbeat_count = 1;

	if (ppc_md.progress)
		ppc_md.progress("hdpu_setup_arch: Enabling L2 cache", 0);

	/* Enable L1 Parity Bits */
	hdpu_set_l1pe();

	/* Enable L2 and L3 caches (if 745x) */
	_set_L2CR(0x80080000);

	if (ppc_md.progress)
		ppc_md.progress("hdpu_setup_arch: enter", 0);

	hdpu_setup_bridge();

	hdpu_setup_peripherals();

#ifdef CONFIG_SERIAL_MPSC_CONSOLE
	hdpu_early_serial_map();
#endif

	printk("SKY HDPU Compute Blade \n");

	if (ppc_md.progress)
		ppc_md.progress("hdpu_setup_arch: exit", 0);

	hdpu_cpustate_set(CPUSTATE_KERNEL_MAJOR | CPUSTATE_KERNEL_OK);
	return;
}
static void __init hdpu_init_irq(void)
{
	mv64360_init_irq();
}

static void __init hdpu_set_l1pe()
{
	unsigned long ictrl;
	asm volatile ("mfspr %0, 1011":"=r" (ictrl):);
	ictrl |= ICTRL_EICE | ICTRL_EDC | ICTRL_EICP;
	asm volatile ("mtspr 1011, %0"::"r" (ictrl));
}

/*
 * Set BAT 1 to map 0xf1000000 to end of physical memory space.
 */
static __inline__ void hdpu_set_bat(void)
{
	mb();
	mtspr(SPRN_DBAT1U, 0xf10001fe);
	mtspr(SPRN_DBAT1L, 0xf100002a);
	mb();

	return;
}

unsigned long __init hdpu_find_end_of_memory(void)
{
	return mv64x60_get_mem_size(CONFIG_MV64X60_NEW_BASE,
				    MV64x60_TYPE_MV64360);
}

static void hdpu_reset_board(void)
{
	volatile int infinite = 1;

	hdpu_cpustate_set(CPUSTATE_KERNEL_MAJOR | CPUSTATE_KERNEL_RESET);

	local_irq_disable();

	/* Clear all the LEDs */
	mv64x60_write(&bh, MV64x60_GPP_VALUE_CLR, ((1 << 4) |
						   (1 << 5) | (1 << 6)));

	/* disable and invalidate the L2 cache */
	_set_L2CR(0);
	_set_L2CR(0x200000);

	/* flush and disable L1 I/D cache */
	__asm__ __volatile__
	    ("\n"
	     "mfspr   3,1008\n"
	     "ori	5,5,0xcc00\n"
	     "ori	4,3,0xc00\n"
	     "andc	5,3,5\n"
	     "sync\n"
	     "mtspr	1008,4\n"
	     "isync\n" "sync\n" "mtspr	1008,5\n" "isync\n" "sync\n");

	/* Hit the reset bit */
	mv64x60_write(&bh, MV64x60_GPP_VALUE_CLR, (1 << 3));

	while (infinite)
		infinite = infinite;

	return;
}

static void hdpu_restart(char *cmd)
{
	volatile ulong i = 10000000;

	hdpu_reset_board();

	while (i-- > 0) ;
	panic("restart failed\n");
}

static void hdpu_halt(void)
{
	local_irq_disable();

	hdpu_cpustate_set(CPUSTATE_KERNEL_MAJOR | CPUSTATE_KERNEL_HALT);

	/* Clear all the LEDs */
	mv64x60_write(&bh, MV64x60_GPP_VALUE_CLR, ((1 << 4) | (1 << 5) |
						   (1 << 6)));
	while (1) ;
	/* NOTREACHED */
}

static void hdpu_power_off(void)
{
	hdpu_halt();
	/* NOTREACHED */
}

static int hdpu_show_cpuinfo(struct seq_file *m)
{
	uint pvid;

	pvid = mfspr(SPRN_PVR);
	seq_printf(m, "vendor\t\t: Sky Computers\n");
	seq_printf(m, "machine\t\t: HDPU Compute Blade\n");
	seq_printf(m, "PVID\t\t: 0x%x, vendor: %s\n",
		   pvid, (pvid & (1 << 15) ? "IBM" : "Motorola"));

	return 0;
}

static void __init hdpu_calibrate_decr(void)
{
	ulong freq;

	if (ppcboot_bd_valid)
		freq = ppcboot_bd.bi_busfreq / 4;
	else
		freq = 133000000;

	printk("time_init: decrementer frequency = %lu.%.6lu MHz\n",
	       freq / 1000000, freq % 1000000);

	tb_ticks_per_jiffy = freq / HZ;
	tb_to_us = mulhwu_scale_factor(freq, 1000000);

	return;
}

static void parse_bootinfo(unsigned long r3,
			   unsigned long r4, unsigned long r5,
			   unsigned long r6, unsigned long r7)
{
	bd_t *bd = NULL;
	char *cmdline_start = NULL;
	int cmdline_len = 0;

	if (r3) {
		if ((r3 & 0xf0000000) == 0)
			r3 += KERNELBASE;
		if ((r3 & 0xf0000000) == KERNELBASE) {
			bd = (void *)r3;

			memcpy(&ppcboot_bd, bd, sizeof(ppcboot_bd));
			ppcboot_bd_valid = 1;
		}
	}
#ifdef CONFIG_BLK_DEV_INITRD
	if (r4 && r5 && r5 > r4) {
		if ((r4 & 0xf0000000) == 0)
			r4 += KERNELBASE;
		if ((r5 & 0xf0000000) == 0)
			r5 += KERNELBASE;
		if ((r4 & 0xf0000000) == KERNELBASE) {
			initrd_start = r4;
			initrd_end = r5;
			initrd_below_start_ok = 1;
		}
	}
#endif				/* CONFIG_BLK_DEV_INITRD */

	if (r6 && r7 && r7 > r6) {
		if ((r6 & 0xf0000000) == 0)
			r6 += KERNELBASE;
		if ((r7 & 0xf0000000) == 0)
			r7 += KERNELBASE;
		if ((r6 & 0xf0000000) == KERNELBASE) {
			cmdline_start = (void *)r6;
			cmdline_len = (r7 - r6);
			strncpy(cmd_line, cmdline_start, cmdline_len);
		}
	}
}

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
static void
hdpu_ide_request_region(ide_ioreg_t from, unsigned int extent, const char *name)
{
	request_region(from, extent, name);
	return;
}

static void hdpu_ide_release_region(ide_ioreg_t from, unsigned int extent)
{
	release_region(from, extent);
	return;
}

static void __init
hdpu_ide_pci_init_hwif_ports(hw_regs_t * hw, ide_ioreg_t data_port,
			     ide_ioreg_t ctrl_port, int *irq)
{
	struct pci_dev *dev;

	pci_for_each_dev(dev) {
		if (((dev->class >> 8) == PCI_CLASS_STORAGE_IDE) ||
		    ((dev->class >> 8) == PCI_CLASS_STORAGE_RAID)) {
			hw->irq = dev->irq;

			if (irq != NULL) {
				*irq = dev->irq;
			}
		}
	}

	return;
}
#endif

void hdpu_heartbeat(void)
{
	if (mv64x60_read(&bh, MV64x60_GPP_VALUE) & (1 << 5))
		mv64x60_write(&bh, MV64x60_GPP_VALUE_CLR, (1 << 5));
	else
		mv64x60_write(&bh, MV64x60_GPP_VALUE_SET, (1 << 5));

	ppc_md.heartbeat_count = ppc_md.heartbeat_reset;

}

static void __init hdpu_map_io(void)
{
	io_block_mapping(0xf1000000, 0xf1000000, 0x20000, _PAGE_IO);
}

#ifdef CONFIG_SMP
char hdpu_smp0[] = "SMP Cpu #0";
char hdpu_smp1[] = "SMP Cpu #1";

static irqreturn_t hdpu_smp_cpu0_int_handler(int irq, void *dev_id,
					     struct pt_regs *regs)
{
	volatile unsigned int doorbell;

	doorbell = mv64x60_read(&bh, MV64360_CPU0_DOORBELL);

	/* Ack the doorbell interrupts */
	mv64x60_write(&bh, MV64360_CPU0_DOORBELL_CLR, doorbell);

	if (doorbell & 1) {
		smp_message_recv(0, regs);
	}
	if (doorbell & 2) {
		smp_message_recv(1, regs);
	}
	if (doorbell & 4) {
		smp_message_recv(2, regs);
	}
	if (doorbell & 8) {
		smp_message_recv(3, regs);
	}
	return IRQ_HANDLED;
}

static irqreturn_t hdpu_smp_cpu1_int_handler(int irq, void *dev_id,
					     struct pt_regs *regs)
{
	volatile unsigned int doorbell;

	doorbell = mv64x60_read(&bh, MV64360_CPU1_DOORBELL);

	/* Ack the doorbell interrupts */
	mv64x60_write(&bh, MV64360_CPU1_DOORBELL_CLR, doorbell);

	if (doorbell & 1) {
		smp_message_recv(0, regs);
	}
	if (doorbell & 2) {
		smp_message_recv(1, regs);
	}
	if (doorbell & 4) {
		smp_message_recv(2, regs);
	}
	if (doorbell & 8) {
		smp_message_recv(3, regs);
	}
	return IRQ_HANDLED;
}

static void smp_hdpu_CPU_two(void)
{
	__asm__ __volatile__
	    ("\n"
	     "lis     3,0x0000\n"
	     "ori     3,3,0x00c0\n"
	     "mtspr   26, 3\n" "li      4,0\n" "mtspr   27,4\n" "rfi");

}

static int smp_hdpu_probe(void)
{
	int *cpu_count_reg;
	int num_cpus = 0;

	cpu_count_reg = ioremap(HDPU_NEXUS_ID_BASE, HDPU_NEXUS_ID_SIZE);
	if (cpu_count_reg) {
		num_cpus = (*cpu_count_reg >> 20) & 0x3;
		iounmap(cpu_count_reg);
	}

	/* Validate the bits in the CPLD. If we could not map the reg, return 2.
	 * If the register reported 0 or 3, return 2.
	 * Older CPLD revisions set these bits to all ones (val = 3).
	 */
	if ((num_cpus < 1) || (num_cpus > 2)) {
		printk
		    ("Unable to determine the number of processors %d . deafulting to 2.\n",
		     num_cpus);
		num_cpus = 2;
	}
	return num_cpus;
}

static void
smp_hdpu_message_pass(int target, int msg)
{
	if (msg > 0x3) {
		printk("SMP %d: smp_message_pass: unknown msg %d\n",
		       smp_processor_id(), msg);
		return;
	}
	switch (target) {
	case MSG_ALL:
		mv64x60_write(&bh, MV64360_CPU0_DOORBELL, 1 << msg);
		mv64x60_write(&bh, MV64360_CPU1_DOORBELL, 1 << msg);
		break;
	case MSG_ALL_BUT_SELF:
		if (smp_processor_id())
			mv64x60_write(&bh, MV64360_CPU0_DOORBELL, 1 << msg);
		else
			mv64x60_write(&bh, MV64360_CPU1_DOORBELL, 1 << msg);
		break;
	default:
		if (target == 0)
			mv64x60_write(&bh, MV64360_CPU0_DOORBELL, 1 << msg);
		else
			mv64x60_write(&bh, MV64360_CPU1_DOORBELL, 1 << msg);
		break;
	}
}

static void smp_hdpu_kick_cpu(int nr)
{
	volatile unsigned int *bootaddr;

	if (ppc_md.progress)
		ppc_md.progress("smp_hdpu_kick_cpu", 0);

	hdpu_cpustate_set(CPUSTATE_KERNEL_MAJOR | CPUSTATE_KERNEL_CPU1_KICK);

       /* Disable BootCS. Must also reduce the windows size to zero. */
	bh.ci->disable_window_32bit(&bh, MV64x60_CPU2BOOT_WIN);
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2BOOT_WIN, 0, 0, 0);

	bootaddr = ioremap(HDPU_INTERNAL_SRAM_BASE, HDPU_INTERNAL_SRAM_SIZE);
	if (!bootaddr) {
		if (ppc_md.progress)
			ppc_md.progress("smp_hdpu_kick_cpu: ioremap failed", 0);
		return;
	}

	memcpy((void *)(bootaddr + 0x40), (void *)&smp_hdpu_CPU_two, 0x20);

	/* map SRAM to 0xfff00000 */
	bh.ci->disable_window_32bit(&bh, MV64x60_CPU2SRAM_WIN);

	mv64x60_set_32bit_window(&bh, MV64x60_CPU2SRAM_WIN,
				 0xfff00000, HDPU_INTERNAL_SRAM_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2SRAM_WIN);

	/* Enable CPU1 arbitration */
	mv64x60_clr_bits(&bh, MV64x60_CPU_MASTER_CNTL, (1 << 9));

	/*
	 * Wait 100mSecond until other CPU has reached __secondary_start.
	 * When it reaches, it is permittable to rever the SRAM mapping etc...
	 */
	mdelay(100);
	*(unsigned long *)KERNELBASE = nr;
	asm volatile ("dcbf 0,%0"::"r" (KERNELBASE):"memory");

	iounmap(bootaddr);

	/* Set up window for internal sram (256KByte insize) */
	bh.ci->disable_window_32bit(&bh, MV64x60_CPU2SRAM_WIN);
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2SRAM_WIN,
				 HDPU_INTERNAL_SRAM_BASE,
				 HDPU_INTERNAL_SRAM_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2SRAM_WIN);
	/*
	 * Set up windows for embedded FLASH (using boot CS window).
	 */

	bh.ci->disable_window_32bit(&bh, MV64x60_CPU2BOOT_WIN);
	mv64x60_set_32bit_window(&bh, MV64x60_CPU2BOOT_WIN,
				 HDPU_EMB_FLASH_BASE, HDPU_EMB_FLASH_SIZE, 0);
	bh.ci->enable_window_32bit(&bh, MV64x60_CPU2BOOT_WIN);
}

static void smp_hdpu_setup_cpu(int cpu_nr)
{
	if (cpu_nr == 0) {
		if (ppc_md.progress)
			ppc_md.progress("smp_hdpu_setup_cpu 0", 0);
		mv64x60_write(&bh, MV64360_CPU0_DOORBELL_CLR, 0xff);
		mv64x60_write(&bh, MV64360_CPU0_DOORBELL_MASK, 0xff);
		request_irq(60, hdpu_smp_cpu0_int_handler,
			    SA_INTERRUPT, hdpu_smp0, 0);
	}

	if (cpu_nr == 1) {
		if (ppc_md.progress)
			ppc_md.progress("smp_hdpu_setup_cpu 1", 0);

		hdpu_cpustate_set(CPUSTATE_KERNEL_MAJOR |
				  CPUSTATE_KERNEL_CPU1_OK);

		/* Enable L1 Parity Bits */
		hdpu_set_l1pe();

		/* Enable L2 cache */
		_set_L2CR(0);
		_set_L2CR(0x80080000);

		mv64x60_write(&bh, MV64360_CPU1_DOORBELL_CLR, 0x0);
		mv64x60_write(&bh, MV64360_CPU1_DOORBELL_MASK, 0xff);
		request_irq(28, hdpu_smp_cpu1_int_handler,
			    SA_INTERRUPT, hdpu_smp1, 0);
	}

}

void __devinit hdpu_tben_give()
{
	volatile unsigned long *val = 0;

	/* By writing 0 to the TBEN_BASE, the timebases is frozen */
	val = ioremap(HDPU_TBEN_BASE, 4);
	*val = 0;
	mb();

	spin_lock(&timebase_lock);
	timebase_upper = get_tbu();
	timebase_lower = get_tbl();
	spin_unlock(&timebase_lock);

	while (timebase_upper || timebase_lower)
		barrier();

	/* By writing 1 to the TBEN_BASE, the timebases is thawed */
	*val = 1;
	mb();

	iounmap(val);

}

void __devinit hdpu_tben_take()
{
	while (!(timebase_upper || timebase_lower))
		barrier();

	spin_lock(&timebase_lock);
	set_tb(timebase_upper, timebase_lower);
	timebase_upper = 0;
	timebase_lower = 0;
	spin_unlock(&timebase_lock);
}

static struct smp_ops_t hdpu_smp_ops = {
	.message_pass = smp_hdpu_message_pass,
	.probe = smp_hdpu_probe,
	.kick_cpu = smp_hdpu_kick_cpu,
	.setup_cpu = smp_hdpu_setup_cpu,
	.give_timebase = hdpu_tben_give,
	.take_timebase = hdpu_tben_take,
};
#endif				/* CONFIG_SMP */

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(r3, r4, r5, r6, r7);

	isa_mem_base = 0;

	ppc_md.setup_arch = hdpu_setup_arch;
	ppc_md.init = hdpu_init2;
	ppc_md.show_cpuinfo = hdpu_show_cpuinfo;
	ppc_md.init_IRQ = hdpu_init_irq;
	ppc_md.get_irq = mv64360_get_irq;
	ppc_md.restart = hdpu_restart;
	ppc_md.power_off = hdpu_power_off;
	ppc_md.halt = hdpu_halt;
	ppc_md.find_end_of_memory = hdpu_find_end_of_memory;
	ppc_md.calibrate_decr = hdpu_calibrate_decr;
	ppc_md.setup_io_mappings = hdpu_map_io;

	bh.p_base = CONFIG_MV64X60_NEW_BASE;
	bh.v_base = (unsigned long *)bh.p_base;

	hdpu_set_bat();

#if defined(CONFIG_SERIAL_TEXT_DEBUG)
	ppc_md.progress = hdpu_mpsc_progress;	/* embedded UART */
	mv64x60_progress_init(bh.p_base);
#endif				/* CONFIG_SERIAL_TEXT_DEBUG */

#ifdef CONFIG_SMP
	smp_ops = &hdpu_smp_ops;
#endif				/* CONFIG_SMP */

#if defined(CONFIG_SERIAL_MPSC) || defined(CONFIG_MV643XX_ETH)
	platform_notify = hdpu_platform_notify;
#endif
	return;
}

#if defined(CONFIG_SERIAL_TEXT_DEBUG) && defined(CONFIG_SERIAL_MPSC_CONSOLE)
/* SMP safe version of the serial text debug routine. Uses Semaphore 0 */
void hdpu_mpsc_progress(char *s, unsigned short hex)
{
	while (mv64x60_read(&bh, MV64360_WHO_AM_I) !=
	       mv64x60_read(&bh, MV64360_SEMAPHORE_0)) {
	}
	mv64x60_mpsc_progress(s, hex);
	mv64x60_write(&bh, MV64360_SEMAPHORE_0, 0xff);
}
#endif

static void hdpu_cpustate_set(unsigned char new_state)
{
	unsigned int state = (new_state << 21);
	mv64x60_write(&bh, MV64x60_GPP_VALUE_CLR, (0xff << 21));
	mv64x60_write(&bh, MV64x60_GPP_VALUE_CLR, state);
}

#ifdef CONFIG_MTD_PHYSMAP
static struct mtd_partition hdpu_partitions[] = {
	{
	 .name = "Root FS",
	 .size = 0x03400000,
	 .offset = 0,
	 .mask_flags = 0,
	 },{
	 .name = "User FS",
	 .size = 0x00800000,
	 .offset = 0x03400000,
	 .mask_flags = 0,
	 },{
	 .name = "Kernel Image",
	 .size = 0x002C0000,
	 .offset = 0x03C00000,
	 .mask_flags = 0,
	 },{
	 .name = "bootEnv",
	 .size = 0x00040000,
	 .offset = 0x03EC0000,
	 .mask_flags = 0,
	 },{
	 .name = "bootROM",
	 .size = 0x00100000,
	 .offset = 0x03F00000,
	 .mask_flags = 0,
	 }
};

static int __init hdpu_setup_mtd(void)
{

	physmap_set_partitions(hdpu_partitions, 5);
	return 0;
}

arch_initcall(hdpu_setup_mtd);
#endif

#ifdef CONFIG_HDPU_FEATURES

static struct resource hdpu_cpustate_resources[] = {
	[0] = {
	       .name = "addr base",
	       .start = MV64x60_GPP_VALUE_SET,
	       .end = MV64x60_GPP_VALUE_CLR + 1,
	       .flags = IORESOURCE_MEM,
	       },
};

static struct resource hdpu_nexus_resources[] = {
	[0] = {
	       .name = "nexus register",
	       .start = HDPU_NEXUS_ID_BASE,
	       .end = HDPU_NEXUS_ID_BASE + HDPU_NEXUS_ID_SIZE,
	       .flags = IORESOURCE_MEM,
	       },
};

static struct platform_device hdpu_cpustate_device = {
	.name = HDPU_CPUSTATE_NAME,
	.id = 0,
	.num_resources = ARRAY_SIZE(hdpu_cpustate_resources),
	.resource = hdpu_cpustate_resources,
};

static struct platform_device hdpu_nexus_device = {
	.name = HDPU_NEXUS_NAME,
	.id = 0,
	.num_resources = ARRAY_SIZE(hdpu_nexus_resources),
	.resource = hdpu_nexus_resources,
};

static int __init hdpu_add_pds(void)
{
	platform_device_register(&hdpu_cpustate_device);
	platform_device_register(&hdpu_nexus_device);
	return 0;
}

arch_initcall(hdpu_add_pds);
#endif
