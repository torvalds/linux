// SPDX-License-Identifier: GPL-2.0
/*
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

#define FBA_DEFAULT_RETRIES 32

#define DASD_FBA_CCW_WRITE 0x41
#define DASD_FBA_CCW_READ 0x42
#define DASD_FBA_CCW_LOCATE 0x43
#define DASD_FBA_CCW_DEFINE_EXTENT 0x63

MODULE_LICENSE("GPL");

static struct dasd_discipline dasd_fba_discipline;
static void *dasd_fba_zero_page;

struct dasd_fba_private {
	struct dasd_fba_characteristics rdc_data;
};

static struct ccw_device_id dasd_fba_ids[] = {
	{ CCW_DEVICE_DEVTYPE (0x6310, 0, 0x9336, 0), .driver_info = 0x1},
	{ CCW_DEVICE_DEVTYPE (0x3880, 0, 0x3370, 0), .driver_info = 0x2},
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ccw, dasd_fba_ids);

static int
dasd_fba_set_online(struct ccw_device *cdev)
{
	return dasd_generic_set_online(cdev, &dasd_fba_discipline);
}

static struct ccw_driver dasd_fba_driver = {
	.driver = {
		.name	= "dasd-fba",
		.owner	= THIS_MODULE,
		.dev_groups = dasd_dev_groups,
	},
	.ids         = dasd_fba_ids,
	.probe       = dasd_generic_probe,
	.remove      = dasd_generic_remove,
	.set_offline = dasd_generic_set_offline,
	.set_online  = dasd_fba_set_online,
	.notify      = dasd_generic_notify,
	.path_event  = dasd_generic_path_event,
	.int_class   = IRQIO_DAS,
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
	struct dasd_fba_private *private = device->private;
	struct ccw_device *cdev = device->cdev;
	struct dasd_block *block;
	int readonly, rc;

	if (!private) {
		private = kzalloc(sizeof(*private), GFP_KERNEL | GFP_DMA);
		if (!private) {
			dev_warn(&device->cdev->dev,
				 "Allocating memory for private DASD "
				 "data failed\n");
			return -ENOMEM;
		}
		device->private = private;
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
	device->default_retries = FBA_DEFAULT_RETRIES;
	dasd_path_set_opm(device, LPM_ANYPATH);

	readonly = dasd_device_is_ro(device);
	if (readonly)
		set_bit(DASD_FLAG_DEVICE_RO, &device->flags);

	/* FBA supports discard, set the according feature bit */
	dasd_set_feature(cdev, DASD_FEATURE_DISCARD, 1);

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
	struct dasd_fba_private *private = block->base->private;
	int sb, rc;

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

static void dasd_fba_check_for_device_change(struct dasd_device *device,
					     struct dasd_ccw_req *cqr,
					     struct irb *irb)
{
	char mask;

	/* first of all check for state change pending interrupt */
	mask = DEV_STAT_ATTENTION | DEV_STAT_DEV_END | DEV_STAT_UNIT_EXCEP;
	if ((irb->scsw.cmd.dstat & mask) == mask)
		dasd_generic_handle_state_change(device);
};


/*
 * Builds a CCW with no data payload
 */
static void ccw_write_no_data(struct ccw1 *ccw)
{
	ccw->cmd_code = DASD_FBA_CCW_WRITE;
	ccw->flags |= CCW_FLAG_SLI;
	ccw->count = 0;
}

/*
 * Builds a CCW that writes only zeroes.
 */
static void ccw_write_zero(struct ccw1 *ccw, int count)
{
	ccw->cmd_code = DASD_FBA_CCW_WRITE;
	ccw->flags |= CCW_FLAG_SLI;
	ccw->count = count;
	ccw->cda = (__u32) (addr_t) dasd_fba_zero_page;
}

/*
 * Helper function to count the amount of necessary CCWs within a given range
 * with 4k alignment and command chaining in mind.
 */
static int count_ccws(sector_t first_rec, sector_t last_rec,
		      unsigned int blocks_per_page)
{
	sector_t wz_stop = 0, d_stop = 0;
	int cur_pos = 0;
	int count = 0;

	if (first_rec % blocks_per_page != 0) {
		wz_stop = first_rec + blocks_per_page -
			(first_rec % blocks_per_page) - 1;
		if (wz_stop > last_rec)
			wz_stop = last_rec;
		cur_pos = wz_stop - first_rec + 1;
		count++;
	}

	if (last_rec - (first_rec + cur_pos) + 1 >= blocks_per_page) {
		if ((last_rec - blocks_per_page + 1) % blocks_per_page != 0)
			d_stop = last_rec - ((last_rec - blocks_per_page + 1) %
					     blocks_per_page);
		else
			d_stop = last_rec;

		cur_pos += d_stop - (first_rec + cur_pos) + 1;
		count++;
	}

	if (cur_pos == 0 || first_rec + cur_pos - 1 < last_rec)
		count++;

	return count;
}

