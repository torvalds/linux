// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2001 Ben. Herrenschmidt (benh@kernel.crashing.org)
 *
 *  Modifications for ppc64:
 *      Copyright (C) 2003 Dave Engebretsen <engebret@us.ibm.com>
 *
 *  Copyright 2008 Michael Ellerman, IBM Corporation.
 */

#include <linux/types.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/sched/mm.h>
#include <linux/stop_machine.h>
#include <asm/cputable.h>
#include <asm/code-patching.h>
#include <asm/page.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/security_features.h>
#include <asm/firmware.h>
#include <asm/inst.h>

struct fixup_entry {
	unsigned long	mask;
	unsigned long	value;
	long		start_off;
	long		end_off;
	long		alt_start_off;
	long		alt_end_off;
};

static struct ppc_inst *calc_addr(struct fixup_entry *fcur, long offset)
{
	/*
	 * We store the offset to the code as a negative offset from
	 * the start of the alt_entry, to support the VDSO. This
	 * routine converts that back into an actual address.
	 */
	return (struct ppc_inst *)((unsigned long)fcur + offset);
}

static int patch_alt_instruction(struct ppc_inst *src, struct ppc_inst *dest,
				 struct ppc_inst *alt_start, struct ppc_inst *alt_end)
{
	int err;
	struct ppc_inst instr;

	instr = ppc_inst_read(src);

	if (instr_is_relative_branch(*src)) {
		struct ppc_inst *target = (struct ppc_inst *)branch_target(src);

		/* Branch within the section doesn't need translating */
		if (target < alt_start || target > alt_end) {
			err = translate_branch(&instr, dest, src);
			if (err)
				return 1;
		}
	}

	raw_patch_instruction(dest, instr);

	return 0;
}

static int patch_feature_section(unsigned long value, struct fixup_entry *fcur)
{
	struct ppc_inst *start, *end, *alt_start, *alt_end, *src, *dest, nop;

	start = calc_addr(fcur, fcur->start_off);
	end = calc_addr(fcur, fcur->end_off);
	alt_start = calc_addr(fcur, fcur->alt_start_off);
	alt_end = calc_addr(fcur, fcur->alt_end_off);

	if ((alt_end - alt_start) > (end - start))
		return 1;

	if ((value & fcur->mask) == fcur->value)
		return 0;

	src = alt_start;
	dest = start;

	for (; src < alt_end; src = ppc_inst_next(src, src),
			      dest = ppc_inst_next(dest, dest)) {
		if (patch_alt_instruction(src, dest, alt_start, alt_end))
			return 1;
	}

	nop = ppc_inst(PPC_INST_NOP);
	for (; dest < end; dest = ppc_inst_next(dest, &nop))
		raw_patch_instruction(dest, nop);

	return 0;
}

void do_feature_fixups(unsigned long value, void *fixup_start, void *fixup_end)
{
	struct fixup_entry *fcur, *fend;

	fcur = fixup_start;
	fend = fixup_end;

	for (; fcur < fend; fcur++) {
		if (patch_feature_section(value, fcur)) {
			WARN_ON(1);
			printk("Unable to patch feature section at %p - %p" \
				" with %p - %p\n",
				calc_addr(fcur, fcur->start_off),
				calc_addr(fcur, fcur->end_off),
				calc_addr(fcur, fcur->alt_start_off),
				calc_addr(fcur, fcur->alt_end_off));
		}
	}
}

#ifdef CONFIG_PPC_BOOK3S_64
static void do_stf_entry_barrier_fixups(enum stf_barrier_type types)
{
	unsigned int instrs[3], *dest;
	long *start, *end;
	int i;

	start = PTRRELOC(&__start___stf_entry_barrier_fixup);
	end = PTRRELOC(&__stop___stf_entry_barrier_fixup);

	instrs[0] = 0x60000000; /* nop */
	instrs[1] = 0x60000000; /* nop */
	instrs[2] = 0x60000000; /* nop */

	i = 0;
	if (types & STF_BARRIER_FALLBACK) {
		instrs[i++] = 0x7d4802a6; /* mflr r10		*/
		instrs[i++] = 0x60000000; /* branch patched below */
		instrs[i++] = 0x7d4803a6; /* mtlr r10		*/
	} else if (types & STF_BARRIER_EIEIO) {
		instrs[i++] = 0x7e0006ac; /* eieio + bit 6 hint */
	} else if (types & STF_BARRIER_SYNC_ORI) {
		instrs[i++] = 0x7c0004ac; /* hwsync		*/
		instrs[i++] = 0xe94d0000; /* ld r10,0(r13)	*/
		instrs[i++] = 0x63ff0000; /* ori 31,31,0 speculation barrier */
	}

	for (i = 0; start < end; start++, i++) {
		dest = (void *)start + *start;

		pr_devel("patching dest %lx\n", (unsigned long)dest);

		// See comment in do_entry_flush_fixups() RE order of patching
		if (types & STF_BARRIER_FALLBACK) {
			patch_instruction((struct ppc_inst *)dest, ppc_inst(instrs[0]));
			patch_instruction((struct ppc_inst *)(dest + 2), ppc_inst(instrs[2]));
			patch_branch((struct ppc_inst *)(dest + 1),
				     (unsigned long)&stf_barrier_fallback, BRANCH_SET_LINK);
		} else {
			patch_instruction((struct ppc_inst *)(dest + 1), ppc_inst(instrs[1]));
			patch_instruction((struct ppc_inst *)(dest + 2), ppc_inst(instrs[2]));
			patch_instruction((struct ppc_inst *)dest, ppc_inst(instrs[0]));
		}
	}

	printk(KERN_DEBUG "stf-barrier: patched %d entry locations (%s barrier)\n", i,
		(types == STF_BARRIER_NONE)                  ? "no" :
		(types == STF_BARRIER_FALLBACK)              ? "fallback" :
		(types == STF_BARRIER_EIEIO)                 ? "eieio" :
		(types == (STF_BARRIER_SYNC_ORI))            ? "hwsync"
		                                           : "unknown");
}

