/*
 * ChromeOS EC keyboard driver
 *
 * Copyright (C) 2012 Google, Inc
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
 * This driver uses the Chrome OS EC byte-level message-based protocol for
 * communicating the keyboard state (which keys are pressed) from a keyboard EC
 * to the AP over some bus (such as i2c, lpc, spi).  The EC does debouncing,
 * but everything else (including deghosting) is done here.  The main
 * motivation for this is to keep the EC firmware as simple as possible, since
 * it cannot be easily upgraded and EC flash/IRAM space is relatively
 * expensive.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/input/matrix_keypad.h>
#include <linux/mfd/cros_ec.h>
#include <linux/mfd/cros_ec_commands.h>

/*
 * @rows: Number of rows in the keypad
 * @cols: Number of columns in the keypad
 * @row_shift: log2 or number of rows, rounded up
 * @keymap_data: Matrix keymap data used to convert to keyscan values
 * @ghost_filter: true to enable the matrix key-ghosting filter
 * @old_kb_state: bitmap of keys pressed last scan
 * @dev: Device pointer
 * @idev: Input device
 * @ec: Top level ChromeOS device to use to talk to EC
 * @event_notifier: interrupt event notifier for transport devices
 */
struct cros_ec_keyb {
	unsigned int rows;
	unsigned int cols;
	int row_shift;
	const struct matrix_keymap_data *keymap_data;
	bool ghost_filter;
	uint8_t *old_kb_state;

	struct device *dev;
	struct input_dev *idev;
	struct cros_ec_device *ec;
	struct notifier_block notifier;
};


static bool cros_ec_keyb_row_has_ghosting(struct cros_ec_keyb *ckdev,
					  uint8_t *buf, int row)
{
	int pressed_in_row = 0;
	int row_has_teeth = 0;
	int col, mask;

	mask = 1 << row;
	for (col = 0; col < ckdev->cols; col++) {
		if (buf[col] & mask) {
			pressed_in_row++;
			row_has_teeth |= buf[col] & ~mask;
			if (pressed_in_row > 1 && row_has_teeth) {
				/* ghosting */
				dev_dbg(ckdev->dev,
					"ghost found at: r%d c%d, pressed %d, teeth 0x%x\n",
					row, col, pressed_in_row,
					row_has_teeth);
				return true;
			}
		}
	}

	return false;
}

/*
 * Returns true when there is at least one combination of pressed keys that
 * results in ghosting.
 */
static bool cros_ec_keyb_has_ghosting(struct cros_ec_keyb *ckdev, uint8_t *buf)
{
	int row;

	/*
	 * Ghosting happens if for any pressed key X there are other keys
	 * pressed both in the same row and column of X as, for instance,
	 * in the following diagram:
	 *
	 * . . Y . g .
	 * . . . . . .
	 * . . . . . .
	 * . . X . Z .
	 *
	 * In this case only X, Y, and Z are pressed, but g appears to be
	 * pressed too (see Wikipedia).
	 *
	 * We can detect ghosting in a single pass (*) over the keyboard state
	 * by maintaining two arrays.  pressed_in_row counts how many pressed
	 * keys we have found in a row.  row_has_teeth is true if any of the
	 * pressed keys for this row has other pressed keys in its column.  If
	 * at any point of the scan we find that a row has multiple pressed
	 * keys, and at least one of them is at the intersection with a column
	 * with multiple pressed keys, we're sure there is ghosting.
	 * Conversely, if there is ghosting, we will detect such situation for
	 * at least one key during the pass.
	 *
	 * (*) This looks linear in the number of keys, but it's not.  We can
	 * cheat because the number of rows is small.
	 */
	for (row = 0; row < ckdev->rows; row++)
		if (cros_ec_keyb_row_has_ghosting(ckdev, buf, row))
			return true;

	return false;
}

/*
 * Compares the new keyboard state to the old one and produces key
 * press/release events accordingly.  The keyboard state is 13 bytes (one byte
 * per column)
 */
static void cros_ec_keyb_process(struct cros_ec_keyb *ckdev,
			 uint8_t *kb_state, int len)
{
	struct input_dev *idev = ckdev->idev;
	int col, row;
	int new_state;
	int old_state;
	int num_cols;

	num_cols = len;

	if (ckdev->ghost_filter && cros_ec_keyb_has_ghosting(ckdev, kb_state)) {
		/*
		 * Simple-minded solution: ignore this state. The obvious
		 * improvement is to only ignore changes to keys involved in
		 * the ghosting, but process the other changes.
		 */
		dev_dbg(ckdev->dev, "ghosting found\n");
		return;
	}

	for (col = 0; col < ckdev->cols; col++) {
		for (row = 0; row < ckdev->rows; row++) {
			int pos = MATRIX_SCAN_CODE(row, col, ckdev->row_shift);
			const unsigned short *keycodes = idev->keycode;

			new_state = kb_state[col] & (1 << row);
			old_state = ckdev->old_kb_state[col] & (1 << row);
			if (new_state != old_state) {
				dev_dbg(ckdev->dev,
					"changed: [r%d c%d]: byte %02x\n",
					row, col, new_state);

				input_report_key(idev, keycodes[pos],
						 new_state);
			}
		}
		ckdev->old_kb_state[col] = kb_state[col];
	}
	input_sync(ckdev->idev);
}

