/*
 *
 * Alchemy Semi Db1x00 boards specific pcmcia routines.
 *
 * Copyright 2002 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 * Copyright 2004 Pete Popov, updated the driver to 2.6.
 * Followed the sa11xx API and largely copied many of the hardware
 * independent functions.
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/signal.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-db1x00/db1x00.h>

#include "au1000_generic.h"

#if 0
#define debug(x,args...) printk(KERN_DEBUG "%s: " x, __func__ , ##args)
#else
#define debug(x,args...)
#endif

static BCSR * const bcsr = (BCSR *)BCSR_KSEG1_ADDR;

struct au1000_pcmcia_socket au1000_pcmcia_socket[PCMCIA_NUM_SOCKS];
extern int au1x00_pcmcia_socket_probe(struct device *, struct pcmcia_low_level *, int, int);

static int db1x00_pcmcia_hw_init(struct au1000_pcmcia_socket *skt)
{
#ifdef CONFIG_MIPS_DB1550
	skt->irq = skt->nr ? AU1000_GPIO_5 : AU1000_GPIO_3;
#else
	skt->irq = skt->nr ? AU1000_GPIO_5 : AU1000_GPIO_2;
#endif
	return 0;
}

static void db1x00_pcmcia_shutdown(struct au1000_pcmcia_socket *skt)
{
	bcsr->pcmcia = 0; /* turn off power */
	au_sync_delay(2);
}

static void
db1x00_pcmcia_socket_state(struct au1000_pcmcia_socket *skt, struct pcmcia_state *state)
{
	u32 inserted;
	unsigned char vs;

	state->ready = 0;
	state->vs_Xv = 0;
	state->vs_3v = 0;
	state->detect = 0;

	switch (skt->nr) {
	case 0:
		vs = bcsr->status & 0x3;
		inserted = !(bcsr->status & (1<<4));
		break;
	case 1:
		vs = (bcsr->status & 0xC)>>2;
		inserted = !(bcsr->status & (1<<5));
		break;
	default:/* should never happen */
		return;
	}

	if (inserted)
		debug("db1x00 socket %d: inserted %d, vs %d pcmcia %x\n",
				skt->nr, inserted, vs, bcsr->pcmcia);

	if (inserted) {
		switch (vs) {
			case 0:
			case 2:
				state->vs_3v=1;
				break;
			case 3: /* 5V */
				break;
			default:
				/* return without setting 'detect' */
				printk(KERN_ERR "db1x00 bad VS (%d)\n",
						vs);
		}
		state->detect = 1;
		state->ready = 1;
	}
	else {
		/* if the card was previously inserted and then ejected,
		 * we should turn off power to it
		 */
		if ((skt->nr == 0) && (bcsr->pcmcia & BCSR_PCMCIA_PC0RST)) {
			bcsr->pcmcia &= ~(BCSR_PCMCIA_PC0RST |
					BCSR_PCMCIA_PC0DRVEN |
					BCSR_PCMCIA_PC0VPP |
					BCSR_PCMCIA_PC0VCC);
			au_sync_delay(10);
		}
		else if ((skt->nr == 1) && bcsr->pcmcia & BCSR_PCMCIA_PC1RST) {
			bcsr->pcmcia &= ~(BCSR_PCMCIA_PC1RST |
					BCSR_PCMCIA_PC1DRVEN |
					BCSR_PCMCIA_PC1VPP |
					BCSR_PCMCIA_PC1VCC);
			au_sync_delay(10);
		}
	}

	state->bvd1=1;
	state->bvd2=1;
	state->wrprot=0;
}

