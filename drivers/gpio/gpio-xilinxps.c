/*
 * Xilinx PS GPIO device driver
 *
 * 2009-2011 (c) Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 675 Mass
 * Ave, Cambridge, MA 02139, USA.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pm_wakeup.h>
#include <linux/slab.h>
#include <asm/mach/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>

#define DRIVER_NAME "xgpiops"
#define XGPIOPS_NR_GPIOS	118

static struct irq_domain *irq_domain;

/* Register offsets for the GPIO device */

#define XGPIOPS_DATA_LSW_OFFSET(BANK)	(0x000 + (8 * BANK)) /* LSW Mask &
								Data -WO */
#define XGPIOPS_DATA_MSW_OFFSET(BANK)	(0x004 + (8 * BANK)) /* MSW Mask &
								Data -WO */
#define XGPIOPS_DATA_OFFSET(BANK)	(0x040 + (4 * BANK)) /* Data Register
								-RW */
#define XGPIOPS_DIRM_OFFSET(BANK)	(0x204 + (0x40 * BANK)) /* Direction
								mode reg-RW */
#define XGPIOPS_OUTEN_OFFSET(BANK)	(0x208 + (0x40 * BANK)) /* Output
								enable reg-RW
								 */
#define XGPIOPS_INTMASK_OFFSET(BANK)	(0x20C + (0x40 * BANK)) /* Interrupt
								mask reg-RO */
#define XGPIOPS_INTEN_OFFSET(BANK)	(0x210 + (0x40 * BANK)) /* Interrupt
								enable reg-WO
								 */
#define XGPIOPS_INTDIS_OFFSET(BANK)	(0x214 + (0x40 * BANK)) /* Interrupt
								disable reg-WO
								 */
#define XGPIOPS_INTSTS_OFFSET(BANK)	(0x218 + (0x40 * BANK)) /* Interrupt
								status reg-RO
								 */
#define XGPIOPS_INTTYPE_OFFSET(BANK)	(0x21C + (0x40 * BANK)) /* Interrupt
								type reg-RW
								 */
#define XGPIOPS_INTPOL_OFFSET(BANK)	(0x220 + (0x40 * BANK)) /* Interrupt
								polarity reg
								-RW */
#define XGPIOPS_INTANY_OFFSET(BANK)	(0x224 + (0x40 * BANK)) /* Interrupt on
								any, reg-RW */

/* Read/Write access to the GPIO PS registers */
#define xgpiops_readreg(offset)		__raw_readl(offset)
#define xgpiops_writereg(val, offset)	__raw_writel(val, offset)

static unsigned int xgpiops_pin_table[] = {
	31, /* 0 - 31 */
	53, /* 32 - 53 */
	85, /* 54 - 85 */
	117 /* 86 - 117 */
};

/**
 * struct xgpiops - gpio device private data structure
 * @chip:	instance of the gpio_chip
 * @base_addr:	base address of the GPIO device
 * @gpio_lock:	lock used for synchronization
 */
struct xgpiops {
	struct gpio_chip chip;
	void __iomem *base_addr;
	unsigned int irq;
	unsigned int irq_base;
	struct clk *clk;
	spinlock_t gpio_lock;
};

/**
 * xgpiops_get_bank_pin - Get the bank number and pin number within that bank
 * for a given pin in the GPIO device
 * @pin_num:	gpio pin number within the device
 * @bank_num:	an output parameter used to return the bank number of the gpio
 *		pin
 * @bank_pin_num: an output parameter used to return pin number within a bank
 *		  for the given gpio pin
 *
 * Returns the bank number.
 */
static inline void xgpiops_get_bank_pin(unsigned int pin_num,
					 unsigned int *bank_num,
					 unsigned int *bank_pin_num)
{
	for (*bank_num = 0; *bank_num < 4; (*bank_num)++)
		if (pin_num <= xgpiops_pin_table[*bank_num])
			break;

	if (*bank_num == 0)
		*bank_pin_num = pin_num;
	else
		*bank_pin_num = pin_num %
					(xgpiops_pin_table[*bank_num - 1] + 1);
}

/**
 * xgpiops_get_value - Get the state of the specified pin of GPIO device
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 *
 * This function reads the state of the specified pin of the GPIO device.
 * It returns 0 if the pin is low, 1 if pin is high.
 */
