// SPDX-License-Identifier: GPL-2.0
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

#define pr_fmt(fmt) "efi: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/memblock.h>
#include <linux/ioport.h>
#include <linux/mc146818rtc.h>
#include <linux/efi.h>
#include <linux/export.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/ucs2_string.h>
#include <linux/mem_encrypt.h>
#include <linux/sched/task.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/e820/api.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/proto.h>
#include <asm/efi.h>
#include <asm/cacheflush.h>
#include <asm/fixmap.h>
#include <asm/realmode.h>
#include <asm/time.h>
#include <asm/pgalloc.h>

/*
 * We allocate runtime services regions top-down, starting from -4G, i.e.
 * 0xffff_ffff_0000_0000 and limit EFI VA mapping space to 64G.
 */
static u64 efi_va = EFI_VA_START;

struct efi_scratch efi_scratch;

EXPORT_SYMBOL_GPL(efi_mm);

/*
 * We need our own copy of the higher levels of the page tables
 * because we want to avoid inserting EFI region mappings (EFI_VA_END
 * to EFI_VA_START) into the standard kernel page tables. Everything
 * else can be shared, see efi_sync_low_kernel_mappings().
 *
 * We don't want the pgd on the pgd_list and cannot use pgd_alloc() for the
 * allocation.
 */
int __init efi_alloc_page_tables(void)
{
	pgd_t *pgd, *efi_pgd;
	p4d_t *p4d;
	pud_t *pud;
	gfp_t gfp_mask;

	if (efi_have_uv1_memmap())
		return 0;

	gfp_mask = GFP_KERNEL | __GFP_ZERO;
	efi_pgd = (pgd_t *)__get_free_pages(gfp_mask, PGD_ALLOCATION_ORDER);
	if (!efi_pgd)
		return -ENOMEM;

	pgd = efi_pgd + pgd_index(EFI_VA_END);
	p4d = p4d_alloc(&init_mm, pgd, EFI_VA_END);
	if (!p4d) {
		free_page((unsigned long)efi_pgd);
		return -ENOMEM;
	}

	pud = pud_alloc(&init_mm, p4d, EFI_VA_END);
	if (!pud) {
		if (pgtable_l5_enabled())
			free_page((unsigned long) pgd_page_vaddr(*pgd));
		free_pages((unsigned long)efi_pgd, PGD_ALLOCATION_ORDER);
		return -ENOMEM;
	}

	efi_mm.pgd = efi_pgd;
	mm_init_cpumask(&efi_mm);
	init_new_context(NULL, &efi_mm);

	return 0;
}

/*
 * Add low kernel mappings for passing arguments to EFI functions.
 */
