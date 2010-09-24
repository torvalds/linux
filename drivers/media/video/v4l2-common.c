/*
 *	Video for Linux Two
 *
 *	A generic video device interface for the LINUX operating system
 *	using a set of device structures/vectors for low level operations.
 *
 *	This file replaces the videodev.c file that comes with the
 *	regular kernel distribution.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * Author:	Bill Dirks <bill@thedirks.org>
 *		based on code by Alan Cox, <alan@cymru.net>
 *
 */

/*
 * Video capture interface for Linux
 *
 *	A generic video device interface for the LINUX operating system
 *	using a set of device structures/vectors for low level operations.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Author:	Alan Cox, <alan@lxorguk.ukuu.org.uk>
 *
 * Fixes:
 */

/*
 * Video4linux 1/2 integration by Justin Schoeman
 * <justin@suntiger.ee.up.ac.za>
 * 2.4 PROCFS support ported from 2.4 kernels by
 *  Iñaki García Etxebarria <garetxe@euskalnet.net>
 * Makefile fix by "W. Michael Petullo" <mike@flyn.org>
 * 2.4 devfs support ported from 2.4 kernels by
 *  Dan Merillat <dan@merillat.org>
 * Added Gerd Knorrs v4l1 enhancements (Justin Schoeman)
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#if defined(CONFIG_SPI)
#include <linux/spi/spi.h>
#endif
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/io.h>
#include <asm/div64.h>
#define __OLD_VIDIOC_ /* To allow fixing old calls*/
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-chip-ident.h>

#include <linux/videodev2.h>

MODULE_AUTHOR("Bill Dirks, Justin Schoeman, Gerd Knorr");
MODULE_DESCRIPTION("misc helper functions for v4l2 device drivers");
MODULE_LICENSE("GPL");

/*
 *
 *	V 4 L 2   D R I V E R   H E L P E R   A P I
 *
 */

/*
 *  Video Standard Operations (contributed by Michael Schimek)
 */


/* ----------------------------------------------------------------- */
/* priority handling                                                 */

#define V4L2_PRIO_VALID(val) (val == V4L2_PRIORITY_BACKGROUND   || \
			      val == V4L2_PRIORITY_INTERACTIVE  || \
			      val == V4L2_PRIORITY_RECORD)

void v4l2_prio_init(struct v4l2_prio_state *global)
{
	memset(global, 0, sizeof(*global));
}
EXPORT_SYMBOL(v4l2_prio_init);

int v4l2_prio_change(struct v4l2_prio_state *global, enum v4l2_priority *local,
		     enum v4l2_priority new)
{
	if (!V4L2_PRIO_VALID(new))
		return -EINVAL;
	if (*local == new)
		return 0;

	atomic_inc(&global->prios[new]);
	if (V4L2_PRIO_VALID(*local))
		atomic_dec(&global->prios[*local]);
	*local = new;
	return 0;
}
EXPORT_SYMBOL(v4l2_prio_change);

void v4l2_prio_open(struct v4l2_prio_state *global, enum v4l2_priority *local)
{
	v4l2_prio_change(global, local, V4L2_PRIORITY_DEFAULT);
}
EXPORT_SYMBOL(v4l2_prio_open);

void v4l2_prio_close(struct v4l2_prio_state *global, enum v4l2_priority local)
{
	if (V4L2_PRIO_VALID(local))
		atomic_dec(&global->prios[local]);
}
EXPORT_SYMBOL(v4l2_prio_close);

enum v4l2_priority v4l2_prio_max(struct v4l2_prio_state *global)
{
	if (atomic_read(&global->prios[V4L2_PRIORITY_RECORD]) > 0)
		return V4L2_PRIORITY_RECORD;
	if (atomic_read(&global->prios[V4L2_PRIORITY_INTERACTIVE]) > 0)
		return V4L2_PRIORITY_INTERACTIVE;
	if (atomic_read(&global->prios[V4L2_PRIORITY_BACKGROUND]) > 0)
		return V4L2_PRIORITY_BACKGROUND;
	return V4L2_PRIORITY_UNSET;
}
EXPORT_SYMBOL(v4l2_prio_max);

int v4l2_prio_check(struct v4l2_prio_state *global, enum v4l2_priority local)
{
	return (local < v4l2_prio_max(global)) ? -EBUSY : 0;
}
EXPORT_SYMBOL(v4l2_prio_check);

