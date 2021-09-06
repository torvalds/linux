/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2013 - 2021 Intel Corporation. */

#ifndef _I40E_ADMINQ_CMD_H_
#define _I40E_ADMINQ_CMD_H_

/* This header file defines the i40e Admin Queue commands and is shared between
 * i40e Firmware and Software.
 *
 * This file needs to comply with the Linux Kernel coding style.
 */

#define I40E_FW_API_VERSION_MAJOR	0x0001
#define I40E_FW_API_VERSION_MINOR_X722	0x0009
#define I40E_FW_API_VERSION_MINOR_X710	0x0009

#define I40E_FW_MINOR_VERSION(_h) ((_h)->mac.type == I40E_MAC_XL710 ? \
					I40E_FW_API_VERSION_MINOR_X710 : \
					I40E_FW_API_VERSION_MINOR_X722)

/* API version 1.7 implements additional link and PHY-specific APIs  */
#define I40E_MINOR_VER_GET_LINK_INFO_XL710 0x0007
/* API version 1.9 for X722 implements additional link and PHY-specific APIs */
#define I40E_MINOR_VER_GET_LINK_INFO_X722 0x0009
/* API version 1.6 for X722 devices adds ability to stop FW LLDP agent */
#define I40E_MINOR_VER_FW_LLDP_STOPPABLE_X722 0x0006
/* API version 1.10 for X722 devices adds ability to request FEC encoding */
#define I40E_MINOR_VER_FW_REQUEST_FEC_X722 0x000A

struct i40e_aq_desc {
	__le16 flags;
	__le16 opcode;
	__le16 datalen;
	__le16 retval;
	__le32 cookie_high;
	__le32 cookie_low;
	union {
		struct {
			__le32 param0;
			__le32 param1;
			__le32 param2;
			__le32 param3;
		} internal;
		struct {
			__le32 param0;
			__le32 param1;
			__le32 addr_high;
			__le32 addr_low;
		} external;
		u8 raw[16];
	} params;
};

/* Flags sub-structure
 * |0  |1  |2  |3  |4  |5  |6  |7  |8  |9  |10 |11 |12 |13 |14 |15 |
 * |DD |CMP|ERR|VFE| * *  RESERVED * * |LB |RD |VFC|BUF|SI |EI |FE |
 */

/* command flags and offsets*/
#define I40E_AQ_FLAG_ERR_SHIFT	2
#define I40E_AQ_FLAG_LB_SHIFT	9
#define I40E_AQ_FLAG_RD_SHIFT	10
#define I40E_AQ_FLAG_BUF_SHIFT	12
#define I40E_AQ_FLAG_SI_SHIFT	13

#define I40E_AQ_FLAG_ERR	BIT(I40E_AQ_FLAG_ERR_SHIFT) /* 0x4    */
#define I40E_AQ_FLAG_LB		BIT(I40E_AQ_FLAG_LB_SHIFT)  /* 0x200  */
#define I40E_AQ_FLAG_RD		BIT(I40E_AQ_FLAG_RD_SHIFT)  /* 0x400  */
#define I40E_AQ_FLAG_BUF	BIT(I40E_AQ_FLAG_BUF_SHIFT) /* 0x1000 */
#define I40E_AQ_FLAG_SI		BIT(I40E_AQ_FLAG_SI_SHIFT)  /* 0x2000 */

/* error codes */
enum i40e_admin_queue_err {
	I40E_AQ_RC_OK		= 0,  /* success */
	I40E_AQ_RC_EPERM	= 1,  /* Operation not permitted */
	I40E_AQ_RC_ENOENT	= 2,  /* No such element */
	I40E_AQ_RC_ESRCH	= 3,  /* Bad opcode */
	I40E_AQ_RC_EINTR	= 4,  /* operation interrupted */
	I40E_AQ_RC_EIO		= 5,  /* I/O error */
	I40E_AQ_RC_ENXIO	= 6,  /* No such resource */
	I40E_AQ_RC_E2BIG	= 7,  /* Arg too long */
	I40E_AQ_RC_EAGAIN	= 8,  /* Try again */
	I40E_AQ_RC_ENOMEM	= 9,  /* Out of memory */
	I40E_AQ_RC_EACCES	= 10, /* Permission denied */
	I40E_AQ_RC_EFAULT	= 11, /* Bad address */
	I40E_AQ_RC_EBUSY	= 12, /* Device or resource busy */
	I40E_AQ_RC_EEXIST	= 13, /* object already exists */
	I40E_AQ_RC_EINVAL	= 14, /* Invalid argument */
	I40E_AQ_RC_ENOTTY	= 15, /* Not a typewriter */
	I40E_AQ_RC_ENOSPC	= 16, /* No space left or alloc failure */
	I40E_AQ_RC_ENOSYS	= 17, /* Function not implemented */
	I40E_AQ_RC_ERANGE	= 18, /* Parameter out of range */
	I40E_AQ_RC_EFLUSHED	= 19, /* Cmd flushed due to prev cmd error */
	I40E_AQ_RC_BAD_ADDR	= 20, /* Descriptor contains a bad pointer */
	I40E_AQ_RC_EMODE	= 21, /* Op not allowed in current dev mode */
	I40E_AQ_RC_EFBIG	= 22, /* File too large */
};

/* Admin Queue command opcodes */
enum i40e_admin_queue_opc {
	/* aq commands */
	i40e_aqc_opc_get_version	= 0x0001,
	i40e_aqc_opc_driver_version	= 0x0002,
	i40e_aqc_opc_queue_shutdown	= 0x0003,
	i40e_aqc_opc_set_pf_context	= 0x0004,

	/* resource ownership */
	i40e_aqc_opc_request_resource	= 0x0008,
	i40e_aqc_opc_release_resource	= 0x0009,

	i40e_aqc_opc_list_func_capabilities	= 0x000A,
	i40e_aqc_opc_list_dev_capabilities	= 0x000B,

	/* Proxy commands */
	i40e_aqc_opc_set_proxy_config		= 0x0104,
	i40e_aqc_opc_set_ns_proxy_table_entry	= 0x0105,

	/* LAA */
	i40e_aqc_opc_mac_address_read	= 0x0107,
	i40e_aqc_opc_mac_address_write	= 0x0108,

	/* PXE */
	i40e_aqc_opc_clear_pxe_mode	= 0x0110,

	/* WoL commands */
	i40e_aqc_opc_set_wol_filter	= 0x0120,
	i40e_aqc_opc_get_wake_reason	= 0x0121,

	/* internal switch commands */
	i40e_aqc_opc_get_switch_config		= 0x0200,
	i40e_aqc_opc_add_statistics		= 0x0201,
	i40e_aqc_opc_remove_statistics		= 0x0202,
	i40e_aqc_opc_set_port_parameters	= 0x0203,
	i40e_aqc_opc_get_switch_resource_alloc	= 0x0204,
	i40e_aqc_opc_set_switch_config		= 0x0205,
	i40e_aqc_opc_rx_ctl_reg_read		= 0x0206,
	i40e_aqc_opc_rx_ctl_reg_write		= 0x0207,

	i40e_aqc_opc_add_vsi			= 0x0210,
	i40e_aqc_opc_update_vsi_parameters	= 0x0211,
	i40e_aqc_opc_get_vsi_parameters		= 0x0212,

	i40e_aqc_opc_add_pv			= 0x0220,
	i40e_aqc_opc_update_pv_parameters	= 0x0221,
	i40e_aqc_opc_get_pv_parameters		= 0x0222,

	i40e_aqc_opc_add_veb			= 0x0230,
	i40e_aqc_opc_update_veb_parameters	= 0x0231,
	i40e_aqc_opc_get_veb_parameters		= 0x0232,

	i40e_aqc_opc_delete_element		= 0x0243,

	i40e_aqc_opc_add_macvlan		= 0x0250,
	i40e_aqc_opc_remove_macvlan		= 0x0251,
	i40e_aqc_opc_add_vlan			= 0x0252,
	i40e_aqc_opc_remove_vlan		= 0x0253,
	i40e_aqc_opc_set_vsi_promiscuous_modes	= 0x0254,
	i40e_aqc_opc_add_tag			= 0x0255,
	i40e_aqc_opc_remove_tag			= 0x0256,
	i40e_aqc_opc_add_multicast_etag		= 0x0257,
	i40e_aqc_opc_remove_multicast_etag	= 0x0258,
	i40e_aqc_opc_update_tag			= 0x0259,
	i40e_aqc_opc_add_control_packet_filter	= 0x025A,
	i40e_aqc_opc_remove_control_packet_filter	= 0x025B,
	i40e_aqc_opc_add_cloud_filters		= 0x025C,
	i40e_aqc_opc_remove_cloud_filters	= 0x025D,
	i40e_aqc_opc_clear_wol_switch_filters	= 0x025E,

	i40e_aqc_opc_add_mirror_rule	= 0x0260,
	i40e_aqc_opc_delete_mirror_rule	= 0x0261,

	/* Dynamic Device Personalization */
	i40e_aqc_opc_write_personalization_profile	= 0x0270,
	i40e_aqc_opc_get_personalization_profile_list	= 0x0271,

	/* DCB commands */
	i40e_aqc_opc_dcb_ignore_pfc	= 0x0301,
	i40e_aqc_opc_dcb_updated	= 0x0302,
	i40e_aqc_opc_set_dcb_parameters = 0x0303,

	/* TX scheduler */
	i40e_aqc_opc_configure_vsi_bw_limit		= 0x0400,
	i40e_aqc_opc_configure_vsi_ets_sla_bw_limit	= 0x0406,
	i40e_aqc_opc_configure_vsi_tc_bw		= 0x0407,
	i40e_aqc_opc_query_vsi_bw_config		= 0x0408,
	i40e_aqc_opc_query_vsi_ets_sla_config		= 0x040A,
	i40e_aqc_opc_configure_switching_comp_bw_limit	= 0x0410,

	i40e_aqc_opc_enable_switching_comp_ets			= 0x0413,
	i40e_aqc_opc_modify_switching_comp_ets			= 0x0414,
	i40e_aqc_opc_disable_switching_comp_ets			= 0x0415,
	i40e_aqc_opc_configure_switching_comp_ets_bw_limit	= 0x0416,
	i40e_aqc_opc_configure_switching_comp_bw_config		= 0x0417,
	i40e_aqc_opc_query_switching_comp_ets_config		= 0x0418,
	i40e_aqc_opc_query_port_ets_config			= 0x0419,
	i40e_aqc_opc_query_switching_comp_bw_config		= 0x041A,
	i40e_aqc_opc_suspend_port_tx				= 0x041B,
	i40e_aqc_opc_resume_port_tx				= 0x041C,
	i40e_aqc_opc_configure_partition_bw			= 0x041D,
	/* hmc */
	i40e_aqc_opc_query_hmc_resource_profile	= 0x0500,
	i40e_aqc_opc_set_hmc_resource_profile	= 0x0501,

	/* phy commands*/
	i40e_aqc_opc_get_phy_abilities		= 0x0600,
	i40e_aqc_opc_set_phy_config		= 0x0601,
	i40e_aqc_opc_set_mac_config		= 0x0603,
	i40e_aqc_opc_set_link_restart_an	= 0x0605,
	i40e_aqc_opc_get_link_status		= 0x0607,
	i40e_aqc_opc_set_phy_int_mask		= 0x0613,
	i40e_aqc_opc_get_local_advt_reg		= 0x0614,
	i40e_aqc_opc_set_local_advt_reg		= 0x0615,
	i40e_aqc_opc_get_partner_advt		= 0x0616,
	i40e_aqc_opc_set_lb_modes		= 0x0618,
	i40e_aqc_opc_get_phy_wol_caps		= 0x0621,
	i40e_aqc_opc_set_phy_debug		= 0x0622,
	i40e_aqc_opc_upload_ext_phy_fm		= 0x0625,
	i40e_aqc_opc_run_phy_activity		= 0x0626,
	i40e_aqc_opc_set_phy_register		= 0x0628,
	i40e_aqc_opc_get_phy_register		= 0x0629,

