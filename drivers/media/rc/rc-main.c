/* rc-main.c - Remote Controller core module
 *
 * Copyright (C) 2009-2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
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

#include <media/rc-core.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/module.h>
#include "rc-core-priv.h"

/* Sizes are in bytes, 256 bytes allows for 32 entries on x64 */
#define IR_TAB_MIN_SIZE	256
#define IR_TAB_MAX_SIZE	8192

/* FIXME: IR_KEYPRESS_TIMEOUT should be protocol specific */
#define IR_KEYPRESS_TIMEOUT 250

/* Used to keep track of known keymaps */
static LIST_HEAD(rc_map_list);
static DEFINE_SPINLOCK(rc_map_lock);

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
#ifdef MODULE
	if (!map) {
		int rc = request_module(name);
		if (rc < 0) {
			printk(KERN_ERR "Couldn't load IR keymap %s\n", name);
			return NULL;
		}
		msleep(20);	/* Give some time for IR to register */

		map = seek_rc_map(name);
	}
#endif
	if (!map) {
		printk(KERN_ERR "IR keymap %s not found\n", name);
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
		.scan    = empty,
		.size    = ARRAY_SIZE(empty),
		.rc_type = RC_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_EMPTY,
	}
};

/**
 * ir_create_table() - initializes a scancode table
 * @rc_map:	the rc_map to initialize
 * @name:	name to assign to the table
 * @rc_type:	ir type to assign to the new table
 * @size:	initial size of the table
 * @return:	zero on success or a negative error code
 *
 * This routine will initialize the rc_map and will allocate
 * memory to hold at least the specified number of elements.
 */
static int ir_create_table(struct rc_map *rc_map,
			   const char *name, u64 rc_type, size_t size)
{
	rc_map->name = name;
	rc_map->rc_type = rc_type;
	rc_map->alloc = roundup_pow_of_two(size * sizeof(struct rc_map_table));
	rc_map->size = rc_map->alloc / sizeof(struct rc_map_table);
	rc_map->scan = kmalloc(rc_map->alloc, GFP_KERNEL);
	if (!rc_map->scan)
		return -ENOMEM;

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
	if (dev->scanmask)
		scancode &= dev->scanmask;

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
			     from->rc_type, from->size);
	if (rc)
		return rc;

	IR_dprintk(1, "Allocated space for %u keycode entries (%u bytes)\n",
		   rc_map->size, rc_map->alloc);

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
	int start = 0;
	int end = rc_map->len - 1;
	int mid;

	while (start <= end) {
		mid = (start + end) / 2;
		if (rc_map->scan[mid].scancode < scancode)
			start = mid + 1;
		else if (rc_map->scan[mid].scancode > scancode)
			end = mid - 1;
		else
			return mid;
	}

	return -1U;
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
			   dev->input_name, scancode, keycode);

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
static void ir_timer_keyup(unsigned long cookie)
{
	struct rc_dev *dev = (struct rc_dev *)cookie;
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

	spin_lock_irqsave(&dev->keylock, flags);

	input_event(dev->input_dev, EV_MSC, MSC_SCAN, dev->last_scancode);
	input_sync(dev->input_dev);

	if (!dev->keypressed)
		goto out;

	dev->keyup_jiffies = jiffies + msecs_to_jiffies(IR_KEYPRESS_TIMEOUT);
	mod_timer(&dev->timer_keyup, dev->keyup_jiffies);

out:
	spin_unlock_irqrestore(&dev->keylock, flags);
}
EXPORT_SYMBOL_GPL(rc_repeat);

/**
 * ir_do_keydown() - internal function to process a keypress
 * @dev:	the struct rc_dev descriptor of the device
 * @scancode:   the scancode of the keypress
 * @keycode:    the keycode of the keypress
 * @toggle:     the toggle value of the keypress
 *
 * This function is used internally to register a keypress, it must be
 * called with keylock held.
 */