/*
 * This function builds a CCW request for block layer discard requests.
 * Each page in the z/VM hypervisor that represents certain records of an FBA
 * device will be padded with zeros. This is a special behaviour of the WRITE
 * command which is triggered when no data payload is added to the CCW.
 *
 * Note: Due to issues in some z/VM versions, we can't fully utilise this
 * special behaviour. We have to keep a 4k (or 8 block) alignment in mind to
 * work around those issues and write actual zeroes to the unaligned parts in
 * the request. This workaround might be removed in the future.
 */
static struct dasd_ccw_req *dasd_fba_build_cp_discard(
						struct dasd_device *memdev,
						struct dasd_block *block,
						struct request *req)
{
	struct LO_fba_data *LO_data;
	struct dasd_ccw_req *cqr;
	struct ccw1 *ccw;

	sector_t wz_stop = 0, d_stop = 0;
	sector_t first_rec, last_rec;

	unsigned int blksize = block->bp_block;
	unsigned int blocks_per_page;
	int wz_count = 0;
	int d_count = 0;
	int cur_pos = 0; /* Current position within the extent */
	int count = 0;
	int cplength;
	int datasize;
	int nr_ccws;

	first_rec = blk_rq_pos(req) >> block->s2b_shift;
	last_rec =
		(blk_rq_pos(req) + blk_rq_sectors(req) - 1) >> block->s2b_shift;
	count = last_rec - first_rec + 1;

	blocks_per_page = BLOCKS_PER_PAGE(blksize);
	nr_ccws = count_ccws(first_rec, last_rec, blocks_per_page);

	/* define extent + nr_ccws * locate record + nr_ccws * single CCW */
	cplength = 1 + 2 * nr_ccws;
	datasize = sizeof(struct DE_fba_data) +
		nr_ccws * (sizeof(struct LO_fba_data) + sizeof(struct ccw1));

	cqr = dasd_smalloc_request(DASD_FBA_MAGIC, cplength, datasize, memdev,
				   blk_mq_rq_to_pdu(req));
	if (IS_ERR(cqr))
		return cqr;

	ccw = cqr->cpaddr;

	define_extent(ccw++, cqr->data, WRITE, blksize, first_rec, count);
	LO_data = cqr->data + sizeof(struct DE_fba_data);

	/* First part is not aligned. Calculate range to write zeroes. */
	if (first_rec % blocks_per_page != 0) {
		wz_stop = first_rec + blocks_per_page -
			(first_rec % blocks_per_page) - 1;
		if (wz_stop > last_rec)
			wz_stop = last_rec;
		wz_count = wz_stop - first_rec + 1;

		ccw[-1].flags |= CCW_FLAG_CC;
		locate_record(ccw++, LO_data++, WRITE, cur_pos, wz_count);

		ccw[-1].flags |= CCW_FLAG_CC;
		ccw_write_zero(ccw++, wz_count * blksize);

		cur_pos = wz_count;
	}

	/* We can do proper discard when we've got at least blocks_per_page blocks. */
	if (last_rec - (first_rec + cur_pos) + 1 >= blocks_per_page) {
		/* is last record at page boundary? */
		if ((last_rec - blocks_per_page + 1) % blocks_per_page != 0)
			d_stop = last_rec - ((last_rec - blocks_per_page + 1) %
					     blocks_per_page);
		else
			d_stop = last_rec;

		d_count = d_stop - (first_rec + cur_pos) + 1;

		ccw[-1].flags |= CCW_FLAG_CC;
		locate_record(ccw++, LO_data++, WRITE, cur_pos, d_count);

		ccw[-1].flags |= CCW_FLAG_CC;
		ccw_write_no_data(ccw++);

		cur_pos += d_count;
	}

	/* We might still have some bits left which need to be zeroed. */
	if (cur_pos == 0 || first_rec + cur_pos - 1 < last_rec) {
		if (d_stop != 0)
			wz_count = last_rec - d_stop;
		else if (wz_stop != 0)
			wz_count = last_rec - wz_stop;
		else
			wz_count = count;

		ccw[-1].flags |= CCW_FLAG_CC;
		locate_record(ccw++, LO_data++, WRITE, cur_pos, wz_count);

		ccw[-1].flags |= CCW_FLAG_CC;
		ccw_write_zero(ccw++, wz_count * blksize);
	}

