/*
 * File...........: linux/drivers/s390/block/dasd_fba.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * Copyright IBM Corp. 1999, 2009
 */

#define KMSG_COMPONENT "dasd-fba"

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <asm/debug.h>

#include <linux/slab.h>
#include <linux/hdreg.h>	/* HDIO_GETGEO			    */
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/init.h>

#include <asm/idals.h>
#include <asm/ebcdic.h>
#include <asm/io.h>
#include <asm/ccwdev.h>

#include "dasd_int.h"
#include "dasd_fba.h"

#ifdef PRINTK_HEADER
#undef PRINTK_HEADER
#endif				/* PRINTK_HEADER */
#define PRINTK_HEADER "dasd(fba):"

#define DASD_FBA_CCW_WRITE 0x41
#define DASD_FBA_CCW_READ 0x42
#define DASD_FBA_CCW_LOCATE 0x43
#define DASD_FBA_CCW_DEFINE_EXTENT 0x63

MODULE_LICENSE("GPL");

static struct dasd_discipline dasd_fba_discipline;

struct dasd_fba_private {
	struct dasd_fba_characteristics rdc_data;
};

static struct ccw_device_id dasd_fba_ids[] = {
	{ CCW_DEVICE_DEVTYPE (0x6310, 0, 0x9336, 0), .driver_info = 0x1},
	{ CCW_DEVICE_DEVTYPE (0x3880, 0, 0x3370, 0), .driver_info = 0x2},
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ccw, dasd_fba_ids);

static struct ccw_driver dasd_fba_driver; /* see below */
static int
dasd_fba_probe(struct ccw_device *cdev)
{
	return dasd_generic_probe(cdev, &dasd_fba_discipline);
}

static int
dasd_fba_set_online(struct ccw_device *cdev)
{
	return dasd_generic_set_online(cdev, &dasd_fba_discipline);
}

static struct ccw_driver dasd_fba_driver = {
	.name        = "dasd-fba",
	.owner       = THIS_MODULE,
	.ids         = dasd_fba_ids,
	.probe       = dasd_fba_probe,
	.remove      = dasd_generic_remove,
	.set_offline = dasd_generic_set_offline,
	.set_online  = dasd_fba_set_online,
	.notify      = dasd_generic_notify,
	.path_event  = dasd_generic_path_event,
	.freeze      = dasd_generic_pm_freeze,
	.thaw	     = dasd_generic_restore_device,
	.restore     = dasd_generic_restore_device,
};

static void
define_extent(struct ccw1 * ccw, struct DE_fba_data *data, int rw,
	      int blksize, int beg, int nr)
{
	ccw->cmd_code = DASD_FBA_CCW_DEFINE_EXTENT;
	ccw->flags = 0;
	ccw->count = 16;
	ccw->cda = (__u32) __pa(data);
	memset(data, 0, sizeof (struct DE_fba_data));
	if (rw == WRITE)
		(data->mask).perm = 0x0;
	else if (rw == READ)
		(data->mask).perm = 0x1;
	else
		data->mask.perm = 0x2;
	data->blk_size = blksize;
	data->ext_loc = beg;
	data->ext_end = nr - 1;
}

static void
locate_record(struct ccw1 * ccw, struct LO_fba_data *data, int rw,
	      int block_nr, int block_ct)
{
	ccw->cmd_code = DASD_FBA_CCW_LOCATE;
	ccw->flags = 0;
	ccw->count = 8;
	ccw->cda = (__u32) __pa(data);
	memset(data, 0, sizeof (struct LO_fba_data));
	if (rw == WRITE)
		data->operation.cmd = 0x5;
	else if (rw == READ)
		data->operation.cmd = 0x6;
	else
		data->operation.cmd = 0x8;
	data->blk_nr = block_nr;
	data->blk_ct = block_ct;
}

