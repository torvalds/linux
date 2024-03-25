/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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
#ifndef __RV770_SMC_H__
#define __RV770_SMC_H__

#include "ppsmc.h"

#pragma pack(push, 1)

#define RV770_SMC_TABLE_ADDRESS 0xB000

#define RV770_SMC_PERFORMANCE_LEVELS_PER_SWSTATE    3

struct RV770_SMC_SCLK_VALUE {
    uint32_t        vCG_SPLL_FUNC_CNTL;
    uint32_t        vCG_SPLL_FUNC_CNTL_2;
    uint32_t        vCG_SPLL_FUNC_CNTL_3;
    uint32_t        vCG_SPLL_SPREAD_SPECTRUM;
    uint32_t        vCG_SPLL_SPREAD_SPECTRUM_2;
    uint32_t        sclk_value;
};

typedef struct RV770_SMC_SCLK_VALUE RV770_SMC_SCLK_VALUE;

struct RV770_SMC_MCLK_VALUE {
    uint32_t        vMPLL_AD_FUNC_CNTL;
    uint32_t        vMPLL_AD_FUNC_CNTL_2;
    uint32_t        vMPLL_DQ_FUNC_CNTL;
    uint32_t        vMPLL_DQ_FUNC_CNTL_2;
    uint32_t        vMCLK_PWRMGT_CNTL;
    uint32_t        vDLL_CNTL;
    uint32_t        vMPLL_SS;
    uint32_t        vMPLL_SS2;
    uint32_t        mclk_value;
};

typedef struct RV770_SMC_MCLK_VALUE RV770_SMC_MCLK_VALUE;


struct RV730_SMC_MCLK_VALUE {
    uint32_t        vMCLK_PWRMGT_CNTL;
    uint32_t        vDLL_CNTL;
    uint32_t        vMPLL_FUNC_CNTL;
    uint32_t        vMPLL_FUNC_CNTL2;
    uint32_t        vMPLL_FUNC_CNTL3;
    uint32_t        vMPLL_SS;
    uint32_t        vMPLL_SS2;
    uint32_t        mclk_value;
};

typedef struct RV730_SMC_MCLK_VALUE RV730_SMC_MCLK_VALUE;

struct RV770_SMC_VOLTAGE_VALUE {
    uint16_t             value;
    uint8_t              index;
    uint8_t              padding;
};

typedef struct RV770_SMC_VOLTAGE_VALUE RV770_SMC_VOLTAGE_VALUE;

union RV7XX_SMC_MCLK_VALUE {
    RV770_SMC_MCLK_VALUE    mclk770;
    RV730_SMC_MCLK_VALUE    mclk730;
};

typedef union RV7XX_SMC_MCLK_VALUE RV7XX_SMC_MCLK_VALUE, *LPRV7XX_SMC_MCLK_VALUE;

struct RV770_SMC_HW_PERFORMANCE_LEVEL {
    uint8_t                 arbValue;
    union{
        uint8_t             seqValue;
        uint8_t             ACIndex;
    };
    uint8_t                 displayWatermark;
    uint8_t                 gen2PCIE;
    uint8_t                 gen2XSP;
    uint8_t                 backbias;
    uint8_t                 strobeMode;
    uint8_t                 mcFlags;
    uint32_t                aT;
    uint32_t                bSP;
    RV770_SMC_SCLK_VALUE    sclk;
    RV7XX_SMC_MCLK_VALUE    mclk;
    RV770_SMC_VOLTAGE_VALUE vddc;
    RV770_SMC_VOLTAGE_VALUE mvdd;
    RV770_SMC_VOLTAGE_VALUE vddci;
    uint8_t                 reserved1;
    uint8_t                 reserved2;
    uint8_t                 stateFlags;
    uint8_t                 padding;
};

#define SMC_STROBE_RATIO    0x0F
#define SMC_STROBE_ENABLE   0x10

#define SMC_MC_EDC_RD_FLAG  0x01
#define SMC_MC_EDC_WR_FLAG  0x02
#define SMC_MC_RTT_ENABLE   0x04
#define SMC_MC_STUTTER_EN   0x08

