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

#include "mantis_hif.h"
#include "mantis_link.h" /* temporary due to physical layer stuff */

#include "mantis_reg.h"


static int mantis_hif_sbuf_opdone_wait(struct mantis_ca *ca)
{
	struct mantis_pci *mantis = ca->ca_priv;
	int rc = 0;

	if (wait_event_timeout(ca->hif_opdone_wq,
			       ca->hif_event & MANTIS_SBUF_OPDONE,
			       msecs_to_jiffies(500)) == -ERESTARTSYS) {

		dprintk(MANTIS_ERROR, 1, "Adapter(%d) Slot(0): Smart buffer operation timeout !", mantis->num);
		rc = -EREMOTEIO;
	}
	dprintk(MANTIS_DEBUG, 1, "Smart Buffer Operation complete");
	ca->hif_event &= ~MANTIS_SBUF_OPDONE;
	return rc;
}

static int mantis_hif_write_wait(struct mantis_ca *ca)
{
	struct mantis_pci *mantis = ca->ca_priv;
	u32 opdone = 0, timeout = 0;
	int rc = 0;

	if (wait_event_timeout(ca->hif_write_wq,
			       mantis->gpif_status & MANTIS_GPIF_WRACK,
			       msecs_to_jiffies(500)) == -ERESTARTSYS) {

		dprintk(MANTIS_ERROR, 1, "Adapter(%d) Slot(0): Write ACK timed out !", mantis->num);
		rc = -EREMOTEIO;
	}
	dprintk(MANTIS_DEBUG, 1, "Write Acknowledged");
	mantis->gpif_status &= ~MANTIS_GPIF_WRACK;
	while (!opdone) {
		opdone = (mmread(MANTIS_GPIF_STATUS) & MANTIS_SBUF_OPDONE);
		udelay(500);
		timeout++;
		if (timeout > 100) {
			dprintk(MANTIS_ERROR, 1, "Adapter(%d) Slot(0): Write operation timed out!", mantis->num);
			rc = -ETIMEDOUT;
			break;
		}
	}
	dprintk(MANTIS_DEBUG, 1, "HIF Write success");
	return rc;
}


int mantis_hif_read_mem(struct mantis_ca *ca, u32 addr)
{
	struct mantis_pci *mantis = ca->ca_priv;
	u32 hif_addr = 0, data, count = 4;

	dprintk(MANTIS_DEBUG, 1, "Adapter(%d) Slot(0): Request HIF Mem Read", mantis->num);
	mutex_lock(&ca->ca_lock);
	hif_addr &= ~MANTIS_GPIF_PCMCIAREG;
	hif_addr &= ~MANTIS_GPIF_PCMCIAIOM;
	hif_addr |=  MANTIS_HIF_STATUS;
	hif_addr |=  addr;

	mmwrite(hif_addr, MANTIS_GPIF_BRADDR);
	mmwrite(count, MANTIS_GPIF_BRBYTES);
	udelay(20);
	mmwrite(hif_addr | MANTIS_GPIF_HIFRDWRN, MANTIS_GPIF_ADDR);

	if (mantis_hif_sbuf_opdone_wait(ca) != 0) {
		dprintk(MANTIS_ERROR, 1, "Adapter(%d) Slot(0): GPIF Smart Buffer operation failed", mantis->num);
		mutex_unlock(&ca->ca_lock);
		return -EREMOTEIO;
	}
	data = mmread(MANTIS_GPIF_DIN);
	mutex_unlock(&ca->ca_lock);
	dprintk(MANTIS_DEBUG, 1, "Mem Read: 0x%02x", data);
	return (data >> 24) & 0xff;
}

int mantis_hif_write_mem(struct mantis_ca *ca, u32 addr, u8 data)
{
	struct mantis_slot *slot = ca->slot;
	struct mantis_pci *mantis = ca->ca_priv;
	u32 hif_addr = 0;

	dprintk(MANTIS_DEBUG, 1, "Adapter(%d) Slot(0): Request HIF Mem Write", mantis->num);
	mutex_lock(&ca->ca_lock);
	hif_addr &= ~MANTIS_GPIF_HIFRDWRN;
	hif_addr &= ~MANTIS_GPIF_PCMCIAREG;
	hif_addr &= ~MANTIS_GPIF_PCMCIAIOM;
	hif_addr |=  MANTIS_HIF_STATUS;
	hif_addr |=  addr;

	mmwrite(slot->slave_cfg, MANTIS_GPIF_CFGSLA); /* Slot0 alone for now */
	mmwrite(hif_addr, MANTIS_GPIF_ADDR);
	mmwrite(data, MANTIS_GPIF_DOUT);

	if (mantis_hif_write_wait(ca) != 0) {
		dprintk(MANTIS_ERROR, 1, "Adapter(%d) Slot(0): HIF Smart Buffer operation failed", mantis->num);
		mutex_unlock(&ca->ca_lock);
		return -EREMOTEIO;
	}
	dprintk(MANTIS_DEBUG, 1, "Mem Write: (0x%02x to 0x%02x)", data, addr);
	mutex_unlock(&ca->ca_lock);

	return 0;
}

