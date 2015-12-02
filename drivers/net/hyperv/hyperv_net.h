/*
 *
 * Copyright (c) 2011, Microsoft Corporation.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *   K. Y. Srinivasan <kys@microsoft.com>
 *
 */

#ifndef _HYPERV_NET_H
#define _HYPERV_NET_H

#include <linux/list.h>
#include <linux/hyperv.h>
#include <linux/rndis.h>

/* RSS related */
#define OID_GEN_RECEIVE_SCALE_CAPABILITIES 0x00010203  /* query only */
#define OID_GEN_RECEIVE_SCALE_PARAMETERS 0x00010204  /* query and set */

#define NDIS_OBJECT_TYPE_RSS_CAPABILITIES 0x88
#define NDIS_OBJECT_TYPE_RSS_PARAMETERS 0x89

#define NDIS_RECEIVE_SCALE_CAPABILITIES_REVISION_2 2
#define NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_2 2

struct ndis_obj_header {
	u8 type;
	u8 rev;
	u16 size;
} __packed;

/* ndis_recv_scale_cap/cap_flag */
#define NDIS_RSS_CAPS_MESSAGE_SIGNALED_INTERRUPTS 0x01000000
#define NDIS_RSS_CAPS_CLASSIFICATION_AT_ISR       0x02000000
#define NDIS_RSS_CAPS_CLASSIFICATION_AT_DPC       0x04000000
#define NDIS_RSS_CAPS_USING_MSI_X                 0x08000000
#define NDIS_RSS_CAPS_RSS_AVAILABLE_ON_PORTS      0x10000000
#define NDIS_RSS_CAPS_SUPPORTS_MSI_X              0x20000000
#define NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV4          0x00000100
#define NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6          0x00000200
#define NDIS_RSS_CAPS_HASH_TYPE_TCP_IPV6_EX       0x00000400

struct ndis_recv_scale_cap { /* NDIS_RECEIVE_SCALE_CAPABILITIES */
	struct ndis_obj_header hdr;
	u32 cap_flag;
	u32 num_int_msg;
	u32 num_recv_que;
	u16 num_indirect_tabent;
} __packed;


/* ndis_recv_scale_param flags */
#define NDIS_RSS_PARAM_FLAG_BASE_CPU_UNCHANGED     0x0001
#define NDIS_RSS_PARAM_FLAG_HASH_INFO_UNCHANGED    0x0002
#define NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED       0x0004
#define NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED     0x0008
#define NDIS_RSS_PARAM_FLAG_DISABLE_RSS            0x0010

/* Hash info bits */
#define NDIS_HASH_FUNC_TOEPLITZ 0x00000001
#define NDIS_HASH_IPV4          0x00000100
#define NDIS_HASH_TCP_IPV4      0x00000200
#define NDIS_HASH_IPV6          0x00000400
#define NDIS_HASH_IPV6_EX       0x00000800
#define NDIS_HASH_TCP_IPV6      0x00001000
#define NDIS_HASH_TCP_IPV6_EX   0x00002000

#define NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_2 (128 * 4)
#define NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_2   40

#define ITAB_NUM 128
#define HASH_KEYLEN NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_2
extern u8 netvsc_hash_key[];

struct ndis_recv_scale_param { /* NDIS_RECEIVE_SCALE_PARAMETERS */
	struct ndis_obj_header hdr;

	/* Qualifies the rest of the information */
	u16 flag;

	/* The base CPU number to do receive processing. not used */
	u16 base_cpu_number;

	/* This describes the hash function and type being enabled */
	u32 hashinfo;

	/* The size of indirection table array */
	u16 indirect_tabsize;

	/* The offset of the indirection table from the beginning of this
	 * structure
	 */
	u32 indirect_taboffset;

	/* The size of the hash secret key */
	u16 hashkey_size;

	/* The offset of the secret key from the beginning of this structure */
	u32 kashkey_offset;

	u32 processor_masks_offset;
	u32 num_processor_masks;
	u32 processor_masks_entry_size;
};

/* Fwd declaration */
struct ndis_tcp_ip_checksum_info;

/*
 * Represent netvsc packet which contains 1 RNDIS and 1 ethernet frame
 * within the RNDIS
 */
struct hv_netvsc_packet {
	/* Bookkeeping stuff */
	u8 status;
	u8 is_data_pkt;
	u8 xmit_more; /* from skb */
	u8 cp_partial; /* partial copy into send buffer */

	u8 rmsg_size; /* RNDIS header and PPI size */
	u8 rmsg_pgcnt; /* page count of RNDIS header and PPI */
	u8 page_buf_cnt;
	u8 pad0;

	u16 vlan_tci;
	u16 q_idx;
	u32 send_buf_index;

	u32 total_data_buflen;
	u32 pad1;

	struct vmbus_channel *channel;

	u64 send_completion_tid;
	void *send_completion_ctx;
	void (*send_completion)(void *context);


	/* This points to the memory after page_buf */
	struct rndis_message *rndis_msg;

	/* Points to the send/receive buffer where the ethernet frame is */
	void *data;
	struct hv_page_buffer *page_buf;
};

struct netvsc_device_info {
	unsigned char mac_adr[ETH_ALEN];
	bool link_state;	/* 0 - link up, 1 - link down */
	int  ring_size;
	u32  max_num_vrss_chns;
	u32  num_chn;
};

