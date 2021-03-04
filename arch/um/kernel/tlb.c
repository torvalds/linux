// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sched/signal.h>

#include <asm/tlbflush.h>
#include <as-layout.h>
#include <mem_user.h>
#include <os.h>
#include <skas.h>
#include <kern_util.h>

struct host_vm_change {
	struct host_vm_op {
		enum { NONE, MMAP, MUNMAP, MPROTECT } type;
		union {
			struct {
				unsigned long addr;
				unsigned long len;
				unsigned int prot;
				int fd;
				__u64 offset;
			} mmap;
			struct {
				unsigned long addr;
				unsigned long len;
			} munmap;
			struct {
				unsigned long addr;
				unsigned long len;
				unsigned int prot;
			} mprotect;
		} u;
	} ops[1];
	int userspace;
	int index;
	struct mm_struct *mm;
	void *data;
	int force;
};

#define INIT_HVC(mm, force, userspace) \
	((struct host_vm_change) \
	 { .ops		= { { .type = NONE } },	\
	   .mm		= mm, \
       	   .data	= NULL, \
	   .userspace	= userspace, \
	   .index	= 0, \
	   .force	= force })

static void report_enomem(void)
{
	printk(KERN_ERR "UML ran out of memory on the host side! "
			"This can happen due to a memory limitation or "
			"vm.max_map_count has been reached.\n");
}

static int do_ops(struct host_vm_change *hvc, int end,
		  int finished)
{
	struct host_vm_op *op;
	int i, ret = 0;

	for (i = 0; i < end && !ret; i++) {
		op = &hvc->ops[i];
		switch (op->type) {
		case MMAP:
			if (hvc->userspace)
				ret = map(&hvc->mm->context.id, op->u.mmap.addr,
					  op->u.mmap.len, op->u.mmap.prot,
					  op->u.mmap.fd,
					  op->u.mmap.offset, finished,
					  &hvc->data);
			else
				map_memory(op->u.mmap.addr, op->u.mmap.offset,
					   op->u.mmap.len, 1, 1, 1);
			break;
		case MUNMAP:
			if (hvc->userspace)
				ret = unmap(&hvc->mm->context.id,
					    op->u.munmap.addr,
					    op->u.munmap.len, finished,
					    &hvc->data);
			else
				ret = os_unmap_memory(
					(void *) op->u.munmap.addr,
						      op->u.munmap.len);

			break;
		case MPROTECT:
			if (hvc->userspace)
				ret = protect(&hvc->mm->context.id,
					      op->u.mprotect.addr,
					      op->u.mprotect.len,
					      op->u.mprotect.prot,
					      finished, &hvc->data);
			else
				ret = os_protect_memory(
					(void *) op->u.mprotect.addr,
							op->u.mprotect.len,
							1, 1, 1);
			break;
		default:
			printk(KERN_ERR "Unknown op type %d in do_ops\n",
			       op->type);
			BUG();
			break;
		}
	}

	if (ret == -ENOMEM)
		report_enomem();

	return ret;
}

static int add_mmap(unsigned long virt, unsigned long phys, unsigned long len,
		    unsigned int prot, struct host_vm_change *hvc)
{
	__u64 offset;
	struct host_vm_op *last;
	int fd = -1, ret = 0;

	if (virt + len > STUB_START && virt < STUB_END)
		return -EINVAL;

	if (hvc->userspace)
		fd = phys_mapping(phys, &offset);
	else
		offset = phys;
	if (hvc->index != 0) {
		last = &hvc->ops[hvc->index - 1];
		if ((last->type == MMAP) &&
		   (last->u.mmap.addr + last->u.mmap.len == virt) &&
		   (last->u.mmap.prot == prot) && (last->u.mmap.fd == fd) &&
		   (last->u.mmap.offset + last->u.mmap.len == offset)) {
			last->u.mmap.len += len;
			return 0;
		}
	}

	if (hvc->index == ARRAY_SIZE(hvc->ops)) {
		ret = do_ops(hvc, ARRAY_SIZE(hvc->ops), 0);
		hvc->index = 0;
	}

	hvc->ops[hvc->index++] = ((struct host_vm_op)
				  { .type	= MMAP,
				    .u = { .mmap = { .addr	= virt,
						     .len	= len,
						     .prot	= prot,
						     .fd	= fd,
						     .offset	= offset }
			   } });
	return ret;
}

