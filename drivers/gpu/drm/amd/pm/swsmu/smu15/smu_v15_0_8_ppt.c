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
#include "amdgpu_fru_eeprom.h"

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

#define SMUQ10_TO_UINT(x) ((x) >> 10)
#define SMUQ10_FRAC(x) ((x) & 0x3ff)
#define SMUQ10_ROUND(x) ((SMUQ10_TO_UINT(x)) + ((SMUQ10_FRAC(x)) >= 0x200))

#define hbm_stack_mask_valid(umc_mask) \
	(((umc_mask) & 0xF) == 0xF)

#define for_each_hbm_stack(stack_idx, umc_mask) \
	for ((stack_idx) = 0; (umc_mask); \
	     (umc_mask) >>= 4, (stack_idx)++) \

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
	MSG_MAP(SetDriverDramAddr,		     PPSMC_MSG_SetDriverDramAddr,		1),
	MSG_MAP(SetToolsDramAddr,		     PPSMC_MSG_SetToolsDramAddr,		0),
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

static size_t smu_v15_0_8_get_system_metrics_size(void)
{
	return sizeof(SystemMetricsTable_t);
}

static int smu_v15_0_8_tables_init(struct smu_context *smu)
{
	struct smu_v15_0_8_baseboard_temp_metrics *baseboard_temp_metrics;
	struct smu_v15_0_8_gpuboard_temp_metrics *gpuboard_temp_metrics;
	struct smu_table_context *smu_table = &smu->smu_table;
	int ret, gpu_metrcs_size = sizeof(MetricsTable_t);
	struct smu_table *tables = smu_table->tables;
	struct smu_v15_0_8_gpu_metrics *gpu_metrics;
	void *driver_pptable __free(kfree) = NULL;
	void *metrics_table __free(kfree) = NULL;

	SMU_TABLE_INIT(tables, SMU_TABLE_PMSTATUSLOG, SMU15_TOOL_SIZE,
		       PAGE_SIZE, AMDGPU_GEM_DOMAIN_VRAM);

	SMU_TABLE_INIT(tables, SMU_TABLE_SMU_METRICS,
		       gpu_metrcs_size,
		       PAGE_SIZE,
		       AMDGPU_GEM_DOMAIN_VRAM | AMDGPU_GEM_DOMAIN_GTT);
	SMU_TABLE_INIT(tables, SMU_TABLE_PMFW_SYSTEM_METRICS,
		       smu_v15_0_8_get_system_metrics_size(), PAGE_SIZE,
		       AMDGPU_GEM_DOMAIN_VRAM | AMDGPU_GEM_DOMAIN_GTT);

	metrics_table = kzalloc(gpu_metrcs_size, GFP_KERNEL);
	if (!metrics_table)
		return -ENOMEM;

	smu_table->metrics_time = 0;

	driver_pptable = kzalloc(sizeof(PPTable_t), GFP_KERNEL);
	if (!driver_pptable)
		return -ENOMEM;

	ret = smu_driver_table_init(smu, SMU_DRIVER_TABLE_GPU_METRICS,
				    sizeof(struct smu_v15_0_8_gpu_metrics),
				    SMU_GPU_METRICS_CACHE_INTERVAL);
	if (ret)
		return ret;

	gpu_metrics = (struct smu_v15_0_8_gpu_metrics *)smu_driver_table_ptr(smu,
		       SMU_DRIVER_TABLE_GPU_METRICS);
	smu_v15_0_8_gpu_metrics_init(gpu_metrics, 1, 9);

	ret = smu_table_cache_init(smu, SMU_TABLE_PMFW_SYSTEM_METRICS,
				   smu_v15_0_8_get_system_metrics_size(), 5);

	if (ret)
		return ret;

	/* Initialize base board temperature metrics */
	ret = smu_driver_table_init(smu,
				    SMU_DRIVER_TABLE_BASEBOARD_TEMP_METRICS,
				    sizeof(*baseboard_temp_metrics), 50);
	if (ret)
		return ret;
	baseboard_temp_metrics = (struct smu_v15_0_8_baseboard_temp_metrics *)
		smu_driver_table_ptr(smu,
				     SMU_DRIVER_TABLE_BASEBOARD_TEMP_METRICS);
	smu_v15_0_8_baseboard_temp_metrics_init(baseboard_temp_metrics, 1, 1);
	/* Initialize GPU board temperature metrics */
	ret = smu_driver_table_init(smu, SMU_DRIVER_TABLE_GPUBOARD_TEMP_METRICS,
				    sizeof(*gpuboard_temp_metrics), 50);
	if (ret) {
		smu_table_cache_fini(smu, SMU_TABLE_PMFW_SYSTEM_METRICS);
		smu_driver_table_fini(smu,
				      SMU_DRIVER_TABLE_BASEBOARD_TEMP_METRICS);
		return ret;
	}
	gpuboard_temp_metrics = (struct smu_v15_0_8_gpuboard_temp_metrics *)
		smu_driver_table_ptr(smu,
				     SMU_DRIVER_TABLE_GPUBOARD_TEMP_METRICS);
	smu_v15_0_8_gpuboard_temp_metrics_init(gpuboard_temp_metrics, 1, 1);

	smu_table->metrics_table = no_free_ptr(metrics_table);
	smu_table->driver_pptable = no_free_ptr(driver_pptable);

	mutex_init(&smu_table->metrics_lock);

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

static int smu_v15_0_8_tables_fini(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;

	smu_driver_table_fini(smu, SMU_DRIVER_TABLE_BASEBOARD_TEMP_METRICS);
	smu_driver_table_fini(smu, SMU_DRIVER_TABLE_GPUBOARD_TEMP_METRICS);
	smu_table_cache_fini(smu, SMU_TABLE_PMFW_SYSTEM_METRICS);
	mutex_destroy(&smu_table->metrics_lock);

	return 0;
}

static int smu_v15_0_8_fini_smc_tables(struct smu_context *smu)
{
	int ret;

	ret = smu_v15_0_8_tables_fini(smu);
	if (ret)
		return ret;

	ret = smu_v15_0_fini_smc_tables(smu);
	if (ret)
		return ret;

	return ret;
}

static int smu_v15_0_8_init_allowed_features(struct smu_context *smu)
{
	/* pptable will handle the features to enable */
	smu_feature_list_set_all(smu, SMU_FEATURE_LIST_ALLOWED);

	return 0;
}

static int smu_v15_0_8_get_metrics_table_internal(struct smu_context *smu, uint32_t tmo, void *data)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	uint32_t table_size = smu_table->tables[SMU_TABLE_SMU_METRICS].size;
	struct smu_table *table = &smu_table->driver_table;
	struct amdgpu_device *adev = smu->adev;

	mutex_lock(&smu_table->metrics_lock);

	if (!tmo || !smu_table->metrics_time ||
	    time_after(jiffies, smu_table->metrics_time + msecs_to_jiffies(tmo))) {
		int ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetMetricsTable, NULL);
		if (ret) {
			dev_info(adev->dev,
				 "Failed to export SMU metrics table!\n");
			mutex_unlock(&smu_table->metrics_lock);
			return ret;
		}

		amdgpu_device_invalidate_hdp(smu->adev, NULL);
		memcpy(smu_table->metrics_table, table->cpu_addr, table_size);

		smu_table->metrics_time = jiffies;
	}

	if (data)
		memcpy(data, smu_table->metrics_table, table_size);
	mutex_unlock(&smu_table->metrics_lock);
	return 0;
}

static int smu_v15_0_8_get_smu_metrics_data(struct smu_context *smu,
					    MetricsMember_t member, uint32_t *value)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	MetricsTable_t *metrics = (MetricsTable_t *)smu_table->metrics_table;
	struct amdgpu_device *adev = smu->adev;
	int ret, xcc_id;

	ret = smu_v15_0_8_get_metrics_table_internal(smu, 10, NULL);
	if (ret)
		return ret;

	switch (member) {
	case METRICS_CURR_GFXCLK:
	case METRICS_AVERAGE_GFXCLK:
			xcc_id = GET_INST(GC, 0);
			*value = SMUQ10_ROUND(metrics->GfxclkFrequency[xcc_id]);
		break;
	case METRICS_CURR_SOCCLK:
	case METRICS_AVERAGE_SOCCLK:
		*value = SMUQ10_ROUND(metrics->SocclkFrequency[0]);
		break;
	case METRICS_CURR_UCLK:
	case METRICS_AVERAGE_UCLK:
		*value = SMUQ10_ROUND(metrics->UclkFrequency[0]);
		break;
	case METRICS_CURR_VCLK:
		*value = SMUQ10_ROUND(metrics->VclkFrequency[0]);
		break;
	case METRICS_CURR_DCLK:
		*value = SMUQ10_ROUND(metrics->DclkFrequency[0]);
		break;
	case METRICS_CURR_FCLK:
		*value = SMUQ10_ROUND(metrics->FclkFrequency[0]);
		break;
	case METRICS_AVERAGE_GFXACTIVITY:
		*value = SMUQ10_ROUND(metrics->SocketGfxBusy);
		break;
	case METRICS_AVERAGE_MEMACTIVITY:
		*value = SMUQ10_ROUND(metrics->DramBandwidthUtilization);
		break;
	case METRICS_CURR_SOCKETPOWER:
		*value = SMUQ10_ROUND(metrics->SocketPower) << 8;
		break;
	case METRICS_TEMPERATURE_HOTSPOT:
		*value = SMUQ10_ROUND(metrics->MaxSocketTemperature) *
			 SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	case METRICS_TEMPERATURE_MEM:
	{
		struct amdgpu_device *adev = smu->adev;
		u32 max_hbm_temp = 0;

		/* Find max temperature across all HBM stacks */
		if (adev->umc.active_mask) {
			u64 mask = adev->umc.active_mask;
			int stack_idx;

			for_each_hbm_stack(stack_idx, mask) {
				u32 temp;

				if (!hbm_stack_mask_valid(mask))
					continue;

				temp = SMUQ10_ROUND(metrics->HbmTemperature[stack_idx]);
				if (temp > max_hbm_temp)
					max_hbm_temp = temp;
			}
		}
		*value = max_hbm_temp * SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	}
	/* This is the max of all VRs and not just SOC VR.
	 */
	case METRICS_TEMPERATURE_VRSOC:
		*value = SMUQ10_ROUND(metrics->MaxVrTemperature) *
			 SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;
		break;
	default:
		*value = UINT_MAX;
		break;
	}

	return 0;
}

