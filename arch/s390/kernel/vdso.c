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
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/random.h>
#include <linux/vdso_datastore.h>
#include <vdso/datapage.h>
#include <asm/vdso/vsyscall.h>
#include <asm/alternative.h>
#include <asm/vdso.h>

extern char vdso64_start[], vdso64_end[];
extern char vdso32_start[], vdso32_end[];

static int vdso_mremap(const struct vm_special_mapping *sm,
		       struct vm_area_struct *vma)
{
	current->mm->context.vdso_base = vma->vm_start;
	return 0;
}

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

	BUILD_BUG_ON(VDSO_NR_PAGES != __VDSO_PAGES);
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
	vma = vdso_install_vvar_mapping(mm, vvar_start);
	rc = PTR_ERR(vma);
	if (IS_ERR(vma))
		goto out;
	vdso_text_start = vvar_start + VDSO_NR_PAGES * PAGE_SIZE;
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

unsigned long vdso_text_size(void)
{
	unsigned long size;

	if (is_compat_task())
		size = vdso32_end - vdso32_start;
	else
		size = vdso64_end - vdso64_start;
	return PAGE_ALIGN(size);
}

unsigned long vdso_size(void)
{
	return vdso_text_size() + VDSO_NR_PAGES * PAGE_SIZE;
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

static void vdso_apply_alternatives(void)
{
	const struct elf64_shdr *alt, *shdr;
	struct alt_instr *start, *end;
	const struct elf64_hdr *hdr;

	hdr = (struct elf64_hdr *)vdso64_start;
	shdr = (void *)hdr + hdr->e_shoff;
	alt = find_section(hdr, shdr, ".altinstructions");
	if (!alt)
		return;
	start = (void *)hdr + alt->sh_offset;
	end = (void *)hdr + alt->sh_offset + alt->sh_size;
	apply_alternatives(start, end);
}

static int __init vdso_init(void)
{
	vdso_apply_alternatives();
	vdso64_mapping.pages = vdso_setup_pages(vdso64_start, vdso64_end);
	if (IS_ENABLED(CONFIG_COMPAT))
		vdso32_mapping.pages = vdso_setup_pages(vdso32_start, vdso32_end);
	return 0;
}
arch_initcall(vdso_init);
