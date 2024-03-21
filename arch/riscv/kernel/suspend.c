// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 * Copyright (c) 2022 Ventana Micro Systems Inc.
 */

#define pr_fmt(fmt) "suspend: " fmt

#include <linux/ftrace.h>
#include <linux/suspend.h>
#include <asm/csr.h>
#include <asm/sbi.h>
#include <asm/suspend.h>

void suspend_save_csrs(struct suspend_context *context)
{
	context->scratch = csr_read(CSR_SCRATCH);
	if (riscv_cpu_has_extension_unlikely(smp_processor_id(), RISCV_ISA_EXT_XLINUXENVCFG))
		context->envcfg = csr_read(CSR_ENVCFG);
	context->tvec = csr_read(CSR_TVEC);
	context->ie = csr_read(CSR_IE);

	/*
	 * No need to save/restore IP CSR (i.e. MIP or SIP) because:
	 *
	 * 1. For no-MMU (M-mode) kernel, the bits in MIP are set by
	 *    external devices (such as interrupt controller, timer, etc).
	 * 2. For MMU (S-mode) kernel, the bits in SIP are set by
	 *    M-mode firmware and external devices (such as interrupt
	 *    controller, etc).
	 */

#ifdef CONFIG_MMU
	context->satp = csr_read(CSR_SATP);
#endif
}

void suspend_restore_csrs(struct suspend_context *context)
{
	csr_write(CSR_SCRATCH, context->scratch);
	if (riscv_cpu_has_extension_unlikely(smp_processor_id(), RISCV_ISA_EXT_XLINUXENVCFG))
		csr_write(CSR_ENVCFG, context->envcfg);
	csr_write(CSR_TVEC, context->tvec);
	csr_write(CSR_IE, context->ie);

#ifdef CONFIG_MMU
	csr_write(CSR_SATP, context->satp);
#endif
}

int cpu_suspend(unsigned long arg,
		int (*finish)(unsigned long arg,
			      unsigned long entry,
			      unsigned long context))
{
	int rc = 0;
	struct suspend_context context = { 0 };

	/* Finisher should be non-NULL */
	if (!finish)
		return -EINVAL;

	/* Save additional CSRs*/
	suspend_save_csrs(&context);

	/*
	 * Function graph tracer state gets incosistent when the kernel
	 * calls functions that never return (aka finishers) hence disable
	 * graph tracing during their execution.
	 */
	pause_graph_tracing();

	/* Save context on stack */
	if (__cpu_suspend_enter(&context)) {
		/* Call the finisher */
		rc = finish(arg, __pa_symbol(__cpu_resume_enter),
			    (ulong)&context);

		/*
		 * Should never reach here, unless the suspend finisher
		 * fails. Successful cpu_suspend() should return from
		 * __cpu_resume_entry()
		 */
		if (!rc)
			rc = -EOPNOTSUPP;
	}

	/* Enable function graph tracer */
	unpause_graph_tracing();

	/* Restore additional CSRs */
	suspend_restore_csrs(&context);

	return rc;
}

#ifdef CONFIG_RISCV_SBI
static int sbi_system_suspend(unsigned long sleep_type,
			      unsigned long resume_addr,
			      unsigned long opaque)
{
	struct sbiret ret;

	ret = sbi_ecall(SBI_EXT_SUSP, SBI_EXT_SUSP_SYSTEM_SUSPEND,
			sleep_type, resume_addr, opaque, 0, 0, 0);
	if (ret.error)
		return sbi_err_map_linux_errno(ret.error);

	return ret.value;
}

static int sbi_system_suspend_enter(suspend_state_t state)
{
	return cpu_suspend(SBI_SUSP_SLEEP_TYPE_SUSPEND_TO_RAM, sbi_system_suspend);
}

static const struct platform_suspend_ops sbi_system_suspend_ops = {
	.valid = suspend_valid_only_mem,
	.enter = sbi_system_suspend_enter,
};

static int __init sbi_system_suspend_init(void)
{
	if (sbi_spec_version >= sbi_mk_version(2, 0) &&
	    sbi_probe_extension(SBI_EXT_SUSP) > 0) {
		pr_info("SBI SUSP extension detected\n");
		if (IS_ENABLED(CONFIG_SUSPEND))
			suspend_set_ops(&sbi_system_suspend_ops);
	}

	return 0;
}

arch_initcall(sbi_system_suspend_init);
#endif /* CONFIG_RISCV_SBI */
