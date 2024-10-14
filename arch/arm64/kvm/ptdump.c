// SPDX-License-Identifier: GPL-2.0-only
/*
 * Debug helper used to dump the stage-2 pagetables of the system and their
 * associated permissions.
 *
 * Copyright (C) Google, 2024
 * Author: Sebastian Ene <sebastianene@google.com>
 */
#include <linux/debugfs.h>
#include <linux/kvm_host.h>
#include <linux/seq_file.h>

#include <asm/kvm_mmu.h>
#include <asm/kvm_pgtable.h>
#include <asm/ptdump.h>

#define MARKERS_LEN		2
#define KVM_PGTABLE_MAX_LEVELS	(KVM_PGTABLE_LAST_LEVEL + 1)

struct kvm_ptdump_guest_state {
	struct kvm		*kvm;
	struct ptdump_pg_state	parser_state;
	struct addr_marker	ipa_marker[MARKERS_LEN];
	struct ptdump_pg_level	level[KVM_PGTABLE_MAX_LEVELS];
	struct ptdump_range	range[MARKERS_LEN];
};

static const struct ptdump_prot_bits stage2_pte_bits[] = {
	{
		.mask	= PTE_VALID,
		.val	= PTE_VALID,
		.set	= " ",
		.clear	= "F",
	}, {
		.mask	= KVM_PTE_LEAF_ATTR_LO_S2_S2AP_R | PTE_VALID,
		.val	= KVM_PTE_LEAF_ATTR_LO_S2_S2AP_R | PTE_VALID,
		.set	= "R",
		.clear	= " ",
	}, {
		.mask	= KVM_PTE_LEAF_ATTR_LO_S2_S2AP_W | PTE_VALID,
		.val	= KVM_PTE_LEAF_ATTR_LO_S2_S2AP_W | PTE_VALID,
		.set	= "W",
		.clear	= " ",
	}, {
		.mask	= KVM_PTE_LEAF_ATTR_HI_S2_XN | PTE_VALID,
		.val	= PTE_VALID,
		.set	= " ",
		.clear	= "X",
	}, {
		.mask	= KVM_PTE_LEAF_ATTR_LO_S2_AF | PTE_VALID,
		.val	= KVM_PTE_LEAF_ATTR_LO_S2_AF | PTE_VALID,
		.set	= "AF",
		.clear	= "  ",
	}, {
		.mask	= PTE_TABLE_BIT | PTE_VALID,
		.val	= PTE_VALID,
		.set	= "BLK",
		.clear	= "   ",
	},
};

static int kvm_ptdump_visitor(const struct kvm_pgtable_visit_ctx *ctx,
			      enum kvm_pgtable_walk_flags visit)
{
	struct ptdump_pg_state *st = ctx->arg;
	struct ptdump_state *pt_st = &st->ptdump;

	note_page(pt_st, ctx->addr, ctx->level, ctx->old);

	return 0;
}

static int kvm_ptdump_build_levels(struct ptdump_pg_level *level, u32 start_lvl)
{
	u32 i;
	u64 mask;

	if (WARN_ON_ONCE(start_lvl >= KVM_PGTABLE_LAST_LEVEL))
		return -EINVAL;

	mask = 0;
	for (i = 0; i < ARRAY_SIZE(stage2_pte_bits); i++)
		mask |= stage2_pte_bits[i].mask;

	for (i = start_lvl; i < KVM_PGTABLE_MAX_LEVELS; i++) {
		snprintf(level[i].name, sizeof(level[i].name), "%u", i);

		level[i].num	= ARRAY_SIZE(stage2_pte_bits);
		level[i].bits	= stage2_pte_bits;
		level[i].mask	= mask;
	}

	return 0;
}

static struct kvm_ptdump_guest_state *kvm_ptdump_parser_create(struct kvm *kvm)
{
	struct kvm_ptdump_guest_state *st;
	struct kvm_s2_mmu *mmu = &kvm->arch.mmu;
	struct kvm_pgtable *pgtable = mmu->pgt;
	int ret;

	st = kzalloc(sizeof(struct kvm_ptdump_guest_state), GFP_KERNEL_ACCOUNT);
	if (!st)
		return ERR_PTR(-ENOMEM);

	ret = kvm_ptdump_build_levels(&st->level[0], pgtable->start_level);
	if (ret) {
		kfree(st);
		return ERR_PTR(ret);
	}

	st->ipa_marker[0].name		= "Guest IPA";
	st->ipa_marker[1].start_address = BIT(pgtable->ia_bits);
	st->range[0].end		= BIT(pgtable->ia_bits);

