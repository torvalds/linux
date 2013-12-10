/*
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * Copyright IBM Corp. 1999, 2009
 * EMC Symmetrix ioctl Copyright EMC Corporation, 2008
 * Author.........: Nigel Hislop <hislop_nigel@emc.com>
 */

#define KMSG_COMPONENT "dasd-eckd"

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/hdreg.h>	/* HDIO_GETGEO			    */
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/compat.h>
#include <linux/init.h>

#include <asm/css_chars.h>
#include <asm/debug.h>
#include <asm/idals.h>
#include <asm/ebcdic.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/cio.h>
#include <asm/ccwdev.h>
#include <asm/itcw.h>

#include "dasd_int.h"
#include "dasd_eckd.h"

#ifdef PRINTK_HEADER
#undef PRINTK_HEADER
#endif				/* PRINTK_HEADER */
#define PRINTK_HEADER "dasd(eckd):"

#define ECKD_C0(i) (i->home_bytes)
#define ECKD_F(i) (i->formula)
#define ECKD_F1(i) (ECKD_F(i)==0x01?(i->factors.f_0x01.f1):\
		    (i->factors.f_0x02.f1))
#define ECKD_F2(i) (ECKD_F(i)==0x01?(i->factors.f_0x01.f2):\
		    (i->factors.f_0x02.f2))
#define ECKD_F3(i) (ECKD_F(i)==0x01?(i->factors.f_0x01.f3):\
		    (i->factors.f_0x02.f3))
#define ECKD_F4(i) (ECKD_F(i)==0x02?(i->factors.f_0x02.f4):0)
#define ECKD_F5(i) (ECKD_F(i)==0x02?(i->factors.f_0x02.f5):0)
#define ECKD_F6(i) (i->factor6)
#define ECKD_F7(i) (i->factor7)
#define ECKD_F8(i) (i->factor8)

/*
 * raw track access always map to 64k in memory
 * so it maps to 16 blocks of 4k per track
 */
#define DASD_RAW_BLOCK_PER_TRACK 16
#define DASD_RAW_BLOCKSIZE 4096
/* 64k are 128 x 512 byte sectors  */
#define DASD_RAW_SECTORS_PER_TRACK 128

MODULE_LICENSE("GPL");

static struct dasd_discipline dasd_eckd_discipline;

/* The ccw bus type uses this table to find devices that it sends to
 * dasd_eckd_probe */
static struct ccw_device_id dasd_eckd_ids[] = {
	{ CCW_DEVICE_DEVTYPE (0x3990, 0, 0x3390, 0), .driver_info = 0x1},
	{ CCW_DEVICE_DEVTYPE (0x2105, 0, 0x3390, 0), .driver_info = 0x2},
	{ CCW_DEVICE_DEVTYPE (0x3880, 0, 0x3380, 0), .driver_info = 0x3},
	{ CCW_DEVICE_DEVTYPE (0x3990, 0, 0x3380, 0), .driver_info = 0x4},
	{ CCW_DEVICE_DEVTYPE (0x2105, 0, 0x3380, 0), .driver_info = 0x5},
	{ CCW_DEVICE_DEVTYPE (0x9343, 0, 0x9345, 0), .driver_info = 0x6},
	{ CCW_DEVICE_DEVTYPE (0x2107, 0, 0x3390, 0), .driver_info = 0x7},
	{ CCW_DEVICE_DEVTYPE (0x2107, 0, 0x3380, 0), .driver_info = 0x8},
	{ CCW_DEVICE_DEVTYPE (0x1750, 0, 0x3390, 0), .driver_info = 0x9},
	{ CCW_DEVICE_DEVTYPE (0x1750, 0, 0x3380, 0), .driver_info = 0xa},
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ccw, dasd_eckd_ids);

static struct ccw_driver dasd_eckd_driver; /* see below */

static void *rawpadpage;

#define INIT_CQR_OK 0
#define INIT_CQR_UNFORMATTED 1
#define INIT_CQR_ERROR 2

/* emergency request for reserve/release */
static struct {
	struct dasd_ccw_req cqr;
	struct ccw1 ccw;
	char data[32];
} *dasd_reserve_req;
static DEFINE_MUTEX(dasd_reserve_mutex);

/* definitions for the path verification worker */
struct path_verification_work_data {
	struct work_struct worker;
	struct dasd_device *device;
	struct dasd_ccw_req cqr;
	struct ccw1 ccw;
	__u8 rcd_buffer[DASD_ECKD_RCD_DATA_SIZE];
	int isglobal;
	__u8 tbvpm;
};
static struct path_verification_work_data *path_verification_worker;
static DEFINE_MUTEX(dasd_path_verification_mutex);

/* initial attempt at a probe function. this can be simplified once
 * the other detection code is gone */
static int
dasd_eckd_probe (struct ccw_device *cdev)
{
	int ret;

	/* set ECKD specific ccw-device options */
	ret = ccw_device_set_options(cdev, CCWDEV_ALLOW_FORCE |
				     CCWDEV_DO_PATHGROUP | CCWDEV_DO_MULTIPATH);
	if (ret) {
		DBF_EVENT_DEVID(DBF_WARNING, cdev, "%s",
				"dasd_eckd_probe: could not set "
				"ccw-device options");
		return ret;
	}
	ret = dasd_generic_probe(cdev, &dasd_eckd_discipline);
	return ret;
}

static int
dasd_eckd_set_online(struct ccw_device *cdev)
{
	return dasd_generic_set_online(cdev, &dasd_eckd_discipline);
}

static const int sizes_trk0[] = { 28, 148, 84 };
#define LABEL_SIZE 140

/* head and record addresses of count_area read in analysis ccw */
static const int count_area_head[] = { 0, 0, 0, 0, 2 };
static const int count_area_rec[] = { 1, 2, 3, 4, 1 };

static inline unsigned int
round_up_multiple(unsigned int no, unsigned int mult)
{
	int rem = no % mult;
	return (rem ? no - rem + mult : no);
}

static inline unsigned int
ceil_quot(unsigned int d1, unsigned int d2)
{
	return (d1 + (d2 - 1)) / d2;
}

static unsigned int
recs_per_track(struct dasd_eckd_characteristics * rdc,
	       unsigned int kl, unsigned int dl)
{
	int dn, kn;

	switch (rdc->dev_type) {
	case 0x3380:
		if (kl)
			return 1499 / (15 + 7 + ceil_quot(kl + 12, 32) +
				       ceil_quot(dl + 12, 32));
		else
			return 1499 / (15 + ceil_quot(dl + 12, 32));
	case 0x3390:
		dn = ceil_quot(dl + 6, 232) + 1;
		if (kl) {
			kn = ceil_quot(kl + 6, 232) + 1;
			return 1729 / (10 + 9 + ceil_quot(kl + 6 * kn, 34) +
				       9 + ceil_quot(dl + 6 * dn, 34));
		} else
			return 1729 / (10 + 9 + ceil_quot(dl + 6 * dn, 34));
	case 0x9345:
		dn = ceil_quot(dl + 6, 232) + 1;
		if (kl) {
			kn = ceil_quot(kl + 6, 232) + 1;
			return 1420 / (18 + 7 + ceil_quot(kl + 6 * kn, 34) +
				       ceil_quot(dl + 6 * dn, 34));
		} else
			return 1420 / (18 + 7 + ceil_quot(dl + 6 * dn, 34));
	}
	return 0;
}

static void set_ch_t(struct ch_t *geo, __u32 cyl, __u8 head)
{
	geo->cyl = (__u16) cyl;
	geo->head = cyl >> 16;
	geo->head <<= 4;
	geo->head |= head;
}

static int
check_XRC (struct ccw1         *de_ccw,
           struct DE_eckd_data *data,
           struct dasd_device  *device)
{
        struct dasd_eckd_private *private;
	int rc;

        private = (struct dasd_eckd_private *) device->private;
	if (!private->rdc_data.facilities.XRC_supported)
		return 0;

        /* switch on System Time Stamp - needed for XRC Support */
	data->ga_extended |= 0x08; /* switch on 'Time Stamp Valid'   */
	data->ga_extended |= 0x02; /* switch on 'Extended Parameter' */

	rc = get_sync_clock(&data->ep_sys_time);
	/* Ignore return code if sync clock is switched off. */
	if (rc == -EOPNOTSUPP || rc == -EACCES)
		rc = 0;

	de_ccw->count = sizeof(struct DE_eckd_data);
	de_ccw->flags |= CCW_FLAG_SLI;
	return rc;
}

static int
define_extent(struct ccw1 *ccw, struct DE_eckd_data *data, unsigned int trk,
	      unsigned int totrk, int cmd, struct dasd_device *device)
{
	struct dasd_eckd_private *private;
	u32 begcyl, endcyl;
	u16 heads, beghead, endhead;
	int rc = 0;

	private = (struct dasd_eckd_private *) device->private;

	ccw->cmd_code = DASD_ECKD_CCW_DEFINE_EXTENT;
	ccw->flags = 0;
	ccw->count = 16;
	ccw->cda = (__u32) __pa(data);

	memset(data, 0, sizeof(struct DE_eckd_data));
	switch (cmd) {
	case DASD_ECKD_CCW_READ_HOME_ADDRESS:
	case DASD_ECKD_CCW_READ_RECORD_ZERO:
	case DASD_ECKD_CCW_READ:
	case DASD_ECKD_CCW_READ_MT:
	case DASD_ECKD_CCW_READ_CKD:
	case DASD_ECKD_CCW_READ_CKD_MT:
	case DASD_ECKD_CCW_READ_KD:
	case DASD_ECKD_CCW_READ_KD_MT:
	case DASD_ECKD_CCW_READ_COUNT:
		data->mask.perm = 0x1;
		data->attributes.operation = private->attrib.operation;
		break;
	case DASD_ECKD_CCW_WRITE:
	case DASD_ECKD_CCW_WRITE_MT:
	case DASD_ECKD_CCW_WRITE_KD:
	case DASD_ECKD_CCW_WRITE_KD_MT:
		data->mask.perm = 0x02;
		data->attributes.operation = private->attrib.operation;
		rc = check_XRC (ccw, data, device);
		break;
	case DASD_ECKD_CCW_WRITE_CKD:
	case DASD_ECKD_CCW_WRITE_CKD_MT:
		data->attributes.operation = DASD_BYPASS_CACHE;
		rc = check_XRC (ccw, data, device);
		break;
	case DASD_ECKD_CCW_ERASE:
	case DASD_ECKD_CCW_WRITE_HOME_ADDRESS:
	case DASD_ECKD_CCW_WRITE_RECORD_ZERO:
		data->mask.perm = 0x3;
		data->mask.auth = 0x1;
		data->attributes.operation = DASD_BYPASS_CACHE;
		rc = check_XRC (ccw, data, device);
		break;
	default:
		dev_err(&device->cdev->dev,
			"0x%x is not a known command\n", cmd);
		break;
	}

	data->attributes.mode = 0x3;	/* ECKD */

	if ((private->rdc_data.cu_type == 0x2105 ||
	     private->rdc_data.cu_type == 0x2107 ||
	     private->rdc_data.cu_type == 0x1750)
	    && !(private->uses_cdl && trk < 2))
		data->ga_extended |= 0x40; /* Regular Data Format Mode */

	heads = private->rdc_data.trk_per_cyl;
	begcyl = trk / heads;
	beghead = trk % heads;
	endcyl = totrk / heads;
	endhead = totrk % heads;

	/* check for sequential prestage - enhance cylinder range */
	if (data->attributes.operation == DASD_SEQ_PRESTAGE ||
	    data->attributes.operation == DASD_SEQ_ACCESS) {

		if (endcyl + private->attrib.nr_cyl < private->real_cyl)
			endcyl += private->attrib.nr_cyl;
		else
			endcyl = (private->real_cyl - 1);
	}

	set_ch_t(&data->beg_ext, begcyl, beghead);
	set_ch_t(&data->end_ext, endcyl, endhead);
	return rc;
}

static int check_XRC_on_prefix(struct PFX_eckd_data *pfxdata,
			       struct dasd_device  *device)
{
	struct dasd_eckd_private *private;
	int rc;

	private = (struct dasd_eckd_private *) device->private;
	if (!private->rdc_data.facilities.XRC_supported)
		return 0;

	/* switch on System Time Stamp - needed for XRC Support */
	pfxdata->define_extent.ga_extended |= 0x08; /* 'Time Stamp Valid'   */
	pfxdata->define_extent.ga_extended |= 0x02; /* 'Extended Parameter' */
	pfxdata->validity.time_stamp = 1;	    /* 'Time Stamp Valid'   */

	rc = get_sync_clock(&pfxdata->define_extent.ep_sys_time);
	/* Ignore return code if sync clock is switched off. */
	if (rc == -EOPNOTSUPP || rc == -EACCES)
		rc = 0;
	return rc;
}

static void fill_LRE_data(struct LRE_eckd_data *data, unsigned int trk,
			  unsigned int rec_on_trk, int count, int cmd,
			  struct dasd_device *device, unsigned int reclen,
			  unsigned int tlf)
{
	struct dasd_eckd_private *private;
	int sector;
	int dn, d;

	private = (struct dasd_eckd_private *) device->private;

	memset(data, 0, sizeof(*data));
	sector = 0;
	if (rec_on_trk) {
		switch (private->rdc_data.dev_type) {
		case 0x3390:
			dn = ceil_quot(reclen + 6, 232);
			d = 9 + ceil_quot(reclen + 6 * (dn + 1), 34);
			sector = (49 + (rec_on_trk - 1) * (10 + d)) / 8;
			break;
		case 0x3380:
			d = 7 + ceil_quot(reclen + 12, 32);
			sector = (39 + (rec_on_trk - 1) * (8 + d)) / 7;
			break;
		}
	}
	data->sector = sector;
	/* note: meaning of count depends on the operation
	 *	 for record based I/O it's the number of records, but for
	 *	 track based I/O it's the number of tracks
	 */
	data->count = count;
	switch (cmd) {
	case DASD_ECKD_CCW_WRITE_HOME_ADDRESS:
		data->operation.orientation = 0x3;
		data->operation.operation = 0x03;
		break;
	case DASD_ECKD_CCW_READ_HOME_ADDRESS:
		data->operation.orientation = 0x3;
		data->operation.operation = 0x16;
		break;
	case DASD_ECKD_CCW_WRITE_RECORD_ZERO:
		data->operation.orientation = 0x1;
		data->operation.operation = 0x03;
		data->count++;
		break;
	case DASD_ECKD_CCW_READ_RECORD_ZERO:
		data->operation.orientation = 0x3;
		data->operation.operation = 0x16;
		data->count++;
		break;
	case DASD_ECKD_CCW_WRITE:
	case DASD_ECKD_CCW_WRITE_MT:
	case DASD_ECKD_CCW_WRITE_KD:
	case DASD_ECKD_CCW_WRITE_KD_MT:
		data->auxiliary.length_valid = 0x1;
		data->length = reclen;
		data->operation.operation = 0x01;
		break;
	case DASD_ECKD_CCW_WRITE_CKD:
	case DASD_ECKD_CCW_WRITE_CKD_MT:
		data->auxiliary.length_valid = 0x1;
		data->length = reclen;
		data->operation.operation = 0x03;
		break;
	case DASD_ECKD_CCW_WRITE_FULL_TRACK:
		data->operation.orientation = 0x0;
		data->operation.operation = 0x3F;
		data->extended_operation = 0x11;
		data->length = 0;
		data->extended_parameter_length = 0x02;
		if (data->count > 8) {
			data->extended_parameter[0] = 0xFF;
			data->extended_parameter[1] = 0xFF;
			data->extended_parameter[1] <<= (16 - count);
		} else {
			data->extended_parameter[0] = 0xFF;
			data->extended_parameter[0] <<= (8 - count);
			data->extended_parameter[1] = 0x00;
		}
		data->sector = 0xFF;
		break;
	case DASD_ECKD_CCW_WRITE_TRACK_DATA:
		data->auxiliary.length_valid = 0x1;
		data->length = reclen;	/* not tlf, as one might think */
		data->operation.operation = 0x3F;
		data->extended_operation = 0x23;
		break;
	case DASD_ECKD_CCW_READ:
	case DASD_ECKD_CCW_READ_MT:
	case DASD_ECKD_CCW_READ_KD:
	case DASD_ECKD_CCW_READ_KD_MT:
		data->auxiliary.length_valid = 0x1;
		data->length = reclen;
		data->operation.operation = 0x06;
		break;
	case DASD_ECKD_CCW_READ_CKD:
	case DASD_ECKD_CCW_READ_CKD_MT:
		data->auxiliary.length_valid = 0x1;
		data->length = reclen;
		data->operation.operation = 0x16;
		break;
	case DASD_ECKD_CCW_READ_COUNT:
		data->operation.operation = 0x06;
		break;
	case DASD_ECKD_CCW_READ_TRACK:
		data->operation.orientation = 0x1;
		data->operation.operation = 0x0C;
		data->extended_parameter_length = 0;
		data->sector = 0xFF;
		break;
	case DASD_ECKD_CCW_READ_TRACK_DATA:
		data->auxiliary.length_valid = 0x1;
		data->length = tlf;
		data->operation.operation = 0x0C;
		break;
	case DASD_ECKD_CCW_ERASE:
		data->length = reclen;
		data->auxiliary.length_valid = 0x1;
		data->operation.operation = 0x0b;
		break;
	default:
		DBF_DEV_EVENT(DBF_ERR, device,
			    "fill LRE unknown opcode 0x%x", cmd);
		BUG();
	}
	set_ch_t(&data->seek_addr,
		 trk / private->rdc_data.trk_per_cyl,
		 trk % private->rdc_data.trk_per_cyl);
	data->search_arg.cyl = data->seek_addr.cyl;
	data->search_arg.head = data->seek_addr.head;
	data->search_arg.record = rec_on_trk;
}

