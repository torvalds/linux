/*
 * SGI NMI support routines
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 *  Copyright (c) 2009-2013 Silicon Graphics, Inc.  All Rights Reserved.
 *  Copyright (c) Mike Travis
 */

#include <linux/cpu.h>
#include <linux/nmi.h>

#include <asm/apic.h>
#include <asm/nmi.h>
#include <asm/uv/uv.h>
#include <asm/uv/uv_hub.h>
#include <asm/uv/uv_mmrs.h>

/* BMC sets a bit this MMR non-zero before sending an NMI */
#define UVH_NMI_MMR				UVH_SCRATCH5
#define UVH_NMI_MMR_CLEAR			(UVH_NMI_MMR + 8)
#define UV_NMI_PENDING_MASK			(1UL << 63)
DEFINE_PER_CPU(unsigned long, cpu_last_nmi_count);
static DEFINE_SPINLOCK(uv_nmi_lock);

/*
 * When NMI is received, print a stack trace.
 */
int uv_handle_nmi(unsigned int reason, struct pt_regs *regs)
{
	unsigned long real_uv_nmi;
	int bid;

	/*
	 * Each blade has an MMR that indicates when an NMI has been sent
	 * to cpus on the blade. If an NMI is detected, atomically
	 * clear the MMR and update a per-blade NMI count used to
	 * cause each cpu on the blade to notice a new NMI.
	 */
	bid = uv_numa_blade_id();
	real_uv_nmi = (uv_read_local_mmr(UVH_NMI_MMR) & UV_NMI_PENDING_MASK);

	if (unlikely(real_uv_nmi)) {
		spin_lock(&uv_blade_info[bid].nmi_lock);
		real_uv_nmi = (uv_read_local_mmr(UVH_NMI_MMR) &
				UV_NMI_PENDING_MASK);
		if (real_uv_nmi) {
			uv_blade_info[bid].nmi_count++;
			uv_write_local_mmr(UVH_NMI_MMR_CLEAR,
						UV_NMI_PENDING_MASK);
		}
		spin_unlock(&uv_blade_info[bid].nmi_lock);
	}

	if (likely(__get_cpu_var(cpu_last_nmi_count) ==
			uv_blade_info[bid].nmi_count))
		return NMI_DONE;

	__get_cpu_var(cpu_last_nmi_count) = uv_blade_info[bid].nmi_count;

	/*
	 * Use a lock so only one cpu prints at a time.
	 * This prevents intermixed output.
	 */
	spin_lock(&uv_nmi_lock);
	pr_info("UV NMI stack dump cpu %u:\n", smp_processor_id());
	dump_stack();
	spin_unlock(&uv_nmi_lock);

	return NMI_HANDLED;
}

void uv_register_nmi_notifier(void)
{
	if (register_nmi_handler(NMI_UNKNOWN, uv_handle_nmi, 0, "uv"))
		pr_warn("UV NMI handler failed to register\n");
}

void uv_nmi_init(void)
{
	unsigned int value;

	/*
	 * Unmask NMI on all cpus
	 */
	value = apic_read(APIC_LVT1) | APIC_DM_NMI;
	value &= ~APIC_LVT_MASKED;
	apic_write(APIC_LVT1, value);
}

