/*
 * Character LCD driver for Linux
 *
 * Copyright (C) 2000-2008, Willy Tarreau <w@1wt.eu>
 * Copyright (C) 2016-2017 Glider bvba
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#include <generated/utsrelease.h>

#include <misc/charlcd.h>

#define LCD_MINOR		156

#define DEFAULT_LCD_BWIDTH      40
#define DEFAULT_LCD_HWIDTH      64

/* Keep the backlight on this many seconds for each flash */
#define LCD_BL_TEMPO_PERIOD	4

#define LCD_FLAG_B		0x0004	/* Blink on */
#define LCD_FLAG_C		0x0008	/* Cursor on */
#define LCD_FLAG_D		0x0010	/* Display on */
#define LCD_FLAG_F		0x0020	/* Large font mode */
#define LCD_FLAG_N		0x0040	/* 2-rows mode */
#define LCD_FLAG_L		0x0080	/* Backlight enabled */

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

#define LCD_CMD_SET_CGRAM_ADDR	0x40	/* Set char generator RAM address */

#define LCD_CMD_SET_DDRAM_ADDR	0x80	/* Set display data RAM address */

#define LCD_ESCAPE_LEN		24	/* Max chars for LCD escape command */
#define LCD_ESCAPE_CHAR		27	/* Use char 27 for escape command */

struct charlcd_priv {
	struct charlcd lcd;

	struct delayed_work bl_work;
	struct mutex bl_tempo_lock;	/* Protects access to bl_tempo */
	bool bl_tempo;

	bool must_clear;

	/* contains the LCD config state */
	unsigned long int flags;

	/* Contains the LCD X and Y offset */
	struct {
		unsigned long int x;
		unsigned long int y;
	} addr;

	/* Current escape sequence and it's length or -1 if outside */
	struct {
		char buf[LCD_ESCAPE_LEN + 1];
		int len;
	} esc_seq;

	unsigned long long drvdata[0];
};

#define to_priv(p)	container_of(p, struct charlcd_priv, lcd)

/* Device single-open policy control */
static atomic_t charlcd_available = ATOMIC_INIT(1);

/* sleeps that many milliseconds with a reschedule */
static void long_sleep(int ms)
{
	if (in_interrupt())
		mdelay(ms);
	else
		schedule_timeout_interruptible(msecs_to_jiffies(ms));
}

/* turn the backlight on or off */
static void charlcd_backlight(struct charlcd *lcd, int on)
{
	struct charlcd_priv *priv = to_priv(lcd);

	if (!lcd->ops->backlight)
		return;

	mutex_lock(&priv->bl_tempo_lock);
	if (!priv->bl_tempo)
		lcd->ops->backlight(lcd, on);
	mutex_unlock(&priv->bl_tempo_lock);
}

static void charlcd_bl_off(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct charlcd_priv *priv =
		container_of(dwork, struct charlcd_priv, bl_work);

	mutex_lock(&priv->bl_tempo_lock);
	if (priv->bl_tempo) {
		priv->bl_tempo = false;
		if (!(priv->flags & LCD_FLAG_L))
			priv->lcd.ops->backlight(&priv->lcd, 0);
	}
	mutex_unlock(&priv->bl_tempo_lock);
}

/* turn the backlight on for a little while */
void charlcd_poke(struct charlcd *lcd)
{
	struct charlcd_priv *priv = to_priv(lcd);

	if (!lcd->ops->backlight)
		return;

	cancel_delayed_work_sync(&priv->bl_work);

	mutex_lock(&priv->bl_tempo_lock);
	if (!priv->bl_tempo && !(priv->flags & LCD_FLAG_L))
		lcd->ops->backlight(lcd, 1);
	priv->bl_tempo = true;
	schedule_delayed_work(&priv->bl_work, LCD_BL_TEMPO_PERIOD * HZ);
	mutex_unlock(&priv->bl_tempo_lock);
}
EXPORT_SYMBOL_GPL(charlcd_poke);

