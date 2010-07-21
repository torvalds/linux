/*
 * Copyright (c) 2009-2010 NVIDIA Corporation.
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

#include "nvcommon.h"
#include "nvos.h"
#include "nvassert.h"
#include "nvidlcmd.h"
#include "nvreftrack.h"
#include "nvrm_xpc.h"
#include "nvrm_transport.h"
#include "nvrm_memctrl.h"
#include "nvrm_pcie.h"
#include "nvrm_pwm.h"
#include "nvrm_keylist.h"
#include "nvrm_pmu.h"
#include "nvrm_diag.h"
#include "nvrm_pinmux.h"
#include "nvrm_analog.h"
#include "nvrm_owr.h"
#include "nvrm_i2c.h"
#include "nvrm_spi.h"
#include "nvrm_interrupt.h"
#include "nvrm_dma.h"
#include "nvrm_power.h"
#include "nvrm_gpio.h"
#include "nvrm_module.h"
#include "nvrm_memmgr.h"
#include "nvrm_init.h"
NvError nvrm_xpc_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_transport_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_memctrl_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_pcie_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_pwm_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_keylist_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_pmu_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_diag_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_pinmux_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_analog_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_owr_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_i2c_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_spi_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_interrupt_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_dma_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_power_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_gpio_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_module_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_memmgr_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );
NvError nvrm_init_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );

NvError nvrm_xpc_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_memctrl_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_pcie_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_pwm_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_keylist_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_pmu_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_diag_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_pinmux_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_analog_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_owr_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_i2c_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_spi_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_interrupt_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_dma_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_gpio_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_memmgr_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

NvError nvrm_init_Dispatch( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
	printk("NVRM: %s %d\n", __func__, function);
	return NvSuccess;
}

// NvRm Package
typedef enum
{
    NvRm_Invalid = 0,
    NvRm_nvrm_xpc,
    NvRm_nvrm_transport,
    NvRm_nvrm_memctrl,
    NvRm_nvrm_pcie,
    NvRm_nvrm_pwm,
    NvRm_nvrm_keylist,
    NvRm_nvrm_pmu,
    NvRm_nvrm_diag,
    NvRm_nvrm_pinmux,
    NvRm_nvrm_analog,
    NvRm_nvrm_owr,
    NvRm_nvrm_i2c,
    NvRm_nvrm_spi,
    NvRm_nvrm_interrupt,
    NvRm_nvrm_dma,
    NvRm_nvrm_power,
    NvRm_nvrm_gpio,
    NvRm_nvrm_module,
    NvRm_nvrm_memmgr,
    NvRm_nvrm_init,
    NvRm_Num,
    NvRm_Force32 = 0x7FFFFFFF,
} NvRm;

typedef NvError (* NvIdlDispatchFunc)( NvU32 function, void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx );

typedef struct NvIdlDispatchTableRec
{
    NvU32 PackageId;
    NvIdlDispatchFunc DispFunc;
} NvIdlDispatchTable;

static NvIdlDispatchTable gs_DispatchTable[] =
{
    { NvRm_nvrm_xpc, nvrm_xpc_Dispatch },
    { NvRm_nvrm_transport, nvrm_transport_Dispatch },
    { NvRm_nvrm_memctrl, nvrm_memctrl_Dispatch },
    { NvRm_nvrm_pcie, nvrm_pcie_Dispatch },
    { NvRm_nvrm_pwm, nvrm_pwm_Dispatch },
    { NvRm_nvrm_keylist, nvrm_keylist_Dispatch },
    { NvRm_nvrm_pmu, nvrm_pmu_Dispatch },
    { NvRm_nvrm_diag, nvrm_diag_Dispatch },
    { NvRm_nvrm_pinmux, nvrm_pinmux_Dispatch },
    { NvRm_nvrm_analog, nvrm_analog_Dispatch },
    { NvRm_nvrm_owr, nvrm_owr_Dispatch },
    { NvRm_nvrm_i2c, nvrm_i2c_Dispatch },
    { NvRm_nvrm_spi, nvrm_spi_Dispatch },
    { NvRm_nvrm_interrupt, nvrm_interrupt_Dispatch },
    { NvRm_nvrm_dma, nvrm_dma_Dispatch },
    { NvRm_nvrm_power, nvrm_power_Dispatch },
    { NvRm_nvrm_gpio, nvrm_gpio_Dispatch },
    { NvRm_nvrm_module, nvrm_module_Dispatch },
    { NvRm_nvrm_memmgr, nvrm_memmgr_Dispatch },
    { NvRm_nvrm_init, nvrm_init_Dispatch },
    { 0 },
};

NvError NvRm_Dispatch( void *InBuffer, NvU32 InSize, void *OutBuffer, NvU32 OutSize, NvDispatchCtx* Ctx )
{
    NvU32 packid_;
    NvU32 funcid_;
    NvIdlDispatchTable *table_;

    NV_ASSERT( InBuffer );
    NV_ASSERT( OutBuffer );

    packid_ = ((NvU32 *)InBuffer)[0];
    funcid_ = ((NvU32 *)InBuffer)[1];
    table_ = gs_DispatchTable;

    if ( packid_-1 >= NV_ARRAY_SIZE(gs_DispatchTable) ||
         !table_[packid_ - 1].DispFunc )
        return NvError_IoctlFailed;

    return table_[packid_ - 1].DispFunc( funcid_, InBuffer, InSize,
        OutBuffer, OutSize, Ctx );
}

