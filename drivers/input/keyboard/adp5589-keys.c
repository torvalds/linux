/*
 * Description:  keypad driver for ADP5589, ADP5585
 *		 I2C QWERTY Keypad and IO Expander
 * Bugs: Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2010-2011 Analog Devices Inc.
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <linux/errno.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/slab.h>

#include <linux/input/adp5589.h>

/* ADP5589/ADP5585 Common Registers */
#define ADP5589_5_ID			0x00
#define ADP5589_5_INT_STATUS		0x01
#define ADP5589_5_STATUS		0x02
#define ADP5589_5_FIFO_1		0x03
#define ADP5589_5_FIFO_2		0x04
#define ADP5589_5_FIFO_3		0x05
#define ADP5589_5_FIFO_4		0x06
#define ADP5589_5_FIFO_5		0x07
#define ADP5589_5_FIFO_6		0x08
#define ADP5589_5_FIFO_7		0x09
#define ADP5589_5_FIFO_8		0x0A
#define ADP5589_5_FIFO_9		0x0B
#define ADP5589_5_FIFO_10		0x0C
#define ADP5589_5_FIFO_11		0x0D
#define ADP5589_5_FIFO_12		0x0E
#define ADP5589_5_FIFO_13		0x0F
#define ADP5589_5_FIFO_14		0x10
#define ADP5589_5_FIFO_15		0x11
#define ADP5589_5_FIFO_16		0x12
#define ADP5589_5_GPI_INT_STAT_A	0x13
#define ADP5589_5_GPI_INT_STAT_B	0x14

/* ADP5589 Registers */
#define ADP5589_GPI_INT_STAT_C		0x15
#define ADP5589_GPI_STATUS_A		0x16
#define ADP5589_GPI_STATUS_B		0x17
#define ADP5589_GPI_STATUS_C		0x18
#define ADP5589_RPULL_CONFIG_A		0x19
#define ADP5589_RPULL_CONFIG_B		0x1A
#define ADP5589_RPULL_CONFIG_C		0x1B
#define ADP5589_RPULL_CONFIG_D		0x1C
#define ADP5589_RPULL_CONFIG_E		0x1D
#define ADP5589_GPI_INT_LEVEL_A		0x1E
#define ADP5589_GPI_INT_LEVEL_B		0x1F
#define ADP5589_GPI_INT_LEVEL_C		0x20
#define ADP5589_GPI_EVENT_EN_A		0x21
#define ADP5589_GPI_EVENT_EN_B		0x22
#define ADP5589_GPI_EVENT_EN_C		0x23
#define ADP5589_GPI_INTERRUPT_EN_A	0x24
#define ADP5589_GPI_INTERRUPT_EN_B	0x25
#define ADP5589_GPI_INTERRUPT_EN_C	0x26
#define ADP5589_DEBOUNCE_DIS_A		0x27
#define ADP5589_DEBOUNCE_DIS_B		0x28
#define ADP5589_DEBOUNCE_DIS_C		0x29
#define ADP5589_GPO_DATA_OUT_A		0x2A
#define ADP5589_GPO_DATA_OUT_B		0x2B
#define ADP5589_GPO_DATA_OUT_C		0x2C
#define ADP5589_GPO_OUT_MODE_A		0x2D
#define ADP5589_GPO_OUT_MODE_B		0x2E
#define ADP5589_GPO_OUT_MODE_C		0x2F
#define ADP5589_GPIO_DIRECTION_A	0x30
#define ADP5589_GPIO_DIRECTION_B	0x31
#define ADP5589_GPIO_DIRECTION_C	0x32
#define ADP5589_UNLOCK1			0x33
#define ADP5589_UNLOCK2			0x34
#define ADP5589_EXT_LOCK_EVENT		0x35
#define ADP5589_UNLOCK_TIMERS		0x36
#define ADP5589_LOCK_CFG		0x37
#define ADP5589_RESET1_EVENT_A		0x38
#define ADP5589_RESET1_EVENT_B		0x39
#define ADP5589_RESET1_EVENT_C		0x3A
#define ADP5589_RESET2_EVENT_A		0x3B
#define ADP5589_RESET2_EVENT_B		0x3C
#define ADP5589_RESET_CFG		0x3D
#define ADP5589_PWM_OFFT_LOW		0x3E
#define ADP5589_PWM_OFFT_HIGH		0x3F
#define ADP5589_PWM_ONT_LOW		0x40
#define ADP5589_PWM_ONT_HIGH		0x41
#define ADP5589_PWM_CFG			0x42
#define ADP5589_CLOCK_DIV_CFG		0x43
#define ADP5589_LOGIC_1_CFG		0x44
#define ADP5589_LOGIC_2_CFG		0x45
#define ADP5589_LOGIC_FF_CFG		0x46
#define ADP5589_LOGIC_INT_EVENT_EN	0x47
#define ADP5589_POLL_PTIME_CFG		0x48
#define ADP5589_PIN_CONFIG_A		0x49
#define ADP5589_PIN_CONFIG_B		0x4A
#define ADP5589_PIN_CONFIG_C		0x4B
#define ADP5589_PIN_CONFIG_D		0x4C
#define ADP5589_GENERAL_CFG		0x4D
#define ADP5589_INT_EN			0x4E

