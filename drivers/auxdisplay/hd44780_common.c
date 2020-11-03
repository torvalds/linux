// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/module.h>
#include <linux/slab.h>

#include "charlcd.h"
#include "hd44780_common.h"

/* LCD commands */
#define LCD_CMD_SET_DDRAM_ADDR	0x80	/* Set display data RAM address */

int hd44780_common_print(struct charlcd *lcd, int c)
{
	struct hd44780_common *hdc = lcd->drvdata;

	if (lcd->addr.x < hdc->bwidth) {
		hdc->write_data(hdc, c);
		return 0;
	}

	return 1;
}
EXPORT_SYMBOL_GPL(hd44780_common_print);

int hd44780_common_gotoxy(struct charlcd *lcd)
{
	struct hd44780_common *hdc = lcd->drvdata;
	unsigned int addr;

	/*
	 * we force the cursor to stay at the end of the
	 * line if it wants to go farther
	 */
	addr = lcd->addr.x < hdc->bwidth ? lcd->addr.x & (hdc->hwidth - 1)
					  : hdc->bwidth - 1;
	if (lcd->addr.y & 1)
		addr += hdc->hwidth;
	if (lcd->addr.y & 2)
		addr += hdc->bwidth;
	hdc->write_cmd(hdc, LCD_CMD_SET_DDRAM_ADDR | addr);
	return 0;
}
EXPORT_SYMBOL_GPL(hd44780_common_gotoxy);

int hd44780_common_home(struct charlcd *lcd)
{
	lcd->addr.x = 0;
	lcd->addr.y = 0;
	return hd44780_common_gotoxy(lcd);
}
EXPORT_SYMBOL_GPL(hd44780_common_home);

struct hd44780_common *hd44780_common_alloc(void)
{
	struct hd44780_common *hd;

	hd = kzalloc(sizeof(*hd), GFP_KERNEL);
	if (!hd)
		return NULL;

	hd->ifwidth = 8;
	hd->bwidth = DEFAULT_LCD_BWIDTH;
	hd->hwidth = DEFAULT_LCD_HWIDTH;
	return hd;
}
EXPORT_SYMBOL_GPL(hd44780_common_alloc);

MODULE_LICENSE("GPL");
