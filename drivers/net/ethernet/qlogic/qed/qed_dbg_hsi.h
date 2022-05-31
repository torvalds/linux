/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2019-2021 Marvell International Ltd.
 */
#ifndef _QED_DBG_HSI_H
#define _QED_DBG_HSI_H

#include <linux/types.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>

/****************************************/
/* Debug Tools HSI constants and macros */
/****************************************/

enum block_id {
	BLOCK_GRC,
	BLOCK_MISCS,
	BLOCK_MISC,
	BLOCK_DBU,
	BLOCK_PGLUE_B,
	BLOCK_CNIG,
	BLOCK_CPMU,
	BLOCK_NCSI,
	BLOCK_OPTE,
	BLOCK_BMB,
	BLOCK_PCIE,
	BLOCK_MCP,
	BLOCK_MCP2,
	BLOCK_PSWHST,
	BLOCK_PSWHST2,
	BLOCK_PSWRD,
	BLOCK_PSWRD2,
	BLOCK_PSWWR,
	BLOCK_PSWWR2,
	BLOCK_PSWRQ,
	BLOCK_PSWRQ2,
	BLOCK_PGLCS,
	BLOCK_DMAE,
	BLOCK_PTU,
	BLOCK_TCM,
	BLOCK_MCM,
	BLOCK_UCM,
	BLOCK_XCM,
	BLOCK_YCM,
	BLOCK_PCM,
	BLOCK_QM,
	BLOCK_TM,
	BLOCK_DORQ,
	BLOCK_BRB,
	BLOCK_SRC,
	BLOCK_PRS,
	BLOCK_TSDM,
	BLOCK_MSDM,
	BLOCK_USDM,
	BLOCK_XSDM,
	BLOCK_YSDM,
	BLOCK_PSDM,
	BLOCK_TSEM,
	BLOCK_MSEM,
	BLOCK_USEM,
	BLOCK_XSEM,
	BLOCK_YSEM,
	BLOCK_PSEM,
	BLOCK_RSS,
	BLOCK_TMLD,
	BLOCK_MULD,
	BLOCK_YULD,
	BLOCK_XYLD,
	BLOCK_PRM,
	BLOCK_PBF_PB1,
	BLOCK_PBF_PB2,
	BLOCK_RPB,
	BLOCK_BTB,
	BLOCK_PBF,
	BLOCK_RDIF,
	BLOCK_TDIF,
	BLOCK_CDU,
	BLOCK_CCFC,
	BLOCK_TCFC,
	BLOCK_IGU,
	BLOCK_CAU,
	BLOCK_UMAC,
	BLOCK_XMAC,
	BLOCK_MSTAT,
	BLOCK_DBG,
	BLOCK_NIG,
	BLOCK_WOL,
	BLOCK_BMBN,
	BLOCK_IPC,
	BLOCK_NWM,
	BLOCK_NWS,
	BLOCK_MS,
	BLOCK_PHY_PCIE,
	BLOCK_LED,
	BLOCK_AVS_WRAP,
	BLOCK_PXPREQBUS,
	BLOCK_BAR0_MAP,
	BLOCK_MCP_FIO,
	BLOCK_LAST_INIT,
	BLOCK_PRS_FC,
	BLOCK_PBF_FC,
	BLOCK_NIG_LB_FC,
	BLOCK_NIG_LB_FC_PLLH,
	BLOCK_NIG_TX_FC_PLLH,
	BLOCK_NIG_TX_FC,
	BLOCK_NIG_RX_FC_PLLH,
	BLOCK_NIG_RX_FC,
	MAX_BLOCK_ID
};

/* binary debug buffer types */
enum bin_dbg_buffer_type {
	BIN_BUF_DBG_MODE_TREE,
	BIN_BUF_DBG_DUMP_REG,
	BIN_BUF_DBG_DUMP_MEM,
	BIN_BUF_DBG_IDLE_CHK_REGS,
	BIN_BUF_DBG_IDLE_CHK_IMMS,
	BIN_BUF_DBG_IDLE_CHK_RULES,
	BIN_BUF_DBG_IDLE_CHK_PARSING_DATA,
	BIN_BUF_DBG_ATTN_BLOCKS,
	BIN_BUF_DBG_ATTN_REGS,
	BIN_BUF_DBG_ATTN_INDEXES,
	BIN_BUF_DBG_ATTN_NAME_OFFSETS,
	BIN_BUF_DBG_BLOCKS,
	BIN_BUF_DBG_BLOCKS_CHIP_DATA,
	BIN_BUF_DBG_BUS_LINES,
	BIN_BUF_DBG_BLOCKS_USER_DATA,
	BIN_BUF_DBG_BLOCKS_CHIP_USER_DATA,
	BIN_BUF_DBG_BUS_LINE_NAME_OFFSETS,
	BIN_BUF_DBG_RESET_REGS,
	BIN_BUF_DBG_PARSING_STRINGS,
	MAX_BIN_DBG_BUFFER_TYPE
};

/* Attention bit mapping */
struct dbg_attn_bit_mapping {
	u16 data;
#define DBG_ATTN_BIT_MAPPING_VAL_MASK			0x7FFF
#define DBG_ATTN_BIT_MAPPING_VAL_SHIFT			0
#define DBG_ATTN_BIT_MAPPING_IS_UNUSED_BIT_CNT_MASK	0x1
#define DBG_ATTN_BIT_MAPPING_IS_UNUSED_BIT_CNT_SHIFT	15
};

/* Attention block per-type data */
struct dbg_attn_block_type_data {
	u16 names_offset;
	u16 reserved1;
	u8 num_regs;
	u8 reserved2;
	u16 regs_offset;

};

/* Block attentions */
struct dbg_attn_block {
	struct dbg_attn_block_type_data per_type_data[2];
};

/* Attention register result */
struct dbg_attn_reg_result {
	u32 data;
#define DBG_ATTN_REG_RESULT_STS_ADDRESS_MASK	0xFFFFFF
#define DBG_ATTN_REG_RESULT_STS_ADDRESS_SHIFT	0
#define DBG_ATTN_REG_RESULT_NUM_REG_ATTN_MASK	0xFF
#define DBG_ATTN_REG_RESULT_NUM_REG_ATTN_SHIFT	24
	u16 block_attn_offset;
	u16 reserved;
	u32 sts_val;
	u32 mask_val;
};

/* Attention block result */
struct dbg_attn_block_result {
	u8 block_id;
	u8 data;
#define DBG_ATTN_BLOCK_RESULT_ATTN_TYPE_MASK	0x3
#define DBG_ATTN_BLOCK_RESULT_ATTN_TYPE_SHIFT	0
#define DBG_ATTN_BLOCK_RESULT_NUM_REGS_MASK	0x3F
#define DBG_ATTN_BLOCK_RESULT_NUM_REGS_SHIFT	2
	u16 names_offset;
	struct dbg_attn_reg_result reg_results[15];
};

