/*
 * File...........: linux/drivers/s390/block/dasd.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999-2001
 *
 * $Revision: 1.14 $
 */

#include <linux/config.h>
#include <linux/ctype.h>
#include <linux/init.h>

#include <asm/debug.h>
#include <asm/ebcdic.h>
#include <asm/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_erp:"

#include "dasd_int.h"

struct dasd_ccw_req *
dasd_alloc_erp_request(char *magic, int cplength, int datasize,
		       struct dasd_device * device)
{
	unsigned long flags;
	struct dasd_ccw_req *cqr;
	char *data;
	int size;

	/* Sanity checks */
	if ( magic == NULL || datasize > PAGE_SIZE ||
	     (cplength*sizeof(struct ccw1)) > PAGE_SIZE)
		BUG();

	size = (sizeof(struct dasd_ccw_req) + 7L) & -8L;
	if (cplength > 0)
		size += cplength * sizeof(struct ccw1);
	if (datasize > 0)
		size += datasize;
	spin_lock_irqsave(&device->mem_lock, flags);
	cqr = (struct dasd_ccw_req *)
		dasd_alloc_chunk(&device->erp_chunks, size);
	spin_unlock_irqrestore(&device->mem_lock, flags);
	if (cqr == NULL)
		return ERR_PTR(-ENOMEM);
	memset(cqr, 0, sizeof(struct dasd_ccw_req));
	data = (char *) cqr + ((sizeof(struct dasd_ccw_req) + 7L) & -8L);
	cqr->cpaddr = NULL;
	if (cplength > 0) {
		cqr->cpaddr = (struct ccw1 *) data;
		data += cplength*sizeof(struct ccw1);
		memset(cqr->cpaddr, 0, cplength*sizeof(struct ccw1));
	}
	cqr->data = NULL;
	if (datasize > 0) {
		cqr->data = data;
 		memset(cqr->data, 0, datasize);
	}
	strncpy((char *) &cqr->magic, magic, 4);
	ASCEBC((char *) &cqr->magic, 4);
	set_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags);
	dasd_get_device(device);
	return cqr;
}

void
dasd_free_erp_request(struct dasd_ccw_req * cqr, struct dasd_device * device)
{
	unsigned long flags;

	spin_lock_irqsave(&device->mem_lock, flags);
	dasd_free_chunk(&device->erp_chunks, cqr);
	spin_unlock_irqrestore(&device->mem_lock, flags);
	atomic_dec(&device->ref_count);
}


/*
 * dasd_default_erp_action just retries the current cqr
 */
struct dasd_ccw_req *
dasd_default_erp_action(struct dasd_ccw_req * cqr)
{
	struct dasd_device *device;

	device = cqr->device;

        /* just retry - there is nothing to save ... I got no sense data.... */
        if (cqr->retries > 0) {
                DEV_MESSAGE (KERN_DEBUG, device, 
                             "default ERP called (%i retries left)",
                             cqr->retries);
		cqr->lpm    = LPM_ANYPATH;
		cqr->status = DASD_CQR_QUEUED;
        } else {
                DEV_MESSAGE (KERN_WARNING, device, "%s",
			     "default ERP called (NO retry left)");
		cqr->status = DASD_CQR_FAILED;
		cqr->stopclk = get_clock ();
        }
        return cqr;
}				/* end dasd_default_erp_action */

/*
 * DESCRIPTION
 *   Frees all ERPs of the current ERP Chain and set the status
 *   of the original CQR either to DASD_CQR_DONE if ERP was successful
 *   or to DASD_CQR_FAILED if ERP was NOT successful.
 *   NOTE: This function is only called if no discipline postaction
 *	   is available
 *
 * PARAMETER
 *   erp		current erp_head
 *
 * RETURN VALUES
 *   cqr		pointer to the original CQR
 */
