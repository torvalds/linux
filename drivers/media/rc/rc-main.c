/* rc-main.c - Remote Controller core module
 *
 * Copyright (C) 2009-2010 by Mauro Carvalho Chehab
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <media/rc-core.h>
#include <linux/bsearch.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <linux/device.h>
#include <linux/module.h>
#include "rc-core-priv.h"

/* Sizes are in bytes, 256 bytes allows for 32 entries on x64 */
#define IR_TAB_MIN_SIZE	256
#define IR_TAB_MAX_SIZE	8192
#define RC_DEV_MAX	256

static const struct {
	const char *name;
	unsigned int repeat_period;
	unsigned int scancode_bits;
} protocols[] = {
	[RC_PROTO_UNKNOWN] = { .name = "unknown", .repeat_period = 250 },
	[RC_PROTO_OTHER] = { .name = "other", .repeat_period = 250 },
	[RC_PROTO_RC5] = { .name = "rc-5",
		.scancode_bits = 0x1f7f, .repeat_period = 164 },
	[RC_PROTO_RC5X_20] = { .name = "rc-5x-20",
		.scancode_bits = 0x1f7f3f, .repeat_period = 164 },
	[RC_PROTO_RC5_SZ] = { .name = "rc-5-sz",
		.scancode_bits = 0x2fff, .repeat_period = 164 },
	[RC_PROTO_JVC] = { .name = "jvc",
		.scancode_bits = 0xffff, .repeat_period = 250 },
	[RC_PROTO_SONY12] = { .name = "sony-12",
		.scancode_bits = 0x1f007f, .repeat_period = 100 },
	[RC_PROTO_SONY15] = { .name = "sony-15",
		.scancode_bits = 0xff007f, .repeat_period = 100 },
	[RC_PROTO_SONY20] = { .name = "sony-20",
		.scancode_bits = 0x1fff7f, .repeat_period = 100 },
	[RC_PROTO_NEC] = { .name = "nec",
		.scancode_bits = 0xffff, .repeat_period = 160 },
	[RC_PROTO_NECX] = { .name = "nec-x",
		.scancode_bits = 0xffffff, .repeat_period = 160 },
	[RC_PROTO_NEC32] = { .name = "nec-32",
		.scancode_bits = 0xffffffff, .repeat_period = 160 },
	[RC_PROTO_SANYO] = { .name = "sanyo",
		.scancode_bits = 0x1fffff, .repeat_period = 250 },
	[RC_PROTO_MCIR2_KBD] = { .name = "mcir2-kbd",
		.scancode_bits = 0xffff, .repeat_period = 150 },
	[RC_PROTO_MCIR2_MSE] = { .name = "mcir2-mse",
		.scancode_bits = 0x1fffff, .repeat_period = 150 },
	[RC_PROTO_RC6_0] = { .name = "rc-6-0",
		.scancode_bits = 0xffff, .repeat_period = 164 },
	[RC_PROTO_RC6_6A_20] = { .name = "rc-6-6a-20",
		.scancode_bits = 0xfffff, .repeat_period = 164 },
	[RC_PROTO_RC6_6A_24] = { .name = "rc-6-6a-24",
		.scancode_bits = 0xffffff, .repeat_period = 164 },
	[RC_PROTO_RC6_6A_32] = { .name = "rc-6-6a-32",
		.scancode_bits = 0xffffffff, .repeat_period = 164 },
	[RC_PROTO_RC6_MCE] = { .name = "rc-6-mce",
		.scancode_bits = 0xffff7fff, .repeat_period = 164 },
	[RC_PROTO_SHARP] = { .name = "sharp",
		.scancode_bits = 0x1fff, .repeat_period = 250 },
	[RC_PROTO_XMP] = { .name = "xmp", .repeat_period = 250 },
	[RC_PROTO_CEC] = { .name = "cec", .repeat_period = 550 },
};

/* Used to keep track of known keymaps */
static LIST_HEAD(rc_map_list);
static DEFINE_SPINLOCK(rc_map_lock);
static struct led_trigger *led_feedback;

/* Used to keep track of rc devices */
static DEFINE_IDA(rc_ida);

static struct rc_map_list *seek_rc_map(const char *name)
{
	struct rc_map_list *map = NULL;

	spin_lock(&rc_map_lock);
	list_for_each_entry(map, &rc_map_list, list) {
		if (!strcmp(name, map->map.name)) {
			spin_unlock(&rc_map_lock);
			return map;
		}
	}
	spin_unlock(&rc_map_lock);

	return NULL;
}

struct rc_map *rc_map_get(const char *name)
{

	struct rc_map_list *map;

	map = seek_rc_map(name);
#ifdef CONFIG_MODULES
	if (!map) {
		int rc = request_module("%s", name);
		if (rc < 0) {
			pr_err("Couldn't load IR keymap %s\n", name);
			return NULL;
		}
		msleep(20);	/* Give some time for IR to register */

		map = seek_rc_map(name);
	}
#endif
	if (!map) {
		pr_err("IR keymap %s not found\n", name);
		return NULL;
	}

	printk(KERN_INFO "Registered IR keymap %s\n", map->map.name);

	return &map->map;
}
EXPORT_SYMBOL_GPL(rc_map_get);

