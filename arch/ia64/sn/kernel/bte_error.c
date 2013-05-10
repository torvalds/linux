/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000-2007 Silicon Graphics, Inc.  All Rights Reserved.
 */

#include <linux/types.h>
#include <asm/sn/sn_sal.h>
#include "ioerror.h"
#include <asm/sn/addrs.h>
#include <asm/sn/shubio.h>
#include <asm/sn/geo.h>
#include "xtalk/xwidgetdev.h"
#include "xtalk/hubdev.h"
#include <asm/sn/bte.h>
#include <asm/param.h>

/*
 * Bte error handling is done in two parts.  The first captures
 * any crb related errors.  Since there can be multiple crbs per
 * interface and multiple interfaces active, we need to wait until
 * all active crbs are completed.  This is the first job of the
 * second part error handler.  When all bte related CRBs are cleanly
 * completed, it resets the interfaces and gets them ready for new
 * transfers to be queued.
 */

void bte_error_handler(unsigned long);

/*
 * Wait until all BTE related CRBs are completed
 * and then reset the interfaces.
 */
int shub1_bte_error_handler(unsigned long _nodepda)
{
	struct nodepda_s *err_nodepda = (struct nodepda_s *)_nodepda;
	struct timer_list *recovery_timer = &err_nodepda->bte_recovery_timer;
	nasid_t nasid;
	int i;
	int valid_crbs;
	ii_imem_u_t imem;	/* II IMEM Register */
	ii_icrb0_d_u_t icrbd;	/* II CRB Register D */
	ii_ibcr_u_t ibcr;
	ii_icmr_u_t icmr;
	ii_ieclr_u_t ieclr;

	BTE_PRINTK(("shub1_bte_error_handler(%p) - %d\n", err_nodepda,
		    smp_processor_id()));

	if ((err_nodepda->bte_if[0].bh_error == BTE_SUCCESS) &&
	    (err_nodepda->bte_if[1].bh_error == BTE_SUCCESS)) {
		BTE_PRINTK(("eh:%p:%d Nothing to do.\n", err_nodepda,
			    smp_processor_id()));
		return 1;
	}

	/* Determine information about our hub */
	nasid = cnodeid_to_nasid(err_nodepda->bte_if[0].bte_cnode);

	/*
	 * A BTE transfer can use multiple CRBs.  We need to make sure
	 * that all the BTE CRBs are complete (or timed out) before
	 * attempting to clean up the error.  Resetting the BTE while
	 * there are still BTE CRBs active will hang the BTE.
	 * We should look at all the CRBs to see if they are allocated
	 * to the BTE and see if they are still active.  When none
	 * are active, we can continue with the cleanup.
	 *
	 * We also want to make sure that the local NI port is up.
	 * When a router resets the NI port can go down, while it
	 * goes through the LLP handshake, but then comes back up.
	 */
	icmr.ii_icmr_regval = REMOTE_HUB_L(nasid, IIO_ICMR);
	if (icmr.ii_icmr_fld_s.i_crb_mark != 0) {
		/*
		 * There are errors which still need to be cleaned up by
		 * hubiio_crb_error_handler
		 */
		mod_timer(recovery_timer, jiffies + (HZ * 5));
		BTE_PRINTK(("eh:%p:%d Marked Giving up\n", err_nodepda,
			    smp_processor_id()));
		return 1;
	}
	if (icmr.ii_icmr_fld_s.i_crb_vld != 0) {

		valid_crbs = icmr.ii_icmr_fld_s.i_crb_vld;

		for (i = 0; i < IIO_NUM_CRBS; i++) {
			if (!((1 << i) & valid_crbs)) {
				/* This crb was not marked as valid, ignore */
				continue;
			}
			icrbd.ii_icrb0_d_regval =
			    REMOTE_HUB_L(nasid, IIO_ICRB_D(i));
			if (icrbd.d_bteop) {
				mod_timer(recovery_timer, jiffies + (HZ * 5));
				BTE_PRINTK(("eh:%p:%d Valid %d, Giving up\n",
					    err_nodepda, smp_processor_id(),
					    i));
				return 1;
			}
		}
	}

	BTE_PRINTK(("eh:%p:%d Cleaning up\n", err_nodepda, smp_processor_id()));
	/* Re-enable both bte interfaces */
	imem.ii_imem_regval = REMOTE_HUB_L(nasid, IIO_IMEM);
	imem.ii_imem_fld_s.i_b0_esd = imem.ii_imem_fld_s.i_b1_esd = 1;
	REMOTE_HUB_S(nasid, IIO_IMEM, imem.ii_imem_regval);

	/* Clear BTE0/1 error bits */
	ieclr.ii_ieclr_regval = 0;
	if (err_nodepda->bte_if[0].bh_error != BTE_SUCCESS)
		ieclr.ii_ieclr_fld_s.i_e_bte_0 = 1;
	if (err_nodepda->bte_if[1].bh_error != BTE_SUCCESS)
		ieclr.ii_ieclr_fld_s.i_e_bte_1 = 1;
	REMOTE_HUB_S(nasid, IIO_IECLR, ieclr.ii_ieclr_regval);

	/* Reinitialize both BTE state machines. */
	ibcr.ii_ibcr_regval = REMOTE_HUB_L(nasid, IIO_IBCR);
	ibcr.ii_ibcr_fld_s.i_soft_reset = 1;
	REMOTE_HUB_S(nasid, IIO_IBCR, ibcr.ii_ibcr_regval);

	del_timer(recovery_timer);
	return 0;
}

