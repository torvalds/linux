/*
 * Copyright (c) 2007-2010 NVIDIA Corporation.
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
#include "nvrm_xpc.h"
#include "ap15rm_private.h"
#include "nvrm_structure.h"
#include "ap15rm_private.h"
#include "ap15rm_clocks.h"
#include "nvodm_query.h"
#include "nvodm_query_pins.h"
#include "common/nvrm_hwintf.h"
#include "ap15/armc.h"
#include "ap15/aremc.h"
#include "ap15/project_relocation_table.h"
#include "ap15/arapb_misc.h"
#include "ap15/arapbpm.h"
#include "nvrm_pinmux_utils.h"
#include "ap15/arfuse.h"
#include "nvbootargs.h"

static NvRmDevice gs_Rm;

extern NvRmCfgMap g_CfgMap[];

void NvRmPrivMemoryInfo( NvRmDeviceHandle hDevice );
extern NvError NvRmPrivMapApertures( NvRmDeviceHandle rm );
extern void NvRmPrivUnmapApertures( NvRmDeviceHandle rm );
extern NvError NvRmPrivPwmInit(NvRmDeviceHandle hRm);
extern void NvRmPrivPwmDeInit(NvRmDeviceHandle hRm);
extern NvU32 NvRmPrivGetBctCustomerOption(NvRmDeviceHandle hRm);
extern void NvRmPrivReadChipId( NvRmDeviceHandle rm );
extern NvU32 *NvRmPrivGetRelocationTable( NvRmDeviceHandle hDevice );
extern NvError NvRmPrivPcieOpen(NvRmDeviceHandle hDeviceHandle);
extern void NvRmPrivPcieClose(NvRmDeviceHandle hDeviceHandle);
static void NvRmPrivInitPinAttributes(NvRmDeviceHandle rm);
static void NvRmPrivBasicReset( NvRmDeviceHandle rm );
static NvError NvRmPrivMcErrorMonitorStart( NvRmDeviceHandle rm );
static void NvRmPrivMcErrorMonitorStop( NvRmDeviceHandle rm );

#if !NV_OAL
/* This function sets some performance timings for Mc & Emc.  Numbers are from
 * the Arch team.
 */
static void
NvRmPrivSetupMc(NvRmDeviceHandle hRm)
{
    switch (hRm->ChipId.Id) {
    case 0x15:
    case 0x16:
        NvRmPrivAp15SetupMc(hRm);
        break;
    case 0x20:
        NvRmPrivAp20SetupMc(hRm);
        break;
    default:
        NV_ASSERT(!"Unsupported chip ID");
        break;
    }
}
#endif

NvError
NvRmOpen(NvRmDeviceHandle *pHandle, NvU32 DeviceId ) {
    return NvRmOpenNew(pHandle);
}

void NvRmPrivReadChipId( NvRmDeviceHandle rm )
{
	NvRmChipId *id;
	u32 reg, fam;

	id = &rm->ChipID;

	reg = readl(IO_TO_VIRT(TEGRA_APB_MISC_BASE) + 0x804);
	id->Id = (reg >> 8) & 0xff;
	id->Major = (reg >> 4) & 0xf;
	id->Minor = (reg >> 16) & 0xf;

	fam = reg & 0xf;

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
}



void NvRmInit(
    NvRmDeviceHandle * pHandle )
{
    NvU32 *table = 0;
    NvRmDevice *rm = 0;
    rm = &gs_Rm;

    if( rm->bPreInit )
    {
        return;
    }

    /* Read the chip Id and store in the Rm structure. */
    NvRmPrivReadChipId( rm );

    /* parse the relocation table */
//    table = NvRmPrivGetRelocationTable( rm );
//    NV_ASSERT(table != NULL);

//    NV_ASSERT_SUCCESS(NvRmPrivModuleInit( &rm->ModuleTable, table ));
//    NvRmPrivMemoryInfo( rm );

//    NvRmPrivInterruptTableInit( rm );

    rm->bPreInit = NV_TRUE;
    *pHandle = rm;

    return;
}

