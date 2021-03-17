/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __MMSCH_V1_0_H__
#define __MMSCH_V1_0_H__

#define MMSCH_VERSION_MAJOR	1
#define MMSCH_VERSION_MINOR	0
#define MMSCH_VERSION	(MMSCH_VERSION_MAJOR << 16 | MMSCH_VERSION_MINOR)

enum mmsch_v1_0_command_type {
	MMSCH_COMMAND__DIRECT_REG_WRITE = 0,
	MMSCH_COMMAND__DIRECT_REG_POLLING = 2,
	MMSCH_COMMAND__DIRECT_REG_READ_MODIFY_WRITE = 3,
	MMSCH_COMMAND__INDIRECT_REG_WRITE = 8,
	MMSCH_COMMAND__END = 0xf
};

struct mmsch_v1_0_init_header {
	uint32_t version;
	uint32_t header_size;
	uint32_t vce_init_status;
	uint32_t uvd_init_status;
	uint32_t vce_table_offset;
	uint32_t vce_table_size;
	uint32_t uvd_table_offset;
	uint32_t uvd_table_size;
};

struct mmsch_vf_eng_init_header {
	uint32_t init_status;
	uint32_t table_offset;
	uint32_t table_size;
};

struct mmsch_v1_1_init_header {
	uint32_t version;
	uint32_t total_size;
	struct mmsch_vf_eng_init_header eng[2];
};

struct mmsch_v1_0_cmd_direct_reg_header {
	uint32_t reg_offset   : 28;
	uint32_t command_type : 4;
};

struct mmsch_v1_0_cmd_indirect_reg_header {
	uint32_t reg_offset    : 20;
	uint32_t reg_idx_space : 8;
	uint32_t command_type  : 4;
};

struct mmsch_v1_0_cmd_direct_write {
	struct mmsch_v1_0_cmd_direct_reg_header cmd_header;
	uint32_t reg_value;
};

struct mmsch_v1_0_cmd_direct_read_modify_write {
	struct mmsch_v1_0_cmd_direct_reg_header cmd_header;
	uint32_t write_data;
	uint32_t mask_value;
};

struct mmsch_v1_0_cmd_direct_polling {
	struct mmsch_v1_0_cmd_direct_reg_header cmd_header;
	uint32_t mask_value;
	uint32_t wait_value;
};

struct mmsch_v1_0_cmd_end {
	struct mmsch_v1_0_cmd_direct_reg_header cmd_header;
};

struct mmsch_v1_0_cmd_indirect_write {
	struct mmsch_v1_0_cmd_indirect_reg_header cmd_header;
	uint32_t reg_value;
};

static inline void mmsch_v1_0_insert_direct_wt(struct mmsch_v1_0_cmd_direct_write *direct_wt,
					       uint32_t *init_table,
					       uint32_t reg_offset,
					       uint32_t value)
{
	direct_wt->cmd_header.reg_offset = reg_offset;
	direct_wt->reg_value = value;
	memcpy((void *)init_table, direct_wt, sizeof(struct mmsch_v1_0_cmd_direct_write));
}

static inline void mmsch_v1_0_insert_direct_rd_mod_wt(struct mmsch_v1_0_cmd_direct_read_modify_write *direct_rd_mod_wt,
						      uint32_t *init_table,
						      uint32_t reg_offset,
						      uint32_t mask, uint32_t data)
{
	direct_rd_mod_wt->cmd_header.reg_offset = reg_offset;
	direct_rd_mod_wt->mask_value = mask;
	direct_rd_mod_wt->write_data = data;
	memcpy((void *)init_table, direct_rd_mod_wt,
	       sizeof(struct mmsch_v1_0_cmd_direct_read_modify_write));
}

static inline void mmsch_v1_0_insert_direct_poll(struct mmsch_v1_0_cmd_direct_polling *direct_poll,
						 uint32_t *init_table,
						 uint32_t reg_offset,
						 uint32_t mask, uint32_t wait)
{
	direct_poll->cmd_header.reg_offset = reg_offset;
	direct_poll->mask_value = mask;
	direct_poll->wait_value = wait;
	memcpy((void *)init_table, direct_poll, sizeof(struct mmsch_v1_0_cmd_direct_polling));
}

#define MMSCH_V1_0_INSERT_DIRECT_RD_MOD_WT(reg, mask, data) { \
	mmsch_v1_0_insert_direct_rd_mod_wt(&direct_rd_mod_wt, \
					   init_table, (reg), \
					   (mask), (data)); \
	init_table += sizeof(struct mmsch_v1_0_cmd_direct_read_modify_write)/4; \
	table_size += sizeof(struct mmsch_v1_0_cmd_direct_read_modify_write)/4; \
}

#define MMSCH_V1_0_INSERT_DIRECT_WT(reg, value) { \
	mmsch_v1_0_insert_direct_wt(&direct_wt, \
				    init_table, (reg), \
				    (value)); \
	init_table += sizeof(struct mmsch_v1_0_cmd_direct_write)/4; \
	table_size += sizeof(struct mmsch_v1_0_cmd_direct_write)/4; \
}

#define MMSCH_V1_0_INSERT_DIRECT_POLL(reg, mask, wait) { \
	mmsch_v1_0_insert_direct_poll(&direct_poll, \
				      init_table, (reg), \
				      (mask), (wait)); \
	init_table += sizeof(struct mmsch_v1_0_cmd_direct_polling)/4; \
	table_size += sizeof(struct mmsch_v1_0_cmd_direct_polling)/4; \
}

#endif
