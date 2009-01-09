/*
 * WUSB Wire Adapter: WLP interface
 * Ethernet to device address cache
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * We need to be able to map ethernet addresses to device addresses
 * and back because there is not explicit relationship between the eth
 * addresses used in the ETH frames and the device addresses (no, it
 * would not have been simpler to force as ETH address the MBOA MAC
 * address...no, not at all :).
 *
 * A device has one MBOA MAC address and one device address. It is possible
 * for a device to have more than one virtual MAC address (although a
 * virtual address can be the same as the MBOA MAC address). The device
 * address is guaranteed to be unique among the devices in the extended
 * beacon group (see ECMA 17.1.1). We thus use the device address as index
 * to this cache. We do allow searching based on virtual address as this
 * is how Ethernet frames will be addressed.
 *
 * We need to support virtual EUI-48. Although, right now the virtual
 * EUI-48 will always be the same as the MAC SAP address. The EDA cache
 * entry thus contains a MAC SAP address as well as the virtual address
 * (used to map the network stack address to a neighbor). When we move
 * to support more than one virtual MAC on a host then this organization
 * will have to change. Perhaps a neighbor has a list of WSSs, each with a
 * tag and virtual EUI-48.
 *
 * On data transmission
 * it is used to determine if the neighbor is connected and what WSS it
 * belongs to. With this we know what tag to add to the WLP frame. Storing
 * the WSS in the EDA cache may be overkill because we only support one
 * WSS. Hopefully we will support more than one WSS at some point.
 * On data reception it is used to determine the WSS based on
 * the tag and address of the transmitting neighbor.
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/wlp.h>
#include "wlp-internal.h"


/* FIXME: cache is not purged, only on device close */

/* FIXME: does not scale, change to dynamic array */

/*
 * Initialize the EDA cache
 *
 * @returns 0 if ok, < 0 errno code on error
 *
 * Call when the interface is being brought up
 *
 * NOTE: Keep it as a separate function as the implementation will
 *       change and be more complex.
 */
void wlp_eda_init(struct wlp_eda *eda)
{
	INIT_LIST_HEAD(&eda->cache);
	spin_lock_init(&eda->lock);
}

/*
 * Release the EDA cache
 *
 * @returns 0 if ok, < 0 errno code on error
 *
 * Called when the interface is brought down
 */
void wlp_eda_release(struct wlp_eda *eda)
{
	unsigned long flags;
	struct wlp_eda_node *itr, *next;

	spin_lock_irqsave(&eda->lock, flags);
	list_for_each_entry_safe(itr, next, &eda->cache, list_node) {
		list_del(&itr->list_node);
		kfree(itr);
	}
	spin_unlock_irqrestore(&eda->lock, flags);
}

/*
 * Add an address mapping
 *
 * @returns 0 if ok, < 0 errno code on error
 *
 * An address mapping is initially created when the neighbor device is seen
 * for the first time (it is "onair"). At this time the neighbor is not
 * connected or associated with a WSS so we only populate the Ethernet and
 * Device address fields.
 *
 */
int wlp_eda_create_node(struct wlp_eda *eda,
			const unsigned char eth_addr[ETH_ALEN],
			const struct uwb_dev_addr *dev_addr)
{
	int result = 0;
	struct wlp_eda_node *itr;
	unsigned long flags;

	BUG_ON(dev_addr == NULL || eth_addr == NULL);
	spin_lock_irqsave(&eda->lock, flags);
	list_for_each_entry(itr, &eda->cache, list_node) {
		if (!memcmp(&itr->dev_addr, dev_addr, sizeof(itr->dev_addr))) {
			printk(KERN_ERR "EDA cache already contains entry "
			       "for neighbor %02x:%02x\n",
			       dev_addr->data[1], dev_addr->data[0]);
			result = -EEXIST;
			goto out_unlock;
		}
	}
	itr = kzalloc(sizeof(*itr), GFP_ATOMIC);
	if (itr != NULL) {
		memcpy(itr->eth_addr, eth_addr, sizeof(itr->eth_addr));
		itr->dev_addr = *dev_addr;
		list_add(&itr->list_node, &eda->cache);
	} else
		result = -ENOMEM;
out_unlock:
	spin_unlock_irqrestore(&eda->lock, flags);
	return result;
}

/*
 * Remove entry from EDA cache
 *
 * This is done when the device goes off air.
 */