enum rndis_device_state {
	RNDIS_DEV_UNINITIALIZED = 0,
	RNDIS_DEV_INITIALIZING,
	RNDIS_DEV_INITIALIZED,
	RNDIS_DEV_DATAINITIALIZED,
};

struct rndis_device {
	struct netvsc_device *net_dev;

	enum rndis_device_state state;
	bool link_state;
	atomic_t new_req_id;

	spinlock_t request_lock;
	struct list_head req_list;

	unsigned char hw_mac_adr[ETH_ALEN];
};


/* Interface */
int netvsc_device_add(struct hv_device *device, void *additional_info);
int netvsc_device_remove(struct hv_device *device);
int netvsc_send(struct hv_device *device,
		struct hv_netvsc_packet *packet);
void netvsc_linkstatus_callback(struct hv_device *device_obj,
				struct rndis_message *resp);
void netvsc_xmit_completion(void *context);
int netvsc_recv_callback(struct hv_device *device_obj,
			struct hv_netvsc_packet *packet,
			struct ndis_tcp_ip_checksum_info *csum_info);
void netvsc_channel_cb(void *context);
int rndis_filter_open(struct hv_device *dev);
int rndis_filter_close(struct hv_device *dev);
int rndis_filter_device_add(struct hv_device *dev,
			void *additional_info);
void rndis_filter_device_remove(struct hv_device *dev);
int rndis_filter_receive(struct hv_device *dev,
			struct hv_netvsc_packet *pkt);

int rndis_filter_set_packet_filter(struct rndis_device *dev, u32 new_filter);
int rndis_filter_set_device_mac(struct hv_device *hdev, char *mac);


#define NVSP_INVALID_PROTOCOL_VERSION	((u32)0xFFFFFFFF)

#define NVSP_PROTOCOL_VERSION_1		2
#define NVSP_PROTOCOL_VERSION_2		0x30002
#define NVSP_PROTOCOL_VERSION_4		0x40000
#define NVSP_PROTOCOL_VERSION_5		0x50000

enum {
	NVSP_MSG_TYPE_NONE = 0,

	/* Init Messages */
	NVSP_MSG_TYPE_INIT			= 1,
	NVSP_MSG_TYPE_INIT_COMPLETE		= 2,

	NVSP_VERSION_MSG_START			= 100,

	/* Version 1 Messages */
	NVSP_MSG1_TYPE_SEND_NDIS_VER		= NVSP_VERSION_MSG_START,

	NVSP_MSG1_TYPE_SEND_RECV_BUF,
	NVSP_MSG1_TYPE_SEND_RECV_BUF_COMPLETE,
	NVSP_MSG1_TYPE_REVOKE_RECV_BUF,

	NVSP_MSG1_TYPE_SEND_SEND_BUF,
	NVSP_MSG1_TYPE_SEND_SEND_BUF_COMPLETE,
	NVSP_MSG1_TYPE_REVOKE_SEND_BUF,

	NVSP_MSG1_TYPE_SEND_RNDIS_PKT,
	NVSP_MSG1_TYPE_SEND_RNDIS_PKT_COMPLETE,

	/* Version 2 messages */
	NVSP_MSG2_TYPE_SEND_CHIMNEY_DELEGATED_BUF,
	NVSP_MSG2_TYPE_SEND_CHIMNEY_DELEGATED_BUF_COMP,
	NVSP_MSG2_TYPE_REVOKE_CHIMNEY_DELEGATED_BUF,

	NVSP_MSG2_TYPE_RESUME_CHIMNEY_RX_INDICATION,

	NVSP_MSG2_TYPE_TERMINATE_CHIMNEY,
	NVSP_MSG2_TYPE_TERMINATE_CHIMNEY_COMP,

	NVSP_MSG2_TYPE_INDICATE_CHIMNEY_EVENT,

	NVSP_MSG2_TYPE_SEND_CHIMNEY_PKT,
	NVSP_MSG2_TYPE_SEND_CHIMNEY_PKT_COMP,

	NVSP_MSG2_TYPE_POST_CHIMNEY_RECV_REQ,
	NVSP_MSG2_TYPE_POST_CHIMNEY_RECV_REQ_COMP,

	NVSP_MSG2_TYPE_ALLOC_RXBUF,
	NVSP_MSG2_TYPE_ALLOC_RXBUF_COMP,

	NVSP_MSG2_TYPE_FREE_RXBUF,

	NVSP_MSG2_TYPE_SEND_VMQ_RNDIS_PKT,
	NVSP_MSG2_TYPE_SEND_VMQ_RNDIS_PKT_COMP,

	NVSP_MSG2_TYPE_SEND_NDIS_CONFIG,

	NVSP_MSG2_TYPE_ALLOC_CHIMNEY_HANDLE,
	NVSP_MSG2_TYPE_ALLOC_CHIMNEY_HANDLE_COMP,

	NVSP_MSG2_MAX = NVSP_MSG2_TYPE_ALLOC_CHIMNEY_HANDLE_COMP,

	/* Version 4 messages */
	NVSP_MSG4_TYPE_SEND_VF_ASSOCIATION,
	NVSP_MSG4_TYPE_SWITCH_DATA_PATH,
	NVSP_MSG4_TYPE_UPLINK_CONNECT_STATE_DEPRECATED,

