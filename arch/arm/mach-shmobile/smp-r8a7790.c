/*
 * SMP support for r8a7790
 *
 * Copyright (C) 2012-2013 Renesas Solutions Corp.
 * Copyright (C) 2012 Takashi Yoshii <takashi.yoshii.ze@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/io.h>

#include <asm/smp_plat.h>

#include "common.h"
#include "pm-rcar.h"
#include "r8a7790.h"

static struct rcar_sysc_ch r8a7790_ca15_scu = {
	.chan_offs = 0x180, /* PWRSR5 .. PWRER5 */
	.isr_bit = 12, /* CA15-SCU */
};

static struct rcar_sysc_ch r8a7790_ca7_scu = {
	.chan_offs = 0x100, /* PWRSR3 .. PWRER3 */
	.isr_bit = 21, /* CA7-SCU */
};

static void __init r8a7790_smp_prepare_cpus(unsigned int max_cpus)
{
	/* let APMU code install data related to shmobile_boot_vector */
	shmobile_smp_apmu_prepare_cpus(max_cpus);

	/* turn on power to SCU */
	r8a7790_pm_init();
	rcar_sysc_power_up(&r8a7790_ca15_scu);
	rcar_sysc_power_up(&r8a7790_ca7_scu);
}

struct smp_operations r8a7790_smp_ops __initdata = {
	.smp_prepare_cpus	= r8a7790_smp_prepare_cpus,
	.smp_boot_secondary	= shmobile_smp_apmu_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable		= shmobile_smp_cpu_disable,
	.cpu_die		= shmobile_smp_apmu_cpu_die,
	.cpu_kill		= shmobile_smp_apmu_cpu_kill,
#endif
};
