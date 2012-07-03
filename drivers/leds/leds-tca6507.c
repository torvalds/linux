/*
 * leds-tca6507
 *
 * The TCA6507 is a programmable LED controller that can drive 7
 * separate lines either by holding them low, or by pulsing them
 * with modulated width.
 * The modulation can be varied in a simple pattern to produce a blink or
 * double-blink.
 *
 * This driver can configure each line either as a 'GPIO' which is out-only
 * (no pull-up) or as an LED with variable brightness and hardware-assisted
 * blinking.
 *
 * Apart from OFF and ON there are three programmable brightness levels which
 * can be programmed from 0 to 15 and indicate how many 500usec intervals in
 * each 8msec that the led is 'on'.  The levels are named MASTER, BANK0 and
 * BANK1.
 *
 * There are two different blink rates that can be programmed, each with
 * separate time for rise, on, fall, off and second-off.  Thus if 3 or more
 * different non-trivial rates are required, software must be used for the extra
 * rates. The two different blink rates must align with the two levels BANK0 and
 * BANK1.
 * This driver does not support double-blink so 'second-off' always matches
 * 'off'.
 *
 * Only 16 different times can be programmed in a roughly logarithmic scale from
 * 64ms to 16320ms.  To be precise the possible times are:
 *    0, 64, 128, 192, 256, 384, 512, 768,
 *    1024, 1536, 2048, 3072, 4096, 5760, 8128, 16320
 *
 * Times that cannot be closely matched with these must be
 * handled in software.  This driver allows 12.5% error in matching.
 *
 * This driver does not allow rise/fall rates to be set explicitly.  When trying
 * to match a given 'on' or 'off' period, an appropriate pair of 'change' and
 * 'hold' times are chosen to get a close match.  If the target delay is even,
 * the 'change' number will be the smaller; if odd, the 'hold' number will be
 * the smaller.

 * Choosing pairs of delays with 12.5% errors allows us to match delays in the
 * ranges: 56-72, 112-144, 168-216, 224-27504, 28560-36720.
 * 26% of the achievable sums can be matched by multiple pairings. For example
 * 1536 == 1536+0, 1024+512, or 768+768.  This driver will always choose the
 * pairing with the least maximum - 768+768 in this case.  Other pairings are
 * not available.
 *
 * Access to the 3 levels and 2 blinks are on a first-come, first-served basis.
 * Access can be shared by multiple leds if they have the same level and
 * either same blink rates, or some don't blink.
 * When a led changes, it relinquishes access and tries again, so it might
 * lose access to hardware blink.
 * If a blink engine cannot be allocated, software blink is used.
 * If the desired brightness cannot be allocated, the closest available non-zero
 * brightness is used.  As 'full' is always available, the worst case would be
 * to have two different blink rates at '1', with Max at '2', then other leds
 * will have to choose between '2' and '16'.  Hopefully this is not likely.
 *
 * Each bank (BANK0 and BANK1) has two usage counts - LEDs using the brightness
 * and LEDs using the blink.  It can only be reprogrammed when the appropriate
 * counter is zero.  The MASTER level has a single usage count.
 *
 * Each Led has programmable 'on' and 'off' time as milliseconds.  With each
 * there is a flag saying if it was explicitly requested or defaulted.
 * Similarly the banks know if each time was explicit or a default.  Defaults
 * are permitted to be changed freely - they are not recognised when matching.
 *
 *
 * An led-tca6507 device must be provided with platform data.  This data
 * lists for each output: the name, default trigger, and whether the signal
 * is being used as a GPiO rather than an led.  'struct led_plaform_data'
 * is used for this.  If 'name' is NULL, the output isn't used.  If 'flags'
 * is TCA6507_MAKE_CPIO, the output is a GPO.
 * The "struct led_platform_data" can be embedded in a
 * "struct tca6507_platform_data" which adds a 'gpio_base' for the GPiOs,
 * and a 'setup' callback which is called once the GPiOs are available.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/leds-tca6507.h>

/* LED select registers determine the source that drives LED outputs */
#define TCA6507_LS_LED_OFF	0x0	/* Output HI-Z (off) */
#define TCA6507_LS_LED_OFF1	0x1	/* Output HI-Z (off) - not used */
#define TCA6507_LS_LED_PWM0	0x2	/* Output LOW with Bank0 rate */
#define TCA6507_LS_LED_PWM1	0x3	/* Output LOW with Bank1 rate */
#define TCA6507_LS_LED_ON	0x4	/* Output LOW (on) */
#define TCA6507_LS_LED_MIR	0x5	/* Output LOW with Master Intensity */
#define TCA6507_LS_BLINK0	0x6	/* Blink at Bank0 rate */
#define TCA6507_LS_BLINK1	0x7	/* Blink at Bank1 rate */

