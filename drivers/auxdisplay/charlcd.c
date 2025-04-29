// SPDX-License-Identifier: GPL-2.0+
/*
 * Character LCD driver for Linux
 *
 * Copyright (C) 2000-2008, Willy Tarreau <w@1wt.eu>
 * Copyright (C) 2016-2017 Glider bvba
 */

#include <linux/atomic.h>
#include <linux/ctype.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#ifndef CONFIG_PANEL_BOOT_MESSAGE
#include <generated/utsrelease.h>
#endif

#include "charlcd.h"

/* Keep the backlight on this many seconds for each flash */
#define LCD_BL_TEMPO_PERIOD	4

#define LCD_ESCAPE_LEN		24	/* Max chars for LCD escape command */
#define LCD_ESCAPE_CHAR		27	/* Use char 27 for escape command */

struct charlcd_priv {
	struct charlcd lcd;

	struct delayed_work bl_work;
	struct mutex bl_tempo_lock;	/* Protects access to bl_tempo */
	bool bl_tempo;

	bool must_clear;

	/* contains the LCD config state */
	unsigned long flags;

	/* Current escape sequence and it's length or -1 if outside */
	struct {
		char buf[LCD_ESCAPE_LEN + 1];
		int len;
	} esc_seq;

	unsigned long long drvdata[];
};

#define charlcd_to_priv(p)	container_of(p, struct charlcd_priv, lcd)

/* Device single-open policy control */
static atomic_t charlcd_available = ATOMIC_INIT(1);

/* turn the backlight on or off */
void charlcd_backlight(struct charlcd *lcd, enum charlcd_onoff on)
{
	struct charlcd_priv *priv = charlcd_to_priv(lcd);

	if (!lcd->ops->backlight)
		return;

	mutex_lock(&priv->bl_tempo_lock);
	if (!priv->bl_tempo)
		lcd->ops->backlight(lcd, on);
	mutex_unlock(&priv->bl_tempo_lock);
}
EXPORT_SYMBOL_GPL(charlcd_backlight);

static void charlcd_bl_off(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct charlcd_priv *priv =
		container_of(dwork, struct charlcd_priv, bl_work);

	mutex_lock(&priv->bl_tempo_lock);
	if (priv->bl_tempo) {
		priv->bl_tempo = false;
		if (!(priv->flags & LCD_FLAG_L))
			priv->lcd.ops->backlight(&priv->lcd, CHARLCD_OFF);
	}
	mutex_unlock(&priv->bl_tempo_lock);
}

/* turn the backlight on for a little while */
void charlcd_poke(struct charlcd *lcd)
{
	struct charlcd_priv *priv = charlcd_to_priv(lcd);

	if (!lcd->ops->backlight)
		return;

	cancel_delayed_work_sync(&priv->bl_work);

	mutex_lock(&priv->bl_tempo_lock);
	if (!priv->bl_tempo && !(priv->flags & LCD_FLAG_L))
		lcd->ops->backlight(lcd, CHARLCD_ON);
	priv->bl_tempo = true;
	schedule_delayed_work(&priv->bl_work, LCD_BL_TEMPO_PERIOD * HZ);
	mutex_unlock(&priv->bl_tempo_lock);
}
EXPORT_SYMBOL_GPL(charlcd_poke);

static void charlcd_home(struct charlcd *lcd)
{
	lcd->addr.x = 0;
	lcd->addr.y = 0;
	lcd->ops->home(lcd);
}

static void charlcd_print(struct charlcd *lcd, char c)
{
	if (lcd->addr.x >= lcd->width)
		return;

	if (lcd->char_conv)
		c = lcd->char_conv[(unsigned char)c];

	if (!lcd->ops->print(lcd, c))
		lcd->addr.x++;

	/* prevents the cursor from wrapping onto the next line */
	if (lcd->addr.x == lcd->width)
		lcd->ops->gotoxy(lcd, lcd->addr.x - 1, lcd->addr.y);
}

static void charlcd_clear_display(struct charlcd *lcd)
{
	lcd->ops->clear_display(lcd);
	lcd->addr.x = 0;
	lcd->addr.y = 0;
}

