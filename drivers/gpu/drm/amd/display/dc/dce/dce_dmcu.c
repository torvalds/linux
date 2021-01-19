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

#include <linux/delay.h>
#include <linux/slab.h>

#include "core_types.h"
#include "link_encoder.h"
#include "dce_dmcu.h"
#include "dm_services.h"
#include "reg_helper.h"
#include "fixed31_32.h"
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
#define MCP_INIT_DMCU 0x88
#define MCP_INIT_IRAM 0x89
#define MCP_SYNC_PHY_LOCK 0x90
#define MCP_SYNC_PHY_UNLOCK 0x91
#define MCP_BL_SET_PWM_FRAC 0x6A  /* Enable or disable Fractional PWM */
#define CRC_WIN_NOTIFY 0x92
#define CRC_STOP_UPDATE 0x93
#define MCP_SEND_EDID_CEA 0xA0
#define EDID_CEA_CMD_ACK 1
#define EDID_CEA_CMD_NACK 2
#define MASTER_COMM_CNTL_REG__MASTER_COMM_INTERRUPT_MASK   0x00000001L

// PSP FW version
#define mmMP0_SMN_C2PMSG_58				0x1607A

//Register access policy version
#define mmMP0_SMN_C2PMSG_91				0x1609B

#if defined(CONFIG_DRM_AMD_DC_DCN)
static const uint32_t abm_gain_stepsize = 0x0060;
#endif

static bool dce_dmcu_init(struct dmcu *dmcu)
{
	// Do nothing
	return true;
}

static bool dce_dmcu_load_iram(struct dmcu *dmcu,
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

static void dce_get_dmcu_psr_state(struct dmcu *dmcu, enum dc_psr_state *state)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);

	uint32_t psr_state_offset = 0xf0;

	/* Enable write access to IRAM */
	REG_UPDATE(DMCU_RAM_ACCESS_CTRL, IRAM_HOST_ACCESS_EN, 1);

	REG_WAIT(DCI_MEM_PWR_STATUS, DMCU_IRAM_MEM_PWR_STATE, 0, 2, 10);

	/* Write address to IRAM_RD_ADDR in DMCU_IRAM_RD_CTRL */
	REG_WRITE(DMCU_IRAM_RD_CTRL, psr_state_offset);

	/* Read data from IRAM_RD_DATA in DMCU_IRAM_RD_DATA*/
	*state = (enum dc_psr_state)REG_READ(DMCU_IRAM_RD_DATA);

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
	enum dc_psr_state state = PSR_STATE0;

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
			dce_get_dmcu_psr_state(dmcu, &state);
			if (enable) {
				if (state != PSR_STATE0)
					break;
			} else {
				if (state == PSR_STATE0)
					break;
			}
			udelay(10);
		}
	}
}

static bool dce_dmcu_setup_psr(struct dmcu *dmcu,
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

	return true;
}

static bool dce_is_dmcu_initialized(struct dmcu *dmcu)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	unsigned int dmcu_uc_reset;

	/* microcontroller is not running */
	REG_GET(DMCU_STATUS, UC_IN_RESET, &dmcu_uc_reset);

	/* DMCU is not running */
	if (dmcu_uc_reset)
		return false;

	return true;
}

static void dce_psr_wait_loop(
	struct dmcu *dmcu,
	unsigned int wait_loop_number)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	union dce_dmcu_psr_config_data_wait_loop_reg1 masterCmdData1;

	if (dmcu->cached_wait_loop_number == wait_loop_number)
		return;

	/* DMCU is not running */
	if (!dce_is_dmcu_initialized(dmcu))
		return;

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 1, 10000);

	masterCmdData1.u32 = 0;
	masterCmdData1.bits.wait_loop = wait_loop_number;
	dmcu->cached_wait_loop_number = wait_loop_number;
	dm_write_reg(dmcu->ctx, REG(MASTER_COMM_DATA_REG1), masterCmdData1.u32);

	/* setDMCUParam_Cmd */
	REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0, PSR_SET_WAITLOOP);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);
}

static void dce_get_psr_wait_loop(
		struct dmcu *dmcu, unsigned int *psr_wait_loop_number)
{
	*psr_wait_loop_number = dmcu->cached_wait_loop_number;
	return;
}

