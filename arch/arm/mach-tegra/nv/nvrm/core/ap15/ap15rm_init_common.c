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

#include "nvcommon.h"
#include "nvos.h"
#include "nvutil.h"
#include "nvassert.h"
#include "nvrm_drf.h"
#include "nvrm_init.h"
#include "nvrm_rmctrace.h"
#include "nvrm_configuration.h"
#include "nvrm_chiplib.h"
#include "nvrm_pmu_private.h"
#include "nvrm_processor.h"
#include "nvrm_structure.h"
#include "ap15rm_private.h"
#include "ap15rm_private.h"
#include "ap15rm_clocks.h"
#include "nvodm_query.h"
#include "nvodm_query_pins.h"
#include "common/nvrm_hwintf.h"
#include "nvrm_pinmux_utils.h"
#include "nvrm_minikernel.h"
#include "ap15/arapb_misc.h" // chipid, has to be the same for all chips
#include "ap15/arapbpm.h"
#include "ap15/arfuse.h"

extern NvRmCfgMap g_CfgMap[];

void NvRmPrivMemoryInfo( NvRmDeviceHandle hDevice );
void NvRmPrivReadChipId( NvRmDeviceHandle rm );
void NvRmPrivGetSku( NvRmDeviceHandle rm );
/** Returns the pointer to the relocation table */
NvU32 *NvRmPrivGetRelocationTable( NvRmDeviceHandle hDevice );
NvError NvRmPrivMapApertures( NvRmDeviceHandle rm );
void NvRmPrivUnmapApertures( NvRmDeviceHandle rm );
NvU32 NvRmPrivGetBctCustomerOption(NvRmDeviceHandle hRm);

NvRmCfgMap g_CfgMap[] =
{
    { "NV_CFG_RMC_FILE", NvRmCfgType_String, (void *)"",
        STRUCT_OFFSET(RmConfigurationVariables, RMCTraceFileName) },

    /* don't need chiplib for non-sim builds */
    { "NV_CFG_CHIPLIB", NvRmCfgType_String, (void *)"",
        STRUCT_OFFSET(RmConfigurationVariables, Chiplib) },

    { "NV_CFG_CHIPLIB_ARGS", NvRmCfgType_String, (void *)"",
        STRUCT_OFFSET(RmConfigurationVariables, ChiplibArgs) },

    { 0 }
};

NvRmModuleTable *
NvRmPrivGetModuleTable(
    NvRmDeviceHandle hDevice )
{
    return &hDevice->ModuleTable;
}

NvU32 *
NvRmPrivGetRelocationTable( NvRmDeviceHandle hDevice )
{
    switch( hDevice->ChipId.Id ) {
    case 0x15:
        return NvRmPrivAp15GetRelocationTable( hDevice );
    case 0x16:
        return NvRmPrivAp16GetRelocationTable( hDevice );
    case 0x20:
        return NvRmPrivAp20GetRelocationTable( hDevice );
    default:
        NV_ASSERT(!"Invalid Chip" );
        return 0;
    }
}

