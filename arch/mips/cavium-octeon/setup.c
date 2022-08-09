/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2007 Cavium Networks
 * Copyright (C) 2008, 2009 Wind River Systems
 *   written by Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/compiler.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/serial.h>
#include <linux/smp.h>
#include <linux/types.h>
#include <linux/string.h>	/* for memset */
#include <linux/tty.h>
#include <linux/time.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial_8250.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <linux/kexec.h>

#include <asm/processor.h>
#include <asm/reboot.h>
#include <asm/smp-ops.h>
#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/bootinfo.h>
#include <asm/sections.h>
#include <asm/fw/fw.h>
#include <asm/setup.h>
#include <asm/prom.h>
#include <asm/time.h>

#include <asm/octeon/octeon.h>
#include <asm/octeon/pci-octeon.h>
#include <asm/octeon/cvmx-rst-defs.h>

/*
 * TRUE for devices having registers with little-endian byte
 * order, FALSE for registers with native-endian byte order.
 * PCI mandates little-endian, USB and SATA are configuraable,
 * but we chose little-endian for these.
 */
const bool octeon_should_swizzle_table[256] = {
	[0x00] = true,	/* bootbus/CF */
	[0x1b] = true,	/* PCI mmio window */
	[0x1c] = true,	/* PCI mmio window */
	[0x1d] = true,	/* PCI mmio window */
	[0x1e] = true,	/* PCI mmio window */
	[0x68] = true,	/* OCTEON III USB */
	[0x69] = true,	/* OCTEON III USB */
	[0x6c] = true,	/* OCTEON III SATA */
	[0x6f] = true,	/* OCTEON II USB */
};
EXPORT_SYMBOL(octeon_should_swizzle_table);

#ifdef CONFIG_PCI
extern void pci_console_init(const char *arg);
#endif

static unsigned long long max_memory = ULLONG_MAX;
static unsigned long long reserve_low_mem;

DEFINE_SEMAPHORE(octeon_bootbus_sem);
EXPORT_SYMBOL(octeon_bootbus_sem);

static struct octeon_boot_descriptor *octeon_boot_desc_ptr;

struct cvmx_bootinfo *octeon_bootinfo;
EXPORT_SYMBOL(octeon_bootinfo);

#ifdef CONFIG_KEXEC
#ifdef CONFIG_SMP
/*
 * Wait for relocation code is prepared and send
 * secondary CPUs to spin until kernel is relocated.
 */
static void octeon_kexec_smp_down(void *ignored)
{
	int cpu = smp_processor_id();

	local_irq_disable();
	set_cpu_online(cpu, false);
	while (!atomic_read(&kexec_ready_to_reboot))
		cpu_relax();

	asm volatile (
	"	sync						\n"
	"	synci	($0)					\n");

	kexec_reboot();
}
#endif

#define OCTEON_DDR0_BASE    (0x0ULL)
#define OCTEON_DDR0_SIZE    (0x010000000ULL)
#define OCTEON_DDR1_BASE    (0x410000000ULL)
#define OCTEON_DDR1_SIZE    (0x010000000ULL)
#define OCTEON_DDR2_BASE    (0x020000000ULL)
#define OCTEON_DDR2_SIZE    (0x3e0000000ULL)
#define OCTEON_MAX_PHY_MEM_SIZE (16*1024*1024*1024ULL)

static struct kimage *kimage_ptr;

