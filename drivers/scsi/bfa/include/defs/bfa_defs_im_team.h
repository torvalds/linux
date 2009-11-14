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

#ifndef __BFA_DEFS_IM_TEAM_H__
#define __BFA_DEFS_IM_TEAM_H__

#include <protocol/types.h>

#define	BFA_TEAM_MAX_PORTS	8
#define	BFA_TEAM_NAME_LEN	256
#define BFA_MAX_NUM_TEAMS	16
#define BFA_TEAM_INVALID_DELAY -1

	BFA_LACP_RATE_SLOW = 1,
	BFA_LACP_RATE_FAST
} bfa_im_lacp_rate_t;

	BFA_TEAM_MODE_FAIL_OVER = 1,
	BFA_TEAM_MODE_FAIL_BACK,
	BFA_TEAM_MODE_LACP,
	BFA_TEAM_MODE_NONE
} bfa_im_team_mode_t;

	BFA_XMIT_POLICY_L2 = 1,
	BFA_XMIT_POLICY_L3_L4
} bfa_im_xmit_policy_t;

	bfa_im_team_mode_t     team_mode;
	bfa_im_lacp_rate_t     lacp_rate;
	bfa_im_xmit_policy_t   xmit_policy;
	int   	          delay;
	wchar_t    	  primary[BFA_ADAPTER_NAME_LEN];
	wchar_t        	  preferred_primary[BFA_ADAPTER_NAME_LEN];
	mac_t	          mac;
	u16       	  num_ports;
	u16          num_vlans;
	u16 vlan_list[BFA_MAX_VLANS_PER_PORT];
	wchar_t	 team_guid_list[BFA_TEAM_MAX_PORTS][BFA_ADAPTER_GUID_LEN];
	wchar_t	 ioc_name_list[BFA_TEAM_MAX_PORTS][BFA_ADAPTER_NAME_LEN];
} bfa_im_team_attr_t;

	wchar_t		             team_name[BFA_TEAM_NAME_LEN];
	bfa_im_xmit_policy_t	 xmit_policy;
	int                 	 delay;
	wchar_t                	 primary[BFA_ADAPTER_NAME_LEN];
	wchar_t               	 preferred_primary[BFA_ADAPTER_NAME_LEN];
} bfa_im_team_edit_t, *pbfa_im_team_edit_t;

	wchar_t					team_name[BFA_TEAM_NAME_LEN];
	bfa_im_team_mode_t      team_mode;
	mac_t	               	mac;
} bfa_im_team_info_t;

	bfa_im_team_info_t 		team_info[BFA_MAX_NUM_TEAMS];
	u16 				num_teams;
} bfa_im_team_list_t, *pbfa_im_team_list_t;

#endif /* __BFA_DEFS_IM_TEAM_H__ */
