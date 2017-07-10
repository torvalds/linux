/*************************************************************************/ /*!
@File
@Title          Header for Services abstraction layer
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Declaration of an interface layer used to abstract code that
                can be compiled outside of the DDK, potentially in a
                completely different OS.
                All the headers included by this file must also be copied to
                the alternative source tree.
                All the functions declared here must have a DDK implementation
                inside the DDK source tree (e.g. rgxlayer_km_impl.h/.c) and
                another different implementation in case they are used outside
                of the DDK.
                All of the functions accept as a first parameter a
                "const void *hPrivate" argument. It should be used to pass
                around any implementation specific data required.
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#if !defined (__RGXLAYER_KM_H__)
#define __RGXLAYER_KM_H__

#if defined (__cplusplus)
extern "C" {
#endif


#include "img_types.h"
#include "img_defs.h"
#include "pvrsrv_error.h" /* includes pvrsrv_errors.h */
#include "rgx_bvnc_defs_km.h"

#include "rgx_firmware_processor.h"
/* includes:
 * rgx_meta.h and rgx_mips.h,
 * rgxdefs_km.h,
 * rgx_cr_defs_km.h,
 * RGX_BVNC_CORE_KM_HEADER (rgxcore_km_B.V.N.C.h),
 * RGX_BNC_CONFIG_KM_HEADER (rgxconfig_km_B.V.N.C.h)
 */


/*!
*******************************************************************************

 @Function      RGXWriteReg32/64

 @Description   Write a value to a 32/64 bit RGX register

 @Input         hPrivate         : Implementation specific data
 @Input         ui32RegAddr      : Register offset inside the register bank
 @Input         ui32/64RegValue  : New register value

 @Return        void

******************************************************************************/
void RGXWriteReg32(const void *hPrivate,
                   IMG_UINT32 ui32RegAddr,
                   IMG_UINT32 ui32RegValue);

void RGXWriteReg64(const void *hPrivate,
                   IMG_UINT32 ui32RegAddr,
                   IMG_UINT64 ui64RegValue);

/*!
*******************************************************************************

 @Function       RGXReadReg32/64

 @Description    Read a 32/64 bit RGX register

 @Input          hPrivate     : Implementation specific data
 @Input          ui32RegAddr  : Register offset inside the register bank

 @Return         Register value

******************************************************************************/
IMG_UINT32 RGXReadReg32(const void *hPrivate,
                        IMG_UINT32 ui32RegAddr);

IMG_UINT64 RGXReadReg64(const void *hPrivate,
                        IMG_UINT32 ui32RegAddr);

/*!
*******************************************************************************

 @Function       RGXPollReg32/64

 @Description    Poll on a 32/64 bit RGX register until some bits are set/unset

 @Input          hPrivate         : Implementation specific data
 @Input          ui32RegAddr      : Register offset inside the register bank
 @Input          ui32/64RegValue  : Value expected from the register
 @Input          ui32/64RegMask   : Only the bits set in this mask will be
                                    checked against uiRegValue

 @Return         PVRSRV_OK if the poll succeeds,
                 PVRSRV_ERROR_TIMEOUT if the poll takes too long

******************************************************************************/
PVRSRV_ERROR RGXPollReg32(const void *hPrivate,
                          IMG_UINT32 ui32RegAddr,
                          IMG_UINT32 ui32RegValue,
                          IMG_UINT32 ui32RegMask);

PVRSRV_ERROR RGXPollReg64(const void *hPrivate,
                          IMG_UINT32 ui32RegAddr,
                          IMG_UINT64 ui64RegValue,
                          IMG_UINT64 ui64RegMask);

/*!
*******************************************************************************

 @Function       RGXWaitCycles

 @Description    Wait for a number of GPU cycles and/or microseconds

 @Input          hPrivate    : Implementation specific data
 @Input          ui32Cycles  : Number of GPU cycles to wait for in pdumps,
                               it can also be used when running driver-live
                               if desired (ignoring the next parameter)
 @Input          ui32WaitUs  : Number of microseconds to wait for when running
                               driver-live

 @Return         void

******************************************************************************/
void RGXWaitCycles(const void *hPrivate,
                   IMG_UINT32 ui32Cycles,
                   IMG_UINT32 ui32WaitUs);

/*!
*******************************************************************************

 @Function       RGXCommentLogPower

 @Description    This function is called with debug messages during
                 the RGX start/stop process

 @Input          hPrivate   : Implementation specific data
 @Input          pszString  : Message to be printed
 @Input          ...        : Variadic arguments

 @Return         void

******************************************************************************/
void RGXCommentLogPower(const void *hPrivate,
                        const IMG_CHAR *pszString,
                        ...) __printf(2, 3);


