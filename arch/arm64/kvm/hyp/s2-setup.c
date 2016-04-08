/*
 * Copyright (C) 2016 - ARM Ltd
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

#include <linux/types.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>

void __hyp_text __init_stage2_translation(void)
{
	u64 val = VTCR_EL2_FLAGS;
	u64 tmp;

	/*
	 * Read the PARange bits from ID_AA64MMFR0_EL1 and set the PS
	 * bits in VTCR_EL2. Amusingly, the PARange is 4 bits, while
	 * PS is only 3. Fortunately, bit 19 is RES0 in VTCR_EL2...
	 */
	val |= (read_sysreg(id_aa64mmfr0_el1) & 7) << 16;

	/*
	 * Read the VMIDBits bits from ID_AA64MMFR1_EL1 and set the VS
	 * bit in VTCR_EL2.
	 */
	tmp = (read_sysreg(id_aa64mmfr1_el1) >> 4) & 0xf;
	val |= (tmp == 2) ? VTCR_EL2_VS : 0;

	write_sysreg(val, vtcr_el2);
}
