/*
 * vvvvvvvvvvvvvvvvvvvvvvv Original vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
 *  Copyright (C) 1992  Eric Youngdale
 *  Simulate a host adapter with 2 disks attached.  Do a lot of checking
 *  to make sure that we are not getting blocks mixed up, and PANIC if
 *  anything out of the ordinary is seen.
 * ^^^^^^^^^^^^^^^^^^^^^^^ Original ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 *  This version is more generic, simulating a variable number of disk
 *  (or disk like devices) sharing a common amount of RAM. To be more
 *  realistic, the simulated devices have the transport attributes of
 *  SAS disks.
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

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/genhd.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/moduleparam.h>
#include <linux/scatterlist.h>

#include <linux/blkdev.h>
#include "scsi.h"
#include <scsi/scsi_host.h>
#include <scsi/scsicam.h>

#include <linux/stat.h>

#include "scsi_logging.h"
#include "scsi_debug.h"

#define SCSI_DEBUG_VERSION "1.81"
static const char * scsi_debug_version_date = "20070104";

/* Additional Sense Code (ASC) */
#define NO_ADDITIONAL_SENSE 0x0
#define LOGICAL_UNIT_NOT_READY 0x4
#define UNRECOVERED_READ_ERR 0x11
#define PARAMETER_LIST_LENGTH_ERR 0x1a
#define INVALID_OPCODE 0x20
#define ADDR_OUT_OF_RANGE 0x21
#define INVALID_FIELD_IN_CDB 0x24
#define INVALID_FIELD_IN_PARAM_LIST 0x26
#define POWERON_RESET 0x29
#define SAVING_PARAMS_UNSUP 0x39
#define TRANSPORT_PROBLEM 0x4b
#define THRESHOLD_EXCEEDED 0x5d
#define LOW_POWER_COND_ON 0x5e

/* Additional Sense Code Qualifier (ASCQ) */
#define ACK_NAK_TO 0x3

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
#define DEF_NO_LUN_0   0
#define DEF_VIRTUAL_GB   0
#define DEF_FAKE_RW	0
#define DEF_VPD_USE_HOSTNO 1

/* bit mask values for scsi_debug_opts */
#define SCSI_DEBUG_OPT_NOISE   1
#define SCSI_DEBUG_OPT_MEDIUM_ERR   2
#define SCSI_DEBUG_OPT_TIMEOUT   4
#define SCSI_DEBUG_OPT_RECOVERED_ERR   8
#define SCSI_DEBUG_OPT_TRANSPORT_ERR   16
/* When "every_nth" > 0 then modulo "every_nth" commands:
 *   - a no response is simulated if SCSI_DEBUG_OPT_TIMEOUT is set
 *   - a RECOVERED_ERROR is simulated on successful read and write
 *     commands if SCSI_DEBUG_OPT_RECOVERED_ERR is set.
 *   - a TRANSPORT_ERROR is simulated on successful read and write
 *     commands if SCSI_DEBUG_OPT_TRANSPORT_ERR is set.
 *
 * When "every_nth" < 0 then after "- every_nth" commands:
 *   - a no response is simulated if SCSI_DEBUG_OPT_TIMEOUT is set
 *   - a RECOVERED_ERROR is simulated on successful read and write
 *     commands if SCSI_DEBUG_OPT_RECOVERED_ERR is set.
 *   - a TRANSPORT_ERROR is simulated on successful read and write
 *     commands if SCSI_DEBUG_OPT_TRANSPORT_ERR is set.
 * This will continue until some other action occurs (e.g. the user
 * writing a new value (other than -1 or 1) to every_nth via sysfs).
 */

/* when 1==SCSI_DEBUG_OPT_MEDIUM_ERR, a medium error is simulated at this
 * sector on read commands: */
#define OPT_MEDIUM_ERR_ADDR   0x1234 /* that's sector 4660 in decimal */

/* If REPORT LUNS has luns >= 256 it can choose "flat space" (value 1)
 * or "peripheral device" addressing (value 0) */
#define SAM2_LUN_ADDRESS_METHOD 0
#define SAM2_WLUN_REPORT_LUNS 0xc101

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
static int scsi_debug_no_lun_0 = DEF_NO_LUN_0;
static int scsi_debug_virtual_gb = DEF_VIRTUAL_GB;
static int scsi_debug_fake_rw = DEF_FAKE_RW;
static int scsi_debug_vpd_use_hostno = DEF_VPD_USE_HOSTNO;

static int scsi_debug_cmnd_count = 0;

#define DEV_READONLY(TGT)      (0)
#define DEV_REMOVEABLE(TGT)    (0)

static unsigned int sdebug_store_size;	/* in bytes */
static unsigned int sdebug_store_sectors;
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
	unsigned int wlun;
	char reset;
	char stopped;
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

static struct scsi_host_template sdebug_driver_template = {
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
	.sg_tablesize =		256,
	.cmd_per_lun =		16,
	.max_sectors =		0xffff,
	.unchecked_isa_dma = 	0,
	.use_clustering = 	ENABLE_CLUSTERING,
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
};

