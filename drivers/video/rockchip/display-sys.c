/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/idr.h>
#include <linux/err.h>
#include <linux/kdev_t.h>
#include <linux/display-sys.h>

static struct list_head main_display_device_list;
static struct list_head aux_display_device_list;

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", dsp->name);
}

static DEVICE_ATTR_RO(name);

static ssize_t type_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%s\n", dsp->type);
}

static DEVICE_ATTR_RO(type);

static ssize_t property_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n", dsp->property);
}

static DEVICE_ATTR_RO(property);

static ssize_t enable_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	int enable;

	if (dsp->ops && dsp->ops->getenable)
		enable = dsp->ops->getenable(dsp);
	else
		return 0;
	return snprintf(buf, PAGE_SIZE, "%d\n", enable);
}

static ssize_t enable_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	int enable;

	if (kstrtoint(buf, 0, &enable))
		return size;
	if (dsp->ops && dsp->ops->setenable)
		dsp->ops->setenable(dsp, enable);
	return size;
}

static DEVICE_ATTR_RW(enable);

static ssize_t connect_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	int connect;

	if (dsp->ops && dsp->ops->getstatus)
		connect = dsp->ops->getstatus(dsp);
	else
		return 0;
	return snprintf(buf, PAGE_SIZE, "%d\n", connect);
}

static DEVICE_ATTR_RO(connect);

static int mode_string(char *buf, unsigned int offset,
		       const struct fb_videomode *mode)
{
	char v = 'p';

	if (!buf || !mode) {
		pr_err("%s parameter error\n", __func__);
		return 0;
	}
	if (mode->xres == 0 && mode->yres == 0)
		return snprintf(&buf[offset], PAGE_SIZE - offset, "auto\n");
	if (mode->vmode & FB_VMODE_INTERLACED)
		v = 'i';
	if (mode->vmode & FB_VMODE_DOUBLE)
		v = 'd';
	if (mode->flag)
		return snprintf(&buf[offset], PAGE_SIZE - offset,
				"%dx%d%c-%d(YCbCr420)\n",
				mode->xres, mode->yres, v, mode->refresh);
	else
		return snprintf(&buf[offset], PAGE_SIZE - offset,
				"%dx%d%c-%d\n",
				mode->xres, mode->yres, v, mode->refresh);
}

static ssize_t modes_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	struct list_head *modelist, *pos;
	struct display_modelist *display_modelist;
	const struct fb_videomode *mode;
	int i;

	mutex_lock(&dsp->lock);
	if (dsp->ops && dsp->ops->getmodelist) {
		if (dsp->ops->getmodelist(dsp, &modelist)) {
			mutex_unlock(&dsp->lock);
			return -EINVAL;
		}
	} else {
		mutex_unlock(&dsp->lock);
		return 0;
	}
	i = 0;
	if (dsp->priority == DISPLAY_PRIORITY_HDMI)
		i += snprintf(buf, PAGE_SIZE, "auto\n");

	list_for_each(pos, modelist) {
		display_modelist = list_entry(pos,
					      struct display_modelist,
					      list);
		mode = &display_modelist->mode;
		i += mode_string(buf, i, mode);
	}
	mutex_unlock(&dsp->lock);
	return i;
}

static DEVICE_ATTR_RO(modes);

static ssize_t mode_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	struct fb_videomode mode;

	if (dsp->ops && dsp->ops->getmode)
		if (dsp->ops->getmode(dsp, &mode) == 0)
			return mode_string(buf, 0, &mode);
	return 0;
}

static ssize_t mode_store(struct device *dev,
			  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	char mstr[100];
	struct list_head *modelist, *pos;
	struct display_modelist *display_modelist;
	struct fb_videomode *mode;
	size_t i;

	mutex_lock(&dsp->lock);
	if (!memcmp(buf, "auto", 4)) {
		if (dsp->ops && dsp->ops->setmode)
			dsp->ops->setmode(dsp, NULL);
		mutex_unlock(&dsp->lock);
		return count;
	}

	if (dsp->ops && dsp->ops->getmodelist) {
		if (dsp->ops && dsp->ops->getmodelist) {
			if (dsp->ops->getmodelist(dsp, &modelist)) {
				mutex_unlock(&dsp->lock);
				return -EINVAL;
			}
		}
		list_for_each(pos, modelist) {
			display_modelist = list_entry(pos,
						      struct display_modelist,
						      list);
			mode = &display_modelist->mode;
			i = mode_string(mstr, 0, mode);
			if (strncmp(mstr, buf, max(count, i)) == 0) {
				if (dsp->ops && dsp->ops->setmode)
					dsp->ops->setmode(dsp, mode);
				mutex_unlock(&dsp->lock);
				return count;
			}
		}
	}
	mutex_unlock(&dsp->lock);
	return -EINVAL;
}

