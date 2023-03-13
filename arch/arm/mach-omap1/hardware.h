/*
 * Hardware definitions for TI OMAP processors and boards
 *
 * NOTE: Please put device driver specific defines into a separate header
 *	 file for each driver.
 *
 * Copyright (C) 2001 RidgeRun, Inc.
 * Author: RidgeRun, Inc. Greg Lonnon <glonnon@ridgerun.com>
 *
 * Reorganized for Linux-2.6 by Tony Lindgren <tony@atomide.com>
 *                          and Dirk Behme <dirk.behme@de.bosch.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ASM_ARCH_OMAP_HARDWARE_H
#define __ASM_ARCH_OMAP_HARDWARE_H

#include <linux/sizes.h>
#include <linux/soc/ti/omap1-io.h>
#ifndef __ASSEMBLER__
#include <asm/types.h>
#include <linux/soc/ti/omap1-soc.h>

#include "tc.h"

/* Almost all documentation for chip and board memory maps assumes
 * BM is clear.  Most devel boards have a switch to control booting
 * from NOR flash (using external chipselect 3) rather than mask ROM,
 * which uses BM to interchange the physical CS0 and CS3 addresses.
 */
static inline u32 omap_cs0m_phys(void)
{
	return (omap_readl(EMIFS_CONFIG) & OMAP_EMIFS_CONFIG_BM)
			?  OMAP_CS3_PHYS : 0;
}

static inline u32 omap_cs3_phys(void)
{
	return (omap_readl(EMIFS_CONFIG) & OMAP_EMIFS_CONFIG_BM)
			? 0 : OMAP_CS3_PHYS;
}

#endif	/* ifndef __ASSEMBLER__ */

#define OMAP1_IO_OFFSET		0x00f00000	/* Virtual IO = 0xff0b0000 */
#define OMAP1_IO_ADDRESS(pa)	IOMEM((pa) - OMAP1_IO_OFFSET)

#include "serial.h"

/*
 * ---------------------------------------------------------------------------
 * Common definitions for all OMAP processors
 * NOTE: Put all processor or board specific parts to the special header
 *	 files.
 * ---------------------------------------------------------------------------
 */

/*
 * ----------------------------------------------------------------------------
 * Timers
 * ----------------------------------------------------------------------------
 */
#define OMAP_MPU_TIMER1_BASE	(0xfffec500)
#define OMAP_MPU_TIMER2_BASE	(0xfffec600)
#define OMAP_MPU_TIMER3_BASE	(0xfffec700)
#define MPU_TIMER_FREE		(1 << 6)
#define MPU_TIMER_CLOCK_ENABLE	(1 << 5)
#define MPU_TIMER_AR		(1 << 1)
#define MPU_TIMER_ST		(1 << 0)

/*
 * ---------------------------------------------------------------------------
 * Watchdog timer
 * ---------------------------------------------------------------------------
 */

/* Watchdog timer within the OMAP3.2 gigacell */
#define OMAP_MPU_WATCHDOG_BASE	(0xfffec800)
#define OMAP_WDT_TIMER		(OMAP_MPU_WATCHDOG_BASE + 0x0)
#define OMAP_WDT_LOAD_TIM	(OMAP_MPU_WATCHDOG_BASE + 0x4)
#define OMAP_WDT_READ_TIM	(OMAP_MPU_WATCHDOG_BASE + 0x4)
#define OMAP_WDT_TIMER_MODE	(OMAP_MPU_WATCHDOG_BASE + 0x8)

/*
 * ---------------------------------------------------------------------------
 * Interrupts
 * ---------------------------------------------------------------------------
 */
#ifdef CONFIG_ARCH_OMAP1

/*
 * XXX: These probably want to be moved to arch/arm/mach-omap/omap1/irq.c
 * or something similar.. -- PFM.
 */

