// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright 2021 VMware, Inc., Palo Alto, CA., USA
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef _VMWGFX_MSG_ARM64_H
#define _VMWGFX_MSG_ARM64_H

#if defined(__aarch64__)

#define VMWARE_HYPERVISOR_PORT    0x5658
#define VMWARE_HYPERVISOR_PORT_HB 0x5659

#define VMWARE_HYPERVISOR_HB  BIT(0)
#define VMWARE_HYPERVISOR_OUT BIT(1)

#define VMWARE_HYPERVISOR_MAGIC	0x564D5868

#define X86_IO_MAGIC 0x86

#define X86_IO_W7_SIZE_SHIFT 0
#define X86_IO_W7_SIZE_MASK (0x3 << X86_IO_W7_SIZE_SHIFT)
#define X86_IO_W7_DIR       (1 << 2)
#define X86_IO_W7_WITH	    (1 << 3)
#define X86_IO_W7_STR	    (1 << 4)
#define X86_IO_W7_DF	    (1 << 5)
#define X86_IO_W7_IMM_SHIFT  5
#define X86_IO_W7_IMM_MASK  (0xff << X86_IO_W7_IMM_SHIFT)

static inline
unsigned long vmware_hypercall1(unsigned long cmd, unsigned long in1)
{
	register u64 x0 asm("x0") = VMWARE_HYPERVISOR_MAGIC;
	register u64 x1 asm("x1") = in1;
	register u64 x2 asm("x2") = cmd;
	register u64 x3 asm("x3") = VMWARE_HYPERVISOR_PORT;
	register u64 x7 asm("x7") = ((u64)X86_IO_MAGIC << 32) |
				    X86_IO_W7_WITH |
				    X86_IO_W7_DIR |
				    (2 << X86_IO_W7_SIZE_SHIFT);

	asm_inline volatile (
		"mrs xzr, mdccsr_el0; "
		: "+r" (x0)
		: "r" (x1), "r" (x2), "r" (x3), "r" (x7)
		: "memory");

	return x0;
}

static inline
unsigned long vmware_hypercall5(unsigned long cmd, unsigned long in1,
				unsigned long in3, unsigned long in4,
				unsigned long in5, u32 *out2)
{
	register u64 x0 asm("x0") = VMWARE_HYPERVISOR_MAGIC;
	register u64 x1 asm("x1") = in1;
	register u64 x2 asm("x2") = cmd;
	register u64 x3 asm("x3") = in3 | VMWARE_HYPERVISOR_PORT;
	register u64 x4 asm("x4") = in4;
	register u64 x5 asm("x5") = in5;
	register u64 x7 asm("x7") = ((u64)X86_IO_MAGIC << 32) |
				    X86_IO_W7_WITH |
				    X86_IO_W7_DIR |
				    (2 << X86_IO_W7_SIZE_SHIFT);

	asm_inline volatile (
		"mrs xzr, mdccsr_el0; "
		: "+r" (x0), "+r" (x2)
		: "r" (x1), "r" (x3), "r" (x4), "r" (x5), "r" (x7)
		: "memory");

	*out2 = x2;
	return x0;
}

static inline
unsigned long vmware_hypercall6(unsigned long cmd, unsigned long in1,
				unsigned long in3, u32 *out2,
				u32 *out3, u32 *out4, u32 *out5)
{
	register u64 x0 asm("x0") = VMWARE_HYPERVISOR_MAGIC;
	register u64 x1 asm("x1") = in1;
	register u64 x2 asm("x2") = cmd;
	register u64 x3 asm("x3") = in3 | VMWARE_HYPERVISOR_PORT;
	register u64 x4 asm("x4");
	register u64 x5 asm("x5");
	register u64 x7 asm("x7") = ((u64)X86_IO_MAGIC << 32) |
				    X86_IO_W7_WITH |
				    X86_IO_W7_DIR |
				    (2 << X86_IO_W7_SIZE_SHIFT);

	asm_inline volatile (
		"mrs xzr, mdccsr_el0; "
		: "+r" (x0), "+r" (x2), "+r" (x3), "=r" (x4), "=r" (x5)
		: "r" (x1), "r" (x7)
		: "memory");

	*out2 = x2;
	*out3 = x3;
	*out4 = x4;
	*out5 = x5;
	return x0;
}

static inline
unsigned long vmware_hypercall7(unsigned long cmd, unsigned long in1,
				unsigned long in3, unsigned long in4,
				unsigned long in5, u32 *out1,
				u32 *out2, u32 *out3)
{
	register u64 x0 asm("x0") = VMWARE_HYPERVISOR_MAGIC;
	register u64 x1 asm("x1") = in1;
	register u64 x2 asm("x2") = cmd;
	register u64 x3 asm("x3") = in3 | VMWARE_HYPERVISOR_PORT;
	register u64 x4 asm("x4") = in4;
	register u64 x5 asm("x5") = in5;
	register u64 x7 asm("x7") = ((u64)X86_IO_MAGIC << 32) |
				    X86_IO_W7_WITH |
				    X86_IO_W7_DIR |
				    (2 << X86_IO_W7_SIZE_SHIFT);

	asm_inline volatile (
		"mrs xzr, mdccsr_el0; "
		: "+r" (x0), "+r" (x1), "+r" (x2), "+r" (x3)
		: "r" (x4), "r" (x5), "r" (x7)
		: "memory");

	*out1 = x1;
	*out2 = x2;
	*out3 = x3;
	return x0;
}

static inline
unsigned long vmware_hypercall_hb(unsigned long cmd, unsigned long in2,
				  unsigned long in3, unsigned long in4,
				  unsigned long in5, unsigned long in6,
				  u32 *out1, int dir)
{
	register u64 x0 asm("x0") = VMWARE_HYPERVISOR_MAGIC;
	register u64 x1 asm("x1") = cmd;
	register u64 x2 asm("x2") = in2;
	register u64 x3 asm("x3") = in3 | VMWARE_HYPERVISOR_PORT_HB;
	register u64 x4 asm("x4") = in4;
	register u64 x5 asm("x5") = in5;
	register u64 x6 asm("x6") = in6;
	register u64 x7 asm("x7") = ((u64)X86_IO_MAGIC << 32) |
				    X86_IO_W7_STR |
				    X86_IO_W7_WITH |
				    dir;

	asm_inline volatile (
		"mrs xzr, mdccsr_el0; "
		: "+r" (x0), "+r" (x1)
		: "r" (x2), "r" (x3), "r" (x4), "r" (x5),
		  "r" (x6), "r" (x7)
		: "memory");

	*out1 = x1;
	return x0;
}

static inline
unsigned long vmware_hypercall_hb_out(unsigned long cmd, unsigned long in2,
				      unsigned long in3, unsigned long in4,
				      unsigned long in5, unsigned long in6,
				      u32 *out1)
{
	return vmware_hypercall_hb(cmd, in2, in3, in4, in5, in6, out1, 0);
}

static inline
unsigned long vmware_hypercall_hb_in(unsigned long cmd, unsigned long in2,
				     unsigned long in3, unsigned long in4,
				     unsigned long in5, unsigned long in6,
				     u32 *out1)
{
	return vmware_hypercall_hb(cmd, in2, in3, in4, in5, in6,  out1,
				   X86_IO_W7_DIR);
}
#endif

#endif /* _VMWGFX_MSG_ARM64_H */