static void charlcd_gotoxy(struct charlcd *lcd)
{
	struct charlcd_priv *priv = to_priv(lcd);
	unsigned int addr;

	/*
	 * we force the cursor to stay at the end of the
	 * line if it wants to go farther
	 */
	addr = priv->addr.x < lcd->bwidth ? priv->addr.x & (lcd->hwidth - 1)
					  : lcd->bwidth - 1;
	if (priv->addr.y & 1)
		addr += lcd->hwidth;
	if (priv->addr.y & 2)
		addr += lcd->bwidth;
	lcd->ops->write_cmd(lcd, LCD_CMD_SET_DDRAM_ADDR | addr);
}

static void charlcd_home(struct charlcd *lcd)
{
	struct charlcd_priv *priv = to_priv(lcd);

	priv->addr.x = 0;
	priv->addr.y = 0;
	charlcd_gotoxy(lcd);
}

static void charlcd_print(struct charlcd *lcd, char c)
{
	struct charlcd_priv *priv = to_priv(lcd);

	if (priv->addr.x < lcd->bwidth) {
		if (lcd->char_conv)
			c = lcd->char_conv[(unsigned char)c];
		lcd->ops->write_data(lcd, c);
		priv->addr.x++;
	}
	/* prevents the cursor from wrapping onto the next line */
	if (priv->addr.x == lcd->bwidth)
		charlcd_gotoxy(lcd);
}

static void charlcd_clear_fast(struct charlcd *lcd)
{
	int pos;

	charlcd_home(lcd);

	if (lcd->ops->clear_fast)
		lcd->ops->clear_fast(lcd);
	else
		for (pos = 0; pos < min(2, lcd->height) * lcd->hwidth; pos++)
			lcd->ops->write_data(lcd, ' ');

	charlcd_home(lcd);
}

/* clears the display and resets X/Y */
static void charlcd_clear_display(struct charlcd *lcd)
{
	struct charlcd_priv *priv = to_priv(lcd);

	lcd->ops->write_cmd(lcd, LCD_CMD_DISPLAY_CLEAR);
	priv->addr.x = 0;
	priv->addr.y = 0;
	/* we must wait a few milliseconds (15) */
	long_sleep(15);
}

static int charlcd_init_display(struct charlcd *lcd)
{
	void (*write_cmd_raw)(struct charlcd *lcd, int cmd);
	struct charlcd_priv *priv = to_priv(lcd);
	u8 init;

	if (lcd->ifwidth != 4 && lcd->ifwidth != 8)
		return -EINVAL;

	priv->flags = ((lcd->height > 1) ? LCD_FLAG_N : 0) | LCD_FLAG_D |
		      LCD_FLAG_C | LCD_FLAG_B;

	long_sleep(20);		/* wait 20 ms after power-up for the paranoid */

	/*
	 * 8-bit mode, 1 line, small fonts; let's do it 3 times, to make sure
	 * the LCD is in 8-bit mode afterwards
	 */
	init = LCD_CMD_FUNCTION_SET | LCD_CMD_DATA_LEN_8BITS;
	if (lcd->ifwidth == 4) {
		init >>= 4;
		write_cmd_raw = lcd->ops->write_cmd_raw4;
	} else {
		write_cmd_raw = lcd->ops->write_cmd;
	}
	write_cmd_raw(lcd, init);
	long_sleep(10);
	write_cmd_raw(lcd, init);
	long_sleep(10);
	write_cmd_raw(lcd, init);
	long_sleep(10);

	if (lcd->ifwidth == 4) {
		/* Switch to 4-bit mode, 1 line, small fonts */
		lcd->ops->write_cmd_raw4(lcd, LCD_CMD_FUNCTION_SET >> 4);
		long_sleep(10);
	}

	/* set font height and lines number */
	lcd->ops->write_cmd(lcd,
		LCD_CMD_FUNCTION_SET |
		((lcd->ifwidth == 8) ? LCD_CMD_DATA_LEN_8BITS : 0) |
		((priv->flags & LCD_FLAG_F) ? LCD_CMD_FONT_5X10_DOTS : 0) |
		((priv->flags & LCD_FLAG_N) ? LCD_CMD_TWO_LINES : 0));
	long_sleep(10);

	/* display off, cursor off, blink off */
	lcd->ops->write_cmd(lcd, LCD_CMD_DISPLAY_CTRL);
	long_sleep(10);

	lcd->ops->write_cmd(lcd,
		LCD_CMD_DISPLAY_CTRL |	/* set display mode */
		((priv->flags & LCD_FLAG_D) ? LCD_CMD_DISPLAY_ON : 0) |
		((priv->flags & LCD_FLAG_C) ? LCD_CMD_CURSOR_ON : 0) |
		((priv->flags & LCD_FLAG_B) ? LCD_CMD_BLINK_ON : 0));

	charlcd_backlight(lcd, (priv->flags & LCD_FLAG_L) ? 1 : 0);

	long_sleep(10);

	/* entry mode set : increment, cursor shifting */
	lcd->ops->write_cmd(lcd, LCD_CMD_ENTRY_MODE | LCD_CMD_CURSOR_INC);

	charlcd_clear_display(lcd);
	return 0;
}

