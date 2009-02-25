/******************************************************************************
 * arch/ia64/xen/xen_pv_ops.c
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/console.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/pm.h>

#include <asm/xen/hypervisor.h>
#include <asm/xen/xencomm.h>
#include <asm/xen/privop.h>

#include "irq_xen.h"
#include "time.h"

/***************************************************************************
 * general info
 */
static struct pv_info xen_info __initdata = {
	.kernel_rpl = 2,	/* or 1: determin at runtime */
	.paravirt_enabled = 1,
	.name = "Xen/ia64",
};

#define IA64_RSC_PL_SHIFT	2
#define IA64_RSC_PL_BIT_SIZE	2
#define IA64_RSC_PL_MASK	\
	(((1UL << IA64_RSC_PL_BIT_SIZE) - 1) << IA64_RSC_PL_SHIFT)

static void __init
xen_info_init(void)
{
	/* Xenified Linux/ia64 may run on pl = 1 or 2.
	 * determin at run time. */
	unsigned long rsc = ia64_getreg(_IA64_REG_AR_RSC);
	unsigned int rpl = (rsc & IA64_RSC_PL_MASK) >> IA64_RSC_PL_SHIFT;
	xen_info.kernel_rpl = rpl;
}

/***************************************************************************
 * pv_init_ops
 * initialization hooks.
 */

static void
xen_panic_hypercall(struct unw_frame_info *info, void *arg)
{
	current->thread.ksp = (__u64)info->sw - 16;
	HYPERVISOR_shutdown(SHUTDOWN_crash);
	/* we're never actually going to get here... */
}

static int
xen_panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	unw_init_running(xen_panic_hypercall, NULL);
	/* we're never actually going to get here... */
	return NOTIFY_DONE;
}

static struct notifier_block xen_panic_block = {
	xen_panic_event, NULL, 0 /* try to go last */
};

static void xen_pm_power_off(void)
{
	local_irq_disable();
	HYPERVISOR_shutdown(SHUTDOWN_poweroff);
}

static void __init
xen_banner(void)
{
	printk(KERN_INFO
	       "Running on Xen! pl = %d start_info_pfn=0x%lx nr_pages=%ld "
	       "flags=0x%x\n",
	       xen_info.kernel_rpl,
	       HYPERVISOR_shared_info->arch.start_info_pfn,
	       xen_start_info->nr_pages, xen_start_info->flags);
}

static int __init
xen_reserve_memory(struct rsvd_region *region)
{
	region->start = (unsigned long)__va(
		(HYPERVISOR_shared_info->arch.start_info_pfn << PAGE_SHIFT));
	region->end   = region->start + PAGE_SIZE;
	return 1;
}

static void __init
xen_arch_setup_early(void)
{
	struct shared_info *s;
	BUG_ON(!xen_pv_domain());

	s = HYPERVISOR_shared_info;
	xen_start_info = __va(s->arch.start_info_pfn << PAGE_SHIFT);

	/* Must be done before any hypercall.  */
	xencomm_initialize();

	xen_setup_features();
	/* Register a call for panic conditions. */
	atomic_notifier_chain_register(&panic_notifier_list,
				       &xen_panic_block);
	pm_power_off = xen_pm_power_off;

	xen_ia64_enable_opt_feature();
}

static void __init
xen_arch_setup_console(char **cmdline_p)
{
	add_preferred_console("xenboot", 0, NULL);
	add_preferred_console("tty", 0, NULL);
	/* use hvc_xen */
	add_preferred_console("hvc", 0, NULL);

#if !defined(CONFIG_VT) || !defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = NULL;
#endif
}

static int __init
xen_arch_setup_nomca(void)
{
	return 1;
}

static void __init
xen_post_smp_prepare_boot_cpu(void)
{
	xen_setup_vcpu_info_placement();
}

static const struct pv_init_ops xen_init_ops __initconst = {
	.banner = xen_banner,

	.reserve_memory = xen_reserve_memory,

	.arch_setup_early = xen_arch_setup_early,
	.arch_setup_console = xen_arch_setup_console,
	.arch_setup_nomca = xen_arch_setup_nomca,

	.post_smp_prepare_boot_cpu = xen_post_smp_prepare_boot_cpu,
};

/***************************************************************************
 * pv_cpu_ops
 * intrinsics hooks.
 */

static void xen_setreg(int regnum, unsigned long val)
{
	switch (regnum) {
	case _IA64_REG_AR_KR0 ... _IA64_REG_AR_KR7:
		xen_set_kr(regnum - _IA64_REG_AR_KR0, val);
		break;
#ifdef CONFIG_IA32_SUPPORT
	case _IA64_REG_AR_EFLAG:
		xen_set_eflag(val);
		break;
#endif
	case _IA64_REG_CR_TPR:
		xen_set_tpr(val);
		break;
	case _IA64_REG_CR_ITM:
		xen_set_itm(val);
		break;
	case _IA64_REG_CR_EOI:
		xen_eoi(val);
		break;
	default:
		ia64_native_setreg_func(regnum, val);
		break;
	}
}

