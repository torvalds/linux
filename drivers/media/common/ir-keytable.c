/* ir-register.c - handle IR scancode->keycode tables
 *
 * Copyright (C) 2009 by Mauro Carvalho Chehab <mchehab@redhat.com>
 */

#include <linux/usb/input.h>

#include <media/ir-common.h>

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
	int i;
	struct ir_scancode_table *rc_tab = input_get_drvdata(dev);
	struct ir_scancode *keymap = rc_tab->scan;

	/* See if we can match the raw key code. */
	for (i = 0; i < rc_tab->size; i++)
		if (keymap[i].scancode == scancode) {
			*keycode = keymap[i].keycode;
			return 0;
		}

	/*
	 * If is there extra space, returns KEY_RESERVED,
	 * otherwise, input core won't let ir_setkeycode
	 * to work
	 */
	for (i = 0; i < rc_tab->size; i++)
		if (keymap[i].keycode == KEY_RESERVED ||
		    keymap[i].keycode == KEY_UNKNOWN) {
			*keycode = KEY_RESERVED;
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
	int i;
	struct ir_scancode_table *rc_tab = input_get_drvdata(dev);
	struct ir_scancode *keymap = rc_tab->scan;

	/* Search if it is replacing an existing keycode */
	for (i = 0; i < rc_tab->size; i++)
		if (keymap[i].scancode == scancode) {
			keymap[i].keycode = keycode;
			return 0;
		}

	/* Search if is there a clean entry. If so, use it */
	for (i = 0; i < rc_tab->size; i++)
		if (keymap[i].keycode == KEY_RESERVED ||
		    keymap[i].keycode == KEY_UNKNOWN) {
			keymap[i].scancode = scancode;
			keymap[i].keycode = keycode;
			return 0;
		}

	/*
	 * FIXME: Currently, it is not possible to increase the size of
	 * scancode table. For it to happen, one possibility
	 * would be to allocate a table with key_map_size + 1,
	 * copying data, appending the new key on it, and freeing
	 * the old one - or maybe just allocating some spare space
	 */

	return -EINVAL;
}

/**
 * ir_g_keycode_from_table() - gets the keycode that corresponds to a scancode
 * @rc_tab:	the ir_scancode_table with the keymap to be used
 * @scancode:	the scancode that we're seeking
 *
 * This routine is used by the input routines when a key is pressed at the
 * IR. The scancode is received and needs to be converted into a keycode.
 * If the key is not found, it returns KEY_UNKNOWN. Otherwise, returns the
 * corresponding keycode from the table.
 */
u32 ir_g_keycode_from_table(struct input_dev *dev, u32 scancode)
{
	int i;
	struct ir_scancode_table *rc_tab = input_get_drvdata(dev);
	struct ir_scancode *keymap = rc_tab->scan;

	for (i = 0; i < rc_tab->size; i++)
		if (keymap[i].scancode == scancode) {
			IR_dprintk(1, "%s: scancode 0x%04x keycode 0x%02x\n",
				dev->name, scancode, keymap[i].keycode);

			return keymap[i].keycode;
		}

	printk(KERN_INFO "%s: unknown key for scancode 0x%04x\n",
	       dev->name, scancode);

	return KEY_UNKNOWN;
}
EXPORT_SYMBOL_GPL(ir_g_keycode_from_table);

/**
 * ir_set_keycode_table() - sets the IR keycode table and add the handlers
 *			    for keymap table get/set
 * @input_dev:	the struct input_dev descriptor of the device
 * @rc_tab:	the struct ir_scancode_table table of scancode/keymap
 *
 * This routine is used to initialize the input infrastructure to work with
 * an IR. It requires that the caller initializes the input_dev struct with
 * some fields: name,
 */
int ir_set_keycode_table(struct input_dev *input_dev,
			 struct ir_scancode_table *rc_tab)
{
	struct ir_scancode *keymap = rc_tab->scan;
	int i;

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
EXPORT_SYMBOL_GPL(ir_set_keycode_table);
