/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
 */
#ifndef SMU_13_0_0_PPTABLE_H
#define SMU_13_0_0_PPTABLE_H

#pragma pack(push, 1)

#define SMU_13_0_0_TABLE_FORMAT_REVISION 15

//// POWERPLAYTABLE::ulPlatformCaps
#define SMU_13_0_0_PP_PLATFORM_CAP_POWERPLAY 0x1        // This cap indicates whether CCC need to show Powerplay page.
#define SMU_13_0_0_PP_PLATFORM_CAP_SBIOSPOWERSOURCE 0x2 // This cap indicates whether power source notificaiton is done by SBIOS instead of OS.
#define SMU_13_0_0_PP_PLATFORM_CAP_HARDWAREDC 0x4       // This cap indicates whether DC mode notificaiton is done by GPIO pin directly.
#define SMU_13_0_0_PP_PLATFORM_CAP_BACO 0x8             // This cap indicates whether board supports the BACO circuitry.
#define SMU_13_0_0_PP_PLATFORM_CAP_MACO 0x10            // This cap indicates whether board supports the MACO circuitry.
#define SMU_13_0_0_PP_PLATFORM_CAP_SHADOWPSTATE 0x20    // This cap indicates whether board supports the Shadow Pstate.

// SMU_13_0_0_PP_THERMALCONTROLLER - Thermal Controller Type
#define SMU_13_0_0_PP_THERMALCONTROLLER_NONE 0
#define SMU_13_0_0_PP_THERMALCONTROLLER_NAVI21 28

#define SMU_13_0_0_PP_OVERDRIVE_VERSION 0x81        // OverDrive 8 Table Version 0.2
#define SMU_13_0_0_PP_POWERSAVINGCLOCK_VERSION 0x01 // Power Saving Clock Table Version 1.00

enum SMU_13_0_0_ODFEATURE_CAP
{
    SMU_13_0_0_ODCAP_GFXCLK_LIMITS = 0,
    SMU_13_0_0_ODCAP_GFXCLK_CURVE,
    SMU_13_0_0_ODCAP_UCLK_LIMITS,
    SMU_13_0_0_ODCAP_POWER_LIMIT,
    SMU_13_0_0_ODCAP_FAN_ACOUSTIC_LIMIT,
    SMU_13_0_0_ODCAP_FAN_SPEED_MIN,
    SMU_13_0_0_ODCAP_TEMPERATURE_FAN,
    SMU_13_0_0_ODCAP_TEMPERATURE_SYSTEM,
    SMU_13_0_0_ODCAP_MEMORY_TIMING_TUNE,
    SMU_13_0_0_ODCAP_FAN_ZERO_RPM_CONTROL,
    SMU_13_0_0_ODCAP_AUTO_UV_ENGINE,
    SMU_13_0_0_ODCAP_AUTO_OC_ENGINE,
    SMU_13_0_0_ODCAP_AUTO_OC_MEMORY,
    SMU_13_0_0_ODCAP_FAN_CURVE,
    SMU_13_0_0_ODCAP_AUTO_FAN_ACOUSTIC_LIMIT,
    SMU_13_0_0_ODCAP_POWER_MODE,
    SMU_13_0_0_ODCAP_COUNT,
};