static void do_stf_exit_barrier_fixups(enum stf_barrier_type types)
{
	unsigned int instrs[6], *dest;
	long *start, *end;
	int i;

	start = PTRRELOC(&__start___stf_exit_barrier_fixup);
	end = PTRRELOC(&__stop___stf_exit_barrier_fixup);

	instrs[0] = 0x60000000; /* nop */
	instrs[1] = 0x60000000; /* nop */
	instrs[2] = 0x60000000; /* nop */
	instrs[3] = 0x60000000; /* nop */
	instrs[4] = 0x60000000; /* nop */
	instrs[5] = 0x60000000; /* nop */

	i = 0;
	if (types & STF_BARRIER_FALLBACK || types & STF_BARRIER_SYNC_ORI) {
		if (cpu_has_feature(CPU_FTR_HVMODE)) {
			instrs[i++] = 0x7db14ba6; /* mtspr 0x131, r13 (HSPRG1) */
			instrs[i++] = 0x7db04aa6; /* mfspr r13, 0x130 (HSPRG0) */
		} else {
			instrs[i++] = 0x7db243a6; /* mtsprg 2,r13	*/
			instrs[i++] = 0x7db142a6; /* mfsprg r13,1    */
	        }
		instrs[i++] = 0x7c0004ac; /* hwsync		*/
		instrs[i++] = 0xe9ad0000; /* ld r13,0(r13)	*/
		instrs[i++] = 0x63ff0000; /* ori 31,31,0 speculation barrier */
		if (cpu_has_feature(CPU_FTR_HVMODE)) {
			instrs[i++] = 0x7db14aa6; /* mfspr r13, 0x131 (HSPRG1) */
		} else {
			instrs[i++] = 0x7db242a6; /* mfsprg r13,2 */
		}
	} else if (types & STF_BARRIER_EIEIO) {
		instrs[i++] = 0x7e0006ac; /* eieio + bit 6 hint */
	}

	for (i = 0; start < end; start++, i++) {
		dest = (void *)start + *start;

		pr_devel("patching dest %lx\n", (unsigned long)dest);

		patch_instruction((struct ppc_inst *)dest, ppc_inst(instrs[0]));
		patch_instruction((struct ppc_inst *)(dest + 1), ppc_inst(instrs[1]));
		patch_instruction((struct ppc_inst *)(dest + 2), ppc_inst(instrs[2]));
		patch_instruction((struct ppc_inst *)(dest + 3), ppc_inst(instrs[3]));
		patch_instruction((struct ppc_inst *)(dest + 4), ppc_inst(instrs[4]));
		patch_instruction((struct ppc_inst *)(dest + 5), ppc_inst(instrs[5]));
	}
	printk(KERN_DEBUG "stf-barrier: patched %d exit locations (%s barrier)\n", i,
		(types == STF_BARRIER_NONE)                  ? "no" :
		(types == STF_BARRIER_FALLBACK)              ? "fallback" :
		(types == STF_BARRIER_EIEIO)                 ? "eieio" :
		(types == (STF_BARRIER_SYNC_ORI))            ? "hwsync"
		                                           : "unknown");
}

static int __do_stf_barrier_fixups(void *data)
{
	enum stf_barrier_type *types = data;

	do_stf_entry_barrier_fixups(*types);
	do_stf_exit_barrier_fixups(*types);

	return 0;
}

void do_stf_barrier_fixups(enum stf_barrier_type types)
{
	/*
	 * The call to the fallback entry flush, and the fallback/sync-ori exit
	 * flush can not be safely patched in/out while other CPUs are executing
	 * them. So call __do_stf_barrier_fixups() on one CPU while all other CPUs
	 * spin in the stop machine core with interrupts hard disabled.
	 */
	stop_machine(__do_stf_barrier_fixups, &types, NULL);
}

