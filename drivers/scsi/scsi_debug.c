/*
 *  linux/kernel/scsi_debug.c
 * vvvvvvvvvvvvvvvvvvvvvvv Original vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
 *  Copyright (C) 1992  Eric Youngdale
 *  Simulate a host adapter with 2 disks attached.  Do a lot of checking
 *  to make sure that we are not getting blocks mixed up, and PANIC if
 *  anything out of the ordinary is seen.
 * ^^^^^^^^^^^^^^^^^^^^^^^ Original ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 *  This version is more generic, simulating a variable number of disk
 *  (or disk like devices) sharing a common amount of RAM
 *
 *
 *  For documentation see http://www.torque.net/sg/sdebug26.html
 *
 *   D. Gilbert (dpg) work for Magneto-Optical device test [20010421]
 *   dpg: work for devfs large number of disks [20010809]
 *        forked for lk 2.5 series [20011216, 20020101]
 *        use vmalloc() more inquiry+mode_sense [20020302]
 *        add timers for delayed responses [20020721]
 *   Patrick Mansfield <patmans@us.ibm.com> max_luns+scsi_level [20021031]
 *   Mike Anderson <andmike@us.ibm.com> sysfs work [20021118]
 *   dpg: change style of boot options to "scsi_debug.num_tgts=2" and
 *        module options to "modprobe scsi_debug num_tgts=2" [20021221]
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/genhd.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>
#include <linux/vmalloc.h>
#include <linux/moduleparam.h>

#include <linux/blkdev.h>
#include "scsi.h"
#include <scsi/scsi_host.h>
#include <scsi/scsicam.h>

#include <linux/stat.h>

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

#include "scsi_logging.h"
#include "scsi_debug.h"

#define SCSI_DEBUG_VERSION "1.75"
static const char * scsi_debug_version_date = "20050113";

/* Additional Sense Code (ASC) used */
#define NO_ADDED_SENSE 0x0
#define UNRECOVERED_READ_ERR 0x11
#define INVALID_OPCODE 0x20
#define ADDR_OUT_OF_RANGE 0x21
#define INVALID_FIELD_IN_CDB 0x24
#define POWERON_RESET 0x29
#define SAVING_PARAMS_UNSUP 0x39
#define THRESHHOLD_EXCEEDED 0x5d

#define SDEBUG_TAGGED_QUEUING 0 /* 0 | MSG_SIMPLE_TAG | MSG_ORDERED_TAG */

/* Default values for driver parameters */
#define DEF_NUM_HOST   1
#define DEF_NUM_TGTS   1
#define DEF_MAX_LUNS   1
/* With these defaults, this driver will make 1 host with 1 target
 * (id 0) containing 1 logical unit (lun 0). That is 1 device.
 */
#define DEF_DELAY   1
#define DEF_DEV_SIZE_MB   8
#define DEF_EVERY_NTH   0
#define DEF_NUM_PARTS   0
#define DEF_OPTS   0
#define DEF_SCSI_LEVEL   5    /* INQUIRY, byte2 [5->SPC-3] */
#define DEF_PTYPE   0
#define DEF_D_SENSE   0

/* bit mask values for scsi_debug_opts */
#define SCSI_DEBUG_OPT_NOISE   1
#define SCSI_DEBUG_OPT_MEDIUM_ERR   2
#define SCSI_DEBUG_OPT_TIMEOUT   4
#define SCSI_DEBUG_OPT_RECOVERED_ERR   8
/* When "every_nth" > 0 then modulo "every_nth" commands:
 *   - a no response is simulated if SCSI_DEBUG_OPT_TIMEOUT is set
 *   - a RECOVERED_ERROR is simulated on successful read and write
 *     commands if SCSI_DEBUG_OPT_RECOVERED_ERR is set.
 *
 * When "every_nth" < 0 then after "- every_nth" commands:
 *   - a no response is simulated if SCSI_DEBUG_OPT_TIMEOUT is set
 *   - a RECOVERED_ERROR is simulated on successful read and write
 *     commands if SCSI_DEBUG_OPT_RECOVERED_ERR is set.
 * This will continue until some other action occurs (e.g. the user
 * writing a new value (other than -1 or 1) to every_nth via sysfs).
 */

/* when 1==SCSI_DEBUG_OPT_MEDIUM_ERR, a medium error is simulated at this
 * sector on read commands: */
#define OPT_MEDIUM_ERR_ADDR   0x1234 /* that's sector 4660 in decimal */

/* If REPORT LUNS has luns >= 256 it can choose "flat space" (value 1)
 * or "peripheral device" addressing (value 0) */
#define SAM2_LUN_ADDRESS_METHOD 0

static int scsi_debug_add_host = DEF_NUM_HOST;
static int scsi_debug_delay = DEF_DELAY;
static int scsi_debug_dev_size_mb = DEF_DEV_SIZE_MB;
static int scsi_debug_every_nth = DEF_EVERY_NTH;
static int scsi_debug_max_luns = DEF_MAX_LUNS;
static int scsi_debug_num_parts = DEF_NUM_PARTS;
static int scsi_debug_num_tgts = DEF_NUM_TGTS; /* targets per host */
static int scsi_debug_opts = DEF_OPTS;
static int scsi_debug_scsi_level = DEF_SCSI_LEVEL;
static int scsi_debug_ptype = DEF_PTYPE; /* SCSI peripheral type (0==disk) */
static int scsi_debug_dsense = DEF_D_SENSE;

static int scsi_debug_cmnd_count = 0;

#define DEV_READONLY(TGT)      (0)
#define DEV_REMOVEABLE(TGT)    (0)

static unsigned long sdebug_store_size;	/* in bytes */
static sector_t sdebug_capacity;	/* in sectors */

/* old BIOS stuff, kernel may get rid of them but some mode sense pages
   may still need them */
static int sdebug_heads;		/* heads per disk */
static int sdebug_cylinders_per;	/* cylinders per surface */
static int sdebug_sectors_per;		/* sectors per cylinder */

/* default sector size is 512 bytes, 2**9 bytes */
#define POW2_SECT_SIZE 9
#define SECT_SIZE (1 << POW2_SECT_SIZE)
#define SECT_SIZE_PER(TGT) SECT_SIZE

#define SDEBUG_MAX_PARTS 4

#define SDEBUG_SENSE_LEN 32

struct sdebug_dev_info {
	struct list_head dev_list;
	unsigned char sense_buff[SDEBUG_SENSE_LEN];	/* weak nexus */
	unsigned int channel;
	unsigned int target;
	unsigned int lun;
	struct sdebug_host_info *sdbg_host;
	char reset;
	char used;
};

struct sdebug_host_info {
	struct list_head host_list;
	struct Scsi_Host *shost;
	struct device dev;
	struct list_head dev_info_list;
};

#define to_sdebug_host(d)	\
	container_of(d, struct sdebug_host_info, dev)

static LIST_HEAD(sdebug_host_list);
static DEFINE_SPINLOCK(sdebug_host_list_lock);

typedef void (* done_funct_t) (struct scsi_cmnd *);

struct sdebug_queued_cmd {
	int in_use;
	struct timer_list cmnd_timer;
	done_funct_t done_funct;
	struct scsi_cmnd * a_cmnd;
	int scsi_result;
};
static struct sdebug_queued_cmd queued_arr[SCSI_DEBUG_CANQUEUE];

static Scsi_Host_Template sdebug_driver_template = {
	.proc_info =		scsi_debug_proc_info,
	.name =			"SCSI DEBUG",
	.info =			scsi_debug_info,
	.slave_alloc =		scsi_debug_slave_alloc,
	.slave_configure =	scsi_debug_slave_configure,
	.slave_destroy =	scsi_debug_slave_destroy,
	.ioctl =		scsi_debug_ioctl,
	.queuecommand =		scsi_debug_queuecommand,
	.eh_abort_handler =	scsi_debug_abort,
	.eh_bus_reset_handler = scsi_debug_bus_reset,
	.eh_device_reset_handler = scsi_debug_device_reset,
	.eh_host_reset_handler = scsi_debug_host_reset,
	.bios_param =		scsi_debug_biosparam,
	.can_queue =		SCSI_DEBUG_CANQUEUE,
	.this_id =		7,
	.sg_tablesize =		64,
	.cmd_per_lun =		3,
	.max_sectors =		4096,
	.unchecked_isa_dma = 	0,
	.use_clustering = 	DISABLE_CLUSTERING,
	.module =		THIS_MODULE,
};

static unsigned char * fake_storep;	/* ramdisk storage */

static int num_aborts = 0;
static int num_dev_resets = 0;
static int num_bus_resets = 0;
static int num_host_resets = 0;

static DEFINE_SPINLOCK(queued_arr_lock);
static DEFINE_RWLOCK(atomic_rw);

static char sdebug_proc_name[] = "scsi_debug";

static int sdebug_driver_probe(struct device *);
static int sdebug_driver_remove(struct device *);
static struct bus_type pseudo_lld_bus;

static struct device_driver sdebug_driverfs_driver = {
	.name 		= sdebug_proc_name,
	.bus		= &pseudo_lld_bus,
	.probe          = sdebug_driver_probe,
	.remove         = sdebug_driver_remove,
};

