/*
 * Sharp SL-C7xx Series PCMCIA routines
 *
 * Copyright (c) 2004-2005 Richard Purdie
 *
 * Based on Sharp's 2.4 kernel patches and pxa2xx_mainstone.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/hardware/scoop.h>
#include <asm/arch/pxa-regs.h>

#include "soc_common.h"

#define	NO_KEEP_VS 0x0001

static void sharpsl_pcmcia_init_reset(struct scoop_pcmcia_dev *scoopdev)
{
	reset_scoop(scoopdev->dev);
	scoopdev->keep_vs = NO_KEEP_VS;
	scoopdev->keep_rd = 0;
}

static int sharpsl_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	int ret;

	/*
	 * Setup default state of GPIO outputs
	 * before we enable them as outputs.
	 */
	GPSR(GPIO48_nPOE) =
		GPIO_bit(GPIO48_nPOE) |
		GPIO_bit(GPIO49_nPWE) |
		GPIO_bit(GPIO50_nPIOR) |
		GPIO_bit(GPIO51_nPIOW) |
		GPIO_bit(GPIO52_nPCE_1) |
		GPIO_bit(GPIO53_nPCE_2);

	pxa_gpio_mode(GPIO48_nPOE_MD);
	pxa_gpio_mode(GPIO49_nPWE_MD);
	pxa_gpio_mode(GPIO50_nPIOR_MD);
	pxa_gpio_mode(GPIO51_nPIOW_MD);
	pxa_gpio_mode(GPIO52_nPCE_1_MD);
	pxa_gpio_mode(GPIO53_nPCE_2_MD);
	pxa_gpio_mode(GPIO54_pSKTSEL_MD);
	pxa_gpio_mode(GPIO55_nPREG_MD);
	pxa_gpio_mode(GPIO56_nPWAIT_MD);
	pxa_gpio_mode(GPIO57_nIOIS16_MD);

	/* Register interrupts */
	if (scoop_devs[skt->nr].cd_irq >= 0) {
		struct pcmcia_irqs cd_irq;

		cd_irq.sock = skt->nr;
		cd_irq.irq  = scoop_devs[skt->nr].cd_irq;
		cd_irq.str  = scoop_devs[skt->nr].cd_irq_str;
		ret = soc_pcmcia_request_irqs(skt, &cd_irq, 1);

		if (ret) {
			printk(KERN_ERR "Request for Compact Flash IRQ failed\n");
			return ret;
		}
	}

	skt->irq = scoop_devs[skt->nr].irq;

	return 0;
}

static void sharpsl_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	if (scoop_devs[skt->nr].cd_irq >= 0) {
		struct pcmcia_irqs cd_irq;

		cd_irq.sock = skt->nr;
		cd_irq.irq  = scoop_devs[skt->nr].cd_irq;
		cd_irq.str  = scoop_devs[skt->nr].cd_irq_str;
		soc_pcmcia_free_irqs(skt, &cd_irq, 1);
	}
}


static void sharpsl_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
				    struct pcmcia_state *state)
{
	unsigned short cpr, csr;
	struct device *scoop = scoop_devs[skt->nr].dev;

	cpr = read_scoop_reg(scoop_devs[skt->nr].dev, SCOOP_CPR);

	write_scoop_reg(scoop, SCOOP_IRM, 0x00FF);
	write_scoop_reg(scoop, SCOOP_ISR, 0x0000);
	write_scoop_reg(scoop, SCOOP_IRM, 0x0000);
	csr = read_scoop_reg(scoop, SCOOP_CSR);
	if (csr & 0x0004) {
		/* card eject */
		write_scoop_reg(scoop, SCOOP_CDR, 0x0000);
		scoop_devs[skt->nr].keep_vs = NO_KEEP_VS;
	}
	else if (!(scoop_devs[skt->nr].keep_vs & NO_KEEP_VS)) {
		/* keep vs1,vs2 */
		write_scoop_reg(scoop, SCOOP_CDR, 0x0000);
		csr |= scoop_devs[skt->nr].keep_vs;
	}
	else if (cpr & 0x0003) {
		/* power on */
		write_scoop_reg(scoop, SCOOP_CDR, 0x0000);
		scoop_devs[skt->nr].keep_vs = (csr & 0x00C0);
	}
	else {
		/* card detect */
		write_scoop_reg(scoop, SCOOP_CDR, 0x0002);
	}

	state->detect = (csr & 0x0004) ? 0 : 1;
	state->ready  = (csr & 0x0002) ? 1 : 0;
	state->bvd1   = (csr & 0x0010) ? 1 : 0;
	state->bvd2   = (csr & 0x0020) ? 1 : 0;
	state->wrprot = (csr & 0x0008) ? 1 : 0;
	state->vs_3v  = (csr & 0x0040) ? 0 : 1;
	state->vs_Xv  = (csr & 0x0080) ? 0 : 1;

	if ((cpr & 0x0080) && ((cpr & 0x8040) != 0x8040)) {
		printk(KERN_ERR "sharpsl_pcmcia_socket_state(): CPR=%04X, Low voltage!\n", cpr);
	}

}


