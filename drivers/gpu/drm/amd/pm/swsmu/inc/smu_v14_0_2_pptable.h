/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#ifndef SMU_14_0_2_PPTABLE_H
#define SMU_14_0_2_PPTABLE_H


#pragma pack(push, 1)

#define SMU_14_0_2_TABLE_FORMAT_REVISION 3

// POWERPLAYTABLE::ulPlatformCaps
#define SMU_14_0_2_PP_PLATFORM_CAP_POWERPLAY        0x1     // This cap indicates whether CCC need to show Powerplay page.
#define SMU_14_0_2_PP_PLATFORM_CAP_SBIOSPOWERSOURCE 0x2     // This cap indicates whether power source notificaiton is done by SBIOS instead of OS.
#define SMU_14_0_2_PP_PLATFORM_CAP_HARDWAREDC       0x4     // This cap indicates whether DC mode notificaiton is done by GPIO pin directly.
#define SMU_14_0_2_PP_PLATFORM_CAP_BACO             0x8     // This cap indicates whether board supports the BACO circuitry.
#define SMU_14_0_2_PP_PLATFORM_CAP_MACO             0x10    // This cap indicates whether board supports the MACO circuitry.
#define SMU_14_0_2_PP_PLATFORM_CAP_SHADOWPSTATE     0x20    // This cap indicates whether board supports the Shadow Pstate.
#define SMU_14_0_2_PP_PLATFORM_CAP_LEDSUPPORTED     0x40    // This cap indicates whether board supports the LED.
#define SMU_14_0_2_PP_PLATFORM_CAP_MOBILEOVERDRIVE  0x80    // This cap indicates whether board supports the Mobile Overdrive.

// SMU_14_0_2_PP_THERMALCONTROLLER - Thermal Controller Type
#define SMU_14_0_2_PP_THERMALCONTROLLER_NONE        0

#define SMU_14_0_2_PP_OVERDRIVE_VERSION             0x1     // TODO: FIX OverDrive Version TBD
#define SMU_14_0_2_PP_POWERSAVINGCLOCK_VERSION      0x01    // Power Saving Clock Table Version 1.00

enum SMU_14_0_2_OD_SW_FEATURE_CAP
{
    SMU_14_0_2_ODCAP_AUTO_FAN_ACOUSTIC_LIMIT        = 0,
    SMU_14_0_2_ODCAP_POWER_MODE                     = 1,
    SMU_14_0_2_ODCAP_AUTO_UV_ENGINE                 = 2,
    SMU_14_0_2_ODCAP_AUTO_OC_ENGINE                 = 3,
    SMU_14_0_2_ODCAP_AUTO_OC_MEMORY                 = 4,
    SMU_14_0_2_ODCAP_MEMORY_TIMING_TUNE             = 5,
    SMU_14_0_2_ODCAP_MANUAL_AC_TIMING               = 6,
    SMU_14_0_2_ODCAP_AUTO_VF_CURVE_OPTIMIZER        = 7,
    SMU_14_0_2_ODCAP_AUTO_SOC_UV                    = 8,
    SMU_14_0_2_ODCAP_COUNT                          = 9,
};