int rc_map_register(struct rc_map_list *map)
{
	spin_lock(&rc_map_lock);
	list_add_tail(&map->list, &rc_map_list);
	spin_unlock(&rc_map_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(rc_map_register);

void rc_map_unregister(struct rc_map_list *map)
{
	spin_lock(&rc_map_lock);
	list_del(&map->list);
	spin_unlock(&rc_map_lock);
}
EXPORT_SYMBOL_GPL(rc_map_unregister);


static struct rc_map_table empty[] = {
	{ 0x2a, KEY_COFFEE },
};

static struct rc_map_list empty_map = {
	.map = {
		.scan     = empty,
		.size     = ARRAY_SIZE(empty),
		.rc_proto = RC_PROTO_UNKNOWN,	/* Legacy IR type */
		.name     = RC_MAP_EMPTY,
	}
};

/**
 * ir_create_table() - initializes a scancode table
 * @rc_map:	the rc_map to initialize
 * @name:	name to assign to the table
 * @rc_proto:	ir type to assign to the new table
 * @size:	initial size of the table
 * @return:	zero on success or a negative error code
 *
 * This routine will initialize the rc_map and will allocate
 * memory to hold at least the specified number of elements.
 */
static int ir_create_table(struct rc_map *rc_map,
			   const char *name, u64 rc_proto, size_t size)
{
	rc_map->name = kstrdup(name, GFP_KERNEL);
	if (!rc_map->name)
		return -ENOMEM;
	rc_map->rc_proto = rc_proto;
	rc_map->alloc = roundup_pow_of_two(size * sizeof(struct rc_map_table));
	rc_map->size = rc_map->alloc / sizeof(struct rc_map_table);
	rc_map->scan = kmalloc(rc_map->alloc, GFP_KERNEL);
	if (!rc_map->scan) {
		kfree(rc_map->name);
		rc_map->name = NULL;
		return -ENOMEM;
	}

	IR_dprintk(1, "Allocated space for %u keycode entries (%u bytes)\n",
		   rc_map->size, rc_map->alloc);
	return 0;
}

/**
 * ir_free_table() - frees memory allocated by a scancode table
 * @rc_map:	the table whose mappings need to be freed
 *
 * This routine will free memory alloctaed for key mappings used by given
 * scancode table.
 */
static void ir_free_table(struct rc_map *rc_map)
{
	rc_map->size = 0;
	kfree(rc_map->name);
	rc_map->name = NULL;
	kfree(rc_map->scan);
	rc_map->scan = NULL;
}

/**
 * ir_resize_table() - resizes a scancode table if necessary
 * @rc_map:	the rc_map to resize
 * @gfp_flags:	gfp flags to use when allocating memory
 * @return:	zero on success or a negative error code
 *
 * This routine will shrink the rc_map if it has lots of
 * unused entries and grow it if it is full.
 */
static int ir_resize_table(struct rc_map *rc_map, gfp_t gfp_flags)
{
	unsigned int oldalloc = rc_map->alloc;
	unsigned int newalloc = oldalloc;
	struct rc_map_table *oldscan = rc_map->scan;
	struct rc_map_table *newscan;

	if (rc_map->size == rc_map->len) {
		/* All entries in use -> grow keytable */
		if (rc_map->alloc >= IR_TAB_MAX_SIZE)
			return -ENOMEM;

		newalloc *= 2;
		IR_dprintk(1, "Growing table to %u bytes\n", newalloc);
	}

	if ((rc_map->len * 3 < rc_map->size) && (oldalloc > IR_TAB_MIN_SIZE)) {
		/* Less than 1/3 of entries in use -> shrink keytable */
		newalloc /= 2;
		IR_dprintk(1, "Shrinking table to %u bytes\n", newalloc);
	}

	if (newalloc == oldalloc)
		return 0;

	newscan = kmalloc(newalloc, gfp_flags);
	if (!newscan) {
		IR_dprintk(1, "Failed to kmalloc %u bytes\n", newalloc);
		return -ENOMEM;
	}

	memcpy(newscan, rc_map->scan, rc_map->len * sizeof(struct rc_map_table));
	rc_map->scan = newscan;
	rc_map->alloc = newalloc;
	rc_map->size = rc_map->alloc / sizeof(struct rc_map_table);
	kfree(oldscan);
	return 0;
}

/**
 * ir_update_mapping() - set a keycode in the scancode->keycode table
 * @dev:	the struct rc_dev device descriptor
 * @rc_map:	scancode table to be adjusted
 * @index:	index of the mapping that needs to be updated
 * @keycode:	the desired keycode
 * @return:	previous keycode assigned to the mapping
 *
 * This routine is used to update scancode->keycode mapping at given
 * position.
 */
static unsigned int ir_update_mapping(struct rc_dev *dev,
				      struct rc_map *rc_map,
				      unsigned int index,
				      unsigned int new_keycode)
{
	int old_keycode = rc_map->scan[index].keycode;
	int i;

	/* Did the user wish to remove the mapping? */
	if (new_keycode == KEY_RESERVED || new_keycode == KEY_UNKNOWN) {
		IR_dprintk(1, "#%d: Deleting scan 0x%04x\n",
			   index, rc_map->scan[index].scancode);
		rc_map->len--;
		memmove(&rc_map->scan[index], &rc_map->scan[index+ 1],
			(rc_map->len - index) * sizeof(struct rc_map_table));
	} else {
		IR_dprintk(1, "#%d: %s scan 0x%04x with key 0x%04x\n",
			   index,
			   old_keycode == KEY_RESERVED ? "New" : "Replacing",
			   rc_map->scan[index].scancode, new_keycode);
		rc_map->scan[index].keycode = new_keycode;
		__set_bit(new_keycode, dev->input_dev->keybit);
	}

	if (old_keycode != KEY_RESERVED) {
		/* A previous mapping was updated... */
		__clear_bit(old_keycode, dev->input_dev->keybit);
		/* ... but another scancode might use the same keycode */
		for (i = 0; i < rc_map->len; i++) {
			if (rc_map->scan[i].keycode == old_keycode) {
				__set_bit(old_keycode, dev->input_dev->keybit);
				break;
			}
		}

		/* Possibly shrink the keytable, failure is not a problem */
		ir_resize_table(rc_map, GFP_ATOMIC);
	}

	return old_keycode;
}

/**
 * ir_establish_scancode() - set a keycode in the scancode->keycode table
 * @dev:	the struct rc_dev device descriptor
 * @rc_map:	scancode table to be searched
 * @scancode:	the desired scancode
 * @resize:	controls whether we allowed to resize the table to
 *		accommodate not yet present scancodes
 * @return:	index of the mapping containing scancode in question
 *		or -1U in case of failure.
 *
 * This routine is used to locate given scancode in rc_map.
 * If scancode is not yet present the routine will allocate a new slot
 * for it.
 */
static unsigned int ir_establish_scancode(struct rc_dev *dev,
					  struct rc_map *rc_map,
					  unsigned int scancode,
					  bool resize)
{
	unsigned int i;

	/*
	 * Unfortunately, some hardware-based IR decoders don't provide
	 * all bits for the complete IR code. In general, they provide only
	 * the command part of the IR code. Yet, as it is possible to replace
	 * the provided IR with another one, it is needed to allow loading
	 * IR tables from other remotes. So, we support specifying a mask to
	 * indicate the valid bits of the scancodes.
	 */
	if (dev->scancode_mask)
		scancode &= dev->scancode_mask;

	/* First check if we already have a mapping for this ir command */
	for (i = 0; i < rc_map->len; i++) {
		if (rc_map->scan[i].scancode == scancode)
			return i;

		/* Keytable is sorted from lowest to highest scancode */
		if (rc_map->scan[i].scancode >= scancode)
			break;
	}

	/* No previous mapping found, we might need to grow the table */
	if (rc_map->size == rc_map->len) {
		if (!resize || ir_resize_table(rc_map, GFP_ATOMIC))
			return -1U;
	}

	/* i is the proper index to insert our new keycode */
	if (i < rc_map->len)
		memmove(&rc_map->scan[i + 1], &rc_map->scan[i],
			(rc_map->len - i) * sizeof(struct rc_map_table));
	rc_map->scan[i].scancode = scancode;
	rc_map->scan[i].keycode = KEY_RESERVED;
	rc_map->len++;

	return i;
}

/**
 * ir_setkeycode() - set a keycode in the scancode->keycode table
 * @idev:	the struct input_dev device descriptor
 * @scancode:	the desired scancode
 * @keycode:	result
 * @return:	-EINVAL if the keycode could not be inserted, otherwise zero.
 *
 * This routine is used to handle evdev EVIOCSKEY ioctl.
 */
static int ir_setkeycode(struct input_dev *idev,
			 const struct input_keymap_entry *ke,
			 unsigned int *old_keycode)
{
	struct rc_dev *rdev = input_get_drvdata(idev);
	struct rc_map *rc_map = &rdev->rc_map;
	unsigned int index;
	unsigned int scancode;
	int retval = 0;
	unsigned long flags;

	spin_lock_irqsave(&rc_map->lock, flags);

	if (ke->flags & INPUT_KEYMAP_BY_INDEX) {
		index = ke->index;
		if (index >= rc_map->len) {
			retval = -EINVAL;
			goto out;
		}
	} else {
		retval = input_scancode_to_scalar(ke, &scancode);
		if (retval)
			goto out;

		index = ir_establish_scancode(rdev, rc_map, scancode, true);
		if (index >= rc_map->len) {
			retval = -ENOMEM;
			goto out;
		}
	}

	*old_keycode = ir_update_mapping(rdev, rc_map, index, ke->keycode);

out:
	spin_unlock_irqrestore(&rc_map->lock, flags);
	return retval;
}

/**
 * ir_setkeytable() - sets several entries in the scancode->keycode table
 * @dev:	the struct rc_dev device descriptor
 * @to:		the struct rc_map to copy entries to
 * @from:	the struct rc_map to copy entries from
 * @return:	-ENOMEM if all keycodes could not be inserted, otherwise zero.
 *
 * This routine is used to handle table initialization.
 */
static int ir_setkeytable(struct rc_dev *dev,
			  const struct rc_map *from)
{
	struct rc_map *rc_map = &dev->rc_map;
	unsigned int i, index;
	int rc;

	rc = ir_create_table(rc_map, from->name,
			     from->rc_proto, from->size);
	if (rc)
		return rc;

	for (i = 0; i < from->size; i++) {
		index = ir_establish_scancode(dev, rc_map,
					      from->scan[i].scancode, false);
		if (index >= rc_map->len) {
			rc = -ENOMEM;
			break;
		}

		ir_update_mapping(dev, rc_map, index,
				  from->scan[i].keycode);
	}

	if (rc)
		ir_free_table(rc_map);

	return rc;
}

static int rc_map_cmp(const void *key, const void *elt)
{
	const unsigned int *scancode = key;
	const struct rc_map_table *e = elt;

	if (*scancode < e->scancode)
		return -1;
	else if (*scancode > e->scancode)
		return 1;
	return 0;
}

/**
 * ir_lookup_by_scancode() - locate mapping by scancode
 * @rc_map:	the struct rc_map to search
 * @scancode:	scancode to look for in the table
 * @return:	index in the table, -1U if not found
 *
 * This routine performs binary search in RC keykeymap table for
 * given scancode.
 */
static unsigned int ir_lookup_by_scancode(const struct rc_map *rc_map,
					  unsigned int scancode)
{
	struct rc_map_table *res;

	res = bsearch(&scancode, rc_map->scan, rc_map->len,
		      sizeof(struct rc_map_table), rc_map_cmp);
	if (!res)
		return -1U;
	else
		return res - rc_map->scan;
}

/**
 * ir_getkeycode() - get a keycode from the scancode->keycode table
 * @idev:	the struct input_dev device descriptor
 * @scancode:	the desired scancode
 * @keycode:	used to return the keycode, if found, or KEY_RESERVED
 * @return:	always returns zero.
 *
 * This routine is used to handle evdev EVIOCGKEY ioctl.
 */
static int ir_getkeycode(struct input_dev *idev,
			 struct input_keymap_entry *ke)
{
	struct rc_dev *rdev = input_get_drvdata(idev);
	struct rc_map *rc_map = &rdev->rc_map;
	struct rc_map_table *entry;
	unsigned long flags;
	unsigned int index;
	unsigned int scancode;
	int retval;

	spin_lock_irqsave(&rc_map->lock, flags);

	if (ke->flags & INPUT_KEYMAP_BY_INDEX) {
		index = ke->index;
	} else {
		retval = input_scancode_to_scalar(ke, &scancode);
		if (retval)
			goto out;

		index = ir_lookup_by_scancode(rc_map, scancode);
	}

	if (index < rc_map->len) {
		entry = &rc_map->scan[index];

		ke->index = index;
		ke->keycode = entry->keycode;
		ke->len = sizeof(entry->scancode);
		memcpy(ke->scancode, &entry->scancode, sizeof(entry->scancode));

	} else if (!(ke->flags & INPUT_KEYMAP_BY_INDEX)) {
		/*
		 * We do not really know the valid range of scancodes
		 * so let's respond with KEY_RESERVED to anything we
		 * do not have mapping for [yet].
		 */
		ke->index = index;
		ke->keycode = KEY_RESERVED;
	} else {
		retval = -EINVAL;
		goto out;
	}

	retval = 0;

out:
	spin_unlock_irqrestore(&rc_map->lock, flags);
	return retval;
}

/**
 * rc_g_keycode_from_table() - gets the keycode that corresponds to a scancode
 * @dev:	the struct rc_dev descriptor of the device
 * @scancode:	the scancode to look for
 * @return:	the corresponding keycode, or KEY_RESERVED
 *
 * This routine is used by drivers which need to convert a scancode to a
 * keycode. Normally it should not be used since drivers should have no
 * interest in keycodes.
 */
u32 rc_g_keycode_from_table(struct rc_dev *dev, u32 scancode)
{
	struct rc_map *rc_map = &dev->rc_map;
	unsigned int keycode;
	unsigned int index;
	unsigned long flags;

	spin_lock_irqsave(&rc_map->lock, flags);

	index = ir_lookup_by_scancode(rc_map, scancode);
	keycode = index < rc_map->len ?
			rc_map->scan[index].keycode : KEY_RESERVED;

	spin_unlock_irqrestore(&rc_map->lock, flags);

	if (keycode != KEY_RESERVED)
		IR_dprintk(1, "%s: scancode 0x%04x keycode 0x%02x\n",
			   dev->device_name, scancode, keycode);

	return keycode;
}
EXPORT_SYMBOL_GPL(rc_g_keycode_from_table);

/**
 * ir_do_keyup() - internal function to signal the release of a keypress
 * @dev:	the struct rc_dev descriptor of the device
 * @sync:	whether or not to call input_sync
 *
 * This function is used internally to release a keypress, it must be
 * called with keylock held.
 */
static void ir_do_keyup(struct rc_dev *dev, bool sync)
{
	if (!dev->keypressed)
		return;

	IR_dprintk(1, "keyup key 0x%04x\n", dev->last_keycode);
	input_report_key(dev->input_dev, dev->last_keycode, 0);
	led_trigger_event(led_feedback, LED_OFF);
	if (sync)
		input_sync(dev->input_dev);
	dev->keypressed = false;
}

/**
 * rc_keyup() - signals the release of a keypress
 * @dev:	the struct rc_dev descriptor of the device
 *
 * This routine is used to signal that a key has been released on the
 * remote control.
 */
void rc_keyup(struct rc_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->keylock, flags);
	ir_do_keyup(dev, true);
	spin_unlock_irqrestore(&dev->keylock, flags);
}
EXPORT_SYMBOL_GPL(rc_keyup);

/**
 * ir_timer_keyup() - generates a keyup event after a timeout
 * @cookie:	a pointer to the struct rc_dev for the device
 *
 * This routine will generate a keyup event some time after a keydown event
 * is generated when no further activity has been detected.
 */
static void ir_timer_keyup(struct timer_list *t)
{
	struct rc_dev *dev = from_timer(dev, t, timer_keyup);
	unsigned long flags;

	/*
	 * ir->keyup_jiffies is used to prevent a race condition if a
	 * hardware interrupt occurs at this point and the keyup timer
	 * event is moved further into the future as a result.
	 *
	 * The timer will then be reactivated and this function called
	 * again in the future. We need to exit gracefully in that case
	 * to allow the input subsystem to do its auto-repeat magic or
	 * a keyup event might follow immediately after the keydown.
	 */
	spin_lock_irqsave(&dev->keylock, flags);
	if (time_is_before_eq_jiffies(dev->keyup_jiffies))
		ir_do_keyup(dev, true);
	spin_unlock_irqrestore(&dev->keylock, flags);
}

/**
 * rc_repeat() - signals that a key is still pressed
 * @dev:	the struct rc_dev descriptor of the device
 *
 * This routine is used by IR decoders when a repeat message which does
 * not include the necessary bits to reproduce the scancode has been
 * received.
 */
void rc_repeat(struct rc_dev *dev)
{
	unsigned long flags;
	unsigned int timeout = protocols[dev->last_protocol].repeat_period;

	spin_lock_irqsave(&dev->keylock, flags);

	if (!dev->keypressed)
		goto out;

	input_event(dev->input_dev, EV_MSC, MSC_SCAN, dev->last_scancode);
	input_sync(dev->input_dev);

	dev->keyup_jiffies = jiffies + msecs_to_jiffies(timeout);
	mod_timer(&dev->timer_keyup, dev->keyup_jiffies);

out:
	spin_unlock_irqrestore(&dev->keylock, flags);
}
EXPORT_SYMBOL_GPL(rc_repeat);

/**
 * ir_do_keydown() - internal function to process a keypress
 * @dev:	the struct rc_dev descriptor of the device
 * @protocol:	the protocol of the keypress
 * @scancode:   the scancode of the keypress
 * @keycode:    the keycode of the keypress
 * @toggle:     the toggle value of the keypress
 *
 * This function is used internally to register a keypress, it must be
 * called with keylock held.
 */
static void ir_do_keydown(struct rc_dev *dev, enum rc_proto protocol,
			  u32 scancode, u32 keycode, u8 toggle)
{
	bool new_event = (!dev->keypressed		 ||
			  dev->last_protocol != protocol ||
			  dev->last_scancode != scancode ||
			  dev->last_toggle   != toggle);

	if (new_event && dev->keypressed)
		ir_do_keyup(dev, false);

	input_event(dev->input_dev, EV_MSC, MSC_SCAN, scancode);

	if (new_event && keycode != KEY_RESERVED) {
		/* Register a keypress */
		dev->keypressed = true;
		dev->last_protocol = protocol;
		dev->last_scancode = scancode;
		dev->last_toggle = toggle;
		dev->last_keycode = keycode;

		IR_dprintk(1, "%s: key down event, key 0x%04x, protocol 0x%04x, scancode 0x%08x\n",
			   dev->device_name, keycode, protocol, scancode);
		input_report_key(dev->input_dev, keycode, 1);

		led_trigger_event(led_feedback, LED_FULL);
	}

	input_sync(dev->input_dev);
}

/**
 * rc_keydown() - generates input event for a key press
 * @dev:	the struct rc_dev descriptor of the device
 * @protocol:	the protocol for the keypress
 * @scancode:	the scancode for the keypress
 * @toggle:     the toggle value (protocol dependent, if the protocol doesn't
 *              support toggle values, this should be set to zero)
 *
 * This routine is used to signal that a key has been pressed on the
 * remote control.
 */
void rc_keydown(struct rc_dev *dev, enum rc_proto protocol, u32 scancode,
		u8 toggle)
{
	unsigned long flags;
	u32 keycode = rc_g_keycode_from_table(dev, scancode);

	spin_lock_irqsave(&dev->keylock, flags);
	ir_do_keydown(dev, protocol, scancode, keycode, toggle);

	if (dev->keypressed) {
		dev->keyup_jiffies = jiffies +
			msecs_to_jiffies(protocols[protocol].repeat_period);
		mod_timer(&dev->timer_keyup, dev->keyup_jiffies);
	}
	spin_unlock_irqrestore(&dev->keylock, flags);
}
EXPORT_SYMBOL_GPL(rc_keydown);

/**
 * rc_keydown_notimeout() - generates input event for a key press without
 *                          an automatic keyup event at a later time
 * @dev:	the struct rc_dev descriptor of the device
 * @protocol:	the protocol for the keypress
 * @scancode:	the scancode for the keypress
 * @toggle:     the toggle value (protocol dependent, if the protocol doesn't
 *              support toggle values, this should be set to zero)
 *
 * This routine is used to signal that a key has been pressed on the
 * remote control. The driver must manually call rc_keyup() at a later stage.
 */
void rc_keydown_notimeout(struct rc_dev *dev, enum rc_proto protocol,
			  u32 scancode, u8 toggle)
{
	unsigned long flags;
	u32 keycode = rc_g_keycode_from_table(dev, scancode);

	spin_lock_irqsave(&dev->keylock, flags);
	ir_do_keydown(dev, protocol, scancode, keycode, toggle);
	spin_unlock_irqrestore(&dev->keylock, flags);
}
EXPORT_SYMBOL_GPL(rc_keydown_notimeout);

/**
 * rc_validate_filter() - checks that the scancode and mask are valid and
 *			  provides sensible defaults
 * @dev:	the struct rc_dev descriptor of the device
 * @filter:	the scancode and mask
 * @return:	0 or -EINVAL if the filter is not valid
 */
static int rc_validate_filter(struct rc_dev *dev,
			      struct rc_scancode_filter *filter)
{
	u32 mask, s = filter->data;
	enum rc_proto protocol = dev->wakeup_protocol;

	if (protocol >= ARRAY_SIZE(protocols))
		return -EINVAL;

	mask = protocols[protocol].scancode_bits;

	switch (protocol) {
	case RC_PROTO_NECX:
		if ((((s >> 16) ^ ~(s >> 8)) & 0xff) == 0)
			return -EINVAL;
		break;
	case RC_PROTO_NEC32:
		if ((((s >> 24) ^ ~(s >> 16)) & 0xff) == 0)
			return -EINVAL;
		break;
	case RC_PROTO_RC6_MCE:
		if ((s & 0xffff0000) != 0x800f0000)
			return -EINVAL;
		break;
	case RC_PROTO_RC6_6A_32:
		if ((s & 0xffff0000) == 0x800f0000)
			return -EINVAL;
		break;
	default:
		break;
	}

	filter->data &= mask;
	filter->mask &= mask;

	/*
	 * If we have to raw encode the IR for wakeup, we cannot have a mask
	 */
	if (dev->encode_wakeup && filter->mask != 0 && filter->mask != mask)
		return -EINVAL;

	return 0;
}

int rc_open(struct rc_dev *rdev)
{
	int rval = 0;

	if (!rdev)
		return -EINVAL;

	mutex_lock(&rdev->lock);

	if (!rdev->users++ && rdev->open != NULL)
		rval = rdev->open(rdev);

	if (rval)
		rdev->users--;

	mutex_unlock(&rdev->lock);

	return rval;
}
EXPORT_SYMBOL_GPL(rc_open);

static int ir_open(struct input_dev *idev)
{
	struct rc_dev *rdev = input_get_drvdata(idev);

	return rc_open(rdev);
}

void rc_close(struct rc_dev *rdev)
{
	if (rdev) {
		mutex_lock(&rdev->lock);

		if (!--rdev->users && rdev->close != NULL)
			rdev->close(rdev);

		mutex_unlock(&rdev->lock);
	}
}
EXPORT_SYMBOL_GPL(rc_close);

static void ir_close(struct input_dev *idev)
{
	struct rc_dev *rdev = input_get_drvdata(idev);
	rc_close(rdev);
}

/* class for /sys/class/rc */
static char *rc_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "rc/%s", dev_name(dev));
}