static int prefix_LRE(struct ccw1 *ccw, struct PFX_eckd_data *pfxdata,
		      unsigned int trk, unsigned int totrk, int cmd,
		      struct dasd_device *basedev, struct dasd_device *startdev,
		      unsigned char format, unsigned int rec_on_trk, int count,
		      unsigned int blksize, unsigned int tlf)
{
	struct dasd_eckd_private *basepriv, *startpriv;
	struct DE_eckd_data *dedata;
	struct LRE_eckd_data *lredata;
	u32 begcyl, endcyl;
	u16 heads, beghead, endhead;
	int rc = 0;

	basepriv = (struct dasd_eckd_private *) basedev->private;
	startpriv = (struct dasd_eckd_private *) startdev->private;
	dedata = &pfxdata->define_extent;
	lredata = &pfxdata->locate_record;

	ccw->cmd_code = DASD_ECKD_CCW_PFX;
	ccw->flags = 0;
	if (cmd == DASD_ECKD_CCW_WRITE_FULL_TRACK) {
		ccw->count = sizeof(*pfxdata) + 2;
		ccw->cda = (__u32) __pa(pfxdata);
		memset(pfxdata, 0, sizeof(*pfxdata) + 2);
	} else {
		ccw->count = sizeof(*pfxdata);
		ccw->cda = (__u32) __pa(pfxdata);
		memset(pfxdata, 0, sizeof(*pfxdata));
	}

	/* prefix data */
	if (format > 1) {
		DBF_DEV_EVENT(DBF_ERR, basedev,
			      "PFX LRE unknown format 0x%x", format);
		BUG();
		return -EINVAL;
	}
	pfxdata->format = format;
	pfxdata->base_address = basepriv->ned->unit_addr;
	pfxdata->base_lss = basepriv->ned->ID;
	pfxdata->validity.define_extent = 1;

	/* private uid is kept up to date, conf_data may be outdated */
	if (startpriv->uid.type != UA_BASE_DEVICE) {
		pfxdata->validity.verify_base = 1;
		if (startpriv->uid.type == UA_HYPER_PAV_ALIAS)
			pfxdata->validity.hyper_pav = 1;
	}

	/* define extend data (mostly)*/
	switch (cmd) {
	case DASD_ECKD_CCW_READ_HOME_ADDRESS:
	case DASD_ECKD_CCW_READ_RECORD_ZERO:
	case DASD_ECKD_CCW_READ:
	case DASD_ECKD_CCW_READ_MT:
	case DASD_ECKD_CCW_READ_CKD:
	case DASD_ECKD_CCW_READ_CKD_MT:
	case DASD_ECKD_CCW_READ_KD:
	case DASD_ECKD_CCW_READ_KD_MT:
	case DASD_ECKD_CCW_READ_COUNT:
		dedata->mask.perm = 0x1;
		dedata->attributes.operation = basepriv->attrib.operation;
		break;
	case DASD_ECKD_CCW_READ_TRACK:
	case DASD_ECKD_CCW_READ_TRACK_DATA:
		dedata->mask.perm = 0x1;
		dedata->attributes.operation = basepriv->attrib.operation;
		dedata->blk_size = 0;
		break;
	case DASD_ECKD_CCW_WRITE:
	case DASD_ECKD_CCW_WRITE_MT:
	case DASD_ECKD_CCW_WRITE_KD:
	case DASD_ECKD_CCW_WRITE_KD_MT:
		dedata->mask.perm = 0x02;
		dedata->attributes.operation = basepriv->attrib.operation;
		rc = check_XRC_on_prefix(pfxdata, basedev);
		break;
	case DASD_ECKD_CCW_WRITE_CKD:
	case DASD_ECKD_CCW_WRITE_CKD_MT:
		dedata->attributes.operation = DASD_BYPASS_CACHE;
		rc = check_XRC_on_prefix(pfxdata, basedev);
		break;
	case DASD_ECKD_CCW_ERASE:
	case DASD_ECKD_CCW_WRITE_HOME_ADDRESS:
	case DASD_ECKD_CCW_WRITE_RECORD_ZERO:
		dedata->mask.perm = 0x3;
		dedata->mask.auth = 0x1;
		dedata->attributes.operation = DASD_BYPASS_CACHE;
		rc = check_XRC_on_prefix(pfxdata, basedev);
		break;
	case DASD_ECKD_CCW_WRITE_FULL_TRACK:
		dedata->mask.perm = 0x03;
		dedata->attributes.operation = basepriv->attrib.operation;
		dedata->blk_size = 0;
		break;
	case DASD_ECKD_CCW_WRITE_TRACK_DATA:
		dedata->mask.perm = 0x02;
		dedata->attributes.operation = basepriv->attrib.operation;
		dedata->blk_size = blksize;
		rc = check_XRC_on_prefix(pfxdata, basedev);
		break;
	default:
		DBF_DEV_EVENT(DBF_ERR, basedev,
			    "PFX LRE unknown opcode 0x%x", cmd);
		BUG();
		return -EINVAL;
	}

	dedata->attributes.mode = 0x3;	/* ECKD */

	if ((basepriv->rdc_data.cu_type == 0x2105 ||
	     basepriv->rdc_data.cu_type == 0x2107 ||
	     basepriv->rdc_data.cu_type == 0x1750)
	    && !(basepriv->uses_cdl && trk < 2))
		dedata->ga_extended |= 0x40; /* Regular Data Format Mode */

	heads = basepriv->rdc_data.trk_per_cyl;
	begcyl = trk / heads;
	beghead = trk % heads;
	endcyl = totrk / heads;
	endhead = totrk % heads;

	/* check for sequential prestage - enhance cylinder range */
	if (dedata->attributes.operation == DASD_SEQ_PRESTAGE ||
	    dedata->attributes.operation == DASD_SEQ_ACCESS) {

		if (endcyl + basepriv->attrib.nr_cyl < basepriv->real_cyl)
			endcyl += basepriv->attrib.nr_cyl;
		else
			endcyl = (basepriv->real_cyl - 1);
	}

	set_ch_t(&dedata->beg_ext, begcyl, beghead);
	set_ch_t(&dedata->end_ext, endcyl, endhead);

	if (format == 1) {
		fill_LRE_data(lredata, trk, rec_on_trk, count, cmd,
			      basedev, blksize, tlf);
	}

	return rc;
}

static int prefix(struct ccw1 *ccw, struct PFX_eckd_data *pfxdata,
		  unsigned int trk, unsigned int totrk, int cmd,
		  struct dasd_device *basedev, struct dasd_device *startdev)
{
	return prefix_LRE(ccw, pfxdata, trk, totrk, cmd, basedev, startdev,
			  0, 0, 0, 0, 0);
}

static void
locate_record(struct ccw1 *ccw, struct LO_eckd_data *data, unsigned int trk,
	      unsigned int rec_on_trk, int no_rec, int cmd,
	      struct dasd_device * device, int reclen)
{
	struct dasd_eckd_private *private;
	int sector;
	int dn, d;

	private = (struct dasd_eckd_private *) device->private;

	DBF_DEV_EVENT(DBF_INFO, device,
		  "Locate: trk %d, rec %d, no_rec %d, cmd %d, reclen %d",
		  trk, rec_on_trk, no_rec, cmd, reclen);

	ccw->cmd_code = DASD_ECKD_CCW_LOCATE_RECORD;
	ccw->flags = 0;
	ccw->count = 16;
	ccw->cda = (__u32) __pa(data);

	memset(data, 0, sizeof(struct LO_eckd_data));
	sector = 0;
	if (rec_on_trk) {
		switch (private->rdc_data.dev_type) {
		case 0x3390:
			dn = ceil_quot(reclen + 6, 232);
			d = 9 + ceil_quot(reclen + 6 * (dn + 1), 34);
			sector = (49 + (rec_on_trk - 1) * (10 + d)) / 8;
			break;
		case 0x3380:
			d = 7 + ceil_quot(reclen + 12, 32);
			sector = (39 + (rec_on_trk - 1) * (8 + d)) / 7;
			break;
		}
	}
	data->sector = sector;
	data->count = no_rec;
	switch (cmd) {
	case DASD_ECKD_CCW_WRITE_HOME_ADDRESS:
		data->operation.orientation = 0x3;
		data->operation.operation = 0x03;
		break;
	case DASD_ECKD_CCW_READ_HOME_ADDRESS:
		data->operation.orientation = 0x3;
		data->operation.operation = 0x16;
		break;
	case DASD_ECKD_CCW_WRITE_RECORD_ZERO:
		data->operation.orientation = 0x1;
		data->operation.operation = 0x03;
		data->count++;
		break;
	case DASD_ECKD_CCW_READ_RECORD_ZERO:
		data->operation.orientation = 0x3;
		data->operation.operation = 0x16;
		data->count++;
		break;
	case DASD_ECKD_CCW_WRITE:
	case DASD_ECKD_CCW_WRITE_MT:
	case DASD_ECKD_CCW_WRITE_KD:
	case DASD_ECKD_CCW_WRITE_KD_MT:
		data->auxiliary.last_bytes_used = 0x1;
		data->length = reclen;
		data->operation.operation = 0x01;
		break;
	case DASD_ECKD_CCW_WRITE_CKD:
	case DASD_ECKD_CCW_WRITE_CKD_MT:
		data->auxiliary.last_bytes_used = 0x1;
		data->length = reclen;
		data->operation.operation = 0x03;
		break;
	case DASD_ECKD_CCW_READ:
	case DASD_ECKD_CCW_READ_MT:
	case DASD_ECKD_CCW_READ_KD:
	case DASD_ECKD_CCW_READ_KD_MT:
		data->auxiliary.last_bytes_used = 0x1;
		data->length = reclen;
		data->operation.operation = 0x06;
		break;
	case DASD_ECKD_CCW_READ_CKD:
	case DASD_ECKD_CCW_READ_CKD_MT:
		data->auxiliary.last_bytes_used = 0x1;
		data->length = reclen;
		data->operation.operation = 0x16;
		break;
	case DASD_ECKD_CCW_READ_COUNT:
		data->operation.operation = 0x06;
		break;
	case DASD_ECKD_CCW_ERASE:
		data->length = reclen;
		data->auxiliary.last_bytes_used = 0x1;
		data->operation.operation = 0x0b;
		break;
	default:
		DBF_DEV_EVENT(DBF_ERR, device, "unknown locate record "
			      "opcode 0x%x", cmd);
	}
	set_ch_t(&data->seek_addr,
		 trk / private->rdc_data.trk_per_cyl,
		 trk % private->rdc_data.trk_per_cyl);
	data->search_arg.cyl = data->seek_addr.cyl;
	data->search_arg.head = data->seek_addr.head;
	data->search_arg.record = rec_on_trk;
}

/*
 * Returns 1 if the block is one of the special blocks that needs
 * to get read/written with the KD variant of the command.
 * That is DASD_ECKD_READ_KD_MT instead of DASD_ECKD_READ_MT and
 * DASD_ECKD_WRITE_KD_MT instead of DASD_ECKD_WRITE_MT.
 * Luckily the KD variants differ only by one bit (0x08) from the
 * normal variant. So don't wonder about code like:
 * if (dasd_eckd_cdl_special(blk_per_trk, recid))
 *         ccw->cmd_code |= 0x8;
 */
static inline int
dasd_eckd_cdl_special(int blk_per_trk, int recid)
{
	if (recid < 3)
		return 1;
	if (recid < blk_per_trk)
		return 0;
	if (recid < 2 * blk_per_trk)
		return 1;
	return 0;
}

/*
 * Returns the record size for the special blocks of the cdl format.
 * Only returns something useful if dasd_eckd_cdl_special is true
 * for the recid.
 */
static inline int
dasd_eckd_cdl_reclen(int recid)
{
	if (recid < 3)
		return sizes_trk0[recid];
	return LABEL_SIZE;
}
/* create unique id from private structure. */
static void create_uid(struct dasd_eckd_private *private)
{
	int count;
	struct dasd_uid *uid;

	uid = &private->uid;
	memset(uid, 0, sizeof(struct dasd_uid));
	memcpy(uid->vendor, private->ned->HDA_manufacturer,
	       sizeof(uid->vendor) - 1);
	EBCASC(uid->vendor, sizeof(uid->vendor) - 1);
	memcpy(uid->serial, private->ned->HDA_location,
	       sizeof(uid->serial) - 1);
	EBCASC(uid->serial, sizeof(uid->serial) - 1);
	uid->ssid = private->gneq->subsystemID;
	uid->real_unit_addr = private->ned->unit_addr;
	if (private->sneq) {
		uid->type = private->sneq->sua_flags;
		if (uid->type == UA_BASE_PAV_ALIAS)
			uid->base_unit_addr = private->sneq->base_unit_addr;
	} else {
		uid->type = UA_BASE_DEVICE;
	}
	if (private->vdsneq) {
		for (count = 0; count < 16; count++) {
			sprintf(uid->vduit+2*count, "%02x",
				private->vdsneq->uit[count]);
		}
	}
}

/*
 * Generate device unique id that specifies the physical device.
 */
static int dasd_eckd_generate_uid(struct dasd_device *device)
{
	struct dasd_eckd_private *private;
	unsigned long flags;

	private = (struct dasd_eckd_private *) device->private;
	if (!private)
		return -ENODEV;
	if (!private->ned || !private->gneq)
		return -ENODEV;
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	create_uid(private);
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	return 0;
}

static int dasd_eckd_get_uid(struct dasd_device *device, struct dasd_uid *uid)
{
	struct dasd_eckd_private *private;
	unsigned long flags;

	if (device->private) {
		private = (struct dasd_eckd_private *)device->private;
		spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
		*uid = private->uid;
		spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
		return 0;
	}
	return -EINVAL;
}

/*
 * compare device UID with data of a given dasd_eckd_private structure
 * return 0 for match
 */
static int dasd_eckd_compare_path_uid(struct dasd_device *device,
				      struct dasd_eckd_private *private)
{
	struct dasd_uid device_uid;

	create_uid(private);
	dasd_eckd_get_uid(device, &device_uid);

	return memcmp(&device_uid, &private->uid, sizeof(struct dasd_uid));
}

static void dasd_eckd_fill_rcd_cqr(struct dasd_device *device,
				   struct dasd_ccw_req *cqr,
				   __u8 *rcd_buffer,
				   __u8 lpm)
{
	struct ccw1 *ccw;
	/*
	 * buffer has to start with EBCDIC "V1.0" to show
	 * support for virtual device SNEQ
	 */
	rcd_buffer[0] = 0xE5;
	rcd_buffer[1] = 0xF1;
	rcd_buffer[2] = 0x4B;
	rcd_buffer[3] = 0xF0;

	ccw = cqr->cpaddr;
	ccw->cmd_code = DASD_ECKD_CCW_RCD;
	ccw->flags = 0;
	ccw->cda = (__u32)(addr_t)rcd_buffer;
	ccw->count = DASD_ECKD_RCD_DATA_SIZE;
	cqr->magic = DASD_ECKD_MAGIC;

	cqr->startdev = device;
	cqr->memdev = device;
	cqr->block = NULL;
	cqr->expires = 10*HZ;
	cqr->lpm = lpm;
	cqr->retries = 256;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;
	set_bit(DASD_CQR_VERIFY_PATH, &cqr->flags);
}

/*
 * Wakeup helper for read_conf
 * if the cqr is not done and needs some error recovery
 * the buffer has to be re-initialized with the EBCDIC "V1.0"
 * to show support for virtual device SNEQ
 */
static void read_conf_cb(struct dasd_ccw_req *cqr, void *data)
{
	struct ccw1 *ccw;
	__u8 *rcd_buffer;

	if (cqr->status !=  DASD_CQR_DONE) {
		ccw = cqr->cpaddr;
		rcd_buffer = (__u8 *)((addr_t) ccw->cda);
		memset(rcd_buffer, 0, sizeof(*rcd_buffer));

		rcd_buffer[0] = 0xE5;
		rcd_buffer[1] = 0xF1;
		rcd_buffer[2] = 0x4B;
		rcd_buffer[3] = 0xF0;
	}
	dasd_wakeup_cb(cqr, data);
}

static int dasd_eckd_read_conf_immediately(struct dasd_device *device,
					   struct dasd_ccw_req *cqr,
					   __u8 *rcd_buffer,
					   __u8 lpm)
{
	struct ciw *ciw;
	int rc;
	/*
	 * sanity check: scan for RCD command in extended SenseID data
	 * some devices do not support RCD
	 */
	ciw = ccw_device_get_ciw(device->cdev, CIW_TYPE_RCD);
	if (!ciw || ciw->cmd != DASD_ECKD_CCW_RCD)
		return -EOPNOTSUPP;

	dasd_eckd_fill_rcd_cqr(device, cqr, rcd_buffer, lpm);
	clear_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags);
	set_bit(DASD_CQR_ALLOW_SLOCK, &cqr->flags);
	cqr->retries = 5;
	cqr->callback = read_conf_cb;
	rc = dasd_sleep_on_immediatly(cqr);
	return rc;
}

static int dasd_eckd_read_conf_lpm(struct dasd_device *device,
				   void **rcd_buffer,
				   int *rcd_buffer_size, __u8 lpm)
{
	struct ciw *ciw;
	char *rcd_buf = NULL;
	int ret;
	struct dasd_ccw_req *cqr;

	/*
	 * sanity check: scan for RCD command in extended SenseID data
	 * some devices do not support RCD
	 */
	ciw = ccw_device_get_ciw(device->cdev, CIW_TYPE_RCD);
	if (!ciw || ciw->cmd != DASD_ECKD_CCW_RCD) {
		ret = -EOPNOTSUPP;
		goto out_error;
	}
	rcd_buf = kzalloc(DASD_ECKD_RCD_DATA_SIZE, GFP_KERNEL | GFP_DMA);
	if (!rcd_buf) {
		ret = -ENOMEM;
		goto out_error;
	}
	cqr = dasd_smalloc_request(DASD_ECKD_MAGIC, 1 /* RCD */,
				   0, /* use rcd_buf as data ara */
				   device);
	if (IS_ERR(cqr)) {
		DBF_DEV_EVENT(DBF_WARNING, device, "%s",
			      "Could not allocate RCD request");
		ret = -ENOMEM;
		goto out_error;
	}
	dasd_eckd_fill_rcd_cqr(device, cqr, rcd_buf, lpm);
	cqr->callback = read_conf_cb;
	ret = dasd_sleep_on(cqr);
	/*
	 * on success we update the user input parms
	 */
	dasd_sfree_request(cqr, cqr->memdev);
	if (ret)
		goto out_error;

	*rcd_buffer_size = DASD_ECKD_RCD_DATA_SIZE;
	*rcd_buffer = rcd_buf;
	return 0;
out_error:
	kfree(rcd_buf);
	*rcd_buffer = NULL;
	*rcd_buffer_size = 0;
	return ret;
}

static int dasd_eckd_identify_conf_parts(struct dasd_eckd_private *private)
{

	struct dasd_sneq *sneq;
	int i, count;

	private->ned = NULL;
	private->sneq = NULL;
	private->vdsneq = NULL;
	private->gneq = NULL;
	count = private->conf_len / sizeof(struct dasd_sneq);
	sneq = (struct dasd_sneq *)private->conf_data;
	for (i = 0; i < count; ++i) {
		if (sneq->flags.identifier == 1 && sneq->format == 1)
			private->sneq = sneq;
		else if (sneq->flags.identifier == 1 && sneq->format == 4)
			private->vdsneq = (struct vd_sneq *)sneq;
		else if (sneq->flags.identifier == 2)
			private->gneq = (struct dasd_gneq *)sneq;
		else if (sneq->flags.identifier == 3 && sneq->res1 == 1)
			private->ned = (struct dasd_ned *)sneq;
		sneq++;
	}
	if (!private->ned || !private->gneq) {
		private->ned = NULL;
		private->sneq = NULL;
		private->vdsneq = NULL;
		private->gneq = NULL;
		return -EINVAL;
	}
	return 0;

};

static unsigned char dasd_eckd_path_access(void *conf_data, int conf_len)
{
	struct dasd_gneq *gneq;
	int i, count, found;

	count = conf_len / sizeof(*gneq);
	gneq = (struct dasd_gneq *)conf_data;
	found = 0;
	for (i = 0; i < count; ++i) {
		if (gneq->flags.identifier == 2) {
			found = 1;
			break;
		}
		gneq++;
	}
	if (found)
		return ((char *)gneq)[18] & 0x07;
	else
		return 0;
}

static int dasd_eckd_read_conf(struct dasd_device *device)
{
	void *conf_data;
	int conf_len, conf_data_saved;
	int rc, path_err;
	__u8 lpm, opm;
	struct dasd_eckd_private *private, path_private;
	struct dasd_path *path_data;
	struct dasd_uid *uid;
	char print_path_uid[60], print_device_uid[60];

	private = (struct dasd_eckd_private *) device->private;
	path_data = &device->path_data;
	opm = ccw_device_get_path_mask(device->cdev);
	conf_data_saved = 0;
	path_err = 0;
	/* get configuration data per operational path */
	for (lpm = 0x80; lpm; lpm>>= 1) {
		if (!(lpm & opm))
			continue;
		rc = dasd_eckd_read_conf_lpm(device, &conf_data,
					     &conf_len, lpm);
		if (rc && rc != -EOPNOTSUPP) {	/* -EOPNOTSUPP is ok */
			DBF_EVENT_DEVID(DBF_WARNING, device->cdev,
					"Read configuration data returned "
					"error %d", rc);
			return rc;
		}
		if (conf_data == NULL) {
			DBF_EVENT_DEVID(DBF_WARNING, device->cdev, "%s",
					"No configuration data "
					"retrieved");
			/* no further analysis possible */
			path_data->opm |= lpm;
			continue;	/* no error */
		}
		/* save first valid configuration data */
		if (!conf_data_saved) {
			kfree(private->conf_data);
			private->conf_data = conf_data;
			private->conf_len = conf_len;
			if (dasd_eckd_identify_conf_parts(private)) {
				private->conf_data = NULL;
				private->conf_len = 0;
				kfree(conf_data);
				continue;
			}
			/*
			 * build device UID that other path data
			 * can be compared to it
			 */
			dasd_eckd_generate_uid(device);
			conf_data_saved++;
		} else {
			path_private.conf_data = conf_data;
			path_private.conf_len = DASD_ECKD_RCD_DATA_SIZE;
			if (dasd_eckd_identify_conf_parts(
				    &path_private)) {
				path_private.conf_data = NULL;
				path_private.conf_len = 0;
				kfree(conf_data);
				continue;
			}

			if (dasd_eckd_compare_path_uid(
				    device, &path_private)) {
				uid = &path_private.uid;
				if (strlen(uid->vduit) > 0)
					snprintf(print_path_uid,
						 sizeof(print_path_uid),
						 "%s.%s.%04x.%02x.%s",
						 uid->vendor, uid->serial,
						 uid->ssid, uid->real_unit_addr,
						 uid->vduit);
				else
					snprintf(print_path_uid,
						 sizeof(print_path_uid),
						 "%s.%s.%04x.%02x",
						 uid->vendor, uid->serial,
						 uid->ssid,
						 uid->real_unit_addr);
				uid = &private->uid;
				if (strlen(uid->vduit) > 0)
					snprintf(print_device_uid,
						 sizeof(print_device_uid),
						 "%s.%s.%04x.%02x.%s",
						 uid->vendor, uid->serial,
						 uid->ssid, uid->real_unit_addr,
						 uid->vduit);
				else
					snprintf(print_device_uid,
						 sizeof(print_device_uid),
						 "%s.%s.%04x.%02x",
						 uid->vendor, uid->serial,
						 uid->ssid,
						 uid->real_unit_addr);
				dev_err(&device->cdev->dev,
					"Not all channel paths lead to "
					"the same device, path %02X leads to "
					"device %s instead of %s\n", lpm,
					print_path_uid, print_device_uid);
				path_err = -EINVAL;
				continue;
			}

			path_private.conf_data = NULL;
			path_private.conf_len = 0;
		}
		switch (dasd_eckd_path_access(conf_data, conf_len)) {
		case 0x02:
			path_data->npm |= lpm;
			break;
		case 0x03:
			path_data->ppm |= lpm;
			break;
		}
		path_data->opm |= lpm;

		if (conf_data != private->conf_data)
			kfree(conf_data);
	}

	return path_err;
}

