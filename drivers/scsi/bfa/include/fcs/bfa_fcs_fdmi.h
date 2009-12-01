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
 *  bfa_fcs_fdmi.h BFA fcs fdmi module public interface
 */

#ifndef __BFA_FCS_FDMI_H__
#define __BFA_FCS_FDMI_H__
#include <bfa_os_inc.h>
#include <protocol/fdmi.h>

#define	BFA_FCS_FDMI_SUPORTED_SPEEDS  (FDMI_TRANS_SPEED_1G  | \
					FDMI_TRANS_SPEED_2G | \
					FDMI_TRANS_SPEED_4G | \
					FDMI_TRANS_SPEED_8G)

/*
* HBA Attribute Block : BFA internal representation. Note : Some variable
* sizes have been trimmed to suit BFA For Ex : Model will be "Brocade". Based
 * on this the size has been reduced to 16 bytes from the standard's 64 bytes.
 */
struct bfa_fcs_fdmi_hba_attr_s {
	wwn_t           node_name;
	u8         manufacturer[64];
	u8         serial_num[64];
	u8         model[16];
	u8         model_desc[256];
	u8         hw_version[8];
	u8         driver_version[8];
	u8         option_rom_ver[BFA_VERSION_LEN];
	u8         fw_version[8];
	u8         os_name[256];
	u32        max_ct_pyld;
};

/*
 * Port Attribute Block
 */
struct bfa_fcs_fdmi_port_attr_s {
	u8         supp_fc4_types[32];	/* supported FC4 types */
	u32        supp_speed;	/* supported speed */
	u32        curr_speed;	/* current Speed */
	u32        max_frm_size;	/* max frame size */
	u8         os_device_name[256];	/* OS device Name */
	u8         host_name[256];	/* host name */
};

#endif /* __BFA_FCS_FDMI_H__ */
