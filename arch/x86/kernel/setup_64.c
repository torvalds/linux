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
#include <asm/pci-direct.h>
#include <linux/efi.h>
#include <linux/acpi.h>
#include <linux/kallsyms.h>
#include <linux/edd.h>
#include <linux/iscsi_ibft.h>
#include <linux/mmzone.h>
#include <linux/kexec.h>
#include <linux/cpufreq.h>
#include <linux/dmi.h>
#include <linux/dma-mapping.h>
#include <linux/ctype.h>
#include <linux/sort.h>
#include <linux/uaccess.h>
#include <linux/init_ohci1394_dma.h>
#include <linux/kvm_para.h>

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
#include <asm/mpspec.h>
#include <asm/dma.h>
#include <asm/gart.h>
#include <asm/mpspec.h>
#include <asm/mmu_context.h>
#include <asm/proto.h>
#include <asm/setup.h>
#include <asm/numa.h>
#include <asm/sections.h>
#include <asm/dmi.h>
#include <asm/cacheflush.h>
#include <asm/mce.h>
#include <asm/ds.h>
#include <asm/topology.h>
#include <asm/trampoline.h>
#include <asm/pat.h>

#include <mach_apic.h>
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

static char __initdata command_line[COMMAND_LINE_SIZE];

static struct resource standard_io_resources[] = {
	{ .name = "dma1", .start = 0x00, .end = 0x1f,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "pic1", .start = 0x20, .end = 0x21,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer0", .start = 0x40, .end = 0x43,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "timer1", .start = 0x50, .end = 0x53,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard", .start = 0x60, .end = 0x60,
		.flags = IORESOURCE_BUSY | IORESOURCE_IO },
	{ .name = "keyboard", .start = 0x64, .end = 0x64,
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

static void __init early_cpu_init(void);
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
	early_res_to_bootmem(0, end_pfn<<PAGE_SHIFT);
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

/* Overridden in paravirt.c if CONFIG_PARAVIRT */
void __attribute__((weak)) __init memory_setup(void)
{
       machine_specific_memory_setup();
}

/* Current gdt points %fs at the "master" per-cpu area: after this,
 * it's on the real one. */
void switch_to_new_gdt(void)
{
	struct desc_ptr gdt_descr;

	gdt_descr.address = (long)get_cpu_gdt_table(smp_processor_id());
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);
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
		     "EL64", 4)) {
		efi_enabled = 1;
		efi_reserve_early();
	}
#endif

	ARCH_SETUP

	setup_memory_map();
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

	early_cpu_init();
	early_identify_cpu(&boot_cpu_data);

	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	parse_setup_data();

	parse_early_param();

	if (acpi_mps_check()) {
		disable_apic = 1;
		clear_cpu_cap(&boot_cpu_data, X86_FEATURE_APIC);
	}

#ifdef CONFIG_PROVIDE_OHCI1394_DMA_INIT
	if (init_ohci1394_dma_early)
		init_ohci1394_dma_on_all_controllers();
#endif

	finish_e820_parsing();

	/* after parse_early_param, so could debug it */
	insert_resource(&iomem_resource, &code_resource);
	insert_resource(&iomem_resource, &data_resource);
	insert_resource(&iomem_resource, &bss_resource);

	early_gart_iommu_check();

	e820_register_active_regions(0, 0, -1UL);
	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	end_pfn = e820_end_of_ram();

	/* pre allocte 4k for mptable mpc */
	early_reserve_e820_mpc_new();
	/* update e820 for memory not covered by WB MTRRs */
	mtrr_bp_init();
	if (mtrr_trim_uncached_memory(end_pfn)) {
		remove_all_active_ranges();
		e820_register_active_regions(0, 0, -1UL);
		end_pfn = e820_end_of_ram();
	}

	num_physpages = end_pfn;

	check_efer();

	max_pfn_mapped = init_memory_mapping(0, (end_pfn << PAGE_SHIFT));
	if (efi_enabled)
		efi_init();

	vsmp_init();

	dmi_scan_machine();

	io_delay_init();

#ifdef CONFIG_KVM_CLOCK
	kvmclock_init();
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

	dma32_reserve_bootmem();

#ifdef CONFIG_ACPI_SLEEP
	/*
	 * Reserve low memory region for sleep support.
	 */
       acpi_reserve_bootmem();
#endif

