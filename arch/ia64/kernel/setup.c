/*
 * Architecture-specific setup.
 *
 * Copyright (C) 1998-2001, 2003-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2000, 2004 Intel Corp
 * 	Rohit Seth <rohit.seth@intel.com>
 * 	Suresh Siddha <suresh.b.siddha@intel.com>
 * 	Gordon Jin <gordon.jin@intel.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 *
 * 12/26/04 S.Siddha, G.Jin, R.Seth
 *			Add multi-threading and multi-core detection
 * 11/12/01 D.Mosberger Convert get_cpuinfo() to seq_file based show_cpuinfo().
 * 04/04/00 D.Mosberger renamed cpu_initialized to cpu_online_map
 * 03/31/00 R.Seth	cpu_initialized and current->processor fixes
 * 02/04/00 D.Mosberger	some more get_cpuinfo fixes...
 * 02/01/00 R.Seth	fixed get_cpuinfo for SMP
 * 01/07/99 S.Eranian	added the support for command line argument
 * 06/24/99 W.Drummond	added boot_cpu_data.
 * 05/28/05 Z. Menyhart	Dynamic stride size for "flush_icache_range()"
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/acpi.h>
#include <linux/bootmem.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/threads.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <linux/efi.h>
#include <linux/initrd.h>
#include <linux/platform.h>
#include <linux/pm.h>
#include <linux/cpufreq.h>

#include <asm/ia32.h>
#include <asm/machvec.h>
#include <asm/mca.h>
#include <asm/meminit.h>
#include <asm/page.h>
#include <asm/patch.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/sal.h>
#include <asm/sections.h>
#include <asm/serial.h>
#include <asm/setup.h>
#include <asm/smp.h>
#include <asm/system.h>
#include <asm/unistd.h>
#include <asm/system.h>

#if defined(CONFIG_SMP) && (IA64_CPU_SIZE > PAGE_SIZE)
# error "struct cpuinfo_ia64 too big!"
#endif

#ifdef CONFIG_SMP
unsigned long __per_cpu_offset[NR_CPUS];
EXPORT_SYMBOL(__per_cpu_offset);
#endif

DEFINE_PER_CPU(struct cpuinfo_ia64, cpu_info);
DEFINE_PER_CPU(unsigned long, local_per_cpu_offset);
DEFINE_PER_CPU(unsigned long, ia64_phys_stacked_size_p8);
unsigned long ia64_cycles_per_usec;
struct ia64_boot_param *ia64_boot_param;
struct screen_info screen_info;
unsigned long vga_console_iobase;
unsigned long vga_console_membase;

static struct resource data_resource = {
	.name	= "Kernel data",
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource code_resource = {
	.name	= "Kernel code",
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};
extern void efi_initialize_iomem_resources(struct resource *,
		struct resource *);
extern char _text[], _end[], _etext[];

unsigned long ia64_max_cacheline_size;

int dma_get_cache_alignment(void)
{
        return ia64_max_cacheline_size;
}
EXPORT_SYMBOL(dma_get_cache_alignment);

unsigned long ia64_iobase;	/* virtual address for I/O accesses */
EXPORT_SYMBOL(ia64_iobase);
struct io_space io_space[MAX_IO_SPACES];
EXPORT_SYMBOL(io_space);
unsigned int num_io_spaces;

/*
 * "flush_icache_range()" needs to know what processor dependent stride size to use
 * when it makes i-cache(s) coherent with d-caches.
 */
#define	I_CACHE_STRIDE_SHIFT	5	/* Safest way to go: 32 bytes by 32 bytes */
unsigned long ia64_i_cache_stride_shift = ~0;

/*
 * The merge_mask variable needs to be set to (max(iommu_page_size(iommu)) - 1).  This
 * mask specifies a mask of address bits that must be 0 in order for two buffers to be
 * mergeable by the I/O MMU (i.e., the end address of the first buffer and the start
 * address of the second buffer must be aligned to (merge_mask+1) in order to be
 * mergeable).  By default, we assume there is no I/O MMU which can merge physically
 * discontiguous buffers, so we set the merge_mask to ~0UL, which corresponds to a iommu
 * page-size of 2^64.
 */
unsigned long ia64_max_iommu_merge_mask = ~0UL;
EXPORT_SYMBOL(ia64_max_iommu_merge_mask);

