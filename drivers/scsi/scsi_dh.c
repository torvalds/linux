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
#include "scsi_priv.h"

static DEFINE_SPINLOCK(list_lock);
static LIST_HEAD(scsi_dh_list);

struct scsi_dh_blist {
	const char *vendor;
	const char *model;
	const char *driver;
};

static const struct scsi_dh_blist scsi_dh_blist[] = {
	{"DGC", "RAID",			"emc" },
	{"DGC", "DISK",			"emc" },
	{"DGC", "VRAID",		"emc" },

	{"COMPAQ", "MSA1000 VOLUME",	"hp_sw" },
	{"COMPAQ", "HSV110",		"hp_sw" },
	{"HP", "HSV100",		"hp_sw"},
	{"DEC", "HSG80",		"hp_sw"},

	{"IBM", "1722",			"rdac", },
	{"IBM", "1724",			"rdac", },
	{"IBM", "1726",			"rdac", },
	{"IBM", "1742",			"rdac", },
	{"IBM", "1745",			"rdac", },
	{"IBM", "1746",			"rdac", },
	{"IBM", "1813",			"rdac", },
	{"IBM", "1814",			"rdac", },
	{"IBM", "1815",			"rdac", },
	{"IBM", "1818",			"rdac", },
	{"IBM", "3526",			"rdac", },
	{"SGI", "TP9",			"rdac", },
	{"SGI", "IS",			"rdac", },
	{"STK", "OPENstorage D280",	"rdac", },
	{"STK", "FLEXLINE 380",		"rdac", },
	{"SUN", "CSM",			"rdac", },
	{"SUN", "LCSM100",		"rdac", },
	{"SUN", "STK6580_6780",		"rdac", },
	{"SUN", "SUN_6180",		"rdac", },
	{"SUN", "ArrayStorage",		"rdac", },
	{"DELL", "MD3",			"rdac", },
	{"NETAPP", "INF-01-00",		"rdac", },
	{"LSI", "INF-01-00",		"rdac", },
	{"ENGENIO", "INF-01-00",	"rdac", },
	{NULL, NULL,			NULL },
};

static const char *
scsi_dh_find_driver(struct scsi_device *sdev)
{
	const struct scsi_dh_blist *b;

	if (scsi_device_tpgs(sdev))
		return "alua";

	for (b = scsi_dh_blist; b->vendor; b++) {
		if (!strncmp(sdev->vendor, b->vendor, strlen(b->vendor)) &&
		    !strncmp(sdev->model, b->model, strlen(b->model))) {
			return b->driver;
		}
	}
	return NULL;
}


static struct scsi_device_handler *__scsi_dh_lookup(const char *name)
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

static struct scsi_device_handler *scsi_dh_lookup(const char *name)
{
	struct scsi_device_handler *dh;

	dh = __scsi_dh_lookup(name);
	if (!dh) {
		request_module("scsi_dh_%s", name);
		dh = __scsi_dh_lookup(name);
	}

	return dh;
}

/*
 * scsi_dh_handler_attach - Attach a device handler to a device
 * @sdev - SCSI device the device handler should attach to
 * @scsi_dh - The device handler to attach
 */
static int scsi_dh_handler_attach(struct scsi_device *sdev,
				  struct scsi_device_handler *scsi_dh)
{
	int error, ret = 0;

	if (!try_module_get(scsi_dh->module))
		return -EINVAL;

	error = scsi_dh->attach(sdev);
	if (error != SCSI_DH_OK) {
		switch (error) {
		case SCSI_DH_NOMEM:
			ret = -ENOMEM;
			break;
		case SCSI_DH_RES_TEMP_UNAVAIL:
			ret = -EAGAIN;
			break;
		case SCSI_DH_DEV_UNSUPP:
		case SCSI_DH_NOSYS:
			ret = -ENODEV;
			break;
		default:
			ret = -EINVAL;
			break;
		}
		if (ret != -ENODEV)
			sdev_printk(KERN_ERR, sdev, "%s: Attach failed (%d)\n",
				    scsi_dh->name, error);
		module_put(scsi_dh->module);
	} else
		sdev->handler = scsi_dh;

