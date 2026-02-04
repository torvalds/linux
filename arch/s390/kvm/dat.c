// SPDX-License-Identifier: GPL-2.0
/*
 *  KVM guest address space mapping code
 *
 *    Copyright IBM Corp. 2007, 2020, 2024
 *    Author(s): Claudio Imbrenda <imbrenda@linux.ibm.com>
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		 David Hildenbrand <david@redhat.com>
 *		 Janosch Frank <frankja@linux.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/pagewalk.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/swapops.h>
#include <linux/ksm.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/pgtable.h>
#include <linux/kvm_types.h>
#include <linux/kvm_host.h>
#include <linux/pgalloc.h>

#include <asm/page-states.h>
#include <asm/tlb.h>
#include "dat.h"

int kvm_s390_mmu_cache_topup(struct kvm_s390_mmu_cache *mc)
{
	void *o;

	for ( ; mc->n_crsts < KVM_S390_MMU_CACHE_N_CRSTS; mc->n_crsts++) {
		o = (void *)__get_free_pages(GFP_KERNEL_ACCOUNT | __GFP_COMP, CRST_ALLOC_ORDER);
		if (!o)
			return -ENOMEM;
		mc->crsts[mc->n_crsts] = o;
	}
	for ( ; mc->n_pts < KVM_S390_MMU_CACHE_N_PTS; mc->n_pts++) {
		o = (void *)__get_free_page(GFP_KERNEL_ACCOUNT);
		if (!o)
			return -ENOMEM;
		mc->pts[mc->n_pts] = o;
	}
	for ( ; mc->n_rmaps < KVM_S390_MMU_CACHE_N_RMAPS; mc->n_rmaps++) {
		o = kzalloc(sizeof(*mc->rmaps[0]), GFP_KERNEL_ACCOUNT);
		if (!o)
			return -ENOMEM;
		mc->rmaps[mc->n_rmaps] = o;
	}
	return 0;
}

static inline struct page_table *dat_alloc_pt_noinit(struct kvm_s390_mmu_cache *mc)
{
	struct page_table *res;

	res = kvm_s390_mmu_cache_alloc_pt(mc);
	if (res)
		__arch_set_page_dat(res, 1);
	return res;
}

static inline struct crst_table *dat_alloc_crst_noinit(struct kvm_s390_mmu_cache *mc)
{
	struct crst_table *res;

	res = kvm_s390_mmu_cache_alloc_crst(mc);
	if (res)
		__arch_set_page_dat(res, 1UL << CRST_ALLOC_ORDER);
	return res;
}

struct crst_table *dat_alloc_crst_sleepable(unsigned long init)
{
	struct page *page;
	void *virt;

	page = alloc_pages(GFP_KERNEL_ACCOUNT | __GFP_COMP, CRST_ALLOC_ORDER);
	if (!page)
		return NULL;
	virt = page_to_virt(page);
	__arch_set_page_dat(virt, 1UL << CRST_ALLOC_ORDER);
	crst_table_init(virt, init);
	return virt;
}

void dat_free_level(struct crst_table *table, bool owns_ptes)
{
	unsigned int i;

	for (i = 0; i < _CRST_ENTRIES; i++) {
		if (table->crstes[i].h.fc || table->crstes[i].h.i)
			continue;
		if (!is_pmd(table->crstes[i]))
			dat_free_level(dereference_crste(table->crstes[i]), owns_ptes);
		else if (owns_ptes)
			dat_free_pt(dereference_pmd(table->crstes[i].pmd));
	}
	dat_free_crst(table);
}

/**
 * dat_crstep_xchg() - Exchange a gmap CRSTE with another.
 * @crstep: Pointer to the CRST entry
 * @new: Replacement entry.
 * @gfn: The affected guest address.
 * @asce: The ASCE of the address space.
 *
 * Context: This function is assumed to be called with kvm->mmu_lock held.
 */