static const int check_condition_result =
		(DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;

static unsigned char ctrl_m_pg[] = {0xa, 10, 2, 0, 0, 0, 0, 0,
				    0, 0, 0x2, 0x4b};
static unsigned char iec_m_pg[] = {0x1c, 0xa, 0x08, 0, 0, 0, 0, 0,
			           0, 0, 0x0, 0x0};

/* function declarations */
static int resp_inquiry(struct scsi_cmnd * SCpnt, int target,
			struct sdebug_dev_info * devip);
static int resp_requests(struct scsi_cmnd * SCpnt,
			 struct sdebug_dev_info * devip);
static int resp_start_stop(struct scsi_cmnd * scp,
			   struct sdebug_dev_info * devip);
static int resp_report_tgtpgs(struct scsi_cmnd * scp,
			      struct sdebug_dev_info * devip);
static int resp_readcap(struct scsi_cmnd * SCpnt,
			struct sdebug_dev_info * devip);
static int resp_readcap16(struct scsi_cmnd * SCpnt,
			  struct sdebug_dev_info * devip);
static int resp_mode_sense(struct scsi_cmnd * scp, int target,
			   struct sdebug_dev_info * devip);
static int resp_mode_select(struct scsi_cmnd * scp, int mselect6,
			    struct sdebug_dev_info * devip);
static int resp_log_sense(struct scsi_cmnd * scp,
			  struct sdebug_dev_info * devip);
static int resp_read(struct scsi_cmnd * SCpnt, unsigned long long lba,
		     unsigned int num, struct sdebug_dev_info * devip);
static int resp_write(struct scsi_cmnd * SCpnt, unsigned long long lba,
		      unsigned int num, struct sdebug_dev_info * devip);
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
static int check_readiness(struct scsi_cmnd * SCpnt, int reset_only,
			   struct sdebug_dev_info * devip);
static int schedule_resp(struct scsi_cmnd * cmnd,
			 struct sdebug_dev_info * devip,
			 done_funct_t done, int scsi_result, int delta_jiff);
static void __init sdebug_build_parts(unsigned char * ramp);
static void __init init_all_queued(void);
static void stop_all_queued(void);
static int stop_queued_cmnd(struct scsi_cmnd * cmnd);
static int inquiry_evpd_83(unsigned char * arr, int port_group_id,
			   int target_dev_id, int dev_id_num,
			   const char * dev_id_str, int dev_id_str_len);
static int inquiry_evpd_88(unsigned char * arr, int target_dev_id);
static int do_create_driverfs_files(void);
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
	int len, k, j;
	unsigned int num;
	unsigned long long lba;
	int errsts = 0;
	int target = SCpnt->device->id;
	struct sdebug_dev_info * devip = NULL;
	int inj_recovered = 0;
	int inj_transport = 0;
	int delay_override = 0;

	if (done == NULL)
		return 0;	/* assume mid level reprocessing command */

	SCpnt->resid = 0;
	if ((SCSI_DEBUG_OPT_NOISE & scsi_debug_opts) && cmd) {
		printk(KERN_INFO "scsi_debug: cmd ");
		for (k = 0, len = SCpnt->cmd_len; k < len; ++k)
			printk("%02x ", (int)cmd[k]);
		printk("\n");
	}
        if(target == sdebug_driver_template.this_id) {
		printk(KERN_INFO "scsi_debug: initiator's id used as "
		       "target!\n");
		return schedule_resp(SCpnt, NULL, done,
				     DID_NO_CONNECT << 16, 0);
        }

	if ((SCpnt->device->lun >= scsi_debug_max_luns) &&
	    (SCpnt->device->lun != SAM2_WLUN_REPORT_LUNS))
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
		else if (SCSI_DEBUG_OPT_TRANSPORT_ERR & scsi_debug_opts)
			inj_transport = 1; /* to reads and writes below */
        }

	if (devip->wlun) {
		switch (*cmd) {
		case INQUIRY:
		case REQUEST_SENSE:
		case TEST_UNIT_READY:
		case REPORT_LUNS:
			break;  /* only allowable wlun commands */
		default:
			if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
				printk(KERN_INFO "scsi_debug: Opcode: 0x%x "
				       "not supported for wlun\n", *cmd);
			mk_sense_buffer(devip, ILLEGAL_REQUEST,
					INVALID_OPCODE, 0);
			errsts = check_condition_result;
			return schedule_resp(SCpnt, devip, done, errsts,
					     0);
		}
	}

	switch (*cmd) {
	case INQUIRY:     /* mandatory, ignore unit attention */
		delay_override = 1;
		errsts = resp_inquiry(SCpnt, target, devip);
		break;
	case REQUEST_SENSE:	/* mandatory, ignore unit attention */
		delay_override = 1;
		errsts = resp_requests(SCpnt, devip);
		break;
	case REZERO_UNIT:	/* actually this is REWIND for SSC */
	case START_STOP:
		errsts = resp_start_stop(SCpnt, devip);
		break;
	case ALLOW_MEDIUM_REMOVAL:
		if ((errsts = check_readiness(SCpnt, 1, devip)))
			break;
		if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
			printk(KERN_INFO "scsi_debug: Medium removal %s\n",
			        cmd[4] ? "inhibited" : "enabled");
		break;
	case SEND_DIAGNOSTIC:     /* mandatory */
		errsts = check_readiness(SCpnt, 1, devip);
		break;
	case TEST_UNIT_READY:     /* mandatory */
		delay_override = 1;
		errsts = check_readiness(SCpnt, 0, devip);
		break;
        case RESERVE:
		errsts = check_readiness(SCpnt, 1, devip);
                break;
        case RESERVE_10:
		errsts = check_readiness(SCpnt, 1, devip);
                break;
        case RELEASE:
		errsts = check_readiness(SCpnt, 1, devip);
                break;
        case RELEASE_10:
		errsts = check_readiness(SCpnt, 1, devip);
                break;
	case READ_CAPACITY:
		errsts = resp_readcap(SCpnt, devip);
		break;
	case SERVICE_ACTION_IN:
		if (SAI_READ_CAPACITY_16 != cmd[1]) {
			mk_sense_buffer(devip, ILLEGAL_REQUEST,
					INVALID_OPCODE, 0);
			errsts = check_condition_result;
			break;
		}
		errsts = resp_readcap16(SCpnt, devip);
		break;
	case MAINTENANCE_IN:
		if (MI_REPORT_TARGET_PGS != cmd[1]) {
			mk_sense_buffer(devip, ILLEGAL_REQUEST,
					INVALID_OPCODE, 0);
			errsts = check_condition_result;
			break;
		}
		errsts = resp_report_tgtpgs(SCpnt, devip);
		break;
	case READ_16:
	case READ_12:
	case READ_10:
	case READ_6:
		if ((errsts = check_readiness(SCpnt, 0, devip)))
			break;
		if (scsi_debug_fake_rw)
			break;
		if ((*cmd) == READ_16) {
			for (lba = 0, j = 0; j < 8; ++j) {
				if (j > 0)
					lba <<= 8;
				lba += cmd[2 + j];
			}
			num = cmd[13] + (cmd[12] << 8) +
				(cmd[11] << 16) + (cmd[10] << 24);
		} else if ((*cmd) == READ_12) {
			lba = cmd[5] + (cmd[4] << 8) +
				(cmd[3] << 16) + (cmd[2] << 24);
			num = cmd[9] + (cmd[8] << 8) +
				(cmd[7] << 16) + (cmd[6] << 24);
		} else if ((*cmd) == READ_10) {
			lba = cmd[5] + (cmd[4] << 8) +
				(cmd[3] << 16) + (cmd[2] << 24);
			num = cmd[8] + (cmd[7] << 8);
		} else {	/* READ (6) */
			lba = cmd[3] + (cmd[2] << 8) +
				((cmd[1] & 0x1f) << 16);
			num = (0 == cmd[4]) ? 256 : cmd[4];
		}
		errsts = resp_read(SCpnt, lba, num, devip);
		if (inj_recovered && (0 == errsts)) {
			mk_sense_buffer(devip, RECOVERED_ERROR,
					THRESHOLD_EXCEEDED, 0);
			errsts = check_condition_result;
		} else if (inj_transport && (0 == errsts)) {
                        mk_sense_buffer(devip, ABORTED_COMMAND,
                                        TRANSPORT_PROBLEM, ACK_NAK_TO);
                        errsts = check_condition_result;
                }
		break;
	case REPORT_LUNS:	/* mandatory, ignore unit attention */
		delay_override = 1;
		errsts = resp_report_luns(SCpnt, devip);
		break;
	case VERIFY:		/* 10 byte SBC-2 command */
		errsts = check_readiness(SCpnt, 0, devip);
		break;
	case WRITE_16:
	case WRITE_12:
	case WRITE_10:
	case WRITE_6:
		if ((errsts = check_readiness(SCpnt, 0, devip)))
			break;
		if (scsi_debug_fake_rw)
			break;
		if ((*cmd) == WRITE_16) {
			for (lba = 0, j = 0; j < 8; ++j) {
				if (j > 0)
					lba <<= 8;
				lba += cmd[2 + j];
			}
			num = cmd[13] + (cmd[12] << 8) +
				(cmd[11] << 16) + (cmd[10] << 24);
		} else if ((*cmd) == WRITE_12) {
			lba = cmd[5] + (cmd[4] << 8) +
				(cmd[3] << 16) + (cmd[2] << 24);
			num = cmd[9] + (cmd[8] << 8) +
				(cmd[7] << 16) + (cmd[6] << 24);
		} else if ((*cmd) == WRITE_10) {
			lba = cmd[5] + (cmd[4] << 8) +
				(cmd[3] << 16) + (cmd[2] << 24);
			num = cmd[8] + (cmd[7] << 8);
		} else {	/* WRITE (6) */
			lba = cmd[3] + (cmd[2] << 8) +
				((cmd[1] & 0x1f) << 16);
			num = (0 == cmd[4]) ? 256 : cmd[4];
		}
		errsts = resp_write(SCpnt, lba, num, devip);
		if (inj_recovered && (0 == errsts)) {
			mk_sense_buffer(devip, RECOVERED_ERROR,
					THRESHOLD_EXCEEDED, 0);
			errsts = check_condition_result;
		}
		break;
	case MODE_SENSE:
	case MODE_SENSE_10:
		errsts = resp_mode_sense(SCpnt, target, devip);
		break;
	case MODE_SELECT:
		errsts = resp_mode_select(SCpnt, 1, devip);
		break;
	case MODE_SELECT_10:
		errsts = resp_mode_select(SCpnt, 0, devip);
		break;
	case LOG_SENSE:
		errsts = resp_log_sense(SCpnt, devip);
		break;
	case SYNCHRONIZE_CACHE:
		delay_override = 1;
		errsts = check_readiness(SCpnt, 0, devip);
		break;
	case WRITE_BUFFER:
		errsts = check_readiness(SCpnt, 1, devip);
		break;
	default:
		if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
			printk(KERN_INFO "scsi_debug: Opcode: 0x%x not "
			       "supported\n", *cmd);
		if ((errsts = check_readiness(SCpnt, 1, devip)))
			break;	/* Unit attention takes precedence */
		mk_sense_buffer(devip, ILLEGAL_REQUEST, INVALID_OPCODE, 0);
		errsts = check_condition_result;
		break;
	}
	return schedule_resp(SCpnt, devip, done, errsts,
			     (delay_override ? 0 : scsi_debug_delay));
}

static int scsi_debug_ioctl(struct scsi_device *dev, int cmd, void __user *arg)
{
	if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts) {
		printk(KERN_INFO "scsi_debug: ioctl: cmd=0x%x\n", cmd);
	}
	return -EINVAL;
	/* return -ENOTTY; // correct return but upsets fdisk */
}

static int check_readiness(struct scsi_cmnd * SCpnt, int reset_only,
			   struct sdebug_dev_info * devip)
{
	if (devip->reset) {
		if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
			printk(KERN_INFO "scsi_debug: Reporting Unit "
			       "attention: power on reset\n");
		devip->reset = 0;
		mk_sense_buffer(devip, UNIT_ATTENTION, POWERON_RESET, 0);
		return check_condition_result;
	}
	if ((0 == reset_only) && devip->stopped) {
		if (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts)
			printk(KERN_INFO "scsi_debug: Reporting Not "
			       "ready: initializing command required\n");
		mk_sense_buffer(devip, NOT_READY, LOGICAL_UNIT_NOT_READY,
				0x2);
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
	struct scatterlist * sg;

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
		if (scp->resid)
			scp->resid -= act_len;
		else
			scp->resid = req_len - act_len;
		return 0;
	}
	active = 1;
	req_len = act_len = 0;
	scsi_for_each_sg(scp, sg, scp->use_sg, k) {
		if (active) {
			kaddr = (unsigned char *)
				kmap_atomic(sg_page(sg), KM_USER0);
			if (NULL == kaddr)
				return (DID_ERROR << 16);
			kaddr_off = (unsigned char *)kaddr + sg->offset;
			len = sg->length;
			if ((req_len + len) > arr_len) {
				active = 0;
				len = arr_len - req_len;
			}
			memcpy(kaddr_off, arr + req_len, len);
			kunmap_atomic(kaddr, KM_USER0);
			act_len += len;
		}
		req_len += sg->length;
	}
	if (scp->resid)
		scp->resid -= act_len;
	else
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
	struct scatterlist * sg;

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
	sg = scsi_sglist(scp);
	req_len = fin = 0;
	for (k = 0; k < scp->use_sg; ++k, sg = sg_next(sg)) {
		kaddr = (unsigned char *)kmap_atomic(sg_page(sg), KM_USER0);
		if (NULL == kaddr)
			return -1;
		kaddr_off = (unsigned char *)kaddr + sg->offset;
		len = sg->length;
		if ((req_len + len) > max_arr_len) {
			len = max_arr_len - req_len;
			fin = 1;
		}
		memcpy(arr + req_len, kaddr_off, len);
		kunmap_atomic(kaddr, KM_USER0);
		if (fin)
			return req_len + len;
		req_len += sg->length;
	}
	return req_len;
}


static const char * inq_vendor_id = "Linux   ";
static const char * inq_product_id = "scsi_debug      ";
static const char * inq_product_rev = "0004";

