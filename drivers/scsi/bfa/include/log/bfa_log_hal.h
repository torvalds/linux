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

/* messages define for HAL Module */
#ifndef	__BFA_LOG_HAL_H__
#define	__BFA_LOG_HAL_H__
#include  <cs/bfa_log.h>
#define BFA_LOG_HAL_ASSERT \
	(((u32) BFA_LOG_HAL_ID << BFA_LOG_MODID_OFFSET) | 1)
#define BFA_LOG_HAL_HEARTBEAT_FAILURE \
	(((u32) BFA_LOG_HAL_ID << BFA_LOG_MODID_OFFSET) | 2)
#define BFA_LOG_HAL_FCPIM_PARM_INVALID \
	(((u32) BFA_LOG_HAL_ID << BFA_LOG_MODID_OFFSET) | 3)
#define BFA_LOG_HAL_SM_ASSERT \
	(((u32) BFA_LOG_HAL_ID << BFA_LOG_MODID_OFFSET) | 4)
#define BFA_LOG_HAL_DRIVER_ERROR \
	(((u32) BFA_LOG_HAL_ID << BFA_LOG_MODID_OFFSET) | 5)
#define BFA_LOG_HAL_DRIVER_CONFIG_ERROR \
	(((u32) BFA_LOG_HAL_ID << BFA_LOG_MODID_OFFSET) | 6)
#define BFA_LOG_HAL_MBOX_ERROR \
	(((u32) BFA_LOG_HAL_ID << BFA_LOG_MODID_OFFSET) | 7)
#endif
