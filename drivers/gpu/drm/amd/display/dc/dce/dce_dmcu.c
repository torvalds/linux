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

#include "core_types.h"
#include "link_encoder.h"
#include "dce_dmcu.h"
#include "dm_services.h"
#include "reg_helper.h"
#include "fixed32_32.h"
#include "dc.h"

#define TO_DCE_DMCU(dmcu)\
	container_of(dmcu, struct dce_dmcu, base)

#define REG(reg) \
	(dmcu_dce->regs->reg)

#undef FN
#define FN(reg_name, field_name) \
	dmcu_dce->dmcu_shift->field_name, dmcu_dce->dmcu_mask->field_name

#define CTX \
	dmcu_dce->base.ctx

/* PSR related commands */
#define PSR_ENABLE 0x20
#define PSR_EXIT 0x21
#define PSR_SET 0x23
#define PSR_SET_WAITLOOP 0x31
#define MASTER_COMM_CNTL_REG__MASTER_COMM_INTERRUPT_MASK   0x00000001L
unsigned int cached_wait_loop_number = 0;

bool dce_dmcu_load_iram(struct dmcu *dmcu,
		unsigned int start_offset,
		const char *src,
		unsigned int bytes)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	unsigned int count = 0;

	/* Enable write access to IRAM */
	REG_UPDATE_2(DMCU_RAM_ACCESS_CTRL,
			IRAM_HOST_ACCESS_EN, 1,
			IRAM_WR_ADDR_AUTO_INC, 1);

	REG_WAIT(DCI_MEM_PWR_STATUS, DMCU_IRAM_MEM_PWR_STATE, 0, 2, 10);

	REG_WRITE(DMCU_IRAM_WR_CTRL, start_offset);

	for (count = 0; count < bytes; count++)
		REG_WRITE(DMCU_IRAM_WR_DATA, src[count]);

	/* Disable write access to IRAM to allow dynamic sleep state */
	REG_UPDATE_2(DMCU_RAM_ACCESS_CTRL,
			IRAM_HOST_ACCESS_EN, 0,
			IRAM_WR_ADDR_AUTO_INC, 0);

	return true;
}

static void dce_get_dmcu_psr_state(struct dmcu *dmcu, uint32_t *psr_state)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);

	uint32_t psrStateOffset = 0xf0;

	/* Enable write access to IRAM */
	REG_UPDATE(DMCU_RAM_ACCESS_CTRL, IRAM_HOST_ACCESS_EN, 1);

	REG_WAIT(DCI_MEM_PWR_STATUS, DMCU_IRAM_MEM_PWR_STATE, 0, 2, 10);

	/* Write address to IRAM_RD_ADDR in DMCU_IRAM_RD_CTRL */
	REG_WRITE(DMCU_IRAM_RD_CTRL, psrStateOffset);

	/* Read data from IRAM_RD_DATA in DMCU_IRAM_RD_DATA*/
	*psr_state = REG_READ(DMCU_IRAM_RD_DATA);

	/* Disable write access to IRAM after finished using IRAM
	 * in order to allow dynamic sleep state
	 */
	REG_UPDATE(DMCU_RAM_ACCESS_CTRL, IRAM_HOST_ACCESS_EN, 0);
}

static void dce_dmcu_set_psr_enable(struct dmcu *dmcu, bool enable, bool wait)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	unsigned int dmcu_max_retry_on_wait_reg_ready = 801;
	unsigned int dmcu_wait_reg_ready_interval = 100;

	unsigned int retryCount;
	uint32_t psr_state = 0;

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0,
				dmcu_wait_reg_ready_interval,
				dmcu_max_retry_on_wait_reg_ready);

	/* setDMCUParam_Cmd */
	if (enable)
		REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0,
				PSR_ENABLE);
	else
		REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0,
				PSR_EXIT);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);
	if (wait == true) {
		for (retryCount = 0; retryCount <= 100; retryCount++) {
			dce_get_dmcu_psr_state(dmcu, &psr_state);
			if (enable) {
				if (psr_state != 0)
					break;
			} else {
				if (psr_state == 0)
					break;
			}
			udelay(10);
		}
	}
}

