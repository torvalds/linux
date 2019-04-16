/*
 * linux/drivers/pcmcia/pxa2xx_lubbock.c
 *
 * Author:	George Davis
 * Created:	Jan 10, 2002
 * Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Originally based upon linux/drivers/pcmcia/sa1100_neponset.c
 *
 * Lubbock PCMCIA specific routines.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <mach/hardware.h>
#include <asm/hardware/sa1111.h>
#include <asm/mach-types.h>

#include "sa1111_generic.h"
#include "max1600.h"

static int lubbock_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	struct max1600 *m;
	int ret;

	ret = max1600_init(skt->socket.dev.parent, &m,
			   skt->nr ? MAX1600_CHAN_B : MAX1600_CHAN_A,
			   MAX1600_CODE_HIGH);
	if (ret == 0)
		skt->driver_data = m;

	return ret;
}

static int
lubbock_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
				const socket_state_t *state)
{
	struct max1600 *m = skt->driver_data;
	int ret = 0;

	/* Lubbock uses the Maxim MAX1602, with the following connections:
	 *
	 * Socket 0 (PCMCIA):
	 *	MAX1602	Lubbock		Register
	 *	Pin	Signal
	 *	-----	-------		----------------------
	 *	A0VPP	S0_PWR0		SA-1111 GPIO A<0>
	 *	A1VPP	S0_PWR1		SA-1111 GPIO A<1>
	 *	A0VCC	S0_PWR2		SA-1111 GPIO A<2>
	 *	A1VCC	S0_PWR3		SA-1111 GPIO A<3>
	 *	VX	VCC
	 *	VY	+3.3V
	 *	12IN	+12V
	 *	CODE	+3.3V		Cirrus  Code, CODE = High (VY)
	 *
	 * Socket 1 (CF):
	 *	MAX1602	Lubbock		Register
	 *	Pin	Signal
	 *	-----	-------		----------------------
	 *	A0VPP	GND		VPP is not connected
	 *	A1VPP	GND		VPP is not connected
	 *	A0VCC	S1_PWR0		MISC_WR<14>
	 *	A1VCC	S1_PWR1		MISC_WR<15>
	 *	VX	VCC
	 *	VY	+3.3V
	 *	12IN	GND		VPP is not connected
	 *	CODE	+3.3V		Cirrus  Code, CODE = High (VY)
	 *
	 */

 again:
	switch (skt->nr) {
	case 0:
	case 1:
		break;

	default:
		ret = -1;
	}

	if (ret == 0)
		ret = sa1111_pcmcia_configure_socket(skt, state);
	if (ret == 0)
		ret = max1600_configure(m, state->Vcc, state->Vpp);

#if 1
	if (ret == 0 && state->Vcc == 33) {
		struct pcmcia_state new_state;

		/*
		 * HACK ALERT:
		 * We can't sense the voltage properly on Lubbock before
		 * actually applying some power to the socket (catch 22).
		 * Resense the socket Voltage Sense pins after applying
		 * socket power.
		 *
		 * Note: It takes about 2.5ms for the MAX1602 VCC output
		 * to rise.
		 */
		mdelay(3);

		sa1111_pcmcia_socket_state(skt, &new_state);

		if (!new_state.vs_3v && !new_state.vs_Xv) {
			/*
			 * Switch to 5V,  Configure socket with 5V voltage
			 */
			max1600_configure(m, 0, 0);

			/*
			 * It takes about 100ms to turn off Vcc.
			 */
			mdelay(100);

			/*
			 * We need to hack around the const qualifier as
			 * well to keep this ugly workaround localized and
			 * not force it to the rest of the code. Barf bags
			 * available in the seat pocket in front of you!
			 */
			((socket_state_t *)state)->Vcc = 50;
			((socket_state_t *)state)->Vpp = 50;
			goto again;
		}
	}
#endif

	return ret;
}

static struct pcmcia_low_level lubbock_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= lubbock_pcmcia_hw_init,
	.configure_socket	= lubbock_pcmcia_configure_socket,
	.first			= 0,
	.nr			= 2,
};

#include "pxa2xx_base.h"

int pcmcia_lubbock_init(struct sa1111_dev *sadev)
{
	pxa2xx_drv_pcmcia_ops(&lubbock_pcmcia_ops);
	pxa2xx_configure_sockets(&sadev->dev, &lubbock_pcmcia_ops);
	return sa1111_pcmcia_add(sadev, &lubbock_pcmcia_ops,
				 pxa2xx_drv_pcmcia_add_one);
}

MODULE_LICENSE("GPL");
