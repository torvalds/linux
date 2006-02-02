/*
 * card.c - contains functions for managing groups of PnP devices
 *
 * Copyright 2002 Adam Belay <ambx1@neo.rr.com>
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pnp.h>
#include "base.h"

LIST_HEAD(pnp_cards);
static LIST_HEAD(pnp_card_drivers);


static const struct pnp_card_device_id * match_card(struct pnp_card_driver * drv, struct pnp_card * card)
{
	const struct pnp_card_device_id * drv_id = drv->id_table;
	while (*drv_id->id){
		if (compare_pnp_id(card->id,drv_id->id)) {
			int i = 0;
			for (;;) {
				int found;
				struct pnp_dev *dev;
				if (i == PNP_MAX_DEVICES || ! *drv_id->devs[i].id)
					return drv_id;
				found = 0;
				card_for_each_dev(card, dev) {
					if (compare_pnp_id(dev->id, drv_id->devs[i].id)) {
						found = 1;
						break;
					}
				}
				if (! found)
					break;
				i++;
			}
		}
		drv_id++;
	}
	return NULL;
}

static void card_remove(struct pnp_dev * dev)
{
	dev->card_link = NULL;
}
 
static void card_remove_first(struct pnp_dev * dev)
{
	struct pnp_card_driver * drv = to_pnp_card_driver(dev->driver);
	if (!dev->card || !drv)
		return;
	if (drv->remove)
		drv->remove(dev->card_link);
	drv->link.remove = &card_remove;
	kfree(dev->card_link);
	card_remove(dev);
}

static int card_probe(struct pnp_card * card, struct pnp_card_driver * drv)
{
	const struct pnp_card_device_id *id = match_card(drv,card);
	if (id) {
		struct pnp_card_link * clink = pnp_alloc(sizeof(struct pnp_card_link));
		if (!clink)
			return 0;
		clink->card = card;
		clink->driver = drv;
		clink->pm_state = PMSG_ON;
		if (drv->probe) {
			if (drv->probe(clink, id)>=0)
				return 1;
			else {
				struct pnp_dev * dev;
				card_for_each_dev(card, dev) {
					if (dev->card_link == clink)
						pnp_release_card_device(dev);
				}
				kfree(clink);
			}
		} else
			return 1;
	}
	return 0;
}

/**
 * pnp_add_card_id - adds an EISA id to the specified card
 * @id: pointer to a pnp_id structure
 * @card: pointer to the desired card
 *
 */

int pnp_add_card_id(struct pnp_id *id, struct pnp_card * card)
{
	struct pnp_id * ptr;
	if (!id)
		return -EINVAL;
	if (!card)
		return -EINVAL;
	id->next = NULL;
	ptr = card->id;
	while (ptr && ptr->next)
		ptr = ptr->next;
	if (ptr)
		ptr->next = id;
	else
		card->id = id;
	return 0;
}

static void pnp_free_card_ids(struct pnp_card * card)
{
	struct pnp_id * id;
	struct pnp_id *next;
	if (!card)
		return;
	id = card->id;
	while (id) {
		next = id->next;
		kfree(id);
		id = next;
	}
}

static void pnp_release_card(struct device *dmdev)
{
	struct pnp_card * card = to_pnp_card(dmdev);
	pnp_free_card_ids(card);
	kfree(card);
}


static ssize_t pnp_show_card_name(struct device *dmdev, struct device_attribute *attr, char *buf)
{
	char *str = buf;
	struct pnp_card *card = to_pnp_card(dmdev);
	str += sprintf(str,"%s\n", card->name);
	return (str - buf);
}

static DEVICE_ATTR(name,S_IRUGO,pnp_show_card_name,NULL);

static ssize_t pnp_show_card_ids(struct device *dmdev, struct device_attribute *attr, char *buf)
{
	char *str = buf;
	struct pnp_card *card = to_pnp_card(dmdev);
	struct pnp_id * pos = card->id;

	while (pos) {
		str += sprintf(str,"%s\n", pos->id);
		pos = pos->next;
	}
	return (str - buf);
}

static DEVICE_ATTR(card_id,S_IRUGO,pnp_show_card_ids,NULL);

static int pnp_interface_attach_card(struct pnp_card *card)
{
	device_create_file(&card->dev,&dev_attr_name);
	device_create_file(&card->dev,&dev_attr_card_id);
	return 0;
}

/**
 * pnp_add_card - adds a PnP card to the PnP Layer
 * @card: pointer to the card to add
 */

int pnp_add_card(struct pnp_card * card)
{
	int error;
	struct list_head * pos, * temp;
	if (!card || !card->protocol)
		return -EINVAL;

	sprintf(card->dev.bus_id, "%02x:%02x", card->protocol->number, card->number);
	card->dev.parent = &card->protocol->dev;
	card->dev.bus = NULL;
	card->dev.release = &pnp_release_card;
	error = device_register(&card->dev);

	if (error == 0) {
		pnp_interface_attach_card(card);
		spin_lock(&pnp_lock);
		list_add_tail(&card->global_list, &pnp_cards);
		list_add_tail(&card->protocol_list, &card->protocol->cards);
		spin_unlock(&pnp_lock);

		/* we wait until now to add devices in order to ensure the drivers
		 * will be able to use all of the related devices on the card
		 * without waiting any unresonable length of time */
		list_for_each(pos,&card->devices){
			struct pnp_dev *dev = card_to_pnp_dev(pos);
			__pnp_add_device(dev);
		}

		/* match with card drivers */
		list_for_each_safe(pos,temp,&pnp_card_drivers){
			struct pnp_card_driver * drv = list_entry(pos, struct pnp_card_driver, global_list);
			card_probe(card,drv);
		}
	} else
		pnp_err("sysfs failure, card '%s' will be unavailable", card->dev.bus_id);
	return error;
}

