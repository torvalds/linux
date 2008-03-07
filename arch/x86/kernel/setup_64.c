/*
 *  Copyright (C) 1995  Linus Torvalds
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/screen_info.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <asm/processor.h>
#include <linux/console.h>
#include <linux/seq_file.h>
#include <linux/crash_dump.h>
#include <linux/root_dev.h>
#include <linux/pci.h>
#include <linux/efi.h>
#include <linux/acpi.h>
#include <linux/kallsyms.h>
#include <linux/edd.h>
#include <linux/mmzone.h>
#include <linux/kexec.h>
#include <linux/cpufreq.h>
#include <linux/dmi.h>
#include <linux/dma-mapping.h>
#include <linux/ctype.h>
#include <linux/uaccess.h>
#include <linux/init_ohci1394_dma.h>

#include <asm/mtrr.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/vsyscall.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/msr.h>
#include <asm/desc.h>
#include <video/edid.h>
#include <asm/e820.h>
#include <asm/dma.h>
#include <asm/gart.h>
#include <asm/mpspec.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>
#include <asm/setup.h>
#include <asm/mach_apic.h>
#include <asm/numa.h>
#include <asm/sections.h>
#include <asm/dmi.h>
#include <asm/cacheflush.h>
#include <asm/mce.h>
#include <asm/ds.h>
#include <asm/topology.h>

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#define ARCH_SETUP
#endif

/*
 * Machine setup..
 */

struct cpuinfo_x86 boot_cpu_data __read_mostly;
EXPORT_SYMBOL(boot_cpu_data);

__u32 cleared_cpu_caps[NCAPINTS] __cpuinitdata;

unsigned long mmu_cr4_features;

/* Boot loader ID as an integer, for the benefit of proc_dointvec */
int bootloader_type;

unsigned long saved_video_mode;

int force_mwait __cpuinitdata;

/*
 * Early DMI memory
 */
int dmi_alloc_index;
char dmi_alloc_data[DMI_MAX_DATA];

/*
 * Setup options
 */
struct screen_info screen_info;
EXPORT_SYMBOL(screen_info);
struct sys_desc_table_struct {
	unsigned short length;
	unsigned char table[0];
};

struct edid_info edid_info;
EXPORT_SYMBOL_GPL(edid_info);

extern int root_mountflags;

char __initdata command_line[COMMAND_LINE_SIZE];

struct resource standard_io_resources[] = {
	{ .name = "dma1", .start = 0x00, .end = 0x1f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic1", .start = 0x20, .end = 0x21,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer0", .start = 0x40, .end = 0x43,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer1", .start = 0x50, .end = 0x53,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard", .start = 0x60, .end = 0x6f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma page reg", .start = 0x80, .end = 0x8f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic2", .start = 0xa0, .end = 0xa1,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "dma2", .start = 0xc0, .end = 0xdf,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "fpu", .start = 0xf0, .end = 0xff,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO }
};

#define IORESOURCE_RAM (IORESOURCE_BUSY | IORESOURCE_MEM)

static struct resource data_resource = {
	.name = "Kernel data",
	.start = 0,
	.end = 0,
	.flags = IORESOURCE_RAM,
};
static struct resource code_resource = {
	.name = "Kernel code",
	.start = 0,
	.end = 0,
	.flags = IORESOURCE_RAM,
};
static struct resource bss_resource = {
	.name = "Kernel bss",
	.start = 0,
	.end = 0,
	.flags = IORESOURCE_RAM,
};

static void __cpuinit early_identify_cpu(struct cpuinfo_x86 *c);

#ifdef CONFIG_PROC_VMCORE
/* elfcorehdr= specifies the location of elf core header
 * stored by the crashed kernel. This option will be passed
 * by kexec loader to the capture kernel.
 */
static int __init setup_elfcorehdr(char *arg)
{
	char *end;
	if (!arg)
		return -EINVAL;
	elfcorehdr_addr = memparse(arg, &end);
	return end > arg ? 0 : -EINVAL;
}
early_param("elfcorehdr", setup_elfcorehdr);
#endif

#ifndef CONFIG_NUMA
static void __init
contig_initmem_init(unsigned long start_pfn, unsigned long end_pfn)
{
	unsigned long bootmap_size, bootmap;

	bootmap_size = bootmem_bootmap_pages(end_pfn)<<PAGE_SHIFT;
	bootmap = find_e820_area(0, end_pfn<<PAGE_SHIFT, bootmap_size,
				 PAGE_SIZE);
	if (bootmap == -1L)
		panic("Cannot find bootmem map of size %ld\n", bootmap_size);
	bootmap_size = init_bootmem(bootmap >> PAGE_SHIFT, end_pfn);
	e820_register_active_regions(0, start_pfn, end_pfn);
	free_bootmem_with_active_regions(0, end_pfn);
	reserve_bootmem(bootmap, bootmap_size, BOOTMEM_DEFAULT);
}
#endif

