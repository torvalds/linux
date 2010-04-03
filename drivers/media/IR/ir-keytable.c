/* ir-register.c - handle IR scancode->keycode tables
 *
 * Copyright (C) 2009 by Mauro Carvalho Chehab <mchehab@redhat.com>
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


#include <linux/input.h>
#include <linux/slab.h>
#include <media/ir-common.h>

/* Sizes are in bytes, 256 bytes allows for 32 entries on x64 */
#define IR_TAB_MIN_SIZE	256
#define IR_TAB_MAX_SIZE	8192

/**
 * ir_resize_table() - resizes a scancode table if necessary
 * @rc_tab:	the ir_scancode_table to resize
 * @return:	zero on success or a negative error code
 *
 * This routine will shrink the ir_scancode_table if it has lots of
 * unused entries and grow it if it is full.
 */
static int ir_resize_table(struct ir_scancode_table *rc_tab)
{
	unsigned int oldalloc = rc_tab->alloc;
	unsigned int newalloc = oldalloc;
	struct ir_scancode *oldscan = rc_tab->scan;
	struct ir_scancode *newscan;

	if (rc_tab->size == rc_tab->len) {
		/* All entries in use -> grow keytable */
		if (rc_tab->alloc >= IR_TAB_MAX_SIZE)
			return -ENOMEM;

		newalloc *= 2;
		IR_dprintk(1, "Growing table to %u bytes\n", newalloc);
	}

	if ((rc_tab->len * 3 < rc_tab->size) && (oldalloc > IR_TAB_MIN_SIZE)) {
		/* Less than 1/3 of entries in use -> shrink keytable */
		newalloc /= 2;
		IR_dprintk(1, "Shrinking table to %u bytes\n", newalloc);
	}

	if (newalloc == oldalloc)
		return 0;

	newscan = kmalloc(newalloc, GFP_ATOMIC);
	if (!newscan) {
		IR_dprintk(1, "Failed to kmalloc %u bytes\n", newalloc);
		return -ENOMEM;
	}

	memcpy(newscan, rc_tab->scan, rc_tab->len * sizeof(struct ir_scancode));
	rc_tab->scan = newscan;
	rc_tab->alloc = newalloc;
	rc_tab->size = rc_tab->alloc / sizeof(struct ir_scancode);
	kfree(oldscan);
	return 0;
}

/**
 * ir_do_setkeycode() - internal function to set a keycode in the
 *			scancode->keycode table
 * @dev:	the struct input_dev device descriptor
 * @rc_tab:	the struct ir_scancode_table to set the keycode in
 * @scancode:	the scancode for the ir command
 * @keycode:	the keycode for the ir command
 * @return:	-EINVAL if the keycode could not be inserted, otherwise zero.
 *
 * This routine is used internally to manipulate the scancode->keycode table.
 * The caller has to hold @rc_tab->lock.
 */
static int ir_do_setkeycode(struct input_dev *dev,
			    struct ir_scancode_table *rc_tab,
			    unsigned scancode, unsigned keycode)
{
	unsigned int i;
	int old_keycode = KEY_RESERVED;

	/* First check if we already have a mapping for this ir command */
	for (i = 0; i < rc_tab->len; i++) {
		/* Keytable is sorted from lowest to highest scancode */
		if (rc_tab->scan[i].scancode > scancode)
			break;
		else if (rc_tab->scan[i].scancode < scancode)
			continue;

		old_keycode = rc_tab->scan[i].keycode;
		rc_tab->scan[i].keycode = keycode;

		/* Did the user wish to remove the mapping? */
		if (keycode == KEY_RESERVED || keycode == KEY_UNKNOWN) {
			IR_dprintk(1, "#%d: Deleting scan 0x%04x\n",
				   i, scancode);
			rc_tab->len--;
			memmove(&rc_tab->scan[i], &rc_tab->scan[i + 1],
				(rc_tab->len - i) * sizeof(struct ir_scancode));
		}

		/* Possibly shrink the keytable, failure is not a problem */
		ir_resize_table(rc_tab);
		break;
	}

	if (old_keycode == KEY_RESERVED) {
		/* No previous mapping found, we might need to grow the table */
		if (ir_resize_table(rc_tab))
			return -ENOMEM;

		IR_dprintk(1, "#%d: New scan 0x%04x with key 0x%04x\n",
			   i, scancode, keycode);

		/* i is the proper index to insert our new keycode */
		memmove(&rc_tab->scan[i + 1], &rc_tab->scan[i],
			(rc_tab->len - i) * sizeof(struct ir_scancode));
		rc_tab->scan[i].scancode = scancode;
		rc_tab->scan[i].keycode = keycode;
		rc_tab->len++;
		set_bit(keycode, dev->keybit);
	} else {
		IR_dprintk(1, "#%d: Replacing scan 0x%04x with key 0x%04x\n",
			   i, scancode, keycode);
		/* A previous mapping was updated... */
		clear_bit(old_keycode, dev->keybit);
		/* ...but another scancode might use the same keycode */
		for (i = 0; i < rc_tab->len; i++) {
			if (rc_tab->scan[i].keycode == old_keycode) {
				set_bit(old_keycode, dev->keybit);
				break;
			}
		}
	}

	return 0;
}

