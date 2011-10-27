/*
 *  Copyright 2006 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>

#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/firmware.h>
#include <asm/kexec.h>
#include <asm/mpic.h>
#include <asm/xics.h>
#include <asm/smp.h>

#include "pseries.h"
#include "plpar_wrappers.h"

static void pseries_kexec_cpu_down(int crash_shutdown, int secondary)
{
	/* Don't risk a hypervisor call if we're crashing */
	if (firmware_has_feature(FW_FEATURE_SPLPAR) && !crash_shutdown) {
		unsigned long addr;
		int ret;

		if (get_lppaca()->dtl_enable_mask) {
			ret = unregister_dtl(hard_smp_processor_id());
			if (ret) {
				pr_err("WARNING: DTL deregistration for cpu "
				       "%d (hw %d) failed with %d\n",
				       smp_processor_id(),
				       hard_smp_processor_id(), ret);
			}
		}

		addr = __pa(get_slb_shadow());
		if (unregister_slb_shadow(hard_smp_processor_id(), addr))
			printk("SLB shadow buffer deregistration of "
			       "cpu %u (hw_cpu_id %d) failed\n",
			       smp_processor_id(),
			       hard_smp_processor_id());

		addr = __pa(get_lppaca());
		if (unregister_vpa(hard_smp_processor_id(), addr)) {
			printk("VPA deregistration of cpu %u (hw_cpu_id %d) "
					"failed\n", smp_processor_id(),
					hard_smp_processor_id());
		}
	}
}

static void pseries_kexec_cpu_down_mpic(int crash_shutdown, int secondary)
{
	pseries_kexec_cpu_down(crash_shutdown, secondary);
	mpic_teardown_this_cpu(secondary);
}

void __init setup_kexec_cpu_down_mpic(void)
{
	ppc_md.kexec_cpu_down = pseries_kexec_cpu_down_mpic;
}

static void pseries_kexec_cpu_down_xics(int crash_shutdown, int secondary)
{
	pseries_kexec_cpu_down(crash_shutdown, secondary);
	xics_kexec_teardown_cpu(secondary);
}

void __init setup_kexec_cpu_down_xics(void)
{
	ppc_md.kexec_cpu_down = pseries_kexec_cpu_down_xics;
}
