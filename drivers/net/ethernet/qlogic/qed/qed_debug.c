// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 * Copyright (c) 2019-2021 Marvell International Ltd.
 */

#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/crc32.h>
#include "qed.h"
#include "qed_cxt.h"
#include "qed_hsi.h"
#include "qed_dbg_hsi.h"
#include "qed_hw.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"

/* Memory groups enum */
enum mem_groups {
	MEM_GROUP_PXP_MEM,
	MEM_GROUP_DMAE_MEM,
	MEM_GROUP_CM_MEM,
	MEM_GROUP_QM_MEM,
	MEM_GROUP_DORQ_MEM,
	MEM_GROUP_BRB_RAM,
	MEM_GROUP_BRB_MEM,
	MEM_GROUP_PRS_MEM,
	MEM_GROUP_SDM_MEM,
	MEM_GROUP_PBUF,
	MEM_GROUP_IOR,
	MEM_GROUP_RAM,
	MEM_GROUP_BTB_RAM,
	MEM_GROUP_RDIF_CTX,
	MEM_GROUP_TDIF_CTX,
	MEM_GROUP_CFC_MEM,
	MEM_GROUP_CONN_CFC_MEM,
	MEM_GROUP_CAU_PI,
	MEM_GROUP_CAU_MEM,
	MEM_GROUP_CAU_MEM_EXT,
	MEM_GROUP_PXP_ILT,
	MEM_GROUP_MULD_MEM,
	MEM_GROUP_BTB_MEM,
	MEM_GROUP_IGU_MEM,
	MEM_GROUP_IGU_MSIX,
	MEM_GROUP_CAU_SB,
	MEM_GROUP_BMB_RAM,
	MEM_GROUP_BMB_MEM,
	MEM_GROUP_TM_MEM,
	MEM_GROUP_TASK_CFC_MEM,
	MEM_GROUPS_NUM
};

/* Memory groups names */
static const char * const s_mem_group_names[] = {
	"PXP_MEM",
	"DMAE_MEM",
	"CM_MEM",
	"QM_MEM",
	"DORQ_MEM",
	"BRB_RAM",
	"BRB_MEM",
	"PRS_MEM",
	"SDM_MEM",
	"PBUF",
	"IOR",
	"RAM",
	"BTB_RAM",
	"RDIF_CTX",
	"TDIF_CTX",
	"CFC_MEM",
	"CONN_CFC_MEM",
	"CAU_PI",
	"CAU_MEM",
	"CAU_MEM_EXT",
	"PXP_ILT",
	"MULD_MEM",
	"BTB_MEM",
	"IGU_MEM",
	"IGU_MSIX",
	"CAU_SB",
	"BMB_RAM",
	"BMB_MEM",
	"TM_MEM",
	"TASK_CFC_MEM",
};

/* Idle check conditions */

static u32 cond5(const u32 *r, const u32 *imm)
{
	return ((r[0] & imm[0]) != imm[1]) && ((r[1] & imm[2]) != imm[3]);
}

static u32 cond7(const u32 *r, const u32 *imm)
{
	return ((r[0] >> imm[0]) & imm[1]) != imm[2];
}

static u32 cond6(const u32 *r, const u32 *imm)
{
	return (r[0] & imm[0]) != imm[1];
}

static u32 cond9(const u32 *r, const u32 *imm)
{
	return ((r[0] & imm[0]) >> imm[1]) !=
	    (((r[0] & imm[2]) >> imm[3]) | ((r[1] & imm[4]) << imm[5]));
}

static u32 cond10(const u32 *r, const u32 *imm)
{
	return ((r[0] & imm[0]) >> imm[1]) != (r[0] & imm[2]);
}

static u32 cond4(const u32 *r, const u32 *imm)
{
	return (r[0] & ~imm[0]) != imm[1];
}

static u32 cond0(const u32 *r, const u32 *imm)
{
	return (r[0] & ~r[1]) != imm[0];
}

static u32 cond14(const u32 *r, const u32 *imm)
{
	return (r[0] | imm[0]) != imm[1];
}

static u32 cond1(const u32 *r, const u32 *imm)
{
	return r[0] != imm[0];
}

static u32 cond11(const u32 *r, const u32 *imm)
{
	return r[0] != r[1] && r[2] == imm[0];
}

static u32 cond12(const u32 *r, const u32 *imm)
{
	return r[0] != r[1] && r[2] > imm[0];
}

static u32 cond3(const u32 *r, const u32 *imm)
{
	return r[0] != r[1];
}

static u32 cond13(const u32 *r, const u32 *imm)
{
	return r[0] & imm[0];
}

static u32 cond8(const u32 *r, const u32 *imm)
{
	return r[0] < (r[1] - imm[0]);
}

static u32 cond2(const u32 *r, const u32 *imm)
{
	return r[0] > imm[0];
}

/* Array of Idle Check conditions */
static u32(*cond_arr[]) (const u32 *r, const u32 *imm) = {
	cond0,
	cond1,
	cond2,
	cond3,
	cond4,
	cond5,
	cond6,
	cond7,
	cond8,
	cond9,
	cond10,
	cond11,
	cond12,
	cond13,
	cond14,
};

#define NUM_PHYS_BLOCKS 84

#define NUM_DBG_RESET_REGS 8

/******************************* Data Types **********************************/

enum hw_types {
	HW_TYPE_ASIC,
	PLATFORM_RESERVED,
	PLATFORM_RESERVED2,
	PLATFORM_RESERVED3,
	PLATFORM_RESERVED4,
	MAX_HW_TYPES
};

/* CM context types */
enum cm_ctx_types {
	CM_CTX_CONN_AG,
	CM_CTX_CONN_ST,
	CM_CTX_TASK_AG,
	CM_CTX_TASK_ST,
	NUM_CM_CTX_TYPES
};

/* Debug bus frame modes */
enum dbg_bus_frame_modes {
	DBG_BUS_FRAME_MODE_4ST = 0,	/* 4 Storm dwords (no HW) */
	DBG_BUS_FRAME_MODE_2ST_2HW = 1,	/* 2 Storm dwords, 2 HW dwords */
	DBG_BUS_FRAME_MODE_1ST_3HW = 2,	/* 1 Storm dwords, 3 HW dwords */
	DBG_BUS_FRAME_MODE_4HW = 3,	/* 4 HW dwords (no Storms) */
	DBG_BUS_FRAME_MODE_8HW = 4,	/* 8 HW dwords (no Storms) */
	DBG_BUS_NUM_FRAME_MODES
};

/* Debug bus SEMI frame modes */
enum dbg_bus_semi_frame_modes {
	DBG_BUS_SEMI_FRAME_MODE_4FAST = 0,	/* 4 fast dw */
	DBG_BUS_SEMI_FRAME_MODE_2FAST_2SLOW = 1, /* 2 fast dw, 2 slow dw */
	DBG_BUS_SEMI_FRAME_MODE_1FAST_3SLOW = 2, /* 1 fast dw,3 slow dw */
	DBG_BUS_SEMI_FRAME_MODE_4SLOW = 3,	/* 4 slow dw */
	DBG_BUS_SEMI_NUM_FRAME_MODES
};

/* Debug bus filter types */
enum dbg_bus_filter_types {
	DBG_BUS_FILTER_TYPE_OFF,	/* Filter always off */
	DBG_BUS_FILTER_TYPE_PRE,	/* Filter before trigger only */
	DBG_BUS_FILTER_TYPE_POST,	/* Filter after trigger only */
	DBG_BUS_FILTER_TYPE_ON	/* Filter always on */
};

/* Debug bus pre-trigger recording types */
enum dbg_bus_pre_trigger_types {
	DBG_BUS_PRE_TRIGGER_FROM_ZERO,	/* Record from time 0 */
	DBG_BUS_PRE_TRIGGER_NUM_CHUNKS,	/* Record some chunks before trigger */
	DBG_BUS_PRE_TRIGGER_DROP	/* Drop data before trigger */
};

/* Debug bus post-trigger recording types */
enum dbg_bus_post_trigger_types {
	DBG_BUS_POST_TRIGGER_RECORD,	/* Start recording after trigger */
	DBG_BUS_POST_TRIGGER_DROP	/* Drop data after trigger */
};

/* Debug bus other engine mode */
enum dbg_bus_other_engine_modes {
	DBG_BUS_OTHER_ENGINE_MODE_NONE,
	DBG_BUS_OTHER_ENGINE_MODE_DOUBLE_BW_TX,
	DBG_BUS_OTHER_ENGINE_MODE_DOUBLE_BW_RX,
	DBG_BUS_OTHER_ENGINE_MODE_CROSS_ENGINE_TX,
	DBG_BUS_OTHER_ENGINE_MODE_CROSS_ENGINE_RX
};

/* DBG block Framing mode definitions */
struct framing_mode_defs {
	u8 id;
	u8 blocks_dword_mask;
	u8 storms_dword_mask;
	u8 semi_framing_mode_id;
	u8 full_buf_thr;
};

/* Chip constant definitions */
struct chip_defs {
	const char *name;
	u8 dwords_per_cycle;
	u8 num_framing_modes;
	u32 num_ilt_pages;
	struct framing_mode_defs *framing_modes;
};

/* HW type constant definitions */
struct hw_type_defs {
	const char *name;
	u32 delay_factor;
	u32 dmae_thresh;
	u32 log_thresh;
};

/* RBC reset definitions */
struct rbc_reset_defs {
	u32 reset_reg_addr;
	u32 reset_val[MAX_CHIP_IDS];
};

/* Storm constant definitions.
 * Addresses are in bytes, sizes are in quad-regs.
 */
struct storm_defs {
	char letter;
	enum block_id sem_block_id;
	enum dbg_bus_clients dbg_client_id[MAX_CHIP_IDS];
	bool has_vfc;
	u32 sem_fast_mem_addr;
	u32 sem_frame_mode_addr;
	u32 sem_slow_enable_addr;
	u32 sem_slow_mode_addr;
	u32 sem_slow_mode1_conf_addr;
	u32 sem_sync_dbg_empty_addr;
	u32 sem_gpre_vect_addr;
	u32 cm_ctx_wr_addr;
	u32 cm_ctx_rd_addr[NUM_CM_CTX_TYPES];
	u32 cm_ctx_lid_sizes[MAX_CHIP_IDS][NUM_CM_CTX_TYPES];
};

/* Debug Bus Constraint operation constant definitions */
struct dbg_bus_constraint_op_defs {
	u8 hw_op_val;
	bool is_cyclic;
};

/* Storm Mode definitions */
struct storm_mode_defs {
	const char *name;
	bool is_fast_dbg;
	u8 id_in_hw;
	u32 src_disable_reg_addr;
	u32 src_enable_val;
	bool exists[MAX_CHIP_IDS];
};

struct grc_param_defs {
	u32 default_val[MAX_CHIP_IDS];
	u32 min;
	u32 max;
	bool is_preset;
	bool is_persistent;
	u32 exclude_all_preset_val;
	u32 crash_preset_val[MAX_CHIP_IDS];
};

/* Address is in 128b units. Width is in bits. */
struct rss_mem_defs {
	const char *mem_name;
	const char *type_name;
	u32 addr;
	u32 entry_width;
	u32 num_entries[MAX_CHIP_IDS];
};

struct vfc_ram_defs {
	const char *mem_name;
	const char *type_name;
	u32 base_row;
	u32 num_rows;
};

struct big_ram_defs {
	const char *instance_name;
	enum mem_groups mem_group_id;
	enum mem_groups ram_mem_group_id;
	enum dbg_grc_params grc_param;
	u32 addr_reg_addr;
	u32 data_reg_addr;
	u32 is_256b_reg_addr;
	u32 is_256b_bit_offset[MAX_CHIP_IDS];
	u32 ram_size[MAX_CHIP_IDS]; /* In dwords */
};

struct phy_defs {
	const char *phy_name;

	/* PHY base GRC address */
	u32 base_addr;

	/* Relative address of indirect TBUS address register (bits 0..7) */
	u32 tbus_addr_lo_addr;

	/* Relative address of indirect TBUS address register (bits 8..10) */
	u32 tbus_addr_hi_addr;

	/* Relative address of indirect TBUS data register (bits 0..7) */
	u32 tbus_data_lo_addr;

	/* Relative address of indirect TBUS data register (bits 8..11) */
	u32 tbus_data_hi_addr;
};

/* Split type definitions */
struct split_type_defs {
	const char *name;
};

/******************************** Constants **********************************/

#define BYTES_IN_DWORD			sizeof(u32)
/* In the macros below, size and offset are specified in bits */
#define CEIL_DWORDS(size)		DIV_ROUND_UP(size, 32)
#define FIELD_BIT_OFFSET(type, field)	type ## _ ## field ## _ ## OFFSET
#define FIELD_BIT_SIZE(type, field)	type ## _ ## field ## _ ## SIZE
#define FIELD_DWORD_OFFSET(type, field) \
	 ((int)(FIELD_BIT_OFFSET(type, field) / 32))
#define FIELD_DWORD_SHIFT(type, field)	(FIELD_BIT_OFFSET(type, field) % 32)
#define FIELD_BIT_MASK(type, field) \
	(((1 << FIELD_BIT_SIZE(type, field)) - 1) << \
	 FIELD_DWORD_SHIFT(type, field))

#define SET_VAR_FIELD(var, type, field, val) \
	do { \
		var[FIELD_DWORD_OFFSET(type, field)] &=	\
		(~FIELD_BIT_MASK(type, field));	\
		var[FIELD_DWORD_OFFSET(type, field)] |= \
		(val) << FIELD_DWORD_SHIFT(type, field); \
	} while (0)

#define ARR_REG_WR(dev, ptt, addr, arr, arr_size) \
	do { \
		for (i = 0; i < (arr_size); i++) \
			qed_wr(dev, ptt, addr,	(arr)[i]); \
	} while (0)

#define DWORDS_TO_BYTES(dwords)		((dwords) * BYTES_IN_DWORD)
#define BYTES_TO_DWORDS(bytes)		((bytes) / BYTES_IN_DWORD)

/* extra lines include a signature line + optional latency events line */
#define NUM_EXTRA_DBG_LINES(block) \
	(GET_FIELD((block)->flags, DBG_BLOCK_CHIP_HAS_LATENCY_EVENTS) ? 2 : 1)
#define NUM_DBG_LINES(block) \
	((block)->num_of_dbg_bus_lines + NUM_EXTRA_DBG_LINES(block))

#define USE_DMAE			true
#define PROTECT_WIDE_BUS		true

#define RAM_LINES_TO_DWORDS(lines)	((lines) * 2)
#define RAM_LINES_TO_BYTES(lines) \
	DWORDS_TO_BYTES(RAM_LINES_TO_DWORDS(lines))

#define REG_DUMP_LEN_SHIFT		24
#define MEM_DUMP_ENTRY_SIZE_DWORDS \
	BYTES_TO_DWORDS(sizeof(struct dbg_dump_mem))

#define IDLE_CHK_RULE_SIZE_DWORDS \
	BYTES_TO_DWORDS(sizeof(struct dbg_idle_chk_rule))

#define IDLE_CHK_RESULT_HDR_DWORDS \
	BYTES_TO_DWORDS(sizeof(struct dbg_idle_chk_result_hdr))

#define IDLE_CHK_RESULT_REG_HDR_DWORDS \
	BYTES_TO_DWORDS(sizeof(struct dbg_idle_chk_result_reg_hdr))

#define PAGE_MEM_DESC_SIZE_DWORDS \
	BYTES_TO_DWORDS(sizeof(struct phys_mem_desc))

#define IDLE_CHK_MAX_ENTRIES_SIZE	32

/* The sizes and offsets below are specified in bits */
#define VFC_CAM_CMD_STRUCT_SIZE		64
#define VFC_CAM_CMD_ROW_OFFSET		48
#define VFC_CAM_CMD_ROW_SIZE		9
#define VFC_CAM_ADDR_STRUCT_SIZE	16
#define VFC_CAM_ADDR_OP_OFFSET		0
#define VFC_CAM_ADDR_OP_SIZE		4
#define VFC_CAM_RESP_STRUCT_SIZE	256
#define VFC_RAM_ADDR_STRUCT_SIZE	16
#define VFC_RAM_ADDR_OP_OFFSET		0
#define VFC_RAM_ADDR_OP_SIZE		2
#define VFC_RAM_ADDR_ROW_OFFSET		2
#define VFC_RAM_ADDR_ROW_SIZE		10
#define VFC_RAM_RESP_STRUCT_SIZE	256

#define VFC_CAM_CMD_DWORDS		CEIL_DWORDS(VFC_CAM_CMD_STRUCT_SIZE)
#define VFC_CAM_ADDR_DWORDS		CEIL_DWORDS(VFC_CAM_ADDR_STRUCT_SIZE)
#define VFC_CAM_RESP_DWORDS		CEIL_DWORDS(VFC_CAM_RESP_STRUCT_SIZE)
#define VFC_RAM_CMD_DWORDS		VFC_CAM_CMD_DWORDS
#define VFC_RAM_ADDR_DWORDS		CEIL_DWORDS(VFC_RAM_ADDR_STRUCT_SIZE)
#define VFC_RAM_RESP_DWORDS		CEIL_DWORDS(VFC_RAM_RESP_STRUCT_SIZE)

#define NUM_VFC_RAM_TYPES		4

#define VFC_CAM_NUM_ROWS		512

#define VFC_OPCODE_CAM_RD		14
#define VFC_OPCODE_RAM_RD		0

#define NUM_RSS_MEM_TYPES		5

#define NUM_BIG_RAM_TYPES		3
#define BIG_RAM_NAME_LEN		3

#define NUM_PHY_TBUS_ADDRESSES		2048
#define PHY_DUMP_SIZE_DWORDS		(NUM_PHY_TBUS_ADDRESSES / 2)

#define RESET_REG_UNRESET_OFFSET	4

#define STALL_DELAY_MS			500

#define STATIC_DEBUG_LINE_DWORDS	9

#define NUM_COMMON_GLOBAL_PARAMS	11

#define MAX_RECURSION_DEPTH		10

#define FW_IMG_KUKU                     0
#define FW_IMG_MAIN			1
#define FW_IMG_L2B                      2

#define REG_FIFO_ELEMENT_DWORDS		2
#define REG_FIFO_DEPTH_ELEMENTS		32
#define REG_FIFO_DEPTH_DWORDS \
	(REG_FIFO_ELEMENT_DWORDS * REG_FIFO_DEPTH_ELEMENTS)

#define IGU_FIFO_ELEMENT_DWORDS		4
#define IGU_FIFO_DEPTH_ELEMENTS		64
#define IGU_FIFO_DEPTH_DWORDS \
	(IGU_FIFO_ELEMENT_DWORDS * IGU_FIFO_DEPTH_ELEMENTS)

#define PROTECTION_OVERRIDE_ELEMENT_DWORDS	2
#define PROTECTION_OVERRIDE_DEPTH_ELEMENTS	20
#define PROTECTION_OVERRIDE_DEPTH_DWORDS \
	(PROTECTION_OVERRIDE_DEPTH_ELEMENTS * \
	 PROTECTION_OVERRIDE_ELEMENT_DWORDS)

#define MCP_SPAD_TRACE_OFFSIZE_ADDR \
	(MCP_REG_SCRATCH + \
	 offsetof(struct static_init, sections[SPAD_SECTION_TRACE]))

#define MAX_SW_PLTAFORM_STR_SIZE	64

#define EMPTY_FW_VERSION_STR		"???_???_???_???"
#define EMPTY_FW_IMAGE_STR		"???????????????"

/***************************** Constant Arrays *******************************/

/* DBG block framing mode definitions, in descending preference order */
static struct framing_mode_defs s_framing_mode_defs[4] = {
	{DBG_BUS_FRAME_MODE_4ST, 0x0, 0xf,
	 DBG_BUS_SEMI_FRAME_MODE_4FAST,
	 10},
	{DBG_BUS_FRAME_MODE_4HW, 0xf, 0x0, DBG_BUS_SEMI_FRAME_MODE_4SLOW,
	 10},
	{DBG_BUS_FRAME_MODE_2ST_2HW, 0x3, 0xc,
	 DBG_BUS_SEMI_FRAME_MODE_2FAST_2SLOW, 10},
	{DBG_BUS_FRAME_MODE_1ST_3HW, 0x7, 0x8,
	 DBG_BUS_SEMI_FRAME_MODE_1FAST_3SLOW, 10}
};

/* Chip constant definitions array */
static struct chip_defs s_chip_defs[MAX_CHIP_IDS] = {
	{"bb", 4, DBG_BUS_NUM_FRAME_MODES, PSWRQ2_REG_ILT_MEMORY_SIZE_BB / 2,
	 s_framing_mode_defs},
	{"ah", 4, DBG_BUS_NUM_FRAME_MODES, PSWRQ2_REG_ILT_MEMORY_SIZE_K2 / 2,
	 s_framing_mode_defs}
};

/* Storm constant definitions array */
static struct storm_defs s_storm_defs[] = {
	/* Tstorm */
	{'T', BLOCK_TSEM,
		{DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT},
		true,
		TSEM_REG_FAST_MEMORY,
		TSEM_REG_DBG_FRAME_MODE, TSEM_REG_SLOW_DBG_ACTIVE,
		TSEM_REG_SLOW_DBG_MODE, TSEM_REG_DBG_MODE1_CFG,
		TSEM_REG_SYNC_DBG_EMPTY, TSEM_REG_DBG_GPRE_VECT,
		TCM_REG_CTX_RBC_ACCS,
		{TCM_REG_AGG_CON_CTX, TCM_REG_SM_CON_CTX, TCM_REG_AGG_TASK_CTX,
		 TCM_REG_SM_TASK_CTX},
		{{4, 16, 2, 4}, {4, 16, 2, 4}} /* {bb} {k2} */
	},

	/* Mstorm */
	{'M', BLOCK_MSEM,
		{DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM},
		false,
		MSEM_REG_FAST_MEMORY,
		MSEM_REG_DBG_FRAME_MODE,
		MSEM_REG_SLOW_DBG_ACTIVE,
		MSEM_REG_SLOW_DBG_MODE,
		MSEM_REG_DBG_MODE1_CFG,
		MSEM_REG_SYNC_DBG_EMPTY,
		MSEM_REG_DBG_GPRE_VECT,
		MCM_REG_CTX_RBC_ACCS,
		{MCM_REG_AGG_CON_CTX, MCM_REG_SM_CON_CTX, MCM_REG_AGG_TASK_CTX,
		 MCM_REG_SM_TASK_CTX },
		{{1, 10, 2, 7}, {1, 10, 2, 7}} /* {bb} {k2}*/
	},

	/* Ustorm */
	{'U', BLOCK_USEM,
		{DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU},
		false,
		USEM_REG_FAST_MEMORY,
		USEM_REG_DBG_FRAME_MODE,
		USEM_REG_SLOW_DBG_ACTIVE,
		USEM_REG_SLOW_DBG_MODE,
		USEM_REG_DBG_MODE1_CFG,
		USEM_REG_SYNC_DBG_EMPTY,
		USEM_REG_DBG_GPRE_VECT,
		UCM_REG_CTX_RBC_ACCS,
		{UCM_REG_AGG_CON_CTX, UCM_REG_SM_CON_CTX, UCM_REG_AGG_TASK_CTX,
		 UCM_REG_SM_TASK_CTX},
		{{2, 13, 3, 3}, {2, 13, 3, 3}} /* {bb} {k2} */
	},

	/* Xstorm */
	{'X', BLOCK_XSEM,
		{DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX},
		false,
		XSEM_REG_FAST_MEMORY,
		XSEM_REG_DBG_FRAME_MODE,
		XSEM_REG_SLOW_DBG_ACTIVE,
		XSEM_REG_SLOW_DBG_MODE,
		XSEM_REG_DBG_MODE1_CFG,
		XSEM_REG_SYNC_DBG_EMPTY,
		XSEM_REG_DBG_GPRE_VECT,
		XCM_REG_CTX_RBC_ACCS,
		{XCM_REG_AGG_CON_CTX, XCM_REG_SM_CON_CTX, 0, 0},
		{{9, 15, 0, 0}, {9, 15,	0, 0}} /* {bb} {k2} */
	},

	/* Ystorm */
	{'Y', BLOCK_YSEM,
		{DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCY},
		false,
		YSEM_REG_FAST_MEMORY,
		YSEM_REG_DBG_FRAME_MODE,
		YSEM_REG_SLOW_DBG_ACTIVE,
		YSEM_REG_SLOW_DBG_MODE,
		YSEM_REG_DBG_MODE1_CFG,
		YSEM_REG_SYNC_DBG_EMPTY,
		YSEM_REG_DBG_GPRE_VECT,
		YCM_REG_CTX_RBC_ACCS,
		{YCM_REG_AGG_CON_CTX, YCM_REG_SM_CON_CTX, YCM_REG_AGG_TASK_CTX,
		 YCM_REG_SM_TASK_CTX},
		{{2, 3, 2, 12}, {2, 3, 2, 12}} /* {bb} {k2} */
	},

	/* Pstorm */
	{'P', BLOCK_PSEM,
		{DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS},
		true,
		PSEM_REG_FAST_MEMORY,
		PSEM_REG_DBG_FRAME_MODE,
		PSEM_REG_SLOW_DBG_ACTIVE,
		PSEM_REG_SLOW_DBG_MODE,
		PSEM_REG_DBG_MODE1_CFG,
		PSEM_REG_SYNC_DBG_EMPTY,
		PSEM_REG_DBG_GPRE_VECT,
		PCM_REG_CTX_RBC_ACCS,
		{0, PCM_REG_SM_CON_CTX, 0, 0},
		{{0, 10, 0, 0}, {0, 10, 0, 0}} /* {bb} {k2} */
	},
};

static struct hw_type_defs s_hw_type_defs[] = {
	/* HW_TYPE_ASIC */
	{"asic", 1, 256, 32768},
	{"reserved", 0, 0, 0},
	{"reserved2", 0, 0, 0},
	{"reserved3", 0, 0, 0},
	{"reserved4", 0, 0, 0}
};

static struct grc_param_defs s_grc_param_defs[] = {
	/* DBG_GRC_PARAM_DUMP_TSTORM */
	{{1, 1}, 0, 1, false, false, 1, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_MSTORM */
	{{1, 1}, 0, 1, false, false, 1, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_USTORM */
	{{1, 1}, 0, 1, false, false, 1, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_XSTORM */
	{{1, 1}, 0, 1, false, false, 1, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_YSTORM */
	{{1, 1}, 0, 1, false, false, 1, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_PSTORM */
	{{1, 1}, 0, 1, false, false, 1, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_REGS */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_RAM */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_PBUF */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_IOR */
	{{0, 0}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_VFC */
	{{0, 0}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_CM_CTX */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_ILT */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_RSS */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_CAU */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_QM */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_MCP */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_DORQ */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_CFC */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_IGU */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_BRB */
	{{0, 0}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_BTB */
	{{0, 0}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_BMB */
	{{0, 0}, 0, 1, false, false, 0, {0, 0}},

	/* DBG_GRC_PARAM_RESERVED1 */
	{{0, 0}, 0, 1, false, false, 0, {0, 0}},

	/* DBG_GRC_PARAM_DUMP_MULD */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_PRS */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_DMAE */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_TM */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_SDM */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_DIF */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_STATIC */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_UNSTALL */
	{{0, 0}, 0, 1, false, false, 0, {0, 0}},

	/* DBG_GRC_PARAM_RESERVED2 */
	{{0, 0}, 0, 1, false, false, 0, {0, 0}},

	/* DBG_GRC_PARAM_MCP_TRACE_META_SIZE */
	{{0, 0}, 1, 0xffffffff, false, true, 0, {0, 0}},

	/* DBG_GRC_PARAM_EXCLUDE_ALL */
	{{0, 0}, 0, 1, true, false, 0, {0, 0}},

	/* DBG_GRC_PARAM_CRASH */
	{{0, 0}, 0, 1, true, false, 0, {0, 0}},

	/* DBG_GRC_PARAM_PARITY_SAFE */
	{{0, 0}, 0, 1, false, false, 0, {0, 0}},

	/* DBG_GRC_PARAM_DUMP_CM */
	{{1, 1}, 0, 1, false, false, 0, {1, 1}},

	/* DBG_GRC_PARAM_DUMP_PHY */
	{{0, 0}, 0, 1, false, false, 0, {0, 0}},

	/* DBG_GRC_PARAM_NO_MCP */
	{{0, 0}, 0, 1, false, false, 0, {0, 0}},

	/* DBG_GRC_PARAM_NO_FW_VER */
	{{0, 0}, 0, 1, false, false, 0, {0, 0}},

	/* DBG_GRC_PARAM_RESERVED3 */
	{{0, 0}, 0, 1, false, false, 0, {0, 0}},

	/* DBG_GRC_PARAM_DUMP_MCP_HW_DUMP */
	{{0, 1}, 0, 1, false, false, 0, {0, 1}},

	/* DBG_GRC_PARAM_DUMP_ILT_CDUC */
	{{1, 1}, 0, 1, false, false, 0, {0, 0}},

	/* DBG_GRC_PARAM_DUMP_ILT_CDUT */
	{{1, 1}, 0, 1, false, false, 0, {0, 0}},

	/* DBG_GRC_PARAM_DUMP_CAU_EXT */
	{{0, 0}, 0, 1, false, false, 0, {1, 1}}
};

static struct rss_mem_defs s_rss_mem_defs[] = {
	{"rss_mem_cid", "rss_cid", 0, 32,
	 {256, 320}},

	{"rss_mem_key_msb", "rss_key", 1024, 256,
	 {128, 208}},

	{"rss_mem_key_lsb", "rss_key", 2048, 64,
	 {128, 208}},

	{"rss_mem_info", "rss_info", 3072, 16,
	 {128, 208}},

	{"rss_mem_ind", "rss_ind", 4096, 16,
	 {16384, 26624}}
};

static struct vfc_ram_defs s_vfc_ram_defs[] = {
	{"vfc_ram_tt1", "vfc_ram", 0, 512},
	{"vfc_ram_mtt2", "vfc_ram", 512, 128},
	{"vfc_ram_stt2", "vfc_ram", 640, 32},
	{"vfc_ram_ro_vect", "vfc_ram", 672, 32}
};

static struct big_ram_defs s_big_ram_defs[] = {
	{"BRB", MEM_GROUP_BRB_MEM, MEM_GROUP_BRB_RAM, DBG_GRC_PARAM_DUMP_BRB,
	 BRB_REG_BIG_RAM_ADDRESS, BRB_REG_BIG_RAM_DATA,
	 MISC_REG_BLOCK_256B_EN, {0, 0},
	 {153600, 180224}},

	{"BTB", MEM_GROUP_BTB_MEM, MEM_GROUP_BTB_RAM, DBG_GRC_PARAM_DUMP_BTB,
	 BTB_REG_BIG_RAM_ADDRESS, BTB_REG_BIG_RAM_DATA,
	 MISC_REG_BLOCK_256B_EN, {0, 1},
	 {92160, 117760}},

	{"BMB", MEM_GROUP_BMB_MEM, MEM_GROUP_BMB_RAM, DBG_GRC_PARAM_DUMP_BMB,
	 BMB_REG_BIG_RAM_ADDRESS, BMB_REG_BIG_RAM_DATA,
	 MISCS_REG_BLOCK_256B_EN, {0, 0},
	 {36864, 36864}}
};

static struct rbc_reset_defs s_rbc_reset_defs[] = {
	{MISCS_REG_RESET_PL_HV,
	 {0x0, 0x400}},
	{MISC_REG_RESET_PL_PDA_VMAIN_1,
	 {0x4404040, 0x4404040}},
	{MISC_REG_RESET_PL_PDA_VMAIN_2,
	 {0x7, 0x7c00007}},
	{MISC_REG_RESET_PL_PDA_VAUX,
	 {0x2, 0x2}},
};

static struct phy_defs s_phy_defs[] = {
	{"nw_phy", NWS_REG_NWS_CMU_K2,
	 PHY_NW_IP_REG_PHY0_TOP_TBUS_ADDR_7_0_K2,
	 PHY_NW_IP_REG_PHY0_TOP_TBUS_ADDR_15_8_K2,
	 PHY_NW_IP_REG_PHY0_TOP_TBUS_DATA_7_0_K2,
	 PHY_NW_IP_REG_PHY0_TOP_TBUS_DATA_11_8_K2},
	{"sgmii_phy", MS_REG_MS_CMU_K2,
	 PHY_SGMII_IP_REG_AHB_CMU_CSR_0_X132_K2,
	 PHY_SGMII_IP_REG_AHB_CMU_CSR_0_X133_K2,
	 PHY_SGMII_IP_REG_AHB_CMU_CSR_0_X130_K2,
	 PHY_SGMII_IP_REG_AHB_CMU_CSR_0_X131_K2},
	{"pcie_phy0", PHY_PCIE_REG_PHY0_K2,
	 PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X132_K2,
	 PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X133_K2,
	 PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X130_K2,
	 PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X131_K2},
	{"pcie_phy1", PHY_PCIE_REG_PHY1_K2,
	 PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X132_K2,
	 PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X133_K2,
	 PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X130_K2,
	 PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X131_K2},
};

static struct split_type_defs s_split_type_defs[] = {
	/* SPLIT_TYPE_NONE */
	{"eng"},

	/* SPLIT_TYPE_PORT */
	{"port"},

	/* SPLIT_TYPE_PF */
	{"pf"},

	/* SPLIT_TYPE_PORT_PF */
	{"port"},

	/* SPLIT_TYPE_VF */
	{"vf"}
};

/******************************** Variables **********************************/

/* The version of the calling app */
static u32 s_app_ver;

/**************************** Private Functions ******************************/

static void qed_static_asserts(void)
{
}

/* Reads and returns a single dword from the specified unaligned buffer */
static u32 qed_read_unaligned_dword(u8 *buf)
{
	u32 dword;

	memcpy((u8 *)&dword, buf, sizeof(dword));
	return dword;
}

/* Sets the value of the specified GRC param */
static void qed_grc_set_param(struct qed_hwfn *p_hwfn,
			      enum dbg_grc_params grc_param, u32 val)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	dev_data->grc.param_val[grc_param] = val;
}

/* Returns the value of the specified GRC param */
static u32 qed_grc_get_param(struct qed_hwfn *p_hwfn,
			     enum dbg_grc_params grc_param)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	return dev_data->grc.param_val[grc_param];
}

/* Initializes the GRC parameters */
static void qed_dbg_grc_init_params(struct qed_hwfn *p_hwfn)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	if (!dev_data->grc.params_initialized) {
		qed_dbg_grc_set_params_default(p_hwfn);
		dev_data->grc.params_initialized = 1;
	}
}

/* Sets pointer and size for the specified binary buffer type */
static void qed_set_dbg_bin_buf(struct qed_hwfn *p_hwfn,
				enum bin_dbg_buffer_type buf_type,
				const u32 *ptr, u32 size)
{
	struct virt_mem_desc *buf = &p_hwfn->dbg_arrays[buf_type];

	buf->ptr = (void *)ptr;
	buf->size = size;
}

/* Initializes debug data for the specified device */
static enum dbg_status qed_dbg_dev_init(struct qed_hwfn *p_hwfn)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u8 num_pfs = 0, max_pfs_per_port = 0;

	if (dev_data->initialized)
		return DBG_STATUS_OK;

	if (!s_app_ver)
		return DBG_STATUS_APP_VERSION_NOT_SET;

	/* Set chip */
	if (QED_IS_K2(p_hwfn->cdev)) {
		dev_data->chip_id = CHIP_K2;
		dev_data->mode_enable[MODE_K2] = 1;
		dev_data->num_vfs = MAX_NUM_VFS_K2;
		num_pfs = MAX_NUM_PFS_K2;
		max_pfs_per_port = MAX_NUM_PFS_K2 / 2;
	} else if (QED_IS_BB_B0(p_hwfn->cdev)) {
		dev_data->chip_id = CHIP_BB;
		dev_data->mode_enable[MODE_BB] = 1;
		dev_data->num_vfs = MAX_NUM_VFS_BB;
		num_pfs = MAX_NUM_PFS_BB;
		max_pfs_per_port = MAX_NUM_PFS_BB;
	} else {
		return DBG_STATUS_UNKNOWN_CHIP;
	}

	/* Set HW type */
	dev_data->hw_type = HW_TYPE_ASIC;
	dev_data->mode_enable[MODE_ASIC] = 1;

	/* Set port mode */
	switch (p_hwfn->cdev->num_ports_in_engine) {
	case 1:
		dev_data->mode_enable[MODE_PORTS_PER_ENG_1] = 1;
		break;
	case 2:
		dev_data->mode_enable[MODE_PORTS_PER_ENG_2] = 1;
		break;
	case 4:
		dev_data->mode_enable[MODE_PORTS_PER_ENG_4] = 1;
		break;
	}

	/* Set 100G mode */
	if (QED_IS_CMT(p_hwfn->cdev))
		dev_data->mode_enable[MODE_100G] = 1;

	/* Set number of ports */
	if (dev_data->mode_enable[MODE_PORTS_PER_ENG_1] ||
	    dev_data->mode_enable[MODE_100G])
		dev_data->num_ports = 1;
	else if (dev_data->mode_enable[MODE_PORTS_PER_ENG_2])
		dev_data->num_ports = 2;
	else if (dev_data->mode_enable[MODE_PORTS_PER_ENG_4])
		dev_data->num_ports = 4;

	/* Set number of PFs per port */
	dev_data->num_pfs_per_port = min_t(u32,
					   num_pfs / dev_data->num_ports,
					   max_pfs_per_port);

	/* Initializes the GRC parameters */
	qed_dbg_grc_init_params(p_hwfn);

	dev_data->use_dmae = true;
	dev_data->initialized = 1;

	return DBG_STATUS_OK;
}

static const struct dbg_block *get_dbg_block(struct qed_hwfn *p_hwfn,
					     enum block_id block_id)
{
	const struct dbg_block *dbg_block;

	dbg_block = p_hwfn->dbg_arrays[BIN_BUF_DBG_BLOCKS].ptr;
	return dbg_block + block_id;
}

static const struct dbg_block_chip *qed_get_dbg_block_per_chip(struct qed_hwfn
							       *p_hwfn,
							       enum block_id
							       block_id)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	return (const struct dbg_block_chip *)
	    p_hwfn->dbg_arrays[BIN_BUF_DBG_BLOCKS_CHIP_DATA].ptr +
	    block_id * MAX_CHIP_IDS + dev_data->chip_id;
}

static const struct dbg_reset_reg *qed_get_dbg_reset_reg(struct qed_hwfn
							 *p_hwfn,
							 u8 reset_reg_id)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	return (const struct dbg_reset_reg *)
	    p_hwfn->dbg_arrays[BIN_BUF_DBG_RESET_REGS].ptr +
	    reset_reg_id * MAX_CHIP_IDS + dev_data->chip_id;
}

/* Reads the FW info structure for the specified Storm from the chip,
 * and writes it to the specified fw_info pointer.
 */
static void qed_read_storm_fw_info(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   u8 storm_id, struct fw_info *fw_info)
{
	struct storm_defs *storm = &s_storm_defs[storm_id];
	struct fw_info_location fw_info_location;
	u32 addr, i, size, *dest;

	memset(&fw_info_location, 0, sizeof(fw_info_location));
	memset(fw_info, 0, sizeof(*fw_info));

	/* Read first the address that points to fw_info location.
	 * The address is located in the last line of the Storm RAM.
	 */
	addr = storm->sem_fast_mem_addr + SEM_FAST_REG_INT_RAM +
	    DWORDS_TO_BYTES(SEM_FAST_REG_INT_RAM_SIZE) -
	    sizeof(fw_info_location);

	dest = (u32 *)&fw_info_location;
	size = BYTES_TO_DWORDS(sizeof(fw_info_location));

	for (i = 0; i < size; i++, addr += BYTES_IN_DWORD)
		dest[i] = qed_rd(p_hwfn, p_ptt, addr);

	/* Read FW version info from Storm RAM */
	size = le32_to_cpu(fw_info_location.size);
	if (!size || size > sizeof(*fw_info))
		return;

	addr = le32_to_cpu(fw_info_location.grc_addr);
	dest = (u32 *)fw_info;
	size = BYTES_TO_DWORDS(size);

	for (i = 0; i < size; i++, addr += BYTES_IN_DWORD)
		dest[i] = qed_rd(p_hwfn, p_ptt, addr);
}

/* Dumps the specified string to the specified buffer.
 * Returns the dumped size in bytes.
 */
static u32 qed_dump_str(char *dump_buf, bool dump, const char *str)
{
	if (dump)
		strcpy(dump_buf, str);

	return (u32)strlen(str) + 1;
}

/* Dumps zeros to align the specified buffer to dwords.
 * Returns the dumped size in bytes.
 */
static u32 qed_dump_align(char *dump_buf, bool dump, u32 byte_offset)
{
	u8 offset_in_dword, align_size;

	offset_in_dword = (u8)(byte_offset & 0x3);
	align_size = offset_in_dword ? BYTES_IN_DWORD - offset_in_dword : 0;

	if (dump && align_size)
		memset(dump_buf, 0, align_size);

	return align_size;
}

/* Writes the specified string param to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 qed_dump_str_param(u32 *dump_buf,
			      bool dump,
			      const char *param_name, const char *param_val)
{
	char *char_buf = (char *)dump_buf;
	u32 offset = 0;

	/* Dump param name */
	offset += qed_dump_str(char_buf + offset, dump, param_name);

	/* Indicate a string param value */
	if (dump)
		*(char_buf + offset) = 1;
	offset++;

	/* Dump param value */
	offset += qed_dump_str(char_buf + offset, dump, param_val);

	/* Align buffer to next dword */
	offset += qed_dump_align(char_buf + offset, dump, offset);

	return BYTES_TO_DWORDS(offset);
}

/* Writes the specified numeric param to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 qed_dump_num_param(u32 *dump_buf,
			      bool dump, const char *param_name, u32 param_val)
{
	char *char_buf = (char *)dump_buf;
	u32 offset = 0;

	/* Dump param name */
	offset += qed_dump_str(char_buf + offset, dump, param_name);

	/* Indicate a numeric param value */
	if (dump)
		*(char_buf + offset) = 0;
	offset++;

	/* Align buffer to next dword */
	offset += qed_dump_align(char_buf + offset, dump, offset);

	/* Dump param value (and change offset from bytes to dwords) */
	offset = BYTES_TO_DWORDS(offset);
	if (dump)
		*(dump_buf + offset) = param_val;
	offset++;

	return offset;
}

/* Reads the FW version and writes it as a param to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 qed_dump_fw_ver_param(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 u32 *dump_buf, bool dump)
{
	char fw_ver_str[16] = EMPTY_FW_VERSION_STR;
	char fw_img_str[16] = EMPTY_FW_IMAGE_STR;
	struct fw_info fw_info = { {0}, {0} };
	u32 offset = 0;

	if (dump && !qed_grc_get_param(p_hwfn, DBG_GRC_PARAM_NO_FW_VER)) {
		/* Read FW info from chip */
		qed_read_fw_info(p_hwfn, p_ptt, &fw_info);

		/* Create FW version/image strings */
		if (snprintf(fw_ver_str, sizeof(fw_ver_str),
			     "%d_%d_%d_%d", fw_info.ver.num.major,
			     fw_info.ver.num.minor, fw_info.ver.num.rev,
			     fw_info.ver.num.eng) < 0)
			DP_NOTICE(p_hwfn,
				  "Unexpected debug error: invalid FW version string\n");
		switch (fw_info.ver.image_id) {
		case FW_IMG_KUKU:
			strcpy(fw_img_str, "kuku");
			break;
		case FW_IMG_MAIN:
			strcpy(fw_img_str, "main");
			break;
		case FW_IMG_L2B:
			strcpy(fw_img_str, "l2b");
			break;
		default:
			strcpy(fw_img_str, "unknown");
			break;
		}
	}

