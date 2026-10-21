// SPDX-License-Identifier: MIT
//
// Copyright 2025 Advanced Micro Devices, Inc.

#include "dcn60_clk_mgr_smu_msg.h"

#include "clk_mgr_internal.h"
#include "reg_helper.h"

#include "dalsmc.h"
#include "dcn401/dcn401_smu14_driver_if.h"

/* MMIO = MP1_BASE__INST0_SEG1 (0x16200 from at2_offset.h)
 *      + regMP1_SMN_C2PMSG_N  (sequential from 0xA2)
 * MSG=C2PMSG_98, RESP=C2PMSG_99, ARG0..3=C2PMSG_100..103
 */
#ifndef mmDAL_MSG_REG
#define mmDAL_MSG_REG    0x162A2
#endif
#ifndef mmDAL_RESP_REG
#define mmDAL_RESP_REG   0x162A3
#endif
#ifndef mmDAL_ARG_REG
#define mmDAL_ARG_REG    0x162A4
#endif
#ifndef mmDAL_ARG_REG_0
#define mmDAL_ARG_REG_0  0x162A4
#endif
#ifndef mmDAL_ARG_REG_1
#define mmDAL_ARG_REG_1  0x162A5
#endif
#ifndef mmDAL_ARG_REG_2
#define mmDAL_ARG_REG_2  0x162A6
#endif
#ifndef mmDAL_ARG_REG_3
#define mmDAL_ARG_REG_3  0x162A7
#endif

#define REG(reg_name) \
	mm ## reg_name

#include "logger_types.h"


#define smu_print(str, ...) {DC_LOG_SMU(str, ##__VA_ARGS__); }

/**
 * dcn60_smu_wait_for_response - Poll DAL_RESP_REG until non-zero.
 * @clk_mgr: clock manager instance
 * @delay_us: microseconds to wait between each poll iteration
 * @max_retries: maximum number of poll iterations before giving up
 * @total_delay_us: if non-NULL, accumulates the total poll delay
 *
 * Cannot use REG_WAIT because the condition is "not equal to zero"
 * and the translation in msg_if.h is incompatible with REG_WAIT.
 *
 * Return: RESP_REG value (non-zero on success, 0 on timeout)
 */
static uint32_t dcn60_smu_wait_for_response(struct clk_mgr_internal *clk_mgr,
		unsigned int delay_us, unsigned int max_retries,
		unsigned int *total_delay_us)
{
	uint32_t reg = 0;

	if (total_delay_us)
		*total_delay_us = 0;

	do {
		reg = REG_READ(DAL_RESP_REG);
		if (reg)
			break;

		if (delay_us >= 1000)
			msleep(delay_us/1000);
		else if (delay_us > 0)
			udelay(delay_us);
		if (total_delay_us)
			*total_delay_us += delay_us;
	} while (max_retries--);

	return reg;
}

/**
 * dcn60_smu_send_msg_with_args - Send a DALSMC message with four argument registers.
 * @clk_mgr: clock manager instance
 * @msg_id: DALSMC_MSG_* message identifier
 * @args: four-register argument payload
 * @param_out: if non-NULL, receives ARG_REG_0 on success
 * @total_delay_us: if non-NULL, accumulates the total poll delay
 */
