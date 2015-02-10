/*
 * x86_64 specific EFI support functions
 * Based on Extensible Firmware Interface Specification version 1.0
 *
 * Copyright (C) 2005-2008 Intel Co.
 *	Fenghua Yu <fenghua.yu@intel.com>
 *	Bibo Mao <bibo.mao@intel.com>
 *	Chandramouli Narayanan <mouli@linux.intel.com>
 *	Huang Ying <ying.huang@intel.com>
 *
 * Code to convert EFI to E820 map has been implemented in elilo bootloader
 * based on a EFI patch by Edgar Hucek. Based on the E820 map, the page table
 * is setup appropriately for EFI runtime code.
 * - mouli 06/14/2007.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/bootmem.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/reboot.h>
#include <linux/slab.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/e820.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/proto.h>
#include <asm/efi.h>
#include <asm/cacheflush.h>
#include <asm/fixmap.h>
#include <asm/realmode.h>
#include <asm/time.h>

static pgd_t *save_pgd __initdata;
static unsigned long efi_flags __initdata;

/*
 * We allocate runtime services regions bottom-up, starting from -4G, i.e.
 * 0xffff_ffff_0000_0000 and limit EFI VA mapping space to 64G.
 */
static u64 efi_va = EFI_VA_START;

/*
 * Scratch space used for switching the pagetable in the EFI stub
 */
struct efi_scratch {
	u64 r15;
	u64 prev_cr3;
	pgd_t *efi_pgt;
	bool use_pgd;
	u64 phys_stack;
} __packed;

static void __init early_code_mapping_set_exec(int executable)
{
	efi_memory_desc_t *md;
	void *p;

	if (!(__supported_pte_mask & _PAGE_NX))
		return;

	/* Make EFI service code area executable */
	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;
		if (md->type == EFI_RUNTIME_SERVICES_CODE ||
		    md->type == EFI_BOOT_SERVICES_CODE)
			efi_set_executable(md, executable);
	}
}

void __init efi_call_phys_prolog(void)
{
	unsigned long vaddress;
	int pgd;
	int n_pgds;

	if (!efi_enabled(EFI_OLD_MEMMAP))
		return;

	early_code_mapping_set_exec(1);
	local_irq_save(efi_flags);

	n_pgds = DIV_ROUND_UP((max_pfn << PAGE_SHIFT), PGDIR_SIZE);
	save_pgd = kmalloc(n_pgds * sizeof(pgd_t), GFP_KERNEL);

	for (pgd = 0; pgd < n_pgds; pgd++) {
		save_pgd[pgd] = *pgd_offset_k(pgd * PGDIR_SIZE);
		vaddress = (unsigned long)__va(pgd * PGDIR_SIZE);
		set_pgd(pgd_offset_k(pgd * PGDIR_SIZE), *pgd_offset_k(vaddress));
	}
	__flush_tlb_all();
}

void __init efi_call_phys_epilog(void)
{
	/*
	 * After the lock is released, the original page table is restored.
	 */
	int pgd;
	int n_pgds = DIV_ROUND_UP((max_pfn << PAGE_SHIFT) , PGDIR_SIZE);

	if (!efi_enabled(EFI_OLD_MEMMAP))
		return;

	for (pgd = 0; pgd < n_pgds; pgd++)
		set_pgd(pgd_offset_k(pgd * PGDIR_SIZE), save_pgd[pgd]);
	kfree(save_pgd);
	__flush_tlb_all();
	local_irq_restore(efi_flags);
	early_code_mapping_set_exec(0);
}

/*
 * Add low kernel mappings for passing arguments to EFI functions.
 */
void efi_sync_low_kernel_mappings(void)
{
	unsigned num_pgds;
	pgd_t *pgd = (pgd_t *)__va(real_mode_header->trampoline_pgd);

	if (efi_enabled(EFI_OLD_MEMMAP))
		return;

	num_pgds = pgd_index(MODULES_END - 1) - pgd_index(PAGE_OFFSET);

	memcpy(pgd + pgd_index(PAGE_OFFSET),
		init_mm.pgd + pgd_index(PAGE_OFFSET),
		sizeof(pgd_t) * num_pgds);
}

