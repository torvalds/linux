/***************************************************************************
 *   Copyright (C) 2010 by Bruno Prémont <bonbons@linux-vserver.org>       *
 *                                                                         *
 *   Based on Logitech G13 driver (v0.4)                                   *
 *     Copyright (C) 2009 by Rick L. Vinyard, Jr. <rvinyard@cs.nmsu.edu>   *
 *                                                                         *
 *   This program is free software: you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation, version 2 of the License.               *
 *                                                                         *
 *   This driver is distributed in the hope that it will be useful, but    *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   General Public License for more details.                              *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this software. If not see <http://www.gnu.org/licenses/>.  *
 ***************************************************************************/

#include <linux/hid.h>
#include <linux/hid-debug.h>
#include <linux/input.h>
#include "hid-ids.h"
#include "usbhid/usbhid.h"
#include <linux/usb.h>

#include <linux/fb.h>
#include <linux/vmalloc.h>
#include <linux/backlight.h>
#include <linux/lcd.h>

#include <linux/leds.h>

#include <linux/seq_file.h>
#include <linux/debugfs.h>

#include <linux/completion.h>
#include <linux/uaccess.h>
#include <linux/module.h>

#define PICOLCD_NAME "PicoLCD (graphic)"

/* Report numbers */
#define REPORT_ERROR_CODE      0x10 /* LCD: IN[16]  */
#define   ERR_SUCCESS            0x00
#define   ERR_PARAMETER_MISSING  0x01
#define   ERR_DATA_MISSING       0x02
#define   ERR_BLOCK_READ_ONLY    0x03
#define   ERR_BLOCK_NOT_ERASABLE 0x04
#define   ERR_BLOCK_TOO_BIG      0x05
#define   ERR_SECTION_OVERFLOW   0x06
#define   ERR_INVALID_CMD_LEN    0x07
#define   ERR_INVALID_DATA_LEN   0x08
#define REPORT_KEY_STATE       0x11 /* LCD: IN[2]   */
#define REPORT_IR_DATA         0x21 /* LCD: IN[63]  */
#define REPORT_EE_DATA         0x32 /* LCD: IN[63]  */
#define REPORT_MEMORY          0x41 /* LCD: IN[63]  */
#define REPORT_LED_STATE       0x81 /* LCD: OUT[1]  */
#define REPORT_BRIGHTNESS      0x91 /* LCD: OUT[1]  */
#define REPORT_CONTRAST        0x92 /* LCD: OUT[1]  */
#define REPORT_RESET           0x93 /* LCD: OUT[2]  */
#define REPORT_LCD_CMD         0x94 /* LCD: OUT[63] */
#define REPORT_LCD_DATA        0x95 /* LCD: OUT[63] */
#define REPORT_LCD_CMD_DATA    0x96 /* LCD: OUT[63] */
#define	REPORT_EE_READ         0xa3 /* LCD: OUT[63] */
#define REPORT_EE_WRITE        0xa4 /* LCD: OUT[63] */
#define REPORT_ERASE_MEMORY    0xb2 /* LCD: OUT[2]  */
#define REPORT_READ_MEMORY     0xb3 /* LCD: OUT[3]  */
#define REPORT_WRITE_MEMORY    0xb4 /* LCD: OUT[63] */
#define REPORT_SPLASH_RESTART  0xc1 /* LCD: OUT[1]  */
#define REPORT_EXIT_KEYBOARD   0xef /* LCD: OUT[2]  */
#define REPORT_VERSION         0xf1 /* LCD: IN[2],OUT[1]    Bootloader: IN[2],OUT[1]   */
#define REPORT_BL_ERASE_MEMORY 0xf2 /*                      Bootloader: IN[36],OUT[4]  */
#define REPORT_BL_READ_MEMORY  0xf3 /*                      Bootloader: IN[36],OUT[4]  */
#define REPORT_BL_WRITE_MEMORY 0xf4 /*                      Bootloader: IN[36],OUT[36] */
#define REPORT_DEVID           0xf5 /* LCD: IN[5], OUT[1]   Bootloader: IN[5],OUT[1]   */
#define REPORT_SPLASH_SIZE     0xf6 /* LCD: IN[4], OUT[1]   */
#define REPORT_HOOK_VERSION    0xf7 /* LCD: IN[2], OUT[1]   */
#define REPORT_EXIT_FLASHER    0xff /*                      Bootloader: OUT[2]         */

#ifdef CONFIG_HID_PICOLCD_FB
/* Framebuffer
 *
 * The PicoLCD use a Topway LCD module of 256x64 pixel
 * This display area is tiled over 4 controllers with 8 tiles
 * each. Each tile has 8x64 pixel, each data byte representing
 * a 1-bit wide vertical line of the tile.
 *
 * The display can be updated at a tile granularity.
 *
 *       Chip 1           Chip 2           Chip 3           Chip 4
 * +----------------+----------------+----------------+----------------+
 * |     Tile 1     |     Tile 1     |     Tile 1     |     Tile 1     |
 * +----------------+----------------+----------------+----------------+
 * |     Tile 2     |     Tile 2     |     Tile 2     |     Tile 2     |
 * +----------------+----------------+----------------+----------------+
 *                                  ...
 * +----------------+----------------+----------------+----------------+
 * |     Tile 8     |     Tile 8     |     Tile 8     |     Tile 8     |
 * +----------------+----------------+----------------+----------------+
 */
#define PICOLCDFB_NAME "picolcdfb"
#define PICOLCDFB_WIDTH (256)
#define PICOLCDFB_HEIGHT (64)
#define PICOLCDFB_SIZE (PICOLCDFB_WIDTH * PICOLCDFB_HEIGHT / 8)

#define PICOLCDFB_UPDATE_RATE_LIMIT   10
#define PICOLCDFB_UPDATE_RATE_DEFAULT  2

/* Framebuffer visual structures */
static const struct fb_fix_screeninfo picolcdfb_fix = {
	.id          = PICOLCDFB_NAME,
	.type        = FB_TYPE_PACKED_PIXELS,
	.visual      = FB_VISUAL_MONO01,
	.xpanstep    = 0,
	.ypanstep    = 0,
	.ywrapstep   = 0,
	.line_length = PICOLCDFB_WIDTH / 8,
	.accel       = FB_ACCEL_NONE,
};

static const struct fb_var_screeninfo picolcdfb_var = {
	.xres           = PICOLCDFB_WIDTH,
	.yres           = PICOLCDFB_HEIGHT,
	.xres_virtual   = PICOLCDFB_WIDTH,
	.yres_virtual   = PICOLCDFB_HEIGHT,
	.width          = 103,
	.height         = 26,
	.bits_per_pixel = 1,
	.grayscale      = 1,
	.red            = {
		.offset = 0,
		.length = 1,
		.msb_right = 0,
	},
	.green          = {
		.offset = 0,
		.length = 1,
		.msb_right = 0,
	},
	.blue           = {
		.offset = 0,
		.length = 1,
		.msb_right = 0,
	},
	.transp         = {
		.offset = 0,
		.length = 0,
		.msb_right = 0,
	},
};
#endif /* CONFIG_HID_PICOLCD_FB */

/* Input device
 *
 * The PicoLCD has an IR receiver header, a built-in keypad with 5 keys
 * and header for 4x4 key matrix. The built-in keys are part of the matrix.
 */
static const unsigned short def_keymap[] = {
	KEY_RESERVED,	/* none */
	KEY_BACK,	/* col 4 + row 1 */
	KEY_HOMEPAGE,	/* col 3 + row 1 */
	KEY_RESERVED,	/* col 2 + row 1 */
	KEY_RESERVED,	/* col 1 + row 1 */
	KEY_SCROLLUP,	/* col 4 + row 2 */
	KEY_OK,		/* col 3 + row 2 */
	KEY_SCROLLDOWN,	/* col 2 + row 2 */
	KEY_RESERVED,	/* col 1 + row 2 */
	KEY_RESERVED,	/* col 4 + row 3 */
	KEY_RESERVED,	/* col 3 + row 3 */
	KEY_RESERVED,	/* col 2 + row 3 */
	KEY_RESERVED,	/* col 1 + row 3 */
	KEY_RESERVED,	/* col 4 + row 4 */
	KEY_RESERVED,	/* col 3 + row 4 */
	KEY_RESERVED,	/* col 2 + row 4 */
	KEY_RESERVED,	/* col 1 + row 4 */
};
#define PICOLCD_KEYS ARRAY_SIZE(def_keymap)

/* Description of in-progress IO operation, used for operations
 * that trigger response from device */
struct picolcd_pending {
	struct hid_report *out_report;
	struct hid_report *in_report;
	struct completion ready;
	int raw_size;
	u8 raw_data[64];
};

/* Per device data structure */
struct picolcd_data {
	struct hid_device *hdev;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debug_reset;
	struct dentry *debug_eeprom;
	struct dentry *debug_flash;
	struct mutex mutex_flash;
	int addr_sz;
#endif
	u8 version[2];
	unsigned short opmode_delay;
	/* input stuff */
	u8 pressed_keys[2];
	struct input_dev *input_keys;
	struct input_dev *input_cir;
	unsigned short keycode[PICOLCD_KEYS];

#ifdef CONFIG_HID_PICOLCD_FB
	/* Framebuffer stuff */
	u8 fb_update_rate;
	u8 fb_bpp;
	u8 fb_force;
	u8 *fb_vbitmap;		/* local copy of what was sent to PicoLCD */
	u8 *fb_bitmap;		/* framebuffer */
	struct fb_info *fb_info;
	struct fb_deferred_io fb_defio;
#endif /* CONFIG_HID_PICOLCD_FB */
#ifdef CONFIG_HID_PICOLCD_LCD
	struct lcd_device *lcd;
	u8 lcd_contrast;
#endif /* CONFIG_HID_PICOLCD_LCD */
#ifdef CONFIG_HID_PICOLCD_BACKLIGHT
	struct backlight_device *backlight;
	u8 lcd_brightness;
	u8 lcd_power;
#endif /* CONFIG_HID_PICOLCD_BACKLIGHT */
#ifdef CONFIG_HID_PICOLCD_LEDS
	/* LED stuff */
	u8 led_state;
	struct led_classdev *led[8];
#endif /* CONFIG_HID_PICOLCD_LEDS */

	/* Housekeeping stuff */
	spinlock_t lock;
	struct mutex mutex;
	struct picolcd_pending *pending;
	int status;
#define PICOLCD_BOOTLOADER 1
#define PICOLCD_FAILED 2
#define PICOLCD_READY_FB 4
};


/* Find a given report */
#define picolcd_in_report(id, dev) picolcd_report(id, dev, HID_INPUT_REPORT)
#define picolcd_out_report(id, dev) picolcd_report(id, dev, HID_OUTPUT_REPORT)

static struct hid_report *picolcd_report(int id, struct hid_device *hdev, int dir)
{
	struct list_head *feature_report_list = &hdev->report_enum[dir].report_list;
	struct hid_report *report = NULL;

	list_for_each_entry(report, feature_report_list, list) {
		if (report->id == id)
			return report;
	}
	hid_warn(hdev, "No report with id 0x%x found\n", id);
	return NULL;
}

#ifdef CONFIG_DEBUG_FS
static void picolcd_debug_out_report(struct picolcd_data *data,
		struct hid_device *hdev, struct hid_report *report);
#define usbhid_submit_report(a, b, c) \
	do { \
		picolcd_debug_out_report(hid_get_drvdata(a), a, b); \
		usbhid_submit_report(a, b, c); \
	} while (0)
#endif

/* Submit a report and wait for a reply from device - if device fades away
 * or does not respond in time, return NULL */
