// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * fbsysfs.c - framebuffer device class and attributes
 *
 * Copyright (c) 2004 James Simmons <jsimmons@infradead.org>
 */

/*
 * Note:  currently there's only stubs for framebuffer_alloc and
 * framebuffer_release here.  The reson for that is that until all drivers
 * are converted to use it a sysfsification will open OOPSable races.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/fbcon.h>
#include <linux/console.h>
#include <linux/module.h>

#define FB_SYSFS_FLAG_ATTR 1

/**
 * framebuffer_alloc - creates a new frame buffer info structure
 *
 * @size: size of driver private data, can be zero
 * @dev: pointer to the device for this fb, this can be NULL
 *
 * Creates a new frame buffer info structure. Also reserves @size bytes
 * for driver private data (info->par). info->par (if any) will be
 * aligned to sizeof(long).
 *
 * Returns the new structure, or NULL if an error occurred.
 *
 */
struct fb_info *framebuffer_alloc(size_t size, struct device *dev)
{
#define BYTES_PER_LONG (BITS_PER_LONG/8)
#define PADDING (BYTES_PER_LONG - (sizeof(struct fb_info) % BYTES_PER_LONG))
	int fb_info_size = sizeof(struct fb_info);
	struct fb_info *info;
	char *p;

	if (size)
		fb_info_size += PADDING;

	p = kzalloc(fb_info_size + size, GFP_KERNEL);

	if (!p)
		return NULL;

	info = (struct fb_info *) p;

	if (size)
		info->par = p + fb_info_size;

	info->device = dev;
	info->fbcon_rotate_hint = -1;

#if IS_ENABLED(CONFIG_FB_BACKLIGHT)
	mutex_init(&info->bl_curve_mutex);
#endif

	return info;
#undef PADDING
#undef BYTES_PER_LONG
}
EXPORT_SYMBOL(framebuffer_alloc);

/**
 * framebuffer_release - marks the structure available for freeing
 *
 * @info: frame buffer info structure
 *
 * Drop the reference count of the device embedded in the
 * framebuffer info structure.
 *
 */
void framebuffer_release(struct fb_info *info)
{
	if (!info)
		return;

	if (WARN_ON(refcount_read(&info->count)))
		return;

#if IS_ENABLED(CONFIG_FB_BACKLIGHT)
	mutex_destroy(&info->bl_curve_mutex);
#endif

	kfree(info->apertures);
	kfree(info);
}
EXPORT_SYMBOL(framebuffer_release);

static int activate(struct fb_info *fb_info, struct fb_var_screeninfo *var)
{
	int err;

	var->activate |= FB_ACTIVATE_FORCE;
	console_lock();
	err = fb_set_var(fb_info, var);
	if (!err)
		fbcon_update_vcs(fb_info, var->activate & FB_ACTIVATE_ALL);
	console_unlock();
	if (err)
		return err;
	return 0;
}

static int mode_string(char *buf, unsigned int offset,
		       const struct fb_videomode *mode)
{
	char m = 'U';
	char v = 'p';

	if (mode->flag & FB_MODE_IS_DETAILED)
		m = 'D';
	if (mode->flag & FB_MODE_IS_VESA)
		m = 'V';
	if (mode->flag & FB_MODE_IS_STANDARD)
		m = 'S';

	if (mode->vmode & FB_VMODE_INTERLACED)
		v = 'i';
	if (mode->vmode & FB_VMODE_DOUBLE)
		v = 'd';

	return snprintf(&buf[offset], PAGE_SIZE - offset, "%c:%dx%d%c-%d\n",
	                m, mode->xres, mode->yres, v, mode->refresh);
}