	NVSP_MSG4_MAX = NVSP_MSG4_TYPE_UPLINK_CONNECT_STATE_DEPRECATED,

	/* Version 5 messages */
	NVSP_MSG5_TYPE_OID_QUERY_EX,
	NVSP_MSG5_TYPE_OID_QUERY_EX_COMP,
	NVSP_MSG5_TYPE_SUBCHANNEL,
	NVSP_MSG5_TYPE_SEND_INDIRECTION_TABLE,

	NVSP_MSG5_MAX = NVSP_MSG5_TYPE_SEND_INDIRECTION_TABLE,
};

enum {
	NVSP_STAT_NONE = 0,
	NVSP_STAT_SUCCESS,
	NVSP_STAT_FAIL,
	NVSP_STAT_PROTOCOL_TOO_NEW,
	NVSP_STAT_PROTOCOL_TOO_OLD,
	NVSP_STAT_INVALID_RNDIS_PKT,
	NVSP_STAT_BUSY,
	NVSP_STAT_PROTOCOL_UNSUPPORTED,
	NVSP_STAT_MAX,
};

struct nvsp_message_header {
	u32 msg_type;
};

/* Init Messages */

/*
 * This message is used by the VSC to initialize the channel after the channels
 * has been opened. This message should never include anything other then
 * versioning (i.e. this message will be the same for ever).
 */
struct nvsp_message_init {
	u32 min_protocol_ver;
	u32 max_protocol_ver;
} __packed;

/*
 * This message is used by the VSP to complete the initialization of the
 * channel. This message should never include anything other then versioning
 * (i.e. this message will be the same for ever).
 */
struct nvsp_message_init_complete {
	u32 negotiated_protocol_ver;
	u32 max_mdl_chain_len;
	u32 status;
} __packed;

union nvsp_message_init_uber {
	struct nvsp_message_init init;
	struct nvsp_message_init_complete init_complete;
} __packed;

/* Version 1 Messages */

/*
 * This message is used by the VSC to send the NDIS version to the VSP. The VSP
 * can use this information when handling OIDs sent by the VSC.
 */
struct nvsp_1_message_send_ndis_version {
	u32 ndis_major_ver;
	u32 ndis_minor_ver;
} __packed;

/*
 * This message is used by the VSC to send a receive buffer to the VSP. The VSP
 * can then use the receive buffer to send data to the VSC.
 */
struct nvsp_1_message_send_receive_buffer {
	u32 gpadl_handle;
	u16 id;
} __packed;

struct nvsp_1_receive_buffer_section {
	u32 offset;
	u32 sub_alloc_size;
	u32 num_sub_allocs;
	u32 end_offset;
} __packed;

/*
 * This message is used by the VSP to acknowledge a receive buffer send by the
 * VSC. This message must be sent by the VSP before the VSP uses the receive
 * buffer.
 */
struct nvsp_1_message_send_receive_buffer_complete {
	u32 status;
	u32 num_sections;

	/*
	 * The receive buffer is split into two parts, a large suballocation
	 * section and a small suballocation section. These sections are then
	 * suballocated by a certain size.
	 */

	/*
	 * For example, the following break up of the receive buffer has 6
	 * large suballocations and 10 small suballocations.
	 */

	/*
	 * |            Large Section          |  |   Small Section   |
	 * ------------------------------------------------------------
	 * |     |     |     |     |     |     |  | | | | | | | | | | |
	 * |                                      |
	 *  LargeOffset                            SmallOffset
	 */

	struct nvsp_1_receive_buffer_section sections[1];
} __packed;

/*
 * This message is sent by the VSC to revoke the receive buffer.  After the VSP
 * completes this transaction, the vsp should never use the receive buffer
 * again.
 */
struct nvsp_1_message_revoke_receive_buffer {
	u16 id;
};

/*
 * This message is used by the VSC to send a send buffer to the VSP. The VSC
 * can then use the send buffer to send data to the VSP.
 */
struct nvsp_1_message_send_send_buffer {
	u32 gpadl_handle;
	u16 id;
} __packed;

/*
 * This message is used by the VSP to acknowledge a send buffer sent by the
 * VSC. This message must be sent by the VSP before the VSP uses the sent
 * buffer.
 */
struct nvsp_1_message_send_send_buffer_complete {
	u32 status;

	/*
	 * The VSC gets to choose the size of the send buffer and the VSP gets
	 * to choose the sections size of the buffer.  This was done to enable
	 * dynamic reconfigurations when the cost of GPA-direct buffers
	 * decreases.
	 */
	u32 section_size;
} __packed;

/*
 * This message is sent by the VSC to revoke the send buffer.  After the VSP
 * completes this transaction, the vsp should never use the send buffer again.
 */
struct nvsp_1_message_revoke_send_buffer {
	u16 id;
};

/*
 * This message is used by both the VSP and the VSC to send a RNDIS message to
 * the opposite channel endpoint.
 */
struct nvsp_1_message_send_rndis_packet {
	/*
	 * This field is specified by RNIDS. They assume there's two different
	 * channels of communication. However, the Network VSP only has one.
	 * Therefore, the channel travels with the RNDIS packet.
	 */
	u32 channel_type;

	/*
	 * This field is used to send part or all of the data through a send
	 * buffer. This values specifies an index into the send buffer. If the
	 * index is 0xFFFFFFFF, then the send buffer is not being used and all
	 * of the data was sent through other VMBus mechanisms.
	 */
	u32 send_buf_section_index;
	u32 send_buf_section_size;
} __packed;

