/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#ifndef SMU8_FUSION_H
#define SMU8_FUSION_H

#include "smu8.h"

#pragma pack(push, 1)

#define SMU8_MAX_CUS 2
#define SMU8_PSMS_PER_CU 4
#define SMU8_CACS_PER_CU 4

struct SMU8_GfxCuPgScoreboard {
    uint8_t Enabled;
    uint8_t spare[3];
};

struct SMU8_Port80MonitorTable {
	uint32_t MmioAddress;
	uint32_t MemoryBaseHi;
	uint32_t MemoryBaseLo;
	uint16_t MemoryBufferSize;
	uint16_t MemoryPosition;
	uint16_t PollingInterval;
	uint8_t  EnableCsrShadow;
	uint8_t  EnableDramShadow;
};

/*  Display specific power management parameters */
#define PWRMGT_SEPARATION_TIME_SHIFT            0
#define PWRMGT_SEPARATION_TIME_MASK             0xFFFF
#define PWRMGT_DISABLE_CPU_CSTATES_SHIFT        16
#define PWRMGT_DISABLE_CPU_CSTATES_MASK         0x1
#define PWRMGT_DISABLE_CPU_PSTATES_SHIFT        24
#define PWRMGT_DISABLE_CPU_PSTATES_MASK         0x1

/* Clock Table Definitions */
#define NUM_SCLK_LEVELS     8
#define NUM_LCLK_LEVELS     8
#define NUM_UVD_LEVELS      8
#define NUM_ECLK_LEVELS     8
#define NUM_ACLK_LEVELS     8

struct SMU8_Fusion_ClkLevel {
	uint8_t		GnbVid;
	uint8_t		GfxVid;
	uint8_t		DfsDid;
	uint8_t		DeepSleepDid;
	uint32_t	DfsBypass;
	uint32_t	Frequency;
};

struct SMU8_Fusion_SclkBreakdownTable {
	struct SMU8_Fusion_ClkLevel ClkLevel[NUM_SCLK_LEVELS];
	struct SMU8_Fusion_ClkLevel DpmOffLevel;
	/* SMU8_Fusion_ClkLevel PwrOffLevel; */
	uint32_t    SclkValidMask;
	uint32_t    MaxSclkIndex;
};

struct SMU8_Fusion_LclkBreakdownTable {
	struct SMU8_Fusion_ClkLevel ClkLevel[NUM_LCLK_LEVELS];
	struct SMU8_Fusion_ClkLevel DpmOffLevel;
    /* SMU8_Fusion_ClkLevel PwrOffLevel; */
	uint32_t    LclkValidMask;
	uint32_t    MaxLclkIndex;
};

struct SMU8_Fusion_EclkBreakdownTable {
	struct SMU8_Fusion_ClkLevel ClkLevel[NUM_ECLK_LEVELS];
	struct SMU8_Fusion_ClkLevel DpmOffLevel;
	struct SMU8_Fusion_ClkLevel PwrOffLevel;
	uint32_t    EclkValidMask;
	uint32_t    MaxEclkIndex;
};

struct SMU8_Fusion_VclkBreakdownTable {
	struct SMU8_Fusion_ClkLevel ClkLevel[NUM_UVD_LEVELS];
	struct SMU8_Fusion_ClkLevel DpmOffLevel;
	struct SMU8_Fusion_ClkLevel PwrOffLevel;
	uint32_t    VclkValidMask;
	uint32_t    MaxVclkIndex;
};

struct SMU8_Fusion_DclkBreakdownTable {
	struct SMU8_Fusion_ClkLevel ClkLevel[NUM_UVD_LEVELS];
	struct SMU8_Fusion_ClkLevel DpmOffLevel;
	struct SMU8_Fusion_ClkLevel PwrOffLevel;
	uint32_t    DclkValidMask;
	uint32_t    MaxDclkIndex;
};

struct SMU8_Fusion_AclkBreakdownTable {
	struct SMU8_Fusion_ClkLevel ClkLevel[NUM_ACLK_LEVELS];
	struct SMU8_Fusion_ClkLevel DpmOffLevel;
	struct SMU8_Fusion_ClkLevel PwrOffLevel;
	uint32_t    AclkValidMask;
	uint32_t    MaxAclkIndex;
};


struct SMU8_Fusion_ClkTable {
	struct SMU8_Fusion_SclkBreakdownTable SclkBreakdownTable;
	struct SMU8_Fusion_LclkBreakdownTable LclkBreakdownTable;
	struct SMU8_Fusion_EclkBreakdownTable EclkBreakdownTable;
	struct SMU8_Fusion_VclkBreakdownTable VclkBreakdownTable;
	struct SMU8_Fusion_DclkBreakdownTable DclkBreakdownTable;
	struct SMU8_Fusion_AclkBreakdownTable AclkBreakdownTable;
};

#pragma pack(pop)

#endif
