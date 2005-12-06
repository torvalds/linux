/* 
 * File...........: linux/drivers/s390/block/dasd_diag.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Based on.......: linux/drivers/s390/block/mdisk.c
 * ...............: by Hartmunt Penner <hpenner@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 *
 * $Revision: 1.51 $
 */

#include <linux/config.h>
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
#include <asm/ebcdic.h>
#include <asm/io.h>
#include <asm/s390_ext.h>
#include <asm/todclk.h>

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
#define DIAG_TIMEOUT		50 * HZ

struct dasd_discipline dasd_diag_discipline;

struct dasd_diag_private {
	struct dasd_diag_characteristics rdc_data;
	struct dasd_diag_rw_io iob;
	struct dasd_diag_init_io iib;
	blocknum_t pt_block;
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
static __inline__ int
dia250(void *iob, int cmd)
{
	typedef union {
		struct dasd_diag_init_io init_io;
		struct dasd_diag_rw_io rw_io;
	} addr_type;
	int rc;

	__asm__ __volatile__(
#ifdef CONFIG_ARCH_S390X
		"	lghi	%0,3\n"
		"	lgr	0,%3\n"
		"	diag	0,%2,0x250\n"
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"	or	%0,1\n"
		"1:\n"
		".section __ex_table,\"a\"\n"
		"	.align 8\n"
		"	.quad  0b,1b\n"
		".previous\n"
#else
		"	lhi	%0,3\n"
		"	lr	0,%3\n"
		"	diag	0,%2,0x250\n"
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"	or	%0,1\n"
		"1:\n"
		".section __ex_table,\"a\"\n"
		"	.align 4\n"
		"	.long 0b,1b\n"
		".previous\n"
#endif
		: "=&d" (rc), "=m" (*(addr_type *) iob)
		: "d" (cmd), "d" (iob), "m" (*(addr_type *) iob)
		: "0", "1", "cc");
	return rc;
}

/* Initialize block I/O to DIAG device using the specified blocksize and
 * block offset. On success, return zero and set end_block to contain the
 * number of blocks on the device minus the specified offset. Return non-zero
 * otherwise. */
static __inline__ int
mdsk_init_io(struct dasd_device *device, unsigned int blocksize,
	     blocknum_t offset, blocknum_t *end_block)
{
	struct dasd_diag_private *private;
	struct dasd_diag_init_io *iib;
	int rc;

	private = (struct dasd_diag_private *) device->private;
	iib = &private->iib;
	memset(iib, 0, sizeof (struct dasd_diag_init_io));

	iib->dev_nr = _ccw_device_get_device_number(device->cdev);
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
static __inline__ int
mdsk_term_io(struct dasd_device * device)
{
	struct dasd_diag_private *private;
	struct dasd_diag_init_io *iib;
	int rc;

	private = (struct dasd_diag_private *) device->private;
	iib = &private->iib;
	memset(iib, 0, sizeof (struct dasd_diag_init_io));
	iib->dev_nr = _ccw_device_get_device_number(device->cdev);
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
	rc = mdsk_init_io(device, device->bp_block, 0, NULL);
	if (rc)
		DEV_MESSAGE(KERN_WARNING, device, "DIAG ERP unsuccessful, "
			    "rc=%d", rc);
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

	device = cqr->device;
	if (cqr->retries < 0) {
		DEV_MESSAGE(KERN_WARNING, device, "DIAG start_IO: request %p "
			    "- no retry left)", cqr);
		cqr->status = DASD_CQR_FAILED;
		return -EIO;
	}
	private = (struct dasd_diag_private *) device->private;
	dreq = (struct dasd_diag_req *) cqr->data;

	private->iob.dev_nr = _ccw_device_get_device_number(device->cdev);
	private->iob.key = 0;
	private->iob.flags = DASD_DIAG_RWFLAG_ASYNC;
	private->iob.block_count = dreq->block_count;
	private->iob.interrupt_params = (addr_t) cqr;
	private->iob.bio_list = dreq->bio;
	private->iob.flaga = DASD_DIAG_FLAGA_DEFAULT;

	cqr->startclk = get_clock();
	cqr->starttime = jiffies;
	cqr->retries--;

	rc = dia250(&private->iob, RW_BIO);
	switch (rc) {
	case 0: /* Synchronous I/O finished successfully */
		cqr->stopclk = get_clock();
		cqr->status = DASD_CQR_DONE;
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
		DEV_MESSAGE(KERN_WARNING, device, "dia250 returned rc=%d", rc);
		dasd_diag_erp(device);
		rc = -EIO;
		break;
	}
	return rc;
}

/* Terminate given request at the device. */
static int
dasd_diag_term_IO(struct dasd_ccw_req * cqr)
{
	struct dasd_device *device;

	device = cqr->device;
	mdsk_term_io(device);
	mdsk_init_io(device, device->bp_block, 0, NULL);
	cqr->status = DASD_CQR_CLEAR;
	cqr->stopclk = get_clock();
	dasd_schedule_bh(device);
	return 0;
}

/* Handle external interruption. */
static void
dasd_ext_handler(struct pt_regs *regs, __u16 code)
{
	struct dasd_ccw_req *cqr, *next;
	struct dasd_device *device;
	unsigned long long expires;
	unsigned long flags;
	u8 int_code, status;
	addr_t ip;
	int rc;

	int_code = *((u8 *) DASD_DIAG_LC_INT_CODE);
	status = *((u8 *) DASD_DIAG_LC_INT_STATUS);
	switch (int_code) {
	case DASD_DIAG_CODE_31BIT:
		ip = (addr_t) *((u32 *) DASD_DIAG_LC_INT_PARM_31BIT);
		break;
	case DASD_DIAG_CODE_64BIT:
		ip = (addr_t) *((u64 *) DASD_DIAG_LC_INT_PARM_64BIT);
		break;
	default:
		return;
	}
	if (!ip) {		/* no intparm: unsolicited interrupt */
		MESSAGE(KERN_DEBUG, "%s", "caught unsolicited interrupt");
		return;
	}
	cqr = (struct dasd_ccw_req *) ip;
	device = (struct dasd_device *) cqr->device;
	if (strncmp(device->discipline->ebcname, (char *) &cqr->magic, 4)) {
		DEV_MESSAGE(KERN_WARNING, device,
			    " magic number of dasd_ccw_req 0x%08X doesn't"
			    " match discipline 0x%08X",
			    cqr->magic, *(int *) (&device->discipline->name));
		return;
	}

	/* get irq lock to modify request queue */
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);

	/* Check for a pending clear operation */
	if (cqr->status == DASD_CQR_CLEAR) {
		cqr->status = DASD_CQR_QUEUED;
		dasd_clear_timer(device);
		dasd_schedule_bh(device);
		spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
		return;
	}

	cqr->stopclk = get_clock();

	expires = 0;
	if (status == 0) {
		cqr->status = DASD_CQR_DONE;
		/* Start first request on queue if possible -> fast_io. */
		if (!list_empty(&device->ccw_queue)) {
			next = list_entry(device->ccw_queue.next,
					  struct dasd_ccw_req, list);
			if (next->status == DASD_CQR_QUEUED) {
				rc = dasd_start_diag(next);
				if (rc == 0)
					expires = next->expires;
				else if (rc != -EACCES)
					DEV_MESSAGE(KERN_WARNING, device, "%s",
						    "Interrupt fastpath "
						    "failed!");
			}
		}
	} else {
		cqr->status = DASD_CQR_QUEUED;
		DEV_MESSAGE(KERN_WARNING, device, "interrupt status for "
			    "request %p was %d (%d retries left)", cqr, status,
			    cqr->retries);
		dasd_diag_erp(device);
	}

	if (expires != 0)
		dasd_set_timer(device, expires);
	else
		dasd_clear_timer(device);
	dasd_schedule_bh(device);

	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
}

/* Check whether device can be controlled by DIAG discipline. Return zero on
 * success, non-zero otherwise. */
static int
dasd_diag_check_device(struct dasd_device *device)
{
	struct dasd_diag_private *private;
	struct dasd_diag_characteristics *rdc_data;
	struct dasd_diag_bio bio;
	struct dasd_diag_cms_label *label;
	blocknum_t end_block;
	unsigned int sb, bsize;
	int rc;

	private = (struct dasd_diag_private *) device->private;
	if (private == NULL) {
		private = kmalloc(sizeof(struct dasd_diag_private),GFP_KERNEL);
		if (private == NULL) {
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				"memory allocation failed for private data");
			return -ENOMEM;
		}
		device->private = (void *) private;
	}
	/* Read Device Characteristics */
	rdc_data = (void *) &(private->rdc_data);
	rdc_data->dev_nr = _ccw_device_get_device_number(device->cdev);
	rdc_data->rdc_len = sizeof (struct dasd_diag_characteristics);

