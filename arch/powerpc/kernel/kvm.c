/*
 * Copyright (C) 2010 SUSE Linux Products GmbH. All rights reserved.
 *
 * Authors:
 *     Alexander Graf <agraf@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kvm_host.h>
#include <linux/init.h>
#include <linux/kvm_para.h>
#include <linux/slab.h>
#include <linux/of.h>

#include <asm/reg.h>
#include <asm/kvm_ppc.h>
#include <asm/sections.h>
#include <asm/cacheflush.h>
#include <asm/disassemble.h>

#define KVM_MAGIC_PAGE		(-4096L)
#define magic_var(x) KVM_MAGIC_PAGE + offsetof(struct kvm_vcpu_arch_shared, x)

unsigned long kvm_hypercall(unsigned long *in,
			    unsigned long *out,
			    unsigned long nr)
{
	unsigned long register r0 asm("r0");
	unsigned long register r3 asm("r3") = in[0];
	unsigned long register r4 asm("r4") = in[1];
	unsigned long register r5 asm("r5") = in[2];
	unsigned long register r6 asm("r6") = in[3];
	unsigned long register r7 asm("r7") = in[4];
	unsigned long register r8 asm("r8") = in[5];
	unsigned long register r9 asm("r9") = in[6];
	unsigned long register r10 asm("r10") = in[7];
	unsigned long register r11 asm("r11") = nr;
	unsigned long register r12 asm("r12");

	asm volatile("bl	kvm_hypercall_start"
		     : "=r"(r0), "=r"(r3), "=r"(r4), "=r"(r5), "=r"(r6),
		       "=r"(r7), "=r"(r8), "=r"(r9), "=r"(r10), "=r"(r11),
		       "=r"(r12)
		     : "r"(r3), "r"(r4), "r"(r5), "r"(r6), "r"(r7), "r"(r8),
		       "r"(r9), "r"(r10), "r"(r11)
		     : "memory", "cc", "xer", "ctr", "lr");

	out[0] = r4;
	out[1] = r5;
	out[2] = r6;
	out[3] = r7;
	out[4] = r8;
	out[5] = r9;
	out[6] = r10;
	out[7] = r11;

	return r3;
}
EXPORT_SYMBOL_GPL(kvm_hypercall);