static struct picolcd_pending *picolcd_send_and_wait(struct hid_device *hdev,
		int report_id, const u8 *raw_data, int size)
{
	struct picolcd_data *data = hid_get_drvdata(hdev);
	struct picolcd_pending *work;
	struct hid_report *report = picolcd_out_report(report_id, hdev);
	unsigned long flags;
	int i, j, k;

	if (!report || !data)
		return NULL;
	if (data->status & PICOLCD_FAILED)
		return NULL;
	work = kzalloc(sizeof(*work), GFP_KERNEL);
	if (!work)
		return NULL;

	init_completion(&work->ready);
	work->out_report = report;
	work->in_report  = NULL;
	work->raw_size   = 0;

	mutex_lock(&data->mutex);
	spin_lock_irqsave(&data->lock, flags);
	for (i = k = 0; i < report->maxfield; i++)
		for (j = 0; j < report->field[i]->report_count; j++) {
			hid_set_field(report->field[i], j, k < size ? raw_data[k] : 0);
			k++;
		}
	data->pending = work;
	usbhid_submit_report(data->hdev, report, USB_DIR_OUT);
	spin_unlock_irqrestore(&data->lock, flags);
	wait_for_completion_interruptible_timeout(&work->ready, HZ*2);
	spin_lock_irqsave(&data->lock, flags);
	data->pending = NULL;
	spin_unlock_irqrestore(&data->lock, flags);
	mutex_unlock(&data->mutex);
	return work;
}

#ifdef CONFIG_HID_PICOLCD_FB
/* Send a given tile to PicoLCD */
static int picolcd_fb_send_tile(struct hid_device *hdev, int chip, int tile)
{
	struct picolcd_data *data = hid_get_drvdata(hdev);
	struct hid_report *report1 = picolcd_out_report(REPORT_LCD_CMD_DATA, hdev);
	struct hid_report *report2 = picolcd_out_report(REPORT_LCD_DATA, hdev);
	unsigned long flags;
	u8 *tdata;
	int i;

	if (!report1 || report1->maxfield != 1 || !report2 || report2->maxfield != 1)
		return -ENODEV;

	spin_lock_irqsave(&data->lock, flags);
	hid_set_field(report1->field[0],  0, chip << 2);
	hid_set_field(report1->field[0],  1, 0x02);
	hid_set_field(report1->field[0],  2, 0x00);
	hid_set_field(report1->field[0],  3, 0x00);
	hid_set_field(report1->field[0],  4, 0xb8 | tile);
	hid_set_field(report1->field[0],  5, 0x00);
	hid_set_field(report1->field[0],  6, 0x00);
	hid_set_field(report1->field[0],  7, 0x40);
	hid_set_field(report1->field[0],  8, 0x00);
	hid_set_field(report1->field[0],  9, 0x00);
	hid_set_field(report1->field[0], 10,   32);

	hid_set_field(report2->field[0],  0, (chip << 2) | 0x01);
	hid_set_field(report2->field[0],  1, 0x00);
	hid_set_field(report2->field[0],  2, 0x00);
	hid_set_field(report2->field[0],  3,   32);

	tdata = data->fb_vbitmap + (tile * 4 + chip) * 64;
	for (i = 0; i < 64; i++)
		if (i < 32)
			hid_set_field(report1->field[0], 11 + i, tdata[i]);
		else
			hid_set_field(report2->field[0], 4 + i - 32, tdata[i]);

	usbhid_submit_report(data->hdev, report1, USB_DIR_OUT);
	usbhid_submit_report(data->hdev, report2, USB_DIR_OUT);
	spin_unlock_irqrestore(&data->lock, flags);
	return 0;
}

/* Translate a single tile*/
static int picolcd_fb_update_tile(u8 *vbitmap, const u8 *bitmap, int bpp,
		int chip, int tile)
{
	int i, b, changed = 0;
	u8 tdata[64];
	u8 *vdata = vbitmap + (tile * 4 + chip) * 64;

	if (bpp == 1) {
		for (b = 7; b >= 0; b--) {
			const u8 *bdata = bitmap + tile * 256 + chip * 8 + b * 32;
			for (i = 0; i < 64; i++) {
				tdata[i] <<= 1;
				tdata[i] |= (bdata[i/8] >> (i % 8)) & 0x01;
			}
		}
	} else if (bpp == 8) {
		for (b = 7; b >= 0; b--) {
			const u8 *bdata = bitmap + (tile * 256 + chip * 8 + b * 32) * 8;
			for (i = 0; i < 64; i++) {
				tdata[i] <<= 1;
				tdata[i] |= (bdata[i] & 0x80) ? 0x01 : 0x00;
			}
		}
	} else {
		/* Oops, we should never get here! */
		WARN_ON(1);
		return 0;
	}

	for (i = 0; i < 64; i++)
		if (tdata[i] != vdata[i]) {
			changed = 1;
			vdata[i] = tdata[i];
		}
	return changed;
}

/* Reconfigure LCD display */
static int picolcd_fb_reset(struct picolcd_data *data, int clear)
{
	struct hid_report *report = picolcd_out_report(REPORT_LCD_CMD, data->hdev);
	int i, j;
	unsigned long flags;
	static const u8 mapcmd[8] = { 0x00, 0x02, 0x00, 0x64, 0x3f, 0x00, 0x64, 0xc0 };

	if (!report || report->maxfield != 1)
		return -ENODEV;

	spin_lock_irqsave(&data->lock, flags);
	for (i = 0; i < 4; i++) {
		for (j = 0; j < report->field[0]->maxusage; j++)
			if (j == 0)
				hid_set_field(report->field[0], j, i << 2);
			else if (j < sizeof(mapcmd))
				hid_set_field(report->field[0], j, mapcmd[j]);
			else
				hid_set_field(report->field[0], j, 0);
		usbhid_submit_report(data->hdev, report, USB_DIR_OUT);
	}

	data->status |= PICOLCD_READY_FB;
	spin_unlock_irqrestore(&data->lock, flags);

	if (data->fb_bitmap) {
		if (clear) {
			memset(data->fb_vbitmap, 0, PICOLCDFB_SIZE);
			memset(data->fb_bitmap, 0, PICOLCDFB_SIZE*data->fb_bpp);
		}
		data->fb_force = 1;
	}

	/* schedule first output of framebuffer */
	if (data->fb_info)
		schedule_delayed_work(&data->fb_info->deferred_work, 0);

	return 0;
}

/* Update fb_vbitmap from the screen_base and send changed tiles to device */
static void picolcd_fb_update(struct picolcd_data *data)
{
	int chip, tile, n;
	unsigned long flags;

	if (!data)
		return;

	spin_lock_irqsave(&data->lock, flags);
	if (!(data->status & PICOLCD_READY_FB)) {
		spin_unlock_irqrestore(&data->lock, flags);
		picolcd_fb_reset(data, 0);
	} else {
		spin_unlock_irqrestore(&data->lock, flags);
	}

	/*
	 * Translate the framebuffer into the format needed by the PicoLCD.
	 * See display layout above.
	 * Do this one tile after the other and push those tiles that changed.
	 *
	 * Wait for our IO to complete as otherwise we might flood the queue!
	 */
	n = 0;
	for (chip = 0; chip < 4; chip++)
		for (tile = 0; tile < 8; tile++)
			if (picolcd_fb_update_tile(data->fb_vbitmap,
					data->fb_bitmap, data->fb_bpp, chip, tile) ||
				data->fb_force) {
				n += 2;
				if (!data->fb_info->par)
					return; /* device lost! */
				if (n >= HID_OUTPUT_FIFO_SIZE / 2) {
					usbhid_wait_io(data->hdev);
					n = 0;
				}
				picolcd_fb_send_tile(data->hdev, chip, tile);
			}
	data->fb_force = false;
	if (n)
		usbhid_wait_io(data->hdev);
}

/* Stub to call the system default and update the image on the picoLCD */
static void picolcd_fb_fillrect(struct fb_info *info,
		const struct fb_fillrect *rect)
{
	if (!info->par)
		return;
	sys_fillrect(info, rect);

	schedule_delayed_work(&info->deferred_work, 0);
}

/* Stub to call the system default and update the image on the picoLCD */
static void picolcd_fb_copyarea(struct fb_info *info,
		const struct fb_copyarea *area)
{
	if (!info->par)
		return;
	sys_copyarea(info, area);

	schedule_delayed_work(&info->deferred_work, 0);
}

/* Stub to call the system default and update the image on the picoLCD */
static void picolcd_fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	if (!info->par)
		return;
	sys_imageblit(info, image);

	schedule_delayed_work(&info->deferred_work, 0);
}

/*
 * this is the slow path from userspace. they can seek and write to
 * the fb. it's inefficient to do anything less than a full screen draw
 */
static ssize_t picolcd_fb_write(struct fb_info *info, const char __user *buf,
		size_t count, loff_t *ppos)
{
	ssize_t ret;
	if (!info->par)
		return -ENODEV;
	ret = fb_sys_write(info, buf, count, ppos);
	if (ret >= 0)
		schedule_delayed_work(&info->deferred_work, 0);
	return ret;
}

static int picolcd_fb_blank(int blank, struct fb_info *info)
{
	if (!info->par)
		return -ENODEV;
	/* We let fb notification do this for us via lcd/backlight device */
	return 0;
}

static void picolcd_fb_destroy(struct fb_info *info)
{
	struct picolcd_data *data = info->par;
	u32 *ref_cnt = info->pseudo_palette;
	int may_release;

	info->par = NULL;
	if (data)
		data->fb_info = NULL;
	fb_deferred_io_cleanup(info);

	ref_cnt--;
	mutex_lock(&info->lock);
	(*ref_cnt)--;
	may_release = !*ref_cnt;
	mutex_unlock(&info->lock);
	if (may_release) {
		vfree((u8 *)info->fix.smem_start);
		framebuffer_release(info);
	}
}

static int picolcd_fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	__u32 bpp      = var->bits_per_pixel;
	__u32 activate = var->activate;

	/* only allow 1/8 bit depth (8-bit is grayscale) */
	*var = picolcdfb_var;
	var->activate = activate;
	if (bpp >= 8) {
		var->bits_per_pixel = 8;
		var->red.length     = 8;
		var->green.length   = 8;
		var->blue.length    = 8;
	} else {
		var->bits_per_pixel = 1;
		var->red.length     = 1;
		var->green.length   = 1;
		var->blue.length    = 1;
	}
	return 0;
}

static int picolcd_set_par(struct fb_info *info)
{
	struct picolcd_data *data = info->par;
	u8 *tmp_fb, *o_fb;
	if (!data)
		return -ENODEV;
	if (info->var.bits_per_pixel == data->fb_bpp)
		return 0;
	/* switch between 1/8 bit depths */
	if (info->var.bits_per_pixel != 1 && info->var.bits_per_pixel != 8)
		return -EINVAL;

	o_fb   = data->fb_bitmap;
	tmp_fb = kmalloc(PICOLCDFB_SIZE*info->var.bits_per_pixel, GFP_KERNEL);
	if (!tmp_fb)
		return -ENOMEM;

	/* translate FB content to new bits-per-pixel */
	if (info->var.bits_per_pixel == 1) {
		int i, b;
		for (i = 0; i < PICOLCDFB_SIZE; i++) {
			u8 p = 0;
			for (b = 0; b < 8; b++) {
				p <<= 1;
				p |= o_fb[i*8+b] ? 0x01 : 0x00;
			}
			tmp_fb[i] = p;
		}
		memcpy(o_fb, tmp_fb, PICOLCDFB_SIZE);
		info->fix.visual = FB_VISUAL_MONO01;
		info->fix.line_length = PICOLCDFB_WIDTH / 8;
	} else {
		int i;
		memcpy(tmp_fb, o_fb, PICOLCDFB_SIZE);
		for (i = 0; i < PICOLCDFB_SIZE * 8; i++)
			o_fb[i] = tmp_fb[i/8] & (0x01 << (7 - i % 8)) ? 0xff : 0x00;
		info->fix.visual = FB_VISUAL_DIRECTCOLOR;
		info->fix.line_length = PICOLCDFB_WIDTH;
	}

	kfree(tmp_fb);
	data->fb_bpp      = info->var.bits_per_pixel;
	return 0;
}

