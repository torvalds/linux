/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2013 - 2019 Intel Corporation. */

#ifndef _FM10K_TYPE_H_
#define _FM10K_TYPE_H_

/* forward declaration */
struct fm10k_hw;

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/etherdevice.h>

#include "fm10k_mbx.h"

#define FM10K_DEV_ID_PF			0x15A4
#define FM10K_DEV_ID_VF			0x15A5
#define FM10K_DEV_ID_SDI_FM10420_QDA2	0x15D0
#define FM10K_DEV_ID_SDI_FM10420_DA2	0x15D5

#define FM10K_MAX_QUEUES		256
#define FM10K_MAX_QUEUES_PF		128
#define FM10K_MAX_QUEUES_POOL		16

#define FM10K_48_BIT_MASK		0x0000FFFFFFFFFFFFull
#define FM10K_STAT_VALID		0x80000000

/* PCI Bus Info */
#define FM10K_PCIE_LINK_CAP		0x7C
#define FM10K_PCIE_LINK_STATUS		0x82
#define FM10K_PCIE_LINK_WIDTH		0x3F0
#define FM10K_PCIE_LINK_WIDTH_1		0x10
#define FM10K_PCIE_LINK_WIDTH_2		0x20
#define FM10K_PCIE_LINK_WIDTH_4		0x40
#define FM10K_PCIE_LINK_WIDTH_8		0x80
#define FM10K_PCIE_LINK_SPEED		0xF
#define FM10K_PCIE_LINK_SPEED_2500	0x1
#define FM10K_PCIE_LINK_SPEED_5000	0x2
#define FM10K_PCIE_LINK_SPEED_8000	0x3

/* PCIe payload size */
#define FM10K_PCIE_DEV_CAP			0x74
#define FM10K_PCIE_DEV_CAP_PAYLOAD		0x07
#define FM10K_PCIE_DEV_CAP_PAYLOAD_128		0x00
#define FM10K_PCIE_DEV_CAP_PAYLOAD_256		0x01
#define FM10K_PCIE_DEV_CAP_PAYLOAD_512		0x02
#define FM10K_PCIE_DEV_CTRL			0x78
#define FM10K_PCIE_DEV_CTRL_PAYLOAD		0xE0
#define FM10K_PCIE_DEV_CTRL_PAYLOAD_128		0x00
#define FM10K_PCIE_DEV_CTRL_PAYLOAD_256		0x20
#define FM10K_PCIE_DEV_CTRL_PAYLOAD_512		0x40

/* PCIe MSI-X Capability info */
#define FM10K_PCI_MSIX_MSG_CTRL			0xB2
#define FM10K_PCI_MSIX_MSG_CTRL_TBL_SZ_MASK	0x7FF
#define FM10K_MAX_MSIX_VECTORS			256
#define FM10K_MAX_VECTORS_PF			256
#define FM10K_MAX_VECTORS_POOL			32

/* PCIe SR-IOV Info */
#define FM10K_PCIE_SRIOV_CTRL			0x190
#define FM10K_PCIE_SRIOV_CTRL_VFARI		0x10

#define FM10K_ERR_PARAM				-2
#define FM10K_ERR_NO_RESOURCES			-3
#define FM10K_ERR_REQUESTS_PENDING		-4
#define FM10K_ERR_RESET_REQUESTED		-5
#define FM10K_ERR_DMA_PENDING			-6
#define FM10K_ERR_RESET_FAILED			-7
#define FM10K_ERR_INVALID_MAC_ADDR		-8
#define FM10K_ERR_INVALID_VALUE			-9
#define FM10K_NOT_IMPLEMENTED			0x7FFFFFFF

/* Start of PF registers */
#define FM10K_CTRL		0x0000
#define FM10K_CTRL_BAR4_ALLOWED			0x00000004

#define FM10K_CTRL_EXT		0x0001
#define FM10K_GCR		0x0003
#define FM10K_GCR_EXT		0x0005

