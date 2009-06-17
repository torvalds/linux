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

static inline void raw_local_irq_restore(unsigned long flags)
{
	if (!raw_irqs_disabled_flags(flags))
		raw_local_irq_enable();
}

static inline unsigned long __raw_local_irq_save(void)
{
	unsigned long flags = bfin_cli();
#ifdef CONFIG_DEBUG_HWERR
	bfin_sti(0x3f);
#endif
	return flags;
}
#define raw_local_irq_save(flags) do { (flags) = __raw_local_irq_save(); } while (0)

#endif
