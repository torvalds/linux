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

static u32 xen_set_apic_id(unsigned int x)
{
	WARN_ON(1);
	return x;
}

static unsigned int xen_get_apic_id(unsigned long x)
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
	int ret = 0;

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

static u64 xen_apic_icr_read(void)
{
	return 0;
}

static void xen_apic_icr_write(u32 low, u32 id)
{
	/* Warn to see if there's any stray references */
	WARN_ON(1);
}

static u32 xen_safe_apic_wait_icr_idle(void)
{
        return 0;
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

static int xen_id_always_valid(u32 apicid)
{
	return 1;
}

static int xen_id_always_registered(void)
{
	return 1;
}

static int xen_phys_pkg_id(int initial_apic_id, int index_msb)
{
	return initial_apic_id >> index_msb;
}

static void xen_noop(void)
{
}

static void xen_silent_inquire(int apicid)
{
}

static int xen_cpu_present_to_apicid(int cpu)
{
	if (cpu_present(cpu))
		return cpu_data(cpu).apicid;
	else
		return BAD_APICID;
}

static struct apic xen_pv_apic = {
	.name 				= "Xen PV",
	.probe 				= xen_apic_probe_pv,
	.acpi_madt_oem_check		= xen_madt_oem_check,
	.apic_id_valid 			= xen_id_always_valid,
	.apic_id_registered 		= xen_id_always_registered,

	/* .delivery_mode and .dest_mode_logical not used by XENPV */

	.disable_esr			= 0,

	.check_apicid_used		= default_check_apicid_used, /* Used on 32-bit */
	.init_apic_ldr			= xen_noop, /* setup_local_APIC calls it */
	.ioapic_phys_id_map		= default_ioapic_phys_id_map, /* Used on 32-bit */
	.setup_apic_routing		= NULL,
	.cpu_present_to_apicid		= xen_cpu_present_to_apicid,
	.apicid_to_cpu_present		= physid_set_mask_of_physid, /* Used on 32-bit */
	.check_phys_apicid_present	= default_check_phys_apicid_present, /* smp_sanity_check needs it */
	.phys_pkg_id			= xen_phys_pkg_id, /* detect_ht */

	.get_apic_id 			= xen_get_apic_id,
	.set_apic_id 			= xen_set_apic_id, /* Can be NULL on 32-bit. */

	.calc_dest_apicid		= apic_flat_calc_apicid,

#ifdef CONFIG_SMP
	.send_IPI_mask 			= xen_send_IPI_mask,
	.send_IPI_mask_allbutself 	= xen_send_IPI_mask_allbutself,
	.send_IPI_allbutself 		= xen_send_IPI_allbutself,
	.send_IPI_all 			= xen_send_IPI_all,
	.send_IPI_self 			= xen_send_IPI_self,
#endif
	/* .wait_for_init_deassert- used  by AP bootup - smp_callin which we don't use */
	.inquire_remote_apic		= xen_silent_inquire,

	.read				= xen_apic_read,
	.write				= xen_apic_write,
	.eoi_write			= xen_apic_write,

	.icr_read 			= xen_apic_icr_read,
	.icr_write 			= xen_apic_icr_write,
	.wait_icr_idle 			= xen_noop,
	.safe_wait_icr_idle 		= xen_safe_apic_wait_icr_idle,
};

static void __init xen_apic_check(void)
{
	if (apic == &xen_pv_apic)
		return;

	pr_info("Switched APIC routing from %s to %s.\n", apic->name,
		xen_pv_apic.name);
	apic = &xen_pv_apic;
}
void __init xen_init_apic(void)
{
	x86_apic_ops.io_apic_read = xen_io_apic_read;
	/* On PV guests the APIC CPUID bit is disabled so none of the
	 * routines end up executing. */
	if (!xen_initial_domain())
		apic = &xen_pv_apic;

	x86_platform.apic_post_init = xen_apic_check;
}
apic_driver(xen_pv_apic);
