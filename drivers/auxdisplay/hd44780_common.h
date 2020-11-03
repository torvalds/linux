/* SPDX-License-Identifier: GPL-2.0-or-later */

#define DEFAULT_LCD_BWIDTH      40
#define DEFAULT_LCD_HWIDTH      64

struct hd44780_common {
	int ifwidth;			/* 4-bit or 8-bit (default) */
	int bwidth;			/* Default set by hd44780_alloc() */
	int hwidth;			/* Default set by hd44780_alloc() */
	void (*write_data)(struct hd44780_common *hdc, int data);
	void (*write_cmd)(struct hd44780_common *hdc, int cmd);
	/* write_cmd_raw4 is for 4-bit connected displays only */
	void (*write_cmd_raw4)(struct hd44780_common *hdc, int cmd);
	void *hd44780;
};

int hd44780_common_print(struct charlcd *lcd, int c);
int hd44780_common_gotoxy(struct charlcd *lcd);
int hd44780_common_home(struct charlcd *lcd);
struct hd44780_common *hd44780_common_alloc(void);
