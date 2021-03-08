// SPDX-License-Identifier: GPL-2.0
/*
 * HT16K33 driver
 *
 * Author: Robin van der Gracht <robin@protonic.nl>
 *
 * Copyright: (C) 2016 Protonic Holland.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/fb.h>
#include <linux/slab.h>
#include <linux/backlight.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/workqueue.h>
#include <linux/mm.h>

/* Registers */
#define REG_SYSTEM_SETUP		0x20
#define REG_SYSTEM_SETUP_OSC_ON		BIT(0)

#define REG_DISPLAY_SETUP		0x80
#define REG_DISPLAY_SETUP_ON		BIT(0)

#define REG_ROWINT_SET			0xA0
#define REG_ROWINT_SET_INT_EN		BIT(0)
#define REG_ROWINT_SET_INT_ACT_HIGH	BIT(1)

#define REG_BRIGHTNESS			0xE0

/* Defines */
#define DRIVER_NAME			"ht16k33"

#define MIN_BRIGHTNESS			0x1
#define MAX_BRIGHTNESS			0x10

#define HT16K33_MATRIX_LED_MAX_COLS	8
#define HT16K33_MATRIX_LED_MAX_ROWS	16
#define HT16K33_MATRIX_KEYPAD_MAX_COLS	3
#define HT16K33_MATRIX_KEYPAD_MAX_ROWS	12

#define BYTES_PER_ROW		(HT16K33_MATRIX_LED_MAX_ROWS / 8)
#define HT16K33_FB_SIZE		(HT16K33_MATRIX_LED_MAX_COLS * BYTES_PER_ROW)

struct ht16k33_keypad {
	struct i2c_client *client;
	struct input_dev *dev;
	uint32_t cols;
	uint32_t rows;
	uint32_t row_shift;
	uint32_t debounce_ms;
	uint16_t last_key_state[HT16K33_MATRIX_KEYPAD_MAX_COLS];

	wait_queue_head_t wait;
	bool stopped;
};

struct ht16k33_fbdev {
	struct fb_info *info;
	uint32_t refresh_rate;
	uint8_t *buffer;
	uint8_t *cache;
	struct delayed_work work;
};

struct ht16k33_priv {
	struct i2c_client *client;
	struct ht16k33_keypad keypad;
	struct ht16k33_fbdev fbdev;
};

static const struct fb_fix_screeninfo ht16k33_fb_fix = {
	.id		= DRIVER_NAME,
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_MONO10,
	.xpanstep	= 0,
	.ypanstep	= 0,
	.ywrapstep	= 0,
	.line_length	= HT16K33_MATRIX_LED_MAX_ROWS,
	.accel		= FB_ACCEL_NONE,
};

static const struct fb_var_screeninfo ht16k33_fb_var = {
	.xres = HT16K33_MATRIX_LED_MAX_ROWS,
	.yres = HT16K33_MATRIX_LED_MAX_COLS,
	.xres_virtual = HT16K33_MATRIX_LED_MAX_ROWS,
	.yres_virtual = HT16K33_MATRIX_LED_MAX_COLS,
	.bits_per_pixel = 1,
	.red = { 0, 1, 0 },
	.green = { 0, 1, 0 },
	.blue = { 0, 1, 0 },
	.left_margin = 0,
	.right_margin = 0,
	.upper_margin = 0,
	.lower_margin = 0,
	.vmode = FB_VMODE_NONINTERLACED,
};

static int ht16k33_display_on(struct ht16k33_priv *priv)
{
	uint8_t data = REG_DISPLAY_SETUP | REG_DISPLAY_SETUP_ON;

	return i2c_smbus_write_byte(priv->client, data);
}

static int ht16k33_display_off(struct ht16k33_priv *priv)
{
	return i2c_smbus_write_byte(priv->client, REG_DISPLAY_SETUP);
}

static void ht16k33_fb_queue(struct ht16k33_priv *priv)
{
	struct ht16k33_fbdev *fbdev = &priv->fbdev;

	schedule_delayed_work(&fbdev->work, HZ / fbdev->refresh_rate);
}

/*
 * This gets the fb data from cache and copies it to ht16k33 display RAM
 */