static int inquiry_evpd_83(unsigned char * arr, int port_group_id,
			   int target_dev_id, int dev_id_num,
			   const char * dev_id_str,
			   int dev_id_str_len)
{
	int num, port_a;
	char b[32];

	port_a = target_dev_id + 1;
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
	if (dev_id_num >= 0) {
		/* NAA-5, Logical unit identifier (binary) */
		arr[num++] = 0x1;	/* binary (not necessarily sas) */
		arr[num++] = 0x3;	/* PIV=0, lu, naa */
		arr[num++] = 0x0;
		arr[num++] = 0x8;
		arr[num++] = 0x53;  /* naa-5 ieee company id=0x333333 (fake) */
		arr[num++] = 0x33;
		arr[num++] = 0x33;
		arr[num++] = 0x30;
		arr[num++] = (dev_id_num >> 24);
		arr[num++] = (dev_id_num >> 16) & 0xff;
		arr[num++] = (dev_id_num >> 8) & 0xff;
		arr[num++] = dev_id_num & 0xff;
		/* Target relative port number */
		arr[num++] = 0x61;	/* proto=sas, binary */
		arr[num++] = 0x94;	/* PIV=1, target port, rel port */
		arr[num++] = 0x0;	/* reserved */
		arr[num++] = 0x4;	/* length */
		arr[num++] = 0x0;	/* reserved */
		arr[num++] = 0x0;	/* reserved */
		arr[num++] = 0x0;
		arr[num++] = 0x1;	/* relative port A */
	}
	/* NAA-5, Target port identifier */
	arr[num++] = 0x61;	/* proto=sas, binary */
	arr[num++] = 0x93;	/* piv=1, target port, naa */
	arr[num++] = 0x0;
	arr[num++] = 0x8;
	arr[num++] = 0x52;	/* naa-5, company id=0x222222 (fake) */
	arr[num++] = 0x22;
	arr[num++] = 0x22;
	arr[num++] = 0x20;
	arr[num++] = (port_a >> 24);
	arr[num++] = (port_a >> 16) & 0xff;
	arr[num++] = (port_a >> 8) & 0xff;
	arr[num++] = port_a & 0xff;
	/* NAA-5, Target port group identifier */
	arr[num++] = 0x61;	/* proto=sas, binary */
	arr[num++] = 0x95;	/* piv=1, target port group id */
	arr[num++] = 0x0;
	arr[num++] = 0x4;
	arr[num++] = 0;
	arr[num++] = 0;
	arr[num++] = (port_group_id >> 8) & 0xff;
	arr[num++] = port_group_id & 0xff;
	/* NAA-5, Target device identifier */
	arr[num++] = 0x61;	/* proto=sas, binary */
	arr[num++] = 0xa3;	/* piv=1, target device, naa */
	arr[num++] = 0x0;
	arr[num++] = 0x8;
	arr[num++] = 0x52;	/* naa-5, company id=0x222222 (fake) */
	arr[num++] = 0x22;
	arr[num++] = 0x22;
	arr[num++] = 0x20;
	arr[num++] = (target_dev_id >> 24);
	arr[num++] = (target_dev_id >> 16) & 0xff;
	arr[num++] = (target_dev_id >> 8) & 0xff;
	arr[num++] = target_dev_id & 0xff;
	/* SCSI name string: Target device identifier */
	arr[num++] = 0x63;	/* proto=sas, UTF-8 */
	arr[num++] = 0xa8;	/* piv=1, target device, SCSI name string */
	arr[num++] = 0x0;
	arr[num++] = 24;
	memcpy(arr + num, "naa.52222220", 12);
	num += 12;
	snprintf(b, sizeof(b), "%08X", target_dev_id);
	memcpy(arr + num, b, 8);
	num += 8;
	memset(arr + num, 0, 4);
	num += 4;
	return num;
}


static unsigned char vpd84_data[] = {
/* from 4th byte */ 0x22,0x22,0x22,0x0,0xbb,0x0,
    0x22,0x22,0x22,0x0,0xbb,0x1,
    0x22,0x22,0x22,0x0,0xbb,0x2,
};

static int inquiry_evpd_84(unsigned char * arr)
{
	memcpy(arr, vpd84_data, sizeof(vpd84_data));
	return sizeof(vpd84_data);
}

static int inquiry_evpd_85(unsigned char * arr)
{
	int num = 0;
	const char * na1 = "https://www.kernel.org/config";
	const char * na2 = "http://www.kernel.org/log";
	int plen, olen;

	arr[num++] = 0x1;	/* lu, storage config */
	arr[num++] = 0x0;	/* reserved */
	arr[num++] = 0x0;
	olen = strlen(na1);
	plen = olen + 1;
	if (plen % 4)
		plen = ((plen / 4) + 1) * 4;
	arr[num++] = plen;	/* length, null termianted, padded */
	memcpy(arr + num, na1, olen);
	memset(arr + num + olen, 0, plen - olen);
	num += plen;

	arr[num++] = 0x4;	/* lu, logging */
	arr[num++] = 0x0;	/* reserved */
	arr[num++] = 0x0;
	olen = strlen(na2);
	plen = olen + 1;
	if (plen % 4)
		plen = ((plen / 4) + 1) * 4;
	arr[num++] = plen;	/* length, null terminated, padded */
	memcpy(arr + num, na2, olen);
	memset(arr + num + olen, 0, plen - olen);
	num += plen;

	return num;
}

/* SCSI ports VPD page */
static int inquiry_evpd_88(unsigned char * arr, int target_dev_id)
{
	int num = 0;
	int port_a, port_b;

	port_a = target_dev_id + 1;
	port_b = port_a + 1;
	arr[num++] = 0x0;	/* reserved */
	arr[num++] = 0x0;	/* reserved */
	arr[num++] = 0x0;
	arr[num++] = 0x1;	/* relative port 1 (primary) */
	memset(arr + num, 0, 6);
	num += 6;
	arr[num++] = 0x0;
	arr[num++] = 12;	/* length tp descriptor */
	/* naa-5 target port identifier (A) */
	arr[num++] = 0x61;	/* proto=sas, binary */
	arr[num++] = 0x93;	/* PIV=1, target port, NAA */
	arr[num++] = 0x0;	/* reserved */
	arr[num++] = 0x8;	/* length */
	arr[num++] = 0x52;	/* NAA-5, company_id=0x222222 (fake) */
	arr[num++] = 0x22;
	arr[num++] = 0x22;
	arr[num++] = 0x20;
	arr[num++] = (port_a >> 24);
	arr[num++] = (port_a >> 16) & 0xff;
	arr[num++] = (port_a >> 8) & 0xff;
	arr[num++] = port_a & 0xff;

	arr[num++] = 0x0;	/* reserved */
	arr[num++] = 0x0;	/* reserved */
	arr[num++] = 0x0;
	arr[num++] = 0x2;	/* relative port 2 (secondary) */
	memset(arr + num, 0, 6);
	num += 6;
	arr[num++] = 0x0;
	arr[num++] = 12;	/* length tp descriptor */
	/* naa-5 target port identifier (B) */
	arr[num++] = 0x61;	/* proto=sas, binary */
	arr[num++] = 0x93;	/* PIV=1, target port, NAA */
	arr[num++] = 0x0;	/* reserved */
	arr[num++] = 0x8;	/* length */
	arr[num++] = 0x52;	/* NAA-5, company_id=0x222222 (fake) */
	arr[num++] = 0x22;
	arr[num++] = 0x22;
	arr[num++] = 0x20;
	arr[num++] = (port_b >> 24);
	arr[num++] = (port_b >> 16) & 0xff;
	arr[num++] = (port_b >> 8) & 0xff;
	arr[num++] = port_b & 0xff;

	return num;
}


static unsigned char vpd89_data[] = {
/* from 4th byte */ 0,0,0,0,
'l','i','n','u','x',' ',' ',' ',
'S','A','T',' ','s','c','s','i','_','d','e','b','u','g',' ',' ',
'1','2','3','4',
0x34,0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,
0xec,0,0,0,
0x5a,0xc,0xff,0x3f,0x37,0xc8,0x10,0,0,0,0,0,0x3f,0,0,0,
0,0,0,0,0x58,0x58,0x58,0x58,0x58,0x58,0x58,0x58,0x20,0x20,0x20,0x20,
0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0,0,0,0x40,0x4,0,0x2e,0x33,
0x38,0x31,0x20,0x20,0x20,0x20,0x54,0x53,0x38,0x33,0x30,0x30,0x33,0x31,
0x53,0x41,
0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
0x20,0x20,
0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
0x10,0x80,
0,0,0,0x2f,0,0,0,0x2,0,0x2,0x7,0,0xff,0xff,0x1,0,
0x3f,0,0xc1,0xff,0x3e,0,0x10,0x1,0xb0,0xf8,0x50,0x9,0,0,0x7,0,
0x3,0,0x78,0,0x78,0,0xf0,0,0x78,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0x2,0,0,0,0,0,0,0,
0x7e,0,0x1b,0,0x6b,0x34,0x1,0x7d,0x3,0x40,0x69,0x34,0x1,0x3c,0x3,0x40,
0x7f,0x40,0,0,0,0,0xfe,0xfe,0,0,0,0,0,0xfe,0,0,
0,0,0,0,0,0,0,0,0xb0,0xf8,0x50,0x9,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0x1,0,0xb0,0xf8,0x50,0x9,0xb0,0xf8,0x50,0x9,0x20,0x20,0x2,0,0xb6,0x42,
0,0x80,0x8a,0,0x6,0x3c,0xa,0x3c,0xff,0xff,0xc6,0x7,0,0x1,0,0x8,
0xf0,0xf,0,0x10,0x2,0,0x30,0,0,0,0,0,0,0,0x6,0xfe,
0,0,0x2,0,0x50,0,0x8a,0,0x4f,0x95,0,0,0x21,0,0xb,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xa5,0x51,
};

static int inquiry_evpd_89(unsigned char * arr)
{
	memcpy(arr, vpd89_data, sizeof(vpd89_data));
	return sizeof(vpd89_data);
}


static unsigned char vpdb0_data[] = {
	/* from 4th byte */ 0,0,0,4,
	0,0,0x4,0,
	0,0,0,64,
};

static int inquiry_evpd_b0(unsigned char * arr)
{
	memcpy(arr, vpdb0_data, sizeof(vpdb0_data));
	if (sdebug_store_sectors > 0x400) {
		arr[4] = (sdebug_store_sectors >> 24) & 0xff;
		arr[5] = (sdebug_store_sectors >> 16) & 0xff;
		arr[6] = (sdebug_store_sectors >> 8) & 0xff;
		arr[7] = sdebug_store_sectors & 0xff;
	}
	return sizeof(vpdb0_data);
}


#define SDEBUG_LONG_INQ_SZ 96
#define SDEBUG_MAX_INQ_ARR_SZ 584

static int resp_inquiry(struct scsi_cmnd * scp, int target,
			struct sdebug_dev_info * devip)
{
	unsigned char pq_pdt;
	unsigned char * arr;
	unsigned char *cmd = (unsigned char *)scp->cmnd;
	int alloc_len, n, ret;