static DEVICE_ATTR_RW(mode);

static ssize_t scale_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	int xscale, yscale;

	if (dsp->ops && dsp->ops->getscale) {
		xscale = dsp->ops->getscale(dsp, DISPLAY_SCALE_X);
		yscale = dsp->ops->getscale(dsp, DISPLAY_SCALE_Y);
		if (xscale && yscale)
			return snprintf(buf, PAGE_SIZE,
					"xscale=%d yscale=%d\n",
					xscale, yscale);
	}
	return -EINVAL;
}

static ssize_t scale_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	int scale = 100;

	if (dsp->ops && dsp->ops->setscale) {
		if (!strncmp(buf, "xscale", 6)) {
			if (!kstrtoint(buf, 0, &scale))
				dsp->ops->setscale(dsp,
						   DISPLAY_SCALE_X,
						   scale);
		} else if (!strncmp(buf, "yscale", 6)) {
			if (!kstrtoint(buf, 0, &scale))
				dsp->ops->setscale(dsp,
						   DISPLAY_SCALE_Y,
						   scale);
		} else {
			if (!kstrtoint(buf, 0, &scale)) {
				dsp->ops->setscale(dsp,
						   DISPLAY_SCALE_X,
						   scale);
				dsp->ops->setscale(dsp,
						   DISPLAY_SCALE_Y,
						   scale);
			}
		}
		return count;
	}
	return -EINVAL;
}

static DEVICE_ATTR_RW(scale);

static ssize_t mode3d_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	struct list_head *modelist, *pos;
	struct display_modelist *display_modelist;
	struct fb_videomode mode;
	int i = 0, cur_3d_mode = -1;
	char mode_str[128];
	int mode_strlen, format_3d;

	mutex_lock(&dsp->lock);
	if (dsp->ops && dsp->ops->getmodelist) {
		if (dsp->ops->getmodelist(dsp, &modelist)) {
			mutex_unlock(&dsp->lock);
			return -EINVAL;
		}
	} else {
		mutex_unlock(&dsp->lock);
		return 0;
	}

	if (dsp->ops && dsp->ops->getmode) {
		if (dsp->ops->getmode(dsp, &mode)) {
			mutex_unlock(&dsp->lock);
			return -EINVAL;
		}
	} else {
		mutex_unlock(&dsp->lock);
		return 0;
	}

	list_for_each(pos, modelist) {
		display_modelist = list_entry(pos,
					      struct display_modelist,
					      list);
		if (!fb_mode_is_equal(&mode, &display_modelist->mode))
			display_modelist = NULL;
		else
			break;
	}
	if (display_modelist)
		i = snprintf(buf, PAGE_SIZE, "3dmodes=%d\n",
			     display_modelist->format_3d);
	else
		i = snprintf(buf, PAGE_SIZE, "3dmodes=0\n");

	if (dsp->ops && dsp->ops->get3dmode)
		cur_3d_mode = dsp->ops->get3dmode(dsp);
	i += snprintf(buf + i, PAGE_SIZE - i, "cur3dmode=%d\n", cur_3d_mode);

	list_for_each(pos, modelist) {
		display_modelist = list_entry(pos,
					      struct display_modelist,
					      list);
		mode_strlen = mode_string(mode_str, 0,
					  &display_modelist->mode);
		mode_str[mode_strlen - 1] = 0;
		format_3d = display_modelist->format_3d;
		i += snprintf(buf + i, PAGE_SIZE, "%s,%d\n",
			      mode_str, format_3d);
	}
	mutex_unlock(&dsp->lock);
	return i;
}