static const int check_condition_result =
		(DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;

/* function declarations */
static int resp_inquiry(struct scsi_cmnd * SCpnt, int target,
			struct sdebug_dev_info * devip);
static int resp_requests(struct scsi_cmnd * SCpnt,
			 struct sdebug_dev_info * devip);
static int resp_readcap(struct scsi_cmnd * SCpnt,
			struct sdebug_dev_info * devip);
static int resp_mode_sense(struct scsi_cmnd * SCpnt, int target,
			   struct sdebug_dev_info * devip);
static int resp_read(struct scsi_cmnd * SCpnt, int upper_blk, int block,
		     int num, struct sdebug_dev_info * devip);
static int resp_write(struct scsi_cmnd * SCpnt, int upper_blk, int block,
		      int num, struct sdebug_dev_info * devip);
static int resp_report_luns(struct scsi_cmnd * SCpnt,
			    struct sdebug_dev_info * devip);
static int fill_from_dev_buffer(struct scsi_cmnd * scp, unsigned char * arr,
                                int arr_len);
static int fetch_to_dev_buffer(struct scsi_cmnd * scp, unsigned char * arr,
                               int max_arr_len);
static void timer_intr_handler(unsigned long);
static struct sdebug_dev_info * devInfoReg(struct scsi_device * sdev);
static void mk_sense_buffer(struct sdebug_dev_info * devip, int key,
			    int asc, int asq);
static int check_reset(struct scsi_cmnd * SCpnt,
		       struct sdebug_dev_info * devip);
static int schedule_resp(struct scsi_cmnd * cmnd,
			 struct sdebug_dev_info * devip,
			 done_funct_t done, int scsi_result, int delta_jiff);
static void __init sdebug_build_parts(unsigned char * ramp);
static void __init init_all_queued(void);
static void stop_all_queued(void);
static int stop_queued_cmnd(struct scsi_cmnd * cmnd);
static int inquiry_evpd_83(unsigned char * arr, int dev_id_num,
                           const char * dev_id_str, int dev_id_str_len);
static void do_create_driverfs_files(void);
static void do_remove_driverfs_files(void);

static int sdebug_add_adapter(void);
static void sdebug_remove_adapter(void);
static void sdebug_max_tgts_luns(void);

static struct device pseudo_primary;
static struct bus_type pseudo_lld_bus;


static
int scsi_debug_queuecommand(struct scsi_cmnd * SCpnt, done_funct_t done)
{
	unsigned char *cmd = (unsigned char *) SCpnt->cmnd;
	int block, upper_blk, num, k;
	int errsts = 0;
	int target = SCpnt->device->id;
	struct sdebug_dev_info * devip = NULL;
	int inj_recovered = 0;

	if (done == NULL)
		return 0;	/* assume mid level reprocessing command */

	if ((SCSI_DEBUG_OPT_NOISE & scsi_debug_opts) && cmd) {
		printk(KERN_INFO "scsi_debug: cmd ");
		for (k = 0, num = SCpnt->cmd_len; k < num; ++k)
			printk("%02x ", (int)cmd[k]);
		printk("\n");
	}
        if(target == sdebug_driver_template.this_id) {
		printk(KERN_INFO "scsi_debug: initiator's id used as "
		       "target!\n");
		return schedule_resp(SCpnt, NULL, done,
				     DID_NO_CONNECT << 16, 0);
        }

	if (SCpnt->device->lun >= scsi_debug_max_luns)
		return schedule_resp(SCpnt, NULL, done,
				     DID_NO_CONNECT << 16, 0);
	devip = devInfoReg(SCpnt->device);
	if (NULL == devip)
		return schedule_resp(SCpnt, NULL, done,
				     DID_NO_CONNECT << 16, 0);

        if ((scsi_debug_every_nth != 0) &&
            (++scsi_debug_cmnd_count >= abs(scsi_debug_every_nth))) {
                scsi_debug_cmnd_count = 0;
		if (scsi_debug_every_nth < -1)
			scsi_debug_every_nth = -1;
		if (SCSI_DEBUG_OPT_TIMEOUT & scsi_debug_opts)
			return 0; /* ignore command causing timeout */
		else if (SCSI_DEBUG_OPT_RECOVERED_ERR & scsi_debug_opts)
			inj_recovered = 1; /* to reads and writes below */
        }

	switch (*cmd) {
	case INQUIRY:     /* mandatory, ignore unit attention */
		errsts = resp_inquiry(SCpnt, target, devip);
		break;
	case REQUEST_SENSE:	/* mandatory, ignore unit attention */
		errsts = resp_requests(SCpnt, devip);
		break;
	case REZERO_UNIT:	/* actually this is REWIND for SSC */
	case START_STOP:
		errsts = check_reset(SCpnt, devip);
		break;
	case ALLOW_MEDIUM_REMOVAL:
		if ((errsts = check_reset(SCpnt, devip)))
			break;
		if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
			printk(KERN_INFO "scsi_debug: Medium removal %s\n",
			        cmd[4] ? "inhibited" : "enabled");
		break;
	case SEND_DIAGNOSTIC:     /* mandatory */
		errsts = check_reset(SCpnt, devip);
		break;
	case TEST_UNIT_READY:     /* mandatory */
		errsts = check_reset(SCpnt, devip);
		break;
        case RESERVE:
		errsts = check_reset(SCpnt, devip);
                break;
        case RESERVE_10:
		errsts = check_reset(SCpnt, devip);
                break;
        case RELEASE:
		errsts = check_reset(SCpnt, devip);
                break;
        case RELEASE_10:
		errsts = check_reset(SCpnt, devip);
                break;
	case READ_CAPACITY:
		errsts = resp_readcap(SCpnt, devip);
		break;
	case READ_16:
	case READ_12:
	case READ_10:
	case READ_6:
		if ((errsts = check_reset(SCpnt, devip)))
			break;
		upper_blk = 0;
		if ((*cmd) == READ_16) {
			upper_blk = cmd[5] + (cmd[4] << 8) +
				    (cmd[3] << 16) + (cmd[2] << 24);
			block = cmd[9] + (cmd[8] << 8) +
				(cmd[7] << 16) + (cmd[6] << 24);
			num = cmd[13] + (cmd[12] << 8) +
				(cmd[11] << 16) + (cmd[10] << 24);
		} else if ((*cmd) == READ_12) {
			block = cmd[5] + (cmd[4] << 8) +
				(cmd[3] << 16) + (cmd[2] << 24);
			num = cmd[9] + (cmd[8] << 8) +
				(cmd[7] << 16) + (cmd[6] << 24);
		} else if ((*cmd) == READ_10) {
			block = cmd[5] + (cmd[4] << 8) +
				(cmd[3] << 16) + (cmd[2] << 24);
			num = cmd[8] + (cmd[7] << 8);
		} else {
			block = cmd[3] + (cmd[2] << 8) +
				((cmd[1] & 0x1f) << 16);
			num = cmd[4];
		}
		errsts = resp_read(SCpnt, upper_blk, block, num, devip);
		if (inj_recovered && (0 == errsts)) {
			mk_sense_buffer(devip, RECOVERED_ERROR,
					THRESHHOLD_EXCEEDED, 0);
			errsts = check_condition_result;
		}
		break;
	case REPORT_LUNS:	/* mandatory, ignore unit attention */
		errsts = resp_report_luns(SCpnt, devip);
		break;
	case VERIFY:		/* 10 byte SBC-2 command */
		errsts = check_reset(SCpnt, devip);
		break;
	case WRITE_16:
	case WRITE_12:
	case WRITE_10:
	case WRITE_6:
		if ((errsts = check_reset(SCpnt, devip)))
			break;
		upper_blk = 0;
		if ((*cmd) == WRITE_16) {
			upper_blk = cmd[5] + (cmd[4] << 8) +
				    (cmd[3] << 16) + (cmd[2] << 24);
			block = cmd[9] + (cmd[8] << 8) +
				(cmd[7] << 16) + (cmd[6] << 24);
			num = cmd[13] + (cmd[12] << 8) +
				(cmd[11] << 16) + (cmd[10] << 24);
		} else if ((*cmd) == WRITE_12) {
			block = cmd[5] + (cmd[4] << 8) +
				(cmd[3] << 16) + (cmd[2] << 24);
			num = cmd[9] + (cmd[8] << 8) +
				(cmd[7] << 16) + (cmd[6] << 24);
		} else if ((*cmd) == WRITE_10) {
			block = cmd[5] + (cmd[4] << 8) +
				(cmd[3] << 16) + (cmd[2] << 24);
			num = cmd[8] + (cmd[7] << 8);
		} else {
			block = cmd[3] + (cmd[2] << 8) +
				((cmd[1] & 0x1f) << 16);
			num = cmd[4];
		}
		errsts = resp_write(SCpnt, upper_blk, block, num, devip);
		if (inj_recovered && (0 == errsts)) {
			mk_sense_buffer(devip, RECOVERED_ERROR,
					THRESHHOLD_EXCEEDED, 0);
			errsts = check_condition_result;
		}
		break;
	case MODE_SENSE:
	case MODE_SENSE_10:
		errsts = resp_mode_sense(SCpnt, target, devip);
		break;
	case SYNCHRONIZE_CACHE:
		errsts = check_reset(SCpnt, devip);
		break;
	default:
		if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
			printk(KERN_INFO "scsi_debug: Opcode: 0x%x not "
			       "supported\n", *cmd);
		if ((errsts = check_reset(SCpnt, devip)))
			break;	/* Unit attention takes precedence */
		mk_sense_buffer(devip, ILLEGAL_REQUEST, INVALID_OPCODE, 0);
		errsts = check_condition_result;
		break;
	}
	return schedule_resp(SCpnt, devip, done, errsts, scsi_debug_delay);
}

static int scsi_debug_ioctl(struct scsi_device *dev, int cmd, void __user *arg)
{
	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts) {
		printk(KERN_INFO "scsi_debug: ioctl: cmd=0x%x\n", cmd);
	}
	return -EINVAL;
	/* return -ENOTTY; // correct return but upsets fdisk */
}

