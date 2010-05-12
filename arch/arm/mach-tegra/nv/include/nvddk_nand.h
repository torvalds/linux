/*
 * Copyright (c) 2008-2009 NVIDIA Corporation.
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
 * <b> NVIDIA Driver Development Kit: NAND Flash Controller Interface</b>
 *
 * @b Description: This file declares the interface for the NAND module.
 */

#ifndef INCLUDED_NVDDK_NAND_H
#define INCLUDED_NVDDK_NAND_H

/**
 * @defgroup nvddk_nand NAND Flash Controller Interface
 *
 * This driver provides the interface to access external NAND flash devices
 * that are interfaced to the SOC.
 * It provides the APIs to access the NAND flash physically (in raw block number
 * and page numbers) and logically (in logical block number through 
 * block device interface).
 * It does not support any software ECC algorithms. It makes use of hardware ECC 
 * features supported by NAND Controller for validating the data.
 * It supports accessing NAND flash devices in interleave mode.
 *
 * @ingroup nvddk_modules
 * @{
 */

#include "nvcommon.h"
#include "nvos.h"
#include "nvrm_init.h"
#include "nvodm_query_nand.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NvDdkNandHandle is an opaque context to the NvDdkNandRec interface.
 */
typedef struct NvDdkNandRec *NvDdkNandHandle;


enum{ MAX_NAND_SUPPORTED = 8};


/**
 * NAND flash device information.
 */
typedef struct
{
    /// Vendor ID.
    NvU8 VendorId;
    /// Device ID.
    NvU8 DeviceId;
    /**
     * Redundant area size per page to write any tag information. This will
     * be calculated as:
     * <pre>    TagSize = spareAreaSize - mainAreaEcc - SpareAreaEcc </pre>
     * Shim layer is always supposed to request in multiples
     * of this number when spare area operations are requested.
     */
    NvU8 TagSize;
    /// Bus width of the chip: can be 8- or 16-bit.
    NvU8 BusWidth;
    /// Page size in bytes, includes only data area, no redundant area.
    NvU32 PageSize;
    /// Number of Pages per block.
    NvU32 PagesPerBlock;
    /// Total number of blocks that are present in the NAND flash device.
    NvU32 NoOfBlocks;
    /**
     * Holds the zones per flash device--minimum value possible is 1.
     * Zone is a group of contiguous blocks among which internal copy back can 
     * be performed, if the chip supports copy-back operation.
     * Zone is also referred as plane or district by some flashes.
     */
    NvU32 ZonesPerDevice;
    /**
     * Total device capacity in kilobytes.
     * Includes only data area, no redundant area.
     */
    NvU32 DeviceCapacityInKBytes;
    /// Interleave capability of the flash.
    NvOdmNandInterleaveCapability InterleaveCapability;
    /// Device type: SLC or MLC.
    NvOdmNandFlashType NandType;
    /// Number of NAND flash devices present on the board.
    NvU8 NumberOfDevices;
    // Size of Spare area
    NvU32 NumSpareAreaBytes;
    // Offset of Tag data in the spare area.
    NvU32 TagOffset;
}NvDdkNandDeviceInfo;

/**
 * Information related to a physical block.
 */
typedef struct 
{
    /// Tag information of the block.
    NvU8* pTagBuffer;
    /// Number of bytes to copy in tag buffer.
    NvU32 TagBufferSize;
    /// Determines whether the block is factory good block or not. 
    /// - NV_TRUE if factory good block.
    /// - NV_FALSE if factory bad block.
    NvBool IsFactoryGoodBlock;
    /// Gives the lock status of the block.
    NvBool IsBlockLocked;
}NandBlockInfo;


/**
 * NAND DDK capabilities.
 */
typedef struct
{
    /**
     * Flag indicating whether or not ECC is supported by the driver.
     * NV_TRUE means it supports ECC, else not supported.
     */
    NvBool IsEccSupported;
    /**
     * Flag indicating whether or not interleaving operation is
     * supported by the driver.
     * NV_TRUE means it supports interleaving, else not supported.
     */
    NvBool IsInterleavingSupported;
    /// Whether the command queue mode is supported by the SOC.
    NvBool IsCommandQueueModeSupported;
    /// Whether EDO mode is suported by the SOC.
    NvBool IsEdoModeSupported;
    /// Number of ECC parity bytes per spare area.
    NvU8 TagEccParitySize;
    /// Total number of NAND devices supported by SOC.
    NvU32 NumberOfDevicesSupported;
    /// Maximum data size that DMA can transfer.
    NvU32 MaxDataTransferSize;
    /// NAND controller default timing register value.
    NvU32 ControllerDefaultTiming;
    NvBool IsBCHEccSupported;
}NvDdkNandDriverCapabilities;

