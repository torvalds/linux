/*
 * Copyright (c) 2006-2009 NVIDIA Corporation.
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

/**
 * @file
 * <b>NVIDIA Tegra ODM Kit:
 *         NAND Memory Query Interface</b>
 *
 * @b Description: Defines the ODM query interface for NVIDIA NAND memory adaptation.
 *
 */
#ifndef INCLUDED_NVODM_QUERY_NAND_H
#define INCLUDED_NVODM_QUERY_NAND_H

#include "nvcommon.h"
#include "nvodm_modules.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * @defgroup nvodm_query_Nand NAND Memory Query Interface
 * This is the ODM query interface for NAND configurations.
 * @ingroup nvodm_query
 * @{
 */

#define FLASH_TYPE_SHIFT 16
#define DEVICE_SHIFT 8
#define FOURTH_ID_SHIFT 24
/**
 *  Defines the list of various capabilities of the NAND devices.
 */
typedef enum
{
    /// Specifies detected NAND device has only one plane; interleave not
    /// supported.
    SINGLE_PLANE,
    /// Specifies detected NAND device has only one plane; but interleave is
    /// supported for page programming.
    SINGLE_PLANE_INTERLEAVE,
    /// Specifies all types of multiplane capabilities should be declared after
    /// this.
    MULTI_PLANE,
    /// Specifies detected NAND device has multiple planes, and each plane is
    /// formed with alternate blocks from each bank.
    MULTIPLANE_ALT_BLOCK,
    /// Specifies detected NAND device has multiple planes, and each plane is
    /// formed with sequential blocks from each bank.
    MULTIPLANE_ALT_PLANE,
    /// Specifies detected NAND device has multiple planes, and each plane is
    /// formed with alternate blocks from each bank. Interleaving operation is
    /// supported across the banks.
    MULTIPLANE_ALT_BLOCK_INTERLEAVE,
    /// Specifies detected NAND device has multiple planes, and each plane is
    /// formed with sequential blocks from each bank. Interleaving operation is
    /// supported across the banks.
    MULTIPLANE_ALT_PLANE_INTERLEAVE
}NvOdmNandInterleaveCapability;

/**
 * Specifies the NAND Flash type.
 */
typedef enum
{
    /// Specifies NAND flash type is not known.
    NvOdmNandFlashType_UnKnown,
    /// Specifies SLC NAND flash type.
    NvOdmNandFlashType_Slc,
    /// Specifies MLC NAND flash type.
    NvOdmNandFlashType_Mlc,
    /// Ignore. Forces compilers to make 32-bit enums.
    NvOdmNandFlashType_Force32 = 0x7FFFFFFF
}NvOdmNandFlashType;

/// Defines the type of algorithm for error-correcting code (ECC).
typedef enum
{
    /// Specifies Hamming ECC.
    NvOdmNandECCAlgorithm_Hamming = 0,
    /// Specifies Reed-Solomon ECC.
    NvOdmNandECCAlgorithm_ReedSolomon,
    /// Specifies BCH ECC.
    NvOdmNandECCAlgorithm_BCH,
    /// Specifies to disable ECC, if the the NAND flash part being used
    /// has error correction capability within itself.
    NvOdmNandECCAlgorithm_NoEcc,
    /// Ignore. Forces compilers to make 32-bit enums.
    NvOdmNandECCAlgorithm_Force32 = 0x7FFFFFFF
}NvOdmNandECCAlgorithm;

/// Defines the number of skip spare bytes.
typedef enum
{
    NvOdmNandSkipSpareBytes_0,
    NvOdmNandSkipSpareBytes_4,
    NvOdmNandSkipSpareBytes_8,
    NvOdmNandSkipSpareBytes_12,
    NvOdmNandSkipSpareBytes_16,
    NvOdmNandSkipSpareBytes_Force32 = 0x7FFFFFFF
}NvOdmNandSkipSpareBytes;

/**
 * Defines the number of symbol errors correctable per each 512 continous
 * bytes of the flash area when Reed-Solomon algorithm is chosen for error
 * correction. Here each symbol is of 9 contiguous bits in the flash.
 *
 * @note Based on the chosen number of errors correctable, parity bytes
 * required to be stored in the spare area of NAND flash will vary. For 4
 * correctable errors the number of parity bytes required are 36 bytes.
 * Similarly, for 6 and 8 symbol error correction, 56 and 72 parity bytes
 * must be stored in the spare area. As we also must use the spare area for
 * bad block management and wear levelling, we need to have 12 bytes for that
 * in the spare area. So, the spare area size should be able to accommodate
 * parity bytes and bytes required for bad block management.
 * Hence fill this parameter based on the spare area size of the flash being
 * used.
 */
typedef enum
{
    /// Specifies 4 symbol error correction per 512 byte area of NAND flash.
    NvOdmNandNumberOfCorrectableSymbolErrors_Four,
    /// Specifies 6 symbol error correction per 512 byte area of NAND flash.
    NvOdmNandNumberOfCorrectableSymbolErrors_Six,
    /// Specifies 8 symbol error correction per 512 byte area of NAND flash.
    NvOdmNandNumberOfCorrectableSymbolErrors_Eight,
    /// Ignore. Forces compilers to make 32-bit enums.
    NvOdmNandNumberOfCorrectableSymbolErrors_Force32 = 0x7FFFFFFF
}NvOdmNandNumberOfCorrectableSymbolErrors;

/// Defines the NAND flash command set.
typedef enum
{
    /// Specifies to read command 1st cycle.
    NvOdmNandCommandList_Read = 0x00,
    /// Specifies to read command start 2nd cycle.
    NvOdmNandCommandList_Read_Start = 0x30,
    /// Specifies to read copy back 1st cycle.
    NvOdmNandCommandList_Read_Cpy_Bck = 0x00,
    /// Specifies to read copy back start 2nd cycle.
    NvOdmNandCommandList_Read_Cpy_Bck_Start = 0x35,
    /// Specifies to cache the read command.
    NvOdmNandCommandList_Cache_Read = 0x31,
    /// Specifies the last command to end cache read operation.
    NvOdmNandCommandList_Cache_ReadEnd = 0x3F,
    /// Specifies to read device ID.
    NvOdmNandCommandList_Read_Id = 0x90,
    /// Specifies to reset the device.
    NvOdmNandCommandList_Reset = 0xFF,
    /// Specifies to program/write page 1st cycle.
    NvOdmNandCommandList_Page_Program = 0x80,
    /// Specifies to program/write page 2nd cycle.
    NvOdmNandCommandList_Page_Program_Start = 0x10,
    /// Specifies to cache program 1st cycle.
    NvOdmNandCommandList_Cache_Program = 0x80,
    /// Specifies to cache program 2nd cycle.
    NvOdmNandCommandList_Cache_Program_Start = 0x15,
    /// Specifies to erase block.
    NvOdmNandCommandList_Block_Erase = 0x60,
    /// Specifies erase block start.
    NvOdmNandCommandList_Block_Erase_Start = 0xD0,
    /// Specifies copy back data.
    NvOdmNandCommandList_Copy_Back = 0x85,
    /// Specifies random data write.
    NvOdmNandCommandList_Random_Data_Input = 0x85,
    /// Specifies random data read.
    NvOdmNandCommandList_Random_Data_Out = 0x05,
    /// Specifies random data read start.
    NvOdmNandCommandList_Random_Data_Out_Start = 0xE0,
    /// Specifies multi page command.
    NvOdmNandCommandList_MultiPage = 0x11,
    NvOdmNandCommandList_MultiPageProgPlane2 = 0x81,
    /// Specifies read device status.
    NvOdmNandCommandList_Status = 0x70,
    /// Specifies read status of chip 1.
    NvOdmNandCommandList_Status_1 = 0xF1,
    /// Specifies read status of chip 2.
    NvOdmNandCommandList_Status_2 = 0xF2,
    /// Specifies ONFI read ID command.
    NvOdmNandCommandList_ONFIReadId = 0xEC,
    /// Ignore -- Forces compilers to make 32-bit enums.
    NvOdmNandCommandList_Force32 = 0x7FFFFFFF
}NvOdmNandCommandList;

/// Defines NAND flash types (42nm NAND or normal NAND).
typedef enum
{
    /// Specifies conventional NAND flash (50nm, 60nm).
    NvOdmNandDeviceType_Type1,
    /// Specifies 42nm technology NAND flash.
    NvOdmNandDeviceType_Type2,
    NvOdmNandDeviceType_Force32 = 0x7FFFFFFF
}NvOdmNandDeviceType;

/**
 * This structure holds various NAND flash parameters.
 */
typedef struct NvOdmNandFlashParamsRec
{
    /// Holds the vendor ID code.
    NvU8 VendorId;
    /// Holds the device ID code.
    NvU8 DeviceId;
    /// Holds the device type.
    NvOdmNandFlashType NandType;
    /// Holds the information whether the used NAND flash supports internal
    /// copy back command.
    NvBool IsCopyBackCommandSupported;
    /// Holds the information whether the used NAND flash supports cache
    /// write operations.
    NvBool IsCacheWriteSupported;
    /// Holds the size of the flash (in megabytes).
    NvU32 CapacityInMB;
    /// Holds the Zones per flash device--minimum value possible is 1.
    /// Zone is a group of contiguous blocks among which internal copy back can
    /// be performed, if the chip supports copy-back operation.
    /// Zone is also referred as plane or district by some flashes.
    NvU32 ZonesPerDevice;
    /// Holds the blocks per Zone of the flash.
    NvU32 BlocksPerZone;
    /// Holds the expected flash response for READ STATUS command
    /// when requested previous operation is successful.
    NvU32 OperationSuccessStatus;
    /// Holds the interleave mechanism supported by the flash.
    NvOdmNandInterleaveCapability InterleaveCapability;
    /// Holds the ECC algorithm to be used for error correction.
    NvOdmNandECCAlgorithm EccAlgorithm;
    /// Holds the number of errors that can be corrected per 512 byte area of NAND
    /// flash using Reed-Solomon algorithm.
    NvOdmNandNumberOfCorrectableSymbolErrors ErrorsCorrectable;
    /// Holds the number of bytes to be skipped in spare area, starting from
    /// spare byte 0.
    NvOdmNandSkipSpareBytes SkippedSpareBytes;
    /// Flash timing parameters, which are all to be filled in nSec.
    /// Holds read pulse width in nSec.
    NvU32 TRP;
    /// Holds read hold delay in nSec.
    NvU32 TRH;
    /// Holds write pulse width in nSec.
    NvU32 TWP;
    /// Holds write hold delay in nSec.
    NvU32 TWH;
    /// Holds CE# setup time.
    NvU32 TCS;
    /// Holds write hold to read delay in nSec.
    NvU32 TWHR;
    /// Holds WE to BSY set wait time in nSec.
    NvU32 TWB;
    /// Holds read pulse width for PIO read commands.
    NvU32 TREA;
    /// Holds time from final rising edge of WE of addrress input to
    /// first rising edge of WE for data input.
    NvU32 TADL;
    /*
        tCLH, tALH, tCH, tCLS, tALS params are also
        required to calculate tCS value.
    */
    /// Holds CLE setup time.
    NvU32 TCLS;
    /// Holds CLE hold time.
    NvU32 TCLH;
    /// Holds CE# hold time.
    NvU32 TCH;
    /// Holds ALE setup time.
    NvU32 TALS;
    /// Holds ALE hold time.
    NvU32 TALH;
    /// Holds Read Cycle hold time.
    NvU32 TRC;
    /// Holds Write Cycle hold time.
    NvU32 TWC;
    /// Holds CLE High to Read Delay Some data sheets refer it as TCLR.
    NvU32 TCR;
    /// Holds ALE High to Read Delay 
    NvU32 TAR;
    /// Holds RBSY High to Read Delay 
    NvU32 TRR;
    /// Describes whether the NAND is 42 nm NAND or normal.
    NvOdmNandDeviceType NandDeviceType;

    /// Holds the 4th ID data of the read ID command (as given by the data sheet)
    /// here to differentiate between 42 nm and other flashes that have the
    /// same ManufaturerId, DevId, and Flash type (e.g., K9LBG08U0M & K9LBG08U0D).
    NvU8 ReadIdFourthByte;
}NvOdmNandFlashParams;

/**
 * Gets the NAND flash device information.
 *
 * @param ReadID The NAND flash ID value that is read from the flash.
 * @return NULL if unsuccessful, or the appropriate flash params structure.
 */
NvOdmNandFlashParams *NvOdmNandGetFlashInfo (NvU32 ReadID);

/** @}*/

#if defined(__cplusplus)
}
#endif

#endif  // INCLUDED_NVODM_QUERY_NAND_H