void dat_crstep_xchg(union crste *crstep, union crste new, gfn_t gfn, union asce asce)
{
	if (crstep->h.i) {
		WRITE_ONCE(*crstep, new);
		return;
	} else if (cpu_has_edat2()) {
		crdte_crste(crstep, *crstep, new, gfn, asce);
		return;
	}

	if (machine_has_tlb_guest())
		idte_crste(crstep, gfn, IDTE_GUEST_ASCE, asce, IDTE_GLOBAL);
	else
		idte_crste(crstep, gfn, 0, NULL_ASCE, IDTE_GLOBAL);
	WRITE_ONCE(*crstep, new);
}

/**
 * dat_crstep_xchg_atomic() - Atomically exchange a gmap CRSTE with another.
 * @crstep: Pointer to the CRST entry.
 * @old: Expected old value.
 * @new: Replacement entry.
 * @gfn: The affected guest address.
 * @asce: The asce of the address space.
 *
 * This function is needed to atomically exchange a CRSTE that potentially
 * maps a prefix area, without having to invalidate it inbetween.
 *
 * Context: This function is assumed to be called with kvm->mmu_lock held.
 *
 * Return: %true if the exchange was successful.
 */
bool dat_crstep_xchg_atomic(union crste *crstep, union crste old, union crste new, gfn_t gfn,
			    union asce asce)
{
	if (old.h.i)
		return arch_try_cmpxchg((long *)crstep, &old.val, new.val);
	if (cpu_has_edat2())
		return crdte_crste(crstep, old, new, gfn, asce);
	return cspg_crste(crstep, old, new);
}

static void dat_set_storage_key_from_pgste(union pte pte, union pgste pgste)
{
	union skey nkey = { .acc = pgste.acc, .fp = pgste.fp };

	page_set_storage_key(pte_origin(pte), nkey.skey, 0);
}

static void dat_move_storage_key(union pte old, union pte new)
{
	page_set_storage_key(pte_origin(new), page_get_storage_key(pte_origin(old)), 1);
}

static union pgste dat_save_storage_key_into_pgste(union pte pte, union pgste pgste)
{
	union skey skey;

	skey.skey = page_get_storage_key(pte_origin(pte));

	pgste.acc = skey.acc;
	pgste.fp = skey.fp;
	pgste.gr |= skey.r;
	pgste.gc |= skey.c;

	return pgste;
}

union pgste __dat_ptep_xchg(union pte *ptep, union pgste pgste, union pte new, gfn_t gfn,
			    union asce asce, bool uses_skeys)
{
	union pte old = READ_ONCE(*ptep);

	/* Updating only the software bits while holding the pgste lock. */
	if (!((ptep->val ^ new.val) & ~_PAGE_SW_BITS)) {
		WRITE_ONCE(ptep->swbyte, new.swbyte);
		return pgste;
	}

	if (!old.h.i) {
		unsigned long opts = IPTE_GUEST_ASCE | (pgste.nodat ? IPTE_NODAT : 0);

		if (machine_has_tlb_guest())
			__ptep_ipte(gfn_to_gpa(gfn), (void *)ptep, opts, asce.val, IPTE_GLOBAL);
		else
			__ptep_ipte(gfn_to_gpa(gfn), (void *)ptep, 0, 0, IPTE_GLOBAL);
	}

	if (uses_skeys) {
		if (old.h.i && !new.h.i)
			/* Invalid to valid: restore storage keys from PGSTE. */
			dat_set_storage_key_from_pgste(new, pgste);
		else if (!old.h.i && new.h.i)
			/* Valid to invalid: save storage keys to PGSTE. */
			pgste = dat_save_storage_key_into_pgste(old, pgste);
		else if (!old.h.i && !new.h.i)
			/* Valid to valid: move storage keys. */
			if (old.h.pfra != new.h.pfra)
				dat_move_storage_key(old, new);
		/* Invalid to invalid: nothing to do. */
	}