static int add_munmap(unsigned long addr, unsigned long len,
		      struct host_vm_change *hvc)
{
	struct host_vm_op *last;
	int ret = 0;

	if (addr + len > STUB_START && addr < STUB_END)
		return -EINVAL;

	if (hvc->index != 0) {
		last = &hvc->ops[hvc->index - 1];
		if ((last->type == MUNMAP) &&
		   (last->u.munmap.addr + last->u.mmap.len == addr)) {
			last->u.munmap.len += len;
			return 0;
		}
	}

	if (hvc->index == ARRAY_SIZE(hvc->ops)) {
		ret = do_ops(hvc, ARRAY_SIZE(hvc->ops), 0);
		hvc->index = 0;
	}

	hvc->ops[hvc->index++] = ((struct host_vm_op)
				  { .type	= MUNMAP,
			     	    .u = { .munmap = { .addr	= addr,
						       .len	= len } } });
	return ret;
}

static int add_mprotect(unsigned long addr, unsigned long len,
			unsigned int prot, struct host_vm_change *hvc)
{
	struct host_vm_op *last;
	int ret = 0;

	if (addr + len > STUB_START && addr < STUB_END)
		return -EINVAL;

	if (hvc->index != 0) {
		last = &hvc->ops[hvc->index - 1];
		if ((last->type == MPROTECT) &&
		   (last->u.mprotect.addr + last->u.mprotect.len == addr) &&
		   (last->u.mprotect.prot == prot)) {
			last->u.mprotect.len += len;
			return 0;
		}
	}

	if (hvc->index == ARRAY_SIZE(hvc->ops)) {
		ret = do_ops(hvc, ARRAY_SIZE(hvc->ops), 0);
		hvc->index = 0;
	}

	hvc->ops[hvc->index++] = ((struct host_vm_op)
				  { .type	= MPROTECT,
			     	    .u = { .mprotect = { .addr	= addr,
							 .len	= len,
							 .prot	= prot } } });
	return ret;
}

#define ADD_ROUND(n, inc) (((n) + (inc)) & ~((inc) - 1))

static inline int update_pte_range(pmd_t *pmd, unsigned long addr,
				   unsigned long end,
				   struct host_vm_change *hvc)
{
	pte_t *pte;
	int r, w, x, prot, ret = 0;

	pte = pte_offset_kernel(pmd, addr);
	do {
		if ((addr >= STUB_START) && (addr < STUB_END))
			continue;

		r = pte_read(*pte);
		w = pte_write(*pte);
		x = pte_exec(*pte);
		if (!pte_young(*pte)) {
			r = 0;
			w = 0;
		} else if (!pte_dirty(*pte))
			w = 0;

		prot = ((r ? UM_PROT_READ : 0) | (w ? UM_PROT_WRITE : 0) |
			(x ? UM_PROT_EXEC : 0));
		if (hvc->force || pte_newpage(*pte)) {
			if (pte_present(*pte)) {
				if (pte_newpage(*pte))
					ret = add_mmap(addr, pte_val(*pte) & PAGE_MASK,
						       PAGE_SIZE, prot, hvc);
			} else
				ret = add_munmap(addr, PAGE_SIZE, hvc);
		} else if (pte_newprot(*pte))
			ret = add_mprotect(addr, PAGE_SIZE, prot, hvc);
		*pte = pte_mkuptodate(*pte);
	} while (pte++, addr += PAGE_SIZE, ((addr < end) && !ret));
	return ret;
}

static inline int update_pmd_range(pud_t *pud, unsigned long addr,
				   unsigned long end,
				   struct host_vm_change *hvc)
{
	pmd_t *pmd;
	unsigned long next;
	int ret = 0;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (!pmd_present(*pmd)) {
			if (hvc->force || pmd_newpage(*pmd)) {
				ret = add_munmap(addr, next - addr, hvc);
				pmd_mkuptodate(*pmd);
			}
		}
		else ret = update_pte_range(pmd, addr, next, hvc);
	} while (pmd++, addr = next, ((addr < end) && !ret));
	return ret;
}

