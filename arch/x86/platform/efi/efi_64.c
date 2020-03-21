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
#include <linux/bootmem.h>
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

static void __init early_code_mapping_set_exec(int executable)
{
	efi_memory_desc_t *md;

	if (!(__supported_pte_mask & _PAGE_NX))
		return;

	/* Make EFI service code area executable */
	for_each_efi_memory_desc(md) {
		if (md->type == EFI_RUNTIME_SERVICES_CODE ||
		    md->type == EFI_BOOT_SERVICES_CODE)
			efi_set_executable(md, executable);
	}
}

pgd_t * __init efi_call_phys_prolog(void)
{
	unsigned long vaddr, addr_pgd, addr_p4d, addr_pud;
	pgd_t *save_pgd, *pgd_k, *pgd_efi;
	p4d_t *p4d, *p4d_k, *p4d_efi;
	pud_t *pud;

	int pgd;
	int n_pgds, i, j;

	if (!efi_enabled(EFI_OLD_MEMMAP)) {
		efi_switch_mm(&efi_mm);
		return NULL;
	}

	early_code_mapping_set_exec(1);

	n_pgds = DIV_ROUND_UP((max_pfn << PAGE_SHIFT), PGDIR_SIZE);
	save_pgd = kmalloc_array(n_pgds, sizeof(*save_pgd), GFP_KERNEL);

	/*
	 * Build 1:1 identity mapping for efi=old_map usage. Note that
	 * PAGE_OFFSET is PGDIR_SIZE aligned when KASLR is disabled, while
	 * it is PUD_SIZE ALIGNED with KASLR enabled. So for a given physical
	 * address X, the pud_index(X) != pud_index(__va(X)), we can only copy
	 * PUD entry of __va(X) to fill in pud entry of X to build 1:1 mapping.
	 * This means here we can only reuse the PMD tables of the direct mapping.
	 */
	for (pgd = 0; pgd < n_pgds; pgd++) {
		addr_pgd = (unsigned long)(pgd * PGDIR_SIZE);
		vaddr = (unsigned long)__va(pgd * PGDIR_SIZE);
		pgd_efi = pgd_offset_k(addr_pgd);
		save_pgd[pgd] = *pgd_efi;

		p4d = p4d_alloc(&init_mm, pgd_efi, addr_pgd);
		if (!p4d) {
			pr_err("Failed to allocate p4d table!\n");
			goto out;
		}

		for (i = 0; i < PTRS_PER_P4D; i++) {
			addr_p4d = addr_pgd + i * P4D_SIZE;
			p4d_efi = p4d + p4d_index(addr_p4d);

			pud = pud_alloc(&init_mm, p4d_efi, addr_p4d);
			if (!pud) {
				pr_err("Failed to allocate pud table!\n");
				goto out;
			}

			for (j = 0; j < PTRS_PER_PUD; j++) {
				addr_pud = addr_p4d + j * PUD_SIZE;

				if (addr_pud > (max_pfn << PAGE_SHIFT))
					break;

				vaddr = (unsigned long)__va(addr_pud);

				pgd_k = pgd_offset_k(vaddr);
				p4d_k = p4d_offset(pgd_k, vaddr);
				pud[j] = *pud_offset(p4d_k, vaddr);
			}
		}
		pgd_offset_k(pgd * PGDIR_SIZE)->pgd &= ~_PAGE_NX;
	}

out:
	__flush_tlb_all();

	return save_pgd;
}

