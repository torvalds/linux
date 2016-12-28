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

#include "smumgr.h"
#include "smu73.h"
#include "smu_ucode_xfer_vi.h"
#include "fiji_smumgr.h"
#include "fiji_ppsmc.h"
#include "smu73_discrete.h"
#include "ppatomctrl.h"
#include "smu/smu_7_1_3_d.h"
#include "smu/smu_7_1_3_sh_mask.h"
#include "gmc/gmc_8_1_d.h"
#include "gmc/gmc_8_1_sh_mask.h"
#include "oss/oss_3_0_d.h"
#include "gca/gfx_8_0_d.h"
#include "bif/bif_5_0_d.h"
#include "bif/bif_5_0_sh_mask.h"
#include "pp_debug.h"
#include "fiji_pwrvirus.h"
#include "fiji_smc.h"

#define AVFS_EN_MSB                                        1568
#define AVFS_EN_LSB                                        1568

#define FIJI_SMC_SIZE 0x20000

static const struct SMU73_Discrete_GraphicsLevel avfs_graphics_level[8] = {
		/*  Min        Sclk       pcie     DeepSleep Activity  CgSpll      CgSpll    spllSpread  SpllSpread   CcPwr  CcPwr  Sclk   Display     Enabled     Enabled                       Voltage    Power */
		/* Voltage,  Frequency,  DpmLevel,  DivId,    Level,  FuncCntl3,  FuncCntl4,  Spectrum,   Spectrum2,  DynRm, DynRm1  Did, Watermark, ForActivity, ForThrottle, UpHyst, DownHyst, DownHyst, Throttle */
		{ 0x3c0fd047, 0x30750000,   0x00,     0x03,   0x1e00, 0x00200410, 0x87020000, 0x21680000, 0x0c000000,   0,      0,   0x16,   0x00,       0x01,        0x01,      0x00,   0x00,      0x00,     0x00 },
		{ 0xa00fd047, 0x409c0000,   0x01,     0x04,   0x1e00, 0x00800510, 0x87020000, 0x21680000, 0x11000000,   0,      0,   0x16,   0x00,       0x01,        0x01,      0x00,   0x00,      0x00,     0x00 },
		{ 0x0410d047, 0x50c30000,   0x01,     0x00,   0x1e00, 0x00600410, 0x87020000, 0x21680000, 0x0d000000,   0,      0,   0x0e,   0x00,       0x01,        0x01,      0x00,   0x00,      0x00,     0x00 },
		{ 0x6810d047, 0x60ea0000,   0x01,     0x00,   0x1e00, 0x00800410, 0x87020000, 0x21680000, 0x0e000000,   0,      0,   0x0c,   0x00,       0x01,        0x01,      0x00,   0x00,      0x00,     0x00 },
		{ 0xcc10d047, 0xe8fd0000,   0x01,     0x00,   0x1e00, 0x00e00410, 0x87020000, 0x21680000, 0x0f000000,   0,      0,   0x0c,   0x00,       0x01,        0x01,      0x00,   0x00,      0x00,     0x00 },
		{ 0x3011d047, 0x70110100,   0x01,     0x00,   0x1e00, 0x00400510, 0x87020000, 0x21680000, 0x10000000,   0,      0,   0x0c,   0x00,       0x01,        0x01,      0x00,   0x00,      0x00,     0x00 },
		{ 0x9411d047, 0xf8240100,   0x01,     0x00,   0x1e00, 0x00a00510, 0x87020000, 0x21680000, 0x11000000,   0,      0,   0x0c,   0x00,       0x01,        0x01,      0x00,   0x00,      0x00,     0x00 },
		{ 0xf811d047, 0x80380100,   0x01,     0x00,   0x1e00, 0x00000610, 0x87020000, 0x21680000, 0x12000000,   0,      0,   0x0c,   0x01,       0x01,        0x01,      0x00,   0x00,      0x00,     0x00 }
};

