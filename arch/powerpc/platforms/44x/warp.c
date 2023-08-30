// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PIKA Warp(tm) board specific routines
 *
 * Copyright (c) 2008-2009 PIKA Technologies
 *   Sean MacLennan <smaclennan@pikatech.com>
 */
#include <linux/err.h>
#include <linux/init.h>
#include <linux/of_platform.h>
#include <linux/kthread.h>
#include <linux/leds.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <linux/export.h>

#include <asm/machdep.h>
#include <asm/udbg.h>
#include <asm/time.h>
#include <asm/uic.h>
#include <asm/ppc4xx.h>
#include <asm/dma.h>


static const struct of_device_id warp_of_bus[] __initconst = {
	{ .compatible = "ibm,plb4", },
	{ .compatible = "ibm,opb", },
	{ .compatible = "ibm,ebc", },
	{},
};

static int __init warp_device_probe(void)
{
	of_platform_bus_probe(NULL, warp_of_bus, NULL);
	return 0;
}
machine_device_initcall(warp, warp_device_probe);

define_machine(warp) {
	.name		= "Warp",
	.compatible	= "pika,warp",
	.progress 	= udbg_progress,
	.init_IRQ 	= uic_init_tree,
	.get_irq 	= uic_get_irq,
	.restart	= ppc4xx_reset_system,
};


static int __init warp_post_info(void)
{
	struct device_node *np;
	void __iomem *fpga;
	u32 post1, post2;

	/* Sighhhh... POST information is in the sd area. */
	np = of_find_compatible_node(NULL, NULL, "pika,fpga-sd");
	if (np == NULL)
		return -ENOENT;

	fpga = of_iomap(np, 0);
	of_node_put(np);
	if (fpga == NULL)
		return -ENOENT;

	post1 = in_be32(fpga + 0x40);
	post2 = in_be32(fpga + 0x44);

	iounmap(fpga);

	if (post1 || post2)
		printk(KERN_INFO "Warp POST %08x %08x\n", post1, post2);
	else
		printk(KERN_INFO "Warp POST OK\n");

	return 0;
}


#ifdef CONFIG_SENSORS_AD7414

static LIST_HEAD(dtm_shutdown_list);
static void __iomem *dtm_fpga;

struct dtm_shutdown {
	struct list_head list;
	void (*func)(void *arg);
	void *arg;
};

int pika_dtm_register_shutdown(void (*func)(void *arg), void *arg)
{
	struct dtm_shutdown *shutdown;

	shutdown = kmalloc(sizeof(struct dtm_shutdown), GFP_KERNEL);
	if (shutdown == NULL)
		return -ENOMEM;

	shutdown->func = func;
	shutdown->arg = arg;

	list_add(&shutdown->list, &dtm_shutdown_list);

	return 0;
}

int pika_dtm_unregister_shutdown(void (*func)(void *arg), void *arg)
{
	struct dtm_shutdown *shutdown;

	list_for_each_entry(shutdown, &dtm_shutdown_list, list)
		if (shutdown->func == func && shutdown->arg == arg) {
			list_del(&shutdown->list);
			kfree(shutdown);
			return 0;
		}

	return -EINVAL;
}

#define WARP_GREEN_LED	0
#define WARP_RED_LED	1

static struct gpio_led warp_gpio_led_pins[] = {
	[WARP_GREEN_LED] = {
		.name		= "green",
		.default_state	= LEDS_DEFSTATE_KEEP,
		.gpiod		= NULL, /* to be filled by pika_setup_leds() */
	},
	[WARP_RED_LED] = {
		.name		= "red",
		.default_state	= LEDS_DEFSTATE_KEEP,
		.gpiod		= NULL, /* to be filled by pika_setup_leds() */
	},
};

static struct gpio_led_platform_data warp_gpio_led_data = {
	.leds		= warp_gpio_led_pins,
	.num_leds	= ARRAY_SIZE(warp_gpio_led_pins),
};

static struct platform_device warp_gpio_leds = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data = &warp_gpio_led_data,
	},
};

static irqreturn_t temp_isr(int irq, void *context)
{
	struct dtm_shutdown *shutdown;
	int value = 1;

	local_irq_disable();

	gpiod_set_value(warp_gpio_led_pins[WARP_GREEN_LED].gpiod, 0);

	/* Run through the shutdown list. */
	list_for_each_entry(shutdown, &dtm_shutdown_list, list)
		shutdown->func(shutdown->arg);

	printk(KERN_EMERG "\n\nCritical Temperature Shutdown\n\n");

	while (1) {
		if (dtm_fpga) {
			unsigned reset = in_be32(dtm_fpga + 0x14);
			out_be32(dtm_fpga + 0x14, reset);
		}

		gpiod_set_value(warp_gpio_led_pins[WARP_RED_LED].gpiod, value);
		value ^= 1;
		mdelay(500);
	}

	/* Not reached */
	return IRQ_HANDLED;
}

/*
 * Because green and red power LEDs are normally driven by leds-gpio driver,
 * but in case of critical temperature shutdown we want to drive them
 * ourselves, we acquire both and then create leds-gpio platform device
 * ourselves, instead of doing it through device tree. This way we can still
 * keep access to the gpios and use them when needed.
 */
