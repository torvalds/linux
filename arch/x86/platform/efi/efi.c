/*
 * Common EFI (Extensible Firmware Interface) support functions
 * Based on Extensible Firmware Interface Specification version 1.0
 *
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999-2002 Hewlett-Packard Co.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2005-2008 Intel Co.
 *	Fenghua Yu <fenghua.yu@intel.com>
 *	Bibo Mao <bibo.mao@intel.com>
 *	Chandramouli Narayanan <mouli@linux.intel.com>
 *	Huang Ying <ying.huang@intel.com>
 * Copyright (C) 2013 SuSE Labs
 *	Borislav Petkov <bp@suse.de> - runtime services VA mapping
 *
 * Copied from efi_32.c to eliminate the duplicated code between EFI
 * 32/64 support code. --ying 2007-10-26
 *
 * All EFI Runtime Services are not implemented yet as EFI only
 * supports physical mode addressing on SoftSDV. This is to be fixed
 * in a future version.  --drummond 1999-07-20
 *
 * Implemented EFI runtime services and virtual mode calls.  --davidm
 *
 * Goutham Rao: <goutham.rao@intel.com>
 *	Skip non-WB memory and ignore empty memory ranges.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/efi.h>
#include <linux/efi-bgrt.h>
#include <linux/export.h>
#include <linux/bootmem.h>
#include <linux/slab.h>
#include <linux/memblock.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/reboot.h>
#include <linux/bcd.h>

#include <asm/setup.h>
#include <asm/efi.h>
#include <asm/time.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/x86_init.h>
#include <asm/uv/uv.h>

static struct efi efi_phys __initdata;
static efi_system_table_t efi_systab __initdata;

static efi_config_table_type_t arch_tables[] __initdata = {
#ifdef CONFIG_X86_UV
	{UV_SYSTEM_TABLE_GUID, "UVsystab", &efi.uv_systab},
#endif
	{NULL_GUID, NULL, NULL},
};

u64 efi_setup;		/* efi setup_data physical address */

static int add_efi_memmap __initdata;
static int __init setup_add_efi_memmap(char *arg)
{
	add_efi_memmap = 1;
	return 0;
}
early_param("add_efi_memmap", setup_add_efi_memmap);

static efi_status_t __init phys_efi_set_virtual_address_map(
	unsigned long memory_map_size,
	unsigned long descriptor_size,
	u32 descriptor_version,
	efi_memory_desc_t *virtual_map)
{
	efi_status_t status;
	unsigned long flags;
	pgd_t *save_pgd;

	save_pgd = efi_call_phys_prolog();

	/* Disable interrupts around EFI calls: */
	local_irq_save(flags);
	status = efi_call_phys(efi_phys.set_virtual_address_map,
			       memory_map_size, descriptor_size,
			       descriptor_version, virtual_map);
	local_irq_restore(flags);

	efi_call_phys_epilog(save_pgd);

	return status;
}

void __init efi_find_mirror(void)
{
	efi_memory_desc_t *md;
	u64 mirror_size = 0, total_size = 0;

	for_each_efi_memory_desc(md) {
		unsigned long long start = md->phys_addr;
		unsigned long long size = md->num_pages << EFI_PAGE_SHIFT;

		total_size += size;
		if (md->attribute & EFI_MEMORY_MORE_RELIABLE) {
			memblock_mark_mirror(start, size);
			mirror_size += size;
		}
	}
	if (mirror_size)
		pr_info("Memory: %lldM/%lldM mirrored memory\n",
			mirror_size>>20, total_size>>20);
}

/*
 * Tell the kernel about the EFI memory map.  This might include
 * more than the max 128 entries that can fit in the e820 legacy
 * (zeropage) memory map.
 */