static ssize_t mode3d_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	int mode;

	mutex_lock(&dsp->lock);
	if (dsp->ops && dsp->ops->set3dmode) {
		if (!kstrtoint(buf, 0, &mode))
			dsp->ops->set3dmode(dsp, mode);
		mutex_unlock(&dsp->lock);
		return count;
	}
	mutex_unlock(&dsp->lock);
	return -EINVAL;
}

static DEVICE_ATTR_RW(mode3d);

static ssize_t color_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	int ret = 0;

	mutex_lock(&dsp->lock);
	if (dsp->ops && dsp->ops->getcolor)
		ret = dsp->ops->getcolor(dsp, buf);
	mutex_unlock(&dsp->lock);
	return ret;
}

static ssize_t color_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);

	mutex_lock(&dsp->lock);
	if (dsp->ops && dsp->ops->setcolor) {
		if (!dsp->ops->setcolor(dsp, buf, count)) {
			mutex_unlock(&dsp->lock);
			return count;
		}
	}
	mutex_unlock(&dsp->lock);
	return -EINVAL;
}

static DEVICE_ATTR_RW(color);

static ssize_t audioinfo_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	char audioinfo[200];
	int ret = 0;

	mutex_lock(&dsp->lock);
	if (dsp->ops && dsp->ops->getedidaudioinfo) {
		ret = dsp->ops->getedidaudioinfo(dsp, audioinfo, 200);
		if (!ret) {
			mutex_unlock(&dsp->lock);
			return snprintf(buf, PAGE_SIZE, "%s\n", audioinfo);
		}
	}
	mutex_unlock(&dsp->lock);
	return -EINVAL;
}

static DEVICE_ATTR_RO(audioinfo);

static ssize_t monspecs_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	struct fb_monspecs monspecs;
	int ret = 0;

	mutex_lock(&dsp->lock);
	if (dsp->ops && dsp->ops->getmonspecs) {
		ret = dsp->ops->getmonspecs(dsp, &monspecs);
		if (!ret) {
			mutex_unlock(&dsp->lock);
			memcpy(buf, &monspecs, sizeof(struct fb_monspecs));
			return sizeof(struct fb_monspecs);
		}
	}
	mutex_unlock(&dsp->lock);
	return -EINVAL;
}

static DEVICE_ATTR_RO(monspecs);

static ssize_t debug_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	int ret = -EINVAL;

	mutex_lock(&dsp->lock);
	if (dsp->ops && dsp->ops->getdebug)
		ret = dsp->ops->getdebug(dsp, buf);
	mutex_unlock(&dsp->lock);
	return ret;
}

static ssize_t debug_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	int cmd, ret = -EINVAL;
	struct rk_display_device *dsp = dev_get_drvdata(dev);

	mutex_lock(&dsp->lock);
	if (dsp->ops && dsp->ops->setdebug) {
		if (kstrtoint(buf, 0, &cmd) == 0)
			dsp->ops->setdebug(dsp, cmd);
		ret = count;
	}
	mutex_unlock(&dsp->lock);
	return ret;
}

static DEVICE_ATTR_RW(debug);

static ssize_t prop_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);
	int ret = -EINVAL;

	mutex_lock(&dsp->lock);
	if (dsp->ops && dsp->ops->getvrinfo)
		ret = dsp->ops->getvrinfo(dsp, buf);
	mutex_unlock(&dsp->lock);

	return ret;
}

static DEVICE_ATTR_RO(prop);

static struct attribute *display_device_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_type.attr,
	&dev_attr_property.attr,
	&dev_attr_enable.attr,
	&dev_attr_connect.attr,
	&dev_attr_modes.attr,
	&dev_attr_mode.attr,
	&dev_attr_scale.attr,
	&dev_attr_mode3d.attr,
	&dev_attr_color.attr,
	&dev_attr_audioinfo.attr,
	&dev_attr_monspecs.attr,
	&dev_attr_debug.attr,
	&dev_attr_prop.attr,
	NULL,
};

ATTRIBUTE_GROUPS(display_device);

static int display_suspend(struct device *dev, pm_message_t state)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);

	mutex_lock(&dsp->lock);
	if (likely(dsp->driver->suspend))
		dsp->driver->suspend(dsp, state);
	mutex_unlock(&dsp->lock);
	return 0;
};

static int display_resume(struct device *dev)
{
	struct rk_display_device *dsp = dev_get_drvdata(dev);

	mutex_lock(&dsp->lock);
	if (likely(dsp->driver->resume))
		dsp->driver->resume(dsp);
	mutex_unlock(&dsp->lock);
	return 0;
};

