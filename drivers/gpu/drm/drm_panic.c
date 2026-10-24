// SPDX-License-Identifier: GPL-2.0 or MIT
/*
 * Copyright (c) 2023 Red Hat.
 * Author: Jocelyn Falempe <jfalempe@redhat.com>
 * inspired by the drm_log driver from David Herrmann <dh.herrmann@gmail.com>
 * Tux Ascii art taken from cowsay written by Tony Monroe
 */

#include <linux/export.h>
#include <linux/init.h>
#include <linux/kdebug.h>
#include <linux/kmsg_dump.h>
#include <linux/list.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/printk.h>
#include <linux/types.h>

#include <drm/drm_drv.h>
#include <drm/drm_panic.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>

#include "drm_crtc_internal.h"
#include "drm_panic_internal.h"

MODULE_AUTHOR("Jocelyn Falempe");
MODULE_DESCRIPTION("DRM panic handler");
MODULE_LICENSE("GPL");

/**
 * DOC: overview
 *
 * This module displays a user friendly message on screen when a kernel
 * panic occurs. It's intended for end users and therefore have minimal
 * technical/debug information.
 *
 * To enable DRM panic for a driver, the at least one primary plane must
 * implement struct &drm_plane_funcs.display_panic_screen. The plane is
 * then automatically registered to the drm panic handler.
 *
 * When a panic occurs, the DRM panic handler calls struct
 * &drm_plane_funcs.display_panic_screen. See
 * drm_plane_helper_display_panic_screen() for a generic implementation.
 */

#if defined(CONFIG_DRM_PANIC_SCREEN_QR_CODE)
static uint panic_qr_version = CONFIG_DRM_PANIC_SCREEN_QR_VERSION;
module_param(panic_qr_version, uint, 0644);
MODULE_PARM_DESC(panic_qr_version, "maximum version (size) of the QR code");
#endif

static enum drm_panic_type drm_panic_type = -1;

static const char *drm_panic_type_map[] = {
	[DRM_PANIC_TYPE_KMSG] = "kmsg",
	[DRM_PANIC_TYPE_USER] = "user",
#if IS_ENABLED(CONFIG_DRM_PANIC_SCREEN_QR_CODE)
	[DRM_PANIC_TYPE_QR] = "qr_code",
#endif
};

