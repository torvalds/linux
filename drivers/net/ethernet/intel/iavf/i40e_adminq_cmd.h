/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#ifndef _I40E_ADMINQ_CMD_H_
#define _I40E_ADMINQ_CMD_H_

/* This header file defines the i40e Admin Queue commands and is shared between
 * i40e Firmware and Software.  Do not change the names in this file to IAVF
 * because this file should be diff-able against the i40e version, even
 * though many parts have been removed in this VF version.
 *
 * This file needs to comply with the Linux Kernel coding style.
 */

#define I40E_FW_API_VERSION_MAJOR	0x0001
#define I40E_FW_API_VERSION_MINOR_X722	0x0005
#define I40E_FW_API_VERSION_MINOR_X710	0x0007

#define I40E_FW_MINOR_VERSION(_h) ((_h)->mac.type == I40E_MAC_XL710 ? \
					I40E_FW_API_VERSION_MINOR_X710 : \
					I40E_FW_API_VERSION_MINOR_X722)

/* API version 1.7 implements additional link and PHY-specific APIs  */
#define I40E_MINOR_VER_GET_LINK_INFO_XL710 0x0007

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
#define I40E_AQ_FLAG_DD_SHIFT	0
#define I40E_AQ_FLAG_CMP_SHIFT	1
#define I40E_AQ_FLAG_ERR_SHIFT	2
#define I40E_AQ_FLAG_VFE_SHIFT	3
#define I40E_AQ_FLAG_LB_SHIFT	9
#define I40E_AQ_FLAG_RD_SHIFT	10
#define I40E_AQ_FLAG_VFC_SHIFT	11
#define I40E_AQ_FLAG_BUF_SHIFT	12
#define I40E_AQ_FLAG_SI_SHIFT	13
#define I40E_AQ_FLAG_EI_SHIFT	14
#define I40E_AQ_FLAG_FE_SHIFT	15

#define I40E_AQ_FLAG_DD		BIT(I40E_AQ_FLAG_DD_SHIFT)  /* 0x1    */
#define I40E_AQ_FLAG_CMP	BIT(I40E_AQ_FLAG_CMP_SHIFT) /* 0x2    */
#define I40E_AQ_FLAG_ERR	BIT(I40E_AQ_FLAG_ERR_SHIFT) /* 0x4    */
#define I40E_AQ_FLAG_VFE	BIT(I40E_AQ_FLAG_VFE_SHIFT) /* 0x8    */
#define I40E_AQ_FLAG_LB		BIT(I40E_AQ_FLAG_LB_SHIFT)  /* 0x200  */
#define I40E_AQ_FLAG_RD		BIT(I40E_AQ_FLAG_RD_SHIFT)  /* 0x400  */
#define I40E_AQ_FLAG_VFC	BIT(I40E_AQ_FLAG_VFC_SHIFT) /* 0x800  */
#define I40E_AQ_FLAG_BUF	BIT(I40E_AQ_FLAG_BUF_SHIFT) /* 0x1000 */
#define I40E_AQ_FLAG_SI		BIT(I40E_AQ_FLAG_SI_SHIFT)  /* 0x2000 */
#define I40E_AQ_FLAG_EI		BIT(I40E_AQ_FLAG_EI_SHIFT)  /* 0x4000 */
#define I40E_AQ_FLAG_FE		BIT(I40E_AQ_FLAG_FE_SHIFT)  /* 0x8000 */

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

/* Queue Shutdown (direct 0x0003) */
struct i40e_aqc_queue_shutdown {
	__le32	driver_unloading;
#define I40E_AQ_DRIVER_UNLOADING	0x1
	u8	reserved[12];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_queue_shutdown);