	alloc_len = (cmd[3] << 8) + cmd[4];
	arr = kzalloc(SDEBUG_MAX_INQ_ARR_SZ, GFP_ATOMIC);
	if (! arr)
		return DID_REQUEUE << 16;
	if (devip->wlun)
		pq_pdt = 0x1e;	/* present, wlun */
	else if (scsi_debug_no_lun_0 && (0 == devip->lun))
		pq_pdt = 0x7f;	/* not present, no device type */
	else
		pq_pdt = (scsi_debug_ptype & 0x1f);
	arr[0] = pq_pdt;
	if (0x2 & cmd[1]) {  /* CMDDT bit set */
		mk_sense_buffer(devip, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB,
			       	0);
		kfree(arr);
		return check_condition_result;
	} else if (0x1 & cmd[1]) {  /* EVPD bit set */
		int lu_id_num, port_group_id, target_dev_id, len;
		char lu_id_str[6];
		int host_no = devip->sdbg_host->shost->host_no;
		
		port_group_id = (((host_no + 1) & 0x7f) << 8) +
		    (devip->channel & 0x7f);
		if (0 == scsi_debug_vpd_use_hostno)
			host_no = 0;
		lu_id_num = devip->wlun ? -1 : (((host_no + 1) * 2000) +
			    (devip->target * 1000) + devip->lun);
		target_dev_id = ((host_no + 1) * 2000) +
				 (devip->target * 1000) - 3;
		len = scnprintf(lu_id_str, 6, "%d", lu_id_num);
		if (0 == cmd[2]) { /* supported vital product data pages */
			arr[1] = cmd[2];	/*sanity */
			n = 4;
			arr[n++] = 0x0;   /* this page */
			arr[n++] = 0x80;  /* unit serial number */
			arr[n++] = 0x83;  /* device identification */
			arr[n++] = 0x84;  /* software interface ident. */
			arr[n++] = 0x85;  /* management network addresses */
			arr[n++] = 0x86;  /* extended inquiry */
			arr[n++] = 0x87;  /* mode page policy */
			arr[n++] = 0x88;  /* SCSI ports */
			arr[n++] = 0x89;  /* ATA information */
			arr[n++] = 0xb0;  /* Block limits (SBC) */
			arr[3] = n - 4;	  /* number of supported VPD pages */
		} else if (0x80 == cmd[2]) { /* unit serial number */
			arr[1] = cmd[2];	/*sanity */
			arr[3] = len;
			memcpy(&arr[4], lu_id_str, len);
		} else if (0x83 == cmd[2]) { /* device identification */
			arr[1] = cmd[2];	/*sanity */
			arr[3] = inquiry_evpd_83(&arr[4], port_group_id,
						 target_dev_id, lu_id_num,
						 lu_id_str, len);
		} else if (0x84 == cmd[2]) { /* Software interface ident. */
			arr[1] = cmd[2];	/*sanity */
			arr[3] = inquiry_evpd_84(&arr[4]);
		} else if (0x85 == cmd[2]) { /* Management network addresses */
			arr[1] = cmd[2];	/*sanity */
			arr[3] = inquiry_evpd_85(&arr[4]);
		} else if (0x86 == cmd[2]) { /* extended inquiry */
			arr[1] = cmd[2];	/*sanity */
			arr[3] = 0x3c;	/* number of following entries */
			arr[4] = 0x0;   /* no protection stuff */
			arr[5] = 0x7;   /* head of q, ordered + simple q's */
		} else if (0x87 == cmd[2]) { /* mode page policy */
			arr[1] = cmd[2];	/*sanity */
			arr[3] = 0x8;	/* number of following entries */
			arr[4] = 0x2;	/* disconnect-reconnect mp */
			arr[6] = 0x80;	/* mlus, shared */
			arr[8] = 0x18;	 /* protocol specific lu */
			arr[10] = 0x82;	 /* mlus, per initiator port */
		} else if (0x88 == cmd[2]) { /* SCSI Ports */
			arr[1] = cmd[2];	/*sanity */
			arr[3] = inquiry_evpd_88(&arr[4], target_dev_id);
		} else if (0x89 == cmd[2]) { /* ATA information */
			arr[1] = cmd[2];        /*sanity */
			n = inquiry_evpd_89(&arr[4]);
			arr[2] = (n >> 8);
			arr[3] = (n & 0xff);
		} else if (0xb0 == cmd[2]) { /* Block limits (SBC) */
			arr[1] = cmd[2];        /*sanity */
			arr[3] = inquiry_evpd_b0(&arr[4]);
		} else {
			/* Illegal request, invalid field in cdb */
			mk_sense_buffer(devip, ILLEGAL_REQUEST,
					INVALID_FIELD_IN_CDB, 0);
			kfree(arr);
			return check_condition_result;
		}
		len = min(((arr[2] << 8) + arr[3]) + 4, alloc_len);
		ret = fill_from_dev_buffer(scp, arr,
			    min(len, SDEBUG_MAX_INQ_ARR_SZ));
		kfree(arr);
		return ret;
	}
	/* drops through here for a standard inquiry */
	arr[1] = DEV_REMOVEABLE(target) ? 0x80 : 0;	/* Removable disk */
	arr[2] = scsi_debug_scsi_level;
	arr[3] = 2;    /* response_data_format==2 */
	arr[4] = SDEBUG_LONG_INQ_SZ - 5;
	if (0 == scsi_debug_vpd_use_hostno)
		arr[5] = 0x10; /* claim: implicit TGPS */
	arr[6] = 0x10; /* claim: MultiP */
	/* arr[6] |= 0x40; ... claim: EncServ (enclosure services) */
	arr[7] = 0xa; /* claim: LINKED + CMDQUE */
	memcpy(&arr[8], inq_vendor_id, 8);
	memcpy(&arr[16], inq_product_id, 16);
	memcpy(&arr[32], inq_product_rev, 4);
	/* version descriptors (2 bytes each) follow */
	arr[58] = 0x0; arr[59] = 0x77; /* SAM-3 ANSI */
	arr[60] = 0x3; arr[61] = 0x14;  /* SPC-3 ANSI */
	n = 62;
	if (scsi_debug_ptype == 0) {
		arr[n++] = 0x3; arr[n++] = 0x3d; /* SBC-2 ANSI */
	} else if (scsi_debug_ptype == 1) {
		arr[n++] = 0x3; arr[n++] = 0x60; /* SSC-2 no version */
	}
	arr[n++] = 0xc; arr[n++] = 0xf;  /* SAS-1.1 rev 10 */
	ret = fill_from_dev_buffer(scp, arr,
			    min(alloc_len, SDEBUG_LONG_INQ_SZ));
	kfree(arr);
	return ret;
}

static int resp_requests(struct scsi_cmnd * scp,
			 struct sdebug_dev_info * devip)
{
	unsigned char * sbuff;
	unsigned char *cmd = (unsigned char *)scp->cmnd;
	unsigned char arr[SDEBUG_SENSE_LEN];
	int want_dsense;
	int len = 18;

	memset(arr, 0, sizeof(arr));
	if (devip->reset == 1)
		mk_sense_buffer(devip, 0, NO_ADDITIONAL_SENSE, 0);
	want_dsense = !!(cmd[1] & 1) || scsi_debug_dsense;
	sbuff = devip->sense_buff;
	if ((iec_m_pg[2] & 0x4) && (6 == (iec_m_pg[3] & 0xf))) {
		if (want_dsense) {
			arr[0] = 0x72;
			arr[1] = 0x0;		/* NO_SENSE in sense_key */
			arr[2] = THRESHOLD_EXCEEDED;
			arr[3] = 0xff;		/* TEST set and MRIE==6 */
		} else {
			arr[0] = 0x70;
			arr[2] = 0x0;		/* NO_SENSE in sense_key */
			arr[7] = 0xa;   	/* 18 byte sense buffer */
			arr[12] = THRESHOLD_EXCEEDED;
			arr[13] = 0xff;		/* TEST set and MRIE==6 */
		}
	} else {
		memcpy(arr, sbuff, SDEBUG_SENSE_LEN);
		if ((cmd[1] & 1) && (! scsi_debug_dsense)) {
			/* DESC bit set and sense_buff in fixed format */
			memset(arr, 0, sizeof(arr));
			arr[0] = 0x72;
			arr[1] = sbuff[2];     /* sense key */
			arr[2] = sbuff[12];    /* asc */
			arr[3] = sbuff[13];    /* ascq */
			len = 8;
		}
	}
	mk_sense_buffer(devip, 0, NO_ADDITIONAL_SENSE, 0);
	return fill_from_dev_buffer(scp, arr, len);
}

static int resp_start_stop(struct scsi_cmnd * scp,
			   struct sdebug_dev_info * devip)
{
	unsigned char *cmd = (unsigned char *)scp->cmnd;
	int power_cond, errsts, start;

	if ((errsts = check_readiness(scp, 1, devip)))
		return errsts;
	power_cond = (cmd[4] & 0xf0) >> 4;
	if (power_cond) {
		mk_sense_buffer(devip, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB,
			       	0);
		return check_condition_result;
	}
	start = cmd[4] & 1;
	if (start == devip->stopped)
		devip->stopped = !start;
	return 0;
}

#define SDEBUG_READCAP_ARR_SZ 8
static int resp_readcap(struct scsi_cmnd * scp,
			struct sdebug_dev_info * devip)
{
	unsigned char arr[SDEBUG_READCAP_ARR_SZ];
	unsigned int capac;
	int errsts;

	if ((errsts = check_readiness(scp, 1, devip)))
		return errsts;
	/* following just in case virtual_gb changed */
	if (scsi_debug_virtual_gb > 0) {
		sdebug_capacity = 2048 * 1024;
		sdebug_capacity *= scsi_debug_virtual_gb;
	} else
		sdebug_capacity = sdebug_store_sectors;
	memset(arr, 0, SDEBUG_READCAP_ARR_SZ);
	if (sdebug_capacity < 0xffffffff) {
		capac = (unsigned int)sdebug_capacity - 1;
		arr[0] = (capac >> 24);
		arr[1] = (capac >> 16) & 0xff;
		arr[2] = (capac >> 8) & 0xff;
		arr[3] = capac & 0xff;
	} else {
		arr[0] = 0xff;
		arr[1] = 0xff;
		arr[2] = 0xff;
		arr[3] = 0xff;
	}
	arr[6] = (SECT_SIZE_PER(target) >> 8) & 0xff;
	arr[7] = SECT_SIZE_PER(target) & 0xff;
	return fill_from_dev_buffer(scp, arr, SDEBUG_READCAP_ARR_SZ);
}

#define SDEBUG_READCAP16_ARR_SZ 32
static int resp_readcap16(struct scsi_cmnd * scp,
			  struct sdebug_dev_info * devip)
{
	unsigned char *cmd = (unsigned char *)scp->cmnd;
	unsigned char arr[SDEBUG_READCAP16_ARR_SZ];
	unsigned long long capac;
	int errsts, k, alloc_len;

	if ((errsts = check_readiness(scp, 1, devip)))
		return errsts;
	alloc_len = ((cmd[10] << 24) + (cmd[11] << 16) + (cmd[12] << 8)
		     + cmd[13]);
	/* following just in case virtual_gb changed */
	if (scsi_debug_virtual_gb > 0) {
		sdebug_capacity = 2048 * 1024;
		sdebug_capacity *= scsi_debug_virtual_gb;
	} else
		sdebug_capacity = sdebug_store_sectors;
	memset(arr, 0, SDEBUG_READCAP16_ARR_SZ);
	capac = sdebug_capacity - 1;
	for (k = 0; k < 8; ++k, capac >>= 8)
		arr[7 - k] = capac & 0xff;
	arr[8] = (SECT_SIZE_PER(target) >> 24) & 0xff;
	arr[9] = (SECT_SIZE_PER(target) >> 16) & 0xff;
	arr[10] = (SECT_SIZE_PER(target) >> 8) & 0xff;
	arr[11] = SECT_SIZE_PER(target) & 0xff;
	return fill_from_dev_buffer(scp, arr,
				    min(alloc_len, SDEBUG_READCAP16_ARR_SZ));
}

