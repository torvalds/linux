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
#ifndef _SMUMGR_H_
#define _SMUMGR_H_
#include <linux/types.h>
#include "amd_powerplay.h"
#include "hwmgr.h"

enum SMU_TABLE {
	SMU_UVD_TABLE = 0,
	SMU_VCE_TABLE,
	SMU_SAMU_TABLE,
	SMU_BIF_TABLE,
};

enum SMU_TYPE {
	SMU_SoftRegisters = 0,
	SMU_Discrete_DpmTable,
};

enum SMU_MEMBER {
	HandshakeDisables = 0,
	VoltageChangeTimeout,
	AverageGraphicsActivity,
	PreVBlankGap,
	VBlankTimeout,
	UcodeLoadStatus,
	UvdBootLevel,
	VceBootLevel,
	SamuBootLevel,
	LowSclkInterruptThreshold,
	DRAM_LOG_ADDR_H,
	DRAM_LOG_ADDR_L,
	DRAM_LOG_PHY_ADDR_H,
	DRAM_LOG_PHY_ADDR_L,
	DRAM_LOG_BUFF_SIZE,
};


enum SMU_MAC_DEFINITION {
	SMU_MAX_LEVELS_GRAPHICS = 0,
	SMU_MAX_LEVELS_MEMORY,
	SMU_MAX_LEVELS_LINK,
	SMU_MAX_ENTRIES_SMIO,
	SMU_MAX_LEVELS_VDDC,
	SMU_MAX_LEVELS_VDDGFX,
	SMU_MAX_LEVELS_VDDCI,
	SMU_MAX_LEVELS_MVDD,
	SMU_UVD_MCLK_HANDSHAKE_DISABLE,
};

enum SMU9_TABLE_ID {
	PPTABLE = 0,
	WMTABLE,
	AVFSTABLE,
	TOOLSTABLE,
	AVFSFUSETABLE
};

enum SMU10_TABLE_ID {
	SMU10_WMTABLE = 0,
	SMU10_CLOCKTABLE,
};

extern int smum_get_argument(struct pp_hwmgr *hwmgr);

extern int smum_download_powerplay_table(struct pp_hwmgr *hwmgr, void **table);

extern int smum_upload_powerplay_table(struct pp_hwmgr *hwmgr);

extern int smum_send_msg_to_smc(struct pp_hwmgr *hwmgr, uint16_t msg);

extern int smum_send_msg_to_smc_with_parameter(struct pp_hwmgr *hwmgr,
					uint16_t msg, uint32_t parameter);

extern int smum_update_sclk_threshold(struct pp_hwmgr *hwmgr);

extern int smum_update_smc_table(struct pp_hwmgr *hwmgr, uint32_t type);
extern int smum_process_firmware_header(struct pp_hwmgr *hwmgr);
extern int smum_thermal_avfs_enable(struct pp_hwmgr *hwmgr);
extern int smum_thermal_setup_fan_table(struct pp_hwmgr *hwmgr);
extern int smum_init_smc_table(struct pp_hwmgr *hwmgr);
extern int smum_populate_all_graphic_levels(struct pp_hwmgr *hwmgr);
extern int smum_populate_all_memory_levels(struct pp_hwmgr *hwmgr);
extern int smum_initialize_mc_reg_table(struct pp_hwmgr *hwmgr);
extern uint32_t smum_get_offsetof(struct pp_hwmgr *hwmgr,
				uint32_t type, uint32_t member);
extern uint32_t smum_get_mac_definition(struct pp_hwmgr *hwmgr, uint32_t value);

extern bool smum_is_dpm_running(struct pp_hwmgr *hwmgr);

extern bool smum_is_hw_avfs_present(struct pp_hwmgr *hwmgr);

extern int smum_update_dpm_settings(struct pp_hwmgr *hwmgr, void *profile_setting);

extern int smum_smc_table_manager(struct pp_hwmgr *hwmgr, uint8_t *table, uint16_t table_id, bool rw);

#endif