static struct class rc_class = {
	.name		= "rc",
	.devnode	= rc_devnode,
};

/*
 * These are the protocol textual descriptions that are
 * used by the sysfs protocols file. Note that the order
 * of the entries is relevant.
 */
static const struct {
	u64	type;
	const char	*name;
	const char	*module_name;
} proto_names[] = {
	{ RC_PROTO_BIT_NONE,	"none",		NULL			},
	{ RC_PROTO_BIT_OTHER,	"other",	NULL			},
	{ RC_PROTO_BIT_UNKNOWN,	"unknown",	NULL			},
	{ RC_PROTO_BIT_RC5 |
	  RC_PROTO_BIT_RC5X_20,	"rc-5",		"ir-rc5-decoder"	},
	{ RC_PROTO_BIT_NEC |
	  RC_PROTO_BIT_NECX |
	  RC_PROTO_BIT_NEC32,	"nec",		"ir-nec-decoder"	},
	{ RC_PROTO_BIT_RC6_0 |
	  RC_PROTO_BIT_RC6_6A_20 |
	  RC_PROTO_BIT_RC6_6A_24 |
	  RC_PROTO_BIT_RC6_6A_32 |
	  RC_PROTO_BIT_RC6_MCE,	"rc-6",		"ir-rc6-decoder"	},
	{ RC_PROTO_BIT_JVC,	"jvc",		"ir-jvc-decoder"	},
	{ RC_PROTO_BIT_SONY12 |
	  RC_PROTO_BIT_SONY15 |
	  RC_PROTO_BIT_SONY20,	"sony",		"ir-sony-decoder"	},
	{ RC_PROTO_BIT_RC5_SZ,	"rc-5-sz",	"ir-rc5-decoder"	},
	{ RC_PROTO_BIT_SANYO,	"sanyo",	"ir-sanyo-decoder"	},
	{ RC_PROTO_BIT_SHARP,	"sharp",	"ir-sharp-decoder"	},
	{ RC_PROTO_BIT_MCIR2_KBD |
	  RC_PROTO_BIT_MCIR2_MSE, "mce_kbd",	"ir-mce_kbd-decoder"	},
	{ RC_PROTO_BIT_XMP,	"xmp",		"ir-xmp-decoder"	},
	{ RC_PROTO_BIT_CEC,	"cec",		NULL			},
};