#define SDEBUG_MAX_TGTPGS_ARR_SZ 1412

static int resp_report_tgtpgs(struct scsi_cmnd * scp,
			      struct sdebug_dev_info * devip)
{
	unsigned char *cmd = (unsigned char *)scp->cmnd;
	unsigned char * arr;
	int host_no = devip->sdbg_host->shost->host_no;
	int n, ret, alen, rlen;
	int port_group_a, port_group_b, port_a, port_b;

	alen = ((cmd[6] << 24) + (cmd[7] << 16) + (cmd[8] << 8)
		+ cmd[9]);

	arr = kzalloc(SDEBUG_MAX_TGTPGS_ARR_SZ, GFP_ATOMIC);
	if (! arr)
		return DID_REQUEUE << 16;
	/*
	 * EVPD page 0x88 states we have two ports, one
	 * real and a fake port with no device connected.
	 * So we create two port groups with one port each
	 * and set the group with port B to unavailable.
	 */
	port_a = 0x1; /* relative port A */
	port_b = 0x2; /* relative port B */
	port_group_a = (((host_no + 1) & 0x7f) << 8) +
	    (devip->channel & 0x7f);
	port_group_b = (((host_no + 1) & 0x7f) << 8) +
	    (devip->channel & 0x7f) + 0x80;

	/*
	 * The asymmetric access state is cycled according to the host_id.
	 */
	n = 4;
	if (0 == scsi_debug_vpd_use_hostno) {
	    arr[n++] = host_no % 3; /* Asymm access state */
	    arr[n++] = 0x0F; /* claim: all states are supported */
	} else {
	    arr[n++] = 0x0; /* Active/Optimized path */
	    arr[n++] = 0x01; /* claim: only support active/optimized paths */
	}
	arr[n++] = (port_group_a >> 8) & 0xff;
	arr[n++] = port_group_a & 0xff;
	arr[n++] = 0;    /* Reserved */
	arr[n++] = 0;    /* Status code */
	arr[n++] = 0;    /* Vendor unique */
	arr[n++] = 0x1;  /* One port per group */
	arr[n++] = 0;    /* Reserved */
	arr[n++] = 0;    /* Reserved */
	arr[n++] = (port_a >> 8) & 0xff;
	arr[n++] = port_a & 0xff;
	arr[n++] = 3;    /* Port unavailable */
	arr[n++] = 0x08; /* claim: only unavailalbe paths are supported */
	arr[n++] = (port_group_b >> 8) & 0xff;
	arr[n++] = port_group_b & 0xff;
	arr[n++] = 0;    /* Reserved */
	arr[n++] = 0;    /* Status code */
	arr[n++] = 0;    /* Vendor unique */
	arr[n++] = 0x1;  /* One port per group */
	arr[n++] = 0;    /* Reserved */
	arr[n++] = 0;    /* Reserved */
	arr[n++] = (port_b >> 8) & 0xff;
	arr[n++] = port_b & 0xff;

	rlen = n - 4;
	arr[0] = (rlen >> 24) & 0xff;
	arr[1] = (rlen >> 16) & 0xff;
	arr[2] = (rlen >> 8) & 0xff;
	arr[3] = rlen & 0xff;

	/*
	 * Return the smallest value of either
	 * - The allocated length
	 * - The constructed command length
	 * - The maximum array size
	 */
	rlen = min(alen,n);
	ret = fill_from_dev_buffer(scp, arr,
				   min(rlen, SDEBUG_MAX_TGTPGS_ARR_SZ));
	kfree(arr);
	return ret;
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
	unsigned char ch_ctrl_m_pg[] = {/* 0xa, 10, */ 0x6, 0, 0, 0, 0, 0,
				        0, 0, 0, 0};
	unsigned char d_ctrl_m_pg[] = {0xa, 10, 2, 0, 0, 0, 0, 0,
				     0, 0, 0x2, 0x4b};

	if (scsi_debug_dsense)
		ctrl_m_pg[2] |= 0x4;
	else
		ctrl_m_pg[2] &= ~0x4;
	memcpy(p, ctrl_m_pg, sizeof(ctrl_m_pg));
	if (1 == pcontrol)
		memcpy(p + 2, ch_ctrl_m_pg, sizeof(ch_ctrl_m_pg));
	else if (2 == pcontrol)
		memcpy(p, d_ctrl_m_pg, sizeof(d_ctrl_m_pg));
	return sizeof(ctrl_m_pg);
}


static int resp_iec_m_pg(unsigned char * p, int pcontrol, int target)
{	/* Informational Exceptions control mode page for mode_sense */
	unsigned char ch_iec_m_pg[] = {/* 0x1c, 0xa, */ 0x4, 0xf, 0, 0, 0, 0,
				       0, 0, 0x0, 0x0};
	unsigned char d_iec_m_pg[] = {0x1c, 0xa, 0x08, 0, 0, 0, 0, 0,
				      0, 0, 0x0, 0x0};

	memcpy(p, iec_m_pg, sizeof(iec_m_pg));
	if (1 == pcontrol)
		memcpy(p + 2, ch_iec_m_pg, sizeof(ch_iec_m_pg));
	else if (2 == pcontrol)
		memcpy(p, d_iec_m_pg, sizeof(d_iec_m_pg));
	return sizeof(iec_m_pg);
}

static int resp_sas_sf_m_pg(unsigned char * p, int pcontrol, int target)
{	/* SAS SSP mode page - short format for mode_sense */
	unsigned char sas_sf_m_pg[] = {0x19, 0x6,
		0x6, 0x0, 0x7, 0xd0, 0x0, 0x0};

	memcpy(p, sas_sf_m_pg, sizeof(sas_sf_m_pg));
	if (1 == pcontrol)
		memset(p + 2, 0, sizeof(sas_sf_m_pg) - 2);
	return sizeof(sas_sf_m_pg);
}


static int resp_sas_pcd_m_spg(unsigned char * p, int pcontrol, int target,
			      int target_dev_id)
{	/* SAS phy control and discover mode page for mode_sense */
	unsigned char sas_pcd_m_pg[] = {0x59, 0x1, 0, 0x64, 0, 0x6, 0, 2,
		    0, 0, 0, 0, 0x10, 0x9, 0x8, 0x0,
		    0x52, 0x22, 0x22, 0x20, 0x0, 0x0, 0x0, 0x0,
		    0x51, 0x11, 0x11, 0x10, 0x0, 0x0, 0x0, 0x1,
		    0x2, 0, 0, 0, 0, 0, 0, 0,
		    0x88, 0x99, 0, 0, 0, 0, 0, 0,
		    0, 0, 0, 0, 0, 0, 0, 0,
		    0, 1, 0, 0, 0x10, 0x9, 0x8, 0x0,
		    0x52, 0x22, 0x22, 0x20, 0x0, 0x0, 0x0, 0x0,
		    0x51, 0x11, 0x11, 0x10, 0x0, 0x0, 0x0, 0x1,
		    0x3, 0, 0, 0, 0, 0, 0, 0,
		    0x88, 0x99, 0, 0, 0, 0, 0, 0,
		    0, 0, 0, 0, 0, 0, 0, 0,
		};
	int port_a, port_b;

	port_a = target_dev_id + 1;
	port_b = port_a + 1;
	memcpy(p, sas_pcd_m_pg, sizeof(sas_pcd_m_pg));
	p[20] = (port_a >> 24);
	p[21] = (port_a >> 16) & 0xff;
	p[22] = (port_a >> 8) & 0xff;
	p[23] = port_a & 0xff;
	p[48 + 20] = (port_b >> 24);
	p[48 + 21] = (port_b >> 16) & 0xff;
	p[48 + 22] = (port_b >> 8) & 0xff;
	p[48 + 23] = port_b & 0xff;
	if (1 == pcontrol)
		memset(p + 4, 0, sizeof(sas_pcd_m_pg) - 4);
	return sizeof(sas_pcd_m_pg);
}

static int resp_sas_sha_m_spg(unsigned char * p, int pcontrol)
{	/* SAS SSP shared protocol specific port mode subpage */
	unsigned char sas_sha_m_pg[] = {0x59, 0x2, 0, 0xc, 0, 0x6, 0x10, 0,
		    0, 0, 0, 0, 0, 0, 0, 0,
		};

	memcpy(p, sas_sha_m_pg, sizeof(sas_sha_m_pg));
	if (1 == pcontrol)
		memset(p + 4, 0, sizeof(sas_sha_m_pg) - 4);
	return sizeof(sas_sha_m_pg);
}

#define SDEBUG_MAX_MSENSE_SZ 256

static int resp_mode_sense(struct scsi_cmnd * scp, int target,
			   struct sdebug_dev_info * devip)
{
	unsigned char dbd, llbaa;
	int pcontrol, pcode, subpcode, bd_len;
	unsigned char dev_spec;
	int k, alloc_len, msense_6, offset, len, errsts, target_dev_id;
	unsigned char * ap;
	unsigned char arr[SDEBUG_MAX_MSENSE_SZ];
	unsigned char *cmd = (unsigned char *)scp->cmnd;