/* ----------------------------------------------------------------- */

/* Helper functions for control handling			     */

/* Check for correctness of the ctrl's value based on the data from
   struct v4l2_queryctrl and the available menu items. Note that
   menu_items may be NULL, in that case it is ignored. */
int v4l2_ctrl_check(struct v4l2_ext_control *ctrl, struct v4l2_queryctrl *qctrl,
		const char **menu_items)
{
	if (qctrl->flags & V4L2_CTRL_FLAG_DISABLED)
		return -EINVAL;
	if (qctrl->flags & V4L2_CTRL_FLAG_GRABBED)
		return -EBUSY;
	if (qctrl->type == V4L2_CTRL_TYPE_STRING)
		return 0;
	if (qctrl->type == V4L2_CTRL_TYPE_BUTTON ||
	    qctrl->type == V4L2_CTRL_TYPE_INTEGER64 ||
	    qctrl->type == V4L2_CTRL_TYPE_CTRL_CLASS)
		return 0;
	if (ctrl->value < qctrl->minimum || ctrl->value > qctrl->maximum)
		return -ERANGE;
	if (qctrl->type == V4L2_CTRL_TYPE_MENU && menu_items != NULL) {
		if (menu_items[ctrl->value] == NULL ||
		    menu_items[ctrl->value][0] == '\0')
			return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_check);

/* Fill in a struct v4l2_queryctrl */
int v4l2_ctrl_query_fill(struct v4l2_queryctrl *qctrl, s32 min, s32 max, s32 step, s32 def)
{
	const char *name;

	v4l2_ctrl_fill(qctrl->id, &name, &qctrl->type,
		       &min, &max, &step, &def, &qctrl->flags);

	if (name == NULL)
		return -EINVAL;

	qctrl->minimum = min;
	qctrl->maximum = max;
	qctrl->step = step;
	qctrl->default_value = def;
	qctrl->reserved[0] = qctrl->reserved[1] = 0;
	strlcpy(qctrl->name, name, sizeof(qctrl->name));
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_query_fill);

/* Fill in a struct v4l2_querymenu based on the struct v4l2_queryctrl and
   the menu. The qctrl pointer may be NULL, in which case it is ignored.
   If menu_items is NULL, then the menu items are retrieved using
   v4l2_ctrl_get_menu. */
int v4l2_ctrl_query_menu(struct v4l2_querymenu *qmenu, struct v4l2_queryctrl *qctrl,
	       const char **menu_items)
{
	int i;

	qmenu->reserved = 0;
	if (menu_items == NULL)
		menu_items = v4l2_ctrl_get_menu(qmenu->id);
	if (menu_items == NULL ||
	    (qctrl && (qmenu->index < qctrl->minimum || qmenu->index > qctrl->maximum)))
		return -EINVAL;
	for (i = 0; i < qmenu->index && menu_items[i]; i++) ;
	if (menu_items[i] == NULL || menu_items[i][0] == '\0')
		return -EINVAL;
	strlcpy(qmenu->name, menu_items[qmenu->index], sizeof(qmenu->name));
	return 0;
}
EXPORT_SYMBOL(v4l2_ctrl_query_menu);

/* Fill in a struct v4l2_querymenu based on the specified array of valid
   menu items (terminated by V4L2_CTRL_MENU_IDS_END).
   Use this if there are 'holes' in the list of valid menu items. */
int v4l2_ctrl_query_menu_valid_items(struct v4l2_querymenu *qmenu, const u32 *ids)
{
	const char **menu_items = v4l2_ctrl_get_menu(qmenu->id);

	qmenu->reserved = 0;
	if (menu_items == NULL || ids == NULL)
		return -EINVAL;
	while (*ids != V4L2_CTRL_MENU_IDS_END) {
		if (*ids++ == qmenu->index) {
			strlcpy(qmenu->name, menu_items[qmenu->index],
					sizeof(qmenu->name));
			return 0;
		}
	}
	return -EINVAL;
}
EXPORT_SYMBOL(v4l2_ctrl_query_menu_valid_items);

/* ctrl_classes points to an array of u32 pointers, the last element is
   a NULL pointer. Each u32 array is a 0-terminated array of control IDs.
   Each array must be sorted low to high and belong to the same control
   class. The array of u32 pointers must also be sorted, from low class IDs
   to high class IDs.

   This function returns the first ID that follows after the given ID.
   When no more controls are available 0 is returned. */
u32 v4l2_ctrl_next(const u32 * const * ctrl_classes, u32 id)
{
	u32 ctrl_class = V4L2_CTRL_ID2CLASS(id);
	const u32 *pctrl;

	if (ctrl_classes == NULL)
		return 0;

	/* if no query is desired, then check if the ID is part of ctrl_classes */
	if ((id & V4L2_CTRL_FLAG_NEXT_CTRL) == 0) {
		/* find class */
		while (*ctrl_classes && V4L2_CTRL_ID2CLASS(**ctrl_classes) != ctrl_class)
			ctrl_classes++;
		if (*ctrl_classes == NULL)
			return 0;
		pctrl = *ctrl_classes;
		/* find control ID */
		while (*pctrl && *pctrl != id) pctrl++;
		return *pctrl ? id : 0;
	}
	id &= V4L2_CTRL_ID_MASK;
	id++;	/* select next control */
	/* find first class that matches (or is greater than) the class of
	   the ID */
	while (*ctrl_classes && V4L2_CTRL_ID2CLASS(**ctrl_classes) < ctrl_class)
		ctrl_classes++;
	/* no more classes */
	if (*ctrl_classes == NULL)
		return 0;
	pctrl = *ctrl_classes;
	/* find first ctrl within the class that is >= ID */
	while (*pctrl && *pctrl < id) pctrl++;
	if (*pctrl)
		return *pctrl;
	/* we are at the end of the controls of the current class. */
	/* continue with next class if available */
	ctrl_classes++;
	if (*ctrl_classes == NULL)
		return 0;
	return **ctrl_classes;
}
EXPORT_SYMBOL(v4l2_ctrl_next);

int v4l2_chip_match_host(const struct v4l2_dbg_match *match)
{
	switch (match->type) {
	case V4L2_CHIP_MATCH_HOST:
		return match->addr == 0;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(v4l2_chip_match_host);

#if defined(CONFIG_I2C) || (defined(CONFIG_I2C_MODULE) && defined(MODULE))
int v4l2_chip_match_i2c_client(struct i2c_client *c, const struct v4l2_dbg_match *match)
{
	int len;

	if (c == NULL || match == NULL)
		return 0;

	switch (match->type) {
	case V4L2_CHIP_MATCH_I2C_DRIVER:
		if (c->driver == NULL || c->driver->driver.name == NULL)
			return 0;
		len = strlen(c->driver->driver.name);
		/* legacy drivers have a ' suffix, don't try to match that */
		if (len && c->driver->driver.name[len - 1] == '\'')
			len--;
		return len && !strncmp(c->driver->driver.name, match->name, len);
	case V4L2_CHIP_MATCH_I2C_ADDR:
		return c->addr == match->addr;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(v4l2_chip_match_i2c_client);

int v4l2_chip_ident_i2c_client(struct i2c_client *c, struct v4l2_dbg_chip_ident *chip,
		u32 ident, u32 revision)
{
	if (!v4l2_chip_match_i2c_client(c, &chip->match))
		return 0;
	if (chip->ident == V4L2_IDENT_NONE) {
		chip->ident = ident;
		chip->revision = revision;
	}
	else {
		chip->ident = V4L2_IDENT_AMBIGUOUS;
		chip->revision = 0;
	}
	return 0;
}
EXPORT_SYMBOL(v4l2_chip_ident_i2c_client);

/* ----------------------------------------------------------------- */

/* I2C Helper functions */


void v4l2_i2c_subdev_init(struct v4l2_subdev *sd, struct i2c_client *client,
		const struct v4l2_subdev_ops *ops)
{
	v4l2_subdev_init(sd, ops);
	sd->flags |= V4L2_SUBDEV_FL_IS_I2C;
	/* the owner is the same as the i2c_client's driver owner */
	sd->owner = client->driver->driver.owner;
	/* i2c_client and v4l2_subdev point to one another */
	v4l2_set_subdevdata(sd, client);
	i2c_set_clientdata(client, sd);
	/* initialize name */
	snprintf(sd->name, sizeof(sd->name), "%s %d-%04x",
		client->driver->driver.name, i2c_adapter_id(client->adapter),
		client->addr);
}
EXPORT_SYMBOL_GPL(v4l2_i2c_subdev_init);



/* Load an i2c sub-device. */
struct v4l2_subdev *v4l2_i2c_new_subdev_board(struct v4l2_device *v4l2_dev,
		struct i2c_adapter *adapter, const char *module_name,
		struct i2c_board_info *info, const unsigned short *probe_addrs)
{
	struct v4l2_subdev *sd = NULL;
	struct i2c_client *client;

	BUG_ON(!v4l2_dev);

	if (module_name)
		request_module(module_name);
	else
		request_module(I2C_MODULE_PREFIX "%s", info->type);

	/* Create the i2c client */
	if (info->addr == 0 && probe_addrs)
		client = i2c_new_probed_device(adapter, info, probe_addrs,
					       NULL);
	else
		client = i2c_new_device(adapter, info);

	/* Note: by loading the module first we are certain that c->driver
	   will be set if the driver was found. If the module was not loaded
	   first, then the i2c core tries to delay-load the module for us,
	   and then c->driver is still NULL until the module is finally
	   loaded. This delay-load mechanism doesn't work if other drivers
	   want to use the i2c device, so explicitly loading the module
	   is the best alternative. */
	if (client == NULL || client->driver == NULL)
		goto error;

	/* Lock the module so we can safely get the v4l2_subdev pointer */
	if (!try_module_get(client->driver->driver.owner))
		goto error;
	sd = i2c_get_clientdata(client);

	/* Register with the v4l2_device which increases the module's
	   use count as well. */
	if (v4l2_device_register_subdev(v4l2_dev, sd))
		sd = NULL;
	/* Decrease the module use count to match the first try_module_get. */
	module_put(client->driver->driver.owner);

	if (sd) {
		/* We return errors from v4l2_subdev_call only if we have the
		   callback as the .s_config is not mandatory */
		int err = v4l2_subdev_call(sd, core, s_config,
				info->irq, info->platform_data);

		if (err && err != -ENOIOCTLCMD) {
			v4l2_device_unregister_subdev(sd);
			sd = NULL;
		}
	}

error:
	/* If we have a client but no subdev, then something went wrong and
	   we must unregister the client. */
	if (client && sd == NULL)
		i2c_unregister_device(client);
	return sd;
}
EXPORT_SYMBOL_GPL(v4l2_i2c_new_subdev_board);

struct v4l2_subdev *v4l2_i2c_new_subdev_cfg(struct v4l2_device *v4l2_dev,
		struct i2c_adapter *adapter,
		const char *module_name, const char *client_type,
		int irq, void *platform_data,
		u8 addr, const unsigned short *probe_addrs)
{
	struct i2c_board_info info;

	/* Setup the i2c board info with the device type and
	   the device address. */
	memset(&info, 0, sizeof(info));
	strlcpy(info.type, client_type, sizeof(info.type));
	info.addr = addr;
	info.irq = irq;
	info.platform_data = platform_data;

	return v4l2_i2c_new_subdev_board(v4l2_dev, adapter, module_name,
			&info, probe_addrs);
}
EXPORT_SYMBOL_GPL(v4l2_i2c_new_subdev_cfg);

/* Return i2c client address of v4l2_subdev. */
unsigned short v4l2_i2c_subdev_addr(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	return client ? client->addr : I2C_CLIENT_END;
}
EXPORT_SYMBOL_GPL(v4l2_i2c_subdev_addr);

/* Return a list of I2C tuner addresses to probe. Use only if the tuner
   addresses are unknown. */
const unsigned short *v4l2_i2c_tuner_addrs(enum v4l2_i2c_tuner_type type)
{
	static const unsigned short radio_addrs[] = {
#if defined(CONFIG_MEDIA_TUNER_TEA5761) || defined(CONFIG_MEDIA_TUNER_TEA5761_MODULE)
		0x10,
#endif
		0x60,
		I2C_CLIENT_END
	};
	static const unsigned short demod_addrs[] = {
		0x42, 0x43, 0x4a, 0x4b,
		I2C_CLIENT_END
	};
	static const unsigned short tv_addrs[] = {
		0x42, 0x43, 0x4a, 0x4b,		/* tda8290 */
		0x60, 0x61, 0x62, 0x63, 0x64,
		I2C_CLIENT_END
	};

	switch (type) {
	case ADDRS_RADIO:
		return radio_addrs;
	case ADDRS_DEMOD:
		return demod_addrs;
	case ADDRS_TV:
		return tv_addrs;
	case ADDRS_TV_WITH_DEMOD:
		return tv_addrs + 4;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(v4l2_i2c_tuner_addrs);

#endif /* defined(CONFIG_I2C) */

#if defined(CONFIG_SPI)

/* Load a spi sub-device. */

void v4l2_spi_subdev_init(struct v4l2_subdev *sd, struct spi_device *spi,
		const struct v4l2_subdev_ops *ops)
{
	v4l2_subdev_init(sd, ops);
	sd->flags |= V4L2_SUBDEV_FL_IS_SPI;
	/* the owner is the same as the spi_device's driver owner */
	sd->owner = spi->dev.driver->owner;
	/* spi_device and v4l2_subdev point to one another */
	v4l2_set_subdevdata(sd, spi);
	spi_set_drvdata(spi, sd);
	/* initialize name */
	strlcpy(sd->name, spi->dev.driver->name, sizeof(sd->name));
}
EXPORT_SYMBOL_GPL(v4l2_spi_subdev_init);

struct v4l2_subdev *v4l2_spi_new_subdev(struct v4l2_device *v4l2_dev,
		struct spi_master *master, struct spi_board_info *info)
{
	struct v4l2_subdev *sd = NULL;
	struct spi_device *spi = NULL;

	BUG_ON(!v4l2_dev);

	if (info->modalias)
		request_module(info->modalias);

	spi = spi_new_device(master, info);

	if (spi == NULL || spi->dev.driver == NULL)
		goto error;

	if (!try_module_get(spi->dev.driver->owner))
		goto error;

	sd = spi_get_drvdata(spi);

	/* Register with the v4l2_device which increases the module's
	   use count as well. */
	if (v4l2_device_register_subdev(v4l2_dev, sd))
		sd = NULL;

	/* Decrease the module use count to match the first try_module_get. */
	module_put(spi->dev.driver->owner);

error:
	/* If we have a client but no subdev, then something went wrong and
	   we must unregister the client. */
	if (spi && sd == NULL)
		spi_unregister_device(spi);

	return sd;
}
EXPORT_SYMBOL_GPL(v4l2_spi_new_subdev);

#endif /* defined(CONFIG_SPI) */

/* Clamp x to be between min and max, aligned to a multiple of 2^align.  min
 * and max don't have to be aligned, but there must be at least one valid
 * value.  E.g., min=17,max=31,align=4 is not allowed as there are no multiples
 * of 16 between 17 and 31.  */
static unsigned int clamp_align(unsigned int x, unsigned int min,
				unsigned int max, unsigned int align)
{
	/* Bits that must be zero to be aligned */
	unsigned int mask = ~((1 << align) - 1);

	/* Round to nearest aligned value */
	if (align)
		x = (x + (1 << (align - 1))) & mask;

	/* Clamp to aligned value of min and max */
	if (x < min)
		x = (min + ~mask) & mask;
	else if (x > max)
		x = max & mask;

	return x;
}

/* Bound an image to have a width between wmin and wmax, and height between
 * hmin and hmax, inclusive.  Additionally, the width will be a multiple of
 * 2^walign, the height will be a multiple of 2^halign, and the overall size
 * (width*height) will be a multiple of 2^salign.  The image may be shrunk
 * or enlarged to fit the alignment constraints.
 *
 * The width or height maximum must not be smaller than the corresponding
 * minimum.  The alignments must not be so high there are no possible image
 * sizes within the allowed bounds.  wmin and hmin must be at least 1
 * (don't use 0).  If you don't care about a certain alignment, specify 0,
 * as 2^0 is 1 and one byte alignment is equivalent to no alignment.  If
 * you only want to adjust downward, specify a maximum that's the same as
 * the initial value.
 */
void v4l_bound_align_image(u32 *w, unsigned int wmin, unsigned int wmax,
			   unsigned int walign,
			   u32 *h, unsigned int hmin, unsigned int hmax,
			   unsigned int halign, unsigned int salign)
{
	*w = clamp_align(*w, wmin, wmax, walign);
	*h = clamp_align(*h, hmin, hmax, halign);

	/* Usually we don't need to align the size and are done now. */
	if (!salign)
		return;

	/* How much alignment do we have? */
	walign = __ffs(*w);
	halign = __ffs(*h);
	/* Enough to satisfy the image alignment? */
	if (walign + halign < salign) {
		/* Max walign where there is still a valid width */
		unsigned int wmaxa = __fls(wmax ^ (wmin - 1));
		/* Max halign where there is still a valid height */
		unsigned int hmaxa = __fls(hmax ^ (hmin - 1));

		/* up the smaller alignment until we have enough */
		do {
			if (halign >= hmaxa ||
			    (walign <= halign && walign < wmaxa)) {
				*w = clamp_align(*w, wmin, wmax, walign + 1);
				walign = __ffs(*w);
			} else {
				*h = clamp_align(*h, hmin, hmax, halign + 1);
				halign = __ffs(*h);
			}
		} while (halign + walign < salign);
	}
}
EXPORT_SYMBOL_GPL(v4l_bound_align_image);

/**
 * v4l_fill_dv_preset_info - fill description of a digital video preset
 * @preset - preset value
 * @info - pointer to struct v4l2_dv_enum_preset
 *
 * drivers can use this helper function to fill description of dv preset
 * in info.
 */
int v4l_fill_dv_preset_info(u32 preset, struct v4l2_dv_enum_preset *info)
{
	static const struct v4l2_dv_preset_info {
		u16 width;
		u16 height;
		const char *name;
	} dv_presets[] = {
		{ 0, 0, "Invalid" },		/* V4L2_DV_INVALID */
		{ 720,  480, "480p@59.94" },	/* V4L2_DV_480P59_94 */
		{ 720,  576, "576p@50" },	/* V4L2_DV_576P50 */
		{ 1280, 720, "720p@24" },	/* V4L2_DV_720P24 */
		{ 1280, 720, "720p@25" },	/* V4L2_DV_720P25 */
		{ 1280, 720, "720p@30" },	/* V4L2_DV_720P30 */
		{ 1280, 720, "720p@50" },	/* V4L2_DV_720P50 */
		{ 1280, 720, "720p@59.94" },	/* V4L2_DV_720P59_94 */
		{ 1280, 720, "720p@60" },	/* V4L2_DV_720P60 */
		{ 1920, 1080, "1080i@29.97" },	/* V4L2_DV_1080I29_97 */
		{ 1920, 1080, "1080i@30" },	/* V4L2_DV_1080I30 */
		{ 1920, 1080, "1080i@25" },	/* V4L2_DV_1080I25 */
		{ 1920, 1080, "1080i@50" },	/* V4L2_DV_1080I50 */
		{ 1920, 1080, "1080i@60" },	/* V4L2_DV_1080I60 */
		{ 1920, 1080, "1080p@24" },	/* V4L2_DV_1080P24 */
		{ 1920, 1080, "1080p@25" },	/* V4L2_DV_1080P25 */
		{ 1920, 1080, "1080p@30" },	/* V4L2_DV_1080P30 */
		{ 1920, 1080, "1080p@50" },	/* V4L2_DV_1080P50 */
		{ 1920, 1080, "1080p@60" },	/* V4L2_DV_1080P60 */
	};

	if (info == NULL || preset >= ARRAY_SIZE(dv_presets))
		return -EINVAL;

	info->preset = preset;
	info->width = dv_presets[preset].width;
	info->height = dv_presets[preset].height;
	strlcpy(info->name, dv_presets[preset].name, sizeof(info->name));
	return 0;
}
EXPORT_SYMBOL_GPL(v4l_fill_dv_preset_info);

const struct v4l2_frmsize_discrete *v4l2_find_nearest_format(
		const struct v4l2_discrete_probe *probe,
		s32 width, s32 height)
{
	int i;
	u32 error, min_error = UINT_MAX;
	const struct v4l2_frmsize_discrete *size, *best = NULL;

	if (!probe)
		return best;

	for (i = 0, size = probe->sizes; i < probe->num_sizes; i++, size++) {
		error = abs(size->width - width) + abs(size->height - height);
		if (error < min_error) {
			min_error = error;
			best = size;
		}
		if (!error)
			break;
	}

	return best;
}
EXPORT_SYMBOL_GPL(v4l2_find_nearest_format);