static void __init do_add_efi_memmap(void)
{
	efi_memory_desc_t *md;

	for_each_efi_memory_desc(md) {
		unsigned long long start = md->phys_addr;
		unsigned long long size = md->num_pages << EFI_PAGE_SHIFT;
		int e820_type;

		switch (md->type) {
		case EFI_LOADER_CODE:
		case EFI_LOADER_DATA:
		case EFI_BOOT_SERVICES_CODE:
		case EFI_BOOT_SERVICES_DATA:
		case EFI_CONVENTIONAL_MEMORY:
			if (md->attribute & EFI_MEMORY_WB)
				e820_type = E820_RAM;
			else
				e820_type = E820_RESERVED;
			break;
		case EFI_ACPI_RECLAIM_MEMORY:
			e820_type = E820_ACPI;
			break;
		case EFI_ACPI_MEMORY_NVS:
			e820_type = E820_NVS;
			break;
		case EFI_UNUSABLE_MEMORY:
			e820_type = E820_UNUSABLE;
			break;
		case EFI_PERSISTENT_MEMORY:
			e820_type = E820_PMEM;
			break;
		default:
			/*
			 * EFI_RESERVED_TYPE EFI_RUNTIME_SERVICES_CODE
			 * EFI_RUNTIME_SERVICES_DATA EFI_MEMORY_MAPPED_IO
			 * EFI_MEMORY_MAPPED_IO_PORT_SPACE EFI_PAL_CODE
			 */
			e820_type = E820_RESERVED;
			break;
		}
		e820_add_region(start, size, e820_type);
	}
	sanitize_e820_map(e820.map, ARRAY_SIZE(e820.map), &e820.nr_map);
}

int __init efi_memblock_x86_reserve_range(void)
{
	struct efi_info *e = &boot_params.efi_info;
	struct efi_memory_map_data data;
	phys_addr_t pmap;
	int rv;

	if (efi_enabled(EFI_PARAVIRT))
		return 0;

#ifdef CONFIG_X86_32
	/* Can't handle data above 4GB at this time */
	if (e->efi_memmap_hi) {
		pr_err("Memory map is above 4GB, disabling EFI.\n");
		return -EINVAL;
	}
	pmap =  e->efi_memmap;
#else
	pmap = (e->efi_memmap |	((__u64)e->efi_memmap_hi << 32));
#endif
	data.phys_map		= pmap;
	data.size 		= e->efi_memmap_size;
	data.desc_size		= e->efi_memdesc_size;
	data.desc_version	= e->efi_memdesc_version;

	rv = efi_memmap_init_early(&data);
	if (rv)
		return rv;

	if (add_efi_memmap)
		do_add_efi_memmap();

	WARN(efi.memmap.desc_version != 1,
	     "Unexpected EFI_MEMORY_DESCRIPTOR version %ld",
	     efi.memmap.desc_version);

	memblock_reserve(pmap, efi.memmap.nr_map * efi.memmap.desc_size);

	return 0;
}

void __init efi_print_memmap(void)
{
	efi_memory_desc_t *md;
	int i = 0;

	for_each_efi_memory_desc(md) {
		char buf[64];

		pr_info("mem%02u: %s range=[0x%016llx-0x%016llx] (%lluMB)\n",
			i++, efi_md_typeattr_format(buf, sizeof(buf), md),
			md->phys_addr,
			md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT) - 1,
			(md->num_pages >> (20 - EFI_PAGE_SHIFT)));
	}
}

