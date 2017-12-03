/*
 * Copyright (C) 2017 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kvm_host.h>
#include <asm/alternative.h>
#include <asm/debug-monitors.h>
#include <asm/insn.h>
#include <asm/kvm_mmu.h>

#define HYP_PAGE_OFFSET_HIGH_MASK	((UL(1) << VA_BITS) - 1)
#define HYP_PAGE_OFFSET_LOW_MASK	((UL(1) << (VA_BITS - 1)) - 1)

static u64 va_mask;

static void compute_layout(void)
{
	phys_addr_t idmap_addr = __pa_symbol(__hyp_idmap_text_start);
	unsigned long mask = HYP_PAGE_OFFSET_HIGH_MASK;

	/*
	 * Activate the lower HYP offset only if the idmap doesn't
	 * clash with it,
	 */
	if (idmap_addr > HYP_PAGE_OFFSET_LOW_MASK)
		mask = HYP_PAGE_OFFSET_LOW_MASK;

	va_mask = mask;
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
	}

	return insn;
}

void __init kvm_update_va_mask(struct alt_instr *alt,
			       __le32 *origptr, __le32 *updptr, int nr_inst)
{
	int i;

	/* We only expect a single instruction in the alternative sequence */
	BUG_ON(nr_inst != 1);

	if (!has_vhe() && !va_mask)
		compute_layout();

	for (i = 0; i < nr_inst; i++) {
		u32 rd, rn, insn, oinsn;

		/*
		 * VHE doesn't need any address translation, let's NOP
		 * everything.
		 */
		if (has_vhe()) {
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
