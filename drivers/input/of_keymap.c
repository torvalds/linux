/*
 * Helpers for open firmware matrix keyboard bindings
 *
 * Copyright (C) 2012 Google, Inc
 *
 * Author:
 *	Olof Johansson <olof@lixom.net>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/of.h>
#include <linux/input/matrix_keypad.h>
#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/slab.h>

struct matrix_keymap_data *
matrix_keyboard_of_fill_keymap(struct device_node *np,
			       const char *propname)
{
	struct matrix_keymap_data *kd;
	u32 *keymap;
	int proplen, i;
	const __be32 *prop;

	if (!np)
		return NULL;

	if (!propname)
		propname = "linux,keymap";

	prop = of_get_property(np, propname, &proplen);
	if (!prop)
		return NULL;

	if (proplen % sizeof(u32)) {
		pr_warn("Malformed keymap property %s in %s\n",
			propname, np->full_name);
		return NULL;
	}

	kd = kzalloc(sizeof(*kd), GFP_KERNEL);
	if (!kd)
		return NULL;

	kd->keymap = keymap = kzalloc(proplen, GFP_KERNEL);
	if (!kd->keymap) {
		kfree(kd);
		return NULL;
	}

	kd->keymap_size = proplen / sizeof(u32);

	for (i = 0; i < kd->keymap_size; i++) {
		u32 tmp = be32_to_cpup(prop + i);
		int key_code, row, col;

		row = (tmp >> 24) & 0xff;
		col = (tmp >> 16) & 0xff;
		key_code = tmp & 0xffff;
		keymap[i] = KEY(row, col, key_code);
	}

	return kd;
}
EXPORT_SYMBOL_GPL(matrix_keyboard_of_fill_keymap);

void matrix_keyboard_of_free_keymap(const struct matrix_keymap_data *kd)
{
	if (kd) {
		kfree(kd->keymap);
		kfree(kd);
	}
}
EXPORT_SYMBOL_GPL(matrix_keyboard_of_free_keymap);
