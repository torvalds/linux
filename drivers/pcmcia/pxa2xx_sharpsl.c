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
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/hardware/scoop.h>

#include "soc_common.h"

#define	NO_KEEP_VS 0x0001
#define SCOOP_DEV platform_scoop_config->devs

static void sharpsl_pcmcia_init_reset(struct soc_pcmcia_socket *skt)
{
	struct scoop_pcmcia_dev *scoopdev = &SCOOP_DEV[skt->nr];

	reset_scoop(scoopdev->dev);

	/* Shared power controls need to be handled carefully */
	if (platform_scoop_config->power_ctrl)
		platform_scoop_config->power_ctrl(scoopdev->dev, 0x0000, skt->nr);
	else
		write_scoop_reg(scoopdev->dev, SCOOP_CPR, 0x0000);

	scoopdev->keep_vs = NO_KEEP_VS;
	scoopdev->keep_rd = 0;
}

static int sharpsl_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	int ret;

	if (platform_scoop_config->pcmcia_init)
		platform_scoop_config->pcmcia_init();

	/* Register interrupts */
	if (SCOOP_DEV[skt->nr].cd_irq >= 0) {
		struct pcmcia_irqs cd_irq;

		cd_irq.sock = skt->nr;
		cd_irq.irq  = SCOOP_DEV[skt->nr].cd_irq;
		cd_irq.str  = SCOOP_DEV[skt->nr].cd_irq_str;
		ret = soc_pcmcia_request_irqs(skt, &cd_irq, 1);

		if (ret) {
			printk(KERN_ERR "Request for Compact Flash IRQ failed\n");
			return ret;
		}
	}

	skt->irq = SCOOP_DEV[skt->nr].irq;

	return 0;
}

static void sharpsl_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	if (SCOOP_DEV[skt->nr].cd_irq >= 0) {
		struct pcmcia_irqs cd_irq;

		cd_irq.sock = skt->nr;
		cd_irq.irq  = SCOOP_DEV[skt->nr].cd_irq;
		cd_irq.str  = SCOOP_DEV[skt->nr].cd_irq_str;
		soc_pcmcia_free_irqs(skt, &cd_irq, 1);
	}
}


static void sharpsl_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
				    struct pcmcia_state *state)
{
	unsigned short cpr, csr;
	struct device *scoop = SCOOP_DEV[skt->nr].dev;

	cpr = read_scoop_reg(SCOOP_DEV[skt->nr].dev, SCOOP_CPR);

	write_scoop_reg(scoop, SCOOP_IRM, 0x00FF);
	write_scoop_reg(scoop, SCOOP_ISR, 0x0000);
	write_scoop_reg(scoop, SCOOP_IRM, 0x0000);
	csr = read_scoop_reg(scoop, SCOOP_CSR);
	if (csr & 0x0004) {
		/* card eject */
		write_scoop_reg(scoop, SCOOP_CDR, 0x0000);
		SCOOP_DEV[skt->nr].keep_vs = NO_KEEP_VS;
	}
	else if (!(SCOOP_DEV[skt->nr].keep_vs & NO_KEEP_VS)) {
		/* keep vs1,vs2 */
		write_scoop_reg(scoop, SCOOP_CDR, 0x0000);
		csr |= SCOOP_DEV[skt->nr].keep_vs;
	}
	else if (cpr & 0x0003) {
		/* power on */
		write_scoop_reg(scoop, SCOOP_CDR, 0x0000);
		SCOOP_DEV[skt->nr].keep_vs = (csr & 0x00C0);
	}
	else {
		/* card detect */
	        if ((machine_is_spitz() || machine_is_borzoi()) && skt->nr == 1) {
	                write_scoop_reg(scoop, SCOOP_CDR, 0x0000);
	        } else {
		        write_scoop_reg(scoop, SCOOP_CDR, 0x0002);
	        }
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
	struct device *scoop = SCOOP_DEV[skt->nr].dev;

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

	if ((machine_is_spitz() || machine_is_borzoi() || machine_is_akita()) && skt->nr == 0) {
	        ncpr |= (state->Vcc == 33) ? 0x0002 :
		        (state->Vcc == 50) ? 0x0002 : 0;
	} else {
	        ncpr |= (state->Vcc == 33) ? 0x0001 :
		        (state->Vcc == 50) ? 0x0002 : 0;
	}
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
		SCOOP_DEV[skt->nr].keep_rd = 0;
	} else if (!SCOOP_DEV[skt->nr].keep_rd) {
		if (nccr & 0x0080)
			SCOOP_DEV[skt->nr].keep_rd = 1;
		else
			nccr |= 0x0080;
	}

