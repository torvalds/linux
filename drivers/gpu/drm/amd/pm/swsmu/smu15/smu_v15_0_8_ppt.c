/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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

#define SWSMU_CODE_LAYER_L2

#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "smu_v15_0_8_pmfw.h"
#include "smu15_driver_if_v15_0_8.h"
#include "smu_v15_0_8_ppsmc.h"
#include "smu_v15_0_8_ppt.h"
#include <linux/pci.h>
#include "smu_cmn.h"
#include "mp/mp_15_0_8_offset.h"
#include "mp/mp_15_0_8_sh_mask.h"
#include "smu_v15_0.h"

#undef MP1_Public

/* address block */
#define MP1_Public 			0x03b00000
#define smnMP1_FIRMWARE_FLAGS_15_0_8 	0x3010024
/*
 * DO NOT use these for err/warn/info/debug messages.
 * Use dev_err, dev_warn, dev_info and dev_dbg instead.
 * They are more MGPU friendly.
 */
#undef pr_err
#undef pr_warn
#undef pr_info
#undef pr_debug

#define to_amdgpu_device(x) (container_of(x, struct amdgpu_device, pm.smu_i2c))

#define SMU_15_0_8_FEA_MAP(smu_feature, smu_15_0_8_feature)                    \
	[smu_feature] = { 1, (smu_15_0_8_feature) }

#define FEATURE_MASK(feature) (1ULL << feature)

static const struct smu_feature_bits smu_v15_0_8_dpm_features = {
	.bits = { SMU_FEATURE_BIT_INIT(FEATURE_ID_DATA_CALCULATION),
		  SMU_FEATURE_BIT_INIT(FEATURE_ID_DPM_GFXCLK),
		  SMU_FEATURE_BIT_INIT(FEATURE_ID_DPM_UCLK),
		  SMU_FEATURE_BIT_INIT(FEATURE_ID_DPM_FCLK),
		  SMU_FEATURE_BIT_INIT(FEATURE_ID_DPM_GL2CLK) }
};