static int smu_v15_0_8_get_current_clk_freq_by_table(struct smu_context *smu,
						     enum smu_clk_type clk_type,
						     uint32_t *value)
{
	MetricsMember_t member_type;

	if (!value)
		return -EINVAL;

	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
		member_type = METRICS_CURR_GFXCLK;
		break;
	case SMU_UCLK:
	case SMU_MCLK:
		member_type = METRICS_CURR_UCLK;
		break;
	case SMU_SOCCLK:
		member_type = METRICS_CURR_SOCCLK;
		break;
	case SMU_VCLK:
		member_type = METRICS_CURR_VCLK;
		break;
	case SMU_DCLK:
		member_type = METRICS_CURR_DCLK;
		break;
	case SMU_FCLK:
		member_type = METRICS_CURR_FCLK;
		break;
	default:
		return -EINVAL;
	}

	return smu_v15_0_8_get_smu_metrics_data(smu, member_type, value);
}

static int smu_v15_0_8_get_current_activity_percent(struct smu_context *smu,
						    enum amd_pp_sensors sensor,
						    uint32_t *value)
{
	int ret = 0;

	if (!value)
		return -EINVAL;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = smu_v15_0_8_get_smu_metrics_data(smu,
						       METRICS_AVERAGE_GFXACTIVITY, value);
		break;
	case AMDGPU_PP_SENSOR_MEM_LOAD:
		ret = smu_v15_0_8_get_smu_metrics_data(smu,
						       METRICS_AVERAGE_MEMACTIVITY, value);
		break;
	default:
		dev_err(smu->adev->dev,
			"Invalid sensor for retrieving clock activity\n");
		return -EINVAL;
	}

	return ret;
}

static int smu_v15_0_8_thermal_get_temperature(struct smu_context *smu,
					       enum amd_pp_sensors sensor,
					       uint32_t *value)
{
	int ret = 0;

	if (!value)
		return -EINVAL;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
		ret = smu_v15_0_8_get_smu_metrics_data(smu,
						       METRICS_TEMPERATURE_HOTSPOT, value);
		break;
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		ret = smu_v15_0_8_get_smu_metrics_data(smu,
						       METRICS_TEMPERATURE_MEM, value);
		break;
	default:
		dev_err(smu->adev->dev, "Invalid sensor for retrieving temp\n");
		return -EINVAL;
	}

	return ret;
}

static int smu_v15_0_8_get_system_metrics_table(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *table = &smu_table->driver_table;
	struct smu_table *tables = smu_table->tables;
	struct smu_table *sys_table;
	int ret;

	sys_table = &tables[SMU_TABLE_PMFW_SYSTEM_METRICS];
	if (smu_table_cache_is_valid(sys_table))
		return 0;

	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetSystemMetricsTable, NULL);
	if (ret) {
		dev_info(smu->adev->dev,
			 "Failed to export system metrics table!\n");
		return ret;
	}

	amdgpu_hdp_invalidate(smu->adev, NULL);
	smu_table_cache_update_time(sys_table, jiffies);
	memcpy(sys_table->cache.buffer, table->cpu_addr,
	       sizeof(SystemMetricsTable_t));

	return 0;
}