/* Mode header */
struct dbg_mode_hdr {
	u16 data;
#define DBG_MODE_HDR_EVAL_MODE_MASK		0x1
#define DBG_MODE_HDR_EVAL_MODE_SHIFT		0
#define DBG_MODE_HDR_MODES_BUF_OFFSET_MASK	0x7FFF
#define DBG_MODE_HDR_MODES_BUF_OFFSET_SHIFT	1
};

/* Attention register */
struct dbg_attn_reg {
	struct dbg_mode_hdr mode;
	u16 block_attn_offset;
	u32 data;
#define DBG_ATTN_REG_STS_ADDRESS_MASK	0xFFFFFF
#define DBG_ATTN_REG_STS_ADDRESS_SHIFT	0
#define DBG_ATTN_REG_NUM_REG_ATTN_MASK	0xFF
#define DBG_ATTN_REG_NUM_REG_ATTN_SHIFT 24
	u32 sts_clr_address;
	u32 mask_address;
};

/* Attention types */
enum dbg_attn_type {
	ATTN_TYPE_INTERRUPT,
	ATTN_TYPE_PARITY,
	MAX_DBG_ATTN_TYPE
};

/* Block debug data */
struct dbg_block {
	u8 name[15];
	u8 associated_storm_letter;
};

/* Chip-specific block debug data */
struct dbg_block_chip {
	u8 flags;
#define DBG_BLOCK_CHIP_IS_REMOVED_MASK		 0x1
#define DBG_BLOCK_CHIP_IS_REMOVED_SHIFT		 0
#define DBG_BLOCK_CHIP_HAS_RESET_REG_MASK	 0x1
#define DBG_BLOCK_CHIP_HAS_RESET_REG_SHIFT	 1
#define DBG_BLOCK_CHIP_UNRESET_BEFORE_DUMP_MASK  0x1
#define DBG_BLOCK_CHIP_UNRESET_BEFORE_DUMP_SHIFT 2
#define DBG_BLOCK_CHIP_HAS_DBG_BUS_MASK		 0x1
#define DBG_BLOCK_CHIP_HAS_DBG_BUS_SHIFT	 3
#define DBG_BLOCK_CHIP_HAS_LATENCY_EVENTS_MASK	 0x1
#define DBG_BLOCK_CHIP_HAS_LATENCY_EVENTS_SHIFT  4
#define DBG_BLOCK_CHIP_RESERVED0_MASK		 0x7
#define DBG_BLOCK_CHIP_RESERVED0_SHIFT		 5
	u8 dbg_client_id;
	u8 reset_reg_id;
	u8 reset_reg_bit_offset;
	struct dbg_mode_hdr dbg_bus_mode;
	u16 reserved1;
	u8 reserved2;
	u8 num_of_dbg_bus_lines;
	u16 dbg_bus_lines_offset;
	u32 dbg_select_reg_addr;
	u32 dbg_dword_enable_reg_addr;
	u32 dbg_shift_reg_addr;
	u32 dbg_force_valid_reg_addr;
	u32 dbg_force_frame_reg_addr;
};

/* Chip-specific block user debug data */
struct dbg_block_chip_user {
	u8 num_of_dbg_bus_lines;
	u8 has_latency_events;
	u16 names_offset;
};

/* Block user debug data */
struct dbg_block_user {
	u8 name[16];
};

/* Block Debug line data */
struct dbg_bus_line {
	u8 data;
#define DBG_BUS_LINE_NUM_OF_GROUPS_MASK		0xF
#define DBG_BUS_LINE_NUM_OF_GROUPS_SHIFT	0
#define DBG_BUS_LINE_IS_256B_MASK		0x1
#define DBG_BUS_LINE_IS_256B_SHIFT		4
#define DBG_BUS_LINE_RESERVED_MASK		0x7
#define DBG_BUS_LINE_RESERVED_SHIFT		5
	u8 group_sizes;
};

/* Condition header for registers dump */
struct dbg_dump_cond_hdr {
	struct dbg_mode_hdr mode; /* Mode header */
	u8 block_id; /* block ID */
	u8 data_size; /* size in dwords of the data following this header */
};

/* Memory data for registers dump */
struct dbg_dump_mem {
	u32 dword0;
#define DBG_DUMP_MEM_ADDRESS_MASK	0xFFFFFF
#define DBG_DUMP_MEM_ADDRESS_SHIFT	0
#define DBG_DUMP_MEM_MEM_GROUP_ID_MASK	0xFF
#define DBG_DUMP_MEM_MEM_GROUP_ID_SHIFT	24
	u32 dword1;
#define DBG_DUMP_MEM_LENGTH_MASK	0xFFFFFF
#define DBG_DUMP_MEM_LENGTH_SHIFT	0
#define DBG_DUMP_MEM_WIDE_BUS_MASK	0x1
#define DBG_DUMP_MEM_WIDE_BUS_SHIFT	24
#define DBG_DUMP_MEM_RESERVED_MASK	0x7F
#define DBG_DUMP_MEM_RESERVED_SHIFT	25
};

/* Register data for registers dump */
struct dbg_dump_reg {
	u32 data;
#define DBG_DUMP_REG_ADDRESS_MASK	0x7FFFFF
#define DBG_DUMP_REG_ADDRESS_SHIFT	0
#define DBG_DUMP_REG_WIDE_BUS_MASK	0x1
#define DBG_DUMP_REG_WIDE_BUS_SHIFT	23
#define DBG_DUMP_REG_LENGTH_MASK	0xFF
#define DBG_DUMP_REG_LENGTH_SHIFT	24
};

/* Split header for registers dump */
struct dbg_dump_split_hdr {
	u32 hdr;
#define DBG_DUMP_SPLIT_HDR_DATA_SIZE_MASK	0xFFFFFF
#define DBG_DUMP_SPLIT_HDR_DATA_SIZE_SHIFT	0
#define DBG_DUMP_SPLIT_HDR_SPLIT_TYPE_ID_MASK	0xFF
#define DBG_DUMP_SPLIT_HDR_SPLIT_TYPE_ID_SHIFT	24
};

/* Condition header for idle check */
struct dbg_idle_chk_cond_hdr {
	struct dbg_mode_hdr mode; /* Mode header */
	u16 data_size; /* size in dwords of the data following this header */
};

/* Idle Check condition register */
struct dbg_idle_chk_cond_reg {
	u32 data;
#define DBG_IDLE_CHK_COND_REG_ADDRESS_MASK	0x7FFFFF
#define DBG_IDLE_CHK_COND_REG_ADDRESS_SHIFT	0
#define DBG_IDLE_CHK_COND_REG_WIDE_BUS_MASK	0x1
#define DBG_IDLE_CHK_COND_REG_WIDE_BUS_SHIFT	23
#define DBG_IDLE_CHK_COND_REG_BLOCK_ID_MASK	0xFF
#define DBG_IDLE_CHK_COND_REG_BLOCK_ID_SHIFT	24
	u16 num_entries;
	u8 entry_size;
	u8 start_entry;
};