static const struct cmn2asic_msg_mapping smu_v15_0_8_message_map[SMU_MSG_MAX_COUNT] = {
	MSG_MAP(TestMessage,			     PPSMC_MSG_TestMessage,			0),
	MSG_MAP(GetSmuVersion,			     PPSMC_MSG_GetSmuVersion,			1),
	MSG_MAP(GfxDeviceDriverReset,		     PPSMC_MSG_GfxDriverReset,			SMU_MSG_RAS_PRI | SMU_MSG_NO_PRECHECK),
	MSG_MAP(GetDriverIfVersion,		     PPSMC_MSG_GetDriverIfVersion,		1),
	MSG_MAP(EnableAllSmuFeatures,		     PPSMC_MSG_EnableAllSmuFeatures,		0),
	MSG_MAP(GetMetricsVersion,		     PPSMC_MSG_GetMetricsVersion,		1),
	MSG_MAP(GetMetricsTable,		     PPSMC_MSG_GetMetricsTable,			1),
	MSG_MAP(GetEnabledSmuFeatures,	     	     PPSMC_MSG_GetEnabledSmuFeatures,		1),
	MSG_MAP(SetDriverDramAddrHigh,		     PPSMC_MSG_SetDriverDramAddr,		1),
	MSG_MAP(SetToolsDramAddrHigh,		     PPSMC_MSG_SetToolsDramAddr,		0),
	MSG_MAP(SetSoftMaxByFreq,		     PPSMC_MSG_SetSoftMaxByFreq,		1),
	MSG_MAP(SetPptLimit,			     PPSMC_MSG_SetPptLimit,			0),
	MSG_MAP(GetPptLimit,			     PPSMC_MSG_GetPptLimit,			1),
	MSG_MAP(DramLogSetDramAddr,		     PPSMC_MSG_DramLogSetDramAddr,		0),
	MSG_MAP(HeavySBR,		     	     PPSMC_MSG_HeavySBR,			0),
	MSG_MAP(DFCstateControl,		     PPSMC_MSG_DFCstateControl,			0),
	MSG_MAP(GfxDriverResetRecovery,		     PPSMC_MSG_GfxDriverResetRecovery,		0),
	MSG_MAP(SetSoftMinGfxclk,                    PPSMC_MSG_SetSoftMinGfxClk,                1),
	MSG_MAP(SetSoftMaxGfxClk,                    PPSMC_MSG_SetSoftMaxGfxClk,                1),
	MSG_MAP(PrepareMp1ForUnload,                 PPSMC_MSG_PrepareForDriverUnload,          0),
	MSG_MAP(QueryValidMcaCount,                  PPSMC_MSG_QueryValidMcaCount,              SMU_MSG_RAS_PRI),
	MSG_MAP(McaBankDumpDW,                       PPSMC_MSG_McaBankDumpDW,                   SMU_MSG_RAS_PRI),
	MSG_MAP(ClearMcaOnRead,	                     PPSMC_MSG_ClearMcaOnRead,                  0),
	MSG_MAP(QueryValidMcaCeCount,                PPSMC_MSG_QueryValidMcaCeCount,            SMU_MSG_RAS_PRI),
	MSG_MAP(McaBankCeDumpDW,                     PPSMC_MSG_McaBankCeDumpDW,                 SMU_MSG_RAS_PRI),
	MSG_MAP(SelectPLPDMode,                      PPSMC_MSG_SelectPLPDMode,                  0),
	MSG_MAP(SetThrottlingPolicy,                 PPSMC_MSG_SetThrottlingPolicy,             0),
	MSG_MAP(ResetSDMA,                           PPSMC_MSG_ResetSDMA,                       0),
	MSG_MAP(GetRASTableVersion,                  PPSMC_MSG_GetRasTableVersion,              0),
	MSG_MAP(SetTimestamp,                        PPSMC_MSG_SetTimestamp,                    0),
	MSG_MAP(GetTimestamp,                        PPSMC_MSG_GetTimestamp,                    0),
	MSG_MAP(GetBadPageIpid,                      PPSMC_MSG_GetBadPageIpIdLoHi,              0),
	MSG_MAP(EraseRasTable,                       PPSMC_MSG_EraseRasTable,                   0),
	MSG_MAP(GetStaticMetricsTable,               PPSMC_MSG_GetStaticMetricsTable,		1),
	MSG_MAP(GetSystemMetricsTable,               PPSMC_MSG_GetSystemMetricsTable,           1),
	MSG_MAP(GetSystemMetricsVersion,             PPSMC_MSG_GetSystemMetricsVersion,		0),
	MSG_MAP(ResetVCN,                            PPSMC_MSG_ResetVCN,                        0),
	MSG_MAP(SetFastPptLimit,                     PPSMC_MSG_SetFastPptLimit,			0),
	MSG_MAP(GetFastPptLimit,                     PPSMC_MSG_GetFastPptLimit,			0),
	MSG_MAP(SetSoftMinGl2clk,                    PPSMC_MSG_SetSoftMinGl2clk,		0),
	MSG_MAP(SetSoftMaxGl2clk,                    PPSMC_MSG_SetSoftMaxGl2clk,		0),
	MSG_MAP(SetSoftMinFclk,                      PPSMC_MSG_SetSoftMinFclk,			0),
	MSG_MAP(SetSoftMaxFclk,                      PPSMC_MSG_SetSoftMaxFclk,			0),
};

/* TODO: Update the clk map once enum PPCLK is updated in smu15_driver_if_v15_0_8.h */
static struct cmn2asic_mapping smu_v15_0_8_clk_map[SMU_CLK_COUNT] = {
	CLK_MAP(UCLK,		PPCLK_UCLK),
};

