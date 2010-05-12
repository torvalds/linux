/*
 * Copyright (c) 2009 NVIDIA Corporation.
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

#ifndef INCLUDED_nvrm_module_H
#define INCLUDED_nvrm_module_H


#if defined(__cplusplus)
extern "C"
{
#endif

#include "nvrm_init.h"

#include "nvrm_drf.h"

/**
 * SOC hardware controller class identifiers.
 */

typedef enum
{

    /// Specifies an invalid module ID.
        NvRmModuleID_Invalid = 0,

    /// Specifies the application processor.
        NvRmModuleID_Cpu,

    /// Specifies the Audio Video Processor
        NvRmModuleID_Avp,

    /// Specifies the Vector Co Processor
        NvRmModuleID_Vcp,

    /// Specifies the display controller.
        NvRmModuleID_Display,

    /// Specifies the IDE controller.
        NvRmModuleID_Ide,

    /// Graphics Host
        NvRmModuleID_GraphicsHost,

    /// Specifies 2D graphics controller
        NvRmModuleID_2D,

    /// Specifies 3D graphics controller
        NvRmModuleID_3D,

    /// Specifies VG graphics controller
        NvRmModuleID_VG,

    /// NV epp (encoder pre-processor)
        NvRmModuleID_Epp,

    /// NV isp (image signal processor)
        NvRmModuleID_Isp,

    /// NV vi (video input)
        NvRmModuleID_Vi,

    /// Specifies USB2 OTG controller
        NvRmModuleID_Usb2Otg,

    /// Specifies the I2S controller.
        NvRmModuleID_I2s,

    /// Specifies the Pulse Width Modulator controller.
        NvRmModuleID_Pwm,

    /// Specifies the Three Wire controller.
        NvRmModuleID_Twc,

    /// HSMMC controller
        NvRmModuleID_Hsmmc,

    /// Specifies SDIO controller
        NvRmModuleID_Sdio,

    /// Specifies the NAND controller.
        NvRmModuleID_Nand,

    /// Specifies the I2C controller.
        NvRmModuleID_I2c,

    /// Specifies the Sony Phillips Digital Interface Format controller.
        NvRmModuleID_Spdif,

    /// Specifies the %UART controller.
        NvRmModuleID_Uart,

    /// Specifies the timer controller.
        NvRmModuleID_Timer,

    /// Specifies the timer controller microsecond counter.
        NvRmModuleID_TimerUs,

    /// Real time clock controller.
        NvRmModuleID_Rtc,

    /// Specifies the Audio Codec 97 controller.
        NvRmModuleID_Ac97,

    /// Specifies Audio Bit Stream Engine
        NvRmModuleID_BseA,

    /// Specifies Video decoder
        NvRmModuleID_Vde,

    /// Specifies Video encoder (Motion Picture Encoder)
        NvRmModuleID_Mpe,

    /// Specifies Camera Serial Interface
        NvRmModuleID_Csi,

    /// Specifies High-Bandwidth Digital Content Protection interface
        NvRmModuleID_Hdcp,

    /// Specifies High definition Multimedia Interface
        NvRmModuleID_Hdmi,

    /// Specifies MIPI baseband controller
        NvRmModuleID_Mipi,

    /// Specifies TV out controller
        NvRmModuleID_Tvo,

    /// Specifies Serial Display
        NvRmModuleID_Dsi,

    /// Specifies Dynamic Voltage Controller
        NvRmModuleID_Dvc,

    /// Specifies the eXtended I/O controller.
        NvRmModuleID_Xio,

    /// SPI controller
        NvRmModuleID_Spi,

    /// Specifies SLink controller
        NvRmModuleID_Slink,

    /// Specifies FUSE controller
        NvRmModuleID_Fuse,

    /// Specifies KFUSE controller
        NvRmModuleID_KFuse,

    /// Specifies EthernetMIO controller
        NvRmModuleID_Mio,

    /// Specifies keyboard controller
        NvRmModuleID_Kbc,

    /// Specifies Pmif controller
        NvRmModuleID_Pmif,

    /// Specifies Unified Command Queue
        NvRmModuleID_Ucq,

    /// Specifies Event controller
        NvRmModuleID_EventCtrl,

    /// Specifies Flow controller
        NvRmModuleID_FlowCtrl,

    /// Resource Semaphore
        NvRmModuleID_ResourceSema,

    /// Arbitration Semaphore
        NvRmModuleID_ArbitrationSema,

    /// Specifies Arbitration Priority
        NvRmModuleID_ArbPriority,

    /// Specifies Cache Memory Controller
        NvRmModuleID_CacheMemCtrl,

    /// Specifies very fast infra red controller
        NvRmModuleID_Vfir,

    /// Specifies Exception Vector
        NvRmModuleID_ExceptionVector,

    /// Specifies Boot Strap Controller
        NvRmModuleID_BootStrap,

    /// Specifies System Statistics Monitor controller
        NvRmModuleID_SysStatMonitor,

    /// Specifies System 
        NvRmModuleID_Cdev,
 
    /// Misc module ID which contains registers for PInmux/DAP control etc.
        NvRmModuleID_Misc,

    // PCIE Device attached to AP20
        NvRmModuleID_PcieDevice,

    // One-wire interface controller
        NvRmModuleID_OneWire,

    // Sync NOR controller
        NvRmModuleID_SyncNor,

    // NOR Memory aperture
        NvRmModuleID_Nor,

    // AVP UCQ module.
        NvRmModuleID_AvpUcq,

    /// clock and reset controller
        NvRmPrivModuleID_ClockAndReset,

    /// interrupt controller
        NvRmPrivModuleID_Interrupt,

    /// interrupt controller Arbitration Semaphore grant registers
        NvRmPrivModuleID_InterruptArbGnt,

    /// interrupt controller DMA Tx/Rx DRQ registers
        NvRmPrivModuleID_InterruptDrq,

    /// interrupt controller special SW interrupt
        NvRmPrivModuleID_InterruptSw,

    /// interrupt controller special CPU interrupt
        NvRmPrivModuleID_InterruptCpu,

    /// Apb Dma controller
        NvRmPrivModuleID_ApbDma,

    /// Apb Dma Channel
        NvRmPrivModuleID_ApbDmaChannel,

    /// Gpio controller
        NvRmPrivModuleID_Gpio,

    /// Pin-Mux Controller
        NvRmPrivModuleID_PinMux,

    /// memory configuation
        NvRmPrivModuleID_Mselect,

    /// memory controller (internal memory and memory arbitration)
        NvRmPrivModuleID_MemoryController,

    /// external memory (ddr ram, etc.)
        NvRmPrivModuleID_ExternalMemoryController,

    /// Processor Id
        NvRmPrivModuleID_ProcId,

    /// Entire System (used for system reset)
        NvRmPrivModuleID_System,

    /* CC device id (not sure what it actually does, but it is needed to
     * set the mem_init_done bit so that memory works).
     */
        NvRmPrivModuleID_CC,

    /// AHB Arbitration Control
        NvRmPrivModuleID_Ahb_Arb_Ctrl,

    /// AHB Gizmo Control
        NvRmPrivModuleID_Ahb_Gizmo_Ctrl,

    /// External memory
        NvRmPrivModuleID_ExternalMemory,

    /// Internal memory
        NvRmPrivModuleID_InternalMemory,

    /// TCRAM 
        NvRmPrivModuleID_Tcram,

    /// IRAM
        NvRmPrivModuleID_Iram,

    /// GART 
        NvRmPrivModuleID_Gart,

    /// MIO/EXIO
        NvRmPrivModuleID_Mio_Exio,

    /* External PMU */
        NvRmPrivModuleID_PmuExt,

    /* One module ID for all peripherals which includes cache controller, 
     * SCU and interrupt controller */
        NvRmPrivModuleID_ArmPerif,
    NvRmPrivModuleID_ArmInterruptctrl,

    /* PCIE Root Port internally is made up of 3 major blocks. These 3 blocks
     * have seperate reset and clock domains. So, the driver treats these 
     *  
     *  AFI is the wrapper on the top of the PCI core.
     *  PCIe refers to the core PCIe state machine module.
     *  PcieXclk refers to the transmit/receive logic which runs at different
     *  clock and have different reset.
     * */
        NvRmPrivModuleID_Afi,
    NvRmPrivModuleID_Pcie,
    NvRmPrivModuleID_PcieXclk,

    /* PL310 */
        NvRmPrivModuleID_Pl310,

    /* 
     * AHB re-map aperture seen from AVP. Use this aperture for AVP to have
     * uncached access to SDRAM.
     */
        NvRmPrivModuleID_AhbRemap,
    NvRmModuleID_Num,
    NvRmModuleID_Force32 = 0x7FFFFFFF
} NvRmModuleID;

