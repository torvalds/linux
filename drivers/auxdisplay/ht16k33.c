// SPDX-License-Identifier: GPL-2.0
/*
 * HT16K33 driver
 *
 * Author: Robin van der Gracht <robin@protonic.nl>
 *
 * Copyright: (C) 2016 Protonic Holland.
 * Copyright (C) 2021 Glider bv
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/property.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/container_of.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/leds.h>
#include <linux/workqueue.h>
#include <linux/mm.h>

#include <linux/map_to_7segment.h>
#include <linux/map_to_14segment.h>

#include <linux/unaligned.h>

#include "line-display.h"

/* Registers */
#define REG_SYSTEM_SETUP		0x20
#define REG_SYSTEM_SETUP_OSC_ON		BIT(0)

#define REG_DISPLAY_SETUP		0x80
#define REG_DISPLAY_SETUP_ON		BIT(0)
#define REG_DISPLAY_SETUP_BLINK_OFF	(0 << 1)
#define REG_DISPLAY_SETUP_BLINK_2HZ	(1 << 1)
#define REG_DISPLAY_SETUP_BLINK_1HZ	(2 << 1)
#define REG_DISPLAY_SETUP_BLINK_0HZ5	(3 << 1)

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

enum display_type {
	DISP_MATRIX = 0,
	DISP_QUAD_7SEG,
	DISP_QUAD_14SEG,
};

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
};

struct ht16k33_priv {
	struct i2c_client *client;
	struct delayed_work work;
	struct led_classdev led;
	struct ht16k33_keypad keypad;
	union {
		struct ht16k33_fbdev fbdev;
		struct linedisp linedisp;
	};
	enum display_type type;
	uint8_t blink;
};

#define ht16k33_work_to_priv(p)				\
	container_of(p, struct ht16k33_priv, work.work)

#define ht16k33_led_to_priv(p)				\
	container_of(p, struct ht16k33_priv, led)

#define ht16k33_linedisp_to_priv(p)			\
	container_of(p, struct ht16k33_priv, linedisp)

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
	uint8_t data = REG_DISPLAY_SETUP | REG_DISPLAY_SETUP_ON | priv->blink;

	return i2c_smbus_write_byte(priv->client, data);
}

static int ht16k33_display_off(struct ht16k33_priv *priv)
{
	return i2c_smbus_write_byte(priv->client, REG_DISPLAY_SETUP);
}

static int ht16k33_brightness_set(struct ht16k33_priv *priv,
				  unsigned int brightness)
{
	int err;

	if (brightness == 0) {
		priv->blink = REG_DISPLAY_SETUP_BLINK_OFF;
		return ht16k33_display_off(priv);
	}

	err = ht16k33_display_on(priv);
	if (err)
		return err;

	return i2c_smbus_write_byte(priv->client,
				    REG_BRIGHTNESS | (brightness - 1));
}

static int ht16k33_brightness_set_blocking(struct led_classdev *led_cdev,
					   enum led_brightness brightness)
{
	struct ht16k33_priv *priv = ht16k33_led_to_priv(led_cdev);

	return ht16k33_brightness_set(priv, brightness);
}

static int ht16k33_blink_set(struct led_classdev *led_cdev,
			     unsigned long *delay_on, unsigned long *delay_off)
{
	struct ht16k33_priv *priv = ht16k33_led_to_priv(led_cdev);
	unsigned int delay;
	uint8_t blink;
	int err;

	if (!*delay_on && !*delay_off) {
		blink = REG_DISPLAY_SETUP_BLINK_1HZ;
		delay = 1000;
	} else if (*delay_on <= 750) {
		blink = REG_DISPLAY_SETUP_BLINK_2HZ;
		delay = 500;
	} else if (*delay_on <= 1500) {
		blink = REG_DISPLAY_SETUP_BLINK_1HZ;
		delay = 1000;
	} else {
		blink = REG_DISPLAY_SETUP_BLINK_0HZ5;
		delay = 2000;
	}

	err = i2c_smbus_write_byte(priv->client,
				   REG_DISPLAY_SETUP | REG_DISPLAY_SETUP_ON |
				   blink);
	if (err)
		return err;

	priv->blink = blink;
	*delay_on = *delay_off = delay;
	return 0;
}

static void ht16k33_fb_queue(struct ht16k33_priv *priv)
{
	struct ht16k33_fbdev *fbdev = &priv->fbdev;

	schedule_delayed_work(&priv->work, HZ / fbdev->refresh_rate);
}

/*
 * This gets the fb data from cache and copies it to ht16k33 display RAM
 */
