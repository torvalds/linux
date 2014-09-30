/*
 * Sysfs entries for PCI Error Recovery for PAPR-compliant platform.
 * Copyright IBM Corporation 2007
 * Copyright Linas Vepstas <linas@austin.ibm.com> 2007
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send comments and feedback to Linas Vepstas <linas@austin.ibm.com>
 */
#include <linux/pci.h>
#include <linux/stat.h>
#include <asm/ppc-pci.h>
#include <asm/pci-bridge.h>

/**
 * EEH_SHOW_ATTR -- Create sysfs entry for eeh statistic
 * @_name: name of file in sysfs directory
 * @_memb: name of member in struct pci_dn to access
 * @_format: printf format for display
 *
 * All of the attributes look very similar, so just
 * auto-gen a cut-n-paste routine to display them.
 */
#define EEH_SHOW_ATTR(_name,_memb,_format)               \
static ssize_t eeh_show_##_name(struct device *dev,      \
		struct device_attribute *attr, char *buf)          \
{                                                        \
	struct pci_dev *pdev = to_pci_dev(dev);               \
	struct eeh_dev *edev = pci_dev_to_eeh_dev(pdev);      \
	                                                      \
	if (!edev)                                            \
		return 0;                                     \
	                                                      \
	return sprintf(buf, _format "\n", edev->_memb);       \
}                                                        \
static DEVICE_ATTR(_name, S_IRUGO, eeh_show_##_name, NULL);

EEH_SHOW_ATTR(eeh_mode,            mode,            "0x%x");
EEH_SHOW_ATTR(eeh_config_addr,     config_addr,     "0x%x");
EEH_SHOW_ATTR(eeh_pe_config_addr,  pe_config_addr,  "0x%x");

static ssize_t eeh_pe_state_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct eeh_dev *edev = pci_dev_to_eeh_dev(pdev);
	int state;

	if (!edev || !edev->pe)
		return -ENODEV;

	state = eeh_ops->get_state(edev->pe, NULL);
	return sprintf(buf, "%08x %08x\n",
		       state, edev->pe->state);
}

static ssize_t eeh_pe_state_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct eeh_dev *edev = pci_dev_to_eeh_dev(pdev);
	int ret;

	if (!edev || !edev->pe)
		return -ENODEV;

	/* Nothing to do if it's not frozen */
	if (!(edev->pe->state & EEH_PE_ISOLATED))
		return count;

	/* Enable MMIO */
	ret = eeh_pci_enable(edev->pe, EEH_OPT_THAW_MMIO);
	if (ret) {
		pr_warn("%s: Failure %d enabling MMIO for PHB#%d-PE#%d\n",
			__func__, ret, edev->pe->phb->global_number,
			edev->pe->addr);
		return -EIO;
	}

	/* Enable DMA */
	ret = eeh_pci_enable(edev->pe, EEH_OPT_THAW_DMA);
	if (ret) {
		pr_warn("%s: Failure %d enabling DMA for PHB#%d-PE#%d\n",
			__func__, ret, edev->pe->phb->global_number,
			edev->pe->addr);
		return -EIO;
	}

	/* Clear software state */
	eeh_pe_state_clear(edev->pe, EEH_PE_ISOLATED);

	return count;
}

static DEVICE_ATTR_RW(eeh_pe_state);

void eeh_sysfs_add_device(struct pci_dev *pdev)
{
	struct eeh_dev *edev = pci_dev_to_eeh_dev(pdev);
	int rc=0;

	if (!eeh_enabled())
		return;

	if (edev && (edev->mode & EEH_DEV_SYSFS))
		return;

	rc += device_create_file(&pdev->dev, &dev_attr_eeh_mode);
	rc += device_create_file(&pdev->dev, &dev_attr_eeh_config_addr);
	rc += device_create_file(&pdev->dev, &dev_attr_eeh_pe_config_addr);
	rc += device_create_file(&pdev->dev, &dev_attr_eeh_pe_state);

	if (rc)
		pr_warn("EEH: Unable to create sysfs entries\n");
	else if (edev)
		edev->mode |= EEH_DEV_SYSFS;
}

void eeh_sysfs_remove_device(struct pci_dev *pdev)
{
	struct eeh_dev *edev = pci_dev_to_eeh_dev(pdev);

	/*
	 * The parent directory might have been removed. We needn't
	 * continue for that case.
	 */
	if (!pdev->dev.kobj.sd) {
		if (edev)
			edev->mode &= ~EEH_DEV_SYSFS;
		return;
	}

	device_remove_file(&pdev->dev, &dev_attr_eeh_mode);
	device_remove_file(&pdev->dev, &dev_attr_eeh_config_addr);
	device_remove_file(&pdev->dev, &dev_attr_eeh_pe_config_addr);
	device_remove_file(&pdev->dev, &dev_attr_eeh_pe_state);

	if (edev)
		edev->mode &= ~EEH_DEV_SYSFS;
}