static void ht16k33_fb_update(struct work_struct *work)
{
	struct ht16k33_fbdev *fbdev =
		container_of(work, struct ht16k33_fbdev, work.work);
	struct ht16k33_priv *priv =
		container_of(fbdev, struct ht16k33_priv, fbdev);

	uint8_t *p1, *p2;
	int len, pos = 0, first = -1;

	p1 = fbdev->cache;
	p2 = fbdev->buffer;

	/* Search for the first byte with changes */
	while (pos < HT16K33_FB_SIZE && first < 0) {
		if (*(p1++) - *(p2++))
			first = pos;
		pos++;
	}

	/* No changes found */
	if (first < 0)
		goto requeue;

	len = HT16K33_FB_SIZE - first;
	p1 = fbdev->cache + HT16K33_FB_SIZE - 1;
	p2 = fbdev->buffer + HT16K33_FB_SIZE - 1;

	/* Determine i2c transfer length */
	while (len > 1) {
		if (*(p1--) - *(p2--))
			break;
		len--;
	}

	p1 = fbdev->cache + first;
	p2 = fbdev->buffer + first;
	if (!i2c_smbus_write_i2c_block_data(priv->client, first, len, p2))
		memcpy(p1, p2, len);
requeue:
	ht16k33_fb_queue(priv);
}

static int ht16k33_initialize(struct ht16k33_priv *priv)
{
	uint8_t byte;
	int err;
	uint8_t data[HT16K33_MATRIX_LED_MAX_COLS * 2];

	/* Clear RAM (8 * 16 bits) */
	memset(data, 0, sizeof(data));
	err = i2c_smbus_write_block_data(priv->client, 0, sizeof(data), data);
	if (err)
		return err;

	/* Turn on internal oscillator */
	byte = REG_SYSTEM_SETUP_OSC_ON | REG_SYSTEM_SETUP;
	err = i2c_smbus_write_byte(priv->client, byte);
	if (err)
		return err;

	/* Configure INT pin */
	byte = REG_ROWINT_SET | REG_ROWINT_SET_INT_ACT_HIGH;
	if (priv->client->irq > 0)
		byte |= REG_ROWINT_SET_INT_EN;
	return i2c_smbus_write_byte(priv->client, byte);
}

static int ht16k33_bl_update_status(struct backlight_device *bl)
{
	int brightness = bl->props.brightness;
	struct ht16k33_priv *priv = bl_get_data(bl);

	if (bl->props.power != FB_BLANK_UNBLANK ||
	    bl->props.fb_blank != FB_BLANK_UNBLANK ||
	    bl->props.state & BL_CORE_FBBLANK || brightness == 0) {
		return ht16k33_display_off(priv);
	}

	ht16k33_display_on(priv);
	return i2c_smbus_write_byte(priv->client,
				    REG_BRIGHTNESS | (brightness - 1));
}

static int ht16k33_bl_check_fb(struct backlight_device *bl, struct fb_info *fi)
{
	struct ht16k33_priv *priv = bl_get_data(bl);

	return (fi == NULL) || (fi->par == priv);
}

static const struct backlight_ops ht16k33_bl_ops = {
	.update_status	= ht16k33_bl_update_status,
	.check_fb	= ht16k33_bl_check_fb,
};

static int ht16k33_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct ht16k33_priv *priv = info->par;
	struct page *pages = virt_to_page(priv->fbdev.buffer);

	return vm_map_pages_zero(vma, &pages, 1);
}

static const struct fb_ops ht16k33_fb_ops = {
	.owner = THIS_MODULE,
	.fb_read = fb_sys_read,
	.fb_write = fb_sys_write,
	.fb_fillrect = sys_fillrect,
	.fb_copyarea = sys_copyarea,
	.fb_imageblit = sys_imageblit,
	.fb_mmap = ht16k33_mmap,
};

/*
 * This gets the keys from keypad and reports it to input subsystem.
 * Returns true if a key is pressed.
 */
static bool ht16k33_keypad_scan(struct ht16k33_keypad *keypad)
{
	const unsigned short *keycodes = keypad->dev->keycode;
	u16 new_state[HT16K33_MATRIX_KEYPAD_MAX_COLS];
	__le16 data[HT16K33_MATRIX_KEYPAD_MAX_COLS];
	unsigned long bits_changed;
	int row, col, code;
	int rc;
	bool pressed = false;

	rc = i2c_smbus_read_i2c_block_data(keypad->client, 0x40,
					   sizeof(data), (u8 *)data);
	if (rc != sizeof(data)) {
		dev_err(&keypad->client->dev,
			"Failed to read key data, rc=%d\n", rc);
		return false;
	}

	for (col = 0; col < keypad->cols; col++) {
		new_state[col] = le16_to_cpu(data[col]);
		if (new_state[col])
			pressed = true;
		bits_changed = keypad->last_key_state[col] ^ new_state[col];

		for_each_set_bit(row, &bits_changed, BITS_PER_LONG) {
			code = MATRIX_SCAN_CODE(row, col, keypad->row_shift);
			input_event(keypad->dev, EV_MSC, MSC_SCAN, code);
			input_report_key(keypad->dev, keycodes[code],
					 new_state[col] & BIT(row));
		}
	}
	input_sync(keypad->dev);
	memcpy(keypad->last_key_state, new_state, sizeof(u16) * keypad->cols);

	return pressed;
}