static ssize_t store_mode(struct device *device, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	char mstr[100];
	struct fb_var_screeninfo var;
	struct fb_modelist *modelist;
	struct fb_videomode *mode;
	struct list_head *pos;
	size_t i;
	int err;

	memset(&var, 0, sizeof(var));

	list_for_each(pos, &fb_info->modelist) {
		modelist = list_entry(pos, struct fb_modelist, list);
		mode = &modelist->mode;
		i = mode_string(mstr, 0, mode);
		if (strncmp(mstr, buf, max(count, i)) == 0) {

			var = fb_info->var;
			fb_videomode_to_var(&var, mode);
			if ((err = activate(fb_info, &var)))
				return err;
			fb_info->mode = mode;
			return count;
		}
	}
	return -EINVAL;
}

static ssize_t show_mode(struct device *device, struct device_attribute *attr,
			 char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);

	if (!fb_info->mode)
		return 0;

	return mode_string(buf, 0, fb_info->mode);
}

static ssize_t store_modes(struct device *device,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	LIST_HEAD(old_list);
	int i = count / sizeof(struct fb_videomode);

	if (i * sizeof(struct fb_videomode) != count)
		return -EINVAL;

	console_lock();
	lock_fb_info(fb_info);

	list_splice(&fb_info->modelist, &old_list);
	fb_videomode_to_modelist((const struct fb_videomode *)buf, i,
				 &fb_info->modelist);
	if (fb_new_modelist(fb_info)) {
		fb_destroy_modelist(&fb_info->modelist);
		list_splice(&old_list, &fb_info->modelist);
	} else
		fb_destroy_modelist(&old_list);

	unlock_fb_info(fb_info);
	console_unlock();

	return 0;
}

static ssize_t show_modes(struct device *device, struct device_attribute *attr,
			  char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	unsigned int i;
	struct list_head *pos;
	struct fb_modelist *modelist;
	const struct fb_videomode *mode;

	i = 0;
	list_for_each(pos, &fb_info->modelist) {
		modelist = list_entry(pos, struct fb_modelist, list);
		mode = &modelist->mode;
		i += mode_string(buf, i, mode);
	}
	return i;
}

static ssize_t store_bpp(struct device *device, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fb_var_screeninfo var;
	char ** last = NULL;
	int err;

	var = fb_info->var;
	var.bits_per_pixel = simple_strtoul(buf, last, 0);
	if ((err = activate(fb_info, &var)))
		return err;
	return count;
}

static ssize_t show_bpp(struct device *device, struct device_attribute *attr,
			char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	return snprintf(buf, PAGE_SIZE, "%d\n", fb_info->var.bits_per_pixel);
}

static ssize_t store_rotate(struct device *device,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fb_var_screeninfo var;
	char **last = NULL;
	int err;

	var = fb_info->var;
	var.rotate = simple_strtoul(buf, last, 0);

	if ((err = activate(fb_info, &var)))
		return err;

	return count;
}


static ssize_t show_rotate(struct device *device,
			   struct device_attribute *attr, char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);

	return snprintf(buf, PAGE_SIZE, "%d\n", fb_info->var.rotate);
}

static ssize_t store_virtual(struct device *device,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fb_var_screeninfo var;
	char *last = NULL;
	int err;

	var = fb_info->var;
	var.xres_virtual = simple_strtoul(buf, &last, 0);
	last++;
	if (last - buf >= count)
		return -EINVAL;
	var.yres_virtual = simple_strtoul(last, &last, 0);

	if ((err = activate(fb_info, &var)))
		return err;
	return count;
}

static ssize_t show_virtual(struct device *device,
			    struct device_attribute *attr, char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	return snprintf(buf, PAGE_SIZE, "%d,%d\n", fb_info->var.xres_virtual,
			fb_info->var.yres_virtual);
}

static ssize_t show_stride(struct device *device,
			   struct device_attribute *attr, char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	return snprintf(buf, PAGE_SIZE, "%d\n", fb_info->fix.line_length);
}

static ssize_t store_blank(struct device *device,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	char *last = NULL;
	int err, arg;

	arg = simple_strtoul(buf, &last, 0);
	console_lock();
	err = fb_blank(fb_info, arg);
	/* might again call into fb_blank */
	fbcon_fb_blanked(fb_info, arg);
	console_unlock();
	if (err < 0)
		return err;
	return count;
}