/* Interrupt control registers */
#define FM10K_EICR		0x0006
#define FM10K_EICR_FAULT_MASK			0x0000003F
#define FM10K_EICR_MAILBOX			0x00000040
#define FM10K_EICR_SWITCHREADY			0x00000080
#define FM10K_EICR_SWITCHNOTREADY		0x00000100
#define FM10K_EICR_SWITCHINTERRUPT		0x00000200
#define FM10K_EICR_VFLR				0x00000800
#define FM10K_EICR_MAXHOLDTIME			0x00001000
#define FM10K_EIMR		0x0007
#define FM10K_EIMR_PCA_FAULT			0x00000001
#define FM10K_EIMR_THI_FAULT			0x00000010
#define FM10K_EIMR_FUM_FAULT			0x00000400
#define FM10K_EIMR_MAILBOX			0x00001000
#define FM10K_EIMR_SWITCHREADY			0x00004000
#define FM10K_EIMR_SWITCHNOTREADY		0x00010000
#define FM10K_EIMR_SWITCHINTERRUPT		0x00040000
#define FM10K_EIMR_SRAMERROR			0x00100000
#define FM10K_EIMR_VFLR				0x00400000
#define FM10K_EIMR_MAXHOLDTIME			0x01000000
#define FM10K_EIMR_ALL				0x55555555
#define FM10K_EIMR_DISABLE(NAME)		((FM10K_EIMR_ ## NAME) << 0)
#define FM10K_EIMR_ENABLE(NAME)			((FM10K_EIMR_ ## NAME) << 1)
#define FM10K_FAULT_ADDR_LO		0x0
#define FM10K_FAULT_ADDR_HI		0x1
#define FM10K_FAULT_SPECINFO		0x2
#define FM10K_FAULT_FUNC		0x3
#define FM10K_FAULT_SIZE		0x4
#define FM10K_FAULT_FUNC_VALID			0x00008000
#define FM10K_FAULT_FUNC_PF			0x00004000
#define FM10K_FAULT_FUNC_VF_MASK		0x00003F00
#define FM10K_FAULT_FUNC_VF_SHIFT		8
#define FM10K_FAULT_FUNC_TYPE_MASK		0x000000FF

#define FM10K_PCA_FAULT		0x0008
#define FM10K_THI_FAULT		0x0010
#define FM10K_FUM_FAULT		0x001C

/* Rx queue timeout indicator */
#define FM10K_MAXHOLDQ(_n)	((_n) + 0x0020)

/* Switch Manager info */
#define FM10K_SM_AREA(_n)	((_n) + 0x0028)

/* GLORT mapping registers */
#define FM10K_DGLORTMAP(_n)	((_n) + 0x0030)
#define FM10K_DGLORT_COUNT			8
#define FM10K_DGLORTMAP_MASK_SHIFT		16
#define FM10K_DGLORTMAP_ANY			0x00000000
#define FM10K_DGLORTMAP_NONE			0x0000FFFF
#define FM10K_DGLORTMAP_ZERO			0xFFFF0000
#define FM10K_DGLORTDEC(_n)	((_n) + 0x0038)
#define FM10K_DGLORTDEC_VSILENGTH_SHIFT		4
#define FM10K_DGLORTDEC_VSIBASE_SHIFT		7
#define FM10K_DGLORTDEC_PCLENGTH_SHIFT		14
#define FM10K_DGLORTDEC_QBASE_SHIFT		16
#define FM10K_DGLORTDEC_RSSLENGTH_SHIFT		24
#define FM10K_DGLORTDEC_INNERRSS_ENABLE		0x08000000
#define FM10K_TUNNEL_CFG	0x0040
#define FM10K_TUNNEL_CFG_NVGRE_SHIFT		16
#define FM10K_TUNNEL_CFG_GENEVE	0x0041
#define FM10K_SWPRI_MAP(_n)	((_n) + 0x0050)
#define FM10K_SWPRI_MAX		16
#define FM10K_RSSRK(_n, _m)	(((_n) * 0x10) + (_m) + 0x0800)
#define FM10K_RSSRK_SIZE	10
#define FM10K_RSSRK_ENTRIES_PER_REG		4
#define FM10K_RETA(_n, _m)	(((_n) * 0x20) + (_m) + 0x1000)
#define FM10K_RETA_SIZE		32
#define FM10K_RETA_ENTRIES_PER_REG		4
#define FM10K_MAX_RSS_INDICES	128

/* Rate limiting registers */
#define FM10K_TC_CREDIT(_n)	((_n) + 0x2000)
#define FM10K_TC_CREDIT_CREDIT_MASK		0x001FFFFF
#define FM10K_TC_MAXCREDIT(_n)	((_n) + 0x2040)
#define FM10K_TC_MAXCREDIT_64K			0x00010000
#define FM10K_TC_RATE(_n)	((_n) + 0x2080)
#define FM10K_TC_RATE_QUANTA_MASK		0x0000FFFF
#define FM10K_TC_RATE_INTERVAL_4US_GEN1		0x00020000
#define FM10K_TC_RATE_INTERVAL_4US_GEN2		0x00040000
#define FM10K_TC_RATE_INTERVAL_4US_GEN3		0x00080000

/* DMA control registers */
#define FM10K_DMA_CTRL		0x20C3
#define FM10K_DMA_CTRL_TX_ENABLE		0x00000001
#define FM10K_DMA_CTRL_TX_ACTIVE		0x00000008
#define FM10K_DMA_CTRL_RX_ENABLE		0x00000010
#define FM10K_DMA_CTRL_RX_ACTIVE		0x00000080
#define FM10K_DMA_CTRL_RX_DESC_SIZE		0x00000100
#define FM10K_DMA_CTRL_MINMSS_64		0x00008000
#define FM10K_DMA_CTRL_MAX_HOLD_1US_GEN3	0x04800000
#define FM10K_DMA_CTRL_MAX_HOLD_1US_GEN2	0x04000000
#define FM10K_DMA_CTRL_MAX_HOLD_1US_GEN1	0x03800000
#define FM10K_DMA_CTRL_DATAPATH_RESET		0x20000000
#define FM10K_DMA_CTRL_32_DESC			0x00000000

