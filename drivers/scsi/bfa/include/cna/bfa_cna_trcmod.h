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
 *  bfa_cna_trcmod.h CNA trace modules
 */

#ifndef __BFA_CNA_TRCMOD_H__
#define __BFA_CNA_TRCMOD_H__

#include <cs/bfa_trc.h>

/*
 * !!! Only append to the enums defined here to avoid any versioning
 * !!! needed between trace utility and driver version
 */
enum {
	BFA_TRC_CNA_CEE		= 1,
	BFA_TRC_CNA_PORT	= 2,
	BFA_TRC_CNA_IOC     = 3,
	BFA_TRC_CNA_DIAG    = 4,
	BFA_TRC_CNA_IOC_CB  = 5,
	BFA_TRC_CNA_IOC_CT  = 6,
};

#endif /* __BFA_CNA_TRCMOD_H__ */
