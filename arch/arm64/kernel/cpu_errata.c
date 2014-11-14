/*
 * Contains CPU specific errata definitions
 *
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

#define pr_fmt(fmt) "alternative: " fmt

#include <linux/types.h>
#include <asm/cpu.h>
#include <asm/cputype.h>
#include <asm/cpufeature.h>

/*
 * Add a struct or another datatype to the union below if you need
 * different means to detect an affected CPU.
 */
struct arm64_cpu_capabilities {
	const char *desc;
	u16 capability;
	bool (*is_affected)(struct arm64_cpu_capabilities *);
	union {
		struct {
			u32 midr_model;
			u32 midr_range_min, midr_range_max;
		};
	};
};

struct arm64_cpu_capabilities arm64_errata[] = {
	{}
};

void check_local_cpu_errata(void)
{
	struct arm64_cpu_capabilities *cpus = arm64_errata;
	int i;

	for (i = 0; cpus[i].desc; i++) {
		if (!cpus[i].is_affected(&cpus[i]))
			continue;

		if (!cpus_have_cap(cpus[i].capability))
			pr_info("enabling workaround for %s\n", cpus[i].desc);
		cpus_set_cap(cpus[i].capability);
	}
}