/* Idle Check info register */
struct dbg_idle_chk_info_reg {
	u32 data;
#define DBG_IDLE_CHK_INFO_REG_ADDRESS_MASK	0x7FFFFF
#define DBG_IDLE_CHK_INFO_REG_ADDRESS_SHIFT	0
#define DBG_IDLE_CHK_INFO_REG_WIDE_BUS_MASK	0x1
#define DBG_IDLE_CHK_INFO_REG_WIDE_BUS_SHIFT	23
#define DBG_IDLE_CHK_INFO_REG_BLOCK_ID_MASK	0xFF
#define DBG_IDLE_CHK_INFO_REG_BLOCK_ID_SHIFT	24
	u16 size; /* register size in dwords */
	struct dbg_mode_hdr mode; /* Mode header */
};

/* Idle Check register */
union dbg_idle_chk_reg {
	struct dbg_idle_chk_cond_reg cond_reg; /* condition register */
	struct dbg_idle_chk_info_reg info_reg; /* info register */
};

/* Idle Check result header */
struct dbg_idle_chk_result_hdr {
	u16 rule_id; /* Failing rule index */
	u16 mem_entry_id; /* Failing memory entry index */
	u8 num_dumped_cond_regs; /* number of dumped condition registers */
	u8 num_dumped_info_regs; /* number of dumped condition registers */
	u8 severity; /* from dbg_idle_chk_severity_types enum */
	u8 reserved;
};

/* Idle Check result register header */
struct dbg_idle_chk_result_reg_hdr {
	u8 data;
#define DBG_IDLE_CHK_RESULT_REG_HDR_IS_MEM_MASK  0x1
#define DBG_IDLE_CHK_RESULT_REG_HDR_IS_MEM_SHIFT 0
#define DBG_IDLE_CHK_RESULT_REG_HDR_REG_ID_MASK  0x7F
#define DBG_IDLE_CHK_RESULT_REG_HDR_REG_ID_SHIFT 1
	u8 start_entry; /* index of the first checked entry */
	u16 size; /* register size in dwords */
};

/* Idle Check rule */
struct dbg_idle_chk_rule {
	u16 rule_id; /* Idle Check rule ID */
	u8 severity; /* value from dbg_idle_chk_severity_types enum */
	u8 cond_id; /* Condition ID */
	u8 num_cond_regs; /* number of condition registers */
	u8 num_info_regs; /* number of info registers */
	u8 num_imms; /* number of immediates in the condition */
	u8 reserved1;
	u16 reg_offset; /* offset of this rules registers in the idle check
			 * register array (in dbg_idle_chk_reg units).
			 */
	u16 imm_offset; /* offset of this rules immediate values in the
			 * immediate values array (in dwords).
			 */
};

/* Idle Check rule parsing data */
struct dbg_idle_chk_rule_parsing_data {
	u32 data;
#define DBG_IDLE_CHK_RULE_PARSING_DATA_HAS_FW_MSG_MASK	0x1
#define DBG_IDLE_CHK_RULE_PARSING_DATA_HAS_FW_MSG_SHIFT	0
#define DBG_IDLE_CHK_RULE_PARSING_DATA_STR_OFFSET_MASK	0x7FFFFFFF
#define DBG_IDLE_CHK_RULE_PARSING_DATA_STR_OFFSET_SHIFT	1
};

/* Idle check severity types */
enum dbg_idle_chk_severity_types {
	/* idle check failure should cause an error */
	IDLE_CHK_SEVERITY_ERROR,
	/* idle check failure should cause an error only if theres no traffic */
	IDLE_CHK_SEVERITY_ERROR_NO_TRAFFIC,
	/* idle check failure should cause a warning */
	IDLE_CHK_SEVERITY_WARNING,
	MAX_DBG_IDLE_CHK_SEVERITY_TYPES
};

/* Reset register */
struct dbg_reset_reg {
	u32 data;
#define DBG_RESET_REG_ADDR_MASK        0xFFFFFF
#define DBG_RESET_REG_ADDR_SHIFT       0
#define DBG_RESET_REG_IS_REMOVED_MASK  0x1
#define DBG_RESET_REG_IS_REMOVED_SHIFT 24
#define DBG_RESET_REG_RESERVED_MASK    0x7F
#define DBG_RESET_REG_RESERVED_SHIFT   25
};

/* Debug Bus block data */
struct dbg_bus_block_data {
	u8 enable_mask;
	u8 right_shift;
	u8 force_valid_mask;
	u8 force_frame_mask;
	u8 dword_mask;
	u8 line_num;
	u8 hw_id;
	u8 flags;
#define DBG_BUS_BLOCK_DATA_IS_256B_LINE_MASK  0x1
#define DBG_BUS_BLOCK_DATA_IS_256B_LINE_SHIFT 0
#define DBG_BUS_BLOCK_DATA_RESERVED_MASK      0x7F
#define DBG_BUS_BLOCK_DATA_RESERVED_SHIFT     1
};

enum dbg_bus_clients {
	DBG_BUS_CLIENT_RBCN,
	DBG_BUS_CLIENT_RBCP,
	DBG_BUS_CLIENT_RBCR,
	DBG_BUS_CLIENT_RBCT,
	DBG_BUS_CLIENT_RBCU,
	DBG_BUS_CLIENT_RBCF,
	DBG_BUS_CLIENT_RBCX,
	DBG_BUS_CLIENT_RBCS,
	DBG_BUS_CLIENT_RBCH,
	DBG_BUS_CLIENT_RBCZ,
	DBG_BUS_CLIENT_OTHER_ENGINE,
	DBG_BUS_CLIENT_TIMESTAMP,
	DBG_BUS_CLIENT_CPU,
	DBG_BUS_CLIENT_RBCY,
	DBG_BUS_CLIENT_RBCQ,
	DBG_BUS_CLIENT_RBCM,
	DBG_BUS_CLIENT_RBCB,
	DBG_BUS_CLIENT_RBCW,
	DBG_BUS_CLIENT_RBCV,
	MAX_DBG_BUS_CLIENTS
};

/* Debug Bus constraint operation types */
enum dbg_bus_constraint_ops {
	DBG_BUS_CONSTRAINT_OP_EQ,
	DBG_BUS_CONSTRAINT_OP_NE,
	DBG_BUS_CONSTRAINT_OP_LT,
	DBG_BUS_CONSTRAINT_OP_LTC,
	DBG_BUS_CONSTRAINT_OP_LE,
	DBG_BUS_CONSTRAINT_OP_LEC,
	DBG_BUS_CONSTRAINT_OP_GT,
	DBG_BUS_CONSTRAINT_OP_GTC,
	DBG_BUS_CONSTRAINT_OP_GE,
	DBG_BUS_CONSTRAINT_OP_GEC,
	MAX_DBG_BUS_CONSTRAINT_OPS
};

