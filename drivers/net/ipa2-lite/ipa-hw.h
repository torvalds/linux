/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 */

#define IPA_BAM_BASE			0x4000
#define IPA_BASE			0x40000
#define IPA_SRAM_BASE			0x45000

#define BAM_REG(off)			((off) + IPA_BAM_BASE)
#define BAM_P_REG(off, pipe)		(BAM_REG(off) + (pipe) * 0x1000)

#define IPA_REG(off)			((off) + IPA_BASE)
#define IPA_EP_REG(off, pipe)		(IPA_REG(off) + (pipe) * 4)

#define REG_BAM_CTRL			BAM_REG(0x0000)
#define BAM_SW_RST			BIT(0)
#define BAM_EN				BIT(1)
#define REG_BAM_REVISION		BAM_REG(0x1000)
#define NUM_EES_SHIFT			8
#define NUM_EES_MASK			0xF
#define REG_BAM_NUM_PIPES		BAM_REG(0x1008)
#define BAM_NUM_PIPES_SHIFT		0
#define BAM_NUM_PIPES_MASK		0xFF

#define REG_BAM_DESC_CNT_TRSHLD		BAM_REG(0x0008)
#define CNT_TRSHLD			0xffff

#define REG_BAM_IRQ_SRCS		BAM_REG(0x3010)
#define BAM_IRQ				BIT(31)

#define REG_BAM_IRQ_SRCS_MSK		BAM_REG(0x3014)
#define REG_BAM_IRQ_SRCS_UNMASKED	BAM_REG(0x3018)
#define REG_BAM_IRQ_STTS		BAM_REG(0x0014)
#define REG_BAM_IRQ_CLR			BAM_REG(0x0018)
#define REG_BAM_IRQ_EN			BAM_REG(0x001C)
#define BAM_TIMER_EN			BIT(4)
#define BAM_EMPTY_EN			BIT(3)
#define BAM_ERROR_EN			BIT(2)
#define BAM_HRESP_ERR_EN		BIT(1)

#define REG_BAM_CNFG_BITS		BAM_REG(0x007C)
#define REG_BAM_IRQ_SRCS_EE0		BAM_REG(0x3000)
#define REG_BAM_IRQ_SRCS_MSK_EE0	BAM_REG(0x3004)
#define REG_BAM_P_CTRL(pipe)		BAM_P_REG(0x13000, (pipe))
#define P_EN				BIT(1)
#define P_DIRECTION			BIT(3)
#define P_SYS_MODE			BIT(5)

#define REG_BAM_P_RST(pipe)		BAM_P_REG(0x13004, (pipe))
#define REG_BAM_P_HALT(pipe)		BAM_P_REG(0x13008, (pipe))
#define REG_BAM_P_IRQ_STTS(pipe)	BAM_P_REG(0x13010, (pipe))
#define REG_BAM_P_IRQ_CLR(pipe)		BAM_P_REG(0x13014, (pipe))
#define REG_BAM_P_IRQ_EN(pipe)		BAM_P_REG(0x13018, (pipe))
#define P_PRCSD_DESC_EN			BIT(0)
#define P_TIMER_EN			BIT(1)
#define P_WAKE_EN			BIT(2)
#define P_OUT_OF_DESC_EN		BIT(3)
#define P_ERR_EN			BIT(4)
#define P_TRNSFR_END_EN			BIT(5)

#define REG_BAM_P_EVNT_DEST_ADDR(pipe)	BAM_P_REG(0x1382C, (pipe))
#define REG_BAM_P_WR_OFF_REG(pipe)	BAM_P_REG(0x13818, (pipe))
#define REG_BAM_P_RD_OFF_REG(pipe)	BAM_P_REG(0x13800, (pipe))
#define REG_BAM_P_DATA_FIFO_ADDR(pipe)	BAM_P_REG(0x13824, (pipe))
#define REG_BAM_P_DESC_FIFO_ADDR(pipe)	BAM_P_REG(0x1381C, (pipe))
#define REG_BAM_P_EVNT_GEN_TRSHLD(pipe)	BAM_P_REG(0x13828, (pipe))
#define REG_BAM_P_FIFO_SIZES(pipe)	BAM_P_REG(0x13820, (pipe))