static int check_reset(struct scsi_cmnd * SCpnt, struct sdebug_dev_info * devip)
{
	if (devip->reset) {
		if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
			printk(KERN_INFO "scsi_debug: Reporting Unit "
			       "attention: power on reset\n");
		devip->reset = 0;
		mk_sense_buffer(devip, UNIT_ATTENTION, POWERON_RESET, 0);
		return check_condition_result;
	}
	return 0;
}

/* Returns 0 if ok else (DID_ERROR << 16). Sets scp->resid . */
static int fill_from_dev_buffer(struct scsi_cmnd * scp, unsigned char * arr,
				int arr_len)
{
	int k, req_len, act_len, len, active;
	void * kaddr;
	void * kaddr_off;
	struct scatterlist * sgpnt;

	if (0 == scp->request_bufflen)
		return 0;
	if (NULL == scp->request_buffer)
		return (DID_ERROR << 16);
	if (! ((scp->sc_data_direction == DMA_BIDIRECTIONAL) ||
	      (scp->sc_data_direction == DMA_FROM_DEVICE)))
		return (DID_ERROR << 16);
	if (0 == scp->use_sg) {
		req_len = scp->request_bufflen;
		act_len = (req_len < arr_len) ? req_len : arr_len;
		memcpy(scp->request_buffer, arr, act_len);
		scp->resid = req_len - act_len;
		return 0;
	}
	sgpnt = (struct scatterlist *)scp->request_buffer;
	active = 1;
	for (k = 0, req_len = 0, act_len = 0; k < scp->use_sg; ++k, ++sgpnt) {
		if (active) {
			kaddr = (unsigned char *)
				kmap_atomic(sgpnt->page, KM_USER0);
			if (NULL == kaddr)
				return (DID_ERROR << 16);
			kaddr_off = (unsigned char *)kaddr + sgpnt->offset;
			len = sgpnt->length;
			if ((req_len + len) > arr_len) {
				active = 0;
				len = arr_len - req_len;
			}
			memcpy(kaddr_off, arr + req_len, len);
			kunmap_atomic(kaddr, KM_USER0);
			act_len += len;
		}
		req_len += sgpnt->length;
	}
	scp->resid = req_len - act_len;
	return 0;
}

/* Returns number of bytes fetched into 'arr' or -1 if error. */
static int fetch_to_dev_buffer(struct scsi_cmnd * scp, unsigned char * arr,
			       int max_arr_len)
{
	int k, req_len, len, fin;
	void * kaddr;
	void * kaddr_off;
	struct scatterlist * sgpnt;

	if (0 == scp->request_bufflen)
		return 0;
	if (NULL == scp->request_buffer)
		return -1;
	if (! ((scp->sc_data_direction == DMA_BIDIRECTIONAL) ||
	      (scp->sc_data_direction == DMA_TO_DEVICE)))
		return -1;
	if (0 == scp->use_sg) {
		req_len = scp->request_bufflen;
		len = (req_len < max_arr_len) ? req_len : max_arr_len;
		memcpy(arr, scp->request_buffer, len);
		return len;
	}
	sgpnt = (struct scatterlist *)scp->request_buffer;
	for (k = 0, req_len = 0, fin = 0; k < scp->use_sg; ++k, ++sgpnt) {
		kaddr = (unsigned char *)kmap_atomic(sgpnt->page, KM_USER0);
		if (NULL == kaddr)
			return -1;
		kaddr_off = (unsigned char *)kaddr + sgpnt->offset;
		len = sgpnt->length;
		if ((req_len + len) > max_arr_len) {
			len = max_arr_len - req_len;
			fin = 1;
		}
		memcpy(arr + req_len, kaddr_off, len);
		kunmap_atomic(kaddr, KM_USER0);
		if (fin)
			return req_len + len;
		req_len += sgpnt->length;
	}
	return req_len;
}


static const char * inq_vendor_id = "Linux   ";
static const char * inq_product_id = "scsi_debug      ";
static const char * inq_product_rev = "0004";

static int inquiry_evpd_83(unsigned char * arr, int dev_id_num,
			   const char * dev_id_str, int dev_id_str_len)
{
	int num;

	/* Two identification descriptors: */
	/* T10 vendor identifier field format (faked) */
	arr[0] = 0x2;	/* ASCII */
	arr[1] = 0x1;
	arr[2] = 0x0;
	memcpy(&arr[4], inq_vendor_id, 8);
	memcpy(&arr[12], inq_product_id, 16);
	memcpy(&arr[28], dev_id_str, dev_id_str_len);
	num = 8 + 16 + dev_id_str_len;
	arr[3] = num;
	num += 4;
	/* NAA IEEE registered identifier (faked) */
	arr[num] = 0x1;	/* binary */
	arr[num + 1] = 0x3;
	arr[num + 2] = 0x0;
	arr[num + 3] = 0x8;
	arr[num + 4] = 0x51;	/* ieee company id=0x123456 (faked) */
	arr[num + 5] = 0x23;
	arr[num + 6] = 0x45;
	arr[num + 7] = 0x60;
	arr[num + 8] = (dev_id_num >> 24);
	arr[num + 9] = (dev_id_num >> 16) & 0xff;
	arr[num + 10] = (dev_id_num >> 8) & 0xff;
	arr[num + 11] = dev_id_num & 0xff;
	return num + 12;
}


#define SDEBUG_LONG_INQ_SZ 96
#define SDEBUG_MAX_INQ_ARR_SZ 128

static int resp_inquiry(struct scsi_cmnd * scp, int target,
			struct sdebug_dev_info * devip)
{
	unsigned char pq_pdt;
	unsigned char arr[SDEBUG_MAX_INQ_ARR_SZ];
	unsigned char *cmd = (unsigned char *)scp->cmnd;
	int alloc_len;

	alloc_len = (cmd[3] << 8) + cmd[4];
	memset(arr, 0, SDEBUG_MAX_INQ_ARR_SZ);
	pq_pdt = (scsi_debug_ptype & 0x1f);
	arr[0] = pq_pdt;
	if (0x2 & cmd[1]) {  /* CMDDT bit set */
		mk_sense_buffer(devip, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB,
			       	0);
		return check_condition_result;
	} else if (0x1 & cmd[1]) {  /* EVPD bit set */
		int dev_id_num, len;
		char dev_id_str[6];
		
		dev_id_num = ((devip->sdbg_host->shost->host_no + 1) * 2000) +
			     (devip->target * 1000) + devip->lun;
		len = scnprintf(dev_id_str, 6, "%d", dev_id_num);
		if (0 == cmd[2]) { /* supported vital product data pages */
			arr[3] = 3;
			arr[4] = 0x0; /* this page */
			arr[5] = 0x80; /* unit serial number */
			arr[6] = 0x83; /* device identification */
		} else if (0x80 == cmd[2]) { /* unit serial number */
			arr[1] = 0x80;
			arr[3] = len;
			memcpy(&arr[4], dev_id_str, len);
		} else if (0x83 == cmd[2]) { /* device identification */
			arr[1] = 0x83;
			arr[3] = inquiry_evpd_83(&arr[4], dev_id_num,
						 dev_id_str, len);
		} else {
			/* Illegal request, invalid field in cdb */
			mk_sense_buffer(devip, ILLEGAL_REQUEST,
					INVALID_FIELD_IN_CDB, 0);
			return check_condition_result;
		}
		return fill_from_dev_buffer(scp, arr,
			    min(alloc_len, SDEBUG_MAX_INQ_ARR_SZ));
	}
	/* drops through here for a standard inquiry */
	arr[1] = DEV_REMOVEABLE(target) ? 0x80 : 0;	/* Removable disk */
	arr[2] = scsi_debug_scsi_level;
	arr[3] = 2;    /* response_data_format==2 */
	arr[4] = SDEBUG_LONG_INQ_SZ - 5;
	arr[6] = 0x1; /* claim: ADDR16 */
	/* arr[6] |= 0x40; ... claim: EncServ (enclosure services) */
	arr[7] = 0x3a; /* claim: WBUS16, SYNC, LINKED + CMDQUE */
	memcpy(&arr[8], inq_vendor_id, 8);
	memcpy(&arr[16], inq_product_id, 16);
	memcpy(&arr[32], inq_product_rev, 4);
	/* version descriptors (2 bytes each) follow */
	arr[58] = 0x0; arr[59] = 0x40; /* SAM-2 */
	arr[60] = 0x3; arr[61] = 0x0;  /* SPC-3 */
	if (scsi_debug_ptype == 0) {
		arr[62] = 0x1; arr[63] = 0x80; /* SBC */
	} else if (scsi_debug_ptype == 1) {
		arr[62] = 0x2; arr[63] = 0x00; /* SSC */
	}
	return fill_from_dev_buffer(scp, arr,
			    min(alloc_len, SDEBUG_LONG_INQ_SZ));
}

static int resp_requests(struct scsi_cmnd * scp,
			 struct sdebug_dev_info * devip)
{
	unsigned char * sbuff;
	unsigned char *cmd = (unsigned char *)scp->cmnd;
	unsigned char arr[SDEBUG_SENSE_LEN];
	int len = 18;

	memset(arr, 0, SDEBUG_SENSE_LEN);
	if (devip->reset == 1)
		mk_sense_buffer(devip, 0, NO_ADDED_SENSE, 0);
	sbuff = devip->sense_buff;
	if ((cmd[1] & 1) && (! scsi_debug_dsense)) {
		/* DESC bit set and sense_buff in fixed format */
		arr[0] = 0x72;
		arr[1] = sbuff[2];     /* sense key */
		arr[2] = sbuff[12];    /* asc */
		arr[3] = sbuff[13];    /* ascq */
		len = 8;
	} else
		memcpy(arr, sbuff, SDEBUG_SENSE_LEN);
	mk_sense_buffer(devip, 0, NO_ADDED_SENSE, 0);
	return fill_from_dev_buffer(scp, arr, len);
}