static int verify_fcx_max_data(struct dasd_device *device, __u8 lpm)
{
	struct dasd_eckd_private *private;
	int mdc;
	u32 fcx_max_data;

	private = (struct dasd_eckd_private *) device->private;
	if (private->fcx_max_data) {
		mdc = ccw_device_get_mdc(device->cdev, lpm);
		if ((mdc < 0)) {
			dev_warn(&device->cdev->dev,
				 "Detecting the maximum data size for zHPF "
				 "requests failed (rc=%d) for a new path %x\n",
				 mdc, lpm);
			return mdc;
		}
		fcx_max_data = mdc * FCX_MAX_DATA_FACTOR;
		if (fcx_max_data < private->fcx_max_data) {
			dev_warn(&device->cdev->dev,
				 "The maximum data size for zHPF requests %u "
				 "on a new path %x is below the active maximum "
				 "%u\n", fcx_max_data, lpm,
				 private->fcx_max_data);
			return -EACCES;
		}
	}
	return 0;
}

static int rebuild_device_uid(struct dasd_device *device,
			      struct path_verification_work_data *data)
{
	struct dasd_eckd_private *private;
	struct dasd_path *path_data;
	__u8 lpm, opm;
	int rc;

	rc = -ENODEV;
	private = (struct dasd_eckd_private *) device->private;
	path_data = &device->path_data;
	opm = device->path_data.opm;

	for (lpm = 0x80; lpm; lpm >>= 1) {
		if (!(lpm & opm))
			continue;
		memset(&data->rcd_buffer, 0, sizeof(data->rcd_buffer));
		memset(&data->cqr, 0, sizeof(data->cqr));
		data->cqr.cpaddr = &data->ccw;
		rc = dasd_eckd_read_conf_immediately(device, &data->cqr,
						     data->rcd_buffer,
						     lpm);

		if (rc) {
			if (rc == -EOPNOTSUPP) /* -EOPNOTSUPP is ok */
				continue;
			DBF_EVENT_DEVID(DBF_WARNING, device->cdev,
					"Read configuration data "
					"returned error %d", rc);
			break;
		}
		memcpy(private->conf_data, data->rcd_buffer,
		       DASD_ECKD_RCD_DATA_SIZE);
		if (dasd_eckd_identify_conf_parts(private)) {
			rc = -ENODEV;
		} else /* first valid path is enough */
			break;
	}

	if (!rc)
		rc = dasd_eckd_generate_uid(device);

	return rc;
}

static void do_path_verification_work(struct work_struct *work)
{
	struct path_verification_work_data *data;
	struct dasd_device *device;
	struct dasd_eckd_private path_private;
	struct dasd_uid *uid;
	__u8 path_rcd_buf[DASD_ECKD_RCD_DATA_SIZE];
	__u8 lpm, opm, npm, ppm, epm;
	unsigned long flags;
	char print_uid[60];
	int rc;

	data = container_of(work, struct path_verification_work_data, worker);
	device = data->device;

	/* delay path verification until device was resumed */
	if (test_bit(DASD_FLAG_SUSPENDED, &device->flags)) {
		schedule_work(work);
		return;
	}

	opm = 0;
	npm = 0;
	ppm = 0;
	epm = 0;
	for (lpm = 0x80; lpm; lpm >>= 1) {
		if (!(lpm & data->tbvpm))
			continue;
		memset(&data->rcd_buffer, 0, sizeof(data->rcd_buffer));
		memset(&data->cqr, 0, sizeof(data->cqr));
		data->cqr.cpaddr = &data->ccw;
		rc = dasd_eckd_read_conf_immediately(device, &data->cqr,
						     data->rcd_buffer,
						     lpm);
		if (!rc) {
			switch (dasd_eckd_path_access(data->rcd_buffer,
						      DASD_ECKD_RCD_DATA_SIZE)
				) {
			case 0x02:
				npm |= lpm;
				break;
			case 0x03:
				ppm |= lpm;
				break;
			}
			opm |= lpm;
		} else if (rc == -EOPNOTSUPP) {
			DBF_EVENT_DEVID(DBF_WARNING, device->cdev, "%s",
					"path verification: No configuration "
					"data retrieved");
			opm |= lpm;
		} else if (rc == -EAGAIN) {
			DBF_EVENT_DEVID(DBF_WARNING, device->cdev, "%s",
					"path verification: device is stopped,"
					" try again later");
			epm |= lpm;
		} else {
			dev_warn(&device->cdev->dev,
				 "Reading device feature codes failed "
				 "(rc=%d) for new path %x\n", rc, lpm);
			continue;
		}
		if (verify_fcx_max_data(device, lpm)) {
			opm &= ~lpm;
			npm &= ~lpm;
			ppm &= ~lpm;
			continue;
		}

		/*
		 * save conf_data for comparison after
		 * rebuild_device_uid may have changed
		 * the original data
		 */
		memcpy(&path_rcd_buf, data->rcd_buffer,
		       DASD_ECKD_RCD_DATA_SIZE);
		path_private.conf_data = (void *) &path_rcd_buf;
		path_private.conf_len = DASD_ECKD_RCD_DATA_SIZE;
		if (dasd_eckd_identify_conf_parts(&path_private)) {
			path_private.conf_data = NULL;
			path_private.conf_len = 0;
			continue;
		}

		/*
		 * compare path UID with device UID only if at least
		 * one valid path is left
		 * in other case the device UID may have changed and
		 * the first working path UID will be used as device UID
		 */
		if (device->path_data.opm &&
		    dasd_eckd_compare_path_uid(device, &path_private)) {
			/*
			 * the comparison was not successful
			 * rebuild the device UID with at least one
			 * known path in case a z/VM hyperswap command
			 * has changed the device
			 *
			 * after this compare again
			 *
			 * if either the rebuild or the recompare fails
			 * the path can not be used
			 */
			if (rebuild_device_uid(device, data) ||
			    dasd_eckd_compare_path_uid(
				    device, &path_private)) {
				uid = &path_private.uid;
				if (strlen(uid->vduit) > 0)
					snprintf(print_uid, sizeof(print_uid),
						 "%s.%s.%04x.%02x.%s",
						 uid->vendor, uid->serial,
						 uid->ssid, uid->real_unit_addr,
						 uid->vduit);
				else
					snprintf(print_uid, sizeof(print_uid),
						 "%s.%s.%04x.%02x",
						 uid->vendor, uid->serial,
						 uid->ssid,
						 uid->real_unit_addr);
				dev_err(&device->cdev->dev,
					"The newly added channel path %02X "
					"will not be used because it leads "
					"to a different device %s\n",
					lpm, print_uid);
				opm &= ~lpm;
				npm &= ~lpm;
				ppm &= ~lpm;
				continue;
			}
		}

		/*
		 * There is a small chance that a path is lost again between
		 * above path verification and the following modification of
		 * the device opm mask. We could avoid that race here by using
		 * yet another path mask, but we rather deal with this unlikely
		 * situation in dasd_start_IO.
		 */
		spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
		if (!device->path_data.opm && opm) {
			device->path_data.opm = opm;
			dasd_generic_path_operational(device);
		} else
			device->path_data.opm |= opm;
		device->path_data.npm |= npm;
		device->path_data.ppm |= ppm;
		device->path_data.tbvpm |= epm;
		spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	}

	dasd_put_device(device);
	if (data->isglobal)
		mutex_unlock(&dasd_path_verification_mutex);
	else
		kfree(data);
}

static int dasd_eckd_verify_path(struct dasd_device *device, __u8 lpm)
{
	struct path_verification_work_data *data;

	data = kmalloc(sizeof(*data), GFP_ATOMIC | GFP_DMA);
	if (!data) {
		if (mutex_trylock(&dasd_path_verification_mutex)) {
			data = path_verification_worker;
			data->isglobal = 1;
		} else
			return -ENOMEM;
	} else {
		memset(data, 0, sizeof(*data));
		data->isglobal = 0;
	}
	INIT_WORK(&data->worker, do_path_verification_work);
	dasd_get_device(device);
	data->device = device;
	data->tbvpm = lpm;
	schedule_work(&data->worker);
	return 0;
}

static int dasd_eckd_read_features(struct dasd_device *device)
{
	struct dasd_psf_prssd_data *prssdp;
	struct dasd_rssd_features *features;
	struct dasd_ccw_req *cqr;
	struct ccw1 *ccw;
	int rc;
	struct dasd_eckd_private *private;

	private = (struct dasd_eckd_private *) device->private;
	memset(&private->features, 0, sizeof(struct dasd_rssd_features));
	cqr = dasd_smalloc_request(DASD_ECKD_MAGIC, 1 /* PSF */	+ 1 /* RSSD */,
				   (sizeof(struct dasd_psf_prssd_data) +
				    sizeof(struct dasd_rssd_features)),
				   device);
	if (IS_ERR(cqr)) {
		DBF_EVENT_DEVID(DBF_WARNING, device->cdev, "%s", "Could not "
				"allocate initialization request");
		return PTR_ERR(cqr);
	}
	cqr->startdev = device;
	cqr->memdev = device;
	cqr->block = NULL;
	cqr->retries = 256;
	cqr->expires = 10 * HZ;

	/* Prepare for Read Subsystem Data */
	prssdp = (struct dasd_psf_prssd_data *) cqr->data;
	memset(prssdp, 0, sizeof(struct dasd_psf_prssd_data));
	prssdp->order = PSF_ORDER_PRSSD;
	prssdp->suborder = 0x41;	/* Read Feature Codes */
	/* all other bytes of prssdp must be zero */

	ccw = cqr->cpaddr;
	ccw->cmd_code = DASD_ECKD_CCW_PSF;
	ccw->count = sizeof(struct dasd_psf_prssd_data);
	ccw->flags |= CCW_FLAG_CC;
	ccw->cda = (__u32)(addr_t) prssdp;

	/* Read Subsystem Data - feature codes */
	features = (struct dasd_rssd_features *) (prssdp + 1);
	memset(features, 0, sizeof(struct dasd_rssd_features));

	ccw++;
	ccw->cmd_code = DASD_ECKD_CCW_RSSD;
	ccw->count = sizeof(struct dasd_rssd_features);
	ccw->cda = (__u32)(addr_t) features;

	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;
	rc = dasd_sleep_on(cqr);
	if (rc == 0) {
		prssdp = (struct dasd_psf_prssd_data *) cqr->data;
		features = (struct dasd_rssd_features *) (prssdp + 1);
		memcpy(&private->features, features,
		       sizeof(struct dasd_rssd_features));
	} else
		dev_warn(&device->cdev->dev, "Reading device feature codes"
			 " failed with rc=%d\n", rc);
	dasd_sfree_request(cqr, cqr->memdev);
	return rc;
}


/*
 * Build CP for Perform Subsystem Function - SSC.
 */
