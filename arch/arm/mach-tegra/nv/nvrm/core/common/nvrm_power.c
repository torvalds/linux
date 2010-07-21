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

#define is_vi(module) (NVRM_MODULE_ID_MODULE(module)==NvRmModuleID_Vi)

#define is_csi(module) (NVRM_MODULE_ID_MODULE(module)==NvRmModuleID_Csi)

#define is_isp(module) (NVRM_MODULE_ID_MODULE(module)==NvRmModuleID_Isp)

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
    struct clk *clk = NULL;
    const char *name;
    unsigned long rate;
    int ret;

    if (CurrentFreq)
        *CurrentFreq = 0;

    if (!is_vi(ModuleId))
        return NvSuccess;

    if (flags & NvRmClockConfig_SubConfig)
        name = "vi_sensor";
    else
        name = "vi";

    clk = clk_get_sys(name, NULL);

    if (IS_ERR_OR_NULL(clk)) {
        pr_err("%s: failed to get struct clk %s\n", __func__, name);
        return NvSuccess;
    }

    if (PrefFreqListCount)
        rate = *PrefFreqList * 1000;
    else if (MaxFreq != NvRmFreqUnspecified)
        rate = MaxFreq * 1000;
    else if (MinFreq != NvRmFreqUnspecified)
        rate = MinFreq * 1000;
    else
        rate = INT_MAX;

    ret = clk_set_rate(clk, rate);
    if (ret) {
        pr_err("%s: err %d setting %s to %luHz\n", __func__, ret, name, rate);
        clk_put(clk);
        return NvError_BadParameter;
    }

    rate = clk_get_rate(clk);
    if (CurrentFreq)
        *CurrentFreq = (rate+500) / 1000;


    if (!(flags & NvRmClockConfig_SubConfig)) {
        void __iomem *car = IO_ADDRESS(TEGRA_CLK_RESET_BASE);
        u32 val;

        if ((flags & NvRmClockConfig_InternalClockForPads) &&
            (flags & NvRmClockConfig_ExternalClockForCore)) {
            pr_err("%s: invalid VI flag combination: %08x\n", __func__, flags);
            clk_put(clk);
            return NvError_BadParameter;
        }
        val = readl(car + clk->reg);  
        val &= ~(CLK_VI_CORE_EXTERNAL | CLK_VI_PAD_INTERNAL);
        if (flags & NvRmClockConfig_InternalClockForPads)
            val |= CLK_VI_PAD_INTERNAL;
        else if (flags & NvRmClockConfig_ExternalClockForCore)
            val |= CLK_VI_CORE_EXTERNAL;
        writel(val, car + clk->reg);
    }

    clk_put(clk);
    return NvSuccess;
}

NvError NvRmPowerModuleClockControl(
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID ModuleId,
    NvU32 ClientId,
    NvBool Enable)
{
    const char *vi_names[] = { "vi", "vi_sensor", "csus", NULL };
    const char *csi_names[] = { "csi", NULL };
    const char *isp_names[] = { "isp", NULL };
    const char **names = NULL;

    if (is_vi(ModuleId))
        names = vi_names;
    else if (is_csi(ModuleId))
        names = csi_names;
    else if (is_isp(ModuleId))
        names = isp_names;

    if (!names) {
        pr_err("%s: MOD[%lu] INST[%lu] not supported\n", __func__,
               NVRM_MODULE_ID_MODULE(ModuleId),
               NVRM_MODULE_ID_INSTANCE(ModuleId));
        return NvSuccess;
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
