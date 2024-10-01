// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * File: drivers/input/keyboard/adp5588_keys.c
 * Description:  keypad driver for ADP5588 and ADP5587
 *		 I2C QWERTY Keypad and IO Expander
 * Bugs: Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2008-2010 Analog Devices Inc.
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/matrix_keypad.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>

#define DEV_ID 0x00		/* Device ID */
#define CFG 0x01		/* Configuration Register1 */
#define INT_STAT 0x02		/* Interrupt Status Register */
#define KEY_LCK_EC_STAT 0x03	/* Key Lock and Event Counter Register */
#define KEY_EVENTA 0x04		/* Key Event Register A */
#define KEY_EVENTB 0x05		/* Key Event Register B */
#define KEY_EVENTC 0x06		/* Key Event Register C */
#define KEY_EVENTD 0x07		/* Key Event Register D */
#define KEY_EVENTE 0x08		/* Key Event Register E */
#define KEY_EVENTF 0x09		/* Key Event Register F */
#define KEY_EVENTG 0x0A		/* Key Event Register G */
#define KEY_EVENTH 0x0B		/* Key Event Register H */
#define KEY_EVENTI 0x0C		/* Key Event Register I */
#define KEY_EVENTJ 0x0D		/* Key Event Register J */
#define KP_LCK_TMR 0x0E		/* Keypad Lock1 to Lock2 Timer */
#define UNLOCK1 0x0F		/* Unlock Key1 */
#define UNLOCK2 0x10		/* Unlock Key2 */
#define GPIO_INT_STAT1 0x11	/* GPIO Interrupt Status */
#define GPIO_INT_STAT2 0x12	/* GPIO Interrupt Status */
#define GPIO_INT_STAT3 0x13	/* GPIO Interrupt Status */
#define GPIO_DAT_STAT1 0x14	/* GPIO Data Status, Read twice to clear */
#define GPIO_DAT_STAT2 0x15	/* GPIO Data Status, Read twice to clear */
#define GPIO_DAT_STAT3 0x16	/* GPIO Data Status, Read twice to clear */
#define GPIO_DAT_OUT1 0x17	/* GPIO DATA OUT */
#define GPIO_DAT_OUT2 0x18	/* GPIO DATA OUT */
#define GPIO_DAT_OUT3 0x19	/* GPIO DATA OUT */
#define GPIO_INT_EN1 0x1A	/* GPIO Interrupt Enable */
#define GPIO_INT_EN2 0x1B	/* GPIO Interrupt Enable */
#define GPIO_INT_EN3 0x1C	/* GPIO Interrupt Enable */
#define KP_GPIO1 0x1D		/* Keypad or GPIO Selection */
#define KP_GPIO2 0x1E		/* Keypad or GPIO Selection */
#define KP_GPIO3 0x1F		/* Keypad or GPIO Selection */
#define GPI_EM1 0x20		/* GPI Event Mode 1 */
#define GPI_EM2 0x21		/* GPI Event Mode 2 */
#define GPI_EM3 0x22		/* GPI Event Mode 3 */
#define GPIO_DIR1 0x23		/* GPIO Data Direction */
#define GPIO_DIR2 0x24		/* GPIO Data Direction */
#define GPIO_DIR3 0x25		/* GPIO Data Direction */
#define GPIO_INT_LVL1 0x26	/* GPIO Edge/Level Detect */
#define GPIO_INT_LVL2 0x27	/* GPIO Edge/Level Detect */
#define GPIO_INT_LVL3 0x28	/* GPIO Edge/Level Detect */
#define DEBOUNCE_DIS1 0x29	/* Debounce Disable */
#define DEBOUNCE_DIS2 0x2A	/* Debounce Disable */
#define DEBOUNCE_DIS3 0x2B	/* Debounce Disable */
#define GPIO_PULL1 0x2C		/* GPIO Pull Disable */
#define GPIO_PULL2 0x2D		/* GPIO Pull Disable */
#define GPIO_PULL3 0x2E		/* GPIO Pull Disable */
#define CMP_CFG_STAT 0x30	/* Comparator Configuration and Status Register */
#define CMP_CONFG_SENS1 0x31	/* Sensor1 Comparator Configuration Register */
#define CMP_CONFG_SENS2 0x32	/* L2 Light Sensor Reference Level, Output Falling for Sensor 1 */
#define CMP1_LVL2_TRIP 0x33	/* L2 Light Sensor Hysteresis (Active when Output Rising) for Sensor 1 */
#define CMP1_LVL2_HYS 0x34	/* L3 Light Sensor Reference Level, Output Falling For Sensor 1 */
#define CMP1_LVL3_TRIP 0x35	/* L3 Light Sensor Hysteresis (Active when Output Rising) For Sensor 1 */
#define CMP1_LVL3_HYS 0x36	/* Sensor 2 Comparator Configuration Register */
#define CMP2_LVL2_TRIP 0x37	/* L2 Light Sensor Reference Level, Output Falling for Sensor 2 */
#define CMP2_LVL2_HYS 0x38	/* L2 Light Sensor Hysteresis (Active when Output Rising) for Sensor 2 */
#define CMP2_LVL3_TRIP 0x39	/* L3 Light Sensor Reference Level, Output Falling For Sensor 2 */
#define CMP2_LVL3_HYS 0x3A	/* L3 Light Sensor Hysteresis (Active when Output Rising) For Sensor 2 */
#define CMP1_ADC_DAT_R1 0x3B	/* Comparator 1 ADC data Register1 */
#define CMP1_ADC_DAT_R2 0x3C	/* Comparator 1 ADC data Register2 */
#define CMP2_ADC_DAT_R1 0x3D	/* Comparator 2 ADC data Register1 */
#define CMP2_ADC_DAT_R2 0x3E	/* Comparator 2 ADC data Register2 */

