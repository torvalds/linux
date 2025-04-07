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
#include <linux/mman.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vdso_datastore.h>

#include <asm/abi.h>
#include <asm/mips-cps.h>
#include <asm/page.h>
#include <asm/vdso.h>
#include <vdso/helpers.h>
#include <vdso/vsyscall.h>

static_assert(VDSO_NR_PAGES == __VDSO_PAGES);

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
	unsigned long gic_size, size, base, data_addr, vdso_addr, gic_pfn, gic_base;
	struct vm_area_struct *vma;
	int ret;

	if (mmap_write_lock_killable(mm))
		return -EINTR;

	if (IS_ENABLED(CONFIG_MIPS_FP_SUPPORT)) {
		unsigned long unused;

		/* Map delay slot emulation page */
		base = do_mmap(NULL, STACK_TOP, PAGE_SIZE, PROT_READ | PROT_EXEC,
			       MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, 0, 0, &unused,
			       NULL);
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
	size = gic_size + VDSO_NR_PAGES * PAGE_SIZE + image->size;

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
		base += ((unsigned long)vdso_k_time_data - gic_size) & shm_align_mask;
	}

	data_addr = base + gic_size;
	vdso_addr = data_addr + VDSO_NR_PAGES * PAGE_SIZE;

	vma = vdso_install_vvar_mapping(mm, data_addr);
	if (IS_ERR(vma)) {
		ret = PTR_ERR(vma);
		goto out;
	}

	/* Map GIC user page. */
	if (gic_size) {
		gic_base = (unsigned long)mips_gic_base + MIPS_GIC_USER_OFS;
		gic_pfn = PFN_DOWN(__pa(gic_base));
		static const struct vm_special_mapping gic_mapping = {
			.name	= "[gic]",
			.pages	= (struct page **) { NULL },
		};

		vma = _install_special_mapping(mm, base, gic_size, VM_READ | VM_MAYREAD,
					       &gic_mapping);
		if (IS_ERR(vma)) {
			ret = PTR_ERR(vma);
			goto out;
		}

		ret = io_remap_pfn_range(vma, base, gic_pfn, gic_size,
					 pgprot_noncached(vma->vm_page_prot));
		if (ret)
			goto out;
	}

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