	/* NVM commands */
	i40e_aqc_opc_nvm_read			= 0x0701,
	i40e_aqc_opc_nvm_erase			= 0x0702,
	i40e_aqc_opc_nvm_update			= 0x0703,
	i40e_aqc_opc_nvm_config_read		= 0x0704,
	i40e_aqc_opc_nvm_config_write		= 0x0705,
	i40e_aqc_opc_oem_post_update		= 0x0720,
	i40e_aqc_opc_thermal_sensor		= 0x0721,

	/* virtualization commands */
	i40e_aqc_opc_send_msg_to_pf		= 0x0801,
	i40e_aqc_opc_send_msg_to_vf		= 0x0802,
	i40e_aqc_opc_send_msg_to_peer		= 0x0803,

	/* alternate structure */
	i40e_aqc_opc_alternate_write		= 0x0900,
	i40e_aqc_opc_alternate_write_indirect	= 0x0901,
	i40e_aqc_opc_alternate_read		= 0x0902,
	i40e_aqc_opc_alternate_read_indirect	= 0x0903,
	i40e_aqc_opc_alternate_write_done	= 0x0904,
	i40e_aqc_opc_alternate_set_mode		= 0x0905,
	i40e_aqc_opc_alternate_clear_port	= 0x0906,

	/* LLDP commands */
	i40e_aqc_opc_lldp_get_mib	= 0x0A00,
	i40e_aqc_opc_lldp_update_mib	= 0x0A01,
	i40e_aqc_opc_lldp_add_tlv	= 0x0A02,
	i40e_aqc_opc_lldp_update_tlv	= 0x0A03,
	i40e_aqc_opc_lldp_delete_tlv	= 0x0A04,
	i40e_aqc_opc_lldp_stop		= 0x0A05,
	i40e_aqc_opc_lldp_start		= 0x0A06,
	i40e_aqc_opc_get_cee_dcb_cfg	= 0x0A07,
	i40e_aqc_opc_lldp_set_local_mib	= 0x0A08,
	i40e_aqc_opc_lldp_stop_start_spec_agent	= 0x0A09,
	i40e_aqc_opc_lldp_restore		= 0x0A0A,

	/* Tunnel commands */
	i40e_aqc_opc_add_udp_tunnel	= 0x0B00,
	i40e_aqc_opc_del_udp_tunnel	= 0x0B01,
	i40e_aqc_opc_set_rss_key	= 0x0B02,
	i40e_aqc_opc_set_rss_lut	= 0x0B03,
	i40e_aqc_opc_get_rss_key	= 0x0B04,
	i40e_aqc_opc_get_rss_lut	= 0x0B05,

	/* Async Events */
	i40e_aqc_opc_event_lan_overflow		= 0x1001,

	/* OEM commands */
	i40e_aqc_opc_oem_parameter_change	= 0xFE00,
	i40e_aqc_opc_oem_device_status_change	= 0xFE01,
	i40e_aqc_opc_oem_ocsd_initialize	= 0xFE02,
	i40e_aqc_opc_oem_ocbb_initialize	= 0xFE03,

	/* debug commands */
	i40e_aqc_opc_debug_read_reg		= 0xFF03,
	i40e_aqc_opc_debug_write_reg		= 0xFF04,
	i40e_aqc_opc_debug_modify_reg		= 0xFF07,
	i40e_aqc_opc_debug_dump_internals	= 0xFF08,
};

/* command structures and indirect data structures */

/* Structure naming conventions:
 * - no suffix for direct command descriptor structures
 * - _data for indirect sent data
 * - _resp for indirect return data (data which is both will use _data)
 * - _completion for direct return data
 * - _element_ for repeated elements (may also be _data or _resp)
 *
 * Command structures are expected to overlay the params.raw member of the basic
 * descriptor, and as such cannot exceed 16 bytes in length.
 */

/* This macro is used to generate a compilation error if a structure
 * is not exactly the correct length. It gives a divide by zero error if the
 * structure is not of the correct size, otherwise it creates an enum that is
 * never used.
 */
