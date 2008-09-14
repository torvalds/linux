#include <linux/module.h>
#include <linux/preempt.h>
#include <linux/smp.h>
#include <asm/msr.h>

struct msr_info {
	u32 msr_no;
	u32 l, h;
	int err;
};

static void __rdmsr_on_cpu(void *info)
{
	struct msr_info *rv = info;

	rdmsr(rv->msr_no, rv->l, rv->h);
}

static void __rdmsr_safe_on_cpu(void *info)
{
	struct msr_info *rv = info;

	rv->err = rdmsr_safe(rv->msr_no, &rv->l, &rv->h);
}

static int _rdmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h, int safe)
{
	int err = 0;
	struct msr_info rv;

	rv.msr_no = msr_no;
	if (safe) {
		err = smp_call_function_single(cpu, __rdmsr_safe_on_cpu,
					       &rv, 1);
		err = err ? err : rv.err;
	} else {
		err = smp_call_function_single(cpu, __rdmsr_on_cpu, &rv, 1);
	}
	*l = rv.l;
	*h = rv.h;

	return err;
}

static void __wrmsr_on_cpu(void *info)
{
	struct msr_info *rv = info;

	wrmsr(rv->msr_no, rv->l, rv->h);
}

static void __wrmsr_safe_on_cpu(void *info)
{
	struct msr_info *rv = info;

	rv->err = wrmsr_safe(rv->msr_no, rv->l, rv->h);
}

static int _wrmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h, int safe)
{
	int err = 0;
	struct msr_info rv;

	rv.msr_no = msr_no;
	rv.l = l;
	rv.h = h;
	if (safe) {
		err = smp_call_function_single(cpu, __wrmsr_safe_on_cpu,
					       &rv, 1);
		err = err ? err : rv.err;
	} else {
		err = smp_call_function_single(cpu, __wrmsr_on_cpu, &rv, 1);
	}

	return err;
}

int wrmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h)
{
	return _wrmsr_on_cpu(cpu, msr_no, l, h, 0);
}

int rdmsr_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h)
{
	return _rdmsr_on_cpu(cpu, msr_no, l, h, 0);
}

/* These "safe" variants are slower and should be used when the target MSR
   may not actually exist. */
int wrmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 l, u32 h)
{
	return _wrmsr_on_cpu(cpu, msr_no, l, h, 1);
}

int rdmsr_safe_on_cpu(unsigned int cpu, u32 msr_no, u32 *l, u32 *h)
{
	return _rdmsr_on_cpu(cpu, msr_no, l, h, 1);
}

EXPORT_SYMBOL(rdmsr_on_cpu);
EXPORT_SYMBOL(wrmsr_on_cpu);
EXPORT_SYMBOL(rdmsr_safe_on_cpu);
EXPORT_SYMBOL(wrmsr_safe_on_cpu);