static const struct cmn2asic_mapping smu_v15_0_8_feature_mask_map[SMU_FEATURE_COUNT] = {
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DATA_CALCULATIONS_BIT, 		FEATURE_ID_DATA_CALCULATION),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DPM_GFXCLK_BIT, 			FEATURE_ID_DPM_GFXCLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DPM_UCLK_BIT,                    FEATURE_ID_DPM_UCLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DPM_FCLK_BIT, 			FEATURE_ID_DPM_FCLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DPM_GL2CLK_BIT, 			FEATURE_ID_DPM_GL2CLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_GFXCLK_BIT, 			FEATURE_ID_DS_GFXCLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_SOCCLK_BIT, 			FEATURE_ID_DS_SOCCLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_LCLK_BIT, 			FEATURE_ID_DS_LCLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_FCLK_BIT, 			FEATURE_ID_DS_FCLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_DMABECLK_BIT, 		FEATURE_ID_DS_DMABECLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_MPIFOECLK_BIT, 		FEATURE_ID_DS_MPIFOECLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_MPRASCLK_BIT, 		FEATURE_ID_DS_MPRASCLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_MPNHTCLK_BIT, 		FEATURE_ID_DS_MPNHTCLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_FIOCLK_BIT, 			FEATURE_ID_DS_FIOCLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_DXIOCLK_BIT, 			FEATURE_ID_DS_DXIOCLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_GL2CLK_BIT, 			FEATURE_ID_DS_GL2CLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_PPT_BIT, 			FEATURE_ID_PPT),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_TDC_BIT, 			FEATURE_ID_TDC),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_MP1_CG_BIT, 			FEATURE_ID_SMU_CG),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_FW_CTF_BIT, 			FEATURE_ID_FW_CTF),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_THERMAL_BIT, 			FEATURE_ID_THERMAL),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_SOC_PCC_BIT, 			FEATURE_ID_SOC_PCC),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_XGMI_PER_LINK_PWR_DWN_BIT,	FEATURE_ID_XGMI_PER_LINK_PWR_DOWN),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_VCN_BIT,			FEATURE_ID_DS_VCN),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_MP1CLK_BIT,			FEATURE_ID_DS_MP1CLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_MPIOCLK_BIT,			FEATURE_ID_DS_MPIOCLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_DS_MP0CLK_BIT,			FEATURE_ID_DS_MP0CLK),
	SMU_15_0_8_FEA_MAP(SMU_FEATURE_PIT_BIT,				FEATURE_ID_PIT),
};

#define TABLE_PMSTATUSLOG             0
#define TABLE_SMU_METRICS             1
#define TABLE_I2C_COMMANDS            2
#define TABLE_COUNT                   3

static const struct cmn2asic_mapping smu_v15_0_8_table_map[SMU_TABLE_COUNT] = {
	TAB_MAP(PMSTATUSLOG),
	TAB_MAP(SMU_METRICS),
	TAB_MAP(I2C_COMMANDS),
};

static int smu_v15_0_8_tables_init(struct smu_context *smu)
{
	return 0;
}

static int smu_v15_0_8_allocate_dpm_context(struct smu_context *smu)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;

	smu_dpm->dpm_context =
		kzalloc(sizeof(struct smu_15_0_dpm_context), GFP_KERNEL);
	if (!smu_dpm->dpm_context)
		return -ENOMEM;
	smu_dpm->dpm_context_size = sizeof(struct smu_15_0_dpm_context);

	smu_dpm->dpm_policies =
		kzalloc(sizeof(struct smu_dpm_policy_ctxt), GFP_KERNEL);
	if (!smu_dpm->dpm_policies) {
		kfree(smu_dpm->dpm_context);
		return -ENOMEM;
	}

	return 0;
}

static int smu_v15_0_8_init_smc_tables(struct smu_context *smu)
{
	int ret = 0;

	ret = smu_v15_0_8_tables_init(smu);
	if (ret)
		return ret;

	ret = smu_v15_0_8_allocate_dpm_context(smu);

	return ret;
}