#define I40E_CHECK_STRUCT_LEN(n, X) enum i40e_static_assert_enum_##X \
	{ i40e_static_assert_##X = (n)/((sizeof(struct X) == (n)) ? 1 : 0) }

/* This macro is used extensively to ensure that command structures are 16
 * bytes in length as they have to map to the raw array of that size.
 */
#define I40E_CHECK_CMD_LENGTH(X)	I40E_CHECK_STRUCT_LEN(16, X)

/* internal (0x00XX) commands */

/* Get version (direct 0x0001) */
struct i40e_aqc_get_version {
	__le32 rom_ver;
	__le32 fw_build;
	__le16 fw_major;
	__le16 fw_minor;
	__le16 api_major;
	__le16 api_minor;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_get_version);

/* Send driver version (indirect 0x0002) */
struct i40e_aqc_driver_version {
	u8	driver_major_ver;
	u8	driver_minor_ver;
	u8	driver_build_ver;
	u8	driver_subbuild_ver;
	u8	reserved[4];
	__le32	address_high;
	__le32	address_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_driver_version);

/* Queue Shutdown (direct 0x0003) */
struct i40e_aqc_queue_shutdown {
	__le32	driver_unloading;
#define I40E_AQ_DRIVER_UNLOADING	0x1
	u8	reserved[12];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_queue_shutdown);

/* Set PF context (0x0004, direct) */
struct i40e_aqc_set_pf_context {
	u8	pf_id;
	u8	reserved[15];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_set_pf_context);

/* Request resource ownership (direct 0x0008)
 * Release resource ownership (direct 0x0009)
 */
struct i40e_aqc_request_resource {
	__le16	resource_id;
	__le16	access_type;
	__le32	timeout;
	__le32	resource_number;
	u8	reserved[4];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_request_resource);

/* Get function capabilities (indirect 0x000A)
 * Get device capabilities (indirect 0x000B)
 */
struct i40e_aqc_list_capabilites {
	u8 command_flags;
	u8 pf_index;
	u8 reserved[2];
	__le32 count;
	__le32 addr_high;
	__le32 addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_list_capabilites);

struct i40e_aqc_list_capabilities_element_resp {
	__le16	id;
	u8	major_rev;
	u8	minor_rev;
	__le32	number;
	__le32	logical_id;
	__le32	phys_id;
	u8	reserved[16];
};

/* list of caps */

#define I40E_AQ_CAP_ID_SWITCH_MODE	0x0001
#define I40E_AQ_CAP_ID_MNG_MODE		0x0002
#define I40E_AQ_CAP_ID_NPAR_ACTIVE	0x0003
#define I40E_AQ_CAP_ID_OS2BMC_CAP	0x0004
#define I40E_AQ_CAP_ID_FUNCTIONS_VALID	0x0005
#define I40E_AQ_CAP_ID_SRIOV		0x0012
#define I40E_AQ_CAP_ID_VF		0x0013
#define I40E_AQ_CAP_ID_VMDQ		0x0014
#define I40E_AQ_CAP_ID_8021QBG		0x0015
#define I40E_AQ_CAP_ID_8021QBR		0x0016
#define I40E_AQ_CAP_ID_VSI		0x0017
#define I40E_AQ_CAP_ID_DCB		0x0018
#define I40E_AQ_CAP_ID_FCOE		0x0021
#define I40E_AQ_CAP_ID_ISCSI		0x0022
#define I40E_AQ_CAP_ID_RSS		0x0040
#define I40E_AQ_CAP_ID_RXQ		0x0041
#define I40E_AQ_CAP_ID_TXQ		0x0042
#define I40E_AQ_CAP_ID_MSIX		0x0043
#define I40E_AQ_CAP_ID_VF_MSIX		0x0044
#define I40E_AQ_CAP_ID_FLOW_DIRECTOR	0x0045
#define I40E_AQ_CAP_ID_1588		0x0046
#define I40E_AQ_CAP_ID_IWARP		0x0051
#define I40E_AQ_CAP_ID_LED		0x0061
#define I40E_AQ_CAP_ID_SDP		0x0062
#define I40E_AQ_CAP_ID_MDIO		0x0063
#define I40E_AQ_CAP_ID_WSR_PROT		0x0064
#define I40E_AQ_CAP_ID_NVM_MGMT		0x0080
#define I40E_AQ_CAP_ID_FLEX10		0x00F1
#define I40E_AQ_CAP_ID_CEM		0x00F2

/* Set CPPM Configuration (direct 0x0103) */
struct i40e_aqc_cppm_configuration {
	__le16	command_flags;
	__le16	ttlx;
	__le32	dmacr;
	__le16	dmcth;
	u8	hptc;
	u8	reserved;
	__le32	pfltrc;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_cppm_configuration);

/* Set ARP Proxy command / response (indirect 0x0104) */
struct i40e_aqc_arp_proxy_data {
	__le16	command_flags;
	__le16	table_id;
	__le32	enabled_offloads;
	__le32	ip_addr;
	u8	mac_addr[6];
	u8	reserved[2];
};

I40E_CHECK_STRUCT_LEN(0x14, i40e_aqc_arp_proxy_data);

/* Set NS Proxy Table Entry Command (indirect 0x0105) */
struct i40e_aqc_ns_proxy_data {
	__le16	table_idx_mac_addr_0;
	__le16	table_idx_mac_addr_1;
	__le16	table_idx_ipv6_0;
	__le16	table_idx_ipv6_1;
	__le16	control;
	u8	mac_addr_0[6];
	u8	mac_addr_1[6];
	u8	local_mac_addr[6];
	u8	ipv6_addr_0[16]; /* Warning! spec specifies BE byte order */
	u8	ipv6_addr_1[16];
};

I40E_CHECK_STRUCT_LEN(0x3c, i40e_aqc_ns_proxy_data);

/* Manage LAA Command (0x0106) - obsolete */
struct i40e_aqc_mng_laa {
	__le16	command_flags;
	u8	reserved[2];
	__le32	sal;
	__le16	sah;
	u8	reserved2[6];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_mng_laa);

/* Manage MAC Address Read Command (indirect 0x0107) */
struct i40e_aqc_mac_address_read {
	__le16	command_flags;
#define I40E_AQC_LAN_ADDR_VALID		0x10
#define I40E_AQC_PORT_ADDR_VALID	0x40
	u8	reserved[6];
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_mac_address_read);

struct i40e_aqc_mac_address_read_data {
	u8 pf_lan_mac[6];
	u8 pf_san_mac[6];
	u8 port_mac[6];
	u8 pf_wol_mac[6];
};

I40E_CHECK_STRUCT_LEN(24, i40e_aqc_mac_address_read_data);

/* Manage MAC Address Write Command (0x0108) */
struct i40e_aqc_mac_address_write {
	__le16	command_flags;
#define I40E_AQC_MC_MAG_EN		0x0100
#define I40E_AQC_WOL_PRESERVE_ON_PFR	0x0200
#define I40E_AQC_WRITE_TYPE_LAA_ONLY	0x0000
#define I40E_AQC_WRITE_TYPE_LAA_WOL	0x4000
#define I40E_AQC_WRITE_TYPE_UPDATE_MC_MAG	0xC000

	__le16	mac_sah;
	__le32	mac_sal;
	u8	reserved[8];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_mac_address_write);

/* PXE commands (0x011x) */

/* Clear PXE Command and response  (direct 0x0110) */
struct i40e_aqc_clear_pxe {
	u8	rx_cnt;
	u8	reserved[15];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_clear_pxe);

/* Set WoL Filter (0x0120) */

struct i40e_aqc_set_wol_filter {
	__le16 filter_index;

	__le16 cmd_flags;
	__le16 valid_flags;
	u8 reserved[2];
	__le32	address_high;
	__le32	address_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_set_wol_filter);

struct i40e_aqc_set_wol_filter_data {
	u8 filter[128];
	u8 mask[16];
};

I40E_CHECK_STRUCT_LEN(0x90, i40e_aqc_set_wol_filter_data);

/* Get Wake Reason (0x0121) */

struct i40e_aqc_get_wake_reason_completion {
	u8 reserved_1[2];
	__le16 wake_reason;
	u8 reserved_2[12];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_get_wake_reason_completion);

/* Switch configuration commands (0x02xx) */

/* Used by many indirect commands that only pass an seid and a buffer in the
 * command
 */
struct i40e_aqc_switch_seid {
	__le16	seid;
	u8	reserved[6];
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_switch_seid);

/* Get Switch Configuration command (indirect 0x0200)
 * uses i40e_aqc_switch_seid for the descriptor
 */
struct i40e_aqc_get_switch_config_header_resp {
	__le16	num_reported;
	__le16	num_total;
	u8	reserved[12];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_get_switch_config_header_resp);

struct i40e_aqc_switch_config_element_resp {
	u8	element_type;
	u8	revision;
	__le16	seid;
	__le16	uplink_seid;
	__le16	downlink_seid;
	u8	reserved[3];
	u8	connection_type;
	__le16	scheduler_id;
	__le16	element_info;
};

I40E_CHECK_STRUCT_LEN(0x10, i40e_aqc_switch_config_element_resp);

/* Get Switch Configuration (indirect 0x0200)
 *    an array of elements are returned in the response buffer
 *    the first in the array is the header, remainder are elements
 */
struct i40e_aqc_get_switch_config_resp {
	struct i40e_aqc_get_switch_config_header_resp	header;
	struct i40e_aqc_switch_config_element_resp	element[1];
};

I40E_CHECK_STRUCT_LEN(0x20, i40e_aqc_get_switch_config_resp);

/* Add Statistics (direct 0x0201)
 * Remove Statistics (direct 0x0202)
 */
struct i40e_aqc_add_remove_statistics {
	__le16	seid;
	__le16	vlan;
	__le16	stat_index;
	u8	reserved[10];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_remove_statistics);

/* Set Port Parameters command (direct 0x0203) */
struct i40e_aqc_set_port_parameters {
	__le16	command_flags;
	__le16	bad_frame_vsi;
	__le16	default_seid;        /* reserved for command */
	u8	reserved[10];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_set_port_parameters);

/* Get Switch Resource Allocation (indirect 0x0204) */
struct i40e_aqc_get_switch_resource_alloc {
	u8	num_entries;         /* reserved for command */
	u8	reserved[7];
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_get_switch_resource_alloc);

/* expect an array of these structs in the response buffer */
struct i40e_aqc_switch_resource_alloc_element_resp {
	u8	resource_type;
	u8	reserved1;
	__le16	guaranteed;
	__le16	total;
	__le16	used;
	__le16	total_unalloced;
	u8	reserved2[6];
};

I40E_CHECK_STRUCT_LEN(0x10, i40e_aqc_switch_resource_alloc_element_resp);

/* Set Switch Configuration (direct 0x0205) */
struct i40e_aqc_set_switch_config {
	__le16	flags;
/* flags used for both fields below */
#define I40E_AQ_SET_SWITCH_CFG_PROMISC		0x0001
	__le16	valid_flags;
	/* The ethertype in switch_tag is dropped on ingress and used
	 * internally by the switch. Set this to zero for the default
	 * of 0x88a8 (802.1ad). Should be zero for firmware API
	 * versions lower than 1.7.
	 */
	__le16	switch_tag;
	/* The ethertypes in first_tag and second_tag are used to
	 * match the outer and inner VLAN tags (respectively) when HW
	 * double VLAN tagging is enabled via the set port parameters
	 * AQ command. Otherwise these are both ignored. Set them to
	 * zero for their defaults of 0x8100 (802.1Q). Should be zero
	 * for firmware API versions lower than 1.7.
	 */
	__le16	first_tag;
	__le16	second_tag;
	/* Next byte is split into following:
	 * Bit 7    : 0 : No action, 1: Switch to mode defined by bits 6:0
	 * Bit 6    : 0 : Destination Port, 1: source port
	 * Bit 5..4 : L4 type
	 * 0: rsvd
	 * 1: TCP
	 * 2: UDP
	 * 3: Both TCP and UDP
	 * Bits 3:0 Mode
	 * 0: default mode
	 * 1: L4 port only mode
	 * 2: non-tunneled mode
	 * 3: tunneled mode
	 */
#define I40E_AQ_SET_SWITCH_BIT7_VALID		0x80


#define I40E_AQ_SET_SWITCH_L4_TYPE_TCP		0x10

#define I40E_AQ_SET_SWITCH_MODE_NON_TUNNEL	0x02
	u8	mode;
	u8	rsvd5[5];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_set_switch_config);

/* Read Receive control registers  (direct 0x0206)
 * Write Receive control registers (direct 0x0207)
 *     used for accessing Rx control registers that can be
 *     slow and need special handling when under high Rx load
 */
struct i40e_aqc_rx_ctl_reg_read_write {
	__le32 reserved1;
	__le32 address;
	__le32 reserved2;
	__le32 value;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_rx_ctl_reg_read_write);

/* Add VSI (indirect 0x0210)
 *    this indirect command uses struct i40e_aqc_vsi_properties_data
 *    as the indirect buffer (128 bytes)
 *
 * Update VSI (indirect 0x211)
 *     uses the same data structure as Add VSI
 *
 * Get VSI (indirect 0x0212)
 *     uses the same completion and data structure as Add VSI
 */
struct i40e_aqc_add_get_update_vsi {
	__le16	uplink_seid;
	u8	connection_type;
#define I40E_AQ_VSI_CONN_TYPE_NORMAL	0x1
	u8	reserved1;
	u8	vf_id;
	u8	reserved2;
	__le16	vsi_flags;
#define I40E_AQ_VSI_TYPE_VF		0x0
#define I40E_AQ_VSI_TYPE_VMDQ2		0x1
#define I40E_AQ_VSI_TYPE_PF		0x2
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_get_update_vsi);

struct i40e_aqc_add_get_update_vsi_completion {
	__le16 seid;
	__le16 vsi_number;
	__le16 vsi_used;
	__le16 vsi_free;
	__le32 addr_high;
	__le32 addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_get_update_vsi_completion);

struct i40e_aqc_vsi_properties_data {
	/* first 96 byte are written by SW */
	__le16	valid_sections;
#define I40E_AQ_VSI_PROP_SWITCH_VALID		0x0001
#define I40E_AQ_VSI_PROP_SECURITY_VALID		0x0002
#define I40E_AQ_VSI_PROP_VLAN_VALID		0x0004
#define I40E_AQ_VSI_PROP_QUEUE_MAP_VALID	0x0040
#define I40E_AQ_VSI_PROP_QUEUE_OPT_VALID	0x0080
#define I40E_AQ_VSI_PROP_SCHED_VALID		0x0200
	/* switch section */
	__le16	switch_id; /* 12bit id combined with flags below */
#define I40E_AQ_VSI_SW_ID_SHIFT		0x0000
#define I40E_AQ_VSI_SW_ID_MASK		(0xFFF << I40E_AQ_VSI_SW_ID_SHIFT)
#define I40E_AQ_VSI_SW_ID_FLAG_ALLOW_LB	0x2000
#define I40E_AQ_VSI_SW_ID_FLAG_LOCAL_LB	0x4000
	u8	sw_reserved[2];
	/* security section */
	u8	sec_flags;
#define I40E_AQ_VSI_SEC_FLAG_ENABLE_VLAN_CHK	0x02
#define I40E_AQ_VSI_SEC_FLAG_ENABLE_MAC_CHK	0x04
	u8	sec_reserved;
	/* VLAN section */
	__le16	pvid; /* VLANS include priority bits */
	__le16	fcoe_pvid;
	u8	port_vlan_flags;
#define I40E_AQ_VSI_PVLAN_MODE_SHIFT	0x00
#define I40E_AQ_VSI_PVLAN_MODE_MASK	(0x03 << \
					 I40E_AQ_VSI_PVLAN_MODE_SHIFT)
#define I40E_AQ_VSI_PVLAN_MODE_TAGGED	0x01
#define I40E_AQ_VSI_PVLAN_MODE_ALL	0x03
#define I40E_AQ_VSI_PVLAN_INSERT_PVID	0x04
#define I40E_AQ_VSI_PVLAN_EMOD_SHIFT	0x03
#define I40E_AQ_VSI_PVLAN_EMOD_MASK	(0x3 << \
					 I40E_AQ_VSI_PVLAN_EMOD_SHIFT)
#define I40E_AQ_VSI_PVLAN_EMOD_STR_BOTH	0x0
#define I40E_AQ_VSI_PVLAN_EMOD_STR	0x10
#define I40E_AQ_VSI_PVLAN_EMOD_NOTHING	0x18
	u8	pvlan_reserved[3];
	/* ingress egress up sections */
	__le32	ingress_table; /* bitmap, 3 bits per up */
	__le32	egress_table;   /* same defines as for ingress table */
	/* cascaded PV section */
	__le16	cas_pv_tag;
	u8	cas_pv_flags;
	u8	cas_pv_reserved;
	/* queue mapping section */
	__le16	mapping_flags;
#define I40E_AQ_VSI_QUE_MAP_CONTIG	0x0
#define I40E_AQ_VSI_QUE_MAP_NONCONTIG	0x1
	__le16	queue_mapping[16];
	__le16	tc_mapping[8];
#define I40E_AQ_VSI_TC_QUE_OFFSET_SHIFT	0
#define I40E_AQ_VSI_TC_QUE_NUMBER_SHIFT	9
	/* queueing option section */
	u8	queueing_opt_flags;
#define I40E_AQ_VSI_QUE_OPT_TCP_ENA	0x10
#define I40E_AQ_VSI_QUE_OPT_RSS_LUT_VSI	0x40
	u8	queueing_opt_reserved[3];
	/* scheduler section */
	u8	up_enable_bits;
	u8	sched_reserved;
	/* outer up section */
	__le32	outer_up_table; /* same structure and defines as ingress tbl */
	u8	cmd_reserved[8];
	/* last 32 bytes are written by FW */
	__le16	qs_handle[8];
#define I40E_AQ_VSI_QS_HANDLE_INVALID	0xFFFF
	__le16	stat_counter_idx;
	__le16	sched_id;
	u8	resp_reserved[12];
};

I40E_CHECK_STRUCT_LEN(128, i40e_aqc_vsi_properties_data);

/* Add Port Virtualizer (direct 0x0220)
 * also used for update PV (direct 0x0221) but only flags are used
 * (IS_CTRL_PORT only works on add PV)
 */
struct i40e_aqc_add_update_pv {
	__le16	command_flags;
	__le16	uplink_seid;
	__le16	connected_seid;
	u8	reserved[10];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_update_pv);

struct i40e_aqc_add_update_pv_completion {
	/* reserved for update; for add also encodes error if rc == ENOSPC */
	__le16	pv_seid;
	u8	reserved[14];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_update_pv_completion);

/* Get PV Params (direct 0x0222)
 * uses i40e_aqc_switch_seid for the descriptor
 */

struct i40e_aqc_get_pv_params_completion {
	__le16	seid;
	__le16	default_stag;
	__le16	pv_flags; /* same flags as add_pv */
	u8	reserved[8];
	__le16	default_port_seid;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_get_pv_params_completion);

/* Add VEB (direct 0x0230) */
struct i40e_aqc_add_veb {
	__le16	uplink_seid;
	__le16	downlink_seid;
	__le16	veb_flags;
#define I40E_AQC_ADD_VEB_FLOATING		0x1
#define I40E_AQC_ADD_VEB_PORT_TYPE_DEFAULT	0x2
#define I40E_AQC_ADD_VEB_PORT_TYPE_DATA		0x4
#define I40E_AQC_ADD_VEB_ENABLE_DISABLE_STATS	0x10
	u8	enable_tcs;
	u8	reserved[9];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_veb);