#define OMAP_IH1_BASE		0xfffecb00
#define OMAP_IH2_BASE		0xfffe0000
#define OMAP_IH2_0_BASE		(0xfffe0000)
#define OMAP_IH2_1_BASE		(0xfffe0100)
#define OMAP_IH2_2_BASE		(0xfffe0200)
#define OMAP_IH2_3_BASE		(0xfffe0300)

#define OMAP_IH1_ITR		(OMAP_IH1_BASE + 0x00)
#define OMAP_IH1_MIR		(OMAP_IH1_BASE + 0x04)
#define OMAP_IH1_SIR_IRQ	(OMAP_IH1_BASE + 0x10)
#define OMAP_IH1_SIR_FIQ	(OMAP_IH1_BASE + 0x14)
#define OMAP_IH1_CONTROL	(OMAP_IH1_BASE + 0x18)
#define OMAP_IH1_ILR0		(OMAP_IH1_BASE + 0x1c)
#define OMAP_IH1_ISR		(OMAP_IH1_BASE + 0x9c)

#define OMAP_IH2_ITR		(OMAP_IH2_BASE + 0x00)
#define OMAP_IH2_MIR		(OMAP_IH2_BASE + 0x04)
#define OMAP_IH2_SIR_IRQ	(OMAP_IH2_BASE + 0x10)
#define OMAP_IH2_SIR_FIQ	(OMAP_IH2_BASE + 0x14)
#define OMAP_IH2_CONTROL	(OMAP_IH2_BASE + 0x18)
#define OMAP_IH2_ILR0		(OMAP_IH2_BASE + 0x1c)
#define OMAP_IH2_ISR		(OMAP_IH2_BASE + 0x9c)

#define OMAP_IH2_0_ITR		(OMAP_IH2_0_BASE + 0x00)
#define OMAP_IH2_0_MIR		(OMAP_IH2_0_BASE + 0x04)
#define OMAP_IH2_0_SIR_IRQ	(OMAP_IH2_0_BASE + 0x10)
#define OMAP_IH2_0_SIR_FIQ	(OMAP_IH2_0_BASE + 0x14)
#define OMAP_IH2_0_CONTROL	(OMAP_IH2_0_BASE + 0x18)
#define OMAP_IH2_0_ILR0		(OMAP_IH2_0_BASE + 0x1c)
#define OMAP_IH2_0_ISR		(OMAP_IH2_0_BASE + 0x9c)

#define OMAP_IH2_1_ITR		(OMAP_IH2_1_BASE + 0x00)
#define OMAP_IH2_1_MIR		(OMAP_IH2_1_BASE + 0x04)
#define OMAP_IH2_1_SIR_IRQ	(OMAP_IH2_1_BASE + 0x10)
#define OMAP_IH2_1_SIR_FIQ	(OMAP_IH2_1_BASE + 0x14)
#define OMAP_IH2_1_CONTROL	(OMAP_IH2_1_BASE + 0x18)
#define OMAP_IH2_1_ILR1		(OMAP_IH2_1_BASE + 0x1c)
#define OMAP_IH2_1_ISR		(OMAP_IH2_1_BASE + 0x9c)

#define OMAP_IH2_2_ITR		(OMAP_IH2_2_BASE + 0x00)
#define OMAP_IH2_2_MIR		(OMAP_IH2_2_BASE + 0x04)
#define OMAP_IH2_2_SIR_IRQ	(OMAP_IH2_2_BASE + 0x10)
#define OMAP_IH2_2_SIR_FIQ	(OMAP_IH2_2_BASE + 0x14)
#define OMAP_IH2_2_CONTROL	(OMAP_IH2_2_BASE + 0x18)
#define OMAP_IH2_2_ILR2		(OMAP_IH2_2_BASE + 0x1c)
#define OMAP_IH2_2_ISR		(OMAP_IH2_2_BASE + 0x9c)

