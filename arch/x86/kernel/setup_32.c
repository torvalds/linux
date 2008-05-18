/*
 *  Copyright (C) 1995  Linus Torvalds
 *
 *  Support of BIGMEM added by Gerhard Wichert, Siemens AG, July 1999
 *
 *  Memory region support
 *	David Parsons <orc@pell.chi.il.us>, July-August 1999
 *
 *  Added E820 sanitization routine (removes overlapping memory regions);
 *  Brian Moyle <bmoyle@mvista.com>, February 2001
 *
 * Moved CPU detection code to cpu/${cpu}.c
 *    Patrick Mochel <mochel@osdl.org>, March 2002
 *
 *  Provisions for empty E820 memory regions (reported by certain BIOSes).
 *  Alex Achenbach <xela@slit.de>, December 2002.
 *
 */

/*
 * This file handles the architecture-dependent parts of initialization
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/screen_info.h>
#include <linux/ioport.h>
#include <linux/acpi.h>
#include <linux/apm_bios.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/console.h>
#include <linux/mca.h>
#include <linux/root_dev.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/edd.h>
#include <linux/iscsi_ibft.h>
#include <linux/nodemask.h>
#include <linux/kexec.h>
#include <linux/crash_dump.h>
#include <linux/dmi.h>
#include <linux/pfn.h>
#include <linux/pci.h>
#include <linux/init_ohci1394_dma.h>
#include <linux/kvm_para.h>

#include <video/edid.h>

#include <asm/mtrr.h>
#include <asm/apic.h>
#include <asm/e820.h>
#include <asm/mpspec.h>
#include <asm/mmzone.h>
#include <asm/setup.h>
#include <asm/arch_hooks.h>
#include <asm/sections.h>
#include <asm/io_apic.h>
#include <asm/ist.h>
#include <asm/io.h>
#include <asm/vmi.h>
#include <setup_arch.h>
#include <asm/bios_ebda.h>
#include <asm/cacheflush.h>
#include <asm/processor.h>

/* This value is set up by the early boot code to point to the value
   immediately after the boot time page tables.  It contains a *physical*
   address, and must not be in the .bss segment! */
unsigned long init_pg_tables_end __initdata = ~0UL;

/*
 * Machine setup..
 */
