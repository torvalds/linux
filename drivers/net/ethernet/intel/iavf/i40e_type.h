/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2013 - 2018 Intel Corporation. */

#ifndef _I40E_TYPE_H_
#define _I40E_TYPE_H_

#include "i40e_status.h"
#include "i40e_osdep.h"
#include "i40e_register.h"
#include "i40e_adminq.h"
#include "i40e_devids.h"

#define I40E_RXQ_CTX_DBUFF_SHIFT 7

/* I40E_MASK is a macro used on 32 bit registers */
#define I40E_MASK(mask, shift) ((u32)(mask) << (shift))

#define I40E_MAX_VSI_QP			16
#define I40E_MAX_VF_VSI			3
#define I40E_MAX_CHAINED_RX_BUFFERS	5

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
	I40E_DEBUG_PACKAGE		= 0x00002000,

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
	I40E_MAC_XL710,
	I40E_MAC_VF,
	I40E_MAC_X722,
	I40E_MAC_X722_VF,
	I40E_MAC_GENERIC,
};

enum i40e_vsi_type {
	I40E_VSI_MAIN	= 0,
	I40E_VSI_VMDQ1	= 1,
	I40E_VSI_VMDQ2	= 2,
	I40E_VSI_CTRL	= 3,
	I40E_VSI_FCOE	= 4,
	I40E_VSI_MIRROR	= 5,
	I40E_VSI_SRIOV	= 6,
	I40E_VSI_FDIR	= 7,
	I40E_VSI_TYPE_UNKNOWN
};

enum i40e_queue_type {
	I40E_QUEUE_TYPE_RX = 0,
	I40E_QUEUE_TYPE_TX,
	I40E_QUEUE_TYPE_PE_CEQ,
	I40E_QUEUE_TYPE_UNKNOWN
};

#define I40E_HW_CAP_MAX_GPIO		30
/* Capabilities of a PF or a VF or the whole device */
struct i40e_hw_capabilities {
	bool dcb;
	bool fcoe;
	u32 num_vsis;
	u32 num_rx_qp;
	u32 num_tx_qp;
	u32 base_queue;
	u32 num_msix_vectors_vf;
};

struct i40e_mac_info {
	enum i40e_mac_type type;
	u8 addr[ETH_ALEN];
	u8 perm_addr[ETH_ALEN];
	u8 san_addr[ETH_ALEN];
	u16 max_fcoeq;
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
	u16 bus_id;
};

#define I40E_MAX_TRAFFIC_CLASS		8
#define I40E_MAX_USER_PRIORITY		8
/* Port hardware description */
struct i40e_hw {
	u8 __iomem *hw_addr;
	void *back;

	/* subsystem structs */
	struct i40e_mac_info mac;
	struct i40e_bus_info bus;

	/* pci info */
	u16 device_id;
	u16 vendor_id;
	u16 subsystem_device_id;
	u16 subsystem_vendor_id;
	u8 revision_id;

	/* capabilities for entire device and PCI func */
	struct i40e_hw_capabilities dev_caps;

	/* Admin Queue info */
	struct i40e_adminq_info aq;

	/* debug mask */
	u32 debug_mask;
	char err_str[16];
};

static inline bool i40e_is_vf(struct i40e_hw *hw)
{
	return (hw->mac.type == I40E_MAC_VF ||
		hw->mac.type == I40E_MAC_X722_VF);
}

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
	/* Note: Bit 8 is reserved in X710 and XL710 */
	I40E_RX_DESC_STATUS_EXT_UDP_0_SHIFT	= 8,
	I40E_RX_DESC_STATUS_UMBCAST_SHIFT	= 9, /* 2 BITS */
	I40E_RX_DESC_STATUS_FLM_SHIFT		= 11,
	I40E_RX_DESC_STATUS_FLTSTAT_SHIFT	= 12, /* 2 BITS */
	I40E_RX_DESC_STATUS_LPBK_SHIFT		= 14,
	I40E_RX_DESC_STATUS_IPV6EXADD_SHIFT	= 15,
	I40E_RX_DESC_STATUS_RESERVED_SHIFT	= 16, /* 2 BITS */
	/* Note: For non-tunnel packets INT_UDP_0 is the right status for
	 * UDP header
	 */
	I40E_RX_DESC_STATUS_INT_UDP_0_SHIFT	= 18,
	I40E_RX_DESC_STATUS_LAST /* this entry must be last!!! */
};