static int xgpiops_get_value(struct gpio_chip *chip, unsigned int pin)
{
	unsigned int bank_num, bank_pin_num;
	struct xgpiops *gpio = container_of(chip, struct xgpiops, chip);

	xgpiops_get_bank_pin(pin, &bank_num, &bank_pin_num);

	return (xgpiops_readreg(gpio->base_addr +
				 XGPIOPS_DATA_OFFSET(bank_num)) >>
		bank_pin_num) & 1;
}

/**
 * xgpiops_set_value - Modify the state of the pin with specified value
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 * @state:	value used to modify the state of the specified pin
 *
 * This function calculates the register offset (i.e to lower 16 bits or
 * upper 16 bits) based on the given pin number and sets the state of a
 * gpio pin to the specified value. The state is either 0 or non-zero.
 */
static void xgpiops_set_value(struct gpio_chip *chip, unsigned int pin,
			       int state)
{
	unsigned long flags;
	unsigned int reg_offset;
	unsigned int bank_num, bank_pin_num;
	struct xgpiops *gpio = container_of(chip, struct xgpiops, chip);

	xgpiops_get_bank_pin(pin, &bank_num, &bank_pin_num);

	if (bank_pin_num >= 16) {
		bank_pin_num -= 16; /* only 16 data bits in bit maskable reg */
		reg_offset = XGPIOPS_DATA_MSW_OFFSET(bank_num);
	} else {
		reg_offset = XGPIOPS_DATA_LSW_OFFSET(bank_num);
	}

	/*
	 * get the 32 bit value to be written to the mask/data register where
	 * the upper 16 bits is the mask and lower 16 bits is the data
	 */
	if (state)
		state = 1;
	state = ~(1 << (bank_pin_num + 16)) & ((state << bank_pin_num) |
					       0xFFFF0000);

	spin_lock_irqsave(&gpio->gpio_lock, flags);
	xgpiops_writereg(state, gpio->base_addr + reg_offset);
	spin_unlock_irqrestore(&gpio->gpio_lock, flags);
}

/**
 * xgpiops_dir_in - Set the direction of the specified GPIO pin as input
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 *
 * This function uses the read-modify-write sequence to set the direction of
 * the gpio pin as input. Returns 0 always.
 */
static int xgpiops_dir_in(struct gpio_chip *chip, unsigned int pin)
{
	unsigned int reg, bank_num, bank_pin_num;
	struct xgpiops *gpio = container_of(chip, struct xgpiops, chip);

	xgpiops_get_bank_pin(pin, &bank_num, &bank_pin_num);
	/* clear the bit in direction mode reg to set the pin as input */
	reg = xgpiops_readreg(gpio->base_addr + XGPIOPS_DIRM_OFFSET(bank_num));
	reg &= ~(1 << bank_pin_num);
	xgpiops_writereg(reg, gpio->base_addr + XGPIOPS_DIRM_OFFSET(bank_num));

	return 0;
}

/**
 * xgpiops_dir_out - Set the direction of the specified GPIO pin as output
 * @chip:	gpio_chip instance to be worked on
 * @pin:	gpio pin number within the device
 * @state:	value to be written to specified pin
 *
 * This function sets the direction of specified GPIO pin as output, configures
 * the Output Enable register for the pin and uses xgpiops_set to set the state
 * of the pin to the value specified. Returns 0 always.
 */
static int xgpiops_dir_out(struct gpio_chip *chip, unsigned int pin, int state)
{
	struct xgpiops *gpio = container_of(chip, struct xgpiops, chip);
	unsigned int reg, bank_num, bank_pin_num;

	xgpiops_get_bank_pin(pin, &bank_num, &bank_pin_num);

	/* set the GPIO pin as output */
	reg = xgpiops_readreg(gpio->base_addr + XGPIOPS_DIRM_OFFSET(bank_num));
	reg |= 1 << bank_pin_num;
	xgpiops_writereg(reg, gpio->base_addr + XGPIOPS_DIRM_OFFSET(bank_num));

	/* configure the output enable reg for the pin */
	reg = xgpiops_readreg(gpio->base_addr + XGPIOPS_OUTEN_OFFSET(bank_num));
	reg |= 1 << bank_pin_num;
	xgpiops_writereg(reg, gpio->base_addr + XGPIOPS_OUTEN_OFFSET(bank_num));

	/* set the state of the pin */
	xgpiops_set_value(chip, pin, state);
	return 0;
}

