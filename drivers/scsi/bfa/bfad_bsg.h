/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
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
#ifndef BFAD_BSG_H
#define BFAD_BSG_H

#include "bfa_defs.h"
#include "bfa_defs_fcs.h"

/* Definitions of vendor unique structures and command codes passed in
 * using FC_BSG_HST_VENDOR message code.
 */
enum {
	IOCMD_IOC_GET_ATTR = 0x1,
	IOCMD_IOC_GET_INFO,
	IOCMD_PORT_GET_ATTR,
	IOCMD_LPORT_GET_ATTR,
	IOCMD_RPORT_GET_ADDR,
	IOCMD_FABRIC_GET_LPORTS,
	IOCMD_ITNIM_GET_ATTR,
	IOCMD_IOC_PCIFN_CFG,
	IOCMD_PCIFN_CREATE,
	IOCMD_PCIFN_DELETE,
	IOCMD_PCIFN_BW,
	IOCMD_ADAPTER_CFG_MODE,
	IOCMD_PORT_CFG_MODE,
	IOCMD_FLASH_ENABLE_OPTROM,
	IOCMD_FLASH_DISABLE_OPTROM,
};

struct bfa_bsg_gen_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
};

struct bfa_bsg_ioc_info_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	char		serialnum[64];
	char		hwpath[BFA_STRING_32];
	char		adapter_hwpath[BFA_STRING_32];
	char		guid[BFA_ADAPTER_SYM_NAME_LEN*2];
	char		name[BFA_ADAPTER_SYM_NAME_LEN];
	char		port_name[BFA_ADAPTER_SYM_NAME_LEN];
	char		eth_name[BFA_ADAPTER_SYM_NAME_LEN];
	wwn_t		pwwn;
	wwn_t		nwwn;
	wwn_t		factorypwwn;
	wwn_t		factorynwwn;
	mac_t		mac;
	mac_t		factory_mac; /* Factory mac address */
	mac_t		current_mac; /* Currently assigned mac address */
	enum bfa_ioc_type_e	ioc_type;
	u16		pvid; /* Port vlan id */
	u16		rsvd1;
	u32		host;
	u32		bandwidth; /* For PF support */
	u32		rsvd2;
};

struct bfa_bsg_ioc_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_ioc_attr_s  ioc_attr;
};

struct bfa_bsg_port_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_port_attr_s	attr;
};

struct bfa_bsg_lport_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		pwwn;
	struct bfa_lport_attr_s port_attr;
};

struct bfa_bsg_rport_scsi_addr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		pwwn;
	wwn_t		rpwwn;
	u32		host;
	u32		bus;
	u32		target;
	u32		lun;
};

struct bfa_bsg_fabric_get_lports_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	u64		buf_ptr;
	u32		nports;
	u32		rsvd;
};

struct bfa_bsg_itnim_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		lpwwn;
	wwn_t		rpwwn;
	struct bfa_itnim_attr_s	attr;
};

struct bfa_bsg_pcifn_cfg_s {
	bfa_status_t		status;
	u16			bfad_num;
	u16			rsvd;
	struct bfa_ablk_cfg_s	pcifn_cfg;
};

struct bfa_bsg_pcifn_s {
	bfa_status_t		status;
	u16			bfad_num;
	u16			pcifn_id;
	u32			bandwidth;
	u8			port;
	enum bfi_pcifn_class	pcifn_class;
	u8			rsvd[1];
};

struct bfa_bsg_adapter_cfg_mode_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_adapter_cfg_mode_s	cfg;
};

struct bfa_bsg_port_cfg_mode_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		instance;
	struct bfa_port_cfg_mode_s cfg;
};

struct bfa_bsg_fcpt_s {
	bfa_status_t    status;
	u16		vf_id;
	wwn_t		lpwwn;
	wwn_t		dpwwn;
	u32		tsecs;
	int		cts;
	enum fc_cos	cos;
	struct fchs_s	fchs;
};
#define bfa_bsg_fcpt_t struct bfa_bsg_fcpt_s

struct bfa_bsg_data {
	int payload_len;
	void *payload;
};

#define bfad_chk_iocmd_sz(__payload_len, __hdrsz, __bufsz)	\
	(((__payload_len) != ((__hdrsz) + (__bufsz))) ?		\
	 BFA_STATUS_FAILED : BFA_STATUS_OK)

#endif /* BFAD_BSG_H */
