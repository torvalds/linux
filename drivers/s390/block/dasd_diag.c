/*
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Based on.......: linux/drivers/s390/block/mdisk.c
 * ...............: by Hartmunt Penner <hpenner@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * Copyright IBM Corp. 1999, 2000
 *
 */

#define KMSG_COMPONENT "dasd"

#include <linux/kernel_stat.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/hdreg.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/jiffies.h>

#include <asm/dasd.h>
#include <asm/debug.h>
#include <asm/diag.h>
#include <asm/ebcdic.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/vtoc.h>
#include <asm/diag.h>

#include "dasd_int.h"
#include "dasd_diag.h"

#define PRINTK_HEADER "dasd(diag):"

MODULE_LICENSE("GPL");

/* The maximum number of blocks per request (max_blocks) is dependent on the
 * amount of storage that is available in the static I/O buffer for each
 * device. Currently each device gets 2 pages. We want to fit two requests
 * into the available memory so that we can immediately start the next if one
 * finishes. */
#define DIAG_MAX_BLOCKS	(((2 * PAGE_SIZE - sizeof(struct dasd_ccw_req) - \
			   sizeof(struct dasd_diag_req)) / \
		           sizeof(struct dasd_diag_bio)) / 2)
#define DIAG_MAX_RETRIES	32
#define DIAG_TIMEOUT		50

static struct dasd_discipline dasd_diag_discipline;

struct dasd_diag_private {
	struct dasd_diag_characteristics rdc_data;
	struct dasd_diag_rw_io iob;
	struct dasd_diag_init_io iib;
	blocknum_t pt_block;
	struct ccw_dev_id dev_id;
};

struct dasd_diag_req {
	unsigned int block_count;
	struct dasd_diag_bio bio[0];
};

static const u8 DASD_DIAG_CMS1[] = { 0xc3, 0xd4, 0xe2, 0xf1 };/* EBCDIC CMS1 */

/* Perform DIAG250 call with block I/O parameter list iob (input and output)
 * and function code cmd.
 * In case of an exception return 3. Otherwise return result of bitwise OR of
 * resulting condition code and DIAG return code. */
static inline int __dia250(void *iob, int cmd)
{
	register unsigned long reg2 asm ("2") = (unsigned long) iob;
	typedef union {
		struct dasd_diag_init_io init_io;
		struct dasd_diag_rw_io rw_io;
	} addr_type;
	int rc;

	rc = 3;
	asm volatile(
		"	diag	2,%2,0x250\n"
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"	or	%0,3\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+d" (rc), "=m" (*(addr_type *) iob)
		: "d" (cmd), "d" (reg2), "m" (*(addr_type *) iob)
		: "3", "cc");
	return rc;
}

static inline int dia250(void *iob, int cmd)
{
	diag_stat_inc(DIAG_STAT_X250);
	return __dia250(iob, cmd);
}

/* Initialize block I/O to DIAG device using the specified blocksize and
 * block offset. On success, return zero and set end_block to contain the
 * number of blocks on the device minus the specified offset. Return non-zero
 * otherwise. */
static inline int
mdsk_init_io(struct dasd_device *device, unsigned int blocksize,
	     blocknum_t offset, blocknum_t *end_block)
{
	struct dasd_diag_private *private;
	struct dasd_diag_init_io *iib;
	int rc;

	private = (struct dasd_diag_private *) device->private;
	iib = &private->iib;
	memset(iib, 0, sizeof (struct dasd_diag_init_io));

	iib->dev_nr = private->dev_id.devno;
	iib->block_size = blocksize;
	iib->offset = offset;
	iib->flaga = DASD_DIAG_FLAGA_DEFAULT;

	rc = dia250(iib, INIT_BIO);

	if ((rc & 3) == 0 && end_block)
		*end_block = iib->end_block;

	return rc;
}

/* Remove block I/O environment for device. Return zero on success, non-zero
 * otherwise. */