/* ADP5585 Registers */
#define ADP5585_GPI_STATUS_A		0x15
#define ADP5585_GPI_STATUS_B		0x16
#define ADP5585_RPULL_CONFIG_A		0x17
#define ADP5585_RPULL_CONFIG_B		0x18
#define ADP5585_RPULL_CONFIG_C		0x19
#define ADP5585_RPULL_CONFIG_D		0x1A
#define ADP5585_GPI_INT_LEVEL_A		0x1B
#define ADP5585_GPI_INT_LEVEL_B		0x1C
#define ADP5585_GPI_EVENT_EN_A		0x1D
#define ADP5585_GPI_EVENT_EN_B		0x1E
#define ADP5585_GPI_INTERRUPT_EN_A	0x1F
#define ADP5585_GPI_INTERRUPT_EN_B	0x20
#define ADP5585_DEBOUNCE_DIS_A		0x21
#define ADP5585_DEBOUNCE_DIS_B		0x22
#define ADP5585_GPO_DATA_OUT_A		0x23
#define ADP5585_GPO_DATA_OUT_B		0x24
#define ADP5585_GPO_OUT_MODE_A		0x25
#define ADP5585_GPO_OUT_MODE_B		0x26
#define ADP5585_GPIO_DIRECTION_A	0x27
#define ADP5585_GPIO_DIRECTION_B	0x28
#define ADP5585_RESET1_EVENT_A		0x29
#define ADP5585_RESET1_EVENT_B		0x2A
#define ADP5585_RESET1_EVENT_C		0x2B
#define ADP5585_RESET2_EVENT_A		0x2C
#define ADP5585_RESET2_EVENT_B		0x2D
#define ADP5585_RESET_CFG		0x2E
#define ADP5585_PWM_OFFT_LOW		0x2F
#define ADP5585_PWM_OFFT_HIGH		0x30
#define ADP5585_PWM_ONT_LOW		0x31
#define ADP5585_PWM_ONT_HIGH		0x32
#define ADP5585_PWM_CFG			0x33
#define ADP5585_LOGIC_CFG		0x34
#define ADP5585_LOGIC_FF_CFG		0x35
#define ADP5585_LOGIC_INT_EVENT_EN	0x36
#define ADP5585_POLL_PTIME_CFG		0x37
#define ADP5585_PIN_CONFIG_A		0x38
#define ADP5585_PIN_CONFIG_B		0x39
#define ADP5585_PIN_CONFIG_D		0x3A
#define ADP5585_GENERAL_CFG		0x3B
#define ADP5585_INT_EN			0x3C

/* ID Register */
#define ADP5589_5_DEVICE_ID_MASK	0xF
#define ADP5589_5_MAN_ID_MASK		0xF
#define ADP5589_5_MAN_ID_SHIFT		4
#define ADP5589_5_MAN_ID		0x02

/* GENERAL_CFG Register */
#define OSC_EN		(1 << 7)
#define CORE_CLK(x)	(((x) & 0x3) << 5)
#define LCK_TRK_LOGIC	(1 << 4)	/* ADP5589 only */
#define LCK_TRK_GPI	(1 << 3)	/* ADP5589 only */
#define INT_CFG		(1 << 1)
#define RST_CFG		(1 << 0)

/* INT_EN Register */
#define LOGIC2_IEN	(1 << 5)	/* ADP5589 only */
#define LOGIC1_IEN	(1 << 4)
#define LOCK_IEN	(1 << 3)	/* ADP5589 only */
#define OVRFLOW_IEN	(1 << 2)
#define GPI_IEN		(1 << 1)
#define EVENT_IEN	(1 << 0)

/* Interrupt Status Register */
#define LOGIC2_INT	(1 << 5)	/* ADP5589 only */
#define LOGIC1_INT	(1 << 4)
#define LOCK_INT	(1 << 3)	/* ADP5589 only */
#define OVRFLOW_INT	(1 << 2)
#define GPI_INT		(1 << 1)
#define EVENT_INT	(1 << 0)

/* STATUS Register */
#define LOGIC2_STAT	(1 << 7)	/* ADP5589 only */
#define LOGIC1_STAT	(1 << 6)
#define LOCK_STAT	(1 << 5)	/* ADP5589 only */
#define KEC		0x1F

/* PIN_CONFIG_D Register */
#define C4_EXTEND_CFG	(1 << 6)	/* RESET2 */
#define R4_EXTEND_CFG	(1 << 5)	/* RESET1 */

/* LOCK_CFG */
#define LOCK_EN		(1 << 0)

#define PTIME_MASK	0x3
#define LTIME_MASK	0x3		/* ADP5589 only */

/* Key Event Register xy */
#define KEY_EV_PRESSED		(1 << 7)
#define KEY_EV_MASK		(0x7F)

#define KEYP_MAX_EVENT		16
#define ADP5589_MAXGPIO		19
#define ADP5585_MAXGPIO		11 /* 10 on the ADP5585-01, 11 on ADP5585-02 */

enum {
	ADP5589,
	ADP5585_01,
	ADP5585_02
};

struct adp_constants {
	u8 maxgpio;
	u8 keymapsize;
	u8 gpi_pin_row_base;
	u8 gpi_pin_row_end;
	u8 gpi_pin_col_base;
	u8 gpi_pin_base;
	u8 gpi_pin_end;
	u8 gpimapsize_max;
	u8 max_row_num;
	u8 max_col_num;
	u8 row_mask;
	u8 col_mask;
	u8 col_shift;
	u8 c4_extend_cfg;
	u8 (*bank) (u8 offset);
	u8 (*bit) (u8 offset);
	u8 (*reg) (u8 reg);
};

