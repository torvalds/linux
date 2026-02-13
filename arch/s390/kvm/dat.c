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

int dat_set_asce_limit(struct kvm_s390_mmu_cache *mc, union asce *asce, int newtype)
{
	struct crst_table *table;
	union crste crste;

	while (asce->dt > newtype) {
		table = dereference_asce(*asce);
		crste = table->crstes[0];
		if (crste.h.fc)
			return 0;
		if (!crste.h.i) {
			asce->rsto = crste.h.fc0.to;
			dat_free_crst(table);
		} else {
			crste.h.tt--;
			crst_table_init((void *)table, crste.val);
		}
		asce->dt--;
	}
	while (asce->dt < newtype) {
		crste = _crste_fc0(asce->rsto, asce->dt + 1);
		table = dat_alloc_crst_noinit(mc);
		if (!table)
			return -ENOMEM;
		crst_table_init((void *)table, _CRSTE_HOLE(crste.h.tt).val);
		table->crstes[0] = crste;
		asce->rsto = __pa(table) >> PAGE_SHIFT;
		asce->dt++;
	}
	return 0;
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

struct slot_priv {
	unsigned long token;
	struct kvm_s390_mmu_cache *mc;
};

static long _dat_slot_pte(union pte *ptep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	struct slot_priv *p = walk->priv;
	union crste dummy = { .val = p->token };
	union pte new_pte, pte = READ_ONCE(*ptep);

	new_pte = _PTE_TOK(dummy.tok.type, dummy.tok.par);

	/* Table entry already in the desired state. */
	if (pte.val == new_pte.val)
		return 0;

	dat_ptep_xchg(ptep, new_pte, gfn, walk->asce, false);
	return 0;
}

static long _dat_slot_crste(union crste *crstep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	union crste new_crste, crste = READ_ONCE(*crstep);
	struct slot_priv *p = walk->priv;

	new_crste.val = p->token;
	new_crste.h.tt = crste.h.tt;

	/* Table entry already in the desired state. */
	if (crste.val == new_crste.val)
		return 0;

	/* This table entry needs to be updated. */
	if (walk->start <= gfn && walk->end >= next) {
		dat_crstep_xchg_atomic(crstep, crste, new_crste, gfn, walk->asce);
		/* A lower level table was present, needs to be freed. */
		if (!crste.h.fc && !crste.h.i) {
			if (is_pmd(crste))
				dat_free_pt(dereference_pmd(crste.pmd));
			else
				dat_free_level(dereference_crste(crste), true);
		}
		return 0;
	}

	/* A lower level table is present, things will handled there. */
	if (!crste.h.fc && !crste.h.i)
		return 0;
	/* Split (install a lower level table), and handle things there. */
	return dat_split_crste(p->mc, crstep, gfn, walk->asce, false);
}

static const struct dat_walk_ops dat_slot_ops = {
	.pte_entry = _dat_slot_pte,
	.crste_ops = { _dat_slot_crste, _dat_slot_crste, _dat_slot_crste, _dat_slot_crste, },
};

int dat_set_slot(struct kvm_s390_mmu_cache *mc, union asce asce, gfn_t start, gfn_t end,
		 u16 type, u16 param)
{
	struct slot_priv priv = {
		.token = _CRSTE_TOK(0, type, param).val,
		.mc = mc,
	};

	return _dat_walk_gfn_range(start, end, asce, &dat_slot_ops,
				   DAT_WALK_IGN_HOLES | DAT_WALK_ANY, &priv);
}

static void pgste_set_unlock_multiple(union pte *first, int n, union pgste *pgstes)
{
	int i;

	for (i = 0; i < n; i++) {
		if (!pgstes[i].pcl)
			break;
		pgste_set_unlock(first + i, pgstes[i]);
	}
}

static bool pgste_get_trylock_multiple(union pte *first, int n, union pgste *pgstes)
{
	int i;

	for (i = 0; i < n; i++) {
		if (!pgste_get_trylock(first + i, pgstes + i))
			break;
	}
	if (i == n)
		return true;
	pgste_set_unlock_multiple(first, n, pgstes);
	return false;
}

unsigned long dat_get_ptval(struct page_table *table, struct ptval_param param)
{
	union pgste pgstes[4] = {};
	unsigned long res = 0;
	int i, n;

	n = param.len + 1;

	while (!pgste_get_trylock_multiple(table->ptes + param.offset, n, pgstes))
		cpu_relax();

	for (i = 0; i < n; i++)
		res = res << 16 | pgstes[i].val16;

	pgste_set_unlock_multiple(table->ptes + param.offset, n, pgstes);
	return res;
}

void dat_set_ptval(struct page_table *table, struct ptval_param param, unsigned long val)
{
	union pgste pgstes[4] = {};
	int i, n;

	n = param.len + 1;

	while (!pgste_get_trylock_multiple(table->ptes + param.offset, n, pgstes))
		cpu_relax();

	for (i = param.len; i >= 0; i--) {
		pgstes[i].val16 = val;
		val = val >> 16;
	}

	pgste_set_unlock_multiple(table->ptes + param.offset, n, pgstes);
}

static long _dat_test_young_pte(union pte *ptep, gfn_t start, gfn_t end, struct dat_walk *walk)
{
	return ptep->s.y;
}

static long _dat_test_young_crste(union crste *crstep, gfn_t start, gfn_t end,
				  struct dat_walk *walk)
{
	return crstep->h.fc && crstep->s.fc1.y;
}

static const struct dat_walk_ops test_age_ops = {
	.pte_entry = _dat_test_young_pte,
	.pmd_entry = _dat_test_young_crste,
	.pud_entry = _dat_test_young_crste,
};

/**
 * dat_test_age_gfn() - Test young.
 * @asce: The ASCE whose address range is to be tested.
 * @start: The first guest frame of the range to check.
 * @end: The guest frame after the last in the range.
 *
 * Context: called by KVM common code with the kvm mmu write lock held.
 *
 * Return: %true if any page in the given range is young, otherwise %false.
 */
bool dat_test_age_gfn(union asce asce, gfn_t start, gfn_t end)
{
	return _dat_walk_gfn_range(start, end, asce, &test_age_ops, 0, NULL) > 0;
}

int dat_link(struct kvm_s390_mmu_cache *mc, union asce asce, int level,
	     bool uses_skeys, struct guest_fault *f)
{
	union crste oldval, newval;
	union pte newpte, oldpte;
	union pgste pgste;
	int rc = 0;

	rc = dat_entry_walk(mc, f->gfn, asce, DAT_WALK_ALLOC_CONTINUE, level, &f->crstep, &f->ptep);
	if (rc == -EINVAL || rc == -ENOMEM)
		return rc;
	if (rc)
		return -EAGAIN;

	if (WARN_ON_ONCE(unlikely(get_level(f->crstep, f->ptep) > level)))
		return -EINVAL;

	if (f->ptep) {
		pgste = pgste_get_lock(f->ptep);
		oldpte = *f->ptep;
		newpte = _pte(f->pfn, f->writable, f->write_attempt | oldpte.s.d, !f->page);
		newpte.s.sd = oldpte.s.sd;
		oldpte.s.sd = 0;
		if (oldpte.val == _PTE_EMPTY.val || oldpte.h.pfra == f->pfn) {
			pgste = __dat_ptep_xchg(f->ptep, pgste, newpte, f->gfn, asce, uses_skeys);
			if (f->callback)
				f->callback(f);
		} else {
			rc = -EAGAIN;
		}
		pgste_set_unlock(f->ptep, pgste);
	} else {
		oldval = READ_ONCE(*f->crstep);
		newval = _crste_fc1(f->pfn, oldval.h.tt, f->writable,
				    f->write_attempt | oldval.s.fc1.d);
		newval.s.fc1.sd = oldval.s.fc1.sd;
		if (oldval.val != _CRSTE_EMPTY(oldval.h.tt).val &&
		    crste_origin_large(oldval) != crste_origin_large(newval))
			return -EAGAIN;
		if (!dat_crstep_xchg_atomic(f->crstep, oldval, newval, f->gfn, asce))
			return -EAGAIN;
		if (f->callback)
			f->callback(f);
	}

	return rc;
}

static long dat_set_pn_crste(union crste *crstep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	union crste crste = READ_ONCE(*crstep);
	int *n = walk->priv;

	if (!crste.h.fc || crste.h.i || crste.h.p)
		return 0;

	*n = 2;
	if (crste.s.fc1.prefix_notif)
		return 0;
	crste.s.fc1.prefix_notif = 1;
	dat_crstep_xchg(crstep, crste, gfn, walk->asce);
	return 0;
}

static long dat_set_pn_pte(union pte *ptep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	int *n = walk->priv;
	union pgste pgste;

	pgste = pgste_get_lock(ptep);
	if (!ptep->h.i && !ptep->h.p) {
		pgste.prefix_notif = 1;
		*n += 1;
	}
	pgste_set_unlock(ptep, pgste);
	return 0;
}

int dat_set_prefix_notif_bit(union asce asce, gfn_t gfn)
{
	static const struct dat_walk_ops ops = {
		.pte_entry = dat_set_pn_pte,
		.pmd_entry = dat_set_pn_crste,
		.pud_entry = dat_set_pn_crste,
	};

	int n = 0;

	_dat_walk_gfn_range(gfn, gfn + 2, asce, &ops, DAT_WALK_IGN_HOLES, &n);
	if (n != 2)
		return -EAGAIN;
	return 0;
}

/**
 * dat_perform_essa() - Perform ESSA actions on the PGSTE.
 * @asce: The asce to operate on.
 * @gfn: The guest page frame to operate on.
 * @orc: The specific action to perform, see the ESSA_SET_* macros.
 * @state: The storage attributes to be returned to the guest.
 * @dirty: Returns whether the function dirtied a previously clean entry.
 *
 * Context: Called with kvm->mmu_lock held.
 *
 * Return:
 * * %1 if the page state has been altered and the page is to be added to the CBRL
 * * %0 if the page state has been altered, but the page is not to be added to the CBRL
 * * %-1 if the page state has not been altered and the page is not to be added to the CBRL
 */
int dat_perform_essa(union asce asce, gfn_t gfn, int orc, union essa_state *state, bool *dirty)
{
	union crste *crstep;
	union pgste pgste;
	union pte *ptep;
	int res = 0;

	if (dat_entry_walk(NULL, gfn, asce, 0, TABLE_TYPE_PAGE_TABLE, &crstep, &ptep)) {
		*state = (union essa_state) { .exception = 1 };
		return -1;
	}

	pgste = pgste_get_lock(ptep);

	*state = (union essa_state) {
		.content = (ptep->h.i << 1) + (ptep->h.i && pgste.zero),
		.nodat = pgste.nodat,
		.usage = pgste.usage,
		};

	switch (orc) {
	case ESSA_GET_STATE:
		res = -1;
		break;
	case ESSA_SET_STABLE:
		pgste.usage = PGSTE_GPS_USAGE_STABLE;
		pgste.nodat = 0;
		break;
	case ESSA_SET_UNUSED:
		pgste.usage = PGSTE_GPS_USAGE_UNUSED;
		if (ptep->h.i)
			res = 1;
		break;
	case ESSA_SET_VOLATILE:
		pgste.usage = PGSTE_GPS_USAGE_VOLATILE;
		if (ptep->h.i)
			res = 1;
		break;
	case ESSA_SET_POT_VOLATILE:
		if (!ptep->h.i) {
			pgste.usage = PGSTE_GPS_USAGE_POT_VOLATILE;
		} else if (pgste.zero) {
			pgste.usage = PGSTE_GPS_USAGE_VOLATILE;
		} else if (!pgste.gc) {
			pgste.usage = PGSTE_GPS_USAGE_VOLATILE;
			res = 1;
		}
		break;
	case ESSA_SET_STABLE_RESIDENT:
		pgste.usage = PGSTE_GPS_USAGE_STABLE;
		/*
		 * Since the resident state can go away any time after this
		 * call, we will not make this page resident. We can revisit
		 * this decision if a guest will ever start using this.
		 */
		break;
	case ESSA_SET_STABLE_IF_RESIDENT:
		if (!ptep->h.i)
			pgste.usage = PGSTE_GPS_USAGE_STABLE;
		break;
	case ESSA_SET_STABLE_NODAT:
		pgste.usage = PGSTE_GPS_USAGE_STABLE;
		pgste.nodat = 1;
		break;
	default:
		WARN_ONCE(1, "Invalid ORC!");
		res = -1;
		break;
	}
	/* If we are discarding a page, set it to logical zero. */
	pgste.zero = res == 1;
	if (orc > 0) {
		*dirty = !pgste.cmma_d;
		pgste.cmma_d = 1;
	}

	pgste_set_unlock(ptep, pgste);

	return res;
}

static long dat_reset_cmma_pte(union pte *ptep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	union pgste pgste;

	pgste = pgste_get_lock(ptep);
	pgste.usage = 0;
	pgste.nodat = 0;
	pgste.cmma_d = 0;
	pgste_set_unlock(ptep, pgste);
	if (need_resched())
		return next;
	return 0;
}

long dat_reset_cmma(union asce asce, gfn_t start)
{
	const struct dat_walk_ops dat_reset_cmma_ops = {
		.pte_entry = dat_reset_cmma_pte,
	};

	return _dat_walk_gfn_range(start, asce_end(asce), asce, &dat_reset_cmma_ops,
				   DAT_WALK_IGN_HOLES, NULL);
}

struct dat_get_cmma_state {
	gfn_t start;
	gfn_t end;
	unsigned int count;
	u8 *values;
	atomic64_t *remaining;
};

static long __dat_peek_cmma_pte(union pte *ptep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	struct dat_get_cmma_state *state = walk->priv;
	union pgste pgste;

	pgste = pgste_get_lock(ptep);
	state->values[gfn - walk->start] = pgste.usage | (pgste.nodat << 6);
	pgste_set_unlock(ptep, pgste);
	state->end = next;

	return 0;
}

static long __dat_peek_cmma_crste(union crste *crstep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	struct dat_get_cmma_state *state = walk->priv;

	if (crstep->h.i)
		state->end = min(walk->end, next);
	return 0;
}

int dat_peek_cmma(gfn_t start, union asce asce, unsigned int *count, u8 *values)
{
	const struct dat_walk_ops ops = {
		.pte_entry = __dat_peek_cmma_pte,
		.pmd_entry = __dat_peek_cmma_crste,
		.pud_entry = __dat_peek_cmma_crste,
		.p4d_entry = __dat_peek_cmma_crste,
		.pgd_entry = __dat_peek_cmma_crste,
	};
	struct dat_get_cmma_state state = { .values = values, };
	int rc;

	rc = _dat_walk_gfn_range(start, start + *count, asce, &ops, DAT_WALK_DEFAULT, &state);
	*count = state.end - start;
	/* Return success if at least one value was saved, otherwise an error. */
	return (rc == -EFAULT && *count > 0) ? 0 : rc;
}

static long __dat_get_cmma_pte(union pte *ptep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	struct dat_get_cmma_state *state = walk->priv;
	union pgste pgste;

	if (state->start != -1) {
		if ((gfn - state->end) > KVM_S390_MAX_BIT_DISTANCE)
			return 1;
		if (gfn - state->start >= state->count)
			return 1;
	}

	if (!READ_ONCE(*pgste_of(ptep)).cmma_d)
		return 0;

	pgste = pgste_get_lock(ptep);
	if (pgste.cmma_d) {
		if (state->start == -1)
			state->start = gfn;
		pgste.cmma_d = 0;
		atomic64_dec(state->remaining);
		state->values[gfn - state->start] = pgste.usage | pgste.nodat << 6;
		state->end = next;
	}
	pgste_set_unlock(ptep, pgste);
	return 0;
}

int dat_get_cmma(union asce asce, gfn_t *start, unsigned int *count, u8 *values, atomic64_t *rem)
{
	const struct dat_walk_ops ops = { .pte_entry = __dat_get_cmma_pte, };
	struct dat_get_cmma_state state = {
		.remaining = rem,
		.values = values,
		.count = *count,
		.start = -1,
	};

	_dat_walk_gfn_range(*start, asce_end(asce), asce, &ops, DAT_WALK_IGN_HOLES, &state);

	if (state.start == -1) {
		*count = 0;
	} else {
		*count = state.end - state.start;
		*start = state.start;
	}

	return 0;
}

struct dat_set_cmma_state {
	unsigned long mask;
	const u8 *bits;
};

static long __dat_set_cmma_pte(union pte *ptep, gfn_t gfn, gfn_t next, struct dat_walk *walk)
{
	struct dat_set_cmma_state *state = walk->priv;
	union pgste pgste, tmp;

	tmp.val = (state->bits[gfn - walk->start] << 24) & state->mask;

	pgste = pgste_get_lock(ptep);
	pgste.usage = tmp.usage;
	pgste.nodat = tmp.nodat;
	pgste_set_unlock(ptep, pgste);

	return 0;
}

/**
 * dat_set_cmma_bits() - Set CMMA bits for a range of guest pages.
 * @mc: Cache used for allocations.
 * @asce: The ASCE of the guest.
 * @gfn: The guest frame of the fist page whose CMMA bits are to set.
 * @count: How many pages need to be processed.
 * @mask: Which PGSTE bits should be set.
 * @bits: Points to an array with the CMMA attributes.
 *
 * This function sets the CMMA attributes for the given pages. If the input
 * buffer has zero length, no action is taken, otherwise the attributes are
 * set and the mm->context.uses_cmm flag is set.
 *
 * Each byte in @bits contains new values for bits 32-39 of the PGSTE.
 * Currently, only the fields NT and US are applied.
 *
 * Return: %0 in case of success, a negative error value otherwise.
 */
int dat_set_cmma_bits(struct kvm_s390_mmu_cache *mc, union asce asce, gfn_t gfn,
		      unsigned long count, unsigned long mask, const uint8_t *bits)
{
	const struct dat_walk_ops ops = { .pte_entry = __dat_set_cmma_pte, };
	struct dat_set_cmma_state state = { .mask = mask, .bits = bits, };
	union crste *crstep;
	union pte *ptep;
	gfn_t cur;
	int rc;

	for (cur = ALIGN_DOWN(gfn, _PAGE_ENTRIES); cur < gfn + count; cur += _PAGE_ENTRIES) {
		rc = dat_entry_walk(mc, cur, asce, DAT_WALK_ALLOC, TABLE_TYPE_PAGE_TABLE,
				    &crstep, &ptep);
		if (rc)
			return rc;
	}
	return _dat_walk_gfn_range(gfn, gfn + count, asce, &ops, DAT_WALK_IGN_HOLES, &state);
}