static void ir_do_keydown(struct rc_dev *dev, int scancode,
			  u32 keycode, u8 toggle)
{
	bool new_event = !dev->keypressed ||
			 dev->last_scancode != scancode ||
			 dev->last_toggle != toggle;

	if (new_event && dev->keypressed)
		ir_do_keyup(dev, false);

	input_event(dev->input_dev, EV_MSC, MSC_SCAN, scancode);

	if (new_event && keycode != KEY_RESERVED) {
		/* Register a keypress */
		dev->keypressed = true;
		dev->last_scancode = scancode;
		dev->last_toggle = toggle;
		dev->last_keycode = keycode;

		IR_dprintk(1, "%s: key down event, "
			   "key 0x%04x, scancode 0x%04x\n",
			   dev->input_name, keycode, scancode);
		input_report_key(dev->input_dev, keycode, 1);
	}

	input_sync(dev->input_dev);
}

/**
 * rc_keydown() - generates input event for a key press
 * @dev:	the struct rc_dev descriptor of the device
 * @scancode:   the scancode that we're seeking
 * @toggle:     the toggle value (protocol dependent, if the protocol doesn't
 *              support toggle values, this should be set to zero)
 *
 * This routine is used to signal that a key has been pressed on the
 * remote control.
 */
void rc_keydown(struct rc_dev *dev, int scancode, u8 toggle)
{
	unsigned long flags;
	u32 keycode = rc_g_keycode_from_table(dev, scancode);

	spin_lock_irqsave(&dev->keylock, flags);
	ir_do_keydown(dev, scancode, keycode, toggle);

	if (dev->keypressed) {
		dev->keyup_jiffies = jiffies + msecs_to_jiffies(IR_KEYPRESS_TIMEOUT);
		mod_timer(&dev->timer_keyup, dev->keyup_jiffies);
	}
	spin_unlock_irqrestore(&dev->keylock, flags);
}
EXPORT_SYMBOL_GPL(rc_keydown);

/**
 * rc_keydown_notimeout() - generates input event for a key press without
 *                          an automatic keyup event at a later time
 * @dev:	the struct rc_dev descriptor of the device
 * @scancode:   the scancode that we're seeking
 * @toggle:     the toggle value (protocol dependent, if the protocol doesn't
 *              support toggle values, this should be set to zero)
 *
 * This routine is used to signal that a key has been pressed on the
 * remote control. The driver must manually call rc_keyup() at a later stage.
 */
void rc_keydown_notimeout(struct rc_dev *dev, int scancode, u8 toggle)
{
	unsigned long flags;
	u32 keycode = rc_g_keycode_from_table(dev, scancode);

	spin_lock_irqsave(&dev->keylock, flags);
	ir_do_keydown(dev, scancode, keycode, toggle);
	spin_unlock_irqrestore(&dev->keylock, flags);
}
EXPORT_SYMBOL_GPL(rc_keydown_notimeout);

static int ir_open(struct input_dev *idev)
{
	struct rc_dev *rdev = input_get_drvdata(idev);

	return rdev->open(rdev);
}

static void ir_close(struct input_dev *idev)
{
	struct rc_dev *rdev = input_get_drvdata(idev);

	 if (rdev)
		rdev->close(rdev);
}

/* class for /sys/class/rc */
static char *ir_devnode(struct device *dev, umode_t *mode)
{
	return kasprintf(GFP_KERNEL, "rc/%s", dev_name(dev));
}

static struct class ir_input_class = {
	.name		= "rc",
	.devnode	= ir_devnode,
};

static struct {
	u64	type;
	char	*name;
} proto_names[] = {
	{ RC_TYPE_UNKNOWN,	"unknown"	},
	{ RC_TYPE_RC5,		"rc-5"		},
	{ RC_TYPE_NEC,		"nec"		},
	{ RC_TYPE_RC6,		"rc-6"		},
	{ RC_TYPE_JVC,		"jvc"		},
	{ RC_TYPE_SONY,		"sony"		},
	{ RC_TYPE_RC5_SZ,	"rc-5-sz"	},
	{ RC_TYPE_SANYO,	"sanyo"		},
	{ RC_TYPE_MCE_KBD,	"mce_kbd"	},
	{ RC_TYPE_LIRC,		"lirc"		},
	{ RC_TYPE_OTHER,	"other"		},
};

#define PROTO_NONE	"none"

