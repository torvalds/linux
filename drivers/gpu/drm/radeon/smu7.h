/*
 * Copyright 2013 Advanced Micro Devices, Inc.
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

#ifndef SMU7_H
#define SMU7_H

#pragma pack(push, 1)

#define SMU7_CONTEXT_ID_SMC        1
#define SMU7_CONTEXT_ID_VBIOS      2


#define SMU7_CONTEXT_ID_SMC        1
#define SMU7_CONTEXT_ID_VBIOS      2

#define SMU7_MAX_LEVELS_VDDC            8
#define SMU7_MAX_LEVELS_VDDCI           4
#define SMU7_MAX_LEVELS_MVDD            4
#define SMU7_MAX_LEVELS_VDDNB           8

#define SMU7_MAX_LEVELS_GRAPHICS        SMU__NUM_SCLK_DPM_STATE   // SCLK + SQ DPM + ULV
#define SMU7_MAX_LEVELS_MEMORY          SMU__NUM_MCLK_DPM_LEVELS   // MCLK Levels DPM
#define SMU7_MAX_LEVELS_GIO             SMU__NUM_LCLK_DPM_LEVELS  // LCLK Levels
#define SMU7_MAX_LEVELS_LINK            SMU__NUM_PCIE_DPM_LEVELS  // PCIe speed and number of lanes.
#define SMU7_MAX_LEVELS_UVD             8   // VCLK/DCLK levels for UVD.
#define SMU7_MAX_LEVELS_VCE             8   // ECLK levels for VCE.
#define SMU7_MAX_LEVELS_ACP             8   // ACLK levels for ACP.
#define SMU7_MAX_LEVELS_SAMU            8   // SAMCLK levels for SAMU.
#define SMU7_MAX_ENTRIES_SMIO           32  // Number of entries in SMIO table.

#define DPM_NO_LIMIT 0
#define DPM_NO_UP 1
#define DPM_GO_DOWN 2
#define DPM_GO_UP 3

#define SMU7_FIRST_DPM_GRAPHICS_LEVEL    0
#define SMU7_FIRST_DPM_MEMORY_LEVEL      0

#define GPIO_CLAMP_MODE_VRHOT      1
#define GPIO_CLAMP_MODE_THERM      2
#define GPIO_CLAMP_MODE_DC         4

#define SCRATCH_B_TARG_PCIE_INDEX_SHIFT 0
#define SCRATCH_B_TARG_PCIE_INDEX_MASK  (0x7<<SCRATCH_B_TARG_PCIE_INDEX_SHIFT)
#define SCRATCH_B_CURR_PCIE_INDEX_SHIFT 3
#define SCRATCH_B_CURR_PCIE_INDEX_MASK  (0x7<<SCRATCH_B_CURR_PCIE_INDEX_SHIFT)
#define SCRATCH_B_TARG_UVD_INDEX_SHIFT  6
#define SCRATCH_B_TARG_UVD_INDEX_MASK   (0x7<<SCRATCH_B_TARG_UVD_INDEX_SHIFT)
#define SCRATCH_B_CURR_UVD_INDEX_SHIFT  9
#define SCRATCH_B_CURR_UVD_INDEX_MASK   (0x7<<SCRATCH_B_CURR_UVD_INDEX_SHIFT)
#define SCRATCH_B_TARG_VCE_INDEX_SHIFT  12
#define SCRATCH_B_TARG_VCE_INDEX_MASK   (0x7<<SCRATCH_B_TARG_VCE_INDEX_SHIFT)
#define SCRATCH_B_CURR_VCE_INDEX_SHIFT  15
#define SCRATCH_B_CURR_VCE_INDEX_MASK   (0x7<<SCRATCH_B_CURR_VCE_INDEX_SHIFT)
#define SCRATCH_B_TARG_ACP_INDEX_SHIFT  18
#define SCRATCH_B_TARG_ACP_INDEX_MASK   (0x7<<SCRATCH_B_TARG_ACP_INDEX_SHIFT)
#define SCRATCH_B_CURR_ACP_INDEX_SHIFT  21
#define SCRATCH_B_CURR_ACP_INDEX_MASK   (0x7<<SCRATCH_B_CURR_ACP_INDEX_SHIFT)
#define SCRATCH_B_TARG_SAMU_INDEX_SHIFT 24
#define SCRATCH_B_TARG_SAMU_INDEX_MASK  (0x7<<SCRATCH_B_TARG_SAMU_INDEX_SHIFT)
#define SCRATCH_B_CURR_SAMU_INDEX_SHIFT 27
#define SCRATCH_B_CURR_SAMU_INDEX_MASK  (0x7<<SCRATCH_B_CURR_SAMU_INDEX_SHIFT)


struct SMU7_PIDController
{
    uint32_t Ki;
    int32_t LFWindupUL;
    int32_t LFWindupLL;
    uint32_t StatePrecision;
    uint32_t LfPrecision;
    uint32_t LfOffset;
    uint32_t MaxState;
    uint32_t MaxLfFraction;
    uint32_t StateShift;
};

typedef struct SMU7_PIDController SMU7_PIDController;

// -------------------------------------------------------------------------------------------------------------------------
#define SMU7_MAX_PCIE_LINK_SPEEDS 3 /* 0:Gen1 1:Gen2 2:Gen3 */