struct adp5589_kpad {
	struct i2c_client *client;
	struct input_dev *input;
	const struct adp_constants *var;
	unsigned short keycode[ADP5589_KEYMAPSIZE];
	const struct adp5589_gpi_map *gpimap;
	unsigned short gpimapsize;
	unsigned extend_cfg;
	bool is_adp5585;
	bool support_row5;
#ifdef CONFIG_GPIOLIB
	unsigned char gpiomap[ADP5589_MAXGPIO];
	bool export_gpio;
	struct gpio_chip gc;
	struct mutex gpio_lock;	/* Protect cached dir, dat_out */
	u8 dat_out[3];
	u8 dir[3];
#endif
};

/*
 *  ADP5589 / ADP5585 derivative / variant handling
 */


/* ADP5589 */

static unsigned char adp5589_bank(unsigned char offset)
{
	return offset >> 3;
}

static unsigned char adp5589_bit(unsigned char offset)
{
	return 1u << (offset & 0x7);
}

static unsigned char adp5589_reg(unsigned char reg)
{
	return reg;
}

static const struct adp_constants const_adp5589 = {
	.maxgpio		= ADP5589_MAXGPIO,
	.keymapsize		= ADP5589_KEYMAPSIZE,
	.gpi_pin_row_base	= ADP5589_GPI_PIN_ROW_BASE,
	.gpi_pin_row_end	= ADP5589_GPI_PIN_ROW_END,
	.gpi_pin_col_base	= ADP5589_GPI_PIN_COL_BASE,
	.gpi_pin_base		= ADP5589_GPI_PIN_BASE,
	.gpi_pin_end		= ADP5589_GPI_PIN_END,
	.gpimapsize_max		= ADP5589_GPIMAPSIZE_MAX,
	.c4_extend_cfg		= 12,
	.max_row_num		= ADP5589_MAX_ROW_NUM,
	.max_col_num		= ADP5589_MAX_COL_NUM,
	.row_mask		= ADP5589_ROW_MASK,
	.col_mask		= ADP5589_COL_MASK,
	.col_shift		= ADP5589_COL_SHIFT,
	.bank			= adp5589_bank,
	.bit			= adp5589_bit,
	.reg			= adp5589_reg,
};

/* ADP5585 */

static unsigned char adp5585_bank(unsigned char offset)
{
	return offset > ADP5585_MAX_ROW_NUM;
}

static unsigned char adp5585_bit(unsigned char offset)
{
	return (offset > ADP5585_MAX_ROW_NUM) ?
		1u << (offset - ADP5585_COL_SHIFT) : 1u << offset;
}

static const unsigned char adp5585_reg_lut[] = {
	[ADP5589_GPI_STATUS_A]		= ADP5585_GPI_STATUS_A,
	[ADP5589_GPI_STATUS_B]		= ADP5585_GPI_STATUS_B,
	[ADP5589_RPULL_CONFIG_A]	= ADP5585_RPULL_CONFIG_A,
	[ADP5589_RPULL_CONFIG_B]	= ADP5585_RPULL_CONFIG_B,
	[ADP5589_RPULL_CONFIG_C]	= ADP5585_RPULL_CONFIG_C,
	[ADP5589_RPULL_CONFIG_D]	= ADP5585_RPULL_CONFIG_D,
	[ADP5589_GPI_INT_LEVEL_A]	= ADP5585_GPI_INT_LEVEL_A,
	[ADP5589_GPI_INT_LEVEL_B]	= ADP5585_GPI_INT_LEVEL_B,
	[ADP5589_GPI_EVENT_EN_A]	= ADP5585_GPI_EVENT_EN_A,
	[ADP5589_GPI_EVENT_EN_B]	= ADP5585_GPI_EVENT_EN_B,
	[ADP5589_GPI_INTERRUPT_EN_A]	= ADP5585_GPI_INTERRUPT_EN_A,
	[ADP5589_GPI_INTERRUPT_EN_B]	= ADP5585_GPI_INTERRUPT_EN_B,
	[ADP5589_DEBOUNCE_DIS_A]	= ADP5585_DEBOUNCE_DIS_A,
	[ADP5589_DEBOUNCE_DIS_B]	= ADP5585_DEBOUNCE_DIS_B,
	[ADP5589_GPO_DATA_OUT_A]	= ADP5585_GPO_DATA_OUT_A,
	[ADP5589_GPO_DATA_OUT_B]	= ADP5585_GPO_DATA_OUT_B,
	[ADP5589_GPO_OUT_MODE_A]	= ADP5585_GPO_OUT_MODE_A,
	[ADP5589_GPO_OUT_MODE_B]	= ADP5585_GPO_OUT_MODE_B,
	[ADP5589_GPIO_DIRECTION_A]	= ADP5585_GPIO_DIRECTION_A,
	[ADP5589_GPIO_DIRECTION_B]	= ADP5585_GPIO_DIRECTION_B,
	[ADP5589_RESET1_EVENT_A]	= ADP5585_RESET1_EVENT_A,
	[ADP5589_RESET1_EVENT_B]	= ADP5585_RESET1_EVENT_B,
	[ADP5589_RESET1_EVENT_C]	= ADP5585_RESET1_EVENT_C,
	[ADP5589_RESET2_EVENT_A]	= ADP5585_RESET2_EVENT_A,
	[ADP5589_RESET2_EVENT_B]	= ADP5585_RESET2_EVENT_B,
	[ADP5589_RESET_CFG]		= ADP5585_RESET_CFG,
	[ADP5589_PWM_OFFT_LOW]		= ADP5585_PWM_OFFT_LOW,
	[ADP5589_PWM_OFFT_HIGH]		= ADP5585_PWM_OFFT_HIGH,
	[ADP5589_PWM_ONT_LOW]		= ADP5585_PWM_ONT_LOW,
	[ADP5589_PWM_ONT_HIGH]		= ADP5585_PWM_ONT_HIGH,
	[ADP5589_PWM_CFG]		= ADP5585_PWM_CFG,
	[ADP5589_LOGIC_1_CFG]		= ADP5585_LOGIC_CFG,
	[ADP5589_LOGIC_FF_CFG]		= ADP5585_LOGIC_FF_CFG,
	[ADP5589_LOGIC_INT_EVENT_EN]	= ADP5585_LOGIC_INT_EVENT_EN,
	[ADP5589_POLL_PTIME_CFG]	= ADP5585_POLL_PTIME_CFG,
	[ADP5589_PIN_CONFIG_A]		= ADP5585_PIN_CONFIG_A,
	[ADP5589_PIN_CONFIG_B]		= ADP5585_PIN_CONFIG_B,
	[ADP5589_PIN_CONFIG_D]		= ADP5585_PIN_CONFIG_D,
	[ADP5589_GENERAL_CFG]		= ADP5585_GENERAL_CFG,
	[ADP5589_INT_EN]		= ADP5585_INT_EN,
};

