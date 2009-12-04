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

static int mantis_ca_read_attr_mem(struct dvb_ca_en50221 *en50221, int slot, int addr)
{
	struct mantis_ca *ca = en50221->data;
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(verbose, MANTIS_DEBUG, 1, "Slot(%d): Request Attribute Mem Read", slot);

	if (slot != 0)
		return -EINVAL;

	return mantis_hif_read_mem(ca, addr);
}

static int mantis_ca_write_attr_mem(struct dvb_ca_en50221 *en50221, int slot, int addr, u8 data)
{
	struct mantis_ca *ca = en50221->data;
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(verbose, MANTIS_DEBUG, 1, "Slot(%d): Request Attribute Mem Write", slot);

	if (slot != 0)
		return -EINVAL;

	return mantis_hif_write_mem(ca, addr, data);
}

static int mantis_ca_read_cam_ctl(struct dvb_ca_en50221 *en50221, int slot, u8 addr)
{
	struct mantis_ca *ca = en50221->data;
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(verbose, MANTIS_DEBUG, 1, "Slot(%d): Request CAM control Read", slot);

	if (slot != 0)
		return -EINVAL;

	return mantis_hif_read_iom(ca, addr);
}

static int mantis_ca_write_cam_ctl(struct dvb_ca_en50221 *en50221, int slot, u8 addr, u8 data)
{
	struct mantis_ca *ca = en50221->data;
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(verbose, MANTIS_DEBUG, 1, "Slot(%d): Request CAM control Write", slot);

	if (slot != 0)
		return -EINVAL;

	return mantis_hif_write_iom(ca, addr, data);
}

static int mantis_ca_slot_reset(struct dvb_ca_en50221 *en50221, int slot)
{
	struct mantis_ca *ca = en50221->data;
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(verbose, MANTIS_DEBUG, 1, "Slot(%d): Slot RESET", slot);

	return 0;
}

static int mantis_ca_slot_shutdown(struct dvb_ca_en50221 *en50221, int slot)
{
	struct mantis_ca *ca = en50221->data;
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(verbose, MANTIS_DEBUG, 1, "Slot(%d): Slot shutdown", slot);

	return 0;
}

static int mantis_ts_control(struct dvb_ca_en50221 *en50221, int slot)
{
	struct mantis_ca *ca = en50221->data;
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(verbose, MANTIS_DEBUG, 1, "Slot(%d): TS control", slot);

	return 0;
}

static int mantis_slot_status(struct dvb_ca_en50221 *en50221, int slot, int open)
{
	struct mantis_ca *ca = en50221->data;
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(verbose, MANTIS_DEBUG, 1, "Slot(%d): Poll Slot status", slot);

	if (ca->slot_state == MODULE_INSERTED)
		return DVB_CA_EN50221_POLL_CAM_PRESENT | DVB_CA_EN50221_POLL_CAM_READY;

	return 0;
}

int mantis_ca_init(struct mantis_pci *mantis)
{
	struct dvb_adapter *dvb_adapter = &mantis->dvb_adapter;
	struct mantis_ca *ca;
	int ca_flags = 0, result;

	dprintk(verbose, MANTIS_DEBUG, 1, "Initializing Mantis CA");
	if (!(ca = kzalloc(sizeof (struct mantis_ca), GFP_KERNEL))) {
		dprintk(verbose, MANTIS_ERROR, 1, "Out of memory!, exiting ..");
		result = -ENOMEM;
		goto err;
	}

	ca->ca_priv = mantis;
	mantis->mantis_ca = ca;

	/* register CA interface */
	ca->en50221.owner		= THIS_MODULE;
	ca->en50221.read_attribute_mem	= mantis_ca_read_attr_mem;
	ca->en50221.write_attribute_mem	= mantis_ca_write_attr_mem;
	ca->en50221.read_cam_control	= mantis_ca_read_cam_ctl;
	ca->en50221.write_cam_control	= mantis_ca_write_cam_ctl;
	ca->en50221.slot_reset		= mantis_ca_slot_reset;
	ca->en50221.slot_shutdown	= mantis_ca_slot_shutdown;
	ca->en50221.slot_ts_enable	= mantis_ts_control;
	ca->en50221.poll_slot_status	= mantis_slot_status;
	ca->en50221.data		= ca;

	dprintk(verbose, MANTIS_ERROR, 1, "Registering EN50221 device");
	if ((result = dvb_ca_en50221_init(dvb_adapter, &ca->en50221, ca_flags, 1)) != 0) {
		dprintk(verbose, MANTIS_ERROR, 1, "EN50221: Initialization failed");
		goto err;
	}
	dprintk(verbose, MANTIS_ERROR, 1, "Registered EN50221 device");
	mantis_evmgr_init(ca);
	return 0;
err:
	kfree(ca);
	return result;
}

void mantis_ca_exit(struct mantis_pci *mantis)
{
	struct mantis_ca *ca = mantis->mantis_ca;
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(verbose, MANTIS_DEBUG, 1, "Mantis CA exit");

	mantis_evmgr_exit(ca);
	dprintk(verbose, MANTIS_ERROR, 1, "Unregistering EN50221 device");
	dvb_ca_en50221_release(&ca->en50221);

	kfree(ca);
}