void do_uaccess_flush_fixups(enum l1d_flush_type types)
{
	unsigned int instrs[4], *dest;
	long *start, *end;
	int i;

	start = PTRRELOC(&__start___uaccess_flush_fixup);
	end = PTRRELOC(&__stop___uaccess_flush_fixup);

	instrs[0] = 0x60000000; /* nop */
	instrs[1] = 0x60000000; /* nop */
	instrs[2] = 0x60000000; /* nop */
	instrs[3] = 0x4e800020; /* blr */

	i = 0;
	if (types == L1D_FLUSH_FALLBACK) {
		instrs[3] = 0x60000000; /* nop */
		/* fallthrough to fallback flush */
	}

	if (types & L1D_FLUSH_ORI) {
		instrs[i++] = 0x63ff0000; /* ori 31,31,0 speculation barrier */
		instrs[i++] = 0x63de0000; /* ori 30,30,0 L1d flush*/
	}

	if (types & L1D_FLUSH_MTTRIG)
		instrs[i++] = 0x7c12dba6; /* mtspr TRIG2,r0 (SPR #882) */

	for (i = 0; start < end; start++, i++) {
		dest = (void *)start + *start;

		pr_devel("patching dest %lx\n", (unsigned long)dest);

		patch_instruction((struct ppc_inst *)dest, ppc_inst(instrs[0]));

		patch_instruction((struct ppc_inst *)(dest + 1), ppc_inst(instrs[1]));
		patch_instruction((struct ppc_inst *)(dest + 2), ppc_inst(instrs[2]));
		patch_instruction((struct ppc_inst *)(dest + 3), ppc_inst(instrs[3]));
	}

	printk(KERN_DEBUG "uaccess-flush: patched %d locations (%s flush)\n", i,
		(types == L1D_FLUSH_NONE)       ? "no" :
		(types == L1D_FLUSH_FALLBACK)   ? "fallback displacement" :
		(types &  L1D_FLUSH_ORI)        ? (types & L1D_FLUSH_MTTRIG)
							? "ori+mttrig type"
							: "ori type" :
		(types &  L1D_FLUSH_MTTRIG)     ? "mttrig type"
						: "unknown");
}

static int __do_entry_flush_fixups(void *data)
{
	enum l1d_flush_type types = *(enum l1d_flush_type *)data;
	unsigned int instrs[3], *dest;
	long *start, *end;
	int i;

	instrs[0] = 0x60000000; /* nop */
	instrs[1] = 0x60000000; /* nop */
	instrs[2] = 0x60000000; /* nop */

	i = 0;
	if (types == L1D_FLUSH_FALLBACK) {
		instrs[i++] = 0x7d4802a6; /* mflr r10		*/
		instrs[i++] = 0x60000000; /* branch patched below */
		instrs[i++] = 0x7d4803a6; /* mtlr r10		*/
	}

	if (types & L1D_FLUSH_ORI) {
		instrs[i++] = 0x63ff0000; /* ori 31,31,0 speculation barrier */
		instrs[i++] = 0x63de0000; /* ori 30,30,0 L1d flush*/
	}

	if (types & L1D_FLUSH_MTTRIG)
		instrs[i++] = 0x7c12dba6; /* mtspr TRIG2,r0 (SPR #882) */

	/*
	 * If we're patching in or out the fallback flush we need to be careful about the
	 * order in which we patch instructions. That's because it's possible we could
	 * take a page fault after patching one instruction, so the sequence of
	 * instructions must be safe even in a half patched state.
	 *
	 * To make that work, when patching in the fallback flush we patch in this order:
	 *  - the mflr		(dest)
	 *  - the mtlr		(dest + 2)
	 *  - the branch	(dest + 1)
	 *
	 * That ensures the sequence is safe to execute at any point. In contrast if we
	 * patch the mtlr last, it's possible we could return from the branch and not
	 * restore LR, leading to a crash later.
	 *
	 * When patching out the fallback flush (either with nops or another flush type),
	 * we patch in this order:
	 *  - the branch	(dest + 1)
	 *  - the mtlr		(dest + 2)
	 *  - the mflr		(dest)
	 *
	 * Note we are protected by stop_machine() from other CPUs executing the code in a
	 * semi-patched state.
	 */

	start = PTRRELOC(&__start___entry_flush_fixup);
	end = PTRRELOC(&__stop___entry_flush_fixup);
	for (i = 0; start < end; start++, i++) {
		dest = (void *)start + *start;

		pr_devel("patching dest %lx\n", (unsigned long)dest);

		if (types == L1D_FLUSH_FALLBACK) {
			patch_instruction((struct ppc_inst *)dest, ppc_inst(instrs[0]));
			patch_instruction((struct ppc_inst *)(dest + 2), ppc_inst(instrs[2]));
			patch_branch((struct ppc_inst *)(dest + 1),
				     (unsigned long)&entry_flush_fallback, BRANCH_SET_LINK);
		} else {
			patch_instruction((struct ppc_inst *)(dest + 1), ppc_inst(instrs[1]));
			patch_instruction((struct ppc_inst *)(dest + 2), ppc_inst(instrs[2]));
			patch_instruction((struct ppc_inst *)dest, ppc_inst(instrs[0]));
		}
	}

	start = PTRRELOC(&__start___scv_entry_flush_fixup);
	end = PTRRELOC(&__stop___scv_entry_flush_fixup);
	for (; start < end; start++, i++) {
		dest = (void *)start + *start;

		pr_devel("patching dest %lx\n", (unsigned long)dest);

		if (types == L1D_FLUSH_FALLBACK) {
			patch_instruction((struct ppc_inst *)dest, ppc_inst(instrs[0]));
			patch_instruction((struct ppc_inst *)(dest + 2), ppc_inst(instrs[2]));
			patch_branch((struct ppc_inst *)(dest + 1),
				     (unsigned long)&scv_entry_flush_fallback, BRANCH_SET_LINK);
		} else {
			patch_instruction((struct ppc_inst *)(dest + 1), ppc_inst(instrs[1]));
			patch_instruction((struct ppc_inst *)(dest + 2), ppc_inst(instrs[2]));
			patch_instruction((struct ppc_inst *)dest, ppc_inst(instrs[0]));
		}
	}


	printk(KERN_DEBUG "entry-flush: patched %d locations (%s flush)\n", i,
		(types == L1D_FLUSH_NONE)       ? "no" :
		(types == L1D_FLUSH_FALLBACK)   ? "fallback displacement" :
		(types &  L1D_FLUSH_ORI)        ? (types & L1D_FLUSH_MTTRIG)
							? "ori+mttrig type"
							: "ori type" :
		(types &  L1D_FLUSH_MTTRIG)     ? "mttrig type"
						: "unknown");

	return 0;
}

