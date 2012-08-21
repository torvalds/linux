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
#include <asm/sections.h>
#include <asm/cacheflush.h>
#include <asm/disassemble.h>

#define KVM_MAGIC_PAGE		(-4096L)
#define magic_var(x) KVM_MAGIC_PAGE + offsetof(struct kvm_vcpu_arch_shared, x)

#define KVM_INST_LWZ		0x80000000
#define KVM_INST_STW		0x90000000
#define KVM_INST_LD		0xe8000000
#define KVM_INST_STD		0xf8000000
#define KVM_INST_NOP		0x60000000
#define KVM_INST_B		0x48000000
#define KVM_INST_B_MASK		0x03ffffff
#define KVM_INST_B_MAX		0x01ffffff

#define KVM_MASK_RT		0x03e00000
#define KVM_RT_30		0x03c00000
#define KVM_MASK_RB		0x0000f800
#define KVM_INST_MFMSR		0x7c0000a6
#define KVM_INST_MFSPR_SPRG0	0x7c1042a6
#define KVM_INST_MFSPR_SPRG1	0x7c1142a6
#define KVM_INST_MFSPR_SPRG2	0x7c1242a6
#define KVM_INST_MFSPR_SPRG3	0x7c1342a6
#define KVM_INST_MFSPR_SRR0	0x7c1a02a6
#define KVM_INST_MFSPR_SRR1	0x7c1b02a6
#define KVM_INST_MFSPR_DAR	0x7c1302a6
#define KVM_INST_MFSPR_DSISR	0x7c1202a6

#define KVM_INST_MTSPR_SPRG0	0x7c1043a6
#define KVM_INST_MTSPR_SPRG1	0x7c1143a6
#define KVM_INST_MTSPR_SPRG2	0x7c1243a6
#define KVM_INST_MTSPR_SPRG3	0x7c1343a6
#define KVM_INST_MTSPR_SRR0	0x7c1a03a6
#define KVM_INST_MTSPR_SRR1	0x7c1b03a6
#define KVM_INST_MTSPR_DAR	0x7c1303a6
#define KVM_INST_MTSPR_DSISR	0x7c1203a6

#define KVM_INST_TLBSYNC	0x7c00046c
#define KVM_INST_MTMSRD_L0	0x7c000164
#define KVM_INST_MTMSRD_L1	0x7c010164
#define KVM_INST_MTMSR		0x7c000124

#define KVM_INST_WRTEEI_0	0x7c000146
#define KVM_INST_WRTEEI_1	0x7c008146

#define KVM_INST_MTSRIN		0x7c0001e4

static bool kvm_patching_worked = true;
static char kvm_tmp[1024 * 1024];
static int kvm_tmp_index;

static inline void kvm_patch_ins(u32 *inst, u32 new_inst)
{
	*inst = new_inst;
	flush_icache_range((ulong)inst, (ulong)inst + 4);
}

static void kvm_patch_ins_ll(u32 *inst, long addr, u32 rt)
{
#ifdef CONFIG_64BIT
	kvm_patch_ins(inst, KVM_INST_LD | rt | (addr & 0x0000fffc));
#else
	kvm_patch_ins(inst, KVM_INST_LWZ | rt | (addr & 0x0000fffc));
#endif
}

static void kvm_patch_ins_ld(u32 *inst, long addr, u32 rt)
{
#ifdef CONFIG_64BIT
	kvm_patch_ins(inst, KVM_INST_LD | rt | (addr & 0x0000fffc));
#else
	kvm_patch_ins(inst, KVM_INST_LWZ | rt | ((addr + 4) & 0x0000fffc));
#endif
}

static void kvm_patch_ins_lwz(u32 *inst, long addr, u32 rt)
{
	kvm_patch_ins(inst, KVM_INST_LWZ | rt | (addr & 0x0000ffff));
}

static void kvm_patch_ins_std(u32 *inst, long addr, u32 rt)
{
#ifdef CONFIG_64BIT
	kvm_patch_ins(inst, KVM_INST_STD | rt | (addr & 0x0000fffc));
#else
	kvm_patch_ins(inst, KVM_INST_STW | rt | ((addr + 4) & 0x0000fffc));
#endif
}

static void kvm_patch_ins_stw(u32 *inst, long addr, u32 rt)
{
	kvm_patch_ins(inst, KVM_INST_STW | rt | (addr & 0x0000fffc));
}

static void kvm_patch_ins_nop(u32 *inst)
{
	kvm_patch_ins(inst, KVM_INST_NOP);
}

static void kvm_patch_ins_b(u32 *inst, int addr)
{
#if defined(CONFIG_RELOCATABLE) && defined(CONFIG_PPC_BOOK3S)
	/* On relocatable kernels interrupts handlers and our code
	   can be in different regions, so we don't patch them */

	if ((ulong)inst < (ulong)&__end_interrupts)
		return;
#endif

	kvm_patch_ins(inst, KVM_INST_B | (addr & KVM_INST_B_MASK));
}