static int smu_v15_0_8_get_npm_data(struct smu_context *smu,
				    enum amd_pp_sensors sensor,
				    uint32_t *value)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	SystemMetricsTable_t *metrics;
	struct smu_table *sys_table;
	int ret;

	if (sensor == AMDGPU_PP_SENSOR_MAXNODEPOWERLIMIT) {
		/*TBD as of now put 0 */
		*value = 0;
		return 0;
	}

	ret = smu_v15_0_8_get_system_metrics_table(smu);
	if (ret)
		return ret;

	sys_table = &tables[SMU_TABLE_PMFW_SYSTEM_METRICS];
	metrics = (SystemMetricsTable_t *)sys_table->cache.buffer;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_NODEPOWERLIMIT:
		*value = SMUQ10_ROUND(metrics->NodePowerLimit);
		break;
	case AMDGPU_PP_SENSOR_NODEPOWER:
		*value = SMUQ10_ROUND(metrics->NodePower);
		break;
	case AMDGPU_PP_SENSOR_GPPTRESIDENCY:
		*value = SMUQ10_ROUND(metrics->GlobalPPTResidencyAcc);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int smu_v15_0_8_read_sensor(struct smu_context *smu,
				   enum amd_pp_sensors sensor, void *data,
				   uint32_t *size)
{
	struct smu_15_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	int ret = 0;

	if (amdgpu_ras_intr_triggered())
		return 0;

	if (!data || !size)
		return -EINVAL;

	switch (sensor) {
	case AMDGPU_PP_SENSOR_MEM_LOAD:
	case AMDGPU_PP_SENSOR_GPU_LOAD:
		ret = smu_v15_0_8_get_current_activity_percent(smu, sensor,
							       (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_INPUT_POWER:
		ret = smu_v15_0_8_get_smu_metrics_data(smu,
						       METRICS_CURR_SOCKETPOWER,
						       (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_HOTSPOT_TEMP:
	case AMDGPU_PP_SENSOR_MEM_TEMP:
		ret = smu_v15_0_8_thermal_get_temperature(smu, sensor,
							  (uint32_t *)data);
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_MCLK:
		ret = smu_v15_0_8_get_current_clk_freq_by_table(smu,
								SMU_UCLK, (uint32_t *)data);
		/* the output clock frequency in 10K unit */
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GFX_SCLK:
		ret = smu_v15_0_8_get_current_clk_freq_by_table(smu,
								SMU_GFXCLK, (uint32_t *)data);
		*(uint32_t *)data *= 100;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_VDDBOARD:
		*(uint32_t *)data = dpm_context->board_volt;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_NODEPOWERLIMIT:
	case AMDGPU_PP_SENSOR_NODEPOWER:
	case AMDGPU_PP_SENSOR_GPPTRESIDENCY:
	case AMDGPU_PP_SENSOR_MAXNODEPOWERLIMIT:
		ret = smu_v15_0_8_get_npm_data(smu, sensor, (uint32_t *)data);
		if (ret)
			return ret;
		*size = 4;
		break;
	case AMDGPU_PP_SENSOR_GPU_AVG_POWER:
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int smu_v15_0_8_emit_clk_levels(struct smu_context *smu,
				       enum smu_clk_type type, char *buf,
				       int *offset)
{
	struct smu_umd_pstate_table *pstate_table = &smu->pstate_table;
	struct smu_15_0_dpm_context *dpm_context;
	struct smu_dpm_table *single_dpm_table = NULL;
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	int ret, now, size = *offset;

	if (amdgpu_ras_intr_triggered()) {
		sysfs_emit_at(buf, size, "unavailable\n");
		return -EBUSY;
	}

	dpm_context = smu_dpm->dpm_context;

	switch (type) {
	case SMU_OD_SCLK:
		size += sysfs_emit_at(buf, size, "%s:\n", "OD_SCLK");
		size += sysfs_emit_at(buf, size, "0: %uMhz\n1: %uMhz\n",
				      pstate_table->gfxclk_pstate.curr.min,
				      pstate_table->gfxclk_pstate.curr.max);
		break;
	case SMU_OD_MCLK:
		size += sysfs_emit_at(buf, size, "%s:\n", "OD_MCLK");
		size += sysfs_emit_at(buf, size, "0: %uMhz\n1: %uMhz\n",
				      pstate_table->uclk_pstate.curr.min,
				      pstate_table->uclk_pstate.curr.max);
		break;
	case SMU_SCLK:
	case SMU_GFXCLK:
		single_dpm_table = &dpm_context->dpm_tables.gfx_table;
		break;
	case SMU_MCLK:
	case SMU_UCLK:
		single_dpm_table = &dpm_context->dpm_tables.uclk_table;
		break;
	case SMU_SOCCLK:
		single_dpm_table = &dpm_context->dpm_tables.soc_table;
		break;
	case SMU_FCLK:
		single_dpm_table = &dpm_context->dpm_tables.fclk_table;
		break;
	case SMU_VCLK:
		single_dpm_table = &dpm_context->dpm_tables.vclk_table;
		break;
	case SMU_DCLK:
		single_dpm_table = &dpm_context->dpm_tables.dclk_table;
		break;
	default:
		break;
	}

	if (single_dpm_table) {
		ret = smu_v15_0_8_get_current_clk_freq_by_table(smu, type, &now);
		if (ret) {
			dev_err(smu->adev->dev,
				"Attempt to get current clk Failed!");
			return ret;
		}
		ret = smu_cmn_print_dpm_clk_levels(smu, single_dpm_table, now,
						   buf, offset);
		if (ret < 0)
			return ret;

		return 0;
	}

	*offset = size;

	return 0;
}

static int smu_v15_0_8_get_dpm_ultimate_freq(struct smu_context *smu,
					     enum smu_clk_type clk_type,
					     uint32_t *min, uint32_t *max)
{
	struct smu_15_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	struct smu_table_context *smu_table = &smu->smu_table;
	PPTable_t *pptable = (PPTable_t *)smu_table->driver_pptable;
	struct smu_dpm_table *dpm_table;
	uint32_t min_clk = 0, max_clk = 0;

	if (!pptable->init)
		return -EINVAL;

	/* Try cached DPM tables first */
	if (dpm_context) {
		switch (clk_type) {
		case SMU_MCLK:
		case SMU_UCLK:
			dpm_table = &dpm_context->dpm_tables.uclk_table;
			break;
		case SMU_GFXCLK:
		case SMU_SCLK:
			dpm_table = &dpm_context->dpm_tables.gfx_table;
			break;
		case SMU_SOCCLK:
			dpm_table = &dpm_context->dpm_tables.soc_table;
			break;
		case SMU_FCLK:
			dpm_table = &dpm_context->dpm_tables.fclk_table;
			break;
		case SMU_GL2CLK:
			dpm_table = &dpm_context->dpm_tables.gl2_table;
			break;
		case SMU_VCLK:
			dpm_table = &dpm_context->dpm_tables.vclk_table;
			break;
		case SMU_DCLK:
			dpm_table = &dpm_context->dpm_tables.dclk_table;
			break;
		default:
			dpm_table = NULL;
			break;
		}

		if (dpm_table && dpm_table->count > 0) {
			min_clk = SMU_DPM_TABLE_MIN(dpm_table);
			max_clk = SMU_DPM_TABLE_MAX(dpm_table);

			if (min_clk && max_clk) {
				if (min)
					*min = min_clk;
				if (max)
					*max = max_clk;
				return 0;
			}
		}
	}

	/* Fall back to pptable */
	switch (clk_type) {
	case SMU_GFXCLK:
	case SMU_SCLK:
		min_clk = pptable->MinGfxclkFrequency;
		max_clk = pptable->MaxGfxclkFrequency;
		break;
	case SMU_FCLK:
		min_clk = pptable->MinFclkFrequency;
		max_clk = pptable->MaxFclkFrequency;
		break;
	case SMU_GL2CLK:
		min_clk = pptable->MinGl2clkFrequency;
		max_clk = pptable->MaxGl2clkFrequency;
		break;
	case SMU_MCLK:
	case SMU_UCLK:
		min_clk = pptable->UclkFrequencyTable[0];
		max_clk = pptable->UclkFrequencyTable[ARRAY_SIZE(pptable->UclkFrequencyTable) - 1];
		break;
	case SMU_SOCCLK:
		min_clk = pptable->SocclkFrequency;
		max_clk = pptable->SocclkFrequency;
		break;
	case SMU_VCLK:
		min_clk = pptable->VclkFrequency;
		max_clk = pptable->VclkFrequency;
		break;
	case SMU_DCLK:
		min_clk = pptable->DclkFrequency;
		max_clk = pptable->DclkFrequency;
		break;
	default:
		return -EINVAL;
	}

	if (min)
		*min = min_clk;
	if (max)
		*max = max_clk;

	return 0;
}

static int smu_v15_0_8_set_dpm_table(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_15_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	struct smu_dpm_table *dpm_table;
	PPTable_t *pptable = (PPTable_t *)smu_table->driver_pptable;
	int i, ret;
	uint32_t gfxclkmin, gfxclkmax;

	/* gfxclk dpm table setup - fine-grained */
	dpm_table = &dpm_context->dpm_tables.gfx_table;
	dpm_table->clk_type = SMU_GFXCLK;
	dpm_table->flags = SMU_DPM_TABLE_FINE_GRAINED;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_GFXCLK_BIT)) {
		ret = smu_v15_0_8_get_dpm_ultimate_freq(smu, SMU_GFXCLK,
							&gfxclkmin, &gfxclkmax);
		if (ret)
			return ret;

		dpm_table->count = 2;
		dpm_table->dpm_levels[0].value = gfxclkmin;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->dpm_levels[1].value = gfxclkmax;
		dpm_table->dpm_levels[1].enabled = true;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = pptable->MinGfxclkFrequency;
		dpm_table->dpm_levels[0].enabled = true;
	}

	/* fclk dpm table setup - fine-grained */
	dpm_table = &dpm_context->dpm_tables.fclk_table;
	dpm_table->clk_type = SMU_FCLK;
	dpm_table->flags = SMU_DPM_TABLE_FINE_GRAINED;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_FCLK_BIT)) {
		dpm_table->count = 2;
		dpm_table->dpm_levels[0].value = pptable->MinFclkFrequency;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->dpm_levels[1].value = pptable->MaxFclkFrequency;
		dpm_table->dpm_levels[1].enabled = true;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = pptable->MinFclkFrequency;
		dpm_table->dpm_levels[0].enabled = true;
	}

	/* gl2clk dpm table setup - fine-grained */
	dpm_table = &dpm_context->dpm_tables.gl2_table;
	dpm_table->flags = SMU_DPM_TABLE_FINE_GRAINED;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_GL2CLK_BIT)) {
		dpm_table->count = 2;
		dpm_table->dpm_levels[0].value = pptable->MinGl2clkFrequency;
		dpm_table->dpm_levels[0].enabled = true;
		dpm_table->dpm_levels[1].value = pptable->MaxGl2clkFrequency;
		dpm_table->dpm_levels[1].enabled = true;
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = pptable->MinGl2clkFrequency;
		dpm_table->dpm_levels[0].enabled = true;
	}

	/* uclk dpm table setup - discrete levels */
	dpm_table = &dpm_context->dpm_tables.uclk_table;
	dpm_table->clk_type = SMU_UCLK;
	dpm_table->flags = 0;
	if (smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
		dpm_table->count = ARRAY_SIZE(pptable->UclkFrequencyTable);
		for (i = 0; i < dpm_table->count; ++i) {
			dpm_table->dpm_levels[i].value = pptable->UclkFrequencyTable[i];
			dpm_table->dpm_levels[i].enabled = true;
		}
	} else {
		dpm_table->count = 1;
		dpm_table->dpm_levels[0].value = pptable->UclkFrequencyTable[0];
		dpm_table->dpm_levels[0].enabled = true;
	}

	/* socclk dpm table setup - single boot-time value */
	dpm_table = &dpm_context->dpm_tables.soc_table;
	dpm_table->clk_type = SMU_SOCCLK;
	dpm_table->flags = 0;
	dpm_table->count = 1;
	dpm_table->dpm_levels[0].value = pptable->SocclkFrequency;
	dpm_table->dpm_levels[0].enabled = true;

	/* vclk dpm table setup - single boot-time value */
	dpm_table = &dpm_context->dpm_tables.vclk_table;
	dpm_table->clk_type = SMU_VCLK;
	dpm_table->flags = 0;
	dpm_table->count = 1;
	dpm_table->dpm_levels[0].value = pptable->VclkFrequency;
	dpm_table->dpm_levels[0].enabled = true;

	/* dclk dpm table setup - single boot-time value */
	dpm_table = &dpm_context->dpm_tables.dclk_table;
	dpm_table->clk_type = SMU_DCLK;
	dpm_table->flags = 0;
	dpm_table->count = 1;
	dpm_table->dpm_levels[0].value = pptable->DclkFrequency;
	dpm_table->dpm_levels[0].enabled = true;

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

static int smu_v15_0_8_get_static_metrics_table(struct smu_context *smu)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	uint32_t table_size = smu_table->tables[SMU_TABLE_SMU_METRICS].size;
	struct smu_table *table = &smu_table->driver_table;
	int ret;

	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetStaticMetricsTable, NULL);
	if (ret) {
		dev_err(smu->adev->dev,
			 "Failed to export static metrics table!\n");
		return ret;
	}

	amdgpu_hdp_invalidate(smu->adev, NULL);
	memcpy(smu_table->metrics_table, table->cpu_addr, table_size);

	return 0;
}