	/* Dump FW version, image and timestamp */
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "fw-version", fw_ver_str);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "fw-image", fw_img_str);
	offset += qed_dump_num_param(dump_buf + offset, dump, "fw-timestamp",
				     le32_to_cpu(fw_info.ver.timestamp));

	return offset;
}

/* Reads the MFW version and writes it as a param to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 qed_dump_mfw_ver_param(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  u32 *dump_buf, bool dump)
{
	char mfw_ver_str[16] = EMPTY_FW_VERSION_STR;

	if (dump &&
	    !qed_grc_get_param(p_hwfn, DBG_GRC_PARAM_NO_FW_VER)) {
		u32 global_section_offsize, global_section_addr, mfw_ver;
		u32 public_data_addr, global_section_offsize_addr;

		/* Find MCP public data GRC address. Needs to be ORed with
		 * MCP_REG_SCRATCH due to a HW bug.
		 */
		public_data_addr = qed_rd(p_hwfn,
					  p_ptt,
					  MISC_REG_SHARED_MEM_ADDR) |
				   MCP_REG_SCRATCH;

		/* Find MCP public global section offset */
		global_section_offsize_addr = public_data_addr +
					      offsetof(struct mcp_public_data,
						       sections) +
					      sizeof(offsize_t) * PUBLIC_GLOBAL;
		global_section_offsize = qed_rd(p_hwfn, p_ptt,
						global_section_offsize_addr);
		global_section_addr =
			MCP_REG_SCRATCH +
			(global_section_offsize & OFFSIZE_OFFSET_MASK) * 4;

		/* Read MFW version from MCP public global section */
		mfw_ver = qed_rd(p_hwfn, p_ptt,
				 global_section_addr +
				 offsetof(struct public_global, mfw_ver));

		/* Dump MFW version param */
		if (snprintf(mfw_ver_str, sizeof(mfw_ver_str), "%d_%d_%d_%d",
			     (u8)(mfw_ver >> 24), (u8)(mfw_ver >> 16),
			     (u8)(mfw_ver >> 8), (u8)mfw_ver) < 0)
			DP_NOTICE(p_hwfn,
				  "Unexpected debug error: invalid MFW version string\n");
	}

	return qed_dump_str_param(dump_buf, dump, "mfw-version", mfw_ver_str);
}

/* Reads the chip revision from the chip and writes it as a param to the
 * specified buffer. Returns the dumped size in dwords.
 */
static u32 qed_dump_chip_revision_param(struct qed_hwfn *p_hwfn,
					struct qed_ptt *p_ptt,
					u32 *dump_buf, bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	char param_str[3] = "??";

	if (dev_data->hw_type == HW_TYPE_ASIC) {
		u32 chip_rev, chip_metal;

		chip_rev = qed_rd(p_hwfn, p_ptt, MISCS_REG_CHIP_REV);
		chip_metal = qed_rd(p_hwfn, p_ptt, MISCS_REG_CHIP_METAL);

		param_str[0] = 'a' + (u8)chip_rev;
		param_str[1] = '0' + (u8)chip_metal;
	}

	return qed_dump_str_param(dump_buf, dump, "chip-revision", param_str);
}

/* Writes a section header to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 qed_dump_section_hdr(u32 *dump_buf,
				bool dump, const char *name, u32 num_params)
{
	return qed_dump_num_param(dump_buf, dump, name, num_params);
}

/* Writes the common global params to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 qed_dump_common_global_params(struct qed_hwfn *p_hwfn,
					 struct qed_ptt *p_ptt,
					 u32 *dump_buf,
					 bool dump,
					 u8 num_specific_global_params)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	char sw_platform_str[MAX_SW_PLTAFORM_STR_SIZE];
	u32 offset = 0;
	u8 num_params;

	/* Dump global params section header */
	num_params = NUM_COMMON_GLOBAL_PARAMS + num_specific_global_params +
		(dev_data->chip_id == CHIP_BB ? 1 : 0);
	offset += qed_dump_section_hdr(dump_buf + offset,
				       dump, "global_params", num_params);

	/* Store params */
	offset += qed_dump_fw_ver_param(p_hwfn, p_ptt, dump_buf + offset, dump);
	offset += qed_dump_mfw_ver_param(p_hwfn,
					 p_ptt, dump_buf + offset, dump);
	offset += qed_dump_chip_revision_param(p_hwfn,
					       p_ptt, dump_buf + offset, dump);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump, "tools-version", TOOLS_VERSION);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump,
				     "chip",
				     s_chip_defs[dev_data->chip_id].name);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump,
				     "platform",
				     s_hw_type_defs[dev_data->hw_type].name);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "sw-platform", sw_platform_str);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump, "pci-func", p_hwfn->abs_pf_id);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump, "epoch", qed_get_epoch_time());
	if (dev_data->chip_id == CHIP_BB)
		offset += qed_dump_num_param(dump_buf + offset,
					     dump, "path", QED_PATH_ID(p_hwfn));

	return offset;
}

/* Writes the "last" section (including CRC) to the specified buffer at the
 * given offset. Returns the dumped size in dwords.
 */
static u32 qed_dump_last_section(u32 *dump_buf, u32 offset, bool dump)
{
	u32 start_offset = offset;

	/* Dump CRC section header */
	offset += qed_dump_section_hdr(dump_buf + offset, dump, "last", 0);

	/* Calculate CRC32 and add it to the dword after the "last" section */
	if (dump)
		*(dump_buf + offset) = ~crc32(0xffffffff,
					      (u8 *)dump_buf,
					      DWORDS_TO_BYTES(offset));

	offset++;

	return offset - start_offset;
}

/* Update blocks reset state  */
static void qed_update_blocks_reset_state(struct qed_hwfn *p_hwfn,
					  struct qed_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 reg_val[NUM_DBG_RESET_REGS] = { 0 };
	u8 rst_reg_id;
	u32 blk_id;

	/* Read reset registers */
	for (rst_reg_id = 0; rst_reg_id < NUM_DBG_RESET_REGS; rst_reg_id++) {
		const struct dbg_reset_reg *rst_reg;
		bool rst_reg_removed;
		u32 rst_reg_addr;

		rst_reg = qed_get_dbg_reset_reg(p_hwfn, rst_reg_id);
		rst_reg_removed = GET_FIELD(rst_reg->data,
					    DBG_RESET_REG_IS_REMOVED);
		rst_reg_addr = DWORDS_TO_BYTES(GET_FIELD(rst_reg->data,
							 DBG_RESET_REG_ADDR));

		if (!rst_reg_removed)
			reg_val[rst_reg_id] = qed_rd(p_hwfn, p_ptt,
						     rst_reg_addr);
	}

	/* Check if blocks are in reset */
	for (blk_id = 0; blk_id < NUM_PHYS_BLOCKS; blk_id++) {
		const struct dbg_block_chip *blk;
		bool has_rst_reg;
		bool is_removed;

		blk = qed_get_dbg_block_per_chip(p_hwfn, (enum block_id)blk_id);
		is_removed = GET_FIELD(blk->flags, DBG_BLOCK_CHIP_IS_REMOVED);
		has_rst_reg = GET_FIELD(blk->flags,
					DBG_BLOCK_CHIP_HAS_RESET_REG);

		if (!is_removed && has_rst_reg)
			dev_data->block_in_reset[blk_id] =
			    !(reg_val[blk->reset_reg_id] &
			      BIT(blk->reset_reg_bit_offset));
	}
}

/* is_mode_match recursive function */
static bool qed_is_mode_match_rec(struct qed_hwfn *p_hwfn,
				  u16 *modes_buf_offset, u8 rec_depth)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u8 *dbg_array;
	bool arg1, arg2;
	u8 tree_val;

	if (rec_depth > MAX_RECURSION_DEPTH) {
		DP_NOTICE(p_hwfn,
			  "Unexpected error: is_mode_match_rec exceeded the max recursion depth. This is probably due to a corrupt init/debug buffer.\n");
		return false;
	}

	/* Get next element from modes tree buffer */
	dbg_array = p_hwfn->dbg_arrays[BIN_BUF_DBG_MODE_TREE].ptr;
	tree_val = dbg_array[(*modes_buf_offset)++];

	switch (tree_val) {
	case INIT_MODE_OP_NOT:
		return !qed_is_mode_match_rec(p_hwfn,
					      modes_buf_offset, rec_depth + 1);
	case INIT_MODE_OP_OR:
	case INIT_MODE_OP_AND:
		arg1 = qed_is_mode_match_rec(p_hwfn,
					     modes_buf_offset, rec_depth + 1);
		arg2 = qed_is_mode_match_rec(p_hwfn,
					     modes_buf_offset, rec_depth + 1);
		return (tree_val == INIT_MODE_OP_OR) ? (arg1 ||
							arg2) : (arg1 && arg2);
	default:
		return dev_data->mode_enable[tree_val - MAX_INIT_MODE_OPS] > 0;
	}
}

/* Returns true if the mode (specified using modes_buf_offset) is enabled */
static bool qed_is_mode_match(struct qed_hwfn *p_hwfn, u16 *modes_buf_offset)
{
	return qed_is_mode_match_rec(p_hwfn, modes_buf_offset, 0);
}

/* Enable / disable the Debug block */
static void qed_bus_enable_dbg_block(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt, bool enable)
{
	qed_wr(p_hwfn, p_ptt, DBG_REG_DBG_BLOCK_ON, enable ? 1 : 0);
}

/* Resets the Debug block */
static void qed_bus_reset_dbg_block(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt)
{
	u32 reset_reg_addr, old_reset_reg_val, new_reset_reg_val;
	const struct dbg_reset_reg *reset_reg;
	const struct dbg_block_chip *block;

	block = qed_get_dbg_block_per_chip(p_hwfn, BLOCK_DBG);
	reset_reg = qed_get_dbg_reset_reg(p_hwfn, block->reset_reg_id);
	reset_reg_addr =
	    DWORDS_TO_BYTES(GET_FIELD(reset_reg->data, DBG_RESET_REG_ADDR));

	old_reset_reg_val = qed_rd(p_hwfn, p_ptt, reset_reg_addr);
	new_reset_reg_val =
	    old_reset_reg_val & ~BIT(block->reset_reg_bit_offset);

	qed_wr(p_hwfn, p_ptt, reset_reg_addr, new_reset_reg_val);
	qed_wr(p_hwfn, p_ptt, reset_reg_addr, old_reset_reg_val);
}

/* Enable / disable Debug Bus clients according to the specified mask
 * (1 = enable, 0 = disable).
 */
static void qed_bus_enable_clients(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt, u32 client_mask)
{
	qed_wr(p_hwfn, p_ptt, DBG_REG_CLIENT_ENABLE, client_mask);
}

static void qed_bus_config_dbg_line(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    enum block_id block_id,
				    u8 line_id,
				    u8 enable_mask,
				    u8 right_shift,
				    u8 force_valid_mask, u8 force_frame_mask)
{
	const struct dbg_block_chip *block =
		qed_get_dbg_block_per_chip(p_hwfn, block_id);

	qed_wr(p_hwfn, p_ptt, DWORDS_TO_BYTES(block->dbg_select_reg_addr),
	       line_id);
	qed_wr(p_hwfn, p_ptt, DWORDS_TO_BYTES(block->dbg_dword_enable_reg_addr),
	       enable_mask);
	qed_wr(p_hwfn, p_ptt, DWORDS_TO_BYTES(block->dbg_shift_reg_addr),
	       right_shift);
	qed_wr(p_hwfn, p_ptt, DWORDS_TO_BYTES(block->dbg_force_valid_reg_addr),
	       force_valid_mask);
	qed_wr(p_hwfn, p_ptt, DWORDS_TO_BYTES(block->dbg_force_frame_reg_addr),
	       force_frame_mask);
}

/* Disable debug bus in all blocks */
static void qed_bus_disable_blocks(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 block_id;

	/* Disable all blocks */
	for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
		const struct dbg_block_chip *block_per_chip =
		    qed_get_dbg_block_per_chip(p_hwfn,
					       (enum block_id)block_id);

		if (GET_FIELD(block_per_chip->flags,
			      DBG_BLOCK_CHIP_IS_REMOVED) ||
		    dev_data->block_in_reset[block_id])
			continue;

		/* Disable debug bus */
		if (GET_FIELD(block_per_chip->flags,
			      DBG_BLOCK_CHIP_HAS_DBG_BUS)) {
			u32 dbg_en_addr =
				block_per_chip->dbg_dword_enable_reg_addr;
			u16 modes_buf_offset =
			    GET_FIELD(block_per_chip->dbg_bus_mode.data,
				      DBG_MODE_HDR_MODES_BUF_OFFSET);
			bool eval_mode =
			    GET_FIELD(block_per_chip->dbg_bus_mode.data,
				      DBG_MODE_HDR_EVAL_MODE) > 0;

			if (!eval_mode ||
			    qed_is_mode_match(p_hwfn, &modes_buf_offset))
				qed_wr(p_hwfn, p_ptt,
				       DWORDS_TO_BYTES(dbg_en_addr),
				       0);
		}
	}
}

/* Returns true if the specified entity (indicated by GRC param) should be
 * included in the dump, false otherwise.
 */
static bool qed_grc_is_included(struct qed_hwfn *p_hwfn,
				enum dbg_grc_params grc_param)
{
	return qed_grc_get_param(p_hwfn, grc_param) > 0;
}

/* Returns the storm_id that matches the specified Storm letter,
 * or MAX_DBG_STORMS if invalid storm letter.
 */
static enum dbg_storms qed_get_id_from_letter(char storm_letter)
{
	u8 storm_id;

	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++)
		if (s_storm_defs[storm_id].letter == storm_letter)
			return (enum dbg_storms)storm_id;

	return MAX_DBG_STORMS;
}

/* Returns true of the specified Storm should be included in the dump, false
 * otherwise.
 */
static bool qed_grc_is_storm_included(struct qed_hwfn *p_hwfn,
				      enum dbg_storms storm)
{
	return qed_grc_get_param(p_hwfn, (enum dbg_grc_params)storm) > 0;
}

/* Returns true if the specified memory should be included in the dump, false
 * otherwise.
 */
static bool qed_grc_is_mem_included(struct qed_hwfn *p_hwfn,
				    enum block_id block_id, u8 mem_group_id)
{
	const struct dbg_block *block;
	u8 i;

	block = get_dbg_block(p_hwfn, block_id);

	/* If the block is associated with a Storm, check Storm match */
	if (block->associated_storm_letter) {
		enum dbg_storms associated_storm_id =
		    qed_get_id_from_letter(block->associated_storm_letter);

		if (associated_storm_id == MAX_DBG_STORMS ||
		    !qed_grc_is_storm_included(p_hwfn, associated_storm_id))
			return false;
	}

	for (i = 0; i < NUM_BIG_RAM_TYPES; i++) {
		struct big_ram_defs *big_ram = &s_big_ram_defs[i];

		if (mem_group_id == big_ram->mem_group_id ||
		    mem_group_id == big_ram->ram_mem_group_id)
			return qed_grc_is_included(p_hwfn, big_ram->grc_param);
	}

	switch (mem_group_id) {
	case MEM_GROUP_PXP_ILT:
	case MEM_GROUP_PXP_MEM:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_PXP);
	case MEM_GROUP_RAM:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_RAM);
	case MEM_GROUP_PBUF:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_PBUF);
	case MEM_GROUP_CAU_MEM:
	case MEM_GROUP_CAU_SB:
	case MEM_GROUP_CAU_PI:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_CAU);
	case MEM_GROUP_CAU_MEM_EXT:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_CAU_EXT);
	case MEM_GROUP_QM_MEM:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_QM);
	case MEM_GROUP_CFC_MEM:
	case MEM_GROUP_CONN_CFC_MEM:
	case MEM_GROUP_TASK_CFC_MEM:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_CFC) ||
		       qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_CM_CTX);
	case MEM_GROUP_DORQ_MEM:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_DORQ);
	case MEM_GROUP_IGU_MEM:
	case MEM_GROUP_IGU_MSIX:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_IGU);
	case MEM_GROUP_MULD_MEM:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_MULD);
	case MEM_GROUP_PRS_MEM:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_PRS);
	case MEM_GROUP_DMAE_MEM:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_DMAE);
	case MEM_GROUP_TM_MEM:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_TM);
	case MEM_GROUP_SDM_MEM:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_SDM);
	case MEM_GROUP_TDIF_CTX:
	case MEM_GROUP_RDIF_CTX:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_DIF);
	case MEM_GROUP_CM_MEM:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_CM);
	case MEM_GROUP_IOR:
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_IOR);
	default:
		return true;
	}
}

/* Stalls all Storms */
static void qed_grc_stall_storms(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt, bool stall)
{
	u32 reg_addr;
	u8 storm_id;

	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		if (!qed_grc_is_storm_included(p_hwfn,
					       (enum dbg_storms)storm_id))
			continue;

		reg_addr = s_storm_defs[storm_id].sem_fast_mem_addr +
		    SEM_FAST_REG_STALL_0;
		qed_wr(p_hwfn, p_ptt, reg_addr, stall ? 1 : 0);
	}

	msleep(STALL_DELAY_MS);
}

/* Takes all blocks out of reset. If rbc_only is true, only RBC clients are
 * taken out of reset.
 */
static void qed_grc_unreset_blocks(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt, bool rbc_only)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u8 chip_id = dev_data->chip_id;
	u32 i;

	/* Take RBCs out of reset */
	for (i = 0; i < ARRAY_SIZE(s_rbc_reset_defs); i++)
		if (s_rbc_reset_defs[i].reset_val[dev_data->chip_id])
			qed_wr(p_hwfn,
			       p_ptt,
			       s_rbc_reset_defs[i].reset_reg_addr +
			       RESET_REG_UNRESET_OFFSET,
			       s_rbc_reset_defs[i].reset_val[chip_id]);

	if (!rbc_only) {
		u32 reg_val[NUM_DBG_RESET_REGS] = { 0 };
		u8 reset_reg_id;
		u32 block_id;

		/* Fill reset regs values */
		for (block_id = 0; block_id < NUM_PHYS_BLOCKS; block_id++) {
			bool is_removed, has_reset_reg, unreset_before_dump;
			const struct dbg_block_chip *block;

			block = qed_get_dbg_block_per_chip(p_hwfn,
							   (enum block_id)
							   block_id);
			is_removed =
			    GET_FIELD(block->flags, DBG_BLOCK_CHIP_IS_REMOVED);
			has_reset_reg =
			    GET_FIELD(block->flags,
				      DBG_BLOCK_CHIP_HAS_RESET_REG);
			unreset_before_dump =
			    GET_FIELD(block->flags,
				      DBG_BLOCK_CHIP_UNRESET_BEFORE_DUMP);

			if (!is_removed && has_reset_reg && unreset_before_dump)
				reg_val[block->reset_reg_id] |=
				    BIT(block->reset_reg_bit_offset);
		}

		/* Write reset registers */
		for (reset_reg_id = 0; reset_reg_id < NUM_DBG_RESET_REGS;
		     reset_reg_id++) {
			const struct dbg_reset_reg *reset_reg;
			u32 reset_reg_addr;

			reset_reg = qed_get_dbg_reset_reg(p_hwfn, reset_reg_id);

			if (GET_FIELD
			    (reset_reg->data, DBG_RESET_REG_IS_REMOVED))
				continue;

			if (reg_val[reset_reg_id]) {
				reset_reg_addr =
				    GET_FIELD(reset_reg->data,
					      DBG_RESET_REG_ADDR);
				qed_wr(p_hwfn,
				       p_ptt,
				       DWORDS_TO_BYTES(reset_reg_addr) +
				       RESET_REG_UNRESET_OFFSET,
				       reg_val[reset_reg_id]);
			}
		}
	}
}

/* Returns the attention block data of the specified block */
static const struct dbg_attn_block_type_data *
qed_get_block_attn_data(struct qed_hwfn *p_hwfn,
			enum block_id block_id, enum dbg_attn_type attn_type)
{
	const struct dbg_attn_block *base_attn_block_arr =
	    (const struct dbg_attn_block *)
	    p_hwfn->dbg_arrays[BIN_BUF_DBG_ATTN_BLOCKS].ptr;

	return &base_attn_block_arr[block_id].per_type_data[attn_type];
}

/* Returns the attention registers of the specified block */
static const struct dbg_attn_reg *
qed_get_block_attn_regs(struct qed_hwfn *p_hwfn,
			enum block_id block_id, enum dbg_attn_type attn_type,
			u8 *num_attn_regs)
{
	const struct dbg_attn_block_type_data *block_type_data =
	    qed_get_block_attn_data(p_hwfn, block_id, attn_type);

	*num_attn_regs = block_type_data->num_regs;

	return (const struct dbg_attn_reg *)
		p_hwfn->dbg_arrays[BIN_BUF_DBG_ATTN_REGS].ptr +
		block_type_data->regs_offset;
}

/* For each block, clear the status of all parities */
static void qed_grc_clear_all_prty(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	const struct dbg_attn_reg *attn_reg_arr;
	u32 block_id, sts_clr_address;
	u8 reg_idx, num_attn_regs;

	for (block_id = 0; block_id < NUM_PHYS_BLOCKS; block_id++) {
		if (dev_data->block_in_reset[block_id])
			continue;

		attn_reg_arr = qed_get_block_attn_regs(p_hwfn,
						       (enum block_id)block_id,
						       ATTN_TYPE_PARITY,
						       &num_attn_regs);

		for (reg_idx = 0; reg_idx < num_attn_regs; reg_idx++) {
			const struct dbg_attn_reg *reg_data =
				&attn_reg_arr[reg_idx];
			u16 modes_buf_offset;
			bool eval_mode;

			/* Check mode */
			eval_mode = GET_FIELD(reg_data->mode.data,
					      DBG_MODE_HDR_EVAL_MODE) > 0;
			modes_buf_offset =
				GET_FIELD(reg_data->mode.data,
					  DBG_MODE_HDR_MODES_BUF_OFFSET);

			sts_clr_address = reg_data->sts_clr_address;
			/* If Mode match: clear parity status */
			if (!eval_mode ||
			    qed_is_mode_match(p_hwfn, &modes_buf_offset))
				qed_rd(p_hwfn, p_ptt,
				       DWORDS_TO_BYTES(sts_clr_address));
		}
	}
}

/* Finds the meta data image in NVRAM */
static enum dbg_status qed_find_nvram_image(struct qed_hwfn *p_hwfn,
					    struct qed_ptt *p_ptt,
					    u32 image_type,
					    u32 *nvram_offset_bytes,
					    u32 *nvram_size_bytes)
{
	u32 ret_mcp_resp, ret_mcp_param, ret_txn_size;
	struct mcp_file_att file_att;
	int nvm_result;

	/* Call NVRAM get file command */
	nvm_result = qed_mcp_nvm_rd_cmd(p_hwfn,
					p_ptt,
					DRV_MSG_CODE_NVM_GET_FILE_ATT,
					image_type,
					&ret_mcp_resp,
					&ret_mcp_param,
					&ret_txn_size,
					(u32 *)&file_att, false);

	/* Check response */
	if (nvm_result || (ret_mcp_resp & FW_MSG_CODE_MASK) !=
	    FW_MSG_CODE_NVM_OK)
		return DBG_STATUS_NVRAM_GET_IMAGE_FAILED;

	/* Update return values */
	*nvram_offset_bytes = file_att.nvm_start_addr;
	*nvram_size_bytes = file_att.len;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_DEBUG,
		   "find_nvram_image: found NVRAM image of type %d in NVRAM offset %d bytes with size %d bytes\n",
		   image_type, *nvram_offset_bytes, *nvram_size_bytes);

	/* Check alignment */
	if (*nvram_size_bytes & 0x3)
		return DBG_STATUS_NON_ALIGNED_NVRAM_IMAGE;

	return DBG_STATUS_OK;
}

/* Reads data from NVRAM */
static enum dbg_status qed_nvram_read(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      u32 nvram_offset_bytes,
				      u32 nvram_size_bytes, u32 *ret_buf)
{
	u32 ret_mcp_resp, ret_mcp_param, ret_read_size, bytes_to_copy;
	s32 bytes_left = nvram_size_bytes;
	u32 read_offset = 0, param = 0;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_DEBUG,
		   "nvram_read: reading image of size %d bytes from NVRAM\n",
		   nvram_size_bytes);

	do {
		bytes_to_copy =
		    (bytes_left >
		     MCP_DRV_NVM_BUF_LEN) ? MCP_DRV_NVM_BUF_LEN : bytes_left;

		/* Call NVRAM read command */
		SET_MFW_FIELD(param,
			      DRV_MB_PARAM_NVM_OFFSET,
			      nvram_offset_bytes + read_offset);
		SET_MFW_FIELD(param, DRV_MB_PARAM_NVM_LEN, bytes_to_copy);
		if (qed_mcp_nvm_rd_cmd(p_hwfn, p_ptt,
				       DRV_MSG_CODE_NVM_READ_NVRAM, param,
				       &ret_mcp_resp,
				       &ret_mcp_param, &ret_read_size,
				       (u32 *)((u8 *)ret_buf + read_offset),
				       false))
			return DBG_STATUS_NVRAM_READ_FAILED;

		/* Check response */
		if ((ret_mcp_resp & FW_MSG_CODE_MASK) != FW_MSG_CODE_NVM_OK)
			return DBG_STATUS_NVRAM_READ_FAILED;

		/* Update read offset */
		read_offset += ret_read_size;
		bytes_left -= ret_read_size;
	} while (bytes_left > 0);

	return DBG_STATUS_OK;
}

/* Dumps GRC registers section header. Returns the dumped size in dwords.
 * the following parameters are dumped:
 * - count: no. of dumped entries
 * - split_type: split type
 * - split_id: split ID (dumped only if split_id != SPLIT_TYPE_NONE)
 * - reg_type_name: register type name (dumped only if reg_type_name != NULL)
 */
static u32 qed_grc_dump_regs_hdr(u32 *dump_buf,
				 bool dump,
				 u32 num_reg_entries,
				 enum init_split_types split_type,
				 u8 split_id, const char *reg_type_name)
{
	u8 num_params = 2 +
	    (split_type != SPLIT_TYPE_NONE ? 1 : 0) + (reg_type_name ? 1 : 0);
	u32 offset = 0;

	offset += qed_dump_section_hdr(dump_buf + offset,
				       dump, "grc_regs", num_params);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump, "count", num_reg_entries);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "split",
				     s_split_type_defs[split_type].name);
	if (split_type != SPLIT_TYPE_NONE)
		offset += qed_dump_num_param(dump_buf + offset,
					     dump, "id", split_id);
	if (reg_type_name)
		offset += qed_dump_str_param(dump_buf + offset,
					     dump, "type", reg_type_name);

	return offset;
}

/* Reads the specified registers into the specified buffer.
 * The addr and len arguments are specified in dwords.
 */
void qed_read_regs(struct qed_hwfn *p_hwfn,
		   struct qed_ptt *p_ptt, u32 *buf, u32 addr, u32 len)
{
	u32 i;

	for (i = 0; i < len; i++)
		buf[i] = qed_rd(p_hwfn, p_ptt, DWORDS_TO_BYTES(addr + i));
}

/* Dumps the GRC registers in the specified address range.
 * Returns the dumped size in dwords.
 * The addr and len arguments are specified in dwords.
 */
static u32 qed_grc_dump_addr_range(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   u32 *dump_buf,
				   bool dump, u32 addr, u32 len, bool wide_bus,
				   enum init_split_types split_type,
				   u8 split_id)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u8 port_id = 0, pf_id = 0, vf_id = 0, fid = 0;
	bool read_using_dmae = false;
	u32 thresh;

	if (!dump)
		return len;

	switch (split_type) {
	case SPLIT_TYPE_PORT:
		port_id = split_id;
		break;
	case SPLIT_TYPE_PF:
		pf_id = split_id;
		break;
	case SPLIT_TYPE_PORT_PF:
		port_id = split_id / dev_data->num_pfs_per_port;
		pf_id = port_id + dev_data->num_ports *
		    (split_id % dev_data->num_pfs_per_port);
		break;
	case SPLIT_TYPE_VF:
		vf_id = split_id;
		break;
	default:
		break;
	}

	/* Try reading using DMAE */
	if (dev_data->use_dmae && split_type != SPLIT_TYPE_VF &&
	    (len >= s_hw_type_defs[dev_data->hw_type].dmae_thresh ||
	     (PROTECT_WIDE_BUS && wide_bus))) {
		struct qed_dmae_params dmae_params;

		/* Set DMAE params */
		memset(&dmae_params, 0, sizeof(dmae_params));
		SET_FIELD(dmae_params.flags, QED_DMAE_PARAMS_COMPLETION_DST, 1);
		switch (split_type) {
		case SPLIT_TYPE_PORT:
			SET_FIELD(dmae_params.flags, QED_DMAE_PARAMS_PORT_VALID,
				  1);
			dmae_params.port_id = port_id;
			break;
		case SPLIT_TYPE_PF:
			SET_FIELD(dmae_params.flags,
				  QED_DMAE_PARAMS_SRC_PF_VALID, 1);
			dmae_params.src_pfid = pf_id;
			break;
		case SPLIT_TYPE_PORT_PF:
			SET_FIELD(dmae_params.flags, QED_DMAE_PARAMS_PORT_VALID,
				  1);
			SET_FIELD(dmae_params.flags,
				  QED_DMAE_PARAMS_SRC_PF_VALID, 1);
			dmae_params.port_id = port_id;
			dmae_params.src_pfid = pf_id;
			break;
		default:
			break;
		}

		/* Execute DMAE command */
		read_using_dmae = !qed_dmae_grc2host(p_hwfn,
						     p_ptt,
						     DWORDS_TO_BYTES(addr),
						     (u64)(uintptr_t)(dump_buf),
						     len, &dmae_params);
		if (!read_using_dmae) {
			dev_data->use_dmae = 0;
			DP_VERBOSE(p_hwfn,
				   QED_MSG_DEBUG,
				   "Failed reading from chip using DMAE, using GRC instead\n");
		}
	}

	if (read_using_dmae)
		goto print_log;

	/* If not read using DMAE, read using GRC */

	/* Set pretend */
	if (split_type != dev_data->pretend.split_type ||
	    split_id != dev_data->pretend.split_id) {
		switch (split_type) {
		case SPLIT_TYPE_PORT:
			qed_port_pretend(p_hwfn, p_ptt, port_id);
			break;
		case SPLIT_TYPE_PF:
			fid = FIELD_VALUE(PXP_PRETEND_CONCRETE_FID_PFID,
					  pf_id);
			qed_fid_pretend(p_hwfn, p_ptt, fid);
			break;
		case SPLIT_TYPE_PORT_PF:
			fid = FIELD_VALUE(PXP_PRETEND_CONCRETE_FID_PFID,
					  pf_id);
			qed_port_fid_pretend(p_hwfn, p_ptt, port_id, fid);
			break;
		case SPLIT_TYPE_VF:
			fid = FIELD_VALUE(PXP_PRETEND_CONCRETE_FID_VFVALID, 1)
			      | FIELD_VALUE(PXP_PRETEND_CONCRETE_FID_VFID,
					  vf_id);
			qed_fid_pretend(p_hwfn, p_ptt, fid);
			break;
		default:
			break;
		}

		dev_data->pretend.split_type = (u8)split_type;
		dev_data->pretend.split_id = split_id;
	}

	/* Read registers using GRC */
	qed_read_regs(p_hwfn, p_ptt, dump_buf, addr, len);

print_log:
	/* Print log */
	dev_data->num_regs_read += len;
	thresh = s_hw_type_defs[dev_data->hw_type].log_thresh;
	if ((dev_data->num_regs_read / thresh) >
	    ((dev_data->num_regs_read - len) / thresh))
		DP_VERBOSE(p_hwfn,
			   QED_MSG_DEBUG,
			   "Dumped %d registers...\n", dev_data->num_regs_read);

	return len;
}

/* Dumps GRC registers sequence header. Returns the dumped size in dwords.
 * The addr and len arguments are specified in dwords.
 */
static u32 qed_grc_dump_reg_entry_hdr(u32 *dump_buf,
				      bool dump, u32 addr, u32 len)
{
	if (dump)
		*dump_buf = addr | (len << REG_DUMP_LEN_SHIFT);

	return 1;
}

/* Dumps GRC registers sequence. Returns the dumped size in dwords.
 * The addr and len arguments are specified in dwords.
 */
static u32 qed_grc_dump_reg_entry(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  u32 *dump_buf,
				  bool dump, u32 addr, u32 len, bool wide_bus,
				  enum init_split_types split_type, u8 split_id)
{
	u32 offset = 0;

	offset += qed_grc_dump_reg_entry_hdr(dump_buf, dump, addr, len);
	offset += qed_grc_dump_addr_range(p_hwfn,
					  p_ptt,
					  dump_buf + offset,
					  dump, addr, len, wide_bus,
					  split_type, split_id);

	return offset;
}

/* Dumps GRC registers sequence with skip cycle.
 * Returns the dumped size in dwords.
 * - addr:	start GRC address in dwords
 * - total_len:	total no. of dwords to dump
 * - read_len:	no. consecutive dwords to read
 * - skip_len:	no. of dwords to skip (and fill with zeros)
 */
static u32 qed_grc_dump_reg_entry_skip(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt,
				       u32 *dump_buf,
				       bool dump,
				       u32 addr,
				       u32 total_len,
				       u32 read_len, u32 skip_len)
{
	u32 offset = 0, reg_offset = 0;

	offset += qed_grc_dump_reg_entry_hdr(dump_buf, dump, addr, total_len);

	if (!dump)
		return offset + total_len;

	while (reg_offset < total_len) {
		u32 curr_len = min_t(u32, read_len, total_len - reg_offset);

		offset += qed_grc_dump_addr_range(p_hwfn,
						  p_ptt,
						  dump_buf + offset,
						  dump,  addr, curr_len, false,
						  SPLIT_TYPE_NONE, 0);
		reg_offset += curr_len;
		addr += curr_len;

		if (reg_offset < total_len) {
			curr_len = min_t(u32, skip_len, total_len - skip_len);
			memset(dump_buf + offset, 0, DWORDS_TO_BYTES(curr_len));
			offset += curr_len;
			reg_offset += curr_len;
			addr += curr_len;
		}
	}

	return offset;
}

