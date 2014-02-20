/*
 * drivers/i2c/chips/lm8323.c
 *
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * Written by Daniel Stone <daniel.stone@nokia.com>
 *            Timo O. Karjalainen <timo.o.karjalainen@nokia.com>
 *
 * Updated by Felipe Balbi <felipe.balbi@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation (version 2 of the License only).
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/leds.h>
#include <linux/pm.h>
#include <linux/i2c/lm8323.h>
#include <linux/slab.h>

/* Commands to send to the chip. */
#define LM8323_CMD_READ_ID		0x80 /* Read chip ID. */
#define LM8323_CMD_WRITE_CFG		0x81 /* Set configuration item. */
#define LM8323_CMD_READ_INT		0x82 /* Get interrupt status. */
#define LM8323_CMD_RESET		0x83 /* Reset, same as external one */
#define LM8323_CMD_WRITE_PORT_SEL	0x85 /* Set GPIO in/out. */
#define LM8323_CMD_WRITE_PORT_STATE	0x86 /* Set GPIO pullup. */
#define LM8323_CMD_READ_PORT_SEL	0x87 /* Get GPIO in/out. */
#define LM8323_CMD_READ_PORT_STATE	0x88 /* Get GPIO pullup. */
#define LM8323_CMD_READ_FIFO		0x89 /* Read byte from FIFO. */
#define LM8323_CMD_RPT_READ_FIFO	0x8a /* Read FIFO (no increment). */
#define LM8323_CMD_SET_ACTIVE		0x8b /* Set active time. */
#define LM8323_CMD_READ_ERR		0x8c /* Get error status. */
#define LM8323_CMD_READ_ROTATOR		0x8e /* Read rotator status. */
#define LM8323_CMD_SET_DEBOUNCE		0x8f /* Set debouncing time. */
#define LM8323_CMD_SET_KEY_SIZE		0x90 /* Set keypad size. */
#define LM8323_CMD_READ_KEY_SIZE	0x91 /* Get keypad size. */
#define LM8323_CMD_READ_CFG		0x92 /* Get configuration item. */
#define LM8323_CMD_WRITE_CLOCK		0x93 /* Set clock config. */
#define LM8323_CMD_READ_CLOCK		0x94 /* Get clock config. */
#define LM8323_CMD_PWM_WRITE		0x95 /* Write PWM script. */
#define LM8323_CMD_START_PWM		0x96 /* Start PWM engine. */
#define LM8323_CMD_STOP_PWM		0x97 /* Stop PWM engine. */

/* Interrupt status. */
#define INT_KEYPAD			0x01 /* Key event. */
#define INT_ROTATOR			0x02 /* Rotator event. */
#define INT_ERROR			0x08 /* Error: use CMD_READ_ERR. */
#define INT_NOINIT			0x10 /* Lost configuration. */
#define INT_PWM1			0x20 /* PWM1 stopped. */
#define INT_PWM2			0x40 /* PWM2 stopped. */
#define INT_PWM3			0x80 /* PWM3 stopped. */

/* Errors (signalled by INT_ERROR, read with CMD_READ_ERR). */
#define ERR_BADPAR			0x01 /* Bad parameter. */
#define ERR_CMDUNK			0x02 /* Unknown command. */
#define ERR_KEYOVR			0x04 /* Too many keys pressed. */
#define ERR_FIFOOVER			0x40 /* FIFO overflow. */

/* Configuration keys (CMD_{WRITE,READ}_CFG). */
#define CFG_MUX1SEL			0x01 /* Select MUX1_OUT input. */
#define CFG_MUX1EN			0x02 /* Enable MUX1_OUT. */
#define CFG_MUX2SEL			0x04 /* Select MUX2_OUT input. */
#define CFG_MUX2EN			0x08 /* Enable MUX2_OUT. */
#define CFG_PSIZE			0x20 /* Package size (must be 0). */
#define CFG_ROTEN			0x40 /* Enable rotator. */

/* Clock settings (CMD_{WRITE,READ}_CLOCK). */
#define CLK_RCPWM_INTERNAL		0x00
#define CLK_RCPWM_EXTERNAL		0x03
#define CLK_SLOWCLKEN			0x08 /* Enable 32.768kHz clock. */
#define CLK_SLOWCLKOUT			0x40 /* Enable slow pulse output. */

