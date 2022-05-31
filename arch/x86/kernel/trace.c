#include <asm/trace/irq_vectors.h>
#include <linux/trace.h>

#if defined(CONFIG_OSNOISE_TRACER) && defined(CONFIG_X86_LOCAL_APIC)
/*
 * trace_intel_irq_entry - record intel specific IRQ entry
 */
static void trace_intel_irq_entry(void *data, int vector)
{
	osnoise_trace_irq_entry(vector);
}

/*
 * trace_intel_irq_exit - record intel specific IRQ exit
 */
static void trace_intel_irq_exit(void *data, int vector)
{
	char *vector_desc = (char *) data;

	osnoise_trace_irq_exit(vector, vector_desc);
}

/*
 * register_intel_irq_tp - Register intel specific IRQ entry tracepoints
 */
int osnoise_arch_register(void)
{
	int ret;

	ret = register_trace_local_timer_entry(trace_intel_irq_entry, NULL);
	if (ret)
		goto out_err;

	ret = register_trace_local_timer_exit(trace_intel_irq_exit, "local_timer");
	if (ret)
		goto out_timer_entry;

#ifdef CONFIG_X86_THERMAL_VECTOR
	ret = register_trace_thermal_apic_entry(trace_intel_irq_entry, NULL);
	if (ret)
		goto out_timer_exit;

	ret = register_trace_thermal_apic_exit(trace_intel_irq_exit, "thermal_apic");
	if (ret)
		goto out_thermal_entry;
#endif /* CONFIG_X86_THERMAL_VECTOR */

#ifdef CONFIG_X86_MCE_AMD
	ret = register_trace_deferred_error_apic_entry(trace_intel_irq_entry, NULL);
	if (ret)
		goto out_thermal_exit;

	ret = register_trace_deferred_error_apic_exit(trace_intel_irq_exit, "deferred_error");
	if (ret)
		goto out_deferred_entry;
#endif

#ifdef CONFIG_X86_MCE_THRESHOLD
	ret = register_trace_threshold_apic_entry(trace_intel_irq_entry, NULL);
	if (ret)
		goto out_deferred_exit;

	ret = register_trace_threshold_apic_exit(trace_intel_irq_exit, "threshold_apic");
	if (ret)
		goto out_threshold_entry;
#endif /* CONFIG_X86_MCE_THRESHOLD */

#ifdef CONFIG_SMP
	ret = register_trace_call_function_single_entry(trace_intel_irq_entry, NULL);
	if (ret)
		goto out_threshold_exit;

	ret = register_trace_call_function_single_exit(trace_intel_irq_exit,
						       "call_function_single");
	if (ret)
		goto out_call_function_single_entry;

	ret = register_trace_call_function_entry(trace_intel_irq_entry, NULL);
	if (ret)
		goto out_call_function_single_exit;

	ret = register_trace_call_function_exit(trace_intel_irq_exit, "call_function");
	if (ret)
		goto out_call_function_entry;

	ret = register_trace_reschedule_entry(trace_intel_irq_entry, NULL);
	if (ret)
		goto out_call_function_exit;

	ret = register_trace_reschedule_exit(trace_intel_irq_exit, "reschedule");
	if (ret)
		goto out_reschedule_entry;
#endif /* CONFIG_SMP */

#ifdef CONFIG_IRQ_WORK
	ret = register_trace_irq_work_entry(trace_intel_irq_entry, NULL);
	if (ret)
		goto out_reschedule_exit;

	ret = register_trace_irq_work_exit(trace_intel_irq_exit, "irq_work");
	if (ret)
		goto out_irq_work_entry;
#endif

	ret = register_trace_x86_platform_ipi_entry(trace_intel_irq_entry, NULL);
	if (ret)
		goto out_irq_work_exit;

	ret = register_trace_x86_platform_ipi_exit(trace_intel_irq_exit, "x86_platform_ipi");
	if (ret)
		goto out_x86_ipi_entry;

	ret = register_trace_error_apic_entry(trace_intel_irq_entry, NULL);
	if (ret)
		goto out_x86_ipi_exit;

	ret = register_trace_error_apic_exit(trace_intel_irq_exit, "error_apic");
	if (ret)
		goto out_error_apic_entry;

	ret = register_trace_spurious_apic_entry(trace_intel_irq_entry, NULL);
	if (ret)
		goto out_error_apic_exit;

	ret = register_trace_spurious_apic_exit(trace_intel_irq_exit, "spurious_apic");
	if (ret)
		goto out_spurious_apic_entry;

	return 0;

out_spurious_apic_entry:
	unregister_trace_spurious_apic_entry(trace_intel_irq_entry, NULL);
out_error_apic_exit:
	unregister_trace_error_apic_exit(trace_intel_irq_exit, "error_apic");
out_error_apic_entry:
	unregister_trace_error_apic_entry(trace_intel_irq_entry, NULL);
out_x86_ipi_exit:
	unregister_trace_x86_platform_ipi_exit(trace_intel_irq_exit, "x86_platform_ipi");
out_x86_ipi_entry:
	unregister_trace_x86_platform_ipi_entry(trace_intel_irq_entry, NULL);
out_irq_work_exit:

#ifdef CONFIG_IRQ_WORK
	unregister_trace_irq_work_exit(trace_intel_irq_exit, "irq_work");
out_irq_work_entry:
	unregister_trace_irq_work_entry(trace_intel_irq_entry, NULL);
out_reschedule_exit:
#endif

#ifdef CONFIG_SMP
	unregister_trace_reschedule_exit(trace_intel_irq_exit, "reschedule");
out_reschedule_entry:
	unregister_trace_reschedule_entry(trace_intel_irq_entry, NULL);
out_call_function_exit:
	unregister_trace_call_function_exit(trace_intel_irq_exit, "call_function");
out_call_function_entry:
	unregister_trace_call_function_entry(trace_intel_irq_entry, NULL);
out_call_function_single_exit:
	unregister_trace_call_function_single_exit(trace_intel_irq_exit, "call_function_single");
out_call_function_single_entry:
	unregister_trace_call_function_single_entry(trace_intel_irq_entry, NULL);
out_threshold_exit:
#endif

#ifdef CONFIG_X86_MCE_THRESHOLD
	unregister_trace_threshold_apic_exit(trace_intel_irq_exit, "threshold_apic");
out_threshold_entry:
	unregister_trace_threshold_apic_entry(trace_intel_irq_entry, NULL);
out_deferred_exit:
#endif

#ifdef CONFIG_X86_MCE_AMD
	unregister_trace_deferred_error_apic_exit(trace_intel_irq_exit, "deferred_error");
out_deferred_entry:
	unregister_trace_deferred_error_apic_entry(trace_intel_irq_entry, NULL);
out_thermal_exit:
#endif /* CONFIG_X86_MCE_AMD */

#ifdef CONFIG_X86_THERMAL_VECTOR
	unregister_trace_thermal_apic_exit(trace_intel_irq_exit, "thermal_apic");
out_thermal_entry:
	unregister_trace_thermal_apic_entry(trace_intel_irq_entry, NULL);
out_timer_exit:
#endif /* CONFIG_X86_THERMAL_VECTOR */

	unregister_trace_local_timer_exit(trace_intel_irq_exit, "local_timer");
out_timer_entry:
	unregister_trace_local_timer_entry(trace_intel_irq_entry, NULL);
out_err:
	return -EINVAL;
}