/* Dumps GRC registers entries. Returns the dumped size in dwords. */
static u32 qed_grc_dump_regs_entries(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct virt_mem_desc input_regs_arr,
				     u32 *dump_buf,
				     bool dump,
				     enum init_split_types split_type,
				     u8 split_id,
				     bool block_enable[MAX_BLOCK_ID],
				     u32 *num_dumped_reg_entries)
{
	u32 i, offset = 0, input_offset = 0;
	bool mode_match = true;

	*num_dumped_reg_entries = 0;

	while (input_offset < BYTES_TO_DWORDS(input_regs_arr.size)) {
		const struct dbg_dump_cond_hdr *cond_hdr =
		    (const struct dbg_dump_cond_hdr *)
		    input_regs_arr.ptr + input_offset++;
		u16 modes_buf_offset;
		bool eval_mode;

		/* Check mode/block */
		eval_mode = GET_FIELD(cond_hdr->mode.data,
				      DBG_MODE_HDR_EVAL_MODE) > 0;
		if (eval_mode) {
			modes_buf_offset =
				GET_FIELD(cond_hdr->mode.data,
					  DBG_MODE_HDR_MODES_BUF_OFFSET);
			mode_match = qed_is_mode_match(p_hwfn,
						       &modes_buf_offset);
		}

		if (!mode_match || !block_enable[cond_hdr->block_id]) {
			input_offset += cond_hdr->data_size;
			continue;
		}

		for (i = 0; i < cond_hdr->data_size; i++, input_offset++) {
			const struct dbg_dump_reg *reg =
			    (const struct dbg_dump_reg *)
			    input_regs_arr.ptr + input_offset;
			u32 addr, len;
			bool wide_bus;

			addr = GET_FIELD(reg->data, DBG_DUMP_REG_ADDRESS);
			len = GET_FIELD(reg->data, DBG_DUMP_REG_LENGTH);
			wide_bus = GET_FIELD(reg->data, DBG_DUMP_REG_WIDE_BUS);
			offset += qed_grc_dump_reg_entry(p_hwfn,
							 p_ptt,
							 dump_buf + offset,
							 dump,
							 addr,
							 len,
							 wide_bus,
							 split_type, split_id);
			(*num_dumped_reg_entries)++;
		}
	}

	return offset;
}

/* Dumps GRC registers entries. Returns the dumped size in dwords. */
static u32 qed_grc_dump_split_data(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   struct virt_mem_desc input_regs_arr,
				   u32 *dump_buf,
				   bool dump,
				   bool block_enable[MAX_BLOCK_ID],
				   enum init_split_types split_type,
				   u8 split_id, const char *reg_type_name)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	enum init_split_types hdr_split_type = split_type;
	u32 num_dumped_reg_entries, offset;
	u8 hdr_split_id = split_id;

	/* In PORT_PF split type, print a port split header */
	if (split_type == SPLIT_TYPE_PORT_PF) {
		hdr_split_type = SPLIT_TYPE_PORT;
		hdr_split_id = split_id / dev_data->num_pfs_per_port;
	}

	/* Calculate register dump header size (and skip it for now) */
	offset = qed_grc_dump_regs_hdr(dump_buf,
				       false,
				       0,
				       hdr_split_type,
				       hdr_split_id, reg_type_name);

	/* Dump registers */
	offset += qed_grc_dump_regs_entries(p_hwfn,
					    p_ptt,
					    input_regs_arr,
					    dump_buf + offset,
					    dump,
					    split_type,
					    split_id,
					    block_enable,
					    &num_dumped_reg_entries);

	/* Write register dump header */
	if (dump && num_dumped_reg_entries > 0)
		qed_grc_dump_regs_hdr(dump_buf,
				      dump,
				      num_dumped_reg_entries,
				      hdr_split_type,
				      hdr_split_id, reg_type_name);

	return num_dumped_reg_entries > 0 ? offset : 0;
}

/* Dumps registers according to the input registers array. Returns the dumped
 * size in dwords.
 */
static u32 qed_grc_dump_registers(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  u32 *dump_buf,
				  bool dump,
				  bool block_enable[MAX_BLOCK_ID],
				  const char *reg_type_name)
{
	struct virt_mem_desc *dbg_buf =
	    &p_hwfn->dbg_arrays[BIN_BUF_DBG_DUMP_REG];
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 offset = 0, input_offset = 0;

	while (input_offset < BYTES_TO_DWORDS(dbg_buf->size)) {
		const struct dbg_dump_split_hdr *split_hdr;
		struct virt_mem_desc curr_input_regs_arr;
		enum init_split_types split_type;
		u16 split_count = 0;
		u32 split_data_size;
		u8 split_id;

		split_hdr =
		    (const struct dbg_dump_split_hdr *)
		    dbg_buf->ptr + input_offset++;
		split_type =
		    GET_FIELD(split_hdr->hdr,
			      DBG_DUMP_SPLIT_HDR_SPLIT_TYPE_ID);
		split_data_size = GET_FIELD(split_hdr->hdr,
					    DBG_DUMP_SPLIT_HDR_DATA_SIZE);
		curr_input_regs_arr.ptr =
		    (u32 *)p_hwfn->dbg_arrays[BIN_BUF_DBG_DUMP_REG].ptr +
		    input_offset;
		curr_input_regs_arr.size = DWORDS_TO_BYTES(split_data_size);

		switch (split_type) {
		case SPLIT_TYPE_NONE:
			split_count = 1;
			break;
		case SPLIT_TYPE_PORT:
			split_count = dev_data->num_ports;
			break;
		case SPLIT_TYPE_PF:
		case SPLIT_TYPE_PORT_PF:
			split_count = dev_data->num_ports *
			    dev_data->num_pfs_per_port;
			break;
		case SPLIT_TYPE_VF:
			split_count = dev_data->num_vfs;
			break;
		default:
			return 0;
		}

		for (split_id = 0; split_id < split_count; split_id++)
			offset += qed_grc_dump_split_data(p_hwfn, p_ptt,
							  curr_input_regs_arr,
							  dump_buf + offset,
							  dump, block_enable,
							  split_type,
							  split_id,
							  reg_type_name);

		input_offset += split_data_size;
	}

	/* Cancel pretends (pretend to original PF) */
	if (dump) {
		qed_fid_pretend(p_hwfn, p_ptt,
				FIELD_VALUE(PXP_PRETEND_CONCRETE_FID_PFID,
					    p_hwfn->rel_pf_id));
		dev_data->pretend.split_type = SPLIT_TYPE_NONE;
		dev_data->pretend.split_id = 0;
	}

	return offset;
}

/* Dump reset registers. Returns the dumped size in dwords. */
static u32 qed_grc_dump_reset_regs(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   u32 *dump_buf, bool dump)
{
	u32 offset = 0, num_regs = 0;
	u8 reset_reg_id;

	/* Calculate header size */
	offset += qed_grc_dump_regs_hdr(dump_buf,
					false,
					0, SPLIT_TYPE_NONE, 0, "RESET_REGS");

	/* Write reset registers */
	for (reset_reg_id = 0; reset_reg_id < NUM_DBG_RESET_REGS;
	     reset_reg_id++) {
		const struct dbg_reset_reg *reset_reg;
		u32 reset_reg_addr;

		reset_reg = qed_get_dbg_reset_reg(p_hwfn, reset_reg_id);

		if (GET_FIELD(reset_reg->data, DBG_RESET_REG_IS_REMOVED))
			continue;

		reset_reg_addr = GET_FIELD(reset_reg->data, DBG_RESET_REG_ADDR);
		offset += qed_grc_dump_reg_entry(p_hwfn,
						 p_ptt,
						 dump_buf + offset,
						 dump,
						 reset_reg_addr,
						 1, false, SPLIT_TYPE_NONE, 0);
		num_regs++;
	}

	/* Write header */
	if (dump)
		qed_grc_dump_regs_hdr(dump_buf,
				      true, num_regs, SPLIT_TYPE_NONE,
				      0, "RESET_REGS");

	return offset;
}

/* Dump registers that are modified during GRC Dump and therefore must be
 * dumped first. Returns the dumped size in dwords.
 */
static u32 qed_grc_dump_modified_regs(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      u32 *dump_buf, bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 block_id, offset = 0, stall_regs_offset;
	const struct dbg_attn_reg *attn_reg_arr;
	u8 storm_id, reg_idx, num_attn_regs;
	u32 num_reg_entries = 0;

	/* Write empty header for attention registers */
	offset += qed_grc_dump_regs_hdr(dump_buf,
					false,
					0, SPLIT_TYPE_NONE, 0, "ATTN_REGS");

	/* Write parity registers */
	for (block_id = 0; block_id < NUM_PHYS_BLOCKS; block_id++) {
		if (dev_data->block_in_reset[block_id] && dump)
			continue;

		attn_reg_arr = qed_get_block_attn_regs(p_hwfn,
						       (enum block_id)block_id,
						       ATTN_TYPE_PARITY,
						       &num_attn_regs);

		for (reg_idx = 0; reg_idx < num_attn_regs; reg_idx++) {
			const struct dbg_attn_reg *reg_data =
				&attn_reg_arr[reg_idx];
			u16 modes_buf_offset;
			bool eval_mode;
			u32 addr;

			/* Check mode */
			eval_mode = GET_FIELD(reg_data->mode.data,
					      DBG_MODE_HDR_EVAL_MODE) > 0;
			modes_buf_offset =
				GET_FIELD(reg_data->mode.data,
					  DBG_MODE_HDR_MODES_BUF_OFFSET);
			if (eval_mode &&
			    !qed_is_mode_match(p_hwfn, &modes_buf_offset))
				continue;

			/* Mode match: read & dump registers */
			addr = reg_data->mask_address;
			offset += qed_grc_dump_reg_entry(p_hwfn,
							 p_ptt,
							 dump_buf + offset,
							 dump,
							 addr,
							 1, false,
							 SPLIT_TYPE_NONE, 0);
			addr = GET_FIELD(reg_data->data,
					 DBG_ATTN_REG_STS_ADDRESS);
			offset += qed_grc_dump_reg_entry(p_hwfn,
							 p_ptt,
							 dump_buf + offset,
							 dump,
							 addr,
							 1, false,
							 SPLIT_TYPE_NONE, 0);
			num_reg_entries += 2;
		}
	}

	/* Overwrite header for attention registers */
	if (dump)
		qed_grc_dump_regs_hdr(dump_buf,
				      true,
				      num_reg_entries,
				      SPLIT_TYPE_NONE, 0, "ATTN_REGS");

	/* Write empty header for stall registers */
	stall_regs_offset = offset;
	offset += qed_grc_dump_regs_hdr(dump_buf,
					false, 0, SPLIT_TYPE_NONE, 0, "REGS");

	/* Write Storm stall status registers */
	for (storm_id = 0, num_reg_entries = 0; storm_id < MAX_DBG_STORMS;
	     storm_id++) {
		struct storm_defs *storm = &s_storm_defs[storm_id];
		u32 addr;

		if (dev_data->block_in_reset[storm->sem_block_id] && dump)
			continue;

		addr =
		    BYTES_TO_DWORDS(storm->sem_fast_mem_addr +
				    SEM_FAST_REG_STALLED);
		offset += qed_grc_dump_reg_entry(p_hwfn,
						 p_ptt,
						 dump_buf + offset,
						 dump,
						 addr,
						 1,
						 false, SPLIT_TYPE_NONE, 0);
		num_reg_entries++;
	}

	/* Overwrite header for stall registers */
	if (dump)
		qed_grc_dump_regs_hdr(dump_buf + stall_regs_offset,
				      true,
				      num_reg_entries,
				      SPLIT_TYPE_NONE, 0, "REGS");

	return offset;
}

/* Dumps registers that can't be represented in the debug arrays */
static u32 qed_grc_dump_special_regs(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     u32 *dump_buf, bool dump)
{
	u32 offset = 0, addr;

	offset += qed_grc_dump_regs_hdr(dump_buf,
					dump, 2, SPLIT_TYPE_NONE, 0, "REGS");

	/* Dump R/TDIF_REG_DEBUG_ERROR_INFO_SIZE (every 8'th register should be
	 * skipped).
	 */
	addr = BYTES_TO_DWORDS(RDIF_REG_DEBUG_ERROR_INFO);
	offset += qed_grc_dump_reg_entry_skip(p_hwfn,
					      p_ptt,
					      dump_buf + offset,
					      dump,
					      addr,
					      RDIF_REG_DEBUG_ERROR_INFO_SIZE,
					      7,
					      1);
	addr = BYTES_TO_DWORDS(TDIF_REG_DEBUG_ERROR_INFO);
	offset +=
	    qed_grc_dump_reg_entry_skip(p_hwfn,
					p_ptt,
					dump_buf + offset,
					dump,
					addr,
					TDIF_REG_DEBUG_ERROR_INFO_SIZE,
					7,
					1);

	return offset;
}

/* Dumps a GRC memory header (section and params). Returns the dumped size in
 * dwords. The following parameters are dumped:
 * - name:	   dumped only if it's not NULL.
 * - addr:	   in dwords, dumped only if name is NULL.
 * - len:	   in dwords, always dumped.
 * - width:	   dumped if it's not zero.
 * - packed:	   dumped only if it's not false.
 * - mem_group:	   always dumped.
 * - is_storm:	   true only if the memory is related to a Storm.
 * - storm_letter: valid only if is_storm is true.
 *
 */
static u32 qed_grc_dump_mem_hdr(struct qed_hwfn *p_hwfn,
				u32 *dump_buf,
				bool dump,
				const char *name,
				u32 addr,
				u32 len,
				u32 bit_width,
				bool packed,
				const char *mem_group, char storm_letter)
{
	u8 num_params = 3;
	u32 offset = 0;
	char buf[64];

	if (!len)
		DP_NOTICE(p_hwfn,
			  "Unexpected GRC Dump error: dumped memory size must be non-zero\n");

	if (bit_width)
		num_params++;
	if (packed)
		num_params++;

	/* Dump section header */
	offset += qed_dump_section_hdr(dump_buf + offset,
				       dump, "grc_mem", num_params);

	if (name) {
		/* Dump name */
		if (storm_letter) {
			strcpy(buf, "?STORM_");
			buf[0] = storm_letter;
			strcpy(buf + strlen(buf), name);
		} else {
			strcpy(buf, name);
		}

		offset += qed_dump_str_param(dump_buf + offset,
					     dump, "name", buf);
	} else {
		/* Dump address */
		u32 addr_in_bytes = DWORDS_TO_BYTES(addr);

		offset += qed_dump_num_param(dump_buf + offset,
					     dump, "addr", addr_in_bytes);
	}

	/* Dump len */
	offset += qed_dump_num_param(dump_buf + offset, dump, "len", len);

	/* Dump bit width */
	if (bit_width)
		offset += qed_dump_num_param(dump_buf + offset,
					     dump, "width", bit_width);

	/* Dump packed */
	if (packed)
		offset += qed_dump_num_param(dump_buf + offset,
					     dump, "packed", 1);

	/* Dump reg type */
	if (storm_letter) {
		strcpy(buf, "?STORM_");
		buf[0] = storm_letter;
		strcpy(buf + strlen(buf), mem_group);
	} else {
		strcpy(buf, mem_group);
	}

	offset += qed_dump_str_param(dump_buf + offset, dump, "type", buf);

	return offset;
}

/* Dumps a single GRC memory. If name is NULL, the memory is stored by address.
 * Returns the dumped size in dwords.
 * The addr and len arguments are specified in dwords.
 */
static u32 qed_grc_dump_mem(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    u32 *dump_buf,
			    bool dump,
			    const char *name,
			    u32 addr,
			    u32 len,
			    bool wide_bus,
			    u32 bit_width,
			    bool packed,
			    const char *mem_group, char storm_letter)
{
	u32 offset = 0;

	offset += qed_grc_dump_mem_hdr(p_hwfn,
				       dump_buf + offset,
				       dump,
				       name,
				       addr,
				       len,
				       bit_width,
				       packed, mem_group, storm_letter);
	offset += qed_grc_dump_addr_range(p_hwfn,
					  p_ptt,
					  dump_buf + offset,
					  dump, addr, len, wide_bus,
					  SPLIT_TYPE_NONE, 0);

	return offset;
}

/* Dumps GRC memories entries. Returns the dumped size in dwords. */
static u32 qed_grc_dump_mem_entries(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    struct virt_mem_desc input_mems_arr,
				    u32 *dump_buf, bool dump)
{
	u32 i, offset = 0, input_offset = 0;
	bool mode_match = true;

	while (input_offset < BYTES_TO_DWORDS(input_mems_arr.size)) {
		const struct dbg_dump_cond_hdr *cond_hdr;
		u16 modes_buf_offset;
		u32 num_entries;
		bool eval_mode;

		cond_hdr =
		    (const struct dbg_dump_cond_hdr *)input_mems_arr.ptr +
		    input_offset++;
		num_entries = cond_hdr->data_size / MEM_DUMP_ENTRY_SIZE_DWORDS;

		/* Check required mode */
		eval_mode = GET_FIELD(cond_hdr->mode.data,
				      DBG_MODE_HDR_EVAL_MODE) > 0;
		if (eval_mode) {
			modes_buf_offset =
				GET_FIELD(cond_hdr->mode.data,
					  DBG_MODE_HDR_MODES_BUF_OFFSET);
			mode_match = qed_is_mode_match(p_hwfn,
						       &modes_buf_offset);
		}

		if (!mode_match) {
			input_offset += cond_hdr->data_size;
			continue;
		}

		for (i = 0; i < num_entries;
		     i++, input_offset += MEM_DUMP_ENTRY_SIZE_DWORDS) {
			const struct dbg_dump_mem *mem =
			    (const struct dbg_dump_mem *)((u32 *)
							  input_mems_arr.ptr
							  + input_offset);
			const struct dbg_block *block;
			char storm_letter = 0;
			u32 mem_addr, mem_len;
			bool mem_wide_bus;
			u8 mem_group_id;

			mem_group_id = GET_FIELD(mem->dword0,
						 DBG_DUMP_MEM_MEM_GROUP_ID);
			if (mem_group_id >= MEM_GROUPS_NUM) {
				DP_NOTICE(p_hwfn, "Invalid mem_group_id\n");
				return 0;
			}

			if (!qed_grc_is_mem_included(p_hwfn,
						     (enum block_id)
						     cond_hdr->block_id,
						     mem_group_id))
				continue;

			mem_addr = GET_FIELD(mem->dword0, DBG_DUMP_MEM_ADDRESS);
			mem_len = GET_FIELD(mem->dword1, DBG_DUMP_MEM_LENGTH);
			mem_wide_bus = GET_FIELD(mem->dword1,
						 DBG_DUMP_MEM_WIDE_BUS);

			block = get_dbg_block(p_hwfn,
					      cond_hdr->block_id);

			/* If memory is associated with Storm,
			 * update storm details
			 */
			if (block->associated_storm_letter)
				storm_letter = block->associated_storm_letter;

			/* Dump memory */
			offset += qed_grc_dump_mem(p_hwfn,
						p_ptt,
						dump_buf + offset,
						dump,
						NULL,
						mem_addr,
						mem_len,
						mem_wide_bus,
						0,
						false,
						s_mem_group_names[mem_group_id],
						storm_letter);
		}
	}

	return offset;
}

/* Dumps GRC memories according to the input array dump_mem.
 * Returns the dumped size in dwords.
 */
static u32 qed_grc_dump_memories(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 u32 *dump_buf, bool dump)
{
	struct virt_mem_desc *dbg_buf =
	    &p_hwfn->dbg_arrays[BIN_BUF_DBG_DUMP_MEM];
	u32 offset = 0, input_offset = 0;

	while (input_offset < BYTES_TO_DWORDS(dbg_buf->size)) {
		const struct dbg_dump_split_hdr *split_hdr;
		struct virt_mem_desc curr_input_mems_arr;
		enum init_split_types split_type;
		u32 split_data_size;

		split_hdr =
		    (const struct dbg_dump_split_hdr *)dbg_buf->ptr +
		    input_offset++;
		split_type = GET_FIELD(split_hdr->hdr,
				       DBG_DUMP_SPLIT_HDR_SPLIT_TYPE_ID);
		split_data_size = GET_FIELD(split_hdr->hdr,
					    DBG_DUMP_SPLIT_HDR_DATA_SIZE);
		curr_input_mems_arr.ptr = (u32 *)dbg_buf->ptr + input_offset;
		curr_input_mems_arr.size = DWORDS_TO_BYTES(split_data_size);

		if (split_type == SPLIT_TYPE_NONE)
			offset += qed_grc_dump_mem_entries(p_hwfn,
							   p_ptt,
							   curr_input_mems_arr,
							   dump_buf + offset,
							   dump);
		else
			DP_NOTICE(p_hwfn,
				  "Dumping split memories is currently not supported\n");

		input_offset += split_data_size;
	}

	return offset;
}

/* Dumps GRC context data for the specified Storm.
 * Returns the dumped size in dwords.
 * The lid_size argument is specified in quad-regs.
 */
static u32 qed_grc_dump_ctx_data(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 u32 *dump_buf,
				 bool dump,
				 const char *name,
				 u32 num_lids,
				 enum cm_ctx_types ctx_type, u8 storm_id)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct storm_defs *storm = &s_storm_defs[storm_id];
	u32 i, lid, lid_size, total_size;
	u32 rd_reg_addr, offset = 0;

	/* Convert quad-regs to dwords */
	lid_size = storm->cm_ctx_lid_sizes[dev_data->chip_id][ctx_type] * 4;

	if (!lid_size)
		return 0;

	total_size = num_lids * lid_size;

	offset += qed_grc_dump_mem_hdr(p_hwfn,
				       dump_buf + offset,
				       dump,
				       name,
				       0,
				       total_size,
				       lid_size * 32,
				       false, name, storm->letter);

	if (!dump)
		return offset + total_size;

	rd_reg_addr = BYTES_TO_DWORDS(storm->cm_ctx_rd_addr[ctx_type]);

	/* Dump context data */
	for (lid = 0; lid < num_lids; lid++) {
		for (i = 0; i < lid_size; i++) {
			qed_wr(p_hwfn,
			       p_ptt, storm->cm_ctx_wr_addr, (i << 9) | lid);
			offset += qed_grc_dump_addr_range(p_hwfn,
							  p_ptt,
							  dump_buf + offset,
							  dump,
							  rd_reg_addr,
							  1,
							  false,
							  SPLIT_TYPE_NONE, 0);
		}
	}

	return offset;
}

/* Dumps GRC contexts. Returns the dumped size in dwords. */
static u32 qed_grc_dump_ctx(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt, u32 *dump_buf, bool dump)
{
	u32 offset = 0;
	u8 storm_id;

	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		if (!qed_grc_is_storm_included(p_hwfn,
					       (enum dbg_storms)storm_id))
			continue;

		/* Dump Conn AG context size */
		offset += qed_grc_dump_ctx_data(p_hwfn,
						p_ptt,
						dump_buf + offset,
						dump,
						"CONN_AG_CTX",
						NUM_OF_LCIDS,
						CM_CTX_CONN_AG, storm_id);

		/* Dump Conn ST context size */
		offset += qed_grc_dump_ctx_data(p_hwfn,
						p_ptt,
						dump_buf + offset,
						dump,
						"CONN_ST_CTX",
						NUM_OF_LCIDS,
						CM_CTX_CONN_ST, storm_id);

		/* Dump Task AG context size */
		offset += qed_grc_dump_ctx_data(p_hwfn,
						p_ptt,
						dump_buf + offset,
						dump,
						"TASK_AG_CTX",
						NUM_OF_LTIDS,
						CM_CTX_TASK_AG, storm_id);

		/* Dump Task ST context size */
		offset += qed_grc_dump_ctx_data(p_hwfn,
						p_ptt,
						dump_buf + offset,
						dump,
						"TASK_ST_CTX",
						NUM_OF_LTIDS,
						CM_CTX_TASK_ST, storm_id);
	}

	return offset;
}

#define VFC_STATUS_RESP_READY_BIT	0
#define VFC_STATUS_BUSY_BIT		1
#define VFC_STATUS_SENDING_CMD_BIT	2

#define VFC_POLLING_DELAY_MS	1
#define VFC_POLLING_COUNT		20

/* Reads data from VFC. Returns the number of dwords read (0 on error).
 * Sizes are specified in dwords.
 */
static u32 qed_grc_dump_read_from_vfc(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      struct storm_defs *storm,
				      u32 *cmd_data,
				      u32 cmd_size,
				      u32 *addr_data,
				      u32 addr_size,
				      u32 resp_size, u32 *dump_buf)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 vfc_status, polling_ms, polling_count = 0, i;
	u32 reg_addr, sem_base;
	bool is_ready = false;

	sem_base = storm->sem_fast_mem_addr;
	polling_ms = VFC_POLLING_DELAY_MS *
	    s_hw_type_defs[dev_data->hw_type].delay_factor;

	/* Write VFC command */
	ARR_REG_WR(p_hwfn,
		   p_ptt,
		   sem_base + SEM_FAST_REG_VFC_DATA_WR,
		   cmd_data, cmd_size);

	/* Write VFC address */
	ARR_REG_WR(p_hwfn,
		   p_ptt,
		   sem_base + SEM_FAST_REG_VFC_ADDR,
		   addr_data, addr_size);

	/* Read response */
	for (i = 0; i < resp_size; i++) {
		/* Poll until ready */
		do {
			reg_addr = sem_base + SEM_FAST_REG_VFC_STATUS;
			qed_grc_dump_addr_range(p_hwfn,
						p_ptt,
						&vfc_status,
						true,
						BYTES_TO_DWORDS(reg_addr),
						1,
						false, SPLIT_TYPE_NONE, 0);
			is_ready = vfc_status & BIT(VFC_STATUS_RESP_READY_BIT);

			if (!is_ready) {
				if (polling_count++ == VFC_POLLING_COUNT)
					return 0;

				msleep(polling_ms);
			}
		} while (!is_ready);

		reg_addr = sem_base + SEM_FAST_REG_VFC_DATA_RD;
		qed_grc_dump_addr_range(p_hwfn,
					p_ptt,
					dump_buf + i,
					true,
					BYTES_TO_DWORDS(reg_addr),
					1, false, SPLIT_TYPE_NONE, 0);
	}

	return resp_size;
}

/* Dump VFC CAM. Returns the dumped size in dwords. */
static u32 qed_grc_dump_vfc_cam(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt,
				u32 *dump_buf, bool dump, u8 storm_id)
{
	u32 total_size = VFC_CAM_NUM_ROWS * VFC_CAM_RESP_DWORDS;
	struct storm_defs *storm = &s_storm_defs[storm_id];
	u32 cam_addr[VFC_CAM_ADDR_DWORDS] = { 0 };
	u32 cam_cmd[VFC_CAM_CMD_DWORDS] = { 0 };
	u32 row, offset = 0;

	offset += qed_grc_dump_mem_hdr(p_hwfn,
				       dump_buf + offset,
				       dump,
				       "vfc_cam",
				       0,
				       total_size,
				       256,
				       false, "vfc_cam", storm->letter);

	if (!dump)
		return offset + total_size;

	/* Prepare CAM address */
	SET_VAR_FIELD(cam_addr, VFC_CAM_ADDR, OP, VFC_OPCODE_CAM_RD);

	/* Read VFC CAM data */
	for (row = 0; row < VFC_CAM_NUM_ROWS; row++) {
		SET_VAR_FIELD(cam_cmd, VFC_CAM_CMD, ROW, row);
		offset += qed_grc_dump_read_from_vfc(p_hwfn,
						     p_ptt,
						     storm,
						     cam_cmd,
						     VFC_CAM_CMD_DWORDS,
						     cam_addr,
						     VFC_CAM_ADDR_DWORDS,
						     VFC_CAM_RESP_DWORDS,
						     dump_buf + offset);
	}

	return offset;
}

/* Dump VFC RAM. Returns the dumped size in dwords. */
static u32 qed_grc_dump_vfc_ram(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt,
				u32 *dump_buf,
				bool dump,
				u8 storm_id, struct vfc_ram_defs *ram_defs)
{
	u32 total_size = ram_defs->num_rows * VFC_RAM_RESP_DWORDS;
	struct storm_defs *storm = &s_storm_defs[storm_id];
	u32 ram_addr[VFC_RAM_ADDR_DWORDS] = { 0 };
	u32 ram_cmd[VFC_RAM_CMD_DWORDS] = { 0 };
	u32 row, offset = 0;

	offset += qed_grc_dump_mem_hdr(p_hwfn,
				       dump_buf + offset,
				       dump,
				       ram_defs->mem_name,
				       0,
				       total_size,
				       256,
				       false,
				       ram_defs->type_name,
				       storm->letter);

	if (!dump)
		return offset + total_size;

	/* Prepare RAM address */
	SET_VAR_FIELD(ram_addr, VFC_RAM_ADDR, OP, VFC_OPCODE_RAM_RD);

	/* Read VFC RAM data */
	for (row = ram_defs->base_row;
	     row < ram_defs->base_row + ram_defs->num_rows; row++) {
		SET_VAR_FIELD(ram_addr, VFC_RAM_ADDR, ROW, row);
		offset += qed_grc_dump_read_from_vfc(p_hwfn,
						     p_ptt,
						     storm,
						     ram_cmd,
						     VFC_RAM_CMD_DWORDS,
						     ram_addr,
						     VFC_RAM_ADDR_DWORDS,
						     VFC_RAM_RESP_DWORDS,
						     dump_buf + offset);
	}

	return offset;
}

/* Dumps GRC VFC data. Returns the dumped size in dwords. */
static u32 qed_grc_dump_vfc(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt, u32 *dump_buf, bool dump)
{
	u8 storm_id, i;
	u32 offset = 0;

	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		if (!qed_grc_is_storm_included(p_hwfn,
					       (enum dbg_storms)storm_id) ||
		    !s_storm_defs[storm_id].has_vfc)
			continue;

		/* Read CAM */
		offset += qed_grc_dump_vfc_cam(p_hwfn,
					       p_ptt,
					       dump_buf + offset,
					       dump, storm_id);

		/* Read RAM */
		for (i = 0; i < NUM_VFC_RAM_TYPES; i++)
			offset += qed_grc_dump_vfc_ram(p_hwfn,
						       p_ptt,
						       dump_buf + offset,
						       dump,
						       storm_id,
						       &s_vfc_ram_defs[i]);
	}

	return offset;
}

/* Dumps GRC RSS data. Returns the dumped size in dwords. */
static u32 qed_grc_dump_rss(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt, u32 *dump_buf, bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 offset = 0;
	u8 rss_mem_id;

	for (rss_mem_id = 0; rss_mem_id < NUM_RSS_MEM_TYPES; rss_mem_id++) {
		u32 rss_addr, num_entries, total_dwords;
		struct rss_mem_defs *rss_defs;
		u32 addr, num_dwords_to_read;
		bool packed;

		rss_defs = &s_rss_mem_defs[rss_mem_id];
		rss_addr = rss_defs->addr;
		num_entries = rss_defs->num_entries[dev_data->chip_id];
		total_dwords = (num_entries * rss_defs->entry_width) / 32;
		packed = (rss_defs->entry_width == 16);

		offset += qed_grc_dump_mem_hdr(p_hwfn,
					       dump_buf + offset,
					       dump,
					       rss_defs->mem_name,
					       0,
					       total_dwords,
					       rss_defs->entry_width,
					       packed,
					       rss_defs->type_name, 0);

		/* Dump RSS data */
		if (!dump) {
			offset += total_dwords;
			continue;
		}

		addr = BYTES_TO_DWORDS(RSS_REG_RSS_RAM_DATA);
		while (total_dwords) {
			num_dwords_to_read = min_t(u32,
						   RSS_REG_RSS_RAM_DATA_SIZE,
						   total_dwords);
			qed_wr(p_hwfn, p_ptt, RSS_REG_RSS_RAM_ADDR, rss_addr);
			offset += qed_grc_dump_addr_range(p_hwfn,
							  p_ptt,
							  dump_buf + offset,
							  dump,
							  addr,
							  num_dwords_to_read,
							  false,
							  SPLIT_TYPE_NONE, 0);
			total_dwords -= num_dwords_to_read;
			rss_addr++;
		}
	}

	return offset;
}

/* Dumps GRC Big RAM. Returns the dumped size in dwords. */
static u32 qed_grc_dump_big_ram(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt,
				u32 *dump_buf, bool dump, u8 big_ram_id)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 block_size, ram_size, offset = 0, reg_val, i;
	char mem_name[12] = "???_BIG_RAM";
	char type_name[8] = "???_RAM";
	struct big_ram_defs *big_ram;

	big_ram = &s_big_ram_defs[big_ram_id];
	ram_size = big_ram->ram_size[dev_data->chip_id];

	reg_val = qed_rd(p_hwfn, p_ptt, big_ram->is_256b_reg_addr);
	block_size = reg_val &
		     BIT(big_ram->is_256b_bit_offset[dev_data->chip_id]) ? 256
									 : 128;

	strncpy(type_name, big_ram->instance_name, BIG_RAM_NAME_LEN);
	strncpy(mem_name, big_ram->instance_name, BIG_RAM_NAME_LEN);

	/* Dump memory header */
	offset += qed_grc_dump_mem_hdr(p_hwfn,
				       dump_buf + offset,
				       dump,
				       mem_name,
				       0,
				       ram_size,
				       block_size * 8,
				       false, type_name, 0);

	/* Read and dump Big RAM data */
	if (!dump)
		return offset + ram_size;

	/* Dump Big RAM */
	for (i = 0; i < DIV_ROUND_UP(ram_size, BRB_REG_BIG_RAM_DATA_SIZE);
	     i++) {
		u32 addr, len;

		qed_wr(p_hwfn, p_ptt, big_ram->addr_reg_addr, i);
		addr = BYTES_TO_DWORDS(big_ram->data_reg_addr);
		len = BRB_REG_BIG_RAM_DATA_SIZE;
		offset += qed_grc_dump_addr_range(p_hwfn,
						  p_ptt,
						  dump_buf + offset,
						  dump,
						  addr,
						  len,
						  false, SPLIT_TYPE_NONE, 0);
	}

	return offset;
}

/* Dumps MCP scratchpad. Returns the dumped size in dwords. */
static u32 qed_grc_dump_mcp(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt, u32 *dump_buf, bool dump)
{
	bool block_enable[MAX_BLOCK_ID] = { 0 };
	u32 offset = 0, addr;
	bool halted = false;

	/* Halt MCP */
	if (dump && !qed_grc_get_param(p_hwfn, DBG_GRC_PARAM_NO_MCP)) {
		halted = !qed_mcp_halt(p_hwfn, p_ptt);
		if (!halted)
			DP_NOTICE(p_hwfn, "MCP halt failed!\n");
	}

	/* Dump MCP scratchpad */
	offset += qed_grc_dump_mem(p_hwfn,
				   p_ptt,
				   dump_buf + offset,
				   dump,
				   NULL,
				   BYTES_TO_DWORDS(MCP_REG_SCRATCH),
				   MCP_REG_SCRATCH_SIZE,
				   false, 0, false, "MCP", 0);

	/* Dump MCP cpu_reg_file */
	offset += qed_grc_dump_mem(p_hwfn,
				   p_ptt,
				   dump_buf + offset,
				   dump,
				   NULL,
				   BYTES_TO_DWORDS(MCP_REG_CPU_REG_FILE),
				   MCP_REG_CPU_REG_FILE_SIZE,
				   false, 0, false, "MCP", 0);

	/* Dump MCP registers */
	block_enable[BLOCK_MCP] = true;
	offset += qed_grc_dump_registers(p_hwfn,
					 p_ptt,
					 dump_buf + offset,
					 dump, block_enable, "MCP");

	/* Dump required non-MCP registers */
	offset += qed_grc_dump_regs_hdr(dump_buf + offset,
					dump, 1, SPLIT_TYPE_NONE, 0,
					"MCP");
	addr = BYTES_TO_DWORDS(MISC_REG_SHARED_MEM_ADDR);
	offset += qed_grc_dump_reg_entry(p_hwfn,
					 p_ptt,
					 dump_buf + offset,
					 dump,
					 addr,
					 1,
					 false, SPLIT_TYPE_NONE, 0);

	/* Release MCP */
	if (halted && qed_mcp_resume(p_hwfn, p_ptt))
		DP_NOTICE(p_hwfn, "Failed to resume MCP after halt!\n");

	return offset;
}

/* Dumps the tbus indirect memory for all PHYs.
 * Returns the dumped size in dwords.
 */
static u32 qed_grc_dump_phy(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt, u32 *dump_buf, bool dump)
{
	u32 offset = 0, tbus_lo_offset, tbus_hi_offset;
	char mem_name[32];
	u8 phy_id;

	for (phy_id = 0; phy_id < ARRAY_SIZE(s_phy_defs); phy_id++) {
		u32 addr_lo_addr, addr_hi_addr, data_lo_addr, data_hi_addr;
		struct phy_defs *phy_defs;
		u8 *bytes_buf;

		phy_defs = &s_phy_defs[phy_id];
		addr_lo_addr = phy_defs->base_addr +
			       phy_defs->tbus_addr_lo_addr;
		addr_hi_addr = phy_defs->base_addr +
			       phy_defs->tbus_addr_hi_addr;
		data_lo_addr = phy_defs->base_addr +
			       phy_defs->tbus_data_lo_addr;
		data_hi_addr = phy_defs->base_addr +
			       phy_defs->tbus_data_hi_addr;

		if (snprintf(mem_name, sizeof(mem_name), "tbus_%s",
			     phy_defs->phy_name) < 0)
			DP_NOTICE(p_hwfn,
				  "Unexpected debug error: invalid PHY memory name\n");

		offset += qed_grc_dump_mem_hdr(p_hwfn,
					       dump_buf + offset,
					       dump,
					       mem_name,
					       0,
					       PHY_DUMP_SIZE_DWORDS,
					       16, true, mem_name, 0);

		if (!dump) {
			offset += PHY_DUMP_SIZE_DWORDS;
			continue;
		}

		bytes_buf = (u8 *)(dump_buf + offset);
		for (tbus_hi_offset = 0;
		     tbus_hi_offset < (NUM_PHY_TBUS_ADDRESSES >> 8);
		     tbus_hi_offset++) {
			qed_wr(p_hwfn, p_ptt, addr_hi_addr, tbus_hi_offset);
			for (tbus_lo_offset = 0; tbus_lo_offset < 256;
			     tbus_lo_offset++) {
				qed_wr(p_hwfn,
				       p_ptt, addr_lo_addr, tbus_lo_offset);
				*(bytes_buf++) = (u8)qed_rd(p_hwfn,
							    p_ptt,
							    data_lo_addr);
				*(bytes_buf++) = (u8)qed_rd(p_hwfn,
							    p_ptt,
							    data_hi_addr);
			}
		}

		offset += PHY_DUMP_SIZE_DWORDS;
	}

	return offset;
}

