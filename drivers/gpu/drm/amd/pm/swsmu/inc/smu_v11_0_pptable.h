/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
#ifndef SMU_11_0_PPTABLE_H
#define SMU_11_0_PPTABLE_H

#pragma pack(push, 1)

#define SMU_11_0_TABLE_FORMAT_REVISION                  12

//// POWERPLAYTABLE::ulPlatformCaps
#define SMU_11_0_PP_PLATFORM_CAP_POWERPLAY              0x1
#define SMU_11_0_PP_PLATFORM_CAP_SBIOSPOWERSOURCE       0x2
#define SMU_11_0_PP_PLATFORM_CAP_HARDWAREDC             0x4
#define SMU_11_0_PP_PLATFORM_CAP_BACO                   0x8
#define SMU_11_0_PP_PLATFORM_CAP_MACO                   0x10
#define SMU_11_0_PP_PLATFORM_CAP_SHADOWPSTATE           0x20

// SMU_11_0_PP_THERMALCONTROLLER - Thermal Controller Type
#define SMU_11_0_PP_THERMALCONTROLLER_NONE              0

#define SMU_11_0_PP_OVERDRIVE_VERSION                   0x0800
#define SMU_11_0_PP_POWERSAVINGCLOCK_VERSION            0x0100

enum SMU_11_0_ODFEATURE_CAP {
    SMU_11_0_ODCAP_GFXCLK_LIMITS = 0,
    SMU_11_0_ODCAP_GFXCLK_CURVE,
    SMU_11_0_ODCAP_UCLK_MAX,
    SMU_11_0_ODCAP_POWER_LIMIT,
    SMU_11_0_ODCAP_FAN_ACOUSTIC_LIMIT,
    SMU_11_0_ODCAP_FAN_SPEED_MIN,
    SMU_11_0_ODCAP_TEMPERATURE_FAN,
    SMU_11_0_ODCAP_TEMPERATURE_SYSTEM,
    SMU_11_0_ODCAP_MEMORY_TIMING_TUNE,
    SMU_11_0_ODCAP_FAN_ZERO_RPM_CONTROL,
    SMU_11_0_ODCAP_AUTO_UV_ENGINE,
    SMU_11_0_ODCAP_AUTO_OC_ENGINE,
    SMU_11_0_ODCAP_AUTO_OC_MEMORY,
    SMU_11_0_ODCAP_FAN_CURVE,
    SMU_11_0_ODCAP_COUNT,
};

enum SMU_11_0_ODFEATURE_ID {
    SMU_11_0_ODFEATURE_GFXCLK_LIMITS        = 1 << SMU_11_0_ODCAP_GFXCLK_LIMITS,            //GFXCLK Limit feature
    SMU_11_0_ODFEATURE_GFXCLK_CURVE         = 1 << SMU_11_0_ODCAP_GFXCLK_CURVE,             //GFXCLK Curve feature
    SMU_11_0_ODFEATURE_UCLK_MAX             = 1 << SMU_11_0_ODCAP_UCLK_MAX,                 //UCLK Limit feature
    SMU_11_0_ODFEATURE_POWER_LIMIT          = 1 << SMU_11_0_ODCAP_POWER_LIMIT,              //Power Limit feature
    SMU_11_0_ODFEATURE_FAN_ACOUSTIC_LIMIT   = 1 << SMU_11_0_ODCAP_FAN_ACOUSTIC_LIMIT,       //Fan Acoustic RPM feature
    SMU_11_0_ODFEATURE_FAN_SPEED_MIN        = 1 << SMU_11_0_ODCAP_FAN_SPEED_MIN,            //Minimum Fan Speed feature
    SMU_11_0_ODFEATURE_TEMPERATURE_FAN      = 1 << SMU_11_0_ODCAP_TEMPERATURE_FAN,          //Fan Target Temperature Limit feature
    SMU_11_0_ODFEATURE_TEMPERATURE_SYSTEM   = 1 << SMU_11_0_ODCAP_TEMPERATURE_SYSTEM,       //Operating Temperature Limit feature
    SMU_11_0_ODFEATURE_MEMORY_TIMING_TUNE   = 1 << SMU_11_0_ODCAP_MEMORY_TIMING_TUNE,       //AC Timing Tuning feature
    SMU_11_0_ODFEATURE_FAN_ZERO_RPM_CONTROL = 1 << SMU_11_0_ODCAP_FAN_ZERO_RPM_CONTROL,     //Zero RPM feature
    SMU_11_0_ODFEATURE_AUTO_UV_ENGINE       = 1 << SMU_11_0_ODCAP_AUTO_UV_ENGINE,           //Auto Under Volt GFXCLK feature
    SMU_11_0_ODFEATURE_AUTO_OC_ENGINE       = 1 << SMU_11_0_ODCAP_AUTO_OC_ENGINE,           //Auto Over Clock GFXCLK feature
    SMU_11_0_ODFEATURE_AUTO_OC_MEMORY       = 1 << SMU_11_0_ODCAP_AUTO_OC_MEMORY,           //Auto Over Clock MCLK feature
    SMU_11_0_ODFEATURE_FAN_CURVE            = 1 << SMU_11_0_ODCAP_FAN_CURVE,                //Fan Curve feature
    SMU_11_0_ODFEATURE_COUNT                = 14,
};
#define SMU_11_0_MAX_ODFEATURE    32          //Maximum Number of OD Features