/*
 * This message is used by both the VSP and the VSC to complete a RNDIS message
 * to the opposite channel endpoint. At this point, the initiator of this
 * message cannot use any resources associated with the original RNDIS packet.
 */
struct nvsp_1_message_send_rndis_packet_complete {
	u32 status;
};

union nvsp_1_message_uber {
	struct nvsp_1_message_send_ndis_version send_ndis_ver;

	struct nvsp_1_message_send_receive_buffer send_recv_buf;
	struct nvsp_1_message_send_receive_buffer_complete
						send_recv_buf_complete;
	struct nvsp_1_message_revoke_receive_buffer revoke_recv_buf;

	struct nvsp_1_message_send_send_buffer send_send_buf;
	struct nvsp_1_message_send_send_buffer_complete send_send_buf_complete;
	struct nvsp_1_message_revoke_send_buffer revoke_send_buf;

	struct nvsp_1_message_send_rndis_packet send_rndis_pkt;
	struct nvsp_1_message_send_rndis_packet_complete
						send_rndis_pkt_complete;
} __packed;


/*
 * Network VSP protocol version 2 messages:
 */
struct nvsp_2_vsc_capability {
	union {
		u64 data;
		struct {
			u64 vmq:1;
			u64 chimney:1;
			u64 sriov:1;
			u64 ieee8021q:1;
			u64 correlation_id:1;
		};
	};
} __packed;

struct nvsp_2_send_ndis_config {
	u32 mtu;
	u32 reserved;
	struct nvsp_2_vsc_capability capability;
} __packed;

/* Allocate receive buffer */
struct nvsp_2_alloc_rxbuf {
	/* Allocation ID to match the allocation request and response */
	u32 alloc_id;

	/* Length of the VM shared memory receive buffer that needs to
	 * be allocated
	 */
	u32 len;
} __packed;

/* Allocate receive buffer complete */
struct nvsp_2_alloc_rxbuf_comp {
	/* The NDIS_STATUS code for buffer allocation */
	u32 status;

	u32 alloc_id;

	/* GPADL handle for the allocated receive buffer */
	u32 gpadl_handle;

	/* Receive buffer ID */
	u64 recv_buf_id;
} __packed;

struct nvsp_2_free_rxbuf {
	u64 recv_buf_id;
} __packed;

union nvsp_2_message_uber {
	struct nvsp_2_send_ndis_config send_ndis_config;
	struct nvsp_2_alloc_rxbuf alloc_rxbuf;
	struct nvsp_2_alloc_rxbuf_comp alloc_rxbuf_comp;
	struct nvsp_2_free_rxbuf free_rxbuf;
} __packed;

struct nvsp_4_send_vf_association {
	/* 1: allocated, serial number is valid. 0: not allocated */
	u32 allocated;

	/* Serial number of the VF to team with */
	u32 serial;
} __packed;

enum nvsp_vm_datapath {
	NVSP_DATAPATH_SYNTHETIC = 0,
	NVSP_DATAPATH_VF,
	NVSP_DATAPATH_MAX
};

struct nvsp_4_sw_datapath {
	u32 active_datapath; /* active data path in VM */
} __packed;

union nvsp_4_message_uber {
	struct nvsp_4_send_vf_association vf_assoc;
	struct nvsp_4_sw_datapath active_dp;
} __packed;

enum nvsp_subchannel_operation {
	NVSP_SUBCHANNEL_NONE = 0,
	NVSP_SUBCHANNEL_ALLOCATE,
	NVSP_SUBCHANNEL_MAX
};

struct nvsp_5_subchannel_request {
	u32 op;
	u32 num_subchannels;
} __packed;

struct nvsp_5_subchannel_complete {
	u32 status;
	u32 num_subchannels; /* Actual number of subchannels allocated */
} __packed;

struct nvsp_5_send_indirect_table {
	/* The number of entries in the send indirection table */
	u32 count;

	/* The offset of the send indireciton table from top of this struct.
	 * The send indirection table tells which channel to put the send
	 * traffic on. Each entry is a channel number.
	 */
	u32 offset;
} __packed;

union nvsp_5_message_uber {
	struct nvsp_5_subchannel_request subchn_req;
	struct nvsp_5_subchannel_complete subchn_comp;
	struct nvsp_5_send_indirect_table send_table;
} __packed;

union nvsp_all_messages {
	union nvsp_message_init_uber init_msg;
	union nvsp_1_message_uber v1_msg;
	union nvsp_2_message_uber v2_msg;
	union nvsp_4_message_uber v4_msg;
	union nvsp_5_message_uber v5_msg;
} __packed;

/* ALL Messages */
struct nvsp_message {
	struct nvsp_message_header hdr;
	union nvsp_all_messages msg;
} __packed;


#define NETVSC_MTU 65536
#define NETVSC_MTU_MIN 68

#define NETVSC_RECEIVE_BUFFER_SIZE		(1024*1024*16)	/* 16MB */
#define NETVSC_RECEIVE_BUFFER_SIZE_LEGACY	(1024*1024*15)  /* 15MB */
#define NETVSC_SEND_BUFFER_SIZE			(1024 * 1024 * 15)   /* 15MB */
#define NETVSC_INVALID_INDEX			-1