#ifdef CONFIG_X86_MPPARSE
       /*
	* Find and reserve possible boot-time SMP configuration:
	*/
	find_smp_config();
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	if (boot_params.hdr.type_of_loader && boot_params.hdr.ramdisk_image) {
		unsigned long ramdisk_image = boot_params.hdr.ramdisk_image;
		unsigned long ramdisk_size  = boot_params.hdr.ramdisk_size;
		unsigned long ramdisk_end   = ramdisk_image + ramdisk_size;
		unsigned long end_of_mem    = end_pfn << PAGE_SHIFT;

		if (ramdisk_end <= end_of_mem) {
			/*
			 * don't need to reserve again, already reserved early
			 * in x86_64_start_kernel, and early_res_to_bootmem
			 * convert that to reserved in bootmem
			 */
			initrd_start = ramdisk_image + PAGE_OFFSET;
			initrd_end = initrd_start+ramdisk_size;
		} else {
			free_bootmem(ramdisk_image, ramdisk_size);
			printk(KERN_ERR "initrd extends beyond end of memory "
			       "(0x%08lx > 0x%08lx)\ndisabling initrd\n",
			       ramdisk_end, end_of_mem);
			initrd_start = 0;
		}
	}
#endif
	reserve_crashkernel();

	reserve_ibft_region();

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

#ifdef CONFIG_X86_MPPARSE
	/*
	 * get boot-time SMP configuration:
	 */
	if (smp_found_config)
		get_smp_config();
#endif
	init_apic_mappings();
	ioapic_init_mappings();

	kvm_guest_init();

	/*
	 * We trust e820 completely. No explicit ROM probing in memory.
	 */
	e820_reserve_resources();
	e820_mark_nosave_regions(end_pfn);

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

struct cpu_dev *cpu_devs[X86_VENDOR_NUM] = {};

static void __cpuinit default_init(struct cpuinfo_x86 *c)
{
	display_cacheinfo(c);
}

static struct cpu_dev __cpuinitdata default_cpu = {
	.c_init	= default_init,
	.c_vendor = "Unknown",
};
static struct cpu_dev *this_cpu __cpuinitdata = &default_cpu;

int __cpuinit get_model_name(struct cpuinfo_x86 *c)
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


void __cpuinit display_cacheinfo(struct cpuinfo_x86 *c)
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

static void __cpuinit get_cpu_vendor(struct cpuinfo_x86 *c)
{
	char *v = c->x86_vendor_id;
	int i;
	static int printed;

	for (i = 0; i < X86_VENDOR_NUM; i++) {
		if (cpu_devs[i]) {
			if (!strcmp(v, cpu_devs[i]->c_ident[0]) ||
			    (cpu_devs[i]->c_ident[1] &&
			    !strcmp(v, cpu_devs[i]->c_ident[1]))) {
				c->x86_vendor = i;
				this_cpu = cpu_devs[i];
				return;
			}
		}
	}
	if (!printed) {
		printed++;
		printk(KERN_ERR "CPU: Vendor unknown, using generic init.\n");
		printk(KERN_ERR "CPU: Your system may be unstable.\n");
	}
	c->x86_vendor = X86_VENDOR_UNKNOWN;
}

static void __init early_cpu_support_print(void)
{
	int i,j;
	struct cpu_dev *cpu_devx;

	printk("KERNEL supported cpus:\n");
	for (i = 0; i < X86_VENDOR_NUM; i++) {
		cpu_devx = cpu_devs[i];
		if (!cpu_devx)
			continue;
		for (j = 0; j < 2; j++) {
			if (!cpu_devx->c_ident[j])
				continue;
			printk("  %s %s\n", cpu_devx->c_vendor,
				cpu_devx->c_ident[j]);
		}
	}
}

static void __init early_cpu_init(void)
{
        struct cpu_vendor_dev *cvdev;

        for (cvdev = __x86cpuvendor_start ;
             cvdev < __x86cpuvendor_end   ;
             cvdev++)
                cpu_devs[cvdev->vendor] = cvdev->cpu_dev;
	early_cpu_support_print();
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
		if (test_cpu_cap(c, X86_FEATURE_CLFLSH))
			c->x86_clflush_size = ((misc >> 8) & 0xff) * 8;
	} else {
		/* Have CPUID level 0 only - unheard of */
		c->x86 = 4;
	}

	c->initial_apicid = (cpuid_ebx(1) >> 24) & 0xff;
#ifdef CONFIG_SMP
	c->phys_proc_id = c->initial_apicid;
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

	if (c->x86_vendor != X86_VENDOR_UNKNOWN &&
	    cpu_devs[c->x86_vendor]->c_early_init)
		cpu_devs[c->x86_vendor]->c_early_init(c);

	validate_pat_support(c);

	/* early_param could clear that, but recall get it set again */
	if (disable_apic)
		clear_cpu_cap(c, X86_FEATURE_APIC);
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
	if (this_cpu->c_init)
		this_cpu->c_init(c);

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

#ifdef CONFIG_NUMA
	numa_add_cpu(smp_processor_id());
#endif

}

void __cpuinit identify_boot_cpu(void)
{
	identify_cpu(&boot_cpu_data);
}

void __cpuinit identify_secondary_cpu(struct cpuinfo_x86 *c)
{
	BUG_ON(c == &boot_cpu_data);
	identify_cpu(c);
	mtrr_ap_init();
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