#if defined(CONFIG_DRM_AMD_DC_DCN)
static void dcn10_get_dmcu_version(struct dmcu *dmcu)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	uint32_t dmcu_version_offset = 0xf1;

	/* Enable write access to IRAM */
	REG_UPDATE_2(DMCU_RAM_ACCESS_CTRL,
			IRAM_HOST_ACCESS_EN, 1,
			IRAM_RD_ADDR_AUTO_INC, 1);

	REG_WAIT(DMU_MEM_PWR_CNTL, DMCU_IRAM_MEM_PWR_STATE, 0, 2, 10);

	/* Write address to IRAM_RD_ADDR and read from DATA register */
	REG_WRITE(DMCU_IRAM_RD_CTRL, dmcu_version_offset);
	dmcu->dmcu_version.interface_version = REG_READ(DMCU_IRAM_RD_DATA);
	dmcu->dmcu_version.abm_version = REG_READ(DMCU_IRAM_RD_DATA);
	dmcu->dmcu_version.psr_version = REG_READ(DMCU_IRAM_RD_DATA);
	dmcu->dmcu_version.build_version = ((REG_READ(DMCU_IRAM_RD_DATA) << 8) |
						REG_READ(DMCU_IRAM_RD_DATA));

	/* Disable write access to IRAM to allow dynamic sleep state */
	REG_UPDATE_2(DMCU_RAM_ACCESS_CTRL,
			IRAM_HOST_ACCESS_EN, 0,
			IRAM_RD_ADDR_AUTO_INC, 0);
}

static void dcn10_dmcu_enable_fractional_pwm(struct dmcu *dmcu,
		uint32_t fractional_pwm)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);

	/* Wait until microcontroller is ready to process interrupt */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 100, 800);

	/* Set PWM fractional enable/disable */
	REG_WRITE(MASTER_COMM_DATA_REG1, fractional_pwm);

	/* Set command to enable or disable fractional PWM microcontroller */
	REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0,
			MCP_BL_SET_PWM_FRAC);

	/* Notify microcontroller of new command */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);

	/* Ensure command has been executed before continuing */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 100, 800);
}

static bool dcn10_dmcu_init(struct dmcu *dmcu)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	const struct dc_config *config = &dmcu->ctx->dc->config;
	bool status = false;
	struct dc_context *ctx = dmcu->ctx;
	unsigned int i;
	//  5 4 3 2 1 0
	//  F E D C B A - bit 0 is A, bit 5 is F
	unsigned int tx_interrupt_mask = 0;

	PERF_TRACE();
	/*  Definition of DC_DMCU_SCRATCH
	 *  0 : firmare not loaded
	 *  1 : PSP load DMCU FW but not initialized
	 *  2 : Firmware already initialized
	 */
	dmcu->dmcu_state = REG_READ(DC_DMCU_SCRATCH);

	for (i = 0; i < ctx->dc->link_count; i++) {
		if (ctx->dc->links[i]->link_enc->features.flags.bits.DP_IS_USB_C) {
			if (ctx->dc->links[i]->link_enc->transmitter >= TRANSMITTER_UNIPHY_A &&
					ctx->dc->links[i]->link_enc->transmitter <= TRANSMITTER_UNIPHY_F) {
				tx_interrupt_mask |= 1 << ctx->dc->links[i]->link_enc->transmitter;
			}
		}
	}

	switch (dmcu->dmcu_state) {
	case DMCU_UNLOADED:
		status = false;
		break;
	case DMCU_LOADED_UNINITIALIZED:
		/* Wait until microcontroller is ready to process interrupt */
		REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 100, 800);

		/* Set initialized ramping boundary value */
		REG_WRITE(MASTER_COMM_DATA_REG1, 0xFFFF);

		/* Set backlight ramping stepsize */
		REG_WRITE(MASTER_COMM_DATA_REG2, abm_gain_stepsize);

		REG_WRITE(MASTER_COMM_DATA_REG3, tx_interrupt_mask);

		/* Set command to initialize microcontroller */
		REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0,
			MCP_INIT_DMCU);

		/* Notify microcontroller of new command */
		REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);

		/* Ensure command has been executed before continuing */
		REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 100, 800);

		// Check state is initialized
		dmcu->dmcu_state = REG_READ(DC_DMCU_SCRATCH);

		// If microcontroller is not in running state, fail
		if (dmcu->dmcu_state == DMCU_RUNNING) {
			/* Retrieve and cache the DMCU firmware version. */
			dcn10_get_dmcu_version(dmcu);

			/* Initialize DMCU to use fractional PWM or not */
			dcn10_dmcu_enable_fractional_pwm(dmcu,
				(config->disable_fractional_pwm == false) ? 1 : 0);
			status = true;
		} else {
			status = false;
		}

		break;
	case DMCU_RUNNING:
		status = true;
		break;
	default:
		status = false;
		break;
	}

	PERF_TRACE();
	return status;
}

