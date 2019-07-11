/*
 * pSeries_lpar.c
 * Copyright (C) 2001 Todd Inglett, IBM Corporation
 *
 * pSeries LPAR support.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

/* Enables debugging of low-level hash table routines - careful! */
#undef DEBUG
#define pr_fmt(fmt) "lpar: " fmt

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/console.h>
#include <linux/export.h>
#include <linux/jump_label.h>
#include <linux/delay.h>
#include <linux/stop_machine.h>
#include <asm/processor.h>
#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>
#include <asm/mmu_context.h>
#include <asm/iommu.h>
#include <asm/tlb.h>
#include <asm/prom.h>
#include <asm/cputable.h>
#include <asm/udbg.h>
#include <asm/smp.h>
#include <asm/trace.h>
#include <asm/firmware.h>
#include <asm/plpar_wrappers.h>
#include <asm/kexec.h>
#include <asm/fadump.h>
#include <asm/asm-prototypes.h>
#include <asm/debugfs.h>

#include "pseries.h"

/* Flag bits for H_BULK_REMOVE */
#define HBR_REQUEST	0x4000000000000000UL
#define HBR_RESPONSE	0x8000000000000000UL
#define HBR_END		0xc000000000000000UL
#define HBR_AVPN	0x0200000000000000UL
#define HBR_ANDCOND	0x0100000000000000UL


/* in hvCall.S */
EXPORT_SYMBOL(plpar_hcall);
EXPORT_SYMBOL(plpar_hcall9);
EXPORT_SYMBOL(plpar_hcall_norets);

void vpa_init(int cpu)
{
	int hwcpu = get_hard_smp_processor_id(cpu);
	unsigned long addr;
	long ret;
	struct paca_struct *pp;
	struct dtl_entry *dtl;

	/*
	 * The spec says it "may be problematic" if CPU x registers the VPA of
	 * CPU y. We should never do that, but wail if we ever do.
	 */
	WARN_ON(cpu != smp_processor_id());

	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		lppaca_of(cpu).vmxregs_in_use = 1;

	if (cpu_has_feature(CPU_FTR_ARCH_207S))
		lppaca_of(cpu).ebb_regs_in_use = 1;

	addr = __pa(&lppaca_of(cpu));
	ret = register_vpa(hwcpu, addr);

	if (ret) {
		pr_err("WARNING: VPA registration for cpu %d (hw %d) of area "
		       "%lx failed with %ld\n", cpu, hwcpu, addr, ret);
		return;
	}

#ifdef CONFIG_PPC_BOOK3S_64
	/*
	 * PAPR says this feature is SLB-Buffer but firmware never
	 * reports that.  All SPLPAR support SLB shadow buffer.
	 */
	if (!radix_enabled() && firmware_has_feature(FW_FEATURE_SPLPAR)) {
		addr = __pa(paca_ptrs[cpu]->slb_shadow_ptr);
		ret = register_slb_shadow(hwcpu, addr);
		if (ret)
			pr_err("WARNING: SLB shadow buffer registration for "
			       "cpu %d (hw %d) of area %lx failed with %ld\n",
			       cpu, hwcpu, addr, ret);
	}
#endif /* CONFIG_PPC_BOOK3S_64 */

	/*
	 * Register dispatch trace log, if one has been allocated.
	 */
	pp = paca_ptrs[cpu];
	dtl = pp->dispatch_log;
	if (dtl) {
		pp->dtl_ridx = 0;
		pp->dtl_curr = dtl;
		lppaca_of(cpu).dtl_idx = 0;

		/* hypervisor reads buffer length from this field */
		dtl->enqueue_to_dispatch_time = cpu_to_be32(DISPATCH_LOG_BYTES);
		ret = register_dtl(hwcpu, __pa(dtl));
		if (ret)
			pr_err("WARNING: DTL registration of cpu %d (hw %d) "
			       "failed with %ld\n", smp_processor_id(),
			       hwcpu, ret);
		lppaca_of(cpu).dtl_enable_mask = 2;
	}
}

#ifdef CONFIG_PPC_BOOK3S_64

static long pSeries_lpar_hpte_insert(unsigned long hpte_group,
				     unsigned long vpn, unsigned long pa,
				     unsigned long rflags, unsigned long vflags,
				     int psize, int apsize, int ssize)
{
	unsigned long lpar_rc;
	unsigned long flags;
	unsigned long slot;
	unsigned long hpte_v, hpte_r;

	if (!(vflags & HPTE_V_BOLTED))
		pr_devel("hpte_insert(group=%lx, vpn=%016lx, "
			 "pa=%016lx, rflags=%lx, vflags=%lx, psize=%d)\n",
			 hpte_group, vpn,  pa, rflags, vflags, psize);

	hpte_v = hpte_encode_v(vpn, psize, apsize, ssize) | vflags | HPTE_V_VALID;
	hpte_r = hpte_encode_r(pa, psize, apsize) | rflags;

	if (!(vflags & HPTE_V_BOLTED))
		pr_devel(" hpte_v=%016lx, hpte_r=%016lx\n", hpte_v, hpte_r);

	/* Now fill in the actual HPTE */
	/* Set CEC cookie to 0         */
	/* Zero page = 0               */
	/* I-cache Invalidate = 0      */
	/* I-cache synchronize = 0     */
	/* Exact = 0                   */
	flags = 0;

	if (firmware_has_feature(FW_FEATURE_XCMO) && !(hpte_r & HPTE_R_N))
		flags |= H_COALESCE_CAND;

	lpar_rc = plpar_pte_enter(flags, hpte_group, hpte_v, hpte_r, &slot);
	if (unlikely(lpar_rc == H_PTEG_FULL)) {
		pr_devel("Hash table group is full\n");
		return -1;
	}

	/*
	 * Since we try and ioremap PHBs we don't own, the pte insert
	 * will fail. However we must catch the failure in hash_page
	 * or we will loop forever, so return -2 in this case.
	 */
	if (unlikely(lpar_rc != H_SUCCESS)) {
		pr_err("Failed hash pte insert with error %ld\n", lpar_rc);
		return -2;
	}
	if (!(vflags & HPTE_V_BOLTED))
		pr_devel(" -> slot: %lu\n", slot & 7);

	/* Because of iSeries, we have to pass down the secondary
	 * bucket bit here as well
	 */
	return (slot & 7) | (!!(vflags & HPTE_V_SECONDARY) << 3);
}

