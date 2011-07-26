/*
 * caam descriptor construction helper functions
 *
 * Copyright 2008-2011 Freescale Semiconductor, Inc.
 */

#include "desc.h"

#define IMMEDIATE (1 << 23)
#define CAAM_CMD_SZ sizeof(u32)
#define CAAM_PTR_SZ sizeof(dma_addr_t)
#define CAAM_DESC_BYTES_MAX (CAAM_CMD_SZ * MAX_CAAM_DESCSIZE)

#ifdef DEBUG
#define PRINT_POS do { printk(KERN_DEBUG "%02d: %s\n", desc_len(desc),\
			      &__func__[sizeof("append")]); } while (0)
#else
#define PRINT_POS
#endif

#define SET_OK_PROP_ERRORS (IMMEDIATE | LDST_CLASS_DECO | \
			    LDST_SRCDST_WORD_DECOCTRL | \
			    (LDOFF_CHG_SHARE_OK_PROP << LDST_OFFSET_SHIFT))
#define DISABLE_AUTO_INFO_FIFO (IMMEDIATE | LDST_CLASS_DECO | \
				LDST_SRCDST_WORD_DECOCTRL | \
				(LDOFF_DISABLE_AUTO_NFIFO << LDST_OFFSET_SHIFT))
#define ENABLE_AUTO_INFO_FIFO (IMMEDIATE | LDST_CLASS_DECO | \
			       LDST_SRCDST_WORD_DECOCTRL | \
			       (LDOFF_ENABLE_AUTO_NFIFO << LDST_OFFSET_SHIFT))

static inline int desc_len(u32 *desc)
{
	return *desc & HDR_DESCLEN_MASK;
}

static inline int desc_bytes(void *desc)
{
	return desc_len(desc) * CAAM_CMD_SZ;
}

static inline u32 *desc_end(u32 *desc)
{
	return desc + desc_len(desc);
}

static inline void *sh_desc_pdb(u32 *desc)
{
	return desc + 1;
}

static inline void init_desc(u32 *desc, u32 options)
{
	*desc = options | HDR_ONE | 1;
}

static inline void init_sh_desc(u32 *desc, u32 options)
{
	PRINT_POS;
	init_desc(desc, CMD_SHARED_DESC_HDR | options);
}

static inline void init_sh_desc_pdb(u32 *desc, u32 options, size_t pdb_bytes)
{
	u32 pdb_len = pdb_bytes / CAAM_CMD_SZ + 1;

	init_sh_desc(desc, ((pdb_len << HDR_START_IDX_SHIFT) + pdb_len) |
		     options);
}

static inline void init_job_desc(u32 *desc, u32 options)
{
	init_desc(desc, CMD_DESC_HDR | options);
}

static inline void append_ptr(u32 *desc, dma_addr_t ptr)
{
	dma_addr_t *offset = (dma_addr_t *)desc_end(desc);

	*offset = ptr;

	(*desc) += CAAM_PTR_SZ / CAAM_CMD_SZ;
}

static inline void init_job_desc_shared(u32 *desc, dma_addr_t ptr, int len,
					u32 options)
{
	PRINT_POS;
	init_job_desc(desc, HDR_SHARED | options |
		      (len << HDR_START_IDX_SHIFT));
	append_ptr(desc, ptr);
}

static inline void append_data(u32 *desc, void *data, int len)
{
	u32 *offset = desc_end(desc);

	if (len) /* avoid sparse warning: memcpy with byte count of 0 */
		memcpy(offset, data, len);

	(*desc) += (len + CAAM_CMD_SZ - 1) / CAAM_CMD_SZ;
}

static inline void append_cmd(u32 *desc, u32 command)
{
	u32 *cmd = desc_end(desc);

	*cmd = command;

	(*desc)++;
}

static inline void append_cmd_ptr(u32 *desc, dma_addr_t ptr, int len,
				  u32 command)
{
	append_cmd(desc, command | len);
	append_ptr(desc, ptr);
}

static inline void append_cmd_data(u32 *desc, void *data, int len,
				   u32 command)
{
	append_cmd(desc, command | IMMEDIATE | len);
	append_data(desc, data, len);
}

static inline u32 *append_jump(u32 *desc, u32 options)
{
	u32 *cmd = desc_end(desc);

	PRINT_POS;
	append_cmd(desc, CMD_JUMP | options);

	return cmd;
}

static inline void set_jump_tgt_here(u32 *desc, u32 *jump_cmd)
{
	*jump_cmd = *jump_cmd | (desc_len(desc) - (jump_cmd - desc));
}

