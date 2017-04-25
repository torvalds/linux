/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#ifndef _I40E_ADMINQ_CMD_H_
#define _I40E_ADMINQ_CMD_H_

/* This header file defines the i40e Admin Queue commands and is shared between
 * i40e Firmware and Software.
 *
 * This file needs to comply with the Linux Kernel coding style.
 */

#define I40E_FW_API_VERSION_MAJOR	0x0001
#define I40E_FW_API_VERSION_MINOR	0x0005

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

	/* LAA */
	i40e_aqc_opc_mac_address_read	= 0x0107,
	i40e_aqc_opc_mac_address_write	= 0x0108,

	/* PXE */
	i40e_aqc_opc_clear_pxe_mode	= 0x0110,

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

	i40e_aqc_opc_add_mirror_rule	= 0x0260,
	i40e_aqc_opc_delete_mirror_rule	= 0x0261,

	/* DCB commands */
	i40e_aqc_opc_dcb_ignore_pfc	= 0x0301,
	i40e_aqc_opc_dcb_updated	= 0x0302,

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
#define I40E_AQ_RESOURCE_NVM			1
#define I40E_AQ_RESOURCE_SDP			2
#define I40E_AQ_RESOURCE_ACCESS_READ		1
#define I40E_AQ_RESOURCE_ACCESS_WRITE		2
#define I40E_AQ_RESOURCE_NVM_READ_TIMEOUT	3000
#define I40E_AQ_RESOURCE_NVM_WRITE_TIMEOUT	180000

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
#define I40E_AQ_LIST_CAP_PF_INDEX_EN	1
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
#define I40E_AQ_CAP_ID_ALTERNATE_RAM	0x0006
#define I40E_AQ_CAP_ID_WOL_AND_PROXY	0x0008
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
#define I40E_AQ_CPPM_EN_LTRC	0x0800
#define I40E_AQ_CPPM_EN_DMCTH	0x1000
#define I40E_AQ_CPPM_EN_DMCTLX	0x2000
#define I40E_AQ_CPPM_EN_HPTC	0x4000
#define I40E_AQ_CPPM_EN_DMARC	0x8000
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
#define I40E_AQ_ARP_INIT_IPV4	0x0800
#define I40E_AQ_ARP_UNSUP_CTL	0x1000
#define I40E_AQ_ARP_ENA		0x2000
#define I40E_AQ_ARP_ADD_IPV4	0x4000
#define I40E_AQ_ARP_DEL_IPV4	0x8000
	__le16	table_id;
	__le32	enabled_offloads;
#define I40E_AQ_ARP_DIRECTED_OFFLOAD_ENABLE	0x00000020
#define I40E_AQ_ARP_OFFLOAD_ENABLE		0x00000800
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
#define I40E_AQ_NS_PROXY_ADD_0		0x0001
#define I40E_AQ_NS_PROXY_DEL_0		0x0002
#define I40E_AQ_NS_PROXY_ADD_1		0x0004
#define I40E_AQ_NS_PROXY_DEL_1		0x0008
#define I40E_AQ_NS_PROXY_ADD_IPV6_0	0x0010
#define I40E_AQ_NS_PROXY_DEL_IPV6_0	0x0020
#define I40E_AQ_NS_PROXY_ADD_IPV6_1	0x0040
#define I40E_AQ_NS_PROXY_DEL_IPV6_1	0x0080
#define I40E_AQ_NS_PROXY_COMMAND_SEQ	0x0100
#define I40E_AQ_NS_PROXY_INIT_IPV6_TBL	0x0200
#define I40E_AQ_NS_PROXY_INIT_MAC_TBL	0x0400
#define I40E_AQ_NS_PROXY_OFFLOAD_ENABLE	0x0800
#define I40E_AQ_NS_PROXY_DIRECTED_OFFLOAD_ENABLE	0x1000
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
#define I40E_AQ_LAA_FLAG_WR	0x8000
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
#define I40E_AQC_SAN_ADDR_VALID		0x20
#define I40E_AQC_PORT_ADDR_VALID	0x40
#define I40E_AQC_WOL_ADDR_VALID		0x80
#define I40E_AQC_MC_MAG_EN_VALID	0x100
#define I40E_AQC_ADDR_VALID_MASK	0x1F0
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
#define I40E_AQC_WRITE_TYPE_PORT	0x8000
#define I40E_AQC_WRITE_TYPE_UPDATE_MC_MAG	0xC000
#define I40E_AQC_WRITE_TYPE_MASK	0xC000

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
#define I40E_AQ_SW_ELEM_TYPE_MAC	1
#define I40E_AQ_SW_ELEM_TYPE_PF		2
#define I40E_AQ_SW_ELEM_TYPE_VF		3
#define I40E_AQ_SW_ELEM_TYPE_EMP	4
#define I40E_AQ_SW_ELEM_TYPE_BMC	5
#define I40E_AQ_SW_ELEM_TYPE_PV		16
#define I40E_AQ_SW_ELEM_TYPE_VEB	17
#define I40E_AQ_SW_ELEM_TYPE_PA		18
#define I40E_AQ_SW_ELEM_TYPE_VSI	19
	u8	revision;
