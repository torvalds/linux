/*
 *  sunxi_gpio.c - GPIO interface for sunxi platforms (Allwinner A1X)
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

#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <asm/mach/irq.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <plat/sys_config.h>
#include <mach/hardware.h>
#include <mach/system.h>
#include <mach/irqs.h>
#include "gpio-sunxi.h"

#define GPIO_IRQ_NO SW_INT_IRQNO_PIO
#define EINT_NUM gpio_eint_count

static struct gpio_eint_data *gpio_eint_list;
static u32 gpio_eint_count;

static inline struct sunxi_gpio_chip *to_sunxi_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct sunxi_gpio_chip, chip);
}

/* Check if GPIO can be EINT source and return its virtual irq */
/* number. These irq numbers handled by separate virtual irq chip */
static int sunxi_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct sunxi_gpio_chip *sgpio = to_sunxi_gpio(chip);
	if ((offset > chip->ngpio - 1) || (offset < 0))
		return -EINVAL;

	if ((sgpio->irq_base >= 0) && (sgpio->data[offset].eint >= 0))
		return sgpio->irq_base + sgpio->data[offset].eint;

	return -EINVAL;
}

static int sunxi_find_gpio_irq(struct gpio_chip *chip, unsigned offset)
{
	int i = 0;
	int pp = 0;
	int pi = 0;
	struct sunxi_gpio_chip *sgpio = to_sunxi_gpio(chip);

	/* search port:pin in the eint list */
	pp = sgpio->data[offset].info.port-1;
	pi = sgpio->data[offset].info.port_num;
	for (i = 0;; i++) {
		if ((gpio_eint_list[i].port < 0) &&
		    (gpio_eint_list[i].pin < 0) &&
		    (gpio_eint_list[i].mux < 0) &&
		    (gpio_eint_list[i].gpio < 0))
			break;

		/* Table contain EINT_NUM sources */
		if ((gpio_eint_list[i].port == pp) &&
		    (gpio_eint_list[i].pin == pi) &&
		    (gpio_eint_list[i].mux >= 0)) {
			return i;
		}
	}

	return -EINVAL;
}

/* Get gpio pin value */
static int sunxi_gpio_get(struct gpio_chip *chip, unsigned gpio)
{
	int ret;
	unsigned long flags;
	struct sunxi_gpio_chip *sgpio = to_sunxi_gpio(chip);

	/* Some pins can be used as interrupt source. This  */
	/* works only for EINT pin mode. But you can't read */
	/* pin value in this mode. So we should check mode  */
	/* here and then switch_input/read/switch_eint      */
	if (sgpio->data[gpio].info.mul_sel == sgpio->data[gpio].eint_mux) {

		spin_lock_irqsave(&sgpio->irq_lock, flags);
		SUNXI_SET_GPIO_MODE(sgpio->gaddr, sgpio->data[gpio].info.port,
				    sgpio->data[gpio].info.port_num,
				    SUNXI_GPIO_INPUT);
		ret = gpio_read_one_pin_value(sgpio->data[gpio].gpio_handler,
						sgpio->data[gpio].pin_name);
		SUNXI_SET_GPIO_MODE(sgpio->gaddr, sgpio->data[gpio].info.port,
				    sgpio->data[gpio].info.port_num,
				    sgpio->data[gpio].eint_mux);
		spin_unlock_irqrestore(&sgpio->irq_lock, flags);
	} else {
		/* "normal" pin read */
		ret = gpio_read_one_pin_value(sgpio->data[gpio].gpio_handler,
						sgpio->data[gpio].pin_name);
	}

	return ret;
}

/* Set gpio pin value */
static void sunxi_gpio_set(struct gpio_chip *chip, unsigned gpio, int val)
{
	int ret;
	struct sunxi_gpio_chip *sgpio = to_sunxi_gpio(chip);
	ret = gpio_write_one_pin_value(sgpio->data[gpio].gpio_handler,
					val, sgpio->data[gpio].pin_name);
	return;
}

