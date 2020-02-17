// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * BIOS run time interface routines.
 *
 *  Copyright (c) 2008-2009 Silicon Graphics, Inc.  All Rights Reserved.
 *  Copyright (c) Russ Anderson <rja@sgi.com>
 */

#include <linux/efi.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <asm/efi.h>
#include <linux/io.h>
#include <asm/uv/bios.h>
#include <asm/uv/uv_hub.h>

unsigned long uv_systab_phys __ro_after_init = EFI_INVALID_TABLE_ADDR;

struct uv_systab *uv_systab;

static s64 __uv_bios_call(enum uv_bios_cmd which, u64 a1, u64 a2, u64 a3,
			u64 a4, u64 a5)
{
	struct uv_systab *tab = uv_systab;
	s64 ret;

	if (!tab || !tab->function)
		/*
		 * BIOS does not support UV systab
		 */
		return BIOS_STATUS_UNIMPLEMENTED;

	/*
	 * If EFI_UV1_MEMMAP is set, we need to fall back to using our old EFI
	 * callback method, which uses efi_call() directly, with the kernel page tables:
	 */
	if (unlikely(efi_enabled(EFI_UV1_MEMMAP))) {
		kernel_fpu_begin();
		ret = efi_call((void *)__va(tab->function), (u64)which, a1, a2, a3, a4, a5);
		kernel_fpu_end();
	} else {
		ret = efi_call_virt_pointer(tab, function, (u64)which, a1, a2, a3, a4, a5);
	}

	return ret;
}

