/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
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

#ifndef _I40E_TYPE_H_
#define _I40E_TYPE_H_

#include "i40e_status.h"
#include "i40e_osdep.h"
#include "i40e_register.h"
#include "i40e_adminq.h"
#include "i40e_hmc.h"
#include "i40e_lan_hmc.h"

/* Device IDs */
#define I40E_DEV_ID_SFP_XL710		0x1572
#define I40E_DEV_ID_QEMU		0x1574
#define I40E_DEV_ID_KX_A		0x157F
#define I40E_DEV_ID_KX_B		0x1580
#define I40E_DEV_ID_KX_C		0x1581
#define I40E_DEV_ID_QSFP_A		0x1583
#define I40E_DEV_ID_QSFP_B		0x1584
#define I40E_DEV_ID_QSFP_C		0x1585
#define I40E_DEV_ID_VF			0x154C
#define I40E_DEV_ID_VF_HV		0x1571

#define i40e_is_40G_device(d)		((d) == I40E_DEV_ID_QSFP_A  || \
					 (d) == I40E_DEV_ID_QSFP_B  || \
					 (d) == I40E_DEV_ID_QSFP_C)

/* I40E_MASK is a macro used on 32 bit registers */
#define I40E_MASK(mask, shift) (mask << shift)

#define I40E_MAX_VSI_QP			16
#define I40E_MAX_VF_VSI			3
#define I40E_MAX_CHAINED_RX_BUFFERS	5
#define I40E_MAX_PF_UDP_OFFLOAD_PORTS	16

/* Max default timeout in ms, */
#define I40E_MAX_NVM_TIMEOUT		18000

/* Switch from ms to the 1usec global time (this is the GTIME resolution) */
#define I40E_MS_TO_GTIME(time)		((time) * 1000)

/* forward declaration */
struct i40e_hw;
typedef void (*I40E_ADMINQ_CALLBACK)(struct i40e_hw *, struct i40e_aq_desc *);

/* Data type manipulation macros. */

#define I40E_DESC_UNUSED(R)	\
	((((R)->next_to_clean > (R)->next_to_use) ? 0 : (R)->count) + \
	(R)->next_to_clean - (R)->next_to_use - 1)

/* bitfields for Tx queue mapping in QTX_CTL */
#define I40E_QTX_CTL_VF_QUEUE	0x0
#define I40E_QTX_CTL_VM_QUEUE	0x1
#define I40E_QTX_CTL_PF_QUEUE	0x2

/* debug masks - set these bits in hw->debug_mask to control output */
enum i40e_debug_mask {
	I40E_DEBUG_INIT			= 0x00000001,
	I40E_DEBUG_RELEASE		= 0x00000002,

	I40E_DEBUG_LINK			= 0x00000010,
	I40E_DEBUG_PHY			= 0x00000020,
	I40E_DEBUG_HMC			= 0x00000040,
	I40E_DEBUG_NVM			= 0x00000080,
	I40E_DEBUG_LAN			= 0x00000100,
	I40E_DEBUG_FLOW			= 0x00000200,
	I40E_DEBUG_DCB			= 0x00000400,
	I40E_DEBUG_DIAG			= 0x00000800,
	I40E_DEBUG_FD			= 0x00001000,

	I40E_DEBUG_AQ_MESSAGE		= 0x01000000,
	I40E_DEBUG_AQ_DESCRIPTOR	= 0x02000000,
	I40E_DEBUG_AQ_DESC_BUFFER	= 0x04000000,
	I40E_DEBUG_AQ_COMMAND		= 0x06000000,
	I40E_DEBUG_AQ			= 0x0F000000,

	I40E_DEBUG_USER			= 0xF0000000,

	I40E_DEBUG_ALL			= 0xFFFFFFFF
};

/* These are structs for managing the hardware information and the operations.
 * The structures of function pointers are filled out at init time when we
 * know for sure exactly which hardware we're working with.  This gives us the
 * flexibility of using the same main driver code but adapting to slightly
 * different hardware needs as new parts are developed.  For this architecture,
 * the Firmware and AdminQ are intended to insulate the driver from most of the
 * future changes, but these structures will also do part of the job.
 */
enum i40e_mac_type {
	I40E_MAC_UNKNOWN = 0,
	I40E_MAC_X710,
	I40E_MAC_XL710,
	I40E_MAC_VF,
	I40E_MAC_GENERIC,
};

enum i40e_media_type {
	I40E_MEDIA_TYPE_UNKNOWN = 0,
	I40E_MEDIA_TYPE_FIBER,
	I40E_MEDIA_TYPE_BASET,
	I40E_MEDIA_TYPE_BACKPLANE,
	I40E_MEDIA_TYPE_CX4,
	I40E_MEDIA_TYPE_DA,
	I40E_MEDIA_TYPE_VIRTUAL
};

enum i40e_fc_mode {
	I40E_FC_NONE = 0,
	I40E_FC_RX_PAUSE,
	I40E_FC_TX_PAUSE,
	I40E_FC_FULL,
	I40E_FC_PFC,
	I40E_FC_DEFAULT
};

enum i40e_vsi_type {
	I40E_VSI_MAIN = 0,
	I40E_VSI_VMDQ1,
	I40E_VSI_VMDQ2,
	I40E_VSI_CTRL,
	I40E_VSI_FCOE,
	I40E_VSI_MIRROR,
	I40E_VSI_SRIOV,
	I40E_VSI_FDIR,
	I40E_VSI_TYPE_UNKNOWN
};

enum i40e_queue_type {
	I40E_QUEUE_TYPE_RX = 0,
	I40E_QUEUE_TYPE_TX,
	I40E_QUEUE_TYPE_PE_CEQ,
	I40E_QUEUE_TYPE_UNKNOWN
};

struct i40e_link_status {
	enum i40e_aq_phy_type phy_type;
	enum i40e_aq_link_speed link_speed;
	u8 link_info;
	u8 an_info;
	u8 ext_info;
	u8 loopback;
	/* is Link Status Event notification to SW enabled */
	bool lse_enable;
	u16 max_frame_size;
	bool crc_enable;
	u8 pacing;
};

struct i40e_phy_info {
	struct i40e_link_status link_info;
	struct i40e_link_status link_info_old;
	u32 autoneg_advertised;
	u32 phy_id;
	u32 module_type;
	bool get_link_info;
	enum i40e_media_type media_type;
};

#define I40E_HW_CAP_MAX_GPIO			30
/* Capabilities of a PF or a VF or the whole device */
struct i40e_hw_capabilities {
	u32  switch_mode;
#define I40E_NVM_IMAGE_TYPE_EVB		0x0
#define I40E_NVM_IMAGE_TYPE_CLOUD	0x2
#define I40E_NVM_IMAGE_TYPE_UDP_CLOUD	0x3