enum {
	BANK0,
	BANK1,
	MASTER,
};
static int bank_source[3] = {
	TCA6507_LS_LED_PWM0,
	TCA6507_LS_LED_PWM1,
	TCA6507_LS_LED_MIR,
};
static int blink_source[2] = {
	TCA6507_LS_BLINK0,
	TCA6507_LS_BLINK1,
};

/* PWM registers */
#define	TCA6507_REG_CNT			11

/*
 * 0x00, 0x01, 0x02 encode the TCA6507_LS_* values, each output
 * owns one bit in each register
 */
#define	TCA6507_FADE_ON			0x03
#define	TCA6507_FULL_ON			0x04
#define	TCA6507_FADE_OFF		0x05
#define	TCA6507_FIRST_OFF		0x06
#define	TCA6507_SECOND_OFF		0x07
#define	TCA6507_MAX_INTENSITY		0x08
#define	TCA6507_MASTER_INTENSITY	0x09
#define	TCA6507_INITIALIZE		0x0A

#define	INIT_CODE			0x8

#define TIMECODES 16
static int time_codes[TIMECODES] = {
	0, 64, 128, 192, 256, 384, 512, 768,
	1024, 1536, 2048, 3072, 4096, 5760, 8128, 16320
};

/* Convert an led.brightness level (0..255) to a TCA6507 level (0..15) */
static inline int TO_LEVEL(int brightness)
{
	return brightness >> 4;
}

/* ...and convert back */
static inline int TO_BRIGHT(int level)
{
	if (level)
		return (level << 4) | 0xf;
	return 0;
}

#define NUM_LEDS 7
struct tca6507_chip {
	int			reg_set;	/* One bit per register where
						 * a '1' means the register
						 * should be written */
	u8			reg_file[TCA6507_REG_CNT];
	/* Bank 2 is Master Intensity and doesn't use times */
	struct bank {
		int level;
		int ontime, offtime;
		int on_dflt, off_dflt;
		int time_use, level_use;
	} bank[3];
	struct i2c_client	*client;
	struct work_struct	work;
	spinlock_t		lock;

	struct tca6507_led {
		struct tca6507_chip	*chip;
		struct led_classdev	led_cdev;
		int			num;
		int			ontime, offtime;
		int			on_dflt, off_dflt;
		int			bank;	/* Bank used, or -1 */
		int			blink;	/* Set if hardware-blinking */
	} leds[NUM_LEDS];
#ifdef CONFIG_GPIOLIB
	struct gpio_chip		gpio;
	const char			*gpio_name[NUM_LEDS];
	int				gpio_map[NUM_LEDS];
#endif
};

