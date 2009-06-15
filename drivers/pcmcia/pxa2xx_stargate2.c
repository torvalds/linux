/*
 * linux/drivers/pcmcia/pxa2xx_stargate2.c
 *
 * Stargate 2 PCMCIA specific routines.
 *
 * Created:	December 6, 2005
 * Author:	Ed C. Epp
 * Copyright:	Intel Corp 2005
 *              Jonathan Cameron <jic23@cam.ac.uk> 2009
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <pcmcia/ss.h>

#include <asm/irq.h>
#include <asm/mach-types.h>

#include "soc_common.h"

#define SG2_S0_BUFF_CTL		120
#define SG2_S0_POWER_CTL	108
#define SG2_S0_GPIO_RESET	82
#define SG2_S0_GPIO_DETECT	53
#define SG2_S0_GPIO_READY	81

static struct pcmcia_irqs irqs[] = {
	{ 0, IRQ_GPIO(SG2_S0_GPIO_DETECT), "PCMCIA0 CD" },
};

static int sg2_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	skt->irq = IRQ_GPIO(SG2_S0_GPIO_READY);
	return soc_pcmcia_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void sg2_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	soc_pcmcia_free_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void sg2_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
				    struct pcmcia_state *state)
{
	state->detect = !gpio_get_value(SG2_S0_GPIO_DETECT);
	state->ready  = !!gpio_get_value(SG2_S0_GPIO_READY);
	state->bvd1   = 0; /* not available - battery detect on card */
	state->bvd2   = 0; /* not available */
	state->vs_3v  = 1; /* not available - voltage detect for card */
	state->vs_Xv  = 0; /* not available */
	state->wrprot = 0; /* not available - write protect */
}

static int sg2_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				       const socket_state_t *state)
{
	/* Enable card power */
	switch (state->Vcc) {
	case 0:
		/* sets power ctl register high */
		gpio_set_value(SG2_S0_POWER_CTL, 1);
		break;
	case 33:
	case 50:
		/* sets power control register low (clear) */
		gpio_set_value(SG2_S0_POWER_CTL, 0);
		msleep(100);
		break;
	default:
		pr_err("%s(): bad Vcc %u\n",
		       __func__, state->Vcc);
		return -1;
	}

	/* reset */
	gpio_set_value(SG2_S0_GPIO_RESET, !!(state->flags & SS_RESET));

	return 0;
}

static void sg2_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
	soc_pcmcia_enable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void sg2_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
	soc_pcmcia_disable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static struct pcmcia_low_level sg2_pcmcia_ops __initdata = {
	.owner			= THIS_MODULE,
	.hw_init		= sg2_pcmcia_hw_init,
	.hw_shutdown		= sg2_pcmcia_hw_shutdown,
	.socket_state		= sg2_pcmcia_socket_state,
	.configure_socket	= sg2_pcmcia_configure_socket,
	.socket_init		= sg2_pcmcia_socket_init,
	.socket_suspend		= sg2_pcmcia_socket_suspend,
	.nr			= 1,
};

static struct platform_device *sg2_pcmcia_device;

static int __init sg2_pcmcia_init(void)
{
	int ret;

	if (!machine_is_stargate2())
		return -ENODEV;

	sg2_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);
	if (!sg2_pcmcia_device)
		return -ENOMEM;

	ret = gpio_request(SG2_S0_BUFF_CTL, "SG2 CF buff ctl");
	if (ret)
		goto error_put_platform_device;
	ret = gpio_request(SG2_S0_POWER_CTL, "SG2 CF power ctl");
	if (ret)
		goto error_free_gpio_buff_ctl;
	ret = gpio_request(SG2_S0_GPIO_RESET, "SG2 CF reset");
	if (ret)
		goto error_free_gpio_power_ctl;
	/* Set gpio directions */
	gpio_direction_output(SG2_S0_BUFF_CTL, 0);
	gpio_direction_output(SG2_S0_POWER_CTL, 1);
	gpio_direction_output(SG2_S0_GPIO_RESET, 1);

	ret = platform_device_add_data(sg2_pcmcia_device,
				       &sg2_pcmcia_ops,
				       sizeof(sg2_pcmcia_ops));
	if (ret)
		goto error_free_gpio_reset;

	ret = platform_device_add(sg2_pcmcia_device);
	if (ret)
		goto error_free_gpio_reset;

	return 0;
error_free_gpio_reset:
	gpio_free(SG2_S0_GPIO_RESET);
error_free_gpio_power_ctl:
	gpio_free(SG2_S0_POWER_CTL);
error_free_gpio_buff_ctl:
	gpio_free(SG2_S0_BUFF_CTL);
error_put_platform_device:
	platform_device_put(sg2_pcmcia_device);

	return ret;
}

static void __exit sg2_pcmcia_exit(void)
{
	platform_device_unregister(sg2_pcmcia_device);
	gpio_free(SG2_S0_BUFF_CTL);
	gpio_free(SG2_S0_POWER_CTL);
	gpio_free(SG2_S0_GPIO_RESET);
}

fs_initcall(sg2_pcmcia_init);
module_exit(sg2_pcmcia_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa2xx-pcmcia");
