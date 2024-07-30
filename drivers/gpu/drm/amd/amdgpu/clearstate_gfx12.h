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
#ifndef __CLEARSTATE_GFX12_H_
#define __CLEARSTATE_GFX12_H_

static const unsigned int gfx12_SECT_CONTEXT_def_1[] = {
0x00000000, //mmSC_MEM_TEMPORAL
0x00000000, //mmSC_MEM_SPEC_READ
0x00000000, //mmPA_SC_VPORT_0_TL
0x00000000, //mmPA_SC_VPORT_0_BR
0x00000000, //mmPA_SC_VPORT_1_TL
0x00000000, //mmPA_SC_VPORT_1_BR
0x00000000, //mmPA_SC_VPORT_2_TL
0x00000000, //mmPA_SC_VPORT_2_BR
0x00000000, //mmPA_SC_VPORT_3_TL
0x00000000, //mmPA_SC_VPORT_3_BR
0x00000000, //mmPA_SC_VPORT_4_TL
0x00000000, //mmPA_SC_VPORT_4_BR
0x00000000, //mmPA_SC_VPORT_5_TL
0x00000000, //mmPA_SC_VPORT_5_BR
0x00000000, //mmPA_SC_VPORT_6_TL
0x00000000, //mmPA_SC_VPORT_6_BR
0x00000000, //mmPA_SC_VPORT_7_TL
0x00000000, //mmPA_SC_VPORT_7_BR
0x00000000, //mmPA_SC_VPORT_8_TL
0x00000000, //mmPA_SC_VPORT_8_BR
0x00000000, //mmPA_SC_VPORT_9_TL
0x00000000, //mmPA_SC_VPORT_9_BR
0x00000000, //mmPA_SC_VPORT_10_TL
0x00000000, //mmPA_SC_VPORT_10_BR
0x00000000, //mmPA_SC_VPORT_11_TL
0x00000000, //mmPA_SC_VPORT_11_BR
0x00000000, //mmPA_SC_VPORT_12_TL
0x00000000, //mmPA_SC_VPORT_12_BR
0x00000000, //mmPA_SC_VPORT_13_TL
0x00000000, //mmPA_SC_VPORT_13_BR
0x00000000, //mmPA_SC_VPORT_14_TL
0x00000000, //mmPA_SC_VPORT_14_BR
0x00000000, //mmPA_SC_VPORT_15_TL
0x00000000, //mmPA_SC_VPORT_15_BR
};

static const unsigned int gfx12_SECT_CONTEXT_def_2[] = {
0x00000000, //mmPA_CL_PROG_NEAR_CLIP_Z
0x00000000, //mmPA_RATE_CNTL
};

static const unsigned int gfx12_SECT_CONTEXT_def_3[] = {
0x00000000, //mmCP_PERFMON_CNTX_CNTL
};

static const unsigned int gfx12_SECT_CONTEXT_def_4[] = {
0x00000000, //mmCONTEXT_RESERVED_REG0
0x00000000, //mmCONTEXT_RESERVED_REG1
0x00000000, //mmPA_SC_CLIPRECT_0_EXT
0x00000000, //mmPA_SC_CLIPRECT_1_EXT
0x00000000, //mmPA_SC_CLIPRECT_2_EXT
0x00000000, //mmPA_SC_CLIPRECT_3_EXT
};

static const unsigned int gfx12_SECT_CONTEXT_def_5[] = {
0x00000000, //mmPA_SC_HIZ_INFO
0x00000000, //mmPA_SC_HIS_INFO
0x00000000, //mmPA_SC_HIZ_BASE
0x00000000, //mmPA_SC_HIZ_BASE_EXT
0x00000000, //mmPA_SC_HIZ_SIZE_XY
0x00000000, //mmPA_SC_HIS_BASE
0x00000000, //mmPA_SC_HIS_BASE_EXT
0x00000000, //mmPA_SC_HIS_SIZE_XY
0x00000000, //mmPA_SC_BINNER_OUTPUT_TIMEOUT_CNTL
0x00000000, //mmPA_SC_BINNER_DYNAMIC_BATCH_LIMIT
0x00000000, //mmPA_SC_HISZ_CONTROL
};

static const unsigned int gfx12_SECT_CONTEXT_def_6[] = {
0x00000000, //mmCB_MEM0_INFO
0x00000000, //mmCB_MEM1_INFO
0x00000000, //mmCB_MEM2_INFO
0x00000000, //mmCB_MEM3_INFO
0x00000000, //mmCB_MEM4_INFO
0x00000000, //mmCB_MEM5_INFO
0x00000000, //mmCB_MEM6_INFO
0x00000000, //mmCB_MEM7_INFO
};

static const struct cs_extent_def gfx12_SECT_CONTEXT_defs[] = {
    {gfx12_SECT_CONTEXT_def_1, 0x0000a03e, 34 },
    {gfx12_SECT_CONTEXT_def_2, 0x0000a0cc, 2 },
    {gfx12_SECT_CONTEXT_def_3, 0x0000a0d8, 1 },
    {gfx12_SECT_CONTEXT_def_4, 0x0000a0db, 6 },
    {gfx12_SECT_CONTEXT_def_5, 0x0000a2e5, 11 },
    {gfx12_SECT_CONTEXT_def_6, 0x0000a3c0, 8 },
    { 0, 0, 0 }
};

static const struct cs_section_def gfx12_cs_data[] = {
    { gfx12_SECT_CONTEXT_defs, SECT_CONTEXT },
    { 0, SECT_NONE }
};

#endif /* __CLEARSTATE_GFX12_H_ */
