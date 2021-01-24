// SPDX-License-Identifier: GPL-2.0
/*
 * vdso setup for s390
 *
 *  Copyright IBM Corp. 2008
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <linux/binfmts.h>
#include <linux/compat.h>
#include <linux/elf.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <vdso/datapage.h>
#include <asm/vdso.h>

extern char vdso64_start[], vdso64_end[];
static unsigned int vdso_pages;

static union {
	struct vdso_data	data[CS_BASES];
	u8			page[PAGE_SIZE];
} vdso_data_store __page_aligned_data;

struct vdso_data *vdso_data = vdso_data_store.data;

unsigned int __read_mostly vdso_enabled = 1;

static int __init vdso_setup(char *str)
{
	bool enabled;

	if (!kstrtobool(str, &enabled))
		vdso_enabled = enabled;
	return 1;
}
__setup("vdso=", vdso_setup);

static vm_fault_t vvar_fault(const struct vm_special_mapping *sm,
			     struct vm_area_struct *vma, struct vm_fault *vmf)
{
	if (vmf->pgoff == 0)
		return vmf_insert_pfn(vma, vmf->address, virt_to_pfn(vdso_data));
	return VM_FAULT_SIGBUS;
}

static int vdso_mremap(const struct vm_special_mapping *sm,
		       struct vm_area_struct *vma)
{
	current->mm->context.vdso_base = vma->vm_start;
	return 0;
}

static struct vm_special_mapping vvar_mapping = {
	.name = "[vvar]",
	.fault = vvar_fault,
};

static struct vm_special_mapping vdso_mapping = {
	.name = "[vdso]",
	.mremap = vdso_mremap,
};

int vdso_getcpu_init(void)
{
	set_tod_programmable_field(smp_processor_id());
	return 0;
}
early_initcall(vdso_getcpu_init); /* Must be called before SMP init */

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	unsigned long addr, vdso_text_start, vdso_text_len, vdso_mapping_len;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int rc;

	if (!vdso_enabled || is_compat_task())
		return 0;
	if (mmap_write_lock_killable(mm))
		return -EINTR;
	vdso_text_len = vdso_pages << PAGE_SHIFT;
	vdso_mapping_len = vdso_text_len + PAGE_SIZE;
	addr = get_unmapped_area(NULL, 0, vdso_mapping_len, 0, 0);
	rc = addr;
	if (IS_ERR_VALUE(addr))
		goto out;
	/* VM_MAYWRITE for COW so gdb can set breakpoints */
	vdso_text_start = addr;
	vma = _install_special_mapping(mm, addr, vdso_text_len,
				       VM_READ|VM_EXEC|
				       VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
				       &vdso_mapping);
	rc = PTR_ERR(vma);
	if (IS_ERR(vma))
		goto out;
	addr += vdso_text_len;
	vma = _install_special_mapping(mm, addr, PAGE_SIZE,
				       VM_READ|VM_MAYREAD|VM_PFNMAP,
				       &vvar_mapping);
	if (IS_ERR(vma)) {
		do_munmap(mm, vdso_text_start, vdso_text_len, NULL);
		rc = PTR_ERR(vma);
	} else {
		current->mm->context.vdso_base = vdso_text_start;
		rc = 0;
	}
out:
	mmap_write_unlock(mm);
	return rc;
}

static int __init vdso_init(void)
{
	struct page **pages;
	int i;

	vdso_pages = (vdso64_end - vdso64_start) >> PAGE_SHIFT;
	pages = kcalloc(vdso_pages + 1, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		vdso_enabled = 0;
		return -ENOMEM;
	}
	for (i = 0; i < vdso_pages; i++)
		pages[i] = virt_to_page(vdso64_start + i * PAGE_SIZE);
	pages[vdso_pages] = NULL;
	vdso_mapping.pages = pages;
	return 0;
}
arch_initcall(vdso_init);