struct i40e_aqc_vsi_properties_data {
	/* first 96 byte are written by SW */
	__le16	valid_sections;
#define I40E_AQ_VSI_PROP_SWITCH_VALID		0x0001
#define I40E_AQ_VSI_PROP_SECURITY_VALID		0x0002
#define I40E_AQ_VSI_PROP_VLAN_VALID		0x0004
#define I40E_AQ_VSI_PROP_CAS_PV_VALID		0x0008
#define I40E_AQ_VSI_PROP_INGRESS_UP_VALID	0x0010
#define I40E_AQ_VSI_PROP_EGRESS_UP_VALID	0x0020
#define I40E_AQ_VSI_PROP_QUEUE_MAP_VALID	0x0040
#define I40E_AQ_VSI_PROP_QUEUE_OPT_VALID	0x0080
#define I40E_AQ_VSI_PROP_OUTER_UP_VALID		0x0100
#define I40E_AQ_VSI_PROP_SCHED_VALID		0x0200
	/* switch section */
	__le16	switch_id; /* 12bit id combined with flags below */
#define I40E_AQ_VSI_SW_ID_SHIFT		0x0000
#define I40E_AQ_VSI_SW_ID_MASK		(0xFFF << I40E_AQ_VSI_SW_ID_SHIFT)
#define I40E_AQ_VSI_SW_ID_FLAG_NOT_STAG	0x1000
#define I40E_AQ_VSI_SW_ID_FLAG_ALLOW_LB	0x2000
#define I40E_AQ_VSI_SW_ID_FLAG_LOCAL_LB	0x4000
	u8	sw_reserved[2];
	/* security section */
	u8	sec_flags;
#define I40E_AQ_VSI_SEC_FLAG_ALLOW_DEST_OVRD	0x01
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
#define I40E_AQ_VSI_PVLAN_MODE_UNTAGGED	0x02
#define I40E_AQ_VSI_PVLAN_MODE_ALL	0x03
#define I40E_AQ_VSI_PVLAN_INSERT_PVID	0x04
#define I40E_AQ_VSI_PVLAN_EMOD_SHIFT	0x03
#define I40E_AQ_VSI_PVLAN_EMOD_MASK	(0x3 << \
					 I40E_AQ_VSI_PVLAN_EMOD_SHIFT)
#define I40E_AQ_VSI_PVLAN_EMOD_STR_BOTH	0x0
#define I40E_AQ_VSI_PVLAN_EMOD_STR_UP	0x08
#define I40E_AQ_VSI_PVLAN_EMOD_STR	0x10
#define I40E_AQ_VSI_PVLAN_EMOD_NOTHING	0x18
	u8	pvlan_reserved[3];
	/* ingress egress up sections */
	__le32	ingress_table; /* bitmap, 3 bits per up */
#define I40E_AQ_VSI_UP_TABLE_UP0_SHIFT	0
#define I40E_AQ_VSI_UP_TABLE_UP0_MASK	(0x7 << \
					 I40E_AQ_VSI_UP_TABLE_UP0_SHIFT)
#define I40E_AQ_VSI_UP_TABLE_UP1_SHIFT	3
#define I40E_AQ_VSI_UP_TABLE_UP1_MASK	(0x7 << \
					 I40E_AQ_VSI_UP_TABLE_UP1_SHIFT)
#define I40E_AQ_VSI_UP_TABLE_UP2_SHIFT	6
#define I40E_AQ_VSI_UP_TABLE_UP2_MASK	(0x7 << \
					 I40E_AQ_VSI_UP_TABLE_UP2_SHIFT)
#define I40E_AQ_VSI_UP_TABLE_UP3_SHIFT	9
#define I40E_AQ_VSI_UP_TABLE_UP3_MASK	(0x7 << \
					 I40E_AQ_VSI_UP_TABLE_UP3_SHIFT)
#define I40E_AQ_VSI_UP_TABLE_UP4_SHIFT	12
#define I40E_AQ_VSI_UP_TABLE_UP4_MASK	(0x7 << \
					 I40E_AQ_VSI_UP_TABLE_UP4_SHIFT)
#define I40E_AQ_VSI_UP_TABLE_UP5_SHIFT	15
#define I40E_AQ_VSI_UP_TABLE_UP5_MASK	(0x7 << \
					 I40E_AQ_VSI_UP_TABLE_UP5_SHIFT)
#define I40E_AQ_VSI_UP_TABLE_UP6_SHIFT	18
#define I40E_AQ_VSI_UP_TABLE_UP6_MASK	(0x7 << \
					 I40E_AQ_VSI_UP_TABLE_UP6_SHIFT)
#define I40E_AQ_VSI_UP_TABLE_UP7_SHIFT	21
#define I40E_AQ_VSI_UP_TABLE_UP7_MASK	(0x7 << \
					 I40E_AQ_VSI_UP_TABLE_UP7_SHIFT)
	__le32	egress_table;   /* same defines as for ingress table */
	/* cascaded PV section */
	__le16	cas_pv_tag;
	u8	cas_pv_flags;