void
NvRmPrivReadChipId( NvRmDeviceHandle rm )
{
#if (NVCPU_IS_X86 && NVOS_IS_WINDOWS)
    NvRmChipId *id;
    NV_ASSERT( rm );

    id = &rm->ChipId;

    id->Family = NvRmChipFamily_HandheldSoc;
    id->Id = 0x15;
    id->Major = 0x0;
    id->Minor = 0x0;
    id->SKU = 0x0;
    id->Netlist = 0x0;
    id->Patch = 0x0;
#else
    NvU32 reg;
    NvRmChipId *id;
    NvU32 fam;
    char *s;
    NvU8 *VirtAddr;
    NvError e;

    NV_ASSERT( rm );
    id = &rm->ChipId;

    /* Hard coding the address of the chip ID address space, as we haven't yet
     * parsed the relocation table.
     */
    e = NvRmPhysicalMemMap(0x70000000, 0x1000, NVOS_MEM_READ_WRITE,
        NvOsMemAttribute_Uncached, (void **)&VirtAddr);
    if (e != NvSuccess)
    {
        NV_DEBUG_PRINTF(("APB misc aperture map failure\n"));
        return;
    }

    /* chip id is in the misc aperture */
    reg = NV_READ32( VirtAddr + APB_MISC_GP_HIDREV_0 );
    id->Id = (NvU16)NV_DRF_VAL( APB_MISC_GP, HIDREV, CHIPID, reg );
    id->Major = (NvU8)NV_DRF_VAL( APB_MISC_GP, HIDREV, MAJORREV, reg );
    id->Minor = (NvU8)NV_DRF_VAL( APB_MISC_GP, HIDREV, MINORREV, reg );

    fam = NV_DRF_VAL( APB_MISC_GP, HIDREV, HIDFAM, reg );
    switch( fam ) {
    case APB_MISC_GP_HIDREV_0_HIDFAM_GPU:
        id->Family = NvRmChipFamily_Gpu;
        s = "GPU";
        break;
    case APB_MISC_GP_HIDREV_0_HIDFAM_HANDHELD:
        id->Family = NvRmChipFamily_Handheld;
        s = "Handheld";
        break;
    case APB_MISC_GP_HIDREV_0_HIDFAM_BR_CHIPS:
        id->Family = NvRmChipFamily_BrChips;
        s = "BrChips";
        break;
    case APB_MISC_GP_HIDREV_0_HIDFAM_CRUSH:
        id->Family = NvRmChipFamily_Crush;
        s = "Crush";
        break;
    case APB_MISC_GP_HIDREV_0_HIDFAM_MCP:
        id->Family = NvRmChipFamily_Mcp;
        s = "MCP";
        break;
    case APB_MISC_GP_HIDREV_0_HIDFAM_CK:
        id->Family = NvRmChipFamily_Ck;
        s = "Ck";
        break;
    case APB_MISC_GP_HIDREV_0_HIDFAM_VAIO:
        id->Family = NvRmChipFamily_Vaio;
        s = "Vaio";
        break;
    case APB_MISC_GP_HIDREV_0_HIDFAM_HANDHELD_SOC:
        id->Family = NvRmChipFamily_HandheldSoc;
        s = "Handheld SOC";
        break;
    default:
        NV_ASSERT( !"bad chip family" );
        NvRmPhysicalMemUnmap(VirtAddr, 0x1000);
        return;
    }

    reg = NV_READ32( VirtAddr + APB_MISC_GP_EMU_REVID_0 );
    id->Netlist = (NvU16)NV_DRF_VAL( APB_MISC_GP, EMU_REVID, NETLIST, reg );
    id->Patch = (NvU16)NV_DRF_VAL( APB_MISC_GP, EMU_REVID, PATCH, reg );

    if( id->Major == 0 )
    {
        char *emu;
        if( id->Netlist == 0 )
        {
            NvOsDebugPrintf( "Simulation Chip: 0x%x\n", id->Id );
        }
        else
        {
            if( id->Minor == 0 )
            {
                emu = "QuickTurn";
            }
            else
            {
                emu = "FPGA";
            }

            NvOsDebugPrintf( "Emulation (%s) Chip: 0x%x Netlist: 0x%x "
                "Patch: 0x%x\n", emu, id->Id, id->Netlist, id->Patch );
        }
    }
    else
    {
        // on real silicon

        NvRmPrivGetSku( rm );

        NvOsDebugPrintf( "Chip Id: 0x%x (%s) Major: 0x%x Minor: 0x%x "
            "SKU: 0x%x\n", id->Id, s, id->Major, id->Minor, id->SKU );
    }

    // add a sanity check here, so that if we think we are on sim, but don't
    // detect a sim/quickturn netlist bail out with an error
    if ( NvRmIsSimulation() && id->Major != 0 )
    {
        // this should all get optimized away in release builds because the
        // above will get evaluated to if ( 0 )
        NV_ASSERT(!"invalid major version number for simulation");
    }
    NvRmPhysicalMemUnmap(VirtAddr, 0x1000);
#endif
}

