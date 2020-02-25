/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB OR BSD-2-Clause */
/* Copyright (c) 2017-2019 Pensando Systems, Inc.  All rights reserved. */

#ifndef _IONIC_IF_H_
#define _IONIC_IF_H_

#pragma pack(push, 1)

#define IONIC_DEV_INFO_SIGNATURE		0x44455649      /* 'DEVI' */
#define IONIC_DEV_INFO_VERSION			1
#define IONIC_IFNAMSIZ				16

/**
 * Commands
 */
enum ionic_cmd_opcode {
	IONIC_CMD_NOP				= 0,

	/* Device commands */
	IONIC_CMD_IDENTIFY			= 1,
	IONIC_CMD_INIT				= 2,
	IONIC_CMD_RESET				= 3,
	IONIC_CMD_GETATTR			= 4,
	IONIC_CMD_SETATTR			= 5,

	/* Port commands */
	IONIC_CMD_PORT_IDENTIFY			= 10,
	IONIC_CMD_PORT_INIT			= 11,
	IONIC_CMD_PORT_RESET			= 12,
	IONIC_CMD_PORT_GETATTR			= 13,
	IONIC_CMD_PORT_SETATTR			= 14,

	/* LIF commands */
	IONIC_CMD_LIF_IDENTIFY			= 20,
	IONIC_CMD_LIF_INIT			= 21,
	IONIC_CMD_LIF_RESET			= 22,
	IONIC_CMD_LIF_GETATTR			= 23,
	IONIC_CMD_LIF_SETATTR			= 24,

	IONIC_CMD_RX_MODE_SET			= 30,
	IONIC_CMD_RX_FILTER_ADD			= 31,
	IONIC_CMD_RX_FILTER_DEL			= 32,

	/* Queue commands */
	IONIC_CMD_Q_INIT			= 40,
	IONIC_CMD_Q_CONTROL			= 41,

	/* RDMA commands */
	IONIC_CMD_RDMA_RESET_LIF		= 50,
	IONIC_CMD_RDMA_CREATE_EQ		= 51,
	IONIC_CMD_RDMA_CREATE_CQ		= 52,
	IONIC_CMD_RDMA_CREATE_ADMINQ		= 53,

	/* SR/IOV commands */
	IONIC_CMD_VF_GETATTR			= 60,
	IONIC_CMD_VF_SETATTR			= 61,

	/* QoS commands */
	IONIC_CMD_QOS_CLASS_IDENTIFY		= 240,
	IONIC_CMD_QOS_CLASS_INIT		= 241,
	IONIC_CMD_QOS_CLASS_RESET		= 242,

	/* Firmware commands */
	IONIC_CMD_FW_DOWNLOAD			= 254,
	IONIC_CMD_FW_CONTROL			= 255,
};

/**
 * Command Return codes
 */
enum ionic_status_code {
	IONIC_RC_SUCCESS	= 0,	/* Success */
	IONIC_RC_EVERSION	= 1,	/* Incorrect version for request */
	IONIC_RC_EOPCODE	= 2,	/* Invalid cmd opcode */
	IONIC_RC_EIO		= 3,	/* I/O error */
	IONIC_RC_EPERM		= 4,	/* Permission denied */
	IONIC_RC_EQID		= 5,	/* Bad qid */
	IONIC_RC_EQTYPE		= 6,	/* Bad qtype */
	IONIC_RC_ENOENT		= 7,	/* No such element */
	IONIC_RC_EINTR		= 8,	/* operation interrupted */
	IONIC_RC_EAGAIN		= 9,	/* Try again */
	IONIC_RC_ENOMEM		= 10,	/* Out of memory */
	IONIC_RC_EFAULT		= 11,	/* Bad address */
	IONIC_RC_EBUSY		= 12,	/* Device or resource busy */
	IONIC_RC_EEXIST		= 13,	/* object already exists */
	IONIC_RC_EINVAL		= 14,	/* Invalid argument */
	IONIC_RC_ENOSPC		= 15,	/* No space left or alloc failure */
	IONIC_RC_ERANGE		= 16,	/* Parameter out of range */
	IONIC_RC_BAD_ADDR	= 17,	/* Descriptor contains a bad ptr */
	IONIC_RC_DEV_CMD	= 18,	/* Device cmd attempted on AdminQ */
	IONIC_RC_ENOSUPP	= 19,	/* Operation not supported */
	IONIC_RC_ERROR		= 29,	/* Generic error */

	IONIC_RC_ERDMA		= 30,	/* Generic RDMA error */
};

enum ionic_notifyq_opcode {
	IONIC_EVENT_LINK_CHANGE		= 1,
	IONIC_EVENT_RESET		= 2,
	IONIC_EVENT_HEARTBEAT		= 3,
	IONIC_EVENT_LOG			= 4,
};

/**
 * struct cmd - General admin command format
 * @opcode:     Opcode for the command
 * @lif_index:  LIF index
 * @cmd_data:   Opcode-specific command bytes
 */
struct ionic_admin_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 lif_index;
	u8     cmd_data[60];
};

/**
 * struct ionic_admin_comp - General admin command completion format
 * @status:     The status of the command (enum status_code)
 * @comp_index: The index in the descriptor ring for which this
 *              is the completion.
 * @cmd_data:   Command-specific bytes.
 * @color:      Color bit.  (Always 0 for commands issued to the
 *              Device Cmd Registers.)
 */
struct ionic_admin_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	u8     cmd_data[11];
	u8     color;
#define IONIC_COMP_COLOR_MASK  0x80
};

static inline u8 color_match(u8 color, u8 done_color)
{
	return (!!(color & IONIC_COMP_COLOR_MASK)) == done_color;
}

/**
 * struct ionic_nop_cmd - NOP command
 * @opcode: opcode
 */
struct ionic_nop_cmd {
	u8 opcode;
	u8 rsvd[63];
};

/**
 * struct ionic_nop_comp - NOP command completion
 * @status: The status of the command (enum status_code)
 */
struct ionic_nop_comp {
	u8 status;
	u8 rsvd[15];
};

/**
 * struct ionic_dev_init_cmd - Device init command
 * @opcode:    opcode
 * @type:      device type
 */
struct ionic_dev_init_cmd {
	u8     opcode;
	u8     type;
	u8     rsvd[62];
};

/**
 * struct init_comp - Device init command completion
 * @status: The status of the command (enum status_code)
 */
struct ionic_dev_init_comp {
	u8 status;
	u8 rsvd[15];
};

/**
 * struct ionic_dev_reset_cmd - Device reset command
 * @opcode: opcode
 */
struct ionic_dev_reset_cmd {
	u8 opcode;
	u8 rsvd[63];
};

/**
 * struct reset_comp - Reset command completion
 * @status: The status of the command (enum status_code)
 */
struct ionic_dev_reset_comp {
	u8 status;
	u8 rsvd[15];
};

#define IONIC_IDENTITY_VERSION_1	1

/**
 * struct ionic_dev_identify_cmd - Driver/device identify command
 * @opcode:  opcode
 * @ver:     Highest version of identify supported by driver
 */
struct ionic_dev_identify_cmd {
	u8 opcode;
	u8 ver;
	u8 rsvd[62];
};

/**
 * struct dev_identify_comp - Driver/device identify command completion
 * @status: The status of the command (enum status_code)
 * @ver:    Version of identify returned by device
 */
struct ionic_dev_identify_comp {
	u8 status;
	u8 ver;
	u8 rsvd[14];
};

enum ionic_os_type {
	IONIC_OS_TYPE_LINUX   = 1,
	IONIC_OS_TYPE_WIN     = 2,
	IONIC_OS_TYPE_DPDK    = 3,
	IONIC_OS_TYPE_FREEBSD = 4,
	IONIC_OS_TYPE_IPXE    = 5,
	IONIC_OS_TYPE_ESXI    = 6,
};

/**
 * union drv_identity - driver identity information
 * @os_type:          OS type (see enum os_type)
 * @os_dist:          OS distribution, numeric format
 * @os_dist_str:      OS distribution, string format
 * @kernel_ver:       Kernel version, numeric format
 * @kernel_ver_str:   Kernel version, string format
 * @driver_ver_str:   Driver version, string format
 */
union ionic_drv_identity {
	struct {
		__le32 os_type;
		__le32 os_dist;
		char   os_dist_str[128];
		__le32 kernel_ver;
		char   kernel_ver_str[32];
		char   driver_ver_str[32];
	};
	__le32 words[512];
};

/**
 * union dev_identity - device identity information
 * @version:          Version of device identify
 * @type:             Identify type (0 for now)
 * @nports:           Number of ports provisioned
 * @nlifs:            Number of LIFs provisioned
 * @nintrs:           Number of interrupts provisioned
 * @ndbpgs_per_lif:   Number of doorbell pages per LIF
 * @intr_coal_mult:   Interrupt coalescing multiplication factor.
 *                    Scale user-supplied interrupt coalescing
 *                    value in usecs to device units using:
 *                    device units = usecs * mult / div
 * @intr_coal_div:    Interrupt coalescing division factor.
 *                    Scale user-supplied interrupt coalescing
 *                    value in usecs to device units using:
 *                    device units = usecs * mult / div
 *
 */
union ionic_dev_identity {
	struct {
		u8     version;
		u8     type;
		u8     rsvd[2];
		u8     nports;
		u8     rsvd2[3];
		__le32 nlifs;
		__le32 nintrs;
		__le32 ndbpgs_per_lif;
		__le32 intr_coal_mult;
		__le32 intr_coal_div;
	};
	__le32 words[512];
};

enum ionic_lif_type {
	IONIC_LIF_TYPE_CLASSIC = 0,
	IONIC_LIF_TYPE_MACVLAN = 1,
	IONIC_LIF_TYPE_NETQUEUE = 2,
};

/**
 * struct ionic_lif_identify_cmd - lif identify command
 * @opcode:  opcode
 * @type:    lif type (enum lif_type)
 * @ver:     version of identify returned by device
 */
struct ionic_lif_identify_cmd {
	u8 opcode;
	u8 type;
	u8 ver;
	u8 rsvd[61];
};

/**
 * struct ionic_lif_identify_comp - lif identify command completion
 * @status:  status of the command (enum status_code)
 * @ver:     version of identify returned by device
 */
struct ionic_lif_identify_comp {
	u8 status;
	u8 ver;
	u8 rsvd2[14];
};

enum ionic_lif_capability {
	IONIC_LIF_CAP_ETH        = BIT(0),
	IONIC_LIF_CAP_RDMA       = BIT(1),
};

/**
 * Logical Queue Types
 */
enum ionic_logical_qtype {
	IONIC_QTYPE_ADMINQ  = 0,
	IONIC_QTYPE_NOTIFYQ = 1,
	IONIC_QTYPE_RXQ     = 2,
	IONIC_QTYPE_TXQ     = 3,
	IONIC_QTYPE_EQ      = 4,
	IONIC_QTYPE_MAX     = 16,
};

/**
 * struct ionic_lif_logical_qtype - Descriptor of logical to hardware queue type.
 * @qtype:          Hardware Queue Type.
 * @qid_count:      Number of Queue IDs of the logical type.
 * @qid_base:       Minimum Queue ID of the logical type.
 */