static int
dasd_fba_check_characteristics(struct dasd_device *device)
{
	struct dasd_block *block;
	struct dasd_fba_private *private;
	struct ccw_device *cdev = device->cdev;
	int rc;
	int readonly;

	private = (struct dasd_fba_private *) device->private;
	if (!private) {
		private = kzalloc(sizeof(*private), GFP_KERNEL | GFP_DMA);
		if (!private) {
			dev_warn(&device->cdev->dev,
				 "Allocating memory for private DASD "
				 "data failed\n");
			return -ENOMEM;
		}
		device->private = (void *) private;
	} else {
		memset(private, 0, sizeof(*private));
	}
	block = dasd_alloc_block();
	if (IS_ERR(block)) {
		DBF_EVENT_DEVID(DBF_WARNING, cdev, "%s", "could not allocate "
				"dasd block structure");
		device->private = NULL;
		kfree(private);
		return PTR_ERR(block);
	}
	device->block = block;
	block->base = device;

	/* Read Device Characteristics */
	rc = dasd_generic_read_dev_chars(device, DASD_FBA_MAGIC,
					 &private->rdc_data, 32);
	if (rc) {
		DBF_EVENT_DEVID(DBF_WARNING, cdev, "Read device "
				"characteristics returned error %d", rc);
		device->block = NULL;
		dasd_free_block(block);
		device->private = NULL;
		kfree(private);
		return rc;
	}

	device->default_expires = DASD_EXPIRES;
	device->path_data.opm = LPM_ANYPATH;

	readonly = dasd_device_is_ro(device);
	if (readonly)
		set_bit(DASD_FLAG_DEVICE_RO, &device->flags);

	dev_info(&device->cdev->dev,
		 "New FBA DASD %04X/%02X (CU %04X/%02X) with %d MB "
		 "and %d B/blk%s\n",
		 cdev->id.dev_type,
		 cdev->id.dev_model,
		 cdev->id.cu_type,
		 cdev->id.cu_model,
		 ((private->rdc_data.blk_bdsa *
		   (private->rdc_data.blk_size >> 9)) >> 11),
		 private->rdc_data.blk_size,
		 readonly ? ", read-only device" : "");
	return 0;
}

static int dasd_fba_do_analysis(struct dasd_block *block)
{
	struct dasd_fba_private *private;
	int sb, rc;

	private = (struct dasd_fba_private *) block->base->private;
	rc = dasd_check_blocksize(private->rdc_data.blk_size);
	if (rc) {
		DBF_DEV_EVENT(DBF_WARNING, block->base, "unknown blocksize %d",
			    private->rdc_data.blk_size);
		return rc;
	}
	block->blocks = private->rdc_data.blk_bdsa;
	block->bp_block = private->rdc_data.blk_size;
	block->s2b_shift = 0;	/* bits to shift 512 to get a block */
	for (sb = 512; sb < private->rdc_data.blk_size; sb = sb << 1)
		block->s2b_shift++;
	return 0;
}

static int dasd_fba_fill_geometry(struct dasd_block *block,
				  struct hd_geometry *geo)
{
	if (dasd_check_blocksize(block->bp_block) != 0)
		return -EINVAL;
	geo->cylinders = (block->blocks << block->s2b_shift) >> 10;
	geo->heads = 16;
	geo->sectors = 128 >> block->s2b_shift;
	return 0;
}

static dasd_erp_fn_t
dasd_fba_erp_action(struct dasd_ccw_req * cqr)
{
	return dasd_default_erp_action;
}

static dasd_erp_fn_t
dasd_fba_erp_postaction(struct dasd_ccw_req * cqr)
{
	if (cqr->function == dasd_default_erp_action)
		return dasd_default_erp_postaction;

	DBF_DEV_EVENT(DBF_WARNING, cqr->startdev, "unknown ERP action %p",
		    cqr->function);
	return NULL;
}

static void dasd_fba_handle_unsolicited_interrupt(struct dasd_device *device,
						   struct irb *irb)
{
	char mask;

