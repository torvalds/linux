/*
 * drivers/pcmcia/sa1100_assabet.c
 *
 * PCMCIA implementation routines for Assabet
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <mach/assabet.h>

#include "sa1100_generic.h"

static int assabet_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	skt->stat[SOC_STAT_CD].gpio = ASSABET_GPIO_CF_CD;
	skt->stat[SOC_STAT_CD].name = "CF CD";
	skt->stat[SOC_STAT_BVD1].gpio = ASSABET_GPIO_CF_BVD1;
	skt->stat[SOC_STAT_BVD1].name = "CF BVD1";
	skt->stat[SOC_STAT_BVD2].gpio = ASSABET_GPIO_CF_BVD2;
	skt->stat[SOC_STAT_BVD2].name = "CF BVD2";
	skt->stat[SOC_STAT_RDY].gpio = ASSABET_GPIO_CF_IRQ;
	skt->stat[SOC_STAT_RDY].name = "CF RDY";

	return 0;
}

static int
assabet_pcmcia_configure_socket(struct soc_pcmcia_socket *skt, const socket_state_t *state)
{
	unsigned int mask;

	switch (state->Vcc) {
	case 0:
		mask = 0;
		break;

	case 50:
		printk(KERN_WARNING "%s(): CS asked for 5V, applying 3.3V...\n",
			__func__);

	case 33:  /* Can only apply 3.3V to the CF slot. */
		mask = ASSABET_BCR_CF_PWR;
		break;

	default:
		printk(KERN_ERR "%s(): unrecognized Vcc %u\n", __func__,
			state->Vcc);
		return -1;
	}

	/* Silently ignore Vpp, speaker enable. */

	if (state->flags & SS_RESET)
		mask |= ASSABET_BCR_CF_RST;
	if (!(state->flags & SS_OUTPUT_ENA))
		mask |= ASSABET_BCR_CF_BUS_OFF;

	ASSABET_BCR_frob(ASSABET_BCR_CF_RST | ASSABET_BCR_CF_PWR |
			ASSABET_BCR_CF_BUS_OFF, mask);

	return 0;
}

/*
 * Disable card status IRQs on suspend.
 */
static void assabet_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
	/*
	 * Tristate the CF bus signals.  Also assert CF
	 * reset as per user guide page 4-11.
	 */
	ASSABET_BCR_set(ASSABET_BCR_CF_BUS_OFF | ASSABET_BCR_CF_RST);
}

static struct pcmcia_low_level assabet_pcmcia_ops = { 
	.owner			= THIS_MODULE,
	.hw_init		= assabet_pcmcia_hw_init,
	.socket_state		= soc_common_cf_socket_state,
	.configure_socket	= assabet_pcmcia_configure_socket,
	.socket_suspend		= assabet_pcmcia_socket_suspend,
};

int pcmcia_assabet_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_assabet() && !machine_has_neponset())
		ret = sa11xx_drv_pcmcia_probe(dev, &assabet_pcmcia_ops, 1, 1);

	return ret;
}
