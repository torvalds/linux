/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#include "pp_debug.h"
#include "smumgr.h"
#include "smu74.h"
#include "smu_ucode_xfer_vi.h"
#include "polaris10_smumgr.h"
#include "smu74_discrete.h"
#include "smu/smu_7_1_3_d.h"
#include "smu/smu_7_1_3_sh_mask.h"
#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"
#include "oss/oss_3_0_d.h"
#include "gca/gfx_8_0_d.h"
#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"
#include "polaris10_pwrvirus.h"
#include "ppatomctrl.h"
#include "cgs_common.h"
#include "polaris10_smc.h"
#include "smu7_ppsmc.h"
#include "smu7_smumgr.h"

#define PPPOLARIS10_TARGETACTIVITY_DFLT                     50

static const SMU74_Discrete_GraphicsLevel avfs_graphics_level_polaris10[8] = {
	/*  Min      pcie   DeepSleep Activity  CgSpll      CgSpll    CcPwr  CcPwr  Sclk         Enabled      Enabled                       Voltage    Power */
	/* Voltage, DpmLevel, DivId,  Level,  FuncCntl3,  FuncCntl4,  DynRm, DynRm1 Did, Padding,ForActivity, ForThrottle, UpHyst, DownHyst, DownHyst, Throttle */
	{ 0x100ea446, 0x00, 0x03, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0x30750000, 0x3000, 0, 0x2600, 0, 0, 0x0004, 0x8f02, 0xffff, 0x2f00, 0x300e, 0x2700 } },
	{ 0x400ea446, 0x01, 0x04, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0x409c0000, 0x2000, 0, 0x1e00, 1, 1, 0x0004, 0x8300, 0xffff, 0x1f00, 0xcb5e, 0x1a00 } },
	{ 0x740ea446, 0x01, 0x00, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0x50c30000, 0x2800, 0, 0x2000, 1, 1, 0x0004, 0x0c02, 0xffff, 0x2700, 0x6433, 0x2100 } },
	{ 0xa40ea446, 0x01, 0x00, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0x60ea0000, 0x3000, 0, 0x2600, 1, 1, 0x0004, 0x8f02, 0xffff, 0x2f00, 0x300e, 0x2700 } },
	{ 0xd80ea446, 0x01, 0x00, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0x70110100, 0x3800, 0, 0x2c00, 1, 1, 0x0004, 0x1203, 0xffff, 0x3600, 0xc9e2, 0x2e00 } },
	{ 0x3c0fa446, 0x01, 0x00, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0x80380100, 0x2000, 0, 0x1e00, 2, 1, 0x0004, 0x8300, 0xffff, 0x1f00, 0xcb5e, 0x1a00 } },
	{ 0x6c0fa446, 0x01, 0x00, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0x905f0100, 0x2400, 0, 0x1e00, 2, 1, 0x0004, 0x8901, 0xffff, 0x2300, 0x314c, 0x1d00 } },
	{ 0xa00fa446, 0x01, 0x00, 0x3200, 0, 0, 0, 0, 0, 0, 0x01, 0x01, 0x0a, 0x00, 0x00, 0x00, { 0xa0860100, 0x2800, 0, 0x2000, 2, 1, 0x0004, 0x0c02, 0xffff, 0x2700, 0x6433, 0x2100 } }
};

static const SMU74_Discrete_MemoryLevel avfs_memory_level_polaris10 = {
	0x100ea446, 0, 0x30750000, 0x01, 0x01, 0x01, 0x00, 0x00, 0x64, 0x00, 0x00, 0x1f00, 0x00, 0x00};

static int polaris10_setup_pwr_virus(struct pp_smumgr *smumgr)
{
	int i;
	int result = -EINVAL;
	uint32_t reg, data;

	const PWR_Command_Table *pvirus = pwr_virus_table;
	struct smu7_smumgr *smu_data = (struct smu7_smumgr *)(smumgr->backend);

	for (i = 0; i < PWR_VIRUS_TABLE_SIZE; i++) {
		switch (pvirus->command) {
		case PwrCmdWrite:
			reg  = pvirus->reg;
			data = pvirus->data;
			cgs_write_register(smumgr->device, reg, data);
			break;

		case PwrCmdEnd:
			result = 0;
			break;

		default:
			pr_info("Table Exit with Invalid Command!");
			smu_data->avfs.avfs_btc_status = AVFS_BTC_VIRUS_FAIL;
			result = -EINVAL;
			break;
		}
		pvirus++;
	}

	return result;
}

