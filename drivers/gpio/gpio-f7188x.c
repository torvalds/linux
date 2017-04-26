/*
 * GPIO driver for Fintek Super-I/O F71869, F71869A, F71882, F71889 and F81866
 *
 * Copyright (C) 2010-2013 LaCie
 *
 * Author: Simon Guinot <simon.guinot@sequanux.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio/driver.h>
#include <linux/bitops.h>

#define DRVNAME "gpio-f7188x"

/*
 * Super-I/O registers
 */
#define SIO_LDSEL		0x07	/* Logical device select */
#define SIO_DEVID		0x20	/* Device ID (2 bytes) */
#define SIO_DEVREV		0x22	/* Device revision */
#define SIO_MANID		0x23	/* Fintek ID (2 bytes) */

#define SIO_LD_GPIO		0x06	/* GPIO logical device */
#define SIO_UNLOCK_KEY		0x87	/* Key to enable Super-I/O */
#define SIO_LOCK_KEY		0xAA	/* Key to disable Super-I/O */

#define SIO_FINTEK_ID		0x1934	/* Manufacturer ID */
#define SIO_F71869_ID		0x0814	/* F71869 chipset ID */
#define SIO_F71869A_ID		0x1007	/* F71869A chipset ID */
#define SIO_F71882_ID		0x0541	/* F71882 chipset ID */
#define SIO_F71889_ID		0x0909	/* F71889 chipset ID */
#define SIO_F71889A_ID		0x1005	/* F71889A chipset ID */
#define SIO_F81866_ID		0x1010	/* F81866 chipset ID */

enum chips { f71869, f71869a, f71882fg, f71889a, f71889f, f81866 };

static const char * const f7188x_names[] = {
	"f71869",
	"f71869a",
	"f71882fg",
	"f71889a",
	"f71889f",
	"f81866",
};

struct f7188x_sio {
	int addr;
	enum chips type;
};

struct f7188x_gpio_bank {
	struct gpio_chip chip;
	unsigned int regbase;
	struct f7188x_gpio_data *data;
};

struct f7188x_gpio_data {
	struct f7188x_sio *sio;
	int nr_bank;
	struct f7188x_gpio_bank *bank;
};

/*
 * Super-I/O functions.
 */

static inline int superio_inb(int base, int reg)
{
	outb(reg, base);
	return inb(base + 1);
}

static int superio_inw(int base, int reg)
{
	int val;

	outb(reg++, base);
	val = inb(base + 1) << 8;
	outb(reg, base);
	val |= inb(base + 1);

	return val;
}

static inline void superio_outb(int base, int reg, int val)
{
	outb(reg, base);
	outb(val, base + 1);
}

static inline int superio_enter(int base)
{
	/* Don't step on other drivers' I/O space by accident. */
	if (!request_muxed_region(base, 2, DRVNAME)) {
		pr_err(DRVNAME "I/O address 0x%04x already in use\n", base);
		return -EBUSY;
	}

	/* According to the datasheet the key must be send twice. */
	outb(SIO_UNLOCK_KEY, base);
	outb(SIO_UNLOCK_KEY, base);

	return 0;
}

static inline void superio_select(int base, int ld)
{
	outb(SIO_LDSEL, base);
	outb(ld, base + 1);
}

static inline void superio_exit(int base)
{
	outb(SIO_LOCK_KEY, base);
	release_region(base, 2);
}

/*
 * GPIO chip.
 */

static int f7188x_gpio_get_direction(struct gpio_chip *chip, unsigned offset);
static int f7188x_gpio_direction_in(struct gpio_chip *chip, unsigned offset);
static int f7188x_gpio_get(struct gpio_chip *chip, unsigned offset);
static int f7188x_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value);
static void f7188x_gpio_set(struct gpio_chip *chip, unsigned offset, int value);
static int f7188x_gpio_set_config(struct gpio_chip *chip, unsigned offset,
				  unsigned long config);

#define F7188X_GPIO_BANK(_base, _ngpio, _regbase)			\
	{								\
		.chip = {						\
			.label            = DRVNAME,			\
			.owner            = THIS_MODULE,		\
			.get_direction    = f7188x_gpio_get_direction,	\
			.direction_input  = f7188x_gpio_direction_in,	\
			.get              = f7188x_gpio_get,		\
			.direction_output = f7188x_gpio_direction_out,	\
			.set              = f7188x_gpio_set,		\
			.set_config	  = f7188x_gpio_set_config,	\
			.base             = _base,			\
			.ngpio            = _ngpio,			\
			.can_sleep        = true,			\
		},							\
		.regbase = _regbase,					\
	}

