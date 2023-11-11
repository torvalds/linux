// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 */

#define DSS_SUBSYS_NAME "DISPLAY"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>

#include <video/omapfb_dss.h>
#include "dss.h"

static ssize_t display_name_show(struct omap_dss_device *dssdev, char *buf)
{
	return sysfs_emit(buf, "%s\n",
			dssdev->name ?
			dssdev->name : "");
}

static ssize_t display_enabled_show(struct omap_dss_device *dssdev, char *buf)
{
	return sysfs_emit(buf, "%d\n",
			omapdss_device_is_enabled(dssdev));
}

static ssize_t display_enabled_store(struct omap_dss_device *dssdev,
		const char *buf, size_t size)
{
	int r;
	bool enable;

	r = strtobool(buf, &enable);
	if (r)
		return r;

	if (enable == omapdss_device_is_enabled(dssdev))
		return size;

	if (omapdss_device_is_connected(dssdev) == false)
		return -ENODEV;

	if (enable) {
		r = dssdev->driver->enable(dssdev);
		if (r)
			return r;
	} else {
		dssdev->driver->disable(dssdev);
	}

	return size;
}

static ssize_t display_tear_show(struct omap_dss_device *dssdev, char *buf)
{
	return sysfs_emit(buf, "%d\n",
			dssdev->driver->get_te ?
			dssdev->driver->get_te(dssdev) : 0);
}

static ssize_t display_tear_store(struct omap_dss_device *dssdev,
	const char *buf, size_t size)
{
	int r;
	bool te;

	if (!dssdev->driver->enable_te || !dssdev->driver->get_te)
		return -ENOENT;

	r = strtobool(buf, &te);
	if (r)
		return r;

	r = dssdev->driver->enable_te(dssdev, te);
	if (r)
		return r;

	return size;
}

static ssize_t display_timings_show(struct omap_dss_device *dssdev, char *buf)
{
	struct omap_video_timings t;

	if (!dssdev->driver->get_timings)
		return -ENOENT;

	dssdev->driver->get_timings(dssdev, &t);

	return sysfs_emit(buf, "%u,%u/%u/%u/%u,%u/%u/%u/%u\n",
			t.pixelclock,
			t.x_res, t.hfp, t.hbp, t.hsw,
			t.y_res, t.vfp, t.vbp, t.vsw);
}

static ssize_t display_timings_store(struct omap_dss_device *dssdev,
	const char *buf, size_t size)
{
	struct omap_video_timings t = dssdev->panel.timings;
	int r, found;

	if (!dssdev->driver->set_timings || !dssdev->driver->check_timings)
		return -ENOENT;

	found = 0;
#ifdef CONFIG_FB_OMAP2_DSS_VENC
	if (strncmp("pal", buf, 3) == 0) {
		t = omap_dss_pal_timings;
		found = 1;
	} else if (strncmp("ntsc", buf, 4) == 0) {
		t = omap_dss_ntsc_timings;
		found = 1;
	}
#endif
	if (!found && sscanf(buf, "%u,%hu/%hu/%hu/%hu,%hu/%hu/%hu/%hu",
				&t.pixelclock,
				&t.x_res, &t.hfp, &t.hbp, &t.hsw,
				&t.y_res, &t.vfp, &t.vbp, &t.vsw) != 9)
		return -EINVAL;

	r = dssdev->driver->check_timings(dssdev, &t);
	if (r)
		return r;

	dssdev->driver->disable(dssdev);
	dssdev->driver->set_timings(dssdev, &t);
	r = dssdev->driver->enable(dssdev);
	if (r)
		return r;

	return size;
}

static ssize_t display_rotate_show(struct omap_dss_device *dssdev, char *buf)
{
	int rotate;
	if (!dssdev->driver->get_rotate)
		return -ENOENT;
	rotate = dssdev->driver->get_rotate(dssdev);
	return sysfs_emit(buf, "%u\n", rotate);
}

static ssize_t display_rotate_store(struct omap_dss_device *dssdev,
	const char *buf, size_t size)
{
	int rot, r;

	if (!dssdev->driver->set_rotate || !dssdev->driver->get_rotate)
		return -ENOENT;

	r = kstrtoint(buf, 0, &rot);
	if (r)
		return r;

	r = dssdev->driver->set_rotate(dssdev, rot);
	if (r)
		return r;

	return size;
}

static ssize_t display_mirror_show(struct omap_dss_device *dssdev, char *buf)
{
	int mirror;
	if (!dssdev->driver->get_mirror)
		return -ENOENT;
	mirror = dssdev->driver->get_mirror(dssdev);
	return sysfs_emit(buf, "%u\n", mirror);
}

static ssize_t display_mirror_store(struct omap_dss_device *dssdev,
	const char *buf, size_t size)
{
	int r;
	bool mirror;

	if (!dssdev->driver->set_mirror || !dssdev->driver->get_mirror)
		return -ENOENT;

	r = strtobool(buf, &mirror);
	if (r)
		return r;

	r = dssdev->driver->set_mirror(dssdev, mirror);
	if (r)
		return r;

	return size;
}

