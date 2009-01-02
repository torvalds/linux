/*
 * vdso setup for s390
 *
 *  Copyright IBM Corp. 2008
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/elf.h>
#include <linux/security.h>
#include <linux/bootmem.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/sections.h>
#include <asm/vdso.h>

/* Max supported size for symbol names */
#define MAX_SYMNAME	64

#if defined(CONFIG_32BIT) || defined(CONFIG_COMPAT)
extern char vdso32_start, vdso32_end;
static void *vdso32_kbase = &vdso32_start;
static unsigned int vdso32_pages;
static struct page **vdso32_pagelist;
#endif

#ifdef CONFIG_64BIT
extern char vdso64_start, vdso64_end;
static void *vdso64_kbase = &vdso64_start;
static unsigned int vdso64_pages;
static struct page **vdso64_pagelist;
#endif /* CONFIG_64BIT */

/*
 * Should the kernel map a VDSO page into processes and pass its
 * address down to glibc upon exec()?
 */
unsigned int __read_mostly vdso_enabled = 1;

static int __init vdso_setup(char *s)
{
	vdso_enabled = simple_strtoul(s, NULL, 0);
	return 1;
}
__setup("vdso=", vdso_setup);

/*
 * The vdso data page
 */
static union {
	struct vdso_data	data;
	u8			page[PAGE_SIZE];
} vdso_data_store __attribute__((__section__(".data.page_aligned")));
struct vdso_data *vdso_data = &vdso_data_store.data;

/*
 * This is called from binfmt_elf, we create the special vma for the
 * vDSO and insert it into the mm struct tree
 */
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	struct page **vdso_pagelist;
	unsigned long vdso_pages;
	unsigned long vdso_base;
	int rc;

	if (!vdso_enabled)
		return 0;
	/*
	 * Only map the vdso for dynamically linked elf binaries.
	 */
	if (!uses_interp)
		return 0;

	vdso_base = mm->mmap_base;
#ifdef CONFIG_64BIT
	vdso_pagelist = vdso64_pagelist;
	vdso_pages = vdso64_pages;
#ifdef CONFIG_COMPAT
	if (test_thread_flag(TIF_31BIT)) {
		vdso_pagelist = vdso32_pagelist;
		vdso_pages = vdso32_pages;
	}
#endif
#else
	vdso_pagelist = vdso32_pagelist;
	vdso_pages = vdso32_pages;
#endif

	/*
	 * vDSO has a problem and was disabled, just don't "enable" it for
	 * the process
	 */
	if (vdso_pages == 0)
		return 0;

	current->mm->context.vdso_base = 0;

	/*
	 * pick a base address for the vDSO in process space. We try to put
	 * it at vdso_base which is the "natural" base for it, but we might
	 * fail and end up putting it elsewhere.
	 */
	down_write(&mm->mmap_sem);
	vdso_base = get_unmapped_area(NULL, vdso_base,
				      vdso_pages << PAGE_SHIFT, 0, 0);
	if (IS_ERR_VALUE(vdso_base)) {
		rc = vdso_base;
		goto out_up;
	}

	/*
	 * our vma flags don't have VM_WRITE so by default, the process
	 * isn't allowed to write those pages.
	 * gdb can break that with ptrace interface, and thus trigger COW
	 * on those pages but it's then your responsibility to never do that
	 * on the "data" page of the vDSO or you'll stop getting kernel
	 * updates and your nice userland gettimeofday will be totally dead.
	 * It's fine to use that for setting breakpoints in the vDSO code
	 * pages though
	 *
	 * Make sure the vDSO gets into every core dump.
	 * Dumping its contents makes post-mortem fully interpretable later
	 * without matching up the same kernel and hardware config to see
	 * what PC values meant.
	 */
	rc = install_special_mapping(mm, vdso_base, vdso_pages << PAGE_SHIFT,
				     VM_READ|VM_EXEC|
				     VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC|
				     VM_ALWAYSDUMP,
				     vdso_pagelist);
	if (rc)
		goto out_up;

	/* Put vDSO base into mm struct */
	current->mm->context.vdso_base = vdso_base;

	up_write(&mm->mmap_sem);
	return 0;

out_up:
	up_write(&mm->mmap_sem);
	return rc;
}

const char *arch_vma_name(struct vm_area_struct *vma)
{
	if (vma->vm_mm && vma->vm_start == vma->vm_mm->context.vdso_base)
		return "[vdso]";
	return NULL;
}

static int __init vdso_init(void)
{
	int i;

#if defined(CONFIG_32BIT) || defined(CONFIG_COMPAT)
	/* Calculate the size of the 32 bit vDSO */
	vdso32_pages = ((&vdso32_end - &vdso32_start
			 + PAGE_SIZE - 1) >> PAGE_SHIFT) + 1;

	/* Make sure pages are in the correct state */
	vdso32_pagelist = kzalloc(sizeof(struct page *) * (vdso32_pages + 1),
				  GFP_KERNEL);
	BUG_ON(vdso32_pagelist == NULL);
	for (i = 0; i < vdso32_pages - 1; i++) {
		struct page *pg = virt_to_page(vdso32_kbase + i*PAGE_SIZE);
		ClearPageReserved(pg);
		get_page(pg);
		vdso32_pagelist[i] = pg;
	}
	vdso32_pagelist[vdso32_pages - 1] = virt_to_page(vdso_data);
	vdso32_pagelist[vdso32_pages] = NULL;
#endif

#ifdef CONFIG_64BIT
	/* Calculate the size of the 64 bit vDSO */
	vdso64_pages = ((&vdso64_end - &vdso64_start
			 + PAGE_SIZE - 1) >> PAGE_SHIFT) + 1;

	/* Make sure pages are in the correct state */
	vdso64_pagelist = kzalloc(sizeof(struct page *) * (vdso64_pages + 1),
				  GFP_KERNEL);
	BUG_ON(vdso64_pagelist == NULL);
	for (i = 0; i < vdso64_pages - 1; i++) {
		struct page *pg = virt_to_page(vdso64_kbase + i*PAGE_SIZE);
		ClearPageReserved(pg);
		get_page(pg);
		vdso64_pagelist[i] = pg;
	}
	vdso64_pagelist[vdso64_pages - 1] = virt_to_page(vdso_data);
	vdso64_pagelist[vdso64_pages] = NULL;
#endif /* CONFIG_64BIT */

	get_page(virt_to_page(vdso_data));

	smp_wmb();

	return 0;
}
arch_initcall(vdso_init);

int in_gate_area_no_task(unsigned long addr)
{
	return 0;
}

int in_gate_area(struct task_struct *task, unsigned long addr)
{
	return 0;
}

struct vm_area_struct *get_gate_vma(struct task_struct *tsk)
{
	return NULL;
}
