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

#include <linux/slab.h>
#include <linux/module.h>
#include <scsi/scsi_dh.h>
#include "../scsi_priv.h"

static DEFINE_SPINLOCK(list_lock);
static LIST_HEAD(scsi_dh_list);

static struct scsi_device_handler *get_device_handler(const char *name)
{
	struct scsi_device_handler *tmp, *found = NULL;

	spin_lock(&list_lock);
	list_for_each_entry(tmp, &scsi_dh_list, list) {
		if (!strncmp(tmp->name, name, strlen(tmp->name))) {
			found = tmp;
			break;
		}
	}
	spin_unlock(&list_lock);
	return found;
}

/*
 * device_handler_match_function - Match a device handler to a device
 * @sdev - SCSI device to be tested
 *
 * Tests @sdev against the match function of all registered device_handler.
 * Returns the found device handler or NULL if not found.
 */
static struct scsi_device_handler *
device_handler_match_function(struct scsi_device *sdev)
{
	struct scsi_device_handler *tmp_dh, *found_dh = NULL;

	spin_lock(&list_lock);
	list_for_each_entry(tmp_dh, &scsi_dh_list, list) {
		if (tmp_dh->match && tmp_dh->match(sdev)) {
			found_dh = tmp_dh;
			break;
		}
	}
	spin_unlock(&list_lock);
	return found_dh;
}

/*
 * device_handler_match - Attach a device handler to a device
 * @scsi_dh - The device handler to match against or NULL
 * @sdev - SCSI device to be tested against @scsi_dh
 *
 * Tests @sdev against the device handler @scsi_dh or against
 * all registered device_handler if @scsi_dh == NULL.
 * Returns the found device handler or NULL if not found.
 */
static struct scsi_device_handler *
device_handler_match(struct scsi_device_handler *scsi_dh,
		     struct scsi_device *sdev)
{
	struct scsi_device_handler *found_dh;

	found_dh = device_handler_match_function(sdev);

	if (scsi_dh && found_dh != scsi_dh)
		found_dh = NULL;

	return found_dh;
}

/*
 * scsi_dh_handler_attach - Attach a device handler to a device
 * @sdev - SCSI device the device handler should attach to
 * @scsi_dh - The device handler to attach
 */
static int scsi_dh_handler_attach(struct scsi_device *sdev,
				  struct scsi_device_handler *scsi_dh)
{
	int err = 0;

	if (sdev->scsi_dh_data) {
		if (sdev->scsi_dh_data->scsi_dh != scsi_dh)
			err = -EBUSY;
		else
			kref_get(&sdev->scsi_dh_data->kref);
	} else if (scsi_dh->attach) {
		err = scsi_dh->attach(sdev);
		if (!err) {
			kref_init(&sdev->scsi_dh_data->kref);
			sdev->scsi_dh_data->sdev = sdev;
		}
	}
	return err;
}

static void __detach_handler (struct kref *kref)
{
	struct scsi_dh_data *scsi_dh_data = container_of(kref, struct scsi_dh_data, kref);
	scsi_dh_data->scsi_dh->detach(scsi_dh_data->sdev);
}

/*
 * scsi_dh_handler_detach - Detach a device handler from a device
 * @sdev - SCSI device the device handler should be detached from
 * @scsi_dh - Device handler to be detached
 *
 * Detach from a device handler. If a device handler is specified,
 * only detach if the currently attached handler matches @scsi_dh.
 */
static void scsi_dh_handler_detach(struct scsi_device *sdev,
				   struct scsi_device_handler *scsi_dh)
{
	if (!sdev->scsi_dh_data)
		return;

	if (scsi_dh && scsi_dh != sdev->scsi_dh_data->scsi_dh)
		return;

	if (!scsi_dh)
		scsi_dh = sdev->scsi_dh_data->scsi_dh;

	if (scsi_dh && scsi_dh->detach)
		kref_put(&sdev->scsi_dh_data->kref, __detach_handler);
}

/*
 * Functions for sysfs attribute 'dh_state'
 */