int __init efi_setup_page_tables(unsigned long pa_memmap, unsigned num_pages)
{
	unsigned long text;
	struct page *page;
	unsigned npages;
	pgd_t *pgd;

	if (efi_enabled(EFI_OLD_MEMMAP))
		return 0;

	efi_scratch.efi_pgt = (pgd_t *)(unsigned long)real_mode_header->trampoline_pgd;
	pgd = __va(efi_scratch.efi_pgt);

	/*
	 * It can happen that the physical address of new_memmap lands in memory
	 * which is not mapped in the EFI page table. Therefore we need to go
	 * and ident-map those pages containing the map before calling
	 * phys_efi_set_virtual_address_map().
	 */
	if (kernel_map_pages_in_pgd(pgd, pa_memmap, pa_memmap, num_pages, _PAGE_NX)) {
		pr_err("Error ident-mapping new memmap (0x%lx)!\n", pa_memmap);
		return 1;
	}

	efi_scratch.use_pgd = true;

	/*
	 * When making calls to the firmware everything needs to be 1:1
	 * mapped and addressable with 32-bit pointers. Map the kernel
	 * text and allocate a new stack because we can't rely on the
	 * stack pointer being < 4GB.
	 */
	if (!IS_ENABLED(CONFIG_EFI_MIXED))
		return 0;

	page = alloc_page(GFP_KERNEL|__GFP_DMA32);
	if (!page)
		panic("Unable to allocate EFI runtime stack < 4GB\n");

	efi_scratch.phys_stack = virt_to_phys(page_address(page));
	efi_scratch.phys_stack += PAGE_SIZE; /* stack grows down */

	npages = (_end - _text) >> PAGE_SHIFT;
	text = __pa(_text);

	if (kernel_map_pages_in_pgd(pgd, text >> PAGE_SHIFT, text, npages, 0)) {
		pr_err("Failed to map kernel text 1:1\n");
		return 1;
	}

	return 0;
}

void __init efi_cleanup_page_tables(unsigned long pa_memmap, unsigned num_pages)
{
	pgd_t *pgd = (pgd_t *)__va(real_mode_header->trampoline_pgd);

	kernel_unmap_pages_in_pgd(pgd, pa_memmap, num_pages);
}

static void __init __map_region(efi_memory_desc_t *md, u64 va)
{
	pgd_t *pgd = (pgd_t *)__va(real_mode_header->trampoline_pgd);
	unsigned long pf = 0;

	if (!(md->attribute & EFI_MEMORY_WB))
		pf |= _PAGE_PCD;

	if (kernel_map_pages_in_pgd(pgd, md->phys_addr, va, md->num_pages, pf))
		pr_warn("Error mapping PA 0x%llx -> VA 0x%llx!\n",
			   md->phys_addr, va);
}

void __init efi_map_region(efi_memory_desc_t *md)
{
	unsigned long size = md->num_pages << PAGE_SHIFT;
	u64 pa = md->phys_addr;

	if (efi_enabled(EFI_OLD_MEMMAP))
		return old_map_region(md);

	/*
	 * Make sure the 1:1 mappings are present as a catch-all for b0rked
	 * firmware which doesn't update all internal pointers after switching
	 * to virtual mode and would otherwise crap on us.
	 */
	__map_region(md, md->phys_addr);

	/*
	 * Enforce the 1:1 mapping as the default virtual address when
	 * booting in EFI mixed mode, because even though we may be
	 * running a 64-bit kernel, the firmware may only be 32-bit.
	 */
	if (!efi_is_native () && IS_ENABLED(CONFIG_EFI_MIXED)) {
		md->virt_addr = md->phys_addr;
		return;
	}

	efi_va -= size;

	/* Is PA 2M-aligned? */
	if (!(pa & (PMD_SIZE - 1))) {
		efi_va &= PMD_MASK;
	} else {
		u64 pa_offset = pa & (PMD_SIZE - 1);
		u64 prev_va = efi_va;

		/* get us the same offset within this 2M page */
		efi_va = (efi_va & PMD_MASK) + pa_offset;

		if (efi_va > prev_va)
			efi_va -= PMD_SIZE;
	}

	if (efi_va < EFI_VA_END) {
		pr_warn(FW_WARN "VA address range overflow!\n");
		return;
	}

	/* Do the VA map */
	__map_region(md, efi_va);
	md->virt_addr = efi_va;
}

/*
 * kexec kernel will use efi_map_region_fixed to map efi runtime memory ranges.
 * md->virt_addr is the original virtual address which had been mapped in kexec
 * 1st kernel.
 */
void __init efi_map_region_fixed(efi_memory_desc_t *md)
{
	__map_region(md, md->virt_addr);
}

void __iomem *__init efi_ioremap(unsigned long phys_addr, unsigned long size,
				 u32 type, u64 attribute)
{
	unsigned long last_map_pfn;

	if (type == EFI_MEMORY_MAPPED_IO)
		return ioremap(phys_addr, size);

	last_map_pfn = init_memory_mapping(phys_addr, phys_addr + size);
	if ((last_map_pfn << PAGE_SHIFT) < phys_addr + size) {
		unsigned long top = last_map_pfn << PAGE_SHIFT;
		efi_ioremap(top, size - (top - phys_addr), type, attribute);
	}

	if (!(attribute & EFI_MEMORY_WB))
		efi_memory_uc((u64)(unsigned long)__va(phys_addr), size);

	return (void __iomem *)__va(phys_addr);
}