/*
 * These are the file operation function for user access to /dev/lcd
 * This function can also be called from inside the kernel, by
 * setting file and ppos to NULL.
 *
 */

static inline int handle_lcd_special_code(struct charlcd *lcd)
{
	struct charlcd_priv *priv = to_priv(lcd);

	/* LCD special codes */

	int processed = 0;

	char *esc = priv->esc_seq.buf + 2;
	int oldflags = priv->flags;

	/* check for display mode flags */
	switch (*esc) {
	case 'D':	/* Display ON */
		priv->flags |= LCD_FLAG_D;
		processed = 1;
		break;
	case 'd':	/* Display OFF */
		priv->flags &= ~LCD_FLAG_D;
		processed = 1;
		break;
	case 'C':	/* Cursor ON */
		priv->flags |= LCD_FLAG_C;
		processed = 1;
		break;
	case 'c':	/* Cursor OFF */
		priv->flags &= ~LCD_FLAG_C;
		processed = 1;
		break;
	case 'B':	/* Blink ON */
		priv->flags |= LCD_FLAG_B;
		processed = 1;
		break;
	case 'b':	/* Blink OFF */
		priv->flags &= ~LCD_FLAG_B;
		processed = 1;
		break;
	case '+':	/* Back light ON */
		priv->flags |= LCD_FLAG_L;
		processed = 1;
		break;
	case '-':	/* Back light OFF */
		priv->flags &= ~LCD_FLAG_L;
		processed = 1;
		break;
	case '*':	/* Flash back light */
		charlcd_poke(lcd);
		processed = 1;
		break;
	case 'f':	/* Small Font */
		priv->flags &= ~LCD_FLAG_F;
		processed = 1;
		break;
	case 'F':	/* Large Font */
		priv->flags |= LCD_FLAG_F;
		processed = 1;
		break;
	case 'n':	/* One Line */
		priv->flags &= ~LCD_FLAG_N;
		processed = 1;
		break;
	case 'N':	/* Two Lines */
		priv->flags |= LCD_FLAG_N;
		break;
	case 'l':	/* Shift Cursor Left */
		if (priv->addr.x > 0) {
			/* back one char if not at end of line */
			if (priv->addr.x < lcd->bwidth)
				lcd->ops->write_cmd(lcd, LCD_CMD_SHIFT);
			priv->addr.x--;
		}
		processed = 1;
		break;
	case 'r':	/* shift cursor right */
		if (priv->addr.x < lcd->width) {
			/* allow the cursor to pass the end of the line */
			if (priv->addr.x < (lcd->bwidth - 1))
				lcd->ops->write_cmd(lcd,
					LCD_CMD_SHIFT | LCD_CMD_SHIFT_RIGHT);
			priv->addr.x++;
		}
		processed = 1;
		break;
	case 'L':	/* shift display left */
		lcd->ops->write_cmd(lcd, LCD_CMD_SHIFT | LCD_CMD_DISPLAY_SHIFT);
		processed = 1;
		break;
	case 'R':	/* shift display right */
		lcd->ops->write_cmd(lcd,
				    LCD_CMD_SHIFT | LCD_CMD_DISPLAY_SHIFT |
				    LCD_CMD_SHIFT_RIGHT);
		processed = 1;
		break;
	case 'k': {	/* kill end of line */
		int x;

		for (x = priv->addr.x; x < lcd->bwidth; x++)
			lcd->ops->write_data(lcd, ' ');

		/* restore cursor position */
		charlcd_gotoxy(lcd);
		processed = 1;
		break;
	}
	case 'I':	/* reinitialize display */
		charlcd_init_display(lcd);
		processed = 1;
		break;
	case 'G': {
		/* Generator : LGcxxxxx...xx; must have <c> between '0'
		 * and '7', representing the numerical ASCII code of the
		 * redefined character, and <xx...xx> a sequence of 16
		 * hex digits representing 8 bytes for each character.
		 * Most LCDs will only use 5 lower bits of the 7 first
		 * bytes.
		 */

		unsigned char cgbytes[8];
		unsigned char cgaddr;
		int cgoffset;
		int shift;
		char value;
		int addr;

		if (!strchr(esc, ';'))
			break;

		esc++;

		cgaddr = *(esc++) - '0';
		if (cgaddr > 7) {
			processed = 1;
			break;
		}

		cgoffset = 0;
		shift = 0;
		value = 0;
		while (*esc && cgoffset < 8) {
			shift ^= 4;
			if (*esc >= '0' && *esc <= '9') {
				value |= (*esc - '0') << shift;
			} else if (*esc >= 'A' && *esc <= 'Z') {
				value |= (*esc - 'A' + 10) << shift;
			} else if (*esc >= 'a' && *esc <= 'z') {
				value |= (*esc - 'a' + 10) << shift;
			} else {
				esc++;
				continue;
			}

			if (shift == 0) {
				cgbytes[cgoffset++] = value;
				value = 0;
			}

			esc++;
		}

		lcd->ops->write_cmd(lcd, LCD_CMD_SET_CGRAM_ADDR | (cgaddr * 8));
		for (addr = 0; addr < cgoffset; addr++)
			lcd->ops->write_data(lcd, cgbytes[addr]);

		/* ensures that we stop writing to CGRAM */
		charlcd_gotoxy(lcd);
		processed = 1;
		break;
	}
	case 'x':	/* gotoxy : LxXXX[yYYY]; */
	case 'y':	/* gotoxy : LyYYY[xXXX]; */
		if (!strchr(esc, ';'))
			break;

		while (*esc) {
			if (*esc == 'x') {
				esc++;
				if (kstrtoul(esc, 10, &priv->addr.x) < 0)
					break;
			} else if (*esc == 'y') {
				esc++;
				if (kstrtoul(esc, 10, &priv->addr.y) < 0)
					break;
			} else {
				break;
			}
		}

		charlcd_gotoxy(lcd);
		processed = 1;
		break;
	}

	/* TODO: This indent party here got ugly, clean it! */
	/* Check whether one flag was changed */
	if (oldflags == priv->flags)
		return processed;

	/* check whether one of B,C,D flags were changed */
	if ((oldflags ^ priv->flags) &
	    (LCD_FLAG_B | LCD_FLAG_C | LCD_FLAG_D))
		/* set display mode */
		lcd->ops->write_cmd(lcd,
			LCD_CMD_DISPLAY_CTRL |
			((priv->flags & LCD_FLAG_D) ? LCD_CMD_DISPLAY_ON : 0) |
			((priv->flags & LCD_FLAG_C) ? LCD_CMD_CURSOR_ON : 0) |
			((priv->flags & LCD_FLAG_B) ? LCD_CMD_BLINK_ON : 0));
	/* check whether one of F,N flags was changed */
	else if ((oldflags ^ priv->flags) & (LCD_FLAG_F | LCD_FLAG_N))
		lcd->ops->write_cmd(lcd,
			LCD_CMD_FUNCTION_SET |
			((lcd->ifwidth == 8) ? LCD_CMD_DATA_LEN_8BITS : 0) |
			((priv->flags & LCD_FLAG_F) ? LCD_CMD_FONT_5X10_DOTS : 0) |
			((priv->flags & LCD_FLAG_N) ? LCD_CMD_TWO_LINES : 0));
	/* check whether L flag was changed */
	else if ((oldflags ^ priv->flags) & LCD_FLAG_L)
		charlcd_backlight(lcd, !!(priv->flags & LCD_FLAG_L));

	return processed;
}