struct i40e_aqc_add_veb_completion {
	u8	reserved[6];
	__le16	switch_seid;
	/* also encodes error if rc == ENOSPC; codes are the same as add_pv */
	__le16	veb_seid;
	__le16	statistic_index;
	__le16	vebs_used;
	__le16	vebs_free;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_veb_completion);

/* Get VEB Parameters (direct 0x0232)
 * uses i40e_aqc_switch_seid for the descriptor
 */
struct i40e_aqc_get_veb_parameters_completion {
	__le16	seid;
	__le16	switch_id;
	__le16	veb_flags; /* only the first/last flags from 0x0230 is valid */
	__le16	statistic_index;
	__le16	vebs_used;
	__le16	vebs_free;
	u8	reserved[4];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_get_veb_parameters_completion);

/* Delete Element (direct 0x0243)
 * uses the generic i40e_aqc_switch_seid
 */

/* Add MAC-VLAN (indirect 0x0250) */

/* used for the command for most vlan commands */
struct i40e_aqc_macvlan {
	__le16	num_addresses;
	__le16	seid[3];
#define I40E_AQC_MACVLAN_CMD_SEID_VALID		0x8000
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_macvlan);

/* indirect data for command and response */
struct i40e_aqc_add_macvlan_element_data {
	u8	mac_addr[6];
	__le16	vlan_tag;
	__le16	flags;
#define I40E_AQC_MACVLAN_ADD_PERFECT_MATCH	0x0001
#define I40E_AQC_MACVLAN_ADD_IGNORE_VLAN	0x0004
#define I40E_AQC_MACVLAN_ADD_USE_SHARED_MAC	0x0010
	__le16	queue_number;
	/* response section */
	u8	match_method;
#define I40E_AQC_MM_ERR_NO_RES		0xFF
	u8	reserved1[3];
};

struct i40e_aqc_add_remove_macvlan_completion {
	__le16 perfect_mac_used;
	__le16 perfect_mac_free;
	__le16 unicast_hash_free;
	__le16 multicast_hash_free;
	__le32 addr_high;
	__le32 addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_remove_macvlan_completion);

/* Remove MAC-VLAN (indirect 0x0251)
 * uses i40e_aqc_macvlan for the descriptor
 * data points to an array of num_addresses of elements
 */

struct i40e_aqc_remove_macvlan_element_data {
	u8	mac_addr[6];
	__le16	vlan_tag;
	u8	flags;
#define I40E_AQC_MACVLAN_DEL_PERFECT_MATCH	0x01
#define I40E_AQC_MACVLAN_DEL_IGNORE_VLAN	0x08
	u8	reserved[3];
	/* reply section */
	u8	error_code;
	u8	reply_reserved[3];
};

/* Add VLAN (indirect 0x0252)
 * Remove VLAN (indirect 0x0253)
 * use the generic i40e_aqc_macvlan for the command
 */
struct i40e_aqc_add_remove_vlan_element_data {
	__le16	vlan_tag;
	u8	vlan_flags;
	u8	reserved;
	u8	result;
	u8	reserved1[3];
};

struct i40e_aqc_add_remove_vlan_completion {
	u8	reserved[4];
	__le16	vlans_used;
	__le16	vlans_free;
	__le32	addr_high;
	__le32	addr_low;
};

/* Set VSI Promiscuous Modes (direct 0x0254) */
struct i40e_aqc_set_vsi_promiscuous_modes {
	__le16	promiscuous_flags;
	__le16	valid_flags;
/* flags used for both fields above */
#define I40E_AQC_SET_VSI_PROMISC_UNICAST	0x01
#define I40E_AQC_SET_VSI_PROMISC_MULTICAST	0x02
#define I40E_AQC_SET_VSI_PROMISC_BROADCAST	0x04
#define I40E_AQC_SET_VSI_DEFAULT		0x08
#define I40E_AQC_SET_VSI_PROMISC_VLAN		0x10
#define I40E_AQC_SET_VSI_PROMISC_RX_ONLY	0x8000
	__le16	seid;
	__le16	vlan_tag;
#define I40E_AQC_SET_VSI_VLAN_VALID		0x8000
	u8	reserved[8];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_set_vsi_promiscuous_modes);

/* Add S/E-tag command (direct 0x0255)
 * Uses generic i40e_aqc_add_remove_tag_completion for completion
 */
struct i40e_aqc_add_tag {
	__le16	flags;
	__le16	seid;
	__le16	tag;
	__le16	queue_number;
	u8	reserved[8];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_tag);

struct i40e_aqc_add_remove_tag_completion {
	u8	reserved[12];
	__le16	tags_used;
	__le16	tags_free;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_remove_tag_completion);

/* Remove S/E-tag command (direct 0x0256)
 * Uses generic i40e_aqc_add_remove_tag_completion for completion
 */
struct i40e_aqc_remove_tag {
	__le16	seid;
	__le16	tag;
	u8	reserved[12];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_remove_tag);

/* Add multicast E-Tag (direct 0x0257)
 * del multicast E-Tag (direct 0x0258) only uses pv_seid and etag fields
 * and no external data
 */
struct i40e_aqc_add_remove_mcast_etag {
	__le16	pv_seid;
	__le16	etag;
	u8	num_unicast_etags;
	u8	reserved[3];
	__le32	addr_high;          /* address of array of 2-byte s-tags */
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_remove_mcast_etag);

struct i40e_aqc_add_remove_mcast_etag_completion {
	u8	reserved[4];
	__le16	mcast_etags_used;
	__le16	mcast_etags_free;
	__le32	addr_high;
	__le32	addr_low;

};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_remove_mcast_etag_completion);

/* Update S/E-Tag (direct 0x0259) */
struct i40e_aqc_update_tag {
	__le16	seid;
	__le16	old_tag;
	__le16	new_tag;
	u8	reserved[10];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_update_tag);

struct i40e_aqc_update_tag_completion {
	u8	reserved[12];
	__le16	tags_used;
	__le16	tags_free;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_update_tag_completion);

/* Add Control Packet filter (direct 0x025A)
 * Remove Control Packet filter (direct 0x025B)
 * uses the i40e_aqc_add_oveb_cloud,
 * and the generic direct completion structure
 */
struct i40e_aqc_add_remove_control_packet_filter {
	u8	mac[6];
	__le16	etype;
	__le16	flags;
#define I40E_AQC_ADD_CONTROL_PACKET_FLAGS_IGNORE_MAC	0x0001
#define I40E_AQC_ADD_CONTROL_PACKET_FLAGS_DROP		0x0002
#define I40E_AQC_ADD_CONTROL_PACKET_FLAGS_TX		0x0008
#define I40E_AQC_ADD_CONTROL_PACKET_FLAGS_RX		0x0000
	__le16	seid;
	__le16	queue;
	u8	reserved[2];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_remove_control_packet_filter);

struct i40e_aqc_add_remove_control_packet_filter_completion {
	__le16	mac_etype_used;
	__le16	etype_used;
	__le16	mac_etype_free;
	__le16	etype_free;
	u8	reserved[8];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_remove_control_packet_filter_completion);

/* Add Cloud filters (indirect 0x025C)
 * Remove Cloud filters (indirect 0x025D)
 * uses the i40e_aqc_add_remove_cloud_filters,
 * and the generic indirect completion structure
 */
struct i40e_aqc_add_remove_cloud_filters {
	u8	num_filters;
	u8	reserved;
	__le16	seid;
	u8	big_buffer_flag;
#define I40E_AQC_ADD_CLOUD_CMD_BB	1
	u8	reserved2[3];
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_remove_cloud_filters);

struct i40e_aqc_cloud_filters_element_data {
	u8	outer_mac[6];
	u8	inner_mac[6];
	__le16	inner_vlan;
	union {
		struct {
			u8 reserved[12];
			u8 data[4];
		} v4;
		struct {
			u8 data[16];
		} v6;
		struct {
			__le16 data[8];
		} raw_v6;
	} ipaddr;
	__le16	flags;
/* 0x0000 reserved */
/* 0x0001 reserved */
/* 0x0002 reserved */
#define I40E_AQC_ADD_CLOUD_FILTER_IMAC_IVLAN		0x0003
#define I40E_AQC_ADD_CLOUD_FILTER_IMAC_IVLAN_TEN_ID	0x0004
/* 0x0005 reserved */
#define I40E_AQC_ADD_CLOUD_FILTER_IMAC_TEN_ID		0x0006
/* 0x0007 reserved */
/* 0x0008 reserved */
#define I40E_AQC_ADD_CLOUD_FILTER_OMAC			0x0009
#define I40E_AQC_ADD_CLOUD_FILTER_IMAC			0x000A
#define I40E_AQC_ADD_CLOUD_FILTER_OMAC_TEN_ID_IMAC	0x000B
#define I40E_AQC_ADD_CLOUD_FILTER_IIP			0x000C
/* 0x000D reserved */
/* 0x000E reserved */
/* 0x000F reserved */
/* 0x0010 to 0x0017 is for custom filters */
#define I40E_AQC_ADD_CLOUD_FILTER_IP_PORT		0x0010 /* Dest IP + L4 Port */
#define I40E_AQC_ADD_CLOUD_FILTER_MAC_PORT		0x0011 /* Dest MAC + L4 Port */
#define I40E_AQC_ADD_CLOUD_FILTER_MAC_VLAN_PORT		0x0012 /* Dest MAC + VLAN + L4 Port */

#define I40E_AQC_ADD_CLOUD_FLAGS_IPV4			0
#define I40E_AQC_ADD_CLOUD_FLAGS_IPV6			0x0100

#define I40E_AQC_ADD_CLOUD_TNL_TYPE_SHIFT		9
#define I40E_AQC_ADD_CLOUD_TNL_TYPE_MASK		0x1E00
#define I40E_AQC_ADD_CLOUD_TNL_TYPE_GENEVE		2


	__le32	tenant_id;
	u8	reserved[4];
	__le16	queue_number;
	u8	reserved2[14];
	/* response section */
	u8	allocation_result;
	u8	response_reserved[7];
};

I40E_CHECK_STRUCT_LEN(0x40, i40e_aqc_cloud_filters_element_data);

/* i40e_aqc_cloud_filters_element_bb is used when
 * I40E_AQC_CLOUD_CMD_BB flag is set.
 */
struct i40e_aqc_cloud_filters_element_bb {
	struct i40e_aqc_cloud_filters_element_data element;
	u16     general_fields[32];
#define I40E_AQC_ADD_CLOUD_FV_FLU_0X16_WORD0	15
};

I40E_CHECK_STRUCT_LEN(0x80, i40e_aqc_cloud_filters_element_bb);

struct i40e_aqc_remove_cloud_filters_completion {
	__le16 perfect_ovlan_used;
	__le16 perfect_ovlan_free;
	__le16 vlan_used;
	__le16 vlan_free;
	__le32 addr_high;
	__le32 addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_remove_cloud_filters_completion);

/* Replace filter Command 0x025F
 * uses the i40e_aqc_replace_cloud_filters,
 * and the generic indirect completion structure
 */
struct i40e_filter_data {
	u8 filter_type;
	u8 input[3];
};

I40E_CHECK_STRUCT_LEN(4, i40e_filter_data);

struct i40e_aqc_replace_cloud_filters_cmd {
	u8      valid_flags;
	u8      old_filter_type;
	u8      new_filter_type;
	u8      tr_bit;
	u8      reserved[4];
	__le32 addr_high;
	__le32 addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_replace_cloud_filters_cmd);

struct i40e_aqc_replace_cloud_filters_cmd_buf {
	u8      data[32];
	struct i40e_filter_data filters[8];
};

I40E_CHECK_STRUCT_LEN(0x40, i40e_aqc_replace_cloud_filters_cmd_buf);

/* Add Mirror Rule (indirect or direct 0x0260)
 * Delete Mirror Rule (indirect or direct 0x0261)
 * note: some rule types (4,5) do not use an external buffer.
 *       take care to set the flags correctly.
 */