/* FIXME 
 * Hack to make the existing drivers work. 
 * NvRmPriv* should be renamed to NvRm*
 */
#define NvRmPrivModuleID_Num  NvRmModuleID_Num

/**
 * Multiple module instances are handled by packing the instance number into
 * the high bits of the module id.  This avoids ponderous apis with both
 * module ids and instance numbers.
 */

/**
 * Module bitfields that are compatible with the NV_DRF macros.
 */
#define NVRM_MODULE_0                   (0x0)
#define NVRM_MODULE_0_ID_RANGE          15:0
#define NVRM_MODULE_0_INSTANCE_RANGE    19:16
#define NVRM_MODULE_0_BAR_RANGE         23:20

/**
 * Create a module id with a given instance.
 */
#define NVRM_MODULE_ID( id, instance ) \
    (NvRmModuleID)( \
          NV_DRF_NUM( NVRM, MODULE, ID, (id) ) \
        | NV_DRF_NUM( NVRM, MODULE, INSTANCE, (instance) ) )

/**
 * Get the actual module id.
 */
#define NVRM_MODULE_ID_MODULE( id ) \
    NV_DRF_VAL( NVRM, MODULE, ID, (id) )

/**
 * Get the instance number of the module id.
 */
#define NVRM_MODULE_ID_INSTANCE( id ) \
    NV_DRF_VAL( NVRM, MODULE, INSTANCE, (id) )

