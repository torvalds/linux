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
#include <media/ir-common.h>

#define IR_TAB_MIN_SIZE	32
#define IR_TAB_MAX_SIZE	1024

/**
 * ir_seek_table() - returns the element order on the table
 * @rc_tab:	the ir_scancode_table with the keymap to be used
 * @scancode:	the scancode that we're seeking
 *
 * This routine is used by the input routines when a key is pressed at the
 * IR. The scancode is received and needs to be converted into a keycode.
 * If the key is not found, it returns KEY_UNKNOWN. Otherwise, returns the
 * corresponding keycode from the table.
 */
static int ir_seek_table(struct ir_scancode_table *rc_tab, u32 scancode)
{
	int rc;
	unsigned long flags;
	struct ir_scancode *keymap = rc_tab->scan;

	spin_lock_irqsave(&rc_tab->lock, flags);

	/* FIXME: replace it by a binary search */

	for (rc = 0; rc < rc_tab->size; rc++)
		if (keymap[rc].scancode == scancode)
			goto exit;

	/* Not found */
	rc = -EINVAL;

exit:
	spin_unlock_irqrestore(&rc_tab->lock, flags);
	return rc;
}

/**
 * ir_roundup_tablesize() - gets an optimum value for the table size
 * @n_elems:		minimum number of entries to store keycodes
 *
 * This routine is used to choose the keycode table size.
 *
 * In order to have some empty space for new keycodes,
 * and knowing in advance that kmalloc allocates only power of two
 * segments, it optimizes the allocated space to have some spare space
 * for those new keycodes by using the maximum number of entries that
 * will be effectively be allocated by kmalloc.
 * In order to reduce the quantity of table resizes, it has a minimum
 * table size of IR_TAB_MIN_SIZE.
 */
int ir_roundup_tablesize(int n_elems)
{
	size_t size;

	if (n_elems < IR_TAB_MIN_SIZE)
		n_elems = IR_TAB_MIN_SIZE;

	/*
	 * As kmalloc only allocates sizes of power of two, get as
	 * much entries as possible for the allocated memory segment
	 */
	size = roundup_pow_of_two(n_elems * sizeof(struct ir_scancode));
	n_elems = size / sizeof(struct ir_scancode);

	return n_elems;
}
EXPORT_SYMBOL_GPL(ir_roundup_tablesize);

/**
 * ir_copy_table() - copies a keytable, discarding the unused entries
 * @destin:	destin table
 * @origin:	origin table
 *
 * Copies all entries where the keycode is not KEY_UNKNOWN/KEY_RESERVED
 */

int ir_copy_table(struct ir_scancode_table *destin,
		 const struct ir_scancode_table *origin)
{
	int i, j = 0;

	for (i = 0; i < origin->size; i++) {
		if (origin->scan[i].keycode == KEY_UNKNOWN ||
		   origin->scan[i].keycode == KEY_RESERVED)
			continue;

		memcpy(&destin->scan[j], &origin->scan[i], sizeof(struct ir_scancode));
		j++;
	}
	destin->size = j;

	IR_dprintk(1, "Copied %d scancodes to the new keycode table\n", destin->size);

	return 0;
}
EXPORT_SYMBOL_GPL(ir_copy_table);

/**
 * ir_getkeycode() - get a keycode at the evdev scancode ->keycode table
 * @dev:	the struct input_dev device descriptor
 * @scancode:	the desired scancode
 * @keycode:	the keycode to be retorned.
 *
 * This routine is used to handle evdev EVIOCGKEY ioctl.
 * If the key is not found, returns -EINVAL, otherwise, returns 0.
 */
static int ir_getkeycode(struct input_dev *dev,
			 int scancode, int *keycode)
{
	int elem;
	struct ir_input_dev *ir_dev = input_get_drvdata(dev);
	struct ir_scancode_table *rc_tab = &ir_dev->rc_tab;

	elem = ir_seek_table(rc_tab, scancode);
	if (elem >= 0) {
		*keycode = rc_tab->scan[elem].keycode;
		return 0;
	}

	/*
	 * Scancode not found and table can't be expanded
	 */
	if (elem < 0 && rc_tab->size == IR_TAB_MAX_SIZE)
		return -EINVAL;

	/*
	 * If is there extra space, returns KEY_RESERVED,
	 * otherwise, input core won't let ir_setkeycode to work
	 */
	*keycode = KEY_RESERVED;
	return 0;
}

/**
 * ir_is_resize_needed() - Check if the table needs rezise
 * @table:		keycode table that may need to resize
 * @n_elems:		minimum number of entries to store keycodes
 *
 * Considering that kmalloc uses power of two storage areas, this
 * routine detects if the real alloced size will change. If not, it
 * just returns without doing nothing. Otherwise, it will extend or
 * reduce the table size to meet the new needs.
 *
 * It returns 0 if no resize is needed, 1 otherwise.
 */
static int ir_is_resize_needed(struct ir_scancode_table *table, int n_elems)
{
	int cur_size = ir_roundup_tablesize(table->size);
	int new_size = ir_roundup_tablesize(n_elems);

	if (cur_size == new_size)
		return 0;

	/* Resize is needed */
	return 1;
}

