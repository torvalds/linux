// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Sysfs entries for PCI Error Recovery for PAPR-compliant platform.
 * Copyright IBM Corporation 2007
 * Copyright Linas Vepstas <linas@austin.ibm.com> 2007
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
static DEVICE_ATTR(_name, 0444, eeh_show_##_name, NULL);

EEH_SHOW_ATTR(eeh_mode,            mode,            "0x%x");
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
	return sprintf(buf, "0x%08x 0x%08x\n",
		       state, edev->pe->state);
}

static ssize_t eeh_pe_state_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct eeh_dev *edev = pci_dev_to_eeh_dev(pdev);

	if (!edev || !edev->pe)
		return -ENODEV;

	/* Nothing to do if it's not frozen */
	if (!(edev->pe->state & EEH_PE_ISOLATED))
		return count;

	if (eeh_unfreeze_pe(edev->pe))
		return -EIO;
	eeh_pe_state_clear(edev->pe, EEH_PE_ISOLATED, true);

	return count;
}

static DEVICE_ATTR_RW(eeh_pe_state);

#ifdef CONFIG_PCI_IOV
static ssize_t eeh_notify_resume_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct eeh_dev *edev = pci_dev_to_eeh_dev(pdev);
	struct pci_dn *pdn = pci_get_pdn(pdev);

	if (!edev || !edev->pe)
		return -ENODEV;

	pdn = pci_get_pdn(pdev);
	return sprintf(buf, "%d\n", pdn->last_allow_rc);
}

static ssize_t eeh_notify_resume_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct eeh_dev *edev = pci_dev_to_eeh_dev(pdev);

	if (!edev || !edev->pe || !eeh_ops->notify_resume)
		return -ENODEV;

	if (eeh_ops->notify_resume(pci_get_pdn(pdev)))
		return -EIO;

	return count;
}
static DEVICE_ATTR_RW(eeh_notify_resume);

static int eeh_notify_resume_add(struct pci_dev *pdev)
{
	struct device_node *np;
	int rc = 0;

	np = pci_device_to_OF_node(pdev->is_physfn ? pdev : pdev->physfn);

	if (of_property_read_bool(np, "ibm,is-open-sriov-pf"))
		rc = device_create_file(&pdev->dev, &dev_attr_eeh_notify_resume);

	return rc;
}

static void eeh_notify_resume_remove(struct pci_dev *pdev)
{
	struct device_node *np;

	np = pci_device_to_OF_node(pdev->is_physfn ? pdev : pdev->physfn);

	if (of_property_read_bool(np, "ibm,is-open-sriov-pf"))
		device_remove_file(&pdev->dev, &dev_attr_eeh_notify_resume);
}
#else
static inline int eeh_notify_resume_add(struct pci_dev *pdev) { return 0; }
static inline void eeh_notify_resume_remove(struct pci_dev *pdev) { }
#endif /* CONFIG_PCI_IOV */

void eeh_sysfs_add_device(struct pci_dev *pdev)
{
	struct eeh_dev *edev = pci_dev_to_eeh_dev(pdev);
	int rc=0;

	if (!eeh_enabled())
		return;

	if (edev && (edev->mode & EEH_DEV_SYSFS))
		return;

	rc += device_create_file(&pdev->dev, &dev_attr_eeh_mode);
	rc += device_create_file(&pdev->dev, &dev_attr_eeh_pe_config_addr);
	rc += device_create_file(&pdev->dev, &dev_attr_eeh_pe_state);
	rc += eeh_notify_resume_add(pdev);

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
	device_remove_file(&pdev->dev, &dev_attr_eeh_pe_config_addr);
	device_remove_file(&pdev->dev, &dev_attr_eeh_pe_state);

	eeh_notify_resume_remove(pdev);

	if (edev)
		edev->mode &= ~EEH_DEV_SYSFS;
}