static int smu_v15_0_8_init_allowed_features(struct smu_context *smu)
{
	/* pptable will handle the features to enable */
	smu_feature_list_set_all(smu, SMU_FEATURE_LIST_ALLOWED);

	return 0;
}

static int smu_v15_0_8_set_default_dpm_table(struct smu_context *smu)
{
	return 0;
}

static int smu_v15_0_8_setup_pptable(struct smu_context *smu)
{
	struct smu_table_context *table_context = &smu->smu_table;

	/* TODO: PPTable is not available.
	 * 1) Find an alternate way to get 'PPTable values' here.
	 * 2) Check if there is SW CTF
	 */
	table_context->thermal_controller_type = 0;

	return 0;
}

static int smu_v15_0_8_check_fw_status(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t mp1_fw_flags;

	mp1_fw_flags = RREG32_PCIE(MP1_Public |
				   (smnMP1_FIRMWARE_FLAGS_15_0_8 & 0xffffffff));

	if ((mp1_fw_flags & MP1_CRU1_MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED_MASK) >>
	    MP1_CRU1_MP1_FIRMWARE_FLAGS__INTERRUPTS_ENABLED__SHIFT)
		return 0;

	return -EIO;
}

static int smu_v15_0_8_irq_process(struct amdgpu_device *adev,
				   struct amdgpu_irq_src *source,
				   struct amdgpu_iv_entry *entry)
{
	struct smu_context *smu = adev->powerplay.pp_handle;
	struct smu_power_context *smu_power = &smu->smu_power;
	struct smu_15_0_power_context *power_context = smu_power->power_context;
	uint32_t client_id = entry->client_id;
	uint32_t ctxid = entry->src_data[0];
	uint32_t src_id = entry->src_id;
	uint32_t data;

	if (client_id == SOC_V1_0_IH_CLIENTID_MP1) {
		if (src_id == IH_INTERRUPT_ID_TO_DRIVER) {
			/* ACK SMUToHost interrupt */
			data = RREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL);
			data = REG_SET_FIELD(data, MP1_SMN_IH_SW_INT_CTRL, INT_ACK, 1);
			WREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL, data);
			/*
			 * ctxid is used to distinguish different events for SMCToHost
			 * interrupt.
			 */
			switch (ctxid) {
			case IH_INTERRUPT_CONTEXT_ID_THERMAL_THROTTLING:
				/*
				 * Increment the throttle interrupt counter
				 */
				atomic64_inc(&smu->throttle_int_counter);

				if (!atomic_read(&adev->throttling_logging_enabled))
					return 0;

				/* This uses the new method which fixes the
				 * incorrect throttling status reporting
				 * through metrics table. For older FWs,
				 * it will be ignored.
				 */
				if (__ratelimit(&adev->throttling_logging_rs)) {
					atomic_set(
						&power_context->throttle_status,
							entry->src_data[1]);
					schedule_work(&smu->throttling_logging_work);
				}
				break;
			default:
				dev_dbg(adev->dev, "Unhandled context id %d from client:%d!\n",
									ctxid, client_id);
				break;
			}
		}
	}

	return 0;
}

static int smu_v15_0_8_set_irq_state(struct amdgpu_device *adev,
				     struct amdgpu_irq_src *source,
				     unsigned type,
				     enum amdgpu_interrupt_state state)
{
	uint32_t val = 0;

	switch (state) {
	case AMDGPU_IRQ_STATE_DISABLE:
		/* For MP1 SW irqs */
		val = RREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT_CTRL, INT_MASK, 1);
		WREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL, val);

		break;
	case AMDGPU_IRQ_STATE_ENABLE:
		/* For MP1 SW irqs */
		val = RREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT, ID, 0xFE);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT, VALID, 0);
		WREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT, val);

		val = RREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL);
		val = REG_SET_FIELD(val, MP1_SMN_IH_SW_INT_CTRL, INT_MASK, 0);
		WREG32_SOC15(MP1, 0, regMP1_SMN_IH_SW_INT_CTRL, val);

		break;
	default:
		break;
	}

	return 0;
}