static bool dcn60_smu_send_msg_with_args(struct clk_mgr_internal *clk_mgr,
		uint32_t msg_id, DALSMC_args_t args, uint32_t *param_out,
		unsigned int *total_delay_us)
{
	unsigned int delay1_us = 0, delay2_us = 0;

	if (total_delay_us)
		*total_delay_us = 0;

	/* Wait for response register to be ready */
	dcn60_smu_wait_for_response(clk_mgr, 10, 200000,
			total_delay_us ? &delay1_us : NULL);

	smu_print("SMU msg 0x%x enter: arg0=0x%08x arg1=0x%08x arg2=0x%08x arg3=0x%08x\n",
			msg_id, args.Reg0, args.Reg1, args.Reg2, args.Reg3);
	TRACE_SMU_MSG_ENTER(msg_id, args.Reg0, clk_mgr->base.ctx);

	/* Clear response register */
	REG_WRITE(DAL_RESP_REG, 0);

	/* Write all four argument registers */
	REG_WRITE(DAL_ARG_REG_0, args.Reg0);
	REG_WRITE(DAL_ARG_REG_1, args.Reg1);
	REG_WRITE(DAL_ARG_REG_2, args.Reg2);
	REG_WRITE(DAL_ARG_REG_3, args.Reg3);

	/* Trigger the message transaction by writing the message ID */
	REG_WRITE(DAL_MSG_REG, msg_id);

	/* Wait for response */
	if (dcn60_smu_wait_for_response(clk_mgr, 10, 200000,
			total_delay_us ? &delay2_us : NULL) == DALSMC_Result_OK) {
		if (param_out)
			*param_out = REG_READ(DAL_ARG_REG_0);

		if (total_delay_us)
			*total_delay_us = delay1_us + delay2_us;

		smu_print("SMU msg 0x%x exit: ok resp=0x%08x\n",
				msg_id, param_out ? *param_out : 0);
		TRACE_SMU_MSG_EXIT(true, param_out ? *param_out : 0, clk_mgr->base.ctx);
		return true;
	}

	if (total_delay_us)
		*total_delay_us = delay1_us + 2000000;

	smu_print("SMU msg 0x%x exit: failed\n", msg_id);
	TRACE_SMU_MSG_EXIT(false, 0, clk_mgr->base.ctx);
	return false;
}


static unsigned int dcn60_smu_get_hard_min_status(struct clk_mgr_internal *clk_mgr,
		bool *no_timeout, unsigned int *total_delay_us)
{
	DALSMC_args_t args = {};
	uint32_t response = 0;

	*no_timeout = dcn60_smu_send_msg_with_args(clk_mgr,
			DALSMC_MSG_ReturnHardMinStatus, args, &response,
			total_delay_us);

	smu_print("SMU Get hard min status: no_timeout %d delay %d us clk bits %x\n",
			*no_timeout, *total_delay_us, response);

	return response;
}


static bool dcn60_smu_wait_hard_min_status(struct clk_mgr_internal *clk_mgr, uint32_t ppclk)
{
	const unsigned int max_delay_us = 1000000;

	unsigned int hardmin_status_mask = (1 << ppclk);
	unsigned int total_delay_us = 0;
	bool hardmin_done = false;

	while (!hardmin_done && total_delay_us < max_delay_us) {
		unsigned int hardmin_status;
		unsigned int read_total_delay_us;
		bool no_timeout;

		if (!hardmin_done && total_delay_us > 0) {
			/* hardmin not yet fulfilled, wait 500us and retry*/
			udelay(500);
			total_delay_us += 500;

			smu_print("SMU Wait hard min status for %d us\n", total_delay_us);
		}

		hardmin_status = dcn60_smu_get_hard_min_status(clk_mgr, &no_timeout, &read_total_delay_us);
		total_delay_us += read_total_delay_us;
		hardmin_done = hardmin_status & hardmin_status_mask;
	}

	return hardmin_done;
}

/* TODO: update callers to pass kHz directly for finer granularity
 * now that the DALSMC interface supports 24-bit kHz encoding.
 */
unsigned int dcn60_smu_set_hard_min_by_freq(struct clk_mgr_internal *clk_mgr, uint32_t clk, uint16_t freq_mhz)
{
	DALSMC_SetHardMinByFreq_arg_t arg = {};
	uint32_t response = 0;
	bool hard_min_done = false;

	smu_print("SMU Set hard min by freq: clk = %d, freq_mhz = %d MHz\n", clk, freq_mhz);

	/* New interface encodes frequency in kHz (24-bit) and PPCLK in bits [31:24] */
	arg.FreqKhz = (uint32_t)freq_mhz * 1000;
	arg.Ppclk   = clk;

	dcn60_smu_send_msg_with_args(clk_mgr,
			DALSMC_MSG_SetHardMinByFreq, arg.Args, &response, NULL);

	/* wait until hardmin acknowledged */
	hard_min_done = dcn60_smu_wait_hard_min_status(clk_mgr, clk);
	smu_print("SMU Frequency set = %d KHz hard_min_done %d\n", response, hard_min_done);

	return response;
}


