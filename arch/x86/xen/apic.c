// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/thread_info.h>

#include <asm/x86_init.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/xen/hypercall.h>

#include <xen/xen.h>
#include <xen/interface/physdev.h>
#include "xen-ops.h"
#include "pmu.h"
#include "smp.h"

static unsigned int xen_io_apic_read(unsigned apic, unsigned reg)
{
	struct physdev_apic apic_op;
	int ret;

	apic_op.apic_physbase = mpc_ioapic_addr(apic);
	apic_op.reg = reg;
	ret = HYPERVISOR_physdev_op(PHYSDEVOP_apic_read, &apic_op);
	if (!ret)
		return apic_op.value;

	/* fallback to return an emulated IO_APIC values */
	if (reg == 0x1)
		return 0x00170020;
	else if (reg == 0x0)
		return apic << 24;

	return 0xfd;
}

static u32 xen_set_apic_id(u32 x)
{
	WARN_ON(1);
	return x;
}

static u32 xen_get_apic_id(u32 x)
{
	return ((x)>>24) & 0xFFu;
}

static u32 xen_apic_read(u32 reg)
{
	struct xen_platform_op op = {
		.cmd = XENPF_get_cpuinfo,
		.interface_version = XENPF_INTERFACE_VERSION,
		.u.pcpu_info.xen_cpuid = 0,
	};
	int ret;

	/* Shouldn't need this as APIC is turned off for PV, and we only
	 * get called on the bootup processor. But just in case. */
	if (!xen_initial_domain() || smp_processor_id())
		return 0;

	if (reg == APIC_LVR)
		return 0x14;
	if (reg != APIC_ID)
		return 0;

	ret = HYPERVISOR_platform_op(&op);
	if (ret)
		op.u.pcpu_info.apic_id = BAD_APICID;

	return op.u.pcpu_info.apic_id << 24;
}

static void xen_apic_write(u32 reg, u32 val)
{
	if (reg == APIC_LVTPC) {
		(void)pmu_apic_update(reg);
		return;
	}

	/* Warn to see if there's any stray references */
	WARN(1,"register: %x, value: %x\n", reg, val);
}

static void xen_apic_eoi(void)
{
	WARN_ON_ONCE(1);
}

static u64 xen_apic_icr_read(void)
{
	return 0;
}

static void xen_apic_icr_write(u32 low, u32 id)
{
	/* Warn to see if there's any stray references */
	WARN_ON(1);
}

static int xen_apic_probe_pv(void)
{
	if (xen_pv_domain())
		return 1;

	return 0;
}

static int xen_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return xen_pv_domain();
}

static u32 xen_phys_pkg_id(u32 initial_apic_id, int index_msb)
{
	return initial_apic_id >> index_msb;
}

static u32 xen_cpu_present_to_apicid(int cpu)
{
	if (cpu_present(cpu))
		return cpu_data(cpu).topo.apicid;
	else
		return BAD_APICID;
}

static struct apic xen_pv_apic __ro_after_init = {
	.name				= "Xen PV",
	.probe				= xen_apic_probe_pv,
	.acpi_madt_oem_check		= xen_madt_oem_check,

	/* .delivery_mode and .dest_mode_logical not used by XENPV */

	.disable_esr			= 0,

	.cpu_present_to_apicid		= xen_cpu_present_to_apicid,
	.phys_pkg_id			= xen_phys_pkg_id, /* detect_ht */

	.max_apic_id			= UINT_MAX,
	.get_apic_id			= xen_get_apic_id,
	.set_apic_id			= xen_set_apic_id,

	.calc_dest_apicid		= apic_flat_calc_apicid,

#ifdef CONFIG_SMP
	.send_IPI_mask			= xen_send_IPI_mask,
	.send_IPI_mask_allbutself	= xen_send_IPI_mask_allbutself,
	.send_IPI_allbutself		= xen_send_IPI_allbutself,
	.send_IPI_all			= xen_send_IPI_all,
	.send_IPI_self			= xen_send_IPI_self,
#endif
	.read				= xen_apic_read,
	.write				= xen_apic_write,
	.eoi				= xen_apic_eoi,

	.icr_read			= xen_apic_icr_read,
	.icr_write			= xen_apic_icr_write,
};
apic_driver(xen_pv_apic);

void __init xen_init_apic(void)
{
	x86_apic_ops.io_apic_read = xen_io_apic_read;
}
