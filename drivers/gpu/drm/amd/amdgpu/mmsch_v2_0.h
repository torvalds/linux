/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#ifndef __MMSCH_V2_0_H__
#define __MMSCH_V2_0_H__

// addressBlock: uvd0_mmsch_dec
// base address: 0x1e000
#define mmMMSCH_UCODE_ADDR                                                                             0x0000
#define mmMMSCH_UCODE_ADDR_BASE_IDX                                                                    0
#define mmMMSCH_UCODE_DATA                                                                             0x0001
#define mmMMSCH_UCODE_DATA_BASE_IDX                                                                    0
#define mmMMSCH_SRAM_ADDR                                                                              0x0002
#define mmMMSCH_SRAM_ADDR_BASE_IDX                                                                     0
#define mmMMSCH_SRAM_DATA                                                                              0x0003
#define mmMMSCH_SRAM_DATA_BASE_IDX                                                                     0
#define mmMMSCH_VF_SRAM_OFFSET                                                                         0x0004
#define mmMMSCH_VF_SRAM_OFFSET_BASE_IDX                                                                0
#define mmMMSCH_DB_SRAM_OFFSET                                                                         0x0005
#define mmMMSCH_DB_SRAM_OFFSET_BASE_IDX                                                                0
#define mmMMSCH_CTX_SRAM_OFFSET                                                                        0x0006
#define mmMMSCH_CTX_SRAM_OFFSET_BASE_IDX                                                               0
#define mmMMSCH_CTL                                                                                    0x0007
#define mmMMSCH_CTL_BASE_IDX                                                                           0
#define mmMMSCH_INTR                                                                                   0x0008
#define mmMMSCH_INTR_BASE_IDX                                                                          0
#define mmMMSCH_INTR_ACK                                                                               0x0009
#define mmMMSCH_INTR_ACK_BASE_IDX                                                                      0
#define mmMMSCH_INTR_STATUS                                                                            0x000a
#define mmMMSCH_INTR_STATUS_BASE_IDX                                                                   0
#define mmMMSCH_VF_VMID                                                                                0x000b
#define mmMMSCH_VF_VMID_BASE_IDX                                                                       0
#define mmMMSCH_VF_CTX_ADDR_LO                                                                         0x000c
#define mmMMSCH_VF_CTX_ADDR_LO_BASE_IDX                                                                0
#define mmMMSCH_VF_CTX_ADDR_HI                                                                         0x000d
#define mmMMSCH_VF_CTX_ADDR_HI_BASE_IDX                                                                0
#define mmMMSCH_VF_CTX_SIZE                                                                            0x000e
#define mmMMSCH_VF_CTX_SIZE_BASE_IDX                                                                   0
#define mmMMSCH_VF_GPCOM_ADDR_LO                                                                       0x000f
#define mmMMSCH_VF_GPCOM_ADDR_LO_BASE_IDX                                                              0
#define mmMMSCH_VF_GPCOM_ADDR_HI                                                                       0x0010
#define mmMMSCH_VF_GPCOM_ADDR_HI_BASE_IDX                                                              0
#define mmMMSCH_VF_GPCOM_SIZE                                                                          0x0011
#define mmMMSCH_VF_GPCOM_SIZE_BASE_IDX                                                                 0
#define mmMMSCH_VF_MAILBOX_HOST                                                                        0x0012
#define mmMMSCH_VF_MAILBOX_HOST_BASE_IDX                                                               0
#define mmMMSCH_VF_MAILBOX_RESP                                                                        0x0013
#define mmMMSCH_VF_MAILBOX_RESP_BASE_IDX                                                               0
#define mmMMSCH_VF_MAILBOX_0                                                                           0x0014
#define mmMMSCH_VF_MAILBOX_0_BASE_IDX                                                                  0
#define mmMMSCH_VF_MAILBOX_0_RESP                                                                      0x0015
#define mmMMSCH_VF_MAILBOX_0_RESP_BASE_IDX                                                             0
#define mmMMSCH_VF_MAILBOX_1                                                                           0x0016
#define mmMMSCH_VF_MAILBOX_1_BASE_IDX                                                                  0
#define mmMMSCH_VF_MAILBOX_1_RESP                                                                      0x0017
#define mmMMSCH_VF_MAILBOX_1_RESP_BASE_IDX                                                             0
#define mmMMSCH_CNTL                                                                                   0x001c
#define mmMMSCH_CNTL_BASE_IDX                                                                          0
#define mmMMSCH_NONCACHE_OFFSET0                                                                       0x001d
#define mmMMSCH_NONCACHE_OFFSET0_BASE_IDX                                                              0
#define mmMMSCH_NONCACHE_SIZE0                                                                         0x001e
#define mmMMSCH_NONCACHE_SIZE0_BASE_IDX                                                                0
#define mmMMSCH_NONCACHE_OFFSET1                                                                       0x001f
#define mmMMSCH_NONCACHE_OFFSET1_BASE_IDX                                                              0
#define mmMMSCH_NONCACHE_SIZE1                                                                         0x0020
#define mmMMSCH_NONCACHE_SIZE1_BASE_IDX                                                                0
#define mmMMSCH_PDEBUG_STATUS                                                                          0x0021
#define mmMMSCH_PDEBUG_STATUS_BASE_IDX                                                                 0
#define mmMMSCH_PDEBUG_DATA_32UPPERBITS                                                                0x0022
#define mmMMSCH_PDEBUG_DATA_32UPPERBITS_BASE_IDX                                                       0
#define mmMMSCH_PDEBUG_DATA_32LOWERBITS                                                                0x0023
#define mmMMSCH_PDEBUG_DATA_32LOWERBITS_BASE_IDX                                                       0
#define mmMMSCH_PDEBUG_EPC                                                                             0x0024
#define mmMMSCH_PDEBUG_EPC_BASE_IDX                                                                    0
#define mmMMSCH_PDEBUG_EXCCAUSE                                                                        0x0025
#define mmMMSCH_PDEBUG_EXCCAUSE_BASE_IDX                                                               0
#define mmMMSCH_PROC_STATE1                                                                            0x0026
#define mmMMSCH_PROC_STATE1_BASE_IDX                                                                   0
#define mmMMSCH_LAST_MC_ADDR                                                                           0x0027
#define mmMMSCH_LAST_MC_ADDR_BASE_IDX                                                                  0
#define mmMMSCH_LAST_MEM_ACCESS_HI                                                                     0x0028
#define mmMMSCH_LAST_MEM_ACCESS_HI_BASE_IDX                                                            0
#define mmMMSCH_LAST_MEM_ACCESS_LO                                                                     0x0029
#define mmMMSCH_LAST_MEM_ACCESS_LO_BASE_IDX                                                            0
#define mmMMSCH_IOV_ACTIVE_FCN_ID                                                                      0x002a
#define mmMMSCH_IOV_ACTIVE_FCN_ID_BASE_IDX                                                             0
#define mmMMSCH_SCRATCH_0                                                                              0x002b
#define mmMMSCH_SCRATCH_0_BASE_IDX                                                                     0
#define mmMMSCH_SCRATCH_1                                                                              0x002c
#define mmMMSCH_SCRATCH_1_BASE_IDX                                                                     0
#define mmMMSCH_GPUIOV_SCH_BLOCK_0                                                                     0x002d
#define mmMMSCH_GPUIOV_SCH_BLOCK_0_BASE_IDX                                                            0
#define mmMMSCH_GPUIOV_CMD_CONTROL_0                                                                   0x002e
#define mmMMSCH_GPUIOV_CMD_CONTROL_0_BASE_IDX                                                          0
#define mmMMSCH_GPUIOV_CMD_STATUS_0                                                                    0x002f
#define mmMMSCH_GPUIOV_CMD_STATUS_0_BASE_IDX                                                           0
#define mmMMSCH_GPUIOV_VM_BUSY_STATUS_0                                                                0x0030
#define mmMMSCH_GPUIOV_VM_BUSY_STATUS_0_BASE_IDX                                                       0
#define mmMMSCH_GPUIOV_ACTIVE_FCNS_0                                                                   0x0031
#define mmMMSCH_GPUIOV_ACTIVE_FCNS_0_BASE_IDX                                                          0
#define mmMMSCH_GPUIOV_ACTIVE_FCN_ID_0                                                                 0x0032
#define mmMMSCH_GPUIOV_ACTIVE_FCN_ID_0_BASE_IDX                                                        0
#define mmMMSCH_GPUIOV_DW6_0                                                                           0x0033
#define mmMMSCH_GPUIOV_DW6_0_BASE_IDX                                                                  0
#define mmMMSCH_GPUIOV_DW7_0                                                                           0x0034
#define mmMMSCH_GPUIOV_DW7_0_BASE_IDX                                                                  0
#define mmMMSCH_GPUIOV_DW8_0                                                                           0x0035
#define mmMMSCH_GPUIOV_DW8_0_BASE_IDX                                                                  0
#define mmMMSCH_GPUIOV_SCH_BLOCK_1                                                                     0x0036
#define mmMMSCH_GPUIOV_SCH_BLOCK_1_BASE_IDX                                                            0
#define mmMMSCH_GPUIOV_CMD_CONTROL_1                                                                   0x0037
#define mmMMSCH_GPUIOV_CMD_CONTROL_1_BASE_IDX                                                          0
#define mmMMSCH_GPUIOV_CMD_STATUS_1                                                                    0x0038
#define mmMMSCH_GPUIOV_CMD_STATUS_1_BASE_IDX                                                           0
#define mmMMSCH_GPUIOV_VM_BUSY_STATUS_1                                                                0x0039
#define mmMMSCH_GPUIOV_VM_BUSY_STATUS_1_BASE_IDX                                                       0
#define mmMMSCH_GPUIOV_ACTIVE_FCNS_1                                                                   0x003a
#define mmMMSCH_GPUIOV_ACTIVE_FCNS_1_BASE_IDX                                                          0
#define mmMMSCH_GPUIOV_ACTIVE_FCN_ID_1                                                                 0x003b
#define mmMMSCH_GPUIOV_ACTIVE_FCN_ID_1_BASE_IDX                                                        0
#define mmMMSCH_GPUIOV_DW6_1                                                                           0x003c
#define mmMMSCH_GPUIOV_DW6_1_BASE_IDX                                                                  0
#define mmMMSCH_GPUIOV_DW7_1                                                                           0x003d
#define mmMMSCH_GPUIOV_DW7_1_BASE_IDX                                                                  0
#define mmMMSCH_GPUIOV_DW8_1                                                                           0x003e
#define mmMMSCH_GPUIOV_DW8_1_BASE_IDX                                                                  0
#define mmMMSCH_GPUIOV_CNTXT                                                                           0x003f
#define mmMMSCH_GPUIOV_CNTXT_BASE_IDX                                                                  0
#define mmMMSCH_SCRATCH_2                                                                              0x0040
#define mmMMSCH_SCRATCH_2_BASE_IDX                                                                     0
#define mmMMSCH_SCRATCH_3                                                                              0x0041
#define mmMMSCH_SCRATCH_3_BASE_IDX                                                                     0
#define mmMMSCH_SCRATCH_4                                                                              0x0042
#define mmMMSCH_SCRATCH_4_BASE_IDX                                                                     0
#define mmMMSCH_SCRATCH_5                                                                              0x0043
#define mmMMSCH_SCRATCH_5_BASE_IDX                                                                     0
#define mmMMSCH_SCRATCH_6                                                                              0x0044
#define mmMMSCH_SCRATCH_6_BASE_IDX                                                                     0
#define mmMMSCH_SCRATCH_7                                                                              0x0045
#define mmMMSCH_SCRATCH_7_BASE_IDX                                                                     0
#define mmMMSCH_VFID_FIFO_HEAD_0                                                                       0x0046
#define mmMMSCH_VFID_FIFO_HEAD_0_BASE_IDX                                                              0
#define mmMMSCH_VFID_FIFO_TAIL_0                                                                       0x0047
#define mmMMSCH_VFID_FIFO_TAIL_0_BASE_IDX                                                              0
#define mmMMSCH_VFID_FIFO_HEAD_1                                                                       0x0048
#define mmMMSCH_VFID_FIFO_HEAD_1_BASE_IDX                                                              0
#define mmMMSCH_VFID_FIFO_TAIL_1                                                                       0x0049
#define mmMMSCH_VFID_FIFO_TAIL_1_BASE_IDX                                                              0
#define mmMMSCH_NACK_STATUS                                                                            0x004a
#define mmMMSCH_NACK_STATUS_BASE_IDX                                                                   0
#define mmMMSCH_VF_MAILBOX0_DATA                                                                       0x004b
#define mmMMSCH_VF_MAILBOX0_DATA_BASE_IDX                                                              0
#define mmMMSCH_VF_MAILBOX1_DATA                                                                       0x004c
#define mmMMSCH_VF_MAILBOX1_DATA_BASE_IDX                                                              0
#define mmMMSCH_GPUIOV_SCH_BLOCK_IP_0                                                                  0x004d
#define mmMMSCH_GPUIOV_SCH_BLOCK_IP_0_BASE_IDX                                                         0
#define mmMMSCH_GPUIOV_CMD_STATUS_IP_0                                                                 0x004e
#define mmMMSCH_GPUIOV_CMD_STATUS_IP_0_BASE_IDX                                                        0
#define mmMMSCH_GPUIOV_ACTIVE_FCN_ID_IP_0                                                              0x004f
#define mmMMSCH_GPUIOV_ACTIVE_FCN_ID_IP_0_BASE_IDX                                                     0
#define mmMMSCH_GPUIOV_SCH_BLOCK_IP_1                                                                  0x0050
#define mmMMSCH_GPUIOV_SCH_BLOCK_IP_1_BASE_IDX                                                         0
#define mmMMSCH_GPUIOV_CMD_STATUS_IP_1                                                                 0x0051
#define mmMMSCH_GPUIOV_CMD_STATUS_IP_1_BASE_IDX                                                        0
#define mmMMSCH_GPUIOV_ACTIVE_FCN_ID_IP_1                                                              0x0052
#define mmMMSCH_GPUIOV_ACTIVE_FCN_ID_IP_1_BASE_IDX                                                     0
#define mmMMSCH_GPUIOV_CNTXT_IP                                                                        0x0053
#define mmMMSCH_GPUIOV_CNTXT_IP_BASE_IDX                                                               0
#define mmMMSCH_GPUIOV_SCH_BLOCK_2                                                                     0x0054
#define mmMMSCH_GPUIOV_SCH_BLOCK_2_BASE_IDX                                                            0
#define mmMMSCH_GPUIOV_CMD_CONTROL_2                                                                   0x0055
#define mmMMSCH_GPUIOV_CMD_CONTROL_2_BASE_IDX                                                          0
#define mmMMSCH_GPUIOV_CMD_STATUS_2                                                                    0x0056
#define mmMMSCH_GPUIOV_CMD_STATUS_2_BASE_IDX                                                           0
#define mmMMSCH_GPUIOV_VM_BUSY_STATUS_2                                                                0x0057
#define mmMMSCH_GPUIOV_VM_BUSY_STATUS_2_BASE_IDX                                                       0
#define mmMMSCH_GPUIOV_ACTIVE_FCNS_2                                                                   0x0058
#define mmMMSCH_GPUIOV_ACTIVE_FCNS_2_BASE_IDX                                                          0
#define mmMMSCH_GPUIOV_ACTIVE_FCN_ID_2                                                                 0x0059
#define mmMMSCH_GPUIOV_ACTIVE_FCN_ID_2_BASE_IDX                                                        0
#define mmMMSCH_GPUIOV_DW6_2                                                                           0x005a
#define mmMMSCH_GPUIOV_DW6_2_BASE_IDX                                                                  0
#define mmMMSCH_GPUIOV_DW7_2                                                                           0x005b
#define mmMMSCH_GPUIOV_DW7_2_BASE_IDX                                                                  0
#define mmMMSCH_GPUIOV_DW8_2                                                                           0x005c
#define mmMMSCH_GPUIOV_DW8_2_BASE_IDX                                                                  0
#define mmMMSCH_GPUIOV_SCH_BLOCK_IP_2                                                                  0x005d
#define mmMMSCH_GPUIOV_SCH_BLOCK_IP_2_BASE_IDX                                                         0
#define mmMMSCH_GPUIOV_CMD_STATUS_IP_2                                                                 0x005e
#define mmMMSCH_GPUIOV_CMD_STATUS_IP_2_BASE_IDX                                                        0
#define mmMMSCH_GPUIOV_ACTIVE_FCN_ID_IP_2                                                              0x005f
#define mmMMSCH_GPUIOV_ACTIVE_FCN_ID_IP_2_BASE_IDX                                                     0
#define mmMMSCH_VFID_FIFO_HEAD_2                                                                       0x0060
#define mmMMSCH_VFID_FIFO_HEAD_2_BASE_IDX                                                              0
#define mmMMSCH_VFID_FIFO_TAIL_2                                                                       0x0061
#define mmMMSCH_VFID_FIFO_TAIL_2_BASE_IDX                                                              0
#define mmMMSCH_VM_BUSY_STATUS_0                                                                       0x0062
#define mmMMSCH_VM_BUSY_STATUS_0_BASE_IDX                                                              0
#define mmMMSCH_VM_BUSY_STATUS_1                                                                       0x0063
#define mmMMSCH_VM_BUSY_STATUS_1_BASE_IDX                                                              0
#define mmMMSCH_VM_BUSY_STATUS_2                                                                       0x0064
#define mmMMSCH_VM_BUSY_STATUS_2_BASE_IDX                                                              0