void dcn60_smu_set_stutter_efficiency(struct clk_mgr_internal *clk_mgr, uint8_t base_efficiency, uint8_t low_power_efficiency)
{
	DALSMC_SetStutterEfficiency_arg_t arg = {};

	smu_print("SMU Set stutter efficiencies: base(LP1) = %d percent, low power(LP2) = %d percent\n",
			base_efficiency, low_power_efficiency);

	arg.BaseEfficiencyPct     = base_efficiency;
	arg.LowPowerEfficiencyPct = low_power_efficiency;

	dcn60_smu_send_msg_with_args(clk_mgr,
			DALSMC_MSG_SetStutterEfficiency, arg.Args, NULL, NULL);
}

void dcn60_smu_set_min_deep_sleep_dcfclk(struct clk_mgr_internal *clk_mgr, uint32_t freq_mhz)
{
	DALSMC_SetMinDeepSleepDcfclk_arg_t arg = {};

	smu_print("SMU Set min deep sleep dcfclk: freq_mhz = %d MHz\n", freq_mhz);

	arg.MinDcfclkMhz = freq_mhz;

	dcn60_smu_send_msg_with_args(clk_mgr,
			DALSMC_MSG_SetMinDeepSleepDcfclk, arg.Args, NULL, NULL);
}

void dcn60_smu_set_pme_workaround(struct clk_mgr_internal *clk_mgr)
{
	DALSMC_args_t args = {};

	smu_print("SMU Set PME workaround (BacoAudioD3PME)\n");

	dcn60_smu_send_msg_with_args(clk_mgr,
			DALSMC_MSG_BacoAudioD3PME, args, NULL, NULL);
}

void dcn60_smu_indicate_pstate_status(struct clk_mgr_internal *clk_mgr,
		bool allow_fclk, bool allow_uclk,
		bool wait_resp, bool drr_enable, bool alt_ch_enable)
{
	DALSMC_IndicatePstateStatus_arg_t arg = {};

	smu_print("SMU Indicate pstate status: allow_fclk=%d allow_uclk=%d wait_resp=%d drr_enable=%d alt_ch_enable=%d\n",
			allow_fclk, allow_uclk, wait_resp, drr_enable, alt_ch_enable);

	arg.AllowFclk = allow_fclk ? 1 : 0;
	arg.AllowUclk = allow_uclk ? 1 : 0;
	arg.WaitResp  = wait_resp ? 1 : 0;
	arg.DrrEnable = drr_enable ? 1 : 0;
	arg.AltCh = alt_ch_enable ? 1 : 0;

	dcn60_smu_send_msg_with_args(clk_mgr,
			DALSMC_MSG_IndicatePstateStatus, arg.Args, NULL, NULL);
}

static bool dcn60_smu_transfer_table_smu_2_dram(struct clk_mgr_internal *clk_mgr,
		uint32_t table_id, long long dram_addr)
{
	DALSMC_TransferTable_arg_t arg = {};

	smu_print("SMU TransferTableSmu2Dram: table_id=0x%x addr=0x%08x_%08x\n",
			table_id,
			(uint32_t)(dram_addr >> 32),
			(uint32_t)(dram_addr & 0xFFFFFFFF));

	arg.TableId  = table_id;
	arg.AddrLow  = (uint32_t)(dram_addr & 0xFFFFFFFF);
	arg.AddrHigh = (uint32_t)(dram_addr >> 32);

	return dcn60_smu_send_msg_with_args(clk_mgr,
			DALSMC_MSG_TransferTableSmu2Dram, arg.Args, NULL, NULL);
}