	u32  management_mode;
	u32  npar_enable;
	u32  os2bmc;
	u32  valid_functions;
	bool sr_iov_1_1;
	bool vmdq;
	bool evb_802_1_qbg; /* Edge Virtual Bridging */
	bool evb_802_1_qbh; /* Bridge Port Extension */
	bool dcb;
	bool fcoe;
	bool mfp_mode_1;
	bool mgmt_cem;
	bool ieee_1588;
	bool iwarp;
	bool fd;
	u32 fd_filters_guaranteed;
	u32 fd_filters_best_effort;
	bool rss;
	u32 rss_table_size;
	u32 rss_table_entry_width;
	bool led[I40E_HW_CAP_MAX_GPIO];
	bool sdp[I40E_HW_CAP_MAX_GPIO];
	u32 nvm_image_type;
	u32 num_flow_director_filters;
	u32 num_vfs;
	u32 vf_base_id;
	u32 num_vsis;
	u32 num_rx_qp;
	u32 num_tx_qp;
	u32 base_queue;
	u32 num_msix_vectors;
	u32 num_msix_vectors_vf;
	u32 led_pin_num;
	u32 sdp_pin_num;
	u32 mdio_port_num;
	u32 mdio_port_mode;
	u8 rx_buf_chain_len;
	u32 enabled_tcmap;
	u32 maxtc;
};

struct i40e_mac_info {
	enum i40e_mac_type type;
	u8 addr[ETH_ALEN];
	u8 perm_addr[ETH_ALEN];
	u8 san_addr[ETH_ALEN];
	u16 max_fcoeq;
};

enum i40e_aq_resources_ids {
	I40E_NVM_RESOURCE_ID = 1
};

enum i40e_aq_resource_access_type {
	I40E_RESOURCE_READ = 1,
	I40E_RESOURCE_WRITE
};

struct i40e_nvm_info {
	u64 hw_semaphore_timeout; /* 2usec global time (GTIME resolution) */
	u64 hw_semaphore_wait;    /* - || - */
	u32 timeout;              /* [ms] */
	u16 sr_size;              /* Shadow RAM size in words */
	bool blank_nvm_mode;      /* is NVM empty (no FW present)*/
	u16 version;              /* NVM package version */
	u32 eetrack;              /* NVM data version */
};

/* PCI bus types */
enum i40e_bus_type {
	i40e_bus_type_unknown = 0,
	i40e_bus_type_pci,
	i40e_bus_type_pcix,
	i40e_bus_type_pci_express,
	i40e_bus_type_reserved
};

/* PCI bus speeds */
enum i40e_bus_speed {
	i40e_bus_speed_unknown	= 0,
	i40e_bus_speed_33	= 33,
	i40e_bus_speed_66	= 66,
	i40e_bus_speed_100	= 100,
	i40e_bus_speed_120	= 120,
	i40e_bus_speed_133	= 133,
	i40e_bus_speed_2500	= 2500,
	i40e_bus_speed_5000	= 5000,
	i40e_bus_speed_8000	= 8000,
	i40e_bus_speed_reserved
};

/* PCI bus widths */
enum i40e_bus_width {
	i40e_bus_width_unknown	= 0,
	i40e_bus_width_pcie_x1	= 1,
	i40e_bus_width_pcie_x2	= 2,
	i40e_bus_width_pcie_x4	= 4,
	i40e_bus_width_pcie_x8	= 8,
	i40e_bus_width_32	= 32,
	i40e_bus_width_64	= 64,
	i40e_bus_width_reserved
};

/* Bus parameters */
struct i40e_bus_info {
	enum i40e_bus_speed speed;
	enum i40e_bus_width width;
	enum i40e_bus_type type;

	u16 func;
	u16 device;
	u16 lan_id;
};

/* Flow control (FC) parameters */
struct i40e_fc_info {
	enum i40e_fc_mode current_mode; /* FC mode in effect */
	enum i40e_fc_mode requested_mode; /* FC mode requested by caller */
};

#define I40E_MAX_TRAFFIC_CLASS		8
#define I40E_MAX_USER_PRIORITY		8
#define I40E_DCBX_MAX_APPS		32
#define I40E_LLDPDU_SIZE		1500

/* IEEE 802.1Qaz ETS Configuration data */
struct i40e_ieee_ets_config {
	u8 willing;
	u8 cbs;
	u8 maxtcs;
	u8 prioritytable[I40E_MAX_TRAFFIC_CLASS];
	u8 tcbwtable[I40E_MAX_TRAFFIC_CLASS];
	u8 tsatable[I40E_MAX_TRAFFIC_CLASS];
};

/* IEEE 802.1Qaz ETS Recommendation data */
struct i40e_ieee_ets_recommend {
	u8 prioritytable[I40E_MAX_TRAFFIC_CLASS];
	u8 tcbwtable[I40E_MAX_TRAFFIC_CLASS];
	u8 tsatable[I40E_MAX_TRAFFIC_CLASS];
};

/* IEEE 802.1Qaz PFC Configuration data */
struct i40e_ieee_pfc_config {
	u8 willing;
	u8 mbc;
	u8 pfccap;
	u8 pfcenable;
};

/* IEEE 802.1Qaz Application Priority data */
struct i40e_ieee_app_priority_table {
	u8  priority;
	u8  selector;
	u16 protocolid;
};

struct i40e_dcbx_config {
	u32 numapps;
	struct i40e_ieee_ets_config etscfg;
	struct i40e_ieee_ets_recommend etsrec;
	struct i40e_ieee_pfc_config pfc;
	struct i40e_ieee_app_priority_table app[I40E_DCBX_MAX_APPS];
};

/* Port hardware description */
struct i40e_hw {
	u8 __iomem *hw_addr;
	void *back;

	/* function pointer structs */
	struct i40e_phy_info phy;
	struct i40e_mac_info mac;
	struct i40e_bus_info bus;
	struct i40e_nvm_info nvm;
	struct i40e_fc_info fc;

	/* pci info */
	u16 device_id;
	u16 vendor_id;
	u16 subsystem_device_id;
	u16 subsystem_vendor_id;
	u8 revision_id;
	u8 port;
	bool adapter_stopped;

	/* capabilities for entire device and PCI func */
	struct i40e_hw_capabilities dev_caps;
	struct i40e_hw_capabilities func_caps;

	/* Flow Director shared filter space */
	u16 fdir_shared_filter_count;

	/* device profile info */
	u8  pf_id;
	u16 main_vsi_seid;

	/* Closest numa node to the device */
	u16 numa_node;

	/* Admin Queue info */
	struct i40e_adminq_info aq;

	/* HMC info */
	struct i40e_hmc_info hmc; /* HMC info struct */

	/* LLDP/DCBX Status */
	u16 dcbx_status;

	/* DCBX info */
	struct i40e_dcbx_config local_dcbx_config;
	struct i40e_dcbx_config remote_dcbx_config;

	/* debug mask */
	u32 debug_mask;
};

struct i40e_driver_version {
	u8 major_version;
	u8 minor_version;
	u8 build_version;
	u8 subbuild_version;
	u8 driver_string[32];
};