static int smu_v15_0_8_fru_get_product_info(struct smu_context *smu,
					    StaticMetricsTable_t *static_metrics)
{
	struct amdgpu_fru_info *fru_info;
	struct amdgpu_device *adev = smu->adev;

	if (!adev->fru_info) {
		adev->fru_info = kzalloc(sizeof(*adev->fru_info), GFP_KERNEL);
		if (!adev->fru_info)
			return -ENOMEM;
	}

	fru_info = adev->fru_info;
	strscpy(fru_info->product_number, static_metrics->ProductInfo.ModelNumber,
		sizeof(fru_info->product_number));
	strscpy(fru_info->product_name, static_metrics->ProductInfo.Name,
		sizeof(fru_info->product_name));
	strscpy(fru_info->serial, static_metrics->ProductInfo.Serial,
		sizeof(fru_info->serial));
	strscpy(fru_info->manufacturer_name, static_metrics->ProductInfo.ManufacturerName,
		sizeof(fru_info->manufacturer_name));
	strscpy(fru_info->fru_id, static_metrics->ProductInfo.FruId,
		sizeof(fru_info->fru_id));

	return 0;
}

static void smu_v15_0_8_init_xgmi_data(struct smu_context *smu,
				       StaticMetricsTable_t *static_metrics)
{
	uint16_t max_speed;
	uint8_t max_width;

	max_width = (uint8_t)static_metrics->MaxXgmiWidth;
	max_speed = (uint16_t)static_metrics->MaxXgmiBitrate;
	amgpu_xgmi_set_max_speed_width(smu->adev, max_speed, max_width);
}

static int smu_v15_0_8_set_driver_pptable(struct smu_context *smu)
{
	struct smu_15_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	struct smu_table_context *smu_table = &smu->smu_table;
	StaticMetricsTable_t *static_metrics = (StaticMetricsTable_t *)smu_table->metrics_table;
	PPTable_t *pptable = (PPTable_t *)smu_table->driver_pptable;
	int ret, i, n;
	uint32_t table_version;

	if (!pptable->init) {
		ret = smu_v15_0_8_get_static_metrics_table(smu);
		if (ret)
			return ret;

		ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetMetricsVersion,
					   &table_version);
		if (ret)
			return ret;
		smu_table->tables[SMU_TABLE_SMU_METRICS].version =
			table_version;

		pptable->MaxSocketPowerLimit =
			SMUQ10_ROUND(static_metrics->MaxSocketPowerLimit);
		pptable->MaxGfxclkFrequency =
			SMUQ10_ROUND(static_metrics->MaxGfxclkFrequency);
		pptable->MinGfxclkFrequency =
			SMUQ10_ROUND(static_metrics->MinGfxclkFrequency);
		pptable->MaxFclkFrequency =
			SMUQ10_ROUND(static_metrics->MaxFclkFrequency);
		pptable->MinFclkFrequency =
			SMUQ10_ROUND(static_metrics->MinFclkFrequency);
		pptable->MaxGl2clkFrequency =
			SMUQ10_ROUND(static_metrics->MaxGl2clkFrequency);
		pptable->MinGl2clkFrequency =
			SMUQ10_ROUND(static_metrics->MinGl2clkFrequency);

		for (i = 0; i < ARRAY_SIZE(static_metrics->UclkFrequencyTable); ++i)
			pptable->UclkFrequencyTable[i] =
				SMUQ10_ROUND(static_metrics->UclkFrequencyTable[i]);

		pptable->SocclkFrequency = SMUQ10_ROUND(static_metrics->SocclkFrequency);
		pptable->LclkFrequency = SMUQ10_ROUND(static_metrics->LclkFrequency);
		pptable->VclkFrequency = SMUQ10_ROUND(static_metrics->VclkFrequency);
		pptable->DclkFrequency = SMUQ10_ROUND(static_metrics->DclkFrequency);

		pptable->CTFLimitMID = SMUQ10_ROUND(static_metrics->CTFLimit_MID);
		pptable->CTFLimitAID = SMUQ10_ROUND(static_metrics->CTFLimit_AID);
		pptable->CTFLimitXCD = SMUQ10_ROUND(static_metrics->CTFLimit_XCD);
		pptable->CTFLimitHBM = SMUQ10_ROUND(static_metrics->CTFLimit_HBM);
		pptable->ThermalLimitMID = SMUQ10_ROUND(static_metrics->ThermalLimit_MID);
		pptable->ThermalLimitAID = SMUQ10_ROUND(static_metrics->ThermalLimit_AID);
		pptable->ThermalLimitXCD = SMUQ10_ROUND(static_metrics->ThermalLimit_XCD);
		pptable->ThermalLimitHBM = SMUQ10_ROUND(static_metrics->ThermalLimit_HBM);

		/* use MID0 serial number by default */
		pptable->PublicSerialNumberMID =
			static_metrics->PublicSerialNumber_MID[0];

		amdgpu_device_set_uid(smu->adev->uid_info, AMDGPU_UID_TYPE_SOC,
				      0, pptable->PublicSerialNumberMID);
		pptable->PublicSerialNumberAID =
			static_metrics->PublicSerialNumber_AID[0];
		pptable->PublicSerialNumberXCD =
			static_metrics->PublicSerialNumber_XCD[0];
		n = ARRAY_SIZE(static_metrics->PublicSerialNumber_MID);
		for (i = 0; i < n; i++) {
			amdgpu_device_set_uid(smu->adev->uid_info, AMDGPU_UID_TYPE_MID, i,
					      static_metrics->PublicSerialNumber_MID[i]);
		}
		n = ARRAY_SIZE(static_metrics->PublicSerialNumber_AID);
		for (i = 0; i < n; i++) {
			amdgpu_device_set_uid(smu->adev->uid_info, AMDGPU_UID_TYPE_AID, i,
					      static_metrics->PublicSerialNumber_AID[i]);
		}
		n = ARRAY_SIZE(static_metrics->PublicSerialNumber_XCD);
		for (i = 0; i < n; i++) {
			amdgpu_device_set_uid(smu->adev->uid_info, AMDGPU_UID_TYPE_XCD, i,
					      static_metrics->PublicSerialNumber_XCD[i]);
		}

		ret = smu_v15_0_8_fru_get_product_info(smu, static_metrics);
		if (ret)
			return ret;
		pptable->PPT1Max = static_metrics->PPT1Max;
		pptable->PPT1Min = static_metrics->PPT1Min;
		pptable->PPT1Default = static_metrics->PPT1Default;

		if (static_metrics->pldmVersion[0] != 0xFFFFFFFF)
			smu->adev->firmware.pldm_version =
				static_metrics->pldmVersion[0];
		dpm_context->board_volt = static_metrics->InputTelemetryVoltageInmV;
		smu_v15_0_8_init_xgmi_data(smu, static_metrics);
		pptable->init = true;
	}

	return 0;
}