static void charlcd_write_char(struct charlcd *lcd, char c)
{
	struct charlcd_priv *priv = to_priv(lcd);

	/* first, we'll test if we're in escape mode */
	if ((c != '\n') && priv->esc_seq.len >= 0) {
		/* yes, let's add this char to the buffer */
		priv->esc_seq.buf[priv->esc_seq.len++] = c;
		priv->esc_seq.buf[priv->esc_seq.len] = 0;
	} else {
		/* aborts any previous escape sequence */
		priv->esc_seq.len = -1;

		switch (c) {
		case LCD_ESCAPE_CHAR:
			/* start of an escape sequence */
			priv->esc_seq.len = 0;
			priv->esc_seq.buf[priv->esc_seq.len] = 0;
			break;
		case '\b':
			/* go back one char and clear it */
			if (priv->addr.x > 0) {
				/*
				 * check if we're not at the
				 * end of the line
				 */
				if (priv->addr.x < lcd->bwidth)
					/* back one char */
					lcd->ops->write_cmd(lcd, LCD_CMD_SHIFT);
				priv->addr.x--;
			}
			/* replace with a space */
			lcd->ops->write_data(lcd, ' ');
			/* back one char again */
			lcd->ops->write_cmd(lcd, LCD_CMD_SHIFT);
			break;
		case '\014':
			/* quickly clear the display */
			charlcd_clear_fast(lcd);
			break;
		case '\n':
			/*
			 * flush the remainder of the current line and
			 * go to the beginning of the next line
			 */
			for (; priv->addr.x < lcd->bwidth; priv->addr.x++)
				lcd->ops->write_data(lcd, ' ');
			priv->addr.x = 0;
			priv->addr.y = (priv->addr.y + 1) % lcd->height;
			charlcd_gotoxy(lcd);
			break;
		case '\r':
			/* go to the beginning of the same line */
			priv->addr.x = 0;
			charlcd_gotoxy(lcd);
			break;
		case '\t':
			/* print a space instead of the tab */
			charlcd_print(lcd, ' ');
			break;
		default:
			/* simply print this char */
			charlcd_print(lcd, c);
			break;
		}
	}

	/*
	 * now we'll see if we're in an escape mode and if the current
	 * escape sequence can be understood.
	 */
	if (priv->esc_seq.len >= 2) {
		int processed = 0;

		if (!strcmp(priv->esc_seq.buf, "[2J")) {
			/* clear the display */
			charlcd_clear_fast(lcd);
			processed = 1;
		} else if (!strcmp(priv->esc_seq.buf, "[H")) {
			/* cursor to home */
			charlcd_home(lcd);
			processed = 1;
		}
		/* codes starting with ^[[L */
		else if ((priv->esc_seq.len >= 3) &&
			 (priv->esc_seq.buf[0] == '[') &&
			 (priv->esc_seq.buf[1] == 'L')) {
			processed = handle_lcd_special_code(lcd);
		}

		/* LCD special escape codes */
		/*
		 * flush the escape sequence if it's been processed
		 * or if it is getting too long.
		 */
		if (processed || (priv->esc_seq.len >= LCD_ESCAPE_LEN))
			priv->esc_seq.len = -1;
	} /* escape codes */
}

