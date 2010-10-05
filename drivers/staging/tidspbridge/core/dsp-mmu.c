/*
 * dsp-mmu.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * DSP iommu.
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <dspbridge/host_os.h>
#include <plat/dmtimer.h>
#include <dspbridge/dbdefs.h>
#include <dspbridge/dev.h>
#include <dspbridge/io_sm.h>
#include <dspbridge/dspdeh.h>
#include "_tiomap.h"

#include <dspbridge/dsp-mmu.h>

#define MMU_CNTL_TWL_EN		(1 << 2)

static struct tasklet_struct mmu_tasklet;

#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
static void mmu_fault_print_stack(struct bridge_dev_context *dev_context)
{
	void *dummy_addr;
	u32 fa, tmp;
	struct iotlb_entry e;
	struct iommu *mmu = dev_context->dsp_mmu;
	dummy_addr = (void *)__get_free_page(GFP_ATOMIC);

	/*
	 * Before acking the MMU fault, let's make sure MMU can only
	 * access entry #0. Then add a new entry so that the DSP OS
	 * can continue in order to dump the stack.
	 */
	tmp = iommu_read_reg(mmu, MMU_CNTL);
	tmp &= ~MMU_CNTL_TWL_EN;
	iommu_write_reg(mmu, tmp, MMU_CNTL);
	fa = iommu_read_reg(mmu, MMU_FAULT_AD);
	e.da = fa & PAGE_MASK;
	e.pa = virt_to_phys(dummy_addr);
	e.valid = 1;
	e.prsvd = 1;
	e.pgsz = IOVMF_PGSZ_4K & MMU_CAM_PGSZ_MASK;
	e.endian = MMU_RAM_ENDIAN_LITTLE;
	e.elsz = MMU_RAM_ELSZ_32;
	e.mixed = 0;

	load_iotlb_entry(mmu, &e);

	dsp_clk_enable(DSP_CLK_GPT8);

	dsp_gpt_wait_overflow(DSP_CLK_GPT8, 0xfffffffe);

	/* Clear MMU interrupt */
	tmp = iommu_read_reg(mmu, MMU_IRQSTATUS);
	iommu_write_reg(mmu, tmp, MMU_IRQSTATUS);

	dump_dsp_stack(dev_context);
	dsp_clk_disable(DSP_CLK_GPT8);

	iopgtable_clear_entry(mmu, fa);
	free_page((unsigned long)dummy_addr);
}
#endif


static void fault_tasklet(unsigned long data)
{
	struct iommu *mmu = (struct iommu *)data;
	struct bridge_dev_context *dev_ctx;
	struct deh_mgr *dm;
	u32 fa;
	dev_get_deh_mgr(dev_get_first(), &dm);
	dev_get_bridge_context(dev_get_first(), &dev_ctx);

	if (!dm || !dev_ctx)
		return;

	fa = iommu_read_reg(mmu, MMU_FAULT_AD);

#ifdef CONFIG_TIDSPBRIDGE_BACKTRACE
	print_dsp_trace_buffer(dev_ctx);
	dump_dl_modules(dev_ctx);
	mmu_fault_print_stack(dev_ctx);
#endif

	bridge_deh_notify(dm, DSP_MMUFAULT, fa);
}

/*
 *  ======== mmu_fault_isr ========
 *      ISR to be triggered by a DSP MMU fault interrupt.
 */
static int mmu_fault_callback(struct iommu *mmu)
{
	if (!mmu)
		return -EPERM;

	iommu_write_reg(mmu, 0, MMU_IRQENABLE);
	tasklet_schedule(&mmu_tasklet);
	return 0;
}

/**
 * dsp_mmu_init() - initialize dsp_mmu module and returns a handle
 *
 * This function initialize dsp mmu module and returns a struct iommu
 * handle to use it for dsp maps.
 *
 */
struct iommu *dsp_mmu_init()
{
	struct iommu *mmu;

	mmu = iommu_get("iva2");

	if (!IS_ERR(mmu)) {
		tasklet_init(&mmu_tasklet, fault_tasklet, (unsigned long)mmu);
		mmu->isr = mmu_fault_callback;
	}

	return mmu;
}

/**
 * dsp_mmu_exit() - destroy dsp mmu module
 * @mmu:	Pointer to iommu handle.
 *
 * This function destroys dsp mmu module.
 *
 */
void dsp_mmu_exit(struct iommu *mmu)
{
	if (mmu)
		iommu_put(mmu);
	tasklet_kill(&mmu_tasklet);
}