static int sharpsl_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				       const socket_state_t *state)
{
	unsigned long flags;
	struct device *scoop = scoop_devs[skt->nr].dev;

	unsigned short cpr, ncpr, ccr, nccr, mcr, nmcr, imr, nimr;

	switch (state->Vcc) {
	case	0:  	break;
	case 	33: 	break;
	case	50: 	break;
	default:
		 printk(KERN_ERR "sharpsl_pcmcia_configure_socket(): bad Vcc %u\n", state->Vcc);
		 return -1;
	}

	if ((state->Vpp!=state->Vcc) && (state->Vpp!=0)) {
		printk(KERN_ERR "CF slot cannot support Vpp %u\n", state->Vpp);
		return -1;
	}

	local_irq_save(flags);

	nmcr = (mcr = read_scoop_reg(scoop, SCOOP_MCR)) & ~0x0010;
	ncpr = (cpr = read_scoop_reg(scoop, SCOOP_CPR)) & ~0x0083;
	nccr = (ccr = read_scoop_reg(scoop, SCOOP_CCR)) & ~0x0080;
	nimr = (imr = read_scoop_reg(scoop, SCOOP_IMR)) & ~0x003E;

	ncpr |= (state->Vcc == 33) ? 0x0001 :
				(state->Vcc == 50) ? 0x0002 : 0;
	nmcr |= (state->flags&SS_IOCARD) ? 0x0010 : 0;
	ncpr |= (state->flags&SS_OUTPUT_ENA) ? 0x0080 : 0;
	nccr |= (state->flags&SS_RESET)? 0x0080: 0;
	nimr |=	((skt->status&SS_DETECT) ? 0x0004 : 0)|
			((skt->status&SS_READY)  ? 0x0002 : 0)|
			((skt->status&SS_BATDEAD)? 0x0010 : 0)|
			((skt->status&SS_BATWARN)? 0x0020 : 0)|
			((skt->status&SS_STSCHG) ? 0x0010 : 0)|
			((skt->status&SS_WRPROT) ? 0x0008 : 0);

	if (!(ncpr & 0x0003)) {
		scoop_devs[skt->nr].keep_rd = 0;
	} else if (!scoop_devs[skt->nr].keep_rd) {
		if (nccr & 0x0080)
			scoop_devs[skt->nr].keep_rd = 1;
		else
			nccr |= 0x0080;
	}

	if (mcr != nmcr)
		write_scoop_reg(scoop, SCOOP_MCR, nmcr);
	if (cpr != ncpr)
		write_scoop_reg(scoop, SCOOP_CPR, ncpr);
	if (ccr != nccr)
		write_scoop_reg(scoop, SCOOP_CCR, nccr);
	if (imr != nimr)
		write_scoop_reg(scoop, SCOOP_IMR, nimr);

	local_irq_restore(flags);

	return 0;
}

static void sharpsl_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
	sharpsl_pcmcia_init_reset(&scoop_devs[skt->nr]);

	/* Enable interrupt */
	write_scoop_reg(scoop_devs[skt->nr].dev, SCOOP_IMR, 0x00C0);
	write_scoop_reg(scoop_devs[skt->nr].dev, SCOOP_MCR, 0x0101);
	scoop_devs[skt->nr].keep_vs = NO_KEEP_VS;
}

static void sharpsl_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
	/* CF_BUS_OFF */
	sharpsl_pcmcia_init_reset(&scoop_devs[skt->nr]);
}

static struct pcmcia_low_level sharpsl_pcmcia_ops = {
	.owner				= THIS_MODULE,
	.hw_init			= sharpsl_pcmcia_hw_init,
	.hw_shutdown		= sharpsl_pcmcia_hw_shutdown,
	.socket_state		= sharpsl_pcmcia_socket_state,
	.configure_socket	= sharpsl_pcmcia_configure_socket,
	.socket_init		= sharpsl_pcmcia_socket_init,
	.socket_suspend		= sharpsl_pcmcia_socket_suspend,
	.first				= 0,
	.nr					= 0,
};

static struct platform_device *sharpsl_pcmcia_device;

static int __init sharpsl_pcmcia_init(void)
{
	int ret;

	sharpsl_pcmcia_ops.nr=scoop_num;
	sharpsl_pcmcia_device = kmalloc(sizeof(*sharpsl_pcmcia_device), GFP_KERNEL);
	if (!sharpsl_pcmcia_device)
		return -ENOMEM;

	memset(sharpsl_pcmcia_device, 0, sizeof(*sharpsl_pcmcia_device));
	sharpsl_pcmcia_device->name = "pxa2xx-pcmcia";
	sharpsl_pcmcia_device->dev.platform_data = &sharpsl_pcmcia_ops;
	sharpsl_pcmcia_device->dev.parent=scoop_devs[0].dev;

	ret = platform_device_register(sharpsl_pcmcia_device);
	if (ret)
		kfree(sharpsl_pcmcia_device);

	return ret;
}

static void __exit sharpsl_pcmcia_exit(void)
{
	/*
	 * This call is supposed to free our sharpsl_pcmcia_device.
	 * Unfortunately platform_device don't have a free method, and
	 * we can't assume it's free of any reference at this point so we
	 * can't free it either.
	 */
	platform_device_unregister(sharpsl_pcmcia_device);
}

fs_initcall(sharpsl_pcmcia_init);
module_exit(sharpsl_pcmcia_exit);

MODULE_DESCRIPTION("Sharp SL Series PCMCIA Support");
MODULE_LICENSE("GPL");