enum SMU_11_0_ODSETTING_ID {
    SMU_11_0_ODSETTING_GFXCLKFMAX = 0,
    SMU_11_0_ODSETTING_GFXCLKFMIN,
    SMU_11_0_ODSETTING_VDDGFXCURVEFREQ_P1,
    SMU_11_0_ODSETTING_VDDGFXCURVEVOLTAGE_P1,
    SMU_11_0_ODSETTING_VDDGFXCURVEFREQ_P2,
    SMU_11_0_ODSETTING_VDDGFXCURVEVOLTAGE_P2,
    SMU_11_0_ODSETTING_VDDGFXCURVEFREQ_P3,
    SMU_11_0_ODSETTING_VDDGFXCURVEVOLTAGE_P3,
    SMU_11_0_ODSETTING_UCLKFMAX,
    SMU_11_0_ODSETTING_POWERPERCENTAGE,
    SMU_11_0_ODSETTING_FANRPMMIN,
    SMU_11_0_ODSETTING_FANRPMACOUSTICLIMIT,
    SMU_11_0_ODSETTING_FANTARGETTEMPERATURE,
    SMU_11_0_ODSETTING_OPERATINGTEMPMAX,
    SMU_11_0_ODSETTING_ACTIMING,
    SMU_11_0_ODSETTING_FAN_ZERO_RPM_CONTROL,
    SMU_11_0_ODSETTING_AUTOUVENGINE,
    SMU_11_0_ODSETTING_AUTOOCENGINE,
    SMU_11_0_ODSETTING_AUTOOCMEMORY,
    SMU_11_0_ODSETTING_COUNT,
};
#define SMU_11_0_MAX_ODSETTING    32          //Maximum Number of ODSettings

struct smu_11_0_overdrive_table
{
    uint8_t  revision;                                        //Revision = SMU_11_0_PP_OVERDRIVE_VERSION
    uint8_t  reserve[3];                                      //Zero filled field reserved for future use
    uint32_t feature_count;                                   //Total number of supported features
    uint32_t setting_count;                                   //Total number of supported settings
    uint8_t  cap[SMU_11_0_MAX_ODFEATURE];                     //OD feature support flags
    uint32_t max[SMU_11_0_MAX_ODSETTING];                     //default maximum settings
    uint32_t min[SMU_11_0_MAX_ODSETTING];                     //default minimum settings
};

enum SMU_11_0_PPCLOCK_ID {
    SMU_11_0_PPCLOCK_GFXCLK = 0,
    SMU_11_0_PPCLOCK_VCLK,
    SMU_11_0_PPCLOCK_DCLK,
    SMU_11_0_PPCLOCK_ECLK,
    SMU_11_0_PPCLOCK_SOCCLK,
    SMU_11_0_PPCLOCK_UCLK,
    SMU_11_0_PPCLOCK_DCEFCLK,
    SMU_11_0_PPCLOCK_DISPCLK,
    SMU_11_0_PPCLOCK_PIXCLK,
    SMU_11_0_PPCLOCK_PHYCLK,
    SMU_11_0_PPCLOCK_COUNT,
};
#define SMU_11_0_MAX_PPCLOCK      16          //Maximum Number of PP Clocks

struct smu_11_0_power_saving_clock_table
{
    uint8_t  revision;                                        //Revision = SMU_11_0_PP_POWERSAVINGCLOCK_VERSION
    uint8_t  reserve[3];                                      //Zero filled field reserved for future use
    uint32_t count;                                           //power_saving_clock_count = SMU_11_0_PPCLOCK_COUNT
    uint32_t max[SMU_11_0_MAX_PPCLOCK];                       //PowerSavingClock Mode Clock Maximum array In MHz
    uint32_t min[SMU_11_0_MAX_PPCLOCK];                       //PowerSavingClock Mode Clock Minimum array In MHz
};

struct smu_11_0_powerplay_table
{
      struct atom_common_table_header header;
      uint8_t  table_revision;
      uint16_t table_size;                          //Driver portion table size. The offset to smc_pptable including header size
      uint32_t golden_pp_id;
      uint32_t golden_revision;
      uint16_t format_id;
      uint32_t platform_caps;                       //POWERPLAYABLE::ulPlatformCaps
                                                    
      uint8_t  thermal_controller_type;             //one of SMU_11_0_PP_THERMALCONTROLLER

      uint16_t small_power_limit1;
      uint16_t small_power_limit2;
      uint16_t boost_power_limit;
      uint16_t od_turbo_power_limit;                //Power limit setting for Turbo mode in Performance UI Tuning. 
      uint16_t od_power_save_power_limit;           //Power limit setting for PowerSave/Optimal mode in Performance UI Tuning. 
      uint16_t software_shutdown_temp;

      uint16_t reserve[6];                          //Zero filled field reserved for future use

      struct smu_11_0_power_saving_clock_table      power_saving_clock;
      struct smu_11_0_overdrive_table               overdrive_table;

#ifndef SMU_11_0_PARTIAL_PPTABLE
      PPTable_t smc_pptable;                        //PPTable_t in smu11_driver_if.h
#endif
};

#pragma pack(pop)

#endif
