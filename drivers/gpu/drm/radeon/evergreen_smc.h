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
#ifndef __EVERGREEN_SMC_H__
#define __EVERGREEN_SMC_H__

#include "rv770_smc.h"

#pragma pack(push, 1)

#define SMC_EVERGREEN_MC_REGISTER_ARRAY_SIZE 16

struct SMC_Evergreen_MCRegisterAddress
{
    uint16_t s0;
    uint16_t s1;
};

typedef struct SMC_Evergreen_MCRegisterAddress SMC_Evergreen_MCRegisterAddress;


struct SMC_Evergreen_MCRegisterSet
{
    uint32_t value[SMC_EVERGREEN_MC_REGISTER_ARRAY_SIZE];
};

typedef struct SMC_Evergreen_MCRegisterSet SMC_Evergreen_MCRegisterSet;

struct SMC_Evergreen_MCRegisters
{
    uint8_t                             last;
    uint8_t                             reserved[3];
    SMC_Evergreen_MCRegisterAddress     address[SMC_EVERGREEN_MC_REGISTER_ARRAY_SIZE];
    SMC_Evergreen_MCRegisterSet         data[5];
};

typedef struct SMC_Evergreen_MCRegisters SMC_Evergreen_MCRegisters;

#define EVERGREEN_SMC_FIRMWARE_HEADER_LOCATION 0x100

#define EVERGREEN_SMC_FIRMWARE_HEADER_softRegisters   0x0
#define EVERGREEN_SMC_FIRMWARE_HEADER_stateTable      0xC
#define EVERGREEN_SMC_FIRMWARE_HEADER_mcRegisterTable 0x20


#pragma pack(pop)

#endif
