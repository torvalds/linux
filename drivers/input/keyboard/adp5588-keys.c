// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * File: drivers/input/keyboard/adp5588_keys.c
 * Description:  keypad driver for ADP5588 and ADP5587
 *		 I2C QWERTY Keypad and IO Expander
 * Bugs: Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2008-2010 Analog Devices Inc.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>

#include <linux/platform_data/adp5588.h>

/* Key Event Register xy */
#define KEY_EV_PRESSED		(1 << 7)
#define KEY_EV_MASK		(0x7F)

#define KP_SEL(x)		(0xFFFF >> (16 - x))	/* 2^x-1 */

#define KEYP_MAX_EVENT		10

/*
 * Early pre 4.0 Silicon required to delay readout by at least 25ms,
 * since the Event Counter Register updated 25ms after the interrupt
 * asserted.
 */
#define WA_DELAYED_READOUT_REVID(rev)		((rev) < 4)
#define WA_DELAYED_READOUT_TIME			25

struct adp5588_kpad {
	struct i2c_client *client;
	struct input_dev *input;
	ktime_t irq_time;
	unsigned long delay;
	unsigned short keycode[ADP5588_KEYMAPSIZE];
	const struct adp5588_gpi_map *gpimap;
	unsigned short gpimapsize;
#ifdef CONFIG_GPIOLIB
	unsigned char gpiomap[ADP5588_MAXGPIO];
	struct gpio_chip gc;
	struct mutex gpio_lock;	/* Protect cached dir, dat_out */
	u8 dat_out[3];
	u8 dir[3];
#endif
};

static int adp5588_read(struct i2c_client *client, u8 reg)
{
	int ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0)
		dev_err(&client->dev, "Read Error\n");

	return ret;
}

static int adp5588_write(struct i2c_client *client, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(client, reg, val);
}

#ifdef CONFIG_GPIOLIB
static int adp5588_gpio_get_value(struct gpio_chip *chip, unsigned off)
{
	struct adp5588_kpad *kpad = gpiochip_get_data(chip);
	unsigned int bank = ADP5588_BANK(kpad->gpiomap[off]);
	unsigned int bit = ADP5588_BIT(kpad->gpiomap[off]);
	int val;

	mutex_lock(&kpad->gpio_lock);

	if (kpad->dir[bank] & bit)
		val = kpad->dat_out[bank];
	else
		val = adp5588_read(kpad->client, GPIO_DAT_STAT1 + bank);

	mutex_unlock(&kpad->gpio_lock);

	return !!(val & bit);
}

static void adp5588_gpio_set_value(struct gpio_chip *chip,
				   unsigned off, int val)
{
	struct adp5588_kpad *kpad = gpiochip_get_data(chip);
	unsigned int bank = ADP5588_BANK(kpad->gpiomap[off]);
	unsigned int bit = ADP5588_BIT(kpad->gpiomap[off]);

	mutex_lock(&kpad->gpio_lock);

	if (val)
		kpad->dat_out[bank] |= bit;
	else
		kpad->dat_out[bank] &= ~bit;

	adp5588_write(kpad->client, GPIO_DAT_OUT1 + bank,
			   kpad->dat_out[bank]);

	mutex_unlock(&kpad->gpio_lock);
}

static int adp5588_gpio_direction_input(struct gpio_chip *chip, unsigned off)
{
	struct adp5588_kpad *kpad = gpiochip_get_data(chip);
	unsigned int bank = ADP5588_BANK(kpad->gpiomap[off]);
	unsigned int bit = ADP5588_BIT(kpad->gpiomap[off]);
	int ret;

	mutex_lock(&kpad->gpio_lock);

	kpad->dir[bank] &= ~bit;
	ret = adp5588_write(kpad->client, GPIO_DIR1 + bank, kpad->dir[bank]);

	mutex_unlock(&kpad->gpio_lock);

	return ret;
}

