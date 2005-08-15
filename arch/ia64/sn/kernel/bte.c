/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/addrs.h>
#include <asm/sn/arch.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/pda.h>
#include <asm/sn/shubio.h>
#include <asm/nodedata.h>
#include <asm/delay.h>

#include <linux/bootmem.h>
#include <linux/string.h>
#include <linux/sched.h>

#include <asm/sn/bte.h>

#ifndef L1_CACHE_MASK
#define L1_CACHE_MASK (L1_CACHE_BYTES - 1)
#endif

/* two interfaces on two btes */
#define MAX_INTERFACES_TO_TRY		4
#define MAX_NODES_TO_TRY		2

static struct bteinfo_s *bte_if_on_node(nasid_t nasid, int interface)
{
	nodepda_t *tmp_nodepda;

	if (nasid_to_cnodeid(nasid) == -1)
		return (struct bteinfo_s *)NULL;;

	tmp_nodepda = NODEPDA(nasid_to_cnodeid(nasid));
	return &tmp_nodepda->bte_if[interface];

}

static inline void bte_start_transfer(struct bteinfo_s *bte, u64 len, u64 mode)
{
	if (is_shub2()) {
		BTE_CTRL_STORE(bte, (IBLS_BUSY | ((len) | (mode) << 24)));
	} else {
		BTE_LNSTAT_STORE(bte, len);
		BTE_CTRL_STORE(bte, mode);
	}
}

/************************************************************************
 * Block Transfer Engine copy related functions.
 *
 ***********************************************************************/

/*
 * bte_copy(src, dest, len, mode, notification)
 *
 * Use the block transfer engine to move kernel memory from src to dest
 * using the assigned mode.
 *
 * Paramaters:
 *   src - physical address of the transfer source.
 *   dest - physical address of the transfer destination.
 *   len - number of bytes to transfer from source to dest.
 *   mode - hardware defined.  See reference information
 *          for IBCT0/1 in the SHUB Programmers Reference
 *   notification - kernel virtual address of the notification cache
 *                  line.  If NULL, the default is used and
 *                  the bte_copy is synchronous.
 *
 * NOTE:  This function requires src, dest, and len to
 * be cacheline aligned.
 */