	rc = diag210((struct diag210 *) rdc_data);
	if (rc) {
		DEV_MESSAGE(KERN_WARNING, device, "failed to retrieve device "
			    "information (rc=%d)", rc);
		return -ENOTSUPP;
	}

	/* Figure out position of label block */
	switch (private->rdc_data.vdev_class) {
	case DEV_CLASS_FBA:
		private->pt_block = 1;
		break;
	case DEV_CLASS_ECKD:
		private->pt_block = 2;
		break;
	default:
		DEV_MESSAGE(KERN_WARNING, device, "unsupported device class "
			    "(class=%d)", private->rdc_data.vdev_class);
		return -ENOTSUPP;
	}

	DBF_DEV_EVENT(DBF_INFO, device,
		      "%04X: %04X on real %04X/%02X",
		      rdc_data->dev_nr,
		      rdc_data->vdev_type,
		      rdc_data->rdev_type, rdc_data->rdev_model);

	/* terminate all outstanding operations */
	mdsk_term_io(device);

	/* figure out blocksize of device */
	label = (struct dasd_diag_cms_label *) get_zeroed_page(GFP_KERNEL);
	if (label == NULL)  {
		DEV_MESSAGE(KERN_WARNING, device, "%s",
			    "No memory to allocate initialization request");
		return -ENOMEM;
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
			DEV_MESSAGE(KERN_WARNING, device, "%s",
				"DIAG call failed");
			rc = -EOPNOTSUPP;
			goto out;
		}
		mdsk_term_io(device);
		if (rc == 0)
			break;
	}
	if (bsize > PAGE_SIZE) {
		DEV_MESSAGE(KERN_WARNING, device, "device access failed "
			    "(rc=%d)", rc);
		rc = -EIO;
		goto out;
	}
	/* check for label block */
	if (memcmp(label->label_id, DASD_DIAG_CMS1,
		  sizeof(DASD_DIAG_CMS1)) == 0) {
		/* get formatted blocksize from label block */
		bsize = (unsigned int) label->block_size;
		device->blocks = (unsigned long) label->block_count;
	} else
		device->blocks = end_block;
	device->bp_block = bsize;
	device->s2b_shift = 0;	/* bits to shift 512 to get a block */
	for (sb = 512; sb < bsize; sb = sb << 1)
		device->s2b_shift++;
	rc = mdsk_init_io(device, device->bp_block, 0, NULL);
	if (rc) {
		DEV_MESSAGE(KERN_WARNING, device, "DIAG initialization "
			"failed (rc=%d)", rc);
		rc = -EIO;
	} else {
		DEV_MESSAGE(KERN_INFO, device,
			    "(%ld B/blk): %ldkB",
			    (unsigned long) device->bp_block,
			    (unsigned long) (device->blocks <<
				device->s2b_shift) >> 1);
	}
