/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Character LCD driver for Linux
 *
 * Copyright (C) 2000-2008, Willy Tarreau <w@1wt.eu>
 * Copyright (C) 2016-2017 Glider bvba
 */

#ifndef _CHARLCD_H
#define _CHARLCD_H

#define LCD_FLAG_B		0x0004	/* Blink on */
#define LCD_FLAG_C		0x0008	/* Cursor on */
#define LCD_FLAG_D		0x0010	/* Display on */
#define LCD_FLAG_F		0x0020	/* Large font mode */
#define LCD_FLAG_N		0x0040	/* 2-rows mode */
#define LCD_FLAG_L		0x0080	/* Backlight enabled */

enum charlcd_onoff {
	CHARLCD_OFF = 0,
	CHARLCD_ON,
};

enum charlcd_shift_dir {
	CHARLCD_SHIFT_LEFT,
	CHARLCD_SHIFT_RIGHT,
};

enum charlcd_fontsize {
	CHARLCD_FONTSIZE_SMALL,
	CHARLCD_FONTSIZE_LARGE,
};

enum charlcd_lines {
	CHARLCD_LINES_1,
	CHARLCD_LINES_2,
};

struct charlcd {
	const struct charlcd_ops *ops;
	const unsigned char *char_conv;	/* Optional */

	int height;
	int width;

	/* Contains the LCD X and Y offset */
	struct {
		unsigned long x;
		unsigned long y;
	} addr;

	void *drvdata;
};

/**
 * struct charlcd_ops - Functions used by charlcd. Drivers have to implement
 * these.
 * @backlight: Turn backlight on or off. Optional.
 * @print: Print one character to the display at current cursor position.
 * The buffered cursor position is advanced by charlcd. The cursor should not
 * wrap to the next line at the end of a line.
 * @gotoxy: Set cursor to x, y. The x and y values to set the cursor to are
 * previously set in addr.x and addr.y by charlcd.
 * @home: Set cursor to 0, 0. The values in addr.x and addr.y are set to 0, 0 by
 * charlcd prior to calling this function.
 * @clear_display: Clear the whole display and set the cursor to 0, 0. The
 * values in addr.x and addr.y are set to 0, 0 by charlcd after to calling this
 * function.
 * @init_display: Initialize the display.
 * @shift_cursor: Shift cursor left or right one position.
 * @shift_display: Shift whole display content left or right.
 * @display: Turn display on or off.
 * @cursor: Turn cursor on or off.
 * @blink: Turn cursor blink on or off.
 * @lines: One or two lines.
 * @redefine_char: Redefine the actual pixel matrix of character.
 */
struct charlcd_ops {
	void (*backlight)(struct charlcd *lcd, enum charlcd_onoff on);
	int (*print)(struct charlcd *lcd, int c);
	int (*gotoxy)(struct charlcd *lcd, unsigned int x, unsigned int y);
	int (*home)(struct charlcd *lcd);
	int (*clear_display)(struct charlcd *lcd);
	int (*init_display)(struct charlcd *lcd);
	int (*shift_cursor)(struct charlcd *lcd, enum charlcd_shift_dir dir);
	int (*shift_display)(struct charlcd *lcd, enum charlcd_shift_dir dir);
	int (*display)(struct charlcd *lcd, enum charlcd_onoff on);
	int (*cursor)(struct charlcd *lcd, enum charlcd_onoff on);
	int (*blink)(struct charlcd *lcd, enum charlcd_onoff on);
	int (*fontsize)(struct charlcd *lcd, enum charlcd_fontsize size);
	int (*lines)(struct charlcd *lcd, enum charlcd_lines lines);
	int (*redefine_char)(struct charlcd *lcd, char *esc);
};

void charlcd_backlight(struct charlcd *lcd, enum charlcd_onoff on);
struct charlcd *charlcd_alloc(void);
void charlcd_free(struct charlcd *lcd);

int charlcd_register(struct charlcd *lcd);
int charlcd_unregister(struct charlcd *lcd);

void charlcd_poke(struct charlcd *lcd);

#endif /* CHARLCD_H */