#define ADP5588_DEVICE_ID_MASK	0xF

 /* Configuration Register1 */
#define ADP5588_AUTO_INC	BIT(7)
#define ADP5588_GPIEM_CFG	BIT(6)
#define ADP5588_OVR_FLOW_M	BIT(5)
#define ADP5588_INT_CFG		BIT(4)
#define ADP5588_OVR_FLOW_IEN	BIT(3)
#define ADP5588_K_LCK_IM	BIT(2)
#define ADP5588_GPI_IEN		BIT(1)
#define ADP5588_KE_IEN		BIT(0)

/* Interrupt Status Register */
#define ADP5588_CMP2_INT	BIT(5)
#define ADP5588_CMP1_INT	BIT(4)
#define ADP5588_OVR_FLOW_INT	BIT(3)
#define ADP5588_K_LCK_INT	BIT(2)
#define ADP5588_GPI_INT		BIT(1)
#define ADP5588_KE_INT		BIT(0)

/* Key Lock and Event Counter Register */
#define ADP5588_K_LCK_EN	BIT(6)
#define ADP5588_LCK21		0x30
#define ADP5588_KEC		GENMASK(3, 0)

#define ADP5588_MAXGPIO		18
#define ADP5588_BANK(offs)	((offs) >> 3)
#define ADP5588_BIT(offs)	(1u << ((offs) & 0x7))

/* Put one of these structures in i2c_board_info platform_data */

/*
 * 128 so it fits matrix-keymap maximum number of keys when the full
 * 10cols * 8rows are used.
 */
#define ADP5588_KEYMAPSIZE 128

#define GPI_PIN_ROW0 97
#define GPI_PIN_ROW1 98
#define GPI_PIN_ROW2 99
#define GPI_PIN_ROW3 100
#define GPI_PIN_ROW4 101
#define GPI_PIN_ROW5 102
#define GPI_PIN_ROW6 103
#define GPI_PIN_ROW7 104
#define GPI_PIN_COL0 105
#define GPI_PIN_COL1 106
#define GPI_PIN_COL2 107
#define GPI_PIN_COL3 108
#define GPI_PIN_COL4 109
#define GPI_PIN_COL5 110
#define GPI_PIN_COL6 111
#define GPI_PIN_COL7 112
#define GPI_PIN_COL8 113
#define GPI_PIN_COL9 114