enum SMU_14_0_2_OD_SW_FEATURE_ID
{
    SMU_14_0_2_ODFEATURE_AUTO_FAN_ACOUSTIC_LIMIT      = 1 << SMU_14_0_2_ODCAP_AUTO_FAN_ACOUSTIC_LIMIT,      // Auto Fan Acoustic RPM
    SMU_14_0_2_ODFEATURE_POWER_MODE                   = 1 << SMU_14_0_2_ODCAP_POWER_MODE,                   // Optimized GPU Power Mode
    SMU_14_0_2_ODFEATURE_AUTO_UV_ENGINE               = 1 << SMU_14_0_2_ODCAP_AUTO_UV_ENGINE,               // Auto Under Volt GFXCLK
    SMU_14_0_2_ODFEATURE_AUTO_OC_ENGINE               = 1 << SMU_14_0_2_ODCAP_AUTO_OC_ENGINE,               // Auto Over Clock GFXCLK
    SMU_14_0_2_ODFEATURE_AUTO_OC_MEMORY               = 1 << SMU_14_0_2_ODCAP_AUTO_OC_MEMORY,               // Auto Over Clock MCLK
    SMU_14_0_2_ODFEATURE_MEMORY_TIMING_TUNE           = 1 << SMU_14_0_2_ODCAP_MEMORY_TIMING_TUNE,           // Auto AC Timing Tuning
    SMU_14_0_2_ODFEATURE_MANUAL_AC_TIMING             = 1 << SMU_14_0_2_ODCAP_MANUAL_AC_TIMING,             // Manual fine grain AC Timing tuning
    SMU_14_0_2_ODFEATURE_AUTO_VF_CURVE_OPTIMIZER      = 1 << SMU_14_0_2_ODCAP_AUTO_VF_CURVE_OPTIMIZER,      // Fine grain auto VF curve tuning
    SMU_14_0_2_ODFEATURE_AUTO_SOC_UV                  = 1 << SMU_14_0_2_ODCAP_AUTO_SOC_UV,                  // Auto Unver Volt VDDSOC
};

#define SMU_14_0_2_MAX_ODFEATURE 32 // Maximum Number of OD Features

enum SMU_14_0_2_OD_SW_FEATURE_SETTING_ID
{
    SMU_14_0_2_ODSETTING_AUTO_FAN_ACOUSTIC_LIMIT    = 0,
    SMU_14_0_2_ODSETTING_POWER_MODE                 = 1,
    SMU_14_0_2_ODSETTING_AUTOUVENGINE               = 2,
    SMU_14_0_2_ODSETTING_AUTOOCENGINE               = 3,
    SMU_14_0_2_ODSETTING_AUTOOCMEMORY               = 4,
    SMU_14_0_2_ODSETTING_ACTIMING                   = 5,
    SMU_14_0_2_ODSETTING_MANUAL_AC_TIMING           = 6,
    SMU_14_0_2_ODSETTING_AUTO_VF_CURVE_OPTIMIZER    = 7,
    SMU_14_0_2_ODSETTING_AUTO_SOC_UV                = 8,
    SMU_14_0_2_ODSETTING_COUNT                      = 9,
};
#define SMU_14_0_2_MAX_ODSETTING 64 // Maximum Number of ODSettings

enum SMU_14_0_2_PWRMODE_SETTING
{
    SMU_14_0_2_PMSETTING_POWER_LIMIT_QUIET = 0,
    SMU_14_0_2_PMSETTING_POWER_LIMIT_BALANCE,
    SMU_14_0_2_PMSETTING_POWER_LIMIT_TURBO,
    SMU_14_0_2_PMSETTING_POWER_LIMIT_RAGE,
    SMU_14_0_2_PMSETTING_ACOUSTIC_TEMP_QUIET,
    SMU_14_0_2_PMSETTING_ACOUSTIC_TEMP_BALANCE,
    SMU_14_0_2_PMSETTING_ACOUSTIC_TEMP_TURBO,
    SMU_14_0_2_PMSETTING_ACOUSTIC_TEMP_RAGE,
    SMU_14_0_2_PMSETTING_ACOUSTIC_TARGET_RPM_QUIET,
    SMU_14_0_2_PMSETTING_ACOUSTIC_TARGET_RPM_BALANCE,
    SMU_14_0_2_PMSETTING_ACOUSTIC_TARGET_RPM_TURBO,
    SMU_14_0_2_PMSETTING_ACOUSTIC_TARGET_RPM_RAGE,
    SMU_14_0_2_PMSETTING_ACOUSTIC_LIMIT_RPM_QUIET,
    SMU_14_0_2_PMSETTING_ACOUSTIC_LIMIT_RPM_BALANCE,
    SMU_14_0_2_PMSETTING_ACOUSTIC_LIMIT_RPM_TURBO,
    SMU_14_0_2_PMSETTING_ACOUSTIC_LIMIT_RPM_RAGE,
};
#define SMU_14_0_2_MAX_PMSETTING 32 // Maximum Number of PowerMode Settings

