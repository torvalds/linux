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
#include <linux/vdso_datastore.h>
#include <linux/vmalloc.h>
#include <vdso/datapage.h>
#include <vdso/helpers.h>
#include <vdso/vsyscall.h>

#include <asm/cacheflush.h>
#include <asm/signal32.h>
#include <asm/vdso.h>

enum vdso_abi {
	VDSO_ABI_AA64,
	VDSO_ABI_AA32,
};

struct vdso_abi_info {
	const char *name;
	const char *vdso_code_start;
	const char *vdso_code_end;
	unsigned long vdso_pages;
	/* Code Mapping */
	struct vm_special_mapping *cm;
};

static struct vdso_abi_info vdso_info[] __ro_after_init = {
	[VDSO_ABI_AA64] = {
		.name = "vdso",
		.vdso_code_start = vdso_start,
		.vdso_code_end = vdso_end,
	},
#ifdef CONFIG_COMPAT_VDSO
	[VDSO_ABI_AA32] = {
		.name = "vdso32",
		.vdso_code_start = vdso32_start,
		.vdso_code_end = vdso32_end,
	},
#endif /* CONFIG_COMPAT_VDSO */
};

static int vdso_mremap(const struct vm_special_mapping *sm,
		struct vm_area_struct *new_vma)
{
	current->mm->context.vdso = (void *)new_vma->vm_start;

	return 0;
}

static int __init __vdso_init(enum vdso_abi abi)
{
	int i;
	struct page **vdso_pagelist;
	unsigned long pfn;

	if (memcmp(vdso_info[abi].vdso_code_start, "\177ELF", 4)) {
		pr_err("vDSO is not a valid ELF object!\n");
		return -EINVAL;
	}

	vdso_info[abi].vdso_pages = (
			vdso_info[abi].vdso_code_end -
			vdso_info[abi].vdso_code_start) >>
			PAGE_SHIFT;

	vdso_pagelist = kcalloc(vdso_info[abi].vdso_pages,
				sizeof(struct page *),
				GFP_KERNEL);
	if (vdso_pagelist == NULL)
		return -ENOMEM;

	/* Grab the vDSO code pages. */
	pfn = sym_to_pfn(vdso_info[abi].vdso_code_start);

	for (i = 0; i < vdso_info[abi].vdso_pages; i++)
		vdso_pagelist[i] = pfn_to_page(pfn + i);

	vdso_info[abi].cm->pages = vdso_pagelist;

	return 0;
}

