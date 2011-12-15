/*
 * Performance events - AMD IBS
 *
 *  Copyright (C) 2011 Advanced Micro Devices, Inc., Robert Richter
 *
 *  For licencing details see kernel-base/COPYING
 */

#include <linux/perf_event.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <asm/apic.h>

static u32 ibs_caps;

#if defined(CONFIG_PERF_EVENTS) && defined(CONFIG_CPU_SUP_AMD)

#include <linux/kprobes.h>
#include <linux/hardirq.h>

#include <asm/nmi.h>

#define IBS_FETCH_CONFIG_MASK	(IBS_FETCH_RAND_EN | IBS_FETCH_MAX_CNT)
#define IBS_OP_CONFIG_MASK	IBS_OP_MAX_CNT

enum ibs_states {
	IBS_ENABLED	= 0,
	IBS_STARTED	= 1,
	IBS_STOPPING	= 2,

	IBS_MAX_STATES,
};

struct cpu_perf_ibs {
	struct perf_event	*event;
	unsigned long		state[BITS_TO_LONGS(IBS_MAX_STATES)];
};

struct perf_ibs {
	struct pmu	pmu;
	unsigned int	msr;
	u64		config_mask;
	u64		cnt_mask;
	u64		enable_mask;
	u64		valid_mask;
	unsigned long	offset_mask[1];
	int		offset_max;
	struct cpu_perf_ibs __percpu *pcpu;
};

struct perf_ibs_data {
	u32		size;
	union {
		u32	data[0];	/* data buffer starts here */
		u32	caps;
	};
	u64		regs[MSR_AMD64_IBS_REG_COUNT_MAX];
};

static struct perf_ibs perf_ibs_fetch;
static struct perf_ibs perf_ibs_op;

static struct perf_ibs *get_ibs_pmu(int type)
{
	if (perf_ibs_fetch.pmu.type == type)
		return &perf_ibs_fetch;
	if (perf_ibs_op.pmu.type == type)
		return &perf_ibs_op;
	return NULL;
}

static int perf_ibs_init(struct perf_event *event)
{
	struct hw_perf_event *hwc = &event->hw;
	struct perf_ibs *perf_ibs;
	u64 max_cnt, config;

	perf_ibs = get_ibs_pmu(event->attr.type);
	if (!perf_ibs)
		return -ENOENT;

	config = event->attr.config;
	if (config & ~perf_ibs->config_mask)
		return -EINVAL;

	if (hwc->sample_period) {
		if (config & perf_ibs->cnt_mask)
			/* raw max_cnt may not be set */
			return -EINVAL;
		if (hwc->sample_period & 0x0f)
			/* lower 4 bits can not be set in ibs max cnt */
			return -EINVAL;
		max_cnt = hwc->sample_period >> 4;
		if (max_cnt & ~perf_ibs->cnt_mask)
			/* out of range */
			return -EINVAL;
		config |= max_cnt;
	} else {
		max_cnt = config & perf_ibs->cnt_mask;
		event->attr.sample_period = max_cnt << 4;
		hwc->sample_period = event->attr.sample_period;
	}

	if (!max_cnt)
		return -EINVAL;

	hwc->config_base = perf_ibs->msr;
	hwc->config = config;

	return 0;
}

static void perf_ibs_start(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct perf_ibs *perf_ibs = container_of(event->pmu, struct perf_ibs, pmu);
	struct cpu_perf_ibs *pcpu = this_cpu_ptr(perf_ibs->pcpu);

	if (test_and_set_bit(IBS_STARTED, pcpu->state))
		return;

	wrmsrl(hwc->config_base, hwc->config | perf_ibs->enable_mask);
}

static void perf_ibs_stop(struct perf_event *event, int flags)
{
	struct hw_perf_event *hwc = &event->hw;
	struct perf_ibs *perf_ibs = container_of(event->pmu, struct perf_ibs, pmu);
	struct cpu_perf_ibs *pcpu = this_cpu_ptr(perf_ibs->pcpu);
	u64 val;

	if (!test_and_clear_bit(IBS_STARTED, pcpu->state))
		return;

	set_bit(IBS_STOPPING, pcpu->state);

	rdmsrl(hwc->config_base, val);
	val &= ~perf_ibs->enable_mask;
	wrmsrl(hwc->config_base, val);
}