static int adp5588_gpio_direction_output(struct gpio_chip *chip,
					 unsigned off, int val)
{
	struct adp5588_kpad *kpad = gpiochip_get_data(chip);
	unsigned int bank = ADP5588_BANK(kpad->gpiomap[off]);
	unsigned int bit = ADP5588_BIT(kpad->gpiomap[off]);
	int ret;

	mutex_lock(&kpad->gpio_lock);

	kpad->dir[bank] |= bit;

	if (val)
		kpad->dat_out[bank] |= bit;
	else
		kpad->dat_out[bank] &= ~bit;

	ret = adp5588_write(kpad->client, GPIO_DAT_OUT1 + bank,
				 kpad->dat_out[bank]);
	ret |= adp5588_write(kpad->client, GPIO_DIR1 + bank,
				 kpad->dir[bank]);

	mutex_unlock(&kpad->gpio_lock);

	return ret;
}

static int adp5588_build_gpiomap(struct adp5588_kpad *kpad,
				const struct adp5588_kpad_platform_data *pdata)
{
	bool pin_used[ADP5588_MAXGPIO];
	int n_unused = 0;
	int i;

	memset(pin_used, 0, sizeof(pin_used));

	for (i = 0; i < pdata->rows; i++)
		pin_used[i] = true;

	for (i = 0; i < pdata->cols; i++)
		pin_used[i + GPI_PIN_COL_BASE - GPI_PIN_BASE] = true;

	for (i = 0; i < kpad->gpimapsize; i++)
		pin_used[kpad->gpimap[i].pin - GPI_PIN_BASE] = true;

	for (i = 0; i < ADP5588_MAXGPIO; i++)
		if (!pin_used[i])
			kpad->gpiomap[n_unused++] = i;

	return n_unused;
}

static void adp5588_gpio_do_teardown(void *_kpad)
{
	struct adp5588_kpad *kpad = _kpad;
	struct device *dev = &kpad->client->dev;
	const struct adp5588_kpad_platform_data *pdata = dev_get_platdata(dev);
	const struct adp5588_gpio_platform_data *gpio_data = pdata->gpio_data;
	int error;

	error = gpio_data->teardown(kpad->client,
				    kpad->gc.base, kpad->gc.ngpio,
				    gpio_data->context);
	if (error)
		dev_warn(&kpad->client->dev, "teardown failed %d\n", error);
}

static int adp5588_gpio_add(struct adp5588_kpad *kpad)
{
	struct device *dev = &kpad->client->dev;
	const struct adp5588_kpad_platform_data *pdata = dev_get_platdata(dev);
	const struct adp5588_gpio_platform_data *gpio_data = pdata->gpio_data;
	int i, error;

	if (!gpio_data)
		return 0;

	kpad->gc.ngpio = adp5588_build_gpiomap(kpad, pdata);
	if (kpad->gc.ngpio == 0) {
		dev_info(dev, "No unused gpios left to export\n");
		return 0;
	}

	kpad->gc.direction_input = adp5588_gpio_direction_input;
	kpad->gc.direction_output = adp5588_gpio_direction_output;
	kpad->gc.get = adp5588_gpio_get_value;
	kpad->gc.set = adp5588_gpio_set_value;
	kpad->gc.can_sleep = 1;

	kpad->gc.base = gpio_data->gpio_start;
	kpad->gc.label = kpad->client->name;
	kpad->gc.owner = THIS_MODULE;
	kpad->gc.names = gpio_data->names;

	mutex_init(&kpad->gpio_lock);

	error = devm_gpiochip_add_data(dev, &kpad->gc, kpad);
	if (error) {
		dev_err(dev, "gpiochip_add failed: %d\n", error);
		return error;
	}

	for (i = 0; i <= ADP5588_BANK(ADP5588_MAXGPIO); i++) {
		kpad->dat_out[i] = adp5588_read(kpad->client,
						GPIO_DAT_OUT1 + i);
		kpad->dir[i] = adp5588_read(kpad->client, GPIO_DIR1 + i);
	}

	if (gpio_data->setup) {
		error = gpio_data->setup(kpad->client,
					 kpad->gc.base, kpad->gc.ngpio,
					 gpio_data->context);
		if (error)
			dev_warn(dev, "setup failed: %d\n", error);
	}

	if (gpio_data->teardown) {
		error = devm_add_action(dev, adp5588_gpio_do_teardown, kpad);
		if (error)
			dev_warn(dev, "failed to schedule teardown: %d\n",
				 error);
	}

	return 0;
}