/* RX Descriptors */
union i40e_16byte_rx_desc {
	struct {
		__le64 pkt_addr; /* Packet buffer address */
		__le64 hdr_addr; /* Header buffer address */
	} read;
	struct {
		struct {
			struct {
				union {
					__le16 mirroring_status;
					__le16 fcoe_ctx_id;
				} mirr_fcoe;
				__le16 l2tag1;
			} lo_dword;
			union {
				__le32 rss; /* RSS Hash */
				__le32 fd_id; /* Flow director filter id */
				__le32 fcoe_param; /* FCoE DDP Context id */
			} hi_dword;
		} qword0;
		struct {
			/* ext status/error/pktype/length */
			__le64 status_error_len;
		} qword1;
	} wb;  /* writeback */
};

union i40e_32byte_rx_desc {
	struct {
		__le64  pkt_addr; /* Packet buffer address */
		__le64  hdr_addr; /* Header buffer address */
			/* bit 0 of hdr_buffer_addr is DD bit */
		__le64  rsvd1;
		__le64  rsvd2;
	} read;
	struct {
		struct {
			struct {
				union {
					__le16 mirroring_status;
					__le16 fcoe_ctx_id;
				} mirr_fcoe;
				__le16 l2tag1;
			} lo_dword;
			union {
				__le32 rss; /* RSS Hash */
				__le32 fcoe_param; /* FCoE DDP Context id */
				/* Flow director filter id in case of
				 * Programming status desc WB
				 */
				__le32 fd_id;
			} hi_dword;
		} qword0;
		struct {
			/* status/error/pktype/length */
			__le64 status_error_len;
		} qword1;
		struct {
			__le16 ext_status; /* extended status */
			__le16 rsvd;
			__le16 l2tag2_1;
			__le16 l2tag2_2;
		} qword2;
		struct {
			union {
				__le32 flex_bytes_lo;
				__le32 pe_status;
			} lo_dword;
			union {
				__le32 flex_bytes_hi;
				__le32 fd_id;
			} hi_dword;
		} qword3;
	} wb;  /* writeback */
};

enum i40e_rx_desc_status_bits {
	/* Note: These are predefined bit offsets */
	I40E_RX_DESC_STATUS_DD_SHIFT		= 0,
	I40E_RX_DESC_STATUS_EOF_SHIFT		= 1,
	I40E_RX_DESC_STATUS_L2TAG1P_SHIFT	= 2,
	I40E_RX_DESC_STATUS_L3L4P_SHIFT		= 3,
	I40E_RX_DESC_STATUS_CRCP_SHIFT		= 4,
	I40E_RX_DESC_STATUS_TSYNINDX_SHIFT	= 5, /* 2 BITS */
	I40E_RX_DESC_STATUS_TSYNVALID_SHIFT	= 7,
	I40E_RX_DESC_STATUS_PIF_SHIFT		= 8,
	I40E_RX_DESC_STATUS_UMBCAST_SHIFT	= 9, /* 2 BITS */
	I40E_RX_DESC_STATUS_FLM_SHIFT		= 11,
	I40E_RX_DESC_STATUS_FLTSTAT_SHIFT	= 12, /* 2 BITS */
	I40E_RX_DESC_STATUS_LPBK_SHIFT		= 14,
	I40E_RX_DESC_STATUS_IPV6EXADD_SHIFT	= 15,
	I40E_RX_DESC_STATUS_RESERVED_SHIFT	= 16, /* 2 BITS */
	I40E_RX_DESC_STATUS_UDP_0_SHIFT		= 18,
	I40E_RX_DESC_STATUS_LAST /* this entry must be last!!! */
};

#define I40E_RXD_QW1_STATUS_SHIFT	0
#define I40E_RXD_QW1_STATUS_MASK	(((1 << I40E_RX_DESC_STATUS_LAST) - 1) \
					 << I40E_RXD_QW1_STATUS_SHIFT)

#define I40E_RXD_QW1_STATUS_TSYNINDX_SHIFT   I40E_RX_DESC_STATUS_TSYNINDX_SHIFT
#define I40E_RXD_QW1_STATUS_TSYNINDX_MASK	(0x3UL << \
					     I40E_RXD_QW1_STATUS_TSYNINDX_SHIFT)

#define I40E_RXD_QW1_STATUS_TSYNVALID_SHIFT  I40E_RX_DESC_STATUS_TSYNVALID_SHIFT
#define I40E_RXD_QW1_STATUS_TSYNVALID_MASK	(0x1UL << \
					 I40E_RXD_QW1_STATUS_TSYNVALID_SHIFT)

enum i40e_rx_desc_fltstat_values {
	I40E_RX_DESC_FLTSTAT_NO_DATA	= 0,
	I40E_RX_DESC_FLTSTAT_RSV_FD_ID	= 1, /* 16byte desc? FD_ID : RSV */
	I40E_RX_DESC_FLTSTAT_RSV	= 2,
	I40E_RX_DESC_FLTSTAT_RSS_HASH	= 3,
};

#define I40E_RXD_QW1_ERROR_SHIFT	19
#define I40E_RXD_QW1_ERROR_MASK		(0xFFUL << I40E_RXD_QW1_ERROR_SHIFT)

enum i40e_rx_desc_error_bits {
	/* Note: These are predefined bit offsets */
	I40E_RX_DESC_ERROR_RXE_SHIFT		= 0,
	I40E_RX_DESC_ERROR_RECIPE_SHIFT		= 1,
	I40E_RX_DESC_ERROR_HBO_SHIFT		= 2,
	I40E_RX_DESC_ERROR_L3L4E_SHIFT		= 3, /* 3 BITS */
	I40E_RX_DESC_ERROR_IPE_SHIFT		= 3,
	I40E_RX_DESC_ERROR_L4E_SHIFT		= 4,
	I40E_RX_DESC_ERROR_EIPE_SHIFT		= 5,
	I40E_RX_DESC_ERROR_OVERSIZE_SHIFT	= 6,
	I40E_RX_DESC_ERROR_PPRS_SHIFT		= 7
};

enum i40e_rx_desc_error_l3l4e_fcoe_masks {
	I40E_RX_DESC_ERROR_L3L4E_NONE		= 0,
	I40E_RX_DESC_ERROR_L3L4E_PROT		= 1,
	I40E_RX_DESC_ERROR_L3L4E_FC		= 2,
	I40E_RX_DESC_ERROR_L3L4E_DMAC_ERR	= 3,
	I40E_RX_DESC_ERROR_L3L4E_DMAC_WARN	= 4
};

#define I40E_RXD_QW1_PTYPE_SHIFT	30
#define I40E_RXD_QW1_PTYPE_MASK		(0xFFULL << I40E_RXD_QW1_PTYPE_SHIFT)

