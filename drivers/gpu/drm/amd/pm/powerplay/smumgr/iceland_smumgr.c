/*
 * Copyright 2016 Advanced Micro Devices, Inc.
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
 * Author: Huang Rui <ray.huang@amd.com>
 *
 */
#include "pp_debug.h"
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/gfp.h>

#include "smumgr.h"
#include "iceland_smumgr.h"

#include "ppsmc.h"

#include "cgs_common.h"

#include "smu7_dyn_defaults.h"
#include "smu7_hwmgr.h"
#include "hardwaremanager.h"
#include "ppatomctrl.h"
#include "atombios.h"
#include "pppcielanes.h"
#include "pp_endian.h"
#include "processpptables.h"


#include "smu/smu_7_1_1_d.h"
#include "smu/smu_7_1_1_sh_mask.h"
#include "smu71_discrete.h"

#include "smu_ucode_xfer_vi.h"
#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"
#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"
#include "dce/dce_10_0_d.h"
#include "dce/dce_10_0_sh_mask.h"


#define ICELAND_SMC_SIZE               0x20000

#define POWERTUNE_DEFAULT_SET_MAX    1
#define MC_CG_ARB_FREQ_F1           0x0b
#define VDDC_VDDCI_DELTA            200

#define DEVICE_ID_VI_ICELAND_M_6900	0x6900
#define DEVICE_ID_VI_ICELAND_M_6901	0x6901
#define DEVICE_ID_VI_ICELAND_M_6902	0x6902
#define DEVICE_ID_VI_ICELAND_M_6903	0x6903

static const struct iceland_pt_defaults defaults_iceland = {
	/*
	 * sviLoadLIneEn, SviLoadLineVddC, TDC_VDDC_ThrottleReleaseLimitPerc,
	 * TDC_MAWt, TdcWaterfallCtl, DTEAmbientTempBase, DisplayCac, BAPM_TEMP_GRADIENT
	 */
	1, 0xF, 0xFD, 0x19, 5, 45, 0, 0xB0000,
	{ 0x79,  0x253, 0x25D, 0xAE,  0x72,  0x80,  0x83,  0x86,  0x6F,  0xC8,  0xC9,  0xC9,  0x2F,  0x4D,  0x61  },
	{ 0x17C, 0x172, 0x180, 0x1BC, 0x1B3, 0x1BD, 0x206, 0x200, 0x203, 0x25D, 0x25A, 0x255, 0x2C3, 0x2C5, 0x2B4 }
};

/* 35W - XT, XTL */
static const struct iceland_pt_defaults defaults_icelandxt = {
	/*
	 * sviLoadLIneEn, SviLoadLineVddC,
	 * TDC_VDDC_ThrottleReleaseLimitPerc, TDC_MAWt,
	 * TdcWaterfallCtl, DTEAmbientTempBase, DisplayCac,
	 * BAPM_TEMP_GRADIENT
	 */
	1, 0xF, 0xFD, 0x19, 5, 45, 0, 0x0,
	{ 0xA7,  0x0, 0x0, 0xB5,  0x0, 0x0, 0x9F,  0x0, 0x0, 0xD6,  0x0, 0x0, 0xD7,  0x0, 0x0},
	{ 0x1EA, 0x0, 0x0, 0x224, 0x0, 0x0, 0x25E, 0x0, 0x0, 0x28E, 0x0, 0x0, 0x2AB, 0x0, 0x0}
};

/* 25W - PRO, LE */
static const struct iceland_pt_defaults defaults_icelandpro = {
	/*
	 * sviLoadLIneEn, SviLoadLineVddC,
	 * TDC_VDDC_ThrottleReleaseLimitPerc, TDC_MAWt,
	 * TdcWaterfallCtl, DTEAmbientTempBase, DisplayCac,
	 * BAPM_TEMP_GRADIENT
	 */
	1, 0xF, 0xFD, 0x19, 5, 45, 0, 0x0,
	{ 0xB7,  0x0, 0x0, 0xC3,  0x0, 0x0, 0xB5,  0x0, 0x0, 0xEA,  0x0, 0x0, 0xE6,  0x0, 0x0},
	{ 0x1EA, 0x0, 0x0, 0x224, 0x0, 0x0, 0x25E, 0x0, 0x0, 0x28E, 0x0, 0x0, 0x2AB, 0x0, 0x0}
};

static int iceland_start_smc(struct pp_hwmgr *hwmgr)
{
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				  SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	return 0;
}

static void iceland_reset_smc(struct pp_hwmgr *hwmgr)
{
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				  SMC_SYSCON_RESET_CNTL,
				  rst_reg, 1);
}


static void iceland_stop_smc_clock(struct pp_hwmgr *hwmgr)
{
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				  SMC_SYSCON_CLOCK_CNTL_0,
				  ck_disable, 1);
}

static void iceland_start_smc_clock(struct pp_hwmgr *hwmgr)
{
	PHM_WRITE_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC,
				  SMC_SYSCON_CLOCK_CNTL_0,
				  ck_disable, 0);
}

static int iceland_smu_start_smc(struct pp_hwmgr *hwmgr)
{
	/* set smc instruct start point at 0x0 */
	smu7_program_jump_on_start(hwmgr);

	/* enable smc clock */
	iceland_start_smc_clock(hwmgr);

	/* de-assert reset */
	iceland_start_smc(hwmgr);

	PHM_WAIT_INDIRECT_FIELD(hwmgr, SMC_IND, FIRMWARE_FLAGS,
				 INTERRUPTS_ENABLED, 1);

	return 0;
}


static int iceland_upload_smc_firmware_data(struct pp_hwmgr *hwmgr,
					uint32_t length, const uint8_t *src,
					uint32_t limit, uint32_t start_addr)
{
	uint32_t byte_count = length;
	uint32_t data;

	PP_ASSERT_WITH_CODE((limit >= byte_count), "SMC address is beyond the SMC RAM area.", return -EINVAL);

	cgs_write_register(hwmgr->device, mmSMC_IND_INDEX_0, start_addr);
	PHM_WRITE_FIELD(hwmgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_0, 1);

	while (byte_count >= 4) {
		data = src[0] * 0x1000000 + src[1] * 0x10000 + src[2] * 0x100 + src[3];
		cgs_write_register(hwmgr->device, mmSMC_IND_DATA_0, data);
		src += 4;
		byte_count -= 4;
	}

	PHM_WRITE_FIELD(hwmgr->device, SMC_IND_ACCESS_CNTL, AUTO_INCREMENT_IND_0, 0);

	PP_ASSERT_WITH_CODE((0 == byte_count), "SMC size must be divisible by 4.", return -EINVAL);

	return 0;
}


static int iceland_smu_upload_firmware_image(struct pp_hwmgr *hwmgr)
{
	uint32_t val;
	struct cgs_firmware_info info = {0};

	if (hwmgr == NULL || hwmgr->device == NULL)
		return -EINVAL;

	/* load SMC firmware */
	cgs_get_firmware_info(hwmgr->device,
		smu7_convert_fw_type_to_cgs(UCODE_ID_SMU), &info);

	if (info.image_size & 3) {
		pr_err("[ powerplay ] SMC ucode is not 4 bytes aligned\n");
		return -EINVAL;
	}

	if (info.image_size > ICELAND_SMC_SIZE) {
		pr_err("[ powerplay ] SMC address is beyond the SMC RAM area\n");
		return -EINVAL;
	}
	hwmgr->smu_version = info.version;
	/* wait for smc boot up */
	PHM_WAIT_INDIRECT_FIELD_UNEQUAL(hwmgr, SMC_IND,
					 RCU_UC_EVENTS, boot_seq_done, 0);

	/* clear firmware interrupt enable flag */
	val = cgs_read_ind_register(hwmgr->device, CGS_IND_REG__SMC,
				    ixSMC_SYSCON_MISC_CNTL);
	cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			       ixSMC_SYSCON_MISC_CNTL, val | 1);

	/* stop smc clock */
	iceland_stop_smc_clock(hwmgr);

	/* reset smc */
	iceland_reset_smc(hwmgr);
	iceland_upload_smc_firmware_data(hwmgr, info.image_size,
				(uint8_t *)info.kptr, ICELAND_SMC_SIZE,
				info.ucode_start_address);

	return 0;
}

static int iceland_request_smu_load_specific_fw(struct pp_hwmgr *hwmgr,
						uint32_t firmwareType)
{
	return 0;
}

static int iceland_start_smu(struct pp_hwmgr *hwmgr)
{
	struct iceland_smumgr *priv = hwmgr->smu_backend;
	int result;

	if (!smu7_is_smc_ram_running(hwmgr)) {
		result = iceland_smu_upload_firmware_image(hwmgr);
		if (result)
			return result;

		iceland_smu_start_smc(hwmgr);
	}

	/* Setup SoftRegsStart here to visit the register UcodeLoadStatus
	 * to check fw loading state
	 */
	smu7_read_smc_sram_dword(hwmgr,
			SMU71_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU71_Firmware_Header, SoftRegisters),
			&(priv->smu7_data.soft_regs_start), 0x40000);

	result = smu7_request_smu_load_fw(hwmgr);

	return result;
}

static int iceland_smu_init(struct pp_hwmgr *hwmgr)
{
	struct iceland_smumgr *iceland_priv = NULL;

	iceland_priv = kzalloc(sizeof(struct iceland_smumgr), GFP_KERNEL);

	if (iceland_priv == NULL)
		return -ENOMEM;

	hwmgr->smu_backend = iceland_priv;

	if (smu7_init(hwmgr)) {
		kfree(iceland_priv);
		return -EINVAL;
	}

	return 0;
}


static void iceland_initialize_power_tune_defaults(struct pp_hwmgr *hwmgr)
{
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	struct amdgpu_device *adev = hwmgr->adev;
	uint32_t dev_id;

	dev_id = adev->pdev->device;

	switch (dev_id) {
	case DEVICE_ID_VI_ICELAND_M_6900:
	case DEVICE_ID_VI_ICELAND_M_6903:
		smu_data->power_tune_defaults = &defaults_icelandxt;
		break;

	case DEVICE_ID_VI_ICELAND_M_6901:
	case DEVICE_ID_VI_ICELAND_M_6902:
		smu_data->power_tune_defaults = &defaults_icelandpro;
		break;
	default:
		smu_data->power_tune_defaults = &defaults_iceland;
		pr_warn("Unknown V.I. Device ID.\n");
		break;
	}
	return;
}

static int iceland_populate_svi_load_line(struct pp_hwmgr *hwmgr)
{
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	const struct iceland_pt_defaults *defaults = smu_data->power_tune_defaults;

	smu_data->power_tune_table.SviLoadLineEn = defaults->svi_load_line_en;
	smu_data->power_tune_table.SviLoadLineVddC = defaults->svi_load_line_vddc;
	smu_data->power_tune_table.SviLoadLineTrimVddC = 3;
	smu_data->power_tune_table.SviLoadLineOffsetVddC = 0;

	return 0;
}

static int iceland_populate_tdc_limit(struct pp_hwmgr *hwmgr)
{
	uint16_t tdc_limit;
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	const struct iceland_pt_defaults *defaults = smu_data->power_tune_defaults;

	tdc_limit = (uint16_t)(hwmgr->dyn_state.cac_dtp_table->usTDC * 256);
	smu_data->power_tune_table.TDC_VDDC_PkgLimit =
			CONVERT_FROM_HOST_TO_SMC_US(tdc_limit);
	smu_data->power_tune_table.TDC_VDDC_ThrottleReleaseLimitPerc =
			defaults->tdc_vddc_throttle_release_limit_perc;
	smu_data->power_tune_table.TDC_MAWt = defaults->tdc_mawt;

	return 0;
}

static int iceland_populate_dw8(struct pp_hwmgr *hwmgr, uint32_t fuse_table_offset)
{
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	const struct iceland_pt_defaults *defaults = smu_data->power_tune_defaults;
	uint32_t temp;

	if (smu7_read_smc_sram_dword(hwmgr,
			fuse_table_offset +
			offsetof(SMU71_Discrete_PmFuses, TdcWaterfallCtl),
			(uint32_t *)&temp, SMC_RAM_END))
		PP_ASSERT_WITH_CODE(false,
				"Attempt to read PmFuses.DW6 (SviLoadLineEn) from SMC Failed!",
				return -EINVAL);
	else
		smu_data->power_tune_table.TdcWaterfallCtl = defaults->tdc_waterfall_ctl;

	return 0;
}

static int iceland_populate_temperature_scaler(struct pp_hwmgr *hwmgr)
{
	return 0;
}

static int iceland_populate_gnb_lpml(struct pp_hwmgr *hwmgr)
{
	int i;
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);

	/* Currently not used. Set all to zero. */
	for (i = 0; i < 8; i++)
		smu_data->power_tune_table.GnbLPML[i] = 0;

	return 0;
}

static int iceland_populate_bapm_vddc_base_leakage_sidd(struct pp_hwmgr *hwmgr)
{
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	uint16_t HiSidd = smu_data->power_tune_table.BapmVddCBaseLeakageHiSidd;
	uint16_t LoSidd = smu_data->power_tune_table.BapmVddCBaseLeakageLoSidd;
	struct phm_cac_tdp_table *cac_table = hwmgr->dyn_state.cac_dtp_table;

	HiSidd = (uint16_t)(cac_table->usHighCACLeakage / 100 * 256);
	LoSidd = (uint16_t)(cac_table->usLowCACLeakage / 100 * 256);

	smu_data->power_tune_table.BapmVddCBaseLeakageHiSidd =
			CONVERT_FROM_HOST_TO_SMC_US(HiSidd);
	smu_data->power_tune_table.BapmVddCBaseLeakageLoSidd =
			CONVERT_FROM_HOST_TO_SMC_US(LoSidd);

	return 0;
}