#define I40E_AQ_SW_ELEM_REV_1		1
	__le16	seid;
	__le16	uplink_seid;
	__le16	downlink_seid;
	u8	reserved[3];
	u8	connection_type;
#define I40E_AQ_CONN_TYPE_REGULAR	0x1
#define I40E_AQ_CONN_TYPE_DEFAULT	0x2
#define I40E_AQ_CONN_TYPE_CASCADED	0x3
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
#define I40E_AQ_SET_P_PARAMS_SAVE_BAD_PACKETS	1
#define I40E_AQ_SET_P_PARAMS_PAD_SHORT_PACKETS	2 /* must set! */
#define I40E_AQ_SET_P_PARAMS_DOUBLE_VLAN_ENA	4
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
#define I40E_AQ_RESOURCE_TYPE_VEB		0x0
#define I40E_AQ_RESOURCE_TYPE_VSI		0x1
#define I40E_AQ_RESOURCE_TYPE_MACADDR		0x2
#define I40E_AQ_RESOURCE_TYPE_STAG		0x3
#define I40E_AQ_RESOURCE_TYPE_ETAG		0x4
#define I40E_AQ_RESOURCE_TYPE_MULTICAST_HASH	0x5
#define I40E_AQ_RESOURCE_TYPE_UNICAST_HASH	0x6
#define I40E_AQ_RESOURCE_TYPE_VLAN		0x7
#define I40E_AQ_RESOURCE_TYPE_VSI_LIST_ENTRY	0x8
#define I40E_AQ_RESOURCE_TYPE_ETAG_LIST_ENTRY	0x9
#define I40E_AQ_RESOURCE_TYPE_VLAN_STAT_POOL	0xA
#define I40E_AQ_RESOURCE_TYPE_MIRROR_RULE	0xB
#define I40E_AQ_RESOURCE_TYPE_QUEUE_SETS	0xC
#define I40E_AQ_RESOURCE_TYPE_VLAN_FILTERS	0xD
#define I40E_AQ_RESOURCE_TYPE_INNER_MAC_FILTERS	0xF
#define I40E_AQ_RESOURCE_TYPE_IP_FILTERS	0x10
#define I40E_AQ_RESOURCE_TYPE_GRE_VN_KEYS	0x11
#define I40E_AQ_RESOURCE_TYPE_VN2_KEYS		0x12
#define I40E_AQ_RESOURCE_TYPE_TUNNEL_PORTS	0x13
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
#define I40E_AQ_SET_SWITCH_CFG_PROMISC		0x0001
#define I40E_AQ_SET_SWITCH_CFG_L2_FILTER	0x0002
	__le16	valid_flags;
	u8	reserved[12];
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
#define I40E_AQ_VSI_CONN_TYPE_DEFAULT	0x2
#define I40E_AQ_VSI_CONN_TYPE_CASCADED	0x3
	u8	reserved1;
	u8	vf_id;
	u8	reserved2;
	__le16	vsi_flags;
#define I40E_AQ_VSI_TYPE_SHIFT		0x0
#define I40E_AQ_VSI_TYPE_MASK		(0x3 << I40E_AQ_VSI_TYPE_SHIFT)
#define I40E_AQ_VSI_TYPE_VF		0x0
#define I40E_AQ_VSI_TYPE_VMDQ2		0x1
#define I40E_AQ_VSI_TYPE_PF		0x2
#define I40E_AQ_VSI_TYPE_EMP_MNG	0x3
#define I40E_AQ_VSI_FLAG_CASCADED_PV	0x4
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

/* Add Port Virtualizer (direct 0x0220)
 * also used for update PV (direct 0x0221) but only flags are used
 * (IS_CTRL_PORT only works on add PV)
 */
struct i40e_aqc_add_update_pv {
	__le16	command_flags;
#define I40E_AQC_PV_FLAG_PV_TYPE		0x1
#define I40E_AQC_PV_FLAG_FWD_UNKNOWN_STAG_EN	0x2
#define I40E_AQC_PV_FLAG_FWD_UNKNOWN_ETAG_EN	0x4
#define I40E_AQC_PV_FLAG_IS_CTRL_PORT		0x8
	__le16	uplink_seid;
	__le16	connected_seid;
	u8	reserved[10];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_update_pv);