/* Set gpio pin input mode */
static int sunxi_gpio_direction_in(struct gpio_chip *chip, unsigned gpio)
{
	int ret = 0;
	struct sunxi_gpio_chip *sgpio = to_sunxi_gpio(chip);

	/* EINT mode == INPUT mode, so if we  */
	/* are in EINT mode - just skip setup */
	/* To switch to input mode you should */
	/* disable irq first */
	if (sgpio->data[gpio].info.mul_sel != sgpio->data[gpio].eint_mux) {
		ret = gpio_set_one_pin_io_status(sgpio->data[gpio].gpio_handler,
						 SUNXI_GPIO_INPUT,
						 sgpio->data[gpio].pin_name);
		if (!ret)
			sgpio->data[gpio].info.mul_sel = SUNXI_GPIO_INPUT;
	}

	return ret;
}

/* Set gpio pin output mode and value */
static int sunxi_gpio_direction_out(struct gpio_chip *chip,
					unsigned gpio, int val)
{
	int ret;
	struct sunxi_gpio_chip *sgpio = to_sunxi_gpio(chip);
	ret =  gpio_set_one_pin_io_status(sgpio->data[gpio].gpio_handler,
					  SUNXI_GPIO_OUTPUT,
					  sgpio->data[gpio].pin_name);
	if (!ret) {
		sgpio->data[gpio].info.mul_sel = SUNXI_GPIO_OUTPUT;
		ret = gpio_write_one_pin_value(sgpio->data[gpio].gpio_handler,
					       val, sgpio->data[gpio].pin_name);
	}

	return ret;
}

/* Request pin from platform code and setup its sysfs name */
static int sunxi_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	int eint = 0;
	unsigned long flags;
	struct sunxi_gpio_chip *sgpio = to_sunxi_gpio(chip);
	if ((offset > chip->ngpio - 1) || (offset < 0))
		return -EINVAL;

	/* Set sysfs exported gpio name (example "gpio254_ph20") */
	sprintf((char *)(chip->names[offset]), "gpio%d_p%c%d",
		offset+chip->base,
		'a'+sgpio->data[offset].info.port-1,
		sgpio->data[offset].info.port_num);

	sgpio->data[offset].gpio_handler = gpio_request_ex("gpio_para",
						sgpio->data[offset].pin_name);

	if (!sgpio->data[offset].gpio_handler) {
		pr_err("%s can't request '[gpio_para]' '%s', already used ?",
			__func__, sgpio->data[offset].pin_name);
		return -EINVAL;
	}

	/* Save eint in gpio data for irq -> gpio conversion */
	eint = sunxi_find_gpio_irq(chip, offset);
	sgpio->data[offset].eint = eint;
	sgpio->data[offset].eint_mux = -1;
	if (eint >= 0) {
		sgpio->data[offset].eint_mux = gpio_eint_list[eint].mux;
		gpio_eint_list[eint].gpio = offset;
	}

	/* Set gpio input mode (gpiolib initial mode) */
	spin_lock_irqsave(&sgpio->irq_lock, flags);
	SUNXI_SET_GPIO_MODE(sgpio->gaddr, sgpio->data[offset].info.port,
			    sgpio->data[offset].info.port_num,
			    SUNXI_GPIO_INPUT);
	sgpio->data[offset].info.mul_sel = SUNXI_GPIO_INPUT;
	spin_unlock_irqrestore(&sgpio->irq_lock, flags);

	return 0;
}

static void sunxi_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	int eint = 0;
	struct sunxi_gpio_chip *sgpio = to_sunxi_gpio(chip);
	if ((offset > chip->ngpio - 1) || (offset < 0))
		return;

	gpio_release(sgpio->data[offset].gpio_handler, 1);

	/* Mark irq unused (for irq_handler) */
	eint = sunxi_gpio_to_irq(chip, offset) - sgpio->irq_base;
	if (eint >= 0)
		gpio_eint_list[eint].gpio = -1;
}

static struct gpio_chip template_chip = {
	.label			= "sunxi-gpio",
	.owner			= THIS_MODULE,
	.get			= sunxi_gpio_get,
	.direction_input	= sunxi_gpio_direction_in,
	.set			= sunxi_gpio_set,
	.direction_output	= sunxi_gpio_direction_out,
	.request		= sunxi_gpio_request,
	.free			= sunxi_gpio_free,
	.to_irq			= sunxi_gpio_to_irq,
};

