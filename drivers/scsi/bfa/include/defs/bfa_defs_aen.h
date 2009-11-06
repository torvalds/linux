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

#ifndef __BFA_DEFS_AEN_H__
#define __BFA_DEFS_AEN_H__

#include <defs/bfa_defs_types.h>
#include <defs/bfa_defs_ioc.h>
#include <defs/bfa_defs_adapter.h>
#include <defs/bfa_defs_port.h>
#include <defs/bfa_defs_lport.h>
#include <defs/bfa_defs_rport.h>
#include <defs/bfa_defs_itnim.h>
#include <defs/bfa_defs_tin.h>
#include <defs/bfa_defs_ipfc.h>
#include <defs/bfa_defs_audit.h>
#include <defs/bfa_defs_ethport.h>

enum bfa_aen_category {
	BFA_AEN_CAT_ADAPTER 	= 1,
	BFA_AEN_CAT_PORT 	= 2,
	BFA_AEN_CAT_LPORT 	= 3,
	BFA_AEN_CAT_RPORT 	= 4,
	BFA_AEN_CAT_ITNIM 	= 5,
	BFA_AEN_CAT_TIN 	= 6,
	BFA_AEN_CAT_IPFC 	= 7,
	BFA_AEN_CAT_AUDIT 	= 8,
	BFA_AEN_CAT_IOC 	= 9,
	BFA_AEN_CAT_ETHPORT	= 10,
	BFA_AEN_MAX_CAT 	= 10
};

#pragma pack(1)
union bfa_aen_data_u {
	struct bfa_adapter_aen_data_s 	adapter;
	struct bfa_port_aen_data_s 	port;
	struct bfa_lport_aen_data_s 	lport;
	struct bfa_rport_aen_data_s 	rport;
	struct bfa_itnim_aen_data_s 	itnim;
	struct bfa_audit_aen_data_s 	audit;
	struct bfa_ioc_aen_data_s 	ioc;
	struct bfa_ethport_aen_data_s 	ethport;
};

struct bfa_aen_entry_s {
	enum bfa_aen_category 	aen_category;
	int			aen_type;
	union bfa_aen_data_u  	aen_data;
	struct bfa_timeval_s   	aen_tv;
	s32         	seq_num;
	s32         	bfad_num;
	s32         	rsvd[1];
};

#pragma pack()

#define bfa_aen_event_t int

#endif /* __BFA_DEFS_AEN_H__ */