void do_entry_flush_fixups(enum l1d_flush_type types)
{
	/*
	 * The call to the fallback flush can not be safely patched in/out while
	 * other CPUs are executing it. So call __do_entry_flush_fixups() on one
	 * CPU while all other CPUs spin in the stop machine core with interrupts
	 * hard disabled.
	 */
	stop_machine(__do_entry_flush_fixups, &types, NULL);
}

void do_rfi_flush_fixups(enum l1d_flush_type types)
{
	unsigned int instrs[3], *dest;
	long *start, *end;
	int i;

	start = PTRRELOC(&__start___rfi_flush_fixup);
	end = PTRRELOC(&__stop___rfi_flush_fixup);

	instrs[0] = 0x60000000; /* nop */
	instrs[1] = 0x60000000; /* nop */
	instrs[2] = 0x60000000; /* nop */

	if (types & L1D_FLUSH_FALLBACK)
		/* b .+16 to fallback flush */
		instrs[0] = 0x48000010;

	i = 0;
	if (types & L1D_FLUSH_ORI) {
		instrs[i++] = 0x63ff0000; /* ori 31,31,0 speculation barrier */
		instrs[i++] = 0x63de0000; /* ori 30,30,0 L1d flush*/
	}

	if (types & L1D_FLUSH_MTTRIG)
		instrs[i++] = 0x7c12dba6; /* mtspr TRIG2,r0 (SPR #882) */

	for (i = 0; start < end; start++, i++) {
		dest = (void *)start + *start;

		pr_devel("patching dest %lx\n", (unsigned long)dest);

		patch_instruction((struct ppc_inst *)dest, ppc_inst(instrs[0]));
		patch_instruction((struct ppc_inst *)(dest + 1), ppc_inst(instrs[1]));
		patch_instruction((struct ppc_inst *)(dest + 2), ppc_inst(instrs[2]));
	}

	printk(KERN_DEBUG "rfi-flush: patched %d locations (%s flush)\n", i,
		(types == L1D_FLUSH_NONE)       ? "no" :
		(types == L1D_FLUSH_FALLBACK)   ? "fallback displacement" :
		(types &  L1D_FLUSH_ORI)        ? (types & L1D_FLUSH_MTTRIG)
							? "ori+mttrig type"
							: "ori type" :
		(types &  L1D_FLUSH_MTTRIG)     ? "mttrig type"
						: "unknown");
}

void do_barrier_nospec_fixups_range(bool enable, void *fixup_start, void *fixup_end)
{
	unsigned int instr, *dest;
	long *start, *end;
	int i;

	start = fixup_start;
	end = fixup_end;

	instr = 0x60000000; /* nop */

	if (enable) {
		pr_info("barrier-nospec: using ORI speculation barrier\n");
		instr = 0x63ff0000; /* ori 31,31,0 speculation barrier */
	}

	for (i = 0; start < end; start++, i++) {
		dest = (void *)start + *start;

		pr_devel("patching dest %lx\n", (unsigned long)dest);
		patch_instruction((struct ppc_inst *)dest, ppc_inst(instr));
	}

	printk(KERN_DEBUG "barrier-nospec: patched %d locations\n", i);
}

#endif /* CONFIG_PPC_BOOK3S_64 */

#ifdef CONFIG_PPC_BARRIER_NOSPEC
void do_barrier_nospec_fixups(bool enable)
{
	void *start, *end;

	start = PTRRELOC(&__start___barrier_nospec_fixup);
	end = PTRRELOC(&__stop___barrier_nospec_fixup);

	do_barrier_nospec_fixups_range(enable, start, end);
}
#endif /* CONFIG_PPC_BARRIER_NOSPEC */