static bool dcn21_dmcu_init(struct dmcu *dmcu)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	uint32_t dmcub_psp_version = REG_READ(DMCUB_SCRATCH15);

	if (dmcu->auto_load_dmcu && dmcub_psp_version == 0) {
		return false;
	}

	return dcn10_dmcu_init(dmcu);
}

static bool dcn10_dmcu_load_iram(struct dmcu *dmcu,
		unsigned int start_offset,
		const char *src,
		unsigned int bytes)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	unsigned int count = 0;

	/* If microcontroller is not running, do nothing */
	if (dmcu->dmcu_state != DMCU_RUNNING)
		return false;

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

	/* Wait until microcontroller is ready to process interrupt */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 100, 800);

	/* Set command to signal IRAM is loaded and to initialize IRAM */
	REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0,
			MCP_INIT_IRAM);

	/* Notify microcontroller of new command */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);

	/* Ensure command has been executed before continuing */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 100, 800);

	return true;
}

static void dcn10_get_dmcu_psr_state(struct dmcu *dmcu, enum dc_psr_state *state)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);

	uint32_t psr_state_offset = 0xf0;

	/* If microcontroller is not running, do nothing */
	if (dmcu->dmcu_state != DMCU_RUNNING)
		return;

	/* Enable write access to IRAM */
	REG_UPDATE(DMCU_RAM_ACCESS_CTRL, IRAM_HOST_ACCESS_EN, 1);

	REG_WAIT(DMU_MEM_PWR_CNTL, DMCU_IRAM_MEM_PWR_STATE, 0, 2, 10);

	/* Write address to IRAM_RD_ADDR in DMCU_IRAM_RD_CTRL */
	REG_WRITE(DMCU_IRAM_RD_CTRL, psr_state_offset);

	/* Read data from IRAM_RD_DATA in DMCU_IRAM_RD_DATA*/
	*state = (enum dc_psr_state)REG_READ(DMCU_IRAM_RD_DATA);

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
	enum dc_psr_state state = PSR_STATE0;

	/* If microcontroller is not running, do nothing */
	if (dmcu->dmcu_state != DMCU_RUNNING)
		return;

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
			dcn10_get_dmcu_psr_state(dmcu, &state);
			if (enable) {
				if (state != PSR_STATE0)
					break;
			} else {
				if (state == PSR_STATE0)
					break;
			}
			udelay(500);
		}

		/* assert if max retry hit */
		if (retryCount >= 1000)
			ASSERT(0);
	}
}

static bool dcn10_dmcu_setup_psr(struct dmcu *dmcu,
		struct dc_link *link,
		struct psr_context *psr_context)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);

	unsigned int dmcu_max_retry_on_wait_reg_ready = 801;
	unsigned int dmcu_wait_reg_ready_interval = 100;

	union dce_dmcu_psr_config_data_reg1 masterCmdData1;
	union dce_dmcu_psr_config_data_reg2 masterCmdData2;
	union dce_dmcu_psr_config_data_reg3 masterCmdData3;

	/* If microcontroller is not running, do nothing */
	if (dmcu->dmcu_state != DMCU_RUNNING)
		return false;

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

	if (psr_context->allow_smu_optimizations)
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
	masterCmdData1.bits.allow_smu_optimizations = psr_context->allow_smu_optimizations;
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

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 1, 10000);

	return true;
}

