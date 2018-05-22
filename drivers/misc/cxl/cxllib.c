/*
 * Copyright 2017 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/hugetlb.h>
#include <linux/sched/mm.h>
#include <asm/pnv-pci.h>
#include <misc/cxllib.h>

#include "cxl.h"

#define CXL_INVALID_DRA                 ~0ull
#define CXL_DUMMY_READ_SIZE             128
#define CXL_DUMMY_READ_ALIGN            8
#define CXL_CAPI_WINDOW_START           0x2000000000000ull
#define CXL_CAPI_WINDOW_LOG_SIZE        48
#define CXL_XSL_CONFIG_CURRENT_VERSION  CXL_XSL_CONFIG_VERSION1


bool cxllib_slot_is_supported(struct pci_dev *dev, unsigned long flags)
{
	int rc;
	u32 phb_index;
	u64 chip_id, capp_unit_id;

	/* No flags currently supported */
	if (flags)
		return false;

	if (!cpu_has_feature(CPU_FTR_HVMODE))
		return false;

	if (!cxl_is_power9())
		return false;

	if (cxl_slot_is_switched(dev))
		return false;

	/* on p9, some pci slots are not connected to a CAPP unit */
	rc = cxl_calc_capp_routing(dev, &chip_id, &phb_index, &capp_unit_id);
	if (rc)
		return false;

	return true;
}
EXPORT_SYMBOL_GPL(cxllib_slot_is_supported);

static DEFINE_MUTEX(dra_mutex);
static u64 dummy_read_addr = CXL_INVALID_DRA;

static int allocate_dummy_read_buf(void)
{
	u64 buf, vaddr;
	size_t buf_size;

	/*
	 * Dummy read buffer is 128-byte long, aligned on a
	 * 256-byte boundary and we need the physical address.
	 */
	buf_size = CXL_DUMMY_READ_SIZE + (1ull << CXL_DUMMY_READ_ALIGN);
	buf = (u64) kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	vaddr = (buf + (1ull << CXL_DUMMY_READ_ALIGN) - 1) &
					(~0ull << CXL_DUMMY_READ_ALIGN);

	WARN((vaddr + CXL_DUMMY_READ_SIZE) > (buf + buf_size),
		"Dummy read buffer alignment issue");
	dummy_read_addr = virt_to_phys((void *) vaddr);
	return 0;
}

int cxllib_get_xsl_config(struct pci_dev *dev, struct cxllib_xsl_config *cfg)
{
	int rc;
	u32 phb_index;
	u64 chip_id, capp_unit_id;

	if (!cpu_has_feature(CPU_FTR_HVMODE))
		return -EINVAL;

	mutex_lock(&dra_mutex);
	if (dummy_read_addr == CXL_INVALID_DRA) {
		rc = allocate_dummy_read_buf();
		if (rc) {
			mutex_unlock(&dra_mutex);
			return rc;
		}
	}
	mutex_unlock(&dra_mutex);

	rc = cxl_calc_capp_routing(dev, &chip_id, &phb_index, &capp_unit_id);
	if (rc)
		return rc;

	rc = cxl_get_xsl9_dsnctl(capp_unit_id, &cfg->dsnctl);
	if (rc)
		return rc;
	if (cpu_has_feature(CPU_FTR_POWER9_DD1)) {
		/* workaround for DD1 - nbwind = capiind */
		cfg->dsnctl |= ((u64)0x02 << (63-47));
	}

	cfg->version  = CXL_XSL_CONFIG_CURRENT_VERSION;
	cfg->log_bar_size = CXL_CAPI_WINDOW_LOG_SIZE;
	cfg->bar_addr = CXL_CAPI_WINDOW_START;
	cfg->dra = dummy_read_addr;
	return 0;
}
EXPORT_SYMBOL_GPL(cxllib_get_xsl_config);