/* Dumps the MCP HW dump from NVRAM. Returns the dumped size in dwords. */
static u32 qed_grc_dump_mcp_hw_dump(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    u32 *dump_buf, bool dump)
{
	u32 hw_dump_offset_bytes = 0, hw_dump_size_bytes = 0;
	u32 hw_dump_size_dwords = 0, offset = 0;
	enum dbg_status status;

	/* Read HW dump image from NVRAM */
	status = qed_find_nvram_image(p_hwfn,
				      p_ptt,
				      NVM_TYPE_HW_DUMP_OUT,
				      &hw_dump_offset_bytes,
				      &hw_dump_size_bytes);
	if (status != DBG_STATUS_OK)
		return 0;

	hw_dump_size_dwords = BYTES_TO_DWORDS(hw_dump_size_bytes);

	/* Dump HW dump image section */
	offset += qed_dump_section_hdr(dump_buf + offset,
				       dump, "mcp_hw_dump", 1);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump, "size", hw_dump_size_dwords);

	/* Read MCP HW dump image into dump buffer */
	if (dump && hw_dump_size_dwords) {
		status = qed_nvram_read(p_hwfn,
					p_ptt,
					hw_dump_offset_bytes,
					hw_dump_size_bytes, dump_buf + offset);
		if (status != DBG_STATUS_OK) {
			DP_NOTICE(p_hwfn,
				  "Failed to read MCP HW Dump image from NVRAM\n");
			return 0;
		}
	}
	offset += hw_dump_size_dwords;

	return offset;
}

/* Dumps Static Debug data. Returns the dumped size in dwords. */
static u32 qed_grc_dump_static_debug(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     u32 *dump_buf, bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 block_id, line_id, offset = 0, addr, len;

	/* Don't dump static debug if a debug bus recording is in progress */
	if (dump && qed_rd(p_hwfn, p_ptt, DBG_REG_DBG_BLOCK_ON))
		return 0;

	if (dump) {
		/* Disable debug bus in all blocks */
		qed_bus_disable_blocks(p_hwfn, p_ptt);

		qed_bus_reset_dbg_block(p_hwfn, p_ptt);
		qed_wr(p_hwfn,
		       p_ptt, DBG_REG_FRAMING_MODE, DBG_BUS_FRAME_MODE_8HW);
		qed_wr(p_hwfn,
		       p_ptt, DBG_REG_DEBUG_TARGET, DBG_BUS_TARGET_ID_INT_BUF);
		qed_wr(p_hwfn, p_ptt, DBG_REG_FULL_MODE, 1);
		qed_bus_enable_dbg_block(p_hwfn, p_ptt, true);
	}

	/* Dump all static debug lines for each relevant block */
	for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
		const struct dbg_block_chip *block_per_chip;
		const struct dbg_block *block;
		bool is_removed, has_dbg_bus;
		u16 modes_buf_offset;
		u32 block_dwords;

		block_per_chip =
		    qed_get_dbg_block_per_chip(p_hwfn, (enum block_id)block_id);
		is_removed = GET_FIELD(block_per_chip->flags,
				       DBG_BLOCK_CHIP_IS_REMOVED);
		has_dbg_bus = GET_FIELD(block_per_chip->flags,
					DBG_BLOCK_CHIP_HAS_DBG_BUS);

		if (!is_removed && has_dbg_bus &&
		    GET_FIELD(block_per_chip->dbg_bus_mode.data,
			      DBG_MODE_HDR_EVAL_MODE) > 0) {
			modes_buf_offset =
			    GET_FIELD(block_per_chip->dbg_bus_mode.data,
				      DBG_MODE_HDR_MODES_BUF_OFFSET);
			if (!qed_is_mode_match(p_hwfn, &modes_buf_offset))
				has_dbg_bus = false;
		}

		if (is_removed || !has_dbg_bus)
			continue;

		block_dwords = NUM_DBG_LINES(block_per_chip) *
			       STATIC_DEBUG_LINE_DWORDS;

		/* Dump static section params */
		block = get_dbg_block(p_hwfn, (enum block_id)block_id);
		offset += qed_grc_dump_mem_hdr(p_hwfn,
					       dump_buf + offset,
					       dump,
					       block->name,
					       0,
					       block_dwords,
					       32, false, "STATIC", 0);

		if (!dump) {
			offset += block_dwords;
			continue;
		}

		/* If all lines are invalid - dump zeros */
		if (dev_data->block_in_reset[block_id]) {
			memset(dump_buf + offset, 0,
			       DWORDS_TO_BYTES(block_dwords));
			offset += block_dwords;
			continue;
		}

		/* Enable block's client */
		qed_bus_enable_clients(p_hwfn,
				       p_ptt,
				       BIT(block_per_chip->dbg_client_id));

		addr = BYTES_TO_DWORDS(DBG_REG_CALENDAR_OUT_DATA);
		len = STATIC_DEBUG_LINE_DWORDS;
		for (line_id = 0; line_id < (u32)NUM_DBG_LINES(block_per_chip);
		     line_id++) {
			/* Configure debug line ID */
			qed_bus_config_dbg_line(p_hwfn,
						p_ptt,
						(enum block_id)block_id,
						(u8)line_id, 0xf, 0, 0, 0);

			/* Read debug line info */
			offset += qed_grc_dump_addr_range(p_hwfn,
							  p_ptt,
							  dump_buf + offset,
							  dump,
							  addr,
							  len,
							  true, SPLIT_TYPE_NONE,
							  0);
		}

		/* Disable block's client and debug output */
		qed_bus_enable_clients(p_hwfn, p_ptt, 0);
		qed_bus_config_dbg_line(p_hwfn, p_ptt,
					(enum block_id)block_id, 0, 0, 0, 0, 0);
	}

	if (dump) {
		qed_bus_enable_dbg_block(p_hwfn, p_ptt, false);
		qed_bus_enable_clients(p_hwfn, p_ptt, 0);
	}

	return offset;
}

/* Performs GRC Dump to the specified buffer.
 * Returns the dumped size in dwords.
 */
static enum dbg_status qed_grc_dump(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    u32 *dump_buf,
				    bool dump, u32 *num_dumped_dwords)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	bool parities_masked = false;
	u32 dwords_read, offset = 0;
	u8 i;

	*num_dumped_dwords = 0;
	dev_data->num_regs_read = 0;

	/* Update reset state */
	if (dump)
		qed_update_blocks_reset_state(p_hwfn, p_ptt);

	/* Dump global params */
	offset += qed_dump_common_global_params(p_hwfn,
						p_ptt,
						dump_buf + offset, dump, 4);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "dump-type", "grc-dump");
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "num-lcids",
				     NUM_OF_LCIDS);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "num-ltids",
				     NUM_OF_LTIDS);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump, "num-ports", dev_data->num_ports);

	/* Dump reset registers (dumped before taking blocks out of reset ) */
	if (qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_REGS))
		offset += qed_grc_dump_reset_regs(p_hwfn,
						  p_ptt,
						  dump_buf + offset, dump);

	/* Take all blocks out of reset (using reset registers) */
	if (dump) {
		qed_grc_unreset_blocks(p_hwfn, p_ptt, false);
		qed_update_blocks_reset_state(p_hwfn, p_ptt);
	}

	/* Disable all parities using MFW command */
	if (dump &&
	    !qed_grc_get_param(p_hwfn, DBG_GRC_PARAM_NO_MCP)) {
		parities_masked = !qed_mcp_mask_parities(p_hwfn, p_ptt, 1);
		if (!parities_masked) {
			DP_NOTICE(p_hwfn,
				  "Failed to mask parities using MFW\n");
			if (qed_grc_get_param
			    (p_hwfn, DBG_GRC_PARAM_PARITY_SAFE))
				return DBG_STATUS_MCP_COULD_NOT_MASK_PRTY;
		}
	}

	/* Dump modified registers (dumped before modifying them) */
	if (qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_REGS))
		offset += qed_grc_dump_modified_regs(p_hwfn,
						     p_ptt,
						     dump_buf + offset, dump);

	/* Stall storms */
	if (dump &&
	    (qed_grc_is_included(p_hwfn,
				 DBG_GRC_PARAM_DUMP_IOR) ||
	     qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_VFC)))
		qed_grc_stall_storms(p_hwfn, p_ptt, true);

	/* Dump all regs  */
	if (qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_REGS)) {
		bool block_enable[MAX_BLOCK_ID];

		/* Dump all blocks except MCP */
		for (i = 0; i < MAX_BLOCK_ID; i++)
			block_enable[i] = true;
		block_enable[BLOCK_MCP] = false;
		offset += qed_grc_dump_registers(p_hwfn,
						 p_ptt,
						 dump_buf +
						 offset,
						 dump,
						 block_enable, NULL);

		/* Dump special registers */
		offset += qed_grc_dump_special_regs(p_hwfn,
						    p_ptt,
						    dump_buf + offset, dump);
	}

	/* Dump memories */
	offset += qed_grc_dump_memories(p_hwfn, p_ptt, dump_buf + offset, dump);

	/* Dump MCP */
	if (qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_MCP))
		offset += qed_grc_dump_mcp(p_hwfn,
					   p_ptt, dump_buf + offset, dump);

	/* Dump context */
	if (qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_CM_CTX))
		offset += qed_grc_dump_ctx(p_hwfn,
					   p_ptt, dump_buf + offset, dump);

	/* Dump RSS memories */
	if (qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_RSS))
		offset += qed_grc_dump_rss(p_hwfn,
					   p_ptt, dump_buf + offset, dump);

	/* Dump Big RAM */
	for (i = 0; i < NUM_BIG_RAM_TYPES; i++)
		if (qed_grc_is_included(p_hwfn, s_big_ram_defs[i].grc_param))
			offset += qed_grc_dump_big_ram(p_hwfn,
						       p_ptt,
						       dump_buf + offset,
						       dump, i);

	/* Dump VFC */
	if (qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_VFC)) {
		dwords_read = qed_grc_dump_vfc(p_hwfn,
					       p_ptt, dump_buf + offset, dump);
		offset += dwords_read;
		if (!dwords_read)
			return DBG_STATUS_VFC_READ_ERROR;
	}

	/* Dump PHY tbus */
	if (qed_grc_is_included(p_hwfn,
				DBG_GRC_PARAM_DUMP_PHY) && dev_data->chip_id ==
	    CHIP_K2 && dev_data->hw_type == HW_TYPE_ASIC)
		offset += qed_grc_dump_phy(p_hwfn,
					   p_ptt, dump_buf + offset, dump);

	/* Dump MCP HW Dump */
	if (qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_MCP_HW_DUMP) &&
	    !qed_grc_get_param(p_hwfn, DBG_GRC_PARAM_NO_MCP) && 1)
		offset += qed_grc_dump_mcp_hw_dump(p_hwfn,
						   p_ptt,
						   dump_buf + offset, dump);

	/* Dump static debug data (only if not during debug bus recording) */
	if (qed_grc_is_included(p_hwfn,
				DBG_GRC_PARAM_DUMP_STATIC) &&
	    (!dump || dev_data->bus.state == DBG_BUS_STATE_IDLE))
		offset += qed_grc_dump_static_debug(p_hwfn,
						    p_ptt,
						    dump_buf + offset, dump);

	/* Dump last section */
	offset += qed_dump_last_section(dump_buf, offset, dump);

	if (dump) {
		/* Unstall storms */
		if (qed_grc_get_param(p_hwfn, DBG_GRC_PARAM_UNSTALL))
			qed_grc_stall_storms(p_hwfn, p_ptt, false);

		/* Clear parity status */
		qed_grc_clear_all_prty(p_hwfn, p_ptt);

		/* Enable all parities using MFW command */
		if (parities_masked)
			qed_mcp_mask_parities(p_hwfn, p_ptt, 0);
	}

	*num_dumped_dwords = offset;

	return DBG_STATUS_OK;
}

/* Writes the specified failing Idle Check rule to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 qed_idle_chk_dump_failure(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     u32 *dump_buf,
				     bool dump,
				     u16 rule_id,
				     const struct dbg_idle_chk_rule *rule,
				     u16 fail_entry_id, u32 *cond_reg_values)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	const struct dbg_idle_chk_cond_reg *cond_regs;
	const struct dbg_idle_chk_info_reg *info_regs;
	u32 i, next_reg_offset = 0, offset = 0;
	struct dbg_idle_chk_result_hdr *hdr;
	const union dbg_idle_chk_reg *regs;
	u8 reg_id;

	hdr = (struct dbg_idle_chk_result_hdr *)dump_buf;
	regs = (const union dbg_idle_chk_reg *)
		p_hwfn->dbg_arrays[BIN_BUF_DBG_IDLE_CHK_REGS].ptr +
		rule->reg_offset;
	cond_regs = &regs[0].cond_reg;
	info_regs = &regs[rule->num_cond_regs].info_reg;

	/* Dump rule data */
	if (dump) {
		memset(hdr, 0, sizeof(*hdr));
		hdr->rule_id = rule_id;
		hdr->mem_entry_id = fail_entry_id;
		hdr->severity = rule->severity;
		hdr->num_dumped_cond_regs = rule->num_cond_regs;
	}

	offset += IDLE_CHK_RESULT_HDR_DWORDS;

	/* Dump condition register values */
	for (reg_id = 0; reg_id < rule->num_cond_regs; reg_id++) {
		const struct dbg_idle_chk_cond_reg *reg = &cond_regs[reg_id];
		struct dbg_idle_chk_result_reg_hdr *reg_hdr;

		reg_hdr =
		    (struct dbg_idle_chk_result_reg_hdr *)(dump_buf + offset);

		/* Write register header */
		if (!dump) {
			offset += IDLE_CHK_RESULT_REG_HDR_DWORDS +
			    reg->entry_size;
			continue;
		}

		offset += IDLE_CHK_RESULT_REG_HDR_DWORDS;
		memset(reg_hdr, 0, sizeof(*reg_hdr));
		reg_hdr->start_entry = reg->start_entry;
		reg_hdr->size = reg->entry_size;
		SET_FIELD(reg_hdr->data,
			  DBG_IDLE_CHK_RESULT_REG_HDR_IS_MEM,
			  reg->num_entries > 1 || reg->start_entry > 0 ? 1 : 0);
		SET_FIELD(reg_hdr->data,
			  DBG_IDLE_CHK_RESULT_REG_HDR_REG_ID, reg_id);

		/* Write register values */
		for (i = 0; i < reg_hdr->size; i++, next_reg_offset++, offset++)
			dump_buf[offset] = cond_reg_values[next_reg_offset];
	}

	/* Dump info register values */
	for (reg_id = 0; reg_id < rule->num_info_regs; reg_id++) {
		const struct dbg_idle_chk_info_reg *reg = &info_regs[reg_id];
		u32 block_id;

		/* Check if register's block is in reset */
		if (!dump) {
			offset += IDLE_CHK_RESULT_REG_HDR_DWORDS + reg->size;
			continue;
		}

		block_id = GET_FIELD(reg->data, DBG_IDLE_CHK_INFO_REG_BLOCK_ID);
		if (block_id >= MAX_BLOCK_ID) {
			DP_NOTICE(p_hwfn, "Invalid block_id\n");
			return 0;
		}

		if (!dev_data->block_in_reset[block_id]) {
			struct dbg_idle_chk_result_reg_hdr *reg_hdr;
			bool wide_bus, eval_mode, mode_match = true;
			u16 modes_buf_offset;
			u32 addr;

			reg_hdr = (struct dbg_idle_chk_result_reg_hdr *)
				  (dump_buf + offset);

			/* Check mode */
			eval_mode = GET_FIELD(reg->mode.data,
					      DBG_MODE_HDR_EVAL_MODE) > 0;
			if (eval_mode) {
				modes_buf_offset =
				    GET_FIELD(reg->mode.data,
					      DBG_MODE_HDR_MODES_BUF_OFFSET);
				mode_match =
					qed_is_mode_match(p_hwfn,
							  &modes_buf_offset);
			}

			if (!mode_match)
				continue;

			addr = GET_FIELD(reg->data,
					 DBG_IDLE_CHK_INFO_REG_ADDRESS);
			wide_bus = GET_FIELD(reg->data,
					     DBG_IDLE_CHK_INFO_REG_WIDE_BUS);

			/* Write register header */
			offset += IDLE_CHK_RESULT_REG_HDR_DWORDS;
			hdr->num_dumped_info_regs++;
			memset(reg_hdr, 0, sizeof(*reg_hdr));
			reg_hdr->size = reg->size;
			SET_FIELD(reg_hdr->data,
				  DBG_IDLE_CHK_RESULT_REG_HDR_REG_ID,
				  rule->num_cond_regs + reg_id);

			/* Write register values */
			offset += qed_grc_dump_addr_range(p_hwfn,
							  p_ptt,
							  dump_buf + offset,
							  dump,
							  addr,
							  reg->size, wide_bus,
							  SPLIT_TYPE_NONE, 0);
		}
	}

	return offset;
}

/* Dumps idle check rule entries. Returns the dumped size in dwords. */
static u32
qed_idle_chk_dump_rule_entries(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			       u32 *dump_buf, bool dump,
			       const struct dbg_idle_chk_rule *input_rules,
			       u32 num_input_rules, u32 *num_failing_rules)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 cond_reg_values[IDLE_CHK_MAX_ENTRIES_SIZE];
	u32 i, offset = 0;
	u16 entry_id;
	u8 reg_id;

	*num_failing_rules = 0;

	for (i = 0; i < num_input_rules; i++) {
		const struct dbg_idle_chk_cond_reg *cond_regs;
		const struct dbg_idle_chk_rule *rule;
		const union dbg_idle_chk_reg *regs;
		u16 num_reg_entries = 1;
		bool check_rule = true;
		const u32 *imm_values;

		rule = &input_rules[i];
		regs = (const union dbg_idle_chk_reg *)
			p_hwfn->dbg_arrays[BIN_BUF_DBG_IDLE_CHK_REGS].ptr +
			rule->reg_offset;
		cond_regs = &regs[0].cond_reg;
		imm_values =
		    (u32 *)p_hwfn->dbg_arrays[BIN_BUF_DBG_IDLE_CHK_IMMS].ptr +
		    rule->imm_offset;

		/* Check if all condition register blocks are out of reset, and
		 * find maximal number of entries (all condition registers that
		 * are memories must have the same size, which is > 1).
		 */
		for (reg_id = 0; reg_id < rule->num_cond_regs && check_rule;
		     reg_id++) {
			u32 block_id =
				GET_FIELD(cond_regs[reg_id].data,
					  DBG_IDLE_CHK_COND_REG_BLOCK_ID);

			if (block_id >= MAX_BLOCK_ID) {
				DP_NOTICE(p_hwfn, "Invalid block_id\n");
				return 0;
			}

			check_rule = !dev_data->block_in_reset[block_id];
			if (cond_regs[reg_id].num_entries > num_reg_entries)
				num_reg_entries = cond_regs[reg_id].num_entries;
		}

		if (!check_rule && dump)
			continue;

		if (!dump) {
			u32 entry_dump_size =
				qed_idle_chk_dump_failure(p_hwfn,
							  p_ptt,
							  dump_buf + offset,
							  false,
							  rule->rule_id,
							  rule,
							  0,
							  NULL);

			offset += num_reg_entries * entry_dump_size;
			(*num_failing_rules) += num_reg_entries;
			continue;
		}

		/* Go over all register entries (number of entries is the same
		 * for all condition registers).
		 */
		for (entry_id = 0; entry_id < num_reg_entries; entry_id++) {
			u32 next_reg_offset = 0;

			/* Read current entry of all condition registers */
			for (reg_id = 0; reg_id < rule->num_cond_regs;
			     reg_id++) {
				const struct dbg_idle_chk_cond_reg *reg =
					&cond_regs[reg_id];
				u32 padded_entry_size, addr;
				bool wide_bus;

				/* Find GRC address (if it's a memory, the
				 * address of the specific entry is calculated).
				 */
				addr = GET_FIELD(reg->data,
						 DBG_IDLE_CHK_COND_REG_ADDRESS);
				wide_bus =
				    GET_FIELD(reg->data,
					      DBG_IDLE_CHK_COND_REG_WIDE_BUS);
				if (reg->num_entries > 1 ||
				    reg->start_entry > 0) {
					padded_entry_size =
					   reg->entry_size > 1 ?
					   roundup_pow_of_two(reg->entry_size) :
					   1;
					addr += (reg->start_entry + entry_id) *
						padded_entry_size;
				}

				/* Read registers */
				if (next_reg_offset + reg->entry_size >=
				    IDLE_CHK_MAX_ENTRIES_SIZE) {
					DP_NOTICE(p_hwfn,
						  "idle check registers entry is too large\n");
					return 0;
				}

				next_reg_offset +=
				    qed_grc_dump_addr_range(p_hwfn, p_ptt,
							    cond_reg_values +
							    next_reg_offset,
							    dump, addr,
							    reg->entry_size,
							    wide_bus,
							    SPLIT_TYPE_NONE, 0);
			}

			/* Call rule condition function.
			 * If returns true, it's a failure.
			 */
			if ((*cond_arr[rule->cond_id]) (cond_reg_values,
							imm_values)) {
				offset += qed_idle_chk_dump_failure(p_hwfn,
							p_ptt,
							dump_buf + offset,
							dump,
							rule->rule_id,
							rule,
							entry_id,
							cond_reg_values);
				(*num_failing_rules)++;
			}
		}
	}

	return offset;
}

/* Performs Idle Check Dump to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 qed_idle_chk_dump(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt, u32 *dump_buf, bool dump)
{
	struct virt_mem_desc *dbg_buf =
	    &p_hwfn->dbg_arrays[BIN_BUF_DBG_IDLE_CHK_RULES];
	u32 num_failing_rules_offset, offset = 0,
	    input_offset = 0, num_failing_rules = 0;

	/* Dump global params  - 1 must match below amount of params */
	offset += qed_dump_common_global_params(p_hwfn,
						p_ptt,
						dump_buf + offset, dump, 1);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "dump-type", "idle-chk");

	/* Dump idle check section header with a single parameter */
	offset += qed_dump_section_hdr(dump_buf + offset, dump, "idle_chk", 1);
	num_failing_rules_offset = offset;
	offset += qed_dump_num_param(dump_buf + offset, dump, "num_rules", 0);

	while (input_offset < BYTES_TO_DWORDS(dbg_buf->size)) {
		const struct dbg_idle_chk_cond_hdr *cond_hdr =
		    (const struct dbg_idle_chk_cond_hdr *)dbg_buf->ptr +
		    input_offset++;
		bool eval_mode, mode_match = true;
		u32 curr_failing_rules;
		u16 modes_buf_offset;

		/* Check mode */
		eval_mode = GET_FIELD(cond_hdr->mode.data,
				      DBG_MODE_HDR_EVAL_MODE) > 0;
		if (eval_mode) {
			modes_buf_offset =
				GET_FIELD(cond_hdr->mode.data,
					  DBG_MODE_HDR_MODES_BUF_OFFSET);
			mode_match = qed_is_mode_match(p_hwfn,
						       &modes_buf_offset);
		}

		if (mode_match) {
			const struct dbg_idle_chk_rule *rule =
			    (const struct dbg_idle_chk_rule *)((u32 *)
							       dbg_buf->ptr
							       + input_offset);
			u32 num_input_rules =
				cond_hdr->data_size / IDLE_CHK_RULE_SIZE_DWORDS;
			offset +=
			    qed_idle_chk_dump_rule_entries(p_hwfn,
							   p_ptt,
							   dump_buf +
							   offset,
							   dump,
							   rule,
							   num_input_rules,
							   &curr_failing_rules);
			num_failing_rules += curr_failing_rules;
		}

		input_offset += cond_hdr->data_size;
	}

	/* Overwrite num_rules parameter */
	if (dump)
		qed_dump_num_param(dump_buf + num_failing_rules_offset,
				   dump, "num_rules", num_failing_rules);

	/* Dump last section */
	offset += qed_dump_last_section(dump_buf, offset, dump);

	return offset;
}

/* Get info on the MCP Trace data in the scratchpad:
 * - trace_data_grc_addr (OUT): trace data GRC address in bytes
 * - trace_data_size (OUT): trace data size in bytes (without the header)
 */
static enum dbg_status qed_mcp_trace_get_data_info(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 *trace_data_grc_addr,
						   u32 *trace_data_size)
{
	u32 spad_trace_offsize, signature;

	/* Read trace section offsize structure from MCP scratchpad */
	spad_trace_offsize = qed_rd(p_hwfn, p_ptt, MCP_SPAD_TRACE_OFFSIZE_ADDR);

	/* Extract trace section address from offsize (in scratchpad) */
	*trace_data_grc_addr =
		MCP_REG_SCRATCH + SECTION_OFFSET(spad_trace_offsize);

	/* Read signature from MCP trace section */
	signature = qed_rd(p_hwfn, p_ptt,
			   *trace_data_grc_addr +
			   offsetof(struct mcp_trace, signature));

	if (signature != MFW_TRACE_SIGNATURE)
		return DBG_STATUS_INVALID_TRACE_SIGNATURE;

	/* Read trace size from MCP trace section */
	*trace_data_size = qed_rd(p_hwfn,
				  p_ptt,
				  *trace_data_grc_addr +
				  offsetof(struct mcp_trace, size));

	return DBG_STATUS_OK;
}

/* Reads MCP trace meta data image from NVRAM
 * - running_bundle_id (OUT): running bundle ID (invalid when loaded from file)
 * - trace_meta_offset (OUT): trace meta offset in NVRAM in bytes (invalid when
 *			      loaded from file).
 * - trace_meta_size (OUT):   size in bytes of the trace meta data.
 */
static enum dbg_status qed_mcp_trace_get_meta_info(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 trace_data_size_bytes,
						   u32 *running_bundle_id,
						   u32 *trace_meta_offset,
						   u32 *trace_meta_size)
{
	u32 spad_trace_offsize, nvram_image_type, running_mfw_addr;

	/* Read MCP trace section offsize structure from MCP scratchpad */
	spad_trace_offsize = qed_rd(p_hwfn, p_ptt, MCP_SPAD_TRACE_OFFSIZE_ADDR);

	/* Find running bundle ID */
	running_mfw_addr =
		MCP_REG_SCRATCH + SECTION_OFFSET(spad_trace_offsize) +
		QED_SECTION_SIZE(spad_trace_offsize) + trace_data_size_bytes;
	*running_bundle_id = qed_rd(p_hwfn, p_ptt, running_mfw_addr);
	if (*running_bundle_id > 1)
		return DBG_STATUS_INVALID_NVRAM_BUNDLE;

	/* Find image in NVRAM */
	nvram_image_type =
	    (*running_bundle_id ==
	     DIR_ID_1) ? NVM_TYPE_MFW_TRACE1 : NVM_TYPE_MFW_TRACE2;
	return qed_find_nvram_image(p_hwfn,
				    p_ptt,
				    nvram_image_type,
				    trace_meta_offset, trace_meta_size);
}

/* Reads the MCP Trace meta data from NVRAM into the specified buffer */
static enum dbg_status qed_mcp_trace_read_meta(struct qed_hwfn *p_hwfn,
					       struct qed_ptt *p_ptt,
					       u32 nvram_offset_in_bytes,
					       u32 size_in_bytes, u32 *buf)
{
	u8 modules_num, module_len, i, *byte_buf = (u8 *)buf;
	enum dbg_status status;
	u32 signature;

	/* Read meta data from NVRAM */
	status = qed_nvram_read(p_hwfn,
				p_ptt,
				nvram_offset_in_bytes, size_in_bytes, buf);
	if (status != DBG_STATUS_OK)
		return status;

	/* Extract and check first signature */
	signature = qed_read_unaligned_dword(byte_buf);
	byte_buf += sizeof(signature);
	if (signature != NVM_MAGIC_VALUE)
		return DBG_STATUS_INVALID_TRACE_SIGNATURE;

	/* Extract number of modules */
	modules_num = *(byte_buf++);

	/* Skip all modules */
	for (i = 0; i < modules_num; i++) {
		module_len = *(byte_buf++);
		byte_buf += module_len;
	}

	/* Extract and check second signature */
	signature = qed_read_unaligned_dword(byte_buf);
	byte_buf += sizeof(signature);
	if (signature != NVM_MAGIC_VALUE)
		return DBG_STATUS_INVALID_TRACE_SIGNATURE;

	return DBG_STATUS_OK;
}

/* Dump MCP Trace */
static enum dbg_status qed_mcp_trace_dump(struct qed_hwfn *p_hwfn,
					  struct qed_ptt *p_ptt,
					  u32 *dump_buf,
					  bool dump, u32 *num_dumped_dwords)
{
	u32 trace_data_grc_addr, trace_data_size_bytes, trace_data_size_dwords;
	u32 trace_meta_size_dwords = 0, running_bundle_id, offset = 0;
	u32 trace_meta_offset_bytes = 0, trace_meta_size_bytes = 0;
	enum dbg_status status;
	int halted = 0;
	bool use_mfw;

	*num_dumped_dwords = 0;

	use_mfw = !qed_grc_get_param(p_hwfn, DBG_GRC_PARAM_NO_MCP);

	/* Get trace data info */
	status = qed_mcp_trace_get_data_info(p_hwfn,
					     p_ptt,
					     &trace_data_grc_addr,
					     &trace_data_size_bytes);
	if (status != DBG_STATUS_OK)
		return status;

	/* Dump global params */
	offset += qed_dump_common_global_params(p_hwfn,
						p_ptt,
						dump_buf + offset, dump, 1);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "dump-type", "mcp-trace");

	/* Halt MCP while reading from scratchpad so the read data will be
	 * consistent. if halt fails, MCP trace is taken anyway, with a small
	 * risk that it may be corrupt.
	 */
	if (dump && use_mfw) {
		halted = !qed_mcp_halt(p_hwfn, p_ptt);
		if (!halted)
			DP_NOTICE(p_hwfn, "MCP halt failed!\n");
	}

	/* Find trace data size */
	trace_data_size_dwords =
	    DIV_ROUND_UP(trace_data_size_bytes + sizeof(struct mcp_trace),
			 BYTES_IN_DWORD);

	/* Dump trace data section header and param */
	offset += qed_dump_section_hdr(dump_buf + offset,
				       dump, "mcp_trace_data", 1);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump, "size", trace_data_size_dwords);

	/* Read trace data from scratchpad into dump buffer */
	offset += qed_grc_dump_addr_range(p_hwfn,
					  p_ptt,
					  dump_buf + offset,
					  dump,
					  BYTES_TO_DWORDS(trace_data_grc_addr),
					  trace_data_size_dwords, false,
					  SPLIT_TYPE_NONE, 0);

	/* Resume MCP (only if halt succeeded) */
	if (halted && qed_mcp_resume(p_hwfn, p_ptt))
		DP_NOTICE(p_hwfn, "Failed to resume MCP after halt!\n");

	/* Dump trace meta section header */
	offset += qed_dump_section_hdr(dump_buf + offset,
				       dump, "mcp_trace_meta", 1);

	/* If MCP Trace meta size parameter was set, use it.
	 * Otherwise, read trace meta.
	 * trace_meta_size_bytes is dword-aligned.
	 */
	trace_meta_size_bytes =
		qed_grc_get_param(p_hwfn, DBG_GRC_PARAM_MCP_TRACE_META_SIZE);
	if ((!trace_meta_size_bytes || dump) && use_mfw)
		status = qed_mcp_trace_get_meta_info(p_hwfn,
						     p_ptt,
						     trace_data_size_bytes,
						     &running_bundle_id,
						     &trace_meta_offset_bytes,
						     &trace_meta_size_bytes);
	if (status == DBG_STATUS_OK)
		trace_meta_size_dwords = BYTES_TO_DWORDS(trace_meta_size_bytes);

	/* Dump trace meta size param */
	offset += qed_dump_num_param(dump_buf + offset,
				     dump, "size", trace_meta_size_dwords);

	/* Read trace meta image into dump buffer */
	if (dump && trace_meta_size_dwords)
		status = qed_mcp_trace_read_meta(p_hwfn,
						 p_ptt,
						 trace_meta_offset_bytes,
						 trace_meta_size_bytes,
						 dump_buf + offset);
	if (status == DBG_STATUS_OK)
		offset += trace_meta_size_dwords;

	/* Dump last section */
	offset += qed_dump_last_section(dump_buf, offset, dump);

	*num_dumped_dwords = offset;

	/* If no mcp access, indicate that the dump doesn't contain the meta
	 * data from NVRAM.
	 */
	return use_mfw ? status : DBG_STATUS_NVRAM_GET_IMAGE_FAILED;
}

/* Dump GRC FIFO */
static enum dbg_status qed_reg_fifo_dump(struct qed_hwfn *p_hwfn,
					 struct qed_ptt *p_ptt,
					 u32 *dump_buf,
					 bool dump, u32 *num_dumped_dwords)
{
	u32 dwords_read, size_param_offset, offset = 0, addr, len;
	bool fifo_has_data;

	*num_dumped_dwords = 0;

	/* Dump global params */
	offset += qed_dump_common_global_params(p_hwfn,
						p_ptt,
						dump_buf + offset, dump, 1);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "dump-type", "reg-fifo");

	/* Dump fifo data section header and param. The size param is 0 for
	 * now, and is overwritten after reading the FIFO.
	 */
	offset += qed_dump_section_hdr(dump_buf + offset,
				       dump, "reg_fifo_data", 1);
	size_param_offset = offset;
	offset += qed_dump_num_param(dump_buf + offset, dump, "size", 0);

	if (!dump) {
		/* FIFO max size is REG_FIFO_DEPTH_DWORDS. There is no way to
		 * test how much data is available, except for reading it.
		 */
		offset += REG_FIFO_DEPTH_DWORDS;
		goto out;
	}

	fifo_has_data = qed_rd(p_hwfn, p_ptt,
			       GRC_REG_TRACE_FIFO_VALID_DATA) > 0;

	/* Pull available data from fifo. Use DMAE since this is widebus memory
	 * and must be accessed atomically. Test for dwords_read not passing
	 * buffer size since more entries could be added to the buffer as we are
	 * emptying it.
	 */
	addr = BYTES_TO_DWORDS(GRC_REG_TRACE_FIFO);
	len = REG_FIFO_ELEMENT_DWORDS;
	for (dwords_read = 0;
	     fifo_has_data && dwords_read < REG_FIFO_DEPTH_DWORDS;
	     dwords_read += REG_FIFO_ELEMENT_DWORDS) {
		offset += qed_grc_dump_addr_range(p_hwfn,
						  p_ptt,
						  dump_buf + offset,
						  true,
						  addr,
						  len,
						  true, SPLIT_TYPE_NONE,
						  0);
		fifo_has_data = qed_rd(p_hwfn, p_ptt,
				       GRC_REG_TRACE_FIFO_VALID_DATA) > 0;
	}

	qed_dump_num_param(dump_buf + size_param_offset, dump, "size",
			   dwords_read);
out:
	/* Dump last section */
	offset += qed_dump_last_section(dump_buf, offset, dump);

	*num_dumped_dwords = offset;

	return DBG_STATUS_OK;
}

/* Dump IGU FIFO */
static enum dbg_status qed_igu_fifo_dump(struct qed_hwfn *p_hwfn,
					 struct qed_ptt *p_ptt,
					 u32 *dump_buf,
					 bool dump, u32 *num_dumped_dwords)
{
	u32 dwords_read, size_param_offset, offset = 0, addr, len;
	bool fifo_has_data;

	*num_dumped_dwords = 0;

	/* Dump global params */
	offset += qed_dump_common_global_params(p_hwfn,
						p_ptt,
						dump_buf + offset, dump, 1);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "dump-type", "igu-fifo");

	/* Dump fifo data section header and param. The size param is 0 for
	 * now, and is overwritten after reading the FIFO.
	 */
	offset += qed_dump_section_hdr(dump_buf + offset,
				       dump, "igu_fifo_data", 1);
	size_param_offset = offset;
	offset += qed_dump_num_param(dump_buf + offset, dump, "size", 0);

	if (!dump) {
		/* FIFO max size is IGU_FIFO_DEPTH_DWORDS. There is no way to
		 * test how much data is available, except for reading it.
		 */
		offset += IGU_FIFO_DEPTH_DWORDS;
		goto out;
	}

	fifo_has_data = qed_rd(p_hwfn, p_ptt,
			       IGU_REG_ERROR_HANDLING_DATA_VALID) > 0;

	/* Pull available data from fifo. Use DMAE since this is widebus memory
	 * and must be accessed atomically. Test for dwords_read not passing
	 * buffer size since more entries could be added to the buffer as we are
	 * emptying it.
	 */
	addr = BYTES_TO_DWORDS(IGU_REG_ERROR_HANDLING_MEMORY);
	len = IGU_FIFO_ELEMENT_DWORDS;
	for (dwords_read = 0;
	     fifo_has_data && dwords_read < IGU_FIFO_DEPTH_DWORDS;
	     dwords_read += IGU_FIFO_ELEMENT_DWORDS) {
		offset += qed_grc_dump_addr_range(p_hwfn,
						  p_ptt,
						  dump_buf + offset,
						  true,
						  addr,
						  len,
						  true, SPLIT_TYPE_NONE,
						  0);
		fifo_has_data = qed_rd(p_hwfn, p_ptt,
				       IGU_REG_ERROR_HANDLING_DATA_VALID) > 0;
	}

	qed_dump_num_param(dump_buf + size_param_offset, dump, "size",
			   dwords_read);
out:
	/* Dump last section */
	offset += qed_dump_last_section(dump_buf, offset, dump);

	*num_dumped_dwords = offset;

	return DBG_STATUS_OK;
}

/* Protection Override dump */
static enum dbg_status qed_protection_override_dump(struct qed_hwfn *p_hwfn,
						    struct qed_ptt *p_ptt,
						    u32 *dump_buf,
						    bool dump,
						    u32 *num_dumped_dwords)
{
	u32 size_param_offset, override_window_dwords, offset = 0, addr;

	*num_dumped_dwords = 0;

	/* Dump global params */
	offset += qed_dump_common_global_params(p_hwfn,
						p_ptt,
						dump_buf + offset, dump, 1);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "dump-type", "protection-override");

	/* Dump data section header and param. The size param is 0 for now,
	 * and is overwritten after reading the data.
	 */
	offset += qed_dump_section_hdr(dump_buf + offset,
				       dump, "protection_override_data", 1);
	size_param_offset = offset;
	offset += qed_dump_num_param(dump_buf + offset, dump, "size", 0);

	if (!dump) {
		offset += PROTECTION_OVERRIDE_DEPTH_DWORDS;
		goto out;
	}

	/* Add override window info to buffer */
	override_window_dwords =
		qed_rd(p_hwfn, p_ptt, GRC_REG_NUMBER_VALID_OVERRIDE_WINDOW) *
		PROTECTION_OVERRIDE_ELEMENT_DWORDS;
	if (override_window_dwords) {
		addr = BYTES_TO_DWORDS(GRC_REG_PROTECTION_OVERRIDE_WINDOW);
		offset += qed_grc_dump_addr_range(p_hwfn,
						  p_ptt,
						  dump_buf + offset,
						  true,
						  addr,
						  override_window_dwords,
						  true, SPLIT_TYPE_NONE, 0);
		qed_dump_num_param(dump_buf + size_param_offset, dump, "size",
				   override_window_dwords);
	}
