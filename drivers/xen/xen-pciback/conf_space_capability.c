/*
 * PCI Backend - Handles the virtual fields found on the capability lists
 *               in the configuration space.
 *
 * Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include "pciback.h"
#include "conf_space.h"
#include "conf_space_capability.h"

static LIST_HEAD(capabilities);

static const struct config_field caplist_header[] = {
	{
	 .offset    = PCI_CAP_LIST_ID,
	 .size      = 2, /* encompass PCI_CAP_LIST_ID & PCI_CAP_LIST_NEXT */
	 .u.w.read  = pciback_read_config_word,
	 .u.w.write = NULL,
	},
	{}
};

static inline void register_capability(struct pciback_config_capability *cap)
{
	list_add_tail(&cap->cap_list, &capabilities);
}

int pciback_config_capability_add_fields(struct pci_dev *dev)
{
	int err = 0;
	struct pciback_config_capability *cap;
	int cap_offset;

	list_for_each_entry(cap, &capabilities, cap_list) {
		cap_offset = pci_find_capability(dev, cap->capability);
		if (cap_offset) {
			dev_dbg(&dev->dev, "Found capability 0x%x at 0x%x\n",
				cap->capability, cap_offset);

			err = pciback_config_add_fields_offset(dev,
							       caplist_header,
							       cap_offset);
			if (err)
				goto out;
			err = pciback_config_add_fields_offset(dev,
							       cap->fields,
							       cap_offset);
			if (err)
				goto out;
		}
	}

out:
	return err;
}

int pciback_config_capability_init(void)
{
	register_capability(&pciback_config_capability_vpd);
	register_capability(&pciback_config_capability_pm);

	return 0;
}
