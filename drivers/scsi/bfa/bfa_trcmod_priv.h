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
 *  hal_trcmod.h BFA trace modules
 */

#ifndef __BFA_TRCMOD_PRIV_H__
#define __BFA_TRCMOD_PRIV_H__

#include <cs/bfa_trc.h>

/*
 * !!! Only append to the enums defined here to avoid any versioning
 * !!! needed between trace utility and driver version
 */
enum {
	BFA_TRC_HAL_INTR	= 1,
	BFA_TRC_HAL_FCXP	= 2,
	BFA_TRC_HAL_UF		= 3,
	BFA_TRC_HAL_RPORT	= 4,
	BFA_TRC_HAL_FCPIM	= 5,
	BFA_TRC_HAL_IOIM	= 6,
	BFA_TRC_HAL_TSKIM	= 7,
	BFA_TRC_HAL_ITNIM	= 8,
	BFA_TRC_HAL_FCPORT	= 9,
	BFA_TRC_HAL_SGPG	= 10,
	BFA_TRC_HAL_FLASH	= 11,
	BFA_TRC_HAL_DEBUG	= 12,
	BFA_TRC_HAL_WWN		= 13,
	BFA_TRC_HAL_FLASH_RAW	= 14,
	BFA_TRC_HAL_SBOOT	= 15,
	BFA_TRC_HAL_SBOOT_IO	= 16,
	BFA_TRC_HAL_SBOOT_INTR	= 17,
	BFA_TRC_HAL_SBTEST	= 18,
	BFA_TRC_HAL_IPFC	= 19,
	BFA_TRC_HAL_IOCFC	= 20,
	BFA_TRC_HAL_FCPTM	= 21,
	BFA_TRC_HAL_IOTM	= 22,
	BFA_TRC_HAL_TSKTM	= 23,
	BFA_TRC_HAL_TIN		= 24,
	BFA_TRC_HAL_LPS		= 25,
	BFA_TRC_HAL_FCDIAG	= 26,
	BFA_TRC_HAL_PBIND	= 27,
	BFA_TRC_HAL_IOCFC_CT	= 28,
	BFA_TRC_HAL_IOCFC_CB	= 29,
	BFA_TRC_HAL_IOCFC_Q	= 30,
};

#endif /* __BFA_TRCMOD_PRIV_H__ */