/**
 * Get the bar number for the module.
 */
#define NVRM_MODULE_ID_BAR( id ) \
    NV_DRF_VAL( NVRM, MODULE, BAR, (id) )

/**
 * Module Information structure
 */

typedef struct NvRmModuleInfoRec
{
    NvU32 Instance;
    NvU32 Bar;
    NvRmPhysAddr BaseAddress;
    NvU32 Length;
} NvRmModuleInfo;

/**
 * Returns list of available module instances and their information.
 *
 * @param hRmDeviceHandle The RM device handle
 * @param Module The module for which to get the number of instances.
 * @param pNum Unsigned integer indicating the number of module information
 *      structures in the array pModuleInfo.
 * @param pModuleInfo A pointer to an array of module information structure,
 *      where the size of array is determined by the value in pNum.
 *
 * @retval NvSuccess If successful, or the appropriate error.
 */

 NvError NvRmModuleGetModuleInfo( 
    NvRmDeviceHandle hDevice,
    NvRmModuleID module,
    NvU32 * pNum,
    NvRmModuleInfo * pModuleInfo );

/**
 * Returns a physical address associated with a hardware module.
 * (To be depcreated and replaced by NvRmModuleGetModuleInfo)
 *
 * @param hRmDeviceHandle The RM device handle
 * @param Module the module for which to get addresses.
 * @param pBaseAddress a pointer to the beginning of the
 * hardware register bank is stored here.
 * @param pSize the length of the aperture in bytes is stored
 * here.
 */

 void NvRmModuleGetBaseAddress( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID Module,
    NvRmPhysAddr * pBaseAddress,
    NvU32 * pSize );

/**
 * Returns the number of instances of a particular hardware module.
 * (To be depcreated and replaced by NvRmModuleGetModuleInfo)
 *
 * @param hRmDeviceHandle The RM device handle
 * @param Module The module for which to get the number of instances.
 *
 * @returns Number of instances.
 */

 NvU32 NvRmModuleGetNumInstances( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID Module );

/**
 * Resets the module controller hardware.
 *
 * @param hRmDeviceHandle The RM device handle
 * @param Module The module to reset
 */

 void NvRmModuleReset( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID Module );

/** 
 *  Resets the controller with an option to hold the controller in the reset.
 *  
 *  @param hRmDeviceHandle Rm device handle
 *  @param Module The module to be reset
 *  @param bHold If NV_TRUE hold the module in reset, If NV_TRUE pulse the
 *  reset.
 *
 *  So, to keep the module in reset and do something 
 *  NvRmModuleResetWithHold(hRm, ModId, NV_TRUE)
 *  ... update some registers
 *  NvRmModuleResetWithHold(hRm, ModId, NV_FALSE)
 */

 void NvRmModuleResetWithHold( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID Module,
    NvBool bHold );