struct ionic_lif_logical_qtype {
	u8     qtype;
	u8     rsvd[3];
	__le32 qid_count;
	__le32 qid_base;
};

enum ionic_lif_state {
	IONIC_LIF_DISABLE	= 0,
	IONIC_LIF_ENABLE	= 1,
	IONIC_LIF_HANG_RESET	= 2,
};

/**
 * LIF configuration
 * @state:          lif state (enum lif_state)
 * @name:           lif name
 * @mtu:            mtu
 * @mac:            station mac address
 * @features:       features (enum ionic_eth_hw_features)
 * @queue_count:    queue counts per queue-type
 */
union ionic_lif_config {
	struct {
		u8     state;
		u8     rsvd[3];
		char   name[IONIC_IFNAMSIZ];
		__le32 mtu;
		u8     mac[6];
		u8     rsvd2[2];
		__le64 features;
		__le32 queue_count[IONIC_QTYPE_MAX];
	};
	__le32 words[64];
};

/**
 * struct ionic_lif_identity - lif identity information (type-specific)
 *
 * @capabilities    LIF capabilities
 *
 * Ethernet:
 *     @version:          Ethernet identify structure version.
 *     @features:         Ethernet features supported on this lif type.
 *     @max_ucast_filters:  Number of perfect unicast addresses supported.
 *     @max_mcast_filters:  Number of perfect multicast addresses supported.
 *     @min_frame_size:   Minimum size of frames to be sent
 *     @max_frame_size:   Maximim size of frames to be sent
 *     @config:           LIF config struct with features, mtu, mac, q counts
 *
 * RDMA:
 *     @version:         RDMA version of opcodes and queue descriptors.
 *     @qp_opcodes:      Number of rdma queue pair opcodes supported.
 *     @admin_opcodes:   Number of rdma admin opcodes supported.
 *     @npts_per_lif:    Page table size per lif
 *     @nmrs_per_lif:    Number of memory regions per lif
 *     @nahs_per_lif:    Number of address handles per lif
 *     @max_stride:      Max work request stride.
 *     @cl_stride:       Cache line stride.
 *     @pte_stride:      Page table entry stride.
 *     @rrq_stride:      Remote RQ work request stride.
 *     @rsq_stride:      Remote SQ work request stride.
 *     @dcqcn_profiles:  Number of DCQCN profiles
 *     @aq_qtype:        RDMA Admin Qtype.
 *     @sq_qtype:        RDMA Send Qtype.
 *     @rq_qtype:        RDMA Receive Qtype.
 *     @cq_qtype:        RDMA Completion Qtype.
 *     @eq_qtype:        RDMA Event Qtype.
 */
union ionic_lif_identity {
	struct {
		__le64 capabilities;

		struct {
			u8 version;
			u8 rsvd[3];
			__le32 max_ucast_filters;
			__le32 max_mcast_filters;
			__le16 rss_ind_tbl_sz;
			__le32 min_frame_size;
			__le32 max_frame_size;
			u8 rsvd2[106];
			union ionic_lif_config config;
		} eth;

		struct {
			u8 version;
			u8 qp_opcodes;
			u8 admin_opcodes;
			u8 rsvd;
			__le32 npts_per_lif;
			__le32 nmrs_per_lif;
			__le32 nahs_per_lif;
			u8 max_stride;
			u8 cl_stride;
			u8 pte_stride;
			u8 rrq_stride;
			u8 rsq_stride;
			u8 dcqcn_profiles;
			u8 rsvd_dimensions[10];
			struct ionic_lif_logical_qtype aq_qtype;
			struct ionic_lif_logical_qtype sq_qtype;
			struct ionic_lif_logical_qtype rq_qtype;
			struct ionic_lif_logical_qtype cq_qtype;
			struct ionic_lif_logical_qtype eq_qtype;
		} rdma;
	};
	__le32 words[512];
};

/**
 * struct ionic_lif_init_cmd - LIF init command
 * @opcode:       opcode
 * @type:         LIF type (enum lif_type)
 * @index:        LIF index
 * @info_pa:      destination address for lif info (struct ionic_lif_info)
 */
struct ionic_lif_init_cmd {
	u8     opcode;
	u8     type;
	__le16 index;
	__le32 rsvd;
	__le64 info_pa;
	u8     rsvd2[48];
};

/**
 * struct ionic_lif_init_comp - LIF init command completion
 * @status: The status of the command (enum status_code)
 */
struct ionic_lif_init_comp {
	u8 status;
	u8 rsvd;
	__le16 hw_index;
	u8 rsvd2[12];
};

/**
 * struct ionic_q_init_cmd - Queue init command
 * @opcode:       opcode
 * @type:         Logical queue type
 * @ver:          Queue version (defines opcode/descriptor scope)
 * @lif_index:    LIF index
 * @index:        (lif, qtype) relative admin queue index
 * @intr_index:   Interrupt control register index
 * @pid:          Process ID
 * @flags:
 *    IRQ:        Interrupt requested on completion
 *    ENA:        Enable the queue.  If ENA=0 the queue is initialized
 *                but remains disabled, to be later enabled with the
 *                Queue Enable command.  If ENA=1, then queue is
 *                initialized and then enabled.
 *    SG:         Enable Scatter-Gather on the queue.
 *                in number of descs.  The actual ring size is
 *                (1 << ring_size).  For example, to
 *                select a ring size of 64 descriptors write
 *                ring_size = 6.  The minimum ring_size value is 2
 *                for a ring size of 4 descriptors.  The maximum
 *                ring_size value is 16 for a ring size of 64k
 *                descriptors.  Values of ring_size <2 and >16 are
 *                reserved.
 *    EQ:         Enable the Event Queue
 * @cos:          Class of service for this queue.
 * @ring_size:    Queue ring size, encoded as a log2(size)
 * @ring_base:    Queue ring base address
 * @cq_ring_base: Completion queue ring base address
 * @sg_ring_base: Scatter/Gather ring base address
 * @eq_index:	  Event queue index
 */
struct ionic_q_init_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 lif_index;
	u8     type;
	u8     ver;
	u8     rsvd1[2];
	__le32 index;
	__le16 pid;
	__le16 intr_index;
	__le16 flags;
#define IONIC_QINIT_F_IRQ	0x01	/* Request interrupt on completion */
#define IONIC_QINIT_F_ENA	0x02	/* Enable the queue */
#define IONIC_QINIT_F_SG	0x04	/* Enable scatter/gather on the queue */
#define IONIC_QINIT_F_EQ	0x08	/* Enable event queue */
#define IONIC_QINIT_F_DEBUG 0x80	/* Enable queue debugging */
	u8     cos;
	u8     ring_size;
	__le64 ring_base;
	__le64 cq_ring_base;
	__le64 sg_ring_base;
	__le32 eq_index;
	u8     rsvd2[16];
};

/**
 * struct ionic_q_init_comp - Queue init command completion
 * @status:     The status of the command (enum status_code)
 * @ver:        Queue version (defines opcode/descriptor scope)
 * @comp_index: The index in the descriptor ring for which this
 *              is the completion.
 * @hw_index:   Hardware Queue ID
 * @hw_type:    Hardware Queue type
 * @color:      Color
 */
struct ionic_q_init_comp {
	u8     status;
	u8     ver;
	__le16 comp_index;
	__le32 hw_index;
	u8     hw_type;
	u8     rsvd2[6];
	u8     color;
};

/* the device's internal addressing uses up to 52 bits */
#define IONIC_ADDR_LEN		52
#define IONIC_ADDR_MASK		(BIT_ULL(IONIC_ADDR_LEN) - 1)

enum ionic_txq_desc_opcode {
	IONIC_TXQ_DESC_OPCODE_CSUM_NONE = 0,
	IONIC_TXQ_DESC_OPCODE_CSUM_PARTIAL = 1,
	IONIC_TXQ_DESC_OPCODE_CSUM_HW = 2,
	IONIC_TXQ_DESC_OPCODE_TSO = 3,
};

/**
 * struct ionic_txq_desc - Ethernet Tx queue descriptor format
 * @opcode:       Tx operation, see TXQ_DESC_OPCODE_*:
 *
 *                   IONIC_TXQ_DESC_OPCODE_CSUM_NONE:
 *
 *                      Non-offload send.  No segmentation,
 *                      fragmentation or checksum calc/insertion is
 *                      performed by device; packet is prepared
 *                      to send by software stack and requires
 *                      no further manipulation from device.
 *
 *                   IONIC_TXQ_DESC_OPCODE_CSUM_PARTIAL:
 *
 *                      Offload 16-bit L4 checksum
 *                      calculation/insertion.  The device will
 *                      calculate the L4 checksum value and
 *                      insert the result in the packet's L4
 *                      header checksum field.  The L4 checksum
 *                      is calculated starting at @csum_start bytes
 *                      into the packet to the end of the packet.
 *                      The checksum insertion position is given
 *                      in @csum_offset.  This feature is only
 *                      applicable to protocols such as TCP, UDP
 *                      and ICMP where a standard (i.e. the
 *                      'IP-style' checksum) one's complement
 *                      16-bit checksum is used, using an IP
 *                      pseudo-header to seed the calculation.
 *                      Software will preload the L4 checksum
 *                      field with the IP pseudo-header checksum.
 *
 *                      For tunnel encapsulation, @csum_start and
 *                      @csum_offset refer to the inner L4
 *                      header.  Supported tunnels encapsulations
 *                      are: IPIP, GRE, and UDP.  If the @encap
 *                      is clear, no further processing by the
 *                      device is required; software will
 *                      calculate the outer header checksums.  If
 *                      the @encap is set, the device will
 *                      offload the outer header checksums using
 *                      LCO (local checksum offload) (see
 *                      Documentation/networking/checksum-offloads.rst
 *                      for more info).
 *
 *                   IONIC_TXQ_DESC_OPCODE_CSUM_HW:
 *
 *                      Offload 16-bit checksum computation to hardware.
 *                      If @csum_l3 is set then the packet's L3 checksum is
 *                      updated. Similarly, if @csum_l4 is set the the L4
 *                      checksum is updated. If @encap is set then encap header
 *                      checksums are also updated.
 *
 *                   IONIC_TXQ_DESC_OPCODE_TSO:
 *
 *                      Device preforms TCP segmentation offload
 *                      (TSO).  @hdr_len is the number of bytes
 *                      to the end of TCP header (the offset to
 *                      the TCP payload).  @mss is the desired
 *                      MSS, the TCP payload length for each
 *                      segment.  The device will calculate/
 *                      insert IP (IPv4 only) and TCP checksums
 *                      for each segment.  In the first data
 *                      buffer containing the header template,
 *                      the driver will set IPv4 checksum to 0
 *                      and preload TCP checksum with the IP
 *                      pseudo header calculated with IP length = 0.
 *
 *                      Supported tunnel encapsulations are IPIP,
 *                      layer-3 GRE, and UDP. @hdr_len includes
 *                      both outer and inner headers.  The driver
 *                      will set IPv4 checksum to zero and
 *                      preload TCP checksum with IP pseudo
 *                      header on the inner header.
 *
 *                      TCP ECN offload is supported.  The device
 *                      will set CWR flag in the first segment if
 *                      CWR is set in the template header, and
 *                      clear CWR in remaining segments.
 * @flags:
 *                vlan:
 *                    Insert an L2 VLAN header using @vlan_tci.
 *                encap:
 *                    Calculate encap header checksum.
 *                csum_l3:
 *                    Compute L3 header checksum.
 *                csum_l4:
 *                    Compute L4 header checksum.
 *                tso_sot:
 *                    TSO start
 *                tso_eot:
 *                    TSO end
 * @num_sg_elems: Number of scatter-gather elements in SG
 *                descriptor
 * @addr:         First data buffer's DMA address.
 *                (Subsequent data buffers are on txq_sg_desc).
 * @len:          First data buffer's length, in bytes
 * @vlan_tci:     VLAN tag to insert in the packet (if requested
 *                by @V-bit).  Includes .1p and .1q tags
 * @hdr_len:      Length of packet headers, including
 *                encapsulating outer header, if applicable.
 *                Valid for opcodes TXQ_DESC_OPCODE_CALC_CSUM and
 *                TXQ_DESC_OPCODE_TSO.  Should be set to zero for
 *                all other modes.  For
 *                TXQ_DESC_OPCODE_CALC_CSUM, @hdr_len is length
 *                of headers up to inner-most L4 header.  For
 *                TXQ_DESC_OPCODE_TSO, @hdr_len is up to
 *                inner-most L4 payload, so inclusive of
 *                inner-most L4 header.
 * @mss:          Desired MSS value for TSO.  Only applicable for
 *                TXQ_DESC_OPCODE_TSO.
 * @csum_start:   Offset into inner-most L3 header of checksum
 * @csum_offset:  Offset into inner-most L4 header of checksum
 */