static int __init efi_systab_init(void *phys)
{
	if (efi_enabled(EFI_64BIT)) {
		efi_system_table_64_t *systab64;
		struct efi_setup_data *data = NULL;
		u64 tmp = 0;

		if (efi_setup) {
			data = early_memremap(efi_setup, sizeof(*data));
			if (!data)
				return -ENOMEM;
		}
		systab64 = early_memremap((unsigned long)phys,
					 sizeof(*systab64));
		if (systab64 == NULL) {
			pr_err("Couldn't map the system table!\n");
			if (data)
				early_memunmap(data, sizeof(*data));
			return -ENOMEM;
		}

		efi_systab.hdr = systab64->hdr;
		efi_systab.fw_vendor = data ? (unsigned long)data->fw_vendor :
					      systab64->fw_vendor;
		tmp |= data ? data->fw_vendor : systab64->fw_vendor;
		efi_systab.fw_revision = systab64->fw_revision;
		efi_systab.con_in_handle = systab64->con_in_handle;
		tmp |= systab64->con_in_handle;
		efi_systab.con_in = systab64->con_in;
		tmp |= systab64->con_in;
		efi_systab.con_out_handle = systab64->con_out_handle;
		tmp |= systab64->con_out_handle;
		efi_systab.con_out = systab64->con_out;
		tmp |= systab64->con_out;
		efi_systab.stderr_handle = systab64->stderr_handle;
		tmp |= systab64->stderr_handle;
		efi_systab.stderr = systab64->stderr;
		tmp |= systab64->stderr;
		efi_systab.runtime = data ?
				     (void *)(unsigned long)data->runtime :
				     (void *)(unsigned long)systab64->runtime;
		tmp |= data ? data->runtime : systab64->runtime;
		efi_systab.boottime = (void *)(unsigned long)systab64->boottime;
		tmp |= systab64->boottime;
		efi_systab.nr_tables = systab64->nr_tables;
		efi_systab.tables = data ? (unsigned long)data->tables :
					   systab64->tables;
		tmp |= data ? data->tables : systab64->tables;

		early_memunmap(systab64, sizeof(*systab64));
		if (data)
			early_memunmap(data, sizeof(*data));
#ifdef CONFIG_X86_32
		if (tmp >> 32) {
			pr_err("EFI data located above 4GB, disabling EFI.\n");
			return -EINVAL;
		}
#endif
	} else {
		efi_system_table_32_t *systab32;

		systab32 = early_memremap((unsigned long)phys,
					 sizeof(*systab32));
		if (systab32 == NULL) {
			pr_err("Couldn't map the system table!\n");
			return -ENOMEM;
		}

		efi_systab.hdr = systab32->hdr;
		efi_systab.fw_vendor = systab32->fw_vendor;
		efi_systab.fw_revision = systab32->fw_revision;
		efi_systab.con_in_handle = systab32->con_in_handle;
		efi_systab.con_in = systab32->con_in;
		efi_systab.con_out_handle = systab32->con_out_handle;
		efi_systab.con_out = systab32->con_out;
		efi_systab.stderr_handle = systab32->stderr_handle;
		efi_systab.stderr = systab32->stderr;
		efi_systab.runtime = (void *)(unsigned long)systab32->runtime;
		efi_systab.boottime = (void *)(unsigned long)systab32->boottime;
		efi_systab.nr_tables = systab32->nr_tables;
		efi_systab.tables = systab32->tables;

		early_memunmap(systab32, sizeof(*systab32));
	}

	efi.systab = &efi_systab;

	/*
	 * Verify the EFI Table
	 */
	if (efi.systab->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE) {
		pr_err("System table signature incorrect!\n");
		return -EINVAL;
	}
	if ((efi.systab->hdr.revision >> 16) == 0)
		pr_err("Warning: System table version %d.%02d, expected 1.00 or greater!\n",
		       efi.systab->hdr.revision >> 16,
		       efi.systab->hdr.revision & 0xffff);

	return 0;
}

static int __init efi_runtime_init32(void)
{
	efi_runtime_services_32_t *runtime;

	runtime = early_memremap((unsigned long)efi.systab->runtime,
			sizeof(efi_runtime_services_32_t));
	if (!runtime) {
		pr_err("Could not map the runtime service table!\n");
		return -ENOMEM;
	}

	/*
	 * We will only need *early* access to the SetVirtualAddressMap
	 * EFI runtime service. All other runtime services will be called
	 * via the virtual mapping.
	 */
	efi_phys.set_virtual_address_map =
			(efi_set_virtual_address_map_t *)
			(unsigned long)runtime->set_virtual_address_map;
	early_memunmap(runtime, sizeof(efi_runtime_services_32_t));

	return 0;
}