struct dasd_ccw_req *
dasd_default_erp_postaction(struct dasd_ccw_req * cqr)
{
	struct dasd_device *device;
	int success;

	if (cqr->refers == NULL || cqr->function == NULL)
		BUG();

	device = cqr->device;
	success = cqr->status == DASD_CQR_DONE;

	/* free all ERPs - but NOT the original cqr */
	while (cqr->refers != NULL) {
		struct dasd_ccw_req *refers;

		refers = cqr->refers;
		/* remove the request from the device queue */
		list_del(&cqr->list);
		/* free the finished erp request */
		dasd_free_erp_request(cqr, device);
		cqr = refers;
	}

	/* set corresponding status to original cqr */
	if (success)
		cqr->status = DASD_CQR_DONE;
	else {
		cqr->status = DASD_CQR_FAILED;
		cqr->stopclk = get_clock();
	}

	return cqr;

}				/* end default_erp_postaction */

/*
 * Print the hex dump of the memory used by a request. This includes
 * all error recovery ccws that have been chained in from of the 
 * real request.
 */
static inline void
hex_dump_memory(struct dasd_device *device, void *data, int len)
{
	int *pint;

	pint = (int *) data;
	while (len > 0) {
		DEV_MESSAGE(KERN_ERR, device, "%p: %08x %08x %08x %08x",
			    pint, pint[0], pint[1], pint[2], pint[3]);
		pint += 4;
		len -= 16;
	}
}

void
dasd_log_sense(struct dasd_ccw_req *cqr, struct irb *irb)
{
	struct dasd_device *device;

	device = cqr->device;
	/* dump sense data */
	if (device->discipline && device->discipline->dump_sense)
		device->discipline->dump_sense(device, cqr, irb);
}

void
dasd_log_ccw(struct dasd_ccw_req * cqr, int caller, __u32 cpa)
{
	struct dasd_device *device;
	struct dasd_ccw_req *lcqr;
	struct ccw1 *ccw;
	int cplength;

	device = cqr->device;
	/* log the channel program */
	for (lcqr = cqr; lcqr != NULL; lcqr = lcqr->refers) {
		DEV_MESSAGE(KERN_ERR, device,
			    "(%s) ERP chain report for req: %p",
			    caller == 0 ? "EXAMINE" : "ACTION", lcqr);
		hex_dump_memory(device, lcqr, sizeof(struct dasd_ccw_req));

		cplength = 1;
		ccw = lcqr->cpaddr;
		while (ccw++->flags & (CCW_FLAG_DC | CCW_FLAG_CC))
			cplength++;

		if (cplength > 40) {	/* log only parts of the CP */
			DEV_MESSAGE(KERN_ERR, device, "%s",
				    "Start of channel program:");
			hex_dump_memory(device, lcqr->cpaddr,
					40*sizeof(struct ccw1));

			DEV_MESSAGE(KERN_ERR, device, "%s",
				    "End of channel program:");
			hex_dump_memory(device, lcqr->cpaddr + cplength - 10,
					10*sizeof(struct ccw1));
		} else {	/* log the whole CP */
			DEV_MESSAGE(KERN_ERR, device, "%s",
				    "Channel program (complete):");
			hex_dump_memory(device, lcqr->cpaddr,
					cplength*sizeof(struct ccw1));
		}

		if (lcqr != cqr)
			continue;

		/*
		 * Log bytes arround failed CCW but only if we did
		 * not log the whole CP of the CCW is outside the
		 * logged CP. 
		 */
		if (cplength > 40 ||
		    ((addr_t) cpa < (addr_t) lcqr->cpaddr &&
		     (addr_t) cpa > (addr_t) (lcqr->cpaddr + cplength + 4))) {
			
			DEV_MESSAGE(KERN_ERR, device,
				    "Failed CCW (%p) (area):",
				    (void *) (long) cpa);
			hex_dump_memory(device, cqr->cpaddr - 10,
					20*sizeof(struct ccw1));
		}
	}

}				/* end log_erp_chain */

EXPORT_SYMBOL(dasd_default_erp_action);
EXPORT_SYMBOL(dasd_default_erp_postaction);
EXPORT_SYMBOL(dasd_alloc_erp_request);
EXPORT_SYMBOL(dasd_free_erp_request);
EXPORT_SYMBOL(dasd_log_sense);
EXPORT_SYMBOL(dasd_log_ccw);