#define MMSCH_VERSION_MAJOR	2
#define MMSCH_VERSION_MINOR	0
#define MMSCH_VERSION	(MMSCH_VERSION_MAJOR << 16 | MMSCH_VERSION_MINOR)

enum mmsch_v2_0_command_type {
	MMSCH_COMMAND__DIRECT_REG_WRITE = 0,
	MMSCH_COMMAND__DIRECT_REG_POLLING = 2,
	MMSCH_COMMAND__DIRECT_REG_READ_MODIFY_WRITE = 3,
	MMSCH_COMMAND__INDIRECT_REG_WRITE = 8,
	MMSCH_COMMAND__END = 0xf
};

struct mmsch_v2_0_init_header {
	uint32_t version;
	uint32_t header_size;
	uint32_t vcn_init_status;
	uint32_t vcn_table_offset;
	uint32_t vcn_table_size;
};

struct mmsch_v2_0_cmd_direct_reg_header {
	uint32_t reg_offset   : 28;
	uint32_t command_type : 4;
};

struct mmsch_v2_0_cmd_indirect_reg_header {
	uint32_t reg_offset    : 20;
	uint32_t reg_idx_space : 8;
	uint32_t command_type  : 4;
};

struct mmsch_v2_0_cmd_direct_write {
	struct mmsch_v2_0_cmd_direct_reg_header cmd_header;
	uint32_t reg_value;
};

