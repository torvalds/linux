// SPDX-License-Identifier: GPL-2.0
/*
 * vdso setup for s390
 *
 *  Copyright IBM Corp. 2008
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/elf.h>
#include <linux/security.h>
#include <linux/bootmem.h>
#include <linux/compat.h>
#include <asm/asm-offsets.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/sections.h>
#include <asm/vdso.h>
#include <asm/facility.h>

#ifdef CONFIG_COMPAT
extern char vdso32_start, vdso32_end;
static void *vdso32_kbase = &vdso32_start;
static unsigned int vdso32_pages;
static struct page **vdso32_pagelist;
#endif

extern char vdso64_start, vdso64_end;
static void *vdso64_kbase = &vdso64_start;
static unsigned int vdso64_pages;
static struct page **vdso64_pagelist;

/*
 * Should the kernel map a VDSO page into processes and pass its
 * address down to glibc upon exec()?
 */
unsigned int __read_mostly vdso_enabled = 1;

static int vdso_fault(const struct vm_special_mapping *sm,
		      struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct page **vdso_pagelist;
	unsigned long vdso_pages;

	vdso_pagelist = vdso64_pagelist;
	vdso_pages = vdso64_pages;
#ifdef CONFIG_COMPAT
	if (is_compat_task()) {
		vdso_pagelist = vdso32_pagelist;
		vdso_pages = vdso32_pages;
	}
#endif

	if (vmf->pgoff >= vdso_pages)
		return VM_FAULT_SIGBUS;

	vmf->page = vdso_pagelist[vmf->pgoff];
	get_page(vmf->page);
	return 0;
}

static int vdso_mremap(const struct vm_special_mapping *sm,
		       struct vm_area_struct *vma)
{
	unsigned long vdso_pages;

	vdso_pages = vdso64_pages;
#ifdef CONFIG_COMPAT
	if (is_compat_task())
		vdso_pages = vdso32_pages;
#endif

	if ((vdso_pages << PAGE_SHIFT) != vma->vm_end - vma->vm_start)
		return -EINVAL;

	if (WARN_ON_ONCE(current->mm != vma->vm_mm))
		return -EFAULT;

	current->mm->context.vdso_base = vma->vm_start;
	return 0;
}

static const struct vm_special_mapping vdso_mapping = {
	.name = "[vdso]",
	.fault = vdso_fault,
	.mremap = vdso_mremap,
};

static int __init vdso_setup(char *s)
{
	unsigned long val;
	int rc;

	rc = 0;
	if (strncmp(s, "on", 3) == 0)
		vdso_enabled = 1;
	else if (strncmp(s, "off", 4) == 0)
		vdso_enabled = 0;
	else {
		rc = kstrtoul(s, 0, &val);
		vdso_enabled = rc ? 0 : !!val;
	}
	return !rc;
}
__setup("vdso=", vdso_setup);

/*
 * The vdso data page
 */
static union {
	struct vdso_data	data;
	u8			page[PAGE_SIZE];
} vdso_data_store __page_aligned_data;
struct vdso_data *vdso_data = &vdso_data_store.data;

/*
 * Setup vdso data page.
 */
static void __init vdso_init_data(struct vdso_data *vd)
{
	vd->ectg_available = test_facility(31);
}

/*
 * Allocate/free per cpu vdso data.
 */
#define SEGMENT_ORDER	2

/*
 * The initial vdso_data structure for the boot CPU. Eventually
 * it is replaced with a properly allocated structure in vdso_init.
 * This is necessary because a valid S390_lowcore.vdso_per_cpu_data
 * pointer is required to be able to return from an interrupt or
 * program check. See the exit paths in entry.S.
 */
struct vdso_data boot_vdso_data __initdata;

void __init vdso_alloc_boot_cpu(struct lowcore *lowcore)
{
	lowcore->vdso_per_cpu_data = (unsigned long) &boot_vdso_data;
}

int vdso_alloc_per_cpu(struct lowcore *lowcore)
{
	unsigned long segment_table, page_table, page_frame;
	struct vdso_per_cpu_data *vd;

	segment_table = __get_free_pages(GFP_KERNEL, SEGMENT_ORDER);
	page_table = get_zeroed_page(GFP_KERNEL);
	page_frame = get_zeroed_page(GFP_KERNEL);
	if (!segment_table || !page_table || !page_frame)
		goto out;
	arch_set_page_dat(virt_to_page(segment_table), SEGMENT_ORDER);
	arch_set_page_dat(virt_to_page(page_table), 0);

	/* Initialize per-cpu vdso data page */
	vd = (struct vdso_per_cpu_data *) page_frame;
	vd->cpu_nr = lowcore->cpu_nr;
	vd->node_id = cpu_to_node(vd->cpu_nr);

	/* Set up page table for the vdso address space */
	memset64((u64 *)segment_table, _SEGMENT_ENTRY_EMPTY, _CRST_ENTRIES);
	memset64((u64 *)page_table, _PAGE_INVALID, PTRS_PER_PTE);

	*(unsigned long *) segment_table = _SEGMENT_ENTRY + page_table;
	*(unsigned long *) page_table = _PAGE_PROTECT + page_frame;

	lowcore->vdso_asce = segment_table +
		_ASCE_TABLE_LENGTH + _ASCE_USER_BITS + _ASCE_TYPE_SEGMENT;
	lowcore->vdso_per_cpu_data = page_frame;

	return 0;

out:
	free_page(page_frame);
	free_page(page_table);
	free_pages(segment_table, SEGMENT_ORDER);
	return -ENOMEM;
}

