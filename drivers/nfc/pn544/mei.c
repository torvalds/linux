/*
 * HCI based Driver for NXP pn544 NFC Chip
 *
 * Copyright (C) 2013  Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/nfc.h>
#include <net/nfc/hci.h>
#include <net/nfc/llc.h>

#include "../mei_phy.h"
#include "pn544.h"

#define PN544_DRIVER_NAME "pn544"

static int pn544_mei_probe(struct mei_cl_device *device,
			       const struct mei_cl_device_id *id)
{
	struct nfc_mei_phy *phy;
	int r;

	pr_info("Probing NFC pn544\n");

	phy = nfc_mei_phy_alloc(device);
	if (!phy) {
		pr_err("Cannot allocate memory for pn544 mei phy.\n");
		return -ENOMEM;
	}

	r = mei_cl_register_event_cb(device, nfc_mei_event_cb, phy);
	if (r) {
		pr_err(PN544_DRIVER_NAME ": event cb registration failed\n");
		goto err_out;
	}

	r = pn544_hci_probe(phy, &mei_phy_ops, LLC_NOP_NAME,
			    MEI_NFC_HEADER_SIZE, 0, MEI_NFC_MAX_HCI_PAYLOAD,
			    &phy->hdev);
	if (r < 0)
		goto err_out;

	return 0;

err_out:
	nfc_mei_phy_free(phy);

	return r;
}

static int pn544_mei_remove(struct mei_cl_device *device)
{
	struct nfc_mei_phy *phy = mei_cl_get_drvdata(device);

	pr_info("Removing pn544\n");

	pn544_hci_remove(phy->hdev);

	nfc_mei_phy_disable(phy);

	nfc_mei_phy_free(phy);

	return 0;
}

static struct mei_cl_device_id pn544_mei_tbl[] = {
	{ PN544_DRIVER_NAME },

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

static int pn544_mei_init(void)
{
	int r;

	pr_debug(DRIVER_DESC ": %s\n", __func__);

	r = mei_cl_driver_register(&pn544_driver);
	if (r) {
		pr_err(PN544_DRIVER_NAME ": driver registration failed\n");
		return r;
	}

	return 0;
}

static void pn544_mei_exit(void)
{
	mei_cl_driver_unregister(&pn544_driver);
}

module_init(pn544_mei_init);
module_exit(pn544_mei_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