/**
 * ir_delete_key() - remove a keycode from the table
 * @rc_tab:		keycode table
 * @elem:		element to be removed
 *
 */
static void ir_delete_key(struct ir_scancode_table *rc_tab, int elem)
{
	unsigned long flags = 0;
	int newsize = rc_tab->size - 1;
	int resize = ir_is_resize_needed(rc_tab, newsize);
	struct ir_scancode *oldkeymap = rc_tab->scan;
	struct ir_scancode *newkeymap;

	if (resize) {
		newkeymap = kzalloc(ir_roundup_tablesize(newsize) *
				    sizeof(*newkeymap), GFP_ATOMIC);

		/* There's no memory for resize. Keep the old table */
		if (!newkeymap)
			resize = 0;
	}

	if (!resize) {
		newkeymap = oldkeymap;

		/* We'll modify the live table. Lock it */
		spin_lock_irqsave(&rc_tab->lock, flags);
	}

	/*
	 * Copy the elements before the one that will be deleted
	 * if (!resize), both oldkeymap and newkeymap points
	 * to the same place, so, there's no need to copy
	 */
	if (resize && elem > 0)
		memcpy(newkeymap, oldkeymap,
		       elem * sizeof(*newkeymap));

	/*
	 * Copy the other elements overwriting the element to be removed
	 * This operation applies to both resize and non-resize case
	 */
	if (elem < newsize)
		memcpy(&newkeymap[elem], &oldkeymap[elem + 1],
		       (newsize - elem) * sizeof(*newkeymap));

	if (resize) {
		/*
		 * As the copy happened to a temporary table, only here
		 * it needs to lock while replacing the table pointers
		 * to use the new table
		 */
		spin_lock_irqsave(&rc_tab->lock, flags);
		rc_tab->size = newsize;
		rc_tab->scan = newkeymap;
		spin_unlock_irqrestore(&rc_tab->lock, flags);

		/* Frees the old keytable */
		kfree(oldkeymap);
	} else {
		rc_tab->size = newsize;
		spin_unlock_irqrestore(&rc_tab->lock, flags);
	}
}

/**
 * ir_insert_key() - insert a keycode at the table
 * @rc_tab:		keycode table
 * @scancode:	the desired scancode
 * @keycode:	the keycode to be retorned.
 *
 */
static int ir_insert_key(struct ir_scancode_table *rc_tab,
			  int scancode, int keycode)
{
	unsigned long flags;
	int elem = rc_tab->size;
	int newsize = rc_tab->size + 1;
	int resize = ir_is_resize_needed(rc_tab, newsize);
	struct ir_scancode *oldkeymap = rc_tab->scan;
	struct ir_scancode *newkeymap;

	if (resize) {
		newkeymap = kzalloc(ir_roundup_tablesize(newsize) *
				    sizeof(*newkeymap), GFP_ATOMIC);
		if (!newkeymap)
			return -ENOMEM;

		memcpy(newkeymap, oldkeymap,
		       rc_tab->size * sizeof(*newkeymap));
	} else
		newkeymap  = oldkeymap;

	/* Stores the new code at the table */
	IR_dprintk(1, "#%d: New scan 0x%04x with key 0x%04x\n",
		   rc_tab->size, scancode, keycode);

	spin_lock_irqsave(&rc_tab->lock, flags);
	rc_tab->size = newsize;
	if (resize) {
		rc_tab->scan = newkeymap;
		kfree(oldkeymap);
	}
	newkeymap[elem].scancode = scancode;
	newkeymap[elem].keycode  = keycode;
	spin_unlock_irqrestore(&rc_tab->lock, flags);

	return 0;
}

/**
 * ir_setkeycode() - set a keycode at the evdev scancode ->keycode table
 * @dev:	the struct input_dev device descriptor
 * @scancode:	the desired scancode
 * @keycode:	the keycode to be retorned.
 *
 * This routine is used to handle evdev EVIOCSKEY ioctl.
 * There's one caveat here: how can we increase the size of the table?
 * If the key is not found, returns -EINVAL, otherwise, returns 0.
 */
