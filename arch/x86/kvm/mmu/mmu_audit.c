// SPDX-License-Identifier: GPL-2.0-only
/*
 * mmu_audit.c:
 *
 * Audit code for KVM MMU
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright 2010 Red Hat, Inc. and/or its affiliates.
 *
 * Authors:
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Avi Kivity   <avi@qumranet.com>
 *   Marcelo Tosatti <mtosatti@redhat.com>
 *   Xiao Guangrong <xiaoguangrong@cn.fujitsu.com>
 */

#include <linux/ratelimit.h>

static char const *audit_point_name[] = {
	"pre page fault",
	"post page fault",
	"pre pte write",
	"post pte write",
	"pre sync",
	"post sync"
};

#define audit_printk(kvm, fmt, args...)		\
	printk(KERN_ERR "audit: (%s) error: "	\
		fmt, audit_point_name[kvm->arch.audit_point], ##args)

typedef void (*inspect_spte_fn) (struct kvm_vcpu *vcpu, u64 *sptep, int level);

static void __mmu_spte_walk(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
			    inspect_spte_fn fn, int level)
{
	int i;

	for (i = 0; i < PT64_ENT_PER_PAGE; ++i) {
		u64 *ent = sp->spt;

		fn(vcpu, ent + i, level);

		if (is_shadow_present_pte(ent[i]) &&
		      !is_last_spte(ent[i], level)) {
			struct kvm_mmu_page *child;

			child = to_shadow_page(ent[i] & PT64_BASE_ADDR_MASK);
			__mmu_spte_walk(vcpu, child, fn, level - 1);
		}
	}
}

static void mmu_spte_walk(struct kvm_vcpu *vcpu, inspect_spte_fn fn)
{
	int i;
	struct kvm_mmu_page *sp;

	if (!VALID_PAGE(vcpu->arch.mmu->root_hpa))
		return;

	if (vcpu->arch.mmu->root_level >= PT64_ROOT_4LEVEL) {
		hpa_t root = vcpu->arch.mmu->root_hpa;

		sp = to_shadow_page(root);
		__mmu_spte_walk(vcpu, sp, fn, vcpu->arch.mmu->root_level);
		return;
	}

	for (i = 0; i < 4; ++i) {
		hpa_t root = vcpu->arch.mmu->pae_root[i];

		if (IS_VALID_PAE_ROOT(root)) {
			root &= PT64_BASE_ADDR_MASK;
			sp = to_shadow_page(root);
			__mmu_spte_walk(vcpu, sp, fn, 2);
		}
	}

	return;
}

typedef void (*sp_handler) (struct kvm *kvm, struct kvm_mmu_page *sp);

static void walk_all_active_sps(struct kvm *kvm, sp_handler fn)
{
	struct kvm_mmu_page *sp;

	list_for_each_entry(sp, &kvm->arch.active_mmu_pages, link)
		fn(kvm, sp);
}

static void audit_mappings(struct kvm_vcpu *vcpu, u64 *sptep, int level)
{
	struct kvm_mmu_page *sp;
	gfn_t gfn;
	kvm_pfn_t pfn;
	hpa_t hpa;

	sp = sptep_to_sp(sptep);

	if (sp->unsync) {
		if (level != PG_LEVEL_4K) {
			audit_printk(vcpu->kvm, "unsync sp: %p "
				     "level = %d\n", sp, level);
			return;
		}
	}

	if (!is_shadow_present_pte(*sptep) || !is_last_spte(*sptep, level))
		return;

	gfn = kvm_mmu_page_get_gfn(sp, sptep - sp->spt);
	pfn = kvm_vcpu_gfn_to_pfn_atomic(vcpu, gfn);

	if (is_error_pfn(pfn))
		return;

	hpa =  pfn << PAGE_SHIFT;
	if ((*sptep & PT64_BASE_ADDR_MASK) != hpa)
		audit_printk(vcpu->kvm, "levels %d pfn %llx hpa %llx "
			     "ent %llxn", vcpu->arch.mmu->root_level, pfn,
			     hpa, *sptep);
}

static void inspect_spte_has_rmap(struct kvm *kvm, u64 *sptep)
{
	static DEFINE_RATELIMIT_STATE(ratelimit_state, 5 * HZ, 10);
	struct kvm_rmap_head *rmap_head;
	struct kvm_mmu_page *rev_sp;
	struct kvm_memslots *slots;
	struct kvm_memory_slot *slot;
	gfn_t gfn;

	rev_sp = sptep_to_sp(sptep);
	gfn = kvm_mmu_page_get_gfn(rev_sp, sptep - rev_sp->spt);

	slots = kvm_memslots_for_spte_role(kvm, rev_sp->role);
	slot = __gfn_to_memslot(slots, gfn);
	if (!slot) {
		if (!__ratelimit(&ratelimit_state))
			return;
		audit_printk(kvm, "no memslot for gfn %llx\n", gfn);
		audit_printk(kvm, "index %ld of sp (gfn=%llx)\n",
		       (long int)(sptep - rev_sp->spt), rev_sp->gfn);
		dump_stack();
		return;
	}

	rmap_head = gfn_to_rmap(gfn, rev_sp->role.level, slot);
	if (!rmap_head->val) {
		if (!__ratelimit(&ratelimit_state))
			return;
		audit_printk(kvm, "no rmap for writable spte %llx\n",
			     *sptep);
		dump_stack();
	}
}

static void audit_sptes_have_rmaps(struct kvm_vcpu *vcpu, u64 *sptep, int level)
{
	if (is_shadow_present_pte(*sptep) && is_last_spte(*sptep, level))
		inspect_spte_has_rmap(vcpu->kvm, sptep);
}

static void audit_spte_after_sync(struct kvm_vcpu *vcpu, u64 *sptep, int level)
{
	struct kvm_mmu_page *sp = sptep_to_sp(sptep);

	if (vcpu->kvm->arch.audit_point == AUDIT_POST_SYNC && sp->unsync)
		audit_printk(vcpu->kvm, "meet unsync sp(%p) after sync "
			     "root.\n", sp);
}

static void check_mappings_rmap(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	int i;

	if (sp->role.level != PG_LEVEL_4K)
		return;

	for (i = 0; i < PT64_ENT_PER_PAGE; ++i) {
		if (!is_shadow_present_pte(sp->spt[i]))
			continue;

		inspect_spte_has_rmap(kvm, sp->spt + i);
	}
}

static void audit_write_protection(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	struct kvm_rmap_head *rmap_head;
	u64 *sptep;
	struct rmap_iterator iter;
	struct kvm_memslots *slots;
	struct kvm_memory_slot *slot;

	if (sp->role.direct || sp->unsync || sp->role.invalid)
		return;

	slots = kvm_memslots_for_spte_role(kvm, sp->role);
	slot = __gfn_to_memslot(slots, sp->gfn);
	rmap_head = gfn_to_rmap(sp->gfn, PG_LEVEL_4K, slot);

	for_each_rmap_spte(rmap_head, &iter, sptep) {
		if (is_writable_pte(*sptep))
			audit_printk(kvm, "shadow page has writable "
				     "mappings: gfn %llx role %x\n",
				     sp->gfn, sp->role.word);
	}
}

static void audit_sp(struct kvm *kvm, struct kvm_mmu_page *sp)
{
	check_mappings_rmap(kvm, sp);
	audit_write_protection(kvm, sp);
}

static void audit_all_active_sps(struct kvm *kvm)
{
	walk_all_active_sps(kvm, audit_sp);
}

static void audit_spte(struct kvm_vcpu *vcpu, u64 *sptep, int level)
{
	audit_sptes_have_rmaps(vcpu, sptep, level);
	audit_mappings(vcpu, sptep, level);
	audit_spte_after_sync(vcpu, sptep, level);
}

static void audit_vcpu_spte(struct kvm_vcpu *vcpu)
{
	mmu_spte_walk(vcpu, audit_spte);
}

static bool mmu_audit;
static DEFINE_STATIC_KEY_FALSE(mmu_audit_key);

static void __kvm_mmu_audit(struct kvm_vcpu *vcpu, int point)
{
	static DEFINE_RATELIMIT_STATE(ratelimit_state, 5 * HZ, 10);

	if (!__ratelimit(&ratelimit_state))
		return;

	vcpu->kvm->arch.audit_point = point;
	audit_all_active_sps(vcpu->kvm);
	audit_vcpu_spte(vcpu);
}

static inline void kvm_mmu_audit(struct kvm_vcpu *vcpu, int point)
{
	if (static_branch_unlikely((&mmu_audit_key)))
		__kvm_mmu_audit(vcpu, point);
}

static void mmu_audit_enable(void)
{
	if (mmu_audit)
		return;

	static_branch_inc(&mmu_audit_key);
	mmu_audit = true;
}

static void mmu_audit_disable(void)
{
	if (!mmu_audit)
		return;

	static_branch_dec(&mmu_audit_key);
	mmu_audit = false;
}

static int mmu_audit_set(const char *val, const struct kernel_param *kp)
{
	int ret;
	unsigned long enable;

	ret = kstrtoul(val, 10, &enable);
	if (ret < 0)
		return -EINVAL;

	switch (enable) {
	case 0:
		mmu_audit_disable();
		break;
	case 1:
		mmu_audit_enable();
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct kernel_param_ops audit_param_ops = {
	.set = mmu_audit_set,
	.get = param_get_bool,
};

arch_param_cb(mmu_audit, &audit_param_ops, &mmu_audit, 0644);