struct mmsch_v2_0_cmd_direct_read_modify_write {
	struct mmsch_v2_0_cmd_direct_reg_header cmd_header;
	uint32_t write_data;
	uint32_t mask_value;
};

struct mmsch_v2_0_cmd_direct_polling {
	struct mmsch_v2_0_cmd_direct_reg_header cmd_header;
	uint32_t mask_value;
	uint32_t wait_value;
};

struct mmsch_v2_0_cmd_end {
	struct mmsch_v2_0_cmd_direct_reg_header cmd_header;
};

struct mmsch_v2_0_cmd_indirect_write {
	struct mmsch_v2_0_cmd_indirect_reg_header cmd_header;
	uint32_t reg_value;
};

static inline void mmsch_v2_0_insert_direct_wt(struct mmsch_v2_0_cmd_direct_write *direct_wt,
					       uint32_t *init_table,
					       uint32_t reg_offset,
					       uint32_t value)
{
	direct_wt->cmd_header.reg_offset = reg_offset;
	direct_wt->reg_value = value;
	memcpy((void *)init_table, direct_wt, sizeof(struct mmsch_v2_0_cmd_direct_write));
}

static inline void mmsch_v2_0_insert_direct_rd_mod_wt(struct mmsch_v2_0_cmd_direct_read_modify_write *direct_rd_mod_wt,
						      uint32_t *init_table,
						      uint32_t reg_offset,
						      uint32_t mask, uint32_t data)
{
	direct_rd_mod_wt->cmd_header.reg_offset = reg_offset;
	direct_rd_mod_wt->mask_value = mask;
	direct_rd_mod_wt->write_data = data;
	memcpy((void *)init_table, direct_rd_mod_wt,
	       sizeof(struct mmsch_v2_0_cmd_direct_read_modify_write));
}