	return ret;
}

/*
 * scsi_dh_handler_detach - Detach a device handler from a device
 * @sdev - SCSI device the device handler should be detached from
 */
static void scsi_dh_handler_detach(struct scsi_device *sdev)
{
	sdev->handler->detach(sdev);
	sdev_printk(KERN_NOTICE, sdev, "%s: Detached\n", sdev->handler->name);
	module_put(sdev->handler->module);
}

void scsi_dh_add_device(struct scsi_device *sdev)
{
	struct scsi_device_handler *devinfo = NULL;
	const char *drv;

	drv = scsi_dh_find_driver(sdev);
	if (drv)
		devinfo = __scsi_dh_lookup(drv);
	/*
	 * device_handler is optional, so ignore errors
	 * from scsi_dh_handler_attach()
	 */
	if (devinfo)
		(void)scsi_dh_handler_attach(sdev, devinfo);
}

void scsi_dh_release_device(struct scsi_device *sdev)
{
	if (sdev->handler)
		scsi_dh_handler_detach(sdev);
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
	if (__scsi_dh_lookup(scsi_dh->name))
		return -EBUSY;

	if (!scsi_dh->attach || !scsi_dh->detach)
		return -EINVAL;

	spin_lock(&list_lock);
	list_add(&scsi_dh->list, &scsi_dh_list);
	spin_unlock(&list_lock);

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
	if (!__scsi_dh_lookup(scsi_dh->name))
		return -ENODEV;

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
	struct scsi_device *sdev;
	int err = SCSI_DH_NOSYS;

	sdev = scsi_device_from_queue(q);
	if (!sdev) {
		if (fn)
			fn(data, err);
		return err;
	}

	if (!sdev->handler)
		goto out_fn;
	err = SCSI_DH_NOTCONN;
	if (sdev->sdev_state == SDEV_CANCEL ||
	    sdev->sdev_state == SDEV_DEL)
		goto out_fn;

	err = SCSI_DH_DEV_OFFLINED;
	if (sdev->sdev_state == SDEV_OFFLINE)
		goto out_fn;

	if (sdev->handler->activate)
		err = sdev->handler->activate(sdev, fn, data);

out_put_device:
	put_device(&sdev->sdev_gendev);
	return err;

out_fn:
	if (fn)
		fn(data, err);
	goto out_put_device;
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
	struct scsi_device *sdev;
	int err = -SCSI_DH_NOSYS;

	sdev = scsi_device_from_queue(q);
	if (!sdev)
		return err;

	if (sdev->handler && sdev->handler->set_params)
		err = sdev->handler->set_params(sdev, params);
	put_device(&sdev->sdev_gendev);
	return err;
}
EXPORT_SYMBOL_GPL(scsi_dh_set_params);

/*
 * scsi_dh_attach - Attach device handler
 * @q - Request queue that is associated with the scsi_device
 *      the handler should be attached to
 * @name - name of the handler to attach
 */
int scsi_dh_attach(struct request_queue *q, const char *name)
{
	struct scsi_device *sdev;
	struct scsi_device_handler *scsi_dh;
	int err = 0;

	sdev = scsi_device_from_queue(q);
	if (!sdev)
		return -ENODEV;

	scsi_dh = scsi_dh_lookup(name);
	if (!scsi_dh) {
		err = -EINVAL;
		goto out_put_device;
	}

	if (sdev->handler) {
		if (sdev->handler != scsi_dh)
			err = -EBUSY;
		goto out_put_device;
	}

	err = scsi_dh_handler_attach(sdev, scsi_dh);

out_put_device:
	put_device(&sdev->sdev_gendev);
	return err;
}
EXPORT_SYMBOL_GPL(scsi_dh_attach);

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
	struct scsi_device *sdev;
	const char *handler_name = NULL;

	sdev = scsi_device_from_queue(q);
	if (!sdev)
		return NULL;

	if (sdev->handler)
		handler_name = kstrdup(sdev->handler->name, gfp);
	put_device(&sdev->sdev_gendev);
	return handler_name;
}
EXPORT_SYMBOL_GPL(scsi_dh_attached_handler_name);