static ssize_t
store_dh_state(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	struct scsi_device_handler *scsi_dh;
	int err = -EINVAL;

	if (sdev->sdev_state == SDEV_CANCEL ||
	    sdev->sdev_state == SDEV_DEL)
		return -ENODEV;

	if (!sdev->scsi_dh_data) {
		/*
		 * Attach to a device handler
		 */
		if (!(scsi_dh = get_device_handler(buf)))
			return err;
		err = scsi_dh_handler_attach(sdev, scsi_dh);
	} else {
		scsi_dh = sdev->scsi_dh_data->scsi_dh;
		if (!strncmp(buf, "detach", 6)) {
			/*
			 * Detach from a device handler
			 */
			scsi_dh_handler_detach(sdev, scsi_dh);
			err = 0;
		} else if (!strncmp(buf, "activate", 8)) {
			/*
			 * Activate a device handler
			 */
			if (scsi_dh->activate)
				err = scsi_dh->activate(sdev, NULL, NULL);
			else
				err = 0;
		}
	}

	return err<0?err:count;
}

static ssize_t
show_dh_state(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);

	if (!sdev->scsi_dh_data)
		return snprintf(buf, 20, "detached\n");

	return snprintf(buf, 20, "%s\n", sdev->scsi_dh_data->scsi_dh->name);
}

static struct device_attribute scsi_dh_state_attr =
	__ATTR(dh_state, S_IRUGO | S_IWUSR, show_dh_state,
	       store_dh_state);

/*
 * scsi_dh_sysfs_attr_add - Callback for scsi_init_dh
 */
static int scsi_dh_sysfs_attr_add(struct device *dev, void *data)
{
	struct scsi_device *sdev;
	int err;

	if (!scsi_is_sdev_device(dev))
		return 0;

	sdev = to_scsi_device(dev);

	err = device_create_file(&sdev->sdev_gendev,
				 &scsi_dh_state_attr);

	return 0;
}

/*
 * scsi_dh_sysfs_attr_remove - Callback for scsi_exit_dh
 */
static int scsi_dh_sysfs_attr_remove(struct device *dev, void *data)
{
	struct scsi_device *sdev;

	if (!scsi_is_sdev_device(dev))
		return 0;

	sdev = to_scsi_device(dev);

	device_remove_file(&sdev->sdev_gendev,
			   &scsi_dh_state_attr);

	return 0;
}

/*
 * scsi_dh_notifier - notifier chain callback
 */
static int scsi_dh_notifier(struct notifier_block *nb,
			    unsigned long action, void *data)
{
	struct device *dev = data;
	struct scsi_device *sdev;
	int err = 0;
	struct scsi_device_handler *devinfo = NULL;

	if (!scsi_is_sdev_device(dev))
		return 0;

	sdev = to_scsi_device(dev);

	if (action == BUS_NOTIFY_ADD_DEVICE) {
		err = device_create_file(dev, &scsi_dh_state_attr);
		/* don't care about err */
		devinfo = device_handler_match(NULL, sdev);
		if (devinfo)
			err = scsi_dh_handler_attach(sdev, devinfo);
	} else if (action == BUS_NOTIFY_DEL_DEVICE) {
		device_remove_file(dev, &scsi_dh_state_attr);
		scsi_dh_handler_detach(sdev, NULL);
	}
	return err;
}

/*
 * scsi_dh_notifier_add - Callback for scsi_register_device_handler
 */
static int scsi_dh_notifier_add(struct device *dev, void *data)
{
	struct scsi_device_handler *scsi_dh = data;
	struct scsi_device *sdev;

	if (!scsi_is_sdev_device(dev))
		return 0;

	if (!get_device(dev))
		return 0;

	sdev = to_scsi_device(dev);

	if (device_handler_match(scsi_dh, sdev))
		scsi_dh_handler_attach(sdev, scsi_dh);

	put_device(dev);

	return 0;
}

/*
 * scsi_dh_notifier_remove - Callback for scsi_unregister_device_handler
 */
static int scsi_dh_notifier_remove(struct device *dev, void *data)
{
	struct scsi_device_handler *scsi_dh = data;
	struct scsi_device *sdev;

	if (!scsi_is_sdev_device(dev))
		return 0;

	if (!get_device(dev))
		return 0;

	sdev = to_scsi_device(dev);

	scsi_dh_handler_detach(sdev, scsi_dh);

	put_device(dev);

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

	if (get_device_handler(scsi_dh->name))
		return -EBUSY;

	spin_lock(&list_lock);
	list_add(&scsi_dh->list, &scsi_dh_list);
	spin_unlock(&list_lock);

	bus_for_each_dev(&scsi_bus_type, NULL, scsi_dh, scsi_dh_notifier_add);
	printk(KERN_INFO "%s: device handler registered\n", scsi_dh->name);

	return SCSI_DH_OK;
}
EXPORT_SYMBOL_GPL(scsi_register_device_handler);