static int polaris10_perform_btc(struct pp_smumgr *smumgr)
{
	int result = 0;
	struct smu7_smumgr *smu_data = (struct smu7_smumgr *)(smumgr->backend);

	if (0 != smu_data->avfs.avfs_btc_param) {
		if (0 != smu7_send_msg_to_smc_with_parameter(smumgr, PPSMC_MSG_PerformBtc, smu_data->avfs.avfs_btc_param)) {
			pr_info("[AVFS][SmuPolaris10_PerformBtc] PerformBTC SMU msg failed");
			result = -1;
		}
	}
	if (smu_data->avfs.avfs_btc_param > 1) {
		/* Soft-Reset to reset the engine before loading uCode */
		/* halt */
		cgs_write_register(smumgr->device, mmCP_MEC_CNTL, 0x50000000);
		/* reset everything */
		cgs_write_register(smumgr->device, mmGRBM_SOFT_RESET, 0xffffffff);
		cgs_write_register(smumgr->device, mmGRBM_SOFT_RESET, 0);
	}
	return result;
}


static int polaris10_setup_graphics_level_structure(struct pp_smumgr *smumgr)
{
	uint32_t vr_config;
	uint32_t dpm_table_start;

	uint16_t u16_boot_mvdd;
	uint32_t graphics_level_address, vr_config_address, graphics_level_size;

	graphics_level_size = sizeof(avfs_graphics_level_polaris10);
	u16_boot_mvdd = PP_HOST_TO_SMC_US(1300 * VOLTAGE_SCALE);

	PP_ASSERT_WITH_CODE(0 == smu7_read_smc_sram_dword(smumgr,
				SMU7_FIRMWARE_HEADER_LOCATION + offsetof(SMU74_Firmware_Header, DpmTable),
				&dpm_table_start, 0x40000),
			"[AVFS][Polaris10_SetupGfxLvlStruct] SMU could not communicate starting address of DPM table",
			return -1);

	/*  Default value for VRConfig = VR_MERGED_WITH_VDDC + VR_STATIC_VOLTAGE(VDDCI) */
	vr_config = 0x01000500; /* Real value:0x50001 */

	vr_config_address = dpm_table_start + offsetof(SMU74_Discrete_DpmTable, VRConfig);

	PP_ASSERT_WITH_CODE(0 == smu7_copy_bytes_to_smc(smumgr, vr_config_address,
				(uint8_t *)&vr_config, sizeof(uint32_t), 0x40000),
			"[AVFS][Polaris10_SetupGfxLvlStruct] Problems copying VRConfig value over to SMC",
			return -1);

	graphics_level_address = dpm_table_start + offsetof(SMU74_Discrete_DpmTable, GraphicsLevel);

	PP_ASSERT_WITH_CODE(0 == smu7_copy_bytes_to_smc(smumgr, graphics_level_address,
				(uint8_t *)(&avfs_graphics_level_polaris10),
				graphics_level_size, 0x40000),
			"[AVFS][Polaris10_SetupGfxLvlStruct] Copying of SCLK DPM table failed!",
			return -1);

	graphics_level_address = dpm_table_start + offsetof(SMU74_Discrete_DpmTable, MemoryLevel);

	PP_ASSERT_WITH_CODE(0 == smu7_copy_bytes_to_smc(smumgr, graphics_level_address,
				(uint8_t *)(&avfs_memory_level_polaris10), sizeof(avfs_memory_level_polaris10), 0x40000),
				"[AVFS][Polaris10_SetupGfxLvlStruct] Copying of MCLK DPM table failed!",
			return -1);

	/* MVDD Boot value - neccessary for getting rid of the hang that occurs during Mclk DPM enablement */

	graphics_level_address = dpm_table_start + offsetof(SMU74_Discrete_DpmTable, BootMVdd);

	PP_ASSERT_WITH_CODE(0 == smu7_copy_bytes_to_smc(smumgr, graphics_level_address,
			(uint8_t *)(&u16_boot_mvdd), sizeof(u16_boot_mvdd), 0x40000),
			"[AVFS][Polaris10_SetupGfxLvlStruct] Copying of DPM table failed!",
			return -1);

	return 0;
}


