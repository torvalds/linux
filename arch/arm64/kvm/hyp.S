/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/linkage.h>

#include <asm/assembler.h>

/*
 * u64 kvm_call_hyp(void *hypfn, ...);
 *
 * This is not really a variadic function in the classic C-way and care must
 * be taken when calling this to ensure parameters are passed in registers
 * only, since the stack will change between the caller and the callee.
 *
 * Call the function with the first argument containing a pointer to the
 * function you wish to call in Hyp mode, and subsequent arguments will be
 * passed as x0, x1, and x2 (a maximum of 3 arguments in addition to the
 * function pointer can be passed).  The function being called must be mapped
 * in Hyp mode (see init_hyp_mode in arch/arm/kvm/arm.c).  Return values are
 * passed in x0.
 *
 * A function pointer with a value of 0 has a special meaning, and is
 * used to implement __hyp_get_vectors in the same way as in
 * arch/arm64/kernel/hyp_stub.S.
 */
ENTRY(kvm_call_hyp)
	hvc	#0
	ret
ENDPROC(kvm_call_hyp)
