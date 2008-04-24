/*
 * linux/drivers/pcmcia/pxa/pxa_cm_x270.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Compulab Ltd., 2003, 2007
 * Mike Rapoport <mike@compulab.co.il>
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/delay.h>

#include <pcmcia/ss.h>
#include <asm/hardware.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/pxa2xx-gpio.h>
#include <asm/arch/cm-x270.h>

#include "soc_common.h"

static struct pcmcia_irqs irqs[] = {
	{ 0, PCMCIA_S0_CD_VALID, "PCMCIA0 CD" },
	{ 1, PCMCIA_S1_CD_VALID, "PCMCIA1 CD" },
};

static int cmx270_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	GPSR(GPIO48_nPOE) = GPIO_bit(GPIO48_nPOE) |
		GPIO_bit(GPIO49_nPWE) |
		GPIO_bit(GPIO50_nPIOR) |
		GPIO_bit(GPIO51_nPIOW) |
		GPIO_bit(GPIO85_nPCE_1) |
		GPIO_bit(GPIO54_nPCE_2);

	pxa_gpio_mode(GPIO48_nPOE_MD);
	pxa_gpio_mode(GPIO49_nPWE_MD);
	pxa_gpio_mode(GPIO50_nPIOR_MD);
	pxa_gpio_mode(GPIO51_nPIOW_MD);
	pxa_gpio_mode(GPIO85_nPCE_1_MD);
	pxa_gpio_mode(GPIO54_nPCE_2_MD);
	pxa_gpio_mode(GPIO55_nPREG_MD);
	pxa_gpio_mode(GPIO56_nPWAIT_MD);
	pxa_gpio_mode(GPIO57_nIOIS16_MD);

	/* Reset signal */
	pxa_gpio_mode(GPIO53_nPCE_2 | GPIO_OUT);
	GPCR(GPIO53_nPCE_2) = GPIO_bit(GPIO53_nPCE_2);

	set_irq_type(PCMCIA_S0_CD_VALID, IRQ_TYPE_EDGE_BOTH);
	set_irq_type(PCMCIA_S1_CD_VALID, IRQ_TYPE_EDGE_BOTH);

	/* irq's for slots: */
	set_irq_type(PCMCIA_S0_RDYINT, IRQ_TYPE_EDGE_FALLING);
	set_irq_type(PCMCIA_S1_RDYINT, IRQ_TYPE_EDGE_FALLING);

	skt->irq = (skt->nr == 0) ? PCMCIA_S0_RDYINT : PCMCIA_S1_RDYINT;
	return soc_pcmcia_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void cmx270_pcmcia_shutdown(struct soc_pcmcia_socket *skt)
{
	soc_pcmcia_free_irqs(skt, irqs, ARRAY_SIZE(irqs));

	set_irq_type(IRQ_TO_GPIO(PCMCIA_S0_CD_VALID), IRQ_TYPE_NONE);
	set_irq_type(IRQ_TO_GPIO(PCMCIA_S1_CD_VALID), IRQ_TYPE_NONE);

	set_irq_type(IRQ_TO_GPIO(PCMCIA_S0_RDYINT), IRQ_TYPE_NONE);
	set_irq_type(IRQ_TO_GPIO(PCMCIA_S1_RDYINT), IRQ_TYPE_NONE);
}


static void cmx270_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
				       struct pcmcia_state *state)
{
	state->detect = (PCC_DETECT(skt->nr) == 0) ? 1 : 0;
	state->ready  = (PCC_READY(skt->nr) == 0) ? 0 : 1;
	state->bvd1   = 1;
	state->bvd2   = 1;
	state->vs_3v  = 0;
	state->vs_Xv  = 0;
	state->wrprot = 0;  /* not available */
}


static int cmx270_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
					  const socket_state_t *state)
{
	GPSR(GPIO49_nPWE) = GPIO_bit(GPIO49_nPWE);
	pxa_gpio_mode(GPIO49_nPWE | GPIO_OUT);

	switch (skt->nr) {
	case 0:
		if (state->flags & SS_RESET) {
			GPCR(GPIO49_nPWE) = GPIO_bit(GPIO49_nPWE);
			GPSR(GPIO53_nPCE_2) = GPIO_bit(GPIO53_nPCE_2);
			udelay(10);
			GPCR(GPIO53_nPCE_2) = GPIO_bit(GPIO53_nPCE_2);
			GPSR(GPIO49_nPWE) = GPIO_bit(GPIO49_nPWE);
		}
		break;
	case 1:
		if (state->flags & SS_RESET) {
			GPCR(GPIO49_nPWE) = GPIO_bit(GPIO49_nPWE);
			GPSR(GPIO53_nPCE_2) = GPIO_bit(GPIO53_nPCE_2);
			udelay(10);
			GPCR(GPIO53_nPCE_2) = GPIO_bit(GPIO53_nPCE_2);
			GPSR(GPIO49_nPWE) = GPIO_bit(GPIO49_nPWE);
		}
		break;
	}

	pxa_gpio_mode(GPIO49_nPWE_MD);

	return 0;
}

static void cmx270_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
}

static void cmx270_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
}


static struct pcmcia_low_level cmx270_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= cmx270_pcmcia_hw_init,
	.hw_shutdown		= cmx270_pcmcia_shutdown,
	.socket_state		= cmx270_pcmcia_socket_state,
	.configure_socket	= cmx270_pcmcia_configure_socket,
	.socket_init		= cmx270_pcmcia_socket_init,
	.socket_suspend		= cmx270_pcmcia_socket_suspend,
	.nr			= 2,
};

static struct platform_device *cmx270_pcmcia_device;

static int __init cmx270_pcmcia_init(void)
{
	int ret;

	cmx270_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);

	if (!cmx270_pcmcia_device)
		return -ENOMEM;

	cmx270_pcmcia_device->dev.platform_data = &cmx270_pcmcia_ops;

	printk(KERN_INFO "Registering cm-x270 PCMCIA interface.\n");
	ret = platform_device_add(cmx270_pcmcia_device);

	if (ret)
		platform_device_put(cmx270_pcmcia_device);

	return ret;
}

static void __exit cmx270_pcmcia_exit(void)
{
	platform_device_unregister(cmx270_pcmcia_device);
}

module_init(cmx270_pcmcia_init);
module_exit(cmx270_pcmcia_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_DESCRIPTION("CM-x270 PCMCIA driver");
