/*
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
 *
 * Copyright SUSE Linux Products GmbH 2010
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#ifndef __ASM_KVM_BOOK3S_64_H__
#define __ASM_KVM_BOOK3S_64_H__

#ifdef CONFIG_KVM_BOOK3S_PR
static inline struct kvmppc_book3s_shadow_vcpu *to_svcpu(struct kvm_vcpu *vcpu)
{
	return &get_paca()->shadow_vcpu;
}
#endif

#define SPAPR_TCE_SHIFT		12

static inline unsigned long compute_tlbie_rb(unsigned long v, unsigned long r,
					     unsigned long pte_index)
{
	unsigned long rb, va_low;

	rb = (v & ~0x7fUL) << 16;		/* AVA field */
	va_low = pte_index >> 3;
	if (v & HPTE_V_SECONDARY)
		va_low = ~va_low;
	/* xor vsid from AVA */
	if (!(v & HPTE_V_1TB_SEG))
		va_low ^= v >> 12;
	else
		va_low ^= v >> 24;
	va_low &= 0x7ff;
	if (v & HPTE_V_LARGE) {
		rb |= 1;			/* L field */
		if (cpu_has_feature(CPU_FTR_ARCH_206) &&
		    (r & 0xff000)) {
			/* non-16MB large page, must be 64k */
			/* (masks depend on page size) */
			rb |= 0x1000;		/* page encoding in LP field */
			rb |= (va_low & 0x7f) << 16; /* 7b of VA in AVA/LP field */
			rb |= (va_low & 0xfe);	/* AVAL field (P7 doesn't seem to care) */
		}
	} else {
		/* 4kB page */
		rb |= (va_low & 0x7ff) << 12;	/* remaining 11b of VA */
	}
	rb |= (v >> 54) & 0x300;		/* B field */
	return rb;
}

#endif /* __ASM_KVM_BOOK3S_64_H__ */