enum SMU_13_0_0_ODFEATURE_ID
{
    SMU_13_0_0_ODFEATURE_GFXCLK_LIMITS           = 1 << SMU_13_0_0_ODCAP_GFXCLK_LIMITS,           //GFXCLK Limit feature
    SMU_13_0_0_ODFEATURE_GFXCLK_CURVE            = 1 << SMU_13_0_0_ODCAP_GFXCLK_CURVE,            //GFXCLK Curve feature
    SMU_13_0_0_ODFEATURE_UCLK_LIMITS             = 1 << SMU_13_0_0_ODCAP_UCLK_LIMITS,             //UCLK Limit feature
    SMU_13_0_0_ODFEATURE_POWER_LIMIT             = 1 << SMU_13_0_0_ODCAP_POWER_LIMIT,             //Power Limit feature
    SMU_13_0_0_ODFEATURE_FAN_ACOUSTIC_LIMIT      = 1 << SMU_13_0_0_ODCAP_FAN_ACOUSTIC_LIMIT,      //Fan Acoustic RPM feature
    SMU_13_0_0_ODFEATURE_FAN_SPEED_MIN           = 1 << SMU_13_0_0_ODCAP_FAN_SPEED_MIN,           //Minimum Fan Speed feature
    SMU_13_0_0_ODFEATURE_TEMPERATURE_FAN         = 1 << SMU_13_0_0_ODCAP_TEMPERATURE_FAN,         //Fan Target Temperature Limit feature
    SMU_13_0_0_ODFEATURE_TEMPERATURE_SYSTEM      = 1 << SMU_13_0_0_ODCAP_TEMPERATURE_SYSTEM,      //Operating Temperature Limit feature
    SMU_13_0_0_ODFEATURE_MEMORY_TIMING_TUNE      = 1 << SMU_13_0_0_ODCAP_MEMORY_TIMING_TUNE,      //AC Timing Tuning feature
    SMU_13_0_0_ODFEATURE_FAN_ZERO_RPM_CONTROL    = 1 << SMU_13_0_0_ODCAP_FAN_ZERO_RPM_CONTROL,    //Zero RPM feature
    SMU_13_0_0_ODFEATURE_AUTO_UV_ENGINE          = 1 << SMU_13_0_0_ODCAP_AUTO_UV_ENGINE,          //Auto Under Volt GFXCLK feature
    SMU_13_0_0_ODFEATURE_AUTO_OC_ENGINE          = 1 << SMU_13_0_0_ODCAP_AUTO_OC_ENGINE,          //Auto Over Clock GFXCLK feature
    SMU_13_0_0_ODFEATURE_AUTO_OC_MEMORY          = 1 << SMU_13_0_0_ODCAP_AUTO_OC_MEMORY,          //Auto Over Clock MCLK feature
    SMU_13_0_0_ODFEATURE_FAN_CURVE               = 1 << SMU_13_0_0_ODCAP_FAN_CURVE,               //Fan Curve feature
    SMU_13_0_0_ODFEATURE_AUTO_FAN_ACOUSTIC_LIMIT = 1 << SMU_13_0_0_ODCAP_AUTO_FAN_ACOUSTIC_LIMIT, //Auto Fan Acoustic RPM feature
    SMU_13_0_0_ODFEATURE_POWER_MODE              = 1 << SMU_13_0_0_ODCAP_POWER_MODE,              //Optimized GPU Power Mode feature
    SMU_13_0_0_ODFEATURE_COUNT                   = 16,
};

#define SMU_13_0_0_MAX_ODFEATURE 32 //Maximum Number of OD Features

enum SMU_13_0_0_ODSETTING_ID
{
    SMU_13_0_0_ODSETTING_GFXCLKFMAX = 0,
    SMU_13_0_0_ODSETTING_GFXCLKFMIN,
    SMU_13_0_0_ODSETTING_CUSTOM_GFX_VF_CURVE_A,
    SMU_13_0_0_ODSETTING_CUSTOM_GFX_VF_CURVE_B,
    SMU_13_0_0_ODSETTING_CUSTOM_GFX_VF_CURVE_C,
    SMU_13_0_0_ODSETTING_CUSTOM_CURVE_VFT_FMIN,
    SMU_13_0_0_ODSETTING_UCLKFMIN,
    SMU_13_0_0_ODSETTING_UCLKFMAX,
    SMU_13_0_0_ODSETTING_POWERPERCENTAGE,
    SMU_13_0_0_ODSETTING_FANRPMMIN,
    SMU_13_0_0_ODSETTING_FANRPMACOUSTICLIMIT,
    SMU_13_0_0_ODSETTING_FANTARGETTEMPERATURE,
    SMU_13_0_0_ODSETTING_OPERATINGTEMPMAX,
    SMU_13_0_0_ODSETTING_ACTIMING,
    SMU_13_0_0_ODSETTING_FAN_ZERO_RPM_CONTROL,
    SMU_13_0_0_ODSETTING_AUTOUVENGINE,
    SMU_13_0_0_ODSETTING_AUTOOCENGINE,
    SMU_13_0_0_ODSETTING_AUTOOCMEMORY,
    SMU_13_0_0_ODSETTING_FAN_CURVE_TEMPERATURE_1,
    SMU_13_0_0_ODSETTING_FAN_CURVE_SPEED_1,
    SMU_13_0_0_ODSETTING_FAN_CURVE_TEMPERATURE_2,
    SMU_13_0_0_ODSETTING_FAN_CURVE_SPEED_2,
    SMU_13_0_0_ODSETTING_FAN_CURVE_TEMPERATURE_3,
    SMU_13_0_0_ODSETTING_FAN_CURVE_SPEED_3,
    SMU_13_0_0_ODSETTING_FAN_CURVE_TEMPERATURE_4,
    SMU_13_0_0_ODSETTING_FAN_CURVE_SPEED_4,
    SMU_13_0_0_ODSETTING_FAN_CURVE_TEMPERATURE_5,
    SMU_13_0_0_ODSETTING_FAN_CURVE_SPEED_5,
    SMU_13_0_0_ODSETTING_AUTO_FAN_ACOUSTIC_LIMIT,
    SMU_13_0_0_ODSETTING_POWER_MODE,
    SMU_13_0_0_ODSETTING_COUNT,
};
#define SMU_13_0_0_MAX_ODSETTING 64 //Maximum Number of ODSettings

