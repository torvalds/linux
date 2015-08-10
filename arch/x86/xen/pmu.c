#include <linux/types.h>
#include <linux/interrupt.h>

#include <asm/xen/hypercall.h>
#include <xen/page.h>
#include <xen/interface/xen.h>
#include <xen/interface/vcpu.h>
#include <xen/interface/xenpmu.h>

#include "xen-ops.h"
#include "pmu.h"

/* x86_pmu.handle_irq definition */
#include "../kernel/cpu/perf_event.h"


/* Shared page between hypervisor and domain */
static DEFINE_PER_CPU(struct xen_pmu_data *, xenpmu_shared);
#define get_xenpmu_data()    per_cpu(xenpmu_shared, smp_processor_id())

/* perf callbacks */
static int xen_is_in_guest(void)
{
	const struct xen_pmu_data *xenpmu_data = get_xenpmu_data();

	if (!xenpmu_data) {
		pr_warn_once("%s: pmudata not initialized\n", __func__);
		return 0;
	}

	if (!xen_initial_domain() || (xenpmu_data->domain_id >= DOMID_SELF))
		return 0;

	return 1;
}

static int xen_is_user_mode(void)
{
	const struct xen_pmu_data *xenpmu_data = get_xenpmu_data();

	if (!xenpmu_data) {
		pr_warn_once("%s: pmudata not initialized\n", __func__);
		return 0;
	}

	if (xenpmu_data->pmu.pmu_flags & PMU_SAMPLE_PV)
		return (xenpmu_data->pmu.pmu_flags & PMU_SAMPLE_USER);
	else
		return !!(xenpmu_data->pmu.r.regs.cpl & 3);
}

static unsigned long xen_get_guest_ip(void)
{
	const struct xen_pmu_data *xenpmu_data = get_xenpmu_data();

	if (!xenpmu_data) {
		pr_warn_once("%s: pmudata not initialized\n", __func__);
		return 0;
	}

	return xenpmu_data->pmu.r.regs.ip;
}

static struct perf_guest_info_callbacks xen_guest_cbs = {
	.is_in_guest            = xen_is_in_guest,
	.is_user_mode           = xen_is_user_mode,
	.get_guest_ip           = xen_get_guest_ip,
};

/* Convert registers from Xen's format to Linux' */
static void xen_convert_regs(const struct xen_pmu_regs *xen_regs,
			     struct pt_regs *regs, uint64_t pmu_flags)
{
	regs->ip = xen_regs->ip;
	regs->cs = xen_regs->cs;
	regs->sp = xen_regs->sp;

	if (pmu_flags & PMU_SAMPLE_PV) {
		if (pmu_flags & PMU_SAMPLE_USER)
			regs->cs |= 3;
		else
			regs->cs &= ~3;
	} else {
		if (xen_regs->cpl)
			regs->cs |= 3;
		else
			regs->cs &= ~3;
	}
}

irqreturn_t xen_pmu_irq_handler(int irq, void *dev_id)
{
	int ret = IRQ_NONE;
	struct pt_regs regs;
	const struct xen_pmu_data *xenpmu_data = get_xenpmu_data();

	if (!xenpmu_data) {
		pr_warn_once("%s: pmudata not initialized\n", __func__);
		return ret;
	}

	xen_convert_regs(&xenpmu_data->pmu.r.regs, &regs,
			 xenpmu_data->pmu.pmu_flags);
	if (x86_pmu.handle_irq(&regs))
		ret = IRQ_HANDLED;

	return ret;
}

bool is_xen_pmu(int cpu)
{
	return (per_cpu(xenpmu_shared, cpu) != NULL);
}

void xen_pmu_init(int cpu)
{
	int err;
	struct xen_pmu_params xp;
	unsigned long pfn;
	struct xen_pmu_data *xenpmu_data;

	BUILD_BUG_ON(sizeof(struct xen_pmu_data) > PAGE_SIZE);

	if (xen_hvm_domain())
		return;

	xenpmu_data = (struct xen_pmu_data *)get_zeroed_page(GFP_KERNEL);
	if (!xenpmu_data) {
		pr_err("VPMU init: No memory\n");
		return;
	}
	pfn = virt_to_pfn(xenpmu_data);

	xp.val = pfn_to_mfn(pfn);
	xp.vcpu = cpu;
	xp.version.maj = XENPMU_VER_MAJ;
	xp.version.min = XENPMU_VER_MIN;
	err = HYPERVISOR_xenpmu_op(XENPMU_init, &xp);
	if (err)
		goto fail;

	per_cpu(xenpmu_shared, cpu) = xenpmu_data;

	if (cpu == 0)
		perf_register_guest_info_callbacks(&xen_guest_cbs);

	return;

fail:
	pr_warn_once("Could not initialize VPMU for cpu %d, error %d\n",
		cpu, err);
	free_pages((unsigned long)xenpmu_data, 0);
}

void xen_pmu_finish(int cpu)
{
	struct xen_pmu_params xp;

	if (xen_hvm_domain())
		return;

	xp.vcpu = cpu;
	xp.version.maj = XENPMU_VER_MAJ;
	xp.version.min = XENPMU_VER_MIN;

	(void)HYPERVISOR_xenpmu_op(XENPMU_finish, &xp);

	free_pages((unsigned long)per_cpu(xenpmu_shared, cpu), 0);
	per_cpu(xenpmu_shared, cpu) = NULL;
}