static inline int
mdsk_term_io(struct dasd_device * device)
{
	struct dasd_diag_private *private;
	struct dasd_diag_init_io *iib;
	int rc;

	private = (struct dasd_diag_private *) device->private;
	iib = &private->iib;
	memset(iib, 0, sizeof (struct dasd_diag_init_io));
	iib->dev_nr = private->dev_id.devno;
	rc = dia250(iib, TERM_BIO);
	return rc;
}

/* Error recovery for failed DIAG requests - try to reestablish the DIAG
 * environment. */
static void
dasd_diag_erp(struct dasd_device *device)
{
	int rc;

	mdsk_term_io(device);
	rc = mdsk_init_io(device, device->block->bp_block, 0, NULL);
	if (rc == 4) {
		if (!(test_and_set_bit(DASD_FLAG_DEVICE_RO, &device->flags)))
			pr_warning("%s: The access mode of a DIAG device "
				   "changed to read-only\n",
				   dev_name(&device->cdev->dev));
		rc = 0;
	}
	if (rc)
		pr_warning("%s: DIAG ERP failed with "
			    "rc=%d\n", dev_name(&device->cdev->dev), rc);
}

/* Start a given request at the device. Return zero on success, non-zero
 * otherwise. */
static int
dasd_start_diag(struct dasd_ccw_req * cqr)
{
	struct dasd_device *device;
	struct dasd_diag_private *private;
	struct dasd_diag_req *dreq;
	int rc;

	device = cqr->startdev;
	if (cqr->retries < 0) {
		DBF_DEV_EVENT(DBF_ERR, device, "DIAG start_IO: request %p "
			    "- no retry left)", cqr);
		cqr->status = DASD_CQR_ERROR;
		return -EIO;
	}
	private = (struct dasd_diag_private *) device->private;
	dreq = (struct dasd_diag_req *) cqr->data;

	private->iob.dev_nr = private->dev_id.devno;
	private->iob.key = 0;
	private->iob.flags = DASD_DIAG_RWFLAG_ASYNC;
	private->iob.block_count = dreq->block_count;
	private->iob.interrupt_params = (addr_t) cqr;
	private->iob.bio_list = dreq->bio;
	private->iob.flaga = DASD_DIAG_FLAGA_DEFAULT;

	cqr->startclk = get_tod_clock();
	cqr->starttime = jiffies;
	cqr->retries--;

	rc = dia250(&private->iob, RW_BIO);
	switch (rc) {
	case 0: /* Synchronous I/O finished successfully */
		cqr->stopclk = get_tod_clock();
		cqr->status = DASD_CQR_SUCCESS;
		/* Indicate to calling function that only a dasd_schedule_bh()
		   and no timer is needed */
                rc = -EACCES;
		break;
	case 8: /* Asynchronous I/O was started */
		cqr->status = DASD_CQR_IN_IO;
		rc = 0;
		break;
	default: /* Error condition */
		cqr->status = DASD_CQR_QUEUED;
		DBF_DEV_EVENT(DBF_WARNING, device, "dia250 returned rc=%d", rc);
		dasd_diag_erp(device);
		rc = -EIO;
		break;
	}
	cqr->intrc = rc;
	return rc;
}

/* Terminate given request at the device. */
static int
dasd_diag_term_IO(struct dasd_ccw_req * cqr)
{
	struct dasd_device *device;

	device = cqr->startdev;
	mdsk_term_io(device);
	mdsk_init_io(device, device->block->bp_block, 0, NULL);
	cqr->status = DASD_CQR_CLEAR_PENDING;
	cqr->stopclk = get_tod_clock();
	dasd_schedule_device_bh(device);
	return 0;
}

