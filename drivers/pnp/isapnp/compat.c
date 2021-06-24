// SPDX-License-Identifier: GPL-2.0
/*
 * compat.c - A series of functions to make it easier to convert drivers that use
 *            the old isapnp APIs. If possible use the new APIs instead.
 *
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 */

#include <linux/module.h>
#include <linux/isapnp.h>
#include <linux/string.h>

static void pnp_convert_id(char *buf, unsigned short vendor,
			   unsigned short device)
{
	sprintf(buf, "%c%c%c%x%x%x%x",
		'A' + ((vendor >> 2) & 0x3f) - 1,
		'A' + (((vendor & 3) << 3) | ((vendor >> 13) & 7)) - 1,
		'A' + ((vendor >> 8) & 0x1f) - 1,
		(device >> 4) & 0x0f, device & 0x0f,
		(device >> 12) & 0x0f, (device >> 8) & 0x0f);
}

struct pnp_dev *pnp_find_dev(struct pnp_card *card, unsigned short vendor,
			     unsigned short function, struct pnp_dev *from)
{
	char id[8];
	char any[8];

	pnp_convert_id(id, vendor, function);
	pnp_convert_id(any, ISAPNP_ANY_ID, ISAPNP_ANY_ID);
	if (card == NULL) {	/* look for a logical device from all cards */
		struct list_head *list;

		list = pnp_global.next;
		if (from)
			list = from->global_list.next;

		while (list != &pnp_global) {
			struct pnp_dev *dev = global_to_pnp_dev(list);

			if (compare_pnp_id(dev->id, id) ||
			    (memcmp(id, any, 7) == 0))
				return dev;
			list = list->next;
		}
	} else {
		struct list_head *list;

		list = card->devices.next;
		if (from) {
			list = from->card_list.next;
			if (from->card != card)	/* something is wrong */
				return NULL;
		}
		while (list != &card->devices) {
			struct pnp_dev *dev = card_to_pnp_dev(list);

			if (compare_pnp_id(dev->id, id))
				return dev;
			list = list->next;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(pnp_find_dev);