struct __packed fifo_desc {
	__le32 addr;
	union {
		__le16 size;
		__le16 opcode;
	};
	__le16 flags;
#define DESC_FLAG_IMMCMD		BIT(8)
#define DESC_FLAG_INT			BIT(15)
#define DESC_FLAG_EOT			BIT(14)
};

/* IPA registers */

/* IPA 2.0+ */
#define REG_IPA_IRQ_STTS_EE0				IPA_REG(0x1008)
#define REG_IPA_IRQ_EN_EE0				IPA_REG(0x100c)
#define REG_IPA_IRQ_CLR_EE0				IPA_REG(0x1010)
#define REG_IPA_IRQ_SUSPEND_INFO_EE0			IPA_REG(0x1098)

/* IPA 2.5+ */
#define REG_IPA_BCR_OFST				IPA_REG(0x05B0)
#define REG_IPA_COUNTER_CFG_OFST			IPA_REG(0x05E8)
#define IPA_COUNTER_CFG_AGGR_GRAN_BMSK			0x000001F0
#define IPA_COUNTER_CFG_EOT_COAL_GRAN_BMSK		0x0000000F

/* IPA 2.6/2.6L+*/
#define REG_IPA_ENABLED_PIPES_OFST			IPA_REG(0x05DC)
#define REG_IPA_YELLOW_MARKER_SYS_CFG_OFST		IPA_REG(0x0728)

/* IPA Common Registers */
#define REG_IPA_COMP_SW_RESET_OFST			IPA_REG(0x003c)
#define REG_IPA_VERSION_OFST				IPA_REG(0x0034)
#define REG_IPA_COMP_HW_VERSION_OFST			IPA_REG(0x0030)
#define REG_IPA_SHARED_MEM				IPA_REG(0x0050)
#define IPA_SHARED_MEM_BADDR_BMSK			0xffff0000
#define IPA_SHARED_MEM_SIZE_BMSK			0x0000ffff
#define REG_IPA_ROUTE_OFST				IPA_REG(0x0044)
#define IPA_ROUTE_DIS_BMSK				0x00000001
#define IPA_ROUTE_DEF_PIPE_BMSK				0x0000003e
#define IPA_ROUTE_DEF_HDR_TABLE_BMSK			0x00000040
#define IPA_ROUTE_DEF_HDR_OFST_BMSK			0x0001ff80
#define IPA_ROUTE_FRAG_DEF_PIPE_BMSK			0x003e0000
#define REG_IPA_FILTER_OFST				IPA_REG(0x0048)
#define REG_IPA_SRAM_SW_FIRST_v2_5			IPA_REG(0x5000)
#define REG_IPA_COMP_CFG_OFST				IPA_REG(0x0038)

#define REG_IPA_EP_ROUTE(ep)				IPA_EP_REG(0x0370,  (ep))
#define IPA_EP_ROUTE_TABLE_INDEX_BMSK			0x0000001f

