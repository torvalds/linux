/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2007 Cavium Networks
 * Copyright (C) 2008 Wind River Systems
 */
#include <linux/init.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/serial.h>
#include <linux/types.h>
#include <linux/string.h>	/* for memset */
#include <linux/tty.h>
#include <linux/time.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>

#include <asm/processor.h>
#include <asm/reboot.h>
#include <asm/smp-ops.h>
#include <asm/system.h>
#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/bootinfo.h>
#include <asm/sections.h>
#include <asm/time.h>

#include <asm/octeon/octeon.h>

#ifdef CONFIG_CAVIUM_DECODE_RSL
extern void cvmx_interrupt_rsl_decode(void);
extern int __cvmx_interrupt_ecc_report_single_bit_errors;
extern void cvmx_interrupt_rsl_enable(void);
#endif

extern struct plat_smp_ops octeon_smp_ops;

#ifdef CONFIG_PCI
extern void pci_console_init(const char *arg);
#endif

#ifdef CONFIG_CAVIUM_RESERVE32
extern uint64_t octeon_reserve32_memory;
#endif
static unsigned long long MAX_MEMORY = 512ull << 20;

struct octeon_boot_descriptor *octeon_boot_desc_ptr;

struct cvmx_bootinfo *octeon_bootinfo;
EXPORT_SYMBOL(octeon_bootinfo);

#ifdef CONFIG_CAVIUM_RESERVE32
uint64_t octeon_reserve32_memory;
EXPORT_SYMBOL(octeon_reserve32_memory);
#endif

static int octeon_uart;

extern asmlinkage void handle_int(void);
extern asmlinkage void plat_irq_dispatch(void);

/**
 * Return non zero if we are currently running in the Octeon simulator
 *
 * Returns
 */
int octeon_is_simulation(void)
{
	return octeon_bootinfo->board_type == CVMX_BOARD_TYPE_SIM;
}
EXPORT_SYMBOL(octeon_is_simulation);

/**
 * Return true if Octeon is in PCI Host mode. This means
 * Linux can control the PCI bus.
 *
 * Returns Non zero if Octeon in host mode.
 */
int octeon_is_pci_host(void)
{
#ifdef CONFIG_PCI
	return octeon_bootinfo->config_flags & CVMX_BOOTINFO_CFG_FLAG_PCI_HOST;
#else
	return 0;
#endif
}

/**
 * Get the clock rate of Octeon
 *
 * Returns Clock rate in HZ
 */
uint64_t octeon_get_clock_rate(void)
{
	if (octeon_is_simulation())
		octeon_bootinfo->eclock_hz = 6000000;
	return octeon_bootinfo->eclock_hz;
}
EXPORT_SYMBOL(octeon_get_clock_rate);

/**
 * Write to the LCD display connected to the bootbus. This display
 * exists on most Cavium evaluation boards. If it doesn't exist, then
 * this function doesn't do anything.
 *
 * @s:      String to write
 */
void octeon_write_lcd(const char *s)
{
	if (octeon_bootinfo->led_display_base_addr) {
		void __iomem *lcd_address =
			ioremap_nocache(octeon_bootinfo->led_display_base_addr,
					8);
		int i;
		for (i = 0; i < 8; i++, s++) {
			if (*s)
				iowrite8(*s, lcd_address + i);
			else
				iowrite8(' ', lcd_address + i);
		}
		iounmap(lcd_address);
	}
}

/**
 * Return the console uart passed by the bootloader
 *
 * Returns uart   (0 or 1)
 */
int octeon_get_boot_uart(void)
{
	int uart;
#ifdef CONFIG_CAVIUM_OCTEON_2ND_KERNEL
	uart = 1;
#else
	uart = (octeon_boot_desc_ptr->flags & OCTEON_BL_FLAG_CONSOLE_UART1) ?
		1 : 0;
#endif
	return uart;
}

/**
 * Get the coremask Linux was booted on.
 *
 * Returns Core mask
 */
int octeon_get_boot_coremask(void)
{
	return octeon_boot_desc_ptr->core_mask;
}

/**
 * Check the hardware BIST results for a CPU
 */