static void dce_dmcu_setup_psr(struct dmcu *dmcu,
		struct dc_link *link,
		struct psr_context *psr_context)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);

	unsigned int dmcu_max_retry_on_wait_reg_ready = 801;
	unsigned int dmcu_wait_reg_ready_interval = 100;

	union dce_dmcu_psr_config_data_reg1 masterCmdData1;
	union dce_dmcu_psr_config_data_reg2 masterCmdData2;
	union dce_dmcu_psr_config_data_reg3 masterCmdData3;

	link->link_enc->funcs->psr_program_dp_dphy_fast_training(link->link_enc,
			psr_context->psrExitLinkTrainingRequired);

	/* Enable static screen interrupts for PSR supported display */
	/* Disable the interrupt coming from other displays. */
	REG_UPDATE_4(DMCU_INTERRUPT_TO_UC_EN_MASK,
			STATIC_SCREEN1_INT_TO_UC_EN, 0,
			STATIC_SCREEN2_INT_TO_UC_EN, 0,
			STATIC_SCREEN3_INT_TO_UC_EN, 0,
			STATIC_SCREEN4_INT_TO_UC_EN, 0);

	switch (psr_context->controllerId) {
	/* Driver uses case 1 for unconfigured */
	case 1:
		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN1_INT_TO_UC_EN, 1);
		break;
	case 2:
		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN2_INT_TO_UC_EN, 1);
		break;
	case 3:
		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN3_INT_TO_UC_EN, 1);
		break;
	case 4:
		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN4_INT_TO_UC_EN, 1);
		break;
	case 5:
		/* CZ/NL only has 4 CRTC!!
		 * really valid.
		 * There is no interrupt enable mask for these instances.
		 */
		break;
	case 6:
		/* CZ/NL only has 4 CRTC!!
		 * These are here because they are defined in HW regspec,
		 * but not really valid. There is no interrupt enable mask
		 * for these instances.
		 */
		break;
	default:
		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN1_INT_TO_UC_EN, 1);
		break;
	}

	link->link_enc->funcs->psr_program_secondary_packet(link->link_enc,
			psr_context->sdpTransmitLineNumDeadline);

	if (psr_context->psr_level.bits.SKIP_SMU_NOTIFICATION)
		REG_UPDATE(SMU_INTERRUPT_CONTROL, DC_SMU_INT_ENABLE, 1);

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0,
					dmcu_wait_reg_ready_interval,
					dmcu_max_retry_on_wait_reg_ready);

	/* setDMCUParam_PSRHostConfigData */
	masterCmdData1.u32All = 0;
	masterCmdData1.bits.timehyst_frames = psr_context->timehyst_frames;
	masterCmdData1.bits.hyst_lines = psr_context->hyst_lines;
	masterCmdData1.bits.rfb_update_auto_en =
			psr_context->rfb_update_auto_en;
	masterCmdData1.bits.dp_port_num = psr_context->transmitterId;
	masterCmdData1.bits.dcp_sel = psr_context->controllerId;
	masterCmdData1.bits.phy_type  = psr_context->phyType;
	masterCmdData1.bits.frame_cap_ind =
			psr_context->psrFrameCaptureIndicationReq;
	masterCmdData1.bits.aux_chan = psr_context->channel;
	masterCmdData1.bits.aux_repeat = psr_context->aux_repeats;
	dm_write_reg(dmcu->ctx, REG(MASTER_COMM_DATA_REG1),
					masterCmdData1.u32All);

	masterCmdData2.u32All = 0;
	masterCmdData2.bits.dig_fe = psr_context->engineId;
	masterCmdData2.bits.dig_be = psr_context->transmitterId;
	masterCmdData2.bits.skip_wait_for_pll_lock =
			psr_context->skipPsrWaitForPllLock;
	masterCmdData2.bits.frame_delay = psr_context->frame_delay;
	masterCmdData2.bits.smu_phy_id = psr_context->smuPhyId;
	masterCmdData2.bits.num_of_controllers =
			psr_context->numberOfControllers;
	dm_write_reg(dmcu->ctx, REG(MASTER_COMM_DATA_REG2),
			masterCmdData2.u32All);

	masterCmdData3.u32All = 0;
	masterCmdData3.bits.psr_level = psr_context->psr_level.u32all;
	dm_write_reg(dmcu->ctx, REG(MASTER_COMM_DATA_REG3),
			masterCmdData3.u32All);

	/* setDMCUParam_Cmd */
	REG_UPDATE(MASTER_COMM_CMD_REG,
			MASTER_COMM_CMD_REG_BYTE0, PSR_SET);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);
}

