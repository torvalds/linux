// SPDX-License-Identifier: GPL-2.0
/*
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * Copyright IBM Corp. 1999, 2001
 *
 */

#define KMSG_COMPONENT "dasd"

#include <linux/ctype.h>
#include <linux/init.h>

#include <asm/debug.h>
#include <asm/ebcdic.h>
#include <linux/uaccess.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_erp:"

#include "dasd_int.h"

struct dasd_ccw_req *
dasd_alloc_erp_request(unsigned int magic, int cplength, int datasize,
		       struct dasd_device * device)
{
	unsigned long flags;
	struct dasd_ccw_req *cqr;
	char *data;
	int size;

	/* Sanity checks */
	BUG_ON(datasize > PAGE_SIZE ||
	       (cplength*sizeof(struct ccw1)) > PAGE_SIZE);

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
	INIT_LIST_HEAD(&cqr->devlist);
	INIT_LIST_HEAD(&cqr->blocklist);
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
	cqr->magic = magic;
	ASCEBC((char *) &cqr->magic, 4);
	set_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags);
	dasd_get_device(device);
	return cqr;
}

void
dasd_free_erp_request(struct dasd_ccw_req *cqr, struct dasd_device * device)
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
dasd_default_erp_action(struct dasd_ccw_req *cqr)
{
	struct dasd_device *device;

	device = cqr->startdev;

        /* just retry - there is nothing to save ... I got no sense data.... */
        if (cqr->retries > 0) {
		DBF_DEV_EVENT(DBF_DEBUG, device,
                             "default ERP called (%i retries left)",
                             cqr->retries);
		if (!test_bit(DASD_CQR_VERIFY_PATH, &cqr->flags))
			cqr->lpm = dasd_path_get_opm(device);
		cqr->status = DASD_CQR_FILLED;
        } else {
		pr_err("%s: default ERP has run out of retries and failed\n",
		       dev_name(&device->cdev->dev));
		cqr->status = DASD_CQR_FAILED;
		cqr->stopclk = get_tod_clock();
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
struct dasd_ccw_req *dasd_default_erp_postaction(struct dasd_ccw_req *cqr)
{
	int success;
	unsigned long startclk, stopclk;
	struct dasd_device *startdev;

	BUG_ON(cqr->refers == NULL || cqr->function == NULL);

	success = cqr->status == DASD_CQR_DONE;
	startclk = cqr->startclk;
	stopclk = cqr->stopclk;
	startdev = cqr->startdev;

	/* free all ERPs - but NOT the original cqr */
	while (cqr->refers != NULL) {
		struct dasd_ccw_req *refers;

		refers = cqr->refers;
		/* remove the request from the block queue */
		list_del(&cqr->blocklist);
		/* free the finished erp request */
		dasd_free_erp_request(cqr, cqr->memdev);
		cqr = refers;
	}

	/* set corresponding status to original cqr */
	cqr->startclk = startclk;
	cqr->stopclk = stopclk;
	cqr->startdev = startdev;
	if (success)
		cqr->status = DASD_CQR_DONE;
	else {
		cqr->status = DASD_CQR_FAILED;
		cqr->stopclk = get_tod_clock();
	}

	return cqr;

}				/* end default_erp_postaction */

void
dasd_log_sense(struct dasd_ccw_req *cqr, struct irb *irb)
{
	struct dasd_device *device;

	device = cqr->startdev;
	if (cqr->intrc == -ETIMEDOUT) {
		dev_err(&device->cdev->dev,
			"A timeout error occurred for cqr %p\n", cqr);
		return;
	}
	if (cqr->intrc == -ENOLINK) {
		dev_err(&device->cdev->dev,
			"A transport error occurred for cqr %p\n", cqr);
		return;
	}
	/* dump sense data */
	if (device->discipline && device->discipline->dump_sense)
		device->discipline->dump_sense(device, cqr, irb);
}

void
dasd_log_sense_dbf(struct dasd_ccw_req *cqr, struct irb *irb)
{
	struct dasd_device *device;

	device = cqr->startdev;
	/* dump sense data to s390 debugfeature*/
	if (device->discipline && device->discipline->dump_sense_dbf)
		device->discipline->dump_sense_dbf(device, irb, "log");
}
EXPORT_SYMBOL(dasd_log_sense_dbf);

EXPORT_SYMBOL(dasd_default_erp_action);
EXPORT_SYMBOL(dasd_default_erp_postaction);
EXPORT_SYMBOL(dasd_alloc_erp_request);
EXPORT_SYMBOL(dasd_free_erp_request);
EXPORT_SYMBOL(dasd_log_sense);

