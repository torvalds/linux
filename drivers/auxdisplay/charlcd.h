/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Character LCD driver for Linux
 *
 * Copyright (C) 2000-2008, Willy Tarreau <w@1wt.eu>
 * Copyright (C) 2016-2017 Glider bvba
 */

struct charlcd {
	const struct charlcd_ops *ops;
	const unsigned char *char_conv;	/* Optional */

	int ifwidth;			/* 4-bit or 8-bit (default) */
	int height;
	int width;
	int bwidth;			/* Default set by charlcd_alloc() */
	int hwidth;			/* Default set by charlcd_alloc() */

	void *drvdata;			/* Set by charlcd_alloc() */
};

struct charlcd_ops {
	/* Required */
	void (*write_cmd)(struct charlcd *lcd, int cmd);
	void (*write_data)(struct charlcd *lcd, int data);

	/* Optional */
	void (*write_cmd_raw4)(struct charlcd *lcd, int cmd);	/* 4-bit only */
	void (*clear_fast)(struct charlcd *lcd);
	void (*backlight)(struct charlcd *lcd, int on);
};

struct charlcd *charlcd_alloc(unsigned int drvdata_size);
void charlcd_free(struct charlcd *lcd);

int charlcd_register(struct charlcd *lcd);
int charlcd_unregister(struct charlcd *lcd);

void charlcd_poke(struct charlcd *lcd);
