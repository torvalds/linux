// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/preempt.h>
#include <linux/smp.h>
#include <linux/completion.h>
#include <asm/msr.h>

static void __rdmsr_on_cpu(void *info)
{
	struct msr_info *rv = info;
	struct msr *reg;
	int this_cpu = raw_smp_processor_id();

	if (rv->msrs)
		reg = per_cpu_ptr(rv->msrs, this_cpu);
	else
		reg = &rv->reg;

	rdmsr(rv->msr_no, reg->l, reg->h);
}

static void __wrmsr_on_cpu(void *info)
{
	struct msr_info *rv = info;
	struct msr *reg;
	int this_cpu = raw_smp_processor_id();

	if (rv->msrs)
		reg = per_cpu_ptr(rv->msrs, this_cpu);
	else
		reg = &rv->reg;

	wrmsr(rv->msr_no, reg->l, reg->h);
}

int rdmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h)
{
	int err;
	struct msr_info rv;

	memset(&rv, 0, sizeof(rv));

	rv.msr_no = msr_no;
	err = smp_call_function_single(cpu, __rdmsr_on_cpu, &rv, 1);
	*l = rv.reg.l;
	*h = rv.reg.h;

	return err;
}
EXPORT_SYMBOL(rdmsr_on_cpu);

int rdmsrl_on_cpu(unsigned int cpu, u32 msr_no, u64 *q)
{
	int err;
	struct msr_info rv;

	memset(&rv, 0, sizeof(rv));

	rv.msr_no = msr_no;
	err = smp_call_function_single(cpu, __rdmsr_on_cpu, &rv, 1);
	*q = rv.reg.q;

	return err;
}
EXPORT_SYMBOL(rdmsrl_on_cpu);

int wrmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h)
{
	int err;
	struct msr_info rv;

	memset(&rv, 0, sizeof(rv));

	rv.msr_no = msr_no;
	rv.reg.l = l;
	rv.reg.h = h;
	err = smp_call_function_single(cpu, __wrmsr_on_cpu, &rv, 1);

	return err;
}
EXPORT_SYMBOL(wrmsr_on_cpu);

int wrmsrl_on_cpu(unsigned int cpu, u32 msr_no, u64 q)
{
	int err;
	struct msr_info rv;

	memset(&rv, 0, sizeof(rv));

	rv.msr_no = msr_no;
	rv.reg.q = q;

	err = smp_call_function_single(cpu, __wrmsr_on_cpu, &rv, 1);

	return err;
}
EXPORT_SYMBOL(wrmsrl_on_cpu);

static void __rwmsr_on_cpus(const struct cpumask *mask, u32 msr_no,
			    struct msr *msrs,
			    void (*msr_func) (void *info))
{
	struct msr_info rv;
	int this_cpu;

	memset(&rv, 0, sizeof(rv));

	rv.msrs	  = msrs;
	rv.msr_no = msr_no;

	this_cpu = get_cpu();

	if (cpumask_test_cpu(this_cpu, mask))
		msr_func(&rv);

	smp_call_function_many(mask, msr_func, &rv, 1);
	put_cpu();
}

/* rdmsr on a bunch of CPUs
 *
 * @mask:       which CPUs
 * @msr_no:     which MSR
 * @msrs:       array of MSR values
 *
 */
void rdmsr_on_cpus(const struct cpumask *mask, u32 msr_no, struct msr *msrs)
{
	__rwmsr_on_cpus(mask, msr_no, msrs, __rdmsr_on_cpu);
}
EXPORT_SYMBOL(rdmsr_on_cpus);

/*
 * wrmsr on a bunch of CPUs
 *
 * @mask:       which CPUs
 * @msr_no:     which MSR
 * @msrs:       array of MSR values
 *
 */
void wrmsr_on_cpus(const struct cpumask *mask, u32 msr_no, struct msr *msrs)
{
	__rwmsr_on_cpus(mask, msr_no, msrs, __wrmsr_on_cpu);
}
EXPORT_SYMBOL(wrmsr_on_cpus);