#define NETVSC_RECEIVE_BUFFER_ID		0xcafe
#define NETVSC_SEND_BUFFER_ID			0

#define NETVSC_PACKET_SIZE                      4096

#define VRSS_SEND_TAB_SIZE 16

#define RNDIS_MAX_PKT_DEFAULT 8
#define RNDIS_PKT_ALIGN_DEFAULT 8

struct multi_send_data {
	spinlock_t lock; /* protect struct multi_send_data */
	struct hv_netvsc_packet *pkt; /* netvsc pkt pending */
	u32 count; /* counter of batched packets */
};

struct netvsc_stats {
	u64 packets;
	u64 bytes;
	struct u64_stats_sync syncp;
};

struct netvsc_reconfig {
	struct list_head list;
	u32 event;
};

/* The context of the netvsc device  */
struct net_device_context {
	/* point back to our device context */
	struct hv_device *device_ctx;
	/* reconfigure work */
	struct delayed_work dwork;
	/* last reconfig time */
	unsigned long last_reconfig;
	/* reconfig events */
	struct list_head reconfig_events;
	/* list protection */
	spinlock_t lock;

	struct work_struct work;
	u32 msg_enable; /* debug level */

	struct netvsc_stats __percpu *tx_stats;
	struct netvsc_stats __percpu *rx_stats;
};

/* Per netvsc device */
struct netvsc_device {
	struct hv_device *dev;

	u32 nvsp_version;

	atomic_t num_outstanding_sends;
	wait_queue_head_t wait_drain;
	bool start_remove;
	bool destroy;

	/* Receive buffer allocated by us but manages by NetVSP */
	void *recv_buf;
	u32 recv_buf_size;
	u32 recv_buf_gpadl_handle;
	u32 recv_section_cnt;
	struct nvsp_1_receive_buffer_section *recv_section;

	/* Send buffer allocated by us */
	void *send_buf;
	u32 send_buf_size;
	u32 send_buf_gpadl_handle;
	u32 send_section_cnt;
	u32 send_section_size;
	unsigned long *send_section_map;
	int map_words;

	/* Used for NetVSP initialization protocol */
	struct completion channel_init_wait;
	struct nvsp_message channel_init_pkt;

	struct nvsp_message revoke_packet;
	/* unsigned char HwMacAddr[HW_MACADDR_LEN]; */

	struct net_device *ndev;

	struct vmbus_channel *chn_table[NR_CPUS];
	u32 send_table[VRSS_SEND_TAB_SIZE];
	u32 max_chn;
	u32 num_chn;
	spinlock_t sc_lock; /* Protects num_sc_offered variable */
	u32 num_sc_offered;
	atomic_t queue_sends[NR_CPUS];

	/* Holds rndis device info */
	void *extension;

	int ring_size;

	/* The primary channel callback buffer */
	unsigned char *cb_buffer;
	/* The sub channel callback buffer */
	unsigned char *sub_cb_buf;

	struct multi_send_data msd[NR_CPUS];
	u32 max_pkt; /* max number of pkt in one send, e.g. 8 */
	u32 pkt_align; /* alignment bytes, e.g. 8 */

	/* The net device context */
	struct net_device_context *nd_ctx;

	/* 1: allocated, serial number is valid. 0: not allocated */
	u32 vf_alloc;
	/* Serial number of the VF to team with */
	u32 vf_serial;
};

/* NdisInitialize message */
struct rndis_initialize_request {
	u32 req_id;
	u32 major_ver;
	u32 minor_ver;
	u32 max_xfer_size;
};

/* Response to NdisInitialize */
struct rndis_initialize_complete {
	u32 req_id;
	u32 status;
	u32 major_ver;
	u32 minor_ver;
	u32 dev_flags;
	u32 medium;
	u32 max_pkt_per_msg;
	u32 max_xfer_size;
	u32 pkt_alignment_factor;
	u32 af_list_offset;
	u32 af_list_size;
};

/* Call manager devices only: Information about an address family */
/* supported by the device is appended to the response to NdisInitialize. */
struct rndis_co_address_family {
	u32 address_family;
	u32 major_ver;
	u32 minor_ver;
};

/* NdisHalt message */
struct rndis_halt_request {
	u32 req_id;
};

/* NdisQueryRequest message */
struct rndis_query_request {
	u32 req_id;
	u32 oid;
	u32 info_buflen;
	u32 info_buf_offset;
	u32 dev_vc_handle;
};

/* Response to NdisQueryRequest */
struct rndis_query_complete {
	u32 req_id;
	u32 status;
	u32 info_buflen;
	u32 info_buf_offset;
};

/* NdisSetRequest message */
struct rndis_set_request {
	u32 req_id;
	u32 oid;
	u32 info_buflen;
	u32 info_buf_offset;
	u32 dev_vc_handle;
};

/* Response to NdisSetRequest */
struct rndis_set_complete {
	u32 req_id;
	u32 status;
};

/* NdisReset message */
struct rndis_reset_request {
	u32 reserved;
};

/* Response to NdisReset */
struct rndis_reset_complete {
	u32 status;
	u32 addressing_reset;
};

/* NdisMIndicateStatus message */
struct rndis_indicate_status {
	u32 status;
	u32 status_buflen;
	u32 status_buf_offset;
};