/**
 * show_protocols() - shows the current IR protocol(s)
 * @device:	the device descriptor
 * @mattr:	the device attribute struct (unused)
 * @buf:	a pointer to the output buffer
 *
 * This routine is a callback routine for input read the IR protocol type(s).
 * it is trigged by reading /sys/class/rc/rc?/protocols.
 * It returns the protocol names of supported protocols.
 * Enabled protocols are printed in brackets.
 *
 * dev->lock is taken to guard against races between device
 * registration, store_protocols and show_protocols.
 */
static ssize_t show_protocols(struct device *device,
			      struct device_attribute *mattr, char *buf)
{
	struct rc_dev *dev = to_rc_dev(device);
	u64 allowed, enabled;
	char *tmp = buf;
	int i;

	/* Device is being removed */
	if (!dev)
		return -EINVAL;

	mutex_lock(&dev->lock);

	if (dev->driver_type == RC_DRIVER_SCANCODE) {
		enabled = dev->rc_map.rc_type;
		allowed = dev->allowed_protos;
	} else {
		enabled = dev->raw->enabled_protocols;
		allowed = ir_raw_get_allowed_protocols();
	}

	IR_dprintk(1, "allowed - 0x%llx, enabled - 0x%llx\n",
		   (long long)allowed,
		   (long long)enabled);

	for (i = 0; i < ARRAY_SIZE(proto_names); i++) {
		if (allowed & enabled & proto_names[i].type)
			tmp += sprintf(tmp, "[%s] ", proto_names[i].name);
		else if (allowed & proto_names[i].type)
			tmp += sprintf(tmp, "%s ", proto_names[i].name);
	}

	if (tmp != buf)
		tmp--;
	*tmp = '\n';

	mutex_unlock(&dev->lock);

	return tmp + 1 - buf;
}

/**
 * store_protocols() - changes the current IR protocol(s)
 * @device:	the device descriptor
 * @mattr:	the device attribute struct (unused)
 * @buf:	a pointer to the input buffer
 * @len:	length of the input buffer
 *
 * This routine is for changing the IR protocol type.
 * It is trigged by writing to /sys/class/rc/rc?/protocols.
 * Writing "+proto" will add a protocol to the list of enabled protocols.
 * Writing "-proto" will remove a protocol from the list of enabled protocols.
 * Writing "proto" will enable only "proto".
 * Writing "none" will disable all protocols.
 * Returns -EINVAL if an invalid protocol combination or unknown protocol name
 * is used, otherwise @len.
 *
 * dev->lock is taken to guard against races between device
 * registration, store_protocols and show_protocols.
 */
static ssize_t store_protocols(struct device *device,
			       struct device_attribute *mattr,
			       const char *data,
			       size_t len)
{
	struct rc_dev *dev = to_rc_dev(device);
	bool enable, disable;
	const char *tmp;
	u64 type;
	u64 mask;
	int rc, i, count = 0;
	unsigned long flags;
	ssize_t ret;

	/* Device is being removed */
	if (!dev)
		return -EINVAL;

	mutex_lock(&dev->lock);

	if (dev->driver_type == RC_DRIVER_SCANCODE)
		type = dev->rc_map.rc_type;
	else if (dev->raw)
		type = dev->raw->enabled_protocols;
	else {
		IR_dprintk(1, "Protocol switching not supported\n");
		ret = -EINVAL;
		goto out;
	}

	while ((tmp = strsep((char **) &data, " \n")) != NULL) {
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

		if (!enable && !disable && !strncasecmp(tmp, PROTO_NONE, sizeof(PROTO_NONE))) {
			tmp += sizeof(PROTO_NONE);
			mask = 0;
			count++;
		} else {
			for (i = 0; i < ARRAY_SIZE(proto_names); i++) {
				if (!strcasecmp(tmp, proto_names[i].name)) {
					tmp += strlen(proto_names[i].name);
					mask = proto_names[i].type;
					break;
				}
			}
			if (i == ARRAY_SIZE(proto_names)) {
				IR_dprintk(1, "Unknown protocol: '%s'\n", tmp);
				ret = -EINVAL;
				goto out;
			}
			count++;
		}

		if (enable)
			type |= mask;
		else if (disable)
			type &= ~mask;
		else
			type = mask;
	}

	if (!count) {
		IR_dprintk(1, "Protocol not specified\n");
		ret = -EINVAL;
		goto out;
	}

	if (dev->change_protocol) {
		rc = dev->change_protocol(dev, type);
		if (rc < 0) {
			IR_dprintk(1, "Error setting protocols to 0x%llx\n",
				   (long long)type);
			ret = -EINVAL;
			goto out;
		}
	}

	if (dev->driver_type == RC_DRIVER_SCANCODE) {
		spin_lock_irqsave(&dev->rc_map.lock, flags);
		dev->rc_map.rc_type = type;
		spin_unlock_irqrestore(&dev->rc_map.lock, flags);
	} else {
		dev->raw->enabled_protocols = type;
	}

	IR_dprintk(1, "Current protocol(s): 0x%llx\n",
		   (long long)type);

	ret = len;

out:
	mutex_unlock(&dev->lock);
	return ret;
}

