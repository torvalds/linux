/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/linkage.h>

#include <asm/alternative.h>
#include <asm/assembler.h>
#include <asm/cpufeature.h>

/*
 * u64 __kvm_call_hyp_init(phys_addr_t pgd_ptr,
 * 			   unsigned long hyp_stack_ptr,
 * 			   unsigned long vector_ptr,
 * 			   unsigned long tpidr_el2);
 */
SYM_FUNC_START(__kvm_call_hyp_init)
	hvc	#0
	ret
SYM_FUNC_END(__kvm_call_hyp_init)