#define GPI_PIN_ROW_BASE GPI_PIN_ROW0
#define GPI_PIN_ROW_END GPI_PIN_ROW7
#define GPI_PIN_COL_BASE GPI_PIN_COL0
#define GPI_PIN_COL_END GPI_PIN_COL9

#define GPI_PIN_BASE GPI_PIN_ROW_BASE
#define GPI_PIN_END GPI_PIN_COL_END

#define ADP5588_ROWS_MAX (GPI_PIN_ROW7 - GPI_PIN_ROW0 + 1)
#define ADP5588_COLS_MAX (GPI_PIN_COL9 - GPI_PIN_COL0 + 1)

#define ADP5588_GPIMAPSIZE_MAX (GPI_PIN_END - GPI_PIN_BASE + 1)

/* Key Event Register xy */
#define KEY_EV_PRESSED		BIT(7)
#define KEY_EV_MASK		GENMASK(6, 0)

#define KP_SEL(x)		(BIT(x) - 1)	/* 2^x-1 */

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
	u32 row_shift;
	u32 rows;
	u32 cols;
	u32 unlock_keys[2];
	int nkeys_unlock;
	bool gpio_only;
	unsigned short keycode[ADP5588_KEYMAPSIZE];
	unsigned char gpiomap[ADP5588_MAXGPIO];
	struct gpio_chip gc;
	struct mutex gpio_lock;	/* Protect cached dir, dat_out */
	u8 dat_out[3];
	u8 dir[3];
	u8 int_en[3];
	u8 irq_mask[3];
	u8 pull_dis[3];
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

static int adp5588_gpio_get_value(struct gpio_chip *chip, unsigned int off)
{
	struct adp5588_kpad *kpad = gpiochip_get_data(chip);
	unsigned int bank = ADP5588_BANK(kpad->gpiomap[off]);
	unsigned int bit = ADP5588_BIT(kpad->gpiomap[off]);
	int val;

	guard(mutex)(&kpad->gpio_lock);

	if (kpad->dir[bank] & bit)
		val = kpad->dat_out[bank];
	else
		val = adp5588_read(kpad->client, GPIO_DAT_STAT1 + bank);

	return !!(val & bit);
}

static void adp5588_gpio_set_value(struct gpio_chip *chip,
				   unsigned int off, int val)
{
	struct adp5588_kpad *kpad = gpiochip_get_data(chip);
	unsigned int bank = ADP5588_BANK(kpad->gpiomap[off]);
	unsigned int bit = ADP5588_BIT(kpad->gpiomap[off]);

	guard(mutex)(&kpad->gpio_lock);

	if (val)
		kpad->dat_out[bank] |= bit;
	else
		kpad->dat_out[bank] &= ~bit;

	adp5588_write(kpad->client, GPIO_DAT_OUT1 + bank, kpad->dat_out[bank]);
}

static int adp5588_gpio_set_config(struct gpio_chip *chip,  unsigned int off,
				   unsigned long config)
{
	struct adp5588_kpad *kpad = gpiochip_get_data(chip);
	unsigned int bank = ADP5588_BANK(kpad->gpiomap[off]);
	unsigned int bit = ADP5588_BIT(kpad->gpiomap[off]);
	bool pull_disable;

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_BIAS_PULL_UP:
		pull_disable = false;
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		pull_disable = true;
		break;
	default:
		return -ENOTSUPP;
	}

	guard(mutex)(&kpad->gpio_lock);

	if (pull_disable)
		kpad->pull_dis[bank] |= bit;
	else
		kpad->pull_dis[bank] &= bit;

	return adp5588_write(kpad->client, GPIO_PULL1 + bank,
			     kpad->pull_dis[bank]);
}