#define IONIC_TXQ_DESC_OPCODE_MASK		0xf
#define IONIC_TXQ_DESC_OPCODE_SHIFT		4
#define IONIC_TXQ_DESC_FLAGS_MASK		0xf
#define IONIC_TXQ_DESC_FLAGS_SHIFT		0
#define IONIC_TXQ_DESC_NSGE_MASK		0xf
#define IONIC_TXQ_DESC_NSGE_SHIFT		8
#define IONIC_TXQ_DESC_ADDR_MASK		(BIT_ULL(IONIC_ADDR_LEN) - 1)
#define IONIC_TXQ_DESC_ADDR_SHIFT		12

/* common flags */
#define IONIC_TXQ_DESC_FLAG_VLAN		0x1
#define IONIC_TXQ_DESC_FLAG_ENCAP		0x2

/* flags for csum_hw opcode */
#define IONIC_TXQ_DESC_FLAG_CSUM_L3		0x4
#define IONIC_TXQ_DESC_FLAG_CSUM_L4		0x8

/* flags for tso opcode */
#define IONIC_TXQ_DESC_FLAG_TSO_SOT		0x4
#define IONIC_TXQ_DESC_FLAG_TSO_EOT		0x8

struct ionic_txq_desc {
	__le64  cmd;
	__le16  len;
	union {
		__le16  vlan_tci;
		__le16  hword0;
	};
	union {
		__le16  csum_start;
		__le16  hdr_len;
		__le16  hword1;
	};
	union {
		__le16  csum_offset;
		__le16  mss;
		__le16  hword2;
	};
};

static inline u64 encode_txq_desc_cmd(u8 opcode, u8 flags,
				      u8 nsge, u64 addr)
{
	u64 cmd;

	cmd = (opcode & IONIC_TXQ_DESC_OPCODE_MASK) << IONIC_TXQ_DESC_OPCODE_SHIFT;
	cmd |= (flags & IONIC_TXQ_DESC_FLAGS_MASK) << IONIC_TXQ_DESC_FLAGS_SHIFT;
	cmd |= (nsge & IONIC_TXQ_DESC_NSGE_MASK) << IONIC_TXQ_DESC_NSGE_SHIFT;
	cmd |= (addr & IONIC_TXQ_DESC_ADDR_MASK) << IONIC_TXQ_DESC_ADDR_SHIFT;

	return cmd;
};

static inline void decode_txq_desc_cmd(u64 cmd, u8 *opcode, u8 *flags,
				       u8 *nsge, u64 *addr)
{
	*opcode = (cmd >> IONIC_TXQ_DESC_OPCODE_SHIFT) & IONIC_TXQ_DESC_OPCODE_MASK;
	*flags = (cmd >> IONIC_TXQ_DESC_FLAGS_SHIFT) & IONIC_TXQ_DESC_FLAGS_MASK;
	*nsge = (cmd >> IONIC_TXQ_DESC_NSGE_SHIFT) & IONIC_TXQ_DESC_NSGE_MASK;
	*addr = (cmd >> IONIC_TXQ_DESC_ADDR_SHIFT) & IONIC_TXQ_DESC_ADDR_MASK;
};

#define IONIC_TX_MAX_SG_ELEMS	8
#define IONIC_RX_MAX_SG_ELEMS	8

/**
 * struct ionic_txq_sg_desc - Transmit scatter-gather (SG) list
 * @addr:      DMA address of SG element data buffer
 * @len:       Length of SG element data buffer, in bytes
 */
struct ionic_txq_sg_desc {
	struct ionic_txq_sg_elem {
		__le64 addr;
		__le16 len;
		__le16 rsvd[3];
	} elems[IONIC_TX_MAX_SG_ELEMS];
};

/**
 * struct ionic_txq_comp - Ethernet transmit queue completion descriptor
 * @status:     The status of the command (enum status_code)
 * @comp_index: The index in the descriptor ring for which this
 *                 is the completion.
 * @color:      Color bit.
 */
struct ionic_txq_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	u8     rsvd2[11];
	u8     color;
};

enum ionic_rxq_desc_opcode {
	IONIC_RXQ_DESC_OPCODE_SIMPLE = 0,
	IONIC_RXQ_DESC_OPCODE_SG = 1,
};

/**
 * struct ionic_rxq_desc - Ethernet Rx queue descriptor format
 * @opcode:       Rx operation, see RXQ_DESC_OPCODE_*:
 *
 *                   RXQ_DESC_OPCODE_SIMPLE:
 *
 *                      Receive full packet into data buffer
 *                      starting at @addr.  Results of
 *                      receive, including actual bytes received,
 *                      are recorded in Rx completion descriptor.
 *
 * @len:          Data buffer's length, in bytes.
 * @addr:         Data buffer's DMA address
 */
struct ionic_rxq_desc {
	u8     opcode;
	u8     rsvd[5];
	__le16 len;
	__le64 addr;
};

/**
 * struct ionic_rxq_sg_desc - Receive scatter-gather (SG) list
 * @addr:      DMA address of SG element data buffer
 * @len:       Length of SG element data buffer, in bytes
 */
struct ionic_rxq_sg_desc {
	struct ionic_rxq_sg_elem {
		__le64 addr;
		__le16 len;
		__le16 rsvd[3];
	} elems[IONIC_RX_MAX_SG_ELEMS];
};

/**
 * struct ionic_rxq_comp - Ethernet receive queue completion descriptor
 * @status:       The status of the command (enum status_code)
 * @num_sg_elems: Number of SG elements used by this descriptor
 * @comp_index:   The index in the descriptor ring for which this
 *                is the completion.
 * @rss_hash:     32-bit RSS hash
 * @csum:         16-bit sum of the packet's L2 payload.
 *                If the packet's L2 payload is odd length, an extra
 *                zero-value byte is included in the @csum calculation but
 *                not included in @len.
 * @vlan_tci:     VLAN tag stripped from the packet.  Valid if @VLAN is
 *                set.  Includes .1p and .1q tags.
 * @len:          Received packet length, in bytes.  Excludes FCS.
 * @csum_calc     L2 payload checksum is computed or not
 * @csum_tcp_ok:  The TCP checksum calculated by the device
 *                matched the checksum in the receive packet's
 *                TCP header
 * @csum_tcp_bad: The TCP checksum calculated by the device did
 *                not match the checksum in the receive packet's
 *                TCP header.
 * @csum_udp_ok:  The UDP checksum calculated by the device
 *                matched the checksum in the receive packet's
 *                UDP header
 * @csum_udp_bad: The UDP checksum calculated by the device did
 *                not match the checksum in the receive packet's
 *                UDP header.
 * @csum_ip_ok:   The IPv4 checksum calculated by the device
 *                matched the checksum in the receive packet's
 *                first IPv4 header.  If the receive packet
 *                contains both a tunnel IPv4 header and a
 *                transport IPv4 header, the device validates the
 *                checksum for the both IPv4 headers.
 * @csum_ip_bad:  The IPv4 checksum calculated by the device did
 *                not match the checksum in the receive packet's
 *                first IPv4 header. If the receive packet
 *                contains both a tunnel IPv4 header and a
 *                transport IPv4 header, the device validates the
 *                checksum for both IP headers.
 * @VLAN:         VLAN header was stripped and placed in @vlan_tci.
 * @pkt_type:     Packet type
 * @color:        Color bit.
 */
struct ionic_rxq_comp {
	u8     status;
	u8     num_sg_elems;
	__le16 comp_index;
	__le32 rss_hash;
	__le16 csum;
	__le16 vlan_tci;
	__le16 len;
	u8     csum_flags;
#define IONIC_RXQ_COMP_CSUM_F_TCP_OK	0x01
#define IONIC_RXQ_COMP_CSUM_F_TCP_BAD	0x02
#define IONIC_RXQ_COMP_CSUM_F_UDP_OK	0x04
#define IONIC_RXQ_COMP_CSUM_F_UDP_BAD	0x08
#define IONIC_RXQ_COMP_CSUM_F_IP_OK	0x10
#define IONIC_RXQ_COMP_CSUM_F_IP_BAD	0x20
#define IONIC_RXQ_COMP_CSUM_F_VLAN	0x40
#define IONIC_RXQ_COMP_CSUM_F_CALC	0x80
	u8     pkt_type_color;
#define IONIC_RXQ_COMP_PKT_TYPE_MASK	0x7f
};

enum ionic_pkt_type {
	IONIC_PKT_TYPE_NON_IP     = 0x000,
	IONIC_PKT_TYPE_IPV4       = 0x001,
	IONIC_PKT_TYPE_IPV4_TCP   = 0x003,
	IONIC_PKT_TYPE_IPV4_UDP   = 0x005,
	IONIC_PKT_TYPE_IPV6       = 0x008,
	IONIC_PKT_TYPE_IPV6_TCP   = 0x018,
	IONIC_PKT_TYPE_IPV6_UDP   = 0x028,
};

enum ionic_eth_hw_features {
	IONIC_ETH_HW_VLAN_TX_TAG	= BIT(0),
	IONIC_ETH_HW_VLAN_RX_STRIP	= BIT(1),
	IONIC_ETH_HW_VLAN_RX_FILTER	= BIT(2),
	IONIC_ETH_HW_RX_HASH		= BIT(3),
	IONIC_ETH_HW_RX_CSUM		= BIT(4),
	IONIC_ETH_HW_TX_SG		= BIT(5),
	IONIC_ETH_HW_RX_SG		= BIT(6),
	IONIC_ETH_HW_TX_CSUM		= BIT(7),
	IONIC_ETH_HW_TSO		= BIT(8),
	IONIC_ETH_HW_TSO_IPV6		= BIT(9),
	IONIC_ETH_HW_TSO_ECN		= BIT(10),
	IONIC_ETH_HW_TSO_GRE		= BIT(11),
	IONIC_ETH_HW_TSO_GRE_CSUM	= BIT(12),
	IONIC_ETH_HW_TSO_IPXIP4	= BIT(13),
	IONIC_ETH_HW_TSO_IPXIP6	= BIT(14),
	IONIC_ETH_HW_TSO_UDP		= BIT(15),
	IONIC_ETH_HW_TSO_UDP_CSUM	= BIT(16),
};

