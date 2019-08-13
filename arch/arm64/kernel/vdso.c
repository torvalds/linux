// SPDX-License-Identifier: GPL-2.0-only
/*
 * VDSO implementations.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#include <linux/cache.h>
#include <linux/clocksource.h>
#include <linux/elf.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/timekeeper_internal.h>
#include <linux/vmalloc.h>
#include <vdso/datapage.h>
#include <vdso/helpers.h>
#include <vdso/vsyscall.h>

#include <asm/cacheflush.h>
#include <asm/signal32.h>
#include <asm/vdso.h>

extern char vdso_start[], vdso_end[];
#ifdef CONFIG_COMPAT_VDSO
extern char vdso32_start[], vdso32_end[];
#endif /* CONFIG_COMPAT_VDSO */

/* vdso_lookup arch_index */
enum arch_vdso_type {
	ARM64_VDSO = 0,
#ifdef CONFIG_COMPAT_VDSO
	ARM64_VDSO32 = 1,
#endif /* CONFIG_COMPAT_VDSO */
};
#ifdef CONFIG_COMPAT_VDSO
#define VDSO_TYPES		(ARM64_VDSO32 + 1)
#else
#define VDSO_TYPES		(ARM64_VDSO + 1)
#endif /* CONFIG_COMPAT_VDSO */

struct __vdso_abi {
	const char *name;
	const char *vdso_code_start;
	const char *vdso_code_end;
	unsigned long vdso_pages;
	/* Data Mapping */
	struct vm_special_mapping *dm;
	/* Code Mapping */
	struct vm_special_mapping *cm;
};

static struct __vdso_abi vdso_lookup[VDSO_TYPES] __ro_after_init = {
	{
		.name = "vdso",
		.vdso_code_start = vdso_start,
		.vdso_code_end = vdso_end,
	},
#ifdef CONFIG_COMPAT_VDSO
	{
		.name = "vdso32",
		.vdso_code_start = vdso32_start,
		.vdso_code_end = vdso32_end,
	},
#endif /* CONFIG_COMPAT_VDSO */
};

/*
 * The vDSO data page.
 */
static union {
	struct vdso_data	data[CS_BASES];
	u8			page[PAGE_SIZE];
} vdso_data_store __page_aligned_data;
struct vdso_data *vdso_data = vdso_data_store.data;

static int __vdso_remap(enum arch_vdso_type arch_index,
			const struct vm_special_mapping *sm,
			struct vm_area_struct *new_vma)
{
	unsigned long new_size = new_vma->vm_end - new_vma->vm_start;
	unsigned long vdso_size = vdso_lookup[arch_index].vdso_code_end -
				  vdso_lookup[arch_index].vdso_code_start;

	if (vdso_size != new_size)
		return -EINVAL;

	current->mm->context.vdso = (void *)new_vma->vm_start;

	return 0;
}

static int __vdso_init(enum arch_vdso_type arch_index)
{
	int i;
	struct page **vdso_pagelist;
	unsigned long pfn;

	if (memcmp(vdso_lookup[arch_index].vdso_code_start, "\177ELF", 4)) {
		pr_err("vDSO is not a valid ELF object!\n");
		return -EINVAL;
	}

	vdso_lookup[arch_index].vdso_pages = (
			vdso_lookup[arch_index].vdso_code_end -
			vdso_lookup[arch_index].vdso_code_start) >>
			PAGE_SHIFT;

	/* Allocate the vDSO pagelist, plus a page for the data. */
	vdso_pagelist = kcalloc(vdso_lookup[arch_index].vdso_pages + 1,
				sizeof(struct page *),
				GFP_KERNEL);
	if (vdso_pagelist == NULL)
		return -ENOMEM;

	/* Grab the vDSO data page. */
	vdso_pagelist[0] = phys_to_page(__pa_symbol(vdso_data));


	/* Grab the vDSO code pages. */
	pfn = sym_to_pfn(vdso_lookup[arch_index].vdso_code_start);

	for (i = 0; i < vdso_lookup[arch_index].vdso_pages; i++)
		vdso_pagelist[i + 1] = pfn_to_page(pfn + i);

	vdso_lookup[arch_index].dm->pages = &vdso_pagelist[0];
	vdso_lookup[arch_index].cm->pages = &vdso_pagelist[1];

	return 0;
}