struct i40e_aqc_add_update_pv_completion {
	/* reserved for update; for add also encodes error if rc == ENOSPC */
	__le16	pv_seid;
#define I40E_AQC_PV_ERR_FLAG_NO_PV	0x1
#define I40E_AQC_PV_ERR_FLAG_NO_SCHED	0x2
#define I40E_AQC_PV_ERR_FLAG_NO_COUNTER	0x4
#define I40E_AQC_PV_ERR_FLAG_NO_ENTRY	0x8
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
#define I40E_AQC_GET_PV_PV_TYPE			0x1
#define I40E_AQC_GET_PV_FRWD_UNKNOWN_STAG	0x2
#define I40E_AQC_GET_PV_FRWD_UNKNOWN_ETAG	0x4
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
#define I40E_AQC_ADD_VEB_PORT_TYPE_SHIFT	1
#define I40E_AQC_ADD_VEB_PORT_TYPE_MASK		(0x3 << \
					I40E_AQC_ADD_VEB_PORT_TYPE_SHIFT)
#define I40E_AQC_ADD_VEB_PORT_TYPE_DEFAULT	0x2
#define I40E_AQC_ADD_VEB_PORT_TYPE_DATA		0x4
#define I40E_AQC_ADD_VEB_ENABLE_L2_FILTER	0x8     /* deprecated */
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
#define I40E_AQC_VEB_ERR_FLAG_NO_VEB		0x1
#define I40E_AQC_VEB_ERR_FLAG_NO_SCHED		0x2
#define I40E_AQC_VEB_ERR_FLAG_NO_COUNTER	0x4
#define I40E_AQC_VEB_ERR_FLAG_NO_ENTRY		0x8
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
#define I40E_AQC_MACVLAN_CMD_SEID_NUM_SHIFT	0
#define I40E_AQC_MACVLAN_CMD_SEID_NUM_MASK	(0x3FF << \
					I40E_AQC_MACVLAN_CMD_SEID_NUM_SHIFT)
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
#define I40E_AQC_MACVLAN_ADD_HASH_MATCH		0x0002
#define I40E_AQC_MACVLAN_ADD_IGNORE_VLAN	0x0004
#define I40E_AQC_MACVLAN_ADD_TO_QUEUE		0x0008
#define I40E_AQC_MACVLAN_ADD_USE_SHARED_MAC	0x0010
	__le16	queue_number;
#define I40E_AQC_MACVLAN_CMD_QUEUE_SHIFT	0
#define I40E_AQC_MACVLAN_CMD_QUEUE_MASK		(0x7FF << \
					I40E_AQC_MACVLAN_CMD_SEID_NUM_SHIFT)
	/* response section */
	u8	match_method;
#define I40E_AQC_MM_PERFECT_MATCH	0x01
#define I40E_AQC_MM_HASH_MATCH		0x02
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
#define I40E_AQC_MACVLAN_DEL_HASH_MATCH		0x02
#define I40E_AQC_MACVLAN_DEL_IGNORE_VLAN	0x08
#define I40E_AQC_MACVLAN_DEL_ALL_VSIS		0x10
	u8	reserved[3];
	/* reply section */
	u8	error_code;
#define I40E_AQC_REMOVE_MACVLAN_SUCCESS		0x0
#define I40E_AQC_REMOVE_MACVLAN_FAIL		0xFF
	u8	reply_reserved[3];
};

/* Add VLAN (indirect 0x0252)
 * Remove VLAN (indirect 0x0253)
 * use the generic i40e_aqc_macvlan for the command
 */
struct i40e_aqc_add_remove_vlan_element_data {
	__le16	vlan_tag;
	u8	vlan_flags;
/* flags for add VLAN */
#define I40E_AQC_ADD_VLAN_LOCAL			0x1
#define I40E_AQC_ADD_PVLAN_TYPE_SHIFT		1
#define I40E_AQC_ADD_PVLAN_TYPE_MASK	(0x3 << I40E_AQC_ADD_PVLAN_TYPE_SHIFT)
#define I40E_AQC_ADD_PVLAN_TYPE_REGULAR		0x0
#define I40E_AQC_ADD_PVLAN_TYPE_PRIMARY		0x2
#define I40E_AQC_ADD_PVLAN_TYPE_SECONDARY	0x4
#define I40E_AQC_VLAN_PTYPE_SHIFT		3
#define I40E_AQC_VLAN_PTYPE_MASK	(0x3 << I40E_AQC_VLAN_PTYPE_SHIFT)
#define I40E_AQC_VLAN_PTYPE_REGULAR_VSI		0x0
#define I40E_AQC_VLAN_PTYPE_PROMISC_VSI		0x8
#define I40E_AQC_VLAN_PTYPE_COMMUNITY_VSI	0x10
#define I40E_AQC_VLAN_PTYPE_ISOLATED_VSI	0x18
/* flags for remove VLAN */
#define I40E_AQC_REMOVE_VLAN_ALL	0x1
	u8	reserved;
	u8	result;
/* flags for add VLAN */
#define I40E_AQC_ADD_VLAN_SUCCESS	0x0
#define I40E_AQC_ADD_VLAN_FAIL_REQUEST	0xFE
#define I40E_AQC_ADD_VLAN_FAIL_RESOURCE	0xFF
/* flags for remove VLAN */
#define I40E_AQC_REMOVE_VLAN_SUCCESS	0x0
#define I40E_AQC_REMOVE_VLAN_FAIL	0xFF
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
#define I40E_AQC_SET_VSI_PROMISC_TX		0x8000
	__le16	seid;
#define I40E_AQC_VSI_PROM_CMD_SEID_MASK		0x3FF
	__le16	vlan_tag;
#define I40E_AQC_SET_VSI_VLAN_MASK		0x0FFF
#define I40E_AQC_SET_VSI_VLAN_VALID		0x8000
	u8	reserved[8];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_set_vsi_promiscuous_modes);