/* The possible addresses corresponding to CONFIG1 and CONFIG2 pin wirings. */
#define LM8323_I2C_ADDR00		(0x84 >> 1)	/* 1000 010x */
#define LM8323_I2C_ADDR01		(0x86 >> 1)	/* 1000 011x */
#define LM8323_I2C_ADDR10		(0x88 >> 1)	/* 1000 100x */
#define LM8323_I2C_ADDR11		(0x8A >> 1)	/* 1000 101x */

/* Key event fifo length */
#define LM8323_FIFO_LEN			15

/* Commands for PWM engine; feed in with PWM_WRITE. */
/* Load ramp counter from duty cycle field (range 0 - 0xff). */
#define PWM_SET(v)			(0x4000 | ((v) & 0xff))
/* Go to start of script. */
#define PWM_GOTOSTART			0x0000
/*
 * Stop engine (generates interrupt).  If reset is 1, clear the program
 * counter, else leave it.
 */
#define PWM_END(reset)			(0xc000 | (!!(reset) << 11))
/*
 * Ramp.  If s is 1, divide clock by 512, else divide clock by 16.
 * Take t clock scales (up to 63) per step, for n steps (up to 126).
 * If u is set, ramp up, else ramp down.
 */
#define PWM_RAMP(s, t, n, u)		((!!(s) << 14) | ((t) & 0x3f) << 8 | \
					 ((n) & 0x7f) | ((u) ? 0 : 0x80))
/*
 * Loop (i.e. jump back to pos) for a given number of iterations (up to 63).
 * If cnt is zero, execute until PWM_END is encountered.
 */
#define PWM_LOOP(cnt, pos)		(0xa000 | (((cnt) & 0x3f) << 7) | \
					 ((pos) & 0x3f))
/*
 * Wait for trigger.  Argument is a mask of channels, shifted by the channel
 * number, e.g. 0xa for channels 3 and 1.  Note that channels are numbered
 * from 1, not 0.
 */
#define PWM_WAIT_TRIG(chans)		(0xe000 | (((chans) & 0x7) << 6))
/* Send trigger.  Argument is same as PWM_WAIT_TRIG. */
#define PWM_SEND_TRIG(chans)		(0xe000 | ((chans) & 0x7))

struct lm8323_pwm {
	int			id;
	int			fade_time;
	int			brightness;
	int			desired_brightness;
	bool			enabled;
	bool			running;
	/* pwm lock */
	struct mutex		lock;
	struct work_struct	work;
	struct led_classdev	cdev;
	struct lm8323_chip	*chip;
};

struct lm8323_chip {
	/* device lock */
	struct mutex		lock;
	struct i2c_client	*client;
	struct input_dev	*idev;
	bool			kp_enabled;
	bool			pm_suspend;
	unsigned		keys_down;
	char			phys[32];
	unsigned short		keymap[LM8323_KEYMAP_SIZE];
	int			size_x;
	int			size_y;
	int			debounce_time;
	int			active_time;
	struct lm8323_pwm	pwm[LM8323_NUM_PWMS];
};

#define client_to_lm8323(c)	container_of(c, struct lm8323_chip, client)
#define dev_to_lm8323(d)	container_of(d, struct lm8323_chip, client->dev)
#define cdev_to_pwm(c)		container_of(c, struct lm8323_pwm, cdev)
#define work_to_pwm(w)		container_of(w, struct lm8323_pwm, work)

#define LM8323_MAX_DATA 8

/*
 * To write, we just access the chip's address in write mode, and dump the
 * command and data out on the bus.  The command byte and data are taken as
 * sequential u8s out of varargs, to a maximum of LM8323_MAX_DATA.
 */
