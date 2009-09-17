/* arch/arm/mach-msm/qdsp5/adsp_vfe_verify_cmd.c
 *
 * Verification code for aDSP VFE packets from userspace.
 *
 * Copyright (c) 2008 QUALCOMM Incorporated
 * Copyright (C) 2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <mach/qdsp5/qdsp5vfecmdi.h>
#include "adsp.h"

static uint32_t size1_y, size2_y, size1_cbcr, size2_cbcr;
static uint32_t af_size = 4228;
static uint32_t awb_size = 8196;

static inline int verify_cmd_op_ack(struct msm_adsp_module *module,
				    void *cmd_data, size_t cmd_size)
{
	vfe_cmd_op1_ack *cmd = (vfe_cmd_op1_ack *)cmd_data;
	void **addr_y = (void **)&cmd->op1_buf_y_addr;
	void **addr_cbcr = (void **)(&cmd->op1_buf_cbcr_addr);

	if (cmd_size != sizeof(vfe_cmd_op1_ack))
		return -1;
	if ((*addr_y && adsp_pmem_fixup(module, addr_y, size1_y)) ||
	    (*addr_cbcr && adsp_pmem_fixup(module, addr_cbcr, size1_cbcr)))
		return -1;
	return 0;
}

static inline int verify_cmd_stats_autofocus_cfg(struct msm_adsp_module *module,
						 void *cmd_data, size_t cmd_size)
{
	int i;
	vfe_cmd_stats_autofocus_cfg *cmd =
		(vfe_cmd_stats_autofocus_cfg *)cmd_data;

	if (cmd_size != sizeof(vfe_cmd_stats_autofocus_cfg))
		return -1;

	for (i = 0; i < 3; i++) {
		void **addr = (void **)(&cmd->af_stats_op_buf[i]);
		if (*addr && adsp_pmem_fixup(module, addr, af_size))
			return -1;
	}
	return 0;
}

static inline int verify_cmd_stats_wb_exp_cfg(struct msm_adsp_module *module,
					      void *cmd_data, size_t cmd_size)
{
	vfe_cmd_stats_wb_exp_cfg *cmd =
		(vfe_cmd_stats_wb_exp_cfg *)cmd_data;
	int i;

	if (cmd_size != sizeof(vfe_cmd_stats_wb_exp_cfg))
		return -1;

	for (i = 0; i < 3; i++) {
		void **addr = (void **)(&cmd->wb_exp_stats_op_buf[i]);
		if (*addr && adsp_pmem_fixup(module, addr, awb_size))
			return -1;
	}
	return 0;
}

static inline int verify_cmd_stats_af_ack(struct msm_adsp_module *module,
					  void *cmd_data, size_t cmd_size)
{
	vfe_cmd_stats_af_ack *cmd = (vfe_cmd_stats_af_ack *)cmd_data;
	void **addr = (void **)&cmd->af_stats_op_buf;

	if (cmd_size != sizeof(vfe_cmd_stats_af_ack))
		return -1;

	if (*addr && adsp_pmem_fixup(module, addr, af_size))
		return -1;
	return 0;
}

static inline int verify_cmd_stats_wb_exp_ack(struct msm_adsp_module *module,
					      void *cmd_data, size_t cmd_size)
{
	vfe_cmd_stats_wb_exp_ack *cmd =
		(vfe_cmd_stats_wb_exp_ack *)cmd_data;
	void **addr = (void **)&cmd->wb_exp_stats_op_buf;

	if (cmd_size != sizeof(vfe_cmd_stats_wb_exp_ack))
		return -1;

	if (*addr && adsp_pmem_fixup(module, addr, awb_size))
		return -1;
	return 0;
}

static int verify_vfe_command(struct msm_adsp_module *module,
			      void *cmd_data, size_t cmd_size)
{
	uint32_t cmd_id = ((uint32_t *)cmd_data)[0];
	switch (cmd_id) {
	case VFE_CMD_OP1_ACK:
		return verify_cmd_op_ack(module, cmd_data, cmd_size);
	case VFE_CMD_OP2_ACK:
		return verify_cmd_op_ack(module, cmd_data, cmd_size);
	case VFE_CMD_STATS_AUTOFOCUS_CFG:
		return verify_cmd_stats_autofocus_cfg(module, cmd_data,
						      cmd_size);
	case VFE_CMD_STATS_WB_EXP_CFG:
		return verify_cmd_stats_wb_exp_cfg(module, cmd_data, cmd_size);
	case VFE_CMD_STATS_AF_ACK:
		return verify_cmd_stats_af_ack(module, cmd_data, cmd_size);
	case VFE_CMD_STATS_WB_EXP_ACK:
		return verify_cmd_stats_wb_exp_ack(module, cmd_data, cmd_size);
	default:
		if (cmd_id > 29) {
			printk(KERN_ERR "adsp: module %s: invalid VFE command id %d\n", module->name, cmd_id);
			return -1;
		}
	}
	return 0;
}

static int verify_vfe_command_scale(struct msm_adsp_module *module,
				    void *cmd_data, size_t cmd_size)
{
	uint32_t cmd_id = ((uint32_t *)cmd_data)[0];
	// FIXME: check the size
	if (cmd_id > 1) {
		printk(KERN_ERR "adsp: module %s: invalid VFE SCALE command id %d\n", module->name, cmd_id);
		return -1;
	}
	return 0;
}


static uint32_t get_size(uint32_t hw)
{
	uint32_t height, width;
	uint32_t height_mask = 0x3ffc;
	uint32_t width_mask = 0x3ffc000;

	height = (hw & height_mask) >> 2;
	width = (hw & width_mask) >> 14 ;
	return height * width;
}

static int verify_vfe_command_table(struct msm_adsp_module *module,
				    void *cmd_data, size_t cmd_size)
{
	uint32_t cmd_id = ((uint32_t *)cmd_data)[0];
	int i;

	switch (cmd_id) {
	case VFE_CMD_AXI_IP_CFG:
	{
		vfe_cmd_axi_ip_cfg *cmd = (vfe_cmd_axi_ip_cfg *)cmd_data;
		uint32_t size;
		if (cmd_size != sizeof(vfe_cmd_axi_ip_cfg)) {
			printk(KERN_ERR "adsp: module %s: invalid VFE TABLE (VFE_CMD_AXI_IP_CFG) command size %d\n",
				module->name, cmd_size);
			return -1;
		}
		size = get_size(cmd->ip_cfg_part2);

		for (i = 0; i < 8; i++) {
			void **addr = (void **)
				&cmd->ip_buf_addr[i];
			if (*addr && adsp_pmem_fixup(module, addr, size))
				return -1;
		}
	}
	case VFE_CMD_AXI_OP_CFG:
	{
		vfe_cmd_axi_op_cfg *cmd = (vfe_cmd_axi_op_cfg *)cmd_data;
		void **addr1_y, **addr2_y, **addr1_cbcr, **addr2_cbcr;

		if (cmd_size != sizeof(vfe_cmd_axi_op_cfg)) {
			printk(KERN_ERR "adsp: module %s: invalid VFE TABLE (VFE_CMD_AXI_OP_CFG) command size %d\n",
				module->name, cmd_size);
			return -1;
		}
		size1_y = get_size(cmd->op1_y_cfg_part2);
		size1_cbcr = get_size(cmd->op1_cbcr_cfg_part2);
		size2_y = get_size(cmd->op2_y_cfg_part2);
		size2_cbcr = get_size(cmd->op2_cbcr_cfg_part2);
		for (i = 0; i < 8; i++) {
			addr1_y = (void **)(&cmd->op1_buf1_addr[2*i]);
			addr1_cbcr = (void **)(&cmd->op1_buf1_addr[2*i+1]);
			addr2_y = (void **)(&cmd->op2_buf1_addr[2*i]);
			addr2_cbcr = (void **)(&cmd->op2_buf1_addr[2*i+1]);
/*
			printk("module %s: [%d] %p %p %p %p\n",
				module->name, i,
				*addr1_y, *addr1_cbcr, *addr2_y, *addr2_cbcr);
*/
			if ((*addr1_y && adsp_pmem_fixup(module, addr1_y, size1_y)) ||
			    (*addr1_cbcr && adsp_pmem_fixup(module, addr1_cbcr, size1_cbcr)) ||
			    (*addr2_y && adsp_pmem_fixup(module, addr2_y, size2_y)) ||
			    (*addr2_cbcr && adsp_pmem_fixup(module, addr2_cbcr, size2_cbcr)))
				return -1;
		}
	}
	default:
		if (cmd_id > 4) {
			printk(KERN_ERR "adsp: module %s: invalid VFE TABLE command id %d\n",
				module->name, cmd_id);
			return -1;
		}
	}
	return 0;
}

int adsp_vfe_verify_cmd(struct msm_adsp_module *module,
			unsigned int queue_id, void *cmd_data,
			size_t cmd_size)
{
	switch (queue_id) {
	case QDSP_vfeCommandQueue:
		return verify_vfe_command(module, cmd_data, cmd_size);
	case QDSP_vfeCommandScaleQueue:
		return verify_vfe_command_scale(module, cmd_data, cmd_size);
	case QDSP_vfeCommandTableQueue:
		return verify_vfe_command_table(module, cmd_data, cmd_size);
	default:
		printk(KERN_ERR "adsp: module %s: unknown queue id %d\n",
			module->name, queue_id);
		return -1;
	}
}