/* Add S/E-tag command (direct 0x0255)
 * Uses generic i40e_aqc_add_remove_tag_completion for completion
 */
struct i40e_aqc_add_tag {
	__le16	flags;
#define I40E_AQC_ADD_TAG_FLAG_TO_QUEUE		0x0001
	__le16	seid;
#define I40E_AQC_ADD_TAG_CMD_SEID_NUM_SHIFT	0
#define I40E_AQC_ADD_TAG_CMD_SEID_NUM_MASK	(0x3FF << \
					I40E_AQC_ADD_TAG_CMD_SEID_NUM_SHIFT)
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
#define I40E_AQC_REMOVE_TAG_CMD_SEID_NUM_SHIFT	0
#define I40E_AQC_REMOVE_TAG_CMD_SEID_NUM_MASK	(0x3FF << \
					I40E_AQC_REMOVE_TAG_CMD_SEID_NUM_SHIFT)
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
#define I40E_AQC_UPDATE_TAG_CMD_SEID_NUM_SHIFT	0
#define I40E_AQC_UPDATE_TAG_CMD_SEID_NUM_MASK	(0x3FF << \
					I40E_AQC_UPDATE_TAG_CMD_SEID_NUM_SHIFT)
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
#define I40E_AQC_ADD_CONTROL_PACKET_FLAGS_TO_QUEUE	0x0004
#define I40E_AQC_ADD_CONTROL_PACKET_FLAGS_TX		0x0008
#define I40E_AQC_ADD_CONTROL_PACKET_FLAGS_RX		0x0000
	__le16	seid;
#define I40E_AQC_ADD_CONTROL_PACKET_CMD_SEID_NUM_SHIFT	0
#define I40E_AQC_ADD_CONTROL_PACKET_CMD_SEID_NUM_MASK	(0x3FF << \
				I40E_AQC_ADD_CONTROL_PACKET_CMD_SEID_NUM_SHIFT)
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
#define I40E_AQC_ADD_CLOUD_CMD_SEID_NUM_SHIFT	0
#define I40E_AQC_ADD_CLOUD_CMD_SEID_NUM_MASK	(0x3FF << \
					I40E_AQC_ADD_CLOUD_CMD_SEID_NUM_SHIFT)
	u8	reserved2[4];
	__le32	addr_high;
	__le32	addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_remove_cloud_filters);

struct i40e_aqc_add_remove_cloud_filters_element_data {
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
	} ipaddr;
	__le16	flags;
#define I40E_AQC_ADD_CLOUD_FILTER_SHIFT			0
#define I40E_AQC_ADD_CLOUD_FILTER_MASK	(0x3F << \
					I40E_AQC_ADD_CLOUD_FILTER_SHIFT)
/* 0x0000 reserved */
#define I40E_AQC_ADD_CLOUD_FILTER_OIP			0x0001
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

#define I40E_AQC_ADD_CLOUD_FLAGS_TO_QUEUE		0x0080
#define I40E_AQC_ADD_CLOUD_VNK_SHIFT			6
#define I40E_AQC_ADD_CLOUD_VNK_MASK			0x00C0
#define I40E_AQC_ADD_CLOUD_FLAGS_IPV4			0
#define I40E_AQC_ADD_CLOUD_FLAGS_IPV6			0x0100

#define I40E_AQC_ADD_CLOUD_TNL_TYPE_SHIFT		9
#define I40E_AQC_ADD_CLOUD_TNL_TYPE_MASK		0x1E00
#define I40E_AQC_ADD_CLOUD_TNL_TYPE_VXLAN		0
#define I40E_AQC_ADD_CLOUD_TNL_TYPE_NVGRE_OMAC		1
#define I40E_AQC_ADD_CLOUD_TNL_TYPE_GENEVE		2
#define I40E_AQC_ADD_CLOUD_TNL_TYPE_IP			3
#define I40E_AQC_ADD_CLOUD_TNL_TYPE_RESERVED		4
#define I40E_AQC_ADD_CLOUD_TNL_TYPE_VXLAN_GPE		5

