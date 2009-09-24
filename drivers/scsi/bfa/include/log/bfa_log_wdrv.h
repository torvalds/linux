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

/*
 * messages define for WDRV Module
 */
#ifndef	__BFA_LOG_WDRV_H__
#define	__BFA_LOG_WDRV_H__
#include  <cs/bfa_log.h>
#define BFA_LOG_WDRV_IOC_INIT_ERROR 	\
	(((u32) BFA_LOG_WDRV_ID << BFA_LOG_MODID_OFFSET) | 1)
#define BFA_LOG_WDRV_IOC_INTERNAL_ERROR \
	(((u32) BFA_LOG_WDRV_ID << BFA_LOG_MODID_OFFSET) | 2)
#define BFA_LOG_WDRV_IOC_START_ERROR 	\
	(((u32) BFA_LOG_WDRV_ID << BFA_LOG_MODID_OFFSET) | 3)
#define BFA_LOG_WDRV_IOC_STOP_ERROR 	\
	(((u32) BFA_LOG_WDRV_ID << BFA_LOG_MODID_OFFSET) | 4)
#define BFA_LOG_WDRV_INSUFFICIENT_RESOURCES \
	(((u32) BFA_LOG_WDRV_ID << BFA_LOG_MODID_OFFSET) | 5)
#define BFA_LOG_WDRV_BASE_ADDRESS_MAP_ERROR \
	(((u32) BFA_LOG_WDRV_ID << BFA_LOG_MODID_OFFSET) | 6)
#endif
