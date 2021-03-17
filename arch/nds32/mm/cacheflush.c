// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/module.h>
#include <asm/cacheflush.h>
#include <asm/proc-fns.h>
#include <asm/shmparam.h>
#include <asm/cache_info.h>

extern struct cache_info L1_cache_info[2];

void flush_icache_range(unsigned long start, unsigned long end)
{
	unsigned long line_size, flags;
	line_size = L1_cache_info[DCACHE].line_size;
	start = start & ~(line_size - 1);
	end = (end + line_size - 1) & ~(line_size - 1);
	local_irq_save(flags);
	cpu_cache_wbinval_range(start, end, 1);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(flush_icache_range);

void flush_icache_page(struct vm_area_struct *vma, struct page *page)
{
	unsigned long flags;
	unsigned long kaddr;
	local_irq_save(flags);
	kaddr = (unsigned long)kmap_atomic(page);
	cpu_cache_wbinval_page(kaddr, vma->vm_flags & VM_EXEC);
	kunmap_atomic((void *)kaddr);
	local_irq_restore(flags);
}

void flush_icache_user_page(struct vm_area_struct *vma, struct page *page,
	                     unsigned long addr, int len)
{
	unsigned long kaddr;
	kaddr = (unsigned long)kmap_atomic(page) + (addr & ~PAGE_MASK);
	flush_icache_range(kaddr, kaddr + len);
	kunmap_atomic((void *)kaddr);
}

void update_mmu_cache(struct vm_area_struct *vma, unsigned long addr,
		      pte_t * pte)
{
	struct page *page;
	unsigned long pfn = pte_pfn(*pte);
	unsigned long flags;

	if (!pfn_valid(pfn))
		return;

	if (vma->vm_mm == current->active_mm) {
		local_irq_save(flags);
		__nds32__mtsr_dsb(addr, NDS32_SR_TLB_VPN);
		__nds32__tlbop_rwr(*pte);
		__nds32__isb();
		local_irq_restore(flags);
	}
	page = pfn_to_page(pfn);

	if ((test_and_clear_bit(PG_dcache_dirty, &page->flags)) ||
	    (vma->vm_flags & VM_EXEC)) {
		unsigned long kaddr;
		local_irq_save(flags);
		kaddr = (unsigned long)kmap_atomic(page);
		cpu_cache_wbinval_page(kaddr, vma->vm_flags & VM_EXEC);
		kunmap_atomic((void *)kaddr);
		local_irq_restore(flags);
	}
}
#ifdef CONFIG_CPU_CACHE_ALIASING
extern pte_t va_present(struct mm_struct *mm, unsigned long addr);

static inline unsigned long aliasing(unsigned long addr, unsigned long page)
{
	return ((addr & PAGE_MASK) ^ page) & (SHMLBA - 1);
}

static inline unsigned long kremap0(unsigned long uaddr, unsigned long pa)
{
	unsigned long kaddr, pte;

#define BASE_ADDR0 0xffffc000
	kaddr = BASE_ADDR0 | (uaddr & L1_cache_info[DCACHE].aliasing_mask);
	pte = (pa | PAGE_KERNEL);
	__nds32__mtsr_dsb(kaddr, NDS32_SR_TLB_VPN);
	__nds32__tlbop_rwlk(pte);
	__nds32__isb();
	return kaddr;
}

static inline void kunmap01(unsigned long kaddr)
{
	__nds32__tlbop_unlk(kaddr);
	__nds32__tlbop_inv(kaddr);
	__nds32__isb();
}

static inline unsigned long kremap1(unsigned long uaddr, unsigned long pa)
{
	unsigned long kaddr, pte;

#define BASE_ADDR1 0xffff8000
	kaddr = BASE_ADDR1 | (uaddr & L1_cache_info[DCACHE].aliasing_mask);
	pte = (pa | PAGE_KERNEL);
	__nds32__mtsr_dsb(kaddr, NDS32_SR_TLB_VPN);
	__nds32__tlbop_rwlk(pte);
	__nds32__isb();
	return kaddr;
}

void flush_cache_mm(struct mm_struct *mm)
{
	unsigned long flags;

	local_irq_save(flags);
	cpu_dcache_wbinval_all();
	cpu_icache_inval_all();
	local_irq_restore(flags);
}

void flush_cache_dup_mm(struct mm_struct *mm)
{
}

void flush_cache_range(struct vm_area_struct *vma,
		       unsigned long start, unsigned long end)
{
	unsigned long flags;

	if ((end - start) > 8 * PAGE_SIZE) {
		cpu_dcache_wbinval_all();
		if (vma->vm_flags & VM_EXEC)
			cpu_icache_inval_all();
		return;
	}
	local_irq_save(flags);
	while (start < end) {
		if (va_present(vma->vm_mm, start))
			cpu_cache_wbinval_page(start, vma->vm_flags & VM_EXEC);
		start += PAGE_SIZE;
	}
	local_irq_restore(flags);
	return;
}

void flush_cache_page(struct vm_area_struct *vma,
		      unsigned long addr, unsigned long pfn)
{
	unsigned long vto, flags;

	local_irq_save(flags);
	vto = kremap0(addr, pfn << PAGE_SHIFT);
	cpu_cache_wbinval_page(vto, vma->vm_flags & VM_EXEC);
	kunmap01(vto);
	local_irq_restore(flags);
}

void flush_cache_vmap(unsigned long start, unsigned long end)
{
	cpu_dcache_wbinval_all();
	cpu_icache_inval_all();
}

void flush_cache_vunmap(unsigned long start, unsigned long end)
{
	cpu_dcache_wbinval_all();
	cpu_icache_inval_all();
}

void copy_user_page(void *vto, void *vfrom, unsigned long vaddr,
		    struct page *to)
{
	cpu_dcache_wbinval_page((unsigned long)vaddr);
	cpu_icache_inval_page((unsigned long)vaddr);
	copy_page(vto, vfrom);
	cpu_dcache_wbinval_page((unsigned long)vto);
	cpu_icache_inval_page((unsigned long)vto);
}

void clear_user_page(void *addr, unsigned long vaddr, struct page *page)
{
	cpu_dcache_wbinval_page((unsigned long)vaddr);
	cpu_icache_inval_page((unsigned long)vaddr);
	clear_page(addr);
	cpu_dcache_wbinval_page((unsigned long)addr);
	cpu_icache_inval_page((unsigned long)addr);
}

void copy_user_highpage(struct page *to, struct page *from,
			unsigned long vaddr, struct vm_area_struct *vma)
{
	unsigned long vto, vfrom, flags, kto, kfrom, pfrom, pto;
	kto = ((unsigned long)page_address(to) & PAGE_MASK);
	kfrom = ((unsigned long)page_address(from) & PAGE_MASK);
	pto = page_to_phys(to);
	pfrom = page_to_phys(from);

	local_irq_save(flags);
	if (aliasing(vaddr, (unsigned long)kfrom))
		cpu_dcache_wb_page((unsigned long)kfrom);
	vto = kremap0(vaddr, pto);
	vfrom = kremap1(vaddr, pfrom);
	copy_page((void *)vto, (void *)vfrom);
	kunmap01(vfrom);
	kunmap01(vto);
	local_irq_restore(flags);
}

EXPORT_SYMBOL(copy_user_highpage);

void clear_user_highpage(struct page *page, unsigned long vaddr)
{
	unsigned long vto, flags, kto;

	kto = ((unsigned long)page_address(page) & PAGE_MASK);

	local_irq_save(flags);
	if (aliasing(kto, vaddr) && kto != 0) {
		cpu_dcache_inval_page(kto);
		cpu_icache_inval_page(kto);
	}
	vto = kremap0(vaddr, page_to_phys(page));
	clear_page((void *)vto);
	kunmap01(vto);
	local_irq_restore(flags);
}

EXPORT_SYMBOL(clear_user_highpage);

void flush_dcache_page(struct page *page)
{
	struct address_space *mapping;

	mapping = page_mapping(page);
	if (mapping && !mapping_mapped(mapping))
		set_bit(PG_dcache_dirty, &page->flags);
	else {
		unsigned long kaddr, flags;

		kaddr = (unsigned long)page_address(page);
		local_irq_save(flags);
		cpu_dcache_wbinval_page(kaddr);
		if (mapping) {
			unsigned long vaddr, kto;

			vaddr = page->index << PAGE_SHIFT;
			if (aliasing(vaddr, kaddr)) {
				kto = kremap0(vaddr, page_to_phys(page));
				cpu_dcache_wbinval_page(kto);
				kunmap01(kto);
			}
		}
		local_irq_restore(flags);
	}
}
EXPORT_SYMBOL(flush_dcache_page);

void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
		       unsigned long vaddr, void *dst, void *src, int len)
{
	unsigned long line_size, start, end, vto, flags;

	local_irq_save(flags);
	vto = kremap0(vaddr, page_to_phys(page));
	dst = (void *)(vto | (vaddr & (PAGE_SIZE - 1)));
	memcpy(dst, src, len);
	if (vma->vm_flags & VM_EXEC) {
		line_size = L1_cache_info[DCACHE].line_size;
		start = (unsigned long)dst & ~(line_size - 1);
		end =
		    ((unsigned long)dst + len + line_size - 1) & ~(line_size -
								   1);
		cpu_cache_wbinval_range(start, end, 1);
	}
	kunmap01(vto);
	local_irq_restore(flags);
}