static bool dcn60_smu_transfer_table_dram_2_smu(struct clk_mgr_internal *clk_mgr,
		uint32_t table_id, long long dram_addr)
{
	DALSMC_TransferTable_arg_t arg = {};

	smu_print("SMU TransferTableDram2Smu: table_id=0x%x addr=0x%08x_%08x\n",
			table_id,
			(uint32_t)(dram_addr >> 32),
			(uint32_t)(dram_addr & 0xFFFFFFFF));

	arg.TableId  = table_id;
	arg.AddrLow  = (uint32_t)(dram_addr & 0xFFFFFFFF);
	arg.AddrHigh = (uint32_t)(dram_addr >> 32);

	return dcn60_smu_send_msg_with_args(clk_mgr,
			DALSMC_MSG_TransferTableDram2Smu, arg.Args, NULL, NULL);
}

bool dcn60_smu_set_soc_utm_table(struct clk_mgr_internal *clk_mgr,
		long long dram_addr)
{
	return dcn60_smu_transfer_table_dram_2_smu(clk_mgr,
			TABLE_SOC_UTM, dram_addr);
}

bool dcn60_smu_get_dal_init_table(struct clk_mgr_internal *clk_mgr,
		const DalInitTable_t **init_table)
{
	if (!dcn60_smu_transfer_table_smu_2_dram(clk_mgr,
			TABLE_DAL_INIT, clk_mgr->dal_init_table_addr))
		return false;

	*init_table = (const DalInitTable_t *)clk_mgr->dal_init_table;
	return true;
}

bool dcn60_smu_update_utm_qos_request(struct clk_mgr_internal *clk_mgr,
		uint32_t latency_sop_index,
		uint32_t nominal_bandwidth_KBps,
		uint32_t urgent_bandwidth_KBps,
		uint32_t lsdma_bandwidth_KBps)
{
	DALSMC_UpdateUTMQoSRequest_arg_t arg = {};

	smu_print("SMU UpdateUTMQoSRequest: sop_idx=%d nominal=%d urgent=%d lsdma=%d KBps\n",
			latency_sop_index, nominal_bandwidth_KBps,
			urgent_bandwidth_KBps, lsdma_bandwidth_KBps);

	arg.LatencySopIndex      = latency_sop_index;
	arg.NominalBandwidthKBps = nominal_bandwidth_KBps;
	arg.UrgentBandwidthKBps  = urgent_bandwidth_KBps;
	arg.LsdmaBandwidthKBps   = lsdma_bandwidth_KBps;

	return dcn60_smu_send_msg_with_args(clk_mgr,
			DALSMC_MSG_UpdateUTMQoSRequest, arg.Args, NULL, NULL);
}

bool dcn60_smu_get_msg_header_version(struct clk_mgr_internal *clk_mgr,
		uint32_t *version)
{
	DALSMC_args_t args = {};

	return dcn60_smu_send_msg_with_args(clk_mgr,
			DALSMC_MSG_GetMsgHeaderVersion, args, version, NULL);
}

void dcn60_smu_set_display_idle_optimization(struct clk_mgr_internal *clk_mgr, bool is_idle)
{
	DALSMC_SetDisplayIdleOptimizations_arg_t arg = {};

	arg.DfRequestDisabled = is_idle ? 1 : 0;
	arg.PhyRefClkOff      = is_idle ? 1 : 0;
	arg.S0i2Rdy           = is_idle ? 1 : 0;

	smu_print("SMU SetDisplayIdleOptimizations: DfRequestDisabled=%d PhyRefClkOff=%d S0i2Rdy=%d\n",
		  arg.DfRequestDisabled, arg.PhyRefClkOff, arg.S0i2Rdy);

	dcn60_smu_send_msg_with_args(clk_mgr,
			DALSMC_MSG_SetDisplayIdleOptimizations, arg.Args, NULL, NULL);
}