static struct dasd_ccw_req *dasd_eckd_build_psf_ssc(struct dasd_device *device,
						    int enable_pav)
{
	struct dasd_ccw_req *cqr;
	struct dasd_psf_ssc_data *psf_ssc_data;
	struct ccw1 *ccw;

	cqr = dasd_smalloc_request(DASD_ECKD_MAGIC, 1 /* PSF */ ,
				  sizeof(struct dasd_psf_ssc_data),
				  device);

	if (IS_ERR(cqr)) {
		DBF_DEV_EVENT(DBF_WARNING, device, "%s",
			   "Could not allocate PSF-SSC request");
		return cqr;
	}
	psf_ssc_data = (struct dasd_psf_ssc_data *)cqr->data;
	psf_ssc_data->order = PSF_ORDER_SSC;
	psf_ssc_data->suborder = 0xc0;
	if (enable_pav) {
		psf_ssc_data->suborder |= 0x08;
		psf_ssc_data->reserved[0] = 0x88;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = DASD_ECKD_CCW_PSF;
	ccw->cda = (__u32)(addr_t)psf_ssc_data;
	ccw->count = 66;

	cqr->startdev = device;
	cqr->memdev = device;
	cqr->block = NULL;
	cqr->retries = 256;
	cqr->expires = 10*HZ;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;
	return cqr;
}

/*
 * Perform Subsystem Function.
 * It is necessary to trigger CIO for channel revalidation since this
 * call might change behaviour of DASD devices.
 */
static int
dasd_eckd_psf_ssc(struct dasd_device *device, int enable_pav,
		  unsigned long flags)
{
	struct dasd_ccw_req *cqr;
	int rc;

	cqr = dasd_eckd_build_psf_ssc(device, enable_pav);
	if (IS_ERR(cqr))
		return PTR_ERR(cqr);

	/*
	 * set flags e.g. turn on failfast, to prevent blocking
	 * the calling function should handle failed requests
	 */
	cqr->flags |= flags;

	rc = dasd_sleep_on(cqr);
	if (!rc)
		/* trigger CIO to reprobe devices */
		css_schedule_reprobe();
	else if (cqr->intrc == -EAGAIN)
		rc = -EAGAIN;

	dasd_sfree_request(cqr, cqr->memdev);
	return rc;
}

/*
 * Valide storage server of current device.
 */
static int dasd_eckd_validate_server(struct dasd_device *device,
				     unsigned long flags)
{
	int rc;
	struct dasd_eckd_private *private;
	int enable_pav;

	private = (struct dasd_eckd_private *) device->private;
	if (private->uid.type == UA_BASE_PAV_ALIAS ||
	    private->uid.type == UA_HYPER_PAV_ALIAS)
		return 0;
	if (dasd_nopav || MACHINE_IS_VM)
		enable_pav = 0;
	else
		enable_pav = 1;
	rc = dasd_eckd_psf_ssc(device, enable_pav, flags);

	/* may be requested feature is not available on server,
	 * therefore just report error and go ahead */
	DBF_EVENT_DEVID(DBF_WARNING, device->cdev, "PSF-SSC for SSID %04x "
			"returned rc=%d", private->uid.ssid, rc);
	return rc;
}

/*
 * worker to do a validate server in case of a lost pathgroup
 */
static void dasd_eckd_do_validate_server(struct work_struct *work)
{
	struct dasd_device *device = container_of(work, struct dasd_device,
						  kick_validate);
	unsigned long flags = 0;

	set_bit(DASD_CQR_FLAGS_FAILFAST, &flags);
	if (dasd_eckd_validate_server(device, flags)
	    == -EAGAIN) {
		/* schedule worker again if failed */
		schedule_work(&device->kick_validate);
		return;
	}

	dasd_put_device(device);
}

static void dasd_eckd_kick_validate_server(struct dasd_device *device)
{
	dasd_get_device(device);
	/* exit if device not online or in offline processing */
	if (test_bit(DASD_FLAG_OFFLINE, &device->flags) ||
	   device->state < DASD_STATE_ONLINE) {
		dasd_put_device(device);
		return;
	}
	/* queue call to do_validate_server to the kernel event daemon. */
	schedule_work(&device->kick_validate);
}

static u32 get_fcx_max_data(struct dasd_device *device)
{
#if defined(CONFIG_64BIT)
	int tpm, mdc;
	int fcx_in_css, fcx_in_gneq, fcx_in_features;
	struct dasd_eckd_private *private;

	if (dasd_nofcx)
		return 0;
	/* is transport mode supported? */
	private = (struct dasd_eckd_private *) device->private;
	fcx_in_css = css_general_characteristics.fcx;
	fcx_in_gneq = private->gneq->reserved2[7] & 0x04;
	fcx_in_features = private->features.feature[40] & 0x80;
	tpm = fcx_in_css && fcx_in_gneq && fcx_in_features;

	if (!tpm)
		return 0;

	mdc = ccw_device_get_mdc(device->cdev, 0);
	if (mdc < 0) {
		dev_warn(&device->cdev->dev, "Detecting the maximum supported"
			 " data size for zHPF requests failed\n");
		return 0;
	} else
		return mdc * FCX_MAX_DATA_FACTOR;
#else
	return 0;
#endif
}

/*
 * Check device characteristics.
 * If the device is accessible using ECKD discipline, the device is enabled.
 */
static int
dasd_eckd_check_characteristics(struct dasd_device *device)
{
	struct dasd_eckd_private *private;
	struct dasd_block *block;
	struct dasd_uid temp_uid;
	int rc, i;
	int readonly;
	unsigned long value;

	/* setup work queue for validate server*/
	INIT_WORK(&device->kick_validate, dasd_eckd_do_validate_server);

	if (!ccw_device_is_pathgroup(device->cdev)) {
		dev_warn(&device->cdev->dev,
			 "A channel path group could not be established\n");
		return -EIO;
	}
	if (!ccw_device_is_multipath(device->cdev)) {
		dev_info(&device->cdev->dev,
			 "The DASD is not operating in multipath mode\n");
	}
	private = (struct dasd_eckd_private *) device->private;
	if (!private) {
		private = kzalloc(sizeof(*private), GFP_KERNEL | GFP_DMA);
		if (!private) {
			dev_warn(&device->cdev->dev,
				 "Allocating memory for private DASD data "
				 "failed\n");
			return -ENOMEM;
		}
		device->private = (void *) private;
	} else {
		memset(private, 0, sizeof(*private));
	}
	/* Invalidate status of initial analysis. */
	private->init_cqr_status = -1;
	/* Set default cache operations. */
	private->attrib.operation = DASD_NORMAL_CACHE;
	private->attrib.nr_cyl = 0;

	/* Read Configuration Data */
	rc = dasd_eckd_read_conf(device);
	if (rc)
		goto out_err1;

	/* set default timeout */
	device->default_expires = DASD_EXPIRES;
	/* set default retry count */
	device->default_retries = DASD_RETRIES;

	if (private->gneq) {
		value = 1;
		for (i = 0; i < private->gneq->timeout.value; i++)
			value = 10 * value;
		value = value * private->gneq->timeout.number;
		/* do not accept useless values */
		if (value != 0 && value <= DASD_EXPIRES_MAX)
			device->default_expires = value;
	}

	dasd_eckd_get_uid(device, &temp_uid);
	if (temp_uid.type == UA_BASE_DEVICE) {
		block = dasd_alloc_block();
		if (IS_ERR(block)) {
			DBF_EVENT_DEVID(DBF_WARNING, device->cdev, "%s",
					"could not allocate dasd "
					"block structure");
			rc = PTR_ERR(block);
			goto out_err1;
		}
		device->block = block;
		block->base = device;
	}

	/* register lcu with alias handling, enable PAV */
	rc = dasd_alias_make_device_known_to_lcu(device);
	if (rc)
		goto out_err2;

	dasd_eckd_validate_server(device, 0);

	/* device may report different configuration data after LCU setup */
	rc = dasd_eckd_read_conf(device);
	if (rc)
		goto out_err3;

	/* Read Feature Codes */
	dasd_eckd_read_features(device);

	/* Read Device Characteristics */
	rc = dasd_generic_read_dev_chars(device, DASD_ECKD_MAGIC,
					 &private->rdc_data, 64);
	if (rc) {
		DBF_EVENT_DEVID(DBF_WARNING, device->cdev,
				"Read device characteristic failed, rc=%d", rc);
		goto out_err3;
	}

	if ((device->features & DASD_FEATURE_USERAW) &&
	    !(private->rdc_data.facilities.RT_in_LR)) {
		dev_err(&device->cdev->dev, "The storage server does not "
			"support raw-track access\n");
		rc = -EINVAL;
		goto out_err3;
	}

	/* find the valid cylinder size */
	if (private->rdc_data.no_cyl == LV_COMPAT_CYL &&
	    private->rdc_data.long_no_cyl)
		private->real_cyl = private->rdc_data.long_no_cyl;
	else
		private->real_cyl = private->rdc_data.no_cyl;

	private->fcx_max_data = get_fcx_max_data(device);

	readonly = dasd_device_is_ro(device);
	if (readonly)
		set_bit(DASD_FLAG_DEVICE_RO, &device->flags);

	dev_info(&device->cdev->dev, "New DASD %04X/%02X (CU %04X/%02X) "
		 "with %d cylinders, %d heads, %d sectors%s\n",
		 private->rdc_data.dev_type,
		 private->rdc_data.dev_model,
		 private->rdc_data.cu_type,
		 private->rdc_data.cu_model.model,
		 private->real_cyl,
		 private->rdc_data.trk_per_cyl,
		 private->rdc_data.sec_per_trk,
		 readonly ? ", read-only device" : "");
	return 0;

out_err3:
	dasd_alias_disconnect_device_from_lcu(device);
out_err2:
	dasd_free_block(device->block);
	device->block = NULL;
out_err1:
	kfree(private->conf_data);
	kfree(device->private);
	device->private = NULL;
	return rc;
}

static void dasd_eckd_uncheck_device(struct dasd_device *device)
{
	struct dasd_eckd_private *private;

	private = (struct dasd_eckd_private *) device->private;
	dasd_alias_disconnect_device_from_lcu(device);
	private->ned = NULL;
	private->sneq = NULL;
	private->vdsneq = NULL;
	private->gneq = NULL;
	private->conf_len = 0;
	kfree(private->conf_data);
	private->conf_data = NULL;
}

static struct dasd_ccw_req *
dasd_eckd_analysis_ccw(struct dasd_device *device)
{
	struct dasd_eckd_private *private;
	struct eckd_count *count_data;
	struct LO_eckd_data *LO_data;
	struct dasd_ccw_req *cqr;
	struct ccw1 *ccw;
	int cplength, datasize;
	int i;

	private = (struct dasd_eckd_private *) device->private;

	cplength = 8;
	datasize = sizeof(struct DE_eckd_data) + 2*sizeof(struct LO_eckd_data);
	cqr = dasd_smalloc_request(DASD_ECKD_MAGIC, cplength, datasize, device);
	if (IS_ERR(cqr))
		return cqr;
	ccw = cqr->cpaddr;
	/* Define extent for the first 3 tracks. */
	define_extent(ccw++, cqr->data, 0, 2,
		      DASD_ECKD_CCW_READ_COUNT, device);
	LO_data = cqr->data + sizeof(struct DE_eckd_data);
	/* Locate record for the first 4 records on track 0. */
	ccw[-1].flags |= CCW_FLAG_CC;
	locate_record(ccw++, LO_data++, 0, 0, 4,
		      DASD_ECKD_CCW_READ_COUNT, device, 0);

	count_data = private->count_area;
	for (i = 0; i < 4; i++) {
		ccw[-1].flags |= CCW_FLAG_CC;
		ccw->cmd_code = DASD_ECKD_CCW_READ_COUNT;
		ccw->flags = 0;
		ccw->count = 8;
		ccw->cda = (__u32)(addr_t) count_data;
		ccw++;
		count_data++;
	}

	/* Locate record for the first record on track 2. */
	ccw[-1].flags |= CCW_FLAG_CC;
	locate_record(ccw++, LO_data++, 2, 0, 1,
		      DASD_ECKD_CCW_READ_COUNT, device, 0);
	/* Read count ccw. */
	ccw[-1].flags |= CCW_FLAG_CC;
	ccw->cmd_code = DASD_ECKD_CCW_READ_COUNT;
	ccw->flags = 0;
	ccw->count = 8;
	ccw->cda = (__u32)(addr_t) count_data;

	cqr->block = NULL;
	cqr->startdev = device;
	cqr->memdev = device;
	cqr->retries = 255;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;
	return cqr;
}

/* differentiate between 'no record found' and any other error */
static int dasd_eckd_analysis_evaluation(struct dasd_ccw_req *init_cqr)
{
	char *sense;
	if (init_cqr->status == DASD_CQR_DONE)
		return INIT_CQR_OK;
	else if (init_cqr->status == DASD_CQR_NEED_ERP ||
		 init_cqr->status == DASD_CQR_FAILED) {
		sense = dasd_get_sense(&init_cqr->irb);
		if (sense && (sense[1] & SNS1_NO_REC_FOUND))
			return INIT_CQR_UNFORMATTED;
		else
			return INIT_CQR_ERROR;
	} else
		return INIT_CQR_ERROR;
}

/*
 * This is the callback function for the init_analysis cqr. It saves
 * the status of the initial analysis ccw before it frees it and kicks
 * the device to continue the startup sequence. This will call
 * dasd_eckd_do_analysis again (if the devices has not been marked
 * for deletion in the meantime).
 */
static void dasd_eckd_analysis_callback(struct dasd_ccw_req *init_cqr,
					void *data)
{
	struct dasd_eckd_private *private;
	struct dasd_device *device;

	device = init_cqr->startdev;
	private = (struct dasd_eckd_private *) device->private;
	private->init_cqr_status = dasd_eckd_analysis_evaluation(init_cqr);
	dasd_sfree_request(init_cqr, device);
	dasd_kick_device(device);
}

static int dasd_eckd_start_analysis(struct dasd_block *block)
{
	struct dasd_ccw_req *init_cqr;

	init_cqr = dasd_eckd_analysis_ccw(block->base);
	if (IS_ERR(init_cqr))
		return PTR_ERR(init_cqr);
	init_cqr->callback = dasd_eckd_analysis_callback;
	init_cqr->callback_data = NULL;
	init_cqr->expires = 5*HZ;
	/* first try without ERP, so we can later handle unformatted
	 * devices as special case
	 */
	clear_bit(DASD_CQR_FLAGS_USE_ERP, &init_cqr->flags);
	init_cqr->retries = 0;
	dasd_add_request_head(init_cqr);
	return -EAGAIN;
}

static int dasd_eckd_end_analysis(struct dasd_block *block)
{
	struct dasd_device *device;
	struct dasd_eckd_private *private;
	struct eckd_count *count_area;
	unsigned int sb, blk_per_trk;
	int status, i;
	struct dasd_ccw_req *init_cqr;

	device = block->base;
	private = (struct dasd_eckd_private *) device->private;
	status = private->init_cqr_status;
	private->init_cqr_status = -1;
	if (status == INIT_CQR_ERROR) {
		/* try again, this time with full ERP */
		init_cqr = dasd_eckd_analysis_ccw(device);
		dasd_sleep_on(init_cqr);
		status = dasd_eckd_analysis_evaluation(init_cqr);
		dasd_sfree_request(init_cqr, device);
	}

	if (device->features & DASD_FEATURE_USERAW) {
		block->bp_block = DASD_RAW_BLOCKSIZE;
		blk_per_trk = DASD_RAW_BLOCK_PER_TRACK;
		block->s2b_shift = 3;
		goto raw;
	}

	if (status == INIT_CQR_UNFORMATTED) {
		dev_warn(&device->cdev->dev, "The DASD is not formatted\n");
		return -EMEDIUMTYPE;
	} else if (status == INIT_CQR_ERROR) {
		dev_err(&device->cdev->dev,
			"Detecting the DASD disk layout failed because "
			"of an I/O error\n");
		return -EIO;
	}

	private->uses_cdl = 1;
	/* Check Track 0 for Compatible Disk Layout */
	count_area = NULL;
	for (i = 0; i < 3; i++) {
		if (private->count_area[i].kl != 4 ||
		    private->count_area[i].dl != dasd_eckd_cdl_reclen(i) - 4 ||
		    private->count_area[i].cyl != 0 ||
		    private->count_area[i].head != count_area_head[i] ||
		    private->count_area[i].record != count_area_rec[i]) {
			private->uses_cdl = 0;
			break;
		}
	}
	if (i == 3)
		count_area = &private->count_area[4];

	if (private->uses_cdl == 0) {
		for (i = 0; i < 5; i++) {
			if ((private->count_area[i].kl != 0) ||
			    (private->count_area[i].dl !=
			     private->count_area[0].dl) ||
			    private->count_area[i].cyl !=  0 ||
			    private->count_area[i].head != count_area_head[i] ||
			    private->count_area[i].record != count_area_rec[i])
				break;
		}
		if (i == 5)
			count_area = &private->count_area[0];
	} else {
		if (private->count_area[3].record == 1)
			dev_warn(&device->cdev->dev,
				 "Track 0 has no records following the VTOC\n");
	}

	if (count_area != NULL && count_area->kl == 0) {
		/* we found notthing violating our disk layout */
		if (dasd_check_blocksize(count_area->dl) == 0)
			block->bp_block = count_area->dl;
	}
	if (block->bp_block == 0) {
		dev_warn(&device->cdev->dev,
			 "The disk layout of the DASD is not supported\n");
		return -EMEDIUMTYPE;
	}
	block->s2b_shift = 0;	/* bits to shift 512 to get a block */
	for (sb = 512; sb < block->bp_block; sb = sb << 1)
		block->s2b_shift++;

	blk_per_trk = recs_per_track(&private->rdc_data, 0, block->bp_block);

raw:
	block->blocks = (private->real_cyl *
			  private->rdc_data.trk_per_cyl *
			  blk_per_trk);

	dev_info(&device->cdev->dev,
		 "DASD with %d KB/block, %d KB total size, %d KB/track, "
		 "%s\n", (block->bp_block >> 10),
		 ((private->real_cyl *
		   private->rdc_data.trk_per_cyl *
		   blk_per_trk * (block->bp_block >> 9)) >> 1),
		 ((blk_per_trk * block->bp_block) >> 10),
		 private->uses_cdl ?
		 "compatible disk layout" : "linux disk layout");

	return 0;
}

static int dasd_eckd_do_analysis(struct dasd_block *block)
{
	struct dasd_eckd_private *private;

	private = (struct dasd_eckd_private *) block->base->private;
	if (private->init_cqr_status < 0)
		return dasd_eckd_start_analysis(block);
	else
		return dasd_eckd_end_analysis(block);
}

static int dasd_eckd_basic_to_ready(struct dasd_device *device)
{
	return dasd_alias_add_device(device);
};

static int dasd_eckd_online_to_ready(struct dasd_device *device)
{
	cancel_work_sync(&device->reload_device);
	cancel_work_sync(&device->kick_validate);
	return 0;
};

static int dasd_eckd_ready_to_basic(struct dasd_device *device)
{
	return dasd_alias_remove_device(device);
};

static int
dasd_eckd_fill_geometry(struct dasd_block *block, struct hd_geometry *geo)
{
	struct dasd_eckd_private *private;

	private = (struct dasd_eckd_private *) block->base->private;
	if (dasd_check_blocksize(block->bp_block) == 0) {
		geo->sectors = recs_per_track(&private->rdc_data,
					      0, block->bp_block);
	}
	geo->cylinders = private->rdc_data.no_cyl;
	geo->heads = private->rdc_data.trk_per_cyl;
	return 0;
}

static struct dasd_ccw_req *
dasd_eckd_build_format(struct dasd_device *base,
		       struct format_data_t *fdata)
{
	struct dasd_eckd_private *base_priv;
	struct dasd_eckd_private *start_priv;
	struct dasd_device *startdev;
	struct dasd_ccw_req *fcp;
	struct eckd_count *ect;
	struct ch_t address;
	struct ccw1 *ccw;
	void *data;
	int rpt;
	int cplength, datasize;
	int i, j;
	int intensity = 0;
	int r0_perm;
	int nr_tracks;
	int use_prefix;

	startdev = dasd_alias_get_start_dev(base);
	if (!startdev)
		startdev = base;

	start_priv = (struct dasd_eckd_private *) startdev->private;
	base_priv = (struct dasd_eckd_private *) base->private;

	rpt = recs_per_track(&base_priv->rdc_data, 0, fdata->blksize);

	nr_tracks = fdata->stop_unit - fdata->start_unit + 1;

	/*
	 * fdata->intensity is a bit string that tells us what to do:
	 *   Bit 0: write record zero
	 *   Bit 1: write home address, currently not supported
	 *   Bit 2: invalidate tracks
	 *   Bit 3: use OS/390 compatible disk layout (cdl)
	 *   Bit 4: do not allow storage subsystem to modify record zero
	 * Only some bit combinations do make sense.
	 */
	if (fdata->intensity & 0x10) {
		r0_perm = 0;
		intensity = fdata->intensity & ~0x10;
	} else {
		r0_perm = 1;
		intensity = fdata->intensity;
	}

	use_prefix = base_priv->features.feature[8] & 0x01;

	switch (intensity) {
	case 0x00:	/* Normal format */
	case 0x08:	/* Normal format, use cdl. */
		cplength = 2 + (rpt*nr_tracks);
		if (use_prefix)
			datasize = sizeof(struct PFX_eckd_data) +
				sizeof(struct LO_eckd_data) +
				rpt * nr_tracks * sizeof(struct eckd_count);
		else
			datasize = sizeof(struct DE_eckd_data) +
				sizeof(struct LO_eckd_data) +
				rpt * nr_tracks * sizeof(struct eckd_count);
		break;
	case 0x01:	/* Write record zero and format track. */
	case 0x09:	/* Write record zero and format track, use cdl. */
		cplength = 2 + rpt * nr_tracks;
		if (use_prefix)
			datasize = sizeof(struct PFX_eckd_data) +
				sizeof(struct LO_eckd_data) +
				sizeof(struct eckd_count) +
				rpt * nr_tracks * sizeof(struct eckd_count);
		else
			datasize = sizeof(struct DE_eckd_data) +
				sizeof(struct LO_eckd_data) +
				sizeof(struct eckd_count) +
				rpt * nr_tracks * sizeof(struct eckd_count);
		break;
	case 0x04:	/* Invalidate track. */
	case 0x0c:	/* Invalidate track, use cdl. */
		cplength = 3;
		if (use_prefix)
			datasize = sizeof(struct PFX_eckd_data) +
				sizeof(struct LO_eckd_data) +
				sizeof(struct eckd_count);
		else
			datasize = sizeof(struct DE_eckd_data) +
				sizeof(struct LO_eckd_data) +
				sizeof(struct eckd_count);
		break;
	default:
		dev_warn(&startdev->cdev->dev,
			 "An I/O control call used incorrect flags 0x%x\n",
			 fdata->intensity);
		return ERR_PTR(-EINVAL);
	}
	/* Allocate the format ccw request. */
	fcp = dasd_smalloc_request(DASD_ECKD_MAGIC, cplength,
				   datasize, startdev);
	if (IS_ERR(fcp))
		return fcp;

	start_priv->count++;
	data = fcp->data;
	ccw = fcp->cpaddr;

	switch (intensity & ~0x08) {
	case 0x00: /* Normal format. */
		if (use_prefix) {
			prefix(ccw++, (struct PFX_eckd_data *) data,
			       fdata->start_unit, fdata->stop_unit,
			       DASD_ECKD_CCW_WRITE_CKD, base, startdev);
			/* grant subsystem permission to format R0 */
			if (r0_perm)
				((struct PFX_eckd_data *)data)
					->define_extent.ga_extended |= 0x04;
			data += sizeof(struct PFX_eckd_data);
		} else {
			define_extent(ccw++, (struct DE_eckd_data *) data,
				      fdata->start_unit, fdata->stop_unit,
				      DASD_ECKD_CCW_WRITE_CKD, startdev);
			/* grant subsystem permission to format R0 */
			if (r0_perm)
				((struct DE_eckd_data *) data)
					->ga_extended |= 0x04;
			data += sizeof(struct DE_eckd_data);
		}
		ccw[-1].flags |= CCW_FLAG_CC;
		locate_record(ccw++, (struct LO_eckd_data *) data,
			      fdata->start_unit, 0, rpt*nr_tracks,
			      DASD_ECKD_CCW_WRITE_CKD, base,
			      fdata->blksize);
		data += sizeof(struct LO_eckd_data);
		break;
	case 0x01: /* Write record zero + format track. */
		if (use_prefix) {
			prefix(ccw++, (struct PFX_eckd_data *) data,
			       fdata->start_unit, fdata->stop_unit,
			       DASD_ECKD_CCW_WRITE_RECORD_ZERO,
			       base, startdev);
			data += sizeof(struct PFX_eckd_data);
		} else {
			define_extent(ccw++, (struct DE_eckd_data *) data,
			       fdata->start_unit, fdata->stop_unit,
			       DASD_ECKD_CCW_WRITE_RECORD_ZERO, startdev);
			data += sizeof(struct DE_eckd_data);
		}
		ccw[-1].flags |= CCW_FLAG_CC;
		locate_record(ccw++, (struct LO_eckd_data *) data,
			      fdata->start_unit, 0, rpt * nr_tracks + 1,
			      DASD_ECKD_CCW_WRITE_RECORD_ZERO, base,
			      base->block->bp_block);
		data += sizeof(struct LO_eckd_data);
		break;
	case 0x04: /* Invalidate track. */
		if (use_prefix) {
			prefix(ccw++, (struct PFX_eckd_data *) data,
			       fdata->start_unit, fdata->stop_unit,
			       DASD_ECKD_CCW_WRITE_CKD, base, startdev);
			data += sizeof(struct PFX_eckd_data);
		} else {
			define_extent(ccw++, (struct DE_eckd_data *) data,
			       fdata->start_unit, fdata->stop_unit,
			       DASD_ECKD_CCW_WRITE_CKD, startdev);
			data += sizeof(struct DE_eckd_data);
		}
		ccw[-1].flags |= CCW_FLAG_CC;
		locate_record(ccw++, (struct LO_eckd_data *) data,
			      fdata->start_unit, 0, 1,
			      DASD_ECKD_CCW_WRITE_CKD, base, 8);
		data += sizeof(struct LO_eckd_data);
		break;
	}

	for (j = 0; j < nr_tracks; j++) {
		/* calculate cylinder and head for the current track */
		set_ch_t(&address,
			 (fdata->start_unit + j) /
			 base_priv->rdc_data.trk_per_cyl,
			 (fdata->start_unit + j) %
			 base_priv->rdc_data.trk_per_cyl);
		if (intensity & 0x01) {	/* write record zero */
			ect = (struct eckd_count *) data;
			data += sizeof(struct eckd_count);
			ect->cyl = address.cyl;
			ect->head = address.head;
			ect->record = 0;
			ect->kl = 0;
			ect->dl = 8;
			ccw[-1].flags |= CCW_FLAG_CC;
			ccw->cmd_code = DASD_ECKD_CCW_WRITE_RECORD_ZERO;
			ccw->flags = CCW_FLAG_SLI;
			ccw->count = 8;
			ccw->cda = (__u32)(addr_t) ect;
			ccw++;
		}
		if ((intensity & ~0x08) & 0x04) {	/* erase track */
			ect = (struct eckd_count *) data;
			data += sizeof(struct eckd_count);
			ect->cyl = address.cyl;
			ect->head = address.head;
			ect->record = 1;
			ect->kl = 0;
			ect->dl = 0;
			ccw[-1].flags |= CCW_FLAG_CC;
			ccw->cmd_code = DASD_ECKD_CCW_WRITE_CKD;
			ccw->flags = CCW_FLAG_SLI;
			ccw->count = 8;
			ccw->cda = (__u32)(addr_t) ect;
		} else {		/* write remaining records */
			for (i = 0; i < rpt; i++) {
				ect = (struct eckd_count *) data;
				data += sizeof(struct eckd_count);
				ect->cyl = address.cyl;
				ect->head = address.head;
				ect->record = i + 1;
				ect->kl = 0;
				ect->dl = fdata->blksize;
				/*
				 * Check for special tracks 0-1
				 * when formatting CDL
				 */
				if ((intensity & 0x08) &&
				    fdata->start_unit == 0) {
					if (i < 3) {
						ect->kl = 4;
						ect->dl = sizes_trk0[i] - 4;
					}
				}
				if ((intensity & 0x08) &&
				    fdata->start_unit == 1) {
					ect->kl = 44;
					ect->dl = LABEL_SIZE - 44;
				}
				ccw[-1].flags |= CCW_FLAG_CC;
				if (i != 0 || j == 0)
					ccw->cmd_code =
						DASD_ECKD_CCW_WRITE_CKD;
				else
					ccw->cmd_code =
						DASD_ECKD_CCW_WRITE_CKD_MT;
				ccw->flags = CCW_FLAG_SLI;
				ccw->count = 8;
					ccw->cda = (__u32)(addr_t) ect;
					ccw++;
			}
		}
	}

	fcp->startdev = startdev;
	fcp->memdev = startdev;
	fcp->retries = 256;
	fcp->expires = startdev->default_expires * HZ;
	fcp->buildclk = get_tod_clock();
	fcp->status = DASD_CQR_FILLED;

	return fcp;
}

static int
dasd_eckd_format_device(struct dasd_device *base,
			struct format_data_t *fdata)
{
	struct dasd_ccw_req *cqr, *n;
	struct dasd_block *block;
	struct dasd_eckd_private *private;
	struct list_head format_queue;
	struct dasd_device *device;
	int old_stop, format_step;
	int step, rc = 0;

	block = base->block;
	private = (struct dasd_eckd_private *) base->private;

	/* Sanity checks. */
	if (fdata->start_unit >=
	    (private->real_cyl * private->rdc_data.trk_per_cyl)) {
		dev_warn(&base->cdev->dev,
			 "Start track number %u used in formatting is too big\n",
			 fdata->start_unit);
		return -EINVAL;
	}
	if (fdata->stop_unit >=
	    (private->real_cyl * private->rdc_data.trk_per_cyl)) {
		dev_warn(&base->cdev->dev,
			 "Stop track number %u used in formatting is too big\n",
			 fdata->stop_unit);
		return -EINVAL;
	}
	if (fdata->start_unit > fdata->stop_unit) {
		dev_warn(&base->cdev->dev,
			 "Start track %u used in formatting exceeds end track\n",
			 fdata->start_unit);
		return -EINVAL;
	}
	if (dasd_check_blocksize(fdata->blksize) != 0) {
		dev_warn(&base->cdev->dev,
			 "The DASD cannot be formatted with block size %u\n",
			 fdata->blksize);
		return -EINVAL;
	}

	INIT_LIST_HEAD(&format_queue);
	old_stop = fdata->stop_unit;

	while (fdata->start_unit <= 1) {
		fdata->stop_unit = fdata->start_unit;
		cqr = dasd_eckd_build_format(base, fdata);
		list_add(&cqr->blocklist, &format_queue);

		fdata->stop_unit = old_stop;
		fdata->start_unit++;

		if (fdata->start_unit > fdata->stop_unit)
			goto sleep;
	}

retry:
	format_step = 255 / recs_per_track(&private->rdc_data, 0,
					   fdata->blksize);
	while (fdata->start_unit <= old_stop) {
		step = fdata->stop_unit - fdata->start_unit + 1;
		if (step > format_step)
			fdata->stop_unit = fdata->start_unit + format_step - 1;

		cqr = dasd_eckd_build_format(base, fdata);
		if (IS_ERR(cqr)) {
			if (PTR_ERR(cqr) == -ENOMEM) {
				/*
				 * not enough memory available
				 * go to out and start requests
				 * retry after first requests were finished
				 */
				fdata->stop_unit = old_stop;
				goto sleep;
			} else
				return PTR_ERR(cqr);
		}
		list_add(&cqr->blocklist, &format_queue);

		fdata->start_unit = fdata->stop_unit + 1;
		fdata->stop_unit = old_stop;
	}

sleep:
	dasd_sleep_on_queue(&format_queue);

	list_for_each_entry_safe(cqr, n, &format_queue, blocklist) {
		device = cqr->startdev;
		private = (struct dasd_eckd_private *) device->private;
		if (cqr->status == DASD_CQR_FAILED)
			rc = -EIO;
		list_del_init(&cqr->blocklist);
		dasd_sfree_request(cqr, device);
		private->count--;
	}

	/*
	 * in case of ENOMEM we need to retry after
	 * first requests are finished
	 */
	if (fdata->start_unit <= fdata->stop_unit)
		goto retry;

	return rc;
}

static void dasd_eckd_handle_terminated_request(struct dasd_ccw_req *cqr)
{
	if (cqr->retries < 0) {
		cqr->status = DASD_CQR_FAILED;
		return;
	}
	cqr->status = DASD_CQR_FILLED;
	if (cqr->block && (cqr->startdev != cqr->block->base)) {
		dasd_eckd_reset_ccw_to_base_io(cqr);
		cqr->startdev = cqr->block->base;
		cqr->lpm = cqr->block->base->path_data.opm;
	}
};

static dasd_erp_fn_t
dasd_eckd_erp_action(struct dasd_ccw_req * cqr)
{
	struct dasd_device *device = (struct dasd_device *) cqr->startdev;
	struct ccw_device *cdev = device->cdev;

	switch (cdev->id.cu_type) {
	case 0x3990:
	case 0x2105:
	case 0x2107:
	case 0x1750:
		return dasd_3990_erp_action;
	case 0x9343:
	case 0x3880:
	default:
		return dasd_default_erp_action;
	}
}

static dasd_erp_fn_t
dasd_eckd_erp_postaction(struct dasd_ccw_req * cqr)
{
	return dasd_default_erp_postaction;
}

static void dasd_eckd_check_for_device_change(struct dasd_device *device,
					      struct dasd_ccw_req *cqr,
					      struct irb *irb)
{
	char mask;
	char *sense = NULL;
	struct dasd_eckd_private *private;

	private = (struct dasd_eckd_private *) device->private;
	/* first of all check for state change pending interrupt */
	mask = DEV_STAT_ATTENTION | DEV_STAT_DEV_END | DEV_STAT_UNIT_EXCEP;
	if ((scsw_dstat(&irb->scsw) & mask) == mask) {
		/*
		 * for alias only, not in offline processing
		 * and only if not suspended
		 */
		if (!device->block && private->lcu &&
		    device->state == DASD_STATE_ONLINE &&
		    !test_bit(DASD_FLAG_OFFLINE, &device->flags) &&
		    !test_bit(DASD_FLAG_SUSPENDED, &device->flags)) {
			/*
			 * the state change could be caused by an alias
			 * reassignment remove device from alias handling
			 * to prevent new requests from being scheduled on
			 * the wrong alias device
			 */
			dasd_alias_remove_device(device);

			/* schedule worker to reload device */
			dasd_reload_device(device);
		}
		dasd_generic_handle_state_change(device);
		return;
	}

	sense = dasd_get_sense(irb);
	if (!sense)
		return;

	/* summary unit check */
	if ((sense[27] & DASD_SENSE_BIT_0) && (sense[7] == 0x0D) &&
	    (scsw_dstat(&irb->scsw) & DEV_STAT_UNIT_CHECK)) {
		dasd_alias_handle_summary_unit_check(device, irb);
		return;
	}

	/* service information message SIM */
	if (!cqr && !(sense[27] & DASD_SENSE_BIT_0) &&
	    ((sense[6] & DASD_SIM_SENSE) == DASD_SIM_SENSE)) {
		dasd_3990_erp_handle_sim(device, sense);
		return;
	}

	/* loss of device reservation is handled via base devices only
	 * as alias devices may be used with several bases
	 */
	if (device->block && (sense[27] & DASD_SENSE_BIT_0) &&
	    (sense[7] == 0x3F) &&
	    (scsw_dstat(&irb->scsw) & DEV_STAT_UNIT_CHECK) &&
	    test_bit(DASD_FLAG_IS_RESERVED, &device->flags)) {
		if (device->features & DASD_FEATURE_FAILONSLCK)
			set_bit(DASD_FLAG_LOCK_STOLEN, &device->flags);
		clear_bit(DASD_FLAG_IS_RESERVED, &device->flags);
		dev_err(&device->cdev->dev,
			"The device reservation was lost\n");
	}
}

static struct dasd_ccw_req *dasd_eckd_build_cp_cmd_single(
					       struct dasd_device *startdev,
					       struct dasd_block *block,
					       struct request *req,
					       sector_t first_rec,
					       sector_t last_rec,
					       sector_t first_trk,
					       sector_t last_trk,
					       unsigned int first_offs,
					       unsigned int last_offs,
					       unsigned int blk_per_trk,
					       unsigned int blksize)
{
	struct dasd_eckd_private *private;
	unsigned long *idaws;
	struct LO_eckd_data *LO_data;
	struct dasd_ccw_req *cqr;
	struct ccw1 *ccw;
	struct req_iterator iter;
	struct bio_vec *bv;
	char *dst;
	unsigned int off;
	int count, cidaw, cplength, datasize;
	sector_t recid;
	unsigned char cmd, rcmd;
	int use_prefix;
	struct dasd_device *basedev;

	basedev = block->base;
	private = (struct dasd_eckd_private *) basedev->private;
	if (rq_data_dir(req) == READ)
		cmd = DASD_ECKD_CCW_READ_MT;
	else if (rq_data_dir(req) == WRITE)
		cmd = DASD_ECKD_CCW_WRITE_MT;
	else
		return ERR_PTR(-EINVAL);

	/* Check struct bio and count the number of blocks for the request. */
	count = 0;
	cidaw = 0;
	rq_for_each_segment(bv, req, iter) {
		if (bv->bv_len & (blksize - 1))
			/* Eckd can only do full blocks. */
			return ERR_PTR(-EINVAL);
		count += bv->bv_len >> (block->s2b_shift + 9);
#if defined(CONFIG_64BIT)
		if (idal_is_needed (page_address(bv->bv_page), bv->bv_len))
			cidaw += bv->bv_len >> (block->s2b_shift + 9);
#endif
	}
	/* Paranoia. */
	if (count != last_rec - first_rec + 1)
		return ERR_PTR(-EINVAL);

	/* use the prefix command if available */
	use_prefix = private->features.feature[8] & 0x01;
	if (use_prefix) {
		/* 1x prefix + number of blocks */
		cplength = 2 + count;
		/* 1x prefix + cidaws*sizeof(long) */
		datasize = sizeof(struct PFX_eckd_data) +
			sizeof(struct LO_eckd_data) +
			cidaw * sizeof(unsigned long);
	} else {
		/* 1x define extent + 1x locate record + number of blocks */
		cplength = 2 + count;
		/* 1x define extent + 1x locate record + cidaws*sizeof(long) */
		datasize = sizeof(struct DE_eckd_data) +
			sizeof(struct LO_eckd_data) +
			cidaw * sizeof(unsigned long);
	}
	/* Find out the number of additional locate record ccws for cdl. */
	if (private->uses_cdl && first_rec < 2*blk_per_trk) {
		if (last_rec >= 2*blk_per_trk)
			count = 2*blk_per_trk - first_rec;
		cplength += count;
		datasize += count*sizeof(struct LO_eckd_data);
	}
	/* Allocate the ccw request. */
	cqr = dasd_smalloc_request(DASD_ECKD_MAGIC, cplength, datasize,
				   startdev);
	if (IS_ERR(cqr))
		return cqr;
	ccw = cqr->cpaddr;
	/* First ccw is define extent or prefix. */
	if (use_prefix) {
		if (prefix(ccw++, cqr->data, first_trk,
			   last_trk, cmd, basedev, startdev) == -EAGAIN) {
			/* Clock not in sync and XRC is enabled.
			 * Try again later.
			 */
			dasd_sfree_request(cqr, startdev);
			return ERR_PTR(-EAGAIN);
		}
		idaws = (unsigned long *) (cqr->data +
					   sizeof(struct PFX_eckd_data));
	} else {
		if (define_extent(ccw++, cqr->data, first_trk,
				  last_trk, cmd, basedev) == -EAGAIN) {
			/* Clock not in sync and XRC is enabled.
			 * Try again later.
			 */
			dasd_sfree_request(cqr, startdev);
			return ERR_PTR(-EAGAIN);
		}
		idaws = (unsigned long *) (cqr->data +
					   sizeof(struct DE_eckd_data));
	}
	/* Build locate_record+read/write/ccws. */
	LO_data = (struct LO_eckd_data *) (idaws + cidaw);
	recid = first_rec;
	if (private->uses_cdl == 0 || recid > 2*blk_per_trk) {
		/* Only standard blocks so there is just one locate record. */
		ccw[-1].flags |= CCW_FLAG_CC;
		locate_record(ccw++, LO_data++, first_trk, first_offs + 1,
			      last_rec - recid + 1, cmd, basedev, blksize);
	}
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
			sector_t trkid = recid;
			unsigned int recoffs = sector_div(trkid, blk_per_trk);
			rcmd = cmd;
			count = blksize;
			/* Locate record for cdl special block ? */
			if (private->uses_cdl && recid < 2*blk_per_trk) {
				if (dasd_eckd_cdl_special(blk_per_trk, recid)){
					rcmd |= 0x8;
					count = dasd_eckd_cdl_reclen(recid);
					if (count < blksize &&
					    rq_data_dir(req) == READ)
						memset(dst + count, 0xe5,
						       blksize - count);
				}
				ccw[-1].flags |= CCW_FLAG_CC;
				locate_record(ccw++, LO_data++,
					      trkid, recoffs + 1,
					      1, rcmd, basedev, count);
			}
			/* Locate record for standard blocks ? */
			if (private->uses_cdl && recid == 2*blk_per_trk) {
				ccw[-1].flags |= CCW_FLAG_CC;
				locate_record(ccw++, LO_data++,
					      trkid, recoffs + 1,
					      last_rec - recid + 1,
					      cmd, basedev, count);
			}
			/* Read/write ccw. */
			ccw[-1].flags |= CCW_FLAG_CC;
			ccw->cmd_code = rcmd;
			ccw->count = count;
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
	cqr->startdev = startdev;
	cqr->memdev = startdev;
	cqr->block = block;
	cqr->expires = startdev->default_expires * HZ;	/* default 5 minutes */
	cqr->lpm = startdev->path_data.ppm;
	cqr->retries = startdev->default_retries;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;
	return cqr;
}

static struct dasd_ccw_req *dasd_eckd_build_cp_cmd_track(
					       struct dasd_device *startdev,
					       struct dasd_block *block,
					       struct request *req,
					       sector_t first_rec,
					       sector_t last_rec,
					       sector_t first_trk,
					       sector_t last_trk,
					       unsigned int first_offs,
					       unsigned int last_offs,
					       unsigned int blk_per_trk,
					       unsigned int blksize)
{
	unsigned long *idaws;
	struct dasd_ccw_req *cqr;
	struct ccw1 *ccw;
	struct req_iterator iter;
	struct bio_vec *bv;
	char *dst, *idaw_dst;
	unsigned int cidaw, cplength, datasize;
	unsigned int tlf;
	sector_t recid;
	unsigned char cmd;
	struct dasd_device *basedev;
	unsigned int trkcount, count, count_to_trk_end;
	unsigned int idaw_len, seg_len, part_len, len_to_track_end;
	unsigned char new_track, end_idaw;
	sector_t trkid;
	unsigned int recoffs;

	basedev = block->base;
	if (rq_data_dir(req) == READ)
		cmd = DASD_ECKD_CCW_READ_TRACK_DATA;
	else if (rq_data_dir(req) == WRITE)
		cmd = DASD_ECKD_CCW_WRITE_TRACK_DATA;
	else
		return ERR_PTR(-EINVAL);

	/* Track based I/O needs IDAWs for each page, and not just for
	 * 64 bit addresses. We need additional idals for pages
	 * that get filled from two tracks, so we use the number
	 * of records as upper limit.
	 */
	cidaw = last_rec - first_rec + 1;
	trkcount = last_trk - first_trk + 1;

	/* 1x prefix + one read/write ccw per track */
	cplength = 1 + trkcount;

	/* on 31-bit we need space for two 32 bit addresses per page
	 * on 64-bit one 64 bit address
	 */
	datasize = sizeof(struct PFX_eckd_data) +
		cidaw * sizeof(unsigned long long);

	/* Allocate the ccw request. */
	cqr = dasd_smalloc_request(DASD_ECKD_MAGIC, cplength, datasize,
				   startdev);
	if (IS_ERR(cqr))
		return cqr;
	ccw = cqr->cpaddr;
	/* transfer length factor: how many bytes to read from the last track */
	if (first_trk == last_trk)
		tlf = last_offs - first_offs + 1;
	else
		tlf = last_offs + 1;
	tlf *= blksize;

	if (prefix_LRE(ccw++, cqr->data, first_trk,
		       last_trk, cmd, basedev, startdev,
		       1 /* format */, first_offs + 1,
		       trkcount, blksize,
		       tlf) == -EAGAIN) {
		/* Clock not in sync and XRC is enabled.
		 * Try again later.
		 */
		dasd_sfree_request(cqr, startdev);
		return ERR_PTR(-EAGAIN);
	}

	/*
	 * The translation of request into ccw programs must meet the
	 * following conditions:
	 * - all idaws but the first and the last must address full pages
	 *   (or 2K blocks on 31-bit)
	 * - the scope of a ccw and it's idal ends with the track boundaries
	 */
	idaws = (unsigned long *) (cqr->data + sizeof(struct PFX_eckd_data));
	recid = first_rec;
	new_track = 1;
	end_idaw = 0;
	len_to_track_end = 0;
	idaw_dst = NULL;
	idaw_len = 0;
	rq_for_each_segment(bv, req, iter) {
		dst = page_address(bv->bv_page) + bv->bv_offset;
		seg_len = bv->bv_len;
		while (seg_len) {
			if (new_track) {
				trkid = recid;
				recoffs = sector_div(trkid, blk_per_trk);
				count_to_trk_end = blk_per_trk - recoffs;
				count = min((last_rec - recid + 1),
					    (sector_t)count_to_trk_end);
				len_to_track_end = count * blksize;
				ccw[-1].flags |= CCW_FLAG_CC;
				ccw->cmd_code = cmd;
				ccw->count = len_to_track_end;
				ccw->cda = (__u32)(addr_t)idaws;
				ccw->flags = CCW_FLAG_IDA;
				ccw++;
				recid += count;
				new_track = 0;
				/* first idaw for a ccw may start anywhere */
				if (!idaw_dst)
					idaw_dst = dst;
			}
			/* If we start a new idaw, we must make sure that it
			 * starts on an IDA_BLOCK_SIZE boundary.
			 * If we continue an idaw, we must make sure that the
			 * current segment begins where the so far accumulated
			 * idaw ends
			 */
			if (!idaw_dst) {
				if (__pa(dst) & (IDA_BLOCK_SIZE-1)) {
					dasd_sfree_request(cqr, startdev);
					return ERR_PTR(-ERANGE);
				} else
					idaw_dst = dst;
			}
			if ((idaw_dst + idaw_len) != dst) {
				dasd_sfree_request(cqr, startdev);
				return ERR_PTR(-ERANGE);
			}
			part_len = min(seg_len, len_to_track_end);
			seg_len -= part_len;
			dst += part_len;
			idaw_len += part_len;
			len_to_track_end -= part_len;
			/* collected memory area ends on an IDA_BLOCK border,
			 * -> create an idaw
			 * idal_create_words will handle cases where idaw_len
			 * is larger then IDA_BLOCK_SIZE
			 */
			if (!(__pa(idaw_dst + idaw_len) & (IDA_BLOCK_SIZE-1)))
				end_idaw = 1;
			/* We also need to end the idaw at track end */
			if (!len_to_track_end) {
				new_track = 1;
				end_idaw = 1;
			}
			if (end_idaw) {
				idaws = idal_create_words(idaws, idaw_dst,
							  idaw_len);
				idaw_dst = NULL;
				idaw_len = 0;
				end_idaw = 0;
			}
		}
	}

	if (blk_noretry_request(req) ||
	    block->base->features & DASD_FEATURE_FAILFAST)
		set_bit(DASD_CQR_FLAGS_FAILFAST, &cqr->flags);
	cqr->startdev = startdev;
	cqr->memdev = startdev;
	cqr->block = block;
	cqr->expires = startdev->default_expires * HZ;	/* default 5 minutes */
	cqr->lpm = startdev->path_data.ppm;
	cqr->retries = startdev->default_retries;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;
	return cqr;
}

static int prepare_itcw(struct itcw *itcw,
			unsigned int trk, unsigned int totrk, int cmd,
			struct dasd_device *basedev,
			struct dasd_device *startdev,
			unsigned int rec_on_trk, int count,
			unsigned int blksize,
			unsigned int total_data_size,
			unsigned int tlf,
			unsigned int blk_per_trk)
{
	struct PFX_eckd_data pfxdata;
	struct dasd_eckd_private *basepriv, *startpriv;
	struct DE_eckd_data *dedata;
	struct LRE_eckd_data *lredata;
	struct dcw *dcw;

	u32 begcyl, endcyl;
	u16 heads, beghead, endhead;
	u8 pfx_cmd;

	int rc = 0;
	int sector = 0;
	int dn, d;


	/* setup prefix data */
	basepriv = (struct dasd_eckd_private *) basedev->private;
	startpriv = (struct dasd_eckd_private *) startdev->private;
	dedata = &pfxdata.define_extent;
	lredata = &pfxdata.locate_record;

	memset(&pfxdata, 0, sizeof(pfxdata));
	pfxdata.format = 1; /* PFX with LRE */
	pfxdata.base_address = basepriv->ned->unit_addr;
	pfxdata.base_lss = basepriv->ned->ID;
	pfxdata.validity.define_extent = 1;

	/* private uid is kept up to date, conf_data may be outdated */
	if (startpriv->uid.type != UA_BASE_DEVICE) {
		pfxdata.validity.verify_base = 1;
		if (startpriv->uid.type == UA_HYPER_PAV_ALIAS)
			pfxdata.validity.hyper_pav = 1;
	}

	switch (cmd) {
	case DASD_ECKD_CCW_READ_TRACK_DATA:
		dedata->mask.perm = 0x1;
		dedata->attributes.operation = basepriv->attrib.operation;
		dedata->blk_size = blksize;
		dedata->ga_extended |= 0x42;
		lredata->operation.orientation = 0x0;
		lredata->operation.operation = 0x0C;
		lredata->auxiliary.check_bytes = 0x01;
		pfx_cmd = DASD_ECKD_CCW_PFX_READ;
		break;
	case DASD_ECKD_CCW_WRITE_TRACK_DATA:
		dedata->mask.perm = 0x02;
		dedata->attributes.operation = basepriv->attrib.operation;
		dedata->blk_size = blksize;
		rc = check_XRC_on_prefix(&pfxdata, basedev);
		dedata->ga_extended |= 0x42;
		lredata->operation.orientation = 0x0;
		lredata->operation.operation = 0x3F;
		lredata->extended_operation = 0x23;
		lredata->auxiliary.check_bytes = 0x2;
		pfx_cmd = DASD_ECKD_CCW_PFX;
		break;
	default:
		DBF_DEV_EVENT(DBF_ERR, basedev,
			      "prepare itcw, unknown opcode 0x%x", cmd);
		BUG();
		break;
	}
	if (rc)
		return rc;

	dedata->attributes.mode = 0x3;	/* ECKD */

	heads = basepriv->rdc_data.trk_per_cyl;
	begcyl = trk / heads;
	beghead = trk % heads;
	endcyl = totrk / heads;
	endhead = totrk % heads;

	/* check for sequential prestage - enhance cylinder range */
	if (dedata->attributes.operation == DASD_SEQ_PRESTAGE ||
	    dedata->attributes.operation == DASD_SEQ_ACCESS) {

		if (endcyl + basepriv->attrib.nr_cyl < basepriv->real_cyl)
			endcyl += basepriv->attrib.nr_cyl;
		else
			endcyl = (basepriv->real_cyl - 1);
	}

	set_ch_t(&dedata->beg_ext, begcyl, beghead);
	set_ch_t(&dedata->end_ext, endcyl, endhead);

	dedata->ep_format = 0x20; /* records per track is valid */
	dedata->ep_rec_per_track = blk_per_trk;

	if (rec_on_trk) {
		switch (basepriv->rdc_data.dev_type) {
		case 0x3390:
			dn = ceil_quot(blksize + 6, 232);
			d = 9 + ceil_quot(blksize + 6 * (dn + 1), 34);
			sector = (49 + (rec_on_trk - 1) * (10 + d)) / 8;
			break;
		case 0x3380:
			d = 7 + ceil_quot(blksize + 12, 32);
			sector = (39 + (rec_on_trk - 1) * (8 + d)) / 7;
			break;
		}
	}

	lredata->auxiliary.length_valid = 1;
	lredata->auxiliary.length_scope = 1;
	lredata->auxiliary.imbedded_ccw_valid = 1;
	lredata->length = tlf;
	lredata->imbedded_ccw = cmd;
	lredata->count = count;
	lredata->sector = sector;
	set_ch_t(&lredata->seek_addr, begcyl, beghead);
	lredata->search_arg.cyl = lredata->seek_addr.cyl;
	lredata->search_arg.head = lredata->seek_addr.head;
	lredata->search_arg.record = rec_on_trk;

	dcw = itcw_add_dcw(itcw, pfx_cmd, 0,
		     &pfxdata, sizeof(pfxdata), total_data_size);
	return PTR_RET(dcw);
}

static struct dasd_ccw_req *dasd_eckd_build_cp_tpm_track(
					       struct dasd_device *startdev,
					       struct dasd_block *block,
					       struct request *req,
					       sector_t first_rec,
					       sector_t last_rec,
					       sector_t first_trk,
					       sector_t last_trk,
					       unsigned int first_offs,
					       unsigned int last_offs,
					       unsigned int blk_per_trk,
					       unsigned int blksize)
{
	struct dasd_ccw_req *cqr;
	struct req_iterator iter;
	struct bio_vec *bv;
	char *dst;
	unsigned int trkcount, ctidaw;
	unsigned char cmd;
	struct dasd_device *basedev;
	unsigned int tlf;
	struct itcw *itcw;
	struct tidaw *last_tidaw = NULL;
	int itcw_op;
	size_t itcw_size;
	u8 tidaw_flags;
	unsigned int seg_len, part_len, len_to_track_end;
	unsigned char new_track;
	sector_t recid, trkid;
	unsigned int offs;
	unsigned int count, count_to_trk_end;
	int ret;

	basedev = block->base;
	if (rq_data_dir(req) == READ) {
		cmd = DASD_ECKD_CCW_READ_TRACK_DATA;
		itcw_op = ITCW_OP_READ;
	} else if (rq_data_dir(req) == WRITE) {
		cmd = DASD_ECKD_CCW_WRITE_TRACK_DATA;
		itcw_op = ITCW_OP_WRITE;
	} else
		return ERR_PTR(-EINVAL);

	/* trackbased I/O needs address all memory via TIDAWs,
	 * not just for 64 bit addresses. This allows us to map
	 * each segment directly to one tidaw.
	 * In the case of write requests, additional tidaws may
	 * be needed when a segment crosses a track boundary.
	 */
	trkcount = last_trk - first_trk + 1;
	ctidaw = 0;
	rq_for_each_segment(bv, req, iter) {
		++ctidaw;
	}
	if (rq_data_dir(req) == WRITE)
		ctidaw += (last_trk - first_trk);

	/* Allocate the ccw request. */
	itcw_size = itcw_calc_size(0, ctidaw, 0);
	cqr = dasd_smalloc_request(DASD_ECKD_MAGIC, 0, itcw_size, startdev);
	if (IS_ERR(cqr))
		return cqr;

	/* transfer length factor: how many bytes to read from the last track */
	if (first_trk == last_trk)
		tlf = last_offs - first_offs + 1;
	else
		tlf = last_offs + 1;
	tlf *= blksize;

	itcw = itcw_init(cqr->data, itcw_size, itcw_op, 0, ctidaw, 0);
	if (IS_ERR(itcw)) {
		ret = -EINVAL;
		goto out_error;
	}
	cqr->cpaddr = itcw_get_tcw(itcw);
	if (prepare_itcw(itcw, first_trk, last_trk,
			 cmd, basedev, startdev,
			 first_offs + 1,
			 trkcount, blksize,
			 (last_rec - first_rec + 1) * blksize,
			 tlf, blk_per_trk) == -EAGAIN) {
		/* Clock not in sync and XRC is enabled.
		 * Try again later.
		 */
		ret = -EAGAIN;
		goto out_error;
	}
	len_to_track_end = 0;
	/*
	 * A tidaw can address 4k of memory, but must not cross page boundaries
	 * We can let the block layer handle this by setting
	 * blk_queue_segment_boundary to page boundaries and
	 * blk_max_segment_size to page size when setting up the request queue.
	 * For write requests, a TIDAW must not cross track boundaries, because
	 * we have to set the CBC flag on the last tidaw for each track.
	 */
	if (rq_data_dir(req) == WRITE) {
		new_track = 1;
		recid = first_rec;
		rq_for_each_segment(bv, req, iter) {
			dst = page_address(bv->bv_page) + bv->bv_offset;
			seg_len = bv->bv_len;
			while (seg_len) {
				if (new_track) {
					trkid = recid;
					offs = sector_div(trkid, blk_per_trk);
					count_to_trk_end = blk_per_trk - offs;
					count = min((last_rec - recid + 1),
						    (sector_t)count_to_trk_end);
					len_to_track_end = count * blksize;
					recid += count;
					new_track = 0;
				}
				part_len = min(seg_len, len_to_track_end);
				seg_len -= part_len;
				len_to_track_end -= part_len;
				/* We need to end the tidaw at track end */
				if (!len_to_track_end) {
					new_track = 1;
					tidaw_flags = TIDAW_FLAGS_INSERT_CBC;
				} else
					tidaw_flags = 0;
				last_tidaw = itcw_add_tidaw(itcw, tidaw_flags,
							    dst, part_len);
				if (IS_ERR(last_tidaw)) {
					ret = -EINVAL;
					goto out_error;
				}
				dst += part_len;
			}
		}
	} else {
		rq_for_each_segment(bv, req, iter) {
			dst = page_address(bv->bv_page) + bv->bv_offset;
			last_tidaw = itcw_add_tidaw(itcw, 0x00,
						    dst, bv->bv_len);
			if (IS_ERR(last_tidaw)) {
				ret = -EINVAL;
				goto out_error;
			}
		}
	}
	last_tidaw->flags |= TIDAW_FLAGS_LAST;
	last_tidaw->flags &= ~TIDAW_FLAGS_INSERT_CBC;
	itcw_finalize(itcw);

	if (blk_noretry_request(req) ||
	    block->base->features & DASD_FEATURE_FAILFAST)
		set_bit(DASD_CQR_FLAGS_FAILFAST, &cqr->flags);
	cqr->cpmode = 1;
	cqr->startdev = startdev;
	cqr->memdev = startdev;
	cqr->block = block;
	cqr->expires = startdev->default_expires * HZ;	/* default 5 minutes */
	cqr->lpm = startdev->path_data.ppm;
	cqr->retries = startdev->default_retries;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;
	return cqr;
out_error:
	dasd_sfree_request(cqr, startdev);
	return ERR_PTR(ret);
}

static struct dasd_ccw_req *dasd_eckd_build_cp(struct dasd_device *startdev,
					       struct dasd_block *block,
					       struct request *req)
{
	int cmdrtd, cmdwtd;
	int use_prefix;
	int fcx_multitrack;
	struct dasd_eckd_private *private;
	struct dasd_device *basedev;
	sector_t first_rec, last_rec;
	sector_t first_trk, last_trk;
	unsigned int first_offs, last_offs;
	unsigned int blk_per_trk, blksize;
	int cdlspecial;
	unsigned int data_size;
	struct dasd_ccw_req *cqr;

	basedev = block->base;
	private = (struct dasd_eckd_private *) basedev->private;

	/* Calculate number of blocks/records per track. */
	blksize = block->bp_block;
	blk_per_trk = recs_per_track(&private->rdc_data, 0, blksize);
	if (blk_per_trk == 0)
		return ERR_PTR(-EINVAL);
	/* Calculate record id of first and last block. */
	first_rec = first_trk = blk_rq_pos(req) >> block->s2b_shift;
	first_offs = sector_div(first_trk, blk_per_trk);
	last_rec = last_trk =
		(blk_rq_pos(req) + blk_rq_sectors(req) - 1) >> block->s2b_shift;
	last_offs = sector_div(last_trk, blk_per_trk);
	cdlspecial = (private->uses_cdl && first_rec < 2*blk_per_trk);

	fcx_multitrack = private->features.feature[40] & 0x20;
	data_size = blk_rq_bytes(req);
	if (data_size % blksize)
		return ERR_PTR(-EINVAL);
	/* tpm write request add CBC data on each track boundary */
	if (rq_data_dir(req) == WRITE)
		data_size += (last_trk - first_trk) * 4;

	/* is read track data and write track data in command mode supported? */
	cmdrtd = private->features.feature[9] & 0x20;
	cmdwtd = private->features.feature[12] & 0x40;
	use_prefix = private->features.feature[8] & 0x01;

	cqr = NULL;
	if (cdlspecial || dasd_page_cache) {
		/* do nothing, just fall through to the cmd mode single case */
	} else if ((data_size <= private->fcx_max_data)
		   && (fcx_multitrack || (first_trk == last_trk))) {
		cqr = dasd_eckd_build_cp_tpm_track(startdev, block, req,
						    first_rec, last_rec,
						    first_trk, last_trk,
						    first_offs, last_offs,
						    blk_per_trk, blksize);
		if (IS_ERR(cqr) && (PTR_ERR(cqr) != -EAGAIN) &&
		    (PTR_ERR(cqr) != -ENOMEM))
			cqr = NULL;
	} else if (use_prefix &&
		   (((rq_data_dir(req) == READ) && cmdrtd) ||
		    ((rq_data_dir(req) == WRITE) && cmdwtd))) {
		cqr = dasd_eckd_build_cp_cmd_track(startdev, block, req,
						   first_rec, last_rec,
						   first_trk, last_trk,
						   first_offs, last_offs,
						   blk_per_trk, blksize);
		if (IS_ERR(cqr) && (PTR_ERR(cqr) != -EAGAIN) &&
		    (PTR_ERR(cqr) != -ENOMEM))
			cqr = NULL;
	}
	if (!cqr)
		cqr = dasd_eckd_build_cp_cmd_single(startdev, block, req,
						    first_rec, last_rec,
						    first_trk, last_trk,
						    first_offs, last_offs,
						    blk_per_trk, blksize);
	return cqr;
}

static struct dasd_ccw_req *dasd_raw_build_cp(struct dasd_device *startdev,
					       struct dasd_block *block,
					       struct request *req)
{
	unsigned long *idaws;
	struct dasd_device *basedev;
	struct dasd_ccw_req *cqr;
	struct ccw1 *ccw;
	struct req_iterator iter;
	struct bio_vec *bv;
	char *dst;
	unsigned char cmd;
	unsigned int trkcount;
	unsigned int seg_len, len_to_track_end;
	unsigned int first_offs;
	unsigned int cidaw, cplength, datasize;
	sector_t first_trk, last_trk, sectors;
	sector_t start_padding_sectors, end_sector_offset, end_padding_sectors;
	unsigned int pfx_datasize;

	/*
	 * raw track access needs to be mutiple of 64k and on 64k boundary
	 * For read requests we can fix an incorrect alignment by padding
	 * the request with dummy pages.
	 */
	start_padding_sectors = blk_rq_pos(req) % DASD_RAW_SECTORS_PER_TRACK;
	end_sector_offset = (blk_rq_pos(req) + blk_rq_sectors(req)) %
		DASD_RAW_SECTORS_PER_TRACK;
	end_padding_sectors = (DASD_RAW_SECTORS_PER_TRACK - end_sector_offset) %
		DASD_RAW_SECTORS_PER_TRACK;
	basedev = block->base;
	if ((start_padding_sectors || end_padding_sectors) &&
	    (rq_data_dir(req) == WRITE)) {
		DBF_DEV_EVENT(DBF_ERR, basedev,
			      "raw write not track aligned (%lu,%lu) req %p",
			      start_padding_sectors, end_padding_sectors, req);
		cqr = ERR_PTR(-EINVAL);
		goto out;
	}

	first_trk = blk_rq_pos(req) / DASD_RAW_SECTORS_PER_TRACK;
	last_trk = (blk_rq_pos(req) + blk_rq_sectors(req) - 1) /
		DASD_RAW_SECTORS_PER_TRACK;
	trkcount = last_trk - first_trk + 1;
	first_offs = 0;

	if (rq_data_dir(req) == READ)
		cmd = DASD_ECKD_CCW_READ_TRACK;
	else if (rq_data_dir(req) == WRITE)
		cmd = DASD_ECKD_CCW_WRITE_FULL_TRACK;
	else {
		cqr = ERR_PTR(-EINVAL);
		goto out;
	}

	/*
	 * Raw track based I/O needs IDAWs for each page,
	 * and not just for 64 bit addresses.
	 */
	cidaw = trkcount * DASD_RAW_BLOCK_PER_TRACK;

	/* 1x prefix + one read/write ccw per track */
	cplength = 1 + trkcount;

	/*
	 * struct PFX_eckd_data has up to 2 byte as extended parameter
	 * this is needed for write full track and has to be mentioned
	 * separately
	 * add 8 instead of 2 to keep 8 byte boundary
	 */
	pfx_datasize = sizeof(struct PFX_eckd_data) + 8;

	datasize = pfx_datasize + cidaw * sizeof(unsigned long long);

	/* Allocate the ccw request. */
	cqr = dasd_smalloc_request(DASD_ECKD_MAGIC, cplength,
				   datasize, startdev);
	if (IS_ERR(cqr))
		goto out;
	ccw = cqr->cpaddr;

	if (prefix_LRE(ccw++, cqr->data, first_trk, last_trk, cmd,
		       basedev, startdev, 1 /* format */, first_offs + 1,
		       trkcount, 0, 0) == -EAGAIN) {
		/* Clock not in sync and XRC is enabled.
		 * Try again later.
		 */
		dasd_sfree_request(cqr, startdev);
		cqr = ERR_PTR(-EAGAIN);
		goto out;
	}

	idaws = (unsigned long *)(cqr->data + pfx_datasize);
	len_to_track_end = 0;
	if (start_padding_sectors) {
		ccw[-1].flags |= CCW_FLAG_CC;
		ccw->cmd_code = cmd;
		/* maximum 3390 track size */
		ccw->count = 57326;
		/* 64k map to one track */
		len_to_track_end = 65536 - start_padding_sectors * 512;
		ccw->cda = (__u32)(addr_t)idaws;
		ccw->flags |= CCW_FLAG_IDA;
		ccw->flags |= CCW_FLAG_SLI;
		ccw++;
		for (sectors = 0; sectors < start_padding_sectors; sectors += 8)
			idaws = idal_create_words(idaws, rawpadpage, PAGE_SIZE);
	}
	rq_for_each_segment(bv, req, iter) {
		dst = page_address(bv->bv_page) + bv->bv_offset;
		seg_len = bv->bv_len;
		if (cmd == DASD_ECKD_CCW_READ_TRACK)
			memset(dst, 0, seg_len);
		if (!len_to_track_end) {
			ccw[-1].flags |= CCW_FLAG_CC;
			ccw->cmd_code = cmd;
			/* maximum 3390 track size */
			ccw->count = 57326;
			/* 64k map to one track */
			len_to_track_end = 65536;
			ccw->cda = (__u32)(addr_t)idaws;
			ccw->flags |= CCW_FLAG_IDA;
			ccw->flags |= CCW_FLAG_SLI;
			ccw++;
		}
		len_to_track_end -= seg_len;
		idaws = idal_create_words(idaws, dst, seg_len);
	}
	for (sectors = 0; sectors < end_padding_sectors; sectors += 8)
		idaws = idal_create_words(idaws, rawpadpage, PAGE_SIZE);
	if (blk_noretry_request(req) ||
	    block->base->features & DASD_FEATURE_FAILFAST)
		set_bit(DASD_CQR_FLAGS_FAILFAST, &cqr->flags);
	cqr->startdev = startdev;
	cqr->memdev = startdev;
	cqr->block = block;
	cqr->expires = startdev->default_expires * HZ;
	cqr->lpm = startdev->path_data.ppm;
	cqr->retries = startdev->default_retries;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;

	if (IS_ERR(cqr) && PTR_ERR(cqr) != -EAGAIN)
		cqr = NULL;
out:
	return cqr;
}


static int
dasd_eckd_free_cp(struct dasd_ccw_req *cqr, struct request *req)
{
	struct dasd_eckd_private *private;
	struct ccw1 *ccw;
	struct req_iterator iter;
	struct bio_vec *bv;
	char *dst, *cda;
	unsigned int blksize, blk_per_trk, off;
	sector_t recid;
	int status;

	if (!dasd_page_cache)
		goto out;
	private = (struct dasd_eckd_private *) cqr->block->base->private;
	blksize = cqr->block->bp_block;
	blk_per_trk = recs_per_track(&private->rdc_data, 0, blksize);
	recid = blk_rq_pos(req) >> cqr->block->s2b_shift;
	ccw = cqr->cpaddr;
	/* Skip over define extent & locate record. */
	ccw++;
	if (private->uses_cdl == 0 || recid > 2*blk_per_trk)
		ccw++;
	rq_for_each_segment(bv, req, iter) {
		dst = page_address(bv->bv_page) + bv->bv_offset;
		for (off = 0; off < bv->bv_len; off += blksize) {
			/* Skip locate record. */
			if (private->uses_cdl && recid <= 2*blk_per_trk)
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
			recid++;
		}
	}
out:
	status = cqr->status == DASD_CQR_DONE;
	dasd_sfree_request(cqr, cqr->memdev);
	return status;
}

/*
 * Modify ccw/tcw in cqr so it can be started on a base device.
 *
 * Note that this is not enough to restart the cqr!
 * Either reset cqr->startdev as well (summary unit check handling)
 * or restart via separate cqr (as in ERP handling).
 */
void dasd_eckd_reset_ccw_to_base_io(struct dasd_ccw_req *cqr)
{
	struct ccw1 *ccw;
	struct PFX_eckd_data *pfxdata;
	struct tcw *tcw;
	struct tccb *tccb;
	struct dcw *dcw;

	if (cqr->cpmode == 1) {
		tcw = cqr->cpaddr;
		tccb = tcw_get_tccb(tcw);
		dcw = (struct dcw *)&tccb->tca[0];
		pfxdata = (struct PFX_eckd_data *)&dcw->cd[0];
		pfxdata->validity.verify_base = 0;
		pfxdata->validity.hyper_pav = 0;
	} else {
		ccw = cqr->cpaddr;
		pfxdata = cqr->data;
		if (ccw->cmd_code == DASD_ECKD_CCW_PFX) {
			pfxdata->validity.verify_base = 0;
			pfxdata->validity.hyper_pav = 0;
		}
	}
}

#define DASD_ECKD_CHANQ_MAX_SIZE 4

static struct dasd_ccw_req *dasd_eckd_build_alias_cp(struct dasd_device *base,
						     struct dasd_block *block,
						     struct request *req)
{
	struct dasd_eckd_private *private;
	struct dasd_device *startdev;
	unsigned long flags;
	struct dasd_ccw_req *cqr;

	startdev = dasd_alias_get_start_dev(base);
	if (!startdev)
		startdev = base;
	private = (struct dasd_eckd_private *) startdev->private;
	if (private->count >= DASD_ECKD_CHANQ_MAX_SIZE)
		return ERR_PTR(-EBUSY);

	spin_lock_irqsave(get_ccwdev_lock(startdev->cdev), flags);
	private->count++;
	if ((base->features & DASD_FEATURE_USERAW))
		cqr = dasd_raw_build_cp(startdev, block, req);
	else
		cqr = dasd_eckd_build_cp(startdev, block, req);
	if (IS_ERR(cqr))
		private->count--;
	spin_unlock_irqrestore(get_ccwdev_lock(startdev->cdev), flags);
	return cqr;
}

static int dasd_eckd_free_alias_cp(struct dasd_ccw_req *cqr,
				   struct request *req)
{
	struct dasd_eckd_private *private;
	unsigned long flags;

	spin_lock_irqsave(get_ccwdev_lock(cqr->memdev->cdev), flags);
	private = (struct dasd_eckd_private *) cqr->memdev->private;
	private->count--;
	spin_unlock_irqrestore(get_ccwdev_lock(cqr->memdev->cdev), flags);
	return dasd_eckd_free_cp(cqr, req);
}

static int
dasd_eckd_fill_info(struct dasd_device * device,
		    struct dasd_information2_t * info)
{
	struct dasd_eckd_private *private;

	private = (struct dasd_eckd_private *) device->private;
	info->label_block = 2;
	info->FBA_layout = private->uses_cdl ? 0 : 1;
	info->format = private->uses_cdl ? DASD_FORMAT_CDL : DASD_FORMAT_LDL;
	info->characteristics_size = sizeof(struct dasd_eckd_characteristics);
	memcpy(info->characteristics, &private->rdc_data,
	       sizeof(struct dasd_eckd_characteristics));
	info->confdata_size = min((unsigned long)private->conf_len,
				  sizeof(info->configuration_data));
	memcpy(info->configuration_data, private->conf_data,
	       info->confdata_size);
	return 0;
}

/*
 * SECTION: ioctl functions for eckd devices.
 */

/*
 * Release device ioctl.
 * Buils a channel programm to releases a prior reserved
 * (see dasd_eckd_reserve) device.
 */
static int
dasd_eckd_release(struct dasd_device *device)
{
	struct dasd_ccw_req *cqr;
	int rc;
	struct ccw1 *ccw;
	int useglobal;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	useglobal = 0;
	cqr = dasd_smalloc_request(DASD_ECKD_MAGIC, 1, 32, device);
	if (IS_ERR(cqr)) {
		mutex_lock(&dasd_reserve_mutex);
		useglobal = 1;
		cqr = &dasd_reserve_req->cqr;
		memset(cqr, 0, sizeof(*cqr));
		memset(&dasd_reserve_req->ccw, 0,
		       sizeof(dasd_reserve_req->ccw));
		cqr->cpaddr = &dasd_reserve_req->ccw;
		cqr->data = &dasd_reserve_req->data;
		cqr->magic = DASD_ECKD_MAGIC;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = DASD_ECKD_CCW_RELEASE;
	ccw->flags |= CCW_FLAG_SLI;
	ccw->count = 32;
	ccw->cda = (__u32)(addr_t) cqr->data;
	cqr->startdev = device;
	cqr->memdev = device;
	clear_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags);
	set_bit(DASD_CQR_FLAGS_FAILFAST, &cqr->flags);
	cqr->retries = 2;	/* set retry counter to enable basic ERP */
	cqr->expires = 2 * HZ;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;

	rc = dasd_sleep_on_immediatly(cqr);
	if (!rc)
		clear_bit(DASD_FLAG_IS_RESERVED, &device->flags);

	if (useglobal)
		mutex_unlock(&dasd_reserve_mutex);
	else
		dasd_sfree_request(cqr, cqr->memdev);
	return rc;
}

/*
 * Reserve device ioctl.
 * Options are set to 'synchronous wait for interrupt' and
 * 'timeout the request'. This leads to a terminate IO if
 * the interrupt is outstanding for a certain time.
 */
static int
dasd_eckd_reserve(struct dasd_device *device)
{
	struct dasd_ccw_req *cqr;
	int rc;
	struct ccw1 *ccw;
	int useglobal;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	useglobal = 0;
	cqr = dasd_smalloc_request(DASD_ECKD_MAGIC, 1, 32, device);
	if (IS_ERR(cqr)) {
		mutex_lock(&dasd_reserve_mutex);
		useglobal = 1;
		cqr = &dasd_reserve_req->cqr;
		memset(cqr, 0, sizeof(*cqr));
		memset(&dasd_reserve_req->ccw, 0,
		       sizeof(dasd_reserve_req->ccw));
		cqr->cpaddr = &dasd_reserve_req->ccw;
		cqr->data = &dasd_reserve_req->data;
		cqr->magic = DASD_ECKD_MAGIC;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = DASD_ECKD_CCW_RESERVE;
	ccw->flags |= CCW_FLAG_SLI;
	ccw->count = 32;
	ccw->cda = (__u32)(addr_t) cqr->data;
	cqr->startdev = device;
	cqr->memdev = device;
	clear_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags);
	set_bit(DASD_CQR_FLAGS_FAILFAST, &cqr->flags);
	cqr->retries = 2;	/* set retry counter to enable basic ERP */
	cqr->expires = 2 * HZ;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;

	rc = dasd_sleep_on_immediatly(cqr);
	if (!rc)
		set_bit(DASD_FLAG_IS_RESERVED, &device->flags);

	if (useglobal)
		mutex_unlock(&dasd_reserve_mutex);
	else
		dasd_sfree_request(cqr, cqr->memdev);
	return rc;
}

/*
 * Steal lock ioctl - unconditional reserve device.
 * Buils a channel programm to break a device's reservation.
 * (unconditional reserve)
 */
static int
dasd_eckd_steal_lock(struct dasd_device *device)
{
	struct dasd_ccw_req *cqr;
	int rc;
	struct ccw1 *ccw;
	int useglobal;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	useglobal = 0;
	cqr = dasd_smalloc_request(DASD_ECKD_MAGIC, 1, 32, device);
	if (IS_ERR(cqr)) {
		mutex_lock(&dasd_reserve_mutex);
		useglobal = 1;
		cqr = &dasd_reserve_req->cqr;
		memset(cqr, 0, sizeof(*cqr));
		memset(&dasd_reserve_req->ccw, 0,
		       sizeof(dasd_reserve_req->ccw));
		cqr->cpaddr = &dasd_reserve_req->ccw;
		cqr->data = &dasd_reserve_req->data;
		cqr->magic = DASD_ECKD_MAGIC;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = DASD_ECKD_CCW_SLCK;
	ccw->flags |= CCW_FLAG_SLI;
	ccw->count = 32;
	ccw->cda = (__u32)(addr_t) cqr->data;
	cqr->startdev = device;
	cqr->memdev = device;
	clear_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags);
	set_bit(DASD_CQR_FLAGS_FAILFAST, &cqr->flags);
	cqr->retries = 2;	/* set retry counter to enable basic ERP */
	cqr->expires = 2 * HZ;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;

	rc = dasd_sleep_on_immediatly(cqr);
	if (!rc)
		set_bit(DASD_FLAG_IS_RESERVED, &device->flags);

	if (useglobal)
		mutex_unlock(&dasd_reserve_mutex);
	else
		dasd_sfree_request(cqr, cqr->memdev);
	return rc;
}

/*
 * SNID - Sense Path Group ID
 * This ioctl may be used in situations where I/O is stalled due to
 * a reserve, so if the normal dasd_smalloc_request fails, we use the
 * preallocated dasd_reserve_req.
 */
static int dasd_eckd_snid(struct dasd_device *device,
			  void __user *argp)
{
	struct dasd_ccw_req *cqr;
	int rc;
	struct ccw1 *ccw;
	int useglobal;
	struct dasd_snid_ioctl_data usrparm;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (copy_from_user(&usrparm, argp, sizeof(usrparm)))
		return -EFAULT;

	useglobal = 0;
	cqr = dasd_smalloc_request(DASD_ECKD_MAGIC, 1,
				   sizeof(struct dasd_snid_data), device);
	if (IS_ERR(cqr)) {
		mutex_lock(&dasd_reserve_mutex);
		useglobal = 1;
		cqr = &dasd_reserve_req->cqr;
		memset(cqr, 0, sizeof(*cqr));
		memset(&dasd_reserve_req->ccw, 0,
		       sizeof(dasd_reserve_req->ccw));
		cqr->cpaddr = &dasd_reserve_req->ccw;
		cqr->data = &dasd_reserve_req->data;
		cqr->magic = DASD_ECKD_MAGIC;
	}
	ccw = cqr->cpaddr;
	ccw->cmd_code = DASD_ECKD_CCW_SNID;
	ccw->flags |= CCW_FLAG_SLI;
	ccw->count = 12;
	ccw->cda = (__u32)(addr_t) cqr->data;
	cqr->startdev = device;
	cqr->memdev = device;
	clear_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags);
	set_bit(DASD_CQR_FLAGS_FAILFAST, &cqr->flags);
	set_bit(DASD_CQR_ALLOW_SLOCK, &cqr->flags);
	cqr->retries = 5;
	cqr->expires = 10 * HZ;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;
	cqr->lpm = usrparm.path_mask;

	rc = dasd_sleep_on_immediatly(cqr);
	/* verify that I/O processing didn't modify the path mask */
	if (!rc && usrparm.path_mask && (cqr->lpm != usrparm.path_mask))
		rc = -EIO;
	if (!rc) {
		usrparm.data = *((struct dasd_snid_data *)cqr->data);
		if (copy_to_user(argp, &usrparm, sizeof(usrparm)))
			rc = -EFAULT;
	}

	if (useglobal)
		mutex_unlock(&dasd_reserve_mutex);
	else
		dasd_sfree_request(cqr, cqr->memdev);
	return rc;
}