void vdso_free_per_cpu(struct lowcore *lowcore)
{
	unsigned long segment_table, page_table, page_frame;

	segment_table = lowcore->vdso_asce & PAGE_MASK;
	page_table = *(unsigned long *) segment_table;
	page_frame = *(unsigned long *) page_table;

	free_page(page_frame);
	free_page(page_table);
	free_pages(segment_table, SEGMENT_ORDER);
}

/*
 * This is called from binfmt_elf, we create the special vma for the
 * vDSO and insert it into the mm struct tree
 */
int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long vdso_pages;
	unsigned long vdso_base;
	int rc;

	if (!vdso_enabled)
		return 0;
	/*
	 * Only map the vdso for dynamically linked elf binaries.
	 */
	if (!uses_interp)
		return 0;

	vdso_pages = vdso64_pages;
#ifdef CONFIG_COMPAT
	if (is_compat_task())
		vdso_pages = vdso32_pages;
#endif
	/*
	 * vDSO has a problem and was disabled, just don't "enable" it for
	 * the process
	 */
	if (vdso_pages == 0)
		return 0;

	/*
	 * pick a base address for the vDSO in process space. We try to put
	 * it at vdso_base which is the "natural" base for it, but we might
	 * fail and end up putting it elsewhere.
	 */
	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;
	vdso_base = get_unmapped_area(NULL, 0, vdso_pages << PAGE_SHIFT, 0, 0);
	if (IS_ERR_VALUE(vdso_base)) {
		rc = vdso_base;
		goto out_up;
	}

	/*
	 * our vma flags don't have VM_WRITE so by default, the process
	 * isn't allowed to write those pages.
	 * gdb can break that with ptrace interface, and thus trigger COW
	 * on those pages but it's then your responsibility to never do that
	 * on the "data" page of the vDSO or you'll stop getting kernel
	 * updates and your nice userland gettimeofday will be totally dead.
	 * It's fine to use that for setting breakpoints in the vDSO code
	 * pages though.
	 */
	vma = _install_special_mapping(mm, vdso_base, vdso_pages << PAGE_SHIFT,
				       VM_READ|VM_EXEC|
				       VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
				       &vdso_mapping);
	if (IS_ERR(vma)) {
		rc = PTR_ERR(vma);
		goto out_up;
	}

	current->mm->context.vdso_base = vdso_base;
	rc = 0;

out_up:
	up_write(&mm->mmap_sem);
	return rc;
}

static int __init vdso_init(void)
{
	int i;

	vdso_init_data(vdso_data);
#ifdef CONFIG_COMPAT
	/* Calculate the size of the 32 bit vDSO */
	vdso32_pages = ((&vdso32_end - &vdso32_start
			 + PAGE_SIZE - 1) >> PAGE_SHIFT) + 1;

	/* Make sure pages are in the correct state */
	vdso32_pagelist = kzalloc(sizeof(struct page *) * (vdso32_pages + 1),
				  GFP_KERNEL);
	BUG_ON(vdso32_pagelist == NULL);
	for (i = 0; i < vdso32_pages - 1; i++) {
		struct page *pg = virt_to_page(vdso32_kbase + i*PAGE_SIZE);
		ClearPageReserved(pg);
		get_page(pg);
		vdso32_pagelist[i] = pg;
	}
	vdso32_pagelist[vdso32_pages - 1] = virt_to_page(vdso_data);
	vdso32_pagelist[vdso32_pages] = NULL;
#endif

	/* Calculate the size of the 64 bit vDSO */
	vdso64_pages = ((&vdso64_end - &vdso64_start
			 + PAGE_SIZE - 1) >> PAGE_SHIFT) + 1;

	/* Make sure pages are in the correct state */
	vdso64_pagelist = kzalloc(sizeof(struct page *) * (vdso64_pages + 1),
				  GFP_KERNEL);
	BUG_ON(vdso64_pagelist == NULL);
	for (i = 0; i < vdso64_pages - 1; i++) {
		struct page *pg = virt_to_page(vdso64_kbase + i*PAGE_SIZE);
		ClearPageReserved(pg);
		get_page(pg);
		vdso64_pagelist[i] = pg;
	}
	vdso64_pagelist[vdso64_pages - 1] = virt_to_page(vdso_data);
	vdso64_pagelist[vdso64_pages] = NULL;
	if (vdso_alloc_per_cpu(&S390_lowcore))
		BUG();

	get_page(virt_to_page(vdso_data));

	return 0;
}
early_initcall(vdso_init);
