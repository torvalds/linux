
#define NV_IDL_IS_STUB

/*
 * Copyright (c) 2009 NVIDIA Corporation.  All rights reserved.
 *
 * NVIDIA Corporation and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA Corporation is strictly prohibited.
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <mach/iomap.h>
#include "nvcommon.h"
#include "nvrm_module.h"

NvError NvRmModuleGetCapabilities( NvRmDeviceHandle hDeviceHandle,
    NvRmModuleID Module, NvRmModuleCapability * pCaps, NvU32 NumCaps,
    void * * Capability )
{
	NvU32 major = 0, minor = 0;
	unsigned i;

        switch (NVRM_MODULE_ID_MODULE(Module)) {
        case NvRmModuleID_Mpe:
		major = 1;
		minor = 2;
                break;

	case NvRmModuleID_BseA:
		major = 1;
		minor = 1;
		break;

        case NvRmModuleID_Display:
		major = 1;
		minor = 3;
                break;

	case NvRmModuleID_Spdif:
		major = 1;
		minor = 0;
		break;

	case NvRmModuleID_I2s:
		major = 1;
		minor = 1;
		break;

	case NvRmModuleID_Misc:
		major = 2;
		minor = 0;
		break;

	case NvRmModuleID_Vde:
		major = 1;
		minor = 2;
		break;

	case NvRmModuleID_Isp:
		major = 1;
		minor = 0;
		break;

	case NvRmModuleID_Vi:
		major = 1;
		minor = 1;
		break;

	case NvRmModuleID_3D:
		major = 1;
		minor = 2;
		break;

	case NvRmModuleID_2D:
		major = 1;
		minor = 1;
                break;

        default:
                printk("%s module %d not implemented\n", __func__, Module);
        }

	for (i=0; i<NumCaps; i++) {
		if (pCaps[i].MajorVersion==major &&
		    pCaps[i].MinorVersion==minor) {
			*Capability = pCaps[i].Capability;
			return NvSuccess;
		}
	}

        return NvError_NotSupported;
}

NvU32 NvRmModuleGetNumInstances( NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID Module )
{
        switch (Module) {
	case NvRmModuleID_I2s:
		return 4;

        case NvRmModuleID_Display:
                return 2;

	case NvRmModuleID_3D:
	case NvRmModuleID_Avp:
	case NvRmModuleID_GraphicsHost:
	case NvRmModuleID_Vcp:
	case NvRmModuleID_Isp:
	case NvRmModuleID_Vi:
	case NvRmModuleID_Epp:
        case NvRmModuleID_2D:
	case NvRmModuleID_Spdif:
	case NvRmModuleID_Vde:
	case NvRmModuleID_Mpe:
	case NvRmModuleID_Hdcp:
	case NvRmModuleID_Hdmi:
	case NvRmModuleID_Tvo:
	case NvRmModuleID_Dsi:
	case NvRmModuleID_BseA:
                return 1;

        default:
                printk("%s module %d not implemented\n", __func__, Module);
                return 1;
        }
}

void NvRmModuleGetBaseAddress( NvRmDeviceHandle hRmDeviceHandle, NvRmModuleID Module, NvRmPhysAddr * pBaseAddress, NvU32 * pSize )
{
	switch (NVRM_MODULE_ID_MODULE(Module)) {
	case NvRmModuleID_GraphicsHost:
		*pBaseAddress = 0x50000000;
		*pSize = 144 * 1024;
		break;
	case NvRmModuleID_Display:
		*pBaseAddress = 0x54200000 + (NVRM_MODULE_ID_INSTANCE(Module))*0x40000;
		*pSize = 256 * 1024;
		break;

	case NvRmModuleID_Vi:
		*pBaseAddress = 0x54080000;
		*pSize = 256 * 1024;
		break;

	case NvRmModuleID_Dsi:
		*pBaseAddress = 0x54300000;
		*pSize = 256 * 1024;
		break;

	default:
		*pBaseAddress = 0x0000000;
		*pSize = 00;
		printk("%s module %d not implemented\n", __func__, Module);
	}
	printk("%s module %d 0x%08x x %dK\n", __func__, Module, *pBaseAddress, *pSize / 1024);
}

void NvRmModuleReset(NvRmDeviceHandle hRmDevice, NvRmModuleID Module)
{
    void __iomem *clk_rst = IO_ADDRESS(TEGRA_CLK_RESET_BASE);
    if (NVRM_MODULE_ID_MODULE(Module) != NvRmModuleID_Avp ||
        NVRM_MODULE_ID_INSTANCE(Module) != 0) {
        printk("%s MOD[%lu] INST[%lu] not implemented\n", __func__,
               NVRM_MODULE_ID_MODULE(Module),
               NVRM_MODULE_ID_INSTANCE(Module));
        return;
    }

    writel(1<<1, clk_rst + 0x300);
    udelay(10);
    writel(1<<1, clk_rst + 0x304);
}