	if (blk_noretry_request(req) ||
	    block->base->features & DASD_FEATURE_FAILFAST)
		set_bit(DASD_CQR_FLAGS_FAILFAST, &cqr->flags);

	cqr->startdev = memdev;
	cqr->memdev = memdev;
	cqr->block = block;
	cqr->expires = memdev->default_expires * HZ;	/* default 5 minutes */
	cqr->retries = memdev->default_retries;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;

	return cqr;
}

static struct dasd_ccw_req *dasd_fba_build_cp_regular(
						struct dasd_device *memdev,
						struct dasd_block *block,
						struct request *req)
{
	struct dasd_fba_private *private = block->base->private;
	unsigned long *idaws;
	struct LO_fba_data *LO_data;
	struct dasd_ccw_req *cqr;
	struct ccw1 *ccw;
	struct req_iterator iter;
	struct bio_vec bv;
	char *dst;
	int count, cidaw, cplength, datasize;
	sector_t recid, first_rec, last_rec;
	unsigned int blksize, off;
	unsigned char cmd;

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
		if (bv.bv_len & (blksize - 1))
			/* Fba can only do full blocks. */
			return ERR_PTR(-EINVAL);
		count += bv.bv_len >> (block->s2b_shift + 9);
		if (idal_is_needed (page_address(bv.bv_page), bv.bv_len))
			cidaw += bv.bv_len / blksize;
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
	cqr = dasd_smalloc_request(DASD_FBA_MAGIC, cplength, datasize, memdev,
				   blk_mq_rq_to_pdu(req));
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
		dst = page_address(bv.bv_page) + bv.bv_offset;
		if (dasd_page_cache) {
			char *copy = kmem_cache_alloc(dasd_page_cache,
						      GFP_DMA | __GFP_NOWARN);
			if (copy && rq_data_dir(req) == WRITE)
				memcpy(copy + bv.bv_offset, dst, bv.bv_len);
			if (copy)
				dst = copy + bv.bv_offset;
		}
		for (off = 0; off < bv.bv_len; off += blksize) {
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
	cqr->retries = memdev->default_retries;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;
	return cqr;
}

static struct dasd_ccw_req *dasd_fba_build_cp(struct dasd_device *memdev,
					      struct dasd_block *block,
					      struct request *req)
{
	if (req_op(req) == REQ_OP_DISCARD || req_op(req) == REQ_OP_WRITE_ZEROES)
		return dasd_fba_build_cp_discard(memdev, block, req);
	else
		return dasd_fba_build_cp_regular(memdev, block, req);
}

static int
dasd_fba_free_cp(struct dasd_ccw_req *cqr, struct request *req)
{
	struct dasd_fba_private *private = cqr->block->base->private;
	struct ccw1 *ccw;
	struct req_iterator iter;
	struct bio_vec bv;
	char *dst, *cda;
	unsigned int blksize, off;
	int status;

	if (!dasd_page_cache)
		goto out;
	blksize = cqr->block->bp_block;
	ccw = cqr->cpaddr;
	/* Skip over define extent & locate record. */
	ccw++;
	if (private->rdc_data.mode.bits.data_chain != 0)
		ccw++;
	rq_for_each_segment(bv, req, iter) {
		dst = page_address(bv.bv_page) + bv.bv_offset;
		for (off = 0; off < bv.bv_len; off += blksize) {
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
						memcpy(dst, cda, bv.bv_len);
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
	if (cqr->retries < 0)
		cqr->status = DASD_CQR_FAILED;
	else
		cqr->status = DASD_CQR_FILLED;
};

static int
dasd_fba_fill_info(struct dasd_device * device,
		   struct dasd_information2_t * info)
{
	struct dasd_fba_private *private = device->private;

	info->label_block = 1;
	info->FBA_layout = 1;
	info->format = DASD_FORMAT_LDL;
	info->characteristics_size = sizeof(private->rdc_data);
	memcpy(info->characteristics, &private->rdc_data,
	       sizeof(private->rdc_data));
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
	len = sprintf(page, PRINTK_HEADER
		      " I/O status report for device %s:\n",
		      dev_name(&device->cdev->dev));
	len += sprintf(page + len, PRINTK_HEADER
		       " in req: %p CS: 0x%02X DS: 0x%02X\n", req,
		       irb->scsw.cmd.cstat, irb->scsw.cmd.dstat);
	len += sprintf(page + len, PRINTK_HEADER
		       " device %s: Failing CCW: %p\n",
		       dev_name(&device->cdev->dev),
		       (void *) (addr_t) irb->scsw.cmd.cpa);
	if (irb->esw.esw0.erw.cons) {
		for (sl = 0; sl < 4; sl++) {
			len += sprintf(page + len, PRINTK_HEADER
				       " Sense(hex) %2d-%2d:",
				       (8 * sl), ((8 * sl) + 7));

			for (sct = 0; sct < 8; sct++) {
				len += sprintf(page + len, " %02x",
					       irb->ecw[8 * sl + sct]);
			}
			len += sprintf(page + len, "\n");
		}
	} else {
		len += sprintf(page + len, PRINTK_HEADER
			       " SORRY - NO VALID SENSE AVAILABLE\n");
	}
	printk(KERN_ERR "%s", page);

	/* dump the Channel Program */
	/* print first CCWs (maximum 8) */
	act = req->cpaddr;
        for (last = act; last->flags & (CCW_FLAG_CC | CCW_FLAG_DC); last++);
	end = min(act + 8, last);
	len = sprintf(page, PRINTK_HEADER " Related CP in req: %p\n", req);
	while (act <= end) {
		len += sprintf(page + len, PRINTK_HEADER
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
		len += sprintf(page + len, PRINTK_HEADER "......\n");
	}
	end = min((struct ccw1 *)(addr_t) irb->scsw.cmd.cpa + 2, last);
	while (act <= end) {
		len += sprintf(page + len, PRINTK_HEADER
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
		len += sprintf(page + len, PRINTK_HEADER "......\n");
	}
	while (act <= last) {
		len += sprintf(page + len, PRINTK_HEADER
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
 * Initialize block layer request queue.
 */
static void dasd_fba_setup_blk_queue(struct dasd_block *block)
{
	unsigned int logical_block_size = block->bp_block;
	struct request_queue *q = block->request_queue;
	unsigned int max_bytes, max_discard_sectors;
	int max;

	max = DASD_FBA_MAX_BLOCKS << block->s2b_shift;
	blk_queue_flag_set(QUEUE_FLAG_NONROT, q);
	q->limits.max_dev_sectors = max;
	blk_queue_logical_block_size(q, logical_block_size);
	blk_queue_max_hw_sectors(q, max);
	blk_queue_max_segments(q, USHRT_MAX);
	/* With page sized segments each segment can be translated into one idaw/tidaw */
	blk_queue_max_segment_size(q, PAGE_SIZE);
	blk_queue_segment_boundary(q, PAGE_SIZE - 1);

	q->limits.discard_granularity = logical_block_size;
	q->limits.discard_alignment = PAGE_SIZE;

	/* Calculate max_discard_sectors and make it PAGE aligned */
	max_bytes = USHRT_MAX * logical_block_size;
	max_bytes = ALIGN_DOWN(max_bytes, PAGE_SIZE);
	max_discard_sectors = max_bytes / logical_block_size;

	blk_queue_max_discard_sectors(q, max_discard_sectors);
	blk_queue_max_write_zeroes_sectors(q, max_discard_sectors);
	blk_queue_flag_set(QUEUE_FLAG_DISCARD, q);
}

static struct dasd_discipline dasd_fba_discipline = {
	.owner = THIS_MODULE,
	.name = "FBA ",
	.ebcname = "FBA ",
	.check_device = dasd_fba_check_characteristics,
	.do_analysis = dasd_fba_do_analysis,
	.verify_path = dasd_generic_verify_path,
	.setup_blk_queue = dasd_fba_setup_blk_queue,
	.fill_geometry = dasd_fba_fill_geometry,
	.start_IO = dasd_start_IO,
	.term_IO = dasd_term_IO,
	.handle_terminated_request = dasd_fba_handle_terminated_request,
	.erp_action = dasd_fba_erp_action,
	.erp_postaction = dasd_fba_erp_postaction,
	.check_for_device_change = dasd_fba_check_for_device_change,
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

	dasd_fba_zero_page = (void *)get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!dasd_fba_zero_page)
		return -ENOMEM;

	ret = ccw_driver_register(&dasd_fba_driver);
	if (!ret)
		wait_for_device_probe();

	return ret;
}

static void __exit
dasd_fba_cleanup(void)
{
	ccw_driver_unregister(&dasd_fba_driver);
	free_page((unsigned long)dasd_fba_zero_page);
}

module_init(dasd_fba_init);
module_exit(dasd_fba_cleanup);
