/*
 * Copyright 2015 - 2016 Amazon.com, Inc. or its affiliates.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef _ENA_ADMIN_H_
#define _ENA_ADMIN_H_


enum ena_admin_aq_opcode {
	ENA_ADMIN_CREATE_SQ                         = 1,
	ENA_ADMIN_DESTROY_SQ                        = 2,
	ENA_ADMIN_CREATE_CQ                         = 3,
	ENA_ADMIN_DESTROY_CQ                        = 4,
	ENA_ADMIN_GET_FEATURE                       = 8,
	ENA_ADMIN_SET_FEATURE                       = 9,
	ENA_ADMIN_GET_STATS                         = 11,
};

enum ena_admin_aq_completion_status {
	ENA_ADMIN_SUCCESS                           = 0,
	ENA_ADMIN_RESOURCE_ALLOCATION_FAILURE       = 1,
	ENA_ADMIN_BAD_OPCODE                        = 2,
	ENA_ADMIN_UNSUPPORTED_OPCODE                = 3,
	ENA_ADMIN_MALFORMED_REQUEST                 = 4,
	/* Additional status is provided in ACQ entry extended_status */
	ENA_ADMIN_ILLEGAL_PARAMETER                 = 5,
	ENA_ADMIN_UNKNOWN_ERROR                     = 6,
	ENA_ADMIN_RESOURCE_BUSY                     = 7,
};

enum ena_admin_aq_feature_id {
	ENA_ADMIN_DEVICE_ATTRIBUTES                 = 1,
	ENA_ADMIN_MAX_QUEUES_NUM                    = 2,
	ENA_ADMIN_HW_HINTS                          = 3,
	ENA_ADMIN_LLQ                               = 4,
	ENA_ADMIN_MAX_QUEUES_EXT                    = 7,
	ENA_ADMIN_RSS_HASH_FUNCTION                 = 10,
	ENA_ADMIN_STATELESS_OFFLOAD_CONFIG          = 11,
	ENA_ADMIN_RSS_REDIRECTION_TABLE_CONFIG      = 12,
	ENA_ADMIN_MTU                               = 14,
	ENA_ADMIN_RSS_HASH_INPUT                    = 18,
	ENA_ADMIN_INTERRUPT_MODERATION              = 20,
	ENA_ADMIN_AENQ_CONFIG                       = 26,
	ENA_ADMIN_LINK_CONFIG                       = 27,
	ENA_ADMIN_HOST_ATTR_CONFIG                  = 28,
	ENA_ADMIN_FEATURES_OPCODE_NUM               = 32,
};

enum ena_admin_placement_policy_type {
	/* descriptors and headers are in host memory */
	ENA_ADMIN_PLACEMENT_POLICY_HOST             = 1,
	/* descriptors and headers are in device memory (a.k.a Low Latency
	 * Queue)
	 */
	ENA_ADMIN_PLACEMENT_POLICY_DEV              = 3,
};

enum ena_admin_link_types {
	ENA_ADMIN_LINK_SPEED_1G                     = 0x1,
	ENA_ADMIN_LINK_SPEED_2_HALF_G               = 0x2,
	ENA_ADMIN_LINK_SPEED_5G                     = 0x4,
	ENA_ADMIN_LINK_SPEED_10G                    = 0x8,
	ENA_ADMIN_LINK_SPEED_25G                    = 0x10,
	ENA_ADMIN_LINK_SPEED_40G                    = 0x20,
	ENA_ADMIN_LINK_SPEED_50G                    = 0x40,
	ENA_ADMIN_LINK_SPEED_100G                   = 0x80,
	ENA_ADMIN_LINK_SPEED_200G                   = 0x100,
	ENA_ADMIN_LINK_SPEED_400G                   = 0x200,
};

enum ena_admin_completion_policy_type {
	/* completion queue entry for each sq descriptor */
	ENA_ADMIN_COMPLETION_POLICY_DESC            = 0,
	/* completion queue entry upon request in sq descriptor */
	ENA_ADMIN_COMPLETION_POLICY_DESC_ON_DEMAND  = 1,
	/* current queue head pointer is updated in OS memory upon sq
	 * descriptor request
	 */
	ENA_ADMIN_COMPLETION_POLICY_HEAD_ON_DEMAND  = 2,
	/* current queue head pointer is updated in OS memory for each sq
	 * descriptor
	 */
	ENA_ADMIN_COMPLETION_POLICY_HEAD            = 3,
};

/* basic stats return ena_admin_basic_stats while extanded stats return a
 * buffer (string format) with additional statistics per queue and per
 * device id
 */
enum ena_admin_get_stats_type {
	ENA_ADMIN_GET_STATS_TYPE_BASIC              = 0,
	ENA_ADMIN_GET_STATS_TYPE_EXTENDED           = 1,
};