#if defined(CONFIG_EDD) || defined(CONFIG_EDD_MODULE)
struct edd edd;
#ifdef CONFIG_EDD_MODULE
EXPORT_SYMBOL(edd);
#endif
/**
 * copy_edd() - Copy the BIOS EDD information
 *              from boot_params into a safe place.
 *
 */
static inline void copy_edd(void)
{
     memcpy(edd.mbr_signature, boot_params.edd_mbr_sig_buffer,
	    sizeof(edd.mbr_signature));
     memcpy(edd.edd_info, boot_params.eddbuf, sizeof(edd.edd_info));
     edd.mbr_signature_nr = boot_params.edd_mbr_sig_buf_entries;
     edd.edd_info_nr = boot_params.eddbuf_entries;
}
#else
static inline void copy_edd(void)
{
}
#endif

#ifdef CONFIG_KEXEC
static void __init reserve_crashkernel(void)
{
	unsigned long long total_mem;
	unsigned long long crash_size, crash_base;
	int ret;

	total_mem = ((unsigned long long)max_low_pfn - min_low_pfn) << PAGE_SHIFT;

	ret = parse_crashkernel(boot_command_line, total_mem,
			&crash_size, &crash_base);
	if (ret == 0 && crash_size) {
		if (crash_base <= 0) {
			printk(KERN_INFO "crashkernel reservation failed - "
					"you have to specify a base address\n");
			return;
		}

		if (reserve_bootmem(crash_base, crash_size,
					BOOTMEM_EXCLUSIVE) < 0) {
			printk(KERN_INFO "crashkernel reservation failed - "
					"memory is in use\n");
			return;
		}

		printk(KERN_INFO "Reserving %ldMB of memory at %ldMB "
				"for crashkernel (System RAM: %ldMB)\n",
				(unsigned long)(crash_size >> 20),
				(unsigned long)(crash_base >> 20),
				(unsigned long)(total_mem >> 20));
		crashk_res.start = crash_base;
		crashk_res.end   = crash_base + crash_size - 1;
	}
}
#else
static inline void __init reserve_crashkernel(void)
{}
#endif

/* Overridden in paravirt.c if CONFIG_PARAVIRT */
void __attribute__((weak)) __init memory_setup(void)
{
       machine_specific_memory_setup();
}

/*
 * setup_arch - architecture-specific boot-time initializations
 *
 * Note: On x86_64, fixmaps are ready for use even before this is called.
 */
