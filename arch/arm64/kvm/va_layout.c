// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/kvm_host.h>
#include <linux/random.h>
#include <linux/memblock.h>
#include <asm/alternative.h>
#include <asm/debug-monitors.h>
#include <asm/insn.h>
#include <asm/kvm_mmu.h>
#include <asm/memory.h>

/*
 * The LSB of the HYP VA tag
 */
static u8 tag_lsb;
/*
 * The HYP VA tag value with the region bit
 */
static u64 tag_val;
static u64 va_mask;

/*
 * Compute HYP VA by using the same computation as kern_hyp_va().
 */
static u64 __early_kern_hyp_va(u64 addr)
{
	addr &= va_mask;
	addr |= tag_val << tag_lsb;
	return addr;
}

/*
 * Store a hyp VA <-> PA offset into a EL2-owned variable.
 */
static void init_hyp_physvirt_offset(void)
{
	u64 kern_va, hyp_va;

	/* Compute the offset from the hyp VA and PA of a random symbol. */
	kern_va = (u64)lm_alias(__hyp_text_start);
	hyp_va = __early_kern_hyp_va(kern_va);
	hyp_physvirt_offset = (s64)__pa(kern_va) - (s64)hyp_va;
}

/*
 * We want to generate a hyp VA with the following format (with V ==
 * vabits_actual):
 *
 *  63 ... V |     V-1    | V-2 .. tag_lsb | tag_lsb - 1 .. 0
 *  ---------------------------------------------------------
 * | 0000000 | hyp_va_msb |   random tag   |  kern linear VA |
 *           |--------- tag_val -----------|----- va_mask ---|
 *
 * which does not conflict with the idmap regions.
 */
__init void kvm_compute_layout(void)
{
	phys_addr_t idmap_addr = __pa_symbol(__hyp_idmap_text_start);
	u64 hyp_va_msb;

	/* Where is my RAM region? */
	hyp_va_msb  = idmap_addr & BIT(vabits_actual - 1);
	hyp_va_msb ^= BIT(vabits_actual - 1);

	tag_lsb = fls64((u64)phys_to_virt(memblock_start_of_DRAM()) ^
			(u64)(high_memory - 1));

	va_mask = GENMASK_ULL(tag_lsb - 1, 0);
	tag_val = hyp_va_msb;

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE) && tag_lsb != (vabits_actual - 1)) {
		/* We have some free bits to insert a random tag. */
		tag_val |= get_random_long() & GENMASK_ULL(vabits_actual - 2, tag_lsb);
	}
	tag_val >>= tag_lsb;

	init_hyp_physvirt_offset();
}

/*
 * The .hyp.reloc ELF section contains a list of kimg positions that
 * contains kimg VAs but will be accessed only in hyp execution context.
 * Convert them to hyp VAs. See gen-hyprel.c for more details.
 */
__init void kvm_apply_hyp_relocations(void)
{
	int32_t *rel;
	int32_t *begin = (int32_t *)__hyp_reloc_begin;
	int32_t *end = (int32_t *)__hyp_reloc_end;

	for (rel = begin; rel < end; ++rel) {
		uintptr_t *ptr, kimg_va;

		/*
		 * Each entry contains a 32-bit relative offset from itself
		 * to a kimg VA position.
		 */
		ptr = (uintptr_t *)lm_alias((char *)rel + *rel);

		/* Read the kimg VA value at the relocation address. */
		kimg_va = *ptr;

		/* Convert to hyp VA and store back to the relocation address. */
		*ptr = __early_kern_hyp_va((uintptr_t)lm_alias(kimg_va));
	}
}

static u32 compute_instruction(int n, u32 rd, u32 rn)
{
	u32 insn = AARCH64_BREAK_FAULT;

	switch (n) {
	case 0:
		insn = aarch64_insn_gen_logical_immediate(AARCH64_INSN_LOGIC_AND,
							  AARCH64_INSN_VARIANT_64BIT,
							  rn, rd, va_mask);
		break;

	case 1:
		/* ROR is a variant of EXTR with Rm = Rn */
		insn = aarch64_insn_gen_extr(AARCH64_INSN_VARIANT_64BIT,
					     rn, rn, rd,
					     tag_lsb);
		break;

	case 2:
		insn = aarch64_insn_gen_add_sub_imm(rd, rn,
						    tag_val & GENMASK(11, 0),
						    AARCH64_INSN_VARIANT_64BIT,
						    AARCH64_INSN_ADSB_ADD);
		break;

	case 3:
		insn = aarch64_insn_gen_add_sub_imm(rd, rn,
						    tag_val & GENMASK(23, 12),
						    AARCH64_INSN_VARIANT_64BIT,
						    AARCH64_INSN_ADSB_ADD);
		break;

	case 4:
		/* ROR is a variant of EXTR with Rm = Rn */
		insn = aarch64_insn_gen_extr(AARCH64_INSN_VARIANT_64BIT,
					     rn, rn, rd, 64 - tag_lsb);
		break;
	}

	return insn;
}