static int perf_ibs_add(struct perf_event *event, int flags)
{
	struct perf_ibs *perf_ibs = container_of(event->pmu, struct perf_ibs, pmu);
	struct cpu_perf_ibs *pcpu = this_cpu_ptr(perf_ibs->pcpu);

	if (test_and_set_bit(IBS_ENABLED, pcpu->state))
		return -ENOSPC;

	pcpu->event = event;

	if (flags & PERF_EF_START)
		perf_ibs_start(event, PERF_EF_RELOAD);

	return 0;
}

static void perf_ibs_del(struct perf_event *event, int flags)
{
	struct perf_ibs *perf_ibs = container_of(event->pmu, struct perf_ibs, pmu);
	struct cpu_perf_ibs *pcpu = this_cpu_ptr(perf_ibs->pcpu);

	if (!test_and_clear_bit(IBS_ENABLED, pcpu->state))
		return;

	perf_ibs_stop(event, 0);

	pcpu->event = NULL;
}

static void perf_ibs_read(struct perf_event *event) { }

static struct perf_ibs perf_ibs_fetch = {
	.pmu = {
		.task_ctx_nr	= perf_invalid_context,

		.event_init	= perf_ibs_init,
		.add		= perf_ibs_add,
		.del		= perf_ibs_del,
		.start		= perf_ibs_start,
		.stop		= perf_ibs_stop,
		.read		= perf_ibs_read,
	},
	.msr			= MSR_AMD64_IBSFETCHCTL,
	.config_mask		= IBS_FETCH_CONFIG_MASK,
	.cnt_mask		= IBS_FETCH_MAX_CNT,
	.enable_mask		= IBS_FETCH_ENABLE,
	.valid_mask		= IBS_FETCH_VAL,
	.offset_mask		= { MSR_AMD64_IBSFETCH_REG_MASK },
	.offset_max		= MSR_AMD64_IBSFETCH_REG_COUNT,
};

static struct perf_ibs perf_ibs_op = {
	.pmu = {
		.task_ctx_nr	= perf_invalid_context,

		.event_init	= perf_ibs_init,
		.add		= perf_ibs_add,
		.del		= perf_ibs_del,
		.start		= perf_ibs_start,
		.stop		= perf_ibs_stop,
		.read		= perf_ibs_read,
	},
	.msr			= MSR_AMD64_IBSOPCTL,
	.config_mask		= IBS_OP_CONFIG_MASK,
	.cnt_mask		= IBS_OP_MAX_CNT,
	.enable_mask		= IBS_OP_ENABLE,
	.valid_mask		= IBS_OP_VAL,
	.offset_mask		= { MSR_AMD64_IBSOP_REG_MASK },
	.offset_max		= MSR_AMD64_IBSOP_REG_COUNT,
};

static int perf_ibs_handle_irq(struct perf_ibs *perf_ibs, struct pt_regs *iregs)
{
	struct cpu_perf_ibs *pcpu = this_cpu_ptr(perf_ibs->pcpu);
	struct perf_event *event = pcpu->event;
	struct hw_perf_event *hwc = &event->hw;
	struct perf_sample_data data;
	struct perf_raw_record raw;
	struct pt_regs regs;
	struct perf_ibs_data ibs_data;
	int offset, size;
	unsigned int msr;
	u64 *buf;

	if (!test_bit(IBS_STARTED, pcpu->state)) {
		/* Catch spurious interrupts after stopping IBS: */
		if (!test_and_clear_bit(IBS_STOPPING, pcpu->state))
			return 0;
		rdmsrl(perf_ibs->msr, *ibs_data.regs);
		return (*ibs_data.regs & perf_ibs->valid_mask) ? 1 : 0;
	}

	msr = hwc->config_base;
	buf = ibs_data.regs;
	rdmsrl(msr, *buf);
	if (!(*buf++ & perf_ibs->valid_mask))
		return 0;

	perf_sample_data_init(&data, 0);
	if (event->attr.sample_type & PERF_SAMPLE_RAW) {
		ibs_data.caps = ibs_caps;
		size = 1;
		offset = 1;
		do {
		    rdmsrl(msr + offset, *buf++);
		    size++;
		    offset = find_next_bit(perf_ibs->offset_mask,
					   perf_ibs->offset_max,
					   offset + 1);
		} while (offset < perf_ibs->offset_max);
		raw.size = sizeof(u32) + sizeof(u64) * size;
		raw.data = ibs_data.data;
		data.raw = &raw;
	}

	regs = *iregs; /* XXX: update ip from ibs sample */

	if (perf_event_overflow(event, &data, &regs))
		; /* stop */
	else
		/* reenable */
		wrmsrl(hwc->config_base, hwc->config | perf_ibs->enable_mask);

	return 1;
}

