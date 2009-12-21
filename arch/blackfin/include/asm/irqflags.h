/*
 * interface to Blackfin CEC
 *
 * Copyright 2009 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_BFIN_IRQFLAGS_H__
#define __ASM_BFIN_IRQFLAGS_H__

#ifdef CONFIG_SMP
# include <asm/pda.h>
# include <asm/processor.h>
/* Forward decl needed due to cdef inter dependencies */
static inline uint32_t __pure bfin_dspid(void);
# define blackfin_core_id() (bfin_dspid() & 0xff)
# define bfin_irq_flags cpu_pda[blackfin_core_id()].imask
#else
extern unsigned long bfin_irq_flags;
#endif

static inline void bfin_sti(unsigned long flags)
{
	asm volatile("sti %0;" : : "d" (flags));
}

static inline unsigned long bfin_cli(void)
{
	unsigned long flags;
	asm volatile("cli %0;" : "=d" (flags));
	return flags;
}

#ifdef CONFIG_IPIPE

#include <linux/compiler.h>
#include <linux/ipipe_base.h>
#include <linux/ipipe_trace.h>

#ifdef CONFIG_DEBUG_HWERR
# define bfin_no_irqs 0x3f
#else
# define bfin_no_irqs 0x1f
#endif

#define raw_local_irq_disable()				\
	do {						\
		ipipe_check_context(ipipe_root_domain);	\
		__ipipe_stall_root();			\
		barrier();				\
	} while (0)

#define raw_local_irq_enable()				\
	do {						\
		barrier();				\
		ipipe_check_context(ipipe_root_domain);	\
		__ipipe_unstall_root();			\
	} while (0)

#define raw_local_save_flags_ptr(x)					\
	do {								\
		*(x) = __ipipe_test_root() ? bfin_no_irqs : bfin_irq_flags; \
	} while (0)

#define raw_local_save_flags(x)		raw_local_save_flags_ptr(&(x))

#define raw_irqs_disabled_flags(x)	((x) == bfin_no_irqs)

#define raw_local_irq_save_ptr(x)					\
	do {								\
		*(x) = __ipipe_test_and_stall_root() ? bfin_no_irqs : bfin_irq_flags; \
		barrier();						\
	} while (0)

#define raw_local_irq_save(x)				\
	do {						\
		ipipe_check_context(ipipe_root_domain);	\
		raw_local_irq_save_ptr(&(x));		\
	} while (0)

static inline unsigned long raw_mangle_irq_bits(int virt, unsigned long real)
{
	/*
	 * Merge virtual and real interrupt mask bits into a single
	 * 32bit word.
	 */
	return (real & ~(1 << 31)) | ((virt != 0) << 31);
}

static inline int raw_demangle_irq_bits(unsigned long *x)
{
	int virt = (*x & (1 << 31)) != 0;
	*x &= ~(1L << 31);
	return virt;
}

static inline void local_irq_disable_hw_notrace(void)
{
	bfin_cli();
}

static inline void local_irq_enable_hw_notrace(void)
{
	bfin_sti(bfin_irq_flags);
}

#define local_save_flags_hw(flags)			\
	do {						\
		(flags) = bfin_read_IMASK();		\
	} while (0)

#define irqs_disabled_flags_hw(flags) (((flags) & ~0x3f) == 0)

#define irqs_disabled_hw()			\
	({					\
	unsigned long flags;			\
	local_save_flags_hw(flags);		\
	irqs_disabled_flags_hw(flags);		\
	})

static inline void local_irq_save_ptr_hw(unsigned long *flags)
{
	*flags = bfin_cli();
#ifdef CONFIG_DEBUG_HWERR
	bfin_sti(0x3f);
#endif
}

#define local_irq_save_hw_notrace(flags)		\
	do {						\
		local_irq_save_ptr_hw(&(flags));	\
	} while (0)

static inline void local_irq_restore_hw_notrace(unsigned long flags)
{
	if (!irqs_disabled_flags_hw(flags))
		local_irq_enable_hw_notrace();
}

#ifdef CONFIG_IPIPE_TRACE_IRQSOFF
# define local_irq_disable_hw()				\
	do {						\
		if (!irqs_disabled_hw()) {		\
			local_irq_disable_hw_notrace();	\
			ipipe_trace_begin(0x80000000);	\
		}					\
	} while (0)
# define local_irq_enable_hw()				\
	do {						\
		if (irqs_disabled_hw()) {		\
			ipipe_trace_end(0x80000000);	\
			local_irq_enable_hw_notrace();	\
		}					\
	} while (0)
# define local_irq_save_hw(flags)			\
	do {						\
		local_save_flags_hw(flags);		\
		if (!irqs_disabled_flags_hw(flags)) {	\
			local_irq_disable_hw_notrace();	\
			ipipe_trace_begin(0x80000001);	\
		}					\
	} while (0)
# define local_irq_restore_hw(flags)			\
	do {						\
		if (!irqs_disabled_flags_hw(flags)) {	\
			ipipe_trace_end(0x80000001);	\
			local_irq_enable_hw_notrace();	\
		}					\
	} while (0)
#else /* !CONFIG_IPIPE_TRACE_IRQSOFF */
# define local_irq_disable_hw()		local_irq_disable_hw_notrace()
# define local_irq_enable_hw()		local_irq_enable_hw_notrace()
# define local_irq_save_hw(flags)	local_irq_save_hw_notrace(flags)
# define local_irq_restore_hw(flags)	local_irq_restore_hw_notrace(flags)
#endif /* !CONFIG_IPIPE_TRACE_IRQSOFF */

#else /* CONFIG_IPIPE */

static inline void raw_local_irq_disable(void)
{
	bfin_cli();
}
static inline void raw_local_irq_enable(void)
{
	bfin_sti(bfin_irq_flags);
}

#define raw_local_save_flags(flags) do { (flags) = bfin_read_IMASK(); } while (0)

#define raw_irqs_disabled_flags(flags) (((flags) & ~0x3f) == 0)

static inline unsigned long __raw_local_irq_save(void)
{
	unsigned long flags = bfin_cli();
#ifdef CONFIG_DEBUG_HWERR
	bfin_sti(0x3f);
#endif
	return flags;
}
#define raw_local_irq_save(flags) do { (flags) = __raw_local_irq_save(); } while (0)

#define local_irq_save_hw(flags)	raw_local_irq_save(flags)
#define local_irq_restore_hw(flags)	raw_local_irq_restore(flags)
#define local_irq_enable_hw()		raw_local_irq_enable()
#define local_irq_disable_hw()		raw_local_irq_disable()
#define irqs_disabled_hw()		irqs_disabled()

#endif /* !CONFIG_IPIPE */

static inline void raw_local_irq_restore(unsigned long flags)
{
	if (!raw_irqs_disabled_flags(flags))
		raw_local_irq_enable();
}

#endif