#ifdef CONFIG_PPC_FSL_BOOK3E
void do_barrier_nospec_fixups_range(bool enable, void *fixup_start, void *fixup_end)
{
	unsigned int instr[2], *dest;
	long *start, *end;
	int i;

	start = fixup_start;
	end = fixup_end;

	instr[0] = PPC_INST_NOP;
	instr[1] = PPC_INST_NOP;

	if (enable) {
		pr_info("barrier-nospec: using isync; sync as speculation barrier\n");
		instr[0] = PPC_INST_ISYNC;
		instr[1] = PPC_INST_SYNC;
	}

	for (i = 0; start < end; start++, i++) {
		dest = (void *)start + *start;

		pr_devel("patching dest %lx\n", (unsigned long)dest);
		patch_instruction((struct ppc_inst *)dest, ppc_inst(instr[0]));
		patch_instruction((struct ppc_inst *)(dest + 1), ppc_inst(instr[1]));
	}

	printk(KERN_DEBUG "barrier-nospec: patched %d locations\n", i);
}

static void patch_btb_flush_section(long *curr)
{
	unsigned int *start, *end;

	start = (void *)curr + *curr;
	end = (void *)curr + *(curr + 1);
	for (; start < end; start++) {
		pr_devel("patching dest %lx\n", (unsigned long)start);
		patch_instruction((struct ppc_inst *)start, ppc_inst(PPC_INST_NOP));
	}
}

void do_btb_flush_fixups(void)
{
	long *start, *end;

	start = PTRRELOC(&__start__btb_flush_fixup);
	end = PTRRELOC(&__stop__btb_flush_fixup);

	for (; start < end; start += 2)
		patch_btb_flush_section(start);
}
#endif /* CONFIG_PPC_FSL_BOOK3E */

void do_lwsync_fixups(unsigned long value, void *fixup_start, void *fixup_end)
{
	long *start, *end;
	struct ppc_inst *dest;

	if (!(value & CPU_FTR_LWSYNC))
		return ;

	start = fixup_start;
	end = fixup_end;

	for (; start < end; start++) {
		dest = (void *)start + *start;
		raw_patch_instruction(dest, ppc_inst(PPC_INST_LWSYNC));
	}
}

static void do_final_fixups(void)
{
#if defined(CONFIG_PPC64) && defined(CONFIG_RELOCATABLE)
	struct ppc_inst inst, *src, *dest, *end;

	if (PHYSICAL_START == 0)
		return;

	src = (struct ppc_inst *)(KERNELBASE + PHYSICAL_START);
	dest = (struct ppc_inst *)KERNELBASE;
	end = (void *)src + (__end_interrupts - _stext);

	while (src < end) {
		inst = ppc_inst_read(src);
		raw_patch_instruction(dest, inst);
		src = ppc_inst_next(src, src);
		dest = ppc_inst_next(dest, dest);
	}
#endif
}

static unsigned long __initdata saved_cpu_features;
static unsigned int __initdata saved_mmu_features;
#ifdef CONFIG_PPC64
static unsigned long __initdata saved_firmware_features;
#endif

void __init apply_feature_fixups(void)
{
	struct cpu_spec *spec = PTRRELOC(*PTRRELOC(&cur_cpu_spec));

	*PTRRELOC(&saved_cpu_features) = spec->cpu_features;
	*PTRRELOC(&saved_mmu_features) = spec->mmu_features;

	/*
	 * Apply the CPU-specific and firmware specific fixups to kernel text
	 * (nop out sections not relevant to this CPU or this firmware).
	 */
	do_feature_fixups(spec->cpu_features,
			  PTRRELOC(&__start___ftr_fixup),
			  PTRRELOC(&__stop___ftr_fixup));

	do_feature_fixups(spec->mmu_features,
			  PTRRELOC(&__start___mmu_ftr_fixup),
			  PTRRELOC(&__stop___mmu_ftr_fixup));

	do_lwsync_fixups(spec->cpu_features,
			 PTRRELOC(&__start___lwsync_fixup),
			 PTRRELOC(&__stop___lwsync_fixup));

#ifdef CONFIG_PPC64
	saved_firmware_features = powerpc_firmware_features;
	do_feature_fixups(powerpc_firmware_features,
			  &__start___fw_ftr_fixup, &__stop___fw_ftr_fixup);
#endif
	do_final_fixups();
}

void __init setup_feature_keys(void)
{
	/*
	 * Initialise jump label. This causes all the cpu/mmu_has_feature()
	 * checks to take on their correct polarity based on the current set of
	 * CPU/MMU features.
	 */
	jump_label_init();
	cpu_feature_keys_init();
	mmu_feature_keys_init();
}

static int __init check_features(void)
{
	WARN(saved_cpu_features != cur_cpu_spec->cpu_features,
	     "CPU features changed after feature patching!\n");
	WARN(saved_mmu_features != cur_cpu_spec->mmu_features,
	     "MMU features changed after feature patching!\n");
#ifdef CONFIG_PPC64
	WARN(saved_firmware_features != powerpc_firmware_features,
	     "Firmware features changed after feature patching!\n");
#endif

	return 0;
}
late_initcall(check_features);