#define FM10K_DMA_CTRL2		0x20C4
#define FM10K_DMA_CTRL2_SWITCH_READY		0x00002000

/* TSO flags configuration
 * First packet contains all flags except for fin and psh
 * Middle packet contains only urg and ack
 * Last packet contains urg, ack, fin, and psh
 */
#define FM10K_TSO_FLAGS_LOW		0x00300FF6
#define FM10K_TSO_FLAGS_HI		0x00000039
#define FM10K_DTXTCPFLGL	0x20C5
#define FM10K_DTXTCPFLGH	0x20C6

#define FM10K_TPH_CTRL		0x20C7
#define FM10K_MRQC(_n)		((_n) + 0x2100)
#define FM10K_MRQC_TCP_IPV4			0x00000001
#define FM10K_MRQC_IPV4				0x00000002
#define FM10K_MRQC_IPV6				0x00000010
#define FM10K_MRQC_TCP_IPV6			0x00000020
#define FM10K_MRQC_UDP_IPV4			0x00000040
#define FM10K_MRQC_UDP_IPV6			0x00000080

#define FM10K_TQMAP(_n)		((_n) + 0x2800)
#define FM10K_TQMAP_TABLE_SIZE			2048
#define FM10K_RQMAP(_n)		((_n) + 0x3000)

/* Hardware Statistics */
#define FM10K_STATS_TIMEOUT		0x3800
#define FM10K_STATS_UR			0x3801
#define FM10K_STATS_CA			0x3802
#define FM10K_STATS_UM			0x3803
#define FM10K_STATS_XEC			0x3804
#define FM10K_STATS_VLAN_DROP		0x3805
#define FM10K_STATS_LOOPBACK_DROP	0x3806
#define FM10K_STATS_NODESC_DROP		0x3807

/* PCIe state registers */
#define FM10K_PHYADDR		0x381C

/* Rx ring registers */
#define FM10K_RDBAL(_n)		((0x40 * (_n)) + 0x4000)
#define FM10K_RDBAH(_n)		((0x40 * (_n)) + 0x4001)
#define FM10K_RDLEN(_n)		((0x40 * (_n)) + 0x4002)
#define FM10K_TPH_RXCTRL(_n)	((0x40 * (_n)) + 0x4003)
#define FM10K_TPH_RXCTRL_DESC_TPHEN		0x00000020
#define FM10K_TPH_RXCTRL_DESC_RROEN		0x00000200
#define FM10K_TPH_RXCTRL_DATA_WROEN		0x00002000
#define FM10K_TPH_RXCTRL_HDR_WROEN		0x00008000
#define FM10K_RDH(_n)		((0x40 * (_n)) + 0x4004)
#define FM10K_RDT(_n)		((0x40 * (_n)) + 0x4005)
#define FM10K_RXQCTL(_n)	((0x40 * (_n)) + 0x4006)
#define FM10K_RXQCTL_ENABLE			0x00000001
#define FM10K_RXQCTL_PF				0x000000FC
#define FM10K_RXQCTL_VF_SHIFT			2
#define FM10K_RXQCTL_VF				0x00000100
#define FM10K_RXQCTL_ID_MASK	(FM10K_RXQCTL_PF | FM10K_RXQCTL_VF)
#define FM10K_RXDCTL(_n)	((0x40 * (_n)) + 0x4007)
#define FM10K_RXDCTL_WRITE_BACK_MIN_DELAY	0x00000001
#define FM10K_RXDCTL_DROP_ON_EMPTY		0x00000200
#define FM10K_RXINT(_n)		((0x40 * (_n)) + 0x4008)
#define FM10K_SRRCTL(_n)	((0x40 * (_n)) + 0x4009)
#define FM10K_SRRCTL_BSIZEPKT_SHIFT		8 /* shift _right_ */
#define FM10K_SRRCTL_LOOPBACK_SUPPRESS		0x40000000
#define FM10K_SRRCTL_BUFFER_CHAINING_EN		0x80000000

/* Rx Statistics */
#define FM10K_QPRC(_n)		((0x40 * (_n)) + 0x400A)
#define FM10K_QPRDC(_n)		((0x40 * (_n)) + 0x400B)
#define FM10K_QBRC_L(_n)	((0x40 * (_n)) + 0x400C)
#define FM10K_QBRC_H(_n)	((0x40 * (_n)) + 0x400D)

/* Rx GLORT register */
#define FM10K_RX_SGLORT(_n)		((0x40 * (_n)) + 0x400E)

