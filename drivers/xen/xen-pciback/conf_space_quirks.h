/*
 * PCI Backend - Data structures for special overlays for broken devices.
 *
 * Ryan Wilson <hap9@epoch.ncsc.mil>
 * Chris Bookholt <hap10@epoch.ncsc.mil>
 */

#ifndef __XEN_PCIBACK_CONF_SPACE_QUIRKS_H__
#define __XEN_PCIBACK_CONF_SPACE_QUIRKS_H__

#include <linux/pci.h>
#include <linux/list.h>

struct pciback_config_quirk {
	struct list_head quirks_list;
	struct pci_device_id devid;
	struct pci_dev *pdev;
};

struct pciback_config_quirk *pciback_find_quirk(struct pci_dev *dev);

int pciback_config_quirks_add_field(struct pci_dev *dev, struct config_field
				    *field);

int pciback_config_quirks_remove_field(struct pci_dev *dev, int reg);

int pciback_config_quirks_init(struct pci_dev *dev);

void pciback_config_field_free(struct config_field *field);

int pciback_config_quirk_release(struct pci_dev *dev);

int pciback_field_is_dup(struct pci_dev *dev, unsigned int reg);

#endif
