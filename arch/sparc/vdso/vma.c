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
#include <asm/cacheflush.h>
#include <asm/spitfire.h>
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

struct vdso_elfinfo32 {
	Elf32_Ehdr	*hdr;
	Elf32_Sym	*dynsym;
	unsigned long	dynsymsize;
	const char	*dynstr;
	unsigned long	text;
};

struct vdso_elfinfo64 {
	Elf64_Ehdr	*hdr;
	Elf64_Sym	*dynsym;
	unsigned long	dynsymsize;
	const char	*dynstr;
	unsigned long	text;
};

struct vdso_elfinfo {
	union {
		struct vdso_elfinfo32 elf32;
		struct vdso_elfinfo64 elf64;
	} u;
};

static void *one_section64(struct vdso_elfinfo64 *e, const char *name,
			   unsigned long *size)
{
	const char *snames;
	Elf64_Shdr *shdrs;
	unsigned int i;

	shdrs = (void *)e->hdr + e->hdr->e_shoff;
	snames = (void *)e->hdr + shdrs[e->hdr->e_shstrndx].sh_offset;
	for (i = 1; i < e->hdr->e_shnum; i++) {
		if (!strcmp(snames+shdrs[i].sh_name, name)) {
			if (size)
				*size = shdrs[i].sh_size;
			return (void *)e->hdr + shdrs[i].sh_offset;
		}
	}
	return NULL;
}

static int find_sections64(const struct vdso_image *image, struct vdso_elfinfo *_e)
{
	struct vdso_elfinfo64 *e = &_e->u.elf64;

	e->hdr = image->data;
	e->dynsym = one_section64(e, ".dynsym", &e->dynsymsize);
	e->dynstr = one_section64(e, ".dynstr", NULL);

	if (!e->dynsym || !e->dynstr) {
		pr_err("VDSO64: Missing symbol sections.\n");
		return -ENODEV;
	}
	return 0;
}

static Elf64_Sym *find_sym64(const struct vdso_elfinfo64 *e, const char *name)
{
	unsigned int i;

	for (i = 0; i < (e->dynsymsize / sizeof(Elf64_Sym)); i++) {
		Elf64_Sym *s = &e->dynsym[i];
		if (s->st_name == 0)
			continue;
		if (!strcmp(e->dynstr + s->st_name, name))
			return s;
	}
	return NULL;
}

static int patchsym64(struct vdso_elfinfo *_e, const char *orig,
		      const char *new)
{
	struct vdso_elfinfo64 *e = &_e->u.elf64;
	Elf64_Sym *osym = find_sym64(e, orig);
	Elf64_Sym *nsym = find_sym64(e, new);

	if (!nsym || !osym) {
		pr_err("VDSO64: Missing symbols.\n");
		return -ENODEV;
	}
	osym->st_value = nsym->st_value;
	osym->st_size = nsym->st_size;
	osym->st_info = nsym->st_info;
	osym->st_other = nsym->st_other;
	osym->st_shndx = nsym->st_shndx;

	return 0;
}

static void *one_section32(struct vdso_elfinfo32 *e, const char *name,
			   unsigned long *size)
{
	const char *snames;
	Elf32_Shdr *shdrs;
	unsigned int i;

	shdrs = (void *)e->hdr + e->hdr->e_shoff;
	snames = (void *)e->hdr + shdrs[e->hdr->e_shstrndx].sh_offset;
	for (i = 1; i < e->hdr->e_shnum; i++) {
		if (!strcmp(snames+shdrs[i].sh_name, name)) {
			if (size)
				*size = shdrs[i].sh_size;
			return (void *)e->hdr + shdrs[i].sh_offset;
		}
	}
	return NULL;
}

static int find_sections32(const struct vdso_image *image, struct vdso_elfinfo *_e)
{
	struct vdso_elfinfo32 *e = &_e->u.elf32;

	e->hdr = image->data;
	e->dynsym = one_section32(e, ".dynsym", &e->dynsymsize);
	e->dynstr = one_section32(e, ".dynstr", NULL);

	if (!e->dynsym || !e->dynstr) {
		pr_err("VDSO32: Missing symbol sections.\n");
		return -ENODEV;
	}
	return 0;
}

static Elf32_Sym *find_sym32(const struct vdso_elfinfo32 *e, const char *name)
{
	unsigned int i;

	for (i = 0; i < (e->dynsymsize / sizeof(Elf32_Sym)); i++) {
		Elf32_Sym *s = &e->dynsym[i];
		if (s->st_name == 0)
			continue;
		if (!strcmp(e->dynstr + s->st_name, name))
			return s;
	}
	return NULL;
}

static int patchsym32(struct vdso_elfinfo *_e, const char *orig,
		      const char *new)
{
	struct vdso_elfinfo32 *e = &_e->u.elf32;
	Elf32_Sym *osym = find_sym32(e, orig);
	Elf32_Sym *nsym = find_sym32(e, new);

	if (!nsym || !osym) {
		pr_err("VDSO32: Missing symbols.\n");
		return -ENODEV;
	}
	osym->st_value = nsym->st_value;
	osym->st_size = nsym->st_size;
	osym->st_info = nsym->st_info;
	osym->st_other = nsym->st_other;
	osym->st_shndx = nsym->st_shndx;

	return 0;
}

static int find_sections(const struct vdso_image *image, struct vdso_elfinfo *e,
			 bool elf64)
{
	if (elf64)
		return find_sections64(image, e);
	else
		return find_sections32(image, e);
}

static int patch_one_symbol(struct vdso_elfinfo *e, const char *orig,
			    const char *new_target, bool elf64)
{
	if (elf64)
		return patchsym64(e, orig, new_target);
	else
		return patchsym32(e, orig, new_target);
}

static int stick_patch(const struct vdso_image *image, struct vdso_elfinfo *e, bool elf64)
{
	int err;

	err = find_sections(image, e, elf64);
	if (err)
		return err;

	err = patch_one_symbol(e,
			       "__vdso_gettimeofday",
			       "__vdso_gettimeofday_stick", elf64);
	if (err)
		return err;

	return patch_one_symbol(e,
				"__vdso_clock_gettime",
				"__vdso_clock_gettime_stick", elf64);
	return 0;
}

/*
 * Allocate pages for the vdso and vvar, and copy in the vdso text from the
 * kernel image.
 */
int __init init_vdso_image(const struct vdso_image *image,
			   struct vm_special_mapping *vdso_mapping, bool elf64)
{
	int cnpages = (image->size) / PAGE_SIZE;
	struct page *dp, **dpp = NULL;
	struct page *cp, **cpp = NULL;
	struct vdso_elfinfo ei;
	int i, dnpages = 0;

	if (tlb_type != spitfire) {
		int err = stick_patch(image, &ei, elf64);
		if (err)
			return err;
	}

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

static int map_vdso(const struct vdso_image *image,
		struct vm_special_mapping *vdso_mapping)
{
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
