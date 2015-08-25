/*
 * EEH functionality support for VFIO devices. The feature is only
 * available on sPAPR compatible platforms.
 *
 * Copyright Gavin Shan, IBM Corporation 2014.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>
#include <asm/eeh.h>

#define DRIVER_VERSION	"0.1"
#define DRIVER_AUTHOR	"Gavin Shan, IBM Corporation"
#define DRIVER_DESC	"VFIO IOMMU SPAPR EEH"

/* We might build address mapping here for "fast" path later */
void vfio_spapr_pci_eeh_open(struct pci_dev *pdev)
{
	eeh_dev_open(pdev);
}
EXPORT_SYMBOL_GPL(vfio_spapr_pci_eeh_open);

void vfio_spapr_pci_eeh_release(struct pci_dev *pdev)
{
	eeh_dev_release(pdev);
}
EXPORT_SYMBOL_GPL(vfio_spapr_pci_eeh_release);

long vfio_spapr_iommu_eeh_ioctl(struct iommu_group *group,
				unsigned int cmd, unsigned long arg)
{
	struct eeh_pe *pe;
	struct vfio_eeh_pe_op op;
	unsigned long minsz;
	long ret = -EINVAL;

	switch (cmd) {
	case VFIO_CHECK_EXTENSION:
		if (arg == VFIO_EEH)
			ret = eeh_enabled() ? 1 : 0;
		else
			ret = 0;
		break;
	case VFIO_EEH_PE_OP:
		pe = eeh_iommu_group_to_pe(group);
		if (!pe)
			return -ENODEV;

		minsz = offsetofend(struct vfio_eeh_pe_op, op);
		if (copy_from_user(&op, (void __user *)arg, minsz))
			return -EFAULT;
		if (op.argsz < minsz || op.flags)
			return -EINVAL;

		switch (op.op) {
		case VFIO_EEH_PE_DISABLE:
			ret = eeh_pe_set_option(pe, EEH_OPT_DISABLE);
			break;
		case VFIO_EEH_PE_ENABLE:
			ret = eeh_pe_set_option(pe, EEH_OPT_ENABLE);
			break;
		case VFIO_EEH_PE_UNFREEZE_IO:
			ret = eeh_pe_set_option(pe, EEH_OPT_THAW_MMIO);
			break;
		case VFIO_EEH_PE_UNFREEZE_DMA:
			ret = eeh_pe_set_option(pe, EEH_OPT_THAW_DMA);
			break;
		case VFIO_EEH_PE_GET_STATE:
			ret = eeh_pe_get_state(pe);
			break;
		case VFIO_EEH_PE_RESET_DEACTIVATE:
			ret = eeh_pe_reset(pe, EEH_RESET_DEACTIVATE);
			break;
		case VFIO_EEH_PE_RESET_HOT:
			ret = eeh_pe_reset(pe, EEH_RESET_HOT);
			break;
		case VFIO_EEH_PE_RESET_FUNDAMENTAL:
			ret = eeh_pe_reset(pe, EEH_RESET_FUNDAMENTAL);
			break;
		case VFIO_EEH_PE_CONFIGURE:
			ret = eeh_pe_configure(pe);
			break;
		case VFIO_EEH_PE_INJECT_ERR:
			minsz = offsetofend(struct vfio_eeh_pe_op, err.mask);
			if (op.argsz < minsz)
				return -EINVAL;
			if (copy_from_user(&op, (void __user *)arg, minsz))
				return -EFAULT;

			ret = eeh_pe_inject_err(pe, op.err.type, op.err.func,
						op.err.addr, op.err.mask);
			break;
		default:
			ret = -EINVAL;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(vfio_spapr_iommu_eeh_ioctl);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
