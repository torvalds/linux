// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "charlcd.h"
#include "hd44780_common.h"

/* LCD commands */
#define LCD_CMD_DISPLAY_CLEAR	0x01	/* Clear entire display */

#define LCD_CMD_ENTRY_MODE	0x04	/* Set entry mode */
#define LCD_CMD_CURSOR_INC	0x02	/* Increment cursor */

#define LCD_CMD_DISPLAY_CTRL	0x08	/* Display control */
#define LCD_CMD_DISPLAY_ON	0x04	/* Set display on */
#define LCD_CMD_CURSOR_ON	0x02	/* Set cursor on */
#define LCD_CMD_BLINK_ON	0x01	/* Set blink on */

#define LCD_CMD_SHIFT		0x10	/* Shift cursor/display */
#define LCD_CMD_DISPLAY_SHIFT	0x08	/* Shift display instead of cursor */
#define LCD_CMD_SHIFT_RIGHT	0x04	/* Shift display/cursor to the right */

#define LCD_CMD_FUNCTION_SET	0x20	/* Set function */
#define LCD_CMD_DATA_LEN_8BITS	0x10	/* Set data length to 8 bits */
#define LCD_CMD_TWO_LINES	0x08	/* Set to two display lines */
#define LCD_CMD_FONT_5X10_DOTS	0x04	/* Set char font to 5x10 dots */

#define LCD_CMD_SET_DDRAM_ADDR	0x80	/* Set display data RAM address */

/* sleeps that many milliseconds with a reschedule */
static void long_sleep(int ms)
{
	schedule_timeout_interruptible(msecs_to_jiffies(ms));
}

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

/* clears the display and resets X/Y */
int hd44780_common_clear_display(struct charlcd *lcd)
{
	struct hd44780_common *hdc = lcd->drvdata;

	hdc->write_cmd(hdc, LCD_CMD_DISPLAY_CLEAR);
	/* we must wait a few milliseconds (15) */
	long_sleep(15);
	return 0;
}
EXPORT_SYMBOL_GPL(hd44780_common_clear_display);

int hd44780_common_init_display(struct charlcd *lcd)
{
	struct hd44780_common *hdc = lcd->drvdata;

	void (*write_cmd_raw)(struct hd44780_common *hdc, int cmd);
	u8 init;

	if (hdc->ifwidth != 4 && hdc->ifwidth != 8)
		return -EINVAL;

	hdc->hd44780_common_flags = ((lcd->height > 1) ? LCD_FLAG_N : 0) |
		LCD_FLAG_D | LCD_FLAG_C | LCD_FLAG_B;

	long_sleep(20);		/* wait 20 ms after power-up for the paranoid */

	/*
	 * 8-bit mode, 1 line, small fonts; let's do it 3 times, to make sure
	 * the LCD is in 8-bit mode afterwards
	 */
	init = LCD_CMD_FUNCTION_SET | LCD_CMD_DATA_LEN_8BITS;
	if (hdc->ifwidth == 4) {
		init >>= 4;
		write_cmd_raw = hdc->write_cmd_raw4;
	} else {
		write_cmd_raw = hdc->write_cmd;
	}
	write_cmd_raw(hdc, init);
	long_sleep(10);
	write_cmd_raw(hdc, init);
	long_sleep(10);
	write_cmd_raw(hdc, init);
	long_sleep(10);

	if (hdc->ifwidth == 4) {
		/* Switch to 4-bit mode, 1 line, small fonts */
		hdc->write_cmd_raw4(hdc, LCD_CMD_FUNCTION_SET >> 4);
		long_sleep(10);
	}

	/* set font height and lines number */
	hdc->write_cmd(hdc,
		LCD_CMD_FUNCTION_SET |
		((hdc->ifwidth == 8) ? LCD_CMD_DATA_LEN_8BITS : 0) |
		((hdc->hd44780_common_flags & LCD_FLAG_F) ?
			LCD_CMD_FONT_5X10_DOTS : 0) |
		((hdc->hd44780_common_flags & LCD_FLAG_N) ?
			LCD_CMD_TWO_LINES : 0));
	long_sleep(10);

	/* display off, cursor off, blink off */
	hdc->write_cmd(hdc, LCD_CMD_DISPLAY_CTRL);
	long_sleep(10);

	hdc->write_cmd(hdc,
		LCD_CMD_DISPLAY_CTRL |	/* set display mode */
		((hdc->hd44780_common_flags & LCD_FLAG_D) ?
			LCD_CMD_DISPLAY_ON : 0) |
		((hdc->hd44780_common_flags & LCD_FLAG_C) ?
			LCD_CMD_CURSOR_ON : 0) |
		((hdc->hd44780_common_flags & LCD_FLAG_B) ?
			LCD_CMD_BLINK_ON : 0));

	charlcd_backlight(lcd,
			(hdc->hd44780_common_flags & LCD_FLAG_L) ? 1 : 0);

	long_sleep(10);

	/* entry mode set : increment, cursor shifting */
	hdc->write_cmd(hdc, LCD_CMD_ENTRY_MODE | LCD_CMD_CURSOR_INC);

	hd44780_common_clear_display(lcd);
	return 0;
}
EXPORT_SYMBOL_GPL(hd44780_common_init_display);

