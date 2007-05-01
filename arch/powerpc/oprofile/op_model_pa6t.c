/*
 * Copyright (C) 2006-2007 PA Semi, Inc
 *
 * Author: Shashi Rao, PA Semi
 *
 * Maintained by: Olof Johansson <olof@lixom.net>
 *
 * Based on arch/powerpc/oprofile/op_model_power4.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/oprofile.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/oprofile_impl.h>
#include <asm/reg.h>

static unsigned char oprofile_running;

/* mmcr values are set in pa6t_reg_setup, used in pa6t_cpu_setup */
static u64 mmcr0_val;
static u64 mmcr1_val;

/* inited in pa6t_reg_setup */
static u64 reset_value[OP_MAX_COUNTER];

static inline u64 ctr_read(unsigned int i)
{
	switch (i) {
	case 0:
		return mfspr(SPRN_PA6T_PMC0);
	case 1:
		return mfspr(SPRN_PA6T_PMC1);
	case 2:
		return mfspr(SPRN_PA6T_PMC2);
	case 3:
		return mfspr(SPRN_PA6T_PMC3);
	case 4:
		return mfspr(SPRN_PA6T_PMC4);
	case 5:
		return mfspr(SPRN_PA6T_PMC5);
	default:
		printk(KERN_ERR "ctr_read called with bad arg %u\n", i);
		return 0;
	}
}

static inline void ctr_write(unsigned int i, u64 val)
{
	switch (i) {
	case 0:
		mtspr(SPRN_PA6T_PMC0, val);
		break;
	case 1:
		mtspr(SPRN_PA6T_PMC1, val);
		break;
	case 2:
		mtspr(SPRN_PA6T_PMC2, val);
		break;
	case 3:
		mtspr(SPRN_PA6T_PMC3, val);
		break;
	case 4:
		mtspr(SPRN_PA6T_PMC4, val);
		break;
	case 5:
		mtspr(SPRN_PA6T_PMC5, val);
		break;
	default:
		printk(KERN_ERR "ctr_write called with bad arg %u\n", i);
		break;
	}
}


/* precompute the values to stuff in the hardware registers */
static void pa6t_reg_setup(struct op_counter_config *ctr,
			   struct op_system_config *sys,
			   int num_ctrs)
{
	int pmc;

	/*
	 * adjust the mmcr0.en[0-5] and mmcr0.inten[0-5] values obtained from the
	 * event_mappings file by turning off the counters that the user doesn't
	 * care about
	 *
	 * setup user and kernel profiling
	 */
	for (pmc = 0; pmc < cur_cpu_spec->num_pmcs; pmc++)
		if (!ctr[pmc].enabled) {
			sys->mmcr0 &= ~(0x1UL << pmc);
			sys->mmcr0 &= ~(0x1UL << (pmc+12));
			pr_debug("turned off counter %u\n", pmc);
		}

	if (sys->enable_kernel)
		sys->mmcr0 |= PA6T_MMCR0_SUPEN | PA6T_MMCR0_HYPEN;
	else
		sys->mmcr0 &= ~(PA6T_MMCR0_SUPEN | PA6T_MMCR0_HYPEN);

	if (sys->enable_user)
		sys->mmcr0 |= PA6T_MMCR0_PREN;
	else
		sys->mmcr0 &= ~PA6T_MMCR0_PREN;

	/*
	 * The performance counter event settings are given in the mmcr0 and
	 * mmcr1 values passed from the user in the op_system_config
	 * structure (sys variable).
	 */
	mmcr0_val = sys->mmcr0;
	mmcr1_val = sys->mmcr1;
	pr_debug("mmcr0_val inited to %016lx\n", sys->mmcr0);
	pr_debug("mmcr1_val inited to %016lx\n", sys->mmcr1);

	for (pmc = 0; pmc < cur_cpu_spec->num_pmcs; pmc++) {
		/* counters are 40 bit. Move to cputable at some point? */
		reset_value[pmc] = (0x1UL << 39) - ctr[pmc].count;
		pr_debug("reset_value for pmc%u inited to 0x%lx\n",
				 pmc, reset_value[pmc]);
	}
}