/*
 * Wait until all BTE related CRBs are completed
 * and then reset the interfaces.
 */
int shub2_bte_error_handler(unsigned long _nodepda)
{
	struct nodepda_s *err_nodepda = (struct nodepda_s *)_nodepda;
	struct timer_list *recovery_timer = &err_nodepda->bte_recovery_timer;
	struct bteinfo_s *bte;
	nasid_t nasid;
	u64 status;
	int i;

	nasid = cnodeid_to_nasid(err_nodepda->bte_if[0].bte_cnode);

	/*
	 * Verify that all the BTEs are complete
	 */
	for (i = 0; i < BTES_PER_NODE; i++) {
		bte = &err_nodepda->bte_if[i];
		status = BTE_LNSTAT_LOAD(bte);
		if (status & IBLS_ERROR) {
			bte->bh_error = BTE_SHUB2_ERROR(status);
			continue;
		}
		if (!(status & IBLS_BUSY))
			continue;
		mod_timer(recovery_timer, jiffies + (HZ * 5));
		BTE_PRINTK(("eh:%p:%d Marked Giving up\n", err_nodepda,
			    smp_processor_id()));
		return 1;
	}
	if (ia64_sn_bte_recovery(nasid))
		panic("bte_error_handler(): Fatal BTE Error");

	del_timer(recovery_timer);
	return 0;
}

/*
 * Wait until all BTE related CRBs are completed
 * and then reset the interfaces.
 */
void bte_error_handler(unsigned long _nodepda)
{
	struct nodepda_s *err_nodepda = (struct nodepda_s *)_nodepda;
	spinlock_t *recovery_lock = &err_nodepda->bte_recovery_lock;
	int i;
	unsigned long irq_flags;
	volatile u64 *notify;
	bte_result_t bh_error;

	BTE_PRINTK(("bte_error_handler(%p) - %d\n", err_nodepda,
		    smp_processor_id()));

	spin_lock_irqsave(recovery_lock, irq_flags);

	/*
	 * Lock all interfaces on this node to prevent new transfers
	 * from being queued.
	 */
	for (i = 0; i < BTES_PER_NODE; i++) {
		if (err_nodepda->bte_if[i].cleanup_active) {
			continue;
		}
		spin_lock(&err_nodepda->bte_if[i].spinlock);
		BTE_PRINTK(("eh:%p:%d locked %d\n", err_nodepda,
			    smp_processor_id(), i));
		err_nodepda->bte_if[i].cleanup_active = 1;
	}

	if (is_shub1()) {
		if (shub1_bte_error_handler(_nodepda)) {
			spin_unlock_irqrestore(recovery_lock, irq_flags);
			return;
		}
	} else {
		if (shub2_bte_error_handler(_nodepda)) {
			spin_unlock_irqrestore(recovery_lock, irq_flags);
			return;
		}
	}

	for (i = 0; i < BTES_PER_NODE; i++) {
		bh_error = err_nodepda->bte_if[i].bh_error;
		if (bh_error != BTE_SUCCESS) {
			/* There is an error which needs to be notified */
			notify = err_nodepda->bte_if[i].most_rcnt_na;
			BTE_PRINTK(("cnode %d bte %d error=0x%lx\n",
				    err_nodepda->bte_if[i].bte_cnode,
				    err_nodepda->bte_if[i].bte_num,
				    IBLS_ERROR | (u64) bh_error));
			*notify = IBLS_ERROR | bh_error;
			err_nodepda->bte_if[i].bh_error = BTE_SUCCESS;
		}

		err_nodepda->bte_if[i].cleanup_active = 0;
		BTE_PRINTK(("eh:%p:%d Unlocked %d\n", err_nodepda,
			    smp_processor_id(), i));
		spin_unlock(&err_nodepda->bte_if[i].spinlock);
	}

	spin_unlock_irqrestore(recovery_lock, irq_flags);
}

/*
 * First part error handler.  This is called whenever any error CRB interrupt
 * is generated by the II.
 */
void
bte_crb_error_handler(cnodeid_t cnode, int btenum,
                      int crbnum, ioerror_t * ioe, int bteop)
{
	struct bteinfo_s *bte;


	bte = &(NODEPDA(cnode)->bte_if[btenum]);

	/*
	 * The caller has already figured out the error type, we save that
	 * in the bte handle structure for the thread exercising the
	 * interface to consume.
	 */
	bte->bh_error = ioe->ie_errortype + BTEFAIL_OFFSET;
	bte->bte_error_count++;

	BTE_PRINTK(("Got an error on cnode %d bte %d: HW error type 0x%x\n",
		bte->bte_cnode, bte->bte_num, ioe->ie_errortype));
	bte_error_handler((unsigned long) NODEPDA(cnode));
}