static unsigned char adp5585_reg(unsigned char reg)
{
	return adp5585_reg_lut[reg];
}

static const struct adp_constants const_adp5585 = {
	.maxgpio		= ADP5585_MAXGPIO,
	.keymapsize		= ADP5585_KEYMAPSIZE,
	.gpi_pin_row_base	= ADP5585_GPI_PIN_ROW_BASE,
	.gpi_pin_row_end	= ADP5585_GPI_PIN_ROW_END,
	.gpi_pin_col_base	= ADP5585_GPI_PIN_COL_BASE,
	.gpi_pin_base		= ADP5585_GPI_PIN_BASE,
	.gpi_pin_end		= ADP5585_GPI_PIN_END,
	.gpimapsize_max		= ADP5585_GPIMAPSIZE_MAX,
	.c4_extend_cfg		= 10,
	.max_row_num		= ADP5585_MAX_ROW_NUM,
	.max_col_num		= ADP5585_MAX_COL_NUM,
	.row_mask		= ADP5585_ROW_MASK,
	.col_mask		= ADP5585_COL_MASK,
	.col_shift		= ADP5585_COL_SHIFT,
	.bank			= adp5585_bank,
	.bit			= adp5585_bit,
	.reg			= adp5585_reg,
};

static int adp5589_read(struct i2c_client *client, u8 reg)
{
	int ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0)
		dev_err(&client->dev, "Read Error\n");

	return ret;
}

static int adp5589_write(struct i2c_client *client, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(client, reg, val);
}

#ifdef CONFIG_GPIOLIB
static int adp5589_gpio_get_value(struct gpio_chip *chip, unsigned off)
{
	struct adp5589_kpad *kpad = gpiochip_get_data(chip);
	unsigned int bank = kpad->var->bank(kpad->gpiomap[off]);
	unsigned int bit = kpad->var->bit(kpad->gpiomap[off]);

	return !!(adp5589_read(kpad->client,
			       kpad->var->reg(ADP5589_GPI_STATUS_A) + bank) &
			       bit);
}

static void adp5589_gpio_set_value(struct gpio_chip *chip,
				   unsigned off, int val)
{
	struct adp5589_kpad *kpad = gpiochip_get_data(chip);
	unsigned int bank = kpad->var->bank(kpad->gpiomap[off]);
	unsigned int bit = kpad->var->bit(kpad->gpiomap[off]);

	mutex_lock(&kpad->gpio_lock);

	if (val)
		kpad->dat_out[bank] |= bit;
	else
		kpad->dat_out[bank] &= ~bit;

	adp5589_write(kpad->client, kpad->var->reg(ADP5589_GPO_DATA_OUT_A) +
		      bank, kpad->dat_out[bank]);

	mutex_unlock(&kpad->gpio_lock);
}

static int adp5589_gpio_direction_input(struct gpio_chip *chip, unsigned off)
{
	struct adp5589_kpad *kpad = gpiochip_get_data(chip);
	unsigned int bank = kpad->var->bank(kpad->gpiomap[off]);
	unsigned int bit = kpad->var->bit(kpad->gpiomap[off]);
	int ret;

	mutex_lock(&kpad->gpio_lock);

	kpad->dir[bank] &= ~bit;
	ret = adp5589_write(kpad->client,
			    kpad->var->reg(ADP5589_GPIO_DIRECTION_A) + bank,
			    kpad->dir[bank]);

	mutex_unlock(&kpad->gpio_lock);

	return ret;
}

