/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2017 Benjamin Herrenschmidt, IBM Corporation
 */

#ifndef _KVM_PPC_BOOK3S_XIVE_H
#define _KVM_PPC_BOOK3S_XIVE_H

#ifdef CONFIG_KVM_XICS
#include "book3s_xics.h"

/*
 * The XIVE Interrupt source numbers are within the range 0 to
 * KVMPPC_XICS_NR_IRQS.
 */
#define KVMPPC_XIVE_FIRST_IRQ	0
#define KVMPPC_XIVE_NR_IRQS	KVMPPC_XICS_NR_IRQS

/*
 * State for one guest irq source.
 *
 * For each guest source we allocate a HW interrupt in the XIVE
 * which we use for all SW triggers. It will be unused for
 * pass-through but it's easier to keep around as the same
 * guest interrupt can alternatively be emulated or pass-through
 * if a physical device is hot unplugged and replaced with an
 * emulated one.
 *
 * This state structure is very similar to the XICS one with
 * additional XIVE specific tracking.
 */
struct kvmppc_xive_irq_state {
	bool valid;			/* Interrupt entry is valid */

	u32 number;			/* Guest IRQ number */
	u32 ipi_number;			/* XIVE IPI HW number */
	struct xive_irq_data ipi_data;	/* XIVE IPI associated data */
	u32 pt_number;			/* XIVE Pass-through number if any */
	struct xive_irq_data *pt_data;	/* XIVE Pass-through associated data */

	/* Targetting as set by guest */
	u8 guest_priority;		/* Guest set priority */
	u8 saved_priority;		/* Saved priority when masking */

	/* Actual targetting */
	u32 act_server;			/* Actual server */
	u8 act_priority;		/* Actual priority */

	/* Various state bits */
	bool in_eoi;			/* Synchronize with H_EOI */
	bool old_p;			/* P bit state when masking */
	bool old_q;			/* Q bit state when masking */
	bool lsi;			/* level-sensitive interrupt */
	bool asserted;			/* Only for emulated LSI: current state */

	/* Saved for migration state */
	bool in_queue;
	bool saved_p;
	bool saved_q;
	u8 saved_scan_prio;

	/* Xive native */
	u32 eisn;			/* Guest Effective IRQ number */
};

/* Select the "right" interrupt (IPI vs. passthrough) */
static inline void kvmppc_xive_select_irq(struct kvmppc_xive_irq_state *state,
					  u32 *out_hw_irq,
					  struct xive_irq_data **out_xd)
{
	if (state->pt_number) {
		if (out_hw_irq)
			*out_hw_irq = state->pt_number;
		if (out_xd)
			*out_xd = state->pt_data;
	} else {
		if (out_hw_irq)
			*out_hw_irq = state->ipi_number;
		if (out_xd)
			*out_xd = &state->ipi_data;
	}
}

/*
 * This corresponds to an "ICS" in XICS terminology, we use it
 * as a mean to break up source information into multiple structures.
 */
struct kvmppc_xive_src_block {
	arch_spinlock_t lock;
	u16 id;
	struct kvmppc_xive_irq_state irq_state[KVMPPC_XICS_IRQ_PER_ICS];
};

struct kvmppc_xive;

struct kvmppc_xive_ops {
	int (*reset_mapped)(struct kvm *kvm, unsigned long guest_irq);
};

struct kvmppc_xive {
	struct kvm *kvm;
	struct kvm_device *dev;
	struct dentry *dentry;

	/* VP block associated with the VM */
	u32	vp_base;

	/* Blocks of sources */
	struct kvmppc_xive_src_block *src_blocks[KVMPPC_XICS_MAX_ICS_ID + 1];
	u32	max_sbid;

	/*
	 * For state save, we lazily scan the queues on the first interrupt
	 * being migrated. We don't have a clean way to reset that flags
	 * so we keep track of the number of valid sources and how many of
	 * them were migrated so we can reset when all of them have been
	 * processed.
	 */
	u32	src_count;
	u32	saved_src_count;

	/*
	 * Some irqs are delayed on restore until the source is created,
	 * keep track here of how many of them
	 */
	u32	delayed_irqs;

	/* Which queues (priorities) are in use by the guest */
	u8	qmap;

	/* Queue orders */
	u32	q_order;
	u32	q_page_order;

	/* Flags */
	u8	single_escalation;

	struct kvmppc_xive_ops *ops;
	struct address_space   *mapping;
	struct mutex mapping_lock;
	struct mutex lock;
};

#define KVMPPC_XIVE_Q_COUNT	8

struct kvmppc_xive_vcpu {
	struct kvmppc_xive	*xive;
	struct kvm_vcpu		*vcpu;
	bool			valid;

	/* Server number. This is the HW CPU ID from a guest perspective */
	u32			server_num;

	/*
	 * HW VP corresponding to this VCPU. This is the base of the VP
	 * block plus the server number.
	 */
	u32			vp_id;
	u32			vp_chip_id;
	u32			vp_cam;

	/* IPI used for sending ... IPIs */
	u32			vp_ipi;
	struct xive_irq_data	vp_ipi_data;