static int __setup_additional_pages(enum vdso_abi abi,
				    struct mm_struct *mm,
				    struct linux_binprm *bprm,
				    int uses_interp)
{
	unsigned long vdso_base, vdso_text_len, vdso_mapping_len;
	unsigned long gp_flags = 0;
	void *ret;

	BUILD_BUG_ON(VDSO_NR_PAGES != __VDSO_PAGES);

	vdso_text_len = vdso_info[abi].vdso_pages << PAGE_SHIFT;
	/* Be sure to map the data page */
	vdso_mapping_len = vdso_text_len + VDSO_NR_PAGES * PAGE_SIZE;

	vdso_base = get_unmapped_area(NULL, 0, vdso_mapping_len, 0, 0);
	if (IS_ERR_VALUE(vdso_base)) {
		ret = ERR_PTR(vdso_base);
		goto up_fail;
	}

	ret = vdso_install_vvar_mapping(mm, vdso_base);
	if (IS_ERR(ret))
		goto up_fail;

	if (system_supports_bti_kernel())
		gp_flags = VM_ARM64_BTI;

	vdso_base += VDSO_NR_PAGES * PAGE_SIZE;
	mm->context.vdso = (void *)vdso_base;
	ret = _install_special_mapping(mm, vdso_base, vdso_text_len,
				       VM_READ|VM_EXEC|gp_flags|
				       VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC|
				       VM_SEALED_SYSMAP,
				       vdso_info[abi].cm);
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
enum aarch32_map {
	AA32_MAP_VECTORS, /* kuser helpers */
	AA32_MAP_SIGPAGE,
	AA32_MAP_VDSO,
};

static struct page *aarch32_vectors_page __ro_after_init;
static struct page *aarch32_sig_page __ro_after_init;

static int aarch32_sigpage_mremap(const struct vm_special_mapping *sm,
				  struct vm_area_struct *new_vma)
{
	current->mm->context.sigpage = (void *)new_vma->vm_start;

	return 0;
}

static struct vm_special_mapping aarch32_vdso_maps[] = {
	[AA32_MAP_VECTORS] = {
		.name	= "[vectors]", /* ABI */
		.pages	= &aarch32_vectors_page,
	},
	[AA32_MAP_SIGPAGE] = {
		.name	= "[sigpage]", /* ABI */
		.pages	= &aarch32_sig_page,
		.mremap	= aarch32_sigpage_mremap,
	},
	[AA32_MAP_VDSO] = {
		.name = "[vdso]",
		.mremap = vdso_mremap,
	},
};

static int aarch32_alloc_kuser_vdso_page(void)
{
	extern char __kuser_helper_start[], __kuser_helper_end[];
	int kuser_sz = __kuser_helper_end - __kuser_helper_start;
	unsigned long vdso_page;

	if (!IS_ENABLED(CONFIG_KUSER_HELPERS))
		return 0;

	vdso_page = get_zeroed_page(GFP_KERNEL);
	if (!vdso_page)
		return -ENOMEM;

	memcpy((void *)(vdso_page + 0x1000 - kuser_sz), __kuser_helper_start,
	       kuser_sz);
	aarch32_vectors_page = virt_to_page((void *)vdso_page);
	return 0;
}

#define COMPAT_SIGPAGE_POISON_WORD	0xe7fddef1
static int aarch32_alloc_sigpage(void)
{
	extern char __aarch32_sigret_code_start[], __aarch32_sigret_code_end[];
	int sigret_sz = __aarch32_sigret_code_end - __aarch32_sigret_code_start;
	__le32 poison = cpu_to_le32(COMPAT_SIGPAGE_POISON_WORD);
	void *sigpage;

	sigpage = (void *)__get_free_page(GFP_KERNEL);
	if (!sigpage)
		return -ENOMEM;

	memset32(sigpage, (__force u32)poison, PAGE_SIZE / sizeof(poison));
	memcpy(sigpage, __aarch32_sigret_code_start, sigret_sz);
	aarch32_sig_page = virt_to_page(sigpage);
	return 0;
}

static int __init __aarch32_alloc_vdso_pages(void)
{

	if (!IS_ENABLED(CONFIG_COMPAT_VDSO))
		return 0;

	vdso_info[VDSO_ABI_AA32].cm = &aarch32_vdso_maps[AA32_MAP_VDSO];

	return __vdso_init(VDSO_ABI_AA32);
}

static int __init aarch32_alloc_vdso_pages(void)
{
	int ret;

	ret = __aarch32_alloc_vdso_pages();
	if (ret)
		return ret;

	ret = aarch32_alloc_sigpage();
	if (ret)
		return ret;

	return aarch32_alloc_kuser_vdso_page();
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
				       VM_MAYREAD | VM_MAYEXEC |
				       VM_SEALED_SYSMAP,
				       &aarch32_vdso_maps[AA32_MAP_VECTORS]);

	return PTR_ERR_OR_ZERO(ret);
}

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
				       VM_MAYWRITE | VM_MAYEXEC |
				       VM_SEALED_SYSMAP,
				       &aarch32_vdso_maps[AA32_MAP_SIGPAGE]);
	if (IS_ERR(ret))
		goto out;

	mm->context.sigpage = (void *)addr;

out:
	return PTR_ERR_OR_ZERO(ret);
}

int aarch32_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	int ret;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	ret = aarch32_kuser_helpers_setup(mm);
	if (ret)
		goto out;

	if (IS_ENABLED(CONFIG_COMPAT_VDSO)) {
		ret = __setup_additional_pages(VDSO_ABI_AA32, mm, bprm,
					       uses_interp);
		if (ret)
			goto out;
	}

	ret = aarch32_sigreturn_setup(mm);
out:
	mmap_write_unlock(mm);
	return ret;
}
#endif /* CONFIG_COMPAT */

static struct vm_special_mapping aarch64_vdso_map __ro_after_init = {
	.name	= "[vdso]",
	.mremap = vdso_mremap,
};

static int __init vdso_init(void)
{
	vdso_info[VDSO_ABI_AA64].cm = &aarch64_vdso_map;

	return __vdso_init(VDSO_ABI_AA64);
}
arch_initcall(vdso_init);

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	int ret;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	ret = __setup_additional_pages(VDSO_ABI_AA64, mm, bprm, uses_interp);
	mmap_write_unlock(mm);

	return ret;
}