#define SMU7_SCLK_DPM_CONFIG_MASK                        0x01
#define SMU7_VOLTAGE_CONTROLLER_CONFIG_MASK              0x02
#define SMU7_THERMAL_CONTROLLER_CONFIG_MASK              0x04
#define SMU7_MCLK_DPM_CONFIG_MASK                        0x08
#define SMU7_UVD_DPM_CONFIG_MASK                         0x10
#define SMU7_VCE_DPM_CONFIG_MASK                         0x20
#define SMU7_ACP_DPM_CONFIG_MASK                         0x40
#define SMU7_SAMU_DPM_CONFIG_MASK                        0x80
#define SMU7_PCIEGEN_DPM_CONFIG_MASK                    0x100

#define SMU7_ACP_MCLK_HANDSHAKE_DISABLE                  0x00000001
#define SMU7_ACP_SCLK_HANDSHAKE_DISABLE                  0x00000002
#define SMU7_UVD_MCLK_HANDSHAKE_DISABLE                  0x00000100
#define SMU7_UVD_SCLK_HANDSHAKE_DISABLE                  0x00000200
#define SMU7_VCE_MCLK_HANDSHAKE_DISABLE                  0x00010000
#define SMU7_VCE_SCLK_HANDSHAKE_DISABLE                  0x00020000

struct SMU7_Firmware_Header
{
    uint32_t Digest[5];
    uint32_t Version;
    uint32_t HeaderSize;
    uint32_t Flags;
    uint32_t EntryPoint;
    uint32_t CodeSize;
    uint32_t ImageSize;

    uint32_t Rtos;
    uint32_t SoftRegisters;
    uint32_t DpmTable;
    uint32_t FanTable;
    uint32_t CacConfigTable;
    uint32_t CacStatusTable;

    uint32_t mcRegisterTable;

    uint32_t mcArbDramTimingTable;

    uint32_t PmFuseTable;
    uint32_t Globals;
    uint32_t Reserved[42];
    uint32_t Signature;
};

typedef struct SMU7_Firmware_Header SMU7_Firmware_Header;

#define SMU7_FIRMWARE_HEADER_LOCATION 0x20000

enum  DisplayConfig {
    PowerDown = 1,
    DP54x4,
    DP54x2,
    DP54x1,
    DP27x4,
    DP27x2,
    DP27x1,
    HDMI297,
    HDMI162,
    LVDS,
    DP324x4,
    DP324x2,
    DP324x1
};

#pragma pack(pop)

#endif

