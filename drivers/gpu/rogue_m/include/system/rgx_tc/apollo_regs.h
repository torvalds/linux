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

/*
 * The core clock speed is passed through a multiplier depending on the TC version.
 *
 * On TC_ES1: Multiplier = x3, final speed = 270MHz
 * On TC_ES2: Multiplier = x6, final speed = 540MHz
 * On TCF5:   Multiplier = 1x final speed = 45MHz
 *
 *
 * The base (unmultiplied speed) can be adjusted using a module parameter called "sys_core_clk_speed",
 * a number in Hz.
 *
 * As an example:
 *
 * PVR_SRVKM_PARAMS="sys_core_clk_speed=60000000" /etc/init.d/rc.pvr start
 *
 * would result in a core speed of 60MHz xMultiplier.
 *
 *
 *
 *
 * The memory clock is unmultiplied and can be adjusted using a module parameter called 
 * "sys_mem_clk_speed", this should be the number in Hz for the memory clock speed.
 * 
 * As an example:
 *
 * PVR_SRVKM_PARAMS="sys_mem_clk_speed=100000000" /etc/init.d/rc.pvr start
 *
 * would attempt to start the driver with the memory clock speed set to 100MHz.
 *
 *
 * Same applies to the system interface clock speed sys_sysif_clk_speed.
 * Needed for TCF5 but not for TC_ES2/ES1.
 * As an example:
 *
 * PVR_SRVKM_PARAMS="sys_mem_clk_speed=45000000" /etc/init.d/rc.pvr start
 *
 * would attempt to start the driver with the system clock speed set to 45MHz.
 *
 *
 * All parameters can be specified at once, eg:
 * PVR_SRVKM_PARAMS="sys_mem_clk_speed=MEMORY_SPEED sys_core_clk_speed=CORE_SPEED sys_mem_clk_speed=SYSIF_SPEED" /etc/init.d/rc.pvr start
 */

 /* TC TCF5 */
#define RGX_TC_SYS_CLOCK_SPEED		(45000000) /*< At the moment just used for TCF5 */

#if defined(TC_APOLLO_TCF5)
 #define RGX_TC_CORE_CLOCK_SPEED	(45000000)
 #define RGX_TC_MEM_CLOCK_SPEED		(45000000)

#elif defined(TC_APOLLO_BONNIE)
 /* TC Bonnie */
 #define RGX_TC_CORE_CLOCK_SPEED	(18000000)
 #define RGX_TC_MEM_CLOCK_SPEED		(65000000)

#elif defined(TC_APOLLO_ES2)
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

#define SYS_APOLLO_REG_PDP_OFFSET	(0xC000)
#define SYS_APOLLO_REG_PDP_SIZE		(0x0400)

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

#endif /* if !defined(__APOLLO_REGS_H__) */