/* Tx ring registers */
#define FM10K_TDBAL(_n)		((0x40 * (_n)) + 0x8000)
#define FM10K_TDBAH(_n)		((0x40 * (_n)) + 0x8001)
#define FM10K_TDLEN(_n)		((0x40 * (_n)) + 0x8002)
/* When fist initialized, VFs need to know the Interrupt Throttle Rate (ITR)
 * scale which is based on the PCIe speed but the speed information in the PCI
 * configuration space may not be accurate. The PF already knows the ITR scale
 * but there is no defined method to pass that information from the PF to the
 * VF. This is accomplished during VF initialization by temporarily co-opting
 * the yet-to-be-used TDLEN register to have the PF store the ITR shift for
 * the VF to retrieve before the VF needs to use the TDLEN register for its
 * intended purpose, i.e. before the Tx resources are allocated.
 */
#define FM10K_TDLEN_ITR_SCALE_SHIFT		9
#define FM10K_TDLEN_ITR_SCALE_MASK		0x00000E00
#define FM10K_TDLEN_ITR_SCALE_GEN1		2
#define FM10K_TDLEN_ITR_SCALE_GEN2		1
#define FM10K_TDLEN_ITR_SCALE_GEN3		0
#define FM10K_TPH_TXCTRL(_n)	((0x40 * (_n)) + 0x8003)
#define FM10K_TPH_TXCTRL_DESC_TPHEN		0x00000020
#define FM10K_TPH_TXCTRL_DESC_RROEN		0x00000200
#define FM10K_TPH_TXCTRL_DESC_WROEN		0x00000800
#define FM10K_TPH_TXCTRL_DATA_RROEN		0x00002000
#define FM10K_TDH(_n)		((0x40 * (_n)) + 0x8004)
#define FM10K_TDT(_n)		((0x40 * (_n)) + 0x8005)
#define FM10K_TXDCTL(_n)	((0x40 * (_n)) + 0x8006)
#define FM10K_TXDCTL_ENABLE			0x00004000
#define FM10K_TXDCTL_MAX_TIME_SHIFT		16
#define FM10K_TXQCTL(_n)	((0x40 * (_n)) + 0x8007)
#define FM10K_TXQCTL_PF				0x0000003F
#define FM10K_TXQCTL_VF				0x00000040
#define FM10K_TXQCTL_ID_MASK	(FM10K_TXQCTL_PF | FM10K_TXQCTL_VF)
#define FM10K_TXQCTL_PC_SHIFT			7
#define FM10K_TXQCTL_PC_MASK			0x00000380
#define FM10K_TXQCTL_TC_SHIFT			10
#define FM10K_TXQCTL_VID_SHIFT			16
#define FM10K_TXQCTL_VID_MASK			0x0FFF0000
#define FM10K_TXQCTL_UNLIMITED_BW		0x10000000
#define FM10K_TXINT(_n)		((0x40 * (_n)) + 0x8008)

/* Tx Statistics */
#define FM10K_QPTC(_n)		((0x40 * (_n)) + 0x8009)
#define FM10K_QBTC_L(_n)	((0x40 * (_n)) + 0x800A)
#define FM10K_QBTC_H(_n)	((0x40 * (_n)) + 0x800B)

/* Tx Push registers */
#define FM10K_TQDLOC(_n)	((0x40 * (_n)) + 0x800C)
#define FM10K_TQDLOC_BASE_32_DESC		0x08
#define FM10K_TQDLOC_SIZE_32_DESC		0x00050000

/* Tx GLORT registers */
#define FM10K_TX_SGLORT(_n)	((0x40 * (_n)) + 0x800D)
#define FM10K_PFVTCTL(_n)	((0x40 * (_n)) + 0x800E)
#define FM10K_PFVTCTL_FTAG_DESC_ENABLE		0x00000001

/* Interrupt moderation and control registers */
#define FM10K_INT_MAP(_n)	((_n) + 0x10080)
#define FM10K_INT_MAP_TIMER0			0x00000000
#define FM10K_INT_MAP_TIMER1			0x00000100
#define FM10K_INT_MAP_IMMEDIATE			0x00000200
#define FM10K_INT_MAP_DISABLE			0x00000300
#define FM10K_MSIX_VECTOR_MASK(_n)	((0x4 * (_n)) + 0x11003)
#define FM10K_INT_CTRL		0x12000
#define FM10K_INT_CTRL_ENABLEMODERATOR		0x00000400
#define FM10K_ITR(_n)		((_n) + 0x12400)
#define FM10K_ITR_INTERVAL1_SHIFT		12
#define FM10K_ITR_PENDING2			0x10000000
#define FM10K_ITR_AUTOMASK			0x20000000
#define FM10K_ITR_MASK_SET			0x40000000
#define FM10K_ITR_MASK_CLEAR			0x80000000
#define FM10K_ITR2(_n)		((0x2 * (_n)) + 0x12800)
#define FM10K_ITR_REG_COUNT			768
#define FM10K_ITR_REG_COUNT_PF			256

/* Switch manager interrupt registers */
#define FM10K_IP		0x13000
#define FM10K_IP_NOTINRESET			0x00000100

