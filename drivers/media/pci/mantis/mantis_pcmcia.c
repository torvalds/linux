/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/kernel.h>

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/io.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"

#include "mantis_common.h"
#include "mantis_link.h" /* temporary due to physical layer stuff */
#include "mantis_reg.h"

/*
 * If Slot state is already PLUG_IN event and we are called
 * again, definitely it is jitter alone
 */
void mantis_event_cam_plugin(struct mantis_ca *ca)
{
	struct mantis_pci *mantis = ca->ca_priv;

	u32 gpif_irqcfg;

	if (ca->slot_state == MODULE_XTRACTED) {
		dprintk(MANTIS_DEBUG, 1, "Event: CAM Plugged IN: Adapter(%d) Slot(0)", mantis->num);
		udelay(50);
		mmwrite(0xda000000, MANTIS_CARD_RESET);
		gpif_irqcfg  = mmread(MANTIS_GPIF_IRQCFG);
		gpif_irqcfg |= MANTIS_MASK_PLUGOUT;
		gpif_irqcfg &= ~MANTIS_MASK_PLUGIN;
		mmwrite(gpif_irqcfg, MANTIS_GPIF_IRQCFG);
		udelay(500);
		ca->slot_state = MODULE_INSERTED;
	}
	udelay(100);
}

/*
 * If Slot state is already UN_PLUG event and we are called
 * again, definitely it is jitter alone
 */
void mantis_event_cam_unplug(struct mantis_ca *ca)
{
	struct mantis_pci *mantis = ca->ca_priv;

	u32 gpif_irqcfg;

	if (ca->slot_state == MODULE_INSERTED) {
		dprintk(MANTIS_DEBUG, 1, "Event: CAM Unplugged: Adapter(%d) Slot(0)", mantis->num);
		udelay(50);
		mmwrite(0x00da0000, MANTIS_CARD_RESET);
		gpif_irqcfg  = mmread(MANTIS_GPIF_IRQCFG);
		gpif_irqcfg |= MANTIS_MASK_PLUGIN;
		gpif_irqcfg &= ~MANTIS_MASK_PLUGOUT;
		mmwrite(gpif_irqcfg, MANTIS_GPIF_IRQCFG);
		udelay(500);
		ca->slot_state = MODULE_XTRACTED;
	}
	udelay(100);
}

int mantis_pcmcia_init(struct mantis_ca *ca)
{
	struct mantis_pci *mantis = ca->ca_priv;

	u32 gpif_stat, card_stat;

	mantis_unmask_ints(mantis, MANTIS_INT_IRQ0);
	gpif_stat = mmread(MANTIS_GPIF_STATUS);
	card_stat = mmread(MANTIS_GPIF_IRQCFG);

	if (gpif_stat & MANTIS_GPIF_DETSTAT) {
		dprintk(MANTIS_DEBUG, 1, "CAM found on Adapter(%d) Slot(0)", mantis->num);
		mmwrite(card_stat | MANTIS_MASK_PLUGOUT, MANTIS_GPIF_IRQCFG);
		ca->slot_state = MODULE_INSERTED;
		dvb_ca_en50221_camchange_irq(&ca->en50221,
					     0,
					     DVB_CA_EN50221_CAMCHANGE_INSERTED);
	} else {
		dprintk(MANTIS_DEBUG, 1, "Empty Slot on Adapter(%d) Slot(0)", mantis->num);
		mmwrite(card_stat | MANTIS_MASK_PLUGIN, MANTIS_GPIF_IRQCFG);
		ca->slot_state = MODULE_XTRACTED;
		dvb_ca_en50221_camchange_irq(&ca->en50221,
					     0,
					     DVB_CA_EN50221_CAMCHANGE_REMOVED);
	}

	return 0;
}

void mantis_pcmcia_exit(struct mantis_ca *ca)
{
	struct mantis_pci *mantis = ca->ca_priv;

	mmwrite(mmread(MANTIS_GPIF_STATUS) & (~MANTIS_CARD_PLUGOUT | ~MANTIS_CARD_PLUGIN), MANTIS_GPIF_STATUS);
	mantis_mask_ints(mantis, MANTIS_INT_IRQ0);
}
