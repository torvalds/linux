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
	u64 val;

	val = read_sysreg(VTCR) & ~VTCR_MASK;

	val |= read_sysreg(HTCR) & VTCR_HTCR_SH;
	val |= KVM_VTCR_SL0 | KVM_VTCR_T0SZ | KVM_VTCR_S;

	write_sysreg(val, VTCR);
}