static struct resource data_resource = {
	.name	= "Kernel data",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource code_resource = {
	.name	= "Kernel code",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource bss_resource = {
	.name	= "Kernel bss",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource video_ram_resource = {
	.name	= "Video RAM area",
	.start	= 0xa0000,
	.end	= 0xbffff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_MEM
};

static struct resource standard_io_resources[] = { {
	.name	= "dma1",
	.start	= 0x0000,
	.end	= 0x001f,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name	= "pic1",
	.start	= 0x0020,
	.end	= 0x0021,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name   = "timer0",
	.start	= 0x0040,
	.end    = 0x0043,
	.flags  = IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name   = "timer1",
	.start  = 0x0050,
	.end    = 0x0053,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name	= "keyboard",
	.start	= 0x0060,
	.end	= 0x0060,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name	= "keyboard",
	.start	= 0x0064,
	.end	= 0x0064,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name	= "dma page reg",
	.start	= 0x0080,
	.end	= 0x008f,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name	= "pic2",
	.start	= 0x00a0,
	.end	= 0x00a1,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name	= "dma2",
	.start	= 0x00c0,
	.end	= 0x00df,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
}, {
	.name	= "fpu",
	.start	= 0x00f0,
	.end	= 0x00ff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_IO
} };

/* cpu data as detected by the assembly code in head.S */
struct cpuinfo_x86 new_cpu_data __cpuinitdata = { 0, 0, 0, 0, -1, 1, 0, 0, -1 };
/* common cpu data for all cpus */
struct cpuinfo_x86 boot_cpu_data __read_mostly = { 0, 0, 0, 0, -1, 1, 0, 0, -1 };
EXPORT_SYMBOL(boot_cpu_data);

unsigned int def_to_bigsmp;

#ifndef CONFIG_X86_PAE
unsigned long mmu_cr4_features;
#else
unsigned long mmu_cr4_features = X86_CR4_PAE;
#endif

/* for MCA, but anyone else can use it if they want */
unsigned int machine_id;
unsigned int machine_submodel_id;
unsigned int BIOS_revision;

/* Boot loader ID as an integer, for the benefit of proc_dointvec */
int bootloader_type;

/* user-defined highmem size */
static unsigned int highmem_pages = -1;

/*
 * Setup options
 */
struct screen_info screen_info;
EXPORT_SYMBOL(screen_info);
struct apm_info apm_info;
EXPORT_SYMBOL(apm_info);
struct edid_info edid_info;
EXPORT_SYMBOL_GPL(edid_info);
struct ist_info ist_info;
#if defined(CONFIG_X86_SPEEDSTEP_SMI) || \
	defined(CONFIG_X86_SPEEDSTEP_SMI_MODULE)
EXPORT_SYMBOL(ist_info);
#endif

extern void early_cpu_init(void);
extern int root_mountflags;

unsigned long saved_video_mode;

#define RAMDISK_IMAGE_START_MASK	0x07FF
#define RAMDISK_PROMPT_FLAG		0x8000
#define RAMDISK_LOAD_FLAG		0x4000

static char __initdata command_line[COMMAND_LINE_SIZE];

#ifndef CONFIG_DEBUG_BOOT_PARAMS
struct boot_params __initdata boot_params;
#else
struct boot_params boot_params;
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

#ifdef CONFIG_PROC_VMCORE
/* elfcorehdr= specifies the location of elf core header
 * stored by the crashed kernel.
 */
static int __init parse_elfcorehdr(char *arg)
{
	if (!arg)
		return -EINVAL;

	elfcorehdr_addr = memparse(arg, &arg);
	return 0;
}
early_param("elfcorehdr", parse_elfcorehdr);
#endif /* CONFIG_PROC_VMCORE */

/*
 * highmem=size forces highmem to be exactly 'size' bytes.
 * This works even on boxes that have no highmem otherwise.
 * This also works to reduce highmem size on bigger boxes.
 */
static int __init parse_highmem(char *arg)
{
	if (!arg)
		return -EINVAL;

	highmem_pages = memparse(arg, &arg) >> PAGE_SHIFT;
	return 0;
}
early_param("highmem", parse_highmem);

/*
 * vmalloc=size forces the vmalloc area to be exactly 'size'
 * bytes. This can be used to increase (or decrease) the
 * vmalloc area - the default is 128m.
 */
static int __init parse_vmalloc(char *arg)
{
	if (!arg)
		return -EINVAL;

	__VMALLOC_RESERVE = memparse(arg, &arg);
	return 0;
}
early_param("vmalloc", parse_vmalloc);

/*
 * reservetop=size reserves a hole at the top of the kernel address space which
 * a hypervisor can load into later.  Needed for dynamically loaded hypervisors,
 * so relocating the fixmap can be done before paging initialization.
 */
static int __init parse_reservetop(char *arg)
{
	unsigned long address;

	if (!arg)
		return -EINVAL;

	address = memparse(arg, &arg);
	reserve_top_address(address);
	return 0;
}
early_param("reservetop", parse_reservetop);

/*
 * Determine low and high memory ranges:
 */
unsigned long __init find_max_low_pfn(void)
{
	unsigned long max_low_pfn;

	max_low_pfn = max_pfn;
	if (max_low_pfn > MAXMEM_PFN) {
		if (highmem_pages == -1)
			highmem_pages = max_pfn - MAXMEM_PFN;
		if (highmem_pages + MAXMEM_PFN < max_pfn)
			max_pfn = MAXMEM_PFN + highmem_pages;
		if (highmem_pages + MAXMEM_PFN > max_pfn) {
			printk("only %luMB highmem pages available, ignoring highmem size of %uMB.\n", pages_to_mb(max_pfn - MAXMEM_PFN), pages_to_mb(highmem_pages));
			highmem_pages = 0;
		}
		max_low_pfn = MAXMEM_PFN;
#ifndef CONFIG_HIGHMEM
		/* Maximum memory usable is what is directly addressable */
		printk(KERN_WARNING "Warning only %ldMB will be used.\n",
					MAXMEM>>20);
		if (max_pfn > MAX_NONPAE_PFN)
			printk(KERN_WARNING "Use a HIGHMEM64G enabled kernel.\n");
		else
			printk(KERN_WARNING "Use a HIGHMEM enabled kernel.\n");
		max_pfn = MAXMEM_PFN;
#else /* !CONFIG_HIGHMEM */
#ifndef CONFIG_HIGHMEM64G
		if (max_pfn > MAX_NONPAE_PFN) {
			max_pfn = MAX_NONPAE_PFN;
			printk(KERN_WARNING "Warning only 4GB will be used.\n");
			printk(KERN_WARNING "Use a HIGHMEM64G enabled kernel.\n");
		}
#endif /* !CONFIG_HIGHMEM64G */
#endif /* !CONFIG_HIGHMEM */
	} else {
		if (highmem_pages == -1)
			highmem_pages = 0;
#ifdef CONFIG_HIGHMEM
		if (highmem_pages >= max_pfn) {
			printk(KERN_ERR "highmem size specified (%uMB) is bigger than pages available (%luMB)!.\n", pages_to_mb(highmem_pages), pages_to_mb(max_pfn));
			highmem_pages = 0;
		}
		if (highmem_pages) {
			if (max_low_pfn-highmem_pages < 64*1024*1024/PAGE_SIZE){
				printk(KERN_ERR "highmem size %uMB results in smaller than 64MB lowmem, ignoring it.\n", pages_to_mb(highmem_pages));
				highmem_pages = 0;
			}
			max_low_pfn -= highmem_pages;
		}
#else
		if (highmem_pages)
			printk(KERN_ERR "ignoring highmem size on non-highmem kernel!\n");
#endif
	}
	return max_low_pfn;
}

#ifndef CONFIG_NEED_MULTIPLE_NODES
static void __init setup_bootmem_allocator(void);
static unsigned long __init setup_memory(void)
{
	/*
	 * partially used pages are not usable - thus
	 * we are rounding upwards:
	 */
	min_low_pfn = PFN_UP(init_pg_tables_end);

	max_low_pfn = find_max_low_pfn();

#ifdef CONFIG_HIGHMEM
	highstart_pfn = highend_pfn = max_pfn;
	if (max_pfn > max_low_pfn) {
		highstart_pfn = max_low_pfn;
	}
	printk(KERN_NOTICE "%ldMB HIGHMEM available.\n",
		pages_to_mb(highend_pfn - highstart_pfn));
	num_physpages = highend_pfn;
	high_memory = (void *) __va(highstart_pfn * PAGE_SIZE - 1) + 1;
#else
	num_physpages = max_low_pfn;
	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE - 1) + 1;
#endif
#ifdef CONFIG_FLATMEM
	max_mapnr = num_physpages;
#endif
	printk(KERN_NOTICE "%ldMB LOWMEM available.\n",
			pages_to_mb(max_low_pfn));

	setup_bootmem_allocator();

	return max_low_pfn;
}

static void __init zone_sizes_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];
	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
	max_zone_pfns[ZONE_DMA] =
		virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
#ifdef CONFIG_HIGHMEM
	max_zone_pfns[ZONE_HIGHMEM] = highend_pfn;
	add_active_range(0, 0, highend_pfn);
#else
	add_active_range(0, 0, max_low_pfn);
#endif