static int adp5588_gpio_direction_input(struct gpio_chip *chip, unsigned int off)
{
	struct adp5588_kpad *kpad = gpiochip_get_data(chip);
	unsigned int bank = ADP5588_BANK(kpad->gpiomap[off]);
	unsigned int bit = ADP5588_BIT(kpad->gpiomap[off]);

	guard(mutex)(&kpad->gpio_lock);

	kpad->dir[bank] &= ~bit;
	return adp5588_write(kpad->client, GPIO_DIR1 + bank, kpad->dir[bank]);
}

static int adp5588_gpio_direction_output(struct gpio_chip *chip,
					 unsigned int off, int val)
{
	struct adp5588_kpad *kpad = gpiochip_get_data(chip);
	unsigned int bank = ADP5588_BANK(kpad->gpiomap[off]);
	unsigned int bit = ADP5588_BIT(kpad->gpiomap[off]);
	int error;

	guard(mutex)(&kpad->gpio_lock);

	kpad->dir[bank] |= bit;

	if (val)
		kpad->dat_out[bank] |= bit;
	else
		kpad->dat_out[bank] &= ~bit;

	error = adp5588_write(kpad->client, GPIO_DAT_OUT1 + bank,
			      kpad->dat_out[bank]);
	if (error)
		return error;

	error = adp5588_write(kpad->client, GPIO_DIR1 + bank, kpad->dir[bank]);
	if (error)
		return error;

	return 0;
}