	WRITE_ONCE(*ptep, new);
	return pgste;
}

/*
 * dat_split_ste() - Split a segment table entry into page table entries.
 *
 * Context: This function is assumed to be called with kvm->mmu_lock held.
 *
 * Return: 0 in case of success, -ENOMEM if running out of memory.
 */
static int dat_split_ste(struct kvm_s390_mmu_cache *mc, union pmd *pmdp, gfn_t gfn,
			 union asce asce, bool uses_skeys)
{
	union pgste pgste_init;
	struct page_table *pt;
	union pmd new, old;
	union pte init;
	int i;

	BUG_ON(!mc);
	old = READ_ONCE(*pmdp);

	/* Already split, nothing to do. */
	if (!old.h.i && !old.h.fc)
		return 0;

	pt = dat_alloc_pt_noinit(mc);
	if (!pt)
		return -ENOMEM;
	new.val = virt_to_phys(pt);

	while (old.h.i || old.h.fc) {
		init.val = pmd_origin_large(old);
		init.h.p = old.h.p;
		init.h.i = old.h.i;
		init.s.d = old.s.fc1.d;
		init.s.w = old.s.fc1.w;
		init.s.y = old.s.fc1.y;
		init.s.sd = old.s.fc1.sd;
		init.s.pr = old.s.fc1.pr;
		pgste_init.val = 0;
		if (old.h.fc) {
			for (i = 0; i < _PAGE_ENTRIES; i++)
				pt->ptes[i].val = init.val | i * PAGE_SIZE;
			/* No need to take locks as the page table is not installed yet. */
			pgste_init.prefix_notif = old.s.fc1.prefix_notif;
			pgste_init.pcl = uses_skeys && init.h.i;
			dat_init_pgstes(pt, pgste_init.val);
		} else {
			dat_init_page_table(pt, init.val, 0);
		}

		if (dat_pmdp_xchg_atomic(pmdp, old, new, gfn, asce)) {
			if (!pgste_init.pcl)
				return 0;
			for (i = 0; i < _PAGE_ENTRIES; i++) {
				union pgste pgste = pt->pgstes[i];

				pgste = dat_save_storage_key_into_pgste(pt->ptes[i], pgste);
				pgste_set_unlock(pt->ptes + i, pgste);
			}
			return 0;
		}
		old = READ_ONCE(*pmdp);
	}

	dat_free_pt(pt);
	return 0;
}

/*
 * dat_split_crste() - Split a crste into smaller crstes.
 *
 * Context: This function is assumed to be called with kvm->mmu_lock held.
 *
 * Return: %0 in case of success, %-ENOMEM if running out of memory.
 */
static int dat_split_crste(struct kvm_s390_mmu_cache *mc, union crste *crstep,
			   gfn_t gfn, union asce asce, bool uses_skeys)
{
	struct crst_table *table;
	union crste old, new, init;
	int i;

	old = READ_ONCE(*crstep);
	if (is_pmd(old))
		return dat_split_ste(mc, &crstep->pmd, gfn, asce, uses_skeys);

	BUG_ON(!mc);

	/* Already split, nothing to do. */
	if (!old.h.i && !old.h.fc)
		return 0;

	table = dat_alloc_crst_noinit(mc);
	if (!table)
		return -ENOMEM;

	new.val = virt_to_phys(table);
	new.h.tt = old.h.tt;
	new.h.fc0.tl = _REGION_ENTRY_LENGTH;

	while (old.h.i || old.h.fc) {
		init = old;
		init.h.tt--;
		if (old.h.fc) {
			for (i = 0; i < _CRST_ENTRIES; i++)
				table->crstes[i].val = init.val | i * HPAGE_SIZE;
		} else {
			crst_table_init((void *)table, init.val);
		}
		if (dat_crstep_xchg_atomic(crstep, old, new, gfn, asce))
			return 0;
		old = READ_ONCE(*crstep);
	}

	dat_free_crst(table);
	return 0;
}