	free_area_init_nodes(max_zone_pfns);
}
#else
extern unsigned long __init setup_memory(void);
extern void zone_sizes_init(void);
#endif /* !CONFIG_NEED_MULTIPLE_NODES */

static inline unsigned long long get_total_mem(void)
{
	unsigned long long total;

	total = max_low_pfn - min_low_pfn;
#ifdef CONFIG_HIGHMEM
	total += highend_pfn - highstart_pfn;
#endif

	return total << PAGE_SHIFT;
}

#ifdef CONFIG_KEXEC
static void __init reserve_crashkernel(void)
{
	unsigned long long total_mem;
	unsigned long long crash_size, crash_base;
	int ret;

	total_mem = get_total_mem();

	ret = parse_crashkernel(boot_command_line, total_mem,
			&crash_size, &crash_base);
	if (ret == 0 && crash_size > 0) {
		if (crash_base > 0) {
			printk(KERN_INFO "Reserving %ldMB of memory at %ldMB "
					"for crashkernel (System RAM: %ldMB)\n",
					(unsigned long)(crash_size >> 20),
					(unsigned long)(crash_base >> 20),
					(unsigned long)(total_mem >> 20));
			crashk_res.start = crash_base;
			crashk_res.end   = crash_base + crash_size - 1;
			reserve_bootmem(crash_base, crash_size,
					BOOTMEM_DEFAULT);
		} else
			printk(KERN_INFO "crashkernel reservation failed - "
					"you have to specify a base address\n");
	}
}
#else
static inline void __init reserve_crashkernel(void)
{}
#endif

#ifdef CONFIG_BLK_DEV_INITRD

