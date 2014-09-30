#include <linux/percpu.h>
#include <linux/jump_label.h>
#include <asm/trace.h>

#ifdef CONFIG_JUMP_LABEL
struct static_key opal_tracepoint_key = STATIC_KEY_INIT;

void opal_tracepoint_regfunc(void)
{
	static_key_slow_inc(&opal_tracepoint_key);
}

void opal_tracepoint_unregfunc(void)
{
	static_key_slow_dec(&opal_tracepoint_key);
}
#else
/*
 * We optimise OPAL calls by placing opal_tracepoint_refcount
 * directly in the TOC so we can check if the opal tracepoints are
 * enabled via a single load.
 */

/* NB: reg/unreg are called while guarded with the tracepoints_mutex */
extern long opal_tracepoint_refcount;

void opal_tracepoint_regfunc(void)
{
	opal_tracepoint_refcount++;
}

void opal_tracepoint_unregfunc(void)
{
	opal_tracepoint_refcount--;
}
#endif

/*
 * Since the tracing code might execute OPAL calls we need to guard against
 * recursion.
 */
static DEFINE_PER_CPU(unsigned int, opal_trace_depth);

void __trace_opal_entry(unsigned long opcode, unsigned long *args)
{
	unsigned long flags;
	unsigned int *depth;

	local_irq_save(flags);

	depth = &__get_cpu_var(opal_trace_depth);

	if (*depth)
		goto out;

	(*depth)++;
	preempt_disable();
	trace_opal_entry(opcode, args);
	(*depth)--;

out:
	local_irq_restore(flags);
}

void __trace_opal_exit(long opcode, unsigned long retval)
{
	unsigned long flags;
	unsigned int *depth;

	local_irq_save(flags);

	depth = &__get_cpu_var(opal_trace_depth);

	if (*depth)
		goto out;

	(*depth)++;
	trace_opal_exit(opcode, retval);
	preempt_enable();
	(*depth)--;

out:
	local_irq_restore(flags);
}