/*
 * Parses a movement command of the form "(.*);", where the group can be
 * any number of subcommands of the form "(x|y)[0-9]+".
 *
 * Returns whether the command is valid. The position arguments are
 * only written if the parsing was successful.
 *
 * For instance:
 *   - ";"          returns (<original x>, <original y>).
 *   - "x1;"        returns (1, <original y>).
 *   - "y2x1;"      returns (1, 2).
 *   - "x12y34x56;" returns (56, 34).
 *   - ""           fails.
 *   - "x"          fails.
 *   - "x;"         fails.
 *   - "x1"         fails.
 *   - "xy12;"      fails.
 *   - "x12yy12;"   fails.
 *   - "xx"         fails.
 */
static bool parse_xy(const char *s, unsigned long *x, unsigned long *y)
{
	unsigned long new_x = *x;
	unsigned long new_y = *y;
	char *p;

	for (;;) {
		if (!*s)
			return false;

		if (*s == ';')
			break;

		if (*s == 'x') {
			new_x = simple_strtoul(s + 1, &p, 10);
			if (p == s + 1)
				return false;
			s = p;
		} else if (*s == 'y') {
			new_y = simple_strtoul(s + 1, &p, 10);
			if (p == s + 1)
				return false;
			s = p;
		} else {
			return false;
		}
	}

	*x = new_x;
	*y = new_y;
	return true;
}

/*
 * These are the file operation function for user access to /dev/lcd
 * This function can also be called from inside the kernel, by
 * setting file and ppos to NULL.
 *
 */

static inline int handle_lcd_special_code(struct charlcd *lcd)
{
	struct charlcd_priv *priv = charlcd_to_priv(lcd);

	/* LCD special codes */

	int processed = 0;

	char *esc = priv->esc_seq.buf + 2;
	int oldflags = priv->flags;

	/* check for display mode flags */
	switch (*esc) {
	case 'D':	/* Display ON */
		priv->flags |= LCD_FLAG_D;
		if (priv->flags != oldflags)
			lcd->ops->display(lcd, CHARLCD_ON);

		processed = 1;
		break;
	case 'd':	/* Display OFF */
		priv->flags &= ~LCD_FLAG_D;
		if (priv->flags != oldflags)
			lcd->ops->display(lcd, CHARLCD_OFF);

		processed = 1;
		break;
	case 'C':	/* Cursor ON */
		priv->flags |= LCD_FLAG_C;
		if (priv->flags != oldflags)
			lcd->ops->cursor(lcd, CHARLCD_ON);

		processed = 1;
		break;
	case 'c':	/* Cursor OFF */
		priv->flags &= ~LCD_FLAG_C;
		if (priv->flags != oldflags)
			lcd->ops->cursor(lcd, CHARLCD_OFF);

		processed = 1;
		break;
	case 'B':	/* Blink ON */
		priv->flags |= LCD_FLAG_B;
		if (priv->flags != oldflags)
			lcd->ops->blink(lcd, CHARLCD_ON);

		processed = 1;
		break;
	case 'b':	/* Blink OFF */
		priv->flags &= ~LCD_FLAG_B;
		if (priv->flags != oldflags)
			lcd->ops->blink(lcd, CHARLCD_OFF);

		processed = 1;
		break;
	case '+':	/* Back light ON */
		priv->flags |= LCD_FLAG_L;
		if (priv->flags != oldflags)
			charlcd_backlight(lcd, CHARLCD_ON);

		processed = 1;
		break;
	case '-':	/* Back light OFF */
		priv->flags &= ~LCD_FLAG_L;
		if (priv->flags != oldflags)
			charlcd_backlight(lcd, CHARLCD_OFF);

		processed = 1;
		break;
	case '*':	/* Flash back light */
		charlcd_poke(lcd);
		processed = 1;
		break;
	case 'f':	/* Small Font */
		priv->flags &= ~LCD_FLAG_F;
		if (priv->flags != oldflags)
			lcd->ops->fontsize(lcd, CHARLCD_FONTSIZE_SMALL);

		processed = 1;
		break;
	case 'F':	/* Large Font */
		priv->flags |= LCD_FLAG_F;
		if (priv->flags != oldflags)
			lcd->ops->fontsize(lcd, CHARLCD_FONTSIZE_LARGE);

		processed = 1;
		break;
	case 'n':	/* One Line */
		priv->flags &= ~LCD_FLAG_N;
		if (priv->flags != oldflags)
			lcd->ops->lines(lcd, CHARLCD_LINES_1);

		processed = 1;
		break;
	case 'N':	/* Two Lines */
		priv->flags |= LCD_FLAG_N;
		if (priv->flags != oldflags)
			lcd->ops->lines(lcd, CHARLCD_LINES_2);

		processed = 1;
		break;
	case 'l':	/* Shift Cursor Left */
		if (lcd->addr.x > 0) {
			if (!lcd->ops->shift_cursor(lcd, CHARLCD_SHIFT_LEFT))
				lcd->addr.x--;
		}

		processed = 1;
		break;
	case 'r':	/* shift cursor right */
		if (lcd->addr.x < lcd->width) {
			if (!lcd->ops->shift_cursor(lcd, CHARLCD_SHIFT_RIGHT))
				lcd->addr.x++;
		}

		processed = 1;
		break;
	case 'L':	/* shift display left */
		lcd->ops->shift_display(lcd, CHARLCD_SHIFT_LEFT);
		processed = 1;
		break;
	case 'R':	/* shift display right */
		lcd->ops->shift_display(lcd, CHARLCD_SHIFT_RIGHT);
		processed = 1;
		break;
	case 'k': {	/* kill end of line */
		int x, xs, ys;

		xs = lcd->addr.x;
		ys = lcd->addr.y;
		for (x = lcd->addr.x; x < lcd->width; x++)
			lcd->ops->print(lcd, ' ');

		/* restore cursor position */
		lcd->addr.x = xs;
		lcd->addr.y = ys;
		lcd->ops->gotoxy(lcd, lcd->addr.x, lcd->addr.y);
		processed = 1;
		break;
	}
	case 'I':	/* reinitialize display */
		lcd->ops->init_display(lcd);
		priv->flags = ((lcd->height > 1) ? LCD_FLAG_N : 0) | LCD_FLAG_D |
			LCD_FLAG_C | LCD_FLAG_B;
		processed = 1;
		break;
	case 'G':
		if (lcd->ops->redefine_char)
			processed = lcd->ops->redefine_char(lcd, esc);
		else
			processed = 1;
		break;

	case 'x':	/* gotoxy : LxXXX[yYYY]; */
	case 'y':	/* gotoxy : LyYYY[xXXX]; */
		if (priv->esc_seq.buf[priv->esc_seq.len - 1] != ';')
			break;

		/* If the command is valid, move to the new address */
		if (parse_xy(esc, &lcd->addr.x, &lcd->addr.y))
			lcd->ops->gotoxy(lcd, lcd->addr.x, lcd->addr.y);

		/* Regardless of its validity, mark as processed */
		processed = 1;
		break;
	}

	return processed;
}

