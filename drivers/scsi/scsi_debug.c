/*
 * vvvvvvvvvvvvvvvvvvvvvvv Original vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
 *  Copyright (C) 1992  Eric Youngdale
 *  Simulate a host adapter with 2 disks attached.  Do a lot of checking
 *  to make sure that we are not getting blocks mixed up, and PANIC if
 *  anything out of the ordinary is seen.
 * ^^^^^^^^^^^^^^^^^^^^^^^ Original ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
 *
 * Copyright (C) 2001 - 2016 Douglas Gilbert
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 *  For documentation see http://sg.danny.cz/sg/sdebug26.html
 *
 */


#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/slab.h>
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
#include <linux/crc-t10dif.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/atomic.h>
#include <linux/hrtimer.h>
#include <linux/uuid.h>
#include <linux/t10-pi.h>

#include <net/checksum.h>

#include <asm/unaligned.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsicam.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_dbg.h>

#include "sd.h"
#include "scsi_logging.h"

/* make sure inq_product_rev string corresponds to this version */
#define SDEBUG_VERSION "1.86"
static const char *sdebug_version_date = "20160430";

#define MY_NAME "scsi_debug"

/* Additional Sense Code (ASC) */
#define NO_ADDITIONAL_SENSE 0x0
#define LOGICAL_UNIT_NOT_READY 0x4
#define LOGICAL_UNIT_COMMUNICATION_FAILURE 0x8
#define UNRECOVERED_READ_ERR 0x11
#define PARAMETER_LIST_LENGTH_ERR 0x1a
#define INVALID_OPCODE 0x20
#define LBA_OUT_OF_RANGE 0x21
#define INVALID_FIELD_IN_CDB 0x24
#define INVALID_FIELD_IN_PARAM_LIST 0x26
#define UA_RESET_ASC 0x29
#define UA_CHANGED_ASC 0x2a
#define TARGET_CHANGED_ASC 0x3f
#define LUNS_CHANGED_ASCQ 0x0e
#define INSUFF_RES_ASC 0x55
#define INSUFF_RES_ASCQ 0x3
#define POWER_ON_RESET_ASCQ 0x0
#define BUS_RESET_ASCQ 0x2	/* scsi bus reset occurred */
#define MODE_CHANGED_ASCQ 0x1	/* mode parameters changed */
#define CAPACITY_CHANGED_ASCQ 0x9
#define SAVING_PARAMS_UNSUP 0x39
#define TRANSPORT_PROBLEM 0x4b
#define THRESHOLD_EXCEEDED 0x5d
#define LOW_POWER_COND_ON 0x5e
#define MISCOMPARE_VERIFY_ASC 0x1d
#define MICROCODE_CHANGED_ASCQ 0x1	/* with TARGET_CHANGED_ASC */
#define MICROCODE_CHANGED_WO_RESET_ASCQ 0x16

/* Additional Sense Code Qualifier (ASCQ) */
#define ACK_NAK_TO 0x3

/* Default values for driver parameters */
#define DEF_NUM_HOST   1
#define DEF_NUM_TGTS   1
#define DEF_MAX_LUNS   1
/* With these defaults, this driver will make 1 host with 1 target
 * (id 0) containing 1 logical unit (lun 0). That is 1 device.
 */
#define DEF_ATO 1
#define DEF_JDELAY   1		/* if > 0 unit is a jiffy */
#define DEF_DEV_SIZE_MB   8
#define DEF_DIF 0
#define DEF_DIX 0
#define DEF_D_SENSE   0
#define DEF_EVERY_NTH   0
#define DEF_FAKE_RW	0
#define DEF_GUARD 0
#define DEF_HOST_LOCK 0
#define DEF_LBPU 0
#define DEF_LBPWS 0
#define DEF_LBPWS10 0
#define DEF_LBPRZ 1
#define DEF_LOWEST_ALIGNED 0
#define DEF_NDELAY   0		/* if > 0 unit is a nanosecond */
#define DEF_NO_LUN_0   0
#define DEF_NUM_PARTS   0
#define DEF_OPTS   0
#define DEF_OPT_BLKS 1024
#define DEF_PHYSBLK_EXP 0
#define DEF_PTYPE   TYPE_DISK
#define DEF_REMOVABLE false
#define DEF_SCSI_LEVEL   7    /* INQUIRY, byte2 [6->SPC-4; 7->SPC-5] */
#define DEF_SECTOR_SIZE 512
#define DEF_UNMAP_ALIGNMENT 0
#define DEF_UNMAP_GRANULARITY 1
#define DEF_UNMAP_MAX_BLOCKS 0xFFFFFFFF
#define DEF_UNMAP_MAX_DESC 256
#define DEF_VIRTUAL_GB   0
#define DEF_VPD_USE_HOSTNO 1
#define DEF_WRITESAME_LENGTH 0xFFFF
#define DEF_STRICT 0
#define DEF_STATISTICS false
#define DEF_SUBMIT_QUEUES 1
#define DEF_UUID_CTL 0
#define JDELAY_OVERRIDDEN -9999

#define SDEBUG_LUN_0_VAL 0

/* bit mask values for sdebug_opts */
#define SDEBUG_OPT_NOISE		1
#define SDEBUG_OPT_MEDIUM_ERR		2
#define SDEBUG_OPT_TIMEOUT		4
#define SDEBUG_OPT_RECOVERED_ERR	8
#define SDEBUG_OPT_TRANSPORT_ERR	16
#define SDEBUG_OPT_DIF_ERR		32
#define SDEBUG_OPT_DIX_ERR		64
#define SDEBUG_OPT_MAC_TIMEOUT		128
#define SDEBUG_OPT_SHORT_TRANSFER	0x100
#define SDEBUG_OPT_Q_NOISE		0x200
#define SDEBUG_OPT_ALL_TSF		0x400
#define SDEBUG_OPT_RARE_TSF		0x800
#define SDEBUG_OPT_N_WCE		0x1000
#define SDEBUG_OPT_RESET_NOISE		0x2000
#define SDEBUG_OPT_NO_CDB_NOISE		0x4000
#define SDEBUG_OPT_ALL_NOISE (SDEBUG_OPT_NOISE | SDEBUG_OPT_Q_NOISE | \
			      SDEBUG_OPT_RESET_NOISE)
#define SDEBUG_OPT_ALL_INJECTING (SDEBUG_OPT_RECOVERED_ERR | \
				  SDEBUG_OPT_TRANSPORT_ERR | \
				  SDEBUG_OPT_DIF_ERR | SDEBUG_OPT_DIX_ERR | \
				  SDEBUG_OPT_SHORT_TRANSFER)
/* When "every_nth" > 0 then modulo "every_nth" commands:
 *   - a missing response is simulated if SDEBUG_OPT_TIMEOUT is set
 *   - a RECOVERED_ERROR is simulated on successful read and write
 *     commands if SDEBUG_OPT_RECOVERED_ERR is set.
 *   - a TRANSPORT_ERROR is simulated on successful read and write
 *     commands if SDEBUG_OPT_TRANSPORT_ERR is set.
 *
 * When "every_nth" < 0 then after "- every_nth" commands:
 *   - a missing response is simulated if SDEBUG_OPT_TIMEOUT is set
 *   - a RECOVERED_ERROR is simulated on successful read and write
 *     commands if SDEBUG_OPT_RECOVERED_ERR is set.
 *   - a TRANSPORT_ERROR is simulated on successful read and write
 *     commands if _DEBUG_OPT_TRANSPORT_ERR is set.
 * This will continue on every subsequent command until some other action
 * occurs (e.g. the user * writing a new value (other than -1 or 1) to
 * every_nth via sysfs).
 */

/* As indicated in SAM-5 and SPC-4 Unit Attentions (UAs) are returned in
 * priority order. In the subset implemented here lower numbers have higher
 * priority. The UA numbers should be a sequence starting from 0 with
 * SDEBUG_NUM_UAS being 1 higher than the highest numbered UA. */
#define SDEBUG_UA_POR 0		/* Power on, reset, or bus device reset */
#define SDEBUG_UA_BUS_RESET 1
#define SDEBUG_UA_MODE_CHANGED 2
#define SDEBUG_UA_CAPACITY_CHANGED 3
#define SDEBUG_UA_LUNS_CHANGED 4
#define SDEBUG_UA_MICROCODE_CHANGED 5	/* simulate firmware change */
#define SDEBUG_UA_MICROCODE_CHANGED_WO_RESET 6
#define SDEBUG_NUM_UAS 7

/* when 1==SDEBUG_OPT_MEDIUM_ERR, a medium error is simulated at this
 * sector on read commands: */
#define OPT_MEDIUM_ERR_ADDR   0x1234 /* that's sector 4660 in decimal */
#define OPT_MEDIUM_ERR_NUM    10     /* number of consecutive medium errs */

/* If REPORT LUNS has luns >= 256 it can choose "flat space" (value 1)
 * or "peripheral device" addressing (value 0) */
#define SAM2_LUN_ADDRESS_METHOD 0

/* SDEBUG_CANQUEUE is the maximum number of commands that can be queued
 * (for response) per submit queue at one time. Can be reduced by max_queue
 * option. Command responses are not queued when jdelay=0 and ndelay=0. The
 * per-device DEF_CMD_PER_LUN can be changed via sysfs:
 * /sys/class/scsi_device/<h:c:t:l>/device/queue_depth
 * but cannot exceed SDEBUG_CANQUEUE .
 */
#define SDEBUG_CANQUEUE_WORDS  3	/* a WORD is bits in a long */
#define SDEBUG_CANQUEUE  (SDEBUG_CANQUEUE_WORDS * BITS_PER_LONG)
#define DEF_CMD_PER_LUN  255

#define F_D_IN			1
#define F_D_OUT			2
#define F_D_OUT_MAYBE		4	/* WRITE SAME, NDOB bit */
#define F_D_UNKN		8
#define F_RL_WLUN_OK		0x10
#define F_SKIP_UA		0x20
#define F_DELAY_OVERR		0x40
#define F_SA_LOW		0x80	/* cdb byte 1, bits 4 to 0 */
#define F_SA_HIGH		0x100	/* as used by variable length cdbs */
#define F_INV_OP		0x200
#define F_FAKE_RW		0x400
#define F_M_ACCESS		0x800	/* media access */

#define FF_RESPOND (F_RL_WLUN_OK | F_SKIP_UA | F_DELAY_OVERR)
#define FF_DIRECT_IO (F_M_ACCESS | F_FAKE_RW)
#define FF_SA (F_SA_HIGH | F_SA_LOW)

#define SDEBUG_MAX_PARTS 4

#define SDEBUG_MAX_CMD_LEN 32


struct sdebug_dev_info {
	struct list_head dev_list;
	unsigned int channel;
	unsigned int target;
	u64 lun;
	uuid_be lu_name;
	struct sdebug_host_info *sdbg_host;
	unsigned long uas_bm[1];
	atomic_t num_in_q;
	atomic_t stopped;
	bool used;
};

struct sdebug_host_info {
	struct list_head host_list;
	struct Scsi_Host *shost;
	struct device dev;
	struct list_head dev_info_list;
};

#define to_sdebug_host(d)	\
	container_of(d, struct sdebug_host_info, dev)

struct sdebug_defer {
	struct hrtimer hrt;
	struct execute_work ew;
	int sqa_idx;	/* index of sdebug_queue array */
	int qc_idx;	/* index of sdebug_queued_cmd array within sqa_idx */
	int issuing_cpu;
};

struct sdebug_queued_cmd {
	/* corresponding bit set in in_use_bm[] in owning struct sdebug_queue
	 * instance indicates this slot is in use.
	 */
	struct sdebug_defer *sd_dp;
	struct scsi_cmnd *a_cmnd;
	unsigned int inj_recovered:1;
	unsigned int inj_transport:1;
	unsigned int inj_dif:1;
	unsigned int inj_dix:1;
	unsigned int inj_short:1;
};

struct sdebug_queue {
	struct sdebug_queued_cmd qc_arr[SDEBUG_CANQUEUE];
	unsigned long in_use_bm[SDEBUG_CANQUEUE_WORDS];
	spinlock_t qc_lock;
	atomic_t blocked;	/* to temporarily stop more being queued */
};

static atomic_t sdebug_cmnd_count;   /* number of incoming commands */
static atomic_t sdebug_completions;  /* count of deferred completions */
static atomic_t sdebug_miss_cpus;    /* submission + completion cpus differ */
static atomic_t sdebug_a_tsf;	     /* 'almost task set full' counter */

struct opcode_info_t {
	u8 num_attached;	/* 0 if this is it (i.e. a leaf); use 0xff */
				/* for terminating element */
	u8 opcode;		/* if num_attached > 0, preferred */
	u16 sa;			/* service action */
	u32 flags;		/* OR-ed set of SDEB_F_* */
	int (*pfp)(struct scsi_cmnd *, struct sdebug_dev_info *);
	const struct opcode_info_t *arrp;  /* num_attached elements or NULL */
	u8 len_mask[16];	/* len=len_mask[0], then mask for cdb[1]... */
				/* ignore cdb bytes after position 15 */
};

/* SCSI opcodes (first byte of cdb) of interest mapped onto these indexes */
enum sdeb_opcode_index {
	SDEB_I_INVALID_OPCODE =	0,
	SDEB_I_INQUIRY = 1,
	SDEB_I_REPORT_LUNS = 2,
	SDEB_I_REQUEST_SENSE = 3,
	SDEB_I_TEST_UNIT_READY = 4,
	SDEB_I_MODE_SENSE = 5,		/* 6, 10 */
	SDEB_I_MODE_SELECT = 6,		/* 6, 10 */
	SDEB_I_LOG_SENSE = 7,
	SDEB_I_READ_CAPACITY = 8,	/* 10; 16 is in SA_IN(16) */
	SDEB_I_READ = 9,		/* 6, 10, 12, 16 */
	SDEB_I_WRITE = 10,		/* 6, 10, 12, 16 */
	SDEB_I_START_STOP = 11,
	SDEB_I_SERV_ACT_IN = 12,	/* 12, 16 */
	SDEB_I_SERV_ACT_OUT = 13,	/* 12, 16 */
	SDEB_I_MAINT_IN = 14,
	SDEB_I_MAINT_OUT = 15,
	SDEB_I_VERIFY = 16,		/* 10 only */
	SDEB_I_VARIABLE_LEN = 17,
	SDEB_I_RESERVE = 18,		/* 6, 10 */
	SDEB_I_RELEASE = 19,		/* 6, 10 */
	SDEB_I_ALLOW_REMOVAL = 20,	/* PREVENT ALLOW MEDIUM REMOVAL */
	SDEB_I_REZERO_UNIT = 21,	/* REWIND in SSC */
	SDEB_I_ATA_PT = 22,		/* 12, 16 */
	SDEB_I_SEND_DIAG = 23,
	SDEB_I_UNMAP = 24,
	SDEB_I_XDWRITEREAD = 25,	/* 10 only */
	SDEB_I_WRITE_BUFFER = 26,
	SDEB_I_WRITE_SAME = 27,		/* 10, 16 */
	SDEB_I_SYNC_CACHE = 28,		/* 10 only */
	SDEB_I_COMP_WRITE = 29,
	SDEB_I_LAST_ELEMENT = 30,	/* keep this last */
};