/**
 * dat_entry_walk() - Walk the gmap page tables.
 * @mc: Cache to use to allocate dat tables, if needed; can be NULL if neither
 *      %DAT_WALK_SPLIT or %DAT_WALK_ALLOC is specified in @flags.
 * @gfn: Guest frame.
 * @asce: The ASCE of the address space.
 * @flags: Flags from WALK_* macros.
 * @walk_level: Level to walk to, from LEVEL_* macros.
 * @last: Will be filled the last visited non-pte DAT entry.
 * @ptepp: Will be filled the last visited pte entry, if any, otherwise NULL.
 *
 * Returns a table entry pointer for the given guest address and @walk_level.
 *
 * The @flags have the following meanings:
 * * %DAT_WALK_IGN_HOLES: consider holes as normal table entries
 * * %DAT_WALK_ALLOC: allocate new tables to reach the requested level, if needed
 * * %DAT_WALK_SPLIT: split existing large pages to reach the requested level, if needed
 * * %DAT_WALK_LEAF: return successfully whenever a large page is encountered
 * * %DAT_WALK_ANY: return successfully even if the requested level could not be reached
 * * %DAT_WALK_CONTINUE: walk to the requested level with the specified flags, and then try to
 *                       continue walking to ptes with only DAT_WALK_ANY
 * * %DAT_WALK_USES_SKEYS: storage keys are in use
 *
 * Context: called with kvm->mmu_lock held.
 *
 * Return:
 * * %PGM_ADDRESSING if the requested address lies outside memory
 * * a PIC number if the requested address lies in a memory hole of type _DAT_TOKEN_PIC
 * * %-EFAULT if the requested address lies inside a memory hole of a different type
 * * %-EINVAL if the given ASCE is not compatible with the requested level
 * * %-EFBIG if the requested level could not be reached because a larger frame was found
 * * %-ENOENT if the requested level could not be reached for other reasons
 * * %-ENOMEM if running out of memory while allocating or splitting a table
 */
