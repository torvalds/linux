// SPDX-License-Identifier: GPL-2.0 or MIT

#include <linux/sysrq.h>

#include <drm/drm_client_event.h>
#include <drm/drm_device.h>
#include <drm/drm_print.h>

#include "drm_internal.h"

#ifdef CONFIG_MAGIC_SYSRQ
static LIST_HEAD(drm_client_sysrq_dev_list);
static DEFINE_MUTEX(drm_client_sysrq_dev_lock);

/* emergency restore, don't bother with error reporting */
static void drm_client_sysrq_restore_work_fn(struct work_struct *ignored)
{
	struct drm_device *dev;

	guard(mutex)(&drm_client_sysrq_dev_lock);

	list_for_each_entry(dev, &drm_client_sysrq_dev_list, client_sysrq_list) {
		if (dev->switch_power_state == DRM_SWITCH_POWER_OFF)
			continue;

		drm_client_dev_restore(dev, true);
	}
}

static DECLARE_WORK(drm_client_sysrq_restore_work, drm_client_sysrq_restore_work_fn);

static void drm_client_sysrq_restore_handler(u8 ignored)
{
	schedule_work(&drm_client_sysrq_restore_work);
}

static const struct sysrq_key_op drm_client_sysrq_restore_op = {
	.handler = drm_client_sysrq_restore_handler,
	.help_msg = "force-fb(v)",
	.action_msg = "Restore framebuffer console",
};

void drm_client_sysrq_register(struct drm_device *dev)
{
	guard(mutex)(&drm_client_sysrq_dev_lock);

	if (list_empty(&drm_client_sysrq_dev_list))
		register_sysrq_key('v', &drm_client_sysrq_restore_op);

	list_add(&dev->client_sysrq_list, &drm_client_sysrq_dev_list);
}

void drm_client_sysrq_unregister(struct drm_device *dev)
{
	guard(mutex)(&drm_client_sysrq_dev_lock);

	/* remove device from global restore list */
	if (!drm_WARN_ON(dev, list_empty(&dev->client_sysrq_list)))
		list_del(&dev->client_sysrq_list);

	/* no devices left; unregister key */
	if (list_empty(&drm_client_sysrq_dev_list))
		unregister_sysrq_key('v', &drm_client_sysrq_restore_op);
}
#endif