void __init kvm_update_va_mask(struct alt_instr *alt,
			       __le32 *origptr, __le32 *updptr, int nr_inst)
{
	int i;

	BUG_ON(nr_inst != 5);

	for (i = 0; i < nr_inst; i++) {
		u32 rd, rn, insn, oinsn;

		/*
		 * VHE doesn't need any address translation, let's NOP
		 * everything.
		 *
		 * Alternatively, if the tag is zero (because the layout
		 * dictates it and we don't have any spare bits in the
		 * address), NOP everything after masking the kernel VA.
		 */
		if (has_vhe() || (!tag_val && i > 0)) {
			updptr[i] = cpu_to_le32(aarch64_insn_gen_nop());
			continue;
		}

		oinsn = le32_to_cpu(origptr[i]);
		rd = aarch64_insn_decode_register(AARCH64_INSN_REGTYPE_RD, oinsn);
		rn = aarch64_insn_decode_register(AARCH64_INSN_REGTYPE_RN, oinsn);

		insn = compute_instruction(i, rd, rn);
		BUG_ON(insn == AARCH64_BREAK_FAULT);

		updptr[i] = cpu_to_le32(insn);
	}
}

void kvm_patch_vector_branch(struct alt_instr *alt,
			     __le32 *origptr, __le32 *updptr, int nr_inst)
{
	u64 addr;
	u32 insn;

	BUG_ON(nr_inst != 4);

	if (!cpus_have_const_cap(ARM64_SPECTRE_V3A) || WARN_ON_ONCE(has_vhe()))
		return;

	/*
	 * Compute HYP VA by using the same computation as kern_hyp_va()
	 */
	addr = __early_kern_hyp_va((u64)kvm_ksym_ref(__kvm_hyp_vector));

	/* Use PC[10:7] to branch to the same vector in KVM */
	addr |= ((u64)origptr & GENMASK_ULL(10, 7));

	/*
	 * Branch over the preamble in order to avoid the initial store on
	 * the stack (which we already perform in the hardening vectors).
	 */
	addr += KVM_VECTOR_PREAMBLE;

	/* movz x0, #(addr & 0xffff) */
	insn = aarch64_insn_gen_movewide(AARCH64_INSN_REG_0,
					 (u16)addr,
					 0,
					 AARCH64_INSN_VARIANT_64BIT,
					 AARCH64_INSN_MOVEWIDE_ZERO);
	*updptr++ = cpu_to_le32(insn);

	/* movk x0, #((addr >> 16) & 0xffff), lsl #16 */
	insn = aarch64_insn_gen_movewide(AARCH64_INSN_REG_0,
					 (u16)(addr >> 16),
					 16,
					 AARCH64_INSN_VARIANT_64BIT,
					 AARCH64_INSN_MOVEWIDE_KEEP);
	*updptr++ = cpu_to_le32(insn);

	/* movk x0, #((addr >> 32) & 0xffff), lsl #32 */
	insn = aarch64_insn_gen_movewide(AARCH64_INSN_REG_0,
					 (u16)(addr >> 32),
					 32,
					 AARCH64_INSN_VARIANT_64BIT,
					 AARCH64_INSN_MOVEWIDE_KEEP);
	*updptr++ = cpu_to_le32(insn);

	/* br x0 */
	insn = aarch64_insn_gen_branch_reg(AARCH64_INSN_REG_0,
					   AARCH64_INSN_BRANCH_NOLINK);
	*updptr++ = cpu_to_le32(insn);
}

static void generate_mov_q(u64 val, __le32 *origptr, __le32 *updptr, int nr_inst)
{
	u32 insn, oinsn, rd;

	BUG_ON(nr_inst != 4);

	/* Compute target register */
	oinsn = le32_to_cpu(*origptr);
	rd = aarch64_insn_decode_register(AARCH64_INSN_REGTYPE_RD, oinsn);

	/* movz rd, #(val & 0xffff) */
	insn = aarch64_insn_gen_movewide(rd,
					 (u16)val,
					 0,
					 AARCH64_INSN_VARIANT_64BIT,
					 AARCH64_INSN_MOVEWIDE_ZERO);
	*updptr++ = cpu_to_le32(insn);

	/* movk rd, #((val >> 16) & 0xffff), lsl #16 */
	insn = aarch64_insn_gen_movewide(rd,
					 (u16)(val >> 16),
					 16,
					 AARCH64_INSN_VARIANT_64BIT,
					 AARCH64_INSN_MOVEWIDE_KEEP);
	*updptr++ = cpu_to_le32(insn);

	/* movk rd, #((val >> 32) & 0xffff), lsl #32 */
	insn = aarch64_insn_gen_movewide(rd,
					 (u16)(val >> 32),
					 32,
					 AARCH64_INSN_VARIANT_64BIT,
					 AARCH64_INSN_MOVEWIDE_KEEP);
	*updptr++ = cpu_to_le32(insn);

	/* movk rd, #((val >> 48) & 0xffff), lsl #48 */
	insn = aarch64_insn_gen_movewide(rd,
					 (u16)(val >> 48),
					 48,
					 AARCH64_INSN_VARIANT_64BIT,
					 AARCH64_INSN_MOVEWIDE_KEEP);
	*updptr++ = cpu_to_le32(insn);
}

void kvm_get_kimage_voffset(struct alt_instr *alt,
			    __le32 *origptr, __le32 *updptr, int nr_inst)
{
	generate_mov_q(kimage_voffset, origptr, updptr, nr_inst);
}