void octeon_check_cpu_bist(void)
{
	const int coreid = cvmx_get_core_num();
	unsigned long long mask;
	unsigned long long bist_val;

	/* Check BIST results for COP0 registers */
	mask = 0x1f00000000ull;
	bist_val = read_octeon_c0_icacheerr();
	if (bist_val & mask)
		pr_err("Core%d BIST Failure: CacheErr(icache) = 0x%llx\n",
		       coreid, bist_val);

	bist_val = read_octeon_c0_dcacheerr();
	if (bist_val & 1)
		pr_err("Core%d L1 Dcache parity error: "
		       "CacheErr(dcache) = 0x%llx\n",
		       coreid, bist_val);

	mask = 0xfc00000000000000ull;
	bist_val = read_c0_cvmmemctl();
	if (bist_val & mask)
		pr_err("Core%d BIST Failure: COP0_CVM_MEM_CTL = 0x%llx\n",
		       coreid, bist_val);

	write_octeon_c0_dcacheerr(0);
}

#ifdef CONFIG_CAVIUM_RESERVE32_USE_WIRED_TLB
/**
 * Called on every core to setup the wired tlb entry needed
 * if CONFIG_CAVIUM_RESERVE32_USE_WIRED_TLB is set.
 *
 */
static void octeon_hal_setup_per_cpu_reserved32(void *unused)
{
	/*
	 * The config has selected to wire the reserve32 memory for all
	 * userspace applications. We need to put a wired TLB entry in for each
	 * 512MB of reserve32 memory. We only handle double 256MB pages here,
	 * so reserve32 must be multiple of 512MB.
	 */
	uint32_t size = CONFIG_CAVIUM_RESERVE32;
	uint32_t entrylo0 =
		0x7 | ((octeon_reserve32_memory & ((1ul << 40) - 1)) >> 6);
	uint32_t entrylo1 = entrylo0 + (256 << 14);
	uint32_t entryhi = (0x80000000UL - (CONFIG_CAVIUM_RESERVE32 << 20));
	while (size >= 512) {
#if 0
		pr_info("CPU%d: Adding double wired TLB entry for 0x%lx\n",
			smp_processor_id(), entryhi);
#endif
		add_wired_entry(entrylo0, entrylo1, entryhi, PM_256M);
		entrylo0 += 512 << 14;
		entrylo1 += 512 << 14;
		entryhi += 512 << 20;
		size -= 512;
	}
}
#endif /* CONFIG_CAVIUM_RESERVE32_USE_WIRED_TLB */

/**
 * Called to release the named block which was used to made sure
 * that nobody used the memory for something else during
 * init. Now we'll free it so userspace apps can use this
 * memory region with bootmem_alloc.
 *
 * This function is called only once from prom_free_prom_memory().
 */
void octeon_hal_setup_reserved32(void)
{
#ifdef CONFIG_CAVIUM_RESERVE32_USE_WIRED_TLB
	on_each_cpu(octeon_hal_setup_per_cpu_reserved32, NULL, 0, 1);
#endif
}

/**
 * Reboot Octeon
 *
 * @command: Command to pass to the bootloader. Currently ignored.
 */
static void octeon_restart(char *command)
{
	/* Disable all watchdogs before soft reset. They don't get cleared */
#ifdef CONFIG_SMP
	int cpu;
	for_each_online_cpu(cpu)
		cvmx_write_csr(CVMX_CIU_WDOGX(cpu_logical_map(cpu)), 0);
#else
	cvmx_write_csr(CVMX_CIU_WDOGX(cvmx_get_core_num()), 0);
#endif

	mb();
	while (1)
		cvmx_write_csr(CVMX_CIU_SOFT_RST, 1);
}


/**
 * Permanently stop a core.
 *
 * @arg: Ignored.
 */
static void octeon_kill_core(void *arg)
{
	mb();
	if (octeon_is_simulation()) {
		/* The simulator needs the watchdog to stop for dead cores */
		cvmx_write_csr(CVMX_CIU_WDOGX(cvmx_get_core_num()), 0);
		/* A break instruction causes the simulator stop a core */
		asm volatile ("sync\nbreak");
	}
}


/**
 * Halt the system
 */
static void octeon_halt(void)
{
	smp_call_function(octeon_kill_core, NULL, 0);

	switch (octeon_bootinfo->board_type) {
	case CVMX_BOARD_TYPE_NAO38:
		/* Driving a 1 to GPIO 12 shuts off this board */
		cvmx_write_csr(CVMX_GPIO_BIT_CFGX(12), 1);
		cvmx_write_csr(CVMX_GPIO_TX_SET, 0x1000);
		break;
	default:
		octeon_write_lcd("PowerOff");
		break;
	}

	octeon_kill_core(NULL);
}

