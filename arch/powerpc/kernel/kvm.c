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

#define KVM_MASK_RT		0x03e00000

static bool kvm_patching_worked = true;

static inline void kvm_patch_ins(u32 *inst, u32 new_inst)
{
	*inst = new_inst;
	flush_icache_range((ulong)inst, (ulong)inst + 4);
}

static void kvm_map_magic_page(void *data)
{
	kvm_hypercall2(KVM_HC_PPC_MAP_MAGIC_PAGE,
		       KVM_MAGIC_PAGE,  /* Physical Address */
		       KVM_MAGIC_PAGE); /* Effective Address */
}

static void kvm_check_ins(u32 *inst)
{
	u32 _inst = *inst;
	u32 inst_no_rt = _inst & ~KVM_MASK_RT;
	u32 inst_rt = _inst & KVM_MASK_RT;

	switch (inst_no_rt) {
	}

	switch (_inst) {
	}
}

static void kvm_use_magic_page(void)
{
	u32 *p;
	u32 *start, *end;
	u32 tmp;

	/* Tell the host to map the magic page to -4096 on all CPUs */
	on_each_cpu(kvm_map_magic_page, NULL, 1);

	/* Quick self-test to see if the mapping works */
	if (__get_user(tmp, (u32*)KVM_MAGIC_PAGE)) {
		kvm_patching_worked = false;
		return;
	}

	/* Now loop through all code and find instructions */
	start = (void*)_stext;
	end = (void*)_etext;

	for (p = start; p < end; p++)
		kvm_check_ins(p);

	printk(KERN_INFO "KVM: Live patching for a fast VM %s\n",
			 kvm_patching_worked ? "worked" : "failed");
}

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

static int kvm_para_setup(void)
{
	extern u32 kvm_hypercall_start;
	struct device_node *hyper_node;
	u32 *insts;
	int len, i;

	hyper_node = of_find_node_by_path("/hypervisor");
	if (!hyper_node)
		return -1;

	insts = (u32*)of_get_property(hyper_node, "hcall-instructions", &len);
	if (len % 4)
		return -1;
	if (len > (4 * 4))
		return -1;

	for (i = 0; i < (len / 4); i++)
		kvm_patch_ins(&(&kvm_hypercall_start)[i], insts[i]);

	return 0;
}

static int __init kvm_guest_init(void)
{
	if (!kvm_para_available())
		return 0;

	if (kvm_para_setup())
		return 0;

	if (kvm_para_has_feature(KVM_FEATURE_MAGIC_PAGE))
		kvm_use_magic_page();

	return 0;
}

postcore_initcall(kvm_guest_init);
