/*
	Mantis VP-1034 driver

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
#include "mantis_vp1034.h"

struct mb86a16_config vp1034_config = {
	.demod_address	= 0x08,
	.set_voltage	= vp1034_set_voltage,
};

#define MANTIS_MODEL_NAME	"VP-1034"
#define MANTIS_DEV_TYPE		"DVB-S/DSS"

struct mantis_hwconfig vp1034_mantis_config = {
	.model_name	= MANTIS_MODEL_NAME,
	.dev_type	= MANTIS_DEV_TYPE,
};

int vp1034_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{
	struct mantis_pci *mantis = fe->dvb->priv;

	switch (voltage) {
	case SEC_VOLTAGE_13:
		mmwrite((mmread(MANTIS_GPIF_ADDR)) | voltage, MANTIS_GPIF_ADDR);
		dprintk(verbose, MANTIS_ERROR, 1, "Polarization=[13V]");
		break;
	case SEC_VOLTAGE_18:
		mmwrite((mmread(MANTIS_GPIF_ADDR)) & voltage, MANTIS_GPIF_ADDR);
		dprintk(verbose, MANTIS_ERROR, 1, "Polarization=[18V]");
		break;
	case SEC_VOLTAGE_OFF:
		dprintk(verbose, MANTIS_ERROR, 1, "Frontend (dummy) POWERDOWN");
		break;
	default:
		dprintk(verbose, MANTIS_ERROR, 1, "Invalid = (%d)", (u32 ) voltage);
		return -EINVAL;
	}
	mmwrite(0x00, MANTIS_GPIF_DOUT);

	return 0;
}