NvError
NvRmOpenNew(NvRmDeviceHandle *pHandle)
{
    NvError err;
    NvRmDevice *rm = 0;
    NvU32 *table = 0;

    NvU32 BctCustomerOption = 0;
    NvU64 Uid = 0;

    NvOsMutexHandle rmMutex = NULL;

    /* open the nvos trace file */
    NVOS_TRACE_LOG_START;

    // OAL does not support these mutexes
    if (gs_Rm.mutex == NULL)
    {
        err = NvOsMutexCreate(&rmMutex);
        if (err != NvSuccess)
            return err;

        if (NvOsAtomicCompareExchange32((NvS32*)&gs_Rm.mutex, 0,
                (NvS32)rmMutex) != 0)
            NvOsMutexDestroy(rmMutex);
    }

    NvOsMutexLock(gs_Rm.mutex);
    rm = &gs_Rm;

    if(rm->refcount )
    {
        rm->refcount++;
        *pHandle = rm;
        NvOsMutexUnlock(gs_Rm.mutex);
        return NvSuccess;
    }

    rmMutex = gs_Rm.mutex;
    gs_Rm.mutex = rmMutex;

    // create the memmgr mutex
    err = NvOsMutexCreate(&rm->MemMgrMutex);
    if (err)
        goto fail;

    // create mutex for the clock and reset r-m-w top level registers access
    err = NvOsMutexCreate(&rm->CarMutex);
    if (err)
        goto fail;

    /* NvRmOpen needs to be re-entrant to allow I2C, GPIO and KeyList ODM
     * services to be available to the ODM query.  Therefore, the refcount is
     * bumped extremely early in initialization, and if any initialization
     * fails the refcount is reset to 0.
     */
    rm->refcount = 1;

#if 0
    if( !rm->bBasicInit )
    {
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
    }
#endif

#if 0
    /* start chiplib */
    if (rm->cfg.Chiplib[0] != '\0')
    {
        err = NvRmPrivChiplibStartup( rm->cfg.Chiplib, rm->cfg.ChiplibArgs,
            NULL );
        if( err != NvSuccess )
        {
            goto fail;
        }
    }

    /* open the RMC file */
    err = NvRmRmcOpen( rm->cfg.RMCTraceFileName, &rm->rmc );
    if( err != NvSuccess )
    {
        goto fail;
    }

    if( !rm->bPreInit )
    {
        /* Read the chip Id and store in the Rm structure. */
        NvRmPrivReadChipId( rm );

        /* parse the relocation table */
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

        // Now populate the logical interrupt table.
        NvRmPrivInterruptTableInit( rm );
    }

    if( !rm->bBasicInit && !NVOS_IS_WINDOWS_X86 )
    {
        err = NvRmPrivMapApertures( rm );
        if( err != NvSuccess )
        {
            goto fail;
        }

        // Initializing the ODM-defined key list
        //  This gets initialized first, since the RMs calls into
        //  the ODM query may result in the ODM query calling
        //  back into the RM to get this value!
        BctCustomerOption = NvRmPrivGetBctCustomerOption(rm);
        err = NvRmPrivInitKeyList(rm, &BctCustomerOption, 1);
        if (err != NvSuccess)
        {
            goto fail;
        }
    }

    // prevent re-inits
    rm->bBasicInit = NV_TRUE;
    rm->bPreInit = NV_TRUE;


    if (!NVOS_IS_WINDOWS_X86)
    {
        NvRmPrivCheckBondOut( rm );

        /* bring modules out of reset */
        NvRmPrivBasicReset( rm );

        /* initialize power manager before any other module that may access
         * clock or voltage resources
         */
        err = NvRmPrivPowerInit(rm);
        if( err != NvSuccess )
        {
            goto fail;
        }

        NvRmPrivInterruptStart( rm );

        // Initializing pins attributes
        NvRmPrivInitPinAttributes(rm);

        // Initialize RM pin-mux (init's the state of internal shadow
        // register variables)
        NvRmInitPinMux(rm, NV_TRUE);

        // Initalize the module clocks.
        err = NvRmPrivClocksInit( rm );
        if( err != NvSuccess )
        {
            goto fail;
        }
    }
#endif

#ifdef GHACK
    if (!NVOS_IS_WINDOWS_X86)
    {
        // FIXME: this crashes in simulation
        // Enabling only for the non simulation modes.
        if ((rm->ChipId.Major == 0) && (rm->ChipId.Netlist == 0))
        {
            // this is the csim case, so we don't do this here.
        }
        else
        {
            // Initializing the dma.
            err = NvRmPrivDmaInit(rm);
            if( err != NvSuccess )
            {
                goto fail;
            }

            // Initializing the Spi and Slink.
            err = NvRmPrivSpiSlinkInit(rm);
            if( err != NvSuccess )
            {
                goto fail;
            }

            //  Complete pin mux initialization
            NvRmInitPinMux(rm, NV_FALSE);

            // Initializing the dfs
            err = NvRmPrivDfsInit(rm);
            if( err != NvSuccess )
            {
                goto fail;
            }
        }

        // Initializing the Pwm
        err = NvRmPrivPwmInit(rm);
        if (err != NvSuccess)
        {
            goto fail;
        }

        // PMU interface init utilizes ODM services that reenter NvRmOpen().
        // Therefore, it shall be performed after refcount is set so that
        // reentry has no side-effects except bumping refcount. The latter
        // is reset below so that RM can be eventually closed.
        err = NvRmPrivPmuInit(rm);
        if( err != NvSuccess )
        {
            goto fail;
        }

        // set the mc & emc tuning parameters
        NvRmPrivSetupMc(rm);
        if (!NvRmIsSimulation())
        {
            // Configure PLL rails, boost core power and clocks
            // Initialize and start temperature monitoring
            NvRmPrivPllRailsInit(rm);
            NvRmPrivBoostClocks(rm);
            NvRmPrivDttInit(rm);
        }

        if (0)  /* FIXME Don't enable PCI yet */
        {
            err = NvRmPrivPcieOpen( rm );
            if (err != NvSuccess && err != NvError_ModuleNotPresent)
            {
                goto fail;
            }
        }
        // Asynchronous interrupts must be disabled until the very end of
        // RmOpen. They can be enabled just before releasing rm mutex after
        // completion of all initialization calls.
        NvRmPrivPmuInterruptEnable(rm);

        // Start Memory Controller Error monitoring.
        err = NvRmPrivMcErrorMonitorStart(rm);
        if( err != NvSuccess )
        {
            goto fail;
        }

        // WAR for bug 600821
        if ((rm->ChipId.Id == 0x20) && 
            (rm->ChipId.Major == 0x1) && (rm->ChipId.Minor == 0x2))
        {
            err = NvRmQueryChipUniqueId(rm, sizeof (NvU64), &Uid);
            if ((Uid>>32) == 0x08080105)
            {
                NV_REGW(rm, NvRmModuleID_Pmif, 0, 0xD0, 0xFFFFFFEF);
            }
        }
    }
    err = NvRmXpcInitArbSemaSystem(rm);
    if( err != NvSuccess )
    {
        goto fail;
    }
#endif

    /* assign the handle pointer */
    *pHandle = rm;

    NvOsMutexUnlock(gs_Rm.mutex);
    return NvSuccess;

fail:
    // FIXME: free rm if it becomes dynamically allocated
    // BUG:  there are about ten places that we go to fail, and we make no
    // effort here to clean anything up.
    NvOsMutexUnlock(gs_Rm.mutex);
    NV_DEBUG_PRINTF(("RM init failed\n"));
    rm->refcount = 0;
    return err;
}

