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
#include <linux/time_namespace.h>
#include <linux/random.h>
#include <vdso/datapage.h>
#include <asm/vdso.h>

extern char vdso64_start[], vdso64_end[];
extern char vdso32_start[], vdso32_end[];

static struct vm_special_mapping vvar_mapping;

static union {
	struct vdso_data	data[CS_BASES];
	u8			page[PAGE_SIZE];
} vdso_data_store __page_aligned_data;

struct vdso_data *vdso_data = vdso_data_store.data;

enum vvar_pages {
	VVAR_DATA_PAGE_OFFSET,
	VVAR_TIMENS_PAGE_OFFSET,
	VVAR_NR_PAGES,
};

#ifdef CONFIG_TIME_NS
struct vdso_data *arch_get_vdso_data(void *vvar_page)
{
	return (struct vdso_data *)(vvar_page);
}

/*
 * The VVAR page layout depends on whether a task belongs to the root or
 * non-root time namespace. Whenever a task changes its namespace, the VVAR
 * page tables are cleared and then they will be re-faulted with a
 * corresponding layout.
 * See also the comment near timens_setup_vdso_data() for details.
 */
int vdso_join_timens(struct task_struct *task, struct time_namespace *ns)
{
	struct mm_struct *mm = task->mm;
	VMA_ITERATOR(vmi, mm, 0);
	struct vm_area_struct *vma;

	mmap_read_lock(mm);
	for_each_vma(vmi, vma) {
		unsigned long size = vma->vm_end - vma->vm_start;

		if (!vma_is_special_mapping(vma, &vvar_mapping))
			continue;
		zap_page_range(vma, vma->vm_start, size);
		break;
	}
	mmap_read_unlock(mm);
	return 0;
}
#endif

static vm_fault_t vvar_fault(const struct vm_special_mapping *sm,
			     struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page *timens_page = find_timens_vvar_page(vma);
	unsigned long addr, pfn;
	vm_fault_t err;

	switch (vmf->pgoff) {
	case VVAR_DATA_PAGE_OFFSET:
		pfn = virt_to_pfn(vdso_data);
		if (timens_page) {
			/*
			 * Fault in VVAR page too, since it will be accessed
			 * to get clock data anyway.
			 */
			addr = vmf->address + VVAR_TIMENS_PAGE_OFFSET * PAGE_SIZE;
			err = vmf_insert_pfn(vma, addr, pfn);
			if (unlikely(err & VM_FAULT_ERROR))
				return err;
			pfn = page_to_pfn(timens_page);
		}
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
		pfn = virt_to_pfn(vdso_data);
		break;
#endif /* CONFIG_TIME_NS */
	default:
		return VM_FAULT_SIGBUS;
	}
	return vmf_insert_pfn(vma, vmf->address, pfn);
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

static struct vm_special_mapping vdso64_mapping = {
	.name = "[vdso]",
	.mremap = vdso_mremap,
};

static struct vm_special_mapping vdso32_mapping = {
	.name = "[vdso]",
	.mremap = vdso_mremap,
};

int vdso_getcpu_init(void)
{
	set_tod_programmable_field(smp_processor_id());
	return 0;
}
early_initcall(vdso_getcpu_init); /* Must be called before SMP init */

static int map_vdso(unsigned long addr, unsigned long vdso_mapping_len)
{
	unsigned long vvar_start, vdso_text_start, vdso_text_len;
	struct vm_special_mapping *vdso_mapping;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int rc;

	BUILD_BUG_ON(VVAR_NR_PAGES != __VVAR_PAGES);
	if (mmap_write_lock_killable(mm))
		return -EINTR;

	if (is_compat_task()) {
		vdso_text_len = vdso32_end - vdso32_start;
		vdso_mapping = &vdso32_mapping;
	} else {
		vdso_text_len = vdso64_end - vdso64_start;
		vdso_mapping = &vdso64_mapping;
	}
	vvar_start = get_unmapped_area(NULL, addr, vdso_mapping_len, 0, 0);
	rc = vvar_start;
	if (IS_ERR_VALUE(vvar_start))
		goto out;
	vma = _install_special_mapping(mm, vvar_start, VVAR_NR_PAGES*PAGE_SIZE,
				       VM_READ|VM_MAYREAD|VM_IO|VM_DONTDUMP|
				       VM_PFNMAP,
				       &vvar_mapping);
	rc = PTR_ERR(vma);
	if (IS_ERR(vma))
		goto out;
	vdso_text_start = vvar_start + VVAR_NR_PAGES * PAGE_SIZE;
	/* VM_MAYWRITE for COW so gdb can set breakpoints */
	vma = _install_special_mapping(mm, vdso_text_start, vdso_text_len,
				       VM_READ|VM_EXEC|
				       VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
				       vdso_mapping);
	if (IS_ERR(vma)) {
		do_munmap(mm, vvar_start, PAGE_SIZE, NULL);
		rc = PTR_ERR(vma);
	} else {
		current->mm->context.vdso_base = vdso_text_start;
		rc = 0;
	}
out:
	mmap_write_unlock(mm);
	return rc;
}

static unsigned long vdso_addr(unsigned long start, unsigned long len)
{
	unsigned long addr, end, offset;

	/*
	 * Round up the start address. It can start out unaligned as a result
	 * of stack start randomization.
	 */
	start = PAGE_ALIGN(start);

	/* Round the lowest possible end address up to a PMD boundary. */
	end = (start + len + PMD_SIZE - 1) & PMD_MASK;
	if (end >= VDSO_BASE)
		end = VDSO_BASE;
	end -= len;

	if (end > start) {
		offset = get_random_u32_below(((end - start) >> PAGE_SHIFT) + 1);
		addr = start + (offset << PAGE_SHIFT);
	} else {
		addr = start;
	}
	return addr;
}

unsigned long vdso_size(void)
{
	unsigned long size = VVAR_NR_PAGES * PAGE_SIZE;

	if (is_compat_task())
		size += vdso32_end - vdso32_start;
	else
		size += vdso64_end - vdso64_start;
	return PAGE_ALIGN(size);
}

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	unsigned long addr = VDSO_BASE;
	unsigned long size = vdso_size();

	if (current->flags & PF_RANDOMIZE)
		addr = vdso_addr(current->mm->start_stack + PAGE_SIZE, size);
	return map_vdso(addr, size);
}

static struct page ** __init vdso_setup_pages(void *start, void *end)
{
	int pages = (end - start) >> PAGE_SHIFT;
	struct page **pagelist;
	int i;

	pagelist = kcalloc(pages + 1, sizeof(struct page *), GFP_KERNEL);
	if (!pagelist)
		panic("%s: Cannot allocate page list for VDSO", __func__);
	for (i = 0; i < pages; i++)
		pagelist[i] = virt_to_page(start + i * PAGE_SIZE);
	return pagelist;
}

static int __init vdso_init(void)
{
	vdso64_mapping.pages = vdso_setup_pages(vdso64_start, vdso64_end);
	if (IS_ENABLED(CONFIG_COMPAT))
		vdso32_mapping.pages = vdso_setup_pages(vdso32_start, vdso32_end);
	return 0;
}
arch_initcall(vdso_init);