static int __init efi_runtime_init64(void)
{
	efi_runtime_services_64_t *runtime;

	runtime = early_memremap((unsigned long)efi.systab->runtime,
			sizeof(efi_runtime_services_64_t));
	if (!runtime) {
		pr_err("Could not map the runtime service table!\n");
		return -ENOMEM;
	}

	/*
	 * We will only need *early* access to the SetVirtualAddressMap
	 * EFI runtime service. All other runtime services will be called
	 * via the virtual mapping.
	 */
	efi_phys.set_virtual_address_map =
			(efi_set_virtual_address_map_t *)
			(unsigned long)runtime->set_virtual_address_map;
	early_memunmap(runtime, sizeof(efi_runtime_services_64_t));

	return 0;
}

static int __init efi_runtime_init(void)
{
	int rv;

	/*
	 * Check out the runtime services table. We need to map
	 * the runtime services table so that we can grab the physical
	 * address of several of the EFI runtime functions, needed to
	 * set the firmware into virtual mode.
	 *
	 * When EFI_PARAVIRT is in force then we could not map runtime
	 * service memory region because we do not have direct access to it.
	 * However, runtime services are available through proxy functions
	 * (e.g. in case of Xen dom0 EFI implementation they call special
	 * hypercall which executes relevant EFI functions) and that is why
	 * they are always enabled.
	 */

	if (!efi_enabled(EFI_PARAVIRT)) {
		if (efi_enabled(EFI_64BIT))
			rv = efi_runtime_init64();
		else
			rv = efi_runtime_init32();

		if (rv)
			return rv;
	}

	set_bit(EFI_RUNTIME_SERVICES, &efi.flags);

	return 0;
}

void __init efi_init(void)
{
	efi_char16_t *c16;
	char vendor[100] = "unknown";
	int i = 0;
	void *tmp;

#ifdef CONFIG_X86_32
	if (boot_params.efi_info.efi_systab_hi ||
	    boot_params.efi_info.efi_memmap_hi) {
		pr_info("Table located above 4GB, disabling EFI.\n");
		return;
	}
	efi_phys.systab = (efi_system_table_t *)boot_params.efi_info.efi_systab;
#else
	efi_phys.systab = (efi_system_table_t *)
			  (boot_params.efi_info.efi_systab |
			  ((__u64)boot_params.efi_info.efi_systab_hi<<32));
#endif

	if (efi_systab_init(efi_phys.systab))
		return;

	efi.config_table = (unsigned long)efi.systab->tables;
	efi.fw_vendor	 = (unsigned long)efi.systab->fw_vendor;
	efi.runtime	 = (unsigned long)efi.systab->runtime;

	/*
	 * Show what we know for posterity
	 */
	c16 = tmp = early_memremap(efi.systab->fw_vendor, 2);
	if (c16) {
		for (i = 0; i < sizeof(vendor) - 1 && *c16; ++i)
			vendor[i] = *c16++;
		vendor[i] = '\0';
	} else
		pr_err("Could not map the firmware vendor!\n");
	early_memunmap(tmp, 2);

	pr_info("EFI v%u.%.02u by %s\n",
		efi.systab->hdr.revision >> 16,
		efi.systab->hdr.revision & 0xffff, vendor);

	if (efi_reuse_config(efi.systab->tables, efi.systab->nr_tables))
		return;

	if (efi_config_init(arch_tables))
		return;

	/*
	 * Note: We currently don't support runtime services on an EFI
	 * that doesn't match the kernel 32/64-bit mode.
	 */

	if (!efi_runtime_supported())
		pr_info("No EFI runtime due to 32/64-bit mismatch with kernel\n");
	else {
		if (efi_runtime_disabled() || efi_runtime_init()) {
			efi_memmap_unmap();
			return;
		}
	}

	if (efi_enabled(EFI_DBG))
		efi_print_memmap();
}