	if ((errsts = check_readiness(scp, 1, devip)))
		return errsts;
	dbd = !!(cmd[1] & 0x8);
	pcontrol = (cmd[2] & 0xc0) >> 6;
	pcode = cmd[2] & 0x3f;
	subpcode = cmd[3];
	msense_6 = (MODE_SENSE == cmd[0]);
	llbaa = msense_6 ? 0 : !!(cmd[1] & 0x10);
	if ((0 == scsi_debug_ptype) && (0 == dbd))
		bd_len = llbaa ? 16 : 8;
	else
		bd_len = 0;
	alloc_len = msense_6 ? cmd[4] : ((cmd[7] << 8) | cmd[8]);
	memset(arr, 0, SDEBUG_MAX_MSENSE_SZ);
	if (0x3 == pcontrol) {  /* Saving values not supported */
		mk_sense_buffer(devip, ILLEGAL_REQUEST, SAVING_PARAMS_UNSUP,
			       	0);
		return check_condition_result;
	}
	target_dev_id = ((devip->sdbg_host->shost->host_no + 1) * 2000) +
			(devip->target * 1000) - 3;
	/* set DPOFUA bit for disks */
	if (0 == scsi_debug_ptype)
		dev_spec = (DEV_READONLY(target) ? 0x80 : 0x0) | 0x10;
	else
		dev_spec = 0x0;
	if (msense_6) {
		arr[2] = dev_spec;
		arr[3] = bd_len;
		offset = 4;
	} else {
		arr[3] = dev_spec;
		if (16 == bd_len)
			arr[4] = 0x1;	/* set LONGLBA bit */
		arr[7] = bd_len;	/* assume 255 or less */
		offset = 8;
	}
	ap = arr + offset;
	if ((bd_len > 0) && (0 == sdebug_capacity)) {
		if (scsi_debug_virtual_gb > 0) {
			sdebug_capacity = 2048 * 1024;
			sdebug_capacity *= scsi_debug_virtual_gb;
		} else
			sdebug_capacity = sdebug_store_sectors;
	}
	if (8 == bd_len) {
		if (sdebug_capacity > 0xfffffffe) {
			ap[0] = 0xff;
			ap[1] = 0xff;
			ap[2] = 0xff;
			ap[3] = 0xff;
		} else {
			ap[0] = (sdebug_capacity >> 24) & 0xff;
			ap[1] = (sdebug_capacity >> 16) & 0xff;
			ap[2] = (sdebug_capacity >> 8) & 0xff;
			ap[3] = sdebug_capacity & 0xff;
		}
        	ap[6] = (SECT_SIZE_PER(target) >> 8) & 0xff;
        	ap[7] = SECT_SIZE_PER(target) & 0xff;
		offset += bd_len;
		ap = arr + offset;
	} else if (16 == bd_len) {
		unsigned long long capac = sdebug_capacity;

        	for (k = 0; k < 8; ++k, capac >>= 8)
                	ap[7 - k] = capac & 0xff;
        	ap[12] = (SECT_SIZE_PER(target) >> 24) & 0xff;
        	ap[13] = (SECT_SIZE_PER(target) >> 16) & 0xff;
        	ap[14] = (SECT_SIZE_PER(target) >> 8) & 0xff;
        	ap[15] = SECT_SIZE_PER(target) & 0xff;
		offset += bd_len;
		ap = arr + offset;
	}

