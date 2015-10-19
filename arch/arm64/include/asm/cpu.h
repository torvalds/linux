/*
  * Copyright (C) 2014 ARM Ltd.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_CPU_H
#define __ASM_CPU_H

#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/percpu.h>

/*
 * Records attributes of an individual CPU.
 */
struct cpuinfo_arm64 {
	struct cpu	cpu;
	u32		reg_ctr;
	u32		reg_cntfrq;
	u32		reg_dczid;
	u32		reg_midr;

	u64		reg_id_aa64dfr0;
	u64		reg_id_aa64dfr1;
	u64		reg_id_aa64isar0;
	u64		reg_id_aa64isar1;
	u64		reg_id_aa64mmfr0;
	u64		reg_id_aa64mmfr1;
	u64		reg_id_aa64pfr0;
	u64		reg_id_aa64pfr1;

	u32		reg_id_dfr0;
	u32		reg_id_isar0;
	u32		reg_id_isar1;
	u32		reg_id_isar2;
	u32		reg_id_isar3;
	u32		reg_id_isar4;
	u32		reg_id_isar5;
	u32		reg_id_mmfr0;
	u32		reg_id_mmfr1;
	u32		reg_id_mmfr2;
	u32		reg_id_mmfr3;
	u32		reg_id_pfr0;
	u32		reg_id_pfr1;

	u32		reg_mvfr0;
	u32		reg_mvfr1;
	u32		reg_mvfr2;
};

DECLARE_PER_CPU(struct cpuinfo_arm64, cpu_data);

void cpuinfo_store_cpu(void);
void __init cpuinfo_store_boot_cpu(void);

void update_cpu_features(struct cpuinfo_arm64 *info);

#endif /* __ASM_CPU_H */