static void kexec_bootmem_init(uint64_t mem_size, uint32_t low_reserved_bytes)
{
	int64_t addr;
	struct cvmx_bootmem_desc *bootmem_desc;

	bootmem_desc = cvmx_bootmem_get_desc();

	if (mem_size > OCTEON_MAX_PHY_MEM_SIZE) {
		mem_size = OCTEON_MAX_PHY_MEM_SIZE;
		pr_err("Error: requested memory too large,"
		       "truncating to maximum size\n");
	}

	bootmem_desc->major_version = CVMX_BOOTMEM_DESC_MAJ_VER;
	bootmem_desc->minor_version = CVMX_BOOTMEM_DESC_MIN_VER;

	addr = (OCTEON_DDR0_BASE + reserve_low_mem + low_reserved_bytes);
	bootmem_desc->head_addr = 0;

	if (mem_size <= OCTEON_DDR0_SIZE) {
		__cvmx_bootmem_phy_free(addr,
				mem_size - reserve_low_mem -
				low_reserved_bytes, 0);
		return;
	}

	__cvmx_bootmem_phy_free(addr,
			OCTEON_DDR0_SIZE - reserve_low_mem -
			low_reserved_bytes, 0);

	mem_size -= OCTEON_DDR0_SIZE;

	if (mem_size > OCTEON_DDR1_SIZE) {
		__cvmx_bootmem_phy_free(OCTEON_DDR1_BASE, OCTEON_DDR1_SIZE, 0);
		__cvmx_bootmem_phy_free(OCTEON_DDR2_BASE,
				mem_size - OCTEON_DDR1_SIZE, 0);
	} else
		__cvmx_bootmem_phy_free(OCTEON_DDR1_BASE, mem_size, 0);
}

static int octeon_kexec_prepare(struct kimage *image)
{
	int i;
	char *bootloader = "kexec";

	octeon_boot_desc_ptr->argc = 0;
	for (i = 0; i < image->nr_segments; i++) {
		if (!strncmp(bootloader, (char *)image->segment[i].buf,
				strlen(bootloader))) {
			/*
			 * convert command line string to array
			 * of parameters (as bootloader does).
			 */
			int argc = 0, offt;
			char *str = (char *)image->segment[i].buf;
			char *ptr = strchr(str, ' ');
			while (ptr && (OCTEON_ARGV_MAX_ARGS > argc)) {
				*ptr = '\0';
				if (ptr[1] != ' ') {
					offt = (int)(ptr - str + 1);
					octeon_boot_desc_ptr->argv[argc] =
						image->segment[i].mem + offt;
					argc++;
				}
				ptr = strchr(ptr + 1, ' ');
			}
			octeon_boot_desc_ptr->argc = argc;
			break;
		}
	}

	/*
	 * Information about segments will be needed during pre-boot memory
	 * initialization.
	 */
	kimage_ptr = image;
	return 0;
}

static void octeon_generic_shutdown(void)
{
	int i;
#ifdef CONFIG_SMP
	int cpu;
#endif
	struct cvmx_bootmem_desc *bootmem_desc;
	void *named_block_array_ptr;

	bootmem_desc = cvmx_bootmem_get_desc();
	named_block_array_ptr =
		cvmx_phys_to_ptr(bootmem_desc->named_block_array_addr);

#ifdef CONFIG_SMP
	/* disable watchdogs */
	for_each_online_cpu(cpu)
		cvmx_write_csr(CVMX_CIU_WDOGX(cpu_logical_map(cpu)), 0);
#else
	cvmx_write_csr(CVMX_CIU_WDOGX(cvmx_get_core_num()), 0);
#endif
	if (kimage_ptr != kexec_crash_image) {
		memset(named_block_array_ptr,
			0x0,
			CVMX_BOOTMEM_NUM_NAMED_BLOCKS *
			sizeof(struct cvmx_bootmem_named_block_desc));
		/*
		 * Mark all memory (except low 0x100000 bytes) as free.
		 * It is the same thing that bootloader does.
		 */
		kexec_bootmem_init(octeon_bootinfo->dram_size*1024ULL*1024ULL,
				0x100000);
		/*
		 * Allocate all segments to avoid their corruption during boot.
		 */
		for (i = 0; i < kimage_ptr->nr_segments; i++)
			cvmx_bootmem_alloc_address(
				kimage_ptr->segment[i].memsz + 2*PAGE_SIZE,
				kimage_ptr->segment[i].mem - PAGE_SIZE,
				PAGE_SIZE);
	} else {
		/*
		 * Do not mark all memory as free. Free only named sections
		 * leaving the rest of memory unchanged.
		 */
		struct cvmx_bootmem_named_block_desc *ptr =
			(struct cvmx_bootmem_named_block_desc *)
			named_block_array_ptr;

		for (i = 0; i < bootmem_desc->named_block_num_blocks; i++)
			if (ptr[i].size)
				cvmx_bootmem_free_named(ptr[i].name);
	}
	kexec_args[2] = 1UL; /* running on octeon_main_processor */
	kexec_args[3] = (unsigned long)octeon_boot_desc_ptr;
#ifdef CONFIG_SMP
	secondary_kexec_args[2] = 0UL; /* running on secondary cpu */
	secondary_kexec_args[3] = (unsigned long)octeon_boot_desc_ptr;
#endif
}