#else
static inline int adp5588_gpio_add(struct adp5588_kpad *kpad)
{
	return 0;
}
#endif

static void adp5588_report_events(struct adp5588_kpad *kpad, int ev_cnt)
{
	int i, j;

	for (i = 0; i < ev_cnt; i++) {
		int key = adp5588_read(kpad->client, Key_EVENTA + i);
		int key_val = key & KEY_EV_MASK;

		if (key_val >= GPI_PIN_BASE && key_val <= GPI_PIN_END) {
			for (j = 0; j < kpad->gpimapsize; j++) {
				if (key_val == kpad->gpimap[j].pin) {
					input_report_switch(kpad->input,
							kpad->gpimap[j].sw_evt,
							key & KEY_EV_PRESSED);
					break;
				}
			}
		} else {
			input_report_key(kpad->input,
					 kpad->keycode[key_val - 1],
					 key & KEY_EV_PRESSED);
		}
	}
}

static irqreturn_t adp5588_hard_irq(int irq, void *handle)
{
	struct adp5588_kpad *kpad = handle;

	kpad->irq_time = ktime_get();

	return IRQ_WAKE_THREAD;
}

static irqreturn_t adp5588_thread_irq(int irq, void *handle)
{
	struct adp5588_kpad *kpad = handle;
	struct i2c_client *client = kpad->client;
	ktime_t target_time, now;
	unsigned long delay;
	int status, ev_cnt;

	/*
	 * Readout needs to wait for at least 25ms after the notification
	 * for REVID < 4.
	 */
	if (kpad->delay) {
		target_time = ktime_add_ms(kpad->irq_time, kpad->delay);
		now = ktime_get();
		if (ktime_before(now, target_time)) {
			delay = ktime_to_us(ktime_sub(target_time, now));
			usleep_range(delay, delay + 1000);
		}
	}

	status = adp5588_read(client, INT_STAT);

	if (status & ADP5588_OVR_FLOW_INT)	/* Unlikely and should never happen */
		dev_err(&client->dev, "Event Overflow Error\n");

	if (status & ADP5588_KE_INT) {
		ev_cnt = adp5588_read(client, KEY_LCK_EC_STAT) & ADP5588_KEC;
		if (ev_cnt) {
			adp5588_report_events(kpad, ev_cnt);
			input_sync(kpad->input);
		}
	}

	adp5588_write(client, INT_STAT, status); /* Status is W1C */

	return IRQ_HANDLED;
}

static int adp5588_setup(struct i2c_client *client)
{
	const struct adp5588_kpad_platform_data *pdata =
			dev_get_platdata(&client->dev);
	const struct adp5588_gpio_platform_data *gpio_data = pdata->gpio_data;
	int i, ret;
	unsigned char evt_mode1 = 0, evt_mode2 = 0, evt_mode3 = 0;

	ret = adp5588_write(client, KP_GPIO1, KP_SEL(pdata->rows));
	ret |= adp5588_write(client, KP_GPIO2, KP_SEL(pdata->cols) & 0xFF);
	ret |= adp5588_write(client, KP_GPIO3, KP_SEL(pdata->cols) >> 8);

	if (pdata->en_keylock) {
		ret |= adp5588_write(client, UNLOCK1, pdata->unlock_key1);
		ret |= adp5588_write(client, UNLOCK2, pdata->unlock_key2);
		ret |= adp5588_write(client, KEY_LCK_EC_STAT, ADP5588_K_LCK_EN);
	}

	for (i = 0; i < KEYP_MAX_EVENT; i++)
		ret |= adp5588_read(client, Key_EVENTA);

	for (i = 0; i < pdata->gpimapsize; i++) {
		unsigned short pin = pdata->gpimap[i].pin;

		if (pin <= GPI_PIN_ROW_END) {
			evt_mode1 |= (1 << (pin - GPI_PIN_ROW_BASE));
		} else {
			evt_mode2 |= ((1 << (pin - GPI_PIN_COL_BASE)) & 0xFF);
			evt_mode3 |= ((1 << (pin - GPI_PIN_COL_BASE)) >> 8);
		}
	}

	if (pdata->gpimapsize) {
		ret |= adp5588_write(client, GPI_EM1, evt_mode1);
		ret |= adp5588_write(client, GPI_EM2, evt_mode2);
		ret |= adp5588_write(client, GPI_EM3, evt_mode3);
	}

	if (gpio_data) {
		for (i = 0; i <= ADP5588_BANK(ADP5588_MAXGPIO); i++) {
			int pull_mask = gpio_data->pullup_dis_mask;

			ret |= adp5588_write(client, GPIO_PULL1 + i,
				(pull_mask >> (8 * i)) & 0xFF);
		}
	}

	ret |= adp5588_write(client, INT_STAT,
				ADP5588_CMP2_INT | ADP5588_CMP1_INT |
				ADP5588_OVR_FLOW_INT | ADP5588_K_LCK_INT |
				ADP5588_GPI_INT | ADP5588_KE_INT); /* Status is W1C */

	ret |= adp5588_write(client, CFG, ADP5588_INT_CFG |
					  ADP5588_OVR_FLOW_IEN |
					  ADP5588_KE_IEN);

	if (ret < 0) {
		dev_err(&client->dev, "Write Error\n");
		return ret;
	}

	return 0;
}