/**
 * pnp_remove_card - removes a PnP card from the PnP Layer
 * @card: pointer to the card to remove
 */

void pnp_remove_card(struct pnp_card * card)
{
	struct list_head *pos, *temp;
	if (!card)
		return;
	device_unregister(&card->dev);
	spin_lock(&pnp_lock);
	list_del(&card->global_list);
	list_del(&card->protocol_list);
	spin_unlock(&pnp_lock);
	list_for_each_safe(pos,temp,&card->devices){
		struct pnp_dev *dev = card_to_pnp_dev(pos);
		pnp_remove_card_device(dev);
	}
}

/**
 * pnp_add_card_device - adds a device to the specified card
 * @card: pointer to the card to add to
 * @dev: pointer to the device to add
 */

int pnp_add_card_device(struct pnp_card * card, struct pnp_dev * dev)
{
	if (!card || !dev || !dev->protocol)
		return -EINVAL;
	dev->dev.parent = &card->dev;
	dev->card_link = NULL;
	snprintf(dev->dev.bus_id, BUS_ID_SIZE, "%02x:%02x.%02x", dev->protocol->number,
		 card->number,dev->number);
	spin_lock(&pnp_lock);
	dev->card = card;
	list_add_tail(&dev->card_list, &card->devices);
	spin_unlock(&pnp_lock);
	return 0;
}

/**
 * pnp_remove_card_device- removes a device from the specified card
 * @dev: pointer to the device to remove
 */

void pnp_remove_card_device(struct pnp_dev * dev)
{
	spin_lock(&pnp_lock);
	dev->card = NULL;
	list_del(&dev->card_list);
	spin_unlock(&pnp_lock);
	__pnp_remove_device(dev);
}

/**
 * pnp_request_card_device - Searches for a PnP device under the specified card
 * @clink: pointer to the card link, cannot be NULL
 * @id: pointer to a PnP ID structure that explains the rules for finding the device
 * @from: Starting place to search from. If NULL it will start from the begining.
 */

struct pnp_dev * pnp_request_card_device(struct pnp_card_link *clink, const char * id, struct pnp_dev * from)
{
	struct list_head * pos;
	struct pnp_dev * dev;
	struct pnp_card_driver * drv;
	struct pnp_card * card;
	if (!clink || !id)
		goto done;
	card = clink->card;
	drv = clink->driver;
	if (!from) {
		pos = card->devices.next;
	} else {
		if (from->card != card)
			goto done;
		pos = from->card_list.next;
	}
	while (pos != &card->devices) {
		dev = card_to_pnp_dev(pos);
		if ((!dev->card_link) && compare_pnp_id(dev->id,id))
			goto found;
		pos = pos->next;
	}

done:
	return NULL;

found:
	down_write(&dev->dev.bus->subsys.rwsem);
	dev->card_link = clink;
	dev->dev.driver = &drv->link.driver;
	if (drv->link.driver.probe) {
		if (drv->link.driver.probe(&dev->dev)) {
			dev->dev.driver = NULL;
			dev->card_link = NULL;
			up_write(&dev->dev.bus->subsys.rwsem);
			return NULL;
		}
	}
	device_bind_driver(&dev->dev);
	up_write(&dev->dev.bus->subsys.rwsem);

	return dev;
}

/**
 * pnp_release_card_device - call this when the driver no longer needs the device
 * @dev: pointer to the PnP device stucture
 */

void pnp_release_card_device(struct pnp_dev * dev)
{
	struct pnp_card_driver * drv = dev->card_link->driver;
	if (!drv)
		return;
	down_write(&dev->dev.bus->subsys.rwsem);
	drv->link.remove = &card_remove;
	device_release_driver(&dev->dev);
	drv->link.remove = &card_remove_first;
	up_write(&dev->dev.bus->subsys.rwsem);
}

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

int pnp_register_card_driver(struct pnp_card_driver * drv)
{
	int count;
	struct list_head *pos, *temp;

	drv->link.name = drv->name;
	drv->link.id_table = NULL;	/* this will disable auto matching */
	drv->link.flags = drv->flags;
	drv->link.probe = NULL;
	drv->link.remove = &card_remove_first;
	drv->link.suspend = drv->suspend ? card_suspend : NULL;
	drv->link.resume = drv->resume ? card_resume : NULL;

	count = pnp_register_driver(&drv->link);
	if (count < 0)
		return count;

	spin_lock(&pnp_lock);
	list_add_tail(&drv->global_list, &pnp_card_drivers);
	spin_unlock(&pnp_lock);

	count = 0;

	list_for_each_safe(pos,temp,&pnp_cards){
		struct pnp_card *card = list_entry(pos, struct pnp_card, global_list);
		count += card_probe(card,drv);
	}
	return count;
}

/**
 * pnp_unregister_card_driver - unregisters a PnP card driver from the PnP Layer
 * @drv: pointer to the driver to unregister
 */

void pnp_unregister_card_driver(struct pnp_card_driver * drv)
{
	spin_lock(&pnp_lock);
	list_del(&drv->global_list);
	spin_unlock(&pnp_lock);
	pnp_unregister_driver(&drv->link);
}

#if 0
EXPORT_SYMBOL(pnp_add_card);
EXPORT_SYMBOL(pnp_remove_card);
EXPORT_SYMBOL(pnp_add_card_device);
EXPORT_SYMBOL(pnp_remove_card_device);
EXPORT_SYMBOL(pnp_add_card_id);
#endif  /*  0  */
EXPORT_SYMBOL(pnp_request_card_device);
EXPORT_SYMBOL(pnp_release_card_device);
EXPORT_SYMBOL(pnp_register_card_driver);
EXPORT_SYMBOL(pnp_unregister_card_driver);
