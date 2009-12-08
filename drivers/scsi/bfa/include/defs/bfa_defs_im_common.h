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

#ifndef __BFA_DEFS_IM_COMMON_H__
#define __BFA_DEFS_IM_COMMON_H__

#define	BFA_ADAPTER_NAME_LEN	256
#define BFA_ADAPTER_GUID_LEN    256
#define RESERVED_VLAN_NAME      L"PORT VLAN"
#define PASSTHRU_VLAN_NAME      L"PASSTHRU VLAN"

	u64	tx_pkt_cnt;
	u64	rx_pkt_cnt;
	u32	duration;
	u8		status;
} bfa_im_stats_t, *pbfa_im_stats_t;

#endif /* __BFA_DEFS_IM_COMMON_H__ */