bte_result_t bte_copy(u64 src, u64 dest, u64 len, u64 mode, void *notification)
{
	u64 transfer_size;
	u64 transfer_stat;
	u64 notif_phys_addr;
	struct bteinfo_s *bte;
	bte_result_t bte_status;
	unsigned long irq_flags;
	unsigned long itc_end = 0;
	int nasid_to_try[MAX_NODES_TO_TRY];
	int my_nasid = get_nasid();
	int bte_if_index, nasid_index;
	int bte_first, btes_per_node = BTES_PER_NODE;

	BTE_PRINTK(("bte_copy(0x%lx, 0x%lx, 0x%lx, 0x%lx, 0x%p)\n",
		    src, dest, len, mode, notification));

	if (len == 0) {
		return BTE_SUCCESS;
	}

	BUG_ON((len & L1_CACHE_MASK) ||
		 (src & L1_CACHE_MASK) || (dest & L1_CACHE_MASK));
	BUG_ON(!(len < ((BTE_LEN_MASK + 1) << L1_CACHE_SHIFT)));

	/*
	 * Start with interface corresponding to cpu number
	 */
	bte_first = raw_smp_processor_id() % btes_per_node;

	if (mode & BTE_USE_DEST) {
		/* try remote then local */
		nasid_to_try[0] = NASID_GET(dest);
		if (mode & BTE_USE_ANY) {
			nasid_to_try[1] = my_nasid;
		} else {
			nasid_to_try[1] = (int)NULL;
		}
	} else {
		/* try local then remote */
		nasid_to_try[0] = my_nasid;
		if (mode & BTE_USE_ANY) {
			nasid_to_try[1] = NASID_GET(dest);
		} else {
			nasid_to_try[1] = (int)NULL;
		}
	}

retry_bteop:
	do {
		local_irq_save(irq_flags);

		bte_if_index = bte_first;
		nasid_index = 0;

		/* Attempt to lock one of the BTE interfaces. */
		while (nasid_index < MAX_NODES_TO_TRY) {
			bte = bte_if_on_node(nasid_to_try[nasid_index],bte_if_index);

			if (bte == NULL) {
				continue;
			}

			if (spin_trylock(&bte->spinlock)) {
				if (!(*bte->most_rcnt_na & BTE_WORD_AVAILABLE) ||
				    (BTE_LNSTAT_LOAD(bte) & BTE_ACTIVE)) {
					/* Got the lock but BTE still busy */
					spin_unlock(&bte->spinlock);
				} else {
					/* we got the lock and it's not busy */
					break;
				}
			}

			bte_if_index = (bte_if_index + 1) % btes_per_node; /* Next interface */
			if (bte_if_index == bte_first) {
				/*
				 * We've tried all interfaces on this node
				 */
				nasid_index++;
			}

			bte = NULL;
		}

		if (bte != NULL) {
			break;
		}

		local_irq_restore(irq_flags);

		if (!(mode & BTE_WACQUIRE)) {
			return BTEFAIL_NOTAVAIL;
		}
	} while (1);

	if (notification == NULL) {
		/* User does not want to be notified. */
		bte->most_rcnt_na = &bte->notify;
	} else {
		bte->most_rcnt_na = notification;
	}

	/* Calculate the number of cache lines to transfer. */
	transfer_size = ((len >> L1_CACHE_SHIFT) & BTE_LEN_MASK);

	/* Initialize the notification to a known value. */
	*bte->most_rcnt_na = BTE_WORD_BUSY;
	notif_phys_addr = TO_PHYS(ia64_tpa((unsigned long)bte->most_rcnt_na));

	if (is_shub2()) {
		src = SH2_TIO_PHYS_TO_DMA(src);
		dest = SH2_TIO_PHYS_TO_DMA(dest);
		notif_phys_addr = SH2_TIO_PHYS_TO_DMA(notif_phys_addr);
	}
	/* Set the source and destination registers */
	BTE_PRINTKV(("IBSA = 0x%lx)\n", (TO_PHYS(src))));
	BTE_SRC_STORE(bte, TO_PHYS(src));
	BTE_PRINTKV(("IBDA = 0x%lx)\n", (TO_PHYS(dest))));
	BTE_DEST_STORE(bte, TO_PHYS(dest));

	/* Set the notification register */
	BTE_PRINTKV(("IBNA = 0x%lx)\n", notif_phys_addr));
	BTE_NOTIF_STORE(bte, notif_phys_addr);

	/* Initiate the transfer */
	BTE_PRINTK(("IBCT = 0x%lx)\n", BTE_VALID_MODE(mode)));
	bte_start_transfer(bte, transfer_size, BTE_VALID_MODE(mode));

	itc_end = ia64_get_itc() + (40000000 * local_cpu_data->cyc_per_usec);

	spin_unlock_irqrestore(&bte->spinlock, irq_flags);

	if (notification != NULL) {
		return BTE_SUCCESS;
	}

	while ((transfer_stat = *bte->most_rcnt_na) == BTE_WORD_BUSY) {
		cpu_relax();
		if (ia64_get_itc() > itc_end) {
			BTE_PRINTK(("BTE timeout nasid 0x%x bte%d IBLS = 0x%lx na 0x%lx\n",
				NASID_GET(bte->bte_base_addr), bte->bte_num,
				BTE_LNSTAT_LOAD(bte), *bte->most_rcnt_na) );
			bte->bte_error_count++;
			bte->bh_error = IBLS_ERROR;
			bte_error_handler((unsigned long)NODEPDA(bte->bte_cnode));
			*bte->most_rcnt_na = BTE_WORD_AVAILABLE;
			goto retry_bteop;
		}
	}

	BTE_PRINTKV((" Delay Done.  IBLS = 0x%lx, most_rcnt_na = 0x%lx\n",
		     BTE_LNSTAT_LOAD(bte), *bte->most_rcnt_na));

	if (transfer_stat & IBLS_ERROR) {
		bte_status = transfer_stat & ~IBLS_ERROR;
	} else {
		bte_status = BTE_SUCCESS;
	}
	*bte->most_rcnt_na = BTE_WORD_AVAILABLE;

	BTE_PRINTK(("Returning status is 0x%lx and most_rcnt_na is 0x%lx\n",
		    BTE_LNSTAT_LOAD(bte), *bte->most_rcnt_na));

	return bte_status;
}