static int smu_v15_0_8_set_default_dpm_table(struct smu_context *smu)
{
	int ret;

	ret = smu_v15_0_8_set_driver_pptable(smu);
	if (ret)
		return ret;

	ret = smu_v15_0_8_set_dpm_table(smu);
	if (ret)
		return ret;

	return 0;
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

static ssize_t smu_v15_0_8_get_pm_metrics(struct smu_context *smu,
					  void *metrics, size_t max_size)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct amdgpu_pm_metrics *pm_metrics = (struct amdgpu_pm_metrics *)metrics;
	uint32_t table_version = smu_table->tables[SMU_TABLE_SMU_METRICS].version;
	uint32_t table_size = smu_table->tables[SMU_TABLE_SMU_METRICS].size;
	uint32_t pmfw_version;
	int ret;

	if (!pm_metrics || !max_size)
		return -EINVAL;

	if (max_size < (table_size + sizeof(pm_metrics->common_header)))
		return -EOVERFLOW;

	/* Don't use cached metrics data */
	ret = smu_v15_0_8_get_metrics_table_internal(smu, 0, pm_metrics->data);
	if (ret)
		return ret;

	smu_cmn_get_smc_version(smu, NULL, &pmfw_version);
	memset(&pm_metrics->common_header, 0, sizeof(pm_metrics->common_header));
	pm_metrics->common_header.mp1_ip_discovery_version =
		amdgpu_ip_version(smu->adev, MP1_HWIP, 0);
	pm_metrics->common_header.pmfw_version = pmfw_version;
	pm_metrics->common_header.pmmetrics_version = table_version;
	pm_metrics->common_header.structure_size =
		sizeof(pm_metrics->common_header) + table_size;

	return pm_metrics->common_header.structure_size;
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

static bool smu_v15_0_8_is_temp_metrics_supported(struct smu_context *smu,
						  enum smu_temp_metric_type type)
{
	switch (type) {
	case SMU_TEMP_METRIC_BASEBOARD:
		if (smu->adev->gmc.xgmi.physical_node_id == 0)
			return true;
		return false;
	case SMU_TEMP_METRIC_GPUBOARD:
		return true;
	default:
		return false;
	}
}

static void smu_v15_0_8_fill_baseboard_temp_metrics(
	struct smu_v15_0_8_baseboard_temp_metrics *baseboard_temp_metrics,
	const SystemMetricsTable_t *metrics)
{
	baseboard_temp_metrics->accumulation_counter = metrics->AccumulationCounter;
	baseboard_temp_metrics->label_version = metrics->LabelVersion;
	baseboard_temp_metrics->node_id = metrics->NodeIdentifier;

	baseboard_temp_metrics->system_temp_ubb_fpga =
		metrics->SystemTemperatures[SYSTEM_TEMP_UBB_FPGA];
	baseboard_temp_metrics->system_temp_ubb_front =
		metrics->SystemTemperatures[SYSTEM_TEMP_UBB_FRONT];
	baseboard_temp_metrics->system_temp_ubb_back =
		metrics->SystemTemperatures[SYSTEM_TEMP_UBB_BACK];
	baseboard_temp_metrics->system_temp_ubb_oam7 =
		metrics->SystemTemperatures[SYSTEM_TEMP_UBB_OAM7];
	baseboard_temp_metrics->system_temp_ubb_ibc =
		metrics->SystemTemperatures[SYSTEM_TEMP_UBB_IBC];
	baseboard_temp_metrics->system_temp_ubb_ufpga =
		metrics->SystemTemperatures[SYSTEM_TEMP_UBB_UFPGA];
	baseboard_temp_metrics->system_temp_ubb_oam1 =
		metrics->SystemTemperatures[SYSTEM_TEMP_UBB_OAM1];
	baseboard_temp_metrics->system_temp_oam_0_1_hsc =
		metrics->SystemTemperatures[SYSTEM_TEMP_OAM_0_1_HSC];
	baseboard_temp_metrics->system_temp_oam_2_3_hsc =
		metrics->SystemTemperatures[SYSTEM_TEMP_OAM_2_3_HSC];
	baseboard_temp_metrics->system_temp_oam_4_5_hsc =
		metrics->SystemTemperatures[SYSTEM_TEMP_OAM_4_5_HSC];
	baseboard_temp_metrics->system_temp_oam_6_7_hsc =
		metrics->SystemTemperatures[SYSTEM_TEMP_OAM_6_7_HSC];
	baseboard_temp_metrics->system_temp_ubb_fpga_0v72_vr =
		metrics->SystemTemperatures[SYSTEM_TEMP_UBB_FPGA_0V72_VR];
	baseboard_temp_metrics->system_temp_ubb_fpga_3v3_vr =
		metrics->SystemTemperatures[SYSTEM_TEMP_UBB_FPGA_3V3_VR];
	baseboard_temp_metrics->system_temp_retimer_0_1_2_3_1v2_vr =
		metrics->SystemTemperatures[SYSTEM_TEMP_RETIMER_0_1_2_3_1V2_VR];
	baseboard_temp_metrics->system_temp_retimer_4_5_6_7_1v2_vr =
		metrics->SystemTemperatures[SYSTEM_TEMP_RETIMER_4_5_6_7_1V2_VR];
	baseboard_temp_metrics->system_temp_retimer_0_1_0v9_vr =
		metrics->SystemTemperatures[SYSTEM_TEMP_RETIMER_0_1_0V9_VR];
	baseboard_temp_metrics->system_temp_retimer_4_5_0v9_vr =
		metrics->SystemTemperatures[SYSTEM_TEMP_RETIMER_4_5_0V9_VR];
	baseboard_temp_metrics->system_temp_retimer_2_3_0v9_vr =
		metrics->SystemTemperatures[SYSTEM_TEMP_RETIMER_2_3_0V9_VR];
	baseboard_temp_metrics->system_temp_retimer_6_7_0v9_vr =
		metrics->SystemTemperatures[SYSTEM_TEMP_RETIMER_6_7_0V9_VR];
	baseboard_temp_metrics->system_temp_oam_0_1_2_3_3v3_vr =
		metrics->SystemTemperatures[SYSTEM_TEMP_OAM_0_1_2_3_3V3_VR];
	baseboard_temp_metrics->system_temp_oam_4_5_6_7_3v3_vr =
		metrics->SystemTemperatures[SYSTEM_TEMP_OAM_4_5_6_7_3V3_VR];
	baseboard_temp_metrics->system_temp_ibc_hsc =
		metrics->SystemTemperatures[SYSTEM_TEMP_IBC_HSC];
	baseboard_temp_metrics->system_temp_ibc =
		metrics->SystemTemperatures[SYSTEM_TEMP_IBC];
}

static void smu_v15_0_8_fill_gpuboard_temp_metrics(
	struct smu_v15_0_8_gpuboard_temp_metrics *gpuboard_temp_metrics,
	const SystemMetricsTable_t *metrics)
{
	gpuboard_temp_metrics->accumulation_counter = metrics->AccumulationCounter;
	gpuboard_temp_metrics->label_version = metrics->LabelVersion;
	gpuboard_temp_metrics->node_id = metrics->NodeIdentifier;

	gpuboard_temp_metrics->node_temp_retimer =
		metrics->NodeTemperatures[NODE_TEMP_RETIMER];
	gpuboard_temp_metrics->node_temp_ibc =
		metrics->NodeTemperatures[NODE_TEMP_IBC_TEMP];
	gpuboard_temp_metrics->node_temp_ibc_2 =
		metrics->NodeTemperatures[NODE_TEMP_IBC_2_TEMP];
	gpuboard_temp_metrics->node_temp_vdd18_vr =
		metrics->NodeTemperatures[NODE_TEMP_VDD18_VR_TEMP];
	gpuboard_temp_metrics->node_temp_04_hbm_b_vr =
		metrics->NodeTemperatures[NODE_TEMP_04_HBM_B_VR_TEMP];
	gpuboard_temp_metrics->node_temp_04_hbm_d_vr =
		metrics->NodeTemperatures[NODE_TEMP_04_HBM_D_VR_TEMP];

	gpuboard_temp_metrics->vr_temp_vddcr_socio_a =
		metrics->VrTemperatures[SVI_PLANE_VDDCR_SOCIO_A_TEMP];
	gpuboard_temp_metrics->vr_temp_vddcr_socio_c =
		metrics->VrTemperatures[SVI_PLANE_VDDCR_SOCIO_C_TEMP];
	gpuboard_temp_metrics->vr_temp_vddcr_x0 =
		metrics->VrTemperatures[SVI_PLANE_VDDCR_X0_TEMP];
	gpuboard_temp_metrics->vr_temp_vddcr_x1 =
		metrics->VrTemperatures[SVI_PLANE_VDDCR_X1_TEMP];
	gpuboard_temp_metrics->vr_temp_vddio_hbm_b =
		metrics->VrTemperatures[SVI_PLANE_VDDIO_HBM_B_TEMP];
	gpuboard_temp_metrics->vr_temp_vddio_hbm_d =
		metrics->VrTemperatures[SVI_PLANE_VDDIO_HBM_D_TEMP];
	gpuboard_temp_metrics->vr_temp_vddio_04_hbm_b =
		metrics->VrTemperatures[SVI_PLANE_VDDIO_04_HBM_B_TEMP];
	gpuboard_temp_metrics->vr_temp_vddio_04_hbm_d =
		metrics->VrTemperatures[SVI_PLANE_VDDIO_04_HBM_D_TEMP];
	gpuboard_temp_metrics->vr_temp_vddcr_hbm_b =
		metrics->VrTemperatures[SVI_PLANE_VDDCR_HBM_B_TEMP];
	gpuboard_temp_metrics->vr_temp_vddcr_hbm_d =
		metrics->VrTemperatures[SVI_PLANE_VDDCR_HBM_D_TEMP];
	gpuboard_temp_metrics->vr_temp_vddcr_075_hbm_b =
		metrics->VrTemperatures[SVI_PLANE_VDDCR_075_HBM_B_TEMP];
	gpuboard_temp_metrics->vr_temp_vddcr_075_hbm_d =
		metrics->VrTemperatures[SVI_PLANE_VDDCR_075_HBM_D_TEMP];
	gpuboard_temp_metrics->vr_temp_vddio_11_gta_a =
		metrics->VrTemperatures[SVI_PLANE_VDDIO_11_GTA_A_TEMP];
	gpuboard_temp_metrics->vr_temp_vddio_11_gta_c =
		metrics->VrTemperatures[SVI_PLANE_VDDIO_11_GTA_C_TEMP];
	gpuboard_temp_metrics->vr_temp_vddan_075_gta_a =
		metrics->VrTemperatures[SVI_PLANE_VDDAN_075_GTA_A_TEMP];
	gpuboard_temp_metrics->vr_temp_vddan_075_gta_c =
		metrics->VrTemperatures[SVI_PLANE_VDDAN_075_GTA_C_TEMP];
	gpuboard_temp_metrics->vr_temp_vddcr_075_ucie =
		metrics->VrTemperatures[SVI_PLANE_VDDCR_075_UCIE_TEMP];
	gpuboard_temp_metrics->vr_temp_vddio_065_ucieaa =
		metrics->VrTemperatures[SVI_PLANE_VDDIO_065_UCIEAA_TEMP];
	gpuboard_temp_metrics->vr_temp_vddio_065_ucieam_a =
		metrics->VrTemperatures[SVI_PLANE_VDDIO_065_UCIEAM_A_TEMP];
	gpuboard_temp_metrics->vr_temp_vddio_065_ucieam_c =
		metrics->VrTemperatures[SVI_PLANE_VDDIO_065_UCIEAM_C_TEMP];
	gpuboard_temp_metrics->vr_temp_vddan_075 =
		metrics->VrTemperatures[SVI_PLANE_VDDAN_075_TEMP];
}

static ssize_t smu_v15_0_8_get_temp_metrics(struct smu_context *smu,
					    enum smu_temp_metric_type type,
					    void *table)
{
	struct smu_v15_0_8_baseboard_temp_metrics *baseboard_temp_metrics;
	struct smu_v15_0_8_gpuboard_temp_metrics *gpuboard_temp_metrics;
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_table *tables = smu_table->tables;
	SystemMetricsTable_t *metrics;
	struct smu_table *sys_table;
	int ret;

	ret = smu_v15_0_8_get_system_metrics_table(smu);
	if (ret)
		return ret;

	sys_table = &tables[SMU_TABLE_PMFW_SYSTEM_METRICS];
	metrics = (SystemMetricsTable_t *)sys_table->cache.buffer;

	switch (type) {
	case SMU_TEMP_METRIC_GPUBOARD:
		gpuboard_temp_metrics =
			(struct smu_v15_0_8_gpuboard_temp_metrics *)
			smu_driver_table_ptr(smu, SMU_DRIVER_TABLE_GPUBOARD_TEMP_METRICS);
		smu_driver_table_update_cache_time(smu, SMU_DRIVER_TABLE_GPUBOARD_TEMP_METRICS);
		smu_v15_0_8_fill_gpuboard_temp_metrics(gpuboard_temp_metrics,
						       metrics);
		memcpy(table, gpuboard_temp_metrics, sizeof(*gpuboard_temp_metrics));
		return sizeof(*gpuboard_temp_metrics);
	case SMU_TEMP_METRIC_BASEBOARD:
		baseboard_temp_metrics =
			(struct smu_v15_0_8_baseboard_temp_metrics *)
			smu_driver_table_ptr(smu, SMU_DRIVER_TABLE_BASEBOARD_TEMP_METRICS);
		smu_driver_table_update_cache_time(smu, SMU_DRIVER_TABLE_BASEBOARD_TEMP_METRICS);
		smu_v15_0_8_fill_baseboard_temp_metrics(baseboard_temp_metrics,
							metrics);
		memcpy(table, baseboard_temp_metrics, sizeof(*baseboard_temp_metrics));
		return sizeof(*baseboard_temp_metrics);
	default:
		return -EINVAL;
	}
}

static ssize_t smu_v15_0_8_get_gpu_metrics(struct smu_context *smu, void **table)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	struct smu_v15_0_8_gpu_metrics *gpu_metrics;
	struct amdgpu_device *adev = smu->adev;
	int ret = 0, xcc_id, inst, i, j, idx;
	uint32_t aid_mask = adev->aid_mask;
	uint32_t mid_mask = adev->aid_mask;
	MetricsTable_t *metrics;

	metrics = kzalloc(sizeof(MetricsTable_t), GFP_KERNEL);

	ret = smu_v15_0_8_get_metrics_table_internal(smu, 1, NULL);
	if (ret)
		return ret;

	metrics = (MetricsTable_t *)smu_table->metrics_table;
	gpu_metrics = (struct smu_v15_0_8_gpu_metrics *)smu_driver_table_ptr(smu,
		       SMU_DRIVER_TABLE_GPU_METRICS);

	gpu_metrics->system_clock_counter = ktime_get_boottime_ns();

	gpu_metrics->temperature_hotspot = SMUQ10_ROUND(metrics->MaxSocketTemperature);

	/* Per-HBM stack temperatures */
	if (adev->umc.active_mask) {
		u64 mask = adev->umc.active_mask;
		int out_idx = 0;
		int stack_idx;

		if (unlikely(hweight64(mask)/4 > SMU_15_0_8_MAX_HBM_STACKS))
			dev_warn(adev->dev, "Invalid umc mask %lld\n", mask);
		else  {
			for_each_hbm_stack(stack_idx, mask) {
				if (!hbm_stack_mask_valid(mask))
					continue;
				gpu_metrics->temperature_hbm[out_idx++] =
					SMUQ10_ROUND(metrics->HbmTemperature[stack_idx]);
			}
		}
	}

	/* Reports max temperature of all voltage rails */
	gpu_metrics->temperature_vrsoc = SMUQ10_ROUND(metrics->MaxVrTemperature);
	/* MID, AID, XCD temperatures */
	idx = 0;
	for_each_inst(i, mid_mask) {
		gpu_metrics->temperature_mid[idx] = SMUQ10_ROUND(metrics->MidTemperature[i]);
		idx++;
	}

	idx = 0;
	for_each_inst(i, aid_mask) {
		gpu_metrics->temperature_aid[idx] = SMUQ10_ROUND(metrics->AidTemperature[i]);
		idx++;
	}

	for (i = 0; i < NUM_XCC(adev->gfx.xcc_mask); ++i) {
		xcc_id = GET_INST(GC, i);
		if (xcc_id >= 0)
			gpu_metrics->temperature_xcd[i] = SMUQ10_ROUND(metrics->XcdTemperature[xcc_id]);
	}
	/* Power */
	gpu_metrics->curr_socket_power = SMUQ10_ROUND(metrics->SocketPower);

	gpu_metrics->average_gfx_activity = SMUQ10_ROUND(metrics->SocketGfxBusy);
	gpu_metrics->average_umc_activity = SMUQ10_ROUND(metrics->DramBandwidthUtilization);
	gpu_metrics->mem_max_bandwidth = SMUQ10_ROUND(metrics->MaxDramBandwidth);

	/* Energy counter reported in 15.259uJ (2^-16) units */
	gpu_metrics->energy_accumulator = metrics->SocketEnergyAcc;

	for (i = 0; i < NUM_XCC(adev->gfx.xcc_mask); ++i) {
		xcc_id = GET_INST(GC, i);
		if (xcc_id >= 0) {
			gpu_metrics->current_gfxclk[i] =
				SMUQ10_ROUND(metrics->GfxclkFrequency[xcc_id]);
		}
	}

	/* Per-MID clocks */
	idx = 0;
	for_each_inst(i, mid_mask) {
		gpu_metrics->current_socclk[idx] = SMUQ10_ROUND(metrics->SocclkFrequency[i]);
		idx++;
	}

	/* Per-VCN clocks */
	for (i = 0; i < adev->vcn.num_vcn_inst; ++i) {
		inst = GET_INST(VCN, i);
		if (inst >= 0) {
			gpu_metrics->current_vclk0[i] = SMUQ10_ROUND(metrics->VclkFrequency[inst]);
			gpu_metrics->current_dclk0[i] = SMUQ10_ROUND(metrics->DclkFrequency[inst]);
		}
	}

	/* Per-AID clocks */
	idx = 0;
	for_each_inst(i, aid_mask) {
		gpu_metrics->current_uclk[idx] = SMUQ10_ROUND(metrics->UclkFrequency[i]);
		idx++;
	}

	/* Total accumulated cycle counter */
	gpu_metrics->accumulation_counter = metrics->AccumulationCounter;

	/* Accumulated throttler residencies */
	gpu_metrics->prochot_residency_acc = metrics->ProchotResidencyAcc;
	gpu_metrics->ppt_residency_acc = metrics->PptResidencyAcc;
	gpu_metrics->socket_thm_residency_acc = metrics->SocketThmResidencyAcc;
	gpu_metrics->vr_thm_residency_acc = metrics->VrThmResidencyAcc;
	gpu_metrics->hbm_thm_residency_acc = metrics->HbmThmResidencyAcc;

	gpu_metrics->gfx_activity_acc = SMUQ10_ROUND(metrics->SocketGfxBusyAcc);
	gpu_metrics->mem_activity_acc = SMUQ10_ROUND(metrics->DramBandwidthUtilizationAcc);

	for (i = 0; i < NUM_XGMI_LINKS; i++) {
		j = amdgpu_xgmi_get_ext_link(adev, i);
		if (j < 0 || j >= NUM_XGMI_LINKS)
			continue;
		ret = amdgpu_get_xgmi_link_status(adev, i);
		if (ret >= 0)
			gpu_metrics->xgmi_link_status[j] = ret;
	}

	gpu_metrics->xgmi_read_data_acc = SMUQ10_ROUND(metrics->XgmiReadBandwidthAcc);
	gpu_metrics->xgmi_write_data_acc = SMUQ10_ROUND(metrics->XgmiWriteBandwidthAcc);

	for (i = 0; i < NUM_XCC(adev->gfx.xcc_mask); ++i) {
		inst = GET_INST(GC, i);
		gpu_metrics->gfx_busy_inst[i] = SMUQ10_ROUND(metrics->GfxBusy[inst]);
		gpu_metrics->gfx_busy_acc[i] = SMUQ10_ROUND(metrics->GfxBusyAcc[inst]);
		gpu_metrics->gfx_below_host_limit_ppt_acc[i] =
			SMUQ10_ROUND(metrics->GfxclkBelowHostLimitPptAcc[inst]);
		gpu_metrics->gfx_below_host_limit_thm_acc[i] =
			SMUQ10_ROUND(metrics->GfxclkBelowHostLimitThmAcc[inst]);
		gpu_metrics->gfx_low_utilization_acc[i] =
			SMUQ10_ROUND(metrics->GfxclkLowUtilizationAcc[inst]);
		gpu_metrics->gfx_below_host_limit_total_acc[i] =
			SMUQ10_ROUND(metrics->GfxclkBelowHostLimitTotalAcc[inst]);
	}

	gpu_metrics->xgmi_link_width = metrics->XgmiWidth;
	gpu_metrics->xgmi_link_speed = metrics->XgmiBitrate;

	gpu_metrics->firmware_timestamp = metrics->Timestamp;

	*table = gpu_metrics;

	smu_driver_table_update_cache_time(smu, SMU_DRIVER_TABLE_GPU_METRICS);

	return sizeof(*gpu_metrics);
}