/*
 * Read performance statistics
 */
static int
dasd_eckd_performance(struct dasd_device *device, void __user *argp)
{
	struct dasd_psf_prssd_data *prssdp;
	struct dasd_rssd_perf_stats_t *stats;
	struct dasd_ccw_req *cqr;
	struct ccw1 *ccw;
	int rc;

	cqr = dasd_smalloc_request(DASD_ECKD_MAGIC, 1 /* PSF */  + 1 /* RSSD */,
				   (sizeof(struct dasd_psf_prssd_data) +
				    sizeof(struct dasd_rssd_perf_stats_t)),
				   device);
	if (IS_ERR(cqr)) {
		DBF_DEV_EVENT(DBF_WARNING, device, "%s",
			    "Could not allocate initialization request");
		return PTR_ERR(cqr);
	}
	cqr->startdev = device;
	cqr->memdev = device;
	cqr->retries = 0;
	clear_bit(DASD_CQR_FLAGS_USE_ERP, &cqr->flags);
	cqr->expires = 10 * HZ;

	/* Prepare for Read Subsystem Data */
	prssdp = (struct dasd_psf_prssd_data *) cqr->data;
	memset(prssdp, 0, sizeof(struct dasd_psf_prssd_data));
	prssdp->order = PSF_ORDER_PRSSD;
	prssdp->suborder = 0x01;	/* Performance Statistics */
	prssdp->varies[1] = 0x01;	/* Perf Statistics for the Subsystem */

	ccw = cqr->cpaddr;
	ccw->cmd_code = DASD_ECKD_CCW_PSF;
	ccw->count = sizeof(struct dasd_psf_prssd_data);
	ccw->flags |= CCW_FLAG_CC;
	ccw->cda = (__u32)(addr_t) prssdp;

	/* Read Subsystem Data - Performance Statistics */
	stats = (struct dasd_rssd_perf_stats_t *) (prssdp + 1);
	memset(stats, 0, sizeof(struct dasd_rssd_perf_stats_t));

	ccw++;
	ccw->cmd_code = DASD_ECKD_CCW_RSSD;
	ccw->count = sizeof(struct dasd_rssd_perf_stats_t);
	ccw->cda = (__u32)(addr_t) stats;

	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;
	rc = dasd_sleep_on(cqr);
	if (rc == 0) {
		prssdp = (struct dasd_psf_prssd_data *) cqr->data;
		stats = (struct dasd_rssd_perf_stats_t *) (prssdp + 1);
		if (copy_to_user(argp, stats,
				 sizeof(struct dasd_rssd_perf_stats_t)))
			rc = -EFAULT;
	}
	dasd_sfree_request(cqr, cqr->memdev);
	return rc;
}