#ifdef CONFIG_FTR_FIXUP_SELFTEST

#define check(x)	\
	if (!(x)) printk("feature-fixups: test failed at line %d\n", __LINE__);

/* This must be after the text it fixes up, vmlinux.lds.S enforces that atm */
static struct fixup_entry fixup;

static long calc_offset(struct fixup_entry *entry, unsigned int *p)
{
	return (unsigned long)p - (unsigned long)entry;
}

static void test_basic_patching(void)
{
	extern unsigned int ftr_fixup_test1[];
	extern unsigned int end_ftr_fixup_test1[];
	extern unsigned int ftr_fixup_test1_orig[];
	extern unsigned int ftr_fixup_test1_expected[];
	int size = 4 * (end_ftr_fixup_test1 - ftr_fixup_test1);

	fixup.value = fixup.mask = 8;
	fixup.start_off = calc_offset(&fixup, ftr_fixup_test1 + 1);
	fixup.end_off = calc_offset(&fixup, ftr_fixup_test1 + 2);
	fixup.alt_start_off = fixup.alt_end_off = 0;

	/* Sanity check */
	check(memcmp(ftr_fixup_test1, ftr_fixup_test1_orig, size) == 0);

	/* Check we don't patch if the value matches */
	patch_feature_section(8, &fixup);
	check(memcmp(ftr_fixup_test1, ftr_fixup_test1_orig, size) == 0);

	/* Check we do patch if the value doesn't match */
	patch_feature_section(0, &fixup);
	check(memcmp(ftr_fixup_test1, ftr_fixup_test1_expected, size) == 0);

	/* Check we do patch if the mask doesn't match */
	memcpy(ftr_fixup_test1, ftr_fixup_test1_orig, size);
	check(memcmp(ftr_fixup_test1, ftr_fixup_test1_orig, size) == 0);
	patch_feature_section(~8, &fixup);
	check(memcmp(ftr_fixup_test1, ftr_fixup_test1_expected, size) == 0);
}

static void test_alternative_patching(void)
{
	extern unsigned int ftr_fixup_test2[];
	extern unsigned int end_ftr_fixup_test2[];
	extern unsigned int ftr_fixup_test2_orig[];
	extern unsigned int ftr_fixup_test2_alt[];
	extern unsigned int ftr_fixup_test2_expected[];
	int size = 4 * (end_ftr_fixup_test2 - ftr_fixup_test2);

	fixup.value = fixup.mask = 0xF;
	fixup.start_off = calc_offset(&fixup, ftr_fixup_test2 + 1);
	fixup.end_off = calc_offset(&fixup, ftr_fixup_test2 + 2);
	fixup.alt_start_off = calc_offset(&fixup, ftr_fixup_test2_alt);
	fixup.alt_end_off = calc_offset(&fixup, ftr_fixup_test2_alt + 1);

	/* Sanity check */
	check(memcmp(ftr_fixup_test2, ftr_fixup_test2_orig, size) == 0);

	/* Check we don't patch if the value matches */
	patch_feature_section(0xF, &fixup);
	check(memcmp(ftr_fixup_test2, ftr_fixup_test2_orig, size) == 0);

	/* Check we do patch if the value doesn't match */
	patch_feature_section(0, &fixup);
	check(memcmp(ftr_fixup_test2, ftr_fixup_test2_expected, size) == 0);

	/* Check we do patch if the mask doesn't match */
	memcpy(ftr_fixup_test2, ftr_fixup_test2_orig, size);
	check(memcmp(ftr_fixup_test2, ftr_fixup_test2_orig, size) == 0);
	patch_feature_section(~0xF, &fixup);
	check(memcmp(ftr_fixup_test2, ftr_fixup_test2_expected, size) == 0);
}

static void test_alternative_case_too_big(void)
{
	extern unsigned int ftr_fixup_test3[];
	extern unsigned int end_ftr_fixup_test3[];
	extern unsigned int ftr_fixup_test3_orig[];
	extern unsigned int ftr_fixup_test3_alt[];
	int size = 4 * (end_ftr_fixup_test3 - ftr_fixup_test3);

	fixup.value = fixup.mask = 0xC;
	fixup.start_off = calc_offset(&fixup, ftr_fixup_test3 + 1);
	fixup.end_off = calc_offset(&fixup, ftr_fixup_test3 + 2);
	fixup.alt_start_off = calc_offset(&fixup, ftr_fixup_test3_alt);
	fixup.alt_end_off = calc_offset(&fixup, ftr_fixup_test3_alt + 2);

	/* Sanity check */
	check(memcmp(ftr_fixup_test3, ftr_fixup_test3_orig, size) == 0);

	/* Expect nothing to be patched, and the error returned to us */
	check(patch_feature_section(0xF, &fixup) == 1);
	check(memcmp(ftr_fixup_test3, ftr_fixup_test3_orig, size) == 0);
	check(patch_feature_section(0, &fixup) == 1);
	check(memcmp(ftr_fixup_test3, ftr_fixup_test3_orig, size) == 0);
	check(patch_feature_section(~0xF, &fixup) == 1);
	check(memcmp(ftr_fixup_test3, ftr_fixup_test3_orig, size) == 0);
}

