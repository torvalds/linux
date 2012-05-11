/*
 * Helpers for matrix keyboard bindings
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
#include <linux/export.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/input/matrix_keypad.h>


/**
 * matrix_keypad_build_keymap - convert platform keymap into matrix keymap
 * @keymap_data: keymap supplied by the platform code
 * @keymap_name: name of device tree property containing keymap (if device
 *	tree support is enabled).
 * @rows: number of rows in target keymap array
 * @cols: number of cols in target keymap array
 * @keymap: expanded version of keymap that is suitable for use by
 * matrix keyboard driver
 * @input_dev: input devices for which we are setting up the keymap
 *
 * This function converts platform keymap (encoded with KEY() macro) into
 * an array of keycodes that is suitable for using in a standard matrix
 * keyboard driver that uses row and col as indices.
 */
int matrix_keypad_build_keymap(const struct matrix_keymap_data *keymap_data,
			       const char *keymap_name,
			       unsigned int rows, unsigned int cols,
			       unsigned short *keymap,
			       struct input_dev *input_dev)
{
	unsigned int row_shift = get_count_order(cols);
	int i;

	input_dev->keycode = keymap;
	input_dev->keycodesize = sizeof(*keymap);
	input_dev->keycodemax = rows << row_shift;

	__set_bit(EV_KEY, input_dev->evbit);

	for (i = 0; i < keymap_data->keymap_size; i++) {
		unsigned int key = keymap_data->keymap[i];
		unsigned int row = KEY_ROW(key);
		unsigned int col = KEY_COL(key);
		unsigned short code = KEY_VAL(key);

		if (row >= rows || col >= cols) {
			dev_err(input_dev->dev.parent,
				"%s: invalid keymap entry %d (row: %d, col: %d, rows: %d, cols: %d)\n",
				__func__, i, row, col, rows, cols);
			return -EINVAL;
		}

		keymap[MATRIX_SCAN_CODE(row, col, row_shift)] = code;
		__set_bit(code, input_dev->keybit);
	}
	__clear_bit(KEY_RESERVED, input_dev->keybit);

	return 0;
}
EXPORT_SYMBOL(matrix_keypad_build_keymap);

#ifdef CONFIG_OF
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
#endif
