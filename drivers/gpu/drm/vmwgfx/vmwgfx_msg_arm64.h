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

#define X86_IO_MAGIC 0x86

#define X86_IO_W7_SIZE_SHIFT 0
#define X86_IO_W7_SIZE_MASK (0x3 << X86_IO_W7_SIZE_SHIFT)
#define X86_IO_W7_DIR       (1 << 2)
#define X86_IO_W7_WITH	    (1 << 3)
#define X86_IO_W7_STR	    (1 << 4)
#define X86_IO_W7_DF	    (1 << 5)
#define X86_IO_W7_IMM_SHIFT  5
#define X86_IO_W7_IMM_MASK  (0xff << X86_IO_W7_IMM_SHIFT)

static inline void vmw_port(unsigned long cmd, unsigned long in_ebx,
			    unsigned long in_si, unsigned long in_di,
			    unsigned long flags, unsigned long magic,
			    unsigned long *eax, unsigned long *ebx,
			    unsigned long *ecx, unsigned long *edx,
			    unsigned long *si, unsigned long *di)
{
	register u64 x0 asm("x0") = magic;
	register u64 x1 asm("x1") = in_ebx;
	register u64 x2 asm("x2") = cmd;
	register u64 x3 asm("x3") = flags | VMWARE_HYPERVISOR_PORT;
	register u64 x4 asm("x4") = in_si;
	register u64 x5 asm("x5") = in_di;

	register u64 x7 asm("x7") = ((u64)X86_IO_MAGIC << 32) |
				    X86_IO_W7_WITH |
				    X86_IO_W7_DIR |
				    (2 << X86_IO_W7_SIZE_SHIFT);

	asm volatile("mrs xzr, mdccsr_el0 \n\t"
		     : "+r"(x0), "+r"(x1), "+r"(x2),
		       "+r"(x3), "+r"(x4), "+r"(x5)
		     : "r"(x7)
		     :);
	*eax = x0;
	*ebx = x1;
	*ecx = x2;
	*edx = x3;
	*si = x4;
	*di = x5;
}

static inline void vmw_port_hb(unsigned long cmd, unsigned long in_ecx,
			       unsigned long in_si, unsigned long in_di,
			       unsigned long flags, unsigned long magic,
			       unsigned long bp, u32 w7dir,
			       unsigned long *eax, unsigned long *ebx,
			       unsigned long *ecx, unsigned long *edx,
			       unsigned long *si, unsigned long *di)
{
	register u64 x0 asm("x0") = magic;
	register u64 x1 asm("x1") = cmd;
	register u64 x2 asm("x2") = in_ecx;
	register u64 x3 asm("x3") = flags | VMWARE_HYPERVISOR_PORT_HB;
	register u64 x4 asm("x4") = in_si;
	register u64 x5 asm("x5") = in_di;
	register u64 x6 asm("x6") = bp;
	register u64 x7 asm("x7") = ((u64)X86_IO_MAGIC << 32) |
				    X86_IO_W7_STR |
				    X86_IO_W7_WITH |
				    w7dir;

	asm volatile("mrs xzr, mdccsr_el0 \n\t"
		     : "+r"(x0), "+r"(x1), "+r"(x2),
		       "+r"(x3), "+r"(x4), "+r"(x5)
		     : "r"(x6), "r"(x7)
		     :);
	*eax = x0;
	*ebx = x1;
	*ecx = x2;
	*edx = x3;
	*si  = x4;
	*di  = x5;
}

#define VMW_PORT(cmd, in_ebx, in_si, in_di, flags, magic, eax, ebx, ecx, edx,  \
		 si, di)                                                       \
	vmw_port(cmd, in_ebx, in_si, in_di, flags, magic, &eax, &ebx, &ecx,    \
		 &edx, &si, &di)

#define VMW_PORT_HB_OUT(cmd, in_ecx, in_si, in_di, flags, magic, bp, eax, ebx, \
		        ecx, edx, si, di)                                      \
	vmw_port_hb(cmd, in_ecx, in_si, in_di, flags, magic, bp,               \
                    0, &eax, &ebx, &ecx, &edx, &si, &di)

#define VMW_PORT_HB_IN(cmd, in_ecx, in_si, in_di, flags, magic, bp, eax, ebx,  \
		       ecx, edx, si, di)                                       \
	vmw_port_hb(cmd, in_ecx, in_si, in_di, flags, magic, bp,               \
		    X86_IO_W7_DIR, &eax, &ebx, &ecx, &edx, &si, &di)

#endif

#endif /* _VMWGFX_MSG_ARM64_H */
