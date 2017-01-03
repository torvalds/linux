/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/crc32.h>
#include "qed.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"

/* Chip IDs enum */
enum chip_ids {
	CHIP_RESERVED,
	CHIP_BB_B0,
	CHIP_K2,
	MAX_CHIP_IDS
};

/* Memory groups enum */
enum mem_groups {
	MEM_GROUP_PXP_MEM,
	MEM_GROUP_DMAE_MEM,
	MEM_GROUP_CM_MEM,
	MEM_GROUP_QM_MEM,
	MEM_GROUP_TM_MEM,
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
	MEM_GROUP_CONN_CFC_MEM,
	MEM_GROUP_TASK_CFC_MEM,
	MEM_GROUP_CAU_PI,
	MEM_GROUP_CAU_MEM,
	MEM_GROUP_PXP_ILT,
	MEM_GROUP_MULD_MEM,
	MEM_GROUP_BTB_MEM,
	MEM_GROUP_IGU_MEM,
	MEM_GROUP_IGU_MSIX,
	MEM_GROUP_CAU_SB,
	MEM_GROUP_BMB_RAM,
	MEM_GROUP_BMB_MEM,
	MEM_GROUPS_NUM
};

/* Memory groups names */
static const char * const s_mem_group_names[] = {
	"PXP_MEM",
	"DMAE_MEM",
	"CM_MEM",
	"QM_MEM",
	"TM_MEM",
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
	"CONN_CFC_MEM",
	"TASK_CFC_MEM",
	"CAU_PI",
	"CAU_MEM",
	"PXP_ILT",
	"MULD_MEM",
	"BTB_MEM",
	"IGU_MEM",
	"IGU_MSIX",
	"CAU_SB",
	"BMB_RAM",
	"BMB_MEM",
};

/* Idle check conditions */
static u32 cond4(const u32 *r, const u32 *imm)
{
	return ((r[0] & imm[0]) != imm[1]) && ((r[1] & imm[2]) != imm[3]);
}

static u32 cond6(const u32 *r, const u32 *imm)
{
	return ((r[0] >> imm[0]) & imm[1]) != imm[2];
}

static u32 cond5(const u32 *r, const u32 *imm)
{
	return (r[0] & imm[0]) != imm[1];
}

static u32 cond8(const u32 *r, const u32 *imm)
{
	return ((r[0] & imm[0]) >> imm[1]) !=
	    (((r[0] & imm[2]) >> imm[3]) | ((r[1] & imm[4]) << imm[5]));
}

static u32 cond9(const u32 *r, const u32 *imm)
{
	return ((r[0] & imm[0]) >> imm[1]) != (r[0] & imm[2]);
}

static u32 cond1(const u32 *r, const u32 *imm)
{
	return (r[0] & ~imm[0]) != imm[1];
}

static u32 cond0(const u32 *r, const u32 *imm)
{
	return r[0] != imm[0];
}

static u32 cond10(const u32 *r, const u32 *imm)
{
	return r[0] != r[1] && r[2] == imm[0];
}

static u32 cond11(const u32 *r, const u32 *imm)
{
	return r[0] != r[1] && r[2] > imm[0];
}

static u32 cond3(const u32 *r, const u32 *imm)
{
	return r[0] != r[1];
}

static u32 cond12(const u32 *r, const u32 *imm)
{
	return r[0] & imm[0];
}

static u32 cond7(const u32 *r, const u32 *imm)
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
};

/******************************* Data Types **********************************/

enum platform_ids {
	PLATFORM_ASIC,
	PLATFORM_RESERVED,
	PLATFORM_RESERVED2,
	PLATFORM_RESERVED3,
	MAX_PLATFORM_IDS
};

struct dbg_array {
	const u32 *ptr;
	u32 size_in_dwords;
};

/* Chip constant definitions */
struct chip_defs {
	const char *name;
	struct {
		u8 num_ports;
		u8 num_pfs;
	} per_platform[MAX_PLATFORM_IDS];
};

/* Platform constant definitions */
struct platform_defs {
	const char *name;
	u32 delay_factor;
};

/* Storm constant definitions */
struct storm_defs {
	char letter;
	enum block_id block_id;
	enum dbg_bus_clients dbg_client_id[MAX_CHIP_IDS];
	bool has_vfc;
	u32 sem_fast_mem_addr;
	u32 sem_frame_mode_addr;
	u32 sem_slow_enable_addr;
	u32 sem_slow_mode_addr;
	u32 sem_slow_mode1_conf_addr;
	u32 sem_sync_dbg_empty_addr;
	u32 sem_slow_dbg_empty_addr;
	u32 cm_ctx_wr_addr;
	u32 cm_conn_ag_ctx_lid_size; /* In quad-regs */
	u32 cm_conn_ag_ctx_rd_addr;
	u32 cm_conn_st_ctx_lid_size; /* In quad-regs */
	u32 cm_conn_st_ctx_rd_addr;
	u32 cm_task_ag_ctx_lid_size; /* In quad-regs */
	u32 cm_task_ag_ctx_rd_addr;
	u32 cm_task_st_ctx_lid_size; /* In quad-regs */
	u32 cm_task_st_ctx_rd_addr;
};

/* Block constant definitions */
struct block_defs {
	const char *name;
	bool has_dbg_bus[MAX_CHIP_IDS];
	bool associated_to_storm;
	u32 storm_id; /* Valid only if associated_to_storm is true */
	enum dbg_bus_clients dbg_client_id[MAX_CHIP_IDS];
	u32 dbg_select_addr;
	u32 dbg_cycle_enable_addr;
	u32 dbg_shift_addr;
	u32 dbg_force_valid_addr;
	u32 dbg_force_frame_addr;
	bool has_reset_bit;
	bool unreset; /* If true, the block is taken out of reset before dump */
	enum dbg_reset_regs reset_reg;
	u8 reset_bit_offset; /* Bit offset in reset register */
};

/* Reset register definitions */
struct reset_reg_defs {
	u32 addr;
	u32 unreset_val;
	bool exists[MAX_CHIP_IDS];
};

struct grc_param_defs {
	u32 default_val[MAX_CHIP_IDS];
	u32 min;
	u32 max;
	bool is_preset;
	u32 exclude_all_preset_val;
	u32 crash_preset_val;
};

struct rss_mem_defs {
	const char *mem_name;
	const char *type_name;
	u32 addr; /* In 128b units */
	u32 num_entries[MAX_CHIP_IDS];
	u32 entry_width[MAX_CHIP_IDS]; /* In bits */
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
	u32 num_of_blocks[MAX_CHIP_IDS];
};

struct phy_defs {
	const char *phy_name;
	u32 base_addr;
	u32 tbus_addr_lo_addr;
	u32 tbus_addr_hi_addr;
	u32 tbus_data_lo_addr;
	u32 tbus_data_hi_addr;
};

/******************************** Constants **********************************/

#define MAX_LCIDS			320
#define MAX_LTIDS			320
#define NUM_IOR_SETS			2
#define IORS_PER_SET			176
#define IOR_SET_OFFSET(set_id)		((set_id) * 256)
#define BYTES_IN_DWORD			sizeof(u32)

/* In the macros below, size and offset are specified in bits */
#define CEIL_DWORDS(size)		DIV_ROUND_UP(size, 32)
#define FIELD_BIT_OFFSET(type, field)	type ## _ ## field ## _ ## OFFSET
#define FIELD_BIT_SIZE(type, field)	type ## _ ## field ## _ ## SIZE
#define FIELD_DWORD_OFFSET(type, field) \
	 (int)(FIELD_BIT_OFFSET(type, field) / 32)
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
#define ARR_REG_RD(dev, ptt, addr, arr, arr_size) \
	do { \
		for (i = 0; i < (arr_size); i++) \
			(arr)[i] = qed_rd(dev, ptt, addr); \
	} while (0)

#define DWORDS_TO_BYTES(dwords)		((dwords) * BYTES_IN_DWORD)
#define BYTES_TO_DWORDS(bytes)		((bytes) / BYTES_IN_DWORD)
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
#define BIG_RAM_BLOCK_SIZE_BYTES	128
#define BIG_RAM_BLOCK_SIZE_DWORDS \
	BYTES_TO_DWORDS(BIG_RAM_BLOCK_SIZE_BYTES)
#define NUM_PHY_TBUS_ADDRESSES		2048
#define PHY_DUMP_SIZE_DWORDS		(NUM_PHY_TBUS_ADDRESSES / 2)
#define RESET_REG_UNRESET_OFFSET	4
#define STALL_DELAY_MS			500
#define STATIC_DEBUG_LINE_DWORDS	9
#define NUM_DBG_BUS_LINES		256
#define NUM_COMMON_GLOBAL_PARAMS	8
#define FW_IMG_MAIN			1
#define REG_FIFO_DEPTH_ELEMENTS		32
#define REG_FIFO_ELEMENT_DWORDS		2
#define REG_FIFO_DEPTH_DWORDS \
	(REG_FIFO_ELEMENT_DWORDS * REG_FIFO_DEPTH_ELEMENTS)
#define IGU_FIFO_DEPTH_ELEMENTS		64
#define IGU_FIFO_ELEMENT_DWORDS		4
#define IGU_FIFO_DEPTH_DWORDS \
	(IGU_FIFO_ELEMENT_DWORDS * IGU_FIFO_DEPTH_ELEMENTS)
#define PROTECTION_OVERRIDE_DEPTH_ELEMENTS	20
#define PROTECTION_OVERRIDE_ELEMENT_DWORDS	2
#define PROTECTION_OVERRIDE_DEPTH_DWORDS \
	(PROTECTION_OVERRIDE_DEPTH_ELEMENTS * \
	 PROTECTION_OVERRIDE_ELEMENT_DWORDS)
#define MCP_SPAD_TRACE_OFFSIZE_ADDR \
	(MCP_REG_SCRATCH + \
	 offsetof(struct static_init, sections[SPAD_SECTION_TRACE]))
#define MCP_TRACE_META_IMAGE_SIGNATURE  0x669955aa
#define EMPTY_FW_VERSION_STR		"???_???_???_???"
#define EMPTY_FW_IMAGE_STR		"???????????????"

/***************************** Constant Arrays *******************************/

/* Debug arrays */
static struct dbg_array s_dbg_arrays[MAX_BIN_DBG_BUFFER_TYPE] = { {NULL} };

/* Chip constant definitions array */
static struct chip_defs s_chip_defs[MAX_CHIP_IDS] = {
	{ "reserved", { {0, 0}, {0, 0}, {0, 0}, {0, 0} } },
	{ "bb_b0",
	  { {MAX_NUM_PORTS_BB, MAX_NUM_PFS_BB}, {0, 0}, {0, 0}, {0, 0} } },
	{ "k2", { {MAX_NUM_PORTS_K2, MAX_NUM_PFS_K2}, {0, 0}, {0, 0}, {0, 0} } }
};

/* Storm constant definitions array */
static struct storm_defs s_storm_defs[] = {
	/* Tstorm */
	{'T', BLOCK_TSEM,
	 {DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT,
	  DBG_BUS_CLIENT_RBCT}, true,
	 TSEM_REG_FAST_MEMORY,
	 TSEM_REG_DBG_FRAME_MODE, TSEM_REG_SLOW_DBG_ACTIVE,
	 TSEM_REG_SLOW_DBG_MODE, TSEM_REG_DBG_MODE1_CFG,
	 TSEM_REG_SYNC_DBG_EMPTY, TSEM_REG_SLOW_DBG_EMPTY,
	 TCM_REG_CTX_RBC_ACCS,
	 4, TCM_REG_AGG_CON_CTX,
	 16, TCM_REG_SM_CON_CTX,
	 2, TCM_REG_AGG_TASK_CTX,
	 4, TCM_REG_SM_TASK_CTX},
	/* Mstorm */
	{'M', BLOCK_MSEM,
	 {DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT,
	  DBG_BUS_CLIENT_RBCM}, false,
	 MSEM_REG_FAST_MEMORY,
	 MSEM_REG_DBG_FRAME_MODE, MSEM_REG_SLOW_DBG_ACTIVE,
	 MSEM_REG_SLOW_DBG_MODE, MSEM_REG_DBG_MODE1_CFG,
	 MSEM_REG_SYNC_DBG_EMPTY, MSEM_REG_SLOW_DBG_EMPTY,
	 MCM_REG_CTX_RBC_ACCS,
	 1, MCM_REG_AGG_CON_CTX,
	 10, MCM_REG_SM_CON_CTX,
	 2, MCM_REG_AGG_TASK_CTX,
	 7, MCM_REG_SM_TASK_CTX},
	/* Ustorm */
	{'U', BLOCK_USEM,
	 {DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU,
	  DBG_BUS_CLIENT_RBCU}, false,
	 USEM_REG_FAST_MEMORY,
	 USEM_REG_DBG_FRAME_MODE, USEM_REG_SLOW_DBG_ACTIVE,
	 USEM_REG_SLOW_DBG_MODE, USEM_REG_DBG_MODE1_CFG,
	 USEM_REG_SYNC_DBG_EMPTY, USEM_REG_SLOW_DBG_EMPTY,
	 UCM_REG_CTX_RBC_ACCS,
	 2, UCM_REG_AGG_CON_CTX,
	 13, UCM_REG_SM_CON_CTX,
	 3, UCM_REG_AGG_TASK_CTX,
	 3, UCM_REG_SM_TASK_CTX},
	/* Xstorm */
	{'X', BLOCK_XSEM,
	 {DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX,
	  DBG_BUS_CLIENT_RBCX}, false,
	 XSEM_REG_FAST_MEMORY,
	 XSEM_REG_DBG_FRAME_MODE, XSEM_REG_SLOW_DBG_ACTIVE,
	 XSEM_REG_SLOW_DBG_MODE, XSEM_REG_DBG_MODE1_CFG,
	 XSEM_REG_SYNC_DBG_EMPTY, XSEM_REG_SLOW_DBG_EMPTY,
	 XCM_REG_CTX_RBC_ACCS,
	 9, XCM_REG_AGG_CON_CTX,
	 15, XCM_REG_SM_CON_CTX,
	 0, 0,
	 0, 0},
	/* Ystorm */
	{'Y', BLOCK_YSEM,
	 {DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX,
	  DBG_BUS_CLIENT_RBCY}, false,
	 YSEM_REG_FAST_MEMORY,
	 YSEM_REG_DBG_FRAME_MODE, YSEM_REG_SLOW_DBG_ACTIVE,
	 YSEM_REG_SLOW_DBG_MODE, YSEM_REG_DBG_MODE1_CFG,
	 YSEM_REG_SYNC_DBG_EMPTY, TSEM_REG_SLOW_DBG_EMPTY,
	 YCM_REG_CTX_RBC_ACCS,
	 2, YCM_REG_AGG_CON_CTX,
	 3, YCM_REG_SM_CON_CTX,
	 2, YCM_REG_AGG_TASK_CTX,
	 12, YCM_REG_SM_TASK_CTX},
	/* Pstorm */
	{'P', BLOCK_PSEM,
	 {DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS,
	  DBG_BUS_CLIENT_RBCS}, true,
	 PSEM_REG_FAST_MEMORY,
	 PSEM_REG_DBG_FRAME_MODE, PSEM_REG_SLOW_DBG_ACTIVE,
	 PSEM_REG_SLOW_DBG_MODE, PSEM_REG_DBG_MODE1_CFG,
	 PSEM_REG_SYNC_DBG_EMPTY, PSEM_REG_SLOW_DBG_EMPTY,
	 PCM_REG_CTX_RBC_ACCS,
	 0, 0,
	 10, PCM_REG_SM_CON_CTX,
	 0, 0,
	 0, 0}
};

/* Block definitions array */
static struct block_defs block_grc_defs = {
	"grc", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCN, DBG_BUS_CLIENT_RBCN, DBG_BUS_CLIENT_RBCN},
	GRC_REG_DBG_SELECT, GRC_REG_DBG_DWORD_ENABLE,
	GRC_REG_DBG_SHIFT, GRC_REG_DBG_FORCE_VALID,
	GRC_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_UA, 1
};

static struct block_defs block_miscs_defs = {
	"miscs", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0
};

static struct block_defs block_misc_defs = {
	"misc", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0
};

static struct block_defs block_dbu_defs = {
	"dbu", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0
};

static struct block_defs block_pglue_b_defs = {
	"pglue_b", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCH, DBG_BUS_CLIENT_RBCH, DBG_BUS_CLIENT_RBCH},
	PGLUE_B_REG_DBG_SELECT, PGLUE_B_REG_DBG_DWORD_ENABLE,
	PGLUE_B_REG_DBG_SHIFT, PGLUE_B_REG_DBG_FORCE_VALID,
	PGLUE_B_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 1
};

static struct block_defs block_cnig_defs = {
	"cnig", {false, false, true}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCW},
	CNIG_REG_DBG_SELECT_K2, CNIG_REG_DBG_DWORD_ENABLE_K2,
	CNIG_REG_DBG_SHIFT_K2, CNIG_REG_DBG_FORCE_VALID_K2,
	CNIG_REG_DBG_FORCE_FRAME_K2,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 0
};

static struct block_defs block_cpmu_defs = {
	"cpmu", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 8
};

static struct block_defs block_ncsi_defs = {
	"ncsi", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCZ, DBG_BUS_CLIENT_RBCZ, DBG_BUS_CLIENT_RBCZ},
	NCSI_REG_DBG_SELECT, NCSI_REG_DBG_DWORD_ENABLE,
	NCSI_REG_DBG_SHIFT, NCSI_REG_DBG_FORCE_VALID,
	NCSI_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 5
};

static struct block_defs block_opte_defs = {
	"opte", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 4
};

static struct block_defs block_bmb_defs = {
	"bmb", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCZ, DBG_BUS_CLIENT_RBCZ, DBG_BUS_CLIENT_RBCB},
	BMB_REG_DBG_SELECT, BMB_REG_DBG_DWORD_ENABLE,
	BMB_REG_DBG_SHIFT, BMB_REG_DBG_FORCE_VALID,
	BMB_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISCS_PL_UA, 7
};

static struct block_defs block_pcie_defs = {
	"pcie", {false, false, true}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCH},
	PCIE_REG_DBG_COMMON_SELECT, PCIE_REG_DBG_COMMON_DWORD_ENABLE,
	PCIE_REG_DBG_COMMON_SHIFT, PCIE_REG_DBG_COMMON_FORCE_VALID,
	PCIE_REG_DBG_COMMON_FORCE_FRAME,
	false, false, MAX_DBG_RESET_REGS, 0
};

static struct block_defs block_mcp_defs = {
	"mcp", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0
};

static struct block_defs block_mcp2_defs = {
	"mcp2", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCZ, DBG_BUS_CLIENT_RBCZ, DBG_BUS_CLIENT_RBCZ},
	MCP2_REG_DBG_SELECT, MCP2_REG_DBG_DWORD_ENABLE,
	MCP2_REG_DBG_SHIFT, MCP2_REG_DBG_FORCE_VALID,
	MCP2_REG_DBG_FORCE_FRAME,
	false, false, MAX_DBG_RESET_REGS, 0
};

static struct block_defs block_pswhst_defs = {
	"pswhst", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP},
	PSWHST_REG_DBG_SELECT, PSWHST_REG_DBG_DWORD_ENABLE,
	PSWHST_REG_DBG_SHIFT, PSWHST_REG_DBG_FORCE_VALID,
	PSWHST_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_HV, 0
};

static struct block_defs block_pswhst2_defs = {
	"pswhst2", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP},
	PSWHST2_REG_DBG_SELECT, PSWHST2_REG_DBG_DWORD_ENABLE,
	PSWHST2_REG_DBG_SHIFT, PSWHST2_REG_DBG_FORCE_VALID,
	PSWHST2_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_HV, 0
};

static struct block_defs block_pswrd_defs = {
	"pswrd", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP},
	PSWRD_REG_DBG_SELECT, PSWRD_REG_DBG_DWORD_ENABLE,
	PSWRD_REG_DBG_SHIFT, PSWRD_REG_DBG_FORCE_VALID,
	PSWRD_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_HV, 2
};

static struct block_defs block_pswrd2_defs = {
	"pswrd2", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP},
	PSWRD2_REG_DBG_SELECT, PSWRD2_REG_DBG_DWORD_ENABLE,
	PSWRD2_REG_DBG_SHIFT, PSWRD2_REG_DBG_FORCE_VALID,
	PSWRD2_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_HV, 2
};

static struct block_defs block_pswwr_defs = {
	"pswwr", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP},
	PSWWR_REG_DBG_SELECT, PSWWR_REG_DBG_DWORD_ENABLE,
	PSWWR_REG_DBG_SHIFT, PSWWR_REG_DBG_FORCE_VALID,
	PSWWR_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_HV, 3
};

static struct block_defs block_pswwr2_defs = {
	"pswwr2", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	true, false, DBG_RESET_REG_MISC_PL_HV, 3
};

static struct block_defs block_pswrq_defs = {
	"pswrq", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP},
	PSWRQ_REG_DBG_SELECT, PSWRQ_REG_DBG_DWORD_ENABLE,
	PSWRQ_REG_DBG_SHIFT, PSWRQ_REG_DBG_FORCE_VALID,
	PSWRQ_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_HV, 1
};

static struct block_defs block_pswrq2_defs = {
	"pswrq2", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP},
	PSWRQ2_REG_DBG_SELECT, PSWRQ2_REG_DBG_DWORD_ENABLE,
	PSWRQ2_REG_DBG_SHIFT, PSWRQ2_REG_DBG_FORCE_VALID,
	PSWRQ2_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISC_PL_HV, 1
};

static struct block_defs block_pglcs_defs = {
	"pglcs", {false, false, true}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCH},
	PGLCS_REG_DBG_SELECT, PGLCS_REG_DBG_DWORD_ENABLE,
	PGLCS_REG_DBG_SHIFT, PGLCS_REG_DBG_FORCE_VALID,
	PGLCS_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 2
};

static struct block_defs block_ptu_defs = {
	"ptu", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP},
	PTU_REG_DBG_SELECT, PTU_REG_DBG_DWORD_ENABLE,
	PTU_REG_DBG_SHIFT, PTU_REG_DBG_FORCE_VALID,
	PTU_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 20
};

static struct block_defs block_dmae_defs = {
	"dmae", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP},
	DMAE_REG_DBG_SELECT, DMAE_REG_DBG_DWORD_ENABLE,
	DMAE_REG_DBG_SHIFT, DMAE_REG_DBG_FORCE_VALID,
	DMAE_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 28
};

static struct block_defs block_tcm_defs = {
	"tcm", {true, true, true}, true, DBG_TSTORM_ID,
	{DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT},
	TCM_REG_DBG_SELECT, TCM_REG_DBG_DWORD_ENABLE,
	TCM_REG_DBG_SHIFT, TCM_REG_DBG_FORCE_VALID,
	TCM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 5
};

static struct block_defs block_mcm_defs = {
	"mcm", {true, true, true}, true, DBG_MSTORM_ID,
	{DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM},
	MCM_REG_DBG_SELECT, MCM_REG_DBG_DWORD_ENABLE,
	MCM_REG_DBG_SHIFT, MCM_REG_DBG_FORCE_VALID,
	MCM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 3
};

static struct block_defs block_ucm_defs = {
	"ucm", {true, true, true}, true, DBG_USTORM_ID,
	{DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU},
	UCM_REG_DBG_SELECT, UCM_REG_DBG_DWORD_ENABLE,
	UCM_REG_DBG_SHIFT, UCM_REG_DBG_FORCE_VALID,
	UCM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 8
};

static struct block_defs block_xcm_defs = {
	"xcm", {true, true, true}, true, DBG_XSTORM_ID,
	{DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX},
	XCM_REG_DBG_SELECT, XCM_REG_DBG_DWORD_ENABLE,
	XCM_REG_DBG_SHIFT, XCM_REG_DBG_FORCE_VALID,
	XCM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 19
};

static struct block_defs block_ycm_defs = {
	"ycm", {true, true, true}, true, DBG_YSTORM_ID,
	{DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCY},
	YCM_REG_DBG_SELECT, YCM_REG_DBG_DWORD_ENABLE,
	YCM_REG_DBG_SHIFT, YCM_REG_DBG_FORCE_VALID,
	YCM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 5
};