enum SMU_14_0_2_overdrive_table_id
{
    SMU_14_0_2_OVERDRIVE_TABLE_BASIC    = 0,
    SMU_14_0_2_OVERDRIVE_TABLE_ADVANCED = 1,
    SMU_14_0_2_OVERDRIVE_TABLE_COUNT    = 2,
};

struct smu_14_0_2_overdrive_table
{
    uint8_t revision;                                                           // Revision = SMU_14_0_2_PP_OVERDRIVE_VERSION
    uint8_t reserve[3];                                                         // Zero filled field reserved for future use
    uint8_t cap[SMU_14_0_2_OVERDRIVE_TABLE_COUNT][SMU_14_0_2_MAX_ODFEATURE];    // OD feature support flags
    int32_t max[SMU_14_0_2_OVERDRIVE_TABLE_COUNT][SMU_14_0_2_MAX_ODSETTING];    // maximum settings
    int32_t min[SMU_14_0_2_OVERDRIVE_TABLE_COUNT][SMU_14_0_2_MAX_ODSETTING];    // minimum settings
    int16_t pm_setting[SMU_14_0_2_MAX_PMSETTING];                               // Optimized power mode feature settings
};

struct smu_14_0_2_powerplay_table
{
    struct atom_common_table_header header;                 // header.format_revision = 3 (HAS TO MATCH SMU_14_0_2_TABLE_FORMAT_REVISION), header.content_revision = ? structuresize is calculated by PPGen.
    uint8_t table_revision;                                 // PPGen use only: table_revision = 3
    uint8_t padding;                                        // Padding 1 byte to align table_size offset to 6 bytes (pmfw_start_offset, for PMFW to know the starting offset of PPTable_t).
    uint16_t pmfw_pptable_start_offset;                     // The start offset of the pmfw portion. i.e. start of PPTable_t (start of SkuTable_t)
    uint16_t pmfw_pptable_size;                             // The total size of pmfw_pptable, i.e PPTable_t.
    uint16_t pmfw_pfe_table_start_offset;                   // The start offset of the PFE_Settings_t within pmfw_pptable.
    uint16_t pmfw_pfe_table_size;                           // The size of PFE_Settings_t.
    uint16_t pmfw_board_table_start_offset;                 // The start offset of the BoardTable_t within pmfw_pptable.
    uint16_t pmfw_board_table_size;                         // The size of BoardTable_t.
    uint16_t pmfw_custom_sku_table_start_offset;            // The start offset of the CustomSkuTable_t within pmfw_pptable.
    uint16_t pmfw_custom_sku_table_size;                    // The size of the CustomSkuTable_t.
    uint32_t golden_pp_id;                                  // PPGen use only: PP Table ID on the Golden Data Base
    uint32_t golden_revision;                               // PPGen use only: PP Table Revision on the Golden Data Base
    uint16_t format_id;                                     // PPGen use only: PPTable for different ASICs.
    uint32_t platform_caps;                                 // POWERPLAYTABLE::ulPlatformCaps

    uint8_t thermal_controller_type;                        // one of smu_14_0_2_PP_THERMALCONTROLLER

    uint16_t small_power_limit1;
    uint16_t small_power_limit2;
    uint16_t boost_power_limit;                             // For Gemini Board, when the slave adapter is in BACO mode, the master adapter will use this boost power limit instead of the default power limit to boost the power limit.
    uint16_t software_shutdown_temp;

    uint8_t reserve[143];                                   // Zero filled field reserved for future use

    struct smu_14_0_2_overdrive_table overdrive_table;

    PPTable_t smc_pptable;                          // PPTable_t in driver_if.h -- as requested by PMFW, this offset should start at a 32-byte boundary, and the table_size above should remain at offset=6 bytes
};

#pragma pack(pop)

#endif