void __init setup_arch(char **cmdline_p)
{
	unsigned i;

	printk(KERN_INFO "Command line: %s\n", boot_command_line);

	ROOT_DEV = old_decode_dev(boot_params.hdr.root_dev);
	screen_info = boot_params.screen_info;
	edid_info = boot_params.edid_info;
	saved_video_mode = boot_params.hdr.vid_mode;
	bootloader_type = boot_params.hdr.type_of_loader;

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = boot_params.hdr.ram_size & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((boot_params.hdr.ram_size & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((boot_params.hdr.ram_size & RAMDISK_LOAD_FLAG) != 0);
#endif
#ifdef CONFIG_EFI
	if (!strncmp((char *)&boot_params.efi_info.efi_loader_signature,
		     "EL64", 4))
		efi_enabled = 1;
#endif

	ARCH_SETUP

	memory_setup();
	copy_edd();

	if (!boot_params.hdr.root_flags)
		root_mountflags &= ~MS_RDONLY;
	init_mm.start_code = (unsigned long) &_text;
	init_mm.end_code = (unsigned long) &_etext;
	init_mm.end_data = (unsigned long) &_edata;
	init_mm.brk = (unsigned long) &_end;

	code_resource.start = virt_to_phys(&_text);
	code_resource.end = virt_to_phys(&_etext)-1;
	data_resource.start = virt_to_phys(&_etext);
	data_resource.end = virt_to_phys(&_edata)-1;
	bss_resource.start = virt_to_phys(&__bss_start);
	bss_resource.end = virt_to_phys(&__bss_stop)-1;

	early_identify_cpu(&boot_cpu_data);

	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	parse_early_param();

#ifdef CONFIG_PROVIDE_OHCI1394_DMA_INIT
	if (init_ohci1394_dma_early)
		init_ohci1394_dma_on_all_controllers();
#endif

	finish_e820_parsing();

	early_gart_iommu_check();

	e820_register_active_regions(0, 0, -1UL);
	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	end_pfn = e820_end_of_ram();
	/* update e820 for memory not covered by WB MTRRs */
	mtrr_bp_init();
	if (mtrr_trim_uncached_memory(end_pfn)) {
		e820_register_active_regions(0, 0, -1UL);
		end_pfn = e820_end_of_ram();
	}

	num_physpages = end_pfn;

	check_efer();

	init_memory_mapping(0, (end_pfn_map << PAGE_SHIFT));
	if (efi_enabled)
		efi_init();

	dmi_scan_machine();

	io_delay_init();

#ifdef CONFIG_SMP
	/* setup to use the early static init tables during kernel startup */
	x86_cpu_to_apicid_early_ptr = (void *)x86_cpu_to_apicid_init;
	x86_bios_cpu_apicid_early_ptr = (void *)x86_bios_cpu_apicid_init;
#ifdef CONFIG_NUMA
	x86_cpu_to_node_map_early_ptr = (void *)x86_cpu_to_node_map_init;
#endif
#endif

#ifdef CONFIG_ACPI
	/*
	 * Initialize the ACPI boot-time table parser (gets the RSDP and SDT).
	 * Call this early for SRAT node setup.
	 */
	acpi_boot_table_init();
#endif

	/* How many end-of-memory variables you have, grandma! */
	max_low_pfn = end_pfn;
	max_pfn = end_pfn;
	high_memory = (void *)__va(end_pfn * PAGE_SIZE - 1) + 1;

	/* Remove active ranges so rediscovery with NUMA-awareness happens */
	remove_all_active_ranges();

#ifdef CONFIG_ACPI_NUMA
	/*
	 * Parse SRAT to discover nodes.
	 */
	acpi_numa_init();
#endif

#ifdef CONFIG_NUMA
	numa_initmem_init(0, end_pfn);
#else
	contig_initmem_init(0, end_pfn);
#endif

	early_res_to_bootmem();

	dma32_reserve_bootmem();

#ifdef CONFIG_ACPI_SLEEP
	/*
	 * Reserve low memory region for sleep support.
	 */
       acpi_reserve_bootmem();
#endif

	if (efi_enabled)
		efi_reserve_bootmem();

       /*
	* Find and reserve possible boot-time SMP configuration:
	*/
	find_smp_config();
#ifdef CONFIG_BLK_DEV_INITRD
	if (boot_params.hdr.type_of_loader && boot_params.hdr.ramdisk_image) {
		unsigned long ramdisk_image = boot_params.hdr.ramdisk_image;
		unsigned long ramdisk_size  = boot_params.hdr.ramdisk_size;
		unsigned long ramdisk_end   = ramdisk_image + ramdisk_size;
		unsigned long end_of_mem    = end_pfn << PAGE_SHIFT;

		if (ramdisk_end <= end_of_mem) {
			reserve_bootmem_generic(ramdisk_image, ramdisk_size);
			initrd_start = ramdisk_image + PAGE_OFFSET;
			initrd_end = initrd_start+ramdisk_size;
		} else {
			/* Assumes everything on node 0 */
			free_bootmem(ramdisk_image, ramdisk_size);
			printk(KERN_ERR "initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       ramdisk_end, end_of_mem);
			initrd_start = 0;
		}
	}
#endif
	reserve_crashkernel();
	paging_init();
	map_vsyscall();

	early_quirks();

#ifdef CONFIG_ACPI
	/*
	 * Read APIC and some other early information from ACPI tables.
	 */
	acpi_boot_init();
#endif

	init_cpu_to_node();

	/*
	 * get boot-time SMP configuration:
	 */
	if (smp_found_config)
		get_smp_config();
	init_apic_mappings();
	ioapic_init_mappings();

	/*
	 * We trust e820 completely. No explicit ROM probing in memory.
	 */
	e820_reserve_resources(&code_resource, &data_resource, &bss_resource);
	e820_mark_nosave_regions();

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < ARRAY_SIZE(standard_io_resources); i++)
		request_resource(&ioport_resource, &standard_io_resources[i]);

	e820_setup_gap();

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	if (!efi_enabled || (efi_mem_type(0xa0000) != EFI_CONVENTIONAL_MEMORY))
		conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
}

static int __cpuinit get_model_name(struct cpuinfo_x86 *c)
{
	unsigned int *v;

	if (c->extended_cpuid_level < 0x80000004)
		return 0;

	v = (unsigned int *) c->x86_model_id;
	cpuid(0x80000002, &v[0], &v[1], &v[2], &v[3]);
	cpuid(0x80000003, &v[4], &v[5], &v[6], &v[7]);
	cpuid(0x80000004, &v[8], &v[9], &v[10], &v[11]);
	c->x86_model_id[48] = 0;
	return 1;
}


static void __cpuinit display_cacheinfo(struct cpuinfo_x86 *c)
{
	unsigned int n, dummy, eax, ebx, ecx, edx;

	n = c->extended_cpuid_level;

	if (n >= 0x80000005) {
		cpuid(0x80000005, &dummy, &ebx, &ecx, &edx);
		printk(KERN_INFO "CPU: L1 I Cache: %dK (%d bytes/line), "
		       "D cache %dK (%d bytes/line)\n",
		       edx>>24, edx&0xFF, ecx>>24, ecx&0xFF);
		c->x86_cache_size = (ecx>>24) + (edx>>24);
		/* On K8 L1 TLB is inclusive, so don't count it */
		c->x86_tlbsize = 0;
	}

	if (n >= 0x80000006) {
		cpuid(0x80000006, &dummy, &ebx, &ecx, &edx);
		ecx = cpuid_ecx(0x80000006);
		c->x86_cache_size = ecx >> 16;
		c->x86_tlbsize += ((ebx >> 16) & 0xfff) + (ebx & 0xfff);

		printk(KERN_INFO "CPU: L2 Cache: %dK (%d bytes/line)\n",
		c->x86_cache_size, ecx & 0xFF);
	}
	if (n >= 0x80000008) {
		cpuid(0x80000008, &eax, &dummy, &dummy, &dummy);
		c->x86_virt_bits = (eax >> 8) & 0xff;
		c->x86_phys_bits = eax & 0xff;
	}
}

#ifdef CONFIG_NUMA
static int __cpuinit nearby_node(int apicid)
{
	int i, node;

	for (i = apicid - 1; i >= 0; i--) {
		node = apicid_to_node[i];
		if (node != NUMA_NO_NODE && node_online(node))
			return node;
	}
	for (i = apicid + 1; i < MAX_LOCAL_APIC; i++) {
		node = apicid_to_node[i];
		if (node != NUMA_NO_NODE && node_online(node))
			return node;
	}
	return first_node(node_online_map); /* Shouldn't happen */
}
#endif

/*
 * On a AMD dual core setup the lower bits of the APIC id distingush the cores.
 * Assumes number of cores is a power of two.
 */
static void __cpuinit amd_detect_cmp(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	unsigned bits;
#ifdef CONFIG_NUMA
	int cpu = smp_processor_id();
	int node = 0;
	unsigned apicid = hard_smp_processor_id();
#endif
	bits = c->x86_coreid_bits;

	/* Low order bits define the core id (index of core in socket) */
	c->cpu_core_id = c->phys_proc_id & ((1 << bits)-1);
	/* Convert the APIC ID into the socket ID */
	c->phys_proc_id = phys_pkg_id(bits);

#ifdef CONFIG_NUMA
	node = c->phys_proc_id;
	if (apicid_to_node[apicid] != NUMA_NO_NODE)
		node = apicid_to_node[apicid];
	if (!node_online(node)) {
		/* Two possibilities here:
		   - The CPU is missing memory and no node was created.
		   In that case try picking one from a nearby CPU
		   - The APIC IDs differ from the HyperTransport node IDs
		   which the K8 northbridge parsing fills in.
		   Assume they are all increased by a constant offset,
		   but in the same order as the HT nodeids.
		   If that doesn't result in a usable node fall back to the
		   path for the previous case.  */

		int ht_nodeid = apicid - (cpu_data(0).phys_proc_id << bits);

		if (ht_nodeid >= 0 &&
		    apicid_to_node[ht_nodeid] != NUMA_NO_NODE)
			node = apicid_to_node[ht_nodeid];
		/* Pick a nearby node */
		if (!node_online(node))
			node = nearby_node(apicid);
	}
	numa_set_node(cpu, node);

	printk(KERN_INFO "CPU %d/%x -> Node %d\n", cpu, apicid, node);
#endif
#endif
}

static void __cpuinit early_init_amd_mc(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	unsigned bits, ecx;

	/* Multi core CPU? */
	if (c->extended_cpuid_level < 0x80000008)
		return;

	ecx = cpuid_ecx(0x80000008);

	c->x86_max_cores = (ecx & 0xff) + 1;

	/* CPU telling us the core id bits shift? */
	bits = (ecx >> 12) & 0xF;

	/* Otherwise recompute */
	if (bits == 0) {
		while ((1 << bits) < c->x86_max_cores)
			bits++;
	}

	c->x86_coreid_bits = bits;

#endif
}

#define ENABLE_C1E_MASK		0x18000000
#define CPUID_PROCESSOR_SIGNATURE	1
#define CPUID_XFAM		0x0ff00000
#define CPUID_XFAM_K8		0x00000000
#define CPUID_XFAM_10H		0x00100000
#define CPUID_XFAM_11H		0x00200000
#define CPUID_XMOD		0x000f0000
#define CPUID_XMOD_REV_F	0x00040000

/* AMD systems with C1E don't have a working lAPIC timer. Check for that. */
static __cpuinit int amd_apic_timer_broken(void)
{
	u32 lo, hi, eax = cpuid_eax(CPUID_PROCESSOR_SIGNATURE);

	switch (eax & CPUID_XFAM) {
	case CPUID_XFAM_K8:
		if ((eax & CPUID_XMOD) < CPUID_XMOD_REV_F)
			break;
	case CPUID_XFAM_10H:
	case CPUID_XFAM_11H:
		rdmsr(MSR_K8_ENABLE_C1E, lo, hi);
		if (lo & ENABLE_C1E_MASK)
			return 1;
		break;
	default:
		/* err on the side of caution */
		return 1;
	}
	return 0;
}

static void __cpuinit early_init_amd(struct cpuinfo_x86 *c)
{
	early_init_amd_mc(c);

 	/* c->x86_power is 8000_0007 edx. Bit 8 is constant TSC */
	if (c->x86_power & (1<<8))
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);
}

static void __cpuinit init_amd(struct cpuinfo_x86 *c)
{
	unsigned level;

#ifdef CONFIG_SMP
	unsigned long value;

	/*
	 * Disable TLB flush filter by setting HWCR.FFDIS on K8
	 * bit 6 of msr C001_0015
	 *
	 * Errata 63 for SH-B3 steppings
	 * Errata 122 for all steppings (F+ have it disabled by default)
	 */
	if (c->x86 == 15) {
		rdmsrl(MSR_K8_HWCR, value);
		value |= 1 << 6;
		wrmsrl(MSR_K8_HWCR, value);
	}
#endif

	/* Bit 31 in normal CPUID used for nonstandard 3DNow ID;
	   3DNow is IDd by bit 31 in extended CPUID (1*32+31) anyway */
	clear_bit(0*32+31, (unsigned long *)&c->x86_capability);

	/* On C+ stepping K8 rep microcode works well for copy/memset */
	level = cpuid_eax(1);
	if (c->x86 == 15 && ((level >= 0x0f48 && level < 0x0f50) ||
			     level >= 0x0f58))
		set_cpu_cap(c, X86_FEATURE_REP_GOOD);
	if (c->x86 == 0x10 || c->x86 == 0x11)
		set_cpu_cap(c, X86_FEATURE_REP_GOOD);

	/* Enable workaround for FXSAVE leak */
	if (c->x86 >= 6)
		set_cpu_cap(c, X86_FEATURE_FXSAVE_LEAK);

	level = get_model_name(c);
	if (!level) {
		switch (c->x86) {
		case 15:
			/* Should distinguish Models here, but this is only
			   a fallback anyways. */
			strcpy(c->x86_model_id, "Hammer");
			break;
		}
	}
	display_cacheinfo(c);

	/* Multi core CPU? */
	if (c->extended_cpuid_level >= 0x80000008)
		amd_detect_cmp(c);

	if (c->extended_cpuid_level >= 0x80000006 &&
		(cpuid_edx(0x80000006) & 0xf000))
		num_cache_leaves = 4;
	else
		num_cache_leaves = 3;

	if (c->x86 == 0xf || c->x86 == 0x10 || c->x86 == 0x11)
		set_cpu_cap(c, X86_FEATURE_K8);

	/* MFENCE stops RDTSC speculation */
	set_cpu_cap(c, X86_FEATURE_MFENCE_RDTSC);

	if (amd_apic_timer_broken())
		disable_apic_timer = 1;
}

void __cpuinit detect_ht(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	u32 eax, ebx, ecx, edx;
	int index_msb, core_bits;

	cpuid(1, &eax, &ebx, &ecx, &edx);


	if (!cpu_has(c, X86_FEATURE_HT))
		return;
	if (cpu_has(c, X86_FEATURE_CMP_LEGACY))
		goto out;

	smp_num_siblings = (ebx & 0xff0000) >> 16;

	if (smp_num_siblings == 1) {
		printk(KERN_INFO  "CPU: Hyper-Threading is disabled\n");
	} else if (smp_num_siblings > 1) {

		if (smp_num_siblings > NR_CPUS) {
			printk(KERN_WARNING "CPU: Unsupported number of "
			       "siblings %d", smp_num_siblings);
			smp_num_siblings = 1;
			return;
		}

		index_msb = get_count_order(smp_num_siblings);
		c->phys_proc_id = phys_pkg_id(index_msb);

		smp_num_siblings = smp_num_siblings / c->x86_max_cores;

		index_msb = get_count_order(smp_num_siblings);

		core_bits = get_count_order(c->x86_max_cores);

		c->cpu_core_id = phys_pkg_id(index_msb) &
					       ((1 << core_bits) - 1);
	}
out:
	if ((c->x86_max_cores * smp_num_siblings) > 1) {
		printk(KERN_INFO  "CPU: Physical Processor ID: %d\n",
		       c->phys_proc_id);
		printk(KERN_INFO  "CPU: Processor Core ID: %d\n",
		       c->cpu_core_id);
	}

#endif
}

/*
 * find out the number of processor cores on the die
 */
static int __cpuinit intel_num_cpu_cores(struct cpuinfo_x86 *c)
{
	unsigned int eax, t;

	if (c->cpuid_level < 4)
		return 1;

	cpuid_count(4, 0, &eax, &t, &t, &t);

	if (eax & 0x1f)
		return ((eax >> 26) + 1);
	else
		return 1;
}

static void __cpuinit srat_detect_node(void)
{
#ifdef CONFIG_NUMA
	unsigned node;
	int cpu = smp_processor_id();
	int apicid = hard_smp_processor_id();

	/* Don't do the funky fallback heuristics the AMD version employs
	   for now. */
	node = apicid_to_node[apicid];
	if (node == NUMA_NO_NODE)
		node = first_node(node_online_map);
	numa_set_node(cpu, node);

	printk(KERN_INFO "CPU %d/%x -> Node %d\n", cpu, apicid, node);
#endif
}

static void __cpuinit early_init_intel(struct cpuinfo_x86 *c)
{
	if ((c->x86 == 0xf && c->x86_model >= 0x03) ||
	    (c->x86 == 0x6 && c->x86_model >= 0x0e))
		set_bit(X86_FEATURE_CONSTANT_TSC, &c->x86_capability);
}

static void __cpuinit init_intel(struct cpuinfo_x86 *c)
{
	/* Cache sizes */
	unsigned n;

	init_intel_cacheinfo(c);
	if (c->cpuid_level > 9) {
		unsigned eax = cpuid_eax(10);
		/* Check for version and the number of counters */
		if ((eax & 0xff) && (((eax>>8) & 0xff) > 1))
			set_cpu_cap(c, X86_FEATURE_ARCH_PERFMON);
	}

	if (cpu_has_ds) {
		unsigned int l1, l2;
		rdmsr(MSR_IA32_MISC_ENABLE, l1, l2);
		if (!(l1 & (1<<11)))
			set_cpu_cap(c, X86_FEATURE_BTS);
		if (!(l1 & (1<<12)))
			set_cpu_cap(c, X86_FEATURE_PEBS);
	}


	if (cpu_has_bts)
		ds_init_intel(c);

	n = c->extended_cpuid_level;
	if (n >= 0x80000008) {
		unsigned eax = cpuid_eax(0x80000008);
		c->x86_virt_bits = (eax >> 8) & 0xff;
		c->x86_phys_bits = eax & 0xff;
		/* CPUID workaround for Intel 0F34 CPU */
		if (c->x86_vendor == X86_VENDOR_INTEL &&
		    c->x86 == 0xF && c->x86_model == 0x3 &&
		    c->x86_mask == 0x4)
			c->x86_phys_bits = 36;
	}

	if (c->x86 == 15)
		c->x86_cache_alignment = c->x86_clflush_size * 2;
	if ((c->x86 == 0xf && c->x86_model >= 0x03) ||
	    (c->x86 == 0x6 && c->x86_model >= 0x0e))
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);
	if (c->x86 == 6)
		set_cpu_cap(c, X86_FEATURE_REP_GOOD);
	set_cpu_cap(c, X86_FEATURE_LFENCE_RDTSC);
	c->x86_max_cores = intel_num_cpu_cores(c);

	srat_detect_node();
}

