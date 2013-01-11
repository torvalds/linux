/*
 * Copyright (c) 2010-2012, NVIDIA Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MACH_TEGRA_SLEEP_H
#define __MACH_TEGRA_SLEEP_H

#include "iomap.h"

#define TEGRA_ARM_PERIF_VIRT (TEGRA_ARM_PERIF_BASE - IO_CPU_PHYS \
					+ IO_CPU_VIRT)
#define TEGRA_FLOW_CTRL_VIRT (TEGRA_FLOW_CTRL_BASE - IO_PPSB_PHYS \
					+ IO_PPSB_VIRT)
#define TEGRA_CLK_RESET_VIRT (TEGRA_CLK_RESET_BASE - IO_PPSB_PHYS \
					+ IO_PPSB_VIRT)

#ifdef __ASSEMBLY__
/* returns the offset of the flow controller halt register for a cpu */
.macro cpu_to_halt_reg rd, rcpu
	cmp	\rcpu, #0
	subne	\rd, \rcpu, #1
	movne	\rd, \rd, lsl #3
	addne	\rd, \rd, #0x14
	moveq	\rd, #0
.endm

/* returns the offset of the flow controller csr register for a cpu */
.macro cpu_to_csr_reg rd, rcpu
	cmp	\rcpu, #0
	subne	\rd, \rcpu, #1
	movne	\rd, \rd, lsl #3
	addne	\rd, \rd, #0x18
	moveq	\rd, #8
.endm

/* returns the ID of the current processor */
.macro cpu_id, rd
	mrc	p15, 0, \rd, c0, c0, 5
	and	\rd, \rd, #0xF
.endm

/* loads a 32-bit value into a register without a data access */
.macro mov32, reg, val
	movw	\reg, #:lower16:\val
	movt	\reg, #:upper16:\val
.endm

/* Macro to exit SMP coherency. */
.macro exit_smp, tmp1, tmp2
	mrc	p15, 0, \tmp1, c1, c0, 1	@ ACTLR
	bic	\tmp1, \tmp1, #(1<<6) | (1<<0)	@ clear ACTLR.SMP | ACTLR.FW
	mcr	p15, 0, \tmp1, c1, c0, 1	@ ACTLR
	isb
	cpu_id	\tmp1
	mov	\tmp1, \tmp1, lsl #2
	mov	\tmp2, #0xf
	mov	\tmp2, \tmp2, lsl \tmp1
	mov32	\tmp1, TEGRA_ARM_PERIF_VIRT + 0xC
	str	\tmp2, [\tmp1]			@ invalidate SCU tags for CPU
	dsb
.endm

/* Macro to resume & re-enable L2 cache */
#ifndef L2X0_CTRL_EN
#define L2X0_CTRL_EN	1
#endif

#ifdef CONFIG_CACHE_L2X0
.macro l2_cache_resume, tmp1, tmp2, tmp3, phys_l2x0_saved_regs
	adr	\tmp1, \phys_l2x0_saved_regs
	ldr	\tmp1, [\tmp1]
	ldr	\tmp2, [\tmp1, #L2X0_R_PHY_BASE]
	ldr	\tmp3, [\tmp2, #L2X0_CTRL]
	tst	\tmp3, #L2X0_CTRL_EN
	bne	exit_l2_resume
	ldr	\tmp3, [\tmp1, #L2X0_R_TAG_LATENCY]
	str	\tmp3, [\tmp2, #L2X0_TAG_LATENCY_CTRL]
	ldr	\tmp3, [\tmp1, #L2X0_R_DATA_LATENCY]
	str	\tmp3, [\tmp2, #L2X0_DATA_LATENCY_CTRL]
	ldr	\tmp3, [\tmp1, #L2X0_R_PREFETCH_CTRL]
	str	\tmp3, [\tmp2, #L2X0_PREFETCH_CTRL]
	ldr	\tmp3, [\tmp1, #L2X0_R_PWR_CTRL]
	str	\tmp3, [\tmp2, #L2X0_POWER_CTRL]
	ldr	\tmp3, [\tmp1, #L2X0_R_AUX_CTRL]
	str	\tmp3, [\tmp2, #L2X0_AUX_CTRL]
	mov	\tmp3, #L2X0_CTRL_EN
	str	\tmp3, [\tmp2, #L2X0_CTRL]
exit_l2_resume:
.endm
#else /* CONFIG_CACHE_L2X0 */
.macro l2_cache_resume, tmp1, tmp2, tmp3, phys_l2x0_saved_regs
.endm
#endif /* CONFIG_CACHE_L2X0 */
#else
void tegra_resume(void);
int tegra_sleep_cpu_finish(unsigned long);

#ifdef CONFIG_HOTPLUG_CPU
void tegra20_hotplug_init(void);
void tegra30_hotplug_init(void);
#else
static inline void tegra20_hotplug_init(void) {}
static inline void tegra30_hotplug_init(void) {}
#endif

int tegra30_sleep_cpu_secondary_finish(unsigned long);
void tegra30_tear_down_cpu(void);

#endif
#endif