#define I40E_AQC_ADD_CLOUD_FLAGS_SHARED_OUTER_MAC	0x2000
#define I40E_AQC_ADD_CLOUD_FLAGS_SHARED_INNER_MAC	0x4000
#define I40E_AQC_ADD_CLOUD_FLAGS_SHARED_OUTER_IP	0x8000

	__le32	tenant_id;
	u8	reserved[4];
	__le16	queue_number;
#define I40E_AQC_ADD_CLOUD_QUEUE_SHIFT		0
#define I40E_AQC_ADD_CLOUD_QUEUE_MASK		(0x7FF << \
						 I40E_AQC_ADD_CLOUD_QUEUE_SHIFT)
	u8	reserved2[14];
	/* response section */
	u8	allocation_result;
#define I40E_AQC_ADD_CLOUD_FILTER_SUCCESS	0x0
#define I40E_AQC_ADD_CLOUD_FILTER_FAIL		0xFF
	u8	response_reserved[7];
};

struct i40e_aqc_remove_cloud_filters_completion {
	__le16 perfect_ovlan_used;
	__le16 perfect_ovlan_free;
	__le16 vlan_used;
	__le16 vlan_free;
	__le32 addr_high;
	__le32 addr_low;
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_remove_cloud_filters_completion);

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
#define I40E_AQC_MIRROR_RULE_TYPE_VPORT_INGRESS	1
#define I40E_AQC_MIRROR_RULE_TYPE_VPORT_EGRESS	2
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

/* DCB 0x03xx*/

/* PFC Ignore (direct 0x0301)
 *    the command and response use the same descriptor structure
 */
struct i40e_aqc_pfc_ignore {
	u8	tc_bitmap;
	u8	command_flags; /* unused on response */
#define I40E_AQC_PFC_IGNORE_SET		0x80
#define I40E_AQC_PFC_IGNORE_CLEAR	0x0
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
#define I40E_AQ_ETS_SEEPAGE_EN_MASK	0x1
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
	I40E_PHY_TYPE_MAX
};

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
#define I40E_AQ_PHY_FLAG_LOW_POWER	0x04
#define I40E_AQ_PHY_LINK_ENABLED	0x08
#define I40E_AQ_PHY_AN_ENABLED		0x10
#define I40E_AQ_PHY_FLAG_MODULE_QUAL	0x20
#define I40E_AQ_PHY_FEC_ABILITY_KR	0x40
#define I40E_AQ_PHY_FEC_ABILITY_RS	0x80
	__le16	eee_capability;
#define I40E_AQ_EEE_100BASE_TX		0x0002
#define I40E_AQ_EEE_1000BASE_T		0x0004
#define I40E_AQ_EEE_10GBASE_T		0x0008
#define I40E_AQ_EEE_1000BASE_KX		0x0010
#define I40E_AQ_EEE_10GBASE_KX4		0x0020
#define I40E_AQ_EEE_10GBASE_KR		0x0040
	__le32	eeer_val;
	u8	d3_lpan;
#define I40E_AQ_SET_PHY_D3_LPAN_ENA	0x01
	u8	phy_type_ext;
#define I40E_AQ_PHY_TYPE_EXT_25G_KR	0X01
#define I40E_AQ_PHY_TYPE_EXT_25G_CR	0X02
#define I40E_AQ_PHY_TYPE_EXT_25G_SR	0x04
#define I40E_AQ_PHY_TYPE_EXT_25G_LR	0x08
	u8	fec_cfg_curr_mod_ext_info;