	if ((subpcode > 0x0) && (subpcode < 0xff) && (0x19 != pcode)) {
		/* TODO: Control Extension page */
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
	case 0x19:	/* if spc==1 then sas phy, control+discover */
		if ((subpcode > 0x2) && (subpcode < 0xff)) {
		        mk_sense_buffer(devip, ILLEGAL_REQUEST,
					INVALID_FIELD_IN_CDB, 0);
			return check_condition_result;
	        }
		len = 0;
		if ((0x0 == subpcode) || (0xff == subpcode))
			len += resp_sas_sf_m_pg(ap + len, pcontrol, target);
		if ((0x1 == subpcode) || (0xff == subpcode))
			len += resp_sas_pcd_m_spg(ap + len, pcontrol, target,
						  target_dev_id);
		if ((0x2 == subpcode) || (0xff == subpcode))
			len += resp_sas_sha_m_spg(ap + len, pcontrol);
		offset += len;
		break;
	case 0x1c:	/* Informational Exceptions Mode page, all devices */
		len = resp_iec_m_pg(ap, pcontrol, target);
		offset += len;
		break;
	case 0x3f:	/* Read all Mode pages */
		if ((0 == subpcode) || (0xff == subpcode)) {
			len = resp_err_recov_pg(ap, pcontrol, target);
			len += resp_disconnect_pg(ap + len, pcontrol, target);
			len += resp_format_pg(ap + len, pcontrol, target);
			len += resp_caching_pg(ap + len, pcontrol, target);
			len += resp_ctrl_m_pg(ap + len, pcontrol, target);
			len += resp_sas_sf_m_pg(ap + len, pcontrol, target);
			if (0xff == subpcode) {
				len += resp_sas_pcd_m_spg(ap + len, pcontrol,
						  target, target_dev_id);
				len += resp_sas_sha_m_spg(ap + len, pcontrol);
			}
			len += resp_iec_m_pg(ap + len, pcontrol, target);
		} else {
			mk_sense_buffer(devip, ILLEGAL_REQUEST,
					INVALID_FIELD_IN_CDB, 0);
			return check_condition_result;
                }
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

#define SDEBUG_MAX_MSELECT_SZ 512

static int resp_mode_select(struct scsi_cmnd * scp, int mselect6,
			    struct sdebug_dev_info * devip)
{
	int pf, sp, ps, md_len, bd_len, off, spf, pg_len;
	int param_len, res, errsts, mpage;
	unsigned char arr[SDEBUG_MAX_MSELECT_SZ];
	unsigned char *cmd = (unsigned char *)scp->cmnd;

	if ((errsts = check_readiness(scp, 1, devip)))
		return errsts;
	memset(arr, 0, sizeof(arr));
	pf = cmd[1] & 0x10;
	sp = cmd[1] & 0x1;
	param_len = mselect6 ? cmd[4] : ((cmd[7] << 8) + cmd[8]);
	if ((0 == pf) || sp || (param_len > SDEBUG_MAX_MSELECT_SZ)) {
		mk_sense_buffer(devip, ILLEGAL_REQUEST,
				INVALID_FIELD_IN_CDB, 0);
		return check_condition_result;
	}
        res = fetch_to_dev_buffer(scp, arr, param_len);
        if (-1 == res)
                return (DID_ERROR << 16);
        else if ((res < param_len) &&
                 (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts))
                printk(KERN_INFO "scsi_debug: mode_select: cdb indicated=%d, "
                       " IO sent=%d bytes\n", param_len, res);
	md_len = mselect6 ? (arr[0] + 1) : ((arr[0] << 8) + arr[1] + 2);
	bd_len = mselect6 ? arr[3] : ((arr[6] << 8) + arr[7]);
	if (md_len > 2) {
		mk_sense_buffer(devip, ILLEGAL_REQUEST,
				INVALID_FIELD_IN_PARAM_LIST, 0);
		return check_condition_result;
	}
	off = bd_len + (mselect6 ? 4 : 8);
	mpage = arr[off] & 0x3f;
	ps = !!(arr[off] & 0x80);
	if (ps) {
		mk_sense_buffer(devip, ILLEGAL_REQUEST,
				INVALID_FIELD_IN_PARAM_LIST, 0);
		return check_condition_result;
	}
	spf = !!(arr[off] & 0x40);
	pg_len = spf ? ((arr[off + 2] << 8) + arr[off + 3] + 4) :
		       (arr[off + 1] + 2);
	if ((pg_len + off) > param_len) {
		mk_sense_buffer(devip, ILLEGAL_REQUEST,
				PARAMETER_LIST_LENGTH_ERR, 0);
		return check_condition_result;
	}
	switch (mpage) {
	case 0xa:      /* Control Mode page */
		if (ctrl_m_pg[1] == arr[off + 1]) {
			memcpy(ctrl_m_pg + 2, arr + off + 2,
			       sizeof(ctrl_m_pg) - 2);
			scsi_debug_dsense = !!(ctrl_m_pg[2] & 0x4);
			return 0;
		}
		break;
	case 0x1c:      /* Informational Exceptions Mode page */
		if (iec_m_pg[1] == arr[off + 1]) {
			memcpy(iec_m_pg + 2, arr + off + 2,
			       sizeof(iec_m_pg) - 2);
			return 0;
		}
		break;
	default:
		break;
	}
	mk_sense_buffer(devip, ILLEGAL_REQUEST,
			INVALID_FIELD_IN_PARAM_LIST, 0);
	return check_condition_result;
}

static int resp_temp_l_pg(unsigned char * arr)
{
	unsigned char temp_l_pg[] = {0x0, 0x0, 0x3, 0x2, 0x0, 38,
				     0x0, 0x1, 0x3, 0x2, 0x0, 65,
		};

        memcpy(arr, temp_l_pg, sizeof(temp_l_pg));
        return sizeof(temp_l_pg);
}

static int resp_ie_l_pg(unsigned char * arr)
{
	unsigned char ie_l_pg[] = {0x0, 0x0, 0x3, 0x3, 0x0, 0x0, 38,
		};

        memcpy(arr, ie_l_pg, sizeof(ie_l_pg));
	if (iec_m_pg[2] & 0x4) {	/* TEST bit set */
		arr[4] = THRESHOLD_EXCEEDED;
		arr[5] = 0xff;
	}
        return sizeof(ie_l_pg);
}

#define SDEBUG_MAX_LSENSE_SZ 512

static int resp_log_sense(struct scsi_cmnd * scp,
                          struct sdebug_dev_info * devip)
{
	int ppc, sp, pcontrol, pcode, subpcode, alloc_len, errsts, len, n;
	unsigned char arr[SDEBUG_MAX_LSENSE_SZ];
	unsigned char *cmd = (unsigned char *)scp->cmnd;

	if ((errsts = check_readiness(scp, 1, devip)))
		return errsts;
	memset(arr, 0, sizeof(arr));
	ppc = cmd[1] & 0x2;
	sp = cmd[1] & 0x1;
	if (ppc || sp) {
		mk_sense_buffer(devip, ILLEGAL_REQUEST,
				INVALID_FIELD_IN_CDB, 0);
		return check_condition_result;
	}
	pcontrol = (cmd[2] & 0xc0) >> 6;
	pcode = cmd[2] & 0x3f;
	subpcode = cmd[3] & 0xff;
	alloc_len = (cmd[7] << 8) + cmd[8];
	arr[0] = pcode;
	if (0 == subpcode) {
		switch (pcode) {
		case 0x0:	/* Supported log pages log page */
			n = 4;
			arr[n++] = 0x0;		/* this page */
			arr[n++] = 0xd;		/* Temperature */
			arr[n++] = 0x2f;	/* Informational exceptions */
			arr[3] = n - 4;
			break;
		case 0xd:	/* Temperature log page */
			arr[3] = resp_temp_l_pg(arr + 4);
			break;
		case 0x2f:	/* Informational exceptions log page */
			arr[3] = resp_ie_l_pg(arr + 4);
			break;
		default:
			mk_sense_buffer(devip, ILLEGAL_REQUEST,
					INVALID_FIELD_IN_CDB, 0);
			return check_condition_result;
		}
	} else if (0xff == subpcode) {
		arr[0] |= 0x40;
		arr[1] = subpcode;
		switch (pcode) {
		case 0x0:	/* Supported log pages and subpages log page */
			n = 4;
			arr[n++] = 0x0;
			arr[n++] = 0x0;		/* 0,0 page */
			arr[n++] = 0x0;
			arr[n++] = 0xff;	/* this page */
			arr[n++] = 0xd;
			arr[n++] = 0x0;		/* Temperature */
			arr[n++] = 0x2f;
			arr[n++] = 0x0;	/* Informational exceptions */
			arr[3] = n - 4;
			break;
		case 0xd:	/* Temperature subpages */
			n = 4;
			arr[n++] = 0xd;
			arr[n++] = 0x0;		/* Temperature */
			arr[3] = n - 4;
			break;
		case 0x2f:	/* Informational exceptions subpages */
			n = 4;
			arr[n++] = 0x2f;
			arr[n++] = 0x0;		/* Informational exceptions */
			arr[3] = n - 4;
			break;
		default:
			mk_sense_buffer(devip, ILLEGAL_REQUEST,
					INVALID_FIELD_IN_CDB, 0);
			return check_condition_result;
		}
	} else {
		mk_sense_buffer(devip, ILLEGAL_REQUEST,
				INVALID_FIELD_IN_CDB, 0);
		return check_condition_result;
	}
	len = min(((arr[2] << 8) + arr[3]) + 4, alloc_len);
	return fill_from_dev_buffer(scp, arr,
		    min(len, SDEBUG_MAX_INQ_ARR_SZ));
}

static int resp_read(struct scsi_cmnd * SCpnt, unsigned long long lba,
		     unsigned int num, struct sdebug_dev_info * devip)
{
	unsigned long iflags;
	unsigned int block, from_bottom;
	unsigned long long u;
	int ret;

	if (lba + num > sdebug_capacity) {
		mk_sense_buffer(devip, ILLEGAL_REQUEST, ADDR_OUT_OF_RANGE,
				0);
		return check_condition_result;
	}
	/* transfer length excessive (tie in to block limits VPD page) */
	if (num > sdebug_store_sectors) {
		mk_sense_buffer(devip, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB,
				0);
		return check_condition_result;
	}
	if ((SCSI_DEBUG_OPT_MEDIUM_ERR & scsi_debug_opts) &&
	    (lba <= OPT_MEDIUM_ERR_ADDR) &&
	    ((lba + num) > OPT_MEDIUM_ERR_ADDR)) {
		/* claim unrecoverable read error */
		mk_sense_buffer(devip, MEDIUM_ERROR, UNRECOVERED_READ_ERR,
				0);
		/* set info field and valid bit for fixed descriptor */
		if (0x70 == (devip->sense_buff[0] & 0x7f)) {
			devip->sense_buff[0] |= 0x80;	/* Valid bit */
			ret = OPT_MEDIUM_ERR_ADDR;
			devip->sense_buff[3] = (ret >> 24) & 0xff;
			devip->sense_buff[4] = (ret >> 16) & 0xff;
			devip->sense_buff[5] = (ret >> 8) & 0xff;
			devip->sense_buff[6] = ret & 0xff;
		}
		return check_condition_result;
	}
	read_lock_irqsave(&atomic_rw, iflags);
	if ((lba + num) <= sdebug_store_sectors)
		ret = fill_from_dev_buffer(SCpnt,
					   fake_storep + (lba * SECT_SIZE),
			   		   num * SECT_SIZE);
	else {
		/* modulo when one arg is 64 bits needs do_div() */
		u = lba;
		block = do_div(u, sdebug_store_sectors);
		from_bottom = 0;
		if ((block + num) > sdebug_store_sectors)
			from_bottom = (block + num) - sdebug_store_sectors;
		ret = fill_from_dev_buffer(SCpnt,
					   fake_storep + (block * SECT_SIZE),
			   		   (num - from_bottom) * SECT_SIZE);
		if ((0 == ret) && (from_bottom > 0))
			ret = fill_from_dev_buffer(SCpnt, fake_storep,
						   from_bottom * SECT_SIZE);
	}
	read_unlock_irqrestore(&atomic_rw, iflags);
	return ret;
}

static int resp_write(struct scsi_cmnd * SCpnt, unsigned long long lba,
		      unsigned int num, struct sdebug_dev_info * devip)
{
	unsigned long iflags;
	unsigned int block, to_bottom;
	unsigned long long u;
	int res;

	if (lba + num > sdebug_capacity) {
		mk_sense_buffer(devip, ILLEGAL_REQUEST, ADDR_OUT_OF_RANGE,
			       	0);
		return check_condition_result;
	}
	/* transfer length excessive (tie in to block limits VPD page) */
	if (num > sdebug_store_sectors) {
		mk_sense_buffer(devip, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB,
				0);
		return check_condition_result;
	}

	write_lock_irqsave(&atomic_rw, iflags);
	if ((lba + num) <= sdebug_store_sectors)
		res = fetch_to_dev_buffer(SCpnt,
					  fake_storep + (lba * SECT_SIZE),
			   		  num * SECT_SIZE);
	else {
		/* modulo when one arg is 64 bits needs do_div() */
		u = lba;
		block = do_div(u, sdebug_store_sectors);
		to_bottom = 0;
		if ((block + num) > sdebug_store_sectors)
			to_bottom = (block + num) - sdebug_store_sectors;
		res = fetch_to_dev_buffer(SCpnt,
					  fake_storep + (block * SECT_SIZE),
			   		  (num - to_bottom) * SECT_SIZE);
		if ((0 == res) && (to_bottom > 0))
			res = fetch_to_dev_buffer(SCpnt, fake_storep,
						  to_bottom * SECT_SIZE);
	}
	write_unlock_irqrestore(&atomic_rw, iflags);
	if (-1 == res)
		return (DID_ERROR << 16);
	else if ((res < (num * SECT_SIZE)) &&
		 (SCSI_DEBUG_OPT_NOISE & scsi_debug_opts))
		printk(KERN_INFO "scsi_debug: write: cdb indicated=%u, "
		       " IO sent=%d bytes\n", num * SECT_SIZE, res);
	return 0;
}

#define SDEBUG_RLUN_ARR_SZ 256

static int resp_report_luns(struct scsi_cmnd * scp,
			    struct sdebug_dev_info * devip)
{
	unsigned int alloc_len;
	int lun_cnt, i, upper, num, n, wlun, lun;
	unsigned char *cmd = (unsigned char *)scp->cmnd;
	int select_report = (int)cmd[2];
	struct scsi_lun *one_lun;
	unsigned char arr[SDEBUG_RLUN_ARR_SZ];
	unsigned char * max_addr;

	alloc_len = cmd[9] + (cmd[8] << 8) + (cmd[7] << 16) + (cmd[6] << 24);
	if ((alloc_len < 4) || (select_report > 2)) {
		mk_sense_buffer(devip, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB,
			       	0);
		return check_condition_result;
	}
	/* can produce response with up to 16k luns (lun 0 to lun 16383) */
	memset(arr, 0, SDEBUG_RLUN_ARR_SZ);
	lun_cnt = scsi_debug_max_luns;
	if (1 == select_report)
		lun_cnt = 0;
	else if (scsi_debug_no_lun_0 && (lun_cnt > 0))
		--lun_cnt;
	wlun = (select_report > 0) ? 1 : 0;
	num = lun_cnt + wlun;
	arr[2] = ((sizeof(struct scsi_lun) * num) >> 8) & 0xff;
	arr[3] = (sizeof(struct scsi_lun) * num) & 0xff;
	n = min((int)((SDEBUG_RLUN_ARR_SZ - 8) /
			    sizeof(struct scsi_lun)), num);
	if (n < num) {
		wlun = 0;
		lun_cnt = n;
	}
	one_lun = (struct scsi_lun *) &arr[8];
	max_addr = arr + SDEBUG_RLUN_ARR_SZ;
	for (i = 0, lun = (scsi_debug_no_lun_0 ? 1 : 0);
             ((i < lun_cnt) && ((unsigned char *)(one_lun + i) < max_addr));
	     i++, lun++) {
		upper = (lun >> 8) & 0x3f;
		if (upper)
			one_lun[i].scsi_lun[0] =
			    (upper | (SAM2_LUN_ADDRESS_METHOD << 6));
		one_lun[i].scsi_lun[1] = lun & 0xff;
	}
	if (wlun) {
		one_lun[i].scsi_lun[0] = (SAM2_WLUN_REPORT_LUNS >> 8) & 0xff;
		one_lun[i].scsi_lun[1] = SAM2_WLUN_REPORT_LUNS & 0xff;
		i++;
	}
	alloc_len = (unsigned char *)(one_lun + i) - arr;
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
	if (NULL == devip)
		return 1;	/* no resources, will be marked offline */
	sdp->hostdata = devip;
	if (sdp->host->cmd_per_lun)
		scsi_adjust_queue_depth(sdp, SDEBUG_TAGGED_QUEUING,
					sdp->host->cmd_per_lun);
	blk_queue_max_segment_size(sdp->request_queue, 256 * 1024);
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
		open_devip = kzalloc(sizeof(*open_devip),GFP_ATOMIC);
		if (NULL == open_devip) {
			printk(KERN_ERR "%s: out of memory at line %d\n",
				__FUNCTION__, __LINE__);
			return NULL;
		}
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
		if (sdev->lun == SAM2_WLUN_REPORT_LUNS)
			open_devip->wlun = SAM2_WLUN_REPORT_LUNS & 0xff;
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
	num_sectors = (int)sdebug_store_sectors;
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

/* Note: The following macros create attribute files in the
   /sys/module/scsi_debug/parameters directory. Unfortunately this
   driver is unaware of a change and cannot trigger auxiliary actions
   as it can when the corresponding attribute in the
   /sys/bus/pseudo/drivers/scsi_debug directory is changed.
 */
module_param_named(add_host, scsi_debug_add_host, int, S_IRUGO | S_IWUSR);
module_param_named(delay, scsi_debug_delay, int, S_IRUGO | S_IWUSR);
module_param_named(dev_size_mb, scsi_debug_dev_size_mb, int, S_IRUGO);
module_param_named(dsense, scsi_debug_dsense, int, S_IRUGO | S_IWUSR);
module_param_named(every_nth, scsi_debug_every_nth, int, S_IRUGO | S_IWUSR);
module_param_named(fake_rw, scsi_debug_fake_rw, int, S_IRUGO | S_IWUSR);
module_param_named(max_luns, scsi_debug_max_luns, int, S_IRUGO | S_IWUSR);
module_param_named(no_lun_0, scsi_debug_no_lun_0, int, S_IRUGO | S_IWUSR);
module_param_named(num_parts, scsi_debug_num_parts, int, S_IRUGO);
module_param_named(num_tgts, scsi_debug_num_tgts, int, S_IRUGO | S_IWUSR);
module_param_named(opts, scsi_debug_opts, int, S_IRUGO | S_IWUSR);
module_param_named(ptype, scsi_debug_ptype, int, S_IRUGO | S_IWUSR);
module_param_named(scsi_level, scsi_debug_scsi_level, int, S_IRUGO);
module_param_named(virtual_gb, scsi_debug_virtual_gb, int, S_IRUGO | S_IWUSR);
module_param_named(vpd_use_hostno, scsi_debug_vpd_use_hostno, int,
		   S_IRUGO | S_IWUSR);

MODULE_AUTHOR("Eric Youngdale + Douglas Gilbert");
MODULE_DESCRIPTION("SCSI debug adapter driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(SCSI_DEBUG_VERSION);

MODULE_PARM_DESC(add_host, "0..127 hosts allowed(def=1)");
MODULE_PARM_DESC(delay, "# of jiffies to delay response(def=1)");
MODULE_PARM_DESC(dev_size_mb, "size in MB of ram shared by devs(def=8)");
MODULE_PARM_DESC(dsense, "use descriptor sense format(def=0 -> fixed)");
MODULE_PARM_DESC(every_nth, "timeout every nth command(def=0)");
MODULE_PARM_DESC(fake_rw, "fake reads/writes instead of copying (def=0)");
MODULE_PARM_DESC(max_luns, "number of LUNs per target to simulate(def=1)");
MODULE_PARM_DESC(no_lun_0, "no LU number 0 (def=0 -> have lun 0)");
MODULE_PARM_DESC(num_parts, "number of partitions(def=0)");
MODULE_PARM_DESC(num_tgts, "number of targets per host to simulate(def=1)");
MODULE_PARM_DESC(opts, "1->noise, 2->medium_err, 4->timeout, 8->recovered_err... (def=0)");
MODULE_PARM_DESC(ptype, "SCSI peripheral type(def=0[disk])");
MODULE_PARM_DESC(scsi_level, "SCSI level to simulate(def=5[SPC-3])");
MODULE_PARM_DESC(virtual_gb, "virtual gigabyte size (def=0 -> use dev_size_mb)");
MODULE_PARM_DESC(vpd_use_hostno, "0 -> dev ids ignore hostno (def=1 -> unique dev ids)");


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

static ssize_t sdebug_fake_rw_show(struct device_driver * ddp, char * buf)
{
        return scnprintf(buf, PAGE_SIZE, "%d\n", scsi_debug_fake_rw);
}
static ssize_t sdebug_fake_rw_store(struct device_driver * ddp,
				    const char * buf, size_t count)
{
        int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		scsi_debug_fake_rw = n;
		return count;
	}
	return -EINVAL;
}
DRIVER_ATTR(fake_rw, S_IRUGO | S_IWUSR, sdebug_fake_rw_show,
	    sdebug_fake_rw_store);

static ssize_t sdebug_no_lun_0_show(struct device_driver * ddp, char * buf)
{
        return scnprintf(buf, PAGE_SIZE, "%d\n", scsi_debug_no_lun_0);
}
static ssize_t sdebug_no_lun_0_store(struct device_driver * ddp,
				     const char * buf, size_t count)
{
        int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		scsi_debug_no_lun_0 = n;
		return count;
	}
	return -EINVAL;
}
DRIVER_ATTR(no_lun_0, S_IRUGO | S_IWUSR, sdebug_no_lun_0_show,
	    sdebug_no_lun_0_store);

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

static ssize_t sdebug_virtual_gb_show(struct device_driver * ddp, char * buf)
{
        return scnprintf(buf, PAGE_SIZE, "%d\n", scsi_debug_virtual_gb);
}
static ssize_t sdebug_virtual_gb_store(struct device_driver * ddp,
				       const char * buf, size_t count)
{
        int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		scsi_debug_virtual_gb = n;
		if (scsi_debug_virtual_gb > 0) {
			sdebug_capacity = 2048 * 1024;
			sdebug_capacity *= scsi_debug_virtual_gb;
		} else
			sdebug_capacity = sdebug_store_sectors;
		return count;
	}
	return -EINVAL;
}
DRIVER_ATTR(virtual_gb, S_IRUGO | S_IWUSR, sdebug_virtual_gb_show,
	    sdebug_virtual_gb_store);

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

static ssize_t sdebug_vpd_use_hostno_show(struct device_driver * ddp,
					  char * buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", scsi_debug_vpd_use_hostno);
}
static ssize_t sdebug_vpd_use_hostno_store(struct device_driver * ddp,
					   const char * buf, size_t count)
{
	int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		scsi_debug_vpd_use_hostno = n;
		return count;
	}
	return -EINVAL;
}
DRIVER_ATTR(vpd_use_hostno, S_IRUGO | S_IWUSR, sdebug_vpd_use_hostno_show,
	    sdebug_vpd_use_hostno_store);