static inline int update_pud_range(p4d_t *p4d, unsigned long addr,
				   unsigned long end,
				   struct host_vm_change *hvc)
{
	pud_t *pud;
	unsigned long next;
	int ret = 0;

	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (!pud_present(*pud)) {
			if (hvc->force || pud_newpage(*pud)) {
				ret = add_munmap(addr, next - addr, hvc);
				pud_mkuptodate(*pud);
			}
		}
		else ret = update_pmd_range(pud, addr, next, hvc);
	} while (pud++, addr = next, ((addr < end) && !ret));
	return ret;
}

static inline int update_p4d_range(pgd_t *pgd, unsigned long addr,
				   unsigned long end,
				   struct host_vm_change *hvc)
{
	p4d_t *p4d;
	unsigned long next;
	int ret = 0;

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (!p4d_present(*p4d)) {
			if (hvc->force || p4d_newpage(*p4d)) {
				ret = add_munmap(addr, next - addr, hvc);
				p4d_mkuptodate(*p4d);
			}
		} else
			ret = update_pud_range(p4d, addr, next, hvc);
	} while (p4d++, addr = next, ((addr < end) && !ret));
	return ret;
}

void fix_range_common(struct mm_struct *mm, unsigned long start_addr,
		      unsigned long end_addr, int force)
{
	pgd_t *pgd;
	struct host_vm_change hvc;
	unsigned long addr = start_addr, next;
	int ret = 0, userspace = 1;

	hvc = INIT_HVC(mm, force, userspace);
	pgd = pgd_offset(mm, addr);
	do {
		next = pgd_addr_end(addr, end_addr);
		if (!pgd_present(*pgd)) {
			if (force || pgd_newpage(*pgd)) {
				ret = add_munmap(addr, next - addr, &hvc);
				pgd_mkuptodate(*pgd);
			}
		} else
			ret = update_p4d_range(pgd, addr, next, &hvc);
	} while (pgd++, addr = next, ((addr < end_addr) && !ret));

	if (!ret)
		ret = do_ops(&hvc, hvc.index, 1);

	/* This is not an else because ret is modified above */
	if (ret) {
		struct mm_id *mm_idp = &current->mm->context.id;

		printk(KERN_ERR "fix_range_common: failed, killing current "
		       "process: %d\n", task_tgid_vnr(current));
		mm_idp->kill = 1;
	}
}

