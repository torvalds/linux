/*
 * Copyright 2012-16 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */


#ifndef _DCE_DMCU_H_
#define _DCE_DMCU_H_

#include "dmcu.h"

#define DMCU_COMMON_REG_LIST_DCE_BASE() \
	SR(DMCU_CTRL), \
	SR(DMCU_STATUS), \
	SR(DMCU_RAM_ACCESS_CTRL), \
	SR(DMCU_IRAM_WR_CTRL), \
	SR(DMCU_IRAM_WR_DATA), \
	SR(MASTER_COMM_DATA_REG1), \
	SR(MASTER_COMM_DATA_REG2), \
	SR(MASTER_COMM_DATA_REG3), \
	SR(MASTER_COMM_CMD_REG), \
	SR(MASTER_COMM_CNTL_REG), \
	SR(DMCU_IRAM_RD_CTRL), \
	SR(DMCU_IRAM_RD_DATA), \
	SR(DMCU_INTERRUPT_TO_UC_EN_MASK), \
	SR(SMU_INTERRUPT_CONTROL), \
	SR(DC_DMCU_SCRATCH)

#define DMCU_DCE80_REG_LIST() \
	SR(DMCU_CTRL), \
	SR(DMCU_STATUS), \
	SR(DMCU_RAM_ACCESS_CTRL), \
	SR(DMCU_IRAM_WR_CTRL), \
	SR(DMCU_IRAM_WR_DATA), \
	SR(MASTER_COMM_DATA_REG1), \
	SR(MASTER_COMM_DATA_REG2), \
	SR(MASTER_COMM_DATA_REG3), \
	SR(MASTER_COMM_CMD_REG), \
	SR(MASTER_COMM_CNTL_REG), \
	SR(DMCU_IRAM_RD_CTRL), \
	SR(DMCU_IRAM_RD_DATA), \
	SR(DMCU_INTERRUPT_TO_UC_EN_MASK), \
	SR(SMU_INTERRUPT_CONTROL), \
	SR(DC_DMCU_SCRATCH)

#define DMCU_DCE110_COMMON_REG_LIST() \
	DMCU_COMMON_REG_LIST_DCE_BASE(), \
	SR(DCI_MEM_PWR_STATUS)

#define DMCU_DCN10_REG_LIST()\
	DMCU_COMMON_REG_LIST_DCE_BASE(), \
	SR(DMU_MEM_PWR_CNTL)

#define DMCU_DCN20_REG_LIST()\
	DMCU_DCN10_REG_LIST(), \
	SR(DMCUB_SCRATCH15)

#define DMCU_SF(reg_name, field_name, post_fix)\
	.field_name = reg_name ## __ ## field_name ## post_fix

#define DMCU_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(mask_sh) \
	DMCU_SF(DMCU_CTRL, \
			DMCU_ENABLE, mask_sh), \
	DMCU_SF(DMCU_STATUS, \
			UC_IN_STOP_MODE, mask_sh), \
	DMCU_SF(DMCU_STATUS, \
			UC_IN_RESET, mask_sh), \
	DMCU_SF(DMCU_RAM_ACCESS_CTRL, \
			IRAM_HOST_ACCESS_EN, mask_sh), \
	DMCU_SF(DMCU_RAM_ACCESS_CTRL, \
			IRAM_WR_ADDR_AUTO_INC, mask_sh), \
	DMCU_SF(DMCU_RAM_ACCESS_CTRL, \
			IRAM_RD_ADDR_AUTO_INC, mask_sh), \
	DMCU_SF(MASTER_COMM_CMD_REG, \
			MASTER_COMM_CMD_REG_BYTE0, mask_sh), \
	DMCU_SF(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, mask_sh), \
	DMCU_SF(DMCU_INTERRUPT_TO_UC_EN_MASK, \
			STATIC_SCREEN1_INT_TO_UC_EN, mask_sh), \
	DMCU_SF(DMCU_INTERRUPT_TO_UC_EN_MASK, \
			STATIC_SCREEN2_INT_TO_UC_EN, mask_sh), \
	DMCU_SF(DMCU_INTERRUPT_TO_UC_EN_MASK, \
			STATIC_SCREEN3_INT_TO_UC_EN, mask_sh), \
	DMCU_SF(DMCU_INTERRUPT_TO_UC_EN_MASK, \
			STATIC_SCREEN4_INT_TO_UC_EN, mask_sh), \
	DMCU_SF(SMU_INTERRUPT_CONTROL, DC_SMU_INT_ENABLE, mask_sh)