void
NvRmPrivGetSku( NvRmDeviceHandle rm )
{
    NvError e;
    NvRmChipId *id;
    NvU8 *FuseVirt;
    NvU32 reg;

#if NV_USE_FUSE_CLOCK_ENABLE
    NvU8 *CarVirt = 0;
#endif

    NV_ASSERT( rm );
    id = &rm->ChipId;

#if NV_USE_FUSE_CLOCK_ENABLE
    // Enable fuse clock
    e = NvRmPhysicalMemMap(0x60006000, 0x1000, NVOS_MEM_READ_WRITE,
        NvOsMemAttribute_Uncached, (void **)&CarVirt);
    if (e == NvSuccess)
    {
       reg = NV_READ32(CarVirt + CLK_RST_CONTROLLER_CLK_OUT_ENB_H_0);
       reg |= 0x80;
       NV_WRITE32(CarVirt + CLK_RST_CONTROLLER_CLK_OUT_ENB_H_0, reg);
    }
#endif

    /* Read the fuse only on real silicon, as it was not gauranteed to be
     * preset on the eluation/simulation platforms.
     */
    e = NvRmPhysicalMemMap(0x7000f800, 0x400, NVOS_MEM_READ_WRITE,
        NvOsMemAttribute_Uncached, (void **)&FuseVirt);
    if (e == NvSuccess)
    {
        // Read the SKU from the fuse module.
        reg = NV_READ32( FuseVirt + FUSE_SKU_INFO_0 );
        id->SKU = (NvU16)reg;
        NvRmPhysicalMemUnmap(FuseVirt, 0x400);

#if NV_USE_FUSE_CLOCK_ENABLE
        // Disable fuse clock
        if (CarVirt)
        {
            reg = NV_READ32(CarVirt + CLK_RST_CONTROLLER_CLK_OUT_ENB_H_0);
            reg &= ~0x80;
            NV_WRITE32(CarVirt + CLK_RST_CONTROLLER_CLK_OUT_ENB_H_0, reg);
            NvRmPhysicalMemUnmap(CarVirt, 0x1000);
        }
#endif
    } else
    {
        NV_ASSERT(!"Cannot map the FUSE aperture to get the SKU");
        id->SKU = 0;
    }
}

NvError
NvRmPrivMapApertures( NvRmDeviceHandle rm )
{
    NvRmModuleTable *tbl;
    NvRmModuleInstance *inst;
    NvRmModule *mod;
    NvU32 devid;
    NvU32 i;
    NvError e;

    NV_ASSERT( rm );

    /* loop over the instance list and map everything */
    tbl = &rm->ModuleTable;
    mod = tbl->Modules;
    for( i = 0; i < NvRmPrivModuleID_Num; i++ )
    {
        if( mod[i].Index == NVRM_MODULE_INVALID )
        {
            continue;
        }

        if ((i != NvRmPrivModuleID_Ahb_Arb_Ctrl ) &&
            (i != NvRmPrivModuleID_ApbDma ) &&
            (i != NvRmPrivModuleID_ApbDmaChannel ) &&
            (i != NvRmPrivModuleID_ClockAndReset ) &&
            (i != NvRmPrivModuleID_ExternalMemoryController ) &&
            (i != NvRmPrivModuleID_Gpio ) &&
            (i != NvRmPrivModuleID_Interrupt ) &&
            (i != NvRmPrivModuleID_InterruptArbGnt ) &&
            (i != NvRmPrivModuleID_InterruptDrq ) &&
            (i != NvRmPrivModuleID_MemoryController ) &&
            (i != NvRmModuleID_Misc) &&
            (i != NvRmPrivModuleID_ArmPerif) &&
            (i != NvRmModuleID_3D) &&
            (i != NvRmModuleID_CacheMemCtrl ) &&
            (i != NvRmModuleID_Display) &&
            (i != NvRmModuleID_Dvc) &&
            (i != NvRmModuleID_FlowCtrl ) &&
            (i != NvRmModuleID_Fuse ) &&
            (i != NvRmModuleID_GraphicsHost ) &&
            (i != NvRmModuleID_I2c) &&
            (i != NvRmModuleID_Isp) &&
            (i != NvRmModuleID_Mpe) &&
            (i != NvRmModuleID_Pmif ) &&
            (i != NvRmModuleID_Mipi ) &&
            (i != NvRmModuleID_ResourceSema ) &&
            (i != NvRmModuleID_SysStatMonitor ) &&
            (i != NvRmModuleID_TimerUs ) &&
            (i != NvRmModuleID_Vde ) &&
            (i != NvRmModuleID_ExceptionVector ) &&
            (i != NvRmModuleID_Usb2Otg ) &&
            (i != NvRmModuleID_Vi)
            )
        {
            continue;
        }

        /* FIXME If the multiple instances of the same module is adjacent to
         * each other then we can do one allocation for all those modules.
         */

        /* map all of the device instances */
        inst = tbl->ModInst + mod[i].Index;
        devid = inst->DeviceId;
        while( devid == inst->DeviceId )
        {
            /* If this is a device that actually has an aperture... */
            if (inst->PhysAddr)
            {
                e = NvRmPhysicalMemMap(
                        inst->PhysAddr, inst->Length, NVOS_MEM_READ_WRITE,
                        NvOsMemAttribute_Uncached, &inst->VirtAddr);
                if (e != NvSuccess)
                {
                    NV_DEBUG_PRINTF(("Device %d at physical addr 0x%X has no "
                        "virtual mapping\n", devid, inst->PhysAddr));
                    return e;
                }
            }

            inst++;
        }
    }

    return NvSuccess;
}