static int adp5588_build_gpiomap(struct adp5588_kpad *kpad)
{
	bool pin_used[ADP5588_MAXGPIO];
	int n_unused = 0;
	int i;

	memset(pin_used, 0, sizeof(pin_used));

	for (i = 0; i < kpad->rows; i++)
		pin_used[i] = true;

	for (i = 0; i < kpad->cols; i++)
		pin_used[i + GPI_PIN_COL_BASE - GPI_PIN_BASE] = true;

	for (i = 0; i < ADP5588_MAXGPIO; i++)
		if (!pin_used[i])
			kpad->gpiomap[n_unused++] = i;

	return n_unused;
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
	struct gpio_irq_chip *girq;
	int i, error;

	kpad->gc.ngpio = adp5588_build_gpiomap(kpad);
	if (kpad->gc.ngpio == 0) {
		dev_info(dev, "No unused gpios left to export\n");
		return 0;
	}

	kpad->gc.parent = &kpad->client->dev;
	kpad->gc.direction_input = adp5588_gpio_direction_input;
	kpad->gc.direction_output = adp5588_gpio_direction_output;
	kpad->gc.get = adp5588_gpio_get_value;
	kpad->gc.set = adp5588_gpio_set_value;
	kpad->gc.set_config = adp5588_gpio_set_config;
	kpad->gc.can_sleep = 1;

	kpad->gc.base = -1;
	kpad->gc.label = kpad->client->name;
	kpad->gc.owner = THIS_MODULE;

	if (device_property_present(dev, "interrupt-controller")) {
		if (!kpad->client->irq) {
			dev_err(dev, "Unable to serve as interrupt controller without interrupt");
			return -EINVAL;
		}

		girq = &kpad->gc.irq;
		gpio_irq_chip_set_chip(girq, &adp5588_irq_chip);
		girq->handler = handle_bad_irq;
		girq->threaded = true;
	}

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
		kpad->pull_dis[i] = adp5588_read(kpad->client, GPIO_PULL1 + i);
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
		int key = adp5588_read(kpad->client, KEY_EVENTA + i);
		int key_val = key & KEY_EV_MASK;
		int key_press = key & KEY_EV_PRESSED;

		if (key_val >= GPI_PIN_BASE && key_val <= GPI_PIN_END) {
			/* gpio line used as IRQ source */
			adp5588_gpio_irq_handle(kpad, key_val, key_press);
		} else {
			int row = (key_val - 1) / ADP5588_COLS_MAX;
			int col =  (key_val - 1) % ADP5588_COLS_MAX;
			int code = MATRIX_SCAN_CODE(row, col, kpad->row_shift);

			dev_dbg_ratelimited(&kpad->client->dev,
					    "report key(%d) r(%d) c(%d) code(%d)\n",
					    key_val, row, col, kpad->keycode[code]);

			input_report_key(kpad->input,
					 kpad->keycode[code], key_press);
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

static int adp5588_setup(struct adp5588_kpad *kpad)
{
	struct i2c_client *client = kpad->client;
	int i, ret;

	ret = adp5588_write(client, KP_GPIO1, KP_SEL(kpad->rows));
	if (ret)
		return ret;

	ret = adp5588_write(client, KP_GPIO2, KP_SEL(kpad->cols) & 0xFF);
	if (ret)
		return ret;

	ret = adp5588_write(client, KP_GPIO3, KP_SEL(kpad->cols) >> 8);
	if (ret)
		return ret;

	for (i = 0; i < kpad->nkeys_unlock; i++) {
		ret = adp5588_write(client, UNLOCK1 + i, kpad->unlock_keys[i]);
		if (ret)
			return ret;
	}

	if (kpad->nkeys_unlock) {
		ret = adp5588_write(client, KEY_LCK_EC_STAT, ADP5588_K_LCK_EN);
		if (ret)
			return ret;
	}

	for (i = 0; i < KEYP_MAX_EVENT; i++) {
		ret = adp5588_read(client, KEY_EVENTA);
		if (ret < 0)
			return ret;
	}

	ret = adp5588_write(client, INT_STAT,
			    ADP5588_CMP2_INT | ADP5588_CMP1_INT |
			    ADP5588_OVR_FLOW_INT | ADP5588_K_LCK_INT |
			    ADP5588_GPI_INT | ADP5588_KE_INT); /* Status is W1C */
	if (ret)
		return ret;

	return adp5588_write(client, CFG, ADP5588_INT_CFG |
			     ADP5588_OVR_FLOW_IEN | ADP5588_KE_IEN);
}

static int adp5588_fw_parse(struct adp5588_kpad *kpad)
{
	struct i2c_client *client = kpad->client;
	int ret, i;

	/*
	 * Check if the device is to be operated purely in GPIO mode. To do
	 * so, check that no keypad rows or columns have been specified,
	 * since all GPINS should be configured as GPIO.
	 */
	if (!device_property_present(&client->dev, "keypad,num-rows") &&
	    !device_property_present(&client->dev, "keypad,num-columns")) {
		/* If purely GPIO, skip keypad setup */
		kpad->gpio_only = true;
		return 0;
	}

	ret = matrix_keypad_parse_properties(&client->dev, &kpad->rows,
					     &kpad->cols);
	if (ret)
		return ret;

	if (kpad->rows > ADP5588_ROWS_MAX || kpad->cols > ADP5588_COLS_MAX) {
		dev_err(&client->dev, "Invalid nr of rows(%u) or cols(%u)\n",
			kpad->rows, kpad->cols);
		return -EINVAL;
	}

	ret = matrix_keypad_build_keymap(NULL, NULL, kpad->rows, kpad->cols,
					 kpad->keycode, kpad->input);
	if (ret)
		return ret;

	kpad->row_shift = get_count_order(kpad->cols);

	if (device_property_read_bool(&client->dev, "autorepeat"))
		__set_bit(EV_REP, kpad->input->evbit);

	kpad->nkeys_unlock = device_property_count_u32(&client->dev,
						       "adi,unlock-keys");
	if (kpad->nkeys_unlock <= 0) {
		/* so that we don't end up enabling key lock */
		kpad->nkeys_unlock = 0;
		return 0;
	}

	if (kpad->nkeys_unlock > ARRAY_SIZE(kpad->unlock_keys)) {
		dev_err(&client->dev, "number of unlock keys(%d) > (%zu)\n",
			kpad->nkeys_unlock, ARRAY_SIZE(kpad->unlock_keys));
		return -EINVAL;
	}

	ret = device_property_read_u32_array(&client->dev, "adi,unlock-keys",
					     kpad->unlock_keys,
					     kpad->nkeys_unlock);
	if (ret)
		return ret;

	for (i = 0; i < kpad->nkeys_unlock; i++) {
		/*
		 * Even though it should be possible (as stated in the datasheet)
		 * to use GPIs (which are part of the keys event) as unlock keys,
		 * it was not working at all and was leading to overflow events
		 * at some point. Hence, for now, let's just allow keys which are
		 * part of keypad matrix to be used and if a reliable way of
		 * using GPIs is found, this condition can be removed/lightened.
		 */
		if (kpad->unlock_keys[i] >= kpad->cols * kpad->rows) {
			dev_err(&client->dev, "Invalid unlock key(%d)\n",
				kpad->unlock_keys[i]);
			return -EINVAL;
		}

		/*
		 * Firmware properties keys start from 0 but on the device they
		 * start from 1.
		 */
		kpad->unlock_keys[i] += 1;
	}

	return 0;
}

static int adp5588_probe(struct i2c_client *client)
{
	struct adp5588_kpad *kpad;
	struct input_dev *input;
	struct gpio_desc *gpio;
	unsigned int revid;
	int ret;
	int error;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "SMBUS Byte Data not Supported\n");
		return -EIO;
	}

	kpad = devm_kzalloc(&client->dev, sizeof(*kpad), GFP_KERNEL);
	if (!kpad)
		return -ENOMEM;

	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;

	kpad->client = client;
	kpad->input = input;

	error = adp5588_fw_parse(kpad);
	if (error)
		return error;

	error = devm_regulator_get_enable(&client->dev, "vcc");
	if (error)
		return error;

	gpio = devm_gpiod_get_optional(&client->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);

	if (gpio) {
		fsleep(30);
		gpiod_set_value_cansleep(gpio, 0);
		fsleep(60);
	}

	ret = adp5588_read(client, DEV_ID);
	if (ret < 0)
		return ret;

	revid = ret & ADP5588_DEVICE_ID_MASK;
	if (WA_DELAYED_READOUT_REVID(revid))
		kpad->delay = msecs_to_jiffies(WA_DELAYED_READOUT_TIME);

	input->name = client->name;
	input->phys = "adp5588-keys/input0";

	input_set_drvdata(input, kpad);

	input->id.bustype = BUS_I2C;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = revid;

	error = input_register_device(input);
	if (error) {
		dev_err(&client->dev, "unable to register input device: %d\n",
			error);
		return error;
	}

	error = adp5588_setup(kpad);
	if (error)
		return error;

	error = adp5588_gpio_add(kpad);
	if (error)
		return error;

	if (client->irq) {
		error = devm_request_threaded_irq(&client->dev, client->irq,
						  adp5588_hard_irq, adp5588_thread_irq,
						  IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
						  client->dev.driver->name, kpad);
		if (error) {
			dev_err(&client->dev, "failed to request irq %d: %d\n",
				client->irq, error);
			return error;
		}
	}

	dev_info(&client->dev, "Rev.%d controller\n", revid);
	return 0;
}

static void adp5588_remove(struct i2c_client *client)
{
	adp5588_write(client, CFG, 0);

	/* all resources will be freed by devm */
}

static int adp5588_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	disable_irq(client->irq);

	return 0;
}

static int adp5588_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	enable_irq(client->irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(adp5588_dev_pm_ops, adp5588_suspend, adp5588_resume);

static const struct i2c_device_id adp5588_id[] = {
	{ "adp5588-keys" },
	{ "adp5587-keys" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adp5588_id);

static const struct of_device_id adp5588_of_match[] = {
	{ .compatible = "adi,adp5588" },
	{ .compatible = "adi,adp5587" },
	{}
};
MODULE_DEVICE_TABLE(of, adp5588_of_match);

static struct i2c_driver adp5588_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = adp5588_of_match,
		.pm   = pm_sleep_ptr(&adp5588_dev_pm_ops),
	},
	.probe    = adp5588_probe,
	.remove   = adp5588_remove,
	.id_table = adp5588_id,
};

module_i2c_driver(adp5588_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("ADP5588/87 Keypad driver");