static int lm8323_write(struct lm8323_chip *lm, int len, ...)
{
	int ret, i;
	va_list ap;
	u8 data[LM8323_MAX_DATA];

	va_start(ap, len);

	if (unlikely(len > LM8323_MAX_DATA)) {
		dev_err(&lm->client->dev, "tried to send %d bytes\n", len);
		va_end(ap);
		return 0;
	}

	for (i = 0; i < len; i++)
		data[i] = va_arg(ap, int);

	va_end(ap);

	/*
	 * If the host is asleep while we send the data, we can get a NACK
	 * back while it wakes up, so try again, once.
	 */
	ret = i2c_master_send(lm->client, data, len);
	if (unlikely(ret == -EREMOTEIO))
		ret = i2c_master_send(lm->client, data, len);
	if (unlikely(ret != len))
		dev_err(&lm->client->dev, "sent %d bytes of %d total\n",
			len, ret);

	return ret;
}

/*
 * To read, we first send the command byte to the chip and end the transaction,
 * then access the chip in read mode, at which point it will send the data.
 */
static int lm8323_read(struct lm8323_chip *lm, u8 cmd, u8 *buf, int len)
{
	int ret;

	/*
	 * If the host is asleep while we send the byte, we can get a NACK
	 * back while it wakes up, so try again, once.
	 */
	ret = i2c_master_send(lm->client, &cmd, 1);
	if (unlikely(ret == -EREMOTEIO))
		ret = i2c_master_send(lm->client, &cmd, 1);
	if (unlikely(ret != 1)) {
		dev_err(&lm->client->dev, "sending read cmd 0x%02x failed\n",
			cmd);
		return 0;
	}

	ret = i2c_master_recv(lm->client, buf, len);
	if (unlikely(ret != len))
		dev_err(&lm->client->dev, "wanted %d bytes, got %d\n",
			len, ret);

	return ret;
}

/*
 * Set the chip active time (idle time before it enters halt).
 */
static void lm8323_set_active_time(struct lm8323_chip *lm, int time)
{
	lm8323_write(lm, 2, LM8323_CMD_SET_ACTIVE, time >> 2);
}

/*
 * The signals are AT-style: the low 7 bits are the keycode, and the top
 * bit indicates the state (1 for down, 0 for up).
 */
static inline u8 lm8323_whichkey(u8 event)
{
	return event & 0x7f;
}

static inline int lm8323_ispress(u8 event)
{
	return (event & 0x80) ? 1 : 0;
}

static void process_keys(struct lm8323_chip *lm)
{
	u8 event;
	u8 key_fifo[LM8323_FIFO_LEN + 1];
	int old_keys_down = lm->keys_down;
	int ret;
	int i = 0;

	/*
	 * Read all key events from the FIFO at once. Next READ_FIFO clears the
	 * FIFO even if we didn't read all events previously.
	 */
	ret = lm8323_read(lm, LM8323_CMD_READ_FIFO, key_fifo, LM8323_FIFO_LEN);

	if (ret < 0) {
		dev_err(&lm->client->dev, "Failed reading fifo \n");
		return;
	}
	key_fifo[ret] = 0;

	while ((event = key_fifo[i++])) {
		u8 key = lm8323_whichkey(event);
		int isdown = lm8323_ispress(event);
		unsigned short keycode = lm->keymap[key];

		dev_vdbg(&lm->client->dev, "key 0x%02x %s\n",
			 key, isdown ? "down" : "up");

		if (lm->kp_enabled) {
			input_event(lm->idev, EV_MSC, MSC_SCAN, key);
			input_report_key(lm->idev, keycode, isdown);
			input_sync(lm->idev);
		}

		if (isdown)
			lm->keys_down++;
		else
			lm->keys_down--;
	}

	/*
	 * Errata: We need to ensure that the chip never enters halt mode
	 * during a keypress, so set active time to 0.  When it's released,
	 * we can enter halt again, so set the active time back to normal.
	 */
	if (!old_keys_down && lm->keys_down)
		lm8323_set_active_time(lm, 0);
	if (old_keys_down && !lm->keys_down)
		lm8323_set_active_time(lm, lm->active_time);
}

