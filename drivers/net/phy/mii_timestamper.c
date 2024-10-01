// SPDX-License-Identifier: GPL-2.0
//
// Support for generic time stamping devices on MII buses.
// Copyright (C) 2018 Richard Cochran <richardcochran@gmail.com>
//

#include <linux/mii_timestamper.h>

static LIST_HEAD(mii_timestamping_devices);
static DEFINE_MUTEX(tstamping_devices_lock);

struct mii_timestamping_desc {
	struct list_head list;
	struct mii_timestamping_ctrl *ctrl;
	struct device *device;
};

/**
 * register_mii_tstamp_controller() - registers an MII time stamping device.
 *
 * @device:	The device to be registered.
 * @ctrl:	Pointer to device's control interface.
 *
 * Returns zero on success or non-zero on failure.
 */
int register_mii_tstamp_controller(struct device *device,
				   struct mii_timestamping_ctrl *ctrl)
{
	struct mii_timestamping_desc *desc;

	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	INIT_LIST_HEAD(&desc->list);
	desc->ctrl = ctrl;
	desc->device = device;

	mutex_lock(&tstamping_devices_lock);
	list_add_tail(&mii_timestamping_devices, &desc->list);
	mutex_unlock(&tstamping_devices_lock);

	return 0;
}
EXPORT_SYMBOL(register_mii_tstamp_controller);

/**
 * unregister_mii_tstamp_controller() - unregisters an MII time stamping device.
 *
 * @device:	A device previously passed to register_mii_tstamp_controller().
 */
void unregister_mii_tstamp_controller(struct device *device)
{
	struct mii_timestamping_desc *desc;
	struct list_head *this, *next;

	mutex_lock(&tstamping_devices_lock);
	list_for_each_safe(this, next, &mii_timestamping_devices) {
		desc = list_entry(this, struct mii_timestamping_desc, list);
		if (desc->device == device) {
			list_del_init(&desc->list);
			kfree(desc);
			break;
		}
	}
	mutex_unlock(&tstamping_devices_lock);
}
EXPORT_SYMBOL(unregister_mii_tstamp_controller);

/**
 * register_mii_timestamper - Enables a given port of an MII time stamper.
 *
 * @node:	The device tree node of the MII time stamp controller.
 * @port:	The index of the port to be enabled.
 *
 * Returns a valid interface on success or ERR_PTR otherwise.
 */
struct mii_timestamper *register_mii_timestamper(struct device_node *node,
						 unsigned int port)
{
	struct mii_timestamper *mii_ts = NULL;
	struct mii_timestamping_desc *desc;
	struct list_head *this;

	mutex_lock(&tstamping_devices_lock);
	list_for_each(this, &mii_timestamping_devices) {
		desc = list_entry(this, struct mii_timestamping_desc, list);
		if (desc->device->of_node == node) {
			mii_ts = desc->ctrl->probe_channel(desc->device, port);
			if (!IS_ERR(mii_ts)) {
				mii_ts->device = desc->device;
				get_device(desc->device);
			}
			break;
		}
	}
	mutex_unlock(&tstamping_devices_lock);

	return mii_ts ? mii_ts : ERR_PTR(-EPROBE_DEFER);
}
EXPORT_SYMBOL(register_mii_timestamper);

/**
 * unregister_mii_timestamper - Disables a given MII time stamper.
 *
 * @mii_ts:	An interface obtained via register_mii_timestamper().
 *
 */
void unregister_mii_timestamper(struct mii_timestamper *mii_ts)
{
	struct mii_timestamping_desc *desc;
	struct list_head *this;

	if (!mii_ts)
		return;

	/* mii_timestamper statically registered by the PHY driver won't use the
	 * register_mii_timestamper() and thus don't have ->device set. Don't
	 * try to unregister these.
	 */
	if (!mii_ts->device)
		return;

	mutex_lock(&tstamping_devices_lock);
	list_for_each(this, &mii_timestamping_devices) {
		desc = list_entry(this, struct mii_timestamping_desc, list);
		if (desc->device == mii_ts->device) {
			desc->ctrl->release_channel(desc->device, mii_ts);
			put_device(desc->device);
			break;
		}
	}
	mutex_unlock(&tstamping_devices_lock);
}
EXPORT_SYMBOL(unregister_mii_timestamper);