static int sunxi_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	unsigned long flags;
	unsigned int int_mode = POSITIVE_EDGE;
	struct sunxi_gpio_chip *sgpio = irq_data_get_irq_chip_data(d);
	int offset = d->irq - sgpio->irq_base;

	if (type == IRQ_TYPE_LEVEL_LOW)
		int_mode = LOW_LEVEL;
	if (type == IRQ_TYPE_LEVEL_HIGH)
		int_mode = HIGH_LEVEL;
	if (type == IRQ_TYPE_EDGE_FALLING)
		int_mode = NEGATIVE_EDGE;
	if (type == IRQ_TYPE_EDGE_BOTH)
		int_mode = DOUBLE_EDGE;

	spin_lock_irqsave(&sgpio->irq_lock, flags);
	SUNXI_SET_GPIO_IRQ_TYPE(sgpio->gaddr, offset, int_mode);
	spin_unlock_irqrestore(&sgpio->irq_lock, flags);

	return 0;
}

static void sunxi_gpio_irq_mask_ack(struct irq_data *d)
{
	unsigned long flags;
	struct sunxi_gpio_chip *sgpio = irq_data_get_irq_chip_data(d);
	int offset = d->irq - sgpio->irq_base;
	spin_lock_irqsave(&sgpio->irq_lock, flags);
	SUNXI_MASK_GPIO_IRQ(sgpio->gaddr, offset);
	SUNXI_CLEAR_EINT(sgpio->gaddr, offset);
	spin_unlock_irqrestore(&sgpio->irq_lock, flags);
}

static void sunxi_gpio_irq_mask(struct irq_data *d)
{
	unsigned long flags;
	struct sunxi_gpio_chip *sgpio = irq_data_get_irq_chip_data(d);
	int offset = d->irq - sgpio->irq_base;
	spin_lock_irqsave(&sgpio->irq_lock, flags);
	SUNXI_MASK_GPIO_IRQ(sgpio->gaddr, offset);
	spin_unlock_irqrestore(&sgpio->irq_lock, flags);
}

static void sunxi_gpio_irq_unmask(struct irq_data *d)
{
	int gpio;
	unsigned long flags;
	struct sunxi_gpio_chip *sgpio = irq_data_get_irq_chip_data(d);
	int offset = d->irq - sgpio->irq_base;

	spin_lock_irqsave(&sgpio->irq_lock, flags);

	/* Check if we should set EINT gpio mode */
	if ((gpio_eint_list[offset].gpio >= 0) &&
	    (gpio_eint_list[offset].mux > 0)) {

		gpio = gpio_eint_list[offset].gpio;
		sgpio->data[gpio].info.mul_sel = gpio_eint_list[offset].mux;
		SUNXI_SET_GPIO_MODE(sgpio->gaddr, sgpio->data[gpio].info.port,
				    sgpio->data[gpio].info.port_num,
				    gpio_eint_list[offset].mux);
	}

	SUNXI_UNMASK_GPIO_IRQ(sgpio->gaddr, offset);
	spin_unlock_irqrestore(&sgpio->irq_lock, flags);
}

/* IRQ_CHIP with EINT_NUM interrupts to demux single A1X PIO irq line */
static struct irq_chip sunxi_gpio_irq_chip = {
	.name			= "gpio-sunxi",
	.irq_mask		= sunxi_gpio_irq_mask,
	.irq_mask_ack		= sunxi_gpio_irq_mask_ack,
	.irq_unmask		= sunxi_gpio_irq_unmask,
	.irq_set_type		= sunxi_gpio_irq_set_type,
};

