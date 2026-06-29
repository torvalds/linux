// SPDX-License-Identifier: GPL-2.0
/*
 * arm64 SDEI-based cross-CPU NMI service.
 *
 * Delivering an "NMI-shaped" event to an EL1 context that has locally
 * masked interrupts, on silicon without FEAT_NMI, can be done two ways:
 *
 *   - pseudo-NMI: mask "interrupts" via the GIC priority register
 *     (ICC_PMR_EL1) instead of PSTATE.DAIF, leaving a high-priority band
 *     deliverable. Functionally this works -- but it reimplements every
 *     local_irq_disable()/enable() and exception entry/exit as a PMR
 *     write plus synchronisation, a cost paid on that hot path forever,
 *     whether or not an NMI is ever delivered.
 *
 *   - SDEI: leave interrupt masking as the cheap PSTATE.DAIF operation
 *     and have the firmware bounce an EL3-routed Group-0 SGI back to
 *     NS-EL1 as an event callback. The cost is a firmware round-trip,
 *     but only at the rare moment delivery is actually needed.
 *
 * This driver takes the second path: it keeps the IRQ-mask hot path
 * free and pays only when it fires, which is what makes cross-CPU NMI
 * affordable on hardware where the pseudo-NMI tax isn't, until FEAT_NMI
 * makes NMI masking cheap in the architecture itself.
 *
 * Capabilities provided:
 *
 *   - sdei_nmi_trigger_cpumask_backtrace() — override for arm64's
 *     arch_trigger_cpumask_backtrace(), so sysrq-l, RCU stall dumps,
 *     hardlockup_all_cpu_backtrace, soft-lockup/hung-task secondary
 *     dumps all reach interrupt-masked CPUs.
 *
 * Delivery uses the standard SDEI software-signalled event (event 0) and
 * SDEI_EVENT_SIGNAL. We register a handler for event 0, enable it, and
 * poke a target CPU with sdei_event_signal(0, mpidr): firmware makes
 * event 0 pending on that PE and dispatches the handler NMI-like,
 * regardless of the target's DAIF.
 * Availability is simply whether event 0 registers and enables -- if SDEI
 * and its software-signalled event are present we use it, otherwise the
 * driver stays inert.
 */

#define pr_fmt(fmt) "sdei_nmi: " fmt

#include <linux/arm_sdei.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/nmi.h>
#include <linux/printk.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <asm/nmi.h>
#include <asm/smp_plat.h>

static bool sdei_nmi_available;

#define SDEI_NMI_EVENT			0

static int sdei_nmi_handler(u32 event, struct pt_regs *regs, void *arg)
{
	/*
	 * nmi_cpu_backtrace() no-ops unless this CPU's bit is set in the
	 * global backtrace mask (driven by nmi_trigger_cpumask_backtrace()),
	 * so a fire that reaches a CPU not being backtraced is harmless.
	 */
	nmi_cpu_backtrace(regs);
	return SDEI_EV_HANDLED;
}
NOKPROBE_SYMBOL(sdei_nmi_handler);

static void sdei_nmi_fire(unsigned int target_cpu)
{
	int err = sdei_event_signal(SDEI_NMI_EVENT, cpu_logical_map(target_cpu));

	if (err)
		pr_warn("SDEI_EVENT_SIGNAL to CPU %u failed: %d\n",
			target_cpu, err);
}

/*
 * Raise callback for nmi_trigger_cpumask_backtrace(): signal event 0
 * at every CPU still pending in @mask. The framework excludes the local
 * CPU from @mask before calling us.
 */
static void sdei_nmi_raise_backtrace(cpumask_t *mask)
{
	unsigned int cpu;

	/*
	 * Publish backtrace_mask (set by nmi_trigger_cpumask_backtrace())
	 * before signalling. As in the stop path, the SMC is not a memory
	 * store, so dsb(ishst) is needed for the target to observe the mask.
	 */
	dsb(ishst);

	for_each_cpu(cpu, mask)
		sdei_nmi_fire(cpu);
}

/*
 * Override hook for arch_trigger_cpumask_backtrace() (see
 * arch/arm64/kernel/smp.c). Returns true when SDEI handled the request,
 * which is the case whenever SDEI is active; on a false return the arch
 * falls back to its regular-IRQ (or pseudo-NMI, if enabled) IPI.
 *
 * On a kernel built without paying the pseudo-NMI hot-path cost (the
 * usual case for this driver's target), the IPI can't reach a CPU that
 * has interrupts masked -- so the backtrace of the one CPU you care
 * about comes back empty. SDEI is dispatched out of EL3 and lands
 * regardless of the target's DAIF, without taxing the IRQ-mask path.
 */
bool sdei_nmi_trigger_cpumask_backtrace(const cpumask_t *mask, int exclude_cpu)
{
	if (!sdei_nmi_available)
		return false;

	nmi_trigger_cpumask_backtrace(mask, exclude_cpu,
				      sdei_nmi_raise_backtrace);
	return true;
}

/*
 * device_initcall (after arch_initcall(sdei_init), so the SDEI subsystem
 * is up): probe the firmware, register the event, and turn on the
 * cross-CPU service. If the probe fails the driver stays inert and the
 * override hooks decline, leaving the arch's own paths in place.
 */
static int __init sdei_nmi_init(void)
{
	int err;

	if (!sdei_is_present())
		return 0;

	err = sdei_event_register(SDEI_NMI_EVENT, sdei_nmi_handler, NULL);
	if (err) {
		pr_err("sdei_event_register(%u) failed: %d\n",
		       SDEI_NMI_EVENT, err);
		return 0;
	}

	err = sdei_event_enable(SDEI_NMI_EVENT);
	if (err) {
		pr_err("sdei_event_enable(%u) failed: %d\n",
		       SDEI_NMI_EVENT, err);
		sdei_event_unregister(SDEI_NMI_EVENT);
		return 0;
	}

	sdei_nmi_available = true;
	pr_info("using SDEI cross-CPU NMI (SDEI_EVENT_SIGNAL, event %u)\n",
		SDEI_NMI_EVENT);

	return 0;
}
device_initcall(sdei_nmi_init);