enum ena_admin_get_stats_scope {
	ENA_ADMIN_SPECIFIC_QUEUE                    = 0,
	ENA_ADMIN_ETH_TRAFFIC                       = 1,
};

struct ena_admin_aq_common_desc {
	/* 11:0 : command_id
	 * 15:12 : reserved12
	 */
	u16 command_id;

	/* as appears in ena_admin_aq_opcode */
	u8 opcode;

	/* 0 : phase
	 * 1 : ctrl_data - control buffer address valid
	 * 2 : ctrl_data_indirect - control buffer address
	 *    points to list of pages with addresses of control
	 *    buffers
	 * 7:3 : reserved3
	 */
	u8 flags;
};

/* used in ena_admin_aq_entry. Can point directly to control data, or to a
 * page list chunk. Used also at the end of indirect mode page list chunks,
 * for chaining.
 */
struct ena_admin_ctrl_buff_info {
	u32 length;

	struct ena_common_mem_addr address;
};

struct ena_admin_sq {
	u16 sq_idx;

	/* 4:0 : reserved
	 * 7:5 : sq_direction - 0x1 - Tx; 0x2 - Rx
	 */
	u8 sq_identity;

	u8 reserved1;
};

struct ena_admin_aq_entry {
	struct ena_admin_aq_common_desc aq_common_descriptor;

	union {
		u32 inline_data_w1[3];

		struct ena_admin_ctrl_buff_info control_buffer;
	} u;

	u32 inline_data_w4[12];
};

struct ena_admin_acq_common_desc {
	/* command identifier to associate it with the aq descriptor
	 * 11:0 : command_id
	 * 15:12 : reserved12
	 */
	u16 command;

	u8 status;

	/* 0 : phase
	 * 7:1 : reserved1
	 */
	u8 flags;

	u16 extended_status;

	/* indicates to the driver which AQ entry has been consumed by the
	 *    device and could be reused
	 */
	u16 sq_head_indx;
};

struct ena_admin_acq_entry {
	struct ena_admin_acq_common_desc acq_common_descriptor;

	u32 response_specific_data[14];
};

struct ena_admin_aq_create_sq_cmd {
	struct ena_admin_aq_common_desc aq_common_descriptor;

	/* 4:0 : reserved0_w1
	 * 7:5 : sq_direction - 0x1 - Tx, 0x2 - Rx
	 */
	u8 sq_identity;

	u8 reserved8_w1;

	/* 3:0 : placement_policy - Describing where the SQ
	 *    descriptor ring and the SQ packet headers reside:
	 *    0x1 - descriptors and headers are in OS memory,
	 *    0x3 - descriptors and headers in device memory
	 *    (a.k.a Low Latency Queue)
	 * 6:4 : completion_policy - Describing what policy
	 *    to use for generation completion entry (cqe) in
	 *    the CQ associated with this SQ: 0x0 - cqe for each
	 *    sq descriptor, 0x1 - cqe upon request in sq
	 *    descriptor, 0x2 - current queue head pointer is
	 *    updated in OS memory upon sq descriptor request
	 *    0x3 - current queue head pointer is updated in OS
	 *    memory for each sq descriptor
	 * 7 : reserved15_w1
	 */
	u8 sq_caps_2;

	/* 0 : is_physically_contiguous - Described if the
	 *    queue ring memory is allocated in physical
	 *    contiguous pages or split.
	 * 7:1 : reserved17_w1
	 */
	u8 sq_caps_3;

	/* associated completion queue id. This CQ must be created prior to
	 *    SQ creation
	 */
	u16 cq_idx;

	/* submission queue depth in entries */
	u16 sq_depth;

	/* SQ physical base address in OS memory. This field should not be
	 * used for Low Latency queues. Has to be page aligned.
	 */
	struct ena_common_mem_addr sq_ba;

	/* specifies queue head writeback location in OS memory. Valid if
	 * completion_policy is set to completion_policy_head_on_demand or
	 * completion_policy_head. Has to be cache aligned
	 */
	struct ena_common_mem_addr sq_head_writeback;

	u32 reserved0_w7;

	u32 reserved0_w8;
};

enum ena_admin_sq_direction {
	ENA_ADMIN_SQ_DIRECTION_TX                   = 1,
	ENA_ADMIN_SQ_DIRECTION_RX                   = 2,
};

struct ena_admin_acq_create_sq_resp_desc {
	struct ena_admin_acq_common_desc acq_common_desc;

	u16 sq_idx;

	u16 reserved;

	/* queue doorbell address as an offset to PCIe MMIO REG BAR */
	u32 sq_doorbell_offset;

	/* low latency queue ring base address as an offset to PCIe MMIO
	 * LLQ_MEM BAR
	 */
	u32 llq_descriptors_offset;

	/* low latency queue headers' memory as an offset to PCIe MMIO
	 * LLQ_MEM BAR
	 */
	u32 llq_headers_offset;
};

