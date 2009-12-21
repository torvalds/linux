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

/* messages define for LINUX Module */
#ifndef	__BFA_LOG_LINUX_H__
#define	__BFA_LOG_LINUX_H__
#include  <cs/bfa_log.h>
#define BFA_LOG_LINUX_DEVICE_CLAIMED \
		(((u32) BFA_LOG_LINUX_ID << BFA_LOG_MODID_OFFSET) | 1)
#define BFA_LOG_LINUX_HASH_INIT_FAILED \
		(((u32) BFA_LOG_LINUX_ID << BFA_LOG_MODID_OFFSET) | 2)
#define BFA_LOG_LINUX_SYSFS_FAILED \
		(((u32) BFA_LOG_LINUX_ID << BFA_LOG_MODID_OFFSET) | 3)
#define BFA_LOG_LINUX_MEM_ALLOC_FAILED \
		(((u32) BFA_LOG_LINUX_ID << BFA_LOG_MODID_OFFSET) | 4)
#define BFA_LOG_LINUX_DRIVER_REGISTRATION_FAILED \
		(((u32) BFA_LOG_LINUX_ID << BFA_LOG_MODID_OFFSET) | 5)
#define BFA_LOG_LINUX_ITNIM_FREE \
		(((u32) BFA_LOG_LINUX_ID << BFA_LOG_MODID_OFFSET) | 6)
#define BFA_LOG_LINUX_ITNIM_ONLINE \
		(((u32) BFA_LOG_LINUX_ID << BFA_LOG_MODID_OFFSET) | 7)
#define BFA_LOG_LINUX_ITNIM_OFFLINE \
		(((u32) BFA_LOG_LINUX_ID << BFA_LOG_MODID_OFFSET) | 8)
#define BFA_LOG_LINUX_SCSI_HOST_FREE \
		(((u32) BFA_LOG_LINUX_ID << BFA_LOG_MODID_OFFSET) | 9)
#define BFA_LOG_LINUX_SCSI_ABORT \
		(((u32) BFA_LOG_LINUX_ID << BFA_LOG_MODID_OFFSET) | 10)
#define BFA_LOG_LINUX_SCSI_ABORT_COMP \
		(((u32) BFA_LOG_LINUX_ID << BFA_LOG_MODID_OFFSET) | 11)
#endif
