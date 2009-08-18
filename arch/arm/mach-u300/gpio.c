/*
 *
 * arch/arm/mach-u300/gpio.c
 *
 *
 * Copyright (C) 2007-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * U300 GPIO module.
 * This can driver either of the two basic GPIO cores
 * available in the U300 platforms:
 * COH 901 335   - Used in DB3150 (U300 1.0) and DB3200 (U330 1.0)
 * COH 901 571/3 - Used in DB3210 (U365 2.0) and DB3350 (U335 1.0)
 * Notice that you also have inline macros in <asm-arch/gpio.h>
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com>
 *
 */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

/* Need access to SYSCON registers for PADmuxing */
#include <mach/syscon.h>

#include "padmux.h"

/* Reference to GPIO block clock */
static struct clk *clk;

/* Memory resource */
static struct resource *memres;
static void __iomem *virtbase;
static struct device *gpiodev;

struct u300_gpio_port {
	const char *name;
	int irq;
	int number;
};


static struct u300_gpio_port gpio_ports[] = {
	{
		.name = "gpio0",
		.number = 0,
	},
	{
		.name = "gpio1",
		.number = 1,
	},
	{
		.name = "gpio2",
		.number = 2,
	},
#ifdef U300_COH901571_3
	{
		.name = "gpio3",
		.number = 3,
	},
	{
		.name = "gpio4",
		.number = 4,
	},
#ifdef CONFIG_MACH_U300_BS335
	{
		.name = "gpio5",
		.number = 5,
	},
	{
		.name = "gpio6",
		.number = 6,
	},
#endif
#endif

};


#ifdef U300_COH901571_3

/* Default input value */
#define DEFAULT_OUTPUT_LOW   0
#define DEFAULT_OUTPUT_HIGH  1

/* GPIO Pull-Up status */
#define DISABLE_PULL_UP  0
#define ENABLE_PULL_UP  1

#define GPIO_NOT_USED 0
#define GPIO_IN       1
#define GPIO_OUT      2

struct u300_gpio_configuration_data {
	unsigned char pin_usage;
	unsigned char default_output_value;
	unsigned char pull_up;
};

/* Initial configuration */
const struct u300_gpio_configuration_data
u300_gpio_config[U300_GPIO_NUM_PORTS][U300_GPIO_PINS_PER_PORT] = {
#ifdef CONFIG_MACH_U300_BS335
	/* Port 0, pins 0-7 */
	{
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_HIGH,  DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP}
	},
	/* Port 1, pins 0-7 */
	{
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_HIGH,  DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP}
	},
	/* Port 2, pins 0-7 */
	{
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP}
	},
	/* Port 3, pins 0-7 */
	{
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP}
	},
	/* Port 4, pins 0-7 */
	{
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP}
	},
	/* Port 5, pins 0-7 */
	{
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP}
	},
	/* Port 6, pind 0-7 */
	{
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP}
	}
#endif

#ifdef CONFIG_MACH_U300_BS365
	/* Port 0, pins 0-7 */
	{
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP}
	},
	/* Port 1, pins 0-7 */
	{
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_HIGH,  DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP}
	},
	/* Port 2, pins 0-7 */
	{
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,   DISABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP}
	},
	/* Port 3, pins 0-7 */
	{
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP}
	},
	/* Port 4, pins 0-7 */
	{
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_IN,  DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		/* These 4 pins doesn't exist on DB3210 */
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP},
		{GPIO_OUT, DEFAULT_OUTPUT_LOW,    ENABLE_PULL_UP}
	}
#endif
};
#endif


/* No users == we can power down GPIO */
static int gpio_users;

struct gpio_struct {
	int (*callback)(void *);
	void *data;
	int users;
};

static struct gpio_struct gpio_pin[U300_GPIO_MAX];

/*
 * Let drivers register callback in order to get notified when there is
 * an interrupt on the gpio pin
 */
