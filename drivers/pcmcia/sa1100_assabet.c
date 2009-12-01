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

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/irq.h>
#include <asm/signal.h>
#include <mach/assabet.h>

#include "sa1100_generic.h"

static struct pcmcia_irqs irqs[] = {
	{ 1, ASSABET_IRQ_GPIO_CF_CD,   "CF CD"   },
	{ 1, ASSABET_IRQ_GPIO_CF_BVD2, "CF BVD2" },
	{ 1, ASSABET_IRQ_GPIO_CF_BVD1, "CF BVD1" },
};

static int assabet_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	skt->irq = ASSABET_IRQ_GPIO_CF_IRQ;

	return soc_pcmcia_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

/*
 * Release all resources.
 */
static void assabet_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	soc_pcmcia_free_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void
assabet_pcmcia_socket_state(struct soc_pcmcia_socket *skt, struct pcmcia_state *state)
{
	unsigned long levels = GPLR;

	state->detect = (levels & ASSABET_GPIO_CF_CD) ? 0 : 1;
	state->ready  = (levels & ASSABET_GPIO_CF_IRQ) ? 1 : 0;
	state->bvd1   = (levels & ASSABET_GPIO_CF_BVD1) ? 1 : 0;
	state->bvd2   = (levels & ASSABET_GPIO_CF_BVD2) ? 1 : 0;
	state->wrprot = 0; /* Not available on Assabet. */
	state->vs_3v  = 1; /* Can only apply 3.3V on Assabet. */
	state->vs_Xv  = 0;
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

	/* Silently ignore Vpp, output enable, speaker enable. */

	if (state->flags & SS_RESET)
		mask |= ASSABET_BCR_CF_RST;

	ASSABET_BCR_frob(ASSABET_BCR_CF_RST | ASSABET_BCR_CF_PWR, mask);

	return 0;
}

/*
 * Enable card status IRQs on (re-)initialisation.  This can
 * be called at initialisation, power management event, or
 * pcmcia event.
 */
static void assabet_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
	/*
	 * Enable CF bus
	 */
	ASSABET_BCR_clear(ASSABET_BCR_CF_BUS_OFF);

	soc_pcmcia_enable_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

/*
 * Disable card status IRQs on suspend.
 */
static void assabet_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
	soc_pcmcia_disable_irqs(skt, irqs, ARRAY_SIZE(irqs));

	/*
	 * Tristate the CF bus signals.  Also assert CF
	 * reset as per user guide page 4-11.
	 */
	ASSABET_BCR_set(ASSABET_BCR_CF_BUS_OFF | ASSABET_BCR_CF_RST);
}

static struct pcmcia_low_level assabet_pcmcia_ops = { 
	.owner			= THIS_MODULE,

	.hw_init		= assabet_pcmcia_hw_init,
	.hw_shutdown		= assabet_pcmcia_hw_shutdown,

	.socket_state		= assabet_pcmcia_socket_state,
	.configure_socket	= assabet_pcmcia_configure_socket,

	.socket_init		= assabet_pcmcia_socket_init,
	.socket_suspend		= assabet_pcmcia_socket_suspend,
};

int pcmcia_assabet_init(struct device *dev)
{
	int ret = -ENODEV;

	if (machine_is_assabet() && !machine_has_neponset())
		ret = sa11xx_drv_pcmcia_probe(dev, &assabet_pcmcia_ops, 1, 1);

	return ret;
}