void __init efi_late_init(void)
{
	efi_bgrt_init();
}

void __init efi_set_executable(efi_memory_desc_t *md, bool executable)
{
	u64 addr, npages;

	addr = md->virt_addr;
	npages = md->num_pages;

	memrange_efi_to_native(&addr, &npages);

	if (executable)
		set_memory_x(addr, npages);
	else
		set_memory_nx(addr, npages);
}

void __init runtime_code_page_mkexec(void)
{
	efi_memory_desc_t *md;

	/* Make EFI runtime service code area executable */
	for_each_efi_memory_desc(md) {
		if (md->type != EFI_RUNTIME_SERVICES_CODE)
			continue;

		efi_set_executable(md, true);
	}
}

void __init efi_memory_uc(u64 addr, unsigned long size)
{
	unsigned long page_shift = 1UL << EFI_PAGE_SHIFT;
	u64 npages;

	npages = round_up(size, page_shift) / page_shift;
	memrange_efi_to_native(&addr, &npages);
	set_memory_uc(addr, npages);
}

void __init old_map_region(efi_memory_desc_t *md)
{
	u64 start_pfn, end_pfn, end;
	unsigned long size;
	void *va;

	start_pfn = PFN_DOWN(md->phys_addr);
	size	  = md->num_pages << PAGE_SHIFT;
	end	  = md->phys_addr + size;
	end_pfn   = PFN_UP(end);

	if (pfn_range_is_mapped(start_pfn, end_pfn)) {
		va = __va(md->phys_addr);

		if (!(md->attribute & EFI_MEMORY_WB))
			efi_memory_uc((u64)(unsigned long)va, size);
	} else
		va = efi_ioremap(md->phys_addr, size,
				 md->type, md->attribute);

	md->virt_addr = (u64) (unsigned long) va;
	if (!va)
		pr_err("ioremap of 0x%llX failed!\n",
		       (unsigned long long)md->phys_addr);
}

/* Merge contiguous regions of the same type and attribute */
static void __init efi_merge_regions(void)
{
	efi_memory_desc_t *md, *prev_md = NULL;

	for_each_efi_memory_desc(md) {
		u64 prev_size;

		if (!prev_md) {
			prev_md = md;
			continue;
		}

		if (prev_md->type != md->type ||
		    prev_md->attribute != md->attribute) {
			prev_md = md;
			continue;
		}

		prev_size = prev_md->num_pages << EFI_PAGE_SHIFT;

		if (md->phys_addr == (prev_md->phys_addr + prev_size)) {
			prev_md->num_pages += md->num_pages;
			md->type = EFI_RESERVED_TYPE;
			md->attribute = 0;
			continue;
		}
		prev_md = md;
	}
}

static void __init get_systab_virt_addr(efi_memory_desc_t *md)
{
	unsigned long size;
	u64 end, systab;

	size = md->num_pages << EFI_PAGE_SHIFT;
	end = md->phys_addr + size;
	systab = (u64)(unsigned long)efi_phys.systab;
	if (md->phys_addr <= systab && systab < end) {
		systab += md->virt_addr - md->phys_addr;
		efi.systab = (efi_system_table_t *)(unsigned long)systab;
	}
}

static void *realloc_pages(void *old_memmap, int old_shift)
{
	void *ret;

	ret = (void *)__get_free_pages(GFP_KERNEL, old_shift + 1);
	if (!ret)
		goto out;

	/*
	 * A first-time allocation doesn't have anything to copy.
	 */
	if (!old_memmap)
		return ret;

	memcpy(ret, old_memmap, PAGE_SIZE << old_shift);

out:
	free_pages((unsigned long)old_memmap, old_shift);
	return ret;
}