static void __cpuinit get_cpu_vendor(struct cpuinfo_x86 *c)
{
	char *v = c->x86_vendor_id;

	if (!strcmp(v, "AuthenticAMD"))
		c->x86_vendor = X86_VENDOR_AMD;
	else if (!strcmp(v, "GenuineIntel"))
		c->x86_vendor = X86_VENDOR_INTEL;
	else
		c->x86_vendor = X86_VENDOR_UNKNOWN;
}

/* Do some early cpuid on the boot CPU to get some parameter that are
   needed before check_bugs. Everything advanced is in identify_cpu
   below. */
static void __cpuinit early_identify_cpu(struct cpuinfo_x86 *c)
{
	u32 tfms, xlvl;

	c->loops_per_jiffy = loops_per_jiffy;
	c->x86_cache_size = -1;
	c->x86_vendor = X86_VENDOR_UNKNOWN;
	c->x86_model = c->x86_mask = 0;	/* So far unknown... */
	c->x86_vendor_id[0] = '\0'; /* Unset */
	c->x86_model_id[0] = '\0';  /* Unset */
	c->x86_clflush_size = 64;
	c->x86_cache_alignment = c->x86_clflush_size;
	c->x86_max_cores = 1;
	c->x86_coreid_bits = 0;
	c->extended_cpuid_level = 0;
	memset(&c->x86_capability, 0, sizeof c->x86_capability);

	/* Get vendor name */
	cpuid(0x00000000, (unsigned int *)&c->cpuid_level,
	      (unsigned int *)&c->x86_vendor_id[0],
	      (unsigned int *)&c->x86_vendor_id[8],
	      (unsigned int *)&c->x86_vendor_id[4]);

	get_cpu_vendor(c);

	/* Initialize the standard set of capabilities */
	/* Note that the vendor-specific code below might override */

	/* Intel-defined flags: level 0x00000001 */
	if (c->cpuid_level >= 0x00000001) {
		__u32 misc;
		cpuid(0x00000001, &tfms, &misc, &c->x86_capability[4],
		      &c->x86_capability[0]);
		c->x86 = (tfms >> 8) & 0xf;
		c->x86_model = (tfms >> 4) & 0xf;
		c->x86_mask = tfms & 0xf;
		if (c->x86 == 0xf)
			c->x86 += (tfms >> 20) & 0xff;
		if (c->x86 >= 0x6)
			c->x86_model += ((tfms >> 16) & 0xF) << 4;
		if (c->x86_capability[0] & (1<<19))
			c->x86_clflush_size = ((misc >> 8) & 0xff) * 8;
	} else {
		/* Have CPUID level 0 only - unheard of */
		c->x86 = 4;
	}

#ifdef CONFIG_SMP
	c->phys_proc_id = (cpuid_ebx(1) >> 24) & 0xff;
#endif
	/* AMD-defined flags: level 0x80000001 */
	xlvl = cpuid_eax(0x80000000);
	c->extended_cpuid_level = xlvl;
	if ((xlvl & 0xffff0000) == 0x80000000) {
		if (xlvl >= 0x80000001) {
			c->x86_capability[1] = cpuid_edx(0x80000001);
			c->x86_capability[6] = cpuid_ecx(0x80000001);
		}
		if (xlvl >= 0x80000004)
			get_model_name(c); /* Default name */
	}

	/* Transmeta-defined flags: level 0x80860001 */
	xlvl = cpuid_eax(0x80860000);
	if ((xlvl & 0xffff0000) == 0x80860000) {
		/* Don't set x86_cpuid_level here for now to not confuse. */
		if (xlvl >= 0x80860001)
			c->x86_capability[2] = cpuid_edx(0x80860001);
	}

	c->extended_cpuid_level = cpuid_eax(0x80000000);
	if (c->extended_cpuid_level >= 0x80000007)
		c->x86_power = cpuid_edx(0x80000007);

	switch (c->x86_vendor) {
	case X86_VENDOR_AMD:
		early_init_amd(c);
		break;
	case X86_VENDOR_INTEL:
		early_init_intel(c);
		break;
	}

}