/* Handle external interruption. */
static void dasd_ext_handler(struct ext_code ext_code,
			     unsigned int param32, unsigned long param64)
{
	struct dasd_ccw_req *cqr, *next;
	struct dasd_device *device;
	unsigned long long expires;
	unsigned long flags;
	addr_t ip;
	int rc;

	switch (ext_code.subcode >> 8) {
	case DASD_DIAG_CODE_31BIT:
		ip = (addr_t) param32;
		break;
	case DASD_DIAG_CODE_64BIT:
		ip = (addr_t) param64;
		break;
	default:
		return;
	}
	inc_irq_stat(IRQEXT_DSD);
	if (!ip) {		/* no intparm: unsolicited interrupt */
		DBF_EVENT(DBF_NOTICE, "%s", "caught unsolicited "
			      "interrupt");
		return;
	}
	cqr = (struct dasd_ccw_req *) ip;
	device = (struct dasd_device *) cqr->startdev;
	if (strncmp(device->discipline->ebcname, (char *) &cqr->magic, 4)) {
		DBF_DEV_EVENT(DBF_WARNING, device,
			    " magic number of dasd_ccw_req 0x%08X doesn't"
			    " match discipline 0x%08X",
			    cqr->magic, *(int *) (&device->discipline->name));
		return;
	}

	/* get irq lock to modify request queue */
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);

	/* Check for a pending clear operation */
	if (cqr->status == DASD_CQR_CLEAR_PENDING) {
		cqr->status = DASD_CQR_CLEARED;
		dasd_device_clear_timer(device);
		dasd_schedule_device_bh(device);
		spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
		return;
	}

	cqr->stopclk = get_tod_clock();

	expires = 0;
	if ((ext_code.subcode & 0xff) == 0) {
		cqr->status = DASD_CQR_SUCCESS;
		/* Start first request on queue if possible -> fast_io. */
		if (!list_empty(&device->ccw_queue)) {
			next = list_entry(device->ccw_queue.next,
					  struct dasd_ccw_req, devlist);
			if (next->status == DASD_CQR_QUEUED) {
				rc = dasd_start_diag(next);
				if (rc == 0)
					expires = next->expires;
			}
		}
	} else {
		cqr->status = DASD_CQR_QUEUED;
		DBF_DEV_EVENT(DBF_DEBUG, device, "interrupt status for "
			      "request %p was %d (%d retries left)", cqr,
			      ext_code.subcode & 0xff, cqr->retries);
		dasd_diag_erp(device);
	}

	if (expires != 0)
		dasd_device_set_timer(device, expires);
	else
		dasd_device_clear_timer(device);
	dasd_schedule_device_bh(device);

	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
}

/* Check whether device can be controlled by DIAG discipline. Return zero on
 * success, non-zero otherwise. */