static ssize_t show_blank(struct device *device,
			  struct device_attribute *attr, char *buf)
{
//	struct fb_info *fb_info = dev_get_drvdata(device);
	return 0;
}

static ssize_t store_console(struct device *device,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
//	struct fb_info *fb_info = dev_get_drvdata(device);
	return 0;
}

static ssize_t show_console(struct device *device,
			    struct device_attribute *attr, char *buf)
{
//	struct fb_info *fb_info = dev_get_drvdata(device);
	return 0;
}

static ssize_t store_cursor(struct device *device,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
//	struct fb_info *fb_info = dev_get_drvdata(device);
	return 0;
}

static ssize_t show_cursor(struct device *device,
			   struct device_attribute *attr, char *buf)
{
//	struct fb_info *fb_info = dev_get_drvdata(device);
	return 0;
}

static ssize_t store_pan(struct device *device,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fb_var_screeninfo var;
	char *last = NULL;
	int err;

	var = fb_info->var;
	var.xoffset = simple_strtoul(buf, &last, 0);
	last++;
	if (last - buf >= count)
		return -EINVAL;
	var.yoffset = simple_strtoul(last, &last, 0);

	console_lock();
	err = fb_pan_display(fb_info, &var);
	console_unlock();

	if (err < 0)
		return err;
	return count;
}

static ssize_t show_pan(struct device *device,
			struct device_attribute *attr, char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	return snprintf(buf, PAGE_SIZE, "%d,%d\n", fb_info->var.xoffset,
			fb_info->var.yoffset);
}

static ssize_t show_name(struct device *device,
			 struct device_attribute *attr, char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);

	return snprintf(buf, PAGE_SIZE, "%s\n", fb_info->fix.id);
}

static ssize_t store_fbstate(struct device *device,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	u32 state;
	char *last = NULL;

	state = simple_strtoul(buf, &last, 0);

	console_lock();
	lock_fb_info(fb_info);

	fb_set_suspend(fb_info, (int)state);

	unlock_fb_info(fb_info);
	console_unlock();

	return count;
}

static ssize_t show_fbstate(struct device *device,
			    struct device_attribute *attr, char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	return snprintf(buf, PAGE_SIZE, "%d\n", fb_info->state);
}

#if IS_ENABLED(CONFIG_FB_BACKLIGHT)
static ssize_t store_bl_curve(struct device *device,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	u8 tmp_curve[FB_BACKLIGHT_LEVELS];
	unsigned int i;

	/* Some drivers don't use framebuffer_alloc(), but those also
	 * don't have backlights.
	 */
	if (!fb_info || !fb_info->bl_dev)
		return -ENODEV;

	if (count != (FB_BACKLIGHT_LEVELS / 8 * 24))
		return -EINVAL;

	for (i = 0; i < (FB_BACKLIGHT_LEVELS / 8); ++i)
		if (sscanf(&buf[i * 24],
			"%2hhx %2hhx %2hhx %2hhx %2hhx %2hhx %2hhx %2hhx\n",
			&tmp_curve[i * 8 + 0],
			&tmp_curve[i * 8 + 1],
			&tmp_curve[i * 8 + 2],
			&tmp_curve[i * 8 + 3],
			&tmp_curve[i * 8 + 4],
			&tmp_curve[i * 8 + 5],
			&tmp_curve[i * 8 + 6],
			&tmp_curve[i * 8 + 7]) != 8)
			return -EINVAL;

	/* If there has been an error in the input data, we won't
	 * reach this loop.
	 */
	mutex_lock(&fb_info->bl_curve_mutex);
	for (i = 0; i < FB_BACKLIGHT_LEVELS; ++i)
		fb_info->bl_curve[i] = tmp_curve[i];
	mutex_unlock(&fb_info->bl_curve_mutex);

	return count;
}