#if 0
/**
 * Platform time init specifics.
 * Returns
 */
void __init plat_time_init(void)
{
	/* Nothing special here, but we are required to have one */
}

#endif

/**
 * Handle all the error condition interrupts that might occur.
 *
 */
#ifdef CONFIG_CAVIUM_DECODE_RSL
static irqreturn_t octeon_rlm_interrupt(int cpl, void *dev_id)
{
	cvmx_interrupt_rsl_decode();
	return IRQ_HANDLED;
}
#endif

/**
 * Return a string representing the system type
 *
 * Returns
 */
const char *octeon_board_type_string(void)
{
	static char name[80];
	sprintf(name, "%s (%s)",
		cvmx_board_type_to_string(octeon_bootinfo->board_type),
		octeon_model_get_string(read_c0_prid()));
	return name;
}

const char *get_system_type(void)
	__attribute__ ((alias("octeon_board_type_string")));

void octeon_user_io_init(void)
{
	union octeon_cvmemctl cvmmemctl;
	union cvmx_iob_fau_timeout fau_timeout;
	union cvmx_pow_nw_tim nm_tim;
	uint64_t cvmctl;

	/* Get the current settings for CP0_CVMMEMCTL_REG */
	cvmmemctl.u64 = read_c0_cvmmemctl();
	/* R/W If set, marked write-buffer entries time out the same
	 * as as other entries; if clear, marked write-buffer entries
	 * use the maximum timeout. */
	cvmmemctl.s.dismarkwblongto = 1;
	/* R/W If set, a merged store does not clear the write-buffer
	 * entry timeout state. */
	cvmmemctl.s.dismrgclrwbto = 0;
	/* R/W Two bits that are the MSBs of the resultant CVMSEG LM
	 * word location for an IOBDMA. The other 8 bits come from the
	 * SCRADDR field of the IOBDMA. */
	cvmmemctl.s.iobdmascrmsb = 0;
	/* R/W If set, SYNCWS and SYNCS only order marked stores; if
	 * clear, SYNCWS and SYNCS only order unmarked
	 * stores. SYNCWSMARKED has no effect when DISSYNCWS is
	 * set. */
	cvmmemctl.s.syncwsmarked = 0;
	/* R/W If set, SYNCWS acts as SYNCW and SYNCS acts as SYNC. */
	cvmmemctl.s.dissyncws = 0;
	/* R/W If set, no stall happens on write buffer full. */
	if (OCTEON_IS_MODEL(OCTEON_CN38XX_PASS2))
		cvmmemctl.s.diswbfst = 1;
	else
		cvmmemctl.s.diswbfst = 0;
	/* R/W If set (and SX set), supervisor-level loads/stores can
	 * use XKPHYS addresses with <48>==0 */
	cvmmemctl.s.xkmemenas = 0;

	/* R/W If set (and UX set), user-level loads/stores can use
	 * XKPHYS addresses with VA<48>==0 */
	cvmmemctl.s.xkmemenau = 0;

	/* R/W If set (and SX set), supervisor-level loads/stores can
	 * use XKPHYS addresses with VA<48>==1 */
	cvmmemctl.s.xkioenas = 0;

	/* R/W If set (and UX set), user-level loads/stores can use
	 * XKPHYS addresses with VA<48>==1 */
	cvmmemctl.s.xkioenau = 0;

	/* R/W If set, all stores act as SYNCW (NOMERGE must be set
	 * when this is set) RW, reset to 0. */
	cvmmemctl.s.allsyncw = 0;

	/* R/W If set, no stores merge, and all stores reach the
	 * coherent bus in order. */
	cvmmemctl.s.nomerge = 0;
	/* R/W Selects the bit in the counter used for DID time-outs 0
	 * = 231, 1 = 230, 2 = 229, 3 = 214. Actual time-out is
	 * between 1x and 2x this interval. For example, with
	 * DIDTTO=3, expiration interval is between 16K and 32K. */
	cvmmemctl.s.didtto = 0;
	/* R/W If set, the (mem) CSR clock never turns off. */
	cvmmemctl.s.csrckalwys = 0;
	/* R/W If set, mclk never turns off. */
	cvmmemctl.s.mclkalwys = 0;
	/* R/W Selects the bit in the counter used for write buffer
	 * flush time-outs (WBFLT+11) is the bit position in an
	 * internal counter used to determine expiration. The write
	 * buffer expires between 1x and 2x this interval. For
	 * example, with WBFLT = 0, a write buffer expires between 2K
	 * and 4K cycles after the write buffer entry is allocated. */
	cvmmemctl.s.wbfltime = 0;
	/* R/W If set, do not put Istream in the L2 cache. */
	cvmmemctl.s.istrnol2 = 0;
	/* R/W The write buffer threshold. */
	cvmmemctl.s.wbthresh = 10;
	/* R/W If set, CVMSEG is available for loads/stores in
	 * kernel/debug mode. */
#if CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE > 0
	cvmmemctl.s.cvmsegenak = 1;
#else
	cvmmemctl.s.cvmsegenak = 0;
#endif
	/* R/W If set, CVMSEG is available for loads/stores in
	 * supervisor mode. */
	cvmmemctl.s.cvmsegenas = 0;
	/* R/W If set, CVMSEG is available for loads/stores in user
	 * mode. */
	cvmmemctl.s.cvmsegenau = 0;
	/* R/W Size of local memory in cache blocks, 54 (6912 bytes)
	 * is max legal value. */
	cvmmemctl.s.lmemsz = CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE;


	if (smp_processor_id() == 0)
		pr_notice("CVMSEG size: %d cache lines (%d bytes)\n",
			  CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE,
			  CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE * 128);

	write_c0_cvmmemctl(cvmmemctl.u64);

	/* Move the performance counter interrupts to IRQ 6 */
	cvmctl = read_c0_cvmctl();
	cvmctl &= ~(7 << 7);
	cvmctl |= 6 << 7;
	write_c0_cvmctl(cvmctl);

	/* Set a default for the hardware timeouts */
	fau_timeout.u64 = 0;
	fau_timeout.s.tout_val = 0xfff;
	/* Disable tagwait FAU timeout */
	fau_timeout.s.tout_enb = 0;
	cvmx_write_csr(CVMX_IOB_FAU_TIMEOUT, fau_timeout.u64);

	nm_tim.u64 = 0;
	/* 4096 cycles */
	nm_tim.s.nw_tim = 3;
	cvmx_write_csr(CVMX_POW_NW_TIM, nm_tim.u64);

	write_octeon_c0_icacheerr(0);
	write_c0_derraddr1(0);
}

