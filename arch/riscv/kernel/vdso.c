// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2004 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 * Copyright (C) 2012 ARM Limited
 * Copyright (C) 2015 Regents of the University of California
 */

#include <linux/elf.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/binfmts.h>
#include <linux/err.h>
#include <asm/page.h>
#include <asm/vdso.h>
#include <linux/time_namespace.h>

#ifdef CONFIG_GENERIC_TIME_VSYSCALL
#include <vdso/datapage.h>
#else
struct vdso_data {
};
#endif

extern char vdso_start[], vdso_end[];
#ifdef CONFIG_COMPAT
extern char compat_vdso_start[], compat_vdso_end[];
#endif

enum vvar_pages {
	VVAR_DATA_PAGE_OFFSET,
	VVAR_TIMENS_PAGE_OFFSET,
	VVAR_NR_PAGES,
};

enum rv_vdso_map {
	RV_VDSO_MAP_VVAR,
	RV_VDSO_MAP_VDSO,
};

#define VVAR_SIZE  (VVAR_NR_PAGES << PAGE_SHIFT)

/*
 * The vDSO data page.
 */
static union {
	struct vdso_data	data;
	u8			page[PAGE_SIZE];
} vdso_data_store __page_aligned_data;
struct vdso_data *vdso_data = &vdso_data_store.data;

struct __vdso_info {
	const char *name;
	const char *vdso_code_start;
	const char *vdso_code_end;
	unsigned long vdso_pages;
	/* Data Mapping */
	struct vm_special_mapping *dm;
	/* Code Mapping */
	struct vm_special_mapping *cm;
};

static int vdso_mremap(const struct vm_special_mapping *sm,
		       struct vm_area_struct *new_vma)
{
	current->mm->context.vdso = (void *)new_vma->vm_start;

	return 0;
}

static void __init __vdso_init(struct __vdso_info *vdso_info)
{
	unsigned int i;
	struct page **vdso_pagelist;
	unsigned long pfn;

	if (memcmp(vdso_info->vdso_code_start, "\177ELF", 4))
		panic("vDSO is not a valid ELF object!\n");

	vdso_info->vdso_pages = (
		vdso_info->vdso_code_end -
		vdso_info->vdso_code_start) >>
		PAGE_SHIFT;

	vdso_pagelist = kcalloc(vdso_info->vdso_pages,
				sizeof(struct page *),
				GFP_KERNEL);
	if (vdso_pagelist == NULL)
		panic("vDSO kcalloc failed!\n");

	/* Grab the vDSO code pages. */
	pfn = sym_to_pfn(vdso_info->vdso_code_start);

	for (i = 0; i < vdso_info->vdso_pages; i++)
		vdso_pagelist[i] = pfn_to_page(pfn + i);

	vdso_info->cm->pages = vdso_pagelist;
}

#ifdef CONFIG_TIME_NS
struct vdso_data *arch_get_vdso_data(void *vvar_page)
{
	return (struct vdso_data *)(vvar_page);
}

/*
 * The vvar mapping contains data for a specific time namespace, so when a task
 * changes namespace we must unmap its vvar data for the old namespace.
 * Subsequent faults will map in data for the new namespace.
 *
 * For more details see timens_setup_vdso_data().
 */
int vdso_join_timens(struct task_struct *task, struct time_namespace *ns)
{
	struct mm_struct *mm = task->mm;
	struct vm_area_struct *vma;
	struct __vdso_info *vdso_info = mm->context.vdso_info;

	mmap_read_lock(mm);

	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		unsigned long size = vma->vm_end - vma->vm_start;

		if (vma_is_special_mapping(vma, vdso_info->dm))
			zap_page_range(vma, vma->vm_start, size);
	}

	mmap_read_unlock(mm);
	return 0;
}

static struct page *find_timens_vvar_page(struct vm_area_struct *vma)
{
	if (likely(vma->vm_mm == current->mm))
		return current->nsproxy->time_ns->vvar_page;

	/*
	 * VM_PFNMAP | VM_IO protect .fault() handler from being called
	 * through interfaces like /proc/$pid/mem or
	 * process_vm_{readv,writev}() as long as there's no .access()
	 * in special_mapping_vmops.
	 * For more details check_vma_flags() and __access_remote_vm()
	 */
	WARN(1, "vvar_page accessed remotely");

	return NULL;
}
#else
static struct page *find_timens_vvar_page(struct vm_area_struct *vma)
{
	return NULL;
}
#endif