static struct charlcd *the_charlcd;

static ssize_t charlcd_write(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	const char __user *tmp = buf;
	char c;

	for (; count-- > 0; (*ppos)++, tmp++) {
		if (!in_interrupt() && (((count + 1) & 0x1f) == 0))
			/*
			 * let's be a little nice with other processes
			 * that need some CPU
			 */
			schedule();

		if (get_user(c, tmp))
			return -EFAULT;

		charlcd_write_char(the_charlcd, c);
	}

	return tmp - buf;
}

static int charlcd_open(struct inode *inode, struct file *file)
{
	struct charlcd_priv *priv = to_priv(the_charlcd);
	int ret;

	ret = -EBUSY;
	if (!atomic_dec_and_test(&charlcd_available))
		goto fail;	/* open only once at a time */

	ret = -EPERM;
	if (file->f_mode & FMODE_READ)	/* device is write-only */
		goto fail;

	if (priv->must_clear) {
		charlcd_clear_display(&priv->lcd);
		priv->must_clear = false;
	}
	return nonseekable_open(inode, file);

 fail:
	atomic_inc(&charlcd_available);
	return ret;
}

static int charlcd_release(struct inode *inode, struct file *file)
{
	atomic_inc(&charlcd_available);
	return 0;
}

static const struct file_operations charlcd_fops = {
	.write   = charlcd_write,
	.open    = charlcd_open,
	.release = charlcd_release,
	.llseek  = no_llseek,
};

