/*
 * mmu_audit.c:
 *
 * Audit code for KVM MMU
 *
 * Copyright (C) 2006 Qumranet, Inc.
 * Copyright 2010 Red Hat, Inc. and/or its affilates.
 *
 * Authors:
 *   Yaniv Kamay  <yaniv@qumranet.com>
 *   Avi Kivity   <avi@qumranet.com>
 *   Marcelo Tosatti <mtosatti@redhat.com>
 *   Xiao Guangrong <xiaoguangrong@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

static const char *audit_msg;

typedef void (*inspect_spte_fn) (struct kvm *kvm, u64 *sptep);

static void __mmu_spte_walk(struct kvm *kvm, struct kvm_mmu_page *sp,
			    inspect_spte_fn fn)
{
	int i;

	for (i = 0; i < PT64_ENT_PER_PAGE; ++i) {
		u64 ent = sp->spt[i];

		if (is_shadow_present_pte(ent)) {
			if (!is_last_spte(ent, sp->role.level)) {
				struct kvm_mmu_page *child;
				child = page_header(ent & PT64_BASE_ADDR_MASK);
				__mmu_spte_walk(kvm, child, fn);
			} else
				fn(kvm, &sp->spt[i]);
		}
	}
}

static void mmu_spte_walk(struct kvm_vcpu *vcpu, inspect_spte_fn fn)
{
	int i;
	struct kvm_mmu_page *sp;

	if (!VALID_PAGE(vcpu->arch.mmu.root_hpa))
		return;
	if (vcpu->arch.mmu.shadow_root_level == PT64_ROOT_LEVEL) {
		hpa_t root = vcpu->arch.mmu.root_hpa;
		sp = page_header(root);
		__mmu_spte_walk(vcpu->kvm, sp, fn);
		return;
	}
	for (i = 0; i < 4; ++i) {
		hpa_t root = vcpu->arch.mmu.pae_root[i];

		if (root && VALID_PAGE(root)) {
			root &= PT64_BASE_ADDR_MASK;
			sp = page_header(root);
			__mmu_spte_walk(vcpu->kvm, sp, fn);
		}
	}
	return;
}

static void audit_mappings_page(struct kvm_vcpu *vcpu, u64 page_pte,
				gva_t va, int level)
{
	u64 *pt = __va(page_pte & PT64_BASE_ADDR_MASK);
	int i;
	gva_t va_delta = 1ul << (PAGE_SHIFT + 9 * (level - 1));

	for (i = 0; i < PT64_ENT_PER_PAGE; ++i, va += va_delta) {
		u64 *sptep = pt + i;
		struct kvm_mmu_page *sp;
		gfn_t gfn;
		pfn_t pfn;
		hpa_t hpa;

		sp = page_header(__pa(sptep));

		if (sp->unsync) {
			if (level != PT_PAGE_TABLE_LEVEL) {
				printk(KERN_ERR "audit: (%s) error: unsync sp: %p level = %d\n",
						audit_msg, sp, level);
				return;
			}

			if (*sptep == shadow_notrap_nonpresent_pte) {
				printk(KERN_ERR "audit: (%s) error: notrap spte in unsync sp: %p\n",
						audit_msg, sp);
				return;
			}
		}

		if (sp->role.direct && *sptep == shadow_notrap_nonpresent_pte) {
			printk(KERN_ERR "audit: (%s) error: notrap spte in direct sp: %p\n",
					audit_msg, sp);
			return;
		}

		if (!is_shadow_present_pte(*sptep) ||
		      !is_last_spte(*sptep, level))
			return;

		gfn = kvm_mmu_page_get_gfn(sp, sptep - sp->spt);
		pfn = gfn_to_pfn_atomic(vcpu->kvm, gfn);

		if (is_error_pfn(pfn)) {
			kvm_release_pfn_clean(pfn);
			return;
		}

		hpa =  pfn << PAGE_SHIFT;

		if ((*sptep & PT64_BASE_ADDR_MASK) != hpa)
			printk(KERN_ERR "xx audit error: (%s) levels %d"
					   " gva %lx pfn %llx hpa %llx ent %llxn",
					   audit_msg, vcpu->arch.mmu.root_level,
					   va, pfn, hpa, *sptep);
	}
}

static void audit_mappings(struct kvm_vcpu *vcpu)
{
	unsigned i;

	if (vcpu->arch.mmu.root_level == 4)
		audit_mappings_page(vcpu, vcpu->arch.mmu.root_hpa, 0, 4);
	else
		for (i = 0; i < 4; ++i)
			if (vcpu->arch.mmu.pae_root[i] & PT_PRESENT_MASK)
				audit_mappings_page(vcpu,
						    vcpu->arch.mmu.pae_root[i],
						    i << 30,
						    2);
}

void inspect_spte_has_rmap(struct kvm *kvm, u64 *sptep)
{
	unsigned long *rmapp;
	struct kvm_mmu_page *rev_sp;
	gfn_t gfn;


	rev_sp = page_header(__pa(sptep));
	gfn = kvm_mmu_page_get_gfn(rev_sp, sptep - rev_sp->spt);

	if (!gfn_to_memslot(kvm, gfn)) {
		if (!printk_ratelimit())
			return;
		printk(KERN_ERR "%s: no memslot for gfn %llx\n",
				 audit_msg, gfn);
		printk(KERN_ERR "%s: index %ld of sp (gfn=%llx)\n",
		       audit_msg, (long int)(sptep - rev_sp->spt),
				rev_sp->gfn);
		dump_stack();
		return;
	}

	rmapp = gfn_to_rmap(kvm, gfn, rev_sp->role.level);
	if (!*rmapp) {
		if (!printk_ratelimit())
			return;
		printk(KERN_ERR "%s: no rmap for writable spte %llx\n",
				 audit_msg, *sptep);
		dump_stack();
	}
}

void audit_sptes_have_rmaps(struct kvm_vcpu *vcpu)
{
	mmu_spte_walk(vcpu, inspect_spte_has_rmap);
}

static void check_mappings_rmap(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_page *sp;
	int i;

	list_for_each_entry(sp, &vcpu->kvm->arch.active_mmu_pages, link) {
		u64 *pt = sp->spt;

		if (sp->role.level != PT_PAGE_TABLE_LEVEL)
			continue;

		for (i = 0; i < PT64_ENT_PER_PAGE; ++i) {
			if (!is_rmap_spte(pt[i]))
				continue;

			inspect_spte_has_rmap(vcpu->kvm, &pt[i]);
		}
	}
	return;
}

static void audit_rmap(struct kvm_vcpu *vcpu)
{
	check_mappings_rmap(vcpu);
}

static void audit_write_protection(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_page *sp;
	struct kvm_memory_slot *slot;
	unsigned long *rmapp;
	u64 *spte;

	list_for_each_entry(sp, &vcpu->kvm->arch.active_mmu_pages, link) {
		if (sp->role.direct)
			continue;
		if (sp->unsync)
			continue;
		if (sp->role.invalid)
			continue;

		slot = gfn_to_memslot(vcpu->kvm, sp->gfn);
		rmapp = &slot->rmap[sp->gfn - slot->base_gfn];

		spte = rmap_next(vcpu->kvm, rmapp, NULL);
		while (spte) {
			if (is_writable_pte(*spte))
				printk(KERN_ERR "%s: (%s) shadow page has "
				"writable mappings: gfn %llx role %x\n",
			       __func__, audit_msg, sp->gfn,
			       sp->role.word);
			spte = rmap_next(vcpu->kvm, rmapp, spte);
		}
	}
}

static void kvm_mmu_audit(void *ignore, struct kvm_vcpu *vcpu, int audit_point)
{
	audit_msg = audit_point_name[audit_point];
	audit_rmap(vcpu);
	audit_write_protection(vcpu);
	if (strcmp("pre pte write", audit_msg) != 0)
		audit_mappings(vcpu);
	audit_sptes_have_rmaps(vcpu);
}

static bool mmu_audit;

static void mmu_audit_enable(void)
{
	int ret;

	if (mmu_audit)
		return;

	ret = register_trace_kvm_mmu_audit(kvm_mmu_audit, NULL);
	WARN_ON(ret);

	mmu_audit = true;
}

static void mmu_audit_disable(void)
{
	if (!mmu_audit)
		return;

	unregister_trace_kvm_mmu_audit(kvm_mmu_audit, NULL);
	tracepoint_synchronize_unregister();
	mmu_audit = false;
}

static int mmu_audit_set(const char *val, const struct kernel_param *kp)
{
	int ret;
	unsigned long enable;

	ret = strict_strtoul(val, 10, &enable);
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

static struct kernel_param_ops audit_param_ops = {
	.set = mmu_audit_set,
	.get = param_get_bool,
};

module_param_cb(mmu_audit, &audit_param_ops, &mmu_audit, 0644);
