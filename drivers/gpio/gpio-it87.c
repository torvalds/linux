/*
 *  GPIO interface for IT87xx Super I/O chips
 *
 *  Author: Diego Elio Pettenò <flameeyes@flameeyes.eu>
 *
 *  Based on it87_wdt.c     by Oliver Schuster
 *           gpio-it8761e.c by Denis Turischev
 *           gpio-stmpe.c   by Rabin Vincent
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License 2 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/gpio.h>

/* Chip Id numbers */
#define NO_DEV_ID	0xffff
#define IT8620_ID	0x8620
#define IT8628_ID	0x8628
#define IT8728_ID	0x8728
#define IT8732_ID	0x8732
#define IT8761_ID	0x8761

/* IO Ports */
#define REG		0x2e
#define VAL		0x2f

/* Logical device Numbers LDN */
#define GPIO		0x07

/* Configuration Registers and Functions */
#define LDNREG		0x07
#define CHIPID		0x20
#define CHIPREV		0x22

/**
 * struct it87_gpio - it87-specific GPIO chip
 * @chip the underlying gpio_chip structure
 * @lock a lock to avoid races between operations
 * @io_base base address for gpio ports
 * @io_size size of the port rage starting from io_base.
 * @output_base Super I/O register address for Output Enable register
 * @simple_base Super I/O 'Simple I/O' Enable register
 * @simple_size Super IO 'Simple I/O' Enable register size; this is
 *	required because IT87xx chips might only provide Simple I/O
 *	switches on a subset of lines, whereas the others keep the
 *	same status all time.
 */
struct it87_gpio {
	struct gpio_chip chip;
	spinlock_t lock;
	u16 io_base;
	u16 io_size;
	u8 output_base;
	u8 simple_base;
	u8 simple_size;
};

static struct it87_gpio it87_gpio_chip = {
	.lock = __SPIN_LOCK_UNLOCKED(it87_gpio_chip.lock),
};

/* Superio chip access functions; copied from wdt_it87 */

static inline int superio_enter(void)
{
	/*
	 * Try to reserve REG and REG + 1 for exclusive access.
	 */
	if (!request_muxed_region(REG, 2, KBUILD_MODNAME))
		return -EBUSY;

	outb(0x87, REG);
	outb(0x01, REG);
	outb(0x55, REG);
	outb(0x55, REG);
	return 0;
}

static inline void superio_exit(void)
{
	outb(0x02, REG);
	outb(0x02, VAL);
	release_region(REG, 2);
}

static inline void superio_select(int ldn)
{
	outb(LDNREG, REG);
	outb(ldn, VAL);
}

static inline int superio_inb(int reg)
{
	outb(reg, REG);
	return inb(VAL);
}

static inline void superio_outb(int val, int reg)
{
	outb(reg, REG);
	outb(val, VAL);
}

static inline int superio_inw(int reg)
{
	int val;

	outb(reg++, REG);
	val = inb(VAL) << 8;
	outb(reg, REG);
	val |= inb(VAL);
	return val;
}

static inline void superio_outw(int val, int reg)
{
	outb(reg++, REG);
	outb(val >> 8, VAL);
	outb(reg, REG);
	outb(val, VAL);
}

static inline void superio_set_mask(int mask, int reg)
{
	u8 curr_val = superio_inb(reg);
	u8 new_val = curr_val | mask;

	if (curr_val != new_val)
		superio_outb(new_val, reg);
}

static inline void superio_clear_mask(int mask, int reg)
{
	u8 curr_val = superio_inb(reg);
	u8 new_val = curr_val & ~mask;

	if (curr_val != new_val)
		superio_outb(new_val, reg);
}

static int it87_gpio_request(struct gpio_chip *chip, unsigned gpio_num)
{
	u8 mask, group;
	int rc = 0;
	struct it87_gpio *it87_gpio = gpiochip_get_data(chip);

	mask = 1 << (gpio_num % 8);
	group = (gpio_num / 8);

	spin_lock(&it87_gpio->lock);

	rc = superio_enter();
	if (rc)
		goto exit;

	/* not all the IT87xx chips support Simple I/O and not all of
	 * them allow all the lines to be set/unset to Simple I/O.
	 */
	if (group < it87_gpio->simple_size)
		superio_set_mask(mask, group + it87_gpio->simple_base);

	/* clear output enable, setting the pin to input, as all the
	 * newly-exported GPIO interfaces are set to input.
	 */
	superio_clear_mask(mask, group + it87_gpio->output_base);

	superio_exit();

exit:
	spin_unlock(&it87_gpio->lock);
	return rc;
}

static int it87_gpio_get(struct gpio_chip *chip, unsigned gpio_num)
{
	u16 reg;
	u8 mask;
	struct it87_gpio *it87_gpio = gpiochip_get_data(chip);

	mask = 1 << (gpio_num % 8);
	reg = (gpio_num / 8) + it87_gpio->io_base;

	return !!(inb(reg) & mask);
}

static int it87_gpio_direction_in(struct gpio_chip *chip, unsigned gpio_num)
{
	u8 mask, group;
	int rc = 0;
	struct it87_gpio *it87_gpio = gpiochip_get_data(chip);

	mask = 1 << (gpio_num % 8);
	group = (gpio_num / 8);

	spin_lock(&it87_gpio->lock);

	rc = superio_enter();
	if (rc)
		goto exit;

	/* clear the output enable bit */
	superio_clear_mask(mask, group + it87_gpio->output_base);

	superio_exit();

exit:
	spin_unlock(&it87_gpio->lock);
	return rc;
}