/* Diagnostic information passed as the status buffer in */
/* struct rndis_indicate_status messages signifying error conditions. */
struct rndis_diagnostic_info {
	u32 diag_status;
	u32 error_offset;
};

/* NdisKeepAlive message */
struct rndis_keepalive_request {
	u32 req_id;
};

/* Response to NdisKeepAlive */
struct rndis_keepalive_complete {
	u32 req_id;
	u32 status;
};

/*
 * Data message. All Offset fields contain byte offsets from the beginning of
 * struct rndis_packet. All Length fields are in bytes.  VcHandle is set
 * to 0 for connectionless data, otherwise it contains the VC handle.
 */
struct rndis_packet {
	u32 data_offset;
	u32 data_len;
	u32 oob_data_offset;
	u32 oob_data_len;
	u32 num_oob_data_elements;
	u32 per_pkt_info_offset;
	u32 per_pkt_info_len;
	u32 vc_handle;
	u32 reserved;
};

/* Optional Out of Band data associated with a Data message. */
struct rndis_oobd {
	u32 size;
	u32 type;
	u32 class_info_offset;
};

/* Packet extension field contents associated with a Data message. */
struct rndis_per_packet_info {
	u32 size;
	u32 type;
	u32 ppi_offset;
};

enum ndis_per_pkt_info_type {
	TCPIP_CHKSUM_PKTINFO,
	IPSEC_PKTINFO,
	TCP_LARGESEND_PKTINFO,
	CLASSIFICATION_HANDLE_PKTINFO,
	NDIS_RESERVED,
	SG_LIST_PKTINFO,
	IEEE_8021Q_INFO,
	ORIGINAL_PKTINFO,
	PACKET_CANCEL_ID,
	NBL_HASH_VALUE = PACKET_CANCEL_ID,
	ORIGINAL_NET_BUFLIST,
	CACHED_NET_BUFLIST,
	SHORT_PKT_PADINFO,
	MAX_PER_PKT_INFO
};

struct ndis_pkt_8021q_info {
	union {
		struct {
			u32 pri:3; /* User Priority */
			u32 cfi:1; /* Canonical Format ID */
			u32 vlanid:12; /* VLAN ID */
			u32 reserved:16;
		};
		u32 value;
	};
};

struct ndis_oject_header {
	u8 type;
	u8 revision;
	u16 size;
};

#define NDIS_OBJECT_TYPE_DEFAULT	0x80
#define NDIS_OFFLOAD_PARAMETERS_REVISION_3 3
#define NDIS_OFFLOAD_PARAMETERS_NO_CHANGE 0
#define NDIS_OFFLOAD_PARAMETERS_LSOV2_DISABLED 1
#define NDIS_OFFLOAD_PARAMETERS_LSOV2_ENABLED  2
#define NDIS_OFFLOAD_PARAMETERS_LSOV1_ENABLED  2
#define NDIS_OFFLOAD_PARAMETERS_RSC_DISABLED 1
#define NDIS_OFFLOAD_PARAMETERS_RSC_ENABLED 2
#define NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED 1
#define NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED 2
#define NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED 3
#define NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED 4

#define NDIS_TCP_LARGE_SEND_OFFLOAD_V2_TYPE	1
#define NDIS_TCP_LARGE_SEND_OFFLOAD_IPV4	0
#define NDIS_TCP_LARGE_SEND_OFFLOAD_IPV6	1

#define VERSION_4_OFFLOAD_SIZE			22
/*
 * New offload OIDs for NDIS 6
 */
#define OID_TCP_OFFLOAD_CURRENT_CONFIG 0xFC01020B /* query only */
#define OID_TCP_OFFLOAD_PARAMETERS 0xFC01020C		/* set only */
#define OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES 0xFC01020D/* query only */
#define OID_TCP_CONNECTION_OFFLOAD_CURRENT_CONFIG 0xFC01020E /* query only */
#define OID_TCP_CONNECTION_OFFLOAD_HARDWARE_CAPABILITIES 0xFC01020F /* query */
#define OID_OFFLOAD_ENCAPSULATION 0x0101010A /* set/query */

struct ndis_offload_params {
	struct ndis_oject_header header;
	u8 ip_v4_csum;
	u8 tcp_ip_v4_csum;
	u8 udp_ip_v4_csum;
	u8 tcp_ip_v6_csum;
	u8 udp_ip_v6_csum;
	u8 lso_v1;
	u8 ip_sec_v1;
	u8 lso_v2_ipv4;
	u8 lso_v2_ipv6;
	u8 tcp_connection_ip_v4;
	u8 tcp_connection_ip_v6;
	u32 flags;
	u8 ip_sec_v2;
	u8 ip_sec_v2_ip_v4;
	struct {
		u8 rsc_ip_v4;
		u8 rsc_ip_v6;
	};
	struct {
		u8 encapsulated_packet_task_offload;
		u8 encapsulation_types;
	};
};

struct ndis_tcp_ip_checksum_info {
	union {
		struct {
			u32 is_ipv4:1;
			u32 is_ipv6:1;
			u32 tcp_checksum:1;
			u32 udp_checksum:1;
			u32 ip_header_checksum:1;
			u32 reserved:11;
			u32 tcp_header_offset:10;
		} transmit;
		struct {
			u32 tcp_checksum_failed:1;
			u32 udp_checksum_failed:1;
			u32 ip_checksum_failed:1;
			u32 tcp_checksum_succeeded:1;
			u32 udp_checksum_succeeded:1;
			u32 ip_checksum_succeeded:1;
			u32 loopback:1;
			u32 tcp_checksum_value_invalid:1;
			u32 ip_checksum_value_invalid:1;
		} receive;
		u32  value;
	};
};