/* Do refcounting on our FB and cleanup per worker if FB is
 * closed after unplug of our device
 * (fb_release holds info->lock and still touches info after
 *  we return so we can't release it immediately.
 */
struct picolcd_fb_cleanup_item {
	struct fb_info *info;
	struct picolcd_fb_cleanup_item *next;
};
static struct picolcd_fb_cleanup_item *fb_pending;
static DEFINE_SPINLOCK(fb_pending_lock);

static void picolcd_fb_do_cleanup(struct work_struct *data)
{
	struct picolcd_fb_cleanup_item *item;
	unsigned long flags;

	do {
		spin_lock_irqsave(&fb_pending_lock, flags);
		item = fb_pending;
		fb_pending = item ? item->next : NULL;
		spin_unlock_irqrestore(&fb_pending_lock, flags);

		if (item) {
			u8 *fb = (u8 *)item->info->fix.smem_start;
			/* make sure we do not race against fb core when
			 * releasing */
			mutex_lock(&item->info->lock);
			mutex_unlock(&item->info->lock);
			framebuffer_release(item->info);
			vfree(fb);
		}
	} while (item);
}

static DECLARE_WORK(picolcd_fb_cleanup, picolcd_fb_do_cleanup);

static int picolcd_fb_open(struct fb_info *info, int u)
{
	u32 *ref_cnt = info->pseudo_palette;
	ref_cnt--;

	(*ref_cnt)++;
	return 0;
}

static int picolcd_fb_release(struct fb_info *info, int u)
{
	u32 *ref_cnt = info->pseudo_palette;
	ref_cnt--;

	(*ref_cnt)++;
	if (!*ref_cnt) {
		unsigned long flags;
		struct picolcd_fb_cleanup_item *item = (struct picolcd_fb_cleanup_item *)ref_cnt;
		item--;
		spin_lock_irqsave(&fb_pending_lock, flags);
		item->next = fb_pending;
		fb_pending = item;
		spin_unlock_irqrestore(&fb_pending_lock, flags);
		schedule_work(&picolcd_fb_cleanup);
	}
	return 0;
}

/* Note this can't be const because of struct fb_info definition */
static struct fb_ops picolcdfb_ops = {
	.owner        = THIS_MODULE,
	.fb_destroy   = picolcd_fb_destroy,
	.fb_open      = picolcd_fb_open,
	.fb_release   = picolcd_fb_release,
	.fb_read      = fb_sys_read,
	.fb_write     = picolcd_fb_write,
	.fb_blank     = picolcd_fb_blank,
	.fb_fillrect  = picolcd_fb_fillrect,
	.fb_copyarea  = picolcd_fb_copyarea,
	.fb_imageblit = picolcd_fb_imageblit,
	.fb_check_var = picolcd_fb_check_var,
	.fb_set_par   = picolcd_set_par,
};


/* Callback from deferred IO workqueue */
static void picolcd_fb_deferred_io(struct fb_info *info, struct list_head *pagelist)
{
	picolcd_fb_update(info->par);
}

static const struct fb_deferred_io picolcd_fb_defio = {
	.delay = HZ / PICOLCDFB_UPDATE_RATE_DEFAULT,
	.deferred_io = picolcd_fb_deferred_io,
};


/*
 * The "fb_update_rate" sysfs attribute
 */
static ssize_t picolcd_fb_update_rate_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct picolcd_data *data = dev_get_drvdata(dev);
	unsigned i, fb_update_rate = data->fb_update_rate;
	size_t ret = 0;

	for (i = 1; i <= PICOLCDFB_UPDATE_RATE_LIMIT; i++)
		if (ret >= PAGE_SIZE)
			break;
		else if (i == fb_update_rate)
			ret += snprintf(buf+ret, PAGE_SIZE-ret, "[%u] ", i);
		else
			ret += snprintf(buf+ret, PAGE_SIZE-ret, "%u ", i);
	if (ret > 0)
		buf[min(ret, (size_t)PAGE_SIZE)-1] = '\n';
	return ret;
}

static ssize_t picolcd_fb_update_rate_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct picolcd_data *data = dev_get_drvdata(dev);
	int i;
	unsigned u;

	if (count < 1 || count > 10)
		return -EINVAL;

	i = sscanf(buf, "%u", &u);
	if (i != 1)
		return -EINVAL;

	if (u > PICOLCDFB_UPDATE_RATE_LIMIT)
		return -ERANGE;
	else if (u == 0)
		u = PICOLCDFB_UPDATE_RATE_DEFAULT;

	data->fb_update_rate = u;
	data->fb_defio.delay = HZ / data->fb_update_rate;
	return count;
}

static DEVICE_ATTR(fb_update_rate, 0666, picolcd_fb_update_rate_show,
		picolcd_fb_update_rate_store);

/* initialize Framebuffer device */
static int picolcd_init_framebuffer(struct picolcd_data *data)
{
	struct device *dev = &data->hdev->dev;
	struct fb_info *info = NULL;
	int i, error = -ENOMEM;
	u8 *fb_vbitmap = NULL;
	u8 *fb_bitmap  = NULL;
	u32 *palette;

	fb_bitmap = vmalloc(PICOLCDFB_SIZE*8);
	if (fb_bitmap == NULL) {
		dev_err(dev, "can't get a free page for framebuffer\n");
		goto err_nomem;
	}

	fb_vbitmap = kmalloc(PICOLCDFB_SIZE, GFP_KERNEL);
	if (fb_vbitmap == NULL) {
		dev_err(dev, "can't alloc vbitmap image buffer\n");
		goto err_nomem;
	}

	data->fb_update_rate = PICOLCDFB_UPDATE_RATE_DEFAULT;
	data->fb_defio = picolcd_fb_defio;
	/* The extra memory is:
	 * - struct picolcd_fb_cleanup_item
	 * - u32 for ref_count
	 * - 256*u32 for pseudo_palette
	 */
	info = framebuffer_alloc(257 * sizeof(u32) + sizeof(struct picolcd_fb_cleanup_item), dev);
	if (info == NULL) {
		dev_err(dev, "failed to allocate a framebuffer\n");
		goto err_nomem;
	}

	palette  = info->par + sizeof(struct picolcd_fb_cleanup_item);
	*palette = 1;
	palette++;
	for (i = 0; i < 256; i++)
		palette[i] = i > 0 && i < 16 ? 0xff : 0;
	info->pseudo_palette = palette;
	info->fbdefio = &data->fb_defio;
	info->screen_base = (char __force __iomem *)fb_bitmap;
	info->fbops = &picolcdfb_ops;
	info->var = picolcdfb_var;
	info->fix = picolcdfb_fix;
	info->fix.smem_len   = PICOLCDFB_SIZE*8;
	info->fix.smem_start = (unsigned long)fb_bitmap;
	info->par = data;
	info->flags = FBINFO_FLAG_DEFAULT;

	data->fb_vbitmap = fb_vbitmap;
	data->fb_bitmap  = fb_bitmap;
	data->fb_bpp     = picolcdfb_var.bits_per_pixel;
	error = picolcd_fb_reset(data, 1);
	if (error) {
		dev_err(dev, "failed to configure display\n");
		goto err_cleanup;
	}
	error = device_create_file(dev, &dev_attr_fb_update_rate);
	if (error) {
		dev_err(dev, "failed to create sysfs attributes\n");
		goto err_cleanup;
	}
	fb_deferred_io_init(info);
	data->fb_info    = info;
	error = register_framebuffer(info);
	if (error) {
		dev_err(dev, "failed to register framebuffer\n");
		goto err_sysfs;
	}
	/* schedule first output of framebuffer */
	data->fb_force = 1;
	schedule_delayed_work(&info->deferred_work, 0);
	return 0;

err_sysfs:
	fb_deferred_io_cleanup(info);
	device_remove_file(dev, &dev_attr_fb_update_rate);
err_cleanup:
	data->fb_vbitmap = NULL;
	data->fb_bitmap  = NULL;
	data->fb_bpp     = 0;
	data->fb_info    = NULL;

err_nomem:
	framebuffer_release(info);
	vfree(fb_bitmap);
	kfree(fb_vbitmap);
	return error;
}

static void picolcd_exit_framebuffer(struct picolcd_data *data)
{
	struct fb_info *info = data->fb_info;
	u8 *fb_vbitmap = data->fb_vbitmap;

	if (!info)
		return;

	info->par = NULL;
	device_remove_file(&data->hdev->dev, &dev_attr_fb_update_rate);
	unregister_framebuffer(info);
	data->fb_vbitmap = NULL;
	data->fb_bitmap  = NULL;
	data->fb_bpp     = 0;
	data->fb_info    = NULL;
	kfree(fb_vbitmap);
}

#define picolcd_fbinfo(d) ((d)->fb_info)
#else
static inline int picolcd_fb_reset(struct picolcd_data *data, int clear)
{
	return 0;
}
static inline int picolcd_init_framebuffer(struct picolcd_data *data)
{
	return 0;
}
static inline void picolcd_exit_framebuffer(struct picolcd_data *data)
{
}
#define picolcd_fbinfo(d) NULL
#endif /* CONFIG_HID_PICOLCD_FB */

#ifdef CONFIG_HID_PICOLCD_BACKLIGHT
/*
 * backlight class device
 */
static int picolcd_get_brightness(struct backlight_device *bdev)
{
	struct picolcd_data *data = bl_get_data(bdev);
	return data->lcd_brightness;
}

static int picolcd_set_brightness(struct backlight_device *bdev)
{
	struct picolcd_data *data = bl_get_data(bdev);
	struct hid_report *report = picolcd_out_report(REPORT_BRIGHTNESS, data->hdev);
	unsigned long flags;

	if (!report || report->maxfield != 1 || report->field[0]->report_count != 1)
		return -ENODEV;

	data->lcd_brightness = bdev->props.brightness & 0x0ff;
	data->lcd_power      = bdev->props.power;
	spin_lock_irqsave(&data->lock, flags);
	hid_set_field(report->field[0], 0, data->lcd_power == FB_BLANK_UNBLANK ? data->lcd_brightness : 0);
	usbhid_submit_report(data->hdev, report, USB_DIR_OUT);
	spin_unlock_irqrestore(&data->lock, flags);
	return 0;
}

static int picolcd_check_bl_fb(struct backlight_device *bdev, struct fb_info *fb)
{
	return fb && fb == picolcd_fbinfo((struct picolcd_data *)bl_get_data(bdev));
}

static const struct backlight_ops picolcd_blops = {
	.update_status  = picolcd_set_brightness,
	.get_brightness = picolcd_get_brightness,
	.check_fb       = picolcd_check_bl_fb,
};

static int picolcd_init_backlight(struct picolcd_data *data, struct hid_report *report)
{
	struct device *dev = &data->hdev->dev;
	struct backlight_device *bdev;
	struct backlight_properties props;
	if (!report)
		return -ENODEV;
	if (report->maxfield != 1 || report->field[0]->report_count != 1 ||
			report->field[0]->report_size != 8) {
		dev_err(dev, "unsupported BRIGHTNESS report");
		return -EINVAL;
	}

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = 0xff;
	bdev = backlight_device_register(dev_name(dev), dev, data,
			&picolcd_blops, &props);
	if (IS_ERR(bdev)) {
		dev_err(dev, "failed to register backlight\n");
		return PTR_ERR(bdev);
	}
	bdev->props.brightness     = 0xff;
	data->lcd_brightness       = 0xff;
	data->backlight            = bdev;
	picolcd_set_brightness(bdev);
	return 0;
}

static void picolcd_exit_backlight(struct picolcd_data *data)
{
	struct backlight_device *bdev = data->backlight;

	data->backlight = NULL;
	if (bdev)
		backlight_device_unregister(bdev);
}

static inline int picolcd_resume_backlight(struct picolcd_data *data)
{
	if (!data->backlight)
		return 0;
	return picolcd_set_brightness(data->backlight);
}