void efi_sync_low_kernel_mappings(void)
{
	unsigned num_entries;
	pgd_t *pgd_k, *pgd_efi;
	p4d_t *p4d_k, *p4d_efi;
	pud_t *pud_k, *pud_efi;
	pgd_t *efi_pgd = efi_mm.pgd;

	if (efi_have_uv1_memmap())
		return;

	/*
	 * We can share all PGD entries apart from the one entry that
	 * covers the EFI runtime mapping space.
	 *
	 * Make sure the EFI runtime region mappings are guaranteed to
	 * only span a single PGD entry and that the entry also maps
	 * other important kernel regions.
	 */
	MAYBE_BUILD_BUG_ON(pgd_index(EFI_VA_END) != pgd_index(MODULES_END));
	MAYBE_BUILD_BUG_ON((EFI_VA_START & PGDIR_MASK) !=
			(EFI_VA_END & PGDIR_MASK));

	pgd_efi = efi_pgd + pgd_index(PAGE_OFFSET);
	pgd_k = pgd_offset_k(PAGE_OFFSET);

	num_entries = pgd_index(EFI_VA_END) - pgd_index(PAGE_OFFSET);
	memcpy(pgd_efi, pgd_k, sizeof(pgd_t) * num_entries);

	/*
	 * As with PGDs, we share all P4D entries apart from the one entry
	 * that covers the EFI runtime mapping space.
	 */
	BUILD_BUG_ON(p4d_index(EFI_VA_END) != p4d_index(MODULES_END));
	BUILD_BUG_ON((EFI_VA_START & P4D_MASK) != (EFI_VA_END & P4D_MASK));

	pgd_efi = efi_pgd + pgd_index(EFI_VA_END);
	pgd_k = pgd_offset_k(EFI_VA_END);
	p4d_efi = p4d_offset(pgd_efi, 0);
	p4d_k = p4d_offset(pgd_k, 0);

	num_entries = p4d_index(EFI_VA_END);
	memcpy(p4d_efi, p4d_k, sizeof(p4d_t) * num_entries);

	/*
	 * We share all the PUD entries apart from those that map the
	 * EFI regions. Copy around them.
	 */
	BUILD_BUG_ON((EFI_VA_START & ~PUD_MASK) != 0);
	BUILD_BUG_ON((EFI_VA_END & ~PUD_MASK) != 0);

	p4d_efi = p4d_offset(pgd_efi, EFI_VA_END);
	p4d_k = p4d_offset(pgd_k, EFI_VA_END);
	pud_efi = pud_offset(p4d_efi, 0);
	pud_k = pud_offset(p4d_k, 0);

	num_entries = pud_index(EFI_VA_END);
	memcpy(pud_efi, pud_k, sizeof(pud_t) * num_entries);

	pud_efi = pud_offset(p4d_efi, EFI_VA_START);
	pud_k = pud_offset(p4d_k, EFI_VA_START);

	num_entries = PTRS_PER_PUD - pud_index(EFI_VA_START);
	memcpy(pud_efi, pud_k, sizeof(pud_t) * num_entries);
}

/*
 * Wrapper for slow_virt_to_phys() that handles NULL addresses.
 */
static inline phys_addr_t
virt_to_phys_or_null_size(void *va, unsigned long size)
{
	bool bad_size;

	if (!va)
		return 0;

	if (virt_addr_valid(va))
		return virt_to_phys(va);

	/*
	 * A fully aligned variable on the stack is guaranteed not to
	 * cross a page bounary. Try to catch strings on the stack by
	 * checking that 'size' is a power of two.
	 */
	bad_size = size > PAGE_SIZE || !is_power_of_2(size);

	WARN_ON(!IS_ALIGNED((unsigned long)va, size) || bad_size);

	return slow_virt_to_phys(va);
}

#define virt_to_phys_or_null(addr)				\
	virt_to_phys_or_null_size((addr), sizeof(*(addr)))

int __init efi_setup_page_tables(unsigned long pa_memmap, unsigned num_pages)
{
	unsigned long pfn, text, pf;
	struct page *page;
	unsigned npages;
	pgd_t *pgd = efi_mm.pgd;

	if (efi_have_uv1_memmap())
		return 0;

	/*
	 * It can happen that the physical address of new_memmap lands in memory
	 * which is not mapped in the EFI page table. Therefore we need to go
	 * and ident-map those pages containing the map before calling
	 * phys_efi_set_virtual_address_map().
	 */
	pfn = pa_memmap >> PAGE_SHIFT;
	pf = _PAGE_NX | _PAGE_RW | _PAGE_ENC;
	if (kernel_map_pages_in_pgd(pgd, pfn, pa_memmap, num_pages, pf)) {
		pr_err("Error ident-mapping new memmap (0x%lx)!\n", pa_memmap);
		return 1;
	}

	/*
	 * Certain firmware versions are way too sentimential and still believe
	 * they are exclusive and unquestionable owners of the first physical page,
	 * even though they explicitly mark it as EFI_CONVENTIONAL_MEMORY
	 * (but then write-access it later during SetVirtualAddressMap()).
	 *
	 * Create a 1:1 mapping for this page, to avoid triple faults during early
	 * boot with such firmware. We are free to hand this page to the BIOS,
	 * as trim_bios_range() will reserve the first page and isolate it away
	 * from memory allocators anyway.
	 */
	if (kernel_map_pages_in_pgd(pgd, 0x0, 0x0, 1, pf)) {
		pr_err("Failed to create 1:1 mapping for the first page!\n");
		return 1;
	}

	/*
	 * When making calls to the firmware everything needs to be 1:1
	 * mapped and addressable with 32-bit pointers. Map the kernel
	 * text and allocate a new stack because we can't rely on the
	 * stack pointer being < 4GB.
	 */
	if (!efi_is_mixed())
		return 0;

	page = alloc_page(GFP_KERNEL|__GFP_DMA32);
	if (!page) {
		pr_err("Unable to allocate EFI runtime stack < 4GB\n");
		return 1;
	}

	efi_scratch.phys_stack = page_to_phys(page + 1); /* stack grows down */

	npages = (__end_rodata_aligned - _text) >> PAGE_SHIFT;
	text = __pa(_text);
	pfn = text >> PAGE_SHIFT;

	pf = _PAGE_ENC;
	if (kernel_map_pages_in_pgd(pgd, pfn, text, npages, pf)) {
		pr_err("Failed to map kernel text 1:1\n");
		return 1;
	}

	return 0;
}