static void smu_v15_0_8_get_unique_id(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	struct smu_table_context *smu_table = &smu->smu_table;
	PPTable_t *pptable = (PPTable_t *)smu_table->driver_pptable;

	adev->unique_id = pptable->PublicSerialNumberMID;
}

static int smu_v15_0_8_get_power_limit(struct smu_context *smu,
				       uint32_t *current_power_limit,
				       uint32_t *default_power_limit,
				       uint32_t *max_power_limit,
				       uint32_t *min_power_limit)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	PPTable_t *pptable = (PPTable_t *)smu_table->driver_pptable;
	uint32_t power_limit = 0;
	int ret;

	ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetPptLimit, &power_limit);
	if (ret) {
		dev_err(smu->adev->dev, "Couldn't get PPT limit");
		return -EINVAL;
	}

	if (current_power_limit)
		*current_power_limit = power_limit;

	if (default_power_limit)
		*max_power_limit = pptable->MaxSocketPowerLimit;

	if (max_power_limit)
		*max_power_limit = pptable->MaxSocketPowerLimit;

	if (min_power_limit)
		*min_power_limit = 0;

	return 0;
}

static int smu_v15_0_8_populate_umd_state_clk(struct smu_context *smu)
{
	struct smu_15_0_dpm_context *dpm_context = smu->smu_dpm.dpm_context;
	struct smu_dpm_table *gfx_table = &dpm_context->dpm_tables.gfx_table;
	struct smu_dpm_table *mem_table = &dpm_context->dpm_tables.uclk_table;
	struct smu_umd_pstate_table *pstate_table = &smu->pstate_table;

	pstate_table->gfxclk_pstate.curr.min = SMU_DPM_TABLE_MIN(gfx_table);
	pstate_table->gfxclk_pstate.curr.max = SMU_DPM_TABLE_MAX(gfx_table);

	pstate_table->uclk_pstate.curr.min = SMU_DPM_TABLE_MIN(mem_table);
	pstate_table->uclk_pstate.curr.max = SMU_DPM_TABLE_MAX(mem_table);
	return 0;
}