/**
 * Early entry point for arch setup
 */
void __init prom_init(void)
{
	struct cvmx_sysinfo *sysinfo;
	const int coreid = cvmx_get_core_num();
	int i;
	int argc;
	struct uart_port octeon_port;
#ifdef CONFIG_CAVIUM_RESERVE32
	int64_t addr = -1;
#endif
	/*
	 * The bootloader passes a pointer to the boot descriptor in
	 * $a3, this is available as fw_arg3.
	 */
	octeon_boot_desc_ptr = (struct octeon_boot_descriptor *)fw_arg3;
	octeon_bootinfo =
		cvmx_phys_to_ptr(octeon_boot_desc_ptr->cvmx_desc_vaddr);
	cvmx_bootmem_init(cvmx_phys_to_ptr(octeon_bootinfo->phy_mem_desc_addr));

	/*
	 * Only enable the LED controller if we're running on a CN38XX, CN58XX,
	 * or CN56XX. The CN30XX and CN31XX don't have an LED controller.
	 */
	if (!octeon_is_simulation() &&
	    octeon_has_feature(OCTEON_FEATURE_LED_CONTROLLER)) {
		cvmx_write_csr(CVMX_LED_EN, 0);
		cvmx_write_csr(CVMX_LED_PRT, 0);
		cvmx_write_csr(CVMX_LED_DBG, 0);
		cvmx_write_csr(CVMX_LED_PRT_FMT, 0);
		cvmx_write_csr(CVMX_LED_UDD_CNTX(0), 32);
		cvmx_write_csr(CVMX_LED_UDD_CNTX(1), 32);
		cvmx_write_csr(CVMX_LED_UDD_DATX(0), 0);
		cvmx_write_csr(CVMX_LED_UDD_DATX(1), 0);
		cvmx_write_csr(CVMX_LED_EN, 1);
	}
#ifdef CONFIG_CAVIUM_RESERVE32
	/*
	 * We need to temporarily allocate all memory in the reserve32
	 * region. This makes sure the kernel doesn't allocate this
	 * memory when it is getting memory from the
	 * bootloader. Later, after the memory allocations are
	 * complete, the reserve32 will be freed.
	 */
#ifdef CONFIG_CAVIUM_RESERVE32_USE_WIRED_TLB
	if (CONFIG_CAVIUM_RESERVE32 & 0x1ff)
		pr_err("CAVIUM_RESERVE32 isn't a multiple of 512MB. "
		       "This is required if CAVIUM_RESERVE32_USE_WIRED_TLB "
		       "is set\n");
	else
		addr = cvmx_bootmem_phy_named_block_alloc(CONFIG_CAVIUM_RESERVE32 << 20,
							0, 0, 512 << 20,
							"CAVIUM_RESERVE32", 0);
#else
	/*
	 * Allocate memory for RESERVED32 aligned on 2MB boundary. This
	 * is in case we later use hugetlb entries with it.
	 */
	addr = cvmx_bootmem_phy_named_block_alloc(CONFIG_CAVIUM_RESERVE32 << 20,
						0, 0, 2 << 20,
						"CAVIUM_RESERVE32", 0);
#endif
	if (addr < 0)
		pr_err("Failed to allocate CAVIUM_RESERVE32 memory area\n");
	else
		octeon_reserve32_memory = addr;
#endif

#ifdef CONFIG_CAVIUM_OCTEON_LOCK_L2
	if (cvmx_read_csr(CVMX_L2D_FUS3) & (3ull << 34)) {
		pr_info("Skipping L2 locking due to reduced L2 cache size\n");
	} else {
		uint32_t ebase = read_c0_ebase() & 0x3ffff000;
#ifdef CONFIG_CAVIUM_OCTEON_LOCK_L2_TLB
		/* TLB refill */
		cvmx_l2c_lock_mem_region(ebase, 0x100);
#endif
#ifdef CONFIG_CAVIUM_OCTEON_LOCK_L2_EXCEPTION
		/* General exception */
		cvmx_l2c_lock_mem_region(ebase + 0x180, 0x80);
#endif
#ifdef CONFIG_CAVIUM_OCTEON_LOCK_L2_LOW_LEVEL_INTERRUPT
		/* Interrupt handler */
		cvmx_l2c_lock_mem_region(ebase + 0x200, 0x80);
#endif
#ifdef CONFIG_CAVIUM_OCTEON_LOCK_L2_INTERRUPT
		cvmx_l2c_lock_mem_region(__pa_symbol(handle_int), 0x100);
		cvmx_l2c_lock_mem_region(__pa_symbol(plat_irq_dispatch), 0x80);
#endif
#ifdef CONFIG_CAVIUM_OCTEON_LOCK_L2_MEMCPY
		cvmx_l2c_lock_mem_region(__pa_symbol(memcpy), 0x480);
#endif
	}
#endif

	sysinfo = cvmx_sysinfo_get();
	memset(sysinfo, 0, sizeof(*sysinfo));
	sysinfo->system_dram_size = octeon_bootinfo->dram_size << 20;
	sysinfo->phy_mem_desc_ptr =
		cvmx_phys_to_ptr(octeon_bootinfo->phy_mem_desc_addr);
	sysinfo->core_mask = octeon_bootinfo->core_mask;
	sysinfo->exception_base_addr = octeon_bootinfo->exception_base_addr;
	sysinfo->cpu_clock_hz = octeon_bootinfo->eclock_hz;
	sysinfo->dram_data_rate_hz = octeon_bootinfo->dclock_hz * 2;
	sysinfo->board_type = octeon_bootinfo->board_type;
	sysinfo->board_rev_major = octeon_bootinfo->board_rev_major;
	sysinfo->board_rev_minor = octeon_bootinfo->board_rev_minor;
	memcpy(sysinfo->mac_addr_base, octeon_bootinfo->mac_addr_base,
	       sizeof(sysinfo->mac_addr_base));
	sysinfo->mac_addr_count = octeon_bootinfo->mac_addr_count;
	memcpy(sysinfo->board_serial_number,
	       octeon_bootinfo->board_serial_number,
	       sizeof(sysinfo->board_serial_number));
	sysinfo->compact_flash_common_base_addr =
		octeon_bootinfo->compact_flash_common_base_addr;
	sysinfo->compact_flash_attribute_base_addr =
		octeon_bootinfo->compact_flash_attribute_base_addr;
	sysinfo->led_display_base_addr = octeon_bootinfo->led_display_base_addr;
	sysinfo->dfa_ref_clock_hz = octeon_bootinfo->dfa_ref_clock_hz;
	sysinfo->bootloader_config_flags = octeon_bootinfo->config_flags;


	octeon_check_cpu_bist();

	octeon_uart = octeon_get_boot_uart();

	/*
	 * Disable All CIU Interrupts. The ones we need will be
	 * enabled later.  Read the SUM register so we know the write
	 * completed.
	 */
	cvmx_write_csr(CVMX_CIU_INTX_EN0((coreid * 2)), 0);
	cvmx_write_csr(CVMX_CIU_INTX_EN0((coreid * 2 + 1)), 0);
	cvmx_write_csr(CVMX_CIU_INTX_EN1((coreid * 2)), 0);
	cvmx_write_csr(CVMX_CIU_INTX_EN1((coreid * 2 + 1)), 0);
	cvmx_read_csr(CVMX_CIU_INTX_SUM0((coreid * 2)));

#ifdef CONFIG_SMP
	octeon_write_lcd("LinuxSMP");
#else
	octeon_write_lcd("Linux");
#endif

#ifdef CONFIG_CAVIUM_GDB
	/*
	 * When debugging the linux kernel, force the cores to enter
	 * the debug exception handler to break in.
	 */
	if (octeon_get_boot_debug_flag()) {
		cvmx_write_csr(CVMX_CIU_DINT, 1 << cvmx_get_core_num());
		cvmx_read_csr(CVMX_CIU_DINT);
	}
#endif

	/*
	 * BIST should always be enabled when doing a soft reset. L2
	 * Cache locking for instance is not cleared unless BIST is
	 * enabled.  Unfortunately due to a chip errata G-200 for
	 * Cn38XX and CN31XX, BIST msut be disabled on these parts.
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN38XX_PASS2) ||
	    OCTEON_IS_MODEL(OCTEON_CN31XX))
		cvmx_write_csr(CVMX_CIU_SOFT_BIST, 0);
	else
		cvmx_write_csr(CVMX_CIU_SOFT_BIST, 1);

	/* Default to 64MB in the simulator to speed things up */
	if (octeon_is_simulation())
		MAX_MEMORY = 64ull << 20;

	arcs_cmdline[0] = 0;
	argc = octeon_boot_desc_ptr->argc;
	for (i = 0; i < argc; i++) {
		const char *arg =
			cvmx_phys_to_ptr(octeon_boot_desc_ptr->argv[i]);
		if ((strncmp(arg, "MEM=", 4) == 0) ||
		    (strncmp(arg, "mem=", 4) == 0)) {
			sscanf(arg + 4, "%llu", &MAX_MEMORY);
			MAX_MEMORY <<= 20;
			if (MAX_MEMORY == 0)
				MAX_MEMORY = 32ull << 30;
		} else if (strcmp(arg, "ecc_verbose") == 0) {
#ifdef CONFIG_CAVIUM_REPORT_SINGLE_BIT_ECC
			__cvmx_interrupt_ecc_report_single_bit_errors = 1;
			pr_notice("Reporting of single bit ECC errors is "
				  "turned on\n");
#endif
		} else if (strlen(arcs_cmdline) + strlen(arg) + 1 <
			   sizeof(arcs_cmdline) - 1) {
			strcat(arcs_cmdline, " ");
			strcat(arcs_cmdline, arg);
		}
	}

	if (strstr(arcs_cmdline, "console=") == NULL) {
#ifdef CONFIG_GDB_CONSOLE
		strcat(arcs_cmdline, " console=gdb");
#else
#ifdef CONFIG_CAVIUM_OCTEON_2ND_KERNEL
		strcat(arcs_cmdline, " console=ttyS0,115200");
#else
		if (octeon_uart == 1)
			strcat(arcs_cmdline, " console=ttyS1,115200");
		else
			strcat(arcs_cmdline, " console=ttyS0,115200");
#endif
#endif
	}

	if (octeon_is_simulation()) {
		/*
		 * The simulator uses a mtdram device pre filled with
		 * the filesystem. Also specify the calibration delay
		 * to avoid calculating it every time.
		 */
		strcat(arcs_cmdline, " rw root=1f00"
		       " lpj=60176 slram=root,0x40000000,+1073741824");
	}

	mips_hpt_frequency = octeon_get_clock_rate();

	octeon_init_cvmcount();

	_machine_restart = octeon_restart;
	_machine_halt = octeon_halt;

	memset(&octeon_port, 0, sizeof(octeon_port));
	/*
	 * For early_serial_setup we don't set the port type or
	 * UPF_FIXED_TYPE.
	 */
	octeon_port.flags = ASYNC_SKIP_TEST | UPF_SHARE_IRQ;
	octeon_port.iotype = UPIO_MEM;
	/* I/O addresses are every 8 bytes */
	octeon_port.regshift = 3;
	/* Clock rate of the chip */
	octeon_port.uartclk = mips_hpt_frequency;
	octeon_port.fifosize = 64;
	octeon_port.mapbase = 0x0001180000000800ull + (1024 * octeon_uart);
	octeon_port.membase = cvmx_phys_to_ptr(octeon_port.mapbase);
	octeon_port.serial_in = octeon_serial_in;
	octeon_port.serial_out = octeon_serial_out;
#ifdef CONFIG_CAVIUM_OCTEON_2ND_KERNEL
	octeon_port.line = 0;
#else
	octeon_port.line = octeon_uart;
#endif
	octeon_port.irq = 42 + octeon_uart;
	early_serial_setup(&octeon_port);

	octeon_user_io_init();
	register_smp_ops(&octeon_smp_ops);
}