static void __init __map_region(efi_memory_desc_t *md, u64 va)
{
	unsigned long flags = _PAGE_RW;
	unsigned long pfn;
	pgd_t *pgd = efi_mm.pgd;

	/*
	 * EFI_RUNTIME_SERVICES_CODE regions typically cover PE/COFF
	 * executable images in memory that consist of both R-X and
	 * RW- sections, so we cannot apply read-only or non-exec
	 * permissions just yet. However, modern EFI systems provide
	 * a memory attributes table that describes those sections
	 * with the appropriate restricted permissions, which are
	 * applied in efi_runtime_update_mappings() below. All other
	 * regions can be mapped non-executable at this point, with
	 * the exception of boot services code regions, but those will
	 * be unmapped again entirely in efi_free_boot_services().
	 */
	if (md->type != EFI_BOOT_SERVICES_CODE &&
	    md->type != EFI_RUNTIME_SERVICES_CODE)
		flags |= _PAGE_NX;

	if (!(md->attribute & EFI_MEMORY_WB))
		flags |= _PAGE_PCD;

	if (sev_active() && md->type != EFI_MEMORY_MAPPED_IO)
		flags |= _PAGE_ENC;

	pfn = md->phys_addr >> PAGE_SHIFT;
	if (kernel_map_pages_in_pgd(pgd, pfn, va, md->num_pages, flags))
		pr_warn("Error mapping PA 0x%llx -> VA 0x%llx!\n",
			   md->phys_addr, va);
}

