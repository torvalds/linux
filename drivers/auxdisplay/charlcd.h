/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Character LCD driver for Linux
 *
 * Copyright (C) 2000-2008, Willy Tarreau <w@1wt.eu>
 * Copyright (C) 2016-2017 Glider bvba
 */

#ifndef _CHARLCD_H
#define _CHARLCD_H

enum charlcd_onoff {
	CHARLCD_OFF = 0,
	CHARLCD_ON,
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
 * @clear_fast: Clear the whole display and set cursor to position 0, 0.
 * Optional.
 * @backlight: Turn backlight on or off. Optional.
 * @print: Print one character to the display at current cursor position.
 * The buffered cursor position is advanced by charlcd. The cursor should not
 * wrap to the next line at the end of a line.
 * @gotoxy: Set cursor to x, y. The x and y values to set the cursor to are
 * previously set in addr.x and addr.y by charlcd.
 * @home: Set cursor to 0, 0. The values in addr.x and addr.y are set to 0, 0 by
 * charlcd prior to calling this function.
 * @clear_display: Again clear the whole display, set the cursor to 0, 0. The
 * values in addr.x and addr.y are set to 0, 0 by charlcd prior to calling this
 * function.
 */
struct charlcd_ops {
	void (*clear_fast)(struct charlcd *lcd);
	void (*backlight)(struct charlcd *lcd, enum charlcd_onoff on);
	int (*print)(struct charlcd *lcd, int c);
	int (*gotoxy)(struct charlcd *lcd);
	int (*home)(struct charlcd *lcd);
	int (*clear_display)(struct charlcd *lcd);
};

void charlcd_backlight(struct charlcd *lcd, enum charlcd_onoff on);
struct charlcd *charlcd_alloc(void);
void charlcd_free(struct charlcd *lcd);

int charlcd_register(struct charlcd *lcd);
int charlcd_unregister(struct charlcd *lcd);

void charlcd_poke(struct charlcd *lcd);

#endif /* CHARLCD_H */