static int xgpiops_to_irq(struct gpio_chip *chip, unsigned offset)
{
	return irq_find_mapping(irq_domain, offset);
}

/**
 * xgpiops_irq_ack - Acknowledge the interrupt of a gpio pin
 * @irq_data: irq data containing irq number of gpio pin for the irq to ack
 *
 * This function calculates gpio pin number from irq number and sets the bit
 * in the Interrupt Status Register of the corresponding bank, to ACK the irq.
 */
static void xgpiops_irq_ack(struct irq_data *irq_data)
{
	struct xgpiops *gpio = (struct xgpiops *)
				irq_data_get_irq_chip_data(irq_data);
	unsigned int device_pin_num, bank_num, bank_pin_num;

	device_pin_num = irq_data->hwirq;
	xgpiops_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num);
	xgpiops_writereg(1 << bank_pin_num, gpio->base_addr +
			(XGPIOPS_INTSTS_OFFSET(bank_num)));
}

/**
 * xgpiops_irq_mask - Disable the interrupts for a gpio pin
 * @irq:	irq number of gpio pin for which interrupt is to be disabled
 *
 * This function calculates gpio pin number from irq number and sets the
 * bit in the Interrupt Disable register of the corresponding bank to disable
 * interrupts for that pin.
 */
static void xgpiops_irq_mask(struct irq_data *irq_data)
{
	struct xgpiops *gpio = (struct xgpiops *)
				irq_data_get_irq_chip_data(irq_data);
	unsigned int device_pin_num, bank_num, bank_pin_num;

	device_pin_num = irq_data->hwirq;
	xgpiops_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num);
	xgpiops_writereg(1 << bank_pin_num,
			  gpio->base_addr + XGPIOPS_INTDIS_OFFSET(bank_num));
}

/**
 * xgpiops_irq_unmask - Enable the interrupts for a gpio pin
 * @irq_data: irq data containing irq number of gpio pin for the irq to enable
 *
 * This function calculates the gpio pin number from irq number and sets the
 * bit in the Interrupt Enable register of the corresponding bank to enable
 * interrupts for that pin.
 */
static void xgpiops_irq_unmask(struct irq_data *irq_data)
{
	struct xgpiops *gpio = irq_data_get_irq_chip_data(irq_data);
	unsigned int device_pin_num, bank_num, bank_pin_num;

	device_pin_num = irq_data->hwirq;
	xgpiops_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num);
	xgpiops_writereg(1 << bank_pin_num,
			  gpio->base_addr + XGPIOPS_INTEN_OFFSET(bank_num));
}

/**
 * xgpiops_set_irq_type - Set the irq type for a gpio pin
 * @irq_data:	irq data containing irq number of gpio pin
 * @type:	interrupt type that is to be set for the gpio pin
 *
 * This function gets the gpio pin number and its bank from the gpio pin number
 * and configures the INT_TYPE, INT_POLARITY and INT_ANY registers. Returns 0,
 * negative error otherwise.
 * TYPE-EDGE_RISING,  INT_TYPE - 1, INT_POLARITY - 1,  INT_ANY - 0;
 * TYPE-EDGE_FALLING, INT_TYPE - 1, INT_POLARITY - 0,  INT_ANY - 0;
 * TYPE-EDGE_BOTH,    INT_TYPE - 1, INT_POLARITY - NA, INT_ANY - 1;
 * TYPE-LEVEL_HIGH,   INT_TYPE - 0, INT_POLARITY - 1,  INT_ANY - NA;
 * TYPE-LEVEL_LOW,    INT_TYPE - 0, INT_POLARITY - 0,  INT_ANY - NA
 */
