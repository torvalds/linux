/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include "linux/mm.h"
#include "asm/page.h"
#include "asm/pgalloc.h"
#include "asm/pgtable.h"
#include "asm/tlbflush.h"
#include "choose-mode.h"
#include "mode_kern.h"
#include "as-layout.h"
#include "tlb.h"
#include "mem.h"
#include "mem_user.h"
#include "os.h"

static int add_mmap(unsigned long virt, unsigned long phys, unsigned long len,
		    int r, int w, int x, struct host_vm_op *ops, int *index,
		    int last_filled, union mm_context *mmu, void **flush,
		    int (*do_ops)(union mm_context *, struct host_vm_op *,
				  int, int, void **))
{
	__u64 offset;
	struct host_vm_op *last;
	int fd, ret = 0;

	fd = phys_mapping(phys, &offset);
	if(*index != -1){
		last = &ops[*index];
		if((last->type == MMAP) &&
		   (last->u.mmap.addr + last->u.mmap.len == virt) &&
		   (last->u.mmap.r == r) && (last->u.mmap.w == w) &&
		   (last->u.mmap.x == x) && (last->u.mmap.fd == fd) &&
		   (last->u.mmap.offset + last->u.mmap.len == offset)){
			last->u.mmap.len += len;
			return 0;
		}
	}

	if(*index == last_filled){
		ret = (*do_ops)(mmu, ops, last_filled, 0, flush);
		*index = -1;
	}

	ops[++*index] = ((struct host_vm_op) { .type	= MMAP,
			     			.u = { .mmap = {
						       .addr	= virt,
						       .len	= len,
						       .r	= r,
						       .w	= w,
						       .x	= x,
						       .fd	= fd,
						       .offset	= offset }
			   } });
	return ret;
}

static int add_munmap(unsigned long addr, unsigned long len,
		      struct host_vm_op *ops, int *index, int last_filled,
		      union mm_context *mmu, void **flush,
		      int (*do_ops)(union mm_context *, struct host_vm_op *,
				    int, int, void **))
{
	struct host_vm_op *last;
	int ret = 0;

	if(*index != -1){
		last = &ops[*index];
		if((last->type == MUNMAP) &&
		   (last->u.munmap.addr + last->u.mmap.len == addr)){
			last->u.munmap.len += len;
			return 0;
		}
	}

	if(*index == last_filled){
		ret = (*do_ops)(mmu, ops, last_filled, 0, flush);
		*index = -1;
	}

	ops[++*index] = ((struct host_vm_op) { .type	= MUNMAP,
			     		       .u = { .munmap = {
						        .addr	= addr,
							.len	= len } } });
	return ret;
}

static int add_mprotect(unsigned long addr, unsigned long len, int r, int w,
			int x, struct host_vm_op *ops, int *index,
			int last_filled, union mm_context *mmu, void **flush,
			int (*do_ops)(union mm_context *, struct host_vm_op *,
				      int, int, void **))
{
	struct host_vm_op *last;
	int ret = 0;

	if(*index != -1){
		last = &ops[*index];
		if((last->type == MPROTECT) &&
		   (last->u.mprotect.addr + last->u.mprotect.len == addr) &&
		   (last->u.mprotect.r == r) && (last->u.mprotect.w == w) &&
		   (last->u.mprotect.x == x)){
			last->u.mprotect.len += len;
			return 0;
		}
	}

	if(*index == last_filled){
		ret = (*do_ops)(mmu, ops, last_filled, 0, flush);
		*index = -1;
	}

	ops[++*index] = ((struct host_vm_op) { .type	= MPROTECT,
			     		       .u = { .mprotect = {
						       .addr	= addr,
						       .len	= len,
						       .r	= r,
						       .w	= w,
						       .x	= x } } });
	return ret;
}

#define ADD_ROUND(n, inc) (((n) + (inc)) & ~((inc) - 1))