/* Debug Bus trigger state data */
struct dbg_bus_trigger_state_data {
	u8 msg_len;
	u8 constraint_dword_mask;
	u8 storm_id;
	u8 reserved;
};

/* Debug Bus memory address */
struct dbg_bus_mem_addr {
	u32 lo;
	u32 hi;
};

/* Debug Bus PCI buffer data */
struct dbg_bus_pci_buf_data {
	struct dbg_bus_mem_addr phys_addr; /* PCI buffer physical address */
	struct dbg_bus_mem_addr virt_addr; /* PCI buffer virtual address */
	u32 size; /* PCI buffer size in bytes */
};

/* Debug Bus Storm EID range filter params */
struct dbg_bus_storm_eid_range_params {
	u8 min; /* Minimal event ID to filter on */
	u8 max; /* Maximal event ID to filter on */
};

/* Debug Bus Storm EID mask filter params */
struct dbg_bus_storm_eid_mask_params {
	u8 val; /* Event ID value */
	u8 mask; /* Event ID mask. 1s in the mask = dont care bits. */
};

/* Debug Bus Storm EID filter params */
union dbg_bus_storm_eid_params {
	struct dbg_bus_storm_eid_range_params range;
	struct dbg_bus_storm_eid_mask_params mask;
};

/* Debug Bus Storm data */
struct dbg_bus_storm_data {
	u8 enabled;
	u8 mode;
	u8 hw_id;
	u8 eid_filter_en;
	u8 eid_range_not_mask;
	u8 cid_filter_en;
	union dbg_bus_storm_eid_params eid_filter_params;
	u32 cid;
};

/* Debug Bus data */
struct dbg_bus_data {
	u32 app_version;
	u8 state;
	u8 mode_256b_en;
	u8 num_enabled_blocks;
	u8 num_enabled_storms;
	u8 target;
	u8 one_shot_en;
	u8 grc_input_en;
	u8 timestamp_input_en;
	u8 filter_en;
	u8 adding_filter;
	u8 filter_pre_trigger;
	u8 filter_post_trigger;
	u8 trigger_en;
	u8 filter_constraint_dword_mask;
	u8 next_trigger_state;
	u8 next_constraint_id;
	struct dbg_bus_trigger_state_data trigger_states[3];
	u8 filter_msg_len;
	u8 rcv_from_other_engine;
	u8 blocks_dword_mask;
	u8 blocks_dword_overlap;
	u32 hw_id_mask;
	struct dbg_bus_pci_buf_data pci_buf;
	struct dbg_bus_block_data blocks[132];
	struct dbg_bus_storm_data storms[6];
};

/* Debug bus states */
enum dbg_bus_states {
	DBG_BUS_STATE_IDLE,
	DBG_BUS_STATE_READY,
	DBG_BUS_STATE_RECORDING,
	DBG_BUS_STATE_STOPPED,
	MAX_DBG_BUS_STATES
};

/* Debug Bus Storm modes */
enum dbg_bus_storm_modes {
	DBG_BUS_STORM_MODE_PRINTF,
	DBG_BUS_STORM_MODE_PRAM_ADDR,
	DBG_BUS_STORM_MODE_DRA_RW,
	DBG_BUS_STORM_MODE_DRA_W,
	DBG_BUS_STORM_MODE_LD_ST_ADDR,
	DBG_BUS_STORM_MODE_DRA_FSM,
	DBG_BUS_STORM_MODE_FAST_DBGMUX,
	DBG_BUS_STORM_MODE_RH,
	DBG_BUS_STORM_MODE_RH_WITH_STORE,
	DBG_BUS_STORM_MODE_FOC,
	DBG_BUS_STORM_MODE_EXT_STORE,
	MAX_DBG_BUS_STORM_MODES
};

/* Debug bus target IDs */
enum dbg_bus_targets {
	DBG_BUS_TARGET_ID_INT_BUF,
	DBG_BUS_TARGET_ID_NIG,
	DBG_BUS_TARGET_ID_PCI,
	MAX_DBG_BUS_TARGETS
};

/* GRC Dump data */
struct dbg_grc_data {
	u8 params_initialized;
	u8 reserved1;
	u16 reserved2;
	u32 param_val[48];
};

/* Debug GRC params */
enum dbg_grc_params {
	DBG_GRC_PARAM_DUMP_TSTORM,
	DBG_GRC_PARAM_DUMP_MSTORM,
	DBG_GRC_PARAM_DUMP_USTORM,
	DBG_GRC_PARAM_DUMP_XSTORM,
	DBG_GRC_PARAM_DUMP_YSTORM,
	DBG_GRC_PARAM_DUMP_PSTORM,
	DBG_GRC_PARAM_DUMP_REGS,
	DBG_GRC_PARAM_DUMP_RAM,
	DBG_GRC_PARAM_DUMP_PBUF,
	DBG_GRC_PARAM_DUMP_IOR,
	DBG_GRC_PARAM_DUMP_VFC,
	DBG_GRC_PARAM_DUMP_CM_CTX,
	DBG_GRC_PARAM_DUMP_PXP,
	DBG_GRC_PARAM_DUMP_RSS,
	DBG_GRC_PARAM_DUMP_CAU,
	DBG_GRC_PARAM_DUMP_QM,
	DBG_GRC_PARAM_DUMP_MCP,
	DBG_GRC_PARAM_DUMP_DORQ,
	DBG_GRC_PARAM_DUMP_CFC,
	DBG_GRC_PARAM_DUMP_IGU,
	DBG_GRC_PARAM_DUMP_BRB,
	DBG_GRC_PARAM_DUMP_BTB,
	DBG_GRC_PARAM_DUMP_BMB,
	DBG_GRC_PARAM_RESERVD1,
	DBG_GRC_PARAM_DUMP_MULD,
	DBG_GRC_PARAM_DUMP_PRS,
	DBG_GRC_PARAM_DUMP_DMAE,
	DBG_GRC_PARAM_DUMP_TM,
	DBG_GRC_PARAM_DUMP_SDM,
	DBG_GRC_PARAM_DUMP_DIF,
	DBG_GRC_PARAM_DUMP_STATIC,
	DBG_GRC_PARAM_UNSTALL,
	DBG_GRC_PARAM_RESERVED2,
	DBG_GRC_PARAM_MCP_TRACE_META_SIZE,
	DBG_GRC_PARAM_EXCLUDE_ALL,
	DBG_GRC_PARAM_CRASH,
	DBG_GRC_PARAM_PARITY_SAFE,
	DBG_GRC_PARAM_DUMP_CM,
	DBG_GRC_PARAM_DUMP_PHY,
	DBG_GRC_PARAM_NO_MCP,
	DBG_GRC_PARAM_NO_FW_VER,
	DBG_GRC_PARAM_RESERVED3,
	DBG_GRC_PARAM_DUMP_MCP_HW_DUMP,
	DBG_GRC_PARAM_DUMP_ILT_CDUC,
	DBG_GRC_PARAM_DUMP_ILT_CDUT,
	DBG_GRC_PARAM_DUMP_CAU_EXT,
	MAX_DBG_GRC_PARAMS
};