#define I40E_AQ_VSI_CAS_PV_TAGX_SHIFT		0x00
#define I40E_AQ_VSI_CAS_PV_TAGX_MASK		(0x03 << \
						 I40E_AQ_VSI_CAS_PV_TAGX_SHIFT)
#define I40E_AQ_VSI_CAS_PV_TAGX_LEAVE		0x00
#define I40E_AQ_VSI_CAS_PV_TAGX_REMOVE		0x01
#define I40E_AQ_VSI_CAS_PV_TAGX_COPY		0x02
#define I40E_AQ_VSI_CAS_PV_INSERT_TAG		0x10
#define I40E_AQ_VSI_CAS_PV_ETAG_PRUNE		0x20
#define I40E_AQ_VSI_CAS_PV_ACCEPT_HOST_TAG	0x40
	u8	cas_pv_reserved;
	/* queue mapping section */
	__le16	mapping_flags;
#define I40E_AQ_VSI_QUE_MAP_CONTIG	0x0
#define I40E_AQ_VSI_QUE_MAP_NONCONTIG	0x1
	__le16	queue_mapping[16];
#define I40E_AQ_VSI_QUEUE_SHIFT		0x0
#define I40E_AQ_VSI_QUEUE_MASK		(0x7FF << I40E_AQ_VSI_QUEUE_SHIFT)
	__le16	tc_mapping[8];
#define I40E_AQ_VSI_TC_QUE_OFFSET_SHIFT	0
#define I40E_AQ_VSI_TC_QUE_OFFSET_MASK	(0x1FF << \
					 I40E_AQ_VSI_TC_QUE_OFFSET_SHIFT)
#define I40E_AQ_VSI_TC_QUE_NUMBER_SHIFT	9
#define I40E_AQ_VSI_TC_QUE_NUMBER_MASK	(0x7 << \
					 I40E_AQ_VSI_TC_QUE_NUMBER_SHIFT)
	/* queueing option section */
	u8	queueing_opt_flags;
#define I40E_AQ_VSI_QUE_OPT_MULTICAST_UDP_ENA	0x04
#define I40E_AQ_VSI_QUE_OPT_UNICAST_UDP_ENA	0x08
#define I40E_AQ_VSI_QUE_OPT_TCP_ENA	0x10
#define I40E_AQ_VSI_QUE_OPT_FCOE_ENA	0x20
#define I40E_AQ_VSI_QUE_OPT_RSS_LUT_PF	0x00
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

#define I40E_LINK_SPEED_100MB_SHIFT	0x1
#define I40E_LINK_SPEED_1000MB_SHIFT	0x2
#define I40E_LINK_SPEED_10GB_SHIFT	0x3
#define I40E_LINK_SPEED_40GB_SHIFT	0x4
#define I40E_LINK_SPEED_20GB_SHIFT	0x5
#define I40E_LINK_SPEED_25GB_SHIFT	0x6

enum i40e_aq_link_speed {
	I40E_LINK_SPEED_UNKNOWN	= 0,
	I40E_LINK_SPEED_100MB	= BIT(I40E_LINK_SPEED_100MB_SHIFT),
	I40E_LINK_SPEED_1GB	= BIT(I40E_LINK_SPEED_1000MB_SHIFT),
	I40E_LINK_SPEED_10GB	= BIT(I40E_LINK_SPEED_10GB_SHIFT),
	I40E_LINK_SPEED_40GB	= BIT(I40E_LINK_SPEED_40GB_SHIFT),
	I40E_LINK_SPEED_20GB	= BIT(I40E_LINK_SPEED_20GB_SHIFT),
	I40E_LINK_SPEED_25GB	= BIT(I40E_LINK_SPEED_25GB_SHIFT),
};

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
#define I40E_AQC_SET_RSS_LUT_TABLE_TYPE_MASK \
				BIT(I40E_AQC_SET_RSS_LUT_TABLE_TYPE_SHIFT)

#define I40E_AQC_SET_RSS_LUT_TABLE_TYPE_VSI	0
#define I40E_AQC_SET_RSS_LUT_TABLE_TYPE_PF	1
	__le16	flags;
	u8	reserved[4];
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_get_set_rss_lut);
#endif /* _I40E_ADMINQ_CMD_H_ */
