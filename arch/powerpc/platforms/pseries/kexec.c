// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright 2006 Michael Ellerman, IBM Corporation
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/firmware.h>
#include <asm/kexec.h>
#include <asm/xics.h>
#include <asm/xive.h>
#include <asm/smp.h>
#include <asm/plpar_wrappers.h>

#include "pseries.h"

void pseries_kexec_cpu_down(int crash_shutdown, int secondary)
{
	/*
	 * Ensure vpa/slb_shadow/dtl cleanup even while we are crashing.
	 * Why? The hypervisor is not crashing so at least attempt unregister to
	 * avoid the hypervisor stepping on our memory. If hypervisor or kexec
	 * kernel steps on the old memory allocated to these areas before the
	 * new kexec-kernel happens to allocate and register new areas,
	 * the hypervisor will see invalid content which may cause
	 * unexpected behavior.
	 */
	if (firmware_has_feature(FW_FEATURE_SPLPAR)) {
		int ret;
		int cpu = smp_processor_id();
		int hwcpu = hard_smp_processor_id();

		if (get_lppaca()->dtl_enable_mask) {
			ret = unregister_dtl(hwcpu);
			if (ret) {
				pr_err("WARNING: DTL deregistration for cpu "
				       "%d (hw %d) failed with %d\n",
				       cpu, hwcpu, ret);
			}
		}

		ret = unregister_slb_shadow(hwcpu);
		if (ret) {
			pr_err("WARNING: SLB shadow buffer deregistration "
			       "for cpu %d (hw %d) failed with %d\n",
			       cpu, hwcpu, ret);
		}

		ret = unregister_vpa(hwcpu);
		if (ret) {
			pr_err("WARNING: VPA deregistration for cpu %d "
			       "(hw %d) failed with %d\n", cpu, hwcpu, ret);
		}
	}

	if (xive_enabled()) {
		xive_teardown_cpu();

		if (!secondary)
			xive_shutdown();
	} else
		xics_kexec_teardown_cpu(secondary);
}