static DEFINE_SPINLOCK(pSeries_lpar_tlbie_lock);

static long pSeries_lpar_hpte_remove(unsigned long hpte_group)
{
	unsigned long slot_offset;
	unsigned long lpar_rc;
	int i;
	unsigned long dummy1, dummy2;

	/* pick a random slot to start at */
	slot_offset = mftb() & 0x7;

	for (i = 0; i < HPTES_PER_GROUP; i++) {

		/* don't remove a bolted entry */
		lpar_rc = plpar_pte_remove(H_ANDCOND, hpte_group + slot_offset,
					   (0x1UL << 4), &dummy1, &dummy2);
		if (lpar_rc == H_SUCCESS)
			return i;

		/*
		 * The test for adjunct partition is performed before the
		 * ANDCOND test.  H_RESOURCE may be returned, so we need to
		 * check for that as well.
		 */
		BUG_ON(lpar_rc != H_NOT_FOUND && lpar_rc != H_RESOURCE);

		slot_offset++;
		slot_offset &= 0x7;
	}

	return -1;
}

static void manual_hpte_clear_all(void)
{
	unsigned long size_bytes = 1UL << ppc64_pft_size;
	unsigned long hpte_count = size_bytes >> 4;
	struct {
		unsigned long pteh;
		unsigned long ptel;
	} ptes[4];
	long lpar_rc;
	unsigned long i, j;

	/* Read in batches of 4,
	 * invalidate only valid entries not in the VRMA
	 * hpte_count will be a multiple of 4
         */
	for (i = 0; i < hpte_count; i += 4) {
		lpar_rc = plpar_pte_read_4_raw(0, i, (void *)ptes);
		if (lpar_rc != H_SUCCESS) {
			pr_info("Failed to read hash page table at %ld err %ld\n",
				i, lpar_rc);
			continue;
		}
		for (j = 0; j < 4; j++){
			if ((ptes[j].pteh & HPTE_V_VRMA_MASK) ==
				HPTE_V_VRMA_MASK)
				continue;
			if (ptes[j].pteh & HPTE_V_VALID)
				plpar_pte_remove_raw(0, i + j, 0,
					&(ptes[j].pteh), &(ptes[j].ptel));
		}
	}
}

static int hcall_hpte_clear_all(void)
{
	int rc;

	do {
		rc = plpar_hcall_norets(H_CLEAR_HPT);
	} while (rc == H_CONTINUE);

	return rc;
}

static void pseries_hpte_clear_all(void)
{
	int rc;

	rc = hcall_hpte_clear_all();
	if (rc != H_SUCCESS)
		manual_hpte_clear_all();

#ifdef __LITTLE_ENDIAN__
	/*
	 * Reset exceptions to big endian.
	 *
	 * FIXME this is a hack for kexec, we need to reset the exception
	 * endian before starting the new kernel and this is a convenient place
	 * to do it.
	 *
	 * This is also called on boot when a fadump happens. In that case we
	 * must not change the exception endian mode.
	 */
	if (firmware_has_feature(FW_FEATURE_SET_MODE) && !is_fadump_active())
		pseries_big_endian_exceptions();
#endif
}

/*
 * NOTE: for updatepp ops we are fortunate that the linux "newpp" bits and
 * the low 3 bits of flags happen to line up.  So no transform is needed.
 * We can probably optimize here and assume the high bits of newpp are
 * already zero.  For now I am paranoid.
 */
static long pSeries_lpar_hpte_updatepp(unsigned long slot,
				       unsigned long newpp,
				       unsigned long vpn,
				       int psize, int apsize,
				       int ssize, unsigned long inv_flags)
{
	unsigned long lpar_rc;
	unsigned long flags;
	unsigned long want_v;

	want_v = hpte_encode_avpn(vpn, psize, ssize);

	flags = (newpp & 7) | H_AVPN;
	if (mmu_has_feature(MMU_FTR_KERNEL_RO))
		/* Move pp0 into bit 8 (IBM 55) */
		flags |= (newpp & HPTE_R_PP0) >> 55;

	pr_devel("    update: avpnv=%016lx, hash=%016lx, f=%lx, psize: %d ...",
		 want_v, slot, flags, psize);

	lpar_rc = plpar_pte_protect(flags, slot, want_v);

	if (lpar_rc == H_NOT_FOUND) {
		pr_devel("not found !\n");
		return -1;
	}

	pr_devel("ok\n");

	BUG_ON(lpar_rc != H_SUCCESS);

	return 0;
}

