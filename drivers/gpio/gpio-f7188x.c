// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * GPIO driver for Fintek and Nuvoton Super-I/O chips
 *
 * Copyright (C) 2010-2013 LaCie
 *
 * Author: Simon Guinot <simon.guinot@sequanux.org>
 */

#define DRVNAME "gpio-f7188x"
#define pr_fmt(fmt) DRVNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio/driver.h>
#include <linux/bitops.h>

/*
 * Super-I/O registers
 */
#define SIO_LDSEL		0x07	/* Logical device select */
#define SIO_DEVID		0x20	/* Device ID (2 bytes) */

#define SIO_UNLOCK_KEY		0x87	/* Key to enable Super-I/O */
#define SIO_LOCK_KEY		0xAA	/* Key to disable Super-I/O */

/*
 * Fintek devices.
 */
#define SIO_FINTEK_DEVREV	0x22	/* Fintek Device revision */
#define SIO_FINTEK_MANID	0x23    /* Fintek ID (2 bytes) */

#define SIO_FINTEK_ID		0x1934  /* Manufacturer ID */

#define SIO_F71869_ID		0x0814	/* F71869 chipset ID */
#define SIO_F71869A_ID		0x1007	/* F71869A chipset ID */
#define SIO_F71882_ID		0x0541	/* F71882 chipset ID */
#define SIO_F71889_ID		0x0909	/* F71889 chipset ID */
#define SIO_F71889A_ID		0x1005	/* F71889A chipset ID */
#define SIO_F81866_ID		0x1010	/* F81866 chipset ID */
#define SIO_F81804_ID		0x1502  /* F81804 chipset ID, same for F81966 */
#define SIO_F81865_ID		0x0704	/* F81865 chipset ID */

#define SIO_LD_GPIO_FINTEK	0x06	/* GPIO logical device */

/*
 * Nuvoton devices.
 */
#define SIO_NCT6126D_ID		0xD283  /* NCT6126D chipset ID */

#define SIO_LD_GPIO_NUVOTON	0x07	/* GPIO logical device */


enum chips {
	f71869,
	f71869a,
	f71882fg,
	f71889a,
	f71889f,
	f81866,
	f81804,
	f81865,
	nct6126d,
};

static const char * const f7188x_names[] = {
	"f71869",
	"f71869a",
	"f71882fg",
	"f71889a",
	"f71889f",
	"f81866",
	"f81804",
	"f81865",
	"nct6126d",
};

struct f7188x_sio {
	int addr;
	int device;
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
		pr_err("I/O address 0x%04x already in use\n", base);
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
static int f7188x_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value);
static int f7188x_gpio_set_config(struct gpio_chip *chip, unsigned offset,
				  unsigned long config);

#define F7188X_GPIO_BANK(_ngpio, _regbase, _label)			\
	{								\
		.chip = {						\
			.label            = _label,			\
			.owner            = THIS_MODULE,		\
			.get_direction    = f7188x_gpio_get_direction,	\
			.direction_input  = f7188x_gpio_direction_in,	\
			.get              = f7188x_gpio_get,		\
			.direction_output = f7188x_gpio_direction_out,	\
			.set              = f7188x_gpio_set,		\
			.set_config	  = f7188x_gpio_set_config,	\
			.base             = -1,				\
			.ngpio            = _ngpio,			\
			.can_sleep        = true,			\
		},							\
		.regbase = _regbase,					\
	}

#define f7188x_gpio_dir(base) ((base) + 0)
#define f7188x_gpio_data_out(base) ((base) + 1)
#define f7188x_gpio_data_in(base) ((base) + 2)
/* Output mode register (0:open drain 1:push-pull). */
#define f7188x_gpio_out_mode(base) ((base) + 3)

#define f7188x_gpio_dir_invert(type)	((type) == nct6126d)
#define f7188x_gpio_data_single(type)	((type) == nct6126d)

static struct f7188x_gpio_bank f71869_gpio_bank[] = {
	F7188X_GPIO_BANK(6, 0xF0, DRVNAME "-0"),
	F7188X_GPIO_BANK(8, 0xE0, DRVNAME "-1"),
	F7188X_GPIO_BANK(8, 0xD0, DRVNAME "-2"),
	F7188X_GPIO_BANK(8, 0xC0, DRVNAME "-3"),
	F7188X_GPIO_BANK(8, 0xB0, DRVNAME "-4"),
	F7188X_GPIO_BANK(5, 0xA0, DRVNAME "-5"),
	F7188X_GPIO_BANK(6, 0x90, DRVNAME "-6"),
};

static struct f7188x_gpio_bank f71869a_gpio_bank[] = {
	F7188X_GPIO_BANK(6, 0xF0, DRVNAME "-0"),
	F7188X_GPIO_BANK(8, 0xE0, DRVNAME "-1"),
	F7188X_GPIO_BANK(8, 0xD0, DRVNAME "-2"),
	F7188X_GPIO_BANK(8, 0xC0, DRVNAME "-3"),
	F7188X_GPIO_BANK(8, 0xB0, DRVNAME "-4"),
	F7188X_GPIO_BANK(5, 0xA0, DRVNAME "-5"),
	F7188X_GPIO_BANK(8, 0x90, DRVNAME "-6"),
	F7188X_GPIO_BANK(8, 0x80, DRVNAME "-7"),
};

