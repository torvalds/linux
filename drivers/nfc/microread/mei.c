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
#include "microread.h"

#define MICROREAD_DRIVER_NAME "microread"

static int microread_mei_probe(struct mei_cl_device *device,
			       const struct mei_cl_device_id *id)
{
	struct nfc_mei_phy *phy;
	int r;

	pr_info("Probing NFC microread\n");

	phy = nfc_mei_phy_alloc(device);
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

static int microread_mei_remove(struct mei_cl_device *device)
{
	struct nfc_mei_phy *phy = mei_cl_get_drvdata(device);

	pr_info("Removing microread\n");

	microread_remove(phy->hdev);

	nfc_mei_phy_free(phy);

	return 0;
}

static struct mei_cl_device_id microread_mei_tbl[] = {
	{ MICROREAD_DRIVER_NAME },

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

static int microread_mei_init(void)
{
	int r;

	pr_debug(DRIVER_DESC ": %s\n", __func__);

	r = mei_cl_driver_register(&microread_driver);
	if (r) {
		pr_err(MICROREAD_DRIVER_NAME ": driver registration failed\n");
		return r;
	}

	return 0;
}

static void microread_mei_exit(void)
{
	mei_cl_driver_unregister(&microread_driver);
}

module_init(microread_mei_init);
module_exit(microread_mei_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