static void it87_gpio_set(struct gpio_chip *chip,
			  unsigned gpio_num, int val)
{
	u8 mask, curr_vals;
	u16 reg;
	struct it87_gpio *it87_gpio = gpiochip_get_data(chip);

	mask = 1 << (gpio_num % 8);
	reg = (gpio_num / 8) + it87_gpio->io_base;

	curr_vals = inb(reg);
	if (val)
		outb(curr_vals | mask, reg);
	else
		outb(curr_vals & ~mask, reg);
}

static int it87_gpio_direction_out(struct gpio_chip *chip,
				   unsigned gpio_num, int val)
{
	u8 mask, group;
	int rc = 0;
	struct it87_gpio *it87_gpio = gpiochip_get_data(chip);

	mask = 1 << (gpio_num % 8);
	group = (gpio_num / 8);

	spin_lock(&it87_gpio->lock);

	rc = superio_enter();
	if (rc)
		goto exit;

	/* set the output enable bit */
	superio_set_mask(mask, group + it87_gpio->output_base);

	it87_gpio_set(chip, gpio_num, val);

	superio_exit();

exit:
	spin_unlock(&it87_gpio->lock);
	return rc;
}

static struct gpio_chip it87_template_chip = {
	.label			= KBUILD_MODNAME,
	.owner			= THIS_MODULE,
	.request		= it87_gpio_request,
	.get			= it87_gpio_get,
	.direction_input	= it87_gpio_direction_in,
	.set			= it87_gpio_set,
	.direction_output	= it87_gpio_direction_out,
	.base			= -1
};

static int __init it87_gpio_init(void)
{
	int rc = 0, i;
	u16 chip_type;
	u8 chip_rev, gpio_ba_reg;
	char *labels, **labels_table;

	struct it87_gpio *it87_gpio = &it87_gpio_chip;

	rc = superio_enter();
	if (rc)
		return rc;

	chip_type = superio_inw(CHIPID);
	chip_rev  = superio_inb(CHIPREV) & 0x0f;
	superio_exit();

	it87_gpio->chip = it87_template_chip;

	switch (chip_type) {
	case IT8620_ID:
	case IT8628_ID:
		gpio_ba_reg = 0x62;
		it87_gpio->io_size = 11;
		it87_gpio->output_base = 0xc8;
		it87_gpio->simple_size = 0;
		it87_gpio->chip.ngpio = 64;
		break;
	case IT8728_ID:
	case IT8732_ID:
		gpio_ba_reg = 0x62;
		it87_gpio->io_size = 8;
		it87_gpio->output_base = 0xc8;
		it87_gpio->simple_base = 0xc0;
		it87_gpio->simple_size = 5;
		it87_gpio->chip.ngpio = 64;
		break;
	case IT8761_ID:
		gpio_ba_reg = 0x60;
		it87_gpio->io_size = 4;
		it87_gpio->output_base = 0xf0;
		it87_gpio->simple_size = 0;
		it87_gpio->chip.ngpio = 16;
		break;
	case NO_DEV_ID:
		pr_err("no device\n");
		return -ENODEV;
	default:
		pr_err("Unknown Chip found, Chip %04x Revision %x\n",
		       chip_type, chip_rev);
		return -ENODEV;
	}

	rc = superio_enter();
	if (rc)
		return rc;

	superio_select(GPIO);

	/* fetch GPIO base address */
	it87_gpio->io_base = superio_inw(gpio_ba_reg);

	superio_exit();

	pr_info("Found Chip IT%04x rev %x. %u GPIO lines starting at %04xh\n",
		chip_type, chip_rev, it87_gpio->chip.ngpio,
		it87_gpio->io_base);

	if (!request_region(it87_gpio->io_base, it87_gpio->io_size,
							KBUILD_MODNAME))
		return -EBUSY;

	/* Set up aliases for the GPIO connection.
	 *
	 * ITE documentation for recent chips such as the IT8728F
	 * refers to the GPIO lines as GPxy, with a coordinates system
	 * where x is the GPIO group (starting from 1) and y is the
	 * bit within the group.
	 *
	 * By creating these aliases, we make it easier to understand
	 * to which GPIO pin we're referring to.
	 */
	labels = kcalloc(it87_gpio->chip.ngpio, sizeof("it87_gpXY"),
								GFP_KERNEL);
	labels_table = kcalloc(it87_gpio->chip.ngpio, sizeof(const char *),
								GFP_KERNEL);

	if (!labels || !labels_table) {
		rc = -ENOMEM;
		goto labels_free;
	}

	for (i = 0; i < it87_gpio->chip.ngpio; i++) {
		char *label = &labels[i * sizeof("it87_gpXY")];

		sprintf(label, "it87_gp%u%u", 1+(i/8), i%8);
		labels_table[i] = label;
	}

	it87_gpio->chip.names = (const char *const*)labels_table;

	rc = gpiochip_add_data(&it87_gpio->chip, it87_gpio);
	if (rc)
		goto labels_free;

	return 0;

labels_free:
	kfree(labels_table);
	kfree(labels);
	release_region(it87_gpio->io_base, it87_gpio->io_size);
	return rc;
}

static void __exit it87_gpio_exit(void)
{
	struct it87_gpio *it87_gpio = &it87_gpio_chip;

	gpiochip_remove(&it87_gpio->chip);
	release_region(it87_gpio->io_base, it87_gpio->io_size);
	kfree(it87_gpio->chip.names[0]);
	kfree(it87_gpio->chip.names);
}

module_init(it87_gpio_init);
module_exit(it87_gpio_exit);

MODULE_AUTHOR("Diego Elio PettenÃ² <flameeyes@flameeyes.eu>");
MODULE_DESCRIPTION("GPIO interface for IT87xx Super I/O chips");
MODULE_LICENSE("GPL");