#define REG_IPA_EP_AGGR(ep)				IPA_EP_REG(0x0320,  (ep))
#define IPA_EP_AGGR_FORCE_CLOSE_BMSK			0x00400000
#define IPA_EP_AGGR_SW_EOF_ACTIVE_BMSK			0x00200000
#define IPA_EP_AGGR_PKT_LIMIT_BMSK			0x001f8000
#define IPA_EP_AGGR_TIME_LIMIT_BMSK			0x00007c00
#define IPA_EP_AGGR_BYTE_LIMIT_BMSK			0x000003e0
#define IPA_EP_AGGR_TYPE_BMSK				0x0000001c
#define IPA_EP_AGGR_EN_BMSK				0x00000003
#define REG_IPA_EP_MODE(ep)				IPA_EP_REG(0x02c0,  (ep))
#define IPA_EP_MODE_DEST_PIPE_INDEX_BMSK_v2_0		0x000001f0
#define IPA_EP_MODE_MODE_BMSK				0x00000007
#define REG_IPA_EP_HDR(ep)				IPA_EP_REG(0x0170,  (ep))
#define IPA_EP_HDR_LEN_BMSK				0x0000003f
#define IPA_EP_HDR_METADATA_REG_VALID_BMSK_v2		0x10000000
#define IPA_EP_HDR_LEN_INC_DEAGG_HDR_BMSK_v2		0x08000000
#define IPA_EP_HDR_A5_MUX_BMSK				0x04000000
#define IPA_EP_HDR_OFST_PKT_SIZE_BMSK			0x03f00000
#define IPA_EP_HDR_OFST_PKT_SIZE_VALID_BMSK		0x00080000
#define IPA_EP_HDR_ADDITIONAL_CONST_LEN_BMSK		0x0007e000
#define IPA_EP_HDR_OFST_METADATA_BMSK			0x00001f80
#define IPA_EP_HDR_OFST_METADATA_VALID_BMSK		0x00000040
#define REG_IPA_EP_NAT(ep)				IPA_EP_REG(0x0120,  (ep))
#define IPA_EP_NAT_EN_BMSK				0x00000003
#define REG_IPA_EP_HDR_EXT(ep)				IPA_EP_REG(0x01c0,  (ep))
#define IPA_EP_HDR_EXT_ENDIANNESS_BMSK			0x00000001
#define IPA_EP_HDR_EXT_TOTAL_LEN_OR_PAD_VALID_BMSK	0x00000002
#define IPA_EP_HDR_EXT_TOTAL_LEN_OR_PAD_BMSK		0x00000004
#define IPA_EP_HDR_EXT_PAYLOAD_LEN_INC_PADDING_BMSK	0x00000008
#define IPA_EP_HDR_EXT_TOTAL_LEN_OR_PAD_OFFSET_BMSK	0x000003f0
#define IPA_EP_HDR_EXT_PAD_TO_ALIGNMENT_BMSK_v2_0	0x00001c00
#define IPA_EP_HDR_EXT_PAD_TO_ALIGNMENT_BMSK_v2_5	0x00003c00
#define REG_IPA_EP_CTRL(ep)				IPA_EP_REG(0x0070,  (ep))
#define IPA_EP_CTRL_SUSPEND_BMSK			0x00000001
#define IPA_EP_CTRL_DELAY_BMSK				0x00000002
#define REG_IPA_EP_HOL_BLOCK_EN(ep)			IPA_EP_REG(0x03c0,  (ep))
#define IPA_EP_HOL_BLOCK_EN_EN_BMSK			0x00000001
#define REG_IPA_EP_DEAGGR(ep)				IPA_EP_REG(0x0470,  (ep))
#define IPA_EP_DEAGGR_DEAGGR_HDR_LEN_BMSK		0x0000003F
#define IPA_EP_DEAGGR_PACKET_OFFSET_VALID_BMSK		0x00000040
#define IPA_EP_DEAGGR_PACKET_OFFSET_LOCATION_BMSK	0x00003F00
#define IPA_EP_DEAGGR_MAX_PACKET_LEN_BMSK		0xFFFF0000
#define REG_IPA_EP_HOL_BLOCK_TIMER(ep)			IPA_EP_REG(0x0420,  (ep))
#define IPA_EP_HOL_BLOCK_TIMER_TIMER_BMSK		0x000001ff
#define REG_IPA_EP_DBG_CNT_REG(ep)			IPA_EP_REG(0x0600,  (ep))
#define IPA_EP_DBG_CNT_REG_DBG_CNT_REG_BMSK		0xffffffff
#define REG_IPA_EP_DBG_CNT_CTRL(ep)			IPA_EP_REG(0x0640,  (ep))
#define IPA_EP_DBG_CNT_CTRL_RULE_INDEX_BMSK		0x1ff00000
#define IPA_EP_DBG_CNT_CTRL_SOURCE_PIPE_BMSK		0x0001f000
#define IPA_EP_DBG_CNT_CTRL_PRODUCT_BMSK		0x00000100
#define IPA_EP_DBG_CNT_CTRL_TYPE_BMSK			0x00000070
#define IPA_EP_DBG_CNT_CTRL_EN_BMSK			0x00000001
#define REG_IPA_EP_STATUS(ep)				IPA_EP_REG(0x04c0,  (ep))
#define IPA_EP_STATUS_EP_BMSK				0x0000003e
#define IPA_EP_STATUS_EN_BMSK				0x00000001
#define REG_IPA_EP_CFG(ep)				IPA_EP_REG(0x00c0,  (ep))
#define IPA_EP_CFG_CS_METADATA_HDR_OFFSET_BMSK		0x00000078
#define IPA_EP_CFG_CS_OFFLOAD_EN_BMSK			0x00000006
#define IPA_EP_CFG_FRAG_OFFLOAD_EN_BMSK			0x00000001
#define REG_IPA_EP_HDR_METADATA_MASK(ep)		IPA_EP_REG(0x0220,  (ep))
#define REG_IPA_EP_HDR_METADATA(ep)			IPA_EP_REG(0x0270,  (ep))
#define IPA_EP_HDR_METADATA_MUX_ID_BMSK			0x00FF0000
#define REG_IPA_IRQ_UC_EE0				IPA_REG(0x101c)
#define IPA_IRQ_UC_INT_BMSK				0x00000001
#define REG_IPA_SYS_PKT_PROC_CNTXT_BASE_OFST		IPA_REG(0x05d8)
#define REG_IPA_LOCAL_PKT_PROC_CNTXT_BASE_OFST		IPA_REG(0x05e0)