static void dcn10_psr_wait_loop(
	struct dmcu *dmcu,
	unsigned int wait_loop_number)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	union dce_dmcu_psr_config_data_wait_loop_reg1 masterCmdData1;

	/* If microcontroller is not running, do nothing */
	if (dmcu->dmcu_state != DMCU_RUNNING)
		return;

	if (wait_loop_number != 0) {
	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 1, 10000);

	masterCmdData1.u32 = 0;
	masterCmdData1.bits.wait_loop = wait_loop_number;
	dmcu->cached_wait_loop_number = wait_loop_number;
	dm_write_reg(dmcu->ctx, REG(MASTER_COMM_DATA_REG1), masterCmdData1.u32);

	/* setDMCUParam_Cmd */
	REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0, PSR_SET_WAITLOOP);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);
	}
}

static void dcn10_get_psr_wait_loop(
		struct dmcu *dmcu, unsigned int *psr_wait_loop_number)
{
	*psr_wait_loop_number = dmcu->cached_wait_loop_number;
	return;
}

static bool dcn10_is_dmcu_initialized(struct dmcu *dmcu)
{
	/* microcontroller is not running */
	if (dmcu->dmcu_state != DMCU_RUNNING)
		return false;
	return true;
}



static bool dcn20_lock_phy(struct dmcu *dmcu)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);

	/* If microcontroller is not running, do nothing */
	if (dmcu->dmcu_state != DMCU_RUNNING)
		return false;

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 1, 10000);

	/* setDMCUParam_Cmd */
	REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0, MCP_SYNC_PHY_LOCK);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 1, 10000);

	return true;
}

static bool dcn20_unlock_phy(struct dmcu *dmcu)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);

	/* If microcontroller is not running, do nothing */
	if (dmcu->dmcu_state != DMCU_RUNNING)
		return false;

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 1, 10000);

	/* setDMCUParam_Cmd */
	REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0, MCP_SYNC_PHY_UNLOCK);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 1, 10000);

	return true;
}

static bool dcn10_send_edid_cea(struct dmcu *dmcu,
		int offset,
		int total_length,
		uint8_t *data,
		int length)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	uint32_t header, data1, data2;

	/* If microcontroller is not running, do nothing */
	if (dmcu->dmcu_state != DMCU_RUNNING)
		return false;

	if (length > 8 || length <= 0)
		return false;

	header = ((uint32_t)offset & 0xFFFF) << 16 | (total_length & 0xFFFF);
	data1 = (((uint32_t)data[0]) << 24) | (((uint32_t)data[1]) << 16) |
		(((uint32_t)data[2]) << 8) | ((uint32_t)data[3]);
	data2 = (((uint32_t)data[4]) << 24) | (((uint32_t)data[5]) << 16) |
		(((uint32_t)data[6]) << 8) | ((uint32_t)data[7]);

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 1, 10000);

	/* setDMCUParam_Cmd */
	REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0, MCP_SEND_EDID_CEA);

	REG_WRITE(MASTER_COMM_DATA_REG1, header);
	REG_WRITE(MASTER_COMM_DATA_REG2, data1);
	REG_WRITE(MASTER_COMM_DATA_REG3, data2);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0, 1, 10000);

	return true;
}

static bool dcn10_get_scp_results(struct dmcu *dmcu,
		uint32_t *cmd,
		uint32_t *data1,
		uint32_t *data2,
		uint32_t *data3)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);

	/* If microcontroller is not running, do nothing */
	if (dmcu->dmcu_state != DMCU_RUNNING)
		return false;

	*cmd = REG_READ(SLAVE_COMM_CMD_REG);
	*data1 =  REG_READ(SLAVE_COMM_DATA_REG1);
	*data2 =  REG_READ(SLAVE_COMM_DATA_REG2);
	*data3 =  REG_READ(SLAVE_COMM_DATA_REG3);

	/* clear SCP interrupt */
	REG_UPDATE(SLAVE_COMM_CNTL_REG, SLAVE_COMM_INTERRUPT, 0);

	return true;
}

static bool dcn10_recv_amd_vsdb(struct dmcu *dmcu,
		int *version,
		int *min_frame_rate,
		int *max_frame_rate)
{
	uint32_t data[4];
	int cmd, ack, len;

	if (!dcn10_get_scp_results(dmcu, &data[0], &data[1], &data[2], &data[3]))
		return false;

	cmd = data[0] & 0x3FF;
	len = (data[0] >> 10) & 0x3F;
	ack = data[1];

	if (cmd != MCP_SEND_EDID_CEA || ack != EDID_CEA_CMD_ACK || len != 12)
		return false;

	if ((data[2] & 0xFF)) {
		*version = (data[2] >> 8) & 0xFF;
		*min_frame_rate = (data[3] >> 16) & 0xFFFF;
		*max_frame_rate = data[3] & 0xFFFF;
		return true;
	}

	return false;
}