static int pika_setup_leds(void)
{
	struct device_node *np, *child;
	struct gpio_desc *gpio;
	struct gpio_led *led;
	int led_count = 0;
	int error;
	int i;

	np = of_find_compatible_node(NULL, NULL, "warp-power-leds");
	if (!np) {
		printk(KERN_ERR __FILE__ ": Unable to find leds\n");
		return -ENOENT;
	}

	for_each_child_of_node(np, child) {
		for (i = 0; i < ARRAY_SIZE(warp_gpio_led_pins); i++) {
			led = &warp_gpio_led_pins[i];

			if (!of_node_name_eq(child, led->name))
				continue;

			if (led->gpiod) {
				printk(KERN_ERR __FILE__ ": %s led has already been defined\n",
				       led->name);
				continue;
			}

			gpio = fwnode_gpiod_get_index(of_fwnode_handle(child),
						      NULL, 0, GPIOD_ASIS,
						      led->name);
			error = PTR_ERR_OR_ZERO(gpio);
			if (error) {
				printk(KERN_ERR __FILE__ ": Failed to get %s led gpio: %d\n",
				       led->name, error);
				of_node_put(child);
				goto err_cleanup_pins;
			}

			led->gpiod = gpio;
			led_count++;
		}
	}

	of_node_put(np);

	/* Skip device registration if no leds have been defined */
	if (led_count) {
		error = platform_device_register(&warp_gpio_leds);
		if (error) {
			printk(KERN_ERR __FILE__ ": Unable to add leds-gpio: %d\n",
			       error);
			goto err_cleanup_pins;
		}
	}

	return 0;

err_cleanup_pins:
	for (i = 0; i < ARRAY_SIZE(warp_gpio_led_pins); i++) {
		led = &warp_gpio_led_pins[i];
		gpiod_put(led->gpiod);
		led->gpiod = NULL;
	}
	return error;
}

static void pika_setup_critical_temp(struct device_node *np,
				     struct i2c_client *client)
{
	int irq, rc;

	/* Do this before enabling critical temp interrupt since we
	 * may immediately interrupt.
	 */
	pika_setup_leds();

	/* These registers are in 1 degree increments. */
	i2c_smbus_write_byte_data(client, 2, 65); /* Thigh */
	i2c_smbus_write_byte_data(client, 3,  0); /* Tlow */

	irq = irq_of_parse_and_map(np, 0);
	if (!irq) {
		printk(KERN_ERR __FILE__ ": Unable to get ad7414 irq\n");
		return;
	}

	rc = request_irq(irq, temp_isr, 0, "ad7414", NULL);
	if (rc) {
		printk(KERN_ERR __FILE__
		       ": Unable to request ad7414 irq %d = %d\n", irq, rc);
		return;
	}
}

static inline void pika_dtm_check_fan(void __iomem *fpga)
{
	static int fan_state;
	u32 fan = in_be32(fpga + 0x34) & (1 << 14);

	if (fan_state != fan) {
		fan_state = fan;
		if (fan)
			printk(KERN_WARNING "Fan rotation error detected."
				   " Please check hardware.\n");
	}
}

static int pika_dtm_thread(void __iomem *fpga)
{
	struct device_node *np;
	struct i2c_client *client;

	np = of_find_compatible_node(NULL, NULL, "adi,ad7414");
	if (np == NULL)
		return -ENOENT;

	client = of_find_i2c_device_by_node(np);
	if (client == NULL) {
		of_node_put(np);
		return -ENOENT;
	}

	pika_setup_critical_temp(np, client);

	of_node_put(np);

	printk(KERN_INFO "Warp DTM thread running.\n");

	while (!kthread_should_stop()) {
		int val;

		val = i2c_smbus_read_word_data(client, 0);
		if (val < 0)
			dev_dbg(&client->dev, "DTM read temp failed.\n");
		else {
			s16 temp = swab16(val);
			out_be32(fpga + 0x20, temp);
		}

		pika_dtm_check_fan(fpga);

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}

	return 0;
}

static int __init pika_dtm_start(void)
{
	struct task_struct *dtm_thread;
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "pika,fpga");
	if (np == NULL)
		return -ENOENT;

	dtm_fpga = of_iomap(np, 0);
	of_node_put(np);
	if (dtm_fpga == NULL)
		return -ENOENT;

	/* Must get post info before thread starts. */
	warp_post_info();

	dtm_thread = kthread_run(pika_dtm_thread, dtm_fpga, "pika-dtm");
	if (IS_ERR(dtm_thread)) {
		iounmap(dtm_fpga);
		return PTR_ERR(dtm_thread);
	}

	return 0;
}
machine_late_initcall(warp, pika_dtm_start);

#else /* !CONFIG_SENSORS_AD7414 */

int pika_dtm_register_shutdown(void (*func)(void *arg), void *arg)
{
	return 0;
}

int pika_dtm_unregister_shutdown(void (*func)(void *arg), void *arg)
{
	return 0;
}

machine_late_initcall(warp, warp_post_info);

#endif

EXPORT_SYMBOL(pika_dtm_register_shutdown);
EXPORT_SYMBOL(pika_dtm_unregister_shutdown);