/**
 * struct ionic_q_control_cmd - Queue control command
 * @opcode:     opcode
 * @type:       Queue type
 * @lif_index:  LIF index
 * @index:      Queue index
 * @oper:       Operation (enum q_control_oper)
 */
struct ionic_q_control_cmd {
	u8     opcode;
	u8     type;
	__le16 lif_index;
	__le32 index;
	u8     oper;
	u8     rsvd[55];
};

typedef struct ionic_admin_comp ionic_q_control_comp;

enum q_control_oper {
	IONIC_Q_DISABLE		= 0,
	IONIC_Q_ENABLE		= 1,
	IONIC_Q_HANG_RESET	= 2,
};

/**
 * Physical connection type
 */
enum ionic_phy_type {
	IONIC_PHY_TYPE_NONE	= 0,
	IONIC_PHY_TYPE_COPPER	= 1,
	IONIC_PHY_TYPE_FIBER	= 2,
};

/**
 * Transceiver status
 */
enum ionic_xcvr_state {
	IONIC_XCVR_STATE_REMOVED	 = 0,
	IONIC_XCVR_STATE_INSERTED	 = 1,
	IONIC_XCVR_STATE_PENDING	 = 2,
	IONIC_XCVR_STATE_SPROM_READ	 = 3,
	IONIC_XCVR_STATE_SPROM_READ_ERR  = 4,
};

/**
 * Supported link modes
 */
enum ionic_xcvr_pid {
	IONIC_XCVR_PID_UNKNOWN           = 0,

	/* CU */
	IONIC_XCVR_PID_QSFP_100G_CR4     = 1,
	IONIC_XCVR_PID_QSFP_40GBASE_CR4  = 2,
	IONIC_XCVR_PID_SFP_25GBASE_CR_S  = 3,
	IONIC_XCVR_PID_SFP_25GBASE_CR_L  = 4,
	IONIC_XCVR_PID_SFP_25GBASE_CR_N  = 5,

	/* Fiber */
	IONIC_XCVR_PID_QSFP_100G_AOC    = 50,
	IONIC_XCVR_PID_QSFP_100G_ACC    = 51,
	IONIC_XCVR_PID_QSFP_100G_SR4    = 52,
	IONIC_XCVR_PID_QSFP_100G_LR4    = 53,
	IONIC_XCVR_PID_QSFP_100G_ER4    = 54,
	IONIC_XCVR_PID_QSFP_40GBASE_ER4 = 55,
	IONIC_XCVR_PID_QSFP_40GBASE_SR4 = 56,
	IONIC_XCVR_PID_QSFP_40GBASE_LR4 = 57,
	IONIC_XCVR_PID_QSFP_40GBASE_AOC = 58,
	IONIC_XCVR_PID_SFP_25GBASE_SR   = 59,
	IONIC_XCVR_PID_SFP_25GBASE_LR   = 60,
	IONIC_XCVR_PID_SFP_25GBASE_ER   = 61,
	IONIC_XCVR_PID_SFP_25GBASE_AOC  = 62,
	IONIC_XCVR_PID_SFP_10GBASE_SR   = 63,
	IONIC_XCVR_PID_SFP_10GBASE_LR   = 64,
	IONIC_XCVR_PID_SFP_10GBASE_LRM  = 65,
	IONIC_XCVR_PID_SFP_10GBASE_ER   = 66,
	IONIC_XCVR_PID_SFP_10GBASE_AOC  = 67,
	IONIC_XCVR_PID_SFP_10GBASE_CU   = 68,
	IONIC_XCVR_PID_QSFP_100G_CWDM4  = 69,
	IONIC_XCVR_PID_QSFP_100G_PSM4   = 70,
};

/**
 * Port types
 */
enum ionic_port_type {
	IONIC_PORT_TYPE_NONE = 0,  /* port type not configured */
	IONIC_PORT_TYPE_ETH  = 1,  /* port carries ethernet traffic (inband) */
	IONIC_PORT_TYPE_MGMT = 2,  /* port carries mgmt traffic (out-of-band) */
};

/**
 * Port config state
 */
enum ionic_port_admin_state {
	IONIC_PORT_ADMIN_STATE_NONE = 0,   /* port admin state not configured */
	IONIC_PORT_ADMIN_STATE_DOWN = 1,   /* port is admin disabled */
	IONIC_PORT_ADMIN_STATE_UP   = 2,   /* port is admin enabled */
};

/**
 * Port operational status
 */
enum ionic_port_oper_status {
	IONIC_PORT_OPER_STATUS_NONE  = 0,	/* port is disabled */
	IONIC_PORT_OPER_STATUS_UP    = 1,	/* port is linked up */
	IONIC_PORT_OPER_STATUS_DOWN  = 2,	/* port link status is down */
};

/**
 * Ethernet Forward error correction (fec) modes
 */
enum ionic_port_fec_type {
	IONIC_PORT_FEC_TYPE_NONE = 0,		/* Disabled */
	IONIC_PORT_FEC_TYPE_FC   = 1,		/* FireCode */
	IONIC_PORT_FEC_TYPE_RS   = 2,		/* ReedSolomon */
};

/**
 * Ethernet pause (flow control) modes
 */
enum ionic_port_pause_type {
	IONIC_PORT_PAUSE_TYPE_NONE = 0,	/* Disable Pause */
	IONIC_PORT_PAUSE_TYPE_LINK = 1,	/* Link level pause */
	IONIC_PORT_PAUSE_TYPE_PFC  = 2,	/* Priority-Flow control */
};

/**
 * Loopback modes
 */
enum ionic_port_loopback_mode {
	IONIC_PORT_LOOPBACK_MODE_NONE = 0,	/* Disable loopback */
	IONIC_PORT_LOOPBACK_MODE_MAC  = 1,	/* MAC loopback */
	IONIC_PORT_LOOPBACK_MODE_PHY  = 2,	/* PHY/Serdes loopback */
};

/**
 * Transceiver Status information
 * @state:    Transceiver status (enum ionic_xcvr_state)
 * @phy:      Physical connection type (enum ionic_phy_type)
 * @pid:      Transceiver link mode (enum pid)
 * @sprom:    Transceiver sprom contents
 */
struct ionic_xcvr_status {
	u8     state;
	u8     phy;
	__le16 pid;
	u8     sprom[256];
};

/**
 * Port configuration
 * @speed:              port speed (in Mbps)
 * @mtu:                mtu
 * @state:              port admin state (enum port_admin_state)
 * @an_enable:          autoneg enable
 * @fec_type:           fec type (enum ionic_port_fec_type)
 * @pause_type:         pause type (enum ionic_port_pause_type)
 * @loopback_mode:      loopback mode (enum ionic_port_loopback_mode)
 */
union ionic_port_config {
	struct {
#define IONIC_SPEED_100G	100000	/* 100G in Mbps */
#define IONIC_SPEED_50G		50000	/* 50G in Mbps */
#define IONIC_SPEED_40G		40000	/* 40G in Mbps */
#define IONIC_SPEED_25G		25000	/* 25G in Mbps */
#define IONIC_SPEED_10G		10000	/* 10G in Mbps */
#define IONIC_SPEED_1G		1000	/* 1G in Mbps */
		__le32 speed;
		__le32 mtu;
		u8     state;
		u8     an_enable;
		u8     fec_type;
#define IONIC_PAUSE_TYPE_MASK		0x0f
#define IONIC_PAUSE_FLAGS_MASK		0xf0
#define IONIC_PAUSE_F_TX		0x10
#define IONIC_PAUSE_F_RX		0x20
		u8     pause_type;
		u8     loopback_mode;
	};
	__le32 words[64];
};

/**
 * Port Status information
 * @status:             link status (enum ionic_port_oper_status)
 * @id:                 port id
 * @speed:              link speed (in Mbps)
 * @xcvr:               tranceiver status
 */
struct ionic_port_status {
	__le32 id;
	__le32 speed;
	u8     status;
	u8     rsvd[51];
	struct ionic_xcvr_status  xcvr;
};

/**
 * struct ionic_port_identify_cmd - Port identify command
 * @opcode:     opcode
 * @index:      port index
 * @ver:        Highest version of identify supported by driver
 */
struct ionic_port_identify_cmd {
	u8 opcode;
	u8 index;
	u8 ver;
	u8 rsvd[61];
};

/**
 * struct ionic_port_identify_comp - Port identify command completion
 * @status: The status of the command (enum status_code)
 * @ver:    Version of identify returned by device
 */
struct ionic_port_identify_comp {
	u8 status;
	u8 ver;
	u8 rsvd[14];
};

/**
 * struct ionic_port_init_cmd - Port initialization command
 * @opcode:     opcode
 * @index:      port index
 * @info_pa:    destination address for port info (struct ionic_port_info)
 */
struct ionic_port_init_cmd {
	u8     opcode;
	u8     index;
	u8     rsvd[6];
	__le64 info_pa;
	u8     rsvd2[48];
};

/**
 * struct ionic_port_init_comp - Port initialization command completion
 * @status: The status of the command (enum status_code)
 */
struct ionic_port_init_comp {
	u8 status;
	u8 rsvd[15];
};

/**
 * struct ionic_port_reset_cmd - Port reset command
 * @opcode:     opcode
 * @index:      port index
 */
struct ionic_port_reset_cmd {
	u8 opcode;
	u8 index;
	u8 rsvd[62];
};

/**
 * struct ionic_port_reset_comp - Port reset command completion
 * @status: The status of the command (enum status_code)
 */
struct ionic_port_reset_comp {
	u8 status;
	u8 rsvd[15];
};

/**
 * enum stats_ctl_cmd - List of commands for stats control
 */
enum ionic_stats_ctl_cmd {
	IONIC_STATS_CTL_RESET		= 0,
};


/**
 * enum ionic_port_attr - List of device attributes
 */
enum ionic_port_attr {
	IONIC_PORT_ATTR_STATE		= 0,
	IONIC_PORT_ATTR_SPEED		= 1,
	IONIC_PORT_ATTR_MTU		= 2,
	IONIC_PORT_ATTR_AUTONEG		= 3,
	IONIC_PORT_ATTR_FEC		= 4,
	IONIC_PORT_ATTR_PAUSE		= 5,
	IONIC_PORT_ATTR_LOOPBACK	= 6,
	IONIC_PORT_ATTR_STATS_CTRL	= 7,
};

/**
 * struct ionic_port_setattr_cmd - Set port attributes on the NIC
 * @opcode:     Opcode
 * @index:      port index
 * @attr:       Attribute type (enum ionic_port_attr)
 */
