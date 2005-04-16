/*
 * linux/drivers/pcmcia/pxa2xx_mainstone.c
 *
 * Mainstone PCMCIA specific routines.
 *
 * Created:	May 12, 2004
 * Author:	Nicolas Pitre
 * Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>

#include <pcmcia/ss.h>

#include <asm/hardware.h>
#include <asm/irq.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/mainstone.h>

#include "soc_common.h"


static struct pcmcia_irqs irqs[] = {
	{ 0, MAINSTONE_S0_CD_IRQ, "PCMCIA0 CD" },
	{ 1, MAINSTONE_S1_CD_IRQ, "PCMCIA1 CD" },
	{ 0, MAINSTONE_S0_STSCHG_IRQ, "PCMCIA0 STSCHG" },
	{ 1, MAINSTONE_S1_STSCHG_IRQ, "PCMCIA1 STSCHG" },
};

static int mst_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	/*
	 * Setup default state of GPIO outputs
	 * before we enable them as outputs.
	 */
	GPSR(GPIO48_nPOE) =
		GPIO_bit(GPIO48_nPOE) |
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
	pxa_gpio_mode(GPIO79_pSKTSEL_MD);
	pxa_gpio_mode(GPIO55_nPREG_MD);
	pxa_gpio_mode(GPIO56_nPWAIT_MD);
	pxa_gpio_mode(GPIO57_nIOIS16_MD);

	skt->irq = (skt->nr == 0) ? MAINSTONE_S0_IRQ : MAINSTONE_S1_IRQ;
	return soc_pcmcia_request_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static void mst_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	soc_pcmcia_free_irqs(skt, irqs, ARRAY_SIZE(irqs));
}

static unsigned long mst_pcmcia_status[2];

static void mst_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
				    struct pcmcia_state *state)
{
	unsigned long status, flip;

	status = (skt->nr == 0) ? MST_PCMCIA0 : MST_PCMCIA1;
	flip = (status ^ mst_pcmcia_status[skt->nr]) & MST_PCMCIA_nSTSCHG_BVD1;

	/*
	 * Workaround for STSCHG which can't be deasserted:
	 * We therefore disable/enable corresponding IRQs
	 * as needed to avoid IRQ locks.
	 */
	if (flip) {
		mst_pcmcia_status[skt->nr] = status;
		if (status & MST_PCMCIA_nSTSCHG_BVD1)
			enable_irq( (skt->nr == 0) ? MAINSTONE_S0_STSCHG_IRQ
						   : MAINSTONE_S1_STSCHG_IRQ );
		else
			disable_irq( (skt->nr == 0) ? MAINSTONE_S0_STSCHG_IRQ
						    : MAINSTONE_S1_STSCHG_IRQ );
	}

	state->detect = (status & MST_PCMCIA_nCD) ? 0 : 1;
	state->ready  = (status & MST_PCMCIA_nIRQ) ? 1 : 0;
	state->bvd1   = (status & MST_PCMCIA_nSTSCHG_BVD1) ? 1 : 0;
	state->bvd2   = (status & MST_PCMCIA_nSPKR_BVD2) ? 1 : 0;
	state->vs_3v  = (status & MST_PCMCIA_nVS1) ? 0 : 1;
	state->vs_Xv  = (status & MST_PCMCIA_nVS2) ? 0 : 1;
	state->wrprot = 0;  /* not available */
}

static int mst_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				       const socket_state_t *state)
{
	unsigned long power = 0;
	int ret = 0;

	switch (state->Vcc) {
	case 0:  power |= MST_PCMCIA_PWR_VCC_0;  break;
	case 33: power |= MST_PCMCIA_PWR_VCC_33; break;
	case 50: power |= MST_PCMCIA_PWR_VCC_50; break;
	default:
		 printk(KERN_ERR "%s(): bad Vcc %u\n",
				 __FUNCTION__, state->Vcc);
		 ret = -1;
	}

	switch (state->Vpp) {
	case 0:   power |= MST_PCMCIA_PWR_VPP_0;   break;
	case 120: power |= MST_PCMCIA_PWR_VPP_120; break;
	default:
		  if(state->Vpp == state->Vcc) {
			  power |= MST_PCMCIA_PWR_VPP_VCC;
		  } else {
			  printk(KERN_ERR "%s(): bad Vpp %u\n",
					  __FUNCTION__, state->Vpp);
			  ret = -1;
		  }
	}

	if (state->flags & SS_RESET)
	       power |= MST_PCMCIA_RESET;

	switch (skt->nr) {
	case 0:  MST_PCMCIA0 = power; break;
	case 1:  MST_PCMCIA1 = power; break;
	default: ret = -1;
	}

	return ret;
}

static void mst_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
}

static void mst_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
}

static struct pcmcia_low_level mst_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= mst_pcmcia_hw_init,
	.hw_shutdown		= mst_pcmcia_hw_shutdown,
	.socket_state		= mst_pcmcia_socket_state,
	.configure_socket	= mst_pcmcia_configure_socket,
	.socket_init		= mst_pcmcia_socket_init,
	.socket_suspend		= mst_pcmcia_socket_suspend,
	.nr			= 2,
};

static struct platform_device *mst_pcmcia_device;

static int __init mst_pcmcia_init(void)
{
	int ret;

	mst_pcmcia_device = kmalloc(sizeof(*mst_pcmcia_device), GFP_KERNEL);
	if (!mst_pcmcia_device)
		return -ENOMEM;
	memset(mst_pcmcia_device, 0, sizeof(*mst_pcmcia_device));
	mst_pcmcia_device->name = "pxa2xx-pcmcia";
	mst_pcmcia_device->dev.platform_data = &mst_pcmcia_ops;

	ret = platform_device_register(mst_pcmcia_device);
	if (ret)
		kfree(mst_pcmcia_device);

	return ret;
}

static void __exit mst_pcmcia_exit(void)
{
	/*
	 * This call is supposed to free our mst_pcmcia_device.
	 * Unfortunately platform_device don't have a free method, and
	 * we can't assume it's free of any reference at this point so we
	 * can't free it either.
	 */
	platform_device_unregister(mst_pcmcia_device);
}

module_init(mst_pcmcia_init);
module_exit(mst_pcmcia_exit);

MODULE_LICENSE("GPL");
