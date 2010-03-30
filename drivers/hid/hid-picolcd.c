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

#include <linux/seq_file.h>
#include <linux/debugfs.h>

#include <linux/completion.h>

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
	int addr_sz;
#endif
	u8 version[2];
	/* input stuff */
	u8 pressed_keys[2];
	struct input_dev *input_keys;
	struct input_dev *input_cir;
	unsigned short keycode[PICOLCD_KEYS];

	/* Housekeeping stuff */
	spinlock_t lock;
	struct mutex mutex;
	struct picolcd_pending *pending;
	int status;
#define PICOLCD_BOOTLOADER 1
#define PICOLCD_FAILED 2
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
	dev_warn(&hdev->dev, "No report with id 0x%x found\n", id);
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
		dev_err(&hdev->dev, "no version response from PicoLCD");
		return -ENODEV;
	}

	if (verinfo->raw_size == 2) {
		if (data->status & PICOLCD_BOOTLOADER) {
			dev_info(&hdev->dev, "PicoLCD, bootloader version %d.%d\n",
					verinfo->raw_data[0], verinfo->raw_data[1]);
			data->version[0] = verinfo->raw_data[0];
			data->version[1] = verinfo->raw_data[1];
		} else {
			dev_info(&hdev->dev, "PicoLCD, firmware version %d.%d\n",
					verinfo->raw_data[1], verinfo->raw_data[0]);
			data->version[0] = verinfo->raw_data[1];
			data->version[1] = verinfo->raw_data[0];
		}
	} else {
		dev_err(&hdev->dev, "confused, got unexpected version response from PicoLCD\n");
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

	if (!data || !report || report->maxfield != 1)
		return -ENODEV;

	spin_lock_irqsave(&data->lock, flags);
	if (hdev->product == USB_DEVICE_ID_PICOLCD_BOOTLOADER)
		data->status |= PICOLCD_BOOTLOADER;

	/* perform the reset */
	hid_set_field(report->field[0], 0, 1);
	usbhid_submit_report(hdev, report, USB_DIR_OUT);
	spin_unlock_irqrestore(&data->lock, flags);

	return picolcd_check_version(hdev);
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
	int timeout = 5000;
	unsigned u;
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

	while (cnt > 0 && (*buf == ' ' || *buf == '\t')) {
		buf++;
		cnt--;
	}
	while (cnt > 0 && (buf[cnt-1] == '\n' || buf[cnt-1] == '\r'))
		cnt--;
	if (cnt > 0) {
		if (sscanf(buf, "%u", &u) != 1)
			return -EINVAL;
		if (u > 30000)
			return -EINVAL;
		else
			timeout = u;
	}

	spin_lock_irqsave(&data->lock, flags);
	hid_set_field(report->field[0], 0, timeout & 0xff);
	hid_set_field(report->field[0], 1, (timeout >> 8) & 0xff);
	usbhid_submit_report(data->hdev, report, USB_DIR_OUT);
	spin_unlock_irqrestore(&data->lock, flags);
	return count;
}

static DEVICE_ATTR(operation_mode, 0644, picolcd_operation_mode_show,
		picolcd_operation_mode_store);


#ifdef CONFIG_DEBUG_FS
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
#else
#define picolcd_debug_raw_event(data, hdev, report, raw_data, size)
#endif

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
		dev_err(&hdev->dev, "unsupported KEY_STATE report");
		return -EINVAL;
	}

	idev = input_allocate_device();
	if (idev == NULL) {
		dev_err(&hdev->dev, "failed to allocate input device");
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
		dev_err(&hdev->dev, "error registering the input device");
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
	struct hid_report *report;
	int error;

	error = picolcd_check_version(hdev);
	if (error)
		return error;

	if (data->version[0] != 0 && data->version[1] != 3)
		dev_info(&hdev->dev, "Device with untested firmware revision, "
				"please submit /sys/kernel/debug/hid/%s/rdesc for this device.\n",
				dev_name(&hdev->dev));

	/* Setup keypad input device */
	error = picolcd_init_keys(data, picolcd_in_report(REPORT_KEY_STATE, hdev));
	if (error)
		goto err;

	/* Setup CIR input device */
	error = picolcd_init_cir(data, picolcd_in_report(REPORT_IR_DATA, hdev));
	if (error)
		goto err;

#ifdef CONFIG_DEBUG_FS
	report = picolcd_out_report(REPORT_READ_MEMORY, hdev);
	if (report && report->maxfield == 1 && report->field[0]->report_size == 8)
		data->addr_sz = report->field[0]->report_count - 1;
	else
		data->addr_sz = -1;
#endif
	return 0;
err:
	picolcd_exit_cir(data);
	picolcd_exit_keys(data);
	return error;
}