int dat_entry_walk(struct kvm_s390_mmu_cache *mc, gfn_t gfn, union asce asce, int flags,
		   int walk_level, union crste **last, union pte **ptepp)
{
	union vaddress vaddr = { .addr = gfn_to_gpa(gfn) };
	bool continue_anyway = flags & DAT_WALK_CONTINUE;
	bool uses_skeys = flags & DAT_WALK_USES_SKEYS;
	bool ign_holes = flags & DAT_WALK_IGN_HOLES;
	bool allocate = flags & DAT_WALK_ALLOC;
	bool split = flags & DAT_WALK_SPLIT;
	bool leaf = flags & DAT_WALK_LEAF;
	bool any = flags & DAT_WALK_ANY;
	struct page_table *pgtable;
	struct crst_table *table;
	union crste entry;
	int rc;

	*last = NULL;
	*ptepp = NULL;
	if (WARN_ON_ONCE(unlikely(!asce.val)))
		return -EINVAL;
	if (WARN_ON_ONCE(unlikely(walk_level > asce.dt)))
		return -EINVAL;
	if (!asce_contains_gfn(asce, gfn))
		return PGM_ADDRESSING;

	table = dereference_asce(asce);
	if (asce.dt >= ASCE_TYPE_REGION1) {
		*last = table->crstes + vaddr.rfx;
		entry = READ_ONCE(**last);
		if (WARN_ON_ONCE(entry.h.tt != TABLE_TYPE_REGION1))
			return -EINVAL;
		if (crste_hole(entry) && !ign_holes)
			return entry.tok.type == _DAT_TOKEN_PIC ? entry.tok.par : -EFAULT;
		if (walk_level == TABLE_TYPE_REGION1)
			return 0;
		if (entry.pgd.h.i) {
			if (!allocate)
				return any ? 0 : -ENOENT;
			rc = dat_split_crste(mc, *last, gfn, asce, uses_skeys);
			if (rc)
				return rc;
			entry = READ_ONCE(**last);
		}
		table = dereference_crste(entry.pgd);
	}

	if (asce.dt >= ASCE_TYPE_REGION2) {
		*last = table->crstes + vaddr.rsx;
		entry = READ_ONCE(**last);
		if (WARN_ON_ONCE(entry.h.tt != TABLE_TYPE_REGION2))
			return -EINVAL;
		if (crste_hole(entry) && !ign_holes)
			return entry.tok.type == _DAT_TOKEN_PIC ? entry.tok.par : -EFAULT;
		if (walk_level == TABLE_TYPE_REGION2)
			return 0;
		if (entry.p4d.h.i) {
			if (!allocate)
				return any ? 0 : -ENOENT;
			rc = dat_split_crste(mc, *last, gfn, asce, uses_skeys);
			if (rc)
				return rc;
			entry = READ_ONCE(**last);
		}
		table = dereference_crste(entry.p4d);
	}

	if (asce.dt >= ASCE_TYPE_REGION3) {
		*last = table->crstes + vaddr.rtx;
		entry = READ_ONCE(**last);
		if (WARN_ON_ONCE(entry.h.tt != TABLE_TYPE_REGION3))
			return -EINVAL;
		if (crste_hole(entry) && !ign_holes)
			return entry.tok.type == _DAT_TOKEN_PIC ? entry.tok.par : -EFAULT;
		if (walk_level == TABLE_TYPE_REGION3 &&
		    continue_anyway && !entry.pud.h.fc && !entry.h.i) {
			walk_level = TABLE_TYPE_PAGE_TABLE;
			allocate = false;
		}
		if (walk_level == TABLE_TYPE_REGION3 || ((leaf || any) && entry.pud.h.fc))
			return 0;
		if (entry.pud.h.i && !entry.pud.h.fc) {
			if (!allocate)
				return any ? 0 : -ENOENT;
			rc = dat_split_crste(mc, *last, gfn, asce, uses_skeys);
			if (rc)
				return rc;
			entry = READ_ONCE(**last);
		}
		if (walk_level <= TABLE_TYPE_SEGMENT && entry.pud.h.fc) {
			if (!split)
				return -EFBIG;
			rc = dat_split_crste(mc, *last, gfn, asce, uses_skeys);
			if (rc)
				return rc;
			entry = READ_ONCE(**last);
		}
		table = dereference_crste(entry.pud);
	}

	*last = table->crstes + vaddr.sx;
	entry = READ_ONCE(**last);
	if (WARN_ON_ONCE(entry.h.tt != TABLE_TYPE_SEGMENT))
		return -EINVAL;
	if (crste_hole(entry) && !ign_holes)
		return entry.tok.type == _DAT_TOKEN_PIC ? entry.tok.par : -EFAULT;
	if (continue_anyway && !entry.pmd.h.fc && !entry.h.i) {
		walk_level = TABLE_TYPE_PAGE_TABLE;
		allocate = false;
	}
	if (walk_level == TABLE_TYPE_SEGMENT || ((leaf || any) && entry.pmd.h.fc))
		return 0;

	if (entry.pmd.h.i && !entry.pmd.h.fc) {
		if (!allocate)
			return any ? 0 : -ENOENT;
		rc = dat_split_ste(mc, &(*last)->pmd, gfn, asce, uses_skeys);
		if (rc)
			return rc;
		entry = READ_ONCE(**last);
	}
	if (walk_level <= TABLE_TYPE_PAGE_TABLE && entry.pmd.h.fc) {
		if (!split)
			return -EFBIG;
		rc = dat_split_ste(mc, &(*last)->pmd, gfn, asce, uses_skeys);
		if (rc)
			return rc;
		entry = READ_ONCE(**last);
	}
	pgtable = dereference_pmd(entry.pmd);
	*ptepp = pgtable->ptes + vaddr.px;
	if (pte_hole(**ptepp) && !ign_holes)
		return (*ptepp)->tok.type == _DAT_TOKEN_PIC ? (*ptepp)->tok.par : -EFAULT;
	return 0;
}