static int adp5589_gpio_direction_output(struct gpio_chip *chip,
					 unsigned off, int val)
{
	struct adp5589_kpad *kpad = gpiochip_get_data(chip);
	unsigned int bank = kpad->var->bank(kpad->gpiomap[off]);
	unsigned int bit = kpad->var->bit(kpad->gpiomap[off]);
	int ret;

	mutex_lock(&kpad->gpio_lock);

	kpad->dir[bank] |= bit;

	if (val)
		kpad->dat_out[bank] |= bit;
	else
		kpad->dat_out[bank] &= ~bit;

	ret = adp5589_write(kpad->client, kpad->var->reg(ADP5589_GPO_DATA_OUT_A)
			    + bank, kpad->dat_out[bank]);
	ret |= adp5589_write(kpad->client,
			     kpad->var->reg(ADP5589_GPIO_DIRECTION_A) + bank,
			     kpad->dir[bank]);

	mutex_unlock(&kpad->gpio_lock);

	return ret;
}

static int adp5589_build_gpiomap(struct adp5589_kpad *kpad,
				const struct adp5589_kpad_platform_data *pdata)
{
	bool pin_used[ADP5589_MAXGPIO];
	int n_unused = 0;
	int i;

	memset(pin_used, false, sizeof(pin_used));

	for (i = 0; i < kpad->var->maxgpio; i++)
		if (pdata->keypad_en_mask & (1 << i))
			pin_used[i] = true;

	for (i = 0; i < kpad->gpimapsize; i++)
		pin_used[kpad->gpimap[i].pin - kpad->var->gpi_pin_base] = true;

	if (kpad->extend_cfg & R4_EXTEND_CFG)
		pin_used[4] = true;

	if (kpad->extend_cfg & C4_EXTEND_CFG)
		pin_used[kpad->var->c4_extend_cfg] = true;

	if (!kpad->support_row5)
		pin_used[5] = true;

	for (i = 0; i < kpad->var->maxgpio; i++)
		if (!pin_used[i])
			kpad->gpiomap[n_unused++] = i;

	return n_unused;
}

static int adp5589_gpio_add(struct adp5589_kpad *kpad)
{
	struct device *dev = &kpad->client->dev;
	const struct adp5589_kpad_platform_data *pdata = dev_get_platdata(dev);
	const struct adp5589_gpio_platform_data *gpio_data = pdata->gpio_data;
	int i, error;

	if (!gpio_data)
		return 0;

	kpad->gc.ngpio = adp5589_build_gpiomap(kpad, pdata);
	if (kpad->gc.ngpio == 0) {
		dev_info(dev, "No unused gpios left to export\n");
		return 0;
	}

	kpad->export_gpio = true;

	kpad->gc.direction_input = adp5589_gpio_direction_input;
	kpad->gc.direction_output = adp5589_gpio_direction_output;
	kpad->gc.get = adp5589_gpio_get_value;
	kpad->gc.set = adp5589_gpio_set_value;
	kpad->gc.can_sleep = 1;

	kpad->gc.base = gpio_data->gpio_start;
	kpad->gc.label = kpad->client->name;
	kpad->gc.owner = THIS_MODULE;

	mutex_init(&kpad->gpio_lock);

	error = gpiochip_add_data(&kpad->gc, kpad);
	if (error) {
		dev_err(dev, "gpiochip_add_data() failed, err: %d\n", error);
		return error;
	}

	for (i = 0; i <= kpad->var->bank(kpad->var->maxgpio); i++) {
		kpad->dat_out[i] = adp5589_read(kpad->client, kpad->var->reg(
						ADP5589_GPO_DATA_OUT_A) + i);
		kpad->dir[i] = adp5589_read(kpad->client, kpad->var->reg(
					    ADP5589_GPIO_DIRECTION_A) + i);
	}

	if (gpio_data->setup) {
		error = gpio_data->setup(kpad->client,
					 kpad->gc.base, kpad->gc.ngpio,
					 gpio_data->context);
		if (error)
			dev_warn(dev, "setup failed, %d\n", error);
	}

	return 0;
}

static void adp5589_gpio_remove(struct adp5589_kpad *kpad)
{
	struct device *dev = &kpad->client->dev;
	const struct adp5589_kpad_platform_data *pdata = dev_get_platdata(dev);
	const struct adp5589_gpio_platform_data *gpio_data = pdata->gpio_data;
	int error;

	if (!kpad->export_gpio)
		return;

	if (gpio_data->teardown) {
		error = gpio_data->teardown(kpad->client,
					    kpad->gc.base, kpad->gc.ngpio,
					    gpio_data->context);
		if (error)
			dev_warn(dev, "teardown failed %d\n", error);
	}

	gpiochip_remove(&kpad->gc);
}
#else
static inline int adp5589_gpio_add(struct adp5589_kpad *kpad)
{
	return 0;
}

static inline void adp5589_gpio_remove(struct adp5589_kpad *kpad)
{
}
#endif

static void adp5589_report_switches(struct adp5589_kpad *kpad,
				    int key, int key_val)
{
	int i;

	for (i = 0; i < kpad->gpimapsize; i++) {
		if (key_val == kpad->gpimap[i].pin) {
			input_report_switch(kpad->input,
					    kpad->gpimap[i].sw_evt,
					    key & KEY_EV_PRESSED);
			break;
		}
	}
}