static void lm8323_process_error(struct lm8323_chip *lm)
{
	u8 error;

	if (lm8323_read(lm, LM8323_CMD_READ_ERR, &error, 1) == 1) {
		if (error & ERR_FIFOOVER)
			dev_vdbg(&lm->client->dev, "fifo overflow!\n");
		if (error & ERR_KEYOVR)
			dev_vdbg(&lm->client->dev,
					"more than two keys pressed\n");
		if (error & ERR_CMDUNK)
			dev_vdbg(&lm->client->dev,
					"unknown command submitted\n");
		if (error & ERR_BADPAR)
			dev_vdbg(&lm->client->dev, "bad command parameter\n");
	}
}

static void lm8323_reset(struct lm8323_chip *lm)
{
	/* The docs say we must pass 0xAA as the data byte. */
	lm8323_write(lm, 2, LM8323_CMD_RESET, 0xAA);
}

static int lm8323_configure(struct lm8323_chip *lm)
{
	int keysize = (lm->size_x << 4) | lm->size_y;
	int clock = (CLK_SLOWCLKEN | CLK_RCPWM_EXTERNAL);
	int debounce = lm->debounce_time >> 2;
	int active = lm->active_time >> 2;

	/*
	 * Active time must be greater than the debounce time: if it's
	 * a close-run thing, give ourselves a 12ms buffer.
	 */
	if (debounce >= active)
		active = debounce + 3;

	lm8323_write(lm, 2, LM8323_CMD_WRITE_CFG, 0);
	lm8323_write(lm, 2, LM8323_CMD_WRITE_CLOCK, clock);
	lm8323_write(lm, 2, LM8323_CMD_SET_KEY_SIZE, keysize);
	lm8323_set_active_time(lm, lm->active_time);
	lm8323_write(lm, 2, LM8323_CMD_SET_DEBOUNCE, debounce);
	lm8323_write(lm, 3, LM8323_CMD_WRITE_PORT_STATE, 0xff, 0xff);
	lm8323_write(lm, 3, LM8323_CMD_WRITE_PORT_SEL, 0, 0);

	/*
	 * Not much we can do about errors at this point, so just hope
	 * for the best.
	 */

	return 0;
}

static void pwm_done(struct lm8323_pwm *pwm)
{
	mutex_lock(&pwm->lock);
	pwm->running = false;
	if (pwm->desired_brightness != pwm->brightness)
		schedule_work(&pwm->work);
	mutex_unlock(&pwm->lock);
}

/*
 * Bottom half: handle the interrupt by posting key events, or dealing with
 * errors appropriately.
 */
static irqreturn_t lm8323_irq(int irq, void *_lm)
{
	struct lm8323_chip *lm = _lm;
	u8 ints;
	int i;

	mutex_lock(&lm->lock);

	while ((lm8323_read(lm, LM8323_CMD_READ_INT, &ints, 1) == 1) && ints) {
		if (likely(ints & INT_KEYPAD))
			process_keys(lm);
		if (ints & INT_ROTATOR) {
			/* We don't currently support the rotator. */
			dev_vdbg(&lm->client->dev, "rotator fired\n");
		}
		if (ints & INT_ERROR) {
			dev_vdbg(&lm->client->dev, "error!\n");
			lm8323_process_error(lm);
		}
		if (ints & INT_NOINIT) {
			dev_err(&lm->client->dev, "chip lost config; "
						  "reinitialising\n");
			lm8323_configure(lm);
		}
		for (i = 0; i < LM8323_NUM_PWMS; i++) {
			if (ints & (INT_PWM1 << i)) {
				dev_vdbg(&lm->client->dev,
					 "pwm%d engine completed\n", i);
				pwm_done(&lm->pwm[i]);
			}
		}
	}

	mutex_unlock(&lm->lock);

	return IRQ_HANDLED;
}

/*
 * Read the chip ID.
 */
static int lm8323_read_id(struct lm8323_chip *lm, u8 *buf)
{
	int bytes;

	bytes = lm8323_read(lm, LM8323_CMD_READ_ID, buf, 2);
	if (unlikely(bytes != 2))
		return -EIO;

	return 0;
}

static void lm8323_write_pwm_one(struct lm8323_pwm *pwm, int pos, u16 cmd)
{
	lm8323_write(pwm->chip, 4, LM8323_CMD_PWM_WRITE, (pos << 2) | pwm->id,
		     (cmd & 0xff00) >> 8, cmd & 0x00ff);
}

