/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/exception.h
 *
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_EXCEPTION_H
#define __ASM_EXCEPTION_H

#include <asm/esr.h>

#include <linux/interrupt.h>

#define __exception	__attribute__((section(".exception.text")))
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
#define __exception_irq_entry	__irq_entry
#else
#define __exception_irq_entry	__exception
#endif

static inline u32 disr_to_esr(u64 disr)
{
	unsigned int esr = ESR_ELx_EC_SERROR << ESR_ELx_EC_SHIFT;

	if ((disr & DISR_EL1_IDS) == 0)
		esr |= (disr & DISR_EL1_ESR_MASK);
	else
		esr |= (disr & ESR_ELx_ISS_MASK);

	return esr;
}

#endif	/* __ASM_EXCEPTION_H */
