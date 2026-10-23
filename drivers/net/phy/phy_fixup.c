// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PHY fixup support
 */

#include <linux/list.h>
#include <linux/phy.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "phylib-internal.h"

static struct list_head phy_fixup_list __ro_after_init =
	LIST_HEAD_INIT(phy_fixup_list);

struct phy_fixup {
	struct list_head list;
	char bus_id[MII_BUS_ID_SIZE + 3];
	u32 phy_uid;
	u32 phy_uid_mask;
	int (*run)(struct phy_device *phydev);
};

/**
 * phy_register_fixup - creates a new phy_fixup and adds it to the list
 * @bus_id: A string which matches phydev->mdio.dev.bus_id (or NULL)
 * @phy_uid: Used to match against phydev->phy_id (the UID of the PHY)
 * @phy_uid_mask: Applied to phydev->phy_id and fixup->phy_uid before
 *	comparison (or 0 to disable id-based matching)
 * @run: The actual code to be run when a matching PHY is found
 */
static void __init phy_register_fixup(const char *bus_id, u32 phy_uid,
				      u32 phy_uid_mask,
				      int (*run)(struct phy_device *))
{
	struct phy_fixup *fixup = kzalloc_obj(*fixup);

	if (!fixup)
		return;

	if (bus_id)
		strscpy(fixup->bus_id, bus_id);
	fixup->phy_uid = phy_uid;
	fixup->phy_uid_mask = phy_uid_mask;
	fixup->run = run;

	list_add_tail(&fixup->list, &phy_fixup_list);
}

/* Registers a fixup to be run on any PHY with the UID in phy_uid */
void __init phy_register_fixup_for_uid(u32 phy_uid, u32 phy_uid_mask,
				       int (*run)(struct phy_device *))
{
	phy_register_fixup(NULL, phy_uid, phy_uid_mask, run);
}

/* Registers a fixup to be run on the PHY with id string bus_id */
void __init phy_register_fixup_for_id(const char *bus_id,
				      int (*run)(struct phy_device *))
{
	phy_register_fixup(bus_id, 0, 0, run);
}

static bool phy_needs_fixup(struct phy_device *phydev, struct phy_fixup *fixup)
{
	if (!strcmp(fixup->bus_id, phydev_name(phydev)))
		return true;

	if (fixup->phy_uid_mask &&
	    phy_id_compare(phydev->phy_id, fixup->phy_uid, fixup->phy_uid_mask))
		return true;

	return false;
}

/**
 * phy_scan_fixups - runs any matching fixups for this phydev
 * @phydev: the phydev to search and run fixups for
 * Returns: 0 or an errno
 */
int phy_scan_fixups(struct phy_device *phydev)
{
	struct phy_fixup *fixup;

	list_for_each_entry(fixup, &phy_fixup_list, list) {
		if (phy_needs_fixup(phydev, fixup)) {
			int err = fixup->run(phydev);

			if (err < 0)
				return err;

			phydev->has_fixups = true;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(phy_scan_fixups);
