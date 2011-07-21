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
	IOCMD_IOC_ENABLE = 0x1,
	IOCMD_IOC_DISABLE,
	IOCMD_IOC_GET_ATTR,
	IOCMD_IOC_GET_INFO,
	IOCMD_IOC_GET_STATS,
	IOCMD_IOC_GET_FWSTATS,
	IOCMD_IOC_RESET_STATS,
	IOCMD_IOC_RESET_FWSTATS,
	IOCMD_IOC_SET_ADAPTER_NAME,
	IOCMD_IOC_SET_PORT_NAME,
	IOCMD_IOCFC_GET_ATTR,
	IOCMD_IOCFC_SET_INTR,
	IOCMD_PORT_ENABLE,
	IOCMD_PORT_DISABLE,
	IOCMD_PORT_GET_ATTR,
	IOCMD_PORT_GET_STATS,
	IOCMD_PORT_RESET_STATS,
	IOCMD_PORT_CFG_TOPO,
	IOCMD_PORT_CFG_SPEED,
	IOCMD_PORT_CFG_ALPA,
	IOCMD_PORT_CFG_MAXFRSZ,
	IOCMD_PORT_CLR_ALPA,
	IOCMD_PORT_BBSC_ENABLE,
	IOCMD_PORT_BBSC_DISABLE,
	IOCMD_LPORT_GET_ATTR,
	IOCMD_LPORT_GET_RPORTS,
	IOCMD_LPORT_GET_STATS,
	IOCMD_LPORT_RESET_STATS,
	IOCMD_LPORT_GET_IOSTATS,
	IOCMD_RPORT_GET_ATTR,
	IOCMD_RPORT_GET_ADDR,
	IOCMD_RPORT_GET_STATS,
	IOCMD_RPORT_RESET_STATS,
	IOCMD_RPORT_SET_SPEED,
	IOCMD_VPORT_GET_ATTR,
	IOCMD_VPORT_GET_STATS,
	IOCMD_VPORT_RESET_STATS,
	IOCMD_FABRIC_GET_LPORTS,
	IOCMD_RATELIM_ENABLE,
	IOCMD_RATELIM_DISABLE,
	IOCMD_RATELIM_DEF_SPEED,
	IOCMD_FCPIM_FAILOVER,
	IOCMD_FCPIM_MODSTATS,
	IOCMD_FCPIM_MODSTATSCLR,
	IOCMD_FCPIM_DEL_ITN_STATS,
	IOCMD_ITNIM_GET_ATTR,
	IOCMD_ITNIM_GET_IOSTATS,
	IOCMD_ITNIM_RESET_STATS,
	IOCMD_ITNIM_GET_ITNSTATS,
	IOCMD_IOC_PCIFN_CFG,
	IOCMD_FCPORT_ENABLE,
	IOCMD_FCPORT_DISABLE,
	IOCMD_PCIFN_CREATE,
	IOCMD_PCIFN_DELETE,
	IOCMD_PCIFN_BW,
	IOCMD_ADAPTER_CFG_MODE,
	IOCMD_PORT_CFG_MODE,
	IOCMD_FLASH_ENABLE_OPTROM,
	IOCMD_FLASH_DISABLE_OPTROM,
	IOCMD_FAA_ENABLE,
	IOCMD_FAA_DISABLE,
	IOCMD_FAA_QUERY,
	IOCMD_CEE_GET_ATTR,
	IOCMD_CEE_GET_STATS,
	IOCMD_CEE_RESET_STATS,
	IOCMD_SFP_MEDIA,
	IOCMD_SFP_SPEED,
	IOCMD_FLASH_GET_ATTR,
	IOCMD_FLASH_ERASE_PART,
	IOCMD_FLASH_UPDATE_PART,
	IOCMD_FLASH_READ_PART,
	IOCMD_DIAG_TEMP,
	IOCMD_DIAG_MEMTEST,
	IOCMD_DIAG_LOOPBACK,
	IOCMD_DIAG_FWPING,
	IOCMD_DIAG_QUEUETEST,
	IOCMD_DIAG_SFP,
	IOCMD_DIAG_LED,
	IOCMD_DIAG_BEACON_LPORT,
	IOCMD_DIAG_LB_STAT,
	IOCMD_PHY_GET_ATTR,
	IOCMD_PHY_GET_STATS,
	IOCMD_PHY_UPDATE_FW,
	IOCMD_PHY_READ_FW,
	IOCMD_VHBA_QUERY,
	IOCMD_DEBUG_PORTLOG,
	IOCMD_DEBUG_FW_CORE,
	IOCMD_DEBUG_FW_STATE_CLR,
	IOCMD_DEBUG_PORTLOG_CLR,
	IOCMD_DEBUG_START_DTRC,
	IOCMD_DEBUG_STOP_DTRC,
	IOCMD_DEBUG_PORTLOG_CTL,
	IOCMD_FCPIM_PROFILE_ON,
	IOCMD_FCPIM_PROFILE_OFF,
	IOCMD_ITNIM_GET_IOPROFILE,
	IOCMD_FCPORT_GET_STATS,
	IOCMD_FCPORT_RESET_STATS,
	IOCMD_BOOT_CFG,
	IOCMD_BOOT_QUERY,
	IOCMD_PREBOOT_QUERY,
	IOCMD_ETHBOOT_CFG,
	IOCMD_ETHBOOT_QUERY,
	IOCMD_TRUNK_ENABLE,
	IOCMD_TRUNK_DISABLE,
	IOCMD_TRUNK_GET_ATTR,
	IOCMD_QOS_ENABLE,
	IOCMD_QOS_DISABLE,
	IOCMD_QOS_GET_ATTR,
	IOCMD_QOS_GET_VC_ATTR,
	IOCMD_QOS_GET_STATS,
	IOCMD_QOS_RESET_STATS,
	IOCMD_VF_GET_STATS,
	IOCMD_VF_RESET_STATS,
};