typedef struct RV770_SMC_HW_PERFORMANCE_LEVEL RV770_SMC_HW_PERFORMANCE_LEVEL;

struct RV770_SMC_SWSTATE {
    uint8_t           flags;
    uint8_t           padding1;
    uint8_t           padding2;
    uint8_t           padding3;
    RV770_SMC_HW_PERFORMANCE_LEVEL levels[RV770_SMC_PERFORMANCE_LEVELS_PER_SWSTATE];
};

typedef struct RV770_SMC_SWSTATE RV770_SMC_SWSTATE;

#define RV770_SMC_VOLTAGEMASK_VDDC 0
#define RV770_SMC_VOLTAGEMASK_MVDD 1
#define RV770_SMC_VOLTAGEMASK_VDDCI 2
#define RV770_SMC_VOLTAGEMASK_MAX  4

struct RV770_SMC_VOLTAGEMASKTABLE {
    uint8_t  highMask[RV770_SMC_VOLTAGEMASK_MAX];
    uint32_t lowMask[RV770_SMC_VOLTAGEMASK_MAX];
};

typedef struct RV770_SMC_VOLTAGEMASKTABLE RV770_SMC_VOLTAGEMASKTABLE;

#define MAX_NO_VREG_STEPS 32

struct RV770_SMC_STATETABLE {
    uint8_t             thermalProtectType;
    uint8_t             systemFlags;
    uint8_t             maxVDDCIndexInPPTable;
    uint8_t             extraFlags;
    uint8_t             highSMIO[MAX_NO_VREG_STEPS];
    uint32_t            lowSMIO[MAX_NO_VREG_STEPS];
    RV770_SMC_VOLTAGEMASKTABLE voltageMaskTable;
    RV770_SMC_SWSTATE   initialState;
    RV770_SMC_SWSTATE   ACPIState;
    RV770_SMC_SWSTATE   driverState;
    RV770_SMC_SWSTATE   ULVState;
};

typedef struct RV770_SMC_STATETABLE RV770_SMC_STATETABLE;

#define PPSMC_STATEFLAG_AUTO_PULSE_SKIP 0x01

#pragma pack(pop)

#define RV770_SMC_SOFT_REGISTERS_START        0x104

#define RV770_SMC_SOFT_REGISTER_mclk_chg_timeout        0x0
#define RV770_SMC_SOFT_REGISTER_baby_step_timer         0x8
#define RV770_SMC_SOFT_REGISTER_delay_bbias             0xC
#define RV770_SMC_SOFT_REGISTER_delay_vreg              0x10
#define RV770_SMC_SOFT_REGISTER_delay_acpi              0x2C
#define RV770_SMC_SOFT_REGISTER_seq_index               0x64
#define RV770_SMC_SOFT_REGISTER_mvdd_chg_time           0x68
#define RV770_SMC_SOFT_REGISTER_mclk_switch_lim         0x78
#define RV770_SMC_SOFT_REGISTER_mc_block_delay          0x90
#define RV770_SMC_SOFT_REGISTER_uvd_enabled             0x9C
#define RV770_SMC_SOFT_REGISTER_is_asic_lombok          0xA0

int rv770_copy_bytes_to_smc(struct radeon_device *rdev,
			    u16 smc_start_address, const u8 *src,
			    u16 byte_count, u16 limit);
void rv770_start_smc(struct radeon_device *rdev);
void rv770_reset_smc(struct radeon_device *rdev);
void rv770_stop_smc_clock(struct radeon_device *rdev);
void rv770_start_smc_clock(struct radeon_device *rdev);
bool rv770_is_smc_running(struct radeon_device *rdev);
PPSMC_Result rv770_send_msg_to_smc(struct radeon_device *rdev, PPSMC_Msg msg);
PPSMC_Result rv770_wait_for_smc_inactive(struct radeon_device *rdev);
int rv770_read_smc_sram_dword(struct radeon_device *rdev,
			      u16 smc_address, u32 *value, u16 limit);
int rv770_write_smc_sram_dword(struct radeon_device *rdev,
			       u16 smc_address, u32 value, u16 limit);
int rv770_load_smc_ucode(struct radeon_device *rdev,
			 u16 limit);

#endif