static int
dasd_diag_check_device(struct dasd_device *device)
{
	struct dasd_block *block;
	struct dasd_diag_private *private;
	struct dasd_diag_characteristics *rdc_data;
	struct dasd_diag_bio bio;
	struct vtoc_cms_label *label;
	blocknum_t end_block;
	unsigned int sb, bsize;
	int rc;

	private = (struct dasd_diag_private *) device->private;
	if (private == NULL) {
		private = kzalloc(sizeof(struct dasd_diag_private),GFP_KERNEL);
		if (private == NULL) {
			DBF_DEV_EVENT(DBF_WARNING, device, "%s",
				"Allocating memory for private DASD data "
				      "failed\n");
			return -ENOMEM;
		}
		ccw_device_get_id(device->cdev, &private->dev_id);
		device->private = (void *) private;
	}
	block = dasd_alloc_block();
	if (IS_ERR(block)) {
		DBF_DEV_EVENT(DBF_WARNING, device, "%s",
			    "could not allocate dasd block structure");
		device->private = NULL;
		kfree(private);
		return PTR_ERR(block);
	}
	device->block = block;
	block->base = device;

	/* Read Device Characteristics */
	rdc_data = (void *) &(private->rdc_data);
	rdc_data->dev_nr = private->dev_id.devno;
	rdc_data->rdc_len = sizeof (struct dasd_diag_characteristics);

	rc = diag210((struct diag210 *) rdc_data);
	if (rc) {
		DBF_DEV_EVENT(DBF_WARNING, device, "failed to retrieve device "
			    "information (rc=%d)", rc);
		rc = -EOPNOTSUPP;
		goto out;
	}

	device->default_expires = DIAG_TIMEOUT;
	device->default_retries = DIAG_MAX_RETRIES;

	/* Figure out position of label block */
	switch (private->rdc_data.vdev_class) {
	case DEV_CLASS_FBA:
		private->pt_block = 1;
		break;
	case DEV_CLASS_ECKD:
		private->pt_block = 2;
		break;
	default:
		pr_warning("%s: Device type %d is not supported "
			   "in DIAG mode\n", dev_name(&device->cdev->dev),
			   private->rdc_data.vdev_class);
		rc = -EOPNOTSUPP;
		goto out;
	}

	DBF_DEV_EVENT(DBF_INFO, device,
		      "%04X: %04X on real %04X/%02X",
		      rdc_data->dev_nr,
		      rdc_data->vdev_type,
		      rdc_data->rdev_type, rdc_data->rdev_model);

	/* terminate all outstanding operations */
	mdsk_term_io(device);

	/* figure out blocksize of device */
	label = (struct vtoc_cms_label *) get_zeroed_page(GFP_KERNEL);
	if (label == NULL)  {
		DBF_DEV_EVENT(DBF_WARNING, device, "%s",
			    "No memory to allocate initialization request");
		rc = -ENOMEM;
		goto out;
	}
	rc = 0;
	end_block = 0;
	/* try all sizes - needed for ECKD devices */
	for (bsize = 512; bsize <= PAGE_SIZE; bsize <<= 1) {
		mdsk_init_io(device, bsize, 0, &end_block);
		memset(&bio, 0, sizeof (struct dasd_diag_bio));
		bio.type = MDSK_READ_REQ;
		bio.block_number = private->pt_block + 1;
		bio.buffer = label;
		memset(&private->iob, 0, sizeof (struct dasd_diag_rw_io));
		private->iob.dev_nr = rdc_data->dev_nr;
		private->iob.key = 0;
		private->iob.flags = 0;	/* do synchronous io */
		private->iob.block_count = 1;
		private->iob.interrupt_params = 0;
		private->iob.bio_list = &bio;
		private->iob.flaga = DASD_DIAG_FLAGA_DEFAULT;
		rc = dia250(&private->iob, RW_BIO);
		if (rc == 3) {
			pr_warning("%s: A 64-bit DIAG call failed\n",
				   dev_name(&device->cdev->dev));
			rc = -EOPNOTSUPP;
			goto out_label;
		}
		mdsk_term_io(device);
		if (rc == 0)
			break;
	}
	if (bsize > PAGE_SIZE) {
		pr_warning("%s: Accessing the DASD failed because of an "
			   "incorrect format (rc=%d)\n",
			   dev_name(&device->cdev->dev), rc);
		rc = -EIO;
		goto out_label;
	}
	/* check for label block */
	if (memcmp(label->label_id, DASD_DIAG_CMS1,
		  sizeof(DASD_DIAG_CMS1)) == 0) {
		/* get formatted blocksize from label block */
		bsize = (unsigned int) label->block_size;
		block->blocks = (unsigned long) label->block_count;
	} else
		block->blocks = end_block;
	block->bp_block = bsize;
	block->s2b_shift = 0;	/* bits to shift 512 to get a block */
	for (sb = 512; sb < bsize; sb = sb << 1)
		block->s2b_shift++;
	rc = mdsk_init_io(device, block->bp_block, 0, NULL);
	if (rc && (rc != 4)) {
		pr_warning("%s: DIAG initialization failed with rc=%d\n",
			   dev_name(&device->cdev->dev), rc);
		rc = -EIO;
	} else {
		if (rc == 4)
			set_bit(DASD_FLAG_DEVICE_RO, &device->flags);
		pr_info("%s: New DASD with %ld byte/block, total size %ld "
			"KB%s\n", dev_name(&device->cdev->dev),
			(unsigned long) block->bp_block,
			(unsigned long) (block->blocks <<
					 block->s2b_shift) >> 1,
			(rc == 4) ? ", read-only device" : "");
		rc = 0;
	}
out_label:
	free_page((long) label);
out:
	if (rc) {
		device->block = NULL;
		dasd_free_block(block);
		device->private = NULL;
		kfree(private);
	}
	return rc;
}