static u32 *kvm_alloc(int len)
{
	u32 *p;

	if ((kvm_tmp_index + len) > ARRAY_SIZE(kvm_tmp)) {
		printk(KERN_ERR "KVM: No more space (%d + %d)\n",
				kvm_tmp_index, len);
		kvm_patching_worked = false;
		return NULL;
	}

	p = (void*)&kvm_tmp[kvm_tmp_index];
	kvm_tmp_index += len;

	return p;
}

extern u32 kvm_emulate_mtmsrd_branch_offs;
extern u32 kvm_emulate_mtmsrd_reg_offs;
extern u32 kvm_emulate_mtmsrd_orig_ins_offs;
extern u32 kvm_emulate_mtmsrd_len;
extern u32 kvm_emulate_mtmsrd[];

static void kvm_patch_ins_mtmsrd(u32 *inst, u32 rt)
{
	u32 *p;
	int distance_start;
	int distance_end;
	ulong next_inst;

	p = kvm_alloc(kvm_emulate_mtmsrd_len * 4);
	if (!p)
		return;

	/* Find out where we are and put everything there */
	distance_start = (ulong)p - (ulong)inst;
	next_inst = ((ulong)inst + 4);
	distance_end = next_inst - (ulong)&p[kvm_emulate_mtmsrd_branch_offs];

	/* Make sure we only write valid b instructions */
	if (distance_start > KVM_INST_B_MAX) {
		kvm_patching_worked = false;
		return;
	}

	/* Modify the chunk to fit the invocation */
	memcpy(p, kvm_emulate_mtmsrd, kvm_emulate_mtmsrd_len * 4);
	p[kvm_emulate_mtmsrd_branch_offs] |= distance_end & KVM_INST_B_MASK;
	switch (get_rt(rt)) {
	case 30:
		kvm_patch_ins_ll(&p[kvm_emulate_mtmsrd_reg_offs],
				 magic_var(scratch2), KVM_RT_30);
		break;
	case 31:
		kvm_patch_ins_ll(&p[kvm_emulate_mtmsrd_reg_offs],
				 magic_var(scratch1), KVM_RT_30);
		break;
	default:
		p[kvm_emulate_mtmsrd_reg_offs] |= rt;
		break;
	}

	p[kvm_emulate_mtmsrd_orig_ins_offs] = *inst;
	flush_icache_range((ulong)p, (ulong)p + kvm_emulate_mtmsrd_len * 4);

	/* Patch the invocation */
	kvm_patch_ins_b(inst, distance_start);
}

extern u32 kvm_emulate_mtmsr_branch_offs;
extern u32 kvm_emulate_mtmsr_reg1_offs;
extern u32 kvm_emulate_mtmsr_reg2_offs;
extern u32 kvm_emulate_mtmsr_orig_ins_offs;
extern u32 kvm_emulate_mtmsr_len;
extern u32 kvm_emulate_mtmsr[];

static void kvm_patch_ins_mtmsr(u32 *inst, u32 rt)
{
	u32 *p;
	int distance_start;
	int distance_end;
	ulong next_inst;

	p = kvm_alloc(kvm_emulate_mtmsr_len * 4);
	if (!p)
		return;

	/* Find out where we are and put everything there */
	distance_start = (ulong)p - (ulong)inst;
	next_inst = ((ulong)inst + 4);
	distance_end = next_inst - (ulong)&p[kvm_emulate_mtmsr_branch_offs];

	/* Make sure we only write valid b instructions */
	if (distance_start > KVM_INST_B_MAX) {
		kvm_patching_worked = false;
		return;
	}

	/* Modify the chunk to fit the invocation */
	memcpy(p, kvm_emulate_mtmsr, kvm_emulate_mtmsr_len * 4);
	p[kvm_emulate_mtmsr_branch_offs] |= distance_end & KVM_INST_B_MASK;

	/* Make clobbered registers work too */
	switch (get_rt(rt)) {
	case 30:
		kvm_patch_ins_ll(&p[kvm_emulate_mtmsr_reg1_offs],
				 magic_var(scratch2), KVM_RT_30);
		kvm_patch_ins_ll(&p[kvm_emulate_mtmsr_reg2_offs],
				 magic_var(scratch2), KVM_RT_30);
		break;
	case 31:
		kvm_patch_ins_ll(&p[kvm_emulate_mtmsr_reg1_offs],
				 magic_var(scratch1), KVM_RT_30);
		kvm_patch_ins_ll(&p[kvm_emulate_mtmsr_reg2_offs],
				 magic_var(scratch1), KVM_RT_30);
		break;
	default:
		p[kvm_emulate_mtmsr_reg1_offs] |= rt;
		p[kvm_emulate_mtmsr_reg2_offs] |= rt;
		break;
	}

	p[kvm_emulate_mtmsr_orig_ins_offs] = *inst;
	flush_icache_range((ulong)p, (ulong)p + kvm_emulate_mtmsr_len * 4);

	/* Patch the invocation */
	kvm_patch_ins_b(inst, distance_start);
}