static void adp5589_report_events(struct adp5589_kpad *kpad, int ev_cnt)
{
	int i;

	for (i = 0; i < ev_cnt; i++) {
		int key = adp5589_read(kpad->client, ADP5589_5_FIFO_1 + i);
		int key_val = key & KEY_EV_MASK;

		if (key_val >= kpad->var->gpi_pin_base &&
		    key_val <= kpad->var->gpi_pin_end) {
			adp5589_report_switches(kpad, key, key_val);
		} else {
			input_report_key(kpad->input,
					 kpad->keycode[key_val - 1],
					 key & KEY_EV_PRESSED);
		}
	}
}

static irqreturn_t adp5589_irq(int irq, void *handle)
{
	struct adp5589_kpad *kpad = handle;
	struct i2c_client *client = kpad->client;
	int status, ev_cnt;

	status = adp5589_read(client, ADP5589_5_INT_STATUS);

	if (status & OVRFLOW_INT)	/* Unlikely and should never happen */
		dev_err(&client->dev, "Event Overflow Error\n");

	if (status & EVENT_INT) {
		ev_cnt = adp5589_read(client, ADP5589_5_STATUS) & KEC;
		if (ev_cnt) {
			adp5589_report_events(kpad, ev_cnt);
			input_sync(kpad->input);
		}
	}

	adp5589_write(client, ADP5589_5_INT_STATUS, status); /* Status is W1C */

	return IRQ_HANDLED;
}

static int adp5589_get_evcode(struct adp5589_kpad *kpad, unsigned short key)
{
	int i;

	for (i = 0; i < kpad->var->keymapsize; i++)
		if (key == kpad->keycode[i])
			return (i + 1) | KEY_EV_PRESSED;

	dev_err(&kpad->client->dev, "RESET/UNLOCK key not in keycode map\n");

	return -EINVAL;
}

