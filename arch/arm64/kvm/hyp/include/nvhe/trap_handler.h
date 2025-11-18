/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Trap handler helpers.
 *
 * Copyright (C) 2020 - Google LLC
 * Author: Marc Zyngier <maz@kernel.org>
 */

#ifndef __ARM64_KVM_NVHE_TRAP_HANDLER_H__
#define __ARM64_KVM_NVHE_TRAP_HANDLER_H__

#include <asm/kvm_host.h>

#define cpu_reg(ctxt, r)	(ctxt)->regs.regs[r]
#define DECLARE_REG(type, name, ctxt, reg)					\
				__always_unused int ___check_reg_ ## reg;	\
				type name = (type)cpu_reg(ctxt, (reg))

#endif /* __ARM64_KVM_NVHE_TRAP_HANDLER_H__ */
