/* SPDX-License-Identifier: GPL-2.0-or-later */

#define DEFAULT_LCD_BWIDTH      40
#define DEFAULT_LCD_HWIDTH      64

struct hd44780_common {
	int ifwidth;			/* 4-bit or 8-bit (default) */
	int bwidth;			/* Default set by hd44780_alloc() */
	int hwidth;			/* Default set by hd44780_alloc() */
	unsigned long hd44780_common_flags;
	void (*write_data)(struct hd44780_common *hdc, int data);
	void (*write_cmd)(struct hd44780_common *hdc, int cmd);
	/* write_cmd_raw4 is for 4-bit connected displays only */
	void (*write_cmd_raw4)(struct hd44780_common *hdc, int cmd);
	void *hd44780;
};

int hd44780_common_print(struct charlcd *lcd, int c);
int hd44780_common_gotoxy(struct charlcd *lcd, unsigned int x, unsigned int y);
int hd44780_common_home(struct charlcd *lcd);
int hd44780_common_clear_display(struct charlcd *lcd);
int hd44780_common_init_display(struct charlcd *lcd);
int hd44780_common_shift_cursor(struct charlcd *lcd,
		enum charlcd_shift_dir dir);
int hd44780_common_shift_display(struct charlcd *lcd,
		enum charlcd_shift_dir dir);
int hd44780_common_display(struct charlcd *lcd, enum charlcd_onoff on);
int hd44780_common_cursor(struct charlcd *lcd, enum charlcd_onoff on);
int hd44780_common_blink(struct charlcd *lcd, enum charlcd_onoff on);
int hd44780_common_fontsize(struct charlcd *lcd, enum charlcd_fontsize size);
int hd44780_common_lines(struct charlcd *lcd, enum charlcd_lines lines);
int hd44780_common_redefine_char(struct charlcd *lcd, char *esc);

struct charlcd *hd44780_common_alloc(void);
void hd44780_common_free(struct charlcd *lcd);