static long dat_pte_walk_range(gfn_t gfn, gfn_t end, struct page_table *table, struct dat_walk *w)
{
	unsigned int idx = gfn & (_PAGE_ENTRIES - 1);
	long rc = 0;

	for ( ; gfn < end; idx++, gfn++) {
		if (pte_hole(READ_ONCE(table->ptes[idx]))) {
			if (!(w->flags & DAT_WALK_IGN_HOLES))
				return -EFAULT;
			if (!(w->flags & DAT_WALK_ANY))
				continue;
		}

		rc = w->ops->pte_entry(table->ptes + idx, gfn, gfn + 1, w);
		if (rc)
			break;
	}
	return rc;
}

static long dat_crste_walk_range(gfn_t start, gfn_t end, struct crst_table *table,
				 struct dat_walk *walk)
{
	unsigned long idx, cur_shift, cur_size;
	dat_walk_op the_op;
	union crste crste;
	gfn_t cur, next;
	long rc = 0;

	cur_shift = 8 + table->crstes[0].h.tt * 11;
	idx = (start >> cur_shift) & (_CRST_ENTRIES - 1);
	cur_size = 1UL << cur_shift;

	for (cur = ALIGN_DOWN(start, cur_size); cur < end; idx++, cur = next) {
		next = cur + cur_size;
		walk->last = table->crstes + idx;
		crste = READ_ONCE(*walk->last);

		if (crste_hole(crste)) {
			if (!(walk->flags & DAT_WALK_IGN_HOLES))
				return -EFAULT;
			if (!(walk->flags & DAT_WALK_ANY))
				continue;
		}

		the_op = walk->ops->crste_ops[crste.h.tt];
		if (the_op) {
			rc = the_op(walk->last, cur, next, walk);
			crste = READ_ONCE(*walk->last);
		}
		if (rc)
			break;
		if (!crste.h.i && !crste.h.fc) {
			if (!is_pmd(crste))
				rc = dat_crste_walk_range(max(start, cur), min(end, next),
							  _dereference_crste(crste), walk);
			else if (walk->ops->pte_entry)
				rc = dat_pte_walk_range(max(start, cur), min(end, next),
							dereference_pmd(crste.pmd), walk);
		}
	}
	return rc;
}

/**
 * _dat_walk_gfn_range() - Walk DAT tables.
 * @start: The first guest page frame to walk.
 * @end: The guest page frame immediately after the last one to walk.
 * @asce: The ASCE of the guest mapping.
 * @ops: The gmap_walk_ops that will be used to perform the walk.
 * @flags: Flags from WALK_* (currently only WALK_IGN_HOLES is supported).
 * @priv: Will be passed as-is to the callbacks.
 *
 * Any callback returning non-zero causes the walk to stop immediately.
 *
 * Return: %-EINVAL in case of error, %-EFAULT if @start is too high for the
 *         given ASCE unless the DAT_WALK_IGN_HOLES flag is specified,
 *         otherwise it returns whatever the callbacks return.
 */
long _dat_walk_gfn_range(gfn_t start, gfn_t end, union asce asce,
			 const struct dat_walk_ops *ops, int flags, void *priv)
{
	struct crst_table *table = dereference_asce(asce);
	struct dat_walk walk = {
		.ops	= ops,
		.asce	= asce,
		.priv	= priv,
		.flags	= flags,
		.start	= start,
		.end	= end,
	};

	if (WARN_ON_ONCE(unlikely(!asce.val)))
		return -EINVAL;
	if (!asce_contains_gfn(asce, start))
		return (flags & DAT_WALK_IGN_HOLES) ? 0 : -EFAULT;

	return dat_crste_walk_range(start, min(end, asce_end(asce)), table, &walk);
}

