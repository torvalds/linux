/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/binfmts.h>
#include <linux/elf.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/abi.h>
#include <asm/vdso.h>

/* Kernel-provided data used by the VDSO. */
static union mips_vdso_data vdso_data __page_aligned_data;

/*
 * Mapping for the VDSO data pages. The real pages are mapped manually, as
 * what we map and where within the area they are mapped is determined at
 * runtime.
 */
static struct page *no_pages[] = { NULL };
static struct vm_special_mapping vdso_vvar_mapping = {
	.name = "[vvar]",
	.pages = no_pages,
};

static void __init init_vdso_image(struct mips_vdso_image *image)
{
	unsigned long num_pages, i;

	BUG_ON(!PAGE_ALIGNED(image->data));
	BUG_ON(!PAGE_ALIGNED(image->size));

	num_pages = image->size / PAGE_SIZE;

	for (i = 0; i < num_pages; i++) {
		image->mapping.pages[i] =
			virt_to_page(image->data + (i * PAGE_SIZE));
	}
}

static int __init init_vdso(void)
{
	init_vdso_image(&vdso_image);

#ifdef CONFIG_MIPS32_O32
	init_vdso_image(&vdso_image_o32);
#endif

#ifdef CONFIG_MIPS32_N32
	init_vdso_image(&vdso_image_n32);
#endif

	return 0;
}
subsys_initcall(init_vdso);

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mips_vdso_image *image = current->thread.abi->vdso;
	struct mm_struct *mm = current->mm;
	unsigned long base, vdso_addr;
	struct vm_area_struct *vma;
	int ret;

	down_write(&mm->mmap_sem);

	base = get_unmapped_area(NULL, 0, PAGE_SIZE + image->size, 0, 0);
	if (IS_ERR_VALUE(base)) {
		ret = base;
		goto out;
	}

	vdso_addr = base + PAGE_SIZE;

	vma = _install_special_mapping(mm, base, PAGE_SIZE,
				       VM_READ | VM_MAYREAD,
				       &vdso_vvar_mapping);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto out;
	}

	/* Map data page. */
	ret = remap_pfn_range(vma, base,
			      virt_to_phys(&vdso_data) >> PAGE_SHIFT,
			      PAGE_SIZE, PAGE_READONLY);
	if (ret)
		goto out;

	/* Map VDSO image. */
	vma = _install_special_mapping(mm, vdso_addr, image->size,
				       VM_READ | VM_EXEC |
				       VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC,
				       &image->mapping);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto out;
	}

	mm->context.vdso = (void *)vdso_addr;
	ret = 0;

out:
	up_write(&mm->mmap_sem);
	return ret;
}
