// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
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
#include <linux/timekeeper_internal.h>

#include <asm/abi.h>
#include <asm/mips-cps.h>
#include <asm/page.h>
#include <asm/vdso.h>
#include <vdso/helpers.h>
#include <vdso/vsyscall.h>

/* Kernel-provided data used by the VDSO. */
static union vdso_data_store mips_vdso_data __page_aligned_data;
struct vdso_data *vdso_data = mips_vdso_data.data;

/*
 * Mapping for the VDSO data/GIC pages. The real pages are mapped manually, as
 * what we map and where within the area they are mapped is determined at
 * runtime.
 */
static struct page *no_pages[] = { NULL };
static struct vm_special_mapping vdso_vvar_mapping = {
	.name = "[vvar]",
	.pages = no_pages,
};

static void __init init_vdso_image(struct mips_vdso_image *image)
{
	unsigned long num_pages, i;
	unsigned long data_pfn;

	BUG_ON(!PAGE_ALIGNED(image->data));
	BUG_ON(!PAGE_ALIGNED(image->size));

	num_pages = image->size / PAGE_SIZE;

	data_pfn = __phys_to_pfn(__pa_symbol(image->data));
	for (i = 0; i < num_pages; i++)
		image->mapping.pages[i] = pfn_to_page(data_pfn + i);
}

static int __init init_vdso(void)
{
	init_vdso_image(&vdso_image);

#ifdef CONFIG_MIPS32_O32
	init_vdso_image(&vdso_image_o32);
#endif

#ifdef CONFIG_MIPS32_N32
	init_vdso_image(&vdso_image_n32);
#endif

	return 0;
}
subsys_initcall(init_vdso);

static unsigned long vdso_base(void)
{
	unsigned long base = STACK_TOP;

	if (IS_ENABLED(CONFIG_MIPS_FP_SUPPORT)) {
		/* Skip the delay slot emulation page */
		base += PAGE_SIZE;
	}

	if (current->flags & PF_RANDOMIZE) {
		base += get_random_u32_below(VDSO_RANDOMIZE_SIZE);
		base = PAGE_ALIGN(base);
	}

	return base;
}

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mips_vdso_image *image = current->thread.abi->vdso;
	struct mm_struct *mm = current->mm;
	unsigned long gic_size, vvar_size, size, base, data_addr, vdso_addr, gic_pfn, gic_base;
	struct vm_area_struct *vma;
	int ret;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	if (IS_ENABLED(CONFIG_MIPS_FP_SUPPORT)) {
		/* Map delay slot emulation page */
		base = mmap_region(NULL, STACK_TOP, PAGE_SIZE,
				VM_READ | VM_EXEC |
				VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC,
				0, NULL);
		if (IS_ERR_VALUE(base)) {
			ret = base;
			goto out;
		}
	}

	/*
	 * Determine total area size. This includes the VDSO data itself, the
	 * data page, and the GIC user page if present. Always create a mapping
	 * for the GIC user area if the GIC is present regardless of whether it
	 * is the current clocksource, in case it comes into use later on. We
	 * only map a page even though the total area is 64K, as we only need
	 * the counter registers at the start.
	 */
	gic_size = mips_gic_present() ? PAGE_SIZE : 0;
	vvar_size = gic_size + PAGE_SIZE;
	size = vvar_size + image->size;

	/*
	 * Find a region that's large enough for us to perform the
	 * colour-matching alignment below.
	 */
	if (cpu_has_dc_aliases)
		size += shm_align_mask + 1;

	base = get_unmapped_area(NULL, vdso_base(), size, 0, 0);
	if (IS_ERR_VALUE(base)) {
		ret = base;
		goto out;
	}

	/*
	 * If we suffer from dcache aliasing, ensure that the VDSO data page
	 * mapping is coloured the same as the kernel's mapping of that memory.
	 * This ensures that when the kernel updates the VDSO data userland
	 * will observe it without requiring cache invalidations.
	 */
	if (cpu_has_dc_aliases) {
		base = __ALIGN_MASK(base, shm_align_mask);
		base += ((unsigned long)vdso_data - gic_size) & shm_align_mask;
	}

	data_addr = base + gic_size;
	vdso_addr = data_addr + PAGE_SIZE;

	vma = _install_special_mapping(mm, base, vvar_size,
				       VM_READ | VM_MAYREAD,
				       &vdso_vvar_mapping);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto out;
	}

	/* Map GIC user page. */
	if (gic_size) {
		gic_base = (unsigned long)mips_gic_base + MIPS_GIC_USER_OFS;
		gic_pfn = PFN_DOWN(__pa(gic_base));

		ret = io_remap_pfn_range(vma, base, gic_pfn, gic_size,
					 pgprot_noncached(vma->vm_page_prot));
		if (ret)
			goto out;
	}

	/* Map data page. */
	ret = remap_pfn_range(vma, data_addr,
			      virt_to_phys(vdso_data) >> PAGE_SHIFT,
			      PAGE_SIZE, vma->vm_page_prot);
	if (ret)
		goto out;

	/* Map VDSO image. */
	vma = _install_special_mapping(mm, vdso_addr, image->size,
				       VM_READ | VM_EXEC |
				       VM_MAYREAD | VM_MAYWRITE | VM_MAYEXEC,
				       &image->mapping);
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