static void ht16k33_fb_update(struct work_struct *work)
{
	struct ht16k33_priv *priv = ht16k33_work_to_priv(work);
	struct ht16k33_fbdev *fbdev = &priv->fbdev;

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
	uint8_t data[HT16K33_FB_SIZE];
	uint8_t byte;
	int err;

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
	const int brightness = backlight_get_brightness(bl);
	struct ht16k33_priv *priv = bl_get_data(bl);

	return ht16k33_brightness_set(priv, brightness);
}

static const struct backlight_ops ht16k33_bl_ops = {
	.update_status	= ht16k33_bl_update_status,
};

/*
 * Blank events will be passed to the actual device handling the backlight when
 * we return zero here.
 */
static int ht16k33_blank(int blank, struct fb_info *info)
{
	return 0;
}

static int ht16k33_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct ht16k33_priv *priv = info->par;
	struct page *pages = virt_to_page(priv->fbdev.buffer);

	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

	return vm_map_pages_zero(vma, &pages, 1);
}

static const struct fb_ops ht16k33_fb_ops = {
	.owner = THIS_MODULE,
	__FB_DEFAULT_SYSMEM_OPS_RDWR,
	.fb_blank = ht16k33_blank,
	__FB_DEFAULT_SYSMEM_OPS_DRAW,
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

static void ht16k33_seg7_update(struct work_struct *work)
{
	struct ht16k33_priv *priv = ht16k33_work_to_priv(work);
	struct linedisp_map *map = priv->linedisp.map;
	char *s = priv->linedisp.buf;
	uint8_t buf[9];

	buf[0] = map_to_seg7(&map->map.seg7, *s++);
	buf[1] = 0;
	buf[2] = map_to_seg7(&map->map.seg7, *s++);
	buf[3] = 0;
	buf[4] = 0;
	buf[5] = 0;
	buf[6] = map_to_seg7(&map->map.seg7, *s++);
	buf[7] = 0;
	buf[8] = map_to_seg7(&map->map.seg7, *s++);

	i2c_smbus_write_i2c_block_data(priv->client, 0, ARRAY_SIZE(buf), buf);
}

static void ht16k33_seg14_update(struct work_struct *work)
{
	struct ht16k33_priv *priv = ht16k33_work_to_priv(work);
	struct linedisp_map *map = priv->linedisp.map;
	char *s = priv->linedisp.buf;
	uint8_t buf[8];

	put_unaligned_le16(map_to_seg14(&map->map.seg14, *s++), buf + 0);
	put_unaligned_le16(map_to_seg14(&map->map.seg14, *s++), buf + 2);
	put_unaligned_le16(map_to_seg14(&map->map.seg14, *s++), buf + 4);
	put_unaligned_le16(map_to_seg14(&map->map.seg14, *s++), buf + 6);

	i2c_smbus_write_i2c_block_data(priv->client, 0, ARRAY_SIZE(buf), buf);
}

static int ht16k33_linedisp_get_map_type(struct linedisp *linedisp)
{
	struct ht16k33_priv *priv = ht16k33_linedisp_to_priv(linedisp);

	switch (priv->type) {
	case DISP_QUAD_7SEG:
		INIT_DELAYED_WORK(&priv->work, ht16k33_seg7_update);
		return LINEDISP_MAP_SEG7;

	case DISP_QUAD_14SEG:
		INIT_DELAYED_WORK(&priv->work, ht16k33_seg14_update);
		return LINEDISP_MAP_SEG14;

	default:
		return -EINVAL;
	}
}

static void ht16k33_linedisp_update(struct linedisp *linedisp)
{
	struct ht16k33_priv *priv = ht16k33_linedisp_to_priv(linedisp);

	schedule_delayed_work(&priv->work, 0);
}

static const struct linedisp_ops ht16k33_linedisp_ops = {
	.get_map_type = ht16k33_linedisp_get_map_type,
	.update = ht16k33_linedisp_update,
};

static int ht16k33_led_probe(struct device *dev, struct led_classdev *led,
			     unsigned int brightness)
{
	struct led_init_data init_data = {};
	int err;

	/* The LED is optional */
	init_data.fwnode = device_get_named_child_node(dev, "led");
	if (!init_data.fwnode)
		return 0;

	init_data.devicename = "auxdisplay";
	init_data.devname_mandatory = true;

	led->brightness_set_blocking = ht16k33_brightness_set_blocking;
	led->blink_set = ht16k33_blink_set;
	led->flags = LED_CORE_SUSPENDRESUME;
	led->brightness = brightness;
	led->max_brightness = MAX_BRIGHTNESS;

	err = devm_led_classdev_register_ext(dev, led, &init_data);
	fwnode_handle_put(init_data.fwnode);
	if (err)
		dev_err(dev, "Failed to register LED\n");

	return err;
}

static int ht16k33_keypad_probe(struct i2c_client *client,
				struct ht16k33_keypad *keypad)
{
	struct device *dev = &client->dev;
	u32 rows = HT16K33_MATRIX_KEYPAD_MAX_ROWS;
	u32 cols = HT16K33_MATRIX_KEYPAD_MAX_COLS;
	int err;

	keypad->client = client;
	init_waitqueue_head(&keypad->wait);

	keypad->dev = devm_input_allocate_device(dev);
	if (!keypad->dev)
		return -ENOMEM;

	input_set_drvdata(keypad->dev, keypad);

	keypad->dev->name = DRIVER_NAME"-keypad";
	keypad->dev->id.bustype = BUS_I2C;
	keypad->dev->open = ht16k33_keypad_start;
	keypad->dev->close = ht16k33_keypad_stop;

	if (!device_property_read_bool(dev, "linux,no-autorepeat"))
		__set_bit(EV_REP, keypad->dev->evbit);

	err = device_property_read_u32(dev, "debounce-delay-ms",
				       &keypad->debounce_ms);
	if (err) {
		dev_err(dev, "key debounce delay not specified\n");
		return err;
	}

	err = matrix_keypad_parse_properties(dev, &rows, &cols);
	if (err)
		return err;
	if (rows > HT16K33_MATRIX_KEYPAD_MAX_ROWS ||
	    cols > HT16K33_MATRIX_KEYPAD_MAX_COLS) {
		dev_err(dev, "%u rows or %u cols out of range in DT\n", rows,
			cols);
		return -ERANGE;
	}

	keypad->rows = rows;
	keypad->cols = cols;
	keypad->row_shift = get_count_order(cols);

	err = matrix_keypad_build_keymap(NULL, NULL, rows, cols, NULL,
					 keypad->dev);
	if (err) {
		dev_err(dev, "failed to build keymap\n");
		return err;
	}

	err = devm_request_threaded_irq(dev, client->irq, NULL,
					ht16k33_keypad_irq_thread,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					DRIVER_NAME, keypad);
	if (err) {
		dev_err(dev, "irq request failed %d, error %d\n", client->irq,
			err);
		return err;
	}

	ht16k33_keypad_stop(keypad->dev);

	return input_register_device(keypad->dev);
}

static int ht16k33_fbdev_probe(struct device *dev, struct ht16k33_priv *priv,
			       uint32_t brightness)
{
	struct ht16k33_fbdev *fbdev = &priv->fbdev;
	struct backlight_device *bl = NULL;
	int err;

	if (priv->led.dev) {
		err = ht16k33_brightness_set(priv, brightness);
		if (err)
			return err;
	} else {
		/* backwards compatibility with DT lacking an led subnode */
		struct backlight_properties bl_props;

		memset(&bl_props, 0, sizeof(struct backlight_properties));
		bl_props.type = BACKLIGHT_RAW;
		bl_props.max_brightness = MAX_BRIGHTNESS;

		bl = devm_backlight_device_register(dev, DRIVER_NAME"-bl", dev,
						    priv, &ht16k33_bl_ops,
						    &bl_props);
		if (IS_ERR(bl)) {
			dev_err(dev, "failed to register backlight\n");
			return PTR_ERR(bl);
		}

		bl->props.brightness = brightness;
		ht16k33_bl_update_status(bl);
	}

	/* Framebuffer (2 bytes per column) */
	BUILD_BUG_ON(PAGE_SIZE < HT16K33_FB_SIZE);
	fbdev->buffer = (unsigned char *) get_zeroed_page(GFP_KERNEL);
	if (!fbdev->buffer)
		return -ENOMEM;

	fbdev->cache = devm_kmalloc(dev, HT16K33_FB_SIZE, GFP_KERNEL);
	if (!fbdev->cache) {
		err = -ENOMEM;
		goto err_fbdev_buffer;
	}

	fbdev->info = framebuffer_alloc(0, dev);
	if (!fbdev->info) {
		err = -ENOMEM;
		goto err_fbdev_buffer;
	}

	err = device_property_read_u32(dev, "refresh-rate-hz",
				       &fbdev->refresh_rate);
	if (err) {
		dev_err(dev, "refresh rate not specified\n");
		goto err_fbdev_info;
	}
	fb_bl_default_curve(fbdev->info, 0, MIN_BRIGHTNESS, MAX_BRIGHTNESS);

	INIT_DELAYED_WORK(&priv->work, ht16k33_fb_update);
	fbdev->info->fbops = &ht16k33_fb_ops;
	fbdev->info->flags |= FBINFO_VIRTFB;
	fbdev->info->screen_buffer = fbdev->buffer;
	fbdev->info->screen_size = HT16K33_FB_SIZE;
	fbdev->info->fix = ht16k33_fb_fix;
	fbdev->info->var = ht16k33_fb_var;
	fbdev->info->bl_dev = bl;
	fbdev->info->pseudo_palette = NULL;
	fbdev->info->par = priv;

	err = register_framebuffer(fbdev->info);
	if (err)
		goto err_fbdev_info;

	ht16k33_fb_queue(priv);
	return 0;

err_fbdev_info:
	framebuffer_release(fbdev->info);
err_fbdev_buffer:
	free_page((unsigned long) fbdev->buffer);

	return err;
}

static int ht16k33_seg_probe(struct device *dev, struct ht16k33_priv *priv,
			     uint32_t brightness)
{
	struct linedisp *linedisp = &priv->linedisp;
	int err;

	err = ht16k33_brightness_set(priv, brightness);
	if (err)
		return err;

	return linedisp_register(linedisp, dev, 4, &ht16k33_linedisp_ops);
}

static int ht16k33_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ht16k33_priv *priv;
	uint32_t dft_brightness;
	int err;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "i2c_check_functionality error\n");
		return -EIO;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	priv->type = (uintptr_t)i2c_get_match_data(client);

	i2c_set_clientdata(client, priv);

	err = ht16k33_initialize(priv);
	if (err)
		return err;

	err = device_property_read_u32(dev, "default-brightness-level",
				       &dft_brightness);
	if (err) {
		dft_brightness = MAX_BRIGHTNESS;
	} else if (dft_brightness > MAX_BRIGHTNESS) {
		dev_warn(dev,
			 "invalid default brightness level: %u, using %u\n",
			 dft_brightness, MAX_BRIGHTNESS);
		dft_brightness = MAX_BRIGHTNESS;
	}

	/* LED */
	err = ht16k33_led_probe(dev, &priv->led, dft_brightness);
	if (err)
		return err;

	/* Keypad */
	if (client->irq > 0) {
		err = ht16k33_keypad_probe(client, &priv->keypad);
		if (err)
			return err;
	}

	switch (priv->type) {
	case DISP_MATRIX:
		/* Frame Buffer Display */
		err = ht16k33_fbdev_probe(dev, priv, dft_brightness);
		break;

	case DISP_QUAD_7SEG:
	case DISP_QUAD_14SEG:
		/* Segment Display */
		err = ht16k33_seg_probe(dev, priv, dft_brightness);
		break;

	default:
		return -EINVAL;
	}
	return err;
}