#define SDEBUG_READCAP_ARR_SZ 8
static int resp_readcap(struct scsi_cmnd * scp,
			struct sdebug_dev_info * devip)
{
	unsigned char arr[SDEBUG_READCAP_ARR_SZ];
	unsigned long capac;
	int errsts;

	if ((errsts = check_reset(scp, devip)))
		return errsts;
	memset(arr, 0, SDEBUG_READCAP_ARR_SZ);
	capac = (unsigned long)sdebug_capacity - 1;
	arr[0] = (capac >> 24);
	arr[1] = (capac >> 16) & 0xff;
	arr[2] = (capac >> 8) & 0xff;
	arr[3] = capac & 0xff;
	arr[6] = (SECT_SIZE_PER(target) >> 8) & 0xff;
	arr[7] = SECT_SIZE_PER(target) & 0xff;
	return fill_from_dev_buffer(scp, arr, SDEBUG_READCAP_ARR_SZ);
}

/* <<Following mode page info copied from ST318451LW>> */

static int resp_err_recov_pg(unsigned char * p, int pcontrol, int target)
{	/* Read-Write Error Recovery page for mode_sense */
	unsigned char err_recov_pg[] = {0x1, 0xa, 0xc0, 11, 240, 0, 0, 0,
					5, 0, 0xff, 0xff};

	memcpy(p, err_recov_pg, sizeof(err_recov_pg));
	if (1 == pcontrol)
		memset(p + 2, 0, sizeof(err_recov_pg) - 2);
	return sizeof(err_recov_pg);
}

static int resp_disconnect_pg(unsigned char * p, int pcontrol, int target)
{ 	/* Disconnect-Reconnect page for mode_sense */
	unsigned char disconnect_pg[] = {0x2, 0xe, 128, 128, 0, 10, 0, 0,
					 0, 0, 0, 0, 0, 0, 0, 0};

	memcpy(p, disconnect_pg, sizeof(disconnect_pg));
	if (1 == pcontrol)
		memset(p + 2, 0, sizeof(disconnect_pg) - 2);
	return sizeof(disconnect_pg);
}

static int resp_format_pg(unsigned char * p, int pcontrol, int target)
{       /* Format device page for mode_sense */
        unsigned char format_pg[] = {0x3, 0x16, 0, 0, 0, 0, 0, 0,
                                     0, 0, 0, 0, 0, 0, 0, 0,
                                     0, 0, 0, 0, 0x40, 0, 0, 0};

        memcpy(p, format_pg, sizeof(format_pg));
        p[10] = (sdebug_sectors_per >> 8) & 0xff;
        p[11] = sdebug_sectors_per & 0xff;
        p[12] = (SECT_SIZE >> 8) & 0xff;
        p[13] = SECT_SIZE & 0xff;
        if (DEV_REMOVEABLE(target))
                p[20] |= 0x20; /* should agree with INQUIRY */
        if (1 == pcontrol)
                memset(p + 2, 0, sizeof(format_pg) - 2);
        return sizeof(format_pg);
}

static int resp_caching_pg(unsigned char * p, int pcontrol, int target)
{ 	/* Caching page for mode_sense */
	unsigned char caching_pg[] = {0x8, 18, 0x14, 0, 0xff, 0xff, 0, 0,
		0xff, 0xff, 0xff, 0xff, 0x80, 0x14, 0, 0,     0, 0, 0, 0};

	memcpy(p, caching_pg, sizeof(caching_pg));
	if (1 == pcontrol)
		memset(p + 2, 0, sizeof(caching_pg) - 2);
	return sizeof(caching_pg);
}

static int resp_ctrl_m_pg(unsigned char * p, int pcontrol, int target)
{ 	/* Control mode page for mode_sense */
	unsigned char ctrl_m_pg[] = {0xa, 10, 2, 0, 0, 0, 0, 0,
				     0, 0, 0x2, 0x4b};

	if (scsi_debug_dsense)
		ctrl_m_pg[2] |= 0x4;
	memcpy(p, ctrl_m_pg, sizeof(ctrl_m_pg));
	if (1 == pcontrol)
		memset(p + 2, 0, sizeof(ctrl_m_pg) - 2);
	return sizeof(ctrl_m_pg);
}

static int resp_iec_m_pg(unsigned char * p, int pcontrol, int target)
{	/* Informational Exceptions control mode page for mode_sense */
	unsigned char iec_m_pg[] = {0x1c, 0xa, 0x08, 0, 0, 0, 0, 0,
				    0, 0, 0x0, 0x0};
	memcpy(p, iec_m_pg, sizeof(iec_m_pg));
	if (1 == pcontrol)
		memset(p + 2, 0, sizeof(iec_m_pg) - 2);
	return sizeof(iec_m_pg);
}

#define SDEBUG_MAX_MSENSE_SZ 256

static int resp_mode_sense(struct scsi_cmnd * scp, int target,
			   struct sdebug_dev_info * devip)
{
	unsigned char dbd;
	int pcontrol, pcode, subpcode;
	unsigned char dev_spec;
	int alloc_len, msense_6, offset, len, errsts;
	unsigned char * ap;
	unsigned char arr[SDEBUG_MAX_MSENSE_SZ];
	unsigned char *cmd = (unsigned char *)scp->cmnd;

	if ((errsts = check_reset(scp, devip)))
		return errsts;
	dbd = cmd[1] & 0x8;
	pcontrol = (cmd[2] & 0xc0) >> 6;
	pcode = cmd[2] & 0x3f;
	subpcode = cmd[3];
	msense_6 = (MODE_SENSE == cmd[0]);
	alloc_len = msense_6 ? cmd[4] : ((cmd[7] << 8) | cmd[8]);
	memset(arr, 0, SDEBUG_MAX_MSENSE_SZ);
	if (0x3 == pcontrol) {  /* Saving values not supported */
		mk_sense_buffer(devip, ILLEGAL_REQUEST, SAVING_PARAMS_UNSUP,
			       	0);
		return check_condition_result;
	}
	dev_spec = DEV_READONLY(target) ? 0x80 : 0x0;
	if (msense_6) {
		arr[2] = dev_spec;
		offset = 4;
	} else {
		arr[3] = dev_spec;
		offset = 8;
	}
	ap = arr + offset;

	if (0 != subpcode) { /* TODO: Control Extension page */
		mk_sense_buffer(devip, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB,
			       	0);
		return check_condition_result;
	}
	switch (pcode) {
	case 0x1:	/* Read-Write error recovery page, direct access */
		len = resp_err_recov_pg(ap, pcontrol, target);
		offset += len;
		break;
	case 0x2:	/* Disconnect-Reconnect page, all devices */
		len = resp_disconnect_pg(ap, pcontrol, target);
		offset += len;
		break;
        case 0x3:       /* Format device page, direct access */
                len = resp_format_pg(ap, pcontrol, target);
                offset += len;
                break;
	case 0x8:	/* Caching page, direct access */
		len = resp_caching_pg(ap, pcontrol, target);
		offset += len;
		break;
	case 0xa:	/* Control Mode page, all devices */
		len = resp_ctrl_m_pg(ap, pcontrol, target);
		offset += len;
		break;
	case 0x1c:	/* Informational Exceptions Mode page, all devices */
		len = resp_iec_m_pg(ap, pcontrol, target);
		offset += len;
		break;
	case 0x3f:	/* Read all Mode pages */
		len = resp_err_recov_pg(ap, pcontrol, target);
		len += resp_disconnect_pg(ap + len, pcontrol, target);
		len += resp_format_pg(ap + len, pcontrol, target);
		len += resp_caching_pg(ap + len, pcontrol, target);
		len += resp_ctrl_m_pg(ap + len, pcontrol, target);
		len += resp_iec_m_pg(ap + len, pcontrol, target);
		offset += len;
		break;
	default:
		mk_sense_buffer(devip, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB,
			       	0);
		return check_condition_result;
	}
	if (msense_6)
		arr[0] = offset - 1;
	else {
		arr[0] = ((offset - 2) >> 8) & 0xff;
		arr[1] = (offset - 2) & 0xff;
	}
	return fill_from_dev_buffer(scp, arr, min(alloc_len, offset));
}

static int resp_read(struct scsi_cmnd * SCpnt, int upper_blk, int block,
		     int num, struct sdebug_dev_info * devip)
{
	unsigned long iflags;
	int ret;

	if (upper_blk || (block + num > sdebug_capacity)) {
		mk_sense_buffer(devip, ILLEGAL_REQUEST, ADDR_OUT_OF_RANGE,
				0);
		return check_condition_result;
	}
	if ((SCSI_DEBUG_OPT_MEDIUM_ERR & scsi_debug_opts) &&
	    (block <= OPT_MEDIUM_ERR_ADDR) &&
	    ((block + num) > OPT_MEDIUM_ERR_ADDR)) {
		mk_sense_buffer(devip, MEDIUM_ERROR, UNRECOVERED_READ_ERR,
				0);
		/* claim unrecoverable read error */
		return check_condition_result;
	}
	read_lock_irqsave(&atomic_rw, iflags);
	ret = fill_from_dev_buffer(SCpnt, fake_storep + (block * SECT_SIZE),
			   	   num * SECT_SIZE);
	read_unlock_irqrestore(&atomic_rw, iflags);
	return ret;
}

