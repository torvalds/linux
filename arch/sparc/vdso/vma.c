/*
 * Set up the VMAs to tell the VM about the vDSO.
 * Copyright 2007 Andi Kleen, SUSE Labs.
 * Subject to the GPL, v.2
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
#include <asm/vdso.h>
#include <asm/vvar.h>
#include <asm/page.h>

unsigned int __read_mostly vdso_enabled = 1;

static struct vm_special_mapping vvar_mapping = {
	.name = "[vvar]"
};

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

struct vvar_data *vvar_data;

#define	SAVE_INSTR_SIZE	4

/*
 * Allocate pages for the vdso and vvar, and copy in the vdso text from the
 * kernel image.
 */
int __init init_vdso_image(const struct vdso_image *image,
		struct vm_special_mapping *vdso_mapping)
{
	int i;
	struct page *dp, **dpp = NULL;
	int dnpages = 0;
	struct page *cp, **cpp = NULL;
	int cnpages = (image->size) / PAGE_SIZE;

	/*
	 * First, the vdso text.  This is initialied data, an integral number of
	 * pages long.
	 */
	if (WARN_ON(image->size % PAGE_SIZE != 0))
		goto oom;

	cpp = kcalloc(cnpages, sizeof(struct page *), GFP_KERNEL);
	vdso_mapping->pages = cpp;

	if (!cpp)
		goto oom;

	if (vdso_fix_stick) {
		/*
		 * If the system uses %tick instead of %stick, patch the VDSO
		 * with instruction reading %tick instead of %stick.
		 */
		unsigned int j, k = SAVE_INSTR_SIZE;
		unsigned char *data = image->data;

		for (j = image->sym_vread_tick_patch_start;
		     j < image->sym_vread_tick_patch_end; j++) {

			data[image->sym_vread_tick + k] = data[j];
			k++;
		}
	}

	for (i = 0; i < cnpages; i++) {
		cp = alloc_page(GFP_KERNEL);
		if (!cp)
			goto oom;
		cpp[i] = cp;
		copy_page(page_address(cp), image->data + i * PAGE_SIZE);
	}

	/*
	 * Now the vvar page.  This is uninitialized data.
	 */

	if (vvar_data == NULL) {
		dnpages = (sizeof(struct vvar_data) / PAGE_SIZE) + 1;
		if (WARN_ON(dnpages != 1))
			goto oom;
		dpp = kcalloc(dnpages, sizeof(struct page *), GFP_KERNEL);
		vvar_mapping.pages = dpp;

		if (!dpp)
			goto oom;

		dp = alloc_page(GFP_KERNEL);
		if (!dp)
			goto oom;

		dpp[0] = dp;
		vvar_data = page_address(dp);
		memset(vvar_data, 0, PAGE_SIZE);

		vvar_data->seq = 0;
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

	if (dpp != NULL) {
		for (i = 0; i < dnpages; i++) {
			if (dpp[i] != NULL)
				__free_page(dpp[i]);
		}
		kfree(dpp);
		vvar_mapping.pages = NULL;
	}

	pr_warn("Cannot allocate vdso\n");
	vdso_enabled = 0;
	return -ENOMEM;
}

static int __init init_vdso(void)
{
	int err = 0;
#ifdef CONFIG_SPARC64
	err = init_vdso_image(&vdso_image_64_builtin, &vdso_mapping64);
	if (err)
		return err;
#endif

#ifdef CONFIG_COMPAT
	err = init_vdso_image(&vdso_image_32_builtin, &vdso_mapping32);
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
	offset = get_random_int() & (PTRS_PER_PTE - 1);
	return start + (offset << PAGE_SHIFT);
}

static int map_vdso(const struct vdso_image *image,
		struct vm_special_mapping *vdso_mapping)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long text_start, addr = 0;
	int ret = 0;

	down_write(&mm->mmap_sem);

	/*
	 * First, get an unmapped region: then randomize it, and make sure that
	 * region is free.
	 */
	if (current->flags & PF_RANDOMIZE) {
		addr = get_unmapped_area(NULL, 0,
					 image->size - image->sym_vvar_start,
					 0, 0);
		if (IS_ERR_VALUE(addr)) {
			ret = addr;
			goto up_fail;
		}
		addr = vdso_addr(addr, image->size - image->sym_vvar_start);
	}
	addr = get_unmapped_area(NULL, addr,
				 image->size - image->sym_vvar_start, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = addr;
		goto up_fail;
	}

	text_start = addr - image->sym_vvar_start;
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

	vma = _install_special_mapping(mm,
				       addr,
				       -image->sym_vvar_start,
				       VM_READ|VM_MAYREAD,
				       &vvar_mapping);

	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		do_munmap(mm, text_start, image->size, NULL);
	}

up_fail:
	if (ret)
		current->mm->context.vdso = NULL;

	up_write(&mm->mmap_sem);
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
	vdso_enabled = val;
	return err;
}
__setup("vdso=", vdso_setup);