#define DMCU_MASK_SH_LIST_DCE80(mask_sh) \
	DMCU_SF(DMCU_CTRL, \
			DMCU_ENABLE, mask_sh), \
	DMCU_SF(DMCU_STATUS, \
			UC_IN_STOP_MODE, mask_sh), \
	DMCU_SF(DMCU_STATUS, \
			UC_IN_RESET, mask_sh), \
	DMCU_SF(DMCU_RAM_ACCESS_CTRL, \
			IRAM_HOST_ACCESS_EN, mask_sh), \
	DMCU_SF(DMCU_RAM_ACCESS_CTRL, \
			IRAM_WR_ADDR_AUTO_INC, mask_sh), \
	DMCU_SF(DMCU_RAM_ACCESS_CTRL, \
			IRAM_RD_ADDR_AUTO_INC, mask_sh), \
	DMCU_SF(MASTER_COMM_CMD_REG, \
			MASTER_COMM_CMD_REG_BYTE0, mask_sh), \
	DMCU_SF(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, mask_sh), \
	DMCU_SF(SMU_INTERRUPT_CONTROL, DC_SMU_INT_ENABLE, mask_sh)

#define DMCU_MASK_SH_LIST_DCE110(mask_sh) \
	DMCU_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(mask_sh), \
	DMCU_SF(DCI_MEM_PWR_STATUS, \
		DMCU_IRAM_MEM_PWR_STATE, mask_sh)

#define DMCU_MASK_SH_LIST_DCN10(mask_sh) \
	DMCU_COMMON_MASK_SH_LIST_DCE_COMMON_BASE(mask_sh), \
	DMCU_SF(DMU_MEM_PWR_CNTL, \
			DMCU_IRAM_MEM_PWR_STATE, mask_sh)

#define DMCU_REG_FIELD_LIST(type) \
	type DMCU_IRAM_MEM_PWR_STATE; \
	type IRAM_HOST_ACCESS_EN; \
	type IRAM_WR_ADDR_AUTO_INC; \
	type IRAM_RD_ADDR_AUTO_INC; \
	type DMCU_ENABLE; \
	type UC_IN_STOP_MODE; \
	type UC_IN_RESET; \
	type MASTER_COMM_CMD_REG_BYTE0; \
	type MASTER_COMM_INTERRUPT; \
	type DPHY_RX_FAST_TRAINING_CAPABLE; \
	type DPHY_LOAD_BS_COUNT; \
	type STATIC_SCREEN1_INT_TO_UC_EN; \
	type STATIC_SCREEN2_INT_TO_UC_EN; \
	type STATIC_SCREEN3_INT_TO_UC_EN; \
	type STATIC_SCREEN4_INT_TO_UC_EN; \
	type DP_SEC_GSP0_LINE_NUM; \
	type DP_SEC_GSP0_PRIORITY; \
	type DC_SMU_INT_ENABLE

struct dce_dmcu_shift {
	DMCU_REG_FIELD_LIST(uint8_t);
};

struct dce_dmcu_mask {
	DMCU_REG_FIELD_LIST(uint32_t);
};

struct dce_dmcu_registers {
	uint32_t DMCU_CTRL;
	uint32_t DMCU_STATUS;
	uint32_t DMCU_RAM_ACCESS_CTRL;
	uint32_t DCI_MEM_PWR_STATUS;
	uint32_t DMU_MEM_PWR_CNTL;
	uint32_t DMCU_IRAM_WR_CTRL;
	uint32_t DMCU_IRAM_WR_DATA;

	uint32_t MASTER_COMM_DATA_REG1;
	uint32_t MASTER_COMM_DATA_REG2;
	uint32_t MASTER_COMM_DATA_REG3;
	uint32_t MASTER_COMM_CMD_REG;
	uint32_t MASTER_COMM_CNTL_REG;
	uint32_t DMCU_IRAM_RD_CTRL;
	uint32_t DMCU_IRAM_RD_DATA;
	uint32_t DMCU_INTERRUPT_TO_UC_EN_MASK;
	uint32_t SMU_INTERRUPT_CONTROL;
	uint32_t DC_DMCU_SCRATCH;
	uint32_t DMCUB_SCRATCH15;
};