struct ena_admin_aq_destroy_sq_cmd {
	struct ena_admin_aq_common_desc aq_common_descriptor;

	struct ena_admin_sq sq;
};

struct ena_admin_acq_destroy_sq_resp_desc {
	struct ena_admin_acq_common_desc acq_common_desc;
};

struct ena_admin_aq_create_cq_cmd {
	struct ena_admin_aq_common_desc aq_common_descriptor;

	/* 4:0 : reserved5
	 * 5 : interrupt_mode_enabled - if set, cq operates
	 *    in interrupt mode, otherwise - polling
	 * 7:6 : reserved6
	 */
	u8 cq_caps_1;

	/* 4:0 : cq_entry_size_words - size of CQ entry in
	 *    32-bit words, valid values: 4, 8.
	 * 7:5 : reserved7
	 */
	u8 cq_caps_2;

	/* completion queue depth in # of entries. must be power of 2 */
	u16 cq_depth;

	/* msix vector assigned to this cq */
	u32 msix_vector;

	/* cq physical base address in OS memory. CQ must be physically
	 * contiguous
	 */
	struct ena_common_mem_addr cq_ba;
};

struct ena_admin_acq_create_cq_resp_desc {
	struct ena_admin_acq_common_desc acq_common_desc;

	u16 cq_idx;

	/* actual cq depth in number of entries */
	u16 cq_actual_depth;

	u32 numa_node_register_offset;

	u32 cq_head_db_register_offset;

	u32 cq_interrupt_unmask_register_offset;
};

struct ena_admin_aq_destroy_cq_cmd {
	struct ena_admin_aq_common_desc aq_common_descriptor;

	u16 cq_idx;

	u16 reserved1;
};

struct ena_admin_acq_destroy_cq_resp_desc {
	struct ena_admin_acq_common_desc acq_common_desc;
};

/* ENA AQ Get Statistics command. Extended statistics are placed in control
 * buffer pointed by AQ entry
 */
struct ena_admin_aq_get_stats_cmd {
	struct ena_admin_aq_common_desc aq_common_descriptor;

	union {
		/* command specific inline data */
		u32 inline_data_w1[3];

		struct ena_admin_ctrl_buff_info control_buffer;
	} u;

	/* stats type as defined in enum ena_admin_get_stats_type */
	u8 type;

	/* stats scope defined in enum ena_admin_get_stats_scope */
	u8 scope;

	u16 reserved3;

	/* queue id. used when scope is specific_queue */
	u16 queue_idx;

	/* device id, value 0xFFFF means mine. only privileged device can get
	 *    stats of other device
	 */
	u16 device_id;
};

/* Basic Statistics Command. */
struct ena_admin_basic_stats {
	u32 tx_bytes_low;

	u32 tx_bytes_high;

	u32 tx_pkts_low;

	u32 tx_pkts_high;

	u32 rx_bytes_low;

	u32 rx_bytes_high;

	u32 rx_pkts_low;

	u32 rx_pkts_high;

	u32 rx_drops_low;

	u32 rx_drops_high;

	u32 tx_drops_low;

	u32 tx_drops_high;
};

struct ena_admin_acq_get_stats_resp {
	struct ena_admin_acq_common_desc acq_common_desc;

	struct ena_admin_basic_stats basic_stats;
};

struct ena_admin_get_set_feature_common_desc {
	/* 1:0 : select - 0x1 - current value; 0x3 - default
	 *    value
	 * 7:3 : reserved3
	 */
	u8 flags;

	/* as appears in ena_admin_aq_feature_id */
	u8 feature_id;

	/* The driver specifies the max feature version it supports and the
	 * device responds with the currently supported feature version. The
	 * field is zero based
	 */
	u8 feature_version;

	u8 reserved8;
};

struct ena_admin_device_attr_feature_desc {
	u32 impl_id;

	u32 device_version;

	/* bitmap of ena_admin_aq_feature_id */
	u32 supported_features;

	u32 reserved3;

	/* Indicates how many bits are used physical address access. */
	u32 phys_addr_width;

	/* Indicates how many bits are used virtual address access. */
	u32 virt_addr_width;

	/* unicast MAC address (in Network byte order) */
	u8 mac_addr[6];

	u8 reserved7[2];

	u32 max_mtu;
};

enum ena_admin_llq_header_location {
	/* header is in descriptor list */
	ENA_ADMIN_INLINE_HEADER                     = 1,
	/* header in a separate ring, implies 16B descriptor list entry */
	ENA_ADMIN_HEADER_RING                       = 2,
};

enum ena_admin_llq_ring_entry_size {
	ENA_ADMIN_LIST_ENTRY_SIZE_128B              = 1,
	ENA_ADMIN_LIST_ENTRY_SIZE_192B              = 2,
	ENA_ADMIN_LIST_ENTRY_SIZE_256B              = 4,
};