int display_add_videomode(const struct fb_videomode *mode,
			  struct list_head *head)
{
	struct list_head *pos;
	struct display_modelist *modelist;
	struct fb_videomode *m;
	int found = 0;

	list_for_each(pos, head) {
		modelist = list_entry(pos, struct display_modelist, list);
		m = &modelist->mode;
		if (fb_mode_is_equal(m, mode)) {
			found = 1;
			break;
		}
	}
	if (!found) {
		modelist = kmalloc(sizeof(*modelist),
				   GFP_KERNEL);

		if (!modelist)
			return -ENOMEM;
		modelist->mode = *mode;
		list_add(&modelist->list, head);
	}
	return 0;
}

void rk_display_device_enable(struct rk_display_device *ddev)
{
	struct list_head *pos, *head;
	struct rk_display_device *dev = NULL, *dev_enabled = NULL;
	struct rk_display_device *dev_enable = NULL;
	int enable = 0, connect;

	if (ddev->property == DISPLAY_MAIN)
		head = &main_display_device_list;
	else
		head = &aux_display_device_list;

	list_for_each(pos, head) {
		dev = list_entry(pos, struct rk_display_device, list);
		enable = dev->ops->getenable(dev);
		connect = dev->ops->getstatus(dev);
		if (connect)
			dev_enable = dev;
		if (enable == 1)
			dev_enabled = dev;
	}
	/* If no device is connected, enable highest priority device. */
	if (!dev_enable) {
		dev->ops->setenable(dev, 1);
		return;
	}

	if (dev_enable == dev_enabled) {
		if (dev_enable != ddev)
			ddev->ops->setenable(ddev, 0);
	} else {
		if (dev_enabled &&
		    dev_enabled->priority != DISPLAY_PRIORITY_HDMI)
			dev_enabled->ops->setenable(dev_enabled, 0);
		dev_enable->ops->setenable(dev_enable, 1);
	}
}
EXPORT_SYMBOL(rk_display_device_enable);

void rk_display_device_enable_other(struct rk_display_device *ddev)
{
#ifndef CONFIG_DISPLAY_AUTO_SWITCH
	return;
#else
	struct list_head *pos, *head;
	struct rk_display_device *dev;
	int connect = 0;

	if (ddev->property == DISPLAY_MAIN)
		head = &main_display_device_list;
	else
		head = &aux_display_device_list;

	list_for_each_prev(pos, head) {
		dev = list_entry(pos, struct rk_display_device, list);
		if (dev != ddev) {
			connect = dev->ops->getstatus(dev);
			if (connect) {
				dev->ops->setenable(dev, 1);
				return;
			}
		}
	}
#endif
}
EXPORT_SYMBOL(rk_display_device_enable_other);

void rk_display_device_disable_other(struct rk_display_device *ddev)
{
#ifndef CONFIG_DISPLAY_AUTO_SWITCH
	return;
#else
	struct list_head *pos, *head;
	struct rk_display_device *dev;
	int enable = 0;

	if (ddev->property == DISPLAY_MAIN)
		head = &main_display_device_list;
	else
		head = &aux_display_device_list;

	list_for_each(pos, head) {
		dev = list_entry(pos, struct rk_display_device, list);
		if (dev != ddev) {
			enable = dev->ops->getenable(dev);
			if (enable)
				dev->ops->setenable(dev, 0);
		}
	}
	ddev->ops->setenable(ddev, 1);
#endif
}
EXPORT_SYMBOL(rk_display_device_disable_other);

void rk_display_device_select(int property, int priority)
{
	struct list_head *pos, *head;
	struct rk_display_device *dev;
	int enable, found = 0;

	if (property == DISPLAY_MAIN)
		head = &main_display_device_list;
	else
		head = &aux_display_device_list;

	list_for_each(pos, head) {
		dev = list_entry(pos, struct rk_display_device, list);
		if (dev->priority == priority)
			found = 1;
	}

	if (!found) {
		pr_err("[%s] select display interface %d not exist\n",
		       __func__, priority);
		return;
	}

	list_for_each(pos, head) {
		dev = list_entry(pos, struct rk_display_device, list);
		enable = dev->ops->getenable(dev);
		if (dev->priority == priority) {
			if (!enable)
				dev->ops->setenable(dev, 1);
		} else if (enable) {
			dev->ops->setenable(dev, 0);
		}
	}
}
EXPORT_SYMBOL(rk_display_device_select);
static struct mutex allocated_dsp_lock;
static DEFINE_IDR(allocated_dsp);
static struct class *display_class;