/* Packet type non-ip values */
enum i40e_rx_l2_ptype {
	I40E_RX_PTYPE_L2_RESERVED			= 0,
	I40E_RX_PTYPE_L2_MAC_PAY2			= 1,
	I40E_RX_PTYPE_L2_TIMESYNC_PAY2			= 2,
	I40E_RX_PTYPE_L2_FIP_PAY2			= 3,
	I40E_RX_PTYPE_L2_OUI_PAY2			= 4,
	I40E_RX_PTYPE_L2_MACCNTRL_PAY2			= 5,
	I40E_RX_PTYPE_L2_LLDP_PAY2			= 6,
	I40E_RX_PTYPE_L2_ECP_PAY2			= 7,
	I40E_RX_PTYPE_L2_EVB_PAY2			= 8,
	I40E_RX_PTYPE_L2_QCN_PAY2			= 9,
	I40E_RX_PTYPE_L2_EAPOL_PAY2			= 10,
	I40E_RX_PTYPE_L2_ARP				= 11,
	I40E_RX_PTYPE_L2_FCOE_PAY3			= 12,
	I40E_RX_PTYPE_L2_FCOE_FCDATA_PAY3		= 13,
	I40E_RX_PTYPE_L2_FCOE_FCRDY_PAY3		= 14,
	I40E_RX_PTYPE_L2_FCOE_FCRSP_PAY3		= 15,
	I40E_RX_PTYPE_L2_FCOE_FCOTHER_PA		= 16,
	I40E_RX_PTYPE_L2_FCOE_VFT_PAY3			= 17,
	I40E_RX_PTYPE_L2_FCOE_VFT_FCDATA		= 18,
	I40E_RX_PTYPE_L2_FCOE_VFT_FCRDY			= 19,
	I40E_RX_PTYPE_L2_FCOE_VFT_FCRSP			= 20,
	I40E_RX_PTYPE_L2_FCOE_VFT_FCOTHER		= 21,
	I40E_RX_PTYPE_GRENAT4_MAC_PAY3			= 58,
	I40E_RX_PTYPE_GRENAT4_MACVLAN_IPV6_ICMP_PAY4	= 87,
	I40E_RX_PTYPE_GRENAT6_MAC_PAY3			= 124,
	I40E_RX_PTYPE_GRENAT6_MACVLAN_IPV6_ICMP_PAY4	= 153
};

struct i40e_rx_ptype_decoded {
	u32 ptype:8;
	u32 known:1;
	u32 outer_ip:1;
	u32 outer_ip_ver:1;
	u32 outer_frag:1;
	u32 tunnel_type:3;
	u32 tunnel_end_prot:2;
	u32 tunnel_end_frag:1;
	u32 inner_prot:4;
	u32 payload_layer:3;
};

enum i40e_rx_ptype_outer_ip {
	I40E_RX_PTYPE_OUTER_L2	= 0,
	I40E_RX_PTYPE_OUTER_IP	= 1
};

enum i40e_rx_ptype_outer_ip_ver {
	I40E_RX_PTYPE_OUTER_NONE	= 0,
	I40E_RX_PTYPE_OUTER_IPV4	= 0,
	I40E_RX_PTYPE_OUTER_IPV6	= 1
};

enum i40e_rx_ptype_outer_fragmented {
	I40E_RX_PTYPE_NOT_FRAG	= 0,
	I40E_RX_PTYPE_FRAG	= 1
};

enum i40e_rx_ptype_tunnel_type {
	I40E_RX_PTYPE_TUNNEL_NONE		= 0,
	I40E_RX_PTYPE_TUNNEL_IP_IP		= 1,
	I40E_RX_PTYPE_TUNNEL_IP_GRENAT		= 2,
	I40E_RX_PTYPE_TUNNEL_IP_GRENAT_MAC	= 3,
	I40E_RX_PTYPE_TUNNEL_IP_GRENAT_MAC_VLAN	= 4,
};

enum i40e_rx_ptype_tunnel_end_prot {
	I40E_RX_PTYPE_TUNNEL_END_NONE	= 0,
	I40E_RX_PTYPE_TUNNEL_END_IPV4	= 1,
	I40E_RX_PTYPE_TUNNEL_END_IPV6	= 2,
};

enum i40e_rx_ptype_inner_prot {
	I40E_RX_PTYPE_INNER_PROT_NONE		= 0,
	I40E_RX_PTYPE_INNER_PROT_UDP		= 1,
	I40E_RX_PTYPE_INNER_PROT_TCP		= 2,
	I40E_RX_PTYPE_INNER_PROT_SCTP		= 3,
	I40E_RX_PTYPE_INNER_PROT_ICMP		= 4,
	I40E_RX_PTYPE_INNER_PROT_TIMESYNC	= 5
};

enum i40e_rx_ptype_payload_layer {
	I40E_RX_PTYPE_PAYLOAD_LAYER_NONE	= 0,
	I40E_RX_PTYPE_PAYLOAD_LAYER_PAY2	= 1,
	I40E_RX_PTYPE_PAYLOAD_LAYER_PAY3	= 2,
	I40E_RX_PTYPE_PAYLOAD_LAYER_PAY4	= 3,
};

#define I40E_RXD_QW1_LENGTH_PBUF_SHIFT	38
#define I40E_RXD_QW1_LENGTH_PBUF_MASK	(0x3FFFULL << \
					 I40E_RXD_QW1_LENGTH_PBUF_SHIFT)

#define I40E_RXD_QW1_LENGTH_HBUF_SHIFT	52
#define I40E_RXD_QW1_LENGTH_HBUF_MASK	(0x7FFULL << \
					 I40E_RXD_QW1_LENGTH_HBUF_SHIFT)

#define I40E_RXD_QW1_LENGTH_SPH_SHIFT	63
#define I40E_RXD_QW1_LENGTH_SPH_MASK	(0x1ULL << \
					 I40E_RXD_QW1_LENGTH_SPH_SHIFT)

enum i40e_rx_desc_ext_status_bits {
	/* Note: These are predefined bit offsets */
	I40E_RX_DESC_EXT_STATUS_L2TAG2P_SHIFT	= 0,
	I40E_RX_DESC_EXT_STATUS_L2TAG3P_SHIFT	= 1,
	I40E_RX_DESC_EXT_STATUS_FLEXBL_SHIFT	= 2, /* 2 BITS */
	I40E_RX_DESC_EXT_STATUS_FLEXBH_SHIFT	= 4, /* 2 BITS */
	I40E_RX_DESC_EXT_STATUS_FDLONGB_SHIFT	= 9,
	I40E_RX_DESC_EXT_STATUS_FCOELONGB_SHIFT	= 10,
	I40E_RX_DESC_EXT_STATUS_PELONGB_SHIFT	= 11,
};

enum i40e_rx_desc_pe_status_bits {
	/* Note: These are predefined bit offsets */
	I40E_RX_DESC_PE_STATUS_QPID_SHIFT	= 0, /* 18 BITS */
	I40E_RX_DESC_PE_STATUS_L4PORT_SHIFT	= 0, /* 16 BITS */
	I40E_RX_DESC_PE_STATUS_IPINDEX_SHIFT	= 16, /* 8 BITS */
	I40E_RX_DESC_PE_STATUS_QPIDHIT_SHIFT	= 24,
	I40E_RX_DESC_PE_STATUS_APBVTHIT_SHIFT	= 25,
	I40E_RX_DESC_PE_STATUS_PORTV_SHIFT	= 26,
	I40E_RX_DESC_PE_STATUS_URG_SHIFT	= 27,
	I40E_RX_DESC_PE_STATUS_IPFRAG_SHIFT	= 28,
	I40E_RX_DESC_PE_STATUS_IPOPT_SHIFT	= 29
};