static int resp_write(struct scsi_cmnd * SCpnt, int upper_blk, int block,
		      int num, struct sdebug_dev_info * devip)
{
	unsigned long iflags;
	int res;

	if (upper_blk || (block + num > sdebug_capacity)) {
		mk_sense_buffer(devip, ILLEGAL_REQUEST, ADDR_OUT_OF_RANGE,
			       	0);
		return check_condition_result;
	}

	write_lock_irqsave(&atomic_rw, iflags);
	res = fetch_to_dev_buffer(SCpnt, fake_storep + (block * SECT_SIZE),
			   	  num * SECT_SIZE);
	write_unlock_irqrestore(&atomic_rw, iflags);
	if (-1 == res)
		return (DID_ERROR << 16);
	else if ((res < (num * SECT_SIZE)) &&
		 (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts))
		printk(KERN_INFO "scsi_debug: write: cdb indicated=%d, "
		       " IO sent=%d bytes\n", num * SECT_SIZE, res);
	return 0;
}

#define SDEBUG_RLUN_ARR_SZ 128

static int resp_report_luns(struct scsi_cmnd * scp,
			    struct sdebug_dev_info * devip)
{
	unsigned int alloc_len;
	int lun_cnt, i, upper;
	unsigned char *cmd = (unsigned char *)scp->cmnd;
	int select_report = (int)cmd[2];
	struct scsi_lun *one_lun;
	unsigned char arr[SDEBUG_RLUN_ARR_SZ];

	alloc_len = cmd[9] + (cmd[8] << 8) + (cmd[7] << 16) + (cmd[6] << 24);
	if ((alloc_len < 16) || (select_report > 2)) {
		mk_sense_buffer(devip, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB,
			       	0);
		return check_condition_result;
	}
	/* can produce response with up to 16k luns (lun 0 to lun 16383) */
	memset(arr, 0, SDEBUG_RLUN_ARR_SZ);
	lun_cnt = scsi_debug_max_luns;
	arr[2] = ((sizeof(struct scsi_lun) * lun_cnt) >> 8) & 0xff;
	arr[3] = (sizeof(struct scsi_lun) * lun_cnt) & 0xff;
	lun_cnt = min((int)((SDEBUG_RLUN_ARR_SZ - 8) /
			    sizeof(struct scsi_lun)), lun_cnt);
	one_lun = (struct scsi_lun *) &arr[8];
	for (i = 0; i < lun_cnt; i++) {
		upper = (i >> 8) & 0x3f;
		if (upper)
			one_lun[i].scsi_lun[0] =
			    (upper | (SAM2_LUN_ADDRESS_METHOD << 6));
		one_lun[i].scsi_lun[1] = i & 0xff;
	}
	return fill_from_dev_buffer(scp, arr,
				    min((int)alloc_len, SDEBUG_RLUN_ARR_SZ));
}

/* When timer goes off this function is called. */
static void timer_intr_handler(unsigned long indx)
{
	struct sdebug_queued_cmd * sqcp;
	unsigned long iflags;

	if (indx >= SCSI_DEBUG_CANQUEUE) {
		printk(KERN_ERR "scsi_debug:timer_intr_handler: indx too "
		       "large\n");
		return;
	}
	spin_lock_irqsave(&queued_arr_lock, iflags);
	sqcp = &queued_arr[(int)indx];
	if (! sqcp->in_use) {
		printk(KERN_ERR "scsi_debug:timer_intr_handler: Unexpected "
		       "interrupt\n");
		spin_unlock_irqrestore(&queued_arr_lock, iflags);
		return;
	}
	sqcp->in_use = 0;
	if (sqcp->done_funct) {
		sqcp->a_cmnd->result = sqcp->scsi_result;
		sqcp->done_funct(sqcp->a_cmnd); /* callback to mid level */
	}
	sqcp->done_funct = NULL;
	spin_unlock_irqrestore(&queued_arr_lock, iflags);
}

static int scsi_debug_slave_alloc(struct scsi_device * sdp)
{
	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: slave_alloc <%u %u %u %u>\n",
		       sdp->host->host_no, sdp->channel, sdp->id, sdp->lun);
	return 0;
}

static int scsi_debug_slave_configure(struct scsi_device * sdp)
{
	struct sdebug_dev_info * devip;

	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: slave_configure <%u %u %u %u>\n",
		       sdp->host->host_no, sdp->channel, sdp->id, sdp->lun);
	if (sdp->host->max_cmd_len != SCSI_DEBUG_MAX_CMD_LEN)
		sdp->host->max_cmd_len = SCSI_DEBUG_MAX_CMD_LEN;
	devip = devInfoReg(sdp);
	sdp->hostdata = devip;
	if (sdp->host->cmd_per_lun)
		scsi_adjust_queue_depth(sdp, SDEBUG_TAGGED_QUEUING,
					sdp->host->cmd_per_lun);
	return 0;
}

static void scsi_debug_slave_destroy(struct scsi_device * sdp)
{
	struct sdebug_dev_info * devip =
				(struct sdebug_dev_info *)sdp->hostdata;

	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: slave_destroy <%u %u %u %u>\n",
		       sdp->host->host_no, sdp->channel, sdp->id, sdp->lun);
	if (devip) {
		/* make this slot avaliable for re-use */
		devip->used = 0;
		sdp->hostdata = NULL;
	}
}

static struct sdebug_dev_info * devInfoReg(struct scsi_device * sdev)
{
	struct sdebug_host_info * sdbg_host;
	struct sdebug_dev_info * open_devip = NULL;
	struct sdebug_dev_info * devip =
			(struct sdebug_dev_info *)sdev->hostdata;

	if (devip)
		return devip;
	sdbg_host = *(struct sdebug_host_info **) sdev->host->hostdata;
        if(! sdbg_host) {
                printk(KERN_ERR "Host info NULL\n");
		return NULL;
        }
	list_for_each_entry(devip, &sdbg_host->dev_info_list, dev_list) {
		if ((devip->used) && (devip->channel == sdev->channel) &&
                    (devip->target == sdev->id) &&
                    (devip->lun == sdev->lun))
                        return devip;
		else {
			if ((!devip->used) && (!open_devip))
				open_devip = devip;
		}
	}
	if (NULL == open_devip) { /* try and make a new one */
		open_devip = kmalloc(sizeof(*open_devip),GFP_KERNEL);
		if (NULL == open_devip) {
			printk(KERN_ERR "%s: out of memory at line %d\n",
				__FUNCTION__, __LINE__);
			return NULL;
		}
		memset(open_devip, 0, sizeof(*open_devip));
		open_devip->sdbg_host = sdbg_host;
		list_add_tail(&open_devip->dev_list,
		&sdbg_host->dev_info_list);
	}
        if (open_devip) {
		open_devip->channel = sdev->channel;
		open_devip->target = sdev->id;
		open_devip->lun = sdev->lun;
		open_devip->sdbg_host = sdbg_host;
		open_devip->reset = 1;
		open_devip->used = 1;
		memset(open_devip->sense_buff, 0, SDEBUG_SENSE_LEN);
		if (scsi_debug_dsense)
			open_devip->sense_buff[0] = 0x72;
		else {
			open_devip->sense_buff[0] = 0x70;
			open_devip->sense_buff[7] = 0xa;
		}
		return open_devip;
        }
        return NULL;
}

static void mk_sense_buffer(struct sdebug_dev_info * devip, int key,
			    int asc, int asq)
{
	unsigned char * sbuff;

	sbuff = devip->sense_buff;
	memset(sbuff, 0, SDEBUG_SENSE_LEN);
	if (scsi_debug_dsense) {
		sbuff[0] = 0x72;  /* descriptor, current */
		sbuff[1] = key;
		sbuff[2] = asc;
		sbuff[3] = asq;
	} else {
		sbuff[0] = 0x70;  /* fixed, current */
		sbuff[2] = key;
		sbuff[7] = 0xa;	  /* implies 18 byte sense buffer */
		sbuff[12] = asc;
		sbuff[13] = asq;
	}
	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug:    [sense_key,asc,ascq]: "
		      "[0x%x,0x%x,0x%x]\n", key, asc, asq);
}

static int scsi_debug_abort(struct scsi_cmnd * SCpnt)
{
	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: abort\n");
	++num_aborts;
	stop_queued_cmnd(SCpnt);
	return SUCCESS;
}

static int scsi_debug_biosparam(struct scsi_device *sdev,
		struct block_device * bdev, sector_t capacity, int *info)
{
	int res;
	unsigned char *buf;

	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: biosparam\n");
	buf = scsi_bios_ptable(bdev);
	if (buf) {
		res = scsi_partsize(buf, capacity,
				    &info[2], &info[0], &info[1]);
		kfree(buf);
		if (! res)
			return res;
	}
	info[0] = sdebug_heads;
	info[1] = sdebug_sectors_per;
	info[2] = sdebug_cylinders_per;
	return 0;
}

static int scsi_debug_device_reset(struct scsi_cmnd * SCpnt)
{
	struct sdebug_dev_info * devip;

	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: device_reset\n");
	++num_dev_resets;
	if (SCpnt) {
		devip = devInfoReg(SCpnt->device);
		if (devip)
			devip->reset = 1;
	}
	return SUCCESS;
}

static int scsi_debug_bus_reset(struct scsi_cmnd * SCpnt)
{
	struct sdebug_host_info *sdbg_host;
        struct sdebug_dev_info * dev_info;
        struct scsi_device * sdp;
        struct Scsi_Host * hp;

	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: bus_reset\n");
	++num_bus_resets;
	if (SCpnt && ((sdp = SCpnt->device)) && ((hp = sdp->host))) {
		sdbg_host = *(struct sdebug_host_info **) hp->hostdata;
		if (sdbg_host) {
			list_for_each_entry(dev_info,
                                            &sdbg_host->dev_info_list,
                                            dev_list)
				dev_info->reset = 1;
		}
	}
	return SUCCESS;
}