static void charlcd_write_char(struct charlcd *lcd, char c)
{
	struct charlcd_priv *priv = charlcd_to_priv(lcd);

	/* first, we'll test if we're in escape mode */
	if ((c != '\n') && priv->esc_seq.len >= 0) {
		/* yes, let's add this char to the buffer */
		priv->esc_seq.buf[priv->esc_seq.len++] = c;
		priv->esc_seq.buf[priv->esc_seq.len] = '\0';
	} else {
		/* aborts any previous escape sequence */
		priv->esc_seq.len = -1;

		switch (c) {
		case LCD_ESCAPE_CHAR:
			/* start of an escape sequence */
			priv->esc_seq.len = 0;
			priv->esc_seq.buf[priv->esc_seq.len] = '\0';
			break;
		case '\b':
			/* go back one char and clear it */
			if (lcd->addr.x > 0) {
				/* back one char */
				if (!lcd->ops->shift_cursor(lcd,
							CHARLCD_SHIFT_LEFT))
					lcd->addr.x--;
			}
			/* replace with a space */
			charlcd_print(lcd, ' ');
			/* back one char again */
			if (!lcd->ops->shift_cursor(lcd, CHARLCD_SHIFT_LEFT))
				lcd->addr.x--;

			break;
		case '\f':
			/* quickly clear the display */
			charlcd_clear_display(lcd);
			break;
		case '\n':
			/*
			 * flush the remainder of the current line and
			 * go to the beginning of the next line
			 */
			for (; lcd->addr.x < lcd->width; lcd->addr.x++)
				lcd->ops->print(lcd, ' ');

			lcd->addr.x = 0;
			lcd->addr.y = (lcd->addr.y + 1) % lcd->height;
			lcd->ops->gotoxy(lcd, lcd->addr.x, lcd->addr.y);
			break;
		case '\r':
			/* go to the beginning of the same line */
			lcd->addr.x = 0;
			lcd->ops->gotoxy(lcd, lcd->addr.x, lcd->addr.y);
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
			charlcd_clear_display(lcd);
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
		if (((count + 1) & 0x1f) == 0) {
			/*
			 * charlcd_write() is invoked as a VFS->write() callback
			 * and as such it is always invoked from preemptible
			 * context and may sleep.
			 */
			cond_resched();
		}

		if (get_user(c, tmp))
			return -EFAULT;

		charlcd_write_char(the_charlcd, c);
	}

	return tmp - buf;
}

static int charlcd_open(struct inode *inode, struct file *file)
{
	struct charlcd_priv *priv = charlcd_to_priv(the_charlcd);
	int ret;

	ret = -EBUSY;
	if (!atomic_dec_and_test(&charlcd_available))
		goto fail;	/* open only once at a time */

	ret = -EPERM;
	if (file->f_mode & FMODE_READ)	/* device is write-only */
		goto fail;

	if (priv->must_clear) {
		priv->lcd.ops->clear_display(&priv->lcd);
		priv->must_clear = false;
		priv->lcd.addr.x = 0;
		priv->lcd.addr.y = 0;
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
		if (((count + 1) & 0x1f) == 0)
			cond_resched();

		charlcd_write_char(lcd, *tmp);
	}
}

#ifdef CONFIG_PANEL_BOOT_MESSAGE
#define LCD_INIT_TEXT CONFIG_PANEL_BOOT_MESSAGE
#else
#define LCD_INIT_TEXT "Linux-" UTS_RELEASE "\n"
#endif

#ifdef CONFIG_CHARLCD_BL_ON
#define LCD_INIT_BL "\x1b[L+"
#elif defined(CONFIG_CHARLCD_BL_FLASH)
#define LCD_INIT_BL "\x1b[L*"
#else
#define LCD_INIT_BL "\x1b[L-"
#endif

/* initialize the LCD driver */
static int charlcd_init(struct charlcd *lcd)
{
	struct charlcd_priv *priv = charlcd_to_priv(lcd);
	int ret;

	priv->flags = ((lcd->height > 1) ? LCD_FLAG_N : 0) | LCD_FLAG_D |
		      LCD_FLAG_C | LCD_FLAG_B;
	if (lcd->ops->backlight) {
		mutex_init(&priv->bl_tempo_lock);
		INIT_DELAYED_WORK(&priv->bl_work, charlcd_bl_off);
	}

	/*
	 * before this line, we must NOT send anything to the display.
	 * Since charlcd_init_display() needs to write data, we have to
	 * enable mark the LCD initialized just before.
	 */
	if (WARN_ON(!lcd->ops->init_display))
		return -EINVAL;

	ret = lcd->ops->init_display(lcd);
	if (ret)
		return ret;

	/* display a short message */
	charlcd_puts(lcd, "\x1b[Lc\x1b[Lb" LCD_INIT_BL LCD_INIT_TEXT);

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
	lcd->drvdata = priv->drvdata;

	return lcd;
}
EXPORT_SYMBOL_GPL(charlcd_alloc);

void charlcd_free(struct charlcd *lcd)
{
	kfree(charlcd_to_priv(lcd));
}
EXPORT_SYMBOL_GPL(charlcd_free);

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
	.notifier_call = panel_notify_sys,
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
	struct charlcd_priv *priv = charlcd_to_priv(lcd);

	unregister_reboot_notifier(&panel_notifier);
	charlcd_puts(lcd, "\x0cLCD driver unloaded.\x1b[Lc\x1b[Lb\x1b[L-");
	misc_deregister(&charlcd_dev);
	the_charlcd = NULL;
	if (lcd->ops->backlight) {
		cancel_delayed_work_sync(&priv->bl_work);
		priv->lcd.ops->backlight(&priv->lcd, CHARLCD_OFF);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(charlcd_unregister);

MODULE_DESCRIPTION("Character LCD core support");
MODULE_LICENSE("GPL");