static int cros_ec_keyb_open(struct input_dev *dev)
{
	struct cros_ec_keyb *ckdev = input_get_drvdata(dev);

	return blocking_notifier_chain_register(&ckdev->ec->event_notifier,
						&ckdev->notifier);
}

static void cros_ec_keyb_close(struct input_dev *dev)
{
	struct cros_ec_keyb *ckdev = input_get_drvdata(dev);

	blocking_notifier_chain_unregister(&ckdev->ec->event_notifier,
					   &ckdev->notifier);
}

static int cros_ec_keyb_get_state(struct cros_ec_keyb *ckdev, uint8_t *kb_state)
{
	return ckdev->ec->command_recv(ckdev->ec, EC_CMD_MKBP_STATE,
					  kb_state, ckdev->cols);
}

static int cros_ec_keyb_work(struct notifier_block *nb,
		     unsigned long state, void *_notify)
{
	int ret;
	struct cros_ec_keyb *ckdev = container_of(nb, struct cros_ec_keyb,
						    notifier);
	uint8_t kb_state[ckdev->cols];

	ret = cros_ec_keyb_get_state(ckdev, kb_state);
	if (ret >= 0)
		cros_ec_keyb_process(ckdev, kb_state, ret);

	return NOTIFY_DONE;
}

static int cros_ec_keyb_probe(struct platform_device *pdev)
{
	struct cros_ec_device *ec = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = ec->dev;
	struct cros_ec_keyb *ckdev;
	struct input_dev *idev;
	struct device_node *np;
	int err;

	np = pdev->dev.of_node;
	if (!np)
		return -ENODEV;

	ckdev = devm_kzalloc(&pdev->dev, sizeof(*ckdev), GFP_KERNEL);
	if (!ckdev)
		return -ENOMEM;
	err = matrix_keypad_parse_of_params(&pdev->dev, &ckdev->rows,
					    &ckdev->cols);
	if (err)
		return err;
	ckdev->old_kb_state = devm_kzalloc(&pdev->dev, ckdev->cols, GFP_KERNEL);
	if (!ckdev->old_kb_state)
		return -ENOMEM;

	idev = devm_input_allocate_device(&pdev->dev);
	if (!idev)
		return -ENOMEM;

	ckdev->ec = ec;
	ckdev->notifier.notifier_call = cros_ec_keyb_work;
	ckdev->dev = dev;
	dev_set_drvdata(&pdev->dev, ckdev);

	idev->name = ec->ec_name;
	idev->phys = ec->phys_name;
	__set_bit(EV_REP, idev->evbit);

	idev->id.bustype = BUS_VIRTUAL;
	idev->id.version = 1;
	idev->id.product = 0;
	idev->dev.parent = &pdev->dev;
	idev->open = cros_ec_keyb_open;
	idev->close = cros_ec_keyb_close;

	ckdev->ghost_filter = of_property_read_bool(np,
					"google,needs-ghost-filter");

	err = matrix_keypad_build_keymap(NULL, NULL, ckdev->rows, ckdev->cols,
					 NULL, idev);
	if (err) {
		dev_err(dev, "cannot build key matrix\n");
		return err;
	}

	ckdev->row_shift = get_count_order(ckdev->cols);

	input_set_capability(idev, EV_MSC, MSC_SCAN);
	input_set_drvdata(idev, ckdev);
	ckdev->idev = idev;
	err = input_register_device(ckdev->idev);
	if (err) {
		dev_err(dev, "cannot register input device\n");
		return err;
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
/* Clear any keys in the buffer */
static void cros_ec_keyb_clear_keyboard(struct cros_ec_keyb *ckdev)
{
	uint8_t old_state[ckdev->cols];
	uint8_t new_state[ckdev->cols];
	unsigned long duration;
	int i, ret;

	/*
	 * Keep reading until we see that the scan state does not change.
	 * That indicates that we are done.
	 *
	 * Assume that the EC keyscan buffer is at most 32 deep.
	 */
	duration = jiffies;
	ret = cros_ec_keyb_get_state(ckdev, new_state);
	for (i = 1; !ret && i < 32; i++) {
		memcpy(old_state, new_state, sizeof(old_state));
		ret = cros_ec_keyb_get_state(ckdev, new_state);
		if (0 == memcmp(old_state, new_state, sizeof(old_state)))
			break;
	}
	duration = jiffies - duration;
	dev_info(ckdev->dev, "Discarded %d keyscan(s) in %dus\n", i,
		jiffies_to_usecs(duration));
}

static int cros_ec_keyb_resume(struct device *dev)
{
	struct cros_ec_keyb *ckdev = dev_get_drvdata(dev);

	/*
	 * When the EC is not a wake source, then it could not have caused the
	 * resume, so we clear the EC's key scan buffer. If the EC was a
	 * wake source (e.g. the lid is open and the user might press a key to
	 * wake) then the key scan buffer should be preserved.
	 */
	if (ckdev->ec->was_wake_device)
		cros_ec_keyb_clear_keyboard(ckdev);

	return 0;
}

#endif

static SIMPLE_DEV_PM_OPS(cros_ec_keyb_pm_ops, NULL, cros_ec_keyb_resume);

static struct platform_driver cros_ec_keyb_driver = {
	.probe = cros_ec_keyb_probe,
	.driver = {
		.name = "cros-ec-keyb",
		.pm	= &cros_ec_keyb_pm_ops,
	},
};

module_platform_driver(cros_ec_keyb_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ChromeOS EC keyboard driver");
MODULE_ALIAS("platform:cros-ec-keyb");