/*
 * scsi_unregister_device_handler - register a device handler personality
 *      module.
 * @scsi_dh - device handler to be unregistered.
 *
 * Returns 0 on success, -ENODEV if handler not registered.
 */
int scsi_unregister_device_handler(struct scsi_device_handler *scsi_dh)
{

	if (!get_device_handler(scsi_dh->name))
		return -ENODEV;

	bus_for_each_dev(&scsi_bus_type, NULL, scsi_dh,
			 scsi_dh_notifier_remove);

	spin_lock(&list_lock);
	list_del(&scsi_dh->list);
	spin_unlock(&list_lock);
	printk(KERN_INFO "%s: device handler unregistered\n", scsi_dh->name);

	return SCSI_DH_OK;
}
EXPORT_SYMBOL_GPL(scsi_unregister_device_handler);

/*
 * scsi_dh_activate - activate the path associated with the scsi_device
 *      corresponding to the given request queue.
 *     Returns immediately without waiting for activation to be completed.
 * @q    - Request queue that is associated with the scsi_device to be
 *         activated.
 * @fn   - Function to be called upon completion of the activation.
 *         Function fn is called with data (below) and the error code.
 *         Function fn may be called from the same calling context. So,
 *         do not hold the lock in the caller which may be needed in fn.
 * @data - data passed to the function fn upon completion.
 *
 */
int scsi_dh_activate(struct request_queue *q, activate_complete fn, void *data)
{
	int err = 0;
	unsigned long flags;
	struct scsi_device *sdev;
	struct scsi_device_handler *scsi_dh = NULL;
	struct device *dev = NULL;

	spin_lock_irqsave(q->queue_lock, flags);
	sdev = q->queuedata;
	if (!sdev) {
		spin_unlock_irqrestore(q->queue_lock, flags);
		err = SCSI_DH_NOSYS;
		if (fn)
			fn(data, err);
		return err;
	}

	if (sdev->scsi_dh_data)
		scsi_dh = sdev->scsi_dh_data->scsi_dh;
	dev = get_device(&sdev->sdev_gendev);
	if (!scsi_dh || !dev ||
	    sdev->sdev_state == SDEV_CANCEL ||
	    sdev->sdev_state == SDEV_DEL)
		err = SCSI_DH_NOSYS;
	if (sdev->sdev_state == SDEV_OFFLINE)
		err = SCSI_DH_DEV_OFFLINED;
	spin_unlock_irqrestore(q->queue_lock, flags);

	if (err) {
		if (fn)
			fn(data, err);
		goto out;
	}

	if (scsi_dh->activate)
		err = scsi_dh->activate(sdev, fn, data);
out:
	put_device(dev);
	return err;
}
EXPORT_SYMBOL_GPL(scsi_dh_activate);

/*
 * scsi_dh_set_params - set the parameters for the device as per the
 *      string specified in params.
 * @q - Request queue that is associated with the scsi_device for
 *      which the parameters to be set.
 * @params - parameters in the following format
 *      "no_of_params\0param1\0param2\0param3\0...\0"
 *      for example, string for 2 parameters with value 10 and 21
 *      is specified as "2\010\021\0".
 */
int scsi_dh_set_params(struct request_queue *q, const char *params)
{
	int err = -SCSI_DH_NOSYS;
	unsigned long flags;
	struct scsi_device *sdev;
	struct scsi_device_handler *scsi_dh = NULL;

	spin_lock_irqsave(q->queue_lock, flags);
	sdev = q->queuedata;
	if (sdev && sdev->scsi_dh_data)
		scsi_dh = sdev->scsi_dh_data->scsi_dh;
	if (scsi_dh && scsi_dh->set_params && get_device(&sdev->sdev_gendev))
		err = 0;
	spin_unlock_irqrestore(q->queue_lock, flags);

	if (err)
		return err;
	err = scsi_dh->set_params(sdev, params);
	put_device(&sdev->sdev_gendev);
	return err;
}
EXPORT_SYMBOL_GPL(scsi_dh_set_params);

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