/* configure registers on this cpu */
static void pa6t_cpu_setup(struct op_counter_config *ctr)
{
	u64 mmcr0 = mmcr0_val;
	u64 mmcr1 = mmcr1_val;

	/* Default is all PMCs off */
	mmcr0 &= ~(0x3FUL);
	mtspr(SPRN_PA6T_MMCR0, mmcr0);

	/* program selected programmable events in */
	mtspr(SPRN_PA6T_MMCR1, mmcr1);

	pr_debug("setup on cpu %d, mmcr0 %016lx\n", smp_processor_id(),
		mfspr(SPRN_PA6T_MMCR0));
	pr_debug("setup on cpu %d, mmcr1 %016lx\n", smp_processor_id(),
		mfspr(SPRN_PA6T_MMCR1));
}

static void pa6t_start(struct op_counter_config *ctr)
{
	int i;

	/* Hold off event counting until rfid */
	u64 mmcr0 = mmcr0_val | PA6T_MMCR0_HANDDIS;

	for (i = 0; i < cur_cpu_spec->num_pmcs; i++)
		if (ctr[i].enabled)
			ctr_write(i, reset_value[i]);
		else
			ctr_write(i, 0UL);

	mtspr(SPRN_PA6T_MMCR0, mmcr0);

	oprofile_running = 1;

	pr_debug("start on cpu %d, mmcr0 %lx\n", smp_processor_id(), mmcr0);
}

static void pa6t_stop(void)
{
	u64 mmcr0;

	/* freeze counters */
	mmcr0 = mfspr(SPRN_PA6T_MMCR0);
	mmcr0 |= PA6T_MMCR0_FCM0;
	mtspr(SPRN_PA6T_MMCR0, mmcr0);

	oprofile_running = 0;

	pr_debug("stop on cpu %d, mmcr0 %lx\n", smp_processor_id(), mmcr0);
}

/* handle the perfmon overflow vector */
static void pa6t_handle_interrupt(struct pt_regs *regs,
				  struct op_counter_config *ctr)
{
	unsigned long pc = mfspr(SPRN_PA6T_SIAR);
	int is_kernel = is_kernel_addr(pc);
	u64 val;
	int i;
	u64 mmcr0;

	/* disable perfmon counting until rfid */
	mmcr0 = mfspr(SPRN_PA6T_MMCR0);
	mtspr(SPRN_PA6T_MMCR0, mmcr0 | PA6T_MMCR0_HANDDIS);

	/* Record samples. We've got one global bit for whether a sample
	 * was taken, so add it for any counter that triggered overflow.
	 */
	for (i = 0; i < cur_cpu_spec->num_pmcs; i++) {
		val = ctr_read(i);
		if (val & (0x1UL << 39)) { /* Overflow bit set */
			if (oprofile_running && ctr[i].enabled) {
				if (mmcr0 & PA6T_MMCR0_SIARLOG)
					oprofile_add_ext_sample(pc, regs, i, is_kernel);
				ctr_write(i, reset_value[i]);
			} else {
				ctr_write(i, 0UL);
			}
		}
	}

	/* Restore mmcr0 to a good known value since the PMI changes it */
	mmcr0 = mmcr0_val | PA6T_MMCR0_HANDDIS;
	mtspr(SPRN_PA6T_MMCR0, mmcr0);
}

struct op_powerpc_model op_model_pa6t = {
	.reg_setup		= pa6t_reg_setup,
	.cpu_setup		= pa6t_cpu_setup,
	.start			= pa6t_start,
	.stop			= pa6t_stop,
	.handle_interrupt	= pa6t_handle_interrupt,
};