void osnoise_arch_unregister(void)
{
	unregister_trace_spurious_apic_exit(trace_intel_irq_exit, "spurious_apic");
	unregister_trace_spurious_apic_entry(trace_intel_irq_entry, NULL);
	unregister_trace_error_apic_exit(trace_intel_irq_exit, "error_apic");
	unregister_trace_error_apic_entry(trace_intel_irq_entry, NULL);
	unregister_trace_x86_platform_ipi_exit(trace_intel_irq_exit, "x86_platform_ipi");
	unregister_trace_x86_platform_ipi_entry(trace_intel_irq_entry, NULL);

#ifdef CONFIG_IRQ_WORK
	unregister_trace_irq_work_exit(trace_intel_irq_exit, "irq_work");
	unregister_trace_irq_work_entry(trace_intel_irq_entry, NULL);
#endif

#ifdef CONFIG_SMP
	unregister_trace_reschedule_exit(trace_intel_irq_exit, "reschedule");
	unregister_trace_reschedule_entry(trace_intel_irq_entry, NULL);
	unregister_trace_call_function_exit(trace_intel_irq_exit, "call_function");
	unregister_trace_call_function_entry(trace_intel_irq_entry, NULL);
	unregister_trace_call_function_single_exit(trace_intel_irq_exit, "call_function_single");
	unregister_trace_call_function_single_entry(trace_intel_irq_entry, NULL);
#endif

#ifdef CONFIG_X86_MCE_THRESHOLD
	unregister_trace_threshold_apic_exit(trace_intel_irq_exit, "threshold_apic");
	unregister_trace_threshold_apic_entry(trace_intel_irq_entry, NULL);
#endif

#ifdef CONFIG_X86_MCE_AMD
	unregister_trace_deferred_error_apic_exit(trace_intel_irq_exit, "deferred_error");
	unregister_trace_deferred_error_apic_entry(trace_intel_irq_entry, NULL);
#endif

#ifdef CONFIG_X86_THERMAL_VECTOR
	unregister_trace_thermal_apic_exit(trace_intel_irq_exit, "thermal_apic");
	unregister_trace_thermal_apic_entry(trace_intel_irq_entry, NULL);
#endif /* CONFIG_X86_THERMAL_VECTOR */

	unregister_trace_local_timer_exit(trace_intel_irq_exit, "local_timer");
	unregister_trace_local_timer_entry(trace_intel_irq_entry, NULL);
}
#endif /* CONFIG_OSNOISE_TRACER && CONFIG_X86_LOCAL_APIC */