static int adp5589_setup(struct adp5589_kpad *kpad)
{
	struct i2c_client *client = kpad->client;
	const struct adp5589_kpad_platform_data *pdata =
		dev_get_platdata(&client->dev);
	u8 (*reg) (u8) = kpad->var->reg;
	unsigned char evt_mode1 = 0, evt_mode2 = 0, evt_mode3 = 0;
	unsigned char pull_mask = 0;
	int i, ret;

	ret = adp5589_write(client, reg(ADP5589_PIN_CONFIG_A),
			    pdata->keypad_en_mask & kpad->var->row_mask);
	ret |= adp5589_write(client, reg(ADP5589_PIN_CONFIG_B),
			     (pdata->keypad_en_mask >> kpad->var->col_shift) &
			     kpad->var->col_mask);

	if (!kpad->is_adp5585)
		ret |= adp5589_write(client, ADP5589_PIN_CONFIG_C,
				     (pdata->keypad_en_mask >> 16) & 0xFF);

	if (!kpad->is_adp5585 && pdata->en_keylock) {
		ret |= adp5589_write(client, ADP5589_UNLOCK1,
				     pdata->unlock_key1);
		ret |= adp5589_write(client, ADP5589_UNLOCK2,
				     pdata->unlock_key2);
		ret |= adp5589_write(client, ADP5589_UNLOCK_TIMERS,
				     pdata->unlock_timer & LTIME_MASK);
		ret |= adp5589_write(client, ADP5589_LOCK_CFG, LOCK_EN);
	}

	for (i = 0; i < KEYP_MAX_EVENT; i++)
		ret |= adp5589_read(client, ADP5589_5_FIFO_1 + i);

	for (i = 0; i < pdata->gpimapsize; i++) {
		unsigned short pin = pdata->gpimap[i].pin;

		if (pin <= kpad->var->gpi_pin_row_end) {
			evt_mode1 |= (1 << (pin - kpad->var->gpi_pin_row_base));
		} else {
			evt_mode2 |=
			    ((1 << (pin - kpad->var->gpi_pin_col_base)) & 0xFF);
			if (!kpad->is_adp5585)
				evt_mode3 |= ((1 << (pin -
					kpad->var->gpi_pin_col_base)) >> 8);
		}
	}

	if (pdata->gpimapsize) {
		ret |= adp5589_write(client, reg(ADP5589_GPI_EVENT_EN_A),
				     evt_mode1);
		ret |= adp5589_write(client, reg(ADP5589_GPI_EVENT_EN_B),
				     evt_mode2);
		if (!kpad->is_adp5585)
			ret |= adp5589_write(client,
					     reg(ADP5589_GPI_EVENT_EN_C),
					     evt_mode3);
	}

	if (pdata->pull_dis_mask & pdata->pullup_en_100k &
		pdata->pullup_en_300k & pdata->pulldown_en_300k)
		dev_warn(&client->dev, "Conflicting pull resistor config\n");

	for (i = 0; i <= kpad->var->max_row_num; i++) {
		unsigned val = 0, bit = (1 << i);
		if (pdata->pullup_en_300k & bit)
			val = 0;
		else if (pdata->pulldown_en_300k & bit)
			val = 1;
		else if (pdata->pullup_en_100k & bit)
			val = 2;
		else if (pdata->pull_dis_mask & bit)
			val = 3;

		pull_mask |= val << (2 * (i & 0x3));

		if (i % 4 == 3 || i == kpad->var->max_row_num) {
			ret |= adp5589_write(client, reg(ADP5585_RPULL_CONFIG_A)
					     + (i >> 2), pull_mask);
			pull_mask = 0;
		}
	}

	for (i = 0; i <= kpad->var->max_col_num; i++) {
		unsigned val = 0, bit = 1 << (i + kpad->var->col_shift);
		if (pdata->pullup_en_300k & bit)
			val = 0;
		else if (pdata->pulldown_en_300k & bit)
			val = 1;
		else if (pdata->pullup_en_100k & bit)
			val = 2;
		else if (pdata->pull_dis_mask & bit)
			val = 3;

		pull_mask |= val << (2 * (i & 0x3));

		if (i % 4 == 3 || i == kpad->var->max_col_num) {
			ret |= adp5589_write(client,
					     reg(ADP5585_RPULL_CONFIG_C) +
					     (i >> 2), pull_mask);
			pull_mask = 0;
		}
	}

	if (pdata->reset1_key_1 && pdata->reset1_key_2 && pdata->reset1_key_3) {
		ret |= adp5589_write(client, reg(ADP5589_RESET1_EVENT_A),
				     adp5589_get_evcode(kpad,
							pdata->reset1_key_1));
		ret |= adp5589_write(client, reg(ADP5589_RESET1_EVENT_B),
				     adp5589_get_evcode(kpad,
							pdata->reset1_key_2));
		ret |= adp5589_write(client, reg(ADP5589_RESET1_EVENT_C),
				     adp5589_get_evcode(kpad,
							pdata->reset1_key_3));
		kpad->extend_cfg |= R4_EXTEND_CFG;
	}

	if (pdata->reset2_key_1 && pdata->reset2_key_2) {
		ret |= adp5589_write(client, reg(ADP5589_RESET2_EVENT_A),
				     adp5589_get_evcode(kpad,
							pdata->reset2_key_1));
		ret |= adp5589_write(client, reg(ADP5589_RESET2_EVENT_B),
				     adp5589_get_evcode(kpad,
							pdata->reset2_key_2));
		kpad->extend_cfg |= C4_EXTEND_CFG;
	}

	if (kpad->extend_cfg) {
		ret |= adp5589_write(client, reg(ADP5589_RESET_CFG),
				     pdata->reset_cfg);
		ret |= adp5589_write(client, reg(ADP5589_PIN_CONFIG_D),
				     kpad->extend_cfg);
	}

	ret |= adp5589_write(client, reg(ADP5589_DEBOUNCE_DIS_A),
			    pdata->debounce_dis_mask & kpad->var->row_mask);

	ret |= adp5589_write(client, reg(ADP5589_DEBOUNCE_DIS_B),
			     (pdata->debounce_dis_mask >> kpad->var->col_shift)
			     & kpad->var->col_mask);

	if (!kpad->is_adp5585)
		ret |= adp5589_write(client, reg(ADP5589_DEBOUNCE_DIS_C),
				     (pdata->debounce_dis_mask >> 16) & 0xFF);

	ret |= adp5589_write(client, reg(ADP5589_POLL_PTIME_CFG),
			     pdata->scan_cycle_time & PTIME_MASK);
	ret |= adp5589_write(client, ADP5589_5_INT_STATUS,
			     (kpad->is_adp5585 ? 0 : LOGIC2_INT) |
			     LOGIC1_INT | OVRFLOW_INT |
			     (kpad->is_adp5585 ? 0 : LOCK_INT) |
			     GPI_INT | EVENT_INT);	/* Status is W1C */

	ret |= adp5589_write(client, reg(ADP5589_GENERAL_CFG),
			     INT_CFG | OSC_EN | CORE_CLK(3));
	ret |= adp5589_write(client, reg(ADP5589_INT_EN),
			     OVRFLOW_IEN | GPI_IEN | EVENT_IEN);

	if (ret < 0) {
		dev_err(&client->dev, "Write Error\n");
		return ret;
	}

	return 0;
}

static void adp5589_report_switch_state(struct adp5589_kpad *kpad)
{
	int gpi_stat_tmp, pin_loc;
	int i;
	int gpi_stat1 = adp5589_read(kpad->client,
				     kpad->var->reg(ADP5589_GPI_STATUS_A));
	int gpi_stat2 = adp5589_read(kpad->client,
				     kpad->var->reg(ADP5589_GPI_STATUS_B));
	int gpi_stat3 = !kpad->is_adp5585 ?
			adp5589_read(kpad->client, ADP5589_GPI_STATUS_C) : 0;

	for (i = 0; i < kpad->gpimapsize; i++) {
		unsigned short pin = kpad->gpimap[i].pin;

		if (pin <= kpad->var->gpi_pin_row_end) {
			gpi_stat_tmp = gpi_stat1;
			pin_loc = pin - kpad->var->gpi_pin_row_base;
		} else if ((pin - kpad->var->gpi_pin_col_base) < 8) {
			gpi_stat_tmp = gpi_stat2;
			pin_loc = pin - kpad->var->gpi_pin_col_base;
		} else {
			gpi_stat_tmp = gpi_stat3;
			pin_loc = pin - kpad->var->gpi_pin_col_base - 8;
		}

		if (gpi_stat_tmp < 0) {
			dev_err(&kpad->client->dev,
				"Can't read GPIO_DAT_STAT switch %d, default to OFF\n",
				pin);
			gpi_stat_tmp = 0;
		}

		input_report_switch(kpad->input,
				    kpad->gpimap[i].sw_evt,
				    !(gpi_stat_tmp & (1 << pin_loc)));
	}

	input_sync(kpad->input);
}