void __init plat_mem_setup(void)
{
	uint64_t mem_alloc_size;
	uint64_t total;
	int64_t memory;

	total = 0;

	/* First add the init memory we will be returning.  */
	memory = __pa_symbol(&__init_begin) & PAGE_MASK;
	mem_alloc_size = (__pa_symbol(&__init_end) & PAGE_MASK) - memory;
	if (mem_alloc_size > 0) {
		add_memory_region(memory, mem_alloc_size, BOOT_MEM_RAM);
		total += mem_alloc_size;
	}

	/*
	 * The Mips memory init uses the first memory location for
	 * some memory vectors. When SPARSEMEM is in use, it doesn't
	 * verify that the size is big enough for the final
	 * vectors. Making the smallest chuck 4MB seems to be enough
	 * to consistantly work.
	 */
	mem_alloc_size = 4 << 20;
	if (mem_alloc_size > MAX_MEMORY)
		mem_alloc_size = MAX_MEMORY;

	/*
	 * When allocating memory, we want incrementing addresses from
	 * bootmem_alloc so the code in add_memory_region can merge
	 * regions next to each other.
	 */
	cvmx_bootmem_lock();
	while ((boot_mem_map.nr_map < BOOT_MEM_MAP_MAX)
		&& (total < MAX_MEMORY)) {
#if defined(CONFIG_64BIT) || defined(CONFIG_64BIT_PHYS_ADDR)
		memory = cvmx_bootmem_phy_alloc(mem_alloc_size,
						__pa_symbol(&__init_end), -1,
						0x100000,
						CVMX_BOOTMEM_FLAG_NO_LOCKING);
#elif defined(CONFIG_HIGHMEM)
		memory = cvmx_bootmem_phy_alloc(mem_alloc_size, 0, 1ull << 31,
						0x100000,
						CVMX_BOOTMEM_FLAG_NO_LOCKING);
#else
		memory = cvmx_bootmem_phy_alloc(mem_alloc_size, 0, 512 << 20,
						0x100000,
						CVMX_BOOTMEM_FLAG_NO_LOCKING);
#endif
		if (memory >= 0) {
			/*
			 * This function automatically merges address
			 * regions next to each other if they are
			 * received in incrementing order.
			 */
			add_memory_region(memory, mem_alloc_size, BOOT_MEM_RAM);
			total += mem_alloc_size;
		} else {
			break;
		}
	}
	cvmx_bootmem_unlock();

#ifdef CONFIG_CAVIUM_RESERVE32
	/*
	 * Now that we've allocated the kernel memory it is safe to
	 * free the reserved region. We free it here so that builtin
	 * drivers can use the memory.
	 */
	if (octeon_reserve32_memory)
		cvmx_bootmem_free_named("CAVIUM_RESERVE32");
#endif /* CONFIG_CAVIUM_RESERVE32 */

	if (total == 0)
		panic("Unable to allocate memory from "
		      "cvmx_bootmem_phy_alloc\n");
}