/* Debug status codes */
enum dbg_status {
	DBG_STATUS_OK,
	DBG_STATUS_APP_VERSION_NOT_SET,
	DBG_STATUS_UNSUPPORTED_APP_VERSION,
	DBG_STATUS_DBG_BLOCK_NOT_RESET,
	DBG_STATUS_INVALID_ARGS,
	DBG_STATUS_OUTPUT_ALREADY_SET,
	DBG_STATUS_INVALID_PCI_BUF_SIZE,
	DBG_STATUS_PCI_BUF_ALLOC_FAILED,
	DBG_STATUS_PCI_BUF_NOT_ALLOCATED,
	DBG_STATUS_INVALID_FILTER_TRIGGER_DWORDS,
	DBG_STATUS_NO_MATCHING_FRAMING_MODE,
	DBG_STATUS_VFC_READ_ERROR,
	DBG_STATUS_STORM_ALREADY_ENABLED,
	DBG_STATUS_STORM_NOT_ENABLED,
	DBG_STATUS_BLOCK_ALREADY_ENABLED,
	DBG_STATUS_BLOCK_NOT_ENABLED,
	DBG_STATUS_NO_INPUT_ENABLED,
	DBG_STATUS_NO_FILTER_TRIGGER_256B,
	DBG_STATUS_FILTER_ALREADY_ENABLED,
	DBG_STATUS_TRIGGER_ALREADY_ENABLED,
	DBG_STATUS_TRIGGER_NOT_ENABLED,
	DBG_STATUS_CANT_ADD_CONSTRAINT,
	DBG_STATUS_TOO_MANY_TRIGGER_STATES,
	DBG_STATUS_TOO_MANY_CONSTRAINTS,
	DBG_STATUS_RECORDING_NOT_STARTED,
	DBG_STATUS_DATA_DIDNT_TRIGGER,
	DBG_STATUS_NO_DATA_RECORDED,
	DBG_STATUS_DUMP_BUF_TOO_SMALL,
	DBG_STATUS_DUMP_NOT_CHUNK_ALIGNED,
	DBG_STATUS_UNKNOWN_CHIP,
	DBG_STATUS_VIRT_MEM_ALLOC_FAILED,
	DBG_STATUS_BLOCK_IN_RESET,
	DBG_STATUS_INVALID_TRACE_SIGNATURE,
	DBG_STATUS_INVALID_NVRAM_BUNDLE,
	DBG_STATUS_NVRAM_GET_IMAGE_FAILED,
	DBG_STATUS_NON_ALIGNED_NVRAM_IMAGE,
	DBG_STATUS_NVRAM_READ_FAILED,
	DBG_STATUS_IDLE_CHK_PARSE_FAILED,
	DBG_STATUS_MCP_TRACE_BAD_DATA,
	DBG_STATUS_MCP_TRACE_NO_META,
	DBG_STATUS_MCP_COULD_NOT_HALT,
	DBG_STATUS_MCP_COULD_NOT_RESUME,
	DBG_STATUS_RESERVED0,
	DBG_STATUS_SEMI_FIFO_NOT_EMPTY,
	DBG_STATUS_IGU_FIFO_BAD_DATA,
	DBG_STATUS_MCP_COULD_NOT_MASK_PRTY,
	DBG_STATUS_FW_ASSERTS_PARSE_FAILED,
	DBG_STATUS_REG_FIFO_BAD_DATA,
	DBG_STATUS_PROTECTION_OVERRIDE_BAD_DATA,
	DBG_STATUS_DBG_ARRAY_NOT_SET,
	DBG_STATUS_RESERVED1,
	DBG_STATUS_NON_MATCHING_LINES,
	DBG_STATUS_INSUFFICIENT_HW_IDS,
	DBG_STATUS_DBG_BUS_IN_USE,
	DBG_STATUS_INVALID_STORM_DBG_MODE,
	DBG_STATUS_OTHER_ENGINE_BB_ONLY,
	DBG_STATUS_FILTER_SINGLE_HW_ID,
	DBG_STATUS_TRIGGER_SINGLE_HW_ID,
	DBG_STATUS_MISSING_TRIGGER_STATE_STORM,
	MAX_DBG_STATUS
};

/* Debug Storms IDs */
enum dbg_storms {
	DBG_TSTORM_ID,
	DBG_MSTORM_ID,
	DBG_USTORM_ID,
	DBG_XSTORM_ID,
	DBG_YSTORM_ID,
	DBG_PSTORM_ID,
	MAX_DBG_STORMS
};

/* Idle Check data */
struct idle_chk_data {
	u32 buf_size;
	u8 buf_size_set;
	u8 reserved1;
	u16 reserved2;
};

struct pretend_params {
	u8 split_type;
	u8 reserved;
	u16 split_id;
};

/* Debug Tools data (per HW function)
 */
struct dbg_tools_data {
	struct dbg_grc_data grc;
	struct dbg_bus_data bus;
	struct idle_chk_data idle_chk;
	u8 mode_enable[40];
	u8 block_in_reset[132];
	u8 chip_id;
	u8 hw_type;
	u8 num_ports;
	u8 num_pfs_per_port;
	u8 num_vfs;
	u8 initialized;
	u8 use_dmae;
	u8 reserved;
	struct pretend_params pretend;
	u32 num_regs_read;
};

/* ILT Clients */
enum ilt_clients {
	ILT_CLI_CDUC,
	ILT_CLI_CDUT,
	ILT_CLI_QM,
	ILT_CLI_TM,
	ILT_CLI_SRC,
	ILT_CLI_TSDM,
	ILT_CLI_RGFS,
	ILT_CLI_TGFS,
	MAX_ILT_CLIENTS
};

/***************************** Public Functions *******************************/

/**
 * qed_dbg_set_bin_ptr(): Sets a pointer to the binary data with debug
 *                        arrays.
 *
 * @p_hwfn: HW device data.
 * @bin_ptr: A pointer to the binary data with debug arrays.
 *
 * Return: enum dbg status.
 */
enum dbg_status qed_dbg_set_bin_ptr(struct qed_hwfn *p_hwfn,
				    const u8 * const bin_ptr);

/**
 * qed_read_regs(): Reads registers into a buffer (using GRC).
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf: Destination buffer.
 * @addr: Source GRC address in dwords.
 * @len: Number of registers to read.
 *
 * Return: Void.
 */
void qed_read_regs(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt, u32 *buf, u32 addr, u32 len);

/**
 * qed_read_fw_info(): Reads FW info from the chip.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @fw_info: (Out) a pointer to write the FW info into.
 *
 * Return: True if the FW info was read successfully from one of the Storms,
 * or false if all Storms are in reset.
 *
 * The FW info contains FW-related information, such as the FW version,
 * FW image (main/L2B/kuku), FW timestamp, etc.
 * The FW info is read from the internal RAM of the first Storm that is not in
 * reset.
 */