#define OMAP_IH2_3_ITR		(OMAP_IH2_3_BASE + 0x00)
#define OMAP_IH2_3_MIR		(OMAP_IH2_3_BASE + 0x04)
#define OMAP_IH2_3_SIR_IRQ	(OMAP_IH2_3_BASE + 0x10)
#define OMAP_IH2_3_SIR_FIQ	(OMAP_IH2_3_BASE + 0x14)
#define OMAP_IH2_3_CONTROL	(OMAP_IH2_3_BASE + 0x18)
#define OMAP_IH2_3_ILR3		(OMAP_IH2_3_BASE + 0x1c)
#define OMAP_IH2_3_ISR		(OMAP_IH2_3_BASE + 0x9c)

#define IRQ_ITR_REG_OFFSET	0x00
#define IRQ_MIR_REG_OFFSET	0x04
#define IRQ_SIR_IRQ_REG_OFFSET	0x10
#define IRQ_SIR_FIQ_REG_OFFSET	0x14
#define IRQ_CONTROL_REG_OFFSET	0x18
#define IRQ_ISR_REG_OFFSET	0x9c
#define IRQ_ILR0_REG_OFFSET	0x1c
#define IRQ_GMR_REG_OFFSET	0xa0

#endif

/* Timer32K for 1610 and 1710*/
#define OMAP_TIMER32K_BASE	0xFFFBC400

/*
 * ---------------------------------------------------------------------------
 * TIPB bus interface
 * ---------------------------------------------------------------------------
 */
#define TIPB_PUBLIC_CNTL_BASE		0xfffed300
#define MPU_PUBLIC_TIPB_CNTL		(TIPB_PUBLIC_CNTL_BASE + 0x8)
#define TIPB_PRIVATE_CNTL_BASE		0xfffeca00
#define MPU_PRIVATE_TIPB_CNTL		(TIPB_PRIVATE_CNTL_BASE + 0x8)

/*
 * ----------------------------------------------------------------------------
 * MPUI interface
 * ----------------------------------------------------------------------------
 */
#define MPUI_BASE			(0xfffec900)
#define MPUI_CTRL			(MPUI_BASE + 0x0)
#define MPUI_DEBUG_ADDR			(MPUI_BASE + 0x4)
#define MPUI_DEBUG_DATA			(MPUI_BASE + 0x8)
#define MPUI_DEBUG_FLAG			(MPUI_BASE + 0xc)
#define MPUI_STATUS_REG			(MPUI_BASE + 0x10)
#define MPUI_DSP_STATUS			(MPUI_BASE + 0x14)
#define MPUI_DSP_BOOT_CONFIG		(MPUI_BASE + 0x18)
#define MPUI_DSP_API_CONFIG		(MPUI_BASE + 0x1c)

/*
 * ----------------------------------------------------------------------------
 * LED Pulse Generator
 * ----------------------------------------------------------------------------
 */
#define OMAP_LPG1_BASE			0xfffbd000
#define OMAP_LPG2_BASE			0xfffbd800
#define OMAP_LPG1_LCR			(OMAP_LPG1_BASE + 0x00)
#define OMAP_LPG1_PMR			(OMAP_LPG1_BASE + 0x04)
#define OMAP_LPG2_LCR			(OMAP_LPG2_BASE + 0x00)
#define OMAP_LPG2_PMR			(OMAP_LPG2_BASE + 0x04)

/*
 * ---------------------------------------------------------------------------
 * DSP
 * ---------------------------------------------------------------------------
 */

#define OMAP1_DSP_BASE		0xE0000000
#define OMAP1_DSP_SIZE		0x28000
#define OMAP1_DSP_START		0xE0000000

#define OMAP1_DSPREG_BASE	0xE1000000
#define OMAP1_DSPREG_SIZE	SZ_128K
#define OMAP1_DSPREG_START	0xE1000000

#endif	/* __ASM_ARCH_OMAP_HARDWARE_H */
