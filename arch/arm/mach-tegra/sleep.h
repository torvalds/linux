/*
 * Copyright (c) 2010-2013, NVIDIA Corporation. All rights reserved.
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
#define TEGRA_PMC_VIRT	(TEGRA_PMC_BASE - IO_APB_PHYS + IO_APB_VIRT)

/* PMC_SCRATCH37-39 and 41 are used for tegra_pen_lock and idle */
#define PMC_SCRATCH37	0x130
#define PMC_SCRATCH38	0x134
#define PMC_SCRATCH39	0x138
#define PMC_SCRATCH41	0x140

#ifdef CONFIG_ARCH_TEGRA_2x_SOC
#define CPU_RESETTABLE		2
#define CPU_RESETTABLE_SOON	1
#define CPU_NOT_RESETTABLE	0
#endif

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

/* Marco to check CPU part num */
.macro check_cpu_part_num part_num, tmp1, tmp2
	mrc	p15, 0, \tmp1, c0, c0, 0
	ubfx	\tmp1, \tmp1, #4, #12
	mov32	\tmp2, \part_num
	cmp	\tmp1, \tmp2
.endm

/* Macro to exit SMP coherency. */
.macro exit_smp, tmp1, tmp2
	mrc	p15, 0, \tmp1, c1, c0, 1	@ ACTLR
	bic	\tmp1, \tmp1, #(1<<6) | (1<<0)	@ clear ACTLR.SMP | ACTLR.FW
	mcr	p15, 0, \tmp1, c1, c0, 1	@ ACTLR
	isb
#ifdef CONFIG_HAVE_ARM_SCU
	check_cpu_part_num 0xc09, \tmp1, \tmp2
	mrceq	p15, 0, \tmp1, c0, c0, 5
	andeq	\tmp1, \tmp1, #0xF
	moveq	\tmp1, \tmp1, lsl #2
	moveq	\tmp2, #0xf
	moveq	\tmp2, \tmp2, lsl \tmp1
	ldreq	\tmp1, =(TEGRA_ARM_PERIF_VIRT + 0xC)
	streq	\tmp2, [\tmp1]			@ invalidate SCU tags for CPU
	dsb
#endif
.endm

/* Macro to check Tegra revision */
#define APB_MISC_GP_HIDREV	0x804
.macro tegra_get_soc_id base, tmp1
	mov32	\tmp1, \base
	ldr	\tmp1, [\tmp1, #APB_MISC_GP_HIDREV]
	and	\tmp1, \tmp1, #0xff00
	mov	\tmp1, \tmp1, lsr #8
.endm

/* Macro to resume & re-enable L2 cache */
#ifndef L2X0_CTRL_EN
#define L2X0_CTRL_EN	1
#endif

#ifdef CONFIG_CACHE_L2X0
.macro l2_cache_resume, tmp1, tmp2, tmp3, phys_l2x0_saved_regs
	W(adr)	\tmp1, \phys_l2x0_saved_regs
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
void tegra_pen_lock(void);
void tegra_pen_unlock(void);
void tegra_resume(void);
int tegra_sleep_cpu_finish(unsigned long);
void tegra_disable_clean_inv_dcache(void);

#ifdef CONFIG_HOTPLUG_CPU
void tegra20_hotplug_shutdown(void);
void tegra30_hotplug_shutdown(void);
void tegra_hotplug_init(void);
#else
static inline void tegra_hotplug_init(void) {}
#endif

void tegra20_cpu_shutdown(int cpu);
int tegra20_cpu_is_resettable_soon(void);
void tegra20_cpu_clear_resettable(void);
#ifdef CONFIG_ARCH_TEGRA_2x_SOC
void tegra20_cpu_set_resettable_soon(void);
#else
static inline void tegra20_cpu_set_resettable_soon(void) {}
#endif

int tegra20_sleep_cpu_secondary_finish(unsigned long);
void tegra20_tear_down_cpu(void);
int tegra30_sleep_cpu_secondary_finish(unsigned long);
void tegra30_tear_down_cpu(void);

#endif
#endif
