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
#include <asm/cpufeature.h>

u32 __hyp_text __init_stage2_translation(void)
{
	u64 val = VTCR_EL2_FLAGS;
	u64 parange;
	u32 phys_shift;
	u64 tmp;

	/*
	 * Read the PARange bits from ID_AA64MMFR0_EL1 and set the PS
	 * bits in VTCR_EL2. Amusingly, the PARange is 4 bits, but the
	 * allocated values are limited to 3bits.
	 */
	parange = read_sysreg(id_aa64mmfr0_el1) & 7;
	if (parange > ID_AA64MMFR0_PARANGE_MAX)
		parange = ID_AA64MMFR0_PARANGE_MAX;
	val |= parange << VTCR_EL2_PS_SHIFT;

	/* Compute the actual PARange... */
	phys_shift = id_aa64mmfr0_parange_to_phys_shift(parange);

	/*
	 * ... and clamp it to 40 bits, unless we have some braindead
	 * HW that implements less than that. In all cases, we'll
	 * return that value for the rest of the kernel to decide what
	 * to do.
	 */
	val |= VTCR_EL2_T0SZ(phys_shift > 40 ? 40 : phys_shift);

	/*
	 * Check the availability of Hardware Access Flag / Dirty Bit
	 * Management in ID_AA64MMFR1_EL1 and enable the feature in VTCR_EL2.
	 */
	tmp = (read_sysreg(id_aa64mmfr1_el1) >> ID_AA64MMFR1_HADBS_SHIFT) & 0xf;
	if (tmp)
		val |= VTCR_EL2_HA;

	/*
	 * Read the VMIDBits bits from ID_AA64MMFR1_EL1 and set the VS
	 * bit in VTCR_EL2.
	 */
	tmp = (read_sysreg(id_aa64mmfr1_el1) >> ID_AA64MMFR1_VMIDBITS_SHIFT) & 0xf;
	val |= (tmp == ID_AA64MMFR1_VMIDBITS_16) ?
			VTCR_EL2_VS_16BIT :
			VTCR_EL2_VS_8BIT;

	write_sysreg(val, vtcr_el2);

	return phys_shift;
}