/*
 * We use a special marker for the end of memory and it uses the extra (+1) slot
 */
struct rsvd_region rsvd_region[IA64_MAX_RSVD_REGIONS + 1];
int num_rsvd_regions;


/*
 * Filter incoming memory segments based on the primitive map created from the boot
 * parameters. Segments contained in the map are removed from the memory ranges. A
 * caller-specified function is called with the memory ranges that remain after filtering.
 * This routine does not assume the incoming segments are sorted.
 */
int
filter_rsvd_memory (unsigned long start, unsigned long end, void *arg)
{
	unsigned long range_start, range_end, prev_start;
	void (*func)(unsigned long, unsigned long, int);
	int i;

#if IGNORE_PFN0
	if (start == PAGE_OFFSET) {
		printk(KERN_WARNING "warning: skipping physical page 0\n");
		start += PAGE_SIZE;
		if (start >= end) return 0;
	}
#endif
	/*
	 * lowest possible address(walker uses virtual)
	 */
	prev_start = PAGE_OFFSET;
	func = arg;

	for (i = 0; i < num_rsvd_regions; ++i) {
		range_start = max(start, prev_start);
		range_end   = min(end, rsvd_region[i].start);

		if (range_start < range_end)
			call_pernode_memory(__pa(range_start), range_end - range_start, func);

		/* nothing more available in this segment */
		if (range_end == end) return 0;

		prev_start = rsvd_region[i].end;
	}
	/* end of memory marker allows full processing inside loop body */
	return 0;
}

static void
sort_regions (struct rsvd_region *rsvd_region, int max)
{
	int j;

	/* simple bubble sorting */
	while (max--) {
		for (j = 0; j < max; ++j) {
			if (rsvd_region[j].start > rsvd_region[j+1].start) {
				struct rsvd_region tmp;
				tmp = rsvd_region[j];
				rsvd_region[j] = rsvd_region[j + 1];
				rsvd_region[j + 1] = tmp;
			}
		}
	}
}

/*
 * Request address space for all standard resources
 */
static int __init register_memory(void)
{
	code_resource.start = ia64_tpa(_text);
	code_resource.end   = ia64_tpa(_etext) - 1;
	data_resource.start = ia64_tpa(_etext);
	data_resource.end   = ia64_tpa(_end) - 1;
	efi_initialize_iomem_resources(&code_resource, &data_resource);

	return 0;
}

__initcall(register_memory);

/**
 * reserve_memory - setup reserved memory areas
 *
 * Setup the reserved memory areas set aside for the boot parameters,
 * initrd, etc.  There are currently %IA64_MAX_RSVD_REGIONS defined,
 * see include/asm-ia64/meminit.h if you need to define more.
 */
void
reserve_memory (void)
{
	int n = 0;

	/*
	 * none of the entries in this table overlap
	 */
	rsvd_region[n].start = (unsigned long) ia64_boot_param;
	rsvd_region[n].end   = rsvd_region[n].start + sizeof(*ia64_boot_param);
	n++;

	rsvd_region[n].start = (unsigned long) __va(ia64_boot_param->efi_memmap);
	rsvd_region[n].end   = rsvd_region[n].start + ia64_boot_param->efi_memmap_size;
	n++;

	rsvd_region[n].start = (unsigned long) __va(ia64_boot_param->command_line);
	rsvd_region[n].end   = (rsvd_region[n].start
				+ strlen(__va(ia64_boot_param->command_line)) + 1);
	n++;

	rsvd_region[n].start = (unsigned long) ia64_imva((void *)KERNEL_START);
	rsvd_region[n].end   = (unsigned long) ia64_imva(_end);
	n++;

#ifdef CONFIG_BLK_DEV_INITRD
	if (ia64_boot_param->initrd_start) {
		rsvd_region[n].start = (unsigned long)__va(ia64_boot_param->initrd_start);
		rsvd_region[n].end   = rsvd_region[n].start + ia64_boot_param->initrd_size;
		n++;
	}
#endif

	efi_memmap_init(&rsvd_region[n].start, &rsvd_region[n].end);
	n++;

	/* end of memory marker */
	rsvd_region[n].start = ~0UL;
	rsvd_region[n].end   = ~0UL;
	n++;

	num_rsvd_regions = n;

	sort_regions(rsvd_region, num_rsvd_regions);
}

