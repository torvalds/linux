/*
 * Generic support for sparse keymaps
 *
 * Copyright (c) 2009 Dmitry Torokhov
 *
 * Derived from wistron button driver:
 * Copyright (C) 2005 Miloslav Trmac <mitr@volny.cz>
 * Copyright (C) 2005 Bernhard Rosenkraenzer <bero@arklinux.org>
 * Copyright (C) 2005 Dmitry Torokhov <dtor@mail.ru>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/input.h>
#include <linux/input/sparse-keymap.h>

MODULE_AUTHOR("Dmitry Torokhov <dtor@mail.ru>");
MODULE_DESCRIPTION("Generic support for sparse keymaps");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.1");

/**
 * sparse_keymap_entry_from_scancode - perform sparse keymap lookup
 * @dev: Input device using sparse keymap
 * @code: Scan code
 *
 * This function is used to perform &struct key_entry lookup in an
 * input device using sparse keymap.
 */
struct key_entry *sparse_keymap_entry_from_scancode(struct input_dev *dev,
						    unsigned int code)
{
	struct key_entry *key;

	for (key = dev->keycode; key->type != KE_END; key++)
		if (code == key->code)
			return key;

	return NULL;
}
EXPORT_SYMBOL(sparse_keymap_entry_from_scancode);

/**
 * sparse_keymap_entry_from_keycode - perform sparse keymap lookup
 * @dev: Input device using sparse keymap
 * @keycode: Key code
 *
 * This function is used to perform &struct key_entry lookup in an
 * input device using sparse keymap.
 */
struct key_entry *sparse_keymap_entry_from_keycode(struct input_dev *dev,
						   unsigned int keycode)
{
	struct key_entry *key;

	for (key = dev->keycode; key->type != KE_END; key++)
		if (key->type == KE_KEY && keycode == key->keycode)
			return key;

	return NULL;
}
EXPORT_SYMBOL(sparse_keymap_entry_from_keycode);

static int sparse_keymap_getkeycode(struct input_dev *dev,
				    int scancode, int *keycode)
{
	const struct key_entry *key =
			sparse_keymap_entry_from_scancode(dev, scancode);

	if (key && key->type == KE_KEY) {
		*keycode = key->keycode;
		return 0;
	}

	return -EINVAL;
}

static int sparse_keymap_setkeycode(struct input_dev *dev,
				    int scancode, int keycode)
{
	struct key_entry *key;
	int old_keycode;

	if (keycode < 0 || keycode > KEY_MAX)
		return -EINVAL;

	key = sparse_keymap_entry_from_scancode(dev, scancode);
	if (key && key->type == KE_KEY) {
		old_keycode = key->keycode;
		key->keycode = keycode;
		set_bit(keycode, dev->keybit);
		if (!sparse_keymap_entry_from_keycode(dev, old_keycode))
			clear_bit(old_keycode, dev->keybit);
		return 0;
	}

	return -EINVAL;
}

/**
 * sparse_keymap_setup - set up sparse keymap for an input device
 * @dev: Input device
 * @keymap: Keymap in form of array of &key_entry structures ending
 *	with %KE_END type entry
 * @setup: Function that can be used to adjust keymap entries
 *	depending on device's deeds, may be %NULL
 *
 * The function calculates size and allocates copy of the original
 * keymap after which sets up input device event bits appropriately.
 * Before destroying input device allocated keymap should be freed
 * with a call to sparse_keymap_free().
 */
int sparse_keymap_setup(struct input_dev *dev,
			const struct key_entry *keymap,
			int (*setup)(struct input_dev *, struct key_entry *))
{
	size_t map_size = 1; /* to account for the last KE_END entry */
	const struct key_entry *e;
	struct key_entry *map, *entry;
	int i;
	int error;

	for (e = keymap; e->type != KE_END; e++)
		map_size++;

	map = kcalloc(map_size, sizeof (struct key_entry), GFP_KERNEL);
	if (!map)
		return -ENOMEM;

	memcpy(map, keymap, map_size * sizeof (struct key_entry));

	for (i = 0; i < map_size; i++) {
		entry = &map[i];

		if (setup) {
			error = setup(dev, entry);
			if (error)
				goto err_out;
		}

		switch (entry->type) {
		case KE_KEY:
			__set_bit(EV_KEY, dev->evbit);
			__set_bit(entry->keycode, dev->keybit);
			break;

		case KE_SW:
			__set_bit(EV_SW, dev->evbit);
			__set_bit(entry->sw.code, dev->swbit);
			break;
		}
	}

	dev->keycode = map;
	dev->keycodemax = map_size;
	dev->getkeycode = sparse_keymap_getkeycode;
	dev->setkeycode = sparse_keymap_setkeycode;

	return 0;

 err_out:
	kfree(keymap);
	return error;

}
EXPORT_SYMBOL(sparse_keymap_setup);

/**
 * sparse_keymap_free - free memory allocated for sparse keymap
 * @dev: Input device using sparse keymap
 *
 * This function is used to free memory allocated by sparse keymap
 * in an input device that was set up by sparse_keymap_setup().
 */
void sparse_keymap_free(struct input_dev *dev)
{
	kfree(dev->keycode);
	dev->keycode = NULL;
	dev->keycodemax = 0;
	dev->getkeycode = NULL;
	dev->setkeycode = NULL;
}
EXPORT_SYMBOL(sparse_keymap_free);

/**
 * sparse_keymap_report_entry - report event corresponding to given key entry
 * @dev: Input device for which event should be reported
 * @ke: key entry describing event
 * @value: Value that should be reported (ignored by %KE_SW entries)
 * @autorelease: Signals whether release event should be emitted for %KE_KEY
 *	entries right after reporting press event, ignored by all other
 *	entries
 *
 * This function is used to report input event described by given
 * &struct key_entry.
 */
void sparse_keymap_report_entry(struct input_dev *dev, const struct key_entry *ke,
				unsigned int value, bool autorelease)
{
	switch (ke->type) {
	case KE_KEY:
		input_report_key(dev, ke->keycode, value);
		input_sync(dev);
		if (value && autorelease) {
			input_report_key(dev, ke->keycode, 0);
			input_sync(dev);
		}
		break;

	case KE_SW:
		value = ke->sw.value;
		/* fall through */

	case KE_VSW:
		input_report_switch(dev, ke->sw.code, value);
		break;
	}
}
EXPORT_SYMBOL(sparse_keymap_report_entry);

/**
 * sparse_keymap_report_event - report event corresponding to given scancode
 * @dev: Input device using sparse keymap
 * @code: Scan code
 * @value: Value that should be reported (ignored by %KE_SW entries)
 * @autorelease: Signals whether release event should be emitted for %KE_KEY
 *	entries right after reporting press event, ignored by all other
 *	entries
 *
 * This function is used to perform lookup in an input device using sparse
 * keymap and report corresponding event. Returns %true if lookup was
 * successful and %false otherwise.
 */
bool sparse_keymap_report_event(struct input_dev *dev, unsigned int code,
				unsigned int value, bool autorelease)
{
	const struct key_entry *ke =
		sparse_keymap_entry_from_scancode(dev, code);

	if (ke) {
		sparse_keymap_report_entry(dev, ke, value, autorelease);
		return true;
	}

	return false;
}
EXPORT_SYMBOL(sparse_keymap_report_event);

