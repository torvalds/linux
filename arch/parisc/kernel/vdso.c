// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (c) 2022 Helge Deller <deller@gmx.de>
 *
 *  based on arch/s390/kernel/vdso.c which is
 *  Copyright IBM Corp. 2008
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/elf.h>
#include <linux/timekeeper_internal.h>
#include <linux/compat.h>
#include <linux/nsproxy.h>
#include <linux/time_namespace.h>
#include <linux/random.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/sections.h>
#include <asm/vdso.h>
#include <asm/cacheflush.h>

extern char vdso32_start, vdso32_end;
extern char vdso64_start, vdso64_end;

static int vdso_mremap(const struct vm_special_mapping *sm,
		       struct vm_area_struct *vma)
{
	current->mm->context.vdso_base = vma->vm_start;
	return 0;
}

#ifdef CONFIG_64BIT
static struct vm_special_mapping vdso64_mapping = {
	.name = "[vdso]",
	.mremap = vdso_mremap,
};
#endif

static struct vm_special_mapping vdso32_mapping = {
	.name = "[vdso]",
	.mremap = vdso_mremap,
};

/*
 * This is called from binfmt_elf, we create the special vma for the
 * vDSO and insert it into the mm struct tree
 */
int arch_setup_additional_pages(struct linux_binprm *bprm,
				int executable_stack)
{

	unsigned long vdso_text_start, vdso_text_len, map_base;
	struct vm_special_mapping *vdso_mapping;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int rc;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

#ifdef CONFIG_64BIT
	if (!is_compat_task()) {
		vdso_text_len = &vdso64_end - &vdso64_start;
		vdso_mapping = &vdso64_mapping;
	} else
#endif
	{
		vdso_text_len = &vdso32_end - &vdso32_start;
		vdso_mapping = &vdso32_mapping;
	}

	map_base = mm->mmap_base;
	if (current->flags & PF_RANDOMIZE)
		map_base -= (get_random_int() & 0x1f) * PAGE_SIZE;

	vdso_text_start = get_unmapped_area(NULL, map_base, vdso_text_len, 0, 0);

	/* VM_MAYWRITE for COW so gdb can set breakpoints */
	vma = _install_special_mapping(mm, vdso_text_start, vdso_text_len,
				       VM_READ|VM_EXEC|
				       VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
				       vdso_mapping);
	if (IS_ERR(vma)) {
		do_munmap(mm, vdso_text_start, PAGE_SIZE, NULL);
		rc = PTR_ERR(vma);
	} else {
		current->mm->context.vdso_base = vdso_text_start;
		rc = 0;
	}

	mmap_write_unlock(mm);
	return rc;
}

static struct page ** __init vdso_setup_pages(void *start, void *end)
{
	int pages = (end - start) >> PAGE_SHIFT;
	struct page **pagelist;
	int i;

	pagelist = kcalloc(pages + 1, sizeof(struct page *), GFP_KERNEL);
	if (!pagelist)
		panic("%s: Cannot allocate page list for VDSO", __func__);
	for (i = 0; i < pages; i++)
		pagelist[i] = virt_to_page(start + i * PAGE_SIZE);
	return pagelist;
}

static int __init vdso_init(void)
{
#ifdef CONFIG_64BIT
	vdso64_mapping.pages = vdso_setup_pages(&vdso64_start, &vdso64_end);
#endif
	if (IS_ENABLED(CONFIG_COMPAT) || !IS_ENABLED(CONFIG_64BIT))
		vdso32_mapping.pages = vdso_setup_pages(&vdso32_start, &vdso32_end);
	return 0;
}
arch_initcall(vdso_init);