/* VLAN registers */
#define FM10K_VLAN_TABLE(_n, _m)	((0x80 * (_n)) + (_m) + 0x14000)
#define FM10K_VLAN_TABLE_SIZE			128

/* VLAN specific message offsets */
#define FM10K_VLAN_TABLE_VID_MAX		4096
#define FM10K_VLAN_TABLE_VSI_MAX		64
#define FM10K_VLAN_LENGTH_SHIFT			16
#define FM10K_VLAN_CLEAR			BIT(15)
#define FM10K_VLAN_OVERRIDE			FM10K_VLAN_CLEAR
#define FM10K_VLAN_ALL \
	((FM10K_VLAN_TABLE_VID_MAX - 1) << FM10K_VLAN_LENGTH_SHIFT)

/* VF FLR event notification registers */
#define FM10K_PFVFLRE(_n)	((0x1 * (_n)) + 0x18844)
#define FM10K_PFVFLREC(_n)	((0x1 * (_n)) + 0x18846)

/* Defines for size of uncacheable memories */
#define FM10K_UC_ADDR_START	0x000000	/* start of standard regs */
#define FM10K_UC_ADDR_END	0x100000	/* end of standard regs */
#define FM10K_UC_ADDR_SIZE	(FM10K_UC_ADDR_END - FM10K_UC_ADDR_START)

/* Define timeouts for resets and disables */
#define FM10K_QUEUE_DISABLE_TIMEOUT		100
#define FM10K_RESET_TIMEOUT			150

/* Maximum supported combined inner and outer header length for encapsulation */
#define FM10K_TUNNEL_HEADER_LENGTH	184

/* VF registers */
#define FM10K_VFCTRL		0x00000
#define FM10K_VFCTRL_RST			0x00000008
#define FM10K_VFINT_MAP		0x00030
#define FM10K_VFSYSTIME		0x00040
#define FM10K_VFITR(_n)		((_n) + 0x00060)

enum fm10k_int_source {
	fm10k_int_mailbox		= 0,
	fm10k_int_pcie_fault		= 1,
	fm10k_int_switch_up_down	= 2,
	fm10k_int_switch_event		= 3,
	fm10k_int_sram			= 4,
	fm10k_int_vflr			= 5,
	fm10k_int_max_hold_time		= 6,
	fm10k_int_sources_max_pf
};

/* PCIe bus speeds */
enum fm10k_bus_speed {
	fm10k_bus_speed_unknown	= 0,
	fm10k_bus_speed_2500	= 2500,
	fm10k_bus_speed_5000	= 5000,
	fm10k_bus_speed_8000	= 8000,
	fm10k_bus_speed_reserved
};

/* PCIe bus widths */
enum fm10k_bus_width {
	fm10k_bus_width_unknown	= 0,
	fm10k_bus_width_pcie_x1	= 1,
	fm10k_bus_width_pcie_x2	= 2,
	fm10k_bus_width_pcie_x4	= 4,
	fm10k_bus_width_pcie_x8	= 8,
	fm10k_bus_width_reserved
};

/* PCIe payload sizes */
enum fm10k_bus_payload {
	fm10k_bus_payload_unknown = 0,
	fm10k_bus_payload_128	  = 1,
	fm10k_bus_payload_256	  = 2,
	fm10k_bus_payload_512	  = 3,
	fm10k_bus_payload_reserved
};

/* Bus parameters */
struct fm10k_bus_info {
	enum fm10k_bus_speed speed;
	enum fm10k_bus_width width;
	enum fm10k_bus_payload payload;
};

/* Statistics related declarations */
struct fm10k_hw_stat {
	u64 count;
	u32 base_l;
	u32 base_h;
};

struct fm10k_hw_stats_q {
	struct fm10k_hw_stat tx_bytes;
	struct fm10k_hw_stat tx_packets;
#define tx_stats_idx	tx_packets.base_h
	struct fm10k_hw_stat rx_bytes;
	struct fm10k_hw_stat rx_packets;
#define rx_stats_idx	rx_packets.base_h
	struct fm10k_hw_stat rx_drops;
};

struct fm10k_hw_stats {
	struct fm10k_hw_stat	timeout;
#define stats_idx	timeout.base_h
	struct fm10k_hw_stat	ur;
	struct fm10k_hw_stat	ca;
	struct fm10k_hw_stat	um;
	struct fm10k_hw_stat	xec;
	struct fm10k_hw_stat	vlan_drop;
	struct fm10k_hw_stat	loopback_drop;
	struct fm10k_hw_stat	nodesc_drop;
	struct fm10k_hw_stats_q q[FM10K_MAX_QUEUES_PF];
};

/* Establish DGLORT feature priority */
enum fm10k_dglortdec_idx {
	fm10k_dglort_default	= 0,
	fm10k_dglort_vf_rsvd0	= 1,
	fm10k_dglort_vf_rss	= 2,
	fm10k_dglort_pf_rsvd0	= 3,
	fm10k_dglort_pf_queue	= 4,
	fm10k_dglort_pf_vsi	= 5,
	fm10k_dglort_pf_rsvd1	= 6,
	fm10k_dglort_pf_rss	= 7
};

