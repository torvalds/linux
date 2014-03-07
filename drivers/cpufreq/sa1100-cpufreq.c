/*
 * cpu-sa1100.c: clock scaling for the SA1100
 *
 * Copyright (C) 2000 2001, The Delft University of Technology
 *
 * Authors:
 * - Johan Pouwelse (J.A.Pouwelse@its.tudelft.nl): initial version
 * - Erik Mouw (J.A.K.Mouw@its.tudelft.nl):
 *   - major rewrite for linux-2.3.99
 *   - rewritten for the more generic power management scheme in
 *     linux-2.4.5-rmk1
 *
 * This software has been developed while working on the LART
 * computing board (http://www.lartmaker.nl/), which is
 * sponsored by the Mobile Multi-media Communications
 * (http://www.mobimedia.org/) and Ubiquitous Communications
 * (http://www.ubicom.tudelft.nl/) projects.
 *
 * The authors can be reached at:
 *
 *  Erik Mouw
 *  Information and Communication Theory Group
 *  Faculty of Information Technology and Systems
 *  Delft University of Technology
 *  P.O. Box 5031
 *  2600 GA Delft
 *  The Netherlands
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 * Theory of operations
 * ====================
 *
 * Clock scaling can be used to lower the power consumption of the CPU
 * core. This will give you a somewhat longer running time.
 *
 * The SA-1100 has a single register to change the core clock speed:
 *
 *   PPCR      0x90020014    PLL config
 *
 * However, the DRAM timings are closely related to the core clock
 * speed, so we need to change these, too. The used registers are:
 *
 *   MDCNFG    0xA0000000    DRAM config
 *   MDCAS0    0xA0000004    Access waveform
 *   MDCAS1    0xA0000008    Access waveform
 *   MDCAS2    0xA000000C    Access waveform
 *
 * Care must be taken to change the DRAM parameters the correct way,
 * because otherwise the DRAM becomes unusable and the kernel will
 * crash.
 *
 * The simple solution to avoid a kernel crash is to put the actual
 * clock change in ROM and jump to that code from the kernel. The main
 * disadvantage is that the ROM has to be modified, which is not
 * possible on all SA-1100 platforms. Another disadvantage is that
 * jumping to ROM makes clock switching unnecessary complicated.
 *
 * The idea behind this driver is that the memory configuration can be
 * changed while running from DRAM (even with interrupts turned on!)
 * as long as all re-configuration steps yield a valid DRAM
 * configuration. The advantages are clear: it will run on all SA-1100
 * platforms, and the code is very simple.
 *
 * If you really want to understand what is going on in
 * sa1100_update_dram_timings(), you'll have to read sections 8.2,
 * 9.5.7.3, and 10.2 from the "Intel StrongARM SA-1100 Microprocessor
 * Developers Manual" (available for free from Intel).
 *
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/io.h>

#include <asm/cputype.h>

#include <mach/generic.h>
#include <mach/hardware.h>

struct sa1100_dram_regs {
	int speed;
	u32 mdcnfg;
	u32 mdcas0;
	u32 mdcas1;
	u32 mdcas2;
};


static struct cpufreq_driver sa1100_driver;

static struct sa1100_dram_regs sa1100_dram_settings[] = {
	/*speed,     mdcnfg,     mdcas0,     mdcas1,     mdcas2,   clock freq */
	{ 59000, 0x00dc88a3, 0xcccccccf, 0xfffffffc, 0xffffffff},/*  59.0 MHz */
	{ 73700, 0x011490a3, 0xcccccccf, 0xfffffffc, 0xffffffff},/*  73.7 MHz */
	{ 88500, 0x014e90a3, 0xcccccccf, 0xfffffffc, 0xffffffff},/*  88.5 MHz */
	{103200, 0x01889923, 0xcccccccf, 0xfffffffc, 0xffffffff},/* 103.2 MHz */
	{118000, 0x01c29923, 0x9999998f, 0xfffffff9, 0xffffffff},/* 118.0 MHz */
	{132700, 0x01fb2123, 0x9999998f, 0xfffffff9, 0xffffffff},/* 132.7 MHz */
	{147500, 0x02352123, 0x3333330f, 0xfffffff3, 0xffffffff},/* 147.5 MHz */
	{162200, 0x026b29a3, 0x38e38e1f, 0xfff8e38e, 0xffffffff},/* 162.2 MHz */
	{176900, 0x02a329a3, 0x71c71c1f, 0xfff1c71c, 0xffffffff},/* 176.9 MHz */
	{191700, 0x02dd31a3, 0xe38e383f, 0xffe38e38, 0xffffffff},/* 191.7 MHz */
	{206400, 0x03153223, 0xc71c703f, 0xffc71c71, 0xffffffff},/* 206.4 MHz */
	{221200, 0x034fba23, 0xc71c703f, 0xffc71c71, 0xffffffff},/* 221.2 MHz */
	{235900, 0x03853a23, 0xe1e1e07f, 0xe1e1e1e1, 0xffffffe1},/* 235.9 MHz */
	{250700, 0x03bf3aa3, 0xc3c3c07f, 0xc3c3c3c3, 0xffffffc3},/* 250.7 MHz */
	{265400, 0x03f7c2a3, 0xc3c3c07f, 0xc3c3c3c3, 0xffffffc3},/* 265.4 MHz */
	{280200, 0x0431c2a3, 0x878780ff, 0x87878787, 0xffffff87},/* 280.2 MHz */
	{ 0, 0, 0, 0, 0 } /* last entry */
};