static long __pSeries_lpar_hpte_find(unsigned long want_v, unsigned long hpte_group)
{
	long lpar_rc;
	unsigned long i, j;
	struct {
		unsigned long pteh;
		unsigned long ptel;
	} ptes[4];

	for (i = 0; i < HPTES_PER_GROUP; i += 4, hpte_group += 4) {

		lpar_rc = plpar_pte_read_4(0, hpte_group, (void *)ptes);
		if (lpar_rc != H_SUCCESS) {
			pr_info("Failed to read hash page table at %ld err %ld\n",
				hpte_group, lpar_rc);
			continue;
		}

		for (j = 0; j < 4; j++) {
			if (HPTE_V_COMPARE(ptes[j].pteh, want_v) &&
			    (ptes[j].pteh & HPTE_V_VALID))
				return i + j;
		}
	}

	return -1;
}

static long pSeries_lpar_hpte_find(unsigned long vpn, int psize, int ssize)
{
	long slot;
	unsigned long hash;
	unsigned long want_v;
	unsigned long hpte_group;

	hash = hpt_hash(vpn, mmu_psize_defs[psize].shift, ssize);
	want_v = hpte_encode_avpn(vpn, psize, ssize);

	/* Bolted entries are always in the primary group */
	hpte_group = (hash & htab_hash_mask) * HPTES_PER_GROUP;
	slot = __pSeries_lpar_hpte_find(want_v, hpte_group);
	if (slot < 0)
		return -1;
	return hpte_group + slot;
}

static void pSeries_lpar_hpte_updateboltedpp(unsigned long newpp,
					     unsigned long ea,
					     int psize, int ssize)
{
	unsigned long vpn;
	unsigned long lpar_rc, slot, vsid, flags;

	vsid = get_kernel_vsid(ea, ssize);
	vpn = hpt_vpn(ea, vsid, ssize);

	slot = pSeries_lpar_hpte_find(vpn, psize, ssize);
	BUG_ON(slot == -1);

	flags = newpp & 7;
	if (mmu_has_feature(MMU_FTR_KERNEL_RO))
		/* Move pp0 into bit 8 (IBM 55) */
		flags |= (newpp & HPTE_R_PP0) >> 55;

	lpar_rc = plpar_pte_protect(flags, slot, 0);

	BUG_ON(lpar_rc != H_SUCCESS);
}

static void pSeries_lpar_hpte_invalidate(unsigned long slot, unsigned long vpn,
					 int psize, int apsize,
					 int ssize, int local)
{
	unsigned long want_v;
	unsigned long lpar_rc;
	unsigned long dummy1, dummy2;

	pr_devel("    inval : slot=%lx, vpn=%016lx, psize: %d, local: %d\n",
		 slot, vpn, psize, local);

	want_v = hpte_encode_avpn(vpn, psize, ssize);
	lpar_rc = plpar_pte_remove(H_AVPN, slot, want_v, &dummy1, &dummy2);
	if (lpar_rc == H_NOT_FOUND)
		return;

	BUG_ON(lpar_rc != H_SUCCESS);
}


/*
 * As defined in the PAPR's section 14.5.4.1.8
 * The control mask doesn't include the returned reference and change bit from
 * the processed PTE.
 */
#define HBLKR_AVPN		0x0100000000000000UL
#define HBLKR_CTRL_MASK		0xf800000000000000UL
#define HBLKR_CTRL_SUCCESS	0x8000000000000000UL
#define HBLKR_CTRL_ERRNOTFOUND	0x8800000000000000UL
#define HBLKR_CTRL_ERRBUSY	0xa000000000000000UL

/**
 * H_BLOCK_REMOVE caller.
 * @idx should point to the latest @param entry set with a PTEX.
 * If PTE cannot be processed because another CPUs has already locked that
 * group, those entries are put back in @param starting at index 1.
 * If entries has to be retried and @retry_busy is set to true, these entries
 * are retried until success. If @retry_busy is set to false, the returned
 * is the number of entries yet to process.
 */