enum ena_admin_llq_num_descs_before_header {
	ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_0     = 0,
	ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_1     = 1,
	ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_2     = 2,
	ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_4     = 4,
	ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_8     = 8,
};

/* packet descriptor list entry always starts with one or more descriptors,
 * followed by a header. The rest of the descriptors are located in the
 * beginning of the subsequent entry. Stride refers to how the rest of the
 * descriptors are placed. This field is relevant only for inline header
 * mode
 */
enum ena_admin_llq_stride_ctrl {
	ENA_ADMIN_SINGLE_DESC_PER_ENTRY             = 1,
	ENA_ADMIN_MULTIPLE_DESCS_PER_ENTRY          = 2,
};

struct ena_admin_feature_llq_desc {
	u32 max_llq_num;

	u32 max_llq_depth;

	/*  specify the header locations the device supports. bitfield of
	 *    enum ena_admin_llq_header_location.
	 */
	u16 header_location_ctrl_supported;

	/* the header location the driver selected to use. */
	u16 header_location_ctrl_enabled;

	/* if inline header is specified - this is the size of descriptor
	 *    list entry. If header in a separate ring is specified - this is
	 *    the size of header ring entry. bitfield of enum
	 *    ena_admin_llq_ring_entry_size. specify the entry sizes the device
	 *    supports
	 */
	u16 entry_size_ctrl_supported;

	/* the entry size the driver selected to use. */
	u16 entry_size_ctrl_enabled;

	/* valid only if inline header is specified. First entry associated
	 *    with the packet includes descriptors and header. Rest of the
	 *    entries occupied by descriptors. This parameter defines the max
	 *    number of descriptors precedding the header in the first entry.
	 *    The field is bitfield of enum
	 *    ena_admin_llq_num_descs_before_header and specify the values the
	 *    device supports
	 */
	u16 desc_num_before_header_supported;

	/* the desire field the driver selected to use */
	u16 desc_num_before_header_enabled;

	/* valid only if inline was chosen. bitfield of enum
	 *    ena_admin_llq_stride_ctrl
	 */
	u16 descriptors_stride_ctrl_supported;

	/* the stride control the driver selected to use */
	u16 descriptors_stride_ctrl_enabled;

	/* Maximum size in bytes taken by llq entries in a single tx burst.
	 * Set to 0 when there is no such limit.
	 */
	u32 max_tx_burst_size;
};

struct ena_admin_queue_ext_feature_fields {
	u32 max_tx_sq_num;

	u32 max_tx_cq_num;

	u32 max_rx_sq_num;

	u32 max_rx_cq_num;

	u32 max_tx_sq_depth;

	u32 max_tx_cq_depth;

	u32 max_rx_sq_depth;

	u32 max_rx_cq_depth;

	u32 max_tx_header_size;

	/* Maximum Descriptors number, including meta descriptor, allowed for
	 * a single Tx packet
	 */
	u16 max_per_packet_tx_descs;

	/* Maximum Descriptors number allowed for a single Rx packet */
	u16 max_per_packet_rx_descs;
};

struct ena_admin_queue_feature_desc {
	u32 max_sq_num;

	u32 max_sq_depth;

	u32 max_cq_num;

	u32 max_cq_depth;

	u32 max_legacy_llq_num;

	u32 max_legacy_llq_depth;

	u32 max_header_size;

	/* Maximum Descriptors number, including meta descriptor, allowed for
	 *    a single Tx packet
	 */
	u16 max_packet_tx_descs;

	/* Maximum Descriptors number allowed for a single Rx packet */
	u16 max_packet_rx_descs;
};

struct ena_admin_set_feature_mtu_desc {
	/* exclude L2 */
	u32 mtu;
};

struct ena_admin_set_feature_host_attr_desc {
	/* host OS info base address in OS memory. host info is 4KB of
	 * physically contiguous
	 */
	struct ena_common_mem_addr os_info_ba;

	/* host debug area base address in OS memory. debug area must be
	 * physically contiguous
	 */
	struct ena_common_mem_addr debug_ba;

	/* debug area size */
	u32 debug_area_size;
};

struct ena_admin_feature_intr_moder_desc {
	/* interrupt delay granularity in usec */
	u16 intr_delay_resolution;

	u16 reserved;
};

struct ena_admin_get_feature_link_desc {
	/* Link speed in Mb */
	u32 speed;

	/* bit field of enum ena_admin_link types */
	u32 supported;

	/* 0 : autoneg
	 * 1 : duplex - Full Duplex
	 * 31:2 : reserved2
	 */
	u32 flags;
};

struct ena_admin_feature_aenq_desc {
	/* bitmask for AENQ groups the device can report */
	u32 supported_groups;

	/* bitmask for AENQ groups to report */
	u32 enabled_groups;
};