/*!
*******************************************************************************

 @Function        RGXAcquireKernelMMUPC

 @Description     Acquire the Kernel MMU Page Catalogue device physical address

 @Input           hPrivate  : Implementation specific data
 @Input           psPCAddr  : Returned page catalog address

 @Return          void

******************************************************************************/
void RGXAcquireKernelMMUPC(const void *hPrivate, IMG_DEV_PHYADDR *psPCAddr);

/*!
*******************************************************************************

 @Function        RGXWriteKernelMMUPC32/64

 @Description     Write the Kernel MMU Page Catalogue to the 32/64 bit
                  RGX register passed as argument.
                  In a driver-live scenario without PDump these functions
                  are the same as RGXWriteReg32/64 and they don't need
                  to be reimplemented.

 @Input           hPrivate        : Implementation specific data
 @Input           ui32PCReg       : Register offset inside the register bank
 @Input           ui32AlignShift  : PC register alignshift
 @Input           ui32Shift       : PC register shift
 @Input           ui32/64PCVal    : Page catalog value (aligned and shifted)

 @Return          void

******************************************************************************/
#if defined(PDUMP)

void RGXWriteKernelMMUPC64(const void *hPrivate,
                           IMG_UINT32 ui32PCReg,
                           IMG_UINT32 ui32PCRegAlignShift,
                           IMG_UINT32 ui32PCRegShift,
                           IMG_UINT64 ui64PCVal);

void RGXWriteKernelMMUPC32(const void *hPrivate,
                           IMG_UINT32 ui32PCReg,
                           IMG_UINT32 ui32PCRegAlignShift,
                           IMG_UINT32 ui32PCRegShift,
                           IMG_UINT32 ui32PCVal);


#else  /* defined(PDUMP) */

#define RGXWriteKernelMMUPC64(priv, pcreg, alignshift, shift, pcval) \
	RGXWriteReg64(priv, pcreg, pcval)

#define RGXWriteKernelMMUPC32(priv, pcreg, alignshift, shift, pcval) \
	RGXWriteReg32(priv, pcreg, pcval)

#endif /* defined(PDUMP) */



/*!
*******************************************************************************

 @Function        RGXAcquireGPURegsAddr

 @Description     Acquire the GPU registers base device physical address

 @Input           hPrivate       : Implementation specific data
 @Input           psGPURegsAddr  : Returned GPU registers base address

 @Return          void

******************************************************************************/
void RGXAcquireGPURegsAddr(const void *hPrivate, IMG_DEV_PHYADDR *psGPURegsAddr);

/*!
*******************************************************************************

 @Function        RGXMIPSWrapperConfig

 @Description     Write GPU register bank transaction ID and MIPS boot mode
                  to the MIPS wrapper config register (passed as argument).
                  In a driver-live scenario without PDump this is the same as
                  RGXWriteReg64 and it doesn't need to be reimplemented.

 @Input           hPrivate          : Implementation specific data
 @Input           ui32RegAddr       : Register offset inside the register bank
 @Input           ui64GPURegsAddr   : GPU registers base address
 @Input           ui32GPURegsAlign  : Register bank transactions alignment
 @Input           ui32BootMode      : Mips BOOT ISA mode

 @Return          void

******************************************************************************/
#if defined(PDUMP)
void RGXMIPSWrapperConfig(const void *hPrivate,
                          IMG_UINT32 ui32RegAddr,
                          IMG_UINT64 ui64GPURegsAddr,
                          IMG_UINT32 ui32GPURegsAlign,
                          IMG_UINT32 ui32BootMode);
#else
#define RGXMIPSWrapperConfig(priv, regaddr, gpuregsaddr, gpuregsalign, bootmode) \
	RGXWriteReg64(priv, regaddr, ((gpuregsaddr) >> (gpuregsalign)) | (bootmode))
#endif

/*!
*******************************************************************************

 @Function        RGXAcquireBootRemapAddr

 @Description     Acquire the device physical address of the MIPS bootloader
                  accessed through remap region

 @Input           hPrivate         : Implementation specific data
 @Output          psBootRemapAddr  : Base address of the remapped bootloader

 @Return          void

******************************************************************************/
void RGXAcquireBootRemapAddr(const void *hPrivate, IMG_DEV_PHYADDR *psBootRemapAddr);