#define I40E_AQ_ENABLE_FEC_KR		0x01
#define I40E_AQ_ENABLE_FEC_RS		0x02
#define I40E_AQ_REQUEST_FEC_KR		0x04
#define I40E_AQ_REQUEST_FEC_RS		0x08
#define I40E_AQ_ENABLE_FEC_AUTO		0x10
#define I40E_AQ_FEC
#define I40E_AQ_MODULE_TYPE_EXT_MASK	0xE0
#define I40E_AQ_MODULE_TYPE_EXT_SHIFT	5

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
#define I40E_AQ_SET_MAC_CONFIG_CRC_EN		0x04
#define I40E_AQ_SET_MAC_CONFIG_PACING_MASK	0x78
#define I40E_AQ_SET_MAC_CONFIG_PACING_SHIFT	3
#define I40E_AQ_SET_MAC_CONFIG_PACING_NONE	0x0
#define I40E_AQ_SET_MAC_CONFIG_PACING_1B_13TX	0xF
#define I40E_AQ_SET_MAC_CONFIG_PACING_1DW_9TX	0x9
#define I40E_AQ_SET_MAC_CONFIG_PACING_1DW_4TX	0x8
#define I40E_AQ_SET_MAC_CONFIG_PACING_3DW_7TX	0x7
#define I40E_AQ_SET_MAC_CONFIG_PACING_2DW_3TX	0x6
#define I40E_AQ_SET_MAC_CONFIG_PACING_1DW_1TX	0x5
#define I40E_AQ_SET_MAC_CONFIG_PACING_3DW_2TX	0x4
#define I40E_AQ_SET_MAC_CONFIG_PACING_7DW_3TX	0x3
#define I40E_AQ_SET_MAC_CONFIG_PACING_4DW_1TX	0x2
#define I40E_AQ_SET_MAC_CONFIG_PACING_9DW_1TX	0x1
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
#define I40E_AQ_LSE_MASK		0x3
#define I40E_AQ_LSE_NOP			0x0
#define I40E_AQ_LSE_DISABLE		0x2
#define I40E_AQ_LSE_ENABLE		0x3
/* only response uses this flag */
#define I40E_AQ_LSE_IS_ENABLED		0x1
	u8	phy_type;    /* i40e_aq_phy_type   */
	u8	link_speed;  /* i40e_aq_link_speed */
	u8	link_info;
#define I40E_AQ_LINK_UP			0x01    /* obsolete */
#define I40E_AQ_LINK_UP_FUNCTION	0x01
#define I40E_AQ_LINK_FAULT		0x02
#define I40E_AQ_LINK_FAULT_TX		0x04
#define I40E_AQ_LINK_FAULT_RX		0x08
#define I40E_AQ_LINK_FAULT_REMOTE	0x10
#define I40E_AQ_LINK_UP_PORT		0x20
#define I40E_AQ_MEDIA_AVAILABLE		0x40
#define I40E_AQ_SIGNAL_DETECT		0x80
	u8	an_info;
#define I40E_AQ_AN_COMPLETED		0x01
#define I40E_AQ_LP_AN_ABILITY		0x02
#define I40E_AQ_PD_FAULT		0x04
#define I40E_AQ_FEC_EN			0x08
#define I40E_AQ_PHY_LOW_POWER		0x10
#define I40E_AQ_LINK_PAUSE_TX		0x20
#define I40E_AQ_LINK_PAUSE_RX		0x40
#define I40E_AQ_QUALIFIED_MODULE	0x80
	u8	ext_info;
#define I40E_AQ_LINK_PHY_TEMP_ALARM	0x01
#define I40E_AQ_LINK_XCESSIVE_ERRORS	0x02
#define I40E_AQ_LINK_TX_SHIFT		0x02
#define I40E_AQ_LINK_TX_MASK		(0x03 << I40E_AQ_LINK_TX_SHIFT)
#define I40E_AQ_LINK_TX_ACTIVE		0x00
#define I40E_AQ_LINK_TX_DRAINED		0x01
#define I40E_AQ_LINK_TX_FLUSHED		0x03
#define I40E_AQ_LINK_FORCED_40G		0x10
/* 25G Error Codes */
#define I40E_AQ_25G_NO_ERR		0X00
#define I40E_AQ_25G_NOT_PRESENT		0X01
#define I40E_AQ_25G_NVM_CRC_ERR		0X02
#define I40E_AQ_25G_SBUS_UCODE_ERR	0X03
#define I40E_AQ_25G_SERDES_UCODE_ERR	0X04
#define I40E_AQ_25G_NIMB_UCODE_ERR	0X05
	u8	loopback; /* use defines from i40e_aqc_set_lb_mode */
	__le16	max_frame_size;
	u8	config;
#define I40E_AQ_CONFIG_FEC_KR_ENA	0x01
#define I40E_AQ_CONFIG_FEC_RS_ENA	0x02
#define I40E_AQ_CONFIG_CRC_ENA		0x04
#define I40E_AQ_CONFIG_PACING_MASK	0x78
	u8	external_power_ability;
#define I40E_AQ_LINK_POWER_CLASS_1	0x00
#define I40E_AQ_LINK_POWER_CLASS_2	0x01
#define I40E_AQ_LINK_POWER_CLASS_3	0x02
#define I40E_AQ_LINK_POWER_CLASS_4	0x03
	u8	reserved[4];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_get_link_status);