void __init efi_call_phys_epilog(pgd_t *save_pgd)
{
	/*
	 * After the lock is released, the original page table is restored.
	 */
	int pgd_idx, i;
	int nr_pgds;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;

	if (!efi_enabled(EFI_OLD_MEMMAP)) {
		efi_switch_mm(efi_scratch.prev_mm);
		return;
	}

	nr_pgds = DIV_ROUND_UP((max_pfn << PAGE_SHIFT) , PGDIR_SIZE);

	for (pgd_idx = 0; pgd_idx < nr_pgds; pgd_idx++) {
		pgd = pgd_offset_k(pgd_idx * PGDIR_SIZE);
		set_pgd(pgd_offset_k(pgd_idx * PGDIR_SIZE), save_pgd[pgd_idx]);

		if (!pgd_present(*pgd))
			continue;

		for (i = 0; i < PTRS_PER_P4D; i++) {
			p4d = p4d_offset(pgd,
					 pgd_idx * PGDIR_SIZE + i * P4D_SIZE);

			if (!p4d_present(*p4d))
				continue;

			pud = (pud_t *)p4d_page_vaddr(*p4d);
			pud_free(&init_mm, pud);
		}

		p4d = (p4d_t *)pgd_page_vaddr(*pgd);
		p4d_free(&init_mm, p4d);
	}

	kfree(save_pgd);

	__flush_tlb_all();
	early_code_mapping_set_exec(0);
}

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

	if (efi_enabled(EFI_OLD_MEMMAP))
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

	if (efi_enabled(EFI_OLD_MEMMAP))
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
	phys_addr_t pa;

	if (!va)
		return 0;

	if (virt_addr_valid(va))
		return virt_to_phys(va);

	pa = slow_virt_to_phys(va);

	/* check if the object crosses a page boundary */
	if (WARN_ON((pa ^ (pa + size - 1)) & PAGE_MASK))
		return 0;

	return pa;
}

#define virt_to_phys_or_null(addr)				\
	virt_to_phys_or_null_size((addr), sizeof(*(addr)))

int __init efi_setup_page_tables(unsigned long pa_memmap, unsigned num_pages)
{
	unsigned long pfn, text, pf;
	struct page *page;
	unsigned npages;
	pgd_t *pgd = efi_mm.pgd;

	if (efi_enabled(EFI_OLD_MEMMAP))
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
	pf = _PAGE_RW;
	if (sev_active())
		pf |= _PAGE_ENC;

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
	if (!IS_ENABLED(CONFIG_EFI_MIXED) || efi_is_native())
		return 0;

	page = alloc_page(GFP_KERNEL|__GFP_DMA32);
	if (!page) {
		pr_err("Unable to allocate EFI runtime stack < 4GB\n");
		return 1;
	}

	efi_scratch.phys_stack = page_to_phys(page + 1); /* stack grows down */

	npages = (_etext - _text) >> PAGE_SHIFT;
	text = __pa(_text);
	pfn = text >> PAGE_SHIFT;

	pf = _PAGE_RW | _PAGE_ENC;
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
	__map_region(md, md->phys_addr);
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

	if (efi_enabled(EFI_OLD_MEMMAP)) {
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
	if (efi_enabled(EFI_OLD_MEMMAP))
		ptdump_walk_pgd_level(NULL, swapper_pg_dir);
	else
		ptdump_walk_pgd_level(NULL, efi_mm.pgd);
#endif
}

/*
 * Makes the calling thread switch to/from efi_mm context. Can be used
 * for SetVirtualAddressMap() i.e. current->active_mm == init_mm as well
 * as during efi runtime calls i.e current->active_mm == current_mm.
 * We are not mm_dropping()/mm_grabbing() any mm, because we are not
 * losing/creating any references.
 */
void efi_switch_mm(struct mm_struct *mm)
{
	task_lock(current);
	efi_scratch.prev_mm = current->active_mm;
	current->active_mm = mm;
	switch_mm(efi_scratch.prev_mm, mm, NULL);
	task_unlock(current);
}

#ifdef CONFIG_EFI_MIXED
extern efi_status_t efi64_thunk(u32, ...);

static DEFINE_SPINLOCK(efi_runtime_lock);

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
	u32 __func;							\
									\
	arch_efi_call_virt_setup();					\
									\
	__func = runtime_service32(f);					\
	__s = efi64_thunk(__func, __VA_ARGS__);				\
									\
	arch_efi_call_virt_teardown();					\
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

	efi_switch_mm(&efi_mm);

	func = (u32)(unsigned long)phys_set_virtual_address_map;
	status = efi64_thunk(func, memory_map_size, descriptor_size,
			     descriptor_version, virtual_map);

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
	u8 buf[24] __aligned(8);
	efi_guid_t *vnd = PTR_ALIGN((efi_guid_t *)buf, sizeof(*vnd));
	efi_status_t status;
	u32 phys_name, phys_vendor, phys_attr;
	u32 phys_data_size, phys_data;
	unsigned long flags;

	spin_lock_irqsave(&efi_runtime_lock, flags);

	*vnd = *vendor;

	phys_data_size = virt_to_phys_or_null(data_size);
	phys_vendor = virt_to_phys_or_null(vnd);
	phys_name = virt_to_phys_or_null_size(name, efi_name_size(name));
	phys_attr = virt_to_phys_or_null(attr);
	phys_data = virt_to_phys_or_null_size(data, *data_size);

	if (!phys_name || (data && !phys_data))
		status = EFI_INVALID_PARAMETER;
	else
		status = efi_thunk(get_variable, phys_name, phys_vendor,
				   phys_attr, phys_data_size, phys_data);

	spin_unlock_irqrestore(&efi_runtime_lock, flags);

	return status;
}