static int __kprobes
perf_ibs_nmi_handler(unsigned int cmd, struct pt_regs *regs)
{
	int handled = 0;

	handled += perf_ibs_handle_irq(&perf_ibs_fetch, regs);
	handled += perf_ibs_handle_irq(&perf_ibs_op, regs);

	if (handled)
		inc_irq_stat(apic_perf_irqs);

	return handled;
}

static __init int perf_ibs_pmu_init(struct perf_ibs *perf_ibs, char *name)
{
	struct cpu_perf_ibs __percpu *pcpu;
	int ret;

	pcpu = alloc_percpu(struct cpu_perf_ibs);
	if (!pcpu)
		return -ENOMEM;

	perf_ibs->pcpu = pcpu;

	ret = perf_pmu_register(&perf_ibs->pmu, name, -1);
	if (ret) {
		perf_ibs->pcpu = NULL;
		free_percpu(pcpu);
	}

	return ret;
}

static __init int perf_event_ibs_init(void)
{
	if (!ibs_caps)
		return -ENODEV;	/* ibs not supported by the cpu */

	perf_ibs_pmu_init(&perf_ibs_fetch, "ibs_fetch");
	perf_ibs_pmu_init(&perf_ibs_op, "ibs_op");
	register_nmi_handler(NMI_LOCAL, &perf_ibs_nmi_handler, 0, "perf_ibs");
	printk(KERN_INFO "perf: AMD IBS detected (0x%08x)\n", ibs_caps);

	return 0;
}

#else /* defined(CONFIG_PERF_EVENTS) && defined(CONFIG_CPU_SUP_AMD) */

static __init int perf_event_ibs_init(void) { return 0; }

#endif

/* IBS - apic initialization, for perf and oprofile */

static __init u32 __get_ibs_caps(void)
{
	u32 caps;
	unsigned int max_level;

	if (!boot_cpu_has(X86_FEATURE_IBS))
		return 0;

	/* check IBS cpuid feature flags */
	max_level = cpuid_eax(0x80000000);
	if (max_level < IBS_CPUID_FEATURES)
		return IBS_CAPS_DEFAULT;

	caps = cpuid_eax(IBS_CPUID_FEATURES);
	if (!(caps & IBS_CAPS_AVAIL))
		/* cpuid flags not valid */
		return IBS_CAPS_DEFAULT;

	return caps;
}

u32 get_ibs_caps(void)
{
	return ibs_caps;
}

EXPORT_SYMBOL(get_ibs_caps);

static inline int get_eilvt(int offset)
{
	return !setup_APIC_eilvt(offset, 0, APIC_EILVT_MSG_NMI, 1);
}

static inline int put_eilvt(int offset)
{
	return !setup_APIC_eilvt(offset, 0, 0, 1);
}

/*
 * Check and reserve APIC extended interrupt LVT offset for IBS if available.
 */
static inline int ibs_eilvt_valid(void)
{
	int offset;
	u64 val;
	int valid = 0;

	preempt_disable();

	rdmsrl(MSR_AMD64_IBSCTL, val);
	offset = val & IBSCTL_LVT_OFFSET_MASK;

	if (!(val & IBSCTL_LVT_OFFSET_VALID)) {
		pr_err(FW_BUG "cpu %d, invalid IBS interrupt offset %d (MSR%08X=0x%016llx)\n",
		       smp_processor_id(), offset, MSR_AMD64_IBSCTL, val);
		goto out;
	}

	if (!get_eilvt(offset)) {
		pr_err(FW_BUG "cpu %d, IBS interrupt offset %d not available (MSR%08X=0x%016llx)\n",
		       smp_processor_id(), offset, MSR_AMD64_IBSCTL, val);
		goto out;
	}

	valid = 1;
out:
	preempt_enable();

	return valid;
}

