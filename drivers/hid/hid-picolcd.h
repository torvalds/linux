/***************************************************************************
 *   Copyright (C) 2010-2012 by Bruno Prémont <bonbons@linux-vserver.org>  *
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

/* Description of in-progress IO operation, used for operations
 * that trigger response from device */
struct picolcd_pending {
	struct hid_report *out_report;
	struct hid_report *in_report;
	struct completion ready;
	int raw_size;
	u8 raw_data[64];
};


#define PICOLCD_KEYS 17

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
#ifdef CONFIG_HID_PICOLCD_CIR
	struct rc_dev *rc_dev;
#endif
	unsigned short keycode[PICOLCD_KEYS];

#ifdef CONFIG_HID_PICOLCD_FB
	/* Framebuffer stuff */
	struct fb_info *fb_info;
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
#define PICOLCD_CIR_SHUN 4
};

#ifdef CONFIG_HID_PICOLCD_FB
struct picolcd_fb_data {
	/* Framebuffer stuff */
	spinlock_t lock;
	struct picolcd_data *picolcd;
	u8 update_rate;
	u8 bpp;
	u8 force;
	u8 ready;
	u8 *vbitmap;		/* local copy of what was sent to PicoLCD */
	u8 *bitmap;		/* framebuffer */
};
#endif /* CONFIG_HID_PICOLCD_FB */

/* Find a given report */
#define picolcd_in_report(id, dev) picolcd_report(id, dev, HID_INPUT_REPORT)
#define picolcd_out_report(id, dev) picolcd_report(id, dev, HID_OUTPUT_REPORT)

struct hid_report *picolcd_report(int id, struct hid_device *hdev, int dir);

#ifdef CONFIG_DEBUG_FS
void picolcd_debug_out_report(struct picolcd_data *data,
		struct hid_device *hdev, struct hid_report *report);
#define hid_hw_request(a, b, c) \
	do { \
		picolcd_debug_out_report(hid_get_drvdata(a), a, b); \
		hid_hw_request(a, b, c); \
	} while (0)

void picolcd_debug_raw_event(struct picolcd_data *data,
		struct hid_device *hdev, struct hid_report *report,
		u8 *raw_data, int size);

void picolcd_init_devfs(struct picolcd_data *data,
		struct hid_report *eeprom_r, struct hid_report *eeprom_w,
		struct hid_report *flash_r, struct hid_report *flash_w,
		struct hid_report *reset);

void picolcd_exit_devfs(struct picolcd_data *data);
#else
static inline void picolcd_debug_out_report(struct picolcd_data *data,
		struct hid_device *hdev, struct hid_report *report)
{
}
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


#ifdef CONFIG_HID_PICOLCD_FB
int picolcd_fb_reset(struct picolcd_data *data, int clear);

int picolcd_init_framebuffer(struct picolcd_data *data);

void picolcd_exit_framebuffer(struct picolcd_data *data);

void picolcd_fb_refresh(struct picolcd_data *data);
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
static inline void picolcd_fb_refresh(struct picolcd_data *data)
{
}
#define picolcd_fbinfo(d) NULL
#endif /* CONFIG_HID_PICOLCD_FB */


#ifdef CONFIG_HID_PICOLCD_BACKLIGHT
int picolcd_init_backlight(struct picolcd_data *data,
		struct hid_report *report);

void picolcd_exit_backlight(struct picolcd_data *data);

int picolcd_resume_backlight(struct picolcd_data *data);

void picolcd_suspend_backlight(struct picolcd_data *data);
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
int picolcd_init_lcd(struct picolcd_data *data,
		struct hid_report *report);

void picolcd_exit_lcd(struct picolcd_data *data);

int picolcd_resume_lcd(struct picolcd_data *data);
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
int picolcd_init_leds(struct picolcd_data *data,
		struct hid_report *report);

void picolcd_exit_leds(struct picolcd_data *data);

void picolcd_leds_set(struct picolcd_data *data);
#else
static inline int picolcd_init_leds(struct picolcd_data *data,
		struct hid_report *report)
{
	return 0;
}
static inline void picolcd_exit_leds(struct picolcd_data *data)
{
}
static inline void picolcd_leds_set(struct picolcd_data *data)
{
}
#endif /* CONFIG_HID_PICOLCD_LEDS */


#ifdef CONFIG_HID_PICOLCD_CIR
int picolcd_raw_cir(struct picolcd_data *data,
		struct hid_report *report, u8 *raw_data, int size);

int picolcd_init_cir(struct picolcd_data *data, struct hid_report *report);

void picolcd_exit_cir(struct picolcd_data *data);
#else
static inline int picolcd_raw_cir(struct picolcd_data *data,
		struct hid_report *report, u8 *raw_data, int size)
{
	return 1;
}
static inline int picolcd_init_cir(struct picolcd_data *data, struct hid_report *report)
{
	return 0;
}
static inline void picolcd_exit_cir(struct picolcd_data *data)
{
}
#endif /* CONFIG_HID_PICOLCD_CIR */

int picolcd_reset(struct hid_device *hdev);
struct picolcd_pending *picolcd_send_and_wait(struct hid_device *hdev,
			int report_id, const u8 *raw_data, int size);
