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
#include <asm/rtc.h>
#include <asm/uv/uv.h>

#define EFI_DEBUG

struct efi_memory_map memmap;

static struct efi efi_phys __initdata;
static efi_system_table_t efi_systab __initdata;

static efi_config_table_type_t arch_tables[] __initdata = {
#ifdef CONFIG_X86_UV
	{UV_SYSTEM_TABLE_GUID, "UVsystab", &efi.uv_systab},
#endif
	{NULL_GUID, NULL, NULL},
};

u64 efi_setup;		/* efi setup_data physical address */

static bool disable_runtime __initdata = false;
static int __init setup_noefi(char *arg)
{
	disable_runtime = true;
	return 0;
}
early_param("noefi", setup_noefi);

int add_efi_memmap;
EXPORT_SYMBOL(add_efi_memmap);

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

	efi_call_phys_prelog();
	status = efi_call_phys(efi_phys.set_virtual_address_map,
			       memory_map_size, descriptor_size,
			       descriptor_version, virtual_map);
	efi_call_phys_epilog();
	return status;
}

void efi_get_time(struct timespec *now)
{
	efi_status_t status;
	efi_time_t eft;
	efi_time_cap_t cap;

	status = efi.get_time(&eft, &cap);
	if (status != EFI_SUCCESS)
		pr_err("Oops: efitime: can't read time!\n");

	now->tv_sec = mktime(eft.year, eft.month, eft.day, eft.hour,
			     eft.minute, eft.second);
	now->tv_nsec = 0;
}

/*
 * Tell the kernel about the EFI memory map.  This might include
 * more than the max 128 entries that can fit in the e820 legacy
 * (zeropage) memory map.
 */

static void __init do_add_efi_memmap(void)
{
	void *p;

	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		efi_memory_desc_t *md = p;
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
	unsigned long pmap;

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
	memmap.phys_map		= (void *)pmap;
	memmap.nr_map		= e->efi_memmap_size /
				  e->efi_memdesc_size;
	memmap.desc_size	= e->efi_memdesc_size;
	memmap.desc_version	= e->efi_memdesc_version;

	memblock_reserve(pmap, memmap.nr_map * memmap.desc_size);

	efi.memmap = &memmap;

	return 0;
}

static void __init print_efi_memmap(void)
{
#ifdef EFI_DEBUG
	efi_memory_desc_t *md;
	void *p;
	int i;

	for (p = memmap.map, i = 0;
	     p < memmap.map_end;
	     p += memmap.desc_size, i++) {
		md = p;
		pr_info("mem%02u: type=%u, attr=0x%llx, range=[0x%016llx-0x%016llx) (%lluMB)\n",
			i, md->type, md->attribute, md->phys_addr,
			md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT),
			(md->num_pages >> (20 - EFI_PAGE_SHIFT)));
	}
#endif  /*  EFI_DEBUG  */
}

void __init efi_unmap_memmap(void)
{
	clear_bit(EFI_MEMMAP, &efi.flags);
	if (memmap.map) {
		early_memunmap(memmap.map, memmap.nr_map * memmap.desc_size);
		memmap.map = NULL;
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

	set_bit(EFI_SYSTEM_TABLES, &efi.flags);

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
	 * We will only need *early* access to the following two
	 * EFI runtime services before set_virtual_address_map
	 * is invoked.
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
	 * We will only need *early* access to the following two
	 * EFI runtime services before set_virtual_address_map
	 * is invoked.
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

static int __init efi_memmap_init(void)
{
	if (efi_enabled(EFI_PARAVIRT))
		return 0;

	/* Map the EFI memory map */
	memmap.map = early_memremap((unsigned long)memmap.phys_map,
				   memmap.nr_map * memmap.desc_size);
	if (memmap.map == NULL) {
		pr_err("Could not map the memory map!\n");
		return -ENOMEM;
	}
	memmap.map_end = memmap.map + (memmap.nr_map * memmap.desc_size);

	if (add_efi_memmap)
		do_add_efi_memmap();

	set_bit(EFI_MEMMAP, &efi.flags);

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
		if (disable_runtime || efi_runtime_init())
			return;
	}
	if (efi_memmap_init())
		return;

	print_efi_memmap();
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
	void *p;

	/* Make EFI runtime service code area executable */
	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;

		if (md->type != EFI_RUNTIME_SERVICES_CODE)
			continue;

		efi_set_executable(md, true);
	}
}

void efi_memory_uc(u64 addr, unsigned long size)
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
	void *p;
	efi_memory_desc_t *md, *prev_md = NULL;

	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		u64 prev_size;
		md = p;

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

static void __init save_runtime_map(void)
{
#ifdef CONFIG_KEXEC
	efi_memory_desc_t *md;
	void *tmp, *p, *q = NULL;
	int count = 0;

	if (efi_enabled(EFI_OLD_MEMMAP))
		return;

	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;

		if (!(md->attribute & EFI_MEMORY_RUNTIME) ||
		    (md->type == EFI_BOOT_SERVICES_CODE) ||
		    (md->type == EFI_BOOT_SERVICES_DATA))
			continue;
		tmp = krealloc(q, (count + 1) * memmap.desc_size, GFP_KERNEL);
		if (!tmp)
			goto out;
		q = tmp;

		memcpy(q + count * memmap.desc_size, md, memmap.desc_size);
		count++;
	}

	efi_runtime_map_setup(q, count, memmap.desc_size);
	return;

out:
	kfree(q);
	pr_err("Error saving runtime map, efi runtime on kexec non-functional!!\n");
#endif
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
 * Map the efi memory ranges of the runtime services and update new_mmap with
 * virtual addresses.
 */