static void __devinit sunxi_gpio_eint_probe(void)
{
	if (sunxi_is_a10()) {
		/* Pins that can be used as interrupt source  */
		/* PH0 - PH21, PI10 - PI19 (all in mux6 mode) */
		/* A-0 B-1 C-2 D-3 E-4 F-5 G-6 H-7 I-8 S-9    */

		static struct gpio_eint_data a10[] = {
		{7,  0, 6, -1}, {7,  1, 6, -1}, {7,  2, 6, -1}, {7,  3, 6, -1},
		{7,  4, 6, -1}, {7,  5, 6, -1}, {7,  6, 6, -1}, {7,  7, 6, -1},
		{7,  8, 6, -1}, {7,  9, 6, -1}, {7, 10, 6, -1}, {7, 11, 6, -1},
		{7, 12, 6, -1}, {7, 13, 6, -1}, {7, 14, 6, -1}, {7, 15, 6, -1},
		{7, 16, 6, -1}, {7, 17, 6, -1}, {7, 18, 6, -1}, {7, 19, 6, -1},
		{7, 20, 6, -1}, {7, 21, 6, -1},
		{8, 10, 6, -1}, {8, 11, 6, -1}, {8, 12, 6, -1}, {8, 13, 6, -1},
		{8, 14, 6, -1}, {8, 15, 6, -1}, {8, 16, 6, -1}, {8, 17, 6, -1},
		{8, 18, 6, -1}, {8, 19, 6, -1},

		{-1, -1, -1, -1},
		};

		gpio_eint_list = a10;
		gpio_eint_count = 32;
	} else if (sunxi_is_a13()) {
		/* Pins that can be used as interrupt source  */
		/* PG00 - PG04, PG09 - PG12, PE00 - PE01, PB02 - PB04, PB10 (all in mux6 mode) */
		/* A-0 B-1 C-2 D-3 E-4 F-5 G-6 H-7 I-8 S-9    */

		static struct gpio_eint_data a13[] = {
		{6,  0, 6,  0}, {6,  1, 6,  1}, {6,  2, 6,  2}, {6,  3, 6,  3},
		{6,  4, 6,  4}, {-1,-1,-1,  5}, {-1,-1,-1,  6}, {-1,-1,-1,  7},
		{-1,-1,-1,  8}, {6,  9, 6,  9}, {6, 10, 6, 10}, {6, 11, 6, 11},
		{6, 12, 6, 12}, {-1,-1,-1, 13}, {4,  0, 6, 14}, {4,  1, 6, 15},
		{1,  2, 6, 16}, {1,  3, 6, 17}, {1,  4, 6, 18}, {-1,-1,-1, 19},
		{-1,-1,-1, 20}, {-1,-1,-1, 21}, {-1,-1,-1, 22}, {-1,-1,-1, 23},
		{1, 10, 6, 24}, {-1,-1,-1, 25}, {-1,-1,-1, 26}, {-1,-1,-1, 27},
		{-1,-1,-1, 28}, {-1,-1,-1, 29}, {-1,-1,-1, 30}, {-1,-1,-1, 31},

		{-1, -1, -1, -1},
		};

		gpio_eint_list = a13;
		gpio_eint_count = 32;
	} else {
		static struct gpio_eint_data none[] = {
		{-1, -1, -1, -1},
		};

		gpio_eint_list = none;
		gpio_eint_count = 0;
	}
}

/* IRQ handler - redirect interrupts to virtual irq chip */
static irqreturn_t sunxi_gpio_irq_handler(int irq, void *devid)
{
	__u32 status = 0;
	int i = 0;
	struct sunxi_gpio_chip *sgpio = devid;
	status = readl(sgpio->gaddr + PIO_INT_STAT_OFFSET);

	for (i = 0; i < EINT_NUM; i++) {
		if ((status & (1 << i)) &&
		    (gpio_eint_list[i].gpio >= 0)) {
			status &= ~(1 << i);
			SUNXI_CLEAR_EINT(sgpio->gaddr, i);
			generic_handle_irq(sgpio->irq_base + i);
		}
	}

	if (status)
		return IRQ_NONE;

	return IRQ_HANDLED;
}

static int __devinit sunxi_gpio_irq_init(struct sunxi_gpio_chip *sgpio)
{
	int irq;
	int base = sgpio->irq_base;

	/* irq_base < 0 on unsupported platforms */
	if (base < 0)
		return 0;

	for (irq = base; irq < base + EINT_NUM; irq++) {
		irq_set_chip_data(irq, sgpio);
		irq_set_chip(irq, &sunxi_gpio_irq_chip);
		irq_set_handler(irq, handle_simple_irq);
		irq_modify_status(irq, IRQ_NOREQUEST | IRQ_NOAUTOEN,
				  IRQ_NOPROBE);
	}

	return 0;
}