s64 uv_bios_call(enum uv_bios_cmd which, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5)
{
	s64 ret;

	if (down_interruptible(&__efi_uv_runtime_lock))
		return BIOS_STATUS_ABORT;

	ret = __uv_bios_call(which, a1, a2, a3, a4, a5);
	up(&__efi_uv_runtime_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(uv_bios_call);

s64 uv_bios_call_irqsave(enum uv_bios_cmd which, u64 a1, u64 a2, u64 a3,
					u64 a4, u64 a5)
{
	unsigned long bios_flags;
	s64 ret;

	if (down_interruptible(&__efi_uv_runtime_lock))
		return BIOS_STATUS_ABORT;

	local_irq_save(bios_flags);
	ret = __uv_bios_call(which, a1, a2, a3, a4, a5);
	local_irq_restore(bios_flags);

	up(&__efi_uv_runtime_lock);

	return ret;
}


long sn_partition_id;
EXPORT_SYMBOL_GPL(sn_partition_id);
long sn_coherency_id;
EXPORT_SYMBOL_GPL(sn_coherency_id);
long sn_region_size;
EXPORT_SYMBOL_GPL(sn_region_size);
long system_serial_number;
EXPORT_SYMBOL_GPL(system_serial_number);
int uv_type;
EXPORT_SYMBOL_GPL(uv_type);


s64 uv_bios_get_sn_info(int fc, int *uvtype, long *partid, long *coher,
		long *region, long *ssn)
{
	s64 ret;
	u64 v0, v1;
	union partition_info_u part;

	ret = uv_bios_call_irqsave(UV_BIOS_GET_SN_INFO, fc,
				(u64)(&v0), (u64)(&v1), 0, 0);
	if (ret != BIOS_STATUS_SUCCESS)
		return ret;

	part.val = v0;
	if (uvtype)
		*uvtype = part.hub_version;
	if (partid)
		*partid = part.partition_id;
	if (coher)
		*coher = part.coherence_id;
	if (region)
		*region = part.region_size;
	if (ssn)
		*ssn = v1;
	return ret;
}
EXPORT_SYMBOL_GPL(uv_bios_get_sn_info);

int
uv_bios_mq_watchlist_alloc(unsigned long addr, unsigned int mq_size,
			   unsigned long *intr_mmr_offset)
{
	u64 watchlist;
	s64 ret;

	/*
	 * bios returns watchlist number or negative error number.
	 */
	ret = (int)uv_bios_call_irqsave(UV_BIOS_WATCHLIST_ALLOC, addr,
			mq_size, (u64)intr_mmr_offset,
			(u64)&watchlist, 0);
	if (ret < BIOS_STATUS_SUCCESS)
		return ret;

	return watchlist;
}
EXPORT_SYMBOL_GPL(uv_bios_mq_watchlist_alloc);

int
uv_bios_mq_watchlist_free(int blade, int watchlist_num)
{
	return (int)uv_bios_call_irqsave(UV_BIOS_WATCHLIST_FREE,
				blade, watchlist_num, 0, 0, 0);
}
EXPORT_SYMBOL_GPL(uv_bios_mq_watchlist_free);

s64
uv_bios_change_memprotect(u64 paddr, u64 len, enum uv_memprotect perms)
{
	return uv_bios_call_irqsave(UV_BIOS_MEMPROTECT, paddr, len,
					perms, 0, 0);
}
EXPORT_SYMBOL_GPL(uv_bios_change_memprotect);

s64
uv_bios_reserved_page_pa(u64 buf, u64 *cookie, u64 *addr, u64 *len)
{
	return uv_bios_call_irqsave(UV_BIOS_GET_PARTITION_ADDR, (u64)cookie,
				    (u64)addr, buf, (u64)len, 0);
}
EXPORT_SYMBOL_GPL(uv_bios_reserved_page_pa);

s64 uv_bios_freq_base(u64 clock_type, u64 *ticks_per_second)
{
	return uv_bios_call(UV_BIOS_FREQ_BASE, clock_type,
			   (u64)ticks_per_second, 0, 0, 0);
}
EXPORT_SYMBOL_GPL(uv_bios_freq_base);

/*
 * uv_bios_set_legacy_vga_target - Set Legacy VGA I/O Target
 * @decode: true to enable target, false to disable target
 * @domain: PCI domain number
 * @bus: PCI bus number
 *
 * Returns:
 *    0: Success
 *    -EINVAL: Invalid domain or bus number
 *    -ENOSYS: Capability not available
 *    -EBUSY: Legacy VGA I/O cannot be retargeted at this time
 */
int uv_bios_set_legacy_vga_target(bool decode, int domain, int bus)
{
	return uv_bios_call(UV_BIOS_SET_LEGACY_VGA_TARGET,
				(u64)decode, (u64)domain, (u64)bus, 0, 0);
}
EXPORT_SYMBOL_GPL(uv_bios_set_legacy_vga_target);

int uv_bios_init(void)
{
	uv_systab = NULL;
	if ((uv_systab_phys == EFI_INVALID_TABLE_ADDR) ||
	    !uv_systab_phys || efi_runtime_disabled()) {
		pr_crit("UV: UVsystab: missing\n");
		return -EEXIST;
	}

	uv_systab = ioremap(uv_systab_phys, sizeof(struct uv_systab));
	if (!uv_systab || strncmp(uv_systab->signature, UV_SYSTAB_SIG, 4)) {
		pr_err("UV: UVsystab: bad signature!\n");
		iounmap(uv_systab);
		return -EINVAL;
	}

	/* Starting with UV4 the UV systab size is variable */
	if (uv_systab->revision >= UV_SYSTAB_VERSION_UV4) {
		int size = uv_systab->size;

		iounmap(uv_systab);
		uv_systab = ioremap(uv_systab_phys, size);
		if (!uv_systab) {
			pr_err("UV: UVsystab: ioremap(%d) failed!\n", size);
			return -EFAULT;
		}
	}
	pr_info("UV: UVsystab: Revision:%x\n", uv_systab->revision);
	return 0;
}

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

void __init efi_uv1_memmap_phys_epilog(pgd_t *save_pgd)
{
	/*
	 * After the lock is released, the original page table is restored.
	 */
	int pgd_idx, i;
	int nr_pgds;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;

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

pgd_t * __init efi_uv1_memmap_phys_prolog(void)
{
	unsigned long vaddr, addr_pgd, addr_p4d, addr_pud;
	pgd_t *save_pgd, *pgd_k, *pgd_efi;
	p4d_t *p4d, *p4d_k, *p4d_efi;
	pud_t *pud;

	int pgd;
	int n_pgds, i, j;

	early_code_mapping_set_exec(1);

	n_pgds = DIV_ROUND_UP((max_pfn << PAGE_SHIFT), PGDIR_SIZE);
	save_pgd = kmalloc_array(n_pgds, sizeof(*save_pgd), GFP_KERNEL);
	if (!save_pgd)
		return NULL;

	/*
	 * Build 1:1 identity mapping for UV1 memmap usage. Note that
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

	__flush_tlb_all();
	return save_pgd;
out:
	efi_uv1_memmap_phys_epilog(save_pgd);
	return NULL;
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

static int __init arch_parse_efi_cmdline(char *str)
{
	if (!str) {
		pr_warn("need at least one option\n");
		return -EINVAL;
	}

	if (!efi_is_mixed() && parse_option_str(str, "old_map"))
		set_bit(EFI_UV1_MEMMAP, &efi.flags);

	return 0;
}
early_param("efi", arch_parse_efi_cmdline);