static void test_alternative_case_too_small(void)
{
	extern unsigned int ftr_fixup_test4[];
	extern unsigned int end_ftr_fixup_test4[];
	extern unsigned int ftr_fixup_test4_orig[];
	extern unsigned int ftr_fixup_test4_alt[];
	extern unsigned int ftr_fixup_test4_expected[];
	int size = 4 * (end_ftr_fixup_test4 - ftr_fixup_test4);
	unsigned long flag;

	/* Check a high-bit flag */
	flag = 1UL << ((sizeof(unsigned long) - 1) * 8);
	fixup.value = fixup.mask = flag;
	fixup.start_off = calc_offset(&fixup, ftr_fixup_test4 + 1);
	fixup.end_off = calc_offset(&fixup, ftr_fixup_test4 + 5);
	fixup.alt_start_off = calc_offset(&fixup, ftr_fixup_test4_alt);
	fixup.alt_end_off = calc_offset(&fixup, ftr_fixup_test4_alt + 2);

	/* Sanity check */
	check(memcmp(ftr_fixup_test4, ftr_fixup_test4_orig, size) == 0);

	/* Check we don't patch if the value matches */
	patch_feature_section(flag, &fixup);
	check(memcmp(ftr_fixup_test4, ftr_fixup_test4_orig, size) == 0);

	/* Check we do patch if the value doesn't match */
	patch_feature_section(0, &fixup);
	check(memcmp(ftr_fixup_test4, ftr_fixup_test4_expected, size) == 0);

	/* Check we do patch if the mask doesn't match */
	memcpy(ftr_fixup_test4, ftr_fixup_test4_orig, size);
	check(memcmp(ftr_fixup_test4, ftr_fixup_test4_orig, size) == 0);
	patch_feature_section(~flag, &fixup);
	check(memcmp(ftr_fixup_test4, ftr_fixup_test4_expected, size) == 0);
}

static void test_alternative_case_with_branch(void)
{
	extern unsigned int ftr_fixup_test5[];
	extern unsigned int end_ftr_fixup_test5[];
	extern unsigned int ftr_fixup_test5_expected[];
	int size = 4 * (end_ftr_fixup_test5 - ftr_fixup_test5);

	check(memcmp(ftr_fixup_test5, ftr_fixup_test5_expected, size) == 0);
}

static void test_alternative_case_with_external_branch(void)
{
	extern unsigned int ftr_fixup_test6[];
	extern unsigned int end_ftr_fixup_test6[];
	extern unsigned int ftr_fixup_test6_expected[];
	int size = 4 * (end_ftr_fixup_test6 - ftr_fixup_test6);

	check(memcmp(ftr_fixup_test6, ftr_fixup_test6_expected, size) == 0);
}

static void test_alternative_case_with_branch_to_end(void)
{
	extern unsigned int ftr_fixup_test7[];
	extern unsigned int end_ftr_fixup_test7[];
	extern unsigned int ftr_fixup_test7_expected[];
	int size = 4 * (end_ftr_fixup_test7 - ftr_fixup_test7);

	check(memcmp(ftr_fixup_test7, ftr_fixup_test7_expected, size) == 0);
}

static void test_cpu_macros(void)
{
	extern u8 ftr_fixup_test_FTR_macros[];
	extern u8 ftr_fixup_test_FTR_macros_expected[];
	unsigned long size = ftr_fixup_test_FTR_macros_expected -
			     ftr_fixup_test_FTR_macros;

	/* The fixups have already been done for us during boot */
	check(memcmp(ftr_fixup_test_FTR_macros,
		     ftr_fixup_test_FTR_macros_expected, size) == 0);
}

static void test_fw_macros(void)
{
#ifdef CONFIG_PPC64
	extern u8 ftr_fixup_test_FW_FTR_macros[];
	extern u8 ftr_fixup_test_FW_FTR_macros_expected[];
	unsigned long size = ftr_fixup_test_FW_FTR_macros_expected -
			     ftr_fixup_test_FW_FTR_macros;

	/* The fixups have already been done for us during boot */
	check(memcmp(ftr_fixup_test_FW_FTR_macros,
		     ftr_fixup_test_FW_FTR_macros_expected, size) == 0);
#endif
}

static void test_lwsync_macros(void)
{
	extern u8 lwsync_fixup_test[];
	extern u8 end_lwsync_fixup_test[];
	extern u8 lwsync_fixup_test_expected_LWSYNC[];
	extern u8 lwsync_fixup_test_expected_SYNC[];
	unsigned long size = end_lwsync_fixup_test -
			     lwsync_fixup_test;

	/* The fixups have already been done for us during boot */
	if (cur_cpu_spec->cpu_features & CPU_FTR_LWSYNC) {
		check(memcmp(lwsync_fixup_test,
			     lwsync_fixup_test_expected_LWSYNC, size) == 0);
	} else {
		check(memcmp(lwsync_fixup_test,
			     lwsync_fixup_test_expected_SYNC, size) == 0);
	}
}