struct bfa_bsg_gen_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
};

struct bfa_bsg_portlogctl_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	bfa_boolean_t	ctl;
	int		inst_no;
};

struct bfa_bsg_fcpim_profile_s {
	bfa_status_t    status;
	u16		bfad_num;
	u16		rsvd;
};

struct bfa_bsg_itnim_ioprofile_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		lpwwn;
	wwn_t		rpwwn;
	struct bfa_itnim_ioprofile_s ioprofile;
};

struct bfa_bsg_fcport_stats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	union bfa_fcport_stats_u stats;
};

struct bfa_bsg_ioc_name_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	char		name[BFA_ADAPTER_SYM_NAME_LEN];
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

struct bfa_bsg_ioc_stats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_ioc_stats_s ioc_stats;
};

struct bfa_bsg_ioc_fwstats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	u32		buf_size;
	u32		rsvd1;
	u64		buf_ptr;
};

struct bfa_bsg_iocfc_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_iocfc_attr_s	iocfc_attr;
};

struct bfa_bsg_iocfc_intr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_iocfc_intr_attr_s attr;
};

struct bfa_bsg_port_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_port_attr_s	attr;
};

struct bfa_bsg_port_cfg_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	u32		param;
	u32		rsvd1;
};

struct bfa_bsg_port_cfg_maxfrsize_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		maxfrsize;
};

struct bfa_bsg_port_stats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	u32		buf_size;
	u32		rsvd1;
	u64		buf_ptr;
};

struct bfa_bsg_lport_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		pwwn;
	struct bfa_lport_attr_s port_attr;
};

struct bfa_bsg_lport_stats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		pwwn;
	struct bfa_lport_stats_s port_stats;
};

struct bfa_bsg_lport_iostats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		pwwn;
	struct bfa_itnim_iostats_s iostats;
};

struct bfa_bsg_lport_get_rports_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		pwwn;
	u64		rbuf_ptr;
	u32		nrports;
	u32		rsvd;
};

struct bfa_bsg_rport_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		pwwn;
	wwn_t		rpwwn;
	struct bfa_rport_attr_s attr;
};

struct bfa_bsg_rport_stats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		pwwn;
	wwn_t		rpwwn;
	struct bfa_rport_stats_s stats;
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

struct bfa_bsg_rport_reset_stats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		pwwn;
	wwn_t		rpwwn;
};

struct bfa_bsg_rport_set_speed_s {
	bfa_status_t		status;
	u16			bfad_num;
	u16			vf_id;
	enum bfa_port_speed	speed;
	u32			rsvd;
	wwn_t			pwwn;
	wwn_t			rpwwn;
};

struct bfa_bsg_vport_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		vpwwn;
	struct bfa_vport_attr_s vport_attr;
};

struct bfa_bsg_vport_stats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		vpwwn;
	struct bfa_vport_stats_s vport_stats;
};

struct bfa_bsg_reset_stats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		vpwwn;
};

struct bfa_bsg_fabric_get_lports_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	u64		buf_ptr;
	u32		nports;
	u32		rsvd;
};

struct bfa_bsg_trl_speed_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	enum bfa_port_speed speed;
};

struct bfa_bsg_fcpim_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		param;
};

struct bfa_bsg_fcpim_modstats_s {
	bfa_status_t	status;
	u16		bfad_num;
	struct bfa_itnim_iostats_s modstats;
};