/*
 * Write a script into a given PWM engine, concluding with PWM_END.
 * If 'kill' is nonzero, the engine will be shut down at the end
 * of the script, producing a zero output. Otherwise the engine
 * will be kept running at the final PWM level indefinitely.
 */
static void lm8323_write_pwm(struct lm8323_pwm *pwm, int kill,
			     int len, const u16 *cmds)
{
	int i;

	for (i = 0; i < len; i++)
		lm8323_write_pwm_one(pwm, i, cmds[i]);

	lm8323_write_pwm_one(pwm, i++, PWM_END(kill));
	lm8323_write(pwm->chip, 2, LM8323_CMD_START_PWM, pwm->id);
	pwm->running = true;
}

static void lm8323_pwm_work(struct work_struct *work)
{
	struct lm8323_pwm *pwm = work_to_pwm(work);
	int div512, perstep, steps, hz, up, kill;
	u16 pwm_cmds[3];
	int num_cmds = 0;

	mutex_lock(&pwm->lock);

	/*
	 * Do nothing if we're already at the requested level,
	 * or previous setting is not yet complete. In the latter
	 * case we will be called again when the previous PWM script
	 * finishes.
	 */
	if (pwm->running || pwm->desired_brightness == pwm->brightness)
		goto out;

	kill = (pwm->desired_brightness == 0);
	up = (pwm->desired_brightness > pwm->brightness);
	steps = abs(pwm->desired_brightness - pwm->brightness);

	/*
	 * Convert time (in ms) into a divisor (512 or 16 on a refclk of
	 * 32768Hz), and number of ticks per step.
	 */
	if ((pwm->fade_time / steps) > (32768 / 512)) {
		div512 = 1;
		hz = 32768 / 512;
	} else {
		div512 = 0;
		hz = 32768 / 16;
	}

	perstep = (hz * pwm->fade_time) / (steps * 1000);

	if (perstep == 0)
		perstep = 1;
	else if (perstep > 63)
		perstep = 63;

	while (steps) {
		int s;

		s = min(126, steps);
		pwm_cmds[num_cmds++] = PWM_RAMP(div512, perstep, s, up);
		steps -= s;
	}

	lm8323_write_pwm(pwm, kill, num_cmds, pwm_cmds);
	pwm->brightness = pwm->desired_brightness;

 out:
	mutex_unlock(&pwm->lock);
}

static void lm8323_pwm_set_brightness(struct led_classdev *led_cdev,
				      enum led_brightness brightness)
{
	struct lm8323_pwm *pwm = cdev_to_pwm(led_cdev);
	struct lm8323_chip *lm = pwm->chip;

	mutex_lock(&pwm->lock);
	pwm->desired_brightness = brightness;
	mutex_unlock(&pwm->lock);

	if (in_interrupt()) {
		schedule_work(&pwm->work);
	} else {
		/*
		 * Schedule PWM work as usual unless we are going into suspend
		 */
		mutex_lock(&lm->lock);
		if (likely(!lm->pm_suspend))
			schedule_work(&pwm->work);
		else
			lm8323_pwm_work(&pwm->work);
		mutex_unlock(&lm->lock);
	}
}

static ssize_t lm8323_pwm_show_time(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct lm8323_pwm *pwm = cdev_to_pwm(led_cdev);

	return sprintf(buf, "%d\n", pwm->fade_time);
}

static ssize_t lm8323_pwm_store_time(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct lm8323_pwm *pwm = cdev_to_pwm(led_cdev);
	int ret, time;

	ret = kstrtoint(buf, 10, &time);
	/* Numbers only, please. */
	if (ret)
		return ret;

	pwm->fade_time = time;

	return strlen(buf);
}
static DEVICE_ATTR(time, 0644, lm8323_pwm_show_time, lm8323_pwm_store_time);

