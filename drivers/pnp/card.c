// SPDX-License-Identifier: GPL-2.0
/*
 * card.c - contains functions for managing groups of PnP devices
 *
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/pnp.h>
#include <linux/dma-mapping.h>
#include "base.h"

LIST_HEAD(pnp_cards);
static LIST_HEAD(pnp_card_drivers);

static const struct pnp_card_device_id *match_card(struct pnp_card_driver *drv,
						   struct pnp_card *card)
{
	const struct pnp_card_device_id *drv_id = drv->id_table;

	while (*drv_id->id) {
		if (compare_pnp_id(card->id, drv_id->id)) {
			int i = 0;

			for (;;) {
				int found;
				struct pnp_dev *dev;

				if (i == PNP_MAX_DEVICES ||
				    !*drv_id->devs[i].id)
					return drv_id;
				found = 0;
				card_for_each_dev(card, dev) {
					if (compare_pnp_id(dev->id,
						   drv_id->devs[i].id)) {
						found = 1;
						break;
					}
				}
				if (!found)
					break;
				i++;
			}
		}
		drv_id++;
	}
	return NULL;
}

static void card_remove(struct pnp_dev *dev)
{
	dev->card_link = NULL;
}

static void card_remove_first(struct pnp_dev *dev)
{
	struct pnp_card_driver *drv = to_pnp_card_driver(dev->driver);

	if (!dev->card || !drv)
		return;
	if (drv->remove)
		drv->remove(dev->card_link);
	drv->link.remove = &card_remove;
	kfree(dev->card_link);
	card_remove(dev);
}

static int card_probe(struct pnp_card *card, struct pnp_card_driver *drv)
{
	const struct pnp_card_device_id *id;
	struct pnp_card_link *clink;
	struct pnp_dev *dev;

	if (!drv->probe)
		return 0;
	id = match_card(drv, card);
	if (!id)
		return 0;

	clink = kzalloc(sizeof(*clink), GFP_KERNEL);
	if (!clink)
		return 0;
	clink->card = card;
	clink->driver = drv;
	clink->pm_state = PMSG_ON;

	if (drv->probe(clink, id) >= 0)
		return 1;

	/* Recovery */
	card_for_each_dev(card, dev) {
		if (dev->card_link == clink)
			pnp_release_card_device(dev);
	}
	kfree(clink);
	return 0;
}

/**
 * pnp_add_card_id - adds an EISA id to the specified card
 * @id: pointer to a pnp_id structure
 * @card: pointer to the desired card
 */
static struct pnp_id *pnp_add_card_id(struct pnp_card *card, char *id)
{
	struct pnp_id *dev_id, *ptr;

	dev_id = kzalloc(sizeof(struct pnp_id), GFP_KERNEL);
	if (!dev_id)
		return NULL;

	dev_id->id[0] = id[0];
	dev_id->id[1] = id[1];
	dev_id->id[2] = id[2];
	dev_id->id[3] = tolower(id[3]);
	dev_id->id[4] = tolower(id[4]);
	dev_id->id[5] = tolower(id[5]);
	dev_id->id[6] = tolower(id[6]);
	dev_id->id[7] = '\0';

	dev_id->next = NULL;
	ptr = card->id;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = dev_id;
	else
		card->id = dev_id;

	return dev_id;
}

static void pnp_free_card_ids(struct pnp_card *card)
{
	struct pnp_id *id;
	struct pnp_id *next;

	id = card->id;
	while (id) {
		next = id->next;
		kfree(id);
		id = next;
	}
}

static void pnp_release_card(struct device *dmdev)
{
	struct pnp_card *card = to_pnp_card(dmdev);

	pnp_free_card_ids(card);
	kfree(card);
}

struct pnp_card *pnp_alloc_card(struct pnp_protocol *protocol, int id, char *pnpid)
{
	struct pnp_card *card;
	struct pnp_id *dev_id;

	card = kzalloc(sizeof(struct pnp_card), GFP_KERNEL);
	if (!card)
		return NULL;

	card->protocol = protocol;
	card->number = id;

	card->dev.parent = &card->protocol->dev;
	dev_set_name(&card->dev, "%02x:%02x", card->protocol->number, card->number);