static ssize_t show_bl_curve(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	ssize_t len = 0;
	unsigned int i;

	/* Some drivers don't use framebuffer_alloc(), but those also
	 * don't have backlights.
	 */
	if (!fb_info || !fb_info->bl_dev)
		return -ENODEV;

	mutex_lock(&fb_info->bl_curve_mutex);
	for (i = 0; i < FB_BACKLIGHT_LEVELS; i += 8)
		len += scnprintf(&buf[len], PAGE_SIZE - len, "%8ph\n",
				fb_info->bl_curve + i);
	mutex_unlock(&fb_info->bl_curve_mutex);

	return len;
}
#endif

/* When cmap is added back in it should be a binary attribute
 * not a text one. Consideration should also be given to converting
 * fbdev to use configfs instead of sysfs */
static struct device_attribute device_attrs[] = {
	__ATTR(bits_per_pixel, S_IRUGO|S_IWUSR, show_bpp, store_bpp),
	__ATTR(blank, S_IRUGO|S_IWUSR, show_blank, store_blank),
	__ATTR(console, S_IRUGO|S_IWUSR, show_console, store_console),
	__ATTR(cursor, S_IRUGO|S_IWUSR, show_cursor, store_cursor),
	__ATTR(mode, S_IRUGO|S_IWUSR, show_mode, store_mode),
	__ATTR(modes, S_IRUGO|S_IWUSR, show_modes, store_modes),
	__ATTR(pan, S_IRUGO|S_IWUSR, show_pan, store_pan),
	__ATTR(virtual_size, S_IRUGO|S_IWUSR, show_virtual, store_virtual),
	__ATTR(name, S_IRUGO, show_name, NULL),
	__ATTR(stride, S_IRUGO, show_stride, NULL),
	__ATTR(rotate, S_IRUGO|S_IWUSR, show_rotate, store_rotate),
	__ATTR(state, S_IRUGO|S_IWUSR, show_fbstate, store_fbstate),
#if IS_ENABLED(CONFIG_FB_BACKLIGHT)
	__ATTR(bl_curve, S_IRUGO|S_IWUSR, show_bl_curve, store_bl_curve),
#endif
};

int fb_init_device(struct fb_info *fb_info)
{
	int i, error = 0;

	dev_set_drvdata(fb_info->dev, fb_info);

	fb_info->class_flag |= FB_SYSFS_FLAG_ATTR;

	for (i = 0; i < ARRAY_SIZE(device_attrs); i++) {
		error = device_create_file(fb_info->dev, &device_attrs[i]);

		if (error)
			break;
	}

	if (error) {
		while (--i >= 0)
			device_remove_file(fb_info->dev, &device_attrs[i]);
		fb_info->class_flag &= ~FB_SYSFS_FLAG_ATTR;
	}

	return 0;
}

void fb_cleanup_device(struct fb_info *fb_info)
{
	unsigned int i;

	if (fb_info->class_flag & FB_SYSFS_FLAG_ATTR) {
		for (i = 0; i < ARRAY_SIZE(device_attrs); i++)
			device_remove_file(fb_info->dev, &device_attrs[i]);

		fb_info->class_flag &= ~FB_SYSFS_FLAG_ATTR;
	}
}

#if IS_ENABLED(CONFIG_FB_BACKLIGHT)
/* This function generates a linear backlight curve
 *
 *     0: off
 *   1-7: min
 * 8-127: linear from min to max
 */
void fb_bl_default_curve(struct fb_info *fb_info, u8 off, u8 min, u8 max)
{
	unsigned int i, flat, count, range = (max - min);

	mutex_lock(&fb_info->bl_curve_mutex);

	fb_info->bl_curve[0] = off;

	for (flat = 1; flat < (FB_BACKLIGHT_LEVELS / 16); ++flat)
		fb_info->bl_curve[flat] = min;

	count = FB_BACKLIGHT_LEVELS * 15 / 16;
	for (i = 0; i < count; ++i)
		fb_info->bl_curve[flat + i] = min + (range * (i + 1) / count);

	mutex_unlock(&fb_info->bl_curve_mutex);
}
EXPORT_SYMBOL_GPL(fb_bl_default_curve);
#endif