static inline int update_pte_range(pmd_t *pmd, unsigned long addr,
				   unsigned long end, struct host_vm_op *ops,
				   int last_op, int *op_index, int force,
				   union mm_context *mmu, void **flush,
				   int (*do_ops)(union mm_context *,
						 struct host_vm_op *, int, int,
						 void **))
{
	pte_t *pte;
	int r, w, x, ret = 0;

	pte = pte_offset_kernel(pmd, addr);
	do {
		r = pte_read(*pte);
		w = pte_write(*pte);
		x = pte_exec(*pte);
		if (!pte_young(*pte)) {
			r = 0;
			w = 0;
		} else if (!pte_dirty(*pte)) {
			w = 0;
		}
		if(force || pte_newpage(*pte)){
			if(pte_present(*pte))
				ret = add_mmap(addr, pte_val(*pte) & PAGE_MASK,
					       PAGE_SIZE, r, w, x, ops,
					       op_index, last_op, mmu, flush,
					       do_ops);
			else ret = add_munmap(addr, PAGE_SIZE, ops, op_index,
					      last_op, mmu, flush, do_ops);
		}
		else if(pte_newprot(*pte))
			ret = add_mprotect(addr, PAGE_SIZE, r, w, x, ops,
					   op_index, last_op, mmu, flush,
					   do_ops);
		*pte = pte_mkuptodate(*pte);
	} while (pte++, addr += PAGE_SIZE, ((addr != end) && !ret));
	return ret;
}

static inline int update_pmd_range(pud_t *pud, unsigned long addr,
				   unsigned long end, struct host_vm_op *ops,
				   int last_op, int *op_index, int force,
				   union mm_context *mmu, void **flush,
				   int (*do_ops)(union mm_context *,
						 struct host_vm_op *, int, int,
						 void **))
{
	pmd_t *pmd;
	unsigned long next;
	int ret = 0;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if(!pmd_present(*pmd)){
			if(force || pmd_newpage(*pmd)){
				ret = add_munmap(addr, next - addr, ops,
						 op_index, last_op, mmu,
						 flush, do_ops);
				pmd_mkuptodate(*pmd);
			}
		}
		else ret = update_pte_range(pmd, addr, next, ops, last_op,
					    op_index, force, mmu, flush,
					    do_ops);
	} while (pmd++, addr = next, ((addr != end) && !ret));
	return ret;
}

static inline int update_pud_range(pgd_t *pgd, unsigned long addr,
				   unsigned long end, struct host_vm_op *ops,
				   int last_op, int *op_index, int force,
				   union mm_context *mmu, void **flush,
				   int (*do_ops)(union mm_context *,
						 struct host_vm_op *, int, int,
						 void **))
{
	pud_t *pud;
	unsigned long next;
	int ret = 0;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if(!pud_present(*pud)){
			if(force || pud_newpage(*pud)){
				ret = add_munmap(addr, next - addr, ops,
						 op_index, last_op, mmu,
						 flush, do_ops);
				pud_mkuptodate(*pud);
			}
		}
		else ret = update_pmd_range(pud, addr, next, ops, last_op,
					    op_index, force, mmu, flush,
					    do_ops);
	} while (pud++, addr = next, ((addr != end) && !ret));
	return ret;
}

void fix_range_common(struct mm_struct *mm, unsigned long start_addr,
		      unsigned long end_addr, int force,
		      int (*do_ops)(union mm_context *, struct host_vm_op *,
				    int, int, void **))
{
	pgd_t *pgd;
	union mm_context *mmu = &mm->context;
	struct host_vm_op ops[1];
	unsigned long addr = start_addr, next;
	int ret = 0, last_op = ARRAY_SIZE(ops) - 1, op_index = -1;
	void *flush = NULL;

	ops[0].type = NONE;
	pgd = pgd_offset(mm, addr);
	do {
		next = pgd_addr_end(addr, end_addr);
		if(!pgd_present(*pgd)){
			if (force || pgd_newpage(*pgd)){
				ret = add_munmap(addr, next - addr, ops,
						 &op_index, last_op, mmu,
						 &flush, do_ops);
				pgd_mkuptodate(*pgd);
			}
		}
		else ret = update_pud_range(pgd, addr, next, ops, last_op,
					    &op_index, force, mmu, &flush,
					    do_ops);
	} while (pgd++, addr = next, ((addr != end_addr) && !ret));
	log_info("total flush time - %Ld nsecs\n", end_time - start_time);

	if(!ret)
		ret = (*do_ops)(mmu, ops, op_index, 1, &flush);

	/* This is not an else because ret is modified above */
	if(ret) {
		printk("fix_range_common: failed, killing current process\n");
		force_sig(SIGKILL, current);
	}
}

