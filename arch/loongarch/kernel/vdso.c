// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <linux/binfmts.h>
#include <linux/elf.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timekeeper_internal.h>

#include <asm/page.h>
#include <asm/vdso.h>
#include <vdso/helpers.h>
#include <vdso/vsyscall.h>
#include <generated/vdso-offsets.h>

extern char vdso_start[], vdso_end[];

/* Kernel-provided data used by the VDSO. */
static union loongarch_vdso_data {
	u8 page[PAGE_SIZE];
	struct vdso_data data[CS_BASES];
} loongarch_vdso_data __page_aligned_data;
struct vdso_data *vdso_data = loongarch_vdso_data.data;
static struct page *vdso_pages[] = { NULL };

static int vdso_mremap(const struct vm_special_mapping *sm, struct vm_area_struct *new_vma)
{
	current->mm->context.vdso = (void *)(new_vma->vm_start);

	return 0;
}

struct loongarch_vdso_info vdso_info = {
	.vdso = vdso_start,
	.size = PAGE_SIZE,
	.code_mapping = {
		.name = "[vdso]",
		.pages = vdso_pages,
		.mremap = vdso_mremap,
	},
	.data_mapping = {
		.name = "[vvar]",
	},
	.offset_sigreturn = vdso_offset_sigreturn,
};

static int __init init_vdso(void)
{
	unsigned long i, pfn;

	BUG_ON(!PAGE_ALIGNED(vdso_info.vdso));
	BUG_ON(!PAGE_ALIGNED(vdso_info.size));

	pfn = __phys_to_pfn(__pa_symbol(vdso_info.vdso));
	for (i = 0; i < vdso_info.size / PAGE_SIZE; i++)
		vdso_info.code_mapping.pages[i] = pfn_to_page(pfn + i);

	return 0;
}
subsys_initcall(init_vdso);

static unsigned long vdso_base(void)
{
	unsigned long base = STACK_TOP;

	if (current->flags & PF_RANDOMIZE) {
		base += get_random_int() & (VDSO_RANDOMIZE_SIZE - 1);
		base = PAGE_ALIGN(base);
	}

	return base;
}

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	int ret;
	unsigned long vvar_size, size, data_addr, vdso_addr;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct loongarch_vdso_info *info = current->thread.vdso;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	/*
	 * Determine total area size. This includes the VDSO data itself
	 * and the data page.
	 */
	vvar_size = PAGE_SIZE;
	size = vvar_size + info->size;

	data_addr = get_unmapped_area(NULL, vdso_base(), size, 0, 0);
	if (IS_ERR_VALUE(data_addr)) {
		ret = data_addr;
		goto out;
	}
	vdso_addr = data_addr + PAGE_SIZE;

	vma = _install_special_mapping(mm, data_addr, vvar_size,
				       VM_READ | VM_MAYREAD,
				       &info->data_mapping);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto out;
	}

	/* Map VDSO data page. */
	ret = remap_pfn_range(vma, data_addr,
			      virt_to_phys(vdso_data) >> PAGE_SHIFT,
			      PAGE_SIZE, PAGE_READONLY);
	if (ret)
		goto out;

	/* Map VDSO code page. */
	vma = _install_special_mapping(mm, vdso_addr, info->size,
				       VM_READ | VM_EXEC | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC,
				       &info->code_mapping);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto out;
	}

	mm->context.vdso = (void *)vdso_addr;
	ret = 0;

out:
	mmap_write_unlock(mm);
	return ret;
}