static int picolcd_probe_bootloader(struct hid_device *hdev, struct picolcd_data *data)
{
	struct hid_report *report;
	int error;

	error = picolcd_check_version(hdev);
	if (error)
		return error;

	if (data->version[0] != 1 && data->version[1] != 0)
		dev_info(&hdev->dev, "Device with untested bootloader revision, "
				"please submit /sys/kernel/debug/hid/%s/rdesc for this device.\n",
				dev_name(&hdev->dev));

#ifdef CONFIG_DEBUG_FS
	report = picolcd_out_report(REPORT_BL_READ_MEMORY, hdev);
	if (report && report->maxfield == 1 && report->field[0]->report_size == 8)
		data->addr_sz = report->field[0]->report_count - 1;
	else
		data->addr_sz = -1;
#endif
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
		dev_err(&hdev->dev, "can't allocate space for Minibox PicoLCD device data\n");
		error = -ENOMEM;
		goto err_no_cleanup;
	}

	spin_lock_init(&data->lock);
	mutex_init(&data->mutex);
	data->hdev = hdev;
	if (hdev->product == USB_DEVICE_ID_PICOLCD_BOOTLOADER)
		data->status |= PICOLCD_BOOTLOADER;
	hid_set_drvdata(hdev, data);

	/* Parse the device reports and start it up */
	error = hid_parse(hdev);
	if (error) {
		dev_err(&hdev->dev, "device report parse failed\n");
		goto err_cleanup_data;
	}

	/* We don't use hidinput but hid_hw_start() fails if nothing is
	 * claimed. So spoof claimed input. */
	hdev->claimed = HID_CLAIMED_INPUT;
	error = hid_hw_start(hdev, 0);
	hdev->claimed = 0;
	if (error) {
		dev_err(&hdev->dev, "hardware start failed\n");
		goto err_cleanup_data;
	}

	error = hdev->ll_driver->open(hdev);
	if (error) {
		dev_err(&hdev->dev, "failed to open input interrupt pipe for key and IR events\n");
		goto err_cleanup_hid_hw;
	}

	error = device_create_file(&hdev->dev, &dev_attr_operation_mode);
	if (error) {
		dev_err(&hdev->dev, "failed to create sysfs attributes\n");
		goto err_cleanup_hid_ll;
	}

	if (data->status & PICOLCD_BOOTLOADER)
		error = picolcd_probe_bootloader(hdev, data);
	else
		error = picolcd_probe_lcd(hdev, data);
	if (error)
		goto err_cleanup_sysfs;

	dbg_hid(PICOLCD_NAME " activated and initialized\n");
	return 0;

err_cleanup_sysfs:
	device_remove_file(&hdev->dev, &dev_attr_operation_mode);
err_cleanup_hid_ll:
	hdev->ll_driver->close(hdev);
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

	device_remove_file(&hdev->dev, &dev_attr_operation_mode);
	hdev->ll_driver->close(hdev);
	hid_hw_stop(hdev);
	hid_set_drvdata(hdev, NULL);

	/* Shortcut potential pending reply that will never arrive */
	spin_lock_irqsave(&data->lock, flags);
	if (data->pending)
		complete(&data->pending->ready);
	spin_unlock_irqrestore(&data->lock, flags);

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
};

static int __init picolcd_init(void)
{
	return hid_register_driver(&picolcd_driver);
}

static void __exit picolcd_exit(void)
{
	hid_unregister_driver(&picolcd_driver);
}

module_init(picolcd_init);
module_exit(picolcd_exit);
MODULE_DESCRIPTION("Minibox graphics PicoLCD Driver");
MODULE_LICENSE("GPL v2");