static int iceland_populate_bapm_vddc_vid_sidd(struct pp_hwmgr *hwmgr)
{
	int i;
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	uint8_t *hi_vid = smu_data->power_tune_table.BapmVddCVidHiSidd;
	uint8_t *lo_vid = smu_data->power_tune_table.BapmVddCVidLoSidd;

	PP_ASSERT_WITH_CODE(NULL != hwmgr->dyn_state.cac_leakage_table,
			    "The CAC Leakage table does not exist!", return -EINVAL);
	PP_ASSERT_WITH_CODE(hwmgr->dyn_state.cac_leakage_table->count <= 8,
			    "There should never be more than 8 entries for BapmVddcVid!!!", return -EINVAL);
	PP_ASSERT_WITH_CODE(hwmgr->dyn_state.cac_leakage_table->count == hwmgr->dyn_state.vddc_dependency_on_sclk->count,
			    "CACLeakageTable->count and VddcDependencyOnSCLk->count not equal", return -EINVAL);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_EVV)) {
		for (i = 0; (uint32_t) i < hwmgr->dyn_state.cac_leakage_table->count; i++) {
			lo_vid[i] = convert_to_vid(hwmgr->dyn_state.cac_leakage_table->entries[i].Vddc1);
			hi_vid[i] = convert_to_vid(hwmgr->dyn_state.cac_leakage_table->entries[i].Vddc2);
		}
	} else {
		PP_ASSERT_WITH_CODE(false, "Iceland should always support EVV", return -EINVAL);
	}

	return 0;
}

static int iceland_populate_vddc_vid(struct pp_hwmgr *hwmgr)
{
	int i;
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	uint8_t *vid = smu_data->power_tune_table.VddCVid;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	PP_ASSERT_WITH_CODE(data->vddc_voltage_table.count <= 8,
		"There should never be more than 8 entries for VddcVid!!!",
		return -EINVAL);

	for (i = 0; i < (int)data->vddc_voltage_table.count; i++) {
		vid[i] = convert_to_vid(data->vddc_voltage_table.entries[i].value);
	}

	return 0;
}



static int iceland_populate_pm_fuses(struct pp_hwmgr *hwmgr)
{
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	uint32_t pm_fuse_table_offset;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_PowerContainment)) {
		if (smu7_read_smc_sram_dword(hwmgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, PmFuseTable),
				&pm_fuse_table_offset, SMC_RAM_END))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to get pm_fuse_table_offset Failed!",
					return -EINVAL);

		/* DW0 - DW3 */
		if (iceland_populate_bapm_vddc_vid_sidd(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate bapm vddc vid Failed!",
					return -EINVAL);

		/* DW4 - DW5 */
		if (iceland_populate_vddc_vid(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate vddc vid Failed!",
					return -EINVAL);

		/* DW6 */
		if (iceland_populate_svi_load_line(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate SviLoadLine Failed!",
					return -EINVAL);
		/* DW7 */
		if (iceland_populate_tdc_limit(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate TDCLimit Failed!", return -EINVAL);
		/* DW8 */
		if (iceland_populate_dw8(hwmgr, pm_fuse_table_offset))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate TdcWaterfallCtl, "
					"LPMLTemperature Min and Max Failed!",
					return -EINVAL);

		/* DW9-DW12 */
		if (0 != iceland_populate_temperature_scaler(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate LPMLTemperatureScaler Failed!",
					return -EINVAL);

		/* DW13-DW16 */
		if (iceland_populate_gnb_lpml(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate GnbLPML Failed!",
					return -EINVAL);

		/* DW18 */
		if (iceland_populate_bapm_vddc_base_leakage_sidd(hwmgr))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to populate BapmVddCBaseLeakage Hi and Lo Sidd Failed!",
					return -EINVAL);

		if (smu7_copy_bytes_to_smc(hwmgr, pm_fuse_table_offset,
				(uint8_t *)&smu_data->power_tune_table,
				sizeof(struct SMU71_Discrete_PmFuses), SMC_RAM_END))
			PP_ASSERT_WITH_CODE(false,
					"Attempt to download PmFuseTable Failed!",
					return -EINVAL);
	}
	return 0;
}

static int iceland_get_dependency_volt_by_clk(struct pp_hwmgr *hwmgr,
	struct phm_clock_voltage_dependency_table *allowed_clock_voltage_table,
	uint32_t clock, uint32_t *vol)
{
	uint32_t i = 0;

	/* clock - voltage dependency table is empty table */
	if (allowed_clock_voltage_table->count == 0)
		return -EINVAL;

	for (i = 0; i < allowed_clock_voltage_table->count; i++) {
		/* find first sclk bigger than request */
		if (allowed_clock_voltage_table->entries[i].clk >= clock) {
			*vol = allowed_clock_voltage_table->entries[i].v;
			return 0;
		}
	}

	/* sclk is bigger than max sclk in the dependence table */
	*vol = allowed_clock_voltage_table->entries[i - 1].v;

	return 0;
}

static int iceland_get_std_voltage_value_sidd(struct pp_hwmgr *hwmgr,
		pp_atomctrl_voltage_table_entry *tab, uint16_t *hi,
		uint16_t *lo)
{
	uint16_t v_index;
	bool vol_found = false;
	*hi = tab->value * VOLTAGE_SCALE;
	*lo = tab->value * VOLTAGE_SCALE;

	/* SCLK/VDDC Dependency Table has to exist. */
	PP_ASSERT_WITH_CODE(NULL != hwmgr->dyn_state.vddc_dependency_on_sclk,
			"The SCLK/VDDC Dependency Table does not exist.",
			return -EINVAL);

	if (NULL == hwmgr->dyn_state.cac_leakage_table) {
		pr_warn("CAC Leakage Table does not exist, using vddc.\n");
		return 0;
	}

	/*
	 * Since voltage in the sclk/vddc dependency table is not
	 * necessarily in ascending order because of ELB voltage
	 * patching, loop through entire list to find exact voltage.
	 */
	for (v_index = 0; (uint32_t)v_index < hwmgr->dyn_state.vddc_dependency_on_sclk->count; v_index++) {
		if (tab->value == hwmgr->dyn_state.vddc_dependency_on_sclk->entries[v_index].v) {
			vol_found = true;
			if ((uint32_t)v_index < hwmgr->dyn_state.cac_leakage_table->count) {
				*lo = hwmgr->dyn_state.cac_leakage_table->entries[v_index].Vddc * VOLTAGE_SCALE;
				*hi = (uint16_t)(hwmgr->dyn_state.cac_leakage_table->entries[v_index].Leakage * VOLTAGE_SCALE);
			} else {
				pr_warn("Index from SCLK/VDDC Dependency Table exceeds the CAC Leakage Table index, using maximum index from CAC table.\n");
				*lo = hwmgr->dyn_state.cac_leakage_table->entries[hwmgr->dyn_state.cac_leakage_table->count - 1].Vddc * VOLTAGE_SCALE;
				*hi = (uint16_t)(hwmgr->dyn_state.cac_leakage_table->entries[hwmgr->dyn_state.cac_leakage_table->count - 1].Leakage * VOLTAGE_SCALE);
			}
			break;
		}
	}

	/*
	 * If voltage is not found in the first pass, loop again to
	 * find the best match, equal or higher value.
	 */
	if (!vol_found) {
		for (v_index = 0; (uint32_t)v_index < hwmgr->dyn_state.vddc_dependency_on_sclk->count; v_index++) {
			if (tab->value <= hwmgr->dyn_state.vddc_dependency_on_sclk->entries[v_index].v) {
				vol_found = true;
				if ((uint32_t)v_index < hwmgr->dyn_state.cac_leakage_table->count) {
					*lo = hwmgr->dyn_state.cac_leakage_table->entries[v_index].Vddc * VOLTAGE_SCALE;
					*hi = (uint16_t)(hwmgr->dyn_state.cac_leakage_table->entries[v_index].Leakage) * VOLTAGE_SCALE;
				} else {
					pr_warn("Index from SCLK/VDDC Dependency Table exceeds the CAC Leakage Table index in second look up, using maximum index from CAC table.");
					*lo = hwmgr->dyn_state.cac_leakage_table->entries[hwmgr->dyn_state.cac_leakage_table->count - 1].Vddc * VOLTAGE_SCALE;
					*hi = (uint16_t)(hwmgr->dyn_state.cac_leakage_table->entries[hwmgr->dyn_state.cac_leakage_table->count - 1].Leakage * VOLTAGE_SCALE);
				}
				break;
			}
		}

		if (!vol_found)
			pr_warn("Unable to get std_vddc from SCLK/VDDC Dependency Table, using vddc.\n");
	}

	return 0;
}

static int iceland_populate_smc_voltage_table(struct pp_hwmgr *hwmgr,
		pp_atomctrl_voltage_table_entry *tab,
		SMU71_Discrete_VoltageLevel *smc_voltage_tab)
{
	int result;

	result = iceland_get_std_voltage_value_sidd(hwmgr, tab,
			&smc_voltage_tab->StdVoltageHiSidd,
			&smc_voltage_tab->StdVoltageLoSidd);
	if (0 != result) {
		smc_voltage_tab->StdVoltageHiSidd = tab->value * VOLTAGE_SCALE;
		smc_voltage_tab->StdVoltageLoSidd = tab->value * VOLTAGE_SCALE;
	}

	smc_voltage_tab->Voltage = PP_HOST_TO_SMC_US(tab->value * VOLTAGE_SCALE);
	CONVERT_FROM_HOST_TO_SMC_US(smc_voltage_tab->StdVoltageHiSidd);
	CONVERT_FROM_HOST_TO_SMC_US(smc_voltage_tab->StdVoltageHiSidd);

	return 0;
}

static int iceland_populate_smc_vddc_table(struct pp_hwmgr *hwmgr,
			SMU71_Discrete_DpmTable *table)
{
	unsigned int count;
	int result;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	table->VddcLevelCount = data->vddc_voltage_table.count;
	for (count = 0; count < table->VddcLevelCount; count++) {
		result = iceland_populate_smc_voltage_table(hwmgr,
				&(data->vddc_voltage_table.entries[count]),
				&(table->VddcLevel[count]));
		PP_ASSERT_WITH_CODE(0 == result, "do not populate SMC VDDC voltage table", return -EINVAL);

		/* GPIO voltage control */
		if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->voltage_control)
			table->VddcLevel[count].Smio |= data->vddc_voltage_table.entries[count].smio_low;
		else if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control)
			table->VddcLevel[count].Smio = 0;
	}

	CONVERT_FROM_HOST_TO_SMC_UL(table->VddcLevelCount);

	return 0;
}

static int iceland_populate_smc_vdd_ci_table(struct pp_hwmgr *hwmgr,
			SMU71_Discrete_DpmTable *table)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	uint32_t count;
	int result;

	table->VddciLevelCount = data->vddci_voltage_table.count;

	for (count = 0; count < table->VddciLevelCount; count++) {
		result = iceland_populate_smc_voltage_table(hwmgr,
				&(data->vddci_voltage_table.entries[count]),
				&(table->VddciLevel[count]));
		PP_ASSERT_WITH_CODE(result == 0, "do not populate SMC VDDCI voltage table", return -EINVAL);
		if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->vddci_control)
			table->VddciLevel[count].Smio |= data->vddci_voltage_table.entries[count].smio_low;
		else
			table->VddciLevel[count].Smio |= 0;
	}

	CONVERT_FROM_HOST_TO_SMC_UL(table->VddciLevelCount);

	return 0;
}

static int iceland_populate_smc_mvdd_table(struct pp_hwmgr *hwmgr,
			SMU71_Discrete_DpmTable *table)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	uint32_t count;
	int result;

	table->MvddLevelCount = data->mvdd_voltage_table.count;

	for (count = 0; count < table->VddciLevelCount; count++) {
		result = iceland_populate_smc_voltage_table(hwmgr,
				&(data->mvdd_voltage_table.entries[count]),
				&table->MvddLevel[count]);
		PP_ASSERT_WITH_CODE(result == 0, "do not populate SMC mvdd voltage table", return -EINVAL);
		if (SMU7_VOLTAGE_CONTROL_BY_GPIO == data->mvdd_control)
			table->MvddLevel[count].Smio |= data->mvdd_voltage_table.entries[count].smio_low;
		else
			table->MvddLevel[count].Smio |= 0;
	}

	CONVERT_FROM_HOST_TO_SMC_UL(table->MvddLevelCount);

	return 0;
}


static int iceland_populate_smc_voltage_tables(struct pp_hwmgr *hwmgr,
	SMU71_Discrete_DpmTable *table)
{
	int result;

	result = iceland_populate_smc_vddc_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"can not populate VDDC voltage table to SMC", return -EINVAL);

	result = iceland_populate_smc_vdd_ci_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"can not populate VDDCI voltage table to SMC", return -EINVAL);

	result = iceland_populate_smc_mvdd_table(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
			"can not populate MVDD voltage table to SMC", return -EINVAL);

	return 0;
}

static int iceland_populate_ulv_level(struct pp_hwmgr *hwmgr,
		struct SMU71_Discrete_Ulv *state)
{
	uint32_t voltage_response_time, ulv_voltage;
	int result;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	state->CcPwrDynRm = 0;
	state->CcPwrDynRm1 = 0;

	result = pp_tables_get_response_times(hwmgr, &voltage_response_time, &ulv_voltage);
	PP_ASSERT_WITH_CODE((0 == result), "can not get ULV voltage value", return result;);

	if (ulv_voltage == 0) {
		data->ulv_supported = false;
		return 0;
	}

