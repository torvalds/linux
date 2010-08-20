/*
 * Copyright (c) 2010 NVIDIA Corporation.
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

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/delay.h>

#include <mach/iomap.h>

#include "nvcommon.h"
#include "nvrm_power.h"
#include "../../../../clock.h"

#define is_vcp(_mod) (NVRM_MODULE_ID_MODULE(_mod)==NvRmModuleID_Vcp)

#define is_bsea(_mod) (NVRM_MODULE_ID_MODULE(_mod)==NvRmModuleID_BseA)

#define is_vde(_mod) (NVRM_MODULE_ID_MODULE(_mod)==NvRmModuleID_Vde)

#define CLK_VI_CORE_EXTERNAL (1<<24)
#define CLK_VI_PAD_INTERNAL (1<<25)

NvError NvRmPowerModuleClockConfig(
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID ModuleId,
    NvU32 ClientId,
    NvRmFreqKHz MinFreq,
    NvRmFreqKHz MaxFreq,
    const NvRmFreqKHz *PrefFreqList,
    NvU32 PrefFreqListCount,
    NvRmFreqKHz *CurrentFreq,
    NvU32 flags)
{
    if (CurrentFreq)
        *CurrentFreq = 0;

    return NvSuccess;
}

NvError NvRmPowerModuleClockControl(
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID ModuleId,
    NvU32 ClientId,
    NvBool Enable)
{
    const char *vcp_names[] = { "vcp", NULL };
    const char *bsea_names[] = { "bsea", NULL };
    const char *vde_names[] = { "vde", NULL };
    const char **names = NULL;

    if (is_vcp(ModuleId))
        names = vcp_names;
    else if (is_bsea(ModuleId))
        names = bsea_names;
    else if (is_vde(ModuleId))
        names = vde_names;

    if (!names) {
        pr_err("%s: MOD[%lu] INST[%lu] not supported\n", __func__,
               NVRM_MODULE_ID_MODULE(ModuleId),
               NVRM_MODULE_ID_INSTANCE(ModuleId));
        return NvError_BadParameter;
    }

    for ( ; *names ; names++) {
        struct clk *clk = clk_get_sys(*names, NULL);

        if (IS_ERR_OR_NULL(clk)) {
            pr_err("%s: unable to get struct clk for %s\n", __func__, *names);
            continue;
        }

        if (Enable)
            clk_enable(clk);
        else
            clk_disable(clk);
    }

    return NvSuccess;
}

NvError NvRmPowerVoltageControl( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID ModuleId,
    NvU32 ClientId,
    NvRmMilliVolts MinVolts,
    NvRmMilliVolts MaxVolts,
    const NvRmMilliVolts * PrefVoltageList,
    NvU32 PrefVoltageListCount,
    NvRmMilliVolts * CurrentVolts)
{
    return NvSuccess;
}