static void ht16k33_remove(struct i2c_client *client)
{
	struct ht16k33_priv *priv = i2c_get_clientdata(client);
	struct ht16k33_fbdev *fbdev = &priv->fbdev;

	cancel_delayed_work_sync(&priv->work);

	switch (priv->type) {
	case DISP_MATRIX:
		unregister_framebuffer(fbdev->info);
		framebuffer_release(fbdev->info);
		free_page((unsigned long)fbdev->buffer);
		break;

	case DISP_QUAD_7SEG:
	case DISP_QUAD_14SEG:
		linedisp_unregister(&priv->linedisp);
		break;

	default:
		break;
	}
}

static const struct i2c_device_id ht16k33_i2c_match[] = {
	{ "3108", DISP_QUAD_7SEG },
	{ "3130", DISP_QUAD_14SEG },
	{ "ht16k33", DISP_MATRIX },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ht16k33_i2c_match);

static const struct of_device_id ht16k33_of_match[] = {
	{
		/* 0.56" 4-Digit 7-Segment FeatherWing Display (Red) */
		.compatible = "adafruit,3108", .data = (void *)DISP_QUAD_7SEG,
	}, {
		/* 0.54" Quad Alphanumeric FeatherWing Display (Red) */
		.compatible = "adafruit,3130", .data = (void *)DISP_QUAD_14SEG,
	}, {
		/* Generic, assumed Dot-Matrix Display */
		.compatible = "holtek,ht16k33", .data = (void *)DISP_MATRIX,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ht16k33_of_match);

static struct i2c_driver ht16k33_driver = {
	.probe		= ht16k33_probe,
	.remove		= ht16k33_remove,
	.driver		= {
		.name		= DRIVER_NAME,
		.of_match_table	= ht16k33_of_match,
	},
	.id_table = ht16k33_i2c_match,
};
module_i2c_driver(ht16k33_driver);

MODULE_DESCRIPTION("Holtek HT16K33 driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(LINEDISP);
MODULE_AUTHOR("Robin van der Gracht <robin@protonic.nl>");