void __init parse_efi_setup(u64 phys_addr, u32 data_len)
{
	efi_setup = phys_addr + sizeof(struct setup_data);
}

void __init efi_runtime_mkexec(void)
{
	if (!efi_enabled(EFI_OLD_MEMMAP))
		return;

	if (__supported_pte_mask & _PAGE_NX)
		runtime_code_page_mkexec();
}

void __init efi_dump_pagetable(void)
{
#ifdef CONFIG_EFI_PGT_DUMP
	pgd_t *pgd = (pgd_t *)__va(real_mode_header->trampoline_pgd);

	ptdump_walk_pgd_level(NULL, pgd);
#endif
}

#ifdef CONFIG_EFI_MIXED
extern efi_status_t efi64_thunk(u32, ...);

#define runtime_service32(func)						 \
({									 \
	u32 table = (u32)(unsigned long)efi.systab;			 \
	u32 *rt, *___f;							 \
									 \
	rt = (u32 *)(table + offsetof(efi_system_table_32_t, runtime));	 \
	___f = (u32 *)(*rt + offsetof(efi_runtime_services_32_t, func)); \
	*___f;								 \
})

/*
 * Switch to the EFI page tables early so that we can access the 1:1
 * runtime services mappings which are not mapped in any other page
 * tables. This function must be called before runtime_service32().
 *
 * Also, disable interrupts because the IDT points to 64-bit handlers,
 * which aren't going to function correctly when we switch to 32-bit.
 */
#define efi_thunk(f, ...)						\
({									\
	efi_status_t __s;						\
	unsigned long flags;						\
	u32 func;							\
									\
	efi_sync_low_kernel_mappings();					\
	local_irq_save(flags);						\
									\
	efi_scratch.prev_cr3 = read_cr3();				\
	write_cr3((unsigned long)efi_scratch.efi_pgt);			\
	__flush_tlb_all();						\
									\
	func = runtime_service32(f);					\
	__s = efi64_thunk(func, __VA_ARGS__);			\
									\
	write_cr3(efi_scratch.prev_cr3);				\
	__flush_tlb_all();						\
	local_irq_restore(flags);					\
									\
	__s;								\
})

efi_status_t efi_thunk_set_virtual_address_map(
	void *phys_set_virtual_address_map,
	unsigned long memory_map_size,
	unsigned long descriptor_size,
	u32 descriptor_version,
	efi_memory_desc_t *virtual_map)
{
	efi_status_t status;
	unsigned long flags;
	u32 func;

	efi_sync_low_kernel_mappings();
	local_irq_save(flags);

	efi_scratch.prev_cr3 = read_cr3();
	write_cr3((unsigned long)efi_scratch.efi_pgt);
	__flush_tlb_all();

	func = (u32)(unsigned long)phys_set_virtual_address_map;
	status = efi64_thunk(func, memory_map_size, descriptor_size,
			     descriptor_version, virtual_map);

	write_cr3(efi_scratch.prev_cr3);
	__flush_tlb_all();
	local_irq_restore(flags);

	return status;
}

static efi_status_t efi_thunk_get_time(efi_time_t *tm, efi_time_cap_t *tc)
{
	efi_status_t status;
	u32 phys_tm, phys_tc;

	spin_lock(&rtc_lock);

	phys_tm = virt_to_phys(tm);
	phys_tc = virt_to_phys(tc);

	status = efi_thunk(get_time, phys_tm, phys_tc);

	spin_unlock(&rtc_lock);

	return status;
}

static efi_status_t efi_thunk_set_time(efi_time_t *tm)
{
	efi_status_t status;
	u32 phys_tm;

	spin_lock(&rtc_lock);

	phys_tm = virt_to_phys(tm);

	status = efi_thunk(set_time, phys_tm);

	spin_unlock(&rtc_lock);

	return status;
}

static efi_status_t
efi_thunk_get_wakeup_time(efi_bool_t *enabled, efi_bool_t *pending,
			  efi_time_t *tm)
{
	efi_status_t status;
	u32 phys_enabled, phys_pending, phys_tm;

	spin_lock(&rtc_lock);

	phys_enabled = virt_to_phys(enabled);
	phys_pending = virt_to_phys(pending);
	phys_tm = virt_to_phys(tm);

	status = efi_thunk(get_wakeup_time, phys_enabled,
			     phys_pending, phys_tm);

	spin_unlock(&rtc_lock);

	return status;
}

static efi_status_t
efi_thunk_set_wakeup_time(efi_bool_t enabled, efi_time_t *tm)
{
	efi_status_t status;
	u32 phys_tm;

	spin_lock(&rtc_lock);

	phys_tm = virt_to_phys(tm);

	status = efi_thunk(set_wakeup_time, enabled, phys_tm);

	spin_unlock(&rtc_lock);

	return status;
}