	if (data->voltage_control != SMU7_VOLTAGE_CONTROL_BY_SVID2) {
		/* use minimum voltage if ulv voltage in pptable is bigger than minimum voltage */
		if (ulv_voltage > hwmgr->dyn_state.vddc_dependency_on_sclk->entries[0].v)
			state->VddcOffset = 0;
		else
			/* used in SMIO Mode. not implemented for now. this is backup only for CI. */
			state->VddcOffset = (uint16_t)(hwmgr->dyn_state.vddc_dependency_on_sclk->entries[0].v - ulv_voltage);
	} else {
		/* use minimum voltage if ulv voltage in pptable is bigger than minimum voltage */
		if (ulv_voltage > hwmgr->dyn_state.vddc_dependency_on_sclk->entries[0].v)
			state->VddcOffsetVid = 0;
		else  /* used in SVI2 Mode */
			state->VddcOffsetVid = (uint8_t)(
					(hwmgr->dyn_state.vddc_dependency_on_sclk->entries[0].v - ulv_voltage)
						* VOLTAGE_VID_OFFSET_SCALE2
						/ VOLTAGE_VID_OFFSET_SCALE1);
	}
	state->VddcPhase = 1;

	CONVERT_FROM_HOST_TO_SMC_UL(state->CcPwrDynRm);
	CONVERT_FROM_HOST_TO_SMC_UL(state->CcPwrDynRm1);
	CONVERT_FROM_HOST_TO_SMC_US(state->VddcOffset);

	return 0;
}

static int iceland_populate_ulv_state(struct pp_hwmgr *hwmgr,
		 SMU71_Discrete_Ulv *ulv_level)
{
	return iceland_populate_ulv_level(hwmgr, ulv_level);
}

static int iceland_populate_smc_link_level(struct pp_hwmgr *hwmgr, SMU71_Discrete_DpmTable *table)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct smu7_dpm_table *dpm_table = &data->dpm_table;
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	uint32_t i;

	/* Index (dpm_table->pcie_speed_table.count) is reserved for PCIE boot level. */
	for (i = 0; i <= dpm_table->pcie_speed_table.count; i++) {
		table->LinkLevel[i].PcieGenSpeed  =
			(uint8_t)dpm_table->pcie_speed_table.dpm_levels[i].value;
		table->LinkLevel[i].PcieLaneCount =
			(uint8_t)encode_pcie_lane_width(dpm_table->pcie_speed_table.dpm_levels[i].param1);
		table->LinkLevel[i].EnabledForActivity =
			1;
		table->LinkLevel[i].SPC =
			(uint8_t)(data->pcie_spc_cap & 0xff);
		table->LinkLevel[i].DownThreshold =
			PP_HOST_TO_SMC_UL(5);
		table->LinkLevel[i].UpThreshold =
			PP_HOST_TO_SMC_UL(30);
	}

	smu_data->smc_state_table.LinkLevelCount =
		(uint8_t)dpm_table->pcie_speed_table.count;
	data->dpm_level_enable_mask.pcie_dpm_enable_mask =
		phm_get_dpm_level_enable_mask_value(&dpm_table->pcie_speed_table);

	return 0;
}

static int iceland_calculate_sclk_params(struct pp_hwmgr *hwmgr,
		uint32_t engine_clock, SMU71_Discrete_GraphicsLevel *sclk)
{
	const struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	pp_atomctrl_clock_dividers_vi dividers;
	uint32_t spll_func_cntl            = data->clock_registers.vCG_SPLL_FUNC_CNTL;
	uint32_t spll_func_cntl_3          = data->clock_registers.vCG_SPLL_FUNC_CNTL_3;
	uint32_t spll_func_cntl_4          = data->clock_registers.vCG_SPLL_FUNC_CNTL_4;
	uint32_t cg_spll_spread_spectrum   = data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM;
	uint32_t cg_spll_spread_spectrum_2 = data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM_2;
	uint32_t    reference_clock;
	uint32_t reference_divider;
	uint32_t fbdiv;
	int result;

	/* get the engine clock dividers for this clock value*/
	result = atomctrl_get_engine_pll_dividers_vi(hwmgr, engine_clock,  &dividers);

	PP_ASSERT_WITH_CODE(result == 0,
		"Error retrieving Engine Clock dividers from VBIOS.", return result);

	/* To get FBDIV we need to multiply this by 16384 and divide it by Fref.*/
	reference_clock = atomctrl_get_reference_clock(hwmgr);

	reference_divider = 1 + dividers.uc_pll_ref_div;

	/* low 14 bits is fraction and high 12 bits is divider*/
	fbdiv = dividers.ul_fb_div.ul_fb_divider & 0x3FFFFFF;

	/* SPLL_FUNC_CNTL setup*/
	spll_func_cntl = PHM_SET_FIELD(spll_func_cntl,
		CG_SPLL_FUNC_CNTL, SPLL_REF_DIV, dividers.uc_pll_ref_div);
	spll_func_cntl = PHM_SET_FIELD(spll_func_cntl,
		CG_SPLL_FUNC_CNTL, SPLL_PDIV_A,  dividers.uc_pll_post_div);

	/* SPLL_FUNC_CNTL_3 setup*/
	spll_func_cntl_3 = PHM_SET_FIELD(spll_func_cntl_3,
		CG_SPLL_FUNC_CNTL_3, SPLL_FB_DIV, fbdiv);

	/* set to use fractional accumulation*/
	spll_func_cntl_3 = PHM_SET_FIELD(spll_func_cntl_3,
		CG_SPLL_FUNC_CNTL_3, SPLL_DITHEN, 1);

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_EngineSpreadSpectrumSupport)) {
		pp_atomctrl_internal_ss_info ss_info;

		uint32_t vcoFreq = engine_clock * dividers.uc_pll_post_div;
		if (0 == atomctrl_get_engine_clock_spread_spectrum(hwmgr, vcoFreq, &ss_info)) {
			/*
			* ss_info.speed_spectrum_percentage -- in unit of 0.01%
			* ss_info.speed_spectrum_rate -- in unit of khz
			*/
			/* clks = reference_clock * 10 / (REFDIV + 1) / speed_spectrum_rate / 2 */
			uint32_t clkS = reference_clock * 5 / (reference_divider * ss_info.speed_spectrum_rate);

			/* clkv = 2 * D * fbdiv / NS */
			uint32_t clkV = 4 * ss_info.speed_spectrum_percentage * fbdiv / (clkS * 10000);

			cg_spll_spread_spectrum =
				PHM_SET_FIELD(cg_spll_spread_spectrum, CG_SPLL_SPREAD_SPECTRUM, CLKS, clkS);
			cg_spll_spread_spectrum =
				PHM_SET_FIELD(cg_spll_spread_spectrum, CG_SPLL_SPREAD_SPECTRUM, SSEN, 1);
			cg_spll_spread_spectrum_2 =
				PHM_SET_FIELD(cg_spll_spread_spectrum_2, CG_SPLL_SPREAD_SPECTRUM_2, CLKV, clkV);
		}
	}

	sclk->SclkFrequency        = engine_clock;
	sclk->CgSpllFuncCntl3      = spll_func_cntl_3;
	sclk->CgSpllFuncCntl4      = spll_func_cntl_4;
	sclk->SpllSpreadSpectrum   = cg_spll_spread_spectrum;
	sclk->SpllSpreadSpectrum2  = cg_spll_spread_spectrum_2;
	sclk->SclkDid              = (uint8_t)dividers.pll_post_divider;

	return 0;
}

static int iceland_populate_phase_value_based_on_sclk(struct pp_hwmgr *hwmgr,
				const struct phm_phase_shedding_limits_table *pl,
					uint32_t sclk, uint32_t *p_shed)
{
	unsigned int i;

	/* use the minimum phase shedding */
	*p_shed = 1;

	for (i = 0; i < pl->count; i++) {
		if (sclk < pl->entries[i].Sclk) {
			*p_shed = i;
			break;
		}
	}
	return 0;
}

static int iceland_populate_single_graphic_level(struct pp_hwmgr *hwmgr,
						uint32_t engine_clock,
				SMU71_Discrete_GraphicsLevel *graphic_level)
{
	int result;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	result = iceland_calculate_sclk_params(hwmgr, engine_clock, graphic_level);

	/* populate graphics levels*/
	result = iceland_get_dependency_volt_by_clk(hwmgr,
		hwmgr->dyn_state.vddc_dependency_on_sclk, engine_clock,
		&graphic_level->MinVddc);
	PP_ASSERT_WITH_CODE((0 == result),
		"can not find VDDC voltage value for VDDC engine clock dependency table", return result);

	/* SCLK frequency in units of 10KHz*/
	graphic_level->SclkFrequency = engine_clock;
	graphic_level->MinVddcPhases = 1;

	if (data->vddc_phase_shed_control)
		iceland_populate_phase_value_based_on_sclk(hwmgr,
				hwmgr->dyn_state.vddc_phase_shed_limits_table,
				engine_clock,
				&graphic_level->MinVddcPhases);

	/* Indicates maximum activity level for this performance level. 50% for now*/
	graphic_level->ActivityLevel = data->current_profile_setting.sclk_activity;

	graphic_level->CcPwrDynRm = 0;
	graphic_level->CcPwrDynRm1 = 0;
	/* this level can be used if activity is high enough.*/
	graphic_level->EnabledForActivity = 0;
	/* this level can be used for throttling.*/
	graphic_level->EnabledForThrottle = 1;
	graphic_level->UpHyst = data->current_profile_setting.sclk_up_hyst;
	graphic_level->DownHyst = data->current_profile_setting.sclk_down_hyst;
	graphic_level->VoltageDownHyst = 0;
	graphic_level->PowerThrottle = 0;

	data->display_timing.min_clock_in_sr =
			hwmgr->display_config->min_core_set_clock_in_sr;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkDeepSleep))
		graphic_level->DeepSleepDivId =
				smu7_get_sleep_divider_id_from_clock(engine_clock,
						data->display_timing.min_clock_in_sr);

	/* Default to slow, highest DPM level will be set to PPSMC_DISPLAY_WATERMARK_LOW later.*/
	graphic_level->DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;

	if (0 == result) {
		graphic_level->MinVddc = PP_HOST_TO_SMC_UL(graphic_level->MinVddc * VOLTAGE_SCALE);
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->MinVddcPhases);
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->SclkFrequency);
		CONVERT_FROM_HOST_TO_SMC_US(graphic_level->ActivityLevel);
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->CgSpllFuncCntl3);
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->CgSpllFuncCntl4);
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->SpllSpreadSpectrum);
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->SpllSpreadSpectrum2);
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->CcPwrDynRm);
		CONVERT_FROM_HOST_TO_SMC_UL(graphic_level->CcPwrDynRm1);
	}

	return result;
}

static int iceland_populate_all_graphic_levels(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	struct smu7_dpm_table *dpm_table = &data->dpm_table;
	uint32_t level_array_adress = smu_data->smu7_data.dpm_table_start +
				offsetof(SMU71_Discrete_DpmTable, GraphicsLevel);

	uint32_t level_array_size = sizeof(SMU71_Discrete_GraphicsLevel) *
						SMU71_MAX_LEVELS_GRAPHICS;

	SMU71_Discrete_GraphicsLevel *levels = smu_data->smc_state_table.GraphicsLevel;

	uint32_t i;
	uint8_t highest_pcie_level_enabled = 0;
	uint8_t lowest_pcie_level_enabled = 0, mid_pcie_level_enabled = 0;
	uint8_t count = 0;
	int result = 0;

	memset(levels, 0x00, level_array_size);

	for (i = 0; i < dpm_table->sclk_table.count; i++) {
		result = iceland_populate_single_graphic_level(hwmgr,
					dpm_table->sclk_table.dpm_levels[i].value,
					&(smu_data->smc_state_table.GraphicsLevel[i]));
		if (result != 0)
			return result;

		/* Making sure only DPM level 0-1 have Deep Sleep Div ID populated. */
		if (i > 1)
			smu_data->smc_state_table.GraphicsLevel[i].DeepSleepDivId = 0;
	}

	/* Only enable level 0 for now. */
	smu_data->smc_state_table.GraphicsLevel[0].EnabledForActivity = 1;

	/* set highest level watermark to high */
	if (dpm_table->sclk_table.count > 1)
		smu_data->smc_state_table.GraphicsLevel[dpm_table->sclk_table.count-1].DisplayWatermark =
			PPSMC_DISPLAY_WATERMARK_HIGH;

	smu_data->smc_state_table.GraphicsDpmLevelCount =
		(uint8_t)dpm_table->sclk_table.count;
	data->dpm_level_enable_mask.sclk_dpm_enable_mask =
		phm_get_dpm_level_enable_mask_value(&dpm_table->sclk_table);

	while ((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
				(1 << (highest_pcie_level_enabled + 1))) != 0) {
		highest_pcie_level_enabled++;
	}

	while ((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
		(1 << lowest_pcie_level_enabled)) == 0) {
		lowest_pcie_level_enabled++;
	}

	while ((count < highest_pcie_level_enabled) &&
			((data->dpm_level_enable_mask.pcie_dpm_enable_mask &
				(1 << (lowest_pcie_level_enabled + 1 + count))) == 0)) {
		count++;
	}

	mid_pcie_level_enabled = (lowest_pcie_level_enabled+1+count) < highest_pcie_level_enabled ?
		(lowest_pcie_level_enabled+1+count) : highest_pcie_level_enabled;


	/* set pcieDpmLevel to highest_pcie_level_enabled*/
	for (i = 2; i < dpm_table->sclk_table.count; i++) {
		smu_data->smc_state_table.GraphicsLevel[i].pcieDpmLevel = highest_pcie_level_enabled;
	}

	/* set pcieDpmLevel to lowest_pcie_level_enabled*/
	smu_data->smc_state_table.GraphicsLevel[0].pcieDpmLevel = lowest_pcie_level_enabled;

	/* set pcieDpmLevel to mid_pcie_level_enabled*/
	smu_data->smc_state_table.GraphicsLevel[1].pcieDpmLevel = mid_pcie_level_enabled;

	/* level count will send to smc once at init smc table and never change*/
	result = smu7_copy_bytes_to_smc(hwmgr, level_array_adress,
				(uint8_t *)levels, (uint32_t)level_array_size,
								SMC_RAM_END);

	return result;
}