#define REG_IPA_UC_CMD					(IPA_SRAM_BASE + 0x00)
#define IPA_UC_CMD_OP_MASK				0x000000ff
#define REG_IPA_UC_CMD_PARAM				(IPA_SRAM_BASE + 0x04)

#define REG_IPA_UC_RESP					(IPA_SRAM_BASE + 0x08)
#define IPA_UC_RESP_OP_MASK				0x000000ff
#define REG_IPA_UC_RESP_PARAM				(IPA_SRAM_BASE + 0x0c)
#define IPA_UC_RESP_OP_PARAM_OP_MASK			0x000000ff
#define IPA_UC_RESP_OP_PARAM_STATUS_MASK		0x0000ff00

#define REG_IPA_UC_EVENT				(IPA_SRAM_BASE + 0x10)
#define IPA_UC_CMD_EVENT_OP_MASK			0x000000ff
#define REG_IPA_UC_EVENT_PARAM				(IPA_SRAM_BASE + 0x14)

/* uC command op-codes*/
enum ipa_cpu_2_hw_commands {
	IPA_UC_CMD_NO_OP			= 0,
	IPA_UC_CMD_UPDATE_FLAGS			= 1,
	IPA_UC_CMD_DEBUG_RUN_TEST		= 2,
	IPA_UC_CMD_DEBUG_GET_INFO		= 3,
	IPA_UC_CMD_ERR_FATAL			= 4,
	IPA_UC_CMD_CLK_GATE			= 5,
	IPA_UC_CMD_CLK_UNGATE			= 6,
	IPA_UC_CMD_MEMCPY			= 7,
	IPA_UC_CMD_RESET_PIPE			= 8,
#define IPA_UC_CMD_RESET_PIPE_PARAM(pipe, is_rx) (((is_rx) << 8) | (pipe))

	IPA_UC_CMD_UPDATE_HOLB_MONITORING	= 9,
};

enum ipa_hw_2_cpu_responses {
	IPA_UC_RESPONSE_INIT_COMPLETED = 1,
	IPA_UC_RESPONSE_CMD_COMPLETED  = 2,
};

enum {
	IPA_IRQ_BAD_SNOC_ACCESS_IRQ		= 0,
	IPA_IRQ_EOT_COAL_IRQ			= 1,
	IPA_IRQ_UC_IRQ_0			= 2,
	IPA_IRQ_UC_IRQ_1			= 3,
	IPA_IRQ_UC_IRQ_2			= 4,
	IPA_IRQ_UC_IRQ_3			= 5,
	IPA_IRQ_UC_IN_Q_NOT_EMPTY_IRQ		= 6,
	IPA_IRQ_UC_RX_CMD_Q_NOT_FULL_IRQ	= 7,
	IPA_IRQ_UC_TX_CMD_Q_NOT_FULL_IRQ	= 8,
	IPA_IRQ_UC_TO_PROC_ACK_Q_NOT_FULL_IRQ	= 9,
	IPA_IRQ_PROC_TO_UC_ACK_Q_NOT_EMPTY_IRQ	= 10,
	IPA_IRQ_RX_ERR_IRQ			= 11,
	IPA_IRQ_DEAGGR_ERR_IRQ			= 12,
	IPA_IRQ_TX_ERR_IRQ			= 13,
	IPA_IRQ_STEP_MODE_IRQ			= 14,
	IPA_IRQ_PROC_ERR_IRQ			= 15,
	IPA_IRQ_TX_SUSPEND_IRQ			= 16,
	IPA_IRQ_TX_HOLB_DROP_IRQ		= 17,
	IPA_IRQ_BAM_IDLE_IRQ			= 18,
};