static int smu_v15_0_8_set_gfx_soft_freq_limited_range(struct smu_context *smu,
						       uint32_t min,
						       uint32_t max)
{
	int ret;

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMaxGfxClk,
					      max & 0xffff, NULL);
	if (ret)
		return ret;

	ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetSoftMinGfxclk,
					      min & 0xffff, NULL);

	return ret;
}

static int smu_v15_0_8_set_performance_level(struct smu_context *smu,
					     enum amd_dpm_forced_level level)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_15_0_dpm_context *dpm_context = smu_dpm->dpm_context;
	struct smu_dpm_table *gfx_table = &dpm_context->dpm_tables.gfx_table;
	struct smu_dpm_table *uclk_table = &dpm_context->dpm_tables.uclk_table;
	struct smu_umd_pstate_table *pstate_table = &smu->pstate_table;
	int ret;

	switch (level) {
	case AMD_DPM_FORCED_LEVEL_PERF_DETERMINISM:
		/* Determinism not supported on SMU v15.0.8 */
		ret = -EOPNOTSUPP;
		break;

	case AMD_DPM_FORCED_LEVEL_AUTO:
		/* Restore GFXCLK to default range */
		if ((SMU_DPM_TABLE_MIN(gfx_table) !=
		     pstate_table->gfxclk_pstate.curr.min) ||
		    (SMU_DPM_TABLE_MAX(gfx_table) !=
		     pstate_table->gfxclk_pstate.curr.max)) {
			ret = smu_v15_0_8_set_gfx_soft_freq_limited_range(
				smu, SMU_DPM_TABLE_MIN(gfx_table),
				SMU_DPM_TABLE_MAX(gfx_table));
			if (ret)
				goto out;

			pstate_table->gfxclk_pstate.curr.min =
				SMU_DPM_TABLE_MIN(gfx_table);
			pstate_table->gfxclk_pstate.curr.max =
				SMU_DPM_TABLE_MAX(gfx_table);
		}

		/* Restore UCLK to default max */
		if (SMU_DPM_TABLE_MAX(uclk_table) !=
		    pstate_table->uclk_pstate.curr.max) {
			/* Min UCLK is not expected to be changed */
			ret = smu_v15_0_set_soft_freq_limited_range(smu,
								    SMU_UCLK, 0,
								    SMU_DPM_TABLE_MAX(uclk_table),
								    false);
			if (ret)
				goto out;

			pstate_table->uclk_pstate.curr.max =
				SMU_DPM_TABLE_MAX(uclk_table);
		}

		if (ret)
			goto out;

		smu_cmn_reset_custom_level(smu);

		break;
	case AMD_DPM_FORCED_LEVEL_MANUAL:
		ret = 0;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

out:
	return ret;
}

static int smu_v15_0_8_set_soft_freq_limited_range(struct smu_context *smu,
						   enum smu_clk_type clk_type,
						   uint32_t min, uint32_t max,
						   bool automatic)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_umd_pstate_table *pstate_table = &smu->pstate_table;
	int ret = 0;

	if (clk_type != SMU_GFXCLK && clk_type != SMU_SCLK &&
	    clk_type != SMU_UCLK)
		return -EINVAL;

	if (smu_dpm->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL)
		return -EINVAL;

	if (smu_dpm->dpm_level == AMD_DPM_FORCED_LEVEL_MANUAL) {
		if (min >= max) {
			dev_err(smu->adev->dev,
				"Minimum clk should be less than the maximum allowed clock\n");
			return -EINVAL;
		}

		if (clk_type == SMU_GFXCLK || clk_type == SMU_SCLK) {
			if ((min == pstate_table->gfxclk_pstate.curr.min) &&
			    (max == pstate_table->gfxclk_pstate.curr.max))
				return 0;

			ret = smu_v15_0_8_set_gfx_soft_freq_limited_range(smu,
									  min, max);
			if (!ret) {
				pstate_table->gfxclk_pstate.curr.min = min;
				pstate_table->gfxclk_pstate.curr.max = max;
			}
		}

		if (clk_type == SMU_UCLK) {
			if (max == pstate_table->uclk_pstate.curr.max)
				return 0;

			ret = smu_v15_0_set_soft_freq_limited_range(smu,
								    SMU_UCLK,
								    0, max,
								    false);
			if (!ret)
				pstate_table->uclk_pstate.curr.max = max;
		}

		return ret;
	}

	return 0;
}