static int iceland_calculate_mclk_params(
		struct pp_hwmgr *hwmgr,
		uint32_t memory_clock,
		SMU71_Discrete_MemoryLevel *mclk,
		bool strobe_mode,
		bool dllStateOn
		)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	uint32_t  dll_cntl = data->clock_registers.vDLL_CNTL;
	uint32_t  mclk_pwrmgt_cntl = data->clock_registers.vMCLK_PWRMGT_CNTL;
	uint32_t  mpll_ad_func_cntl = data->clock_registers.vMPLL_AD_FUNC_CNTL;
	uint32_t  mpll_dq_func_cntl = data->clock_registers.vMPLL_DQ_FUNC_CNTL;
	uint32_t  mpll_func_cntl = data->clock_registers.vMPLL_FUNC_CNTL;
	uint32_t  mpll_func_cntl_1 = data->clock_registers.vMPLL_FUNC_CNTL_1;
	uint32_t  mpll_func_cntl_2 = data->clock_registers.vMPLL_FUNC_CNTL_2;
	uint32_t  mpll_ss1 = data->clock_registers.vMPLL_SS1;
	uint32_t  mpll_ss2 = data->clock_registers.vMPLL_SS2;

	pp_atomctrl_memory_clock_param mpll_param;
	int result;

	result = atomctrl_get_memory_pll_dividers_si(hwmgr,
				memory_clock, &mpll_param, strobe_mode);
	PP_ASSERT_WITH_CODE(0 == result,
		"Error retrieving Memory Clock Parameters from VBIOS.", return result);

	/* MPLL_FUNC_CNTL setup*/
	mpll_func_cntl = PHM_SET_FIELD(mpll_func_cntl, MPLL_FUNC_CNTL, BWCTRL, mpll_param.bw_ctrl);

	/* MPLL_FUNC_CNTL_1 setup*/
	mpll_func_cntl_1  = PHM_SET_FIELD(mpll_func_cntl_1,
							MPLL_FUNC_CNTL_1, CLKF, mpll_param.mpll_fb_divider.cl_kf);
	mpll_func_cntl_1  = PHM_SET_FIELD(mpll_func_cntl_1,
							MPLL_FUNC_CNTL_1, CLKFRAC, mpll_param.mpll_fb_divider.clk_frac);
	mpll_func_cntl_1  = PHM_SET_FIELD(mpll_func_cntl_1,
							MPLL_FUNC_CNTL_1, VCO_MODE, mpll_param.vco_mode);

	/* MPLL_AD_FUNC_CNTL setup*/
	mpll_ad_func_cntl = PHM_SET_FIELD(mpll_ad_func_cntl,
							MPLL_AD_FUNC_CNTL, YCLK_POST_DIV, mpll_param.mpll_post_divider);

	if (data->is_memory_gddr5) {
		/* MPLL_DQ_FUNC_CNTL setup*/
		mpll_dq_func_cntl  = PHM_SET_FIELD(mpll_dq_func_cntl,
								MPLL_DQ_FUNC_CNTL, YCLK_SEL, mpll_param.yclk_sel);
		mpll_dq_func_cntl  = PHM_SET_FIELD(mpll_dq_func_cntl,
								MPLL_DQ_FUNC_CNTL, YCLK_POST_DIV, mpll_param.mpll_post_divider);
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_MemorySpreadSpectrumSupport)) {
		/*
		 ************************************
		 Fref = Reference Frequency
		 NF = Feedback divider ratio
		 NR = Reference divider ratio
		 Fnom = Nominal VCO output frequency = Fref * NF / NR
		 Fs = Spreading Rate
		 D = Percentage down-spread / 2
		 Fint = Reference input frequency to PFD = Fref / NR
		 NS = Spreading rate divider ratio = int(Fint / (2 * Fs))
		 CLKS = NS - 1 = ISS_STEP_NUM[11:0]
		 NV = D * Fs / Fnom * 4 * ((Fnom/Fref * NR) ^ 2)
		 CLKV = 65536 * NV = ISS_STEP_SIZE[25:0]
		 *************************************
		 */
		pp_atomctrl_internal_ss_info ss_info;
		uint32_t freq_nom;
		uint32_t tmp;
		uint32_t reference_clock = atomctrl_get_mpll_reference_clock(hwmgr);

		/* for GDDR5 for all modes and DDR3 */
		if (1 == mpll_param.qdr)
			freq_nom = memory_clock * 4 * (1 << mpll_param.mpll_post_divider);
		else
			freq_nom = memory_clock * 2 * (1 << mpll_param.mpll_post_divider);

		/* tmp = (freq_nom / reference_clock * reference_divider) ^ 2  Note: S.I. reference_divider = 1*/
		tmp = (freq_nom / reference_clock);
		tmp = tmp * tmp;

		if (0 == atomctrl_get_memory_clock_spread_spectrum(hwmgr, freq_nom, &ss_info)) {
			/* ss_info.speed_spectrum_percentage -- in unit of 0.01% */
			/* ss.Info.speed_spectrum_rate -- in unit of khz */
			/* CLKS = reference_clock / (2 * speed_spectrum_rate * reference_divider) * 10 */
			/*     = reference_clock * 5 / speed_spectrum_rate */
			uint32_t clks = reference_clock * 5 / ss_info.speed_spectrum_rate;

			/* CLKV = 65536 * speed_spectrum_percentage / 2 * spreadSpecrumRate / freq_nom * 4 / 100000 * ((freq_nom / reference_clock) ^ 2) */
			/*     = 131 * speed_spectrum_percentage * speed_spectrum_rate / 100 * ((freq_nom / reference_clock) ^ 2) / freq_nom */
			uint32_t clkv =
				(uint32_t)((((131 * ss_info.speed_spectrum_percentage *
							ss_info.speed_spectrum_rate) / 100) * tmp) / freq_nom);

			mpll_ss1 = PHM_SET_FIELD(mpll_ss1, MPLL_SS1, CLKV, clkv);
			mpll_ss2 = PHM_SET_FIELD(mpll_ss2, MPLL_SS2, CLKS, clks);
		}
	}

	/* MCLK_PWRMGT_CNTL setup */
	mclk_pwrmgt_cntl = PHM_SET_FIELD(mclk_pwrmgt_cntl,
		MCLK_PWRMGT_CNTL, DLL_SPEED, mpll_param.dll_speed);
	mclk_pwrmgt_cntl = PHM_SET_FIELD(mclk_pwrmgt_cntl,
		MCLK_PWRMGT_CNTL, MRDCK0_PDNB, dllStateOn);
	mclk_pwrmgt_cntl = PHM_SET_FIELD(mclk_pwrmgt_cntl,
		MCLK_PWRMGT_CNTL, MRDCK1_PDNB, dllStateOn);


	/* Save the result data to outpupt memory level structure */
	mclk->MclkFrequency   = memory_clock;
	mclk->MpllFuncCntl    = mpll_func_cntl;
	mclk->MpllFuncCntl_1  = mpll_func_cntl_1;
	mclk->MpllFuncCntl_2  = mpll_func_cntl_2;
	mclk->MpllAdFuncCntl  = mpll_ad_func_cntl;
	mclk->MpllDqFuncCntl  = mpll_dq_func_cntl;
	mclk->MclkPwrmgtCntl  = mclk_pwrmgt_cntl;
	mclk->DllCntl         = dll_cntl;
	mclk->MpllSs1         = mpll_ss1;
	mclk->MpllSs2         = mpll_ss2;

	return 0;
}

static uint8_t iceland_get_mclk_frequency_ratio(uint32_t memory_clock,
		bool strobe_mode)
{
	uint8_t mc_para_index;

	if (strobe_mode) {
		if (memory_clock < 12500) {
			mc_para_index = 0x00;
		} else if (memory_clock > 47500) {
			mc_para_index = 0x0f;
		} else {
			mc_para_index = (uint8_t)((memory_clock - 10000) / 2500);
		}
	} else {
		if (memory_clock < 65000) {
			mc_para_index = 0x00;
		} else if (memory_clock > 135000) {
			mc_para_index = 0x0f;
		} else {
			mc_para_index = (uint8_t)((memory_clock - 60000) / 5000);
		}
	}

	return mc_para_index;
}

static uint8_t iceland_get_ddr3_mclk_frequency_ratio(uint32_t memory_clock)
{
	uint8_t mc_para_index;

	if (memory_clock < 10000) {
		mc_para_index = 0;
	} else if (memory_clock >= 80000) {
		mc_para_index = 0x0f;
	} else {
		mc_para_index = (uint8_t)((memory_clock - 10000) / 5000 + 1);
	}

	return mc_para_index;
}

static int iceland_populate_phase_value_based_on_mclk(struct pp_hwmgr *hwmgr, const struct phm_phase_shedding_limits_table *pl,
					uint32_t memory_clock, uint32_t *p_shed)
{
	unsigned int i;

	*p_shed = 1;

	for (i = 0; i < pl->count; i++) {
		if (memory_clock < pl->entries[i].Mclk) {
			*p_shed = i;
			break;
		}
	}

	return 0;
}

static int iceland_populate_single_memory_level(
		struct pp_hwmgr *hwmgr,
		uint32_t memory_clock,
		SMU71_Discrete_MemoryLevel *memory_level
		)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	int result = 0;
	bool dll_state_on;
	uint32_t mclk_edc_wr_enable_threshold = 40000;
	uint32_t mclk_edc_enable_threshold = 40000;
	uint32_t mclk_strobe_mode_threshold = 40000;

	if (hwmgr->dyn_state.vddc_dependency_on_mclk != NULL) {
		result = iceland_get_dependency_volt_by_clk(hwmgr,
			hwmgr->dyn_state.vddc_dependency_on_mclk, memory_clock, &memory_level->MinVddc);
		PP_ASSERT_WITH_CODE((0 == result),
			"can not find MinVddc voltage value from memory VDDC voltage dependency table", return result);
	}

	if (data->vddci_control == SMU7_VOLTAGE_CONTROL_NONE) {
		memory_level->MinVddci = memory_level->MinVddc;
	} else if (NULL != hwmgr->dyn_state.vddci_dependency_on_mclk) {
		result = iceland_get_dependency_volt_by_clk(hwmgr,
				hwmgr->dyn_state.vddci_dependency_on_mclk,
				memory_clock,
				&memory_level->MinVddci);
		PP_ASSERT_WITH_CODE((0 == result),
			"can not find MinVddci voltage value from memory VDDCI voltage dependency table", return result);
	}

	memory_level->MinVddcPhases = 1;

	if (data->vddc_phase_shed_control) {
		iceland_populate_phase_value_based_on_mclk(hwmgr, hwmgr->dyn_state.vddc_phase_shed_limits_table,
				memory_clock, &memory_level->MinVddcPhases);
	}

	memory_level->EnabledForThrottle = 1;
	memory_level->EnabledForActivity = 0;
	memory_level->UpHyst = data->current_profile_setting.mclk_up_hyst;
	memory_level->DownHyst = data->current_profile_setting.mclk_down_hyst;
	memory_level->VoltageDownHyst = 0;

	/* Indicates maximum activity level for this performance level.*/
	memory_level->ActivityLevel = data->current_profile_setting.mclk_activity;
	memory_level->StutterEnable = 0;
	memory_level->StrobeEnable = 0;
	memory_level->EdcReadEnable = 0;
	memory_level->EdcWriteEnable = 0;
	memory_level->RttEnable = 0;

	/* default set to low watermark. Highest level will be set to high later.*/
	memory_level->DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;

	data->display_timing.num_existing_displays = hwmgr->display_config->num_display;
	data->display_timing.vrefresh = hwmgr->display_config->vrefresh;

	/* stutter mode not support on iceland */

	/* decide strobe mode*/
	memory_level->StrobeEnable = (mclk_strobe_mode_threshold != 0) &&
		(memory_clock <= mclk_strobe_mode_threshold);

	/* decide EDC mode and memory clock ratio*/
	if (data->is_memory_gddr5) {
		memory_level->StrobeRatio = iceland_get_mclk_frequency_ratio(memory_clock,
					memory_level->StrobeEnable);

		if ((mclk_edc_enable_threshold != 0) &&
				(memory_clock > mclk_edc_enable_threshold)) {
			memory_level->EdcReadEnable = 1;
		}

		if ((mclk_edc_wr_enable_threshold != 0) &&
				(memory_clock > mclk_edc_wr_enable_threshold)) {
			memory_level->EdcWriteEnable = 1;
		}

		if (memory_level->StrobeEnable) {
			if (iceland_get_mclk_frequency_ratio(memory_clock, 1) >=
					((cgs_read_register(hwmgr->device, mmMC_SEQ_MISC7) >> 16) & 0xf))
				dll_state_on = ((cgs_read_register(hwmgr->device, mmMC_SEQ_MISC5) >> 1) & 0x1) ? 1 : 0;
			else
				dll_state_on = ((cgs_read_register(hwmgr->device, mmMC_SEQ_MISC6) >> 1) & 0x1) ? 1 : 0;
		} else
			dll_state_on = data->dll_default_on;
	} else {
		memory_level->StrobeRatio =
			iceland_get_ddr3_mclk_frequency_ratio(memory_clock);
		dll_state_on = ((cgs_read_register(hwmgr->device, mmMC_SEQ_MISC5) >> 1) & 0x1) ? 1 : 0;
	}

	result = iceland_calculate_mclk_params(hwmgr,
		memory_clock, memory_level, memory_level->StrobeEnable, dll_state_on);

	if (0 == result) {
		memory_level->MinVddc = PP_HOST_TO_SMC_UL(memory_level->MinVddc * VOLTAGE_SCALE);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MinVddcPhases);
		memory_level->MinVddci = PP_HOST_TO_SMC_UL(memory_level->MinVddci * VOLTAGE_SCALE);
		memory_level->MinMvdd = PP_HOST_TO_SMC_UL(memory_level->MinMvdd * VOLTAGE_SCALE);
		/* MCLK frequency in units of 10KHz*/
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MclkFrequency);
		/* Indicates maximum activity level for this performance level.*/
		CONVERT_FROM_HOST_TO_SMC_US(memory_level->ActivityLevel);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MpllFuncCntl);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MpllFuncCntl_1);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MpllFuncCntl_2);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MpllAdFuncCntl);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MpllDqFuncCntl);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MclkPwrmgtCntl);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->DllCntl);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MpllSs1);
		CONVERT_FROM_HOST_TO_SMC_UL(memory_level->MpllSs2);
	}

	return result;
}

