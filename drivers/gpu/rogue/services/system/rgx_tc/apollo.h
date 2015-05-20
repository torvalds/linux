/*************************************************************************/ /*!
@File		
@Title          System Description Header
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    This header provides system-specific declarations and macros
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

#if !defined(__APOLLO_H__)
#define __APOLLO_H__

#define TC_SYSTEM_NAME			"Rogue Test Chip"

/* Valid values for the TC_MEMORY_CONFIG configuration option */
#define TC_MEMORY_LOCAL			(1)
#define TC_MEMORY_HOST			(2)
#define TC_MEMORY_HYBRID		(3)
#define TC_MEMORY_DIRECT_MAPPED		(4)

#if defined(SUPPORT_DISPLAY_CLASS) || defined(SUPPORT_DRM_DC_MODULE)
/* Memory reserved for use by the PDP DC. */
#define RGX_TC_RESERVE_DC_MEM_SIZE	(32 * 1024 * 1024)
#endif

#if defined(SUPPORT_ION)
/* Memory reserved for use by ion. */
#define RGX_TC_RESERVE_ION_MEM_SIZE	(384 * 1024 * 1024)
#endif

/* Offsets for flashing Apollo PROMs from base 0 */
#define APOLLO_FLASH_STAT_OFFSET	(0x4058)
#define APOLLO_FLASH_DATA_WRITE_OFFSET	(0x4050)
#define APOLLO_FLASH_RESET_OFFSET	(0x4060)

#define APOLLO_FLASH_FIFO_STATUS_MASK 	 (0xF)
#define APOLLO_FLASH_FIFO_STATUS_SHIFT 	 (0)
#define APOLLO_FLASH_PROGRAM_STATUS_MASK (0xF)
#define APOLLO_FLASH_PROGAM_STATUS_SHIFT (16)

#define APOLLO_FLASH_PROG_COMPLETE_BIT	(0x1)
#define APOLLO_FLASH_PROG_PROGRESS_BIT	(0x2)
#define APOLLO_FLASH_PROG_FAILED_BIT	(0x4)
#define APOLLO_FLASH_INV_FILETYPE_BIT	(0x8)

#define APOLLO_FLASH_FIFO_SIZE		(8)

/* RGX reg on base register 1 */
#define SYS_RGX_REG_PCI_BASENUM		(1)
#define SYS_RGX_REG_REGION_SIZE		(0x00004000)

/* Device memory (including HP mapping) on base register 2 */
#define SYS_DEV_MEM_PCI_BASENUM		(2)

/* Number of bytes that are broken */
#define SYS_DEV_MEM_BROKEN_BYTES	(1024 * 1024)
#define SYS_DEV_MEM_REGION_SIZE		(0x40000000 - SYS_DEV_MEM_BROKEN_BYTES)

#endif /* if !defined(__APOLLO_H__) */