/**
 * struct rc_filter_attribute - Device attribute relating to a filter type.
 * @attr:	Device attribute.
 * @type:	Filter type.
 * @mask:	false for filter value, true for filter mask.
 */
struct rc_filter_attribute {
	struct device_attribute		attr;
	enum rc_filter_type		type;
	bool				mask;
};
#define to_rc_filter_attr(a) container_of(a, struct rc_filter_attribute, attr)

#define RC_FILTER_ATTR(_name, _mode, _show, _store, _type, _mask)	\
	struct rc_filter_attribute dev_attr_##_name = {			\
		.attr = __ATTR(_name, _mode, _show, _store),		\
		.type = (_type),					\
		.mask = (_mask),					\
	}

static bool lirc_is_present(void)
{
#if defined(CONFIG_LIRC_MODULE)
	struct module *lirc;

	mutex_lock(&module_mutex);
	lirc = find_module("lirc_dev");
	mutex_unlock(&module_mutex);

	return lirc ? true : false;
#elif defined(CONFIG_LIRC)
	return true;
#else
	return false;
#endif
}

/**
 * show_protocols() - shows the current IR protocol(s)
 * @device:	the device descriptor
 * @mattr:	the device attribute struct
 * @buf:	a pointer to the output buffer
 *
 * This routine is a callback routine for input read the IR protocol type(s).
 * it is trigged by reading /sys/class/rc/rc?/protocols.
 * It returns the protocol names of supported protocols.
 * Enabled protocols are printed in brackets.
 *
 * dev->lock is taken to guard against races between
 * store_protocols and show_protocols.
 */