#define I40E_RX_PROG_STATUS_DESC_LENGTH_SHIFT		38
#define I40E_RX_PROG_STATUS_DESC_LENGTH			0x2000000

#define I40E_RX_PROG_STATUS_DESC_QW1_PROGID_SHIFT	2
#define I40E_RX_PROG_STATUS_DESC_QW1_PROGID_MASK	(0x7UL << \
				I40E_RX_PROG_STATUS_DESC_QW1_PROGID_SHIFT)

#define I40E_RX_PROG_STATUS_DESC_QW1_ERROR_SHIFT	19
#define I40E_RX_PROG_STATUS_DESC_QW1_ERROR_MASK		(0x3FUL << \
				I40E_RX_PROG_STATUS_DESC_QW1_ERROR_SHIFT)

enum i40e_rx_prog_status_desc_status_bits {
	/* Note: These are predefined bit offsets */
	I40E_RX_PROG_STATUS_DESC_DD_SHIFT	= 0,
	I40E_RX_PROG_STATUS_DESC_PROG_ID_SHIFT	= 2 /* 3 BITS */
};

enum i40e_rx_prog_status_desc_prog_id_masks {
	I40E_RX_PROG_STATUS_DESC_FD_FILTER_STATUS	= 1,
	I40E_RX_PROG_STATUS_DESC_FCOE_CTXT_PROG_STATUS	= 2,
	I40E_RX_PROG_STATUS_DESC_FCOE_CTXT_INVL_STATUS	= 4,
};

enum i40e_rx_prog_status_desc_error_bits {
	/* Note: These are predefined bit offsets */
	I40E_RX_PROG_STATUS_DESC_FD_TBL_FULL_SHIFT	= 0,
	I40E_RX_PROG_STATUS_DESC_NO_FD_ENTRY_SHIFT	= 1,
	I40E_RX_PROG_STATUS_DESC_FCOE_TBL_FULL_SHIFT	= 2,
	I40E_RX_PROG_STATUS_DESC_FCOE_CONFLICT_SHIFT	= 3
};

/* TX Descriptor */
struct i40e_tx_desc {
	__le64 buffer_addr; /* Address of descriptor's data buf */
	__le64 cmd_type_offset_bsz;
};

#define I40E_TXD_QW1_DTYPE_SHIFT	0
#define I40E_TXD_QW1_DTYPE_MASK		(0xFUL << I40E_TXD_QW1_DTYPE_SHIFT)

enum i40e_tx_desc_dtype_value {
	I40E_TX_DESC_DTYPE_DATA		= 0x0,
	I40E_TX_DESC_DTYPE_NOP		= 0x1, /* same as Context desc */
	I40E_TX_DESC_DTYPE_CONTEXT	= 0x1,
	I40E_TX_DESC_DTYPE_FCOE_CTX	= 0x2,
	I40E_TX_DESC_DTYPE_FILTER_PROG	= 0x8,
	I40E_TX_DESC_DTYPE_DDP_CTX	= 0x9,
	I40E_TX_DESC_DTYPE_FLEX_DATA	= 0xB,
	I40E_TX_DESC_DTYPE_FLEX_CTX_1	= 0xC,
	I40E_TX_DESC_DTYPE_FLEX_CTX_2	= 0xD,
	I40E_TX_DESC_DTYPE_DESC_DONE	= 0xF
};

#define I40E_TXD_QW1_CMD_SHIFT	4
#define I40E_TXD_QW1_CMD_MASK	(0x3FFUL << I40E_TXD_QW1_CMD_SHIFT)

enum i40e_tx_desc_cmd_bits {
	I40E_TX_DESC_CMD_EOP			= 0x0001,
	I40E_TX_DESC_CMD_RS			= 0x0002,
	I40E_TX_DESC_CMD_ICRC			= 0x0004,
	I40E_TX_DESC_CMD_IL2TAG1		= 0x0008,
	I40E_TX_DESC_CMD_DUMMY			= 0x0010,
	I40E_TX_DESC_CMD_IIPT_NONIP		= 0x0000, /* 2 BITS */
	I40E_TX_DESC_CMD_IIPT_IPV6		= 0x0020, /* 2 BITS */
	I40E_TX_DESC_CMD_IIPT_IPV4		= 0x0040, /* 2 BITS */
	I40E_TX_DESC_CMD_IIPT_IPV4_CSUM		= 0x0060, /* 2 BITS */
	I40E_TX_DESC_CMD_FCOET			= 0x0080,
	I40E_TX_DESC_CMD_L4T_EOFT_UNK		= 0x0000, /* 2 BITS */
	I40E_TX_DESC_CMD_L4T_EOFT_TCP		= 0x0100, /* 2 BITS */
	I40E_TX_DESC_CMD_L4T_EOFT_SCTP		= 0x0200, /* 2 BITS */
	I40E_TX_DESC_CMD_L4T_EOFT_UDP		= 0x0300, /* 2 BITS */
	I40E_TX_DESC_CMD_L4T_EOFT_EOF_N		= 0x0000, /* 2 BITS */
	I40E_TX_DESC_CMD_L4T_EOFT_EOF_T		= 0x0100, /* 2 BITS */
	I40E_TX_DESC_CMD_L4T_EOFT_EOF_NI	= 0x0200, /* 2 BITS */
	I40E_TX_DESC_CMD_L4T_EOFT_EOF_A		= 0x0300, /* 2 BITS */
};

#define I40E_TXD_QW1_OFFSET_SHIFT	16
#define I40E_TXD_QW1_OFFSET_MASK	(0x3FFFFULL << \
					 I40E_TXD_QW1_OFFSET_SHIFT)

enum i40e_tx_desc_length_fields {
	/* Note: These are predefined bit offsets */
	I40E_TX_DESC_LENGTH_MACLEN_SHIFT	= 0, /* 7 BITS */
	I40E_TX_DESC_LENGTH_IPLEN_SHIFT		= 7, /* 7 BITS */
	I40E_TX_DESC_LENGTH_L4_FC_LEN_SHIFT	= 14 /* 4 BITS */
};

#define I40E_TXD_QW1_TX_BUF_SZ_SHIFT	34
#define I40E_TXD_QW1_TX_BUF_SZ_MASK	(0x3FFFULL << \
					 I40E_TXD_QW1_TX_BUF_SZ_SHIFT)

#define I40E_TXD_QW1_L2TAG1_SHIFT	48
#define I40E_TXD_QW1_L2TAG1_MASK	(0xFFFFULL << I40E_TXD_QW1_L2TAG1_SHIFT)

/* Context descriptors */
struct i40e_tx_context_desc {
	__le32 tunneling_params;
	__le16 l2tag2;
	__le16 rsvd;
	__le64 type_cmd_tso_mss;
};

#define I40E_TXD_CTX_QW1_DTYPE_SHIFT	0
#define I40E_TXD_CTX_QW1_DTYPE_MASK	(0xFUL << I40E_TXD_CTX_QW1_DTYPE_SHIFT)