/**
 * find_initrd - get initrd parameters from the boot parameter structure
 *
 * Grab the initrd start and end from the boot parameter struct given us by
 * the boot loader.
 */
void
find_initrd (void)
{
#ifdef CONFIG_BLK_DEV_INITRD
	if (ia64_boot_param->initrd_start) {
		initrd_start = (unsigned long)__va(ia64_boot_param->initrd_start);
		initrd_end   = initrd_start+ia64_boot_param->initrd_size;

		printk(KERN_INFO "Initial ramdisk at: 0x%lx (%lu bytes)\n",
		       initrd_start, ia64_boot_param->initrd_size);
	}
#endif
}

static void __init
io_port_init (void)
{
	unsigned long phys_iobase;

	/*
	 * Set `iobase' based on the EFI memory map or, failing that, the
	 * value firmware left in ar.k0.
	 *
	 * Note that in ia32 mode, IN/OUT instructions use ar.k0 to compute
	 * the port's virtual address, so ia32_load_state() loads it with a
	 * user virtual address.  But in ia64 mode, glibc uses the
	 * *physical* address in ar.k0 to mmap the appropriate area from
	 * /dev/mem, and the inX()/outX() interfaces use MMIO.  In both
	 * cases, user-mode can only use the legacy 0-64K I/O port space.
	 *
	 * ar.k0 is not involved in kernel I/O port accesses, which can use
	 * any of the I/O port spaces and are done via MMIO using the
	 * virtual mmio_base from the appropriate io_space[].
	 */
	phys_iobase = efi_get_iobase();
	if (!phys_iobase) {
		phys_iobase = ia64_get_kr(IA64_KR_IO_BASE);
		printk(KERN_INFO "No I/O port range found in EFI memory map, "
			"falling back to AR.KR0 (0x%lx)\n", phys_iobase);
	}
	ia64_iobase = (unsigned long) ioremap(phys_iobase, 0);
	ia64_set_kr(IA64_KR_IO_BASE, __pa(ia64_iobase));

	/* setup legacy IO port space */
	io_space[0].mmio_base = ia64_iobase;
	io_space[0].sparse = 1;
	num_io_spaces = 1;
}

/**
 * early_console_setup - setup debugging console
 *
 * Consoles started here require little enough setup that we can start using
 * them very early in the boot process, either right after the machine
 * vector initialization, or even before if the drivers can detect their hw.
 *
 * Returns non-zero if a console couldn't be setup.
 */
static inline int __init
early_console_setup (char *cmdline)
{
	int earlycons = 0;

#ifdef CONFIG_SERIAL_SGI_L1_CONSOLE
	{
		extern int sn_serial_console_early_setup(void);
		if (!sn_serial_console_early_setup())
			earlycons++;
	}
#endif
#ifdef CONFIG_EFI_PCDP
	if (!efi_setup_pcdp_console(cmdline))
		earlycons++;
#endif
#ifdef CONFIG_SERIAL_8250_CONSOLE
	if (!early_serial_console_init(cmdline))
		earlycons++;
#endif

	return (earlycons) ? 0 : -1;
}

static inline void
mark_bsp_online (void)
{
#ifdef CONFIG_SMP
	/* If we register an early console, allow CPU 0 to printk */
	cpu_set(smp_processor_id(), cpu_online_map);
#endif
}

#ifdef CONFIG_SMP
static void
check_for_logical_procs (void)
{
	pal_logical_to_physical_t info;
	s64 status;

	status = ia64_pal_logical_to_phys(0, &info);
	if (status == -1) {
		printk(KERN_INFO "No logical to physical processor mapping "
		       "available\n");
		return;
	}
	if (status) {
		printk(KERN_ERR "ia64_pal_logical_to_phys failed with %ld\n",
		       status);
		return;
	}
	/*
	 * Total number of siblings that BSP has.  Though not all of them 
	 * may have booted successfully. The correct number of siblings 
	 * booted is in info.overview_num_log.
	 */
	smp_num_siblings = info.overview_tpc;
	smp_num_cpucores = info.overview_cpp;
}
#endif