out:
	/* Dump last section */
	offset += qed_dump_last_section(dump_buf, offset, dump);

	*num_dumped_dwords = offset;

	return DBG_STATUS_OK;
}

/* Performs FW Asserts Dump to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 qed_fw_asserts_dump(struct qed_hwfn *p_hwfn,
			       struct qed_ptt *p_ptt, u32 *dump_buf, bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct fw_asserts_ram_section *asserts;
	char storm_letter_str[2] = "?";
	struct fw_info fw_info;
	u32 offset = 0;
	u8 storm_id;

	/* Dump global params */
	offset += qed_dump_common_global_params(p_hwfn,
						p_ptt,
						dump_buf + offset, dump, 1);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "dump-type", "fw-asserts");

	/* Find Storm dump size */
	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		u32 fw_asserts_section_addr, next_list_idx_addr, next_list_idx;
		struct storm_defs *storm = &s_storm_defs[storm_id];
		u32 last_list_idx, addr;

		if (dev_data->block_in_reset[storm->sem_block_id])
			continue;

		/* Read FW info for the current Storm */
		qed_read_storm_fw_info(p_hwfn, p_ptt, storm_id, &fw_info);

		asserts = &fw_info.fw_asserts_section;

		/* Dump FW Asserts section header and params */
		storm_letter_str[0] = storm->letter;
		offset += qed_dump_section_hdr(dump_buf + offset,
					       dump, "fw_asserts", 2);
		offset += qed_dump_str_param(dump_buf + offset,
					     dump, "storm", storm_letter_str);
		offset += qed_dump_num_param(dump_buf + offset,
					     dump,
					     "size",
					     asserts->list_element_dword_size);

		/* Read and dump FW Asserts data */
		if (!dump) {
			offset += asserts->list_element_dword_size;
			continue;
		}

		addr = le16_to_cpu(asserts->section_ram_line_offset);
		fw_asserts_section_addr = storm->sem_fast_mem_addr +
					  SEM_FAST_REG_INT_RAM +
					  RAM_LINES_TO_BYTES(addr);

		next_list_idx_addr = fw_asserts_section_addr +
			DWORDS_TO_BYTES(asserts->list_next_index_dword_offset);
		next_list_idx = qed_rd(p_hwfn, p_ptt, next_list_idx_addr);
		last_list_idx = (next_list_idx > 0 ?
				 next_list_idx :
				 asserts->list_num_elements) - 1;
		addr = BYTES_TO_DWORDS(fw_asserts_section_addr) +
		       asserts->list_dword_offset +
		       last_list_idx * asserts->list_element_dword_size;
		offset +=
		    qed_grc_dump_addr_range(p_hwfn, p_ptt,
					    dump_buf + offset,
					    dump, addr,
					    asserts->list_element_dword_size,
						  false, SPLIT_TYPE_NONE, 0);
	}

	/* Dump last section */
	offset += qed_dump_last_section(dump_buf, offset, dump);

	return offset;
}

/* Dumps the specified ILT pages to the specified buffer.
 * Returns the dumped size in dwords.
 */
static u32 qed_ilt_dump_pages_range(u32 *dump_buf, u32 *given_offset,
				    bool *dump, u32 start_page_id,
				    u32 num_pages,
				    struct phys_mem_desc *ilt_pages,
				    bool dump_page_ids, u32 buf_size_in_dwords,
				    u32 *given_actual_dump_size_in_dwords)
{
	u32 actual_dump_size_in_dwords = *given_actual_dump_size_in_dwords;
	u32 page_id, end_page_id, offset = *given_offset;
	struct phys_mem_desc *mem_desc = NULL;
	bool continue_dump = *dump;
	u32 partial_page_size = 0;

	if (num_pages == 0)
		return offset;

	end_page_id = start_page_id + num_pages - 1;

	for (page_id = start_page_id; page_id <= end_page_id; page_id++) {
		mem_desc = &ilt_pages[page_id];
		if (!ilt_pages[page_id].virt_addr)
			continue;

		if (dump_page_ids) {
			/* Copy page ID to dump buffer
			 * (if dump is needed and buffer is not full)
			 */
			if ((continue_dump) &&
			    (offset + 1 > buf_size_in_dwords)) {
				continue_dump = false;
				actual_dump_size_in_dwords = offset;
			}
			if (continue_dump)
				*(dump_buf + offset) = page_id;
			offset++;
		} else {
			/* Copy page memory to dump buffer */
			if ((continue_dump) &&
			    (offset + BYTES_TO_DWORDS(mem_desc->size) >
			     buf_size_in_dwords)) {
				if (offset + BYTES_TO_DWORDS(mem_desc->size) >
				    buf_size_in_dwords) {
					partial_page_size =
					    buf_size_in_dwords - offset;
					memcpy(dump_buf + offset,
					       mem_desc->virt_addr,
					       partial_page_size);
					continue_dump = false;
					actual_dump_size_in_dwords =
					    offset + partial_page_size;
				}
			}

			if (continue_dump)
				memcpy(dump_buf + offset,
				       mem_desc->virt_addr, mem_desc->size);
			offset += BYTES_TO_DWORDS(mem_desc->size);
		}
	}

	*dump = continue_dump;
	*given_offset = offset;
	*given_actual_dump_size_in_dwords = actual_dump_size_in_dwords;

	return offset;
}

/* Dumps a section containing the dumped ILT pages.
 * Returns the dumped size in dwords.
 */
static u32 qed_ilt_dump_pages_section(struct qed_hwfn *p_hwfn,
				      u32 *dump_buf,
				      u32 *given_offset,
				      bool *dump,
				      u32 valid_conn_pf_pages,
				      u32 valid_conn_vf_pages,
				      struct phys_mem_desc *ilt_pages,
				      bool dump_page_ids,
				      u32 buf_size_in_dwords,
				      u32 *given_actual_dump_size_in_dwords)
{
	struct qed_ilt_client_cfg *clients = p_hwfn->p_cxt_mngr->clients;
	u32 pf_start_line, start_page_id, offset = *given_offset;
	u32 cdut_pf_init_pages, cdut_vf_init_pages;
	u32 cdut_pf_work_pages, cdut_vf_work_pages;
	u32 base_data_offset, size_param_offset;
	u32 src_pages;
	u32 section_header_and_param_size;
	u32 cdut_pf_pages, cdut_vf_pages;
	u32 actual_dump_size_in_dwords;
	bool continue_dump = *dump;
	bool update_size = *dump;
	const char *section_name;
	u32 i;

	actual_dump_size_in_dwords = *given_actual_dump_size_in_dwords;
	section_name = dump_page_ids ? "ilt_page_ids" : "ilt_page_mem";
	cdut_pf_init_pages = qed_get_cdut_num_pf_init_pages(p_hwfn);
	cdut_vf_init_pages = qed_get_cdut_num_vf_init_pages(p_hwfn);
	cdut_pf_work_pages = qed_get_cdut_num_pf_work_pages(p_hwfn);
	cdut_vf_work_pages = qed_get_cdut_num_vf_work_pages(p_hwfn);
	cdut_pf_pages = cdut_pf_init_pages + cdut_pf_work_pages;
	cdut_vf_pages = cdut_vf_init_pages + cdut_vf_work_pages;
	pf_start_line = p_hwfn->p_cxt_mngr->pf_start_line;
	section_header_and_param_size = qed_dump_section_hdr(NULL,
							     false,
							     section_name,
							     1) +
	qed_dump_num_param(NULL, false, "size", 0);

	if ((continue_dump) &&
	    (offset + section_header_and_param_size > buf_size_in_dwords)) {
		continue_dump = false;
		update_size = false;
		actual_dump_size_in_dwords = offset;
	}

	offset += qed_dump_section_hdr(dump_buf + offset,
				       continue_dump, section_name, 1);

	/* Dump size parameter (0 for now, overwritten with real size later) */
	size_param_offset = offset;
	offset += qed_dump_num_param(dump_buf + offset,
				     continue_dump, "size", 0);
	base_data_offset = offset;

	/* CDUC pages are ordered as follows:
	 * - PF pages - valid section (included in PF connection type mapping)
	 * - PF pages - invalid section (not dumped)
	 * - For each VF in the PF:
	 *   - VF pages - valid section (included in VF connection type mapping)
	 *   - VF pages - invalid section (not dumped)
	 */
	if (qed_grc_get_param(p_hwfn, DBG_GRC_PARAM_DUMP_ILT_CDUC)) {
		/* Dump connection PF pages */
		start_page_id = clients[ILT_CLI_CDUC].first.val - pf_start_line;
		qed_ilt_dump_pages_range(dump_buf, &offset, &continue_dump,
					 start_page_id, valid_conn_pf_pages,
					 ilt_pages, dump_page_ids,
					 buf_size_in_dwords,
					 &actual_dump_size_in_dwords);

		/* Dump connection VF pages */
		start_page_id += clients[ILT_CLI_CDUC].pf_total_lines;
		for (i = 0; i < p_hwfn->p_cxt_mngr->vf_count;
		     i++, start_page_id += clients[ILT_CLI_CDUC].vf_total_lines)
			qed_ilt_dump_pages_range(dump_buf, &offset,
						 &continue_dump, start_page_id,
						 valid_conn_vf_pages,
						 ilt_pages, dump_page_ids,
						 buf_size_in_dwords,
						 &actual_dump_size_in_dwords);
	}

	/* CDUT pages are ordered as follows:
	 * - PF init pages (not dumped)
	 * - PF work pages
	 * - For each VF in the PF:
	 *   - VF init pages (not dumped)
	 *   - VF work pages
	 */
	if (qed_grc_get_param(p_hwfn, DBG_GRC_PARAM_DUMP_ILT_CDUT)) {
		/* Dump task PF pages */
		start_page_id = clients[ILT_CLI_CDUT].first.val +
		    cdut_pf_init_pages - pf_start_line;
		qed_ilt_dump_pages_range(dump_buf, &offset, &continue_dump,
					 start_page_id, cdut_pf_work_pages,
					 ilt_pages, dump_page_ids,
					 buf_size_in_dwords,
					 &actual_dump_size_in_dwords);

		/* Dump task VF pages */
		start_page_id = clients[ILT_CLI_CDUT].first.val +
		    cdut_pf_pages + cdut_vf_init_pages - pf_start_line;
		for (i = 0; i < p_hwfn->p_cxt_mngr->vf_count;
		     i++, start_page_id += cdut_vf_pages)
			qed_ilt_dump_pages_range(dump_buf, &offset,
						 &continue_dump, start_page_id,
						 cdut_vf_work_pages, ilt_pages,
						 dump_page_ids,
						 buf_size_in_dwords,
						 &actual_dump_size_in_dwords);
	}

	/*Dump Searcher pages */
	if (clients[ILT_CLI_SRC].active) {
		start_page_id = clients[ILT_CLI_SRC].first.val - pf_start_line;
		src_pages = clients[ILT_CLI_SRC].last.val -
		    clients[ILT_CLI_SRC].first.val + 1;
		qed_ilt_dump_pages_range(dump_buf, &offset, &continue_dump,
					 start_page_id, src_pages, ilt_pages,
					 dump_page_ids, buf_size_in_dwords,
					 &actual_dump_size_in_dwords);
	}

	/* Overwrite size param */
	if (update_size) {
		u32 section_size = (*dump == continue_dump) ?
		    offset - base_data_offset :
		    actual_dump_size_in_dwords - base_data_offset;
		if (section_size > 0)
			qed_dump_num_param(dump_buf + size_param_offset,
					   *dump, "size", section_size);
		else if ((section_size == 0) && (*dump != continue_dump))
			actual_dump_size_in_dwords -=
			    section_header_and_param_size;
	}

	*dump = continue_dump;
	*given_offset = offset;
	*given_actual_dump_size_in_dwords = actual_dump_size_in_dwords;

	return offset;
}

/* Dumps a section containing the global parameters.
 * Part of ilt dump process
 * Returns the dumped size in dwords.
 */
static u32
qed_ilt_dump_dump_common_global_params(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt,
				       u32 *dump_buf,
				       bool dump,
				       u32 cduc_page_size,
				       u32 conn_ctx_size,
				       u32 cdut_page_size,
				       u32 *full_dump_size_param_offset,
				       u32 *actual_dump_size_param_offset)
{
	struct qed_ilt_client_cfg *clients = p_hwfn->p_cxt_mngr->clients;
	u32 offset = 0;

	offset += qed_dump_common_global_params(p_hwfn, p_ptt,
						dump_buf + offset,
						dump, 30);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump,
				     "dump-type", "ilt-dump");
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "cduc-page-size",
				     cduc_page_size);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "cduc-first-page-id",
				     clients[ILT_CLI_CDUC].first.val);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "cduc-last-page-id",
				     clients[ILT_CLI_CDUC].last.val);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "cduc-num-pf-pages",
				     clients[ILT_CLI_CDUC].pf_total_lines);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "cduc-num-vf-pages",
				     clients[ILT_CLI_CDUC].vf_total_lines);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "max-conn-ctx-size",
				     conn_ctx_size);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "cdut-page-size",
				     cdut_page_size);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "cdut-first-page-id",
				     clients[ILT_CLI_CDUT].first.val);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "cdut-last-page-id",
				     clients[ILT_CLI_CDUT].last.val);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "cdut-num-pf-init-pages",
				     qed_get_cdut_num_pf_init_pages(p_hwfn));
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "cdut-num-vf-init-pages",
				     qed_get_cdut_num_vf_init_pages(p_hwfn));
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "cdut-num-pf-work-pages",
				     qed_get_cdut_num_pf_work_pages(p_hwfn));
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "cdut-num-vf-work-pages",
				     qed_get_cdut_num_vf_work_pages(p_hwfn));
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "max-task-ctx-size",
				     p_hwfn->p_cxt_mngr->task_ctx_size);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "first-vf-id-in-pf",
				     p_hwfn->p_cxt_mngr->first_vf_in_pf);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "num-vfs-in-pf",
				     p_hwfn->p_cxt_mngr->vf_count);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "ptr-size-bytes",
				     sizeof(void *));
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "pf-start-line",
				     p_hwfn->p_cxt_mngr->pf_start_line);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "page-mem-desc-size-dwords",
				     PAGE_MEM_DESC_SIZE_DWORDS);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "ilt-shadow-size",
				     p_hwfn->p_cxt_mngr->ilt_shadow_size);

	*full_dump_size_param_offset = offset;

	offset += qed_dump_num_param(dump_buf + offset,
				     dump, "dump-size-full", 0);

	*actual_dump_size_param_offset = offset;

	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "dump-size-actual", 0);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "iscsi_task_pages",
				     p_hwfn->p_cxt_mngr->iscsi_task_pages);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "fcoe_task_pages",
				     p_hwfn->p_cxt_mngr->fcoe_task_pages);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "roce_task_pages",
				     p_hwfn->p_cxt_mngr->roce_task_pages);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "eth_task_pages",
				     p_hwfn->p_cxt_mngr->eth_task_pages);
	offset += qed_dump_num_param(dump_buf + offset,
				      dump,
				      "src-first-page-id",
				      clients[ILT_CLI_SRC].first.val);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "src-last-page-id",
				     clients[ILT_CLI_SRC].last.val);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "src-is-active",
				     clients[ILT_CLI_SRC].active);

	/* Additional/Less parameters require matching of number in call to
	 * dump_common_global_params()
	 */

	return offset;
}

/* Dump section containing number of PF CIDs per connection type.
 * Part of ilt dump process.
 * Returns the dumped size in dwords.
 */
static u32 qed_ilt_dump_dump_num_pf_cids(struct qed_hwfn *p_hwfn,
					 u32 *dump_buf,
					 bool dump, u32 *valid_conn_pf_cids)
{
	u32 num_pf_cids = 0;
	u32 offset = 0;
	u8 conn_type;

	offset += qed_dump_section_hdr(dump_buf + offset,
				       dump, "num_pf_cids_per_conn_type", 1);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump, "size", NUM_OF_CONNECTION_TYPES);
	for (conn_type = 0, *valid_conn_pf_cids = 0;
	     conn_type < NUM_OF_CONNECTION_TYPES; conn_type++, offset++) {
		num_pf_cids = p_hwfn->p_cxt_mngr->conn_cfg[conn_type].cid_count;
		if (dump)
			*(dump_buf + offset) = num_pf_cids;
		*valid_conn_pf_cids += num_pf_cids;
	}

	return offset;
}

/* Dump section containing number of VF CIDs per connection type
 * Part of ilt dump process.
 * Returns the dumped size in dwords.
 */
static u32 qed_ilt_dump_dump_num_vf_cids(struct qed_hwfn *p_hwfn,
					 u32 *dump_buf,
					 bool dump, u32 *valid_conn_vf_cids)
{
	u32 num_vf_cids = 0;
	u32 offset = 0;
	u8 conn_type;

	offset += qed_dump_section_hdr(dump_buf + offset, dump,
				       "num_vf_cids_per_conn_type", 1);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump, "size", NUM_OF_CONNECTION_TYPES);
	for (conn_type = 0, *valid_conn_vf_cids = 0;
	     conn_type < NUM_OF_CONNECTION_TYPES; conn_type++, offset++) {
		num_vf_cids =
		    p_hwfn->p_cxt_mngr->conn_cfg[conn_type].cids_per_vf;
		if (dump)
			*(dump_buf + offset) = num_vf_cids;
		*valid_conn_vf_cids += num_vf_cids;
	}

	return offset;
}

/* Performs ILT Dump to the specified buffer.
 * buf_size_in_dwords - The dumped buffer size.
 * Returns the dumped size in dwords.
 */
static u32 qed_ilt_dump(struct qed_hwfn *p_hwfn,
			struct qed_ptt *p_ptt,
			u32 *dump_buf, u32 buf_size_in_dwords, bool dump)
{
#if ((!defined VMWARE) && (!defined UEFI))
	struct qed_ilt_client_cfg *clients = p_hwfn->p_cxt_mngr->clients;
#endif
	u32 valid_conn_vf_cids = 0,
	    valid_conn_vf_pages, offset = 0, real_dumped_size = 0;
	u32 valid_conn_pf_cids = 0, valid_conn_pf_pages, num_pages;
	u32 num_cids_per_page, conn_ctx_size;
	u32 cduc_page_size, cdut_page_size;
	u32 actual_dump_size_in_dwords = 0;
	struct phys_mem_desc *ilt_pages;
	u32 actul_dump_off = 0;
	u32 last_section_size;
	u32 full_dump_off = 0;
	u32 section_size = 0;
	bool continue_dump;
	u32 page_id;

	last_section_size = qed_dump_last_section(NULL, 0, false);
	cduc_page_size = 1 <<
	    (clients[ILT_CLI_CDUC].p_size.val + PXP_ILT_PAGE_SIZE_NUM_BITS_MIN);
	cdut_page_size = 1 <<
	    (clients[ILT_CLI_CDUT].p_size.val + PXP_ILT_PAGE_SIZE_NUM_BITS_MIN);
	conn_ctx_size = p_hwfn->p_cxt_mngr->conn_ctx_size;
	num_cids_per_page = (int)(cduc_page_size / conn_ctx_size);
	ilt_pages = p_hwfn->p_cxt_mngr->ilt_shadow;
	continue_dump = dump;

	/* if need to dump then save memory for the last section
	 * (last section calculates CRC of dumped data)
	 */
	if (dump) {
		if (buf_size_in_dwords >= last_section_size) {
			buf_size_in_dwords -= last_section_size;
		} else {
			continue_dump = false;
			actual_dump_size_in_dwords = offset;
		}
	}

	/* Dump global params */

	/* if need to dump then first check that there is enough memory
	 * in dumped buffer for this section calculate the size of this
	 * section without dumping. if there is not enough memory - then
	 * stop the dumping.
	 */
	if (continue_dump) {
		section_size =
			qed_ilt_dump_dump_common_global_params(p_hwfn,
							       p_ptt,
							       NULL,
							       false,
							       cduc_page_size,
							       conn_ctx_size,
							       cdut_page_size,
							       &full_dump_off,
							       &actul_dump_off);
		if (offset + section_size > buf_size_in_dwords) {
			continue_dump = false;
			actual_dump_size_in_dwords = offset;
		}
	}

	offset += qed_ilt_dump_dump_common_global_params(p_hwfn,
							 p_ptt,
							 dump_buf + offset,
							 continue_dump,
							 cduc_page_size,
							 conn_ctx_size,
							 cdut_page_size,
							 &full_dump_off,
							 &actul_dump_off);

	/* Dump section containing number of PF CIDs per connection type
	 * If need to dump then first check that there is enough memory in
	 * dumped buffer for this section.
	 */
	if (continue_dump) {
		section_size =
			qed_ilt_dump_dump_num_pf_cids(p_hwfn,
						      NULL,
						      false,
						      &valid_conn_pf_cids);
		if (offset + section_size > buf_size_in_dwords) {
			continue_dump = false;
			actual_dump_size_in_dwords = offset;
		}
	}

	offset += qed_ilt_dump_dump_num_pf_cids(p_hwfn,
						dump_buf + offset,
						continue_dump,
						&valid_conn_pf_cids);

	/* Dump section containing number of VF CIDs per connection type
	 * If need to dump then first check that there is enough memory in
	 * dumped buffer for this section.
	 */
	if (continue_dump) {
		section_size =
			qed_ilt_dump_dump_num_vf_cids(p_hwfn,
						      NULL,
						      false,
						      &valid_conn_vf_cids);
		if (offset + section_size > buf_size_in_dwords) {
			continue_dump = false;
			actual_dump_size_in_dwords = offset;
		}
	}

	offset += qed_ilt_dump_dump_num_vf_cids(p_hwfn,
						dump_buf + offset,
						continue_dump,
						&valid_conn_vf_cids);

	/* Dump section containing physical memory descriptors for each
	 * ILT page.
	 */
	num_pages = p_hwfn->p_cxt_mngr->ilt_shadow_size;

	/* If need to dump then first check that there is enough memory
	 * in dumped buffer for the section header.
	 */
	if (continue_dump) {
		section_size = qed_dump_section_hdr(NULL,
						    false,
						    "ilt_page_desc",
						    1) +
		    qed_dump_num_param(NULL,
				       false,
				       "size",
				       num_pages * PAGE_MEM_DESC_SIZE_DWORDS);
		if (offset + section_size > buf_size_in_dwords) {
			continue_dump = false;
			actual_dump_size_in_dwords = offset;
		}
	}

	offset += qed_dump_section_hdr(dump_buf + offset,
				       continue_dump, "ilt_page_desc", 1);
	offset += qed_dump_num_param(dump_buf + offset,
				     continue_dump,
				     "size",
				     num_pages * PAGE_MEM_DESC_SIZE_DWORDS);

	/* Copy memory descriptors to dump buffer
	 * If need to dump then dump till the dump buffer size
	 */
	if (continue_dump) {
		for (page_id = 0; page_id < num_pages;
		     page_id++, offset += PAGE_MEM_DESC_SIZE_DWORDS) {
			if (continue_dump &&
			    (offset + PAGE_MEM_DESC_SIZE_DWORDS <=
			     buf_size_in_dwords)) {
				memcpy(dump_buf + offset,
				       &ilt_pages[page_id],
				       DWORDS_TO_BYTES
				       (PAGE_MEM_DESC_SIZE_DWORDS));
			} else {
				if (continue_dump) {
					continue_dump = false;
					actual_dump_size_in_dwords = offset;
				}
			}
		}
	} else {
		offset += num_pages * PAGE_MEM_DESC_SIZE_DWORDS;
	}

	valid_conn_pf_pages = DIV_ROUND_UP(valid_conn_pf_cids,
					   num_cids_per_page);
	valid_conn_vf_pages = DIV_ROUND_UP(valid_conn_vf_cids,
					   num_cids_per_page);

	/* Dump ILT pages IDs */
	qed_ilt_dump_pages_section(p_hwfn, dump_buf, &offset, &continue_dump,
				   valid_conn_pf_pages, valid_conn_vf_pages,
				   ilt_pages, true, buf_size_in_dwords,
				   &actual_dump_size_in_dwords);

	/* Dump ILT pages memory */
	qed_ilt_dump_pages_section(p_hwfn, dump_buf, &offset, &continue_dump,
				   valid_conn_pf_pages, valid_conn_vf_pages,
				   ilt_pages, false, buf_size_in_dwords,
				   &actual_dump_size_in_dwords);

	real_dumped_size =
	    (continue_dump == dump) ? offset : actual_dump_size_in_dwords;
	qed_dump_num_param(dump_buf + full_dump_off, dump,
			   "full-dump-size", offset + last_section_size);
	qed_dump_num_param(dump_buf + actul_dump_off,
			   dump,
			   "actual-dump-size",
			   real_dumped_size + last_section_size);

	/* Dump last section */
	real_dumped_size += qed_dump_last_section(dump_buf,
						  real_dumped_size, dump);

	return real_dumped_size;
}

/***************************** Public Functions *******************************/

enum dbg_status qed_dbg_set_bin_ptr(struct qed_hwfn *p_hwfn,
				    const u8 * const bin_ptr)
{
	struct bin_buffer_hdr *buf_hdrs = (struct bin_buffer_hdr *)bin_ptr;
	u8 buf_id;

	/* Convert binary data to debug arrays */
	for (buf_id = 0; buf_id < MAX_BIN_DBG_BUFFER_TYPE; buf_id++)
		qed_set_dbg_bin_buf(p_hwfn,
				    buf_id,
				    (u32 *)(bin_ptr + buf_hdrs[buf_id].offset),
				    buf_hdrs[buf_id].length);

	return DBG_STATUS_OK;
}

static enum dbg_status qed_dbg_set_app_ver(u32 ver)
{
	if (ver < TOOLS_VERSION)
		return DBG_STATUS_UNSUPPORTED_APP_VERSION;

	s_app_ver = ver;

	return DBG_STATUS_OK;
}

bool qed_read_fw_info(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt, struct fw_info *fw_info)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u8 storm_id;

	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		struct storm_defs *storm = &s_storm_defs[storm_id];

		/* Skip Storm if it's in reset */
		if (dev_data->block_in_reset[storm->sem_block_id])
			continue;

		/* Read FW info for the current Storm */
		qed_read_storm_fw_info(p_hwfn, p_ptt, storm_id, fw_info);

		return true;
	}

	return false;
}

enum dbg_status qed_dbg_grc_config(struct qed_hwfn *p_hwfn,
				   enum dbg_grc_params grc_param, u32 val)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	enum dbg_status status;
	int i;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_DEBUG,
		   "dbg_grc_config: paramId = %d, val = %d\n", grc_param, val);

	status = qed_dbg_dev_init(p_hwfn);
	if (status != DBG_STATUS_OK)
		return status;

	/* Initializes the GRC parameters (if not initialized). Needed in order
	 * to set the default parameter values for the first time.
	 */
	qed_dbg_grc_init_params(p_hwfn);

	if (grc_param >= MAX_DBG_GRC_PARAMS || grc_param < 0)
		return DBG_STATUS_INVALID_ARGS;
	if (val < s_grc_param_defs[grc_param].min ||
	    val > s_grc_param_defs[grc_param].max)
		return DBG_STATUS_INVALID_ARGS;

	if (s_grc_param_defs[grc_param].is_preset) {
		/* Preset param */

		/* Disabling a preset is not allowed. Call
		 * dbg_grc_set_params_default instead.
		 */
		if (!val)
			return DBG_STATUS_INVALID_ARGS;

		/* Update all params with the preset values */
		for (i = 0; i < MAX_DBG_GRC_PARAMS; i++) {
			struct grc_param_defs *defs = &s_grc_param_defs[i];
			u32 preset_val;
			/* Skip persistent params */
			if (defs->is_persistent)
				continue;

			/* Find preset value */
			if (grc_param == DBG_GRC_PARAM_EXCLUDE_ALL)
				preset_val =
				    defs->exclude_all_preset_val;
			else if (grc_param == DBG_GRC_PARAM_CRASH)
				preset_val =
				    defs->crash_preset_val[dev_data->chip_id];
			else
				return DBG_STATUS_INVALID_ARGS;

			qed_grc_set_param(p_hwfn, i, preset_val);
		}
	} else {
		/* Regular param - set its value */
		qed_grc_set_param(p_hwfn, grc_param, val);
	}

	return DBG_STATUS_OK;
}

/* Assign default GRC param values */
void qed_dbg_grc_set_params_default(struct qed_hwfn *p_hwfn)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 i;

	for (i = 0; i < MAX_DBG_GRC_PARAMS; i++)
		if (!s_grc_param_defs[i].is_persistent)
			dev_data->grc.param_val[i] =
			    s_grc_param_defs[i].default_val[dev_data->chip_id];
}

enum dbg_status qed_dbg_grc_get_dump_buf_size(struct qed_hwfn *p_hwfn,
					      struct qed_ptt *p_ptt,
					      u32 *buf_size)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn);

	*buf_size = 0;

	if (status != DBG_STATUS_OK)
		return status;

	if (!p_hwfn->dbg_arrays[BIN_BUF_DBG_MODE_TREE].ptr ||
	    !p_hwfn->dbg_arrays[BIN_BUF_DBG_DUMP_REG].ptr ||
	    !p_hwfn->dbg_arrays[BIN_BUF_DBG_DUMP_MEM].ptr ||
	    !p_hwfn->dbg_arrays[BIN_BUF_DBG_ATTN_BLOCKS].ptr ||
	    !p_hwfn->dbg_arrays[BIN_BUF_DBG_ATTN_REGS].ptr)
		return DBG_STATUS_DBG_ARRAY_NOT_SET;

	return qed_grc_dump(p_hwfn, p_ptt, NULL, false, buf_size);
}

enum dbg_status qed_dbg_grc_dump(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 u32 *dump_buf,
				 u32 buf_size_in_dwords,
				 u32 *num_dumped_dwords)
{
	u32 needed_buf_size_in_dwords;
	enum dbg_status status;

	*num_dumped_dwords = 0;

	status = qed_dbg_grc_get_dump_buf_size(p_hwfn,
					       p_ptt,
					       &needed_buf_size_in_dwords);
	if (status != DBG_STATUS_OK)
		return status;

	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Doesn't do anything, needed for compile time asserts */
	qed_static_asserts();

	/* GRC Dump */
	status = qed_grc_dump(p_hwfn, p_ptt, dump_buf, true, num_dumped_dwords);

	/* Revert GRC params to their default */
	qed_dbg_grc_set_params_default(p_hwfn);

	return status;
}

enum dbg_status qed_dbg_idle_chk_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 *buf_size)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct idle_chk_data *idle_chk = &dev_data->idle_chk;
	enum dbg_status status;

	*buf_size = 0;

	status = qed_dbg_dev_init(p_hwfn);
	if (status != DBG_STATUS_OK)
		return status;

	if (!p_hwfn->dbg_arrays[BIN_BUF_DBG_MODE_TREE].ptr ||
	    !p_hwfn->dbg_arrays[BIN_BUF_DBG_IDLE_CHK_REGS].ptr ||
	    !p_hwfn->dbg_arrays[BIN_BUF_DBG_IDLE_CHK_IMMS].ptr ||
	    !p_hwfn->dbg_arrays[BIN_BUF_DBG_IDLE_CHK_RULES].ptr)
		return DBG_STATUS_DBG_ARRAY_NOT_SET;

	if (!idle_chk->buf_size_set) {
		idle_chk->buf_size = qed_idle_chk_dump(p_hwfn,
						       p_ptt, NULL, false);
		idle_chk->buf_size_set = true;
	}

	*buf_size = idle_chk->buf_size;

	return DBG_STATUS_OK;
}

enum dbg_status qed_dbg_idle_chk_dump(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      u32 *dump_buf,
				      u32 buf_size_in_dwords,
				      u32 *num_dumped_dwords)
{
	u32 needed_buf_size_in_dwords;
	enum dbg_status status;

	*num_dumped_dwords = 0;

	status = qed_dbg_idle_chk_get_dump_buf_size(p_hwfn,
						    p_ptt,
						    &needed_buf_size_in_dwords);
	if (status != DBG_STATUS_OK)
		return status;

	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	qed_grc_unreset_blocks(p_hwfn, p_ptt, true);
	qed_update_blocks_reset_state(p_hwfn, p_ptt);

	/* Idle Check Dump */
	*num_dumped_dwords = qed_idle_chk_dump(p_hwfn, p_ptt, dump_buf, true);

	/* Revert GRC params to their default */
	qed_dbg_grc_set_params_default(p_hwfn);

	return DBG_STATUS_OK;
}

enum dbg_status qed_dbg_mcp_trace_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						    struct qed_ptt *p_ptt,
						    u32 *buf_size)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn);

	*buf_size = 0;

	if (status != DBG_STATUS_OK)
		return status;

	return qed_mcp_trace_dump(p_hwfn, p_ptt, NULL, false, buf_size);
}

enum dbg_status qed_dbg_mcp_trace_dump(struct qed_hwfn *p_hwfn,
				       struct qed_ptt *p_ptt,
				       u32 *dump_buf,
				       u32 buf_size_in_dwords,
				       u32 *num_dumped_dwords)
{
	u32 needed_buf_size_in_dwords;
	enum dbg_status status;

	status =
		qed_dbg_mcp_trace_get_dump_buf_size(p_hwfn,
						    p_ptt,
						    &needed_buf_size_in_dwords);
	if (status != DBG_STATUS_OK && status !=
	    DBG_STATUS_NVRAM_GET_IMAGE_FAILED)
		return status;

	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	qed_update_blocks_reset_state(p_hwfn, p_ptt);

	/* Perform dump */
	status = qed_mcp_trace_dump(p_hwfn,
				    p_ptt, dump_buf, true, num_dumped_dwords);

	/* Revert GRC params to their default */
	qed_dbg_grc_set_params_default(p_hwfn);

	return status;
}

enum dbg_status qed_dbg_reg_fifo_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 *buf_size)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn);

	*buf_size = 0;

	if (status != DBG_STATUS_OK)
		return status;

	return qed_reg_fifo_dump(p_hwfn, p_ptt, NULL, false, buf_size);
}

enum dbg_status qed_dbg_reg_fifo_dump(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      u32 *dump_buf,
				      u32 buf_size_in_dwords,
				      u32 *num_dumped_dwords)
{
	u32 needed_buf_size_in_dwords;
	enum dbg_status status;

	*num_dumped_dwords = 0;

	status = qed_dbg_reg_fifo_get_dump_buf_size(p_hwfn,
						    p_ptt,
						    &needed_buf_size_in_dwords);
	if (status != DBG_STATUS_OK)
		return status;

	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	qed_update_blocks_reset_state(p_hwfn, p_ptt);

	status = qed_reg_fifo_dump(p_hwfn,
				   p_ptt, dump_buf, true, num_dumped_dwords);

	/* Revert GRC params to their default */
	qed_dbg_grc_set_params_default(p_hwfn);

	return status;
}

enum dbg_status qed_dbg_igu_fifo_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 *buf_size)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn);

	*buf_size = 0;

	if (status != DBG_STATUS_OK)
		return status;

	return qed_igu_fifo_dump(p_hwfn, p_ptt, NULL, false, buf_size);
}

enum dbg_status qed_dbg_igu_fifo_dump(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      u32 *dump_buf,
				      u32 buf_size_in_dwords,
				      u32 *num_dumped_dwords)
{
	u32 needed_buf_size_in_dwords;
	enum dbg_status status;

	*num_dumped_dwords = 0;

	status = qed_dbg_igu_fifo_get_dump_buf_size(p_hwfn,
						    p_ptt,
						    &needed_buf_size_in_dwords);
	if (status != DBG_STATUS_OK)
		return status;

	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	qed_update_blocks_reset_state(p_hwfn, p_ptt);

	status = qed_igu_fifo_dump(p_hwfn,
				   p_ptt, dump_buf, true, num_dumped_dwords);
	/* Revert GRC params to their default */
	qed_dbg_grc_set_params_default(p_hwfn);

	return status;
}

enum dbg_status
qed_dbg_protection_override_get_dump_buf_size(struct qed_hwfn *p_hwfn,
					      struct qed_ptt *p_ptt,
					      u32 *buf_size)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn);

	*buf_size = 0;

	if (status != DBG_STATUS_OK)
		return status;

	return qed_protection_override_dump(p_hwfn,
					    p_ptt, NULL, false, buf_size);
}

enum dbg_status qed_dbg_protection_override_dump(struct qed_hwfn *p_hwfn,
						 struct qed_ptt *p_ptt,
						 u32 *dump_buf,
						 u32 buf_size_in_dwords,
						 u32 *num_dumped_dwords)
{
	u32 needed_buf_size_in_dwords, *p_size = &needed_buf_size_in_dwords;
	enum dbg_status status;

	*num_dumped_dwords = 0;

	status =
		qed_dbg_protection_override_get_dump_buf_size(p_hwfn,
							      p_ptt,
							      p_size);
	if (status != DBG_STATUS_OK)
		return status;

	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	qed_update_blocks_reset_state(p_hwfn, p_ptt);

	status = qed_protection_override_dump(p_hwfn,
					      p_ptt,
					      dump_buf,
					      true, num_dumped_dwords);

	/* Revert GRC params to their default */
	qed_dbg_grc_set_params_default(p_hwfn);

	return status;
}

enum dbg_status qed_dbg_fw_asserts_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						     struct qed_ptt *p_ptt,
						     u32 *buf_size)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn);

	*buf_size = 0;

	if (status != DBG_STATUS_OK)
		return status;

	/* Update reset state */
	qed_update_blocks_reset_state(p_hwfn, p_ptt);

	*buf_size = qed_fw_asserts_dump(p_hwfn, p_ptt, NULL, false);

	return DBG_STATUS_OK;
}

enum dbg_status qed_dbg_fw_asserts_dump(struct qed_hwfn *p_hwfn,
					struct qed_ptt *p_ptt,
					u32 *dump_buf,
					u32 buf_size_in_dwords,
					u32 *num_dumped_dwords)
{
	u32 needed_buf_size_in_dwords, *p_size = &needed_buf_size_in_dwords;
	enum dbg_status status;

	*num_dumped_dwords = 0;

	status =
		qed_dbg_fw_asserts_get_dump_buf_size(p_hwfn,
						     p_ptt,
						     p_size);
	if (status != DBG_STATUS_OK)
		return status;

	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	*num_dumped_dwords = qed_fw_asserts_dump(p_hwfn, p_ptt, dump_buf, true);

	/* Revert GRC params to their default */
	qed_dbg_grc_set_params_default(p_hwfn);

	return DBG_STATUS_OK;
}

