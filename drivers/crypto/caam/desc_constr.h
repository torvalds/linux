/*
 * caam descriptor construction helper functions
 *
 * Copyright 2008-2011 Freescale Semiconductor, Inc.
 */

#include "desc.h"

#define IMMEDIATE (1 << 23)
#define CAAM_CMD_SZ sizeof(u32)
#define CAAM_PTR_SZ sizeof(dma_addr_t)
#define CAAM_DESC_BYTES_MAX (CAAM_CMD_SZ * 64)

#ifdef DEBUG
#define PRINT_POS do { printk(KERN_DEBUG "%02d: %s\n", desc_len(desc),\
			      &__func__[sizeof("append")]); } while (0)
#else
#define PRINT_POS
#endif

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
