/*
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define DSS_SUBSYS_NAME "DISPLAY"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>

#include <video/omapdss.h>
#include "dss.h"

static struct omap_dss_device *to_dss_device_sysfs(struct device *dev)
{
	struct omap_dss_device *dssdev = NULL;

	for_each_dss_dev(dssdev) {
		if (dssdev->dev == dev) {
			omap_dss_put_device(dssdev);
			return dssdev;
		}
	}

	return NULL;
}

static ssize_t display_name_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device_sysfs(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			dssdev->name ?
			dssdev->name : "");
}

static ssize_t display_enabled_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device_sysfs(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n",
			omapdss_device_is_enabled(dssdev));
}

static ssize_t display_enabled_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct omap_dss_device *dssdev = to_dss_device_sysfs(dev);
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

static ssize_t display_tear_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device_sysfs(dev);
	return snprintf(buf, PAGE_SIZE, "%d\n",
			dssdev->driver->get_te ?
			dssdev->driver->get_te(dssdev) : 0);
}

static ssize_t display_tear_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct omap_dss_device *dssdev = to_dss_device_sysfs(dev);
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

static ssize_t display_timings_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device_sysfs(dev);
	struct omap_video_timings t;

	if (!dssdev->driver->get_timings)
		return -ENOENT;

	dssdev->driver->get_timings(dssdev, &t);

	return snprintf(buf, PAGE_SIZE, "%u,%u/%u/%u/%u,%u/%u/%u/%u\n",
			t.pixel_clock,
			t.x_res, t.hfp, t.hbp, t.hsw,
			t.y_res, t.vfp, t.vbp, t.vsw);
}

static ssize_t display_timings_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct omap_dss_device *dssdev = to_dss_device_sysfs(dev);
	struct omap_video_timings t = dssdev->panel.timings;
	int r, found;

	if (!dssdev->driver->set_timings || !dssdev->driver->check_timings)
		return -ENOENT;

	found = 0;
#ifdef CONFIG_OMAP2_DSS_VENC
	if (strncmp("pal", buf, 3) == 0) {
		t = omap_dss_pal_timings;
		found = 1;
	} else if (strncmp("ntsc", buf, 4) == 0) {
		t = omap_dss_ntsc_timings;
		found = 1;
	}
#endif
	if (!found && sscanf(buf, "%u,%hu/%hu/%hu/%hu,%hu/%hu/%hu/%hu",
				&t.pixel_clock,
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

static ssize_t display_rotate_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device_sysfs(dev);
	int rotate;
	if (!dssdev->driver->get_rotate)
		return -ENOENT;
	rotate = dssdev->driver->get_rotate(dssdev);
	return snprintf(buf, PAGE_SIZE, "%u\n", rotate);
}

static ssize_t display_rotate_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct omap_dss_device *dssdev = to_dss_device_sysfs(dev);
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

static ssize_t display_mirror_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device_sysfs(dev);
	int mirror;
	if (!dssdev->driver->get_mirror)
		return -ENOENT;
	mirror = dssdev->driver->get_mirror(dssdev);
	return snprintf(buf, PAGE_SIZE, "%u\n", mirror);
}

static ssize_t display_mirror_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct omap_dss_device *dssdev = to_dss_device_sysfs(dev);
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

static ssize_t display_wss_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device_sysfs(dev);
	unsigned int wss;

	if (!dssdev->driver->get_wss)
		return -ENOENT;

	wss = dssdev->driver->get_wss(dssdev);

	return snprintf(buf, PAGE_SIZE, "0x%05x\n", wss);
}

static ssize_t display_wss_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct omap_dss_device *dssdev = to_dss_device_sysfs(dev);
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

static DEVICE_ATTR(display_name, S_IRUGO, display_name_show, NULL);
static DEVICE_ATTR(enabled, S_IRUGO|S_IWUSR,
		display_enabled_show, display_enabled_store);
static DEVICE_ATTR(tear_elim, S_IRUGO|S_IWUSR,
		display_tear_show, display_tear_store);
static DEVICE_ATTR(timings, S_IRUGO|S_IWUSR,
		display_timings_show, display_timings_store);
static DEVICE_ATTR(rotate, S_IRUGO|S_IWUSR,
		display_rotate_show, display_rotate_store);
static DEVICE_ATTR(mirror, S_IRUGO|S_IWUSR,
		display_mirror_show, display_mirror_store);
static DEVICE_ATTR(wss, S_IRUGO|S_IWUSR,
		display_wss_show, display_wss_store);

static const struct attribute *display_sysfs_attrs[] = {
	&dev_attr_display_name.attr,
	&dev_attr_enabled.attr,
	&dev_attr_tear_elim.attr,
	&dev_attr_timings.attr,
	&dev_attr_rotate.attr,
	&dev_attr_mirror.attr,
	&dev_attr_wss.attr,
	NULL
};

int display_init_sysfs(struct platform_device *pdev)
{
	struct omap_dss_device *dssdev = NULL;
	int r;

	for_each_dss_dev(dssdev) {
		struct kobject *kobj = &dssdev->dev->kobj;

		r = sysfs_create_files(kobj, display_sysfs_attrs);
		if (r) {
			DSSERR("failed to create sysfs files\n");
			goto err;
		}

		r = sysfs_create_link(&pdev->dev.kobj, kobj, dssdev->alias);
		if (r) {
			sysfs_remove_files(kobj, display_sysfs_attrs);

			DSSERR("failed to create sysfs display link\n");
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
		sysfs_remove_link(&pdev->dev.kobj, dssdev->alias);
		sysfs_remove_files(&dssdev->dev->kobj,
				display_sysfs_attrs);
	}
}
