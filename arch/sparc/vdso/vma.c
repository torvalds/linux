// SPDX-License-Identifier: GPL-2.0-only
/*
 * Set up the VMAs to tell the VM about the vDSO.
 * Copyright 2007 Andi Kleen, SUSE Labs.
 */

/*
 * Copyright (c) 2017 Oracle and/or its affiliates. All rights reserved.
 */

#include <linux/mm.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/random.h>
#include <linux/elf.h>
#include <linux/vdso_datastore.h>
#include <asm/cacheflush.h>
#include <asm/spitfire.h>
#include <asm/vdso.h>
#include <asm/page.h>

#include <vdso/datapage.h>
#include <asm/vdso/vsyscall.h>

unsigned int __read_mostly vdso_enabled = 1;

#ifdef	CONFIG_SPARC64
static struct vm_special_mapping vdso_mapping64 = {
	.name = "[vdso]"
};
#endif

#ifdef CONFIG_COMPAT
static struct vm_special_mapping vdso_mapping32 = {
	.name = "[vdso]"
};
#endif

/*
 * Allocate pages for the vdso and copy in the vdso text from the
 * kernel image.
 */
static int __init init_vdso_image(const struct vdso_image *image,
				  struct vm_special_mapping *vdso_mapping,
				  bool elf64)
{
	int cnpages = (image->size) / PAGE_SIZE;
	struct page *cp, **cpp = NULL;
	int i;

	/*
	 * First, the vdso text.  This is initialied data, an integral number of
	 * pages long.
	 */
	if (WARN_ON(image->size % PAGE_SIZE != 0))
		goto oom;

	cpp = kzalloc_objs(struct page *, cnpages);
	vdso_mapping->pages = cpp;

	if (!cpp)
		goto oom;

	for (i = 0; i < cnpages; i++) {
		cp = alloc_page(GFP_KERNEL);
		if (!cp)
			goto oom;
		cpp[i] = cp;
		copy_page(page_address(cp), image->data + i * PAGE_SIZE);
	}

	return 0;
 oom:
	if (cpp != NULL) {
		for (i = 0; i < cnpages; i++) {
			if (cpp[i] != NULL)
				__free_page(cpp[i]);
		}
		kfree(cpp);
		vdso_mapping->pages = NULL;
	}

	pr_warn("Cannot allocate vdso\n");
	vdso_enabled = 0;
	return -ENOMEM;
}

static int __init init_vdso(void)
{
	int err = 0;
#ifdef CONFIG_SPARC64
	err = init_vdso_image(&vdso_image_64_builtin, &vdso_mapping64, true);
	if (err)
		return err;
#endif

#ifdef CONFIG_COMPAT
	err = init_vdso_image(&vdso_image_32_builtin, &vdso_mapping32, false);
#endif
	return err;

}
subsys_initcall(init_vdso);

struct linux_binprm;

/* Shuffle the vdso up a bit, randomly. */
static unsigned long vdso_addr(unsigned long start, unsigned int len)
{
	unsigned int offset;

	/* This loses some more bits than a modulo, but is cheaper */
	offset = get_random_u32_below(PTRS_PER_PTE);
	return start + (offset << PAGE_SHIFT);
}

static_assert(VDSO_NR_PAGES == __VDSO_PAGES);

static int map_vdso(const struct vdso_image *image,
		struct vm_special_mapping *vdso_mapping)
{
	const size_t area_size = image->size + VDSO_NR_PAGES * PAGE_SIZE;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long text_start, addr = 0;
	int ret = 0;

	mmap_write_lock(mm);

	/*
	 * First, get an unmapped region: then randomize it, and make sure that
	 * region is free.
	 */
	if (current->flags & PF_RANDOMIZE) {
		addr = get_unmapped_area(NULL, 0, area_size, 0, 0);
		if (IS_ERR_VALUE(addr)) {
			ret = addr;
			goto up_fail;
		}
		addr = vdso_addr(addr, area_size);
	}
	addr = get_unmapped_area(NULL, addr, area_size, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	text_start = addr + VDSO_NR_PAGES * PAGE_SIZE;
	current->mm->context.vdso = (void __user *)text_start;

	/*
	 * MAYWRITE to allow gdb to COW and set breakpoints
	 */
	vma = _install_special_mapping(mm,
				       text_start,
				       image->size,
				       VM_READ|VM_EXEC|
				       VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
				       vdso_mapping);

	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto up_fail;
	}

	vma = vdso_install_vvar_mapping(mm, addr);

	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		do_munmap(mm, text_start, image->size, NULL);
	}

up_fail:
	if (ret)
		current->mm->context.vdso = NULL;

	mmap_write_unlock(mm);
	return ret;
}

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{

	if (!vdso_enabled)
		return 0;

#if defined CONFIG_COMPAT
	if (!(is_32bit_task()))
		return map_vdso(&vdso_image_64_builtin, &vdso_mapping64);
	else
		return map_vdso(&vdso_image_32_builtin, &vdso_mapping32);
#else
	return map_vdso(&vdso_image_64_builtin, &vdso_mapping64);
#endif

}

static __init int vdso_setup(char *s)
{
	int err;
	unsigned long val;

	err = kstrtoul(s, 10, &val);
	if (!err)
		vdso_enabled = val;
	return 1;
}
__setup("vdso=", vdso_setup);