/* Note: The following function creates attribute files in the
   /sys/bus/pseudo/drivers/scsi_debug directory. The advantage of these
   files (over those found in the /sys/module/scsi_debug/parameters
   directory) is that auxiliary actions can be triggered when an attribute
   is changed. For example see: sdebug_add_host_store() above.
 */
static int do_create_driverfs_files(void)
{
	int ret;

	ret = driver_create_file(&sdebug_driverfs_driver, &driver_attr_add_host);
	ret |= driver_create_file(&sdebug_driverfs_driver, &driver_attr_delay);
	ret |= driver_create_file(&sdebug_driverfs_driver, &driver_attr_dev_size_mb);
	ret |= driver_create_file(&sdebug_driverfs_driver, &driver_attr_dsense);
	ret |= driver_create_file(&sdebug_driverfs_driver, &driver_attr_every_nth);
	ret |= driver_create_file(&sdebug_driverfs_driver, &driver_attr_fake_rw);
	ret |= driver_create_file(&sdebug_driverfs_driver, &driver_attr_max_luns);
	ret |= driver_create_file(&sdebug_driverfs_driver, &driver_attr_no_lun_0);
	ret |= driver_create_file(&sdebug_driverfs_driver, &driver_attr_num_parts);
	ret |= driver_create_file(&sdebug_driverfs_driver, &driver_attr_num_tgts);
	ret |= driver_create_file(&sdebug_driverfs_driver, &driver_attr_ptype);
	ret |= driver_create_file(&sdebug_driverfs_driver, &driver_attr_opts);
	ret |= driver_create_file(&sdebug_driverfs_driver, &driver_attr_scsi_level);
	ret |= driver_create_file(&sdebug_driverfs_driver, &driver_attr_virtual_gb);
	ret |= driver_create_file(&sdebug_driverfs_driver, &driver_attr_vpd_use_hostno);
	return ret;
}

static void do_remove_driverfs_files(void)
{
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_vpd_use_hostno);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_virtual_gb);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_scsi_level);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_opts);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_ptype);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_num_tgts);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_num_parts);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_no_lun_0);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_max_luns);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_fake_rw);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_every_nth);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_dsense);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_dev_size_mb);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_delay);
	driver_remove_file(&sdebug_driverfs_driver, &driver_attr_add_host);
}

static int __init scsi_debug_init(void)
{
	unsigned int sz;
	int host_to_add;
	int k;
	int ret;

	if (scsi_debug_dev_size_mb < 1)
		scsi_debug_dev_size_mb = 1;  /* force minimum 1 MB ramdisk */
	sdebug_store_size = (unsigned int)scsi_debug_dev_size_mb * 1048576;
	sdebug_store_sectors = sdebug_store_size / SECT_SIZE;
	if (scsi_debug_virtual_gb > 0) {
		sdebug_capacity = 2048 * 1024;
		sdebug_capacity *= scsi_debug_virtual_gb;
	} else
		sdebug_capacity = sdebug_store_sectors;

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

	ret = device_register(&pseudo_primary);
	if (ret < 0) {
		printk(KERN_WARNING "scsi_debug: device_register error: %d\n",
			ret);
		goto free_vm;
	}
	ret = bus_register(&pseudo_lld_bus);
	if (ret < 0) {
		printk(KERN_WARNING "scsi_debug: bus_register error: %d\n",
			ret);
		goto dev_unreg;
	}
	ret = driver_register(&sdebug_driverfs_driver);
	if (ret < 0) {
		printk(KERN_WARNING "scsi_debug: driver_register error: %d\n",
			ret);
		goto bus_unreg;
	}
	ret = do_create_driverfs_files();
	if (ret < 0) {
		printk(KERN_WARNING "scsi_debug: driver_create_file error: %d\n",
			ret);
		goto del_files;
	}

	init_all_queued();

	sdebug_driver_template.proc_name = sdebug_proc_name;

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

del_files:
	do_remove_driverfs_files();
	driver_unregister(&sdebug_driverfs_driver);
bus_unreg:
	bus_unregister(&pseudo_lld_bus);
dev_unreg:
	device_unregister(&pseudo_primary);
free_vm:
	vfree(fake_storep);

	return ret;
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

static void pseudo_0_release(struct device * dev)
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
	.probe = sdebug_driver_probe,
	.remove = sdebug_driver_remove,
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

        sdbg_host = kzalloc(sizeof(*sdbg_host),GFP_KERNEL);
        if (NULL == sdbg_host) {
                printk(KERN_ERR "%s: out of memory at line %d\n",
                       __FUNCTION__, __LINE__);
                return -ENOMEM;
        }

        INIT_LIST_HEAD(&sdbg_host->dev_info_list);

	devs_per_host = scsi_debug_num_tgts * scsi_debug_max_luns;
        for (k = 0; k < devs_per_host; k++) {
                sdbg_devinfo = kzalloc(sizeof(*sdbg_devinfo),GFP_KERNEL);
                if (NULL == sdbg_devinfo) {
                        printk(KERN_ERR "%s: out of memory at line %d\n",
                               __FUNCTION__, __LINE__);
                        error = -ENOMEM;
			goto clean;
                }
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
	hpnt->max_lun = SAM2_WLUN_REPORT_LUNS;	/* = scsi_debug_max_luns; */

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
		hpnt->max_lun = SAM2_WLUN_REPORT_LUNS; /* scsi_debug_max_luns; */
	}
	spin_unlock(&sdebug_host_list_lock);
}