EXPORT_SYMBOL(bte_copy);

/*
 * bte_unaligned_copy(src, dest, len, mode)
 *
 * use the block transfer engine to move kernel
 * memory from src to dest using the assigned mode.
 *
 * Paramaters:
 *   src - physical address of the transfer source.
 *   dest - physical address of the transfer destination.
 *   len - number of bytes to transfer from source to dest.
 *   mode - hardware defined.  See reference information
 *          for IBCT0/1 in the SGI documentation.
 *
 * NOTE: If the source, dest, and len are all cache line aligned,
 * then it would be _FAR_ preferrable to use bte_copy instead.
 */
bte_result_t bte_unaligned_copy(u64 src, u64 dest, u64 len, u64 mode)
{
	int destFirstCacheOffset;
	u64 headBteSource;
	u64 headBteLen;
	u64 headBcopySrcOffset;
	u64 headBcopyDest;
	u64 headBcopyLen;
	u64 footBteSource;
	u64 footBteLen;
	u64 footBcopyDest;
	u64 footBcopyLen;
	bte_result_t rv;
	char *bteBlock, *bteBlock_unaligned;

	if (len == 0) {
		return BTE_SUCCESS;
	}

	/* temporary buffer used during unaligned transfers */
	bteBlock_unaligned = kmalloc(len + 3 * L1_CACHE_BYTES,
				     GFP_KERNEL | GFP_DMA);
	if (bteBlock_unaligned == NULL) {
		return BTEFAIL_NOTAVAIL;
	}
	bteBlock = (char *)L1_CACHE_ALIGN((u64) bteBlock_unaligned);

	headBcopySrcOffset = src & L1_CACHE_MASK;
	destFirstCacheOffset = dest & L1_CACHE_MASK;

	/*
	 * At this point, the transfer is broken into
	 * (up to) three sections.  The first section is
	 * from the start address to the first physical
	 * cache line, the second is from the first physical
	 * cache line to the last complete cache line,
	 * and the third is from the last cache line to the
	 * end of the buffer.  The first and third sections
	 * are handled by bte copying into a temporary buffer
	 * and then bcopy'ing the necessary section into the
	 * final location.  The middle section is handled with
	 * a standard bte copy.
	 *
	 * One nasty exception to the above rule is when the
	 * source and destination are not symetrically
	 * mis-aligned.  If the source offset from the first
	 * cache line is different from the destination offset,
	 * we make the first section be the entire transfer
	 * and the bcopy the entire block into place.
	 */
	if (headBcopySrcOffset == destFirstCacheOffset) {

		/*
		 * Both the source and destination are the same
		 * distance from a cache line boundary so we can
		 * use the bte to transfer the bulk of the
		 * data.
		 */
		headBteSource = src & ~L1_CACHE_MASK;
		headBcopyDest = dest;
		if (headBcopySrcOffset) {
			headBcopyLen =
			    (len >
			     (L1_CACHE_BYTES -
			      headBcopySrcOffset) ? L1_CACHE_BYTES
			     - headBcopySrcOffset : len);
			headBteLen = L1_CACHE_BYTES;
		} else {
			headBcopyLen = 0;
			headBteLen = 0;
		}

		if (len > headBcopyLen) {
			footBcopyLen = (len - headBcopyLen) & L1_CACHE_MASK;
			footBteLen = L1_CACHE_BYTES;

			footBteSource = src + len - footBcopyLen;
			footBcopyDest = dest + len - footBcopyLen;

			if (footBcopyDest == (headBcopyDest + headBcopyLen)) {
				/*
				 * We have two contigous bcopy
				 * blocks.  Merge them.
				 */
				headBcopyLen += footBcopyLen;
				headBteLen += footBteLen;
			} else if (footBcopyLen > 0) {
				rv = bte_copy(footBteSource,
					      ia64_tpa((unsigned long)bteBlock),
					      footBteLen, mode, NULL);
				if (rv != BTE_SUCCESS) {
					kfree(bteBlock_unaligned);
					return rv;
				}

				memcpy(__va(footBcopyDest),
				       (char *)bteBlock, footBcopyLen);
			}
		} else {
			footBcopyLen = 0;
			footBteLen = 0;
		}

		if (len > (headBcopyLen + footBcopyLen)) {
			/* now transfer the middle. */
			rv = bte_copy((src + headBcopyLen),
				      (dest +
				       headBcopyLen),
				      (len - headBcopyLen -
				       footBcopyLen), mode, NULL);
			if (rv != BTE_SUCCESS) {
				kfree(bteBlock_unaligned);
				return rv;
			}

		}
	} else {

		/*
		 * The transfer is not symetric, we will
		 * allocate a buffer large enough for all the
		 * data, bte_copy into that buffer and then
		 * bcopy to the destination.
		 */

		/* Add the leader from source */
		headBteLen = len + (src & L1_CACHE_MASK);
		/* Add the trailing bytes from footer. */
		headBteLen += L1_CACHE_BYTES - (headBteLen & L1_CACHE_MASK);
		headBteSource = src & ~L1_CACHE_MASK;
		headBcopySrcOffset = src & L1_CACHE_MASK;
		headBcopyDest = dest;
		headBcopyLen = len;
	}

	if (headBcopyLen > 0) {
		rv = bte_copy(headBteSource,
			      ia64_tpa((unsigned long)bteBlock), headBteLen,
			      mode, NULL);
		if (rv != BTE_SUCCESS) {
			kfree(bteBlock_unaligned);
			return rv;
		}

		memcpy(__va(headBcopyDest), ((char *)bteBlock +
					     headBcopySrcOffset), headBcopyLen);
	}
	kfree(bteBlock_unaligned);
	return BTE_SUCCESS;
}