static void dce_psr_wait_loop(
	struct dmcu *dmcu,
	unsigned int wait_loop_number)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	union dce_dmcu_psr_config_data_wait_loop_reg1 masterCmdData1;
	if (cached_wait_loop_number == wait_loop_number)
		return;

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 1, 10000);

	masterCmdData1.u32 = 0;
	masterCmdData1.bits.wait_loop = wait_loop_number;
	cached_wait_loop_number = wait_loop_number;
	dm_write_reg(dmcu->ctx, REG(MASTER_COMM_DATA_REG1), masterCmdData1.u32);

	/* setDMCUParam_Cmd */
	REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0, PSR_SET_WAITLOOP);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);
}

static void dce_get_psr_wait_loop(unsigned int *psr_wait_loop_number)
{
	*psr_wait_loop_number = cached_wait_loop_number;
	return;
}

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
bool dcn10_dmcu_load_iram(struct dmcu *dmcu,
		unsigned int start_offset,
		const char *src,
		unsigned int bytes)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	unsigned int count = 0;

	REG_UPDATE(DMCU_CTRL, DMCU_ENABLE, 1);

	/* Enable write access to IRAM */
	REG_UPDATE_2(DMCU_RAM_ACCESS_CTRL,
			IRAM_HOST_ACCESS_EN, 1,
			IRAM_WR_ADDR_AUTO_INC, 1);

	REG_WAIT(DMU_MEM_PWR_CNTL, DMCU_IRAM_MEM_PWR_STATE, 0, 2, 10);

	REG_WRITE(DMCU_IRAM_WR_CTRL, start_offset);

	for (count = 0; count < bytes; count++)
		REG_WRITE(DMCU_IRAM_WR_DATA, src[count]);

	/* Disable write access to IRAM to allow dynamic sleep state */
	REG_UPDATE_2(DMCU_RAM_ACCESS_CTRL,
			IRAM_HOST_ACCESS_EN, 0,
			IRAM_WR_ADDR_AUTO_INC, 0);

	return true;
}

static void dcn10_get_dmcu_psr_state(struct dmcu *dmcu, uint32_t *psr_state)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);

	uint32_t psrStateOffset = 0xf0;

	/* Enable write access to IRAM */
	REG_UPDATE(DMCU_RAM_ACCESS_CTRL, IRAM_HOST_ACCESS_EN, 1);

	REG_WAIT(DMU_MEM_PWR_CNTL, DMCU_IRAM_MEM_PWR_STATE, 0, 2, 10);

	/* Write address to IRAM_RD_ADDR in DMCU_IRAM_RD_CTRL */
	REG_WRITE(DMCU_IRAM_RD_CTRL, psrStateOffset);

	/* Read data from IRAM_RD_DATA in DMCU_IRAM_RD_DATA*/
	*psr_state = REG_READ(DMCU_IRAM_RD_DATA);

	/* Disable write access to IRAM after finished using IRAM
	 * in order to allow dynamic sleep state
	 */
	REG_UPDATE(DMCU_RAM_ACCESS_CTRL, IRAM_HOST_ACCESS_EN, 0);
}

static void dcn10_dmcu_set_psr_enable(struct dmcu *dmcu, bool enable, bool wait)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	unsigned int dmcu_max_retry_on_wait_reg_ready = 801;
	unsigned int dmcu_wait_reg_ready_interval = 100;

	unsigned int retryCount;
	uint32_t psr_state = 0;

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0,
				dmcu_wait_reg_ready_interval,
				dmcu_max_retry_on_wait_reg_ready);

	/* setDMCUParam_Cmd */
	if (enable)
		REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0,
				PSR_ENABLE);
	else
		REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0,
				PSR_EXIT);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);

	/* Below loops 1000 x 500us = 500 ms.
	 *  Exit PSR may need to wait 1-2 frames to power up. Timeout after at
	 *  least a few frames. Should never hit the max retry assert below.
	 */
	if (wait == true) {
	for (retryCount = 0; retryCount <= 1000; retryCount++) {
		dcn10_get_dmcu_psr_state(dmcu, &psr_state);
		if (enable) {
			if (psr_state != 0)
				break;
		} else {
			if (psr_state == 0)
				break;
		}
		udelay(500);
	}

	/* assert if max retry hit */
	ASSERT(retryCount <= 1000);
	}
}