/* immediate command op-codes */
enum ipa_cmd_opcode {
	IPA_CMD_FT_V4_INIT			= 3,
	IPA_CMD_FT_V6_INIT			= 4,
	IPA_CMD_RT_V4_INIT			= 7,
	IPA_CMD_RT_V6_INIT			= 8,
	IPA_CMD_HDR_LOCAL_INIT			= 9,
	IPA_CMD_HDR_SYSTEM_INIT			= 10,
	IPA_CMD_WRITE_REG			= 12,
	IPA_CMD_PACKET_TAG			= 15,
	IPA_CMD_PACKET_INIT			= 16,
	IPA_CMD_DMA_SHARED_MEM			= 19,
	IPA_CMD_PACKET_TAG_STATUS		= 20,
};

/* Processing context TLV type */
#define IPA_PROC_CTX_TLV_TYPE_END 0
#define IPA_PROC_CTX_TLV_TYPE_HDR_ADD 1
#define IPA_PROC_CTX_TLV_TYPE_PROC_CMD 3

/**
 * struct ipa_flt_rule_hw_hdr - HW header of IPA filter rule
 * @word: filtering rule properties
 * @en_rule: enable rule
 * @action: post routing action
 * @rt_tbl_idx: index in routing table
 * @retain_hdr: added to add back to the packet the header removed
 *  as part of header removal. This will be done as part of
 *  header insertion block.
 * @to_uc: direct IPA to sent the packet to uc instead of
 *  the intended destination. This will be performed just after
 *  routing block processing, so routing will have determined
 *  destination end point and uc will receive this information
 *  together with the packet as part of the HW packet TX commands
 * @rsvd: reserved bits
 */
struct ipa_flt_rule_hw_hdr {
	union {
		u32 word;
		struct {
			u32 en_rule:16;
			u32 action:5;
			u32 rt_tbl_idx:5;
			u32 retain_hdr:1;
			u32 to_uc:1;
			u32 rsvd:4;
		} hdr;
	} u;
};

/**
 * struct ipa_rt_rule_hw_hdr - HW header of IPA routing rule
 * @word: filtering rule properties
 * @en_rule: enable rule
 * @pipe_dest_idx: destination pipe index
 * @system: changed from local to system due to HW change
 * @hdr_offset: header offset
 * @proc_ctx: whether hdr_offset points to header table or to
 *	header processing context table
 */
struct ipa_rt_rule_hw_hdr {
	union {
		u32 word;
		struct {
			u32 en_rule:16;
			u32 pipe_dest_idx:5;
			u32 system:1;
			u32 hdr_offset:10;
		} hdr;
		struct {
			u32 en_rule:16;
			u32 pipe_dest_idx:5;
			u32 system:1;
			u32 hdr_offset:9;
			u32 proc_ctx:1;
		} hdr_v2_5;
	} u;
};

/**
 * struct ipa_ip_v4_rule_init - command payload for IPA_IP_V4_FILTER_INIT
 * and IPA_IP_V4_ROUTING_INIT
 * @ipv4_rules_addr: address of ipv4 rules
 * @size_ipv4_rules: size of the above
 * @ipv4_addr: ipv4 address
 * @rsvd: reserved
 */
struct ipa_ip_v4_rule_init {
	u64 ipv4_rules_addr:32;
	u64 size_ipv4_rules:12;
	u64 ipv4_addr:16;
	u64 rsvd:4;
};

/**
 * struct ipa_ip_v6_rule_init - command payload for IPA_IP_V6_FILTER_INIT
 * and IPA_IP_V6_ROUTING_INIT
 * @ipv6_rules_addr: address of ipv6 rules
 * @size_ipv6_rules: size of the above
 * @ipv6_addr: ipv6 address
 */
struct ipa_ip_v6_rule_init {
	u64 ipv6_rules_addr:32;
	u64 size_ipv6_rules:16;
	u64 ipv6_addr:16;
};