struct ndis_tcp_lso_info {
	union {
		struct {
			u32 unused:30;
			u32 type:1;
			u32 reserved2:1;
		} transmit;
		struct {
			u32 mss:20;
			u32 tcp_header_offset:10;
			u32 type:1;
			u32 reserved2:1;
		} lso_v1_transmit;
		struct {
			u32 tcp_payload:30;
			u32 type:1;
			u32 reserved2:1;
		} lso_v1_transmit_complete;
		struct {
			u32 mss:20;
			u32 tcp_header_offset:10;
			u32 type:1;
			u32 ip_version:1;
		} lso_v2_transmit;
		struct {
			u32 reserved:30;
			u32 type:1;
			u32 reserved2:1;
		} lso_v2_transmit_complete;
		u32  value;
	};
};

#define NDIS_VLAN_PPI_SIZE (sizeof(struct rndis_per_packet_info) + \
		sizeof(struct ndis_pkt_8021q_info))

#define NDIS_CSUM_PPI_SIZE (sizeof(struct rndis_per_packet_info) + \
		sizeof(struct ndis_tcp_ip_checksum_info))

#define NDIS_LSO_PPI_SIZE (sizeof(struct rndis_per_packet_info) + \
		sizeof(struct ndis_tcp_lso_info))

#define NDIS_HASH_PPI_SIZE (sizeof(struct rndis_per_packet_info) + \
		sizeof(u32))

/* Total size of all PPI data */
#define NDIS_ALL_PPI_SIZE (NDIS_VLAN_PPI_SIZE + NDIS_CSUM_PPI_SIZE + \
			   NDIS_LSO_PPI_SIZE + NDIS_HASH_PPI_SIZE)

/* Format of Information buffer passed in a SetRequest for the OID */
/* OID_GEN_RNDIS_CONFIG_PARAMETER. */
struct rndis_config_parameter_info {
	u32 parameter_name_offset;
	u32 parameter_name_length;
	u32 parameter_type;
	u32 parameter_value_offset;
	u32 parameter_value_length;
};

/* Values for ParameterType in struct rndis_config_parameter_info */
#define RNDIS_CONFIG_PARAM_TYPE_INTEGER     0
#define RNDIS_CONFIG_PARAM_TYPE_STRING      2

/* CONDIS Miniport messages for connection oriented devices */
/* that do not implement a call manager. */

/* CoNdisMiniportCreateVc message */
struct rcondis_mp_create_vc {
	u32 req_id;
	u32 ndis_vc_handle;
};

/* Response to CoNdisMiniportCreateVc */
struct rcondis_mp_create_vc_complete {
	u32 req_id;
	u32 dev_vc_handle;
	u32 status;
};

/* CoNdisMiniportDeleteVc message */
struct rcondis_mp_delete_vc {
	u32 req_id;
	u32 dev_vc_handle;
};

/* Response to CoNdisMiniportDeleteVc */
struct rcondis_mp_delete_vc_complete {
	u32 req_id;
	u32 status;
};

/* CoNdisMiniportQueryRequest message */
struct rcondis_mp_query_request {
	u32 req_id;
	u32 request_type;
	u32 oid;
	u32 dev_vc_handle;
	u32 info_buflen;
	u32 info_buf_offset;
};

/* CoNdisMiniportSetRequest message */
struct rcondis_mp_set_request {
	u32 req_id;
	u32 request_type;
	u32 oid;
	u32 dev_vc_handle;
	u32 info_buflen;
	u32 info_buf_offset;
};

/* CoNdisIndicateStatus message */
struct rcondis_indicate_status {
	u32 ndis_vc_handle;
	u32 status;
	u32 status_buflen;
	u32 status_buf_offset;
};

/* CONDIS Call/VC parameters */
struct rcondis_specific_parameters {
	u32 parameter_type;
	u32 parameter_length;
	u32 parameter_lffset;
};

struct rcondis_media_parameters {
	u32 flags;
	u32 reserved1;
	u32 reserved2;
	struct rcondis_specific_parameters media_specific;
};

struct rndis_flowspec {
	u32 token_rate;
	u32 token_bucket_size;
	u32 peak_bandwidth;
	u32 latency;
	u32 delay_variation;
	u32 service_type;
	u32 max_sdu_size;
	u32 minimum_policed_size;
};

struct rcondis_call_manager_parameters {
	struct rndis_flowspec transmit;
	struct rndis_flowspec receive;
	struct rcondis_specific_parameters call_mgr_specific;
};

/* CoNdisMiniportActivateVc message */
struct rcondis_mp_activate_vc_request {
	u32 req_id;
	u32 flags;
	u32 dev_vc_handle;
	u32 media_params_offset;
	u32 media_params_length;
	u32 call_mgr_params_offset;
	u32 call_mgr_params_length;
};

/* Response to CoNdisMiniportActivateVc */
struct rcondis_mp_activate_vc_complete {
	u32 req_id;
	u32 status;
};

/* CoNdisMiniportDeactivateVc message */
struct rcondis_mp_deactivate_vc_request {
	u32 req_id;
	u32 flags;
	u32 dev_vc_handle;
};