static void adp5588_report_switch_state(struct adp5588_kpad *kpad)
{
	int gpi_stat1 = adp5588_read(kpad->client, GPIO_DAT_STAT1);
	int gpi_stat2 = adp5588_read(kpad->client, GPIO_DAT_STAT2);
	int gpi_stat3 = adp5588_read(kpad->client, GPIO_DAT_STAT3);
	int gpi_stat_tmp, pin_loc;
	int i;

	for (i = 0; i < kpad->gpimapsize; i++) {
		unsigned short pin = kpad->gpimap[i].pin;

		if (pin <= GPI_PIN_ROW_END) {
			gpi_stat_tmp = gpi_stat1;
			pin_loc = pin - GPI_PIN_ROW_BASE;
		} else if ((pin - GPI_PIN_COL_BASE) < 8) {
			gpi_stat_tmp = gpi_stat2;
			pin_loc = pin - GPI_PIN_COL_BASE;
		} else {
			gpi_stat_tmp = gpi_stat3;
			pin_loc = pin - GPI_PIN_COL_BASE - 8;
		}

		if (gpi_stat_tmp < 0) {
			dev_err(&kpad->client->dev,
				"Can't read GPIO_DAT_STAT switch %d default to OFF\n",
				pin);
			gpi_stat_tmp = 0;
		}

		input_report_switch(kpad->input,
				    kpad->gpimap[i].sw_evt,
				    !(gpi_stat_tmp & (1 << pin_loc)));
	}

	input_sync(kpad->input);
}