static void sunxi_gpio_irq_remove(struct sunxi_gpio_chip *sgpio)
{
	int irq;
	int base = sgpio->irq_base;

	/* irq_base < 0 on unsupported platforms */
	if (base < 0)
		return;

	for (irq = base; irq < base + EINT_NUM; irq++) {
		irq_set_handler(irq, NULL);
		irq_set_chip(irq, NULL);
		irq_set_chip_data(irq, NULL);
		irq_free_desc(irq);
	}
}

static int __devinit sunxi_gpio_probe(struct platform_device *pdev)
{
	int i;
	int err = 0;
	int names_size = 0;
	int gpio_used = 0;
	int gpio_num = 0;
	struct sunxi_gpio_data *gpio_i = NULL;
	struct sunxi_gpio_data *gpio_data = NULL;
	struct sunxi_gpio_chip *sunxi_chip = NULL;
	char **pnames = NULL;

	/* parse script.bin for [gpio_para] section
	   gpio_used/gpio_num/gpio_pin_x */

	pr_info("sunxi_gpio driver init ver %s\n", SUNXI_GPIO_VER);
	err = script_parser_fetch("gpio_para", "gpio_used", &gpio_used,
					sizeof(gpio_used)/sizeof(int));
	if (err) {
		/* Not error - just info */
		pr_info("%s can't find script.bin '[gpio_para]' 'gpio_used'\n",
			__func__);
		return err;
	}

	if (!gpio_used) {
		pr_info("%s gpio_used is false. Skip gpio initialization\n",
			__func__);
		err = 0;
		return err;
	}

	err = script_parser_fetch("gpio_para", "gpio_num", &gpio_num,
					sizeof(gpio_num)/sizeof(int));
	if (err) {
		pr_err("%s script_parser_fetch '[gpio_para]' 'gpio_num' err\n",
			__func__);
		return err;
	}

	if (!gpio_num) {
		pr_info("%s gpio_num is none. Skip gpio initialization\n",
			__func__);
		err = 0;
		return err;
	}

	/* Allocate memory for sunxi_gpio_chip + data/names array */
	sunxi_chip = kzalloc(sizeof(struct sunxi_gpio_chip) +
				sizeof(struct sunxi_gpio_data) * gpio_num,
				GFP_KERNEL);
	gpio_data = (void *)sunxi_chip + sizeof(struct sunxi_gpio_chip);

	/* Allocate memory for variable array of fixed size strings */
	/* in one chunk. This is to avoid 1+gpio_num kzalloc calls */
	names_size = sizeof(*pnames) * gpio_num +
		     sizeof(char) * MAX_GPIO_NAMELEN * gpio_num;

	pnames = kzalloc(names_size, GFP_KERNEL);
	for (i = 0; i < gpio_num; i++) {
		pnames[i] = (void *)pnames + sizeof(*pnames) * gpio_num +
				i * MAX_GPIO_NAMELEN;
	}

	if ((!pnames) || (!sunxi_chip)) {
		pr_err("%s kzalloc failed\n", __func__);
		err = -ENOMEM;
		goto exit;
	}

	/* Parse gpio_para/pin script data */
	gpio_i = gpio_data;
	for (i = 0; i < gpio_num; i++) {

		sprintf(gpio_i->pin_name, "gpio_pin_%d", i+1);
		err = script_parser_fetch("gpio_para", gpio_i->pin_name,
					(int *)&gpio_i->info,
					sizeof(script_gpio_set_t));

		if (err) {
			pr_err("%s script_parser_fetch '[gpio_para]' '%s' err\n",
				__func__, gpio_i->pin_name);
			break;
		}

		gpio_i++;
	}

	sunxi_chip->gaddr = ioremap(PIO_BASE_ADDRESS, PIO_RANGE_SIZE);
	if (!sunxi_chip->gaddr) {
		pr_err("Can't request gpio registers memory\n");
		err = -EIO;
		goto unmap;
	}

	sunxi_chip->dev		= &pdev->dev;
	sunxi_chip->data	= gpio_data;
	sunxi_chip->chip	= template_chip;
	sunxi_chip->chip.ngpio	= gpio_num;
	sunxi_chip->chip.dev	= &pdev->dev;
	sunxi_chip->chip.label	= "A1X_GPIO";
	sunxi_chip->chip.base	= 1;
	sunxi_chip->chip.names	= (const char *const *)pnames;
	sunxi_chip->irq_base	= -1;

	/* configure EINTs for the detected SoC */
	sunxi_gpio_eint_probe();

	/* This needs additional system irq numbers (NR_IRQ=NR_IRQ+EINT_NUM) */
	if (EINT_NUM > 0) {
		sunxi_chip->irq_base = irq_alloc_descs(-1, 0, EINT_NUM, 0);
		if (sunxi_chip->irq_base < 0) {
			pr_err("Couldn't allocate virq numbers. GPIO irq support disabled\n");
			err = sunxi_chip->irq_base;
		}
	} else
		pr_info("GPIO irq support disabled in this platform\n");

	spin_lock_init(&sunxi_chip->irq_lock);
	sunxi_gpio_irq_init(sunxi_chip);

	if (sunxi_chip->irq_base >= 0) {
		err = request_irq(GPIO_IRQ_NO, sunxi_gpio_irq_handler,
				  IRQF_SHARED, "sunxi-gpio", sunxi_chip);
		if (err) {
			pr_err("Can't request irq %d\n", GPIO_IRQ_NO);
			goto irqchip;
		}
	}

	err = gpiochip_add(&sunxi_chip->chip);
	if (err < 0)
		goto irqhdl;

	platform_set_drvdata(pdev, sunxi_chip);
	return 0;

irqhdl:
	if (sunxi_chip->irq_base >= 0)
		free_irq(GPIO_IRQ_NO, sunxi_chip);
irqchip:
	sunxi_gpio_irq_remove(sunxi_chip);
	if (sunxi_chip->irq_base >= 0)
		irq_free_descs(sunxi_chip->irq_base, EINT_NUM);
unmap:
	iounmap(sunxi_chip->gaddr);
exit:
	kfree(sunxi_chip);
	kfree(pnames);

	return err;
}

