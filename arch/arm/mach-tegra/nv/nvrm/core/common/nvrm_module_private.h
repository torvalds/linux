/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef NVRM_MODULE_PRIVATE_H
#define NVRM_MODULE_PRIVATE_H

#include "nvcommon.h"
#include "nvrm_init.h"
#include "nvrm_relocation_table.h"
#include "nvrm_moduleids.h"

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

typedef struct NvRmModuleTableRec
{
    NvRmModule Modules[NvRmPrivModuleID_Num];
    NvRmModuleInstance *ModInst;
    NvRmModuleInstance *LastModInst;
    NvU32 NumModuleInstances;
    NvRmIrqMap IrqMap;
} NvRmModuleTable;

/**
 * Initialize the module info via the relocation table.
 *
 * @param mod_table The module table
 * @param reloc_table The relocation table
 * @param modid The module id conversion function
 */
NvError
NvRmPrivModuleInit(
    NvRmModuleTable *mod_table,
    NvU32 *reloc_table);

void
NvRmPrivModuleDeinit(
    NvRmModuleTable *mod_table );

NvRmModuleTable *
NvRmPrivGetModuleTable(
    NvRmDeviceHandle hDevice );

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif // NVRM_MODULE_PRIVATE_H
