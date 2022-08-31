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

#define ADP5588_INVALID_HWIRQ	(~0UL)

struct adp5588_kpad {
	struct i2c_client *client;
	struct input_dev *input;
	ktime_t irq_time;
	unsigned long delay;
	unsigned short keycode[ADP5588_KEYMAPSIZE];
	unsigned char gpiomap[ADP5588_MAXGPIO];
	struct gpio_chip gc;
	struct mutex gpio_lock;	/* Protect cached dir, dat_out */
	u8 dat_out[3];
	u8 dir[3];
	u8 int_en[3];
	u8 irq_mask[3];
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

static void adp5588_irq_bus_lock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adp5588_kpad *kpad = gpiochip_get_data(gc);

	mutex_lock(&kpad->gpio_lock);
}

static void adp5588_irq_bus_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adp5588_kpad *kpad = gpiochip_get_data(gc);
	int i;

	for (i = 0; i <= ADP5588_BANK(ADP5588_MAXGPIO); i++) {
		if (kpad->int_en[i] ^ kpad->irq_mask[i]) {
			kpad->int_en[i] = kpad->irq_mask[i];
			adp5588_write(kpad->client, GPI_EM1 + i, kpad->int_en[i]);
		}
	}

	mutex_unlock(&kpad->gpio_lock);
}

static void adp5588_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adp5588_kpad *kpad = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	unsigned long real_irq = kpad->gpiomap[hwirq];

	kpad->irq_mask[ADP5588_BANK(real_irq)] &= ~ADP5588_BIT(real_irq);
	gpiochip_disable_irq(gc, hwirq);
}

static void adp5588_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct adp5588_kpad *kpad = gpiochip_get_data(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	unsigned long real_irq = kpad->gpiomap[hwirq];

	gpiochip_enable_irq(gc, hwirq);
	kpad->irq_mask[ADP5588_BANK(real_irq)] |= ADP5588_BIT(real_irq);
}

static int adp5588_irq_set_type(struct irq_data *d, unsigned int type)
{
	if (!(type & IRQ_TYPE_EDGE_BOTH))
		return -EINVAL;

	irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static const struct irq_chip adp5588_irq_chip = {
	.name = "adp5588",
	.irq_mask = adp5588_irq_mask,
	.irq_unmask = adp5588_irq_unmask,
	.irq_bus_lock = adp5588_irq_bus_lock,
	.irq_bus_sync_unlock = adp5588_irq_bus_sync_unlock,
	.irq_set_type = adp5588_irq_set_type,
	.flags = IRQCHIP_SKIP_SET_WAKE | IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int adp5588_gpio_add(struct adp5588_kpad *kpad)
{
	struct device *dev = &kpad->client->dev;
	const struct adp5588_kpad_platform_data *pdata = dev_get_platdata(dev);
	const struct adp5588_gpio_platform_data *gpio_data = pdata->gpio_data;
	struct gpio_irq_chip *girq;
	int i, error;

	if (!gpio_data)
		return 0;

	kpad->gc.ngpio = adp5588_build_gpiomap(kpad, pdata);
	if (kpad->gc.ngpio == 0) {
		dev_info(dev, "No unused gpios left to export\n");
		return 0;
	}

	kpad->gc.parent = &kpad->client->dev;
	kpad->gc.direction_input = adp5588_gpio_direction_input;
	kpad->gc.direction_output = adp5588_gpio_direction_output;
	kpad->gc.get = adp5588_gpio_get_value;
	kpad->gc.set = adp5588_gpio_set_value;
	kpad->gc.can_sleep = 1;

	kpad->gc.base = gpio_data->gpio_start;
	kpad->gc.label = kpad->client->name;
	kpad->gc.owner = THIS_MODULE;
	kpad->gc.names = gpio_data->names;

	girq = &kpad->gc.irq;
	gpio_irq_chip_set_chip(girq, &adp5588_irq_chip);
	girq->handler = handle_bad_irq;
	girq->threaded = true;

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

static unsigned long adp5588_gpiomap_get_hwirq(struct device *dev,
					       const u8 *map, unsigned int gpio,
					       unsigned int ngpios)
{
	unsigned int hwirq;

	for (hwirq = 0; hwirq < ngpios; hwirq++)
		if (map[hwirq] == gpio)
			return hwirq;

	/* should never happen */
	dev_warn_ratelimited(dev, "could not find the hwirq for gpio(%u)\n", gpio);

	return ADP5588_INVALID_HWIRQ;
}

static void adp5588_gpio_irq_handle(struct adp5588_kpad *kpad, int key_val,
				    int key_press)
{
	unsigned int irq, gpio = key_val - GPI_PIN_BASE, irq_type;
	struct i2c_client *client = kpad->client;
	struct irq_data *irqd;
	unsigned long hwirq;

	hwirq = adp5588_gpiomap_get_hwirq(&client->dev, kpad->gpiomap,
					  gpio, kpad->gc.ngpio);
	if (hwirq == ADP5588_INVALID_HWIRQ) {
		dev_err(&client->dev, "Could not get hwirq for key(%u)\n", key_val);
		return;
	}

	irq = irq_find_mapping(kpad->gc.irq.domain, hwirq);
	if (!irq)
		return;

	irqd = irq_get_irq_data(irq);
	if (!irqd) {
		dev_err(&client->dev, "Could not get irq(%u) data\n", irq);
		return;
	}

	irq_type = irqd_get_trigger_type(irqd);

	/*
	 * Default is active low which means key_press is asserted on
	 * the falling edge.
	 */
	if ((irq_type & IRQ_TYPE_EDGE_RISING && !key_press) ||
	    (irq_type & IRQ_TYPE_EDGE_FALLING && key_press))
		handle_nested_irq(irq);
}

static void adp5588_report_events(struct adp5588_kpad *kpad, int ev_cnt)
{
	int i;

	for (i = 0; i < ev_cnt; i++) {
		int key = adp5588_read(kpad->client, Key_EVENTA + i);
		int key_val = key & KEY_EV_MASK;
		int key_press = key & KEY_EV_PRESSED;

		if (key_val >= GPI_PIN_BASE && key_val <= GPI_PIN_END)
			/* gpio line used as IRQ source */
			adp5588_gpio_irq_handle(kpad, key_val, key_press);
		else
			input_report_key(kpad->input,
					 kpad->keycode[key_val - 1], key_press);
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

	/* setup input device */
	__set_bit(EV_KEY, input->evbit);

	if (pdata->repeat)
		__set_bit(EV_REP, input->evbit);

	for (i = 0; i < input->keycodemax; i++)
		if (kpad->keycode[i] <= KEY_MAX)
			__set_bit(kpad->keycode[i], input->keybit);
	__clear_bit(KEY_RESERVED, input->keybit);

	error = input_register_device(input);
	if (error) {
		dev_err(&client->dev, "unable to register input device: %d\n",
			error);
		return error;
	}

	error = adp5588_setup(client);
	if (error)
		return error;

	error = adp5588_gpio_add(kpad);
	if (error)
		return error;

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  adp5588_hard_irq, adp5588_thread_irq,
					  IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					  client->dev.driver->name, kpad);
	if (error) {
		dev_err(&client->dev, "failed to request irq %d: %d\n",
			client->irq, error);
		return error;
	}

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