/*
 * Get attributes (cache operations)
 * Returnes the cache attributes used in Define Extend (DE).
 */
static int
dasd_eckd_get_attrib(struct dasd_device *device, void __user *argp)
{
	struct dasd_eckd_private *private =
		(struct dasd_eckd_private *)device->private;
	struct attrib_data_t attrib = private->attrib;
	int rc;

        if (!capable(CAP_SYS_ADMIN))
                return -EACCES;
	if (!argp)
                return -EINVAL;

	rc = 0;
	if (copy_to_user(argp, (long *) &attrib,
			 sizeof(struct attrib_data_t)))
		rc = -EFAULT;

	return rc;
}

/*
 * Set attributes (cache operations)
 * Stores the attributes for cache operation to be used in Define Extend (DE).
 */
static int
dasd_eckd_set_attrib(struct dasd_device *device, void __user *argp)
{
	struct dasd_eckd_private *private =
		(struct dasd_eckd_private *)device->private;
	struct attrib_data_t attrib;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (!argp)
		return -EINVAL;

	if (copy_from_user(&attrib, argp, sizeof(struct attrib_data_t)))
		return -EFAULT;
	private->attrib = attrib;

	dev_info(&device->cdev->dev,
		 "The DASD cache mode was set to %x (%i cylinder prestage)\n",
		 private->attrib.operation, private->attrib.nr_cyl);
	return 0;
}

