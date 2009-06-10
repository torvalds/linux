/*
 * s6105 control routines
 *
 * Copyright (c) 2009 emlix GmbH
 */
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <asm/bootparam.h>

#include <variant/hardware.h>
#include <variant/gpio.h>

#include <platform/gpio.h>

void platform_halt(void)
{
	local_irq_disable();
	while (1)
		;
}

void platform_power_off(void)
{
	platform_halt();
}

void platform_restart(void)
{
	platform_halt();
}

void __init platform_setup(char **cmdline)
{
	unsigned long reg;

	reg = readl(S6_REG_GREG1 + S6_GREG1_CLKGATE);
	reg &= ~(1 << S6_GREG1_BLOCK_SB);
	writel(reg, S6_REG_GREG1 + S6_GREG1_CLKGATE);

	reg = readl(S6_REG_GREG1 + S6_GREG1_BLOCKENA);
	reg |= 1 << S6_GREG1_BLOCK_SB;
	writel(reg, S6_REG_GREG1 + S6_GREG1_BLOCKENA);

	printk(KERN_NOTICE "S6105 on Stretch S6000 - "
		"Copyright (C) 2009 emlix GmbH <info@emlix.com>\n");
}

void __init platform_init(bp_tag_t *first)
{
	s6_gpio_init();
	gpio_request(GPIO_LED1_NGREEN, "led1_green");
	gpio_request(GPIO_LED1_RED, "led1_red");
	gpio_direction_output(GPIO_LED1_NGREEN, 1);
}

void platform_heartbeat(void)
{
	static unsigned int c;

	if (!(++c & 0x4F))
		gpio_direction_output(GPIO_LED1_RED, !(c & 0x10));
}