static void sa1100_update_dram_timings(int current_speed, int new_speed)
{
	struct sa1100_dram_regs *settings = sa1100_dram_settings;

	/* find speed */
	while (settings->speed != 0) {
		if (new_speed == settings->speed)
			break;

		settings++;
	}

	if (settings->speed == 0) {
		panic("%s: couldn't find dram setting for speed %d\n",
		      __func__, new_speed);
	}

	/* No risk, no fun: run with interrupts on! */
	if (new_speed > current_speed) {
		/* We're going FASTER, so first relax the memory
		 * timings before changing the core frequency
		 */

		/* Half the memory access clock */
		MDCNFG |= MDCNFG_CDB2;

		/* The order of these statements IS important, keep 8
		 * pulses!!
		 */
		MDCAS2 = settings->mdcas2;
		MDCAS1 = settings->mdcas1;
		MDCAS0 = settings->mdcas0;
		MDCNFG = settings->mdcnfg;
	} else {
		/* We're going SLOWER: first decrease the core
		 * frequency and then tighten the memory settings.
		 */

		/* Half the memory access clock */
		MDCNFG |= MDCNFG_CDB2;

		/* The order of these statements IS important, keep 8
		 * pulses!!
		 */
		MDCAS0 = settings->mdcas0;
		MDCAS1 = settings->mdcas1;
		MDCAS2 = settings->mdcas2;
		MDCNFG = settings->mdcnfg;
	}
}

static int sa1100_target(struct cpufreq_policy *policy, unsigned int ppcr)
{
	unsigned int cur = sa11x0_getspeed(0);
	unsigned int new_freq;

	new_freq = sa11x0_freq_table[ppcr].frequency;

	if (new_freq > cur)
		sa1100_update_dram_timings(cur, new_freq);

	PPCR = ppcr;

	if (new_freq < cur)
		sa1100_update_dram_timings(cur, new_freq);

	return 0;
}

static int __init sa1100_cpu_init(struct cpufreq_policy *policy)
{
	return cpufreq_generic_init(policy, sa11x0_freq_table, CPUFREQ_ETERNAL);
}

static struct cpufreq_driver sa1100_driver __refdata = {
	.flags		= CPUFREQ_STICKY,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= sa1100_target,
	.get		= sa11x0_getspeed,
	.init		= sa1100_cpu_init,
	.name		= "sa1100",
};

static int __init sa1100_dram_init(void)
{
	if (cpu_is_sa1100())
		return cpufreq_register_driver(&sa1100_driver);
	else
		return -ENODEV;
}

arch_initcall(sa1100_dram_init);