int dat_get_storage_key(union asce asce, gfn_t gfn, union skey *skey)
{
	union crste *crstep;
	union pgste pgste;
	union pte *ptep;
	int rc;

	skey->skey = 0;
	rc = dat_entry_walk(NULL, gfn, asce, DAT_WALK_ANY, TABLE_TYPE_PAGE_TABLE, &crstep, &ptep);
	if (rc)
		return rc;

	if (!ptep) {
		union crste crste;

		crste = READ_ONCE(*crstep);
		if (!crste.h.fc || !crste.s.fc1.pr)
			return 0;
		skey->skey = page_get_storage_key(large_crste_to_phys(crste, gfn));
		return 0;
	}
	pgste = pgste_get_lock(ptep);
	if (ptep->h.i) {
		skey->acc = pgste.acc;
		skey->fp = pgste.fp;
	} else {
		skey->skey = page_get_storage_key(pte_origin(*ptep));
	}
	skey->r |= pgste.gr;
	skey->c |= pgste.gc;
	pgste_set_unlock(ptep, pgste);
	return 0;
}

static void dat_update_ptep_sd(union pgste old, union pgste pgste, union pte *ptep)
{
	if (pgste.acc != old.acc || pgste.fp != old.fp || pgste.gr != old.gr || pgste.gc != old.gc)
		__atomic64_or(_PAGE_SD, &ptep->val);
}

int dat_set_storage_key(struct kvm_s390_mmu_cache *mc, union asce asce, gfn_t gfn,
			union skey skey, bool nq)
{
	union pgste pgste, old;
	union crste *crstep;
	union pte *ptep;
	int rc;

	rc = dat_entry_walk(mc, gfn, asce, DAT_WALK_LEAF_ALLOC, TABLE_TYPE_PAGE_TABLE,
			    &crstep, &ptep);
	if (rc)
		return rc;

	if (!ptep) {
		page_set_storage_key(large_crste_to_phys(*crstep, gfn), skey.skey, !nq);
		return 0;
	}

	old = pgste_get_lock(ptep);
	pgste = old;

	pgste.acc = skey.acc;
	pgste.fp = skey.fp;
	pgste.gc = skey.c;
	pgste.gr = skey.r;

	if (!ptep->h.i) {
		union skey old_skey;

		old_skey.skey = page_get_storage_key(pte_origin(*ptep));
		pgste.hc |= old_skey.c;
		pgste.hr |= old_skey.r;
		old_skey.c = old.gc;
		old_skey.r = old.gr;
		skey.r = 0;
		skey.c = 0;
		page_set_storage_key(pte_origin(*ptep), skey.skey, !nq);
	}

	dat_update_ptep_sd(old, pgste, ptep);
	pgste_set_unlock(ptep, pgste);
	return 0;
}

static bool page_cond_set_storage_key(phys_addr_t paddr, union skey skey, union skey *oldkey,
				      bool nq, bool mr, bool mc)
{
	oldkey->skey = page_get_storage_key(paddr);
	if (oldkey->acc == skey.acc && oldkey->fp == skey.fp &&
	    (oldkey->r == skey.r || mr) && (oldkey->c == skey.c || mc))
		return false;
	page_set_storage_key(paddr, skey.skey, !nq);
	return true;
}

int dat_cond_set_storage_key(struct kvm_s390_mmu_cache *mmc, union asce asce, gfn_t gfn,
			     union skey skey, union skey *oldkey, bool nq, bool mr, bool mc)
{
	union pgste pgste, old;
	union crste *crstep;
	union skey prev;
	union pte *ptep;
	int rc;