static int scsi_debug_host_reset(struct scsi_cmnd * SCpnt)
{
	struct sdebug_host_info * sdbg_host;
        struct sdebug_dev_info * dev_info;

	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: host_reset\n");
	++num_host_resets;
        spin_lock(&sdebug_host_list_lock);
        list_for_each_entry(sdbg_host, &sdebug_host_list, host_list) {
                list_for_each_entry(dev_info, &sdbg_host->dev_info_list,
                                    dev_list)
                        dev_info->reset = 1;
        }
        spin_unlock(&sdebug_host_list_lock);
	stop_all_queued();
	return SUCCESS;
}

/* Returns 1 if found 'cmnd' and deleted its timer. else returns 0 */
static int stop_queued_cmnd(struct scsi_cmnd * cmnd)
{
	unsigned long iflags;
	int k;
	struct sdebug_queued_cmd * sqcp;

	spin_lock_irqsave(&queued_arr_lock, iflags);
	for (k = 0; k < SCSI_DEBUG_CANQUEUE; ++k) {
		sqcp = &queued_arr[k];
		if (sqcp->in_use && (cmnd == sqcp->a_cmnd)) {
			del_timer_sync(&sqcp->cmnd_timer);
			sqcp->in_use = 0;
			sqcp->a_cmnd = NULL;
			break;
		}
	}
	spin_unlock_irqrestore(&queued_arr_lock, iflags);
	return (k < SCSI_DEBUG_CANQUEUE) ? 1 : 0;
}

/* Deletes (stops) timers of all queued commands */
static void stop_all_queued(void)
{
	unsigned long iflags;
	int k;
	struct sdebug_queued_cmd * sqcp;

	spin_lock_irqsave(&queued_arr_lock, iflags);
	for (k = 0; k < SCSI_DEBUG_CANQUEUE; ++k) {
		sqcp = &queued_arr[k];
		if (sqcp->in_use && sqcp->a_cmnd) {
			del_timer_sync(&sqcp->cmnd_timer);
			sqcp->in_use = 0;
			sqcp->a_cmnd = NULL;
		}
	}
	spin_unlock_irqrestore(&queued_arr_lock, iflags);
}

/* Initializes timers in queued array */
static void __init init_all_queued(void)
{
	unsigned long iflags;
	int k;
	struct sdebug_queued_cmd * sqcp;

	spin_lock_irqsave(&queued_arr_lock, iflags);
	for (k = 0; k < SCSI_DEBUG_CANQUEUE; ++k) {
		sqcp = &queued_arr[k];
		init_timer(&sqcp->cmnd_timer);
		sqcp->in_use = 0;
		sqcp->a_cmnd = NULL;
	}
	spin_unlock_irqrestore(&queued_arr_lock, iflags);
}

static void __init sdebug_build_parts(unsigned char * ramp)
{
	struct partition * pp;
	int starts[SDEBUG_MAX_PARTS + 2];
	int sectors_per_part, num_sectors, k;
	int heads_by_sects, start_sec, end_sec;

	/* assume partition table already zeroed */
	if ((scsi_debug_num_parts < 1) || (sdebug_store_size < 1048576))
		return;
	if (scsi_debug_num_parts > SDEBUG_MAX_PARTS) {
		scsi_debug_num_parts = SDEBUG_MAX_PARTS;
		printk(KERN_WARNING "scsi_debug:build_parts: reducing "
				    "partitions to %d\n", SDEBUG_MAX_PARTS);
	}
	num_sectors = (int)(sdebug_store_size / SECT_SIZE);
	sectors_per_part = (num_sectors - sdebug_sectors_per)
			   / scsi_debug_num_parts;
	heads_by_sects = sdebug_heads * sdebug_sectors_per;
        starts[0] = sdebug_sectors_per;
	for (k = 1; k < scsi_debug_num_parts; ++k)
		starts[k] = ((k * sectors_per_part) / heads_by_sects)
			    * heads_by_sects;
	starts[scsi_debug_num_parts] = num_sectors;
	starts[scsi_debug_num_parts + 1] = 0;

	ramp[510] = 0x55;	/* magic partition markings */
	ramp[511] = 0xAA;
	pp = (struct partition *)(ramp + 0x1be);
	for (k = 0; starts[k + 1]; ++k, ++pp) {
		start_sec = starts[k];
		end_sec = starts[k + 1] - 1;
		pp->boot_ind = 0;

		pp->cyl = start_sec / heads_by_sects;
		pp->head = (start_sec - (pp->cyl * heads_by_sects))
			   / sdebug_sectors_per;
		pp->sector = (start_sec % sdebug_sectors_per) + 1;

		pp->end_cyl = end_sec / heads_by_sects;
		pp->end_head = (end_sec - (pp->end_cyl * heads_by_sects))
			       / sdebug_sectors_per;
		pp->end_sector = (end_sec % sdebug_sectors_per) + 1;

		pp->start_sect = start_sec;
		pp->nr_sects = end_sec - start_sec + 1;
		pp->sys_ind = 0x83;	/* plain Linux partition */
	}
}

static int schedule_resp(struct scsi_cmnd * cmnd,
			 struct sdebug_dev_info * devip,
			 done_funct_t done, int scsi_result, int delta_jiff)
{
	if ((SCSI_DEBUG_OPT_NOISE & scsi_debug_opts) && cmnd) {
		if (scsi_result) {
			struct scsi_device * sdp = cmnd->device;

			printk(KERN_INFO "scsi_debug:    <%u %u %u %u> "
			       "non-zero result=0x%x\n", sdp->host->host_no,
			       sdp->channel, sdp->id, sdp->lun, scsi_result);
		}
	}
	if (cmnd && devip) {
		/* simulate autosense by this driver */
		if (SAM_STAT_CHECK_CONDITION == (scsi_result & 0xff))
			memcpy(cmnd->sense_buffer, devip->sense_buff,
			       (SCSI_SENSE_BUFFERSIZE > SDEBUG_SENSE_LEN) ?
			       SDEBUG_SENSE_LEN : SCSI_SENSE_BUFFERSIZE);
	}
	if (delta_jiff <= 0) {
		if (cmnd)
			cmnd->result = scsi_result;
		if (done)
			done(cmnd);
		return 0;
	} else {
		unsigned long iflags;
		int k;
		struct sdebug_queued_cmd * sqcp = NULL;

		spin_lock_irqsave(&queued_arr_lock, iflags);
		for (k = 0; k < SCSI_DEBUG_CANQUEUE; ++k) {
			sqcp = &queued_arr[k];
			if (! sqcp->in_use)
				break;
		}
		if (k >= SCSI_DEBUG_CANQUEUE) {
			spin_unlock_irqrestore(&queued_arr_lock, iflags);
			printk(KERN_WARNING "scsi_debug: can_queue exceeded\n");
			return 1;	/* report busy to mid level */
		}
		sqcp->in_use = 1;
		sqcp->a_cmnd = cmnd;
		sqcp->scsi_result = scsi_result;
		sqcp->done_funct = done;
		sqcp->cmnd_timer.function = timer_intr_handler;
		sqcp->cmnd_timer.data = k;
		sqcp->cmnd_timer.expires = jiffies + delta_jiff;
		add_timer(&sqcp->cmnd_timer);
		spin_unlock_irqrestore(&queued_arr_lock, iflags);
		if (cmnd)
			cmnd->result = 0;
		return 0;
	}
}

/* Set 'perm' (4th argument) to 0 to disable module_param's definition
 * of sysfs parameters (which module_param doesn't yet support).
 * Sysfs parameters defined explicitly below.
 */
module_param_named(add_host, scsi_debug_add_host, int, 0); /* perm=0644 */
module_param_named(delay, scsi_debug_delay, int, 0); /* perm=0644 */
module_param_named(dev_size_mb, scsi_debug_dev_size_mb, int, 0);
module_param_named(dsense, scsi_debug_dsense, int, 0);
module_param_named(every_nth, scsi_debug_every_nth, int, 0);
module_param_named(max_luns, scsi_debug_max_luns, int, 0);
module_param_named(num_parts, scsi_debug_num_parts, int, 0);
module_param_named(num_tgts, scsi_debug_num_tgts, int, 0);
module_param_named(opts, scsi_debug_opts, int, 0); /* perm=0644 */
module_param_named(ptype, scsi_debug_ptype, int, 0);
module_param_named(scsi_level, scsi_debug_scsi_level, int, 0);