static int adp5589_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct adp5589_kpad *kpad;
	const struct adp5589_kpad_platform_data *pdata =
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

	kpad = kzalloc(sizeof(*kpad), GFP_KERNEL);
	if (!kpad)
		return -ENOMEM;

	switch (id->driver_data) {
	case ADP5585_02:
		kpad->support_row5 = true;
	case ADP5585_01:
		kpad->is_adp5585 = true;
		kpad->var = &const_adp5585;
		break;
	case ADP5589:
		kpad->support_row5 = true;
		kpad->var = &const_adp5589;
		break;
	}

	if (!((pdata->keypad_en_mask & kpad->var->row_mask) &&
			(pdata->keypad_en_mask >> kpad->var->col_shift)) ||
			!pdata->keymap) {
		dev_err(&client->dev, "no rows, cols or keymap from pdata\n");
		error = -EINVAL;
		goto err_free_mem;
	}

	if (pdata->keymapsize != kpad->var->keymapsize) {
		dev_err(&client->dev, "invalid keymapsize\n");
		error = -EINVAL;
		goto err_free_mem;
	}

	if (!pdata->gpimap && pdata->gpimapsize) {
		dev_err(&client->dev, "invalid gpimap from pdata\n");
		error = -EINVAL;
		goto err_free_mem;
	}

	if (pdata->gpimapsize > kpad->var->gpimapsize_max) {
		dev_err(&client->dev, "invalid gpimapsize\n");
		error = -EINVAL;
		goto err_free_mem;
	}

	for (i = 0; i < pdata->gpimapsize; i++) {
		unsigned short pin = pdata->gpimap[i].pin;

		if (pin < kpad->var->gpi_pin_base ||
				pin > kpad->var->gpi_pin_end) {
			dev_err(&client->dev, "invalid gpi pin data\n");
			error = -EINVAL;
			goto err_free_mem;
		}

		if ((1 << (pin - kpad->var->gpi_pin_row_base)) &
				pdata->keypad_en_mask) {
			dev_err(&client->dev, "invalid gpi row/col data\n");
			error = -EINVAL;
			goto err_free_mem;
		}
	}

	if (!client->irq) {
		dev_err(&client->dev, "no IRQ?\n");
		error = -EINVAL;
		goto err_free_mem;
	}

	input = input_allocate_device();
	if (!input) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	kpad->client = client;
	kpad->input = input;

	ret = adp5589_read(client, ADP5589_5_ID);
	if (ret < 0) {
		error = ret;
		goto err_free_input;
	}

	revid = (u8) ret & ADP5589_5_DEVICE_ID_MASK;

	input->name = client->name;
	input->phys = "adp5589-keys/input0";
	input->dev.parent = &client->dev;

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
		dev_err(&client->dev, "unable to register input device\n");
		goto err_free_input;
	}

	error = request_threaded_irq(client->irq, NULL, adp5589_irq,
				     IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				     client->dev.driver->name, kpad);
	if (error) {
		dev_err(&client->dev, "irq %d busy?\n", client->irq);
		goto err_unreg_dev;
	}

	error = adp5589_setup(kpad);
	if (error)
		goto err_free_irq;

	if (kpad->gpimapsize)
		adp5589_report_switch_state(kpad);

	error = adp5589_gpio_add(kpad);
	if (error)
		goto err_free_irq;

	device_init_wakeup(&client->dev, 1);
	i2c_set_clientdata(client, kpad);

	dev_info(&client->dev, "Rev.%d keypad, irq %d\n", revid, client->irq);
	return 0;

err_free_irq:
	free_irq(client->irq, kpad);
err_unreg_dev:
	input_unregister_device(input);
	input = NULL;
err_free_input:
	input_free_device(input);
err_free_mem:
	kfree(kpad);

	return error;
}

static int adp5589_remove(struct i2c_client *client)
{
	struct adp5589_kpad *kpad = i2c_get_clientdata(client);

	adp5589_write(client, kpad->var->reg(ADP5589_GENERAL_CFG), 0);
	free_irq(client->irq, kpad);
	input_unregister_device(kpad->input);
	adp5589_gpio_remove(kpad);
	kfree(kpad);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int adp5589_suspend(struct device *dev)
{
	struct adp5589_kpad *kpad = dev_get_drvdata(dev);
	struct i2c_client *client = kpad->client;

	disable_irq(client->irq);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);

	return 0;
}

static int adp5589_resume(struct device *dev)
{
	struct adp5589_kpad *kpad = dev_get_drvdata(dev);
	struct i2c_client *client = kpad->client;

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);

	enable_irq(client->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(adp5589_dev_pm_ops, adp5589_suspend, adp5589_resume);

static const struct i2c_device_id adp5589_id[] = {
	{"adp5589-keys", ADP5589},
	{"adp5585-keys", ADP5585_01},
	{"adp5585-02-keys", ADP5585_02}, /* Adds ROW5 to ADP5585 */
	{}
};

MODULE_DEVICE_TABLE(i2c, adp5589_id);

static struct i2c_driver adp5589_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.pm = &adp5589_dev_pm_ops,
	},
	.probe = adp5589_probe,
	.remove = adp5589_remove,
	.id_table = adp5589_id,
};

module_i2c_driver(adp5589_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("ADP5589/ADP5585 Keypad driver");