void
NvRmClose(NvRmDeviceHandle handle)
{
    if( !handle )
    {
        return;
    }

    NV_ASSERT( handle->mutex );

    /* decrement refcount */
    NvOsMutexLock( handle->mutex );
    handle->refcount--;

    /* do deinit if refcount is zero */
    if( handle->refcount == 0 )
    {
#ifdef GHACK
        if (!NVOS_IS_WINDOWS_X86)
        {
            // PMU and DTT deinit through ODM services reenters NvRmClose().
            // The refcount will wrap around and this will be the only reentry
            // side-effect, which is compensated after deint exit.
            NvRmPrivDttDeinit();
            handle->refcount = 0;
            NvRmPrivPmuDeinit(handle);
            handle->refcount = 0;

            if (0)  /* FIXME Don't enable PCIE yet */
            {
                NvRmPrivPcieClose( handle );
            }
        }

        if (!NVOS_IS_WINDOWS_X86)
        {
            /* disable modules */
            // Enabling only for the non simulation modes.
            if ((handle->ChipId.Major == 0) && (handle->ChipId.Netlist == 0))
            {
                // this is the csim case, so we don't do this here.
            }
            else
            {
                NvRmPrivDmaDeInit();

                NvRmPrivSpiSlinkDeInit();

                NvRmPrivDfsDeinit(handle);
            }

            /* deinit clock manager */
            NvRmPrivClocksDeinit(handle);

            /* deinit power manager */
            NvRmPrivPowerDeinit(handle);

            NvRmPrivDeInitKeyList(handle);
            NvRmPrivPwmDeInit(handle);
            // Stop Memory controller error monitoring.
            NvRmPrivMcErrorMonitorStop(handle);

            /* if anyone left an interrupt registered, this will clear it. */
            NvRmPrivInterruptShutdown(handle);

            /* unmap the apertures */
            NvRmPrivUnmapApertures( handle );

            if (NvRmIsSimulation())
                NvRmPrivChiplibShutdown();

        }
#endif
        NvRmRmcClose( &handle->rmc );

        /* deallocate the instance table */
//        NvRmPrivModuleDeinit( &handle->ModuleTable );

        /* free up the CAR mutex */
        NvOsMutexDestroy(handle->CarMutex);

        /* free up the memmgr mutex */
        NvOsMutexDestroy(handle->MemMgrMutex);

        /* close the nvos trace file */
        NVOS_TRACE_LOG_END;
    }
    NvOsMutexUnlock( handle->mutex );

#if NVOS_IS_WINDOWS && !NVOS_IS_WINDOWS_CE
    if( handle->refcount == 0 )
    {
        NvOsMutexDestroy(handle->mutex);
        gs_Rm.mutex = 0;
    }
#endif
}