static ssize_t show_protocols(struct device *device,
			      struct device_attribute *mattr, char *buf)
{
	struct rc_dev *dev = to_rc_dev(device);
	u64 allowed, enabled;
	char *tmp = buf;
	int i;

	mutex_lock(&dev->lock);

	enabled = dev->enabled_protocols;
	allowed = dev->allowed_protocols;
	if (dev->raw && !allowed)
		allowed = ir_raw_get_allowed_protocols();

	mutex_unlock(&dev->lock);

	IR_dprintk(1, "%s: allowed - 0x%llx, enabled - 0x%llx\n",
		   __func__, (long long)allowed, (long long)enabled);

	for (i = 0; i < ARRAY_SIZE(proto_names); i++) {
		if (allowed & enabled & proto_names[i].type)
			tmp += sprintf(tmp, "[%s] ", proto_names[i].name);
		else if (allowed & proto_names[i].type)
			tmp += sprintf(tmp, "%s ", proto_names[i].name);

		if (allowed & proto_names[i].type)
			allowed &= ~proto_names[i].type;
	}

	if (dev->driver_type == RC_DRIVER_IR_RAW && lirc_is_present())
		tmp += sprintf(tmp, "[lirc] ");

	if (tmp != buf)
		tmp--;
	*tmp = '\n';

	return tmp + 1 - buf;
}

/**
 * parse_protocol_change() - parses a protocol change request
 * @protocols:	pointer to the bitmask of current protocols
 * @buf:	pointer to the buffer with a list of changes
 *
 * Writing "+proto" will add a protocol to the protocol mask.
 * Writing "-proto" will remove a protocol from protocol mask.
 * Writing "proto" will enable only "proto".
 * Writing "none" will disable all protocols.
 * Returns the number of changes performed or a negative error code.
 */
static int parse_protocol_change(u64 *protocols, const char *buf)
{
	const char *tmp;
	unsigned count = 0;
	bool enable, disable;
	u64 mask;
	int i;

	while ((tmp = strsep((char **)&buf, " \n")) != NULL) {
		if (!*tmp)
			break;

		if (*tmp == '+') {
			enable = true;
			disable = false;
			tmp++;
		} else if (*tmp == '-') {
			enable = false;
			disable = true;
			tmp++;
		} else {
			enable = false;
			disable = false;
		}

		for (i = 0; i < ARRAY_SIZE(proto_names); i++) {
			if (!strcasecmp(tmp, proto_names[i].name)) {
				mask = proto_names[i].type;
				break;
			}
		}

		if (i == ARRAY_SIZE(proto_names)) {
			if (!strcasecmp(tmp, "lirc"))
				mask = 0;
			else {
				IR_dprintk(1, "Unknown protocol: '%s'\n", tmp);
				return -EINVAL;
			}
		}

		count++;

		if (enable)
			*protocols |= mask;
		else if (disable)
			*protocols &= ~mask;
		else
			*protocols = mask;
	}

	if (!count) {
		IR_dprintk(1, "Protocol not specified\n");
		return -EINVAL;
	}

	return count;
}

static void ir_raw_load_modules(u64 *protocols)
{
	u64 available;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(proto_names); i++) {
		if (proto_names[i].type == RC_PROTO_BIT_NONE ||
		    proto_names[i].type & (RC_PROTO_BIT_OTHER |
					   RC_PROTO_BIT_UNKNOWN))
			continue;

		available = ir_raw_get_allowed_protocols();
		if (!(*protocols & proto_names[i].type & ~available))
			continue;

		if (!proto_names[i].module_name) {
			pr_err("Can't enable IR protocol %s\n",
			       proto_names[i].name);
			*protocols &= ~proto_names[i].type;
			continue;
		}

		ret = request_module("%s", proto_names[i].module_name);
		if (ret < 0) {
			pr_err("Couldn't load IR protocol module %s\n",
			       proto_names[i].module_name);
			*protocols &= ~proto_names[i].type;
			continue;
		}
		msleep(20);
		available = ir_raw_get_allowed_protocols();
		if (!(*protocols & proto_names[i].type & ~available))
			continue;

		pr_err("Loaded IR protocol module %s, but protocol %s still not available\n",
		       proto_names[i].module_name,
		       proto_names[i].name);
		*protocols &= ~proto_names[i].type;
	}
}

/**
 * store_protocols() - changes the current/wakeup IR protocol(s)
 * @device:	the device descriptor
 * @mattr:	the device attribute struct
 * @buf:	a pointer to the input buffer
 * @len:	length of the input buffer
 *
 * This routine is for changing the IR protocol type.
 * It is trigged by writing to /sys/class/rc/rc?/[wakeup_]protocols.
 * See parse_protocol_change() for the valid commands.
 * Returns @len on success or a negative error code.
 *
 * dev->lock is taken to guard against races between
 * store_protocols and show_protocols.
 */
