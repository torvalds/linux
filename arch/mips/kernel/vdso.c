/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/binfmts.h>
#include <linux/elf.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/irqchip/mips-gic.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/timekeeper_internal.h>

#include <asm/abi.h>
#include <asm/vdso.h>

/* Kernel-provided data used by the VDSO. */
static union mips_vdso_data vdso_data __page_aligned_data;

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

	BUG_ON(!PAGE_ALIGNED(image->data));
	BUG_ON(!PAGE_ALIGNED(image->size));

	num_pages = image->size / PAGE_SIZE;

	for (i = 0; i < num_pages; i++) {
		image->mapping.pages[i] =
			virt_to_page(image->data + (i * PAGE_SIZE));
	}
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

void update_vsyscall(struct timekeeper *tk)
{
	vdso_data_write_begin(&vdso_data);

	vdso_data.xtime_sec = tk->xtime_sec;
	vdso_data.xtime_nsec = tk->tkr_mono.xtime_nsec;
	vdso_data.wall_to_mono_sec = tk->wall_to_monotonic.tv_sec;
	vdso_data.wall_to_mono_nsec = tk->wall_to_monotonic.tv_nsec;
	vdso_data.cs_shift = tk->tkr_mono.shift;

	vdso_data.clock_mode = tk->tkr_mono.clock->archdata.vdso_clock_mode;
	if (vdso_data.clock_mode != VDSO_CLOCK_NONE) {
		vdso_data.cs_mult = tk->tkr_mono.mult;
		vdso_data.cs_cycle_last = tk->tkr_mono.cycle_last;
		vdso_data.cs_mask = tk->tkr_mono.mask;
	}

	vdso_data_write_end(&vdso_data);
}

void update_vsyscall_tz(void)
{
	if (vdso_data.clock_mode != VDSO_CLOCK_NONE) {
		vdso_data.tz_minuteswest = sys_tz.tz_minuteswest;
		vdso_data.tz_dsttime = sys_tz.tz_dsttime;
	}
}

int arch_setup_additional_pages(struct linux_binprm *bprm, int uses_interp)
{
	struct mips_vdso_image *image = current->thread.abi->vdso;
	struct mm_struct *mm = current->mm;
	unsigned long gic_size, vvar_size, size, base, data_addr, vdso_addr;
	struct vm_area_struct *vma;
	struct resource gic_res;
	int ret;

	if (down_write_killable(&mm->mmap_sem))
		return -EINTR;

	/* Map delay slot emulation page */
	base = mmap_region(NULL, STACK_TOP, PAGE_SIZE,
			   VM_READ|VM_WRITE|VM_EXEC|
			   VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC,
			   0);
	if (IS_ERR_VALUE(base)) {
		ret = base;
		goto out;
	}

	/*
	 * Determine total area size. This includes the VDSO data itself, the
	 * data page, and the GIC user page if present. Always create a mapping
	 * for the GIC user area if the GIC is present regardless of whether it
	 * is the current clocksource, in case it comes into use later on. We
	 * only map a page even though the total area is 64K, as we only need
	 * the counter registers at the start.
	 */
	gic_size = gic_present ? PAGE_SIZE : 0;
	vvar_size = gic_size + PAGE_SIZE;
	size = vvar_size + image->size;

	base = get_unmapped_area(NULL, 0, size, 0, 0);
	if (IS_ERR_VALUE(base)) {
		ret = base;
		goto out;
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
		ret = gic_get_usm_range(&gic_res);
		if (ret)
			goto out;

		ret = io_remap_pfn_range(vma, base,
					 gic_res.start >> PAGE_SHIFT,
					 gic_size,
					 pgprot_noncached(PAGE_READONLY));
		if (ret)
			goto out;
	}

	/* Map data page. */
	ret = remap_pfn_range(vma, data_addr,
			      virt_to_phys(&vdso_data) >> PAGE_SHIFT,
			      PAGE_SIZE, PAGE_READONLY);
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
	up_write(&mm->mmap_sem);
	return ret;
}