#ifdef CONFIG_PPC64
static void __init test_prefix_patching(void)
{
	extern unsigned int ftr_fixup_prefix1[];
	extern unsigned int end_ftr_fixup_prefix1[];
	extern unsigned int ftr_fixup_prefix1_orig[];
	extern unsigned int ftr_fixup_prefix1_expected[];
	int size = sizeof(unsigned int) * (end_ftr_fixup_prefix1 - ftr_fixup_prefix1);

	fixup.value = fixup.mask = 8;
	fixup.start_off = calc_offset(&fixup, ftr_fixup_prefix1 + 1);
	fixup.end_off = calc_offset(&fixup, ftr_fixup_prefix1 + 3);
	fixup.alt_start_off = fixup.alt_end_off = 0;

	/* Sanity check */
	check(memcmp(ftr_fixup_prefix1, ftr_fixup_prefix1_orig, size) == 0);

	patch_feature_section(0, &fixup);
	check(memcmp(ftr_fixup_prefix1, ftr_fixup_prefix1_expected, size) == 0);
	check(memcmp(ftr_fixup_prefix1, ftr_fixup_prefix1_orig, size) != 0);
}

static void __init test_prefix_alt_patching(void)
{
	extern unsigned int ftr_fixup_prefix2[];
	extern unsigned int end_ftr_fixup_prefix2[];
	extern unsigned int ftr_fixup_prefix2_orig[];
	extern unsigned int ftr_fixup_prefix2_expected[];
	extern unsigned int ftr_fixup_prefix2_alt[];
	int size = sizeof(unsigned int) * (end_ftr_fixup_prefix2 - ftr_fixup_prefix2);

	fixup.value = fixup.mask = 8;
	fixup.start_off = calc_offset(&fixup, ftr_fixup_prefix2 + 1);
	fixup.end_off = calc_offset(&fixup, ftr_fixup_prefix2 + 3);
	fixup.alt_start_off = calc_offset(&fixup, ftr_fixup_prefix2_alt);
	fixup.alt_end_off = calc_offset(&fixup, ftr_fixup_prefix2_alt + 2);
	/* Sanity check */
	check(memcmp(ftr_fixup_prefix2, ftr_fixup_prefix2_orig, size) == 0);

	patch_feature_section(0, &fixup);
	check(memcmp(ftr_fixup_prefix2, ftr_fixup_prefix2_expected, size) == 0);
	check(memcmp(ftr_fixup_prefix2, ftr_fixup_prefix2_orig, size) != 0);
}

static void __init test_prefix_word_alt_patching(void)
{
	extern unsigned int ftr_fixup_prefix3[];
	extern unsigned int end_ftr_fixup_prefix3[];
	extern unsigned int ftr_fixup_prefix3_orig[];
	extern unsigned int ftr_fixup_prefix3_expected[];
	extern unsigned int ftr_fixup_prefix3_alt[];
	int size = sizeof(unsigned int) * (end_ftr_fixup_prefix3 - ftr_fixup_prefix3);

	fixup.value = fixup.mask = 8;
	fixup.start_off = calc_offset(&fixup, ftr_fixup_prefix3 + 1);
	fixup.end_off = calc_offset(&fixup, ftr_fixup_prefix3 + 4);
	fixup.alt_start_off = calc_offset(&fixup, ftr_fixup_prefix3_alt);
	fixup.alt_end_off = calc_offset(&fixup, ftr_fixup_prefix3_alt + 3);
	/* Sanity check */
	check(memcmp(ftr_fixup_prefix3, ftr_fixup_prefix3_orig, size) == 0);

	patch_feature_section(0, &fixup);
	check(memcmp(ftr_fixup_prefix3, ftr_fixup_prefix3_expected, size) == 0);
	patch_feature_section(0, &fixup);
	check(memcmp(ftr_fixup_prefix3, ftr_fixup_prefix3_orig, size) != 0);
}
#else
static inline void test_prefix_patching(void) {}
static inline void test_prefix_alt_patching(void) {}
static inline void test_prefix_word_alt_patching(void) {}
#endif /* CONFIG_PPC64 */

static int __init test_feature_fixups(void)
{
	printk(KERN_DEBUG "Running feature fixup self-tests ...\n");

	test_basic_patching();
	test_alternative_patching();
	test_alternative_case_too_big();
	test_alternative_case_too_small();
	test_alternative_case_with_branch();
	test_alternative_case_with_external_branch();
	test_alternative_case_with_branch_to_end();
	test_cpu_macros();
	test_fw_macros();
	test_lwsync_macros();
	test_prefix_patching();
	test_prefix_alt_patching();
	test_prefix_word_alt_patching();

	return 0;
}
late_initcall(test_feature_fixups);

#endif /* CONFIG_FTR_FIXUP_SELFTEST */