static int iceland_populate_all_memory_levels(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	struct smu7_dpm_table *dpm_table = &data->dpm_table;
	int result;

	/* populate MCLK dpm table to SMU7 */
	uint32_t level_array_adress = smu_data->smu7_data.dpm_table_start + offsetof(SMU71_Discrete_DpmTable, MemoryLevel);
	uint32_t level_array_size = sizeof(SMU71_Discrete_MemoryLevel) * SMU71_MAX_LEVELS_MEMORY;
	SMU71_Discrete_MemoryLevel *levels = smu_data->smc_state_table.MemoryLevel;
	uint32_t i;

	memset(levels, 0x00, level_array_size);

	for (i = 0; i < dpm_table->mclk_table.count; i++) {
		PP_ASSERT_WITH_CODE((0 != dpm_table->mclk_table.dpm_levels[i].value),
			"can not populate memory level as memory clock is zero", return -EINVAL);
		result = iceland_populate_single_memory_level(hwmgr, dpm_table->mclk_table.dpm_levels[i].value,
			&(smu_data->smc_state_table.MemoryLevel[i]));
		if (0 != result) {
			return result;
		}
	}

	/* Only enable level 0 for now.*/
	smu_data->smc_state_table.MemoryLevel[0].EnabledForActivity = 1;

	/*
	* in order to prevent MC activity from stutter mode to push DPM up.
	* the UVD change complements this by putting the MCLK in a higher state
	* by default such that we are not effected by up threshold or and MCLK DPM latency.
	*/
	smu_data->smc_state_table.MemoryLevel[0].ActivityLevel = 0x1F;
	CONVERT_FROM_HOST_TO_SMC_US(smu_data->smc_state_table.MemoryLevel[0].ActivityLevel);

	smu_data->smc_state_table.MemoryDpmLevelCount = (uint8_t)dpm_table->mclk_table.count;
	data->dpm_level_enable_mask.mclk_dpm_enable_mask = phm_get_dpm_level_enable_mask_value(&dpm_table->mclk_table);
	/* set highest level watermark to high*/
	smu_data->smc_state_table.MemoryLevel[dpm_table->mclk_table.count-1].DisplayWatermark = PPSMC_DISPLAY_WATERMARK_HIGH;

	/* level count will send to smc once at init smc table and never change*/
	result = smu7_copy_bytes_to_smc(hwmgr,
		level_array_adress, (uint8_t *)levels, (uint32_t)level_array_size,
		SMC_RAM_END);

	return result;
}

static int iceland_populate_mvdd_value(struct pp_hwmgr *hwmgr, uint32_t mclk,
					SMU71_Discrete_VoltageLevel *voltage)
{
	const struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	uint32_t i = 0;

	if (SMU7_VOLTAGE_CONTROL_NONE != data->mvdd_control) {
		/* find mvdd value which clock is more than request */
		for (i = 0; i < hwmgr->dyn_state.mvdd_dependency_on_mclk->count; i++) {
			if (mclk <= hwmgr->dyn_state.mvdd_dependency_on_mclk->entries[i].clk) {
				/* Always round to higher voltage. */
				voltage->Voltage = data->mvdd_voltage_table.entries[i].value;
				break;
			}
		}

		PP_ASSERT_WITH_CODE(i < hwmgr->dyn_state.mvdd_dependency_on_mclk->count,
			"MVDD Voltage is outside the supported range.", return -EINVAL);

	} else {
		return -EINVAL;
	}

	return 0;
}

static int iceland_populate_smc_acpi_level(struct pp_hwmgr *hwmgr,
	SMU71_Discrete_DpmTable *table)
{
	int result = 0;
	const struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct pp_atomctrl_clock_dividers_vi dividers;
	uint32_t vddc_phase_shed_control = 0;

	SMU71_Discrete_VoltageLevel voltage_level;
	uint32_t spll_func_cntl    = data->clock_registers.vCG_SPLL_FUNC_CNTL;
	uint32_t spll_func_cntl_2  = data->clock_registers.vCG_SPLL_FUNC_CNTL_2;
	uint32_t dll_cntl          = data->clock_registers.vDLL_CNTL;
	uint32_t mclk_pwrmgt_cntl  = data->clock_registers.vMCLK_PWRMGT_CNTL;


	/* The ACPI state should not do DPM on DC (or ever).*/
	table->ACPILevel.Flags &= ~PPSMC_SWSTATE_FLAG_DC;

	if (data->acpi_vddc)
		table->ACPILevel.MinVddc = PP_HOST_TO_SMC_UL(data->acpi_vddc * VOLTAGE_SCALE);
	else
		table->ACPILevel.MinVddc = PP_HOST_TO_SMC_UL(data->min_vddc_in_pptable * VOLTAGE_SCALE);

	table->ACPILevel.MinVddcPhases = vddc_phase_shed_control ? 0 : 1;
	/* assign zero for now*/
	table->ACPILevel.SclkFrequency = atomctrl_get_reference_clock(hwmgr);

	/* get the engine clock dividers for this clock value*/
	result = atomctrl_get_engine_pll_dividers_vi(hwmgr,
		table->ACPILevel.SclkFrequency,  &dividers);

	PP_ASSERT_WITH_CODE(result == 0,
		"Error retrieving Engine Clock dividers from VBIOS.", return result);

	/* divider ID for required SCLK*/
	table->ACPILevel.SclkDid = (uint8_t)dividers.pll_post_divider;
	table->ACPILevel.DisplayWatermark = PPSMC_DISPLAY_WATERMARK_LOW;
	table->ACPILevel.DeepSleepDivId = 0;

	spll_func_cntl      = PHM_SET_FIELD(spll_func_cntl,
							CG_SPLL_FUNC_CNTL,   SPLL_PWRON,     0);
	spll_func_cntl      = PHM_SET_FIELD(spll_func_cntl,
							CG_SPLL_FUNC_CNTL,   SPLL_RESET,     1);
	spll_func_cntl_2    = PHM_SET_FIELD(spll_func_cntl_2,
							CG_SPLL_FUNC_CNTL_2, SCLK_MUX_SEL,   4);

	table->ACPILevel.CgSpllFuncCntl = spll_func_cntl;
	table->ACPILevel.CgSpllFuncCntl2 = spll_func_cntl_2;
	table->ACPILevel.CgSpllFuncCntl3 = data->clock_registers.vCG_SPLL_FUNC_CNTL_3;
	table->ACPILevel.CgSpllFuncCntl4 = data->clock_registers.vCG_SPLL_FUNC_CNTL_4;
	table->ACPILevel.SpllSpreadSpectrum = data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM;
	table->ACPILevel.SpllSpreadSpectrum2 = data->clock_registers.vCG_SPLL_SPREAD_SPECTRUM_2;
	table->ACPILevel.CcPwrDynRm = 0;
	table->ACPILevel.CcPwrDynRm1 = 0;


	/* For various features to be enabled/disabled while this level is active.*/
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.Flags);
	/* SCLK frequency in units of 10KHz*/
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.SclkFrequency);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CgSpllFuncCntl);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CgSpllFuncCntl2);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CgSpllFuncCntl3);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CgSpllFuncCntl4);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.SpllSpreadSpectrum);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.SpllSpreadSpectrum2);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CcPwrDynRm);
	CONVERT_FROM_HOST_TO_SMC_UL(table->ACPILevel.CcPwrDynRm1);

	/* table->MemoryACPILevel.MinVddcPhases = table->ACPILevel.MinVddcPhases;*/
	table->MemoryACPILevel.MinVddc = table->ACPILevel.MinVddc;
	table->MemoryACPILevel.MinVddcPhases = table->ACPILevel.MinVddcPhases;

	if (SMU7_VOLTAGE_CONTROL_NONE == data->vddci_control)
		table->MemoryACPILevel.MinVddci = table->MemoryACPILevel.MinVddc;
	else {
		if (data->acpi_vddci != 0)
			table->MemoryACPILevel.MinVddci = PP_HOST_TO_SMC_UL(data->acpi_vddci * VOLTAGE_SCALE);
		else
			table->MemoryACPILevel.MinVddci = PP_HOST_TO_SMC_UL(data->min_vddci_in_pptable * VOLTAGE_SCALE);
	}

	if (0 == iceland_populate_mvdd_value(hwmgr, 0, &voltage_level))
		table->MemoryACPILevel.MinMvdd =
			PP_HOST_TO_SMC_UL(voltage_level.Voltage * VOLTAGE_SCALE);
	else
		table->MemoryACPILevel.MinMvdd = 0;

	/* Force reset on DLL*/
	mclk_pwrmgt_cntl    = PHM_SET_FIELD(mclk_pwrmgt_cntl,
		MCLK_PWRMGT_CNTL, MRDCK0_RESET, 0x1);
	mclk_pwrmgt_cntl    = PHM_SET_FIELD(mclk_pwrmgt_cntl,
		MCLK_PWRMGT_CNTL, MRDCK1_RESET, 0x1);

	/* Disable DLL in ACPIState*/
	mclk_pwrmgt_cntl    = PHM_SET_FIELD(mclk_pwrmgt_cntl,
		MCLK_PWRMGT_CNTL, MRDCK0_PDNB, 0);
	mclk_pwrmgt_cntl    = PHM_SET_FIELD(mclk_pwrmgt_cntl,
		MCLK_PWRMGT_CNTL, MRDCK1_PDNB, 0);

	/* Enable DLL bypass signal*/
	dll_cntl            = PHM_SET_FIELD(dll_cntl,
		DLL_CNTL, MRDCK0_BYPASS, 0);
	dll_cntl            = PHM_SET_FIELD(dll_cntl,
		DLL_CNTL, MRDCK1_BYPASS, 0);

	table->MemoryACPILevel.DllCntl            =
		PP_HOST_TO_SMC_UL(dll_cntl);
	table->MemoryACPILevel.MclkPwrmgtCntl     =
		PP_HOST_TO_SMC_UL(mclk_pwrmgt_cntl);
	table->MemoryACPILevel.MpllAdFuncCntl     =
		PP_HOST_TO_SMC_UL(data->clock_registers.vMPLL_AD_FUNC_CNTL);
	table->MemoryACPILevel.MpllDqFuncCntl     =
		PP_HOST_TO_SMC_UL(data->clock_registers.vMPLL_DQ_FUNC_CNTL);
	table->MemoryACPILevel.MpllFuncCntl       =
		PP_HOST_TO_SMC_UL(data->clock_registers.vMPLL_FUNC_CNTL);
	table->MemoryACPILevel.MpllFuncCntl_1     =
		PP_HOST_TO_SMC_UL(data->clock_registers.vMPLL_FUNC_CNTL_1);
	table->MemoryACPILevel.MpllFuncCntl_2     =
		PP_HOST_TO_SMC_UL(data->clock_registers.vMPLL_FUNC_CNTL_2);
	table->MemoryACPILevel.MpllSs1            =
		PP_HOST_TO_SMC_UL(data->clock_registers.vMPLL_SS1);
	table->MemoryACPILevel.MpllSs2            =
		PP_HOST_TO_SMC_UL(data->clock_registers.vMPLL_SS2);

	table->MemoryACPILevel.EnabledForThrottle = 0;
	table->MemoryACPILevel.EnabledForActivity = 0;
	table->MemoryACPILevel.UpHyst = 0;
	table->MemoryACPILevel.DownHyst = 100;
	table->MemoryACPILevel.VoltageDownHyst = 0;
	/* Indicates maximum activity level for this performance level.*/
	table->MemoryACPILevel.ActivityLevel = PP_HOST_TO_SMC_US(data->current_profile_setting.mclk_activity);

	table->MemoryACPILevel.StutterEnable = 0;
	table->MemoryACPILevel.StrobeEnable = 0;
	table->MemoryACPILevel.EdcReadEnable = 0;
	table->MemoryACPILevel.EdcWriteEnable = 0;
	table->MemoryACPILevel.RttEnable = 0;

	return result;
}

static int iceland_populate_smc_uvd_level(struct pp_hwmgr *hwmgr,
					SMU71_Discrete_DpmTable *table)
{
	return 0;
}

static int iceland_populate_smc_vce_level(struct pp_hwmgr *hwmgr,
		SMU71_Discrete_DpmTable *table)
{
	return 0;
}

static int iceland_populate_smc_acp_level(struct pp_hwmgr *hwmgr,
		SMU71_Discrete_DpmTable *table)
{
	return 0;
}

static int iceland_populate_memory_timing_parameters(
		struct pp_hwmgr *hwmgr,
		uint32_t engine_clock,
		uint32_t memory_clock,
		struct SMU71_Discrete_MCArbDramTimingTableEntry *arb_regs
		)
{
	uint32_t dramTiming;
	uint32_t dramTiming2;
	uint32_t burstTime;
	int result;

	result = atomctrl_set_engine_dram_timings_rv770(hwmgr,
				engine_clock, memory_clock);

	PP_ASSERT_WITH_CODE(result == 0,
		"Error calling VBIOS to set DRAM_TIMING.", return result);

	dramTiming  = cgs_read_register(hwmgr->device, mmMC_ARB_DRAM_TIMING);
	dramTiming2 = cgs_read_register(hwmgr->device, mmMC_ARB_DRAM_TIMING2);
	burstTime = PHM_READ_FIELD(hwmgr->device, MC_ARB_BURST_TIME, STATE0);

	arb_regs->McArbDramTiming  = PP_HOST_TO_SMC_UL(dramTiming);
	arb_regs->McArbDramTiming2 = PP_HOST_TO_SMC_UL(dramTiming2);
	arb_regs->McArbBurstTime = (uint8_t)burstTime;

	return 0;
}