static bool do_relocate_initrd = false;

static void __init reserve_initrd(void)
{
	unsigned long ramdisk_image = boot_params.hdr.ramdisk_image;
	unsigned long ramdisk_size  = boot_params.hdr.ramdisk_size;
	unsigned long ramdisk_end   = ramdisk_image + ramdisk_size;
	unsigned long end_of_lowmem = max_low_pfn << PAGE_SHIFT;
	unsigned long ramdisk_here;

	if (!boot_params.hdr.type_of_loader ||
	    !ramdisk_image || !ramdisk_size)
		return;		/* No initrd provided by bootloader */

	initrd_start = 0;

	if (ramdisk_end < ramdisk_image) {
		free_bootmem(ramdisk_image, ramdisk_size);
		printk(KERN_ERR "initrd wraps around end of memory, "
		       "disabling initrd\n");
		return;
	}
	if (ramdisk_size >= end_of_lowmem/2) {
		free_bootmem(ramdisk_image, ramdisk_size);
		printk(KERN_ERR "initrd too large to handle, "
		       "disabling initrd\n");
		return;
	}

	if (ramdisk_end <= end_of_lowmem) {
		/* All in lowmem, easy case */
		/*
		 * don't need to reserve again, already reserved early
		 * in i386_start_kernel, and early_res_to_bootmem
		 * convert that to reserved in bootmem
		 */
		initrd_start = ramdisk_image + PAGE_OFFSET;
		initrd_end = initrd_start+ramdisk_size;
		return;
	}

	/* We need to move the initrd down into lowmem */
	ramdisk_here = (end_of_lowmem - ramdisk_size) & PAGE_MASK;

	/* Note: this includes all the lowmem currently occupied by
	   the initrd, we rely on that fact to keep the data intact. */
	reserve_bootmem(ramdisk_here, ramdisk_size, BOOTMEM_DEFAULT);
	initrd_start = ramdisk_here + PAGE_OFFSET;
	initrd_end   = initrd_start + ramdisk_size;

	do_relocate_initrd = true;
}

#define MAX_MAP_CHUNK	(NR_FIX_BTMAPS << PAGE_SHIFT)

static void __init relocate_initrd(void)
{
	unsigned long ramdisk_image = boot_params.hdr.ramdisk_image;
	unsigned long ramdisk_size  = boot_params.hdr.ramdisk_size;
	unsigned long end_of_lowmem = max_low_pfn << PAGE_SHIFT;
	unsigned long ramdisk_here;
	unsigned long slop, clen, mapaddr;
	char *p, *q;

	if (!do_relocate_initrd)
		return;

	ramdisk_here = initrd_start - PAGE_OFFSET;

	q = (char *)initrd_start;

	/* Copy any lowmem portion of the initrd */
	if (ramdisk_image < end_of_lowmem) {
		clen = end_of_lowmem - ramdisk_image;
		p = (char *)__va(ramdisk_image);
		memcpy(q, p, clen);
		q += clen;
		/* need to free these low pages...*/
		free_bootmem(ramdisk_image, clen);
		ramdisk_image += clen;
		ramdisk_size  -= clen;
	}

	/* Copy the highmem portion of the initrd */
	while (ramdisk_size) {
		slop = ramdisk_image & ~PAGE_MASK;
		clen = ramdisk_size;
		if (clen > MAX_MAP_CHUNK-slop)
			clen = MAX_MAP_CHUNK-slop;
		mapaddr = ramdisk_image & PAGE_MASK;
		p = early_ioremap(mapaddr, clen+slop);
		memcpy(q, p+slop, clen);
		early_iounmap(p, clen+slop);
		q += clen;
		ramdisk_image += clen;
		ramdisk_size  -= clen;
	}
	/* high pages is not converted by early_res_to_bootmem */
}

#endif /* CONFIG_BLK_DEV_INITRD */