static void * __init efi_map_regions(int *count, int *pg_shift)
{
	void *p, *new_memmap = NULL;
	unsigned long left = 0;
	efi_memory_desc_t *md;

	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;
		if (!(md->attribute & EFI_MEMORY_RUNTIME)) {
#ifdef CONFIG_X86_64
			if (md->type != EFI_BOOT_SERVICES_CODE &&
			    md->type != EFI_BOOT_SERVICES_DATA)
#endif
				continue;
		}

		efi_map_region(md);
		get_systab_virt_addr(md);

		if (left < memmap.desc_size) {
			new_memmap = realloc_pages(new_memmap, *pg_shift);
			if (!new_memmap)
				return NULL;

			left += PAGE_SIZE << *pg_shift;
			(*pg_shift)++;
		}

		memcpy(new_memmap + (*count * memmap.desc_size), md,
		       memmap.desc_size);

		left -= memmap.desc_size;
		(*count)++;
	}

	return new_memmap;
}

static void __init kexec_enter_virtual_mode(void)
{
#ifdef CONFIG_KEXEC
	efi_memory_desc_t *md;
	void *p;

	efi.systab = NULL;

	/*
	 * We don't do virtual mode, since we don't do runtime services, on
	 * non-native EFI
	 */
	if (!efi_is_native()) {
		efi_unmap_memmap();
		return;
	}

	/*
	* Map efi regions which were passed via setup_data. The virt_addr is a
	* fixed addr which was used in first kernel of a kexec boot.
	*/
	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;
		efi_map_region_fixed(md); /* FIXME: add error handling */
		get_systab_virt_addr(md);
	}

	save_runtime_map();

	BUG_ON(!efi.systab);

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
 * ->trampoline_pgd page table using a top-down VA allocation scheme.
 *
 * The old method which used to update that memory descriptor with the
 * virtual address obtained from ioremap() is still supported when the
 * kernel is booted with efi=old_map on its command line. Same old
 * method enabled the runtime services to be called without having to
 * thunk back into physical mode for every invocation.
 *
 * The new method does a pagetable switch in a preemption-safe manner
 * so that we're in a different address space when calling a runtime
 * function. For function arguments passing we do copy the PGDs of the
 * kernel page table into ->trampoline_pgd prior to each call.
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

	efi.systab = NULL;

	efi_merge_regions();
	new_memmap = efi_map_regions(&count, &pg_shift);
	if (!new_memmap) {
		pr_err("Error reallocating memory, EFI runtime non-functional!\n");
		return;
	}

	save_runtime_map();

	BUG_ON(!efi.systab);

	if (efi_setup_page_tables(__pa(new_memmap), 1 << pg_shift))
		return;

	efi_sync_low_kernel_mappings();
	efi_dump_pagetable();

	if (efi_is_native()) {
		status = phys_efi_set_virtual_address_map(
				memmap.desc_size * count,
				memmap.desc_size,
				memmap.desc_version,
				(efi_memory_desc_t *)__pa(new_memmap));
	} else {
		status = efi_thunk_set_virtual_address_map(
				efi_phys.set_virtual_address_map,
				memmap.desc_size * count,
				memmap.desc_size,
				memmap.desc_version,
				(efi_memory_desc_t *)__pa(new_memmap));
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

	efi_runtime_mkexec();

	/*
	 * We mapped the descriptor array into the EFI pagetable above but we're
	 * not unmapping it here. Here's why:
	 *
	 * We're copying select PGDs from the kernel page table to the EFI page
	 * table and when we do so and make changes to those PGDs like unmapping
	 * stuff from them, those changes appear in the kernel page table and we
	 * go boom.
	 *
	 * From setup_real_mode():
	 *
	 * ...
	 * trampoline_pgd[0] = init_level4_pgt[pgd_index(__PAGE_OFFSET)].pgd;
	 *
	 * In this particular case, our allocation is in PGD 0 of the EFI page
	 * table but we've copied that PGD from PGD[272] of the EFI page table:
	 *
	 *	pgd_index(__PAGE_OFFSET = 0xffff880000000000) = 272
	 *
	 * where the direct memory mapping in kernel space is.
	 *
	 * new_memmap's VA comes from that direct mapping and thus clearing it,
	 * it would get cleared in the kernel page table too.
	 *
	 * efi_cleanup_page_tables(__pa(new_memmap), 1 << pg_shift);
	 */
	free_pages((unsigned long)new_memmap, pg_shift);

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
	void *p;

	if (!efi_enabled(EFI_MEMMAP))
		return 0;

	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;
		if ((md->phys_addr <= phys_addr) &&
		    (phys_addr < (md->phys_addr +
				  (md->num_pages << EFI_PAGE_SHIFT))))
			return md->type;
	}
	return 0;
}

u64 efi_mem_attributes(unsigned long phys_addr)
{
	efi_memory_desc_t *md;
	void *p;

	if (!efi_enabled(EFI_MEMMAP))
		return 0;

	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;
		if ((md->phys_addr <= phys_addr) &&
		    (phys_addr < (md->phys_addr +
				  (md->num_pages << EFI_PAGE_SHIFT))))
			return md->attribute;
	}
	return 0;
}

static int __init parse_efi_cmdline(char *str)
{
	if (*str == '=')
		str++;

	while (*str) {
		if (!strncmp(str, "old_map", 7)) {
			set_bit(EFI_OLD_MEMMAP, &efi.flags);
			str += strlen("old_map");
		}

		/*
		 * Skip any options we don't understand. Presumably
		 * they apply to the EFI boot stub.
		 */
		while (*str && *str != ',')
			str++;

		/* If we hit a delimiter, skip it */
		if (*str == ',')
			str++;
	}

	return 0;
}
early_param("efi", parse_efi_cmdline);