/**
 * struct ipa_ip_v4_routing_init - IPA_IP_V4_ROUTING_INIT command payload
 * @ipv4_rules_addr: address of ipv4 rules
 * @size_ipv4_rules: size of the above
 * @ipv4_addr: ipv4 address
 * @rsvd: reserved
 */
struct ipa_ip_v4_routing_init {
	u64 ipv4_rules_addr:32;
	u64 size_ipv4_rules:12;
	u64 ipv4_addr:16;
	u64 rsvd:4;
};

/**
 * struct ipa_ip_v6_routing_init - IPA_IP_V6_ROUTING_INIT command payload
 * @ipv6_rules_addr: address of ipv6 rules
 * @size_ipv6_rules: size of the above
 * @ipv6_addr: ipv6 address
 */
struct ipa_ip_v6_routing_init {
	u64 ipv6_rules_addr:32;
	u64 size_ipv6_rules:16;
	u64 ipv6_addr:16;
};

/**
 * struct ipa_hdr_init_local - IPA_HDR_INIT_LOCAL command payload
 * @hdr_table_src_addr: word address of header table in system memory where the
 *  table starts (use as source for memory copying)
 * @size_hdr_table: size of the above (in bytes)
 * @hdr_table_dst_addr: header address in IPA sram (used as dst for memory copy)
 * @rsvd: reserved
 */
struct ipa_hdr_init_local {
	u64 hdr_table_src_addr:32;
	u64 size_hdr_table:12;
	u64 hdr_table_dst_addr:16;
	u64 rsvd:4;
};

/**
 * struct ipa_hdr_init_system - IPA_HDR_INIT_SYSTEM command payload
 * @hdr_table_addr: word address of header table in system memory where the
 *  table starts (use as source for memory copying)
 * @rsvd: reserved
 */
struct ipa_hdr_init_system {
	u64 hdr_table_addr:32;
	u64 rsvd:32;
};

/**
 * struct ipa_hdr_proc_ctx_tlv -
 * HW structure of IPA processing context header - TLV part
 * @type: 0 - end type
 *        1 - header addition type
 *        3 - processing command type
 * @length: number of bytes after tlv
 *        for type:
 *        0 - needs to be 0
 *        1 - header addition length
 *        3 - number of 32B including type and length.
 * @value: specific value for type
 *        for type:
 *        0 - needs to be 0
 *        1 - header length
 *        3 - command ID (see IPA_HDR_UCP_* definitions)
 */
struct ipa_hdr_proc_ctx_tlv {
	u32 type:8;
	u32 length:8;
	u32 value:16;
};

/**
 * struct ipa_hdr_proc_ctx_hdr_add -
 * HW structure of IPA processing context - add header tlv
 * @tlv: IPA processing context TLV
 * @hdr_addr: processing context header address
 */
struct ipa_hdr_proc_ctx_hdr_add {
	struct ipa_hdr_proc_ctx_tlv tlv;
	u32 hdr_addr;
};

#define IPA_A5_MUX_HDR_EXCP_FLAG_IP		BIT(7)
#define IPA_A5_MUX_HDR_EXCP_FLAG_NAT		BIT(6)
#define IPA_A5_MUX_HDR_EXCP_FLAG_SW_FLT		BIT(5)
#define IPA_A5_MUX_HDR_EXCP_FLAG_TAG		BIT(4)
#define IPA_A5_MUX_HDR_EXCP_FLAG_REPLICATED	BIT(3)
#define IPA_A5_MUX_HDR_EXCP_FLAG_IHL		BIT(2)

/**
 * struct ipa_a5_mux_hdr - A5 MUX header definition
 * @interface_id: interface ID
 * @src_pipe_index: source pipe index
 * @flags: flags
 * @metadata: metadata
 *
 * A5 MUX header is in BE, A5 runs in LE. This struct definition
 * allows A5 SW to correctly parse the header
 */
struct ipa_a5_mux_hdr {
	u16 interface_id;
	u8 src_pipe_index;
	u8 flags;
	u32 metadata;
};

