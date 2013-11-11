/*
 * Copyright (C) 2012, Alejandro Mery <amery@geeks.cl>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#define pr_fmt(fmt)	"sunxi: script: " fmt

#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>

#include <plat/script.h>

const struct sunxi_script *sunxi_script_base = NULL;
EXPORT_SYMBOL(sunxi_script_base);

void sunxi_script_init(const struct sunxi_script *base)
{
	sunxi_script_base = base;
	pr_debug("base: 0x%p\n", base);
	pr_debug("version: %u.%u.%u count: %u\n",
		base->version[0], base->version[1], base->version[2],
		base->count);
}
EXPORT_SYMBOL(sunxi_script_init);

const struct sunxi_property *sunxi_find_property_fmt(
		const struct sunxi_section *sp,
		const char *fmt, ...)
{
	const struct sunxi_property *prop;
	char name[sizeof(prop->name)];
	va_list args;

	va_start(args, fmt);
	vsprintf(name, fmt, args);
	va_end(args);

	prop = sunxi_find_property(sp, name);
	return prop;
}
EXPORT_SYMBOL_GPL(sunxi_find_property_fmt);