struct rk_display_device
	*rk_display_device_register(struct rk_display_driver *driver,
				    struct device *parent, void *devdata)
{
	struct rk_display_device *new_dev = NULL;
	int ret = -EINVAL;

	if (unlikely(!driver))
		return ERR_PTR(ret);

	new_dev = kzalloc(sizeof(*new_dev), GFP_KERNEL);
	if (likely(new_dev) && unlikely(driver->probe(new_dev, devdata))) {
		/* Reserve the index for this display */
		mutex_lock(&allocated_dsp_lock);
		new_dev->idx = idr_alloc(&allocated_dsp, new_dev,
					 0, 0, GFP_KERNEL);
		mutex_unlock(&allocated_dsp_lock);

		if (new_dev->idx >= 0) {
			struct list_head *pos, *head;
			struct rk_display_device *dev;
			int i = 0;

			head = &main_display_device_list;
			list_for_each_entry(dev, head, list) {
				if (strcmp(dev->type, new_dev->type) == 0)
					i++;
			}
			head = &aux_display_device_list;
			list_for_each_entry(dev, head, list) {
				if (strcmp(dev->type, new_dev->type) == 0)
					i++;
			}
			if (i == 0)
				new_dev->dev =
				device_create(display_class, parent,
					      MKDEV(0, 0), new_dev,
					      "%s", new_dev->type);
			else
				new_dev->dev =
				device_create(display_class, parent,
					      MKDEV(0, 0), new_dev,
					      "%s%d", new_dev->type, i);

			if (!IS_ERR(new_dev->dev)) {
				new_dev->parent = parent;
				new_dev->driver = driver;
				if (parent)
					new_dev->dev->driver = parent->driver;
				mutex_init(&new_dev->lock);
				/* Add new device to display device list. */
				if (new_dev->property == DISPLAY_MAIN)
					head = &main_display_device_list;
				else
					head = &aux_display_device_list;

				list_for_each(pos, head) {
					dev =
					list_entry(pos,
						   struct rk_display_device,
						   list);
					if (dev->priority > new_dev->priority)
						break;
				}
				list_add_tail(&new_dev->list, pos);
				return new_dev;
			}
			mutex_lock(&allocated_dsp_lock);
			idr_remove(&allocated_dsp, new_dev->idx);
			mutex_unlock(&allocated_dsp_lock);
			ret = -EINVAL;
		}
	}
	kfree(new_dev);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(rk_display_device_register);

void rk_display_device_unregister(struct rk_display_device *ddev)
{
	if (!ddev)
		return;
	/* Free device */
	mutex_lock(&ddev->lock);
	device_unregister(ddev->dev);
	mutex_unlock(&ddev->lock);
	/* Mark device index as available */
	mutex_lock(&allocated_dsp_lock);
	idr_remove(&allocated_dsp, ddev->idx);
	mutex_unlock(&allocated_dsp_lock);
	list_del(&ddev->list);
	kfree(ddev);
}
EXPORT_SYMBOL(rk_display_device_unregister);

static int __init rk_display_class_init(void)
{
	display_class = class_create(THIS_MODULE, "display");
	if (IS_ERR(display_class)) {
		pr_err("Failed to create display class\n");
		display_class = NULL;
		return -EINVAL;
	}
	display_class->dev_groups = display_device_groups;
	display_class->suspend = display_suspend;
	display_class->resume = display_resume;
	mutex_init(&allocated_dsp_lock);
	INIT_LIST_HEAD(&main_display_device_list);
	INIT_LIST_HEAD(&aux_display_device_list);
	return 0;
}

static void __exit rk_display_class_exit(void)
{
	class_destroy(display_class);
}

subsys_initcall(rk_display_class_init);
module_exit(rk_display_class_exit);

MODULE_AUTHOR("zhengyang@rock-chips.com");
MODULE_DESCRIPTION("Driver for rk display device");
MODULE_LICENSE("GPL");
