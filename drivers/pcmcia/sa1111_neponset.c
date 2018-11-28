// SPDX-License-Identifier: GPL-2.0
/*
 * linux/drivers/pcmcia/sa1100_neponset.c
 *
 * Neponset PCMCIA specific routines
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <asm/mach-types.h>

#include "sa1111_generic.h"
#include "max1600.h"

/*
 * Neponset uses the Maxim MAX1600, with the following connections:
 *
 *   MAX1600      Neponset
 *
 *    A0VCC        SA-1111 GPIO A<1>
 *    A1VCC        SA-1111 GPIO A<0>
 *    A0VPP        CPLD NCR A0VPP
 *    A1VPP        CPLD NCR A1VPP
 *    B0VCC        SA-1111 GPIO A<2>
 *    B1VCC        SA-1111 GPIO A<3>
 *    B0VPP        ground (slot B is CF)
 *    B1VPP        ground (slot B is CF)
 *
 *     VX          VCC (5V)
 *     VY          VCC3_3 (3.3V)
 *     12INA       12V
 *     12INB       ground (slot B is CF)
 *
 * The MAX1600 CODE pin is tied to ground, placing the device in 
 * "Standard Intel code" mode. Refer to the Maxim data sheet for
 * the corresponding truth table.
 */
static int neponset_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	struct max1600 *m;
	int ret;

	ret = max1600_init(skt->socket.dev.parent, &m,
			   skt->nr ? MAX1600_CHAN_B : MAX1600_CHAN_A,
			   MAX1600_CODE_LOW);
	if (ret == 0)
		skt->driver_data = m;

	return ret;
}

static int
neponset_pcmcia_configure_socket(struct soc_pcmcia_socket *skt, const socket_state_t *state)
{
	struct max1600 *m = skt->driver_data;
	int ret;

	ret = sa1111_pcmcia_configure_socket(skt, state);
	if (ret == 0)
		ret = max1600_configure(m, state->Vcc, state->Vpp);

	return ret;
}

static struct pcmcia_low_level neponset_pcmcia_ops = {
	.owner			= THIS_MODULE,
	.hw_init		= neponset_pcmcia_hw_init,
	.configure_socket	= neponset_pcmcia_configure_socket,
	.first			= 0,
	.nr			= 2,
};

int pcmcia_neponset_init(struct sa1111_dev *sadev)
{
	sa11xx_drv_pcmcia_ops(&neponset_pcmcia_ops);
	return sa1111_pcmcia_add(sadev, &neponset_pcmcia_ops,
				 sa11xx_drv_pcmcia_add_one);
}
