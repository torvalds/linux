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

/* messages define for BFA_AEN_CAT_IOC Module */
#ifndef	__bfa_aen_ioc_h__
#define	__bfa_aen_ioc_h__

#include  <cs/bfa_log.h>
#include  <defs/bfa_defs_aen.h>

#define BFA_AEN_IOC_HBGOOD \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_IOC, BFA_IOC_AEN_HBGOOD)
#define BFA_AEN_IOC_HBFAIL \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_IOC, BFA_IOC_AEN_HBFAIL)
#define BFA_AEN_IOC_ENABLE \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_IOC, BFA_IOC_AEN_ENABLE)
#define BFA_AEN_IOC_DISABLE \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_IOC, BFA_IOC_AEN_DISABLE)
#define BFA_AEN_IOC_FWMISMATCH \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_IOC, BFA_IOC_AEN_FWMISMATCH)
#define BFA_AEN_IOC_FWCFG_ERROR \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_IOC, BFA_IOC_AEN_FWCFG_ERROR)
#define BFA_AEN_IOC_INVALID_VENDOR      \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_IOC, BFA_IOC_AEN_INVALID_VENDOR)
#define BFA_AEN_IOC_INVALID_NWWN        \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_IOC, BFA_IOC_AEN_INVALID_NWWN)
#define BFA_AEN_IOC_INVALID_PWWN        \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_IOC, BFA_IOC_AEN_INVALID_PWWN)

#endif

