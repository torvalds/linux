/*
 * AMLOGIC Mali fix driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <asm/io.h>
#include <linux/ioport.h>
#include <linux/dma-mapping.h>

#include <linux/module.h>

#include <mach/am_regs.h>
#include <mach/clock.h>
#include <linux/io.h>
#include <mach/io.h>
#include <plat/io.h>
#include <asm/io.h>

#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"
#include "mali_fix.h"

#define MALI_MM1_REG_ADDR 0xd0064000
#define MALI_MMU_REGISTER_INT_STATUS     0x0008
#define MALI_MM2_REG_ADDR 0xd0065000
#define MALI_MMU_REGISTER_INT_STATUS     0x0008
#define MALI_MM_REG_SIZE 0x1000

#define READ_MALI_MMU1_REG(r) (ioread32(((u8*)mali_mm1_regs) + r))
#define READ_MALI_MMU2_REG(r) (ioread32(((u8*)mali_mm2_regs) + r))

extern int mali_PP0_int_cnt(void);
extern int mali_PP1_int_cnt(void);
 
static ulong * mali_mm1_regs = NULL;
static ulong * mali_mm2_regs = NULL;
static struct timer_list timer;

static u32 mali_pp1_int_count = 0;
static u32 mali_pp2_int_count = 0;
static u32 mali_pp1_mmu_int_count = 0;
static u32 mali_pp2_mmu_int_count = 0;
static u32 mali_mmu_int_process_state[2];

static void timer_callback(ulong data)
{
	unsigned long mali_flags;

	mali_pp1_int_count = mali_PP0_int_cnt();
	mali_pp2_int_count = mali_PP1_int_cnt();

	/* lock mali_clock_gating when access Mali registers */
	mali_flags = mali_clock_gating_lock();

	if (readl((u32 *)P_HHI_MALI_CLK_CNTL) & 0x100) {
		/* polling for PP1 MMU interrupt */
		if (mali_mmu_int_process_state[0] == MMU_INT_NONE) {
			if (READ_MALI_MMU1_REG(MALI_MMU_REGISTER_INT_STATUS) != 0) {
				mali_pp1_mmu_int_count++;
				MALI_DEBUG_PRINT(3, ("Mali MMU: core0 page fault emit \n"));
				mali_mmu_int_process_state[0] = MMU_INT_HIT;
				__raw_writel(1, (volatile void *)P_ISA_TIMERC);
			}
		}

		/* polling for PP2 MMU interrupt */
		if (mali_mmu_int_process_state[1] == MMU_INT_NONE) {
			if (READ_MALI_MMU2_REG(MALI_MMU_REGISTER_INT_STATUS) != 0) {
				mali_pp2_mmu_int_count++;
				MALI_DEBUG_PRINT(3, ("Mali MMU: core1 page fault emit \n"));
				mali_mmu_int_process_state[1] = MMU_INT_HIT;
				__raw_writel(1, (volatile void *)P_ISA_TIMERC);
			}
		}
	}

	mali_clock_gating_unlock(mali_flags);

	timer.expires = jiffies + HZ/100;

	add_timer(&timer);
}

void malifix_set_mmu_int_process_state(int index, int state)
{
	if (index < 2)
		mali_mmu_int_process_state[index] = state;
}

int malifix_get_mmu_int_process_state(int index)
{
	if (index < 2)
		return mali_mmu_int_process_state[index];
	return 0;
}

void malifix_init(void)
{
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
	if (!mali_meson_is_revb())
		return;

	if ((mali_mm1_regs) && (mali_mm2_regs)) return;
	mali_mmu_int_process_state[0] = 0;
	mali_mmu_int_process_state[1] = 0;

	/* set up Timer C as a 1uS one-shot timer */
	aml_clr_reg32_mask(P_ISA_TIMER_MUX, (1<<18)|(1<<14)|(3<<4));
	aml_set_reg32_mask(P_ISA_TIMER_MUX,	(1<<18)|(0<<14)|(0<<4));

	setup_timer(&timer, timer_callback, 0);

	mali_mm1_regs = (ulong *)ioremap_nocache(MALI_MM1_REG_ADDR, MALI_MM_REG_SIZE);
	if (mali_mm1_regs)
		printk("Mali pp1 MMU register mapped at %p...\n", mali_mm1_regs);

	mali_mm2_regs = (ulong *)ioremap_nocache(MALI_MM2_REG_ADDR, MALI_MM_REG_SIZE);
	if (mali_mm2_regs)
		printk("Mali pp2 MMU register mapped at %p...\n", mali_mm2_regs);

	if ((mali_mm1_regs != NULL) && (mali_mm2_regs != NULL))
		mod_timer(&timer, jiffies + HZ/100);
#endif
}

void malifix_exit(void)
{
#if MESON_CPU_TYPE == MESON_CPU_TYPE_MESON6
	if (!mali_meson_is_revb())
		return;

	del_timer(&timer);

	if (mali_mm1_regs != NULL)
		iounmap(mali_mm1_regs);
	mali_mm1_regs = NULL;

	if (mali_mm2_regs != NULL)
		iounmap(mali_mm2_regs);
	mali_mm2_regs = NULL;

#endif
	return;
}

module_param(mali_pp1_int_count, uint, 0664);
MODULE_PARM_DESC(mali_pp1_int_count, "Mali PP1 interrupt count\n");

module_param(mali_pp2_int_count, uint, 0664);
MODULE_PARM_DESC(mali_pp2_int_count, "Mali PP1 interrupt count\n");

module_param(mali_pp1_mmu_int_count, uint, 0664);
MODULE_PARM_DESC(mali_pp1_mmu_int_count, "Mali PP1 mmu interrupt count\n");

module_param(mali_pp2_mmu_int_count, uint, 0664);
MODULE_PARM_DESC(mali_pp2_mmu_int_count, "Mali PP2 mmu interrupt count\n");

MODULE_DESCRIPTION("AMLOGIC mali fix driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Yao <timyao@amlogic.com>");