static ssize_t store_protocols(struct device *device,
			       struct device_attribute *mattr,
			       const char *buf, size_t len)
{
	struct rc_dev *dev = to_rc_dev(device);
	u64 *current_protocols;
	struct rc_scancode_filter *filter;
	u64 old_protocols, new_protocols;
	ssize_t rc;

	IR_dprintk(1, "Normal protocol change requested\n");
	current_protocols = &dev->enabled_protocols;
	filter = &dev->scancode_filter;

	if (!dev->change_protocol) {
		IR_dprintk(1, "Protocol switching not supported\n");
		return -EINVAL;
	}

	mutex_lock(&dev->lock);

	old_protocols = *current_protocols;
	new_protocols = old_protocols;
	rc = parse_protocol_change(&new_protocols, buf);
	if (rc < 0)
		goto out;

	rc = dev->change_protocol(dev, &new_protocols);
	if (rc < 0) {
		IR_dprintk(1, "Error setting protocols to 0x%llx\n",
			   (long long)new_protocols);
		goto out;
	}

	if (dev->driver_type == RC_DRIVER_IR_RAW)
		ir_raw_load_modules(&new_protocols);

	if (new_protocols != old_protocols) {
		*current_protocols = new_protocols;
		IR_dprintk(1, "Protocols changed to 0x%llx\n",
			   (long long)new_protocols);
	}

	/*
	 * If a protocol change was attempted the filter may need updating, even
	 * if the actual protocol mask hasn't changed (since the driver may have
	 * cleared the filter).
	 * Try setting the same filter with the new protocol (if any).
	 * Fall back to clearing the filter.
	 */
	if (dev->s_filter && filter->mask) {
		if (new_protocols)
			rc = dev->s_filter(dev, filter);
		else
			rc = -1;

		if (rc < 0) {
			filter->data = 0;
			filter->mask = 0;
			dev->s_filter(dev, filter);
		}
	}

	rc = len;

out:
	mutex_unlock(&dev->lock);
	return rc;
}

/**
 * show_filter() - shows the current scancode filter value or mask
 * @device:	the device descriptor
 * @attr:	the device attribute struct
 * @buf:	a pointer to the output buffer
 *
 * This routine is a callback routine to read a scancode filter value or mask.
 * It is trigged by reading /sys/class/rc/rc?/[wakeup_]filter[_mask].
 * It prints the current scancode filter value or mask of the appropriate filter
 * type in hexadecimal into @buf and returns the size of the buffer.
 *
 * Bits of the filter value corresponding to set bits in the filter mask are
 * compared against input scancodes and non-matching scancodes are discarded.
 *
 * dev->lock is taken to guard against races between
 * store_filter and show_filter.
 */
static ssize_t show_filter(struct device *device,
			   struct device_attribute *attr,
			   char *buf)
{
	struct rc_dev *dev = to_rc_dev(device);
	struct rc_filter_attribute *fattr = to_rc_filter_attr(attr);
	struct rc_scancode_filter *filter;
	u32 val;

	mutex_lock(&dev->lock);

	if (fattr->type == RC_FILTER_NORMAL)
		filter = &dev->scancode_filter;
	else
		filter = &dev->scancode_wakeup_filter;

	if (fattr->mask)
		val = filter->mask;
	else
		val = filter->data;
	mutex_unlock(&dev->lock);

	return sprintf(buf, "%#x\n", val);
}

/**
 * store_filter() - changes the scancode filter value
 * @device:	the device descriptor
 * @attr:	the device attribute struct
 * @buf:	a pointer to the input buffer
 * @len:	length of the input buffer
 *
 * This routine is for changing a scancode filter value or mask.
 * It is trigged by writing to /sys/class/rc/rc?/[wakeup_]filter[_mask].
 * Returns -EINVAL if an invalid filter value for the current protocol was
 * specified or if scancode filtering is not supported by the driver, otherwise
 * returns @len.
 *
 * Bits of the filter value corresponding to set bits in the filter mask are
 * compared against input scancodes and non-matching scancodes are discarded.
 *
 * dev->lock is taken to guard against races between
 * store_filter and show_filter.
 */
static ssize_t store_filter(struct device *device,
			    struct device_attribute *attr,
			    const char *buf, size_t len)
{
	struct rc_dev *dev = to_rc_dev(device);
	struct rc_filter_attribute *fattr = to_rc_filter_attr(attr);
	struct rc_scancode_filter new_filter, *filter;
	int ret;
	unsigned long val;
	int (*set_filter)(struct rc_dev *dev, struct rc_scancode_filter *filter);

	ret = kstrtoul(buf, 0, &val);
	if (ret < 0)
		return ret;

	if (fattr->type == RC_FILTER_NORMAL) {
		set_filter = dev->s_filter;
		filter = &dev->scancode_filter;
	} else {
		set_filter = dev->s_wakeup_filter;
		filter = &dev->scancode_wakeup_filter;
	}

	if (!set_filter)
		return -EINVAL;

	mutex_lock(&dev->lock);

	new_filter = *filter;
	if (fattr->mask)
		new_filter.mask = val;
	else
		new_filter.data = val;

	if (fattr->type == RC_FILTER_WAKEUP) {
		/*
		 * Refuse to set a filter unless a protocol is enabled
		 * and the filter is valid for that protocol
		 */
		if (dev->wakeup_protocol != RC_PROTO_UNKNOWN)
			ret = rc_validate_filter(dev, &new_filter);
		else
			ret = -EINVAL;

		if (ret != 0)
			goto unlock;
	}

	if (fattr->type == RC_FILTER_NORMAL && !dev->enabled_protocols &&
	    val) {
		/* refuse to set a filter unless a protocol is enabled */
		ret = -EINVAL;
		goto unlock;
	}

	ret = set_filter(dev, &new_filter);
	if (ret < 0)
		goto unlock;

	*filter = new_filter;

unlock:
	mutex_unlock(&dev->lock);
	return (ret < 0) ? ret : len;
}

/**
 * show_wakeup_protocols() - shows the wakeup IR protocol
 * @device:	the device descriptor
 * @mattr:	the device attribute struct
 * @buf:	a pointer to the output buffer
 *
 * This routine is a callback routine for input read the IR protocol type(s).
 * it is trigged by reading /sys/class/rc/rc?/wakeup_protocols.
 * It returns the protocol names of supported protocols.
 * The enabled protocols are printed in brackets.
 *
 * dev->lock is taken to guard against races between
 * store_wakeup_protocols and show_wakeup_protocols.
 */
static ssize_t show_wakeup_protocols(struct device *device,
				     struct device_attribute *mattr,
				     char *buf)
{
	struct rc_dev *dev = to_rc_dev(device);
	u64 allowed;
	enum rc_proto enabled;
	char *tmp = buf;
	int i;

	mutex_lock(&dev->lock);

	allowed = dev->allowed_wakeup_protocols;
	enabled = dev->wakeup_protocol;

	mutex_unlock(&dev->lock);

	IR_dprintk(1, "%s: allowed - 0x%llx, enabled - %d\n",
		   __func__, (long long)allowed, enabled);

	for (i = 0; i < ARRAY_SIZE(protocols); i++) {
		if (allowed & (1ULL << i)) {
			if (i == enabled)
				tmp += sprintf(tmp, "[%s] ", protocols[i].name);
			else
				tmp += sprintf(tmp, "%s ", protocols[i].name);
		}
	}

	if (tmp != buf)
		tmp--;
	*tmp = '\n';

	return tmp + 1 - buf;
}