	rc = dat_entry_walk(mmc, gfn, asce, DAT_WALK_LEAF_ALLOC, TABLE_TYPE_PAGE_TABLE,
			    &crstep, &ptep);
	if (rc)
		return rc;

	if (!ptep)
		return page_cond_set_storage_key(large_crste_to_phys(*crstep, gfn), skey, oldkey,
						 nq, mr, mc);

	old = pgste_get_lock(ptep);
	pgste = old;

	rc = 1;
	pgste.acc = skey.acc;
	pgste.fp = skey.fp;
	pgste.gc = skey.c;
	pgste.gr = skey.r;

	if (!ptep->h.i) {
		rc = page_cond_set_storage_key(pte_origin(*ptep), skey, &prev, nq, mr, mc);
		pgste.hc |= prev.c;
		pgste.hr |= prev.r;
		prev.c |= old.gc;
		prev.r |= old.gr;
	} else {
		prev.acc = old.acc;
		prev.fp = old.fp;
		prev.c = old.gc;
		prev.r = old.gr;
	}
	if (oldkey)
		*oldkey = prev;

	dat_update_ptep_sd(old, pgste, ptep);
	pgste_set_unlock(ptep, pgste);
	return rc;
}

int dat_reset_reference_bit(union asce asce, gfn_t gfn)
{
	union pgste pgste, old;
	union crste *crstep;
	union pte *ptep;
	int rc;

	rc = dat_entry_walk(NULL, gfn, asce, DAT_WALK_ANY, TABLE_TYPE_PAGE_TABLE, &crstep, &ptep);
	if (rc)
		return rc;

	if (!ptep) {
		union crste crste = READ_ONCE(*crstep);

		if (!crste.h.fc || !crste.s.fc1.pr)
			return 0;
		return page_reset_referenced(large_crste_to_phys(*crstep, gfn));
	}
	old = pgste_get_lock(ptep);
	pgste = old;

	if (!ptep->h.i) {
		rc = page_reset_referenced(pte_origin(*ptep));
		pgste.hr = rc >> 1;
	}
	rc |= (pgste.gr << 1) | pgste.gc;
	pgste.gr = 0;

	dat_update_ptep_sd(old, pgste, ptep);
	pgste_set_unlock(ptep, pgste);
	return rc;
}

static long dat_reset_skeys_pte(union pte *ptep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	union pgste pgste;

	pgste = pgste_get_lock(ptep);
	pgste.acc = 0;
	pgste.fp = 0;
	pgste.gr = 0;
	pgste.gc = 0;
	if (ptep->s.pr)
		page_set_storage_key(pte_origin(*ptep), PAGE_DEFAULT_KEY, 1);
	pgste_set_unlock(ptep, pgste);

	if (need_resched())
		return next;
	return 0;
}

static long dat_reset_skeys_crste(union crste *crstep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	phys_addr_t addr, end, origin = crste_origin_large(*crstep);

	if (!crstep->h.fc || !crstep->s.fc1.pr)
		return 0;

	addr = ((max(gfn, walk->start) - gfn) << PAGE_SHIFT) + origin;
	end = ((min(next, walk->end) - gfn) << PAGE_SHIFT) + origin;
	while (ALIGN(addr + 1, _SEGMENT_SIZE) <= end)
		addr = sske_frame(addr, PAGE_DEFAULT_KEY);
	for ( ; addr < end; addr += PAGE_SIZE)
		page_set_storage_key(addr, PAGE_DEFAULT_KEY, 1);

	if (need_resched())
		return next;
	return 0;
}

long dat_reset_skeys(union asce asce, gfn_t start)
{
	const struct dat_walk_ops ops = {
		.pte_entry = dat_reset_skeys_pte,
		.pmd_entry = dat_reset_skeys_crste,
		.pud_entry = dat_reset_skeys_crste,
	};

	return _dat_walk_gfn_range(start, asce_end(asce), asce, &ops, DAT_WALK_IGN_HOLES, NULL);
}