	/* first of all check for state change pending interrupt */
	mask = DEV_STAT_ATTENTION | DEV_STAT_DEV_END | DEV_STAT_UNIT_EXCEP;
	if ((irb->scsw.cmd.dstat & mask) == mask) {
		dasd_generic_handle_state_change(device);
		return;
	}

	/* check for unsolicited interrupts */
	DBF_DEV_EVENT(DBF_WARNING, device, "%s",
		    "unsolicited interrupt received");
	device->discipline->dump_sense_dbf(device, irb, "unsolicited");
	dasd_schedule_device_bh(device);
	return;
};

static struct dasd_ccw_req *dasd_fba_build_cp(struct dasd_device * memdev,
					      struct dasd_block *block,
					      struct request *req)
{
	struct dasd_fba_private *private;
	unsigned long *idaws;
	struct LO_fba_data *LO_data;
	struct dasd_ccw_req *cqr;
	struct ccw1 *ccw;
	struct req_iterator iter;
	struct bio_vec *bv;
	char *dst;
	int count, cidaw, cplength, datasize;
	sector_t recid, first_rec, last_rec;
	unsigned int blksize, off;
	unsigned char cmd;

	private = (struct dasd_fba_private *) block->base->private;
	if (rq_data_dir(req) == READ) {
		cmd = DASD_FBA_CCW_READ;
	} else if (rq_data_dir(req) == WRITE) {
		cmd = DASD_FBA_CCW_WRITE;
	} else
		return ERR_PTR(-EINVAL);
	blksize = block->bp_block;
	/* Calculate record id of first and last block. */
	first_rec = blk_rq_pos(req) >> block->s2b_shift;
	last_rec =
		(blk_rq_pos(req) + blk_rq_sectors(req) - 1) >> block->s2b_shift;
	/* Check struct bio and count the number of blocks for the request. */
	count = 0;
	cidaw = 0;
	rq_for_each_segment(bv, req, iter) {
		if (bv->bv_len & (blksize - 1))
			/* Fba can only do full blocks. */
			return ERR_PTR(-EINVAL);
		count += bv->bv_len >> (block->s2b_shift + 9);
#if defined(CONFIG_64BIT)
		if (idal_is_needed (page_address(bv->bv_page), bv->bv_len))
			cidaw += bv->bv_len / blksize;
#endif
	}
	/* Paranoia. */
	if (count != last_rec - first_rec + 1)
		return ERR_PTR(-EINVAL);
	/* 1x define extent + 1x locate record + number of blocks */
	cplength = 2 + count;
	/* 1x define extent + 1x locate record */
	datasize = sizeof(struct DE_fba_data) + sizeof(struct LO_fba_data) +
		cidaw * sizeof(unsigned long);
	/*
	 * Find out number of additional locate record ccws if the device
	 * can't do data chaining.
	 */
	if (private->rdc_data.mode.bits.data_chain == 0) {
		cplength += count - 1;
		datasize += (count - 1)*sizeof(struct LO_fba_data);
	}
	/* Allocate the ccw request. */
	cqr = dasd_smalloc_request(DASD_FBA_MAGIC, cplength, datasize, memdev);
	if (IS_ERR(cqr))
		return cqr;
	ccw = cqr->cpaddr;
	/* First ccw is define extent. */
	define_extent(ccw++, cqr->data, rq_data_dir(req),
		      block->bp_block, blk_rq_pos(req), blk_rq_sectors(req));
	/* Build locate_record + read/write ccws. */
	idaws = (unsigned long *) (cqr->data + sizeof(struct DE_fba_data));
	LO_data = (struct LO_fba_data *) (idaws + cidaw);
	/* Locate record for all blocks for smart devices. */
	if (private->rdc_data.mode.bits.data_chain != 0) {
		ccw[-1].flags |= CCW_FLAG_CC;
		locate_record(ccw++, LO_data++, rq_data_dir(req), 0, count);
	}
	recid = first_rec;
	rq_for_each_segment(bv, req, iter) {
		dst = page_address(bv->bv_page) + bv->bv_offset;
		if (dasd_page_cache) {
			char *copy = kmem_cache_alloc(dasd_page_cache,
						      GFP_DMA | __GFP_NOWARN);
			if (copy && rq_data_dir(req) == WRITE)
				memcpy(copy + bv->bv_offset, dst, bv->bv_len);
			if (copy)
				dst = copy + bv->bv_offset;
		}
		for (off = 0; off < bv->bv_len; off += blksize) {
			/* Locate record for stupid devices. */
			if (private->rdc_data.mode.bits.data_chain == 0) {
				ccw[-1].flags |= CCW_FLAG_CC;
				locate_record(ccw, LO_data++,
					      rq_data_dir(req),
					      recid - first_rec, 1);
				ccw->flags = CCW_FLAG_CC;
				ccw++;
			} else {
				if (recid > first_rec)
					ccw[-1].flags |= CCW_FLAG_DC;
				else
					ccw[-1].flags |= CCW_FLAG_CC;
			}
			ccw->cmd_code = cmd;
			ccw->count = block->bp_block;
			if (idal_is_needed(dst, blksize)) {
				ccw->cda = (__u32)(addr_t) idaws;
				ccw->flags = CCW_FLAG_IDA;
				idaws = idal_create_words(idaws, dst, blksize);
			} else {
				ccw->cda = (__u32)(addr_t) dst;
				ccw->flags = 0;
			}
			ccw++;
			dst += blksize;
			recid++;
		}
	}
	if (blk_noretry_request(req) ||
	    block->base->features & DASD_FEATURE_FAILFAST)
		set_bit(DASD_CQR_FLAGS_FAILFAST, &cqr->flags);
	cqr->startdev = memdev;
	cqr->memdev = memdev;
	cqr->block = block;
	cqr->expires = memdev->default_expires * HZ;	/* default 5 minutes */
	cqr->retries = 32;
	cqr->buildclk = get_clock();
	cqr->status = DASD_CQR_FILLED;
	return cqr;
}