void
NvRmPrivUnmapApertures( NvRmDeviceHandle rm )
{
    NvRmModuleTable *tbl;
    NvRmModuleInstance *inst;
    NvRmModule *mod;
    NvU32 devid;
    NvU32 i;

    NV_ASSERT( rm );

    /* loop over the instance list and unmap everything */
    tbl = &rm->ModuleTable;
    mod = tbl->Modules;
    for( i = 0; i < NvRmPrivModuleID_Num; i++ )
    {
        if( mod[i].Index == NVRM_MODULE_INVALID )
        {
            continue;
        }

        /* map all of the device instances */
        inst = tbl->ModInst + mod[i].Index;
        devid = inst->DeviceId;
        while( devid == inst->DeviceId )
        {
            NvRmPhysicalMemUnmap( inst->VirtAddr, inst->Length );
            inst++;
        }
    }
}

NvU32
NvRmPrivGetBctCustomerOption(NvRmDeviceHandle hRm)
{
    if (!NvRmIsSimulation())
    {
        return NV_REGR(hRm, NvRmModuleID_Pmif, 0, APBDEV_PMC_SCRATCH20_0);
    }
    else
    {
        return 0;
    }
}

NvRmChipId *
NvRmPrivGetChipId(
    NvRmDeviceHandle hDevice )
{
    return &hDevice->ChipId;
}

#if !NV_OAL
void NvRmBasicInit(NvRmDeviceHandle * pHandle)
{
    NvRmDevice *rm = 0;
    NvError err;
    NvU32 *table = 0;
    NvU32 BctCustomerOption = 0;

    *pHandle = 0;
    rm = NvRmPrivGetRmDeviceHandle();

    if( rm->bBasicInit )
    {
        *pHandle = rm;
        return; 
    }

    /* get the default configuration */
    err = NvRmPrivGetDefaultCfg( g_CfgMap, &rm->cfg );
    if( err != NvSuccess )
    {
        goto fail;
    }

    /* get the requested configuration */
    err = NvRmPrivReadCfgVars( g_CfgMap, &rm->cfg );
    if( err != NvSuccess )
    {
        goto fail;
    }

    /* Read the chip Id and store in the Rm structure. */
    NvRmPrivReadChipId( rm );

    // init the module control (relocation table, resets, etc.)
    table = NvRmPrivGetRelocationTable( rm );
    if( !table )
    {
        goto fail;
    }

    err = NvRmPrivModuleInit( &rm->ModuleTable, table );
    if( err != NvSuccess )
    {
        goto fail;
    }

    NvRmPrivMemoryInfo( rm );

    // setup the hw apertures
    err = NvRmPrivMapApertures( rm );
    if( err != NvSuccess )
    {
        goto fail;
    }

    BctCustomerOption = NvRmPrivGetBctCustomerOption(rm);
    err = NvRmPrivInitKeyList(rm, &BctCustomerOption, 1);
    if (err != NvSuccess)
    {
        goto fail;
    }

    // Now populate the logical interrupt table.
    NvRmPrivInterruptTableInit( rm );

    rm->bBasicInit = NV_TRUE;
    // basic init is a super-set of preinit
    rm->bPreInit = NV_TRUE;
    *pHandle = rm;

fail:
    return;
}

void
NvRmBasicClose(NvRmDeviceHandle handle)
{
    if (!NVOS_IS_WINDOWS_X86)
    {
        NvRmPrivDeInitKeyList(handle);
        /* unmap the apertures */
        NvRmPrivUnmapApertures( handle );
        /* deallocate the instance table */
        NvRmPrivModuleDeinit( &handle->ModuleTable );
    }
}
#endif