static void dcn10_dmcu_setup_psr(struct dmcu *dmcu,
		struct dc_link *link,
		struct psr_context *psr_context)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);

	unsigned int dmcu_max_retry_on_wait_reg_ready = 801;
	unsigned int dmcu_wait_reg_ready_interval = 100;

	union dce_dmcu_psr_config_data_reg1 masterCmdData1;
	union dce_dmcu_psr_config_data_reg2 masterCmdData2;
	union dce_dmcu_psr_config_data_reg3 masterCmdData3;

	link->link_enc->funcs->psr_program_dp_dphy_fast_training(link->link_enc,
			psr_context->psrExitLinkTrainingRequired);

	/* Enable static screen interrupts for PSR supported display */
	/* Disable the interrupt coming from other displays. */
	REG_UPDATE_4(DMCU_INTERRUPT_TO_UC_EN_MASK,
			STATIC_SCREEN1_INT_TO_UC_EN, 0,
			STATIC_SCREEN2_INT_TO_UC_EN, 0,
			STATIC_SCREEN3_INT_TO_UC_EN, 0,
			STATIC_SCREEN4_INT_TO_UC_EN, 0);

	switch (psr_context->controllerId) {
	/* Driver uses case 1 for unconfigured */
	case 1:
		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN1_INT_TO_UC_EN, 1);
		break;
	case 2:
		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN2_INT_TO_UC_EN, 1);
		break;
	case 3:
		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN3_INT_TO_UC_EN, 1);
		break;
	case 4:
		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN4_INT_TO_UC_EN, 1);
		break;
	case 5:
		/* CZ/NL only has 4 CRTC!!
		 * really valid.
		 * There is no interrupt enable mask for these instances.
		 */
		break;
	case 6:
		/* CZ/NL only has 4 CRTC!!
		 * These are here because they are defined in HW regspec,
		 * but not really valid. There is no interrupt enable mask
		 * for these instances.
		 */
		break;
	default:
		REG_UPDATE(DMCU_INTERRUPT_TO_UC_EN_MASK,
				STATIC_SCREEN1_INT_TO_UC_EN, 1);
		break;
	}

	link->link_enc->funcs->psr_program_secondary_packet(link->link_enc,
			psr_context->sdpTransmitLineNumDeadline);

	if (psr_context->psr_level.bits.SKIP_SMU_NOTIFICATION)
		REG_UPDATE(SMU_INTERRUPT_CONTROL, DC_SMU_INT_ENABLE, 1);

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0,
			dmcu_wait_reg_ready_interval,
			dmcu_max_retry_on_wait_reg_ready);

	/* setDMCUParam_PSRHostConfigData */
	masterCmdData1.u32All = 0;
	masterCmdData1.bits.timehyst_frames = psr_context->timehyst_frames;
	masterCmdData1.bits.hyst_lines = psr_context->hyst_lines;
	masterCmdData1.bits.rfb_update_auto_en =
			psr_context->rfb_update_auto_en;
	masterCmdData1.bits.dp_port_num = psr_context->transmitterId;
	masterCmdData1.bits.dcp_sel = psr_context->controllerId;
	masterCmdData1.bits.phy_type  = psr_context->phyType;
	masterCmdData1.bits.frame_cap_ind =
			psr_context->psrFrameCaptureIndicationReq;
	masterCmdData1.bits.aux_chan = psr_context->channel;
	masterCmdData1.bits.aux_repeat = psr_context->aux_repeats;
	dm_write_reg(dmcu->ctx, REG(MASTER_COMM_DATA_REG1),
					masterCmdData1.u32All);

	masterCmdData2.u32All = 0;
	masterCmdData2.bits.dig_fe = psr_context->engineId;
	masterCmdData2.bits.dig_be = psr_context->transmitterId;
	masterCmdData2.bits.skip_wait_for_pll_lock =
			psr_context->skipPsrWaitForPllLock;
	masterCmdData2.bits.frame_delay = psr_context->frame_delay;
	masterCmdData2.bits.smu_phy_id = psr_context->smuPhyId;
	masterCmdData2.bits.num_of_controllers =
			psr_context->numberOfControllers;
	dm_write_reg(dmcu->ctx, REG(MASTER_COMM_DATA_REG2),
			masterCmdData2.u32All);

	masterCmdData3.u32All = 0;
	masterCmdData3.bits.psr_level = psr_context->psr_level.u32all;
	dm_write_reg(dmcu->ctx, REG(MASTER_COMM_DATA_REG3),
			masterCmdData3.u32All);

	/* setDMCUParam_Cmd */
	REG_UPDATE(MASTER_COMM_CMD_REG,
			MASTER_COMM_CMD_REG_BYTE0, PSR_SET);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);
}