void copy_from_user_page(struct vm_area_struct *vma, struct page *page,
			 unsigned long vaddr, void *dst, void *src, int len)
{
	unsigned long vto, flags;

	local_irq_save(flags);
	vto = kremap0(vaddr, page_to_phys(page));
	src = (void *)(vto | (vaddr & (PAGE_SIZE - 1)));
	memcpy(dst, src, len);
	kunmap01(vto);
	local_irq_restore(flags);
}

void flush_anon_page(struct vm_area_struct *vma,
		     struct page *page, unsigned long vaddr)
{
	unsigned long kaddr, flags, ktmp;
	if (!PageAnon(page))
		return;

	if (vma->vm_mm != current->active_mm)
		return;

	local_irq_save(flags);
	if (vma->vm_flags & VM_EXEC)
		cpu_icache_inval_page(vaddr & PAGE_MASK);
	kaddr = (unsigned long)page_address(page);
	if (aliasing(vaddr, kaddr)) {
		ktmp = kremap0(vaddr, page_to_phys(page));
		cpu_dcache_wbinval_page(ktmp);
		kunmap01(ktmp);
	}
	local_irq_restore(flags);
}

void flush_kernel_dcache_page(struct page *page)
{
	unsigned long flags;
	local_irq_save(flags);
	cpu_dcache_wbinval_page((unsigned long)page_address(page));
	local_irq_restore(flags);
}
EXPORT_SYMBOL(flush_kernel_dcache_page);

void flush_kernel_vmap_range(void *addr, int size)
{
	unsigned long flags;
	local_irq_save(flags);
	cpu_dcache_wb_range((unsigned long)addr, (unsigned long)addr +  size);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(flush_kernel_vmap_range);

void invalidate_kernel_vmap_range(void *addr, int size)
{
	unsigned long flags;
	local_irq_save(flags);
	cpu_dcache_inval_range((unsigned long)addr, (unsigned long)addr + size);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(invalidate_kernel_vmap_range);
#endif