/* Fill in virtual disk geometry for device. Return zero on success, non-zero
 * otherwise. */
static int
dasd_diag_fill_geometry(struct dasd_block *block, struct hd_geometry *geo)
{
	if (dasd_check_blocksize(block->bp_block) != 0)
		return -EINVAL;
	geo->cylinders = (block->blocks << block->s2b_shift) >> 10;
	geo->heads = 16;
	geo->sectors = 128 >> block->s2b_shift;
	return 0;
}

static dasd_erp_fn_t
dasd_diag_erp_action(struct dasd_ccw_req * cqr)
{
	return dasd_default_erp_action;
}

static dasd_erp_fn_t
dasd_diag_erp_postaction(struct dasd_ccw_req * cqr)
{
	return dasd_default_erp_postaction;
}

/* Create DASD request from block device request. Return pointer to new
 * request on success, ERR_PTR otherwise. */
static struct dasd_ccw_req *dasd_diag_build_cp(struct dasd_device *memdev,
					       struct dasd_block *block,
					       struct request *req)
{
	struct dasd_ccw_req *cqr;
	struct dasd_diag_req *dreq;
	struct dasd_diag_bio *dbio;
	struct req_iterator iter;
	struct bio_vec bv;
	char *dst;
	unsigned int count, datasize;
	sector_t recid, first_rec, last_rec;
	unsigned int blksize, off;
	unsigned char rw_cmd;

	if (rq_data_dir(req) == READ)
		rw_cmd = MDSK_READ_REQ;
	else if (rq_data_dir(req) == WRITE)
		rw_cmd = MDSK_WRITE_REQ;
	else
		return ERR_PTR(-EINVAL);
	blksize = block->bp_block;
	/* Calculate record id of first and last block. */
	first_rec = blk_rq_pos(req) >> block->s2b_shift;
	last_rec =
		(blk_rq_pos(req) + blk_rq_sectors(req) - 1) >> block->s2b_shift;
	/* Check struct bio and count the number of blocks for the request. */
	count = 0;
	rq_for_each_segment(bv, req, iter) {
		if (bv.bv_len & (blksize - 1))
			/* Fba can only do full blocks. */
			return ERR_PTR(-EINVAL);
		count += bv.bv_len >> (block->s2b_shift + 9);
	}
	/* Paranoia. */
	if (count != last_rec - first_rec + 1)
		return ERR_PTR(-EINVAL);
	/* Build the request */
	datasize = sizeof(struct dasd_diag_req) +
		count*sizeof(struct dasd_diag_bio);
	cqr = dasd_smalloc_request(DASD_DIAG_MAGIC, 0, datasize, memdev);
	if (IS_ERR(cqr))
		return cqr;

	dreq = (struct dasd_diag_req *) cqr->data;
	dreq->block_count = count;
	dbio = dreq->bio;
	recid = first_rec;
	rq_for_each_segment(bv, req, iter) {
		dst = page_address(bv.bv_page) + bv.bv_offset;
		for (off = 0; off < bv.bv_len; off += blksize) {
			memset(dbio, 0, sizeof (struct dasd_diag_bio));
			dbio->type = rw_cmd;
			dbio->block_number = recid + 1;
			dbio->buffer = dst;
			dbio++;
			dst += blksize;
			recid++;
		}
	}
	cqr->retries = memdev->default_retries;
	cqr->buildclk = get_tod_clock();
	if (blk_noretry_request(req) ||
	    block->base->features & DASD_FEATURE_FAILFAST)
		set_bit(DASD_CQR_FLAGS_FAILFAST, &cqr->flags);
	cqr->startdev = memdev;
	cqr->memdev = memdev;
	cqr->block = block;
	cqr->expires = memdev->default_expires * HZ;
	cqr->status = DASD_CQR_FILLED;
	return cqr;
}