struct ena_admin_feature_offload_desc {
	/* 0 : TX_L3_csum_ipv4
	 * 1 : TX_L4_ipv4_csum_part - The checksum field
	 *    should be initialized with pseudo header checksum
	 * 2 : TX_L4_ipv4_csum_full
	 * 3 : TX_L4_ipv6_csum_part - The checksum field
	 *    should be initialized with pseudo header checksum
	 * 4 : TX_L4_ipv6_csum_full
	 * 5 : tso_ipv4
	 * 6 : tso_ipv6
	 * 7 : tso_ecn
	 */
	u32 tx;

	/* Receive side supported stateless offload
	 * 0 : RX_L3_csum_ipv4 - IPv4 checksum
	 * 1 : RX_L4_ipv4_csum - TCP/UDP/IPv4 checksum
	 * 2 : RX_L4_ipv6_csum - TCP/UDP/IPv6 checksum
	 * 3 : RX_hash - Hash calculation
	 */
	u32 rx_supported;

	u32 rx_enabled;
};

enum ena_admin_hash_functions {
	ENA_ADMIN_TOEPLITZ                          = 1,
	ENA_ADMIN_CRC32                             = 2,
};

struct ena_admin_feature_rss_flow_hash_control {
	u32 keys_num;

	u32 reserved;

	u32 key[10];
};

struct ena_admin_feature_rss_flow_hash_function {
	/* 7:0 : funcs - bitmask of ena_admin_hash_functions */
	u32 supported_func;

	/* 7:0 : selected_func - bitmask of
	 *    ena_admin_hash_functions
	 */
	u32 selected_func;

	/* initial value */
	u32 init_val;
};

/* RSS flow hash protocols */
enum ena_admin_flow_hash_proto {
	ENA_ADMIN_RSS_TCP4                          = 0,
	ENA_ADMIN_RSS_UDP4                          = 1,
	ENA_ADMIN_RSS_TCP6                          = 2,
	ENA_ADMIN_RSS_UDP6                          = 3,
	ENA_ADMIN_RSS_IP4                           = 4,
	ENA_ADMIN_RSS_IP6                           = 5,
	ENA_ADMIN_RSS_IP4_FRAG                      = 6,
	ENA_ADMIN_RSS_NOT_IP                        = 7,
	/* TCPv6 with extension header */
	ENA_ADMIN_RSS_TCP6_EX                       = 8,
	/* IPv6 with extension header */
	ENA_ADMIN_RSS_IP6_EX                        = 9,
	ENA_ADMIN_RSS_PROTO_NUM                     = 16,
};

/* RSS flow hash fields */
enum ena_admin_flow_hash_fields {
	/* Ethernet Dest Addr */
	ENA_ADMIN_RSS_L2_DA                         = BIT(0),
	/* Ethernet Src Addr */
	ENA_ADMIN_RSS_L2_SA                         = BIT(1),
	/* ipv4/6 Dest Addr */
	ENA_ADMIN_RSS_L3_DA                         = BIT(2),
	/* ipv4/6 Src Addr */
	ENA_ADMIN_RSS_L3_SA                         = BIT(3),
	/* tcp/udp Dest Port */
	ENA_ADMIN_RSS_L4_DP                         = BIT(4),
	/* tcp/udp Src Port */
	ENA_ADMIN_RSS_L4_SP                         = BIT(5),
};

struct ena_admin_proto_input {
	/* flow hash fields (bitwise according to ena_admin_flow_hash_fields) */
	u16 fields;

	u16 reserved2;
};

struct ena_admin_feature_rss_hash_control {
	struct ena_admin_proto_input supported_fields[ENA_ADMIN_RSS_PROTO_NUM];

	struct ena_admin_proto_input selected_fields[ENA_ADMIN_RSS_PROTO_NUM];

	struct ena_admin_proto_input reserved2[ENA_ADMIN_RSS_PROTO_NUM];

	struct ena_admin_proto_input reserved3[ENA_ADMIN_RSS_PROTO_NUM];
};

struct ena_admin_feature_rss_flow_hash_input {
	/* supported hash input sorting
	 * 1 : L3_sort - support swap L3 addresses if DA is
	 *    smaller than SA
	 * 2 : L4_sort - support swap L4 ports if DP smaller
	 *    SP
	 */
	u16 supported_input_sort;

	/* enabled hash input sorting
	 * 1 : enable_L3_sort - enable swap L3 addresses if
	 *    DA smaller than SA
	 * 2 : enable_L4_sort - enable swap L4 ports if DP
	 *    smaller than SP
	 */
	u16 enabled_input_sort;
};

enum ena_admin_os_type {
	ENA_ADMIN_OS_LINUX                          = 1,
	ENA_ADMIN_OS_WIN                            = 2,
	ENA_ADMIN_OS_DPDK                           = 3,
	ENA_ADMIN_OS_FREEBSD                        = 4,
	ENA_ADMIN_OS_IPXE                           = 5,
	ENA_ADMIN_OS_ESXI                           = 6,
	ENA_ADMIN_OS_GROUPS_NUM                     = 6,
};

