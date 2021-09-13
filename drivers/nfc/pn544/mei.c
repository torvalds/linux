// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2013  Intel Corporation. All rights reserved.
 *
 * HCI based Driver for NXP pn544 NFC Chip
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/nfc.h>
#include <net/nfc/hci.h>
#include <net/nfc/llc.h>

#include "../mei_phy.h"
#include "pn544.h"

#define PN544_DRIVER_NAME "pn544"

static int pn544_mei_probe(struct mei_cl_device *cldev,
			       const struct mei_cl_device_id *id)
{
	struct nfc_mei_phy *phy;
	int r;

	phy = nfc_mei_phy_alloc(cldev);
	if (!phy) {
		pr_err("Cannot allocate memory for pn544 mei phy.\n");
		return -ENOMEM;
	}

	r = pn544_hci_probe(phy, &mei_phy_ops, LLC_NOP_NAME,
			    MEI_NFC_HEADER_SIZE, 0, MEI_NFC_MAX_HCI_PAYLOAD,
			    NULL, &phy->hdev);
	if (r < 0) {
		nfc_mei_phy_free(phy);

		return r;
	}

	return 0;
}

static void pn544_mei_remove(struct mei_cl_device *cldev)
{
	struct nfc_mei_phy *phy = mei_cldev_get_drvdata(cldev);

	pn544_hci_remove(phy->hdev);

	nfc_mei_phy_free(phy);
}

static struct mei_cl_device_id pn544_mei_tbl[] = {
	{ PN544_DRIVER_NAME, MEI_NFC_UUID, MEI_CL_VERSION_ANY},

	/* required last entry */
	{ }
};
MODULE_DEVICE_TABLE(mei, pn544_mei_tbl);

static struct mei_cl_driver pn544_driver = {
	.id_table = pn544_mei_tbl,
	.name = PN544_DRIVER_NAME,

	.probe = pn544_mei_probe,
	.remove = pn544_mei_remove,
};

module_mei_cl_driver(pn544_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