static struct f7188x_gpio_bank f71882_gpio_bank[] = {
	F7188X_GPIO_BANK(8, 0xF0, DRVNAME "-0"),
	F7188X_GPIO_BANK(8, 0xE0, DRVNAME "-1"),
	F7188X_GPIO_BANK(8, 0xD0, DRVNAME "-2"),
	F7188X_GPIO_BANK(4, 0xC0, DRVNAME "-3"),
	F7188X_GPIO_BANK(4, 0xB0, DRVNAME "-4"),
};

static struct f7188x_gpio_bank f71889a_gpio_bank[] = {
	F7188X_GPIO_BANK(7, 0xF0, DRVNAME "-0"),
	F7188X_GPIO_BANK(7, 0xE0, DRVNAME "-1"),
	F7188X_GPIO_BANK(8, 0xD0, DRVNAME "-2"),
	F7188X_GPIO_BANK(8, 0xC0, DRVNAME "-3"),
	F7188X_GPIO_BANK(8, 0xB0, DRVNAME "-4"),
	F7188X_GPIO_BANK(5, 0xA0, DRVNAME "-5"),
	F7188X_GPIO_BANK(8, 0x90, DRVNAME "-6"),
	F7188X_GPIO_BANK(8, 0x80, DRVNAME "-7"),
};

static struct f7188x_gpio_bank f71889_gpio_bank[] = {
	F7188X_GPIO_BANK(7, 0xF0, DRVNAME "-0"),
	F7188X_GPIO_BANK(7, 0xE0, DRVNAME "-1"),
	F7188X_GPIO_BANK(8, 0xD0, DRVNAME "-2"),
	F7188X_GPIO_BANK(8, 0xC0, DRVNAME "-3"),
	F7188X_GPIO_BANK(8, 0xB0, DRVNAME "-4"),
	F7188X_GPIO_BANK(5, 0xA0, DRVNAME "-5"),
	F7188X_GPIO_BANK(8, 0x90, DRVNAME "-6"),
	F7188X_GPIO_BANK(8, 0x80, DRVNAME "-7"),
};

static struct f7188x_gpio_bank f81866_gpio_bank[] = {
	F7188X_GPIO_BANK(8, 0xF0, DRVNAME "-0"),
	F7188X_GPIO_BANK(8, 0xE0, DRVNAME "-1"),
	F7188X_GPIO_BANK(8, 0xD0, DRVNAME "-2"),
	F7188X_GPIO_BANK(8, 0xC0, DRVNAME "-3"),
	F7188X_GPIO_BANK(8, 0xB0, DRVNAME "-4"),
	F7188X_GPIO_BANK(8, 0xA0, DRVNAME "-5"),
	F7188X_GPIO_BANK(8, 0x90, DRVNAME "-6"),
	F7188X_GPIO_BANK(8, 0x80, DRVNAME "-7"),
	F7188X_GPIO_BANK(8, 0x88, DRVNAME "-8"),
};


static struct f7188x_gpio_bank f81804_gpio_bank[] = {
	F7188X_GPIO_BANK(8, 0xF0, DRVNAME "-0"),
	F7188X_GPIO_BANK(8, 0xE0, DRVNAME "-1"),
	F7188X_GPIO_BANK(8, 0xD0, DRVNAME "-2"),
	F7188X_GPIO_BANK(8, 0xA0, DRVNAME "-3"),
	F7188X_GPIO_BANK(8, 0x90, DRVNAME "-4"),
	F7188X_GPIO_BANK(8, 0x80, DRVNAME "-5"),
	F7188X_GPIO_BANK(8, 0x98, DRVNAME "-6"),
};

static struct f7188x_gpio_bank f81865_gpio_bank[] = {
	F7188X_GPIO_BANK(8, 0xF0, DRVNAME "-0"),
	F7188X_GPIO_BANK(8, 0xE0, DRVNAME "-1"),
	F7188X_GPIO_BANK(8, 0xD0, DRVNAME "-2"),
	F7188X_GPIO_BANK(8, 0xC0, DRVNAME "-3"),
	F7188X_GPIO_BANK(8, 0xB0, DRVNAME "-4"),
	F7188X_GPIO_BANK(8, 0xA0, DRVNAME "-5"),
	F7188X_GPIO_BANK(5, 0x90, DRVNAME "-6"),
};

static struct f7188x_gpio_bank nct6126d_gpio_bank[] = {
	F7188X_GPIO_BANK(8, 0xE0, DRVNAME "-0"),
	F7188X_GPIO_BANK(8, 0xE4, DRVNAME "-1"),
	F7188X_GPIO_BANK(8, 0xE8, DRVNAME "-2"),
	F7188X_GPIO_BANK(8, 0xEC, DRVNAME "-3"),
	F7188X_GPIO_BANK(8, 0xF0, DRVNAME "-4"),
	F7188X_GPIO_BANK(8, 0xF4, DRVNAME "-5"),
	F7188X_GPIO_BANK(8, 0xF8, DRVNAME "-6"),
	F7188X_GPIO_BANK(8, 0xFC, DRVNAME "-7"),
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
	superio_select(sio->addr, sio->device);