static struct block_defs block_pcm_defs = {
	"pcm", {true, true, true}, true, DBG_PSTORM_ID,
	{DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS},
	PCM_REG_DBG_SELECT, PCM_REG_DBG_DWORD_ENABLE,
	PCM_REG_DBG_SHIFT, PCM_REG_DBG_FORCE_VALID,
	PCM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 4
};

static struct block_defs block_qm_defs = {
	"qm", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCQ},
	QM_REG_DBG_SELECT, QM_REG_DBG_DWORD_ENABLE,
	QM_REG_DBG_SHIFT, QM_REG_DBG_FORCE_VALID,
	QM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 16
};

static struct block_defs block_tm_defs = {
	"tm", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS},
	TM_REG_DBG_SELECT, TM_REG_DBG_DWORD_ENABLE,
	TM_REG_DBG_SHIFT, TM_REG_DBG_FORCE_VALID,
	TM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 17
};

static struct block_defs block_dorq_defs = {
	"dorq", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCY},
	DORQ_REG_DBG_SELECT, DORQ_REG_DBG_DWORD_ENABLE,
	DORQ_REG_DBG_SHIFT, DORQ_REG_DBG_FORCE_VALID,
	DORQ_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 18
};

static struct block_defs block_brb_defs = {
	"brb", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCR, DBG_BUS_CLIENT_RBCR, DBG_BUS_CLIENT_RBCR},
	BRB_REG_DBG_SELECT, BRB_REG_DBG_DWORD_ENABLE,
	BRB_REG_DBG_SHIFT, BRB_REG_DBG_FORCE_VALID,
	BRB_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 0
};

static struct block_defs block_src_defs = {
	"src", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF},
	SRC_REG_DBG_SELECT, SRC_REG_DBG_DWORD_ENABLE,
	SRC_REG_DBG_SHIFT, SRC_REG_DBG_FORCE_VALID,
	SRC_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 2
};

static struct block_defs block_prs_defs = {
	"prs", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCR, DBG_BUS_CLIENT_RBCR, DBG_BUS_CLIENT_RBCR},
	PRS_REG_DBG_SELECT, PRS_REG_DBG_DWORD_ENABLE,
	PRS_REG_DBG_SHIFT, PRS_REG_DBG_FORCE_VALID,
	PRS_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 1
};

static struct block_defs block_tsdm_defs = {
	"tsdm", {true, true, true}, true, DBG_TSTORM_ID,
	{DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT},
	TSDM_REG_DBG_SELECT, TSDM_REG_DBG_DWORD_ENABLE,
	TSDM_REG_DBG_SHIFT, TSDM_REG_DBG_FORCE_VALID,
	TSDM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 3
};

static struct block_defs block_msdm_defs = {
	"msdm", {true, true, true}, true, DBG_MSTORM_ID,
	{DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM},
	MSDM_REG_DBG_SELECT, MSDM_REG_DBG_DWORD_ENABLE,
	MSDM_REG_DBG_SHIFT, MSDM_REG_DBG_FORCE_VALID,
	MSDM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 6
};

static struct block_defs block_usdm_defs = {
	"usdm", {true, true, true}, true, DBG_USTORM_ID,
	{DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU},
	USDM_REG_DBG_SELECT, USDM_REG_DBG_DWORD_ENABLE,
	USDM_REG_DBG_SHIFT, USDM_REG_DBG_FORCE_VALID,
	USDM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 7
};

static struct block_defs block_xsdm_defs = {
	"xsdm", {true, true, true}, true, DBG_XSTORM_ID,
	{DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX},
	XSDM_REG_DBG_SELECT, XSDM_REG_DBG_DWORD_ENABLE,
	XSDM_REG_DBG_SHIFT, XSDM_REG_DBG_FORCE_VALID,
	XSDM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 20
};

static struct block_defs block_ysdm_defs = {
	"ysdm", {true, true, true}, true, DBG_YSTORM_ID,
	{DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCY},
	YSDM_REG_DBG_SELECT, YSDM_REG_DBG_DWORD_ENABLE,
	YSDM_REG_DBG_SHIFT, YSDM_REG_DBG_FORCE_VALID,
	YSDM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 8
};

static struct block_defs block_psdm_defs = {
	"psdm", {true, true, true}, true, DBG_PSTORM_ID,
	{DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS},
	PSDM_REG_DBG_SELECT, PSDM_REG_DBG_DWORD_ENABLE,
	PSDM_REG_DBG_SHIFT, PSDM_REG_DBG_FORCE_VALID,
	PSDM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 7
};

static struct block_defs block_tsem_defs = {
	"tsem", {true, true, true}, true, DBG_TSTORM_ID,
	{DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT},
	TSEM_REG_DBG_SELECT, TSEM_REG_DBG_DWORD_ENABLE,
	TSEM_REG_DBG_SHIFT, TSEM_REG_DBG_FORCE_VALID,
	TSEM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 4
};

static struct block_defs block_msem_defs = {
	"msem", {true, true, true}, true, DBG_MSTORM_ID,
	{DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM},
	MSEM_REG_DBG_SELECT, MSEM_REG_DBG_DWORD_ENABLE,
	MSEM_REG_DBG_SHIFT, MSEM_REG_DBG_FORCE_VALID,
	MSEM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 9
};

static struct block_defs block_usem_defs = {
	"usem", {true, true, true}, true, DBG_USTORM_ID,
	{DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU},
	USEM_REG_DBG_SELECT, USEM_REG_DBG_DWORD_ENABLE,
	USEM_REG_DBG_SHIFT, USEM_REG_DBG_FORCE_VALID,
	USEM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 9
};

static struct block_defs block_xsem_defs = {
	"xsem", {true, true, true}, true, DBG_XSTORM_ID,
	{DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX},
	XSEM_REG_DBG_SELECT, XSEM_REG_DBG_DWORD_ENABLE,
	XSEM_REG_DBG_SHIFT, XSEM_REG_DBG_FORCE_VALID,
	XSEM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 21
};

static struct block_defs block_ysem_defs = {
	"ysem", {true, true, true}, true, DBG_YSTORM_ID,
	{DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCY},
	YSEM_REG_DBG_SELECT, YSEM_REG_DBG_DWORD_ENABLE,
	YSEM_REG_DBG_SHIFT, YSEM_REG_DBG_FORCE_VALID,
	YSEM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 11
};

static struct block_defs block_psem_defs = {
	"psem", {true, true, true}, true, DBG_PSTORM_ID,
	{DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS},
	PSEM_REG_DBG_SELECT, PSEM_REG_DBG_DWORD_ENABLE,
	PSEM_REG_DBG_SHIFT, PSEM_REG_DBG_FORCE_VALID,
	PSEM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 10
};

static struct block_defs block_rss_defs = {
	"rss", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT},
	RSS_REG_DBG_SELECT, RSS_REG_DBG_DWORD_ENABLE,
	RSS_REG_DBG_SHIFT, RSS_REG_DBG_FORCE_VALID,
	RSS_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 18
};

static struct block_defs block_tmld_defs = {
	"tmld", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM},
	TMLD_REG_DBG_SELECT, TMLD_REG_DBG_DWORD_ENABLE,
	TMLD_REG_DBG_SHIFT, TMLD_REG_DBG_FORCE_VALID,
	TMLD_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 13
};

static struct block_defs block_muld_defs = {
	"muld", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU},
	MULD_REG_DBG_SELECT, MULD_REG_DBG_DWORD_ENABLE,
	MULD_REG_DBG_SHIFT, MULD_REG_DBG_FORCE_VALID,
	MULD_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 14
};

static struct block_defs block_yuld_defs = {
	"yuld", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU, DBG_BUS_CLIENT_RBCU},
	YULD_REG_DBG_SELECT, YULD_REG_DBG_DWORD_ENABLE,
	YULD_REG_DBG_SHIFT, YULD_REG_DBG_FORCE_VALID,
	YULD_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 15
};

static struct block_defs block_xyld_defs = {
	"xyld", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX, DBG_BUS_CLIENT_RBCX},
	XYLD_REG_DBG_SELECT, XYLD_REG_DBG_DWORD_ENABLE,
	XYLD_REG_DBG_SHIFT, XYLD_REG_DBG_FORCE_VALID,
	XYLD_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 12
};

static struct block_defs block_prm_defs = {
	"prm", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM},
	PRM_REG_DBG_SELECT, PRM_REG_DBG_DWORD_ENABLE,
	PRM_REG_DBG_SHIFT, PRM_REG_DBG_FORCE_VALID,
	PRM_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 21
};

static struct block_defs block_pbf_pb1_defs = {
	"pbf_pb1", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCV},
	PBF_PB1_REG_DBG_SELECT, PBF_PB1_REG_DBG_DWORD_ENABLE,
	PBF_PB1_REG_DBG_SHIFT, PBF_PB1_REG_DBG_FORCE_VALID,
	PBF_PB1_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1,
	11
};

static struct block_defs block_pbf_pb2_defs = {
	"pbf_pb2", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCV},
	PBF_PB2_REG_DBG_SELECT, PBF_PB2_REG_DBG_DWORD_ENABLE,
	PBF_PB2_REG_DBG_SHIFT, PBF_PB2_REG_DBG_FORCE_VALID,
	PBF_PB2_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1,
	12
};

static struct block_defs block_rpb_defs = {
	"rpb", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM},
	RPB_REG_DBG_SELECT, RPB_REG_DBG_DWORD_ENABLE,
	RPB_REG_DBG_SHIFT, RPB_REG_DBG_FORCE_VALID,
	RPB_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 13
};

static struct block_defs block_btb_defs = {
	"btb", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCR, DBG_BUS_CLIENT_RBCR, DBG_BUS_CLIENT_RBCV},
	BTB_REG_DBG_SELECT, BTB_REG_DBG_DWORD_ENABLE,
	BTB_REG_DBG_SHIFT, BTB_REG_DBG_FORCE_VALID,
	BTB_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 10
};

static struct block_defs block_pbf_defs = {
	"pbf", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCV},
	PBF_REG_DBG_SELECT, PBF_REG_DBG_DWORD_ENABLE,
	PBF_REG_DBG_SHIFT, PBF_REG_DBG_FORCE_VALID,
	PBF_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 15
};

static struct block_defs block_rdif_defs = {
	"rdif", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCT, DBG_BUS_CLIENT_RBCM},
	RDIF_REG_DBG_SELECT, RDIF_REG_DBG_DWORD_ENABLE,
	RDIF_REG_DBG_SHIFT, RDIF_REG_DBG_FORCE_VALID,
	RDIF_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 16
};

static struct block_defs block_tdif_defs = {
	"tdif", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS, DBG_BUS_CLIENT_RBCS},
	TDIF_REG_DBG_SELECT, TDIF_REG_DBG_DWORD_ENABLE,
	TDIF_REG_DBG_SHIFT, TDIF_REG_DBG_FORCE_VALID,
	TDIF_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 17
};

static struct block_defs block_cdu_defs = {
	"cdu", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF},
	CDU_REG_DBG_SELECT, CDU_REG_DBG_DWORD_ENABLE,
	CDU_REG_DBG_SHIFT, CDU_REG_DBG_FORCE_VALID,
	CDU_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 23
};

static struct block_defs block_ccfc_defs = {
	"ccfc", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF},
	CCFC_REG_DBG_SELECT, CCFC_REG_DBG_DWORD_ENABLE,
	CCFC_REG_DBG_SHIFT, CCFC_REG_DBG_FORCE_VALID,
	CCFC_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 24
};

static struct block_defs block_tcfc_defs = {
	"tcfc", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF, DBG_BUS_CLIENT_RBCF},
	TCFC_REG_DBG_SELECT, TCFC_REG_DBG_DWORD_ENABLE,
	TCFC_REG_DBG_SHIFT, TCFC_REG_DBG_FORCE_VALID,
	TCFC_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 25
};

static struct block_defs block_igu_defs = {
	"igu", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP},
	IGU_REG_DBG_SELECT, IGU_REG_DBG_DWORD_ENABLE,
	IGU_REG_DBG_SHIFT, IGU_REG_DBG_FORCE_VALID,
	IGU_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_1, 27
};

static struct block_defs block_cau_defs = {
	"cau", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP, DBG_BUS_CLIENT_RBCP},
	CAU_REG_DBG_SELECT, CAU_REG_DBG_DWORD_ENABLE,
	CAU_REG_DBG_SHIFT, CAU_REG_DBG_FORCE_VALID,
	CAU_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VMAIN_2, 19
};

static struct block_defs block_umac_defs = {
	"umac", {false, false, true}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCZ},
	UMAC_REG_DBG_SELECT, UMAC_REG_DBG_DWORD_ENABLE,
	UMAC_REG_DBG_SHIFT, UMAC_REG_DBG_FORCE_VALID,
	UMAC_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 6
};

static struct block_defs block_xmac_defs = {
	"xmac", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0
};

static struct block_defs block_dbg_defs = {
	"dbg", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VAUX, 3
};

static struct block_defs block_nig_defs = {
	"nig", {true, true, true}, false, 0,
	{DBG_BUS_CLIENT_RBCN, DBG_BUS_CLIENT_RBCN, DBG_BUS_CLIENT_RBCN},
	NIG_REG_DBG_SELECT, NIG_REG_DBG_DWORD_ENABLE,
	NIG_REG_DBG_SHIFT, NIG_REG_DBG_FORCE_VALID,
	NIG_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VAUX, 0
};

static struct block_defs block_wol_defs = {
	"wol", {false, false, true}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCZ},
	WOL_REG_DBG_SELECT, WOL_REG_DBG_DWORD_ENABLE,
	WOL_REG_DBG_SHIFT, WOL_REG_DBG_FORCE_VALID,
	WOL_REG_DBG_FORCE_FRAME,
	true, true, DBG_RESET_REG_MISC_PL_PDA_VAUX, 7
};

static struct block_defs block_bmbn_defs = {
	"bmbn", {false, false, true}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCB},
	BMBN_REG_DBG_SELECT, BMBN_REG_DBG_DWORD_ENABLE,
	BMBN_REG_DBG_SHIFT, BMBN_REG_DBG_FORCE_VALID,
	BMBN_REG_DBG_FORCE_FRAME,
	false, false, MAX_DBG_RESET_REGS, 0
};

static struct block_defs block_ipc_defs = {
	"ipc", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	true, false, DBG_RESET_REG_MISCS_PL_UA, 8
};

static struct block_defs block_nwm_defs = {
	"nwm", {false, false, true}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCW},
	NWM_REG_DBG_SELECT, NWM_REG_DBG_DWORD_ENABLE,
	NWM_REG_DBG_SHIFT, NWM_REG_DBG_FORCE_VALID,
	NWM_REG_DBG_FORCE_FRAME,
	true, false, DBG_RESET_REG_MISCS_PL_HV_2, 0
};

static struct block_defs block_nws_defs = {
	"nws", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 12
};

static struct block_defs block_ms_defs = {
	"ms", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	true, false, DBG_RESET_REG_MISCS_PL_HV, 13
};

static struct block_defs block_phy_pcie_defs = {
	"phy_pcie", {false, false, true}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, DBG_BUS_CLIENT_RBCH},
	PCIE_REG_DBG_COMMON_SELECT, PCIE_REG_DBG_COMMON_DWORD_ENABLE,
	PCIE_REG_DBG_COMMON_SHIFT, PCIE_REG_DBG_COMMON_FORCE_VALID,
	PCIE_REG_DBG_COMMON_FORCE_FRAME,
	false, false, MAX_DBG_RESET_REGS, 0
};

static struct block_defs block_led_defs = {
	"led", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	true, true, DBG_RESET_REG_MISCS_PL_HV, 14
};

static struct block_defs block_misc_aeu_defs = {
	"misc_aeu", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0
};

static struct block_defs block_bar0_map_defs = {
	"bar0_map", {false, false, false}, false, 0,
	{MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS, MAX_DBG_BUS_CLIENTS},
	0, 0, 0, 0, 0,
	false, false, MAX_DBG_RESET_REGS, 0
};

static struct block_defs *s_block_defs[MAX_BLOCK_ID] = {
	&block_grc_defs,
	&block_miscs_defs,
	&block_misc_defs,
	&block_dbu_defs,
	&block_pglue_b_defs,
	&block_cnig_defs,
	&block_cpmu_defs,
	&block_ncsi_defs,
	&block_opte_defs,
	&block_bmb_defs,
	&block_pcie_defs,
	&block_mcp_defs,
	&block_mcp2_defs,
	&block_pswhst_defs,
	&block_pswhst2_defs,
	&block_pswrd_defs,
	&block_pswrd2_defs,
	&block_pswwr_defs,
	&block_pswwr2_defs,
	&block_pswrq_defs,
	&block_pswrq2_defs,
	&block_pglcs_defs,
	&block_dmae_defs,
	&block_ptu_defs,
	&block_tcm_defs,
	&block_mcm_defs,
	&block_ucm_defs,
	&block_xcm_defs,
	&block_ycm_defs,
	&block_pcm_defs,
	&block_qm_defs,
	&block_tm_defs,
	&block_dorq_defs,
	&block_brb_defs,
	&block_src_defs,
	&block_prs_defs,
	&block_tsdm_defs,
	&block_msdm_defs,
	&block_usdm_defs,
	&block_xsdm_defs,
	&block_ysdm_defs,
	&block_psdm_defs,
	&block_tsem_defs,
	&block_msem_defs,
	&block_usem_defs,
	&block_xsem_defs,
	&block_ysem_defs,
	&block_psem_defs,
	&block_rss_defs,
	&block_tmld_defs,
	&block_muld_defs,
	&block_yuld_defs,
	&block_xyld_defs,
	&block_prm_defs,
	&block_pbf_pb1_defs,
	&block_pbf_pb2_defs,
	&block_rpb_defs,
	&block_btb_defs,
	&block_pbf_defs,
	&block_rdif_defs,
	&block_tdif_defs,
	&block_cdu_defs,
	&block_ccfc_defs,
	&block_tcfc_defs,
	&block_igu_defs,
	&block_cau_defs,
	&block_umac_defs,
	&block_xmac_defs,
	&block_dbg_defs,
	&block_nig_defs,
	&block_wol_defs,
	&block_bmbn_defs,
	&block_ipc_defs,
	&block_nwm_defs,
	&block_nws_defs,
	&block_ms_defs,
	&block_phy_pcie_defs,
	&block_led_defs,
	&block_misc_aeu_defs,
	&block_bar0_map_defs,
};

static struct platform_defs s_platform_defs[] = {
	{"asic", 1},
	{"reserved", 0},
	{"reserved2", 0},
	{"reserved3", 0}
};