#define I40E_TXD_CTX_QW1_CMD_SHIFT	4
#define I40E_TXD_CTX_QW1_CMD_MASK	(0xFFFFUL << I40E_TXD_CTX_QW1_CMD_SHIFT)

enum i40e_tx_ctx_desc_cmd_bits {
	I40E_TX_CTX_DESC_TSO		= 0x01,
	I40E_TX_CTX_DESC_TSYN		= 0x02,
	I40E_TX_CTX_DESC_IL2TAG2	= 0x04,
	I40E_TX_CTX_DESC_IL2TAG2_IL2H	= 0x08,
	I40E_TX_CTX_DESC_SWTCH_NOTAG	= 0x00,
	I40E_TX_CTX_DESC_SWTCH_UPLINK	= 0x10,
	I40E_TX_CTX_DESC_SWTCH_LOCAL	= 0x20,
	I40E_TX_CTX_DESC_SWTCH_VSI	= 0x30,
	I40E_TX_CTX_DESC_SWPE		= 0x40
};

#define I40E_TXD_CTX_QW1_TSO_LEN_SHIFT	30
#define I40E_TXD_CTX_QW1_TSO_LEN_MASK	(0x3FFFFULL << \
					 I40E_TXD_CTX_QW1_TSO_LEN_SHIFT)

#define I40E_TXD_CTX_QW1_MSS_SHIFT	50
#define I40E_TXD_CTX_QW1_MSS_MASK	(0x3FFFULL << \
					 I40E_TXD_CTX_QW1_MSS_SHIFT)

#define I40E_TXD_CTX_QW1_VSI_SHIFT	50
#define I40E_TXD_CTX_QW1_VSI_MASK	(0x1FFULL << I40E_TXD_CTX_QW1_VSI_SHIFT)

#define I40E_TXD_CTX_QW0_EXT_IP_SHIFT	0
#define I40E_TXD_CTX_QW0_EXT_IP_MASK	(0x3ULL << \
					 I40E_TXD_CTX_QW0_EXT_IP_SHIFT)

enum i40e_tx_ctx_desc_eipt_offload {
	I40E_TX_CTX_EXT_IP_NONE		= 0x0,
	I40E_TX_CTX_EXT_IP_IPV6		= 0x1,
	I40E_TX_CTX_EXT_IP_IPV4_NO_CSUM	= 0x2,
	I40E_TX_CTX_EXT_IP_IPV4		= 0x3
};

#define I40E_TXD_CTX_QW0_EXT_IPLEN_SHIFT	2
#define I40E_TXD_CTX_QW0_EXT_IPLEN_MASK	(0x3FULL << \
					 I40E_TXD_CTX_QW0_EXT_IPLEN_SHIFT)

#define I40E_TXD_CTX_QW0_NATT_SHIFT	9
#define I40E_TXD_CTX_QW0_NATT_MASK	(0x3ULL << I40E_TXD_CTX_QW0_NATT_SHIFT)

#define I40E_TXD_CTX_UDP_TUNNELING	(0x1ULL << I40E_TXD_CTX_QW0_NATT_SHIFT)
#define I40E_TXD_CTX_GRE_TUNNELING	(0x2ULL << I40E_TXD_CTX_QW0_NATT_SHIFT)

#define I40E_TXD_CTX_QW0_EIP_NOINC_SHIFT	11
#define I40E_TXD_CTX_QW0_EIP_NOINC_MASK	(0x1ULL << \
					 I40E_TXD_CTX_QW0_EIP_NOINC_SHIFT)

#define I40E_TXD_CTX_EIP_NOINC_IPID_CONST	I40E_TXD_CTX_QW0_EIP_NOINC_MASK

#define I40E_TXD_CTX_QW0_NATLEN_SHIFT	12
#define I40E_TXD_CTX_QW0_NATLEN_MASK	(0X7FULL << \
					 I40E_TXD_CTX_QW0_NATLEN_SHIFT)

#define I40E_TXD_CTX_QW0_DECTTL_SHIFT	19
#define I40E_TXD_CTX_QW0_DECTTL_MASK	(0xFULL << \
					 I40E_TXD_CTX_QW0_DECTTL_SHIFT)

struct i40e_filter_program_desc {
	__le32 qindex_flex_ptype_vsi;
	__le32 rsvd;
	__le32 dtype_cmd_cntindex;
	__le32 fd_id;
};
#define I40E_TXD_FLTR_QW0_QINDEX_SHIFT	0
#define I40E_TXD_FLTR_QW0_QINDEX_MASK	(0x7FFUL << \
					 I40E_TXD_FLTR_QW0_QINDEX_SHIFT)
#define I40E_TXD_FLTR_QW0_FLEXOFF_SHIFT	11
#define I40E_TXD_FLTR_QW0_FLEXOFF_MASK	(0x7UL << \
					 I40E_TXD_FLTR_QW0_FLEXOFF_SHIFT)
#define I40E_TXD_FLTR_QW0_PCTYPE_SHIFT	17
#define I40E_TXD_FLTR_QW0_PCTYPE_MASK	(0x3FUL << \
					 I40E_TXD_FLTR_QW0_PCTYPE_SHIFT)

/* Packet Classifier Types for filters */
enum i40e_filter_pctype {
	/* Note: Values 0-30 are reserved for future use */
	I40E_FILTER_PCTYPE_NONF_IPV4_UDP		= 31,
	/* Note: Value 32 is reserved for future use */
	I40E_FILTER_PCTYPE_NONF_IPV4_TCP		= 33,
	I40E_FILTER_PCTYPE_NONF_IPV4_SCTP		= 34,
	I40E_FILTER_PCTYPE_NONF_IPV4_OTHER		= 35,
	I40E_FILTER_PCTYPE_FRAG_IPV4			= 36,
	/* Note: Values 37-40 are reserved for future use */
	I40E_FILTER_PCTYPE_NONF_IPV6_UDP		= 41,
	I40E_FILTER_PCTYPE_NONF_IPV6_TCP		= 43,
	I40E_FILTER_PCTYPE_NONF_IPV6_SCTP		= 44,
	I40E_FILTER_PCTYPE_NONF_IPV6_OTHER		= 45,
	I40E_FILTER_PCTYPE_FRAG_IPV6			= 46,
	/* Note: Value 47 is reserved for future use */
	I40E_FILTER_PCTYPE_FCOE_OX			= 48,
	I40E_FILTER_PCTYPE_FCOE_RX			= 49,
	I40E_FILTER_PCTYPE_FCOE_OTHER			= 50,
	/* Note: Values 51-62 are reserved for future use */
	I40E_FILTER_PCTYPE_L2_PAYLOAD			= 63,
};

enum i40e_filter_program_desc_dest {
	I40E_FILTER_PROGRAM_DESC_DEST_DROP_PACKET		= 0x0,
	I40E_FILTER_PROGRAM_DESC_DEST_DIRECT_PACKET_QINDEX	= 0x1,
	I40E_FILTER_PROGRAM_DESC_DEST_DIRECT_PACKET_OTHER	= 0x2,
};

