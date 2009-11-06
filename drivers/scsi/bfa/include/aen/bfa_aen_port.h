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

/* messages define for BFA_AEN_CAT_PORT Module */
#ifndef	__bfa_aen_port_h__
#define	__bfa_aen_port_h__

#include  <cs/bfa_log.h>
#include  <defs/bfa_defs_aen.h>

#define BFA_AEN_PORT_ONLINE \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_ONLINE)
#define BFA_AEN_PORT_OFFLINE \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_OFFLINE)
#define BFA_AEN_PORT_RLIR \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_RLIR)
#define BFA_AEN_PORT_SFP_INSERT \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_SFP_INSERT)
#define BFA_AEN_PORT_SFP_REMOVE \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_SFP_REMOVE)
#define BFA_AEN_PORT_SFP_POM \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_SFP_POM)
#define BFA_AEN_PORT_ENABLE \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_ENABLE)
#define BFA_AEN_PORT_DISABLE \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_DISABLE)
#define BFA_AEN_PORT_AUTH_ON \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_AUTH_ON)
#define BFA_AEN_PORT_AUTH_OFF \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_AUTH_OFF)
#define BFA_AEN_PORT_DISCONNECT \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_DISCONNECT)
#define BFA_AEN_PORT_QOS_NEG \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_QOS_NEG)
#define BFA_AEN_PORT_FABRIC_NAME_CHANGE \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_FABRIC_NAME_CHANGE)
#define BFA_AEN_PORT_SFP_ACCESS_ERROR \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_SFP_ACCESS_ERROR)
#define BFA_AEN_PORT_SFP_UNSUPPORT \
	BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, BFA_PORT_AEN_SFP_UNSUPPORT)

#endif