static struct grc_param_defs s_grc_param_defs[] = {
	{{1, 1, 1}, 0, 1, false, 1, 1},	/* DBG_GRC_PARAM_DUMP_TSTORM */
	{{1, 1, 1}, 0, 1, false, 1, 1},	/* DBG_GRC_PARAM_DUMP_MSTORM */
	{{1, 1, 1}, 0, 1, false, 1, 1},	/* DBG_GRC_PARAM_DUMP_USTORM */
	{{1, 1, 1}, 0, 1, false, 1, 1},	/* DBG_GRC_PARAM_DUMP_XSTORM */
	{{1, 1, 1}, 0, 1, false, 1, 1},	/* DBG_GRC_PARAM_DUMP_YSTORM */
	{{1, 1, 1}, 0, 1, false, 1, 1},	/* DBG_GRC_PARAM_DUMP_PSTORM */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_REGS */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_RAM */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_PBUF */
	{{0, 0, 0}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_IOR */
	{{0, 0, 0}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_VFC */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_CM_CTX */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_ILT */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_RSS */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_CAU */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_QM */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_MCP */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_RESERVED */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_CFC */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_IGU */
	{{0, 0, 0}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_BRB */
	{{0, 0, 0}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_BTB */
	{{0, 0, 0}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_BMB */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_NIG */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_MULD */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_PRS */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_DMAE */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_TM */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_SDM */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_DIF */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_STATIC */
	{{0, 0, 0}, 0, 1, false, 0, 0},	/* DBG_GRC_PARAM_UNSTALL */
	{{MAX_LCIDS, MAX_LCIDS, MAX_LCIDS}, 1, MAX_LCIDS, false, MAX_LCIDS,
	 MAX_LCIDS},			/* DBG_GRC_PARAM_NUM_LCIDS */
	{{MAX_LTIDS, MAX_LTIDS, MAX_LTIDS}, 1, MAX_LTIDS, false, MAX_LTIDS,
	 MAX_LTIDS},			/* DBG_GRC_PARAM_NUM_LTIDS */
	{{0, 0, 0}, 0, 1, true, 0, 0},	/* DBG_GRC_PARAM_EXCLUDE_ALL */
	{{0, 0, 0}, 0, 1, true, 0, 0},	/* DBG_GRC_PARAM_CRASH */
	{{0, 0, 0}, 0, 1, false, 1, 0},	/* DBG_GRC_PARAM_PARITY_SAFE */
	{{1, 1, 1}, 0, 1, false, 0, 1},	/* DBG_GRC_PARAM_DUMP_CM */
	{{1, 1, 1}, 0, 1, false, 0, 1}	/* DBG_GRC_PARAM_DUMP_PHY */
};

static struct rss_mem_defs s_rss_mem_defs[] = {
	{ "rss_mem_cid", "rss_cid", 0,
	  {256, 256, 320},
	  {32, 32, 32} },
	{ "rss_mem_key_msb", "rss_key", 1024,
	  {128, 128, 208},
	  {256, 256, 256} },
	{ "rss_mem_key_lsb", "rss_key", 2048,
	  {128, 128, 208},
	  {64, 64, 64} },
	{ "rss_mem_info", "rss_info", 3072,
	  {128, 128, 208},
	  {16, 16, 16} },
	{ "rss_mem_ind", "rss_ind", 4096,
	  {(128 * 128), (128 * 128), (128 * 208)},
	  {16, 16, 16} }
};

static struct vfc_ram_defs s_vfc_ram_defs[] = {
	{"vfc_ram_tt1", "vfc_ram", 0, 512},
	{"vfc_ram_mtt2", "vfc_ram", 512, 128},
	{"vfc_ram_stt2", "vfc_ram", 640, 32},
	{"vfc_ram_ro_vect", "vfc_ram", 672, 32}
};

static struct big_ram_defs s_big_ram_defs[] = {
	{ "BRB", MEM_GROUP_BRB_MEM, MEM_GROUP_BRB_RAM, DBG_GRC_PARAM_DUMP_BRB,
	  BRB_REG_BIG_RAM_ADDRESS, BRB_REG_BIG_RAM_DATA,
	  {4800, 4800, 5632} },
	{ "BTB", MEM_GROUP_BTB_MEM, MEM_GROUP_BTB_RAM, DBG_GRC_PARAM_DUMP_BTB,
	  BTB_REG_BIG_RAM_ADDRESS, BTB_REG_BIG_RAM_DATA,
	  {2880, 2880, 3680} },
	{ "BMB", MEM_GROUP_BMB_MEM, MEM_GROUP_BMB_RAM, DBG_GRC_PARAM_DUMP_BMB,
	  BMB_REG_BIG_RAM_ADDRESS, BMB_REG_BIG_RAM_DATA,
	  {1152, 1152, 1152} }
};

static struct reset_reg_defs s_reset_regs_defs[] = {
	{ MISCS_REG_RESET_PL_UA, 0x0,
	  {true, true, true} },		/* DBG_RESET_REG_MISCS_PL_UA */
	{ MISCS_REG_RESET_PL_HV, 0x0,
	  {true, true, true} },		/* DBG_RESET_REG_MISCS_PL_HV */
	{ MISCS_REG_RESET_PL_HV_2, 0x0,
	  {false, false, true} },	/* DBG_RESET_REG_MISCS_PL_HV_2 */
	{ MISC_REG_RESET_PL_UA, 0x0,
	  {true, true, true} },		/* DBG_RESET_REG_MISC_PL_UA */
	{ MISC_REG_RESET_PL_HV, 0x0,
	  {true, true, true} },		/* DBG_RESET_REG_MISC_PL_HV */
	{ MISC_REG_RESET_PL_PDA_VMAIN_1, 0x4404040,
	  {true, true, true} },		/* DBG_RESET_REG_MISC_PL_PDA_VMAIN_1 */
	{ MISC_REG_RESET_PL_PDA_VMAIN_2, 0x7c00007,
	  {true, true, true} },		/* DBG_RESET_REG_MISC_PL_PDA_VMAIN_2 */
	{ MISC_REG_RESET_PL_PDA_VAUX, 0x2,
	  {true, true, true} },		/* DBG_RESET_REG_MISC_PL_PDA_VAUX */
};

static struct phy_defs s_phy_defs[] = {
	{"nw_phy", NWS_REG_NWS_CMU, PHY_NW_IP_REG_PHY0_TOP_TBUS_ADDR_7_0,
	 PHY_NW_IP_REG_PHY0_TOP_TBUS_ADDR_15_8,
	 PHY_NW_IP_REG_PHY0_TOP_TBUS_DATA_7_0,
	 PHY_NW_IP_REG_PHY0_TOP_TBUS_DATA_11_8},
	{"sgmii_phy", MS_REG_MS_CMU, PHY_SGMII_IP_REG_AHB_CMU_CSR_0_X132,
	 PHY_SGMII_IP_REG_AHB_CMU_CSR_0_X133,
	 PHY_SGMII_IP_REG_AHB_CMU_CSR_0_X130,
	 PHY_SGMII_IP_REG_AHB_CMU_CSR_0_X131},
	{"pcie_phy0", PHY_PCIE_REG_PHY0, PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X132,
	 PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X133,
	 PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X130,
	 PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X131},
	{"pcie_phy1", PHY_PCIE_REG_PHY1, PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X132,
	 PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X133,
	 PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X130,
	 PHY_PCIE_IP_REG_AHB_CMU_CSR_0_X131},
};

/**************************** Private Functions ******************************/

/* Reads and returns a single dword from the specified unaligned buffer */
static u32 qed_read_unaligned_dword(u8 *buf)
{
	u32 dword;

	memcpy((u8 *)&dword, buf, sizeof(dword));
	return dword;
}

/* Initializes debug data for the specified device */
static enum dbg_status qed_dbg_dev_init(struct qed_hwfn *p_hwfn,
					struct qed_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	if (dev_data->initialized)
		return DBG_STATUS_OK;

	if (QED_IS_K2(p_hwfn->cdev)) {
		dev_data->chip_id = CHIP_K2;
		dev_data->mode_enable[MODE_K2] = 1;
	} else if (QED_IS_BB_B0(p_hwfn->cdev)) {
		dev_data->chip_id = CHIP_BB_B0;
		dev_data->mode_enable[MODE_BB_B0] = 1;
	} else {
		return DBG_STATUS_UNKNOWN_CHIP;
	}

	dev_data->platform_id = PLATFORM_ASIC;
	dev_data->mode_enable[MODE_ASIC] = 1;
	dev_data->initialized = true;
	return DBG_STATUS_OK;
}

/* Reads the FW info structure for the specified Storm from the chip,
 * and writes it to the specified fw_info pointer.
 */
static void qed_read_fw_info(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt,
			     u8 storm_id, struct fw_info *fw_info)
{
	/* Read first the address that points to fw_info location.
	 * The address is located in the last line of the Storm RAM.
	 */
	u32 addr = s_storm_defs[storm_id].sem_fast_mem_addr +
		   SEM_FAST_REG_INT_RAM +
		   DWORDS_TO_BYTES(SEM_FAST_REG_INT_RAM_SIZE) -
		   sizeof(struct fw_info_location);
	struct fw_info_location fw_info_location;
	u32 *dest = (u32 *)&fw_info_location;
	u32 i;

	memset(&fw_info_location, 0, sizeof(fw_info_location));
	memset(fw_info, 0, sizeof(*fw_info));
	for (i = 0; i < BYTES_TO_DWORDS(sizeof(fw_info_location));
	     i++, addr += BYTES_IN_DWORD)
		dest[i] = qed_rd(p_hwfn, p_ptt, addr);
	if (fw_info_location.size > 0 && fw_info_location.size <=
	    sizeof(*fw_info)) {
		/* Read FW version info from Storm RAM */
		addr = fw_info_location.grc_addr;
		dest = (u32 *)fw_info;
		for (i = 0; i < BYTES_TO_DWORDS(fw_info_location.size);
		     i++, addr += BYTES_IN_DWORD)
			dest[i] = qed_rd(p_hwfn, p_ptt, addr);
	}
}

/* Dumps the specified string to the specified buffer. Returns the dumped size
 * in bytes (actual length + 1 for the null character termination).
 */
static u32 qed_dump_str(char *dump_buf, bool dump, const char *str)
{
	if (dump)
		strcpy(dump_buf, str);
	return (u32)strlen(str) + 1;
}

/* Dumps zeros to align the specified buffer to dwords. Returns the dumped size
 * in bytes.
 */
static u32 qed_dump_align(char *dump_buf, bool dump, u32 byte_offset)
{
	u8 offset_in_dword = (u8)(byte_offset & 0x3), align_size;

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
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	char fw_ver_str[16] = EMPTY_FW_VERSION_STR;
	char fw_img_str[16] = EMPTY_FW_IMAGE_STR;
	struct fw_info fw_info = { {0}, {0} };
	int printed_chars;
	u32 offset = 0;

	if (dump) {
		/* Read FW image/version from PRAM in a non-reset SEMI */
		bool found = false;
		u8 storm_id;

		for (storm_id = 0; storm_id < MAX_DBG_STORMS && !found;
		     storm_id++) {
			/* Read FW version/image  */
			if (!dev_data->block_in_reset
			    [s_storm_defs[storm_id].block_id]) {
				/* read FW info for the current Storm */
				qed_read_fw_info(p_hwfn,
						 p_ptt, storm_id, &fw_info);

				/* Create FW version/image strings */
				printed_chars =
				    snprintf(fw_ver_str,
					     sizeof(fw_ver_str),
					     "%d_%d_%d_%d",
					     fw_info.ver.num.major,
					     fw_info.ver.num.minor,
					     fw_info.ver.num.rev,
					     fw_info.ver.num.eng);
				if (printed_chars < 0 || printed_chars >=
				    sizeof(fw_ver_str))
					DP_NOTICE(p_hwfn,
						  "Unexpected debug error: invalid FW version string\n");
				switch (fw_info.ver.image_id) {
				case FW_IMG_MAIN:
					strcpy(fw_img_str, "main");
					break;
				default:
					strcpy(fw_img_str, "unknown");
					break;
				}

				found = true;
			}
		}
	}

	/* Dump FW version, image and timestamp */
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "fw-version", fw_ver_str);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "fw-image", fw_img_str);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "fw-timestamp", fw_info.ver.timestamp);
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

	if (dump) {
		u32 global_section_offsize, global_section_addr, mfw_ver;
		u32 public_data_addr, global_section_offsize_addr;
		int printed_chars;

		/* Find MCP public data GRC address.
		 * Needs to be ORed with MCP_REG_SCRATCH due to a HW bug.
		 */
		public_data_addr = qed_rd(p_hwfn, p_ptt,
					  MISC_REG_SHARED_MEM_ADDR) |
					  MCP_REG_SCRATCH;

		/* Find MCP public global section offset */
		global_section_offsize_addr = public_data_addr +
					      offsetof(struct mcp_public_data,
						       sections) +
					      sizeof(offsize_t) * PUBLIC_GLOBAL;
		global_section_offsize = qed_rd(p_hwfn, p_ptt,
						global_section_offsize_addr);
		global_section_addr = MCP_REG_SCRATCH +
				      (global_section_offsize &
				       OFFSIZE_OFFSET_MASK) * 4;

		/* Read MFW version from MCP public global section */
		mfw_ver = qed_rd(p_hwfn, p_ptt,
				 global_section_addr +
				 offsetof(struct public_global, mfw_ver));

		/* Dump MFW version param */
		printed_chars = snprintf(mfw_ver_str, sizeof(mfw_ver_str),
					 "%d_%d_%d_%d",
					 (u8) (mfw_ver >> 24),
					 (u8) (mfw_ver >> 16),
					 (u8) (mfw_ver >> 8),
					 (u8) mfw_ver);
		if (printed_chars < 0 || printed_chars >= sizeof(mfw_ver_str))
			DP_NOTICE(p_hwfn,
				  "Unexpected debug error: invalid MFW version string\n");
	}

	return qed_dump_str_param(dump_buf, dump, "mfw-version", mfw_ver_str);
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
	u32 offset = 0;

	/* Find platform string and dump global params section header */
	offset += qed_dump_section_hdr(dump_buf + offset,
				       dump,
				       "global_params",
				       NUM_COMMON_GLOBAL_PARAMS +
				       num_specific_global_params);

	/* Store params */
	offset += qed_dump_fw_ver_param(p_hwfn, p_ptt, dump_buf + offset, dump);
	offset += qed_dump_mfw_ver_param(p_hwfn,
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
				     s_platform_defs[dev_data->platform_id].
				     name);
	offset +=
	    qed_dump_num_param(dump_buf + offset, dump, "pci-func",
			       p_hwfn->abs_pf_id);
	return offset;
}

/* Writes the last section to the specified buffer at the given offset.
 * Returns the dumped size in dwords.
 */
static u32 qed_dump_last_section(u32 *dump_buf, u32 offset, bool dump)
{
	u32 start_offset = offset, crc = ~0;

	/* Dump CRC section header */
	offset += qed_dump_section_hdr(dump_buf + offset, dump, "last", 0);

	/* Calculate CRC32 and add it to the dword following the "last" section.
	 */
	if (dump)
		*(dump_buf + offset) = ~crc32(crc, (u8 *)dump_buf,
					      DWORDS_TO_BYTES(offset));
	offset++;
	return offset - start_offset;
}

/* Update blocks reset state  */
static void qed_update_blocks_reset_state(struct qed_hwfn *p_hwfn,
					  struct qed_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 reg_val[MAX_DBG_RESET_REGS] = { 0 };
	u32 i;

	/* Read reset registers */
	for (i = 0; i < MAX_DBG_RESET_REGS; i++)
		if (s_reset_regs_defs[i].exists[dev_data->chip_id])
			reg_val[i] = qed_rd(p_hwfn,
					    p_ptt, s_reset_regs_defs[i].addr);

	/* Check if blocks are in reset */
	for (i = 0; i < MAX_BLOCK_ID; i++)
		dev_data->block_in_reset[i] =
		    s_block_defs[i]->has_reset_bit &&
		    !(reg_val[s_block_defs[i]->reset_reg] &
		      BIT(s_block_defs[i]->reset_bit_offset));
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
	u32 dbg_reset_reg_addr, old_reset_reg_val, new_reset_reg_val;

	dbg_reset_reg_addr =
		s_reset_regs_defs[s_block_defs[BLOCK_DBG]->reset_reg].addr;
	old_reset_reg_val = qed_rd(p_hwfn, p_ptt, dbg_reset_reg_addr);
	new_reset_reg_val = old_reset_reg_val &
			    ~BIT(s_block_defs[BLOCK_DBG]->reset_bit_offset);

	qed_wr(p_hwfn, p_ptt, dbg_reset_reg_addr, new_reset_reg_val);
	qed_wr(p_hwfn, p_ptt, dbg_reset_reg_addr, old_reset_reg_val);
}

static void qed_bus_set_framing_mode(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     enum dbg_bus_frame_modes mode)
{
	qed_wr(p_hwfn, p_ptt, DBG_REG_FRAMING_MODE, (u8)mode);
}

/* Enable / disable Debug Bus clients according to the specified mask.
 * (1 = enable, 0 = disable)
 */
static void qed_bus_enable_clients(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt, u32 client_mask)
{
	qed_wr(p_hwfn, p_ptt, DBG_REG_CLIENT_ENABLE, client_mask);
}

static bool qed_is_mode_match(struct qed_hwfn *p_hwfn, u16 *modes_buf_offset)
{
	const u32 *ptr = s_dbg_arrays[BIN_BUF_DBG_MODE_TREE].ptr;
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u8 tree_val = ((u8 *)ptr)[(*modes_buf_offset)++];
	bool arg1, arg2;

	switch (tree_val) {
	case INIT_MODE_OP_NOT:
		return !qed_is_mode_match(p_hwfn, modes_buf_offset);
	case INIT_MODE_OP_OR:
	case INIT_MODE_OP_AND:
		arg1 = qed_is_mode_match(p_hwfn, modes_buf_offset);
		arg2 = qed_is_mode_match(p_hwfn, modes_buf_offset);
		return (tree_val == INIT_MODE_OP_OR) ? (arg1 ||
							arg2) : (arg1 && arg2);
	default:
		return dev_data->mode_enable[tree_val - MAX_INIT_MODE_OPS] > 0;
	}
}

/* Returns the value of the specified GRC param */
static u32 qed_grc_get_param(struct qed_hwfn *p_hwfn,
			     enum dbg_grc_params grc_param)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	return dev_data->grc.param_val[grc_param];
}

/* Clear all GRC params */
static void qed_dbg_grc_clear_params(struct qed_hwfn *p_hwfn)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 i;

	for (i = 0; i < MAX_DBG_GRC_PARAMS; i++)
		dev_data->grc.param_set_by_user[i] = 0;
}

/* Assign default GRC param values */
static void qed_dbg_grc_set_params_default(struct qed_hwfn *p_hwfn)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 i;

	for (i = 0; i < MAX_DBG_GRC_PARAMS; i++)
		if (!dev_data->grc.param_set_by_user[i])
			dev_data->grc.param_val[i] =
			    s_grc_param_defs[i].default_val[dev_data->chip_id];
}

/* Returns true if the specified entity (indicated by GRC param) should be
 * included in the dump, false otherwise.
 */
static bool qed_grc_is_included(struct qed_hwfn *p_hwfn,
				enum dbg_grc_params grc_param)
{
	return qed_grc_get_param(p_hwfn, grc_param) > 0;
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
	u8 i;

	/* Check Storm match */
	if (s_block_defs[block_id]->associated_to_storm &&
	    !qed_grc_is_storm_included(p_hwfn,
			(enum dbg_storms)s_block_defs[block_id]->storm_id))
		return false;

	for (i = 0; i < NUM_BIG_RAM_TYPES; i++)
		if (mem_group_id == s_big_ram_defs[i].mem_group_id ||
		    mem_group_id == s_big_ram_defs[i].ram_mem_group_id)
			return qed_grc_is_included(p_hwfn,
						   s_big_ram_defs[i].grc_param);
	if (mem_group_id == MEM_GROUP_PXP_ILT || mem_group_id ==
	    MEM_GROUP_PXP_MEM)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_PXP);
	if (mem_group_id == MEM_GROUP_RAM)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_RAM);
	if (mem_group_id == MEM_GROUP_PBUF)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_PBUF);
	if (mem_group_id == MEM_GROUP_CAU_MEM ||
	    mem_group_id == MEM_GROUP_CAU_SB ||
	    mem_group_id == MEM_GROUP_CAU_PI)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_CAU);
	if (mem_group_id == MEM_GROUP_QM_MEM)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_QM);
	if (mem_group_id == MEM_GROUP_CONN_CFC_MEM ||
	    mem_group_id == MEM_GROUP_TASK_CFC_MEM)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_CFC);
	if (mem_group_id == MEM_GROUP_IGU_MEM || mem_group_id ==
	    MEM_GROUP_IGU_MSIX)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_IGU);
	if (mem_group_id == MEM_GROUP_MULD_MEM)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_MULD);
	if (mem_group_id == MEM_GROUP_PRS_MEM)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_PRS);
	if (mem_group_id == MEM_GROUP_DMAE_MEM)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_DMAE);
	if (mem_group_id == MEM_GROUP_TM_MEM)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_TM);
	if (mem_group_id == MEM_GROUP_SDM_MEM)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_SDM);
	if (mem_group_id == MEM_GROUP_TDIF_CTX || mem_group_id ==
	    MEM_GROUP_RDIF_CTX)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_DIF);
	if (mem_group_id == MEM_GROUP_CM_MEM)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_CM);
	if (mem_group_id == MEM_GROUP_IOR)
		return qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_IOR);

	return true;
}

/* Stalls all Storms */
static void qed_grc_stall_storms(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt, bool stall)
{
	u8 reg_val = stall ? 1 : 0;
	u8 storm_id;

	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		if (qed_grc_is_storm_included(p_hwfn,
					      (enum dbg_storms)storm_id)) {
			u32 reg_addr =
			    s_storm_defs[storm_id].sem_fast_mem_addr +
			    SEM_FAST_REG_STALL_0;

			qed_wr(p_hwfn, p_ptt, reg_addr, reg_val);
		}
	}

	msleep(STALL_DELAY_MS);
}

/* Takes all blocks out of reset */
static void qed_grc_unreset_blocks(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 reg_val[MAX_DBG_RESET_REGS] = { 0 };
	u32 i;

	/* Fill reset regs values */
	for (i = 0; i < MAX_BLOCK_ID; i++)
		if (s_block_defs[i]->has_reset_bit && s_block_defs[i]->unreset)
			reg_val[s_block_defs[i]->reset_reg] |=
			    BIT(s_block_defs[i]->reset_bit_offset);

	/* Write reset registers */
	for (i = 0; i < MAX_DBG_RESET_REGS; i++) {
		if (s_reset_regs_defs[i].exists[dev_data->chip_id]) {
			reg_val[i] |= s_reset_regs_defs[i].unreset_val;
			if (reg_val[i])
				qed_wr(p_hwfn,
				       p_ptt,
				       s_reset_regs_defs[i].addr +
				       RESET_REG_UNRESET_OFFSET, reg_val[i]);
		}
	}
}

/* Returns the attention name offsets of the specified block */
static const struct dbg_attn_block_type_data *
qed_get_block_attn_data(enum block_id block_id, enum dbg_attn_type attn_type)
{
	const struct dbg_attn_block *base_attn_block_arr =
		(const struct dbg_attn_block *)
		s_dbg_arrays[BIN_BUF_DBG_ATTN_BLOCKS].ptr;

	return &base_attn_block_arr[block_id].per_type_data[attn_type];
}

/* Returns the attention registers of the specified block */
static const struct dbg_attn_reg *
qed_get_block_attn_regs(enum block_id block_id, enum dbg_attn_type attn_type,
			u8 *num_attn_regs)
{
	const struct dbg_attn_block_type_data *block_type_data =
		qed_get_block_attn_data(block_id, attn_type);

	*num_attn_regs = block_type_data->num_regs;
	return &((const struct dbg_attn_reg *)
		 s_dbg_arrays[BIN_BUF_DBG_ATTN_REGS].ptr)[block_type_data->
							  regs_offset];
}

/* For each block, clear the status of all parities */
static void qed_grc_clear_all_prty(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u8 reg_idx, num_attn_regs;
	u32 block_id;

	for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
		const struct dbg_attn_reg *attn_reg_arr;

		if (dev_data->block_in_reset[block_id])
			continue;

		attn_reg_arr = qed_get_block_attn_regs((enum block_id)block_id,
						       ATTN_TYPE_PARITY,
						       &num_attn_regs);
		for (reg_idx = 0; reg_idx < num_attn_regs; reg_idx++) {
			const struct dbg_attn_reg *reg_data =
				&attn_reg_arr[reg_idx];

			/* Check mode */
			bool eval_mode = GET_FIELD(reg_data->mode.data,
						   DBG_MODE_HDR_EVAL_MODE) > 0;
			u16 modes_buf_offset =
				GET_FIELD(reg_data->mode.data,
					  DBG_MODE_HDR_MODES_BUF_OFFSET);

			if (!eval_mode ||
			    qed_is_mode_match(p_hwfn, &modes_buf_offset))
				/* Mode match - read parity status read-clear
				 * register.
				 */
				qed_rd(p_hwfn, p_ptt,
				       DWORDS_TO_BYTES(reg_data->
						       sts_clr_address));
		}
	}
}

/* Dumps GRC registers section header. Returns the dumped size in dwords.
 * The following parameters are dumped:
 * - 'count' = num_dumped_entries
 * - 'split' = split_type
 * - 'id'i = split_id (dumped only if split_id >= 0)
 * - 'param_name' = param_val (user param, dumped only if param_name != NULL and
 *	param_val != NULL)
 */
static u32 qed_grc_dump_regs_hdr(u32 *dump_buf,
				 bool dump,
				 u32 num_reg_entries,
				 const char *split_type,
				 int split_id,
				 const char *param_name, const char *param_val)
{
	u8 num_params = 2 + (split_id >= 0 ? 1 : 0) + (param_name ? 1 : 0);
	u32 offset = 0;

	offset += qed_dump_section_hdr(dump_buf + offset,
				       dump, "grc_regs", num_params);
	offset += qed_dump_num_param(dump_buf + offset,
				     dump, "count", num_reg_entries);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "split", split_type);
	if (split_id >= 0)
		offset += qed_dump_num_param(dump_buf + offset,
					     dump, "id", split_id);
	if (param_name && param_val)
		offset += qed_dump_str_param(dump_buf + offset,
					     dump, param_name, param_val);
	return offset;
}

/* Dumps GRC register/memory. Returns the dumped size in dwords. */
static u32 qed_grc_dump_reg_entry(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt, u32 *dump_buf,
				  bool dump, u32 addr, u32 len)
{
	u32 offset = 0, i;

	if (dump) {
		*(dump_buf + offset++) = addr | (len << REG_DUMP_LEN_SHIFT);
		for (i = 0; i < len; i++, addr++, offset++)
			*(dump_buf + offset) = qed_rd(p_hwfn,
						      p_ptt,
						      DWORDS_TO_BYTES(addr));
	} else {
		offset += len + 1;
	}

	return offset;
}

/* Dumps GRC registers entries. Returns the dumped size in dwords. */
static u32 qed_grc_dump_regs_entries(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     struct dbg_array input_regs_arr,
				     u32 *dump_buf,
				     bool dump,
				     bool block_enable[MAX_BLOCK_ID],
				     u32 *num_dumped_reg_entries)
{
	u32 i, offset = 0, input_offset = 0;
	bool mode_match = true;

	*num_dumped_reg_entries = 0;
	while (input_offset < input_regs_arr.size_in_dwords) {
		const struct dbg_dump_cond_hdr *cond_hdr =
		    (const struct dbg_dump_cond_hdr *)
		    &input_regs_arr.ptr[input_offset++];
		bool eval_mode = GET_FIELD(cond_hdr->mode.data,
					   DBG_MODE_HDR_EVAL_MODE) > 0;

		/* Check mode/block */
		if (eval_mode) {
			u16 modes_buf_offset =
				GET_FIELD(cond_hdr->mode.data,
					  DBG_MODE_HDR_MODES_BUF_OFFSET);
			mode_match = qed_is_mode_match(p_hwfn,
						       &modes_buf_offset);
		}

		if (mode_match && block_enable[cond_hdr->block_id]) {
			for (i = 0; i < cond_hdr->data_size;
			     i++, input_offset++) {
				const struct dbg_dump_reg *reg =
				    (const struct dbg_dump_reg *)
				    &input_regs_arr.ptr[input_offset];

				offset +=
					qed_grc_dump_reg_entry(p_hwfn, p_ptt,
						    dump_buf + offset, dump,
						    GET_FIELD(reg->data,
							DBG_DUMP_REG_ADDRESS),
						    GET_FIELD(reg->data,
							DBG_DUMP_REG_LENGTH));
				(*num_dumped_reg_entries)++;
			}
		} else {
			input_offset += cond_hdr->data_size;
		}
	}

	return offset;
}

/* Dumps GRC registers entries. Returns the dumped size in dwords. */
static u32 qed_grc_dump_split_data(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   struct dbg_array input_regs_arr,
				   u32 *dump_buf,
				   bool dump,
				   bool block_enable[MAX_BLOCK_ID],
				   const char *split_type_name,
				   u32 split_id,
				   const char *param_name,
				   const char *param_val)
{
	u32 num_dumped_reg_entries, offset;

	/* Calculate register dump header size (and skip it for now) */
	offset = qed_grc_dump_regs_hdr(dump_buf,
				       false,
				       0,
				       split_type_name,
				       split_id, param_name, param_val);

	/* Dump registers */
	offset += qed_grc_dump_regs_entries(p_hwfn,
					    p_ptt,
					    input_regs_arr,
					    dump_buf + offset,
					    dump,
					    block_enable,
					    &num_dumped_reg_entries);

	/* Write register dump header */
	if (dump && num_dumped_reg_entries > 0)
		qed_grc_dump_regs_hdr(dump_buf,
				      dump,
				      num_dumped_reg_entries,
				      split_type_name,
				      split_id, param_name, param_val);

	return num_dumped_reg_entries > 0 ? offset : 0;
}