/**
 * The structure for locking of required NAND flash pages.
 */
typedef struct
{
    /// Device number of the flash being protected by lock feature.
    NvU8 DeviceNumber;
    /// Starting page number, from where NAND lock feature should protect data.
    NvU32 StartPageNumber;
    /// Ending page number, up to where NAND lock feature should protect data.
    NvU32 EndPageNumber;
}LockParams;

/*
 * Macro to get expression for modulo value that is power of 2
 * Expression: DIVIDEND % (pow(2, Log2X))
 */
#define MACRO_MOD_LOG2NUM(DIVIDEND, Log2X) \
            ((DIVIDEND) & ((1 << (Log2X)) - 1))

/*
 * Macro to get expression for multiply by number which is power of 2
 * Expression: VAL * (1 << Log2Num)
 */
#define MACRO_POW2_LOG2NUM(Log2Num) \
            (1 << (Log2Num))

/*
 * Macro to get expression for multiply by number which is power of 2
 * Expression: VAL * (1 << Log2Num)
 */
#define MACRO_MULT_POW2_LOG2NUM(VAL, Log2Num) \
            ((VAL) << (Log2Num))

/*
 * Macro to get expression for div by number that is power of 2
 * Expression: VAL / (1 << Log2Num)
 */
#define MACRO_DIV_POW2_LOG2NUM(VAL, Log2Num) \
            ((VAL) >> (Log2Num))

/**
 * Initializes the NAND Controller and returns a created handle to the client.
 * Only one instance of the handle can be created.
 *
 * @pre NAND client must call this API first before calling any further NAND APIs.
 *
 * @param hRmDevice Handle to RM device.
 * @param phNand Returns the created handle.
 *
 * @retval NvSuccess Initialization is successful.
 * @retval NvError_AlreadyAllocated The NAND device is already in use.
 */
NvError NvDdkNandOpen(NvRmDeviceHandle hRmDevice, NvDdkNandHandle *phNand);

/**
 * Closes the NAND controller and frees the handle.
 *
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 *
 */
void NvDdkNandClose(NvDdkNandHandle hNand);

/**
 * Reads the data from the selected NAND device(s) synchronously.
 *
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 * @param StartDeviceNum The Device number, which read operation has to be 
 *      started from. It starts from value '0'.
 * @param pPageNumbers A pointer to an array containing page numbers for
 *      each NAND Device. If there are (n + 1) NAND Devices, then 
 *      array size should be (n + 1).
 *      - pPageNumbers[0] gives page number to access in NAND Device 0.
 *      - pPageNumbers[1] gives page number to access in NAND Device 1.
 *      - ....................................
 *      - pPageNumbers[n] gives page number to access in NAND Device n.
 *      
 *      If NAND Device 'n' should not be accessed, fill pPageNumbers[n] as 
 *      0xFFFFFFFF.
 *      If the read starts from NAND Device 'n', all the page numbers 
 *      in the array should correspond to the same row, even though we don't 
 *      access the same row pages for '0' to 'n-1' Devices.
 * @param pDataBuffer A pointer to read the page data into. The size of buffer 
 *      should be (*pNoOfPages * PageSize).
 * @param pTagBuffer Pointer to read the tag data into. The size of buffer 
 *      should be (*pNoOfPages * TagSize).
 * @param pNoOfPages The number of pages to read. This count should include 
 *      only valid page count. Consder that total NAND devices present is 4,
 *      Need to read 1 page from Device1 and 1 page from Device3. In this case,
 *      \a StartDeviceNum should be 1 and Number of pages should be 2. 
 *      \a pPageNumbers[0] and \a pPageNumbers[2] should have 0xFFFFFFFF.
 *      \a pPageNumbers[1] and \a pPageNumbers[3] should have valid page numbers.
 *      The same pointer returns the number of pages read successfully.
 * @param IgnoreEccError If set to NV_TRUE, it ignores the ECC error and 
 *      continues to read the subsequent pages with out aborting read operation.
 *      This is required during bad block replacements.
 *
 * @retval NvSuccess NAND read operation completed successfully.
 * @retval NvError_NandReadEccFailed Indicates NAND read encountered ECC 
 *      errors that cannot be corrected.
 * @retval NvError_NandErrorThresholdReached Indicates NAND read encountered 
 *      correctable ECC errors and they are equal to the threshold value set.
 * @retval NvError_NandOperationFailed NAND read operation failed.
 */