static efi_status_t
efi_thunk_get_variable(efi_char16_t *name, efi_guid_t *vendor,
		       u32 *attr, unsigned long *data_size, void *data)
{
	efi_status_t status;
	u32 phys_name, phys_vendor, phys_attr;
	u32 phys_data_size, phys_data;

	phys_data_size = virt_to_phys(data_size);
	phys_vendor = virt_to_phys(vendor);
	phys_name = virt_to_phys(name);
	phys_attr = virt_to_phys(attr);
	phys_data = virt_to_phys(data);

	status = efi_thunk(get_variable, phys_name, phys_vendor,
			   phys_attr, phys_data_size, phys_data);

	return status;
}

static efi_status_t
efi_thunk_set_variable(efi_char16_t *name, efi_guid_t *vendor,
		       u32 attr, unsigned long data_size, void *data)
{
	u32 phys_name, phys_vendor, phys_data;
	efi_status_t status;

	phys_name = virt_to_phys(name);
	phys_vendor = virt_to_phys(vendor);
	phys_data = virt_to_phys(data);

	/* If data_size is > sizeof(u32) we've got problems */
	status = efi_thunk(set_variable, phys_name, phys_vendor,
			   attr, data_size, phys_data);

	return status;
}

static efi_status_t
efi_thunk_get_next_variable(unsigned long *name_size,
			    efi_char16_t *name,
			    efi_guid_t *vendor)
{
	efi_status_t status;
	u32 phys_name_size, phys_name, phys_vendor;

	phys_name_size = virt_to_phys(name_size);
	phys_vendor = virt_to_phys(vendor);
	phys_name = virt_to_phys(name);

	status = efi_thunk(get_next_variable, phys_name_size,
			   phys_name, phys_vendor);

	return status;
}

static efi_status_t
efi_thunk_get_next_high_mono_count(u32 *count)
{
	efi_status_t status;
	u32 phys_count;

	phys_count = virt_to_phys(count);
	status = efi_thunk(get_next_high_mono_count, phys_count);

	return status;
}

static void
efi_thunk_reset_system(int reset_type, efi_status_t status,
		       unsigned long data_size, efi_char16_t *data)
{
	u32 phys_data;

	phys_data = virt_to_phys(data);

	efi_thunk(reset_system, reset_type, status, data_size, phys_data);
}

static efi_status_t
efi_thunk_update_capsule(efi_capsule_header_t **capsules,
			 unsigned long count, unsigned long sg_list)
{
	/*
	 * To properly support this function we would need to repackage
	 * 'capsules' because the firmware doesn't understand 64-bit
	 * pointers.
	 */
	return EFI_UNSUPPORTED;
}

static efi_status_t
efi_thunk_query_variable_info(u32 attr, u64 *storage_space,
			      u64 *remaining_space,
			      u64 *max_variable_size)
{
	efi_status_t status;
	u32 phys_storage, phys_remaining, phys_max;

	if (efi.runtime_version < EFI_2_00_SYSTEM_TABLE_REVISION)
		return EFI_UNSUPPORTED;

	phys_storage = virt_to_phys(storage_space);
	phys_remaining = virt_to_phys(remaining_space);
	phys_max = virt_to_phys(max_variable_size);

	status = efi_thunk(query_variable_info, attr, phys_storage,
			   phys_remaining, phys_max);

	return status;
}

static efi_status_t
efi_thunk_query_capsule_caps(efi_capsule_header_t **capsules,
			     unsigned long count, u64 *max_size,
			     int *reset_type)
{
	/*
	 * To properly support this function we would need to repackage
	 * 'capsules' because the firmware doesn't understand 64-bit
	 * pointers.
	 */
	return EFI_UNSUPPORTED;
}

void efi_thunk_runtime_setup(void)
{
	efi.get_time = efi_thunk_get_time;
	efi.set_time = efi_thunk_set_time;
	efi.get_wakeup_time = efi_thunk_get_wakeup_time;
	efi.set_wakeup_time = efi_thunk_set_wakeup_time;
	efi.get_variable = efi_thunk_get_variable;
	efi.get_next_variable = efi_thunk_get_next_variable;
	efi.set_variable = efi_thunk_set_variable;
	efi.get_next_high_mono_count = efi_thunk_get_next_high_mono_count;
	efi.reset_system = efi_thunk_reset_system;
	efi.query_variable_info = efi_thunk_query_variable_info;
	efi.update_capsule = efi_thunk_update_capsule;
	efi.query_capsule_caps = efi_thunk_query_capsule_caps;
}
#endif /* CONFIG_EFI_MIXED */