static struct miscdevice charlcd_dev = {
	.minor	= LCD_MINOR,
	.name	= "lcd",
	.fops	= &charlcd_fops,
};

static void charlcd_puts(struct charlcd *lcd, const char *s)
{
	const char *tmp = s;
	int count = strlen(s);

	for (; count-- > 0; tmp++) {
		if (!in_interrupt() && (((count + 1) & 0x1f) == 0))
			/*
			 * let's be a little nice with other processes
			 * that need some CPU
			 */
			schedule();

		charlcd_write_char(lcd, *tmp);
	}
}

/* initialize the LCD driver */
static int charlcd_init(struct charlcd *lcd)
{
	struct charlcd_priv *priv = to_priv(lcd);
	int ret;

	if (lcd->ops->backlight) {
		mutex_init(&priv->bl_tempo_lock);
		INIT_DELAYED_WORK(&priv->bl_work, charlcd_bl_off);
	}

	/*
	 * before this line, we must NOT send anything to the display.
	 * Since charlcd_init_display() needs to write data, we have to
	 * enable mark the LCD initialized just before.
	 */
	ret = charlcd_init_display(lcd);
	if (ret)
		return ret;

	/* display a short message */
#ifdef CONFIG_PANEL_CHANGE_MESSAGE
#ifdef CONFIG_PANEL_BOOT_MESSAGE
	charlcd_puts(lcd, "\x1b[Lc\x1b[Lb\x1b[L*" CONFIG_PANEL_BOOT_MESSAGE);
#endif
#else
	charlcd_puts(lcd, "\x1b[Lc\x1b[Lb\x1b[L*Linux-" UTS_RELEASE "\n");
#endif
	/* clear the display on the next device opening */
	priv->must_clear = true;
	charlcd_home(lcd);
	return 0;
}

struct charlcd *charlcd_alloc(unsigned int drvdata_size)
{
	struct charlcd_priv *priv;
	struct charlcd *lcd;

	priv = kzalloc(sizeof(*priv) + drvdata_size, GFP_KERNEL);
	if (!priv)
		return NULL;

	priv->esc_seq.len = -1;

	lcd = &priv->lcd;
	lcd->ifwidth = 8;
	lcd->bwidth = DEFAULT_LCD_BWIDTH;
	lcd->hwidth = DEFAULT_LCD_HWIDTH;
	lcd->drvdata = priv->drvdata;

	return lcd;
}
EXPORT_SYMBOL_GPL(charlcd_alloc);

static int panel_notify_sys(struct notifier_block *this, unsigned long code,
			    void *unused)
{
	struct charlcd *lcd = the_charlcd;

	switch (code) {
	case SYS_DOWN:
		charlcd_puts(lcd,
			     "\x0cReloading\nSystem...\x1b[Lc\x1b[Lb\x1b[L+");
		break;
	case SYS_HALT:
		charlcd_puts(lcd, "\x0cSystem Halted.\x1b[Lc\x1b[Lb\x1b[L+");
		break;
	case SYS_POWER_OFF:
		charlcd_puts(lcd, "\x0cPower off.\x1b[Lc\x1b[Lb\x1b[L+");
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block panel_notifier = {
	panel_notify_sys,
	NULL,
	0
};

int charlcd_register(struct charlcd *lcd)
{
	int ret;

	ret = charlcd_init(lcd);
	if (ret)
		return ret;

	ret = misc_register(&charlcd_dev);
	if (ret)
		return ret;

	the_charlcd = lcd;
	register_reboot_notifier(&panel_notifier);
	return 0;
}
EXPORT_SYMBOL_GPL(charlcd_register);

int charlcd_unregister(struct charlcd *lcd)
{
	struct charlcd_priv *priv = to_priv(lcd);

	unregister_reboot_notifier(&panel_notifier);
	charlcd_puts(lcd, "\x0cLCD driver unloaded.\x1b[Lc\x1b[Lb\x1b[L-");
	misc_deregister(&charlcd_dev);
	the_charlcd = NULL;
	if (lcd->ops->backlight) {
		cancel_delayed_work_sync(&priv->bl_work);
		priv->lcd.ops->backlight(&priv->lcd, 0);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(charlcd_unregister);

MODULE_LICENSE("GPL");