#define gpio_dir(base) (base + 0)
#define gpio_data_out(base) (base + 1)
#define gpio_data_in(base) (base + 2)
/* Output mode register (0:open drain 1:push-pull). */
#define gpio_out_mode(base) (base + 3)

static struct f7188x_gpio_bank f71869_gpio_bank[] = {
	F7188X_GPIO_BANK(0, 6, 0xF0),
	F7188X_GPIO_BANK(10, 8, 0xE0),
	F7188X_GPIO_BANK(20, 8, 0xD0),
	F7188X_GPIO_BANK(30, 8, 0xC0),
	F7188X_GPIO_BANK(40, 8, 0xB0),
	F7188X_GPIO_BANK(50, 5, 0xA0),
	F7188X_GPIO_BANK(60, 6, 0x90),
};

static struct f7188x_gpio_bank f71869a_gpio_bank[] = {
	F7188X_GPIO_BANK(0, 6, 0xF0),
	F7188X_GPIO_BANK(10, 8, 0xE0),
	F7188X_GPIO_BANK(20, 8, 0xD0),
	F7188X_GPIO_BANK(30, 8, 0xC0),
	F7188X_GPIO_BANK(40, 8, 0xB0),
	F7188X_GPIO_BANK(50, 5, 0xA0),
	F7188X_GPIO_BANK(60, 8, 0x90),
	F7188X_GPIO_BANK(70, 8, 0x80),
};

static struct f7188x_gpio_bank f71882_gpio_bank[] = {
	F7188X_GPIO_BANK(0, 8, 0xF0),
	F7188X_GPIO_BANK(10, 8, 0xE0),
	F7188X_GPIO_BANK(20, 8, 0xD0),
	F7188X_GPIO_BANK(30, 4, 0xC0),
	F7188X_GPIO_BANK(40, 4, 0xB0),
};

static struct f7188x_gpio_bank f71889a_gpio_bank[] = {
	F7188X_GPIO_BANK(0, 7, 0xF0),
	F7188X_GPIO_BANK(10, 7, 0xE0),
	F7188X_GPIO_BANK(20, 8, 0xD0),
	F7188X_GPIO_BANK(30, 8, 0xC0),
	F7188X_GPIO_BANK(40, 8, 0xB0),
	F7188X_GPIO_BANK(50, 5, 0xA0),
	F7188X_GPIO_BANK(60, 8, 0x90),
	F7188X_GPIO_BANK(70, 8, 0x80),
};

static struct f7188x_gpio_bank f71889_gpio_bank[] = {
	F7188X_GPIO_BANK(0, 7, 0xF0),
	F7188X_GPIO_BANK(10, 7, 0xE0),
	F7188X_GPIO_BANK(20, 8, 0xD0),
	F7188X_GPIO_BANK(30, 8, 0xC0),
	F7188X_GPIO_BANK(40, 8, 0xB0),
	F7188X_GPIO_BANK(50, 5, 0xA0),
	F7188X_GPIO_BANK(60, 8, 0x90),
	F7188X_GPIO_BANK(70, 8, 0x80),
};

static struct f7188x_gpio_bank f81866_gpio_bank[] = {
	F7188X_GPIO_BANK(0, 8, 0xF0),
	F7188X_GPIO_BANK(10, 8, 0xE0),
	F7188X_GPIO_BANK(20, 8, 0xD0),
	F7188X_GPIO_BANK(30, 8, 0xC0),
	F7188X_GPIO_BANK(40, 8, 0xB0),
	F7188X_GPIO_BANK(50, 8, 0xA0),
	F7188X_GPIO_BANK(60, 8, 0x90),
	F7188X_GPIO_BANK(70, 8, 0x80),
	F7188X_GPIO_BANK(80, 8, 0x88),
};

static int f7188x_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	int err;
	struct f7188x_gpio_bank *bank = gpiochip_get_data(chip);
	struct f7188x_sio *sio = bank->data->sio;
	u8 dir;

	err = superio_enter(sio->addr);
	if (err)
		return err;
	superio_select(sio->addr, SIO_LD_GPIO);

	dir = superio_inb(sio->addr, gpio_dir(bank->regbase));

	superio_exit(sio->addr);

	return !(dir & 1 << offset);
}