/*
 * Iterate the EFI memory map in reverse order because the regions
 * will be mapped top-down. The end result is the same as if we had
 * mapped things forward, but doesn't require us to change the
 * existing implementation of efi_map_region().
 */
static inline void *efi_map_next_entry_reverse(void *entry)
{
	/* Initial call */
	if (!entry)
		return efi.memmap.map_end - efi.memmap.desc_size;

	entry -= efi.memmap.desc_size;
	if (entry < efi.memmap.map)
		return NULL;

	return entry;
}

/*
 * efi_map_next_entry - Return the next EFI memory map descriptor
 * @entry: Previous EFI memory map descriptor
 *
 * This is a helper function to iterate over the EFI memory map, which
 * we do in different orders depending on the current configuration.
 *
 * To begin traversing the memory map @entry must be %NULL.
 *
 * Returns %NULL when we reach the end of the memory map.
 */
static void *efi_map_next_entry(void *entry)
{
	if (!efi_enabled(EFI_OLD_MEMMAP) && efi_enabled(EFI_64BIT)) {
		/*
		 * Starting in UEFI v2.5 the EFI_PROPERTIES_TABLE
		 * config table feature requires us to map all entries
		 * in the same order as they appear in the EFI memory
		 * map. That is to say, entry N must have a lower
		 * virtual address than entry N+1. This is because the
		 * firmware toolchain leaves relative references in
		 * the code/data sections, which are split and become
		 * separate EFI memory regions. Mapping things
		 * out-of-order leads to the firmware accessing
		 * unmapped addresses.
		 *
		 * Since we need to map things this way whether or not
		 * the kernel actually makes use of
		 * EFI_PROPERTIES_TABLE, let's just switch to this
		 * scheme by default for 64-bit.
		 */
		return efi_map_next_entry_reverse(entry);
	}

	/* Initial call */
	if (!entry)
		return efi.memmap.map;

	entry += efi.memmap.desc_size;
	if (entry >= efi.memmap.map_end)
		return NULL;

	return entry;
}

static bool should_map_region(efi_memory_desc_t *md)
{
	/*
	 * Runtime regions always require runtime mappings (obviously).
	 */
	if (md->attribute & EFI_MEMORY_RUNTIME)
		return true;

	/*
	 * 32-bit EFI doesn't suffer from the bug that requires us to
	 * reserve boot services regions, and mixed mode support
	 * doesn't exist for 32-bit kernels.
	 */
	if (IS_ENABLED(CONFIG_X86_32))
		return false;

	/*
	 * Map all of RAM so that we can access arguments in the 1:1
	 * mapping when making EFI runtime calls.
	 */
	if (IS_ENABLED(CONFIG_EFI_MIXED) && !efi_is_native()) {
		if (md->type == EFI_CONVENTIONAL_MEMORY ||
		    md->type == EFI_LOADER_DATA ||
		    md->type == EFI_LOADER_CODE)
			return true;
	}

	/*
	 * Map boot services regions as a workaround for buggy
	 * firmware that accesses them even when they shouldn't.
	 *
	 * See efi_{reserve,free}_boot_services().
	 */
	if (md->type == EFI_BOOT_SERVICES_CODE ||
	    md->type == EFI_BOOT_SERVICES_DATA)
		return true;

	return false;
}

/*
 * Map the efi memory ranges of the runtime services and update new_mmap with
 * virtual addresses.
 */
static void * __init efi_map_regions(int *count, int *pg_shift)
{
	void *p, *new_memmap = NULL;
	unsigned long left = 0;
	unsigned long desc_size;
	efi_memory_desc_t *md;

	desc_size = efi.memmap.desc_size;

	p = NULL;
	while ((p = efi_map_next_entry(p))) {
		md = p;

		if (!should_map_region(md))
			continue;

		efi_map_region(md);
		get_systab_virt_addr(md);

		if (left < desc_size) {
			new_memmap = realloc_pages(new_memmap, *pg_shift);
			if (!new_memmap)
				return NULL;

			left += PAGE_SIZE << *pg_shift;
			(*pg_shift)++;
		}

		memcpy(new_memmap + (*count * desc_size), md, desc_size);

		left -= desc_size;
		(*count)++;
	}

	return new_memmap;
}