int gpio_register_callback(unsigned gpio, int (*func)(void *arg), void *data)
{
	if (gpio_pin[gpio].callback)
		dev_warn(gpiodev, "%s: WARNING: callback already "
			 "registered for gpio pin#%d\n", __func__, gpio);
	gpio_pin[gpio].callback = func;
	gpio_pin[gpio].data = data;

	return 0;
}
EXPORT_SYMBOL(gpio_register_callback);

int gpio_unregister_callback(unsigned gpio)
{
	if (!gpio_pin[gpio].callback)
		dev_warn(gpiodev, "%s: WARNING: callback already "
			 "unregistered for gpio pin#%d\n", __func__, gpio);
	gpio_pin[gpio].callback = NULL;
	gpio_pin[gpio].data = NULL;

	return 0;
}
EXPORT_SYMBOL(gpio_unregister_callback);

int gpio_request(unsigned gpio, const char *label)
{
	if (gpio_pin[gpio].users)
		return -EINVAL;
	else
		gpio_pin[gpio].users++;

	gpio_users++;

	return 0;
}
EXPORT_SYMBOL(gpio_request);

void gpio_free(unsigned gpio)
{
	gpio_users--;
	gpio_pin[gpio].users--;
	if (unlikely(gpio_pin[gpio].users < 0)) {
		dev_warn(gpiodev, "warning: gpio#%d release mismatch\n",
			 gpio);
		gpio_pin[gpio].users = 0;
	}

	return;
}
EXPORT_SYMBOL(gpio_free);

/* This returns zero or nonzero */
int gpio_get_value(unsigned gpio)
{
	return readl(virtbase + U300_GPIO_PXPDIR +
	  PIN_TO_PORT(gpio) * U300_GPIO_PORTX_SPACING) & (1 << (gpio & 0x07));
}
EXPORT_SYMBOL(gpio_get_value);

/*
 * We hope that the compiler will optimize away the unused branch
 * in case "value" is a constant
 */
void gpio_set_value(unsigned gpio, int value)
{
	u32 val;
	unsigned long flags;

	local_irq_save(flags);
	if (value) {
		/* set */
		val = readl(virtbase + U300_GPIO_PXPDOR +
		  PIN_TO_PORT(gpio) * U300_GPIO_PORTX_SPACING)
		  & (1 << (gpio & 0x07));
		writel(val | (1 << (gpio & 0x07)), virtbase +
		  U300_GPIO_PXPDOR +
		  PIN_TO_PORT(gpio) * U300_GPIO_PORTX_SPACING);
	} else {
		/* clear */
		val = readl(virtbase + U300_GPIO_PXPDOR +
		  PIN_TO_PORT(gpio) * U300_GPIO_PORTX_SPACING)
		  & (1 << (gpio & 0x07));
		writel(val & ~(1 << (gpio & 0x07)), virtbase +
		  U300_GPIO_PXPDOR +
		  PIN_TO_PORT(gpio) * U300_GPIO_PORTX_SPACING);
	}
	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_set_value);

int gpio_direction_input(unsigned gpio)
{
	unsigned long flags;
	u32 val;

	if (gpio > U300_GPIO_MAX)
		return -EINVAL;

	local_irq_save(flags);
	val = readl(virtbase + U300_GPIO_PXPCR + PIN_TO_PORT(gpio) *
				U300_GPIO_PORTX_SPACING);
	/* Mask out this pin*/
	val &= ~(U300_GPIO_PXPCR_PIN_MODE_MASK << ((gpio & 0x07) << 1));
	/* This is not needed since it sets the bits to zero.*/
	/* val |= (U300_GPIO_PXPCR_PIN_MODE_INPUT << (gpio*2)); */
	writel(val, virtbase + U300_GPIO_PXPCR + PIN_TO_PORT(gpio) *
				U300_GPIO_PORTX_SPACING);
	local_irq_restore(flags);
	return 0;
}
EXPORT_SYMBOL(gpio_direction_input);

