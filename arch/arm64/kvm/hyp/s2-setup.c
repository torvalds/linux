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

u32 __hyp_text __init_stage2_translation(void)
{
	u64 val = VTCR_EL2_FLAGS;
	u64 parange;
	u64 tmp;

	/*
	 * Read the PARange bits from ID_AA64MMFR0_EL1 and set the PS
	 * bits in VTCR_EL2. Amusingly, the PARange is 4 bits, while
	 * PS is only 3. Fortunately, bit 19 is RES0 in VTCR_EL2...
	 */
	parange = read_sysreg(id_aa64mmfr0_el1) & 7;
	val |= parange << 16;

	/* Compute the actual PARange... */
	switch (parange) {
	case 0:
		parange = 32;
		break;
	case 1:
		parange = 36;
		break;
	case 2:
		parange = 40;
		break;
	case 3:
		parange = 42;
		break;
	case 4:
		parange = 44;
		break;
	case 5:
	default:
		parange = 48;
		break;
	}

	/*
	 * ... and clamp it to 40 bits, unless we have some braindead
	 * HW that implements less than that. In all cases, we'll
	 * return that value for the rest of the kernel to decide what
	 * to do.
	 */
	val |= 64 - (parange > 40 ? 40 : parange);

	/*
	 * Check the availability of Hardware Access Flag / Dirty Bit
	 * Management in ID_AA64MMFR1_EL1 and enable the feature in VTCR_EL2.
	 */
	tmp = (read_sysreg(id_aa64mmfr1_el1) >> ID_AA64MMFR1_HADBS_SHIFT) & 0xf;
	if (IS_ENABLED(CONFIG_ARM64_HW_AFDBM) && tmp)
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

	return parange;
}