void __init
setup_arch (char **cmdline_p)
{
	unw_init();

	ia64_patch_vtop((u64) __start___vtop_patchlist, (u64) __end___vtop_patchlist);

	*cmdline_p = __va(ia64_boot_param->command_line);
	strlcpy(saved_command_line, *cmdline_p, COMMAND_LINE_SIZE);

	efi_init();
	io_port_init();

#ifdef CONFIG_IA64_GENERIC
	{
		const char *mvec_name = strstr (*cmdline_p, "machvec=");
		char str[64];

		if (mvec_name) {
			const char *end;
			size_t len;

			mvec_name += 8;
			end = strchr (mvec_name, ' ');
			if (end)
				len = end - mvec_name;
			else
				len = strlen (mvec_name);
			len = min(len, sizeof (str) - 1);
			strncpy (str, mvec_name, len);
			str[len] = '\0';
			mvec_name = str;
		} else
			mvec_name = acpi_get_sysname();
		machvec_init(mvec_name);
	}
#endif

	if (early_console_setup(*cmdline_p) == 0)
		mark_bsp_online();

#ifdef CONFIG_ACPI
	/* Initialize the ACPI boot-time table parser */
	acpi_table_init();
# ifdef CONFIG_ACPI_NUMA
	acpi_numa_init();
# endif
#else
# ifdef CONFIG_SMP
	smp_build_cpu_map();	/* happens, e.g., with the Ski simulator */
# endif
#endif /* CONFIG_APCI_BOOT */

	find_memory();

	/* process SAL system table: */
	ia64_sal_init(efi.sal_systab);

#ifdef CONFIG_SMP
	cpu_physical_id(0) = hard_smp_processor_id();

	cpu_set(0, cpu_sibling_map[0]);
	cpu_set(0, cpu_core_map[0]);

	check_for_logical_procs();
	if (smp_num_cpucores > 1)
		printk(KERN_INFO
		       "cpu package is Multi-Core capable: number of cores=%d\n",
		       smp_num_cpucores);
	if (smp_num_siblings > 1)
		printk(KERN_INFO
		       "cpu package is Multi-Threading capable: number of siblings=%d\n",
		       smp_num_siblings);
#endif

	cpu_init();	/* initialize the bootstrap CPU */
	mmu_context_init();	/* initialize context_id bitmap */

#ifdef CONFIG_ACPI
	acpi_boot_init();
#endif

#ifdef CONFIG_VT
	if (!conswitchp) {
# if defined(CONFIG_DUMMY_CONSOLE)
		conswitchp = &dummy_con;
# endif
# if defined(CONFIG_VGA_CONSOLE)
		/*
		 * Non-legacy systems may route legacy VGA MMIO range to system
		 * memory.  vga_con probes the MMIO hole, so memory looks like
		 * a VGA device to it.  The EFI memory map can tell us if it's
		 * memory so we can avoid this problem.
		 */
		if (efi_mem_type(0xA0000) != EFI_CONVENTIONAL_MEMORY)
			conswitchp = &vga_con;
# endif
	}
#endif

	/* enable IA-64 Machine Check Abort Handling unless disabled */
	if (!strstr(saved_command_line, "nomca"))
		ia64_mca_init();

	platform_setup(cmdline_p);
	paging_init();
}

/*
 * Display cpu info for all cpu's.
 */
