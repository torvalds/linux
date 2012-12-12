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
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef _UAPI__POWERPC_KVM_PARA_H__
#define _UAPI__POWERPC_KVM_PARA_H__

#include <linux/types.h>

/*
 * Additions to this struct must only occur at the end, and should be
 * accompanied by a KVM_MAGIC_FEAT flag to advertise that they are present
 * (albeit not necessarily relevant to the current target hardware platform).
 *
 * Struct fields are always 32 or 64 bit aligned, depending on them being 32
 * or 64 bit wide respectively.
 *
 * See Documentation/virtual/kvm/ppc-pv.txt
 */
struct kvm_vcpu_arch_shared {
	__u64 scratch1;
	__u64 scratch2;
	__u64 scratch3;
	__u64 critical;		/* Guest may not get interrupts if == r1 */
	__u64 sprg0;
	__u64 sprg1;
	__u64 sprg2;
	__u64 sprg3;
	__u64 srr0;
	__u64 srr1;
	__u64 dar;		/* dear on BookE */
	__u64 msr;
	__u32 dsisr;
	__u32 int_pending;	/* Tells the guest if we have an interrupt */
	__u32 sr[16];
	__u32 mas0;
	__u32 mas1;
	__u64 mas7_3;
	__u64 mas2;
	__u32 mas4;
	__u32 mas6;
	__u32 esr;
	__u32 pir;

	/*
	 * SPRG4-7 are user-readable, so we can only keep these consistent
	 * between the shared area and the real registers when there's an
	 * intervening exit to KVM.  This also applies to SPRG3 on some
	 * chips.
	 *
	 * This suffices for access by guest userspace, since in PR-mode
	 * KVM, an exit must occur when changing the guest's MSR[PR].
	 * If the guest kernel writes to SPRG3-7 via the shared area, it
	 * must also use the shared area for reading while in kernel space.
	 */
	__u64 sprg4;
	__u64 sprg5;
	__u64 sprg6;
	__u64 sprg7;
};

#define KVM_SC_MAGIC_R0		0x4b564d21 /* "KVM!" */
#define HC_VENDOR_KVM		(42 << 16)
#define HC_EV_SUCCESS		0
#define HC_EV_UNIMPLEMENTED	12

#define KVM_FEATURE_MAGIC_PAGE	1

#define KVM_MAGIC_FEAT_SR		(1 << 0)

/* MASn, ESR, PIR, and high SPRGs */
#define KVM_MAGIC_FEAT_MAS0_TO_SPRG7	(1 << 1)


#endif /* _UAPI__POWERPC_KVM_PARA_H__ */