struct ionic_port_setattr_cmd {
	u8     opcode;
	u8     index;
	u8     attr;
	u8     rsvd;
	union {
		u8      state;
		__le32  speed;
		__le32  mtu;
		u8      an_enable;
		u8      fec_type;
		u8      pause_type;
		u8      loopback_mode;
		u8	stats_ctl;
		u8      rsvd2[60];
	};
};

/**
 * struct ionic_port_setattr_comp - Port set attr command completion
 * @status:     The status of the command (enum status_code)
 * @color:      Color bit
 */
struct ionic_port_setattr_comp {
	u8     status;
	u8     rsvd[14];
	u8     color;
};

/**
 * struct ionic_port_getattr_cmd - Get port attributes from the NIC
 * @opcode:     Opcode
 * @index:      port index
 * @attr:       Attribute type (enum ionic_port_attr)
 */
struct ionic_port_getattr_cmd {
	u8     opcode;
	u8     index;
	u8     attr;
	u8     rsvd[61];
};

/**
 * struct ionic_port_getattr_comp - Port get attr command completion
 * @status:     The status of the command (enum status_code)
 * @color:      Color bit
 */
struct ionic_port_getattr_comp {
	u8     status;
	u8     rsvd[3];
	union {
		u8      state;
		__le32  speed;
		__le32  mtu;
		u8      an_enable;
		u8      fec_type;
		u8      pause_type;
		u8      loopback_mode;
		u8      rsvd2[11];
	};
	u8     color;
};

/**
 * struct ionic_lif_status - Lif status register
 * @eid:             most recent NotifyQ event id
 * @port_num:        port the lif is connected to
 * @link_status:     port status (enum ionic_port_oper_status)
 * @link_speed:      speed of link in Mbps
 * @link_down_count: number of times link status changes
 */
struct ionic_lif_status {
	__le64 eid;
	u8     port_num;
	u8     rsvd;
	__le16 link_status;
	__le32 link_speed;		/* units of 1Mbps: eg 10000 = 10Gbps */
	__le16 link_down_count;
	u8      rsvd2[46];
};

/**
 * struct ionic_lif_reset_cmd - LIF reset command
 * @opcode:    opcode
 * @index:     LIF index
 */
struct ionic_lif_reset_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 index;
	__le32 rsvd2[15];
};

typedef struct ionic_admin_comp ionic_lif_reset_comp;

enum ionic_dev_state {
	IONIC_DEV_DISABLE	= 0,
	IONIC_DEV_ENABLE	= 1,
	IONIC_DEV_HANG_RESET	= 2,
};

/**
 * enum ionic_dev_attr - List of device attributes
 */
enum ionic_dev_attr {
	IONIC_DEV_ATTR_STATE    = 0,
	IONIC_DEV_ATTR_NAME     = 1,
	IONIC_DEV_ATTR_FEATURES = 2,
};

/**
 * struct ionic_dev_setattr_cmd - Set Device attributes on the NIC
 * @opcode:     Opcode
 * @attr:       Attribute type (enum ionic_dev_attr)
 * @state:      Device state (enum ionic_dev_state)
 * @name:       The bus info, e.g. PCI slot-device-function, 0 terminated
 * @features:   Device features
 */
struct ionic_dev_setattr_cmd {
	u8     opcode;
	u8     attr;
	__le16 rsvd;
	union {
		u8      state;
		char    name[IONIC_IFNAMSIZ];
		__le64  features;
		u8      rsvd2[60];
	};
};

/**
 * struct ionic_dev_setattr_comp - Device set attr command completion
 * @status:     The status of the command (enum status_code)
 * @features:   Device features
 * @color:      Color bit
 */
struct ionic_dev_setattr_comp {
	u8     status;
	u8     rsvd[3];
	union {
		__le64  features;
		u8      rsvd2[11];
	};
	u8     color;
};

/**
 * struct ionic_dev_getattr_cmd - Get Device attributes from the NIC
 * @opcode:     opcode
 * @attr:       Attribute type (enum ionic_dev_attr)
 */
struct ionic_dev_getattr_cmd {
	u8     opcode;
	u8     attr;
	u8     rsvd[62];
};

/**
 * struct ionic_dev_setattr_comp - Device set attr command completion
 * @status:     The status of the command (enum status_code)
 * @features:   Device features
 * @color:      Color bit
 */
struct ionic_dev_getattr_comp {
	u8     status;
	u8     rsvd[3];
	union {
		__le64  features;
		u8      rsvd2[11];
	};
	u8     color;
};

/**
 * RSS parameters
 */
#define IONIC_RSS_HASH_KEY_SIZE		40

enum ionic_rss_hash_types {
	IONIC_RSS_TYPE_IPV4	= BIT(0),
	IONIC_RSS_TYPE_IPV4_TCP	= BIT(1),
	IONIC_RSS_TYPE_IPV4_UDP	= BIT(2),
	IONIC_RSS_TYPE_IPV6	= BIT(3),
	IONIC_RSS_TYPE_IPV6_TCP	= BIT(4),
	IONIC_RSS_TYPE_IPV6_UDP	= BIT(5),
};

/**
 * enum ionic_lif_attr - List of LIF attributes
 */
enum ionic_lif_attr {
	IONIC_LIF_ATTR_STATE        = 0,
	IONIC_LIF_ATTR_NAME         = 1,
	IONIC_LIF_ATTR_MTU          = 2,
	IONIC_LIF_ATTR_MAC          = 3,
	IONIC_LIF_ATTR_FEATURES     = 4,
	IONIC_LIF_ATTR_RSS          = 5,
	IONIC_LIF_ATTR_STATS_CTRL   = 6,
};

/**
 * struct ionic_lif_setattr_cmd - Set LIF attributes on the NIC
 * @opcode:     Opcode
 * @type:       Attribute type (enum ionic_lif_attr)
 * @index:      LIF index
 * @state:      lif state (enum lif_state)
 * @name:       The netdev name string, 0 terminated
 * @mtu:        Mtu
 * @mac:        Station mac
 * @features:   Features (enum ionic_eth_hw_features)
 * @rss:        RSS properties
 *              @types:     The hash types to enable (see rss_hash_types).
 *              @key:       The hash secret key.
 *              @addr:      Address for the indirection table shared memory.
 * @stats_ctl:  stats control commands (enum stats_ctl_cmd)
 */
struct ionic_lif_setattr_cmd {
	u8     opcode;
	u8     attr;
	__le16 index;
	union {
		u8      state;
		char    name[IONIC_IFNAMSIZ];
		__le32  mtu;
		u8      mac[6];
		__le64  features;
		struct {
			__le16 types;
			u8     key[IONIC_RSS_HASH_KEY_SIZE];
			u8     rsvd[6];
			__le64 addr;
		} rss;
		u8	stats_ctl;
		u8      rsvd[60];
	};
};

/**
 * struct ionic_lif_setattr_comp - LIF set attr command completion
 * @status:     The status of the command (enum status_code)
 * @comp_index: The index in the descriptor ring for which this
 *              is the completion.
 * @features:   features (enum ionic_eth_hw_features)
 * @color:      Color bit
 */
struct ionic_lif_setattr_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	union {
		__le64  features;
		u8      rsvd2[11];
	};
	u8     color;
};

/**
 * struct ionic_lif_getattr_cmd - Get LIF attributes from the NIC
 * @opcode:     Opcode
 * @attr:       Attribute type (enum ionic_lif_attr)
 * @index:      LIF index
 */
struct ionic_lif_getattr_cmd {
	u8     opcode;
	u8     attr;
	__le16 index;
	u8     rsvd[60];
};

/**
 * struct ionic_lif_getattr_comp - LIF get attr command completion
 * @status:     The status of the command (enum status_code)
 * @comp_index: The index in the descriptor ring for which this
 *              is the completion.
 * @state:      lif state (enum lif_state)
 * @name:       The netdev name string, 0 terminated
 * @mtu:        Mtu
 * @mac:        Station mac
 * @features:   Features (enum ionic_eth_hw_features)
 * @color:      Color bit
 */
struct ionic_lif_getattr_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	union {
		u8      state;
		__le32  mtu;
		u8      mac[6];
		__le64  features;
		u8      rsvd2[11];
	};
	u8     color;
};

enum ionic_rx_mode {
	IONIC_RX_MODE_F_UNICAST    = BIT(0),
	IONIC_RX_MODE_F_MULTICAST  = BIT(1),
	IONIC_RX_MODE_F_BROADCAST  = BIT(2),
	IONIC_RX_MODE_F_PROMISC    = BIT(3),
	IONIC_RX_MODE_F_ALLMULTI   = BIT(4),
};

/**
 * struct ionic_rx_mode_set_cmd - Set LIF's Rx mode command
 * @opcode:     opcode
 * @lif_index:  LIF index
 * @rx_mode:    Rx mode flags:
 *                  IONIC_RX_MODE_F_UNICAST: Accept known unicast packets.
 *                  IONIC_RX_MODE_F_MULTICAST: Accept known multicast packets.
 *                  IONIC_RX_MODE_F_BROADCAST: Accept broadcast packets.
 *                  IONIC_RX_MODE_F_PROMISC: Accept any packets.
 *                  IONIC_RX_MODE_F_ALLMULTI: Accept any multicast packets.
 */
struct ionic_rx_mode_set_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 lif_index;
	__le16 rx_mode;
	__le16 rsvd2[29];
};

typedef struct ionic_admin_comp ionic_rx_mode_set_comp;

enum ionic_rx_filter_match_type {
	IONIC_RX_FILTER_MATCH_VLAN = 0,
	IONIC_RX_FILTER_MATCH_MAC,
	IONIC_RX_FILTER_MATCH_MAC_VLAN,
};

/**
 * struct ionic_rx_filter_add_cmd - Add LIF Rx filter command
 * @opcode:     opcode
 * @qtype:      Queue type
 * @lif_index:  LIF index
 * @qid:        Queue ID
 * @match:      Rx filter match type.  (See IONIC_RX_FILTER_MATCH_xxx)
 * @vlan:       VLAN ID
 * @addr:       MAC address (network-byte order)
 */
struct ionic_rx_filter_add_cmd {
	u8     opcode;
	u8     qtype;
	__le16 lif_index;
	__le32 qid;
	__le16 match;
	union {
		struct {
			__le16 vlan;
		} vlan;
		struct {
			u8     addr[6];
		} mac;
		struct {
			__le16 vlan;
			u8     addr[6];
		} mac_vlan;
		u8 rsvd[54];
	};
};

/**
 * struct ionic_rx_filter_add_comp - Add LIF Rx filter command completion
 * @status:     The status of the command (enum status_code)
 * @comp_index: The index in the descriptor ring for which this
 *              is the completion.
 * @filter_id:  Filter ID
 * @color:      Color bit.
 */
struct ionic_rx_filter_add_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	__le32 filter_id;
	u8     rsvd2[7];
	u8     color;
};

/**
 * struct ionic_rx_filter_del_cmd - Delete LIF Rx filter command
 * @opcode:     opcode
 * @lif_index:  LIF index
 * @filter_id:  Filter ID
 */
struct ionic_rx_filter_del_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 lif_index;
	__le32 filter_id;
	u8     rsvd2[56];
};

typedef struct ionic_admin_comp ionic_rx_filter_del_comp;

/**
 * struct ionic_qos_identify_cmd - QoS identify command
 * @opcode:    opcode
 * @ver:     Highest version of identify supported by driver
 *
 */