int prom_putchar(char c)
{
	uint64_t lsrval;

	/* Spin until there is room */
	do {
		lsrval = cvmx_read_csr(CVMX_MIO_UARTX_LSR(octeon_uart));
	} while ((lsrval & 0x20) == 0);

	/* Write the byte */
	cvmx_write_csr(CVMX_MIO_UARTX_THR(octeon_uart), c);
	return 1;
}

void prom_free_prom_memory(void)
{
#ifdef CONFIG_CAVIUM_DECODE_RSL
	cvmx_interrupt_rsl_enable();

	/* Add an interrupt handler for general failures. */
	if (request_irq(OCTEON_IRQ_RML, octeon_rlm_interrupt, IRQF_SHARED,
			"RML/RSL", octeon_rlm_interrupt)) {
		panic("Unable to request_irq(OCTEON_IRQ_RML)\n");
	}
#endif

	/* This call is here so that it is performed after any TLB
	   initializations. It needs to be after these in case the
	   CONFIG_CAVIUM_RESERVE32_USE_WIRED_TLB option is set */
	octeon_hal_setup_reserved32();
}

static struct octeon_cf_data octeon_cf_data;

static int __init octeon_cf_device_init(void)
{
	union cvmx_mio_boot_reg_cfgx mio_boot_reg_cfg;
	unsigned long base_ptr, region_base, region_size;
	struct platform_device *pd;
	struct resource cf_resources[3];
	unsigned int num_resources;
	int i;
	int ret = 0;

	/* Setup octeon-cf platform device if present. */
	base_ptr = 0;
	if (octeon_bootinfo->major_version == 1
		&& octeon_bootinfo->minor_version >= 1) {
		if (octeon_bootinfo->compact_flash_common_base_addr)
			base_ptr =
				octeon_bootinfo->compact_flash_common_base_addr;
	} else {
		base_ptr = 0x1d000800;
	}

	if (!base_ptr)
		return ret;

	/* Find CS0 region. */
	for (i = 0; i < 8; i++) {
		mio_boot_reg_cfg.u64 = cvmx_read_csr(CVMX_MIO_BOOT_REG_CFGX(i));
		region_base = mio_boot_reg_cfg.s.base << 16;
		region_size = (mio_boot_reg_cfg.s.size + 1) << 16;
		if (mio_boot_reg_cfg.s.en && base_ptr >= region_base
		    && base_ptr < region_base + region_size)
			break;
	}
	if (i >= 7) {
		/* i and i + 1 are CS0 and CS1, both must be less than 8. */
		goto out;
	}
	octeon_cf_data.base_region = i;
	octeon_cf_data.is16bit = mio_boot_reg_cfg.s.width;
	octeon_cf_data.base_region_bias = base_ptr - region_base;
	memset(cf_resources, 0, sizeof(cf_resources));
	num_resources = 0;
	cf_resources[num_resources].flags	= IORESOURCE_MEM;
	cf_resources[num_resources].start	= region_base;
	cf_resources[num_resources].end	= region_base + region_size - 1;
	num_resources++;


	if (!(base_ptr & 0xfffful)) {
		/*
		 * Boot loader signals availability of DMA (true_ide
		 * mode) by setting low order bits of base_ptr to
		 * zero.
		 */

		/* Asume that CS1 immediately follows. */
		mio_boot_reg_cfg.u64 =
			cvmx_read_csr(CVMX_MIO_BOOT_REG_CFGX(i + 1));
		region_base = mio_boot_reg_cfg.s.base << 16;
		region_size = (mio_boot_reg_cfg.s.size + 1) << 16;
		if (!mio_boot_reg_cfg.s.en)
			goto out;

		cf_resources[num_resources].flags	= IORESOURCE_MEM;
		cf_resources[num_resources].start	= region_base;
		cf_resources[num_resources].end	= region_base + region_size - 1;
		num_resources++;

		octeon_cf_data.dma_engine = 0;
		cf_resources[num_resources].flags	= IORESOURCE_IRQ;
		cf_resources[num_resources].start	= OCTEON_IRQ_BOOTDMA;
		cf_resources[num_resources].end	= OCTEON_IRQ_BOOTDMA;
		num_resources++;
	} else {
		octeon_cf_data.dma_engine = -1;
	}

	pd = platform_device_alloc("pata_octeon_cf", -1);
	if (!pd) {
		ret = -ENOMEM;
		goto out;
	}
	pd->dev.platform_data = &octeon_cf_data;

	ret = platform_device_add_resources(pd, cf_resources, num_resources);
	if (ret)
		goto fail;

	ret = platform_device_add(pd);
	if (ret)
		goto fail;

	return ret;
fail:
	platform_device_put(pd);
out:
	return ret;
}
device_initcall(octeon_cf_device_init);