	if (mcr != nmcr)
		write_scoop_reg(scoop, SCOOP_MCR, nmcr);
	if (cpr != ncpr) {
		if (platform_scoop_config->power_ctrl)
			platform_scoop_config->power_ctrl(scoop, ncpr , skt->nr);
		else
		        write_scoop_reg(scoop, SCOOP_CPR, ncpr);
	}
	if (ccr != nccr)
		write_scoop_reg(scoop, SCOOP_CCR, nccr);
	if (imr != nimr)
		write_scoop_reg(scoop, SCOOP_IMR, nimr);

	local_irq_restore(flags);

	return 0;
}

static void sharpsl_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
	sharpsl_pcmcia_init_reset(skt);

	/* Enable interrupt */
	write_scoop_reg(SCOOP_DEV[skt->nr].dev, SCOOP_IMR, 0x00C0);
	write_scoop_reg(SCOOP_DEV[skt->nr].dev, SCOOP_MCR, 0x0101);
	SCOOP_DEV[skt->nr].keep_vs = NO_KEEP_VS;
}

static void sharpsl_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
	sharpsl_pcmcia_init_reset(skt);
}

static struct pcmcia_low_level sharpsl_pcmcia_ops = {
	.owner                  = THIS_MODULE,
	.hw_init                = sharpsl_pcmcia_hw_init,
	.hw_shutdown            = sharpsl_pcmcia_hw_shutdown,
	.socket_state           = sharpsl_pcmcia_socket_state,
	.configure_socket       = sharpsl_pcmcia_configure_socket,
	.socket_init            = sharpsl_pcmcia_socket_init,
	.socket_suspend         = sharpsl_pcmcia_socket_suspend,
	.first                  = 0,
	.nr                     = 0,
};

#ifdef CONFIG_SA1100_COLLIE
#include "sa11xx_base.h"

int __init pcmcia_collie_init(struct device *dev)
{
       int ret = -ENODEV;

       if (machine_is_collie())
               ret = sa11xx_drv_pcmcia_probe(dev, &sharpsl_pcmcia_ops, 0, 1);

       return ret;
}

#else

static struct platform_device *sharpsl_pcmcia_device;

static int __init sharpsl_pcmcia_init(void)
{
	int ret;

	sharpsl_pcmcia_ops.nr = platform_scoop_config->num_devs;
	sharpsl_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);

	if (!sharpsl_pcmcia_device)
		return -ENOMEM;

	sharpsl_pcmcia_device->dev.platform_data = &sharpsl_pcmcia_ops;
	sharpsl_pcmcia_device->dev.parent = platform_scoop_config->devs[0].dev;

	ret = platform_device_add(sharpsl_pcmcia_device);

	if (ret)
		platform_device_put(sharpsl_pcmcia_device);

	return ret;
}

static void __exit sharpsl_pcmcia_exit(void)
{
	platform_device_unregister(sharpsl_pcmcia_device);
}

fs_initcall(sharpsl_pcmcia_init);
module_exit(sharpsl_pcmcia_exit);
#endif

MODULE_DESCRIPTION("Sharp SL Series PCMCIA Support");
MODULE_LICENSE("GPL");
