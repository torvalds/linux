/*************************************************************************/ /*!
@File           sysconfig.h
@Title          System Configuration
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@Description    System Configuration functions
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

#include "pvrsrv_device.h"
#include "rgxdevice.h"

#if !defined(__SYSCCONFIG_H__)
#define __SYSCCONFIG_H__

#define SYS_RGX_ACTIVE_POWER_LATENCY_MS 100

#define DEFAULT_CLOCK_RATE			IMG_UINT64_C(600000000)

/* fixed IPA Base of the memory carveout reserved for the GPU Firmware Heaps */
#define FW_CARVEOUT_IPA_BASE		IMG_UINT64_C(0x7E000000)

/* fixed IPA Base of the memory carveout reserved for the Firmware's Page Tables */
#define FW_PT_CARVEOUT_IPA_BASE		IMG_UINT64_C(0x8F000000)

/* mock SoC registers */
#define SOC_REGBANK_BASE			IMG_UINT64_C(0xF0000000)
#define SOC_REGBANK_SIZE			IMG_UINT32_C(0x10000)
#define POW_DOMAIN_ENABLE_REG		IMG_UINT32_C(0xA000)
#define POW_DOMAIN_DISABLE_REG		IMG_UINT32_C(0xA008)
#define POW_DOMAIN_STATUS_REG		IMG_UINT32_C(0xA010)

#define POW_DOMAIN_GPU				IMG_UINT32_C(0x1)

#define MPU_EVENT_STATUS_REG				IMG_UINT32_C(0xB000)
#define MPU_EVENT_OSID_REG					IMG_UINT32_C(0xB008)
#define MPU_EVENT_ADDRESS_REG				IMG_UINT32_C(0xB010)
#define MPU_EVENT_DIRECTION_REG				IMG_UINT32_C(0xB018)
#define MPU_EVENT_CLEAR_REG					IMG_UINT32_C(0xB020)

#define MPU_GPU_BUS_REQUESTER				IMG_UINT32_C(1)
#define MPU_WRITE_ACCESS					IMG_UINT32_C(1)

#define MPU_PROTECTED_RANGE0_START_REG		IMG_UINT32_C(0xC000)
#define MPU_PROTECTED_RANGE1_START_REG		IMG_UINT32_C(0xC008)
#define MPU_PROTECTED_RANGE2_START_REG		IMG_UINT32_C(0xC010)
#define MPU_PROTECTED_RANGE3_START_REG		IMG_UINT32_C(0xC018)
#define MPU_PROTECTED_RANGE4_START_REG		IMG_UINT32_C(0xC020)
#define MPU_PROTECTED_RANGE5_START_REG		IMG_UINT32_C(0xC028)
#define MPU_PROTECTED_RANGE6_START_REG		IMG_UINT32_C(0xC030)
#define MPU_PROTECTED_RANGE7_START_REG		IMG_UINT32_C(0xC038)
#define MPU_PROTECTED_RANGE8_START_REG		IMG_UINT32_C(0xC040)
#define MPU_PROTECTED_RANGE9_START_REG		IMG_UINT32_C(0xC048)
#define MPU_PROTECTED_RANGE10_START_REG		IMG_UINT32_C(0xC050)
#define MPU_PROTECTED_RANGE11_START_REG		IMG_UINT32_C(0xC058)
#define MPU_PROTECTED_RANGE12_START_REG		IMG_UINT32_C(0xC060)
#define MPU_PROTECTED_RANGE13_START_REG		IMG_UINT32_C(0xC068)
#define MPU_PROTECTED_RANGE14_START_REG		IMG_UINT32_C(0xC070)
#define MPU_PROTECTED_RANGE15_START_REG		IMG_UINT32_C(0xC078)

#define MPU_PROTECTED_RANGE0_END_REG		IMG_UINT32_C(0xC100)
#define MPU_PROTECTED_RANGE1_END_REG		IMG_UINT32_C(0xC108)
#define MPU_PROTECTED_RANGE2_END_REG		IMG_UINT32_C(0xC110)
#define MPU_PROTECTED_RANGE3_END_REG		IMG_UINT32_C(0xC118)
#define MPU_PROTECTED_RANGE4_END_REG		IMG_UINT32_C(0xC120)
#define MPU_PROTECTED_RANGE5_END_REG		IMG_UINT32_C(0xC128)
#define MPU_PROTECTED_RANGE6_END_REG		IMG_UINT32_C(0xC130)
#define MPU_PROTECTED_RANGE7_END_REG		IMG_UINT32_C(0xC138)
#define MPU_PROTECTED_RANGE8_END_REG		IMG_UINT32_C(0xC140)
#define MPU_PROTECTED_RANGE9_END_REG		IMG_UINT32_C(0xC148)
#define MPU_PROTECTED_RANGE10_END_REG		IMG_UINT32_C(0xC150)
#define MPU_PROTECTED_RANGE11_END_REG		IMG_UINT32_C(0xC158)
#define MPU_PROTECTED_RANGE12_END_REG		IMG_UINT32_C(0xC160)
#define MPU_PROTECTED_RANGE13_END_REG		IMG_UINT32_C(0xC168)
#define MPU_PROTECTED_RANGE14_END_REG		IMG_UINT32_C(0xC170)
#define MPU_PROTECTED_RANGE15_END_REG		IMG_UINT32_C(0xC178)


#define MPU_PROTECTED_RANGE0_OSID_REG		IMG_UINT32_C(0xC200)
#define MPU_PROTECTED_RANGE1_OSID_REG		IMG_UINT32_C(0xC208)
#define MPU_PROTECTED_RANGE2_OSID_REG		IMG_UINT32_C(0xC210)
#define MPU_PROTECTED_RANGE3_OSID_REG		IMG_UINT32_C(0xC218)
#define MPU_PROTECTED_RANGE4_OSID_REG		IMG_UINT32_C(0xC220)
#define MPU_PROTECTED_RANGE5_OSID_REG		IMG_UINT32_C(0xC228)
#define MPU_PROTECTED_RANGE6_OSID_REG		IMG_UINT32_C(0xC230)
#define MPU_PROTECTED_RANGE7_OSID_REG		IMG_UINT32_C(0xC238)
#define MPU_PROTECTED_RANGE8_OSID_REG		IMG_UINT32_C(0xC240)
#define MPU_PROTECTED_RANGE9_OSID_REG		IMG_UINT32_C(0xC248)
#define MPU_PROTECTED_RANGE10_OSID_REG		IMG_UINT32_C(0xC250)
#define MPU_PROTECTED_RANGE11_OSID_REG		IMG_UINT32_C(0xC258)
#define MPU_PROTECTED_RANGE12_OSID_REG		IMG_UINT32_C(0xC260)
#define MPU_PROTECTED_RANGE13_OSID_REG		IMG_UINT32_C(0xC268)
#define MPU_PROTECTED_RANGE14_OSID_REG		IMG_UINT32_C(0xC270)
#define MPU_PROTECTED_RANGE15_OSID_REG		IMG_UINT32_C(0xC278)

#define MPU_PROTECTION_ENABLE_REG			IMG_UINT32_C(0xC300)


/*****************************************************************************
 * system specific data structures
 *****************************************************************************/

#endif /* __SYSCCONFIG_H__ */
