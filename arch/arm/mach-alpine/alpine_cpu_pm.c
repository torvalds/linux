// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Low-level power-management support for Alpine platform.
 *
 * Copyright (C) 2015 Annapurna Labs Ltd.
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "alpine_cpu_pm.h"
#include "alpine_cpu_resume.h"

/* NB registers */
#define AL_SYSFAB_POWER_CONTROL(cpu)	(0x2000 + (cpu)*0x100 + 0x20)

static struct regmap *al_sysfabric;
static struct al_cpu_resume_regs __iomem *al_cpu_resume_regs;
static int wakeup_supported;

int alpine_cpu_wakeup(unsigned int phys_cpu, uint32_t phys_resume_addr)
{
	if (!wakeup_supported)
		return -ENOSYS;

	/*
	 * Set CPU resume address -
	 * secure firmware running on boot will jump to this address
	 * after setting proper CPU mode, and initializing e.g. secure
	 * regs (the same mode all CPUs are booted to - usually HYP)
	 */
	writel(phys_resume_addr,
	       &al_cpu_resume_regs->per_cpu[phys_cpu].resume_addr);

	/* Power-up the CPU */
	regmap_write(al_sysfabric, AL_SYSFAB_POWER_CONTROL(phys_cpu), 0);

	return 0;
}

void __init alpine_cpu_pm_init(void)
{
	struct device_node *np;
	uint32_t watermark;

	al_sysfabric = syscon_regmap_lookup_by_compatible("al,alpine-sysfabric-service");

	np = of_find_compatible_node(NULL, NULL, "al,alpine-cpu-resume");
	al_cpu_resume_regs = of_iomap(np, 0);

	wakeup_supported = !IS_ERR(al_sysfabric) && al_cpu_resume_regs;

	if (wakeup_supported) {
		watermark = readl(&al_cpu_resume_regs->watermark);
		wakeup_supported = (watermark & AL_CPU_RESUME_MAGIC_NUM_MASK)
				    == AL_CPU_RESUME_MAGIC_NUM;
	}
}