EXPORT_SYMBOL(bte_unaligned_copy);

/************************************************************************
 * Block Transfer Engine initialization functions.
 *
 ***********************************************************************/

/*
 * bte_init_node(nodepda, cnode)
 *
 * Initialize the nodepda structure with BTE base addresses and
 * spinlocks.
 */
void bte_init_node(nodepda_t * mynodepda, cnodeid_t cnode)
{
	int i;

	/*
	 * Indicate that all the block transfer engines on this node
	 * are available.
	 */

	/*
	 * Allocate one bte_recover_t structure per node.  It holds
	 * the recovery lock for node.  All the bte interface structures
	 * will point at this one bte_recover structure to get the lock.
	 */
	spin_lock_init(&mynodepda->bte_recovery_lock);
	init_timer(&mynodepda->bte_recovery_timer);
	mynodepda->bte_recovery_timer.function = bte_error_handler;
	mynodepda->bte_recovery_timer.data = (unsigned long)mynodepda;

	for (i = 0; i < BTES_PER_NODE; i++) {
		u64 *base_addr;

		/* Which link status register should we use? */
		base_addr = (u64 *)
		    REMOTE_HUB_ADDR(cnodeid_to_nasid(cnode), BTE_BASE_ADDR(i));
		mynodepda->bte_if[i].bte_base_addr = base_addr;
		mynodepda->bte_if[i].bte_source_addr = BTE_SOURCE_ADDR(base_addr);
		mynodepda->bte_if[i].bte_destination_addr = BTE_DEST_ADDR(base_addr);
		mynodepda->bte_if[i].bte_control_addr = BTE_CTRL_ADDR(base_addr);
		mynodepda->bte_if[i].bte_notify_addr = BTE_NOTIF_ADDR(base_addr);

		/*
		 * Initialize the notification and spinlock
		 * so the first transfer can occur.
		 */
		mynodepda->bte_if[i].most_rcnt_na =
		    &(mynodepda->bte_if[i].notify);
		mynodepda->bte_if[i].notify = BTE_WORD_AVAILABLE;
		spin_lock_init(&mynodepda->bte_if[i].spinlock);

		mynodepda->bte_if[i].bte_cnode = cnode;
		mynodepda->bte_if[i].bte_error_count = 0;
		mynodepda->bte_if[i].bte_num = i;
		mynodepda->bte_if[i].cleanup_active = 0;
		mynodepda->bte_if[i].bh_error = 0;
	}

}
