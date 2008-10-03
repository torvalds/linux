/*
 * arch/sh/boards/landisk/psw.c
 *
 * push switch support for LANDISK and USL-5P
 *
 * Copyright (C) 2006-2007  Paul Mundt
 * Copyright (C) 2007  kogiidena
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/io.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <mach-landisk/mach/iodata_landisk.h>
#include <asm/push-switch.h>

static irqreturn_t psw_irq_handler(int irq, void *arg)
{
	struct platform_device *pdev = arg;
	struct push_switch *psw = platform_get_drvdata(pdev);
	struct push_switch_platform_info *psw_info = pdev->dev.platform_data;
	unsigned int sw_value;
	int ret = 0;

	sw_value = (0x0ff & (~ctrl_inb(PA_STATUS)));

	/* Nothing to do if there's no state change */
	if (psw->state) {
		ret = 1;
		goto out;
	}

	/* Figure out who raised it */
	if (sw_value & (1 << psw_info->bit)) {
		psw->state = 1;
		mod_timer(&psw->debounce, jiffies + 50);
		ret = 1;
	}

out:
	/* Clear the switch IRQs */
	ctrl_outb(0x00, PA_PWRINT_CLR);

	return IRQ_RETVAL(ret);
}

static struct resource psw_power_resources[] = {
	[0] = {
		.start = IRQ_POWER,
		.flags = IORESOURCE_IRQ,
       },
};

static struct resource psw_usl5p_resources[] = {
	[0] = {
		.start = IRQ_BUTTON,
		.flags = IORESOURCE_IRQ,
	},
};

static struct push_switch_platform_info psw_power_platform_data = {
	.name		= "psw_power",
	.bit		= 4,
	.irq_flags	= IRQF_SHARED,
	.irq_handler	= psw_irq_handler,
};

static struct push_switch_platform_info psw1_platform_data = {
	.name		= "psw1",
	.bit		= 0,
	.irq_flags	= IRQF_SHARED,
	.irq_handler	= psw_irq_handler,
};

static struct push_switch_platform_info psw2_platform_data = {
	.name		= "psw2",
	.bit		= 2,
	.irq_flags	= IRQF_SHARED,
	.irq_handler	= psw_irq_handler,
};

static struct push_switch_platform_info psw3_platform_data = {
	.name		= "psw3",
	.bit		= 1,
	.irq_flags	= IRQF_SHARED,
	.irq_handler	= psw_irq_handler,
};

static struct platform_device psw_power_switch_device = {
	.name		= "push-switch",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(psw_power_resources),
	.resource	= psw_power_resources,
	.dev		= {
		.platform_data = &psw_power_platform_data,
	},
};

static struct platform_device psw1_switch_device = {
	.name		= "push-switch",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(psw_usl5p_resources),
	.resource	= psw_usl5p_resources,
	.dev		= {
		.platform_data = &psw1_platform_data,
	},
};

static struct platform_device psw2_switch_device = {
	.name		= "push-switch",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(psw_usl5p_resources),
	.resource	= psw_usl5p_resources,
	.dev		= {
		.platform_data = &psw2_platform_data,
	},
};

static struct platform_device psw3_switch_device = {
	.name		= "push-switch",
	.id		= 3,
	.num_resources	= ARRAY_SIZE(psw_usl5p_resources),
	.resource	= psw_usl5p_resources,
	.dev = {
		.platform_data = &psw3_platform_data,
	},
};

static struct platform_device *psw_devices[] = {
	&psw_power_switch_device,
	&psw1_switch_device,
	&psw2_switch_device,
	&psw3_switch_device,
};

static int __init psw_init(void)
{
	return platform_add_devices(psw_devices, ARRAY_SIZE(psw_devices));
}
module_init(psw_init);