#ifdef CONFIG_PM
static void picolcd_suspend_backlight(struct picolcd_data *data)
{
	int bl_power = data->lcd_power;
	if (!data->backlight)
		return;

	data->backlight->props.power = FB_BLANK_POWERDOWN;
	picolcd_set_brightness(data->backlight);
	data->lcd_power = data->backlight->props.power = bl_power;
}
#endif /* CONFIG_PM */
#else
static inline int picolcd_init_backlight(struct picolcd_data *data,
		struct hid_report *report)
{
	return 0;
}
static inline void picolcd_exit_backlight(struct picolcd_data *data)
{
}
static inline int picolcd_resume_backlight(struct picolcd_data *data)
{
	return 0;
}
static inline void picolcd_suspend_backlight(struct picolcd_data *data)
{
}
#endif /* CONFIG_HID_PICOLCD_BACKLIGHT */

#ifdef CONFIG_HID_PICOLCD_LCD
/*
 * lcd class device
 */
static int picolcd_get_contrast(struct lcd_device *ldev)
{
	struct picolcd_data *data = lcd_get_data(ldev);
	return data->lcd_contrast;
}

static int picolcd_set_contrast(struct lcd_device *ldev, int contrast)
{
	struct picolcd_data *data = lcd_get_data(ldev);
	struct hid_report *report = picolcd_out_report(REPORT_CONTRAST, data->hdev);
	unsigned long flags;

	if (!report || report->maxfield != 1 || report->field[0]->report_count != 1)
		return -ENODEV;

	data->lcd_contrast = contrast & 0x0ff;
	spin_lock_irqsave(&data->lock, flags);
	hid_set_field(report->field[0], 0, data->lcd_contrast);
	usbhid_submit_report(data->hdev, report, USB_DIR_OUT);
	spin_unlock_irqrestore(&data->lock, flags);
	return 0;
}

static int picolcd_check_lcd_fb(struct lcd_device *ldev, struct fb_info *fb)
{
	return fb && fb == picolcd_fbinfo((struct picolcd_data *)lcd_get_data(ldev));
}

static struct lcd_ops picolcd_lcdops = {
	.get_contrast   = picolcd_get_contrast,
	.set_contrast   = picolcd_set_contrast,
	.check_fb       = picolcd_check_lcd_fb,
};

static int picolcd_init_lcd(struct picolcd_data *data, struct hid_report *report)
{
	struct device *dev = &data->hdev->dev;
	struct lcd_device *ldev;

	if (!report)
		return -ENODEV;
	if (report->maxfield != 1 || report->field[0]->report_count != 1 ||
			report->field[0]->report_size != 8) {
		dev_err(dev, "unsupported CONTRAST report");
		return -EINVAL;
	}

	ldev = lcd_device_register(dev_name(dev), dev, data, &picolcd_lcdops);
	if (IS_ERR(ldev)) {
		dev_err(dev, "failed to register LCD\n");
		return PTR_ERR(ldev);
	}
	ldev->props.max_contrast = 0x0ff;
	data->lcd_contrast = 0xe5;
	data->lcd = ldev;
	picolcd_set_contrast(ldev, 0xe5);
	return 0;
}

static void picolcd_exit_lcd(struct picolcd_data *data)
{
	struct lcd_device *ldev = data->lcd;

	data->lcd = NULL;
	if (ldev)
		lcd_device_unregister(ldev);
}

static inline int picolcd_resume_lcd(struct picolcd_data *data)
{
	if (!data->lcd)
		return 0;
	return picolcd_set_contrast(data->lcd, data->lcd_contrast);
}
#else
static inline int picolcd_init_lcd(struct picolcd_data *data,
		struct hid_report *report)
{
	return 0;
}
static inline void picolcd_exit_lcd(struct picolcd_data *data)
{
}
static inline int picolcd_resume_lcd(struct picolcd_data *data)
{
	return 0;
}
#endif /* CONFIG_HID_PICOLCD_LCD */

#ifdef CONFIG_HID_PICOLCD_LEDS
/**
 * LED class device
 */
static void picolcd_leds_set(struct picolcd_data *data)
{
	struct hid_report *report;
	unsigned long flags;

	if (!data->led[0])
		return;
	report = picolcd_out_report(REPORT_LED_STATE, data->hdev);
	if (!report || report->maxfield != 1 || report->field[0]->report_count != 1)
		return;

	spin_lock_irqsave(&data->lock, flags);
	hid_set_field(report->field[0], 0, data->led_state);
	usbhid_submit_report(data->hdev, report, USB_DIR_OUT);
	spin_unlock_irqrestore(&data->lock, flags);
}

static void picolcd_led_set_brightness(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct device *dev;
	struct hid_device *hdev;
	struct picolcd_data *data;
	int i, state = 0;

	dev  = led_cdev->dev->parent;
	hdev = container_of(dev, struct hid_device, dev);
	data = hid_get_drvdata(hdev);
	for (i = 0; i < 8; i++) {
		if (led_cdev != data->led[i])
			continue;
		state = (data->led_state >> i) & 1;
		if (value == LED_OFF && state) {
			data->led_state &= ~(1 << i);
			picolcd_leds_set(data);
		} else if (value != LED_OFF && !state) {
			data->led_state |= 1 << i;
			picolcd_leds_set(data);
		}
		break;
	}
}

static enum led_brightness picolcd_led_get_brightness(struct led_classdev *led_cdev)
{
	struct device *dev;
	struct hid_device *hdev;
	struct picolcd_data *data;
	int i, value = 0;

	dev  = led_cdev->dev->parent;
	hdev = container_of(dev, struct hid_device, dev);
	data = hid_get_drvdata(hdev);
	for (i = 0; i < 8; i++)
		if (led_cdev == data->led[i]) {
			value = (data->led_state >> i) & 1;
			break;
		}
	return value ? LED_FULL : LED_OFF;
}

static int picolcd_init_leds(struct picolcd_data *data, struct hid_report *report)
{
	struct device *dev = &data->hdev->dev;
	struct led_classdev *led;
	size_t name_sz = strlen(dev_name(dev)) + 8;
	char *name;
	int i, ret = 0;

	if (!report)
		return -ENODEV;
	if (report->maxfield != 1 || report->field[0]->report_count != 1 ||
			report->field[0]->report_size != 8) {
		dev_err(dev, "unsupported LED_STATE report");
		return -EINVAL;
	}

	for (i = 0; i < 8; i++) {
		led = kzalloc(sizeof(struct led_classdev)+name_sz, GFP_KERNEL);
		if (!led) {
			dev_err(dev, "can't allocate memory for LED %d\n", i);
			ret = -ENOMEM;
			goto err;
		}
		name = (void *)(&led[1]);
		snprintf(name, name_sz, "%s::GPO%d", dev_name(dev), i);
		led->name = name;
		led->brightness = 0;
		led->max_brightness = 1;
		led->brightness_get = picolcd_led_get_brightness;
		led->brightness_set = picolcd_led_set_brightness;

		data->led[i] = led;
		ret = led_classdev_register(dev, data->led[i]);
		if (ret) {
			data->led[i] = NULL;
			kfree(led);
			dev_err(dev, "can't register LED %d\n", i);
			goto err;
		}
	}
	return 0;
err:
	for (i = 0; i < 8; i++)
		if (data->led[i]) {
			led = data->led[i];
			data->led[i] = NULL;
			led_classdev_unregister(led);
			kfree(led);
		}
	return ret;
}

static void picolcd_exit_leds(struct picolcd_data *data)
{
	struct led_classdev *led;
	int i;

	for (i = 0; i < 8; i++) {
		led = data->led[i];
		data->led[i] = NULL;
		if (!led)
			continue;
		led_classdev_unregister(led);
		kfree(led);
	}
}

#else
static inline int picolcd_init_leds(struct picolcd_data *data,
		struct hid_report *report)
{
	return 0;
}
static inline void picolcd_exit_leds(struct picolcd_data *data)
{
}
static inline int picolcd_leds_set(struct picolcd_data *data)
{
	return 0;
}
#endif /* CONFIG_HID_PICOLCD_LEDS */

/*
 * input class device
 */
static int picolcd_raw_keypad(struct picolcd_data *data,
		struct hid_report *report, u8 *raw_data, int size)
{
	/*
	 * Keypad event
	 * First and second data bytes list currently pressed keys,
	 * 0x00 means no key and at most 2 keys may be pressed at same time
	 */
	int i, j;

	/* determine newly pressed keys */
	for (i = 0; i < size; i++) {
		unsigned int key_code;
		if (raw_data[i] == 0)
			continue;
		for (j = 0; j < sizeof(data->pressed_keys); j++)
			if (data->pressed_keys[j] == raw_data[i])
				goto key_already_down;
		for (j = 0; j < sizeof(data->pressed_keys); j++)
			if (data->pressed_keys[j] == 0) {
				data->pressed_keys[j] = raw_data[i];
				break;
			}
		input_event(data->input_keys, EV_MSC, MSC_SCAN, raw_data[i]);
		if (raw_data[i] < PICOLCD_KEYS)
			key_code = data->keycode[raw_data[i]];
		else
			key_code = KEY_UNKNOWN;
		if (key_code != KEY_UNKNOWN) {
			dbg_hid(PICOLCD_NAME " got key press for %u:%d",
					raw_data[i], key_code);
			input_report_key(data->input_keys, key_code, 1);
		}
		input_sync(data->input_keys);
key_already_down:
		continue;
	}

	/* determine newly released keys */
	for (j = 0; j < sizeof(data->pressed_keys); j++) {
		unsigned int key_code;
		if (data->pressed_keys[j] == 0)
			continue;
		for (i = 0; i < size; i++)
			if (data->pressed_keys[j] == raw_data[i])
				goto key_still_down;
		input_event(data->input_keys, EV_MSC, MSC_SCAN, data->pressed_keys[j]);
		if (data->pressed_keys[j] < PICOLCD_KEYS)
			key_code = data->keycode[data->pressed_keys[j]];
		else
			key_code = KEY_UNKNOWN;
		if (key_code != KEY_UNKNOWN) {
			dbg_hid(PICOLCD_NAME " got key release for %u:%d",
					data->pressed_keys[j], key_code);
			input_report_key(data->input_keys, key_code, 0);
		}
		input_sync(data->input_keys);
		data->pressed_keys[j] = 0;
key_still_down:
		continue;
	}
	return 1;
}

static int picolcd_raw_cir(struct picolcd_data *data,
		struct hid_report *report, u8 *raw_data, int size)
{
	/* Need understanding of CIR data format to implement ... */
	return 1;
}

static int picolcd_check_version(struct hid_device *hdev)
{
	struct picolcd_data *data = hid_get_drvdata(hdev);
	struct picolcd_pending *verinfo;
	int ret = 0;

	if (!data)
		return -ENODEV;

	verinfo = picolcd_send_and_wait(hdev, REPORT_VERSION, NULL, 0);
	if (!verinfo) {
		hid_err(hdev, "no version response from PicoLCD\n");
		return -ENODEV;
	}

	if (verinfo->raw_size == 2) {
		data->version[0] = verinfo->raw_data[1];
		data->version[1] = verinfo->raw_data[0];
		if (data->status & PICOLCD_BOOTLOADER) {
			hid_info(hdev, "PicoLCD, bootloader version %d.%d\n",
				 verinfo->raw_data[1], verinfo->raw_data[0]);
		} else {
			hid_info(hdev, "PicoLCD, firmware version %d.%d\n",
				 verinfo->raw_data[1], verinfo->raw_data[0]);
		}
	} else {
		hid_err(hdev, "confused, got unexpected version response from PicoLCD\n");
		ret = -EINVAL;
	}
	kfree(verinfo);
	return ret;
}

/*
 * Reset our device and wait for answer to VERSION request
 */