static bool dcn10_recv_edid_cea_ack(struct dmcu *dmcu, int *offset)
{
	uint32_t data[4];
	int cmd, ack;

	if (!dcn10_get_scp_results(dmcu,
				&data[0], &data[1], &data[2], &data[3]))
		return false;

	cmd = data[0] & 0x3FF;
	ack = data[1];

	if (cmd != MCP_SEND_EDID_CEA)
		return false;

	if (ack == EDID_CEA_CMD_ACK)
		return true;

	*offset = data[2]; /* nack */
	return false;
}

#endif //(CONFIG_DRM_AMD_DC_DCN)

#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
static void dcn10_forward_crc_window(struct dmcu *dmcu,
					struct crc_region *crc_win,
					struct otg_phy_mux *mux_mapping)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	unsigned int dmcu_max_retry_on_wait_reg_ready = 801;
	unsigned int dmcu_wait_reg_ready_interval = 100;
	unsigned int crc_start = 0, crc_end = 0, otg_phy_mux = 0;

	/* If microcontroller is not running, do nothing */
	if (dmcu->dmcu_state != DMCU_RUNNING)
		return;

	if (!crc_win)
		return;

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0,
				dmcu_wait_reg_ready_interval,
				dmcu_max_retry_on_wait_reg_ready);

	/* build up nitification data */
	crc_start = (((unsigned int) crc_win->x_start) << 16) | crc_win->y_start;
	crc_end = (((unsigned int) crc_win->x_end) << 16) | crc_win->y_end;
	otg_phy_mux =
		(((unsigned int) mux_mapping->otg_output_num) << 16) | mux_mapping->phy_output_num;

	dm_write_reg(dmcu->ctx, REG(MASTER_COMM_DATA_REG1),
					crc_start);

	dm_write_reg(dmcu->ctx, REG(MASTER_COMM_DATA_REG2),
			crc_end);

	dm_write_reg(dmcu->ctx, REG(MASTER_COMM_DATA_REG3),
			otg_phy_mux);

	/* setDMCUParam_Cmd */
	REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0,
				CRC_WIN_NOTIFY);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);
}

static void dcn10_stop_crc_win_update(struct dmcu *dmcu,
					struct otg_phy_mux *mux_mapping)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(dmcu);
	unsigned int dmcu_max_retry_on_wait_reg_ready = 801;
	unsigned int dmcu_wait_reg_ready_interval = 100;
	unsigned int otg_phy_mux = 0;

	/* If microcontroller is not running, do nothing */
	if (dmcu->dmcu_state != DMCU_RUNNING)
		return;

	/* waitDMCUReadyForCmd */
	REG_WAIT(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 0,
				dmcu_wait_reg_ready_interval,
				dmcu_max_retry_on_wait_reg_ready);

	/* build up nitification data */
	otg_phy_mux =
		(((unsigned int) mux_mapping->otg_output_num) << 16) | mux_mapping->phy_output_num;

	dm_write_reg(dmcu->ctx, REG(MASTER_COMM_DATA_REG1),
					otg_phy_mux);

	/* setDMCUParam_Cmd */
	REG_UPDATE(MASTER_COMM_CMD_REG, MASTER_COMM_CMD_REG_BYTE0,
				CRC_STOP_UPDATE);

	/* notifyDMCUMsg */
	REG_UPDATE(MASTER_COMM_CNTL_REG, MASTER_COMM_INTERRUPT, 1);
}
#endif

static const struct dmcu_funcs dce_funcs = {
	.dmcu_init = dce_dmcu_init,
	.load_iram = dce_dmcu_load_iram,
	.set_psr_enable = dce_dmcu_set_psr_enable,
	.setup_psr = dce_dmcu_setup_psr,
	.get_psr_state = dce_get_dmcu_psr_state,
	.set_psr_wait_loop = dce_psr_wait_loop,
	.get_psr_wait_loop = dce_get_psr_wait_loop,
	.is_dmcu_initialized = dce_is_dmcu_initialized
};