	card->dev.coherent_dma_mask = DMA_BIT_MASK(24);
	card->dev.dma_mask = &card->dev.coherent_dma_mask;

	dev_id = pnp_add_card_id(card, pnpid);
	if (!dev_id) {
		kfree(card);
		return NULL;
	}

	return card;
}

static ssize_t name_show(struct device *dmdev,
			 struct device_attribute *attr, char *buf)
{
	char *str = buf;
	struct pnp_card *card = to_pnp_card(dmdev);

	str += sprintf(str, "%s\n", card->name);
	return (str - buf);
}

static DEVICE_ATTR_RO(name);

static ssize_t card_id_show(struct device *dmdev,
			    struct device_attribute *attr, char *buf)
{
	char *str = buf;
	struct pnp_card *card = to_pnp_card(dmdev);
	struct pnp_id *pos = card->id;

	while (pos) {
		str += sprintf(str, "%s\n", pos->id);
		pos = pos->next;
	}
	return (str - buf);
}

static DEVICE_ATTR_RO(card_id);

static int pnp_interface_attach_card(struct pnp_card *card)
{
	int rc = device_create_file(&card->dev, &dev_attr_name);

	if (rc)
		return rc;

	rc = device_create_file(&card->dev, &dev_attr_card_id);
	if (rc)
		goto err_name;

	return 0;

err_name:
	device_remove_file(&card->dev, &dev_attr_name);
	return rc;
}

/**
 * pnp_add_card - adds a PnP card to the PnP Layer
 * @card: pointer to the card to add
 */
int pnp_add_card(struct pnp_card *card)
{
	int error;
	struct list_head *pos, *temp;

	card->dev.bus = NULL;
	card->dev.release = &pnp_release_card;
	error = device_register(&card->dev);
	if (error) {
		dev_err(&card->dev, "could not register (err=%d)\n", error);
		put_device(&card->dev);
		return error;
	}

	pnp_interface_attach_card(card);
	mutex_lock(&pnp_lock);
	list_add_tail(&card->global_list, &pnp_cards);
	list_add_tail(&card->protocol_list, &card->protocol->cards);
	mutex_unlock(&pnp_lock);

	/* we wait until now to add devices in order to ensure the drivers
	 * will be able to use all of the related devices on the card
	 * without waiting an unreasonable length of time */
	list_for_each(pos, &card->devices) {
		struct pnp_dev *dev = card_to_pnp_dev(pos);
		__pnp_add_device(dev);
	}

	/* match with card drivers */
	list_for_each_safe(pos, temp, &pnp_card_drivers) {
		struct pnp_card_driver *drv =
		    list_entry(pos, struct pnp_card_driver,
			       global_list);
		card_probe(card, drv);
	}
	return 0;
}

/**
 * pnp_remove_card - removes a PnP card from the PnP Layer
 * @card: pointer to the card to remove
 */
void pnp_remove_card(struct pnp_card *card)
{
	struct list_head *pos, *temp;

	device_unregister(&card->dev);
	mutex_lock(&pnp_lock);
	list_del(&card->global_list);
	list_del(&card->protocol_list);
	mutex_unlock(&pnp_lock);
	list_for_each_safe(pos, temp, &card->devices) {
		struct pnp_dev *dev = card_to_pnp_dev(pos);
		pnp_remove_card_device(dev);
	}
}

/**
 * pnp_add_card_device - adds a device to the specified card
 * @card: pointer to the card to add to
 * @dev: pointer to the device to add
 */
int pnp_add_card_device(struct pnp_card *card, struct pnp_dev *dev)
{
	dev->dev.parent = &card->dev;
	dev->card_link = NULL;
	dev_set_name(&dev->dev, "%02x:%02x.%02x",
		     dev->protocol->number, card->number, dev->number);
	mutex_lock(&pnp_lock);
	dev->card = card;
	list_add_tail(&dev->card_list, &card->devices);
	mutex_unlock(&pnp_lock);
	return 0;
}

/**
 * pnp_remove_card_device- removes a device from the specified card
 * @dev: pointer to the device to remove
 */
void pnp_remove_card_device(struct pnp_dev *dev)
{
	mutex_lock(&pnp_lock);
	dev->card = NULL;
	list_del(&dev->card_list);
	mutex_unlock(&pnp_lock);
	__pnp_remove_device(dev);
}