void wlp_eda_rm_node(struct wlp_eda *eda, const struct uwb_dev_addr *dev_addr)
{
	struct wlp_eda_node *itr, *next;
	unsigned long flags;

	spin_lock_irqsave(&eda->lock, flags);
	list_for_each_entry_safe(itr, next, &eda->cache, list_node) {
		if (!memcmp(&itr->dev_addr, dev_addr, sizeof(itr->dev_addr))) {
			list_del(&itr->list_node);
			kfree(itr);
			break;
		}
	}
	spin_unlock_irqrestore(&eda->lock, flags);
}

/*
 * Update an address mapping
 *
 * @returns 0 if ok, < 0 errno code on error
 */
int wlp_eda_update_node(struct wlp_eda *eda,
			const struct uwb_dev_addr *dev_addr,
			struct wlp_wss *wss,
			const unsigned char virt_addr[ETH_ALEN],
			const u8 tag, const enum wlp_wss_connect state)
{
	int result = -ENOENT;
	struct wlp_eda_node *itr;
	unsigned long flags;

	spin_lock_irqsave(&eda->lock, flags);
	list_for_each_entry(itr, &eda->cache, list_node) {
		if (!memcmp(&itr->dev_addr, dev_addr, sizeof(itr->dev_addr))) {
			/* Found it, update it */
			itr->wss = wss;
			memcpy(itr->virt_addr, virt_addr,
			       sizeof(itr->virt_addr));
			itr->tag = tag;
			itr->state = state;
			result = 0;
			goto out_unlock;
		}
	}
	/* Not found */
out_unlock:
	spin_unlock_irqrestore(&eda->lock, flags);
	return result;
}

/*
 * Update only state field of an address mapping
 *
 * @returns 0 if ok, < 0 errno code on error
 */
int wlp_eda_update_node_state(struct wlp_eda *eda,
			      const struct uwb_dev_addr *dev_addr,
			      const enum wlp_wss_connect state)
{
	int result = -ENOENT;
	struct wlp_eda_node *itr;
	unsigned long flags;

	spin_lock_irqsave(&eda->lock, flags);
	list_for_each_entry(itr, &eda->cache, list_node) {
		if (!memcmp(&itr->dev_addr, dev_addr, sizeof(itr->dev_addr))) {
			/* Found it, update it */
			itr->state = state;
			result = 0;
			goto out_unlock;
		}
	}
	/* Not found */
out_unlock:
	spin_unlock_irqrestore(&eda->lock, flags);
	return result;
}

/*
 * Return contents of EDA cache entry
 *
 * @dev_addr: index to EDA cache
 * @eda_entry: pointer to where contents of EDA cache will be copied
 */
int wlp_copy_eda_node(struct wlp_eda *eda, struct uwb_dev_addr *dev_addr,
		      struct wlp_eda_node *eda_entry)
{
	int result = -ENOENT;
	struct wlp_eda_node *itr;
	unsigned long flags;

	spin_lock_irqsave(&eda->lock, flags);
	list_for_each_entry(itr, &eda->cache, list_node) {
		if (!memcmp(&itr->dev_addr, dev_addr, sizeof(itr->dev_addr))) {
			*eda_entry = *itr;
			result = 0;
			goto out_unlock;
		}
	}
	/* Not found */
out_unlock:
	spin_unlock_irqrestore(&eda->lock, flags);
	return result;
}

/*
 * Execute function for every element in the cache
 *
 * @function: function to execute on element of cache (must be atomic)
 * @priv:     private data of function
 * @returns:  result of first function that failed, or last function
 *            executed if no function failed.
 *
 * Stop executing when function returns error for any element in cache.
 *
 * IMPORTANT: We are using a spinlock here: the function executed on each
 * element has to be atomic.
 */
int wlp_eda_for_each(struct wlp_eda *eda, wlp_eda_for_each_f function,
		     void *priv)
{
	int result = 0;
	struct wlp *wlp = container_of(eda, struct wlp, eda);
	struct wlp_eda_node *entry;
	unsigned long flags;

	spin_lock_irqsave(&eda->lock, flags);
	list_for_each_entry(entry, &eda->cache, list_node) {
		result = (*function)(wlp, entry, priv);
		if (result < 0)
			break;
	}
	spin_unlock_irqrestore(&eda->lock, flags);
	return result;
}

/*
 * Execute function for single element in the cache (return dev addr)
 *
 * @virt_addr: index into EDA cache used to determine which element to
 *             execute the function on
 * @dev_addr: device address of element in cache will be returned using
 *            @dev_addr
 * @function: function to execute on element of cache (must be atomic)
 * @priv:     private data of function
 * @returns:  result of function
 *
 * IMPORTANT: We are using a spinlock here: the function executed on the
 * element has to be atomic.
 */