static int xgpiops_set_irq_type(struct irq_data *irq_data, unsigned int type)
{
	struct xgpiops *gpio = irq_data_get_irq_chip_data(irq_data);
	unsigned int device_pin_num, bank_num, bank_pin_num;
	unsigned int int_type, int_pol, int_any;

	device_pin_num = irq_data->hwirq;
	xgpiops_get_bank_pin(device_pin_num, &bank_num, &bank_pin_num);

	int_type = xgpiops_readreg(gpio->base_addr +
				    XGPIOPS_INTTYPE_OFFSET(bank_num));
	int_pol = xgpiops_readreg(gpio->base_addr +
				   XGPIOPS_INTPOL_OFFSET(bank_num));
	int_any = xgpiops_readreg(gpio->base_addr +
				   XGPIOPS_INTANY_OFFSET(bank_num));

	/*
	 * based on the type requested, configure the INT_TYPE, INT_POLARITY
	 * and INT_ANY registers
	 */
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		int_type |= (1 << bank_pin_num);
		int_pol |= (1 << bank_pin_num);
		int_any &= ~(1 << bank_pin_num);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		int_type |= (1 << bank_pin_num);
		int_pol &= ~(1 << bank_pin_num);
		int_any &= ~(1 << bank_pin_num);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		int_type |= (1 << bank_pin_num);
		int_any |= (1 << bank_pin_num);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		int_type &= ~(1 << bank_pin_num);
		int_pol |= (1 << bank_pin_num);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		int_type &= ~(1 << bank_pin_num);
		int_pol &= ~(1 << bank_pin_num);
		break;
	default:
		return -EINVAL;
	}

	xgpiops_writereg(int_type,
			  gpio->base_addr + XGPIOPS_INTTYPE_OFFSET(bank_num));
	xgpiops_writereg(int_pol,
			  gpio->base_addr + XGPIOPS_INTPOL_OFFSET(bank_num));
	xgpiops_writereg(int_any,
			  gpio->base_addr + XGPIOPS_INTANY_OFFSET(bank_num));
	return 0;
}

static int xgpiops_set_wake(struct irq_data *data, unsigned int on)
{
	if (on)
		xgpiops_irq_unmask(data);
	else
		xgpiops_irq_mask(data);

	return 0;
}

/* irq chip descriptor */
static struct irq_chip xgpiops_irqchip = {
	.name		= DRIVER_NAME,
	.irq_ack	= xgpiops_irq_ack,
	.irq_mask	= xgpiops_irq_mask,
	.irq_unmask	= xgpiops_irq_unmask,
	.irq_set_type	= xgpiops_set_irq_type,
	.irq_set_wake	= xgpiops_set_wake,
};

/**
 * xgpiops_irqhandler - IRQ handler for the gpio banks of a gpio device
 * @irq:	irq number of the gpio bank where interrupt has occurred
 * @desc:	irq descriptor instance of the 'irq'
 *
 * This function reads the Interrupt Status Register of each bank to get the
 * gpio pin number which has triggered an interrupt. It then acks the triggered
 * interrupt and calls the pin specific handler set by the higher layer
 * application for that pin.
 * Note: A bug is reported if no handler is set for the gpio pin.
 */
static void xgpiops_irqhandler(unsigned int irq, struct irq_desc *desc)
{
	struct xgpiops *gpio = (struct xgpiops *)irq_get_handler_data(irq);
	int gpio_irq = gpio->irq_base;
	unsigned int int_sts, int_enb, bank_num;
	struct irq_desc *gpio_irq_desc;
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	for (bank_num = 0; bank_num < 4; bank_num++) {
		int_sts = xgpiops_readreg(gpio->base_addr +
					   XGPIOPS_INTSTS_OFFSET(bank_num));
		int_enb = xgpiops_readreg(gpio->base_addr +
					   XGPIOPS_INTMASK_OFFSET(bank_num));
		int_sts &= ~int_enb;

		for (; int_sts != 0; int_sts >>= 1, gpio_irq++) {
			if ((int_sts & 1) == 0)
				continue;
			gpio_irq_desc = irq_to_desc(gpio_irq);
			BUG_ON(!gpio_irq_desc);
			chip = irq_desc_get_chip(gpio_irq_desc);
			BUG_ON(!chip);
			chip->irq_ack(&gpio_irq_desc->irq_data);

			/* call the pin specific handler */
			generic_handle_irq(gpio_irq);
		}
		/* shift to first virtual irq of next bank */
		gpio_irq = gpio->irq_base + xgpiops_pin_table[bank_num] + 1;
	}

	chip = irq_desc_get_chip(desc);
	chained_irq_exit(chip, desc);
}