bool qed_read_fw_info(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt, struct fw_info *fw_info);
/**
 * qed_dbg_grc_config(): Sets the value of a GRC parameter.
 *
 * @p_hwfn: HW device data.
 * @grc_param: GRC parameter.
 * @val: Value to set.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set.
 *         - Grc_param is invalid.
 *         - Val is outside the allowed boundaries.
 */
enum dbg_status qed_dbg_grc_config(struct qed_hwfn *p_hwfn,
				   enum dbg_grc_params grc_param, u32 val);

/**
 * qed_dbg_grc_set_params_default(): Reverts all GRC parameters to their
 *                                   default value.
 *
 * @p_hwfn: HW device data.
 *
 * Return: Void.
 */
void qed_dbg_grc_set_params_default(struct qed_hwfn *p_hwfn);
/**
 * qed_dbg_grc_get_dump_buf_size(): Returns the required buffer size for
 *                                  GRC Dump.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf_size: (OUT) required buffer size (in dwords) for the GRC Dump
 *             data.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_grc_get_dump_buf_size(struct qed_hwfn *p_hwfn,
					      struct qed_ptt *p_ptt,
					      u32 *buf_size);

/**
 * qed_dbg_grc_dump(): Dumps GRC data into the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dump_buf: Pointer to write the collected GRC data into.
 * @buf_size_in_dwords:Size of the specified buffer in dwords.
 * @num_dumped_dwords: (OUT) number of dumped dwords.
 *
 * Return: Error if one of the following holds:
 *        - The version wasn't set.
 *        - The specified dump buffer is too small.
 *          Otherwise, returns ok.
 */
enum dbg_status qed_dbg_grc_dump(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 u32 *dump_buf,
				 u32 buf_size_in_dwords,
				 u32 *num_dumped_dwords);

/**
 * qed_dbg_idle_chk_get_dump_buf_size(): Returns the required buffer size
 *                                       for idle check results.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf_size: (OUT) required buffer size (in dwords) for the idle check
 *             data.
 *
 * return: Error if one of the following holds:
 *        - The version wasn't set.
 *          Otherwise, returns ok.
 */
enum dbg_status qed_dbg_idle_chk_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 *buf_size);

/**
 * qed_dbg_idle_chk_dump: Performs idle check and writes the results
 *                        into the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dump_buf: Pointer to write the idle check data into.
 * @buf_size_in_dwords: Size of the specified buffer in dwords.
 * @num_dumped_dwords: (OUT) number of dumped dwords.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set.
 *         - The specified buffer is too small.
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_idle_chk_dump(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      u32 *dump_buf,
				      u32 buf_size_in_dwords,
				      u32 *num_dumped_dwords);

/**
 * qed_dbg_mcp_trace_get_dump_buf_size(): Returns the required buffer size
 *                                        for mcp trace results.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf_size: (OUT) Required buffer size (in dwords) for mcp trace data.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set.
 *         - The trace data in MCP scratchpad contain an invalid signature.
 *         - The bundle ID in NVRAM is invalid.
 *         - The trace meta data cannot be found (in NVRAM or image file).
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_mcp_trace_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						    struct qed_ptt *p_ptt,
						    u32 *buf_size);

/**
 * qed_dbg_mcp_trace_dump(): Performs mcp trace and writes the results
 *                           into the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dump_buf: Pointer to write the mcp trace data into.
 * @buf_size_in_dwords: Size of the specified buffer in dwords.
 * @num_dumped_dwords: (OUT) number of dumped dwords.
 *
 * Return: Error if one of the following holds:
 *        - The version wasn't set.
 *        - The specified buffer is too small.
 *        - The trace data in MCP scratchpad contain an invalid signature.
 *        - The bundle ID in NVRAM is invalid.
 *        - The trace meta data cannot be found (in NVRAM or image file).
 *        - The trace meta data cannot be read (from NVRAM or image file).
 *          Otherwise, returns ok.
 */
enum dbg_status qed_dbg_mcp_trace_dump(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt,
				       u32 *dump_buf,
				       u32 buf_size_in_dwords,
				       u32 *num_dumped_dwords);

/**
 * qed_dbg_reg_fifo_get_dump_buf_size(): Returns the required buffer size
 *                                       for grc trace fifo results.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf_size: (OUT) Required buffer size (in dwords) for reg fifo data.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_reg_fifo_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 *buf_size);

/**
 * qed_dbg_reg_fifo_dump(): Reads the reg fifo and writes the results into
 *                          the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dump_buf: Pointer to write the reg fifo data into.
 * @buf_size_in_dwords: Size of the specified buffer in dwords.
 * @num_dumped_dwords: (OUT) number of dumped dwords.
 *
 * Return: Error if one of the following holds:
 *        - The version wasn't set.
 *        - The specified buffer is too small.
 *        - DMAE transaction failed.
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_reg_fifo_dump(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      u32 *dump_buf,
				      u32 buf_size_in_dwords,
				      u32 *num_dumped_dwords);

/**
 * qed_dbg_igu_fifo_get_dump_buf_size(): Returns the required buffer size
 *                                       for the IGU fifo results.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf_size: (OUT) Required buffer size (in dwords) for the IGU fifo
 *            data.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set.
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_igu_fifo_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 *buf_size);

/**
 * qed_dbg_igu_fifo_dump(): Reads the IGU fifo and writes the results into
 *                          the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dump_buf: Pointer to write the IGU fifo data into.
 * @buf_size_in_dwords: Size of the specified buffer in dwords.
 * @num_dumped_dwords: (OUT) number of dumped dwords.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set
 *         - The specified buffer is too small
 *         - DMAE transaction failed
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_igu_fifo_dump(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      u32 *dump_buf,
				      u32 buf_size_in_dwords,
				      u32 *num_dumped_dwords);

/**
 * qed_dbg_protection_override_get_dump_buf_size(): Returns the required
 *        buffer size for protection override window results.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf_size: (OUT) Required buffer size (in dwords) for protection
 *             override data.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set
 *           Otherwise, returns ok.
 */
enum dbg_status
qed_dbg_protection_override_get_dump_buf_size(struct qed_hwfn *p_hwfn,
					      struct qed_ptt *p_ptt,
					      u32 *buf_size);
/**
 * qed_dbg_protection_override_dump(): Reads protection override window
 *       entries and writes the results into the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dump_buf: Pointer to write the protection override data into.
 * @buf_size_in_dwords: Size of the specified buffer in dwords.
 * @num_dumped_dwords: (OUT) number of dumped dwords.
 *
 * @return: Error if one of the following holds:
 *          - The version wasn't set.
 *          - The specified buffer is too small.
 *          - DMAE transaction failed.
 *             Otherwise, returns ok.
 */
enum dbg_status qed_dbg_protection_override_dump(struct qed_hwfn *p_hwfn,
						 struct qed_ptt *p_ptt,
						 u32 *dump_buf,
						 u32 buf_size_in_dwords,
						 u32 *num_dumped_dwords);