void __init setup_bootmem_allocator(void)
{
	unsigned long bootmap_size, bootmap;
	/*
	 * Initialize the boot-time allocator (with low memory only):
	 */
	bootmap_size = bootmem_bootmap_pages(max_low_pfn)<<PAGE_SHIFT;
	bootmap = find_e820_area(min_low_pfn<<PAGE_SHIFT,
				 max_low_pfn<<PAGE_SHIFT, bootmap_size,
				 PAGE_SIZE);
	if (bootmap == -1L)
		panic("Cannot find bootmem map of size %ld\n", bootmap_size);
	bootmap_size = init_bootmem(bootmap >> PAGE_SHIFT, max_low_pfn);
	register_bootmem_low_pages(max_low_pfn);
	early_res_to_bootmem(0, max_low_pfn<<PAGE_SHIFT);
	reserve_bootmem(bootmap, bootmap_size, BOOTMEM_DEFAULT);

#ifdef CONFIG_ACPI_SLEEP
	/*
	 * Reserve low memory region for sleep support.
	 */
	acpi_reserve_bootmem();
#endif
#ifdef CONFIG_X86_FIND_SMP_CONFIG
	/*
	 * Find and reserve possible boot-time SMP configuration:
	 */
	find_smp_config();
#endif
#ifdef CONFIG_BLK_DEV_INITRD
	reserve_initrd();
#endif
	numa_kva_reserve();
	reserve_crashkernel();

	reserve_ibft_region();
}

/*
 * The node 0 pgdat is initialized before all of these because
 * it's needed for bootmem.  node>0 pgdats have their virtual
 * space allocated before the pagetables are in place to access
 * them, so they can't be cleared then.
 *
 * This should all compile down to nothing when NUMA is off.
 */
static void __init remapped_pgdat_init(void)
{
	int nid;

	for_each_online_node(nid) {
		if (nid != 0)
			memset(NODE_DATA(nid), 0, sizeof(struct pglist_data));
	}
}

#ifdef CONFIG_MCA
static void set_mca_bus(int x)
{
	MCA_bus = x;
}
#else
static void set_mca_bus(int x) { }
#endif

#ifdef CONFIG_NUMA
/*
 * In the golden day, when everything among i386 and x86_64 will be
 * integrated, this will not live here
 */
void *x86_cpu_to_node_map_early_ptr;
int x86_cpu_to_node_map_init[NR_CPUS] = {
	[0 ... NR_CPUS-1] = NUMA_NO_NODE
};
DEFINE_PER_CPU(int, x86_cpu_to_node_map) = NUMA_NO_NODE;
#endif

/*
 * Determine if we were loaded by an EFI loader.  If so, then we have also been
 * passed the efi memmap, systab, etc., so we should use these data structures
 * for initialization.  Note, the efi init code path is determined by the
 * global efi_enabled. This allows the same kernel image to be used on existing
 * systems (with a traditional BIOS) as well as on EFI systems.
 */