#define APPEND_CMD(cmd, op) \
static inline void append_##cmd(u32 *desc, u32 options) \
{ \
	PRINT_POS; \
	append_cmd(desc, CMD_##op | options); \
}
APPEND_CMD(operation, OPERATION)
APPEND_CMD(move, MOVE)

#define APPEND_CMD_LEN(cmd, op) \
static inline void append_##cmd(u32 *desc, unsigned int len, u32 options) \
{ \
	PRINT_POS; \
	append_cmd(desc, CMD_##op | len | options); \
}
APPEND_CMD_LEN(seq_store, SEQ_STORE)
APPEND_CMD_LEN(seq_fifo_load, SEQ_FIFO_LOAD)
APPEND_CMD_LEN(seq_fifo_store, SEQ_FIFO_STORE)

#define APPEND_CMD_PTR(cmd, op) \
static inline void append_##cmd(u32 *desc, dma_addr_t ptr, unsigned int len, \
				u32 options) \
{ \
	PRINT_POS; \
	append_cmd_ptr(desc, ptr, len, CMD_##op | options); \
}
APPEND_CMD_PTR(key, KEY)
APPEND_CMD_PTR(seq_in_ptr, SEQ_IN_PTR)
APPEND_CMD_PTR(seq_out_ptr, SEQ_OUT_PTR)
APPEND_CMD_PTR(load, LOAD)
APPEND_CMD_PTR(store, STORE)
APPEND_CMD_PTR(fifo_load, FIFO_LOAD)
APPEND_CMD_PTR(fifo_store, FIFO_STORE)

#define APPEND_CMD_PTR_TO_IMM(cmd, op) \
static inline void append_##cmd##_as_imm(u32 *desc, void *data, \
					 unsigned int len, u32 options) \
{ \
	PRINT_POS; \
	append_cmd_data(desc, data, len, CMD_##op | options); \
}
APPEND_CMD_PTR_TO_IMM(load, LOAD);
APPEND_CMD_PTR_TO_IMM(fifo_load, FIFO_LOAD);

/*
 * 2nd variant for commands whose specified immediate length differs
 * from length of immediate data provided, e.g., split keys
 */
#define APPEND_CMD_PTR_TO_IMM2(cmd, op) \
static inline void append_##cmd##_as_imm(u32 *desc, void *data, \
					 unsigned int data_len, \
					 unsigned int len, u32 options) \
{ \
	PRINT_POS; \
	append_cmd(desc, CMD_##op | IMMEDIATE | len | options); \
	append_data(desc, data, data_len); \
}
APPEND_CMD_PTR_TO_IMM2(key, KEY);

#define APPEND_CMD_RAW_IMM(cmd, op, type) \
static inline void append_##cmd##_imm_##type(u32 *desc, type immediate, \
					     u32 options) \
{ \
	PRINT_POS; \
	append_cmd(desc, CMD_##op | IMMEDIATE | options | sizeof(type)); \
	append_cmd(desc, immediate); \
}
APPEND_CMD_RAW_IMM(load, LOAD, u32);

/*
 * Append math command. Only the last part of destination and source need to
 * be specified
 */
#define APPEND_MATH(op, desc, dest, src_0, src_1, len) \
append_cmd(desc, CMD_MATH | MATH_FUN_##op | MATH_DEST_##dest | \
	   MATH_SRC0_##src_0 | MATH_SRC1_##src_1 | (u32) (len & MATH_LEN_MASK));

#define append_math_add(desc, dest, src0, src1, len) \
	APPEND_MATH(ADD, desc, dest, src0, src1, len)
#define append_math_sub(desc, dest, src0, src1, len) \
	APPEND_MATH(SUB, desc, dest, src0, src1, len)
#define append_math_add_c(desc, dest, src0, src1, len) \
	APPEND_MATH(ADDC, desc, dest, src0, src1, len)
#define append_math_sub_b(desc, dest, src0, src1, len) \
	APPEND_MATH(SUBB, desc, dest, src0, src1, len)
#define append_math_and(desc, dest, src0, src1, len) \
	APPEND_MATH(AND, desc, dest, src0, src1, len)
#define append_math_or(desc, dest, src0, src1, len) \
	APPEND_MATH(OR, desc, dest, src0, src1, len)
#define append_math_xor(desc, dest, src0, src1, len) \
	APPEND_MATH(XOR, desc, dest, src0, src1, len)
#define append_math_lshift(desc, dest, src0, src1, len) \
	APPEND_MATH(LSHIFT, desc, dest, src0, src1, len)
#define append_math_rshift(desc, dest, src0, src1, len) \
	APPEND_MATH(RSHIFT, desc, dest, src0, src1, len)

/* Exactly one source is IMM. Data is passed in as u32 value */
#define APPEND_MATH_IMM_u32(op, desc, dest, src_0, src_1, data) \
do { \
	APPEND_MATH(op, desc, dest, src_0, src_1, CAAM_CMD_SZ); \
	append_cmd(desc, data); \
} while (0);

#define append_math_add_imm_u32(desc, dest, src0, src1, data) \
	APPEND_MATH_IMM_u32(ADD, desc, dest, src0, src1, data)
#define append_math_sub_imm_u32(desc, dest, src0, src1, data) \
	APPEND_MATH_IMM_u32(SUB, desc, dest, src0, src1, data)
#define append_math_add_c_imm_u32(desc, dest, src0, src1, data) \
	APPEND_MATH_IMM_u32(ADDC, desc, dest, src0, src1, data)
#define append_math_sub_b_imm_u32(desc, dest, src0, src1, data) \
	APPEND_MATH_IMM_u32(SUBB, desc, dest, src0, src1, data)
#define append_math_and_imm_u32(desc, dest, src0, src1, data) \
	APPEND_MATH_IMM_u32(AND, desc, dest, src0, src1, data)
#define append_math_or_imm_u32(desc, dest, src0, src1, data) \
	APPEND_MATH_IMM_u32(OR, desc, dest, src0, src1, data)
#define append_math_xor_imm_u32(desc, dest, src0, src1, data) \
	APPEND_MATH_IMM_u32(XOR, desc, dest, src0, src1, data)
#define append_math_lshift_imm_u32(desc, dest, src0, src1, data) \
	APPEND_MATH_IMM_u32(LSHIFT, desc, dest, src0, src1, data)
#define append_math_rshift_imm_u32(desc, dest, src0, src1, data) \
	APPEND_MATH_IMM_u32(RSHIFT, desc, dest, src0, src1, data)
