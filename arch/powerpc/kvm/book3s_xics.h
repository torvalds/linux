/*
 * Copyright 2012 Michael Ellerman, IBM Corporation.
 * Copyright 2012 Benjamin Herrenschmidt, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 */

#ifndef _KVM_PPC_BOOK3S_XICS_H
#define _KVM_PPC_BOOK3S_XICS_H

/*
 * We use a two-level tree to store interrupt source information.
 * There are up to 1024 ICS nodes, each of which can represent
 * 1024 sources.
 */
#define KVMPPC_XICS_MAX_ICS_ID	1023
#define KVMPPC_XICS_ICS_SHIFT	10
#define KVMPPC_XICS_IRQ_PER_ICS	(1 << KVMPPC_XICS_ICS_SHIFT)
#define KVMPPC_XICS_SRC_MASK	(KVMPPC_XICS_IRQ_PER_ICS - 1)

/*
 * Interrupt source numbers below this are reserved, for example
 * 0 is "no interrupt", and 2 is used for IPIs.
 */
#define KVMPPC_XICS_FIRST_IRQ	16
#define KVMPPC_XICS_NR_IRQS	((KVMPPC_XICS_MAX_ICS_ID + 1) * \
				 KVMPPC_XICS_IRQ_PER_ICS)

/* Priority value to use for disabling an interrupt */
#define MASKED	0xff

/* State for one irq source */
struct ics_irq_state {
	u32 number;
	u32 server;
	u8  priority;
	u8  saved_priority;
	u8  resend;
	u8  masked_pending;
	u8  lsi;		/* level-sensitive interrupt */
	u8  asserted; /* Only for LSI */
	u8  exists;
};

/* Atomic ICP state, updated with a single compare & swap */
union kvmppc_icp_state {
	unsigned long raw;
	struct {
		u8 out_ee:1;
		u8 need_resend:1;
		u8 cppr;
		u8 mfrr;
		u8 pending_pri;
		u32 xisr;
	};
};

/* One bit per ICS */
#define ICP_RESEND_MAP_SIZE	(KVMPPC_XICS_MAX_ICS_ID / BITS_PER_LONG + 1)

struct kvmppc_icp {
	struct kvm_vcpu *vcpu;
	unsigned long server_num;
	union kvmppc_icp_state state;
	unsigned long resend_map[ICP_RESEND_MAP_SIZE];

	/* Real mode might find something too hard, here's the action
	 * it might request from virtual mode
	 */
#define XICS_RM_KICK_VCPU	0x1
#define XICS_RM_CHECK_RESEND	0x2
#define XICS_RM_REJECT		0x4
#define XICS_RM_NOTIFY_EOI	0x8
	u32 rm_action;
	struct kvm_vcpu *rm_kick_target;
	struct kvmppc_icp *rm_resend_icp;
	u32  rm_reject;
	u32  rm_eoied_irq;

	/* Counters for each reason we exited real mode */
	unsigned long n_rm_kick_vcpu;
	unsigned long n_rm_check_resend;
	unsigned long n_rm_reject;
	unsigned long n_rm_notify_eoi;
	/* Counters for handling ICP processing in real mode */
	unsigned long n_check_resend;
	unsigned long n_reject;

	/* Debug stuff for real mode */
	union kvmppc_icp_state rm_dbgstate;
	struct kvm_vcpu *rm_dbgtgt;
};

struct kvmppc_ics {
	arch_spinlock_t lock;
	u16 icsid;
	struct ics_irq_state irq_state[KVMPPC_XICS_IRQ_PER_ICS];
};

struct kvmppc_xics {
	struct kvm *kvm;
	struct kvm_device *dev;
	struct dentry *dentry;
	u32 max_icsid;
	bool real_mode;
	bool real_mode_dbg;
	u32 err_noics;
	u32 err_noicp;
	struct kvmppc_ics *ics[KVMPPC_XICS_MAX_ICS_ID + 1];
};

static inline struct kvmppc_icp *kvmppc_xics_find_server(struct kvm *kvm,
							 u32 nr)
{
	struct kvm_vcpu *vcpu = NULL;
	int i;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (vcpu->arch.icp && nr == vcpu->arch.icp->server_num)
			return vcpu->arch.icp;
	}
	return NULL;
}

static inline struct kvmppc_ics *kvmppc_xics_find_ics(struct kvmppc_xics *xics,
						      u32 irq, u16 *source)
{
	u32 icsid = irq >> KVMPPC_XICS_ICS_SHIFT;
	u16 src = irq & KVMPPC_XICS_SRC_MASK;
	struct kvmppc_ics *ics;

	if (source)
		*source = src;
	if (icsid > KVMPPC_XICS_MAX_ICS_ID)
		return NULL;
	ics = xics->ics[icsid];
	if (!ics)
		return NULL;
	return ics;
}


#endif /* _KVM_PPC_BOOK3S_XICS_H */