static int fiji_start_smu_in_protection_mode(struct pp_smumgr *smumgr)
{
	int result = 0;

	/* Wait for smc boot up */
	/* SMUM_WAIT_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND,
		RCU_UC_EVENTS, boot_seq_done, 0); */

	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
			SMC_SYSCON_RESET_CNTL, rst_reg, 1);

	result = smu7_upload_smu_firmware_image(smumgr);
	if (result)
		return result;

	/* Clear status */
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
			ixSMU_STATUS, 0);

	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
			SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);

	/* De-assert reset */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
			SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	/* Wait for ROM firmware to initialize interrupt hendler */
	/*SMUM_WAIT_VFPF_INDIRECT_REGISTER(smumgr, SMC_IND,
			SMC_INTR_CNTL_MASK_0, 0x10040, 0xFFFFFFFF); */

	/* Set SMU Auto Start */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
			SMU_INPUT_DATA, AUTO_START, 1);

	/* Clear firmware interrupt enable flag */
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
			ixFIRMWARE_FLAGS, 0);

	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND, RCU_UC_EVENTS,
			INTERRUPTS_ENABLED, 1);

	cgs_write_register(smumgr->device, mmSMC_MSG_ARG_0, 0x20000);
	cgs_write_register(smumgr->device, mmSMC_MESSAGE_0, PPSMC_MSG_Test);
	SMUM_WAIT_FIELD_UNEQUAL(smumgr, SMC_RESP_0, SMC_RESP, 0);

	/* Wait for done bit to be set */
	SMUM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND,
			SMU_STATUS, SMU_DONE, 0);

	/* Check pass/failed indicator */
	if (SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
			SMU_STATUS, SMU_PASS) != 1) {
		PP_ASSERT_WITH_CODE(false,
				"SMU Firmware start failed!", return -1);
	}

	/* Wait for firmware to initialize */
	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND,
			FIRMWARE_FLAGS, INTERRUPTS_ENABLED, 1);

	return result;
}

static int fiji_start_smu_in_non_protection_mode(struct pp_smumgr *smumgr)
{
	int result = 0;

	/* wait for smc boot up */
	SMUM_WAIT_VFPF_INDIRECT_FIELD_UNEQUAL(smumgr, SMC_IND,
			RCU_UC_EVENTS, boot_seq_done, 0);

	/* Clear firmware interrupt enable flag */
	cgs_write_ind_register(smumgr->device, CGS_IND_REG__SMC,
			ixFIRMWARE_FLAGS, 0);

	/* Assert reset */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
			SMC_SYSCON_RESET_CNTL, rst_reg, 1);

	result = smu7_upload_smu_firmware_image(smumgr);
	if (result)
		return result;

	/* Set smc instruct start point at 0x0 */
	smu7_program_jump_on_start(smumgr);

	/* Enable clock */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
			SMC_SYSCON_CLOCK_CNTL_0, ck_disable, 0);

	/* De-assert reset */
	SMUM_WRITE_VFPF_INDIRECT_FIELD(smumgr->device, CGS_IND_REG__SMC,
			SMC_SYSCON_RESET_CNTL, rst_reg, 0);

	/* Wait for firmware to initialize */
	SMUM_WAIT_VFPF_INDIRECT_FIELD(smumgr, SMC_IND,
			FIRMWARE_FLAGS, INTERRUPTS_ENABLED, 1);

	return result;
}

static int fiji_setup_pwr_virus(struct pp_smumgr *smumgr)
{
	int i, result = -1;
	uint32_t reg, data;
	const PWR_Command_Table *virus = PwrVirusTable;
	struct fiji_smumgr *priv = (struct fiji_smumgr *)(smumgr->backend);

	priv->avfs.AvfsBtcStatus = AVFS_LOAD_VIRUS;
	for (i = 0; (i < PWR_VIRUS_TABLE_SIZE); i++) {
		switch (virus->command) {
		case PwrCmdWrite:
			reg  = virus->reg;
			data = virus->data;
			cgs_write_register(smumgr->device, reg, data);
			break;
		case PwrCmdEnd:
			priv->avfs.AvfsBtcStatus = AVFS_BTC_VIRUS_LOADED;
			result = 0;
			break;
		default:
			printk(KERN_ERR "Table Exit with Invalid Command!");
			priv->avfs.AvfsBtcStatus = AVFS_BTC_VIRUS_FAIL;
			result = -1;
			break;
		}
		virus++;
	}
	return result;
}

