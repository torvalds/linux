/*
	Mantis PCI bridge driver

	Copyright (C) 2005, 2006 Manu Abraham (abraham.manu@gmail.com)

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

#include "mantis_common.h"
#include "mantis_link.h"
#include "mantis_hif.h"

void mantis_hifevm_tasklet(unsigned long data)
{
	struct mantis_ca *ca = (struct mantis_ca *) data;
	struct mantis_pci *mantis = ca->ca_priv;

	u32 gpif_stat, gpif_mask;

	gpif_stat = mmread(MANTIS_GPIF_STATUS);
	gpif_mask = mmread(MANTIS_GPIF_IRQCFG);
	if (!((gpif_stat & 0xff) & (gpif_mask & 0xff)))
		return;

	if (gpif_stat & MANTIS_GPIF_DETSTAT) {
		if (gpif_stat & MANTIS_CARD_PLUGIN) {
			dprintk(verbose, MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): CAM Plugin", mantis->num);
			mmwrite(0xdada0000, MANTIS_CARD_RESET);
			// Plugin call here
			gpif_stat = 0; // crude !
		}
	} else {
		if (gpif_stat & MANTIS_CARD_PLUGOUT) {
			dprintk(verbose, MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): CAM Unplug", mantis->num);
			mmwrite(0xdada0000, MANTIS_CARD_RESET);
			// Unplug call here
			gpif_stat = 0; // crude !
		}
	}

	if (gpif_stat & MANTIS_GPIF_EXTIRQ)
		dprintk(verbose, MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Ext IRQ", mantis->num);

	if (gpif_stat & MANTIS_SBUF_WSTO)
		dprintk(verbose, MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Smart Buffer Timeout", mantis->num);

	if (gpif_stat & MANTIS_GPIF_OTHERR)
		dprintk(verbose, MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Alignment Error", mantis->num);

	if (gpif_stat & MANTIS_SBUF_OVFLW)
		dprintk(verbose, MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Smart Buffer Overflow", mantis->num);

	if (gpif_stat & MANTIS_GPIF_BRRDY) {
		dprintk(verbose, MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Smart Buffer Read Ready", mantis->num);
		ca->sbuf_status = MANTIS_SBUF_DATA_AVAIL;
		if (ca->hif_job_queue & MANTIS_HIF_MEMRD)
			wake_up(&ca->hif_brrdyw_wq);
	}
	if (gpif_stat & MANTIS_GPIF_WRACK)
		dprintk(verbose, MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Slave Write ACK", mantis->num);

	if (gpif_stat & MANTIS_GPIF_INTSTAT)
		dprintk(verbose, MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): GPIF IRQ", mantis->num);

	if (gpif_stat & MANTIS_SBUF_EMPTY)
		dprintk(verbose, MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Smart Buffer Empty", mantis->num);

	if (gpif_stat & MANTIS_SBUF_OPDONE) {
		dprintk(verbose, MANTIS_DEBUG, 1, "Event Mgr: Adapter(%d) Slot(0): Smart Buffer operation complete", mantis->num);
		if (ca->hif_job_queue) {
			wake_up(&ca->hif_opdone_wq);
			ca->hif_event = MANTIS_SBUF_OPDONE;
		}
	}
}

int mantis_evmgr_init(struct mantis_ca *ca)
{
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(verbose, MANTIS_DEBUG, 1, "Initializing Mantis Host I/F Event manager");
	tasklet_init(&ca->hif_evm_tasklet, mantis_hifevm_tasklet, (unsigned long) ca);

	mantis_pcmcia_init(ca);

	return 0;
}

void mantis_evmgr_exit(struct mantis_ca *ca)
{
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(verbose, MANTIS_DEBUG, 1, "Mantis Host I/F Event manager exiting");
	tasklet_kill(&ca->hif_evm_tasklet);

	mantis_pcmcia_exit(ca);
}