/* Dumps registers according to the input registers array.
 * Returns the dumped size in dwords.
 */
static u32 qed_grc_dump_registers(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  u32 *dump_buf,
				  bool dump,
				  bool block_enable[MAX_BLOCK_ID],
				  const char *param_name, const char *param_val)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 offset = 0, input_offset = 0;
	u8 port_id, pf_id;

	if (dump)
		DP_VERBOSE(p_hwfn, QED_MSG_DEBUG, "Dumping registers...\n");
	while (input_offset <
	       s_dbg_arrays[BIN_BUF_DBG_DUMP_REG].size_in_dwords) {
		const struct dbg_dump_split_hdr *split_hdr =
			(const struct dbg_dump_split_hdr *)
			&s_dbg_arrays[BIN_BUF_DBG_DUMP_REG].ptr[input_offset++];
		u8 split_type_id = GET_FIELD(split_hdr->hdr,
					     DBG_DUMP_SPLIT_HDR_SPLIT_TYPE_ID);
		u32 split_data_size = GET_FIELD(split_hdr->hdr,
						DBG_DUMP_SPLIT_HDR_DATA_SIZE);
		struct dbg_array curr_input_regs_arr = {
			&s_dbg_arrays[BIN_BUF_DBG_DUMP_REG].ptr[input_offset],
			split_data_size};

		switch (split_type_id) {
		case SPLIT_TYPE_NONE:
		case SPLIT_TYPE_VF:
			offset += qed_grc_dump_split_data(p_hwfn,
							  p_ptt,
							  curr_input_regs_arr,
							  dump_buf + offset,
							  dump,
							  block_enable,
							  "eng",
							  (u32)(-1),
							  param_name,
							  param_val);
			break;
		case SPLIT_TYPE_PORT:
			for (port_id = 0;
			     port_id <
			     s_chip_defs[dev_data->chip_id].
			     per_platform[dev_data->platform_id].num_ports;
			     port_id++) {
				if (dump)
					qed_port_pretend(p_hwfn, p_ptt,
							 port_id);
				offset +=
				    qed_grc_dump_split_data(p_hwfn, p_ptt,
							    curr_input_regs_arr,
							    dump_buf + offset,
							    dump, block_enable,
							    "port", port_id,
							    param_name,
							    param_val);
			}
			break;
		case SPLIT_TYPE_PF:
		case SPLIT_TYPE_PORT_PF:
			for (pf_id = 0;
			     pf_id <
			     s_chip_defs[dev_data->chip_id].
			     per_platform[dev_data->platform_id].num_pfs;
			     pf_id++) {
				if (dump)
					qed_fid_pretend(p_hwfn, p_ptt, pf_id);
				offset += qed_grc_dump_split_data(p_hwfn,
							p_ptt,
							curr_input_regs_arr,
							dump_buf + offset,
							dump, block_enable,
							"pf", pf_id, param_name,
							param_val);
			}
			break;
		default:
			break;
		}

		input_offset += split_data_size;
	}

	/* Pretend to original PF */
	if (dump)
		qed_fid_pretend(p_hwfn, p_ptt, p_hwfn->rel_pf_id);
	return offset;
}

/* Dump reset registers. Returns the dumped size in dwords. */
static u32 qed_grc_dump_reset_regs(struct qed_hwfn *p_hwfn,
				   struct qed_ptt *p_ptt,
				   u32 *dump_buf, bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 i, offset = 0, num_regs = 0;

	/* Calculate header size */
	offset += qed_grc_dump_regs_hdr(dump_buf,
					false, 0, "eng", -1, NULL, NULL);

	/* Write reset registers */
	for (i = 0; i < MAX_DBG_RESET_REGS; i++) {
		if (s_reset_regs_defs[i].exists[dev_data->chip_id]) {
			offset += qed_grc_dump_reg_entry(p_hwfn,
							 p_ptt,
							 dump_buf + offset,
							 dump,
							 BYTES_TO_DWORDS
							 (s_reset_regs_defs
							  [i].addr), 1);
			num_regs++;
		}
	}

	/* Write header */
	if (dump)
		qed_grc_dump_regs_hdr(dump_buf,
				      true, num_regs, "eng", -1, NULL, NULL);
	return offset;
}

/* Dump registers that are modified during GRC Dump and therefore must be dumped
 * first. Returns the dumped size in dwords.
 */
static u32 qed_grc_dump_modified_regs(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      u32 *dump_buf, bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 offset = 0, num_reg_entries = 0, block_id;
	u8 storm_id, reg_idx, num_attn_regs;

	/* Calculate header size */
	offset += qed_grc_dump_regs_hdr(dump_buf,
					false, 0, "eng", -1, NULL, NULL);

	/* Write parity registers */
	for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
		const struct dbg_attn_reg *attn_reg_arr;

		if (dev_data->block_in_reset[block_id] && dump)
			continue;

		attn_reg_arr = qed_get_block_attn_regs((enum block_id)block_id,
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
			if (!eval_mode ||
			    qed_is_mode_match(p_hwfn, &modes_buf_offset)) {
				/* Mode match - read and dump registers */
				offset += qed_grc_dump_reg_entry(p_hwfn,
							p_ptt,
							dump_buf + offset,
							dump,
							reg_data->mask_address,
							1);
				offset += qed_grc_dump_reg_entry(p_hwfn,
						p_ptt,
						dump_buf + offset,
						dump,
						GET_FIELD(reg_data->data,
						    DBG_ATTN_REG_STS_ADDRESS),
						1);
				num_reg_entries += 2;
			}
		}
	}

	/* Write storm stall status registers */
	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		if (dev_data->block_in_reset[s_storm_defs[storm_id].block_id] &&
		    dump)
			continue;

		offset += qed_grc_dump_reg_entry(p_hwfn,
					p_ptt,
					dump_buf + offset,
					dump,
					BYTES_TO_DWORDS(s_storm_defs[storm_id].
							sem_fast_mem_addr +
							SEM_FAST_REG_STALLED),
					1);
		num_reg_entries++;
	}

	/* Write header */
	if (dump)
		qed_grc_dump_regs_hdr(dump_buf,
				      true,
				      num_reg_entries, "eng", -1, NULL, NULL);
	return offset;
}

/* Dumps a GRC memory header (section and params).
 * The following parameters are dumped:
 * name - name is dumped only if it's not NULL.
 * addr - byte_addr is dumped only if name is NULL.
 * len - dword_len is always dumped.
 * width - bit_width is dumped if it's not zero.
 * packed - packed=1 is dumped if it's not false.
 * mem_group - mem_group is always dumped.
 * is_storm - true only if the memory is related to a Storm.
 * storm_letter - storm letter (valid only if is_storm is true).
 * Returns the dumped size in dwords.
 */