/**
 * store_wakeup_protocols() - changes the wakeup IR protocol(s)
 * @device:	the device descriptor
 * @mattr:	the device attribute struct
 * @buf:	a pointer to the input buffer
 * @len:	length of the input buffer
 *
 * This routine is for changing the IR protocol type.
 * It is trigged by writing to /sys/class/rc/rc?/wakeup_protocols.
 * Returns @len on success or a negative error code.
 *
 * dev->lock is taken to guard against races between
 * store_wakeup_protocols and show_wakeup_protocols.
 */
static ssize_t store_wakeup_protocols(struct device *device,
				      struct device_attribute *mattr,
				      const char *buf, size_t len)
{
	struct rc_dev *dev = to_rc_dev(device);
	enum rc_proto protocol;
	ssize_t rc;
	u64 allowed;
	int i;

	mutex_lock(&dev->lock);

	allowed = dev->allowed_wakeup_protocols;

	if (sysfs_streq(buf, "none")) {
		protocol = RC_PROTO_UNKNOWN;
	} else {
		for (i = 0; i < ARRAY_SIZE(protocols); i++) {
			if ((allowed & (1ULL << i)) &&
			    sysfs_streq(buf, protocols[i].name)) {
				protocol = i;
				break;
			}
		}

		if (i == ARRAY_SIZE(protocols)) {
			rc = -EINVAL;
			goto out;
		}

		if (dev->encode_wakeup) {
			u64 mask = 1ULL << protocol;

			ir_raw_load_modules(&mask);
			if (!mask) {
				rc = -EINVAL;
				goto out;
			}
		}
	}

	if (dev->wakeup_protocol != protocol) {
		dev->wakeup_protocol = protocol;
		IR_dprintk(1, "Wakeup protocol changed to %d\n", protocol);

		if (protocol == RC_PROTO_RC6_MCE)
			dev->scancode_wakeup_filter.data = 0x800f0000;
		else
			dev->scancode_wakeup_filter.data = 0;
		dev->scancode_wakeup_filter.mask = 0;

		rc = dev->s_wakeup_filter(dev, &dev->scancode_wakeup_filter);
		if (rc == 0)
			rc = len;
	} else {
		rc = len;
	}

out:
	mutex_unlock(&dev->lock);
	return rc;
}

static void rc_dev_release(struct device *device)
{
	struct rc_dev *dev = to_rc_dev(device);

	kfree(dev);
}

#define ADD_HOTPLUG_VAR(fmt, val...)					\
	do {								\
		int err = add_uevent_var(env, fmt, val);		\
		if (err)						\
			return err;					\
	} while (0)

static int rc_dev_uevent(struct device *device, struct kobj_uevent_env *env)
{
	struct rc_dev *dev = to_rc_dev(device);

	if (dev->rc_map.name)
		ADD_HOTPLUG_VAR("NAME=%s", dev->rc_map.name);
	if (dev->driver_name)
		ADD_HOTPLUG_VAR("DRV_NAME=%s", dev->driver_name);
	if (dev->device_name)
		ADD_HOTPLUG_VAR("DEV_NAME=%s", dev->device_name);

	return 0;
}

/*
 * Static device attribute struct with the sysfs attributes for IR's
 */
static struct device_attribute dev_attr_ro_protocols =
__ATTR(protocols, 0444, show_protocols, NULL);
static struct device_attribute dev_attr_rw_protocols =
__ATTR(protocols, 0644, show_protocols, store_protocols);
static DEVICE_ATTR(wakeup_protocols, 0644, show_wakeup_protocols,
		   store_wakeup_protocols);
static RC_FILTER_ATTR(filter, S_IRUGO|S_IWUSR,
		      show_filter, store_filter, RC_FILTER_NORMAL, false);
static RC_FILTER_ATTR(filter_mask, S_IRUGO|S_IWUSR,
		      show_filter, store_filter, RC_FILTER_NORMAL, true);
static RC_FILTER_ATTR(wakeup_filter, S_IRUGO|S_IWUSR,
		      show_filter, store_filter, RC_FILTER_WAKEUP, false);
static RC_FILTER_ATTR(wakeup_filter_mask, S_IRUGO|S_IWUSR,
		      show_filter, store_filter, RC_FILTER_WAKEUP, true);

static struct attribute *rc_dev_rw_protocol_attrs[] = {
	&dev_attr_rw_protocols.attr,
	NULL,
};

static const struct attribute_group rc_dev_rw_protocol_attr_grp = {
	.attrs	= rc_dev_rw_protocol_attrs,
};

static struct attribute *rc_dev_ro_protocol_attrs[] = {
	&dev_attr_ro_protocols.attr,
	NULL,
};

static const struct attribute_group rc_dev_ro_protocol_attr_grp = {
	.attrs	= rc_dev_ro_protocol_attrs,
};

static struct attribute *rc_dev_filter_attrs[] = {
	&dev_attr_filter.attr.attr,
	&dev_attr_filter_mask.attr.attr,
	NULL,
};

static const struct attribute_group rc_dev_filter_attr_grp = {
	.attrs	= rc_dev_filter_attrs,
};

static struct attribute *rc_dev_wakeup_filter_attrs[] = {
	&dev_attr_wakeup_filter.attr.attr,
	&dev_attr_wakeup_filter_mask.attr.attr,
	&dev_attr_wakeup_protocols.attr,
	NULL,
};

static const struct attribute_group rc_dev_wakeup_filter_attr_grp = {
	.attrs	= rc_dev_wakeup_filter_attrs,
};

static const struct device_type rc_dev_type = {
	.release	= rc_dev_release,
	.uevent		= rc_dev_uevent,
};

struct rc_dev *rc_allocate_device(enum rc_driver_type type)
{
	struct rc_dev *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	if (type != RC_DRIVER_IR_RAW_TX) {
		dev->input_dev = input_allocate_device();
		if (!dev->input_dev) {
			kfree(dev);
			return NULL;
		}

		dev->input_dev->getkeycode = ir_getkeycode;
		dev->input_dev->setkeycode = ir_setkeycode;
		input_set_drvdata(dev->input_dev, dev);

		timer_setup(&dev->timer_keyup, ir_timer_keyup, 0);

		spin_lock_init(&dev->rc_map.lock);
		spin_lock_init(&dev->keylock);
	}
	mutex_init(&dev->lock);

	dev->dev.type = &rc_dev_type;
	dev->dev.class = &rc_class;
	device_initialize(&dev->dev);

	dev->driver_type = type;

	__module_get(THIS_MODULE);
	return dev;
}
EXPORT_SYMBOL_GPL(rc_allocate_device);

void rc_free_device(struct rc_dev *dev)
{
	if (!dev)
		return;

	input_free_device(dev->input_dev);

	put_device(&dev->dev);

	/* kfree(dev) will be called by the callback function
	   rc_dev_release() */

	module_put(THIS_MODULE);
}
EXPORT_SYMBOL_GPL(rc_free_device);

static void devm_rc_alloc_release(struct device *dev, void *res)
{
	rc_free_device(*(struct rc_dev **)res);
}

struct rc_dev *devm_rc_allocate_device(struct device *dev,
				       enum rc_driver_type type)
{
	struct rc_dev **dr, *rc;

