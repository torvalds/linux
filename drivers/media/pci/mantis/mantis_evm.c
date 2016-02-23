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
#include "mantis_link.h"
#include "mantis_hif.h"
#include "mantis_reg.h"

static void mantis_hifevm_work(struct work_struct *work)
{
	struct mantis_ca *ca = container_of(work, struct mantis_ca, hif_evm_work);
	struct mantis_pci *mantis = ca->ca_priv;

	u32 gpif_stat;

	gpif_stat = mmread(MANTIS_GPIF_STATUS);

	if (gpif_stat & MANTIS_GPIF_DETSTAT) {
		if (gpif_stat & MANTIS_CARD_PLUGIN) {
			dprintk(MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): CAM Plugin", mantis->num);
			mmwrite(0xdada0000, MANTIS_CARD_RESET);
			mantis_event_cam_plugin(ca);
			dvb_ca_en50221_camchange_irq(&ca->en50221,
						     0,
						     DVB_CA_EN50221_CAMCHANGE_INSERTED);
		}
	} else {
		if (gpif_stat & MANTIS_CARD_PLUGOUT) {
			dprintk(MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): CAM Unplug", mantis->num);
			mmwrite(0xdada0000, MANTIS_CARD_RESET);
			mantis_event_cam_unplug(ca);
			dvb_ca_en50221_camchange_irq(&ca->en50221,
						     0,
						     DVB_CA_EN50221_CAMCHANGE_REMOVED);
		}
	}

	if (mantis->gpif_status & MANTIS_GPIF_EXTIRQ)
		dprintk(MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Ext IRQ", mantis->num);

	if (mantis->gpif_status & MANTIS_SBUF_WSTO)
		dprintk(MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Smart Buffer Timeout", mantis->num);

	if (mantis->gpif_status & MANTIS_GPIF_OTHERR)
		dprintk(MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Alignment Error", mantis->num);

	if (gpif_stat & MANTIS_SBUF_OVFLW)
		dprintk(MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Smart Buffer Overflow", mantis->num);

	if (gpif_stat & MANTIS_GPIF_BRRDY)
		dprintk(MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Smart Buffer Read Ready", mantis->num);

	if (gpif_stat & MANTIS_GPIF_INTSTAT)
		dprintk(MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): GPIF IRQ", mantis->num);

	if (gpif_stat & MANTIS_SBUF_EMPTY)
		dprintk(MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Smart Buffer Empty", mantis->num);

	if (gpif_stat & MANTIS_SBUF_OPDONE) {
		dprintk(MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Smart Buffer operation complete", mantis->num);
		ca->sbuf_status = MANTIS_SBUF_DATA_AVAIL;
		ca->hif_event = MANTIS_SBUF_OPDONE;
		wake_up(&ca->hif_opdone_wq);
	}
}

int mantis_evmgr_init(struct mantis_ca *ca)
{
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(MANTIS_DEBUG, 1, "Initializing Mantis Host I/F Event manager");
	INIT_WORK(&ca->hif_evm_work, mantis_hifevm_work);
	mantis_pcmcia_init(ca);
	schedule_work(&ca->hif_evm_work);
	mantis_hif_init(ca);
	return 0;
}

void mantis_evmgr_exit(struct mantis_ca *ca)
{
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(MANTIS_DEBUG, 1, "Mantis Host I/F Event manager exiting");
	flush_work(&ca->hif_evm_work);
	mantis_hif_exit(ca);
	mantis_pcmcia_exit(ca);
}