static inline void mmsch_v2_0_insert_direct_poll(struct mmsch_v2_0_cmd_direct_polling *direct_poll,
						 uint32_t *init_table,
						 uint32_t reg_offset,
						 uint32_t mask, uint32_t wait)
{
	direct_poll->cmd_header.reg_offset = reg_offset;
	direct_poll->mask_value = mask;
	direct_poll->wait_value = wait;
	memcpy((void *)init_table, direct_poll, sizeof(struct mmsch_v2_0_cmd_direct_polling));
}

#define MMSCH_V2_0_INSERT_DIRECT_RD_MOD_WT(reg, mask, data) { \
	mmsch_v2_0_insert_direct_rd_mod_wt(&direct_rd_mod_wt, \
					   init_table, (reg), \
					   (mask), (data)); \
	init_table += sizeof(struct mmsch_v2_0_cmd_direct_read_modify_write)/4; \
	table_size += sizeof(struct mmsch_v2_0_cmd_direct_read_modify_write)/4; \
}

#define MMSCH_V2_0_INSERT_DIRECT_WT(reg, value) { \
	mmsch_v2_0_insert_direct_wt(&direct_wt, \
				    init_table, (reg), \
				    (value)); \
	init_table += sizeof(struct mmsch_v2_0_cmd_direct_write)/4; \
	table_size += sizeof(struct mmsch_v2_0_cmd_direct_write)/4; \
}

#define MMSCH_V2_0_INSERT_DIRECT_POLL(reg, mask, wait) { \
	mmsch_v2_0_insert_direct_poll(&direct_poll, \
				      init_table, (reg), \
				      (mask), (wait)); \
	init_table += sizeof(struct mmsch_v2_0_cmd_direct_polling)/4; \
	table_size += sizeof(struct mmsch_v2_0_cmd_direct_polling)/4; \
}

#endif