/* Set event mask command (direct 0x613) */
struct i40e_aqc_set_phy_int_mask {
	u8	reserved[8];
	__le16	event_mask;
#define I40E_AQ_EVENT_LINK_UPDOWN	0x0002
#define I40E_AQ_EVENT_MEDIA_NA		0x0004
#define I40E_AQ_EVENT_LINK_FAULT	0x0008
#define I40E_AQ_EVENT_PHY_TEMP_ALARM	0x0010
#define I40E_AQ_EVENT_EXCESSIVE_ERRORS	0x0020
#define I40E_AQ_EVENT_SIGNAL_DETECT	0x0040
#define I40E_AQ_EVENT_AN_COMPLETED	0x0080
#define I40E_AQ_EVENT_MODULE_QUAL_FAIL	0x0100
#define I40E_AQ_EVENT_PORT_TX_SUSPENDED	0x0200
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
#define I40E_AQ_PHY_DEBUG_RESET_INTERNAL	0x02
#define I40E_AQ_PHY_DEBUG_RESET_EXTERNAL_SHIFT	2
#define I40E_AQ_PHY_DEBUG_RESET_EXTERNAL_MASK	(0x03 << \
					I40E_AQ_PHY_DEBUG_RESET_EXTERNAL_SHIFT)
#define I40E_AQ_PHY_DEBUG_RESET_EXTERNAL_NONE	0x00
#define I40E_AQ_PHY_DEBUG_RESET_EXTERNAL_HARD	0x01
#define I40E_AQ_PHY_DEBUG_RESET_EXTERNAL_SOFT	0x02
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

/* NVM Read command (indirect 0x0701)
 * NVM Erase commands (direct 0x0702)
 * NVM Update commands (indirect 0x0703)
 */
struct i40e_aqc_nvm_update {
	u8	command_flags;
#define I40E_AQ_NVM_LAST_CMD	0x01
#define I40E_AQ_NVM_FLASH_ONLY	0x80
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
#define I40E_AQ_ANVM_SINGLE_OR_MULTIPLE_FEATURES_MASK	1 
#define I40E_AQ_ANVM_READ_SINGLE_FEATURE		0 
#define I40E_AQ_ANVM_READ_MULTIPLE_FEATURES		1
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
#define I40E_AQ_ANVM_FEATURE_OR_IMMEDIATE_SHIFT		1
#define I40E_AQ_ANVM_FEATURE_OR_IMMEDIATE_MASK \
				BIT(I40E_AQ_ANVM_FEATURE_OR_IMMEDIATE_SHIFT)
#define I40E_AQ_ANVM_FEATURE		0
#define I40E_AQ_ANVM_IMMEDIATE_FIELD	BIT(FEATURE_OR_IMMEDIATE_SHIFT)
struct i40e_aqc_nvm_config_data_feature {
	__le16 feature_id;
#define I40E_AQ_ANVM_FEATURE_OPTION_OEM_ONLY		0x01
#define I40E_AQ_ANVM_FEATURE_OPTION_DWORD_MAP		0x08
#define I40E_AQ_ANVM_FEATURE_OPTION_POR_CSR		0x10
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
#define I40E_AQ_NVM_OEM_POST_UPDATE_EXTERNAL_DATA	0x01
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
#define I40E_AQ_THERMAL_SENSOR_READ_CONFIG	0
#define I40E_AQ_THERMAL_SENSOR_SET_CONFIG	1
#define I40E_AQ_THERMAL_SENSOR_READ_TEMP	2
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
#define I40E_AQ_ALTERNATE_MODE_BIOS_MASK	1
#define I40E_AQ_ALTERNATE_MODE_BIOS_LEGACY	0
#define I40E_AQ_ALTERNATE_MODE_BIOS_UEFI	1
#define I40E_AQ_ALTERNATE_RESET_NEEDED		2
	u8	reserved[14];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_alternate_write_done);

/* Set OEM mode (direct 0x0905) */
struct i40e_aqc_alternate_set_mode {
	__le32	mode;
#define I40E_AQ_ALTERNATE_MODE_NONE	0
#define I40E_AQ_ALTERNATE_MODE_OEM	1
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
#define I40E_AQ_LLDP_MIB_LOCAL_AND_REMOTE	0x2
#define I40E_AQ_LLDP_BRIDGE_TYPE_MASK		0xC
#define I40E_AQ_LLDP_BRIDGE_TYPE_SHIFT		0x2
#define I40E_AQ_LLDP_BRIDGE_TYPE_NEAREST_BRIDGE	0x0
#define I40E_AQ_LLDP_BRIDGE_TYPE_NON_TPMR	0x1
#define I40E_AQ_LLDP_TX_SHIFT			0x4
#define I40E_AQ_LLDP_TX_MASK			(0x03 << I40E_AQ_LLDP_TX_SHIFT)
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
#define I40E_AQ_LLDP_MIB_UPDATE_ENABLE	0x0
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
#define I40E_AQ_LLDP_AGENT_STOP		0x0
#define I40E_AQ_LLDP_AGENT_SHUTDOWN	0x1
	u8	reserved[15];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_lldp_stop);

/* Start LLDP (direct 0x0A06) */

struct i40e_aqc_lldp_start {
	u8	command;
#define I40E_AQ_LLDP_AGENT_START	0x1
	u8	reserved[15];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_lldp_start);

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
#define SET_LOCAL_MIB_AC_TYPE_DCBX_MASK	BIT(SET_LOCAL_MIB_AC_TYPE_DCBX_SHIFT)
#define SET_LOCAL_MIB_AC_TYPE_LOCAL_MIB	0x0
#define SET_LOCAL_MIB_AC_TYPE_NON_WILLING_APPS_SHIFT	(1)
#define SET_LOCAL_MIB_AC_TYPE_NON_WILLING_APPS_MASK \
			BIT(SET_LOCAL_MIB_AC_TYPE_NON_WILLING_APPS_SHIFT)
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
#define I40E_AQC_START_SPECIFIC_AGENT_SHIFT	0
#define I40E_AQC_START_SPECIFIC_AGENT_MASK \
				BIT(I40E_AQC_START_SPECIFIC_AGENT_SHIFT)
	u8	command;
	u8	reserved[15];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_lldp_stop_start_specific_agent);

