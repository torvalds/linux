/*
 *
 * Alchemy Semi Pb1000 boards specific pcmcia routines.
 *
 * Copyright 2002 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
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
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/types.h>

#include <pcmcia/ss.h>
#include <pcmcia/cistpl.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>

#include <asm/au1000.h>
#include <asm/au1000_pcmcia.h>

#define debug(fmt, arg...) do { } while (0)

#include <asm/pb1000.h>
#define PCMCIA_IRQ AU1000_GPIO_15

static int pb1x00_pcmcia_init(struct pcmcia_init *init)
{
	u16 pcr;
	pcr = PCR_SLOT_0_RST | PCR_SLOT_1_RST;

	au_writel(0x8000, PB1000_MDR); /* clear pcmcia interrupt */
	au_sync_delay(100);
	au_writel(0x4000, PB1000_MDR); /* enable pcmcia interrupt */
	au_sync();

	pcr |= SET_VCC_VPP(VCC_HIZ,VPP_HIZ,0);
	pcr |= SET_VCC_VPP(VCC_HIZ,VPP_HIZ,1);
	au_writel(pcr, PB1000_PCR);
	au_sync_delay(20);
	  
	return PCMCIA_NUM_SOCKS;
}

static int pb1x00_pcmcia_shutdown(void)
{
	u16 pcr;
	pcr = PCR_SLOT_0_RST | PCR_SLOT_1_RST;
	pcr |= SET_VCC_VPP(VCC_HIZ,VPP_HIZ,0);
	pcr |= SET_VCC_VPP(VCC_HIZ,VPP_HIZ,1);
	au_writel(pcr, PB1000_PCR);
	au_sync_delay(20);
	return 0;
}

static int 
pb1x00_pcmcia_socket_state(unsigned sock, struct pcmcia_state *state)
{
	u32 inserted0, inserted1;
	u16 vs0, vs1;

	vs0 = vs1 = (u16)au_readl(PB1000_ACR1);
	inserted0 = !(vs0 & (ACR1_SLOT_0_CD1 | ACR1_SLOT_0_CD2));
	inserted1 = !(vs1 & (ACR1_SLOT_1_CD1 | ACR1_SLOT_1_CD2));
	vs0 = (vs0 >> 4) & 0x3;
	vs1 = (vs1 >> 12) & 0x3;

	state->ready = 0;
	state->vs_Xv = 0;
	state->vs_3v = 0;
	state->detect = 0;

	if (sock == 0) {
		if (inserted0) {
			switch (vs0) {
				case 0:
				case 2:
					state->vs_3v=1;
					break;
				case 3: /* 5V */
					break;
				default:
					/* return without setting 'detect' */
					printk(KERN_ERR "pb1x00 bad VS (%d)\n",
							vs0);
					return 0;
			}
			state->detect = 1;
		}
	}
	else  {
		if (inserted1) {
			switch (vs1) {
				case 0:
				case 2:
					state->vs_3v=1;
					break;
				case 3: /* 5V */
					break;
				default:
					/* return without setting 'detect' */
					printk(KERN_ERR "pb1x00 bad VS (%d)\n",
							vs1);
					return 0;
			}
			state->detect = 1;
		}
	}

	if (state->detect) {
		state->ready = 1;
	}

	state->bvd1=1;
	state->bvd2=1;
	state->wrprot=0; 
	return 1;
}


static int pb1x00_pcmcia_get_irq_info(struct pcmcia_irq_info *info)
{

	if(info->sock > PCMCIA_MAX_SOCK) return -1;

	/*
	 * Even in the case of the Pb1000, both sockets are connected
	 * to the same irq line.
	 */
	info->irq = PCMCIA_IRQ;

	return 0;
}