static int
db1x00_pcmcia_configure_socket(struct au1000_pcmcia_socket *skt, struct socket_state_t *state)
{
	u16 pwr;
	int sock = skt->nr;

	debug("config_skt %d Vcc %dV Vpp %dV, reset %d\n",
			sock, state->Vcc, state->Vpp,
			state->flags & SS_RESET);

	/* pcmcia reg was set to zero at init time. Be careful when
	 * initializing a socket not to wipe out the settings of the
	 * other socket.
	 */
	pwr = bcsr->pcmcia;
	pwr &= ~(0xf << sock*8); /* clear voltage settings */

	state->Vpp = 0;
	switch(state->Vcc){
		case 0:  /* Vcc 0 */
			pwr |= SET_VCC_VPP(0,0,sock);
			break;
		case 50: /* Vcc 5V */
			switch(state->Vpp) {
				case 0:
					pwr |= SET_VCC_VPP(2,0,sock);
					break;
				case 50:
					pwr |= SET_VCC_VPP(2,1,sock);
					break;
				case 12:
					pwr |= SET_VCC_VPP(2,2,sock);
					break;
				case 33:
				default:
					pwr |= SET_VCC_VPP(0,0,sock);
					printk("%s: bad Vcc/Vpp (%d:%d)\n",
							__FUNCTION__,
							state->Vcc,
							state->Vpp);
					break;
			}
			break;
		case 33: /* Vcc 3.3V */
			switch(state->Vpp) {
				case 0:
					pwr |= SET_VCC_VPP(1,0,sock);
					break;
				case 12:
					pwr |= SET_VCC_VPP(1,2,sock);
					break;
				case 33:
					pwr |= SET_VCC_VPP(1,1,sock);
					break;
				case 50:
				default:
					pwr |= SET_VCC_VPP(0,0,sock);
					printk("%s: bad Vcc/Vpp (%d:%d)\n",
							__FUNCTION__,
							state->Vcc,
							state->Vpp);
					break;
			}
			break;
		default: /* what's this ? */
			pwr |= SET_VCC_VPP(0,0,sock);
			printk(KERN_ERR "%s: bad Vcc %d\n",
					__FUNCTION__, state->Vcc);
			break;
	}

	bcsr->pcmcia = pwr;
	au_sync_delay(300);

	if (sock == 0) {
		if (!(state->flags & SS_RESET)) {
			pwr |= BCSR_PCMCIA_PC0DRVEN;
			bcsr->pcmcia = pwr;
			au_sync_delay(300);
			pwr |= BCSR_PCMCIA_PC0RST;
			bcsr->pcmcia = pwr;
			au_sync_delay(100);
		}
		else {
			pwr &= ~(BCSR_PCMCIA_PC0RST | BCSR_PCMCIA_PC0DRVEN);
			bcsr->pcmcia = pwr;
			au_sync_delay(100);
		}
	}
	else {
		if (!(state->flags & SS_RESET)) {
			pwr |= BCSR_PCMCIA_PC1DRVEN;
			bcsr->pcmcia = pwr;
			au_sync_delay(300);
			pwr |= BCSR_PCMCIA_PC1RST;
			bcsr->pcmcia = pwr;
			au_sync_delay(100);
		}
		else {
			pwr &= ~(BCSR_PCMCIA_PC1RST | BCSR_PCMCIA_PC1DRVEN);
			bcsr->pcmcia = pwr;
			au_sync_delay(100);
		}
	}
	return 0;
}

/*
 * Enable card status IRQs on (re-)initialisation.  This can
 * be called at initialisation, power management event, or
 * pcmcia event.
 */
void db1x00_socket_init(struct au1000_pcmcia_socket *skt)
{
	/* nothing to do for now */
}

/*
 * Disable card status IRQs and PCMCIA bus on suspend.
 */
void db1x00_socket_suspend(struct au1000_pcmcia_socket *skt)
{
	/* nothing to do for now */
}

struct pcmcia_low_level db1x00_pcmcia_ops = {
	.owner			= THIS_MODULE,

	.hw_init 		= db1x00_pcmcia_hw_init,
	.hw_shutdown		= db1x00_pcmcia_shutdown,

	.socket_state		= db1x00_pcmcia_socket_state,
	.configure_socket	= db1x00_pcmcia_configure_socket,

	.socket_init		= db1x00_socket_init,
	.socket_suspend		= db1x00_socket_suspend
};

int __init au1x_board_init(struct device *dev)
{
	int ret = -ENODEV;
	bcsr->pcmcia = 0; /* turn off power, if it's not already off */
	au_sync_delay(2);
	ret = au1x00_pcmcia_socket_probe(dev, &db1x00_pcmcia_ops, 0, 2);
	return ret;
}