int gpio_direction_output(unsigned gpio, int value)
{
	unsigned long flags;
	u32 val;

	if (gpio > U300_GPIO_MAX)
		return -EINVAL;

	local_irq_save(flags);
	val = readl(virtbase + U300_GPIO_PXPCR + PIN_TO_PORT(gpio) *
				U300_GPIO_PORTX_SPACING);
	/* Mask out this pin */
	val &= ~(U300_GPIO_PXPCR_PIN_MODE_MASK << ((gpio & 0x07) << 1));
	/*
	 * FIXME: configure for push/pull, open drain or open source per pin
	 * in setup. The current driver will only support push/pull.
	 */
	val |= (U300_GPIO_PXPCR_PIN_MODE_OUTPUT_PUSH_PULL
			<< ((gpio & 0x07) << 1));
	writel(val, virtbase + U300_GPIO_PXPCR + PIN_TO_PORT(gpio) *
				U300_GPIO_PORTX_SPACING);
	gpio_set_value(gpio, value);
	local_irq_restore(flags);
	return 0;
}
EXPORT_SYMBOL(gpio_direction_output);

/*
 * Enable an IRQ, edge is rising edge (!= 0) or falling edge (==0).
 */
void enable_irq_on_gpio_pin(unsigned gpio, int edge)
{
	u32 val;
	unsigned long flags;
	local_irq_save(flags);

	val = readl(virtbase + U300_GPIO_PXIEN + PIN_TO_PORT(gpio) *
				U300_GPIO_PORTX_SPACING);
	val |= (1 << (gpio & 0x07));
	writel(val, virtbase + U300_GPIO_PXIEN + PIN_TO_PORT(gpio) *
				U300_GPIO_PORTX_SPACING);
	val = readl(virtbase + U300_GPIO_PXICR + PIN_TO_PORT(gpio) *
				U300_GPIO_PORTX_SPACING);
	if (edge)
		val |= (1 << (gpio & 0x07));
	else
		val &= ~(1 << (gpio & 0x07));
	writel(val, virtbase + U300_GPIO_PXICR + PIN_TO_PORT(gpio) *
				U300_GPIO_PORTX_SPACING);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(enable_irq_on_gpio_pin);

void disable_irq_on_gpio_pin(unsigned gpio)
{
	u32 val;
	unsigned long flags;

	local_irq_save(flags);
	val = readl(virtbase + U300_GPIO_PXIEN + PIN_TO_PORT(gpio) *
				U300_GPIO_PORTX_SPACING);
	val &= ~(1 << (gpio & 0x07));
	writel(val, virtbase + U300_GPIO_PXIEN + PIN_TO_PORT(gpio) *
				U300_GPIO_PORTX_SPACING);
	local_irq_restore(flags);
}
EXPORT_SYMBOL(disable_irq_on_gpio_pin);

/* Enable (value == 0) or disable (value == 1) internal pullup */
void gpio_pullup(unsigned gpio, int value)
{
	u32 val;
	unsigned long flags;

	local_irq_save(flags);
	if (value) {
		val = readl(virtbase + U300_GPIO_PXPER + PIN_TO_PORT(gpio) *
					U300_GPIO_PORTX_SPACING);
		writel(val | (1 << (gpio & 0x07)), virtbase + U300_GPIO_PXPER +
				PIN_TO_PORT(gpio) * U300_GPIO_PORTX_SPACING);
	} else {
		val = readl(virtbase + U300_GPIO_PXPER + PIN_TO_PORT(gpio) *
					U300_GPIO_PORTX_SPACING);
		writel(val & ~(1 << (gpio & 0x07)), virtbase + U300_GPIO_PXPER +
				PIN_TO_PORT(gpio) * U300_GPIO_PORTX_SPACING);
	}
	local_irq_restore(flags);
}
EXPORT_SYMBOL(gpio_pullup);

static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
	struct u300_gpio_port *port = dev_id;
	u32 val;
	int pin;

	/* Read event register */
	val = readl(virtbase + U300_GPIO_PXIEV + port->number *
				U300_GPIO_PORTX_SPACING);
	/* Mask with enable register */
	val &= readl(virtbase + U300_GPIO_PXIEV + port->number *
				U300_GPIO_PORTX_SPACING);
	/* Mask relevant bits */
	val &= U300_GPIO_PXIEV_ALL_IRQ_EVENT_MASK;
	/* ACK IRQ (clear event) */
	writel(val, virtbase + U300_GPIO_PXIEV + port->number *
				U300_GPIO_PORTX_SPACING);
	/* Print message */
	while (val != 0) {
		unsigned gpio;

		pin = __ffs(val);
		/* mask off this pin */
		val &= ~(1 << pin);
		gpio = (port->number << 3) + pin;

		if (gpio_pin[gpio].callback)
			(void)gpio_pin[gpio].callback(gpio_pin[gpio].data);
		else
			dev_dbg(gpiodev, "stray GPIO IRQ on line %d\n",
			       gpio);
	}
	return IRQ_HANDLED;
}