struct i40e_aqc_add_delete_mirror_rule {
	__le16 seid;
	__le16 rule_type;
#define I40E_AQC_MIRROR_RULE_TYPE_SHIFT		0
#define I40E_AQC_MIRROR_RULE_TYPE_MASK		(0x7 << \
						I40E_AQC_MIRROR_RULE_TYPE_SHIFT)
#define I40E_AQC_MIRROR_RULE_TYPE_VLAN		3
#define I40E_AQC_MIRROR_RULE_TYPE_ALL_INGRESS	4
#define I40E_AQC_MIRROR_RULE_TYPE_ALL_EGRESS	5
	__le16 num_entries;
	__le16 destination;  /* VSI for add, rule id for delete */
	__le32 addr_high;    /* address of array of 2-byte VSI or VLAN ids */
	__le32 addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_delete_mirror_rule);

struct i40e_aqc_add_delete_mirror_rule_completion {
	u8	reserved[2];
	__le16	rule_id;  /* only used on add */
	__le16	mirror_rules_used;
	__le16	mirror_rules_free;
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_delete_mirror_rule_completion);

/* Dynamic Device Personalization */
struct i40e_aqc_write_personalization_profile {
	u8      flags;
	u8      reserved[3];
	__le32  profile_track_id;
	__le32  addr_high;
	__le32  addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_write_personalization_profile);

struct i40e_aqc_write_ddp_resp {
	__le32 error_offset;
	__le32 error_info;
	__le32 addr_high;
	__le32 addr_low;
};

struct i40e_aqc_get_applied_profiles {
	u8      flags;
	u8      rsv[3];
	__le32  reserved;
	__le32  addr_high;
	__le32  addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_get_applied_profiles);

/* DCB 0x03xx*/

/* PFC Ignore (direct 0x0301)
 *    the command and response use the same descriptor structure
 */
struct i40e_aqc_pfc_ignore {
	u8	tc_bitmap;
	u8	command_flags; /* unused on response */
	u8	reserved[14];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_pfc_ignore);

/* DCB Update (direct 0x0302) uses the i40e_aq_desc structure
 * with no parameters
 */

/* TX scheduler 0x04xx */

/* Almost all the indirect commands use
 * this generic struct to pass the SEID in param0
 */
struct i40e_aqc_tx_sched_ind {
	__le16	vsi_seid;
	u8	reserved[6];
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_tx_sched_ind);

/* Several commands respond with a set of queue set handles */
struct i40e_aqc_qs_handles_resp {
	__le16 qs_handles[8];
};

/* Configure VSI BW limits (direct 0x0400) */
struct i40e_aqc_configure_vsi_bw_limit {
	__le16	vsi_seid;
	u8	reserved[2];
	__le16	credit;
	u8	reserved1[2];
	u8	max_credit; /* 0-3, limit = 2^max */
	u8	reserved2[7];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_configure_vsi_bw_limit);

/* Configure VSI Bandwidth Limit per Traffic Type (indirect 0x0406)
 *    responds with i40e_aqc_qs_handles_resp
 */
struct i40e_aqc_configure_vsi_ets_sla_bw_data {
	u8	tc_valid_bits;
	u8	reserved[15];
	__le16	tc_bw_credits[8]; /* FW writesback QS handles here */

	/* 4 bits per tc 0-7, 4th bit is reserved, limit = 2^max */
	__le16	tc_bw_max[2];
	u8	reserved1[28];
};

I40E_CHECK_STRUCT_LEN(0x40, i40e_aqc_configure_vsi_ets_sla_bw_data);

/* Configure VSI Bandwidth Allocation per Traffic Type (indirect 0x0407)
 *    responds with i40e_aqc_qs_handles_resp
 */
struct i40e_aqc_configure_vsi_tc_bw_data {
	u8	tc_valid_bits;
	u8	reserved[3];
	u8	tc_bw_credits[8];
	u8	reserved1[4];
	__le16	qs_handles[8];
};

I40E_CHECK_STRUCT_LEN(0x20, i40e_aqc_configure_vsi_tc_bw_data);

/* Query vsi bw configuration (indirect 0x0408) */
struct i40e_aqc_query_vsi_bw_config_resp {
	u8	tc_valid_bits;
	u8	tc_suspended_bits;
	u8	reserved[14];
	__le16	qs_handles[8];
	u8	reserved1[4];
	__le16	port_bw_limit;
	u8	reserved2[2];
	u8	max_bw; /* 0-3, limit = 2^max */
	u8	reserved3[23];
};

I40E_CHECK_STRUCT_LEN(0x40, i40e_aqc_query_vsi_bw_config_resp);

/* Query VSI Bandwidth Allocation per Traffic Type (indirect 0x040A) */
struct i40e_aqc_query_vsi_ets_sla_config_resp {
	u8	tc_valid_bits;
	u8	reserved[3];
	u8	share_credits[8];
	__le16	credits[8];

	/* 4 bits per tc 0-7, 4th bit is reserved, limit = 2^max */
	__le16	tc_bw_max[2];
};

I40E_CHECK_STRUCT_LEN(0x20, i40e_aqc_query_vsi_ets_sla_config_resp);

/* Configure Switching Component Bandwidth Limit (direct 0x0410) */
struct i40e_aqc_configure_switching_comp_bw_limit {
	__le16	seid;
	u8	reserved[2];
	__le16	credit;
	u8	reserved1[2];
	u8	max_bw; /* 0-3, limit = 2^max */
	u8	reserved2[7];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_configure_switching_comp_bw_limit);

/* Enable  Physical Port ETS (indirect 0x0413)
 * Modify  Physical Port ETS (indirect 0x0414)
 * Disable Physical Port ETS (indirect 0x0415)
 */
struct i40e_aqc_configure_switching_comp_ets_data {
	u8	reserved[4];
	u8	tc_valid_bits;
	u8	seepage;
	u8	tc_strict_priority_flags;
	u8	reserved1[17];
	u8	tc_bw_share_credits[8];
	u8	reserved2[96];
};

I40E_CHECK_STRUCT_LEN(0x80, i40e_aqc_configure_switching_comp_ets_data);

/* Configure Switching Component Bandwidth Limits per Tc (indirect 0x0416) */
struct i40e_aqc_configure_switching_comp_ets_bw_limit_data {
	u8	tc_valid_bits;
	u8	reserved[15];
	__le16	tc_bw_credit[8];

	/* 4 bits per tc 0-7, 4th bit is reserved, limit = 2^max */
	__le16	tc_bw_max[2];
	u8	reserved1[28];
};

I40E_CHECK_STRUCT_LEN(0x40,
		      i40e_aqc_configure_switching_comp_ets_bw_limit_data);

/* Configure Switching Component Bandwidth Allocation per Tc
 * (indirect 0x0417)
 */
struct i40e_aqc_configure_switching_comp_bw_config_data {
	u8	tc_valid_bits;
	u8	reserved[2];
	u8	absolute_credits; /* bool */
	u8	tc_bw_share_credits[8];
	u8	reserved1[20];
};

I40E_CHECK_STRUCT_LEN(0x20, i40e_aqc_configure_switching_comp_bw_config_data);

/* Query Switching Component Configuration (indirect 0x0418) */
struct i40e_aqc_query_switching_comp_ets_config_resp {
	u8	tc_valid_bits;
	u8	reserved[35];
	__le16	port_bw_limit;
	u8	reserved1[2];
	u8	tc_bw_max; /* 0-3, limit = 2^max */
	u8	reserved2[23];
};

I40E_CHECK_STRUCT_LEN(0x40, i40e_aqc_query_switching_comp_ets_config_resp);

/* Query PhysicalPort ETS Configuration (indirect 0x0419) */
struct i40e_aqc_query_port_ets_config_resp {
	u8	reserved[4];
	u8	tc_valid_bits;
	u8	reserved1;
	u8	tc_strict_priority_bits;
	u8	reserved2;
	u8	tc_bw_share_credits[8];
	__le16	tc_bw_limits[8];

	/* 4 bits per tc 0-7, 4th bit reserved, limit = 2^max */
	__le16	tc_bw_max[2];
	u8	reserved3[32];
};

I40E_CHECK_STRUCT_LEN(0x44, i40e_aqc_query_port_ets_config_resp);

/* Query Switching Component Bandwidth Allocation per Traffic Type
 * (indirect 0x041A)
 */
struct i40e_aqc_query_switching_comp_bw_config_resp {
	u8	tc_valid_bits;
	u8	reserved[2];
	u8	absolute_credits_enable; /* bool */
	u8	tc_bw_share_credits[8];
	__le16	tc_bw_limits[8];

	/* 4 bits per tc 0-7, 4th bit is reserved, limit = 2^max */
	__le16	tc_bw_max[2];
};

I40E_CHECK_STRUCT_LEN(0x20, i40e_aqc_query_switching_comp_bw_config_resp);

/* Suspend/resume port TX traffic
 * (direct 0x041B and 0x041C) uses the generic SEID struct
 */

/* Configure partition BW
 * (indirect 0x041D)
 */
struct i40e_aqc_configure_partition_bw_data {
	__le16	pf_valid_bits;
	u8	min_bw[16];      /* guaranteed bandwidth */
	u8	max_bw[16];      /* bandwidth limit */
};

I40E_CHECK_STRUCT_LEN(0x22, i40e_aqc_configure_partition_bw_data);

/* Get and set the active HMC resource profile and status.
 * (direct 0x0500) and (direct 0x0501)
 */
struct i40e_aq_get_set_hmc_resource_profile {
	u8	pm_profile;
	u8	pe_vf_enabled;
	u8	reserved[14];
};

I40E_CHECK_CMD_LENGTH(i40e_aq_get_set_hmc_resource_profile);

enum i40e_aq_hmc_profile {
	/* I40E_HMC_PROFILE_NO_CHANGE	= 0, reserved */
	I40E_HMC_PROFILE_DEFAULT	= 1,
	I40E_HMC_PROFILE_FAVOR_VF	= 2,
	I40E_HMC_PROFILE_EQUAL		= 3,
};

/* Get PHY Abilities (indirect 0x0600) uses the generic indirect struct */

/* set in param0 for get phy abilities to report qualified modules */
#define I40E_AQ_PHY_REPORT_QUALIFIED_MODULES	0x0001
#define I40E_AQ_PHY_REPORT_INITIAL_VALUES	0x0002