static int
polaris10_avfs_event_mgr(struct pp_smumgr *smumgr, bool SMU_VFT_INTACT)
{
	struct smu7_smumgr *smu_data = (struct smu7_smumgr *)(smumgr->backend);

	switch (smu_data->avfs.avfs_btc_status) {
	case AVFS_BTC_COMPLETED_PREVIOUSLY:
		break;

	case AVFS_BTC_BOOT: /* Cold Boot State - Post SMU Start */

		smu_data->avfs.avfs_btc_status = AVFS_BTC_DPMTABLESETUP_FAILED;
		PP_ASSERT_WITH_CODE(0 == polaris10_setup_graphics_level_structure(smumgr),
			"[AVFS][Polaris10_AVFSEventMgr] Could not Copy Graphics Level table over to SMU",
			return -EINVAL);

		if (smu_data->avfs.avfs_btc_param > 1) {
			pr_info("[AVFS][Polaris10_AVFSEventMgr] AC BTC has not been successfully verified on Fiji. There may be in this setting.");
			smu_data->avfs.avfs_btc_status = AVFS_BTC_VIRUS_FAIL;
			PP_ASSERT_WITH_CODE(0 == polaris10_setup_pwr_virus(smumgr),
			"[AVFS][Polaris10_AVFSEventMgr] Could not setup Pwr Virus for AVFS ",
			return -EINVAL);
		}

		smu_data->avfs.avfs_btc_status = AVFS_BTC_FAILED;
		PP_ASSERT_WITH_CODE(0 == polaris10_perform_btc(smumgr),
					"[AVFS][Polaris10_AVFSEventMgr] Failure at SmuPolaris10_PerformBTC. AVFS Disabled",
				 return -EINVAL);
		smu_data->avfs.avfs_btc_status = AVFS_BTC_ENABLEAVFS;
		break;

	case AVFS_BTC_DISABLED:
	case AVFS_BTC_ENABLEAVFS:
	case AVFS_BTC_NOTSUPPORTED:
		break;

	default:
		pr_err("AVFS failed status is %x!\n", smu_data->avfs.avfs_btc_status);
		break;
	}

	return 0;
}

static int polaris10_start_smu_in_protection_mode(struct pp_smumgr *smumgr)
{
	int result = 0;

	/* Wait for smc boot up */
	/* SMUM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND, RCU_UC_EVENTS, boot_seq_done, 0) */

	/* Assert reset */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 1);

	result = smu7_upload_smu_firmware_image(smumgr);
	if (result != 0)
		return result;

	/* Clear status */
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC, ixSMU_STATUS, 0);

	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);

	/* De-assert reset */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 0);


	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND, RCU_UC_EVENTS, INTERRUPTS_ENABLED, 1);


	/* Call Test SMU message with 0x20000 offset to trigger SMU start */
	smu7_send_msg_to_smc_offset(smumgr);

	/* Wait done bit to be set */
	/* Check pass/failed indicator */

	SMUM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND, SMU_STATUS, SMU_DONE, 0);

	if (1 != SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
						SMU_STATUS, SMU_PASS))
		PP_ASSERT_WITH_CODE(false, "SMU Firmware start failed!", return -1);

	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC, ixFIRMWARE_FLAGS, 0);

	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 1);

	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	/* Wait for firmware to initialize */
	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND, FIRMWARE_FLAGS, INTERRUPTS_ENABLED, 1);

	return result;
}

static int polaris10_start_smu_in_non_protection_mode(struct pp_smumgr *smumgr)
{
	int result = 0;

	/* wait for smc boot up */
	SMUM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND, RCU_UC_EVENTS, boot_seq_done, 0);

	/* Clear firmware interrupt enable flag */
	/* SMUM_WRITE_VFPF_INDIRECT_FIELD(pSmuMgr, SMC_IND, SMC_SYSCON_MISC_CNTL, pre_fetcher_en, 1); */
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
				ixFIRMWARE_FLAGS, 0);

	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL,
					rst_reg, 1);

	result = smu7_upload_smu_firmware_image(smumgr);
	if (result != 0)
		return result;

	/* Set smc instruct start point at 0x0 */
	smu7_program_jump_on_start(smumgr);

	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);

	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
					SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	/* Wait for firmware to initialize */

	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND,
					FIRMWARE_FLAGS, INTERRUPTS_ENABLED, 1);

	return result;
}