/**
 * struct ipa_register_write - IPA_REGISTER_WRITE command payload
 * @rsvd: reserved
 * @skip_pipeline_clear: 0 to wait until IPA pipeline is clear
 * @offset: offset from IPA base address
 * @value: value to write to register
 * @value_mask: mask specifying which value bits to write to the register
 */
struct ipa_register_write {
	u32 rsvd:15;
	u32 skip_pipeline_clear:1;
	u32 offset:16;
	u32 value:32;
	u32 value_mask:32;
};

/**
 * struct ipa_nat_dma - IPA_NAT_DMA command payload
 * @table_index: NAT table index
 * @rsvd1: reserved
 * @base_addr: base address
 * @rsvd2: reserved
 * @offset: offset
 * @data: metadata
 * @rsvd3: reserved
 */
struct ipa_nat_dma {
	u64 table_index:3;
	u64 rsvd1:1;
	u64 base_addr:2;
	u64 rsvd2:2;
	u64 offset:32;
	u64 data:16;
	u64 rsvd3:8;
};

/**
 * struct ipa_nat_dma - IPA_IP_PACKET_INIT command payload
 * @destination_pipe_index: destination pipe index
 * @rsvd1: reserved
 * @metadata: metadata
 * @rsvd2: reserved
 */
struct ipa_ip_packet_init {
	u64 destination_pipe_index:5;
	u64 rsvd1:3;
	u64 metadata:32;
	u64 rsvd2:24;
};

/**
 * struct ipa_nat_dma - IPA_IP_V4_NAT_INIT command payload
 * @ipv4_rules_addr: ipv4 rules address
 * @ipv4_expansion_rules_addr: ipv4 expansion rules address
 * @index_table_addr: index tables address
 * @index_table_expansion_addr: index expansion table address
 * @table_index: index in table
 * @ipv4_rules_addr_type: ipv4 address type
 * @ipv4_expansion_rules_addr_type: ipv4 expansion address type
 * @index_table_addr_type: index table address type
 * @index_table_expansion_addr_type: index expansion table type
 * @size_base_tables: size of base tables
 * @size_expansion_tables: size of expansion tables
 * @rsvd2: reserved
 * @public_ip_addr: public IP address
 */
struct ipa_ip_v4_nat_init {
	u64 ipv4_rules_addr:32;
	u64 ipv4_expansion_rules_addr:32;
	u64 index_table_addr:32;
	u64 index_table_expansion_addr:32;
	u64 table_index:3;
	u64 rsvd1:1;
	u64 ipv4_rules_addr_type:1;
	u64 ipv4_expansion_rules_addr_type:1;
	u64 index_table_addr_type:1;
	u64 index_table_expansion_addr_type:1;
	u64 size_base_tables:12;
	u64 size_expansion_tables:10;
	u64 rsvd2:2;
	u64 public_ip_addr:32;
};

/**
 * struct ipa_ip_packet_tag - IPA_IP_PACKET_TAG command payload
 * @tag: tag value returned with response
 */
struct ipa_ip_packet_tag {
	u32 tag;
};

/**
 * struct ipa_ip_packet_tag_status - IPA_IP_PACKET_TAG_STATUS command payload
 * @rsvd: reserved
 * @tag_f_1: tag value returned within status
 * @tag_f_2: tag value returned within status
 */
struct ipa_ip_packet_tag_status {
	u32 rsvd:16;
	u32 tag_f_1:16;
	u32 tag_f_2:32;
};

/*! @brief Struct for the IPAv2.0 and IPAv2.5 UL packet status header */
struct ipa_hw_pkt_status {
	u32 status_opcode:8;
	u32 exception:8;
	u32 status_mask:16;
	u32 pkt_len:16;
	u32 endp_src_idx:5;
	u32 reserved_1:3;
	u32 endp_dest_idx:5;
	u32 reserved_2:3;
	u32 metadata:32;
	union {
		struct {
			u32 filt_local:1;
			u32 filt_global:1;
			u32 filt_pipe_idx:5;
			u32 filt_match:1;
			u32 filt_rule_idx:6;
			u32 ret_hdr:1;
			u32 reserved_3:1;
			u32 tag_f_1:16;

		} ipa_hw_v2_0_pkt_status;
		struct {
			u32 filt_local:1;
			u32 filt_global:1;
			u32 filt_pipe_idx:5;
			u32 ret_hdr:1;
			u32 filt_rule_idx:8;
			u32 tag_f_1:16;

		} ipa_hw_v2_5_pkt_status;
	};