static u32 qed_grc_dump_mem_hdr(struct qed_hwfn *p_hwfn,
				u32 *dump_buf,
				bool dump,
				const char *name,
				u32 byte_addr,
				u32 dword_len,
				u32 bit_width,
				bool packed,
				const char *mem_group,
				bool is_storm, char storm_letter)
{
	u8 num_params = 3;
	u32 offset = 0;
	char buf[64];

	if (!dword_len)
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
		if (is_storm) {
			strcpy(buf, "?STORM_");
			buf[0] = storm_letter;
			strcpy(buf + strlen(buf), name);
		} else {
			strcpy(buf, name);
		}

		offset += qed_dump_str_param(dump_buf + offset,
					     dump, "name", buf);
		if (dump)
			DP_VERBOSE(p_hwfn,
				   QED_MSG_DEBUG,
				   "Dumping %d registers from %s...\n",
				   dword_len, buf);
	} else {
		/* Dump address */
		offset += qed_dump_num_param(dump_buf + offset,
					     dump, "addr", byte_addr);
		if (dump && dword_len > 64)
			DP_VERBOSE(p_hwfn,
				   QED_MSG_DEBUG,
				   "Dumping %d registers from address 0x%x...\n",
				   dword_len, byte_addr);
	}

	/* Dump len */
	offset += qed_dump_num_param(dump_buf + offset, dump, "len", dword_len);

	/* Dump bit width */
	if (bit_width)
		offset += qed_dump_num_param(dump_buf + offset,
					     dump, "width", bit_width);

	/* Dump packed */
	if (packed)
		offset += qed_dump_num_param(dump_buf + offset,
					     dump, "packed", 1);

	/* Dump reg type */
	if (is_storm) {
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
 */
static u32 qed_grc_dump_mem(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt,
			    u32 *dump_buf,
			    bool dump,
			    const char *name,
			    u32 byte_addr,
			    u32 dword_len,
			    u32 bit_width,
			    bool packed,
			    const char *mem_group,
			    bool is_storm, char storm_letter)
{
	u32 offset = 0;

	offset += qed_grc_dump_mem_hdr(p_hwfn,
				       dump_buf + offset,
				       dump,
				       name,
				       byte_addr,
				       dword_len,
				       bit_width,
				       packed,
				       mem_group, is_storm, storm_letter);
	if (dump) {
		u32 i;

		for (i = 0; i < dword_len;
		     i++, byte_addr += BYTES_IN_DWORD, offset++)
			*(dump_buf + offset) = qed_rd(p_hwfn, p_ptt, byte_addr);
	} else {
		offset += dword_len;
	}

	return offset;
}

/* Dumps GRC memories entries. Returns the dumped size in dwords. */
static u32 qed_grc_dump_mem_entries(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    struct dbg_array input_mems_arr,
				    u32 *dump_buf, bool dump)
{
	u32 i, offset = 0, input_offset = 0;
	bool mode_match = true;

	while (input_offset < input_mems_arr.size_in_dwords) {
		const struct dbg_dump_cond_hdr *cond_hdr;
		u32 num_entries;
		bool eval_mode;

		cond_hdr = (const struct dbg_dump_cond_hdr *)
			   &input_mems_arr.ptr[input_offset++];
		eval_mode = GET_FIELD(cond_hdr->mode.data,
				      DBG_MODE_HDR_EVAL_MODE) > 0;

		/* Check required mode */
		if (eval_mode) {
			u16 modes_buf_offset =
				GET_FIELD(cond_hdr->mode.data,
					  DBG_MODE_HDR_MODES_BUF_OFFSET);

			mode_match = qed_is_mode_match(p_hwfn,
						       &modes_buf_offset);
		}

		if (!mode_match) {
			input_offset += cond_hdr->data_size;
			continue;
		}

		num_entries = cond_hdr->data_size / MEM_DUMP_ENTRY_SIZE_DWORDS;
		for (i = 0; i < num_entries;
		     i++, input_offset += MEM_DUMP_ENTRY_SIZE_DWORDS) {
			const struct dbg_dump_mem *mem =
				(const struct dbg_dump_mem *)
				&input_mems_arr.ptr[input_offset];
			u8 mem_group_id;

			mem_group_id = GET_FIELD(mem->dword0,
						 DBG_DUMP_MEM_MEM_GROUP_ID);
			if (mem_group_id >= MEM_GROUPS_NUM) {
				DP_NOTICE(p_hwfn, "Invalid mem_group_id\n");
				return 0;
			}

			if (qed_grc_is_mem_included(p_hwfn,
					(enum block_id)cond_hdr->block_id,
					mem_group_id)) {
				u32 mem_byte_addr =
					DWORDS_TO_BYTES(GET_FIELD(mem->dword0,
							DBG_DUMP_MEM_ADDRESS));
				u32 mem_len = GET_FIELD(mem->dword1,
							DBG_DUMP_MEM_LENGTH);
				char storm_letter = 'a';
				bool is_storm = false;

				/* Update memory length for CCFC/TCFC memories
				 * according to number of LCIDs/LTIDs.
				 */
				if (mem_group_id == MEM_GROUP_CONN_CFC_MEM)
					mem_len = qed_grc_get_param(p_hwfn,
							DBG_GRC_PARAM_NUM_LCIDS)
							* (mem_len / MAX_LCIDS);
				else if (mem_group_id == MEM_GROUP_TASK_CFC_MEM)
					mem_len = qed_grc_get_param(p_hwfn,
							DBG_GRC_PARAM_NUM_LTIDS)
							* (mem_len / MAX_LTIDS);

				/* If memory is associated with Storm, update
				 * Storm details.
				 */
				if (s_block_defs[cond_hdr->block_id]->
							associated_to_storm) {
					is_storm = true;
					storm_letter =
						s_storm_defs[s_block_defs[
						cond_hdr->block_id]->
						storm_id].letter;
				}

				/* Dump memory */
				offset += qed_grc_dump_mem(p_hwfn, p_ptt,
						dump_buf + offset, dump, NULL,
						mem_byte_addr, mem_len, 0,
						false,
						s_mem_group_names[mem_group_id],
						is_storm, storm_letter);
				}
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
	u32 offset = 0, input_offset = 0;

	while (input_offset <
	       s_dbg_arrays[BIN_BUF_DBG_DUMP_MEM].size_in_dwords) {
		const struct dbg_dump_split_hdr *split_hdr =
			(const struct dbg_dump_split_hdr *)
			&s_dbg_arrays[BIN_BUF_DBG_DUMP_MEM].ptr[input_offset++];
		u8 split_type_id = GET_FIELD(split_hdr->hdr,
					     DBG_DUMP_SPLIT_HDR_SPLIT_TYPE_ID);
		u32 split_data_size = GET_FIELD(split_hdr->hdr,
						DBG_DUMP_SPLIT_HDR_DATA_SIZE);
		struct dbg_array curr_input_mems_arr = {
			&s_dbg_arrays[BIN_BUF_DBG_DUMP_MEM].ptr[input_offset],
			split_data_size};

		switch (split_type_id) {
		case SPLIT_TYPE_NONE:
			offset += qed_grc_dump_mem_entries(p_hwfn,
							   p_ptt,
							   curr_input_mems_arr,
							   dump_buf + offset,
							   dump);
			break;
		default:
			DP_NOTICE(p_hwfn,
				  "Dumping split memories is currently not supported\n");
			break;
		}

		input_offset += split_data_size;
	}

	return offset;
}

/* Dumps GRC context data for the specified Storm.
 * Returns the dumped size in dwords.
 */
static u32 qed_grc_dump_ctx_data(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 u32 *dump_buf,
				 bool dump,
				 const char *name,
				 u32 num_lids,
				 u32 lid_size,
				 u32 rd_reg_addr,
				 u8 storm_id)
{
	u32 i, lid, total_size;
	u32 offset = 0;

	if (!lid_size)
		return 0;
	lid_size *= BYTES_IN_DWORD;
	total_size = num_lids * lid_size;
	offset += qed_grc_dump_mem_hdr(p_hwfn,
				       dump_buf + offset,
				       dump,
				       name,
				       0,
				       total_size,
				       lid_size * 32,
				       false,
				       name,
				       true, s_storm_defs[storm_id].letter);

	/* Dump context data */
	if (dump) {
		for (lid = 0; lid < num_lids; lid++) {
			for (i = 0; i < lid_size; i++, offset++) {
				qed_wr(p_hwfn,
				       p_ptt,
				       s_storm_defs[storm_id].cm_ctx_wr_addr,
				       BIT(9) | lid);
				*(dump_buf + offset) = qed_rd(p_hwfn,
							      p_ptt,
							      rd_reg_addr);
			}
		}
	} else {
		offset += total_size;
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
		offset +=
			qed_grc_dump_ctx_data(p_hwfn,
					      p_ptt,
					      dump_buf + offset,
					      dump,
					      "CONN_AG_CTX",
					      qed_grc_get_param(p_hwfn,
						    DBG_GRC_PARAM_NUM_LCIDS),
					      s_storm_defs[storm_id].
						    cm_conn_ag_ctx_lid_size,
					      s_storm_defs[storm_id].
						    cm_conn_ag_ctx_rd_addr,
					      storm_id);

		/* Dump Conn ST context size */
		offset +=
			qed_grc_dump_ctx_data(p_hwfn,
					      p_ptt,
					      dump_buf + offset,
					      dump,
					      "CONN_ST_CTX",
					      qed_grc_get_param(p_hwfn,
						    DBG_GRC_PARAM_NUM_LCIDS),
					      s_storm_defs[storm_id].
						    cm_conn_st_ctx_lid_size,
					      s_storm_defs[storm_id].
						    cm_conn_st_ctx_rd_addr,
					      storm_id);

		/* Dump Task AG context size */
		offset +=
			qed_grc_dump_ctx_data(p_hwfn,
					      p_ptt,
					      dump_buf + offset,
					      dump,
					      "TASK_AG_CTX",
					      qed_grc_get_param(p_hwfn,
						    DBG_GRC_PARAM_NUM_LTIDS),
					      s_storm_defs[storm_id].
						    cm_task_ag_ctx_lid_size,
					      s_storm_defs[storm_id].
						    cm_task_ag_ctx_rd_addr,
					      storm_id);

		/* Dump Task ST context size */
		offset +=
			qed_grc_dump_ctx_data(p_hwfn,
					      p_ptt,
					      dump_buf + offset,
					      dump,
					      "TASK_ST_CTX",
					      qed_grc_get_param(p_hwfn,
						    DBG_GRC_PARAM_NUM_LTIDS),
					      s_storm_defs[storm_id].
						    cm_task_st_ctx_lid_size,
					      s_storm_defs[storm_id].
						    cm_task_st_ctx_rd_addr,
					      storm_id);
	}

	return offset;
}

/* Dumps GRC IORs data. Returns the dumped size in dwords. */
static u32 qed_grc_dump_iors(struct qed_hwfn *p_hwfn,
			     struct qed_ptt *p_ptt, u32 *dump_buf, bool dump)
{
	char buf[10] = "IOR_SET_?";
	u8 storm_id, set_id;
	u32 offset = 0;

	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		if (qed_grc_is_storm_included(p_hwfn,
					      (enum dbg_storms)storm_id)) {
			for (set_id = 0; set_id < NUM_IOR_SETS; set_id++) {
				u32 addr =
				    s_storm_defs[storm_id].sem_fast_mem_addr +
				    SEM_FAST_REG_STORM_REG_FILE +
				    DWORDS_TO_BYTES(IOR_SET_OFFSET(set_id));

				buf[strlen(buf) - 1] = '0' + set_id;
				offset += qed_grc_dump_mem(p_hwfn,
							   p_ptt,
							   dump_buf + offset,
							   dump,
							   buf,
							   addr,
							   IORS_PER_SET,
							   32,
							   false,
							   "ior",
							   true,
							   s_storm_defs
							   [storm_id].letter);
			}
		}
	}

	return offset;
}

/* Dump VFC CAM. Returns the dumped size in dwords. */
static u32 qed_grc_dump_vfc_cam(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt,
				u32 *dump_buf, bool dump, u8 storm_id)
{
	u32 total_size = VFC_CAM_NUM_ROWS * VFC_CAM_RESP_DWORDS;
	u32 cam_addr[VFC_CAM_ADDR_DWORDS] = { 0 };
	u32 cam_cmd[VFC_CAM_CMD_DWORDS] = { 0 };
	u32 offset = 0;
	u32 row, i;

	offset += qed_grc_dump_mem_hdr(p_hwfn,
				       dump_buf + offset,
				       dump,
				       "vfc_cam",
				       0,
				       total_size,
				       256,
				       false,
				       "vfc_cam",
				       true, s_storm_defs[storm_id].letter);
	if (dump) {
		/* Prepare CAM address */
		SET_VAR_FIELD(cam_addr, VFC_CAM_ADDR, OP, VFC_OPCODE_CAM_RD);
		for (row = 0; row < VFC_CAM_NUM_ROWS;
		     row++, offset += VFC_CAM_RESP_DWORDS) {
			/* Write VFC CAM command */
			SET_VAR_FIELD(cam_cmd, VFC_CAM_CMD, ROW, row);
			ARR_REG_WR(p_hwfn,
				   p_ptt,
				   s_storm_defs[storm_id].sem_fast_mem_addr +
				   SEM_FAST_REG_VFC_DATA_WR,
				   cam_cmd, VFC_CAM_CMD_DWORDS);

			/* Write VFC CAM address */
			ARR_REG_WR(p_hwfn,
				   p_ptt,
				   s_storm_defs[storm_id].sem_fast_mem_addr +
				   SEM_FAST_REG_VFC_ADDR,
				   cam_addr, VFC_CAM_ADDR_DWORDS);

			/* Read VFC CAM read response */
			ARR_REG_RD(p_hwfn,
				   p_ptt,
				   s_storm_defs[storm_id].sem_fast_mem_addr +
				   SEM_FAST_REG_VFC_DATA_RD,
				   dump_buf + offset, VFC_CAM_RESP_DWORDS);
		}
	} else {
		offset += total_size;
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
	u32 ram_addr[VFC_RAM_ADDR_DWORDS] = { 0 };
	u32 ram_cmd[VFC_RAM_CMD_DWORDS] = { 0 };
	u32 offset = 0;
	u32 row, i;

	offset += qed_grc_dump_mem_hdr(p_hwfn,
				       dump_buf + offset,
				       dump,
				       ram_defs->mem_name,
				       0,
				       total_size,
				       256,
				       false,
				       ram_defs->type_name,
				       true, s_storm_defs[storm_id].letter);

	/* Prepare RAM address */
	SET_VAR_FIELD(ram_addr, VFC_RAM_ADDR, OP, VFC_OPCODE_RAM_RD);

	if (!dump)
		return offset + total_size;

	for (row = ram_defs->base_row;
	     row < ram_defs->base_row + ram_defs->num_rows;
	     row++, offset += VFC_RAM_RESP_DWORDS) {
		/* Write VFC RAM command */
		ARR_REG_WR(p_hwfn,
			   p_ptt,
			   s_storm_defs[storm_id].sem_fast_mem_addr +
			   SEM_FAST_REG_VFC_DATA_WR,
			   ram_cmd, VFC_RAM_CMD_DWORDS);

		/* Write VFC RAM address */
		SET_VAR_FIELD(ram_addr, VFC_RAM_ADDR, ROW, row);
		ARR_REG_WR(p_hwfn,
			   p_ptt,
			   s_storm_defs[storm_id].sem_fast_mem_addr +
			   SEM_FAST_REG_VFC_ADDR,
			   ram_addr, VFC_RAM_ADDR_DWORDS);

		/* Read VFC RAM read response */
		ARR_REG_RD(p_hwfn,
			   p_ptt,
			   s_storm_defs[storm_id].sem_fast_mem_addr +
			   SEM_FAST_REG_VFC_DATA_RD,
			   dump_buf + offset, VFC_RAM_RESP_DWORDS);
	}

	return offset;
}

/* Dumps GRC VFC data. Returns the dumped size in dwords. */
static u32 qed_grc_dump_vfc(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt, u32 *dump_buf, bool dump)
{
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u8 storm_id, i;
	u32 offset = 0;

	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		if (qed_grc_is_storm_included(p_hwfn,
					      (enum dbg_storms)storm_id) &&
		    s_storm_defs[storm_id].has_vfc &&
		    (storm_id != DBG_PSTORM_ID ||
		     dev_data->platform_id == PLATFORM_ASIC)) {
			/* Read CAM */
			offset += qed_grc_dump_vfc_cam(p_hwfn,
						       p_ptt,
						       dump_buf + offset,
						       dump, storm_id);

			/* Read RAM */
			for (i = 0; i < NUM_VFC_RAM_TYPES; i++)
				offset += qed_grc_dump_vfc_ram(p_hwfn,
							       p_ptt,
							       dump_buf +
							       offset,
							       dump,
							       storm_id,
							       &s_vfc_ram_defs
							       [i]);
		}
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
		struct rss_mem_defs *rss_defs = &s_rss_mem_defs[rss_mem_id];
		u32 num_entries = rss_defs->num_entries[dev_data->chip_id];
		u32 entry_width = rss_defs->entry_width[dev_data->chip_id];
		u32 total_size = (num_entries * entry_width) / 32;
		bool packed = (entry_width == 16);
		u32 addr = rss_defs->addr;
		u32 i, j;

		offset += qed_grc_dump_mem_hdr(p_hwfn,
					       dump_buf + offset,
					       dump,
					       rss_defs->mem_name,
					       addr,
					       total_size,
					       entry_width,
					       packed,
					       rss_defs->type_name, false, 0);

		if (!dump) {
			offset += total_size;
			continue;
		}

		/* Dump RSS data */
		for (i = 0; i < BYTES_TO_DWORDS(total_size); i++, addr++) {
			qed_wr(p_hwfn, p_ptt, RSS_REG_RSS_RAM_ADDR, addr);
			for (j = 0; j < BYTES_IN_DWORD; j++, offset++)
				*(dump_buf + offset) =
					qed_rd(p_hwfn, p_ptt,
					       RSS_REG_RSS_RAM_DATA +
					       DWORDS_TO_BYTES(j));
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
	char mem_name[12] = "???_BIG_RAM";
	char type_name[8] = "???_RAM";
	u32 ram_size, total_blocks;
	u32 offset = 0, i, j;

	total_blocks =
		s_big_ram_defs[big_ram_id].num_of_blocks[dev_data->chip_id];
	ram_size = total_blocks * BIG_RAM_BLOCK_SIZE_DWORDS;

	strncpy(type_name, s_big_ram_defs[big_ram_id].instance_name,
		strlen(s_big_ram_defs[big_ram_id].instance_name));
	strncpy(mem_name, s_big_ram_defs[big_ram_id].instance_name,
		strlen(s_big_ram_defs[big_ram_id].instance_name));

	/* Dump memory header */
	offset += qed_grc_dump_mem_hdr(p_hwfn,
				       dump_buf + offset,
				       dump,
				       mem_name,
				       0,
				       ram_size,
				       BIG_RAM_BLOCK_SIZE_BYTES * 8,
				       false, type_name, false, 0);

	if (!dump)
		return offset + ram_size;

	/* Read and dump Big RAM data */
	for (i = 0; i < total_blocks / 2; i++) {
		qed_wr(p_hwfn, p_ptt, s_big_ram_defs[big_ram_id].addr_reg_addr,
		       i);
		for (j = 0; j < 2 * BIG_RAM_BLOCK_SIZE_DWORDS; j++, offset++)
			*(dump_buf + offset) = qed_rd(p_hwfn, p_ptt,
						s_big_ram_defs[big_ram_id].
							data_reg_addr +
						DWORDS_TO_BYTES(j));
	}

	return offset;
}

static u32 qed_grc_dump_mcp(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt, u32 *dump_buf, bool dump)
{
	bool block_enable[MAX_BLOCK_ID] = { 0 };
	bool halted = false;
	u32 offset = 0;

	/* Halt MCP */
	if (dump) {
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
				   MCP_REG_SCRATCH,
				   MCP_REG_SCRATCH_SIZE,
				   0, false, "MCP", false, 0);

	/* Dump MCP cpu_reg_file */
	offset += qed_grc_dump_mem(p_hwfn,
				   p_ptt,
				   dump_buf + offset,
				   dump,
				   NULL,
				   MCP_REG_CPU_REG_FILE,
				   MCP_REG_CPU_REG_FILE_SIZE,
				   0, false, "MCP", false, 0);

	/* Dump MCP registers */
	block_enable[BLOCK_MCP] = true;
	offset += qed_grc_dump_registers(p_hwfn,
					 p_ptt,
					 dump_buf + offset,
					 dump, block_enable, "block", "MCP");

	/* Dump required non-MCP registers */
	offset += qed_grc_dump_regs_hdr(dump_buf + offset,
					dump, 1, "eng", -1, "block", "MCP");
	offset += qed_grc_dump_reg_entry(p_hwfn,
					 p_ptt,
					 dump_buf + offset,
					 dump,
					 BYTES_TO_DWORDS
					 (MISC_REG_SHARED_MEM_ADDR), 1);

	/* Release MCP */
	if (halted && qed_mcp_resume(p_hwfn, p_ptt))
		DP_NOTICE(p_hwfn, "Failed to resume MCP after halt!\n");
	return offset;
}

/* Dumps the tbus indirect memory for all PHYs. */
static u32 qed_grc_dump_phy(struct qed_hwfn *p_hwfn,
			    struct qed_ptt *p_ptt, u32 *dump_buf, bool dump)
{
	u32 offset = 0, tbus_lo_offset, tbus_hi_offset;
	char mem_name[32];
	u8 phy_id;

	for (phy_id = 0; phy_id < ARRAY_SIZE(s_phy_defs); phy_id++) {
		struct phy_defs *phy_defs = &s_phy_defs[phy_id];
		int printed_chars;

		printed_chars = snprintf(mem_name, sizeof(mem_name), "tbus_%s",
					 phy_defs->phy_name);
		if (printed_chars < 0 || printed_chars >= sizeof(mem_name))
			DP_NOTICE(p_hwfn,
				  "Unexpected debug error: invalid PHY memory name\n");
		offset += qed_grc_dump_mem_hdr(p_hwfn,
					       dump_buf + offset,
					       dump,
					       mem_name,
					       0,
					       PHY_DUMP_SIZE_DWORDS,
					       16, true, mem_name, false, 0);
		if (dump) {
			u32 addr_lo_addr = phy_defs->base_addr +
					   phy_defs->tbus_addr_lo_addr;
			u32 addr_hi_addr = phy_defs->base_addr +
					   phy_defs->tbus_addr_hi_addr;
			u32 data_lo_addr = phy_defs->base_addr +
					   phy_defs->tbus_data_lo_addr;
			u32 data_hi_addr = phy_defs->base_addr +
					   phy_defs->tbus_data_hi_addr;
			u8 *bytes_buf = (u8 *)(dump_buf + offset);

			for (tbus_hi_offset = 0;
			     tbus_hi_offset < (NUM_PHY_TBUS_ADDRESSES >> 8);
			     tbus_hi_offset++) {
				qed_wr(p_hwfn,
				       p_ptt, addr_hi_addr, tbus_hi_offset);
				for (tbus_lo_offset = 0; tbus_lo_offset < 256;
				     tbus_lo_offset++) {
					qed_wr(p_hwfn,
					       p_ptt,
					       addr_lo_addr, tbus_lo_offset);
					*(bytes_buf++) =
						(u8)qed_rd(p_hwfn, p_ptt,
							   data_lo_addr);
					*(bytes_buf++) =
						(u8)qed_rd(p_hwfn, p_ptt,
							   data_hi_addr);
				}
			}
		}

		offset += PHY_DUMP_SIZE_DWORDS;
	}

	return offset;
}

static void qed_config_dbg_line(struct qed_hwfn *p_hwfn,
				struct qed_ptt *p_ptt,
				enum block_id block_id,
				u8 line_id,
				u8 cycle_en,
				u8 right_shift, u8 force_valid, u8 force_frame)
{
	struct block_defs *p_block_defs = s_block_defs[block_id];

	qed_wr(p_hwfn, p_ptt, p_block_defs->dbg_select_addr, line_id);
	qed_wr(p_hwfn, p_ptt, p_block_defs->dbg_cycle_enable_addr, cycle_en);
	qed_wr(p_hwfn, p_ptt, p_block_defs->dbg_shift_addr, right_shift);
	qed_wr(p_hwfn, p_ptt, p_block_defs->dbg_force_valid_addr, force_valid);
	qed_wr(p_hwfn, p_ptt, p_block_defs->dbg_force_frame_addr, force_frame);
}

/* Dumps Static Debug data. Returns the dumped size in dwords. */
static u32 qed_grc_dump_static_debug(struct qed_hwfn *p_hwfn,
				     struct qed_ptt *p_ptt,
				     u32 *dump_buf, bool dump)
{
	u32 block_dwords = NUM_DBG_BUS_LINES * STATIC_DEBUG_LINE_DWORDS;
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	u32 offset = 0, block_id, line_id, addr, i;
	struct block_defs *p_block_defs;

	if (dump) {
		DP_VERBOSE(p_hwfn,
			   QED_MSG_DEBUG, "Dumping static debug data...\n");

		/* Disable all blocks debug output */
		for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
			p_block_defs = s_block_defs[block_id];

			if (p_block_defs->has_dbg_bus[dev_data->chip_id])
				qed_wr(p_hwfn, p_ptt,
				       p_block_defs->dbg_cycle_enable_addr, 0);
		}

		qed_bus_reset_dbg_block(p_hwfn, p_ptt);
		qed_bus_set_framing_mode(p_hwfn,
					 p_ptt, DBG_BUS_FRAME_MODE_8HW_0ST);
		qed_wr(p_hwfn,
		       p_ptt, DBG_REG_DEBUG_TARGET, DBG_BUS_TARGET_ID_INT_BUF);
		qed_wr(p_hwfn, p_ptt, DBG_REG_FULL_MODE, 1);
		qed_bus_enable_dbg_block(p_hwfn, p_ptt, true);
	}

	/* Dump all static debug lines for each relevant block */
	for (block_id = 0; block_id < MAX_BLOCK_ID; block_id++) {
		p_block_defs = s_block_defs[block_id];

		if (!p_block_defs->has_dbg_bus[dev_data->chip_id])
			continue;

		/* Dump static section params */
		offset += qed_grc_dump_mem_hdr(p_hwfn,
					       dump_buf + offset,
					       dump,
					       p_block_defs->name, 0,
					       block_dwords, 32, false,
					       "STATIC", false, 0);

		if (dump && !dev_data->block_in_reset[block_id]) {
			u8 dbg_client_id =
				p_block_defs->dbg_client_id[dev_data->chip_id];

			/* Enable block's client */
			qed_bus_enable_clients(p_hwfn, p_ptt,
					       BIT(dbg_client_id));

			for (line_id = 0; line_id < NUM_DBG_BUS_LINES;
			     line_id++) {
				/* Configure debug line ID */
				qed_config_dbg_line(p_hwfn,
						    p_ptt,
						    (enum block_id)block_id,
						    (u8)line_id,
						    0xf, 0, 0, 0);

				/* Read debug line info */
				for (i = 0, addr = DBG_REG_CALENDAR_OUT_DATA;
				     i < STATIC_DEBUG_LINE_DWORDS;
				     i++, offset++, addr += BYTES_IN_DWORD)
					dump_buf[offset] = qed_rd(p_hwfn, p_ptt,
								  addr);
			}

			/* Disable block's client and debug output */
			qed_bus_enable_clients(p_hwfn, p_ptt, 0);
			qed_wr(p_hwfn, p_ptt,
			       p_block_defs->dbg_cycle_enable_addr, 0);
		} else {
			/* All lines are invalid - dump zeros */
			if (dump)
				memset(dump_buf + offset, 0,
				       DWORDS_TO_BYTES(block_dwords));
			offset += block_dwords;
		}
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
	u8 i, port_mode = 0;
	u32 offset = 0;

	/* Check if emulation platform */
	*num_dumped_dwords = 0;

	/* Fill GRC parameters that were not set by the user with their default
	 * value.
	 */
	qed_dbg_grc_set_params_default(p_hwfn);

	/* Find port mode */
	if (dump) {
		switch (qed_rd(p_hwfn, p_ptt, MISC_REG_PORT_MODE)) {
		case 0:
			port_mode = 1;
			break;
		case 1:
			port_mode = 2;
			break;
		case 2:
			port_mode = 4;
			break;
		}
	}

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
				     qed_grc_get_param(p_hwfn,
						DBG_GRC_PARAM_NUM_LCIDS));
	offset += qed_dump_num_param(dump_buf + offset,
				     dump,
				     "num-ltids",
				     qed_grc_get_param(p_hwfn,
						DBG_GRC_PARAM_NUM_LTIDS));
	offset += qed_dump_num_param(dump_buf + offset,
				     dump, "num-ports", port_mode);

	/* Dump reset registers (dumped before taking blocks out of reset ) */
	if (qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_REGS))
		offset += qed_grc_dump_reset_regs(p_hwfn,
						  p_ptt,
						  dump_buf + offset, dump);

	/* Take all blocks out of reset (using reset registers) */
	if (dump) {
		qed_grc_unreset_blocks(p_hwfn, p_ptt);
		qed_update_blocks_reset_state(p_hwfn, p_ptt);
	}

	/* Disable all parities using MFW command */
	if (dump) {
		parities_masked = !qed_mcp_mask_parities(p_hwfn, p_ptt, 1);
		if (!parities_masked) {
			if (qed_grc_get_param
			    (p_hwfn, DBG_GRC_PARAM_PARITY_SAFE))
				return DBG_STATUS_MCP_COULD_NOT_MASK_PRTY;
			else
				DP_NOTICE(p_hwfn,
					  "Failed to mask parities using MFW\n");
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
		/* Dump all blocks except MCP */
		bool block_enable[MAX_BLOCK_ID];

		for (i = 0; i < MAX_BLOCK_ID; i++)
			block_enable[i] = true;
		block_enable[BLOCK_MCP] = false;
		offset += qed_grc_dump_registers(p_hwfn,
						 p_ptt,
						 dump_buf +
						 offset,
						 dump,
						 block_enable, NULL, NULL);
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

	/* Dump IORs */
	if (qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_IOR))
		offset += qed_grc_dump_iors(p_hwfn,
					    p_ptt, dump_buf + offset, dump);

	/* Dump VFC */
	if (qed_grc_is_included(p_hwfn, DBG_GRC_PARAM_DUMP_VFC))
		offset += qed_grc_dump_vfc(p_hwfn,
					   p_ptt, dump_buf + offset, dump);

	/* Dump PHY tbus */
	if (qed_grc_is_included(p_hwfn,
				DBG_GRC_PARAM_DUMP_PHY) && dev_data->chip_id ==
	    CHIP_K2 && dev_data->platform_id == PLATFORM_ASIC)
		offset += qed_grc_dump_phy(p_hwfn,
					   p_ptt, dump_buf + offset, dump);

	/* Dump static debug data  */
	if (qed_grc_is_included(p_hwfn,
				DBG_GRC_PARAM_DUMP_STATIC) &&
	    dev_data->bus.state == DBG_BUS_STATE_IDLE)
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
				     u32 *
				     dump_buf,
				     bool dump,
				     u16 rule_id,
				     const struct dbg_idle_chk_rule *rule,
				     u16 fail_entry_id, u32 *cond_reg_values)
{
	const union dbg_idle_chk_reg *regs = &((const union dbg_idle_chk_reg *)
					       s_dbg_arrays
					       [BIN_BUF_DBG_IDLE_CHK_REGS].
					       ptr)[rule->reg_offset];
	const struct dbg_idle_chk_cond_reg *cond_regs = &regs[0].cond_reg;
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;
	struct dbg_idle_chk_result_hdr *hdr =
		(struct dbg_idle_chk_result_hdr *)dump_buf;
	const struct dbg_idle_chk_info_reg *info_regs =
		&regs[rule->num_cond_regs].info_reg;
	u32 next_reg_offset = 0, i, offset = 0;
	u8 reg_id;

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

		/* Write register header */
		if (dump) {
			struct dbg_idle_chk_result_reg_hdr *reg_hdr =
			    (struct dbg_idle_chk_result_reg_hdr *)(dump_buf
								   + offset);
			offset += IDLE_CHK_RESULT_REG_HDR_DWORDS;
			memset(reg_hdr, 0,
			       sizeof(struct dbg_idle_chk_result_reg_hdr));
			reg_hdr->start_entry = reg->start_entry;
			reg_hdr->size = reg->entry_size;
			SET_FIELD(reg_hdr->data,
				  DBG_IDLE_CHK_RESULT_REG_HDR_IS_MEM,
				  reg->num_entries > 1 || reg->start_entry > 0
				  ? 1 : 0);
			SET_FIELD(reg_hdr->data,
				  DBG_IDLE_CHK_RESULT_REG_HDR_REG_ID, reg_id);

			/* Write register values */
			for (i = 0; i < reg_hdr->size;
			     i++, next_reg_offset++, offset++)
				dump_buf[offset] =
				    cond_reg_values[next_reg_offset];
		} else {
			offset += IDLE_CHK_RESULT_REG_HDR_DWORDS +
			    reg->entry_size;
		}
	}

	/* Dump info register values */
	for (reg_id = 0; reg_id < rule->num_info_regs; reg_id++) {
		const struct dbg_idle_chk_info_reg *reg = &info_regs[reg_id];
		u32 block_id;

		if (!dump) {
			offset += IDLE_CHK_RESULT_REG_HDR_DWORDS + reg->size;
			continue;
		}

		/* Check if register's block is in reset */
		block_id = GET_FIELD(reg->data, DBG_IDLE_CHK_INFO_REG_BLOCK_ID);
		if (block_id >= MAX_BLOCK_ID) {
			DP_NOTICE(p_hwfn, "Invalid block_id\n");
			return 0;
		}

		if (!dev_data->block_in_reset[block_id]) {
			bool eval_mode = GET_FIELD(reg->mode.data,
						   DBG_MODE_HDR_EVAL_MODE) > 0;
			bool mode_match = true;

			/* Check mode */
			if (eval_mode) {
				u16 modes_buf_offset =
					GET_FIELD(reg->mode.data,
						DBG_MODE_HDR_MODES_BUF_OFFSET);
				mode_match =
					qed_is_mode_match(p_hwfn,
							  &modes_buf_offset);
			}

			if (mode_match) {
				u32 grc_addr =
					DWORDS_TO_BYTES(GET_FIELD(reg->data,
						DBG_IDLE_CHK_INFO_REG_ADDRESS));

				/* Write register header */
				struct dbg_idle_chk_result_reg_hdr *reg_hdr =
					(struct dbg_idle_chk_result_reg_hdr *)
					(dump_buf + offset);

				offset += IDLE_CHK_RESULT_REG_HDR_DWORDS;
				hdr->num_dumped_info_regs++;
				memset(reg_hdr, 0, sizeof(*reg_hdr));
				reg_hdr->size = reg->size;
				SET_FIELD(reg_hdr->data,
					DBG_IDLE_CHK_RESULT_REG_HDR_REG_ID,
					rule->num_cond_regs + reg_id);

				/* Write register values */
				for (i = 0; i < reg->size;
				     i++, offset++, grc_addr += 4)
					dump_buf[offset] =
						qed_rd(p_hwfn, p_ptt, grc_addr);
				}
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
	u32 i, j, offset = 0;
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
		regs = &((const union dbg_idle_chk_reg *)
			 s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_REGS].ptr)
			[rule->reg_offset];
		cond_regs = &regs[0].cond_reg;
		imm_values = &s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_IMMS].ptr
			     [rule->imm_offset];

		/* Check if all condition register blocks are out of reset, and
		 * find maximal number of entries (all condition registers that
		 * are memories must have the same size, which is > 1).
		 */
		for (reg_id = 0; reg_id < rule->num_cond_regs && check_rule;
		     reg_id++) {
			u32 block_id = GET_FIELD(cond_regs[reg_id].data,
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

		/* Go over all register entries (number of entries is the same
		 * for all condition registers).
		 */
		for (entry_id = 0; entry_id < num_reg_entries; entry_id++) {
			/* Read current entry of all condition registers */
			if (dump) {
				u32 next_reg_offset = 0;

				for (reg_id = 0;
				     reg_id < rule->num_cond_regs;
				     reg_id++) {
					const struct dbg_idle_chk_cond_reg
						*reg = &cond_regs[reg_id];

					/* Find GRC address (if it's a memory,
					 * the address of the specific entry is
					 * calculated).
					 */
					u32 grc_addr =
					   DWORDS_TO_BYTES(
						GET_FIELD(reg->data,
						    DBG_IDLE_CHK_COND_REG_ADDRESS));

					if (reg->num_entries > 1 ||
					    reg->start_entry > 0) {
						u32 padded_entry_size =
							reg->entry_size > 1 ?
							roundup_pow_of_two
							(reg->entry_size) : 1;

						grc_addr +=
							DWORDS_TO_BYTES(
								(reg->start_entry +
								entry_id)
								* padded_entry_size);
					}

					/* Read registers */
					if (next_reg_offset + reg->entry_size >=
					    IDLE_CHK_MAX_ENTRIES_SIZE) {
						DP_NOTICE(p_hwfn,
							  "idle check registers entry is too large\n");
						return 0;
					}

					for (j = 0; j < reg->entry_size;
					     j++, next_reg_offset++,
					     grc_addr += 4)
					     cond_reg_values[next_reg_offset] =
						qed_rd(p_hwfn, p_ptt, grc_addr);
				}
			}

			/* Call rule's condition function - a return value of
			 * true indicates failure.
			 */
			if ((*cond_arr[rule->cond_id])(cond_reg_values,
						       imm_values) || !dump) {
				offset +=
					qed_idle_chk_dump_failure(p_hwfn,
							p_ptt,
							dump_buf + offset,
							dump,
							rule->rule_id,
							rule,
							entry_id,
							cond_reg_values);
				(*num_failing_rules)++;
				break;
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
	u32 offset = 0, input_offset = 0, num_failing_rules = 0;
	u32 num_failing_rules_offset;

	/* Dump global params */
	offset += qed_dump_common_global_params(p_hwfn,
						p_ptt,
						dump_buf + offset, dump, 1);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "dump-type", "idle-chk");

	/* Dump idle check section header with a single parameter */
	offset += qed_dump_section_hdr(dump_buf + offset, dump, "idle_chk", 1);
	num_failing_rules_offset = offset;
	offset += qed_dump_num_param(dump_buf + offset, dump, "num_rules", 0);
	while (input_offset <
	       s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_RULES].size_in_dwords) {
		const struct dbg_idle_chk_cond_hdr *cond_hdr =
			(const struct dbg_idle_chk_cond_hdr *)
			&s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_RULES].ptr
			[input_offset++];
		bool eval_mode = GET_FIELD(cond_hdr->mode.data,
					   DBG_MODE_HDR_EVAL_MODE) > 0;
		bool mode_match = true;

		/* Check mode */
		if (eval_mode) {
			u16 modes_buf_offset =
				GET_FIELD(cond_hdr->mode.data,
					  DBG_MODE_HDR_MODES_BUF_OFFSET);

			mode_match = qed_is_mode_match(p_hwfn,
						       &modes_buf_offset);
		}

		if (mode_match) {
			u32 curr_failing_rules;

			offset +=
			    qed_idle_chk_dump_rule_entries(p_hwfn,
				p_ptt,
				dump_buf + offset,
				dump,
				(const struct dbg_idle_chk_rule *)
				&s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_RULES].
				ptr[input_offset],
				cond_hdr->data_size / IDLE_CHK_RULE_SIZE_DWORDS,
				&curr_failing_rules);
			num_failing_rules += curr_failing_rules;
		}

		input_offset += cond_hdr->data_size;
	}

	/* Overwrite num_rules parameter */
	if (dump)
		qed_dump_num_param(dump_buf + num_failing_rules_offset,
				   dump, "num_rules", num_failing_rules);

	return offset;
}

/* Finds the meta data image in NVRAM. */
static enum dbg_status qed_find_nvram_image(struct qed_hwfn *p_hwfn,
					    struct qed_ptt *p_ptt,
					    u32 image_type,
					    u32 *nvram_offset_bytes,
					    u32 *nvram_size_bytes)
{
	u32 ret_mcp_resp, ret_mcp_param, ret_txn_size;
	struct mcp_file_att file_att;

	/* Call NVRAM get file command */
	if (qed_mcp_nvm_rd_cmd(p_hwfn, p_ptt, DRV_MSG_CODE_NVM_GET_FILE_ATT,
			       image_type, &ret_mcp_resp, &ret_mcp_param,
			       &ret_txn_size, (u32 *)&file_att) != 0)
		return DBG_STATUS_NVRAM_GET_IMAGE_FAILED;

	/* Check response */
	if ((ret_mcp_resp & FW_MSG_CODE_MASK) != FW_MSG_CODE_NVM_OK)
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

static enum dbg_status qed_nvram_read(struct qed_hwfn *p_hwfn,
				      struct qed_ptt *p_ptt,
				      u32 nvram_offset_bytes,
				      u32 nvram_size_bytes, u32 *ret_buf)
{
	u32 ret_mcp_resp, ret_mcp_param, ret_read_size;
	u32 bytes_to_copy, read_offset = 0;
	s32 bytes_left = nvram_size_bytes;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_DEBUG,
		   "nvram_read: reading image of size %d bytes from NVRAM\n",
		   nvram_size_bytes);
	do {
		bytes_to_copy =
		    (bytes_left >
		     MCP_DRV_NVM_BUF_LEN) ? MCP_DRV_NVM_BUF_LEN : bytes_left;

		/* Call NVRAM read command */
		if (qed_mcp_nvm_rd_cmd(p_hwfn, p_ptt,
				       DRV_MSG_CODE_NVM_READ_NVRAM,
				       (nvram_offset_bytes +
					read_offset) |
				       (bytes_to_copy <<
					DRV_MB_PARAM_NVM_LEN_SHIFT),
				       &ret_mcp_resp, &ret_mcp_param,
				       &ret_read_size,
				       (u32 *)((u8 *)ret_buf +
					       read_offset)) != 0)
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

/* Get info on the MCP Trace data in the scratchpad:
 * - trace_data_grc_addr - the GRC address of the trace data
 * - trace_data_size_bytes - the size in bytes of the MCP Trace data (without
 *	the header)
 */
static enum dbg_status qed_mcp_trace_get_data_info(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 *trace_data_grc_addr,
						   u32 *trace_data_size_bytes)
{
	/* Read MCP trace section offsize structure from MCP scratchpad */
	u32 spad_trace_offsize = qed_rd(p_hwfn,
					p_ptt,
					MCP_SPAD_TRACE_OFFSIZE_ADDR);
	u32 signature;

	/* Extract MCP trace section GRC address from offsize structure (within
	 * scratchpad).
	 */
	*trace_data_grc_addr =
		MCP_REG_SCRATCH + SECTION_OFFSET(spad_trace_offsize);

	/* Read signature from MCP trace section */
	signature = qed_rd(p_hwfn, p_ptt,
			   *trace_data_grc_addr +
			   offsetof(struct mcp_trace, signature));
	if (signature != MFW_TRACE_SIGNATURE)
		return DBG_STATUS_INVALID_TRACE_SIGNATURE;

	/* Read trace size from MCP trace section */
	*trace_data_size_bytes = qed_rd(p_hwfn,
					p_ptt,
					*trace_data_grc_addr +
					offsetof(struct mcp_trace, size));
	return DBG_STATUS_OK;
}

/* Reads MCP trace meta data image from NVRAM.
 * - running_bundle_id (OUT) - the running bundle ID (invalid when loaded from
 *	file)
 * - trace_meta_offset_bytes (OUT) - the NVRAM offset in bytes in which the MCP
 *	Trace meta data starts (invalid when loaded from file)
 * - trace_meta_size_bytes (OUT) - the size in bytes of the MCP Trace meta data
 */
static enum dbg_status qed_mcp_trace_get_meta_info(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 trace_data_size_bytes,
						   u32 *running_bundle_id,
						   u32 *trace_meta_offset_bytes,
						   u32 *trace_meta_size_bytes)
{
	/* Read MCP trace section offsize structure from MCP scratchpad */
	u32 spad_trace_offsize = qed_rd(p_hwfn,
					p_ptt,
					MCP_SPAD_TRACE_OFFSIZE_ADDR);

	/* Find running bundle ID */
	u32 running_mfw_addr =
		MCP_REG_SCRATCH + SECTION_OFFSET(spad_trace_offsize) +
		QED_SECTION_SIZE(spad_trace_offsize) + trace_data_size_bytes;
	enum dbg_status status;
	u32 nvram_image_type;

	*running_bundle_id = qed_rd(p_hwfn, p_ptt, running_mfw_addr);
	if (*running_bundle_id > 1)
		return DBG_STATUS_INVALID_NVRAM_BUNDLE;

	/* Find image in NVRAM */
	nvram_image_type =
	    (*running_bundle_id ==
	     DIR_ID_1) ? NVM_TYPE_MFW_TRACE1 : NVM_TYPE_MFW_TRACE2;
	status = qed_find_nvram_image(p_hwfn,
				      p_ptt,
				      nvram_image_type,
				      trace_meta_offset_bytes,
				      trace_meta_size_bytes);

	return status;
}

/* Reads the MCP Trace data from the specified GRC address into the specified
 * buffer.
 */
static void qed_mcp_trace_read_data(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    u32 grc_addr, u32 size_in_dwords, u32 *buf)
{
	u32 i;

	DP_VERBOSE(p_hwfn,
		   QED_MSG_DEBUG,
		   "mcp_trace_read_data: reading trace data of size %d dwords from GRC address 0x%x\n",
		   size_in_dwords, grc_addr);
	for (i = 0; i < size_in_dwords; i++, grc_addr += BYTES_IN_DWORD)
		buf[i] = qed_rd(p_hwfn, p_ptt, grc_addr);
}

/* Reads the MCP Trace meta data (from NVRAM or buffer) into the specified
 * buffer.
 */
static enum dbg_status qed_mcp_trace_read_meta(struct qed_hwfn *p_hwfn,
					       struct qed_ptt *p_ptt,
					       u32 nvram_offset_in_bytes,
					       u32 size_in_bytes, u32 *buf)
{
	u8 *byte_buf = (u8 *)buf;
	u8 modules_num, i;
	u32 signature;

	/* Read meta data from NVRAM */
	enum dbg_status status = qed_nvram_read(p_hwfn,
						p_ptt,
						nvram_offset_in_bytes,
						size_in_bytes,
						buf);

	if (status != DBG_STATUS_OK)
		return status;

	/* Extract and check first signature */
	signature = qed_read_unaligned_dword(byte_buf);
	byte_buf += sizeof(u32);
	if (signature != MCP_TRACE_META_IMAGE_SIGNATURE)
		return DBG_STATUS_INVALID_TRACE_SIGNATURE;

	/* Extract number of modules */
	modules_num = *(byte_buf++);

	/* Skip all modules */
	for (i = 0; i < modules_num; i++) {
		u8 module_len = *(byte_buf++);

		byte_buf += module_len;
	}

	/* Extract and check second signature */
	signature = qed_read_unaligned_dword(byte_buf);
	byte_buf += sizeof(u32);
	if (signature != MCP_TRACE_META_IMAGE_SIGNATURE)
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
	u32 trace_meta_size_dwords, running_bundle_id, offset = 0;
	u32 trace_meta_offset_bytes, trace_meta_size_bytes;
	enum dbg_status status;
	int halted = 0;

	*num_dumped_dwords = 0;

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
	 * consistent if halt fails, MCP trace is taken anyway, with a small
	 * risk that it may be corrupt.
	 */
	if (dump) {
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
	if (dump)
		qed_mcp_trace_read_data(p_hwfn,
					p_ptt,
					trace_data_grc_addr,
					trace_data_size_dwords,
					dump_buf + offset);
	offset += trace_data_size_dwords;

	/* Resume MCP (only if halt succeeded) */
	if (halted && qed_mcp_resume(p_hwfn, p_ptt) != 0)
		DP_NOTICE(p_hwfn, "Failed to resume MCP after halt!\n");

	/* Dump trace meta section header */
	offset += qed_dump_section_hdr(dump_buf + offset,
				       dump, "mcp_trace_meta", 1);

	/* Read trace meta info */
	status = qed_mcp_trace_get_meta_info(p_hwfn,
					     p_ptt,
					     trace_data_size_bytes,
					     &running_bundle_id,
					     &trace_meta_offset_bytes,
					     &trace_meta_size_bytes);
	if (status != DBG_STATUS_OK)
		return status;

	/* Dump trace meta size param (trace_meta_size_bytes is always
	 * dword-aligned).
	 */
	trace_meta_size_dwords = BYTES_TO_DWORDS(trace_meta_size_bytes);
	offset += qed_dump_num_param(dump_buf + offset,	dump, "size",
				     trace_meta_size_dwords);

	/* Read trace meta image into dump buffer */
	if (dump) {
		status = qed_mcp_trace_read_meta(p_hwfn,
						p_ptt,
						trace_meta_offset_bytes,
						trace_meta_size_bytes,
						dump_buf + offset);
		if (status != DBG_STATUS_OK)
			return status;
	}

	offset += trace_meta_size_dwords;

	*num_dumped_dwords = offset;

	return DBG_STATUS_OK;
}

/* Dump GRC FIFO */
static enum dbg_status qed_reg_fifo_dump(struct qed_hwfn *p_hwfn,
					 struct qed_ptt *p_ptt,
					 u32 *dump_buf,
					 bool dump, u32 *num_dumped_dwords)
{
	u32 offset = 0, dwords_read, size_param_offset;
	bool fifo_has_data;

	*num_dumped_dwords = 0;

	/* Dump global params */
	offset += qed_dump_common_global_params(p_hwfn,
						p_ptt,
						dump_buf + offset, dump, 1);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "dump-type", "reg-fifo");

	/* Dump fifo data section header and param. The size param is 0 for now,
	 * and is overwritten after reading the FIFO.
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
		*num_dumped_dwords = offset;
		return DBG_STATUS_OK;
	}

	fifo_has_data = qed_rd(p_hwfn, p_ptt,
			       GRC_REG_TRACE_FIFO_VALID_DATA) > 0;

	/* Pull available data from fifo. Use DMAE since this is widebus memory
	 * and must be accessed atomically. Test for dwords_read not passing
	 * buffer size since more entries could be added to the buffer as we are
	 * emptying it.
	 */
	for (dwords_read = 0;
	     fifo_has_data && dwords_read < REG_FIFO_DEPTH_DWORDS;
	     dwords_read += REG_FIFO_ELEMENT_DWORDS, offset +=
	     REG_FIFO_ELEMENT_DWORDS) {
		if (qed_dmae_grc2host(p_hwfn, p_ptt, GRC_REG_TRACE_FIFO,
				      (u64)(uintptr_t)(&dump_buf[offset]),
				      REG_FIFO_ELEMENT_DWORDS, 0))
			return DBG_STATUS_DMAE_FAILED;
		fifo_has_data = qed_rd(p_hwfn, p_ptt,
				       GRC_REG_TRACE_FIFO_VALID_DATA) > 0;
	}

	qed_dump_num_param(dump_buf + size_param_offset, dump, "size",
			   dwords_read);

	*num_dumped_dwords = offset;
	return DBG_STATUS_OK;
}

/* Dump IGU FIFO */
static enum dbg_status qed_igu_fifo_dump(struct qed_hwfn *p_hwfn,
					 struct qed_ptt *p_ptt,
					 u32 *dump_buf,
					 bool dump, u32 *num_dumped_dwords)
{
	u32 offset = 0, dwords_read, size_param_offset;
	bool fifo_has_data;

	*num_dumped_dwords = 0;

	/* Dump global params */
	offset += qed_dump_common_global_params(p_hwfn,
						p_ptt,
						dump_buf + offset, dump, 1);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "dump-type", "igu-fifo");

	/* Dump fifo data section header and param. The size param is 0 for now,
	 * and is overwritten after reading the FIFO.
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
		*num_dumped_dwords = offset;
		return DBG_STATUS_OK;
	}

	fifo_has_data = qed_rd(p_hwfn, p_ptt,
			       IGU_REG_ERROR_HANDLING_DATA_VALID) > 0;

	/* Pull available data from fifo. Use DMAE since this is widebus memory
	 * and must be accessed atomically. Test for dwords_read not passing
	 * buffer size since more entries could be added to the buffer as we are
	 * emptying it.
	 */
	for (dwords_read = 0;
	     fifo_has_data && dwords_read < IGU_FIFO_DEPTH_DWORDS;
	     dwords_read += IGU_FIFO_ELEMENT_DWORDS, offset +=
	     IGU_FIFO_ELEMENT_DWORDS) {
		if (qed_dmae_grc2host(p_hwfn, p_ptt,
				      IGU_REG_ERROR_HANDLING_MEMORY,
				      (u64)(uintptr_t)(&dump_buf[offset]),
				      IGU_FIFO_ELEMENT_DWORDS, 0))
			return DBG_STATUS_DMAE_FAILED;
		fifo_has_data =	qed_rd(p_hwfn, p_ptt,
				       IGU_REG_ERROR_HANDLING_DATA_VALID) > 0;
	}

	qed_dump_num_param(dump_buf + size_param_offset, dump, "size",
			   dwords_read);

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
	u32 offset = 0, size_param_offset, override_window_dwords;

	*num_dumped_dwords = 0;

	/* Dump global params */
	offset += qed_dump_common_global_params(p_hwfn,
						p_ptt,
						dump_buf + offset, dump, 1);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "dump-type", "protection-override");

	/* Dump data section header and param. The size param is 0 for now, and
	 * is overwritten after reading the data.
	 */
	offset += qed_dump_section_hdr(dump_buf + offset,
				       dump, "protection_override_data", 1);
	size_param_offset = offset;
	offset += qed_dump_num_param(dump_buf + offset, dump, "size", 0);

	if (!dump) {
		offset += PROTECTION_OVERRIDE_DEPTH_DWORDS;
		*num_dumped_dwords = offset;
		return DBG_STATUS_OK;
	}

	/* Add override window info to buffer */
	override_window_dwords =
		qed_rd(p_hwfn, p_ptt,
		       GRC_REG_NUMBER_VALID_OVERRIDE_WINDOW) *
		       PROTECTION_OVERRIDE_ELEMENT_DWORDS;
	if (qed_dmae_grc2host(p_hwfn, p_ptt,
			      GRC_REG_PROTECTION_OVERRIDE_WINDOW,
			      (u64)(uintptr_t)(dump_buf + offset),
			      override_window_dwords, 0))
		return DBG_STATUS_DMAE_FAILED;
	offset += override_window_dwords;
	qed_dump_num_param(dump_buf + size_param_offset, dump, "size",
			   override_window_dwords);

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
	char storm_letter_str[2] = "?";
	struct fw_info fw_info;
	u32 offset = 0, i;
	u8 storm_id;

	/* Dump global params */
	offset += qed_dump_common_global_params(p_hwfn,
						p_ptt,
						dump_buf + offset, dump, 1);
	offset += qed_dump_str_param(dump_buf + offset,
				     dump, "dump-type", "fw-asserts");
	for (storm_id = 0; storm_id < MAX_DBG_STORMS; storm_id++) {
		u32 fw_asserts_section_addr, next_list_idx_addr, next_list_idx,
			last_list_idx, element_addr;

		if (dev_data->block_in_reset[s_storm_defs[storm_id].block_id])
			continue;

		/* Read FW info for the current Storm */
		qed_read_fw_info(p_hwfn, p_ptt, storm_id, &fw_info);

		/* Dump FW Asserts section header and params */
		storm_letter_str[0] = s_storm_defs[storm_id].letter;
		offset += qed_dump_section_hdr(dump_buf + offset, dump,
					       "fw_asserts", 2);
		offset += qed_dump_str_param(dump_buf + offset, dump, "storm",
					     storm_letter_str);
		offset += qed_dump_num_param(dump_buf + offset, dump, "size",
					     fw_info.fw_asserts_section.
					     list_element_dword_size);

		if (!dump) {
			offset += fw_info.fw_asserts_section.
				  list_element_dword_size;
			continue;
		}

		/* Read and dump FW Asserts data */
		fw_asserts_section_addr =
			s_storm_defs[storm_id].sem_fast_mem_addr +
			SEM_FAST_REG_INT_RAM +
			RAM_LINES_TO_BYTES(fw_info.fw_asserts_section.
					   section_ram_line_offset);
		next_list_idx_addr =
			fw_asserts_section_addr +
			DWORDS_TO_BYTES(fw_info.fw_asserts_section.
					list_next_index_dword_offset);
		next_list_idx = qed_rd(p_hwfn, p_ptt, next_list_idx_addr);
		last_list_idx = (next_list_idx > 0
				 ? next_list_idx
				 : fw_info.fw_asserts_section.list_num_elements)
				- 1;
		element_addr =
			fw_asserts_section_addr +
			DWORDS_TO_BYTES(fw_info.fw_asserts_section.
					list_dword_offset) +
			last_list_idx *
			DWORDS_TO_BYTES(fw_info.fw_asserts_section.
					list_element_dword_size);
		for (i = 0;
		     i < fw_info.fw_asserts_section.list_element_dword_size;
		     i++, offset++, element_addr += BYTES_IN_DWORD)
			dump_buf[offset] = qed_rd(p_hwfn, p_ptt, element_addr);
	}

	/* Dump last section */
	offset += qed_dump_section_hdr(dump_buf + offset, dump, "last", 0);
	return offset;
}

/***************************** Public Functions *******************************/

enum dbg_status qed_dbg_set_bin_ptr(const u8 * const bin_ptr)
{
	/* Convert binary data to debug arrays */
	u32 num_of_buffers = *(u32 *)bin_ptr;
	struct bin_buffer_hdr *buf_array;
	u8 buf_id;

	buf_array = (struct bin_buffer_hdr *)((u32 *)bin_ptr + 1);

	for (buf_id = 0; buf_id < num_of_buffers; buf_id++) {
		s_dbg_arrays[buf_id].ptr =
		    (u32 *)(bin_ptr + buf_array[buf_id].offset);
		s_dbg_arrays[buf_id].size_in_dwords =
		    BYTES_TO_DWORDS(buf_array[buf_id].length);
	}

	return DBG_STATUS_OK;
}

enum dbg_status qed_dbg_grc_get_dump_buf_size(struct qed_hwfn *p_hwfn,
					      struct qed_ptt *p_ptt,
					      u32 *buf_size)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn, p_ptt);

	*buf_size = 0;
	if (status != DBG_STATUS_OK)
		return status;
	if (!s_dbg_arrays[BIN_BUF_DBG_MODE_TREE].ptr ||
	    !s_dbg_arrays[BIN_BUF_DBG_DUMP_REG].ptr ||
	    !s_dbg_arrays[BIN_BUF_DBG_DUMP_MEM].ptr ||
	    !s_dbg_arrays[BIN_BUF_DBG_ATTN_BLOCKS].ptr ||
	    !s_dbg_arrays[BIN_BUF_DBG_ATTN_REGS].ptr)
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

	status = qed_dbg_grc_get_dump_buf_size(p_hwfn, p_ptt,
					       &needed_buf_size_in_dwords);

	*num_dumped_dwords = 0;
	if (status != DBG_STATUS_OK)
		return status;
	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* GRC Dump */
	status = qed_grc_dump(p_hwfn, p_ptt, dump_buf, true, num_dumped_dwords);

	/* Clear all GRC params */
	qed_dbg_grc_clear_params(p_hwfn);
	return status;
}