static int init_pwm(struct lm8323_chip *lm, int id, struct device *dev,
		    const char *name)
{
	struct lm8323_pwm *pwm;

	BUG_ON(id > 3);

	pwm = &lm->pwm[id - 1];

	pwm->id = id;
	pwm->fade_time = 0;
	pwm->brightness = 0;
	pwm->desired_brightness = 0;
	pwm->running = false;
	pwm->enabled = false;
	INIT_WORK(&pwm->work, lm8323_pwm_work);
	mutex_init(&pwm->lock);
	pwm->chip = lm;

	if (name) {
		pwm->cdev.name = name;
		pwm->cdev.brightness_set = lm8323_pwm_set_brightness;
		if (led_classdev_register(dev, &pwm->cdev) < 0) {
			dev_err(dev, "couldn't register PWM %d\n", id);
			return -1;
		}
		if (device_create_file(pwm->cdev.dev,
					&dev_attr_time) < 0) {
			dev_err(dev, "couldn't register time attribute\n");
			led_classdev_unregister(&pwm->cdev);
			return -1;
		}
		pwm->enabled = true;
	}

	return 0;
}

static struct i2c_driver lm8323_i2c_driver;

static ssize_t lm8323_show_disable(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct lm8323_chip *lm = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", !lm->kp_enabled);
}

static ssize_t lm8323_set_disable(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct lm8323_chip *lm = dev_get_drvdata(dev);
	int ret;
	unsigned int i;

	ret = kstrtouint(buf, 10, &i);

	mutex_lock(&lm->lock);
	lm->kp_enabled = !i;
	mutex_unlock(&lm->lock);

	return count;
}
static DEVICE_ATTR(disable_kp, 0644, lm8323_show_disable, lm8323_set_disable);

static int lm8323_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct lm8323_platform_data *pdata = dev_get_platdata(&client->dev);
	struct input_dev *idev;
	struct lm8323_chip *lm;
	int pwm;
	int i, err;
	unsigned long tmo;
	u8 data[2];

	if (!pdata || !pdata->size_x || !pdata->size_y) {
		dev_err(&client->dev, "missing platform_data\n");
		return -EINVAL;
	}

	if (pdata->size_x > 8) {
		dev_err(&client->dev, "invalid x size %d specified\n",
			pdata->size_x);
		return -EINVAL;
	}

	if (pdata->size_y > 12) {
		dev_err(&client->dev, "invalid y size %d specified\n",
			pdata->size_y);
		return -EINVAL;
	}

	lm = kzalloc(sizeof *lm, GFP_KERNEL);
	idev = input_allocate_device();
	if (!lm || !idev) {
		err = -ENOMEM;
		goto fail1;
	}

	lm->client = client;
	lm->idev = idev;
	mutex_init(&lm->lock);

	lm->size_x = pdata->size_x;
	lm->size_y = pdata->size_y;
	dev_vdbg(&client->dev, "Keypad size: %d x %d\n",
		 lm->size_x, lm->size_y);

	lm->debounce_time = pdata->debounce_time;
	lm->active_time = pdata->active_time;

	lm8323_reset(lm);

	/* Nothing's set up to service the IRQ yet, so just spin for max.
	 * 100ms until we can configure. */
	tmo = jiffies + msecs_to_jiffies(100);
	while (lm8323_read(lm, LM8323_CMD_READ_INT, data, 1) == 1) {
		if (data[0] & INT_NOINIT)
			break;

		if (time_after(jiffies, tmo)) {
			dev_err(&client->dev,
				"timeout waiting for initialisation\n");
			break;
		}

		msleep(1);
	}

	lm8323_configure(lm);

	/* If a true probe check the device */
	if (lm8323_read_id(lm, data) != 0) {
		dev_err(&client->dev, "device not found\n");
		err = -ENODEV;
		goto fail1;
	}

	for (pwm = 0; pwm < LM8323_NUM_PWMS; pwm++) {
		err = init_pwm(lm, pwm + 1, &client->dev,
			       pdata->pwm_names[pwm]);
		if (err < 0)
			goto fail2;
	}

	lm->kp_enabled = true;
	err = device_create_file(&client->dev, &dev_attr_disable_kp);
	if (err < 0)
		goto fail2;

	idev->name = pdata->name ? : "LM8323 keypad";
	snprintf(lm->phys, sizeof(lm->phys),
		 "%s/input-kp", dev_name(&client->dev));
	idev->phys = lm->phys;

	idev->evbit[0] = BIT(EV_KEY) | BIT(EV_MSC);
	__set_bit(MSC_SCAN, idev->mscbit);
	for (i = 0; i < LM8323_KEYMAP_SIZE; i++) {
		__set_bit(pdata->keymap[i], idev->keybit);
		lm->keymap[i] = pdata->keymap[i];
	}
	__clear_bit(KEY_RESERVED, idev->keybit);

	if (pdata->repeat)
		__set_bit(EV_REP, idev->evbit);

	err = input_register_device(idev);
	if (err) {
		dev_dbg(&client->dev, "error registering input device\n");
		goto fail3;
	}

	err = request_threaded_irq(client->irq, NULL, lm8323_irq,
			  IRQF_TRIGGER_LOW|IRQF_ONESHOT, "lm8323", lm);
	if (err) {
		dev_err(&client->dev, "could not get IRQ %d\n", client->irq);
		goto fail4;
	}

	i2c_set_clientdata(client, lm);

	device_init_wakeup(&client->dev, 1);
	enable_irq_wake(client->irq);

	return 0;

