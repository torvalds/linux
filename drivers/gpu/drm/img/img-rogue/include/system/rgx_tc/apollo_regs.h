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

#if !defined(APOLLO_REGS_H)
#define APOLLO_REGS_H

#include "tc_clocks.h"

/* TC TCF5 */
#define TC5_SYS_APOLLO_REG_PCI_BASENUM (1)
#define TC5_SYS_APOLLO_REG_PDP2_OFFSET (0x800000)
#define TC5_SYS_APOLLO_REG_PDP2_SIZE   (0x7C4)

#define TC5_SYS_APOLLO_REG_PDP2_FBDC_OFFSET (0xA00000)
#define TC5_SYS_APOLLO_REG_PDP2_FBDC_SIZE   (0x14)

#define TC5_SYS_APOLLO_REG_HDMI_OFFSET (0xC00000)
#define TC5_SYS_APOLLO_REG_HDMI_SIZE   (0x1C)

/* TC ES2 */
#define TCF_TEMP_SENSOR_SPI_OFFSET	0xe
#define TCF_TEMP_SENSOR_TO_C(raw)	(((raw) * 248 / 4096) - 54)

/* Number of bytes that are broken */
#define SYS_DEV_MEM_BROKEN_BYTES	(1024 * 1024)
#define SYS_DEV_MEM_REGION_SIZE		(0x40000000 - SYS_DEV_MEM_BROKEN_BYTES)

/* Apollo reg on base register 0 */
#define SYS_APOLLO_REG_PCI_BASENUM	(0)
#define SYS_APOLLO_REG_REGION_SIZE	(0x00010000)

#define SYS_APOLLO_REG_SYS_OFFSET	(0x0000)
#define SYS_APOLLO_REG_SYS_SIZE		(0x0400)

#define SYS_APOLLO_REG_PLL_OFFSET	(0x1000)
#define SYS_APOLLO_REG_PLL_SIZE		(0x0400)

#define SYS_APOLLO_REG_HOST_OFFSET	(0x4050)
#define SYS_APOLLO_REG_HOST_SIZE	(0x0014)

#define SYS_APOLLO_REG_PDP1_OFFSET	(0xC000)
#define SYS_APOLLO_REG_PDP1_SIZE	(0x2000)

/* Offsets for flashing Apollo PROMs from base 0 */
#define APOLLO_FLASH_STAT_OFFSET	(0x4058)
#define APOLLO_FLASH_DATA_WRITE_OFFSET	(0x4050)
#define APOLLO_FLASH_RESET_OFFSET	(0x4060)

#define APOLLO_FLASH_FIFO_STATUS_MASK	 (0xF)
#define APOLLO_FLASH_FIFO_STATUS_SHIFT	 (0)
#define APOLLO_FLASH_PROGRAM_STATUS_MASK (0xF)
#define APOLLO_FLASH_PROGRAM_STATUS_SHIFT (16)

#define APOLLO_FLASH_PROG_COMPLETE_BIT	(0x1)
#define APOLLO_FLASH_PROG_PROGRESS_BIT	(0x2)
#define APOLLO_FLASH_PROG_FAILED_BIT	(0x4)
#define APOLLO_FLASH_INV_FILETYPE_BIT	(0x8)

#define APOLLO_FLASH_FIFO_SIZE		(8)

/* RGX reg on base register 1 */
#define SYS_RGX_REG_PCI_BASENUM		(1)
#define SYS_RGX_REG_REGION_SIZE		(0x7FFFF)

/* Device memory (including HP mapping) on base register 2 */
#define SYS_DEV_MEM_PCI_BASENUM		(2)

#endif /* APOLLO_REGS_H */
