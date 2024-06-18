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
#include <linux/time_namespace.h>
#include <linux/timekeeper_internal.h>

#include <asm/page.h>
#include <asm/vdso.h>
#include <vdso/helpers.h>
#include <vdso/vsyscall.h>
#include <vdso/datapage.h>
#include <generated/vdso-offsets.h>

extern char vdso_start[], vdso_end[];

/* Kernel-provided data used by the VDSO. */
static union vdso_data_store generic_vdso_data __page_aligned_data;

static union {
	u8 page[LOONGARCH_VDSO_DATA_SIZE];
	struct loongarch_vdso_data vdata;
} loongarch_vdso_data __page_aligned_data;

static struct page *vdso_pages[] = { NULL };
struct vdso_data *vdso_data = generic_vdso_data.data;
struct vdso_pcpu_data *vdso_pdata = loongarch_vdso_data.vdata.pdata;

static int vdso_mremap(const struct vm_special_mapping *sm, struct vm_area_struct *new_vma)
{
	current->mm->context.vdso = (void *)(new_vma->vm_start);

	return 0;
}

static vm_fault_t vvar_fault(const struct vm_special_mapping *sm,
			     struct vm_area_struct *vma, struct vm_fault *vmf)
{
	unsigned long pfn;
	struct page *timens_page = find_timens_vvar_page(vma);

	switch (vmf->pgoff) {
	case VVAR_GENERIC_PAGE_OFFSET:
		if (!timens_page)
			pfn = sym_to_pfn(vdso_data);
		else
			pfn = page_to_pfn(timens_page);
		break;
#ifdef CONFIG_TIME_NS
	case VVAR_TIMENS_PAGE_OFFSET:
		/*
		 * If a task belongs to a time namespace then a namespace specific
		 * VVAR is mapped with the VVAR_GENERIC_PAGE_OFFSET and the real
		 * VVAR page is mapped with the VVAR_TIMENS_PAGE_OFFSET offset.
		 * See also the comment near timens_setup_vdso_data().
		 */
		if (!timens_page)
			return VM_FAULT_SIGBUS;
		else
			pfn = sym_to_pfn(vdso_data);
		break;
#endif /* CONFIG_TIME_NS */
	case VVAR_LOONGARCH_PAGES_START ... VVAR_LOONGARCH_PAGES_END:
		pfn = sym_to_pfn(&loongarch_vdso_data) + vmf->pgoff - VVAR_LOONGARCH_PAGES_START;
		break;
	default:
		return VM_FAULT_SIGBUS;
	}

	return vmf_insert_pfn(vma, vmf->address, pfn);
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
		.fault = vvar_fault,
	},
	.offset_sigreturn = vdso_offset_sigreturn,
};

static int __init init_vdso(void)
{
	unsigned long i, cpu, pfn;

	BUG_ON(!PAGE_ALIGNED(vdso_info.vdso));
	BUG_ON(!PAGE_ALIGNED(vdso_info.size));

	for_each_possible_cpu(cpu)
		vdso_pdata[cpu].node = cpu_to_node(cpu);

	pfn = __phys_to_pfn(__pa_symbol(vdso_info.vdso));
	for (i = 0; i < vdso_info.size / PAGE_SIZE; i++)
		vdso_info.code_mapping.pages[i] = pfn_to_page(pfn + i);

	return 0;
}
subsys_initcall(init_vdso);

#ifdef CONFIG_TIME_NS
struct vdso_data *arch_get_vdso_data(void *vvar_page)
{
	return (struct vdso_data *)(vvar_page);
}

/*
 * The vvar mapping contains data for a specific time namespace, so when a
 * task changes namespace we must unmap its vvar data for the old namespace.
 * Subsequent faults will map in data for the new namespace.
 *
 * For more details see timens_setup_vdso_data().
 */
int vdso_join_timens(struct task_struct *task, struct time_namespace *ns)
{
	struct mm_struct *mm = task->mm;
	struct vm_area_struct *vma;

	VMA_ITERATOR(vmi, mm, 0);

	mmap_read_lock(mm);
	for_each_vma(vmi, vma) {
		if (vma_is_special_mapping(vma, &vdso_info.data_mapping))
			zap_vma_pages(vma);
	}
	mmap_read_unlock(mm);

	return 0;
}
#endif

static unsigned long vdso_base(void)
{
	unsigned long base = STACK_TOP;

	if (current->flags & PF_RANDOMIZE) {
		base += get_random_u32_below(VDSO_RANDOMIZE_SIZE);
		base = PAGE_ALIGN(base);
	}

	return base;
}

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	int ret;
	unsigned long size, data_addr, vdso_addr;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct loongarch_vdso_info *info = current->thread.vdso;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	/*
	 * Determine total area size. This includes the VDSO data itself
	 * and the data pages.
	 */
	size = VVAR_SIZE + info->size;

	data_addr = get_unmapped_area(NULL, vdso_base(), size, 0, 0);
	if (IS_ERR_VALUE(data_addr)) {
		ret = data_addr;
		goto out;
	}

	vma = _install_special_mapping(mm, data_addr, VVAR_SIZE,
				       VM_READ | VM_MAYREAD | VM_PFNMAP,
				       &info->data_mapping);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto out;
	}

	vdso_addr = data_addr + VVAR_SIZE;
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