static int iceland_program_memory_timing_parameters(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	int result = 0;
	SMU71_Discrete_MCArbDramTimingTable  arb_regs;
	uint32_t i, j;

	memset(&arb_regs, 0x00, sizeof(SMU71_Discrete_MCArbDramTimingTable));

	for (i = 0; i < data->dpm_table.sclk_table.count; i++) {
		for (j = 0; j < data->dpm_table.mclk_table.count; j++) {
			result = iceland_populate_memory_timing_parameters
				(hwmgr, data->dpm_table.sclk_table.dpm_levels[i].value,
				 data->dpm_table.mclk_table.dpm_levels[j].value,
				 &arb_regs.entries[i][j]);

			if (0 != result) {
				break;
			}
		}
	}

	if (0 == result) {
		result = smu7_copy_bytes_to_smc(
				hwmgr,
				smu_data->smu7_data.arb_table_start,
				(uint8_t *)&arb_regs,
				sizeof(SMU71_Discrete_MCArbDramTimingTable),
				SMC_RAM_END
				);
	}

	return result;
}

static int iceland_populate_smc_boot_level(struct pp_hwmgr *hwmgr,
			SMU71_Discrete_DpmTable *table)
{
	int result = 0;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	table->GraphicsBootLevel = 0;
	table->MemoryBootLevel = 0;

	/* find boot level from dpm table*/
	result = phm_find_boot_level(&(data->dpm_table.sclk_table),
			data->vbios_boot_state.sclk_bootup_value,
			(uint32_t *)&(smu_data->smc_state_table.GraphicsBootLevel));

	if (0 != result) {
		smu_data->smc_state_table.GraphicsBootLevel = 0;
		pr_err("VBIOS did not find boot engine clock value in dependency table. Using Graphics DPM level 0!\n");
		result = 0;
	}

	result = phm_find_boot_level(&(data->dpm_table.mclk_table),
		data->vbios_boot_state.mclk_bootup_value,
		(uint32_t *)&(smu_data->smc_state_table.MemoryBootLevel));

	if (0 != result) {
		smu_data->smc_state_table.MemoryBootLevel = 0;
		pr_err("VBIOS did not find boot engine clock value in dependency table. Using Memory DPM level 0!\n");
		result = 0;
	}

	table->BootVddc = data->vbios_boot_state.vddc_bootup_value;
	if (SMU7_VOLTAGE_CONTROL_NONE == data->vddci_control)
		table->BootVddci = table->BootVddc;
	else
		table->BootVddci = data->vbios_boot_state.vddci_bootup_value;

	table->BootMVdd = data->vbios_boot_state.mvdd_bootup_value;

	return result;
}

static int iceland_populate_mc_reg_address(struct pp_hwmgr *hwmgr,
				 SMU71_Discrete_MCRegisters *mc_reg_table)
{
	const struct iceland_smumgr *smu_data = (struct iceland_smumgr *)hwmgr->smu_backend;

	uint32_t i, j;

	for (i = 0, j = 0; j < smu_data->mc_reg_table.last; j++) {
		if (smu_data->mc_reg_table.validflag & 1<<j) {
			PP_ASSERT_WITH_CODE(i < SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE,
				"Index of mc_reg_table->address[] array out of boundary", return -EINVAL);
			mc_reg_table->address[i].s0 =
				PP_HOST_TO_SMC_US(smu_data->mc_reg_table.mc_reg_address[j].s0);
			mc_reg_table->address[i].s1 =
				PP_HOST_TO_SMC_US(smu_data->mc_reg_table.mc_reg_address[j].s1);
			i++;
		}
	}

	mc_reg_table->last = (uint8_t)i;

	return 0;
}

/*convert register values from driver to SMC format */
static void iceland_convert_mc_registers(
	const struct iceland_mc_reg_entry *entry,
	SMU71_Discrete_MCRegisterSet *data,
	uint32_t num_entries, uint32_t valid_flag)
{
	uint32_t i, j;

	for (i = 0, j = 0; j < num_entries; j++) {
		if (valid_flag & 1<<j) {
			data->value[i] = PP_HOST_TO_SMC_UL(entry->mc_data[j]);
			i++;
		}
	}
}

static int iceland_convert_mc_reg_table_entry_to_smc(struct pp_hwmgr *hwmgr,
		const uint32_t memory_clock,
		SMU71_Discrete_MCRegisterSet *mc_reg_table_data
		)
{
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	uint32_t i = 0;

	for (i = 0; i < smu_data->mc_reg_table.num_entries; i++) {
		if (memory_clock <=
			smu_data->mc_reg_table.mc_reg_table_entry[i].mclk_max) {
			break;
		}
	}

	if ((i == smu_data->mc_reg_table.num_entries) && (i > 0))
		--i;

	iceland_convert_mc_registers(&smu_data->mc_reg_table.mc_reg_table_entry[i],
				mc_reg_table_data, smu_data->mc_reg_table.last,
				smu_data->mc_reg_table.validflag);

	return 0;
}

static int iceland_convert_mc_reg_table_to_smc(struct pp_hwmgr *hwmgr,
		SMU71_Discrete_MCRegisters *mc_regs)
{
	int result = 0;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	int res;
	uint32_t i;

	for (i = 0; i < data->dpm_table.mclk_table.count; i++) {
		res = iceland_convert_mc_reg_table_entry_to_smc(
				hwmgr,
				data->dpm_table.mclk_table.dpm_levels[i].value,
				&mc_regs->data[i]
				);

		if (0 != res)
			result = res;
	}

	return result;
}

static int iceland_update_and_upload_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	uint32_t address;
	int32_t result;

	if (0 == (data->need_update_smu7_dpm_table & DPMTABLE_OD_UPDATE_MCLK))
		return 0;


	memset(&smu_data->mc_regs, 0, sizeof(SMU71_Discrete_MCRegisters));

	result = iceland_convert_mc_reg_table_to_smc(hwmgr, &(smu_data->mc_regs));

	if (result != 0)
		return result;


	address = smu_data->smu7_data.mc_reg_table_start + (uint32_t)offsetof(SMU71_Discrete_MCRegisters, data[0]);

	return  smu7_copy_bytes_to_smc(hwmgr, address,
				 (uint8_t *)&smu_data->mc_regs.data[0],
				sizeof(SMU71_Discrete_MCRegisterSet) * data->dpm_table.mclk_table.count,
				SMC_RAM_END);
}

static int iceland_populate_initial_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);

	memset(&smu_data->mc_regs, 0x00, sizeof(SMU71_Discrete_MCRegisters));
	result = iceland_populate_mc_reg_address(hwmgr, &(smu_data->mc_regs));
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize MCRegTable for the MC register addresses!", return result;);

	result = iceland_convert_mc_reg_table_to_smc(hwmgr, &smu_data->mc_regs);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize MCRegTable for driver state!", return result;);

	return smu7_copy_bytes_to_smc(hwmgr, smu_data->smu7_data.mc_reg_table_start,
			(uint8_t *)&smu_data->mc_regs, sizeof(SMU71_Discrete_MCRegisters), SMC_RAM_END);
}

static int iceland_populate_smc_initial_state(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	uint8_t count, level;

	count = (uint8_t)(hwmgr->dyn_state.vddc_dependency_on_sclk->count);

	for (level = 0; level < count; level++) {
		if (hwmgr->dyn_state.vddc_dependency_on_sclk->entries[level].clk
			 >= data->vbios_boot_state.sclk_bootup_value) {
			smu_data->smc_state_table.GraphicsBootLevel = level;
			break;
		}
	}

	count = (uint8_t)(hwmgr->dyn_state.vddc_dependency_on_mclk->count);

	for (level = 0; level < count; level++) {
		if (hwmgr->dyn_state.vddc_dependency_on_mclk->entries[level].clk
			>= data->vbios_boot_state.mclk_bootup_value) {
			smu_data->smc_state_table.MemoryBootLevel = level;
			break;
		}
	}

	return 0;
}

static int iceland_populate_bapm_parameters_in_dpm_table(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	const struct iceland_pt_defaults *defaults = smu_data->power_tune_defaults;
	SMU71_Discrete_DpmTable  *dpm_table = &(smu_data->smc_state_table);
	struct phm_cac_tdp_table *cac_dtp_table = hwmgr->dyn_state.cac_dtp_table;
	struct phm_ppm_table *ppm = hwmgr->dyn_state.ppm_parameter_table;
	const uint16_t *def1, *def2;
	int i, j, k;


	/*
	 * TDP number of fraction bits are changed from 8 to 7 for Iceland
	 * as requested by SMC team
	 */

	dpm_table->DefaultTdp = PP_HOST_TO_SMC_US((uint16_t)(cac_dtp_table->usTDP * 256));
	dpm_table->TargetTdp = PP_HOST_TO_SMC_US((uint16_t)(cac_dtp_table->usConfigurableTDP * 256));


	dpm_table->DTETjOffset = 0;

	dpm_table->GpuTjMax = (uint8_t)(data->thermal_temp_setting.temperature_high / PP_TEMPERATURE_UNITS_PER_CENTIGRADES);
	dpm_table->GpuTjHyst = 8;

	dpm_table->DTEAmbientTempBase = defaults->dte_ambient_temp_base;

	/* The following are for new Iceland Multi-input fan/thermal control */
	if (NULL != ppm) {
		dpm_table->PPM_PkgPwrLimit = (uint16_t)ppm->dgpu_tdp * 256 / 1000;
		dpm_table->PPM_TemperatureLimit = (uint16_t)ppm->tj_max * 256;
	} else {
		dpm_table->PPM_PkgPwrLimit = 0;
		dpm_table->PPM_TemperatureLimit = 0;
	}

	CONVERT_FROM_HOST_TO_SMC_US(dpm_table->PPM_PkgPwrLimit);
	CONVERT_FROM_HOST_TO_SMC_US(dpm_table->PPM_TemperatureLimit);

	dpm_table->BAPM_TEMP_GRADIENT = PP_HOST_TO_SMC_UL(defaults->bapm_temp_gradient);
	def1 = defaults->bapmti_r;
	def2 = defaults->bapmti_rc;

	for (i = 0; i < SMU71_DTE_ITERATIONS; i++) {
		for (j = 0; j < SMU71_DTE_SOURCES; j++) {
			for (k = 0; k < SMU71_DTE_SINKS; k++) {
				dpm_table->BAPMTI_R[i][j][k] = PP_HOST_TO_SMC_US(*def1);
				dpm_table->BAPMTI_RC[i][j][k] = PP_HOST_TO_SMC_US(*def2);
				def1++;
				def2++;
			}
		}
	}

	return 0;
}

static int iceland_populate_smc_svi2_config(struct pp_hwmgr *hwmgr,
					    SMU71_Discrete_DpmTable *tab)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->voltage_control)
		tab->SVI2Enable |= VDDC_ON_SVI2;

	if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->vddci_control)
		tab->SVI2Enable |= VDDCI_ON_SVI2;
	else
		tab->MergedVddci = 1;

	if (SMU7_VOLTAGE_CONTROL_BY_SVID2 == data->mvdd_control)
		tab->SVI2Enable |= MVDD_ON_SVI2;

	PP_ASSERT_WITH_CODE(tab->SVI2Enable != (VDDC_ON_SVI2 | VDDCI_ON_SVI2 | MVDD_ON_SVI2) &&
		(tab->SVI2Enable & VDDC_ON_SVI2), "SVI2 domain configuration is incorrect!", return -EINVAL);

	return 0;
}