#ifdef CONFIG_BOOKE

extern u32 kvm_emulate_wrteei_branch_offs;
extern u32 kvm_emulate_wrteei_ee_offs;
extern u32 kvm_emulate_wrteei_len;
extern u32 kvm_emulate_wrteei[];

static void kvm_patch_ins_wrteei(u32 *inst)
{
	u32 *p;
	int distance_start;
	int distance_end;
	ulong next_inst;

	p = kvm_alloc(kvm_emulate_wrteei_len * 4);
	if (!p)
		return;

	/* Find out where we are and put everything there */
	distance_start = (ulong)p - (ulong)inst;
	next_inst = ((ulong)inst + 4);
	distance_end = next_inst - (ulong)&p[kvm_emulate_wrteei_branch_offs];

	/* Make sure we only write valid b instructions */
	if (distance_start > KVM_INST_B_MAX) {
		kvm_patching_worked = false;
		return;
	}

	/* Modify the chunk to fit the invocation */
	memcpy(p, kvm_emulate_wrteei, kvm_emulate_wrteei_len * 4);
	p[kvm_emulate_wrteei_branch_offs] |= distance_end & KVM_INST_B_MASK;
	p[kvm_emulate_wrteei_ee_offs] |= (*inst & MSR_EE);
	flush_icache_range((ulong)p, (ulong)p + kvm_emulate_wrteei_len * 4);

	/* Patch the invocation */
	kvm_patch_ins_b(inst, distance_start);
}

#endif

#ifdef CONFIG_PPC_BOOK3S_32

extern u32 kvm_emulate_mtsrin_branch_offs;
extern u32 kvm_emulate_mtsrin_reg1_offs;
extern u32 kvm_emulate_mtsrin_reg2_offs;
extern u32 kvm_emulate_mtsrin_orig_ins_offs;
extern u32 kvm_emulate_mtsrin_len;
extern u32 kvm_emulate_mtsrin[];

static void kvm_patch_ins_mtsrin(u32 *inst, u32 rt, u32 rb)
{
	u32 *p;
	int distance_start;
	int distance_end;
	ulong next_inst;

	p = kvm_alloc(kvm_emulate_mtsrin_len * 4);
	if (!p)
		return;

	/* Find out where we are and put everything there */
	distance_start = (ulong)p - (ulong)inst;
	next_inst = ((ulong)inst + 4);
	distance_end = next_inst - (ulong)&p[kvm_emulate_mtsrin_branch_offs];

	/* Make sure we only write valid b instructions */
	if (distance_start > KVM_INST_B_MAX) {
		kvm_patching_worked = false;
		return;
	}

	/* Modify the chunk to fit the invocation */
	memcpy(p, kvm_emulate_mtsrin, kvm_emulate_mtsrin_len * 4);
	p[kvm_emulate_mtsrin_branch_offs] |= distance_end & KVM_INST_B_MASK;
	p[kvm_emulate_mtsrin_reg1_offs] |= (rb << 10);
	p[kvm_emulate_mtsrin_reg2_offs] |= rt;
	p[kvm_emulate_mtsrin_orig_ins_offs] = *inst;
	flush_icache_range((ulong)p, (ulong)p + kvm_emulate_mtsrin_len * 4);

	/* Patch the invocation */
	kvm_patch_ins_b(inst, distance_start);
}

#endif

static void kvm_map_magic_page(void *data)
{
	u32 *features = data;

	ulong in[8];
	ulong out[8];

	in[0] = KVM_MAGIC_PAGE;
	in[1] = KVM_MAGIC_PAGE;

	kvm_hypercall(in, out, HC_VENDOR_KVM | KVM_HC_PPC_MAP_MAGIC_PAGE);

	*features = out[0];
}