enum i40e_aq_phy_type {
	I40E_PHY_TYPE_SGMII			= 0x0,
	I40E_PHY_TYPE_1000BASE_KX		= 0x1,
	I40E_PHY_TYPE_10GBASE_KX4		= 0x2,
	I40E_PHY_TYPE_10GBASE_KR		= 0x3,
	I40E_PHY_TYPE_40GBASE_KR4		= 0x4,
	I40E_PHY_TYPE_XAUI			= 0x5,
	I40E_PHY_TYPE_XFI			= 0x6,
	I40E_PHY_TYPE_SFI			= 0x7,
	I40E_PHY_TYPE_XLAUI			= 0x8,
	I40E_PHY_TYPE_XLPPI			= 0x9,
	I40E_PHY_TYPE_40GBASE_CR4_CU		= 0xA,
	I40E_PHY_TYPE_10GBASE_CR1_CU		= 0xB,
	I40E_PHY_TYPE_10GBASE_AOC		= 0xC,
	I40E_PHY_TYPE_40GBASE_AOC		= 0xD,
	I40E_PHY_TYPE_UNRECOGNIZED		= 0xE,
	I40E_PHY_TYPE_UNSUPPORTED		= 0xF,
	I40E_PHY_TYPE_100BASE_TX		= 0x11,
	I40E_PHY_TYPE_1000BASE_T		= 0x12,
	I40E_PHY_TYPE_10GBASE_T			= 0x13,
	I40E_PHY_TYPE_10GBASE_SR		= 0x14,
	I40E_PHY_TYPE_10GBASE_LR		= 0x15,
	I40E_PHY_TYPE_10GBASE_SFPP_CU		= 0x16,
	I40E_PHY_TYPE_10GBASE_CR1		= 0x17,
	I40E_PHY_TYPE_40GBASE_CR4		= 0x18,
	I40E_PHY_TYPE_40GBASE_SR4		= 0x19,
	I40E_PHY_TYPE_40GBASE_LR4		= 0x1A,
	I40E_PHY_TYPE_1000BASE_SX		= 0x1B,
	I40E_PHY_TYPE_1000BASE_LX		= 0x1C,
	I40E_PHY_TYPE_1000BASE_T_OPTICAL	= 0x1D,
	I40E_PHY_TYPE_20GBASE_KR2		= 0x1E,
	I40E_PHY_TYPE_25GBASE_KR		= 0x1F,
	I40E_PHY_TYPE_25GBASE_CR		= 0x20,
	I40E_PHY_TYPE_25GBASE_SR		= 0x21,
	I40E_PHY_TYPE_25GBASE_LR		= 0x22,
	I40E_PHY_TYPE_25GBASE_AOC		= 0x23,
	I40E_PHY_TYPE_25GBASE_ACC		= 0x24,
	I40E_PHY_TYPE_2_5GBASE_T		= 0x26,
	I40E_PHY_TYPE_5GBASE_T			= 0x27,
	I40E_PHY_TYPE_2_5GBASE_T_LINK_STATUS	= 0x30,
	I40E_PHY_TYPE_5GBASE_T_LINK_STATUS	= 0x31,
	I40E_PHY_TYPE_MAX,
	I40E_PHY_TYPE_NOT_SUPPORTED_HIGH_TEMP	= 0xFD,
	I40E_PHY_TYPE_EMPTY			= 0xFE,
	I40E_PHY_TYPE_DEFAULT			= 0xFF,
};

#define I40E_PHY_TYPES_BITMASK (BIT_ULL(I40E_PHY_TYPE_SGMII) | \
				BIT_ULL(I40E_PHY_TYPE_1000BASE_KX) | \
				BIT_ULL(I40E_PHY_TYPE_10GBASE_KX4) | \
				BIT_ULL(I40E_PHY_TYPE_10GBASE_KR) | \
				BIT_ULL(I40E_PHY_TYPE_40GBASE_KR4) | \
				BIT_ULL(I40E_PHY_TYPE_XAUI) | \
				BIT_ULL(I40E_PHY_TYPE_XFI) | \
				BIT_ULL(I40E_PHY_TYPE_SFI) | \
				BIT_ULL(I40E_PHY_TYPE_XLAUI) | \
				BIT_ULL(I40E_PHY_TYPE_XLPPI) | \
				BIT_ULL(I40E_PHY_TYPE_40GBASE_CR4_CU) | \
				BIT_ULL(I40E_PHY_TYPE_10GBASE_CR1_CU) | \
				BIT_ULL(I40E_PHY_TYPE_10GBASE_AOC) | \
				BIT_ULL(I40E_PHY_TYPE_40GBASE_AOC) | \
				BIT_ULL(I40E_PHY_TYPE_UNRECOGNIZED) | \
				BIT_ULL(I40E_PHY_TYPE_UNSUPPORTED) | \
				BIT_ULL(I40E_PHY_TYPE_100BASE_TX) | \
				BIT_ULL(I40E_PHY_TYPE_1000BASE_T) | \
				BIT_ULL(I40E_PHY_TYPE_10GBASE_T) | \
				BIT_ULL(I40E_PHY_TYPE_10GBASE_SR) | \
				BIT_ULL(I40E_PHY_TYPE_10GBASE_LR) | \
				BIT_ULL(I40E_PHY_TYPE_10GBASE_SFPP_CU) | \
				BIT_ULL(I40E_PHY_TYPE_10GBASE_CR1) | \
				BIT_ULL(I40E_PHY_TYPE_40GBASE_CR4) | \
				BIT_ULL(I40E_PHY_TYPE_40GBASE_SR4) | \
				BIT_ULL(I40E_PHY_TYPE_40GBASE_LR4) | \
				BIT_ULL(I40E_PHY_TYPE_1000BASE_SX) | \
				BIT_ULL(I40E_PHY_TYPE_1000BASE_LX) | \
				BIT_ULL(I40E_PHY_TYPE_1000BASE_T_OPTICAL) | \
				BIT_ULL(I40E_PHY_TYPE_20GBASE_KR2) | \
				BIT_ULL(I40E_PHY_TYPE_25GBASE_KR) | \
				BIT_ULL(I40E_PHY_TYPE_25GBASE_CR) | \
				BIT_ULL(I40E_PHY_TYPE_25GBASE_SR) | \
				BIT_ULL(I40E_PHY_TYPE_25GBASE_LR) | \
				BIT_ULL(I40E_PHY_TYPE_25GBASE_AOC) | \
				BIT_ULL(I40E_PHY_TYPE_25GBASE_ACC) | \
				BIT_ULL(I40E_PHY_TYPE_2_5GBASE_T) | \
				BIT_ULL(I40E_PHY_TYPE_5GBASE_T))

#define I40E_LINK_SPEED_2_5GB_SHIFT	0x0
#define I40E_LINK_SPEED_100MB_SHIFT	0x1
#define I40E_LINK_SPEED_1000MB_SHIFT	0x2
#define I40E_LINK_SPEED_10GB_SHIFT	0x3
#define I40E_LINK_SPEED_40GB_SHIFT	0x4
#define I40E_LINK_SPEED_20GB_SHIFT	0x5
#define I40E_LINK_SPEED_25GB_SHIFT	0x6
#define I40E_LINK_SPEED_5GB_SHIFT	0x7

enum i40e_aq_link_speed {
	I40E_LINK_SPEED_UNKNOWN	= 0,
	I40E_LINK_SPEED_100MB	= BIT(I40E_LINK_SPEED_100MB_SHIFT),
	I40E_LINK_SPEED_1GB	= BIT(I40E_LINK_SPEED_1000MB_SHIFT),
	I40E_LINK_SPEED_2_5GB	= (1 << I40E_LINK_SPEED_2_5GB_SHIFT),
	I40E_LINK_SPEED_5GB	= (1 << I40E_LINK_SPEED_5GB_SHIFT),
	I40E_LINK_SPEED_10GB	= BIT(I40E_LINK_SPEED_10GB_SHIFT),
	I40E_LINK_SPEED_40GB	= BIT(I40E_LINK_SPEED_40GB_SHIFT),
	I40E_LINK_SPEED_20GB	= BIT(I40E_LINK_SPEED_20GB_SHIFT),
	I40E_LINK_SPEED_25GB	= BIT(I40E_LINK_SPEED_25GB_SHIFT),
};

struct i40e_aqc_module_desc {
	u8 oui[3];
	u8 reserved1;
	u8 part_number[16];
	u8 revision[4];
	u8 reserved2[8];
};

I40E_CHECK_STRUCT_LEN(0x20, i40e_aqc_module_desc);

struct i40e_aq_get_phy_abilities_resp {
	__le32	phy_type;       /* bitmap using the above enum for offsets */
	u8	link_speed;     /* bitmap using the above enum bit patterns */
	u8	abilities;
#define I40E_AQ_PHY_FLAG_PAUSE_TX	0x01
#define I40E_AQ_PHY_FLAG_PAUSE_RX	0x02
	__le16	eee_capability;
	__le32	eeer_val;
	u8	d3_lpan;
	u8	phy_type_ext;
#define I40E_AQ_PHY_TYPE_EXT_25G_KR	0X01
#define I40E_AQ_PHY_TYPE_EXT_25G_CR	0X02
#define I40E_AQ_PHY_TYPE_EXT_25G_SR	0x04
#define I40E_AQ_PHY_TYPE_EXT_25G_LR	0x08
	u8	fec_cfg_curr_mod_ext_info;
#define I40E_AQ_REQUEST_FEC_KR		0x04
#define I40E_AQ_REQUEST_FEC_RS		0x08
#define I40E_AQ_ENABLE_FEC_AUTO		0x10

	u8	ext_comp_code;
	u8	phy_id[4];
	u8	module_type[3];
	u8	qualified_module_count;
#define I40E_AQ_PHY_MAX_QMS		16
	struct i40e_aqc_module_desc	qualified_module[I40E_AQ_PHY_MAX_QMS];
};

I40E_CHECK_STRUCT_LEN(0x218, i40e_aq_get_phy_abilities_resp);

/* Set PHY Config (direct 0x0601) */
struct i40e_aq_set_phy_config { /* same bits as above in all */
	__le32	phy_type;
	u8	link_speed;
	u8	abilities;
/* bits 0-2 use the values from get_phy_abilities_resp */
#define I40E_AQ_PHY_ENABLE_LINK		0x08
#define I40E_AQ_PHY_ENABLE_AN		0x10
#define I40E_AQ_PHY_ENABLE_ATOMIC_LINK	0x20
	__le16	eee_capability;
	__le32	eeer;
	u8	low_power_ctrl;
	u8	phy_type_ext;
#define I40E_AQ_PHY_TYPE_EXT_25G_KR	0X01
#define I40E_AQ_PHY_TYPE_EXT_25G_CR	0X02
#define I40E_AQ_PHY_TYPE_EXT_25G_SR	0x04
#define I40E_AQ_PHY_TYPE_EXT_25G_LR	0x08
	u8	fec_config;
#define I40E_AQ_SET_FEC_ABILITY_KR	BIT(0)
#define I40E_AQ_SET_FEC_ABILITY_RS	BIT(1)
#define I40E_AQ_SET_FEC_REQUEST_KR	BIT(2)
#define I40E_AQ_SET_FEC_REQUEST_RS	BIT(3)
#define I40E_AQ_SET_FEC_AUTO		BIT(4)
#define I40E_AQ_PHY_FEC_CONFIG_SHIFT	0x0
#define I40E_AQ_PHY_FEC_CONFIG_MASK	(0x1F << I40E_AQ_PHY_FEC_CONFIG_SHIFT)
	u8	reserved;
};

I40E_CHECK_CMD_LENGTH(i40e_aq_set_phy_config);

/* Set MAC Config command data structure (direct 0x0603) */
struct i40e_aq_set_mac_config {
	__le16	max_frame_size;
	u8	params;
	u8	tx_timer_priority; /* bitmap */
	__le16	tx_timer_value;
	__le16	fc_refresh_threshold;
	u8	reserved[8];
};

I40E_CHECK_CMD_LENGTH(i40e_aq_set_mac_config);

/* Restart Auto-Negotiation (direct 0x605) */
struct i40e_aqc_set_link_restart_an {
	u8	command;
#define I40E_AQ_PHY_RESTART_AN	0x02
#define I40E_AQ_PHY_LINK_ENABLE	0x04
	u8	reserved[15];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_set_link_restart_an);

/* Get Link Status cmd & response data structure (direct 0x0607) */
struct i40e_aqc_get_link_status {
	__le16	command_flags; /* only field set on command */
#define I40E_AQ_LSE_DISABLE		0x2
#define I40E_AQ_LSE_ENABLE		0x3
/* only response uses this flag */
#define I40E_AQ_LSE_IS_ENABLED		0x1
	u8	phy_type;    /* i40e_aq_phy_type   */
	u8	link_speed;  /* i40e_aq_link_speed */
	u8	link_info;
#define I40E_AQ_LINK_UP			0x01    /* obsolete */
#define I40E_AQ_MEDIA_AVAILABLE		0x40
	u8	an_info;
#define I40E_AQ_AN_COMPLETED		0x01
#define I40E_AQ_LINK_PAUSE_TX		0x20
#define I40E_AQ_LINK_PAUSE_RX		0x40
#define I40E_AQ_QUALIFIED_MODULE	0x80
	u8	ext_info;
	u8	loopback; /* use defines from i40e_aqc_set_lb_mode */
/* Since firmware API 1.7 loopback field keeps power class info as well */
#define I40E_AQ_LOOPBACK_MASK		0x07
	__le16	max_frame_size;
	u8	config;
#define I40E_AQ_CONFIG_FEC_KR_ENA	0x01
#define I40E_AQ_CONFIG_FEC_RS_ENA	0x02
#define I40E_AQ_CONFIG_CRC_ENA		0x04
#define I40E_AQ_CONFIG_PACING_MASK	0x78
	union {
		struct {
			u8	power_desc;
			u8	reserved[4];
		};
		struct {
			u8	link_type[4];
			u8	link_type_ext;
		};
	};
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_get_link_status);