/*
 * This does the hard work of actually picking apart the CPU stuff...
 */
void __cpuinit identify_cpu(struct cpuinfo_x86 *c)
{
	int i;

	early_identify_cpu(c);

	init_scattered_cpuid_features(c);

	c->apicid = phys_pkg_id(0);

	/*
	 * Vendor-specific initialization.  In this section we
	 * canonicalize the feature flags, meaning if there are
	 * features a certain CPU supports which CPUID doesn't
	 * tell us, CPUID claiming incorrect flags, or other bugs,
	 * we handle them here.
	 *
	 * At the end of this section, c->x86_capability better
	 * indicate the features this CPU genuinely supports!
	 */
	switch (c->x86_vendor) {
	case X86_VENDOR_AMD:
		init_amd(c);
		break;

	case X86_VENDOR_INTEL:
		init_intel(c);
		break;

	case X86_VENDOR_UNKNOWN:
	default:
		display_cacheinfo(c);
		break;
	}

	detect_ht(c);

	/*
	 * On SMP, boot_cpu_data holds the common feature set between
	 * all CPUs; so make sure that we indicate which features are
	 * common between the CPUs.  The first time this routine gets
	 * executed, c == &boot_cpu_data.
	 */
	if (c != &boot_cpu_data) {
		/* AND the already accumulated flags with these */
		for (i = 0; i < NCAPINTS; i++)
			boot_cpu_data.x86_capability[i] &= c->x86_capability[i];
	}

	/* Clear all flags overriden by options */
	for (i = 0; i < NCAPINTS; i++)
		c->x86_capability[i] &= ~cleared_cpu_caps[i];

#ifdef CONFIG_X86_MCE
	mcheck_init(c);
#endif
	select_idle_routine(c);

	if (c != &boot_cpu_data)
		mtrr_ap_init();
#ifdef CONFIG_NUMA
	numa_add_cpu(smp_processor_id());
#endif

}

