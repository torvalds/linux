// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2012 Paul Parsons <lost.distance@yahoo.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/irq.h>

#include <asm/mach-types.h>
#include "hx4700.h"

#include <pcmcia/soc_common.h>

static struct gpio gpios[] = {
	{ GPIO114_HX4700_CF_RESET,    GPIOF_OUT_INIT_LOW,   "CF reset"        },
	{ EGPIO4_CF_3V3_ON,           GPIOF_OUT_INIT_LOW,   "CF 3.3V enable"  },
};

static int hx4700_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	int ret;

	ret = gpio_request_array(gpios, ARRAY_SIZE(gpios));
	if (ret)
		goto out;

	/*
	 * IRQ type must be set before soc_pcmcia_hw_init() calls request_irq().
	 * The asic3 default IRQ type is level trigger low level detect, exactly
	 * the the signal present on GPIOD4_CF_nCD when a CF card is inserted.
	 * If the IRQ type is not changed, the asic3 interrupt handler will loop
	 * repeatedly because it is unable to clear the level trigger interrupt.
	 */
	irq_set_irq_type(gpio_to_irq(GPIOD4_CF_nCD), IRQ_TYPE_EDGE_BOTH);

	skt->stat[SOC_STAT_CD].gpio = GPIOD4_CF_nCD;
	skt->stat[SOC_STAT_CD].name = "PCMCIA CD";
	skt->stat[SOC_STAT_RDY].gpio = GPIO60_HX4700_CF_RNB;
	skt->stat[SOC_STAT_RDY].name = "PCMCIA Ready";

out:
	return ret;
}

static void hx4700_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	gpio_free_array(gpios, ARRAY_SIZE(gpios));
}

static void hx4700_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
	struct pcmcia_state *state)
{
	state->vs_3v = 1;
	state->vs_Xv = 0;
}

static int hx4700_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
	const socket_state_t *state)
{
	switch (state->Vcc) {
	case 0:
		gpio_set_value(EGPIO4_CF_3V3_ON, 0);
		break;
	case 33:
		gpio_set_value(EGPIO4_CF_3V3_ON, 1);
		break;
	default:
		printk(KERN_ERR "pcmcia: Unsupported Vcc: %d\n", state->Vcc);
		return -EINVAL;
	}

	gpio_set_value(GPIO114_HX4700_CF_RESET, (state->flags & SS_RESET) != 0);

	return 0;
}

static struct pcmcia_low_level hx4700_pcmcia_ops = {
	.owner          = THIS_MODULE,
	.nr             = 1,
	.hw_init        = hx4700_pcmcia_hw_init,
	.hw_shutdown    = hx4700_pcmcia_hw_shutdown,
	.socket_state   = hx4700_pcmcia_socket_state,
	.configure_socket = hx4700_pcmcia_configure_socket,
};

static struct platform_device *hx4700_pcmcia_device;

static int __init hx4700_pcmcia_init(void)
{
	struct platform_device *pdev;

	if (!machine_is_h4700())
		return -ENODEV;

	pdev = platform_device_register_data(NULL, "pxa2xx-pcmcia", -1,
		&hx4700_pcmcia_ops, sizeof(hx4700_pcmcia_ops));
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	hx4700_pcmcia_device = pdev;

	return 0;
}

static void __exit hx4700_pcmcia_exit(void)
{
	platform_device_unregister(hx4700_pcmcia_device);
}

module_init(hx4700_pcmcia_init);
module_exit(hx4700_pcmcia_exit);

MODULE_AUTHOR("Paul Parsons <lost.distance@yahoo.com>");
MODULE_DESCRIPTION("HP iPAQ hx4700 PCMCIA driver");
MODULE_LICENSE("GPL");