/* Set event mask command (direct 0x613) */
struct i40e_aqc_set_phy_int_mask {
	u8	reserved[8];
	__le16	event_mask;
#define I40E_AQ_EVENT_LINK_UPDOWN	0x0002
#define I40E_AQ_EVENT_MEDIA_NA		0x0004
#define I40E_AQ_EVENT_MODULE_QUAL_FAIL	0x0100
	u8	reserved1[6];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_set_phy_int_mask);

/* Get Local AN advt register (direct 0x0614)
 * Set Local AN advt register (direct 0x0615)
 * Get Link Partner AN advt register (direct 0x0616)
 */
struct i40e_aqc_an_advt_reg {
	__le32	local_an_reg0;
	__le16	local_an_reg1;
	u8	reserved[10];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_an_advt_reg);

/* Set Loopback mode (0x0618) */
struct i40e_aqc_set_lb_mode {
	__le16	lb_mode;
#define I40E_AQ_LB_PHY_LOCAL	0x01
#define I40E_AQ_LB_PHY_REMOTE	0x02
#define I40E_AQ_LB_MAC_LOCAL	0x04
	u8	reserved[14];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_set_lb_mode);

/* Set PHY Debug command (0x0622) */
struct i40e_aqc_set_phy_debug {
	u8	command_flags;
/* Disable link manageability on a single port */
#define I40E_AQ_PHY_DEBUG_DISABLE_LINK_FW	0x10
/* Disable link manageability on all ports */
#define I40E_AQ_PHY_DEBUG_DISABLE_ALL_LINK_FW	0x20
	u8	reserved[15];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_set_phy_debug);

enum i40e_aq_phy_reg_type {
	I40E_AQC_PHY_REG_INTERNAL	= 0x1,
	I40E_AQC_PHY_REG_EXERNAL_BASET	= 0x2,
	I40E_AQC_PHY_REG_EXERNAL_MODULE	= 0x3
};

/* Run PHY Activity (0x0626) */
struct i40e_aqc_run_phy_activity {
	__le16  activity_id;
	u8      flags;
	u8      reserved1;
	__le32  control;
	__le32  data;
	u8      reserved2[4];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_run_phy_activity);

/* Set PHY Register command (0x0628) */
/* Get PHY Register command (0x0629) */
struct i40e_aqc_phy_register_access {
	u8	phy_interface;
#define I40E_AQ_PHY_REG_ACCESS_EXTERNAL	1
#define I40E_AQ_PHY_REG_ACCESS_EXTERNAL_MODULE	2
	u8	dev_address;
	u8	cmd_flags;
#define I40E_AQ_PHY_REG_ACCESS_DONT_CHANGE_QSFP_PAGE	0x01
#define I40E_AQ_PHY_REG_ACCESS_SET_MDIO_IF_NUMBER	0x02
#define I40E_AQ_PHY_REG_ACCESS_MDIO_IF_NUMBER_SHIFT	2
#define I40E_AQ_PHY_REG_ACCESS_MDIO_IF_NUMBER_MASK	(0x3 << \
		I40E_AQ_PHY_REG_ACCESS_MDIO_IF_NUMBER_SHIFT)
	u8	reserved1;
	__le32	reg_address;
	__le32	reg_value;
	u8	reserved2[4];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_phy_register_access);

/* NVM Read command (indirect 0x0701)
 * NVM Erase commands (direct 0x0702)
 * NVM Update commands (indirect 0x0703)
 */
struct i40e_aqc_nvm_update {
	u8	command_flags;
#define I40E_AQ_NVM_LAST_CMD			0x01
#define I40E_AQ_NVM_REARRANGE_TO_FLAT		0x20
#define I40E_AQ_NVM_REARRANGE_TO_STRUCT		0x40
#define I40E_AQ_NVM_PRESERVATION_FLAGS_SHIFT	1
#define I40E_AQ_NVM_PRESERVATION_FLAGS_SELECTED	0x03
#define I40E_AQ_NVM_PRESERVATION_FLAGS_ALL	0x01
	u8	module_pointer;
	__le16	length;
	__le32	offset;
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_nvm_update);

/* NVM Config Read (indirect 0x0704) */
struct i40e_aqc_nvm_config_read {
	__le16	cmd_flags;
	__le16	element_count;
	__le16	element_id;	/* Feature/field ID */
	__le16	element_id_msw;	/* MSWord of field ID */
	__le32	address_high;
	__le32	address_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_nvm_config_read);

/* NVM Config Write (indirect 0x0705) */
struct i40e_aqc_nvm_config_write {
	__le16	cmd_flags;
	__le16	element_count;
	u8	reserved[4];
	__le32	address_high;
	__le32	address_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_nvm_config_write);

/* Used for 0x0704 as well as for 0x0705 commands */
struct i40e_aqc_nvm_config_data_feature {
	__le16 feature_id;
	__le16 feature_options;
	__le16 feature_selection;
};

I40E_CHECK_STRUCT_LEN(0x6, i40e_aqc_nvm_config_data_feature);

struct i40e_aqc_nvm_config_data_immediate_field {
	__le32 field_id;
	__le32 field_value;
	__le16 field_options;
	__le16 reserved;
};

I40E_CHECK_STRUCT_LEN(0xc, i40e_aqc_nvm_config_data_immediate_field);

/* OEM Post Update (indirect 0x0720)
 * no command data struct used
 */
struct i40e_aqc_nvm_oem_post_update {
	u8 sel_data;
	u8 reserved[7];
};

I40E_CHECK_STRUCT_LEN(0x8, i40e_aqc_nvm_oem_post_update);

struct i40e_aqc_nvm_oem_post_update_buffer {
	u8 str_len;
	u8 dev_addr;
	__le16 eeprom_addr;
	u8 data[36];
};

I40E_CHECK_STRUCT_LEN(0x28, i40e_aqc_nvm_oem_post_update_buffer);

/* Thermal Sensor (indirect 0x0721)
 *     read or set thermal sensor configs and values
 *     takes a sensor and command specific data buffer, not detailed here
 */
struct i40e_aqc_thermal_sensor {
	u8 sensor_action;
	u8 reserved[7];
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_thermal_sensor);

/* Send to PF command (indirect 0x0801) id is only used by PF
 * Send to VF command (indirect 0x0802) id is only used by PF
 * Send to Peer PF command (indirect 0x0803)
 */
struct i40e_aqc_pf_vf_message {
	__le32	id;
	u8	reserved[4];
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_pf_vf_message);

/* Alternate structure */

/* Direct write (direct 0x0900)
 * Direct read (direct 0x0902)
 */
struct i40e_aqc_alternate_write {
	__le32 address0;
	__le32 data0;
	__le32 address1;
	__le32 data1;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_alternate_write);

/* Indirect write (indirect 0x0901)
 * Indirect read (indirect 0x0903)
 */

struct i40e_aqc_alternate_ind_write {
	__le32 address;
	__le32 length;
	__le32 addr_high;
	__le32 addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_alternate_ind_write);

/* Done alternate write (direct 0x0904)
 * uses i40e_aq_desc
 */
struct i40e_aqc_alternate_write_done {
	__le16	cmd_flags;
	u8	reserved[14];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_alternate_write_done);

/* Set OEM mode (direct 0x0905) */
struct i40e_aqc_alternate_set_mode {
	__le32	mode;
	u8	reserved[12];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_alternate_set_mode);

/* Clear port Alternate RAM (direct 0x0906) uses i40e_aq_desc */

/* async events 0x10xx */

/* Lan Queue Overflow Event (direct, 0x1001) */
struct i40e_aqc_lan_overflow {
	__le32	prtdcb_rupto;
	__le32	otx_ctl;
	u8	reserved[8];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_lan_overflow);

/* Get LLDP MIB (indirect 0x0A00) */
struct i40e_aqc_lldp_get_mib {
	u8	type;
	u8	reserved1;
#define I40E_AQ_LLDP_MIB_TYPE_MASK		0x3
#define I40E_AQ_LLDP_MIB_LOCAL			0x0
#define I40E_AQ_LLDP_MIB_REMOTE			0x1
#define I40E_AQ_LLDP_BRIDGE_TYPE_MASK		0xC
#define I40E_AQ_LLDP_BRIDGE_TYPE_SHIFT		0x2
#define I40E_AQ_LLDP_BRIDGE_TYPE_NEAREST_BRIDGE	0x0
/* TX pause flags use I40E_AQ_LINK_TX_* above */
	__le16	local_len;
	__le16	remote_len;
	u8	reserved2[2];
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_lldp_get_mib);

/* Configure LLDP MIB Change Event (direct 0x0A01)
 * also used for the event (with type in the command field)
 */
struct i40e_aqc_lldp_update_mib {
	u8	command;
#define I40E_AQ_LLDP_MIB_UPDATE_DISABLE	0x1
	u8	reserved[7];
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_lldp_update_mib);

/* Add LLDP TLV (indirect 0x0A02)
 * Delete LLDP TLV (indirect 0x0A04)
 */
struct i40e_aqc_lldp_add_tlv {
	u8	type; /* only nearest bridge and non-TPMR from 0x0A00 */
	u8	reserved1[1];
	__le16	len;
	u8	reserved2[4];
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_lldp_add_tlv);

/* Update LLDP TLV (indirect 0x0A03) */
struct i40e_aqc_lldp_update_tlv {
	u8	type; /* only nearest bridge and non-TPMR from 0x0A00 */
	u8	reserved;
	__le16	old_len;
	__le16	new_offset;
	__le16	new_len;
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_lldp_update_tlv);

/* Stop LLDP (direct 0x0A05) */
struct i40e_aqc_lldp_stop {
	u8	command;
#define I40E_AQ_LLDP_AGENT_SHUTDOWN		0x1
#define I40E_AQ_LLDP_AGENT_STOP_PERSIST		0x2
	u8	reserved[15];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_lldp_stop);

/* Start LLDP (direct 0x0A06) */
struct i40e_aqc_lldp_start {
	u8	command;
#define I40E_AQ_LLDP_AGENT_START		0x1
#define I40E_AQ_LLDP_AGENT_START_PERSIST	0x2
	u8	reserved[15];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_lldp_start);

/* Set DCB (direct 0x0303) */
struct i40e_aqc_set_dcb_parameters {
	u8 command;
#define I40E_AQ_DCB_SET_AGENT	0x1
#define I40E_DCB_VALID		0x1
	u8 valid_flags;
	u8 reserved[14];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_set_dcb_parameters);

/* Get CEE DCBX Oper Config (0x0A07)
 * uses the generic descriptor struct
 * returns below as indirect response
 */

#define I40E_AQC_CEE_APP_FCOE_SHIFT	0x0
#define I40E_AQC_CEE_APP_FCOE_MASK	(0x7 << I40E_AQC_CEE_APP_FCOE_SHIFT)
#define I40E_AQC_CEE_APP_ISCSI_SHIFT	0x3
#define I40E_AQC_CEE_APP_ISCSI_MASK	(0x7 << I40E_AQC_CEE_APP_ISCSI_SHIFT)
#define I40E_AQC_CEE_APP_FIP_SHIFT	0x8
#define I40E_AQC_CEE_APP_FIP_MASK	(0x7 << I40E_AQC_CEE_APP_FIP_SHIFT)