void __init setup_arch(char **cmdline_p)
{
	unsigned long max_low_pfn;

	memcpy(&boot_cpu_data, &new_cpu_data, sizeof(new_cpu_data));
	pre_setup_arch_hook();
	early_cpu_init();
	early_ioremap_init();

#ifdef CONFIG_EFI
	if (!strncmp((char *)&boot_params.efi_info.efi_loader_signature,
		     "EL32", 4))
		efi_enabled = 1;
#endif

	ROOT_DEV = old_decode_dev(boot_params.hdr.root_dev);
	screen_info = boot_params.screen_info;
	edid_info = boot_params.edid_info;
	apm_info.bios = boot_params.apm_bios_info;
	ist_info = boot_params.ist_info;
	saved_video_mode = boot_params.hdr.vid_mode;
	if( boot_params.sys_desc_table.length != 0 ) {
		set_mca_bus(boot_params.sys_desc_table.table[3] & 0x2);
		machine_id = boot_params.sys_desc_table.table[0];
		machine_submodel_id = boot_params.sys_desc_table.table[1];
		BIOS_revision = boot_params.sys_desc_table.table[2];
	}
	bootloader_type = boot_params.hdr.type_of_loader;

#ifdef CONFIG_BLK_DEV_RAM
	rd_image_start = boot_params.hdr.ram_size & RAMDISK_IMAGE_START_MASK;
	rd_prompt = ((boot_params.hdr.ram_size & RAMDISK_PROMPT_FLAG) != 0);
	rd_doload = ((boot_params.hdr.ram_size & RAMDISK_LOAD_FLAG) != 0);
#endif
	ARCH_SETUP

	setup_memory_map();

	copy_edd();

	if (!boot_params.hdr.root_flags)
		root_mountflags &= ~MS_RDONLY;
	init_mm.start_code = (unsigned long) _text;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = init_pg_tables_end + PAGE_OFFSET;

	code_resource.start = virt_to_phys(_text);
	code_resource.end = virt_to_phys(_etext)-1;
	data_resource.start = virt_to_phys(_etext);
	data_resource.end = virt_to_phys(_edata)-1;
	bss_resource.start = virt_to_phys(&__bss_start);
	bss_resource.end = virt_to_phys(&__bss_stop)-1;

	parse_early_param();

	finish_e820_parsing();

	strlcpy(command_line, boot_command_line, COMMAND_LINE_SIZE);
	*cmdline_p = command_line;

	if (efi_enabled)
		efi_init();

	/* update e820 for memory not covered by WB MTRRs */
	propagate_e820_map();
	mtrr_bp_init();
	if (mtrr_trim_uncached_memory(max_pfn))
		propagate_e820_map();

	max_low_pfn = setup_memory();

#ifdef CONFIG_KVM_CLOCK
	kvmclock_init();
#endif

#ifdef CONFIG_VMI
	/*
	 * Must be after max_low_pfn is determined, and before kernel
	 * pagetables are setup.
	 */
	vmi_init();
#endif
	kvm_guest_init();

	/*
	 * NOTE: before this point _nobody_ is allowed to allocate
	 * any memory using the bootmem allocator.  Although the
	 * allocator is now initialised only the first 8Mb of the kernel
	 * virtual address space has been mapped.  All allocations before
	 * paging_init() has completed must use the alloc_bootmem_low_pages()
	 * variant (which allocates DMA'able memory) and care must be taken
	 * not to exceed the 8Mb limit.
	 */

	paging_init();

	/*
	 * NOTE: On x86-32, only from this point on, fixmaps are ready for use.
	 */

#ifdef CONFIG_PROVIDE_OHCI1394_DMA_INIT
	if (init_ohci1394_dma_early)
		init_ohci1394_dma_on_all_controllers();
#endif

	remapped_pgdat_init();
	sparse_init();
	zone_sizes_init();

	/*
	 * NOTE: at this point the bootmem allocator is fully available.
	 */

#ifdef CONFIG_BLK_DEV_INITRD
	relocate_initrd();
#endif

	paravirt_post_allocator_init();

	dmi_scan_machine();

	io_delay_init();

#ifdef CONFIG_X86_SMP
	/*
	 * setup to use the early static init tables during kernel startup
	 * X86_SMP will exclude sub-arches that don't deal well with it.
	 */
	x86_cpu_to_apicid_early_ptr = (void *)x86_cpu_to_apicid_init;
	x86_bios_cpu_apicid_early_ptr = (void *)x86_bios_cpu_apicid_init;
#ifdef CONFIG_NUMA
	x86_cpu_to_node_map_early_ptr = (void *)x86_cpu_to_node_map_init;
#endif
#endif

#ifdef CONFIG_X86_GENERICARCH
	generic_apic_probe();
#endif

#ifdef CONFIG_ACPI
	/*
	 * Parse the ACPI tables for possible boot-time SMP configuration.
	 */
	acpi_boot_table_init();
#endif

	early_quirks();

#ifdef CONFIG_ACPI
	acpi_boot_init();

#if defined(CONFIG_SMP) && defined(CONFIG_X86_PC)
	if (def_to_bigsmp)
		printk(KERN_WARNING "More than 8 CPUs detected and "
			"CONFIG_X86_PC cannot handle it.\nUse "
			"CONFIG_X86_GENERICARCH or CONFIG_X86_BIGSMP.\n");
#endif
#endif
#ifdef CONFIG_X86_LOCAL_APIC
	if (smp_found_config)
		get_smp_config();
#endif

	e820_setup_gap();
	e820_mark_nosave_regions();

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	if (!efi_enabled || (efi_mem_type(0xa0000) != EFI_CONVENTIONAL_MEMORY))
		conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
}

/*
 * Request address space for all standard resources
 *
 * This is called just before pcibios_init(), which is also a
 * subsys_initcall, but is linked in later (in arch/i386/pci/common.c).
 */
static int __init request_standard_resources(void)
{
	int i;

	printk(KERN_INFO "Setting up standard PCI resources\n");
	init_iomem_resources(&code_resource, &data_resource, &bss_resource);

	request_resource(&iomem_resource, &video_ram_resource);

	/* request I/O space for devices used on all i[345]86 PCs */
	for (i = 0; i < ARRAY_SIZE(standard_io_resources); i++)
		request_resource(&ioport_resource, &standard_io_resources[i]);
	return 0;
}

subsys_initcall(request_standard_resources);
