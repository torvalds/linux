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
#include <vdso/datapage.h>
#include <asm/vdso.h>

extern char vdso64_start[], vdso64_end[];
static unsigned int vdso_pages;

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

unsigned int __read_mostly vdso_enabled = 1;

static int __init vdso_setup(char *str)
{
	bool enabled;

	if (!kstrtobool(str, &enabled))
		vdso_enabled = enabled;
	return 1;
}
__setup("vdso=", vdso_setup);

#ifdef CONFIG_TIME_NS
struct vdso_data *arch_get_vdso_data(void *vvar_page)
{
	return (struct vdso_data *)(vvar_page);
}

static struct page *find_timens_vvar_page(struct vm_area_struct *vma)
{
	if (likely(vma->vm_mm == current->mm))
		return current->nsproxy->time_ns->vvar_page;
	/*
	 * VM_PFNMAP | VM_IO protect .fault() handler from being called
	 * through interfaces like /proc/$pid/mem or
	 * process_vm_{readv,writev}() as long as there's no .access()
	 * in special_mapping_vmops().
	 * For more details check_vma_flags() and __access_remote_vm()
	 */
	WARN(1, "vvar_page accessed remotely");
	return NULL;
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
	struct vm_area_struct *vma;

	mmap_read_lock(mm);
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		unsigned long size = vma->vm_end - vma->vm_start;

		if (!vma_is_special_mapping(vma, &vvar_mapping))
			continue;
		zap_page_range(vma, vma->vm_start, size);
		break;
	}
	mmap_read_unlock(mm);
	return 0;
}
#else
static inline struct page *find_timens_vvar_page(struct vm_area_struct *vma)
{
	return NULL;
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

static struct vm_special_mapping vdso_mapping = {
	.name = "[vdso]",
	.mremap = vdso_mremap,
};

int vdso_getcpu_init(void)
{
	set_tod_programmable_field(smp_processor_id());
	return 0;
}
early_initcall(vdso_getcpu_init); /* Must be called before SMP init */

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	unsigned long vdso_text_len, vdso_mapping_len;
	unsigned long vvar_start, vdso_text_start;
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int rc;

	BUILD_BUG_ON(VVAR_NR_PAGES != __VVAR_PAGES);
	if (!vdso_enabled || is_compat_task())
		return 0;
	if (mmap_write_lock_killable(mm))
		return -EINTR;
	vdso_text_len = vdso_pages << PAGE_SHIFT;
	vdso_mapping_len = vdso_text_len + VVAR_NR_PAGES * PAGE_SIZE;
	vvar_start = get_unmapped_area(NULL, 0, vdso_mapping_len, 0, 0);
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
				       &vdso_mapping);
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

static int __init vdso_init(void)
{
	struct page **pages;
	int i;

	vdso_pages = (vdso64_end - vdso64_start) >> PAGE_SHIFT;
	pages = kcalloc(vdso_pages + 1, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		vdso_enabled = 0;
		return -ENOMEM;
	}
	for (i = 0; i < vdso_pages; i++)
		pages[i] = virt_to_page(vdso64_start + i * PAGE_SIZE);
	pages[vdso_pages] = NULL;
	vdso_mapping.pages = pages;
	return 0;
}
arch_initcall(vdso_init);