#ifdef CONFIG_PM_SLEEP
static int xgpiops_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xgpiops *gpio = platform_get_drvdata(pdev);

	if (!device_may_wakeup(dev)) {
		if (!pm_runtime_suspended(dev))
			clk_disable(gpio->clk);
		return 0;
	}

	return 0;
}

static int xgpiops_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xgpiops *gpio = platform_get_drvdata(pdev);

	if (!device_may_wakeup(dev)) {
		if (!pm_runtime_suspended(dev))
			return clk_enable(gpio->clk);
	}

	return 0;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int xgpiops_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xgpiops *gpio = platform_get_drvdata(pdev);

	clk_disable(gpio->clk);

	return 0;
}

static int xgpiops_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct xgpiops *gpio = platform_get_drvdata(pdev);

	return clk_enable(gpio->clk);
}

static int xgpiops_idle(struct device *dev)
{
	return pm_schedule_suspend(dev, 1);
}

static int xgpiops_request(struct gpio_chip *chip, unsigned offset)
{
	int ret;

	ret = pm_runtime_get_sync(chip->dev);

	/*
	 * If the device is already active pm_runtime_get() will return 1 on
	 * success, but gpio_request still needs to return 0.
	 */
	return ret < 0 ? ret : 0;
}

static void xgpiops_free(struct gpio_chip *chip, unsigned offset)
{
	pm_runtime_put_sync(chip->dev);
}

static void xgpiops_pm_runtime_init(struct platform_device *pdev)
{
	struct xgpiops *gpio = platform_get_drvdata(pdev);

	clk_disable(gpio->clk);
	pm_runtime_enable(&pdev->dev);
}

#else /* ! CONFIG_PM_RUNTIME */
#define xgpiops_request	NULL
#define xgpiops_free	NULL
static void xgpiops_pm_runtime_init(struct platform_device *pdev) {}
#endif /* ! CONFIG_PM_RUNTIME */

#if defined(CONFIG_PM_RUNTIME) || defined(CONFIG_PM_SLEEP)
static const struct dev_pm_ops xgpiops_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(xgpiops_suspend, xgpiops_resume)
	SET_RUNTIME_PM_OPS(xgpiops_runtime_suspend, xgpiops_runtime_resume,
			xgpiops_idle)
};
#define XGPIOPS_PM	(&xgpiops_dev_pm_ops)

#else /*! CONFIG_PM_RUNTIME || ! CONFIG_PM_SLEEP */
#define XGPIOPS_PM	NULL
#endif /*! CONFIG_PM_RUNTIME */

/**
 * xgpiops_probe - Initialization method for a xgpiops device
 * @pdev:	platform device instance
 *
 * This function allocates memory resources for the gpio device and registers
 * all the banks of the device. It will also set up interrupts for the gpio
 * pins.
 * Note: Interrupts are disabled for all the banks during initialization.
 * Returns 0 on success, negative error otherwise.
 */