static int
dasd_fba_free_cp(struct dasd_ccw_req *cqr, struct request *req)
{
	struct dasd_fba_private *private;
	struct ccw1 *ccw;
	struct req_iterator iter;
	struct bio_vec *bv;
	char *dst, *cda;
	unsigned int blksize, off;
	int status;

	if (!dasd_page_cache)
		goto out;
	private = (struct dasd_fba_private *) cqr->block->base->private;
	blksize = cqr->block->bp_block;
	ccw = cqr->cpaddr;
	/* Skip over define extent & locate record. */
	ccw++;
	if (private->rdc_data.mode.bits.data_chain != 0)
		ccw++;
	rq_for_each_segment(bv, req, iter) {
		dst = page_address(bv->bv_page) + bv->bv_offset;
		for (off = 0; off < bv->bv_len; off += blksize) {
			/* Skip locate record. */
			if (private->rdc_data.mode.bits.data_chain == 0)
				ccw++;
			if (dst) {
				if (ccw->flags & CCW_FLAG_IDA)
					cda = *((char **)((addr_t) ccw->cda));
				else
					cda = (char *)((addr_t) ccw->cda);
				if (dst != cda) {
					if (rq_data_dir(req) == READ)
						memcpy(dst, cda, bv->bv_len);
					kmem_cache_free(dasd_page_cache,
					    (void *)((addr_t)cda & PAGE_MASK));
				}
				dst = NULL;
			}
			ccw++;
		}
	}
out:
	status = cqr->status == DASD_CQR_DONE;
	dasd_sfree_request(cqr, cqr->memdev);
	return status;
}

static void dasd_fba_handle_terminated_request(struct dasd_ccw_req *cqr)
{
	cqr->status = DASD_CQR_FILLED;
};