struct ionic_qos_identify_cmd {
	u8 opcode;
	u8 ver;
	u8 rsvd[62];
};

/**
 * struct ionic_qos_identify_comp - QoS identify command completion
 * @status: The status of the command (enum status_code)
 * @ver:    Version of identify returned by device
 */
struct ionic_qos_identify_comp {
	u8 status;
	u8 ver;
	u8 rsvd[14];
};

#define IONIC_QOS_CLASS_MAX		7
#define IONIC_QOS_CLASS_NAME_SZ		32
#define IONIC_QOS_DSCP_MAX_VALUES	64

/**
 * enum ionic_qos_class
 */
enum ionic_qos_class {
	IONIC_QOS_CLASS_DEFAULT		= 0,
	IONIC_QOS_CLASS_USER_DEFINED_1	= 1,
	IONIC_QOS_CLASS_USER_DEFINED_2	= 2,
	IONIC_QOS_CLASS_USER_DEFINED_3	= 3,
	IONIC_QOS_CLASS_USER_DEFINED_4	= 4,
	IONIC_QOS_CLASS_USER_DEFINED_5	= 5,
	IONIC_QOS_CLASS_USER_DEFINED_6	= 6,
};

/**
 * enum ionic_qos_class_type - Traffic classification criteria
 */
enum ionic_qos_class_type {
	IONIC_QOS_CLASS_TYPE_NONE	= 0,
	IONIC_QOS_CLASS_TYPE_PCP	= 1,	/* Dot1Q pcp */
	IONIC_QOS_CLASS_TYPE_DSCP	= 2,	/* IP dscp */
};

/**
 * enum ionic_qos_sched_type - Qos class scheduling type
 */
enum ionic_qos_sched_type {
	IONIC_QOS_SCHED_TYPE_STRICT	= 0,	/* Strict priority */
	IONIC_QOS_SCHED_TYPE_DWRR	= 1,	/* Deficit weighted round-robin */
};

enum ionic_vf_attr {
	IONIC_VF_ATTR_SPOOFCHK	= 1,
	IONIC_VF_ATTR_TRUST	= 2,
	IONIC_VF_ATTR_MAC	= 3,
	IONIC_VF_ATTR_LINKSTATE	= 4,
	IONIC_VF_ATTR_VLAN	= 5,
	IONIC_VF_ATTR_RATE	= 6,
	IONIC_VF_ATTR_STATSADDR	= 7,
};

/**
 * VF link status
 */
enum ionic_vf_link_status {
	IONIC_VF_LINK_STATUS_AUTO = 0,	/* link state of the uplink */
	IONIC_VF_LINK_STATUS_UP   = 1,	/* link is always up */
	IONIC_VF_LINK_STATUS_DOWN = 2,	/* link is always down */
};

/**
 * struct ionic_vf_setattr_cmd - Set VF attributes on the NIC
 * @opcode:     Opcode
 * @index:      VF index
 * @attr:       Attribute type (enum ionic_vf_attr)
 *	macaddr		mac address
 *	vlanid		vlan ID
 *	maxrate		max Tx rate in Mbps
 *	spoofchk	enable address spoof checking
 *	trust		enable VF trust
 *	linkstate	set link up or down
 *	stats_pa	set DMA address for VF stats
 */
struct ionic_vf_setattr_cmd {
	u8     opcode;
	u8     attr;
	__le16 vf_index;
	union {
		u8     macaddr[6];
		__le16 vlanid;
		__le32 maxrate;
		u8     spoofchk;
		u8     trust;
		u8     linkstate;
		__le64 stats_pa;
		u8     pad[60];
	};
};

struct ionic_vf_setattr_comp {
	u8     status;
	u8     attr;
	__le16 vf_index;
	__le16 comp_index;
	u8     rsvd[9];
	u8     color;
};

/**
 * struct ionic_vf_getattr_cmd - Get VF attributes from the NIC
 * @opcode:     Opcode
 * @index:      VF index
 * @attr:       Attribute type (enum ionic_vf_attr)
 */
struct ionic_vf_getattr_cmd {
	u8     opcode;
	u8     attr;
	__le16 vf_index;
	u8     rsvd[60];
};

struct ionic_vf_getattr_comp {
	u8     status;
	u8     attr;
	__le16 vf_index;
	union {
		u8     macaddr[6];
		__le16 vlanid;
		__le32 maxrate;
		u8     spoofchk;
		u8     trust;
		u8     linkstate;
		__le64 stats_pa;
		u8     pad[11];
	};
	u8     color;
};

/**
 * union ionic_qos_config - Qos configuration structure
 * @flags:		Configuration flags
 *	IONIC_QOS_CONFIG_F_ENABLE		enable
 *	IONIC_QOS_CONFIG_F_DROP			drop/nodrop
 *	IONIC_QOS_CONFIG_F_RW_DOT1Q_PCP		enable dot1q pcp rewrite
 *	IONIC_QOS_CONFIG_F_RW_IP_DSCP		enable ip dscp rewrite
 * @sched_type:		Qos class scheduling type (enum ionic_qos_sched_type)
 * @class_type:		Qos class type (enum ionic_qos_class_type)
 * @pause_type:		Qos pause type (enum ionic_qos_pause_type)
 * @name:		Qos class name
 * @mtu:		MTU of the class
 * @pfc_dot1q_pcp:	Pcp value for pause frames (valid iff F_NODROP)
 * @dwrr_weight:	Qos class scheduling weight
 * @strict_rlmt:	Rate limit for strict priority scheduling
 * @rw_dot1q_pcp:	Rewrite dot1q pcp to this value	(valid iff F_RW_DOT1Q_PCP)
 * @rw_ip_dscp:		Rewrite ip dscp to this value	(valid iff F_RW_IP_DSCP)
 * @dot1q_pcp:		Dot1q pcp value
 * @ndscp:		Number of valid dscp values in the ip_dscp field
 * @ip_dscp:		IP dscp values
 */
union ionic_qos_config {
	struct {
#define IONIC_QOS_CONFIG_F_ENABLE		BIT(0)
#define IONIC_QOS_CONFIG_F_DROP			BIT(1)
#define IONIC_QOS_CONFIG_F_RW_DOT1Q_PCP		BIT(2)
#define IONIC_QOS_CONFIG_F_RW_IP_DSCP		BIT(3)
		u8      flags;
		u8      sched_type;
		u8      class_type;
		u8      pause_type;
		char    name[IONIC_QOS_CLASS_NAME_SZ];
		__le32  mtu;
		/* flow control */
		u8      pfc_cos;
		/* scheduler */
		union {
			u8      dwrr_weight;
			__le64  strict_rlmt;
		};
		/* marking */
		union {
			u8      rw_dot1q_pcp;
			u8      rw_ip_dscp;
		};
		/* classification */
		union {
			u8      dot1q_pcp;
			struct {
				u8      ndscp;
				u8      ip_dscp[IONIC_QOS_DSCP_MAX_VALUES];
			};
		};
	};
	__le32  words[64];
};

/**
 * union ionic_qos_identity - QoS identity structure
 * @version:	Version of the identify structure
 * @type:	QoS system type
 * @nclasses:	Number of usable QoS classes
 * @config:	Current configuration of classes
 */
union ionic_qos_identity {
	struct {
		u8     version;
		u8     type;
		u8     rsvd[62];
		union  ionic_qos_config config[IONIC_QOS_CLASS_MAX];
	};
	__le32 words[512];
};

/**
 * struct qos_init_cmd - QoS config init command
 * @opcode:	Opcode
 * @group:	Qos class id
 * @info_pa:	destination address for qos info
 */
struct ionic_qos_init_cmd {
	u8     opcode;
	u8     group;
	u8     rsvd[6];
	__le64 info_pa;
	u8     rsvd1[48];
};

typedef struct ionic_admin_comp ionic_qos_init_comp;

/**
 * struct ionic_qos_reset_cmd - Qos config reset command
 * @opcode:	Opcode
 */
struct ionic_qos_reset_cmd {
	u8    opcode;
	u8    group;
	u8    rsvd[62];
};

typedef struct ionic_admin_comp ionic_qos_reset_comp;

/**
 * struct ionic_fw_download_cmd - Firmware download command
 * @opcode:	opcode
 * @addr:	dma address of the firmware buffer
 * @offset:	offset of the firmware buffer within the full image
 * @length:	number of valid bytes in the firmware buffer
 */
struct ionic_fw_download_cmd {
	u8     opcode;
	u8     rsvd[3];
	__le32 offset;
	__le64 addr;
	__le32 length;
};

typedef struct ionic_admin_comp ionic_fw_download_comp;

enum ionic_fw_control_oper {
	IONIC_FW_RESET		= 0,	/* Reset firmware */
	IONIC_FW_INSTALL	= 1,	/* Install firmware */
	IONIC_FW_ACTIVATE	= 2,	/* Activate firmware */
};

/**
 * struct ionic_fw_control_cmd - Firmware control command
 * @opcode:    opcode
 * @oper:      firmware control operation (enum ionic_fw_control_oper)
 * @slot:      slot to activate
 */
struct ionic_fw_control_cmd {
	u8  opcode;
	u8  rsvd[3];
	u8  oper;
	u8  slot;
	u8  rsvd1[58];
};

/**
 * struct ionic_fw_control_comp - Firmware control copletion
 * @opcode:    opcode
 * @slot:      slot where the firmware was installed
 */
struct ionic_fw_control_comp {
	u8     status;
	u8     rsvd;
	__le16 comp_index;
	u8     slot;
	u8     rsvd1[10];
	u8     color;
};

/******************************************************************
 ******************* RDMA Commands ********************************
 ******************************************************************/

/**
 * struct ionic_rdma_reset_cmd - Reset RDMA LIF cmd
 * @opcode:        opcode
 * @lif_index:     lif index
 *
 * There is no rdma specific dev command completion struct.  Completion uses
 * the common struct ionic_admin_comp.  Only the status is indicated.
 * Nonzero status means the LIF does not support rdma.
 **/
struct ionic_rdma_reset_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 lif_index;
	u8     rsvd2[60];
};

/**
 * struct ionic_rdma_queue_cmd - Create RDMA Queue command
 * @opcode:        opcode, 52, 53
 * @lif_index      lif index
 * @qid_ver:       (qid | (rdma version << 24))
 * @cid:           intr, eq_id, or cq_id
 * @dbid:          doorbell page id
 * @depth_log2:    log base two of queue depth
 * @stride_log2:   log base two of queue stride
 * @dma_addr:      address of the queue memory
 * @xxx_table_index: temporary, but should not need pgtbl for contig. queues.
 *
 * The same command struct is used to create an rdma event queue, completion
 * queue, or rdma admin queue.  The cid is an interrupt number for an event
 * queue, an event queue id for a completion queue, or a completion queue id
 * for an rdma admin queue.
 *
 * The queue created via a dev command must be contiguous in dma space.
 *
 * The dev commands are intended only to be used during driver initialization,
 * to create queues supporting the rdma admin queue.  Other queues, and other
 * types of rdma resources like memory regions, will be created and registered
 * via the rdma admin queue, and will support a more complete interface
 * providing scatter gather lists for larger, scattered queue buffers and
 * memory registration.
 *
 * There is no rdma specific dev command completion struct.  Completion uses
 * the common struct ionic_admin_comp.  Only the status is indicated.
 **/