NvError
NvDdkNandRead(
    NvDdkNandHandle hNand,
    NvU8 StartDeviceNum,
    NvU32* pPageNumbers,
    NvU8* const pDataBuffer,
    NvU8* const pTagBuffer,
    NvU32 *pNoOfPages,
    NvBool IgnoreEccError);

/**
 * Writes the data to the selected NAND device(s) synchronously.
 *
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 * @param StartDeviceNum The device number, which write operation has to be 
 *      started from. It starts from value '0'.
 * @param pPageNumbers A pointer to an array containing page numbers for
 *      each NAND Device. If there are (n + 1) NAND Devices, then 
 *      array size should be (n + 1).
 *      - pPageNumbers[0] gives page number to access in NAND Device 0.
 *      - pPageNumbers[1] gives page number to access in NAND Device 1.
 *      - ....................................
 *      - pPageNumbers[n] gives page number to access in NAND Device n.
 *      
 *      If NAND Device 'n' should not be accessed, fill \a pPageNumbers[n] as 
 *      0xFFFFFFFF.
 *      If the read starts from NAND device 'n', all the page numbers 
 *      in the array should correspond to the same row, even though we don't 
 *      access the same row pages for '0' to 'n-1' Devices.
 * @param pDataBuffer A pointer to read the page data into. The size of buffer 
 *      should be (*pNoOfPages * PageSize).
 * @param pTagBuffer Pointer to read the tag data into. The size of buffer 
 *      should be (*pNoOfPages * TagSize).
 * @param pNoOfPages The number of pages to write. This count should include 
 *      only valid page count. Consder that total NAND devices present is 4,
 *      Need to write 1 page to Device1 and 1 page to Device3. In this case,
 *      \a StartDeviceNum should be 1 and Number of pages should be 2.
 *      \a pPageNumbers[0] and \a pPageNumbers[2] should have 0xFFFFFFFF.
 *      \a pPageNumbers[1] and \a pPageNumbers[3] should have valid page numbers.
 *      The same pointer returns the number of pages written successfully.
 * 
 * @retval NvSuccess Operation completed successfully.
 * @retval NvError_NandOperationFailed Operation failed.
 */
NvError
NvDdkNandWrite(
    NvDdkNandHandle hNand,
    NvU8 StartDeviceNum,
    NvU32* pPageNumbers,
    const NvU8* pDataBuffer,
    const NvU8* pTagBuffer,
    NvU32 *pNoOfPages);

/**
 * Erases the selected blocks from the NAND device(s) synchronously.
 *
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 * @param StartDeviceNum The Device number, which erase operation has to be 
 *      started from. It starts from value '0'.
 * @param pPageNumbers A pointer to an array containing page numbers for
 *      each NAND Device. If there are (n + 1) NAND Devices, then 
 *      array size should be (n + 1).
 *      - pPageNumbers[0] gives page number to access in NAND Device 0.
 *      - pPageNumbers[1] gives page number to access in NAND Device 1.
 *      - ....................................
 *      - pPageNumbers[n] gives page number to access in NAND Device n.
 *      
 *      If NAND Device 'n' should not be accessed, fill pPageNumbers[n] as 
 *      0xFFFFFFFF.
 *      If the read starts from NAND device 'n', all the page numbers 
 *      in the array should correspond to the same row, even though we don't 
 *      access the same row pages for '0' to 'n-1' Devices.
 * @param pNumberOfBlocks The number of blocks to erase. This count should include 
 *      only valid block count. Consder that total NAND devices present is 4,
 *      Need to erase 1 block from Device1 and 1 block from Device3. In this case,
 *      \a StartDeviceNum should be 1 and Number of blocks should be 2.
 *      \a pPageNumbers[0] and \a pPageNumbers[2] should have 0xFFFFFFFF.
 *      \a pPageNumbers[1] and \a pPageNumbers[3] should have valid page numbers
 *      corresponding to blocks.
 *      The same pointer returns the number of blocks erased successfully.
 *
 * @retval NvSuccess Operation completed successfully.
 * @retval NvError_NandOperationFailed Operation failed.
 */