/* Response to CoNdisMiniportDeactivateVc */
struct rcondis_mp_deactivate_vc_complete {
	u32 req_id;
	u32 status;
};


/* union with all of the RNDIS messages */
union rndis_message_container {
	struct rndis_packet pkt;
	struct rndis_initialize_request init_req;
	struct rndis_halt_request halt_req;
	struct rndis_query_request query_req;
	struct rndis_set_request set_req;
	struct rndis_reset_request reset_req;
	struct rndis_keepalive_request keep_alive_req;
	struct rndis_indicate_status indicate_status;
	struct rndis_initialize_complete init_complete;
	struct rndis_query_complete query_complete;
	struct rndis_set_complete set_complete;
	struct rndis_reset_complete reset_complete;
	struct rndis_keepalive_complete keep_alive_complete;
	struct rcondis_mp_create_vc co_miniport_create_vc;
	struct rcondis_mp_delete_vc co_miniport_delete_vc;
	struct rcondis_indicate_status co_indicate_status;
	struct rcondis_mp_activate_vc_request co_miniport_activate_vc;
	struct rcondis_mp_deactivate_vc_request co_miniport_deactivate_vc;
	struct rcondis_mp_create_vc_complete co_miniport_create_vc_complete;
	struct rcondis_mp_delete_vc_complete co_miniport_delete_vc_complete;
	struct rcondis_mp_activate_vc_complete co_miniport_activate_vc_complete;
	struct rcondis_mp_deactivate_vc_complete
		co_miniport_deactivate_vc_complete;
};

/* Remote NDIS message format */
struct rndis_message {
	u32 ndis_msg_type;

	/* Total length of this message, from the beginning */
	/* of the sruct rndis_message, in bytes. */
	u32 msg_len;

	/* Actual message */
	union rndis_message_container msg;
};


/* Handy macros */

/* get the size of an RNDIS message. Pass in the message type, */
/* struct rndis_set_request, struct rndis_packet for example */
#define RNDIS_MESSAGE_SIZE(msg)				\
	(sizeof(msg) + (sizeof(struct rndis_message) -	\
	 sizeof(union rndis_message_container)))

/* get pointer to info buffer with message pointer */
#define MESSAGE_TO_INFO_BUFFER(msg)				\
	(((unsigned char *)(msg)) + msg->info_buf_offset)

/* get pointer to status buffer with message pointer */
#define MESSAGE_TO_STATUS_BUFFER(msg)			\
	(((unsigned char *)(msg)) + msg->status_buf_offset)

/* get pointer to OOBD buffer with message pointer */
#define MESSAGE_TO_OOBD_BUFFER(msg)				\
	(((unsigned char *)(msg)) + msg->oob_data_offset)

/* get pointer to data buffer with message pointer */
#define MESSAGE_TO_DATA_BUFFER(msg)				\
	(((unsigned char *)(msg)) + msg->per_pkt_info_offset)

/* get pointer to contained message from NDIS_MESSAGE pointer */
#define RNDIS_MESSAGE_PTR_TO_MESSAGE_PTR(rndis_msg)		\
	((void *) &rndis_msg->msg)

/* get pointer to contained message from NDIS_MESSAGE pointer */
#define RNDIS_MESSAGE_RAW_PTR_TO_MESSAGE_PTR(rndis_msg)	\
	((void *) rndis_msg)


#define __struct_bcount(x)



#define RNDIS_HEADER_SIZE	(sizeof(struct rndis_message) - \
				 sizeof(union rndis_message_container))

#define RNDIS_AND_PPI_SIZE (sizeof(struct rndis_message) + NDIS_ALL_PPI_SIZE)

#define NDIS_PACKET_TYPE_DIRECTED	0x00000001
#define NDIS_PACKET_TYPE_MULTICAST	0x00000002
#define NDIS_PACKET_TYPE_ALL_MULTICAST	0x00000004
#define NDIS_PACKET_TYPE_BROADCAST	0x00000008
#define NDIS_PACKET_TYPE_SOURCE_ROUTING	0x00000010
#define NDIS_PACKET_TYPE_PROMISCUOUS	0x00000020
#define NDIS_PACKET_TYPE_SMT		0x00000040
#define NDIS_PACKET_TYPE_ALL_LOCAL	0x00000080
#define NDIS_PACKET_TYPE_GROUP		0x00000100
#define NDIS_PACKET_TYPE_ALL_FUNCTIONAL	0x00000200
#define NDIS_PACKET_TYPE_FUNCTIONAL	0x00000400
#define NDIS_PACKET_TYPE_MAC_FRAME	0x00000800

#define INFO_IPV4       2
#define INFO_IPV6       4
#define INFO_TCP        2
#define INFO_UDP        4

#define TRANSPORT_INFO_NOT_IP   0
#define TRANSPORT_INFO_IPV4_TCP ((INFO_IPV4 << 16) | INFO_TCP)
#define TRANSPORT_INFO_IPV4_UDP ((INFO_IPV4 << 16) | INFO_UDP)
#define TRANSPORT_INFO_IPV6_TCP ((INFO_IPV6 << 16) | INFO_TCP)
#define TRANSPORT_INFO_IPV6_UDP ((INFO_IPV6 << 16) | INFO_UDP)


#endif /* _HYPERV_NET_H */