MODULE_AUTHOR("Eric Youngdale + Douglas Gilbert");
MODULE_DESCRIPTION("SCSI debug adapter driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(SCSI_DEBUG_VERSION);

MODULE_PARM_DESC(add_host, "0..127 hosts allowed(def=1)");
MODULE_PARM_DESC(delay, "# of jiffies to delay response(def=1)");
MODULE_PARM_DESC(dev_size_mb, "size in MB of ram shared by devs");
MODULE_PARM_DESC(dsense, "use descriptor sense format(def: fixed)");
MODULE_PARM_DESC(every_nth, "timeout every nth command(def=100)");
MODULE_PARM_DESC(max_luns, "number of SCSI LUNs per target to simulate");
MODULE_PARM_DESC(num_parts, "number of partitions(def=0)");
MODULE_PARM_DESC(num_tgts, "number of SCSI targets per host to simulate");
MODULE_PARM_DESC(opts, "1->noise, 2->medium_error, 4->...");
MODULE_PARM_DESC(ptype, "SCSI peripheral type(def=0[disk])");
MODULE_PARM_DESC(scsi_level, "SCSI level to simulate(def=5[SPC-3])");


static char sdebug_info[256];

static const char * scsi_debug_info(struct Scsi_Host * shp)
{
	sprintf(sdebug_info, "scsi_debug, version %s [%s], "
		"dev_size_mb=%d, opts=0x%x", SCSI_DEBUG_VERSION,
		scsi_debug_version_date, scsi_debug_dev_size_mb,
		scsi_debug_opts);
	return sdebug_info;
}

/* scsi_debug_proc_info
 * Used if the driver currently has no own support for /proc/scsi
 */
static int scsi_debug_proc_info(struct Scsi_Host *host, char *buffer, char **start, off_t offset,
				int length, int inout)
{
	int len, pos, begin;
	int orig_length;

	orig_length = length;

	if (inout == 1) {
		char arr[16];
		int minLen = length > 15 ? 15 : length;

		if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
			return -EACCES;
		memcpy(arr, buffer, minLen);
		arr[minLen] = '\0';
		if (1 != sscanf(arr, "%d", &pos))
			return -EINVAL;
		scsi_debug_opts = pos;
		if (scsi_debug_every_nth != 0)
                        scsi_debug_cmnd_count = 0;
		return length;
	}
	begin = 0;
	pos = len = sprintf(buffer, "scsi_debug adapter driver, version "
	    "%s [%s]\n"
	    "num_tgts=%d, shared (ram) size=%d MB, opts=0x%x, "
	    "every_nth=%d(curr:%d)\n"
	    "delay=%d, max_luns=%d, scsi_level=%d\n"
	    "sector_size=%d bytes, cylinders=%d, heads=%d, sectors=%d\n"
	    "number of aborts=%d, device_reset=%d, bus_resets=%d, "
	    "host_resets=%d\n",
	    SCSI_DEBUG_VERSION, scsi_debug_version_date, scsi_debug_num_tgts,
	    scsi_debug_dev_size_mb, scsi_debug_opts, scsi_debug_every_nth,
	    scsi_debug_cmnd_count, scsi_debug_delay,
	    scsi_debug_max_luns, scsi_debug_scsi_level,
	    SECT_SIZE, sdebug_cylinders_per, sdebug_heads, sdebug_sectors_per,
	    num_aborts, num_dev_resets, num_bus_resets, num_host_resets);
	if (pos < offset) {
		len = 0;
		begin = pos;
	}
	*start = buffer + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin);
	if (len > length)
		len = length;
	return len;
}

static ssize_t sdebug_delay_show(struct device_driver * ddp, char * buf)
{
        return scnprintf(buf, PAGE_SIZE, "%d\n", scsi_debug_delay);
}

static ssize_t sdebug_delay_store(struct device_driver * ddp,
				  const char * buf, size_t count)
{
        int delay;
	char work[20];

        if (1 == sscanf(buf, "%10s", work)) {
		if ((1 == sscanf(work, "%d", &delay)) && (delay >= 0)) {
			scsi_debug_delay = delay;
			return count;
		}
	}
	return -EINVAL;
}
DRIVER_ATTR(delay, S_IRUGO | S_IWUSR, sdebug_delay_show,
	    sdebug_delay_store);

static ssize_t sdebug_opts_show(struct device_driver * ddp, char * buf)
{
        return scnprintf(buf, PAGE_SIZE, "0x%x\n", scsi_debug_opts);
}

static ssize_t sdebug_opts_store(struct device_driver * ddp,
				 const char * buf, size_t count)
{
        int opts;
	char work[20];

        if (1 == sscanf(buf, "%10s", work)) {
		if (0 == strnicmp(work,"0x", 2)) {
			if (1 == sscanf(&work[2], "%x", &opts))
				goto opts_done;
		} else {
			if (1 == sscanf(work, "%d", &opts))
				goto opts_done;
		}
	}
	return -EINVAL;
opts_done:
	scsi_debug_opts = opts;
	scsi_debug_cmnd_count = 0;
	return count;
}
DRIVER_ATTR(opts, S_IRUGO | S_IWUSR, sdebug_opts_show,
	    sdebug_opts_store);

static ssize_t sdebug_ptype_show(struct device_driver * ddp, char * buf)
{
        return scnprintf(buf, PAGE_SIZE, "%d\n", scsi_debug_ptype);
}
static ssize_t sdebug_ptype_store(struct device_driver * ddp,
				  const char * buf, size_t count)
{
        int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		scsi_debug_ptype = n;
		return count;
	}
	return -EINVAL;
}
DRIVER_ATTR(ptype, S_IRUGO | S_IWUSR, sdebug_ptype_show, sdebug_ptype_store);

static ssize_t sdebug_dsense_show(struct device_driver * ddp, char * buf)
{
        return scnprintf(buf, PAGE_SIZE, "%d\n", scsi_debug_dsense);
}
static ssize_t sdebug_dsense_store(struct device_driver * ddp,
				  const char * buf, size_t count)
{
        int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		scsi_debug_dsense = n;
		return count;
	}
	return -EINVAL;
}
DRIVER_ATTR(dsense, S_IRUGO | S_IWUSR, sdebug_dsense_show,
	    sdebug_dsense_store);

static ssize_t sdebug_num_tgts_show(struct device_driver * ddp, char * buf)
{
        return scnprintf(buf, PAGE_SIZE, "%d\n", scsi_debug_num_tgts);
}
static ssize_t sdebug_num_tgts_store(struct device_driver * ddp,
				     const char * buf, size_t count)
{
        int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		scsi_debug_num_tgts = n;
		sdebug_max_tgts_luns();
		return count;
	}
	return -EINVAL;
}
DRIVER_ATTR(num_tgts, S_IRUGO | S_IWUSR, sdebug_num_tgts_show,
	    sdebug_num_tgts_store);

static ssize_t sdebug_dev_size_mb_show(struct device_driver * ddp, char * buf)
{
        return scnprintf(buf, PAGE_SIZE, "%d\n", scsi_debug_dev_size_mb);
}
DRIVER_ATTR(dev_size_mb, S_IRUGO, sdebug_dev_size_mb_show, NULL);

static ssize_t sdebug_num_parts_show(struct device_driver * ddp, char * buf)
{
        return scnprintf(buf, PAGE_SIZE, "%d\n", scsi_debug_num_parts);
}
DRIVER_ATTR(num_parts, S_IRUGO, sdebug_num_parts_show, NULL);

static ssize_t sdebug_every_nth_show(struct device_driver * ddp, char * buf)
{
        return scnprintf(buf, PAGE_SIZE, "%d\n", scsi_debug_every_nth);
}
static ssize_t sdebug_every_nth_store(struct device_driver * ddp,
				      const char * buf, size_t count)
{
        int nth;

	if ((count > 0) && (1 == sscanf(buf, "%d", &nth))) {
		scsi_debug_every_nth = nth;
		scsi_debug_cmnd_count = 0;
		return count;
	}
	return -EINVAL;
}
DRIVER_ATTR(every_nth, S_IRUGO | S_IWUSR, sdebug_every_nth_show,
	    sdebug_every_nth_store);

static ssize_t sdebug_max_luns_show(struct device_driver * ddp, char * buf)
{
        return scnprintf(buf, PAGE_SIZE, "%d\n", scsi_debug_max_luns);
}
static ssize_t sdebug_max_luns_store(struct device_driver * ddp,
				     const char * buf, size_t count)
{
        int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		scsi_debug_max_luns = n;
		sdebug_max_tgts_luns();
		return count;
	}
	return -EINVAL;
}
DRIVER_ATTR(max_luns, S_IRUGO | S_IWUSR, sdebug_max_luns_show,
	    sdebug_max_luns_store);

static ssize_t sdebug_scsi_level_show(struct device_driver * ddp, char * buf)
{
        return scnprintf(buf, PAGE_SIZE, "%d\n", scsi_debug_scsi_level);
}
DRIVER_ATTR(scsi_level, S_IRUGO, sdebug_scsi_level_show, NULL);

static ssize_t sdebug_add_host_show(struct device_driver * ddp, char * buf)
{
        return scnprintf(buf, PAGE_SIZE, "%d\n", scsi_debug_add_host);
}

static ssize_t sdebug_add_host_store(struct device_driver * ddp,
				     const char * buf, size_t count)
{
        int delta_hosts;
	char work[20];

        if (1 != sscanf(buf, "%10s", work))
		return -EINVAL;
	{	/* temporary hack around sscanf() problem with -ve nums */
		int neg = 0;

		if ('-' == *work)
			neg = 1;
		if (1 != sscanf(work + neg, "%d", &delta_hosts))
			return -EINVAL;
		if (neg)
			delta_hosts = -delta_hosts;
	}
	if (delta_hosts > 0) {
		do {
			sdebug_add_adapter();
		} while (--delta_hosts);
	} else if (delta_hosts < 0) {
		do {
			sdebug_remove_adapter();
		} while (++delta_hosts);
	}
	return count;
}
DRIVER_ATTR(add_host, S_IRUGO | S_IWUSR, sdebug_add_host_show, 
	    sdebug_add_host_store);

static void do_create_driverfs_files(void)
{
	driver_create_file(&sdebug_driverfs_driver, &driver_attr_add_host);
	driver_create_file(&sdebug_driverfs_driver, &driver_attr_delay);
	driver_create_file(&sdebug_driverfs_driver, &driver_attr_dev_size_mb);
	driver_create_file(&sdebug_driverfs_driver, &driver_attr_dsense);
	driver_create_file(&sdebug_driverfs_driver, &driver_attr_every_nth);
	driver_create_file(&sdebug_driverfs_driver, &driver_attr_max_luns);
	driver_create_file(&sdebug_driverfs_driver, &driver_attr_num_tgts);
	driver_create_file(&sdebug_driverfs_driver, &driver_attr_num_parts);
	driver_create_file(&sdebug_driverfs_driver, &driver_attr_ptype);
	driver_create_file(&sdebug_driverfs_driver, &driver_attr_opts);
	driver_create_file(&sdebug_driverfs_driver, &driver_attr_scsi_level);
}