static void octeon_shutdown(void)
{
	octeon_generic_shutdown();
#ifdef CONFIG_SMP
	smp_call_function(octeon_kexec_smp_down, NULL, 0);
	smp_wmb();
	while (num_online_cpus() > 1) {
		cpu_relax();
		mdelay(1);
	}
#endif
}

static void octeon_crash_shutdown(struct pt_regs *regs)
{
	octeon_generic_shutdown();
	default_machine_crash_shutdown(regs);
}

#ifdef CONFIG_SMP
void octeon_crash_smp_send_stop(void)
{
	int cpu;

	/* disable watchdogs */
	for_each_online_cpu(cpu)
		cvmx_write_csr(CVMX_CIU_WDOGX(cpu_logical_map(cpu)), 0);
}
#endif

#endif /* CONFIG_KEXEC */

#ifdef CONFIG_KEXEC
/* crashkernel cmdline parameter is parsed _after_ memory setup
 * we also parse it here (workaround for EHB5200) */
static uint64_t crashk_size, crashk_base;
#endif

static int octeon_uart;

extern asmlinkage void handle_int(void);

/**
 * octeon_is_simulation - Return non-zero if we are currently running
 * in the Octeon simulator
 *
 * Return: non-0 if running in the Octeon simulator, 0 otherwise
 */
int octeon_is_simulation(void)
{
	return octeon_bootinfo->board_type == CVMX_BOARD_TYPE_SIM;
}
EXPORT_SYMBOL(octeon_is_simulation);

/**
 * octeon_is_pci_host - Return true if Octeon is in PCI Host mode. This means
 * Linux can control the PCI bus.
 *
 * Return: Non-zero if Octeon is in host mode.
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
 * octeon_get_clock_rate - Get the clock rate of Octeon
 *
 * Return: Clock rate in HZ
 */
uint64_t octeon_get_clock_rate(void)
{
	struct cvmx_sysinfo *sysinfo = cvmx_sysinfo_get();

	return sysinfo->cpu_clock_hz;
}
EXPORT_SYMBOL(octeon_get_clock_rate);

static u64 octeon_io_clock_rate;

u64 octeon_get_io_clock_rate(void)
{
	return octeon_io_clock_rate;
}
EXPORT_SYMBOL(octeon_get_io_clock_rate);


/**
 * octeon_write_lcd - Write to the LCD display connected to the bootbus.
 * @s:	    String to write
 *
 * This display exists on most Cavium evaluation boards. If it doesn't exist,
 * then this function doesn't do anything.
 */
