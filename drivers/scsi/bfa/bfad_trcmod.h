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
 *  bfad_trcmod.h Linux driver trace modules
 */


#ifndef __BFAD_TRCMOD_H__
#define __BFAD_TRCMOD_H__

#include <cs/bfa_trc.h>

/*
 * !!! Only append to the enums defined here to avoid any versioning
 * !!! needed between trace utility and driver version
 */
enum {
	/* 2.6 Driver */
	BFA_TRC_LDRV_BFAD		= 1,
	BFA_TRC_LDRV_BFAD_2_6		= 2,
	BFA_TRC_LDRV_BFAD_2_6_9		= 3,
	BFA_TRC_LDRV_BFAD_2_6_10	= 4,
	BFA_TRC_LDRV_INTR		= 5,
	BFA_TRC_LDRV_IOCTL		= 6,
	BFA_TRC_LDRV_OS			= 7,
	BFA_TRC_LDRV_IM			= 8,
	BFA_TRC_LDRV_IM_2_6		= 9,
	BFA_TRC_LDRV_IM_2_6_9		= 10,
	BFA_TRC_LDRV_IM_2_6_10		= 11,
	BFA_TRC_LDRV_TM			= 12,
	BFA_TRC_LDRV_IPFC		= 13,
	BFA_TRC_LDRV_IM_2_4		= 14,
	BFA_TRC_LDRV_IM_VMW		= 15,
	BFA_TRC_LDRV_IM_LT_2_6_10	= 16,
};

#endif /* __BFAD_TRCMOD_H__ */