static enum dbg_status qed_dbg_ilt_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						     struct qed_ptt *p_ptt,
						     u32 *buf_size)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn);

	*buf_size = 0;

	if (status != DBG_STATUS_OK)
		return status;

	*buf_size = qed_ilt_dump(p_hwfn, p_ptt, NULL, 0, false);

	return DBG_STATUS_OK;
}

static enum dbg_status qed_dbg_ilt_dump(struct qed_hwfn *p_hwfn,
					struct qed_ptt *p_ptt,
					u32 *dump_buf,
					u32 buf_size_in_dwords,
					u32 *num_dumped_dwords)
{
	*num_dumped_dwords = qed_ilt_dump(p_hwfn,
					  p_ptt,
					  dump_buf, buf_size_in_dwords, true);

	/* Reveret GRC params to their default */
	qed_dbg_grc_set_params_default(p_hwfn);

	return DBG_STATUS_OK;
}

enum dbg_status qed_dbg_read_attn(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  enum block_id block_id,
				  enum dbg_attn_type attn_type,
				  bool clear_status,
				  struct dbg_attn_block_result *results)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn);
	u8 reg_idx, num_attn_regs, num_result_regs = 0;
	const struct dbg_attn_reg *attn_reg_arr;

	if (status != DBG_STATUS_OK)
		return status;

	if (!p_hwfn->dbg_arrays[BIN_BUF_DBG_MODE_TREE].ptr ||
	    !p_hwfn->dbg_arrays[BIN_BUF_DBG_ATTN_BLOCKS].ptr ||
	    !p_hwfn->dbg_arrays[BIN_BUF_DBG_ATTN_REGS].ptr)
		return DBG_STATUS_DBG_ARRAY_NOT_SET;

	attn_reg_arr = qed_get_block_attn_regs(p_hwfn,
					       block_id,
					       attn_type, &num_attn_regs);

	for (reg_idx = 0; reg_idx < num_attn_regs; reg_idx++) {
		const struct dbg_attn_reg *reg_data = &attn_reg_arr[reg_idx];
		struct dbg_attn_reg_result *reg_result;
		u32 sts_addr, sts_val;
		u16 modes_buf_offset;
		bool eval_mode;

		/* Check mode */
		eval_mode = GET_FIELD(reg_data->mode.data,
				      DBG_MODE_HDR_EVAL_MODE) > 0;
		modes_buf_offset = GET_FIELD(reg_data->mode.data,
					     DBG_MODE_HDR_MODES_BUF_OFFSET);
		if (eval_mode && !qed_is_mode_match(p_hwfn, &modes_buf_offset))
			continue;

		/* Mode match - read attention status register */
		sts_addr = DWORDS_TO_BYTES(clear_status ?
					   reg_data->sts_clr_address :
					   GET_FIELD(reg_data->data,
						     DBG_ATTN_REG_STS_ADDRESS));
		sts_val = qed_rd(p_hwfn, p_ptt, sts_addr);
		if (!sts_val)
			continue;

		/* Non-zero attention status - add to results */
		reg_result = &results->reg_results[num_result_regs];
		SET_FIELD(reg_result->data,
			  DBG_ATTN_REG_RESULT_STS_ADDRESS, sts_addr);
		SET_FIELD(reg_result->data,
			  DBG_ATTN_REG_RESULT_NUM_REG_ATTN,
			  GET_FIELD(reg_data->data, DBG_ATTN_REG_NUM_REG_ATTN));
		reg_result->block_attn_offset = reg_data->block_attn_offset;
		reg_result->sts_val = sts_val;
		reg_result->mask_val = qed_rd(p_hwfn,
					      p_ptt,
					      DWORDS_TO_BYTES
					      (reg_data->mask_address));
		num_result_regs++;
	}

	results->block_id = (u8)block_id;
	results->names_offset =
	    qed_get_block_attn_data(p_hwfn, block_id, attn_type)->names_offset;
	SET_FIELD(results->data, DBG_ATTN_BLOCK_RESULT_ATTN_TYPE, attn_type);
	SET_FIELD(results->data,
		  DBG_ATTN_BLOCK_RESULT_NUM_REGS, num_result_regs);

	return DBG_STATUS_OK;
}

/******************************* Data Types **********************************/

/* REG fifo element */
struct reg_fifo_element {
	u64 data;
#define REG_FIFO_ELEMENT_ADDRESS_SHIFT		0
#define REG_FIFO_ELEMENT_ADDRESS_MASK		0x7fffff
#define REG_FIFO_ELEMENT_ACCESS_SHIFT		23
#define REG_FIFO_ELEMENT_ACCESS_MASK		0x1
#define REG_FIFO_ELEMENT_PF_SHIFT		24
#define REG_FIFO_ELEMENT_PF_MASK		0xf
#define REG_FIFO_ELEMENT_VF_SHIFT		28
#define REG_FIFO_ELEMENT_VF_MASK		0xff
#define REG_FIFO_ELEMENT_PORT_SHIFT		36
#define REG_FIFO_ELEMENT_PORT_MASK		0x3
#define REG_FIFO_ELEMENT_PRIVILEGE_SHIFT	38
#define REG_FIFO_ELEMENT_PRIVILEGE_MASK		0x3
#define REG_FIFO_ELEMENT_PROTECTION_SHIFT	40
#define REG_FIFO_ELEMENT_PROTECTION_MASK	0x7
#define REG_FIFO_ELEMENT_MASTER_SHIFT		43
#define REG_FIFO_ELEMENT_MASTER_MASK		0xf
#define REG_FIFO_ELEMENT_ERROR_SHIFT		47
#define REG_FIFO_ELEMENT_ERROR_MASK		0x1f
};

/* REG fifo error element */
struct reg_fifo_err {
	u32 err_code;
	const char *err_msg;
};

/* IGU fifo element */
struct igu_fifo_element {
	u32 dword0;
#define IGU_FIFO_ELEMENT_DWORD0_FID_SHIFT		0
#define IGU_FIFO_ELEMENT_DWORD0_FID_MASK		0xff
#define IGU_FIFO_ELEMENT_DWORD0_IS_PF_SHIFT		8
#define IGU_FIFO_ELEMENT_DWORD0_IS_PF_MASK		0x1
#define IGU_FIFO_ELEMENT_DWORD0_SOURCE_SHIFT		9
#define IGU_FIFO_ELEMENT_DWORD0_SOURCE_MASK		0xf
#define IGU_FIFO_ELEMENT_DWORD0_ERR_TYPE_SHIFT		13
#define IGU_FIFO_ELEMENT_DWORD0_ERR_TYPE_MASK		0xf
#define IGU_FIFO_ELEMENT_DWORD0_CMD_ADDR_SHIFT		17
#define IGU_FIFO_ELEMENT_DWORD0_CMD_ADDR_MASK		0x7fff
	u32 dword1;
	u32 dword2;
#define IGU_FIFO_ELEMENT_DWORD12_IS_WR_CMD_SHIFT	0
#define IGU_FIFO_ELEMENT_DWORD12_IS_WR_CMD_MASK		0x1
#define IGU_FIFO_ELEMENT_DWORD12_WR_DATA_SHIFT		1
#define IGU_FIFO_ELEMENT_DWORD12_WR_DATA_MASK		0xffffffff
	u32 reserved;
};

struct igu_fifo_wr_data {
	u32 data;
#define IGU_FIFO_WR_DATA_PROD_CONS_SHIFT		0
#define IGU_FIFO_WR_DATA_PROD_CONS_MASK			0xffffff
#define IGU_FIFO_WR_DATA_UPDATE_FLAG_SHIFT		24
#define IGU_FIFO_WR_DATA_UPDATE_FLAG_MASK		0x1
#define IGU_FIFO_WR_DATA_EN_DIS_INT_FOR_SB_SHIFT	25
#define IGU_FIFO_WR_DATA_EN_DIS_INT_FOR_SB_MASK		0x3
#define IGU_FIFO_WR_DATA_SEGMENT_SHIFT			27
#define IGU_FIFO_WR_DATA_SEGMENT_MASK			0x1
#define IGU_FIFO_WR_DATA_TIMER_MASK_SHIFT		28
#define IGU_FIFO_WR_DATA_TIMER_MASK_MASK		0x1
#define IGU_FIFO_WR_DATA_CMD_TYPE_SHIFT			31
#define IGU_FIFO_WR_DATA_CMD_TYPE_MASK			0x1
};

struct igu_fifo_cleanup_wr_data {
	u32 data;
#define IGU_FIFO_CLEANUP_WR_DATA_RESERVED_SHIFT		0
#define IGU_FIFO_CLEANUP_WR_DATA_RESERVED_MASK		0x7ffffff
#define IGU_FIFO_CLEANUP_WR_DATA_CLEANUP_VAL_SHIFT	27
#define IGU_FIFO_CLEANUP_WR_DATA_CLEANUP_VAL_MASK	0x1
#define IGU_FIFO_CLEANUP_WR_DATA_CLEANUP_TYPE_SHIFT	28
#define IGU_FIFO_CLEANUP_WR_DATA_CLEANUP_TYPE_MASK	0x7
#define IGU_FIFO_CLEANUP_WR_DATA_CMD_TYPE_SHIFT		31
#define IGU_FIFO_CLEANUP_WR_DATA_CMD_TYPE_MASK		0x1
};

/* Protection override element */
struct protection_override_element {
	u64 data;
#define PROTECTION_OVERRIDE_ELEMENT_ADDRESS_SHIFT		0
#define PROTECTION_OVERRIDE_ELEMENT_ADDRESS_MASK		0x7fffff
#define PROTECTION_OVERRIDE_ELEMENT_WINDOW_SIZE_SHIFT		23
#define PROTECTION_OVERRIDE_ELEMENT_WINDOW_SIZE_MASK		0xffffff
#define PROTECTION_OVERRIDE_ELEMENT_READ_SHIFT			47
#define PROTECTION_OVERRIDE_ELEMENT_READ_MASK			0x1
#define PROTECTION_OVERRIDE_ELEMENT_WRITE_SHIFT			48
#define PROTECTION_OVERRIDE_ELEMENT_WRITE_MASK			0x1
#define PROTECTION_OVERRIDE_ELEMENT_READ_PROTECTION_SHIFT	49
#define PROTECTION_OVERRIDE_ELEMENT_READ_PROTECTION_MASK	0x7
#define PROTECTION_OVERRIDE_ELEMENT_WRITE_PROTECTION_SHIFT	52
#define PROTECTION_OVERRIDE_ELEMENT_WRITE_PROTECTION_MASK	0x7
};

enum igu_fifo_sources {
	IGU_SRC_PXP0,
	IGU_SRC_PXP1,
	IGU_SRC_PXP2,
	IGU_SRC_PXP3,
	IGU_SRC_PXP4,
	IGU_SRC_PXP5,
	IGU_SRC_PXP6,
	IGU_SRC_PXP7,
	IGU_SRC_CAU,
	IGU_SRC_ATTN,
	IGU_SRC_GRC
};

enum igu_fifo_addr_types {
	IGU_ADDR_TYPE_MSIX_MEM,
	IGU_ADDR_TYPE_WRITE_PBA,
	IGU_ADDR_TYPE_WRITE_INT_ACK,
	IGU_ADDR_TYPE_WRITE_ATTN_BITS,
	IGU_ADDR_TYPE_READ_INT,
	IGU_ADDR_TYPE_WRITE_PROD_UPDATE,
	IGU_ADDR_TYPE_RESERVED
};

struct igu_fifo_addr_data {
	u16 start_addr;
	u16 end_addr;
	char *desc;
	char *vf_desc;
	enum igu_fifo_addr_types type;
};

/******************************** Constants **********************************/

#define MAX_MSG_LEN				1024

#define MCP_TRACE_MAX_MODULE_LEN		8
#define MCP_TRACE_FORMAT_MAX_PARAMS		3
#define MCP_TRACE_FORMAT_PARAM_WIDTH \
	(MCP_TRACE_FORMAT_P2_SIZE_OFFSET - MCP_TRACE_FORMAT_P1_SIZE_OFFSET)

#define REG_FIFO_ELEMENT_ADDR_FACTOR		4
#define REG_FIFO_ELEMENT_IS_PF_VF_VAL		127

#define PROTECTION_OVERRIDE_ELEMENT_ADDR_FACTOR	4

/***************************** Constant Arrays *******************************/

/* Status string array */
static const char * const s_status_str[] = {
	/* DBG_STATUS_OK */
	"Operation completed successfully",

	/* DBG_STATUS_APP_VERSION_NOT_SET */
	"Debug application version wasn't set",

	/* DBG_STATUS_UNSUPPORTED_APP_VERSION */
	"Unsupported debug application version",

	/* DBG_STATUS_DBG_BLOCK_NOT_RESET */
	"The debug block wasn't reset since the last recording",

	/* DBG_STATUS_INVALID_ARGS */
	"Invalid arguments",

	/* DBG_STATUS_OUTPUT_ALREADY_SET */
	"The debug output was already set",

	/* DBG_STATUS_INVALID_PCI_BUF_SIZE */
	"Invalid PCI buffer size",

	/* DBG_STATUS_PCI_BUF_ALLOC_FAILED */
	"PCI buffer allocation failed",

	/* DBG_STATUS_PCI_BUF_NOT_ALLOCATED */
	"A PCI buffer wasn't allocated",

	/* DBG_STATUS_INVALID_FILTER_TRIGGER_DWORDS */
	"The filter/trigger constraint dword offsets are not enabled for recording",
	/* DBG_STATUS_NO_MATCHING_FRAMING_MODE */
	"No matching framing mode",

	/* DBG_STATUS_VFC_READ_ERROR */
	"Error reading from VFC",

	/* DBG_STATUS_STORM_ALREADY_ENABLED */
	"The Storm was already enabled",

	/* DBG_STATUS_STORM_NOT_ENABLED */
	"The specified Storm wasn't enabled",

	/* DBG_STATUS_BLOCK_ALREADY_ENABLED */
	"The block was already enabled",

	/* DBG_STATUS_BLOCK_NOT_ENABLED */
	"The specified block wasn't enabled",

	/* DBG_STATUS_NO_INPUT_ENABLED */
	"No input was enabled for recording",

	/* DBG_STATUS_NO_FILTER_TRIGGER_256B */
	"Filters and triggers are not allowed in E4 256-bit mode",

	/* DBG_STATUS_FILTER_ALREADY_ENABLED */
	"The filter was already enabled",

	/* DBG_STATUS_TRIGGER_ALREADY_ENABLED */
	"The trigger was already enabled",

	/* DBG_STATUS_TRIGGER_NOT_ENABLED */
	"The trigger wasn't enabled",

	/* DBG_STATUS_CANT_ADD_CONSTRAINT */
	"A constraint can be added only after a filter was enabled or a trigger state was added",

	/* DBG_STATUS_TOO_MANY_TRIGGER_STATES */
	"Cannot add more than 3 trigger states",

	/* DBG_STATUS_TOO_MANY_CONSTRAINTS */
	"Cannot add more than 4 constraints per filter or trigger state",

	/* DBG_STATUS_RECORDING_NOT_STARTED */
	"The recording wasn't started",

	/* DBG_STATUS_DATA_DIDNT_TRIGGER */
	"A trigger was configured, but it didn't trigger",

	/* DBG_STATUS_NO_DATA_RECORDED */
	"No data was recorded",

	/* DBG_STATUS_DUMP_BUF_TOO_SMALL */
	"Dump buffer is too small",

	/* DBG_STATUS_DUMP_NOT_CHUNK_ALIGNED */
	"Dumped data is not aligned to chunks",

	/* DBG_STATUS_UNKNOWN_CHIP */
	"Unknown chip",

	/* DBG_STATUS_VIRT_MEM_ALLOC_FAILED */
	"Failed allocating virtual memory",

	/* DBG_STATUS_BLOCK_IN_RESET */
	"The input block is in reset",

	/* DBG_STATUS_INVALID_TRACE_SIGNATURE */
	"Invalid MCP trace signature found in NVRAM",

	/* DBG_STATUS_INVALID_NVRAM_BUNDLE */
	"Invalid bundle ID found in NVRAM",

	/* DBG_STATUS_NVRAM_GET_IMAGE_FAILED */
	"Failed getting NVRAM image",

	/* DBG_STATUS_NON_ALIGNED_NVRAM_IMAGE */
	"NVRAM image is not dword-aligned",

	/* DBG_STATUS_NVRAM_READ_FAILED */
	"Failed reading from NVRAM",

	/* DBG_STATUS_IDLE_CHK_PARSE_FAILED */
	"Idle check parsing failed",

	/* DBG_STATUS_MCP_TRACE_BAD_DATA */
	"MCP Trace data is corrupt",

	/* DBG_STATUS_MCP_TRACE_NO_META */
	"Dump doesn't contain meta data - it must be provided in image file",

	/* DBG_STATUS_MCP_COULD_NOT_HALT */
	"Failed to halt MCP",

	/* DBG_STATUS_MCP_COULD_NOT_RESUME */
	"Failed to resume MCP after halt",

	/* DBG_STATUS_RESERVED0 */
	"",

	/* DBG_STATUS_SEMI_FIFO_NOT_EMPTY */
	"Failed to empty SEMI sync FIFO",

	/* DBG_STATUS_IGU_FIFO_BAD_DATA */
	"IGU FIFO data is corrupt",

	/* DBG_STATUS_MCP_COULD_NOT_MASK_PRTY */
	"MCP failed to mask parities",

	/* DBG_STATUS_FW_ASSERTS_PARSE_FAILED */
	"FW Asserts parsing failed",

	/* DBG_STATUS_REG_FIFO_BAD_DATA */
	"GRC FIFO data is corrupt",

	/* DBG_STATUS_PROTECTION_OVERRIDE_BAD_DATA */
	"Protection Override data is corrupt",

	/* DBG_STATUS_DBG_ARRAY_NOT_SET */
	"Debug arrays were not set (when using binary files, dbg_set_bin_ptr must be called)",

	/* DBG_STATUS_RESERVED1 */
	"",

	/* DBG_STATUS_NON_MATCHING_LINES */
	"Non-matching debug lines - in E4, all lines must be of the same type (either 128b or 256b)",

	/* DBG_STATUS_INSUFFICIENT_HW_IDS */
	"Insufficient HW IDs. Try to record less Storms/blocks",

	/* DBG_STATUS_DBG_BUS_IN_USE */
	"The debug bus is in use",

	/* DBG_STATUS_INVALID_STORM_DBG_MODE */
	"The storm debug mode is not supported in the current chip",

	/* DBG_STATUS_OTHER_ENGINE_BB_ONLY */
	"Other engine is supported only in BB",

	/* DBG_STATUS_FILTER_SINGLE_HW_ID */
	"The configured filter mode requires a single Storm/block input",

	/* DBG_STATUS_TRIGGER_SINGLE_HW_ID */
	"The configured filter mode requires that all the constraints of a single trigger state will be defined on a single Storm/block input",

	/* DBG_STATUS_MISSING_TRIGGER_STATE_STORM */
	"When triggering on Storm data, the Storm to trigger on must be specified",

	/* DBG_STATUS_MDUMP2_FAILED_TO_REQUEST_OFFSIZE */
	"Failed to request MDUMP2 Offsize",

	/* DBG_STATUS_MDUMP2_FAILED_VALIDATION_OF_DATA_CRC */
	"Expected CRC (part of the MDUMP2 data) is different than the calculated CRC over that data",

	/* DBG_STATUS_MDUMP2_INVALID_SIGNATURE */
	"Invalid Signature found at start of MDUMP2",

	/* DBG_STATUS_MDUMP2_INVALID_LOG_SIZE */
	"Invalid Log Size of MDUMP2",

	/* DBG_STATUS_MDUMP2_INVALID_LOG_HDR */
	"Invalid Log Header of MDUMP2",

	/* DBG_STATUS_MDUMP2_INVALID_LOG_DATA */
	"Invalid Log Data of MDUMP2",

	/* DBG_STATUS_MDUMP2_ERROR_EXTRACTING_NUM_PORTS */
	"Could not extract number of ports from regval buf of MDUMP2",

	/* DBG_STATUS_MDUMP2_ERROR_EXTRACTING_MFW_STATUS */
	"Could not extract MFW (link) status from regval buf of MDUMP2",

	/* DBG_STATUS_MDUMP2_ERROR_DISPLAYING_LINKDUMP */
	"Could not display linkdump of MDUMP2",

	/* DBG_STATUS_MDUMP2_ERROR_READING_PHY_CFG */
	"Could not read PHY CFG of MDUMP2",

	/* DBG_STATUS_MDUMP2_ERROR_READING_PLL_MODE */
	"Could not read PLL Mode of MDUMP2",

	/* DBG_STATUS_MDUMP2_ERROR_READING_LANE_REGS */
	"Could not read TSCF/TSCE Lane Regs of MDUMP2",

	/* DBG_STATUS_MDUMP2_ERROR_ALLOCATING_BUF */
	"Could not allocate MDUMP2 reg-val internal buffer"
};

/* Idle check severity names array */
static const char * const s_idle_chk_severity_str[] = {
	"Error",
	"Error if no traffic",
	"Warning"
};

/* MCP Trace level names array */
static const char * const s_mcp_trace_level_str[] = {
	"ERROR",
	"TRACE",
	"DEBUG"
};

/* Access type names array */
static const char * const s_access_strs[] = {
	"read",
	"write"
};

/* Privilege type names array */
static const char * const s_privilege_strs[] = {
	"VF",
	"PDA",
	"HV",
	"UA"
};

/* Protection type names array */
static const char * const s_protection_strs[] = {
	"(default)",
	"(default)",
	"(default)",
	"(default)",
	"override VF",
	"override PDA",
	"override HV",
	"override UA"
};

/* Master type names array */
static const char * const s_master_strs[] = {
	"???",
	"pxp",
	"mcp",
	"msdm",
	"psdm",
	"ysdm",
	"usdm",
	"tsdm",
	"xsdm",
	"dbu",
	"dmae",
	"jdap",
	"???",
	"???",
	"???",
	"???"
};

/* REG FIFO error messages array */
static struct reg_fifo_err s_reg_fifo_errors[] = {
	{1, "grc timeout"},
	{2, "address doesn't belong to any block"},
	{4, "reserved address in block or write to read-only address"},
	{8, "privilege/protection mismatch"},
	{16, "path isolation error"},
	{17, "RSL error"}
};

/* IGU FIFO sources array */
static const char * const s_igu_fifo_source_strs[] = {
	"TSTORM",
	"MSTORM",
	"USTORM",
	"XSTORM",
	"YSTORM",
	"PSTORM",
	"PCIE",
	"NIG_QM_PBF",
	"CAU",
	"ATTN",
	"GRC",
};

/* IGU FIFO error messages */
static const char * const s_igu_fifo_error_strs[] = {
	"no error",
	"length error",
	"function disabled",
	"VF sent command to attention address",
	"host sent prod update command",
	"read of during interrupt register while in MIMD mode",
	"access to PXP BAR reserved address",
	"producer update command to attention index",
	"unknown error",
	"SB index not valid",
	"SB relative index and FID not found",
	"FID not match",
	"command with error flag asserted (PCI error or CAU discard)",
	"VF sent cleanup and RF cleanup is disabled",
	"cleanup command on type bigger than 4"
};

/* IGU FIFO address data */
static const struct igu_fifo_addr_data s_igu_fifo_addr_data[] = {
	{0x0, 0x101, "MSI-X Memory", NULL,
	 IGU_ADDR_TYPE_MSIX_MEM},
	{0x102, 0x1ff, "reserved", NULL,
	 IGU_ADDR_TYPE_RESERVED},
	{0x200, 0x200, "Write PBA[0:63]", NULL,
	 IGU_ADDR_TYPE_WRITE_PBA},
	{0x201, 0x201, "Write PBA[64:127]", "reserved",
	 IGU_ADDR_TYPE_WRITE_PBA},
	{0x202, 0x202, "Write PBA[128]", "reserved",
	 IGU_ADDR_TYPE_WRITE_PBA},
	{0x203, 0x3ff, "reserved", NULL,
	 IGU_ADDR_TYPE_RESERVED},
	{0x400, 0x5ef, "Write interrupt acknowledgment", NULL,
	 IGU_ADDR_TYPE_WRITE_INT_ACK},
	{0x5f0, 0x5f0, "Attention bits update", NULL,
	 IGU_ADDR_TYPE_WRITE_ATTN_BITS},
	{0x5f1, 0x5f1, "Attention bits set", NULL,
	 IGU_ADDR_TYPE_WRITE_ATTN_BITS},
	{0x5f2, 0x5f2, "Attention bits clear", NULL,
	 IGU_ADDR_TYPE_WRITE_ATTN_BITS},
	{0x5f3, 0x5f3, "Read interrupt 0:63 with mask", NULL,
	 IGU_ADDR_TYPE_READ_INT},
	{0x5f4, 0x5f4, "Read interrupt 0:31 with mask", NULL,
	 IGU_ADDR_TYPE_READ_INT},
	{0x5f5, 0x5f5, "Read interrupt 32:63 with mask", NULL,
	 IGU_ADDR_TYPE_READ_INT},
	{0x5f6, 0x5f6, "Read interrupt 0:63 without mask", NULL,
	 IGU_ADDR_TYPE_READ_INT},
	{0x5f7, 0x5ff, "reserved", NULL,
	 IGU_ADDR_TYPE_RESERVED},
	{0x600, 0x7ff, "Producer update", NULL,
	 IGU_ADDR_TYPE_WRITE_PROD_UPDATE}
};

/******************************** Variables **********************************/

/* Temporary buffer, used for print size calculations */
static char s_temp_buf[MAX_MSG_LEN];

/**************************** Private Functions ******************************/

static void qed_user_static_asserts(void)
{
}

static u32 qed_cyclic_add(u32 a, u32 b, u32 size)
{
	return (a + b) % size;
}

static u32 qed_cyclic_sub(u32 a, u32 b, u32 size)
{
	return (size + a - b) % size;
}

/* Reads the specified number of bytes from the specified cyclic buffer (up to 4
 * bytes) and returns them as a dword value. the specified buffer offset is
 * updated.
 */
static u32 qed_read_from_cyclic_buf(void *buf,
				    u32 *offset,
				    u32 buf_size, u8 num_bytes_to_read)
{
	u8 i, *val_ptr, *bytes_buf = (u8 *)buf;
	u32 val = 0;

	val_ptr = (u8 *)&val;

	/* Assume running on a LITTLE ENDIAN and the buffer is network order
	 * (BIG ENDIAN), as high order bytes are placed in lower memory address.
	 */
	for (i = 0; i < num_bytes_to_read; i++) {
		val_ptr[i] = bytes_buf[*offset];
		*offset = qed_cyclic_add(*offset, 1, buf_size);
	}

	return val;
}

/* Reads and returns the next byte from the specified buffer.
 * The specified buffer offset is updated.
 */
static u8 qed_read_byte_from_buf(void *buf, u32 *offset)
{
	return ((u8 *)buf)[(*offset)++];
}

/* Reads and returns the next dword from the specified buffer.
 * The specified buffer offset is updated.
 */
static u32 qed_read_dword_from_buf(void *buf, u32 *offset)
{
	u32 dword_val = *(u32 *)&((u8 *)buf)[*offset];

	*offset += 4;

	return dword_val;
}

/* Reads the next string from the specified buffer, and copies it to the
 * specified pointer. The specified buffer offset is updated.
 */
static void qed_read_str_from_buf(void *buf, u32 *offset, u32 size, char *dest)
{
	const char *source_str = &((const char *)buf)[*offset];

	strncpy(dest, source_str, size);
	dest[size - 1] = '\0';
	*offset += size;
}

/* Returns a pointer to the specified offset (in bytes) of the specified buffer.
 * If the specified buffer in NULL, a temporary buffer pointer is returned.
 */
static char *qed_get_buf_ptr(void *buf, u32 offset)
{
	return buf ? (char *)buf + offset : s_temp_buf;
}

/* Reads a param from the specified buffer. Returns the number of dwords read.
 * If the returned str_param is NULL, the param is numeric and its value is
 * returned in num_param.
 * Otheriwise, the param is a string and its pointer is returned in str_param.
 */
static u32 qed_read_param(u32 *dump_buf,
			  const char **param_name,
			  const char **param_str_val, u32 *param_num_val)
{
	char *char_buf = (char *)dump_buf;
	size_t offset = 0;

	/* Extract param name */
	*param_name = char_buf;
	offset += strlen(*param_name) + 1;

	/* Check param type */
	if (*(char_buf + offset++)) {
		/* String param */
		*param_str_val = char_buf + offset;
		*param_num_val = 0;
		offset += strlen(*param_str_val) + 1;
		if (offset & 0x3)
			offset += (4 - (offset & 0x3));
	} else {
		/* Numeric param */
		*param_str_val = NULL;
		if (offset & 0x3)
			offset += (4 - (offset & 0x3));
		*param_num_val = *(u32 *)(char_buf + offset);
		offset += 4;
	}

	return (u32)offset / 4;
}

/* Reads a section header from the specified buffer.
 * Returns the number of dwords read.
 */
static u32 qed_read_section_hdr(u32 *dump_buf,
				const char **section_name,
				u32 *num_section_params)
{
	const char *param_str_val;

	return qed_read_param(dump_buf,
			      section_name, &param_str_val, num_section_params);
}

/* Reads section params from the specified buffer and prints them to the results
 * buffer. Returns the number of dwords read.
 */
static u32 qed_print_section_params(u32 *dump_buf,
				    u32 num_section_params,
				    char *results_buf, u32 *num_chars_printed)
{
	u32 i, dump_offset = 0, results_offset = 0;

	for (i = 0; i < num_section_params; i++) {
		const char *param_name, *param_str_val;
		u32 param_num_val = 0;

		dump_offset += qed_read_param(dump_buf + dump_offset,
					      &param_name,
					      &param_str_val, &param_num_val);

		if (param_str_val)
			results_offset +=
				sprintf(qed_get_buf_ptr(results_buf,
							results_offset),
					"%s: %s\n", param_name, param_str_val);
		else if (strcmp(param_name, "fw-timestamp"))
			results_offset +=
				sprintf(qed_get_buf_ptr(results_buf,
							results_offset),
					"%s: %d\n", param_name, param_num_val);
	}

	results_offset += sprintf(qed_get_buf_ptr(results_buf, results_offset),
				  "\n");

	*num_chars_printed = results_offset;

	return dump_offset;
}

/* Returns the block name that matches the specified block ID,
 * or NULL if not found.
 */
static const char *qed_dbg_get_block_name(struct qed_hwfn *p_hwfn,
					  enum block_id block_id)
{
	const struct dbg_block_user *block =
	    (const struct dbg_block_user *)
	    p_hwfn->dbg_arrays[BIN_BUF_DBG_BLOCKS_USER_DATA].ptr + block_id;

	return (const char *)block->name;
}

static struct dbg_tools_user_data *qed_dbg_get_user_data(struct qed_hwfn
							 *p_hwfn)
{
	return (struct dbg_tools_user_data *)p_hwfn->dbg_user_info;
}

/* Parses the idle check rules and returns the number of characters printed.
 * In case of parsing error, returns 0.
 */
static u32 qed_parse_idle_chk_dump_rules(struct qed_hwfn *p_hwfn,
					 u32 *dump_buf,
					 u32 *dump_buf_end,
					 u32 num_rules,
					 bool print_fw_idle_chk,
					 char *results_buf,
					 u32 *num_errors, u32 *num_warnings)
{
	/* Offset in results_buf in bytes */
	u32 results_offset = 0;

	u32 rule_idx;
	u16 i, j;

	*num_errors = 0;
	*num_warnings = 0;

	/* Go over dumped results */
	for (rule_idx = 0; rule_idx < num_rules && dump_buf < dump_buf_end;
	     rule_idx++) {
		const struct dbg_idle_chk_rule_parsing_data *rule_parsing_data;
		struct dbg_idle_chk_result_hdr *hdr;
		const char *parsing_str, *lsi_msg;
		u32 parsing_str_offset;
		bool has_fw_msg;
		u8 curr_reg_id;

		hdr = (struct dbg_idle_chk_result_hdr *)dump_buf;
		rule_parsing_data =
		    (const struct dbg_idle_chk_rule_parsing_data *)
		    p_hwfn->dbg_arrays[BIN_BUF_DBG_IDLE_CHK_PARSING_DATA].ptr +
		    hdr->rule_id;
		parsing_str_offset =
		    GET_FIELD(rule_parsing_data->data,
			      DBG_IDLE_CHK_RULE_PARSING_DATA_STR_OFFSET);
		has_fw_msg =
		    GET_FIELD(rule_parsing_data->data,
			      DBG_IDLE_CHK_RULE_PARSING_DATA_HAS_FW_MSG) > 0;
		parsing_str = (const char *)
		    p_hwfn->dbg_arrays[BIN_BUF_DBG_PARSING_STRINGS].ptr +
		    parsing_str_offset;
		lsi_msg = parsing_str;
		curr_reg_id = 0;

		if (hdr->severity >= MAX_DBG_IDLE_CHK_SEVERITY_TYPES)
			return 0;

		/* Skip rule header */
		dump_buf += BYTES_TO_DWORDS(sizeof(*hdr));

		/* Update errors/warnings count */
		if (hdr->severity == IDLE_CHK_SEVERITY_ERROR ||
		    hdr->severity == IDLE_CHK_SEVERITY_ERROR_NO_TRAFFIC)
			(*num_errors)++;
		else
			(*num_warnings)++;

		/* Print rule severity */
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset), "%s: ",
			    s_idle_chk_severity_str[hdr->severity]);

		/* Print rule message */
		if (has_fw_msg)
			parsing_str += strlen(parsing_str) + 1;
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset), "%s.",
			    has_fw_msg &&
			    print_fw_idle_chk ? parsing_str : lsi_msg);
		parsing_str += strlen(parsing_str) + 1;

		/* Print register values */
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset), " Registers:");
		for (i = 0;
		     i < hdr->num_dumped_cond_regs + hdr->num_dumped_info_regs;
		     i++) {
			struct dbg_idle_chk_result_reg_hdr *reg_hdr;
			bool is_mem;
			u8 reg_id;

			reg_hdr =
				(struct dbg_idle_chk_result_reg_hdr *)dump_buf;
			is_mem = GET_FIELD(reg_hdr->data,
					   DBG_IDLE_CHK_RESULT_REG_HDR_IS_MEM);
			reg_id = GET_FIELD(reg_hdr->data,
					   DBG_IDLE_CHK_RESULT_REG_HDR_REG_ID);

			/* Skip reg header */
			dump_buf += BYTES_TO_DWORDS(sizeof(*reg_hdr));

			/* Skip register names until the required reg_id is
			 * reached.
			 */
			for (; reg_id > curr_reg_id; curr_reg_id++)
				parsing_str += strlen(parsing_str) + 1;

			results_offset +=
			    sprintf(qed_get_buf_ptr(results_buf,
						    results_offset), " %s",
				    parsing_str);
			if (i < hdr->num_dumped_cond_regs && is_mem)
				results_offset +=
				    sprintf(qed_get_buf_ptr(results_buf,
							    results_offset),
					    "[%d]", hdr->mem_entry_id +
					    reg_hdr->start_entry);
			results_offset +=
			    sprintf(qed_get_buf_ptr(results_buf,
						    results_offset), "=");
			for (j = 0; j < reg_hdr->size; j++, dump_buf++) {
				results_offset +=
				    sprintf(qed_get_buf_ptr(results_buf,
							    results_offset),
					    "0x%x", *dump_buf);
				if (j < reg_hdr->size - 1)
					results_offset +=
					    sprintf(qed_get_buf_ptr
						    (results_buf,
						     results_offset), ",");
			}
		}

		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf, results_offset), "\n");
	}

	/* Check if end of dump buffer was exceeded */
	if (dump_buf > dump_buf_end)
		return 0;

	return results_offset;
}

/* Parses an idle check dump buffer.
 * If result_buf is not NULL, the idle check results are printed to it.
 * In any case, the required results buffer size is assigned to
 * parsed_results_bytes.
 * The parsing status is returned.
 */
static enum dbg_status qed_parse_idle_chk_dump(struct qed_hwfn *p_hwfn,
					       u32 *dump_buf,
					       u32 num_dumped_dwords,
					       char *results_buf,
					       u32 *parsed_results_bytes,
					       u32 *num_errors,
					       u32 *num_warnings)
{
	u32 num_section_params = 0, num_rules, num_rules_not_dumped;
	const char *section_name, *param_name, *param_str_val;
	u32 *dump_buf_end = dump_buf + num_dumped_dwords;

	/* Offset in results_buf in bytes */
	u32 results_offset = 0;

	*parsed_results_bytes = 0;
	*num_errors = 0;
	*num_warnings = 0;

	if (!p_hwfn->dbg_arrays[BIN_BUF_DBG_PARSING_STRINGS].ptr ||
	    !p_hwfn->dbg_arrays[BIN_BUF_DBG_IDLE_CHK_PARSING_DATA].ptr)
		return DBG_STATUS_DBG_ARRAY_NOT_SET;

	/* Read global_params section */
	dump_buf += qed_read_section_hdr(dump_buf,
					 &section_name, &num_section_params);
	if (strcmp(section_name, "global_params"))
		return DBG_STATUS_IDLE_CHK_PARSE_FAILED;

	/* Print global params */
	dump_buf += qed_print_section_params(dump_buf,
					     num_section_params,
					     results_buf, &results_offset);

	/* Read idle_chk section
	 * There may be 1 or 2 idle_chk section parameters:
	 * - 1st is "num_rules"
	 * - 2nd is "num_rules_not_dumped" (optional)
	 */

	dump_buf += qed_read_section_hdr(dump_buf,
					 &section_name, &num_section_params);
	if (strcmp(section_name, "idle_chk") ||
	    (num_section_params != 2 && num_section_params != 1))
		return DBG_STATUS_IDLE_CHK_PARSE_FAILED;
	dump_buf += qed_read_param(dump_buf,
				   &param_name, &param_str_val, &num_rules);
	if (strcmp(param_name, "num_rules"))
		return DBG_STATUS_IDLE_CHK_PARSE_FAILED;
	if (num_section_params > 1) {
		dump_buf += qed_read_param(dump_buf,
					   &param_name,
					   &param_str_val,
					   &num_rules_not_dumped);
		if (strcmp(param_name, "num_rules_not_dumped"))
			return DBG_STATUS_IDLE_CHK_PARSE_FAILED;
	} else {
		num_rules_not_dumped = 0;
	}

	if (num_rules) {
		u32 rules_print_size;

		/* Print FW output */
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "FW_IDLE_CHECK:\n");
		rules_print_size =
			qed_parse_idle_chk_dump_rules(p_hwfn,
						      dump_buf,
						      dump_buf_end,
						      num_rules,
						      true,
						      results_buf ?
						      results_buf +
						      results_offset :
						      NULL,
						      num_errors,
						      num_warnings);
		results_offset += rules_print_size;
		if (!rules_print_size)
			return DBG_STATUS_IDLE_CHK_PARSE_FAILED;

		/* Print LSI output */
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "\nLSI_IDLE_CHECK:\n");
		rules_print_size =
			qed_parse_idle_chk_dump_rules(p_hwfn,
						      dump_buf,
						      dump_buf_end,
						      num_rules,
						      false,
						      results_buf ?
						      results_buf +
						      results_offset :
						      NULL,
						      num_errors,
						      num_warnings);
		results_offset += rules_print_size;
		if (!rules_print_size)
			return DBG_STATUS_IDLE_CHK_PARSE_FAILED;
	}

	/* Print errors/warnings count */
	if (*num_errors)
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "\nIdle Check failed!!! (with %d errors and %d warnings)\n",
			    *num_errors, *num_warnings);
	else if (*num_warnings)
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "\nIdle Check completed successfully (with %d warnings)\n",
			    *num_warnings);
	else
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "\nIdle Check completed successfully\n");

	if (num_rules_not_dumped)
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "\nIdle Check Partially dumped : num_rules_not_dumped = %d\n",
			    num_rules_not_dumped);

	/* Add 1 for string NULL termination */
	*parsed_results_bytes = results_offset + 1;

	return DBG_STATUS_OK;
}