#define I40E_RXD_QW1_STATUS_SHIFT	0
#define I40E_RXD_QW1_STATUS_MASK	((BIT(I40E_RX_DESC_STATUS_LAST) - 1) \
					 << I40E_RXD_QW1_STATUS_SHIFT)

#define I40E_RXD_QW1_STATUS_TSYNINDX_SHIFT   I40E_RX_DESC_STATUS_TSYNINDX_SHIFT
#define I40E_RXD_QW1_STATUS_TSYNINDX_MASK	(0x3UL << \
					     I40E_RXD_QW1_STATUS_TSYNINDX_SHIFT)

#define I40E_RXD_QW1_STATUS_TSYNVALID_SHIFT  I40E_RX_DESC_STATUS_TSYNVALID_SHIFT
#define I40E_RXD_QW1_STATUS_TSYNVALID_MASK \
				    BIT_ULL(I40E_RXD_QW1_STATUS_TSYNVALID_SHIFT)

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
#define I40E_RXD_QW1_LENGTH_SPH_MASK	BIT_ULL(I40E_RXD_QW1_LENGTH_SPH_SHIFT)

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

#define I40E_TXD_CTX_UDP_TUNNELING	BIT_ULL(I40E_TXD_CTX_QW0_NATT_SHIFT)
#define I40E_TXD_CTX_GRE_TUNNELING	(0x2ULL << I40E_TXD_CTX_QW0_NATT_SHIFT)

#define I40E_TXD_CTX_QW0_EIP_NOINC_SHIFT	11
#define I40E_TXD_CTX_QW0_EIP_NOINC_MASK \
				       BIT_ULL(I40E_TXD_CTX_QW0_EIP_NOINC_SHIFT)

#define I40E_TXD_CTX_EIP_NOINC_IPID_CONST	I40E_TXD_CTX_QW0_EIP_NOINC_MASK

#define I40E_TXD_CTX_QW0_NATLEN_SHIFT	12
#define I40E_TXD_CTX_QW0_NATLEN_MASK	(0X7FULL << \
					 I40E_TXD_CTX_QW0_NATLEN_SHIFT)

#define I40E_TXD_CTX_QW0_DECTTL_SHIFT	19
#define I40E_TXD_CTX_QW0_DECTTL_MASK	(0xFULL << \
					 I40E_TXD_CTX_QW0_DECTTL_SHIFT)

#define I40E_TXD_CTX_QW0_L4T_CS_SHIFT	23
#define I40E_TXD_CTX_QW0_L4T_CS_MASK	BIT_ULL(I40E_TXD_CTX_QW0_L4T_CS_SHIFT)

/* Packet Classifier Types for filters */
enum i40e_filter_pctype {
	/* Note: Values 0-28 are reserved for future use.
	 * Value 29, 30, 32 are not supported on XL710 and X710.
	 */
	I40E_FILTER_PCTYPE_NONF_UNICAST_IPV4_UDP	= 29,
	I40E_FILTER_PCTYPE_NONF_MULTICAST_IPV4_UDP	= 30,
	I40E_FILTER_PCTYPE_NONF_IPV4_UDP		= 31,
	I40E_FILTER_PCTYPE_NONF_IPV4_TCP_SYN_NO_ACK	= 32,
	I40E_FILTER_PCTYPE_NONF_IPV4_TCP		= 33,
	I40E_FILTER_PCTYPE_NONF_IPV4_SCTP		= 34,
	I40E_FILTER_PCTYPE_NONF_IPV4_OTHER		= 35,
	I40E_FILTER_PCTYPE_FRAG_IPV4			= 36,
	/* Note: Values 37-38 are reserved for future use.
	 * Value 39, 40, 42 are not supported on XL710 and X710.
	 */
	I40E_FILTER_PCTYPE_NONF_UNICAST_IPV6_UDP	= 39,
	I40E_FILTER_PCTYPE_NONF_MULTICAST_IPV6_UDP	= 40,
	I40E_FILTER_PCTYPE_NONF_IPV6_UDP		= 41,
	I40E_FILTER_PCTYPE_NONF_IPV6_TCP_SYN_NO_ACK	= 42,
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
#endif /* _I40E_TYPE_H_ */