static const unsigned char opcode_ind_arr[256] = {
/* 0x0; 0x0->0x1f: 6 byte cdbs */
	SDEB_I_TEST_UNIT_READY, SDEB_I_REZERO_UNIT, 0, SDEB_I_REQUEST_SENSE,
	    0, 0, 0, 0,
	SDEB_I_READ, 0, SDEB_I_WRITE, 0, 0, 0, 0, 0,
	0, 0, SDEB_I_INQUIRY, 0, 0, SDEB_I_MODE_SELECT, SDEB_I_RESERVE,
	    SDEB_I_RELEASE,
	0, 0, SDEB_I_MODE_SENSE, SDEB_I_START_STOP, 0, SDEB_I_SEND_DIAG,
	    SDEB_I_ALLOW_REMOVAL, 0,
/* 0x20; 0x20->0x3f: 10 byte cdbs */
	0, 0, 0, 0, 0, SDEB_I_READ_CAPACITY, 0, 0,
	SDEB_I_READ, 0, SDEB_I_WRITE, 0, 0, 0, 0, SDEB_I_VERIFY,
	0, 0, 0, 0, 0, SDEB_I_SYNC_CACHE, 0, 0,
	0, 0, 0, SDEB_I_WRITE_BUFFER, 0, 0, 0, 0,
/* 0x40; 0x40->0x5f: 10 byte cdbs */
	0, SDEB_I_WRITE_SAME, SDEB_I_UNMAP, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, SDEB_I_LOG_SENSE, 0, 0,
	0, 0, 0, SDEB_I_XDWRITEREAD, 0, SDEB_I_MODE_SELECT, SDEB_I_RESERVE,
	    SDEB_I_RELEASE,
	0, 0, SDEB_I_MODE_SENSE, 0, 0, 0, 0, 0,
/* 0x60; 0x60->0x7d are reserved, 0x7e is "extended cdb" */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, SDEB_I_VARIABLE_LEN,
/* 0x80; 0x80->0x9f: 16 byte cdbs */
	0, 0, 0, 0, 0, SDEB_I_ATA_PT, 0, 0,
	SDEB_I_READ, SDEB_I_COMP_WRITE, SDEB_I_WRITE, 0, 0, 0, 0, 0,
	0, 0, 0, SDEB_I_WRITE_SAME, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, SDEB_I_SERV_ACT_IN, SDEB_I_SERV_ACT_OUT,
/* 0xa0; 0xa0->0xbf: 12 byte cdbs */
	SDEB_I_REPORT_LUNS, SDEB_I_ATA_PT, 0, SDEB_I_MAINT_IN,
	     SDEB_I_MAINT_OUT, 0, 0, 0,
	SDEB_I_READ, SDEB_I_SERV_ACT_OUT, SDEB_I_WRITE, SDEB_I_SERV_ACT_IN,
	     0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
/* 0xc0; 0xc0->0xff: vendor specific */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static int resp_inquiry(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_report_luns(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_requests(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_mode_sense(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_mode_select(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_log_sense(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_readcap(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_read_dt0(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_write_dt0(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_start_stop(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_readcap16(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_get_lba_status(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_report_tgtpgs(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_unmap(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_rsup_opcodes(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_rsup_tmfs(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_write_same_10(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_write_same_16(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_xdwriteread_10(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_comp_write(struct scsi_cmnd *, struct sdebug_dev_info *);
static int resp_write_buffer(struct scsi_cmnd *, struct sdebug_dev_info *);

static const struct opcode_info_t msense_iarr[1] = {
	{0, 0x1a, 0, F_D_IN, NULL, NULL,
	    {6,  0xe8, 0xff, 0xff, 0xff, 0xc7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
};

static const struct opcode_info_t mselect_iarr[1] = {
	{0, 0x15, 0, F_D_OUT, NULL, NULL,
	    {6,  0xf1, 0, 0, 0xff, 0xc7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
};

static const struct opcode_info_t read_iarr[3] = {
	{0, 0x28, 0, F_D_IN | FF_DIRECT_IO, resp_read_dt0, NULL,/* READ(10) */
	    {10,  0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xff, 0xff, 0xc7, 0, 0,
	     0, 0, 0, 0} },
	{0, 0x8, 0, F_D_IN | FF_DIRECT_IO, resp_read_dt0, NULL, /* READ(6) */
	    {6,  0xff, 0xff, 0xff, 0xff, 0xc7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{0, 0xa8, 0, F_D_IN | FF_DIRECT_IO, resp_read_dt0, NULL,/* READ(12) */
	    {12,  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x9f,
	     0xc7, 0, 0, 0, 0} },
};

static const struct opcode_info_t write_iarr[3] = {
	{0, 0x2a, 0, F_D_OUT | FF_DIRECT_IO, resp_write_dt0, NULL,   /* 10 */
	    {10,  0xfb, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xff, 0xff, 0xc7, 0, 0,
	     0, 0, 0, 0} },
	{0, 0xa, 0, F_D_OUT | FF_DIRECT_IO, resp_write_dt0, NULL,    /* 6 */
	    {6,  0xff, 0xff, 0xff, 0xff, 0xc7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{0, 0xaa, 0, F_D_OUT | FF_DIRECT_IO, resp_write_dt0, NULL,   /* 12 */
	    {12,  0xfb, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x9f,
	     0xc7, 0, 0, 0, 0} },
};

static const struct opcode_info_t sa_in_iarr[1] = {
	{0, 0x9e, 0x12, F_SA_LOW | F_D_IN, resp_get_lba_status, NULL,
	    {16,  0x12, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	     0xff, 0xff, 0xff, 0, 0xc7} },
};

static const struct opcode_info_t vl_iarr[1] = {	/* VARIABLE LENGTH */
	{0, 0x7f, 0xb, F_SA_HIGH | F_D_OUT | FF_DIRECT_IO, resp_write_dt0,
	    NULL, {32,  0xc7, 0, 0, 0, 0, 0x1f, 0x18, 0x0, 0xb, 0xfa,
		   0, 0xff, 0xff, 0xff, 0xff} },	/* WRITE(32) */
};

static const struct opcode_info_t maint_in_iarr[2] = {
	{0, 0xa3, 0xc, F_SA_LOW | F_D_IN, resp_rsup_opcodes, NULL,
	    {12,  0xc, 0x87, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0,
	     0xc7, 0, 0, 0, 0} },
	{0, 0xa3, 0xd, F_SA_LOW | F_D_IN, resp_rsup_tmfs, NULL,
	    {12,  0xd, 0x80, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0, 0xc7, 0, 0,
	     0, 0} },
};

static const struct opcode_info_t write_same_iarr[1] = {
	{0, 0x93, 0, F_D_OUT_MAYBE | FF_DIRECT_IO, resp_write_same_16, NULL,
	    {16,  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	     0xff, 0xff, 0xff, 0x1f, 0xc7} },
};

static const struct opcode_info_t reserve_iarr[1] = {
	{0, 0x16, 0, F_D_OUT, NULL, NULL,	/* RESERVE(6) */
	    {6,  0x1f, 0xff, 0xff, 0xff, 0xc7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
};

static const struct opcode_info_t release_iarr[1] = {
	{0, 0x17, 0, F_D_OUT, NULL, NULL,	/* RELEASE(6) */
	    {6,  0x1f, 0xff, 0, 0, 0xc7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
};


/* This array is accessed via SDEB_I_* values. Make sure all are mapped,
 * plus the terminating elements for logic that scans this table such as
 * REPORT SUPPORTED OPERATION CODES. */
static const struct opcode_info_t opcode_info_arr[SDEB_I_LAST_ELEMENT + 1] = {
/* 0 */
	{0, 0, 0, F_INV_OP | FF_RESPOND, NULL, NULL,
	    {0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{0, 0x12, 0, FF_RESPOND | F_D_IN, resp_inquiry, NULL,
	    {6,  0xe3, 0xff, 0xff, 0xff, 0xc7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{0, 0xa0, 0, FF_RESPOND | F_D_IN, resp_report_luns, NULL,
	    {12,  0xe3, 0xff, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0, 0xc7, 0, 0,
	     0, 0} },
	{0, 0x3, 0, FF_RESPOND | F_D_IN, resp_requests, NULL,
	    {6,  0xe1, 0, 0, 0xff, 0xc7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{0, 0x0, 0, F_M_ACCESS | F_RL_WLUN_OK, NULL, NULL,/* TEST UNIT READY */
	    {6,  0, 0, 0, 0, 0xc7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{1, 0x5a, 0, F_D_IN, resp_mode_sense, msense_iarr,
	    {10,  0xf8, 0xff, 0xff, 0, 0, 0, 0xff, 0xff, 0xc7, 0, 0, 0, 0, 0,
	     0} },
	{1, 0x55, 0, F_D_OUT, resp_mode_select, mselect_iarr,
	    {10,  0xf1, 0, 0, 0, 0, 0, 0xff, 0xff, 0xc7, 0, 0, 0, 0, 0, 0} },
	{0, 0x4d, 0, F_D_IN, resp_log_sense, NULL,
	    {10,  0xe3, 0xff, 0xff, 0, 0xff, 0xff, 0xff, 0xff, 0xc7, 0, 0, 0,
	     0, 0, 0} },
	{0, 0x25, 0, F_D_IN, resp_readcap, NULL,
	    {10,  0xe1, 0xff, 0xff, 0xff, 0xff, 0, 0, 0x1, 0xc7, 0, 0, 0, 0,
	     0, 0} },
	{3, 0x88, 0, F_D_IN | FF_DIRECT_IO, resp_read_dt0, read_iarr,
	    {16,  0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	     0xff, 0xff, 0xff, 0x9f, 0xc7} },		/* READ(16) */
/* 10 */
	{3, 0x8a, 0, F_D_OUT | FF_DIRECT_IO, resp_write_dt0, write_iarr,
	    {16,  0xfa, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	     0xff, 0xff, 0xff, 0x9f, 0xc7} },		/* WRITE(16) */
	{0, 0x1b, 0, 0, resp_start_stop, NULL,		/* START STOP UNIT */
	    {6,  0x1, 0, 0xf, 0xf7, 0xc7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{1, 0x9e, 0x10, F_SA_LOW | F_D_IN, resp_readcap16, sa_in_iarr,
	    {16,  0x10, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	     0xff, 0xff, 0xff, 0x1, 0xc7} },	/* READ CAPACITY(16) */
	{0, 0, 0, F_INV_OP | FF_RESPOND, NULL, NULL, /* SA OUT */
	    {0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{2, 0xa3, 0xa, F_SA_LOW | F_D_IN, resp_report_tgtpgs, maint_in_iarr,
	    {12,  0xea, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0, 0xc7, 0, 0, 0,
	     0} },
	{0, 0, 0, F_INV_OP | FF_RESPOND, NULL, NULL, /* MAINT OUT */
	    {0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{0, 0x2f, 0, F_D_OUT_MAYBE | FF_DIRECT_IO, NULL, NULL, /* VERIFY(10) */
	    {10,  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc7,
	     0, 0, 0, 0, 0, 0} },
	{1, 0x7f, 0x9, F_SA_HIGH | F_D_IN | FF_DIRECT_IO, resp_read_dt0,
	    vl_iarr, {32,  0xc7, 0, 0, 0, 0, 0x1f, 0x18, 0x0, 0x9, 0xfe, 0,
		      0xff, 0xff, 0xff, 0xff} },/* VARIABLE LENGTH, READ(32) */
	{1, 0x56, 0, F_D_OUT, NULL, reserve_iarr, /* RESERVE(10) */
	    {10,  0xff, 0xff, 0xff, 0, 0, 0, 0xff, 0xff, 0xc7, 0, 0, 0, 0, 0,
	     0} },
	{1, 0x57, 0, F_D_OUT, NULL, release_iarr, /* RELEASE(10) */
	    {10,  0x13, 0xff, 0xff, 0, 0, 0, 0xff, 0xff, 0xc7, 0, 0, 0, 0, 0,
	     0} },
/* 20 */
	{0, 0x1e, 0, 0, NULL, NULL, /* ALLOW REMOVAL */
	    {6,  0, 0, 0, 0x3, 0xc7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{0, 0x1, 0, 0, resp_start_stop, NULL, /* REWIND ?? */
	    {6,  0x1, 0, 0, 0, 0xc7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{0, 0, 0, F_INV_OP | FF_RESPOND, NULL, NULL, /* ATA_PT */
	    {0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{0, 0x1d, F_D_OUT, 0, NULL, NULL,	/* SEND DIAGNOSTIC */
	    {6,  0xf7, 0, 0xff, 0xff, 0xc7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
	{0, 0x42, 0, F_D_OUT | FF_DIRECT_IO, resp_unmap, NULL, /* UNMAP */
	    {10,  0x1, 0, 0, 0, 0, 0x1f, 0xff, 0xff, 0xc7, 0, 0, 0, 0, 0, 0} },
	{0, 0x53, 0, F_D_IN | F_D_OUT | FF_DIRECT_IO, resp_xdwriteread_10,
	    NULL, {10,  0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xff, 0xff, 0xc7,
		   0, 0, 0, 0, 0, 0} },
	{0, 0x3b, 0, F_D_OUT_MAYBE, resp_write_buffer, NULL,
	    {10,  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xc7, 0, 0,
	     0, 0, 0, 0} },			/* WRITE_BUFFER */
	{1, 0x41, 0, F_D_OUT_MAYBE | FF_DIRECT_IO, resp_write_same_10,
	    write_same_iarr, {10,  0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xff,
			      0xff, 0xc7, 0, 0, 0, 0, 0, 0} },
	{0, 0x35, 0, F_DELAY_OVERR | FF_DIRECT_IO, NULL, NULL, /* SYNC_CACHE */
	    {10,  0x7, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xff, 0xff, 0xc7, 0, 0,
	     0, 0, 0, 0} },
	{0, 0x89, 0, F_D_OUT | FF_DIRECT_IO, resp_comp_write, NULL,
	    {16,  0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0,
	     0, 0xff, 0x1f, 0xc7} },		/* COMPARE AND WRITE */

/* 30 */
	{0xff, 0, 0, 0, NULL, NULL,		/* terminating element */
	    {0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
};

static int sdebug_add_host = DEF_NUM_HOST;
static int sdebug_ato = DEF_ATO;
static int sdebug_jdelay = DEF_JDELAY;	/* if > 0 then unit is jiffies */
static int sdebug_dev_size_mb = DEF_DEV_SIZE_MB;
static int sdebug_dif = DEF_DIF;
static int sdebug_dix = DEF_DIX;
static int sdebug_dsense = DEF_D_SENSE;
static int sdebug_every_nth = DEF_EVERY_NTH;
static int sdebug_fake_rw = DEF_FAKE_RW;
static unsigned int sdebug_guard = DEF_GUARD;
static int sdebug_lowest_aligned = DEF_LOWEST_ALIGNED;
static int sdebug_max_luns = DEF_MAX_LUNS;
static int sdebug_max_queue = SDEBUG_CANQUEUE;	/* per submit queue */
static atomic_t retired_max_queue;	/* if > 0 then was prior max_queue */
static int sdebug_ndelay = DEF_NDELAY;	/* if > 0 then unit is nanoseconds */
static int sdebug_no_lun_0 = DEF_NO_LUN_0;
static int sdebug_no_uld;
static int sdebug_num_parts = DEF_NUM_PARTS;
static int sdebug_num_tgts = DEF_NUM_TGTS; /* targets per host */
static int sdebug_opt_blks = DEF_OPT_BLKS;
static int sdebug_opts = DEF_OPTS;
static int sdebug_physblk_exp = DEF_PHYSBLK_EXP;
static int sdebug_ptype = DEF_PTYPE; /* SCSI peripheral device type */
static int sdebug_scsi_level = DEF_SCSI_LEVEL;
static int sdebug_sector_size = DEF_SECTOR_SIZE;
static int sdebug_virtual_gb = DEF_VIRTUAL_GB;
static int sdebug_vpd_use_hostno = DEF_VPD_USE_HOSTNO;
static unsigned int sdebug_lbpu = DEF_LBPU;
static unsigned int sdebug_lbpws = DEF_LBPWS;
static unsigned int sdebug_lbpws10 = DEF_LBPWS10;
static unsigned int sdebug_lbprz = DEF_LBPRZ;
static unsigned int sdebug_unmap_alignment = DEF_UNMAP_ALIGNMENT;
static unsigned int sdebug_unmap_granularity = DEF_UNMAP_GRANULARITY;
static unsigned int sdebug_unmap_max_blocks = DEF_UNMAP_MAX_BLOCKS;
static unsigned int sdebug_unmap_max_desc = DEF_UNMAP_MAX_DESC;
static unsigned int sdebug_write_same_length = DEF_WRITESAME_LENGTH;
static int sdebug_uuid_ctl = DEF_UUID_CTL;
static bool sdebug_removable = DEF_REMOVABLE;
static bool sdebug_clustering;
static bool sdebug_host_lock = DEF_HOST_LOCK;
static bool sdebug_strict = DEF_STRICT;
static bool sdebug_any_injecting_opt;
static bool sdebug_verbose;
static bool have_dif_prot;
static bool sdebug_statistics = DEF_STATISTICS;
static bool sdebug_mq_active;

static unsigned int sdebug_store_sectors;
static sector_t sdebug_capacity;	/* in sectors */

/* old BIOS stuff, kernel may get rid of them but some mode sense pages
   may still need them */
static int sdebug_heads;		/* heads per disk */
static int sdebug_cylinders_per;	/* cylinders per surface */
static int sdebug_sectors_per;		/* sectors per cylinder */

static LIST_HEAD(sdebug_host_list);
static DEFINE_SPINLOCK(sdebug_host_list_lock);

static unsigned char *fake_storep;	/* ramdisk storage */
static struct t10_pi_tuple *dif_storep;	/* protection info */
static void *map_storep;		/* provisioning map */

static unsigned long map_size;
static int num_aborts;
static int num_dev_resets;
static int num_target_resets;
static int num_bus_resets;
static int num_host_resets;
static int dix_writes;
static int dix_reads;
static int dif_errors;

static int submit_queues = DEF_SUBMIT_QUEUES;  /* > 1 for multi-queue (mq) */
static struct sdebug_queue *sdebug_q_arr;  /* ptr to array of submit queues */

static DEFINE_RWLOCK(atomic_rw);

static char sdebug_proc_name[] = MY_NAME;
static const char *my_name = MY_NAME;

static struct bus_type pseudo_lld_bus;

static struct device_driver sdebug_driverfs_driver = {
	.name 		= sdebug_proc_name,
	.bus		= &pseudo_lld_bus,
};

static const int check_condition_result =
		(DRIVER_SENSE << 24) | SAM_STAT_CHECK_CONDITION;

static const int illegal_condition_result =
	(DRIVER_SENSE << 24) | (DID_ABORT << 16) | SAM_STAT_CHECK_CONDITION;

static const int device_qfull_result =
	(DID_OK << 16) | (COMMAND_COMPLETE << 8) | SAM_STAT_TASK_SET_FULL;


/* Only do the extra work involved in logical block provisioning if one or
 * more of the lbpu, lbpws or lbpws10 parameters are given and we are doing
 * real reads and writes (i.e. not skipping them for speed).
 */
static inline bool scsi_debug_lbp(void)
{
	return 0 == sdebug_fake_rw &&
		(sdebug_lbpu || sdebug_lbpws || sdebug_lbpws10);
}

static void *fake_store(unsigned long long lba)
{
	lba = do_div(lba, sdebug_store_sectors);

	return fake_storep + lba * sdebug_sector_size;
}

static struct t10_pi_tuple *dif_store(sector_t sector)
{
	sector = sector_div(sector, sdebug_store_sectors);

	return dif_storep + sector;
}

static void sdebug_max_tgts_luns(void)
{
	struct sdebug_host_info *sdbg_host;
	struct Scsi_Host *hpnt;

	spin_lock(&sdebug_host_list_lock);
	list_for_each_entry(sdbg_host, &sdebug_host_list, host_list) {
		hpnt = sdbg_host->shost;
		if ((hpnt->this_id >= 0) &&
		    (sdebug_num_tgts > hpnt->this_id))
			hpnt->max_id = sdebug_num_tgts + 1;
		else
			hpnt->max_id = sdebug_num_tgts;
		/* sdebug_max_luns; */
		hpnt->max_lun = SCSI_W_LUN_REPORT_LUNS + 1;
	}
	spin_unlock(&sdebug_host_list_lock);
}

enum sdeb_cmd_data {SDEB_IN_DATA = 0, SDEB_IN_CDB = 1};

/* Set in_bit to -1 to indicate no bit position of invalid field */
static void mk_sense_invalid_fld(struct scsi_cmnd *scp,
				 enum sdeb_cmd_data c_d,
				 int in_byte, int in_bit)
{
	unsigned char *sbuff;
	u8 sks[4];
	int sl, asc;

	sbuff = scp->sense_buffer;
	if (!sbuff) {
		sdev_printk(KERN_ERR, scp->device,
			    "%s: sense_buffer is NULL\n", __func__);
		return;
	}
	asc = c_d ? INVALID_FIELD_IN_CDB : INVALID_FIELD_IN_PARAM_LIST;
	memset(sbuff, 0, SCSI_SENSE_BUFFERSIZE);
	scsi_build_sense_buffer(sdebug_dsense, sbuff, ILLEGAL_REQUEST, asc, 0);
	memset(sks, 0, sizeof(sks));
	sks[0] = 0x80;
	if (c_d)
		sks[0] |= 0x40;
	if (in_bit >= 0) {
		sks[0] |= 0x8;
		sks[0] |= 0x7 & in_bit;
	}
	put_unaligned_be16(in_byte, sks + 1);
	if (sdebug_dsense) {
		sl = sbuff[7] + 8;
		sbuff[7] = sl;
		sbuff[sl] = 0x2;
		sbuff[sl + 1] = 0x6;
		memcpy(sbuff + sl + 4, sks, 3);
	} else
		memcpy(sbuff + 15, sks, 3);
	if (sdebug_verbose)
		sdev_printk(KERN_INFO, scp->device, "%s:  [sense_key,asc,ascq"
			    "]: [0x5,0x%x,0x0] %c byte=%d, bit=%d\n",
			    my_name, asc, c_d ? 'C' : 'D', in_byte, in_bit);
}

static void mk_sense_buffer(struct scsi_cmnd *scp, int key, int asc, int asq)
{
	unsigned char *sbuff;

	sbuff = scp->sense_buffer;
	if (!sbuff) {
		sdev_printk(KERN_ERR, scp->device,
			    "%s: sense_buffer is NULL\n", __func__);
		return;
	}
	memset(sbuff, 0, SCSI_SENSE_BUFFERSIZE);

	scsi_build_sense_buffer(sdebug_dsense, sbuff, key, asc, asq);

	if (sdebug_verbose)
		sdev_printk(KERN_INFO, scp->device,
			    "%s:  [sense_key,asc,ascq]: [0x%x,0x%x,0x%x]\n",
			    my_name, key, asc, asq);
}

static void mk_sense_invalid_opcode(struct scsi_cmnd *scp)
{
	mk_sense_buffer(scp, ILLEGAL_REQUEST, INVALID_OPCODE, 0);
}

static int scsi_debug_ioctl(struct scsi_device *dev, int cmd, void __user *arg)
{
	if (sdebug_verbose) {
		if (0x1261 == cmd)
			sdev_printk(KERN_INFO, dev,
				    "%s: BLKFLSBUF [0x1261]\n", __func__);
		else if (0x5331 == cmd)
			sdev_printk(KERN_INFO, dev,
				    "%s: CDROM_GET_CAPABILITY [0x5331]\n",
				    __func__);
		else
			sdev_printk(KERN_INFO, dev, "%s: cmd=0x%x\n",
				    __func__, cmd);
	}
	return -EINVAL;
	/* return -ENOTTY; // correct return but upsets fdisk */
}

static void clear_luns_changed_on_target(struct sdebug_dev_info *devip)
{
	struct sdebug_host_info *sdhp;
	struct sdebug_dev_info *dp;

	spin_lock(&sdebug_host_list_lock);
	list_for_each_entry(sdhp, &sdebug_host_list, host_list) {
		list_for_each_entry(dp, &sdhp->dev_info_list, dev_list) {
			if ((devip->sdbg_host == dp->sdbg_host) &&
			    (devip->target == dp->target))
				clear_bit(SDEBUG_UA_LUNS_CHANGED, dp->uas_bm);
		}
	}
	spin_unlock(&sdebug_host_list_lock);
}

static int make_ua(struct scsi_cmnd *scp, struct sdebug_dev_info *devip)
{
	int k;

	k = find_first_bit(devip->uas_bm, SDEBUG_NUM_UAS);
	if (k != SDEBUG_NUM_UAS) {
		const char *cp = NULL;

		switch (k) {
		case SDEBUG_UA_POR:
			mk_sense_buffer(scp, UNIT_ATTENTION, UA_RESET_ASC,
					POWER_ON_RESET_ASCQ);
			if (sdebug_verbose)
				cp = "power on reset";
			break;
		case SDEBUG_UA_BUS_RESET:
			mk_sense_buffer(scp, UNIT_ATTENTION, UA_RESET_ASC,
					BUS_RESET_ASCQ);
			if (sdebug_verbose)
				cp = "bus reset";
			break;
		case SDEBUG_UA_MODE_CHANGED:
			mk_sense_buffer(scp, UNIT_ATTENTION, UA_CHANGED_ASC,
					MODE_CHANGED_ASCQ);
			if (sdebug_verbose)
				cp = "mode parameters changed";
			break;
		case SDEBUG_UA_CAPACITY_CHANGED:
			mk_sense_buffer(scp, UNIT_ATTENTION, UA_CHANGED_ASC,
					CAPACITY_CHANGED_ASCQ);
			if (sdebug_verbose)
				cp = "capacity data changed";
			break;
		case SDEBUG_UA_MICROCODE_CHANGED:
			mk_sense_buffer(scp, UNIT_ATTENTION,
					TARGET_CHANGED_ASC,
					MICROCODE_CHANGED_ASCQ);
			if (sdebug_verbose)
				cp = "microcode has been changed";
			break;
		case SDEBUG_UA_MICROCODE_CHANGED_WO_RESET:
			mk_sense_buffer(scp, UNIT_ATTENTION,
					TARGET_CHANGED_ASC,
					MICROCODE_CHANGED_WO_RESET_ASCQ);
			if (sdebug_verbose)
				cp = "microcode has been changed without reset";
			break;
		case SDEBUG_UA_LUNS_CHANGED:
			/*
			 * SPC-3 behavior is to report a UNIT ATTENTION with
			 * ASC/ASCQ REPORTED LUNS DATA HAS CHANGED on every LUN
			 * on the target, until a REPORT LUNS command is
			 * received.  SPC-4 behavior is to report it only once.
			 * NOTE:  sdebug_scsi_level does not use the same
			 * values as struct scsi_device->scsi_level.
			 */
			if (sdebug_scsi_level >= 6)	/* SPC-4 and above */
				clear_luns_changed_on_target(devip);
			mk_sense_buffer(scp, UNIT_ATTENTION,
					TARGET_CHANGED_ASC,
					LUNS_CHANGED_ASCQ);
			if (sdebug_verbose)
				cp = "reported luns data has changed";
			break;
		default:
			pr_warn("unexpected unit attention code=%d\n", k);
			if (sdebug_verbose)
				cp = "unknown";
			break;
		}
		clear_bit(k, devip->uas_bm);
		if (sdebug_verbose)
			sdev_printk(KERN_INFO, scp->device,
				   "%s reports: Unit attention: %s\n",
				   my_name, cp);
		return check_condition_result;
	}
	return 0;
}

/* Build SCSI "data-in" buffer. Returns 0 if ok else (DID_ERROR << 16). */
static int fill_from_dev_buffer(struct scsi_cmnd *scp, unsigned char *arr,
				int arr_len)
{
	int act_len;
	struct scsi_data_buffer *sdb = scsi_in(scp);

	if (!sdb->length)
		return 0;
	if (!(scsi_bidi_cmnd(scp) || scp->sc_data_direction == DMA_FROM_DEVICE))
		return DID_ERROR << 16;

	act_len = sg_copy_from_buffer(sdb->table.sgl, sdb->table.nents,
				      arr, arr_len);
	sdb->resid = scsi_bufflen(scp) - act_len;

	return 0;
}

/* Partial build of SCSI "data-in" buffer. Returns 0 if ok else
 * (DID_ERROR << 16). Can write to offset in data-in buffer. If multiple
 * calls, not required to write in ascending offset order. Assumes resid
 * set to scsi_bufflen() prior to any calls.
 */
static int p_fill_from_dev_buffer(struct scsi_cmnd *scp, const void *arr,
				  int arr_len, unsigned int off_dst)
{
	int act_len, n;
	struct scsi_data_buffer *sdb = scsi_in(scp);
	off_t skip = off_dst;

	if (sdb->length <= off_dst)
		return 0;
	if (!(scsi_bidi_cmnd(scp) || scp->sc_data_direction == DMA_FROM_DEVICE))
		return DID_ERROR << 16;

	act_len = sg_pcopy_from_buffer(sdb->table.sgl, sdb->table.nents,
				       arr, arr_len, skip);
	pr_debug("%s: off_dst=%u, scsi_bufflen=%u, act_len=%u, resid=%d\n",
		 __func__, off_dst, scsi_bufflen(scp), act_len, sdb->resid);
	n = (int)scsi_bufflen(scp) - ((int)off_dst + act_len);
	sdb->resid = min(sdb->resid, n);
	return 0;
}

/* Fetches from SCSI "data-out" buffer. Returns number of bytes fetched into
 * 'arr' or -1 if error.
 */
static int fetch_to_dev_buffer(struct scsi_cmnd *scp, unsigned char *arr,
			       int arr_len)
{
	if (!scsi_bufflen(scp))
		return 0;
	if (!(scsi_bidi_cmnd(scp) || scp->sc_data_direction == DMA_TO_DEVICE))
		return -1;

	return scsi_sg_copy_to_buffer(scp, arr, arr_len);
}


static const char * inq_vendor_id = "Linux   ";
static const char * inq_product_id = "scsi_debug      ";
static const char *inq_product_rev = "0186";	/* version less '.' */
/* Use some locally assigned NAAs for SAS addresses. */
static const u64 naa3_comp_a = 0x3222222000000000ULL;
static const u64 naa3_comp_b = 0x3333333000000000ULL;
static const u64 naa3_comp_c = 0x3111111000000000ULL;

/* Device identification VPD page. Returns number of bytes placed in arr */
static int inquiry_vpd_83(unsigned char *arr, int port_group_id,
			  int target_dev_id, int dev_id_num,
			  const char *dev_id_str, int dev_id_str_len,
			  const uuid_be *lu_name)
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
		if (sdebug_uuid_ctl) {
			/* Locally assigned UUID */
			arr[num++] = 0x1;  /* binary (not necessarily sas) */
			arr[num++] = 0xa;  /* PIV=0, lu, naa */
			arr[num++] = 0x0;
			arr[num++] = 0x12;
			arr[num++] = 0x10; /* uuid type=1, locally assigned */
			arr[num++] = 0x0;
			memcpy(arr + num, lu_name, 16);
			num += 16;
		} else {
			/* NAA-3, Logical unit identifier (binary) */
			arr[num++] = 0x1;  /* binary (not necessarily sas) */
			arr[num++] = 0x3;  /* PIV=0, lu, naa */
			arr[num++] = 0x0;
			arr[num++] = 0x8;
			put_unaligned_be64(naa3_comp_b + dev_id_num, arr + num);
			num += 8;
		}
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
	/* NAA-3, Target port identifier */
	arr[num++] = 0x61;	/* proto=sas, binary */
	arr[num++] = 0x93;	/* piv=1, target port, naa */
	arr[num++] = 0x0;
	arr[num++] = 0x8;
	put_unaligned_be64(naa3_comp_a + port_a, arr + num);
	num += 8;
	/* NAA-3, Target port group identifier */
	arr[num++] = 0x61;	/* proto=sas, binary */
	arr[num++] = 0x95;	/* piv=1, target port group id */
	arr[num++] = 0x0;
	arr[num++] = 0x4;
	arr[num++] = 0;
	arr[num++] = 0;
	put_unaligned_be16(port_group_id, arr + num);
	num += 2;
	/* NAA-3, Target device identifier */
	arr[num++] = 0x61;	/* proto=sas, binary */
	arr[num++] = 0xa3;	/* piv=1, target device, naa */
	arr[num++] = 0x0;
	arr[num++] = 0x8;
	put_unaligned_be64(naa3_comp_a + target_dev_id, arr + num);
	num += 8;
	/* SCSI name string: Target device identifier */
	arr[num++] = 0x63;	/* proto=sas, UTF-8 */
	arr[num++] = 0xa8;	/* piv=1, target device, SCSI name string */
	arr[num++] = 0x0;
	arr[num++] = 24;
	memcpy(arr + num, "naa.32222220", 12);
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

/*  Software interface identification VPD page */
static int inquiry_vpd_84(unsigned char *arr)
{
	memcpy(arr, vpd84_data, sizeof(vpd84_data));
	return sizeof(vpd84_data);
}

/* Management network addresses VPD page */
static int inquiry_vpd_85(unsigned char *arr)
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
static int inquiry_vpd_88(unsigned char *arr, int target_dev_id)
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
	put_unaligned_be64(naa3_comp_a + port_a, arr + num);
	num += 8;
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
	put_unaligned_be64(naa3_comp_a + port_b, arr + num);
	num += 8;

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

/* ATA Information VPD page */
static int inquiry_vpd_89(unsigned char *arr)
{
	memcpy(arr, vpd89_data, sizeof(vpd89_data));
	return sizeof(vpd89_data);
}


static unsigned char vpdb0_data[] = {
	/* from 4th byte */ 0,0,0,4, 0,0,0x4,0, 0,0,0,64,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

/* Block limits VPD page (SBC-3) */
static int inquiry_vpd_b0(unsigned char *arr)
{
	unsigned int gran;

	memcpy(arr, vpdb0_data, sizeof(vpdb0_data));

	/* Optimal transfer length granularity */
	gran = 1 << sdebug_physblk_exp;
	put_unaligned_be16(gran, arr + 2);

	/* Maximum Transfer Length */
	if (sdebug_store_sectors > 0x400)
		put_unaligned_be32(sdebug_store_sectors, arr + 4);

	/* Optimal Transfer Length */
	put_unaligned_be32(sdebug_opt_blks, &arr[8]);

	if (sdebug_lbpu) {
		/* Maximum Unmap LBA Count */
		put_unaligned_be32(sdebug_unmap_max_blocks, &arr[16]);

		/* Maximum Unmap Block Descriptor Count */
		put_unaligned_be32(sdebug_unmap_max_desc, &arr[20]);
	}

	/* Unmap Granularity Alignment */
	if (sdebug_unmap_alignment) {
		put_unaligned_be32(sdebug_unmap_alignment, &arr[28]);
		arr[28] |= 0x80; /* UGAVALID */
	}

	/* Optimal Unmap Granularity */
	put_unaligned_be32(sdebug_unmap_granularity, &arr[24]);

	/* Maximum WRITE SAME Length */
	put_unaligned_be64(sdebug_write_same_length, &arr[32]);

	return 0x3c; /* Mandatory page length for Logical Block Provisioning */

	return sizeof(vpdb0_data);
}

/* Block device characteristics VPD page (SBC-3) */
static int inquiry_vpd_b1(unsigned char *arr)
{
	memset(arr, 0, 0x3c);
	arr[0] = 0;
	arr[1] = 1;	/* non rotating medium (e.g. solid state) */
	arr[2] = 0;
	arr[3] = 5;	/* less than 1.8" */

	return 0x3c;
}

/* Logical block provisioning VPD page (SBC-4) */
static int inquiry_vpd_b2(unsigned char *arr)
{
	memset(arr, 0, 0x4);
	arr[0] = 0;			/* threshold exponent */
	if (sdebug_lbpu)
		arr[1] = 1 << 7;
	if (sdebug_lbpws)
		arr[1] |= 1 << 6;
	if (sdebug_lbpws10)
		arr[1] |= 1 << 5;
	if (sdebug_lbprz && scsi_debug_lbp())
		arr[1] |= (sdebug_lbprz & 0x7) << 2;  /* sbc4r07 and later */
	/* anc_sup=0; dp=0 (no provisioning group descriptor) */
	/* minimum_percentage=0; provisioning_type=0 (unknown) */
	/* threshold_percentage=0 */
	return 0x4;
}

#define SDEBUG_LONG_INQ_SZ 96
#define SDEBUG_MAX_INQ_ARR_SZ 584

static int resp_inquiry(struct scsi_cmnd *scp, struct sdebug_dev_info *devip)
{
	unsigned char pq_pdt;
	unsigned char * arr;
	unsigned char *cmd = scp->cmnd;
	int alloc_len, n, ret;
	bool have_wlun, is_disk;

	alloc_len = get_unaligned_be16(cmd + 3);
	arr = kzalloc(SDEBUG_MAX_INQ_ARR_SZ, GFP_ATOMIC);
	if (! arr)
		return DID_REQUEUE << 16;
	is_disk = (sdebug_ptype == TYPE_DISK);
	have_wlun = scsi_is_wlun(scp->device->lun);
	if (have_wlun)
		pq_pdt = TYPE_WLUN;	/* present, wlun */
	else if (sdebug_no_lun_0 && (devip->lun == SDEBUG_LUN_0_VAL))
		pq_pdt = 0x7f;	/* not present, PQ=3, PDT=0x1f */
	else
		pq_pdt = (sdebug_ptype & 0x1f);
	arr[0] = pq_pdt;
	if (0x2 & cmd[1]) {  /* CMDDT bit set */
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, 1, 1);
		kfree(arr);
		return check_condition_result;
	} else if (0x1 & cmd[1]) {  /* EVPD bit set */
		int lu_id_num, port_group_id, target_dev_id, len;
		char lu_id_str[6];
		int host_no = devip->sdbg_host->shost->host_no;
		
		port_group_id = (((host_no + 1) & 0x7f) << 8) +
		    (devip->channel & 0x7f);
		if (sdebug_vpd_use_hostno == 0)
			host_no = 0;
		lu_id_num = have_wlun ? -1 : (((host_no + 1) * 2000) +
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
			if (is_disk) {	  /* SBC only */
				arr[n++] = 0x89;  /* ATA information */
				arr[n++] = 0xb0;  /* Block limits */
				arr[n++] = 0xb1;  /* Block characteristics */
				arr[n++] = 0xb2;  /* Logical Block Prov */
			}
			arr[3] = n - 4;	  /* number of supported VPD pages */
		} else if (0x80 == cmd[2]) { /* unit serial number */
			arr[1] = cmd[2];	/*sanity */
			arr[3] = len;
			memcpy(&arr[4], lu_id_str, len);
		} else if (0x83 == cmd[2]) { /* device identification */
			arr[1] = cmd[2];	/*sanity */
			arr[3] = inquiry_vpd_83(&arr[4], port_group_id,
						target_dev_id, lu_id_num,
						lu_id_str, len,
						&devip->lu_name);
		} else if (0x84 == cmd[2]) { /* Software interface ident. */
			arr[1] = cmd[2];	/*sanity */
			arr[3] = inquiry_vpd_84(&arr[4]);
		} else if (0x85 == cmd[2]) { /* Management network addresses */
			arr[1] = cmd[2];	/*sanity */
			arr[3] = inquiry_vpd_85(&arr[4]);
		} else if (0x86 == cmd[2]) { /* extended inquiry */
			arr[1] = cmd[2];	/*sanity */
			arr[3] = 0x3c;	/* number of following entries */
			if (sdebug_dif == T10_PI_TYPE3_PROTECTION)
				arr[4] = 0x4;	/* SPT: GRD_CHK:1 */
			else if (have_dif_prot)
				arr[4] = 0x5;   /* SPT: GRD_CHK:1, REF_CHK:1 */
			else
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
			arr[3] = inquiry_vpd_88(&arr[4], target_dev_id);
		} else if (is_disk && 0x89 == cmd[2]) { /* ATA information */
			arr[1] = cmd[2];        /*sanity */
			n = inquiry_vpd_89(&arr[4]);
			put_unaligned_be16(n, arr + 2);
		} else if (is_disk && 0xb0 == cmd[2]) { /* Block limits */
			arr[1] = cmd[2];        /*sanity */
			arr[3] = inquiry_vpd_b0(&arr[4]);
		} else if (is_disk && 0xb1 == cmd[2]) { /* Block char. */
			arr[1] = cmd[2];        /*sanity */
			arr[3] = inquiry_vpd_b1(&arr[4]);
		} else if (is_disk && 0xb2 == cmd[2]) { /* LB Prov. */
			arr[1] = cmd[2];        /*sanity */
			arr[3] = inquiry_vpd_b2(&arr[4]);
		} else {
			mk_sense_invalid_fld(scp, SDEB_IN_CDB, 2, -1);
			kfree(arr);
			return check_condition_result;
		}
		len = min(get_unaligned_be16(arr + 2) + 4, alloc_len);
		ret = fill_from_dev_buffer(scp, arr,
			    min(len, SDEBUG_MAX_INQ_ARR_SZ));
		kfree(arr);
		return ret;
	}
	/* drops through here for a standard inquiry */
	arr[1] = sdebug_removable ? 0x80 : 0;	/* Removable disk */
	arr[2] = sdebug_scsi_level;
	arr[3] = 2;    /* response_data_format==2 */
	arr[4] = SDEBUG_LONG_INQ_SZ - 5;
	arr[5] = (int)have_dif_prot;	/* PROTECT bit */
	if (sdebug_vpd_use_hostno == 0)
		arr[5] = 0x10; /* claim: implicit TGPS */
	arr[6] = 0x10; /* claim: MultiP */
	/* arr[6] |= 0x40; ... claim: EncServ (enclosure services) */
	arr[7] = 0xa; /* claim: LINKED + CMDQUE */
	memcpy(&arr[8], inq_vendor_id, 8);
	memcpy(&arr[16], inq_product_id, 16);
	memcpy(&arr[32], inq_product_rev, 4);
	/* version descriptors (2 bytes each) follow */
	put_unaligned_be16(0xc0, arr + 58);   /* SAM-6 no version claimed */
	put_unaligned_be16(0x5c0, arr + 60);  /* SPC-5 no version claimed */
	n = 62;
	if (is_disk) {		/* SBC-4 no version claimed */
		put_unaligned_be16(0x600, arr + n);
		n += 2;
	} else if (sdebug_ptype == TYPE_TAPE) {	/* SSC-4 rev 3 */
		put_unaligned_be16(0x525, arr + n);
		n += 2;
	}
	put_unaligned_be16(0x2100, arr + n);	/* SPL-4 no version claimed */
	ret = fill_from_dev_buffer(scp, arr,
			    min(alloc_len, SDEBUG_LONG_INQ_SZ));
	kfree(arr);
	return ret;
}

static unsigned char iec_m_pg[] = {0x1c, 0xa, 0x08, 0, 0, 0, 0, 0,
				   0, 0, 0x0, 0x0};

static int resp_requests(struct scsi_cmnd * scp,
			 struct sdebug_dev_info * devip)
{
	unsigned char * sbuff;
	unsigned char *cmd = scp->cmnd;
	unsigned char arr[SCSI_SENSE_BUFFERSIZE];
	bool dsense;
	int len = 18;

	memset(arr, 0, sizeof(arr));
	dsense = !!(cmd[1] & 1);
	sbuff = scp->sense_buffer;
	if ((iec_m_pg[2] & 0x4) && (6 == (iec_m_pg[3] & 0xf))) {
		if (dsense) {
			arr[0] = 0x72;
			arr[1] = 0x0;		/* NO_SENSE in sense_key */
			arr[2] = THRESHOLD_EXCEEDED;
			arr[3] = 0xff;		/* TEST set and MRIE==6 */
			len = 8;
		} else {
			arr[0] = 0x70;
			arr[2] = 0x0;		/* NO_SENSE in sense_key */
			arr[7] = 0xa;   	/* 18 byte sense buffer */
			arr[12] = THRESHOLD_EXCEEDED;
			arr[13] = 0xff;		/* TEST set and MRIE==6 */
		}
	} else {
		memcpy(arr, sbuff, SCSI_SENSE_BUFFERSIZE);
		if (arr[0] >= 0x70 && dsense == sdebug_dsense)
			;	/* have sense and formats match */
		else if (arr[0] <= 0x70) {
			if (dsense) {
				memset(arr, 0, 8);
				arr[0] = 0x72;
				len = 8;
			} else {
				memset(arr, 0, 18);
				arr[0] = 0x70;
				arr[7] = 0xa;
			}
		} else if (dsense) {
			memset(arr, 0, 8);
			arr[0] = 0x72;
			arr[1] = sbuff[2];     /* sense key */
			arr[2] = sbuff[12];    /* asc */
			arr[3] = sbuff[13];    /* ascq */
			len = 8;
		} else {
			memset(arr, 0, 18);
			arr[0] = 0x70;
			arr[2] = sbuff[1];
			arr[7] = 0xa;
			arr[12] = sbuff[1];
			arr[13] = sbuff[3];
		}

	}
	mk_sense_buffer(scp, 0, NO_ADDITIONAL_SENSE, 0);
	return fill_from_dev_buffer(scp, arr, len);
}

static int resp_start_stop(struct scsi_cmnd * scp,
			   struct sdebug_dev_info * devip)
{
	unsigned char *cmd = scp->cmnd;
	int power_cond, stop;

	power_cond = (cmd[4] & 0xf0) >> 4;
	if (power_cond) {
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, 4, 7);
		return check_condition_result;
	}
	stop = !(cmd[4] & 1);
	atomic_xchg(&devip->stopped, stop);
	return 0;
}

static sector_t get_sdebug_capacity(void)
{
	static const unsigned int gibibyte = 1073741824;

	if (sdebug_virtual_gb > 0)
		return (sector_t)sdebug_virtual_gb *
			(gibibyte / sdebug_sector_size);
	else
		return sdebug_store_sectors;
}

#define SDEBUG_READCAP_ARR_SZ 8
static int resp_readcap(struct scsi_cmnd * scp,
			struct sdebug_dev_info * devip)
{
	unsigned char arr[SDEBUG_READCAP_ARR_SZ];
	unsigned int capac;

	/* following just in case virtual_gb changed */
	sdebug_capacity = get_sdebug_capacity();
	memset(arr, 0, SDEBUG_READCAP_ARR_SZ);
	if (sdebug_capacity < 0xffffffff) {
		capac = (unsigned int)sdebug_capacity - 1;
		put_unaligned_be32(capac, arr + 0);
	} else
		put_unaligned_be32(0xffffffff, arr + 0);
	put_unaligned_be16(sdebug_sector_size, arr + 6);
	return fill_from_dev_buffer(scp, arr, SDEBUG_READCAP_ARR_SZ);
}

#define SDEBUG_READCAP16_ARR_SZ 32
static int resp_readcap16(struct scsi_cmnd * scp,
			  struct sdebug_dev_info * devip)
{
	unsigned char *cmd = scp->cmnd;
	unsigned char arr[SDEBUG_READCAP16_ARR_SZ];
	int alloc_len;

	alloc_len = get_unaligned_be32(cmd + 10);
	/* following just in case virtual_gb changed */
	sdebug_capacity = get_sdebug_capacity();
	memset(arr, 0, SDEBUG_READCAP16_ARR_SZ);
	put_unaligned_be64((u64)(sdebug_capacity - 1), arr + 0);
	put_unaligned_be32(sdebug_sector_size, arr + 8);
	arr[13] = sdebug_physblk_exp & 0xf;
	arr[14] = (sdebug_lowest_aligned >> 8) & 0x3f;

	if (scsi_debug_lbp()) {
		arr[14] |= 0x80; /* LBPME */
		/* from sbc4r07, this LBPRZ field is 1 bit, but the LBPRZ in
		 * the LB Provisioning VPD page is 3 bits. Note that lbprz=2
		 * in the wider field maps to 0 in this field.
		 */
		if (sdebug_lbprz & 1)	/* precisely what the draft requires */
			arr[14] |= 0x40;
	}

	arr[15] = sdebug_lowest_aligned & 0xff;

	if (have_dif_prot) {
		arr[12] = (sdebug_dif - 1) << 1; /* P_TYPE */
		arr[12] |= 1; /* PROT_EN */
	}

	return fill_from_dev_buffer(scp, arr,
				    min(alloc_len, SDEBUG_READCAP16_ARR_SZ));
}

#define SDEBUG_MAX_TGTPGS_ARR_SZ 1412

static int resp_report_tgtpgs(struct scsi_cmnd * scp,
			      struct sdebug_dev_info * devip)
{
	unsigned char *cmd = scp->cmnd;
	unsigned char * arr;
	int host_no = devip->sdbg_host->shost->host_no;
	int n, ret, alen, rlen;
	int port_group_a, port_group_b, port_a, port_b;

	alen = get_unaligned_be32(cmd + 6);
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
	if (sdebug_vpd_use_hostno == 0) {
		arr[n++] = host_no % 3; /* Asymm access state */
		arr[n++] = 0x0F; /* claim: all states are supported */
	} else {
		arr[n++] = 0x0; /* Active/Optimized path */
		arr[n++] = 0x01; /* only support active/optimized paths */
	}
	put_unaligned_be16(port_group_a, arr + n);
	n += 2;
	arr[n++] = 0;    /* Reserved */
	arr[n++] = 0;    /* Status code */
	arr[n++] = 0;    /* Vendor unique */
	arr[n++] = 0x1;  /* One port per group */
	arr[n++] = 0;    /* Reserved */
	arr[n++] = 0;    /* Reserved */
	put_unaligned_be16(port_a, arr + n);
	n += 2;
	arr[n++] = 3;    /* Port unavailable */
	arr[n++] = 0x08; /* claim: only unavailalbe paths are supported */
	put_unaligned_be16(port_group_b, arr + n);
	n += 2;
	arr[n++] = 0;    /* Reserved */
	arr[n++] = 0;    /* Status code */
	arr[n++] = 0;    /* Vendor unique */
	arr[n++] = 0x1;  /* One port per group */
	arr[n++] = 0;    /* Reserved */
	arr[n++] = 0;    /* Reserved */
	put_unaligned_be16(port_b, arr + n);
	n += 2;

	rlen = n - 4;
	put_unaligned_be32(rlen, arr + 0);

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

static int resp_rsup_opcodes(struct scsi_cmnd *scp,
			     struct sdebug_dev_info *devip)
{
	bool rctd;
	u8 reporting_opts, req_opcode, sdeb_i, supp;
	u16 req_sa, u;
	u32 alloc_len, a_len;
	int k, offset, len, errsts, count, bump, na;
	const struct opcode_info_t *oip;
	const struct opcode_info_t *r_oip;
	u8 *arr;
	u8 *cmd = scp->cmnd;

	rctd = !!(cmd[2] & 0x80);
	reporting_opts = cmd[2] & 0x7;
	req_opcode = cmd[3];
	req_sa = get_unaligned_be16(cmd + 4);
	alloc_len = get_unaligned_be32(cmd + 6);
	if (alloc_len < 4 || alloc_len > 0xffff) {
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, 6, -1);
		return check_condition_result;
	}
	if (alloc_len > 8192)
		a_len = 8192;
	else
		a_len = alloc_len;
	arr = kzalloc((a_len < 256) ? 320 : a_len + 64, GFP_ATOMIC);
	if (NULL == arr) {
		mk_sense_buffer(scp, ILLEGAL_REQUEST, INSUFF_RES_ASC,
				INSUFF_RES_ASCQ);
		return check_condition_result;
	}
	switch (reporting_opts) {
	case 0:	/* all commands */
		/* count number of commands */
		for (count = 0, oip = opcode_info_arr;
		     oip->num_attached != 0xff; ++oip) {
			if (F_INV_OP & oip->flags)
				continue;
			count += (oip->num_attached + 1);
		}
		bump = rctd ? 20 : 8;
		put_unaligned_be32(count * bump, arr);
		for (offset = 4, oip = opcode_info_arr;
		     oip->num_attached != 0xff && offset < a_len; ++oip) {
			if (F_INV_OP & oip->flags)
				continue;
			na = oip->num_attached;
			arr[offset] = oip->opcode;
			put_unaligned_be16(oip->sa, arr + offset + 2);
			if (rctd)
				arr[offset + 5] |= 0x2;
			if (FF_SA & oip->flags)
				arr[offset + 5] |= 0x1;
			put_unaligned_be16(oip->len_mask[0], arr + offset + 6);
			if (rctd)
				put_unaligned_be16(0xa, arr + offset + 8);
			r_oip = oip;
			for (k = 0, oip = oip->arrp; k < na; ++k, ++oip) {
				if (F_INV_OP & oip->flags)
					continue;
				offset += bump;
				arr[offset] = oip->opcode;
				put_unaligned_be16(oip->sa, arr + offset + 2);
				if (rctd)
					arr[offset + 5] |= 0x2;
				if (FF_SA & oip->flags)
					arr[offset + 5] |= 0x1;
				put_unaligned_be16(oip->len_mask[0],
						   arr + offset + 6);
				if (rctd)
					put_unaligned_be16(0xa,
							   arr + offset + 8);
			}
			oip = r_oip;
			offset += bump;
		}
		break;
	case 1:	/* one command: opcode only */
	case 2:	/* one command: opcode plus service action */
	case 3:	/* one command: if sa==0 then opcode only else opcode+sa */
		sdeb_i = opcode_ind_arr[req_opcode];
		oip = &opcode_info_arr[sdeb_i];
		if (F_INV_OP & oip->flags) {
			supp = 1;
			offset = 4;
		} else {
			if (1 == reporting_opts) {
				if (FF_SA & oip->flags) {
					mk_sense_invalid_fld(scp, SDEB_IN_CDB,
							     2, 2);
					kfree(arr);
					return check_condition_result;
				}
				req_sa = 0;
			} else if (2 == reporting_opts &&
				   0 == (FF_SA & oip->flags)) {
				mk_sense_invalid_fld(scp, SDEB_IN_CDB, 4, -1);
				kfree(arr);	/* point at requested sa */
				return check_condition_result;
			}
			if (0 == (FF_SA & oip->flags) &&
			    req_opcode == oip->opcode)
				supp = 3;
			else if (0 == (FF_SA & oip->flags)) {
				na = oip->num_attached;
				for (k = 0, oip = oip->arrp; k < na;
				     ++k, ++oip) {
					if (req_opcode == oip->opcode)
						break;
				}
				supp = (k >= na) ? 1 : 3;
			} else if (req_sa != oip->sa) {
				na = oip->num_attached;
				for (k = 0, oip = oip->arrp; k < na;
				     ++k, ++oip) {
					if (req_sa == oip->sa)
						break;
				}
				supp = (k >= na) ? 1 : 3;
			} else
				supp = 3;
			if (3 == supp) {
				u = oip->len_mask[0];
				put_unaligned_be16(u, arr + 2);
				arr[4] = oip->opcode;
				for (k = 1; k < u; ++k)
					arr[4 + k] = (k < 16) ?
						 oip->len_mask[k] : 0xff;
				offset = 4 + u;
			} else
				offset = 4;
		}
		arr[1] = (rctd ? 0x80 : 0) | supp;
		if (rctd) {
			put_unaligned_be16(0xa, arr + offset);
			offset += 12;
		}
		break;
	default:
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, 2, 2);
		kfree(arr);
		return check_condition_result;
	}
	offset = (offset < a_len) ? offset : a_len;
	len = (offset < alloc_len) ? offset : alloc_len;
	errsts = fill_from_dev_buffer(scp, arr, len);
	kfree(arr);
	return errsts;
}

static int resp_rsup_tmfs(struct scsi_cmnd *scp,
			  struct sdebug_dev_info *devip)
{
	bool repd;
	u32 alloc_len, len;
	u8 arr[16];
	u8 *cmd = scp->cmnd;

	memset(arr, 0, sizeof(arr));
	repd = !!(cmd[2] & 0x80);
	alloc_len = get_unaligned_be32(cmd + 6);
	if (alloc_len < 4) {
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, 6, -1);
		return check_condition_result;
	}
	arr[0] = 0xc8;		/* ATS | ATSS | LURS */
	arr[1] = 0x1;		/* ITNRS */
	if (repd) {
		arr[3] = 0xc;
		len = 16;
	} else
		len = 4;

	len = (len < alloc_len) ? len : alloc_len;
	return fill_from_dev_buffer(scp, arr, len);
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
	put_unaligned_be16(sdebug_sectors_per, p + 10);
	put_unaligned_be16(sdebug_sector_size, p + 12);
	if (sdebug_removable)
		p[20] |= 0x20; /* should agree with INQUIRY */
	if (1 == pcontrol)
		memset(p + 2, 0, sizeof(format_pg) - 2);
	return sizeof(format_pg);
}

static unsigned char caching_pg[] = {0x8, 18, 0x14, 0, 0xff, 0xff, 0, 0,
				     0xff, 0xff, 0xff, 0xff, 0x80, 0x14, 0, 0,
				     0, 0, 0, 0};

static int resp_caching_pg(unsigned char * p, int pcontrol, int target)
{ 	/* Caching page for mode_sense */
	unsigned char ch_caching_pg[] = {/* 0x8, 18, */ 0x4, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	unsigned char d_caching_pg[] = {0x8, 18, 0x14, 0, 0xff, 0xff, 0, 0,
		0xff, 0xff, 0xff, 0xff, 0x80, 0x14, 0, 0,     0, 0, 0, 0};

	if (SDEBUG_OPT_N_WCE & sdebug_opts)
		caching_pg[2] &= ~0x4;	/* set WCE=0 (default WCE=1) */
	memcpy(p, caching_pg, sizeof(caching_pg));
	if (1 == pcontrol)
		memcpy(p + 2, ch_caching_pg, sizeof(ch_caching_pg));
	else if (2 == pcontrol)
		memcpy(p, d_caching_pg, sizeof(d_caching_pg));
	return sizeof(caching_pg);
}

static unsigned char ctrl_m_pg[] = {0xa, 10, 2, 0, 0, 0, 0, 0,
				    0, 0, 0x2, 0x4b};

static int resp_ctrl_m_pg(unsigned char * p, int pcontrol, int target)
{ 	/* Control mode page for mode_sense */
	unsigned char ch_ctrl_m_pg[] = {/* 0xa, 10, */ 0x6, 0, 0, 0, 0, 0,
				        0, 0, 0, 0};
	unsigned char d_ctrl_m_pg[] = {0xa, 10, 2, 0, 0, 0, 0, 0,
				     0, 0, 0x2, 0x4b};

	if (sdebug_dsense)
		ctrl_m_pg[2] |= 0x4;
	else
		ctrl_m_pg[2] &= ~0x4;

	if (sdebug_ato)
		ctrl_m_pg[5] |= 0x80; /* ATO=1 */

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
		    0, 0, 0, 0, 0, 0, 0, 0,	/* insert SAS addr */
		    0, 0, 0, 0, 0, 0, 0, 0,	/* insert SAS addr */
		    0x2, 0, 0, 0, 0, 0, 0, 0,
		    0x88, 0x99, 0, 0, 0, 0, 0, 0,
		    0, 0, 0, 0, 0, 0, 0, 0,
		    0, 1, 0, 0, 0x10, 0x9, 0x8, 0x0,
		    0, 0, 0, 0, 0, 0, 0, 0,	/* insert SAS addr */
		    0, 0, 0, 0, 0, 0, 0, 0,	/* insert SAS addr */
		    0x3, 0, 0, 0, 0, 0, 0, 0,
		    0x88, 0x99, 0, 0, 0, 0, 0, 0,
		    0, 0, 0, 0, 0, 0, 0, 0,
		};
	int port_a, port_b;

	put_unaligned_be64(naa3_comp_a, sas_pcd_m_pg + 16);
	put_unaligned_be64(naa3_comp_c + 1, sas_pcd_m_pg + 24);
	put_unaligned_be64(naa3_comp_a, sas_pcd_m_pg + 64);
	put_unaligned_be64(naa3_comp_c + 1, sas_pcd_m_pg + 72);
	port_a = target_dev_id + 1;
	port_b = port_a + 1;
	memcpy(p, sas_pcd_m_pg, sizeof(sas_pcd_m_pg));
	put_unaligned_be32(port_a, p + 20);
	put_unaligned_be32(port_b, p + 48 + 20);
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

static int resp_mode_sense(struct scsi_cmnd *scp,
			   struct sdebug_dev_info *devip)
{
	int pcontrol, pcode, subpcode, bd_len;
	unsigned char dev_spec;
	int alloc_len, offset, len, target_dev_id;
	int target = scp->device->id;
	unsigned char * ap;
	unsigned char arr[SDEBUG_MAX_MSENSE_SZ];
	unsigned char *cmd = scp->cmnd;
	bool dbd, llbaa, msense_6, is_disk, bad_pcode;

	dbd = !!(cmd[1] & 0x8);		/* disable block descriptors */
	pcontrol = (cmd[2] & 0xc0) >> 6;
	pcode = cmd[2] & 0x3f;
	subpcode = cmd[3];
	msense_6 = (MODE_SENSE == cmd[0]);
	llbaa = msense_6 ? false : !!(cmd[1] & 0x10);
	is_disk = (sdebug_ptype == TYPE_DISK);
	if (is_disk && !dbd)
		bd_len = llbaa ? 16 : 8;
	else
		bd_len = 0;
	alloc_len = msense_6 ? cmd[4] : get_unaligned_be16(cmd + 7);
	memset(arr, 0, SDEBUG_MAX_MSENSE_SZ);
	if (0x3 == pcontrol) {  /* Saving values not supported */
		mk_sense_buffer(scp, ILLEGAL_REQUEST, SAVING_PARAMS_UNSUP, 0);
		return check_condition_result;
	}
	target_dev_id = ((devip->sdbg_host->shost->host_no + 1) * 2000) +
			(devip->target * 1000) - 3;
	/* for disks set DPOFUA bit and clear write protect (WP) bit */
	if (is_disk)
		dev_spec = 0x10;	/* =0x90 if WP=1 implies read-only */
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
	if ((bd_len > 0) && (!sdebug_capacity))
		sdebug_capacity = get_sdebug_capacity();

	if (8 == bd_len) {
		if (sdebug_capacity > 0xfffffffe)
			put_unaligned_be32(0xffffffff, ap + 0);
		else
			put_unaligned_be32(sdebug_capacity, ap + 0);
		put_unaligned_be16(sdebug_sector_size, ap + 6);
		offset += bd_len;
		ap = arr + offset;
	} else if (16 == bd_len) {
		put_unaligned_be64((u64)sdebug_capacity, ap + 0);
		put_unaligned_be32(sdebug_sector_size, ap + 12);
		offset += bd_len;
		ap = arr + offset;
	}

	if ((subpcode > 0x0) && (subpcode < 0xff) && (0x19 != pcode)) {
		/* TODO: Control Extension page */
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, 3, -1);
		return check_condition_result;
	}
	bad_pcode = false;

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
		if (is_disk) {
			len = resp_format_pg(ap, pcontrol, target);
			offset += len;
		} else
			bad_pcode = true;
                break;
	case 0x8:	/* Caching page, direct access */
		if (is_disk) {
			len = resp_caching_pg(ap, pcontrol, target);
			offset += len;
		} else
			bad_pcode = true;
		break;
	case 0xa:	/* Control Mode page, all devices */
		len = resp_ctrl_m_pg(ap, pcontrol, target);
		offset += len;
		break;
	case 0x19:	/* if spc==1 then sas phy, control+discover */
		if ((subpcode > 0x2) && (subpcode < 0xff)) {
			mk_sense_invalid_fld(scp, SDEB_IN_CDB, 3, -1);
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
			if (is_disk) {
				len += resp_format_pg(ap + len, pcontrol,
						      target);
				len += resp_caching_pg(ap + len, pcontrol,
						       target);
			}
			len += resp_ctrl_m_pg(ap + len, pcontrol, target);
			len += resp_sas_sf_m_pg(ap + len, pcontrol, target);
			if (0xff == subpcode) {
				len += resp_sas_pcd_m_spg(ap + len, pcontrol,
						  target, target_dev_id);
				len += resp_sas_sha_m_spg(ap + len, pcontrol);
			}
			len += resp_iec_m_pg(ap + len, pcontrol, target);
			offset += len;
		} else {
			mk_sense_invalid_fld(scp, SDEB_IN_CDB, 3, -1);
			return check_condition_result;
                }
		break;
	default:
		bad_pcode = true;
		break;
	}
	if (bad_pcode) {
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, 2, 5);
		return check_condition_result;
	}
	if (msense_6)
		arr[0] = offset - 1;
	else
		put_unaligned_be16((offset - 2), arr + 0);
	return fill_from_dev_buffer(scp, arr, min(alloc_len, offset));
}

#define SDEBUG_MAX_MSELECT_SZ 512

static int resp_mode_select(struct scsi_cmnd *scp,
			    struct sdebug_dev_info *devip)
{
	int pf, sp, ps, md_len, bd_len, off, spf, pg_len;
	int param_len, res, mpage;
	unsigned char arr[SDEBUG_MAX_MSELECT_SZ];
	unsigned char *cmd = scp->cmnd;
	int mselect6 = (MODE_SELECT == cmd[0]);

	memset(arr, 0, sizeof(arr));
	pf = cmd[1] & 0x10;
	sp = cmd[1] & 0x1;
	param_len = mselect6 ? cmd[4] : get_unaligned_be16(cmd + 7);
	if ((0 == pf) || sp || (param_len > SDEBUG_MAX_MSELECT_SZ)) {
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, mselect6 ? 4 : 7, -1);
		return check_condition_result;
	}
        res = fetch_to_dev_buffer(scp, arr, param_len);
        if (-1 == res)
		return DID_ERROR << 16;
	else if (sdebug_verbose && (res < param_len))
		sdev_printk(KERN_INFO, scp->device,
			    "%s: cdb indicated=%d, IO sent=%d bytes\n",
			    __func__, param_len, res);
	md_len = mselect6 ? (arr[0] + 1) : (get_unaligned_be16(arr + 0) + 2);
	bd_len = mselect6 ? arr[3] : get_unaligned_be16(arr + 6);
	if (md_len > 2) {
		mk_sense_invalid_fld(scp, SDEB_IN_DATA, 0, -1);
		return check_condition_result;
	}
	off = bd_len + (mselect6 ? 4 : 8);
	mpage = arr[off] & 0x3f;
	ps = !!(arr[off] & 0x80);
	if (ps) {
		mk_sense_invalid_fld(scp, SDEB_IN_DATA, off, 7);
		return check_condition_result;
	}
	spf = !!(arr[off] & 0x40);
	pg_len = spf ? (get_unaligned_be16(arr + off + 2) + 4) :
		       (arr[off + 1] + 2);
	if ((pg_len + off) > param_len) {
		mk_sense_buffer(scp, ILLEGAL_REQUEST,
				PARAMETER_LIST_LENGTH_ERR, 0);
		return check_condition_result;
	}
	switch (mpage) {
	case 0x8:      /* Caching Mode page */
		if (caching_pg[1] == arr[off + 1]) {
			memcpy(caching_pg + 2, arr + off + 2,
			       sizeof(caching_pg) - 2);
			goto set_mode_changed_ua;
		}
		break;
	case 0xa:      /* Control Mode page */
		if (ctrl_m_pg[1] == arr[off + 1]) {
			memcpy(ctrl_m_pg + 2, arr + off + 2,
			       sizeof(ctrl_m_pg) - 2);
			sdebug_dsense = !!(ctrl_m_pg[2] & 0x4);
			goto set_mode_changed_ua;
		}
		break;
	case 0x1c:      /* Informational Exceptions Mode page */
		if (iec_m_pg[1] == arr[off + 1]) {
			memcpy(iec_m_pg + 2, arr + off + 2,
			       sizeof(iec_m_pg) - 2);
			goto set_mode_changed_ua;
		}
		break;
	default:
		break;
	}
	mk_sense_invalid_fld(scp, SDEB_IN_DATA, off, 5);
	return check_condition_result;
set_mode_changed_ua:
	set_bit(SDEBUG_UA_MODE_CHANGED, devip->uas_bm);
	return 0;
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
	int ppc, sp, pcontrol, pcode, subpcode, alloc_len, len, n;
	unsigned char arr[SDEBUG_MAX_LSENSE_SZ];
	unsigned char *cmd = scp->cmnd;

	memset(arr, 0, sizeof(arr));
	ppc = cmd[1] & 0x2;
	sp = cmd[1] & 0x1;
	if (ppc || sp) {
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, 1, ppc ? 1 : 0);
		return check_condition_result;
	}
	pcontrol = (cmd[2] & 0xc0) >> 6;
	pcode = cmd[2] & 0x3f;
	subpcode = cmd[3] & 0xff;
	alloc_len = get_unaligned_be16(cmd + 7);
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
			mk_sense_invalid_fld(scp, SDEB_IN_CDB, 2, 5);
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
			mk_sense_invalid_fld(scp, SDEB_IN_CDB, 2, 5);
			return check_condition_result;
		}
	} else {
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, 3, -1);
		return check_condition_result;
	}
	len = min(get_unaligned_be16(arr + 2) + 4, alloc_len);
	return fill_from_dev_buffer(scp, arr,
		    min(len, SDEBUG_MAX_INQ_ARR_SZ));
}

static int check_device_access_params(struct scsi_cmnd *scp,
				      unsigned long long lba, unsigned int num)
{
	if (lba + num > sdebug_capacity) {
		mk_sense_buffer(scp, ILLEGAL_REQUEST, LBA_OUT_OF_RANGE, 0);
		return check_condition_result;
	}
	/* transfer length excessive (tie in to block limits VPD page) */
	if (num > sdebug_store_sectors) {
		/* needs work to find which cdb byte 'num' comes from */
		mk_sense_buffer(scp, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB, 0);
		return check_condition_result;
	}
	return 0;
}

/* Returns number of bytes copied or -1 if error. */
static int do_device_access(struct scsi_cmnd *scmd, u64 lba, u32 num,
			    bool do_write)
{
	int ret;
	u64 block, rest = 0;
	struct scsi_data_buffer *sdb;
	enum dma_data_direction dir;

	if (do_write) {
		sdb = scsi_out(scmd);
		dir = DMA_TO_DEVICE;
	} else {
		sdb = scsi_in(scmd);
		dir = DMA_FROM_DEVICE;
	}

	if (!sdb->length)
		return 0;
	if (!(scsi_bidi_cmnd(scmd) || scmd->sc_data_direction == dir))
		return -1;

	block = do_div(lba, sdebug_store_sectors);
	if (block + num > sdebug_store_sectors)
		rest = block + num - sdebug_store_sectors;

	ret = sg_copy_buffer(sdb->table.sgl, sdb->table.nents,
		   fake_storep + (block * sdebug_sector_size),
		   (num - rest) * sdebug_sector_size, 0, do_write);
	if (ret != (num - rest) * sdebug_sector_size)
		return ret;

	if (rest) {
		ret += sg_copy_buffer(sdb->table.sgl, sdb->table.nents,
			    fake_storep, rest * sdebug_sector_size,
			    (num - rest) * sdebug_sector_size, do_write);
	}

	return ret;
}

/* If fake_store(lba,num) compares equal to arr(num), then copy top half of
 * arr into fake_store(lba,num) and return true. If comparison fails then
 * return false. */
static bool comp_write_worker(u64 lba, u32 num, const u8 *arr)
{
	bool res;
	u64 block, rest = 0;
	u32 store_blks = sdebug_store_sectors;
	u32 lb_size = sdebug_sector_size;

	block = do_div(lba, store_blks);
	if (block + num > store_blks)
		rest = block + num - store_blks;

	res = !memcmp(fake_storep + (block * lb_size), arr,
		      (num - rest) * lb_size);
	if (!res)
		return res;
	if (rest)
		res = memcmp(fake_storep, arr + ((num - rest) * lb_size),
			     rest * lb_size);
	if (!res)
		return res;
	arr += num * lb_size;
	memcpy(fake_storep + (block * lb_size), arr, (num - rest) * lb_size);
	if (rest)
		memcpy(fake_storep, arr + ((num - rest) * lb_size),
		       rest * lb_size);
	return res;
}

static __be16 dif_compute_csum(const void *buf, int len)
{
	__be16 csum;

	if (sdebug_guard)
		csum = (__force __be16)ip_compute_csum(buf, len);
	else
		csum = cpu_to_be16(crc_t10dif(buf, len));

	return csum;
}

static int dif_verify(struct t10_pi_tuple *sdt, const void *data,
		      sector_t sector, u32 ei_lba)
{
	__be16 csum = dif_compute_csum(data, sdebug_sector_size);

	if (sdt->guard_tag != csum) {
		pr_err("GUARD check failed on sector %lu rcvd 0x%04x, data 0x%04x\n",
			(unsigned long)sector,
			be16_to_cpu(sdt->guard_tag),
			be16_to_cpu(csum));
		return 0x01;
	}
	if (sdebug_dif == T10_PI_TYPE1_PROTECTION &&
	    be32_to_cpu(sdt->ref_tag) != (sector & 0xffffffff)) {
		pr_err("REF check failed on sector %lu\n",
			(unsigned long)sector);
		return 0x03;
	}
	if (sdebug_dif == T10_PI_TYPE2_PROTECTION &&
	    be32_to_cpu(sdt->ref_tag) != ei_lba) {
		pr_err("REF check failed on sector %lu\n",
			(unsigned long)sector);
		return 0x03;
	}
	return 0;
}

static void dif_copy_prot(struct scsi_cmnd *SCpnt, sector_t sector,
			  unsigned int sectors, bool read)
{
	size_t resid;
	void *paddr;
	const void *dif_store_end = dif_storep + sdebug_store_sectors;
	struct sg_mapping_iter miter;

	/* Bytes of protection data to copy into sgl */
	resid = sectors * sizeof(*dif_storep);

	sg_miter_start(&miter, scsi_prot_sglist(SCpnt),
			scsi_prot_sg_count(SCpnt), SG_MITER_ATOMIC |
			(read ? SG_MITER_TO_SG : SG_MITER_FROM_SG));

	while (sg_miter_next(&miter) && resid > 0) {
		size_t len = min(miter.length, resid);
		void *start = dif_store(sector);
		size_t rest = 0;

		if (dif_store_end < start + len)
			rest = start + len - dif_store_end;

		paddr = miter.addr;

		if (read)
			memcpy(paddr, start, len - rest);
		else
			memcpy(start, paddr, len - rest);

		if (rest) {
			if (read)
				memcpy(paddr + len - rest, dif_storep, rest);
			else
				memcpy(dif_storep, paddr + len - rest, rest);
		}

		sector += len / sizeof(*dif_storep);
		resid -= len;
	}
	sg_miter_stop(&miter);
}

static int prot_verify_read(struct scsi_cmnd *SCpnt, sector_t start_sec,
			    unsigned int sectors, u32 ei_lba)
{
	unsigned int i;
	struct t10_pi_tuple *sdt;
	sector_t sector;

	for (i = 0; i < sectors; i++, ei_lba++) {
		int ret;

		sector = start_sec + i;
		sdt = dif_store(sector);

		if (sdt->app_tag == cpu_to_be16(0xffff))
			continue;

		ret = dif_verify(sdt, fake_store(sector), sector, ei_lba);
		if (ret) {
			dif_errors++;
			return ret;
		}
	}

	dif_copy_prot(SCpnt, start_sec, sectors, true);
	dix_reads++;

	return 0;
}

static int resp_read_dt0(struct scsi_cmnd *scp, struct sdebug_dev_info *devip)
{
	u8 *cmd = scp->cmnd;
	struct sdebug_queued_cmd *sqcp;
	u64 lba;
	u32 num;
	u32 ei_lba;
	unsigned long iflags;
	int ret;
	bool check_prot;

	switch (cmd[0]) {
	case READ_16:
		ei_lba = 0;
		lba = get_unaligned_be64(cmd + 2);
		num = get_unaligned_be32(cmd + 10);
		check_prot = true;
		break;
	case READ_10:
		ei_lba = 0;
		lba = get_unaligned_be32(cmd + 2);
		num = get_unaligned_be16(cmd + 7);
		check_prot = true;
		break;
	case READ_6:
		ei_lba = 0;
		lba = (u32)cmd[3] | (u32)cmd[2] << 8 |
		      (u32)(cmd[1] & 0x1f) << 16;
		num = (0 == cmd[4]) ? 256 : cmd[4];
		check_prot = true;
		break;
	case READ_12:
		ei_lba = 0;
		lba = get_unaligned_be32(cmd + 2);
		num = get_unaligned_be32(cmd + 6);
		check_prot = true;
		break;
	case XDWRITEREAD_10:
		ei_lba = 0;
		lba = get_unaligned_be32(cmd + 2);
		num = get_unaligned_be16(cmd + 7);
		check_prot = false;
		break;
	default:	/* assume READ(32) */
		lba = get_unaligned_be64(cmd + 12);
		ei_lba = get_unaligned_be32(cmd + 20);
		num = get_unaligned_be32(cmd + 28);
		check_prot = false;
		break;
	}
	if (unlikely(have_dif_prot && check_prot)) {
		if (sdebug_dif == T10_PI_TYPE2_PROTECTION &&
		    (cmd[1] & 0xe0)) {
			mk_sense_invalid_opcode(scp);
			return check_condition_result;
		}
		if ((sdebug_dif == T10_PI_TYPE1_PROTECTION ||
		     sdebug_dif == T10_PI_TYPE3_PROTECTION) &&
		    (cmd[1] & 0xe0) == 0)
			sdev_printk(KERN_ERR, scp->device, "Unprotected RD "
				    "to DIF device\n");
	}
	if (unlikely(sdebug_any_injecting_opt)) {
		sqcp = (struct sdebug_queued_cmd *)scp->host_scribble;

		if (sqcp) {
			if (sqcp->inj_short)
				num /= 2;
		}
	} else
		sqcp = NULL;

	/* inline check_device_access_params() */
	if (unlikely(lba + num > sdebug_capacity)) {
		mk_sense_buffer(scp, ILLEGAL_REQUEST, LBA_OUT_OF_RANGE, 0);
		return check_condition_result;
	}
	/* transfer length excessive (tie in to block limits VPD page) */
	if (unlikely(num > sdebug_store_sectors)) {
		/* needs work to find which cdb byte 'num' comes from */
		mk_sense_buffer(scp, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB, 0);
		return check_condition_result;
	}

	if (unlikely((SDEBUG_OPT_MEDIUM_ERR & sdebug_opts) &&
		     (lba <= (OPT_MEDIUM_ERR_ADDR + OPT_MEDIUM_ERR_NUM - 1)) &&
		     ((lba + num) > OPT_MEDIUM_ERR_ADDR))) {
		/* claim unrecoverable read error */
		mk_sense_buffer(scp, MEDIUM_ERROR, UNRECOVERED_READ_ERR, 0);
		/* set info field and valid bit for fixed descriptor */
		if (0x70 == (scp->sense_buffer[0] & 0x7f)) {
			scp->sense_buffer[0] |= 0x80;	/* Valid bit */
			ret = (lba < OPT_MEDIUM_ERR_ADDR)
			      ? OPT_MEDIUM_ERR_ADDR : (int)lba;
			put_unaligned_be32(ret, scp->sense_buffer + 3);
		}
		scsi_set_resid(scp, scsi_bufflen(scp));
		return check_condition_result;
	}

	read_lock_irqsave(&atomic_rw, iflags);

	/* DIX + T10 DIF */
	if (unlikely(sdebug_dix && scsi_prot_sg_count(scp))) {
		int prot_ret = prot_verify_read(scp, lba, num, ei_lba);

		if (prot_ret) {
			read_unlock_irqrestore(&atomic_rw, iflags);
			mk_sense_buffer(scp, ABORTED_COMMAND, 0x10, prot_ret);
			return illegal_condition_result;
		}
	}

	ret = do_device_access(scp, lba, num, false);
	read_unlock_irqrestore(&atomic_rw, iflags);
	if (unlikely(ret == -1))
		return DID_ERROR << 16;

	scsi_in(scp)->resid = scsi_bufflen(scp) - ret;

	if (unlikely(sqcp)) {
		if (sqcp->inj_recovered) {
			mk_sense_buffer(scp, RECOVERED_ERROR,
					THRESHOLD_EXCEEDED, 0);
			return check_condition_result;
		} else if (sqcp->inj_transport) {
			mk_sense_buffer(scp, ABORTED_COMMAND,
					TRANSPORT_PROBLEM, ACK_NAK_TO);
			return check_condition_result;
		} else if (sqcp->inj_dif) {
			/* Logical block guard check failed */
			mk_sense_buffer(scp, ABORTED_COMMAND, 0x10, 1);
			return illegal_condition_result;
		} else if (sqcp->inj_dix) {
			mk_sense_buffer(scp, ILLEGAL_REQUEST, 0x10, 1);
			return illegal_condition_result;
		}
	}
	return 0;
}

static void dump_sector(unsigned char *buf, int len)
{
	int i, j, n;

	pr_err(">>> Sector Dump <<<\n");
	for (i = 0 ; i < len ; i += 16) {
		char b[128];

		for (j = 0, n = 0; j < 16; j++) {
			unsigned char c = buf[i+j];

			if (c >= 0x20 && c < 0x7e)
				n += scnprintf(b + n, sizeof(b) - n,
					       " %c ", buf[i+j]);
			else
				n += scnprintf(b + n, sizeof(b) - n,
					       "%02x ", buf[i+j]);
		}
		pr_err("%04d: %s\n", i, b);
	}
}

static int prot_verify_write(struct scsi_cmnd *SCpnt, sector_t start_sec,
			     unsigned int sectors, u32 ei_lba)
{
	int ret;
	struct t10_pi_tuple *sdt;
	void *daddr;
	sector_t sector = start_sec;
	int ppage_offset;
	int dpage_offset;
	struct sg_mapping_iter diter;
	struct sg_mapping_iter piter;

	BUG_ON(scsi_sg_count(SCpnt) == 0);
	BUG_ON(scsi_prot_sg_count(SCpnt) == 0);

	sg_miter_start(&piter, scsi_prot_sglist(SCpnt),
			scsi_prot_sg_count(SCpnt),
			SG_MITER_ATOMIC | SG_MITER_FROM_SG);
	sg_miter_start(&diter, scsi_sglist(SCpnt), scsi_sg_count(SCpnt),
			SG_MITER_ATOMIC | SG_MITER_FROM_SG);

	/* For each protection page */
	while (sg_miter_next(&piter)) {
		dpage_offset = 0;
		if (WARN_ON(!sg_miter_next(&diter))) {
			ret = 0x01;
			goto out;
		}

		for (ppage_offset = 0; ppage_offset < piter.length;
		     ppage_offset += sizeof(struct t10_pi_tuple)) {
			/* If we're at the end of the current
			 * data page advance to the next one
			 */
			if (dpage_offset >= diter.length) {
				if (WARN_ON(!sg_miter_next(&diter))) {
					ret = 0x01;
					goto out;
				}
				dpage_offset = 0;
			}

			sdt = piter.addr + ppage_offset;
			daddr = diter.addr + dpage_offset;

			ret = dif_verify(sdt, daddr, sector, ei_lba);
			if (ret) {
				dump_sector(daddr, sdebug_sector_size);
				goto out;
			}

			sector++;
			ei_lba++;
			dpage_offset += sdebug_sector_size;
		}
		diter.consumed = dpage_offset;
		sg_miter_stop(&diter);
	}
	sg_miter_stop(&piter);

	dif_copy_prot(SCpnt, start_sec, sectors, false);
	dix_writes++;

	return 0;

out:
	dif_errors++;
	sg_miter_stop(&diter);
	sg_miter_stop(&piter);
	return ret;
}

static unsigned long lba_to_map_index(sector_t lba)
{
	if (sdebug_unmap_alignment)
		lba += sdebug_unmap_granularity - sdebug_unmap_alignment;
	sector_div(lba, sdebug_unmap_granularity);
	return lba;
}

static sector_t map_index_to_lba(unsigned long index)
{
	sector_t lba = index * sdebug_unmap_granularity;

	if (sdebug_unmap_alignment)
		lba -= sdebug_unmap_granularity - sdebug_unmap_alignment;
	return lba;
}

static unsigned int map_state(sector_t lba, unsigned int *num)
{
	sector_t end;
	unsigned int mapped;
	unsigned long index;
	unsigned long next;

	index = lba_to_map_index(lba);
	mapped = test_bit(index, map_storep);

	if (mapped)
		next = find_next_zero_bit(map_storep, map_size, index);
	else
		next = find_next_bit(map_storep, map_size, index);

	end = min_t(sector_t, sdebug_store_sectors,  map_index_to_lba(next));
	*num = end - lba;
	return mapped;
}

static void map_region(sector_t lba, unsigned int len)
{
	sector_t end = lba + len;

	while (lba < end) {
		unsigned long index = lba_to_map_index(lba);

		if (index < map_size)
			set_bit(index, map_storep);

		lba = map_index_to_lba(index + 1);
	}
}

static void unmap_region(sector_t lba, unsigned int len)
{
	sector_t end = lba + len;

	while (lba < end) {
		unsigned long index = lba_to_map_index(lba);

		if (lba == map_index_to_lba(index) &&
		    lba + sdebug_unmap_granularity <= end &&
		    index < map_size) {
			clear_bit(index, map_storep);
			if (sdebug_lbprz) {  /* for LBPRZ=2 return 0xff_s */
				memset(fake_storep +
				       lba * sdebug_sector_size,
				       (sdebug_lbprz & 1) ? 0 : 0xff,
				       sdebug_sector_size *
				       sdebug_unmap_granularity);
			}
			if (dif_storep) {
				memset(dif_storep + lba, 0xff,
				       sizeof(*dif_storep) *
				       sdebug_unmap_granularity);
			}
		}
		lba = map_index_to_lba(index + 1);
	}
}

static int resp_write_dt0(struct scsi_cmnd *scp, struct sdebug_dev_info *devip)
{
	u8 *cmd = scp->cmnd;
	u64 lba;
	u32 num;
	u32 ei_lba;
	unsigned long iflags;
	int ret;
	bool check_prot;

	switch (cmd[0]) {
	case WRITE_16:
		ei_lba = 0;
		lba = get_unaligned_be64(cmd + 2);
		num = get_unaligned_be32(cmd + 10);
		check_prot = true;
		break;
	case WRITE_10:
		ei_lba = 0;
		lba = get_unaligned_be32(cmd + 2);
		num = get_unaligned_be16(cmd + 7);
		check_prot = true;
		break;
	case WRITE_6:
		ei_lba = 0;
		lba = (u32)cmd[3] | (u32)cmd[2] << 8 |
		      (u32)(cmd[1] & 0x1f) << 16;
		num = (0 == cmd[4]) ? 256 : cmd[4];
		check_prot = true;
		break;
	case WRITE_12:
		ei_lba = 0;
		lba = get_unaligned_be32(cmd + 2);
		num = get_unaligned_be32(cmd + 6);
		check_prot = true;
		break;
	case 0x53:	/* XDWRITEREAD(10) */
		ei_lba = 0;
		lba = get_unaligned_be32(cmd + 2);
		num = get_unaligned_be16(cmd + 7);
		check_prot = false;
		break;
	default:	/* assume WRITE(32) */
		lba = get_unaligned_be64(cmd + 12);
		ei_lba = get_unaligned_be32(cmd + 20);
		num = get_unaligned_be32(cmd + 28);
		check_prot = false;
		break;
	}
	if (unlikely(have_dif_prot && check_prot)) {
		if (sdebug_dif == T10_PI_TYPE2_PROTECTION &&
		    (cmd[1] & 0xe0)) {
			mk_sense_invalid_opcode(scp);
			return check_condition_result;
		}
		if ((sdebug_dif == T10_PI_TYPE1_PROTECTION ||
		     sdebug_dif == T10_PI_TYPE3_PROTECTION) &&
		    (cmd[1] & 0xe0) == 0)
			sdev_printk(KERN_ERR, scp->device, "Unprotected WR "
				    "to DIF device\n");
	}

	/* inline check_device_access_params() */
	if (unlikely(lba + num > sdebug_capacity)) {
		mk_sense_buffer(scp, ILLEGAL_REQUEST, LBA_OUT_OF_RANGE, 0);
		return check_condition_result;
	}
	/* transfer length excessive (tie in to block limits VPD page) */
	if (unlikely(num > sdebug_store_sectors)) {
		/* needs work to find which cdb byte 'num' comes from */
		mk_sense_buffer(scp, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB, 0);
		return check_condition_result;
	}

	write_lock_irqsave(&atomic_rw, iflags);

	/* DIX + T10 DIF */
	if (unlikely(sdebug_dix && scsi_prot_sg_count(scp))) {
		int prot_ret = prot_verify_write(scp, lba, num, ei_lba);

		if (prot_ret) {
			write_unlock_irqrestore(&atomic_rw, iflags);
			mk_sense_buffer(scp, ILLEGAL_REQUEST, 0x10, prot_ret);
			return illegal_condition_result;
		}
	}

	ret = do_device_access(scp, lba, num, true);
	if (unlikely(scsi_debug_lbp()))
		map_region(lba, num);
	write_unlock_irqrestore(&atomic_rw, iflags);
	if (unlikely(-1 == ret))
		return DID_ERROR << 16;
	else if (unlikely(sdebug_verbose &&
			  (ret < (num * sdebug_sector_size))))
		sdev_printk(KERN_INFO, scp->device,
			    "%s: write: cdb indicated=%u, IO sent=%d bytes\n",
			    my_name, num * sdebug_sector_size, ret);

	if (unlikely(sdebug_any_injecting_opt)) {
		struct sdebug_queued_cmd *sqcp =
				(struct sdebug_queued_cmd *)scp->host_scribble;

		if (sqcp) {
			if (sqcp->inj_recovered) {
				mk_sense_buffer(scp, RECOVERED_ERROR,
						THRESHOLD_EXCEEDED, 0);
				return check_condition_result;
			} else if (sqcp->inj_dif) {
				/* Logical block guard check failed */
				mk_sense_buffer(scp, ABORTED_COMMAND, 0x10, 1);
				return illegal_condition_result;
			} else if (sqcp->inj_dix) {
				mk_sense_buffer(scp, ILLEGAL_REQUEST, 0x10, 1);
				return illegal_condition_result;
			}
		}
	}
	return 0;
}

static int resp_write_same(struct scsi_cmnd *scp, u64 lba, u32 num,
			   u32 ei_lba, bool unmap, bool ndob)
{
	unsigned long iflags;
	unsigned long long i;
	int ret;
	u64 lba_off;

	ret = check_device_access_params(scp, lba, num);
	if (ret)
		return ret;

	write_lock_irqsave(&atomic_rw, iflags);

	if (unmap && scsi_debug_lbp()) {
		unmap_region(lba, num);
		goto out;
	}

	lba_off = lba * sdebug_sector_size;
	/* if ndob then zero 1 logical block, else fetch 1 logical block */
	if (ndob) {
		memset(fake_storep + lba_off, 0, sdebug_sector_size);
		ret = 0;
	} else
		ret = fetch_to_dev_buffer(scp, fake_storep + lba_off,
					  sdebug_sector_size);

	if (-1 == ret) {
		write_unlock_irqrestore(&atomic_rw, iflags);
		return DID_ERROR << 16;
	} else if (sdebug_verbose && (ret < (num * sdebug_sector_size)))
		sdev_printk(KERN_INFO, scp->device,
			    "%s: %s: cdb indicated=%u, IO sent=%d bytes\n",
			    my_name, "write same",
			    num * sdebug_sector_size, ret);

	/* Copy first sector to remaining blocks */
	for (i = 1 ; i < num ; i++)
		memcpy(fake_storep + ((lba + i) * sdebug_sector_size),
		       fake_storep + lba_off,
		       sdebug_sector_size);

	if (scsi_debug_lbp())
		map_region(lba, num);
out:
	write_unlock_irqrestore(&atomic_rw, iflags);

	return 0;
}

static int resp_write_same_10(struct scsi_cmnd *scp,
			      struct sdebug_dev_info *devip)
{
	u8 *cmd = scp->cmnd;
	u32 lba;
	u16 num;
	u32 ei_lba = 0;
	bool unmap = false;

	if (cmd[1] & 0x8) {
		if (sdebug_lbpws10 == 0) {
			mk_sense_invalid_fld(scp, SDEB_IN_CDB, 1, 3);
			return check_condition_result;
		} else
			unmap = true;
	}
	lba = get_unaligned_be32(cmd + 2);
	num = get_unaligned_be16(cmd + 7);
	if (num > sdebug_write_same_length) {
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, 7, -1);
		return check_condition_result;
	}
	return resp_write_same(scp, lba, num, ei_lba, unmap, false);
}

static int resp_write_same_16(struct scsi_cmnd *scp,
			      struct sdebug_dev_info *devip)
{
	u8 *cmd = scp->cmnd;
	u64 lba;
	u32 num;
	u32 ei_lba = 0;
	bool unmap = false;
	bool ndob = false;

	if (cmd[1] & 0x8) {	/* UNMAP */
		if (sdebug_lbpws == 0) {
			mk_sense_invalid_fld(scp, SDEB_IN_CDB, 1, 3);
			return check_condition_result;
		} else
			unmap = true;
	}
	if (cmd[1] & 0x1)  /* NDOB (no data-out buffer, assumes zeroes) */
		ndob = true;
	lba = get_unaligned_be64(cmd + 2);
	num = get_unaligned_be32(cmd + 10);
	if (num > sdebug_write_same_length) {
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, 10, -1);
		return check_condition_result;
	}
	return resp_write_same(scp, lba, num, ei_lba, unmap, ndob);
}

/* Note the mode field is in the same position as the (lower) service action
 * field. For the Report supported operation codes command, SPC-4 suggests
 * each mode of this command should be reported separately; for future. */
static int resp_write_buffer(struct scsi_cmnd *scp,
			     struct sdebug_dev_info *devip)
{
	u8 *cmd = scp->cmnd;
	struct scsi_device *sdp = scp->device;
	struct sdebug_dev_info *dp;
	u8 mode;

	mode = cmd[1] & 0x1f;
	switch (mode) {
	case 0x4:	/* download microcode (MC) and activate (ACT) */
		/* set UAs on this device only */
		set_bit(SDEBUG_UA_BUS_RESET, devip->uas_bm);
		set_bit(SDEBUG_UA_MICROCODE_CHANGED, devip->uas_bm);
		break;
	case 0x5:	/* download MC, save and ACT */
		set_bit(SDEBUG_UA_MICROCODE_CHANGED_WO_RESET, devip->uas_bm);
		break;
	case 0x6:	/* download MC with offsets and ACT */
		/* set UAs on most devices (LUs) in this target */
		list_for_each_entry(dp,
				    &devip->sdbg_host->dev_info_list,
				    dev_list)
			if (dp->target == sdp->id) {
				set_bit(SDEBUG_UA_BUS_RESET, dp->uas_bm);
				if (devip != dp)
					set_bit(SDEBUG_UA_MICROCODE_CHANGED,
						dp->uas_bm);
			}
		break;
	case 0x7:	/* download MC with offsets, save, and ACT */
		/* set UA on all devices (LUs) in this target */
		list_for_each_entry(dp,
				    &devip->sdbg_host->dev_info_list,
				    dev_list)
			if (dp->target == sdp->id)
				set_bit(SDEBUG_UA_MICROCODE_CHANGED_WO_RESET,
					dp->uas_bm);
		break;
	default:
		/* do nothing for this command for other mode values */
		break;
	}
	return 0;
}

static int resp_comp_write(struct scsi_cmnd *scp,
			   struct sdebug_dev_info *devip)
{
	u8 *cmd = scp->cmnd;
	u8 *arr;
	u8 *fake_storep_hold;
	u64 lba;
	u32 dnum;
	u32 lb_size = sdebug_sector_size;
	u8 num;
	unsigned long iflags;
	int ret;
	int retval = 0;

	lba = get_unaligned_be64(cmd + 2);
	num = cmd[13];		/* 1 to a maximum of 255 logical blocks */
	if (0 == num)
		return 0;	/* degenerate case, not an error */
	if (sdebug_dif == T10_PI_TYPE2_PROTECTION &&
	    (cmd[1] & 0xe0)) {
		mk_sense_invalid_opcode(scp);
		return check_condition_result;
	}
	if ((sdebug_dif == T10_PI_TYPE1_PROTECTION ||
	     sdebug_dif == T10_PI_TYPE3_PROTECTION) &&
	    (cmd[1] & 0xe0) == 0)
		sdev_printk(KERN_ERR, scp->device, "Unprotected WR "
			    "to DIF device\n");

	/* inline check_device_access_params() */
	if (lba + num > sdebug_capacity) {
		mk_sense_buffer(scp, ILLEGAL_REQUEST, LBA_OUT_OF_RANGE, 0);
		return check_condition_result;
	}
	/* transfer length excessive (tie in to block limits VPD page) */
	if (num > sdebug_store_sectors) {
		/* needs work to find which cdb byte 'num' comes from */
		mk_sense_buffer(scp, ILLEGAL_REQUEST, INVALID_FIELD_IN_CDB, 0);
		return check_condition_result;
	}
	dnum = 2 * num;
	arr = kzalloc(dnum * lb_size, GFP_ATOMIC);
	if (NULL == arr) {
		mk_sense_buffer(scp, ILLEGAL_REQUEST, INSUFF_RES_ASC,
				INSUFF_RES_ASCQ);
		return check_condition_result;
	}

	write_lock_irqsave(&atomic_rw, iflags);

	/* trick do_device_access() to fetch both compare and write buffers
	 * from data-in into arr. Safe (atomic) since write_lock held. */
	fake_storep_hold = fake_storep;
	fake_storep = arr;
	ret = do_device_access(scp, 0, dnum, true);
	fake_storep = fake_storep_hold;
	if (ret == -1) {
		retval = DID_ERROR << 16;
		goto cleanup;
	} else if (sdebug_verbose && (ret < (dnum * lb_size)))
		sdev_printk(KERN_INFO, scp->device, "%s: compare_write: cdb "
			    "indicated=%u, IO sent=%d bytes\n", my_name,
			    dnum * lb_size, ret);
	if (!comp_write_worker(lba, num, arr)) {
		mk_sense_buffer(scp, MISCOMPARE, MISCOMPARE_VERIFY_ASC, 0);
		retval = check_condition_result;
		goto cleanup;
	}
	if (scsi_debug_lbp())
		map_region(lba, num);
cleanup:
	write_unlock_irqrestore(&atomic_rw, iflags);
	kfree(arr);
	return retval;
}

struct unmap_block_desc {
	__be64	lba;
	__be32	blocks;
	__be32	__reserved;
};

static int resp_unmap(struct scsi_cmnd *scp, struct sdebug_dev_info *devip)
{
	unsigned char *buf;
	struct unmap_block_desc *desc;
	unsigned int i, payload_len, descriptors;
	int ret;
	unsigned long iflags;


	if (!scsi_debug_lbp())
		return 0;	/* fib and say its done */
	payload_len = get_unaligned_be16(scp->cmnd + 7);
	BUG_ON(scsi_bufflen(scp) != payload_len);

	descriptors = (payload_len - 8) / 16;
	if (descriptors > sdebug_unmap_max_desc) {
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, 7, -1);
		return check_condition_result;
	}

	buf = kzalloc(scsi_bufflen(scp), GFP_ATOMIC);
	if (!buf) {
		mk_sense_buffer(scp, ILLEGAL_REQUEST, INSUFF_RES_ASC,
				INSUFF_RES_ASCQ);
		return check_condition_result;
	}

	scsi_sg_copy_to_buffer(scp, buf, scsi_bufflen(scp));

	BUG_ON(get_unaligned_be16(&buf[0]) != payload_len - 2);
	BUG_ON(get_unaligned_be16(&buf[2]) != descriptors * 16);

	desc = (void *)&buf[8];

	write_lock_irqsave(&atomic_rw, iflags);

	for (i = 0 ; i < descriptors ; i++) {
		unsigned long long lba = get_unaligned_be64(&desc[i].lba);
		unsigned int num = get_unaligned_be32(&desc[i].blocks);

		ret = check_device_access_params(scp, lba, num);
		if (ret)
			goto out;

		unmap_region(lba, num);
	}

	ret = 0;

out:
	write_unlock_irqrestore(&atomic_rw, iflags);
	kfree(buf);

	return ret;
}

#define SDEBUG_GET_LBA_STATUS_LEN 32

static int resp_get_lba_status(struct scsi_cmnd *scp,
			       struct sdebug_dev_info *devip)
{
	u8 *cmd = scp->cmnd;
	u64 lba;
	u32 alloc_len, mapped, num;
	u8 arr[SDEBUG_GET_LBA_STATUS_LEN];
	int ret;

	lba = get_unaligned_be64(cmd + 2);
	alloc_len = get_unaligned_be32(cmd + 10);

	if (alloc_len < 24)
		return 0;

	ret = check_device_access_params(scp, lba, 1);
	if (ret)
		return ret;

	if (scsi_debug_lbp())
		mapped = map_state(lba, &num);
	else {
		mapped = 1;
		/* following just in case virtual_gb changed */
		sdebug_capacity = get_sdebug_capacity();
		if (sdebug_capacity - lba <= 0xffffffff)
			num = sdebug_capacity - lba;
		else
			num = 0xffffffff;
	}

	memset(arr, 0, SDEBUG_GET_LBA_STATUS_LEN);
	put_unaligned_be32(20, arr);		/* Parameter Data Length */
	put_unaligned_be64(lba, arr + 8);	/* LBA */
	put_unaligned_be32(num, arr + 16);	/* Number of blocks */
	arr[20] = !mapped;		/* prov_stat=0: mapped; 1: dealloc */

	return fill_from_dev_buffer(scp, arr, SDEBUG_GET_LBA_STATUS_LEN);
}

#define RL_BUCKET_ELEMS 8

/* Even though each pseudo target has a REPORT LUNS "well known logical unit"
 * (W-LUN), the normal Linux scanning logic does not associate it with a
 * device (e.g. /dev/sg7). The following magic will make that association:
 *   "cd /sys/class/scsi_host/host<n> ; echo '- - 49409' > scan"
 * where <n> is a host number. If there are multiple targets in a host then
 * the above will associate a W-LUN to each target. To only get a W-LUN
 * for target 2, then use "echo '- 2 49409' > scan" .
 */
static int resp_report_luns(struct scsi_cmnd *scp,
			    struct sdebug_dev_info *devip)
{
	unsigned char *cmd = scp->cmnd;
	unsigned int alloc_len;
	unsigned char select_report;
	u64 lun;
	struct scsi_lun *lun_p;
	u8 arr[RL_BUCKET_ELEMS * sizeof(struct scsi_lun)];
	unsigned int lun_cnt;	/* normal LUN count (max: 256) */
	unsigned int wlun_cnt;	/* report luns W-LUN count */
	unsigned int tlun_cnt;	/* total LUN count */
	unsigned int rlen;	/* response length (in bytes) */
	int k, j, n, res;
	unsigned int off_rsp = 0;
	const int sz_lun = sizeof(struct scsi_lun);

	clear_luns_changed_on_target(devip);

	select_report = cmd[2];
	alloc_len = get_unaligned_be32(cmd + 6);

	if (alloc_len < 4) {
		pr_err("alloc len too small %d\n", alloc_len);
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, 6, -1);
		return check_condition_result;
	}

	switch (select_report) {
	case 0:		/* all LUNs apart from W-LUNs */
		lun_cnt = sdebug_max_luns;
		wlun_cnt = 0;
		break;
	case 1:		/* only W-LUNs */
		lun_cnt = 0;
		wlun_cnt = 1;
		break;
	case 2:		/* all LUNs */
		lun_cnt = sdebug_max_luns;
		wlun_cnt = 1;
		break;
	case 0x10:	/* only administrative LUs */
	case 0x11:	/* see SPC-5 */
	case 0x12:	/* only subsiduary LUs owned by referenced LU */
	default:
		pr_debug("select report invalid %d\n", select_report);
		mk_sense_invalid_fld(scp, SDEB_IN_CDB, 2, -1);
		return check_condition_result;
	}

	if (sdebug_no_lun_0 && (lun_cnt > 0))
		--lun_cnt;

	tlun_cnt = lun_cnt + wlun_cnt;
	rlen = tlun_cnt * sz_lun;	/* excluding 8 byte header */
	scsi_set_resid(scp, scsi_bufflen(scp));
	pr_debug("select_report %d luns = %d wluns = %d no_lun0 %d\n",
		 select_report, lun_cnt, wlun_cnt, sdebug_no_lun_0);

	/* loops rely on sizeof response header same as sizeof lun (both 8) */
	lun = sdebug_no_lun_0 ? 1 : 0;
	for (k = 0, j = 0, res = 0; true; ++k, j = 0) {
		memset(arr, 0, sizeof(arr));
		lun_p = (struct scsi_lun *)&arr[0];
		if (k == 0) {
			put_unaligned_be32(rlen, &arr[0]);
			++lun_p;
			j = 1;
		}
		for ( ; j < RL_BUCKET_ELEMS; ++j, ++lun_p) {
			if ((k * RL_BUCKET_ELEMS) + j > lun_cnt)
				break;
			int_to_scsilun(lun++, lun_p);
		}
		if (j < RL_BUCKET_ELEMS)
			break;
		n = j * sz_lun;
		res = p_fill_from_dev_buffer(scp, arr, n, off_rsp);
		if (res)
			return res;
		off_rsp += n;
	}
	if (wlun_cnt) {
		int_to_scsilun(SCSI_W_LUN_REPORT_LUNS, lun_p);
		++j;
	}
	if (j > 0)
		res = p_fill_from_dev_buffer(scp, arr, j * sz_lun, off_rsp);
	return res;
}

static int resp_xdwriteread(struct scsi_cmnd *scp, unsigned long long lba,
			    unsigned int num, struct sdebug_dev_info *devip)
{
	int j;
	unsigned char *kaddr, *buf;
	unsigned int offset;
	struct scsi_data_buffer *sdb = scsi_in(scp);
	struct sg_mapping_iter miter;

	/* better not to use temporary buffer. */
	buf = kzalloc(scsi_bufflen(scp), GFP_ATOMIC);
	if (!buf) {
		mk_sense_buffer(scp, ILLEGAL_REQUEST, INSUFF_RES_ASC,
				INSUFF_RES_ASCQ);
		return check_condition_result;
	}

	scsi_sg_copy_to_buffer(scp, buf, scsi_bufflen(scp));

	offset = 0;
	sg_miter_start(&miter, sdb->table.sgl, sdb->table.nents,
			SG_MITER_ATOMIC | SG_MITER_TO_SG);

	while (sg_miter_next(&miter)) {
		kaddr = miter.addr;
		for (j = 0; j < miter.length; j++)
			*(kaddr + j) ^= *(buf + offset + j);

		offset += miter.length;
	}
	sg_miter_stop(&miter);
	kfree(buf);

	return 0;
}

static int resp_xdwriteread_10(struct scsi_cmnd *scp,
			       struct sdebug_dev_info *devip)
{
	u8 *cmd = scp->cmnd;
	u64 lba;
	u32 num;
	int errsts;

	if (!scsi_bidi_cmnd(scp)) {
		mk_sense_buffer(scp, ILLEGAL_REQUEST, INSUFF_RES_ASC,
				INSUFF_RES_ASCQ);
		return check_condition_result;
	}
	errsts = resp_read_dt0(scp, devip);
	if (errsts)
		return errsts;
	if (!(cmd[1] & 0x4)) {		/* DISABLE_WRITE is not set */
		errsts = resp_write_dt0(scp, devip);
		if (errsts)
			return errsts;
	}
	lba = get_unaligned_be32(cmd + 2);
	num = get_unaligned_be16(cmd + 7);
	return resp_xdwriteread(scp, lba, num, devip);
}

static struct sdebug_queue *get_queue(struct scsi_cmnd *cmnd)
{
	struct sdebug_queue *sqp = sdebug_q_arr;

	if (sdebug_mq_active) {
		u32 tag = blk_mq_unique_tag(cmnd->request);
		u16 hwq = blk_mq_unique_tag_to_hwq(tag);

		if (unlikely(hwq >= submit_queues)) {
			pr_warn("Unexpected hwq=%d, apply modulo\n", hwq);
			hwq %= submit_queues;
		}
		pr_debug("tag=%u, hwq=%d\n", tag, hwq);
		return sqp + hwq;
	} else
		return sqp;
}

/* Queued (deferred) command completions converge here. */
static void sdebug_q_cmd_complete(struct sdebug_defer *sd_dp)
{
	int qc_idx;
	int retiring = 0;
	unsigned long iflags;
	struct sdebug_queue *sqp;
	struct sdebug_queued_cmd *sqcp;
	struct scsi_cmnd *scp;
	struct sdebug_dev_info *devip;

	qc_idx = sd_dp->qc_idx;
	sqp = sdebug_q_arr + sd_dp->sqa_idx;
	if (sdebug_statistics) {
		atomic_inc(&sdebug_completions);
		if (raw_smp_processor_id() != sd_dp->issuing_cpu)
			atomic_inc(&sdebug_miss_cpus);
	}
	if (unlikely((qc_idx < 0) || (qc_idx >= SDEBUG_CANQUEUE))) {
		pr_err("wild qc_idx=%d\n", qc_idx);
		return;
	}
	spin_lock_irqsave(&sqp->qc_lock, iflags);
	sqcp = &sqp->qc_arr[qc_idx];
	scp = sqcp->a_cmnd;
	if (unlikely(scp == NULL)) {
		spin_unlock_irqrestore(&sqp->qc_lock, iflags);
		pr_err("scp is NULL, sqa_idx=%d, qc_idx=%d\n",
		       sd_dp->sqa_idx, qc_idx);
		return;
	}
	devip = (struct sdebug_dev_info *)scp->device->hostdata;
	if (likely(devip))
		atomic_dec(&devip->num_in_q);
	else
		pr_err("devip=NULL\n");
	if (unlikely(atomic_read(&retired_max_queue) > 0))
		retiring = 1;

	sqcp->a_cmnd = NULL;
	if (unlikely(!test_and_clear_bit(qc_idx, sqp->in_use_bm))) {
		spin_unlock_irqrestore(&sqp->qc_lock, iflags);
		pr_err("Unexpected completion\n");
		return;
	}

	if (unlikely(retiring)) {	/* user has reduced max_queue */
		int k, retval;

		retval = atomic_read(&retired_max_queue);
		if (qc_idx >= retval) {
			spin_unlock_irqrestore(&sqp->qc_lock, iflags);
			pr_err("index %d too large\n", retval);
			return;
		}
		k = find_last_bit(sqp->in_use_bm, retval);
		if ((k < sdebug_max_queue) || (k == retval))
			atomic_set(&retired_max_queue, 0);
		else
			atomic_set(&retired_max_queue, k + 1);
	}
	spin_unlock_irqrestore(&sqp->qc_lock, iflags);
	scp->scsi_done(scp); /* callback to mid level */
}

/* When high resolution timer goes off this function is called. */
static enum hrtimer_restart sdebug_q_cmd_hrt_complete(struct hrtimer *timer)
{
	struct sdebug_defer *sd_dp = container_of(timer, struct sdebug_defer,
						  hrt);
	sdebug_q_cmd_complete(sd_dp);
	return HRTIMER_NORESTART;
}

/* When work queue schedules work, it calls this function. */
static void sdebug_q_cmd_wq_complete(struct work_struct *work)
{
	struct sdebug_defer *sd_dp = container_of(work, struct sdebug_defer,
						  ew.work);
	sdebug_q_cmd_complete(sd_dp);
}

static bool got_shared_uuid;
static uuid_be shared_uuid;

static struct sdebug_dev_info *sdebug_device_create(
			struct sdebug_host_info *sdbg_host, gfp_t flags)
{
	struct sdebug_dev_info *devip;

	devip = kzalloc(sizeof(*devip), flags);
	if (devip) {
		if (sdebug_uuid_ctl == 1)
			uuid_be_gen(&devip->lu_name);
		else if (sdebug_uuid_ctl == 2) {
			if (got_shared_uuid)
				devip->lu_name = shared_uuid;
			else {
				uuid_be_gen(&shared_uuid);
				got_shared_uuid = true;
				devip->lu_name = shared_uuid;
			}
		}
		devip->sdbg_host = sdbg_host;
		list_add_tail(&devip->dev_list, &sdbg_host->dev_info_list);
	}
	return devip;
}

static struct sdebug_dev_info *find_build_dev_info(struct scsi_device *sdev)
{
	struct sdebug_host_info *sdbg_host;
	struct sdebug_dev_info *open_devip = NULL;
	struct sdebug_dev_info *devip;

	sdbg_host = *(struct sdebug_host_info **)shost_priv(sdev->host);
	if (!sdbg_host) {
		pr_err("Host info NULL\n");
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
	if (!open_devip) { /* try and make a new one */
		open_devip = sdebug_device_create(sdbg_host, GFP_ATOMIC);
		if (!open_devip) {
			pr_err("out of memory at line %d\n", __LINE__);
			return NULL;
		}
	}

	open_devip->channel = sdev->channel;
	open_devip->target = sdev->id;
	open_devip->lun = sdev->lun;
	open_devip->sdbg_host = sdbg_host;
	atomic_set(&open_devip->num_in_q, 0);
	set_bit(SDEBUG_UA_POR, open_devip->uas_bm);
	open_devip->used = true;
	return open_devip;
}

static int scsi_debug_slave_alloc(struct scsi_device *sdp)
{
	if (sdebug_verbose)
		pr_info("slave_alloc <%u %u %u %llu>\n",
		       sdp->host->host_no, sdp->channel, sdp->id, sdp->lun);
	queue_flag_set_unlocked(QUEUE_FLAG_BIDI, sdp->request_queue);
	return 0;
}

static int scsi_debug_slave_configure(struct scsi_device *sdp)
{
	struct sdebug_dev_info *devip =
			(struct sdebug_dev_info *)sdp->hostdata;

	if (sdebug_verbose)
		pr_info("slave_configure <%u %u %u %llu>\n",
		       sdp->host->host_no, sdp->channel, sdp->id, sdp->lun);
	if (sdp->host->max_cmd_len != SDEBUG_MAX_CMD_LEN)
		sdp->host->max_cmd_len = SDEBUG_MAX_CMD_LEN;
	if (devip == NULL) {
		devip = find_build_dev_info(sdp);
		if (devip == NULL)
			return 1;  /* no resources, will be marked offline */
	}
	sdp->hostdata = devip;
	blk_queue_max_segment_size(sdp->request_queue, -1U);
	if (sdebug_no_uld)
		sdp->no_uld_attach = 1;
	return 0;
}

static void scsi_debug_slave_destroy(struct scsi_device *sdp)
{
	struct sdebug_dev_info *devip =
		(struct sdebug_dev_info *)sdp->hostdata;

	if (sdebug_verbose)
		pr_info("slave_destroy <%u %u %u %llu>\n",
		       sdp->host->host_no, sdp->channel, sdp->id, sdp->lun);
	if (devip) {
		/* make this slot available for re-use */
		devip->used = false;
		sdp->hostdata = NULL;
	}
}

static void stop_qc_helper(struct sdebug_defer *sd_dp)
{
	if (!sd_dp)
		return;
	if ((sdebug_jdelay > 0) || (sdebug_ndelay > 0))
		hrtimer_cancel(&sd_dp->hrt);
	else if (sdebug_jdelay < 0)
		cancel_work_sync(&sd_dp->ew.work);
}

/* If @cmnd found deletes its timer or work queue and returns true; else
   returns false */
static bool stop_queued_cmnd(struct scsi_cmnd *cmnd)
{
	unsigned long iflags;
	int j, k, qmax, r_qmax;
	struct sdebug_queue *sqp;
	struct sdebug_queued_cmd *sqcp;
	struct sdebug_dev_info *devip;
	struct sdebug_defer *sd_dp;

	for (j = 0, sqp = sdebug_q_arr; j < submit_queues; ++j, ++sqp) {
		spin_lock_irqsave(&sqp->qc_lock, iflags);
		qmax = sdebug_max_queue;
		r_qmax = atomic_read(&retired_max_queue);
		if (r_qmax > qmax)
			qmax = r_qmax;
		for (k = 0; k < qmax; ++k) {
			if (test_bit(k, sqp->in_use_bm)) {
				sqcp = &sqp->qc_arr[k];
				if (cmnd != sqcp->a_cmnd)
					continue;
				/* found */
				devip = (struct sdebug_dev_info *)
						cmnd->device->hostdata;
				if (devip)
					atomic_dec(&devip->num_in_q);
				sqcp->a_cmnd = NULL;
				sd_dp = sqcp->sd_dp;
				spin_unlock_irqrestore(&sqp->qc_lock, iflags);
				stop_qc_helper(sd_dp);
				clear_bit(k, sqp->in_use_bm);
				return true;
			}
		}
		spin_unlock_irqrestore(&sqp->qc_lock, iflags);
	}
	return false;
}

/* Deletes (stops) timers or work queues of all queued commands */
static void stop_all_queued(void)
{
	unsigned long iflags;
	int j, k;
	struct sdebug_queue *sqp;
	struct sdebug_queued_cmd *sqcp;
	struct sdebug_dev_info *devip;
	struct sdebug_defer *sd_dp;

	for (j = 0, sqp = sdebug_q_arr; j < submit_queues; ++j, ++sqp) {
		spin_lock_irqsave(&sqp->qc_lock, iflags);
		for (k = 0; k < SDEBUG_CANQUEUE; ++k) {
			if (test_bit(k, sqp->in_use_bm)) {
				sqcp = &sqp->qc_arr[k];
				if (sqcp->a_cmnd == NULL)
					continue;
				devip = (struct sdebug_dev_info *)
					sqcp->a_cmnd->device->hostdata;
				if (devip)
					atomic_dec(&devip->num_in_q);
				sqcp->a_cmnd = NULL;
				sd_dp = sqcp->sd_dp;
				spin_unlock_irqrestore(&sqp->qc_lock, iflags);
				stop_qc_helper(sd_dp);
				clear_bit(k, sqp->in_use_bm);
				spin_lock_irqsave(&sqp->qc_lock, iflags);
			}
		}
		spin_unlock_irqrestore(&sqp->qc_lock, iflags);
	}
}

/* Free queued command memory on heap */
static void free_all_queued(void)
{
	int j, k;
	struct sdebug_queue *sqp;
	struct sdebug_queued_cmd *sqcp;

	for (j = 0, sqp = sdebug_q_arr; j < submit_queues; ++j, ++sqp) {
		for (k = 0; k < SDEBUG_CANQUEUE; ++k) {
			sqcp = &sqp->qc_arr[k];
			kfree(sqcp->sd_dp);
			sqcp->sd_dp = NULL;
		}
	}
}

static int scsi_debug_abort(struct scsi_cmnd *SCpnt)
{
	bool ok;

	++num_aborts;
	if (SCpnt) {
		ok = stop_queued_cmnd(SCpnt);
		if (SCpnt->device && (SDEBUG_OPT_ALL_NOISE & sdebug_opts))
			sdev_printk(KERN_INFO, SCpnt->device,
				    "%s: command%s found\n", __func__,
				    ok ? "" : " not");
	}
	return SUCCESS;
}

static int scsi_debug_device_reset(struct scsi_cmnd * SCpnt)
{
	++num_dev_resets;
	if (SCpnt && SCpnt->device) {
		struct scsi_device *sdp = SCpnt->device;
		struct sdebug_dev_info *devip =
				(struct sdebug_dev_info *)sdp->hostdata;

		if (SDEBUG_OPT_ALL_NOISE & sdebug_opts)
			sdev_printk(KERN_INFO, sdp, "%s\n", __func__);
		if (devip)
			set_bit(SDEBUG_UA_POR, devip->uas_bm);
	}
	return SUCCESS;
}

static int scsi_debug_target_reset(struct scsi_cmnd *SCpnt)
{
	struct sdebug_host_info *sdbg_host;
	struct sdebug_dev_info *devip;
	struct scsi_device *sdp;
	struct Scsi_Host *hp;
	int k = 0;

	++num_target_resets;
	if (!SCpnt)
		goto lie;
	sdp = SCpnt->device;
	if (!sdp)
		goto lie;
	if (SDEBUG_OPT_ALL_NOISE & sdebug_opts)
		sdev_printk(KERN_INFO, sdp, "%s\n", __func__);
	hp = sdp->host;
	if (!hp)
		goto lie;
	sdbg_host = *(struct sdebug_host_info **)shost_priv(hp);
	if (sdbg_host) {
		list_for_each_entry(devip,
				    &sdbg_host->dev_info_list,
				    dev_list)
			if (devip->target == sdp->id) {
				set_bit(SDEBUG_UA_BUS_RESET, devip->uas_bm);
				++k;
			}
	}
	if (SDEBUG_OPT_RESET_NOISE & sdebug_opts)
		sdev_printk(KERN_INFO, sdp,
			    "%s: %d device(s) found in target\n", __func__, k);
lie:
	return SUCCESS;
}

static int scsi_debug_bus_reset(struct scsi_cmnd * SCpnt)
{
	struct sdebug_host_info *sdbg_host;
	struct sdebug_dev_info *devip;
        struct scsi_device * sdp;
        struct Scsi_Host * hp;
	int k = 0;

	++num_bus_resets;
	if (!(SCpnt && SCpnt->device))
		goto lie;
	sdp = SCpnt->device;
	if (SDEBUG_OPT_ALL_NOISE & sdebug_opts)
		sdev_printk(KERN_INFO, sdp, "%s\n", __func__);
	hp = sdp->host;
	if (hp) {
		sdbg_host = *(struct sdebug_host_info **)shost_priv(hp);
		if (sdbg_host) {
			list_for_each_entry(devip,
                                            &sdbg_host->dev_info_list,
					    dev_list) {
				set_bit(SDEBUG_UA_BUS_RESET, devip->uas_bm);
				++k;
			}
		}
	}
	if (SDEBUG_OPT_RESET_NOISE & sdebug_opts)
		sdev_printk(KERN_INFO, sdp,
			    "%s: %d device(s) found in host\n", __func__, k);
lie:
	return SUCCESS;
}

static int scsi_debug_host_reset(struct scsi_cmnd * SCpnt)
{
	struct sdebug_host_info * sdbg_host;
	struct sdebug_dev_info *devip;
	int k = 0;

	++num_host_resets;
	if ((SCpnt->device) && (SDEBUG_OPT_ALL_NOISE & sdebug_opts))
		sdev_printk(KERN_INFO, SCpnt->device, "%s\n", __func__);
        spin_lock(&sdebug_host_list_lock);
        list_for_each_entry(sdbg_host, &sdebug_host_list, host_list) {
		list_for_each_entry(devip, &sdbg_host->dev_info_list,
				    dev_list) {
			set_bit(SDEBUG_UA_BUS_RESET, devip->uas_bm);
			++k;
		}
        }
        spin_unlock(&sdebug_host_list_lock);
	stop_all_queued();
	if (SDEBUG_OPT_RESET_NOISE & sdebug_opts)
		sdev_printk(KERN_INFO, SCpnt->device,
			    "%s: %d device(s) found\n", __func__, k);
	return SUCCESS;
}

static void __init sdebug_build_parts(unsigned char *ramp,
				      unsigned long store_size)
{
	struct partition * pp;
	int starts[SDEBUG_MAX_PARTS + 2];
	int sectors_per_part, num_sectors, k;
	int heads_by_sects, start_sec, end_sec;

	/* assume partition table already zeroed */
	if ((sdebug_num_parts < 1) || (store_size < 1048576))
		return;
	if (sdebug_num_parts > SDEBUG_MAX_PARTS) {
		sdebug_num_parts = SDEBUG_MAX_PARTS;
		pr_warn("reducing partitions to %d\n", SDEBUG_MAX_PARTS);
	}
	num_sectors = (int)sdebug_store_sectors;
	sectors_per_part = (num_sectors - sdebug_sectors_per)
			   / sdebug_num_parts;
	heads_by_sects = sdebug_heads * sdebug_sectors_per;
        starts[0] = sdebug_sectors_per;
	for (k = 1; k < sdebug_num_parts; ++k)
		starts[k] = ((k * sectors_per_part) / heads_by_sects)
			    * heads_by_sects;
	starts[sdebug_num_parts] = num_sectors;
	starts[sdebug_num_parts + 1] = 0;

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

		pp->start_sect = cpu_to_le32(start_sec);
		pp->nr_sects = cpu_to_le32(end_sec - start_sec + 1);
		pp->sys_ind = 0x83;	/* plain Linux partition */
	}
}

static void block_unblock_all_queues(bool block)
{
	int j;
	struct sdebug_queue *sqp;

	for (j = 0, sqp = sdebug_q_arr; j < submit_queues; ++j, ++sqp)
		atomic_set(&sqp->blocked, (int)block);
}

/* Adjust (by rounding down) the sdebug_cmnd_count so abs(every_nth)-1
 * commands will be processed normally before triggers occur.
 */
static void tweak_cmnd_count(void)
{
	int count, modulo;

	modulo = abs(sdebug_every_nth);
	if (modulo < 2)
		return;
	block_unblock_all_queues(true);
	count = atomic_read(&sdebug_cmnd_count);
	atomic_set(&sdebug_cmnd_count, (count / modulo) * modulo);
	block_unblock_all_queues(false);
}

static void clear_queue_stats(void)
{
	atomic_set(&sdebug_cmnd_count, 0);
	atomic_set(&sdebug_completions, 0);
	atomic_set(&sdebug_miss_cpus, 0);
	atomic_set(&sdebug_a_tsf, 0);
}

static void setup_inject(struct sdebug_queue *sqp,
			 struct sdebug_queued_cmd *sqcp)
{
	if ((atomic_read(&sdebug_cmnd_count) % abs(sdebug_every_nth)) > 0)
		return;
	sqcp->inj_recovered = !!(SDEBUG_OPT_RECOVERED_ERR & sdebug_opts);
	sqcp->inj_transport = !!(SDEBUG_OPT_TRANSPORT_ERR & sdebug_opts);
	sqcp->inj_dif = !!(SDEBUG_OPT_DIF_ERR & sdebug_opts);
	sqcp->inj_dix = !!(SDEBUG_OPT_DIX_ERR & sdebug_opts);
	sqcp->inj_short = !!(SDEBUG_OPT_SHORT_TRANSFER & sdebug_opts);
}

/* Complete the processing of the thread that queued a SCSI command to this
 * driver. It either completes the command by calling cmnd_done() or
 * schedules a hr timer or work queue then returns 0. Returns
 * SCSI_MLQUEUE_HOST_BUSY if temporarily out of resources.
 */
static int schedule_resp(struct scsi_cmnd *cmnd, struct sdebug_dev_info *devip,
			 int scsi_result, int delta_jiff)
{
	unsigned long iflags;
	int k, num_in_q, qdepth, inject;
	struct sdebug_queue *sqp;
	struct sdebug_queued_cmd *sqcp;
	struct scsi_device *sdp;
	struct sdebug_defer *sd_dp;

	if (unlikely(devip == NULL)) {
		if (scsi_result == 0)
			scsi_result = DID_NO_CONNECT << 16;
		goto respond_in_thread;
	}
	sdp = cmnd->device;

	if (unlikely(sdebug_verbose && scsi_result))
		sdev_printk(KERN_INFO, sdp, "%s: non-zero result=0x%x\n",
			    __func__, scsi_result);
	if (delta_jiff == 0)
		goto respond_in_thread;

	/* schedule the response at a later time if resources permit */
	sqp = get_queue(cmnd);
	spin_lock_irqsave(&sqp->qc_lock, iflags);
	if (unlikely(atomic_read(&sqp->blocked))) {
		spin_unlock_irqrestore(&sqp->qc_lock, iflags);
		return SCSI_MLQUEUE_HOST_BUSY;
	}
	num_in_q = atomic_read(&devip->num_in_q);
	qdepth = cmnd->device->queue_depth;
	inject = 0;
	if (unlikely((qdepth > 0) && (num_in_q >= qdepth))) {
		if (scsi_result) {
			spin_unlock_irqrestore(&sqp->qc_lock, iflags);
			goto respond_in_thread;
		} else
			scsi_result = device_qfull_result;
	} else if (unlikely(sdebug_every_nth &&
			    (SDEBUG_OPT_RARE_TSF & sdebug_opts) &&
			    (scsi_result == 0))) {
		if ((num_in_q == (qdepth - 1)) &&
		    (atomic_inc_return(&sdebug_a_tsf) >=
		     abs(sdebug_every_nth))) {
			atomic_set(&sdebug_a_tsf, 0);
			inject = 1;
			scsi_result = device_qfull_result;
		}
	}

	k = find_first_zero_bit(sqp->in_use_bm, sdebug_max_queue);
	if (unlikely(k >= sdebug_max_queue)) {
		spin_unlock_irqrestore(&sqp->qc_lock, iflags);
		if (scsi_result)
			goto respond_in_thread;
		else if (SDEBUG_OPT_ALL_TSF & sdebug_opts)
			scsi_result = device_qfull_result;
		if (SDEBUG_OPT_Q_NOISE & sdebug_opts)
			sdev_printk(KERN_INFO, sdp,
				    "%s: max_queue=%d exceeded, %s\n",
				    __func__, sdebug_max_queue,
				    (scsi_result ?  "status: TASK SET FULL" :
						    "report: host busy"));
		if (scsi_result)
			goto respond_in_thread;
		else
			return SCSI_MLQUEUE_HOST_BUSY;
	}
	__set_bit(k, sqp->in_use_bm);
	atomic_inc(&devip->num_in_q);
	sqcp = &sqp->qc_arr[k];
	sqcp->a_cmnd = cmnd;
	cmnd->host_scribble = (unsigned char *)sqcp;
	cmnd->result = scsi_result;
	sd_dp = sqcp->sd_dp;
	spin_unlock_irqrestore(&sqp->qc_lock, iflags);
	if (unlikely(sdebug_every_nth && sdebug_any_injecting_opt))
		setup_inject(sqp, sqcp);
	if (delta_jiff > 0 || sdebug_ndelay > 0) {
		ktime_t kt;

		if (delta_jiff > 0) {
			struct timespec ts;

			jiffies_to_timespec(delta_jiff, &ts);
			kt = ktime_set(ts.tv_sec, ts.tv_nsec);
		} else
			kt = ktime_set(0, sdebug_ndelay);
		if (NULL == sd_dp) {
			sd_dp = kzalloc(sizeof(*sd_dp), GFP_ATOMIC);
			if (NULL == sd_dp)
				return SCSI_MLQUEUE_HOST_BUSY;
			sqcp->sd_dp = sd_dp;
			hrtimer_init(&sd_dp->hrt, CLOCK_MONOTONIC,
				     HRTIMER_MODE_REL_PINNED);
			sd_dp->hrt.function = sdebug_q_cmd_hrt_complete;
			sd_dp->sqa_idx = sqp - sdebug_q_arr;
			sd_dp->qc_idx = k;
		}
		if (sdebug_statistics)
			sd_dp->issuing_cpu = raw_smp_processor_id();
		hrtimer_start(&sd_dp->hrt, kt, HRTIMER_MODE_REL_PINNED);
	} else {	/* jdelay < 0, use work queue */
		if (NULL == sd_dp) {
			sd_dp = kzalloc(sizeof(*sqcp->sd_dp), GFP_ATOMIC);
			if (NULL == sd_dp)
				return SCSI_MLQUEUE_HOST_BUSY;
			sqcp->sd_dp = sd_dp;
			sd_dp->sqa_idx = sqp - sdebug_q_arr;
			sd_dp->qc_idx = k;
			INIT_WORK(&sd_dp->ew.work, sdebug_q_cmd_wq_complete);
		}
		if (sdebug_statistics)
			sd_dp->issuing_cpu = raw_smp_processor_id();
		schedule_work(&sd_dp->ew.work);
	}
	if (unlikely((SDEBUG_OPT_Q_NOISE & sdebug_opts) &&
		     (scsi_result == device_qfull_result)))
		sdev_printk(KERN_INFO, sdp,
			    "%s: num_in_q=%d +1, %s%s\n", __func__,
			    num_in_q, (inject ? "<inject> " : ""),
			    "status: TASK SET FULL");
	return 0;

respond_in_thread:	/* call back to mid-layer using invocation thread */
	cmnd->result = scsi_result;
	cmnd->scsi_done(cmnd);
	return 0;
}

/* Note: The following macros create attribute files in the
   /sys/module/scsi_debug/parameters directory. Unfortunately this
   driver is unaware of a change and cannot trigger auxiliary actions
   as it can when the corresponding attribute in the
   /sys/bus/pseudo/drivers/scsi_debug directory is changed.
 */
module_param_named(add_host, sdebug_add_host, int, S_IRUGO | S_IWUSR);
module_param_named(ato, sdebug_ato, int, S_IRUGO);
module_param_named(clustering, sdebug_clustering, bool, S_IRUGO | S_IWUSR);
module_param_named(delay, sdebug_jdelay, int, S_IRUGO | S_IWUSR);
module_param_named(dev_size_mb, sdebug_dev_size_mb, int, S_IRUGO);
module_param_named(dif, sdebug_dif, int, S_IRUGO);
module_param_named(dix, sdebug_dix, int, S_IRUGO);
module_param_named(dsense, sdebug_dsense, int, S_IRUGO | S_IWUSR);
module_param_named(every_nth, sdebug_every_nth, int, S_IRUGO | S_IWUSR);
module_param_named(fake_rw, sdebug_fake_rw, int, S_IRUGO | S_IWUSR);
module_param_named(guard, sdebug_guard, uint, S_IRUGO);
module_param_named(host_lock, sdebug_host_lock, bool, S_IRUGO | S_IWUSR);
module_param_named(lbpu, sdebug_lbpu, int, S_IRUGO);
module_param_named(lbpws, sdebug_lbpws, int, S_IRUGO);
module_param_named(lbpws10, sdebug_lbpws10, int, S_IRUGO);
module_param_named(lbprz, sdebug_lbprz, int, S_IRUGO);
module_param_named(lowest_aligned, sdebug_lowest_aligned, int, S_IRUGO);
module_param_named(max_luns, sdebug_max_luns, int, S_IRUGO | S_IWUSR);
module_param_named(max_queue, sdebug_max_queue, int, S_IRUGO | S_IWUSR);
module_param_named(ndelay, sdebug_ndelay, int, S_IRUGO | S_IWUSR);
module_param_named(no_lun_0, sdebug_no_lun_0, int, S_IRUGO | S_IWUSR);
module_param_named(no_uld, sdebug_no_uld, int, S_IRUGO);
module_param_named(num_parts, sdebug_num_parts, int, S_IRUGO);
module_param_named(num_tgts, sdebug_num_tgts, int, S_IRUGO | S_IWUSR);
module_param_named(opt_blks, sdebug_opt_blks, int, S_IRUGO);
module_param_named(opts, sdebug_opts, int, S_IRUGO | S_IWUSR);
module_param_named(physblk_exp, sdebug_physblk_exp, int, S_IRUGO);
module_param_named(ptype, sdebug_ptype, int, S_IRUGO | S_IWUSR);
module_param_named(removable, sdebug_removable, bool, S_IRUGO | S_IWUSR);
module_param_named(scsi_level, sdebug_scsi_level, int, S_IRUGO);
module_param_named(sector_size, sdebug_sector_size, int, S_IRUGO);
module_param_named(statistics, sdebug_statistics, bool, S_IRUGO | S_IWUSR);
module_param_named(strict, sdebug_strict, bool, S_IRUGO | S_IWUSR);
module_param_named(submit_queues, submit_queues, int, S_IRUGO);
module_param_named(unmap_alignment, sdebug_unmap_alignment, int, S_IRUGO);
module_param_named(unmap_granularity, sdebug_unmap_granularity, int, S_IRUGO);
module_param_named(unmap_max_blocks, sdebug_unmap_max_blocks, int, S_IRUGO);
module_param_named(unmap_max_desc, sdebug_unmap_max_desc, int, S_IRUGO);
module_param_named(virtual_gb, sdebug_virtual_gb, int, S_IRUGO | S_IWUSR);
module_param_named(uuid_ctl, sdebug_uuid_ctl, int, S_IRUGO);
module_param_named(vpd_use_hostno, sdebug_vpd_use_hostno, int,
		   S_IRUGO | S_IWUSR);
module_param_named(write_same_length, sdebug_write_same_length, int,
		   S_IRUGO | S_IWUSR);

MODULE_AUTHOR("Eric Youngdale + Douglas Gilbert");
MODULE_DESCRIPTION("SCSI debug adapter driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(SDEBUG_VERSION);

MODULE_PARM_DESC(add_host, "0..127 hosts allowed(def=1)");
MODULE_PARM_DESC(ato, "application tag ownership: 0=disk 1=host (def=1)");
MODULE_PARM_DESC(clustering, "when set enables larger transfers (def=0)");
MODULE_PARM_DESC(delay, "response delay (def=1 jiffy); 0:imm, -1,-2:tiny");
MODULE_PARM_DESC(dev_size_mb, "size in MiB of ram shared by devs(def=8)");
MODULE_PARM_DESC(dif, "data integrity field type: 0-3 (def=0)");
MODULE_PARM_DESC(dix, "data integrity extensions mask (def=0)");
MODULE_PARM_DESC(dsense, "use descriptor sense format(def=0 -> fixed)");
MODULE_PARM_DESC(every_nth, "timeout every nth command(def=0)");
MODULE_PARM_DESC(fake_rw, "fake reads/writes instead of copying (def=0)");
MODULE_PARM_DESC(guard, "protection checksum: 0=crc, 1=ip (def=0)");
MODULE_PARM_DESC(host_lock, "host_lock is ignored (def=0)");
MODULE_PARM_DESC(lbpu, "enable LBP, support UNMAP command (def=0)");
MODULE_PARM_DESC(lbpws, "enable LBP, support WRITE SAME(16) with UNMAP bit (def=0)");
MODULE_PARM_DESC(lbpws10, "enable LBP, support WRITE SAME(10) with UNMAP bit (def=0)");
MODULE_PARM_DESC(lbprz,
	"on read unmapped LBs return 0 when 1 (def), return 0xff when 2");
MODULE_PARM_DESC(lowest_aligned, "lowest aligned lba (def=0)");
MODULE_PARM_DESC(max_luns, "number of LUNs per target to simulate(def=1)");
MODULE_PARM_DESC(max_queue, "max number of queued commands (1 to max(def))");
MODULE_PARM_DESC(ndelay, "response delay in nanoseconds (def=0 -> ignore)");
MODULE_PARM_DESC(no_lun_0, "no LU number 0 (def=0 -> have lun 0)");
MODULE_PARM_DESC(no_uld, "stop ULD (e.g. sd driver) attaching (def=0))");
MODULE_PARM_DESC(num_parts, "number of partitions(def=0)");
MODULE_PARM_DESC(num_tgts, "number of targets per host to simulate(def=1)");
MODULE_PARM_DESC(opt_blks, "optimal transfer length in blocks (def=1024)");
MODULE_PARM_DESC(opts, "1->noise, 2->medium_err, 4->timeout, 8->recovered_err... (def=0)");
MODULE_PARM_DESC(physblk_exp, "physical block exponent (def=0)");
MODULE_PARM_DESC(ptype, "SCSI peripheral type(def=0[disk])");
MODULE_PARM_DESC(removable, "claim to have removable media (def=0)");
MODULE_PARM_DESC(scsi_level, "SCSI level to simulate(def=7[SPC-5])");
MODULE_PARM_DESC(sector_size, "logical block size in bytes (def=512)");
MODULE_PARM_DESC(statistics, "collect statistics on commands, queues (def=0)");
MODULE_PARM_DESC(strict, "stricter checks: reserved field in cdb (def=0)");
MODULE_PARM_DESC(submit_queues, "support for block multi-queue (def=1)");
MODULE_PARM_DESC(unmap_alignment, "lowest aligned thin provisioning lba (def=0)");
MODULE_PARM_DESC(unmap_granularity, "thin provisioning granularity in blocks (def=1)");
MODULE_PARM_DESC(unmap_max_blocks, "max # of blocks can be unmapped in one cmd (def=0xffffffff)");
MODULE_PARM_DESC(unmap_max_desc, "max # of ranges that can be unmapped in one cmd (def=256)");
MODULE_PARM_DESC(uuid_ctl,
		 "1->use uuid for lu name, 0->don't, 2->all use same (def=0)");
MODULE_PARM_DESC(virtual_gb, "virtual gigabyte (GiB) size (def=0 -> use dev_size_mb)");
MODULE_PARM_DESC(vpd_use_hostno, "0 -> dev ids ignore hostno (def=1 -> unique dev ids)");
MODULE_PARM_DESC(write_same_length, "Maximum blocks per WRITE SAME cmd (def=0xffff)");

#define SDEBUG_INFO_LEN 256
static char sdebug_info[SDEBUG_INFO_LEN];

static const char * scsi_debug_info(struct Scsi_Host * shp)
{
	int k;

	k = scnprintf(sdebug_info, SDEBUG_INFO_LEN, "%s: version %s [%s]\n",
		      my_name, SDEBUG_VERSION, sdebug_version_date);
	if (k >= (SDEBUG_INFO_LEN - 1))
		return sdebug_info;
	scnprintf(sdebug_info + k, SDEBUG_INFO_LEN - k,
		  "  dev_size_mb=%d, opts=0x%x, submit_queues=%d, %s=%d",
		  sdebug_dev_size_mb, sdebug_opts, submit_queues,
		  "statistics", (int)sdebug_statistics);
	return sdebug_info;
}

/* 'echo <val> > /proc/scsi/scsi_debug/<host_id>' writes to opts */
static int scsi_debug_write_info(struct Scsi_Host *host, char *buffer,
				 int length)
{
	char arr[16];
	int opts;
	int minLen = length > 15 ? 15 : length;

	if (!capable(CAP_SYS_ADMIN) || !capable(CAP_SYS_RAWIO))
		return -EACCES;
	memcpy(arr, buffer, minLen);
	arr[minLen] = '\0';
	if (1 != sscanf(arr, "%d", &opts))
		return -EINVAL;
	sdebug_opts = opts;
	sdebug_verbose = !!(SDEBUG_OPT_NOISE & opts);
	sdebug_any_injecting_opt = !!(SDEBUG_OPT_ALL_INJECTING & opts);
	if (sdebug_every_nth != 0)
		tweak_cmnd_count();
	return length;
}

/* Output seen with 'cat /proc/scsi/scsi_debug/<host_id>'. It will be the
 * same for each scsi_debug host (if more than one). Some of the counters
 * output are not atomics so might be inaccurate in a busy system. */
static int scsi_debug_show_info(struct seq_file *m, struct Scsi_Host *host)
{
	int f, j, l;
	struct sdebug_queue *sqp;

	seq_printf(m, "scsi_debug adapter driver, version %s [%s]\n",
		   SDEBUG_VERSION, sdebug_version_date);
	seq_printf(m, "num_tgts=%d, %ssize=%d MB, opts=0x%x, every_nth=%d\n",
		   sdebug_num_tgts, "shared (ram) ", sdebug_dev_size_mb,
		   sdebug_opts, sdebug_every_nth);
	seq_printf(m, "delay=%d, ndelay=%d, max_luns=%d, sector_size=%d %s\n",
		   sdebug_jdelay, sdebug_ndelay, sdebug_max_luns,
		   sdebug_sector_size, "bytes");
	seq_printf(m, "cylinders=%d, heads=%d, sectors=%d, command aborts=%d\n",
		   sdebug_cylinders_per, sdebug_heads, sdebug_sectors_per,
		   num_aborts);
	seq_printf(m, "RESETs: device=%d, target=%d, bus=%d, host=%d\n",
		   num_dev_resets, num_target_resets, num_bus_resets,
		   num_host_resets);
	seq_printf(m, "dix_reads=%d, dix_writes=%d, dif_errors=%d\n",
		   dix_reads, dix_writes, dif_errors);
	seq_printf(m, "usec_in_jiffy=%lu, %s=%d, mq_active=%d\n",
		   TICK_NSEC / 1000, "statistics", sdebug_statistics,
		   sdebug_mq_active);
	seq_printf(m, "cmnd_count=%d, completions=%d, %s=%d, a_tsf=%d\n",
		   atomic_read(&sdebug_cmnd_count),
		   atomic_read(&sdebug_completions),
		   "miss_cpus", atomic_read(&sdebug_miss_cpus),
		   atomic_read(&sdebug_a_tsf));

	seq_printf(m, "submit_queues=%d\n", submit_queues);
	for (j = 0, sqp = sdebug_q_arr; j < submit_queues; ++j, ++sqp) {
		seq_printf(m, "  queue %d:\n", j);
		f = find_first_bit(sqp->in_use_bm, sdebug_max_queue);
		if (f != sdebug_max_queue) {
			l = find_last_bit(sqp->in_use_bm, sdebug_max_queue);
			seq_printf(m, "    in_use_bm BUSY: %s: %d,%d\n",
				   "first,last bits", f, l);
		}
	}
	return 0;
}

static ssize_t delay_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_jdelay);
}
/* Returns -EBUSY if jdelay is being changed and commands are queued. The unit
 * of delay is jiffies.
 */
static ssize_t delay_store(struct device_driver *ddp, const char *buf,
			   size_t count)
{
	int jdelay, res;

	if (count > 0 && sscanf(buf, "%d", &jdelay) == 1) {
		res = count;
		if (sdebug_jdelay != jdelay) {
			int j, k;
			struct sdebug_queue *sqp;

			block_unblock_all_queues(true);
			for (j = 0, sqp = sdebug_q_arr; j < submit_queues;
			     ++j, ++sqp) {
				k = find_first_bit(sqp->in_use_bm,
						   sdebug_max_queue);
				if (k != sdebug_max_queue) {
					res = -EBUSY;   /* queued commands */
					break;
				}
			}
			if (res > 0) {
				/* make sure sdebug_defer instances get
				 * re-allocated for new delay variant */
				free_all_queued();
				sdebug_jdelay = jdelay;
				sdebug_ndelay = 0;
			}
			block_unblock_all_queues(false);
		}
		return res;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(delay);

static ssize_t ndelay_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_ndelay);
}
/* Returns -EBUSY if ndelay is being changed and commands are queued */
/* If > 0 and accepted then sdebug_jdelay is set to JDELAY_OVERRIDDEN */
static ssize_t ndelay_store(struct device_driver *ddp, const char *buf,
			    size_t count)
{
	int ndelay, res;

	if ((count > 0) && (1 == sscanf(buf, "%d", &ndelay)) &&
	    (ndelay >= 0) && (ndelay < (1000 * 1000 * 1000))) {
		res = count;
		if (sdebug_ndelay != ndelay) {
			int j, k;
			struct sdebug_queue *sqp;

			block_unblock_all_queues(true);
			for (j = 0, sqp = sdebug_q_arr; j < submit_queues;
			     ++j, ++sqp) {
				k = find_first_bit(sqp->in_use_bm,
						   sdebug_max_queue);
				if (k != sdebug_max_queue) {
					res = -EBUSY;   /* queued commands */
					break;
				}
			}
			if (res > 0) {
				/* make sure sdebug_defer instances get
				 * re-allocated for new delay variant */
				free_all_queued();
				sdebug_ndelay = ndelay;
				sdebug_jdelay = ndelay  ? JDELAY_OVERRIDDEN
							: DEF_JDELAY;
			}
			block_unblock_all_queues(false);
		}
		return res;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(ndelay);

static ssize_t opts_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "0x%x\n", sdebug_opts);
}

static ssize_t opts_store(struct device_driver *ddp, const char *buf,
			  size_t count)
{
        int opts;
	char work[20];

        if (1 == sscanf(buf, "%10s", work)) {
		if (0 == strncasecmp(work,"0x", 2)) {
			if (1 == sscanf(&work[2], "%x", &opts))
				goto opts_done;
		} else {
			if (1 == sscanf(work, "%d", &opts))
				goto opts_done;
		}
	}
	return -EINVAL;
opts_done:
	sdebug_opts = opts;
	sdebug_verbose = !!(SDEBUG_OPT_NOISE & opts);
	sdebug_any_injecting_opt = !!(SDEBUG_OPT_ALL_INJECTING & opts);
	tweak_cmnd_count();
	return count;
}
static DRIVER_ATTR_RW(opts);

static ssize_t ptype_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_ptype);
}
static ssize_t ptype_store(struct device_driver *ddp, const char *buf,
			   size_t count)
{
        int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		sdebug_ptype = n;
		return count;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(ptype);

static ssize_t dsense_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_dsense);
}
static ssize_t dsense_store(struct device_driver *ddp, const char *buf,
			    size_t count)
{
        int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		sdebug_dsense = n;
		return count;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(dsense);

static ssize_t fake_rw_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_fake_rw);
}
static ssize_t fake_rw_store(struct device_driver *ddp, const char *buf,
			     size_t count)
{
        int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		n = (n > 0);
		sdebug_fake_rw = (sdebug_fake_rw > 0);
		if (sdebug_fake_rw != n) {
			if ((0 == n) && (NULL == fake_storep)) {
				unsigned long sz =
					(unsigned long)sdebug_dev_size_mb *
					1048576;

				fake_storep = vmalloc(sz);
				if (NULL == fake_storep) {
					pr_err("out of memory, 9\n");
					return -ENOMEM;
				}
				memset(fake_storep, 0, sz);
			}
			sdebug_fake_rw = n;
		}
		return count;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(fake_rw);

static ssize_t no_lun_0_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_no_lun_0);
}
static ssize_t no_lun_0_store(struct device_driver *ddp, const char *buf,
			      size_t count)
{
        int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		sdebug_no_lun_0 = n;
		return count;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(no_lun_0);

static ssize_t num_tgts_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_num_tgts);
}
static ssize_t num_tgts_store(struct device_driver *ddp, const char *buf,
			      size_t count)
{
        int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		sdebug_num_tgts = n;
		sdebug_max_tgts_luns();
		return count;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(num_tgts);

static ssize_t dev_size_mb_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_dev_size_mb);
}
static DRIVER_ATTR_RO(dev_size_mb);

static ssize_t num_parts_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_num_parts);
}
static DRIVER_ATTR_RO(num_parts);

static ssize_t every_nth_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_every_nth);
}
static ssize_t every_nth_store(struct device_driver *ddp, const char *buf,
			       size_t count)
{
        int nth;

	if ((count > 0) && (1 == sscanf(buf, "%d", &nth))) {
		sdebug_every_nth = nth;
		if (nth && !sdebug_statistics) {
			pr_info("every_nth needs statistics=1, set it\n");
			sdebug_statistics = true;
		}
		tweak_cmnd_count();
		return count;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(every_nth);

static ssize_t max_luns_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_max_luns);
}
static ssize_t max_luns_store(struct device_driver *ddp, const char *buf,
			      size_t count)
{
        int n;
	bool changed;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		if (n > 256) {
			pr_warn("max_luns can be no more than 256\n");
			return -EINVAL;
		}
		changed = (sdebug_max_luns != n);
		sdebug_max_luns = n;
		sdebug_max_tgts_luns();
		if (changed && (sdebug_scsi_level >= 5)) {	/* >= SPC-3 */
			struct sdebug_host_info *sdhp;
			struct sdebug_dev_info *dp;

			spin_lock(&sdebug_host_list_lock);
			list_for_each_entry(sdhp, &sdebug_host_list,
					    host_list) {
				list_for_each_entry(dp, &sdhp->dev_info_list,
						    dev_list) {
					set_bit(SDEBUG_UA_LUNS_CHANGED,
						dp->uas_bm);
				}
			}
			spin_unlock(&sdebug_host_list_lock);
		}
		return count;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(max_luns);

static ssize_t max_queue_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_max_queue);
}
/* N.B. max_queue can be changed while there are queued commands. In flight
 * commands beyond the new max_queue will be completed. */
static ssize_t max_queue_store(struct device_driver *ddp, const char *buf,
			       size_t count)
{
	int j, n, k, a;
	struct sdebug_queue *sqp;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n > 0) &&
	    (n <= SDEBUG_CANQUEUE)) {
		block_unblock_all_queues(true);
		k = 0;
		for (j = 0, sqp = sdebug_q_arr; j < submit_queues;
		     ++j, ++sqp) {
			a = find_last_bit(sqp->in_use_bm, SDEBUG_CANQUEUE);
			if (a > k)
				k = a;
		}
		sdebug_max_queue = n;
		if (k == SDEBUG_CANQUEUE)
			atomic_set(&retired_max_queue, 0);
		else if (k >= n)
			atomic_set(&retired_max_queue, k + 1);
		else
			atomic_set(&retired_max_queue, 0);
		block_unblock_all_queues(false);
		return count;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(max_queue);

static ssize_t no_uld_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_no_uld);
}
static DRIVER_ATTR_RO(no_uld);

static ssize_t scsi_level_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_scsi_level);
}
static DRIVER_ATTR_RO(scsi_level);

static ssize_t virtual_gb_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_virtual_gb);
}
static ssize_t virtual_gb_store(struct device_driver *ddp, const char *buf,
				size_t count)
{
        int n;
	bool changed;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		changed = (sdebug_virtual_gb != n);
		sdebug_virtual_gb = n;
		sdebug_capacity = get_sdebug_capacity();
		if (changed) {
			struct sdebug_host_info *sdhp;
			struct sdebug_dev_info *dp;

			spin_lock(&sdebug_host_list_lock);
			list_for_each_entry(sdhp, &sdebug_host_list,
					    host_list) {
				list_for_each_entry(dp, &sdhp->dev_info_list,
						    dev_list) {
					set_bit(SDEBUG_UA_CAPACITY_CHANGED,
						dp->uas_bm);
				}
			}
			spin_unlock(&sdebug_host_list_lock);
		}
		return count;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(virtual_gb);

static ssize_t add_host_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_add_host);
}

static int sdebug_add_adapter(void);
static void sdebug_remove_adapter(void);

static ssize_t add_host_store(struct device_driver *ddp, const char *buf,
			      size_t count)
{
	int delta_hosts;

	if (sscanf(buf, "%d", &delta_hosts) != 1)
		return -EINVAL;
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
static DRIVER_ATTR_RW(add_host);

static ssize_t vpd_use_hostno_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_vpd_use_hostno);
}
static ssize_t vpd_use_hostno_store(struct device_driver *ddp, const char *buf,
				    size_t count)
{
	int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		sdebug_vpd_use_hostno = n;
		return count;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(vpd_use_hostno);

static ssize_t statistics_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", (int)sdebug_statistics);
}
static ssize_t statistics_store(struct device_driver *ddp, const char *buf,
				size_t count)
{
	int n;

	if ((count > 0) && (sscanf(buf, "%d", &n) == 1) && (n >= 0)) {
		if (n > 0)
			sdebug_statistics = true;
		else {
			clear_queue_stats();
			sdebug_statistics = false;
		}
		return count;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(statistics);

static ssize_t sector_size_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", sdebug_sector_size);
}
static DRIVER_ATTR_RO(sector_size);

static ssize_t submit_queues_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", submit_queues);
}
static DRIVER_ATTR_RO(submit_queues);

static ssize_t dix_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_dix);
}
static DRIVER_ATTR_RO(dix);

static ssize_t dif_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_dif);
}
static DRIVER_ATTR_RO(dif);

static ssize_t guard_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n", sdebug_guard);
}
static DRIVER_ATTR_RO(guard);

static ssize_t ato_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_ato);
}
static DRIVER_ATTR_RO(ato);

static ssize_t map_show(struct device_driver *ddp, char *buf)
{
	ssize_t count;

	if (!scsi_debug_lbp())
		return scnprintf(buf, PAGE_SIZE, "0-%u\n",
				 sdebug_store_sectors);

	count = scnprintf(buf, PAGE_SIZE - 1, "%*pbl",
			  (int)map_size, map_storep);
	buf[count++] = '\n';
	buf[count] = '\0';

	return count;
}
static DRIVER_ATTR_RO(map);

static ssize_t removable_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", sdebug_removable ? 1 : 0);
}
static ssize_t removable_store(struct device_driver *ddp, const char *buf,
			       size_t count)
{
	int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		sdebug_removable = (n > 0);
		return count;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(removable);

static ssize_t host_lock_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", !!sdebug_host_lock);
}
/* N.B. sdebug_host_lock does nothing, kept for backward compatibility */
static ssize_t host_lock_store(struct device_driver *ddp, const char *buf,
			       size_t count)
{
	int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		sdebug_host_lock = (n > 0);
		return count;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(host_lock);

static ssize_t strict_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", !!sdebug_strict);
}
static ssize_t strict_store(struct device_driver *ddp, const char *buf,
			    size_t count)
{
	int n;

	if ((count > 0) && (1 == sscanf(buf, "%d", &n)) && (n >= 0)) {
		sdebug_strict = (n > 0);
		return count;
	}
	return -EINVAL;
}
static DRIVER_ATTR_RW(strict);

static ssize_t uuid_ctl_show(struct device_driver *ddp, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", !!sdebug_uuid_ctl);
}
static DRIVER_ATTR_RO(uuid_ctl);


/* Note: The following array creates attribute files in the
   /sys/bus/pseudo/drivers/scsi_debug directory. The advantage of these
   files (over those found in the /sys/module/scsi_debug/parameters
   directory) is that auxiliary actions can be triggered when an attribute
   is changed. For example see: sdebug_add_host_store() above.
 */

static struct attribute *sdebug_drv_attrs[] = {
	&driver_attr_delay.attr,
	&driver_attr_opts.attr,
	&driver_attr_ptype.attr,
	&driver_attr_dsense.attr,
	&driver_attr_fake_rw.attr,
	&driver_attr_no_lun_0.attr,
	&driver_attr_num_tgts.attr,
	&driver_attr_dev_size_mb.attr,
	&driver_attr_num_parts.attr,
	&driver_attr_every_nth.attr,
	&driver_attr_max_luns.attr,
	&driver_attr_max_queue.attr,
	&driver_attr_no_uld.attr,
	&driver_attr_scsi_level.attr,
	&driver_attr_virtual_gb.attr,
	&driver_attr_add_host.attr,
	&driver_attr_vpd_use_hostno.attr,
	&driver_attr_sector_size.attr,
	&driver_attr_statistics.attr,
	&driver_attr_submit_queues.attr,
	&driver_attr_dix.attr,
	&driver_attr_dif.attr,
	&driver_attr_guard.attr,
	&driver_attr_ato.attr,
	&driver_attr_map.attr,
	&driver_attr_removable.attr,
	&driver_attr_host_lock.attr,
	&driver_attr_ndelay.attr,
	&driver_attr_strict.attr,
	&driver_attr_uuid_ctl.attr,
	NULL,
};
ATTRIBUTE_GROUPS(sdebug_drv);

static struct device *pseudo_primary;

static int __init scsi_debug_init(void)
{
	unsigned long sz;
	int host_to_add;
	int k;
	int ret;

	atomic_set(&retired_max_queue, 0);

	if (sdebug_ndelay >= 1000 * 1000 * 1000) {
		pr_warn("ndelay must be less than 1 second, ignored\n");
		sdebug_ndelay = 0;
	} else if (sdebug_ndelay > 0)
		sdebug_jdelay = JDELAY_OVERRIDDEN;

	switch (sdebug_sector_size) {
	case  512:
	case 1024:
	case 2048:
	case 4096:
		break;
	default:
		pr_err("invalid sector_size %d\n", sdebug_sector_size);
		return -EINVAL;
	}

	switch (sdebug_dif) {
	case T10_PI_TYPE0_PROTECTION:
		break;
	case T10_PI_TYPE1_PROTECTION:
	case T10_PI_TYPE2_PROTECTION:
	case T10_PI_TYPE3_PROTECTION:
		have_dif_prot = true;
		break;

	default:
		pr_err("dif must be 0, 1, 2 or 3\n");
		return -EINVAL;
	}

	if (sdebug_guard > 1) {
		pr_err("guard must be 0 or 1\n");
		return -EINVAL;
	}

	if (sdebug_ato > 1) {
		pr_err("ato must be 0 or 1\n");
		return -EINVAL;
	}

	if (sdebug_physblk_exp > 15) {
		pr_err("invalid physblk_exp %u\n", sdebug_physblk_exp);
		return -EINVAL;
	}
	if (sdebug_max_luns > 256) {
		pr_warn("max_luns can be no more than 256, use default\n");
		sdebug_max_luns = DEF_MAX_LUNS;
	}

	if (sdebug_lowest_aligned > 0x3fff) {
		pr_err("lowest_aligned too big: %u\n", sdebug_lowest_aligned);
		return -EINVAL;
	}

	if (submit_queues < 1) {
		pr_err("submit_queues must be 1 or more\n");
		return -EINVAL;
	}
	sdebug_q_arr = kcalloc(submit_queues, sizeof(struct sdebug_queue),
			       GFP_KERNEL);
	if (sdebug_q_arr == NULL)
		return -ENOMEM;
	for (k = 0; k < submit_queues; ++k)
		spin_lock_init(&sdebug_q_arr[k].qc_lock);

	if (sdebug_dev_size_mb < 1)
		sdebug_dev_size_mb = 1;  /* force minimum 1 MB ramdisk */
	sz = (unsigned long)sdebug_dev_size_mb * 1048576;
	sdebug_store_sectors = sz / sdebug_sector_size;
	sdebug_capacity = get_sdebug_capacity();

	/* play around with geometry, don't waste too much on track 0 */
	sdebug_heads = 8;
	sdebug_sectors_per = 32;
	if (sdebug_dev_size_mb >= 256)
		sdebug_heads = 64;
	else if (sdebug_dev_size_mb >= 16)
		sdebug_heads = 32;
	sdebug_cylinders_per = (unsigned long)sdebug_capacity /
			       (sdebug_sectors_per * sdebug_heads);
	if (sdebug_cylinders_per >= 1024) {
		/* other LLDs do this; implies >= 1GB ram disk ... */
		sdebug_heads = 255;
		sdebug_sectors_per = 63;
		sdebug_cylinders_per = (unsigned long)sdebug_capacity /
			       (sdebug_sectors_per * sdebug_heads);
	}

	if (sdebug_fake_rw == 0) {
		fake_storep = vmalloc(sz);
		if (NULL == fake_storep) {
			pr_err("out of memory, 1\n");
			ret = -ENOMEM;
			goto free_q_arr;
		}
		memset(fake_storep, 0, sz);
		if (sdebug_num_parts > 0)
			sdebug_build_parts(fake_storep, sz);
	}

	if (sdebug_dix) {
		int dif_size;

		dif_size = sdebug_store_sectors * sizeof(struct t10_pi_tuple);
		dif_storep = vmalloc(dif_size);

		pr_err("dif_storep %u bytes @ %p\n", dif_size, dif_storep);

		if (dif_storep == NULL) {
			pr_err("out of mem. (DIX)\n");
			ret = -ENOMEM;
			goto free_vm;
		}

		memset(dif_storep, 0xff, dif_size);
	}

	/* Logical Block Provisioning */
	if (scsi_debug_lbp()) {
		sdebug_unmap_max_blocks =
			clamp(sdebug_unmap_max_blocks, 0U, 0xffffffffU);

		sdebug_unmap_max_desc =
			clamp(sdebug_unmap_max_desc, 0U, 256U);

		sdebug_unmap_granularity =
			clamp(sdebug_unmap_granularity, 1U, 0xffffffffU);

		if (sdebug_unmap_alignment &&
		    sdebug_unmap_granularity <=
		    sdebug_unmap_alignment) {
			pr_err("ERR: unmap_granularity <= unmap_alignment\n");
			ret = -EINVAL;
			goto free_vm;
		}

		map_size = lba_to_map_index(sdebug_store_sectors - 1) + 1;
		map_storep = vmalloc(BITS_TO_LONGS(map_size) * sizeof(long));

		pr_info("%lu provisioning blocks\n", map_size);

		if (map_storep == NULL) {
			pr_err("out of mem. (MAP)\n");
			ret = -ENOMEM;
			goto free_vm;
		}

		bitmap_zero(map_storep, map_size);

		/* Map first 1KB for partition table */
		if (sdebug_num_parts)
			map_region(0, 2);
	}

	pseudo_primary = root_device_register("pseudo_0");
	if (IS_ERR(pseudo_primary)) {
		pr_warn("root_device_register() error\n");
		ret = PTR_ERR(pseudo_primary);
		goto free_vm;
	}
	ret = bus_register(&pseudo_lld_bus);
	if (ret < 0) {
		pr_warn("bus_register error: %d\n", ret);
		goto dev_unreg;
	}
	ret = driver_register(&sdebug_driverfs_driver);
	if (ret < 0) {
		pr_warn("driver_register error: %d\n", ret);
		goto bus_unreg;
	}

	host_to_add = sdebug_add_host;
	sdebug_add_host = 0;

        for (k = 0; k < host_to_add; k++) {
                if (sdebug_add_adapter()) {
			pr_err("sdebug_add_adapter failed k=%d\n", k);
                        break;
                }
        }

	if (sdebug_verbose)
		pr_info("built %d host(s)\n", sdebug_add_host);

	return 0;

bus_unreg:
	bus_unregister(&pseudo_lld_bus);
dev_unreg:
	root_device_unregister(pseudo_primary);
free_vm:
	vfree(map_storep);
	vfree(dif_storep);
	vfree(fake_storep);
free_q_arr:
	kfree(sdebug_q_arr);
	return ret;
}

static void __exit scsi_debug_exit(void)
{
	int k = sdebug_add_host;

	stop_all_queued();
	free_all_queued();
	for (; k; k--)
		sdebug_remove_adapter();
	driver_unregister(&sdebug_driverfs_driver);
	bus_unregister(&pseudo_lld_bus);
	root_device_unregister(pseudo_primary);

	vfree(map_storep);
	vfree(dif_storep);
	vfree(fake_storep);
	kfree(sdebug_q_arr);
}

device_initcall(scsi_debug_init);
module_exit(scsi_debug_exit);

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
	struct sdebug_dev_info *sdbg_devinfo, *tmp;

        sdbg_host = kzalloc(sizeof(*sdbg_host),GFP_KERNEL);
        if (NULL == sdbg_host) {
		pr_err("out of memory at line %d\n", __LINE__);
                return -ENOMEM;
        }

        INIT_LIST_HEAD(&sdbg_host->dev_info_list);

	devs_per_host = sdebug_num_tgts * sdebug_max_luns;
        for (k = 0; k < devs_per_host; k++) {
		sdbg_devinfo = sdebug_device_create(sdbg_host, GFP_KERNEL);
		if (!sdbg_devinfo) {
			pr_err("out of memory at line %d\n", __LINE__);
                        error = -ENOMEM;
			goto clean;
                }
        }

        spin_lock(&sdebug_host_list_lock);
        list_add_tail(&sdbg_host->host_list, &sdebug_host_list);
        spin_unlock(&sdebug_host_list_lock);

        sdbg_host->dev.bus = &pseudo_lld_bus;
        sdbg_host->dev.parent = pseudo_primary;
        sdbg_host->dev.release = &sdebug_release_adapter;
	dev_set_name(&sdbg_host->dev, "adapter%d", sdebug_add_host);

        error = device_register(&sdbg_host->dev);

        if (error)
		goto clean;

	++sdebug_add_host;
        return error;

clean:
	list_for_each_entry_safe(sdbg_devinfo, tmp, &sdbg_host->dev_info_list,
				 dev_list) {
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
	--sdebug_add_host;
}

static int sdebug_change_qdepth(struct scsi_device *sdev, int qdepth)
{
	int num_in_q = 0;
	struct sdebug_dev_info *devip;

	block_unblock_all_queues(true);
	devip = (struct sdebug_dev_info *)sdev->hostdata;
	if (NULL == devip) {
		block_unblock_all_queues(false);
		return	-ENODEV;
	}
	num_in_q = atomic_read(&devip->num_in_q);

	if (qdepth < 1)
		qdepth = 1;
	/* allow to exceed max host qc_arr elements for testing */
	if (qdepth > SDEBUG_CANQUEUE + 10)
		qdepth = SDEBUG_CANQUEUE + 10;
	scsi_change_queue_depth(sdev, qdepth);

	if (SDEBUG_OPT_Q_NOISE & sdebug_opts) {
		sdev_printk(KERN_INFO, sdev, "%s: qdepth=%d, num_in_q=%d\n",
			    __func__, qdepth, num_in_q);
	}
	block_unblock_all_queues(false);
	return sdev->queue_depth;
}

static bool fake_timeout(struct scsi_cmnd *scp)
{
	if (0 == (atomic_read(&sdebug_cmnd_count) % abs(sdebug_every_nth))) {
		if (sdebug_every_nth < -1)
			sdebug_every_nth = -1;
		if (SDEBUG_OPT_TIMEOUT & sdebug_opts)
			return true; /* ignore command causing timeout */
		else if (SDEBUG_OPT_MAC_TIMEOUT & sdebug_opts &&
			 scsi_medium_access_command(scp))
			return true; /* time out reads and writes */
	}
	return false;
}

static int scsi_debug_queuecommand(struct Scsi_Host *shost,
				   struct scsi_cmnd *scp)
{
	u8 sdeb_i;
	struct scsi_device *sdp = scp->device;
	const struct opcode_info_t *oip;
	const struct opcode_info_t *r_oip;
	struct sdebug_dev_info *devip;
	u8 *cmd = scp->cmnd;
	int (*r_pfp)(struct scsi_cmnd *, struct sdebug_dev_info *);
	int k, na;
	int errsts = 0;
	u32 flags;
	u16 sa;
	u8 opcode = cmd[0];
	bool has_wlun_rl;

	scsi_set_resid(scp, 0);
	if (sdebug_statistics)
		atomic_inc(&sdebug_cmnd_count);
	if (unlikely(sdebug_verbose &&
		     !(SDEBUG_OPT_NO_CDB_NOISE & sdebug_opts))) {
		char b[120];
		int n, len, sb;

		len = scp->cmd_len;
		sb = (int)sizeof(b);
		if (len > 32)
			strcpy(b, "too long, over 32 bytes");
		else {
			for (k = 0, n = 0; k < len && n < sb; ++k)
				n += scnprintf(b + n, sb - n, "%02x ",
					       (u32)cmd[k]);
		}
		if (sdebug_mq_active)
			sdev_printk(KERN_INFO, sdp, "%s: tag=%u, cmd %s\n",
				    my_name, blk_mq_unique_tag(scp->request),
				    b);
		else
			sdev_printk(KERN_INFO, sdp, "%s: cmd %s\n", my_name,
				    b);
	}
	has_wlun_rl = (sdp->lun == SCSI_W_LUN_REPORT_LUNS);
	if (unlikely((sdp->lun >= sdebug_max_luns) && !has_wlun_rl))
		goto err_out;

	sdeb_i = opcode_ind_arr[opcode];	/* fully mapped */
	oip = &opcode_info_arr[sdeb_i];		/* safe if table consistent */
	devip = (struct sdebug_dev_info *)sdp->hostdata;
	if (unlikely(!devip)) {
		devip = find_build_dev_info(sdp);
		if (NULL == devip)
			goto err_out;
	}
	na = oip->num_attached;
	r_pfp = oip->pfp;
	if (na) {	/* multiple commands with this opcode */
		r_oip = oip;
		if (FF_SA & r_oip->flags) {
			if (F_SA_LOW & oip->flags)
				sa = 0x1f & cmd[1];
			else
				sa = get_unaligned_be16(cmd + 8);
			for (k = 0; k <= na; oip = r_oip->arrp + k++) {
				if (opcode == oip->opcode && sa == oip->sa)
					break;
			}
		} else {   /* since no service action only check opcode */
			for (k = 0; k <= na; oip = r_oip->arrp + k++) {
				if (opcode == oip->opcode)
					break;
			}
		}
		if (k > na) {
			if (F_SA_LOW & r_oip->flags)
				mk_sense_invalid_fld(scp, SDEB_IN_CDB, 1, 4);
			else if (F_SA_HIGH & r_oip->flags)
				mk_sense_invalid_fld(scp, SDEB_IN_CDB, 8, 7);
			else
				mk_sense_invalid_opcode(scp);
			goto check_cond;
		}
	}	/* else (when na==0) we assume the oip is a match */
	flags = oip->flags;
	if (unlikely(F_INV_OP & flags)) {
		mk_sense_invalid_opcode(scp);
		goto check_cond;
	}
	if (unlikely(has_wlun_rl && !(F_RL_WLUN_OK & flags))) {
		if (sdebug_verbose)
			sdev_printk(KERN_INFO, sdp, "%s: Opcode 0x%x not%s\n",
				    my_name, opcode, " supported for wlun");
		mk_sense_invalid_opcode(scp);
		goto check_cond;
	}
	if (unlikely(sdebug_strict)) {	/* check cdb against mask */
		u8 rem;
		int j;

		for (k = 1; k < oip->len_mask[0] && k < 16; ++k) {
			rem = ~oip->len_mask[k] & cmd[k];
			if (rem) {
				for (j = 7; j >= 0; --j, rem <<= 1) {
					if (0x80 & rem)
						break;
				}
				mk_sense_invalid_fld(scp, SDEB_IN_CDB, k, j);
				goto check_cond;
			}
		}
	}
	if (unlikely(!(F_SKIP_UA & flags) &&
		     find_first_bit(devip->uas_bm,
				    SDEBUG_NUM_UAS) != SDEBUG_NUM_UAS)) {
		errsts = make_ua(scp, devip);
		if (errsts)
			goto check_cond;
	}
	if (unlikely((F_M_ACCESS & flags) && atomic_read(&devip->stopped))) {
		mk_sense_buffer(scp, NOT_READY, LOGICAL_UNIT_NOT_READY, 0x2);
		if (sdebug_verbose)
			sdev_printk(KERN_INFO, sdp, "%s reports: Not ready: "
				    "%s\n", my_name, "initializing command "
				    "required");
		errsts = check_condition_result;
		goto fini;
	}
	if (sdebug_fake_rw && (F_FAKE_RW & flags))
		goto fini;
	if (unlikely(sdebug_every_nth)) {
		if (fake_timeout(scp))
			return 0;	/* ignore command: make trouble */
	}
	if (likely(oip->pfp))
		errsts = oip->pfp(scp, devip);	/* calls a resp_* function */
	else if (r_pfp)	/* if leaf function ptr NULL, try the root's */
		errsts = r_pfp(scp, devip);

fini:
	return schedule_resp(scp, devip, errsts,
			     ((F_DELAY_OVERR & flags) ? 0 : sdebug_jdelay));
check_cond:
	return schedule_resp(scp, devip, check_condition_result, 0);
err_out:
	return schedule_resp(scp, NULL, DID_NO_CONNECT << 16, 0);
}

static struct scsi_host_template sdebug_driver_template = {
	.show_info =		scsi_debug_show_info,
	.write_info =		scsi_debug_write_info,
	.proc_name =		sdebug_proc_name,
	.name =			"SCSI DEBUG",
	.info =			scsi_debug_info,
	.slave_alloc =		scsi_debug_slave_alloc,
	.slave_configure =	scsi_debug_slave_configure,
	.slave_destroy =	scsi_debug_slave_destroy,
	.ioctl =		scsi_debug_ioctl,
	.queuecommand =		scsi_debug_queuecommand,
	.change_queue_depth =	sdebug_change_qdepth,
	.eh_abort_handler =	scsi_debug_abort,
	.eh_device_reset_handler = scsi_debug_device_reset,
	.eh_target_reset_handler = scsi_debug_target_reset,
	.eh_bus_reset_handler = scsi_debug_bus_reset,
	.eh_host_reset_handler = scsi_debug_host_reset,
	.can_queue =		SDEBUG_CANQUEUE,
	.this_id =		7,
	.sg_tablesize =		SG_MAX_SEGMENTS,
	.cmd_per_lun =		DEF_CMD_PER_LUN,
	.max_sectors =		-1U,
	.use_clustering = 	DISABLE_CLUSTERING,
	.module =		THIS_MODULE,
	.track_queue_depth =	1,
};

static int sdebug_driver_probe(struct device * dev)
{
	int error = 0;
	struct sdebug_host_info *sdbg_host;
	struct Scsi_Host *hpnt;
	int hprot;

	sdbg_host = to_sdebug_host(dev);

	sdebug_driver_template.can_queue = sdebug_max_queue;
	if (sdebug_clustering)
		sdebug_driver_template.use_clustering = ENABLE_CLUSTERING;
	hpnt = scsi_host_alloc(&sdebug_driver_template, sizeof(sdbg_host));
	if (NULL == hpnt) {
		pr_err("scsi_host_alloc failed\n");
		error = -ENODEV;
		return error;
	}
	if (submit_queues > nr_cpu_ids) {
		pr_warn("%s: trim submit_queues (was %d) to nr_cpu_ids=%d\n",
			my_name, submit_queues, nr_cpu_ids);
		submit_queues = nr_cpu_ids;
	}
	/* Decide whether to tell scsi subsystem that we want mq */
	/* Following should give the same answer for each host */
	sdebug_mq_active = shost_use_blk_mq(hpnt) && (submit_queues > 1);
	if (sdebug_mq_active)
		hpnt->nr_hw_queues = submit_queues;

        sdbg_host->shost = hpnt;
	*((struct sdebug_host_info **)hpnt->hostdata) = sdbg_host;
	if ((hpnt->this_id >= 0) && (sdebug_num_tgts > hpnt->this_id))
		hpnt->max_id = sdebug_num_tgts + 1;
	else
		hpnt->max_id = sdebug_num_tgts;
	/* = sdebug_max_luns; */
	hpnt->max_lun = SCSI_W_LUN_REPORT_LUNS + 1;

	hprot = 0;

	switch (sdebug_dif) {

	case T10_PI_TYPE1_PROTECTION:
		hprot = SHOST_DIF_TYPE1_PROTECTION;
		if (sdebug_dix)
			hprot |= SHOST_DIX_TYPE1_PROTECTION;
		break;

	case T10_PI_TYPE2_PROTECTION:
		hprot = SHOST_DIF_TYPE2_PROTECTION;
		if (sdebug_dix)
			hprot |= SHOST_DIX_TYPE2_PROTECTION;
		break;

	case T10_PI_TYPE3_PROTECTION:
		hprot = SHOST_DIF_TYPE3_PROTECTION;
		if (sdebug_dix)
			hprot |= SHOST_DIX_TYPE3_PROTECTION;
		break;

	default:
		if (sdebug_dix)
			hprot |= SHOST_DIX_TYPE0_PROTECTION;
		break;
	}

	scsi_host_set_prot(hpnt, hprot);

	if (have_dif_prot || sdebug_dix)
		pr_info("host protection%s%s%s%s%s%s%s\n",
			(hprot & SHOST_DIF_TYPE1_PROTECTION) ? " DIF1" : "",
			(hprot & SHOST_DIF_TYPE2_PROTECTION) ? " DIF2" : "",
			(hprot & SHOST_DIF_TYPE3_PROTECTION) ? " DIF3" : "",
			(hprot & SHOST_DIX_TYPE0_PROTECTION) ? " DIX0" : "",
			(hprot & SHOST_DIX_TYPE1_PROTECTION) ? " DIX1" : "",
			(hprot & SHOST_DIX_TYPE2_PROTECTION) ? " DIX2" : "",
			(hprot & SHOST_DIX_TYPE3_PROTECTION) ? " DIX3" : "");

	if (sdebug_guard == 1)
		scsi_host_set_guard(hpnt, SHOST_DIX_GUARD_IP);
	else
		scsi_host_set_guard(hpnt, SHOST_DIX_GUARD_CRC);

	sdebug_verbose = !!(SDEBUG_OPT_NOISE & sdebug_opts);
	sdebug_any_injecting_opt = !!(SDEBUG_OPT_ALL_INJECTING & sdebug_opts);
	if (sdebug_every_nth)	/* need stats counters for every_nth */
		sdebug_statistics = true;
        error = scsi_add_host(hpnt, &sdbg_host->dev);
        if (error) {
		pr_err("scsi_add_host failed\n");
                error = -ENODEV;
		scsi_host_put(hpnt);
        } else
		scsi_scan_host(hpnt);

	return error;
}

static int sdebug_driver_remove(struct device * dev)
{
        struct sdebug_host_info *sdbg_host;
	struct sdebug_dev_info *sdbg_devinfo, *tmp;

	sdbg_host = to_sdebug_host(dev);

	if (!sdbg_host) {
		pr_err("Unable to locate host info\n");
		return -ENODEV;
	}

        scsi_remove_host(sdbg_host->shost);

	list_for_each_entry_safe(sdbg_devinfo, tmp, &sdbg_host->dev_info_list,
				 dev_list) {
                list_del(&sdbg_devinfo->dev_list);
                kfree(sdbg_devinfo);
        }

        scsi_host_put(sdbg_host->shost);
        return 0;
}

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
	.drv_groups = sdebug_drv_groups,
};