static int iceland_init_smc_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	SMU71_Discrete_DpmTable  *table = &(smu_data->smc_state_table);


	iceland_initialize_power_tune_defaults(hwmgr);
	memset(&(smu_data->smc_state_table), 0x00, sizeof(smu_data->smc_state_table));

	if (SMU7_VOLTAGE_CONTROL_NONE != data->voltage_control) {
		iceland_populate_smc_voltage_tables(hwmgr, table);
	}

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_AutomaticDCTransition))
		table->SystemFlags |= PPSMC_SYSTEMFLAG_GPIO_DC;


	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_StepVddc))
		table->SystemFlags |= PPSMC_SYSTEMFLAG_STEPVDDC;

	if (data->is_memory_gddr5)
		table->SystemFlags |= PPSMC_SYSTEMFLAG_GDDR5;


	if (data->ulv_supported) {
		result = iceland_populate_ulv_state(hwmgr, &(smu_data->ulv_setting));
		PP_ASSERT_WITH_CODE(0 == result,
			"Failed to initialize ULV state!", return result;);

		cgs_write_ind_register(hwmgr->device, CGS_IND_REG__SMC,
			ixCG_ULV_PARAMETER, 0x40035);
	}

	result = iceland_populate_smc_link_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize Link Level!", return result;);

	result = iceland_populate_all_graphic_levels(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize Graphics Level!", return result;);

	result = iceland_populate_all_memory_levels(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize Memory Level!", return result;);

	result = iceland_populate_smc_acpi_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize ACPI Level!", return result;);

	result = iceland_populate_smc_vce_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize VCE Level!", return result;);

	result = iceland_populate_smc_acp_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize ACP Level!", return result;);

	/* Since only the initial state is completely set up at this point (the other states are just copies of the boot state) we only */
	/* need to populate the  ARB settings for the initial state. */
	result = iceland_program_memory_timing_parameters(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to Write ARB settings for the initial state.", return result;);

	result = iceland_populate_smc_uvd_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize UVD Level!", return result;);

	table->GraphicsBootLevel = 0;
	table->MemoryBootLevel = 0;

	result = iceland_populate_smc_boot_level(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to initialize Boot Level!", return result;);

	result = iceland_populate_smc_initial_state(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result, "Failed to initialize Boot State!", return result);

	result = iceland_populate_bapm_parameters_in_dpm_table(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result, "Failed to populate BAPM Parameters!", return result);

	table->GraphicsVoltageChangeEnable  = 1;
	table->GraphicsThermThrottleEnable  = 1;
	table->GraphicsInterval = 1;
	table->VoltageInterval  = 1;
	table->ThermalInterval  = 1;

	table->TemperatureLimitHigh =
		(data->thermal_temp_setting.temperature_high *
		 SMU7_Q88_FORMAT_CONVERSION_UNIT) / PP_TEMPERATURE_UNITS_PER_CENTIGRADES;
	table->TemperatureLimitLow =
		(data->thermal_temp_setting.temperature_low *
		SMU7_Q88_FORMAT_CONVERSION_UNIT) / PP_TEMPERATURE_UNITS_PER_CENTIGRADES;

	table->MemoryVoltageChangeEnable  = 1;
	table->MemoryInterval  = 1;
	table->VoltageResponseTime  = 0;
	table->PhaseResponseTime  = 0;
	table->MemoryThermThrottleEnable  = 1;
	table->PCIeBootLinkLevel = 0;
	table->PCIeGenInterval = 1;

	result = iceland_populate_smc_svi2_config(hwmgr, table);
	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to populate SVI2 setting!", return result);

	table->ThermGpio  = 17;
	table->SclkStepSize = 0x4000;

	CONVERT_FROM_HOST_TO_SMC_UL(table->SystemFlags);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SmioMaskVddcVid);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SmioMaskVddcPhase);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SmioMaskVddciVid);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SmioMaskMvddVid);
	CONVERT_FROM_HOST_TO_SMC_UL(table->SclkStepSize);
	CONVERT_FROM_HOST_TO_SMC_US(table->TemperatureLimitHigh);
	CONVERT_FROM_HOST_TO_SMC_US(table->TemperatureLimitLow);
	CONVERT_FROM_HOST_TO_SMC_US(table->VoltageResponseTime);
	CONVERT_FROM_HOST_TO_SMC_US(table->PhaseResponseTime);

	table->BootVddc = PP_HOST_TO_SMC_US(table->BootVddc * VOLTAGE_SCALE);
	table->BootVddci = PP_HOST_TO_SMC_US(table->BootVddci * VOLTAGE_SCALE);
	table->BootMVdd = PP_HOST_TO_SMC_US(table->BootMVdd * VOLTAGE_SCALE);

	/* Upload all dpm data to SMC memory.(dpm level, dpm level count etc) */
	result = smu7_copy_bytes_to_smc(hwmgr, smu_data->smu7_data.dpm_table_start +
										offsetof(SMU71_Discrete_DpmTable, SystemFlags),
										(uint8_t *)&(table->SystemFlags),
										sizeof(SMU71_Discrete_DpmTable)-3 * sizeof(SMU71_PIDController),
										SMC_RAM_END);

	PP_ASSERT_WITH_CODE(0 == result,
		"Failed to upload dpm data to SMC memory!", return result;);

	/* Upload all ulv setting to SMC memory.(dpm level, dpm level count etc) */
	result = smu7_copy_bytes_to_smc(hwmgr,
			smu_data->smu7_data.ulv_setting_starts,
			(uint8_t *)&(smu_data->ulv_setting),
			sizeof(SMU71_Discrete_Ulv),
			SMC_RAM_END);


	result = iceland_populate_initial_mc_reg_table(hwmgr);
	PP_ASSERT_WITH_CODE((0 == result),
		"Failed to populate initialize MC Reg table!", return result);

	result = iceland_populate_pm_fuses(hwmgr);
	PP_ASSERT_WITH_CODE(0 == result,
			"Failed to  populate PM fuses to SMC memory!", return result);

	return 0;
}

static int iceland_thermal_setup_fan_table(struct pp_hwmgr *hwmgr)
{
	struct smu7_smumgr *smu7_data = (struct smu7_smumgr *)(hwmgr->smu_backend);
	SMU71_Discrete_FanTable fan_table = { FDO_MODE_HARDWARE };
	uint32_t duty100;
	uint32_t t_diff1, t_diff2, pwm_diff1, pwm_diff2;
	uint16_t fdo_min, slope1, slope2;
	uint32_t reference_clock;
	int res;
	uint64_t tmp64;

	if (!phm_cap_enabled(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_MicrocodeFanControl))
		return 0;

	if (hwmgr->thermal_controller.fanInfo.bNoFan) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_MicrocodeFanControl);
		return 0;
	}

	if (0 == smu7_data->fan_table_start) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_MicrocodeFanControl);
		return 0;
	}

	duty100 = PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, CG_FDO_CTRL1, FMAX_DUTY100);

	if (0 == duty100) {
		phm_cap_unset(hwmgr->platform_descriptor.platformCaps, PHM_PlatformCaps_MicrocodeFanControl);
		return 0;
	}

	tmp64 = hwmgr->thermal_controller.advanceFanControlParameters.usPWMMin * duty100;
	do_div(tmp64, 10000);
	fdo_min = (uint16_t)tmp64;

	t_diff1 = hwmgr->thermal_controller.advanceFanControlParameters.usTMed - hwmgr->thermal_controller.advanceFanControlParameters.usTMin;
	t_diff2 = hwmgr->thermal_controller.advanceFanControlParameters.usTHigh - hwmgr->thermal_controller.advanceFanControlParameters.usTMed;

	pwm_diff1 = hwmgr->thermal_controller.advanceFanControlParameters.usPWMMed - hwmgr->thermal_controller.advanceFanControlParameters.usPWMMin;
	pwm_diff2 = hwmgr->thermal_controller.advanceFanControlParameters.usPWMHigh - hwmgr->thermal_controller.advanceFanControlParameters.usPWMMed;

	slope1 = (uint16_t)((50 + ((16 * duty100 * pwm_diff1) / t_diff1)) / 100);
	slope2 = (uint16_t)((50 + ((16 * duty100 * pwm_diff2) / t_diff2)) / 100);

	fan_table.TempMin = cpu_to_be16((50 + hwmgr->thermal_controller.advanceFanControlParameters.usTMin) / 100);
	fan_table.TempMed = cpu_to_be16((50 + hwmgr->thermal_controller.advanceFanControlParameters.usTMed) / 100);
	fan_table.TempMax = cpu_to_be16((50 + hwmgr->thermal_controller.advanceFanControlParameters.usTMax) / 100);

	fan_table.Slope1 = cpu_to_be16(slope1);
	fan_table.Slope2 = cpu_to_be16(slope2);

	fan_table.FdoMin = cpu_to_be16(fdo_min);

	fan_table.HystDown = cpu_to_be16(hwmgr->thermal_controller.advanceFanControlParameters.ucTHyst);

	fan_table.HystUp = cpu_to_be16(1);

	fan_table.HystSlope = cpu_to_be16(1);

	fan_table.TempRespLim = cpu_to_be16(5);

	reference_clock = amdgpu_asic_get_xclk((struct amdgpu_device *)hwmgr->adev);

	fan_table.RefreshPeriod = cpu_to_be32((hwmgr->thermal_controller.advanceFanControlParameters.ulCycleDelay * reference_clock) / 1600);

	fan_table.FdoMax = cpu_to_be16((uint16_t)duty100);

	fan_table.TempSrc = (uint8_t)PHM_READ_VFPF_INDIRECT_FIELD(hwmgr->device, CGS_IND_REG__SMC, CG_MULT_THERMAL_CTRL, TEMP_SEL);

	/* fan_table.FanControl_GL_Flag = 1; */

	res = smu7_copy_bytes_to_smc(hwmgr, smu7_data->fan_table_start, (uint8_t *)&fan_table, (uint32_t)sizeof(fan_table), SMC_RAM_END);

	return res;
}


static int iceland_program_mem_timing_parameters(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	if (data->need_update_smu7_dpm_table &
		(DPMTABLE_OD_UPDATE_SCLK | DPMTABLE_OD_UPDATE_MCLK))
		return iceland_program_memory_timing_parameters(hwmgr);

	return 0;
}

static int iceland_update_sclk_threshold(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);

	int result = 0;
	uint32_t low_sclk_interrupt_threshold = 0;

	if (phm_cap_enabled(hwmgr->platform_descriptor.platformCaps,
			PHM_PlatformCaps_SclkThrottleLowNotification)
		&& (data->low_sclk_interrupt_threshold != 0)) {
		low_sclk_interrupt_threshold =
				data->low_sclk_interrupt_threshold;

		CONVERT_FROM_HOST_TO_SMC_UL(low_sclk_interrupt_threshold);

		result = smu7_copy_bytes_to_smc(
				hwmgr,
				smu_data->smu7_data.dpm_table_start +
				offsetof(SMU71_Discrete_DpmTable,
					LowSclkInterruptThreshold),
				(uint8_t *)&low_sclk_interrupt_threshold,
				sizeof(uint32_t),
				SMC_RAM_END);
	}

	result = iceland_update_and_upload_mc_reg_table(hwmgr);

	PP_ASSERT_WITH_CODE((0 == result), "Failed to upload MC reg table!", return result);

	result = iceland_program_mem_timing_parameters(hwmgr);
	PP_ASSERT_WITH_CODE((result == 0),
			"Failed to program memory timing parameters!",
			);

	return result;
}

static uint32_t iceland_get_offsetof(uint32_t type, uint32_t member)
{
	switch (type) {
	case SMU_SoftRegisters:
		switch (member) {
		case HandshakeDisables:
			return offsetof(SMU71_SoftRegisters, HandshakeDisables);
		case VoltageChangeTimeout:
			return offsetof(SMU71_SoftRegisters, VoltageChangeTimeout);
		case AverageGraphicsActivity:
			return offsetof(SMU71_SoftRegisters, AverageGraphicsActivity);
		case AverageMemoryActivity:
			return offsetof(SMU71_SoftRegisters, AverageMemoryActivity);
		case PreVBlankGap:
			return offsetof(SMU71_SoftRegisters, PreVBlankGap);
		case VBlankTimeout:
			return offsetof(SMU71_SoftRegisters, VBlankTimeout);
		case UcodeLoadStatus:
			return offsetof(SMU71_SoftRegisters, UcodeLoadStatus);
		case DRAM_LOG_ADDR_H:
			return offsetof(SMU71_SoftRegisters, DRAM_LOG_ADDR_H);
		case DRAM_LOG_ADDR_L:
			return offsetof(SMU71_SoftRegisters, DRAM_LOG_ADDR_L);
		case DRAM_LOG_PHY_ADDR_H:
			return offsetof(SMU71_SoftRegisters, DRAM_LOG_PHY_ADDR_H);
		case DRAM_LOG_PHY_ADDR_L:
			return offsetof(SMU71_SoftRegisters, DRAM_LOG_PHY_ADDR_L);
		case DRAM_LOG_BUFF_SIZE:
			return offsetof(SMU71_SoftRegisters, DRAM_LOG_BUFF_SIZE);
		}
		break;
	case SMU_Discrete_DpmTable:
		switch (member) {
		case LowSclkInterruptThreshold:
			return offsetof(SMU71_Discrete_DpmTable, LowSclkInterruptThreshold);
		}
		break;
	}
	pr_warn("can't get the offset of type %x member %x\n", type, member);
	return 0;
}

static uint32_t iceland_get_mac_definition(uint32_t value)
{
	switch (value) {
	case SMU_MAX_LEVELS_GRAPHICS:
		return SMU71_MAX_LEVELS_GRAPHICS;
	case SMU_MAX_LEVELS_MEMORY:
		return SMU71_MAX_LEVELS_MEMORY;
	case SMU_MAX_LEVELS_LINK:
		return SMU71_MAX_LEVELS_LINK;
	case SMU_MAX_ENTRIES_SMIO:
		return SMU71_MAX_ENTRIES_SMIO;
	case SMU_MAX_LEVELS_VDDC:
		return SMU71_MAX_LEVELS_VDDC;
	case SMU_MAX_LEVELS_VDDCI:
		return SMU71_MAX_LEVELS_VDDCI;
	case SMU_MAX_LEVELS_MVDD:
		return SMU71_MAX_LEVELS_MVDD;
	}

	pr_warn("can't get the mac of %x\n", value);
	return 0;
}

static int iceland_process_firmware_header(struct pp_hwmgr *hwmgr)
{
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);
	struct smu7_smumgr *smu7_data = (struct smu7_smumgr *)(hwmgr->smu_backend);

	uint32_t tmp;
	int result;
	bool error = false;

	result = smu7_read_smc_sram_dword(hwmgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, DpmTable),
				&tmp, SMC_RAM_END);

	if (0 == result) {
		smu7_data->dpm_table_start = tmp;
	}

	error |= (0 != result);

	result = smu7_read_smc_sram_dword(hwmgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, SoftRegisters),
				&tmp, SMC_RAM_END);

	if (0 == result) {
		data->soft_regs_start = tmp;
		smu7_data->soft_regs_start = tmp;
	}

	error |= (0 != result);


	result = smu7_read_smc_sram_dword(hwmgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, mcRegisterTable),
				&tmp, SMC_RAM_END);

	if (0 == result) {
		smu7_data->mc_reg_table_start = tmp;
	}

	result = smu7_read_smc_sram_dword(hwmgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, FanTable),
				&tmp, SMC_RAM_END);

	if (0 == result) {
		smu7_data->fan_table_start = tmp;
	}

	error |= (0 != result);

	result = smu7_read_smc_sram_dword(hwmgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, mcArbDramTimingTable),
				&tmp, SMC_RAM_END);

	if (0 == result) {
		smu7_data->arb_table_start = tmp;
	}

	error |= (0 != result);


	result = smu7_read_smc_sram_dword(hwmgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, Version),
				&tmp, SMC_RAM_END);

	if (0 == result) {
		hwmgr->microcode_version_info.SMC = tmp;
	}

	error |= (0 != result);

	result = smu7_read_smc_sram_dword(hwmgr,
				SMU71_FIRMWARE_HEADER_LOCATION +
				offsetof(SMU71_Firmware_Header, UlvSettings),
				&tmp, SMC_RAM_END);

	if (0 == result) {
		smu7_data->ulv_setting_starts = tmp;
	}

	error |= (0 != result);

	return error ? 1 : 0;
}