/*
 * Issue syscall I/O to EMC Symmetrix array.
 * CCWs are PSF and RSSD
 */
static int dasd_symm_io(struct dasd_device *device, void __user *argp)
{
	struct dasd_symmio_parms usrparm;
	char *psf_data, *rssd_result;
	struct dasd_ccw_req *cqr;
	struct ccw1 *ccw;
	char psf0, psf1;
	int rc;

	if (!capable(CAP_SYS_ADMIN) && !capable(CAP_SYS_RAWIO))
		return -EACCES;
	psf0 = psf1 = 0;

	/* Copy parms from caller */
	rc = -EFAULT;
	if (copy_from_user(&usrparm, argp, sizeof(usrparm)))
		goto out;
	if (is_compat_task() || sizeof(long) == 4) {
		/* Make sure pointers are sane even on 31 bit. */
		rc = -EINVAL;
		if ((usrparm.psf_data >> 32) != 0)
			goto out;
		if ((usrparm.rssd_result >> 32) != 0)
			goto out;
		usrparm.psf_data &= 0x7fffffffULL;
		usrparm.rssd_result &= 0x7fffffffULL;
	}
	/* alloc I/O data area */
	psf_data = kzalloc(usrparm.psf_data_len, GFP_KERNEL | GFP_DMA);
	rssd_result = kzalloc(usrparm.rssd_result_len, GFP_KERNEL | GFP_DMA);
	if (!psf_data || !rssd_result) {
		rc = -ENOMEM;
		goto out_free;
	}

	/* get syscall header from user space */
	rc = -EFAULT;
	if (copy_from_user(psf_data,
			   (void __user *)(unsigned long) usrparm.psf_data,
			   usrparm.psf_data_len))
		goto out_free;
	psf0 = psf_data[0];
	psf1 = psf_data[1];

	/* setup CCWs for PSF + RSSD */
	cqr = dasd_smalloc_request(DASD_ECKD_MAGIC, 2 , 0, device);
	if (IS_ERR(cqr)) {
		DBF_DEV_EVENT(DBF_WARNING, device, "%s",
			"Could not allocate initialization request");
		rc = PTR_ERR(cqr);
		goto out_free;
	}

	cqr->startdev = device;
	cqr->memdev = device;
	cqr->retries = 3;
	cqr->expires = 10 * HZ;
	cqr->buildclk = get_tod_clock();
	cqr->status = DASD_CQR_FILLED;

	/* Build the ccws */
	ccw = cqr->cpaddr;

	/* PSF ccw */
	ccw->cmd_code = DASD_ECKD_CCW_PSF;
	ccw->count = usrparm.psf_data_len;
	ccw->flags |= CCW_FLAG_CC;
	ccw->cda = (__u32)(addr_t) psf_data;

	ccw++;

	/* RSSD ccw  */
	ccw->cmd_code = DASD_ECKD_CCW_RSSD;
	ccw->count = usrparm.rssd_result_len;
	ccw->flags = CCW_FLAG_SLI ;
	ccw->cda = (__u32)(addr_t) rssd_result;

	rc = dasd_sleep_on(cqr);
	if (rc)
		goto out_sfree;

	rc = -EFAULT;
	if (copy_to_user((void __user *)(unsigned long) usrparm.rssd_result,
			   rssd_result, usrparm.rssd_result_len))
		goto out_sfree;
	rc = 0;

out_sfree:
	dasd_sfree_request(cqr, cqr->memdev);
out_free:
	kfree(rssd_result);
	kfree(psf_data);
out:
	DBF_DEV_EVENT(DBF_WARNING, device,
		      "Symmetrix ioctl (0x%02x 0x%02x): rc=%d",
		      (int) psf0, (int) psf1, rc);
	return rc;
}