NvError
NvDdkNandErase(
    NvDdkNandHandle hNand,
    NvU8 StartDeviceNum,
    NvU32* pPageNumbers,
    NvU32* pNumberOfBlocks);

/**
 * Copies the data in the source page(s) to the destination page(s) 
 *      synchronously.
 *
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 * @param SrcStartDeviceNum The device number, from which data has to be read 
 *      for the copy back operation. It starts from value '0'.
 * @param DstStartDeviceNum The device number, to which data has to be copied 
 *      for the copy back operation. It starts from value '0'.
 * @param pSrcPageNumbers A pointer to an array containing page numbers for
 *      each NAND Device. If there are (n + 1) NAND Devices, then 
 *      array size should be (n + 1).
 *      - pSrcPageNumbers[0] gives page number to access in NAND Device 0.
 *      - pSrcPageNumbers[1] gives page number to access in NAND Device 1.
 *      - ....................................
 *      - pSrcPageNumbers[n] gives page number to access in NAND Device n.
 *     
 *      If NAND Device 'n' should not be accessed, fill \a pSrcPageNumbers[n] as 
 *      0xFFFFFFFF.
 *      If the copy-back starts from NAND devices 'n', all the page numbers 
 *      in the array should correspond to the same row, even though we don't 
 *      access the same row pages for '0' to 'n-1' Devices.
 * @param pDestPageNumbers A pointer to an array containing page numbers for
 *      each NAND Device. If there are (n + 1) NAND Devices, then 
 *      array size should be (n + 1).
 *      - pDestPageNumbers[0] gives page number to access in NAND Device 0.
 *      - pDestPageNumbers[1] gives page number to access in NAND Device 1.
 *      - ....................................
 *      - pDestPageNumbers[n] gives page number to access in NAND Device n.
 *     
 *      If NAND Device 'n' should not be accessed, fill \a pDestPageNumbers[n] as 
 *      0xFFFFFFFF.
 *      If the Copy-back starts from Interleave column 'n', all the page numbers 
 *      in the array should correspond to the same row, even though we don't 
 *      access the same row pages for '0' to 'n-1' Devices.
 * @param pNoOfPages The number of pages to copy-back. This count should include 
 *      only valid page count. Consider that total NAND devices present is 4,
 *      Need to Copy-back 1 page from Device1 and 1 page from Device3. In this 
 *      case, \a StartDeviceNum should be 1 and Number of pages should be 2.
 *      \a pSrcPageNumbers[0], \a pSrcPageNumbers[2], \a pDestPageNumbers[0] and 
 *      \a pDestPageNumbers[2] should have 0xFFFFFFFF. \a pSrcPageNumbers[1], 
 *      \a pSrcPageNumbers[3], \a pDestPageNumbers[1] and \a pDestPageNumbers[3]
 *      should have valid page numbers.
 *      The same pointer returns the number of pages copied-back successfully.
 * @param IgnoreEccError NV_TRUE to ingnore ECC errors, NV_FALSE otherwise.
 * 
 * @retval NvSuccess Operation completed successfully
 * @retval NvError_NandOperationFailed Operation failed.
 */
NvError
NvDdkNandCopybackPages(
    NvDdkNandHandle hNand,
    NvU8 SrcStartDeviceNum,
    NvU8 DstStartDeviceNum,
    NvU32* pSrcPageNumbers,
    NvU32* pDestPageNumbers,
    NvU32 *pNoOfPages,
    NvBool IgnoreEccError);

/**
 * Gets the NAND flash device information.
 *
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 * @param DeviceNumber NAND flash device number.
 * @param pDeviceInfo Returns the device information.
 *
 * @retval NvSuccess Operation completed successfully.
 * @retval NvError_NandOperationFailed NAND copy back operation failed.
 */
 NvError
 NvDdkNandGetDeviceInfo(
    NvDdkNandHandle hNand,
    NvU8 DeviceNumber,
    NvDdkNandDeviceInfo* pDeviceInfo);

/**
 * Locks the specified NAND flash pages.
 *
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 * @param pFlashLockParams A pointer to the range of pages to be locked.
 */
void 
NvDdkNandSetFlashLock(
    NvDdkNandHandle hNand,
    LockParams* pFlashLockParams);