static irqreturn_t ht16k33_keypad_irq_thread(int irq, void *dev)
{
	struct ht16k33_keypad *keypad = dev;

	do {
		wait_event_timeout(keypad->wait, keypad->stopped,
				    msecs_to_jiffies(keypad->debounce_ms));
		if (keypad->stopped)
			break;
	} while (ht16k33_keypad_scan(keypad));

	return IRQ_HANDLED;
}

static int ht16k33_keypad_start(struct input_dev *dev)
{
	struct ht16k33_keypad *keypad = input_get_drvdata(dev);

	keypad->stopped = false;
	mb();
	enable_irq(keypad->client->irq);

	return 0;
}

static void ht16k33_keypad_stop(struct input_dev *dev)
{
	struct ht16k33_keypad *keypad = input_get_drvdata(dev);

	keypad->stopped = true;
	mb();
	wake_up(&keypad->wait);
	disable_irq(keypad->client->irq);
}

static int ht16k33_keypad_probe(struct i2c_client *client,
				struct ht16k33_keypad *keypad)
{
	struct device_node *node = client->dev.of_node;
	u32 rows = HT16K33_MATRIX_KEYPAD_MAX_ROWS;
	u32 cols = HT16K33_MATRIX_KEYPAD_MAX_COLS;
	int err;

	keypad->client = client;
	init_waitqueue_head(&keypad->wait);

	keypad->dev = devm_input_allocate_device(&client->dev);
	if (!keypad->dev)
		return -ENOMEM;

	input_set_drvdata(keypad->dev, keypad);

	keypad->dev->name = DRIVER_NAME"-keypad";
	keypad->dev->id.bustype = BUS_I2C;
	keypad->dev->open = ht16k33_keypad_start;
	keypad->dev->close = ht16k33_keypad_stop;

	if (!of_get_property(node, "linux,no-autorepeat", NULL))
		__set_bit(EV_REP, keypad->dev->evbit);

	err = of_property_read_u32(node, "debounce-delay-ms",
				   &keypad->debounce_ms);
	if (err) {
		dev_err(&client->dev, "key debounce delay not specified\n");
		return err;
	}

	err = matrix_keypad_parse_of_params(&client->dev, &rows, &cols);
	if (err)
		return err;
	if (rows > HT16K33_MATRIX_KEYPAD_MAX_ROWS ||
	    cols > HT16K33_MATRIX_KEYPAD_MAX_COLS) {
		dev_err(&client->dev, "%u rows or %u cols out of range in DT\n",
			rows, cols);
		return -ERANGE;
	}

	keypad->rows = rows;
	keypad->cols = cols;
	keypad->row_shift = get_count_order(cols);

	err = matrix_keypad_build_keymap(NULL, NULL, rows, cols, NULL,
					 keypad->dev);
	if (err) {
		dev_err(&client->dev, "failed to build keymap\n");
		return err;
	}

	err = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, ht16k33_keypad_irq_thread,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					DRIVER_NAME, keypad);
	if (err) {
		dev_err(&client->dev, "irq request failed %d, error %d\n",
			client->irq, err);
		return err;
	}

	ht16k33_keypad_stop(keypad->dev);

	err = input_register_device(keypad->dev);
	if (err)
		return err;

	return 0;
}