int hd44780_common_shift_cursor(struct charlcd *lcd, enum charlcd_shift_dir dir)
{
	struct hd44780_common *hdc = lcd->drvdata;

	if (dir == CHARLCD_SHIFT_LEFT) {
		/* back one char if not at end of line */
		if (lcd->addr.x < hdc->bwidth)
			hdc->write_cmd(hdc, LCD_CMD_SHIFT);
	} else if (dir == CHARLCD_SHIFT_RIGHT) {
		/* allow the cursor to pass the end of the line */
		if (lcd->addr.x < (hdc->bwidth - 1))
			hdc->write_cmd(hdc,
					LCD_CMD_SHIFT | LCD_CMD_SHIFT_RIGHT);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(hd44780_common_shift_cursor);

int hd44780_common_shift_display(struct charlcd *lcd,
		enum charlcd_shift_dir dir)
{
	struct hd44780_common *hdc = lcd->drvdata;

	if (dir == CHARLCD_SHIFT_LEFT)
		hdc->write_cmd(hdc, LCD_CMD_SHIFT | LCD_CMD_DISPLAY_SHIFT);
	else if (dir == CHARLCD_SHIFT_RIGHT)
		hdc->write_cmd(hdc, LCD_CMD_SHIFT | LCD_CMD_DISPLAY_SHIFT |
			LCD_CMD_SHIFT_RIGHT);

	return 0;
}
EXPORT_SYMBOL_GPL(hd44780_common_shift_display);

static void hd44780_common_set_mode(struct hd44780_common *hdc)
{
	hdc->write_cmd(hdc,
		LCD_CMD_DISPLAY_CTRL |
		((hdc->hd44780_common_flags & LCD_FLAG_D) ?
			LCD_CMD_DISPLAY_ON : 0) |
		((hdc->hd44780_common_flags & LCD_FLAG_C) ?
			LCD_CMD_CURSOR_ON : 0) |
		((hdc->hd44780_common_flags & LCD_FLAG_B) ?
			LCD_CMD_BLINK_ON : 0));
}

int hd44780_common_display(struct charlcd *lcd, enum charlcd_onoff on)
{
	struct hd44780_common *hdc = lcd->drvdata;

	if (on == CHARLCD_ON)
		hdc->hd44780_common_flags |= LCD_FLAG_D;
	else
		hdc->hd44780_common_flags &= ~LCD_FLAG_D;

	hd44780_common_set_mode(hdc);
	return 0;
}
EXPORT_SYMBOL_GPL(hd44780_common_display);

int hd44780_common_cursor(struct charlcd *lcd, enum charlcd_onoff on)
{
	struct hd44780_common *hdc = lcd->drvdata;

	if (on == CHARLCD_ON)
		hdc->hd44780_common_flags |= LCD_FLAG_C;
	else
		hdc->hd44780_common_flags &= ~LCD_FLAG_C;

	hd44780_common_set_mode(hdc);
	return 0;
}
EXPORT_SYMBOL_GPL(hd44780_common_cursor);

int hd44780_common_blink(struct charlcd *lcd, enum charlcd_onoff on)
{
	struct hd44780_common *hdc = lcd->drvdata;

	if (on == CHARLCD_ON)
		hdc->hd44780_common_flags |= LCD_FLAG_B;
	else
		hdc->hd44780_common_flags &= ~LCD_FLAG_B;

	hd44780_common_set_mode(hdc);
	return 0;
}
EXPORT_SYMBOL_GPL(hd44780_common_blink);

static void hd44780_common_set_function(struct hd44780_common *hdc)
{
	hdc->write_cmd(hdc,
		LCD_CMD_FUNCTION_SET |
		((hdc->ifwidth == 8) ? LCD_CMD_DATA_LEN_8BITS : 0) |
		((hdc->hd44780_common_flags & LCD_FLAG_F) ?
			LCD_CMD_FONT_5X10_DOTS : 0) |
		((hdc->hd44780_common_flags & LCD_FLAG_N) ?
			LCD_CMD_TWO_LINES : 0));
}

int hd44780_common_fontsize(struct charlcd *lcd, enum charlcd_fontsize size)
{
	struct hd44780_common *hdc = lcd->drvdata;

	if (size == CHARLCD_FONTSIZE_LARGE)
		hdc->hd44780_common_flags |= LCD_FLAG_F;
	else
		hdc->hd44780_common_flags &= ~LCD_FLAG_F;

	hd44780_common_set_function(hdc);
	return 0;
}
EXPORT_SYMBOL_GPL(hd44780_common_fontsize);

int hd44780_common_lines(struct charlcd *lcd, enum charlcd_lines lines)
{
	struct hd44780_common *hdc = lcd->drvdata;

	if (lines == CHARLCD_LINES_2)
		hdc->hd44780_common_flags |= LCD_FLAG_N;
	else
		hdc->hd44780_common_flags &= ~LCD_FLAG_N;

	hd44780_common_set_function(hdc);
	return 0;
}
EXPORT_SYMBOL_GPL(hd44780_common_lines);

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