enum dbg_status qed_dbg_idle_chk_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 *buf_size)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn, p_ptt);
	struct dbg_tools_data *dev_data = &p_hwfn->dbg_info;

	*buf_size = 0;
	if (status != DBG_STATUS_OK)
		return status;
	if (!s_dbg_arrays[BIN_BUF_DBG_MODE_TREE].ptr ||
	    !s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_REGS].ptr ||
	    !s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_IMMS].ptr ||
	    !s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_RULES].ptr)
		return DBG_STATUS_DBG_ARRAY_NOT_SET;
	if (!dev_data->idle_chk.buf_size_set) {
		dev_data->idle_chk.buf_size = qed_idle_chk_dump(p_hwfn,
								p_ptt,
								NULL, false);
		dev_data->idle_chk.buf_size_set = true;
	}

	*buf_size = dev_data->idle_chk.buf_size;
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

	status = qed_dbg_idle_chk_get_dump_buf_size(p_hwfn, p_ptt,
						    &needed_buf_size_in_dwords);

	*num_dumped_dwords = 0;
	if (status != DBG_STATUS_OK)
		return status;
	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	qed_update_blocks_reset_state(p_hwfn, p_ptt);

	/* Idle Check Dump */
	*num_dumped_dwords = qed_idle_chk_dump(p_hwfn, p_ptt, dump_buf, true);
	return DBG_STATUS_OK;
}

enum dbg_status qed_dbg_mcp_trace_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						    struct qed_ptt *p_ptt,
						    u32 *buf_size)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn, p_ptt);

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

	status = qed_dbg_mcp_trace_get_dump_buf_size(p_hwfn, p_ptt,
						&needed_buf_size_in_dwords);

	if (status != DBG_STATUS_OK)
		return status;
	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	qed_update_blocks_reset_state(p_hwfn, p_ptt);

	/* Perform dump */
	return qed_mcp_trace_dump(p_hwfn,
				  p_ptt, dump_buf, true, num_dumped_dwords);
}

enum dbg_status qed_dbg_reg_fifo_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 *buf_size)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn, p_ptt);

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

	status = qed_dbg_reg_fifo_get_dump_buf_size(p_hwfn, p_ptt,
						    &needed_buf_size_in_dwords);

	*num_dumped_dwords = 0;
	if (status != DBG_STATUS_OK)
		return status;
	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	qed_update_blocks_reset_state(p_hwfn, p_ptt);
	return qed_reg_fifo_dump(p_hwfn,
				 p_ptt, dump_buf, true, num_dumped_dwords);
}

enum dbg_status qed_dbg_igu_fifo_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						   struct qed_ptt *p_ptt,
						   u32 *buf_size)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn, p_ptt);

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

	status = qed_dbg_igu_fifo_get_dump_buf_size(p_hwfn, p_ptt,
						    &needed_buf_size_in_dwords);

	*num_dumped_dwords = 0;
	if (status != DBG_STATUS_OK)
		return status;
	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	qed_update_blocks_reset_state(p_hwfn, p_ptt);
	return qed_igu_fifo_dump(p_hwfn,
				 p_ptt, dump_buf, true, num_dumped_dwords);
}

enum dbg_status
qed_dbg_protection_override_get_dump_buf_size(struct qed_hwfn *p_hwfn,
					      struct qed_ptt *p_ptt,
					      u32 *buf_size)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn, p_ptt);

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
	u32 needed_buf_size_in_dwords;
	enum dbg_status status;

	status = qed_dbg_protection_override_get_dump_buf_size(p_hwfn, p_ptt,
						&needed_buf_size_in_dwords);

	*num_dumped_dwords = 0;
	if (status != DBG_STATUS_OK)
		return status;
	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	/* Update reset state */
	qed_update_blocks_reset_state(p_hwfn, p_ptt);
	return qed_protection_override_dump(p_hwfn,
					    p_ptt,
					    dump_buf, true, num_dumped_dwords);
}

enum dbg_status qed_dbg_fw_asserts_get_dump_buf_size(struct qed_hwfn *p_hwfn,
						     struct qed_ptt *p_ptt,
						     u32 *buf_size)
{
	enum dbg_status status = qed_dbg_dev_init(p_hwfn, p_ptt);

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
	u32 needed_buf_size_in_dwords;
	enum dbg_status status;

	status = qed_dbg_fw_asserts_get_dump_buf_size(p_hwfn, p_ptt,
						&needed_buf_size_in_dwords);

	*num_dumped_dwords = 0;
	if (status != DBG_STATUS_OK)
		return status;
	if (buf_size_in_dwords < needed_buf_size_in_dwords)
		return DBG_STATUS_DUMP_BUF_TOO_SMALL;

	*num_dumped_dwords = qed_fw_asserts_dump(p_hwfn, p_ptt, dump_buf, true);
	return DBG_STATUS_OK;
}

/******************************* Data Types **********************************/

struct mcp_trace_format {
	u32 data;
#define MCP_TRACE_FORMAT_MODULE_MASK	0x0000ffff
#define MCP_TRACE_FORMAT_MODULE_SHIFT	0
#define MCP_TRACE_FORMAT_LEVEL_MASK	0x00030000
#define MCP_TRACE_FORMAT_LEVEL_SHIFT	16
#define MCP_TRACE_FORMAT_P1_SIZE_MASK	0x000c0000
#define MCP_TRACE_FORMAT_P1_SIZE_SHIFT	18
#define MCP_TRACE_FORMAT_P2_SIZE_MASK	0x00300000
#define MCP_TRACE_FORMAT_P2_SIZE_SHIFT	20
#define MCP_TRACE_FORMAT_P3_SIZE_MASK	0x00c00000
#define MCP_TRACE_FORMAT_P3_SIZE_SHIFT	22
#define MCP_TRACE_FORMAT_LEN_MASK	0xff000000
#define MCP_TRACE_FORMAT_LEN_SHIFT	24
	char *format_str;
};