static void octeon_write_lcd(const char *s)
{
	if (octeon_bootinfo->led_display_base_addr) {
		void __iomem *lcd_address =
			ioremap(octeon_bootinfo->led_display_base_addr,
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
 * octeon_get_boot_uart - Return the console uart passed by the bootloader
 *
 * Return: uart number (0 or 1)
 */
static int octeon_get_boot_uart(void)
{
	return (octeon_boot_desc_ptr->flags & OCTEON_BL_FLAG_CONSOLE_UART1) ?
		1 : 0;
}

/**
 * octeon_get_boot_coremask - Get the coremask Linux was booted on.
 *
 * Return: Core mask
 */
int octeon_get_boot_coremask(void)
{
	return octeon_boot_desc_ptr->core_mask;
}

/**
 * octeon_check_cpu_bist - Check the hardware BIST results for a CPU
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

/**
 * octeon_restart - Reboot Octeon
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
		if (OCTEON_IS_OCTEON3())
			cvmx_write_csr(CVMX_RST_SOFT_RST, 1);
		else
			cvmx_write_csr(CVMX_CIU_SOFT_RST, 1);
}


/**
 * octeon_kill_core - Permanently stop a core.
 *
 * @arg: Ignored.
 */
static void octeon_kill_core(void *arg)
{
	if (octeon_is_simulation())
		/* A break instruction causes the simulator stop a core */
		asm volatile ("break" ::: "memory");

	local_irq_disable();
	/* Disable watchdog on this core. */
	cvmx_write_csr(CVMX_CIU_WDOGX(cvmx_get_core_num()), 0);
	/* Spin in a low power mode. */
	while (true)
		asm volatile ("wait" ::: "memory");
}


/**
 * octeon_halt - Halt the system
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

static char __read_mostly octeon_system_type[80];

static void __init init_octeon_system_type(void)
{
	char const *board_type;

	board_type = cvmx_board_type_to_string(octeon_bootinfo->board_type);
	if (board_type == NULL) {
		struct device_node *root;
		int ret;

		root = of_find_node_by_path("/");
		ret = of_property_read_string(root, "model", &board_type);
		of_node_put(root);
		if (ret)
			board_type = "Unsupported Board";
	}

	snprintf(octeon_system_type, sizeof(octeon_system_type), "%s (%s)",
		 board_type, octeon_model_get_string(read_c0_prid()));
}

/**
 * octeon_board_type_string - Return a string representing the system type
 *
 * Return: system type string
 */
const char *octeon_board_type_string(void)
{
	return octeon_system_type;
}

const char *get_system_type(void)
	__attribute__ ((alias("octeon_board_type_string")));

void octeon_user_io_init(void)
{
	union octeon_cvmemctl cvmmemctl;

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

	/*
	 * R/W The write buffer threshold. As per erratum Core-14752
	 * for CN63XX, a sc/scd might fail if the write buffer is
	 * full.  Lowering WBTHRESH greatly lowers the chances of the
	 * write buffer ever being full and triggering the erratum.
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN63XX_PASS1_X))
		cvmmemctl.s.wbthresh = 4;
	else
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

	write_c0_cvmmemctl(cvmmemctl.u64);

	/* Setup of CVMSEG is done in kernel-entry-init.h */
	if (smp_processor_id() == 0)
		pr_notice("CVMSEG size: %d cache lines (%d bytes)\n",
			  CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE,
			  CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE * 128);

	if (octeon_has_feature(OCTEON_FEATURE_FAU)) {
		union cvmx_iob_fau_timeout fau_timeout;

		/* Set a default for the hardware timeouts */
		fau_timeout.u64 = 0;
		fau_timeout.s.tout_val = 0xfff;
		/* Disable tagwait FAU timeout */
		fau_timeout.s.tout_enb = 0;
		cvmx_write_csr(CVMX_IOB_FAU_TIMEOUT, fau_timeout.u64);
	}

	if ((!OCTEON_IS_MODEL(OCTEON_CN68XX) &&
	     !OCTEON_IS_MODEL(OCTEON_CN7XXX)) ||
	    OCTEON_IS_MODEL(OCTEON_CN70XX)) {
		union cvmx_pow_nw_tim nm_tim;

		nm_tim.u64 = 0;
		/* 4096 cycles */
		nm_tim.s.nw_tim = 3;
		cvmx_write_csr(CVMX_POW_NW_TIM, nm_tim.u64);
	}

	write_octeon_c0_icacheerr(0);
	write_c0_derraddr1(0);
}

/**
 * prom_init - Early entry point for arch setup
 */
void __init prom_init(void)
{
	struct cvmx_sysinfo *sysinfo;
	const char *arg;
	char *p;
	int i;
	u64 t;
	int argc;

	/*
	 * The bootloader passes a pointer to the boot descriptor in
	 * $a3, this is available as fw_arg3.
	 */
	octeon_boot_desc_ptr = (struct octeon_boot_descriptor *)fw_arg3;
	octeon_bootinfo =
		cvmx_phys_to_ptr(octeon_boot_desc_ptr->cvmx_desc_vaddr);
	cvmx_bootmem_init(cvmx_phys_to_ptr(octeon_bootinfo->phy_mem_desc_addr));

	sysinfo = cvmx_sysinfo_get();
	memset(sysinfo, 0, sizeof(*sysinfo));
	sysinfo->system_dram_size = octeon_bootinfo->dram_size << 20;
	sysinfo->phy_mem_desc_addr = (u64)phys_to_virt(octeon_bootinfo->phy_mem_desc_addr);

	if ((octeon_bootinfo->major_version > 1) ||
	    (octeon_bootinfo->major_version == 1 &&
	     octeon_bootinfo->minor_version >= 4))
		cvmx_coremask_copy(&sysinfo->core_mask,
				   &octeon_bootinfo->ext_core_mask);
	else
		cvmx_coremask_set64(&sysinfo->core_mask,
				    octeon_bootinfo->core_mask);

	/* Some broken u-boot pass garbage in upper bits, clear them out */
	if (!OCTEON_IS_MODEL(OCTEON_CN78XX))
		for (i = 512; i < 1024; i++)
			cvmx_coremask_clear_core(&sysinfo->core_mask, i);

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

	if (OCTEON_IS_OCTEON2()) {
		/* I/O clock runs at a different rate than the CPU. */
		union cvmx_mio_rst_boot rst_boot;
		rst_boot.u64 = cvmx_read_csr(CVMX_MIO_RST_BOOT);
		octeon_io_clock_rate = 50000000 * rst_boot.s.pnr_mul;
	} else if (OCTEON_IS_OCTEON3()) {
		/* I/O clock runs at a different rate than the CPU. */
		union cvmx_rst_boot rst_boot;
		rst_boot.u64 = cvmx_read_csr(CVMX_RST_BOOT);
		octeon_io_clock_rate = 50000000 * rst_boot.s.pnr_mul;
	} else {
		octeon_io_clock_rate = sysinfo->cpu_clock_hz;
	}

	t = read_c0_cvmctl();
	if ((t & (1ull << 27)) == 0) {
		/*
		 * Setup the multiplier save/restore code if
		 * CvmCtl[NOMUL] clear.
		 */
		void *save;
		void *save_end;
		void *restore;
		void *restore_end;
		int save_len;
		int restore_len;
		int save_max = (char *)octeon_mult_save_end -
			(char *)octeon_mult_save;
		int restore_max = (char *)octeon_mult_restore_end -
			(char *)octeon_mult_restore;
		if (current_cpu_data.cputype == CPU_CAVIUM_OCTEON3) {
			save = octeon_mult_save3;
			save_end = octeon_mult_save3_end;
			restore = octeon_mult_restore3;
			restore_end = octeon_mult_restore3_end;
		} else {
			save = octeon_mult_save2;
			save_end = octeon_mult_save2_end;
			restore = octeon_mult_restore2;
			restore_end = octeon_mult_restore2_end;
		}
		save_len = (char *)save_end - (char *)save;
		restore_len = (char *)restore_end - (char *)restore;
		if (!WARN_ON(save_len > save_max ||
				restore_len > restore_max)) {
			memcpy(octeon_mult_save, save, save_len);
			memcpy(octeon_mult_restore, restore, restore_len);
		}
	}

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

#ifdef CONFIG_CAVIUM_OCTEON_LOCK_L2
	if (cvmx_read_csr(CVMX_L2D_FUS3) & (3ull << 34)) {
		pr_info("Skipping L2 locking due to reduced L2 cache size\n");
	} else {
		uint32_t __maybe_unused ebase = read_c0_ebase() & 0x3ffff000;
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

	octeon_check_cpu_bist();

	octeon_uart = octeon_get_boot_uart();

#ifdef CONFIG_SMP
	octeon_write_lcd("LinuxSMP");
#else
	octeon_write_lcd("Linux");
#endif

	octeon_setup_delays();

	/*
	 * BIST should always be enabled when doing a soft reset. L2
	 * Cache locking for instance is not cleared unless BIST is
	 * enabled.  Unfortunately due to a chip errata G-200 for
	 * Cn38XX and CN31XX, BIST must be disabled on these parts.
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN38XX_PASS2) ||
	    OCTEON_IS_MODEL(OCTEON_CN31XX))
		cvmx_write_csr(CVMX_CIU_SOFT_BIST, 0);
	else
		cvmx_write_csr(CVMX_CIU_SOFT_BIST, 1);

	/* Default to 64MB in the simulator to speed things up */
	if (octeon_is_simulation())
		max_memory = 64ull << 20;

	arg = strstr(arcs_cmdline, "mem=");
	if (arg) {
		max_memory = memparse(arg + 4, &p);
		if (max_memory == 0)
			max_memory = 32ull << 30;
		if (*p == '@')
			reserve_low_mem = memparse(p + 1, &p);
	}

	arcs_cmdline[0] = 0;
	argc = octeon_boot_desc_ptr->argc;
	for (i = 0; i < argc; i++) {
		const char *arg =
			cvmx_phys_to_ptr(octeon_boot_desc_ptr->argv[i]);
		if ((strncmp(arg, "MEM=", 4) == 0) ||
		    (strncmp(arg, "mem=", 4) == 0)) {
			max_memory = memparse(arg + 4, &p);
			if (max_memory == 0)
				max_memory = 32ull << 30;
			if (*p == '@')
				reserve_low_mem = memparse(p + 1, &p);
#ifdef CONFIG_KEXEC
		} else if (strncmp(arg, "crashkernel=", 12) == 0) {
			crashk_size = memparse(arg+12, &p);
			if (*p == '@')
				crashk_base = memparse(p+1, &p);
			strcat(arcs_cmdline, " ");
			strcat(arcs_cmdline, arg);
			/*
			 * To do: switch parsing to new style, something like:
			 * parse_crashkernel(arg, sysinfo->system_dram_size,
			 *		  &crashk_size, &crashk_base);
			 */
#endif
		} else if (strlen(arcs_cmdline) + strlen(arg) + 1 <
			   sizeof(arcs_cmdline) - 1) {
			strcat(arcs_cmdline, " ");
			strcat(arcs_cmdline, arg);
		}
	}

	if (strstr(arcs_cmdline, "console=") == NULL) {
		if (octeon_uart == 1)
			strcat(arcs_cmdline, " console=ttyS1,115200");
		else
			strcat(arcs_cmdline, " console=ttyS0,115200");
	}

	mips_hpt_frequency = octeon_get_clock_rate();

	octeon_init_cvmcount();

	_machine_restart = octeon_restart;
	_machine_halt = octeon_halt;

#ifdef CONFIG_KEXEC
	_machine_kexec_shutdown = octeon_shutdown;
	_machine_crash_shutdown = octeon_crash_shutdown;
	_machine_kexec_prepare = octeon_kexec_prepare;
#ifdef CONFIG_SMP
	_crash_smp_send_stop = octeon_crash_smp_send_stop;
#endif
#endif

	octeon_user_io_init();
	octeon_setup_smp();
}

/* Exclude a single page from the regions obtained in plat_mem_setup. */
#ifndef CONFIG_CRASH_DUMP
static __init void memory_exclude_page(u64 addr, u64 *mem, u64 *size)
{
	if (addr > *mem && addr < *mem + *size) {
		u64 inc = addr - *mem;
		memblock_add(*mem, inc);
		*mem += inc;
		*size -= inc;
	}

	if (addr == *mem && *size > PAGE_SIZE) {
		*mem += PAGE_SIZE;
		*size -= PAGE_SIZE;
	}
}
#endif /* CONFIG_CRASH_DUMP */

void __init fw_init_cmdline(void)
{
	int i;

	octeon_boot_desc_ptr = (struct octeon_boot_descriptor *)fw_arg3;
	for (i = 0; i < octeon_boot_desc_ptr->argc; i++) {
		const char *arg =
			cvmx_phys_to_ptr(octeon_boot_desc_ptr->argv[i]);
		if (strlen(arcs_cmdline) + strlen(arg) + 1 <
			   sizeof(arcs_cmdline) - 1) {
			strcat(arcs_cmdline, " ");
			strcat(arcs_cmdline, arg);
		}
	}
}

void __init *plat_get_fdt(void)
{
	octeon_bootinfo =
		cvmx_phys_to_ptr(octeon_boot_desc_ptr->cvmx_desc_vaddr);
	return phys_to_virt(octeon_bootinfo->fdt_addr);
}

void __init plat_mem_setup(void)
{
	uint64_t mem_alloc_size;
	uint64_t total;
	uint64_t crashk_end;
#ifndef CONFIG_CRASH_DUMP
	int64_t memory;
#endif

	total = 0;
	crashk_end = 0;

	/*
	 * The Mips memory init uses the first memory location for
	 * some memory vectors. When SPARSEMEM is in use, it doesn't
	 * verify that the size is big enough for the final
	 * vectors. Making the smallest chuck 4MB seems to be enough
	 * to consistently work.
	 */
	mem_alloc_size = 4 << 20;
	if (mem_alloc_size > max_memory)
		mem_alloc_size = max_memory;

/* Crashkernel ignores bootmem list. It relies on mem=X@Y option */
#ifdef CONFIG_CRASH_DUMP
	memblock_add(reserve_low_mem, max_memory);
	total += max_memory;
#else
#ifdef CONFIG_KEXEC
	if (crashk_size > 0) {
		memblock_add(crashk_base, crashk_size);
		crashk_end = crashk_base + crashk_size;
	}
#endif
	/*
	 * When allocating memory, we want incrementing addresses,
	 * which is handled by memblock
	 */
	cvmx_bootmem_lock();
	while (total < max_memory) {
		memory = cvmx_bootmem_phy_alloc(mem_alloc_size,
						__pa_symbol(&_end), -1,
						0x100000,
						CVMX_BOOTMEM_FLAG_NO_LOCKING);
		if (memory >= 0) {
			u64 size = mem_alloc_size;
#ifdef CONFIG_KEXEC
			uint64_t end;
#endif

			/*
			 * exclude a page at the beginning and end of
			 * the 256MB PCIe 'hole' so the kernel will not
			 * try to allocate multi-page buffers that
			 * span the discontinuity.
			 */
			memory_exclude_page(CVMX_PCIE_BAR1_PHYS_BASE,
					    &memory, &size);
			memory_exclude_page(CVMX_PCIE_BAR1_PHYS_BASE +
					    CVMX_PCIE_BAR1_PHYS_SIZE,
					    &memory, &size);
#ifdef CONFIG_KEXEC
			end = memory + mem_alloc_size;

			/*
			 * This function automatically merges address regions
			 * next to each other if they are received in
			 * incrementing order
			 */
			if (memory < crashk_base && end >  crashk_end) {
				/* region is fully in */
				memblock_add(memory, crashk_base - memory);
				total += crashk_base - memory;
				memblock_add(crashk_end, end - crashk_end);
				total += end - crashk_end;
				continue;
			}

			if (memory >= crashk_base && end <= crashk_end)
				/*
				 * Entire memory region is within the new
				 *  kernel's memory, ignore it.
				 */
				continue;

			if (memory > crashk_base && memory < crashk_end &&
			    end > crashk_end) {
				/*
				 * Overlap with the beginning of the region,
				 * reserve the beginning.
				  */
				mem_alloc_size -= crashk_end - memory;
				memory = crashk_end;
			} else if (memory < crashk_base && end > crashk_base &&
				   end < crashk_end)
				/*
				 * Overlap with the beginning of the region,
				 * chop of end.
				 */
				mem_alloc_size -= end - crashk_base;
#endif
			memblock_add(memory, mem_alloc_size);
			total += mem_alloc_size;
			/* Recovering mem_alloc_size */
			mem_alloc_size = 4 << 20;
		} else {
			break;
		}
	}
	cvmx_bootmem_unlock();
#endif /* CONFIG_CRASH_DUMP */

	if (total == 0)
		panic("Unable to allocate memory from "
		      "cvmx_bootmem_phy_alloc");
}

/*
 * Emit one character to the boot UART.	 Exported for use by the
 * watchdog timer.
 */
void prom_putchar(char c)
{
	uint64_t lsrval;

	/* Spin until there is room */
	do {
		lsrval = cvmx_read_csr(CVMX_MIO_UARTX_LSR(octeon_uart));
	} while ((lsrval & 0x20) == 0);

	/* Write the byte */
	cvmx_write_csr(CVMX_MIO_UARTX_THR(octeon_uart), c & 0xffull);
}
EXPORT_SYMBOL(prom_putchar);

void __init prom_free_prom_memory(void)
{
	if (OCTEON_IS_MODEL(OCTEON_CN6XXX)) {
		/* Check for presence of Core-14449 fix.  */
		u32 insn;
		u32 *foo;

		foo = &insn;

		asm volatile("# before" : : : "memory");
		prefetch(foo);
		asm volatile(
			".set push\n\t"
			".set noreorder\n\t"
			"bal 1f\n\t"
			"nop\n"
			"1:\tlw %0,-12($31)\n\t"
			".set pop\n\t"
			: "=r" (insn) : : "$31", "memory");

		if ((insn >> 26) != 0x33)
			panic("No PREF instruction at Core-14449 probe point.");

		if (((insn >> 16) & 0x1f) != 28)
			panic("OCTEON II DCache prefetch workaround not in place (%04x).\n"
			      "Please build kernel with proper options (CONFIG_CAVIUM_CN63XXP1).",
			      insn);
	}
}

void __init octeon_fill_mac_addresses(void);

void __init device_tree_init(void)
{
	const void *fdt;
	bool do_prune;
	bool fill_mac;

#ifdef CONFIG_MIPS_ELF_APPENDED_DTB
	if (!fdt_check_header(&__appended_dtb)) {
		fdt = &__appended_dtb;
		do_prune = false;
		fill_mac = true;
		pr_info("Using appended Device Tree.\n");
	} else
#endif
	if (octeon_bootinfo->minor_version >= 3 && octeon_bootinfo->fdt_addr) {
		fdt = phys_to_virt(octeon_bootinfo->fdt_addr);
		if (fdt_check_header(fdt))
			panic("Corrupt Device Tree passed to kernel.");
		do_prune = false;
		fill_mac = false;
		pr_info("Using passed Device Tree.\n");
	} else if (OCTEON_IS_MODEL(OCTEON_CN68XX)) {
		fdt = &__dtb_octeon_68xx_begin;
		do_prune = true;
		fill_mac = true;
	} else {
		fdt = &__dtb_octeon_3xxx_begin;
		do_prune = true;
		fill_mac = true;
	}

	initial_boot_params = (void *)fdt;

	if (do_prune) {
		octeon_prune_device_tree();
		pr_info("Using internal Device Tree.\n");
	}
	if (fill_mac)
		octeon_fill_mac_addresses();
	unflatten_and_copy_device_tree();
	init_octeon_system_type();
}

static int __initdata disable_octeon_edac_p;

static int __init disable_octeon_edac(char *str)
{
	disable_octeon_edac_p = 1;
	return 0;
}
early_param("disable_octeon_edac", disable_octeon_edac);

static char *edac_device_names[] = {
	"octeon_l2c_edac",
	"octeon_pc_edac",
};

static int __init edac_devinit(void)
{
	struct platform_device *dev;
	int i, err = 0;
	int num_lmc;
	char *name;

	if (disable_octeon_edac_p)
		return 0;

	for (i = 0; i < ARRAY_SIZE(edac_device_names); i++) {
		name = edac_device_names[i];
		dev = platform_device_register_simple(name, -1, NULL, 0);
		if (IS_ERR(dev)) {
			pr_err("Registration of %s failed!\n", name);
			err = PTR_ERR(dev);
		}
	}

	num_lmc = OCTEON_IS_MODEL(OCTEON_CN68XX) ? 4 :
		(OCTEON_IS_MODEL(OCTEON_CN56XX) ? 2 : 1);
	for (i = 0; i < num_lmc; i++) {
		dev = platform_device_register_simple("octeon_lmc_edac",
						      i, NULL, 0);
		if (IS_ERR(dev)) {
			pr_err("Registration of octeon_lmc_edac %d failed!\n", i);
			err = PTR_ERR(dev);
		}
	}

	return err;
}
device_initcall(edac_devinit);

static void __initdata *octeon_dummy_iospace;

static int __init octeon_no_pci_init(void)
{
	/*
	 * Initially assume there is no PCI. The PCI/PCIe platform code will
	 * later re-initialize these to correct values if they are present.
	 */
	octeon_dummy_iospace = vzalloc(IO_SPACE_LIMIT);
	set_io_port_base((unsigned long)octeon_dummy_iospace);
	ioport_resource.start = MAX_RESOURCE;
	ioport_resource.end = 0;
	return 0;
}
core_initcall(octeon_no_pci_init);

static int __init octeon_no_pci_release(void)
{
	/*
	 * Release the allocated memory if a real IO space is there.
	 */
	if ((unsigned long)octeon_dummy_iospace != mips_io_port_base)
		vfree(octeon_dummy_iospace);
	return 0;
}
late_initcall(octeon_no_pci_release);
