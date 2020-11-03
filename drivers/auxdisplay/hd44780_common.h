/* SPDX-License-Identifier: GPL-2.0-or-later */

#define DEFAULT_LCD_BWIDTH      40
#define DEFAULT_LCD_HWIDTH      64

struct hd44780_common {
	int bwidth;			/* Default set by hd44780_alloc() */
	int hwidth;			/* Default set by hd44780_alloc() */
	void *hd44780;
};

struct hd44780_common *hd44780_common_alloc(void);