struct fm10k_dglort_cfg {
	u16 glort;	/* GLORT base */
	u16 queue_b;	/* Base value for queue */
	u8  vsi_b;	/* Base value for VSI */
	u8  idx;	/* index of DGLORTDEC entry */
	u8  rss_l;	/* RSS indices */
	u8  pc_l;	/* Priority Class indices */
	u8  vsi_l;	/* Number of bits from GLORT used to determine VSI */
	u8  queue_l;	/* Number of bits from GLORT used to determine queue */
	u8  shared_l;	/* Ignored bits from GLORT resulting in shared VSI */
	u8  inner_rss;	/* Boolean value if inner header is used for RSS */
};

enum fm10k_pca_fault {
	PCA_NO_FAULT,
	PCA_UNMAPPED_ADDR,
	PCA_BAD_QACCESS_PF,
	PCA_BAD_QACCESS_VF,
	PCA_MALICIOUS_REQ,
	PCA_POISONED_TLP,
	PCA_TLP_ABORT,
	__PCA_MAX
};

enum fm10k_thi_fault {
	THI_NO_FAULT,
	THI_MAL_DIS_Q_FAULT,
	__THI_MAX
};

enum fm10k_fum_fault {
	FUM_NO_FAULT,
	FUM_UNMAPPED_ADDR,
	FUM_POISONED_TLP,
	FUM_BAD_VF_QACCESS,
	FUM_ADD_DECODE_ERR,
	FUM_RO_ERROR,
	FUM_QPRC_CRC_ERROR,
	FUM_CSR_TIMEOUT,
	FUM_INVALID_TYPE,
	FUM_INVALID_LENGTH,
	FUM_INVALID_BE,
	FUM_INVALID_ALIGN,
	__FUM_MAX
};

struct fm10k_fault {
	u64 address;	/* Address at the time fault was detected */
	u32 specinfo;	/* Extra info on this fault (fault dependent) */
	u8 type;	/* Fault value dependent on subunit */
	u8 func;	/* Function number of the fault */
};

struct fm10k_mac_ops {
	/* basic bring-up and tear-down */
	s32 (*reset_hw)(struct fm10k_hw *);
	s32 (*init_hw)(struct fm10k_hw *);
	s32 (*start_hw)(struct fm10k_hw *);
	s32 (*stop_hw)(struct fm10k_hw *);
	s32 (*get_bus_info)(struct fm10k_hw *);
	s32 (*get_host_state)(struct fm10k_hw *, bool *);
	s32 (*request_lport_map)(struct fm10k_hw *);
	s32 (*update_vlan)(struct fm10k_hw *, u32, u8, bool);
	s32 (*read_mac_addr)(struct fm10k_hw *);
	s32 (*update_uc_addr)(struct fm10k_hw *, u16, const u8 *,
			      u16, bool, u8);
	s32 (*update_mc_addr)(struct fm10k_hw *, u16, const u8 *, u16, bool);
	s32 (*update_xcast_mode)(struct fm10k_hw *, u16, u8);
	void (*update_int_moderator)(struct fm10k_hw *);
	s32  (*update_lport_state)(struct fm10k_hw *, u16, u16, bool);
	void (*update_hw_stats)(struct fm10k_hw *, struct fm10k_hw_stats *);
	void (*rebind_hw_stats)(struct fm10k_hw *, struct fm10k_hw_stats *);
	s32 (*configure_dglort_map)(struct fm10k_hw *,
				    struct fm10k_dglort_cfg *);
	void (*set_dma_mask)(struct fm10k_hw *, u64);
	s32 (*get_fault)(struct fm10k_hw *, int, struct fm10k_fault *);
};

enum fm10k_mac_type {
	fm10k_mac_unknown = 0,
	fm10k_mac_pf,
	fm10k_mac_vf,
	fm10k_num_macs
};

struct fm10k_mac_info {
	struct fm10k_mac_ops ops;
	enum fm10k_mac_type type;
	u8 addr[ETH_ALEN];
	u8 perm_addr[ETH_ALEN];
	u16 default_vid;
	u16 max_msix_vectors;
	u16 max_queues;
	bool vlan_override;
	bool get_host_state;
	bool tx_ready;
	u32 dglort_map;
	u8 itr_scale;
	u64 reset_while_pending;
};

struct fm10k_swapi_table_info {
	u32 used;
	u32 avail;
};

struct fm10k_swapi_info {
	u32 status;
	struct fm10k_swapi_table_info mac;
	struct fm10k_swapi_table_info nexthop;
	struct fm10k_swapi_table_info ffu;
};