	dir = superio_inb(sio->addr, f7188x_gpio_dir(bank->regbase));

	superio_exit(sio->addr);

	if (f7188x_gpio_dir_invert(sio->type))
		dir = ~dir;

	if (dir & BIT(offset))
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
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
	superio_select(sio->addr, sio->device);

	dir = superio_inb(sio->addr, f7188x_gpio_dir(bank->regbase));

	if (f7188x_gpio_dir_invert(sio->type))
		dir |= BIT(offset);
	else
		dir &= ~BIT(offset);
	superio_outb(sio->addr, f7188x_gpio_dir(bank->regbase), dir);

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
	superio_select(sio->addr, sio->device);

	dir = superio_inb(sio->addr, f7188x_gpio_dir(bank->regbase));
	dir = !!(dir & BIT(offset));
	if (f7188x_gpio_data_single(sio->type) || dir)
		data = superio_inb(sio->addr, f7188x_gpio_data_out(bank->regbase));
	else
		data = superio_inb(sio->addr, f7188x_gpio_data_in(bank->regbase));

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
	superio_select(sio->addr, sio->device);

	data_out = superio_inb(sio->addr, f7188x_gpio_data_out(bank->regbase));
	if (value)
		data_out |= BIT(offset);
	else
		data_out &= ~BIT(offset);
	superio_outb(sio->addr, f7188x_gpio_data_out(bank->regbase), data_out);

	dir = superio_inb(sio->addr, f7188x_gpio_dir(bank->regbase));
	if (f7188x_gpio_dir_invert(sio->type))
		dir &= ~BIT(offset);
	else
		dir |= BIT(offset);
	superio_outb(sio->addr, f7188x_gpio_dir(bank->regbase), dir);

	superio_exit(sio->addr);

	return 0;
}

static int f7188x_gpio_set(struct gpio_chip *chip, unsigned int offset,
			   int value)
{
	int err;
	struct f7188x_gpio_bank *bank = gpiochip_get_data(chip);
	struct f7188x_sio *sio = bank->data->sio;
	u8 data_out;

	err = superio_enter(sio->addr);
	if (err)
		return err;

	superio_select(sio->addr, sio->device);

	data_out = superio_inb(sio->addr, f7188x_gpio_data_out(bank->regbase));
	if (value)
		data_out |= BIT(offset);
	else
		data_out &= ~BIT(offset);
	superio_outb(sio->addr, f7188x_gpio_data_out(bank->regbase), data_out);

	superio_exit(sio->addr);

	return 0;
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
	superio_select(sio->addr, sio->device);

	data = superio_inb(sio->addr, f7188x_gpio_out_mode(bank->regbase));
	if (param == PIN_CONFIG_DRIVE_OPEN_DRAIN)
		data &= ~BIT(offset);
	else
		data |= BIT(offset);
	superio_outb(sio->addr, f7188x_gpio_out_mode(bank->regbase), data);

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
	case  f81804:
		data->nr_bank = ARRAY_SIZE(f81804_gpio_bank);
		data->bank = f81804_gpio_bank;
		break;
	case f81865:
		data->nr_bank = ARRAY_SIZE(f81865_gpio_bank);
		data->bank = f81865_gpio_bank;
		break;
	case nct6126d:
		data->nr_bank = ARRAY_SIZE(nct6126d_gpio_bank);
		data->bank = nct6126d_gpio_bank;
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
	u16 manid;

	err = superio_enter(addr);
	if (err)
		return err;

	err = -ENODEV;

	sio->device = SIO_LD_GPIO_FINTEK;
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
	case SIO_F81804_ID:
		sio->type = f81804;
		break;
	case SIO_F81865_ID:
		sio->type = f81865;
		break;
	case SIO_NCT6126D_ID:
		sio->device = SIO_LD_GPIO_NUVOTON;
		sio->type = nct6126d;
		break;
	default:
		pr_info("Unsupported Fintek device 0x%04x\n", devid);
		goto err;
	}

	/* double check manufacturer where possible */
	if (sio->type != nct6126d) {
		manid = superio_inw(addr, SIO_FINTEK_MANID);
		if (manid != SIO_FINTEK_ID) {
			pr_debug("Not a Fintek device at 0x%08x\n", addr);
			goto err;
		}
	}

	sio->addr = addr;
	err = 0;

	pr_info("Found %s at %#x\n", f7188x_names[sio->type], (unsigned int)addr);
	if (sio->type != nct6126d)
		pr_info("   revision %d\n", superio_inb(addr, SIO_FINTEK_DEVREV));

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
		pr_err("Platform data allocation failed\n");
		goto err;
	}

	err = platform_device_add(f7188x_gpio_pdev);
	if (err) {
		pr_err("Device addition failed\n");
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