#ifdef GHACK
void
NvRmPrivMemoryInfo( NvRmDeviceHandle hDevice )
{
    NvRmModuleTable *tbl;
    NvRmModuleInstance *inst;

    tbl = &hDevice->ModuleTable;

    /* Get External memory module info */
    inst = tbl->ModInst +
        (tbl->Modules)[NvRmPrivModuleID_ExternalMemory].Index;

    hDevice->ExtMemoryInfo.base = inst->PhysAddr;
    hDevice->ExtMemoryInfo.size = inst->Length;

    /* Get Iram Memory Module Info .Special handling since iram has 4 banks
     * and each has a different instance in the relocation table
     */

    inst = tbl->ModInst + (tbl->Modules)[NvRmPrivModuleID_Iram].Index;
    hDevice->IramMemoryInfo.base = inst->PhysAddr;
    hDevice->IramMemoryInfo.size = inst->Length;

    inst++;
    // Below loop works assuming that relocation table parsing compacted
    // scattered multiple instances into sequential list
    while(NvRmPrivDevToModuleID(inst->DeviceId) == NvRmPrivModuleID_Iram)
    {
        // The IRAM banks are contigous address of memory. Cannot handle
        // non-contigous memory for now
        NV_ASSERT(hDevice->IramMemoryInfo.base +
            hDevice->IramMemoryInfo.size == inst->PhysAddr);

        hDevice->IramMemoryInfo.size += inst->Length;
        inst++;
    }

}

#endif

NvError
NvRmGetRmcFile( NvRmDeviceHandle hDevice, NvRmRmcFile **file )
{
    NV_ASSERT(hDevice);

    *file = &hDevice->rmc;
    return NvSuccess;
}