struct ena_admin_host_info {
	/* defined in enum ena_admin_os_type */
	u32 os_type;

	/* os distribution string format */
	u8 os_dist_str[128];

	/* OS distribution numeric format */
	u32 os_dist;

	/* kernel version string format */
	u8 kernel_ver_str[32];

	/* Kernel version numeric format */
	u32 kernel_ver;

	/* 7:0 : major
	 * 15:8 : minor
	 * 23:16 : sub_minor
	 * 31:24 : module_type
	 */
	u32 driver_version;

	/* features bitmap */
	u32 supported_network_features[2];

	/* ENA spec version of driver */
	u16 ena_spec_version;

	/* ENA device's Bus, Device and Function
	 * 2:0 : function
	 * 7:3 : device
	 * 15:8 : bus
	 */
	u16 bdf;

	/* Number of CPUs */
	u16 num_cpus;

	u16 reserved;

	/* 0 : reserved
	 * 1 : rx_offset
	 * 2 : interrupt_moderation
	 * 31:3 : reserved
	 */
	u32 driver_supported_features;
};

struct ena_admin_rss_ind_table_entry {
	u16 cq_idx;

	u16 reserved;
};

struct ena_admin_feature_rss_ind_table {
	/* min supported table size (2^min_size) */
	u16 min_size;

	/* max supported table size (2^max_size) */
	u16 max_size;

	/* table size (2^size) */
	u16 size;

	u16 reserved;

	/* index of the inline entry. 0xFFFFFFFF means invalid */
	u32 inline_index;

	/* used for updating single entry, ignored when setting the entire
	 * table through the control buffer.
	 */
	struct ena_admin_rss_ind_table_entry inline_entry;
};

/* When hint value is 0, driver should use it's own predefined value */
struct ena_admin_ena_hw_hints {
	/* value in ms */
	u16 mmio_read_timeout;

	/* value in ms */
	u16 driver_watchdog_timeout;

	/* Per packet tx completion timeout. value in ms */
	u16 missing_tx_completion_timeout;

	u16 missed_tx_completion_count_threshold_to_reset;

	/* value in ms */
	u16 admin_completion_tx_timeout;

	u16 netdev_wd_timeout;

	u16 max_tx_sgl_size;

	u16 max_rx_sgl_size;

	u16 reserved[8];
};

struct ena_admin_get_feat_cmd {
	struct ena_admin_aq_common_desc aq_common_descriptor;

	struct ena_admin_ctrl_buff_info control_buffer;

	struct ena_admin_get_set_feature_common_desc feat_common;

	u32 raw[11];
};

struct ena_admin_queue_ext_feature_desc {
	/* version */
	u8 version;

	u8 reserved1[3];

	union {
		struct ena_admin_queue_ext_feature_fields max_queue_ext;

		u32 raw[10];
	};
};

struct ena_admin_get_feat_resp {
	struct ena_admin_acq_common_desc acq_common_desc;

	union {
		u32 raw[14];

		struct ena_admin_device_attr_feature_desc dev_attr;

		struct ena_admin_feature_llq_desc llq;

		struct ena_admin_queue_feature_desc max_queue;

		struct ena_admin_queue_ext_feature_desc max_queue_ext;

		struct ena_admin_feature_aenq_desc aenq;

		struct ena_admin_get_feature_link_desc link;

		struct ena_admin_feature_offload_desc offload;

		struct ena_admin_feature_rss_flow_hash_function flow_hash_func;

		struct ena_admin_feature_rss_flow_hash_input flow_hash_input;

		struct ena_admin_feature_rss_ind_table ind_table;

		struct ena_admin_feature_intr_moder_desc intr_moderation;

		struct ena_admin_ena_hw_hints hw_hints;
	} u;
};

struct ena_admin_set_feat_cmd {
	struct ena_admin_aq_common_desc aq_common_descriptor;

	struct ena_admin_ctrl_buff_info control_buffer;

	struct ena_admin_get_set_feature_common_desc feat_common;

	union {
		u32 raw[11];

		/* mtu size */
		struct ena_admin_set_feature_mtu_desc mtu;

		/* host attributes */
		struct ena_admin_set_feature_host_attr_desc host_attr;

		/* AENQ configuration */
		struct ena_admin_feature_aenq_desc aenq;

		/* rss flow hash function */
		struct ena_admin_feature_rss_flow_hash_function flow_hash_func;

		/* rss flow hash input */
		struct ena_admin_feature_rss_flow_hash_input flow_hash_input;

		/* rss indirection table */
		struct ena_admin_feature_rss_ind_table ind_table;

		/* LLQ configuration */
		struct ena_admin_feature_llq_desc llq;
	} u;
};

