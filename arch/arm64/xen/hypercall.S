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
 * The Xen hypercall calling convention is very similar to the procedure
 * call standard for the ARM 64-bit architecture: the first parameter is
 * passed in x0, the second in x1, the third in x2, the fourth in x3 and
 * the fifth in x4.
 *
 * The hypercall number is passed in x16.
 *
 * The return value is in x0.
 *
 * The hvc ISS is required to be 0xEA1, that is the Xen specific ARM
 * hypercall tag.
 *
 * Parameter structs passed to hypercalls are laid out according to
 * the ARM 64-bit EABI standard.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/uaccess.h>
#include <xen/interface/xen.h>


#define XEN_IMM 0xEA1

#define HYPERCALL_SIMPLE(hypercall)		\
ENTRY(HYPERVISOR_##hypercall)			\
	mov x16, #__HYPERVISOR_##hypercall;	\
	hvc XEN_IMM;				\
	ret;					\
ENDPROC(HYPERVISOR_##hypercall)

#define HYPERCALL0 HYPERCALL_SIMPLE
#define HYPERCALL1 HYPERCALL_SIMPLE
#define HYPERCALL2 HYPERCALL_SIMPLE
#define HYPERCALL3 HYPERCALL_SIMPLE
#define HYPERCALL4 HYPERCALL_SIMPLE
#define HYPERCALL5 HYPERCALL_SIMPLE

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
HYPERCALL2(multicall);

ENTRY(privcmd_call)
	mov x16, x0
	mov x0, x1
	mov x1, x2
	mov x2, x3
	mov x3, x4
	mov x4, x5
	/*
	 * Privcmd calls are issued by the userspace. The kernel needs to
	 * enable access to TTBR0_EL1 as the hypervisor would issue stage 1
	 * translations to user memory via AT instructions. Since AT
	 * instructions are not affected by the PAN bit (ARMv8.1), we only
	 * need the explicit uaccess_enable/disable if the TTBR0 PAN emulation
	 * is enabled (it implies that hardware UAO and PAN disabled).
	 */
	uaccess_ttbr0_enable x6, x7, x8
	hvc XEN_IMM

	/*
	 * Disable userspace access from kernel once the hyp call completed.
	 */
	uaccess_ttbr0_disable x6, x7
	ret
ENDPROC(privcmd_call);