NvRmDeviceHandle NvRmPrivGetRmDeviceHandle()
{
    return &gs_Rm;
}

#ifdef GHACK
/**
 * Initializes pins attributes
 * @param hRm The RM device handle
 */
static void
NvRmPrivInitPinAttributes(NvRmDeviceHandle rm)
{
    NvU32 Count = 0, Offset = 0, Value = 0;
    NvU32 Major = 0;
    NvU32 Minor = 0;
    NvOdmPinAttrib *pPinAttribTable = NULL;
    NvRmModuleCapability caps[4];
    NvRmModuleCapability *pCap = NULL;

    NV_ASSERT( rm );

    NvOsMemset(caps, 0, sizeof(caps));

    caps[0].MajorVersion = 1;
    caps[0].MinorVersion = 0;
    caps[0].EcoLevel = 0;
    caps[0].Capability = &caps[0];

    caps[1].MajorVersion = 1;
    caps[1].MinorVersion = 1;
    caps[1].EcoLevel = 0;

    caps[2].MajorVersion = 1;
    caps[2].MinorVersion = 2;
    caps[2].EcoLevel = 0;

    //  the pin attributes for v 1.0 and v1.1 of the misc module
    //  are fully compatible, so the version comparison is made against 1.0
    // Treating 1.2 same as 1.0/1.1.
    caps[1].Capability = &caps[0];
    caps[2].Capability = &caps[0];

    /* AP20 misc module pin attributes, set differently than AP15 as the pin
     * attribute registers in misc module changed */
    caps[3].MajorVersion = 2;
    caps[3].MinorVersion = 0;
    caps[3].EcoLevel = 0;
    caps[3].Capability = &caps[3];

    NV_ASSERT_SUCCESS(NvRmModuleGetCapabilities(
            rm,
            NvRmModuleID_Misc,
            caps,
            sizeof(caps)/sizeof(caps[0]),
            (void**)&pCap));

    Count = NvOdmQueryPinAttributes((const NvOdmPinAttrib **)&pPinAttribTable);

    for ( ; Count ; Count--, pPinAttribTable++)
    {
        Major = (pPinAttribTable->ConfigRegister >> 28);
        Minor = (pPinAttribTable->ConfigRegister >> 24) & 0xF;
        if ((Major == pCap->MajorVersion) && (Minor == pCap->MinorVersion))
        {
            Offset = pPinAttribTable->ConfigRegister & 0xFFFF;
            Value = pPinAttribTable->Value;
            NV_REGW(rm, NvRmModuleID_Misc, 0, Offset, Value);
        }
    }
}


static void NvRmPrivBasicReset( NvRmDeviceHandle rm )
{
    switch (rm->ChipId.Id) {
    case 0x15:
    case 0x16:
        NvRmPrivAp15BasicReset(rm);
        return;
    case 0x20:
        NvRmPrivAp20BasicReset(rm);
        return;
    default:
        NV_ASSERT(!"Unsupported chip ID");
        return;
    }
}

NvError NvRmPrivMcErrorMonitorStart( NvRmDeviceHandle rm )
{
    NvError e = NvError_NotSupported;

    switch (rm->ChipId.Id) {
    case 0x15:
    case 0x16:
        e = NvRmPrivAp15McErrorMonitorStart(rm);
        break;
    case 0x20:
        e = NvRmPrivAp20McErrorMonitorStart(rm);
        break;
    default:
        NV_ASSERT(!"Unsupported chip ID");
        break;
    }
    return e;
}

void NvRmPrivMcErrorMonitorStop( NvRmDeviceHandle rm )
{
    switch (rm->ChipId.Id) {
    case 0x15:
    case 0x16:
        NvRmPrivAp15McErrorMonitorStop(rm);
        break;
    case 0x20:
        NvRmPrivAp20McErrorMonitorStop(rm);
        break;
    default:
        NV_ASSERT(!"Unsupported chip ID");
        break;
    }
}


#endif