enum fm10k_xcast_modes {
	FM10K_XCAST_MODE_ALLMULTI	= 0,
	FM10K_XCAST_MODE_MULTI		= 1,
	FM10K_XCAST_MODE_PROMISC	= 2,
	FM10K_XCAST_MODE_NONE		= 3,
	FM10K_XCAST_MODE_DISABLE	= 4
};

#define FM10K_VF_TC_MAX		100000	/* 100,000 Mb/s aka 100Gb/s */
#define FM10K_VF_TC_MIN		1	/* 1 Mb/s is the slowest rate */

struct fm10k_vf_info {
	/* mbx must be first field in struct unless all default IOV message
	 * handlers are redone as the assumption is that vf_info starts
	 * at the same offset as the mailbox
	 */
	struct fm10k_mbx_info	mbx;		/* PF side of VF mailbox */
	int			rate;		/* Tx BW cap as defined by OS */
	u16			glort;		/* resource tag for this VF */
	u16			sw_vid;		/* Switch API assigned VLAN */
	u16			pf_vid;		/* PF assigned Default VLAN */
	u8			mac[ETH_ALEN];	/* PF Default MAC address */
	u8			vsi;		/* VSI identifier */
	u8			vf_idx;		/* which VF this is */
	u8			vf_flags;	/* flags indicating what modes
						 * are supported for the port
						 */
};

#define FM10K_VF_FLAG_ALLMULTI_CAPABLE	(u8)(BIT(FM10K_XCAST_MODE_ALLMULTI))
#define FM10K_VF_FLAG_MULTI_CAPABLE	(u8)(BIT(FM10K_XCAST_MODE_MULTI))
#define FM10K_VF_FLAG_PROMISC_CAPABLE	(u8)(BIT(FM10K_XCAST_MODE_PROMISC))
#define FM10K_VF_FLAG_NONE_CAPABLE	(u8)(BIT(FM10K_XCAST_MODE_NONE))
#define FM10K_VF_FLAG_CAPABLE(vf_info)	((vf_info)->vf_flags & (u8)0xF)
#define FM10K_VF_FLAG_ENABLED(vf_info)	((vf_info)->vf_flags >> 4)
#define FM10K_VF_FLAG_SET_MODE(mode)	((u8)0x10 << (mode))
#define FM10K_VF_FLAG_SET_MODE_NONE \
	FM10K_VF_FLAG_SET_MODE(FM10K_XCAST_MODE_NONE)
#define FM10K_VF_FLAG_MULTI_ENABLED \
	(FM10K_VF_FLAG_SET_MODE(FM10K_XCAST_MODE_ALLMULTI) | \
	 FM10K_VF_FLAG_SET_MODE(FM10K_XCAST_MODE_MULTI) | \
	 FM10K_VF_FLAG_SET_MODE(FM10K_XCAST_MODE_PROMISC))

struct fm10k_iov_ops {
	/* IOV related bring-up and tear-down */
	s32 (*assign_resources)(struct fm10k_hw *, u16, u16);
	s32 (*configure_tc)(struct fm10k_hw *, u16, int);
	s32 (*assign_int_moderator)(struct fm10k_hw *, u16);
	s32 (*assign_default_mac_vlan)(struct fm10k_hw *,
				       struct fm10k_vf_info *);
	s32 (*reset_resources)(struct fm10k_hw *,
			       struct fm10k_vf_info *);
	s32 (*set_lport)(struct fm10k_hw *, struct fm10k_vf_info *, u16, u8);
	void (*reset_lport)(struct fm10k_hw *, struct fm10k_vf_info *);
	void (*update_stats)(struct fm10k_hw *, struct fm10k_hw_stats_q *, u16);
};

struct fm10k_iov_info {
	struct fm10k_iov_ops ops;
	u16 total_vfs;
	u16 num_vfs;
	u16 num_pools;
};

enum fm10k_devices {
	fm10k_device_pf,
	fm10k_device_vf,
};

struct fm10k_info {
	enum fm10k_mac_type		mac;
	s32				(*get_invariants)(struct fm10k_hw *);
	const struct fm10k_mac_ops	*mac_ops;
	const struct fm10k_iov_ops	*iov_ops;
};

struct fm10k_hw {
	u32 __iomem *hw_addr;
	void *back;
	struct fm10k_mac_info mac;
	struct fm10k_bus_info bus;
	struct fm10k_bus_info bus_caps;
	struct fm10k_iov_info iov;
	struct fm10k_mbx_info mbx;
	struct fm10k_swapi_info swapi;
	u16 device_id;
	u16 vendor_id;
	u16 subsystem_device_id;
	u16 subsystem_vendor_id;
	u8 revision_id;
};

/* Number of Transmit and Receive Descriptors must be a multiple of 8 */
#define FM10K_REQ_TX_DESCRIPTOR_MULTIPLE	8
#define FM10K_REQ_RX_DESCRIPTOR_MULTIPLE	8