static int
dasd_fba_fill_info(struct dasd_device * device,
		   struct dasd_information2_t * info)
{
	info->label_block = 1;
	info->FBA_layout = 1;
	info->format = DASD_FORMAT_LDL;
	info->characteristics_size = sizeof(struct dasd_fba_characteristics);
	memcpy(info->characteristics,
	       &((struct dasd_fba_private *) device->private)->rdc_data,
	       sizeof (struct dasd_fba_characteristics));
	info->confdata_size = 0;
	return 0;
}

static void
dasd_fba_dump_sense_dbf(struct dasd_device *device, struct irb *irb,
			char *reason)
{
	u64 *sense;

	sense = (u64 *) dasd_get_sense(irb);
	if (sense) {
		DBF_DEV_EVENT(DBF_EMERG, device,
			      "%s: %s %02x%02x%02x %016llx %016llx %016llx "
			      "%016llx", reason,
			      scsw_is_tm(&irb->scsw) ? "t" : "c",
			      scsw_cc(&irb->scsw), scsw_cstat(&irb->scsw),
			      scsw_dstat(&irb->scsw), sense[0], sense[1],
			      sense[2], sense[3]);
	} else {
		DBF_DEV_EVENT(DBF_EMERG, device, "%s",
			      "SORRY - NO VALID SENSE AVAILABLE\n");
	}
}


static void
dasd_fba_dump_sense(struct dasd_device *device, struct dasd_ccw_req * req,
		    struct irb *irb)
{
	char *page;
	struct ccw1 *act, *end, *last;
	int len, sl, sct, count;

	page = (char *) get_zeroed_page(GFP_ATOMIC);
	if (page == NULL) {
		DBF_DEV_EVENT(DBF_WARNING, device, "%s",
			    "No memory to dump sense data");
		return;
	}
	len = sprintf(page, KERN_ERR PRINTK_HEADER
		      " I/O status report for device %s:\n",
		      dev_name(&device->cdev->dev));
	len += sprintf(page + len, KERN_ERR PRINTK_HEADER
		       " in req: %p CS: 0x%02X DS: 0x%02X\n", req,
		       irb->scsw.cmd.cstat, irb->scsw.cmd.dstat);
	len += sprintf(page + len, KERN_ERR PRINTK_HEADER
		       " device %s: Failing CCW: %p\n",
		       dev_name(&device->cdev->dev),
		       (void *) (addr_t) irb->scsw.cmd.cpa);
	if (irb->esw.esw0.erw.cons) {
		for (sl = 0; sl < 4; sl++) {
			len += sprintf(page + len, KERN_ERR PRINTK_HEADER
				       " Sense(hex) %2d-%2d:",
				       (8 * sl), ((8 * sl) + 7));

			for (sct = 0; sct < 8; sct++) {
				len += sprintf(page + len, " %02x",
					       irb->ecw[8 * sl + sct]);
			}
			len += sprintf(page + len, "\n");
		}
	} else {
	        len += sprintf(page + len, KERN_ERR PRINTK_HEADER
			       " SORRY - NO VALID SENSE AVAILABLE\n");
	}
	printk(KERN_ERR "%s", page);

	/* dump the Channel Program */
	/* print first CCWs (maximum 8) */
	act = req->cpaddr;
        for (last = act; last->flags & (CCW_FLAG_CC | CCW_FLAG_DC); last++);
	end = min(act + 8, last);
	len = sprintf(page, KERN_ERR PRINTK_HEADER
		      " Related CP in req: %p\n", req);
	while (act <= end) {
		len += sprintf(page + len, KERN_ERR PRINTK_HEADER
			       " CCW %p: %08X %08X DAT:",
			       act, ((int *) act)[0], ((int *) act)[1]);
		for (count = 0; count < 32 && count < act->count;
		     count += sizeof(int))
			len += sprintf(page + len, " %08X",
				       ((int *) (addr_t) act->cda)
				       [(count>>2)]);
		len += sprintf(page + len, "\n");
		act++;
	}
	printk(KERN_ERR "%s", page);