static int ht16k33_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	int err;
	uint32_t dft_brightness;
	struct backlight_device *bl;
	struct backlight_properties bl_props;
	struct ht16k33_priv *priv;
	struct ht16k33_fbdev *fbdev;
	struct device_node *node = client->dev.of_node;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "i2c_check_functionality error\n");
		return -EIO;
	}

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	i2c_set_clientdata(client, priv);
	fbdev = &priv->fbdev;

	err = ht16k33_initialize(priv);
	if (err)
		return err;

	/* Framebuffer (2 bytes per column) */
	BUILD_BUG_ON(PAGE_SIZE < HT16K33_FB_SIZE);
	fbdev->buffer = (unsigned char *) get_zeroed_page(GFP_KERNEL);
	if (!fbdev->buffer)
		return -ENOMEM;

	fbdev->cache = devm_kmalloc(&client->dev, HT16K33_FB_SIZE, GFP_KERNEL);
	if (!fbdev->cache) {
		err = -ENOMEM;
		goto err_fbdev_buffer;
	}

	fbdev->info = framebuffer_alloc(0, &client->dev);
	if (!fbdev->info) {
		err = -ENOMEM;
		goto err_fbdev_buffer;
	}

	err = of_property_read_u32(node, "refresh-rate-hz",
		&fbdev->refresh_rate);
	if (err) {
		dev_err(&client->dev, "refresh rate not specified\n");
		goto err_fbdev_info;
	}
	fb_bl_default_curve(fbdev->info, 0, MIN_BRIGHTNESS, MAX_BRIGHTNESS);

	INIT_DELAYED_WORK(&fbdev->work, ht16k33_fb_update);
	fbdev->info->fbops = &ht16k33_fb_ops;
	fbdev->info->screen_base = (char __iomem *) fbdev->buffer;
	fbdev->info->screen_size = HT16K33_FB_SIZE;
	fbdev->info->fix = ht16k33_fb_fix;
	fbdev->info->var = ht16k33_fb_var;
	fbdev->info->pseudo_palette = NULL;
	fbdev->info->flags = FBINFO_FLAG_DEFAULT;
	fbdev->info->par = priv;

	err = register_framebuffer(fbdev->info);
	if (err)
		goto err_fbdev_info;

	/* Keypad */
	if (client->irq > 0) {
		err = ht16k33_keypad_probe(client, &priv->keypad);
		if (err)
			goto err_fbdev_unregister;
	}

	/* Backlight */
	memset(&bl_props, 0, sizeof(struct backlight_properties));
	bl_props.type = BACKLIGHT_RAW;
	bl_props.max_brightness = MAX_BRIGHTNESS;

	bl = devm_backlight_device_register(&client->dev, DRIVER_NAME"-bl",
					    &client->dev, priv,
					    &ht16k33_bl_ops, &bl_props);
	if (IS_ERR(bl)) {
		dev_err(&client->dev, "failed to register backlight\n");
		err = PTR_ERR(bl);
		goto err_fbdev_unregister;
	}

	err = of_property_read_u32(node, "default-brightness-level",
				   &dft_brightness);
	if (err) {
		dft_brightness = MAX_BRIGHTNESS;
	} else if (dft_brightness > MAX_BRIGHTNESS) {
		dev_warn(&client->dev,
			 "invalid default brightness level: %u, using %u\n",
			 dft_brightness, MAX_BRIGHTNESS);
		dft_brightness = MAX_BRIGHTNESS;
	}

	bl->props.brightness = dft_brightness;
	ht16k33_bl_update_status(bl);

	ht16k33_fb_queue(priv);
	return 0;

err_fbdev_unregister:
	unregister_framebuffer(fbdev->info);
err_fbdev_info:
	framebuffer_release(fbdev->info);
err_fbdev_buffer:
	free_page((unsigned long) fbdev->buffer);

	return err;
}

static int ht16k33_remove(struct i2c_client *client)
{
	struct ht16k33_priv *priv = i2c_get_clientdata(client);
	struct ht16k33_fbdev *fbdev = &priv->fbdev;

	cancel_delayed_work_sync(&fbdev->work);
	unregister_framebuffer(fbdev->info);
	framebuffer_release(fbdev->info);
	free_page((unsigned long) fbdev->buffer);

	return 0;
}

static const struct i2c_device_id ht16k33_i2c_match[] = {
	{ "ht16k33", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ht16k33_i2c_match);

static const struct of_device_id ht16k33_of_match[] = {
	{ .compatible = "holtek,ht16k33", },
	{ }
};
MODULE_DEVICE_TABLE(of, ht16k33_of_match);

static struct i2c_driver ht16k33_driver = {
	.probe		= ht16k33_probe,
	.remove		= ht16k33_remove,
	.driver		= {
		.name		= DRIVER_NAME,
		.of_match_table	= of_match_ptr(ht16k33_of_match),
	},
	.id_table = ht16k33_i2c_match,
};
module_i2c_driver(ht16k33_driver);

MODULE_DESCRIPTION("Holtek HT16K33 driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Robin van der Gracht <robin@protonic.nl>");