static int f7188x_gpio_direction_in(struct gpio_chip *chip, unsigned offset)
{
	int err;
	struct f7188x_gpio_bank *bank = gpiochip_get_data(chip);
	struct f7188x_sio *sio = bank->data->sio;
	u8 dir;

	err = superio_enter(sio->addr);
	if (err)
		return err;
	superio_select(sio->addr, SIO_LD_GPIO);

	dir = superio_inb(sio->addr, gpio_dir(bank->regbase));
	dir &= ~BIT(offset);
	superio_outb(sio->addr, gpio_dir(bank->regbase), dir);

	superio_exit(sio->addr);

	return 0;
}

static int f7188x_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	int err;
	struct f7188x_gpio_bank *bank = gpiochip_get_data(chip);
	struct f7188x_sio *sio = bank->data->sio;
	u8 dir, data;

	err = superio_enter(sio->addr);
	if (err)
		return err;
	superio_select(sio->addr, SIO_LD_GPIO);

	dir = superio_inb(sio->addr, gpio_dir(bank->regbase));
	dir = !!(dir & BIT(offset));
	if (dir)
		data = superio_inb(sio->addr, gpio_data_out(bank->regbase));
	else
		data = superio_inb(sio->addr, gpio_data_in(bank->regbase));

	superio_exit(sio->addr);

	return !!(data & BIT(offset));
}

static int f7188x_gpio_direction_out(struct gpio_chip *chip,
				     unsigned offset, int value)
{
	int err;
	struct f7188x_gpio_bank *bank = gpiochip_get_data(chip);
	struct f7188x_sio *sio = bank->data->sio;
	u8 dir, data_out;

	err = superio_enter(sio->addr);
	if (err)
		return err;
	superio_select(sio->addr, SIO_LD_GPIO);

	data_out = superio_inb(sio->addr, gpio_data_out(bank->regbase));
	if (value)
		data_out |= BIT(offset);
	else
		data_out &= ~BIT(offset);
	superio_outb(sio->addr, gpio_data_out(bank->regbase), data_out);

	dir = superio_inb(sio->addr, gpio_dir(bank->regbase));
	dir |= BIT(offset);
	superio_outb(sio->addr, gpio_dir(bank->regbase), dir);

	superio_exit(sio->addr);

	return 0;
}

static void f7188x_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	int err;
	struct f7188x_gpio_bank *bank = gpiochip_get_data(chip);
	struct f7188x_sio *sio = bank->data->sio;
	u8 data_out;

	err = superio_enter(sio->addr);
	if (err)
		return;
	superio_select(sio->addr, SIO_LD_GPIO);

	data_out = superio_inb(sio->addr, gpio_data_out(bank->regbase));
	if (value)
		data_out |= BIT(offset);
	else
		data_out &= ~BIT(offset);
	superio_outb(sio->addr, gpio_data_out(bank->regbase), data_out);

	superio_exit(sio->addr);
}

static int f7188x_gpio_set_config(struct gpio_chip *chip, unsigned offset,
				  unsigned long config)
{
	int err;
	enum pin_config_param param = pinconf_to_config_param(config);
	struct f7188x_gpio_bank *bank = gpiochip_get_data(chip);
	struct f7188x_sio *sio = bank->data->sio;
	u8 data;

	if (param != PIN_CONFIG_DRIVE_OPEN_DRAIN &&
	    param != PIN_CONFIG_DRIVE_PUSH_PULL)
		return -ENOTSUPP;

	err = superio_enter(sio->addr);
	if (err)
		return err;
	superio_select(sio->addr, SIO_LD_GPIO);

	data = superio_inb(sio->addr, gpio_out_mode(bank->regbase));
	if (param == PIN_CONFIG_DRIVE_OPEN_DRAIN)
		data &= ~BIT(offset);
	else
		data |= BIT(offset);
	superio_outb(sio->addr, gpio_out_mode(bank->regbase), data);

	superio_exit(sio->addr);
	return 0;
}

/*
 * Platform device and driver.
 */