static const struct amdgpu_irq_src_funcs smu_v15_0_8_irq_funcs = {
	.set = smu_v15_0_8_set_irq_state,
	.process = smu_v15_0_8_irq_process,
};

static int smu_v15_0_8_register_irq_handler(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct amdgpu_irq_src *irq_src = &smu->irq_source;
	int ret = 0;

	if (amdgpu_sriov_vf(adev))
		return 0;

	irq_src->num_types = 1;
	irq_src->funcs = &smu_v15_0_8_irq_funcs;

	ret = amdgpu_irq_add_id(adev, SOC_V1_0_IH_CLIENTID_MP1,
				IH_INTERRUPT_ID_TO_DRIVER,
				irq_src);
	if (ret)
		return ret;

	return ret;
}

static int smu_v15_0_8_notify_unload(struct smu_context *smu)
{
	if (amdgpu_in_reset(smu->adev))
		return 0;

	dev_dbg(smu->adev->dev, "Notify PMFW about driver unload");
	/* Ignore return, just intimate FW that driver is not going to be there */
	smu_cmn_send_smc_msg(smu, SMU_MSG_PrepareMp1ForUnload, NULL);

	return 0;
}


static int smu_v15_0_8_system_features_control(struct smu_context *smu,
					       bool enable)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0;

	if (amdgpu_sriov_vf(adev))
		return 0;

	if (enable)
		ret = smu_v15_0_system_features_control(smu, enable);
	else
		smu_v15_0_8_notify_unload(smu);

	return ret;
}

/**
 * smu_v15_0_8_get_enabled_mask - Get enabled SMU features (128-bit)
 * @smu: SMU context
 * @feature_mask: feature mask structure
 *
 * SMU 15 returns all 128 feature bits in a single message via out_args[0..3].
 * For backward compatibility, this function returns only the first 64 bits.
 *
 * Return: 0 on success, negative errno on failure
 */
static int smu_v15_0_8_get_enabled_mask(struct smu_context *smu,
					struct smu_feature_bits *feature_mask)
{
	struct smu_msg_args args = {
		.msg = SMU_MSG_GetEnabledSmuFeatures,
		.num_args = 0,
		.num_out_args = 2,
	};
	int ret;

	if (!feature_mask)
		return -EINVAL;

	ret = smu->msg_ctl.ops->send_msg(&smu->msg_ctl, &args);

	if (ret)
		return ret;

	smu_feature_bits_from_arr32(feature_mask, args.out_args,
				    SMU_FEATURE_NUM_DEFAULT);

	return 0;
}

static bool smu_v15_0_8_is_dpm_running(struct smu_context *smu)
{
	int ret = 0;
	struct smu_feature_bits feature_enabled;

	ret = smu_v15_0_8_get_enabled_mask(smu, &feature_enabled);
	if (ret)
		return false;

	return smu_feature_bits_test_mask(&feature_enabled,
					  smu_v15_0_8_dpm_features.bits);
}

static int smu_v15_0_8_mode2_reset(struct smu_context *smu)
{
	struct smu_msg_ctl *ctl = &smu->msg_ctl;
	struct amdgpu_device *adev = smu->adev;
	int timeout = 10;
	int ret = 0;

	mutex_lock(&ctl->lock);

	ret = smu_msg_send_async_locked(ctl, SMU_MSG_GfxDeviceDriverReset,
					SMU_RESET_MODE_2);

	if (ret)
		goto out;

	/* Reset takes a bit longer, wait for 200ms. */
	msleep(200);

	dev_dbg(adev->dev, "wait for reset ack\n");
	do {
		ret = smu_msg_wait_response(ctl, 0);
		/* Wait a bit more time for getting ACK */
		if (ret == -ETIME) {
			--timeout;
			usleep_range(500, 1000);
			continue;
		}

		if (ret)
			goto out;

	} while (ret == -ETIME && timeout);

out:
	mutex_unlock(&ctl->lock);

	if (ret)
		dev_err(adev->dev, "failed to send mode2 reset, error code %d",
			ret);

	return ret;
}