static void kvm_check_ins(u32 *inst, u32 features)
{
	u32 _inst = *inst;
	u32 inst_no_rt = _inst & ~KVM_MASK_RT;
	u32 inst_rt = _inst & KVM_MASK_RT;

	switch (inst_no_rt) {
	/* Loads */
	case KVM_INST_MFMSR:
		kvm_patch_ins_ld(inst, magic_var(msr), inst_rt);
		break;
	case KVM_INST_MFSPR_SPRG0:
		kvm_patch_ins_ld(inst, magic_var(sprg0), inst_rt);
		break;
	case KVM_INST_MFSPR_SPRG1:
		kvm_patch_ins_ld(inst, magic_var(sprg1), inst_rt);
		break;
	case KVM_INST_MFSPR_SPRG2:
		kvm_patch_ins_ld(inst, magic_var(sprg2), inst_rt);
		break;
	case KVM_INST_MFSPR_SPRG3:
		kvm_patch_ins_ld(inst, magic_var(sprg3), inst_rt);
		break;
	case KVM_INST_MFSPR_SRR0:
		kvm_patch_ins_ld(inst, magic_var(srr0), inst_rt);
		break;
	case KVM_INST_MFSPR_SRR1:
		kvm_patch_ins_ld(inst, magic_var(srr1), inst_rt);
		break;
	case KVM_INST_MFSPR_DAR:
		kvm_patch_ins_ld(inst, magic_var(dar), inst_rt);
		break;
	case KVM_INST_MFSPR_DSISR:
		kvm_patch_ins_lwz(inst, magic_var(dsisr), inst_rt);
		break;

	/* Stores */
	case KVM_INST_MTSPR_SPRG0:
		kvm_patch_ins_std(inst, magic_var(sprg0), inst_rt);
		break;
	case KVM_INST_MTSPR_SPRG1:
		kvm_patch_ins_std(inst, magic_var(sprg1), inst_rt);
		break;
	case KVM_INST_MTSPR_SPRG2:
		kvm_patch_ins_std(inst, magic_var(sprg2), inst_rt);
		break;
	case KVM_INST_MTSPR_SPRG3:
		kvm_patch_ins_std(inst, magic_var(sprg3), inst_rt);
		break;
	case KVM_INST_MTSPR_SRR0:
		kvm_patch_ins_std(inst, magic_var(srr0), inst_rt);
		break;
	case KVM_INST_MTSPR_SRR1:
		kvm_patch_ins_std(inst, magic_var(srr1), inst_rt);
		break;
	case KVM_INST_MTSPR_DAR:
		kvm_patch_ins_std(inst, magic_var(dar), inst_rt);
		break;
	case KVM_INST_MTSPR_DSISR:
		kvm_patch_ins_stw(inst, magic_var(dsisr), inst_rt);
		break;

	/* Nops */
	case KVM_INST_TLBSYNC:
		kvm_patch_ins_nop(inst);
		break;

	/* Rewrites */
	case KVM_INST_MTMSRD_L1:
		kvm_patch_ins_mtmsrd(inst, inst_rt);
		break;
	case KVM_INST_MTMSR:
	case KVM_INST_MTMSRD_L0:
		kvm_patch_ins_mtmsr(inst, inst_rt);
		break;
	}

	switch (inst_no_rt & ~KVM_MASK_RB) {
#ifdef CONFIG_PPC_BOOK3S_32
	case KVM_INST_MTSRIN:
		if (features & KVM_MAGIC_FEAT_SR) {
			u32 inst_rb = _inst & KVM_MASK_RB;
			kvm_patch_ins_mtsrin(inst, inst_rt, inst_rb);
		}
		break;
		break;
#endif
	}

	switch (_inst) {
#ifdef CONFIG_BOOKE
	case KVM_INST_WRTEEI_0:
	case KVM_INST_WRTEEI_1:
		kvm_patch_ins_wrteei(inst);
		break;
#endif
	}
}

static void kvm_use_magic_page(void)
{
	u32 *p;
	u32 *start, *end;
	u32 tmp;
	u32 features;

	/* Tell the host to map the magic page to -4096 on all CPUs */
	on_each_cpu(kvm_map_magic_page, &features, 1);

	/* Quick self-test to see if the mapping works */
	if (__get_user(tmp, (u32*)KVM_MAGIC_PAGE)) {
		kvm_patching_worked = false;
		return;
	}

	/* Now loop through all code and find instructions */
	start = (void*)_stext;
	end = (void*)_etext;

	for (p = start; p < end; p++)
		kvm_check_ins(p, features);

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

static __init void kvm_free_tmp(void)
{
	unsigned long start, end;

	start = (ulong)&kvm_tmp[kvm_tmp_index + (PAGE_SIZE - 1)] & PAGE_MASK;
	end = (ulong)&kvm_tmp[ARRAY_SIZE(kvm_tmp)] & PAGE_MASK;

	/* Free the tmp space we don't need */
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		init_page_count(virt_to_page(start));
		free_page(start);
		totalram_pages++;
	}
}

static int __init kvm_guest_init(void)
{
	if (!kvm_para_available())
		goto free_tmp;

	if (kvm_para_setup())
		goto free_tmp;

	if (kvm_para_has_feature(KVM_FEATURE_MAGIC_PAGE))
		kvm_use_magic_page();

#ifdef CONFIG_PPC_BOOK3S_64
	/* Enable napping */
	powersave_nap = 1;
#endif

free_tmp:
	kvm_free_tmp();

	return 0;
}

postcore_initcall(kvm_guest_init);
