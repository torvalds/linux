/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Copyright (C) 2021 XiaoMi, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/notifier.h>
#include <drm/drm_notifier.h>

static BLOCKING_NOTIFIER_HEAD(mi_drm_notifier_list);

/**
 * mi_drm_register_client - register a client notifier
 * @nb: notifier block to callback on events
 *
 * This function registers a notifier callback function
 * to msm_drm_notifier_list, which would be called when
 * received unblank/power down event.
 */
int mi_drm_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&mi_drm_notifier_list, nb);
}
EXPORT_SYMBOL(mi_drm_register_client);

/**
 * mi_drm_unregister_client - unregister a client notifier
 * @nb: notifier block to callback on events
 *
 * This function unregisters the callback function from
 * msm_drm_notifier_list.
 */
int mi_drm_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&mi_drm_notifier_list, nb);
}
EXPORT_SYMBOL(mi_drm_unregister_client);

/**
 * mi_drm_notifier_call_chain - notify clients of drm_events
 * @val: event MSM_DRM_EARLY_EVENT_BLANK or MSM_DRM_EVENT_BLANK
 * @v: notifier data, inculde display id and display blank
 *     event(unblank or power down).
 */
int mi_drm_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&mi_drm_notifier_list, val, v);
}
EXPORT_SYMBOL(mi_drm_notifier_call_chain);
