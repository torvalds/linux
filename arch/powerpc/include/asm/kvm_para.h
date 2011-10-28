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

#ifndef __POWERPC_KVM_PARA_H__
#define __POWERPC_KVM_PARA_H__

#include <linux/types.h>

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
	__u64 dar;
	__u64 msr;
	__u32 dsisr;
	__u32 int_pending;	/* Tells the guest if we have an interrupt */
	__u32 sr[16];
};

#define KVM_SC_MAGIC_R0		0x4b564d21 /* "KVM!" */
#define HC_VENDOR_KVM		(42 << 16)
#define HC_EV_SUCCESS		0
#define HC_EV_UNIMPLEMENTED	12

#define KVM_FEATURE_MAGIC_PAGE	1

#define KVM_MAGIC_FEAT_SR	(1 << 0)

#ifdef __KERNEL__

#ifdef CONFIG_KVM_GUEST

#include <linux/of.h>

static inline int kvm_para_available(void)
{
	struct device_node *hyper_node;

	hyper_node = of_find_node_by_path("/hypervisor");
	if (!hyper_node)
		return 0;

	if (!of_device_is_compatible(hyper_node, "linux,kvm"))
		return 0;

	return 1;
}

extern unsigned long kvm_hypercall(unsigned long *in,
				   unsigned long *out,
				   unsigned long nr);

#else

static inline int kvm_para_available(void)
{
	return 0;
}

static unsigned long kvm_hypercall(unsigned long *in,
				   unsigned long *out,
				   unsigned long nr)
{
	return HC_EV_UNIMPLEMENTED;
}

#endif

static inline long kvm_hypercall0_1(unsigned int nr, unsigned long *r2)
{
	unsigned long in[8];
	unsigned long out[8];
	unsigned long r;

	r = kvm_hypercall(in, out, nr | HC_VENDOR_KVM);
	*r2 = out[0];

	return r;
}

static inline long kvm_hypercall0(unsigned int nr)
{
	unsigned long in[8];
	unsigned long out[8];

	return kvm_hypercall(in, out, nr | HC_VENDOR_KVM);
}

static inline long kvm_hypercall1(unsigned int nr, unsigned long p1)
{
	unsigned long in[8];
	unsigned long out[8];

	in[0] = p1;
	return kvm_hypercall(in, out, nr | HC_VENDOR_KVM);
}

static inline long kvm_hypercall2(unsigned int nr, unsigned long p1,
				  unsigned long p2)
{
	unsigned long in[8];
	unsigned long out[8];

	in[0] = p1;
	in[1] = p2;
	return kvm_hypercall(in, out, nr | HC_VENDOR_KVM);
}

static inline long kvm_hypercall3(unsigned int nr, unsigned long p1,
				  unsigned long p2, unsigned long p3)
{
	unsigned long in[8];
	unsigned long out[8];

	in[0] = p1;
	in[1] = p2;
	in[2] = p3;
	return kvm_hypercall(in, out, nr | HC_VENDOR_KVM);
}

static inline long kvm_hypercall4(unsigned int nr, unsigned long p1,
				  unsigned long p2, unsigned long p3,
				  unsigned long p4)
{
	unsigned long in[8];
	unsigned long out[8];

	in[0] = p1;
	in[1] = p2;
	in[2] = p3;
	in[3] = p4;
	return kvm_hypercall(in, out, nr | HC_VENDOR_KVM);
}


static inline unsigned int kvm_arch_para_features(void)
{
	unsigned long r;

	if (!kvm_para_available())
		return 0;

	if(kvm_hypercall0_1(KVM_HC_FEATURES, &r))
		return 0;

	return r;
}

#endif /* __KERNEL__ */

#endif /* __POWERPC_KVM_PARA_H__ */