/**
 * DDK capability encapsualtion. See NvRmModuleGetCapabilities().
 */

typedef struct NvRmModuleCapabilityRec
{
    NvU8 MajorVersion;
    NvU8 MinorVersion;
    NvU8 EcoLevel;
    void* Capability;
} NvRmModuleCapability;

/**
 * Returns a pointer to a class-specific capabilities structure.
 *
 * Each DDK will supply a list of NvRmCapability structures sorted by module
 * Minor and Eco levels (assuming that no DDK supports two Major versions
 * simulatenously).  The last cap in the list that matches the hardware's
 * version and eco level will be returned.  If the current hardware's eco
 * level is higher than the given module capability list, the last module
 * capability with the highest eco level (the last in the list) will be
 * returned.
 * 
 * @param hRmDeviceHandle The RM device handle
 * @param Module the target module
 * @param pCaps Pointer to the capability list
 * @param NumCaps The number of capabilities in the list
 * @param Capability Out parameter: the cap that maches the current hardware
 *
 * Example usage:
 *
 * typedef struct FakeDdkCapRec
 * {
 *     NvU32 FeatureBits;
 * } FakeDdkCap;
 *
 * FakeDdkCap cap1;
 * FakeDdkCap cap2;
 * FakeDdkCap *cap;
 * NvRmModuleCapability caps[] =
 *       { { 1, 0, 0, &fcap1 },
 *         { 1, 1, 0, &fcap2 },
 *       };
 * cap1.bits = ...;
 * cap2.bits = ...;
 * err = NvRmModuleGetCapabilities( hDevice, NvRmModuleID_FakeDDK, caps, 2,
 *     (void *)&cap );
 * ...
 * if( cap->FeatureBits & FAKEDKK_SOME_FEATURE )
 * {
 *     ...
 * }
 */

 NvError NvRmModuleGetCapabilities( 
    NvRmDeviceHandle hDeviceHandle,
    NvRmModuleID Module,
    NvRmModuleCapability * pCaps,
    NvU32 NumCaps,
    void* * Capability );

/**
 * @brief Queries for the device unique ID.
 *
 * @pre Not callable from early boot.
 *
 * @param pId A pointer to an area of caller-allocated memory to hold the
 * unique ID.
 * @param pIdSize an input, a pointer to a variable containing the size of
 * the caller-allocated memory to hold the unique ID pointed to by \em pId.
 * Upon successful return, this value is updated to reflect the actual
 * size of the unique ID returned in \em pId.
 *
 * @retval ::NvError_Success \em pId points to the unique ID and \em pIdSize
 * points to the actual size of the ID.
 * @retval ::NvError_BadParameter
 * @retval ::NvError_NotSupported
 * @retval ::NvError_InsufficientMemory
 */
 
 NvError NvRmQueryChipUniqueId( 
    NvRmDeviceHandle hDevHandle,
    NvU32 IdSize,
    void* pId );

/**
 * @brief Returns random bytes using hardware sources of entropy
 *
 * @param hRmDeviceHandle The RM device handle
 * @param NumBytes Number of random bytes to return in pBytes.
 * @param pBytes Array where the random bytes should be stored
 *
 * @retval ::NvError_Success
 * @retval ::NvError_BadParameter
 * @retval ::NvError_NotSupported If no hardware entropy source is available
 */
 
 NvError NvRmGetRandomBytes( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvU32 NumBytes,
    void* pBytes );

/*
 * Module access functions below. 
 * NOTE: Rm doesn't gaurantee access to all the modules as it only maps a few
 * modules.
 * This is not meant to be a primary mechanism to access the module registers.
 * Clients should map their register address and access the registers.
 */

/**
 * NV_REGR: register read from hardware.
 *
 * @param rm The resource manager istance
 * @param aperture The register aperture
 * @param instance The module instance
 * @param offset The offset inside the aperture
 *
 * Note that the aperture comes from the RM's private module id enumeration,
 * which is a superset of the public enumeration from nvrm_module.h.
 */