static int fiji_start_avfs_btc(struct pp_smumgr *smumgr)
{
	int result = 0;
	struct fiji_smumgr *priv = (struct fiji_smumgr *)(smumgr->backend);

	priv->avfs.AvfsBtcStatus = AVFS_BTC_STARTED;
	if (priv->avfs.AvfsBtcParam) {
		if (!smum_send_msg_to_smc_with_parameter(smumgr,
				PPSMC_MSG_PerformBtc, priv->avfs.AvfsBtcParam)) {
			if (!smum_send_msg_to_smc(smumgr, PPSMC_MSG_EnableAvfs)) {
				priv->avfs.AvfsBtcStatus = AVFS_BTC_COMPLETED_UNSAVED;
				result = 0;
			} else {
				printk(KERN_ERR "[AVFS][fiji_start_avfs_btc] Attempt"
						" to Enable AVFS Failed!");
				smum_send_msg_to_smc(smumgr, PPSMC_MSG_DisableAvfs);
				result = -1;
			}
		} else {
			printk(KERN_ERR "[AVFS][fiji_start_avfs_btc] "
					"PerformBTC SMU msg failed");
			result = -1;
		}
	}
	/* Soft-Reset to reset the engine before loading uCode */
	 /* halt */
	cgs_write_register(smumgr->device, mmCP_MEC_CNTL, 0x50000000);
	/* reset everything */
	cgs_write_register(smumgr->device, mmGRBM_SOFT_RESET, 0xffffffff);
	/* clear reset */
	cgs_write_register(smumgr->device, mmGRBM_SOFT_RESET, 0);

	return result;
}

static int fiji_setup_pm_fuse_for_avfs(struct pp_smumgr *smumgr)
{
	int result = 0;
	uint32_t table_start;
	uint32_t charz_freq_addr, inversion_voltage_addr, charz_freq;
	uint16_t inversion_voltage;

	charz_freq = 0x30750000; /* In 10KHz units 0x00007530 Actual value */
	inversion_voltage = 0x1A04; /* mV Q14.2 0x41A Actual value */

	PP_ASSERT_WITH_CODE(0 == smu7_read_smc_sram_dword(smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION + offsetof(SMU73_Firmware_Header,
					PmFuseTable), &table_start, 0x40000),
			"[AVFS][Fiji_SetupGfxLvlStruct] SMU could not communicate "
			"starting address of PmFuse structure",
			return -1;);

	charz_freq_addr = table_start +
			offsetof(struct SMU73_Discrete_PmFuses, PsmCharzFreq);
	inversion_voltage_addr = table_start +
			offsetof(struct SMU73_Discrete_PmFuses, InversionVoltage);

	result = smu7_copy_bytes_to_smc(smumgr, charz_freq_addr,
			(uint8_t *)(&charz_freq), sizeof(charz_freq), 0x40000);
	PP_ASSERT_WITH_CODE(0 == result,
			"[AVFS][fiji_setup_pm_fuse_for_avfs] charz_freq could not "
			"be populated.", return -1;);

	result = smu7_copy_bytes_to_smc(smumgr, inversion_voltage_addr,
			(uint8_t *)(&inversion_voltage), sizeof(inversion_voltage), 0x40000);
	PP_ASSERT_WITH_CODE(0 == result, "[AVFS][fiji_setup_pm_fuse_for_avfs] "
			"charz_freq could not be populated.", return -1;);

	return result;
}