static void rc_dev_release(struct device *device)
{
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

	if (!dev || !dev->input_dev)
		return -ENODEV;

	if (dev->rc_map.name)
		ADD_HOTPLUG_VAR("NAME=%s", dev->rc_map.name);
	if (dev->driver_name)
		ADD_HOTPLUG_VAR("DRV_NAME=%s", dev->driver_name);

	return 0;
}

/*
 * Static device attribute struct with the sysfs attributes for IR's
 */
static DEVICE_ATTR(protocols, S_IRUGO | S_IWUSR,
		   show_protocols, store_protocols);

static struct attribute *rc_dev_attrs[] = {
	&dev_attr_protocols.attr,
	NULL,
};

static struct attribute_group rc_dev_attr_grp = {
	.attrs	= rc_dev_attrs,
};

static const struct attribute_group *rc_dev_attr_groups[] = {
	&rc_dev_attr_grp,
	NULL
};

static struct device_type rc_dev_type = {
	.groups		= rc_dev_attr_groups,
	.release	= rc_dev_release,
	.uevent		= rc_dev_uevent,
};

struct rc_dev *rc_allocate_device(void)
{
	struct rc_dev *dev;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->input_dev = input_allocate_device();
	if (!dev->input_dev) {
		kfree(dev);
		return NULL;
	}

	dev->input_dev->getkeycode = ir_getkeycode;
	dev->input_dev->setkeycode = ir_setkeycode;
	input_set_drvdata(dev->input_dev, dev);

	spin_lock_init(&dev->rc_map.lock);
	spin_lock_init(&dev->keylock);
	mutex_init(&dev->lock);
	setup_timer(&dev->timer_keyup, ir_timer_keyup, (unsigned long)dev);

	dev->dev.type = &rc_dev_type;
	dev->dev.class = &ir_input_class;
	device_initialize(&dev->dev);

	__module_get(THIS_MODULE);
	return dev;
}
EXPORT_SYMBOL_GPL(rc_allocate_device);

void rc_free_device(struct rc_dev *dev)
{
	if (!dev)
		return;

	if (dev->input_dev)
		input_free_device(dev->input_dev);

	put_device(&dev->dev);

	kfree(dev);
	module_put(THIS_MODULE);
}
EXPORT_SYMBOL_GPL(rc_free_device);