static int polaris10_start_smu(struct pp_smumgr *smumgr)
{
	int result = 0;
	struct polaris10_smumgr *smu_data = (struct polaris10_smumgr *)(smumgr->backend);
	bool SMU_VFT_INTACT;

	/* Only start SMC if SMC RAM is not running */
	if (!smu7_is_smc_ram_running(smumgr)) {
		SMU_VFT_INTACT = false;
		smu_data->protected_mode = (uint8_t) (SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC, SMU_FIRMWARE, SMU_MODE));
		smu_data->smu7_data.security_hard_key = (uint8_t) (SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC, SMU_FIRMWARE, SMU_SEL));

		/* Check if SMU is running in protected mode */
		if (smu_data->protected_mode == 0) {
			result = polaris10_start_smu_in_non_protection_mode(smumgr);
		} else {
			result = polaris10_start_smu_in_protection_mode(smumgr);

			/* If failed, try with different security Key. */
			if (result != 0) {
				smu_data->smu7_data.security_hard_key ^= 1;
				cgs_rel_firmware(smumgr->device, CGS_UCODE_ID_SMU);
				result = polaris10_start_smu_in_protection_mode(smumgr);
			}
		}

		if (result != 0)
			PP_ASSERT_WITH_CODE(0, "Failed to load SMU ucode.", return result);

		polaris10_avfs_event_mgr(smumgr, true);
	} else
		SMU_VFT_INTACT = true; /*Driver went offline but SMU was still alive and contains the VFT table */

	polaris10_avfs_event_mgr(smumgr, SMU_VFT_INTACT);
	/* Setup SoftRegsStart here for register lookup in case DummyBackEnd is used and ProcessFirmwareHeader is not executed */
	smu7_read_smc_sram_dword(smumgr, SMU7_FIRMWARE_HEADER_LOCATION + offsetof(SMU74_Firmware_Header, SoftRegisters),
					&(smu_data->smu7_data.soft_regs_start), 0x40000);

	result = smu7_request_smu_load_fw(smumgr);

	return result;
}

static bool polaris10_is_hw_avfs_present(struct pp_smumgr *smumgr)
{
	uint32_t efuse;

	efuse = cgs_read_ind_register(smumgr->device, CGS_IND_REG__SMC, ixSMU_EFUSE_0 + (49*4));
	efuse &= 0x00000001;
	if (efuse)
		return true;

	return false;
}

static int polaris10_smu_init(struct pp_smumgr *smumgr)
{
	struct polaris10_smumgr *smu_data;
	int i;

	smu_data = kzalloc(sizeof(struct polaris10_smumgr), GFP_KERNEL);
	if (smu_data == NULL)
		return -ENOMEM;

	smumgr->backend = smu_data;

	if (smu7_init(smumgr))
		return -EINVAL;

	for (i = 0; i < SMU74_MAX_LEVELS_GRAPHICS; i++)
		smu_data->activity_target[i] = PPPOLARIS10_TARGETACTIVITY_DFLT;

	return 0;
}

const struct pp_smumgr_func polaris10_smu_funcs = {
	.smu_init = polaris10_smu_init,
	.smu_fini = smu7_smu_fini,
	.start_smu = polaris10_start_smu,
	.check_fw_load_finish = smu7_check_fw_load_finish,
	.request_smu_load_fw = smu7_reload_firmware,
	.request_smu_load_specific_fw = NULL,
	.send_msg_to_smc = smu7_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = smu7_send_msg_to_smc_with_parameter,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
	.update_smc_table = polaris10_update_smc_table,
	.get_offsetof = polaris10_get_offsetof,
	.process_firmware_header = polaris10_process_firmware_header,
	.init_smc_table = polaris10_init_smc_table,
	.update_sclk_threshold = polaris10_update_sclk_threshold,
	.thermal_avfs_enable = polaris10_thermal_avfs_enable,
	.thermal_setup_fan_table = polaris10_thermal_setup_fan_table,
	.populate_all_graphic_levels = polaris10_populate_all_graphic_levels,
	.populate_all_memory_levels = polaris10_populate_all_memory_levels,
	.get_mac_definition = polaris10_get_mac_definition,
	.is_dpm_running = polaris10_is_dpm_running,
	.populate_requested_graphic_levels = polaris10_populate_requested_graphic_levels,
	.is_hw_avfs_present = polaris10_is_hw_avfs_present,
};