/* Add Udp Tunnel command and completion (direct 0x0B00) */
struct i40e_aqc_add_udp_tunnel {
	__le16	udp_port;
	u8	reserved0[3];
	u8	protocol_type;
#define I40E_AQC_TUNNEL_TYPE_VXLAN	0x00
#define I40E_AQC_TUNNEL_TYPE_NGE	0x01
#define I40E_AQC_TUNNEL_TYPE_TEREDO	0x10
#define I40E_AQC_TUNNEL_TYPE_VXLAN_GPE	0x11
	u8	reserved1[10];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_add_udp_tunnel);

struct i40e_aqc_add_udp_tunnel_completion {
	__le16	udp_port;
	u8	filter_entry_index;
	u8	multiple_pfs;
#define I40E_AQC_SINGLE_PF		0x0
#define I40E_AQC_MULTIPLE_PFS		0x1
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
#define I40E_AQC_TUNNEL_KEY_STRUCT_OVERRIDE	0x01
/* response flags */
#define I40E_AQC_TUNNEL_KEY_STRUCT_SUCCESS	0x01
#define I40E_AQC_TUNNEL_KEY_STRUCT_MODIFIED	0x02
#define I40E_AQC_TUNNEL_KEY_STRUCT_OVERRIDDEN	0x03
	u8	network_key_index;
#define I40E_AQC_NETWORK_KEY_INDEX_VXLAN		0x0
#define I40E_AQC_NETWORK_KEY_INDEX_NGE			0x1
#define I40E_AQC_NETWORK_KEY_INDEX_FLEX_MAC_IN_UDP	0x2
#define I40E_AQC_NETWORK_KEY_INDEX_GRE			0x3
	u8	reserved[10];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_tunnel_key_structure);

/* OEM mode commands (direct 0xFE0x) */
struct i40e_aqc_oem_param_change {
	__le32	param_type;
#define I40E_AQ_OEM_PARAM_TYPE_PF_CTL	0
#define I40E_AQ_OEM_PARAM_TYPE_BW_CTL	1
#define I40E_AQ_OEM_PARAM_MAC		2
	__le32	param_value1;
	__le16	param_value2;
	u8	reserved[6];
};

I40E_CHECK_CMD_LENGTH(i40e_aqc_oem_param_change);

struct i40e_aqc_oem_state_change {
	__le32	state;
#define I40E_AQ_OEM_STATE_LINK_DOWN	0x0
#define I40E_AQ_OEM_STATE_LINK_UP	0x1
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
#define I40E_AQ_TEST_PARTIAL	0
#define I40E_AQ_TEST_FULL	1
#define I40E_AQ_TEST_NVM	2
	u8	reserved[3];
	u8	command;
#define I40E_AQ_TEST_OPEN	0
#define I40E_AQ_TEST_CLOSE	1
#define I40E_AQ_TEST_INC	2
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

#define I40E_AQ_CLUSTER_ID_AUX		0
#define I40E_AQ_CLUSTER_ID_SWITCH_FLU	1
#define I40E_AQ_CLUSTER_ID_TXSCHED	2
#define I40E_AQ_CLUSTER_ID_HMC		3
#define I40E_AQ_CLUSTER_ID_MAC0		4
#define I40E_AQ_CLUSTER_ID_MAC1		5
#define I40E_AQ_CLUSTER_ID_MAC2		6
#define I40E_AQ_CLUSTER_ID_MAC3		7
#define I40E_AQ_CLUSTER_ID_DCB		8
#define I40E_AQ_CLUSTER_ID_EMP_MEM	9
#define I40E_AQ_CLUSTER_ID_PKT_BUF	10
#define I40E_AQ_CLUSTER_ID_ALTRAM	11

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