enum i40e_filter_program_desc_fd_status {
	I40E_FILTER_PROGRAM_DESC_FD_STATUS_NONE			= 0x0,
	I40E_FILTER_PROGRAM_DESC_FD_STATUS_FD_ID		= 0x1,
	I40E_FILTER_PROGRAM_DESC_FD_STATUS_FD_ID_4FLEX_BYTES	= 0x2,
	I40E_FILTER_PROGRAM_DESC_FD_STATUS_8FLEX_BYTES		= 0x3,
};

#define I40E_TXD_FLTR_QW0_DEST_VSI_SHIFT	23
#define I40E_TXD_FLTR_QW0_DEST_VSI_MASK	(0x1FFUL << \
					 I40E_TXD_FLTR_QW0_DEST_VSI_SHIFT)

#define I40E_TXD_FLTR_QW1_CMD_SHIFT	4
#define I40E_TXD_FLTR_QW1_CMD_MASK	(0xFFFFULL << \
					 I40E_TXD_FLTR_QW1_CMD_SHIFT)

#define I40E_TXD_FLTR_QW1_PCMD_SHIFT	(0x0ULL + I40E_TXD_FLTR_QW1_CMD_SHIFT)
#define I40E_TXD_FLTR_QW1_PCMD_MASK	(0x7ULL << I40E_TXD_FLTR_QW1_PCMD_SHIFT)

enum i40e_filter_program_desc_pcmd {
	I40E_FILTER_PROGRAM_DESC_PCMD_ADD_UPDATE	= 0x1,
	I40E_FILTER_PROGRAM_DESC_PCMD_REMOVE		= 0x2,
};

#define I40E_TXD_FLTR_QW1_DEST_SHIFT	(0x3ULL + I40E_TXD_FLTR_QW1_CMD_SHIFT)
#define I40E_TXD_FLTR_QW1_DEST_MASK	(0x3ULL << I40E_TXD_FLTR_QW1_DEST_SHIFT)

#define I40E_TXD_FLTR_QW1_CNT_ENA_SHIFT	(0x7ULL + I40E_TXD_FLTR_QW1_CMD_SHIFT)
#define I40E_TXD_FLTR_QW1_CNT_ENA_MASK	(0x1ULL << \
					 I40E_TXD_FLTR_QW1_CNT_ENA_SHIFT)

#define I40E_TXD_FLTR_QW1_FD_STATUS_SHIFT	(0x9ULL + \
						 I40E_TXD_FLTR_QW1_CMD_SHIFT)
#define I40E_TXD_FLTR_QW1_FD_STATUS_MASK (0x3ULL << \
					  I40E_TXD_FLTR_QW1_FD_STATUS_SHIFT)

#define I40E_TXD_FLTR_QW1_CNTINDEX_SHIFT 20
#define I40E_TXD_FLTR_QW1_CNTINDEX_MASK	(0x1FFUL << \
					 I40E_TXD_FLTR_QW1_CNTINDEX_SHIFT)

enum i40e_filter_type {
	I40E_FLOW_DIRECTOR_FLTR = 0,
	I40E_PE_QUAD_HASH_FLTR = 1,
	I40E_ETHERTYPE_FLTR,
	I40E_FCOE_CTX_FLTR,
	I40E_MAC_VLAN_FLTR,
	I40E_HASH_FLTR
};

struct i40e_vsi_context {
	u16 seid;
	u16 uplink_seid;
	u16 vsi_number;
	u16 vsis_allocated;
	u16 vsis_unallocated;
	u16 flags;
	u8 pf_num;
	u8 vf_num;
	u8 connection_type;
	struct i40e_aqc_vsi_properties_data info;
};

struct i40e_veb_context {
	u16 seid;
	u16 uplink_seid;
	u16 veb_number;
	u16 vebs_allocated;
	u16 vebs_unallocated;
	u16 flags;
	struct i40e_aqc_get_veb_parameters_completion info;
};

/* Statistics collected by each port, VSI, VEB, and S-channel */
struct i40e_eth_stats {
	u64 rx_bytes;			/* gorc */
	u64 rx_unicast;			/* uprc */
	u64 rx_multicast;		/* mprc */
	u64 rx_broadcast;		/* bprc */
	u64 rx_discards;		/* rdpc */
	u64 rx_unknown_protocol;	/* rupp */
	u64 tx_bytes;			/* gotc */
	u64 tx_unicast;			/* uptc */
	u64 tx_multicast;		/* mptc */
	u64 tx_broadcast;		/* bptc */
	u64 tx_discards;		/* tdpc */
	u64 tx_errors;			/* tepc */
};

/* Statistics collected by the MAC */
struct i40e_hw_port_stats {
	/* eth stats collected by the port */
	struct i40e_eth_stats eth;

	/* additional port specific stats */
	u64 tx_dropped_link_down;	/* tdold */
	u64 crc_errors;			/* crcerrs */
	u64 illegal_bytes;		/* illerrc */
	u64 error_bytes;		/* errbc */
	u64 mac_local_faults;		/* mlfc */
	u64 mac_remote_faults;		/* mrfc */
	u64 rx_length_errors;		/* rlec */
	u64 link_xon_rx;		/* lxonrxc */
	u64 link_xoff_rx;		/* lxoffrxc */
	u64 priority_xon_rx[8];		/* pxonrxc[8] */
	u64 priority_xoff_rx[8];	/* pxoffrxc[8] */
	u64 link_xon_tx;		/* lxontxc */
	u64 link_xoff_tx;		/* lxofftxc */
	u64 priority_xon_tx[8];		/* pxontxc[8] */
	u64 priority_xoff_tx[8];	/* pxofftxc[8] */
	u64 priority_xon_2_xoff[8];	/* pxon2offc[8] */
	u64 rx_size_64;			/* prc64 */
	u64 rx_size_127;		/* prc127 */
	u64 rx_size_255;		/* prc255 */
	u64 rx_size_511;		/* prc511 */
	u64 rx_size_1023;		/* prc1023 */
	u64 rx_size_1522;		/* prc1522 */
	u64 rx_size_big;		/* prc9522 */
	u64 rx_undersize;		/* ruc */
	u64 rx_fragments;		/* rfc */
	u64 rx_oversize;		/* roc */
	u64 rx_jabber;			/* rjc */
	u64 tx_size_64;			/* ptc64 */
	u64 tx_size_127;		/* ptc127 */
	u64 tx_size_255;		/* ptc255 */
	u64 tx_size_511;		/* ptc511 */
	u64 tx_size_1023;		/* ptc1023 */
	u64 tx_size_1522;		/* ptc1522 */
	u64 tx_size_big;		/* ptc9522 */
	u64 mac_short_packet_dropped;	/* mspdc */
	u64 checksum_error;		/* xec */
	/* flow director stats */
	u64 fd_atr_match;
	u64 fd_sb_match;
	/* EEE LPI */
	u32 tx_lpi_status;
	u32 rx_lpi_status;
	u64 tx_lpi_count;		/* etlpic */
	u64 rx_lpi_count;		/* erlpic */
};