struct dce_dmcu {
	struct dmcu base;
	const struct dce_dmcu_registers *regs;
	const struct dce_dmcu_shift *dmcu_shift;
	const struct dce_dmcu_mask *dmcu_mask;
};

/*******************************************************************
 *   MASTER_COMM_DATA_REG1   Bit position    Data
 *                           7:0	            hyst_frames[7:0]
 *                           14:8	        hyst_lines[6:0]
 *                           15	            RFB_UPDATE_AUTO_EN
 *                           18:16	        phy_num[2:0]
 *                           21:19	        dcp_sel[2:0]
 *                           22	            phy_type
 *                           23	            frame_cap_ind
 *                           26:24	        aux_chan[2:0]
 *                           30:27	        aux_repeat[3:0]
 *                           31:31	        reserved[31:31]
 ******************************************************************/
union dce_dmcu_psr_config_data_reg1 {
	struct {
		unsigned int timehyst_frames:8;                  /*[7:0]*/
		unsigned int hyst_lines:7;                       /*[14:8]*/
		unsigned int rfb_update_auto_en:1;               /*[15:15]*/
		unsigned int dp_port_num:3;                      /*[18:16]*/
		unsigned int dcp_sel:3;                          /*[21:19]*/
		unsigned int phy_type:1;                         /*[22:22]*/
		unsigned int frame_cap_ind:1;                    /*[23:23]*/
		unsigned int aux_chan:3;                         /*[26:24]*/
		unsigned int aux_repeat:4;                       /*[30:27]*/
		unsigned int allow_smu_optimizations:1;         /*[31:31]*/
	} bits;
	unsigned int u32All;
};

/*******************************************************************
 *   MASTER_COMM_DATA_REG2
 *******************************************************************/
union dce_dmcu_psr_config_data_reg2 {
	struct {
		unsigned int dig_fe:3;                  /*[2:0]*/
		unsigned int dig_be:3;                  /*[5:3]*/
		unsigned int skip_wait_for_pll_lock:1;  /*[6:6]*/
		unsigned int reserved:9;                /*[15:7]*/
		unsigned int frame_delay:8;             /*[23:16]*/
		unsigned int smu_phy_id:4;              /*[27:24]*/
		unsigned int num_of_controllers:4;      /*[31:28]*/
	} bits;
	unsigned int u32All;
};

/*******************************************************************
 *   MASTER_COMM_DATA_REG3
 *******************************************************************/
union dce_dmcu_psr_config_data_reg3 {
	struct {
		unsigned int psr_level:16;      /*[15:0]*/
		unsigned int link_rate:4;       /*[19:16]*/
		unsigned int reserved:12;        /*[31:20]*/
	} bits;
	unsigned int u32All;
};

union dce_dmcu_psr_config_data_wait_loop_reg1 {
	struct {
		unsigned int wait_loop:16; /* [15:0] */
		unsigned int reserved:16; /* [31:16] */
	} bits;
	unsigned int u32;
};

struct dmcu *dce_dmcu_create(
	struct dc_context *ctx,
	const struct dce_dmcu_registers *regs,
	const struct dce_dmcu_shift *dmcu_shift,
	const struct dce_dmcu_mask *dmcu_mask);

struct dmcu *dcn10_dmcu_create(
	struct dc_context *ctx,
	const struct dce_dmcu_registers *regs,
	const struct dce_dmcu_shift *dmcu_shift,
	const struct dce_dmcu_mask *dmcu_mask);

struct dmcu *dcn20_dmcu_create(
	struct dc_context *ctx,
	const struct dce_dmcu_registers *regs,
	const struct dce_dmcu_shift *dmcu_shift,
	const struct dce_dmcu_mask *dmcu_mask);

struct dmcu *dcn21_dmcu_create(
	struct dc_context *ctx,
	const struct dce_dmcu_registers *regs,
	const struct dce_dmcu_shift *dmcu_shift,
	const struct dce_dmcu_mask *dmcu_mask);

void dce_dmcu_destroy(struct dmcu **dmcu);

static const uint32_t abm_gain_stepsize = 0x0060;

#endif /* _DCE_ABM_H_ */