static int smu_v15_0_8_od_edit_dpm_table(struct smu_context *smu,
					 enum PP_OD_DPM_TABLE_COMMAND type,
					 long input[], uint32_t size)
{
	struct smu_dpm_context *smu_dpm = &smu->smu_dpm;
	struct smu_umd_pstate_table *pstate_table = &smu->pstate_table;
	struct smu_15_0_dpm_context *dpm_context = smu_dpm->dpm_context;
	uint32_t min_clk, max_clk;
	int ret;

	/* Only allowed in manual mode */
	if (smu_dpm->dpm_level != AMD_DPM_FORCED_LEVEL_MANUAL)
		return -EINVAL;

	switch (type) {
	case PP_OD_EDIT_SCLK_VDDC_TABLE:
		if (size != 2) {
			dev_err(smu->adev->dev,
				"Input parameter number not correct\n");
			return -EINVAL;
		}
		min_clk = SMU_DPM_TABLE_MIN(&dpm_context->dpm_tables.gfx_table);
		max_clk = SMU_DPM_TABLE_MAX(&dpm_context->dpm_tables.gfx_table);
		if (input[0] == 0) {
			if (input[1] < min_clk) {
				dev_warn(smu->adev->dev,
					 "Minimum GFX clk (%ld) MHz specified is less than the minimum allowed (%d) MHz\n",
					input[1], min_clk);
				pstate_table->gfxclk_pstate.custom.min =
					pstate_table->gfxclk_pstate.curr.min;
				return -EINVAL;
			}

			pstate_table->gfxclk_pstate.custom.min = input[1];
		} else if (input[0] == 1) {
			if (input[1] > max_clk) {
				dev_warn(smu->adev->dev,
					 "Maximum GFX clk (%ld) MHz specified is greater than the maximum allowed (%d) MHz\n",
					input[1], max_clk);
				pstate_table->gfxclk_pstate.custom.max =
					pstate_table->gfxclk_pstate.curr.max;
				return -EINVAL;
			}

			pstate_table->gfxclk_pstate.custom.max = input[1];
		} else {
			return -EINVAL;
		}
		break;
	case PP_OD_EDIT_MCLK_VDDC_TABLE:
		if (size != 2) {
			dev_err(smu->adev->dev,
				"Input parameter number not correct\n");
			return -EINVAL;
		}

		if (!smu_cmn_feature_is_enabled(smu, SMU_FEATURE_DPM_UCLK_BIT)) {
			dev_warn(smu->adev->dev,
				 "UCLK_LIMITS setting not supported!\n");
			return -EOPNOTSUPP;
		}
		max_clk = SMU_DPM_TABLE_MAX(&dpm_context->dpm_tables.uclk_table);
		if (input[0] == 0) {
			dev_info(smu->adev->dev,
				 "Setting min UCLK level is not supported");
			return -EINVAL;
		} else if (input[0] == 1) {
			if (input[1] > max_clk) {
				dev_warn(smu->adev->dev,
					 "Maximum UCLK (%ld) MHz specified is greater than the maximum allowed (%d) MHz\n",
					input[1], max_clk);
				pstate_table->uclk_pstate.custom.max =
					pstate_table->uclk_pstate.curr.max;

				return -EINVAL;
			}

			pstate_table->uclk_pstate.custom.max = input[1];
		}
		break;
	case PP_OD_RESTORE_DEFAULT_TABLE:
		if (size != 0) {
			dev_err(smu->adev->dev,
				"Input parameter number not correct\n");
			return -EINVAL;
		}

		/* Use the default frequencies for manual mode */
		min_clk = SMU_DPM_TABLE_MIN(&dpm_context->dpm_tables.gfx_table);
		max_clk = SMU_DPM_TABLE_MAX(&dpm_context->dpm_tables.gfx_table);

		ret = smu_v15_0_8_set_soft_freq_limited_range(smu,
							      SMU_GFXCLK,
							      min_clk, max_clk,
							      false);
		if (ret)
			return ret;

		min_clk = SMU_DPM_TABLE_MIN(&dpm_context->dpm_tables.uclk_table);
		max_clk = SMU_DPM_TABLE_MAX(&dpm_context->dpm_tables.uclk_table);
		ret = smu_v15_0_8_set_soft_freq_limited_range(smu,
							      SMU_UCLK,
							      min_clk, max_clk,
							      false);
		if (ret)
			return ret;

		smu_cmn_reset_custom_level(smu);
		break;
	case PP_OD_COMMIT_DPM_TABLE:
		if (size != 0) {
			dev_err(smu->adev->dev,
				"Input parameter number not correct\n");
			return -EINVAL;
		}

		if (!pstate_table->gfxclk_pstate.custom.min)
			pstate_table->gfxclk_pstate.custom.min =
				pstate_table->gfxclk_pstate.curr.min;

		if (!pstate_table->gfxclk_pstate.custom.max)
			pstate_table->gfxclk_pstate.custom.max =
				pstate_table->gfxclk_pstate.curr.max;

		min_clk = pstate_table->gfxclk_pstate.custom.min;
		max_clk = pstate_table->gfxclk_pstate.custom.max;

		ret = smu_v15_0_8_set_soft_freq_limited_range(smu,
							      SMU_GFXCLK,
							      min_clk, max_clk,
							      false);
		if (ret)
			return ret;

		/* Commit UCLK custom range (only max supported) */
		if (pstate_table->uclk_pstate.custom.max) {
			min_clk = pstate_table->uclk_pstate.curr.min;
			max_clk = pstate_table->uclk_pstate.custom.max;
			ret = smu_v15_0_8_set_soft_freq_limited_range(smu,
								      SMU_UCLK,
								      min_clk, max_clk,
								      false);
			if (ret)
				return ret;
		}

		break;
	default:
		return -ENOSYS;
	}

	return 0;
}

static int smu_v15_0_8_get_thermal_temperature_range(struct smu_context *smu,
						     struct smu_temperature_range *range)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	PPTable_t *pptable = (PPTable_t *)smu_table->driver_pptable;
	uint32_t max_ctf, max_thm;

	if (amdgpu_sriov_multi_vf_mode(smu->adev))
		return 0;

	if (!range)
		return -EINVAL;

	/* CTF (Critical Temperature Fault) limits */
	max_ctf = max3(pptable->CTFLimitMID, pptable->CTFLimitXCD,
		       pptable->CTFLimitAID);
	range->hotspot_emergency_max = max_ctf * SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;

	range->mem_emergency_max = pptable->CTFLimitHBM *
				   SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;

	/* Thermal throttling limits */
	max_thm = max3(pptable->ThermalLimitMID, pptable->ThermalLimitXCD,
		       pptable->ThermalLimitAID);
	range->hotspot_crit_max = max_thm * SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;

	range->mem_crit_max = pptable->ThermalLimitHBM *
			      SMU_TEMPERATURE_UNITS_PER_CENTIGRADES;

	return 0;
}

static int smu_v15_0_8_set_power_limit(struct smu_context *smu,
				       enum smu_ppt_limit_type limit_type,
				       uint32_t limit)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	PPTable_t *pptable = (PPTable_t *)smu_table->driver_pptable;
	int ret;

	if (limit_type == SMU_FAST_PPT_LIMIT) {
		if (!pptable->PPT1Max)
			return -EOPNOTSUPP;

		if (limit > pptable->PPT1Max || limit < pptable->PPT1Min) {
			dev_err(smu->adev->dev,
				"New PPT1 limit (%d) should be between min %d and max %d\n",
				limit, pptable->PPT1Min, pptable->PPT1Max);
			return -EINVAL;
		}

		ret = smu_cmn_send_smc_msg_with_param(smu, SMU_MSG_SetFastPptLimit,
						      limit, NULL);
		if (ret)
			dev_err(smu->adev->dev, "Set fast PPT limit failed!\n");

		return ret;
	}

	return smu_v15_0_set_power_limit(smu, limit_type, limit);
}

static int smu_v15_0_8_get_ppt_limit(struct smu_context *smu,
				     uint32_t *ppt_limit,
				     enum smu_ppt_limit_type type,
				     enum smu_ppt_limit_level level)
{
	struct smu_table_context *smu_table = &smu->smu_table;
	PPTable_t *pptable = (PPTable_t *)smu_table->driver_pptable;
	int ret = 0;

	if (!ppt_limit)
		return -EINVAL;

	if (type == SMU_FAST_PPT_LIMIT) {
		if (!pptable->PPT1Max)
			return -EOPNOTSUPP;

		switch (level) {
		case SMU_PPT_LIMIT_MAX:
			*ppt_limit = pptable->PPT1Max;
			break;
		case SMU_PPT_LIMIT_CURRENT:
			ret = smu_cmn_send_smc_msg(smu, SMU_MSG_GetFastPptLimit,
						   ppt_limit);
			if (ret)
				dev_err(smu->adev->dev,
					"Get fast PPT limit failed!\n");
			break;
		case SMU_PPT_LIMIT_DEFAULT:
			*ppt_limit = pptable->PPT1Default;
			break;
		case SMU_PPT_LIMIT_MIN:
			*ppt_limit = pptable->PPT1Min;
			break;
		default:
			return -EOPNOTSUPP;
		}
		return ret;
	}

	return -EOPNOTSUPP;
}

static const struct pptable_funcs smu_v15_0_8_ppt_funcs = {
	.init_allowed_features = smu_v15_0_8_init_allowed_features,
	.set_default_dpm_table = smu_v15_0_8_set_default_dpm_table,
	.is_dpm_running = smu_v15_0_8_is_dpm_running,
	.init_smc_tables = smu_v15_0_8_init_smc_tables,
	.fini_smc_tables = smu_v15_0_8_fini_smc_tables,
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
	.get_pm_metrics = smu_v15_0_8_get_pm_metrics,
	.mode2_reset = smu_v15_0_8_mode2_reset,
	.get_dpm_ultimate_freq = smu_v15_0_8_get_dpm_ultimate_freq,
	.get_gpu_metrics = smu_v15_0_8_get_gpu_metrics,
	.get_unique_id = smu_v15_0_8_get_unique_id,
	.get_power_limit = smu_v15_0_8_get_power_limit,
	.set_power_limit = smu_v15_0_8_set_power_limit,
	.get_ppt_limit = smu_v15_0_8_get_ppt_limit,
	.emit_clk_levels = smu_v15_0_8_emit_clk_levels,
	.read_sensor = smu_v15_0_8_read_sensor,
	.populate_umd_state_clk = smu_v15_0_8_populate_umd_state_clk,
	.set_performance_level = smu_v15_0_8_set_performance_level,
	.od_edit_dpm_table = smu_v15_0_8_od_edit_dpm_table,
	.get_thermal_temperature_range = smu_v15_0_8_get_thermal_temperature_range,
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

static const struct smu_temp_funcs smu_v15_0_8_temp_funcs = {
	.temp_metrics_is_supported = smu_v15_0_8_is_temp_metrics_supported,
	.get_temp_metrics = smu_v15_0_8_get_temp_metrics,
};

void smu_v15_0_8_set_ppt_funcs(struct smu_context *smu)
{
	smu->ppt_funcs = &smu_v15_0_8_ppt_funcs;
	smu->clock_map = smu_v15_0_8_clk_map;
	smu->feature_map = smu_v15_0_8_feature_mask_map;
	smu->table_map = smu_v15_0_8_table_map;
	smu_v15_0_8_init_msg_ctl(smu, smu_v15_0_8_message_map);
	smu->smu_temp.temp_funcs = &smu_v15_0_8_temp_funcs;
	smu->smc_driver_if_version = SMU15_DRIVER_IF_VERSION_SMU_V15_0_8;
}
