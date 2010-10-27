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
#ifndef __BFA_DEFS_ADAPTER_H__
#define __BFA_DEFS_ADAPTER_H__

#include <protocol/types.h>
#include <defs/bfa_defs_version.h>
#include <defs/bfa_defs_mfg.h>

/**
 * BFA adapter level attributes.
 */
enum {
	BFA_ADAPTER_SERIAL_NUM_LEN = STRSZ(BFA_MFG_SERIALNUM_SIZE),
					/*
					 *!< adapter serial num length
					 */
	BFA_ADAPTER_MODEL_NAME_LEN  = 16,  /*  model name length */
	BFA_ADAPTER_MODEL_DESCR_LEN = 128, /*  model description length */
	BFA_ADAPTER_MFG_NAME_LEN    = 8,   /*  manufacturer name length */
	BFA_ADAPTER_SYM_NAME_LEN    = 64,  /*  adapter symbolic name length */
	BFA_ADAPTER_OS_TYPE_LEN	    = 64,  /*  adapter os type length */
};

struct bfa_adapter_attr_s {
	char            manufacturer[BFA_ADAPTER_MFG_NAME_LEN];
	char            serial_num[BFA_ADAPTER_SERIAL_NUM_LEN];
	u32		card_type;
	char            model[BFA_ADAPTER_MODEL_NAME_LEN];
	char            model_descr[BFA_ADAPTER_MODEL_DESCR_LEN];
	wwn_t           pwwn;
	char            node_symname[FC_SYMNAME_MAX];
	char            hw_ver[BFA_VERSION_LEN];
	char            fw_ver[BFA_VERSION_LEN];
	char            optrom_ver[BFA_VERSION_LEN];
	char            os_type[BFA_ADAPTER_OS_TYPE_LEN];
	struct bfa_mfg_vpd_s	vpd;
	struct mac_s	mac;

	u8		nports;
	u8		max_speed;
	u8		prototype;
	char	        asic_rev;

	u8         pcie_gen;
	u8         pcie_lanes_orig;
	u8         pcie_lanes;
	u8	        cna_capable;
	u8         is_mezz;
};

/**
 * BFA adapter level events
 * Arguments below are in BFAL context from Mgmt
 * BFA_PORT_AEN_ADD:        [in]: None     [out]: serial_num, pwwn, nports
 * BFA_PORT_AEN_REMOVE:     [in]: pwwn     [out]: serial_num, pwwn, nports
 */
enum bfa_adapter_aen_event {
	BFA_ADAPTER_AEN_ADD 	= 1,	/*  New Adapter found event */
	BFA_ADAPTER_AEN_REMOVE 	= 2,	/*  Adapter removed event */
};

struct bfa_adapter_aen_data_s {
	char            serial_num[BFA_ADAPTER_SERIAL_NUM_LEN];
	u32        nports;	/*  Number of NPorts */
	wwn_t           pwwn;	/*  WWN of one of its physical port */
};

#endif /* __BFA_DEFS_ADAPTER_H__ */