static int picolcd_reset(struct hid_device *hdev)
{
	struct picolcd_data *data = hid_get_drvdata(hdev);
	struct hid_report *report = picolcd_out_report(REPORT_RESET, hdev);
	unsigned long flags;
	int error;

	if (!data || !report || report->maxfield != 1)
		return -ENODEV;

	spin_lock_irqsave(&data->lock, flags);
	if (hdev->product == USB_DEVICE_ID_PICOLCD_BOOTLOADER)
		data->status |= PICOLCD_BOOTLOADER;

	/* perform the reset */
	hid_set_field(report->field[0], 0, 1);
	usbhid_submit_report(hdev, report, USB_DIR_OUT);
	spin_unlock_irqrestore(&data->lock, flags);

	error = picolcd_check_version(hdev);
	if (error)
		return error;

	picolcd_resume_lcd(data);
	picolcd_resume_backlight(data);
#ifdef CONFIG_HID_PICOLCD_FB
	if (data->fb_info)
		schedule_delayed_work(&data->fb_info->deferred_work, 0);
#endif /* CONFIG_HID_PICOLCD_FB */

	picolcd_leds_set(data);
	return 0;
}

/*
 * The "operation_mode" sysfs attribute
 */
static ssize_t picolcd_operation_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct picolcd_data *data = dev_get_drvdata(dev);

	if (data->status & PICOLCD_BOOTLOADER)
		return snprintf(buf, PAGE_SIZE, "[bootloader] lcd\n");
	else
		return snprintf(buf, PAGE_SIZE, "bootloader [lcd]\n");
}

static ssize_t picolcd_operation_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct picolcd_data *data = dev_get_drvdata(dev);
	struct hid_report *report = NULL;
	size_t cnt = count;
	int timeout = data->opmode_delay;
	unsigned long flags;

	if (cnt >= 3 && strncmp("lcd", buf, 3) == 0) {
		if (data->status & PICOLCD_BOOTLOADER)
			report = picolcd_out_report(REPORT_EXIT_FLASHER, data->hdev);
		buf += 3;
		cnt -= 3;
	} else if (cnt >= 10 && strncmp("bootloader", buf, 10) == 0) {
		if (!(data->status & PICOLCD_BOOTLOADER))
			report = picolcd_out_report(REPORT_EXIT_KEYBOARD, data->hdev);
		buf += 10;
		cnt -= 10;
	}
	if (!report)
		return -EINVAL;

	while (cnt > 0 && (buf[cnt-1] == '\n' || buf[cnt-1] == '\r'))
		cnt--;
	if (cnt != 0)
		return -EINVAL;

	spin_lock_irqsave(&data->lock, flags);
	hid_set_field(report->field[0], 0, timeout & 0xff);
	hid_set_field(report->field[0], 1, (timeout >> 8) & 0xff);
	usbhid_submit_report(data->hdev, report, USB_DIR_OUT);
	spin_unlock_irqrestore(&data->lock, flags);
	return count;
}

static DEVICE_ATTR(operation_mode, 0644, picolcd_operation_mode_show,
		picolcd_operation_mode_store);

/*
 * The "operation_mode_delay" sysfs attribute
 */
static ssize_t picolcd_operation_mode_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct picolcd_data *data = dev_get_drvdata(dev);

	return snprintf(buf, PAGE_SIZE, "%hu\n", data->opmode_delay);
}

static ssize_t picolcd_operation_mode_delay_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct picolcd_data *data = dev_get_drvdata(dev);
	unsigned u;
	if (sscanf(buf, "%u", &u) != 1)
		return -EINVAL;
	if (u > 30000)
		return -EINVAL;
	else
		data->opmode_delay = u;
	return count;
}

static DEVICE_ATTR(operation_mode_delay, 0644, picolcd_operation_mode_delay_show,
		picolcd_operation_mode_delay_store);


#ifdef CONFIG_DEBUG_FS
/*
 * The "reset" file
 */
static int picolcd_debug_reset_show(struct seq_file *f, void *p)
{
	if (picolcd_fbinfo((struct picolcd_data *)f->private))
		seq_printf(f, "all fb\n");
	else
		seq_printf(f, "all\n");
	return 0;
}

static int picolcd_debug_reset_open(struct inode *inode, struct file *f)
{
	return single_open(f, picolcd_debug_reset_show, inode->i_private);
}

static ssize_t picolcd_debug_reset_write(struct file *f, const char __user *user_buf,
		size_t count, loff_t *ppos)
{
	struct picolcd_data *data = ((struct seq_file *)f->private_data)->private;
	char buf[32];
	size_t cnt = min(count, sizeof(buf)-1);
	if (copy_from_user(buf, user_buf, cnt))
		return -EFAULT;

	while (cnt > 0 && (buf[cnt-1] == ' ' || buf[cnt-1] == '\n'))
		cnt--;
	buf[cnt] = '\0';
	if (strcmp(buf, "all") == 0) {
		picolcd_reset(data->hdev);
		picolcd_fb_reset(data, 1);
	} else if (strcmp(buf, "fb") == 0) {
		picolcd_fb_reset(data, 1);
	} else {
		return -EINVAL;
	}
	return count;
}

static const struct file_operations picolcd_debug_reset_fops = {
	.owner    = THIS_MODULE,
	.open     = picolcd_debug_reset_open,
	.read     = seq_read,
	.llseek   = seq_lseek,
	.write    = picolcd_debug_reset_write,
	.release  = single_release,
};

/*
 * The "eeprom" file
 */
static int picolcd_debug_eeprom_open(struct inode *i, struct file *f)
{
	f->private_data = i->i_private;
	return 0;
}

static ssize_t picolcd_debug_eeprom_read(struct file *f, char __user *u,
		size_t s, loff_t *off)
{
	struct picolcd_data *data = f->private_data;
	struct picolcd_pending *resp;
	u8 raw_data[3];
	ssize_t ret = -EIO;

	if (s == 0)
		return -EINVAL;
	if (*off > 0x0ff)
		return 0;

	/* prepare buffer with info about what we want to read (addr & len) */
	raw_data[0] = *off & 0xff;
	raw_data[1] = (*off >> 8) & 0xff;
	raw_data[2] = s < 20 ? s : 20;
	if (*off + raw_data[2] > 0xff)
		raw_data[2] = 0x100 - *off;
	resp = picolcd_send_and_wait(data->hdev, REPORT_EE_READ, raw_data,
			sizeof(raw_data));
	if (!resp)
		return -EIO;

	if (resp->in_report && resp->in_report->id == REPORT_EE_DATA) {
		/* successful read :) */
		ret = resp->raw_data[2];
		if (ret > s)
			ret = s;
		if (copy_to_user(u, resp->raw_data+3, ret))
			ret = -EFAULT;
		else
			*off += ret;
	} /* anything else is some kind of IO error */

	kfree(resp);
	return ret;
}

static ssize_t picolcd_debug_eeprom_write(struct file *f, const char __user *u,
		size_t s, loff_t *off)
{
	struct picolcd_data *data = f->private_data;
	struct picolcd_pending *resp;
	ssize_t ret = -EIO;
	u8 raw_data[23];

	if (s == 0)
		return -EINVAL;
	if (*off > 0x0ff)
		return -ENOSPC;

	memset(raw_data, 0, sizeof(raw_data));
	raw_data[0] = *off & 0xff;
	raw_data[1] = (*off >> 8) & 0xff;
	raw_data[2] = min((size_t)20, s);
	if (*off + raw_data[2] > 0xff)
		raw_data[2] = 0x100 - *off;

	if (copy_from_user(raw_data+3, u, min((u8)20, raw_data[2])))
		return -EFAULT;
	resp = picolcd_send_and_wait(data->hdev, REPORT_EE_WRITE, raw_data,
			sizeof(raw_data));

	if (!resp)
		return -EIO;

	if (resp->in_report && resp->in_report->id == REPORT_EE_DATA) {
		/* check if written data matches */
		if (memcmp(raw_data, resp->raw_data, 3+raw_data[2]) == 0) {
			*off += raw_data[2];
			ret = raw_data[2];
		}
	}
	kfree(resp);
	return ret;
}

/*
 * Notes:
 * - read/write happens in chunks of at most 20 bytes, it's up to userspace
 *   to loop in order to get more data.
 * - on write errors on otherwise correct write request the bytes
 *   that should have been written are in undefined state.
 */
static const struct file_operations picolcd_debug_eeprom_fops = {
	.owner    = THIS_MODULE,
	.open     = picolcd_debug_eeprom_open,
	.read     = picolcd_debug_eeprom_read,
	.write    = picolcd_debug_eeprom_write,
	.llseek   = generic_file_llseek,
};

/*
 * The "flash" file
 */
static int picolcd_debug_flash_open(struct inode *i, struct file *f)
{
	f->private_data = i->i_private;
	return 0;
}

/* record a flash address to buf (bounds check to be done by caller) */
static int _picolcd_flash_setaddr(struct picolcd_data *data, u8 *buf, long off)
{
	buf[0] = off & 0xff;
	buf[1] = (off >> 8) & 0xff;
	if (data->addr_sz == 3)
		buf[2] = (off >> 16) & 0xff;
	return data->addr_sz == 2 ? 2 : 3;
}

/* read a given size of data (bounds check to be done by caller) */
static ssize_t _picolcd_flash_read(struct picolcd_data *data, int report_id,
		char __user *u, size_t s, loff_t *off)
{
	struct picolcd_pending *resp;
	u8 raw_data[4];
	ssize_t ret = 0;
	int len_off, err = -EIO;

	while (s > 0) {
		err = -EIO;
		len_off = _picolcd_flash_setaddr(data, raw_data, *off);
		raw_data[len_off] = s > 32 ? 32 : s;
		resp = picolcd_send_and_wait(data->hdev, report_id, raw_data, len_off+1);
		if (!resp || !resp->in_report)
			goto skip;
		if (resp->in_report->id == REPORT_MEMORY ||
			resp->in_report->id == REPORT_BL_READ_MEMORY) {
			if (memcmp(raw_data, resp->raw_data, len_off+1) != 0)
				goto skip;
			if (copy_to_user(u+ret, resp->raw_data+len_off+1, raw_data[len_off])) {
				err = -EFAULT;
				goto skip;
			}
			*off += raw_data[len_off];
			s    -= raw_data[len_off];
			ret  += raw_data[len_off];
			err   = 0;
		}
skip:
		kfree(resp);
		if (err)
			return ret > 0 ? ret : err;
	}
	return ret;
}

static ssize_t picolcd_debug_flash_read(struct file *f, char __user *u,
		size_t s, loff_t *off)
{
	struct picolcd_data *data = f->private_data;

	if (s == 0)
		return -EINVAL;
	if (*off > 0x05fff)
		return 0;
	if (*off + s > 0x05fff)
		s = 0x06000 - *off;

	if (data->status & PICOLCD_BOOTLOADER)
		return _picolcd_flash_read(data, REPORT_BL_READ_MEMORY, u, s, off);
	else
		return _picolcd_flash_read(data, REPORT_READ_MEMORY, u, s, off);
}

/* erase block aligned to 64bytes boundary */
static ssize_t _picolcd_flash_erase64(struct picolcd_data *data, int report_id,
		loff_t *off)
{
	struct picolcd_pending *resp;
	u8 raw_data[3];
	int len_off;
	ssize_t ret = -EIO;

	if (*off & 0x3f)
		return -EINVAL;

	len_off = _picolcd_flash_setaddr(data, raw_data, *off);
	resp = picolcd_send_and_wait(data->hdev, report_id, raw_data, len_off);
	if (!resp || !resp->in_report)
		goto skip;
	if (resp->in_report->id == REPORT_MEMORY ||
		resp->in_report->id == REPORT_BL_ERASE_MEMORY) {
		if (memcmp(raw_data, resp->raw_data, len_off) != 0)
			goto skip;
		ret = 0;
	}
skip:
	kfree(resp);
	return ret;
}