static const struct pptable_funcs smu_v15_0_8_ppt_funcs = {
	.init_allowed_features = smu_v15_0_8_init_allowed_features,
	.set_default_dpm_table = smu_v15_0_8_set_default_dpm_table,
	.is_dpm_running = smu_v15_0_8_is_dpm_running,
	.init_smc_tables = smu_v15_0_8_init_smc_tables,
	.fini_smc_tables = smu_v15_0_fini_smc_tables,
	.init_power = smu_v15_0_init_power,
	.fini_power = smu_v15_0_fini_power,
	.check_fw_status = smu_v15_0_8_check_fw_status,
	.check_fw_version = smu_cmn_check_fw_version,
	.set_driver_table_location = smu_v15_0_set_driver_table_location,
	.set_tool_table_location = smu_v15_0_set_tool_table_location,
	.notify_memory_pool_location = smu_v15_0_notify_memory_pool_location,
	.system_features_control = smu_v15_0_8_system_features_control,
	.get_enabled_mask = smu_v15_0_8_get_enabled_mask,
	.feature_is_enabled = smu_cmn_feature_is_enabled,
	.register_irq_handler = smu_v15_0_8_register_irq_handler,
	.setup_pptable = smu_v15_0_8_setup_pptable,
	.get_pp_feature_mask = smu_cmn_get_pp_feature_mask,
	.wait_for_event = smu_v15_0_wait_for_event,
	.mode2_reset = smu_v15_0_8_mode2_reset,
};

static void smu_v15_0_8_init_msg_ctl(struct smu_context *smu,
				     const struct cmn2asic_msg_mapping *message_map)
{
	struct amdgpu_device *adev = smu->adev;
	struct smu_msg_ctl *ctl = &smu->msg_ctl;

	ctl->smu = smu;
	mutex_init(&ctl->lock);
	ctl->config.msg_reg = SOC15_REG_OFFSET(MP1, 0, regMP1_SMN_C2PMSG_40);
	ctl->config.resp_reg = SOC15_REG_OFFSET(MP1, 0, regMP1_SMN_C2PMSG_41);
	ctl->config.arg_regs[0] = SOC15_REG_OFFSET(MP1, 0, regMP1_SMN_C2PMSG_42);
	ctl->config.arg_regs[1] = SOC15_REG_OFFSET(MP1, 0, regMP1_SMN_C2PMSG_43);
	ctl->config.arg_regs[2] = SOC15_REG_OFFSET(MP1, 0, regMP1_SMN_C2PMSG_44);
	ctl->config.arg_regs[3] = SOC15_REG_OFFSET(MP1, 0, regMP1_SMN_C2PMSG_45);
	ctl->config.num_arg_regs = 4;
	ctl->ops = &smu_msg_v1_ops;
	ctl->default_timeout = adev->usec_timeout * 20;
	ctl->message_map = message_map;
}

void smu_v15_0_8_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &smu_v15_0_8_ppt_funcs;
	smu->clock_map = smu_v15_0_8_clk_map;
	smu->feature_map = smu_v15_0_8_feature_mask_map;
	smu->table_map = smu_v15_0_8_table_map;
	smu_v15_0_8_init_msg_ctl(smu, smu_v15_0_8_message_map);
	smu->smc_driver_if_version = SMU15_DRIVER_IF_VERSION_SMU_V15_0_8;
}