	/* print failing CCW area */
	len = 0;
	if (act <  ((struct ccw1 *)(addr_t) irb->scsw.cmd.cpa) - 2) {
		act = ((struct ccw1 *)(addr_t) irb->scsw.cmd.cpa) - 2;
		len += sprintf(page + len, KERN_ERR PRINTK_HEADER "......\n");
	}
	end = min((struct ccw1 *)(addr_t) irb->scsw.cmd.cpa + 2, last);
	while (act <= end) {
		len += sprintf(page + len, KERN_ERR PRINTK_HEADER
			       " CCW %p: %08X %08X DAT:",
			       act, ((int *) act)[0], ((int *) act)[1]);
		for (count = 0; count < 32 && count < act->count;
		     count += sizeof(int))
			len += sprintf(page + len, " %08X",
				       ((int *) (addr_t) act->cda)
				       [(count>>2)]);
		len += sprintf(page + len, "\n");
		act++;
	}

	/* print last CCWs */
	if (act <  last - 2) {
		act = last - 2;
		len += sprintf(page + len, KERN_ERR PRINTK_HEADER "......\n");
	}
	while (act <= last) {
		len += sprintf(page + len, KERN_ERR PRINTK_HEADER
			       " CCW %p: %08X %08X DAT:",
			       act, ((int *) act)[0], ((int *) act)[1]);
		for (count = 0; count < 32 && count < act->count;
		     count += sizeof(int))
			len += sprintf(page + len, " %08X",
				       ((int *) (addr_t) act->cda)
				       [(count>>2)]);
		len += sprintf(page + len, "\n");
		act++;
	}
	if (len > 0)
		printk(KERN_ERR "%s", page);
	free_page((unsigned long) page);
}

/*
 * max_blocks is dependent on the amount of storage that is available
 * in the static io buffer for each device. Currently each device has
 * 8192 bytes (=2 pages). For 64 bit one dasd_mchunkt_t structure has
 * 24 bytes, the struct dasd_ccw_req has 136 bytes and each block can use
 * up to 16 bytes (8 for the ccw and 8 for the idal pointer). In
 * addition we have one define extent ccw + 16 bytes of data and a
 * locate record ccw for each block (stupid devices!) + 16 bytes of data.
 * That makes:
 * (8192 - 24 - 136 - 8 - 16) / 40 = 200.2 blocks at maximum.
 * We want to fit two into the available memory so that we can immediately
 * start the next request if one finishes off. That makes 100.1 blocks
 * for one request. Give a little safety and the result is 96.
 */
static struct dasd_discipline dasd_fba_discipline = {
	.owner = THIS_MODULE,
	.name = "FBA ",
	.ebcname = "FBA ",
	.max_blocks = 96,
	.check_device = dasd_fba_check_characteristics,
	.do_analysis = dasd_fba_do_analysis,
	.verify_path = dasd_generic_verify_path,
	.fill_geometry = dasd_fba_fill_geometry,
	.start_IO = dasd_start_IO,
	.term_IO = dasd_term_IO,
	.handle_terminated_request = dasd_fba_handle_terminated_request,
	.erp_action = dasd_fba_erp_action,
	.erp_postaction = dasd_fba_erp_postaction,
	.handle_unsolicited_interrupt = dasd_fba_handle_unsolicited_interrupt,
	.build_cp = dasd_fba_build_cp,
	.free_cp = dasd_fba_free_cp,
	.dump_sense = dasd_fba_dump_sense,
	.dump_sense_dbf = dasd_fba_dump_sense_dbf,
	.fill_info = dasd_fba_fill_info,
};

static int __init
dasd_fba_init(void)
{
	int ret;

	ASCEBC(dasd_fba_discipline.ebcname, 4);
	ret = ccw_driver_register(&dasd_fba_driver);
	if (!ret)
		wait_for_device_probe();

	return ret;
}

static void __exit
dasd_fba_cleanup(void)
{
	ccw_driver_unregister(&dasd_fba_driver);
}

module_init(dasd_fba_init);
module_exit(dasd_fba_cleanup);
