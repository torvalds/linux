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

int mantis_evmgr_init(struct mantis_ca *ca)
{
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(mantis->verbose, MANTIS_DEBUG, 1, "Initializing Mantis Host I/F Event manager");
	return 0;
}

void mantis_evmgr_exit(struct mantis_ca *ca)
{
	struct mantis_pci *mantis = ca->ca_priv;

	dprintk(mantis->verbose, MANTIS_DEBUG, 1, "Mantis Host I/F Event manager exiting");
}