static unsigned long xen_getreg(int regnum)
{
	unsigned long res;

	switch (regnum) {
	case _IA64_REG_PSR:
		res = xen_get_psr();
		break;
#ifdef CONFIG_IA32_SUPPORT
	case _IA64_REG_AR_EFLAG:
		res = xen_get_eflag();
		break;
#endif
	case _IA64_REG_CR_IVR:
		res = xen_get_ivr();
		break;
	case _IA64_REG_CR_TPR:
		res = xen_get_tpr();
		break;
	default:
		res = ia64_native_getreg_func(regnum);
		break;
	}
	return res;
}

/* turning on interrupts is a bit more complicated.. write to the
 * memory-mapped virtual psr.i bit first (to avoid race condition),
 * then if any interrupts were pending, we have to execute a hyperprivop
 * to ensure the pending interrupt gets delivered; else we're done! */
static void
xen_ssm_i(void)
{
	int old = xen_get_virtual_psr_i();
	xen_set_virtual_psr_i(1);
	barrier();
	if (!old && xen_get_virtual_pend())
		xen_hyper_ssm_i();
}

/* turning off interrupts can be paravirtualized simply by writing
 * to a memory-mapped virtual psr.i bit (implemented as a 16-bit bool) */
static void
xen_rsm_i(void)
{
	xen_set_virtual_psr_i(0);
	barrier();
}

static unsigned long
xen_get_psr_i(void)
{
	return xen_get_virtual_psr_i() ? IA64_PSR_I : 0;
}

static void
xen_intrin_local_irq_restore(unsigned long mask)
{
	if (mask & IA64_PSR_I)
		xen_ssm_i();
	else
		xen_rsm_i();
}

static const struct pv_cpu_ops xen_cpu_ops __initdata = {
	.fc		= xen_fc,
	.thash		= xen_thash,
	.get_cpuid	= xen_get_cpuid,
	.get_pmd	= xen_get_pmd,
	.getreg		= xen_getreg,
	.setreg		= xen_setreg,
	.ptcga		= xen_ptcga,
	.get_rr		= xen_get_rr,
	.set_rr		= xen_set_rr,
	.set_rr0_to_rr4	= xen_set_rr0_to_rr4,
	.ssm_i		= xen_ssm_i,
	.rsm_i		= xen_rsm_i,
	.get_psr_i	= xen_get_psr_i,
	.intrin_local_irq_restore
			= xen_intrin_local_irq_restore,
};

/******************************************************************************
 * replacement of hand written assembly codes.
 */

extern char xen_switch_to;
extern char xen_leave_syscall;
extern char xen_work_processed_syscall;
extern char xen_leave_kernel;

const struct pv_cpu_asm_switch xen_cpu_asm_switch = {
	.switch_to		= (unsigned long)&xen_switch_to,
	.leave_syscall		= (unsigned long)&xen_leave_syscall,
	.work_processed_syscall	= (unsigned long)&xen_work_processed_syscall,
	.leave_kernel		= (unsigned long)&xen_leave_kernel,
};

/***************************************************************************
 * pv_iosapic_ops
 * iosapic read/write hooks.
 */
static void
xen_pcat_compat_init(void)
{
	/* nothing */
}

static struct irq_chip*
xen_iosapic_get_irq_chip(unsigned long trigger)
{
	return NULL;
}

static unsigned int
xen_iosapic_read(char __iomem *iosapic, unsigned int reg)
{
	struct physdev_apic apic_op;
	int ret;

	apic_op.apic_physbase = (unsigned long)iosapic -
					__IA64_UNCACHED_OFFSET;
	apic_op.reg = reg;
	ret = HYPERVISOR_physdev_op(PHYSDEVOP_apic_read, &apic_op);
	if (ret)
		return ret;
	return apic_op.value;
}

static void
xen_iosapic_write(char __iomem *iosapic, unsigned int reg, u32 val)
{
	struct physdev_apic apic_op;

	apic_op.apic_physbase = (unsigned long)iosapic -
					__IA64_UNCACHED_OFFSET;
	apic_op.reg = reg;
	apic_op.value = val;
	HYPERVISOR_physdev_op(PHYSDEVOP_apic_write, &apic_op);
}

static const struct pv_iosapic_ops xen_iosapic_ops __initconst = {
	.pcat_compat_init = xen_pcat_compat_init,
	.__get_irq_chip = xen_iosapic_get_irq_chip,

	.__read = xen_iosapic_read,
	.__write = xen_iosapic_write,
};

/***************************************************************************
 * pv_ops initialization
 */

void __init
xen_setup_pv_ops(void)
{
	xen_info_init();
	pv_info = xen_info;
	pv_init_ops = xen_init_ops;
	pv_cpu_ops = xen_cpu_ops;
	pv_iosapic_ops = xen_iosapic_ops;
	pv_irq_ops = xen_irq_ops;
	pv_time_ops = xen_time_ops;

	paravirt_cpu_asm_init(&xen_cpu_asm_switch);
}
