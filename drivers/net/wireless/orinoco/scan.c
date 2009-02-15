/* Helpers for managing scan queues
 *
 * See copyright notice in main.c
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/etherdevice.h>

#include "hermes.h"
#include "orinoco.h"

#include "scan.h"

#define ORINOCO_MAX_BSS_COUNT	64

#define PRIV_BSS	((struct bss_element *)priv->bss_xbss_data)
#define PRIV_XBSS	((struct xbss_element *)priv->bss_xbss_data)

int orinoco_bss_data_allocate(struct orinoco_private *priv)
{
	if (priv->bss_xbss_data)
		return 0;

	if (priv->has_ext_scan)
		priv->bss_xbss_data = kzalloc(ORINOCO_MAX_BSS_COUNT *
					      sizeof(struct xbss_element),
					      GFP_KERNEL);
	else
		priv->bss_xbss_data = kzalloc(ORINOCO_MAX_BSS_COUNT *
					      sizeof(struct bss_element),
					      GFP_KERNEL);

	if (!priv->bss_xbss_data) {
		printk(KERN_WARNING "Out of memory allocating beacons");
		return -ENOMEM;
	}
	return 0;
}

void orinoco_bss_data_free(struct orinoco_private *priv)
{
	kfree(priv->bss_xbss_data);
	priv->bss_xbss_data = NULL;
}

void orinoco_bss_data_init(struct orinoco_private *priv)
{
	int i;

	INIT_LIST_HEAD(&priv->bss_free_list);
	INIT_LIST_HEAD(&priv->bss_list);
	if (priv->has_ext_scan)
		for (i = 0; i < ORINOCO_MAX_BSS_COUNT; i++)
			list_add_tail(&(PRIV_XBSS[i].list),
				      &priv->bss_free_list);
	else
		for (i = 0; i < ORINOCO_MAX_BSS_COUNT; i++)
			list_add_tail(&(PRIV_BSS[i].list),
				      &priv->bss_free_list);

}

void orinoco_clear_scan_results(struct orinoco_private *priv,
				unsigned long scan_age)
{
	if (priv->has_ext_scan) {
		struct xbss_element *bss;
		struct xbss_element *tmp_bss;

		/* Blow away current list of scan results */
		list_for_each_entry_safe(bss, tmp_bss, &priv->bss_list, list) {
			if (!scan_age ||
			    time_after(jiffies, bss->last_scanned + scan_age)) {
				list_move_tail(&bss->list,
					       &priv->bss_free_list);
				/* Don't blow away ->list, just BSS data */
				memset(&bss->bss, 0, sizeof(bss->bss));
				bss->last_scanned = 0;
			}
		}
	} else {
		struct bss_element *bss;
		struct bss_element *tmp_bss;

		/* Blow away current list of scan results */
		list_for_each_entry_safe(bss, tmp_bss, &priv->bss_list, list) {
			if (!scan_age ||
			    time_after(jiffies, bss->last_scanned + scan_age)) {
				list_move_tail(&bss->list,
					       &priv->bss_free_list);
				/* Don't blow away ->list, just BSS data */
				memset(&bss->bss, 0, sizeof(bss->bss));
				bss->last_scanned = 0;
			}
		}
	}
}

void orinoco_add_ext_scan_result(struct orinoco_private *priv,
				 struct agere_ext_scan_info *atom)
{
	struct xbss_element *bss = NULL;
	int found = 0;

	/* Try to update an existing bss first */
	list_for_each_entry(bss, &priv->bss_list, list) {
		if (compare_ether_addr(bss->bss.bssid, atom->bssid))
			continue;
		/* ESSID lengths */
		if (bss->bss.data[1] != atom->data[1])
			continue;
		if (memcmp(&bss->bss.data[2], &atom->data[2],
			   atom->data[1]))
			continue;
		found = 1;
		break;
	}

	/* Grab a bss off the free list */
	if (!found && !list_empty(&priv->bss_free_list)) {
		bss = list_entry(priv->bss_free_list.next,
				 struct xbss_element, list);
		list_del(priv->bss_free_list.next);

		list_add_tail(&bss->list, &priv->bss_list);
	}

	if (bss) {
		/* Always update the BSS to get latest beacon info */
		memcpy(&bss->bss, atom, sizeof(bss->bss));
		bss->last_scanned = jiffies;
	}
}

int orinoco_process_scan_results(struct orinoco_private *priv,
				 unsigned char *buf,
				 int len)
{
	int			offset;		/* In the scan data */
	union hermes_scan_info *atom;
	int			atom_len;

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE:
		atom_len = sizeof(struct agere_scan_apinfo);
		offset = 0;
		break;
	case FIRMWARE_TYPE_SYMBOL:
		/* Lack of documentation necessitates this hack.
		 * Different firmwares have 68 or 76 byte long atoms.
		 * We try modulo first.  If the length divides by both,
		 * we check what would be the channel in the second
		 * frame for a 68-byte atom.  76-byte atoms have 0 there.
		 * Valid channel cannot be 0.  */
		if (len % 76)
			atom_len = 68;
		else if (len % 68)
			atom_len = 76;
		else if (len >= 1292 && buf[68] == 0)
			atom_len = 76;
		else
			atom_len = 68;
		offset = 0;
		break;
	case FIRMWARE_TYPE_INTERSIL:
		offset = 4;
		if (priv->has_hostscan) {
			atom_len = le16_to_cpup((__le16 *)buf);
			/* Sanity check for atom_len */
			if (atom_len < sizeof(struct prism2_scan_apinfo)) {
				printk(KERN_ERR "%s: Invalid atom_len in scan "
				       "data: %d\n", priv->ndev->name,
				       atom_len);
				return -EIO;
			}
		} else
			atom_len = offsetof(struct prism2_scan_apinfo, atim);
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* Check that we got an whole number of atoms */
	if ((len - offset) % atom_len) {
		printk(KERN_ERR "%s: Unexpected scan data length %d, "
		       "atom_len %d, offset %d\n", priv->ndev->name, len,
		       atom_len, offset);
		return -EIO;
	}

	orinoco_clear_scan_results(priv, msecs_to_jiffies(15000));

	/* Read the entries one by one */
	for (; offset + atom_len <= len; offset += atom_len) {
		int found = 0;
		struct bss_element *bss = NULL;

		/* Get next atom */
		atom = (union hermes_scan_info *) (buf + offset);

		/* Try to update an existing bss first */
		list_for_each_entry(bss, &priv->bss_list, list) {
			if (compare_ether_addr(bss->bss.a.bssid, atom->a.bssid))
				continue;
			if (le16_to_cpu(bss->bss.a.essid_len) !=
			      le16_to_cpu(atom->a.essid_len))
				continue;
			if (memcmp(bss->bss.a.essid, atom->a.essid,
			      le16_to_cpu(atom->a.essid_len)))
				continue;
			found = 1;
			break;
		}

		/* Grab a bss off the free list */
		if (!found && !list_empty(&priv->bss_free_list)) {
			bss = list_entry(priv->bss_free_list.next,
					 struct bss_element, list);
			list_del(priv->bss_free_list.next);

			list_add_tail(&bss->list, &priv->bss_list);
		}

		if (bss) {
			/* Always update the BSS to get latest beacon info */
			memcpy(&bss->bss, atom, sizeof(bss->bss));
			bss->last_scanned = jiffies;
		}
	}

	return 0;
}