static int ir_setkeycode(struct input_dev *dev,
			 int scancode, int keycode)
{
	int rc = 0;
	struct ir_input_dev *ir_dev = input_get_drvdata(dev);
	struct ir_scancode_table *rc_tab = &ir_dev->rc_tab;
	struct ir_scancode *keymap = rc_tab->scan;
	unsigned long flags;

	/*
	 * Handle keycode table deletions
	 *
	 * If userspace is adding a KEY_UNKNOWN or KEY_RESERVED,
	 * deal as a trial to remove an existing scancode attribution
	 * if table become too big, reduce it to save space
	 */
	if (keycode == KEY_UNKNOWN || keycode == KEY_RESERVED) {
		rc = ir_seek_table(rc_tab, scancode);
		if (rc < 0)
			return 0;

		IR_dprintk(1, "#%d: Deleting scan 0x%04x\n", rc, scancode);
		clear_bit(keymap[rc].keycode, dev->keybit);
		ir_delete_key(rc_tab, rc);

		return 0;
	}

	/*
	 * Handle keycode replacements
	 *
	 * If the scancode exists, just replace by the new value
	 */
	rc = ir_seek_table(rc_tab, scancode);
	if (rc >= 0) {
		IR_dprintk(1, "#%d: Replacing scan 0x%04x with key 0x%04x\n",
			rc, scancode, keycode);

		clear_bit(keymap[rc].keycode, dev->keybit);

		spin_lock_irqsave(&rc_tab->lock, flags);
		keymap[rc].keycode = keycode;
		spin_unlock_irqrestore(&rc_tab->lock, flags);

		set_bit(keycode, dev->keybit);

		return 0;
	}

	/*
	 * Handle new scancode inserts
	 *
	 * reallocate table if needed and insert a new keycode
	 */

	/* Avoid growing the table indefinitely */
	if (rc_tab->size + 1 > IR_TAB_MAX_SIZE)
		return -EINVAL;

	rc = ir_insert_key(rc_tab, scancode, keycode);
	if (rc < 0)
		return rc;
	set_bit(keycode, dev->keybit);

	return 0;
}

/**
 * ir_g_keycode_from_table() - gets the keycode that corresponds to a scancode
 * @input_dev:	the struct input_dev descriptor of the device
 * @scancode:	the scancode that we're seeking
 *
 * This routine is used by the input routines when a key is pressed at the
 * IR. The scancode is received and needs to be converted into a keycode.
 * If the key is not found, it returns KEY_UNKNOWN. Otherwise, returns the
 * corresponding keycode from the table.
 */
u32 ir_g_keycode_from_table(struct input_dev *dev, u32 scancode)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(dev);
	struct ir_scancode_table *rc_tab = &ir_dev->rc_tab;
	struct ir_scancode *keymap = rc_tab->scan;
	int elem;

	elem = ir_seek_table(rc_tab, scancode);
	if (elem >= 0) {
		IR_dprintk(1, "%s: scancode 0x%04x keycode 0x%02x\n",
			   dev->name, scancode, keymap[elem].keycode);

		return rc_tab->scan[elem].keycode;
	}

	printk(KERN_INFO "%s: unknown key for scancode 0x%04x\n",
	       dev->name, scancode);

	/* Reports userspace that an unknown keycode were got */
	return KEY_RESERVED;
}
EXPORT_SYMBOL_GPL(ir_g_keycode_from_table);

/**
 * ir_input_register() - sets the IR keycode table and add the handlers
 *			    for keymap table get/set
 * @input_dev:	the struct input_dev descriptor of the device
 * @rc_tab:	the struct ir_scancode_table table of scancode/keymap
 *
 * This routine is used to initialize the input infrastructure to work with
 * an IR.
 * It should be called before registering the IR device.
 */
int ir_input_register(struct input_dev *input_dev,
		      struct ir_scancode_table *rc_tab)
{
	struct ir_input_dev *ir_dev;
	struct ir_scancode  *keymap    = rc_tab->scan;
	int i, rc;

	if (rc_tab->scan == NULL || !rc_tab->size)
		return -EINVAL;

	ir_dev = kzalloc(sizeof(*ir_dev), GFP_KERNEL);
	if (!ir_dev)
		return -ENOMEM;

	spin_lock_init(&rc_tab->lock);

	ir_dev->rc_tab.size = ir_roundup_tablesize(rc_tab->size);
	ir_dev->rc_tab.scan = kzalloc(ir_dev->rc_tab.size *
				    sizeof(struct ir_scancode), GFP_KERNEL);
	if (!ir_dev->rc_tab.scan)
		return -ENOMEM;

	IR_dprintk(1, "Allocated space for %d keycode entries (%zd bytes)\n",
		ir_dev->rc_tab.size,
		ir_dev->rc_tab.size * sizeof(ir_dev->rc_tab.scan));

	ir_copy_table(&ir_dev->rc_tab, rc_tab);

	/* set the bits for the keys */
	IR_dprintk(1, "key map size: %d\n", rc_tab->size);
	for (i = 0; i < rc_tab->size; i++) {
		IR_dprintk(1, "#%d: setting bit for keycode 0x%04x\n",
			i, keymap[i].keycode);
		set_bit(keymap[i].keycode, input_dev->keybit);
	}
	clear_bit(0, input_dev->keybit);

	set_bit(EV_KEY, input_dev->evbit);

	input_dev->getkeycode = ir_getkeycode;
	input_dev->setkeycode = ir_setkeycode;
	input_set_drvdata(input_dev, ir_dev);

	rc = input_register_device(input_dev);
	if (rc < 0) {
		kfree(rc_tab->scan);
		kfree(ir_dev);
		input_set_drvdata(input_dev, NULL);
	}

	return rc;
}
EXPORT_SYMBOL_GPL(ir_input_register);

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

	kfree(ir_dev);
	input_unregister_device(dev);
}
EXPORT_SYMBOL_GPL(ir_input_unregister);

int ir_core_debug;    /* ir_debug level (0,1,2) */
EXPORT_SYMBOL_GPL(ir_core_debug);
module_param_named(debug, ir_core_debug, int, 0644);

MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_LICENSE("GPL");