static int
dasd_eckd_ioctl(struct dasd_block *block, unsigned int cmd, void __user *argp)
{
	struct dasd_device *device = block->base;

	switch (cmd) {
	case BIODASDGATTR:
		return dasd_eckd_get_attrib(device, argp);
	case BIODASDSATTR:
		return dasd_eckd_set_attrib(device, argp);
	case BIODASDPSRD:
		return dasd_eckd_performance(device, argp);
	case BIODASDRLSE:
		return dasd_eckd_release(device);
	case BIODASDRSRV:
		return dasd_eckd_reserve(device);
	case BIODASDSLCK:
		return dasd_eckd_steal_lock(device);
	case BIODASDSNID:
		return dasd_eckd_snid(device, argp);
	case BIODASDSYMMIO:
		return dasd_symm_io(device, argp);
	default:
		return -ENOTTY;
	}
}

/*
 * Dump the range of CCWs into 'page' buffer
 * and return number of printed chars.
 */
static int
dasd_eckd_dump_ccw_range(struct ccw1 *from, struct ccw1 *to, char *page)
{
	int len, count;
	char *datap;

	len = 0;
	while (from <= to) {
		len += sprintf(page + len, PRINTK_HEADER
			       " CCW %p: %08X %08X DAT:",
			       from, ((int *) from)[0], ((int *) from)[1]);

		/* get pointer to data (consider IDALs) */
		if (from->flags & CCW_FLAG_IDA)
			datap = (char *) *((addr_t *) (addr_t) from->cda);
		else
			datap = (char *) ((addr_t) from->cda);

		/* dump data (max 32 bytes) */
		for (count = 0; count < from->count && count < 32; count++) {
			if (count % 8 == 0) len += sprintf(page + len, " ");
			if (count % 4 == 0) len += sprintf(page + len, " ");
			len += sprintf(page + len, "%02x", datap[count]);
		}
		len += sprintf(page + len, "\n");
		from++;
	}
	return len;
}

static void
dasd_eckd_dump_sense_dbf(struct dasd_device *device, struct irb *irb,
			 char *reason)
{
	u64 *sense;
	u64 *stat;

	sense = (u64 *) dasd_get_sense(irb);
	stat = (u64 *) &irb->scsw;
	if (sense) {
		DBF_DEV_EVENT(DBF_EMERG, device, "%s: %016llx %08x : "
			      "%016llx %016llx %016llx %016llx",
			      reason, *stat, *((u32 *) (stat + 1)),
			      sense[0], sense[1], sense[2], sense[3]);
	} else {
		DBF_DEV_EVENT(DBF_EMERG, device, "%s: %016llx %08x : %s",
			      reason, *stat, *((u32 *) (stat + 1)),
			      "NO VALID SENSE");
	}
}

/*
 * Print sense data and related channel program.
 * Parts are printed because printk buffer is only 1024 bytes.
 */
static void dasd_eckd_dump_sense_ccw(struct dasd_device *device,
				 struct dasd_ccw_req *req, struct irb *irb)
{
	char *page;
	struct ccw1 *first, *last, *fail, *from, *to;
	int len, sl, sct;

	page = (char *) get_zeroed_page(GFP_ATOMIC);
	if (page == NULL) {
		DBF_DEV_EVENT(DBF_WARNING, device, "%s",
			      "No memory to dump sense data\n");
		return;
	}
	/* dump the sense data */
	len = sprintf(page, PRINTK_HEADER
		      " I/O status report for device %s:\n",
		      dev_name(&device->cdev->dev));
	len += sprintf(page + len, PRINTK_HEADER
		       " in req: %p CC:%02X FC:%02X AC:%02X SC:%02X DS:%02X "
		       "CS:%02X RC:%d\n",
		       req, scsw_cc(&irb->scsw), scsw_fctl(&irb->scsw),
		       scsw_actl(&irb->scsw), scsw_stctl(&irb->scsw),
		       scsw_dstat(&irb->scsw), scsw_cstat(&irb->scsw),
		       req ? req->intrc : 0);
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

		if (irb->ecw[27] & DASD_SENSE_BIT_0) {
			/* 24 Byte Sense Data */
			sprintf(page + len, PRINTK_HEADER
				" 24 Byte: %x MSG %x, "
				"%s MSGb to SYSOP\n",
				irb->ecw[7] >> 4, irb->ecw[7] & 0x0f,
				irb->ecw[1] & 0x10 ? "" : "no");
		} else {
			/* 32 Byte Sense Data */
			sprintf(page + len, PRINTK_HEADER
				" 32 Byte: Format: %x "
				"Exception class %x\n",
				irb->ecw[6] & 0x0f, irb->ecw[22] >> 4);
		}
	} else {
		sprintf(page + len, PRINTK_HEADER
			" SORRY - NO VALID SENSE AVAILABLE\n");
	}
	printk(KERN_ERR "%s", page);

	if (req) {
		/* req == NULL for unsolicited interrupts */
		/* dump the Channel Program (max 140 Bytes per line) */
		/* Count CCW and print first CCWs (maximum 1024 % 140 = 7) */
		first = req->cpaddr;
		for (last = first; last->flags & (CCW_FLAG_CC | CCW_FLAG_DC); last++);
		to = min(first + 6, last);
		len = sprintf(page, PRINTK_HEADER
			      " Related CP in req: %p\n", req);
		dasd_eckd_dump_ccw_range(first, to, page + len);
		printk(KERN_ERR "%s", page);

		/* print failing CCW area (maximum 4) */
		/* scsw->cda is either valid or zero  */
		len = 0;
		from = ++to;
		fail = (struct ccw1 *)(addr_t)
				irb->scsw.cmd.cpa; /* failing CCW */
		if (from <  fail - 2) {
			from = fail - 2;     /* there is a gap - print header */
			len += sprintf(page, PRINTK_HEADER "......\n");
		}
		to = min(fail + 1, last);
		len += dasd_eckd_dump_ccw_range(from, to, page + len);

		/* print last CCWs (maximum 2) */
		from = max(from, ++to);
		if (from < last - 1) {
			from = last - 1;     /* there is a gap - print header */
			len += sprintf(page + len, PRINTK_HEADER "......\n");
		}
		len += dasd_eckd_dump_ccw_range(from, last, page + len);
		if (len > 0)
			printk(KERN_ERR "%s", page);
	}
	free_page((unsigned long) page);
}


/*
 * Print sense data from a tcw.
 */
static void dasd_eckd_dump_sense_tcw(struct dasd_device *device,
				 struct dasd_ccw_req *req, struct irb *irb)
{
	char *page;
	int len, sl, sct, residual;
	struct tsb *tsb;
	u8 *sense, *rcq;

	page = (char *) get_zeroed_page(GFP_ATOMIC);
	if (page == NULL) {
		DBF_DEV_EVENT(DBF_WARNING, device, " %s",
			    "No memory to dump sense data");
		return;
	}
	/* dump the sense data */
	len = sprintf(page, PRINTK_HEADER
		      " I/O status report for device %s:\n",
		      dev_name(&device->cdev->dev));
	len += sprintf(page + len, PRINTK_HEADER
		       " in req: %p CC:%02X FC:%02X AC:%02X SC:%02X DS:%02X "
		       "CS:%02X fcxs:%02X schxs:%02X RC:%d\n",
		       req, scsw_cc(&irb->scsw), scsw_fctl(&irb->scsw),
		       scsw_actl(&irb->scsw), scsw_stctl(&irb->scsw),
		       scsw_dstat(&irb->scsw), scsw_cstat(&irb->scsw),
		       irb->scsw.tm.fcxs, irb->scsw.tm.schxs,
		       req ? req->intrc : 0);
	len += sprintf(page + len, PRINTK_HEADER
		       " device %s: Failing TCW: %p\n",
		       dev_name(&device->cdev->dev),
		       (void *) (addr_t) irb->scsw.tm.tcw);

	tsb = NULL;
	sense = NULL;
	if (irb->scsw.tm.tcw && (irb->scsw.tm.fcxs & 0x01))
		tsb = tcw_get_tsb(
			(struct tcw *)(unsigned long)irb->scsw.tm.tcw);

	if (tsb) {
		len += sprintf(page + len, PRINTK_HEADER
			       " tsb->length %d\n", tsb->length);
		len += sprintf(page + len, PRINTK_HEADER
			       " tsb->flags %x\n", tsb->flags);
		len += sprintf(page + len, PRINTK_HEADER
			       " tsb->dcw_offset %d\n", tsb->dcw_offset);
		len += sprintf(page + len, PRINTK_HEADER
			       " tsb->count %d\n", tsb->count);
		residual = tsb->count - 28;
		len += sprintf(page + len, PRINTK_HEADER
			       " residual %d\n", residual);

		switch (tsb->flags & 0x07) {
		case 1:	/* tsa_iostat */
			len += sprintf(page + len, PRINTK_HEADER
			       " tsb->tsa.iostat.dev_time %d\n",
				       tsb->tsa.iostat.dev_time);
			len += sprintf(page + len, PRINTK_HEADER
			       " tsb->tsa.iostat.def_time %d\n",
				       tsb->tsa.iostat.def_time);
			len += sprintf(page + len, PRINTK_HEADER
			       " tsb->tsa.iostat.queue_time %d\n",
				       tsb->tsa.iostat.queue_time);
			len += sprintf(page + len, PRINTK_HEADER
			       " tsb->tsa.iostat.dev_busy_time %d\n",
				       tsb->tsa.iostat.dev_busy_time);
			len += sprintf(page + len, PRINTK_HEADER
			       " tsb->tsa.iostat.dev_act_time %d\n",
				       tsb->tsa.iostat.dev_act_time);
			sense = tsb->tsa.iostat.sense;
			break;
		case 2: /* ts_ddpc */
			len += sprintf(page + len, PRINTK_HEADER
			       " tsb->tsa.ddpc.rc %d\n", tsb->tsa.ddpc.rc);
			for (sl = 0; sl < 2; sl++) {
				len += sprintf(page + len, PRINTK_HEADER
					       " tsb->tsa.ddpc.rcq %2d-%2d: ",
					       (8 * sl), ((8 * sl) + 7));
				rcq = tsb->tsa.ddpc.rcq;
				for (sct = 0; sct < 8; sct++) {
					len += sprintf(page + len, " %02x",
						       rcq[8 * sl + sct]);
				}
				len += sprintf(page + len, "\n");
			}
			sense = tsb->tsa.ddpc.sense;
			break;
		case 3: /* tsa_intrg */
			len += sprintf(page + len, PRINTK_HEADER
				      " tsb->tsa.intrg.: not supportet yet\n");
			break;
		}

		if (sense) {
			for (sl = 0; sl < 4; sl++) {
				len += sprintf(page + len, PRINTK_HEADER
					       " Sense(hex) %2d-%2d:",
					       (8 * sl), ((8 * sl) + 7));
				for (sct = 0; sct < 8; sct++) {
					len += sprintf(page + len, " %02x",
						       sense[8 * sl + sct]);
				}
				len += sprintf(page + len, "\n");
			}

			if (sense[27] & DASD_SENSE_BIT_0) {
				/* 24 Byte Sense Data */
				sprintf(page + len, PRINTK_HEADER
					" 24 Byte: %x MSG %x, "
					"%s MSGb to SYSOP\n",
					sense[7] >> 4, sense[7] & 0x0f,
					sense[1] & 0x10 ? "" : "no");
			} else {
				/* 32 Byte Sense Data */
				sprintf(page + len, PRINTK_HEADER
					" 32 Byte: Format: %x "
					"Exception class %x\n",
					sense[6] & 0x0f, sense[22] >> 4);
			}
		} else {
			sprintf(page + len, PRINTK_HEADER
				" SORRY - NO VALID SENSE AVAILABLE\n");
		}
	} else {
		sprintf(page + len, PRINTK_HEADER
			" SORRY - NO TSB DATA AVAILABLE\n");
	}
	printk(KERN_ERR "%s", page);
	free_page((unsigned long) page);
}

static void dasd_eckd_dump_sense(struct dasd_device *device,
				 struct dasd_ccw_req *req, struct irb *irb)
{
	if (scsw_is_tm(&irb->scsw))
		dasd_eckd_dump_sense_tcw(device, req, irb);
	else
		dasd_eckd_dump_sense_ccw(device, req, irb);
}

static int dasd_eckd_pm_freeze(struct dasd_device *device)
{
	/*
	 * the device should be disconnected from our LCU structure
	 * on restore we will reconnect it and reread LCU specific
	 * information like PAV support that might have changed
	 */
	dasd_alias_remove_device(device);
	dasd_alias_disconnect_device_from_lcu(device);

	return 0;
}

static int dasd_eckd_restore_device(struct dasd_device *device)
{
	struct dasd_eckd_private *private;
	struct dasd_eckd_characteristics temp_rdc_data;
	int rc;
	struct dasd_uid temp_uid;
	unsigned long flags;
	unsigned long cqr_flags = 0;

	private = (struct dasd_eckd_private *) device->private;

	/* Read Configuration Data */
	dasd_eckd_read_conf(device);

	dasd_eckd_get_uid(device, &temp_uid);
	/* Generate device unique id */
	rc = dasd_eckd_generate_uid(device);
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	if (memcmp(&private->uid, &temp_uid, sizeof(struct dasd_uid)) != 0)
		dev_err(&device->cdev->dev, "The UID of the DASD has "
			"changed\n");
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);
	if (rc)
		goto out_err;

	/* register lcu with alias handling, enable PAV if this is a new lcu */
	rc = dasd_alias_make_device_known_to_lcu(device);
	if (rc)
		return rc;

	set_bit(DASD_CQR_FLAGS_FAILFAST, &cqr_flags);
	dasd_eckd_validate_server(device, cqr_flags);

	/* RE-Read Configuration Data */
	dasd_eckd_read_conf(device);

	/* Read Feature Codes */
	dasd_eckd_read_features(device);

	/* Read Device Characteristics */
	rc = dasd_generic_read_dev_chars(device, DASD_ECKD_MAGIC,
					 &temp_rdc_data, 64);
	if (rc) {
		DBF_EVENT_DEVID(DBF_WARNING, device->cdev,
				"Read device characteristic failed, rc=%d", rc);
		goto out_err;
	}
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	memcpy(&private->rdc_data, &temp_rdc_data, sizeof(temp_rdc_data));
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);

	/* add device to alias management */
	dasd_alias_add_device(device);

	return 0;

out_err:
	return -1;
}

static int dasd_eckd_reload_device(struct dasd_device *device)
{
	struct dasd_eckd_private *private;
	int rc, old_base;
	char print_uid[60];
	struct dasd_uid uid;
	unsigned long flags;

	private = (struct dasd_eckd_private *) device->private;

	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	old_base = private->uid.base_unit_addr;
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);

	/* Read Configuration Data */
	rc = dasd_eckd_read_conf(device);
	if (rc)
		goto out_err;

	rc = dasd_eckd_generate_uid(device);
	if (rc)
		goto out_err;
	/*
	 * update unit address configuration and
	 * add device to alias management
	 */
	dasd_alias_update_add_device(device);

	dasd_eckd_get_uid(device, &uid);

	if (old_base != uid.base_unit_addr) {
		if (strlen(uid.vduit) > 0)
			snprintf(print_uid, sizeof(print_uid),
				 "%s.%s.%04x.%02x.%s", uid.vendor, uid.serial,
				 uid.ssid, uid.base_unit_addr, uid.vduit);
		else
			snprintf(print_uid, sizeof(print_uid),
				 "%s.%s.%04x.%02x", uid.vendor, uid.serial,
				 uid.ssid, uid.base_unit_addr);

		dev_info(&device->cdev->dev,
			 "An Alias device was reassigned to a new base device "
			 "with UID: %s\n", print_uid);
	}
	return 0;

out_err:
	return -1;
}

static struct ccw_driver dasd_eckd_driver = {
	.driver = {
		.name	= "dasd-eckd",
		.owner	= THIS_MODULE,
	},
	.ids	     = dasd_eckd_ids,
	.probe	     = dasd_eckd_probe,
	.remove      = dasd_generic_remove,
	.set_offline = dasd_generic_set_offline,
	.set_online  = dasd_eckd_set_online,
	.notify      = dasd_generic_notify,
	.path_event  = dasd_generic_path_event,
	.shutdown    = dasd_generic_shutdown,
	.freeze      = dasd_generic_pm_freeze,
	.thaw	     = dasd_generic_restore_device,
	.restore     = dasd_generic_restore_device,
	.uc_handler  = dasd_generic_uc_handler,
	.int_class   = IRQIO_DAS,
};

/*
 * max_blocks is dependent on the amount of storage that is available
 * in the static io buffer for each device. Currently each device has
 * 8192 bytes (=2 pages). For 64 bit one dasd_mchunkt_t structure has
 * 24 bytes, the struct dasd_ccw_req has 136 bytes and each block can use
 * up to 16 bytes (8 for the ccw and 8 for the idal pointer). In
 * addition we have one define extent ccw + 16 bytes of data and one
 * locate record ccw + 16 bytes of data. That makes:
 * (8192 - 24 - 136 - 8 - 16 - 8 - 16) / 16 = 499 blocks at maximum.
 * We want to fit two into the available memory so that we can immediately
 * start the next request if one finishes off. That makes 249.5 blocks
 * for one request. Give a little safety and the result is 240.
 */
static struct dasd_discipline dasd_eckd_discipline = {
	.owner = THIS_MODULE,
	.name = "ECKD",
	.ebcname = "ECKD",
	.max_blocks = 190,
	.check_device = dasd_eckd_check_characteristics,
	.uncheck_device = dasd_eckd_uncheck_device,
	.do_analysis = dasd_eckd_do_analysis,
	.verify_path = dasd_eckd_verify_path,
	.basic_to_ready = dasd_eckd_basic_to_ready,
	.online_to_ready = dasd_eckd_online_to_ready,
	.ready_to_basic = dasd_eckd_ready_to_basic,
	.fill_geometry = dasd_eckd_fill_geometry,
	.start_IO = dasd_start_IO,
	.term_IO = dasd_term_IO,
	.handle_terminated_request = dasd_eckd_handle_terminated_request,
	.format_device = dasd_eckd_format_device,
	.erp_action = dasd_eckd_erp_action,
	.erp_postaction = dasd_eckd_erp_postaction,
	.check_for_device_change = dasd_eckd_check_for_device_change,
	.build_cp = dasd_eckd_build_alias_cp,
	.free_cp = dasd_eckd_free_alias_cp,
	.dump_sense = dasd_eckd_dump_sense,
	.dump_sense_dbf = dasd_eckd_dump_sense_dbf,
	.fill_info = dasd_eckd_fill_info,
	.ioctl = dasd_eckd_ioctl,
	.freeze = dasd_eckd_pm_freeze,
	.restore = dasd_eckd_restore_device,
	.reload = dasd_eckd_reload_device,
	.get_uid = dasd_eckd_get_uid,
	.kick_validate = dasd_eckd_kick_validate_server,
};

static int __init
dasd_eckd_init(void)
{
	int ret;

	ASCEBC(dasd_eckd_discipline.ebcname, 4);
	dasd_reserve_req = kmalloc(sizeof(*dasd_reserve_req),
				   GFP_KERNEL | GFP_DMA);
	if (!dasd_reserve_req)
		return -ENOMEM;
	path_verification_worker = kmalloc(sizeof(*path_verification_worker),
				   GFP_KERNEL | GFP_DMA);
	if (!path_verification_worker) {
		kfree(dasd_reserve_req);
		return -ENOMEM;
	}
	rawpadpage = (void *)__get_free_page(GFP_KERNEL);
	if (!rawpadpage) {
		kfree(path_verification_worker);
		kfree(dasd_reserve_req);
		return -ENOMEM;
	}
	ret = ccw_driver_register(&dasd_eckd_driver);
	if (!ret)
		wait_for_device_probe();
	else {
		kfree(path_verification_worker);
		kfree(dasd_reserve_req);
		free_page((unsigned long)rawpadpage);
	}
	return ret;
}

static void __exit
dasd_eckd_cleanup(void)
{
	ccw_driver_unregister(&dasd_eckd_driver);
	kfree(path_verification_worker);
	kfree(dasd_reserve_req);
	free_page((unsigned long)rawpadpage);
}

module_init(dasd_eckd_init);
module_exit(dasd_eckd_cleanup);
