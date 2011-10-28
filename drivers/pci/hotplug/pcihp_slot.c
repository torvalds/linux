/*
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 * Copyright (C) 2003-2004 Intel Corporation
 * (c) Copyright 2009 Hewlett-Packard Development Company, L.P.
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
 */

#include <linux/pci.h>
#include <linux/pci_hotplug.h>

static struct hpp_type0 pci_default_type0 = {
	.revision = 1,
	.cache_line_size = 8,
	.latency_timer = 0x40,
	.enable_serr = 0,
	.enable_perr = 0,
};

static void program_hpp_type0(struct pci_dev *dev, struct hpp_type0 *hpp)
{
	u16 pci_cmd, pci_bctl;

	if (!hpp) {
		/*
		 * Perhaps we *should* use default settings for PCIe, but
		 * pciehp didn't, so we won't either.
		 */
		if (pci_is_pcie(dev))
			return;
		dev_info(&dev->dev, "using default PCI settings\n");
		hpp = &pci_default_type0;
	}

	if (hpp->revision > 1) {
		dev_warn(&dev->dev,
			 "PCI settings rev %d not supported; using defaults\n",
			 hpp->revision);
		hpp = &pci_default_type0;
	}

	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, hpp->cache_line_size);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, hpp->latency_timer);
	pci_read_config_word(dev, PCI_COMMAND, &pci_cmd);
	if (hpp->enable_serr)
		pci_cmd |= PCI_COMMAND_SERR;
	else
		pci_cmd &= ~PCI_COMMAND_SERR;
	if (hpp->enable_perr)
		pci_cmd |= PCI_COMMAND_PARITY;
	else
		pci_cmd &= ~PCI_COMMAND_PARITY;
	pci_write_config_word(dev, PCI_COMMAND, pci_cmd);

	/* Program bridge control value */
	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		pci_write_config_byte(dev, PCI_SEC_LATENCY_TIMER,
				      hpp->latency_timer);
		pci_read_config_word(dev, PCI_BRIDGE_CONTROL, &pci_bctl);
		if (hpp->enable_serr)
			pci_bctl |= PCI_BRIDGE_CTL_SERR;
		else
			pci_bctl &= ~PCI_BRIDGE_CTL_SERR;
		if (hpp->enable_perr)
			pci_bctl |= PCI_BRIDGE_CTL_PARITY;
		else
			pci_bctl &= ~PCI_BRIDGE_CTL_PARITY;
		pci_write_config_word(dev, PCI_BRIDGE_CONTROL, pci_bctl);
	}
}

static void program_hpp_type1(struct pci_dev *dev, struct hpp_type1 *hpp)
{
	if (hpp)
		dev_warn(&dev->dev, "PCI-X settings not supported\n");
}

static void program_hpp_type2(struct pci_dev *dev, struct hpp_type2 *hpp)
{
	int pos;
	u16 reg16;
	u32 reg32;

	if (!hpp)
		return;

	/* Find PCI Express capability */
	pos = pci_pcie_cap(dev);
	if (!pos)
		return;

	if (hpp->revision > 1) {
		dev_warn(&dev->dev, "PCIe settings rev %d not supported\n",
			 hpp->revision);
		return;
	}

	/* Initialize Device Control Register */
	pci_read_config_word(dev, pos + PCI_EXP_DEVCTL, &reg16);
	reg16 = (reg16 & hpp->pci_exp_devctl_and) | hpp->pci_exp_devctl_or;
	pci_write_config_word(dev, pos + PCI_EXP_DEVCTL, reg16);

	/* Initialize Link Control Register */
	if (dev->subordinate) {
		pci_read_config_word(dev, pos + PCI_EXP_LNKCTL, &reg16);
		reg16 = (reg16 & hpp->pci_exp_lnkctl_and)
			| hpp->pci_exp_lnkctl_or;
		pci_write_config_word(dev, pos + PCI_EXP_LNKCTL, reg16);
	}

	/* Find Advanced Error Reporting Enhanced Capability */
	pos = pci_find_ext_capability(dev, PCI_EXT_CAP_ID_ERR);
	if (!pos)
		return;

	/* Initialize Uncorrectable Error Mask Register */
	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_MASK, &reg32);
	reg32 = (reg32 & hpp->unc_err_mask_and) | hpp->unc_err_mask_or;
	pci_write_config_dword(dev, pos + PCI_ERR_UNCOR_MASK, reg32);

	/* Initialize Uncorrectable Error Severity Register */
	pci_read_config_dword(dev, pos + PCI_ERR_UNCOR_SEVER, &reg32);
	reg32 = (reg32 & hpp->unc_err_sever_and) | hpp->unc_err_sever_or;
	pci_write_config_dword(dev, pos + PCI_ERR_UNCOR_SEVER, reg32);

	/* Initialize Correctable Error Mask Register */
	pci_read_config_dword(dev, pos + PCI_ERR_COR_MASK, &reg32);
	reg32 = (reg32 & hpp->cor_err_mask_and) | hpp->cor_err_mask_or;
	pci_write_config_dword(dev, pos + PCI_ERR_COR_MASK, reg32);

	/* Initialize Advanced Error Capabilities and Control Register */
	pci_read_config_dword(dev, pos + PCI_ERR_CAP, &reg32);
	reg32 = (reg32 & hpp->adv_err_cap_and) | hpp->adv_err_cap_or;
	pci_write_config_dword(dev, pos + PCI_ERR_CAP, reg32);

	/*
	 * FIXME: The following two registers are not supported yet.
	 *
	 *   o Secondary Uncorrectable Error Severity Register
	 *   o Secondary Uncorrectable Error Mask Register
	 */
}

