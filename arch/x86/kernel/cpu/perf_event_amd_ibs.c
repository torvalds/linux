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
#include <linux/syscore_ops.h>

#include <asm/apic.h>

static u32 ibs_caps;

#if defined(CONFIG_PERF_EVENTS) && defined(CONFIG_CPU_SUP_AMD)

static struct pmu perf_ibs;

static int perf_ibs_init(struct perf_event *event)
{
	if (perf_ibs.type != event->attr.type)
		return -ENOENT;
	return 0;
}

static int perf_ibs_add(struct perf_event *event, int flags)
{
	return 0;
}

static void perf_ibs_del(struct perf_event *event, int flags)
{
}

static struct pmu perf_ibs = {
	.event_init= perf_ibs_init,
	.add= perf_ibs_add,
	.del= perf_ibs_del,
};

static __init int perf_event_ibs_init(void)
{
	if (!ibs_caps)
		return -ENODEV;	/* ibs not supported by the cpu */

	perf_pmu_register(&perf_ibs, "ibs", -1);
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

static void ibs_eilvt_setup(void)
{
	/*
	 * Force LVT offset assignment for family 10h: The offsets are
	 * not assigned by the BIOS for this family, so the OS is
	 * responsible for doing it. If the OS assignment fails, fall
	 * back to BIOS settings and try to setup this.
	 */
	if (boot_cpu_data.x86 == 0x10)
		force_ibs_eilvt_setup();
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

#ifdef CONFIG_PM

static int perf_ibs_suspend(void)
{
	clear_APIC_ibs(NULL);
	return 0;
}

static void perf_ibs_resume(void)
{
	ibs_eilvt_setup();
	setup_APIC_ibs(NULL);
}

static struct syscore_ops perf_ibs_syscore_ops = {
	.resume		= perf_ibs_resume,
	.suspend	= perf_ibs_suspend,
};

static void perf_ibs_pm_init(void)
{
	register_syscore_ops(&perf_ibs_syscore_ops);
}

#else

static inline void perf_ibs_pm_init(void) { }

#endif

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

	ibs_eilvt_setup();

	if (!ibs_eilvt_valid())
		goto out;

	perf_ibs_pm_init();
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