struct bfa_bsg_fcpim_del_itn_stats_s {
	bfa_status_t	status;
	u16		bfad_num;
	struct bfa_fcpim_del_itn_stats_s modstats;
};

struct bfa_bsg_fcpim_modstatsclr_s {
	bfa_status_t	status;
	u16		bfad_num;
};

struct bfa_bsg_itnim_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		lpwwn;
	wwn_t		rpwwn;
	struct bfa_itnim_attr_s	attr;
};

struct bfa_bsg_itnim_iostats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		lpwwn;
	wwn_t		rpwwn;
	struct bfa_itnim_iostats_s iostats;
};

struct bfa_bsg_itnim_itnstats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	wwn_t		lpwwn;
	wwn_t		rpwwn;
	struct bfa_itnim_stats_s itnstats;
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

struct bfa_bsg_faa_attr_s {
	bfa_status_t		status;
	u16			bfad_num;
	u16			rsvd;
	struct bfa_faa_attr_s	faa_attr;
};

struct bfa_bsg_cee_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	u32		buf_size;
	u32		rsvd1;
	u64		buf_ptr;
};

struct bfa_bsg_cee_stats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	u32		buf_size;
	u32		rsvd1;
	u64		buf_ptr;
};

struct bfa_bsg_sfp_media_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	enum bfa_defs_sfp_media_e media;
};

struct bfa_bsg_sfp_speed_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	enum bfa_port_speed speed;
};

struct bfa_bsg_flash_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_flash_attr_s attr;
};

struct bfa_bsg_flash_s {
	bfa_status_t	status;
	u16		bfad_num;
	u8		instance;
	u8		rsvd;
	enum  bfa_flash_part_type type;
	int		bufsz;
	u64		buf_ptr;
};

struct bfa_bsg_diag_get_temp_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_diag_results_tempsensor_s result;
};

struct bfa_bsg_diag_memtest_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd[3];
	u32		pat;
	struct bfa_diag_memtest_result result;
	struct bfa_diag_memtest_s memtest;
};

struct bfa_bsg_diag_loopback_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	enum bfa_port_opmode opmode;
	enum bfa_port_speed speed;
	u32		lpcnt;
	u32		pat;
	struct bfa_diag_loopback_result_s result;
};

struct bfa_bsg_diag_fwping_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	u32		cnt;
	u32		pattern;
	struct bfa_diag_results_fwping result;
};

struct bfa_bsg_diag_qtest_s {
	bfa_status_t	status;
	u16	bfad_num;
	u16	rsvd;
	u32	force;
	u32	queue;
	struct bfa_diag_qtest_result_s result;
};

struct bfa_bsg_sfp_show_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct sfp_mem_s sfp;
};

struct bfa_bsg_diag_led_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_diag_ledtest_s ledtest;
};

struct bfa_bsg_diag_beacon_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	bfa_boolean_t   beacon;
	bfa_boolean_t   link_e2e_beacon;
	u32		second;
};

struct bfa_bsg_diag_lb_stat_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
};

struct bfa_bsg_phy_attr_s {
	bfa_status_t	status;
	u16	bfad_num;
	u16	instance;
	struct bfa_phy_attr_s	attr;
};

struct bfa_bsg_phy_s {
	bfa_status_t	status;
	u16	bfad_num;
	u16	instance;
	u64	bufsz;
	u64	buf_ptr;
};

struct bfa_bsg_debug_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	u32		bufsz;
	int		inst_no;
	u64		buf_ptr;
	u64		offset;
};

struct bfa_bsg_phy_stats_s {
	bfa_status_t	status;
	u16	bfad_num;
	u16	instance;
	struct bfa_phy_stats_s	stats;
};

struct bfa_bsg_vhba_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		pcifn_id;
	struct bfa_vhba_attr_s	attr;
};

struct bfa_bsg_boot_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_boot_cfg_s	cfg;
};

struct bfa_bsg_preboot_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_boot_pbc_s	cfg;
};

struct bfa_bsg_ethboot_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct  bfa_ethboot_cfg_s  cfg;
};

struct bfa_bsg_trunk_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_trunk_attr_s attr;
};

struct bfa_bsg_qos_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_qos_attr_s	attr;
};

struct bfa_bsg_qos_vc_attr_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		rsvd;
	struct bfa_qos_vc_attr_s attr;
};

struct bfa_bsg_vf_stats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
	struct bfa_vf_stats_s	stats;
};

struct bfa_bsg_vf_reset_stats_s {
	bfa_status_t	status;
	u16		bfad_num;
	u16		vf_id;
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
