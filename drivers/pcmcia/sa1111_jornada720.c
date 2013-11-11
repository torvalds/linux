/*
 * drivers/pcmcia/sa1100_jornada720.c
 *
 * Jornada720 PCMCIA specific routines
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <mach/hardware.h>
#include <asm/hardware/sa1111.h>
#include <asm/mach-types.h>

#include "sa1111_generic.h"

/* Does SOCKET1_3V actually do anything? */
#define SOCKET0_POWER	GPIO_GPIO0
#define SOCKET0_3V	GPIO_GPIO2
#define SOCKET1_POWER	(GPIO_GPIO1 | GPIO_GPIO3)
#define SOCKET1_3V	GPIO_GPIO3

static int
jornada720_pcmcia_configure_socket(struct soc_pcmcia_socket *skt, const socket_state_t *state)
{
	struct sa1111_pcmcia_socket *s = to_skt(skt);
	unsigned int pa_dwr_mask, pa_dwr_set;
	int ret;

	printk(KERN_INFO "%s(): config socket %d vcc %d vpp %d\n", __func__,
		skt->nr, state->Vcc, state->Vpp);

	switch (skt->nr) {
	case 0:
		pa_dwr_mask = SOCKET0_POWER | SOCKET0_3V;

		switch (state->Vcc) {
		default:
		case  0:
			pa_dwr_set = 0;
			break;
		case 33:
			pa_dwr_set = SOCKET0_POWER | SOCKET0_3V;
			break;
		case 50:
			pa_dwr_set = SOCKET0_POWER;
			break;
		}
		break;

	case 1:
		pa_dwr_mask = SOCKET1_POWER;

		switch (state->Vcc) {
		default:
		case 0:
			pa_dwr_set = 0;
			break;
		case 33:
			pa_dwr_set = SOCKET1_POWER;
			break;
		case 50:
			pa_dwr_set = SOCKET1_POWER;
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
		sa1111_set_io(s->dev, pa_dwr_mask, pa_dwr_set);

	return ret;
}

static struct pcmcia_low_level jornada720_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.configure_socket	= jornada720_pcmcia_configure_socket,
	.first			= 0,
	.nr			= 2,
};

int pcmcia_jornada720_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_jornada720()) {
		unsigned int pin = GPIO_A0 | GPIO_A1 | GPIO_A2 | GPIO_A3;

		GRER |= 0x00000002;

		/* Set GPIO_A<3:1> to be outputs for PCMCIA/CF power controller: */
		sa1111_set_io_dir(dev, pin, 0, 0);
		sa1111_set_io(dev, pin, 0);
		sa1111_set_sleep_io(dev, pin, 0);

		sa11xx_drv_pcmcia_ops(&jornada720_pcmcia_ops);
		ret = sa1111_pcmcia_add(dev, &jornada720_pcmcia_ops,
				sa11xx_drv_pcmcia_add_one);
	}

	return ret;
}
