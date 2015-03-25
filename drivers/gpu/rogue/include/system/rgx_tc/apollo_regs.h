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

#if !defined(__APOLLO_REGS_H__)
#define __APOLLO_REGS_H__

#if defined(TC_APOLLO_ES2)
 /* TC ES2 */
 #define RGX_TC_CORE_CLOCK_SPEED	(90000000)
 #define RGX_TC_MEM_CLOCK_SPEED		(104000000)
 #define TCF_TEMP_SENSOR_SPI_OFFSET 	0xe
 #define TCF_TEMP_SENSOR_TO_C(raw) 	(((raw) * 248 / 4096) - 54)
#else
 /* TC ES1 */
 #define RGX_TC_CORE_CLOCK_SPEED	(90000000)
 #define RGX_TC_MEM_CLOCK_SPEED		(65000000)
#endif

/* Apollo reg on base register 0 */
#define SYS_APOLLO_REG_PCI_BASENUM	(0)
#define SYS_APOLLO_REG_REGION_SIZE	(0x00010000)

#define SYS_APOLLO_REG_SYS_OFFSET	(0x0000)
#define SYS_APOLLO_REG_SYS_SIZE		(0x0400)

#define SYS_APOLLO_REG_PLL_OFFSET	(0x1000)
#define SYS_APOLLO_REG_PLL_SIZE		(0x0400)

#define SYS_APOLLO_REG_HOST_OFFSET	(0x4050)
#define SYS_APOLLO_REG_HOST_SIZE	(0x0014)

#define SYS_APOLLO_REG_PDP_OFFSET	(0xC000)
#define SYS_APOLLO_REG_PDP_SIZE		(0x0400)

/* RGX reg on base register 1 */
#define SYS_RGX_REG_PCI_BASENUM		(1)
#define SYS_RGX_REG_REGION_SIZE		(0x00004000)

#endif /* if !defined(__APOLLO_REGS_H__) */