/* Allocates and fills MCP Trace meta data based on the specified meta data
 * dump buffer.
 * Returns debug status code.
 */
static enum dbg_status
qed_mcp_trace_alloc_meta_data(struct qed_hwfn *p_hwfn,
			      const u32 *meta_buf)
{
	struct dbg_tools_user_data *dev_user_data;
	u32 offset = 0, signature, i;
	struct mcp_trace_meta *meta;
	u8 *meta_buf_bytes;

	dev_user_data = qed_dbg_get_user_data(p_hwfn);
	meta = &dev_user_data->mcp_trace_meta;
	meta_buf_bytes = (u8 *)meta_buf;

	/* Free the previous meta before loading a new one. */
	if (meta->is_allocated)
		qed_mcp_trace_free_meta_data(p_hwfn);

	memset(meta, 0, sizeof(*meta));

	/* Read first signature */
	signature = qed_read_dword_from_buf(meta_buf_bytes, &offset);
	if (signature != NVM_MAGIC_VALUE)
		return DBG_STATUS_INVALID_TRACE_SIGNATURE;

	/* Read no. of modules and allocate memory for their pointers */
	meta->modules_num = qed_read_byte_from_buf(meta_buf_bytes, &offset);
	meta->modules = kcalloc(meta->modules_num, sizeof(char *),
				GFP_KERNEL);
	if (!meta->modules)
		return DBG_STATUS_VIRT_MEM_ALLOC_FAILED;

	/* Allocate and read all module strings */
	for (i = 0; i < meta->modules_num; i++) {
		u8 module_len = qed_read_byte_from_buf(meta_buf_bytes, &offset);

		*(meta->modules + i) = kzalloc(module_len, GFP_KERNEL);
		if (!(*(meta->modules + i))) {
			/* Update number of modules to be released */
			meta->modules_num = i ? i - 1 : 0;
			return DBG_STATUS_VIRT_MEM_ALLOC_FAILED;
		}

		qed_read_str_from_buf(meta_buf_bytes, &offset, module_len,
				      *(meta->modules + i));
		if (module_len > MCP_TRACE_MAX_MODULE_LEN)
			(*(meta->modules + i))[MCP_TRACE_MAX_MODULE_LEN] = '\0';
	}

	/* Read second signature */
	signature = qed_read_dword_from_buf(meta_buf_bytes, &offset);
	if (signature != NVM_MAGIC_VALUE)
		return DBG_STATUS_INVALID_TRACE_SIGNATURE;

	/* Read number of formats and allocate memory for all formats */
	meta->formats_num = qed_read_dword_from_buf(meta_buf_bytes, &offset);
	meta->formats = kcalloc(meta->formats_num,
				sizeof(struct mcp_trace_format),
				GFP_KERNEL);
	if (!meta->formats)
		return DBG_STATUS_VIRT_MEM_ALLOC_FAILED;

	/* Allocate and read all strings */
	for (i = 0; i < meta->formats_num; i++) {
		struct mcp_trace_format *format_ptr = &meta->formats[i];
		u8 format_len;

		format_ptr->data = qed_read_dword_from_buf(meta_buf_bytes,
							   &offset);
		format_len = GET_MFW_FIELD(format_ptr->data,
					   MCP_TRACE_FORMAT_LEN);
		format_ptr->format_str = kzalloc(format_len, GFP_KERNEL);
		if (!format_ptr->format_str) {
			/* Update number of modules to be released */
			meta->formats_num = i ? i - 1 : 0;
			return DBG_STATUS_VIRT_MEM_ALLOC_FAILED;
		}

		qed_read_str_from_buf(meta_buf_bytes,
				      &offset,
				      format_len, format_ptr->format_str);
	}

	meta->is_allocated = true;
	return DBG_STATUS_OK;
}

/* Parses an MCP trace buffer. If result_buf is not NULL, the MCP Trace results
 * are printed to it. The parsing status is returned.
 * Arguments:
 * trace_buf - MCP trace cyclic buffer
 * trace_buf_size - MCP trace cyclic buffer size in bytes
 * data_offset - offset in bytes of the data to parse in the MCP trace cyclic
 *		 buffer.
 * data_size - size in bytes of data to parse.
 * parsed_buf - destination buffer for parsed data.
 * parsed_results_bytes - size of parsed data in bytes.
 */
static enum dbg_status qed_parse_mcp_trace_buf(struct qed_hwfn *p_hwfn,
					       u8 *trace_buf,
					       u32 trace_buf_size,
					       u32 data_offset,
					       u32 data_size,
					       char *parsed_buf,
					       u32 *parsed_results_bytes)
{
	struct dbg_tools_user_data *dev_user_data;
	struct mcp_trace_meta *meta;
	u32 param_mask, param_shift;
	enum dbg_status status;

	dev_user_data = qed_dbg_get_user_data(p_hwfn);
	meta = &dev_user_data->mcp_trace_meta;
	*parsed_results_bytes = 0;

	if (!meta->is_allocated)
		return DBG_STATUS_MCP_TRACE_BAD_DATA;

	status = DBG_STATUS_OK;

	while (data_size) {
		struct mcp_trace_format *format_ptr;
		u8 format_level, format_module;
		u32 params[3] = { 0, 0, 0 };
		u32 header, format_idx, i;

		if (data_size < MFW_TRACE_ENTRY_SIZE)
			return DBG_STATUS_MCP_TRACE_BAD_DATA;

		header = qed_read_from_cyclic_buf(trace_buf,
						  &data_offset,
						  trace_buf_size,
						  MFW_TRACE_ENTRY_SIZE);
		data_size -= MFW_TRACE_ENTRY_SIZE;
		format_idx = header & MFW_TRACE_EVENTID_MASK;

		/* Skip message if its index doesn't exist in the meta data */
		if (format_idx >= meta->formats_num) {
			u8 format_size = (u8)GET_MFW_FIELD(header,
							   MFW_TRACE_PRM_SIZE);

			if (data_size < format_size)
				return DBG_STATUS_MCP_TRACE_BAD_DATA;

			data_offset = qed_cyclic_add(data_offset,
						     format_size,
						     trace_buf_size);
			data_size -= format_size;
			continue;
		}

		format_ptr = &meta->formats[format_idx];

		for (i = 0,
		     param_mask = MCP_TRACE_FORMAT_P1_SIZE_MASK, param_shift =
		     MCP_TRACE_FORMAT_P1_SIZE_OFFSET;
		     i < MCP_TRACE_FORMAT_MAX_PARAMS;
		     i++, param_mask <<= MCP_TRACE_FORMAT_PARAM_WIDTH,
		     param_shift += MCP_TRACE_FORMAT_PARAM_WIDTH) {
			/* Extract param size (0..3) */
			u8 param_size = (u8)((format_ptr->data & param_mask) >>
					     param_shift);

			/* If the param size is zero, there are no other
			 * parameters.
			 */
			if (!param_size)
				break;

			/* Size is encoded using 2 bits, where 3 is used to
			 * encode 4.
			 */
			if (param_size == 3)
				param_size = 4;

			if (data_size < param_size)
				return DBG_STATUS_MCP_TRACE_BAD_DATA;

			params[i] = qed_read_from_cyclic_buf(trace_buf,
							     &data_offset,
							     trace_buf_size,
							     param_size);
			data_size -= param_size;
		}

		format_level = (u8)GET_MFW_FIELD(format_ptr->data,
						 MCP_TRACE_FORMAT_LEVEL);
		format_module = (u8)GET_MFW_FIELD(format_ptr->data,
						  MCP_TRACE_FORMAT_MODULE);
		if (format_level >= ARRAY_SIZE(s_mcp_trace_level_str))
			return DBG_STATUS_MCP_TRACE_BAD_DATA;

		/* Print current message to results buffer */
		*parsed_results_bytes +=
			sprintf(qed_get_buf_ptr(parsed_buf,
						*parsed_results_bytes),
				"%s %-8s: ",
				s_mcp_trace_level_str[format_level],
				meta->modules[format_module]);
		*parsed_results_bytes +=
		    sprintf(qed_get_buf_ptr(parsed_buf, *parsed_results_bytes),
			    format_ptr->format_str,
			    params[0], params[1], params[2]);
	}

	/* Add string NULL terminator */
	(*parsed_results_bytes)++;

	return status;
}

/* Parses an MCP Trace dump buffer.
 * If result_buf is not NULL, the MCP Trace results are printed to it.
 * In any case, the required results buffer size is assigned to
 * parsed_results_bytes.
 * The parsing status is returned.
 */
static enum dbg_status qed_parse_mcp_trace_dump(struct qed_hwfn *p_hwfn,
						u32 *dump_buf,
						char *results_buf,
						u32 *parsed_results_bytes,
						bool free_meta_data)
{
	const char *section_name, *param_name, *param_str_val;
	u32 data_size, trace_data_dwords, trace_meta_dwords;
	u32 offset, results_offset, results_buf_bytes;
	u32 param_num_val, num_section_params;
	struct mcp_trace *trace;
	enum dbg_status status;
	const u32 *meta_buf;
	u8 *trace_buf;

	*parsed_results_bytes = 0;

	/* Read global_params section */
	dump_buf += qed_read_section_hdr(dump_buf,
					 &section_name, &num_section_params);
	if (strcmp(section_name, "global_params"))
		return DBG_STATUS_MCP_TRACE_BAD_DATA;

	/* Print global params */
	dump_buf += qed_print_section_params(dump_buf,
					     num_section_params,
					     results_buf, &results_offset);

	/* Read trace_data section */
	dump_buf += qed_read_section_hdr(dump_buf,
					 &section_name, &num_section_params);
	if (strcmp(section_name, "mcp_trace_data") || num_section_params != 1)
		return DBG_STATUS_MCP_TRACE_BAD_DATA;
	dump_buf += qed_read_param(dump_buf,
				   &param_name, &param_str_val, &param_num_val);
	if (strcmp(param_name, "size"))
		return DBG_STATUS_MCP_TRACE_BAD_DATA;
	trace_data_dwords = param_num_val;

	/* Prepare trace info */
	trace = (struct mcp_trace *)dump_buf;
	if (trace->signature != MFW_TRACE_SIGNATURE || !trace->size)
		return DBG_STATUS_MCP_TRACE_BAD_DATA;

	trace_buf = (u8 *)dump_buf + sizeof(*trace);
	offset = trace->trace_oldest;
	data_size = qed_cyclic_sub(trace->trace_prod, offset, trace->size);
	dump_buf += trace_data_dwords;

	/* Read meta_data section */
	dump_buf += qed_read_section_hdr(dump_buf,
					 &section_name, &num_section_params);
	if (strcmp(section_name, "mcp_trace_meta"))
		return DBG_STATUS_MCP_TRACE_BAD_DATA;
	dump_buf += qed_read_param(dump_buf,
				   &param_name, &param_str_val, &param_num_val);
	if (strcmp(param_name, "size"))
		return DBG_STATUS_MCP_TRACE_BAD_DATA;
	trace_meta_dwords = param_num_val;

	/* Choose meta data buffer */
	if (!trace_meta_dwords) {
		/* Dump doesn't include meta data */
		struct dbg_tools_user_data *dev_user_data =
			qed_dbg_get_user_data(p_hwfn);

		if (!dev_user_data->mcp_trace_user_meta_buf)
			return DBG_STATUS_MCP_TRACE_NO_META;

		meta_buf = dev_user_data->mcp_trace_user_meta_buf;
	} else {
		/* Dump includes meta data */
		meta_buf = dump_buf;
	}

	/* Allocate meta data memory */
	status = qed_mcp_trace_alloc_meta_data(p_hwfn, meta_buf);
	if (status != DBG_STATUS_OK)
		return status;

	status = qed_parse_mcp_trace_buf(p_hwfn,
					 trace_buf,
					 trace->size,
					 offset,
					 data_size,
					 results_buf ?
					 results_buf + results_offset :
					 NULL,
					 &results_buf_bytes);
	if (status != DBG_STATUS_OK)
		return status;

	if (free_meta_data)
		qed_mcp_trace_free_meta_data(p_hwfn);

	*parsed_results_bytes = results_offset + results_buf_bytes;

	return DBG_STATUS_OK;
}

/* Parses a Reg FIFO dump buffer.
 * If result_buf is not NULL, the Reg FIFO results are printed to it.
 * In any case, the required results buffer size is assigned to
 * parsed_results_bytes.
 * The parsing status is returned.
 */
static enum dbg_status qed_parse_reg_fifo_dump(u32 *dump_buf,
					       char *results_buf,
					       u32 *parsed_results_bytes)
{
	const char *section_name, *param_name, *param_str_val;
	u32 param_num_val, num_section_params, num_elements;
	struct reg_fifo_element *elements;
	u8 i, j, err_code, vf_val;
	u32 results_offset = 0;
	char vf_str[4];

	/* Read global_params section */
	dump_buf += qed_read_section_hdr(dump_buf,
					 &section_name, &num_section_params);
	if (strcmp(section_name, "global_params"))
		return DBG_STATUS_REG_FIFO_BAD_DATA;

	/* Print global params */
	dump_buf += qed_print_section_params(dump_buf,
					     num_section_params,
					     results_buf, &results_offset);

	/* Read reg_fifo_data section */
	dump_buf += qed_read_section_hdr(dump_buf,
					 &section_name, &num_section_params);
	if (strcmp(section_name, "reg_fifo_data"))
		return DBG_STATUS_REG_FIFO_BAD_DATA;
	dump_buf += qed_read_param(dump_buf,
				   &param_name, &param_str_val, &param_num_val);
	if (strcmp(param_name, "size"))
		return DBG_STATUS_REG_FIFO_BAD_DATA;
	if (param_num_val % REG_FIFO_ELEMENT_DWORDS)
		return DBG_STATUS_REG_FIFO_BAD_DATA;
	num_elements = param_num_val / REG_FIFO_ELEMENT_DWORDS;
	elements = (struct reg_fifo_element *)dump_buf;

	/* Decode elements */
	for (i = 0; i < num_elements; i++) {
		const char *err_msg = NULL;

		/* Discover if element belongs to a VF or a PF */
		vf_val = GET_FIELD(elements[i].data, REG_FIFO_ELEMENT_VF);
		if (vf_val == REG_FIFO_ELEMENT_IS_PF_VF_VAL)
			sprintf(vf_str, "%s", "N/A");
		else
			sprintf(vf_str, "%d", vf_val);

		/* Find error message */
		err_code = GET_FIELD(elements[i].data, REG_FIFO_ELEMENT_ERROR);
		for (j = 0; j < ARRAY_SIZE(s_reg_fifo_errors) && !err_msg; j++)
			if (err_code == s_reg_fifo_errors[j].err_code)
				err_msg = s_reg_fifo_errors[j].err_msg;

		/* Add parsed element to parsed buffer */
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "raw: 0x%016llx, address: 0x%07x, access: %-5s, pf: %2d, vf: %s, port: %d, privilege: %-3s, protection: %-12s, master: %-4s, error: %s\n",
			    elements[i].data,
			    (u32)GET_FIELD(elements[i].data,
					   REG_FIFO_ELEMENT_ADDRESS) *
			    REG_FIFO_ELEMENT_ADDR_FACTOR,
			    s_access_strs[GET_FIELD(elements[i].data,
						    REG_FIFO_ELEMENT_ACCESS)],
			    (u32)GET_FIELD(elements[i].data,
					   REG_FIFO_ELEMENT_PF),
			    vf_str,
			    (u32)GET_FIELD(elements[i].data,
					   REG_FIFO_ELEMENT_PORT),
			    s_privilege_strs[GET_FIELD(elements[i].data,
						REG_FIFO_ELEMENT_PRIVILEGE)],
			    s_protection_strs[GET_FIELD(elements[i].data,
						REG_FIFO_ELEMENT_PROTECTION)],
			    s_master_strs[GET_FIELD(elements[i].data,
						    REG_FIFO_ELEMENT_MASTER)],
			    err_msg ? err_msg : "unknown error code");
	}

	results_offset += sprintf(qed_get_buf_ptr(results_buf,
						  results_offset),
				  "fifo contained %d elements", num_elements);

	/* Add 1 for string NULL termination */
	*parsed_results_bytes = results_offset + 1;

	return DBG_STATUS_OK;
}

static enum dbg_status qed_parse_igu_fifo_element(struct igu_fifo_element
						  *element, char
						  *results_buf,
						  u32 *results_offset)
{
	const struct igu_fifo_addr_data *found_addr = NULL;
	u8 source, err_type, i, is_cleanup;
	char parsed_addr_data[32];
	char parsed_wr_data[256];
	u32 wr_data, prod_cons;
	bool is_wr_cmd, is_pf;
	u16 cmd_addr;
	u64 dword12;

	/* Dword12 (dword index 1 and 2) contains bits 32..95 of the
	 * FIFO element.
	 */
	dword12 = ((u64)element->dword2 << 32) | element->dword1;
	is_wr_cmd = GET_FIELD(dword12, IGU_FIFO_ELEMENT_DWORD12_IS_WR_CMD);
	is_pf = GET_FIELD(element->dword0, IGU_FIFO_ELEMENT_DWORD0_IS_PF);
	cmd_addr = GET_FIELD(element->dword0, IGU_FIFO_ELEMENT_DWORD0_CMD_ADDR);
	source = GET_FIELD(element->dword0, IGU_FIFO_ELEMENT_DWORD0_SOURCE);
	err_type = GET_FIELD(element->dword0, IGU_FIFO_ELEMENT_DWORD0_ERR_TYPE);

	if (source >= ARRAY_SIZE(s_igu_fifo_source_strs))
		return DBG_STATUS_IGU_FIFO_BAD_DATA;
	if (err_type >= ARRAY_SIZE(s_igu_fifo_error_strs))
		return DBG_STATUS_IGU_FIFO_BAD_DATA;

	/* Find address data */
	for (i = 0; i < ARRAY_SIZE(s_igu_fifo_addr_data) && !found_addr; i++) {
		const struct igu_fifo_addr_data *curr_addr =
			&s_igu_fifo_addr_data[i];

		if (cmd_addr >= curr_addr->start_addr && cmd_addr <=
		    curr_addr->end_addr)
			found_addr = curr_addr;
	}

	if (!found_addr)
		return DBG_STATUS_IGU_FIFO_BAD_DATA;

	/* Prepare parsed address data */
	switch (found_addr->type) {
	case IGU_ADDR_TYPE_MSIX_MEM:
		sprintf(parsed_addr_data, " vector_num = 0x%x", cmd_addr / 2);
		break;
	case IGU_ADDR_TYPE_WRITE_INT_ACK:
	case IGU_ADDR_TYPE_WRITE_PROD_UPDATE:
		sprintf(parsed_addr_data,
			" SB = 0x%x", cmd_addr - found_addr->start_addr);
		break;
	default:
		parsed_addr_data[0] = '\0';
	}

	if (!is_wr_cmd) {
		parsed_wr_data[0] = '\0';
		goto out;
	}

	/* Prepare parsed write data */
	wr_data = GET_FIELD(dword12, IGU_FIFO_ELEMENT_DWORD12_WR_DATA);
	prod_cons = GET_FIELD(wr_data, IGU_FIFO_WR_DATA_PROD_CONS);
	is_cleanup = GET_FIELD(wr_data, IGU_FIFO_WR_DATA_CMD_TYPE);

	if (source == IGU_SRC_ATTN) {
		sprintf(parsed_wr_data, "prod: 0x%x, ", prod_cons);
	} else {
		if (is_cleanup) {
			u8 cleanup_val, cleanup_type;

			cleanup_val =
				GET_FIELD(wr_data,
					  IGU_FIFO_CLEANUP_WR_DATA_CLEANUP_VAL);
			cleanup_type =
			    GET_FIELD(wr_data,
				      IGU_FIFO_CLEANUP_WR_DATA_CLEANUP_TYPE);

			sprintf(parsed_wr_data,
				"cmd_type: cleanup, cleanup_val: %s, cleanup_type : %d, ",
				cleanup_val ? "set" : "clear",
				cleanup_type);
		} else {
			u8 update_flag, en_dis_int_for_sb, segment;
			u8 timer_mask;

			update_flag = GET_FIELD(wr_data,
						IGU_FIFO_WR_DATA_UPDATE_FLAG);
			en_dis_int_for_sb =
				GET_FIELD(wr_data,
					  IGU_FIFO_WR_DATA_EN_DIS_INT_FOR_SB);
			segment = GET_FIELD(wr_data,
					    IGU_FIFO_WR_DATA_SEGMENT);
			timer_mask = GET_FIELD(wr_data,
					       IGU_FIFO_WR_DATA_TIMER_MASK);

			sprintf(parsed_wr_data,
				"cmd_type: prod/cons update, prod/cons: 0x%x, update_flag: %s, en_dis_int_for_sb : %s, segment : %s, timer_mask = %d, ",
				prod_cons,
				update_flag ? "update" : "nop",
				en_dis_int_for_sb ?
				(en_dis_int_for_sb == 1 ? "disable" : "nop") :
				"enable",
				segment ? "attn" : "regular",
				timer_mask);
		}
	}
out:
	/* Add parsed element to parsed buffer */
	*results_offset += sprintf(qed_get_buf_ptr(results_buf,
						   *results_offset),
				   "raw: 0x%01x%08x%08x, %s: %d, source : %s, type : %s, cmd_addr : 0x%x(%s%s), %serror: %s\n",
				   element->dword2, element->dword1,
				   element->dword0,
				   is_pf ? "pf" : "vf",
				   GET_FIELD(element->dword0,
					     IGU_FIFO_ELEMENT_DWORD0_FID),
				   s_igu_fifo_source_strs[source],
				   is_wr_cmd ? "wr" : "rd",
				   cmd_addr,
				   (!is_pf && found_addr->vf_desc)
				   ? found_addr->vf_desc
				   : found_addr->desc,
				   parsed_addr_data,
				   parsed_wr_data,
				   s_igu_fifo_error_strs[err_type]);

	return DBG_STATUS_OK;
}

/* Parses an IGU FIFO dump buffer.
 * If result_buf is not NULL, the IGU FIFO results are printed to it.
 * In any case, the required results buffer size is assigned to
 * parsed_results_bytes.
 * The parsing status is returned.
 */
static enum dbg_status qed_parse_igu_fifo_dump(u32 *dump_buf,
					       char *results_buf,
					       u32 *parsed_results_bytes)
{
	const char *section_name, *param_name, *param_str_val;
	u32 param_num_val, num_section_params, num_elements;
	struct igu_fifo_element *elements;
	enum dbg_status status;
	u32 results_offset = 0;
	u8 i;

	/* Read global_params section */
	dump_buf += qed_read_section_hdr(dump_buf,
					 &section_name, &num_section_params);
	if (strcmp(section_name, "global_params"))
		return DBG_STATUS_IGU_FIFO_BAD_DATA;

	/* Print global params */
	dump_buf += qed_print_section_params(dump_buf,
					     num_section_params,
					     results_buf, &results_offset);

	/* Read igu_fifo_data section */
	dump_buf += qed_read_section_hdr(dump_buf,
					 &section_name, &num_section_params);
	if (strcmp(section_name, "igu_fifo_data"))
		return DBG_STATUS_IGU_FIFO_BAD_DATA;
	dump_buf += qed_read_param(dump_buf,
				   &param_name, &param_str_val, &param_num_val);
	if (strcmp(param_name, "size"))
		return DBG_STATUS_IGU_FIFO_BAD_DATA;
	if (param_num_val % IGU_FIFO_ELEMENT_DWORDS)
		return DBG_STATUS_IGU_FIFO_BAD_DATA;
	num_elements = param_num_val / IGU_FIFO_ELEMENT_DWORDS;
	elements = (struct igu_fifo_element *)dump_buf;

	/* Decode elements */
	for (i = 0; i < num_elements; i++) {
		status = qed_parse_igu_fifo_element(&elements[i],
						    results_buf,
						    &results_offset);
		if (status != DBG_STATUS_OK)
			return status;
	}

	results_offset += sprintf(qed_get_buf_ptr(results_buf,
						  results_offset),
				  "fifo contained %d elements", num_elements);

	/* Add 1 for string NULL termination */
	*parsed_results_bytes = results_offset + 1;

	return DBG_STATUS_OK;
}

static enum dbg_status
qed_parse_protection_override_dump(u32 *dump_buf,
				   char *results_buf,
				   u32 *parsed_results_bytes)
{
	const char *section_name, *param_name, *param_str_val;
	u32 param_num_val, num_section_params, num_elements;
	struct protection_override_element *elements;
	u32 results_offset = 0;
	u8 i;

	/* Read global_params section */
	dump_buf += qed_read_section_hdr(dump_buf,
					 &section_name, &num_section_params);
	if (strcmp(section_name, "global_params"))
		return DBG_STATUS_PROTECTION_OVERRIDE_BAD_DATA;

	/* Print global params */
	dump_buf += qed_print_section_params(dump_buf,
					     num_section_params,
					     results_buf, &results_offset);

	/* Read protection_override_data section */
	dump_buf += qed_read_section_hdr(dump_buf,
					 &section_name, &num_section_params);
	if (strcmp(section_name, "protection_override_data"))
		return DBG_STATUS_PROTECTION_OVERRIDE_BAD_DATA;
	dump_buf += qed_read_param(dump_buf,
				   &param_name, &param_str_val, &param_num_val);
	if (strcmp(param_name, "size"))
		return DBG_STATUS_PROTECTION_OVERRIDE_BAD_DATA;
	if (param_num_val % PROTECTION_OVERRIDE_ELEMENT_DWORDS)
		return DBG_STATUS_PROTECTION_OVERRIDE_BAD_DATA;
	num_elements = param_num_val / PROTECTION_OVERRIDE_ELEMENT_DWORDS;
	elements = (struct protection_override_element *)dump_buf;

	/* Decode elements */
	for (i = 0; i < num_elements; i++) {
		u32 address = GET_FIELD(elements[i].data,
					PROTECTION_OVERRIDE_ELEMENT_ADDRESS) *
			      PROTECTION_OVERRIDE_ELEMENT_ADDR_FACTOR;

		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "window %2d, address: 0x%07x, size: %7d regs, read: %d, write: %d, read protection: %-12s, write protection: %-12s\n",
			    i, address,
			    (u32)GET_FIELD(elements[i].data,
				      PROTECTION_OVERRIDE_ELEMENT_WINDOW_SIZE),
			    (u32)GET_FIELD(elements[i].data,
				      PROTECTION_OVERRIDE_ELEMENT_READ),
			    (u32)GET_FIELD(elements[i].data,
				      PROTECTION_OVERRIDE_ELEMENT_WRITE),
			    s_protection_strs[GET_FIELD(elements[i].data,
				PROTECTION_OVERRIDE_ELEMENT_READ_PROTECTION)],
			    s_protection_strs[GET_FIELD(elements[i].data,
				PROTECTION_OVERRIDE_ELEMENT_WRITE_PROTECTION)]);
	}

	results_offset += sprintf(qed_get_buf_ptr(results_buf,
						  results_offset),
				  "protection override contained %d elements",
				  num_elements);

	/* Add 1 for string NULL termination */
	*parsed_results_bytes = results_offset + 1;

	return DBG_STATUS_OK;
}

/* Parses a FW Asserts dump buffer.
 * If result_buf is not NULL, the FW Asserts results are printed to it.
 * In any case, the required results buffer size is assigned to
 * parsed_results_bytes.
 * The parsing status is returned.
 */
static enum dbg_status qed_parse_fw_asserts_dump(u32 *dump_buf,
						 char *results_buf,
						 u32 *parsed_results_bytes)
{
	u32 num_section_params, param_num_val, i, results_offset = 0;
	const char *param_name, *param_str_val, *section_name;
	bool last_section_found = false;

	*parsed_results_bytes = 0;

	/* Read global_params section */
	dump_buf += qed_read_section_hdr(dump_buf,
					 &section_name, &num_section_params);
	if (strcmp(section_name, "global_params"))
		return DBG_STATUS_FW_ASSERTS_PARSE_FAILED;

	/* Print global params */
	dump_buf += qed_print_section_params(dump_buf,
					     num_section_params,
					     results_buf, &results_offset);

	while (!last_section_found) {
		dump_buf += qed_read_section_hdr(dump_buf,
						 &section_name,
						 &num_section_params);
		if (!strcmp(section_name, "fw_asserts")) {
			/* Extract params */
			const char *storm_letter = NULL;
			u32 storm_dump_size = 0;

			for (i = 0; i < num_section_params; i++) {
				dump_buf += qed_read_param(dump_buf,
							   &param_name,
							   &param_str_val,
							   &param_num_val);
				if (!strcmp(param_name, "storm"))
					storm_letter = param_str_val;
				else if (!strcmp(param_name, "size"))
					storm_dump_size = param_num_val;
				else
					return
					    DBG_STATUS_FW_ASSERTS_PARSE_FAILED;
			}

			if (!storm_letter || !storm_dump_size)
				return DBG_STATUS_FW_ASSERTS_PARSE_FAILED;

			/* Print data */
			results_offset +=
			    sprintf(qed_get_buf_ptr(results_buf,
						    results_offset),
				    "\n%sSTORM_ASSERT: size=%d\n",
				    storm_letter, storm_dump_size);
			for (i = 0; i < storm_dump_size; i++, dump_buf++)
				results_offset +=
				    sprintf(qed_get_buf_ptr(results_buf,
							    results_offset),
					    "%08x\n", *dump_buf);
		} else if (!strcmp(section_name, "last")) {
			last_section_found = true;
		} else {
			return DBG_STATUS_FW_ASSERTS_PARSE_FAILED;
		}
	}

	/* Add 1 for string NULL termination */
	*parsed_results_bytes = results_offset + 1;

	return DBG_STATUS_OK;
}

/***************************** Public Functions *******************************/

enum dbg_status qed_dbg_user_set_bin_ptr(struct qed_hwfn *p_hwfn,
					 const u8 * const bin_ptr)
{
	struct bin_buffer_hdr *buf_hdrs = (struct bin_buffer_hdr *)bin_ptr;
	u8 buf_id;

	/* Convert binary data to debug arrays */
	for (buf_id = 0; buf_id < MAX_BIN_DBG_BUFFER_TYPE; buf_id++)
		qed_set_dbg_bin_buf(p_hwfn,
				    (enum bin_dbg_buffer_type)buf_id,
				    (u32 *)(bin_ptr + buf_hdrs[buf_id].offset),
				    buf_hdrs[buf_id].length);

	return DBG_STATUS_OK;
}

enum dbg_status qed_dbg_alloc_user_data(struct qed_hwfn *p_hwfn,
					void **user_data_ptr)
{
	*user_data_ptr = kzalloc(sizeof(struct dbg_tools_user_data),
				 GFP_KERNEL);
	if (!(*user_data_ptr))
		return DBG_STATUS_VIRT_MEM_ALLOC_FAILED;

	return DBG_STATUS_OK;
}

const char *qed_dbg_get_status_str(enum dbg_status status)
{
	return (status <
		MAX_DBG_STATUS) ? s_status_str[status] : "Invalid debug status";
}

enum dbg_status qed_get_idle_chk_results_buf_size(struct qed_hwfn *p_hwfn,
						  u32 *dump_buf,
						  u32 num_dumped_dwords,
						  u32 *results_buf_size)
{
	u32 num_errors, num_warnings;

	return qed_parse_idle_chk_dump(p_hwfn,
				       dump_buf,
				       num_dumped_dwords,
				       NULL,
				       results_buf_size,
				       &num_errors, &num_warnings);
}

enum dbg_status qed_print_idle_chk_results(struct qed_hwfn *p_hwfn,
					   u32 *dump_buf,
					   u32 num_dumped_dwords,
					   char *results_buf,
					   u32 *num_errors,
					   u32 *num_warnings)
{
	u32 parsed_buf_size;

	return qed_parse_idle_chk_dump(p_hwfn,
				       dump_buf,
				       num_dumped_dwords,
				       results_buf,
				       &parsed_buf_size,
				       num_errors, num_warnings);
}

void qed_dbg_mcp_trace_set_meta_data(struct qed_hwfn *p_hwfn,
				     const u32 *meta_buf)
{
	struct dbg_tools_user_data *dev_user_data =
		qed_dbg_get_user_data(p_hwfn);

	dev_user_data->mcp_trace_user_meta_buf = meta_buf;
}

enum dbg_status qed_get_mcp_trace_results_buf_size(struct qed_hwfn *p_hwfn,
						   u32 *dump_buf,
						   u32 num_dumped_dwords,
						   u32 *results_buf_size)
{
	return qed_parse_mcp_trace_dump(p_hwfn,
					dump_buf, NULL, results_buf_size, true);
}

enum dbg_status qed_print_mcp_trace_results(struct qed_hwfn *p_hwfn,
					    u32 *dump_buf,
					    u32 num_dumped_dwords,
					    char *results_buf)
{
	u32 parsed_buf_size;

	/* Doesn't do anything, needed for compile time asserts */
	qed_user_static_asserts();

	return qed_parse_mcp_trace_dump(p_hwfn,
					dump_buf,
					results_buf, &parsed_buf_size, true);
}

enum dbg_status qed_print_mcp_trace_results_cont(struct qed_hwfn *p_hwfn,
						 u32 *dump_buf,
						 char *results_buf)
{
	u32 parsed_buf_size;

	return qed_parse_mcp_trace_dump(p_hwfn, dump_buf, results_buf,
					&parsed_buf_size, false);
}

enum dbg_status qed_print_mcp_trace_line(struct qed_hwfn *p_hwfn,
					 u8 *dump_buf,
					 u32 num_dumped_bytes,
					 char *results_buf)
{
	u32 parsed_results_bytes;

	return qed_parse_mcp_trace_buf(p_hwfn,
				       dump_buf,
				       num_dumped_bytes,
				       0,
				       num_dumped_bytes,
				       results_buf, &parsed_results_bytes);
}

/* Frees the specified MCP Trace meta data */
void qed_mcp_trace_free_meta_data(struct qed_hwfn *p_hwfn)
{
	struct dbg_tools_user_data *dev_user_data;
	struct mcp_trace_meta *meta;
	u32 i;

	dev_user_data = qed_dbg_get_user_data(p_hwfn);
	meta = &dev_user_data->mcp_trace_meta;
	if (!meta->is_allocated)
		return;

	/* Release modules */
	if (meta->modules) {
		for (i = 0; i < meta->modules_num; i++)
			kfree(meta->modules[i]);
		kfree(meta->modules);
	}

	/* Release formats */
	if (meta->formats) {
		for (i = 0; i < meta->formats_num; i++)
			kfree(meta->formats[i].format_str);
		kfree(meta->formats);
	}

	meta->is_allocated = false;
}

enum dbg_status qed_get_reg_fifo_results_buf_size(struct qed_hwfn *p_hwfn,
						  u32 *dump_buf,
						  u32 num_dumped_dwords,
						  u32 *results_buf_size)
{
	return qed_parse_reg_fifo_dump(dump_buf, NULL, results_buf_size);
}

enum dbg_status qed_print_reg_fifo_results(struct qed_hwfn *p_hwfn,
					   u32 *dump_buf,
					   u32 num_dumped_dwords,
					   char *results_buf)
{
	u32 parsed_buf_size;

	return qed_parse_reg_fifo_dump(dump_buf, results_buf, &parsed_buf_size);
}

enum dbg_status qed_get_igu_fifo_results_buf_size(struct qed_hwfn *p_hwfn,
						  u32 *dump_buf,
						  u32 num_dumped_dwords,
						  u32 *results_buf_size)
{
	return qed_parse_igu_fifo_dump(dump_buf, NULL, results_buf_size);
}

enum dbg_status qed_print_igu_fifo_results(struct qed_hwfn *p_hwfn,
					   u32 *dump_buf,
					   u32 num_dumped_dwords,
					   char *results_buf)
{
	u32 parsed_buf_size;

	return qed_parse_igu_fifo_dump(dump_buf, results_buf, &parsed_buf_size);
}

enum dbg_status
qed_get_protection_override_results_buf_size(struct qed_hwfn *p_hwfn,
					     u32 *dump_buf,
					     u32 num_dumped_dwords,
					     u32 *results_buf_size)
{
	return qed_parse_protection_override_dump(dump_buf,
						  NULL, results_buf_size);
}

enum dbg_status qed_print_protection_override_results(struct qed_hwfn *p_hwfn,
						      u32 *dump_buf,
						      u32 num_dumped_dwords,
						      char *results_buf)
{
	u32 parsed_buf_size;

	return qed_parse_protection_override_dump(dump_buf,
						  results_buf,
						  &parsed_buf_size);
}

enum dbg_status qed_get_fw_asserts_results_buf_size(struct qed_hwfn *p_hwfn,
						    u32 *dump_buf,
						    u32 num_dumped_dwords,
						    u32 *results_buf_size)
{
	return qed_parse_fw_asserts_dump(dump_buf, NULL, results_buf_size);
}

enum dbg_status qed_print_fw_asserts_results(struct qed_hwfn *p_hwfn,
					     u32 *dump_buf,
					     u32 num_dumped_dwords,
					     char *results_buf)
{
	u32 parsed_buf_size;

	return qed_parse_fw_asserts_dump(dump_buf,
					 results_buf, &parsed_buf_size);
}