static int adp5588_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct adp5588_kpad *kpad;
	const struct adp5588_kpad_platform_data *pdata =
			dev_get_platdata(&client->dev);
	struct input_dev *input;
	unsigned int revid;
	int ret, i;
	int error;

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "SMBUS Byte Data not Supported\n");
		return -EIO;
	}

	if (!pdata) {
		dev_err(&client->dev, "no platform data?\n");
		return -EINVAL;
	}

	if (!pdata->rows || !pdata->cols || !pdata->keymap) {
		dev_err(&client->dev, "no rows, cols or keymap from pdata\n");
		return -EINVAL;
	}

	if (pdata->keymapsize != ADP5588_KEYMAPSIZE) {
		dev_err(&client->dev, "invalid keymapsize\n");
		return -EINVAL;
	}

	if (!pdata->gpimap && pdata->gpimapsize) {
		dev_err(&client->dev, "invalid gpimap from pdata\n");
		return -EINVAL;
	}

	if (pdata->gpimapsize > ADP5588_GPIMAPSIZE_MAX) {
		dev_err(&client->dev, "invalid gpimapsize\n");
		return -EINVAL;
	}

	for (i = 0; i < pdata->gpimapsize; i++) {
		unsigned short pin = pdata->gpimap[i].pin;

		if (pin < GPI_PIN_BASE || pin > GPI_PIN_END) {
			dev_err(&client->dev, "invalid gpi pin data\n");
			return -EINVAL;
		}

		if (pin <= GPI_PIN_ROW_END) {
			if (pin - GPI_PIN_ROW_BASE + 1 <= pdata->rows) {
				dev_err(&client->dev, "invalid gpi row data\n");
				return -EINVAL;
			}
		} else {
			if (pin - GPI_PIN_COL_BASE + 1 <= pdata->cols) {
				dev_err(&client->dev, "invalid gpi col data\n");
				return -EINVAL;
			}
		}
	}

	if (!client->irq) {
		dev_err(&client->dev, "no IRQ?\n");
		return -EINVAL;
	}

	kpad = devm_kzalloc(&client->dev, sizeof(*kpad), GFP_KERNEL);
	if (!kpad)
		return -ENOMEM;

	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;

	kpad->client = client;
	kpad->input = input;

	ret = adp5588_read(client, DEV_ID);
	if (ret < 0)
		return ret;

	revid = (u8) ret & ADP5588_DEVICE_ID_MASK;
	if (WA_DELAYED_READOUT_REVID(revid))
		kpad->delay = msecs_to_jiffies(WA_DELAYED_READOUT_TIME);

	input->name = client->name;
	input->phys = "adp5588-keys/input0";

	input_set_drvdata(input, kpad);

	input->id.bustype = BUS_I2C;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = revid;

	input->keycodesize = sizeof(kpad->keycode[0]);
	input->keycodemax = pdata->keymapsize;
	input->keycode = kpad->keycode;

	memcpy(kpad->keycode, pdata->keymap,
		pdata->keymapsize * input->keycodesize);

	kpad->gpimap = pdata->gpimap;
	kpad->gpimapsize = pdata->gpimapsize;

	/* setup input device */
	__set_bit(EV_KEY, input->evbit);

	if (pdata->repeat)
		__set_bit(EV_REP, input->evbit);

	for (i = 0; i < input->keycodemax; i++)
		if (kpad->keycode[i] <= KEY_MAX)
			__set_bit(kpad->keycode[i], input->keybit);
	__clear_bit(KEY_RESERVED, input->keybit);

	if (kpad->gpimapsize)
		__set_bit(EV_SW, input->evbit);
	for (i = 0; i < kpad->gpimapsize; i++)
		__set_bit(kpad->gpimap[i].sw_evt, input->swbit);

	error = input_register_device(input);
	if (error) {
		dev_err(&client->dev, "unable to register input device: %d\n",
			error);
		return error;
	}

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  adp5588_hard_irq, adp5588_thread_irq,
					  IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					  client->dev.driver->name, kpad);
	if (error) {
		dev_err(&client->dev, "failed to request irq %d: %d\n",
			client->irq, error);
		return error;
	}

	error = adp5588_setup(client);
	if (error)
		return error;

	if (kpad->gpimapsize)
		adp5588_report_switch_state(kpad);

	error = adp5588_gpio_add(kpad);
	if (error)
		return error;

	dev_info(&client->dev, "Rev.%d keypad, irq %d\n", revid, client->irq);
	return 0;
}

static void adp5588_remove(struct i2c_client *client)
{
	adp5588_write(client, CFG, 0);

	/* all resources will be freed by devm */
}

static int __maybe_unused adp5588_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	disable_irq(client->irq);

	return 0;
}

static int __maybe_unused adp5588_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	enable_irq(client->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(adp5588_dev_pm_ops, adp5588_suspend, adp5588_resume);

static const struct i2c_device_id adp5588_id[] = {
	{ "adp5588-keys", 0 },
	{ "adp5587-keys", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adp5588_id);

static struct i2c_driver adp5588_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.pm   = &adp5588_dev_pm_ops,
	},
	.probe    = adp5588_probe,
	.remove   = adp5588_remove,
	.id_table = adp5588_id,
};

module_i2c_driver(adp5588_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("ADP5588/87 Keypad driver");
