/*
 * drivers/pcmcia/sa1100_shannon.c
 *
 * PCMCIA implementation routines for Shannon
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/arch/shannon.h>
#include <asm/irq.h>
#include "sa1100_generic.h"

static struct pcmcia_irqs irqs[] = {
	{ 0, SHANNON_IRQ_GPIO_EJECT_0, "PCMCIA_CD_0" },
	{ 1, SHANNON_IRQ_GPIO_EJECT_1, "PCMCIA_CD_1" },
};

static int shannon_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	/* All those are inputs */
	GPDR &= ~(SHANNON_GPIO_EJECT_0 | SHANNON_GPIO_EJECT_1 | 
		  SHANNON_GPIO_RDY_0 | SHANNON_GPIO_RDY_1);
	GAFR &= ~(SHANNON_GPIO_EJECT_0 | SHANNON_GPIO_EJECT_1 | 
		  SHANNON_GPIO_RDY_0 | SHANNON_GPIO_RDY_1);

	skt->irq = skt->nr ? SHANNON_IRQ_GPIO_RDY_1 : SHANNON_IRQ_GPIO_RDY_0;

	return soc_pcmcia_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void shannon_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	soc_pcmcia_free_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void
shannon_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
			    struct pcmcia_state *state)
{
	unsigned long levels = GPLR;

	switch (skt->nr) {
	case 0:
		state->detect = (levels & SHANNON_GPIO_EJECT_0) ? 0 : 1;
		state->ready  = (levels & SHANNON_GPIO_RDY_0) ? 1 : 0;
		state->wrprot = 0; /* Not available on Shannon. */
		state->bvd1   = 1; 
		state->bvd2   = 1; 
		state->vs_3v  = 1; /* FIXME Can only apply 3.3V on Shannon. */
		state->vs_Xv  = 0;
		break;

	case 1:
		state->detect = (levels & SHANNON_GPIO_EJECT_1) ? 0 : 1;
		state->ready  = (levels & SHANNON_GPIO_RDY_1) ? 1 : 0;
		state->wrprot = 0; /* Not available on Shannon. */
		state->bvd1   = 1; 
		state->bvd2   = 1; 
		state->vs_3v  = 1; /* FIXME Can only apply 3.3V on Shannon. */
		state->vs_Xv  = 0;
		break;
	}
}

static int
shannon_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				const socket_state_t *state)
{
	switch (state->Vcc) {
	case 0:	/* power off */
		printk(KERN_WARNING "%s(): CS asked for 0V, still applying 3.3V..\n", __FUNCTION__);
		break;
	case 50:
		printk(KERN_WARNING "%s(): CS asked for 5V, applying 3.3V..\n", __FUNCTION__);
	case 33:
		break;
	default:
		printk(KERN_ERR "%s(): unrecognized Vcc %u\n",
		       __FUNCTION__, state->Vcc);
		return -1;
	}

	printk(KERN_WARNING "%s(): Warning, Can't perform reset\n", __FUNCTION__);
	
	/* Silently ignore Vpp, output enable, speaker enable. */

	return 0;
}

static void shannon_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
	soc_pcmcia_enable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void shannon_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
	soc_pcmcia_disable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static struct pcmcia_low_level shannon_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= shannon_pcmcia_hw_init,
	.hw_shutdown		= shannon_pcmcia_hw_shutdown,
	.socket_state		= shannon_pcmcia_socket_state,
	.configure_socket	= shannon_pcmcia_configure_socket,

	.socket_init		= shannon_pcmcia_socket_init,
	.socket_suspend		= shannon_pcmcia_socket_suspend,
};

int __init pcmcia_shannon_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_shannon())
		ret = sa11xx_drv_pcmcia_probe(dev, &shannon_pcmcia_ops, 0, 2);

	return ret;
}