static const struct i2c_device_id tca6507_id[] = {
	{ "tca6507" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tca6507_id);

static int choose_times(int msec, int *c1p, int *c2p)
{
	/*
	 * Choose two timecodes which add to 'msec' as near as possible.
	 * The first returned is the 'on' or 'off' time.  The second is to be
	 * used as a 'fade-on' or 'fade-off' time.  If 'msec' is even,
	 * the first will not be smaller than the second.  If 'msec' is odd,
	 * the first will not be larger than the second.
	 * If we cannot get a sum within 1/8 of 'msec' fail with -EINVAL,
	 * otherwise return the sum that was achieved, plus 1 if the first is
	 * smaller.
	 * If two possibilities are equally good (e.g. 512+0, 256+256), choose
	 * the first pair so there is more change-time visible (i.e. it is
	 * softer).
	 */
	int c1, c2;
	int tmax = msec * 9 / 8;
	int tmin = msec * 7 / 8;
	int diff = 65536;

	/* We start at '1' to ensure we never even think of choosing a
	 * total time of '0'.
	 */
	for (c1 = 1; c1 < TIMECODES; c1++) {
		int t = time_codes[c1];
		if (t*2 < tmin)
			continue;
		if (t > tmax)
			break;
		for (c2 = 0; c2 <= c1; c2++) {
			int tt = t + time_codes[c2];
			int d;
			if (tt < tmin)
				continue;
			if (tt > tmax)
				break;
			/* This works! */
			d = abs(msec - tt);
			if (d >= diff)
				continue;
			/* Best yet */
			*c1p = c1;
			*c2p = c2;
			diff = d;
			if (d == 0)
				return msec;
		}
	}
	if (diff < 65536) {
		int actual;
		if (msec & 1) {
			c1 = *c2p;
			*c2p = *c1p;
			*c1p = c1;
		}
		actual = time_codes[*c1p] + time_codes[*c2p];
		if (*c1p < *c2p)
			return actual + 1;
		else
			return actual;
	}
	/* No close match */
	return -EINVAL;
}

/*
 * Update the register file with the appropriate 3-bit state for
 * the given led.
 */
static void set_select(struct tca6507_chip *tca, int led, int val)
{
	int mask = (1 << led);
	int bit;

	for (bit = 0; bit < 3; bit++) {
		int n = tca->reg_file[bit] & ~mask;
		if (val & (1 << bit))
			n |= mask;
		if (tca->reg_file[bit] != n) {
			tca->reg_file[bit] = n;
			tca->reg_set |= (1 << bit);
		}
	}
}

/* Update the register file with the appropriate 4-bit code for
 * one bank or other.  This can be used for timers, for levels, or
 * for initialisation.
 */
static void set_code(struct tca6507_chip *tca, int reg, int bank, int new)
{
	int mask = 0xF;
	int n;
	if (bank) {
		mask <<= 4;
		new <<= 4;
	}
	n = tca->reg_file[reg] & ~mask;
	n |= new;
	if (tca->reg_file[reg] != n) {
		tca->reg_file[reg] = n;
		tca->reg_set |= 1 << reg;
	}
}

/* Update brightness level. */
static void set_level(struct tca6507_chip *tca, int bank, int level)
{
	switch (bank) {
	case BANK0:
	case BANK1:
		set_code(tca, TCA6507_MAX_INTENSITY, bank, level);
		break;
	case MASTER:
		set_code(tca, TCA6507_MASTER_INTENSITY, 0, level);
		break;
	}
	tca->bank[bank].level = level;
}

/* Record all relevant time code for a given bank */
static void set_times(struct tca6507_chip *tca, int bank)
{
	int c1, c2;
	int result;

	result = choose_times(tca->bank[bank].ontime, &c1, &c2);
	dev_dbg(&tca->client->dev,
		"Chose on  times %d(%d) %d(%d) for %dms\n", c1, time_codes[c1],
		c2, time_codes[c2], tca->bank[bank].ontime);
	set_code(tca, TCA6507_FADE_ON, bank, c2);
	set_code(tca, TCA6507_FULL_ON, bank, c1);
	tca->bank[bank].ontime = result;

	result = choose_times(tca->bank[bank].offtime, &c1, &c2);
	dev_dbg(&tca->client->dev,
		"Chose off times %d(%d) %d(%d) for %dms\n", c1, time_codes[c1],
		c2, time_codes[c2], tca->bank[bank].offtime);
	set_code(tca, TCA6507_FADE_OFF, bank, c2);
	set_code(tca, TCA6507_FIRST_OFF, bank, c1);
	set_code(tca, TCA6507_SECOND_OFF, bank, c1);
	tca->bank[bank].offtime = result;

	set_code(tca, TCA6507_INITIALIZE, bank, INIT_CODE);
}

/* Write all needed register of tca6507 */

static void tca6507_work(struct work_struct *work)
{
	struct tca6507_chip *tca = container_of(work, struct tca6507_chip,
						work);
	struct i2c_client *cl = tca->client;
	int set;
	u8 file[TCA6507_REG_CNT];
	int r;

	spin_lock_irq(&tca->lock);
	set = tca->reg_set;
	memcpy(file, tca->reg_file, TCA6507_REG_CNT);
	tca->reg_set = 0;
	spin_unlock_irq(&tca->lock);

	for (r = 0; r < TCA6507_REG_CNT; r++)
		if (set & (1<<r))
			i2c_smbus_write_byte_data(cl, r, file[r]);
}

static void led_release(struct tca6507_led *led)
{
	/* If led owns any resource, release it. */
	struct tca6507_chip *tca = led->chip;
	if (led->bank >= 0) {
		struct bank *b = tca->bank + led->bank;
		if (led->blink)
			b->time_use--;
		b->level_use--;
	}
	led->blink = 0;
	led->bank = -1;
}

static int led_prepare(struct tca6507_led *led)
{
	/* Assign this led to a bank, configuring that bank if necessary. */
	int level = TO_LEVEL(led->led_cdev.brightness);
	struct tca6507_chip *tca = led->chip;
	int c1, c2;
	int i;
	struct bank *b;
	int need_init = 0;

	led->led_cdev.brightness = TO_BRIGHT(level);
	if (level == 0) {
		set_select(tca, led->num, TCA6507_LS_LED_OFF);
		return 0;
	}

	if (led->ontime == 0 || led->offtime == 0) {
		/*
		 * Just set the brightness, choosing first usable bank.
		 * If none perfect, choose best.
		 * Count backwards so we check MASTER bank first
		 * to avoid wasting a timer.
		 */
		int best = -1;/* full-on */
		int diff = 15-level;

		if (level == 15) {
			set_select(tca, led->num, TCA6507_LS_LED_ON);
			return 0;
		}

		for (i = MASTER; i >= BANK0; i--) {
			int d;
			if (tca->bank[i].level == level ||
			    tca->bank[i].level_use == 0) {
				best = i;
				break;
			}
			d = abs(level - tca->bank[i].level);
			if (d < diff) {
				diff = d;
				best = i;
			}
		}
		if (best == -1) {
			/* Best brightness is full-on */
			set_select(tca, led->num, TCA6507_LS_LED_ON);
			led->led_cdev.brightness = LED_FULL;
			return 0;
		}

		if (!tca->bank[best].level_use)
			set_level(tca, best, level);

		tca->bank[best].level_use++;
		led->bank = best;
		set_select(tca, led->num, bank_source[best]);
		led->led_cdev.brightness = TO_BRIGHT(tca->bank[best].level);
		return 0;
	}

	/*
	 * We have on/off time so we need to try to allocate a timing bank.
	 * First check if times are compatible with hardware and give up if
	 * not.
	 */
	if (choose_times(led->ontime, &c1, &c2) < 0)
		return -EINVAL;
	if (choose_times(led->offtime, &c1, &c2) < 0)
		return -EINVAL;

	for (i = BANK0; i <= BANK1; i++) {
		if (tca->bank[i].level_use == 0)
			/* not in use - it is ours! */
			break;
		if (tca->bank[i].level != level)
			/* Incompatible level - skip */
			/* FIX: if timer matches we maybe should consider
			 * this anyway...
			 */
			continue;

		if (tca->bank[i].time_use == 0)
			/* Timer not in use, and level matches - use it */
			break;

		if (!(tca->bank[i].on_dflt ||
		      led->on_dflt ||
		      tca->bank[i].ontime == led->ontime))
			/* on time is incompatible */
			continue;

		if (!(tca->bank[i].off_dflt ||
		      led->off_dflt ||
		      tca->bank[i].offtime == led->offtime))
			/* off time is incompatible */
			continue;

		/* looks like a suitable match */
		break;
	}

	if (i > BANK1)
		/* Nothing matches - how sad */
		return -EINVAL;

	b = &tca->bank[i];
	if (b->level_use == 0)
		set_level(tca, i, level);
	b->level_use++;
	led->bank = i;

	if (b->on_dflt ||
	    !led->on_dflt ||
	    b->time_use == 0) {
		b->ontime = led->ontime;
		b->on_dflt = led->on_dflt;
		need_init = 1;
	}

	if (b->off_dflt ||
	    !led->off_dflt ||
	    b->time_use == 0) {
		b->offtime = led->offtime;
		b->off_dflt = led->off_dflt;
		need_init = 1;
	}

	if (need_init)
		set_times(tca, i);

	led->ontime = b->ontime;
	led->offtime = b->offtime;

	b->time_use++;
	led->blink = 1;
	led->led_cdev.brightness = TO_BRIGHT(b->level);
	set_select(tca, led->num, blink_source[i]);
	return 0;
}

static int led_assign(struct tca6507_led *led)
{
	struct tca6507_chip *tca = led->chip;
	int err;
	unsigned long flags;

	spin_lock_irqsave(&tca->lock, flags);
	led_release(led);
	err = led_prepare(led);
	if (err) {
		/*
		 * Can only fail on timer setup.  In that case we need to
		 * re-establish as steady level.
		 */
		led->ontime = 0;
		led->offtime = 0;
		led_prepare(led);
	}
	spin_unlock_irqrestore(&tca->lock, flags);

	if (tca->reg_set)
		schedule_work(&tca->work);
	return err;
}

static void tca6507_brightness_set(struct led_classdev *led_cdev,
				   enum led_brightness brightness)
{
	struct tca6507_led *led = container_of(led_cdev, struct tca6507_led,
					       led_cdev);
	led->led_cdev.brightness = brightness;
	led->ontime = 0;
	led->offtime = 0;
	led_assign(led);
}

static int tca6507_blink_set(struct led_classdev *led_cdev,
			     unsigned long *delay_on,
			     unsigned long *delay_off)
{
	struct tca6507_led *led = container_of(led_cdev, struct tca6507_led,
					       led_cdev);

	if (*delay_on == 0)
		led->on_dflt = 1;
	else if (delay_on != &led_cdev->blink_delay_on)
		led->on_dflt = 0;
	led->ontime = *delay_on;

	if (*delay_off == 0)
		led->off_dflt = 1;
	else if (delay_off != &led_cdev->blink_delay_off)
		led->off_dflt = 0;
	led->offtime = *delay_off;

	if (led->ontime == 0)
		led->ontime = 512;
	if (led->offtime == 0)
		led->offtime = 512;

	if (led->led_cdev.brightness == LED_OFF)
		led->led_cdev.brightness = LED_FULL;
	if (led_assign(led) < 0) {
		led->ontime = 0;
		led->offtime = 0;
		led->led_cdev.brightness = LED_OFF;
		return -EINVAL;
	}
	*delay_on = led->ontime;
	*delay_off = led->offtime;
	return 0;
}

#ifdef CONFIG_GPIOLIB
static void tca6507_gpio_set_value(struct gpio_chip *gc,
				   unsigned offset, int val)
{
	struct tca6507_chip *tca = container_of(gc, struct tca6507_chip, gpio);
	unsigned long flags;

	spin_lock_irqsave(&tca->lock, flags);
	/*
	 * 'OFF' is floating high, and 'ON' is pulled down, so it has the
	 * inverse sense of 'val'.
	 */
	set_select(tca, tca->gpio_map[offset],
		   val ? TCA6507_LS_LED_OFF : TCA6507_LS_LED_ON);
	spin_unlock_irqrestore(&tca->lock, flags);
	if (tca->reg_set)
		schedule_work(&tca->work);
}

static int tca6507_gpio_direction_output(struct gpio_chip *gc,
					  unsigned offset, int val)
{
	tca6507_gpio_set_value(gc, offset, val);
	return 0;
}

static int tca6507_probe_gpios(struct i2c_client *client,
			       struct tca6507_chip *tca,
			       struct tca6507_platform_data *pdata)
{
	int err;
	int i = 0;
	int gpios = 0;

	for (i = 0; i < NUM_LEDS; i++)
		if (pdata->leds.leds[i].name && pdata->leds.leds[i].flags) {
			/* Configure as a gpio */
			tca->gpio_name[gpios] = pdata->leds.leds[i].name;
			tca->gpio_map[gpios] = i;
			gpios++;
		}

	if (!gpios)
		return 0;

	tca->gpio.label = "gpio-tca6507";
	tca->gpio.names = tca->gpio_name;
	tca->gpio.ngpio = gpios;
	tca->gpio.base = pdata->gpio_base;
	tca->gpio.owner = THIS_MODULE;
	tca->gpio.direction_output = tca6507_gpio_direction_output;
	tca->gpio.set = tca6507_gpio_set_value;
	tca->gpio.dev = &client->dev;
	err = gpiochip_add(&tca->gpio);
	if (err) {
		tca->gpio.ngpio = 0;
		return err;
	}
	if (pdata->setup)
		pdata->setup(tca->gpio.base, tca->gpio.ngpio);
	return 0;
}

static void tca6507_remove_gpio(struct tca6507_chip *tca)
{
	if (tca->gpio.ngpio) {
		int err = gpiochip_remove(&tca->gpio);
		dev_err(&tca->client->dev, "%s failed, %d\n",
			"gpiochip_remove()", err);
	}
}
#else /* CONFIG_GPIOLIB */
static int tca6507_probe_gpios(struct i2c_client *client,
			       struct tca6507_chip *tca,
			       struct tca6507_platform_data *pdata)
{
	return 0;
}
static void tca6507_remove_gpio(struct tca6507_chip *tca)
{
}
#endif /* CONFIG_GPIOLIB */

static int __devinit tca6507_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct tca6507_chip *tca;
	struct i2c_adapter *adapter;
	struct tca6507_platform_data *pdata;
	int err;
	int i = 0;

	adapter = to_i2c_adapter(client->dev.parent);
	pdata = client->dev.platform_data;

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C))
		return -EIO;

	if (!pdata || pdata->leds.num_leds != NUM_LEDS) {
		dev_err(&client->dev, "Need %d entries in platform-data list\n",
			NUM_LEDS);
		return -ENODEV;
	}
	tca = kzalloc(sizeof(*tca), GFP_KERNEL);
	if (!tca)
		return -ENOMEM;

	tca->client = client;
	INIT_WORK(&tca->work, tca6507_work);
	spin_lock_init(&tca->lock);
	i2c_set_clientdata(client, tca);

	for (i = 0; i < NUM_LEDS; i++) {
		struct tca6507_led *l = tca->leds + i;

		l->chip = tca;
		l->num = i;
		if (pdata->leds.leds[i].name && !pdata->leds.leds[i].flags) {
			l->led_cdev.name = pdata->leds.leds[i].name;
			l->led_cdev.default_trigger
				= pdata->leds.leds[i].default_trigger;
			l->led_cdev.brightness_set = tca6507_brightness_set;
			l->led_cdev.blink_set = tca6507_blink_set;
			l->bank = -1;
			err = led_classdev_register(&client->dev,
						    &l->led_cdev);
			if (err < 0)
				goto exit;
		}
	}
	err = tca6507_probe_gpios(client, tca, pdata);
	if (err)
		goto exit;
	/* set all registers to known state - zero */
	tca->reg_set = 0x7f;
	schedule_work(&tca->work);

	return 0;
exit:
	while (i--) {
		if (tca->leds[i].led_cdev.name)
			led_classdev_unregister(&tca->leds[i].led_cdev);
	}
	kfree(tca);
	return err;
}

static int __devexit tca6507_remove(struct i2c_client *client)
{
	int i;
	struct tca6507_chip *tca = i2c_get_clientdata(client);
	struct tca6507_led *tca_leds = tca->leds;

	for (i = 0; i < NUM_LEDS; i++) {
		if (tca_leds[i].led_cdev.name)
			led_classdev_unregister(&tca_leds[i].led_cdev);
	}
	tca6507_remove_gpio(tca);
	cancel_work_sync(&tca->work);
	kfree(tca);

	return 0;
}

static struct i2c_driver tca6507_driver = {
	.driver   = {
		.name    = "leds-tca6507",
		.owner   = THIS_MODULE,
	},
	.probe    = tca6507_probe,
	.remove   = __devexit_p(tca6507_remove),
	.id_table = tca6507_id,
};

module_i2c_driver(tca6507_driver);

MODULE_AUTHOR("NeilBrown <neilb@suse.de>");
MODULE_DESCRIPTION("TCA6507 LED/GPO driver");
MODULE_LICENSE("GPL v2");