static void dcn10_psr_wait_loop(
	struct dmcu *dmcu,
	unsigned int wait_loop_number)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	union dce_dmcu_psr_config_data_wait_loop_reg1 masterCmdData1;
	if (wait_loop_number != 0) {
	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 1, 10000);

	masterCmdData1.u32 = 0;
	masterCmdData1.bits.wait_loop = wait_loop_number;
	cached_wait_loop_number = wait_loop_number;
	dm_write_reg(dmcu->ctx, REG(MASTER_COMM_DATA_REG1), masterCmdData1.u32);

	/* setDMCUParam_Cmd */
	REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0, PSR_SET_WAITLOOP);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);
	}
}

static void dcn10_get_psr_wait_loop(unsigned int *psr_wait_loop_number)
{
	*psr_wait_loop_number = cached_wait_loop_number;
	return;
}

#endif

static const struct dmcu_funcs dce_funcs = {
	.load_iram = dce_dmcu_load_iram,
	.set_psr_enable = dce_dmcu_set_psr_enable,
	.setup_psr = dce_dmcu_setup_psr,
	.get_psr_state = dce_get_dmcu_psr_state,
	.set_psr_wait_loop = dce_psr_wait_loop,
	.get_psr_wait_loop = dce_get_psr_wait_loop
};

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
static const struct dmcu_funcs dcn10_funcs = {
	.load_iram = dcn10_dmcu_load_iram,
	.set_psr_enable = dcn10_dmcu_set_psr_enable,
	.setup_psr = dcn10_dmcu_setup_psr,
	.get_psr_state = dcn10_get_dmcu_psr_state,
	.set_psr_wait_loop = dcn10_psr_wait_loop,
	.get_psr_wait_loop = dcn10_get_psr_wait_loop
};
#endif

static void dce_dmcu_construct(
	struct dce_dmcu *dmcu_dce,
	struct dc_context *ctx,
	const struct dce_dmcu_registers *regs,
	const struct dce_dmcu_shift *dmcu_shift,
	const struct dce_dmcu_mask *dmcu_mask)
{
	struct dmcu *base = &dmcu_dce->base;

	base->ctx = ctx;
	base->funcs = &dce_funcs;

	dmcu_dce->regs = regs;
	dmcu_dce->dmcu_shift = dmcu_shift;
	dmcu_dce->dmcu_mask = dmcu_mask;
}

struct dmcu *dce_dmcu_create(
	struct dc_context *ctx,
	const struct dce_dmcu_registers *regs,
	const struct dce_dmcu_shift *dmcu_shift,
	const struct dce_dmcu_mask *dmcu_mask)
{
	struct dce_dmcu *dmcu_dce = kzalloc(sizeof(*dmcu_dce), GFP_KERNEL);

	if (dmcu_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dce_dmcu_construct(
		dmcu_dce, ctx, regs, dmcu_shift, dmcu_mask);

	dmcu_dce->base.funcs = &dce_funcs;

	return &dmcu_dce->base;
}

#if defined(CONFIG_DRM_AMD_DC_DCN1_0)
struct dmcu *dcn10_dmcu_create(
	struct dc_context *ctx,
	const struct dce_dmcu_registers *regs,
	const struct dce_dmcu_shift *dmcu_shift,
	const struct dce_dmcu_mask *dmcu_mask)
{
	struct dce_dmcu *dmcu_dce = kzalloc(sizeof(*dmcu_dce), GFP_KERNEL);

	if (dmcu_dce == NULL) {
		BREAK_TO_DEBUGGER();
		return NULL;
	}

	dce_dmcu_construct(
		dmcu_dce, ctx, regs, dmcu_shift, dmcu_mask);

	dmcu_dce->base.funcs = &dcn10_funcs;

	return &dmcu_dce->base;
}
#endif

void dce_dmcu_destroy(struct dmcu **dmcu)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(*dmcu);

	kfree(dmcu_dce);
	*dmcu = NULL;
}
