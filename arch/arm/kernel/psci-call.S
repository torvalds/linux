/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2015 ARM Limited
 *
 * Author: Mark Rutland <mark.rutland@arm.com>
 */

#include <linux/linkage.h>

#include <asm/opcodes-sec.h>
#include <asm/opcodes-virt.h>

/* int __invoke_psci_fn_hvc(u32 function_id, u32 arg0, u32 arg1, u32 arg2) */
ENTRY(__invoke_psci_fn_hvc)
	__HVC(0)
	bx	lr
ENDPROC(__invoke_psci_fn_hvc)

/* int __invoke_psci_fn_smc(u32 function_id, u32 arg0, u32 arg1, u32 arg2) */
ENTRY(__invoke_psci_fn_smc)
	__SMC(0)
	bx	lr
ENDPROC(__invoke_psci_fn_smc)