static vm_fault_t vvar_fault(const struct vm_special_mapping *sm,
			     struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *timens_page = find_timens_vvar_page(vma);
	unsigned long pfn;

	switch (vmf->pgoff) {
	case VVAR_DATA_PAGE_OFFSET:
		if (timens_page)
			pfn = page_to_pfn(timens_page);
		else
			pfn = sym_to_pfn(vdso_data);
		break;
#ifdef CONFIG_TIME_NS
	case VVAR_TIMENS_PAGE_OFFSET:
		/*
		 * If a task belongs to a time namespace then a namespace
		 * specific VVAR is mapped with the VVAR_DATA_PAGE_OFFSET and
		 * the real VVAR page is mapped with the VVAR_TIMENS_PAGE_OFFSET
		 * offset.
		 * See also the comment near timens_setup_vdso_data().
		 */
		if (!timens_page)
			return VM_FAULT_SIGBUS;
		pfn = sym_to_pfn(vdso_data);
		break;
#endif /* CONFIG_TIME_NS */
	default:
		return VM_FAULT_SIGBUS;
	}

	return vmf_insert_pfn(vma, vmf->address, pfn);
}

static struct vm_special_mapping rv_vdso_maps[] __ro_after_init = {
	[RV_VDSO_MAP_VVAR] = {
		.name   = "[vvar]",
		.fault = vvar_fault,
	},
	[RV_VDSO_MAP_VDSO] = {
		.name   = "[vdso]",
		.mremap = vdso_mremap,
	},
};

static struct __vdso_info vdso_info __ro_after_init = {
	.name = "vdso",
	.vdso_code_start = vdso_start,
	.vdso_code_end = vdso_end,
	.dm = &rv_vdso_maps[RV_VDSO_MAP_VVAR],
	.cm = &rv_vdso_maps[RV_VDSO_MAP_VDSO],
};

#ifdef CONFIG_COMPAT
static struct vm_special_mapping rv_compat_vdso_maps[] __ro_after_init = {
	[RV_VDSO_MAP_VVAR] = {
		.name   = "[vvar]",
		.fault = vvar_fault,
	},
	[RV_VDSO_MAP_VDSO] = {
		.name   = "[vdso]",
		.mremap = vdso_mremap,
	},
};

static struct __vdso_info compat_vdso_info __ro_after_init = {
	.name = "compat_vdso",
	.vdso_code_start = compat_vdso_start,
	.vdso_code_end = compat_vdso_end,
	.dm = &rv_compat_vdso_maps[RV_VDSO_MAP_VVAR],
	.cm = &rv_compat_vdso_maps[RV_VDSO_MAP_VDSO],
};
#endif

static int __init vdso_init(void)
{
	__vdso_init(&vdso_info);
#ifdef CONFIG_COMPAT
	__vdso_init(&compat_vdso_info);
#endif

	return 0;
}
arch_initcall(vdso_init);

static int __setup_additional_pages(struct mm_struct *mm,
				    struct linux_binprm *bprm,
				    int uses_interp,
				    struct __vdso_info *vdso_info)
{
	unsigned long vdso_base, vdso_text_len, vdso_mapping_len;
	void *ret;

	BUILD_BUG_ON(VVAR_NR_PAGES != __VVAR_PAGES);

	vdso_text_len = vdso_info->vdso_pages << PAGE_SHIFT;
	/* Be sure to map the data page */
	vdso_mapping_len = vdso_text_len + VVAR_SIZE;

	vdso_base = get_unmapped_area(NULL, 0, vdso_mapping_len, 0, 0);
	if (IS_ERR_VALUE(vdso_base)) {
		ret = ERR_PTR(vdso_base);
		goto up_fail;
	}

	ret = _install_special_mapping(mm, vdso_base, VVAR_SIZE,
		(VM_READ | VM_MAYREAD | VM_PFNMAP), vdso_info->dm);
	if (IS_ERR(ret))
		goto up_fail;

	vdso_base += VVAR_SIZE;
	mm->context.vdso = (void *)vdso_base;
	mm->context.vdso_info = (void *)vdso_info;

	ret =
	   _install_special_mapping(mm, vdso_base, vdso_text_len,
		(VM_READ | VM_EXEC | VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC),
		vdso_info->cm);

	if (IS_ERR(ret))
		goto up_fail;

	return 0;

up_fail:
	mm->context.vdso = NULL;
	return PTR_ERR(ret);
}

#ifdef CONFIG_COMPAT
int compat_arch_setup_additional_pages(struct linux_binprm *bprm,
				       int uses_interp)
{
	struct mm_struct *mm = current->mm;
	int ret;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	ret = __setup_additional_pages(mm, bprm, uses_interp,
							&compat_vdso_info);
	mmap_write_unlock(mm);

	return ret;
}
#endif

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	int ret;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	ret = __setup_additional_pages(mm, bprm, uses_interp, &vdso_info);
	mmap_write_unlock(mm);

	return ret;
}