/* write a given size of data (bounds check to be done by caller) */
static ssize_t _picolcd_flash_write(struct picolcd_data *data, int report_id,
		const char __user *u, size_t s, loff_t *off)
{
	struct picolcd_pending *resp;
	u8 raw_data[36];
	ssize_t ret = 0;
	int len_off, err = -EIO;

	while (s > 0) {
		err = -EIO;
		len_off = _picolcd_flash_setaddr(data, raw_data, *off);
		raw_data[len_off] = s > 32 ? 32 : s;
		if (copy_from_user(raw_data+len_off+1, u, raw_data[len_off])) {
			err = -EFAULT;
			break;
		}
		resp = picolcd_send_and_wait(data->hdev, report_id, raw_data,
				len_off+1+raw_data[len_off]);
		if (!resp || !resp->in_report)
			goto skip;
		if (resp->in_report->id == REPORT_MEMORY ||
			resp->in_report->id == REPORT_BL_WRITE_MEMORY) {
			if (memcmp(raw_data, resp->raw_data, len_off+1+raw_data[len_off]) != 0)
				goto skip;
			*off += raw_data[len_off];
			s    -= raw_data[len_off];
			ret  += raw_data[len_off];
			err   = 0;
		}
skip:
		kfree(resp);
		if (err)
			break;
	}
	return ret > 0 ? ret : err;
}

static ssize_t picolcd_debug_flash_write(struct file *f, const char __user *u,
		size_t s, loff_t *off)
{
	struct picolcd_data *data = f->private_data;
	ssize_t err, ret = 0;
	int report_erase, report_write;

	if (s == 0)
		return -EINVAL;
	if (*off > 0x5fff)
		return -ENOSPC;
	if (s & 0x3f)
		return -EINVAL;
	if (*off & 0x3f)
		return -EINVAL;

	if (data->status & PICOLCD_BOOTLOADER) {
		report_erase = REPORT_BL_ERASE_MEMORY;
		report_write = REPORT_BL_WRITE_MEMORY;
	} else {
		report_erase = REPORT_ERASE_MEMORY;
		report_write = REPORT_WRITE_MEMORY;
	}
	mutex_lock(&data->mutex_flash);
	while (s > 0) {
		err = _picolcd_flash_erase64(data, report_erase, off);
		if (err)
			break;
		err = _picolcd_flash_write(data, report_write, u, 64, off);
		if (err < 0)
			break;
		ret += err;
		*off += err;
		s -= err;
		if (err != 64)
			break;
	}
	mutex_unlock(&data->mutex_flash);
	return ret > 0 ? ret : err;
}

/*
 * Notes:
 * - concurrent writing is prevented by mutex and all writes must be
 *   n*64 bytes and 64-byte aligned, each write being preceded by an
 *   ERASE which erases a 64byte block.
 *   If less than requested was written or an error is returned for an
 *   otherwise correct write request the next 64-byte block which should
 *   have been written is in undefined state (mostly: original, erased,
 *   (half-)written with write error)
 * - reading can happen without special restriction
 */
static const struct file_operations picolcd_debug_flash_fops = {
	.owner    = THIS_MODULE,
	.open     = picolcd_debug_flash_open,
	.read     = picolcd_debug_flash_read,
	.write    = picolcd_debug_flash_write,
	.llseek   = generic_file_llseek,
};


/*
 * Helper code for HID report level dumping/debugging
 */
static const char *error_codes[] = {
	"success", "parameter missing", "data_missing", "block readonly",
	"block not erasable", "block too big", "section overflow",
	"invalid command length", "invalid data length",
};

static void dump_buff_as_hex(char *dst, size_t dst_sz, const u8 *data,
		const size_t data_len)
{
	int i, j;
	for (i = j = 0; i < data_len && j + 3 < dst_sz; i++) {
		dst[j++] = hex_asc[(data[i] >> 4) & 0x0f];
		dst[j++] = hex_asc[data[i] & 0x0f];
		dst[j++] = ' ';
	}
	if (j < dst_sz) {
		dst[j--] = '\0';
		dst[j] = '\n';
	} else
		dst[j] = '\0';
}

static void picolcd_debug_out_report(struct picolcd_data *data,
		struct hid_device *hdev, struct hid_report *report)
{
	u8 raw_data[70];
	int raw_size = (report->size >> 3) + 1;
	char *buff;
#define BUFF_SZ 256

	/* Avoid unnecessary overhead if debugfs is disabled */
	if (!hdev->debug_events)
		return;

	buff = kmalloc(BUFF_SZ, GFP_ATOMIC);
	if (!buff)
		return;

	snprintf(buff, BUFF_SZ, "\nout report %d (size %d) =  ",
			report->id, raw_size);
	hid_debug_event(hdev, buff);
	if (raw_size + 5 > sizeof(raw_data)) {
		kfree(buff);
		hid_debug_event(hdev, " TOO BIG\n");
		return;
	} else {
		raw_data[0] = report->id;
		hid_output_report(report, raw_data);
		dump_buff_as_hex(buff, BUFF_SZ, raw_data, raw_size);
		hid_debug_event(hdev, buff);
	}

	switch (report->id) {
	case REPORT_LED_STATE:
		/* 1 data byte with GPO state */
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_LED_STATE", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tGPO state: 0x%02x\n", raw_data[1]);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_BRIGHTNESS:
		/* 1 data byte with brightness */
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_BRIGHTNESS", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tBrightness: 0x%02x\n", raw_data[1]);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_CONTRAST:
		/* 1 data byte with contrast */
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_CONTRAST", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tContrast: 0x%02x\n", raw_data[1]);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_RESET:
		/* 2 data bytes with reset duration in ms */
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_RESET", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tDuration: 0x%02x%02x (%dms)\n",
				raw_data[2], raw_data[1], raw_data[2] << 8 | raw_data[1]);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_LCD_CMD:
		/* 63 data bytes with LCD commands */
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_LCD_CMD", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		/* TODO: format decoding */
		break;
	case REPORT_LCD_DATA:
		/* 63 data bytes with LCD data */
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_LCD_CMD", report->id, raw_size-1);
		/* TODO: format decoding */
		hid_debug_event(hdev, buff);
		break;
	case REPORT_LCD_CMD_DATA:
		/* 63 data bytes with LCD commands and data */
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_LCD_CMD", report->id, raw_size-1);
		/* TODO: format decoding */
		hid_debug_event(hdev, buff);
		break;
	case REPORT_EE_READ:
		/* 3 data bytes with read area description */
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_EE_READ", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tData address: 0x%02x%02x\n",
				raw_data[2], raw_data[1]);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tData length: %d\n", raw_data[3]);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_EE_WRITE:
		/* 3+1..20 data bytes with write area description */
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_EE_WRITE", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tData address: 0x%02x%02x\n",
				raw_data[2], raw_data[1]);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tData length: %d\n", raw_data[3]);
		hid_debug_event(hdev, buff);
		if (raw_data[3] == 0) {
			snprintf(buff, BUFF_SZ, "\tNo data\n");
		} else if (raw_data[3] + 4 <= raw_size) {
			snprintf(buff, BUFF_SZ, "\tData: ");
			hid_debug_event(hdev, buff);
			dump_buff_as_hex(buff, BUFF_SZ, raw_data+4, raw_data[3]);
		} else {
			snprintf(buff, BUFF_SZ, "\tData overflowed\n");
		}
		hid_debug_event(hdev, buff);
		break;
	case REPORT_ERASE_MEMORY:
	case REPORT_BL_ERASE_MEMORY:
		/* 3 data bytes with pointer inside erase block */
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_ERASE_MEMORY", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		switch (data->addr_sz) {
		case 2:
			snprintf(buff, BUFF_SZ, "\tAddress inside 64 byte block: 0x%02x%02x\n",
					raw_data[2], raw_data[1]);
			break;
		case 3:
			snprintf(buff, BUFF_SZ, "\tAddress inside 64 byte block: 0x%02x%02x%02x\n",
					raw_data[3], raw_data[2], raw_data[1]);
			break;
		default:
			snprintf(buff, BUFF_SZ, "\tNot supported\n");
		}
		hid_debug_event(hdev, buff);
		break;
	case REPORT_READ_MEMORY:
	case REPORT_BL_READ_MEMORY:
		/* 4 data bytes with read area description */
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_READ_MEMORY", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		switch (data->addr_sz) {
		case 2:
			snprintf(buff, BUFF_SZ, "\tData address: 0x%02x%02x\n",
					raw_data[2], raw_data[1]);
			hid_debug_event(hdev, buff);
			snprintf(buff, BUFF_SZ, "\tData length: %d\n", raw_data[3]);
			break;
		case 3:
			snprintf(buff, BUFF_SZ, "\tData address: 0x%02x%02x%02x\n",
					raw_data[3], raw_data[2], raw_data[1]);
			hid_debug_event(hdev, buff);
			snprintf(buff, BUFF_SZ, "\tData length: %d\n", raw_data[4]);
			break;
		default:
			snprintf(buff, BUFF_SZ, "\tNot supported\n");
		}
		hid_debug_event(hdev, buff);
		break;
	case REPORT_WRITE_MEMORY:
	case REPORT_BL_WRITE_MEMORY:
		/* 4+1..32 data bytes with write adrea description */
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_WRITE_MEMORY", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		switch (data->addr_sz) {
		case 2:
			snprintf(buff, BUFF_SZ, "\tData address: 0x%02x%02x\n",
					raw_data[2], raw_data[1]);
			hid_debug_event(hdev, buff);
			snprintf(buff, BUFF_SZ, "\tData length: %d\n", raw_data[3]);
			hid_debug_event(hdev, buff);
			if (raw_data[3] == 0) {
				snprintf(buff, BUFF_SZ, "\tNo data\n");
			} else if (raw_data[3] + 4 <= raw_size) {
				snprintf(buff, BUFF_SZ, "\tData: ");
				hid_debug_event(hdev, buff);
				dump_buff_as_hex(buff, BUFF_SZ, raw_data+4, raw_data[3]);
			} else {
				snprintf(buff, BUFF_SZ, "\tData overflowed\n");
			}
			break;
		case 3:
			snprintf(buff, BUFF_SZ, "\tData address: 0x%02x%02x%02x\n",
					raw_data[3], raw_data[2], raw_data[1]);
			hid_debug_event(hdev, buff);
			snprintf(buff, BUFF_SZ, "\tData length: %d\n", raw_data[4]);
			hid_debug_event(hdev, buff);
			if (raw_data[4] == 0) {
				snprintf(buff, BUFF_SZ, "\tNo data\n");
			} else if (raw_data[4] + 5 <= raw_size) {
				snprintf(buff, BUFF_SZ, "\tData: ");
				hid_debug_event(hdev, buff);
				dump_buff_as_hex(buff, BUFF_SZ, raw_data+5, raw_data[4]);
			} else {
				snprintf(buff, BUFF_SZ, "\tData overflowed\n");
			}
			break;
		default:
			snprintf(buff, BUFF_SZ, "\tNot supported\n");
		}
		hid_debug_event(hdev, buff);
		break;
	case REPORT_SPLASH_RESTART:
		/* TODO */
		break;
	case REPORT_EXIT_KEYBOARD:
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_EXIT_KEYBOARD", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tRestart delay: %dms (0x%02x%02x)\n",
				raw_data[1] | (raw_data[2] << 8),
				raw_data[2], raw_data[1]);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_VERSION:
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_VERSION", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_DEVID:
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_DEVID", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_SPLASH_SIZE:
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_SPLASH_SIZE", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_HOOK_VERSION:
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_HOOK_VERSION", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_EXIT_FLASHER:
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"REPORT_VERSION", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tRestart delay: %dms (0x%02x%02x)\n",
				raw_data[1] | (raw_data[2] << 8),
				raw_data[2], raw_data[1]);
		hid_debug_event(hdev, buff);
		break;
	default:
		snprintf(buff, BUFF_SZ, "out report %s (%d, size=%d)\n",
			"<unknown>", report->id, raw_size-1);
		hid_debug_event(hdev, buff);
		break;
	}
	wake_up_interruptible(&hdev->debug_wait);
	kfree(buff);
}