static void __init kexec_enter_virtual_mode(void)
{
#ifdef CONFIG_KEXEC_CORE
	efi_memory_desc_t *md;
	unsigned int num_pages;

	efi.systab = NULL;

	/*
	 * We don't do virtual mode, since we don't do runtime services, on
	 * non-native EFI
	 */
	if (!efi_is_native()) {
		efi_memmap_unmap();
		clear_bit(EFI_RUNTIME_SERVICES, &efi.flags);
		return;
	}

	if (efi_alloc_page_tables()) {
		pr_err("Failed to allocate EFI page tables\n");
		clear_bit(EFI_RUNTIME_SERVICES, &efi.flags);
		return;
	}

	/*
	* Map efi regions which were passed via setup_data. The virt_addr is a
	* fixed addr which was used in first kernel of a kexec boot.
	*/
	for_each_efi_memory_desc(md) {
		efi_map_region_fixed(md); /* FIXME: add error handling */
		get_systab_virt_addr(md);
	}

	/*
	 * Unregister the early EFI memmap from efi_init() and install
	 * the new EFI memory map.
	 */
	efi_memmap_unmap();

	if (efi_memmap_init_late(efi.memmap.phys_map,
				 efi.memmap.desc_size * efi.memmap.nr_map)) {
		pr_err("Failed to remap late EFI memory map\n");
		clear_bit(EFI_RUNTIME_SERVICES, &efi.flags);
		return;
	}

	BUG_ON(!efi.systab);

	num_pages = ALIGN(efi.memmap.nr_map * efi.memmap.desc_size, PAGE_SIZE);
	num_pages >>= PAGE_SHIFT;

	if (efi_setup_page_tables(efi.memmap.phys_map, num_pages)) {
		clear_bit(EFI_RUNTIME_SERVICES, &efi.flags);
		return;
	}

	efi_sync_low_kernel_mappings();

	/*
	 * Now that EFI is in virtual mode, update the function
	 * pointers in the runtime service table to the new virtual addresses.
	 *
	 * Call EFI services through wrapper functions.
	 */
	efi.runtime_version = efi_systab.hdr.revision;

	efi_native_runtime_setup();

	efi.set_virtual_address_map = NULL;

	if (efi_enabled(EFI_OLD_MEMMAP) && (__supported_pte_mask & _PAGE_NX))
		runtime_code_page_mkexec();

	/* clean DUMMY object */
	efi_delete_dummy_variable();
#endif
}

/*
 * This function will switch the EFI runtime services to virtual mode.
 * Essentially, we look through the EFI memmap and map every region that
 * has the runtime attribute bit set in its memory descriptor into the
 * efi_pgd page table.
 *
 * The old method which used to update that memory descriptor with the
 * virtual address obtained from ioremap() is still supported when the
 * kernel is booted with efi=old_map on its command line. Same old
 * method enabled the runtime services to be called without having to
 * thunk back into physical mode for every invocation.
 *
 * The new method does a pagetable switch in a preemption-safe manner
 * so that we're in a different address space when calling a runtime
 * function. For function arguments passing we do copy the PUDs of the
 * kernel page table into efi_pgd prior to each call.
 *
 * Specially for kexec boot, efi runtime maps in previous kernel should
 * be passed in via setup_data. In that case runtime ranges will be mapped
 * to the same virtual addresses as the first kernel, see
 * kexec_enter_virtual_mode().
 */
