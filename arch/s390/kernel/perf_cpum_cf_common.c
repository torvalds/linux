// SPDX-License-Identifier: GPL-2.0
/*
 * CPU-Measurement Counter Facility Support - Common Layer
 *
 *  Copyright IBM Corp. 2019
 *  Author(s): Hendrik Brueckner <brueckner@linux.ibm.com>
 */
#define KMSG_COMPONENT	"cpum_cf_common"
#define pr_fmt(fmt)	KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <linux/export.h>
#include <asm/ctl_reg.h>
#include <asm/irq.h>
#include <asm/cpu_mcf.h>

/* Per-CPU event structure for the counter facility */
DEFINE_PER_CPU(struct cpu_cf_events, cpu_cf_events) = {
	.ctr_set = {
		[CPUMF_CTR_SET_BASIC]	= ATOMIC_INIT(0),
		[CPUMF_CTR_SET_USER]	= ATOMIC_INIT(0),
		[CPUMF_CTR_SET_CRYPTO]	= ATOMIC_INIT(0),
		[CPUMF_CTR_SET_EXT]	= ATOMIC_INIT(0),
		[CPUMF_CTR_SET_MT_DIAG] = ATOMIC_INIT(0),
	},
	.alert = ATOMIC64_INIT(0),
	.state = 0,
	.flags = 0,
};
/* Indicator whether the CPU-Measurement Counter Facility Support is ready */
static bool cpum_cf_initalized;

/* CPU-measurement alerts for the counter facility */
static void cpumf_measurement_alert(struct ext_code ext_code,
				    unsigned int alert, unsigned long unused)
{
	struct cpu_cf_events *cpuhw;

	if (!(alert & CPU_MF_INT_CF_MASK))
		return;

	inc_irq_stat(IRQEXT_CMC);
	cpuhw = this_cpu_ptr(&cpu_cf_events);

	/* Measurement alerts are shared and might happen when the PMU
	 * is not reserved.  Ignore these alerts in this case. */
	if (!(cpuhw->flags & PMU_F_RESERVED))
		return;

	/* counter authorization change alert */
	if (alert & CPU_MF_INT_CF_CACA)
		qctri(&cpuhw->info);

	/* loss of counter data alert */
	if (alert & CPU_MF_INT_CF_LCDA)
		pr_err("CPU[%i] Counter data was lost\n", smp_processor_id());

	/* loss of MT counter data alert */
	if (alert & CPU_MF_INT_CF_MTDA)
		pr_warn("CPU[%i] MT counter data was lost\n",
			smp_processor_id());

	/* store alert for special handling by in-kernel users */
	atomic64_or(alert, &cpuhw->alert);
}

#define PMC_INIT      0
#define PMC_RELEASE   1
static void cpum_cf_setup_cpu(void *flags)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);

	switch (*((int *) flags)) {
	case PMC_INIT:
		memset(&cpuhw->info, 0, sizeof(cpuhw->info));
		qctri(&cpuhw->info);
		cpuhw->flags |= PMU_F_RESERVED;
		break;

	case PMC_RELEASE:
		cpuhw->flags &= ~PMU_F_RESERVED;
		break;
	}

	/* Disable CPU counter sets */
	lcctl(0);
}

bool kernel_cpumcf_avail(void)
{
	return cpum_cf_initalized;
}
EXPORT_SYMBOL(kernel_cpumcf_avail);


/* Reserve/release functions for sharing perf hardware */
static DEFINE_SPINLOCK(cpumcf_owner_lock);
static void *cpumcf_owner;

/* Initialize the CPU-measurement counter facility */
int __kernel_cpumcf_begin(void)
{
	int flags = PMC_INIT;
	int err = 0;

	spin_lock(&cpumcf_owner_lock);
	if (cpumcf_owner)
		err = -EBUSY;
	else
		cpumcf_owner = __builtin_return_address(0);
	spin_unlock(&cpumcf_owner_lock);
	if (err)
		return err;

	on_each_cpu(cpum_cf_setup_cpu, &flags, 1);
	irq_subclass_register(IRQ_SUBCLASS_MEASUREMENT_ALERT);

	return 0;
}
EXPORT_SYMBOL(__kernel_cpumcf_begin);