/**
 * qed_dbg_fw_asserts_get_dump_buf_size(): Returns the required buffer
 *                                         size for FW Asserts results.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @buf_size: (OUT) Required buffer size (in dwords) for FW Asserts data.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set.
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_fw_asserts_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						     struct qed_ptt *p_ptt,
						     u32 *buf_size);
/**
 * qed_dbg_fw_asserts_dump(): Reads the FW Asserts and writes the results
 *                            into the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @dump_buf: Pointer to write the FW Asserts data into.
 * @buf_size_in_dwords: Size of the specified buffer in dwords.
 * @num_dumped_dwords: (OUT) number of dumped dwords.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set.
 *         - The specified buffer is too small.
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_fw_asserts_dump(struct qed_hwfn *p_hwfn,
					struct qed_ptt *p_ptt,
					u32 *dump_buf,
					u32 buf_size_in_dwords,
					u32 *num_dumped_dwords);

/**
 * qed_dbg_read_attn(): Reads the attention registers of the specified
 * block and type, and writes the results into the specified buffer.
 *
 * @p_hwfn: HW device data.
 * @p_ptt: Ptt window used for writing the registers.
 * @block: Block ID.
 * @attn_type: Attention type.
 * @clear_status: Indicates if the attention status should be cleared.
 * @results:  (OUT) Pointer to write the read results into.
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set
 *          Otherwise, returns ok.
 */
enum dbg_status qed_dbg_read_attn(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  enum block_id block,
				  enum dbg_attn_type attn_type,
				  bool clear_status,
				  struct dbg_attn_block_result *results);

/**
 * qed_dbg_print_attn(): Prints attention registers values in the
 *                       specified results struct.
 *
 * @p_hwfn: HW device data.
 * @results: Pointer to the attention read results
 *
 * Return: Error if one of the following holds:
 *        - The version wasn't set
 *          Otherwise, returns ok.
 */
enum dbg_status qed_dbg_print_attn(struct qed_hwfn *p_hwfn,
				   struct dbg_attn_block_result *results);

/******************************* Data Types **********************************/

struct mcp_trace_format {
	u32 data;
#define MCP_TRACE_FORMAT_MODULE_MASK	0x0000ffff
#define MCP_TRACE_FORMAT_MODULE_OFFSET	0
#define MCP_TRACE_FORMAT_LEVEL_MASK	0x00030000
#define MCP_TRACE_FORMAT_LEVEL_OFFSET	16
#define MCP_TRACE_FORMAT_P1_SIZE_MASK	0x000c0000
#define MCP_TRACE_FORMAT_P1_SIZE_OFFSET 18
#define MCP_TRACE_FORMAT_P2_SIZE_MASK	0x00300000
#define MCP_TRACE_FORMAT_P2_SIZE_OFFSET 20
#define MCP_TRACE_FORMAT_P3_SIZE_MASK	0x00c00000
#define MCP_TRACE_FORMAT_P3_SIZE_OFFSET 22
#define MCP_TRACE_FORMAT_LEN_MASK	0xff000000
#define MCP_TRACE_FORMAT_LEN_OFFSET	24

	char *format_str;
};

/* MCP Trace Meta data structure */
struct mcp_trace_meta {
	u32 modules_num;
	char **modules;
	u32 formats_num;
	struct mcp_trace_format *formats;
	bool is_allocated;
};

/* Debug Tools user data */
struct dbg_tools_user_data {
	struct mcp_trace_meta mcp_trace_meta;
	const u32 *mcp_trace_user_meta_buf;
};

/******************************** Constants **********************************/

#define MAX_NAME_LEN	16

/***************************** Public Functions *******************************/

/**
 * qed_dbg_user_set_bin_ptr(): Sets a pointer to the binary data with
 *                             debug arrays.
 *
 * @p_hwfn: HW device data.
 * @bin_ptr: a pointer to the binary data with debug arrays.
 *
 * Return: dbg_status.
 */
enum dbg_status qed_dbg_user_set_bin_ptr(struct qed_hwfn *p_hwfn,
					 const u8 * const bin_ptr);

/**
 * qed_dbg_alloc_user_data(): Allocates user debug data.
 *
 * @p_hwfn: HW device data.
 * @user_data_ptr: (OUT) a pointer to the allocated memory.
 *
 * Return: dbg_status.
 */
enum dbg_status qed_dbg_alloc_user_data(struct qed_hwfn *p_hwfn,
					void **user_data_ptr);

/**
 * qed_dbg_get_status_str(): Returns a string for the specified status.
 *
 * @status: A debug status code.
 *
 * Return: A string for the specified status.
 */
const char *qed_dbg_get_status_str(enum dbg_status status);

/**
 * qed_get_idle_chk_results_buf_size(): Returns the required buffer size
 *                                      for idle check results (in bytes).
 *
 * @p_hwfn: HW device data.
 * @dump_buf: idle check dump buffer.
 * @num_dumped_dwords: number of dwords that were dumped.
 * @results_buf_size: (OUT) required buffer size (in bytes) for the parsed
 *                    results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_get_idle_chk_results_buf_size(struct qed_hwfn *p_hwfn,
						  u32 *dump_buf,
						  u32  num_dumped_dwords,
						  u32 *results_buf_size);
/**
 * qed_print_idle_chk_results(): Prints idle check results
 *
 * @p_hwfn: HW device data.
 * @dump_buf: idle check dump buffer.
 * @num_dumped_dwords: number of dwords that were dumped.
 * @results_buf: buffer for printing the idle check results.
 * @num_errors: (OUT) number of errors found in idle check.
 * @num_warnings: (OUT) number of warnings found in idle check.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_idle_chk_results(struct qed_hwfn *p_hwfn,
					   u32 *dump_buf,
					   u32 num_dumped_dwords,
					   char *results_buf,
					   u32 *num_errors,
					   u32 *num_warnings);

/**
 * qed_dbg_mcp_trace_set_meta_data(): Sets the MCP Trace meta data.
 *
 * @p_hwfn: HW device data.
 * @meta_buf: Meta buffer.
 *
 * Return: Void.
 *
 * Needed in case the MCP Trace dump doesn't contain the meta data (e.g. due to
 * no NVRAM access).
 */
void qed_dbg_mcp_trace_set_meta_data(struct qed_hwfn *p_hwfn,
				     const u32 *meta_buf);

/**
 * qed_get_mcp_trace_results_buf_size(): Returns the required buffer size
 *                                       for MCP Trace results (in bytes).
 *
 * @p_hwfn: HW device data.
 * @dump_buf: MCP Trace dump buffer.
 * @num_dumped_dwords: number of dwords that were dumped.
 * @results_buf_size: (OUT) required buffer size (in bytes) for the parsed
 *                    results.
 *
 * Return: Rrror if the parsing fails, ok otherwise.
 */
