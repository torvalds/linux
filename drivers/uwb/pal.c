/*
 * UWB PAL support.
 *
 * Copyright (C) 2008 Cambridge Silicon Radio Ltd.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/uwb.h>

#include "uwb-internal.h"

/**
 * uwb_pal_init - initialize a UWB PAL
 * @pal: the PAL to initialize
 */
void uwb_pal_init(struct uwb_pal *pal)
{
	INIT_LIST_HEAD(&pal->node);
}
EXPORT_SYMBOL_GPL(uwb_pal_init);

/**
 * uwb_pal_register - register a UWB PAL
 * @rc: the radio controller the PAL will be using
 * @pal: the PAL
 *
 * The PAL must be initialized with uwb_pal_init().
 */
int uwb_pal_register(struct uwb_rc *rc, struct uwb_pal *pal)
{
	int ret;

	if (pal->device) {
		ret = sysfs_create_link(&pal->device->kobj,
					&rc->uwb_dev.dev.kobj, "uwb_rc");
		if (ret < 0)
			return ret;
		ret = sysfs_create_link(&rc->uwb_dev.dev.kobj,
					&pal->device->kobj, pal->name);
		if (ret < 0) {
			sysfs_remove_link(&pal->device->kobj, "uwb_rc");
			return ret;
		}
	}

	spin_lock(&rc->pal_lock);
	list_add(&pal->node, &rc->pals);
	spin_unlock(&rc->pal_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(uwb_pal_register);

/**
 * uwb_pal_register - unregister a UWB PAL
 * @rc: the radio controller the PAL was using
 * @pal: the PAL
 */
void uwb_pal_unregister(struct uwb_rc *rc, struct uwb_pal *pal)
{
	spin_lock(&rc->pal_lock);
	list_del(&pal->node);
	spin_unlock(&rc->pal_lock);

	if (pal->device) {
		sysfs_remove_link(&rc->uwb_dev.dev.kobj, pal->name);
		sysfs_remove_link(&pal->device->kobj, "uwb_rc");
	}
}
EXPORT_SYMBOL_GPL(uwb_pal_unregister);

/**
 * uwb_rc_pal_init - initialize the PAL related parts of a radio controller
 * @rc: the radio controller
 */
void uwb_rc_pal_init(struct uwb_rc *rc)
{
	spin_lock_init(&rc->pal_lock);
	INIT_LIST_HEAD(&rc->pals);
}