	dr = devres_alloc(devm_rc_alloc_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return NULL;

	rc = rc_allocate_device(type);
	if (!rc) {
		devres_free(dr);
		return NULL;
	}

	rc->dev.parent = dev;
	rc->managed_alloc = true;
	*dr = rc;
	devres_add(dev, dr);

	return rc;
}
EXPORT_SYMBOL_GPL(devm_rc_allocate_device);

static int rc_prepare_rx_device(struct rc_dev *dev)
{
	int rc;
	struct rc_map *rc_map;
	u64 rc_proto;

	if (!dev->map_name)
		return -EINVAL;

	rc_map = rc_map_get(dev->map_name);
	if (!rc_map)
		rc_map = rc_map_get(RC_MAP_EMPTY);
	if (!rc_map || !rc_map->scan || rc_map->size == 0)
		return -EINVAL;

	rc = ir_setkeytable(dev, rc_map);
	if (rc)
		return rc;

	rc_proto = BIT_ULL(rc_map->rc_proto);

	if (dev->driver_type == RC_DRIVER_SCANCODE && !dev->change_protocol)
		dev->enabled_protocols = dev->allowed_protocols;

	if (dev->change_protocol) {
		rc = dev->change_protocol(dev, &rc_proto);
		if (rc < 0)
			goto out_table;
		dev->enabled_protocols = rc_proto;
	}

	if (dev->driver_type == RC_DRIVER_IR_RAW)
		ir_raw_load_modules(&rc_proto);

	set_bit(EV_KEY, dev->input_dev->evbit);
	set_bit(EV_REP, dev->input_dev->evbit);
	set_bit(EV_MSC, dev->input_dev->evbit);
	set_bit(MSC_SCAN, dev->input_dev->mscbit);
	if (dev->open)
		dev->input_dev->open = ir_open;
	if (dev->close)
		dev->input_dev->close = ir_close;

	dev->input_dev->dev.parent = &dev->dev;
	memcpy(&dev->input_dev->id, &dev->input_id, sizeof(dev->input_id));
	dev->input_dev->phys = dev->input_phys;
	dev->input_dev->name = dev->device_name;

	return 0;

out_table:
	ir_free_table(&dev->rc_map);

	return rc;
}

static int rc_setup_rx_device(struct rc_dev *dev)
{
	int rc;

	/* rc_open will be called here */
	rc = input_register_device(dev->input_dev);
	if (rc)
		return rc;

	/*
	 * Default delay of 250ms is too short for some protocols, especially
	 * since the timeout is currently set to 250ms. Increase it to 500ms,
	 * to avoid wrong repetition of the keycodes. Note that this must be
	 * set after the call to input_register_device().
	 */
	dev->input_dev->rep[REP_DELAY] = 500;

	/*
	 * As a repeat event on protocols like RC-5 and NEC take as long as
	 * 110/114ms, using 33ms as a repeat period is not the right thing
	 * to do.
	 */
	dev->input_dev->rep[REP_PERIOD] = 125;

	return 0;
}

static void rc_free_rx_device(struct rc_dev *dev)
{
	if (!dev)
		return;

	if (dev->input_dev) {
		input_unregister_device(dev->input_dev);
		dev->input_dev = NULL;
	}

	ir_free_table(&dev->rc_map);
}

int rc_register_device(struct rc_dev *dev)
{
	const char *path;
	int attr = 0;
	int minor;
	int rc;

	if (!dev)
		return -EINVAL;

	minor = ida_simple_get(&rc_ida, 0, RC_DEV_MAX, GFP_KERNEL);
	if (minor < 0)
		return minor;

	dev->minor = minor;
	dev_set_name(&dev->dev, "rc%u", dev->minor);
	dev_set_drvdata(&dev->dev, dev);

	dev->dev.groups = dev->sysfs_groups;
	if (dev->driver_type == RC_DRIVER_SCANCODE && !dev->change_protocol)
		dev->sysfs_groups[attr++] = &rc_dev_ro_protocol_attr_grp;
	else if (dev->driver_type != RC_DRIVER_IR_RAW_TX)
		dev->sysfs_groups[attr++] = &rc_dev_rw_protocol_attr_grp;
	if (dev->s_filter)
		dev->sysfs_groups[attr++] = &rc_dev_filter_attr_grp;
	if (dev->s_wakeup_filter)
		dev->sysfs_groups[attr++] = &rc_dev_wakeup_filter_attr_grp;
	dev->sysfs_groups[attr++] = NULL;

	if (dev->driver_type == RC_DRIVER_IR_RAW ||
	    dev->driver_type == RC_DRIVER_IR_RAW_TX) {
		rc = ir_raw_event_prepare(dev);
		if (rc < 0)
			goto out_minor;
	}

	if (dev->driver_type != RC_DRIVER_IR_RAW_TX) {
		rc = rc_prepare_rx_device(dev);
		if (rc)
			goto out_raw;
	}

	rc = device_add(&dev->dev);
	if (rc)
		goto out_rx_free;

	path = kobject_get_path(&dev->dev.kobj, GFP_KERNEL);
	dev_info(&dev->dev, "%s as %s\n",
		 dev->device_name ?: "Unspecified device", path ?: "N/A");
	kfree(path);

	if (dev->driver_type != RC_DRIVER_IR_RAW_TX) {
		rc = rc_setup_rx_device(dev);
		if (rc)
			goto out_dev;
	}

	if (dev->driver_type == RC_DRIVER_IR_RAW ||
	    dev->driver_type == RC_DRIVER_IR_RAW_TX) {
		rc = ir_raw_event_register(dev);
		if (rc < 0)
			goto out_rx;
	}

	IR_dprintk(1, "Registered rc%u (driver: %s)\n",
		   dev->minor,
		   dev->driver_name ? dev->driver_name : "unknown");

	return 0;

out_rx:
	rc_free_rx_device(dev);
out_dev:
	device_del(&dev->dev);
out_rx_free:
	ir_free_table(&dev->rc_map);
out_raw:
	ir_raw_event_free(dev);
out_minor:
	ida_simple_remove(&rc_ida, minor);
	return rc;
}
EXPORT_SYMBOL_GPL(rc_register_device);

static void devm_rc_release(struct device *dev, void *res)
{
	rc_unregister_device(*(struct rc_dev **)res);
}

int devm_rc_register_device(struct device *parent, struct rc_dev *dev)
{
	struct rc_dev **dr;
	int ret;

	dr = devres_alloc(devm_rc_release, sizeof(*dr), GFP_KERNEL);
	if (!dr)
		return -ENOMEM;

	ret = rc_register_device(dev);
	if (ret) {
		devres_free(dr);
		return ret;
	}

	*dr = dev;
	devres_add(parent, dr);

	return 0;
}
EXPORT_SYMBOL_GPL(devm_rc_register_device);

void rc_unregister_device(struct rc_dev *dev)
{
	if (!dev)
		return;

	del_timer_sync(&dev->timer_keyup);

	if (dev->driver_type == RC_DRIVER_IR_RAW)
		ir_raw_event_unregister(dev);

	rc_free_rx_device(dev);

	device_del(&dev->dev);

	ida_simple_remove(&rc_ida, dev->minor);

	if (!dev->managed_alloc)
		rc_free_device(dev);
}

EXPORT_SYMBOL_GPL(rc_unregister_device);

/*
 * Init/exit code for the module. Basically, creates/removes /sys/class/rc
 */

static int __init rc_core_init(void)
{
	int rc = class_register(&rc_class);
	if (rc) {
		pr_err("rc_core: unable to register rc class\n");
		return rc;
	}

	led_trigger_register_simple("rc-feedback", &led_feedback);
	rc_map_register(&empty_map);

	return 0;
}

static void __exit rc_core_exit(void)
{
	class_unregister(&rc_class);
	led_trigger_unregister_simple(led_feedback);
	rc_map_unregister(&empty_map);
}

subsys_initcall(rc_core_init);
module_exit(rc_core_exit);

int rc_core_debug;    /* ir_debug level (0,1,2) */
EXPORT_SYMBOL_GPL(rc_core_debug);
module_param_named(debug, rc_core_debug, int, 0644);

MODULE_AUTHOR("Mauro Carvalho Chehab");
MODULE_LICENSE("GPL");