static void gpio_set_initial_values(void)
{
#ifdef U300_COH901571_3
	int i, j;
	unsigned long flags;
	u32 val;

	/* Write default values to all pins */
	for (i = 0; i < U300_GPIO_NUM_PORTS; i++) {
		val = 0;
		for (j = 0; j < 8; j++)
			val |= (u32) (u300_gpio_config[i][j].default_output_value != DEFAULT_OUTPUT_LOW) << j;
		local_irq_save(flags);
		writel(val, virtbase + U300_GPIO_PXPDOR + i * U300_GPIO_PORTX_SPACING);
		local_irq_restore(flags);
	}

	/*
	 * Put all pins that are set to either 'GPIO_OUT' or 'GPIO_NOT_USED'
	 * to output and 'GPIO_IN' to input for each port. And initalize
	 * default value on outputs.
	 */
	for (i = 0; i < U300_GPIO_NUM_PORTS; i++) {
		for (j = 0; j < U300_GPIO_PINS_PER_PORT; j++) {
			local_irq_save(flags);
			val = readl(virtbase + U300_GPIO_PXPCR +
					 i * U300_GPIO_PORTX_SPACING);
			/* Mask out this pin */
			val &= ~(U300_GPIO_PXPCR_PIN_MODE_MASK << (j << 1));

			if (u300_gpio_config[i][j].pin_usage != GPIO_IN)
				val |= (U300_GPIO_PXPCR_PIN_MODE_OUTPUT_PUSH_PULL << (j << 1));
			writel(val, virtbase + U300_GPIO_PXPCR +
					 i * U300_GPIO_PORTX_SPACING);
			local_irq_restore(flags);
		}
	}

	/* Enable or disable the internal pull-ups in the GPIO ASIC block */
	for (i = 0; i < U300_GPIO_MAX; i++) {
		val = 0;
		for (j = 0; j < 8; j++)
			val |= (u32)((u300_gpio_config[i][j].pull_up == DISABLE_PULL_UP)) << j;
		local_irq_save(flags);
		writel(val, virtbase + U300_GPIO_PXPER + i * U300_GPIO_PORTX_SPACING);
		local_irq_restore(flags);
	}
#endif
}

static int __init gpio_probe(struct platform_device *pdev)
{
	u32 val;
	int err = 0;
	int i;
	int num_irqs;

	gpiodev = &pdev->dev;
	memset(gpio_pin, 0, sizeof(gpio_pin));

	/* Get GPIO clock */
	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		err = PTR_ERR(clk);
		dev_err(gpiodev, "could not get GPIO clock\n");
		goto err_no_clk;
	}
	err = clk_enable(clk);
	if (err) {
		dev_err(gpiodev, "could not enable GPIO clock\n");
		goto err_no_clk_enable;
	}

	memres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!memres)
		goto err_no_resource;

	if (request_mem_region(memres->start, memres->end - memres->start, "GPIO Controller")
	    == NULL) {
		err = -ENODEV;
		goto err_no_ioregion;
	}

	virtbase = ioremap(memres->start, resource_size(memres));
	if (!virtbase) {
		err = -ENOMEM;
		goto err_no_ioremap;
	}
	dev_info(gpiodev, "remapped 0x%08x to %p\n",
		 memres->start, virtbase);