struct ionic_rdma_queue_cmd {
	u8     opcode;
	u8     rsvd;
	__le16 lif_index;
	__le32 qid_ver;
	__le32 cid;
	__le16 dbid;
	u8     depth_log2;
	u8     stride_log2;
	__le64 dma_addr;
	u8     rsvd2[36];
	__le32 xxx_table_index;
};

/******************************************************************
 ******************* Notify Events ********************************
 ******************************************************************/

/**
 * struct ionic_notifyq_event
 * @eid:   event number
 * @ecode: event code
 * @data:  unspecified data about the event
 *
 * This is the generic event report struct from which the other
 * actual events will be formed.
 */
struct ionic_notifyq_event {
	__le64 eid;
	__le16 ecode;
	u8     data[54];
};

/**
 * struct ionic_link_change_event
 * @eid:		event number
 * @ecode:		event code = EVENT_OPCODE_LINK_CHANGE
 * @link_status:	link up or down, with error bits (enum port_status)
 * @link_speed:		speed of the network link
 *
 * Sent when the network link state changes between UP and DOWN
 */
struct ionic_link_change_event {
	__le64 eid;
	__le16 ecode;
	__le16 link_status;
	__le32 link_speed;	/* units of 1Mbps: e.g. 10000 = 10Gbps */
	u8     rsvd[48];
};

/**
 * struct ionic_reset_event
 * @eid:		event number
 * @ecode:		event code = EVENT_OPCODE_RESET
 * @reset_code:		reset type
 * @state:		0=pending, 1=complete, 2=error
 *
 * Sent when the NIC or some subsystem is going to be or
 * has been reset.
 */
struct ionic_reset_event {
	__le64 eid;
	__le16 ecode;
	u8     reset_code;
	u8     state;
	u8     rsvd[52];
};

/**
 * struct ionic_heartbeat_event
 * @eid:	event number
 * @ecode:	event code = EVENT_OPCODE_HEARTBEAT
 *
 * Sent periodically by the NIC to indicate continued health
 */
struct ionic_heartbeat_event {
	__le64 eid;
	__le16 ecode;
	u8     rsvd[54];
};

/**
 * struct ionic_log_event
 * @eid:	event number
 * @ecode:	event code = EVENT_OPCODE_LOG
 * @data:	log data
 *
 * Sent to notify the driver of an internal error.
 */
struct ionic_log_event {
	__le64 eid;
	__le16 ecode;
	u8     data[54];
};

/**
 * struct ionic_port_stats
 */
struct ionic_port_stats {
	__le64 frames_rx_ok;
	__le64 frames_rx_all;
	__le64 frames_rx_bad_fcs;
	__le64 frames_rx_bad_all;
	__le64 octets_rx_ok;
	__le64 octets_rx_all;
	__le64 frames_rx_unicast;
	__le64 frames_rx_multicast;
	__le64 frames_rx_broadcast;
	__le64 frames_rx_pause;
	__le64 frames_rx_bad_length;
	__le64 frames_rx_undersized;
	__le64 frames_rx_oversized;
	__le64 frames_rx_fragments;
	__le64 frames_rx_jabber;
	__le64 frames_rx_pripause;
	__le64 frames_rx_stomped_crc;
	__le64 frames_rx_too_long;
	__le64 frames_rx_vlan_good;
	__le64 frames_rx_dropped;
	__le64 frames_rx_less_than_64b;
	__le64 frames_rx_64b;
	__le64 frames_rx_65b_127b;
	__le64 frames_rx_128b_255b;
	__le64 frames_rx_256b_511b;
	__le64 frames_rx_512b_1023b;
	__le64 frames_rx_1024b_1518b;
	__le64 frames_rx_1519b_2047b;
	__le64 frames_rx_2048b_4095b;
	__le64 frames_rx_4096b_8191b;
	__le64 frames_rx_8192b_9215b;
	__le64 frames_rx_other;
	__le64 frames_tx_ok;
	__le64 frames_tx_all;
	__le64 frames_tx_bad;
	__le64 octets_tx_ok;
	__le64 octets_tx_total;
	__le64 frames_tx_unicast;
	__le64 frames_tx_multicast;
	__le64 frames_tx_broadcast;
	__le64 frames_tx_pause;
	__le64 frames_tx_pripause;
	__le64 frames_tx_vlan;
	__le64 frames_tx_less_than_64b;
	__le64 frames_tx_64b;
	__le64 frames_tx_65b_127b;
	__le64 frames_tx_128b_255b;
	__le64 frames_tx_256b_511b;
	__le64 frames_tx_512b_1023b;
	__le64 frames_tx_1024b_1518b;
	__le64 frames_tx_1519b_2047b;
	__le64 frames_tx_2048b_4095b;
	__le64 frames_tx_4096b_8191b;
	__le64 frames_tx_8192b_9215b;
	__le64 frames_tx_other;
	__le64 frames_tx_pri_0;
	__le64 frames_tx_pri_1;
	__le64 frames_tx_pri_2;
	__le64 frames_tx_pri_3;
	__le64 frames_tx_pri_4;
	__le64 frames_tx_pri_5;
	__le64 frames_tx_pri_6;
	__le64 frames_tx_pri_7;
	__le64 frames_rx_pri_0;
	__le64 frames_rx_pri_1;
	__le64 frames_rx_pri_2;
	__le64 frames_rx_pri_3;
	__le64 frames_rx_pri_4;
	__le64 frames_rx_pri_5;
	__le64 frames_rx_pri_6;
	__le64 frames_rx_pri_7;
	__le64 tx_pripause_0_1us_count;
	__le64 tx_pripause_1_1us_count;
	__le64 tx_pripause_2_1us_count;
	__le64 tx_pripause_3_1us_count;
	__le64 tx_pripause_4_1us_count;
	__le64 tx_pripause_5_1us_count;
	__le64 tx_pripause_6_1us_count;
	__le64 tx_pripause_7_1us_count;
	__le64 rx_pripause_0_1us_count;
	__le64 rx_pripause_1_1us_count;
	__le64 rx_pripause_2_1us_count;
	__le64 rx_pripause_3_1us_count;
	__le64 rx_pripause_4_1us_count;
	__le64 rx_pripause_5_1us_count;
	__le64 rx_pripause_6_1us_count;
	__le64 rx_pripause_7_1us_count;
	__le64 rx_pause_1us_count;
	__le64 frames_tx_truncated;
};

struct ionic_mgmt_port_stats {
	__le64 frames_rx_ok;
	__le64 frames_rx_all;
	__le64 frames_rx_bad_fcs;
	__le64 frames_rx_bad_all;
	__le64 octets_rx_ok;
	__le64 octets_rx_all;
	__le64 frames_rx_unicast;
	__le64 frames_rx_multicast;
	__le64 frames_rx_broadcast;
	__le64 frames_rx_pause;
	__le64 frames_rx_bad_length0;
	__le64 frames_rx_undersized1;
	__le64 frames_rx_oversized2;
	__le64 frames_rx_fragments3;
	__le64 frames_rx_jabber4;
	__le64 frames_rx_64b5;
	__le64 frames_rx_65b_127b6;
	__le64 frames_rx_128b_255b7;
	__le64 frames_rx_256b_511b8;
	__le64 frames_rx_512b_1023b9;
	__le64 frames_rx_1024b_1518b0;
	__le64 frames_rx_gt_1518b1;
	__le64 frames_rx_fifo_full2;
	__le64 frames_tx_ok3;
	__le64 frames_tx_all4;
	__le64 frames_tx_bad5;
	__le64 octets_tx_ok6;
	__le64 octets_tx_total7;
	__le64 frames_tx_unicast8;
	__le64 frames_tx_multicast9;
	__le64 frames_tx_broadcast0;
	__le64 frames_tx_pause1;
};

/**
 * struct ionic_port_identity - port identity structure
 * @version:        identity structure version
 * @type:           type of port (enum port_type)
 * @num_lanes:      number of lanes for the port
 * @autoneg:        autoneg supported
 * @min_frame_size: minimum frame size supported
 * @max_frame_size: maximum frame size supported
 * @fec_type:       supported fec types
 * @pause_type:     supported pause types
 * @loopback_mode:  supported loopback mode
 * @speeds:         supported speeds
 * @config:         current port configuration
 */
union ionic_port_identity {
	struct {
		u8     version;
		u8     type;
		u8     num_lanes;
		u8     autoneg;
		__le32 min_frame_size;
		__le32 max_frame_size;
		u8     fec_type[4];
		u8     pause_type[2];
		u8     loopback_mode[2];
		__le32 speeds[16];
		u8     rsvd2[44];
		union ionic_port_config config;
	};
	__le32 words[512];
};

/**
 * struct ionic_port_info - port info structure
 * @port_status:     port status
 * @port_stats:      port stats
 */
struct ionic_port_info {
	union ionic_port_config config;
	struct ionic_port_status status;
	struct ionic_port_stats stats;
};

/**
 * struct ionic_lif_stats
 */
struct ionic_lif_stats {
	/* RX */
	__le64 rx_ucast_bytes;
	__le64 rx_ucast_packets;
	__le64 rx_mcast_bytes;
	__le64 rx_mcast_packets;
	__le64 rx_bcast_bytes;
	__le64 rx_bcast_packets;
	__le64 rsvd0;
	__le64 rsvd1;
	/* RX drops */
	__le64 rx_ucast_drop_bytes;
	__le64 rx_ucast_drop_packets;
	__le64 rx_mcast_drop_bytes;
	__le64 rx_mcast_drop_packets;
	__le64 rx_bcast_drop_bytes;
	__le64 rx_bcast_drop_packets;
	__le64 rx_dma_error;
	__le64 rsvd2;
	/* TX */
	__le64 tx_ucast_bytes;
	__le64 tx_ucast_packets;
	__le64 tx_mcast_bytes;
	__le64 tx_mcast_packets;
	__le64 tx_bcast_bytes;
	__le64 tx_bcast_packets;
	__le64 rsvd3;
	__le64 rsvd4;
	/* TX drops */
	__le64 tx_ucast_drop_bytes;
	__le64 tx_ucast_drop_packets;
	__le64 tx_mcast_drop_bytes;
	__le64 tx_mcast_drop_packets;
	__le64 tx_bcast_drop_bytes;
	__le64 tx_bcast_drop_packets;
	__le64 tx_dma_error;
	__le64 rsvd5;
	/* Rx Queue/Ring drops */
	__le64 rx_queue_disabled;
	__le64 rx_queue_empty;
	__le64 rx_queue_error;
	__le64 rx_desc_fetch_error;
	__le64 rx_desc_data_error;
	__le64 rsvd6;
	__le64 rsvd7;
	__le64 rsvd8;
	/* Tx Queue/Ring drops */
	__le64 tx_queue_disabled;
	__le64 tx_queue_error;
	__le64 tx_desc_fetch_error;
	__le64 tx_desc_data_error;
	__le64 rsvd9;
	__le64 rsvd10;
	__le64 rsvd11;
	__le64 rsvd12;