int flush_tlb_kernel_range_common(unsigned long start, unsigned long end)
{
	struct mm_struct *mm;
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long addr, last;
	int updated = 0, err;

	mm = &init_mm;
	for(addr = start; addr < end;){
		pgd = pgd_offset(mm, addr);
		if(!pgd_present(*pgd)){
			last = ADD_ROUND(addr, PGDIR_SIZE);
			if(last > end)
				last = end;
			if(pgd_newpage(*pgd)){
				updated = 1;
				err = os_unmap_memory((void *) addr,
						      last - addr);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
			}
			addr = last;
			continue;
		}

		pud = pud_offset(pgd, addr);
		if(!pud_present(*pud)){
			last = ADD_ROUND(addr, PUD_SIZE);
			if(last > end)
				last = end;
			if(pud_newpage(*pud)){
				updated = 1;
				err = os_unmap_memory((void *) addr,
						      last - addr);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
			}
			addr = last;
			continue;
		}

		pmd = pmd_offset(pud, addr);
		if(!pmd_present(*pmd)){
			last = ADD_ROUND(addr, PMD_SIZE);
			if(last > end)
				last = end;
			if(pmd_newpage(*pmd)){
				updated = 1;
				err = os_unmap_memory((void *) addr,
						      last - addr);
				if(err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
			}
			addr = last;
			continue;
		}

		pte = pte_offset_kernel(pmd, addr);
		if(!pte_present(*pte) || pte_newpage(*pte)){
			updated = 1;
			err = os_unmap_memory((void *) addr,
					      PAGE_SIZE);
			if(err < 0)
				panic("munmap failed, errno = %d\n",
				      -err);
			if(pte_present(*pte))
				map_memory(addr,
					   pte_val(*pte) & PAGE_MASK,
					   PAGE_SIZE, 1, 1, 1);
		}
		else if(pte_newprot(*pte)){
			updated = 1;
			os_protect_memory((void *) addr, PAGE_SIZE, 1, 1, 1);
		}
		addr += PAGE_SIZE;
	}
	return(updated);
}

pgd_t *pgd_offset_proc(struct mm_struct *mm, unsigned long address)
{
	return(pgd_offset(mm, address));
}

pud_t *pud_offset_proc(pgd_t *pgd, unsigned long address)
{
	return(pud_offset(pgd, address));
}

pmd_t *pmd_offset_proc(pud_t *pud, unsigned long address)
{
	return(pmd_offset(pud, address));
}

pte_t *pte_offset_proc(pmd_t *pmd, unsigned long address)
{
	return(pte_offset_kernel(pmd, address));
}

pte_t *addr_pte(struct task_struct *task, unsigned long addr)
{
	pgd_t *pgd = pgd_offset(task->mm, addr);
	pud_t *pud = pud_offset(pgd, addr);
	pmd_t *pmd = pmd_offset(pud, addr);

	return(pte_offset_map(pmd, addr));
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long address)
{
	address &= PAGE_MASK;

	CHOOSE_MODE(flush_tlb_range(vma, address, address + PAGE_SIZE),
		    flush_tlb_page_skas(vma, address));
}

void flush_tlb_all(void)
{
	flush_tlb_mm(current->mm);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	CHOOSE_MODE_PROC(flush_tlb_kernel_range_tt,
			 flush_tlb_kernel_range_common, start, end);
}

void flush_tlb_kernel_vm(void)
{
	CHOOSE_MODE(flush_tlb_kernel_vm_tt(),
		    flush_tlb_kernel_range_common(start_vm, end_vm));
}

void __flush_tlb_one(unsigned long addr)
{
	CHOOSE_MODE_PROC(__flush_tlb_one_tt, __flush_tlb_one_skas, addr);
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	CHOOSE_MODE_PROC(flush_tlb_range_tt, flush_tlb_range_skas, vma, start,
			 end);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	CHOOSE_MODE_PROC(flush_tlb_mm_tt, flush_tlb_mm_skas, mm);
}

void force_flush_all(void)
{
	CHOOSE_MODE(force_flush_all_tt(), force_flush_all_skas());
}