/*---------------------------MC----------------------------*/

static uint8_t iceland_get_memory_modile_index(struct pp_hwmgr *hwmgr)
{
	return (uint8_t) (0xFF & (cgs_read_register(hwmgr->device, mmBIOS_SCRATCH_4) >> 16));
}

static bool iceland_check_s0_mc_reg_index(uint16_t in_reg, uint16_t *out_reg)
{
	bool result = true;

	switch (in_reg) {
	case  mmMC_SEQ_RAS_TIMING:
		*out_reg = mmMC_SEQ_RAS_TIMING_LP;
		break;

	case  mmMC_SEQ_DLL_STBY:
		*out_reg = mmMC_SEQ_DLL_STBY_LP;
		break;

	case  mmMC_SEQ_G5PDX_CMD0:
		*out_reg = mmMC_SEQ_G5PDX_CMD0_LP;
		break;

	case  mmMC_SEQ_G5PDX_CMD1:
		*out_reg = mmMC_SEQ_G5PDX_CMD1_LP;
		break;

	case  mmMC_SEQ_G5PDX_CTRL:
		*out_reg = mmMC_SEQ_G5PDX_CTRL_LP;
		break;

	case mmMC_SEQ_CAS_TIMING:
		*out_reg = mmMC_SEQ_CAS_TIMING_LP;
		break;

	case mmMC_SEQ_MISC_TIMING:
		*out_reg = mmMC_SEQ_MISC_TIMING_LP;
		break;

	case mmMC_SEQ_MISC_TIMING2:
		*out_reg = mmMC_SEQ_MISC_TIMING2_LP;
		break;

	case mmMC_SEQ_PMG_DVS_CMD:
		*out_reg = mmMC_SEQ_PMG_DVS_CMD_LP;
		break;

	case mmMC_SEQ_PMG_DVS_CTL:
		*out_reg = mmMC_SEQ_PMG_DVS_CTL_LP;
		break;

	case mmMC_SEQ_RD_CTL_D0:
		*out_reg = mmMC_SEQ_RD_CTL_D0_LP;
		break;

	case mmMC_SEQ_RD_CTL_D1:
		*out_reg = mmMC_SEQ_RD_CTL_D1_LP;
		break;

	case mmMC_SEQ_WR_CTL_D0:
		*out_reg = mmMC_SEQ_WR_CTL_D0_LP;
		break;

	case mmMC_SEQ_WR_CTL_D1:
		*out_reg = mmMC_SEQ_WR_CTL_D1_LP;
		break;

	case mmMC_PMG_CMD_EMRS:
		*out_reg = mmMC_SEQ_PMG_CMD_EMRS_LP;
		break;

	case mmMC_PMG_CMD_MRS:
		*out_reg = mmMC_SEQ_PMG_CMD_MRS_LP;
		break;

	case mmMC_PMG_CMD_MRS1:
		*out_reg = mmMC_SEQ_PMG_CMD_MRS1_LP;
		break;

	case mmMC_SEQ_PMG_TIMING:
		*out_reg = mmMC_SEQ_PMG_TIMING_LP;
		break;

	case mmMC_PMG_CMD_MRS2:
		*out_reg = mmMC_SEQ_PMG_CMD_MRS2_LP;
		break;

	case mmMC_SEQ_WR_CTL_2:
		*out_reg = mmMC_SEQ_WR_CTL_2_LP;
		break;

	default:
		result = false;
		break;
	}

	return result;
}

static int iceland_set_s0_mc_reg_index(struct iceland_mc_reg_table *table)
{
	uint32_t i;
	uint16_t address;

	for (i = 0; i < table->last; i++) {
		table->mc_reg_address[i].s0 =
			iceland_check_s0_mc_reg_index(table->mc_reg_address[i].s1, &address)
			? address : table->mc_reg_address[i].s1;
	}
	return 0;
}

static int iceland_copy_vbios_smc_reg_table(const pp_atomctrl_mc_reg_table *table,
					struct iceland_mc_reg_table *ni_table)
{
	uint8_t i, j;

	PP_ASSERT_WITH_CODE((table->last <= SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE),
		"Invalid VramInfo table.", return -EINVAL);
	PP_ASSERT_WITH_CODE((table->num_entries <= MAX_AC_TIMING_ENTRIES),
		"Invalid VramInfo table.", return -EINVAL);

	for (i = 0; i < table->last; i++) {
		ni_table->mc_reg_address[i].s1 = table->mc_reg_address[i].s1;
	}
	ni_table->last = table->last;

	for (i = 0; i < table->num_entries; i++) {
		ni_table->mc_reg_table_entry[i].mclk_max =
			table->mc_reg_table_entry[i].mclk_max;
		for (j = 0; j < table->last; j++) {
			ni_table->mc_reg_table_entry[i].mc_data[j] =
				table->mc_reg_table_entry[i].mc_data[j];
		}
	}

	ni_table->num_entries = table->num_entries;

	return 0;
}

static int iceland_set_mc_special_registers(struct pp_hwmgr *hwmgr,
					struct iceland_mc_reg_table *table)
{
	uint8_t i, j, k;
	uint32_t temp_reg;
	struct smu7_hwmgr *data = (struct smu7_hwmgr *)(hwmgr->backend);

	for (i = 0, j = table->last; i < table->last; i++) {
		PP_ASSERT_WITH_CODE((j < SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE),
			"Invalid VramInfo table.", return -EINVAL);

		switch (table->mc_reg_address[i].s1) {

		case mmMC_SEQ_MISC1:
			temp_reg = cgs_read_register(hwmgr->device, mmMC_PMG_CMD_EMRS);
			table->mc_reg_address[j].s1 = mmMC_PMG_CMD_EMRS;
			table->mc_reg_address[j].s0 = mmMC_SEQ_PMG_CMD_EMRS_LP;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					((temp_reg & 0xffff0000)) |
					((table->mc_reg_table_entry[k].mc_data[i] & 0xffff0000) >> 16);
			}
			j++;

			PP_ASSERT_WITH_CODE((j < SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE),
				"Invalid VramInfo table.", return -EINVAL);
			temp_reg = cgs_read_register(hwmgr->device, mmMC_PMG_CMD_MRS);
			table->mc_reg_address[j].s1 = mmMC_PMG_CMD_MRS;
			table->mc_reg_address[j].s0 = mmMC_SEQ_PMG_CMD_MRS_LP;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					(temp_reg & 0xffff0000) |
					(table->mc_reg_table_entry[k].mc_data[i] & 0x0000ffff);

				if (!data->is_memory_gddr5) {
					table->mc_reg_table_entry[k].mc_data[j] |= 0x100;
				}
			}
			j++;

			if (!data->is_memory_gddr5) {
				PP_ASSERT_WITH_CODE((j < SMU71_DISCRETE_MC_REGISTER_ARRAY_SIZE),
					"Invalid VramInfo table.", return -EINVAL);
				table->mc_reg_address[j].s1 = mmMC_PMG_AUTO_CMD;
				table->mc_reg_address[j].s0 = mmMC_PMG_AUTO_CMD;
				for (k = 0; k < table->num_entries; k++) {
					table->mc_reg_table_entry[k].mc_data[j] =
						(table->mc_reg_table_entry[k].mc_data[i] & 0xffff0000) >> 16;
				}
				j++;
			}

			break;

		case mmMC_SEQ_RESERVE_M:
			temp_reg = cgs_read_register(hwmgr->device, mmMC_PMG_CMD_MRS1);
			table->mc_reg_address[j].s1 = mmMC_PMG_CMD_MRS1;
			table->mc_reg_address[j].s0 = mmMC_SEQ_PMG_CMD_MRS1_LP;
			for (k = 0; k < table->num_entries; k++) {
				table->mc_reg_table_entry[k].mc_data[j] =
					(temp_reg & 0xffff0000) |
					(table->mc_reg_table_entry[k].mc_data[i] & 0x0000ffff);
			}
			j++;
			break;

		default:
			break;
		}

	}

	table->last = j;

	return 0;
}

static int iceland_set_valid_flag(struct iceland_mc_reg_table *table)
{
	uint8_t i, j;
	for (i = 0; i < table->last; i++) {
		for (j = 1; j < table->num_entries; j++) {
			if (table->mc_reg_table_entry[j-1].mc_data[i] !=
				table->mc_reg_table_entry[j].mc_data[i]) {
				table->validflag |= (1<<i);
				break;
			}
		}
	}

	return 0;
}

static int iceland_initialize_mc_reg_table(struct pp_hwmgr *hwmgr)
{
	int result;
	struct iceland_smumgr *smu_data = (struct iceland_smumgr *)(hwmgr->smu_backend);
	pp_atomctrl_mc_reg_table *table;
	struct iceland_mc_reg_table *ni_table = &smu_data->mc_reg_table;
	uint8_t module_index = iceland_get_memory_modile_index(hwmgr);

	table = kzalloc(sizeof(pp_atomctrl_mc_reg_table), GFP_KERNEL);

	if (NULL == table)
		return -ENOMEM;

	/* Program additional LP registers that are no longer programmed by VBIOS */
	cgs_write_register(hwmgr->device, mmMC_SEQ_RAS_TIMING_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_RAS_TIMING));
	cgs_write_register(hwmgr->device, mmMC_SEQ_CAS_TIMING_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_CAS_TIMING));
	cgs_write_register(hwmgr->device, mmMC_SEQ_DLL_STBY_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_DLL_STBY));
	cgs_write_register(hwmgr->device, mmMC_SEQ_G5PDX_CMD0_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_G5PDX_CMD0));
	cgs_write_register(hwmgr->device, mmMC_SEQ_G5PDX_CMD1_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_G5PDX_CMD1));
	cgs_write_register(hwmgr->device, mmMC_SEQ_G5PDX_CTRL_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_G5PDX_CTRL));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_DVS_CMD_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_PMG_DVS_CMD));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_DVS_CTL_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_PMG_DVS_CTL));
	cgs_write_register(hwmgr->device, mmMC_SEQ_MISC_TIMING_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_MISC_TIMING));
	cgs_write_register(hwmgr->device, mmMC_SEQ_MISC_TIMING2_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_MISC_TIMING2));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_CMD_EMRS_LP, cgs_read_register(hwmgr->device, mmMC_PMG_CMD_EMRS));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_CMD_MRS_LP, cgs_read_register(hwmgr->device, mmMC_PMG_CMD_MRS));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_CMD_MRS1_LP, cgs_read_register(hwmgr->device, mmMC_PMG_CMD_MRS1));
	cgs_write_register(hwmgr->device, mmMC_SEQ_WR_CTL_D0_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_WR_CTL_D0));
	cgs_write_register(hwmgr->device, mmMC_SEQ_WR_CTL_D1_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_WR_CTL_D1));
	cgs_write_register(hwmgr->device, mmMC_SEQ_RD_CTL_D0_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_RD_CTL_D0));
	cgs_write_register(hwmgr->device, mmMC_SEQ_RD_CTL_D1_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_RD_CTL_D1));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_TIMING_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_PMG_TIMING));
	cgs_write_register(hwmgr->device, mmMC_SEQ_PMG_CMD_MRS2_LP, cgs_read_register(hwmgr->device, mmMC_PMG_CMD_MRS2));
	cgs_write_register(hwmgr->device, mmMC_SEQ_WR_CTL_2_LP, cgs_read_register(hwmgr->device, mmMC_SEQ_WR_CTL_2));

	result = atomctrl_initialize_mc_reg_table(hwmgr, module_index, table);

	if (0 == result)
		result = iceland_copy_vbios_smc_reg_table(table, ni_table);

	if (0 == result) {
		iceland_set_s0_mc_reg_index(ni_table);
		result = iceland_set_mc_special_registers(hwmgr, ni_table);
	}

	if (0 == result)
		iceland_set_valid_flag(ni_table);

	kfree(table);

	return result;
}

static bool iceland_is_dpm_running(struct pp_hwmgr *hwmgr)
{
	return (1 == PHM_READ_INDIRECT_FIELD(hwmgr->device,
			CGS_IND_REG__SMC, FEATURE_STATUS, VOLTAGE_CONTROLLER_ON))
			? true : false;
}

const struct pp_smumgr_func iceland_smu_funcs = {
	.name = "iceland_smu",
	.smu_init = &iceland_smu_init,
	.smu_fini = &smu7_smu_fini,
	.start_smu = &iceland_start_smu,
	.check_fw_load_finish = &smu7_check_fw_load_finish,
	.request_smu_load_fw = &smu7_request_smu_load_fw,
	.request_smu_load_specific_fw = &iceland_request_smu_load_specific_fw,
	.send_msg_to_smc = &smu7_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = &smu7_send_msg_to_smc_with_parameter,
	.get_argument = smu7_get_argument,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
	.get_offsetof = iceland_get_offsetof,
	.process_firmware_header = iceland_process_firmware_header,
	.init_smc_table = iceland_init_smc_table,
	.update_sclk_threshold = iceland_update_sclk_threshold,
	.thermal_setup_fan_table = iceland_thermal_setup_fan_table,
	.populate_all_graphic_levels = iceland_populate_all_graphic_levels,
	.populate_all_memory_levels = iceland_populate_all_memory_levels,
	.get_mac_definition = iceland_get_mac_definition,
	.initialize_mc_reg_table = iceland_initialize_mc_reg_table,
	.is_dpm_running = iceland_is_dpm_running,
};