static int
show_cpuinfo (struct seq_file *m, void *v)
{
#ifdef CONFIG_SMP
#	define lpj	c->loops_per_jiffy
#	define cpunum	c->cpu
#else
#	define lpj	loops_per_jiffy
#	define cpunum	0
#endif
	static struct {
		unsigned long mask;
		const char *feature_name;
	} feature_bits[] = {
		{ 1UL << 0, "branchlong" },
		{ 1UL << 1, "spontaneous deferral"},
		{ 1UL << 2, "16-byte atomic ops" }
	};
	char family[32], features[128], *cp, sep;
	struct cpuinfo_ia64 *c = v;
	unsigned long mask;
	unsigned long proc_freq;
	int i;

	mask = c->features;

	switch (c->family) {
	      case 0x07:	memcpy(family, "Itanium", 8); break;
	      case 0x1f:	memcpy(family, "Itanium 2", 10); break;
	      default:		sprintf(family, "%u", c->family); break;
	}

	/* build the feature string: */
	memcpy(features, " standard", 10);
	cp = features;
	sep = 0;
	for (i = 0; i < (int) ARRAY_SIZE(feature_bits); ++i) {
		if (mask & feature_bits[i].mask) {
			if (sep)
				*cp++ = sep;
			sep = ',';
			*cp++ = ' ';
			strcpy(cp, feature_bits[i].feature_name);
			cp += strlen(feature_bits[i].feature_name);
			mask &= ~feature_bits[i].mask;
		}
	}
	if (mask) {
		/* print unknown features as a hex value: */
		if (sep)
			*cp++ = sep;
		sprintf(cp, " 0x%lx", mask);
	}

	proc_freq = cpufreq_quick_get(cpunum);
	if (!proc_freq)
		proc_freq = c->proc_freq / 1000;

	seq_printf(m,
		   "processor  : %d\n"
		   "vendor     : %s\n"
		   "arch       : IA-64\n"
		   "family     : %s\n"
		   "model      : %u\n"
		   "revision   : %u\n"
		   "archrev    : %u\n"
		   "features   :%s\n"	/* don't change this---it _is_ right! */
		   "cpu number : %lu\n"
		   "cpu regs   : %u\n"
		   "cpu MHz    : %lu.%06lu\n"
		   "itc MHz    : %lu.%06lu\n"
		   "BogoMIPS   : %lu.%02lu\n",
		   cpunum, c->vendor, family, c->model, c->revision, c->archrev,
		   features, c->ppn, c->number,
		   proc_freq / 1000, proc_freq % 1000,
		   c->itc_freq / 1000000, c->itc_freq % 1000000,
		   lpj*HZ/500000, (lpj*HZ/5000) % 100);
#ifdef CONFIG_SMP
	seq_printf(m, "siblings   : %u\n", cpus_weight(cpu_core_map[cpunum]));
	if (c->threads_per_core > 1 || c->cores_per_socket > 1)
		seq_printf(m,
		   	   "physical id: %u\n"
		   	   "core id    : %u\n"
		   	   "thread id  : %u\n",
		   	   c->socket_id, c->core_id, c->thread_id);
#endif
	seq_printf(m,"\n");

	return 0;
}

static void *
c_start (struct seq_file *m, loff_t *pos)
{
#ifdef CONFIG_SMP
	while (*pos < NR_CPUS && !cpu_isset(*pos, cpu_online_map))
		++*pos;
#endif
	return *pos < NR_CPUS ? cpu_data(*pos) : NULL;
}