/* Obtain the CPU-measurement alerts for the counter facility */
unsigned long kernel_cpumcf_alert(int clear)
{
	struct cpu_cf_events *cpuhw = this_cpu_ptr(&cpu_cf_events);
	unsigned long alert;

	alert = atomic64_read(&cpuhw->alert);
	if (clear)
		atomic64_set(&cpuhw->alert, 0);

	return alert;
}
EXPORT_SYMBOL(kernel_cpumcf_alert);

/* Release the CPU-measurement counter facility */
void __kernel_cpumcf_end(void)
{
	int flags = PMC_RELEASE;

	on_each_cpu(cpum_cf_setup_cpu, &flags, 1);
	irq_subclass_unregister(IRQ_SUBCLASS_MEASUREMENT_ALERT);

	spin_lock(&cpumcf_owner_lock);
	cpumcf_owner = NULL;
	spin_unlock(&cpumcf_owner_lock);
}
EXPORT_SYMBOL(__kernel_cpumcf_end);

static int cpum_cf_setup(unsigned int cpu, int flags)
{
	local_irq_disable();
	cpum_cf_setup_cpu(&flags);
	local_irq_enable();
	return 0;
}

static int cpum_cf_online_cpu(unsigned int cpu)
{
	return cpum_cf_setup(cpu, PMC_INIT);
}

static int cpum_cf_offline_cpu(unsigned int cpu)
{
	return cpum_cf_setup(cpu, PMC_RELEASE);
}

/* Return the maximum possible counter set size (in number of 8 byte counters)
 * depending on type and model number.
 */
size_t cpum_cf_ctrset_size(enum cpumf_ctr_set ctrset,
			   struct cpumf_ctr_info *info)
{
	size_t ctrset_size = 0;

	switch (ctrset) {
	case CPUMF_CTR_SET_BASIC:
		if (info->cfvn >= 1)
			ctrset_size = 6;
		break;
	case CPUMF_CTR_SET_USER:
		if (info->cfvn == 1)
			ctrset_size = 6;
		else if (info->cfvn >= 3)
			ctrset_size = 2;
		break;
	case CPUMF_CTR_SET_CRYPTO:
		if (info->csvn >= 1 && info->csvn <= 5)
			ctrset_size = 16;
		else if (info->csvn == 6)
			ctrset_size = 20;
		break;
	case CPUMF_CTR_SET_EXT:
		if (info->csvn == 1)
			ctrset_size = 32;
		else if (info->csvn == 2)
			ctrset_size = 48;
		else if (info->csvn >= 3 && info->csvn <= 5)
			ctrset_size = 128;
		else if (info->csvn == 6)
			ctrset_size = 160;
		break;
	case CPUMF_CTR_SET_MT_DIAG:
		if (info->csvn > 3)
			ctrset_size = 48;
		break;
	case CPUMF_CTR_SET_MAX:
		break;
	}

	return ctrset_size;
}

static int __init cpum_cf_init(void)
{
	int rc;

	if (!cpum_cf_avail())
		return -ENODEV;

	/* clear bit 15 of cr0 to unauthorize problem-state to
	 * extract measurement counters */
	ctl_clear_bit(0, 48);

	/* register handler for measurement-alert interruptions */
	rc = register_external_irq(EXT_IRQ_MEASURE_ALERT,
				   cpumf_measurement_alert);
	if (rc) {
		pr_err("Registering for CPU-measurement alerts "
		       "failed with rc=%i\n", rc);
		return rc;
	}

	rc = cpuhp_setup_state(CPUHP_AP_PERF_S390_CF_ONLINE,
				"perf/s390/cf:online",
				cpum_cf_online_cpu, cpum_cf_offline_cpu);
	if (!rc)
		cpum_cf_initalized = true;

	return rc;
}
early_initcall(cpum_cf_init);