static void picolcd_debug_raw_event(struct picolcd_data *data,
		struct hid_device *hdev, struct hid_report *report,
		u8 *raw_data, int size)
{
	char *buff;

#define BUFF_SZ 256
	/* Avoid unnecessary overhead if debugfs is disabled */
	if (!hdev->debug_events)
		return;

	buff = kmalloc(BUFF_SZ, GFP_ATOMIC);
	if (!buff)
		return;

	switch (report->id) {
	case REPORT_ERROR_CODE:
		/* 2 data bytes with affected report and error code */
		snprintf(buff, BUFF_SZ, "report %s (%d, size=%d)\n",
			"REPORT_ERROR_CODE", report->id, size-1);
		hid_debug_event(hdev, buff);
		if (raw_data[2] < ARRAY_SIZE(error_codes))
			snprintf(buff, BUFF_SZ, "\tError code 0x%02x (%s) in reply to report 0x%02x\n",
					raw_data[2], error_codes[raw_data[2]], raw_data[1]);
		else
			snprintf(buff, BUFF_SZ, "\tError code 0x%02x in reply to report 0x%02x\n",
					raw_data[2], raw_data[1]);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_KEY_STATE:
		/* 2 data bytes with key state */
		snprintf(buff, BUFF_SZ, "report %s (%d, size=%d)\n",
			"REPORT_KEY_STATE", report->id, size-1);
		hid_debug_event(hdev, buff);
		if (raw_data[1] == 0)
			snprintf(buff, BUFF_SZ, "\tNo key pressed\n");
		else if (raw_data[2] == 0)
			snprintf(buff, BUFF_SZ, "\tOne key pressed: 0x%02x (%d)\n",
					raw_data[1], raw_data[1]);
		else
			snprintf(buff, BUFF_SZ, "\tTwo keys pressed: 0x%02x (%d), 0x%02x (%d)\n",
					raw_data[1], raw_data[1], raw_data[2], raw_data[2]);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_IR_DATA:
		/* Up to 20 byes of IR scancode data */
		snprintf(buff, BUFF_SZ, "report %s (%d, size=%d)\n",
			"REPORT_IR_DATA", report->id, size-1);
		hid_debug_event(hdev, buff);
		if (raw_data[1] == 0) {
			snprintf(buff, BUFF_SZ, "\tUnexpectedly 0 data length\n");
			hid_debug_event(hdev, buff);
		} else if (raw_data[1] + 1 <= size) {
			snprintf(buff, BUFF_SZ, "\tData length: %d\n\tIR Data: ",
					raw_data[1]-1);
			hid_debug_event(hdev, buff);
			dump_buff_as_hex(buff, BUFF_SZ, raw_data+2, raw_data[1]-1);
			hid_debug_event(hdev, buff);
		} else {
			snprintf(buff, BUFF_SZ, "\tOverflowing data length: %d\n",
					raw_data[1]-1);
			hid_debug_event(hdev, buff);
		}
		break;
	case REPORT_EE_DATA:
		/* Data buffer in response to REPORT_EE_READ or REPORT_EE_WRITE */
		snprintf(buff, BUFF_SZ, "report %s (%d, size=%d)\n",
			"REPORT_EE_DATA", report->id, size-1);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tData address: 0x%02x%02x\n",
				raw_data[2], raw_data[1]);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tData length: %d\n", raw_data[3]);
		hid_debug_event(hdev, buff);
		if (raw_data[3] == 0) {
			snprintf(buff, BUFF_SZ, "\tNo data\n");
			hid_debug_event(hdev, buff);
		} else if (raw_data[3] + 4 <= size) {
			snprintf(buff, BUFF_SZ, "\tData: ");
			hid_debug_event(hdev, buff);
			dump_buff_as_hex(buff, BUFF_SZ, raw_data+4, raw_data[3]);
			hid_debug_event(hdev, buff);
		} else {
			snprintf(buff, BUFF_SZ, "\tData overflowed\n");
			hid_debug_event(hdev, buff);
		}
		break;
	case REPORT_MEMORY:
		/* Data buffer in response to REPORT_READ_MEMORY or REPORT_WRTIE_MEMORY */
		snprintf(buff, BUFF_SZ, "report %s (%d, size=%d)\n",
			"REPORT_MEMORY", report->id, size-1);
		hid_debug_event(hdev, buff);
		switch (data->addr_sz) {
		case 2:
			snprintf(buff, BUFF_SZ, "\tData address: 0x%02x%02x\n",
					raw_data[2], raw_data[1]);
			hid_debug_event(hdev, buff);
			snprintf(buff, BUFF_SZ, "\tData length: %d\n", raw_data[3]);
			hid_debug_event(hdev, buff);
			if (raw_data[3] == 0) {
				snprintf(buff, BUFF_SZ, "\tNo data\n");
			} else if (raw_data[3] + 4 <= size) {
				snprintf(buff, BUFF_SZ, "\tData: ");
				hid_debug_event(hdev, buff);
				dump_buff_as_hex(buff, BUFF_SZ, raw_data+4, raw_data[3]);
			} else {
				snprintf(buff, BUFF_SZ, "\tData overflowed\n");
			}
			break;
		case 3:
			snprintf(buff, BUFF_SZ, "\tData address: 0x%02x%02x%02x\n",
					raw_data[3], raw_data[2], raw_data[1]);
			hid_debug_event(hdev, buff);
			snprintf(buff, BUFF_SZ, "\tData length: %d\n", raw_data[4]);
			hid_debug_event(hdev, buff);
			if (raw_data[4] == 0) {
				snprintf(buff, BUFF_SZ, "\tNo data\n");
			} else if (raw_data[4] + 5 <= size) {
				snprintf(buff, BUFF_SZ, "\tData: ");
				hid_debug_event(hdev, buff);
				dump_buff_as_hex(buff, BUFF_SZ, raw_data+5, raw_data[4]);
			} else {
				snprintf(buff, BUFF_SZ, "\tData overflowed\n");
			}
			break;
		default:
			snprintf(buff, BUFF_SZ, "\tNot supported\n");
		}
		hid_debug_event(hdev, buff);
		break;
	case REPORT_VERSION:
		snprintf(buff, BUFF_SZ, "report %s (%d, size=%d)\n",
			"REPORT_VERSION", report->id, size-1);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tFirmware version: %d.%d\n",
				raw_data[2], raw_data[1]);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_BL_ERASE_MEMORY:
		snprintf(buff, BUFF_SZ, "report %s (%d, size=%d)\n",
			"REPORT_BL_ERASE_MEMORY", report->id, size-1);
		hid_debug_event(hdev, buff);
		/* TODO */
		break;
	case REPORT_BL_READ_MEMORY:
		snprintf(buff, BUFF_SZ, "report %s (%d, size=%d)\n",
			"REPORT_BL_READ_MEMORY", report->id, size-1);
		hid_debug_event(hdev, buff);
		/* TODO */
		break;
	case REPORT_BL_WRITE_MEMORY:
		snprintf(buff, BUFF_SZ, "report %s (%d, size=%d)\n",
			"REPORT_BL_WRITE_MEMORY", report->id, size-1);
		hid_debug_event(hdev, buff);
		/* TODO */
		break;
	case REPORT_DEVID:
		snprintf(buff, BUFF_SZ, "report %s (%d, size=%d)\n",
			"REPORT_DEVID", report->id, size-1);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tSerial: 0x%02x%02x%02x%02x\n",
				raw_data[1], raw_data[2], raw_data[3], raw_data[4]);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tType: 0x%02x\n",
				raw_data[5]);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_SPLASH_SIZE:
		snprintf(buff, BUFF_SZ, "report %s (%d, size=%d)\n",
			"REPORT_SPLASH_SIZE", report->id, size-1);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tTotal splash space: %d\n",
				(raw_data[2] << 8) | raw_data[1]);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tUsed splash space: %d\n",
				(raw_data[4] << 8) | raw_data[3]);
		hid_debug_event(hdev, buff);
		break;
	case REPORT_HOOK_VERSION:
		snprintf(buff, BUFF_SZ, "report %s (%d, size=%d)\n",
			"REPORT_HOOK_VERSION", report->id, size-1);
		hid_debug_event(hdev, buff);
		snprintf(buff, BUFF_SZ, "\tFirmware version: %d.%d\n",
				raw_data[1], raw_data[2]);
		hid_debug_event(hdev, buff);
		break;
	default:
		snprintf(buff, BUFF_SZ, "report %s (%d, size=%d)\n",
			"<unknown>", report->id, size-1);
		hid_debug_event(hdev, buff);
		break;
	}
	wake_up_interruptible(&hdev->debug_wait);
	kfree(buff);
}

static void picolcd_init_devfs(struct picolcd_data *data,
		struct hid_report *eeprom_r, struct hid_report *eeprom_w,
		struct hid_report *flash_r, struct hid_report *flash_w,
		struct hid_report *reset)
{
	struct hid_device *hdev = data->hdev;

	mutex_init(&data->mutex_flash);

	/* reset */
	if (reset)
		data->debug_reset = debugfs_create_file("reset", 0600,
				hdev->debug_dir, data, &picolcd_debug_reset_fops);

	/* eeprom */
	if (eeprom_r || eeprom_w)
		data->debug_eeprom = debugfs_create_file("eeprom",
			(eeprom_w ? S_IWUSR : 0) | (eeprom_r ? S_IRUSR : 0),
			hdev->debug_dir, data, &picolcd_debug_eeprom_fops);

	/* flash */
	if (flash_r && flash_r->maxfield == 1 && flash_r->field[0]->report_size == 8)
		data->addr_sz = flash_r->field[0]->report_count - 1;
	else
		data->addr_sz = -1;
	if (data->addr_sz == 2 || data->addr_sz == 3) {
		data->debug_flash = debugfs_create_file("flash",
			(flash_w ? S_IWUSR : 0) | (flash_r ? S_IRUSR : 0),
			hdev->debug_dir, data, &picolcd_debug_flash_fops);
	} else if (flash_r || flash_w)
		hid_warn(hdev, "Unexpected FLASH access reports, please submit rdesc for review\n");
}

static void picolcd_exit_devfs(struct picolcd_data *data)
{
	struct dentry *dent;

	dent = data->debug_reset;
	data->debug_reset = NULL;
	if (dent)
		debugfs_remove(dent);
	dent = data->debug_eeprom;
	data->debug_eeprom = NULL;
	if (dent)
		debugfs_remove(dent);
	dent = data->debug_flash;
	data->debug_flash = NULL;
	if (dent)
		debugfs_remove(dent);
	mutex_destroy(&data->mutex_flash);
}
#else
static inline void picolcd_debug_raw_event(struct picolcd_data *data,
		struct hid_device *hdev, struct hid_report *report,
		u8 *raw_data, int size)
{
}
static inline void picolcd_init_devfs(struct picolcd_data *data,
		struct hid_report *eeprom_r, struct hid_report *eeprom_w,
		struct hid_report *flash_r, struct hid_report *flash_w,
		struct hid_report *reset)
{
}
static inline void picolcd_exit_devfs(struct picolcd_data *data)
{
}
#endif /* CONFIG_DEBUG_FS */

/*
 * Handle raw report as sent by device
 */
static int picolcd_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *raw_data, int size)
{
	struct picolcd_data *data = hid_get_drvdata(hdev);
	unsigned long flags;
	int ret = 0;

	if (!data)
		return 1;

	if (report->id == REPORT_KEY_STATE) {
		if (data->input_keys)
			ret = picolcd_raw_keypad(data, report, raw_data+1, size-1);
	} else if (report->id == REPORT_IR_DATA) {
		if (data->input_cir)
			ret = picolcd_raw_cir(data, report, raw_data+1, size-1);
	} else {
		spin_lock_irqsave(&data->lock, flags);
		/*
		 * We let the caller of picolcd_send_and_wait() check if the
		 * report we got is one of the expected ones or not.
		 */
		if (data->pending) {
			memcpy(data->pending->raw_data, raw_data+1, size-1);
			data->pending->raw_size  = size-1;
			data->pending->in_report = report;
			complete(&data->pending->ready);
		}
		spin_unlock_irqrestore(&data->lock, flags);
	}

	picolcd_debug_raw_event(data, hdev, report, raw_data, size);
	return 1;
}