static int f7188x_gpio_probe(struct platform_device *pdev)
{
	int err;
	int i;
	struct f7188x_sio *sio = dev_get_platdata(&pdev->dev);
	struct f7188x_gpio_data *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	switch (sio->type) {
	case f71869:
		data->nr_bank = ARRAY_SIZE(f71869_gpio_bank);
		data->bank = f71869_gpio_bank;
		break;
	case f71869a:
		data->nr_bank = ARRAY_SIZE(f71869a_gpio_bank);
		data->bank = f71869a_gpio_bank;
		break;
	case f71882fg:
		data->nr_bank = ARRAY_SIZE(f71882_gpio_bank);
		data->bank = f71882_gpio_bank;
		break;
	case f71889a:
		data->nr_bank = ARRAY_SIZE(f71889a_gpio_bank);
		data->bank = f71889a_gpio_bank;
		break;
	case f71889f:
		data->nr_bank = ARRAY_SIZE(f71889_gpio_bank);
		data->bank = f71889_gpio_bank;
		break;
	case f81866:
		data->nr_bank = ARRAY_SIZE(f81866_gpio_bank);
		data->bank = f81866_gpio_bank;
		break;
	default:
		return -ENODEV;
	}
	data->sio = sio;

	platform_set_drvdata(pdev, data);

	/* For each GPIO bank, register a GPIO chip. */
	for (i = 0; i < data->nr_bank; i++) {
		struct f7188x_gpio_bank *bank = &data->bank[i];

		bank->chip.parent = &pdev->dev;
		bank->data = data;

		err = devm_gpiochip_add_data(&pdev->dev, &bank->chip, bank);
		if (err) {
			dev_err(&pdev->dev,
				"Failed to register gpiochip %d: %d\n",
				i, err);
			return err;
		}
	}

	return 0;
}

static int __init f7188x_find(int addr, struct f7188x_sio *sio)
{
	int err;
	u16 devid;

	err = superio_enter(addr);
	if (err)
		return err;

	err = -ENODEV;
	devid = superio_inw(addr, SIO_MANID);
	if (devid != SIO_FINTEK_ID) {
		pr_debug(DRVNAME ": Not a Fintek device at 0x%08x\n", addr);
		goto err;
	}

	devid = superio_inw(addr, SIO_DEVID);
	switch (devid) {
	case SIO_F71869_ID:
		sio->type = f71869;
		break;
	case SIO_F71869A_ID:
		sio->type = f71869a;
		break;
	case SIO_F71882_ID:
		sio->type = f71882fg;
		break;
	case SIO_F71889A_ID:
		sio->type = f71889a;
		break;
	case SIO_F71889_ID:
		sio->type = f71889f;
		break;
	case SIO_F81866_ID:
		sio->type = f81866;
		break;
	default:
		pr_info(DRVNAME ": Unsupported Fintek device 0x%04x\n", devid);
		goto err;
	}
	sio->addr = addr;
	err = 0;

	pr_info(DRVNAME ": Found %s at %#x, revision %d\n",
		f7188x_names[sio->type],
		(unsigned int) addr,
		(int) superio_inb(addr, SIO_DEVREV));

err:
	superio_exit(addr);
	return err;
}

static struct platform_device *f7188x_gpio_pdev;

static int __init
f7188x_gpio_device_add(const struct f7188x_sio *sio)
{
	int err;

	f7188x_gpio_pdev = platform_device_alloc(DRVNAME, -1);
	if (!f7188x_gpio_pdev)
		return -ENOMEM;

	err = platform_device_add_data(f7188x_gpio_pdev,
				       sio, sizeof(*sio));
	if (err) {
		pr_err(DRVNAME "Platform data allocation failed\n");
		goto err;
	}

	err = platform_device_add(f7188x_gpio_pdev);
	if (err) {
		pr_err(DRVNAME "Device addition failed\n");
		goto err;
	}

	return 0;

err:
	platform_device_put(f7188x_gpio_pdev);

	return err;
}

/*
 * Try to match a supported Fintek device by reading the (hard-wired)
 * configuration I/O ports. If available, then register both the platform
 * device and driver to support the GPIOs.
 */

static struct platform_driver f7188x_gpio_driver = {
	.driver = {
		.name	= DRVNAME,
	},
	.probe		= f7188x_gpio_probe,
};

static int __init f7188x_gpio_init(void)
{
	int err;
	struct f7188x_sio sio;

	if (f7188x_find(0x2e, &sio) &&
	    f7188x_find(0x4e, &sio))
		return -ENODEV;

	err = platform_driver_register(&f7188x_gpio_driver);
	if (!err) {
		err = f7188x_gpio_device_add(&sio);
		if (err)
			platform_driver_unregister(&f7188x_gpio_driver);
	}

	return err;
}
subsys_initcall(f7188x_gpio_init);

static void __exit f7188x_gpio_exit(void)
{
	platform_device_unregister(f7188x_gpio_pdev);
	platform_driver_unregister(&f7188x_gpio_driver);
}
module_exit(f7188x_gpio_exit);

MODULE_DESCRIPTION("GPIO driver for Super-I/O chips F71869, F71869A, F71882FG, F71889A, F71889F and F81866");
MODULE_AUTHOR("Simon Guinot <simon.guinot@sequanux.org>");
MODULE_LICENSE("GPL");