int wlp_eda_for_virtual(struct wlp_eda *eda,
			const unsigned char virt_addr[ETH_ALEN],
			struct uwb_dev_addr *dev_addr,
			wlp_eda_for_each_f function,
			void *priv)
{
	int result = 0;
	struct wlp *wlp = container_of(eda, struct wlp, eda);
	struct wlp_eda_node *itr;
	unsigned long flags;
	int found = 0;

	spin_lock_irqsave(&eda->lock, flags);
	list_for_each_entry(itr, &eda->cache, list_node) {
		if (!memcmp(itr->virt_addr, virt_addr,
			   sizeof(itr->virt_addr))) {
			result = (*function)(wlp, itr, priv);
			*dev_addr = itr->dev_addr;
			found = 1;
			break;
		}
	}
	if (!found)
		result = -ENODEV;
	spin_unlock_irqrestore(&eda->lock, flags);
	return result;
}

static const char *__wlp_wss_connect_state[] = { "WLP_WSS_UNCONNECTED",
					  "WLP_WSS_CONNECTED",
					  "WLP_WSS_CONNECT_FAILED",
};

static const char *wlp_wss_connect_state_str(unsigned id)
{
	if (id >= ARRAY_SIZE(__wlp_wss_connect_state))
		return "unknown WSS connection state";
	return __wlp_wss_connect_state[id];
}

/*
 * View EDA cache from user space
 *
 * A debugging feature to give user visibility into the EDA cache. Also
 * used to display members of WSS to user (called from wlp_wss_members_show())
 */
ssize_t wlp_eda_show(struct wlp *wlp, char *buf)
{
	ssize_t result = 0;
	struct wlp_eda_node *entry;
	unsigned long flags;
	struct wlp_eda *eda = &wlp->eda;
	spin_lock_irqsave(&eda->lock, flags);
	result = scnprintf(buf, PAGE_SIZE, "#eth_addr dev_addr wss_ptr "
			   "tag state virt_addr\n");
	list_for_each_entry(entry, &eda->cache, list_node) {
		result += scnprintf(buf + result, PAGE_SIZE - result,
				    "%pM %02x:%02x %p 0x%02x %s %pM\n",
				    entry->eth_addr,
				    entry->dev_addr.data[1],
				    entry->dev_addr.data[0], entry->wss,
				    entry->tag,
				    wlp_wss_connect_state_str(entry->state),
				    entry->virt_addr);
		if (result >= PAGE_SIZE)
			break;
	}
	spin_unlock_irqrestore(&eda->lock, flags);
	return result;
}
EXPORT_SYMBOL_GPL(wlp_eda_show);

/*
 * Add new EDA cache entry based on user input in sysfs
 *
 * Should only be used for debugging.
 *
 * The WSS is assumed to be the only WSS supported. This needs to be
 * redesigned when we support more than one WSS.
 */
ssize_t wlp_eda_store(struct wlp *wlp, const char *buf, size_t size)
{
	ssize_t result;
	struct wlp_eda *eda = &wlp->eda;
	u8 eth_addr[6];
	struct uwb_dev_addr dev_addr;
	u8 tag;
	unsigned state;

	result = sscanf(buf, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx "
			"%02hhx:%02hhx %02hhx %u\n",
			&eth_addr[0], &eth_addr[1],
			&eth_addr[2], &eth_addr[3],
			&eth_addr[4], &eth_addr[5],
			&dev_addr.data[1], &dev_addr.data[0], &tag, &state);
	switch (result) {
	case 6: /* no dev addr specified -- remove entry NOT IMPLEMENTED */
		/*result = wlp_eda_rm(eda, eth_addr, &dev_addr);*/
		result = -ENOSYS;
		break;
	case 10:
		state = state >= 1 ? 1 : 0;
		result = wlp_eda_create_node(eda, eth_addr, &dev_addr);
		if (result < 0 && result != -EEXIST)
			goto error;
		/* Set virtual addr to be same as MAC */
		result = wlp_eda_update_node(eda, &dev_addr, &wlp->wss,
					     eth_addr, tag, state);
		if (result < 0)
			goto error;
		break;
	default: /* bad format */
		result = -EINVAL;
	}
error:
	return result < 0 ? result : size;
}
EXPORT_SYMBOL_GPL(wlp_eda_store);