static int flush_tlb_kernel_range_common(unsigned long start, unsigned long end)
{
	struct mm_struct *mm;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	unsigned long addr, last;
	int updated = 0, err = 0, force = 0, userspace = 0;
	struct host_vm_change hvc;

	mm = &init_mm;
	hvc = INIT_HVC(mm, force, userspace);
	for (addr = start; addr < end;) {
		pgd = pgd_offset(mm, addr);
		if (!pgd_present(*pgd)) {
			last = ADD_ROUND(addr, PGDIR_SIZE);
			if (last > end)
				last = end;
			if (pgd_newpage(*pgd)) {
				updated = 1;
				err = add_munmap(addr, last - addr, &hvc);
				if (err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
			}
			addr = last;
			continue;
		}

		p4d = p4d_offset(pgd, addr);
		if (!p4d_present(*p4d)) {
			last = ADD_ROUND(addr, P4D_SIZE);
			if (last > end)
				last = end;
			if (p4d_newpage(*p4d)) {
				updated = 1;
				err = add_munmap(addr, last - addr, &hvc);
				if (err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
			}
			addr = last;
			continue;
		}

		pud = pud_offset(p4d, addr);
		if (!pud_present(*pud)) {
			last = ADD_ROUND(addr, PUD_SIZE);
			if (last > end)
				last = end;
			if (pud_newpage(*pud)) {
				updated = 1;
				err = add_munmap(addr, last - addr, &hvc);
				if (err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
			}
			addr = last;
			continue;
		}

		pmd = pmd_offset(pud, addr);
		if (!pmd_present(*pmd)) {
			last = ADD_ROUND(addr, PMD_SIZE);
			if (last > end)
				last = end;
			if (pmd_newpage(*pmd)) {
				updated = 1;
				err = add_munmap(addr, last - addr, &hvc);
				if (err < 0)
					panic("munmap failed, errno = %d\n",
					      -err);
			}
			addr = last;
			continue;
		}

		pte = pte_offset_kernel(pmd, addr);
		if (!pte_present(*pte) || pte_newpage(*pte)) {
			updated = 1;
			err = add_munmap(addr, PAGE_SIZE, &hvc);
			if (err < 0)
				panic("munmap failed, errno = %d\n",
				      -err);
			if (pte_present(*pte))
				err = add_mmap(addr, pte_val(*pte) & PAGE_MASK,
					       PAGE_SIZE, 0, &hvc);
		}
		else if (pte_newprot(*pte)) {
			updated = 1;
			err = add_mprotect(addr, PAGE_SIZE, 0, &hvc);
		}
		addr += PAGE_SIZE;
	}
	if (!err)
		err = do_ops(&hvc, hvc.index, 1);

	if (err < 0)
		panic("flush_tlb_kernel failed, errno = %d\n", err);
	return updated;
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long address)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	struct mm_struct *mm = vma->vm_mm;
	void *flush = NULL;
	int r, w, x, prot, err = 0;
	struct mm_id *mm_id;

	address &= PAGE_MASK;

	if (address >= STUB_START && address < STUB_END)
		goto kill;

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto kill;

	p4d = p4d_offset(pgd, address);
	if (!p4d_present(*p4d))
		goto kill;

	pud = pud_offset(p4d, address);
	if (!pud_present(*pud))
		goto kill;

	pmd = pmd_offset(pud, address);
	if (!pmd_present(*pmd))
		goto kill;

	pte = pte_offset_kernel(pmd, address);

	r = pte_read(*pte);
	w = pte_write(*pte);
	x = pte_exec(*pte);
	if (!pte_young(*pte)) {
		r = 0;
		w = 0;
	} else if (!pte_dirty(*pte)) {
		w = 0;
	}

	mm_id = &mm->context.id;
	prot = ((r ? UM_PROT_READ : 0) | (w ? UM_PROT_WRITE : 0) |
		(x ? UM_PROT_EXEC : 0));
	if (pte_newpage(*pte)) {
		if (pte_present(*pte)) {
			unsigned long long offset;
			int fd;

			fd = phys_mapping(pte_val(*pte) & PAGE_MASK, &offset);
			err = map(mm_id, address, PAGE_SIZE, prot, fd, offset,
				  1, &flush);
		}
		else err = unmap(mm_id, address, PAGE_SIZE, 1, &flush);
	}
	else if (pte_newprot(*pte))
		err = protect(mm_id, address, PAGE_SIZE, prot, 1, &flush);

	if (err) {
		if (err == -ENOMEM)
			report_enomem();

		goto kill;
	}

	*pte = pte_mkuptodate(*pte);

	return;

kill:
	printk(KERN_ERR "Failed to flush page for address 0x%lx\n", address);
	force_sig(SIGKILL);
}

void flush_tlb_all(void)
{
	/*
	 * Don't bother flushing if this address space is about to be
	 * destroyed.
	 */
	if (atomic_read(&current->mm->mm_users) == 0)
		return;

	flush_tlb_mm(current->mm);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	flush_tlb_kernel_range_common(start, end);
}

void flush_tlb_kernel_vm(void)
{
	flush_tlb_kernel_range_common(start_vm, end_vm);
}

void __flush_tlb_one(unsigned long addr)
{
	flush_tlb_kernel_range_common(addr, addr + PAGE_SIZE);
}

static void fix_range(struct mm_struct *mm, unsigned long start_addr,
		      unsigned long end_addr, int force)
{
	/*
	 * Don't bother flushing if this address space is about to be
	 * destroyed.
	 */
	if (atomic_read(&mm->mm_users) == 0)
		return;

	fix_range_common(mm, start_addr, end_addr, force);
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	if (vma->vm_mm == NULL)
		flush_tlb_kernel_range_common(start, end);
	else fix_range(vma->vm_mm, start, end, 0);
}
EXPORT_SYMBOL(flush_tlb_range);

void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
			unsigned long end)
{
	fix_range(mm, start, end, 0);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	struct vm_area_struct *vma = mm->mmap;

	while (vma != NULL) {
		fix_range(mm, vma->vm_start, vma->vm_end, 0);
		vma = vma->vm_next;
	}
}

void force_flush_all(void)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma = mm->mmap;

	while (vma != NULL) {
		fix_range(mm, vma->vm_start, vma->vm_end, 1);
		vma = vma->vm_next;
	}
}