enum SMU_13_0_0_PWRMODE_SETTING
{
    SMU_13_0_0_PMSETTING_POWER_LIMIT_QUIET = 0,
    SMU_13_0_0_PMSETTING_POWER_LIMIT_BALANCE,
    SMU_13_0_0_PMSETTING_POWER_LIMIT_TURBO,
    SMU_13_0_0_PMSETTING_POWER_LIMIT_RAGE,
    SMU_13_0_0_PMSETTING_ACOUSTIC_TEMP_QUIET,
    SMU_13_0_0_PMSETTING_ACOUSTIC_TEMP_BALANCE,
    SMU_13_0_0_PMSETTING_ACOUSTIC_TEMP_TURBO,
    SMU_13_0_0_PMSETTING_ACOUSTIC_TEMP_RAGE,
    SMU_13_0_0_PMSETTING_ACOUSTIC_TARGET_RPM_QUIET,
    SMU_13_0_0_PMSETTING_ACOUSTIC_TARGET_RPM_BALANCE,
    SMU_13_0_0_PMSETTING_ACOUSTIC_TARGET_RPM_TURBO,
    SMU_13_0_0_PMSETTING_ACOUSTIC_TARGET_RPM_RAGE,
    SMU_13_0_0_PMSETTING_ACOUSTIC_LIMIT_RPM_QUIET,
    SMU_13_0_0_PMSETTING_ACOUSTIC_LIMIT_RPM_BALANCE,
    SMU_13_0_0_PMSETTING_ACOUSTIC_LIMIT_RPM_TURBO,
    SMU_13_0_0_PMSETTING_ACOUSTIC_LIMIT_RPM_RAGE,
};
#define SMU_13_0_0_MAX_PMSETTING 32 //Maximum Number of PowerMode Settings

struct smu_13_0_0_overdrive_table
{
    uint8_t revision;                             //Revision = SMU_13_0_0_PP_OVERDRIVE_VERSION
    uint8_t reserve[3];                           //Zero filled field reserved for future use
    uint32_t feature_count;                       //Total number of supported features
    uint32_t setting_count;                       //Total number of supported settings
    uint8_t cap[SMU_13_0_0_MAX_ODFEATURE];        //OD feature support flags
    uint32_t max[SMU_13_0_0_MAX_ODSETTING];       //default maximum settings
    uint32_t min[SMU_13_0_0_MAX_ODSETTING];       //default minimum settings
    int16_t pm_setting[SMU_13_0_0_MAX_PMSETTING]; //Optimized power mode feature settings
};

enum SMU_13_0_0_PPCLOCK_ID
{
    SMU_13_0_0_PPCLOCK_GFXCLK = 0,
    SMU_13_0_0_PPCLOCK_SOCCLK,
    SMU_13_0_0_PPCLOCK_UCLK,
    SMU_13_0_0_PPCLOCK_FCLK,
    SMU_13_0_0_PPCLOCK_DCLK_0,
    SMU_13_0_0_PPCLOCK_VCLK_0,
    SMU_13_0_0_PPCLOCK_DCLK_1,
    SMU_13_0_0_PPCLOCK_VCLK_1,
    SMU_13_0_0_PPCLOCK_DCEFCLK,
    SMU_13_0_0_PPCLOCK_DISPCLK,
    SMU_13_0_0_PPCLOCK_PIXCLK,
    SMU_13_0_0_PPCLOCK_PHYCLK,
    SMU_13_0_0_PPCLOCK_DTBCLK,
    SMU_13_0_0_PPCLOCK_COUNT,
};
#define SMU_13_0_0_MAX_PPCLOCK 16 //Maximum Number of PP Clocks

struct smu_13_0_0_powerplay_table
{
    struct atom_common_table_header header; //For SMU13, header.format_revision = 15, header.content_revision = 0
    uint8_t table_revision;                 //For SMU13, table_revision = 2
    uint8_t padding;
    uint16_t table_size;                    //Driver portion table size. The offset to smc_pptable including header size
    uint32_t golden_pp_id;                  //PPGen use only: PP Table ID on the Golden Data Base
    uint32_t golden_revision;               //PPGen use only: PP Table Revision on the Golden Data Base
    uint16_t format_id;                     //PPGen use only: PPTable for different ASICs. For SMU13 this should be 0x80
    uint32_t platform_caps;                 //POWERPLAYABLE::ulPlatformCaps

    uint8_t thermal_controller_type; //one of SMU_13_0_0_PP_THERMALCONTROLLER

    uint16_t small_power_limit1;
    uint16_t small_power_limit2;
    uint16_t boost_power_limit; //For Gemini Board, when the slave adapter is in BACO mode, the master adapter will use this boost power limit instead of the default power limit to boost the power limit.
    uint16_t software_shutdown_temp;

    uint32_t reserve[45];

    struct smu_13_0_0_overdrive_table overdrive_table;
    uint8_t padding1;
    PPTable_t smc_pptable; //PPTable_t in driver_if.h
};

#pragma pack(pop)

#endif