static void __init __efi_enter_virtual_mode(void)
{
	int count = 0, pg_shift = 0;
	void *new_memmap = NULL;
	efi_status_t status;
	phys_addr_t pa;

	efi.systab = NULL;

	if (efi_alloc_page_tables()) {
		pr_err("Failed to allocate EFI page tables\n");
		clear_bit(EFI_RUNTIME_SERVICES, &efi.flags);
		return;
	}

	efi_merge_regions();
	new_memmap = efi_map_regions(&count, &pg_shift);
	if (!new_memmap) {
		pr_err("Error reallocating memory, EFI runtime non-functional!\n");
		clear_bit(EFI_RUNTIME_SERVICES, &efi.flags);
		return;
	}

	pa = __pa(new_memmap);

	/*
	 * Unregister the early EFI memmap from efi_init() and install
	 * the new EFI memory map that we are about to pass to the
	 * firmware via SetVirtualAddressMap().
	 */
	efi_memmap_unmap();

	if (efi_memmap_init_late(pa, efi.memmap.desc_size * count)) {
		pr_err("Failed to remap late EFI memory map\n");
		clear_bit(EFI_RUNTIME_SERVICES, &efi.flags);
		return;
	}

	BUG_ON(!efi.systab);

	if (efi_setup_page_tables(pa, 1 << pg_shift)) {
		clear_bit(EFI_RUNTIME_SERVICES, &efi.flags);
		return;
	}

	efi_sync_low_kernel_mappings();

	if (efi_is_native()) {
		status = phys_efi_set_virtual_address_map(
				efi.memmap.desc_size * count,
				efi.memmap.desc_size,
				efi.memmap.desc_version,
				(efi_memory_desc_t *)pa);
	} else {
		status = efi_thunk_set_virtual_address_map(
				efi_phys.set_virtual_address_map,
				efi.memmap.desc_size * count,
				efi.memmap.desc_size,
				efi.memmap.desc_version,
				(efi_memory_desc_t *)pa);
	}

	if (status != EFI_SUCCESS) {
		pr_alert("Unable to switch EFI into virtual mode (status=%lx)!\n",
			 status);
		panic("EFI call to SetVirtualAddressMap() failed!");
	}

	/*
	 * Now that EFI is in virtual mode, update the function
	 * pointers in the runtime service table to the new virtual addresses.
	 *
	 * Call EFI services through wrapper functions.
	 */
	efi.runtime_version = efi_systab.hdr.revision;

	if (efi_is_native())
		efi_native_runtime_setup();
	else
		efi_thunk_runtime_setup();

	efi.set_virtual_address_map = NULL;

	/*
	 * Apply more restrictive page table mapping attributes now that
	 * SVAM() has been called and the firmware has performed all
	 * necessary relocation fixups for the new virtual addresses.
	 */
	efi_runtime_update_mappings();
	efi_dump_pagetable();

	/* clean DUMMY object */
	efi_delete_dummy_variable();
}

void __init efi_enter_virtual_mode(void)
{
	if (efi_enabled(EFI_PARAVIRT))
		return;

	if (efi_setup)
		kexec_enter_virtual_mode();
	else
		__efi_enter_virtual_mode();
}

/*
 * Convenience functions to obtain memory types and attributes
 */
u32 efi_mem_type(unsigned long phys_addr)
{
	efi_memory_desc_t *md;

	if (!efi_enabled(EFI_MEMMAP))
		return 0;

	for_each_efi_memory_desc(md) {
		if ((md->phys_addr <= phys_addr) &&
		    (phys_addr < (md->phys_addr +
				  (md->num_pages << EFI_PAGE_SHIFT))))
			return md->type;
	}
	return 0;
}

static int __init arch_parse_efi_cmdline(char *str)
{
	if (!str) {
		pr_warn("need at least one option\n");
		return -EINVAL;
	}

	if (parse_option_str(str, "old_map"))
		set_bit(EFI_OLD_MEMMAP, &efi.flags);

	return 0;
}
early_param("efi", arch_parse_efi_cmdline);