/*!
*******************************************************************************

 @Function        RGXBootRemapConfig

 @Description     Configure the bootloader remap registers passed as arguments.
                  In a driver-live scenario without PDump this is the same as
                  two RGXWriteReg64 and it doesn't need to be reimplemented.

 @Input           hPrivate             : Implementation specific data
 @Input           ui32Config1RegAddr   : Remap config1 register offset
 @Input           ui64Config1RegValue  : Remap config1 register value
 @Input           ui32Config2RegAddr   : Remap config2 register offset
 @Input           ui64Config2PhyAddr   : Output remapped aligned physical address
 @Input           ui64Config2PhyMask   : Mask for the output physical address
 @Input           ui64Config2Settings  : Extra settings for this remap region

 @Return          void

******************************************************************************/
#if defined(PDUMP)
void RGXBootRemapConfig(const void *hPrivate,
                        IMG_UINT32 ui32Config1RegAddr,
                        IMG_UINT64 ui64Config1RegValue,
                        IMG_UINT32 ui32Config2RegAddr,
                        IMG_UINT64 ui64Config2PhyAddr,
                        IMG_UINT64 ui64Config2PhyMask,
                        IMG_UINT64 ui64Config2Settings);
#else
#define RGXBootRemapConfig(priv, c1reg, c1val, c2reg, c2phyaddr, c2phymask, c2settings) do { \
		RGXWriteReg64(priv, c1reg, (c1val)); \
		RGXWriteReg64(priv, c2reg, ((c2phyaddr) & (c2phymask)) | (c2settings)); \
	} while (0)
#endif

/*!
*******************************************************************************

 @Function        RGXAcquireCodeRemapAddr

 @Description     Acquire the device physical address of the MIPS code
                  accessed through remap region

 @Input           hPrivate         : Implementation specific data
 @Output          psCodeRemapAddr  : Base address of the remapped code

 @Return          void

******************************************************************************/
void RGXAcquireCodeRemapAddr(const void *hPrivate, IMG_DEV_PHYADDR *psCodeRemapAddr);

/*!
*******************************************************************************

 @Function        RGXCodeRemapConfig

 @Description     Configure the code remap registers passed as arguments.
                  In a driver-live scenario without PDump this is the same as
                  two RGXWriteReg64 and it doesn't need to be reimplemented.

 @Input           hPrivate             : Implementation specific data
 @Input           ui32Config1RegAddr   : Remap config1 register offset
 @Input           ui64Config1RegValue  : Remap config1 register value
 @Input           ui32Config2RegAddr   : Remap config2 register offset
 @Input           ui64Config2PhyAddr   : Output remapped aligned physical address
 @Input           ui64Config2PhyMask   : Mask for the output physical address
 @Input           ui64Config2Settings  : Extra settings for this remap region

 @Return          void

******************************************************************************/
#if defined(PDUMP)
void RGXCodeRemapConfig(const void *hPrivate,
                        IMG_UINT32 ui32Config1RegAddr,
                        IMG_UINT64 ui64Config1RegValue,
                        IMG_UINT32 ui32Config2RegAddr,
                        IMG_UINT64 ui64Config2PhyAddr,
                        IMG_UINT64 ui64Config2PhyMask,
                        IMG_UINT64 ui64Config2Settings);
#else
#define RGXCodeRemapConfig(priv, c1reg, c1val, c2reg, c2phyaddr, c2phymask, c2settings) do { \
		RGXWriteReg64(priv, c1reg, (c1val)); \
		RGXWriteReg64(priv, c2reg, ((c2phyaddr) & (c2phymask)) | (c2settings)); \
	} while (0)
#endif

/*!
*******************************************************************************

 @Function        RGXAcquireDataRemapAddr

 @Description     Acquire the device physical address of the MIPS data
                  accessed through remap region

 @Input           hPrivate         : Implementation specific data
 @Output          psDataRemapAddr  : Base address of the remapped data

 @Return          void

******************************************************************************/
void RGXAcquireDataRemapAddr(const void *hPrivate, IMG_DEV_PHYADDR *psDataRemapAddr);

/*!
*******************************************************************************

 @Function        RGXDataRemapConfig

 @Description     Configure the data remap registers passed as arguments.
                  In a driver-live scenario without PDump this is the same as
                  two RGXWriteReg64 and it doesn't need to be reimplemented.

 @Input           hPrivate             : Implementation specific data
 @Input           ui32Config1RegAddr   : Remap config1 register offset
 @Input           ui64Config1RegValue  : Remap config1 register value
 @Input           ui32Config2RegAddr   : Remap config2 register offset
 @Input           ui64Config2PhyAddr   : Output remapped aligned physical address
 @Input           ui64Config2PhyMask   : Mask for the output physical address
 @Input           ui64Config2Settings  : Extra settings for this remap region

 @Return          void

******************************************************************************/
#if defined(PDUMP)
void RGXDataRemapConfig(const void *hPrivate,
                        IMG_UINT32 ui32Config1RegAddr,
                        IMG_UINT64 ui64Config1RegValue,
                        IMG_UINT32 ui32Config2RegAddr,
                        IMG_UINT64 ui64Config2PhyAddr,
                        IMG_UINT64 ui64Config2PhyMask,
                        IMG_UINT64 ui64Config2Settings);