static int drm_panic_type_set(const char *val, const struct kernel_param *kp)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(drm_panic_type_map); i++) {
		if (!strcmp(val, drm_panic_type_map[i])) {
			drm_panic_type = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int drm_panic_type_get(char *buffer, const struct kernel_param *kp)
{
	return scnprintf(buffer, PAGE_SIZE, "%s\n",
			 drm_panic_type_map[drm_panic_type]);
}

static const struct kernel_param_ops drm_panic_ops = {
	.set = drm_panic_type_set,
	.get = drm_panic_type_get,
};

module_param_cb(panic_screen, &drm_panic_ops, NULL, 0644);
MODULE_PARM_DESC(panic_screen,
#if IS_ENABLED(CONFIG_DRM_PANIC_SCREEN_QR_CODE)
		 "Choose what will be displayed by drm_panic, 'user', 'kmsg' or 'qr_code' [default="
#else
		 "Choose what will be displayed by drm_panic, 'user' or 'kmsg' [default="
#endif
		 CONFIG_DRM_PANIC_SCREEN "]");

#define drm_panic_trylock(dev, flags) \
	raw_spin_trylock_irqsave(&(dev)->mode_config.panic_lock, flags)

static void drm_panic_display_panic_screen(struct drm_plane *plane, const char *description)
{
#if defined(CONFIG_DRM_PANIC_FOREGROUND_COLOR)
	u32 fg_color = CONFIG_DRM_PANIC_FOREGROUND_COLOR;
#else
	u32 fg_color = 0x00ffffff;
#endif
#if defined(CONFIG_DRM_PANIC_BACKGROUND_COLOR)
	u32 bg_color = CONFIG_DRM_PANIC_BACKGROUND_COLOR;
#else
	u32 bg_color = 0x00000000;
#endif
#if IS_ENABLED(CONFIG_DRM_PANIC_SCREEN_QR_CODE)
	unsigned int qr_version = panic_qr_version;
#else
	unsigned int qr_version = 0;
#endif
	struct drm_device *dev = plane->dev;
	unsigned long flags;

	if (drm_panic_trylock(dev, flags)) {
		plane->funcs->display_panic_screen(plane, description, drm_panic_type,
						   fg_color, bg_color, qr_version);
		drm_panic_unlock(dev, flags);
	}
}

static struct drm_plane *to_drm_plane(struct kmsg_dumper *kd)
{
	return container_of(kd, struct drm_plane, kmsg_panic);
}

static void drm_panic(struct kmsg_dumper *dumper, struct kmsg_dump_detail *detail)
{
	struct drm_plane *plane = to_drm_plane(dumper);

	if (detail->reason == KMSG_DUMP_PANIC)
		drm_panic_display_panic_screen(plane, detail->description);
}

/*
 * DEBUG FS, This is currently unsafe.
 * Create one file per plane, so it's possible to debug one plane at a time.
 * TODO: It would be better to emulate an NMI context.
 */
#ifdef CONFIG_DRM_PANIC_DEBUG
#include <linux/debugfs.h>

static ssize_t debugfs_trigger_write(struct file *file, const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	bool run;

	if (kstrtobool_from_user(user_buf, count, &run) == 0 && run) {
		struct drm_plane *plane = file->private_data;

		drm_panic_display_panic_screen(plane, "Test from debugfs");
	}
	return count;
}

static const struct file_operations dbg_drm_panic_ops = {
	.owner = THIS_MODULE,
	.write = debugfs_trigger_write,
	.open = simple_open,
};

static void debugfs_register_plane(struct drm_plane *plane, int index)
{
	char fname[32];

	snprintf(fname, 32, "drm_panic_plane_%d", index);
	debugfs_create_file(fname, 0200, plane->dev->debugfs_root,
			    plane, &dbg_drm_panic_ops);
}
#else
static void debugfs_register_plane(struct drm_plane *plane, int index) {}
#endif /* CONFIG_DRM_PANIC_DEBUG */

/**
 * drm_panic_is_enabled
 * @dev: the drm device that may supports drm_panic
 *
 * returns true if the drm device supports drm_panic
 */
bool drm_panic_is_enabled(struct drm_device *dev)
{
	struct drm_plane *plane;

	if (!dev->mode_config.num_total_plane)
		return false;

	drm_for_each_plane(plane, dev) {
		if (plane->type != DRM_PLANE_TYPE_PRIMARY)
			continue;
		if (!plane->funcs || !plane->funcs->display_panic_screen)
			continue;
		return true;
	}
	return false;
}
EXPORT_SYMBOL(drm_panic_is_enabled);

/**
 * drm_panic_register() - Initialize DRM panic for a device
 * @dev: the drm device on which the panic screen will be displayed.
 */
void drm_panic_register(struct drm_device *dev)
{
	struct drm_plane *plane;
	int registered_plane = 0;

	if (!dev->mode_config.num_total_plane)
		return;

	drm_for_each_plane(plane, dev) {
		if (plane->type != DRM_PLANE_TYPE_PRIMARY)
			continue;
		if (!plane->funcs || !plane->funcs->display_panic_screen)
			continue;
		plane->kmsg_panic.dump = drm_panic;
		plane->kmsg_panic.max_reason = KMSG_DUMP_PANIC;
		if (kmsg_dump_register(&plane->kmsg_panic))
			drm_warn(dev, "Failed to register panic handler\n");
		else {
			debugfs_register_plane(plane, registered_plane);
			registered_plane++;
		}
	}
	if (registered_plane)
		drm_info(dev, "Registered %d planes with drm panic\n", registered_plane);
}

/**
 * drm_panic_unregister()
 * @dev: the drm device previously registered.
 */
void drm_panic_unregister(struct drm_device *dev)
{
	struct drm_plane *plane;

	if (!dev->mode_config.num_total_plane)
		return;

	drm_for_each_plane(plane, dev) {
		if (plane->type != DRM_PLANE_TYPE_PRIMARY)
			continue;
		if (!plane->funcs || !plane->funcs->display_panic_screen)
			continue;
		kmsg_dump_unregister(&plane->kmsg_panic);
	}
}

/**
 * drm_panic_init() - initialize DRM panic.
 */
void __init drm_panic_init(void)
{
	if (drm_panic_type == -1 && drm_panic_type_set(CONFIG_DRM_PANIC_SCREEN, NULL)) {
		pr_warn("Unsupported value for CONFIG_DRM_PANIC_SCREEN ('%s'), falling back to 'user'...\n",
			CONFIG_DRM_PANIC_SCREEN);
		drm_panic_type = DRM_PANIC_TYPE_USER;
	}
}