#define I40E_AQC_CEE_PG_STATUS_SHIFT	0x0
#define I40E_AQC_CEE_PG_STATUS_MASK	(0x7 << I40E_AQC_CEE_PG_STATUS_SHIFT)
#define I40E_AQC_CEE_PFC_STATUS_SHIFT	0x3
#define I40E_AQC_CEE_PFC_STATUS_MASK	(0x7 << I40E_AQC_CEE_PFC_STATUS_SHIFT)
#define I40E_AQC_CEE_APP_STATUS_SHIFT	0x8
#define I40E_AQC_CEE_APP_STATUS_MASK	(0x7 << I40E_AQC_CEE_APP_STATUS_SHIFT)
#define I40E_AQC_CEE_FCOE_STATUS_SHIFT	0x8
#define I40E_AQC_CEE_FCOE_STATUS_MASK	(0x7 << I40E_AQC_CEE_FCOE_STATUS_SHIFT)
#define I40E_AQC_CEE_ISCSI_STATUS_SHIFT	0xB
#define I40E_AQC_CEE_ISCSI_STATUS_MASK	(0x7 << I40E_AQC_CEE_ISCSI_STATUS_SHIFT)
#define I40E_AQC_CEE_FIP_STATUS_SHIFT	0x10
#define I40E_AQC_CEE_FIP_STATUS_MASK	(0x7 << I40E_AQC_CEE_FIP_STATUS_SHIFT)

/* struct i40e_aqc_get_cee_dcb_cfg_v1_resp was originally defined with
 * word boundary layout issues, which the Linux compilers silently deal
 * with by adding padding, making the actual struct larger than designed.
 * However, the FW compiler for the NIC is less lenient and complains
 * about the struct.  Hence, the struct defined here has an extra byte in
 * fields reserved3 and reserved4 to directly acknowledge that padding,
 * and the new length is used in the length check macro.
 */
struct i40e_aqc_get_cee_dcb_cfg_v1_resp {
	u8	reserved1;
	u8	oper_num_tc;
	u8	oper_prio_tc[4];
	u8	reserved2;
	u8	oper_tc_bw[8];
	u8	oper_pfc_en;
	u8	reserved3[2];
	__le16	oper_app_prio;
	u8	reserved4[2];
	__le16	tlv_status;
};

I40E_CHECK_STRUCT_LEN(0x18, i40e_aqc_get_cee_dcb_cfg_v1_resp);

struct i40e_aqc_get_cee_dcb_cfg_resp {
	u8	oper_num_tc;
	u8	oper_prio_tc[4];
	u8	oper_tc_bw[8];
	u8	oper_pfc_en;
	__le16	oper_app_prio;
#define I40E_AQC_CEE_APP_FCOE_SHIFT	0x0
#define I40E_AQC_CEE_APP_FCOE_MASK	(0x7 << I40E_AQC_CEE_APP_FCOE_SHIFT)
#define I40E_AQC_CEE_APP_ISCSI_SHIFT	0x3
#define I40E_AQC_CEE_APP_ISCSI_MASK	(0x7 << I40E_AQC_CEE_APP_ISCSI_SHIFT)
#define I40E_AQC_CEE_APP_FIP_SHIFT	0x8
#define I40E_AQC_CEE_APP_FIP_MASK	(0x7 << I40E_AQC_CEE_APP_FIP_SHIFT)
#define I40E_AQC_CEE_APP_FIP_MASK	(0x7 << I40E_AQC_CEE_APP_FIP_SHIFT)
	__le32	tlv_status;
#define I40E_AQC_CEE_PG_STATUS_SHIFT	0x0
#define I40E_AQC_CEE_PG_STATUS_MASK	(0x7 << I40E_AQC_CEE_PG_STATUS_SHIFT)
#define I40E_AQC_CEE_PFC_STATUS_SHIFT	0x3
#define I40E_AQC_CEE_PFC_STATUS_MASK	(0x7 << I40E_AQC_CEE_PFC_STATUS_SHIFT)
#define I40E_AQC_CEE_APP_STATUS_SHIFT	0x8
#define I40E_AQC_CEE_APP_STATUS_MASK	(0x7 << I40E_AQC_CEE_APP_STATUS_SHIFT)
	u8	reserved[12];
};

I40E_CHECK_STRUCT_LEN(0x20, i40e_aqc_get_cee_dcb_cfg_resp);

/*	Set Local LLDP MIB (indirect 0x0A08)
 *	Used to replace the local MIB of a given LLDP agent. e.g. DCBx
 */
struct i40e_aqc_lldp_set_local_mib {
#define SET_LOCAL_MIB_AC_TYPE_DCBX_SHIFT	0
#define SET_LOCAL_MIB_AC_TYPE_DCBX_MASK	(1 << \
					SET_LOCAL_MIB_AC_TYPE_DCBX_SHIFT)
#define SET_LOCAL_MIB_AC_TYPE_LOCAL_MIB	0x0
#define SET_LOCAL_MIB_AC_TYPE_NON_WILLING_APPS_SHIFT	(1)
#define SET_LOCAL_MIB_AC_TYPE_NON_WILLING_APPS_MASK	(1 << \
				SET_LOCAL_MIB_AC_TYPE_NON_WILLING_APPS_SHIFT)
#define SET_LOCAL_MIB_AC_TYPE_NON_WILLING_APPS		0x1
	u8	type;
	u8	reserved0;
	__le16	length;
	u8	reserved1[4];
	__le32	address_high;
	__le32	address_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_lldp_set_local_mib);

/*	Stop/Start LLDP Agent (direct 0x0A09)
 *	Used for stopping/starting specific LLDP agent. e.g. DCBx
 */
struct i40e_aqc_lldp_stop_start_specific_agent {
	u8	command;
	u8	reserved[15];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_lldp_stop_start_specific_agent);

/* Restore LLDP Agent factory settings (direct 0x0A0A) */
struct i40e_aqc_lldp_restore {
	u8	command;
#define I40E_AQ_LLDP_AGENT_RESTORE		0x1
	u8	reserved[15];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_lldp_restore);

/* Add Udp Tunnel command and completion (direct 0x0B00) */
struct i40e_aqc_add_udp_tunnel {
	__le16	udp_port;
	u8	reserved0[3];
	u8	protocol_type;
#define I40E_AQC_TUNNEL_TYPE_VXLAN	0x00
#define I40E_AQC_TUNNEL_TYPE_NGE	0x01
	u8	reserved1[10];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_udp_tunnel);

struct i40e_aqc_add_udp_tunnel_completion {
	__le16	udp_port;
	u8	filter_entry_index;
	u8	multiple_pfs;
	u8	total_filters;
	u8	reserved[11];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_udp_tunnel_completion);

/* remove UDP Tunnel command (0x0B01) */
struct i40e_aqc_remove_udp_tunnel {
	u8	reserved[2];
	u8	index; /* 0 to 15 */
	u8	reserved2[13];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_remove_udp_tunnel);

struct i40e_aqc_del_udp_tunnel_completion {
	__le16	udp_port;
	u8	index; /* 0 to 15 */
	u8	multiple_pfs;
	u8	total_filters_used;
	u8	reserved1[11];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_del_udp_tunnel_completion);

struct i40e_aqc_get_set_rss_key {
#define I40E_AQC_SET_RSS_KEY_VSI_VALID		BIT(15)
#define I40E_AQC_SET_RSS_KEY_VSI_ID_SHIFT	0
#define I40E_AQC_SET_RSS_KEY_VSI_ID_MASK	(0x3FF << \
					I40E_AQC_SET_RSS_KEY_VSI_ID_SHIFT)
	__le16	vsi_id;
	u8	reserved[6];
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_get_set_rss_key);

struct i40e_aqc_get_set_rss_key_data {
	u8 standard_rss_key[0x28];
	u8 extended_hash_key[0xc];
};

I40E_CHECK_STRUCT_LEN(0x34, i40e_aqc_get_set_rss_key_data);

struct  i40e_aqc_get_set_rss_lut {
#define I40E_AQC_SET_RSS_LUT_VSI_VALID		BIT(15)
#define I40E_AQC_SET_RSS_LUT_VSI_ID_SHIFT	0
#define I40E_AQC_SET_RSS_LUT_VSI_ID_MASK	(0x3FF << \
					I40E_AQC_SET_RSS_LUT_VSI_ID_SHIFT)
	__le16	vsi_id;
#define I40E_AQC_SET_RSS_LUT_TABLE_TYPE_SHIFT	0
#define I40E_AQC_SET_RSS_LUT_TABLE_TYPE_MASK	BIT(I40E_AQC_SET_RSS_LUT_TABLE_TYPE_SHIFT)

#define I40E_AQC_SET_RSS_LUT_TABLE_TYPE_VSI	0
#define I40E_AQC_SET_RSS_LUT_TABLE_TYPE_PF	1
	__le16	flags;
	u8	reserved[4];
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_get_set_rss_lut);

/* tunnel key structure 0x0B10 */

struct i40e_aqc_tunnel_key_structure {
	u8	key1_off;
	u8	key2_off;
	u8	key1_len;  /* 0 to 15 */
	u8	key2_len;  /* 0 to 15 */
	u8	flags;
	u8	network_key_index;
	u8	reserved[10];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_tunnel_key_structure);

/* OEM mode commands (direct 0xFE0x) */
struct i40e_aqc_oem_param_change {
	__le32	param_type;
	__le32	param_value1;
	__le16	param_value2;
	u8	reserved[6];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_oem_param_change);

struct i40e_aqc_oem_state_change {
	__le32	state;
	u8	reserved[12];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_oem_state_change);

/* Initialize OCSD (0xFE02, direct) */
struct i40e_aqc_opc_oem_ocsd_initialize {
	u8 type_status;
	u8 reserved1[3];
	__le32 ocsd_memory_block_addr_high;
	__le32 ocsd_memory_block_addr_low;
	__le32 requested_update_interval;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_opc_oem_ocsd_initialize);

/* Initialize OCBB  (0xFE03, direct) */
struct i40e_aqc_opc_oem_ocbb_initialize {
	u8 type_status;
	u8 reserved1[3];
	__le32 ocbb_memory_block_addr_high;
	__le32 ocbb_memory_block_addr_low;
	u8 reserved2[4];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_opc_oem_ocbb_initialize);

/* debug commands */

/* get device id (0xFF00) uses the generic structure */

/* set test more (0xFF01, internal) */

struct i40e_acq_set_test_mode {
	u8	mode;
	u8	reserved[3];
	u8	command;
	u8	reserved2[3];
	__le32	address_high;
	__le32	address_low;
};

I40E_CHECK_CMD_LENGTH(i40e_acq_set_test_mode);

/* Debug Read Register command (0xFF03)
 * Debug Write Register command (0xFF04)
 */
struct i40e_aqc_debug_reg_read_write {
	__le32 reserved;
	__le32 address;
	__le32 value_high;
	__le32 value_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_debug_reg_read_write);

/* Scatter/gather Reg Read  (indirect 0xFF05)
 * Scatter/gather Reg Write (indirect 0xFF06)
 */

/* i40e_aq_desc is used for the command */
struct i40e_aqc_debug_reg_sg_element_data {
	__le32 address;
	__le32 value;
};

/* Debug Modify register (direct 0xFF07) */
struct i40e_aqc_debug_modify_reg {
	__le32 address;
	__le32 value;
	__le32 clear_mask;
	__le32 set_mask;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_debug_modify_reg);

/* dump internal data (0xFF08, indirect) */
struct i40e_aqc_debug_dump_internals {
	u8	cluster_id;
	u8	table_id;
	__le16	data_size;
	__le32	idx;
	__le32	address_high;
	__le32	address_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_debug_dump_internals);

struct i40e_aqc_debug_modify_internals {
	u8	cluster_id;
	u8	cluster_specific_params[7];
	__le32	address_high;
	__le32	address_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_debug_modify_internals);

#endif /* _I40E_ADMINQ_CMD_H_ */