fail4:
	input_unregister_device(idev);
	idev = NULL;
fail3:
	device_remove_file(&client->dev, &dev_attr_disable_kp);
fail2:
	while (--pwm >= 0)
		if (lm->pwm[pwm].enabled) {
			device_remove_file(lm->pwm[pwm].cdev.dev,
					   &dev_attr_time);
			led_classdev_unregister(&lm->pwm[pwm].cdev);
		}
fail1:
	input_free_device(idev);
	kfree(lm);
	return err;
}

static int lm8323_remove(struct i2c_client *client)
{
	struct lm8323_chip *lm = i2c_get_clientdata(client);
	int i;

	disable_irq_wake(client->irq);
	free_irq(client->irq, lm);

	input_unregister_device(lm->idev);

	device_remove_file(&lm->client->dev, &dev_attr_disable_kp);

	for (i = 0; i < 3; i++)
		if (lm->pwm[i].enabled) {
			device_remove_file(lm->pwm[i].cdev.dev, &dev_attr_time);
			led_classdev_unregister(&lm->pwm[i].cdev);
		}

	kfree(lm);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
/*
 * We don't need to explicitly suspend the chip, as it already switches off
 * when there's no activity.
 */
static int lm8323_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm8323_chip *lm = i2c_get_clientdata(client);
	int i;

	irq_set_irq_wake(client->irq, 0);
	disable_irq(client->irq);

	mutex_lock(&lm->lock);
	lm->pm_suspend = true;
	mutex_unlock(&lm->lock);

	for (i = 0; i < 3; i++)
		if (lm->pwm[i].enabled)
			led_classdev_suspend(&lm->pwm[i].cdev);

	return 0;
}

static int lm8323_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm8323_chip *lm = i2c_get_clientdata(client);
	int i;

	mutex_lock(&lm->lock);
	lm->pm_suspend = false;
	mutex_unlock(&lm->lock);

	for (i = 0; i < 3; i++)
		if (lm->pwm[i].enabled)
			led_classdev_resume(&lm->pwm[i].cdev);

	enable_irq(client->irq);
	irq_set_irq_wake(client->irq, 1);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(lm8323_pm_ops, lm8323_suspend, lm8323_resume);

static const struct i2c_device_id lm8323_id[] = {
	{ "lm8323", 0 },
	{ }
};

static struct i2c_driver lm8323_i2c_driver = {
	.driver = {
		.name	= "lm8323",
		.pm	= &lm8323_pm_ops,
	},
	.probe		= lm8323_probe,
	.remove		= lm8323_remove,
	.id_table	= lm8323_id,
};
MODULE_DEVICE_TABLE(i2c, lm8323_id);

module_i2c_driver(lm8323_i2c_driver);

MODULE_AUTHOR("Timo O. Karjalainen <timo.o.karjalainen@nokia.com>");
MODULE_AUTHOR("Daniel Stone");
MODULE_AUTHOR("Felipe Balbi <felipe.balbi@nokia.com>");
MODULE_DESCRIPTION("LM8323 keypad driver");
MODULE_LICENSE("GPL");

