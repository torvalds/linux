/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/**
 *  fcs_trcmod.h BFA FCS trace modules
 */

#ifndef __FCS_TRCMOD_H__
#define __FCS_TRCMOD_H__

#include <cs/bfa_trc.h>

/*
 * !!! Only append to the enums defined here to avoid any versioning
 * !!! needed between trace utility and driver version
 */
enum {
	BFA_TRC_FCS_FABRIC		= 1,
	BFA_TRC_FCS_VFAPI		= 2,
	BFA_TRC_FCS_PORT		= 3,
	BFA_TRC_FCS_VPORT		= 4,
	BFA_TRC_FCS_VP_API		= 5,
	BFA_TRC_FCS_VPS			= 6,
	BFA_TRC_FCS_RPORT		= 7,
	BFA_TRC_FCS_FCPIM		= 8,
	BFA_TRC_FCS_FCPTM		= 9,
	BFA_TRC_FCS_NS			= 10,
	BFA_TRC_FCS_SCN			= 11,
	BFA_TRC_FCS_LOOP		= 12,
	BFA_TRC_FCS_UF			= 13,
	BFA_TRC_FCS_PPORT		= 14,
	BFA_TRC_FCS_FCPIP		= 15,
	BFA_TRC_FCS_PORT_API	= 16,
	BFA_TRC_FCS_RPORT_API	= 17,
	BFA_TRC_FCS_AUTH		= 18,
	BFA_TRC_FCS_N2N			= 19,
	BFA_TRC_FCS_MS			= 20,
	BFA_TRC_FCS_FDMI		= 21,
	BFA_TRC_FCS_RPORT_FTRS	= 22,
};

#endif /* __FCS_TRCMOD_H__ */