static ssize_t display_wss_show(struct omap_dss_device *dssdev, char *buf)
{
	unsigned int wss;

	if (!dssdev->driver->get_wss)
		return -ENOENT;

	wss = dssdev->driver->get_wss(dssdev);

	return sysfs_emit(buf, "0x%05x\n", wss);
}

static ssize_t display_wss_store(struct omap_dss_device *dssdev,
	const char *buf, size_t size)
{
	u32 wss;
	int r;

	if (!dssdev->driver->get_wss || !dssdev->driver->set_wss)
		return -ENOENT;

	r = kstrtou32(buf, 0, &wss);
	if (r)
		return r;

	if (wss > 0xfffff)
		return -EINVAL;

	r = dssdev->driver->set_wss(dssdev, wss);
	if (r)
		return r;

	return size;
}

struct display_attribute {
	struct attribute attr;
	ssize_t (*show)(struct omap_dss_device *, char *);
	ssize_t	(*store)(struct omap_dss_device *, const char *, size_t);
};

#define DISPLAY_ATTR(_name, _mode, _show, _store) \
	struct display_attribute display_attr_##_name = \
	__ATTR(_name, _mode, _show, _store)

static DISPLAY_ATTR(name, S_IRUGO, display_name_show, NULL);
static DISPLAY_ATTR(display_name, S_IRUGO, display_name_show, NULL);
static DISPLAY_ATTR(enabled, S_IRUGO|S_IWUSR,
		display_enabled_show, display_enabled_store);
static DISPLAY_ATTR(tear_elim, S_IRUGO|S_IWUSR,
		display_tear_show, display_tear_store);
static DISPLAY_ATTR(timings, S_IRUGO|S_IWUSR,
		display_timings_show, display_timings_store);
static DISPLAY_ATTR(rotate, S_IRUGO|S_IWUSR,
		display_rotate_show, display_rotate_store);
static DISPLAY_ATTR(mirror, S_IRUGO|S_IWUSR,
		display_mirror_show, display_mirror_store);
static DISPLAY_ATTR(wss, S_IRUGO|S_IWUSR,
		display_wss_show, display_wss_store);

static struct attribute *display_sysfs_attrs[] = {
	&display_attr_name.attr,
	&display_attr_display_name.attr,
	&display_attr_enabled.attr,
	&display_attr_tear_elim.attr,
	&display_attr_timings.attr,
	&display_attr_rotate.attr,
	&display_attr_mirror.attr,
	&display_attr_wss.attr,
	NULL
};
ATTRIBUTE_GROUPS(display_sysfs);

static ssize_t display_attr_show(struct kobject *kobj, struct attribute *attr,
		char *buf)
{
	struct omap_dss_device *dssdev;
	struct display_attribute *display_attr;

	dssdev = container_of(kobj, struct omap_dss_device, kobj);
	display_attr = container_of(attr, struct display_attribute, attr);

	if (!display_attr->show)
		return -ENOENT;

	return display_attr->show(dssdev, buf);
}

static ssize_t display_attr_store(struct kobject *kobj, struct attribute *attr,
		const char *buf, size_t size)
{
	struct omap_dss_device *dssdev;
	struct display_attribute *display_attr;

	dssdev = container_of(kobj, struct omap_dss_device, kobj);
	display_attr = container_of(attr, struct display_attribute, attr);

	if (!display_attr->store)
		return -ENOENT;

	return display_attr->store(dssdev, buf, size);
}

static const struct sysfs_ops display_sysfs_ops = {
	.show = display_attr_show,
	.store = display_attr_store,
};

static struct kobj_type display_ktype = {
	.sysfs_ops = &display_sysfs_ops,
	.default_groups = display_sysfs_groups,
};

int display_init_sysfs(struct platform_device *pdev)
{
	struct omap_dss_device *dssdev = NULL;
	int r;

	for_each_dss_dev(dssdev) {
		r = kobject_init_and_add(&dssdev->kobj, &display_ktype,
			&pdev->dev.kobj, "%s", dssdev->alias);
		if (r) {
			DSSERR("failed to create sysfs files\n");
			omap_dss_put_device(dssdev);
			goto err;
		}
	}

	return 0;

err:
	display_uninit_sysfs(pdev);

	return r;
}

void display_uninit_sysfs(struct platform_device *pdev)
{
	struct omap_dss_device *dssdev = NULL;

	for_each_dss_dev(dssdev) {
		if (kobject_name(&dssdev->kobj) == NULL)
			continue;

		kobject_del(&dssdev->kobj);
		kobject_put(&dssdev->kobj);

		memset(&dssdev->kobj, 0, sizeof(dssdev->kobj));
	}
}
