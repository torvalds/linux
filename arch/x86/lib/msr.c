#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/smp.h>
#include <asm/msr.h>

struct msr_info {
	u32 msr_no;
	struct msr reg;
	struct msr *msrs;
	int off;
	int err;
};

static void __rdmsr_on_cpu(void *info)
{
	struct msr_info *rv = info;
	struct msr *reg;
	int this_cpu = raw_smp_processor_id();

	if (rv->msrs)
		reg = &rv->msrs[this_cpu - rv->off];
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
		reg = &rv->msrs[this_cpu - rv->off];
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

/* rdmsr on a bunch of CPUs
 *
 * @mask:       which CPUs
 * @msr_no:     which MSR
 * @msrs:       array of MSR values
 *
 */
void rdmsr_on_cpus(const cpumask_t *mask, u32 msr_no, struct msr *msrs)
{
	struct msr_info rv;
	int this_cpu;

	memset(&rv, 0, sizeof(rv));

	rv.off    = cpumask_first(mask);
	rv.msrs	  = msrs;
	rv.msr_no = msr_no;

	this_cpu = get_cpu();

	if (cpumask_test_cpu(this_cpu, mask))
		__rdmsr_on_cpu(&rv);

	smp_call_function_many(mask, __rdmsr_on_cpu, &rv, 1);
	put_cpu();
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
void wrmsr_on_cpus(const cpumask_t *mask, u32 msr_no, struct msr *msrs)
{
	struct msr_info rv;
	int this_cpu;

	memset(&rv, 0, sizeof(rv));

	rv.off    = cpumask_first(mask);
	rv.msrs   = msrs;
	rv.msr_no = msr_no;

	this_cpu = get_cpu();

	if (cpumask_test_cpu(this_cpu, mask))
		__wrmsr_on_cpu(&rv);

	smp_call_function_many(mask, __wrmsr_on_cpu, &rv, 1);
	put_cpu();
}
EXPORT_SYMBOL(wrmsr_on_cpus);

/* These "safe" variants are slower and should be used when the target MSR
   may not actually exist. */
static void __rdmsr_safe_on_cpu(void *info)
{
	struct msr_info *rv = info;

	rv->err = rdmsr_safe(rv->msr_no, &rv->reg.l, &rv->reg.h);
}

static void __wrmsr_safe_on_cpu(void *info)
{
	struct msr_info *rv = info;

	rv->err = wrmsr_safe(rv->msr_no, rv->reg.l, rv->reg.h);
}

int rdmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h)
{
	int err;
	struct msr_info rv;

	memset(&rv, 0, sizeof(rv));

	rv.msr_no = msr_no;
	err = smp_call_function_single(cpu, __rdmsr_safe_on_cpu, &rv, 1);
	*l = rv.reg.l;
	*h = rv.reg.h;

	return err ? err : rv.err;
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

/*
 * These variants are significantly slower, but allows control over
 * the entire 32-bit GPR set.
 */
struct msr_regs_info {
	u32 *regs;
	int err;
};

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
