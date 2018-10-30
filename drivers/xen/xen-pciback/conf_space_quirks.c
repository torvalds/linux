// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Backend - Handle special overlays for broken devices.
 *
 * Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 * Author: Chris Bookholt <hap10@epoch.ncsc.mil>
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include "pciback.h"
#include "conf_space.h"
#include "conf_space_quirks.h"

LIST_HEAD(xen_pcibk_quirks);
static inline const struct pci_device_id *
match_one_device(const struct pci_device_id *id, const struct pci_dev *dev)
{
	if ((id->vendor == PCI_ANY_ID || id->vendor == dev->vendor) &&
	    (id->device == PCI_ANY_ID || id->device == dev->device) &&
	    (id->subvendor == PCI_ANY_ID ||
				id->subvendor == dev->subsystem_vendor) &&
	    (id->subdevice == PCI_ANY_ID ||
				id->subdevice == dev->subsystem_device) &&
	    !((id->class ^ dev->class) & id->class_mask))
		return id;
	return NULL;
}

static struct xen_pcibk_config_quirk *xen_pcibk_find_quirk(struct pci_dev *dev)
{
	struct xen_pcibk_config_quirk *tmp_quirk;

	list_for_each_entry(tmp_quirk, &xen_pcibk_quirks, quirks_list)
		if (match_one_device(&tmp_quirk->devid, dev) != NULL)
			goto out;
	tmp_quirk = NULL;
	printk(KERN_DEBUG DRV_NAME
	       ": quirk didn't match any device known\n");
out:
	return tmp_quirk;
}

static inline void register_quirk(struct xen_pcibk_config_quirk *quirk)
{
	list_add_tail(&quirk->quirks_list, &xen_pcibk_quirks);
}

int xen_pcibk_field_is_dup(struct pci_dev *dev, unsigned int reg)
{
	int ret = 0;
	struct xen_pcibk_dev_data *dev_data = pci_get_drvdata(dev);
	struct config_field_entry *cfg_entry;

	list_for_each_entry(cfg_entry, &dev_data->config_fields, list) {
		if (OFFSET(cfg_entry) == reg) {
			ret = 1;
			break;
		}
	}
	return ret;
}

int xen_pcibk_config_quirks_add_field(struct pci_dev *dev, struct config_field
				    *field)
{
	int err = 0;

	switch (field->size) {
	case 1:
		field->u.b.read = xen_pcibk_read_config_byte;
		field->u.b.write = xen_pcibk_write_config_byte;
		break;
	case 2:
		field->u.w.read = xen_pcibk_read_config_word;
		field->u.w.write = xen_pcibk_write_config_word;
		break;
	case 4:
		field->u.dw.read = xen_pcibk_read_config_dword;
		field->u.dw.write = xen_pcibk_write_config_dword;
		break;
	default:
		err = -EINVAL;
		goto out;
	}

	xen_pcibk_config_add_field(dev, field);

out:
	return err;
}

int xen_pcibk_config_quirks_init(struct pci_dev *dev)
{
	struct xen_pcibk_config_quirk *quirk;
	int ret = 0;

	quirk = kzalloc(sizeof(*quirk), GFP_KERNEL);
	if (!quirk) {
		ret = -ENOMEM;
		goto out;
	}

	quirk->devid.vendor = dev->vendor;
	quirk->devid.device = dev->device;
	quirk->devid.subvendor = dev->subsystem_vendor;
	quirk->devid.subdevice = dev->subsystem_device;
	quirk->devid.class = 0;
	quirk->devid.class_mask = 0;
	quirk->devid.driver_data = 0UL;

	quirk->pdev = dev;

	register_quirk(quirk);
out:
	return ret;
}

void xen_pcibk_config_field_free(struct config_field *field)
{
	kfree(field);
}

int xen_pcibk_config_quirk_release(struct pci_dev *dev)
{
	struct xen_pcibk_config_quirk *quirk;
	int ret = 0;

	quirk = xen_pcibk_find_quirk(dev);
	if (!quirk) {
		ret = -ENXIO;
		goto out;
	}

	list_del(&quirk->quirks_list);
	kfree(quirk);

out:
	return ret;
}