/**
 * Returns the details of the locked apertures, like device number, starting 
 * page number, ending page number of the region locked.
 *
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 * @param pFlashLockParams A pointer to first array element of \a LockParams type 
 * with eight elements in the array. 
 * Check if \a pFlashLockParams[i].DeviceNumber == 0xFF, then that aperture is 
 * free to use for locking.
 */
void 
NvDdkNandGetLockedRegions(
    NvDdkNandHandle hNand,
    LockParams* pFlashLockParams);
/**
 * Releases all regions that were locked using NvDdkNandSetFlashLock API.
 *
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 */
void NvDdkNandReleaseFlashLock(NvDdkNandHandle hNand);

/**
 * Gets the NAND driver capabilities.
 *
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 * @param pNandDriverCapabilities Returns the capabilities.
 *
 */
void
NvDdkNandGetCapabilities(
    NvDdkNandHandle hNand,
    NvDdkNandDriverCapabilities* pNandDriverCapabilities);

/**
 * Gives the block specific information such as tag information, lock status, block good/bad.
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 * @param DeviceNumber Device number in which the requested block exists.
 * @param BlockNumber Requested physical block number.
 * @param pBlockInfo Return the block information.
 * @param SkippedBytesReadEnable NV_TRUE enables reading skipped bytes.
 *
 * @retval NvSuccess Success
 */
NvError
NvDdkNandGetBlockInfo(
    NvDdkNandHandle hNand,
    NvU32 DeviceNumber,
    NvU32 BlockNumber,
    NandBlockInfo* pBlockInfo,
    NvBool SkippedBytesReadEnable);

/**
 * Part of static power management, call this API to put the NAND controller
 * into suspend state. This API is a mechanism for client to augment OS
 * power management policy.
 *
 * The h/w context of the NAND controller is saved. Clock is disabled and power
 * is also disabled to the controller.
 *
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 *
 * @retval NvSuccess Success
 * @retval NvError_BadParameter Invalid input parameter value
 */
NvError NvDdkNandSuspend(NvDdkNandHandle hNand);

/**
 * Part of static power management, call this API to wake the NAND controller
 * from suspend state. This API is a mechanism for client to augment OS power
 * management policy.
 *
 * The h/w context of the NAND controller is restored. Clock is enabled and power
 * is also enabled to the controller
 *
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 *
 * @retval NvSuccess Success
 * @retval NvError_BadParameter Invalid input parameter value
 */
NvError NvDdkNandResume(NvDdkNandHandle hNand);

/**
 * Part of local power management of the driver. Call this API to turn off the 
 * clocks required for NAND controller operation.
 *
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 *
 * @retval NvSuccess Success
 * @retval NvError_BadParameter Invalid input parameter value
 */
NvError NvDdkNandSuspendClocks(NvDdkNandHandle hNand);

/**
 * Part of local power management of the driver. Call this API to turn on the 
 * clocks required for NAND controller operation.
 *
 * @param hNand Handle to the NAND, which is returned by NvDdkNandOpen().
 *
 * @retval NvSuccess Success
 * @retval NvError_BadParameter Invalid input parameter value
 */
NvError NvDdkNandResumeClocks(NvDdkNandHandle hNand);

/**
 *  API to read to the spare area.
 */
NvError
NvDdkNandReadSpare(
    NvDdkNandHandle hNand,
    NvU8 StartDeviceNum,
    NvU32* pPageNumbers,
    NvU8* const pSpareBuffer,
    NvU8 OffsetInSpareAreaInBytes,
    NvU8 NumSpareAreaBytes);

/**
 *  API to write to the spare area. Use this API with caution, as there is a
 *  risk of overriding the factory bad block data.
 */
NvError
NvDdkNandWriteSpare(
    NvDdkNandHandle hNand,
    NvU8 StartDeviceNum,
    NvU32* pPageNumbers,
    NvU8* const pSpareBuffer,
    NvU8 OffsetInSpareAreaInBytes,
    NvU8 NumSpareAreaBytes);

/*
 * Functions shared between Ddk Nand, block driver and FTL code
 */
// Function to compare buffer contents
NvU32 NandUtilMemcmp(const void *pSrc, const void *pDst, NvU32 Size);

// Simple function to get log2, assumed value power of 2, else return 
// log2 for immediately smaller number
NvU8 NandUtilGetLog2(NvU32 Val);

#ifdef __cplusplus
}
#endif

/** @} */
#endif // INCLUDED_NVDDK_NAND_H