static void do_remove_driverfs_files(void)
{
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_scsi_level);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_opts);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_ptype);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_num_parts);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_num_tgts);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_max_luns);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_every_nth);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_dsense);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_dev_size_mb);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_delay);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_add_host);
}

static int __init scsi_debug_init(void)
{
	unsigned long sz;
	int host_to_add;
	int k;

	if (scsi_debug_dev_size_mb < 1)
		scsi_debug_dev_size_mb = 1;  /* force minimum 1 MB ramdisk */
	sdebug_store_size = (unsigned long)scsi_debug_dev_size_mb * 1048576;
	sdebug_capacity = sdebug_store_size / SECT_SIZE;

	/* play around with geometry, don't waste too much on track 0 */
	sdebug_heads = 8;
	sdebug_sectors_per = 32;
	if (scsi_debug_dev_size_mb >= 16)
		sdebug_heads = 32;
	else if (scsi_debug_dev_size_mb >= 256)
		sdebug_heads = 64;
	sdebug_cylinders_per = (unsigned long)sdebug_capacity /
			       (sdebug_sectors_per * sdebug_heads);
	if (sdebug_cylinders_per >= 1024) {
		/* other LLDs do this; implies >= 1GB ram disk ... */
		sdebug_heads = 255;
		sdebug_sectors_per = 63;
		sdebug_cylinders_per = (unsigned long)sdebug_capacity /
			       (sdebug_sectors_per * sdebug_heads);
	}

	sz = sdebug_store_size;
	fake_storep = vmalloc(sz);
	if (NULL == fake_storep) {
		printk(KERN_ERR "scsi_debug_init: out of memory, 1\n");
		return -ENOMEM;
	}
	memset(fake_storep, 0, sz);
	if (scsi_debug_num_parts > 0)
		sdebug_build_parts(fake_storep);

	init_all_queued();

	device_register(&pseudo_primary);
	bus_register(&pseudo_lld_bus);
	driver_register(&sdebug_driverfs_driver);
	do_create_driverfs_files();

	sdebug_driver_template.proc_name = (char *)sdebug_proc_name;

	host_to_add = scsi_debug_add_host;
        scsi_debug_add_host = 0;

        for (k = 0; k < host_to_add; k++) {
                if (sdebug_add_adapter()) {
                        printk(KERN_ERR "scsi_debug_init: "
                               "sdebug_add_adapter failed k=%d\n", k);
                        break;
                }
        }

	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts) {
		printk(KERN_INFO "scsi_debug_init: built %d host(s)\n",
		       scsi_debug_add_host);
	}
	return 0;
}

static void __exit scsi_debug_exit(void)
{
	int k = scsi_debug_add_host;

	stop_all_queued();
	for (; k; k--)
		sdebug_remove_adapter();
	do_remove_driverfs_files();
	driver_unregister(&sdebug_driverfs_driver);
	bus_unregister(&pseudo_lld_bus);
	device_unregister(&pseudo_primary);

	vfree(fake_storep);
}

device_initcall(scsi_debug_init);
module_exit(scsi_debug_exit);

void pseudo_0_release(struct device * dev)
{
	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
		printk(KERN_INFO "scsi_debug: pseudo_0_release() called\n");
}

static struct device pseudo_primary = {
	.bus_id		= "pseudo_0",
	.release	= pseudo_0_release,
};

static int pseudo_lld_bus_match(struct device *dev,
                          struct device_driver *dev_driver)
{
        return 1;
}

static struct bus_type pseudo_lld_bus = {
        .name = "pseudo",
        .match = pseudo_lld_bus_match,
};

static void sdebug_release_adapter(struct device * dev)
{
        struct sdebug_host_info *sdbg_host;

	sdbg_host = to_sdebug_host(dev);
        kfree(sdbg_host);
}

static int sdebug_add_adapter(void)
{
	int k, devs_per_host;
        int error = 0;
        struct sdebug_host_info *sdbg_host;
        struct sdebug_dev_info *sdbg_devinfo;
        struct list_head *lh, *lh_sf;

        sdbg_host = kmalloc(sizeof(*sdbg_host),GFP_KERNEL);

        if (NULL == sdbg_host) {
                printk(KERN_ERR "%s: out of memory at line %d\n",
                       __FUNCTION__, __LINE__);
                return -ENOMEM;
        }

        memset(sdbg_host, 0, sizeof(*sdbg_host));
        INIT_LIST_HEAD(&sdbg_host->dev_info_list);

	devs_per_host = scsi_debug_num_tgts * scsi_debug_max_luns;
        for (k = 0; k < devs_per_host; k++) {
                sdbg_devinfo = kmalloc(sizeof(*sdbg_devinfo),GFP_KERNEL);
                if (NULL == sdbg_devinfo) {
                        printk(KERN_ERR "%s: out of memory at line %d\n",
                               __FUNCTION__, __LINE__);
                        error = -ENOMEM;
			goto clean;
                }
                memset(sdbg_devinfo, 0, sizeof(*sdbg_devinfo));
                sdbg_devinfo->sdbg_host = sdbg_host;
                list_add_tail(&sdbg_devinfo->dev_list,
                              &sdbg_host->dev_info_list);
        }

        spin_lock(&sdebug_host_list_lock);
        list_add_tail(&sdbg_host->host_list, &sdebug_host_list);
        spin_unlock(&sdebug_host_list_lock);

        sdbg_host->dev.bus = &pseudo_lld_bus;
        sdbg_host->dev.parent = &pseudo_primary;
        sdbg_host->dev.release = &sdebug_release_adapter;
        sprintf(sdbg_host->dev.bus_id, "adapter%d", scsi_debug_add_host);

        error = device_register(&sdbg_host->dev);

        if (error)
		goto clean;

	++scsi_debug_add_host;
        return error;

clean:
	list_for_each_safe(lh, lh_sf, &sdbg_host->dev_info_list) {
		sdbg_devinfo = list_entry(lh, struct sdebug_dev_info,
					  dev_list);
		list_del(&sdbg_devinfo->dev_list);
		kfree(sdbg_devinfo);
	}

	kfree(sdbg_host);
        return error;
}

static void sdebug_remove_adapter(void)
{
        struct sdebug_host_info * sdbg_host = NULL;

        spin_lock(&sdebug_host_list_lock);
        if (!list_empty(&sdebug_host_list)) {
                sdbg_host = list_entry(sdebug_host_list.prev,
                                       struct sdebug_host_info, host_list);
		list_del(&sdbg_host->host_list);
	}
        spin_unlock(&sdebug_host_list_lock);

	if (!sdbg_host)
		return;

        device_unregister(&sdbg_host->dev);
        --scsi_debug_add_host;
}

static int sdebug_driver_probe(struct device * dev)
{
        int error = 0;
        struct sdebug_host_info *sdbg_host;
        struct Scsi_Host *hpnt;

	sdbg_host = to_sdebug_host(dev);

        hpnt = scsi_host_alloc(&sdebug_driver_template, sizeof(sdbg_host));
        if (NULL == hpnt) {
                printk(KERN_ERR "%s: scsi_register failed\n", __FUNCTION__);
                error = -ENODEV;
		return error;
        }

        sdbg_host->shost = hpnt;
	*((struct sdebug_host_info **)hpnt->hostdata) = sdbg_host;
	if ((hpnt->this_id >= 0) && (scsi_debug_num_tgts > hpnt->this_id))
		hpnt->max_id = scsi_debug_num_tgts + 1;
	else
		hpnt->max_id = scsi_debug_num_tgts;
	hpnt->max_lun = scsi_debug_max_luns;

        error = scsi_add_host(hpnt, &sdbg_host->dev);
        if (error) {
                printk(KERN_ERR "%s: scsi_add_host failed\n", __FUNCTION__);
                error = -ENODEV;
		scsi_host_put(hpnt);
        } else
		scsi_scan_host(hpnt);


        return error;
}

static int sdebug_driver_remove(struct device * dev)
{
        struct list_head *lh, *lh_sf;
        struct sdebug_host_info *sdbg_host;
        struct sdebug_dev_info *sdbg_devinfo;

	sdbg_host = to_sdebug_host(dev);

	if (!sdbg_host) {
		printk(KERN_ERR "%s: Unable to locate host info\n",
		       __FUNCTION__);
		return -ENODEV;
	}

        scsi_remove_host(sdbg_host->shost);

        list_for_each_safe(lh, lh_sf, &sdbg_host->dev_info_list) {
                sdbg_devinfo = list_entry(lh, struct sdebug_dev_info,
                                          dev_list);
                list_del(&sdbg_devinfo->dev_list);
                kfree(sdbg_devinfo);
        }

        scsi_host_put(sdbg_host->shost);
        return 0;
}

static void sdebug_max_tgts_luns(void)
{
	struct sdebug_host_info * sdbg_host;
	struct Scsi_Host *hpnt;

	spin_lock(&sdebug_host_list_lock);
	list_for_each_entry(sdbg_host, &sdebug_host_list, host_list) {
		hpnt = sdbg_host->shost;
		if ((hpnt->this_id >= 0) &&
		    (scsi_debug_num_tgts > hpnt->this_id))
			hpnt->max_id = scsi_debug_num_tgts + 1;
		else
			hpnt->max_id = scsi_debug_num_tgts;
		hpnt->max_lun = scsi_debug_max_luns;
	}
	spin_unlock(&sdebug_host_list_lock);
}