/* Program PCIE MaxPayload setting on device: ensure parent maxpayload <= device */
static int pci_set_payload(struct pci_dev *dev)
{
       int pos, ppos;
       u16 pctl, psz;
       u16 dctl, dsz, dcap, dmax;
       struct pci_dev *parent;

       parent = dev->bus->self;
       pos = pci_find_capability(dev, PCI_CAP_ID_EXP);
       if (!pos)
               return 0;

       /* Read Device MaxPayload capability and setting */
       pci_read_config_word(dev, pos + PCI_EXP_DEVCTL, &dctl);
       pci_read_config_word(dev, pos + PCI_EXP_DEVCAP, &dcap);
       dsz = (dctl & PCI_EXP_DEVCTL_PAYLOAD) >> 5;
       dmax = (dcap & PCI_EXP_DEVCAP_PAYLOAD);

       /* Read Parent MaxPayload setting */
       ppos = pci_find_capability(parent, PCI_CAP_ID_EXP);
       if (!ppos)
               return 0;
       pci_read_config_word(parent, ppos + PCI_EXP_DEVCTL, &pctl);
       psz = (pctl &  PCI_EXP_DEVCTL_PAYLOAD) >> 5;

       /* If parent payload > device max payload -> error
        * If parent payload > device payload -> set speed
        * If parent payload <= device payload -> do nothing
        */
       if (psz > dmax)
               return -1;
       else if (psz > dsz) {
               dev_info(&dev->dev, "Setting MaxPayload to %d\n", 128 << psz);
               pci_write_config_word(dev, pos + PCI_EXP_DEVCTL,
                                     (dctl & ~PCI_EXP_DEVCTL_PAYLOAD) +
                                     (psz << 5));
       }
       return 0;
}

void pci_configure_slot(struct pci_dev *dev)
{
	struct pci_dev *cdev;
	struct hotplug_params hpp;
	int ret;

	if (!(dev->hdr_type == PCI_HEADER_TYPE_NORMAL ||
			(dev->hdr_type == PCI_HEADER_TYPE_BRIDGE &&
			(dev->class >> 8) == PCI_CLASS_BRIDGE_PCI)))
		return;

       ret = pci_set_payload(dev);
       if (ret)
               dev_warn(&dev->dev, "could not set device max payload\n");

	memset(&hpp, 0, sizeof(hpp));
	ret = pci_get_hp_params(dev, &hpp);
	if (ret)
		dev_warn(&dev->dev, "no hotplug settings from platform\n");

	program_hpp_type2(dev, hpp.t2);
	program_hpp_type1(dev, hpp.t1);
	program_hpp_type0(dev, hpp.t0);

	if (dev->subordinate) {
		list_for_each_entry(cdev, &dev->subordinate->devices,
				    bus_list)
			pci_configure_slot(cdev);
	}
}
EXPORT_SYMBOL_GPL(pci_configure_slot);