struct ena_admin_set_feat_resp {
	struct ena_admin_acq_common_desc acq_common_desc;

	union {
		u32 raw[14];
	} u;
};

struct ena_admin_aenq_common_desc {
	u16 group;

	u16 syndrom;

	/* 0 : phase
	 * 7:1 : reserved - MBZ
	 */
	u8 flags;

	u8 reserved1[3];

	u32 timestamp_low;

	u32 timestamp_high;
};

/* asynchronous event notification groups */
enum ena_admin_aenq_group {
	ENA_ADMIN_LINK_CHANGE                       = 0,
	ENA_ADMIN_FATAL_ERROR                       = 1,
	ENA_ADMIN_WARNING                           = 2,
	ENA_ADMIN_NOTIFICATION                      = 3,
	ENA_ADMIN_KEEP_ALIVE                        = 4,
	ENA_ADMIN_AENQ_GROUPS_NUM                   = 5,
};

enum ena_admin_aenq_notification_syndrom {
	ENA_ADMIN_SUSPEND                           = 0,
	ENA_ADMIN_RESUME                            = 1,
	ENA_ADMIN_UPDATE_HINTS                      = 2,
};

struct ena_admin_aenq_entry {
	struct ena_admin_aenq_common_desc aenq_common_desc;

	/* command specific inline data */
	u32 inline_data_w4[12];
};

struct ena_admin_aenq_link_change_desc {
	struct ena_admin_aenq_common_desc aenq_common_desc;

	/* 0 : link_status */
	u32 flags;
};

struct ena_admin_aenq_keep_alive_desc {
	struct ena_admin_aenq_common_desc aenq_common_desc;

	u32 rx_drops_low;

	u32 rx_drops_high;

	u32 tx_drops_low;

	u32 tx_drops_high;
};

struct ena_admin_ena_mmio_req_read_less_resp {
	u16 req_id;

	u16 reg_off;

	/* value is valid when poll is cleared */
	u32 reg_val;
};

/* aq_common_desc */
#define ENA_ADMIN_AQ_COMMON_DESC_COMMAND_ID_MASK            GENMASK(11, 0)
#define ENA_ADMIN_AQ_COMMON_DESC_PHASE_MASK                 BIT(0)
#define ENA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_SHIFT            1
#define ENA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_MASK             BIT(1)
#define ENA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT_SHIFT   2
#define ENA_ADMIN_AQ_COMMON_DESC_CTRL_DATA_INDIRECT_MASK    BIT(2)

/* sq */
#define ENA_ADMIN_SQ_SQ_DIRECTION_SHIFT                     5
#define ENA_ADMIN_SQ_SQ_DIRECTION_MASK                      GENMASK(7, 5)

/* acq_common_desc */
#define ENA_ADMIN_ACQ_COMMON_DESC_COMMAND_ID_MASK           GENMASK(11, 0)
#define ENA_ADMIN_ACQ_COMMON_DESC_PHASE_MASK                BIT(0)

/* aq_create_sq_cmd */
#define ENA_ADMIN_AQ_CREATE_SQ_CMD_SQ_DIRECTION_SHIFT       5
#define ENA_ADMIN_AQ_CREATE_SQ_CMD_SQ_DIRECTION_MASK        GENMASK(7, 5)
#define ENA_ADMIN_AQ_CREATE_SQ_CMD_PLACEMENT_POLICY_MASK    GENMASK(3, 0)
#define ENA_ADMIN_AQ_CREATE_SQ_CMD_COMPLETION_POLICY_SHIFT  4
#define ENA_ADMIN_AQ_CREATE_SQ_CMD_COMPLETION_POLICY_MASK   GENMASK(6, 4)
#define ENA_ADMIN_AQ_CREATE_SQ_CMD_IS_PHYSICALLY_CONTIGUOUS_MASK BIT(0)

/* aq_create_cq_cmd */
#define ENA_ADMIN_AQ_CREATE_CQ_CMD_INTERRUPT_MODE_ENABLED_SHIFT 5
#define ENA_ADMIN_AQ_CREATE_CQ_CMD_INTERRUPT_MODE_ENABLED_MASK BIT(5)
#define ENA_ADMIN_AQ_CREATE_CQ_CMD_CQ_ENTRY_SIZE_WORDS_MASK GENMASK(4, 0)

/* get_set_feature_common_desc */
#define ENA_ADMIN_GET_SET_FEATURE_COMMON_DESC_SELECT_MASK   GENMASK(1, 0)

/* get_feature_link_desc */
#define ENA_ADMIN_GET_FEATURE_LINK_DESC_AUTONEG_MASK        BIT(0)
#define ENA_ADMIN_GET_FEATURE_LINK_DESC_DUPLEX_SHIFT        1
#define ENA_ADMIN_GET_FEATURE_LINK_DESC_DUPLEX_MASK         BIT(1)

