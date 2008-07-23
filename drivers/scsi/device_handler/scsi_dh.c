/*
 * SCSI device handler infrastruture.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright IBM Corporation, 2007
 *      Authors:
 *               Chandra Seetharaman <sekharan@us.ibm.com>
 *               Mike Anderson <andmike@linux.vnet.ibm.com>
 */

#include <scsi/scsi_dh.h>
#include "../scsi_priv.h"

static DEFINE_SPINLOCK(list_lock);
static LIST_HEAD(scsi_dh_list);

static struct scsi_device_handler *get_device_handler(const char *name)
{
	struct scsi_device_handler *tmp, *found = NULL;

	spin_lock(&list_lock);
	list_for_each_entry(tmp, &scsi_dh_list, list) {
		if (!strcmp(tmp->name, name)) {
			found = tmp;
			break;
		}
	}
	spin_unlock(&list_lock);
	return found;
}

static int scsi_dh_notifier_add(struct device *dev, void *data)
{
	struct scsi_device_handler *scsi_dh = data;

	scsi_dh->nb.notifier_call(&scsi_dh->nb, BUS_NOTIFY_ADD_DEVICE, dev);
	return 0;
}

/*
 * scsi_register_device_handler - register a device handler personality
 *      module.
 * @scsi_dh - device handler to be registered.
 *
 * Returns 0 on success, -EBUSY if handler already registered.
 */
int scsi_register_device_handler(struct scsi_device_handler *scsi_dh)
{
	int ret = -EBUSY;
	struct scsi_device_handler *tmp;

	tmp = get_device_handler(scsi_dh->name);
	if (tmp)
		goto done;

	ret = bus_register_notifier(&scsi_bus_type, &scsi_dh->nb);

	bus_for_each_dev(&scsi_bus_type, NULL, scsi_dh, scsi_dh_notifier_add);
	spin_lock(&list_lock);
	list_add(&scsi_dh->list, &scsi_dh_list);
	spin_unlock(&list_lock);

done:
	return ret;
}
EXPORT_SYMBOL_GPL(scsi_register_device_handler);

static int scsi_dh_notifier_remove(struct device *dev, void *data)
{
	struct scsi_device_handler *scsi_dh = data;

	scsi_dh->nb.notifier_call(&scsi_dh->nb, BUS_NOTIFY_DEL_DEVICE, dev);
	return 0;
}

/*
 * scsi_unregister_device_handler - register a device handler personality
 *      module.
 * @scsi_dh - device handler to be unregistered.
 *
 * Returns 0 on success, -ENODEV if handler not registered.
 */
int scsi_unregister_device_handler(struct scsi_device_handler *scsi_dh)
{
	int ret = -ENODEV;
	struct scsi_device_handler *tmp;

	tmp = get_device_handler(scsi_dh->name);
	if (!tmp)
		goto done;

	ret = bus_unregister_notifier(&scsi_bus_type, &scsi_dh->nb);

	bus_for_each_dev(&scsi_bus_type, NULL, scsi_dh,
					scsi_dh_notifier_remove);
	spin_lock(&list_lock);
	list_del(&scsi_dh->list);
	spin_unlock(&list_lock);

done:
	return ret;
}
EXPORT_SYMBOL_GPL(scsi_unregister_device_handler);

/*
 * scsi_dh_activate - activate the path associated with the scsi_device
 *      corresponding to the given request queue.
 * @q - Request queue that is associated with the scsi_device to be
 *      activated.
 */
int scsi_dh_activate(struct request_queue *q)
{
	int err = 0;
	unsigned long flags;
	struct scsi_device *sdev;
	struct scsi_device_handler *scsi_dh = NULL;

	spin_lock_irqsave(q->queue_lock, flags);
	sdev = q->queuedata;
	if (sdev && sdev->scsi_dh_data)
		scsi_dh = sdev->scsi_dh_data->scsi_dh;
	if (!scsi_dh || !get_device(&sdev->sdev_gendev))
		err = SCSI_DH_NOSYS;
	spin_unlock_irqrestore(q->queue_lock, flags);

	if (err)
		return err;

	if (scsi_dh->activate)
		err = scsi_dh->activate(sdev);
	put_device(&sdev->sdev_gendev);
	return err;
}
EXPORT_SYMBOL_GPL(scsi_dh_activate);

/*
 * scsi_dh_handler_exist - Return TRUE(1) if a device handler exists for
 *	the given name. FALSE(0) otherwise.
 * @name - name of the device handler.
 */
int scsi_dh_handler_exist(const char *name)
{
	return (get_device_handler(name) != NULL);
}
EXPORT_SYMBOL_GPL(scsi_dh_handler_exist);

MODULE_DESCRIPTION("SCSI device handler");
MODULE_AUTHOR("Chandra Seetharaman <sekharan@us.ibm.com>");
MODULE_LICENSE("GPL");