int cxllib_switch_phb_mode(struct pci_dev *dev, enum cxllib_mode mode,
			unsigned long flags)
{
	int rc = 0;

	if (!cpu_has_feature(CPU_FTR_HVMODE))
		return -EINVAL;

	switch (mode) {
	case CXL_MODE_PCI:
		/*
		 * We currently don't support going back to PCI mode
		 * However, we'll turn the invalidations off, so that
		 * the firmware doesn't have to ack them and can do
		 * things like reset, etc.. with no worries.
		 * So always return EPERM (can't go back to PCI) or
		 * EBUSY if we couldn't even turn off snooping
		 */
		rc = pnv_phb_to_cxl_mode(dev, OPAL_PHB_CAPI_MODE_SNOOP_OFF);
		if (rc)
			rc = -EBUSY;
		else
			rc = -EPERM;
		break;
	case CXL_MODE_CXL:
		/* DMA only supported on TVT1 for the time being */
		if (flags != CXL_MODE_DMA_TVT1)
			return -EINVAL;
		rc = pnv_phb_to_cxl_mode(dev, OPAL_PHB_CAPI_MODE_DMA_TVT1);
		if (rc)
			return rc;
		rc = pnv_phb_to_cxl_mode(dev, OPAL_PHB_CAPI_MODE_SNOOP_ON);
		break;
	default:
		rc = -EINVAL;
	}
	return rc;
}
EXPORT_SYMBOL_GPL(cxllib_switch_phb_mode);

/*
 * When switching the PHB to capi mode, the TVT#1 entry for
 * the Partitionable Endpoint is set in bypass mode, like
 * in PCI mode.
 * Configure the device dma to use TVT#1, which is done
 * by calling dma_set_mask() with a mask large enough.
 */
int cxllib_set_device_dma(struct pci_dev *dev, unsigned long flags)
{
	int rc;

	if (flags)
		return -EINVAL;

	rc = dma_set_mask(&dev->dev, DMA_BIT_MASK(64));
	return rc;
}
EXPORT_SYMBOL_GPL(cxllib_set_device_dma);

int cxllib_get_PE_attributes(struct task_struct *task,
			     unsigned long translation_mode,
			     struct cxllib_pe_attributes *attr)
{
	struct mm_struct *mm = NULL;

	if (translation_mode != CXL_TRANSLATED_MODE &&
		translation_mode != CXL_REAL_MODE)
		return -EINVAL;

	attr->sr = cxl_calculate_sr(false,
				task == NULL,
				translation_mode == CXL_REAL_MODE,
				true);
	attr->lpid = mfspr(SPRN_LPID);
	if (task) {
		mm = get_task_mm(task);
		if (mm == NULL)
			return -EINVAL;
		/*
		 * Caller is keeping a reference on mm_users for as long
		 * as XSL uses the memory context
		 */
		attr->pid = mm->context.id;
		mmput(mm);
		attr->tid = task->thread.tidr;
	} else {
		attr->pid = 0;
		attr->tid = 0;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(cxllib_get_PE_attributes);

int cxllib_handle_fault(struct mm_struct *mm, u64 addr, u64 size, u64 flags)
{
	int rc;
	u64 dar;
	struct vm_area_struct *vma = NULL;
	unsigned long page_size;

	if (mm == NULL)
		return -EFAULT;

	down_read(&mm->mmap_sem);

	vma = find_vma(mm, addr);
	if (!vma) {
		pr_err("Can't find vma for addr %016llx\n", addr);
		rc = -EFAULT;
		goto out;
	}
	/* get the size of the pages allocated */
	page_size = vma_kernel_pagesize(vma);

	for (dar = (addr & ~(page_size - 1)); dar < (addr + size); dar += page_size) {
		if (dar < vma->vm_start || dar >= vma->vm_end) {
			vma = find_vma(mm, addr);
			if (!vma) {
				pr_err("Can't find vma for addr %016llx\n", addr);
				rc = -EFAULT;
				goto out;
			}
			/* get the size of the pages allocated */
			page_size = vma_kernel_pagesize(vma);
		}

		rc = cxl_handle_mm_fault(mm, flags, dar);
		if (rc) {
			pr_err("cxl_handle_mm_fault failed %d", rc);
			rc = -EFAULT;
			goto out;
		}
	}
	rc = 0;
out:
	up_read(&mm->mmap_sem);
	return rc;
}
EXPORT_SYMBOL_GPL(cxllib_handle_fault);