/*
 * scsi_dh_attach - Attach device handler
 * @q - Request queue that is associated with the scsi_device
 *      the handler should be attached to
 * @name - name of the handler to attach
 */
int scsi_dh_attach(struct request_queue *q, const char *name)
{
	unsigned long flags;
	struct scsi_device *sdev;
	struct scsi_device_handler *scsi_dh;
	int err = 0;

	scsi_dh = get_device_handler(name);
	if (!scsi_dh)
		return -EINVAL;

	spin_lock_irqsave(q->queue_lock, flags);
	sdev = q->queuedata;
	if (!sdev || !get_device(&sdev->sdev_gendev))
		err = -ENODEV;
	spin_unlock_irqrestore(q->queue_lock, flags);

	if (!err) {
		err = scsi_dh_handler_attach(sdev, scsi_dh);
		put_device(&sdev->sdev_gendev);
	}
	return err;
}
EXPORT_SYMBOL_GPL(scsi_dh_attach);

/*
 * scsi_dh_detach - Detach device handler
 * @q - Request queue that is associated with the scsi_device
 *      the handler should be detached from
 *
 * This function will detach the device handler only
 * if the sdev is not part of the internal list, ie
 * if it has been attached manually.
 */
void scsi_dh_detach(struct request_queue *q)
{
	unsigned long flags;
	struct scsi_device *sdev;
	struct scsi_device_handler *scsi_dh = NULL;

	spin_lock_irqsave(q->queue_lock, flags);
	sdev = q->queuedata;
	if (!sdev || !get_device(&sdev->sdev_gendev))
		sdev = NULL;
	spin_unlock_irqrestore(q->queue_lock, flags);

	if (!sdev)
		return;

	if (sdev->scsi_dh_data) {
		scsi_dh = sdev->scsi_dh_data->scsi_dh;
		scsi_dh_handler_detach(sdev, scsi_dh);
	}
	put_device(&sdev->sdev_gendev);
}
EXPORT_SYMBOL_GPL(scsi_dh_detach);

/*
 * scsi_dh_attached_handler_name - Get attached device handler's name
 * @q - Request queue that is associated with the scsi_device
 *      that may have a device handler attached
 * @gfp - the GFP mask used in the kmalloc() call when allocating memory
 *
 * Returns name of attached handler, NULL if no handler is attached.
 * Caller must take care to free the returned string.
 */
const char *scsi_dh_attached_handler_name(struct request_queue *q, gfp_t gfp)
{
	unsigned long flags;
	struct scsi_device *sdev;
	const char *handler_name = NULL;

	spin_lock_irqsave(q->queue_lock, flags);
	sdev = q->queuedata;
	if (!sdev || !get_device(&sdev->sdev_gendev))
		sdev = NULL;
	spin_unlock_irqrestore(q->queue_lock, flags);

	if (!sdev)
		return NULL;

	if (sdev->scsi_dh_data)
		handler_name = kstrdup(sdev->scsi_dh_data->scsi_dh->name, gfp);

	put_device(&sdev->sdev_gendev);
	return handler_name;
}
EXPORT_SYMBOL_GPL(scsi_dh_attached_handler_name);

static struct notifier_block scsi_dh_nb = {
	.notifier_call = scsi_dh_notifier
};

static int __init scsi_dh_init(void)
{
	int r;

	r = bus_register_notifier(&scsi_bus_type, &scsi_dh_nb);

	if (!r)
		bus_for_each_dev(&scsi_bus_type, NULL, NULL,
				 scsi_dh_sysfs_attr_add);

	return r;
}

static void __exit scsi_dh_exit(void)
{
	bus_for_each_dev(&scsi_bus_type, NULL, NULL,
			 scsi_dh_sysfs_attr_remove);
	bus_unregister_notifier(&scsi_bus_type, &scsi_dh_nb);
}

module_init(scsi_dh_init);
module_exit(scsi_dh_exit);

MODULE_DESCRIPTION("SCSI device handler");
MODULE_AUTHOR("Chandra Seetharaman <sekharan@us.ibm.com>");
MODULE_LICENSE("GPL");
