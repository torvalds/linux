/*
 * Copyright 2011 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
/*
 * generic EDID driver
 */

#include <linux/slab.h>
#include <linux/fb.h>
#include "via_aux.h"
#include "../edid.h"


static const char *name = "EDID";


static void query_edid(struct via_aux_drv *drv)
{
	struct fb_monspecs *spec = drv->data;
	unsigned char edid[EDID_LENGTH];
	bool valid = false;

	if (spec) {
		fb_destroy_modedb(spec->modedb);
	} else {
		spec = kmalloc(sizeof(*spec), GFP_KERNEL);
		if (!spec)
			return;
	}

	spec->version = spec->revision = 0;
	if (via_aux_read(drv, 0x00, edid, EDID_LENGTH)) {
		fb_edid_to_monspecs(edid, spec);
		valid = spec->version || spec->revision;
	}

	if (!valid) {
		kfree(spec);
		spec = NULL;
	} else
		printk(KERN_DEBUG "EDID: %s %s\n", spec->manufacturer, spec->monitor);

	drv->data = spec;
}

static const struct fb_videomode *get_preferred_mode(struct via_aux_drv *drv)
{
	struct fb_monspecs *spec = drv->data;
	int i;

	if (!spec || !spec->modedb || !(spec->misc & FB_MISC_1ST_DETAIL))
		return NULL;

	for (i = 0; i < spec->modedb_len; i++) {
		if (spec->modedb[i].flag & FB_MODE_IS_FIRST &&
			spec->modedb[i].flag & FB_MODE_IS_DETAILED)
			return &spec->modedb[i];
	}

	return NULL;
}

static void cleanup(struct via_aux_drv *drv)
{
	struct fb_monspecs *spec = drv->data;

	if (spec)
		fb_destroy_modedb(spec->modedb);
}

void via_aux_edid_probe(struct via_aux_bus *bus)
{
	struct via_aux_drv drv = {
		.bus	=	bus,
		.addr	=	0x50,
		.name	=	name,
		.cleanup	=	cleanup,
		.get_preferred_mode	=	get_preferred_mode};

	query_edid(&drv);

	/* as EDID devices can be connected/disconnected just add the driver */
	via_aux_add(&drv);
}
