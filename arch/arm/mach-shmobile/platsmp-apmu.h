/*
 * rmobile apmu definition
 *
 * Copyright (C) 2014  Renesas Electronics Corporation
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

#ifndef PLATSMP_APMU_H
#define PLATSMP_APMU_H

struct rcar_apmu_config {
	struct resource iomem;
	int cpus[4];
};

extern void shmobile_smp_apmu_prepare_cpus(unsigned int max_cpus,
					   struct rcar_apmu_config *apmu_config,
					   int num);
extern int shmobile_smp_apmu_boot_secondary(unsigned int cpu,
					    struct task_struct *idle);
extern void shmobile_smp_apmu_cpu_die(unsigned int cpu);
extern int shmobile_smp_apmu_cpu_kill(unsigned int cpu);

#endif /* PLATSMP_APMU_H */