static efi_status_t
efi_thunk_set_variable(efi_char16_t *name, efi_guid_t *vendor,
		       u32 attr, unsigned long data_size, void *data)
{
	u8 buf[24] __aligned(8);
	efi_guid_t *vnd = PTR_ALIGN((efi_guid_t *)buf, sizeof(*vnd));
	u32 phys_name, phys_vendor, phys_data;
	efi_status_t status;
	unsigned long flags;

	spin_lock_irqsave(&efi_runtime_lock, flags);

	*vnd = *vendor;

	phys_name = virt_to_phys_or_null_size(name, efi_name_size(name));
	phys_vendor = virt_to_phys_or_null(vnd);
	phys_data = virt_to_phys_or_null_size(data, data_size);

	if (!phys_name || !phys_data)
		status = EFI_INVALID_PARAMETER;
	else
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
	u8 buf[24] __aligned(8);
	efi_guid_t *vnd = PTR_ALIGN((efi_guid_t *)buf, sizeof(*vnd));
	u32 phys_name, phys_vendor, phys_data;
	efi_status_t status;
	unsigned long flags;

	if (!spin_trylock_irqsave(&efi_runtime_lock, flags))
		return EFI_NOT_READY;

	*vnd = *vendor;

	phys_name = virt_to_phys_or_null_size(name, efi_name_size(name));
	phys_vendor = virt_to_phys_or_null(vnd);
	phys_data = virt_to_phys_or_null_size(data, data_size);

	if (!phys_name || !phys_data)
		status = EFI_INVALID_PARAMETER;
	else
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
	u8 buf[24] __aligned(8);
	efi_guid_t *vnd = PTR_ALIGN((efi_guid_t *)buf, sizeof(*vnd));
	efi_status_t status;
	u32 phys_name_size, phys_name, phys_vendor;
	unsigned long flags;

	spin_lock_irqsave(&efi_runtime_lock, flags);

	*vnd = *vendor;

	phys_name_size = virt_to_phys_or_null(name_size);
	phys_vendor = virt_to_phys_or_null(vnd);
	phys_name = virt_to_phys_or_null_size(name, *name_size);

	if (!phys_name)
		status = EFI_INVALID_PARAMETER;
	else
		status = efi_thunk(get_next_variable, phys_name_size,
				   phys_name, phys_vendor);

	spin_unlock_irqrestore(&efi_runtime_lock, flags);

	*vendor = *vnd;
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

void efi_thunk_runtime_setup(void)
{
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
#endif /* CONFIG_EFI_MIXED */