#ifdef U300_COH901335
	dev_info(gpiodev, "initializing GPIO Controller COH 901 335\n");
	/* Turn on the GPIO block */
	writel(U300_GPIO_CR_BLOCK_CLOCK_ENABLE, virtbase + U300_GPIO_CR);
#endif

#ifdef U300_COH901571_3
	dev_info(gpiodev, "initializing GPIO Controller COH 901 571/3\n");
	val = readl(virtbase + U300_GPIO_CR);
	dev_info(gpiodev, "COH901571/3 block version: %d, " \
	       "number of cores: %d\n",
	       ((val & 0x0000FE00) >> 9),
	       ((val & 0x000001FC) >> 2));
	writel(U300_GPIO_CR_BLOCK_CLKRQ_ENABLE, virtbase + U300_GPIO_CR);
#endif

	/* Set up some padmuxing here */
#ifdef CONFIG_MMC
	pmx_set_mission_mode_mmc();
#endif
#ifdef CONFIG_SPI_PL022
	pmx_set_mission_mode_spi();
#endif

	gpio_set_initial_values();

	for (num_irqs = 0 ; num_irqs < U300_GPIO_NUM_PORTS; num_irqs++) {

		gpio_ports[num_irqs].irq =
			platform_get_irq_byname(pdev,
						gpio_ports[num_irqs].name);

		err = request_irq(gpio_ports[num_irqs].irq,
				  gpio_irq_handler, IRQF_DISABLED,
				  gpio_ports[num_irqs].name,
				  &gpio_ports[num_irqs]);
		if (err) {
			dev_err(gpiodev, "cannot allocate IRQ for %s!\n",
				gpio_ports[num_irqs].name);
			goto err_no_irq;
		}
		/* Turns off PortX_irq_force */
		writel(0x0, virtbase + U300_GPIO_PXIFR +
				 num_irqs * U300_GPIO_PORTX_SPACING);
	}

	return 0;

 err_no_irq:
	for (i = 0; i < num_irqs; i++)
		free_irq(gpio_ports[i].irq, &gpio_ports[i]);
	iounmap(virtbase);
 err_no_ioremap:
	release_mem_region(memres->start, memres->end - memres->start);
 err_no_ioregion:
 err_no_resource:
	clk_disable(clk);
 err_no_clk_enable:
	clk_put(clk);
 err_no_clk:
	dev_info(gpiodev, "module ERROR:%d\n", err);
	return err;
}

static int __exit gpio_remove(struct platform_device *pdev)
{
	int i;

	/* Turn off the GPIO block */
	writel(0x00000000U, virtbase + U300_GPIO_CR);
	for (i = 0 ; i < U300_GPIO_NUM_PORTS; i++)
		free_irq(gpio_ports[i].irq, &gpio_ports[i]);
	iounmap(virtbase);
	release_mem_region(memres->start, memres->end - memres->start);
	clk_disable(clk);
	clk_put(clk);
	return 0;
}

static struct platform_driver gpio_driver = {
	.driver		= {
		.name	= "u300-gpio",
	},
	.remove		= __exit_p(gpio_remove),
};


static int __init u300_gpio_init(void)
{
	return platform_driver_probe(&gpio_driver, gpio_probe);
}

static void __exit u300_gpio_exit(void)
{
	platform_driver_unregister(&gpio_driver);
}

arch_initcall(u300_gpio_init);
module_exit(u300_gpio_exit);

MODULE_AUTHOR("Linus Walleij <linus.walleij@stericsson.com>");

#ifdef U300_COH901571_3
MODULE_DESCRIPTION("ST-Ericsson AB COH 901 571/3 GPIO driver");
#endif

#ifdef U300_COH901335
MODULE_DESCRIPTION("ST-Ericsson AB COH 901 335 GPIO driver");
#endif

MODULE_LICENSE("GPL");