#else
#define RGXDataRemapConfig(priv, c1reg, c1val, c2reg, c2phyaddr, c2phymask, c2settings) do { \
		RGXWriteReg64(priv, c1reg, (c1val)); \
		RGXWriteReg64(priv, c2reg, ((c2phyaddr) & (c2phymask)) | (c2settings)); \
	} while (0)
#endif

/*!
*******************************************************************************

 @Function        RGXAcquireTrampolineRemapAddr

 @Description     Acquire the device physical address of the MIPS data
                  accessed through remap region

 @Input           hPrivate             : Implementation specific data
 @Output          psTrampolineRemapAddr: Base address of the remapped data

 @Return          void

******************************************************************************/
void RGXAcquireTrampolineRemapAddr(const void *hPrivate, IMG_DEV_PHYADDR *psTrampolineRemapAddr);

/*!
*******************************************************************************

 @Function        RGXTrampolineRemapConfig

 @Description     Configure the trampoline remap registers passed as arguments.
                  In a driver-live scenario without PDump this is the same as
                  two RGXWriteReg64 and it doesn't need to be reimplemented.

 @Input           hPrivate             : Implementation specific data
 @Input           ui32Config1RegAddr   : Remap config1 register offset
 @Input           ui64Config1RegValue  : Remap config1 register value
 @Input           ui32Config2RegAddr   : Remap config2 register offset
 @Input           ui64Config2PhyAddr   : Output remapped aligned physical address
 @Input           ui64Config2PhyMask   : Mask for the output physical address
 @Input           ui64Config2Settings  : Extra settings for this remap region

 @Return          void

******************************************************************************/
#define RGXTrampolineRemapConfig(priv, c1reg, c1val, c2reg, c2phyaddr, c2phymask, c2settings) do { \
		RGXWriteReg64(priv, c1reg, (c1val)); \
		RGXWriteReg64(priv, c2reg, ((c2phyaddr) & (c2phymask)) | (c2settings)); \
	} while (0)

/*!
*******************************************************************************

 @Function        RGXDoFWSlaveBoot

 @Description     Returns whether or not a FW Slave Boot is required
                  while powering on

 @Input           hPrivate       : Implementation specific data

 @Return          IMG_BOOL

******************************************************************************/
IMG_BOOL RGXDoFWSlaveBoot(const void *hPrivate);

/*!
*******************************************************************************

 @Function       RGXIOCoherencyTest

 @Description    Performs a coherency test

 @Input          hPrivate         : Implementation specific data

 @Return         PVRSRV_OK if the test succeeds,
                 PVRSRV_ERROR_INIT_FAILURE if the test fails at some point

******************************************************************************/
PVRSRV_ERROR RGXIOCoherencyTest(const void *hPrivate);

/*!
*******************************************************************************

 @Function       RGXDeviceHasFeaturePower

 @Description    Checks if a device has a particular feature

 @Input          hPrivate     : Implementation specific data
 @Input          ui64Feature  : Feature to check

 @Return         IMG_TRUE if the given feature is available, IMG_FALSE otherwise

******************************************************************************/
IMG_BOOL RGXDeviceHasFeaturePower(const void *hPrivate, IMG_UINT64 ui64Feature);

/*!
*******************************************************************************

 @Function       RGXDeviceHasErnBrnPower

 @Description    Checks if a device has a particular errata

 @Input          hPrivate     : Implementation specific data
 @Input          ui64ErnsBrns : Flags to check

 @Return         IMG_TRUE if the given errata is available, IMG_FALSE otherwise

******************************************************************************/
IMG_BOOL RGXDeviceHasErnBrnPower(const void *hPrivate, IMG_UINT64 ui64ErnsBrns);

/*!
*******************************************************************************

 @Function       RGXGetDeviceSLCBanks

 @Description    Returns the number of SLC banks used by the device

 @Input          hPrivate    : Implementation specific data

 @Return         Number of SLC banks

******************************************************************************/
IMG_UINT32 RGXGetDeviceSLCBanks(const void *hPrivate);

/*!
*******************************************************************************

 @Function       RGXGetDeviceSLCSize

 @Description    Returns the device SLC size

 @Input          hPrivate    : Implementation specific data

 @Return         SLC size

******************************************************************************/
IMG_UINT32 RGXGetDeviceSLCSize(const void *hPrivate);

/*!
*******************************************************************************

 @Function       RGXGetDeviceCacheLineSize

 @Description    Returns the device cache line size

 @Input          hPrivate    : Implementation specific data

 @Return         Cache line size

******************************************************************************/
IMG_UINT32 RGXGetDeviceCacheLineSize(const void *hPrivate);

#if defined (__cplusplus)
}
#endif

#endif /* !defined (__RGXLAYER_KM_H__) */