/* Release DASD request. Return non-zero if request was successful, zero
 * otherwise. */
static int
dasd_diag_free_cp(struct dasd_ccw_req *cqr, struct request *req)
{
	int status;

	status = cqr->status == DASD_CQR_DONE;
	dasd_sfree_request(cqr, cqr->memdev);
	return status;
}

static void dasd_diag_handle_terminated_request(struct dasd_ccw_req *cqr)
{
	if (cqr->retries < 0)
		cqr->status = DASD_CQR_FAILED;
	else
		cqr->status = DASD_CQR_FILLED;
};

/* Fill in IOCTL data for device. */
static int
dasd_diag_fill_info(struct dasd_device * device,
		    struct dasd_information2_t * info)
{
	struct dasd_diag_private *private;

	private = (struct dasd_diag_private *) device->private;
	info->label_block = (unsigned int) private->pt_block;
	info->FBA_layout = 1;
	info->format = DASD_FORMAT_LDL;
	info->characteristics_size = sizeof (struct dasd_diag_characteristics);
	memcpy(info->characteristics,
	       &((struct dasd_diag_private *) device->private)->rdc_data,
	       sizeof (struct dasd_diag_characteristics));
	info->confdata_size = 0;
	return 0;
}

static void
dasd_diag_dump_sense(struct dasd_device *device, struct dasd_ccw_req * req,
		     struct irb *stat)
{
	DBF_DEV_EVENT(DBF_WARNING, device, "%s",
		    "dump sense not available for DIAG data");
}

static struct dasd_discipline dasd_diag_discipline = {
	.owner = THIS_MODULE,
	.name = "DIAG",
	.ebcname = "DIAG",
	.max_blocks = DIAG_MAX_BLOCKS,
	.check_device = dasd_diag_check_device,
	.verify_path = dasd_generic_verify_path,
	.fill_geometry = dasd_diag_fill_geometry,
	.start_IO = dasd_start_diag,
	.term_IO = dasd_diag_term_IO,
	.handle_terminated_request = dasd_diag_handle_terminated_request,
	.erp_action = dasd_diag_erp_action,
	.erp_postaction = dasd_diag_erp_postaction,
	.build_cp = dasd_diag_build_cp,
	.free_cp = dasd_diag_free_cp,
	.dump_sense = dasd_diag_dump_sense,
	.fill_info = dasd_diag_fill_info,
};

static int __init
dasd_diag_init(void)
{
	if (!MACHINE_IS_VM) {
		pr_info("Discipline %s cannot be used without z/VM\n",
			dasd_diag_discipline.name);
		return -ENODEV;
	}
	ASCEBC(dasd_diag_discipline.ebcname, 4);

	irq_subclass_register(IRQ_SUBCLASS_SERVICE_SIGNAL);
	register_external_irq(EXT_IRQ_CP_SERVICE, dasd_ext_handler);
	dasd_diag_discipline_pointer = &dasd_diag_discipline;
	return 0;
}

static void __exit
dasd_diag_cleanup(void)
{
	unregister_external_irq(EXT_IRQ_CP_SERVICE, dasd_ext_handler);
	irq_subclass_unregister(IRQ_SUBCLASS_SERVICE_SIGNAL);
	dasd_diag_discipline_pointer = NULL;
}

module_init(dasd_diag_init);
module_exit(dasd_diag_cleanup);