/**
 * user_va2_pa() - get physical address from userspace address.
 * @mm:		mm_struct Pointer of the process.
 * @address:	Virtual user space address.
 *
 */
static u32 user_va2_pa(struct mm_struct *mm, u32 address)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *ptep, pte;

	pgd = pgd_offset(mm, address);
	if (!(pgd_none(*pgd) || pgd_bad(*pgd))) {
		pmd = pmd_offset(pgd, address);
		if (!(pmd_none(*pmd) || pmd_bad(*pmd))) {
			ptep = pte_offset_map(pmd, address);
			if (ptep) {
				pte = *ptep;
				if (pte_present(pte))
					return pte & PAGE_MASK;
			}
		}
	}

	return 0;
}

/**
 * get_io_pages() - pin and get pages of io user's buffer.
 * @mm:		mm_struct Pointer of the process.
 * @uva:		Virtual user space address.
 * @pages	Pages to be pined.
 * @usr_pgs	struct page array pointer where the user pages will be stored
 *
 */
static int get_io_pages(struct mm_struct *mm, u32 uva, unsigned pages,
						struct page **usr_pgs)
{
	u32 pa;
	int i;
	struct page *pg;

	for (i = 0; i < pages; i++) {
		pa = user_va2_pa(mm, uva);

		if (!pfn_valid(__phys_to_pfn(pa)))
			break;

		pg = phys_to_page(pa);
		usr_pgs[i] = pg;
		get_page(pg);
	}
	return i;
}

/**
 * user_to_dsp_map() - maps user to dsp virtual address
 * @mmu:	Pointer to iommu handle.
 * @uva:		Virtual user space address.
 * @da		DSP address
 * @size		Buffer size to map.
 * @usr_pgs	struct page array pointer where the user pages will be stored
 *
 * This function maps a user space buffer into DSP virtual address.
 *
 */
u32 user_to_dsp_map(struct iommu *mmu, u32 uva, u32 da, u32 size,
				struct page **usr_pgs)
{
	int res, w;
	unsigned pages, i;
	struct vm_area_struct *vma;
	struct mm_struct *mm = current->mm;
	struct sg_table *sgt;
	struct scatterlist *sg;

	if (!size || !usr_pgs)
		return -EINVAL;

	pages = size / PG_SIZE4K;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, uva);
	while (vma && (uva + size > vma->vm_end))
		vma = find_vma(mm, vma->vm_end + 1);

	if (!vma) {
		pr_err("%s: Failed to get VMA region for 0x%x (%d)\n",
						__func__, uva, size);
		up_read(&mm->mmap_sem);
		return -EINVAL;
	}
	if (vma->vm_flags & (VM_WRITE | VM_MAYWRITE))
		w = 1;

	if (vma->vm_flags & VM_IO)
		i = get_io_pages(mm, uva, pages, usr_pgs);
	else
		i = get_user_pages(current, mm, uva, pages, w, 1,
							usr_pgs, NULL);
	up_read(&mm->mmap_sem);

	if (i < 0)
		return i;

	if (i < pages) {
		res = -EFAULT;
		goto err_pages;
	}

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		res = -ENOMEM;
		goto err_pages;
	}

	res = sg_alloc_table(sgt, pages, GFP_KERNEL);

	if (res < 0)
		goto err_sg;

	for_each_sg(sgt->sgl, sg, sgt->nents, i)
		sg_set_page(sg, usr_pgs[i], PAGE_SIZE, 0);

	da = iommu_vmap(mmu, da, sgt, IOVMF_ENDIAN_LITTLE | IOVMF_ELSZ_32);

	if (!IS_ERR_VALUE(da))
		return da;
	res = (int)da;

	sg_free_table(sgt);
err_sg:
	kfree(sgt);
	i = pages;
err_pages:
	while (i--)
		put_page(usr_pgs[i]);
	return res;
}

/**
 * user_to_dsp_unmap() - unmaps DSP virtual buffer.
 * @mmu:	Pointer to iommu handle.
 * @da		DSP address
 *
 * This function unmaps a user space buffer into DSP virtual address.
 *
 */
int user_to_dsp_unmap(struct iommu *mmu, u32 da)
{
	unsigned i;
	struct sg_table *sgt;
	struct scatterlist *sg;

	sgt = iommu_vunmap(mmu, da);
	if (!sgt)
		return -EFAULT;

	for_each_sg(sgt->sgl, sg, sgt->nents, i)
		put_page(sg_page(sg));
	sg_free_table(sgt);
	kfree(sgt);

	return 0;
}