enum dbg_status qed_dbg_parse_attn(struct qed_hwfn *p_hwfn,
				   struct dbg_attn_block_result *results)
{
	const u32 *block_attn_name_offsets;
	const char *attn_name_base;
	const char *block_name;
	enum dbg_attn_type attn_type;
	u8 num_regs, i, j;

	num_regs = GET_FIELD(results->data, DBG_ATTN_BLOCK_RESULT_NUM_REGS);
	attn_type = GET_FIELD(results->data, DBG_ATTN_BLOCK_RESULT_ATTN_TYPE);
	block_name = qed_dbg_get_block_name(p_hwfn, results->block_id);
	if (!block_name)
		return DBG_STATUS_INVALID_ARGS;

	if (!p_hwfn->dbg_arrays[BIN_BUF_DBG_ATTN_INDEXES].ptr ||
	    !p_hwfn->dbg_arrays[BIN_BUF_DBG_ATTN_NAME_OFFSETS].ptr ||
	    !p_hwfn->dbg_arrays[BIN_BUF_DBG_PARSING_STRINGS].ptr)
		return DBG_STATUS_DBG_ARRAY_NOT_SET;

	block_attn_name_offsets =
	    (u32 *)p_hwfn->dbg_arrays[BIN_BUF_DBG_ATTN_NAME_OFFSETS].ptr +
	    results->names_offset;

	attn_name_base = p_hwfn->dbg_arrays[BIN_BUF_DBG_PARSING_STRINGS].ptr;

	/* Go over registers with a non-zero attention status */
	for (i = 0; i < num_regs; i++) {
		struct dbg_attn_bit_mapping *bit_mapping;
		struct dbg_attn_reg_result *reg_result;
		u8 num_reg_attn, bit_idx = 0;

		reg_result = &results->reg_results[i];
		num_reg_attn = GET_FIELD(reg_result->data,
					 DBG_ATTN_REG_RESULT_NUM_REG_ATTN);
		bit_mapping = (struct dbg_attn_bit_mapping *)
		    p_hwfn->dbg_arrays[BIN_BUF_DBG_ATTN_INDEXES].ptr +
		    reg_result->block_attn_offset;

		/* Go over attention status bits */
		for (j = 0; j < num_reg_attn; j++) {
			u16 attn_idx_val = GET_FIELD(bit_mapping[j].data,
						     DBG_ATTN_BIT_MAPPING_VAL);
			const char *attn_name, *attn_type_str, *masked_str;
			u32 attn_name_offset;
			u32 sts_addr;

			/* Check if bit mask should be advanced (due to unused
			 * bits).
			 */
			if (GET_FIELD(bit_mapping[j].data,
				      DBG_ATTN_BIT_MAPPING_IS_UNUSED_BIT_CNT)) {
				bit_idx += (u8)attn_idx_val;
				continue;
			}

			/* Check current bit index */
			if (reg_result->sts_val & BIT(bit_idx)) {
				/* An attention bit with value=1 was found
				 * Find attention name
				 */
				attn_name_offset =
					block_attn_name_offsets[attn_idx_val];
				attn_name = attn_name_base + attn_name_offset;
				attn_type_str =
					(attn_type ==
					 ATTN_TYPE_INTERRUPT ? "Interrupt" :
					 "Parity");
				masked_str = reg_result->mask_val &
					     BIT(bit_idx) ?
					     " [masked]" : "";
				sts_addr =
				GET_FIELD(reg_result->data,
					  DBG_ATTN_REG_RESULT_STS_ADDRESS);
				DP_NOTICE(p_hwfn,
					  "%s (%s) : %s [address 0x%08x, bit %d]%s\n",
					  block_name, attn_type_str, attn_name,
					  sts_addr * 4, bit_idx, masked_str);
			}

			bit_idx++;
		}
	}

	return DBG_STATUS_OK;
}

/* Wrapper for unifying the idle_chk and mcp_trace api */
static enum dbg_status
qed_print_idle_chk_results_wrapper(struct qed_hwfn *p_hwfn,
				   u32 *dump_buf,
				   u32 num_dumped_dwords,
				   char *results_buf)
{
	u32 num_errors, num_warnnings;

	return qed_print_idle_chk_results(p_hwfn, dump_buf, num_dumped_dwords,
					  results_buf, &num_errors,
					  &num_warnnings);
}

static DEFINE_MUTEX(qed_dbg_lock);

#define MAX_PHY_RESULT_BUFFER 9000

/******************************** Feature Meta data section ******************/

#define GRC_NUM_STR_FUNCS 2
#define IDLE_CHK_NUM_STR_FUNCS 1
#define MCP_TRACE_NUM_STR_FUNCS 1
#define REG_FIFO_NUM_STR_FUNCS 1
#define IGU_FIFO_NUM_STR_FUNCS 1
#define PROTECTION_OVERRIDE_NUM_STR_FUNCS 1
#define FW_ASSERTS_NUM_STR_FUNCS 1
#define ILT_NUM_STR_FUNCS 1
#define PHY_NUM_STR_FUNCS 20

/* Feature meta data lookup table */
static struct {
	char *name;
	u32 num_funcs;
	enum dbg_status (*get_size)(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt, u32 *size);
	enum dbg_status (*perform_dump)(struct qed_hwfn *p_hwfn,
					struct qed_ptt *p_ptt, u32 *dump_buf,
					u32 buf_size, u32 *dumped_dwords);
	enum dbg_status (*print_results)(struct qed_hwfn *p_hwfn,
					 u32 *dump_buf, u32 num_dumped_dwords,
					 char *results_buf);
	enum dbg_status (*results_buf_size)(struct qed_hwfn *p_hwfn,
					    u32 *dump_buf,
					    u32 num_dumped_dwords,
					    u32 *results_buf_size);
	const struct qed_func_lookup *hsi_func_lookup;
} qed_features_lookup[] = {
	{
	"grc", GRC_NUM_STR_FUNCS, qed_dbg_grc_get_dump_buf_size,
		    qed_dbg_grc_dump, NULL, NULL, NULL}, {
	"idle_chk", IDLE_CHK_NUM_STR_FUNCS,
		    qed_dbg_idle_chk_get_dump_buf_size,
		    qed_dbg_idle_chk_dump,
		    qed_print_idle_chk_results_wrapper,
		    qed_get_idle_chk_results_buf_size,
		    NULL}, {
	"mcp_trace", MCP_TRACE_NUM_STR_FUNCS,
		    qed_dbg_mcp_trace_get_dump_buf_size,
		    qed_dbg_mcp_trace_dump, qed_print_mcp_trace_results,
		    qed_get_mcp_trace_results_buf_size,
		    NULL}, {
	"reg_fifo", REG_FIFO_NUM_STR_FUNCS,
		    qed_dbg_reg_fifo_get_dump_buf_size,
		    qed_dbg_reg_fifo_dump, qed_print_reg_fifo_results,
		    qed_get_reg_fifo_results_buf_size,
		    NULL}, {
	"igu_fifo", IGU_FIFO_NUM_STR_FUNCS,
		    qed_dbg_igu_fifo_get_dump_buf_size,
		    qed_dbg_igu_fifo_dump, qed_print_igu_fifo_results,
		    qed_get_igu_fifo_results_buf_size,
		    NULL}, {
	"protection_override", PROTECTION_OVERRIDE_NUM_STR_FUNCS,
		    qed_dbg_protection_override_get_dump_buf_size,
		    qed_dbg_protection_override_dump,
		    qed_print_protection_override_results,
		    qed_get_protection_override_results_buf_size,
		    NULL}, {
	"fw_asserts", FW_ASSERTS_NUM_STR_FUNCS,
		    qed_dbg_fw_asserts_get_dump_buf_size,
		    qed_dbg_fw_asserts_dump,
		    qed_print_fw_asserts_results,
		    qed_get_fw_asserts_results_buf_size,
		    NULL}, {
	"ilt", ILT_NUM_STR_FUNCS, qed_dbg_ilt_get_dump_buf_size,
		    qed_dbg_ilt_dump, NULL, NULL, NULL},};

static void qed_dbg_print_feature(u8 *p_text_buf, u32 text_size)
{
	u32 i, precision = 80;

	if (!p_text_buf)
		return;

	pr_notice("\n%.*s", precision, p_text_buf);
	for (i = precision; i < text_size; i += precision)
		pr_cont("%.*s", precision, p_text_buf + i);
	pr_cont("\n");
}

#define QED_RESULTS_BUF_MIN_SIZE 16
/* Generic function for decoding debug feature info */
static enum dbg_status format_feature(struct qed_hwfn *p_hwfn,
				      enum qed_dbg_features feature_idx)
{
	struct qed_dbg_feature *feature =
	    &p_hwfn->cdev->dbg_features[feature_idx];
	u32 txt_size_bytes, null_char_pos, i;
	u32 *dbuf, dwords;
	enum dbg_status rc;
	char *text_buf;

	/* Check if feature supports formatting capability */
	if (!qed_features_lookup[feature_idx].results_buf_size)
		return DBG_STATUS_OK;

	dbuf = (u32 *)feature->dump_buf;
	dwords = feature->dumped_dwords;

	/* Obtain size of formatted output */
	rc = qed_features_lookup[feature_idx].results_buf_size(p_hwfn,
							       dbuf,
							       dwords,
							       &txt_size_bytes);
	if (rc != DBG_STATUS_OK)
		return rc;

	/* Make sure that the allocated size is a multiple of dword
	 * (4 bytes).
	 */
	null_char_pos = txt_size_bytes - 1;
	txt_size_bytes = (txt_size_bytes + 3) & ~0x3;

	if (txt_size_bytes < QED_RESULTS_BUF_MIN_SIZE) {
		DP_NOTICE(p_hwfn->cdev,
			  "formatted size of feature was too small %d. Aborting\n",
			  txt_size_bytes);
		return DBG_STATUS_INVALID_ARGS;
	}

	/* allocate temp text buf */
	text_buf = vzalloc(txt_size_bytes);
	if (!text_buf) {
		DP_NOTICE(p_hwfn->cdev,
			  "failed to allocate text buffer. Aborting\n");
		return DBG_STATUS_VIRT_MEM_ALLOC_FAILED;
	}

	/* Decode feature opcodes to string on temp buf */
	rc = qed_features_lookup[feature_idx].print_results(p_hwfn,
							    dbuf,
							    dwords,
							    text_buf);
	if (rc != DBG_STATUS_OK) {
		vfree(text_buf);
		return rc;
	}

	/* Replace the original null character with a '\n' character.
	 * The bytes that were added as a result of the dword alignment are also
	 * padded with '\n' characters.
	 */
	for (i = null_char_pos; i < txt_size_bytes; i++)
		text_buf[i] = '\n';

	/* Dump printable feature to log */
	if (p_hwfn->cdev->print_dbg_data)
		qed_dbg_print_feature(text_buf, txt_size_bytes);

	/* Dump binary data as is to the output file */
	if (p_hwfn->cdev->dbg_bin_dump) {
		vfree(text_buf);
		return rc;
	}

	/* Free the old dump_buf and point the dump_buf to the newly allocated
	 * and formatted text buffer.
	 */
	vfree(feature->dump_buf);
	feature->dump_buf = text_buf;
	feature->buf_size = txt_size_bytes;
	feature->dumped_dwords = txt_size_bytes / 4;

	return rc;
}

#define MAX_DBG_FEATURE_SIZE_DWORDS	0x3FFFFFFF

/* Generic function for performing the dump of a debug feature. */
static enum dbg_status qed_dbg_dump(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    enum qed_dbg_features feature_idx)
{
	struct qed_dbg_feature *feature =
	    &p_hwfn->cdev->dbg_features[feature_idx];
	u32 buf_size_dwords, *dbuf, *dwords;
	enum dbg_status rc;

	DP_NOTICE(p_hwfn->cdev, "Collecting a debug feature [\"%s\"]\n",
		  qed_features_lookup[feature_idx].name);

	/* Dump_buf was already allocated need to free (this can happen if dump
	 * was called but file was never read).
	 * We can't use the buffer as is since size may have changed.
	 */
	if (feature->dump_buf) {
		vfree(feature->dump_buf);
		feature->dump_buf = NULL;
	}

	/* Get buffer size from hsi, allocate accordingly, and perform the
	 * dump.
	 */
	rc = qed_features_lookup[feature_idx].get_size(p_hwfn, p_ptt,
						       &buf_size_dwords);
	if (rc != DBG_STATUS_OK && rc != DBG_STATUS_NVRAM_GET_IMAGE_FAILED)
		return rc;

	if (buf_size_dwords > MAX_DBG_FEATURE_SIZE_DWORDS) {
		feature->buf_size = 0;
		DP_NOTICE(p_hwfn->cdev,
			  "Debug feature [\"%s\"] size (0x%x dwords) exceeds maximum size (0x%x dwords)\n",
			  qed_features_lookup[feature_idx].name,
			  buf_size_dwords, MAX_DBG_FEATURE_SIZE_DWORDS);

		return DBG_STATUS_OK;
	}

	feature->buf_size = buf_size_dwords * sizeof(u32);
	feature->dump_buf = vmalloc(feature->buf_size);
	if (!feature->dump_buf)
		return DBG_STATUS_VIRT_MEM_ALLOC_FAILED;

	dbuf = (u32 *)feature->dump_buf;
	dwords = &feature->dumped_dwords;
	rc = qed_features_lookup[feature_idx].perform_dump(p_hwfn, p_ptt,
							   dbuf,
							   feature->buf_size /
							   sizeof(u32),
							   dwords);

	/* If mcp is stuck we get DBG_STATUS_NVRAM_GET_IMAGE_FAILED error.
	 * In this case the buffer holds valid binary data, but we won't able
	 * to parse it (since parsing relies on data in NVRAM which is only
	 * accessible when MFW is responsive). skip the formatting but return
	 * success so that binary data is provided.
	 */
	if (rc == DBG_STATUS_NVRAM_GET_IMAGE_FAILED)
		return DBG_STATUS_OK;

	if (rc != DBG_STATUS_OK)
		return rc;

	/* Format output */
	rc = format_feature(p_hwfn, feature_idx);
	return rc;
}

int qed_dbg_grc(struct qed_dev *cdev, void *buffer, u32 *num_dumped_bytes)
{
	return qed_dbg_feature(cdev, buffer, DBG_FEATURE_GRC, num_dumped_bytes);
}

int qed_dbg_grc_size(struct qed_dev *cdev)
{
	return qed_dbg_feature_size(cdev, DBG_FEATURE_GRC);
}

int qed_dbg_idle_chk(struct qed_dev *cdev, void *buffer, u32 *num_dumped_bytes)
{
	return qed_dbg_feature(cdev, buffer, DBG_FEATURE_IDLE_CHK,
			       num_dumped_bytes);
}

int qed_dbg_idle_chk_size(struct qed_dev *cdev)
{
	return qed_dbg_feature_size(cdev, DBG_FEATURE_IDLE_CHK);
}

int qed_dbg_reg_fifo(struct qed_dev *cdev, void *buffer, u32 *num_dumped_bytes)
{
	return qed_dbg_feature(cdev, buffer, DBG_FEATURE_REG_FIFO,
			       num_dumped_bytes);
}

int qed_dbg_reg_fifo_size(struct qed_dev *cdev)
{
	return qed_dbg_feature_size(cdev, DBG_FEATURE_REG_FIFO);
}

int qed_dbg_igu_fifo(struct qed_dev *cdev, void *buffer, u32 *num_dumped_bytes)
{
	return qed_dbg_feature(cdev, buffer, DBG_FEATURE_IGU_FIFO,
			       num_dumped_bytes);
}

int qed_dbg_igu_fifo_size(struct qed_dev *cdev)
{
	return qed_dbg_feature_size(cdev, DBG_FEATURE_IGU_FIFO);
}

static int qed_dbg_nvm_image_length(struct qed_hwfn *p_hwfn,
				    enum qed_nvm_images image_id, u32 *length)
{
	struct qed_nvm_image_att image_att;
	int rc;

	*length = 0;
	rc = qed_mcp_get_nvm_image_att(p_hwfn, image_id, &image_att);
	if (rc)
		return rc;

	*length = image_att.length;

	return rc;
}

static int qed_dbg_nvm_image(struct qed_dev *cdev, void *buffer,
			     u32 *num_dumped_bytes,
			     enum qed_nvm_images image_id)
{
	struct qed_hwfn *p_hwfn =
		&cdev->hwfns[cdev->engine_for_debug];
	u32 len_rounded;
	int rc;

	*num_dumped_bytes = 0;
	rc = qed_dbg_nvm_image_length(p_hwfn, image_id, &len_rounded);
	if (rc)
		return rc;

	DP_NOTICE(p_hwfn->cdev,
		  "Collecting a debug feature [\"nvram image %d\"]\n",
		  image_id);

	len_rounded = roundup(len_rounded, sizeof(u32));
	rc = qed_mcp_get_nvm_image(p_hwfn, image_id, buffer, len_rounded);
	if (rc)
		return rc;

	/* QED_NVM_IMAGE_NVM_META image is not swapped like other images */
	if (image_id != QED_NVM_IMAGE_NVM_META)
		cpu_to_be32_array((__force __be32 *)buffer,
				  (const u32 *)buffer,
				  len_rounded / sizeof(u32));

	*num_dumped_bytes = len_rounded;

	return rc;
}

int qed_dbg_protection_override(struct qed_dev *cdev, void *buffer,
				u32 *num_dumped_bytes)
{
	return qed_dbg_feature(cdev, buffer, DBG_FEATURE_PROTECTION_OVERRIDE,
			       num_dumped_bytes);
}

int qed_dbg_protection_override_size(struct qed_dev *cdev)
{
	return qed_dbg_feature_size(cdev, DBG_FEATURE_PROTECTION_OVERRIDE);
}

int qed_dbg_fw_asserts(struct qed_dev *cdev, void *buffer,
		       u32 *num_dumped_bytes)
{
	return qed_dbg_feature(cdev, buffer, DBG_FEATURE_FW_ASSERTS,
			       num_dumped_bytes);
}

int qed_dbg_fw_asserts_size(struct qed_dev *cdev)
{
	return qed_dbg_feature_size(cdev, DBG_FEATURE_FW_ASSERTS);
}

int qed_dbg_ilt(struct qed_dev *cdev, void *buffer, u32 *num_dumped_bytes)
{
	return qed_dbg_feature(cdev, buffer, DBG_FEATURE_ILT, num_dumped_bytes);
}

int qed_dbg_ilt_size(struct qed_dev *cdev)
{
	return qed_dbg_feature_size(cdev, DBG_FEATURE_ILT);
}

int qed_dbg_mcp_trace(struct qed_dev *cdev, void *buffer,
		      u32 *num_dumped_bytes)
{
	return qed_dbg_feature(cdev, buffer, DBG_FEATURE_MCP_TRACE,
			       num_dumped_bytes);
}

int qed_dbg_mcp_trace_size(struct qed_dev *cdev)
{
	return qed_dbg_feature_size(cdev, DBG_FEATURE_MCP_TRACE);
}

/* Defines the amount of bytes allocated for recording the length of debugfs
 * feature buffer.
 */
#define REGDUMP_HEADER_SIZE			sizeof(u32)
#define REGDUMP_HEADER_SIZE_SHIFT		0
#define REGDUMP_HEADER_SIZE_MASK		0xffffff
#define REGDUMP_HEADER_FEATURE_SHIFT		24
#define REGDUMP_HEADER_FEATURE_MASK		0x1f
#define REGDUMP_HEADER_BIN_DUMP_SHIFT		29
#define REGDUMP_HEADER_BIN_DUMP_MASK		0x1
#define REGDUMP_HEADER_OMIT_ENGINE_SHIFT	30
#define REGDUMP_HEADER_OMIT_ENGINE_MASK		0x1
#define REGDUMP_HEADER_ENGINE_SHIFT		31
#define REGDUMP_HEADER_ENGINE_MASK		0x1
#define REGDUMP_MAX_SIZE			0x1000000
#define ILT_DUMP_MAX_SIZE			(1024 * 1024 * 15)

enum debug_print_features {
	OLD_MODE = 0,
	IDLE_CHK = 1,
	GRC_DUMP = 2,
	MCP_TRACE = 3,
	REG_FIFO = 4,
	PROTECTION_OVERRIDE = 5,
	IGU_FIFO = 6,
	PHY = 7,
	FW_ASSERTS = 8,
	NVM_CFG1 = 9,
	DEFAULT_CFG = 10,
	NVM_META = 11,
	MDUMP = 12,
	ILT_DUMP = 13,
};

static u32 qed_calc_regdump_header(struct qed_dev *cdev,
				   enum debug_print_features feature,
				   int engine, u32 feature_size,
				   u8 omit_engine, u8 dbg_bin_dump)
{
	u32 res = 0;

	SET_FIELD(res, REGDUMP_HEADER_SIZE, feature_size);
	if (res != feature_size)
		DP_NOTICE(cdev,
			  "Feature %d is too large (size 0x%x) and will corrupt the dump\n",
			  feature, feature_size);

	SET_FIELD(res, REGDUMP_HEADER_FEATURE, feature);
	SET_FIELD(res, REGDUMP_HEADER_BIN_DUMP, dbg_bin_dump);
	SET_FIELD(res, REGDUMP_HEADER_OMIT_ENGINE, omit_engine);
	SET_FIELD(res, REGDUMP_HEADER_ENGINE, engine);

	return res;
}

int qed_dbg_all_data(struct qed_dev *cdev, void *buffer)
{
	u8 cur_engine, omit_engine = 0, org_engine;
	struct qed_hwfn *p_hwfn = &cdev->hwfns[cdev->engine_for_debug];
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	int grc_params[MAX_DBG_GRC_PARAMS], rc, i;
	u32 offset = 0, feature_size;

	for (i = 0; i < MAX_DBG_GRC_PARAMS; i++)
		grc_params[i] = dev_data->grc.param_val[i];

	if (!QED_IS_CMT(cdev))
		omit_engine = 1;

	cdev->dbg_bin_dump = 1;
	mutex_lock(&qed_dbg_lock);

	org_engine = qed_get_debug_engine(cdev);
	for (cur_engine = 0; cur_engine < cdev->num_hwfns; cur_engine++) {
		/* Collect idle_chks and grcDump for each hw function */
		DP_VERBOSE(cdev, QED_MSG_DEBUG,
			   "obtaining idle_chk and grcdump for current engine\n");
		qed_set_debug_engine(cdev, cur_engine);

		/* First idle_chk */
		rc = qed_dbg_idle_chk(cdev, (u8 *)buffer + offset +
				      REGDUMP_HEADER_SIZE, &feature_size);
		if (!rc) {
			*(u32 *)((u8 *)buffer + offset) =
			    qed_calc_regdump_header(cdev, IDLE_CHK,
						    cur_engine,
						    feature_size,
						    omit_engine,
						    cdev->dbg_bin_dump);
			offset += (feature_size + REGDUMP_HEADER_SIZE);
		} else {
			DP_ERR(cdev, "qed_dbg_idle_chk failed. rc = %d\n", rc);
		}

		/* Second idle_chk */
		rc = qed_dbg_idle_chk(cdev, (u8 *)buffer + offset +
				      REGDUMP_HEADER_SIZE, &feature_size);
		if (!rc) {
			*(u32 *)((u8 *)buffer + offset) =
			    qed_calc_regdump_header(cdev, IDLE_CHK,
						    cur_engine,
						    feature_size,
						    omit_engine,
						    cdev->dbg_bin_dump);
			offset += (feature_size + REGDUMP_HEADER_SIZE);
		} else {
			DP_ERR(cdev, "qed_dbg_idle_chk failed. rc = %d\n", rc);
		}

		/* reg_fifo dump */
		rc = qed_dbg_reg_fifo(cdev, (u8 *)buffer + offset +
				      REGDUMP_HEADER_SIZE, &feature_size);
		if (!rc) {
			*(u32 *)((u8 *)buffer + offset) =
			    qed_calc_regdump_header(cdev, REG_FIFO,
						    cur_engine,
						    feature_size,
						    omit_engine,
						    cdev->dbg_bin_dump);
			offset += (feature_size + REGDUMP_HEADER_SIZE);
		} else {
			DP_ERR(cdev, "qed_dbg_reg_fifo failed. rc = %d\n", rc);
		}

		/* igu_fifo dump */
		rc = qed_dbg_igu_fifo(cdev, (u8 *)buffer + offset +
				      REGDUMP_HEADER_SIZE, &feature_size);
		if (!rc) {
			*(u32 *)((u8 *)buffer + offset) =
			    qed_calc_regdump_header(cdev, IGU_FIFO,
						    cur_engine,
						    feature_size,
						    omit_engine,
						    cdev->dbg_bin_dump);
			offset += (feature_size + REGDUMP_HEADER_SIZE);
		} else {
			DP_ERR(cdev, "qed_dbg_igu_fifo failed. rc = %d", rc);
		}

		/* protection_override dump */
		rc = qed_dbg_protection_override(cdev, (u8 *)buffer + offset +
						 REGDUMP_HEADER_SIZE,
						 &feature_size);
		if (!rc) {
			*(u32 *)((u8 *)buffer + offset) =
			    qed_calc_regdump_header(cdev,
						    PROTECTION_OVERRIDE,
						    cur_engine,
						    feature_size,
						    omit_engine,
						    cdev->dbg_bin_dump);
			offset += (feature_size + REGDUMP_HEADER_SIZE);
		} else {
			DP_ERR(cdev,
			       "qed_dbg_protection_override failed. rc = %d\n",
			       rc);
		}

		/* fw_asserts dump */
		rc = qed_dbg_fw_asserts(cdev, (u8 *)buffer + offset +
					REGDUMP_HEADER_SIZE, &feature_size);
		if (!rc) {
			*(u32 *)((u8 *)buffer + offset) =
			    qed_calc_regdump_header(cdev, FW_ASSERTS,
						    cur_engine,
						    feature_size,
						    omit_engine,
						    cdev->dbg_bin_dump);
			offset += (feature_size + REGDUMP_HEADER_SIZE);
		} else {
			DP_ERR(cdev, "qed_dbg_fw_asserts failed. rc = %d\n",
			       rc);
		}

		feature_size = qed_dbg_ilt_size(cdev);
		if (!cdev->disable_ilt_dump && feature_size <
		    ILT_DUMP_MAX_SIZE) {
			rc = qed_dbg_ilt(cdev, (u8 *)buffer + offset +
					 REGDUMP_HEADER_SIZE, &feature_size);
			if (!rc) {
				*(u32 *)((u8 *)buffer + offset) =
				    qed_calc_regdump_header(cdev, ILT_DUMP,
							    cur_engine,
							    feature_size,
							    omit_engine,
							    cdev->dbg_bin_dump);
				offset += (feature_size + REGDUMP_HEADER_SIZE);
			} else {
				DP_ERR(cdev, "qed_dbg_ilt failed. rc = %d\n",
				       rc);
			}
		}

		/* Grc dump - must be last because when mcp stuck it will
		 * clutter idle_chk, reg_fifo, ...
		 */
		for (i = 0; i < MAX_DBG_GRC_PARAMS; i++)
			dev_data->grc.param_val[i] = grc_params[i];

		rc = qed_dbg_grc(cdev, (u8 *)buffer + offset +
				 REGDUMP_HEADER_SIZE, &feature_size);
		if (!rc) {
			*(u32 *)((u8 *)buffer + offset) =
			    qed_calc_regdump_header(cdev, GRC_DUMP,
						    cur_engine,
						    feature_size,
						    omit_engine,
						    cdev->dbg_bin_dump);
			offset += (feature_size + REGDUMP_HEADER_SIZE);
		} else {
			DP_ERR(cdev, "qed_dbg_grc failed. rc = %d", rc);
		}
	}

	qed_set_debug_engine(cdev, org_engine);

	/* mcp_trace */
	rc = qed_dbg_mcp_trace(cdev, (u8 *)buffer + offset +
			       REGDUMP_HEADER_SIZE, &feature_size);
	if (!rc) {
		*(u32 *)((u8 *)buffer + offset) =
		    qed_calc_regdump_header(cdev, MCP_TRACE, cur_engine,
					    feature_size, omit_engine,
					    cdev->dbg_bin_dump);
		offset += (feature_size + REGDUMP_HEADER_SIZE);
	} else {
		DP_ERR(cdev, "qed_dbg_mcp_trace failed. rc = %d\n", rc);
	}

	/* nvm cfg1 */
	rc = qed_dbg_nvm_image(cdev,
			       (u8 *)buffer + offset +
			       REGDUMP_HEADER_SIZE, &feature_size,
			       QED_NVM_IMAGE_NVM_CFG1);
	if (!rc) {
		*(u32 *)((u8 *)buffer + offset) =
		    qed_calc_regdump_header(cdev, NVM_CFG1, cur_engine,
					    feature_size, omit_engine,
					    cdev->dbg_bin_dump);
		offset += (feature_size + REGDUMP_HEADER_SIZE);
	} else if (rc != -ENOENT) {
		DP_ERR(cdev,
		       "qed_dbg_nvm_image failed for image  %d (%s), rc = %d\n",
		       QED_NVM_IMAGE_NVM_CFG1, "QED_NVM_IMAGE_NVM_CFG1",
		       rc);
	}

		/* nvm default */
	rc = qed_dbg_nvm_image(cdev,
			       (u8 *)buffer + offset +
			       REGDUMP_HEADER_SIZE, &feature_size,
			       QED_NVM_IMAGE_DEFAULT_CFG);
	if (!rc) {
		*(u32 *)((u8 *)buffer + offset) =
		    qed_calc_regdump_header(cdev, DEFAULT_CFG,
					    cur_engine, feature_size,
					    omit_engine,
					    cdev->dbg_bin_dump);
		offset += (feature_size + REGDUMP_HEADER_SIZE);
	} else if (rc != -ENOENT) {
		DP_ERR(cdev,
		       "qed_dbg_nvm_image failed for image %d (%s), rc = %d\n",
		       QED_NVM_IMAGE_DEFAULT_CFG,
		       "QED_NVM_IMAGE_DEFAULT_CFG", rc);
	}

	/* nvm meta */
	rc = qed_dbg_nvm_image(cdev,
			       (u8 *)buffer + offset +
			       REGDUMP_HEADER_SIZE, &feature_size,
			       QED_NVM_IMAGE_NVM_META);
	if (!rc) {
		*(u32 *)((u8 *)buffer + offset) =
		    qed_calc_regdump_header(cdev, NVM_META, cur_engine,
					    feature_size, omit_engine,
					    cdev->dbg_bin_dump);
		offset += (feature_size + REGDUMP_HEADER_SIZE);
	} else if (rc != -ENOENT) {
		DP_ERR(cdev,
		       "qed_dbg_nvm_image failed for image %d (%s), rc = %d\n",
		       QED_NVM_IMAGE_NVM_META, "QED_NVM_IMAGE_NVM_META",
		       rc);
	}

	/* nvm mdump */
	rc = qed_dbg_nvm_image(cdev, (u8 *)buffer + offset +
			       REGDUMP_HEADER_SIZE, &feature_size,
			       QED_NVM_IMAGE_MDUMP);
	if (!rc) {
		*(u32 *)((u8 *)buffer + offset) =
		    qed_calc_regdump_header(cdev, MDUMP, cur_engine,
					    feature_size, omit_engine,
					    cdev->dbg_bin_dump);
		offset += (feature_size + REGDUMP_HEADER_SIZE);
	} else if (rc != -ENOENT) {
		DP_ERR(cdev,
		       "qed_dbg_nvm_image failed for image %d (%s), rc = %d\n",
		       QED_NVM_IMAGE_MDUMP, "QED_NVM_IMAGE_MDUMP", rc);
	}

	mutex_unlock(&qed_dbg_lock);
	cdev->dbg_bin_dump = 0;

	return 0;
}

int qed_dbg_all_data_size(struct qed_dev *cdev)
{
	u32 regs_len = 0, image_len = 0, ilt_len = 0, total_ilt_len = 0;
	struct qed_hwfn *p_hwfn = &cdev->hwfns[cdev->engine_for_debug];
	u8 cur_engine, org_engine;

	cdev->disable_ilt_dump = false;
	org_engine = qed_get_debug_engine(cdev);
	for (cur_engine = 0; cur_engine < cdev->num_hwfns; cur_engine++) {
		/* Engine specific */
		DP_VERBOSE(cdev, QED_MSG_DEBUG,
			   "calculating idle_chk and grcdump register length for current engine\n");
		qed_set_debug_engine(cdev, cur_engine);
		regs_len += REGDUMP_HEADER_SIZE + qed_dbg_idle_chk_size(cdev) +
		    REGDUMP_HEADER_SIZE + qed_dbg_idle_chk_size(cdev) +
		    REGDUMP_HEADER_SIZE + qed_dbg_grc_size(cdev) +
		    REGDUMP_HEADER_SIZE + qed_dbg_reg_fifo_size(cdev) +
		    REGDUMP_HEADER_SIZE + qed_dbg_igu_fifo_size(cdev) +
		    REGDUMP_HEADER_SIZE +
		    qed_dbg_protection_override_size(cdev) +
		    REGDUMP_HEADER_SIZE + qed_dbg_fw_asserts_size(cdev);
		ilt_len = REGDUMP_HEADER_SIZE + qed_dbg_ilt_size(cdev);
		if (ilt_len < ILT_DUMP_MAX_SIZE) {
			total_ilt_len += ilt_len;
			regs_len += ilt_len;
		}
	}

	qed_set_debug_engine(cdev, org_engine);

	/* Engine common */
	regs_len += REGDUMP_HEADER_SIZE + qed_dbg_mcp_trace_size(cdev) +
	    REGDUMP_HEADER_SIZE + qed_dbg_phy_size(cdev);
	qed_dbg_nvm_image_length(p_hwfn, QED_NVM_IMAGE_NVM_CFG1, &image_len);
	if (image_len)
		regs_len += REGDUMP_HEADER_SIZE + image_len;
	qed_dbg_nvm_image_length(p_hwfn, QED_NVM_IMAGE_DEFAULT_CFG, &image_len);
	if (image_len)
		regs_len += REGDUMP_HEADER_SIZE + image_len;
	qed_dbg_nvm_image_length(p_hwfn, QED_NVM_IMAGE_NVM_META, &image_len);
	if (image_len)
		regs_len += REGDUMP_HEADER_SIZE + image_len;
	qed_dbg_nvm_image_length(p_hwfn, QED_NVM_IMAGE_MDUMP, &image_len);
	if (image_len)
		regs_len += REGDUMP_HEADER_SIZE + image_len;

	if (regs_len > REGDUMP_MAX_SIZE) {
		DP_VERBOSE(cdev, QED_MSG_DEBUG,
			   "Dump exceeds max size 0x%x, disable ILT dump\n",
			   REGDUMP_MAX_SIZE);
		cdev->disable_ilt_dump = true;
		regs_len -= total_ilt_len;
	}

	return regs_len;
}

int qed_dbg_feature(struct qed_dev *cdev, void *buffer,
		    enum qed_dbg_features feature, u32 *num_dumped_bytes)
{
	struct qed_dbg_feature *qed_feature = &cdev->dbg_features[feature];
	struct qed_hwfn *p_hwfn = &cdev->hwfns[cdev->engine_for_debug];
	enum dbg_status dbg_rc;
	struct qed_ptt *p_ptt;
	int rc = 0;

	/* Acquire ptt */
	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt)
		return -EINVAL;

	/* Get dump */
	dbg_rc = qed_dbg_dump(p_hwfn, p_ptt, feature);
	if (dbg_rc != DBG_STATUS_OK) {
		DP_VERBOSE(cdev, QED_MSG_DEBUG, "%s\n",
			   qed_dbg_get_status_str(dbg_rc));
		*num_dumped_bytes = 0;
		rc = -EINVAL;
		goto out;
	}

	DP_VERBOSE(cdev, QED_MSG_DEBUG,
		   "copying debugfs feature to external buffer\n");
	memcpy(buffer, qed_feature->dump_buf, qed_feature->buf_size);
	*num_dumped_bytes = cdev->dbg_features[feature].dumped_dwords *
			    4;

out:
	qed_ptt_release(p_hwfn, p_ptt);
	return rc;
}

int qed_dbg_feature_size(struct qed_dev *cdev, enum qed_dbg_features feature)
{
	struct qed_dbg_feature *qed_feature = &cdev->dbg_features[feature];
	struct qed_hwfn *p_hwfn = &cdev->hwfns[cdev->engine_for_debug];
	struct qed_ptt *p_ptt = qed_ptt_acquire(p_hwfn);
	u32 buf_size_dwords;
	enum dbg_status rc;

	if (!p_ptt)
		return -EINVAL;

	rc = qed_features_lookup[feature].get_size(p_hwfn, p_ptt,
						   &buf_size_dwords);
	if (rc != DBG_STATUS_OK)
		buf_size_dwords = 0;

	/* Feature will not be dumped if it exceeds maximum size */
	if (buf_size_dwords > MAX_DBG_FEATURE_SIZE_DWORDS)
		buf_size_dwords = 0;

	qed_ptt_release(p_hwfn, p_ptt);
	qed_feature->buf_size = buf_size_dwords * sizeof(u32);
	return qed_feature->buf_size;
}

int qed_dbg_phy_size(struct qed_dev *cdev)
{
	/* return max size of phy info and
	 * phy mac_stat multiplied by the number of ports
	 */
	return MAX_PHY_RESULT_BUFFER * (1 + qed_device_num_ports(cdev));
}

u8 qed_get_debug_engine(struct qed_dev *cdev)
{
	return cdev->engine_for_debug;
}

void qed_set_debug_engine(struct qed_dev *cdev, int engine_number)
{
	DP_VERBOSE(cdev, QED_MSG_DEBUG, "set debug engine to %d\n",
		   engine_number);
	cdev->engine_for_debug = engine_number;
}

void qed_dbg_pf_init(struct qed_dev *cdev)
{
	const u8 *dbg_values = NULL;
	int i;

	/* Sync ver with debugbus qed code */
	qed_dbg_set_app_ver(TOOLS_VERSION);

	/* Debug values are after init values.
	 * The offset is the first dword of the file.
	 */
	dbg_values = cdev->firmware->data + *(u32 *)cdev->firmware->data;

	for_each_hwfn(cdev, i) {
		qed_dbg_set_bin_ptr(&cdev->hwfns[i], dbg_values);
		qed_dbg_user_set_bin_ptr(&cdev->hwfns[i], dbg_values);
	}

	/* Set the hwfn to be 0 as default */
	cdev->engine_for_debug = 0;
}

void qed_dbg_pf_exit(struct qed_dev *cdev)
{
	struct qed_dbg_feature *feature = NULL;
	enum qed_dbg_features feature_idx;

	/* debug features' buffers may be allocated if debug feature was used
	 * but dump wasn't called
	 */
	for (feature_idx = 0; feature_idx < DBG_FEATURE_NUM; feature_idx++) {
		feature = &cdev->dbg_features[feature_idx];
		if (feature->dump_buf) {
			vfree(feature->dump_buf);
			feature->dump_buf = NULL;
		}
	}
}