	/* Local emulation state */
	uint8_t			cppr;	/* guest CPPR */
	uint8_t			hw_cppr;/* Hardware CPPR */
	uint8_t			mfrr;
	uint8_t			pending;

	/* Each VP has 8 queues though we only provision some */
	struct xive_q		queues[KVMPPC_XIVE_Q_COUNT];
	u32			esc_virq[KVMPPC_XIVE_Q_COUNT];
	char			*esc_virq_names[KVMPPC_XIVE_Q_COUNT];

	/* Stash a delayed irq on restore from migration (see set_icp) */
	u32			delayed_irq;

	/* Stats */
	u64			stat_rm_h_xirr;
	u64			stat_rm_h_ipoll;
	u64			stat_rm_h_cppr;
	u64			stat_rm_h_eoi;
	u64			stat_rm_h_ipi;
	u64			stat_vm_h_xirr;
	u64			stat_vm_h_ipoll;
	u64			stat_vm_h_cppr;
	u64			stat_vm_h_eoi;
	u64			stat_vm_h_ipi;
};

static inline struct kvm_vcpu *kvmppc_xive_find_server(struct kvm *kvm, u32 nr)
{
	struct kvm_vcpu *vcpu = NULL;
	int i;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (vcpu->arch.xive_vcpu && nr == vcpu->arch.xive_vcpu->server_num)
			return vcpu;
	}
	return NULL;
}

static inline struct kvmppc_xive_src_block *kvmppc_xive_find_source(struct kvmppc_xive *xive,
		u32 irq, u16 *source)
{
	u32 bid = irq >> KVMPPC_XICS_ICS_SHIFT;
	u16 src = irq & KVMPPC_XICS_SRC_MASK;

	if (source)
		*source = src;
	if (bid > KVMPPC_XICS_MAX_ICS_ID)
		return NULL;
	return xive->src_blocks[bid];
}

static inline u32 kvmppc_xive_vp(struct kvmppc_xive *xive, u32 server)
{
	return xive->vp_base + kvmppc_pack_vcpu_id(xive->kvm, server);
}

/*
 * Mapping between guest priorities and host priorities
 * is as follow.
 *
 * Guest request for 0...6 are honored. Guest request for anything
 * higher results in a priority of 6 being applied.
 *
 * Similar mapping is done for CPPR values
 */
static inline u8 xive_prio_from_guest(u8 prio)
{
	if (prio == 0xff || prio < 6)
		return prio;
	return 6;
}

static inline u8 xive_prio_to_guest(u8 prio)
{
	return prio;
}

static inline u32 __xive_read_eq(__be32 *qpage, u32 msk, u32 *idx, u32 *toggle)
{
	u32 cur;

	if (!qpage)
		return 0;
	cur = be32_to_cpup(qpage + *idx);
	if ((cur >> 31) == *toggle)
		return 0;
	*idx = (*idx + 1) & msk;
	if (*idx == 0)
		(*toggle) ^= 1;
	return cur & 0x7fffffff;
}

extern unsigned long xive_rm_h_xirr(struct kvm_vcpu *vcpu);
extern unsigned long xive_rm_h_ipoll(struct kvm_vcpu *vcpu, unsigned long server);
extern int xive_rm_h_ipi(struct kvm_vcpu *vcpu, unsigned long server,
			 unsigned long mfrr);
extern int xive_rm_h_cppr(struct kvm_vcpu *vcpu, unsigned long cppr);
extern int xive_rm_h_eoi(struct kvm_vcpu *vcpu, unsigned long xirr);

extern unsigned long (*__xive_vm_h_xirr)(struct kvm_vcpu *vcpu);
extern unsigned long (*__xive_vm_h_ipoll)(struct kvm_vcpu *vcpu, unsigned long server);
extern int (*__xive_vm_h_ipi)(struct kvm_vcpu *vcpu, unsigned long server,
			      unsigned long mfrr);
extern int (*__xive_vm_h_cppr)(struct kvm_vcpu *vcpu, unsigned long cppr);
extern int (*__xive_vm_h_eoi)(struct kvm_vcpu *vcpu, unsigned long xirr);

/*
 * Common Xive routines for XICS-over-XIVE and XIVE native
 */
void kvmppc_xive_disable_vcpu_interrupts(struct kvm_vcpu *vcpu);
int kvmppc_xive_debug_show_queues(struct seq_file *m, struct kvm_vcpu *vcpu);
struct kvmppc_xive_src_block *kvmppc_xive_create_src_block(
	struct kvmppc_xive *xive, int irq);
void kvmppc_xive_free_sources(struct kvmppc_xive_src_block *sb);
int kvmppc_xive_select_target(struct kvm *kvm, u32 *server, u8 prio);
int kvmppc_xive_attach_escalation(struct kvm_vcpu *vcpu, u8 prio,
				  bool single_escalation);
struct kvmppc_xive *kvmppc_xive_get_device(struct kvm *kvm, u32 type);

#endif /* CONFIG_KVM_XICS */
#endif /* _KVM_PPC_BOOK3S_XICS_H */