static int __devexit sunxi_gpio_remove(struct platform_device *pdev)
{
	int ret = 0;
	struct sunxi_gpio_chip *sunxi_chip = platform_get_drvdata(pdev);
	pr_info("sunxi_gpio driver exit\n");

	ret = gpiochip_remove(&sunxi_chip->chip);
	if (ret < 0)
		pr_err("%s(): gpiochip_remove() failed, ret=%d\n",
			__func__, ret);

	if (sunxi_chip->irq_base >= 0)
		free_irq(GPIO_IRQ_NO, sunxi_chip);

	sunxi_gpio_irq_remove(sunxi_chip);
	irq_free_descs(sunxi_chip->irq_base, EINT_NUM);
	iounmap(sunxi_chip->gaddr);
	kfree(sunxi_chip->chip.names);
	kfree(sunxi_chip);

	return 0;
}

static void sunxi_gpiodev_release(struct device *dev)
{
	/* Nothing */
}

struct platform_device sunxi_gpio_device = {
	.name		= "gpio-sunxi",
	.id		= -1,
	.dev = {
		.release = sunxi_gpiodev_release,
		},
};

static struct platform_driver sunxi_gpio_driver = {
	.driver.name	= "gpio-sunxi",
	.driver.owner	= THIS_MODULE,
	.probe		= sunxi_gpio_probe,
	.remove		= __devexit_p(sunxi_gpio_remove),
};

static int __init sunxi_gpio_init(void)
{
	int err = 0;
	err = platform_device_register(&sunxi_gpio_device);
	if (err)
		goto exit;

	return platform_driver_register(&sunxi_gpio_driver);

exit:
	return err;
}
subsys_initcall(sunxi_gpio_init);

static void __exit sunxi_gpio_exit(void)
{
	platform_driver_unregister(&sunxi_gpio_driver);
	platform_device_unregister(&sunxi_gpio_device);
}
module_exit(sunxi_gpio_exit);

MODULE_AUTHOR("Alexandr Shutko <alex@shutko.ru>");
MODULE_DESCRIPTION("GPIO interface for Allwinner A1X SOCs");
MODULE_LICENSE("GPL");
