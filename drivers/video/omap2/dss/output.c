/*
 * Copyright (C) 2012 Texas Instruments Ltd
 * Author: Archit Taneja <archit@ti.com>
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

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <video/omapdss.h>

#include "dss.h"

static LIST_HEAD(output_list);

void dss_register_output(struct omap_dss_output *out)
{
	list_add_tail(&out->list, &output_list);
}

void dss_unregister_output(struct omap_dss_output *out)
{
	list_del(&out->list);
}

struct omap_dss_output *omap_dss_get_output(enum omap_dss_output_id id)
{
	struct omap_dss_output *out;

	list_for_each_entry(out, &output_list, list) {
		if (out->id == id)
			return out;
	}

	return NULL;
}
