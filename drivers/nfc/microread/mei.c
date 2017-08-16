/*
 * HCI based Driver for Inside Secure microread NFC Chip
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/nfc.h>
#include <net/nfc/hci.h>
#include <net/nfc/llc.h>

#include "../mei_phy.h"
#include "microread.h"

#define MICROREAD_DRIVER_NAME "microread"

static int microread_mei_probe(struct mei_cl_device *cldev,
			       const struct mei_cl_device_id *id)
{
	struct nfc_mei_phy *phy;
	int r;

	pr_info("Probing NFC microread\n");

	phy = nfc_mei_phy_alloc(cldev);
	if (!phy) {
		pr_err("Cannot allocate memory for microread mei phy.\n");
		return -ENOMEM;
	}

	r = microread_probe(phy, &mei_phy_ops, LLC_NOP_NAME,
			    MEI_NFC_HEADER_SIZE, 0, MEI_NFC_MAX_HCI_PAYLOAD,
			    &phy->hdev);
	if (r < 0) {
		nfc_mei_phy_free(phy);

		return r;
	}

	return 0;
}

static int microread_mei_remove(struct mei_cl_device *cldev)
{
	struct nfc_mei_phy *phy = mei_cldev_get_drvdata(cldev);

	microread_remove(phy->hdev);

	nfc_mei_phy_free(phy);

	return 0;
}

static struct mei_cl_device_id microread_mei_tbl[] = {
	{ MICROREAD_DRIVER_NAME, MEI_NFC_UUID, MEI_CL_VERSION_ANY},

	/* required last entry */
	{ }
};
MODULE_DEVICE_TABLE(mei, microread_mei_tbl);

static struct mei_cl_driver microread_driver = {
	.id_table = microread_mei_tbl,
	.name = MICROREAD_DRIVER_NAME,

	.probe = microread_mei_probe,
	.remove = microread_mei_remove,
};

module_mei_cl_driver(microread_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
