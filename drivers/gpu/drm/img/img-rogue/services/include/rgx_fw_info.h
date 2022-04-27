/*************************************************************************/ /*!
@File
@Title          FW image information

@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    Utility functions used internally for HWPerf data retrieval
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

#if !defined(RGX_FW_INFO_H)
#define RGX_FW_INFO_H

#include "img_types.h"
#include "rgx_common.h"

/*
 * Firmware binary block unit in bytes.
 * Raw data stored in FW binary will be aligned to this size.
 */
#define FW_BLOCK_SIZE 4096L

typedef enum
{
	META_CODE = 0,
	META_PRIVATE_DATA,
	META_COREMEM_CODE,
	META_COREMEM_DATA,
	MIPS_CODE,
	MIPS_EXCEPTIONS_CODE,
	MIPS_BOOT_CODE,
	MIPS_PRIVATE_DATA,
	MIPS_BOOT_DATA,
	MIPS_STACK,
	RISCV_UNCACHED_CODE,
	RISCV_CACHED_CODE,
	RISCV_PRIVATE_DATA,
	RISCV_COREMEM_CODE,
	RISCV_COREMEM_DATA,
} RGX_FW_SECTION_ID;

typedef enum
{
	NONE = 0,
	FW_CODE,
	FW_DATA,
	FW_COREMEM_CODE,
	FW_COREMEM_DATA
} RGX_FW_SECTION_TYPE;


/*
 * FW binary format with FW info attached:
 *
 *          Contents        Offset
 *     +-----------------+
 *     |                 |    0
 *     |                 |
 *     | Original binary |
 *     |      file       |
 *     |   (.ldr/.elf)   |
 *     |                 |
 *     |                 |
 *     +-----------------+
 *     | FW info header  |  FILE_SIZE - 4K
 *     +-----------------+
 *     |                 |
 *     | FW layout table |
 *     |                 |
 *     +-----------------+
 *                          FILE_SIZE
 */

#define FW_INFO_VERSION  (1)

typedef struct
{
	IMG_UINT32 ui32InfoVersion;      /* FW info version */
	IMG_UINT32 ui32HeaderLen;        /* Header length */
	IMG_UINT32 ui32LayoutEntryNum;   /* Number of entries in the layout table */
	IMG_UINT32 ui32LayoutEntrySize;  /* Size of an entry in the layout table */
	IMG_UINT64 RGXFW_ALIGN ui64BVNC; /* BVNC */
	IMG_UINT32 ui32FwPageSize;       /* Page size of processor on which firmware executes */
	IMG_UINT32 ui32Flags;            /* Compatibility flags */
} RGX_FW_INFO_HEADER;

typedef struct
{
	RGX_FW_SECTION_ID eId;
	RGX_FW_SECTION_TYPE eType;
	IMG_UINT32 ui32BaseAddr;
	IMG_UINT32 ui32MaxSize;
	IMG_UINT32 ui32AllocSize;
	IMG_UINT32 ui32AllocOffset;
} RGX_FW_LAYOUT_ENTRY;

#endif /* RGX_FW_INFO_H */

/******************************************************************************
 End of file (rgx_fw_info.h)
******************************************************************************/
