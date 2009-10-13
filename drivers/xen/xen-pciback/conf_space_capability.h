/*
 * PCI Backend - Data structures for special overlays for structures on
 *               the capability list.
 *
 * Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */

#ifndef __PCIBACK_CONFIG_CAPABILITY_H__
#define __PCIBACK_CONFIG_CAPABILITY_H__

#include <linux/pci.h>
#include <linux/list.h>

struct pciback_config_capability {
	struct list_head cap_list;

	int capability;

	/* If the device has the capability found above, add these fields */
	const struct config_field *fields;
};

extern struct pciback_config_capability pciback_config_capability_vpd;
extern struct pciback_config_capability pciback_config_capability_pm;

#endif