/**
 * pnp_request_card_device - Searches for a PnP device under the specified card
 * @clink: pointer to the card link, cannot be NULL
 * @id: pointer to a PnP ID structure that explains the rules for finding the device
 * @from: Starting place to search from. If NULL it will start from the beginning.
 */
struct pnp_dev *pnp_request_card_device(struct pnp_card_link *clink,
					const char *id, struct pnp_dev *from)
{
	struct list_head *pos;
	struct pnp_dev *dev;
	struct pnp_card_driver *drv;
	struct pnp_card *card;

	if (!clink || !id)
		return NULL;

	card = clink->card;
	drv = clink->driver;
	if (!from) {
		pos = card->devices.next;
	} else {
		if (from->card != card)
			return NULL;
		pos = from->card_list.next;
	}
	while (pos != &card->devices) {
		dev = card_to_pnp_dev(pos);
		if ((!dev->card_link) && compare_pnp_id(dev->id, id))
			goto found;
		pos = pos->next;
	}

	return NULL;

found:
	dev->card_link = clink;
	dev->dev.driver = &drv->link.driver;
	if (pnp_bus_type.probe(&dev->dev))
		goto err_out;
	if (device_bind_driver(&dev->dev))
		goto err_out;

	return dev;

err_out:
	dev->dev.driver = NULL;
	dev->card_link = NULL;
	return NULL;
}
EXPORT_SYMBOL(pnp_request_card_device);

/**
 * pnp_release_card_device - call this when the driver no longer needs the device
 * @dev: pointer to the PnP device structure
 */
void pnp_release_card_device(struct pnp_dev *dev)
{
	struct pnp_card_driver *drv = dev->card_link->driver;

	drv->link.remove = &card_remove;
	device_release_driver(&dev->dev);
	drv->link.remove = &card_remove_first;
}
EXPORT_SYMBOL(pnp_release_card_device);

/*
 * suspend/resume callbacks
 */
static int card_suspend(struct pnp_dev *dev, pm_message_t state)
{
	struct pnp_card_link *link = dev->card_link;

	if (link->pm_state.event == state.event)
		return 0;
	link->pm_state = state;
	return link->driver->suspend(link, state);
}

static int card_resume(struct pnp_dev *dev)
{
	struct pnp_card_link *link = dev->card_link;

	if (link->pm_state.event == PM_EVENT_ON)
		return 0;
	link->pm_state = PMSG_ON;
	link->driver->resume(link);
	return 0;
}

/**
 * pnp_register_card_driver - registers a PnP card driver with the PnP Layer
 * @drv: pointer to the driver to register
 */
int pnp_register_card_driver(struct pnp_card_driver *drv)
{
	int error;
	struct list_head *pos, *temp;

	drv->link.name = drv->name;
	drv->link.id_table = NULL;	/* this will disable auto matching */
	drv->link.flags = drv->flags;
	drv->link.probe = NULL;
	drv->link.remove = &card_remove_first;
	drv->link.suspend = drv->suspend ? card_suspend : NULL;
	drv->link.resume = drv->resume ? card_resume : NULL;

	error = pnp_register_driver(&drv->link);
	if (error < 0)
		return error;

	mutex_lock(&pnp_lock);
	list_add_tail(&drv->global_list, &pnp_card_drivers);
	mutex_unlock(&pnp_lock);

	list_for_each_safe(pos, temp, &pnp_cards) {
		struct pnp_card *card =
		    list_entry(pos, struct pnp_card, global_list);
		card_probe(card, drv);
	}
	return 0;
}
EXPORT_SYMBOL(pnp_register_card_driver);

/**
 * pnp_unregister_card_driver - unregisters a PnP card driver from the PnP Layer
 * @drv: pointer to the driver to unregister
 */
void pnp_unregister_card_driver(struct pnp_card_driver *drv)
{
	mutex_lock(&pnp_lock);
	list_del(&drv->global_list);
	mutex_unlock(&pnp_lock);
	pnp_unregister_driver(&drv->link);
}
EXPORT_SYMBOL(pnp_unregister_card_driver);