static __init int setup_noclflush(char *arg)
{
	setup_clear_cpu_cap(X86_FEATURE_CLFLSH);
	return 1;
}
__setup("noclflush", setup_noclflush);

void __cpuinit print_cpu_info(struct cpuinfo_x86 *c)
{
	if (c->x86_model_id[0])
		printk(KERN_CONT "%s", c->x86_model_id);

	if (c->x86_mask || c->cpuid_level >= 0)
		printk(KERN_CONT " stepping %02x\n", c->x86_mask);
	else
		printk(KERN_CONT "\n");
}

static __init int setup_disablecpuid(char *arg)
{
	int bit;
	if (get_option(&arg, &bit) && bit < NCAPINTS*32)
		setup_clear_cpu_cap(bit);
	else
		return 0;
	return 1;
}
__setup("clearcpuid=", setup_disablecpuid);

/*
 *	Get CPU information for use by the procfs.
 */

static int show_cpuinfo(struct seq_file *m, void *v)
{
	struct cpuinfo_x86 *c = v;
	int cpu = 0, i;

#ifdef CONFIG_SMP
	cpu = c->cpu_index;
#endif

	seq_printf(m, "processor\t: %u\n"
		   "vendor_id\t: %s\n"
		   "cpu family\t: %d\n"
		   "model\t\t: %d\n"
		   "model name\t: %s\n",
		   (unsigned)cpu,
		   c->x86_vendor_id[0] ? c->x86_vendor_id : "unknown",
		   c->x86,
		   (int)c->x86_model,
		   c->x86_model_id[0] ? c->x86_model_id : "unknown");

	if (c->x86_mask || c->cpuid_level >= 0)
		seq_printf(m, "stepping\t: %d\n", c->x86_mask);
	else
		seq_printf(m, "stepping\t: unknown\n");

	if (cpu_has(c, X86_FEATURE_TSC)) {
		unsigned int freq = cpufreq_quick_get((unsigned)cpu);

		if (!freq)
			freq = cpu_khz;
		seq_printf(m, "cpu MHz\t\t: %u.%03u\n",
			   freq / 1000, (freq % 1000));
	}

	/* Cache size */
	if (c->x86_cache_size >= 0)
		seq_printf(m, "cache size\t: %d KB\n", c->x86_cache_size);

#ifdef CONFIG_SMP
	if (smp_num_siblings * c->x86_max_cores > 1) {
		seq_printf(m, "physical id\t: %d\n", c->phys_proc_id);
		seq_printf(m, "siblings\t: %d\n",
			       cpus_weight(per_cpu(cpu_core_map, cpu)));
		seq_printf(m, "core id\t\t: %d\n", c->cpu_core_id);
		seq_printf(m, "cpu cores\t: %d\n", c->booted_cores);
	}
#endif

	seq_printf(m,
		   "fpu\t\t: yes\n"
		   "fpu_exception\t: yes\n"
		   "cpuid level\t: %d\n"
		   "wp\t\t: yes\n"
		   "flags\t\t:",
		   c->cpuid_level);

	for (i = 0; i < 32*NCAPINTS; i++)
		if (cpu_has(c, i) && x86_cap_flags[i] != NULL)
			seq_printf(m, " %s", x86_cap_flags[i]);

	seq_printf(m, "\nbogomips\t: %lu.%02lu\n",
		   c->loops_per_jiffy/(500000/HZ),
		   (c->loops_per_jiffy/(5000/HZ)) % 100);

	if (c->x86_tlbsize > 0)
		seq_printf(m, "TLB size\t: %d 4K pages\n", c->x86_tlbsize);
	seq_printf(m, "clflush size\t: %d\n", c->x86_clflush_size);
	seq_printf(m, "cache_alignment\t: %d\n", c->x86_cache_alignment);

	seq_printf(m, "address sizes\t: %u bits physical, %u bits virtual\n",
		   c->x86_phys_bits, c->x86_virt_bits);

	seq_printf(m, "power management:");
	for (i = 0; i < 32; i++) {
		if (c->x86_power & (1 << i)) {
			if (i < ARRAY_SIZE(x86_power_flags) &&
			    x86_power_flags[i])
				seq_printf(m, "%s%s",
					   x86_power_flags[i][0]?" ":"",
					   x86_power_flags[i]);
			else
				seq_printf(m, " [%d]", i);
		}
	}

	seq_printf(m, "\n\n");

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	if (*pos == 0)	/* just in case, cpu 0 is not the first */
		*pos = first_cpu(cpu_online_map);
	if ((*pos) < NR_CPUS && cpu_online(*pos))
		return &cpu_data(*pos);
	return NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	*pos = next_cpu(*pos, cpu_online_map);
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start = c_start,
	.next =	c_next,
	.stop =	c_stop,
	.show =	show_cpuinfo,
};