static int xgpiops_probe(struct platform_device *pdev)
{
	int ret;
	unsigned int irq_num;
	struct xgpiops *gpio;
	struct gpio_chip *chip;
	resource_size_t remap_size;
	struct resource *mem_res = NULL;
	int pin_num, bank_num, gpio_irq;

	gpio = kzalloc(sizeof(struct xgpiops), GFP_KERNEL);
	if (!gpio) {
		dev_err(&pdev->dev,
			"couldn't allocate memory for gpio private data\n");
		return -ENOMEM;
	}

	spin_lock_init(&gpio->gpio_lock);

	platform_set_drvdata(pdev, gpio);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_res) {
		dev_err(&pdev->dev, "No memory resource\n");
		ret = -ENODEV;
		goto err_free_gpio;
	}

	remap_size = mem_res->end - mem_res->start + 1;
	if (!request_mem_region(mem_res->start, remap_size, pdev->name)) {
		dev_err(&pdev->dev, "Cannot request IO\n");
		ret = -ENXIO;
		goto err_free_gpio;
	}

	gpio->base_addr = ioremap(mem_res->start, remap_size);
	if (gpio->base_addr == NULL) {
		dev_err(&pdev->dev, "Couldn't ioremap memory at 0x%08lx\n",
			(unsigned long)mem_res->start);
		ret = -ENOMEM;
		goto err_release_region;
	}

	irq_num = platform_get_irq(pdev, 0);
	gpio->irq = irq_num;

	/* configure the gpio chip */
	chip = &gpio->chip;
	chip->label = "xgpiops";
	chip->owner = THIS_MODULE;
	chip->dev = &pdev->dev;
	chip->get = xgpiops_get_value;
	chip->set = xgpiops_set_value;
	chip->request = xgpiops_request;
	chip->free = xgpiops_free;
	chip->direction_input = xgpiops_dir_in;
	chip->direction_output = xgpiops_dir_out;
	chip->to_irq = xgpiops_to_irq;
	chip->dbg_show = NULL;
	chip->base = 0;		/* default pin base */
	chip->ngpio = XGPIOPS_NR_GPIOS;
	chip->can_sleep = 0;

	gpio->irq_base = irq_alloc_descs(-1, 0, chip->ngpio, 0);
	if (gpio->irq_base < 0) {
		dev_err(&pdev->dev, "Couldn't allocate IRQ numbers\n");
		ret = -ENODEV;
		goto err_iounmap;
	}

	irq_domain = irq_domain_add_legacy(pdev->dev.of_node,
					   chip->ngpio, gpio->irq_base, 0,
					   &irq_domain_simple_ops, NULL);

	/* report a bug if gpio chip registration fails */
	ret = gpiochip_add(chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "gpio chip registration failed\n");
		goto err_iounmap;
	} else {
		dev_info(&pdev->dev, "gpio at 0x%08lx mapped to 0x%08lx\n",
			 (unsigned long)mem_res->start,
			 (unsigned long)gpio->base_addr);
	}

	/* Enable GPIO clock */
	gpio->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(gpio->clk)) {
		dev_err(&pdev->dev, "input clock not found.\n");
		ret = PTR_ERR(gpio->clk);
		goto err_chip_remove;
	}
	ret = clk_prepare_enable(gpio->clk);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable clock.\n");
		goto err_clk_put;
	}

	/* disable interrupts for all banks */
	for (bank_num = 0; bank_num < 4; bank_num++) {
		xgpiops_writereg(0xffffffff, gpio->base_addr +
				  XGPIOPS_INTDIS_OFFSET(bank_num));
	}

	/*
	 * set the irq chip, handler and irq chip data for callbacks for
	 * each pin
	 */
	for (pin_num = 0; pin_num < min_t(int, XGPIOPS_NR_GPIOS,
						(int)chip->ngpio); pin_num++) {
		gpio_irq = irq_find_mapping(irq_domain, pin_num);
		irq_set_chip_and_handler(gpio_irq, &xgpiops_irqchip,
							handle_simple_irq);
		irq_set_chip_data(gpio_irq, (void *)gpio);
		set_irq_flags(gpio_irq, IRQF_VALID);
	}

	irq_set_handler_data(irq_num, (void *)gpio);
	irq_set_chained_handler(irq_num, xgpiops_irqhandler);

	xgpiops_pm_runtime_init(pdev);

	device_set_wakeup_capable(&pdev->dev, 1);

	return 0;

err_clk_put:
	clk_put(gpio->clk);
err_chip_remove:
	gpiochip_remove(chip);
err_iounmap:
	iounmap(gpio->base_addr);
err_release_region:
	release_mem_region(mem_res->start, remap_size);
err_free_gpio:
	platform_set_drvdata(pdev, NULL);
	kfree(gpio);

	return ret;
}

static int xgpiops_remove(struct platform_device *pdev)
{
	struct xgpiops *gpio = platform_get_drvdata(pdev);

	clk_disable_unprepare(gpio->clk);
	clk_put(gpio->clk);
	device_set_wakeup_capable(&pdev->dev, 0);
	return 0;
}

static struct of_device_id xgpiops_of_match[] = {
	{ .compatible = "xlnx,ps7-gpio-1.00.a", },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, xgpiops_of_match);

static struct platform_driver xgpiops_driver = {
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.pm	= XGPIOPS_PM,
		.of_match_table = xgpiops_of_match,
	},
	.probe		= xgpiops_probe,
	.remove		= xgpiops_remove,
};

/**
 * xgpiops_init - Initial driver registration call
 */
static int __init xgpiops_init(void)
{
	return platform_driver_register(&xgpiops_driver);
}

postcore_initcall(xgpiops_init);