int mantis_hif_read_iom(struct mantis_ca *ca, u32 addr)
{
	struct mantis_pci *mantis = ca->ca_priv;
	u32 data, hif_addr = 0;

	dprintk(MANTIS_DEBUG, 1, "Adapter(%d) Slot(0): Request HIF I/O Read", mantis->num);
	mutex_lock(&ca->ca_lock);
	hif_addr &= ~MANTIS_GPIF_PCMCIAREG;
	hif_addr |=  MANTIS_GPIF_PCMCIAIOM;
	hif_addr |=  MANTIS_HIF_STATUS;
	hif_addr |=  addr;

	mmwrite(hif_addr, MANTIS_GPIF_BRADDR);
	mmwrite(1, MANTIS_GPIF_BRBYTES);
	udelay(20);
	mmwrite(hif_addr | MANTIS_GPIF_HIFRDWRN, MANTIS_GPIF_ADDR);

	if (mantis_hif_sbuf_opdone_wait(ca) != 0) {
		dprintk(MANTIS_ERROR, 1, "Adapter(%d) Slot(0): HIF Smart Buffer operation failed", mantis->num);
		mutex_unlock(&ca->ca_lock);
		return -EREMOTEIO;
	}
	data = mmread(MANTIS_GPIF_DIN);
	dprintk(MANTIS_DEBUG, 1, "I/O Read: 0x%02x", data);
	udelay(50);
	mutex_unlock(&ca->ca_lock);

	return (u8) data;
}

int mantis_hif_write_iom(struct mantis_ca *ca, u32 addr, u8 data)
{
	struct mantis_pci *mantis = ca->ca_priv;
	u32 hif_addr = 0;

	dprintk(MANTIS_DEBUG, 1, "Adapter(%d) Slot(0): Request HIF I/O Write", mantis->num);
	mutex_lock(&ca->ca_lock);
	hif_addr &= ~MANTIS_GPIF_PCMCIAREG;
	hif_addr &= ~MANTIS_GPIF_HIFRDWRN;
	hif_addr |=  MANTIS_GPIF_PCMCIAIOM;
	hif_addr |=  MANTIS_HIF_STATUS;
	hif_addr |=  addr;

	mmwrite(hif_addr, MANTIS_GPIF_ADDR);
	mmwrite(data, MANTIS_GPIF_DOUT);

	if (mantis_hif_write_wait(ca) != 0) {
		dprintk(MANTIS_ERROR, 1, "Adapter(%d) Slot(0): HIF Smart Buffer operation failed", mantis->num);
		mutex_unlock(&ca->ca_lock);
		return -EREMOTEIO;
	}
	dprintk(MANTIS_DEBUG, 1, "I/O Write: (0x%02x to 0x%02x)", data, addr);
	mutex_unlock(&ca->ca_lock);
	udelay(50);

	return 0;
}

int mantis_hif_init(struct mantis_ca *ca)
{
	struct mantis_slot *slot = ca->slot;
	struct mantis_pci *mantis = ca->ca_priv;
	u32 irqcfg;

	slot[0].slave_cfg = 0x70773028;
	dprintk(MANTIS_ERROR, 1, "Adapter(%d) Initializing Mantis Host Interface", mantis->num);

	mutex_lock(&ca->ca_lock);
	irqcfg = mmread(MANTIS_GPIF_IRQCFG);
	irqcfg = MANTIS_MASK_BRRDY	|
		 MANTIS_MASK_WRACK	|
		 MANTIS_MASK_EXTIRQ	|
		 MANTIS_MASK_WSTO	|
		 MANTIS_MASK_OTHERR	|
		 MANTIS_MASK_OVFLW;

	mmwrite(irqcfg, MANTIS_GPIF_IRQCFG);
	mutex_unlock(&ca->ca_lock);

	return 0;
}

void mantis_hif_exit(struct mantis_ca *ca)
{
	struct mantis_pci *mantis = ca->ca_priv;
	u32 irqcfg;

	dprintk(MANTIS_ERROR, 1, "Adapter(%d) Exiting Mantis Host Interface", mantis->num);
	mutex_lock(&ca->ca_lock);
	irqcfg = mmread(MANTIS_GPIF_IRQCFG);
	irqcfg &= ~MANTIS_MASK_BRRDY;
	mmwrite(irqcfg, MANTIS_GPIF_IRQCFG);
	mutex_unlock(&ca->ca_lock);
}