int rc_register_device(struct rc_dev *dev)
{
	static bool raw_init = false; /* raw decoders loaded? */
	static atomic_t devno = ATOMIC_INIT(0);
	struct rc_map *rc_map;
	const char *path;
	int rc;

	if (!dev || !dev->map_name)
		return -EINVAL;

	rc_map = rc_map_get(dev->map_name);
	if (!rc_map)
		rc_map = rc_map_get(RC_MAP_EMPTY);
	if (!rc_map || !rc_map->scan || rc_map->size == 0)
		return -EINVAL;

	set_bit(EV_KEY, dev->input_dev->evbit);
	set_bit(EV_REP, dev->input_dev->evbit);
	set_bit(EV_MSC, dev->input_dev->evbit);
	set_bit(MSC_SCAN, dev->input_dev->mscbit);
	if (dev->open)
		dev->input_dev->open = ir_open;
	if (dev->close)
		dev->input_dev->close = ir_close;

	/*
	 * Take the lock here, as the device sysfs node will appear
	 * when device_add() is called, which may trigger an ir-keytable udev
	 * rule, which will in turn call show_protocols and access either
	 * dev->rc_map.rc_type or dev->raw->enabled_protocols before it has
	 * been initialized.
	 */
	mutex_lock(&dev->lock);

	dev->devno = (unsigned long)(atomic_inc_return(&devno) - 1);
	dev_set_name(&dev->dev, "rc%ld", dev->devno);
	dev_set_drvdata(&dev->dev, dev);
	rc = device_add(&dev->dev);
	if (rc)
		goto out_unlock;

	rc = ir_setkeytable(dev, rc_map);
	if (rc)
		goto out_dev;

	dev->input_dev->dev.parent = &dev->dev;
	memcpy(&dev->input_dev->id, &dev->input_id, sizeof(dev->input_id));
	dev->input_dev->phys = dev->input_phys;
	dev->input_dev->name = dev->input_name;
	rc = input_register_device(dev->input_dev);
	if (rc)
		goto out_table;

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

	path = kobject_get_path(&dev->dev.kobj, GFP_KERNEL);
	printk(KERN_INFO "%s: %s as %s\n",
		dev_name(&dev->dev),
		dev->input_name ? dev->input_name : "Unspecified device",
		path ? path : "N/A");
	kfree(path);

	if (dev->driver_type == RC_DRIVER_IR_RAW) {
		/* Load raw decoders, if they aren't already */
		if (!raw_init) {
			IR_dprintk(1, "Loading raw decoders\n");
			ir_raw_init();
			raw_init = true;
		}
		rc = ir_raw_event_register(dev);
		if (rc < 0)
			goto out_input;
	}

	if (dev->change_protocol) {
		rc = dev->change_protocol(dev, rc_map->rc_type);
		if (rc < 0)
			goto out_raw;
	}

	mutex_unlock(&dev->lock);

	IR_dprintk(1, "Registered rc%ld (driver: %s, remote: %s, mode %s)\n",
		   dev->devno,
		   dev->driver_name ? dev->driver_name : "unknown",
		   rc_map->name ? rc_map->name : "unknown",
		   dev->driver_type == RC_DRIVER_IR_RAW ? "raw" : "cooked");

	return 0;

out_raw:
	if (dev->driver_type == RC_DRIVER_IR_RAW)
		ir_raw_event_unregister(dev);
out_input:
	input_unregister_device(dev->input_dev);
	dev->input_dev = NULL;
out_table:
	ir_free_table(&dev->rc_map);
out_dev:
	device_del(&dev->dev);
out_unlock:
	mutex_unlock(&dev->lock);
	return rc;
}
EXPORT_SYMBOL_GPL(rc_register_device);

void rc_unregister_device(struct rc_dev *dev)
{
	if (!dev)
		return;

	del_timer_sync(&dev->timer_keyup);

	if (dev->driver_type == RC_DRIVER_IR_RAW)
		ir_raw_event_unregister(dev);

	/* Freeing the table should also call the stop callback */
	ir_free_table(&dev->rc_map);
	IR_dprintk(1, "Freed keycode table\n");

	input_unregister_device(dev->input_dev);
	dev->input_dev = NULL;

	device_del(&dev->dev);

	rc_free_device(dev);
}

EXPORT_SYMBOL_GPL(rc_unregister_device);

/*
 * Init/exit code for the module. Basically, creates/removes /sys/class/rc
 */

static int __init rc_core_init(void)
{
	int rc = class_register(&ir_input_class);
	if (rc) {
		printk(KERN_ERR "rc_core: unable to register rc class\n");
		return rc;
	}

	rc_map_register(&empty_map);

	return 0;
}

static void __exit rc_core_exit(void)
{
	class_unregister(&ir_input_class);
	rc_map_unregister(&empty_map);
}

module_init(rc_core_init);
module_exit(rc_core_exit);

int rc_core_debug;    /* ir_debug level (0,1,2) */
EXPORT_SYMBOL_GPL(rc_core_debug);
module_param_named(debug, rc_core_debug, int, 0644);

MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_LICENSE("GPL");