static int setup_ibs_ctl(int ibs_eilvt_off)
{
	struct pci_dev *cpu_cfg;
	int nodes;
	u32 value = 0;

	nodes = 0;
	cpu_cfg = NULL;
	do {
		cpu_cfg = pci_get_device(PCI_VENDOR_ID_AMD,
					 PCI_DEVICE_ID_AMD_10H_NB_MISC,
					 cpu_cfg);
		if (!cpu_cfg)
			break;
		++nodes;
		pci_write_config_dword(cpu_cfg, IBSCTL, ibs_eilvt_off
				       | IBSCTL_LVT_OFFSET_VALID);
		pci_read_config_dword(cpu_cfg, IBSCTL, &value);
		if (value != (ibs_eilvt_off | IBSCTL_LVT_OFFSET_VALID)) {
			pci_dev_put(cpu_cfg);
			printk(KERN_DEBUG "Failed to setup IBS LVT offset, "
			       "IBSCTL = 0x%08x\n", value);
			return -EINVAL;
		}
	} while (1);

	if (!nodes) {
		printk(KERN_DEBUG "No CPU node configured for IBS\n");
		return -ENODEV;
	}

	return 0;
}

/*
 * This runs only on the current cpu. We try to find an LVT offset and
 * setup the local APIC. For this we must disable preemption. On
 * success we initialize all nodes with this offset. This updates then
 * the offset in the IBS_CTL per-node msr. The per-core APIC setup of
 * the IBS interrupt vector is handled by perf_ibs_cpu_notifier that
 * is using the new offset.
 */
static int force_ibs_eilvt_setup(void)
{
	int offset;
	int ret;

	preempt_disable();
	/* find the next free available EILVT entry, skip offset 0 */
	for (offset = 1; offset < APIC_EILVT_NR_MAX; offset++) {
		if (get_eilvt(offset))
			break;
	}
	preempt_enable();

	if (offset == APIC_EILVT_NR_MAX) {
		printk(KERN_DEBUG "No EILVT entry available\n");
		return -EBUSY;
	}

	ret = setup_ibs_ctl(offset);
	if (ret)
		goto out;

	if (!ibs_eilvt_valid()) {
		ret = -EFAULT;
		goto out;
	}

	pr_info("IBS: LVT offset %d assigned\n", offset);

	return 0;
out:
	preempt_disable();
	put_eilvt(offset);
	preempt_enable();
	return ret;
}

static inline int get_ibs_lvt_offset(void)
{
	u64 val;

	rdmsrl(MSR_AMD64_IBSCTL, val);
	if (!(val & IBSCTL_LVT_OFFSET_VALID))
		return -EINVAL;

	return val & IBSCTL_LVT_OFFSET_MASK;
}

static void setup_APIC_ibs(void *dummy)
{
	int offset;

	offset = get_ibs_lvt_offset();
	if (offset < 0)
		goto failed;

	if (!setup_APIC_eilvt(offset, 0, APIC_EILVT_MSG_NMI, 0))
		return;
failed:
	pr_warn("perf: IBS APIC setup failed on cpu #%d\n",
		smp_processor_id());
}

static void clear_APIC_ibs(void *dummy)
{
	int offset;

	offset = get_ibs_lvt_offset();
	if (offset >= 0)
		setup_APIC_eilvt(offset, 0, APIC_EILVT_MSG_FIX, 1);
}

static int __cpuinit
perf_ibs_cpu_notifier(struct notifier_block *self, unsigned long action, void *hcpu)
{
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_STARTING:
		setup_APIC_ibs(NULL);
		break;
	case CPU_DYING:
		clear_APIC_ibs(NULL);
		break;
	default:
		break;
	}

	return NOTIFY_OK;
}

static __init int amd_ibs_init(void)
{
	u32 caps;
	int ret = -EINVAL;

	caps = __get_ibs_caps();
	if (!caps)
		return -ENODEV;	/* ibs not supported by the cpu */

	/*
	 * Force LVT offset assignment for family 10h: The offsets are
	 * not assigned by the BIOS for this family, so the OS is
	 * responsible for doing it. If the OS assignment fails, fall
	 * back to BIOS settings and try to setup this.
	 */
	if (boot_cpu_data.x86 == 0x10)
		force_ibs_eilvt_setup();

	if (!ibs_eilvt_valid())
		goto out;

	get_online_cpus();
	ibs_caps = caps;
	/* make ibs_caps visible to other cpus: */
	smp_mb();
	perf_cpu_notifier(perf_ibs_cpu_notifier);
	smp_call_function(setup_APIC_ibs, NULL, 1);
	put_online_cpus();

	ret = perf_event_ibs_init();
out:
	if (ret)
		pr_err("Failed to setup IBS, %d\n", ret);
	return ret;
}

/* Since we need the pci subsystem to init ibs we can't do this earlier: */
device_initcall(amd_ibs_init);