struct mcp_trace_meta {
	u32 modules_num;
	char **modules;
	u32 formats_num;
	struct mcp_trace_format *formats;
};

/* Reg fifo element */
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
	(MCP_TRACE_FORMAT_P2_SIZE_SHIFT - MCP_TRACE_FORMAT_P1_SIZE_SHIFT)
#define REG_FIFO_ELEMENT_ADDR_FACTOR		4
#define REG_FIFO_ELEMENT_IS_PF_VF_VAL		127
#define PROTECTION_OVERRIDE_ELEMENT_ADDR_FACTOR	4

/********************************* Macros ************************************/

#define BYTES_TO_DWORDS(bytes)			((bytes) / BYTES_IN_DWORD)

/***************************** Constant Arrays *******************************/

/* Status string array */
static const char * const s_status_str[] = {
	"Operation completed successfully",
	"Debug application version wasn't set",
	"Unsupported debug application version",
	"The debug block wasn't reset since the last recording",
	"Invalid arguments",
	"The debug output was already set",
	"Invalid PCI buffer size",
	"PCI buffer allocation failed",
	"A PCI buffer wasn't allocated",
	"Too many inputs were enabled. Enabled less inputs, or set 'unifyInputs' to true",
	"GRC/Timestamp input overlap in cycle dword 0",
	"Cannot record Storm data since the entire recording cycle is used by HW",
	"The Storm was already enabled",
	"The specified Storm wasn't enabled",
	"The block was already enabled",
	"The specified block wasn't enabled",
	"No input was enabled for recording",
	"Filters and triggers are not allowed when recording in 64b units",
	"The filter was already enabled",
	"The trigger was already enabled",
	"The trigger wasn't enabled",
	"A constraint can be added only after a filter was enabled or a trigger state was added",
	"Cannot add more than 3 trigger states",
	"Cannot add more than 4 constraints per filter or trigger state",
	"The recording wasn't started",
	"A trigger was configured, but it didn't trigger",
	"No data was recorded",
	"Dump buffer is too small",
	"Dumped data is not aligned to chunks",
	"Unknown chip",
	"Failed allocating virtual memory",
	"The input block is in reset",
	"Invalid MCP trace signature found in NVRAM",
	"Invalid bundle ID found in NVRAM",
	"Failed getting NVRAM image",
	"NVRAM image is not dword-aligned",
	"Failed reading from NVRAM",
	"Idle check parsing failed",
	"MCP Trace data is corrupt",
	"Dump doesn't contain meta data - it must be provided in an image file",
	"Failed to halt MCP",
	"Failed to resume MCP after halt",
	"DMAE transaction failed",
	"Failed to empty SEMI sync FIFO",
	"IGU FIFO data is corrupt",
	"MCP failed to mask parities",
	"FW Asserts parsing failed",
	"GRC FIFO data is corrupt",
	"Protection Override data is corrupt",
	"Debug arrays were not set (when using binary files, dbg_set_bin_ptr must be called)",
	"When a block is filtered, no other blocks can be recorded unless inputs are unified (due to a HW bug)"
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

/* Parsing strings */
static const char * const s_access_strs[] = {
	"read",
	"write"
};

static const char * const s_privilege_strs[] = {
	"VF",
	"PDA",
	"HV",
	"UA"
};

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
	"???",
	"???",
	"???",
	"???",
	"???"
};

static const char * const s_reg_fifo_error_strs[] = {
	"grc timeout",
	"address doesn't belong to any block",
	"reserved address in block or write to read-only address",
	"privilege/protection mismatch",
	"path isolation error"
};

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

static const char * const s_igu_fifo_error_strs[] = {
	"no error",
	"length error",
	"function disabled",
	"VF sent command to attnetion address",
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
	{0x0, 0x101, "MSI-X Memory", NULL, IGU_ADDR_TYPE_MSIX_MEM},
	{0x102, 0x1ff, "reserved", NULL, IGU_ADDR_TYPE_RESERVED},
	{0x200, 0x200, "Write PBA[0:63]", NULL, IGU_ADDR_TYPE_WRITE_PBA},
	{0x201, 0x201, "Write PBA[64:127]", "reserved",
	 IGU_ADDR_TYPE_WRITE_PBA},
	{0x202, 0x202, "Write PBA[128]", "reserved", IGU_ADDR_TYPE_WRITE_PBA},
	{0x203, 0x3ff, "reserved", NULL, IGU_ADDR_TYPE_RESERVED},
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
	{0x5f7, 0x5ff, "reserved", NULL, IGU_ADDR_TYPE_RESERVED},
	{0x600, 0x7ff, "Producer update", NULL, IGU_ADDR_TYPE_WRITE_PROD_UPDATE}
};

/******************************** Variables **********************************/

/* MCP Trace meta data - used in case the dump doesn't contain the meta data
 * (e.g. due to no NVRAM access).
 */
static struct dbg_array s_mcp_trace_meta = { NULL, 0 };

/* Temporary buffer, used for print size calculations */
static char s_temp_buf[MAX_MSG_LEN];

/***************************** Public Functions *******************************/

enum dbg_status qed_dbg_user_set_bin_ptr(const u8 * const bin_ptr)
{
	/* Convert binary data to debug arrays */
	u32 num_of_buffers = *(u32 *)bin_ptr;
	struct bin_buffer_hdr *buf_array;
	u8 buf_id;

	buf_array = (struct bin_buffer_hdr *)((u32 *)bin_ptr + 1);

	for (buf_id = 0; buf_id < num_of_buffers; buf_id++) {
		s_dbg_arrays[buf_id].ptr =
		    (u32 *)(bin_ptr + buf_array[buf_id].offset);
		s_dbg_arrays[buf_id].size_in_dwords =
		    BYTES_TO_DWORDS(buf_array[buf_id].length);
	}

	return DBG_STATUS_OK;
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
	u8 *bytes_buf = (u8 *)buf;
	u8 *val_ptr;
	u32 val = 0;
	u8 i;

	val_ptr = (u8 *)&val;

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
	u32 offset = 0; /* In bytes */

	/* Extract param name */
	*param_name = char_buf;
	offset += strlen(*param_name) + 1;

	/* Check param type */
	if (*(char_buf + offset++)) {
		/* String param */
		*param_str_val = char_buf + offset;
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

	return offset / 4;
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
		const char *param_name;
		const char *param_str_val;
		u32 param_num_val = 0;

		dump_offset += qed_read_param(dump_buf + dump_offset,
					      &param_name,
					      &param_str_val, &param_num_val);
		if (param_str_val)
			/* String param */
			results_offset +=
				sprintf(qed_get_buf_ptr(results_buf,
							results_offset),
					"%s: %s\n", param_name, param_str_val);
		else if (strcmp(param_name, "fw-timestamp"))
			/* Numeric param */
			results_offset +=
				sprintf(qed_get_buf_ptr(results_buf,
							results_offset),
					"%s: %d\n", param_name, param_num_val);
	}

	results_offset +=
	    sprintf(qed_get_buf_ptr(results_buf, results_offset), "\n");
	*num_chars_printed = results_offset;
	return dump_offset;
}

const char *qed_dbg_get_status_str(enum dbg_status status)
{
	return (status <
		MAX_DBG_STATUS) ? s_status_str[status] : "Invalid debug status";
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
	u32 rule_idx, results_offset = 0; /* Offset in results_buf in bytes */
	u16 i, j;

	*num_errors = 0;
	*num_warnings = 0;

	/* Go over dumped results */
	for (rule_idx = 0; rule_idx < num_rules && dump_buf < dump_buf_end;
	     rule_idx++) {
		const struct dbg_idle_chk_rule_parsing_data *rule_parsing_data;
		struct dbg_idle_chk_result_hdr *hdr;
		const char *parsing_str;
		u32 parsing_str_offset;
		const char *lsi_msg;
		u8 curr_reg_id = 0;
		bool has_fw_msg;

		hdr = (struct dbg_idle_chk_result_hdr *)dump_buf;
		rule_parsing_data =
			(const struct dbg_idle_chk_rule_parsing_data *)
			&s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_PARSING_DATA].
			ptr[hdr->rule_id];
		parsing_str_offset =
			GET_FIELD(rule_parsing_data->data,
				  DBG_IDLE_CHK_RULE_PARSING_DATA_STR_OFFSET);
		has_fw_msg =
			GET_FIELD(rule_parsing_data->data,
				DBG_IDLE_CHK_RULE_PARSING_DATA_HAS_FW_MSG) > 0;
		parsing_str = &((const char *)
				s_dbg_arrays[BIN_BUF_DBG_PARSING_STRINGS].ptr)
				[parsing_str_offset];
		lsi_msg = parsing_str;

		if (hdr->severity >= MAX_DBG_IDLE_CHK_SEVERITY_TYPES)
			return 0;

		/* Skip rule header */
		dump_buf += (sizeof(struct dbg_idle_chk_result_hdr) / 4);

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
			struct dbg_idle_chk_result_reg_hdr *reg_hdr
			    = (struct dbg_idle_chk_result_reg_hdr *)
			    dump_buf;
			bool is_mem =
				GET_FIELD(reg_hdr->data,
					  DBG_IDLE_CHK_RESULT_REG_HDR_IS_MEM);
			u8 reg_id =
				GET_FIELD(reg_hdr->data,
					  DBG_IDLE_CHK_RESULT_REG_HDR_REG_ID);

			/* Skip reg header */
			dump_buf +=
			    (sizeof(struct dbg_idle_chk_result_reg_hdr) / 4);

			/* Skip register names until the required reg_id is
			 * reached.
			 */
			for (; reg_id > curr_reg_id;
			     curr_reg_id++,
			     parsing_str += strlen(parsing_str) + 1);

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
	const char *section_name, *param_name, *param_str_val;
	u32 *dump_buf_end = dump_buf + num_dumped_dwords;
	u32 num_section_params = 0, num_rules;
	u32 results_offset = 0;	/* Offset in results_buf in bytes */

	*parsed_results_bytes = 0;
	*num_errors = 0;
	*num_warnings = 0;
	if (!s_dbg_arrays[BIN_BUF_DBG_PARSING_STRINGS].ptr ||
	    !s_dbg_arrays[BIN_BUF_DBG_IDLE_CHK_PARSING_DATA].ptr)
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

	/* Read idle_chk section */
	dump_buf += qed_read_section_hdr(dump_buf,
					 &section_name, &num_section_params);
	if (strcmp(section_name, "idle_chk") || num_section_params != 1)
		return DBG_STATUS_IDLE_CHK_PARSE_FAILED;

	dump_buf += qed_read_param(dump_buf,
				   &param_name, &param_str_val, &num_rules);
	if (strcmp(param_name, "num_rules") != 0)
		return DBG_STATUS_IDLE_CHK_PARSE_FAILED;

	if (num_rules) {
		u32 rules_print_size;

		/* Print FW output */
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "FW_IDLE_CHECK:\n");
		rules_print_size =
			qed_parse_idle_chk_dump_rules(p_hwfn, dump_buf,
						      dump_buf_end, num_rules,
						      true,
						      results_buf ?
						      results_buf +
						      results_offset : NULL,
						      num_errors, num_warnings);
		results_offset += rules_print_size;
		if (rules_print_size == 0)
			return DBG_STATUS_IDLE_CHK_PARSE_FAILED;

		/* Print LSI output */
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "\nLSI_IDLE_CHECK:\n");
		rules_print_size =
			qed_parse_idle_chk_dump_rules(p_hwfn, dump_buf,
						      dump_buf_end, num_rules,
						      false,
						      results_buf ?
						      results_buf +
						      results_offset : NULL,
						      num_errors, num_warnings);
		results_offset += rules_print_size;
		if (rules_print_size == 0)
			return DBG_STATUS_IDLE_CHK_PARSE_FAILED;
	}

	/* Print errors/warnings count */
	if (*num_errors) {
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "\nIdle Check failed!!! (with %d errors and %d warnings)\n",
			    *num_errors, *num_warnings);
	} else if (*num_warnings) {
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "\nIdle Check completed successfuly (with %d warnings)\n",
			    *num_warnings);
	} else {
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "\nIdle Check completed successfuly\n");
	}

	/* Add 1 for string NULL termination */
	*parsed_results_bytes = results_offset + 1;
	return DBG_STATUS_OK;
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
					   u32 *num_errors, u32 *num_warnings)
{
	u32 parsed_buf_size;

	return qed_parse_idle_chk_dump(p_hwfn,
				       dump_buf,
				       num_dumped_dwords,
				       results_buf,
				       &parsed_buf_size,
				       num_errors, num_warnings);
}

/* Frees the specified MCP Trace meta data */
static void qed_mcp_trace_free_meta(struct qed_hwfn *p_hwfn,
				    struct mcp_trace_meta *meta)
{
	u32 i;

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
}

/* Allocates and fills MCP Trace meta data based on the specified meta data
 * dump buffer.
 * Returns debug status code.
 */
static enum dbg_status qed_mcp_trace_alloc_meta(struct qed_hwfn *p_hwfn,
						const u32 *meta_buf,
						struct mcp_trace_meta *meta)
{
	u8 *meta_buf_bytes = (u8 *)meta_buf;
	u32 offset = 0, signature, i;

	memset(meta, 0, sizeof(*meta));

	/* Read first signature */
	signature = qed_read_dword_from_buf(meta_buf_bytes, &offset);
	if (signature != MCP_TRACE_META_IMAGE_SIGNATURE)
		return DBG_STATUS_INVALID_TRACE_SIGNATURE;

	/* Read number of modules and allocate memory for all the modules
	 * pointers.
	 */
	meta->modules_num = qed_read_byte_from_buf(meta_buf_bytes, &offset);
	meta->modules = kzalloc(meta->modules_num * sizeof(char *), GFP_KERNEL);
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
	if (signature != MCP_TRACE_META_IMAGE_SIGNATURE)
		return DBG_STATUS_INVALID_TRACE_SIGNATURE;

	/* Read number of formats and allocate memory for all formats */
	meta->formats_num = qed_read_dword_from_buf(meta_buf_bytes, &offset);
	meta->formats = kzalloc(meta->formats_num *
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
		format_len =
		    (format_ptr->data &
		     MCP_TRACE_FORMAT_LEN_MASK) >> MCP_TRACE_FORMAT_LEN_SHIFT;
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

	return DBG_STATUS_OK;
}

/* Parses an MCP Trace dump buffer.
 * If result_buf is not NULL, the MCP Trace results are printed to it.
 * In any case, the required results buffer size is assigned to
 * parsed_results_bytes.
 * The parsing status is returned.
 */
static enum dbg_status qed_parse_mcp_trace_dump(struct qed_hwfn *p_hwfn,
						u32 *dump_buf,
						u32 num_dumped_dwords,
						char *results_buf,
						u32 *parsed_results_bytes)
{
	u32 results_offset = 0, param_mask, param_shift, param_num_val;
	u32 num_section_params, offset, end_offset, bytes_left;
	const char *section_name, *param_name, *param_str_val;
	u32 trace_data_dwords, trace_meta_dwords;
	struct mcp_trace_meta meta;
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
	trace_buf = (u8 *)dump_buf + sizeof(struct mcp_trace);
	offset = trace->trace_oldest;
	end_offset = trace->trace_prod;
	bytes_left = qed_cyclic_sub(end_offset, offset, trace->size);
	dump_buf += trace_data_dwords;

	/* Read meta_data section */
	dump_buf += qed_read_section_hdr(dump_buf,
					 &section_name, &num_section_params);
	if (strcmp(section_name, "mcp_trace_meta"))
		return DBG_STATUS_MCP_TRACE_BAD_DATA;
	dump_buf += qed_read_param(dump_buf,
				   &param_name, &param_str_val, &param_num_val);
	if (strcmp(param_name, "size") != 0)
		return DBG_STATUS_MCP_TRACE_BAD_DATA;
	trace_meta_dwords = param_num_val;

	/* Choose meta data buffer */
	if (!trace_meta_dwords) {
		/* Dump doesn't include meta data */
		if (!s_mcp_trace_meta.ptr)
			return DBG_STATUS_MCP_TRACE_NO_META;
		meta_buf = s_mcp_trace_meta.ptr;
	} else {
		/* Dump includes meta data */
		meta_buf = dump_buf;
	}

	/* Allocate meta data memory */
	status = qed_mcp_trace_alloc_meta(p_hwfn, meta_buf, &meta);
	if (status != DBG_STATUS_OK)
		goto free_mem;

	/* Ignore the level and modules masks - just print everything that is
	 * already in the buffer.
	 */
	while (bytes_left) {
		struct mcp_trace_format *format_ptr;
		u8 format_level, format_module;
		u32 params[3] = { 0, 0, 0 };
		u32 header, format_idx, i;

		if (bytes_left < MFW_TRACE_ENTRY_SIZE) {
			status = DBG_STATUS_MCP_TRACE_BAD_DATA;
			goto free_mem;
		}

		header = qed_read_from_cyclic_buf(trace_buf,
						  &offset,
						  trace->size,
						  MFW_TRACE_ENTRY_SIZE);
		bytes_left -= MFW_TRACE_ENTRY_SIZE;
		format_idx = header & MFW_TRACE_EVENTID_MASK;

		/* Skip message if its  index doesn't exist in the meta data */
		if (format_idx > meta.formats_num) {
			u8 format_size =
			    (u8)((header &
				  MFW_TRACE_PRM_SIZE_MASK) >>
				 MFW_TRACE_PRM_SIZE_SHIFT);

			if (bytes_left < format_size) {
				status = DBG_STATUS_MCP_TRACE_BAD_DATA;
				goto free_mem;
			}

			offset = qed_cyclic_add(offset,
						format_size, trace->size);
			bytes_left -= format_size;
			continue;
		}

		format_ptr = &meta.formats[format_idx];
		for (i = 0,
		     param_mask = MCP_TRACE_FORMAT_P1_SIZE_MASK, param_shift =
		     MCP_TRACE_FORMAT_P1_SIZE_SHIFT;
		     i < MCP_TRACE_FORMAT_MAX_PARAMS;
		     i++, param_mask <<= MCP_TRACE_FORMAT_PARAM_WIDTH,
		     param_shift += MCP_TRACE_FORMAT_PARAM_WIDTH) {
			/* Extract param size (0..3) */
			u8 param_size =
			    (u8)((format_ptr->data &
				  param_mask) >> param_shift);

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
			if (bytes_left < param_size) {
				status = DBG_STATUS_MCP_TRACE_BAD_DATA;
				goto free_mem;
			}

			params[i] = qed_read_from_cyclic_buf(trace_buf,
							     &offset,
							     trace->size,
							     param_size);
			bytes_left -= param_size;
		}

		format_level =
		    (u8)((format_ptr->data &
			  MCP_TRACE_FORMAT_LEVEL_MASK) >>
			  MCP_TRACE_FORMAT_LEVEL_SHIFT);
		format_module =
		    (u8)((format_ptr->data &
			  MCP_TRACE_FORMAT_MODULE_MASK) >>
			 MCP_TRACE_FORMAT_MODULE_SHIFT);
		if (format_level >= ARRAY_SIZE(s_mcp_trace_level_str)) {
			status = DBG_STATUS_MCP_TRACE_BAD_DATA;
			goto free_mem;
		}

		/* Print current message to results buffer */
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset), "%s %-8s: ",
			    s_mcp_trace_level_str[format_level],
			    meta.modules[format_module]);
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    format_ptr->format_str, params[0], params[1],
			    params[2]);
	}

free_mem:
	*parsed_results_bytes = results_offset + 1;
	qed_mcp_trace_free_meta(p_hwfn, &meta);
	return status;
}

enum dbg_status qed_get_mcp_trace_results_buf_size(struct qed_hwfn *p_hwfn,
						   u32 *dump_buf,
						   u32 num_dumped_dwords,
						   u32 *results_buf_size)
{
	return qed_parse_mcp_trace_dump(p_hwfn,
					dump_buf,
					num_dumped_dwords,
					NULL, results_buf_size);
}

enum dbg_status qed_print_mcp_trace_results(struct qed_hwfn *p_hwfn,
					    u32 *dump_buf,
					    u32 num_dumped_dwords,
					    char *results_buf)
{
	u32 parsed_buf_size;

	return qed_parse_mcp_trace_dump(p_hwfn,
					dump_buf,
					num_dumped_dwords,
					results_buf, &parsed_buf_size);
}

/* Parses a Reg FIFO dump buffer.
 * If result_buf is not NULL, the Reg FIFO results are printed to it.
 * In any case, the required results buffer size is assigned to
 * parsed_results_bytes.
 * The parsing status is returned.
 */
static enum dbg_status qed_parse_reg_fifo_dump(struct qed_hwfn *p_hwfn,
					       u32 *dump_buf,
					       u32 num_dumped_dwords,
					       char *results_buf,
					       u32 *parsed_results_bytes)
{
	u32 results_offset = 0, param_num_val, num_section_params, num_elements;
	const char *section_name, *param_name, *param_str_val;
	struct reg_fifo_element *elements;
	u8 i, j, err_val, vf_val;
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
		bool err_printed = false;

		/* Discover if element belongs to a VF or a PF */
		vf_val = GET_FIELD(elements[i].data, REG_FIFO_ELEMENT_VF);
		if (vf_val == REG_FIFO_ELEMENT_IS_PF_VF_VAL)
			sprintf(vf_str, "%s", "N/A");
		else
			sprintf(vf_str, "%d", vf_val);

		/* Add parsed element to parsed buffer */
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "raw: 0x%016llx, address: 0x%07llx, access: %-5s, pf: %2lld, vf: %s, port: %lld, privilege: %-3s, protection: %-12s, master: %-4s, errors: ",
			    elements[i].data,
			    GET_FIELD(elements[i].data,
				      REG_FIFO_ELEMENT_ADDRESS) *
				      REG_FIFO_ELEMENT_ADDR_FACTOR,
				      s_access_strs[GET_FIELD(elements[i].data,
						    REG_FIFO_ELEMENT_ACCESS)],
			    GET_FIELD(elements[i].data,
				      REG_FIFO_ELEMENT_PF), vf_str,
			    GET_FIELD(elements[i].data,
				      REG_FIFO_ELEMENT_PORT),
				      s_privilege_strs[GET_FIELD(elements[i].
				      data,
				      REG_FIFO_ELEMENT_PRIVILEGE)],
			    s_protection_strs[GET_FIELD(elements[i].data,
						REG_FIFO_ELEMENT_PROTECTION)],
			    s_master_strs[GET_FIELD(elements[i].data,
						REG_FIFO_ELEMENT_MASTER)]);

		/* Print errors */
		for (j = 0,
		     err_val = GET_FIELD(elements[i].data,
					 REG_FIFO_ELEMENT_ERROR);
		     j < ARRAY_SIZE(s_reg_fifo_error_strs);
		     j++, err_val >>= 1) {
			if (!(err_val & 0x1))
				continue;
			if (err_printed)
				results_offset +=
					sprintf(qed_get_buf_ptr(results_buf,
								results_offset),
						", ");
			results_offset +=
				sprintf(qed_get_buf_ptr(results_buf,
							results_offset), "%s",
					s_reg_fifo_error_strs[j]);
			err_printed = true;
		}

		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf, results_offset), "\n");
	}

	results_offset += sprintf(qed_get_buf_ptr(results_buf,
						  results_offset),
				  "fifo contained %d elements", num_elements);

	/* Add 1 for string NULL termination */
	*parsed_results_bytes = results_offset + 1;
	return DBG_STATUS_OK;
}

enum dbg_status qed_get_reg_fifo_results_buf_size(struct qed_hwfn *p_hwfn,
						  u32 *dump_buf,
						  u32 num_dumped_dwords,
						  u32 *results_buf_size)
{
	return qed_parse_reg_fifo_dump(p_hwfn,
				       dump_buf,
				       num_dumped_dwords,
				       NULL, results_buf_size);
}

