// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#include "rtl_pci.h"
#include "rtl_core.h"

static void _rtl92e_parse_pci_configuration(struct pci_dev *pdev,
					    struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);

	u8 tmp;
	u16 link_ctrl_reg;

	pcie_capability_read_word(priv->pdev, PCI_EXP_LNKCTL, &link_ctrl_reg);

	pci_read_config_byte(pdev, 0x98, &tmp);
	tmp |= BIT4;
	pci_write_config_byte(pdev, 0x98, tmp);

	tmp = 0x17;
	pci_write_config_byte(pdev, 0x70f, tmp);
}

bool rtl92e_check_adapter(struct pci_dev *pdev, struct net_device *dev)
{
	struct r8192_priv *priv = (struct r8192_priv *)rtllib_priv(dev);
	u16 device_id;
	u8  revision_id;
	u16 irq_line;

	device_id = pdev->device;
	revision_id = pdev->revision;
	pci_read_config_word(pdev, 0x3C, &irq_line);

	priv->card_8192 = priv->ops->nic_type;

	if (device_id == 0x8192) {
		switch (revision_id) {
		case HAL_HW_PCI_REVISION_ID_8192PCIE:
			dev_info(&pdev->dev,
				 "Adapter(8192 PCI-E) is found - DeviceID=%x\n",
				 device_id);
			priv->card_8192 = NIC_8192E;
			break;
		case HAL_HW_PCI_REVISION_ID_8192SE:
			dev_info(&pdev->dev,
				 "Adapter(8192SE) is found - DeviceID=%x\n",
				 device_id);
			priv->card_8192 = NIC_8192SE;
			break;
		default:
			dev_info(&pdev->dev,
				 "UNKNOWN nic type(%4x:%4x)\n",
				 pdev->vendor, pdev->device);
			priv->card_8192 = NIC_UNKNOWN;
			return false;
		}
	}

	if (priv->ops->nic_type != priv->card_8192) {
		dev_info(&pdev->dev,
			 "Detect info(%x) and hardware info(%x) not match!\n",
			 priv->ops->nic_type, priv->card_8192);
		dev_info(&pdev->dev,
			 "Please select proper driver before install!!!!\n");
		return false;
	}

	_rtl92e_parse_pci_configuration(pdev, dev);

	return true;
}