#ifdef CONFIG_PM
static int picolcd_suspend(struct hid_device *hdev, pm_message_t message)
{
	if (PMSG_IS_AUTO(message))
		return 0;

	picolcd_suspend_backlight(hid_get_drvdata(hdev));
	dbg_hid(PICOLCD_NAME " device ready for suspend\n");
	return 0;
}

static int picolcd_resume(struct hid_device *hdev)
{
	int ret;
	ret = picolcd_resume_backlight(hid_get_drvdata(hdev));
	if (ret)
		dbg_hid(PICOLCD_NAME " restoring backlight failed: %d\n", ret);
	return 0;
}

static int picolcd_reset_resume(struct hid_device *hdev)
{
	int ret;
	ret = picolcd_reset(hdev);
	if (ret)
		dbg_hid(PICOLCD_NAME " resetting our device failed: %d\n", ret);
	ret = picolcd_fb_reset(hid_get_drvdata(hdev), 0);
	if (ret)
		dbg_hid(PICOLCD_NAME " restoring framebuffer content failed: %d\n", ret);
	ret = picolcd_resume_lcd(hid_get_drvdata(hdev));
	if (ret)
		dbg_hid(PICOLCD_NAME " restoring lcd failed: %d\n", ret);
	ret = picolcd_resume_backlight(hid_get_drvdata(hdev));
	if (ret)
		dbg_hid(PICOLCD_NAME " restoring backlight failed: %d\n", ret);
	picolcd_leds_set(hid_get_drvdata(hdev));
	return 0;
}
#endif

/* initialize keypad input device */
static int picolcd_init_keys(struct picolcd_data *data,
		struct hid_report *report)
{
	struct hid_device *hdev = data->hdev;
	struct input_dev *idev;
	int error, i;

	if (!report)
		return -ENODEV;
	if (report->maxfield != 1 || report->field[0]->report_count != 2 ||
			report->field[0]->report_size != 8) {
		hid_err(hdev, "unsupported KEY_STATE report\n");
		return -EINVAL;
	}

	idev = input_allocate_device();
	if (idev == NULL) {
		hid_err(hdev, "failed to allocate input device\n");
		return -ENOMEM;
	}
	input_set_drvdata(idev, hdev);
	memcpy(data->keycode, def_keymap, sizeof(def_keymap));
	idev->name = hdev->name;
	idev->phys = hdev->phys;
	idev->uniq = hdev->uniq;
	idev->id.bustype = hdev->bus;
	idev->id.vendor  = hdev->vendor;
	idev->id.product = hdev->product;
	idev->id.version = hdev->version;
	idev->dev.parent = hdev->dev.parent;
	idev->keycode     = &data->keycode;
	idev->keycodemax  = PICOLCD_KEYS;
	idev->keycodesize = sizeof(data->keycode[0]);
	input_set_capability(idev, EV_MSC, MSC_SCAN);
	set_bit(EV_REP, idev->evbit);
	for (i = 0; i < PICOLCD_KEYS; i++)
		input_set_capability(idev, EV_KEY, data->keycode[i]);
	error = input_register_device(idev);
	if (error) {
		hid_err(hdev, "error registering the input device\n");
		input_free_device(idev);
		return error;
	}
	data->input_keys = idev;
	return 0;
}

static void picolcd_exit_keys(struct picolcd_data *data)
{
	struct input_dev *idev = data->input_keys;

	data->input_keys = NULL;
	if (idev)
		input_unregister_device(idev);
}

/* initialize CIR input device */
static inline int picolcd_init_cir(struct picolcd_data *data, struct hid_report *report)
{
	/* support not implemented yet */
	return 0;
}

static inline void picolcd_exit_cir(struct picolcd_data *data)
{
}

static int picolcd_probe_lcd(struct hid_device *hdev, struct picolcd_data *data)
{
	int error;

	error = picolcd_check_version(hdev);
	if (error)
		return error;

	if (data->version[0] != 0 && data->version[1] != 3)
		hid_info(hdev, "Device with untested firmware revision, please submit /sys/kernel/debug/hid/%s/rdesc for this device.\n",
			 dev_name(&hdev->dev));

	/* Setup keypad input device */
	error = picolcd_init_keys(data, picolcd_in_report(REPORT_KEY_STATE, hdev));
	if (error)
		goto err;

	/* Setup CIR input device */
	error = picolcd_init_cir(data, picolcd_in_report(REPORT_IR_DATA, hdev));
	if (error)
		goto err;

	/* Set up the framebuffer device */
	error = picolcd_init_framebuffer(data);
	if (error)
		goto err;

	/* Setup lcd class device */
	error = picolcd_init_lcd(data, picolcd_out_report(REPORT_CONTRAST, hdev));
	if (error)
		goto err;

	/* Setup backlight class device */
	error = picolcd_init_backlight(data, picolcd_out_report(REPORT_BRIGHTNESS, hdev));
	if (error)
		goto err;

	/* Setup the LED class devices */
	error = picolcd_init_leds(data, picolcd_out_report(REPORT_LED_STATE, hdev));
	if (error)
		goto err;

	picolcd_init_devfs(data, picolcd_out_report(REPORT_EE_READ, hdev),
			picolcd_out_report(REPORT_EE_WRITE, hdev),
			picolcd_out_report(REPORT_READ_MEMORY, hdev),
			picolcd_out_report(REPORT_WRITE_MEMORY, hdev),
			picolcd_out_report(REPORT_RESET, hdev));
	return 0;
err:
	picolcd_exit_leds(data);
	picolcd_exit_backlight(data);
	picolcd_exit_lcd(data);
	picolcd_exit_framebuffer(data);
	picolcd_exit_cir(data);
	picolcd_exit_keys(data);
	return error;
}

static int picolcd_probe_bootloader(struct hid_device *hdev, struct picolcd_data *data)
{
	int error;

	error = picolcd_check_version(hdev);
	if (error)
		return error;

	if (data->version[0] != 1 && data->version[1] != 0)
		hid_info(hdev, "Device with untested bootloader revision, please submit /sys/kernel/debug/hid/%s/rdesc for this device.\n",
			 dev_name(&hdev->dev));

	picolcd_init_devfs(data, NULL, NULL,
			picolcd_out_report(REPORT_BL_READ_MEMORY, hdev),
			picolcd_out_report(REPORT_BL_WRITE_MEMORY, hdev), NULL);
	return 0;
}

static int picolcd_probe(struct hid_device *hdev,
		     const struct hid_device_id *id)
{
	struct picolcd_data *data;
	int error = -ENOMEM;

	dbg_hid(PICOLCD_NAME " hardware probe...\n");

	/*
	 * Let's allocate the picolcd data structure, set some reasonable
	 * defaults, and associate it with the device
	 */
	data = kzalloc(sizeof(struct picolcd_data), GFP_KERNEL);
	if (data == NULL) {
		hid_err(hdev, "can't allocate space for Minibox PicoLCD device data\n");
		error = -ENOMEM;
		goto err_no_cleanup;
	}

	spin_lock_init(&data->lock);
	mutex_init(&data->mutex);
	data->hdev = hdev;
	data->opmode_delay = 5000;
	if (hdev->product == USB_DEVICE_ID_PICOLCD_BOOTLOADER)
		data->status |= PICOLCD_BOOTLOADER;
	hid_set_drvdata(hdev, data);

	/* Parse the device reports and start it up */
	error = hid_parse(hdev);
	if (error) {
		hid_err(hdev, "device report parse failed\n");
		goto err_cleanup_data;
	}

	/* We don't use hidinput but hid_hw_start() fails if nothing is
	 * claimed. So spoof claimed input. */
	hdev->claimed = HID_CLAIMED_INPUT;
	error = hid_hw_start(hdev, 0);
	hdev->claimed = 0;
	if (error) {
		hid_err(hdev, "hardware start failed\n");
		goto err_cleanup_data;
	}

	error = hid_hw_open(hdev);
	if (error) {
		hid_err(hdev, "failed to open input interrupt pipe for key and IR events\n");
		goto err_cleanup_hid_hw;
	}

	error = device_create_file(&hdev->dev, &dev_attr_operation_mode_delay);
	if (error) {
		hid_err(hdev, "failed to create sysfs attributes\n");
		goto err_cleanup_hid_ll;
	}

	error = device_create_file(&hdev->dev, &dev_attr_operation_mode);
	if (error) {
		hid_err(hdev, "failed to create sysfs attributes\n");
		goto err_cleanup_sysfs1;
	}

	if (data->status & PICOLCD_BOOTLOADER)
		error = picolcd_probe_bootloader(hdev, data);
	else
		error = picolcd_probe_lcd(hdev, data);
	if (error)
		goto err_cleanup_sysfs2;

	dbg_hid(PICOLCD_NAME " activated and initialized\n");
	return 0;

err_cleanup_sysfs2:
	device_remove_file(&hdev->dev, &dev_attr_operation_mode);
err_cleanup_sysfs1:
	device_remove_file(&hdev->dev, &dev_attr_operation_mode_delay);
err_cleanup_hid_ll:
	hid_hw_close(hdev);
err_cleanup_hid_hw:
	hid_hw_stop(hdev);
err_cleanup_data:
	kfree(data);
err_no_cleanup:
	hid_set_drvdata(hdev, NULL);

	return error;
}

static void picolcd_remove(struct hid_device *hdev)
{
	struct picolcd_data *data = hid_get_drvdata(hdev);
	unsigned long flags;

	dbg_hid(PICOLCD_NAME " hardware remove...\n");
	spin_lock_irqsave(&data->lock, flags);
	data->status |= PICOLCD_FAILED;
	spin_unlock_irqrestore(&data->lock, flags);
#ifdef CONFIG_HID_PICOLCD_FB
	/* short-circuit FB as early as possible in order to
	 * avoid long delays if we host console.
	 */
	if (data->fb_info)
		data->fb_info->par = NULL;
#endif

	picolcd_exit_devfs(data);
	device_remove_file(&hdev->dev, &dev_attr_operation_mode);
	device_remove_file(&hdev->dev, &dev_attr_operation_mode_delay);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
	hid_set_drvdata(hdev, NULL);

	/* Shortcut potential pending reply that will never arrive */
	spin_lock_irqsave(&data->lock, flags);
	if (data->pending)
		complete(&data->pending->ready);
	spin_unlock_irqrestore(&data->lock, flags);

	/* Cleanup LED */
	picolcd_exit_leds(data);
	/* Clean up the framebuffer */
	picolcd_exit_backlight(data);
	picolcd_exit_lcd(data);
	picolcd_exit_framebuffer(data);
	/* Cleanup input */
	picolcd_exit_cir(data);
	picolcd_exit_keys(data);

	mutex_destroy(&data->mutex);
	/* Finally, clean up the picolcd data itself */
	kfree(data);
}

static const struct hid_device_id picolcd_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROCHIP, USB_DEVICE_ID_PICOLCD) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_MICROCHIP, USB_DEVICE_ID_PICOLCD_BOOTLOADER) },
	{ }
};
MODULE_DEVICE_TABLE(hid, picolcd_devices);

static struct hid_driver picolcd_driver = {
	.name =          "hid-picolcd",
	.id_table =      picolcd_devices,
	.probe =         picolcd_probe,
	.remove =        picolcd_remove,
	.raw_event =     picolcd_raw_event,
#ifdef CONFIG_PM
	.suspend =       picolcd_suspend,
	.resume =        picolcd_resume,
	.reset_resume =  picolcd_reset_resume,
#endif
};

static int __init picolcd_init(void)
{
	return hid_register_driver(&picolcd_driver);
}

static void __exit picolcd_exit(void)
{
	hid_unregister_driver(&picolcd_driver);
#ifdef CONFIG_HID_PICOLCD_FB
	flush_work_sync(&picolcd_fb_cleanup);
	WARN_ON(fb_pending);
#endif
}

module_init(picolcd_init);
module_exit(picolcd_exit);
MODULE_DESCRIPTION("Minibox graphics PicoLCD Driver");
MODULE_LICENSE("GPL v2");