static int fiji_setup_graphics_level_structure(struct pp_smumgr *smumgr)
{
	int32_t vr_config;
	uint32_t table_start;
	uint32_t level_addr, vr_config_addr;
	uint32_t level_size = sizeof(avfs_graphics_level);

	PP_ASSERT_WITH_CODE(0 == smu7_read_smc_sram_dword(smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU73_Firmware_Header, DpmTable),
			&table_start, 0x40000),
			"[AVFS][Fiji_SetupGfxLvlStruct] SMU could not "
			"communicate starting address of DPM table",
			return -1;);

	/* Default value for vr_config =
	 * VR_MERGED_WITH_VDDC + VR_STATIC_VOLTAGE(VDDCI) */
	vr_config = 0x01000500;   /* Real value:0x50001 */

	vr_config_addr = table_start +
			offsetof(SMU73_Discrete_DpmTable, VRConfig);

	PP_ASSERT_WITH_CODE(0 == smu7_copy_bytes_to_smc(smumgr, vr_config_addr,
			(uint8_t *)&vr_config, sizeof(int32_t), 0x40000),
			"[AVFS][Fiji_SetupGfxLvlStruct] Problems copying "
			"vr_config value over to SMC",
			return -1;);

	level_addr = table_start + offsetof(SMU73_Discrete_DpmTable, GraphicsLevel);

	PP_ASSERT_WITH_CODE(0 == smu7_copy_bytes_to_smc(smumgr, level_addr,
			(uint8_t *)(&avfs_graphics_level), level_size, 0x40000),
			"[AVFS][Fiji_SetupGfxLvlStruct] Copying of DPM table failed!",
			return -1;);

	return 0;
}

/* Work in Progress */
static int fiji_restore_vft_table(struct pp_smumgr *smumgr)
{
	struct fiji_smumgr *priv = (struct fiji_smumgr *)(smumgr->backend);

	if (AVFS_BTC_COMPLETED_SAVED == priv->avfs.AvfsBtcStatus) {
		priv->avfs.AvfsBtcStatus = AVFS_BTC_COMPLETED_RESTORED;
		return 0;
	} else
		return -EINVAL;
}

/* Work in Progress */
static int fiji_save_vft_table(struct pp_smumgr *smumgr)
{
	struct fiji_smumgr *priv = (struct fiji_smumgr *)(smumgr->backend);

	if (AVFS_BTC_COMPLETED_SAVED == priv->avfs.AvfsBtcStatus) {
		priv->avfs.AvfsBtcStatus = AVFS_BTC_COMPLETED_RESTORED;
		return 0;
	} else
		return -EINVAL;
}