/* Checksum and Shadow RAM pointers */
#define I40E_SR_NVM_CONTROL_WORD		0x00
#define I40E_SR_EMP_MODULE_PTR			0x0F
#define I40E_SR_NVM_IMAGE_VERSION		0x18
#define I40E_SR_NVM_WAKE_ON_LAN			0x19
#define I40E_SR_ALTERNATE_SAN_MAC_ADDRESS_PTR	0x27
#define I40E_SR_NVM_EETRACK_LO			0x2D
#define I40E_SR_NVM_EETRACK_HI			0x2E
#define I40E_SR_VPD_PTR				0x2F
#define I40E_SR_PCIE_ALT_AUTO_LOAD_PTR		0x3E
#define I40E_SR_SW_CHECKSUM_WORD		0x3F

/* Auxiliary field, mask and shift definition for Shadow RAM and NVM Flash */
#define I40E_SR_VPD_MODULE_MAX_SIZE		1024
#define I40E_SR_PCIE_ALT_MODULE_MAX_SIZE	1024
#define I40E_SR_CONTROL_WORD_1_SHIFT		0x06
#define I40E_SR_CONTROL_WORD_1_MASK	(0x03 << I40E_SR_CONTROL_WORD_1_SHIFT)

/* Shadow RAM related */
#define I40E_SR_SECTOR_SIZE_IN_WORDS	0x800
#define I40E_SR_WORDS_IN_1KB		512
/* Checksum should be calculated such that after adding all the words,
 * including the checksum word itself, the sum should be 0xBABA.
 */
#define I40E_SR_SW_CHECKSUM_BASE	0xBABA

#define I40E_SRRD_SRCTL_ATTEMPTS	100000

enum i40e_switch_element_types {
	I40E_SWITCH_ELEMENT_TYPE_MAC	= 1,
	I40E_SWITCH_ELEMENT_TYPE_PF	= 2,
	I40E_SWITCH_ELEMENT_TYPE_VF	= 3,
	I40E_SWITCH_ELEMENT_TYPE_EMP	= 4,
	I40E_SWITCH_ELEMENT_TYPE_BMC	= 6,
	I40E_SWITCH_ELEMENT_TYPE_PE	= 16,
	I40E_SWITCH_ELEMENT_TYPE_VEB	= 17,
	I40E_SWITCH_ELEMENT_TYPE_PA	= 18,
	I40E_SWITCH_ELEMENT_TYPE_VSI	= 19,
};

/* Supported EtherType filters */
enum i40e_ether_type_index {
	I40E_ETHER_TYPE_1588		= 0,
	I40E_ETHER_TYPE_FIP		= 1,
	I40E_ETHER_TYPE_OUI_EXTENDED	= 2,
	I40E_ETHER_TYPE_MAC_CONTROL	= 3,
	I40E_ETHER_TYPE_LLDP		= 4,
	I40E_ETHER_TYPE_EVB_PROTOCOL1	= 5,
	I40E_ETHER_TYPE_EVB_PROTOCOL2	= 6,
	I40E_ETHER_TYPE_QCN_CNM		= 7,
	I40E_ETHER_TYPE_8021X		= 8,
	I40E_ETHER_TYPE_ARP		= 9,
	I40E_ETHER_TYPE_RSV1		= 10,
	I40E_ETHER_TYPE_RSV2		= 11,
};

/* Filter context base size is 1K */
#define I40E_HASH_FILTER_BASE_SIZE	1024
/* Supported Hash filter values */
enum i40e_hash_filter_size {
	I40E_HASH_FILTER_SIZE_1K	= 0,
	I40E_HASH_FILTER_SIZE_2K	= 1,
	I40E_HASH_FILTER_SIZE_4K	= 2,
	I40E_HASH_FILTER_SIZE_8K	= 3,
	I40E_HASH_FILTER_SIZE_16K	= 4,
	I40E_HASH_FILTER_SIZE_32K	= 5,
	I40E_HASH_FILTER_SIZE_64K	= 6,
	I40E_HASH_FILTER_SIZE_128K	= 7,
	I40E_HASH_FILTER_SIZE_256K	= 8,
	I40E_HASH_FILTER_SIZE_512K	= 9,
	I40E_HASH_FILTER_SIZE_1M	= 10,
};

/* DMA context base size is 0.5K */
#define I40E_DMA_CNTX_BASE_SIZE		512
/* Supported DMA context values */
enum i40e_dma_cntx_size {
	I40E_DMA_CNTX_SIZE_512		= 0,
	I40E_DMA_CNTX_SIZE_1K		= 1,
	I40E_DMA_CNTX_SIZE_2K		= 2,
	I40E_DMA_CNTX_SIZE_4K		= 3,
	I40E_DMA_CNTX_SIZE_8K		= 4,
	I40E_DMA_CNTX_SIZE_16K		= 5,
	I40E_DMA_CNTX_SIZE_32K		= 6,
	I40E_DMA_CNTX_SIZE_64K		= 7,
	I40E_DMA_CNTX_SIZE_128K		= 8,
	I40E_DMA_CNTX_SIZE_256K		= 9,
};

/* Supported Hash look up table (LUT) sizes */
enum i40e_hash_lut_size {
	I40E_HASH_LUT_SIZE_128		= 0,
	I40E_HASH_LUT_SIZE_512		= 1,
};

/* Structure to hold a per PF filter control settings */
struct i40e_filter_control_settings {
	/* number of PE Quad Hash filter buckets */
	enum i40e_hash_filter_size pe_filt_num;
	/* number of PE Quad Hash contexts */
	enum i40e_dma_cntx_size pe_cntx_num;
	/* number of FCoE filter buckets */
	enum i40e_hash_filter_size fcoe_filt_num;
	/* number of FCoE DDP contexts */
	enum i40e_dma_cntx_size fcoe_cntx_num;
	/* size of the Hash LUT */
	enum i40e_hash_lut_size	hash_lut_size;
	/* enable FDIR filters for PF and its VFs */
	bool enable_fdir;
	/* enable Ethertype filters for PF and its VFs */
	bool enable_ethtype;
	/* enable MAC/VLAN filters for PF and its VFs */
	bool enable_macvlan;
};

/* Structure to hold device level control filter counts */
struct i40e_control_filter_stats {
	u16 mac_etype_used;   /* Used perfect match MAC/EtherType filters */
	u16 etype_used;       /* Used perfect EtherType filters */
	u16 mac_etype_free;   /* Un-used perfect match MAC/EtherType filters */
	u16 etype_free;       /* Un-used perfect EtherType filters */
};

enum i40e_reset_type {
	I40E_RESET_POR		= 0,
	I40E_RESET_CORER	= 1,
	I40E_RESET_GLOBR	= 2,
	I40E_RESET_EMPR		= 3,
};

/* RSS Hash Table Size */
#define I40E_PFQF_CTL_0_HASHLUTSIZE_512	0x00010000
#endif /* _I40E_TYPE_H_ */