#if defined(CONFIG_DRM_AMD_DC_DCN)
static const struct dmcu_funcs dcn10_funcs = {
	.dmcu_init = dcn10_dmcu_init,
	.load_iram = dcn10_dmcu_load_iram,
	.set_psr_enable = dcn10_dmcu_set_psr_enable,
	.setup_psr = dcn10_dmcu_setup_psr,
	.get_psr_state = dcn10_get_dmcu_psr_state,
	.set_psr_wait_loop = dcn10_psr_wait_loop,
	.get_psr_wait_loop = dcn10_get_psr_wait_loop,
	.send_edid_cea = dcn10_send_edid_cea,
	.recv_amd_vsdb = dcn10_recv_amd_vsdb,
	.recv_edid_cea_ack = dcn10_recv_edid_cea_ack,
#if defined(CONFIG_DRM_AMD_SECURE_DISPLAY)
	.forward_crc_window = dcn10_forward_crc_window,
	.stop_crc_win_update = dcn10_stop_crc_win_update,
#endif
	.is_dmcu_initialized = dcn10_is_dmcu_initialized
};

static const struct dmcu_funcs dcn20_funcs = {
	.dmcu_init = dcn10_dmcu_init,
	.load_iram = dcn10_dmcu_load_iram,
	.set_psr_enable = dcn10_dmcu_set_psr_enable,
	.setup_psr = dcn10_dmcu_setup_psr,
	.get_psr_state = dcn10_get_dmcu_psr_state,
	.set_psr_wait_loop = dcn10_psr_wait_loop,
	.get_psr_wait_loop = dcn10_get_psr_wait_loop,
	.is_dmcu_initialized = dcn10_is_dmcu_initialized,
	.lock_phy = dcn20_lock_phy,
	.unlock_phy = dcn20_unlock_phy
};

static const struct dmcu_funcs dcn21_funcs = {
	.dmcu_init = dcn21_dmcu_init,
	.load_iram = dcn10_dmcu_load_iram,
	.set_psr_enable = dcn10_dmcu_set_psr_enable,
	.setup_psr = dcn10_dmcu_setup_psr,
	.get_psr_state = dcn10_get_dmcu_psr_state,
	.set_psr_wait_loop = dcn10_psr_wait_loop,
	.get_psr_wait_loop = dcn10_get_psr_wait_loop,
	.is_dmcu_initialized = dcn10_is_dmcu_initialized,
	.lock_phy = dcn20_lock_phy,
	.unlock_phy = dcn20_unlock_phy
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
	base->cached_wait_loop_number = 0;

	dmcu_dce->regs = regs;
	dmcu_dce->dmcu_shift = dmcu_shift;
	dmcu_dce->dmcu_mask = dmcu_mask;
}

#if defined(CONFIG_DRM_AMD_DC_DCN)
static void dcn21_dmcu_construct(
		struct dce_dmcu *dmcu_dce,
		struct dc_context *ctx,
		const struct dce_dmcu_registers *regs,
		const struct dce_dmcu_shift *dmcu_shift,
		const struct dce_dmcu_mask *dmcu_mask)
{
	uint32_t psp_version = 0;

	dce_dmcu_construct(dmcu_dce, ctx, regs, dmcu_shift, dmcu_mask);

	if (!IS_FPGA_MAXIMUS_DC(ctx->dce_environment)) {
		psp_version = dm_read_reg(ctx, mmMP0_SMN_C2PMSG_58);
		dmcu_dce->base.auto_load_dmcu = ((psp_version & 0x00FF00FF) > 0x00110029);
		dmcu_dce->base.psp_version = psp_version;
	}
}
#endif

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

#if defined(CONFIG_DRM_AMD_DC_DCN)
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

struct dmcu *dcn20_dmcu_create(
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

	dmcu_dce->base.funcs = &dcn20_funcs;

	return &dmcu_dce->base;
}

struct dmcu *dcn21_dmcu_create(
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

	dcn21_dmcu_construct(
		dmcu_dce, ctx, regs, dmcu_shift, dmcu_mask);

	dmcu_dce->base.funcs = &dcn21_funcs;

	return &dmcu_dce->base;
}
#endif

void dce_dmcu_destroy(struct dmcu **dmcu)
{
	struct dce_dmcu *dmcu_dce = TO_DCE_DMCU(*dmcu);

	kfree(dmcu_dce);
	*dmcu = NULL;
}
