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

/*
 * The LSB of the random hyp VA tag or 0 if no randomization is used.
 */
static u8 tag_lsb;
/*
 * The random hyp VA tag value with the region bit if hyp randomization is used
 */
static u64 tag_val;
static u64 va_mask;

static void compute_layout(void)
{
	phys_addr_t idmap_addr = __pa_symbol(__hyp_idmap_text_start);
	u64 hyp_va_msb;
	int kva_msb;

	/* Where is my RAM region? */
	hyp_va_msb  = idmap_addr & BIT(VA_BITS - 1);
	hyp_va_msb ^= BIT(VA_BITS - 1);

	kva_msb = fls64((u64)phys_to_virt(memblock_start_of_DRAM()) ^
			(u64)(high_memory - 1));

	if (kva_msb == (VA_BITS - 1)) {
		/*
		 * No space in the address, let's compute the mask so
		 * that it covers (VA_BITS - 1) bits, and the region
		 * bit. The tag stays set to zero.
		 */
		va_mask  = BIT(VA_BITS - 1) - 1;
		va_mask |= hyp_va_msb;
	} else {
		/*
		 * We do have some free bits to insert a random tag.
		 * Hyp VAs are now created from kernel linear map VAs
		 * using the following formula (with V == VA_BITS):
		 *
		 *  63 ... V |     V-1    | V-2 .. tag_lsb | tag_lsb - 1 .. 0
		 *  ---------------------------------------------------------
		 * | 0000000 | hyp_va_msb |    random tag  |  kern linear VA |
		 */
		tag_lsb = kva_msb;
		va_mask = GENMASK_ULL(tag_lsb - 1, 0);
		tag_val = get_random_long() & GENMASK_ULL(VA_BITS - 2, tag_lsb);
		tag_val |= hyp_va_msb;
		tag_val >>= tag_lsb;
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

	if (!has_vhe() && !va_mask)
		compute_layout();

	for (i = 0; i < nr_inst; i++) {
		u32 rd, rn, insn, oinsn;

		/*
		 * VHE doesn't need any address translation, let's NOP
		 * everything.
		 *
		 * Alternatively, if we don't have any spare bits in
		 * the address, NOP everything after masking that
		 * kernel VA.
		 */
		if (has_vhe() || (!tag_lsb && i > 0)) {
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

void *__kvm_bp_vect_base;
int __kvm_harden_el2_vector_slot;

void kvm_patch_vector_branch(struct alt_instr *alt,
			     __le32 *origptr, __le32 *updptr, int nr_inst)
{
	u64 addr;
	u32 insn;

	BUG_ON(nr_inst != 5);

	if (has_vhe() || !cpus_have_const_cap(ARM64_HARDEN_EL2_VECTORS)) {
		WARN_ON_ONCE(cpus_have_const_cap(ARM64_HARDEN_EL2_VECTORS));
		return;
	}

	if (!va_mask)
		compute_layout();

	/*
	 * Compute HYP VA by using the same computation as kern_hyp_va()
	 */
	addr = (uintptr_t)kvm_ksym_ref(__kvm_hyp_vector);
	addr &= va_mask;
	addr |= tag_val << tag_lsb;

	/* Use PC[10:7] to branch to the same vector in KVM */
	addr |= ((u64)origptr & GENMASK_ULL(10, 7));

	/*
	 * Branch to the second instruction in the vectors in order to
	 * avoid the initial store on the stack (which we already
	 * perform in the hardening vectors).
	 */
	addr += AARCH64_INSN_SIZE;

	/* stp x0, x1, [sp, #-16]! */
	insn = aarch64_insn_gen_load_store_pair(AARCH64_INSN_REG_0,
						AARCH64_INSN_REG_1,
						AARCH64_INSN_REG_SP,
						-16,
						AARCH64_INSN_VARIANT_64BIT,
						AARCH64_INSN_LDST_STORE_PAIR_PRE_INDEX);
	*updptr++ = cpu_to_le32(insn);

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
