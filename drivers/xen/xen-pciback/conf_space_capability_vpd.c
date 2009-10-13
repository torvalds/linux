/*
 * PCI Backend - Configuration space overlay for Vital Product Data
 *
 * Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */

#include <linux/pci.h>
#include "conf_space.h"
#include "conf_space_capability.h"

static int vpd_address_write(struct pci_dev *dev, int offset, u16 value,
			     void *data)
{
	/* Disallow writes to the vital product data */
	if (value & PCI_VPD_ADDR_F)
		return PCIBIOS_SET_FAILED;
	else
		return pci_write_config_word(dev, offset, value);
}

static const struct config_field caplist_vpd[] = {
	{
	 .offset    = PCI_VPD_ADDR,
	 .size      = 2,
	 .u.w.read  = pciback_read_config_word,
	 .u.w.write = vpd_address_write,
	 },
	{
	 .offset     = PCI_VPD_DATA,
	 .size       = 4,
	 .u.dw.read  = pciback_read_config_dword,
	 .u.dw.write = NULL,
	 },
	{}
};

struct pciback_config_capability pciback_config_capability_vpd = {
	.capability = PCI_CAP_ID_VPD,
	.fields = caplist_vpd,
};