static int 
pb1x00_pcmcia_configure_socket(const struct pcmcia_configure *configure)
{
	u16 pcr;

	if(configure->sock > PCMCIA_MAX_SOCK) return -1;

	pcr = au_readl(PB1000_PCR);

	if (configure->sock == 0) {
		pcr &= ~(PCR_SLOT_0_VCC0 | PCR_SLOT_0_VCC1 | 
				PCR_SLOT_0_VPP0 | PCR_SLOT_0_VPP1);
	}
	else  {
		pcr &= ~(PCR_SLOT_1_VCC0 | PCR_SLOT_1_VCC1 | 
				PCR_SLOT_1_VPP0 | PCR_SLOT_1_VPP1);
	}

	pcr &= ~PCR_SLOT_0_RST;
	debug("Vcc %dV Vpp %dV, pcr %x\n", 
			configure->vcc, configure->vpp, pcr);
	switch(configure->vcc){
		case 0:  /* Vcc 0 */
			switch(configure->vpp) {
				case 0:
					pcr |= SET_VCC_VPP(VCC_HIZ,VPP_GND,
							configure->sock);
					break;
				case 12:
					pcr |= SET_VCC_VPP(VCC_HIZ,VPP_12V,
							configure->sock);
					break;
				case 50:
					pcr |= SET_VCC_VPP(VCC_HIZ,VPP_5V,
							configure->sock);
					break;
				case 33:
					pcr |= SET_VCC_VPP(VCC_HIZ,VPP_3V,
							configure->sock);
					break;
				default:
					pcr |= SET_VCC_VPP(VCC_HIZ,VPP_HIZ,
							configure->sock);
					printk("%s: bad Vcc/Vpp (%d:%d)\n", 
							__func__,
							configure->vcc, 
							configure->vpp);
					break;
			}
			break;
		case 50: /* Vcc 5V */
			switch(configure->vpp) {
				case 0:
					pcr |= SET_VCC_VPP(VCC_5V,VPP_GND,
							configure->sock);
					break;
				case 50:
					pcr |= SET_VCC_VPP(VCC_5V,VPP_5V,
							configure->sock);
					break;
				case 12:
					pcr |= SET_VCC_VPP(VCC_5V,VPP_12V,
							configure->sock);
					break;
				case 33:
					pcr |= SET_VCC_VPP(VCC_5V,VPP_3V,
							configure->sock);
					break;
				default:
					pcr |= SET_VCC_VPP(VCC_HIZ,VPP_HIZ,
							configure->sock);
					printk("%s: bad Vcc/Vpp (%d:%d)\n", 
							__func__,
							configure->vcc, 
							configure->vpp);
					break;
			}
			break;
		case 33: /* Vcc 3.3V */
			switch(configure->vpp) {
				case 0:
					pcr |= SET_VCC_VPP(VCC_3V,VPP_GND,
							configure->sock);
					break;
				case 50:
					pcr |= SET_VCC_VPP(VCC_3V,VPP_5V,
							configure->sock);
					break;
				case 12:
					pcr |= SET_VCC_VPP(VCC_3V,VPP_12V,
							configure->sock);
					break;
				case 33:
					pcr |= SET_VCC_VPP(VCC_3V,VPP_3V,
							configure->sock);
					break;
				default:
					pcr |= SET_VCC_VPP(VCC_HIZ,VPP_HIZ,
							configure->sock);
					printk("%s: bad Vcc/Vpp (%d:%d)\n", 
							__func__,
							configure->vcc, 
							configure->vpp);
					break;
			}
			break;
		default: /* what's this ? */
			pcr |= SET_VCC_VPP(VCC_HIZ,VPP_HIZ,configure->sock);
			printk(KERN_ERR "%s: bad Vcc %d\n", 
					__func__, configure->vcc);
			break;
	}

	if (configure->sock == 0) {
	pcr &= ~(PCR_SLOT_0_RST);
		if (configure->reset)
		pcr |= PCR_SLOT_0_RST;
	}
	else {
		pcr &= ~(PCR_SLOT_1_RST);
		if (configure->reset)
			pcr |= PCR_SLOT_1_RST;
	}
	au_writel(pcr, PB1000_PCR);
	au_sync_delay(300);

	return 0;
}


struct pcmcia_low_level pb1x00_pcmcia_ops = { 
	pb1x00_pcmcia_init,
	pb1x00_pcmcia_shutdown,
	pb1x00_pcmcia_socket_state,
	pb1x00_pcmcia_get_irq_info,
	pb1x00_pcmcia_configure_socket
};
