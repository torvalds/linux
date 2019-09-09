// SPDX-License-Identifier: GPL-2.0
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

static LIST_HEAD(capabilities);
struct xen_pcibk_config_capability {
	struct list_head cap_list;

	int capability;

	/* If the device has the capability found above, add these fields */
	const struct config_field *fields;
};

static const struct config_field caplist_header[] = {
	{
	 .offset    = PCI_CAP_LIST_ID,
	 .size      = 2, /* encompass PCI_CAP_LIST_ID & PCI_CAP_LIST_NEXT */
	 .u.w.read  = xen_pcibk_read_config_word,
	 .u.w.write = NULL,
	},
	{}
};

static inline void register_capability(struct xen_pcibk_config_capability *cap)
{
	list_add_tail(&cap->cap_list, &capabilities);
}

int xen_pcibk_config_capability_add_fields(struct pci_dev *dev)
{
	int err = 0;
	struct xen_pcibk_config_capability *cap;
	int cap_offset;

	list_for_each_entry(cap, &capabilities, cap_list) {
		cap_offset = pci_find_capability(dev, cap->capability);
		if (cap_offset) {
			dev_dbg(&dev->dev, "Found capability 0x%x at 0x%x\n",
				cap->capability, cap_offset);

			err = xen_pcibk_config_add_fields_offset(dev,
							       caplist_header,
							       cap_offset);
			if (err)
				goto out;
			err = xen_pcibk_config_add_fields_offset(dev,
							       cap->fields,
							       cap_offset);
			if (err)
				goto out;
		}
	}

out:
	return err;
}

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
	 .u.w.read  = xen_pcibk_read_config_word,
	 .u.w.write = vpd_address_write,
	 },
	{
	 .offset     = PCI_VPD_DATA,
	 .size       = 4,
	 .u.dw.read  = xen_pcibk_read_config_dword,
	 .u.dw.write = NULL,
	 },
	{}
};

static int pm_caps_read(struct pci_dev *dev, int offset, u16 *value,
			void *data)
{
	int err;
	u16 real_value;

	err = pci_read_config_word(dev, offset, &real_value);
	if (err)
		goto out;

	*value = real_value & ~PCI_PM_CAP_PME_MASK;

out:
	return err;
}

/* PM_OK_BITS specifies the bits that the driver domain is allowed to change.
 * Can't allow driver domain to enable PMEs - they're shared */
#define PM_OK_BITS (PCI_PM_CTRL_PME_STATUS|PCI_PM_CTRL_DATA_SEL_MASK)

static int pm_ctrl_write(struct pci_dev *dev, int offset, u16 new_value,
			 void *data)
{
	int err;
	u16 old_value;
	pci_power_t new_state;

	err = pci_read_config_word(dev, offset, &old_value);
	if (err)
		goto out;

	new_state = (pci_power_t)(new_value & PCI_PM_CTRL_STATE_MASK);

	new_value &= PM_OK_BITS;
	if ((old_value & PM_OK_BITS) != new_value) {
		new_value = (old_value & ~PM_OK_BITS) | new_value;
		err = pci_write_config_word(dev, offset, new_value);
		if (err)
			goto out;
	}

	/* Let pci core handle the power management change */
	dev_dbg(&dev->dev, "set power state to %x\n", new_state);
	err = pci_set_power_state(dev, new_state);
	if (err) {
		err = PCIBIOS_SET_FAILED;
		goto out;
	}

 out:
	return err;
}

/* Ensure PMEs are disabled */
static void *pm_ctrl_init(struct pci_dev *dev, int offset)
{
	int err;
	u16 value;

	err = pci_read_config_word(dev, offset, &value);
	if (err)
		goto out;

	if (value & PCI_PM_CTRL_PME_ENABLE) {
		value &= ~PCI_PM_CTRL_PME_ENABLE;
		err = pci_write_config_word(dev, offset, value);
	}

out:
	return ERR_PTR(err);
}

static const struct config_field caplist_pm[] = {
	{
		.offset     = PCI_PM_PMC,
		.size       = 2,
		.u.w.read   = pm_caps_read,
	},
	{
		.offset     = PCI_PM_CTRL,
		.size       = 2,
		.init       = pm_ctrl_init,
		.u.w.read   = xen_pcibk_read_config_word,
		.u.w.write  = pm_ctrl_write,
	},
	{
		.offset     = PCI_PM_PPB_EXTENSIONS,
		.size       = 1,
		.u.b.read   = xen_pcibk_read_config_byte,
	},
	{
		.offset     = PCI_PM_DATA_REGISTER,
		.size       = 1,
		.u.b.read   = xen_pcibk_read_config_byte,
	},
	{}
};

static struct xen_pcibk_config_capability xen_pcibk_config_capability_pm = {
	.capability = PCI_CAP_ID_PM,
	.fields = caplist_pm,
};
static struct xen_pcibk_config_capability xen_pcibk_config_capability_vpd = {
	.capability = PCI_CAP_ID_VPD,
	.fields = caplist_vpd,
};

int xen_pcibk_config_capability_init(void)
{
	register_capability(&xen_pcibk_config_capability_vpd);
	register_capability(&xen_pcibk_config_capability_pm);

	return 0;
}