/**
 * NV_REGW: register write to hardware.
 * 
 * @param rm The resource manager istance
 * @param aperture The register aperture
 * @param instance The module instance
 * @param offset The offset inside the aperture
 * @param data The data to write
 *
 * see the note regarding apertures for NV_REGR.
 */
#define NV_REGR(rm, aperture, instance, offset) \
    NvRegr((rm),(NvRmModuleID)NVRM_MODULE_ID((aperture),(instance)),(offset))

#define NV_REGW(rm, aperture, instance, offset, data) \
    NvRegw((rm),(NvRmModuleID)NVRM_MODULE_ID((aperture),(instance)),(offset),(data))


 NvU32 NvRegr( 
    NvRmDeviceHandle hDeviceHandle,
    NvRmModuleID aperture,
    NvU32 offset );

 void NvRegw( 
    NvRmDeviceHandle hDeviceHandle,
    NvRmModuleID aperture,
    NvU32 offset,
    NvU32 data );

/**
 * NV_REGR_MULT: read multiple registers from hardware
 *
 * @param rm The resource manager istance
 * @param aperture The register aperture
 * @param instance The module instance
 * @param num The number of registers
 * @param offsets The register offsets
 * @param values The register values
 */

/**
 * NV_REGW_MULT: write multiple registers from hardware
 *
 * @param rm The resource manager istance
 * @param aperture The register aperture
 * @param instance The module instance
 * @param num The number of registers
 * @param offsets The register offsets
 * @param values The register values
 */

/**
 * NV_REGW_BLOCK: write a block of registers to hardware
 *
 * @param rm The resource manager istance
 * @param aperture The register aperture
 * @param instance The module instance
 * @param num The number of registers
 * @param offset The beginning register offset
 * @param values The register values
 */

/**
 * NV_REGR_BLOCK: read a block of registers from hardware
 *
 * @param rm The resource manager istance
 * @param aperture The register aperture
 * @param instance The module instance
 * @param num The number of registers
 * @param offset The beginning register offset
 * @param values The register values
 */

#define NV_REGR_MULT(rm, aperture, instance, num, offsets, values) \
    NvRegrm((rm),(NvRmModuleID)NVRM_MODULE_ID((aperture),(instance)),(num),(offsets),(values))

#define NV_REGW_MULT(rm, aperture, instance, num, offsets, values) \
    NvRegwm((rm),(NvRmModuleID)NVRM_MODULE_ID((aperture),(instance)),(num),(offsets),(values))

#define NV_REGW_BLOCK(rm, aperture, instance, num, offset, values) \
    NvRegwb((rm),(NvRmModuleID)NVRM_MODULE_ID((aperture),(instance)),(num),(offset),(values))

#define NV_REGR_BLOCK(rm, aperture, instance, num, offset, values) \
    NvRegrb((rm),(NvRmModuleID)NVRM_MODULE_ID((aperture),(instance)),(num),(offset),(values))

 void NvRegrm( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID aperture,
    NvU32 num,
    const NvU32 * offsets,
    NvU32 * values );

 void NvRegwm( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID aperture,
    NvU32 num,
    const NvU32 * offsets,
    const NvU32 * values );

 void NvRegwb( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID aperture,
    NvU32 num,
    NvU32 offset,
    const NvU32 * values );

 void NvRegrb( 
    NvRmDeviceHandle hRmDeviceHandle,
    NvRmModuleID aperture,
    NvU32 num,
    NvU32 offset,
    NvU32 * values );

#define NV_REGR08(rm, aperture, instance, offset) \
    NvRegr08((rm),(NvRmModuleID)NVRM_MODULE_ID((aperture),(instance)),(offset))

#define NV_REGW08(rm, aperture, instance, offset, data) \
    NvRegw08((rm),(NvRmModuleID)NVRM_MODULE_ID((aperture),(instance)),(offset),(data))

 NvU8 NvRegr08( 
    NvRmDeviceHandle hDeviceHandle,
    NvRmModuleID aperture,
    NvU32 offset );

 void NvRegw08( 
    NvRmDeviceHandle rm,
    NvRmModuleID aperture,
    NvU32 offset,
    NvU8 data );

/** @} */

#if defined(__cplusplus)
}
#endif

#endif