static int fiji_avfs_event_mgr(struct pp_smumgr *smumgr, bool smu_started)
{
	struct fiji_smumgr *priv = (struct fiji_smumgr *)(smumgr->backend);

	switch (priv->avfs.AvfsBtcStatus) {
	case AVFS_BTC_COMPLETED_SAVED: /*S3 State - Pre SMU Start */
		priv->avfs.AvfsBtcStatus = AVFS_BTC_RESTOREVFT_FAILED;
		PP_ASSERT_WITH_CODE(0 == fiji_restore_vft_table(smumgr),
				"[AVFS][fiji_avfs_event_mgr] Could not Copy Graphics "
				"Level table over to SMU",
				return -1;);
		priv->avfs.AvfsBtcStatus = AVFS_BTC_COMPLETED_RESTORED;
		break;
	case AVFS_BTC_COMPLETED_RESTORED: /*S3 State - Post SMU Start*/
		priv->avfs.AvfsBtcStatus = AVFS_BTC_SMUMSG_ERROR;
		PP_ASSERT_WITH_CODE(0 == smum_send_msg_to_smc(smumgr,
				0x666),
				"[AVFS][fiji_avfs_event_mgr] SMU did not respond "
				"correctly to VftTableIsValid Msg",
				return -1;);
		priv->avfs.AvfsBtcStatus = AVFS_BTC_SMUMSG_ERROR;
		PP_ASSERT_WITH_CODE(0 == smum_send_msg_to_smc(smumgr,
				PPSMC_MSG_EnableAvfs),
				"[AVFS][fiji_avfs_event_mgr] SMU did not respond "
				"correctly to EnableAvfs Message Msg",
				return -1;);
		priv->avfs.AvfsBtcStatus = AVFS_BTC_COMPLETED_SAVED;
		break;
	case AVFS_BTC_BOOT: /*Cold Boot State - Post SMU Start*/
		if (!smu_started)
			break;
		priv->avfs.AvfsBtcStatus = AVFS_BTC_FAILED;
		PP_ASSERT_WITH_CODE(0 == fiji_setup_pm_fuse_for_avfs(smumgr),
				"[AVFS][fiji_avfs_event_mgr] Failure at "
				"fiji_setup_pm_fuse_for_avfs",
				return -1;);
		priv->avfs.AvfsBtcStatus = AVFS_BTC_DPMTABLESETUP_FAILED;
		PP_ASSERT_WITH_CODE(0 == fiji_setup_graphics_level_structure(smumgr),
				"[AVFS][fiji_avfs_event_mgr] Could not Copy Graphics Level"
				" table over to SMU",
				return -1;);
		priv->avfs.AvfsBtcStatus = AVFS_BTC_VIRUS_FAIL;
		PP_ASSERT_WITH_CODE(0 == fiji_setup_pwr_virus(smumgr),
				"[AVFS][fiji_avfs_event_mgr] Could not setup "
				"Pwr Virus for AVFS ",
				return -1;);
		priv->avfs.AvfsBtcStatus = AVFS_BTC_FAILED;
		PP_ASSERT_WITH_CODE(0 == fiji_start_avfs_btc(smumgr),
				"[AVFS][fiji_avfs_event_mgr] Failure at "
				"fiji_start_avfs_btc. AVFS Disabled",
				return -1;);
		priv->avfs.AvfsBtcStatus = AVFS_BTC_SAVEVFT_FAILED;
		PP_ASSERT_WITH_CODE(0 == fiji_save_vft_table(smumgr),
				"[AVFS][fiji_avfs_event_mgr] Could not save VFT Table",
				return -1;);
		priv->avfs.AvfsBtcStatus = AVFS_BTC_COMPLETED_SAVED;
		break;
	case AVFS_BTC_DISABLED: /* Do nothing */
		break;
	case AVFS_BTC_NOTSUPPORTED: /* Do nothing */
		break;
	default:
		printk(KERN_ERR "[AVFS] Something is broken. See log!");
		break;
	}
	return 0;
}

static int fiji_start_smu(struct pp_smumgr *smumgr)
{
	int result = 0;
	struct fiji_smumgr *priv = (struct fiji_smumgr *)(smumgr->backend);

	/* Only start SMC if SMC RAM is not running */
	if (!(smu7_is_smc_ram_running(smumgr)
		|| cgs_is_virtualization_enabled(smumgr->device))) {
		fiji_avfs_event_mgr(smumgr, false);

		/* Check if SMU is running in protected mode */
		if (0 == SMUM_READ_VFPF_INDIRECT_FIELD(smumgr->device,
				CGS_IND_REG__SMC,
				SMU_FIRMWARE, SMU_MODE)) {
			result = fiji_start_smu_in_non_protection_mode(smumgr);
			if (result)
				return result;
		} else {
			result = fiji_start_smu_in_protection_mode(smumgr);
			if (result)
				return result;
		}
		fiji_avfs_event_mgr(smumgr, true);
	}

	/* To initialize all clock gating before RLC loaded and running.*/
	cgs_set_clockgating_state(smumgr->device,
			AMD_IP_BLOCK_TYPE_GFX, AMD_CG_STATE_GATE);
	cgs_set_clockgating_state(smumgr->device,
			AMD_IP_BLOCK_TYPE_GMC, AMD_CG_STATE_GATE);
	cgs_set_clockgating_state(smumgr->device,
			AMD_IP_BLOCK_TYPE_SDMA, AMD_CG_STATE_GATE);
	cgs_set_clockgating_state(smumgr->device,
			AMD_IP_BLOCK_TYPE_COMMON, AMD_CG_STATE_GATE);

	/* Setup SoftRegsStart here for register lookup in case
	 * DummyBackEnd is used and ProcessFirmwareHeader is not executed
	 */
	smu7_read_smc_sram_dword(smumgr,
			SMU7_FIRMWARE_HEADER_LOCATION +
			offsetof(SMU73_Firmware_Header, SoftRegisters),
			&(priv->smu7_data.soft_regs_start), 0x40000);

	result = smu7_request_smu_load_fw(smumgr);

	return result;
}

