/*
 * Set up the VMAs to tell the VM about the vDSO.
 * Copyright 2007 Andi Kleen, SUSE Labs.
 * Subject to the GPL, v.2
 */
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/random.h>
#include <asm/vsyscall.h>
#include <asm/vgtod.h>
#include <asm/proto.h>
#include "voffset.h"

int vdso_enabled = 1;

#define VEXTERN(x) extern typeof(__ ## x) *vdso_ ## x;
#include "vextern.h"
#undef VEXTERN

extern char vdso_kernel_start[], vdso_start[], vdso_end[];
extern unsigned short vdso_sync_cpuid;

struct page **vdso_pages;

static inline void *var_ref(void *vbase, char *var, char *name)
{
	unsigned offset = var - &vdso_kernel_start[0] + VDSO_TEXT_OFFSET;
	void *p = vbase + offset;
	if (*(void **)p != (void *)VMAGIC) {
		printk("VDSO: variable %s broken\n", name);
		vdso_enabled = 0;
	}
	return p;
}

static int __init init_vdso_vars(void)
{
	int npages = (vdso_end - vdso_start + PAGE_SIZE - 1) / PAGE_SIZE;
	int i;
	char *vbase;

	vdso_pages = kmalloc(sizeof(struct page *) * npages, GFP_KERNEL);
	if (!vdso_pages)
		goto oom;
	for (i = 0; i < npages; i++) {
		struct page *p;
		p = alloc_page(GFP_KERNEL);
		if (!p)
			goto oom;
		vdso_pages[i] = p;
		copy_page(page_address(p), vdso_start + i*PAGE_SIZE);
	}

	vbase = vmap(vdso_pages, npages, 0, PAGE_KERNEL);
	if (!vbase)
		goto oom;

	if (memcmp(vbase, "\177ELF", 4)) {
		printk("VDSO: I'm broken; not ELF\n");
		vdso_enabled = 0;
	}

#define V(x) *(typeof(x) *) var_ref(vbase, (char *)RELOC_HIDE(&x, 0), #x)
#define VEXTERN(x) \
	V(vdso_ ## x) = &__ ## x;
#include "vextern.h"
#undef VEXTERN
	return 0;

 oom:
	printk("Cannot allocate vdso\n");
	vdso_enabled = 0;
	return -ENOMEM;
}
__initcall(init_vdso_vars);

struct linux_binprm;

/* Put the vdso above the (randomized) stack with another randomized offset.
   This way there is no hole in the middle of address space.
   To save memory make sure it is still in the same PTE as the stack top.
   This doesn't give that many random bits */
static unsigned long vdso_addr(unsigned long start, unsigned len)
{
	unsigned long addr, end;
	unsigned offset;
	end = (start + PMD_SIZE - 1) & PMD_MASK;
	if (end >= TASK_SIZE64)
		end = TASK_SIZE64;
	end -= len;
	/* This loses some more bits than a modulo, but is cheaper */
	offset = get_random_int() & (PTRS_PER_PTE - 1);
	addr = start + (offset << PAGE_SHIFT);
	if (addr >= end)
		addr = end;
	return addr;
}

/* Setup a VMA at program startup for the vsyscall page.
   Not called for compat tasks */
int arch_setup_additional_pages(struct linux_binprm *bprm, int exstack)
{
	struct mm_struct *mm = current->mm;
	unsigned long addr;
	int ret;
	unsigned len = round_up(vdso_end - vdso_start, PAGE_SIZE);

	if (!vdso_enabled)
		return 0;

	down_write(&mm->mmap_sem);
	addr = vdso_addr(mm->start_stack, len);
	addr = get_unmapped_area(NULL, addr, len, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	ret = install_special_mapping(mm, addr, len,
				      VM_READ|VM_EXEC|
				      VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC|
				      VM_ALWAYSDUMP,
				      vdso_pages);
	if (ret)
		goto up_fail;

	current->mm->context.vdso = (void *)addr;
up_fail:
	up_write(&mm->mmap_sem);
	return ret;
}

static __init int vdso_setup(char *s)
{
	vdso_enabled = simple_strtoul(s, NULL, 0);
	return 0;
}
__setup("vdso=", vdso_setup);