static void *
c_next (struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void
c_stop (struct seq_file *m, void *v)
{
}

struct seq_operations cpuinfo_op = {
	.start =	c_start,
	.next =		c_next,
	.stop =		c_stop,
	.show =		show_cpuinfo
};

void
identify_cpu (struct cpuinfo_ia64 *c)
{
	union {
		unsigned long bits[5];
		struct {
			/* id 0 & 1: */
			char vendor[16];

			/* id 2 */
			u64 ppn;		/* processor serial number */

			/* id 3: */
			unsigned number		:  8;
			unsigned revision	:  8;
			unsigned model		:  8;
			unsigned family		:  8;
			unsigned archrev	:  8;
			unsigned reserved	: 24;

			/* id 4: */
			u64 features;
		} field;
	} cpuid;
	pal_vm_info_1_u_t vm1;
	pal_vm_info_2_u_t vm2;
	pal_status_t status;
	unsigned long impl_va_msb = 50, phys_addr_size = 44;	/* Itanium defaults */
	int i;

	for (i = 0; i < 5; ++i)
		cpuid.bits[i] = ia64_get_cpuid(i);

	memcpy(c->vendor, cpuid.field.vendor, 16);
#ifdef CONFIG_SMP
	c->cpu = smp_processor_id();

	/* below default values will be overwritten  by identify_siblings() 
	 * for Multi-Threading/Multi-Core capable cpu's
	 */
	c->threads_per_core = c->cores_per_socket = c->num_log = 1;
	c->socket_id = -1;

	identify_siblings(c);
#endif
	c->ppn = cpuid.field.ppn;
	c->number = cpuid.field.number;
	c->revision = cpuid.field.revision;
	c->model = cpuid.field.model;
	c->family = cpuid.field.family;
	c->archrev = cpuid.field.archrev;
	c->features = cpuid.field.features;

	status = ia64_pal_vm_summary(&vm1, &vm2);
	if (status == PAL_STATUS_SUCCESS) {
		impl_va_msb = vm2.pal_vm_info_2_s.impl_va_msb;
		phys_addr_size = vm1.pal_vm_info_1_s.phys_add_size;
	}
	c->unimpl_va_mask = ~((7L<<61) | ((1L << (impl_va_msb + 1)) - 1));
	c->unimpl_pa_mask = ~((1L<<63) | ((1L << phys_addr_size) - 1));
}

void
setup_per_cpu_areas (void)
{
	/* start_kernel() requires this... */
}

/*
 * Calculate the max. cache line size.
 *
 * In addition, the minimum of the i-cache stride sizes is calculated for
 * "flush_icache_range()".
 */
static void
get_max_cacheline_size (void)
{
	unsigned long line_size, max = 1;
	unsigned int cache_size = 0;
	u64 l, levels, unique_caches;
        pal_cache_config_info_t cci;
        s64 status;

        status = ia64_pal_cache_summary(&levels, &unique_caches);
        if (status != 0) {
                printk(KERN_ERR "%s: ia64_pal_cache_summary() failed (status=%ld)\n",
                       __FUNCTION__, status);
                max = SMP_CACHE_BYTES;
		/* Safest setup for "flush_icache_range()" */
		ia64_i_cache_stride_shift = I_CACHE_STRIDE_SHIFT;
		goto out;
        }

	for (l = 0; l < levels; ++l) {
		status = ia64_pal_cache_config_info(l, /* cache_type (data_or_unified)= */ 2,
						    &cci);
		if (status != 0) {
			printk(KERN_ERR
			       "%s: ia64_pal_cache_config_info(l=%lu, 2) failed (status=%ld)\n",
			       __FUNCTION__, l, status);
			max = SMP_CACHE_BYTES;
			/* The safest setup for "flush_icache_range()" */
			cci.pcci_stride = I_CACHE_STRIDE_SHIFT;
			cci.pcci_unified = 1;
		}
		line_size = 1 << cci.pcci_line_size;
		if (line_size > max)
			max = line_size;
		if (cache_size < cci.pcci_cache_size)
			cache_size = cci.pcci_cache_size;
		if (!cci.pcci_unified) {
			status = ia64_pal_cache_config_info(l,
						    /* cache_type (instruction)= */ 1,
						    &cci);
			if (status != 0) {
				printk(KERN_ERR
				"%s: ia64_pal_cache_config_info(l=%lu, 1) failed (status=%ld)\n",
					__FUNCTION__, l, status);
				/* The safest setup for "flush_icache_range()" */
				cci.pcci_stride = I_CACHE_STRIDE_SHIFT;
			}
		}
		if (cci.pcci_stride < ia64_i_cache_stride_shift)
			ia64_i_cache_stride_shift = cci.pcci_stride;
	}
  out:
#ifdef CONFIG_SMP
	max_cache_size = max(max_cache_size, cache_size);
#endif
	if (max > ia64_max_cacheline_size)
		ia64_max_cacheline_size = max;
}

/*
 * cpu_init() initializes state that is per-CPU.  This function acts
 * as a 'CPU state barrier', nothing should get across.
 */
void
cpu_init (void)
{
	extern void __devinit ia64_mmu_init (void *);
	unsigned long num_phys_stacked;
	pal_vm_info_2_u_t vmi;
	unsigned int max_ctx;
	struct cpuinfo_ia64 *cpu_info;
	void *cpu_data;

	cpu_data = per_cpu_init();

	/*
	 * We set ar.k3 so that assembly code in MCA handler can compute
	 * physical addresses of per cpu variables with a simple:
	 *   phys = ar.k3 + &per_cpu_var
	 */
	ia64_set_kr(IA64_KR_PER_CPU_DATA,
		    ia64_tpa(cpu_data) - (long) __per_cpu_start);

	get_max_cacheline_size();

	/*
	 * We can't pass "local_cpu_data" to identify_cpu() because we haven't called
	 * ia64_mmu_init() yet.  And we can't call ia64_mmu_init() first because it
	 * depends on the data returned by identify_cpu().  We break the dependency by
	 * accessing cpu_data() through the canonical per-CPU address.
	 */
	cpu_info = cpu_data + ((char *) &__ia64_per_cpu_var(cpu_info) - __per_cpu_start);
	identify_cpu(cpu_info);

#ifdef CONFIG_MCKINLEY
	{
#		define FEATURE_SET 16
		struct ia64_pal_retval iprv;

		if (cpu_info->family == 0x1f) {
			PAL_CALL_PHYS(iprv, PAL_PROC_GET_FEATURES, 0, FEATURE_SET, 0);
			if ((iprv.status == 0) && (iprv.v0 & 0x80) && (iprv.v2 & 0x80))
				PAL_CALL_PHYS(iprv, PAL_PROC_SET_FEATURES,
				              (iprv.v1 | 0x80), FEATURE_SET, 0);
		}
	}
#endif

	/* Clear the stack memory reserved for pt_regs: */
	memset(task_pt_regs(current), 0, sizeof(struct pt_regs));

	ia64_set_kr(IA64_KR_FPU_OWNER, 0);

	/*
	 * Initialize the page-table base register to a global
	 * directory with all zeroes.  This ensure that we can handle
	 * TLB-misses to user address-space even before we created the
	 * first user address-space.  This may happen, e.g., due to
	 * aggressive use of lfetch.fault.
	 */
	ia64_set_kr(IA64_KR_PT_BASE, __pa(ia64_imva(empty_zero_page)));

	/*
	 * Initialize default control register to defer speculative faults except
	 * for those arising from TLB misses, which are not deferred.  The
	 * kernel MUST NOT depend on a particular setting of these bits (in other words,
	 * the kernel must have recovery code for all speculative accesses).  Turn on
	 * dcr.lc as per recommendation by the architecture team.  Most IA-32 apps
	 * shouldn't be affected by this (moral: keep your ia32 locks aligned and you'll
	 * be fine).
	 */
	ia64_setreg(_IA64_REG_CR_DCR,  (  IA64_DCR_DP | IA64_DCR_DK | IA64_DCR_DX | IA64_DCR_DR
					| IA64_DCR_DA | IA64_DCR_DD | IA64_DCR_LC));
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	if (current->mm)
		BUG();

	ia64_mmu_init(ia64_imva(cpu_data));
	ia64_mca_cpu_init(ia64_imva(cpu_data));

#ifdef CONFIG_IA32_SUPPORT
	ia32_cpu_init();
#endif

	/* Clear ITC to eliminiate sched_clock() overflows in human time.  */
	ia64_set_itc(0);

	/* disable all local interrupt sources: */
	ia64_set_itv(1 << 16);
	ia64_set_lrr0(1 << 16);
	ia64_set_lrr1(1 << 16);
	ia64_setreg(_IA64_REG_CR_PMV, 1 << 16);
	ia64_setreg(_IA64_REG_CR_CMCV, 1 << 16);

	/* clear TPR & XTP to enable all interrupt classes: */
	ia64_setreg(_IA64_REG_CR_TPR, 0);
#ifdef CONFIG_SMP
	normal_xtp();
#endif

	/* set ia64_ctx.max_rid to the maximum RID that is supported by all CPUs: */
	if (ia64_pal_vm_summary(NULL, &vmi) == 0)
		max_ctx = (1U << (vmi.pal_vm_info_2_s.rid_size - 3)) - 1;
	else {
		printk(KERN_WARNING "cpu_init: PAL VM summary failed, assuming 18 RID bits\n");
		max_ctx = (1U << 15) - 1;	/* use architected minimum */
	}
	while (max_ctx < ia64_ctx.max_ctx) {
		unsigned int old = ia64_ctx.max_ctx;
		if (cmpxchg(&ia64_ctx.max_ctx, old, max_ctx) == old)
			break;
	}

	if (ia64_pal_rse_info(&num_phys_stacked, NULL) != 0) {
		printk(KERN_WARNING "cpu_init: PAL RSE info failed; assuming 96 physical "
		       "stacked regs\n");
		num_phys_stacked = 96;
	}
	/* size of physical stacked register partition plus 8 bytes: */
	__get_cpu_var(ia64_phys_stacked_size_p8) = num_phys_stacked*8 + 8;
	platform_cpu_init();
	pm_idle = default_idle;
}

/*
 * On SMP systems, when the scheduler does migration-cost autodetection,
 * it needs a way to flush as much of the CPU's caches as possible.
 */
void sched_cacheflush(void)
{
	ia64_sal_cache_flush(3);
}

void
check_bugs (void)
{
	ia64_patch_mckinley_e9((unsigned long) __start___mckinley_e9_bundles,
			       (unsigned long) __end___mckinley_e9_bundles);
}
