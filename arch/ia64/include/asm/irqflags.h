/*
 * IRQ flags defines.
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Asit Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 */

#ifndef _ASM_IA64_IRQFLAGS_H
#define _ASM_IA64_IRQFLAGS_H

#ifdef CONFIG_IA64_DEBUG_IRQ
extern unsigned long last_cli_ip;
static inline void arch_maybe_save_ip(unsigned long flags)
{
	if (flags & IA64_PSR_I)
		last_cli_ip = ia64_getreg(_IA64_REG_IP);
}
#else
#define arch_maybe_save_ip(flags) do {} while (0)
#endif

/*
 * - clearing psr.i is implicitly serialized (visible by next insn)
 * - setting psr.i requires data serialization
 * - we need a stop-bit before reading PSR because we sometimes
 *   write a floating-point register right before reading the PSR
 *   and that writes to PSR.mfl
 */

static inline unsigned long arch_local_save_flags(void)
{
	ia64_stop();
#ifdef CONFIG_PARAVIRT
	return ia64_get_psr_i();
#else
	return ia64_getreg(_IA64_REG_PSR);
#endif
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();

	ia64_stop();
	ia64_rsm(IA64_PSR_I);
	arch_maybe_save_ip(flags);
	return flags;
}

static inline void arch_local_irq_disable(void)
{
#ifdef CONFIG_IA64_DEBUG_IRQ
	arch_local_irq_save();
#else
	ia64_stop();
	ia64_rsm(IA64_PSR_I);
#endif
}

static inline void arch_local_irq_enable(void)
{
	ia64_stop();
	ia64_ssm(IA64_PSR_I);
	ia64_srlz_d();
}

static inline void arch_local_irq_restore(unsigned long flags)
{
#ifdef CONFIG_IA64_DEBUG_IRQ
	unsigned long old_psr = arch_local_save_flags();
#endif
	ia64_intrin_local_irq_restore(flags & IA64_PSR_I);
	arch_maybe_save_ip(old_psr & ~flags);
}

static inline bool arch_irqs_disabled_flags(unsigned long flags)
{
	return (flags & IA64_PSR_I) == 0;
}

static inline bool arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}

static inline void arch_safe_halt(void)
{
	ia64_pal_halt_light();	/* PAL_HALT_LIGHT */
}


#endif /* _ASM_IA64_IRQFLAGS_H */