/**
 * ir_setkeycode() - set a keycode in the scancode->keycode table
 * @dev:	the struct input_dev device descriptor
 * @scancode:	the desired scancode
 * @keycode:	result
 * @return:	-EINVAL if the keycode could not be inserted, otherwise zero.
 *
 * This routine is used to handle evdev EVIOCSKEY ioctl.
 */
static int ir_setkeycode(struct input_dev *dev,
			 unsigned int scancode, unsigned int keycode)
{
	int rc;
	unsigned long flags;
	struct ir_input_dev *ir_dev = input_get_drvdata(dev);
	struct ir_scancode_table *rc_tab = &ir_dev->rc_tab;

	spin_lock_irqsave(&rc_tab->lock, flags);
	rc = ir_do_setkeycode(dev, rc_tab, scancode, keycode);
	spin_unlock_irqrestore(&rc_tab->lock, flags);
	return rc;
}

/**
 * ir_setkeytable() - sets several entries in the scancode->keycode table
 * @dev:	the struct input_dev device descriptor
 * @to:		the struct ir_scancode_table to copy entries to
 * @from:	the struct ir_scancode_table to copy entries from
 * @return:	-EINVAL if all keycodes could not be inserted, otherwise zero.
 *
 * This routine is used to handle table initialization.
 */
static int ir_setkeytable(struct input_dev *dev,
			  struct ir_scancode_table *to,
			  const struct ir_scancode_table *from)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(dev);
	struct ir_scancode_table *rc_tab = &ir_dev->rc_tab;
	unsigned long flags;
	unsigned int i;
	int rc = 0;

	spin_lock_irqsave(&rc_tab->lock, flags);
	for (i = 0; i < from->size; i++) {
		rc = ir_do_setkeycode(dev, to, from->scan[i].scancode,
				      from->scan[i].keycode);
		if (rc)
			break;
	}
	spin_unlock_irqrestore(&rc_tab->lock, flags);
	return rc;
}

/**
 * ir_getkeycode() - get a keycode from the scancode->keycode table
 * @dev:	the struct input_dev device descriptor
 * @scancode:	the desired scancode
 * @keycode:	used to return the keycode, if found, or KEY_RESERVED
 * @return:	always returns zero.
 *
 * This routine is used to handle evdev EVIOCGKEY ioctl.
 */
static int ir_getkeycode(struct input_dev *dev,
			 unsigned int scancode, unsigned int *keycode)
{
	int start, end, mid;
	unsigned long flags;
	int key = KEY_RESERVED;
	struct ir_input_dev *ir_dev = input_get_drvdata(dev);
	struct ir_scancode_table *rc_tab = &ir_dev->rc_tab;

	spin_lock_irqsave(&rc_tab->lock, flags);
	start = 0;
	end = rc_tab->len - 1;
	while (start <= end) {
		mid = (start + end) / 2;
		if (rc_tab->scan[mid].scancode < scancode)
			start = mid + 1;
		else if (rc_tab->scan[mid].scancode > scancode)
			end = mid - 1;
		else {
			key = rc_tab->scan[mid].keycode;
			break;
		}
	}
	spin_unlock_irqrestore(&rc_tab->lock, flags);

	if (key == KEY_RESERVED)
		IR_dprintk(1, "unknown key for scancode 0x%04x\n",
			   scancode);

	*keycode = key;
	return 0;
}

/**
 * ir_g_keycode_from_table() - gets the keycode that corresponds to a scancode
 * @input_dev:	the struct input_dev descriptor of the device
 * @scancode:	the scancode that we're seeking
 *
 * This routine is used by the input routines when a key is pressed at the
 * IR. The scancode is received and needs to be converted into a keycode.
 * If the key is not found, it returns KEY_RESERVED. Otherwise, returns the
 * corresponding keycode from the table.
 */
u32 ir_g_keycode_from_table(struct input_dev *dev, u32 scancode)
{
	int keycode;

	ir_getkeycode(dev, scancode, &keycode);
	if (keycode != KEY_RESERVED)
		IR_dprintk(1, "%s: scancode 0x%04x keycode 0x%02x\n",
			   dev->name, scancode, keycode);
	return keycode;
}
EXPORT_SYMBOL_GPL(ir_g_keycode_from_table);

/**
 * ir_keyup() - generates input event to cleanup a key press
 * @input_dev:	the struct input_dev descriptor of the device
 *
 * This routine is used by the input routines when a key is pressed at the
 * IR. It reports a keyup input event via input_report_key().
 */
void ir_keyup(struct input_dev *dev)
{
	struct ir_input_dev *ir = input_get_drvdata(dev);

	if (!ir->keypressed)
		return;

	IR_dprintk(1, "keyup key 0x%04x\n", ir->keycode);
	input_report_key(dev, ir->keycode, 0);
	input_sync(dev);
	ir->keypressed = 0;
}
EXPORT_SYMBOL_GPL(ir_keyup);

