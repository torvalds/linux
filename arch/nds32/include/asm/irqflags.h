// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <asm/nds32.h>
#include <nds32_intrinsic.h>

#define arch_local_irq_disable()	\
	GIE_DISABLE();

#define arch_local_irq_enable()	\
	GIE_ENABLE();
static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;
	flags = __nds32__mfsr(NDS32_SR_PSW) & PSW_mskGIE;
	GIE_DISABLE();
	return flags;
}

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;
	flags = __nds32__mfsr(NDS32_SR_PSW) & PSW_mskGIE;
	return flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	if(flags)
		GIE_ENABLE();
}

static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return !flags;
}