out:
	free_page((long) label);
	return rc;
}

/* Fill in virtual disk geometry for device. Return zero on success, non-zero
 * otherwise. */
static int
dasd_diag_fill_geometry(struct dasd_device *device, struct hd_geometry *geo)
{
	if (dasd_check_blocksize(device->bp_block) != 0)
		return -EINVAL;
	geo->cylinders = (device->blocks << device->s2b_shift) >> 10;
	geo->heads = 16;
	geo->sectors = 128 >> device->s2b_shift;
	return 0;
}

static dasd_era_t
dasd_diag_examine_error(struct dasd_ccw_req * cqr, struct irb * stat)
{
	return dasd_era_fatal;
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
static struct dasd_ccw_req *
dasd_diag_build_cp(struct dasd_device * device, struct request *req)
{
	struct dasd_ccw_req *cqr;
	struct dasd_diag_req *dreq;
	struct dasd_diag_bio *dbio;
	struct bio *bio;
	struct bio_vec *bv;
	char *dst;
	unsigned int count, datasize;
	sector_t recid, first_rec, last_rec;
	unsigned int blksize, off;
	unsigned char rw_cmd;
	int i;

	if (rq_data_dir(req) == READ)
		rw_cmd = MDSK_READ_REQ;
	else if (rq_data_dir(req) == WRITE)
		rw_cmd = MDSK_WRITE_REQ;
	else
		return ERR_PTR(-EINVAL);
	blksize = device->bp_block;
	/* Calculate record id of first and last block. */
	first_rec = req->sector >> device->s2b_shift;
	last_rec = (req->sector + req->nr_sectors - 1) >> device->s2b_shift;
	/* Check struct bio and count the number of blocks for the request. */
	count = 0;
	rq_for_each_bio(bio, req) {
		bio_for_each_segment(bv, bio, i) {
			if (bv->bv_len & (blksize - 1))
				/* Fba can only do full blocks. */
				return ERR_PTR(-EINVAL);
			count += bv->bv_len >> (device->s2b_shift + 9);
		}
	}
	/* Paranoia. */
	if (count != last_rec - first_rec + 1)
		return ERR_PTR(-EINVAL);
	/* Build the request */
	datasize = sizeof(struct dasd_diag_req) +
		count*sizeof(struct dasd_diag_bio);
	cqr = dasd_smalloc_request(dasd_diag_discipline.name, 0,
				   datasize, device);
	if (IS_ERR(cqr))
		return cqr;
	
	dreq = (struct dasd_diag_req *) cqr->data;
	dreq->block_count = count;
	dbio = dreq->bio;
	recid = first_rec;
	rq_for_each_bio(bio, req) {
		bio_for_each_segment(bv, bio, i) {
			dst = page_address(bv->bv_page) + bv->bv_offset;
			for (off = 0; off < bv->bv_len; off += blksize) {
				memset(dbio, 0, sizeof (struct dasd_diag_bio));
				dbio->type = rw_cmd;
				dbio->block_number = recid + 1;
				dbio->buffer = dst;
				dbio++;
				dst += blksize;
				recid++;
			}
		}
	}
	cqr->retries = DIAG_MAX_RETRIES;
	cqr->buildclk = get_clock();
	cqr->device = device;
	cqr->expires = DIAG_TIMEOUT;
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
	dasd_sfree_request(cqr, cqr->device);
	return status;
}

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
	DEV_MESSAGE(KERN_ERR, device, "%s",
		    "dump sense not available for DIAG data");
}

struct dasd_discipline dasd_diag_discipline = {
	.owner = THIS_MODULE,
	.name = "DIAG",
	.ebcname = "DIAG",
	.max_blocks = DIAG_MAX_BLOCKS,
	.check_device = dasd_diag_check_device,
	.fill_geometry = dasd_diag_fill_geometry,
	.start_IO = dasd_start_diag,
	.term_IO = dasd_diag_term_IO,
	.examine_error = dasd_diag_examine_error,
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
		MESSAGE_LOG(KERN_INFO,
			    "Machine is not VM: %s "
			    "discipline not initializing",
			    dasd_diag_discipline.name);
		return -ENODEV;
	}
	ASCEBC(dasd_diag_discipline.ebcname, 4);

	ctl_set_bit(0, 9);
	register_external_interrupt(0x2603, dasd_ext_handler);
	dasd_diag_discipline_pointer = &dasd_diag_discipline;
	return 0;
}

static void __exit
dasd_diag_cleanup(void)
{
	unregister_external_interrupt(0x2603, dasd_ext_handler);
	ctl_clear_bit(0, 9);
	dasd_diag_discipline_pointer = NULL;
}

module_init(dasd_diag_init);
module_exit(dasd_diag_cleanup);