enum dbg_status qed_print_reg_fifo_results(struct qed_hwfn *p_hwfn,
					   u32 *dump_buf,
					   u32 num_dumped_dwords,
					   char *results_buf)
{
	u32 parsed_buf_size;

	return qed_parse_reg_fifo_dump(p_hwfn,
				       dump_buf,
				       num_dumped_dwords,
				       results_buf, &parsed_buf_size);
}

/* Parses an IGU FIFO dump buffer.
 * If result_buf is not NULL, the IGU FIFO results are printed to it.
 * In any case, the required results buffer size is assigned to
 * parsed_results_bytes.
 * The parsing status is returned.
 */
static enum dbg_status qed_parse_igu_fifo_dump(struct qed_hwfn *p_hwfn,
					       u32 *dump_buf,
					       u32 num_dumped_dwords,
					       char *results_buf,
					       u32 *parsed_results_bytes)
{
	u32 results_offset = 0, param_num_val, num_section_params, num_elements;
	const char *section_name, *param_name, *param_str_val;
	struct igu_fifo_element *elements;
	char parsed_addr_data[32];
	char parsed_wr_data[256];
	u8 i, j;

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
		/* dword12 (dword index 1 and 2) contains bits 32..95 of the
		 * FIFO element.
		 */
		u64 dword12 =
		    ((u64)elements[i].dword2 << 32) | elements[i].dword1;
		bool is_wr_cmd = GET_FIELD(dword12,
					   IGU_FIFO_ELEMENT_DWORD12_IS_WR_CMD);
		bool is_pf = GET_FIELD(elements[i].dword0,
				       IGU_FIFO_ELEMENT_DWORD0_IS_PF);
		u16 cmd_addr = GET_FIELD(elements[i].dword0,
					 IGU_FIFO_ELEMENT_DWORD0_CMD_ADDR);
		u8 source = GET_FIELD(elements[i].dword0,
				      IGU_FIFO_ELEMENT_DWORD0_SOURCE);
		u8 err_type = GET_FIELD(elements[i].dword0,
					IGU_FIFO_ELEMENT_DWORD0_ERR_TYPE);
		const struct igu_fifo_addr_data *addr_data = NULL;

		if (source >= ARRAY_SIZE(s_igu_fifo_source_strs))
			return DBG_STATUS_IGU_FIFO_BAD_DATA;
		if (err_type >= ARRAY_SIZE(s_igu_fifo_error_strs))
			return DBG_STATUS_IGU_FIFO_BAD_DATA;

		/* Find address data */
		for (j = 0; j < ARRAY_SIZE(s_igu_fifo_addr_data) && !addr_data;
		     j++)
			if (cmd_addr >= s_igu_fifo_addr_data[j].start_addr &&
			    cmd_addr <= s_igu_fifo_addr_data[j].end_addr)
				addr_data = &s_igu_fifo_addr_data[j];
		if (!addr_data)
			return DBG_STATUS_IGU_FIFO_BAD_DATA;

		/* Prepare parsed address data */
		switch (addr_data->type) {
		case IGU_ADDR_TYPE_MSIX_MEM:
			sprintf(parsed_addr_data,
				" vector_num=0x%x", cmd_addr / 2);
			break;
		case IGU_ADDR_TYPE_WRITE_INT_ACK:
		case IGU_ADDR_TYPE_WRITE_PROD_UPDATE:
			sprintf(parsed_addr_data,
				" SB=0x%x", cmd_addr - addr_data->start_addr);
			break;
		default:
			parsed_addr_data[0] = '\0';
		}

		/* Prepare parsed write data */
		if (is_wr_cmd) {
			u32 wr_data = GET_FIELD(dword12,
					IGU_FIFO_ELEMENT_DWORD12_WR_DATA);
			u32 prod_cons = GET_FIELD(wr_data,
						  IGU_FIFO_WR_DATA_PROD_CONS);
			u8 is_cleanup = GET_FIELD(wr_data,
						  IGU_FIFO_WR_DATA_CMD_TYPE);

			if (source == IGU_SRC_ATTN) {
				sprintf(parsed_wr_data,
					"prod: 0x%x, ", prod_cons);
			} else {
				if (is_cleanup) {
					u8 cleanup_val = GET_FIELD(wr_data,
								   IGU_FIFO_CLEANUP_WR_DATA_CLEANUP_VAL);
					u8 cleanup_type = GET_FIELD(wr_data,
								    IGU_FIFO_CLEANUP_WR_DATA_CLEANUP_TYPE);

					sprintf(parsed_wr_data,
						"cmd_type: cleanup, cleanup_val: %s, cleanup_type: %d, ",
						cleanup_val ? "set" : "clear",
						cleanup_type);
				} else {
					u8 update_flag = GET_FIELD(wr_data,
								   IGU_FIFO_WR_DATA_UPDATE_FLAG);
					u8 en_dis_int_for_sb =
					    GET_FIELD(wr_data,
						      IGU_FIFO_WR_DATA_EN_DIS_INT_FOR_SB);
					u8 segment = GET_FIELD(wr_data,
							       IGU_FIFO_WR_DATA_SEGMENT);
					u8 timer_mask = GET_FIELD(wr_data,
								  IGU_FIFO_WR_DATA_TIMER_MASK);

					sprintf(parsed_wr_data,
						"cmd_type: prod/cons update, prod/cons: 0x%x, update_flag: %s, en_dis_int_for_sb: %s, segment: %s, timer_mask=%d, ",
						prod_cons,
						update_flag ? "update" : "nop",
						en_dis_int_for_sb
						? (en_dis_int_for_sb ==
						   1 ? "disable" : "nop") :
						"enable",
						segment ? "attn" : "regular",
						timer_mask);
				}
			}
		} else {
			parsed_wr_data[0] = '\0';
		}

		/* Add parsed element to parsed buffer */
		results_offset +=
		    sprintf(qed_get_buf_ptr(results_buf,
					    results_offset),
			    "raw: 0x%01x%08x%08x, %s: %d, source: %s, type: %s, cmd_addr: 0x%x (%s%s), %serror: %s\n",
			    elements[i].dword2, elements[i].dword1,
			    elements[i].dword0,
			    is_pf ? "pf" : "vf",
			    GET_FIELD(elements[i].dword0,
				      IGU_FIFO_ELEMENT_DWORD0_FID),
			    s_igu_fifo_source_strs[source],
			    is_wr_cmd ? "wr" : "rd", cmd_addr,
			    (!is_pf && addr_data->vf_desc)
			    ? addr_data->vf_desc : addr_data->desc,
			    parsed_addr_data, parsed_wr_data,
			    s_igu_fifo_error_strs[err_type]);
	}

	results_offset += sprintf(qed_get_buf_ptr(results_buf,
						  results_offset),
				  "fifo contained %d elements", num_elements);

	/* Add 1 for string NULL termination */
	*parsed_results_bytes = results_offset + 1;
	return DBG_STATUS_OK;
}

enum dbg_status qed_get_igu_fifo_results_buf_size(struct qed_hwfn *p_hwfn,
						  u32 *dump_buf,
						  u32 num_dumped_dwords,
						  u32 *results_buf_size)
{
	return qed_parse_igu_fifo_dump(p_hwfn,
				       dump_buf,
				       num_dumped_dwords,
				       NULL, results_buf_size);
}

enum dbg_status qed_print_igu_fifo_results(struct qed_hwfn *p_hwfn,
					   u32 *dump_buf,
					   u32 num_dumped_dwords,
					   char *results_buf)
{
	u32 parsed_buf_size;

	return qed_parse_igu_fifo_dump(p_hwfn,
				       dump_buf,
				       num_dumped_dwords,
				       results_buf, &parsed_buf_size);
}

static enum dbg_status
qed_parse_protection_override_dump(struct qed_hwfn *p_hwfn,
				   u32 *dump_buf,
				   u32 num_dumped_dwords,
				   char *results_buf,
				   u32 *parsed_results_bytes)
{
	u32 results_offset = 0, param_num_val, num_section_params, num_elements;
	const char *section_name, *param_name, *param_str_val;
	struct protection_override_element *elements;
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
	if (param_num_val % PROTECTION_OVERRIDE_ELEMENT_DWORDS != 0)
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
			    "window %2d, address: 0x%07x, size: %7lld regs, read: %lld, write: %lld, read protection: %-12s, write protection: %-12s\n",
			    i, address,
			    GET_FIELD(elements[i].data,
				      PROTECTION_OVERRIDE_ELEMENT_WINDOW_SIZE),
			    GET_FIELD(elements[i].data,
				      PROTECTION_OVERRIDE_ELEMENT_READ),
			    GET_FIELD(elements[i].data,
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

enum dbg_status
qed_get_protection_override_results_buf_size(struct qed_hwfn *p_hwfn,
					     u32 *dump_buf,
					     u32 num_dumped_dwords,
					     u32 *results_buf_size)
{
	return qed_parse_protection_override_dump(p_hwfn,
						  dump_buf,
						  num_dumped_dwords,
						  NULL, results_buf_size);
}

enum dbg_status qed_print_protection_override_results(struct qed_hwfn *p_hwfn,
						      u32 *dump_buf,
						      u32 num_dumped_dwords,
						      char *results_buf)
{
	u32 parsed_buf_size;

	return qed_parse_protection_override_dump(p_hwfn,
						  dump_buf,
						  num_dumped_dwords,
						  results_buf,
						  &parsed_buf_size);
}

/* Parses a FW Asserts dump buffer.
 * If result_buf is not NULL, the FW Asserts results are printed to it.
 * In any case, the required results buffer size is assigned to
 * parsed_results_bytes.
 * The parsing status is returned.
 */
static enum dbg_status qed_parse_fw_asserts_dump(struct qed_hwfn *p_hwfn,
						 u32 *dump_buf,
						 u32 num_dumped_dwords,
						 char *results_buf,
						 u32 *parsed_results_bytes)
{
	u32 results_offset = 0, num_section_params, param_num_val, i;
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
		const char *storm_letter = NULL;
		u32 storm_dump_size = 0;

		dump_buf += qed_read_section_hdr(dump_buf,
						 &section_name,
						 &num_section_params);
		if (!strcmp(section_name, "last")) {
			last_section_found = true;
			continue;
		} else if (strcmp(section_name, "fw_asserts")) {
			return DBG_STATUS_FW_ASSERTS_PARSE_FAILED;
		}

		/* Extract params */
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
				return DBG_STATUS_FW_ASSERTS_PARSE_FAILED;
		}

		if (!storm_letter || !storm_dump_size)
			return DBG_STATUS_FW_ASSERTS_PARSE_FAILED;

		/* Print data */
		results_offset += sprintf(qed_get_buf_ptr(results_buf,
							  results_offset),
					  "\n%sSTORM_ASSERT: size=%d\n",
					  storm_letter, storm_dump_size);
		for (i = 0; i < storm_dump_size; i++, dump_buf++)
			results_offset +=
			    sprintf(qed_get_buf_ptr(results_buf,
						    results_offset),
				    "%08x\n", *dump_buf);
	}

	/* Add 1 for string NULL termination */
	*parsed_results_bytes = results_offset + 1;
	return DBG_STATUS_OK;
}

enum dbg_status qed_get_fw_asserts_results_buf_size(struct qed_hwfn *p_hwfn,
						    u32 *dump_buf,
						    u32 num_dumped_dwords,
						    u32 *results_buf_size)
{
	return qed_parse_fw_asserts_dump(p_hwfn,
					 dump_buf,
					 num_dumped_dwords,
					 NULL, results_buf_size);
}

enum dbg_status qed_print_fw_asserts_results(struct qed_hwfn *p_hwfn,
					     u32 *dump_buf,
					     u32 num_dumped_dwords,
					     char *results_buf)
{
	u32 parsed_buf_size;

	return qed_parse_fw_asserts_dump(p_hwfn,
					 dump_buf,
					 num_dumped_dwords,
					 results_buf, &parsed_buf_size);
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

/* Feature meta data lookup table */
static struct {
	char *name;
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
} qed_features_lookup[] = {
	{
	"grc", qed_dbg_grc_get_dump_buf_size,
		    qed_dbg_grc_dump, NULL, NULL}, {
	"idle_chk",
		    qed_dbg_idle_chk_get_dump_buf_size,
		    qed_dbg_idle_chk_dump,
		    qed_print_idle_chk_results_wrapper,
		    qed_get_idle_chk_results_buf_size}, {
	"mcp_trace",
		    qed_dbg_mcp_trace_get_dump_buf_size,
		    qed_dbg_mcp_trace_dump, qed_print_mcp_trace_results,
		    qed_get_mcp_trace_results_buf_size}, {
	"reg_fifo",
		    qed_dbg_reg_fifo_get_dump_buf_size,
		    qed_dbg_reg_fifo_dump, qed_print_reg_fifo_results,
		    qed_get_reg_fifo_results_buf_size}, {
	"igu_fifo",
		    qed_dbg_igu_fifo_get_dump_buf_size,
		    qed_dbg_igu_fifo_dump, qed_print_igu_fifo_results,
		    qed_get_igu_fifo_results_buf_size}, {
	"protection_override",
		    qed_dbg_protection_override_get_dump_buf_size,
		    qed_dbg_protection_override_dump,
		    qed_print_protection_override_results,
		    qed_get_protection_override_results_buf_size}, {
	"fw_asserts",
		    qed_dbg_fw_asserts_get_dump_buf_size,
		    qed_dbg_fw_asserts_dump,
		    qed_print_fw_asserts_results,
		    qed_get_fw_asserts_results_buf_size},};

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
	    &p_hwfn->cdev->dbg_params.features[feature_idx];
	u32 text_size_bytes, null_char_pos, i;
	enum dbg_status rc;
	char *text_buf;

	/* Check if feature supports formatting capability */
	if (!qed_features_lookup[feature_idx].results_buf_size)
		return DBG_STATUS_OK;

	/* Obtain size of formatted output */
	rc = qed_features_lookup[feature_idx].
		results_buf_size(p_hwfn, (u32 *)feature->dump_buf,
				 feature->dumped_dwords, &text_size_bytes);
	if (rc != DBG_STATUS_OK)
		return rc;

	/* Make sure that the allocated size is a multiple of dword (4 bytes) */
	null_char_pos = text_size_bytes - 1;
	text_size_bytes = (text_size_bytes + 3) & ~0x3;

	if (text_size_bytes < QED_RESULTS_BUF_MIN_SIZE) {
		DP_NOTICE(p_hwfn->cdev,
			  "formatted size of feature was too small %d. Aborting\n",
			  text_size_bytes);
		return DBG_STATUS_INVALID_ARGS;
	}

	/* Allocate temp text buf */
	text_buf = vzalloc(text_size_bytes);
	if (!text_buf)
		return DBG_STATUS_VIRT_MEM_ALLOC_FAILED;

	/* Decode feature opcodes to string on temp buf */
	rc = qed_features_lookup[feature_idx].
		print_results(p_hwfn, (u32 *)feature->dump_buf,
			      feature->dumped_dwords, text_buf);
	if (rc != DBG_STATUS_OK) {
		vfree(text_buf);
		return rc;
	}

	/* Replace the original null character with a '\n' character.
	 * The bytes that were added as a result of the dword alignment are also
	 * padded with '\n' characters.
	 */
	for (i = null_char_pos; i < text_size_bytes; i++)
		text_buf[i] = '\n';

	/* Dump printable feature to log */
	if (p_hwfn->cdev->dbg_params.print_data)
		qed_dbg_print_feature(text_buf, text_size_bytes);

	/* Free the old dump_buf and point the dump_buf to the newly allocagted
	 * and formatted text buffer.
	 */
	vfree(feature->dump_buf);
	feature->dump_buf = text_buf;
	feature->buf_size = text_size_bytes;
	feature->dumped_dwords = text_size_bytes / 4;
	return rc;
}

/* Generic function for performing the dump of a debug feature. */
static enum dbg_status qed_dbg_dump(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt,
				    enum qed_dbg_features feature_idx)
{
	struct qed_dbg_feature *feature =
	    &p_hwfn->cdev->dbg_params.features[feature_idx];
	u32 buf_size_dwords;
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
	if (rc != DBG_STATUS_OK)
		return rc;
	feature->buf_size = buf_size_dwords * sizeof(u32);
	feature->dump_buf = vmalloc(feature->buf_size);
	if (!feature->dump_buf)
		return DBG_STATUS_VIRT_MEM_ALLOC_FAILED;

	rc = qed_features_lookup[feature_idx].
		perform_dump(p_hwfn, p_ptt, (u32 *)feature->dump_buf,
			     feature->buf_size / sizeof(u32),
			     &feature->dumped_dwords);

	/* If mcp is stuck we get DBG_STATUS_NVRAM_GET_IMAGE_FAILED error.
	 * In this case the buffer holds valid binary data, but we wont able
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
#define REGDUMP_HEADER_FEATURE_SHIFT		24
#define REGDUMP_HEADER_ENGINE_SHIFT		31
#define REGDUMP_HEADER_OMIT_ENGINE_SHIFT	30
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
};

static u32 qed_calc_regdump_header(enum debug_print_features feature,
				   int engine, u32 feature_size, u8 omit_engine)
{
	/* Insert the engine, feature and mode inside the header and combine it
	 * with feature size.
	 */
	return feature_size | (feature << REGDUMP_HEADER_FEATURE_SHIFT) |
	       (omit_engine << REGDUMP_HEADER_OMIT_ENGINE_SHIFT) |
	       (engine << REGDUMP_HEADER_ENGINE_SHIFT);
}

int qed_dbg_all_data(struct qed_dev *cdev, void *buffer)
{
	u8 cur_engine, omit_engine = 0, org_engine;
	u32 offset = 0, feature_size;
	int rc;

	if (cdev->num_hwfns == 1)
		omit_engine = 1;

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
			    qed_calc_regdump_header(IDLE_CHK, cur_engine,
						    feature_size, omit_engine);
			offset += (feature_size + REGDUMP_HEADER_SIZE);
		} else {
			DP_ERR(cdev, "qed_dbg_idle_chk failed. rc = %d\n", rc);
		}

		/* Second idle_chk */
		rc = qed_dbg_idle_chk(cdev, (u8 *)buffer + offset +
				      REGDUMP_HEADER_SIZE, &feature_size);
		if (!rc) {
			*(u32 *)((u8 *)buffer + offset) =
			    qed_calc_regdump_header(IDLE_CHK, cur_engine,
						    feature_size, omit_engine);
			offset += (feature_size + REGDUMP_HEADER_SIZE);
		} else {
			DP_ERR(cdev, "qed_dbg_idle_chk failed. rc = %d\n", rc);
		}

		/* reg_fifo dump */
		rc = qed_dbg_reg_fifo(cdev, (u8 *)buffer + offset +
				      REGDUMP_HEADER_SIZE, &feature_size);
		if (!rc) {
			*(u32 *)((u8 *)buffer + offset) =
			    qed_calc_regdump_header(REG_FIFO, cur_engine,
						    feature_size, omit_engine);
			offset += (feature_size + REGDUMP_HEADER_SIZE);
		} else {
			DP_ERR(cdev, "qed_dbg_reg_fifo failed. rc = %d\n", rc);
		}

		/* igu_fifo dump */
		rc = qed_dbg_igu_fifo(cdev, (u8 *)buffer + offset +
				      REGDUMP_HEADER_SIZE, &feature_size);
		if (!rc) {
			*(u32 *)((u8 *)buffer + offset) =
			    qed_calc_regdump_header(IGU_FIFO, cur_engine,
						    feature_size, omit_engine);
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
			    qed_calc_regdump_header(PROTECTION_OVERRIDE,
						    cur_engine,
						    feature_size, omit_engine);
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
			    qed_calc_regdump_header(FW_ASSERTS, cur_engine,
						    feature_size, omit_engine);
			offset += (feature_size + REGDUMP_HEADER_SIZE);
		} else {
			DP_ERR(cdev, "qed_dbg_fw_asserts failed. rc = %d\n",
			       rc);
		}

		/* GRC dump - must be last because when mcp stuck it will
		 * clutter idle_chk, reg_fifo, ...
		 */
		rc = qed_dbg_grc(cdev, (u8 *)buffer + offset +
				 REGDUMP_HEADER_SIZE, &feature_size);
		if (!rc) {
			*(u32 *)((u8 *)buffer + offset) =
			    qed_calc_regdump_header(GRC_DUMP, cur_engine,
						    feature_size, omit_engine);
			offset += (feature_size + REGDUMP_HEADER_SIZE);
		} else {
			DP_ERR(cdev, "qed_dbg_grc failed. rc = %d", rc);
		}
	}

	/* mcp_trace */
	rc = qed_dbg_mcp_trace(cdev, (u8 *)buffer + offset +
			       REGDUMP_HEADER_SIZE, &feature_size);
	if (!rc) {
		*(u32 *)((u8 *)buffer + offset) =
		    qed_calc_regdump_header(MCP_TRACE, cur_engine,
					    feature_size, omit_engine);
		offset += (feature_size + REGDUMP_HEADER_SIZE);
	} else {
		DP_ERR(cdev, "qed_dbg_mcp_trace failed. rc = %d\n", rc);
	}

	qed_set_debug_engine(cdev, org_engine);

	return 0;
}

int qed_dbg_all_data_size(struct qed_dev *cdev)
{
	u8 cur_engine, org_engine;
	u32 regs_len = 0;

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
	}

	/* Engine common */
	regs_len += REGDUMP_HEADER_SIZE + qed_dbg_mcp_trace_size(cdev);
	qed_set_debug_engine(cdev, org_engine);

	return regs_len;
}

int qed_dbg_feature(struct qed_dev *cdev, void *buffer,
		    enum qed_dbg_features feature, u32 *num_dumped_bytes)
{
	struct qed_hwfn *p_hwfn =
		&cdev->hwfns[cdev->dbg_params.engine_for_debug];
	struct qed_dbg_feature *qed_feature =
		&cdev->dbg_params.features[feature];
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
	*num_dumped_bytes = cdev->dbg_params.features[feature].dumped_dwords *
			    4;

out:
	qed_ptt_release(p_hwfn, p_ptt);
	return rc;
}

int qed_dbg_feature_size(struct qed_dev *cdev, enum qed_dbg_features feature)
{
	struct qed_hwfn *p_hwfn =
		&cdev->hwfns[cdev->dbg_params.engine_for_debug];
	struct qed_ptt *p_ptt = qed_ptt_acquire(p_hwfn);
	struct qed_dbg_feature *qed_feature =
		&cdev->dbg_params.features[feature];
	u32 buf_size_dwords;
	enum dbg_status rc;

	if (!p_ptt)
		return -EINVAL;

	rc = qed_features_lookup[feature].get_size(p_hwfn, p_ptt,
						   &buf_size_dwords);
	if (rc != DBG_STATUS_OK)
		buf_size_dwords = 0;

	qed_ptt_release(p_hwfn, p_ptt);
	qed_feature->buf_size = buf_size_dwords * sizeof(u32);
	return qed_feature->buf_size;
}

u8 qed_get_debug_engine(struct qed_dev *cdev)
{
	return cdev->dbg_params.engine_for_debug;
}

void qed_set_debug_engine(struct qed_dev *cdev, int engine_number)
{
	DP_VERBOSE(cdev, QED_MSG_DEBUG, "set debug engine to %d\n",
		   engine_number);
	cdev->dbg_params.engine_for_debug = engine_number;
}

void qed_dbg_pf_init(struct qed_dev *cdev)
{
	const u8 *dbg_values;

	/* Debug values are after init values.
	 * The offset is the first dword of the file.
	 */
	dbg_values = cdev->firmware->data + *(u32 *)cdev->firmware->data;
	qed_dbg_set_bin_ptr((u8 *)dbg_values);
	qed_dbg_user_set_bin_ptr((u8 *)dbg_values);
}

void qed_dbg_pf_exit(struct qed_dev *cdev)
{
	struct qed_dbg_feature *feature = NULL;
	enum qed_dbg_features feature_idx;

	/* Debug features' buffers may be allocated if debug feature was used
	 * but dump wasn't called.
	 */
	for (feature_idx = 0; feature_idx < DBG_FEATURE_NUM; feature_idx++) {
		feature = &cdev->dbg_params.features[feature_idx];
		if (feature->dump_buf) {
			vfree(feature->dump_buf);
			feature->dump_buf = NULL;
		}
	}
}