void __init efi_map_region(efi_memory_desc_t *md)
{
	unsigned long size = md->num_pages << PAGE_SHIFT;
	u64 pa = md->phys_addr;

	if (efi_have_uv1_memmap())
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
	if (efi_is_mixed()) {
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
	__map_region(md, md->phys_addr);
	__map_region(md, md->virt_addr);
}

void __init parse_efi_setup(u64 phys_addr, u32 data_len)
{
	efi_setup = phys_addr + sizeof(struct setup_data);
}

static int __init efi_update_mappings(efi_memory_desc_t *md, unsigned long pf)
{
	unsigned long pfn;
	pgd_t *pgd = efi_mm.pgd;
	int err1, err2;

	/* Update the 1:1 mapping */
	pfn = md->phys_addr >> PAGE_SHIFT;
	err1 = kernel_map_pages_in_pgd(pgd, pfn, md->phys_addr, md->num_pages, pf);
	if (err1) {
		pr_err("Error while updating 1:1 mapping PA 0x%llx -> VA 0x%llx!\n",
			   md->phys_addr, md->virt_addr);
	}

	err2 = kernel_map_pages_in_pgd(pgd, pfn, md->virt_addr, md->num_pages, pf);
	if (err2) {
		pr_err("Error while updating VA mapping PA 0x%llx -> VA 0x%llx!\n",
			   md->phys_addr, md->virt_addr);
	}

	return err1 || err2;
}

static int __init efi_update_mem_attr(struct mm_struct *mm, efi_memory_desc_t *md)
{
	unsigned long pf = 0;

	if (md->attribute & EFI_MEMORY_XP)
		pf |= _PAGE_NX;

	if (!(md->attribute & EFI_MEMORY_RO))
		pf |= _PAGE_RW;

	if (sev_active())
		pf |= _PAGE_ENC;

	return efi_update_mappings(md, pf);
}

void __init efi_runtime_update_mappings(void)
{
	efi_memory_desc_t *md;

	if (efi_have_uv1_memmap()) {
		if (__supported_pte_mask & _PAGE_NX)
			runtime_code_page_mkexec();
		return;
	}

	/*
	 * Use the EFI Memory Attribute Table for mapping permissions if it
	 * exists, since it is intended to supersede EFI_PROPERTIES_TABLE.
	 */
	if (efi_enabled(EFI_MEM_ATTR)) {
		efi_memattr_apply_permissions(NULL, efi_update_mem_attr);
		return;
	}

	/*
	 * EFI_MEMORY_ATTRIBUTES_TABLE is intended to replace
	 * EFI_PROPERTIES_TABLE. So, use EFI_PROPERTIES_TABLE to update
	 * permissions only if EFI_MEMORY_ATTRIBUTES_TABLE is not
	 * published by the firmware. Even if we find a buggy implementation of
	 * EFI_MEMORY_ATTRIBUTES_TABLE, don't fall back to
	 * EFI_PROPERTIES_TABLE, because of the same reason.
	 */

	if (!efi_enabled(EFI_NX_PE_DATA))
		return;

	for_each_efi_memory_desc(md) {
		unsigned long pf = 0;

		if (!(md->attribute & EFI_MEMORY_RUNTIME))
			continue;

		if (!(md->attribute & EFI_MEMORY_WB))
			pf |= _PAGE_PCD;

		if ((md->attribute & EFI_MEMORY_XP) ||
			(md->type == EFI_RUNTIME_SERVICES_DATA))
			pf |= _PAGE_NX;

		if (!(md->attribute & EFI_MEMORY_RO) &&
			(md->type != EFI_RUNTIME_SERVICES_CODE))
			pf |= _PAGE_RW;

		if (sev_active())
			pf |= _PAGE_ENC;

		efi_update_mappings(md, pf);
	}
}

void __init efi_dump_pagetable(void)
{
#ifdef CONFIG_EFI_PGT_DUMP
	if (efi_have_uv1_memmap())
		ptdump_walk_pgd_level(NULL, &init_mm);
	else
		ptdump_walk_pgd_level(NULL, &efi_mm);
#endif
}

/*
 * Makes the calling thread switch to/from efi_mm context. Can be used
 * in a kernel thread and user context. Preemption needs to remain disabled
 * while the EFI-mm is borrowed. mmgrab()/mmdrop() is not used because the mm
 * can not change under us.
 * It should be ensured that there are no concurent calls to this function.
 */
void efi_switch_mm(struct mm_struct *mm)
{
	efi_scratch.prev_mm = current->active_mm;
	current->active_mm = mm;
	switch_mm(efi_scratch.prev_mm, mm, NULL);
}

static DEFINE_SPINLOCK(efi_runtime_lock);

/*
 * DS and ES contain user values.  We need to save them.
 * The 32-bit EFI code needs a valid DS, ES, and SS.  There's no
 * need to save the old SS: __KERNEL_DS is always acceptable.
 */
#define __efi_thunk(func, ...)						\
({									\
	efi_runtime_services_32_t *__rt;				\
	unsigned short __ds, __es;					\
	efi_status_t ____s;						\
									\
	__rt = (void *)(unsigned long)efi.systab->mixed_mode.runtime;	\
									\
	savesegment(ds, __ds);						\
	savesegment(es, __es);						\
									\
	loadsegment(ss, __KERNEL_DS);					\
	loadsegment(ds, __KERNEL_DS);					\
	loadsegment(es, __KERNEL_DS);					\
									\
	____s = efi64_thunk(__rt->func, __VA_ARGS__);			\
									\
	loadsegment(ds, __ds);						\
	loadsegment(es, __es);						\
									\
	____s ^= (____s & BIT(31)) | (____s & BIT_ULL(31)) << 32;	\
	____s;								\
})

/*
 * Switch to the EFI page tables early so that we can access the 1:1
 * runtime services mappings which are not mapped in any other page
 * tables.
 *
 * Also, disable interrupts because the IDT points to 64-bit handlers,
 * which aren't going to function correctly when we switch to 32-bit.
 */
#define efi_thunk(func...)						\
({									\
	efi_status_t __s;						\
									\
	arch_efi_call_virt_setup();					\
									\
	__s = __efi_thunk(func);					\
									\
	arch_efi_call_virt_teardown();					\
									\
	__s;								\
})

static efi_status_t __init __no_sanitize_address
efi_thunk_set_virtual_address_map(unsigned long memory_map_size,
				  unsigned long descriptor_size,
				  u32 descriptor_version,
				  efi_memory_desc_t *virtual_map)
{
	efi_status_t status;
	unsigned long flags;

	efi_sync_low_kernel_mappings();
	local_irq_save(flags);

	efi_switch_mm(&efi_mm);

	status = __efi_thunk(set_virtual_address_map, memory_map_size,
			     descriptor_size, descriptor_version, virtual_map);

	efi_switch_mm(efi_scratch.prev_mm);
	local_irq_restore(flags);

	return status;
}

static efi_status_t efi_thunk_get_time(efi_time_t *tm, efi_time_cap_t *tc)
{
	efi_status_t status;
	u32 phys_tm, phys_tc;
	unsigned long flags;

	spin_lock(&rtc_lock);
	spin_lock_irqsave(&efi_runtime_lock, flags);

	phys_tm = virt_to_phys_or_null(tm);
	phys_tc = virt_to_phys_or_null(tc);

	status = efi_thunk(get_time, phys_tm, phys_tc);

	spin_unlock_irqrestore(&efi_runtime_lock, flags);
	spin_unlock(&rtc_lock);

	return status;
}

static efi_status_t efi_thunk_set_time(efi_time_t *tm)
{
	efi_status_t status;
	u32 phys_tm;
	unsigned long flags;

	spin_lock(&rtc_lock);
	spin_lock_irqsave(&efi_runtime_lock, flags);

	phys_tm = virt_to_phys_or_null(tm);

	status = efi_thunk(set_time, phys_tm);

	spin_unlock_irqrestore(&efi_runtime_lock, flags);
	spin_unlock(&rtc_lock);

	return status;
}

static efi_status_t
efi_thunk_get_wakeup_time(efi_bool_t *enabled, efi_bool_t *pending,
			  efi_time_t *tm)
{
	efi_status_t status;
	u32 phys_enabled, phys_pending, phys_tm;
	unsigned long flags;

	spin_lock(&rtc_lock);
	spin_lock_irqsave(&efi_runtime_lock, flags);

	phys_enabled = virt_to_phys_or_null(enabled);
	phys_pending = virt_to_phys_or_null(pending);
	phys_tm = virt_to_phys_or_null(tm);

	status = efi_thunk(get_wakeup_time, phys_enabled,
			     phys_pending, phys_tm);

	spin_unlock_irqrestore(&efi_runtime_lock, flags);
	spin_unlock(&rtc_lock);

	return status;
}

static efi_status_t
efi_thunk_set_wakeup_time(efi_bool_t enabled, efi_time_t *tm)
{
	efi_status_t status;
	u32 phys_tm;
	unsigned long flags;

	spin_lock(&rtc_lock);
	spin_lock_irqsave(&efi_runtime_lock, flags);

	phys_tm = virt_to_phys_or_null(tm);

	status = efi_thunk(set_wakeup_time, enabled, phys_tm);

	spin_unlock_irqrestore(&efi_runtime_lock, flags);
	spin_unlock(&rtc_lock);

	return status;
}

static unsigned long efi_name_size(efi_char16_t *name)
{
	return ucs2_strsize(name, EFI_VAR_NAME_LEN) + 1;
}

static efi_status_t
efi_thunk_get_variable(efi_char16_t *name, efi_guid_t *vendor,
		       u32 *attr, unsigned long *data_size, void *data)
{
	efi_status_t status;
	u32 phys_name, phys_vendor, phys_attr;
	u32 phys_data_size, phys_data;
	unsigned long flags;

	spin_lock_irqsave(&efi_runtime_lock, flags);

	phys_data_size = virt_to_phys_or_null(data_size);
	phys_vendor = virt_to_phys_or_null(vendor);
	phys_name = virt_to_phys_or_null_size(name, efi_name_size(name));
	phys_attr = virt_to_phys_or_null(attr);
	phys_data = virt_to_phys_or_null_size(data, *data_size);

	status = efi_thunk(get_variable, phys_name, phys_vendor,
			   phys_attr, phys_data_size, phys_data);

	spin_unlock_irqrestore(&efi_runtime_lock, flags);

	return status;
}

static efi_status_t
efi_thunk_set_variable(efi_char16_t *name, efi_guid_t *vendor,
		       u32 attr, unsigned long data_size, void *data)
{
	u32 phys_name, phys_vendor, phys_data;
	efi_status_t status;
	unsigned long flags;

	spin_lock_irqsave(&efi_runtime_lock, flags);

	phys_name = virt_to_phys_or_null_size(name, efi_name_size(name));
	phys_vendor = virt_to_phys_or_null(vendor);
	phys_data = virt_to_phys_or_null_size(data, data_size);

	/* If data_size is > sizeof(u32) we've got problems */
	status = efi_thunk(set_variable, phys_name, phys_vendor,
			   attr, data_size, phys_data);

	spin_unlock_irqrestore(&efi_runtime_lock, flags);

	return status;
}

static efi_status_t
efi_thunk_set_variable_nonblocking(efi_char16_t *name, efi_guid_t *vendor,
				   u32 attr, unsigned long data_size,
				   void *data)
{
	u32 phys_name, phys_vendor, phys_data;
	efi_status_t status;
	unsigned long flags;

	if (!spin_trylock_irqsave(&efi_runtime_lock, flags))
		return EFI_NOT_READY;

	phys_name = virt_to_phys_or_null_size(name, efi_name_size(name));
	phys_vendor = virt_to_phys_or_null(vendor);
	phys_data = virt_to_phys_or_null_size(data, data_size);

	/* If data_size is > sizeof(u32) we've got problems */
	status = efi_thunk(set_variable, phys_name, phys_vendor,
			   attr, data_size, phys_data);

	spin_unlock_irqrestore(&efi_runtime_lock, flags);

	return status;
}

static efi_status_t
efi_thunk_get_next_variable(unsigned long *name_size,
			    efi_char16_t *name,
			    efi_guid_t *vendor)
{
	efi_status_t status;
	u32 phys_name_size, phys_name, phys_vendor;
	unsigned long flags;

	spin_lock_irqsave(&efi_runtime_lock, flags);

	phys_name_size = virt_to_phys_or_null(name_size);
	phys_vendor = virt_to_phys_or_null(vendor);
	phys_name = virt_to_phys_or_null_size(name, *name_size);

	status = efi_thunk(get_next_variable, phys_name_size,
			   phys_name, phys_vendor);

	spin_unlock_irqrestore(&efi_runtime_lock, flags);

	return status;
}

static efi_status_t
efi_thunk_get_next_high_mono_count(u32 *count)
{
	efi_status_t status;
	u32 phys_count;
	unsigned long flags;

	spin_lock_irqsave(&efi_runtime_lock, flags);

	phys_count = virt_to_phys_or_null(count);
	status = efi_thunk(get_next_high_mono_count, phys_count);

	spin_unlock_irqrestore(&efi_runtime_lock, flags);

	return status;
}

static void
efi_thunk_reset_system(int reset_type, efi_status_t status,
		       unsigned long data_size, efi_char16_t *data)
{
	u32 phys_data;
	unsigned long flags;

	spin_lock_irqsave(&efi_runtime_lock, flags);

	phys_data = virt_to_phys_or_null_size(data, data_size);

	efi_thunk(reset_system, reset_type, status, data_size, phys_data);

	spin_unlock_irqrestore(&efi_runtime_lock, flags);
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
	unsigned long flags;

	if (efi.runtime_version < EFI_2_00_SYSTEM_TABLE_REVISION)
		return EFI_UNSUPPORTED;

	spin_lock_irqsave(&efi_runtime_lock, flags);

	phys_storage = virt_to_phys_or_null(storage_space);
	phys_remaining = virt_to_phys_or_null(remaining_space);
	phys_max = virt_to_phys_or_null(max_variable_size);

	status = efi_thunk(query_variable_info, attr, phys_storage,
			   phys_remaining, phys_max);

	spin_unlock_irqrestore(&efi_runtime_lock, flags);

	return status;
}

static efi_status_t
efi_thunk_query_variable_info_nonblocking(u32 attr, u64 *storage_space,
					  u64 *remaining_space,
					  u64 *max_variable_size)
{
	efi_status_t status;
	u32 phys_storage, phys_remaining, phys_max;
	unsigned long flags;

	if (efi.runtime_version < EFI_2_00_SYSTEM_TABLE_REVISION)
		return EFI_UNSUPPORTED;

	if (!spin_trylock_irqsave(&efi_runtime_lock, flags))
		return EFI_NOT_READY;

	phys_storage = virt_to_phys_or_null(storage_space);
	phys_remaining = virt_to_phys_or_null(remaining_space);
	phys_max = virt_to_phys_or_null(max_variable_size);

	status = efi_thunk(query_variable_info, attr, phys_storage,
			   phys_remaining, phys_max);

	spin_unlock_irqrestore(&efi_runtime_lock, flags);

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

void __init efi_thunk_runtime_setup(void)
{
	if (!IS_ENABLED(CONFIG_EFI_MIXED))
		return;

	efi.get_time = efi_thunk_get_time;
	efi.set_time = efi_thunk_set_time;
	efi.get_wakeup_time = efi_thunk_get_wakeup_time;
	efi.set_wakeup_time = efi_thunk_set_wakeup_time;
	efi.get_variable = efi_thunk_get_variable;
	efi.get_next_variable = efi_thunk_get_next_variable;
	efi.set_variable = efi_thunk_set_variable;
	efi.set_variable_nonblocking = efi_thunk_set_variable_nonblocking;
	efi.get_next_high_mono_count = efi_thunk_get_next_high_mono_count;
	efi.reset_system = efi_thunk_reset_system;
	efi.query_variable_info = efi_thunk_query_variable_info;
	efi.query_variable_info_nonblocking = efi_thunk_query_variable_info_nonblocking;
	efi.update_capsule = efi_thunk_update_capsule;
	efi.query_capsule_caps = efi_thunk_query_capsule_caps;
}

efi_status_t __init __no_sanitize_address
efi_set_virtual_address_map(unsigned long memory_map_size,
			    unsigned long descriptor_size,
			    u32 descriptor_version,
			    efi_memory_desc_t *virtual_map)
{
	efi_status_t status;
	unsigned long flags;
	pgd_t *save_pgd = NULL;

	if (efi_is_mixed())
		return efi_thunk_set_virtual_address_map(memory_map_size,
							 descriptor_size,
							 descriptor_version,
							 virtual_map);

	if (efi_have_uv1_memmap()) {
		save_pgd = efi_uv1_memmap_phys_prolog();
		if (!save_pgd)
			return EFI_ABORTED;
	} else {
		efi_switch_mm(&efi_mm);
	}

	kernel_fpu_begin();

	/* Disable interrupts around EFI calls: */
	local_irq_save(flags);
	status = efi_call(efi.systab->runtime->set_virtual_address_map,
			  memory_map_size, descriptor_size,
			  descriptor_version, virtual_map);
	local_irq_restore(flags);

	kernel_fpu_end();

	if (save_pgd)
		efi_uv1_memmap_phys_epilog(save_pgd);
	else
		efi_switch_mm(efi_scratch.prev_mm);

	return status;
}