	st->kvm				= kvm;
	st->parser_state = (struct ptdump_pg_state) {
		.marker		= &st->ipa_marker[0],
		.level		= -1,
		.pg_level	= &st->level[0],
		.ptdump.range	= &st->range[0],
		.start_address	= 0,
	};

	return st;
}

static int kvm_ptdump_guest_show(struct seq_file *m, void *unused)
{
	int ret;
	struct kvm_ptdump_guest_state *st = m->private;
	struct kvm *kvm = st->kvm;
	struct kvm_s2_mmu *mmu = &kvm->arch.mmu;
	struct ptdump_pg_state *parser_state = &st->parser_state;
	struct kvm_pgtable_walker walker = (struct kvm_pgtable_walker) {
		.cb	= kvm_ptdump_visitor,
		.arg	= parser_state,
		.flags	= KVM_PGTABLE_WALK_LEAF,
	};

	parser_state->seq = m;

	write_lock(&kvm->mmu_lock);
	ret = kvm_pgtable_walk(mmu->pgt, 0, BIT(mmu->pgt->ia_bits), &walker);
	write_unlock(&kvm->mmu_lock);

	return ret;
}

static int kvm_ptdump_guest_open(struct inode *m, struct file *file)
{
	struct kvm *kvm = m->i_private;
	struct kvm_ptdump_guest_state *st;
	int ret;

	if (!kvm_get_kvm_safe(kvm))
		return -ENOENT;

	st = kvm_ptdump_parser_create(kvm);
	if (IS_ERR(st)) {
		ret = PTR_ERR(st);
		goto err_with_kvm_ref;
	}

	ret = single_open(file, kvm_ptdump_guest_show, st);
	if (!ret)
		return 0;

	kfree(st);
err_with_kvm_ref:
	kvm_put_kvm(kvm);
	return ret;
}

static int kvm_ptdump_guest_close(struct inode *m, struct file *file)
{
	struct kvm *kvm = m->i_private;
	void *st = ((struct seq_file *)file->private_data)->private;

	kfree(st);
	kvm_put_kvm(kvm);

	return single_release(m, file);
}

static const struct file_operations kvm_ptdump_guest_fops = {
	.open		= kvm_ptdump_guest_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= kvm_ptdump_guest_close,
};

static int kvm_pgtable_range_show(struct seq_file *m, void *unused)
{
	struct kvm_pgtable *pgtable = m->private;

	seq_printf(m, "%2u\n", pgtable->ia_bits);
	return 0;
}

static int kvm_pgtable_levels_show(struct seq_file *m, void *unused)
{
	struct kvm_pgtable *pgtable = m->private;

	seq_printf(m, "%1d\n", KVM_PGTABLE_MAX_LEVELS - pgtable->start_level);
	return 0;
}

static int kvm_pgtable_debugfs_open(struct inode *m, struct file *file,
				    int (*show)(struct seq_file *, void *))
{
	struct kvm *kvm = m->i_private;
	struct kvm_pgtable *pgtable;
	int ret;

	if (!kvm_get_kvm_safe(kvm))
		return -ENOENT;

	pgtable = kvm->arch.mmu.pgt;

	ret = single_open(file, show, pgtable);
	if (ret < 0)
		kvm_put_kvm(kvm);
	return ret;
}

static int kvm_pgtable_range_open(struct inode *m, struct file *file)
{
	return kvm_pgtable_debugfs_open(m, file, kvm_pgtable_range_show);
}

static int kvm_pgtable_levels_open(struct inode *m, struct file *file)
{
	return kvm_pgtable_debugfs_open(m, file, kvm_pgtable_levels_show);
}

static int kvm_pgtable_debugfs_close(struct inode *m, struct file *file)
{
	struct kvm *kvm = m->i_private;

	kvm_put_kvm(kvm);
	return single_release(m, file);
}

static const struct file_operations kvm_pgtable_range_fops = {
	.open		= kvm_pgtable_range_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= kvm_pgtable_debugfs_close,
};

static const struct file_operations kvm_pgtable_levels_fops = {
	.open		= kvm_pgtable_levels_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= kvm_pgtable_debugfs_close,
};

void kvm_s2_ptdump_create_debugfs(struct kvm *kvm)
{
	debugfs_create_file("stage2_page_tables", 0400, kvm->debugfs_dentry,
			    kvm, &kvm_ptdump_guest_fops);
	debugfs_create_file("ipa_range", 0400, kvm->debugfs_dentry, kvm,
			    &kvm_pgtable_range_fops);
	debugfs_create_file("stage2_levels", 0400, kvm->debugfs_dentry,
			    kvm, &kvm_pgtable_levels_fops);
}