static int __setup_additional_pages(enum arch_vdso_type arch_index,
				    struct mm_struct *mm,
				    struct linux_binprm *bprm,
				    int uses_interp)
{
	unsigned long vdso_base, vdso_text_len, vdso_mapping_len;
	void *ret;

	vdso_text_len = vdso_lookup[arch_index].vdso_pages << PAGE_SHIFT;
	/* Be sure to map the data page */
	vdso_mapping_len = vdso_text_len + PAGE_SIZE;

	vdso_base = get_unmapped_area(NULL, 0, vdso_mapping_len, 0, 0);
	if (IS_ERR_VALUE(vdso_base)) {
		ret = ERR_PTR(vdso_base);
		goto up_fail;
	}

	ret = _install_special_mapping(mm, vdso_base, PAGE_SIZE,
				       VM_READ|VM_MAYREAD,
				       vdso_lookup[arch_index].dm);
	if (IS_ERR(ret))
		goto up_fail;

	vdso_base += PAGE_SIZE;
	mm->context.vdso = (void *)vdso_base;
	ret = _install_special_mapping(mm, vdso_base, vdso_text_len,
				       VM_READ|VM_EXEC|
				       VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
				       vdso_lookup[arch_index].cm);
	if (IS_ERR(ret))
		goto up_fail;

	return 0;

up_fail:
	mm->context.vdso = NULL;
	return PTR_ERR(ret);
}

#ifdef CONFIG_COMPAT
/*
 * Create and map the vectors page for AArch32 tasks.
 */
#ifdef CONFIG_COMPAT_VDSO
static int aarch32_vdso_mremap(const struct vm_special_mapping *sm,
		struct vm_area_struct *new_vma)
{
	return __vdso_remap(ARM64_VDSO32, sm, new_vma);
}
#endif /* CONFIG_COMPAT_VDSO */

/*
 * aarch32_vdso_pages:
 * 0 - kuser helpers
 * 1 - sigreturn code
 * or (CONFIG_COMPAT_VDSO):
 * 0 - kuser helpers
 * 1 - vdso data
 * 2 - vdso code
 */
#define C_VECTORS	0
#ifdef CONFIG_COMPAT_VDSO
#define C_VVAR		1
#define C_VDSO		2
#define C_PAGES		(C_VDSO + 1)
#else
#define C_SIGPAGE	1
#define C_PAGES		(C_SIGPAGE + 1)
#endif /* CONFIG_COMPAT_VDSO */
static struct page *aarch32_vdso_pages[C_PAGES] __ro_after_init;
static struct vm_special_mapping aarch32_vdso_spec[C_PAGES] = {
	{
		.name	= "[vectors]", /* ABI */
		.pages	= &aarch32_vdso_pages[C_VECTORS],
	},
#ifdef CONFIG_COMPAT_VDSO
	{
		.name = "[vvar]",
	},
	{
		.name = "[vdso]",
		.mremap = aarch32_vdso_mremap,
	},
#else
	{
		.name	= "[sigpage]", /* ABI */
		.pages	= &aarch32_vdso_pages[C_SIGPAGE],
	},
#endif /* CONFIG_COMPAT_VDSO */
};

static int aarch32_alloc_kuser_vdso_page(void)
{
	extern char __kuser_helper_start[], __kuser_helper_end[];
	int kuser_sz = __kuser_helper_end - __kuser_helper_start;
	unsigned long vdso_page;

	if (!IS_ENABLED(CONFIG_KUSER_HELPERS))
		return 0;

	vdso_page = get_zeroed_page(GFP_ATOMIC);
	if (!vdso_page)
		return -ENOMEM;

	memcpy((void *)(vdso_page + 0x1000 - kuser_sz), __kuser_helper_start,
	       kuser_sz);
	aarch32_vdso_pages[C_VECTORS] = virt_to_page(vdso_page);
	flush_dcache_page(aarch32_vdso_pages[C_VECTORS]);
	return 0;
}

#ifdef CONFIG_COMPAT_VDSO
static int __aarch32_alloc_vdso_pages(void)
{
	int ret;

	vdso_lookup[ARM64_VDSO32].dm = &aarch32_vdso_spec[C_VVAR];
	vdso_lookup[ARM64_VDSO32].cm = &aarch32_vdso_spec[C_VDSO];

	ret = __vdso_init(ARM64_VDSO32);
	if (ret)
		return ret;

	ret = aarch32_alloc_kuser_vdso_page();
	if (ret) {
		unsigned long c_vvar =
			(unsigned long)page_to_virt(aarch32_vdso_pages[C_VVAR]);
		unsigned long c_vdso =
			(unsigned long)page_to_virt(aarch32_vdso_pages[C_VDSO]);

		free_page(c_vvar);
		free_page(c_vdso);
	}

	return ret;
}
#else
static int __aarch32_alloc_vdso_pages(void)
{
	extern char __aarch32_sigret_code_start[], __aarch32_sigret_code_end[];
	int sigret_sz = __aarch32_sigret_code_end - __aarch32_sigret_code_start;
	unsigned long sigpage;
	int ret;

	sigpage = get_zeroed_page(GFP_ATOMIC);
	if (!sigpage)
		return -ENOMEM;

	memcpy((void *)sigpage, __aarch32_sigret_code_start, sigret_sz);
	aarch32_vdso_pages[C_SIGPAGE] = virt_to_page(sigpage);
	flush_dcache_page(aarch32_vdso_pages[C_SIGPAGE]);

	ret = aarch32_alloc_kuser_vdso_page();
	if (ret)
		free_page(sigpage);

	return ret;
}
#endif /* CONFIG_COMPAT_VDSO */