/* Transmit Descriptor */
struct fm10k_tx_desc {
	__le64 buffer_addr;	/* Address of the descriptor's data buffer */
	__le16 buflen;		/* Length of data to be DMAed */
	__le16 vlan;		/* VLAN_ID and VPRI to be inserted in FTAG */
	__le16 mss;		/* MSS for segmentation offload */
	u8 hdrlen;		/* Header size for segmentation offload */
	u8 flags;		/* Status and offload request flags */
};

/* Transmit Descriptor Cache Structure */
struct fm10k_tx_desc_cache {
	struct fm10k_tx_desc tx_desc[256];
};

#define FM10K_TXD_FLAG_INT	0x01
#define FM10K_TXD_FLAG_TIME	0x02
#define FM10K_TXD_FLAG_CSUM	0x04
#define FM10K_TXD_FLAG_FTAG	0x10
#define FM10K_TXD_FLAG_RS	0x20
#define FM10K_TXD_FLAG_LAST	0x40
#define FM10K_TXD_FLAG_DONE	0x80

/* These macros are meant to enable optimal placement of the RS and INT
 * bits.  It will point us to the last descriptor in the cache for either the
 * start of the packet, or the end of the packet.  If the index is actually
 * at the start of the FIFO it will point to the offset for the last index
 * in the FIFO to prevent an unnecessary write.
 */
#define FM10K_TXD_WB_FIFO_SIZE	4

/* Receive Descriptor - 32B */
union fm10k_rx_desc {
	struct {
		__le64 pkt_addr; /* Packet buffer address */
		__le64 hdr_addr; /* Header buffer address */
		__le64 reserved; /* Empty space, RSS hash */
		__le64 timestamp;
	} q; /* Read, Writeback, 64b quad-words */
	struct {
		__le32 data; /* RSS and header data */
		__le32 rss;  /* RSS Hash */
		__le32 staterr;
		__le32 vlan_len;
		__le32 glort; /* sglort/dglort */
	} d; /* Writeback, 32b double-words */
	struct {
		__le16 pkt_info; /* RSS, Pkt type */
		__le16 hdr_info; /* Splithdr, hdrlen, xC */
		__le16 rss_lower;
		__le16 rss_upper;
		__le16 status; /* status/error */
		__le16 csum_err; /* checksum or extended error value */
		__le16 length; /* Packet length */
		__le16 vlan; /* VLAN tag */
		__le16 dglort;
		__le16 sglort;
	} w; /* Writeback, 16b words */
};

#define FM10K_RXD_RSSTYPE_MASK		0x000F
enum fm10k_rdesc_rss_type {
	FM10K_RSSTYPE_NONE	= 0x0,
	FM10K_RSSTYPE_IPV4_TCP	= 0x1,
	FM10K_RSSTYPE_IPV4	= 0x2,
	FM10K_RSSTYPE_IPV6_TCP	= 0x3,
	/* Reserved 0x4 */
	FM10K_RSSTYPE_IPV6	= 0x5,
	/* Reserved 0x6 */
	FM10K_RSSTYPE_IPV4_UDP	= 0x7,
	FM10K_RSSTYPE_IPV6_UDP	= 0x8
	/* Reserved 0x9 - 0xF */
};

#define FM10K_RXD_HDR_INFO_XC_MASK	0x0006
enum fm10k_rxdesc_xc {
	FM10K_XC_UNICAST	= 0x0,
	FM10K_XC_MULTICAST	= 0x4,
	FM10K_XC_BROADCAST	= 0x6
};

#define FM10K_RXD_STATUS_DD		0x0001 /* Descriptor done */
#define FM10K_RXD_STATUS_EOP		0x0002 /* End of packet */
#define FM10K_RXD_STATUS_L4CS		0x0010 /* Indicates an L4 csum */
#define FM10K_RXD_STATUS_L4CS2		0x0040 /* Inner header L4 csum */
#define FM10K_RXD_STATUS_L4E2		0x0800 /* Inner header L4 csum err */
#define FM10K_RXD_STATUS_IPE2		0x1000 /* Inner header IPv4 csum err */
#define FM10K_RXD_STATUS_RXE		0x2000 /* Generic Rx error */
#define FM10K_RXD_STATUS_L4E		0x4000 /* L4 csum error */
#define FM10K_RXD_STATUS_IPE		0x8000 /* IPv4 csum error */

#define FM10K_RXD_ERR_SWITCH_ERROR	0x0001 /* Switch found bad packet */
#define FM10K_RXD_ERR_NO_DESCRIPTOR	0x0002 /* No descriptor available */
#define FM10K_RXD_ERR_PP_ERROR		0x0004 /* RAM error during processing */
#define FM10K_RXD_ERR_SWITCH_READY	0x0008 /* Link transition mid-packet */
#define FM10K_RXD_ERR_TOO_BIG		0x0010 /* Pkt too big for single buf */

struct fm10k_ftag {
	__be16 swpri_type_user;
	__be16 vlan;
	__be16 sglort;
	__be16 dglort;
};

#endif /* _FM10K_TYPE_H */
