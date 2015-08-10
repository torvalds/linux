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


/* AMD PMU */
#define F15H_NUM_COUNTERS   6
#define F10H_NUM_COUNTERS   4

static __read_mostly uint32_t amd_counters_base;
static __read_mostly uint32_t amd_ctrls_base;
static __read_mostly int amd_msr_step;
static __read_mostly int k7_counters_mirrored;
static __read_mostly int amd_num_counters;

/* Intel PMU */
#define MSR_TYPE_COUNTER            0
#define MSR_TYPE_CTRL               1
#define MSR_TYPE_GLOBAL             2
#define MSR_TYPE_ARCH_COUNTER       3
#define MSR_TYPE_ARCH_CTRL          4

/* Number of general pmu registers (CPUID.EAX[0xa].EAX[8..15]) */
#define PMU_GENERAL_NR_SHIFT        8
#define PMU_GENERAL_NR_BITS         8
#define PMU_GENERAL_NR_MASK         (((1 << PMU_GENERAL_NR_BITS) - 1) \
				     << PMU_GENERAL_NR_SHIFT)

/* Number of fixed pmu registers (CPUID.EDX[0xa].EDX[0..4]) */
#define PMU_FIXED_NR_SHIFT          0
#define PMU_FIXED_NR_BITS           5
#define PMU_FIXED_NR_MASK           (((1 << PMU_FIXED_NR_BITS) - 1) \
				     << PMU_FIXED_NR_SHIFT)

/* Alias registers (0x4c1) for full-width writes to PMCs */
#define MSR_PMC_ALIAS_MASK          (~(MSR_IA32_PERFCTR0 ^ MSR_IA32_PMC0))

static __read_mostly int intel_num_arch_counters, intel_num_fixed_counters;


static void xen_pmu_arch_init(void)
{
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD) {

		switch (boot_cpu_data.x86) {
		case 0x15:
			amd_num_counters = F15H_NUM_COUNTERS;
			amd_counters_base = MSR_F15H_PERF_CTR;
			amd_ctrls_base = MSR_F15H_PERF_CTL;
			amd_msr_step = 2;
			k7_counters_mirrored = 1;
			break;
		case 0x10:
		case 0x12:
		case 0x14:
		case 0x16:
		default:
			amd_num_counters = F10H_NUM_COUNTERS;
			amd_counters_base = MSR_K7_PERFCTR0;
			amd_ctrls_base = MSR_K7_EVNTSEL0;
			amd_msr_step = 1;
			k7_counters_mirrored = 0;
			break;
		}
	} else {
		uint32_t eax, ebx, ecx, edx;

		cpuid(0xa, &eax, &ebx, &ecx, &edx);

		intel_num_arch_counters = (eax & PMU_GENERAL_NR_MASK) >>
			PMU_GENERAL_NR_SHIFT;
		intel_num_fixed_counters = (edx & PMU_FIXED_NR_MASK) >>
			PMU_FIXED_NR_SHIFT;
	}
}

static inline uint32_t get_fam15h_addr(u32 addr)
{
	switch (addr) {
	case MSR_K7_PERFCTR0:
	case MSR_K7_PERFCTR1:
	case MSR_K7_PERFCTR2:
	case MSR_K7_PERFCTR3:
		return MSR_F15H_PERF_CTR + (addr - MSR_K7_PERFCTR0);
	case MSR_K7_EVNTSEL0:
	case MSR_K7_EVNTSEL1:
	case MSR_K7_EVNTSEL2:
	case MSR_K7_EVNTSEL3:
		return MSR_F15H_PERF_CTL + (addr - MSR_K7_EVNTSEL0);
	default:
		break;
	}

	return addr;
}

static inline bool is_amd_pmu_msr(unsigned int msr)
{
	if ((msr >= MSR_F15H_PERF_CTL &&
	     msr < MSR_F15H_PERF_CTR + (amd_num_counters * 2)) ||
	    (msr >= MSR_K7_EVNTSEL0 &&
	     msr < MSR_K7_PERFCTR0 + amd_num_counters))
		return true;

	return false;
}

static int is_intel_pmu_msr(u32 msr_index, int *type, int *index)
{
	u32 msr_index_pmc;

	switch (msr_index) {
	case MSR_CORE_PERF_FIXED_CTR_CTRL:
	case MSR_IA32_DS_AREA:
	case MSR_IA32_PEBS_ENABLE:
		*type = MSR_TYPE_CTRL;
		return true;

	case MSR_CORE_PERF_GLOBAL_CTRL:
	case MSR_CORE_PERF_GLOBAL_STATUS:
	case MSR_CORE_PERF_GLOBAL_OVF_CTRL:
		*type = MSR_TYPE_GLOBAL;
		return true;

	default:

		if ((msr_index >= MSR_CORE_PERF_FIXED_CTR0) &&
		    (msr_index < MSR_CORE_PERF_FIXED_CTR0 +
				 intel_num_fixed_counters)) {
			*index = msr_index - MSR_CORE_PERF_FIXED_CTR0;
			*type = MSR_TYPE_COUNTER;
			return true;
		}

		if ((msr_index >= MSR_P6_EVNTSEL0) &&
		    (msr_index < MSR_P6_EVNTSEL0 +  intel_num_arch_counters)) {
			*index = msr_index - MSR_P6_EVNTSEL0;
			*type = MSR_TYPE_ARCH_CTRL;
			return true;
		}

		msr_index_pmc = msr_index & MSR_PMC_ALIAS_MASK;
		if ((msr_index_pmc >= MSR_IA32_PERFCTR0) &&
		    (msr_index_pmc < MSR_IA32_PERFCTR0 +
				     intel_num_arch_counters)) {
			*type = MSR_TYPE_ARCH_COUNTER;
			*index = msr_index_pmc - MSR_IA32_PERFCTR0;
			return true;
		}
		return false;
	}
}

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

	if (cpu == 0) {
		perf_register_guest_info_callbacks(&xen_guest_cbs);
		xen_pmu_arch_init();
	}

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