	/* RDMA/ROCE TX */
	__le64 tx_rdma_ucast_bytes;
	__le64 tx_rdma_ucast_packets;
	__le64 tx_rdma_mcast_bytes;
	__le64 tx_rdma_mcast_packets;
	__le64 tx_rdma_cnp_packets;
	__le64 rsvd13;
	__le64 rsvd14;
	__le64 rsvd15;

	/* RDMA/ROCE RX */
	__le64 rx_rdma_ucast_bytes;
	__le64 rx_rdma_ucast_packets;
	__le64 rx_rdma_mcast_bytes;
	__le64 rx_rdma_mcast_packets;
	__le64 rx_rdma_cnp_packets;
	__le64 rx_rdma_ecn_packets;
	__le64 rsvd16;
	__le64 rsvd17;

	__le64 rsvd18;
	__le64 rsvd19;
	__le64 rsvd20;
	__le64 rsvd21;
	__le64 rsvd22;
	__le64 rsvd23;
	__le64 rsvd24;
	__le64 rsvd25;

	__le64 rsvd26;
	__le64 rsvd27;
	__le64 rsvd28;
	__le64 rsvd29;
	__le64 rsvd30;
	__le64 rsvd31;
	__le64 rsvd32;
	__le64 rsvd33;

	__le64 rsvd34;
	__le64 rsvd35;
	__le64 rsvd36;
	__le64 rsvd37;
	__le64 rsvd38;
	__le64 rsvd39;
	__le64 rsvd40;
	__le64 rsvd41;

	__le64 rsvd42;
	__le64 rsvd43;
	__le64 rsvd44;
	__le64 rsvd45;
	__le64 rsvd46;
	__le64 rsvd47;
	__le64 rsvd48;
	__le64 rsvd49;

	/* RDMA/ROCE REQ Error/Debugs (768 - 895) */
	__le64 rdma_req_rx_pkt_seq_err;
	__le64 rdma_req_rx_rnr_retry_err;
	__le64 rdma_req_rx_remote_access_err;
	__le64 rdma_req_rx_remote_inv_req_err;
	__le64 rdma_req_rx_remote_oper_err;
	__le64 rdma_req_rx_implied_nak_seq_err;
	__le64 rdma_req_rx_cqe_err;
	__le64 rdma_req_rx_cqe_flush_err;

	__le64 rdma_req_rx_dup_responses;
	__le64 rdma_req_rx_invalid_packets;
	__le64 rdma_req_tx_local_access_err;
	__le64 rdma_req_tx_local_oper_err;
	__le64 rdma_req_tx_memory_mgmt_err;
	__le64 rsvd52;
	__le64 rsvd53;
	__le64 rsvd54;

	/* RDMA/ROCE RESP Error/Debugs (896 - 1023) */
	__le64 rdma_resp_rx_dup_requests;
	__le64 rdma_resp_rx_out_of_buffer;
	__le64 rdma_resp_rx_out_of_seq_pkts;
	__le64 rdma_resp_rx_cqe_err;
	__le64 rdma_resp_rx_cqe_flush_err;
	__le64 rdma_resp_rx_local_len_err;
	__le64 rdma_resp_rx_inv_request_err;
	__le64 rdma_resp_rx_local_qp_oper_err;

	__le64 rdma_resp_rx_out_of_atomic_resource;
	__le64 rdma_resp_tx_pkt_seq_err;
	__le64 rdma_resp_tx_remote_inv_req_err;
	__le64 rdma_resp_tx_remote_access_err;
	__le64 rdma_resp_tx_remote_oper_err;
	__le64 rdma_resp_tx_rnr_retry_err;
	__le64 rsvd57;
	__le64 rsvd58;
};

/**
 * struct ionic_lif_info - lif info structure
 */
struct ionic_lif_info {
	union ionic_lif_config config;
	struct ionic_lif_status status;
	struct ionic_lif_stats stats;
};

union ionic_dev_cmd {
	u32 words[16];
	struct ionic_admin_cmd cmd;
	struct ionic_nop_cmd nop;

	struct ionic_dev_identify_cmd identify;
	struct ionic_dev_init_cmd init;
	struct ionic_dev_reset_cmd reset;
	struct ionic_dev_getattr_cmd getattr;
	struct ionic_dev_setattr_cmd setattr;

	struct ionic_port_identify_cmd port_identify;
	struct ionic_port_init_cmd port_init;
	struct ionic_port_reset_cmd port_reset;
	struct ionic_port_getattr_cmd port_getattr;
	struct ionic_port_setattr_cmd port_setattr;

	struct ionic_vf_setattr_cmd vf_setattr;
	struct ionic_vf_getattr_cmd vf_getattr;

	struct ionic_lif_identify_cmd lif_identify;
	struct ionic_lif_init_cmd lif_init;
	struct ionic_lif_reset_cmd lif_reset;

	struct ionic_qos_identify_cmd qos_identify;
	struct ionic_qos_init_cmd qos_init;
	struct ionic_qos_reset_cmd qos_reset;

	struct ionic_q_init_cmd q_init;
};

union ionic_dev_cmd_comp {
	u32 words[4];
	u8 status;
	struct ionic_admin_comp comp;
	struct ionic_nop_comp nop;

	struct ionic_dev_identify_comp identify;
	struct ionic_dev_init_comp init;
	struct ionic_dev_reset_comp reset;
	struct ionic_dev_getattr_comp getattr;
	struct ionic_dev_setattr_comp setattr;

	struct ionic_port_identify_comp port_identify;
	struct ionic_port_init_comp port_init;
	struct ionic_port_reset_comp port_reset;
	struct ionic_port_getattr_comp port_getattr;
	struct ionic_port_setattr_comp port_setattr;

	struct ionic_vf_setattr_comp vf_setattr;
	struct ionic_vf_getattr_comp vf_getattr;

	struct ionic_lif_identify_comp lif_identify;
	struct ionic_lif_init_comp lif_init;
	ionic_lif_reset_comp lif_reset;

	struct ionic_qos_identify_comp qos_identify;
	ionic_qos_init_comp qos_init;
	ionic_qos_reset_comp qos_reset;

	struct ionic_q_init_comp q_init;
};

/**
 * union dev_info - Device info register format (read-only)
 * @signature:       Signature value of 0x44455649 ('DEVI').
 * @version:         Current version of info.
 * @asic_type:       Asic type.
 * @asic_rev:        Asic revision.
 * @fw_status:       Firmware status.
 * @fw_heartbeat:    Firmware heartbeat counter.
 * @serial_num:      Serial number.
 * @fw_version:      Firmware version.
 */
union ionic_dev_info_regs {
#define IONIC_DEVINFO_FWVERS_BUFLEN 32
#define IONIC_DEVINFO_SERIAL_BUFLEN 32
	struct {
		u32    signature;
		u8     version;
		u8     asic_type;
		u8     asic_rev;
		u8     fw_status;
		u32    fw_heartbeat;
		char   fw_version[IONIC_DEVINFO_FWVERS_BUFLEN];
		char   serial_num[IONIC_DEVINFO_SERIAL_BUFLEN];
	};
	u32 words[512];
};

/**
 * union ionic_dev_cmd_regs - Device command register format (read-write)
 * @doorbell:        Device Cmd Doorbell, write-only.
 *                   Write a 1 to signal device to process cmd,
 *                   poll done for completion.
 * @done:            Done indicator, bit 0 == 1 when command is complete.
 * @cmd:             Opcode-specific command bytes
 * @comp:            Opcode-specific response bytes
 * @data:            Opcode-specific side-data
 */
union ionic_dev_cmd_regs {
	struct {
		u32                   doorbell;
		u32                   done;
		union ionic_dev_cmd         cmd;
		union ionic_dev_cmd_comp    comp;
		u8                    rsvd[48];
		u32                   data[478];
	};
	u32 words[512];
};

/**
 * union ionic_dev_regs - Device register format in for bar 0 page 0
 * @info:            Device info registers
 * @devcmd:          Device command registers
 */
union ionic_dev_regs {
	struct {
		union ionic_dev_info_regs info;
		union ionic_dev_cmd_regs  devcmd;
	};
	__le32 words[1024];
};

union ionic_adminq_cmd {
	struct ionic_admin_cmd cmd;
	struct ionic_nop_cmd nop;
	struct ionic_q_init_cmd q_init;
	struct ionic_q_control_cmd q_control;
	struct ionic_lif_setattr_cmd lif_setattr;
	struct ionic_lif_getattr_cmd lif_getattr;
	struct ionic_rx_mode_set_cmd rx_mode_set;
	struct ionic_rx_filter_add_cmd rx_filter_add;
	struct ionic_rx_filter_del_cmd rx_filter_del;
	struct ionic_rdma_reset_cmd rdma_reset;
	struct ionic_rdma_queue_cmd rdma_queue;
	struct ionic_fw_download_cmd fw_download;
	struct ionic_fw_control_cmd fw_control;
};

union ionic_adminq_comp {
	struct ionic_admin_comp comp;
	struct ionic_nop_comp nop;
	struct ionic_q_init_comp q_init;
	struct ionic_lif_setattr_comp lif_setattr;
	struct ionic_lif_getattr_comp lif_getattr;
	struct ionic_rx_filter_add_comp rx_filter_add;
	struct ionic_fw_control_comp fw_control;
};

#define IONIC_BARS_MAX			6
#define IONIC_PCI_BAR_DBELL		1

/* BAR0 */
#define IONIC_BAR0_SIZE				0x8000

#define IONIC_BAR0_DEV_INFO_REGS_OFFSET		0x0000
#define IONIC_BAR0_DEV_CMD_REGS_OFFSET		0x0800
#define IONIC_BAR0_DEV_CMD_DATA_REGS_OFFSET	0x0c00
#define IONIC_BAR0_INTR_STATUS_OFFSET		0x1000
#define IONIC_BAR0_INTR_CTRL_OFFSET		0x2000
#define IONIC_DEV_CMD_DONE			0x00000001

#define IONIC_ASIC_TYPE_CAPRI			0

/**
 * struct ionic_doorbell - Doorbell register layout
 * @p_index: Producer index
 * @ring:    Selects the specific ring of the queue to update.
 *           Type-specific meaning:
 *              ring=0: Default producer/consumer queue.
 *              ring=1: (CQ, EQ) Re-Arm queue.  RDMA CQs
 *              send events to EQs when armed.  EQs send
 *              interrupts when armed.
 * @qid:     The queue id selects the queue destination for the
 *           producer index and flags.
 */
struct ionic_doorbell {
	__le16 p_index;
	u8     ring;
	u8     qid_lo;
	__le16 qid_hi;
	u16    rsvd2;
};

struct ionic_intr_status {
	u32 status[2];
};

struct ionic_notifyq_cmd {
	__le32 data;	/* Not used but needed for qcq structure */
};

union ionic_notifyq_comp {
	struct ionic_notifyq_event event;
	struct ionic_link_change_event link_change;
	struct ionic_reset_event reset;
	struct ionic_heartbeat_event heartbeat;
	struct ionic_log_event log;
};

/* Deprecate */
struct ionic_identity {
	union ionic_drv_identity drv;
	union ionic_dev_identity dev;
	union ionic_lif_identity lif;
	union ionic_port_identity port;
	union ionic_qos_identity qos;
};

#pragma pack(pop)

#endif /* _IONIC_IF_H_ */