static unsigned long call_block_remove(unsigned long idx, unsigned long *param,
				       bool retry_busy)
{
	unsigned long i, rc, new_idx;
	unsigned long retbuf[PLPAR_HCALL9_BUFSIZE];

	if (idx < 2) {
		pr_warn("Unexpected empty call to H_BLOCK_REMOVE");
		return 0;
	}
again:
	new_idx = 0;
	if (idx > PLPAR_HCALL9_BUFSIZE) {
		pr_err("Too many PTEs (%lu) for H_BLOCK_REMOVE", idx);
		idx = PLPAR_HCALL9_BUFSIZE;
	} else if (idx < PLPAR_HCALL9_BUFSIZE)
		param[idx] = HBR_END;

	rc = plpar_hcall9(H_BLOCK_REMOVE, retbuf,
			  param[0], /* AVA */
			  param[1],  param[2],  param[3],  param[4], /* TS0-7 */
			  param[5],  param[6],  param[7],  param[8]);
	if (rc == H_SUCCESS)
		return 0;

	BUG_ON(rc != H_PARTIAL);

	/* Check that the unprocessed entries were 'not found' or 'busy' */
	for (i = 0; i < idx-1; i++) {
		unsigned long ctrl = retbuf[i] & HBLKR_CTRL_MASK;

		if (ctrl == HBLKR_CTRL_ERRBUSY) {
			param[++new_idx] = param[i+1];
			continue;
		}

		BUG_ON(ctrl != HBLKR_CTRL_SUCCESS
		       && ctrl != HBLKR_CTRL_ERRNOTFOUND);
	}

	/*
	 * If there were entries found busy, retry these entries if requested,
	 * of if all the entries have to be retried.
	 */
	if (new_idx && (retry_busy || new_idx == (PLPAR_HCALL9_BUFSIZE-1))) {
		idx = new_idx + 1;
		goto again;
	}

	return new_idx;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
/*
 * Limit iterations holding pSeries_lpar_tlbie_lock to 3. We also need
 * to make sure that we avoid bouncing the hypervisor tlbie lock.
 */
#define PPC64_HUGE_HPTE_BATCH 12

static void hugepage_block_invalidate(unsigned long *slot, unsigned long *vpn,
				      int count, int psize, int ssize)
{
	unsigned long param[PLPAR_HCALL9_BUFSIZE];
	unsigned long shift, current_vpgb, vpgb;
	int i, pix = 0;

	shift = mmu_psize_defs[psize].shift;

	for (i = 0; i < count; i++) {
		/*
		 * Shifting 3 bits more on the right to get a
		 * 8 pages aligned virtual addresse.
		 */
		vpgb = (vpn[i] >> (shift - VPN_SHIFT + 3));
		if (!pix || vpgb != current_vpgb) {
			/*
			 * Need to start a new 8 pages block, flush
			 * the current one if needed.
			 */
			if (pix)
				(void)call_block_remove(pix, param, true);
			current_vpgb = vpgb;
			param[0] = hpte_encode_avpn(vpn[i], psize, ssize);
			pix = 1;
		}

		param[pix++] = HBR_REQUEST | HBLKR_AVPN | slot[i];
		if (pix == PLPAR_HCALL9_BUFSIZE) {
			pix = call_block_remove(pix, param, false);
			/*
			 * pix = 0 means that all the entries were
			 * removed, we can start a new block.
			 * Otherwise, this means that there are entries
			 * to retry, and pix points to latest one, so
			 * we should increment it and try to continue
			 * the same block.
			 */
			if (pix)
				pix++;
		}
	}
	if (pix)
		(void)call_block_remove(pix, param, true);
}

static void hugepage_bulk_invalidate(unsigned long *slot, unsigned long *vpn,
				     int count, int psize, int ssize)
{
	unsigned long param[PLPAR_HCALL9_BUFSIZE];
	int i = 0, pix = 0, rc;

	for (i = 0; i < count; i++) {

		if (!firmware_has_feature(FW_FEATURE_BULK_REMOVE)) {
			pSeries_lpar_hpte_invalidate(slot[i], vpn[i], psize, 0,
						     ssize, 0);
		} else {
			param[pix] = HBR_REQUEST | HBR_AVPN | slot[i];
			param[pix+1] = hpte_encode_avpn(vpn[i], psize, ssize);
			pix += 2;
			if (pix == 8) {
				rc = plpar_hcall9(H_BULK_REMOVE, param,
						  param[0], param[1], param[2],
						  param[3], param[4], param[5],
						  param[6], param[7]);
				BUG_ON(rc != H_SUCCESS);
				pix = 0;
			}
		}
	}
	if (pix) {
		param[pix] = HBR_END;
		rc = plpar_hcall9(H_BULK_REMOVE, param, param[0], param[1],
				  param[2], param[3], param[4], param[5],
				  param[6], param[7]);
		BUG_ON(rc != H_SUCCESS);
	}
}

static inline void __pSeries_lpar_hugepage_invalidate(unsigned long *slot,
						      unsigned long *vpn,
						      int count, int psize,
						      int ssize)
{
	unsigned long flags = 0;
	int lock_tlbie = !mmu_has_feature(MMU_FTR_LOCKLESS_TLBIE);

	if (lock_tlbie)
		spin_lock_irqsave(&pSeries_lpar_tlbie_lock, flags);

	if (firmware_has_feature(FW_FEATURE_BLOCK_REMOVE))
		hugepage_block_invalidate(slot, vpn, count, psize, ssize);
	else
		hugepage_bulk_invalidate(slot, vpn, count, psize, ssize);

	if (lock_tlbie)
		spin_unlock_irqrestore(&pSeries_lpar_tlbie_lock, flags);
}

static void pSeries_lpar_hugepage_invalidate(unsigned long vsid,
					     unsigned long addr,
					     unsigned char *hpte_slot_array,
					     int psize, int ssize, int local)
{
	int i, index = 0;
	unsigned long s_addr = addr;
	unsigned int max_hpte_count, valid;
	unsigned long vpn_array[PPC64_HUGE_HPTE_BATCH];
	unsigned long slot_array[PPC64_HUGE_HPTE_BATCH];
	unsigned long shift, hidx, vpn = 0, hash, slot;

	shift = mmu_psize_defs[psize].shift;
	max_hpte_count = 1U << (PMD_SHIFT - shift);

	for (i = 0; i < max_hpte_count; i++) {
		valid = hpte_valid(hpte_slot_array, i);
		if (!valid)
			continue;
		hidx =  hpte_hash_index(hpte_slot_array, i);

		/* get the vpn */
		addr = s_addr + (i * (1ul << shift));
		vpn = hpt_vpn(addr, vsid, ssize);
		hash = hpt_hash(vpn, shift, ssize);
		if (hidx & _PTEIDX_SECONDARY)
			hash = ~hash;

		slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
		slot += hidx & _PTEIDX_GROUP_IX;

		slot_array[index] = slot;
		vpn_array[index] = vpn;
		if (index == PPC64_HUGE_HPTE_BATCH - 1) {
			/*
			 * Now do a bluk invalidate
			 */
			__pSeries_lpar_hugepage_invalidate(slot_array,
							   vpn_array,
							   PPC64_HUGE_HPTE_BATCH,
							   psize, ssize);
			index = 0;
		} else
			index++;
	}
	if (index)
		__pSeries_lpar_hugepage_invalidate(slot_array, vpn_array,
						   index, psize, ssize);
}
#else
static void pSeries_lpar_hugepage_invalidate(unsigned long vsid,
					     unsigned long addr,
					     unsigned char *hpte_slot_array,
					     int psize, int ssize, int local)
{
	WARN(1, "%s called without THP support\n", __func__);
}
#endif

static int pSeries_lpar_hpte_removebolted(unsigned long ea,
					  int psize, int ssize)
{
	unsigned long vpn;
	unsigned long slot, vsid;

	vsid = get_kernel_vsid(ea, ssize);
	vpn = hpt_vpn(ea, vsid, ssize);

	slot = pSeries_lpar_hpte_find(vpn, psize, ssize);
	if (slot == -1)
		return -ENOENT;

	/*
	 * lpar doesn't use the passed actual page size
	 */
	pSeries_lpar_hpte_invalidate(slot, vpn, psize, 0, ssize, 0);
	return 0;
}


static inline unsigned long compute_slot(real_pte_t pte,
					 unsigned long vpn,
					 unsigned long index,
					 unsigned long shift,
					 int ssize)
{
	unsigned long slot, hash, hidx;

	hash = hpt_hash(vpn, shift, ssize);
	hidx = __rpte_to_hidx(pte, index);
	if (hidx & _PTEIDX_SECONDARY)
		hash = ~hash;
	slot = (hash & htab_hash_mask) * HPTES_PER_GROUP;
	slot += hidx & _PTEIDX_GROUP_IX;
	return slot;
}

/**
 * The hcall H_BLOCK_REMOVE implies that the virtual pages to processed are
 * "all within the same naturally aligned 8 page virtual address block".
 */
static void do_block_remove(unsigned long number, struct ppc64_tlb_batch *batch,
			    unsigned long *param)
{
	unsigned long vpn;
	unsigned long i, pix = 0;
	unsigned long index, shift, slot, current_vpgb, vpgb;
	real_pte_t pte;
	int psize, ssize;

	psize = batch->psize;
	ssize = batch->ssize;

	for (i = 0; i < number; i++) {
		vpn = batch->vpn[i];
		pte = batch->pte[i];
		pte_iterate_hashed_subpages(pte, psize, vpn, index, shift) {
			/*
			 * Shifting 3 bits more on the right to get a
			 * 8 pages aligned virtual addresse.
			 */
			vpgb = (vpn >> (shift - VPN_SHIFT + 3));
			if (!pix || vpgb != current_vpgb) {
				/*
				 * Need to start a new 8 pages block, flush
				 * the current one if needed.
				 */
				if (pix)
					(void)call_block_remove(pix, param,
								true);
				current_vpgb = vpgb;
				param[0] = hpte_encode_avpn(vpn, psize,
							    ssize);
				pix = 1;
			}

			slot = compute_slot(pte, vpn, index, shift, ssize);
			param[pix++] = HBR_REQUEST | HBLKR_AVPN | slot;

			if (pix == PLPAR_HCALL9_BUFSIZE) {
				pix = call_block_remove(pix, param, false);
				/*
				 * pix = 0 means that all the entries were
				 * removed, we can start a new block.
				 * Otherwise, this means that there are entries
				 * to retry, and pix points to latest one, so
				 * we should increment it and try to continue
				 * the same block.
				 */
				if (pix)
					pix++;
			}
		} pte_iterate_hashed_end();
	}

	if (pix)
		(void)call_block_remove(pix, param, true);
}

/*
 * Take a spinlock around flushes to avoid bouncing the hypervisor tlbie
 * lock.
 */
static void pSeries_lpar_flush_hash_range(unsigned long number, int local)
{
	unsigned long vpn;
	unsigned long i, pix, rc;
	unsigned long flags = 0;
	struct ppc64_tlb_batch *batch = this_cpu_ptr(&ppc64_tlb_batch);
	int lock_tlbie = !mmu_has_feature(MMU_FTR_LOCKLESS_TLBIE);
	unsigned long param[PLPAR_HCALL9_BUFSIZE];
	unsigned long index, shift, slot;
	real_pte_t pte;
	int psize, ssize;

	if (lock_tlbie)
		spin_lock_irqsave(&pSeries_lpar_tlbie_lock, flags);

	if (firmware_has_feature(FW_FEATURE_BLOCK_REMOVE)) {
		do_block_remove(number, batch, param);
		goto out;
	}

	psize = batch->psize;
	ssize = batch->ssize;
	pix = 0;
	for (i = 0; i < number; i++) {
		vpn = batch->vpn[i];
		pte = batch->pte[i];
		pte_iterate_hashed_subpages(pte, psize, vpn, index, shift) {
			slot = compute_slot(pte, vpn, index, shift, ssize);
			if (!firmware_has_feature(FW_FEATURE_BULK_REMOVE)) {
				/*
				 * lpar doesn't use the passed actual page size
				 */
				pSeries_lpar_hpte_invalidate(slot, vpn, psize,
							     0, ssize, local);
			} else {
				param[pix] = HBR_REQUEST | HBR_AVPN | slot;
				param[pix+1] = hpte_encode_avpn(vpn, psize,
								ssize);
				pix += 2;
				if (pix == 8) {
					rc = plpar_hcall9(H_BULK_REMOVE, param,
						param[0], param[1], param[2],
						param[3], param[4], param[5],
						param[6], param[7]);
					BUG_ON(rc != H_SUCCESS);
					pix = 0;
				}
			}
		} pte_iterate_hashed_end();
	}
	if (pix) {
		param[pix] = HBR_END;
		rc = plpar_hcall9(H_BULK_REMOVE, param, param[0], param[1],
				  param[2], param[3], param[4], param[5],
				  param[6], param[7]);
		BUG_ON(rc != H_SUCCESS);
	}

out:
	if (lock_tlbie)
		spin_unlock_irqrestore(&pSeries_lpar_tlbie_lock, flags);
}

static int __init disable_bulk_remove(char *str)
{
	if (strcmp(str, "off") == 0 &&
	    firmware_has_feature(FW_FEATURE_BULK_REMOVE)) {
		pr_info("Disabling BULK_REMOVE firmware feature");
		powerpc_firmware_features &= ~FW_FEATURE_BULK_REMOVE;
	}
	return 1;
}

__setup("bulk_remove=", disable_bulk_remove);

#define HPT_RESIZE_TIMEOUT	10000 /* ms */

struct hpt_resize_state {
	unsigned long shift;
	int commit_rc;
};

static int pseries_lpar_resize_hpt_commit(void *data)
{
	struct hpt_resize_state *state = data;

	state->commit_rc = plpar_resize_hpt_commit(0, state->shift);
	if (state->commit_rc != H_SUCCESS)
		return -EIO;

	/* Hypervisor has transitioned the HTAB, update our globals */
	ppc64_pft_size = state->shift;
	htab_size_bytes = 1UL << ppc64_pft_size;
	htab_hash_mask = (htab_size_bytes >> 7) - 1;

	return 0;
}

/* Must be called in user context */
static int pseries_lpar_resize_hpt(unsigned long shift)
{
	struct hpt_resize_state state = {
		.shift = shift,
		.commit_rc = H_FUNCTION,
	};
	unsigned int delay, total_delay = 0;
	int rc;
	ktime_t t0, t1, t2;

	might_sleep();

	if (!firmware_has_feature(FW_FEATURE_HPT_RESIZE))
		return -ENODEV;

	pr_info("Attempting to resize HPT to shift %lu\n", shift);

	t0 = ktime_get();

	rc = plpar_resize_hpt_prepare(0, shift);
	while (H_IS_LONG_BUSY(rc)) {
		delay = get_longbusy_msecs(rc);
		total_delay += delay;
		if (total_delay > HPT_RESIZE_TIMEOUT) {
			/* prepare with shift==0 cancels an in-progress resize */
			rc = plpar_resize_hpt_prepare(0, 0);
			if (rc != H_SUCCESS)
				pr_warn("Unexpected error %d cancelling timed out HPT resize\n",
				       rc);
			return -ETIMEDOUT;
		}
		msleep(delay);
		rc = plpar_resize_hpt_prepare(0, shift);
	};

	switch (rc) {
	case H_SUCCESS:
		/* Continue on */
		break;

	case H_PARAMETER:
		return -EINVAL;
	case H_RESOURCE:
		return -EPERM;
	default:
		pr_warn("Unexpected error %d from H_RESIZE_HPT_PREPARE\n", rc);
		return -EIO;
	}

	t1 = ktime_get();

	rc = stop_machine(pseries_lpar_resize_hpt_commit, &state, NULL);

	t2 = ktime_get();

	if (rc != 0) {
		switch (state.commit_rc) {
		case H_PTEG_FULL:
			pr_warn("Hash collision while resizing HPT\n");
			return -ENOSPC;

		default:
			pr_warn("Unexpected error %d from H_RESIZE_HPT_COMMIT\n",
				state.commit_rc);
			return -EIO;
		};
	}

	pr_info("HPT resize to shift %lu complete (%lld ms / %lld ms)\n",
		shift, (long long) ktime_ms_delta(t1, t0),
		(long long) ktime_ms_delta(t2, t1));

	return 0;
}

static int pseries_lpar_register_process_table(unsigned long base,
			unsigned long page_size, unsigned long table_size)
{
	long rc;
	unsigned long flags = 0;

	if (table_size)
		flags |= PROC_TABLE_NEW;
	if (radix_enabled())
		flags |= PROC_TABLE_RADIX | PROC_TABLE_GTSE;
	else
		flags |= PROC_TABLE_HPT_SLB;
	for (;;) {
		rc = plpar_hcall_norets(H_REGISTER_PROC_TBL, flags, base,
					page_size, table_size);
		if (!H_IS_LONG_BUSY(rc))
			break;
		mdelay(get_longbusy_msecs(rc));
	}
	if (rc != H_SUCCESS) {
		pr_err("Failed to register process table (rc=%ld)\n", rc);
		BUG();
	}
	return rc;
}

void __init hpte_init_pseries(void)
{
	mmu_hash_ops.hpte_invalidate	 = pSeries_lpar_hpte_invalidate;
	mmu_hash_ops.hpte_updatepp	 = pSeries_lpar_hpte_updatepp;
	mmu_hash_ops.hpte_updateboltedpp = pSeries_lpar_hpte_updateboltedpp;
	mmu_hash_ops.hpte_insert	 = pSeries_lpar_hpte_insert;
	mmu_hash_ops.hpte_remove	 = pSeries_lpar_hpte_remove;
	mmu_hash_ops.hpte_removebolted   = pSeries_lpar_hpte_removebolted;
	mmu_hash_ops.flush_hash_range	 = pSeries_lpar_flush_hash_range;
	mmu_hash_ops.hpte_clear_all      = pseries_hpte_clear_all;
	mmu_hash_ops.hugepage_invalidate = pSeries_lpar_hugepage_invalidate;
	register_process_table		 = pseries_lpar_register_process_table;

	if (firmware_has_feature(FW_FEATURE_HPT_RESIZE))
		mmu_hash_ops.resize_hpt = pseries_lpar_resize_hpt;
}

void radix_init_pseries(void)
{
	pr_info("Using radix MMU under hypervisor\n");
	register_process_table = pseries_lpar_register_process_table;
}

#ifdef CONFIG_PPC_SMLPAR
#define CMO_FREE_HINT_DEFAULT 1
static int cmo_free_hint_flag = CMO_FREE_HINT_DEFAULT;

static int __init cmo_free_hint(char *str)
{
	char *parm;
	parm = strstrip(str);

	if (strcasecmp(parm, "no") == 0 || strcasecmp(parm, "off") == 0) {
		pr_info("%s: CMO free page hinting is not active.\n", __func__);
		cmo_free_hint_flag = 0;
		return 1;
	}

	cmo_free_hint_flag = 1;
	pr_info("%s: CMO free page hinting is active.\n", __func__);

	if (strcasecmp(parm, "yes") == 0 || strcasecmp(parm, "on") == 0)
		return 1;

	return 0;
}

__setup("cmo_free_hint=", cmo_free_hint);

static void pSeries_set_page_state(struct page *page, int order,
				   unsigned long state)
{
	int i, j;
	unsigned long cmo_page_sz, addr;

	cmo_page_sz = cmo_get_page_size();
	addr = __pa((unsigned long)page_address(page));

	for (i = 0; i < (1 << order); i++, addr += PAGE_SIZE) {
		for (j = 0; j < PAGE_SIZE; j += cmo_page_sz)
			plpar_hcall_norets(H_PAGE_INIT, state, addr + j, 0);
	}
}

void arch_free_page(struct page *page, int order)
{
	if (radix_enabled())
		return;
	if (!cmo_free_hint_flag || !firmware_has_feature(FW_FEATURE_CMO))
		return;

	pSeries_set_page_state(page, order, H_PAGE_SET_UNUSED);
}
EXPORT_SYMBOL(arch_free_page);

#endif /* CONFIG_PPC_SMLPAR */
#endif /* CONFIG_PPC_BOOK3S_64 */

#ifdef CONFIG_TRACEPOINTS
#ifdef CONFIG_JUMP_LABEL
struct static_key hcall_tracepoint_key = STATIC_KEY_INIT;

int hcall_tracepoint_regfunc(void)
{
	static_key_slow_inc(&hcall_tracepoint_key);
	return 0;
}

void hcall_tracepoint_unregfunc(void)
{
	static_key_slow_dec(&hcall_tracepoint_key);
}
#else
/*
 * We optimise our hcall path by placing hcall_tracepoint_refcount
 * directly in the TOC so we can check if the hcall tracepoints are
 * enabled via a single load.
 */

/* NB: reg/unreg are called while guarded with the tracepoints_mutex */
extern long hcall_tracepoint_refcount;

int hcall_tracepoint_regfunc(void)
{
	hcall_tracepoint_refcount++;
	return 0;
}

void hcall_tracepoint_unregfunc(void)
{
	hcall_tracepoint_refcount--;
}
#endif

/*
 * Since the tracing code might execute hcalls we need to guard against
 * recursion. One example of this are spinlocks calling H_YIELD on
 * shared processor partitions.
 */
static DEFINE_PER_CPU(unsigned int, hcall_trace_depth);


void __trace_hcall_entry(unsigned long opcode, unsigned long *args)
{
	unsigned long flags;
	unsigned int *depth;

	/*
	 * We cannot call tracepoints inside RCU idle regions which
	 * means we must not trace H_CEDE.
	 */
	if (opcode == H_CEDE)
		return;

	local_irq_save(flags);

	depth = this_cpu_ptr(&hcall_trace_depth);

	if (*depth)
		goto out;

	(*depth)++;
	preempt_disable();
	trace_hcall_entry(opcode, args);
	(*depth)--;

out:
	local_irq_restore(flags);
}

void __trace_hcall_exit(long opcode, long retval, unsigned long *retbuf)
{
	unsigned long flags;
	unsigned int *depth;

	if (opcode == H_CEDE)
		return;

	local_irq_save(flags);

	depth = this_cpu_ptr(&hcall_trace_depth);

	if (*depth)
		goto out;

	(*depth)++;
	trace_hcall_exit(opcode, retval, retbuf);
	preempt_enable();
	(*depth)--;

out:
	local_irq_restore(flags);
}
#endif

/**
 * h_get_mpp
 * H_GET_MPP hcall returns info in 7 parms
 */
int h_get_mpp(struct hvcall_mpp_data *mpp_data)
{
	int rc;
	unsigned long retbuf[PLPAR_HCALL9_BUFSIZE];

	rc = plpar_hcall9(H_GET_MPP, retbuf);

	mpp_data->entitled_mem = retbuf[0];
	mpp_data->mapped_mem = retbuf[1];

	mpp_data->group_num = (retbuf[2] >> 2 * 8) & 0xffff;
	mpp_data->pool_num = retbuf[2] & 0xffff;

	mpp_data->mem_weight = (retbuf[3] >> 7 * 8) & 0xff;
	mpp_data->unallocated_mem_weight = (retbuf[3] >> 6 * 8) & 0xff;
	mpp_data->unallocated_entitlement = retbuf[3] & 0xffffffffffffUL;

	mpp_data->pool_size = retbuf[4];
	mpp_data->loan_request = retbuf[5];
	mpp_data->backing_mem = retbuf[6];

	return rc;
}
EXPORT_SYMBOL(h_get_mpp);

int h_get_mpp_x(struct hvcall_mpp_x_data *mpp_x_data)
{
	int rc;
	unsigned long retbuf[PLPAR_HCALL9_BUFSIZE] = { 0 };

	rc = plpar_hcall9(H_GET_MPP_X, retbuf);

	mpp_x_data->coalesced_bytes = retbuf[0];
	mpp_x_data->pool_coalesced_bytes = retbuf[1];
	mpp_x_data->pool_purr_cycles = retbuf[2];
	mpp_x_data->pool_spurr_cycles = retbuf[3];

	return rc;
}

static unsigned long vsid_unscramble(unsigned long vsid, int ssize)
{
	unsigned long protovsid;
	unsigned long va_bits = VA_BITS;
	unsigned long modinv, vsid_modulus;
	unsigned long max_mod_inv, tmp_modinv;

	if (!mmu_has_feature(MMU_FTR_68_BIT_VA))
		va_bits = 65;

	if (ssize == MMU_SEGSIZE_256M) {
		modinv = VSID_MULINV_256M;
		vsid_modulus = ((1UL << (va_bits - SID_SHIFT)) - 1);
	} else {
		modinv = VSID_MULINV_1T;
		vsid_modulus = ((1UL << (va_bits - SID_SHIFT_1T)) - 1);
	}

	/*
	 * vsid outside our range.
	 */
	if (vsid >= vsid_modulus)
		return 0;

	/*
	 * If modinv is the modular multiplicate inverse of (x % vsid_modulus)
	 * and vsid = (protovsid * x) % vsid_modulus, then we say:
	 *   protovsid = (vsid * modinv) % vsid_modulus
	 */

	/* Check if (vsid * modinv) overflow (63 bits) */
	max_mod_inv = 0x7fffffffffffffffull / vsid;
	if (modinv < max_mod_inv)
		return (vsid * modinv) % vsid_modulus;

	tmp_modinv = modinv/max_mod_inv;
	modinv %= max_mod_inv;

	protovsid = (((vsid * max_mod_inv) % vsid_modulus) * tmp_modinv) % vsid_modulus;
	protovsid = (protovsid + vsid * modinv) % vsid_modulus;

	return protovsid;
}

static int __init reserve_vrma_context_id(void)
{
	unsigned long protovsid;

	/*
	 * Reserve context ids which map to reserved virtual addresses. For now
	 * we only reserve the context id which maps to the VRMA VSID. We ignore
	 * the addresses in "ibm,adjunct-virtual-addresses" because we don't
	 * enable adjunct support via the "ibm,client-architecture-support"
	 * interface.
	 */
	protovsid = vsid_unscramble(VRMA_VSID, MMU_SEGSIZE_1T);
	hash__reserve_context_id(protovsid >> ESID_BITS_1T);
	return 0;
}
machine_device_initcall(pseries, reserve_vrma_context_id);

#ifdef CONFIG_DEBUG_FS
/* debugfs file interface for vpa data */
static ssize_t vpa_file_read(struct file *filp, char __user *buf, size_t len,
			      loff_t *pos)
{
	int cpu = (long)filp->private_data;
	struct lppaca *lppaca = &lppaca_of(cpu);

	return simple_read_from_buffer(buf, len, pos, lppaca,
				sizeof(struct lppaca));
}

static const struct file_operations vpa_fops = {
	.open		= simple_open,
	.read		= vpa_file_read,
	.llseek		= default_llseek,
};

static int __init vpa_debugfs_init(void)
{
	char name[16];
	long i;
	static struct dentry *vpa_dir;

	if (!firmware_has_feature(FW_FEATURE_SPLPAR))
		return 0;

	vpa_dir = debugfs_create_dir("vpa", powerpc_debugfs_root);
	if (!vpa_dir) {
		pr_warn("%s: can't create vpa root dir\n", __func__);
		return -ENOMEM;
	}

	/* set up the per-cpu vpa file*/
	for_each_possible_cpu(i) {
		struct dentry *d;

		sprintf(name, "cpu-%ld", i);

		d = debugfs_create_file(name, 0400, vpa_dir, (void *)i,
					&vpa_fops);
		if (!d) {
			pr_warn("%s: can't create per-cpu vpa file\n",
					__func__);
			return -ENOMEM;
		}
	}

	return 0;
}
machine_arch_initcall(pseries, vpa_debugfs_init);
#endif /* CONFIG_DEBUG_FS */