enum dbg_status qed_get_mcp_trace_results_buf_size(struct qed_hwfn *p_hwfn,
						   u32 *dump_buf,
						   u32 num_dumped_dwords,
						   u32 *results_buf_size);

/**
 * qed_print_mcp_trace_results(): Prints MCP Trace results
 *
 * @p_hwfn: HW device data.
 * @dump_buf: MCP trace dump buffer, starting from the header.
 * @num_dumped_dwords: Member of dwords that were dumped.
 * @results_buf: Buffer for printing the mcp trace results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_mcp_trace_results(struct qed_hwfn *p_hwfn,
					    u32 *dump_buf,
					    u32 num_dumped_dwords,
					    char *results_buf);

/**
 * qed_print_mcp_trace_results_cont(): Prints MCP Trace results, and
 * keeps the MCP trace meta data allocated, to support continuous MCP Trace
 * parsing. After the continuous parsing ends, mcp_trace_free_meta_data should
 * be called to free the meta data.
 *
 * @p_hwfn: HW device data.
 * @dump_buf: MVP trace dump buffer, starting from the header.
 * @results_buf: Buffer for printing the mcp trace results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_mcp_trace_results_cont(struct qed_hwfn *p_hwfn,
						 u32 *dump_buf,
						 char *results_buf);

/**
 * qed_print_mcp_trace_line(): Prints MCP Trace results for a single line
 *
 * @p_hwfn: HW device data.
 * @dump_buf: MCP trace dump buffer, starting from the header.
 * @num_dumped_bytes: Number of bytes that were dumped.
 * @results_buf: Buffer for printing the mcp trace results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_mcp_trace_line(struct qed_hwfn *p_hwfn,
					 u8 *dump_buf,
					 u32 num_dumped_bytes,
					 char *results_buf);

/**
 * qed_mcp_trace_free_meta_data(): Frees the MCP Trace meta data.
 * Should be called after continuous MCP Trace parsing.
 *
 * @p_hwfn: HW device data.
 *
 * Return: Void.
 */
void qed_mcp_trace_free_meta_data(struct qed_hwfn *p_hwfn);

/**
 * qed_get_reg_fifo_results_buf_size(): Returns the required buffer size
 *                                      for reg_fifo results (in bytes).
 *
 * @p_hwfn: HW device data.
 * @dump_buf: Reg fifo dump buffer.
 * @num_dumped_dwords: Number of dwords that were dumped.
 * @results_buf_size: (OUT) required buffer size (in bytes) for the parsed
 *                     results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_get_reg_fifo_results_buf_size(struct qed_hwfn *p_hwfn,
						  u32 *dump_buf,
						  u32 num_dumped_dwords,
						  u32 *results_buf_size);

/**
 * qed_print_reg_fifo_results(): Prints reg fifo results.
 *
 * @p_hwfn: HW device data.
 * @dump_buf: Reg fifo dump buffer, starting from the header.
 * @num_dumped_dwords: Number of dwords that were dumped.
 * @results_buf: Buffer for printing the reg fifo results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_reg_fifo_results(struct qed_hwfn *p_hwfn,
					   u32 *dump_buf,
					   u32 num_dumped_dwords,
					   char *results_buf);

/**
 * qed_get_igu_fifo_results_buf_size(): Returns the required buffer size
 *                                      for igu_fifo results (in bytes).
 *
 * @p_hwfn: HW device data.
 * @dump_buf: IGU fifo dump buffer.
 * @num_dumped_dwords: number of dwords that were dumped.
 * @results_buf_size: (OUT) required buffer size (in bytes) for the parsed
 *                    results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_get_igu_fifo_results_buf_size(struct qed_hwfn *p_hwfn,
						  u32 *dump_buf,
						  u32 num_dumped_dwords,
						  u32 *results_buf_size);

/**
 * qed_print_igu_fifo_results(): Prints IGU fifo results
 *
 * @p_hwfn: HW device data.
 * @dump_buf: IGU fifo dump buffer, starting from the header.
 * @num_dumped_dwords: Number of dwords that were dumped.
 * @results_buf: Buffer for printing the IGU fifo results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_igu_fifo_results(struct qed_hwfn *p_hwfn,
					   u32 *dump_buf,
					   u32 num_dumped_dwords,
					   char *results_buf);

/**
 * qed_get_protection_override_results_buf_size(): Returns the required
 *         buffer size for protection override results (in bytes).
 *
 * @p_hwfn: HW device data.
 * @dump_buf: Protection override dump buffer.
 * @num_dumped_dwords: Number of dwords that were dumped.
 * @results_buf_size: (OUT) required buffer size (in bytes) for the parsed
 *                    results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status
qed_get_protection_override_results_buf_size(struct qed_hwfn *p_hwfn,
					     u32 *dump_buf,
					     u32 num_dumped_dwords,
					     u32 *results_buf_size);

/**
 * qed_print_protection_override_results(): Prints protection override
 *                                          results.
 *
 * @p_hwfn: HW device data.
 * @dump_buf: Protection override dump buffer, starting from the header.
 * @num_dumped_dwords: Number of dwords that were dumped.
 * @results_buf: Buffer for printing the reg fifo results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_protection_override_results(struct qed_hwfn *p_hwfn,
						      u32 *dump_buf,
						      u32 num_dumped_dwords,
						      char *results_buf);

/**
 * qed_get_fw_asserts_results_buf_size(): Returns the required buffer size
 *                                        for FW Asserts results (in bytes).
 *
 * @p_hwfn: HW device data.
 * @dump_buf: FW Asserts dump buffer.
 * @num_dumped_dwords: number of dwords that were dumped.
 * @results_buf_size: (OUT) required buffer size (in bytes) for the parsed
 *                    results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_get_fw_asserts_results_buf_size(struct qed_hwfn *p_hwfn,
						    u32 *dump_buf,
						    u32 num_dumped_dwords,
						    u32 *results_buf_size);

/**
 * qed_print_fw_asserts_results(): Prints FW Asserts results.
 *
 * @p_hwfn: HW device data.
 * @dump_buf: FW Asserts dump buffer, starting from the header.
 * @num_dumped_dwords: number of dwords that were dumped.
 * @results_buf: buffer for printing the FW Asserts results.
 *
 * Return: Error if the parsing fails, ok otherwise.
 */
enum dbg_status qed_print_fw_asserts_results(struct qed_hwfn *p_hwfn,
					     u32 *dump_buf,
					     u32 num_dumped_dwords,
					     char *results_buf);

/**
 * qed_dbg_parse_attn(): Parses and prints attention registers values in
 *                      the specified results struct.
 *
 * @p_hwfn: HW device data.
 * @results: Pointer to the attention read results
 *
 * Return: Error if one of the following holds:
 *         - The version wasn't set.
 *           Otherwise, returns ok.
 */
enum dbg_status qed_dbg_parse_attn(struct qed_hwfn *p_hwfn,
				   struct dbg_attn_block_result *results);
#endif