/**
 * ir_keydown() - generates input event for a key press
 * @input_dev:	the struct input_dev descriptor of the device
 * @scancode:	the scancode that we're seeking
 *
 * This routine is used by the input routines when a key is pressed at the
 * IR. It gets the keycode for a scancode and reports an input event via
 * input_report_key().
 */
void ir_keydown(struct input_dev *dev, int scancode)
{
	struct ir_input_dev *ir = input_get_drvdata(dev);

	u32 keycode = ir_g_keycode_from_table(dev, scancode);

	/* If already sent a keydown, do a keyup */
	if (ir->keypressed)
		ir_keyup(dev);

	if (KEY_RESERVED == keycode)
		return;

	ir->keycode = keycode;
	ir->keypressed = 1;

	IR_dprintk(1, "%s: key down event, key 0x%04x, scancode 0x%04x\n",
		dev->name, keycode, scancode);

	input_report_key(dev, ir->keycode, 1);
	input_sync(dev);

}
EXPORT_SYMBOL_GPL(ir_keydown);

static int ir_open(struct input_dev *input_dev)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);

	return ir_dev->props->open(ir_dev->props->priv);
}

static void ir_close(struct input_dev *input_dev)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);

	ir_dev->props->close(ir_dev->props->priv);
}

/**
 * __ir_input_register() - sets the IR keycode table and add the handlers
 *			    for keymap table get/set
 * @input_dev:	the struct input_dev descriptor of the device
 * @rc_tab:	the struct ir_scancode_table table of scancode/keymap
 *
 * This routine is used to initialize the input infrastructure
 * to work with an IR.
 * It will register the input/evdev interface for the device and
 * register the syfs code for IR class
 */
int __ir_input_register(struct input_dev *input_dev,
		      const struct ir_scancode_table *rc_tab,
		      const struct ir_dev_props *props,
		      const char *driver_name)
{
	struct ir_input_dev *ir_dev;
	int rc;

	if (rc_tab->scan == NULL || !rc_tab->size)
		return -EINVAL;

	ir_dev = kzalloc(sizeof(*ir_dev), GFP_KERNEL);
	if (!ir_dev)
		return -ENOMEM;

	ir_dev->driver_name = kasprintf(GFP_KERNEL, "%s", driver_name);
	if (!ir_dev->driver_name) {
		rc = -ENOMEM;
		goto out_dev;
	}

	input_dev->getkeycode = ir_getkeycode;
	input_dev->setkeycode = ir_setkeycode;
	input_set_drvdata(input_dev, ir_dev);

	spin_lock_init(&ir_dev->rc_tab.lock);
	ir_dev->rc_tab.name = rc_tab->name;
	ir_dev->rc_tab.ir_type = rc_tab->ir_type;
	ir_dev->rc_tab.alloc = roundup_pow_of_two(rc_tab->size *
						  sizeof(struct ir_scancode));
	ir_dev->rc_tab.scan = kmalloc(ir_dev->rc_tab.alloc, GFP_KERNEL);
	ir_dev->rc_tab.size = ir_dev->rc_tab.alloc / sizeof(struct ir_scancode);

	if (!ir_dev->rc_tab.scan) {
		rc = -ENOMEM;
		goto out_name;
	}

	IR_dprintk(1, "Allocated space for %u keycode entries (%u bytes)\n",
		   ir_dev->rc_tab.size, ir_dev->rc_tab.alloc);

	set_bit(EV_KEY, input_dev->evbit);
	if (ir_setkeytable(input_dev, &ir_dev->rc_tab, rc_tab)) {
		rc = -ENOMEM;
		goto out_table;
	}

	ir_dev->props = props;
	if (props && props->open)
		input_dev->open = ir_open;
	if (props && props->close)
		input_dev->close = ir_close;

	rc = ir_register_class(input_dev);
	if (rc < 0)
		goto out_table;

	IR_dprintk(1, "Registered input device on %s for %s remote.\n",
		   driver_name, rc_tab->name);

	return 0;

out_table:
	kfree(ir_dev->rc_tab.scan);
out_name:
	kfree(ir_dev->driver_name);
out_dev:
	kfree(ir_dev);
	return rc;
}
EXPORT_SYMBOL_GPL(__ir_input_register);

/**
 * ir_input_unregister() - unregisters IR and frees resources
 * @input_dev:	the struct input_dev descriptor of the device

 * This routine is used to free memory and de-register interfaces.
 */
void ir_input_unregister(struct input_dev *dev)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(dev);
	struct ir_scancode_table *rc_tab;

	if (!ir_dev)
		return;

	IR_dprintk(1, "Freed keycode table\n");

	rc_tab = &ir_dev->rc_tab;
	rc_tab->size = 0;
	kfree(rc_tab->scan);
	rc_tab->scan = NULL;

	ir_unregister_class(dev);

	kfree(ir_dev->driver_name);
	kfree(ir_dev);
}
EXPORT_SYMBOL_GPL(ir_input_unregister);

int ir_core_debug;    /* ir_debug level (0,1,2) */
EXPORT_SYMBOL_GPL(ir_core_debug);
module_param_named(debug, ir_core_debug, int, 0644);

MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_LICENSE("GPL");
