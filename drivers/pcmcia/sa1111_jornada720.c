// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/pcmcia/sa1100_jornada720.c
 *
 * Jornada720 PCMCIA specific routines
 *
 */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>

#include "sa1111_generic.h"

/*
 * Socket 0 power: GPIO A0
 * Socket 0 3V: GPIO A2
 * Socket 1 power: GPIO A1 & GPIO A3
 * Socket 1 3V: GPIO A3
 * Does Socket 1 3V actually do anything?
 */
enum {
	J720_GPIO_PWR,
	J720_GPIO_3V,
	J720_GPIO_MAX,
};
struct jornada720_data {
	struct gpio_desc *gpio[J720_GPIO_MAX];
};

static int jornada720_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	struct device *dev = skt->socket.dev.parent;
	struct jornada720_data *j;

	j = devm_kzalloc(dev, sizeof(*j), GFP_KERNEL);
	if (!j)
		return -ENOMEM;

	j->gpio[J720_GPIO_PWR] = devm_gpiod_get(dev, skt->nr ? "s1-power" :
						"s0-power", GPIOD_OUT_LOW);
	if (IS_ERR(j->gpio[J720_GPIO_PWR]))
		return PTR_ERR(j->gpio[J720_GPIO_PWR]);

	j->gpio[J720_GPIO_3V] = devm_gpiod_get(dev, skt->nr ? "s1-3v" :
					       "s0-3v", GPIOD_OUT_LOW);
	if (IS_ERR(j->gpio[J720_GPIO_3V]))
		return PTR_ERR(j->gpio[J720_GPIO_3V]);

	skt->driver_data = j;

	return 0;
}

static int
jornada720_pcmcia_configure_socket(struct soc_pcmcia_socket *skt, const socket_state_t *state)
{
	struct jornada720_data *j = skt->driver_data;
	DECLARE_BITMAP(values, J720_GPIO_MAX) = { 0, };
	int ret;

	printk(KERN_INFO "%s(): config socket %d vcc %d vpp %d\n", __func__,
		skt->nr, state->Vcc, state->Vpp);

	switch (skt->nr) {
	case 0:
		switch (state->Vcc) {
		default:
		case  0:
			__assign_bit(J720_GPIO_PWR, values, 0);
			__assign_bit(J720_GPIO_3V, values, 0);
			break;
		case 33:
			__assign_bit(J720_GPIO_PWR, values, 1);
			__assign_bit(J720_GPIO_3V, values, 1);
			break;
		case 50:
			__assign_bit(J720_GPIO_PWR, values, 1);
			__assign_bit(J720_GPIO_3V, values, 0);
			break;
		}
		break;

	case 1:
		switch (state->Vcc) {
		default:
		case 0:
			__assign_bit(J720_GPIO_PWR, values, 0);
			__assign_bit(J720_GPIO_3V, values, 0);
			break;
		case 33:
		case 50:
			__assign_bit(J720_GPIO_PWR, values, 1);
			__assign_bit(J720_GPIO_3V, values, 1);
			break;
		}
		break;

	default:
		return -1;
	}

	if (state->Vpp != state->Vcc && state->Vpp != 0) {
		printk(KERN_ERR "%s(): slot cannot support VPP %u\n",
			__func__, state->Vpp);
		return -EPERM;
	}

	ret = sa1111_pcmcia_configure_socket(skt, state);
	if (ret == 0)
		ret = gpiod_set_array_value_cansleep(J720_GPIO_MAX, j->gpio,
						     NULL, values);

	return ret;
}

static struct pcmcia_low_level jornada720_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= jornada720_pcmcia_hw_init,
	.configure_socket	= jornada720_pcmcia_configure_socket,
	.first			= 0,
	.nr			= 2,
};

int pcmcia_jornada720_init(struct sa1111_dev *sadev)
{
	/* Fixme: why messing around with SA11x0's GPIO1? */
	GRER |= 0x00000002;

	sa11xx_drv_pcmcia_ops(&jornada720_pcmcia_ops);
	return sa1111_pcmcia_add(sadev, &jornada720_pcmcia_ops,
				 sa11xx_drv_pcmcia_add_one);
}
