// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/drivers/pcmcia/pxa2xx_trizeps4.c
 *
 * TRIZEPS PCMCIA specific routines.
 *
 * Author:	Jürgen Schindele
 * Created:	20 02, 2006
 * Copyright:	Jürgen Schindele
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <asm/irq.h>

#include <mach/pxa2xx-regs.h>
#include <mach/trizeps4.h>

#include "soc_common.h"

extern void board_pcmcia_power(int power);

static int trizeps_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	/* we dont have voltage/card/ready detection
	 * so we dont need interrupts for it
	 */
	switch (skt->nr) {
	case 0:
		skt->stat[SOC_STAT_CD].gpio = GPIO_PCD;
		skt->stat[SOC_STAT_CD].name = "cs0_cd";
		skt->stat[SOC_STAT_RDY].gpio = GPIO_PRDY;
		skt->stat[SOC_STAT_RDY].name = "cs0_rdy";
		break;
	default:
		break;
	}
	/* release the reset of this card */
	pr_debug("%s: sock %d irq %d\n", __func__, skt->nr, skt->socket.pci_irq);

	return 0;
}

static unsigned long trizeps_pcmcia_status[2];

static void trizeps_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
				struct pcmcia_state *state)
{
	unsigned short status = 0, change;
	status = CFSR_readw();
	change = (status ^ trizeps_pcmcia_status[skt->nr]) &
				ConXS_CFSR_BVD_MASK;
	if (change) {
		trizeps_pcmcia_status[skt->nr] = status;
		if (status & ConXS_CFSR_BVD1) {
			/* enable_irq empty */
		} else {
			/* disable_irq empty */
		}
	}

	switch (skt->nr) {
	case 0:
		/* just fill in fix states */
		state->bvd1   = (status & ConXS_CFSR_BVD1) ? 1 : 0;
		state->bvd2   = (status & ConXS_CFSR_BVD2) ? 1 : 0;
		state->vs_3v  = (status & ConXS_CFSR_VS1) ? 0 : 1;
		state->vs_Xv  = (status & ConXS_CFSR_VS2) ? 0 : 1;
		break;

#ifndef CONFIG_MACH_TRIZEPS_CONXS
	/* on ConXS we only have one slot. Second is inactive */
	case 1:
		state->detect = 0;
		state->ready  = 0;
		state->bvd1   = 0;
		state->bvd2   = 0;
		state->vs_3v  = 0;
		state->vs_Xv  = 0;
		break;

#endif
	}
}

static int trizeps_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				const socket_state_t *state)
{
	int ret = 0;
	unsigned short power = 0;

	/* we do nothing here just check a bit */
	switch (state->Vcc) {
	case 0:  power &= 0xfc; break;
	case 33: power |= ConXS_BCR_S0_VCC_3V3; break;
	case 50:
		pr_err("%s(): Vcc 5V not supported in socket\n", __func__);
		break;
	default:
		pr_err("%s(): bad Vcc %u\n", __func__, state->Vcc);
		ret = -1;
	}

	switch (state->Vpp) {
	case 0:  power &= 0xf3; break;
	case 33: power |= ConXS_BCR_S0_VPP_3V3; break;
	case 120:
		pr_err("%s(): Vpp 12V not supported in socket\n", __func__);
		break;
	default:
		if (state->Vpp != state->Vcc) {
			pr_err("%s(): bad Vpp %u\n", __func__, state->Vpp);
			ret = -1;
		}
	}

	switch (skt->nr) {
	case 0:			 /* we only have 3.3V */
		board_pcmcia_power(power);
		break;

#ifndef CONFIG_MACH_TRIZEPS_CONXS
	/* on ConXS we only have one slot. Second is inactive */
	case 1:
#endif
	default:
		break;
	}

	return ret;
}

static void trizeps_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
	/* default is on */
	board_pcmcia_power(0x9);
}

static void trizeps_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
	board_pcmcia_power(0x0);
}

static struct pcmcia_low_level trizeps_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= trizeps_pcmcia_hw_init,
	.socket_state		= trizeps_pcmcia_socket_state,
	.configure_socket	= trizeps_pcmcia_configure_socket,
	.socket_init		= trizeps_pcmcia_socket_init,
	.socket_suspend		= trizeps_pcmcia_socket_suspend,
#ifdef CONFIG_MACH_TRIZEPS_CONXS
	.nr			= 1,
#else
	.nr			= 2,
#endif
	.first			= 0,
};

static struct platform_device *trizeps_pcmcia_device;

static int __init trizeps_pcmcia_init(void)
{
	int ret;

	if (!machine_is_trizeps4() && !machine_is_trizeps4wl())
		return -ENODEV;

	trizeps_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);
	if (!trizeps_pcmcia_device)
		return -ENOMEM;

	ret = platform_device_add_data(trizeps_pcmcia_device,
			&trizeps_pcmcia_ops, sizeof(trizeps_pcmcia_ops));

	if (ret == 0)
		ret = platform_device_add(trizeps_pcmcia_device);

	if (ret)
		platform_device_put(trizeps_pcmcia_device);

	return ret;
}

static void __exit trizeps_pcmcia_exit(void)
{
	platform_device_unregister(trizeps_pcmcia_device);
}

fs_initcall(trizeps_pcmcia_init);
module_exit(trizeps_pcmcia_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Juergen Schindele");
MODULE_ALIAS("platform:pxa2xx-pcmcia");