	u32 tag_f_2:32;
	u32 time_day_ctr:32;
	u32 nat_hit:1;
	u32 nat_tbl_idx:13;
	u32 nat_type:2;
	u32 route_local:1;
	u32 route_tbl_idx:5;
	u32 route_match:1;
	u32 ucp:1;
	u32 route_rule_idx:8;
	u32 hdr_local:1;
	u32 hdr_offset:10;
	u32 frag_hit:1;
	u32 frag_rule:4;
	u32 reserved_4:16;
};

#define IPA_PKT_STATUS_SIZE 32

/*! @brief Status header opcodes */
enum ipa_hw_status_opcode {
	IPA_HW_STATUS_OPCODE_MIN,
	IPA_HW_STATUS_OPCODE_PACKET = IPA_HW_STATUS_OPCODE_MIN,
	IPA_HW_STATUS_OPCODE_NEW_FRAG_RULE,
	IPA_HW_STATUS_OPCODE_DROPPED_PACKET,
	IPA_HW_STATUS_OPCODE_SUSPENDED_PACKET,
	IPA_HW_STATUS_OPCODE_XLAT_PACKET = 6,
	IPA_HW_STATUS_OPCODE_MAX
};

/*! @brief Possible Masks received in status */
enum ipa_hw_pkt_status_mask {
	IPA_HW_PKT_STATUS_MASK_FRAG_PROCESS      = 0x1,
	IPA_HW_PKT_STATUS_MASK_FILT_PROCESS      = 0x2,
	IPA_HW_PKT_STATUS_MASK_NAT_PROCESS       = 0x4,
	IPA_HW_PKT_STATUS_MASK_ROUTE_PROCESS     = 0x8,
	IPA_HW_PKT_STATUS_MASK_TAG_VALID         = 0x10,
	IPA_HW_PKT_STATUS_MASK_FRAGMENT          = 0x20,
	IPA_HW_PKT_STATUS_MASK_FIRST_FRAGMENT    = 0x40,
	IPA_HW_PKT_STATUS_MASK_V4                = 0x80,
	IPA_HW_PKT_STATUS_MASK_CKSUM_PROCESS     = 0x100,
	IPA_HW_PKT_STATUS_MASK_AGGR_PROCESS      = 0x200,
	IPA_HW_PKT_STATUS_MASK_DEST_EOT          = 0x400,
	IPA_HW_PKT_STATUS_MASK_DEAGGR_PROCESS    = 0x800,
	IPA_HW_PKT_STATUS_MASK_DEAGG_FIRST       = 0x1000,
	IPA_HW_PKT_STATUS_MASK_SRC_EOT           = 0x2000
};

/*! @brief Possible Exceptions received in status */
enum ipa_hw_pkt_status_exception {
	IPA_HW_PKT_STATUS_EXCEPTION_NONE           = 0x0,
	IPA_HW_PKT_STATUS_EXCEPTION_DEAGGR         = 0x1,
	IPA_HW_PKT_STATUS_EXCEPTION_REPL           = 0x2,
	IPA_HW_PKT_STATUS_EXCEPTION_IPTYPE         = 0x4,
	IPA_HW_PKT_STATUS_EXCEPTION_IHL            = 0x8,
	IPA_HW_PKT_STATUS_EXCEPTION_FRAG_RULE_MISS = 0x10,
	IPA_HW_PKT_STATUS_EXCEPTION_SW_FILT        = 0x20,
	IPA_HW_PKT_STATUS_EXCEPTION_NAT            = 0x40,
	IPA_HW_PKT_STATUS_EXCEPTION_ACTUAL_MAX,
	IPA_HW_PKT_STATUS_EXCEPTION_MAX            = 0xFF
};

/*! @brief IPA_HW_IMM_CMD_DMA_SHARED_MEM Immediate Command Parameters */
struct ipa_hw_imm_cmd_dma_shared_mem {
	u32 reserved_1:16;
	u32 size:16;
	u32 system_addr:32;
	u32 local_addr:16;
	u32 direction:1;
	u32 skip_pipeline_clear:1;
	u32 reserved_2:14;
	u32 padding:32;
};