struct msr_info_completion {
	struct msr_info		msr;
	struct completion	done;
};

/* These "safe" variants are slower and should be used when the target MSR
   may not actually exist. */
static void __rdmsr_safe_on_cpu(void *info)
{
	struct msr_info_completion *rv = info;

	rv->msr.err = rdmsr_safe(rv->msr.msr_no, &rv->msr.reg.l, &rv->msr.reg.h);
	complete(&rv->done);
}

static void __wrmsr_safe_on_cpu(void *info)
{
	struct msr_info *rv = info;

	rv->err = wrmsr_safe(rv->msr_no, rv->reg.l, rv->reg.h);
}

int rdmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h)
{
	struct msr_info_completion rv;
	call_single_data_t csd = {
		.func	= __rdmsr_safe_on_cpu,
		.info	= &rv,
	};
	int err;

	memset(&rv, 0, sizeof(rv));
	init_completion(&rv.done);
	rv.msr.msr_no = msr_no;

	err = smp_call_function_single_async(cpu, &csd);
	if (!err) {
		wait_for_completion(&rv.done);
		err = rv.msr.err;
	}
	*l = rv.msr.reg.l;
	*h = rv.msr.reg.h;

	return err;
}
EXPORT_SYMBOL(rdmsr_safe_on_cpu);

int wrmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h)
{
	int err;
	struct msr_info rv;

	memset(&rv, 0, sizeof(rv));

	rv.msr_no = msr_no;
	rv.reg.l = l;
	rv.reg.h = h;
	err = smp_call_function_single(cpu, __wrmsr_safe_on_cpu, &rv, 1);

	return err ? err : rv.err;
}
EXPORT_SYMBOL(wrmsr_safe_on_cpu);

int wrmsrl_safe_on_cpu(unsigned int cpu, u32 msr_no, u64 q)
{
	int err;
	struct msr_info rv;

	memset(&rv, 0, sizeof(rv));

	rv.msr_no = msr_no;
	rv.reg.q = q;

	err = smp_call_function_single(cpu, __wrmsr_safe_on_cpu, &rv, 1);

	return err ? err : rv.err;
}
EXPORT_SYMBOL(wrmsrl_safe_on_cpu);

int rdmsrl_safe_on_cpu(unsigned int cpu, u32 msr_no, u64 *q)
{
	u32 low, high;
	int err;

	err = rdmsr_safe_on_cpu(cpu, msr_no, &low, &high);
	*q = (u64)high << 32 | low;

	return err;
}
EXPORT_SYMBOL(rdmsrl_safe_on_cpu);

/*
 * These variants are significantly slower, but allows control over
 * the entire 32-bit GPR set.
 */
static void __rdmsr_safe_regs_on_cpu(void *info)
{
	struct msr_regs_info *rv = info;

	rv->err = rdmsr_safe_regs(rv->regs);
}

static void __wrmsr_safe_regs_on_cpu(void *info)
{
	struct msr_regs_info *rv = info;

	rv->err = wrmsr_safe_regs(rv->regs);
}

int rdmsr_safe_regs_on_cpu(unsigned int cpu, u32 *regs)
{
	int err;
	struct msr_regs_info rv;

	rv.regs   = regs;
	rv.err    = -EIO;
	err = smp_call_function_single(cpu, __rdmsr_safe_regs_on_cpu, &rv, 1);

	return err ? err : rv.err;
}
EXPORT_SYMBOL(rdmsr_safe_regs_on_cpu);

int wrmsr_safe_regs_on_cpu(unsigned int cpu, u32 *regs)
{
	int err;
	struct msr_regs_info rv;

	rv.regs = regs;
	rv.err  = -EIO;
	err = smp_call_function_single(cpu, __wrmsr_safe_regs_on_cpu, &rv, 1);

	return err ? err : rv.err;
}
EXPORT_SYMBOL(wrmsr_safe_regs_on_cpu);