static bool fiji_is_hw_avfs_present(struct pp_smumgr *smumgr)
{

	uint32_t efuse = 0;
	uint32_t mask = (1 << ((AVFS_EN_MSB - AVFS_EN_LSB) + 1)) - 1;

	if (cgs_is_virtualization_enabled(smumgr->device))
		return 0;

	if (!atomctrl_read_efuse(smumgr->device, AVFS_EN_LSB, AVFS_EN_MSB,
			mask, &efuse)) {
		if (efuse)
			return true;
	}
	return false;
}

/**
* Write a 32bit value to the SMC SRAM space.
* ALL PARAMETERS ARE IN HOST BYTE ORDER.
* @param    smumgr  the address of the powerplay hardware manager.
* @param    smc_addr the address in the SMC RAM to access.
* @param    value to write to the SMC SRAM.
*/
static int fiji_smu_init(struct pp_smumgr *smumgr)
{
	struct fiji_smumgr *priv = (struct fiji_smumgr *)(smumgr->backend);
	int i;

	if (smu7_init(smumgr))
		return -EINVAL;

	priv->avfs.AvfsBtcStatus = AVFS_BTC_BOOT;
	if (fiji_is_hw_avfs_present(smumgr))
		/* AVFS Parameter
		 * 0 - BTC DC disabled, BTC AC disabled
		 * 1 - BTC DC enabled,  BTC AC disabled
		 * 2 - BTC DC disabled, BTC AC enabled
		 * 3 - BTC DC enabled,  BTC AC enabled
		 * Default is 0 - BTC DC disabled, BTC AC disabled
		 */
		priv->avfs.AvfsBtcParam = 0;
	else
		priv->avfs.AvfsBtcStatus = AVFS_BTC_NOTSUPPORTED;

	for (i = 0; i < SMU73_MAX_LEVELS_GRAPHICS; i++)
		priv->activity_target[i] = 30;

	return 0;
}


static const struct pp_smumgr_func fiji_smu_funcs = {
	.smu_init = &fiji_smu_init,
	.smu_fini = &smu7_smu_fini,
	.start_smu = &fiji_start_smu,
	.check_fw_load_finish = &smu7_check_fw_load_finish,
	.request_smu_load_fw = &smu7_reload_firmware,
	.request_smu_load_specific_fw = NULL,
	.send_msg_to_smc = &smu7_send_msg_to_smc,
	.send_msg_to_smc_with_parameter = &smu7_send_msg_to_smc_with_parameter,
	.download_pptable_settings = NULL,
	.upload_pptable_settings = NULL,
	.update_smc_table = fiji_update_smc_table,
	.get_offsetof = fiji_get_offsetof,
	.process_firmware_header = fiji_process_firmware_header,
	.init_smc_table = fiji_init_smc_table,
	.update_sclk_threshold = fiji_update_sclk_threshold,
	.thermal_setup_fan_table = fiji_thermal_setup_fan_table,
	.populate_all_graphic_levels = fiji_populate_all_graphic_levels,
	.populate_all_memory_levels = fiji_populate_all_memory_levels,
	.get_mac_definition = fiji_get_mac_definition,
	.initialize_mc_reg_table = fiji_initialize_mc_reg_table,
	.is_dpm_running = fiji_is_dpm_running,
};

int fiji_smum_init(struct pp_smumgr *smumgr)
{
	struct fiji_smumgr *fiji_smu = NULL;

	fiji_smu = kzalloc(sizeof(struct fiji_smumgr), GFP_KERNEL);

	if (fiji_smu == NULL)
		return -ENOMEM;

	smumgr->backend = fiji_smu;
	smumgr->smumgr_funcs = &fiji_smu_funcs;

	return 0;
}
