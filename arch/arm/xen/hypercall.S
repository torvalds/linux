/******************************************************************************
 * hypercall.S
 *
 * Xen hypercall wrappers
 *
 * Stefano Stabellini <stefano.stabellini@eu.citrix.com>, Citrix, 2012
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * The Xen hypercall calling convention is very similar to the ARM
 * procedure calling convention: the first paramter is passed in r0, the
 * second in r1, the third in r2 and the fourth in r3. Considering that
 * Xen hypercalls have 5 arguments at most, the fifth paramter is passed
 * in r4, differently from the procedure calling convention of using the
 * stack for that case.
 *
 * The hypercall number is passed in r12.
 *
 * The return value is in r0.
 *
 * The hvc ISS is required to be 0xEA1, that is the Xen specific ARM
 * hypercall tag.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/opcodes-virt.h>
#include <xen/interface/xen.h>


#define XEN_IMM 0xEA1

#define HYPERCALL_SIMPLE(hypercall)		\
ENTRY(HYPERVISOR_##hypercall)			\
	mov r12, #__HYPERVISOR_##hypercall;	\
	__HVC(XEN_IMM);						\
	mov pc, lr;							\
ENDPROC(HYPERVISOR_##hypercall)

#define HYPERCALL0 HYPERCALL_SIMPLE
#define HYPERCALL1 HYPERCALL_SIMPLE
#define HYPERCALL2 HYPERCALL_SIMPLE
#define HYPERCALL3 HYPERCALL_SIMPLE
#define HYPERCALL4 HYPERCALL_SIMPLE

#define HYPERCALL5(hypercall)			\
ENTRY(HYPERVISOR_##hypercall)			\
	stmdb sp!, {r4}						\
	ldr r4, [sp, #4]					\
	mov r12, #__HYPERVISOR_##hypercall;	\
	__HVC(XEN_IMM);						\
	ldm sp!, {r4}						\
	mov pc, lr							\
ENDPROC(HYPERVISOR_##hypercall)

                .text

HYPERCALL2(xen_version);
HYPERCALL3(console_io);
HYPERCALL3(grant_table_op);
HYPERCALL2(sched_op);
HYPERCALL2(event_channel_op);
HYPERCALL2(hvm_op);
HYPERCALL2(memory_op);
HYPERCALL2(physdev_op);
HYPERCALL3(vcpu_op);
HYPERCALL1(tmem_op);

ENTRY(privcmd_call)
	stmdb sp!, {r4}
	mov r12, r0
	mov r0, r1
	mov r1, r2
	mov r2, r3
	ldr r3, [sp, #8]
	ldr r4, [sp, #4]
	__HVC(XEN_IMM)
	ldm sp!, {r4}
	mov pc, lr
ENDPROC(privcmd_call);