/* feature_offload_desc */
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L3_CSUM_IPV4_MASK BIT(0)
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV4_CSUM_PART_SHIFT 1
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV4_CSUM_PART_MASK BIT(1)
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV4_CSUM_FULL_SHIFT 2
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV4_CSUM_FULL_MASK BIT(2)
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV6_CSUM_PART_SHIFT 3
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV6_CSUM_PART_MASK BIT(3)
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV6_CSUM_FULL_SHIFT 4
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TX_L4_IPV6_CSUM_FULL_MASK BIT(4)
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TSO_IPV4_SHIFT       5
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TSO_IPV4_MASK        BIT(5)
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TSO_IPV6_SHIFT       6
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TSO_IPV6_MASK        BIT(6)
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TSO_ECN_SHIFT        7
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_TSO_ECN_MASK         BIT(7)
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_RX_L3_CSUM_IPV4_MASK BIT(0)
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_RX_L4_IPV4_CSUM_SHIFT 1
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_RX_L4_IPV4_CSUM_MASK BIT(1)
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_RX_L4_IPV6_CSUM_SHIFT 2
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_RX_L4_IPV6_CSUM_MASK BIT(2)
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_RX_HASH_SHIFT        3
#define ENA_ADMIN_FEATURE_OFFLOAD_DESC_RX_HASH_MASK         BIT(3)

/* feature_rss_flow_hash_function */
#define ENA_ADMIN_FEATURE_RSS_FLOW_HASH_FUNCTION_FUNCS_MASK GENMASK(7, 0)
#define ENA_ADMIN_FEATURE_RSS_FLOW_HASH_FUNCTION_SELECTED_FUNC_MASK GENMASK(7, 0)

/* feature_rss_flow_hash_input */
#define ENA_ADMIN_FEATURE_RSS_FLOW_HASH_INPUT_L3_SORT_SHIFT 1
#define ENA_ADMIN_FEATURE_RSS_FLOW_HASH_INPUT_L3_SORT_MASK  BIT(1)
#define ENA_ADMIN_FEATURE_RSS_FLOW_HASH_INPUT_L4_SORT_SHIFT 2
#define ENA_ADMIN_FEATURE_RSS_FLOW_HASH_INPUT_L4_SORT_MASK  BIT(2)
#define ENA_ADMIN_FEATURE_RSS_FLOW_HASH_INPUT_ENABLE_L3_SORT_SHIFT 1
#define ENA_ADMIN_FEATURE_RSS_FLOW_HASH_INPUT_ENABLE_L3_SORT_MASK BIT(1)
#define ENA_ADMIN_FEATURE_RSS_FLOW_HASH_INPUT_ENABLE_L4_SORT_SHIFT 2
#define ENA_ADMIN_FEATURE_RSS_FLOW_HASH_INPUT_ENABLE_L4_SORT_MASK BIT(2)

/* host_info */
#define ENA_ADMIN_HOST_INFO_MAJOR_MASK                      GENMASK(7, 0)
#define ENA_ADMIN_HOST_INFO_MINOR_SHIFT                     8
#define ENA_ADMIN_HOST_INFO_MINOR_MASK                      GENMASK(15, 8)
#define ENA_ADMIN_HOST_INFO_SUB_MINOR_SHIFT                 16
#define ENA_ADMIN_HOST_INFO_SUB_MINOR_MASK                  GENMASK(23, 16)
#define ENA_ADMIN_HOST_INFO_MODULE_TYPE_SHIFT               24
#define ENA_ADMIN_HOST_INFO_MODULE_TYPE_MASK                GENMASK(31, 24)
#define ENA_ADMIN_HOST_INFO_FUNCTION_MASK                   GENMASK(2, 0)
#define ENA_ADMIN_HOST_INFO_DEVICE_SHIFT                    3
#define ENA_ADMIN_HOST_INFO_DEVICE_MASK                     GENMASK(7, 3)
#define ENA_ADMIN_HOST_INFO_BUS_SHIFT                       8
#define ENA_ADMIN_HOST_INFO_BUS_MASK                        GENMASK(15, 8)
#define ENA_ADMIN_HOST_INFO_RX_OFFSET_SHIFT                 1
#define ENA_ADMIN_HOST_INFO_RX_OFFSET_MASK                  BIT(1)
#define ENA_ADMIN_HOST_INFO_INTERRUPT_MODERATION_SHIFT      2
#define ENA_ADMIN_HOST_INFO_INTERRUPT_MODERATION_MASK       BIT(2)

/* aenq_common_desc */
#define ENA_ADMIN_AENQ_COMMON_DESC_PHASE_MASK               BIT(0)

/* aenq_link_change_desc */
#define ENA_ADMIN_AENQ_LINK_CHANGE_DESC_LINK_STATUS_MASK    BIT(0)

#endif /* _ENA_ADMIN_H_ */