static int __init aarch32_alloc_vdso_pages(void)
{
	return __aarch32_alloc_vdso_pages();
}
arch_initcall(aarch32_alloc_vdso_pages);

static int aarch32_kuser_helpers_setup(struct mm_struct *mm)
{
	void *ret;

	if (!IS_ENABLED(CONFIG_KUSER_HELPERS))
		return 0;

	/*
	 * Avoid VM_MAYWRITE for compatibility with arch/arm/, where it's
	 * not safe to CoW the page containing the CPU exception vectors.
	 */
	ret = _install_special_mapping(mm, AARCH32_VECTORS_BASE, PAGE_SIZE,
				       VM_READ | VM_EXEC |
				       VM_MAYREAD | VM_MAYEXEC,
				       &aarch32_vdso_spec[C_VECTORS]);

	return PTR_ERR_OR_ZERO(ret);
}

#ifndef CONFIG_COMPAT_VDSO
static int aarch32_sigreturn_setup(struct mm_struct *mm)
{
	unsigned long addr;
	void *ret;

	addr = get_unmapped_area(NULL, 0, PAGE_SIZE, 0, 0);
	if (IS_ERR_VALUE(addr)) {
		ret = ERR_PTR(addr);
		goto out;
	}

	/*
	 * VM_MAYWRITE is required to allow gdb to Copy-on-Write and
	 * set breakpoints.
	 */
	ret = _install_special_mapping(mm, addr, PAGE_SIZE,
				       VM_READ | VM_EXEC | VM_MAYREAD |
				       VM_MAYWRITE | VM_MAYEXEC,
				       &aarch32_vdso_spec[C_SIGPAGE]);
	if (IS_ERR(ret))
		goto out;

	mm->context.vdso = (void *)addr;

out:
	return PTR_ERR_OR_ZERO(ret);
}
#endif /* !CONFIG_COMPAT_VDSO */

int aarch32_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	int ret;

	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;

	ret = aarch32_kuser_helpers_setup(mm);
	if (ret)
		goto out;

#ifdef CONFIG_COMPAT_VDSO
	ret = __setup_additional_pages(ARM64_VDSO32,
				       mm,
				       bprm,
				       uses_interp);
#else
	ret = aarch32_sigreturn_setup(mm);
#endif /* CONFIG_COMPAT_VDSO */

out:
	up_write(&mm->mmap_sem);
	return ret;
}
#endif /* CONFIG_COMPAT */

static int vdso_mremap(const struct vm_special_mapping *sm,
		struct vm_area_struct *new_vma)
{
	return __vdso_remap(ARM64_VDSO, sm, new_vma);
}

/*
 * aarch64_vdso_pages:
 * 0 - vvar
 * 1 - vdso
 */
#define A_VVAR		0
#define A_VDSO		1
#define A_PAGES		(A_VDSO + 1)
static struct vm_special_mapping vdso_spec[A_PAGES] __ro_after_init = {
	{
		.name	= "[vvar]",
	},
	{
		.name	= "[vdso]",
		.mremap = vdso_mremap,
	},
};

static int __init vdso_init(void)
{
	vdso_lookup[ARM64_VDSO].dm = &vdso_spec[A_VVAR];
	vdso_lookup[ARM64_VDSO].cm = &vdso_spec[A_VDSO];

	return __vdso_init(ARM64_VDSO);
}
arch_initcall(vdso_init);

int arch_setup_additional_pages(struct linux_binprm *bprm,
				int uses_interp)
{
	struct mm_struct *mm = current->mm;
	int ret;

	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;

	ret = __setup_additional_pages(ARM64_VDSO,
				       mm,
				       bprm,
				       uses_interp);

	up_write(&mm->mmap_sem);

	return ret;
}
