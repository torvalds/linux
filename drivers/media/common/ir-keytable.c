/* ir-register.c - handle IR scancode->keycode tables
 *
 * Copyright (C) 2009 by Mauro Carvalho Chehab <mchehab@redhat.com>
 */

#include <linux/usb/input.h>

#include <media/ir-common.h>

#define IR_TAB_MIN_SIZE	32

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
	struct ir_scancode_table *rc_tab = input_get_drvdata(dev);

	elem = ir_seek_table(rc_tab, scancode);
	if (elem >= 0) {
		*keycode = rc_tab->scan[elem].keycode;
		return 0;
	}

	return -EINVAL;
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
	struct ir_scancode_table *rc_tab = input_get_drvdata(dev);
	struct ir_scancode *keymap = rc_tab->scan;
	unsigned long flags;

	/* Search if it is replacing an existing keycode */
	rc = ir_seek_table(rc_tab, scancode);
	if (rc <0)
		return rc;

	IR_dprintk(1, "#%d: Replacing scan 0x%04x with key 0x%04x\n",
		rc, scancode, keycode);

	clear_bit(keymap[rc].keycode, dev->keybit);

	spin_lock_irqsave(&rc_tab->lock, flags);
	keymap[rc].keycode = keycode;
	spin_unlock_irqrestore(&rc_tab->lock, flags);

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
	struct ir_scancode_table *rc_tab = input_get_drvdata(dev);
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

/**
 * ir_set_keycode_table() - sets the IR keycode table and add the handlers
 *			    for keymap table get/set
 * @input_dev:	the struct input_dev descriptor of the device
 * @rc_tab:	the struct ir_scancode_table table of scancode/keymap
 *
 * This routine is used to initialize the input infrastructure to work with
 * an IR.
 * It should be called before registering the IR device.
 */
int ir_set_keycode_table(struct input_dev *input_dev,
			 struct ir_scancode_table *rc_tab)
{
	struct ir_scancode *keymap = rc_tab->scan;
	int i;

	spin_lock_init(&rc_tab->lock);

	if (rc_tab->scan == NULL || !rc_tab->size)
		return -EINVAL;

	/* set the bits for the keys */
	IR_dprintk(1, "key map size: %d\n", rc_tab->size);
	for (i = 0; i < rc_tab->size; i++) {
		IR_dprintk(1, "#%d: setting bit for keycode 0x%04x\n",
			i, keymap[i].keycode);
		set_bit(keymap[i].keycode, input_dev->keybit);
	}

	input_dev->getkeycode = ir_getkeycode;
	input_dev->setkeycode = ir_setkeycode;
	input_set_drvdata(input_dev, rc_tab);

	return 0;
}

void ir_input_free(struct input_dev *dev)
{
	struct ir_scancode_table *rc_tab = input_get_drvdata(dev);

	IR_dprintk(1, "Freed keycode table\n");

	rc_tab->size = 0;
	kfree(rc_tab->scan);
	rc_tab->scan = NULL;
}
EXPORT_SYMBOL_GPL(ir_input_free);

