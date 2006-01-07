/*
 * The low performance USB storage driver (ub).
 *
 * Copyright (c) 1999, 2000 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 * Copyright (C) 2004 Pete Zaitcev (zaitcev@yahoo.com)
 *
 * This work is a part of Linux kernel, is derived from it,
 * and is not licensed separately. See file COPYING for details.
 *
 * TODO (sorted by decreasing priority)
 *  -- Kill first_open (Al Viro fixed the block layer now)
 *  -- set readonly flag for CDs, set removable flag for CF readers
 *  -- do inquiry and verify we got a disk and not a tape (for LUN mismatch)
 *  -- special case some senses, e.g. 3a/0 -> no media present, reduce retries
 *  -- verify the 13 conditions and do bulk resets
 *  -- kill last_pipe and simply do two-state clearing on both pipes
 *  -- verify protocol (bulk) from USB descriptors (maybe...)
 *  -- highmem
 *  -- move top_sense and work_bcs into separate allocations (if they survive)
 *     for cache purists and esoteric architectures.
 *  -- Allocate structure for LUN 0 before the first ub_sync_tur, avoid NULL. ?
 *  -- prune comments, they are too volumnous
 *  -- Exterminate P3 printks
 *  -- Resove XXX's
 *  -- Redo "benh's retries", perhaps have spin-up code to handle them. V:D=?
 *  -- CLEAR, CLR2STS, CLRRS seem to be ripe for refactoring.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/usb_usual.h>
#include <linux/blkdev.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/timer.h>
#include <scsi/scsi.h>

#define DRV_NAME "ub"
#define DEVFS_NAME DRV_NAME

#define UB_MAJOR 180

/*
 * The command state machine is the key model for understanding of this driver.
 *
 * The general rule is that all transitions are done towards the bottom
 * of the diagram, thus preventing any loops.
 *
 * An exception to that is how the STAT state is handled. A counter allows it
 * to be re-entered along the path marked with [C].
 *
 *       +--------+
 *       ! INIT   !
 *       +--------+
 *           !
 *        ub_scsi_cmd_start fails ->--------------------------------------\
 *           !                                                            !
 *           V                                                            !
 *       +--------+                                                       !
 *       ! CMD    !                                                       !
 *       +--------+                                                       !
 *           !                                            +--------+      !
 *         was -EPIPE -->-------------------------------->! CLEAR  !      !
 *           !                                            +--------+      !
 *           !                                                !           !
 *         was error -->------------------------------------- ! --------->\
 *           !                                                !           !
 *  /--<-- cmd->dir == NONE ?                                 !           !
 *  !        !                                                !           !
 *  !        V                                                !           !
 *  !    +--------+                                           !           !
 *  !    ! DATA   !                                           !           !
 *  !    +--------+                                           !           !
 *  !        !                           +---------+          !           !
 *  !      was -EPIPE -->--------------->! CLR2STS !          !           !
 *  !        !                           +---------+          !           !
 *  !        !                                !               !           !
 *  !        !                              was error -->---- ! --------->\
 *  !      was error -->--------------------- ! ------------- ! --------->\
 *  !        !                                !               !           !
 *  !        V                                !               !           !
 *  \--->+--------+                           !               !           !
 *       ! STAT   !<--------------------------/               !           !
 *  /--->+--------+                                           !           !
 *  !        !                                                !           !
 * [C]     was -EPIPE -->-----------\                         !           !
 *  !        !                      !                         !           !
 *  +<---- len == 0                 !                         !           !
 *  !        !                      !                         !           !
 *  !      was error -->--------------------------------------!---------->\
 *  !        !                      !                         !           !
 *  +<---- bad CSW                  !                         !           !
 *  +<---- bad tag                  !                         !           !
 *  !        !                      V                         !           !
 *  !        !                 +--------+                     !           !
 *  !        !                 ! CLRRS  !                     !           !
 *  !        !                 +--------+                     !           !
 *  !        !                      !                         !           !
 *  \------- ! --------------------[C]--------\               !           !
 *           !                                !               !           !
 *         cmd->error---\                +--------+           !           !
 *           !          +--------------->! SENSE  !<----------/           !
 *         STAT_FAIL----/                +--------+                       !
 *           !                                !                           V
 *           !                                V                      +--------+
 *           \--------------------------------\--------------------->! DONE   !
 *                                                                   +--------+
 */

/*
 * This many LUNs per USB device.
 * Every one of them takes a host, see UB_MAX_HOSTS.
 */
#define UB_MAX_LUNS   9

/*
 */

#define UB_PARTS_PER_LUN      8

#define UB_MAX_CDB_SIZE      16		/* Corresponds to Bulk */

#define UB_SENSE_SIZE  18

/*
 */

/* command block wrapper */
struct bulk_cb_wrap {
	__le32	Signature;		/* contains 'USBC' */
	u32	Tag;			/* unique per command id */
	__le32	DataTransferLength;	/* size of data */
	u8	Flags;			/* direction in bit 0 */
	u8	Lun;			/* LUN */
	u8	Length;			/* of of the CDB */
	u8	CDB[UB_MAX_CDB_SIZE];	/* max command */
};

#define US_BULK_CB_WRAP_LEN	31
#define US_BULK_CB_SIGN		0x43425355	/*spells out USBC */
#define US_BULK_FLAG_IN		1
#define US_BULK_FLAG_OUT	0

/* command status wrapper */
struct bulk_cs_wrap {
	__le32	Signature;		/* should = 'USBS' */
	u32	Tag;			/* same as original command */
	__le32	Residue;		/* amount not transferred */
	u8	Status;			/* see below */
};

#define US_BULK_CS_WRAP_LEN	13
#define US_BULK_CS_SIGN		0x53425355	/* spells out 'USBS' */
#define US_BULK_STAT_OK		0
#define US_BULK_STAT_FAIL	1
#define US_BULK_STAT_PHASE	2

/* bulk-only class specific requests */
#define US_BULK_RESET_REQUEST	0xff
#define US_BULK_GET_MAX_LUN	0xfe

/*
 */
struct ub_dev;

#define UB_MAX_REQ_SG	9	/* cdrecord requires 32KB and maybe a header */
#define UB_MAX_SECTORS 64

/*
 * A second is more than enough for a 32K transfer (UB_MAX_SECTORS)
 * even if a webcam hogs the bus, but some devices need time to spin up.
 */
#define UB_URB_TIMEOUT	(HZ*2)
#define UB_DATA_TIMEOUT	(HZ*5)	/* ZIP does spin-ups in the data phase */
#define UB_STAT_TIMEOUT	(HZ*5)	/* Same spinups and eject for a dataless cmd. */
#define UB_CTRL_TIMEOUT	(HZ/2)	/* 500ms ought to be enough to clear a stall */

/*
 * An instance of a SCSI command in transit.
 */
#define UB_DIR_NONE	0
#define UB_DIR_READ	1
#define UB_DIR_ILLEGAL2	2
#define UB_DIR_WRITE	3

#define UB_DIR_CHAR(c)  (((c)==UB_DIR_WRITE)? 'w': \
			 (((c)==UB_DIR_READ)? 'r': 'n'))

enum ub_scsi_cmd_state {
	UB_CMDST_INIT,			/* Initial state */
	UB_CMDST_CMD,			/* Command submitted */
	UB_CMDST_DATA,			/* Data phase */
	UB_CMDST_CLR2STS,		/* Clearing before requesting status */
	UB_CMDST_STAT,			/* Status phase */
	UB_CMDST_CLEAR,			/* Clearing a stall (halt, actually) */
	UB_CMDST_CLRRS,			/* Clearing before retrying status */
	UB_CMDST_SENSE,			/* Sending Request Sense */
	UB_CMDST_DONE			/* Final state */
};

static char *ub_scsi_cmd_stname[] = {
	".  ",
	"Cmd",
	"dat",
	"c2s",
	"sts",
	"clr",
	"crs",
	"Sen",
	"fin"
};

struct ub_scsi_cmd {
	unsigned char cdb[UB_MAX_CDB_SIZE];
	unsigned char cdb_len;

	unsigned char dir;		/* 0 - none, 1 - read, 3 - write. */
	unsigned char trace_index;
	enum ub_scsi_cmd_state state;
	unsigned int tag;
	struct ub_scsi_cmd *next;

	int error;			/* Return code - valid upon done */
	unsigned int act_len;		/* Return size */
	unsigned char key, asc, ascq;	/* May be valid if error==-EIO */

	int stat_count;			/* Retries getting status. */

	unsigned int len;		/* Requested length */
	unsigned int current_sg;
	unsigned int nsg;		/* sgv[nsg] */
	struct scatterlist sgv[UB_MAX_REQ_SG];

	struct ub_lun *lun;
	void (*done)(struct ub_dev *, struct ub_scsi_cmd *);
	void *back;
};

struct ub_request {
	struct request *rq;
	unsigned int current_try;
	unsigned int nsg;		/* sgv[nsg] */
	struct scatterlist sgv[UB_MAX_REQ_SG];
};

/*
 */
struct ub_capacity {
	unsigned long nsec;		/* Linux size - 512 byte sectors */
	unsigned int bsize;		/* Linux hardsect_size */
	unsigned int bshift;		/* Shift between 512 and hard sects */
};

/*
 * The SCSI command tracing structure.
 */

#define SCMD_ST_HIST_SZ   8
#define SCMD_TRACE_SZ    63		/* Less than 4KB of 61-byte lines */

struct ub_scsi_cmd_trace {
	int hcur;
	unsigned int tag;
	unsigned int req_size, act_size;
	unsigned char op;
	unsigned char dir;
	unsigned char key, asc, ascq;
	char st_hst[SCMD_ST_HIST_SZ];	
};

struct ub_scsi_trace {
	int cur;
	struct ub_scsi_cmd_trace vec[SCMD_TRACE_SZ];
};

/*
 * This is a direct take-off from linux/include/completion.h
 * The difference is that I do not wait on this thing, just poll.
 * When I want to wait (ub_probe), I just use the stock completion.
 *
 * Note that INIT_COMPLETION takes no lock. It is correct. But why
 * in the bloody hell that thing takes struct instead of pointer to struct
 * is quite beyond me. I just copied it from the stock completion.
 */
struct ub_completion {
	unsigned int done;
	spinlock_t lock;
};

static inline void ub_init_completion(struct ub_completion *x)
{
	x->done = 0;
	spin_lock_init(&x->lock);
}

#define UB_INIT_COMPLETION(x)	((x).done = 0)

static void ub_complete(struct ub_completion *x)
{
	unsigned long flags;

	spin_lock_irqsave(&x->lock, flags);
	x->done++;
	spin_unlock_irqrestore(&x->lock, flags);
}

static int ub_is_completed(struct ub_completion *x)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&x->lock, flags);
	ret = x->done;
	spin_unlock_irqrestore(&x->lock, flags);
	return ret;
}

/*
 */
struct ub_scsi_cmd_queue {
	int qlen, qmax;
	struct ub_scsi_cmd *head, *tail;
};

/*
 * The block device instance (one per LUN).
 */
struct ub_lun {
	struct ub_dev *udev;
	struct list_head link;
	struct gendisk *disk;
	int id;				/* Host index */
	int num;			/* LUN number */
	char name[16];

	int changed;			/* Media was changed */
	int removable;
	int readonly;
	int first_open;			/* Kludge. See ub_bd_open. */

	struct ub_request urq;

	/* Use Ingo's mempool if or when we have more than one command. */
	/*
	 * Currently we never need more than one command for the whole device.
	 * However, giving every LUN a command is a cheap and automatic way
	 * to enforce fairness between them.
	 */
	int cmda[1];
	struct ub_scsi_cmd cmdv[1];

	struct ub_capacity capacity; 
};

/*
 * The USB device instance.
 */
struct ub_dev {
	spinlock_t lock;
	atomic_t poison;		/* The USB device is disconnected */
	int openc;			/* protected by ub_lock! */
					/* kref is too implicit for our taste */
	int reset;			/* Reset is running */
	unsigned int tagcnt;
	char name[12];
	struct usb_device *dev;
	struct usb_interface *intf;

	struct list_head luns;

	unsigned int send_bulk_pipe;	/* cached pipe values */
	unsigned int recv_bulk_pipe;
	unsigned int send_ctrl_pipe;
	unsigned int recv_ctrl_pipe;

	struct tasklet_struct tasklet;

	struct ub_scsi_cmd_queue cmd_queue;
	struct ub_scsi_cmd top_rqs_cmd;	/* REQUEST SENSE */
	unsigned char top_sense[UB_SENSE_SIZE];

	struct ub_completion work_done;
	struct urb work_urb;
	struct timer_list work_timer;
	int last_pipe;			/* What might need clearing */
	__le32 signature;		/* Learned signature */
	struct bulk_cb_wrap work_bcb;
	struct bulk_cs_wrap work_bcs;
	struct usb_ctrlrequest work_cr;

	struct work_struct reset_work;
	wait_queue_head_t reset_wait;

	int sg_stat[6];
	struct ub_scsi_trace tr;
};

/*
 */
static void ub_cleanup(struct ub_dev *sc);
static int ub_request_fn_1(struct ub_lun *lun, struct request *rq);
static void ub_cmd_build_block(struct ub_dev *sc, struct ub_lun *lun,
    struct ub_scsi_cmd *cmd, struct ub_request *urq);
static void ub_cmd_build_packet(struct ub_dev *sc, struct ub_lun *lun,
    struct ub_scsi_cmd *cmd, struct ub_request *urq);
static void ub_rw_cmd_done(struct ub_dev *sc, struct ub_scsi_cmd *cmd);
static void ub_end_rq(struct request *rq, int uptodate);
static int ub_rw_cmd_retry(struct ub_dev *sc, struct ub_lun *lun,
    struct ub_request *urq, struct ub_scsi_cmd *cmd);
static int ub_submit_scsi(struct ub_dev *sc, struct ub_scsi_cmd *cmd);
static void ub_urb_complete(struct urb *urb, struct pt_regs *pt);
static void ub_scsi_action(unsigned long _dev);
static void ub_scsi_dispatch(struct ub_dev *sc);
static void ub_scsi_urb_compl(struct ub_dev *sc, struct ub_scsi_cmd *cmd);
static void ub_data_start(struct ub_dev *sc, struct ub_scsi_cmd *cmd);
static void ub_state_done(struct ub_dev *sc, struct ub_scsi_cmd *cmd, int rc);
static int __ub_state_stat(struct ub_dev *sc, struct ub_scsi_cmd *cmd);
static void ub_state_stat(struct ub_dev *sc, struct ub_scsi_cmd *cmd);
static void ub_state_stat_counted(struct ub_dev *sc, struct ub_scsi_cmd *cmd);
static void ub_state_sense(struct ub_dev *sc, struct ub_scsi_cmd *cmd);
static int ub_submit_clear_stall(struct ub_dev *sc, struct ub_scsi_cmd *cmd,
    int stalled_pipe);
static void ub_top_sense_done(struct ub_dev *sc, struct ub_scsi_cmd *scmd);
static void ub_reset_enter(struct ub_dev *sc);
static void ub_reset_task(void *arg);
static int ub_sync_tur(struct ub_dev *sc, struct ub_lun *lun);
static int ub_sync_read_cap(struct ub_dev *sc, struct ub_lun *lun,
    struct ub_capacity *ret);
static int ub_probe_lun(struct ub_dev *sc, int lnum);

/*
 */
#ifdef CONFIG_USB_LIBUSUAL

#define ub_usb_ids  storage_usb_ids
#else

static struct usb_device_id ub_usb_ids[] = {
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_SCSI, US_PR_BULK) },
	{ }
};

MODULE_DEVICE_TABLE(usb, ub_usb_ids);
#endif /* CONFIG_USB_LIBUSUAL */

/*
 * Find me a way to identify "next free minor" for add_disk(),
 * and the array disappears the next day. However, the number of
 * hosts has something to do with the naming and /proc/partitions.
 * This has to be thought out in detail before changing.
 * If UB_MAX_HOST was 1000, we'd use a bitmap. Or a better data structure.
 */
#define UB_MAX_HOSTS  26
static char ub_hostv[UB_MAX_HOSTS];

static DEFINE_SPINLOCK(ub_lock);	/* Locks globals and ->openc */

/*
 * The SCSI command tracing procedures.
 */

static void ub_cmdtr_new(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	int n;
	struct ub_scsi_cmd_trace *t;

	if ((n = sc->tr.cur + 1) == SCMD_TRACE_SZ) n = 0;
	t = &sc->tr.vec[n];

	memset(t, 0, sizeof(struct ub_scsi_cmd_trace));
	t->tag = cmd->tag;
	t->op = cmd->cdb[0];
	t->dir = cmd->dir;
	t->req_size = cmd->len;
	t->st_hst[0] = cmd->state;

	sc->tr.cur = n;
	cmd->trace_index = n;
}

static void ub_cmdtr_state(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	int n;
	struct ub_scsi_cmd_trace *t;

	t = &sc->tr.vec[cmd->trace_index];
	if (t->tag == cmd->tag) {
		if ((n = t->hcur + 1) == SCMD_ST_HIST_SZ) n = 0;
		t->st_hst[n] = cmd->state;
		t->hcur = n;
	}
}

static void ub_cmdtr_act_len(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct ub_scsi_cmd_trace *t;

	t = &sc->tr.vec[cmd->trace_index];
	if (t->tag == cmd->tag)
		t->act_size = cmd->act_len;
}

static void ub_cmdtr_sense(struct ub_dev *sc, struct ub_scsi_cmd *cmd,
    unsigned char *sense)
{
	struct ub_scsi_cmd_trace *t;

	t = &sc->tr.vec[cmd->trace_index];
	if (t->tag == cmd->tag) {
		t->key = sense[2] & 0x0F;
		t->asc = sense[12];
		t->ascq = sense[13];
	}
}

static ssize_t ub_diag_show(struct device *dev, struct device_attribute *attr,
    char *page)
{
	struct usb_interface *intf;
	struct ub_dev *sc;
	struct list_head *p;
	struct ub_lun *lun;
	int cnt;
	unsigned long flags;
	int nc, nh;
	int i, j;
	struct ub_scsi_cmd_trace *t;

	intf = to_usb_interface(dev);
	sc = usb_get_intfdata(intf);
	if (sc == NULL)
		return 0;

	cnt = 0;
	spin_lock_irqsave(&sc->lock, flags);

	cnt += sprintf(page + cnt,
	    "poison %d reset %d\n",
	    atomic_read(&sc->poison), sc->reset);
	cnt += sprintf(page + cnt,
	    "qlen %d qmax %d\n",
	    sc->cmd_queue.qlen, sc->cmd_queue.qmax);
	cnt += sprintf(page + cnt,
	    "sg %d %d %d %d %d .. %d\n",
	    sc->sg_stat[0],
	    sc->sg_stat[1],
	    sc->sg_stat[2],
	    sc->sg_stat[3],
	    sc->sg_stat[4],
	    sc->sg_stat[5]);

	list_for_each (p, &sc->luns) {
		lun = list_entry(p, struct ub_lun, link);
		cnt += sprintf(page + cnt,
		    "lun %u changed %d removable %d readonly %d\n",
		    lun->num, lun->changed, lun->removable, lun->readonly);
	}

	if ((nc = sc->tr.cur + 1) == SCMD_TRACE_SZ) nc = 0;
	for (j = 0; j < SCMD_TRACE_SZ; j++) {
		t = &sc->tr.vec[nc];

		cnt += sprintf(page + cnt, "%08x %02x", t->tag, t->op);
		if (t->op == REQUEST_SENSE) {
			cnt += sprintf(page + cnt, " [sense %x %02x %02x]",
					t->key, t->asc, t->ascq);
		} else {
			cnt += sprintf(page + cnt, " %c", UB_DIR_CHAR(t->dir));
			cnt += sprintf(page + cnt, " [%5d %5d]",
					t->req_size, t->act_size);
		}
		if ((nh = t->hcur + 1) == SCMD_ST_HIST_SZ) nh = 0;
		for (i = 0; i < SCMD_ST_HIST_SZ; i++) {
			cnt += sprintf(page + cnt, " %s",
					ub_scsi_cmd_stname[(int)t->st_hst[nh]]);
			if (++nh == SCMD_ST_HIST_SZ) nh = 0;
		}
		cnt += sprintf(page + cnt, "\n");

		if (++nc == SCMD_TRACE_SZ) nc = 0;
	}

	spin_unlock_irqrestore(&sc->lock, flags);
	return cnt;
}

static DEVICE_ATTR(diag, S_IRUGO, ub_diag_show, NULL); /* N.B. World readable */

/*
 * The id allocator.
 *
 * This also stores the host for indexing by minor, which is somewhat dirty.
 */
static int ub_id_get(void)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&ub_lock, flags);
	for (i = 0; i < UB_MAX_HOSTS; i++) {
		if (ub_hostv[i] == 0) {
			ub_hostv[i] = 1;
			spin_unlock_irqrestore(&ub_lock, flags);
			return i;
		}
	}
	spin_unlock_irqrestore(&ub_lock, flags);
	return -1;
}

static void ub_id_put(int id)
{
	unsigned long flags;

	if (id < 0 || id >= UB_MAX_HOSTS) {
		printk(KERN_ERR DRV_NAME ": bad host ID %d\n", id);
		return;
	}

	spin_lock_irqsave(&ub_lock, flags);
	if (ub_hostv[id] == 0) {
		spin_unlock_irqrestore(&ub_lock, flags);
		printk(KERN_ERR DRV_NAME ": freeing free host ID %d\n", id);
		return;
	}
	ub_hostv[id] = 0;
	spin_unlock_irqrestore(&ub_lock, flags);
}

/*
 * Downcount for deallocation. This rides on two assumptions:
 *  - once something is poisoned, its refcount cannot grow
 *  - opens cannot happen at this time (del_gendisk was done)
 * If the above is true, we can drop the lock, which we need for
 * blk_cleanup_queue(): the silly thing may attempt to sleep.
 * [Actually, it never needs to sleep for us, but it calls might_sleep()]
 */
static void ub_put(struct ub_dev *sc)
{
	unsigned long flags;

	spin_lock_irqsave(&ub_lock, flags);
	--sc->openc;
	if (sc->openc == 0 && atomic_read(&sc->poison)) {
		spin_unlock_irqrestore(&ub_lock, flags);
		ub_cleanup(sc);
	} else {
		spin_unlock_irqrestore(&ub_lock, flags);
	}
}

/*
 * Final cleanup and deallocation.
 */
static void ub_cleanup(struct ub_dev *sc)
{
	struct list_head *p;
	struct ub_lun *lun;
	request_queue_t *q;

	while (!list_empty(&sc->luns)) {
		p = sc->luns.next;
		lun = list_entry(p, struct ub_lun, link);
		list_del(p);

		/* I don't think queue can be NULL. But... Stolen from sx8.c */
		if ((q = lun->disk->queue) != NULL)
			blk_cleanup_queue(q);
		/*
		 * If we zero disk->private_data BEFORE put_disk, we have
		 * to check for NULL all over the place in open, release,
		 * check_media and revalidate, because the block level
		 * semaphore is well inside the put_disk.
		 * But we cannot zero after the call, because *disk is gone.
		 * The sd.c is blatantly racy in this area.
		 */
		/* disk->private_data = NULL; */
		put_disk(lun->disk);
		lun->disk = NULL;

		ub_id_put(lun->id);
		kfree(lun);
	}

	kfree(sc);
}

/*
 * The "command allocator".
 */
static struct ub_scsi_cmd *ub_get_cmd(struct ub_lun *lun)
{
	struct ub_scsi_cmd *ret;

	if (lun->cmda[0])
		return NULL;
	ret = &lun->cmdv[0];
	lun->cmda[0] = 1;
	return ret;
}

static void ub_put_cmd(struct ub_lun *lun, struct ub_scsi_cmd *cmd)
{
	if (cmd != &lun->cmdv[0]) {
		printk(KERN_WARNING "%s: releasing a foreign cmd %p\n",
		    lun->name, cmd);
		return;
	}
	if (!lun->cmda[0]) {
		printk(KERN_WARNING "%s: releasing a free cmd\n", lun->name);
		return;
	}
	lun->cmda[0] = 0;
}

/*
 * The command queue.
 */
static void ub_cmdq_add(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct ub_scsi_cmd_queue *t = &sc->cmd_queue;

	if (t->qlen++ == 0) {
		t->head = cmd;
		t->tail = cmd;
	} else {
		t->tail->next = cmd;
		t->tail = cmd;
	}

	if (t->qlen > t->qmax)
		t->qmax = t->qlen;
}

static void ub_cmdq_insert(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct ub_scsi_cmd_queue *t = &sc->cmd_queue;

	if (t->qlen++ == 0) {
		t->head = cmd;
		t->tail = cmd;
	} else {
		cmd->next = t->head;
		t->head = cmd;
	}

	if (t->qlen > t->qmax)
		t->qmax = t->qlen;
}

static struct ub_scsi_cmd *ub_cmdq_pop(struct ub_dev *sc)
{
	struct ub_scsi_cmd_queue *t = &sc->cmd_queue;
	struct ub_scsi_cmd *cmd;

	if (t->qlen == 0)
		return NULL;
	if (--t->qlen == 0)
		t->tail = NULL;
	cmd = t->head;
	t->head = cmd->next;
	cmd->next = NULL;
	return cmd;
}

#define ub_cmdq_peek(sc)  ((sc)->cmd_queue.head)

/*
 * The request function is our main entry point
 */

static void ub_request_fn(request_queue_t *q)
{
	struct ub_lun *lun = q->queuedata;
	struct request *rq;

	while ((rq = elv_next_request(q)) != NULL) {
		if (ub_request_fn_1(lun, rq) != 0) {
			blk_stop_queue(q);
			break;
		}
	}
}

static int ub_request_fn_1(struct ub_lun *lun, struct request *rq)
{
	struct ub_dev *sc = lun->udev;
	struct ub_scsi_cmd *cmd;
	struct ub_request *urq;
	int n_elem;

	if (atomic_read(&sc->poison) || lun->changed) {
		blkdev_dequeue_request(rq);
		ub_end_rq(rq, 0);
		return 0;
	}

	if (lun->urq.rq != NULL)
		return -1;
	if ((cmd = ub_get_cmd(lun)) == NULL)
		return -1;
	memset(cmd, 0, sizeof(struct ub_scsi_cmd));

	blkdev_dequeue_request(rq);

	urq = &lun->urq;
	memset(urq, 0, sizeof(struct ub_request));
	urq->rq = rq;

	/*
	 * get scatterlist from block layer
	 */
	n_elem = blk_rq_map_sg(lun->disk->queue, rq, &urq->sgv[0]);
	if (n_elem < 0) {
		printk(KERN_INFO "%s: failed request map (%d)\n",
		    lun->name, n_elem); /* P3 */
		goto drop;
	}
	if (n_elem > UB_MAX_REQ_SG) {	/* Paranoia */
		printk(KERN_WARNING "%s: request with %d segments\n",
		    lun->name, n_elem);
		goto drop;
	}
	urq->nsg = n_elem;
	sc->sg_stat[n_elem < 5 ? n_elem : 5]++;

	if (blk_pc_request(rq)) {
		ub_cmd_build_packet(sc, lun, cmd, urq);
	} else {
		ub_cmd_build_block(sc, lun, cmd, urq);
	}
	cmd->state = UB_CMDST_INIT;
	cmd->lun = lun;
	cmd->done = ub_rw_cmd_done;
	cmd->back = urq;

	cmd->tag = sc->tagcnt++;
	if (ub_submit_scsi(sc, cmd) != 0)
		goto drop;

	return 0;

drop:
	ub_put_cmd(lun, cmd);
	ub_end_rq(rq, 0);
	return 0;
}

static void ub_cmd_build_block(struct ub_dev *sc, struct ub_lun *lun,
    struct ub_scsi_cmd *cmd, struct ub_request *urq)
{
	struct request *rq = urq->rq;
	unsigned int block, nblks;

	if (rq_data_dir(rq) == WRITE)
		cmd->dir = UB_DIR_WRITE;
	else
		cmd->dir = UB_DIR_READ;

	cmd->nsg = urq->nsg;
	memcpy(cmd->sgv, urq->sgv, sizeof(struct scatterlist) * cmd->nsg);

	/*
	 * build the command
	 *
	 * The call to blk_queue_hardsect_size() guarantees that request
	 * is aligned, but it is given in terms of 512 byte units, always.
	 */
	block = rq->sector >> lun->capacity.bshift;
	nblks = rq->nr_sectors >> lun->capacity.bshift;

	cmd->cdb[0] = (cmd->dir == UB_DIR_READ)? READ_10: WRITE_10;
	/* 10-byte uses 4 bytes of LBA: 2147483648KB, 2097152MB, 2048GB */
	cmd->cdb[2] = block >> 24;
	cmd->cdb[3] = block >> 16;
	cmd->cdb[4] = block >> 8;
	cmd->cdb[5] = block;
	cmd->cdb[7] = nblks >> 8;
	cmd->cdb[8] = nblks;
	cmd->cdb_len = 10;

	cmd->len = rq->nr_sectors * 512;
}

static void ub_cmd_build_packet(struct ub_dev *sc, struct ub_lun *lun,
    struct ub_scsi_cmd *cmd, struct ub_request *urq)
{
	struct request *rq = urq->rq;

	if (rq->data_len == 0) {
		cmd->dir = UB_DIR_NONE;
	} else {
		if (rq_data_dir(rq) == WRITE)
			cmd->dir = UB_DIR_WRITE;
		else
			cmd->dir = UB_DIR_READ;
	}

	cmd->nsg = urq->nsg;
	memcpy(cmd->sgv, urq->sgv, sizeof(struct scatterlist) * cmd->nsg);

	memcpy(&cmd->cdb, rq->cmd, rq->cmd_len);
	cmd->cdb_len = rq->cmd_len;

	cmd->len = rq->data_len;
}

static void ub_rw_cmd_done(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct ub_lun *lun = cmd->lun;
	struct ub_request *urq = cmd->back;
	struct request *rq;
	int uptodate;

	rq = urq->rq;

	if (cmd->error == 0) {
		uptodate = 1;

		if (blk_pc_request(rq)) {
			if (cmd->act_len >= rq->data_len)
				rq->data_len = 0;
			else
				rq->data_len -= cmd->act_len;
		}
	} else {
		uptodate = 0;

		if (blk_pc_request(rq)) {
			/* UB_SENSE_SIZE is smaller than SCSI_SENSE_BUFFERSIZE */
			memcpy(rq->sense, sc->top_sense, UB_SENSE_SIZE);
			rq->sense_len = UB_SENSE_SIZE;
			if (sc->top_sense[0] != 0)
				rq->errors = SAM_STAT_CHECK_CONDITION;
			else
				rq->errors = DID_ERROR << 16;
		} else {
			if (cmd->error == -EIO) {
				if (ub_rw_cmd_retry(sc, lun, urq, cmd) == 0)
					return;
			}
		}
	}

	urq->rq = NULL;

	ub_put_cmd(lun, cmd);
	ub_end_rq(rq, uptodate);
	blk_start_queue(lun->disk->queue);
}

static void ub_end_rq(struct request *rq, int uptodate)
{
	end_that_request_first(rq, uptodate, rq->hard_nr_sectors);
	end_that_request_last(rq, uptodate);
}

static int ub_rw_cmd_retry(struct ub_dev *sc, struct ub_lun *lun,
    struct ub_request *urq, struct ub_scsi_cmd *cmd)
{

	if (atomic_read(&sc->poison))
		return -ENXIO;

	ub_reset_enter(sc);

	if (urq->current_try >= 3)
		return -EIO;
	urq->current_try++;
	/* P3 */ printk("%s: dir %c len/act %d/%d "
	    "[sense %x %02x %02x] retry %d\n",
	    sc->name, UB_DIR_CHAR(cmd->dir), cmd->len, cmd->act_len,
	    cmd->key, cmd->asc, cmd->ascq, urq->current_try);

	memset(cmd, 0, sizeof(struct ub_scsi_cmd));
	ub_cmd_build_block(sc, lun, cmd, urq);

	cmd->state = UB_CMDST_INIT;
	cmd->lun = lun;
	cmd->done = ub_rw_cmd_done;
	cmd->back = urq;

	cmd->tag = sc->tagcnt++;

#if 0 /* Wasteful */
	return ub_submit_scsi(sc, cmd);
#else
	ub_cmdq_add(sc, cmd);
	return 0;
#endif
}

/*
 * Submit a regular SCSI operation (not an auto-sense).
 *
 * The Iron Law of Good Submit Routine is:
 * Zero return - callback is done, Nonzero return - callback is not done.
 * No exceptions.
 *
 * Host is assumed locked.
 *
 * XXX We only support Bulk for the moment.
 */
static int ub_submit_scsi(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{

	if (cmd->state != UB_CMDST_INIT ||
	    (cmd->dir != UB_DIR_NONE && cmd->len == 0)) {
		return -EINVAL;
	}

	ub_cmdq_add(sc, cmd);
	/*
	 * We can call ub_scsi_dispatch(sc) right away here, but it's a little
	 * safer to jump to a tasklet, in case upper layers do something silly.
	 */
	tasklet_schedule(&sc->tasklet);
	return 0;
}

/*
 * Submit the first URB for the queued command.
 * This function does not deal with queueing in any way.
 */
static int ub_scsi_cmd_start(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct bulk_cb_wrap *bcb;
	int rc;

	bcb = &sc->work_bcb;

	/*
	 * ``If the allocation length is eighteen or greater, and a device
	 * server returns less than eithteen bytes of data, the application
	 * client should assume that the bytes not transferred would have been
	 * zeroes had the device server returned those bytes.''
	 *
	 * We zero sense for all commands so that when a packet request
	 * fails it does not return a stale sense.
	 */
	memset(&sc->top_sense, 0, UB_SENSE_SIZE);

	/* set up the command wrapper */
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->Tag = cmd->tag;		/* Endianness is not important */
	bcb->DataTransferLength = cpu_to_le32(cmd->len);
	bcb->Flags = (cmd->dir == UB_DIR_READ) ? 0x80 : 0;
	bcb->Lun = (cmd->lun != NULL) ? cmd->lun->num : 0;
	bcb->Length = cmd->cdb_len;

	/* copy the command payload */
	memcpy(bcb->CDB, cmd->cdb, UB_MAX_CDB_SIZE);

	UB_INIT_COMPLETION(sc->work_done);

	sc->last_pipe = sc->send_bulk_pipe;
	usb_fill_bulk_urb(&sc->work_urb, sc->dev, sc->send_bulk_pipe,
	    bcb, US_BULK_CB_WRAP_LEN, ub_urb_complete, sc);

	/* Fill what we shouldn't be filling, because usb-storage did so. */
	sc->work_urb.actual_length = 0;
	sc->work_urb.error_count = 0;
	sc->work_urb.status = 0;

	if ((rc = usb_submit_urb(&sc->work_urb, GFP_ATOMIC)) != 0) {
		/* XXX Clear stalls */
		ub_complete(&sc->work_done);
		return rc;
	}

	sc->work_timer.expires = jiffies + UB_URB_TIMEOUT;
	add_timer(&sc->work_timer);

	cmd->state = UB_CMDST_CMD;
	ub_cmdtr_state(sc, cmd);
	return 0;
}

/*
 * Timeout handler.
 */
static void ub_urb_timeout(unsigned long arg)
{
	struct ub_dev *sc = (struct ub_dev *) arg;
	unsigned long flags;

	spin_lock_irqsave(&sc->lock, flags);
	usb_unlink_urb(&sc->work_urb);
	spin_unlock_irqrestore(&sc->lock, flags);
}

/*
 * Completion routine for the work URB.
 *
 * This can be called directly from usb_submit_urb (while we have
 * the sc->lock taken) and from an interrupt (while we do NOT have
 * the sc->lock taken). Therefore, bounce this off to a tasklet.
 */
static void ub_urb_complete(struct urb *urb, struct pt_regs *pt)
{
	struct ub_dev *sc = urb->context;

	ub_complete(&sc->work_done);
	tasklet_schedule(&sc->tasklet);
}

static void ub_scsi_action(unsigned long _dev)
{
	struct ub_dev *sc = (struct ub_dev *) _dev;
	unsigned long flags;

	spin_lock_irqsave(&sc->lock, flags);
	del_timer(&sc->work_timer);
	ub_scsi_dispatch(sc);
	spin_unlock_irqrestore(&sc->lock, flags);
}

static void ub_scsi_dispatch(struct ub_dev *sc)
{
	struct ub_scsi_cmd *cmd;
	int rc;

	while (!sc->reset && (cmd = ub_cmdq_peek(sc)) != NULL) {
		if (cmd->state == UB_CMDST_DONE) {
			ub_cmdq_pop(sc);
			(*cmd->done)(sc, cmd);
		} else if (cmd->state == UB_CMDST_INIT) {
			ub_cmdtr_new(sc, cmd);
			if ((rc = ub_scsi_cmd_start(sc, cmd)) == 0)
				break;
			cmd->error = rc;
			cmd->state = UB_CMDST_DONE;
			ub_cmdtr_state(sc, cmd);
		} else {
			if (!ub_is_completed(&sc->work_done))
				break;
			ub_scsi_urb_compl(sc, cmd);
		}
	}
}

static void ub_scsi_urb_compl(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct urb *urb = &sc->work_urb;
	struct bulk_cs_wrap *bcs;
	int len;
	int rc;

	if (atomic_read(&sc->poison)) {
		ub_state_done(sc, cmd, -ENODEV);
		return;
	}

	if (cmd->state == UB_CMDST_CLEAR) {
		if (urb->status == -EPIPE) {
			/*
			 * STALL while clearning STALL.
			 * The control pipe clears itself - nothing to do.
			 */
			printk(KERN_NOTICE "%s: stall on control pipe\n",
			    sc->name);
			goto Bad_End;
		}

		/*
		 * We ignore the result for the halt clear.
		 */

		/* reset the endpoint toggle */
		usb_settoggle(sc->dev, usb_pipeendpoint(sc->last_pipe),
			usb_pipeout(sc->last_pipe), 0);

		ub_state_sense(sc, cmd);

	} else if (cmd->state == UB_CMDST_CLR2STS) {
		if (urb->status == -EPIPE) {
			printk(KERN_NOTICE "%s: stall on control pipe\n",
			    sc->name);
			goto Bad_End;
		}

		/*
		 * We ignore the result for the halt clear.
		 */

		/* reset the endpoint toggle */
		usb_settoggle(sc->dev, usb_pipeendpoint(sc->last_pipe),
			usb_pipeout(sc->last_pipe), 0);

		ub_state_stat(sc, cmd);

	} else if (cmd->state == UB_CMDST_CLRRS) {
		if (urb->status == -EPIPE) {
			printk(KERN_NOTICE "%s: stall on control pipe\n",
			    sc->name);
			goto Bad_End;
		}

		/*
		 * We ignore the result for the halt clear.
		 */

		/* reset the endpoint toggle */
		usb_settoggle(sc->dev, usb_pipeendpoint(sc->last_pipe),
			usb_pipeout(sc->last_pipe), 0);

		ub_state_stat_counted(sc, cmd);

	} else if (cmd->state == UB_CMDST_CMD) {
		switch (urb->status) {
		case 0:
			break;
		case -EOVERFLOW:
			goto Bad_End;
		case -EPIPE:
			rc = ub_submit_clear_stall(sc, cmd, sc->last_pipe);
			if (rc != 0) {
				printk(KERN_NOTICE "%s: "
				    "unable to submit clear (%d)\n",
				    sc->name, rc);
				/*
				 * This is typically ENOMEM or some other such shit.
				 * Retrying is pointless. Just do Bad End on it...
				 */
				ub_state_done(sc, cmd, rc);
				return;
			}
			cmd->state = UB_CMDST_CLEAR;
			ub_cmdtr_state(sc, cmd);
			return;
		case -ESHUTDOWN:	/* unplug */
		case -EILSEQ:		/* unplug timeout on uhci */
			ub_state_done(sc, cmd, -ENODEV);
			return;
		default:
			goto Bad_End;
		}
		if (urb->actual_length != US_BULK_CB_WRAP_LEN) {
			goto Bad_End;
		}

		if (cmd->dir == UB_DIR_NONE || cmd->nsg < 1) {
			ub_state_stat(sc, cmd);
			return;
		}

		// udelay(125);		// usb-storage has this
		ub_data_start(sc, cmd);

	} else if (cmd->state == UB_CMDST_DATA) {
		if (urb->status == -EPIPE) {
			rc = ub_submit_clear_stall(sc, cmd, sc->last_pipe);
			if (rc != 0) {
				printk(KERN_NOTICE "%s: "
				    "unable to submit clear (%d)\n",
				    sc->name, rc);
				ub_state_done(sc, cmd, rc);
				return;
			}
			cmd->state = UB_CMDST_CLR2STS;
			ub_cmdtr_state(sc, cmd);
			return;
		}
		if (urb->status == -EOVERFLOW) {
			/*
			 * A babble? Failure, but we must transfer CSW now.
			 */
			cmd->error = -EOVERFLOW;	/* A cheap trick... */
			ub_state_stat(sc, cmd);
			return;
		}

		if (cmd->dir == UB_DIR_WRITE) {
			/*
			 * Do not continue writes in case of a failure.
			 * Doing so would cause sectors to be mixed up,
			 * which is worse than sectors lost.
			 *
			 * We must try to read the CSW, or many devices
			 * get confused.
			 */
			len = urb->actual_length;
			if (urb->status != 0 ||
			    len != cmd->sgv[cmd->current_sg].length) {
				cmd->act_len += len;
				ub_cmdtr_act_len(sc, cmd);

				cmd->error = -EIO;
				ub_state_stat(sc, cmd);
				return;
			}

		} else {
			/*
			 * If an error occurs on read, we record it, and
			 * continue to fetch data in order to avoid bubble.
			 *
			 * As a small shortcut, we stop if we detect that
			 * a CSW mixed into data.
			 */
			if (urb->status != 0)
				cmd->error = -EIO;

			len = urb->actual_length;
			if (urb->status != 0 ||
			    len != cmd->sgv[cmd->current_sg].length) {
				if ((len & 0x1FF) == US_BULK_CS_WRAP_LEN)
					goto Bad_End;
			}
		}

		cmd->act_len += urb->actual_length;
		ub_cmdtr_act_len(sc, cmd);

		if (++cmd->current_sg < cmd->nsg) {
			ub_data_start(sc, cmd);
			return;
		}
		ub_state_stat(sc, cmd);

	} else if (cmd->state == UB_CMDST_STAT) {
		if (urb->status == -EPIPE) {
			rc = ub_submit_clear_stall(sc, cmd, sc->last_pipe);
			if (rc != 0) {
				printk(KERN_NOTICE "%s: "
				    "unable to submit clear (%d)\n",
				    sc->name, rc);
				ub_state_done(sc, cmd, rc);
				return;
			}

			/*
			 * Having a stall when getting CSW is an error, so
			 * make sure uppper levels are not oblivious to it.
			 */
			cmd->error = -EIO;		/* A cheap trick... */

			cmd->state = UB_CMDST_CLRRS;
			ub_cmdtr_state(sc, cmd);
			return;
		}

		/* Catch everything, including -EOVERFLOW and other nasties. */
		if (urb->status != 0)
			goto Bad_End;

		if (urb->actual_length == 0) {
			ub_state_stat_counted(sc, cmd);
			return;
		}

		/*
		 * Check the returned Bulk protocol status.
		 * The status block has to be validated first.
		 */

		bcs = &sc->work_bcs;

		if (sc->signature == cpu_to_le32(0)) {
			/*
			 * This is the first reply, so do not perform the check.
			 * Instead, remember the signature the device uses
			 * for future checks. But do not allow a nul.
			 */
			sc->signature = bcs->Signature;
			if (sc->signature == cpu_to_le32(0)) {
				ub_state_stat_counted(sc, cmd);
				return;
			}
		} else {
			if (bcs->Signature != sc->signature) {
				ub_state_stat_counted(sc, cmd);
				return;
			}
		}

		if (bcs->Tag != cmd->tag) {
			/*
			 * This usually happens when we disagree with the
			 * device's microcode about something. For instance,
			 * a few of them throw this after timeouts. They buffer
			 * commands and reply at commands we timed out before.
			 * Without flushing these replies we loop forever.
			 */
			ub_state_stat_counted(sc, cmd);
			return;
		}

		len = le32_to_cpu(bcs->Residue);
		if (len != cmd->len - cmd->act_len) {
			/*
			 * It is all right to transfer less, the caller has
			 * to check. But it's not all right if the device
			 * counts disagree with our counts.
			 */
			/* P3 */ printk("%s: resid %d len %d act %d\n",
			    sc->name, len, cmd->len, cmd->act_len);
			goto Bad_End;
		}

		switch (bcs->Status) {
		case US_BULK_STAT_OK:
			break;
		case US_BULK_STAT_FAIL:
			ub_state_sense(sc, cmd);
			return;
		case US_BULK_STAT_PHASE:
			/* P3 */ printk("%s: status PHASE\n", sc->name);
			goto Bad_End;
		default:
			printk(KERN_INFO "%s: unknown CSW status 0x%x\n",
			    sc->name, bcs->Status);
			ub_state_done(sc, cmd, -EINVAL);
			return;
		}

		/* Not zeroing error to preserve a babble indicator */
		if (cmd->error != 0) {
			ub_state_sense(sc, cmd);
			return;
		}
		cmd->state = UB_CMDST_DONE;
		ub_cmdtr_state(sc, cmd);
		ub_cmdq_pop(sc);
		(*cmd->done)(sc, cmd);

	} else if (cmd->state == UB_CMDST_SENSE) {
		ub_state_done(sc, cmd, -EIO);

	} else {
		printk(KERN_WARNING "%s: "
		    "wrong command state %d\n",
		    sc->name, cmd->state);
		ub_state_done(sc, cmd, -EINVAL);
		return;
	}
	return;

Bad_End: /* Little Excel is dead */
	ub_state_done(sc, cmd, -EIO);
}

/*
 * Factorization helper for the command state machine:
 * Initiate a data segment transfer.
 */
static void ub_data_start(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct scatterlist *sg = &cmd->sgv[cmd->current_sg];
	int pipe;
	int rc;

	UB_INIT_COMPLETION(sc->work_done);

	if (cmd->dir == UB_DIR_READ)
		pipe = sc->recv_bulk_pipe;
	else
		pipe = sc->send_bulk_pipe;
	sc->last_pipe = pipe;
	usb_fill_bulk_urb(&sc->work_urb, sc->dev, pipe,
	    page_address(sg->page) + sg->offset, sg->length,
	    ub_urb_complete, sc);
	sc->work_urb.actual_length = 0;
	sc->work_urb.error_count = 0;
	sc->work_urb.status = 0;

	if ((rc = usb_submit_urb(&sc->work_urb, GFP_ATOMIC)) != 0) {
		/* XXX Clear stalls */
		ub_complete(&sc->work_done);
		ub_state_done(sc, cmd, rc);
		return;
	}

	sc->work_timer.expires = jiffies + UB_DATA_TIMEOUT;
	add_timer(&sc->work_timer);

	cmd->state = UB_CMDST_DATA;
	ub_cmdtr_state(sc, cmd);
}

/*
 * Factorization helper for the command state machine:
 * Finish the command.
 */
static void ub_state_done(struct ub_dev *sc, struct ub_scsi_cmd *cmd, int rc)
{

	cmd->error = rc;
	cmd->state = UB_CMDST_DONE;
	ub_cmdtr_state(sc, cmd);
	ub_cmdq_pop(sc);
	(*cmd->done)(sc, cmd);
}

/*
 * Factorization helper for the command state machine:
 * Submit a CSW read.
 */
static int __ub_state_stat(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	int rc;

	UB_INIT_COMPLETION(sc->work_done);

	sc->last_pipe = sc->recv_bulk_pipe;
	usb_fill_bulk_urb(&sc->work_urb, sc->dev, sc->recv_bulk_pipe,
	    &sc->work_bcs, US_BULK_CS_WRAP_LEN, ub_urb_complete, sc);
	sc->work_urb.actual_length = 0;
	sc->work_urb.error_count = 0;
	sc->work_urb.status = 0;

	if ((rc = usb_submit_urb(&sc->work_urb, GFP_ATOMIC)) != 0) {
		/* XXX Clear stalls */
		ub_complete(&sc->work_done);
		ub_state_done(sc, cmd, rc);
		return -1;
	}

	sc->work_timer.expires = jiffies + UB_STAT_TIMEOUT;
	add_timer(&sc->work_timer);
	return 0;
}

/*
 * Factorization helper for the command state machine:
 * Submit a CSW read and go to STAT state.
 */
static void ub_state_stat(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{

	if (__ub_state_stat(sc, cmd) != 0)
		return;

	cmd->stat_count = 0;
	cmd->state = UB_CMDST_STAT;
	ub_cmdtr_state(sc, cmd);
}

/*
 * Factorization helper for the command state machine:
 * Submit a CSW read and go to STAT state with counter (along [C] path).
 */
static void ub_state_stat_counted(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{

	if (++cmd->stat_count >= 4) {
		ub_state_sense(sc, cmd);
		return;
	}

	if (__ub_state_stat(sc, cmd) != 0)
		return;

	cmd->state = UB_CMDST_STAT;
	ub_cmdtr_state(sc, cmd);
}

/*
 * Factorization helper for the command state machine:
 * Submit a REQUEST SENSE and go to SENSE state.
 */
static void ub_state_sense(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct ub_scsi_cmd *scmd;
	struct scatterlist *sg;
	int rc;

	if (cmd->cdb[0] == REQUEST_SENSE) {
		rc = -EPIPE;
		goto error;
	}

	scmd = &sc->top_rqs_cmd;
	memset(scmd, 0, sizeof(struct ub_scsi_cmd));
	scmd->cdb[0] = REQUEST_SENSE;
	scmd->cdb[4] = UB_SENSE_SIZE;
	scmd->cdb_len = 6;
	scmd->dir = UB_DIR_READ;
	scmd->state = UB_CMDST_INIT;
	scmd->nsg = 1;
	sg = &scmd->sgv[0];
	sg->page = virt_to_page(sc->top_sense);
	sg->offset = (unsigned long)sc->top_sense & (PAGE_SIZE-1);
	sg->length = UB_SENSE_SIZE;
	scmd->len = UB_SENSE_SIZE;
	scmd->lun = cmd->lun;
	scmd->done = ub_top_sense_done;
	scmd->back = cmd;

	scmd->tag = sc->tagcnt++;

	cmd->state = UB_CMDST_SENSE;
	ub_cmdtr_state(sc, cmd);

	ub_cmdq_insert(sc, scmd);
	return;

error:
	ub_state_done(sc, cmd, rc);
}

/*
 * A helper for the command's state machine:
 * Submit a stall clear.
 */
static int ub_submit_clear_stall(struct ub_dev *sc, struct ub_scsi_cmd *cmd,
    int stalled_pipe)
{
	int endp;
	struct usb_ctrlrequest *cr;
	int rc;

	endp = usb_pipeendpoint(stalled_pipe);
	if (usb_pipein (stalled_pipe))
		endp |= USB_DIR_IN;

	cr = &sc->work_cr;
	cr->bRequestType = USB_RECIP_ENDPOINT;
	cr->bRequest = USB_REQ_CLEAR_FEATURE;
	cr->wValue = cpu_to_le16(USB_ENDPOINT_HALT);
	cr->wIndex = cpu_to_le16(endp);
	cr->wLength = cpu_to_le16(0);

	UB_INIT_COMPLETION(sc->work_done);

	usb_fill_control_urb(&sc->work_urb, sc->dev, sc->send_ctrl_pipe,
	    (unsigned char*) cr, NULL, 0, ub_urb_complete, sc);
	sc->work_urb.actual_length = 0;
	sc->work_urb.error_count = 0;
	sc->work_urb.status = 0;

	if ((rc = usb_submit_urb(&sc->work_urb, GFP_ATOMIC)) != 0) {
		ub_complete(&sc->work_done);
		return rc;
	}

	sc->work_timer.expires = jiffies + UB_CTRL_TIMEOUT;
	add_timer(&sc->work_timer);
	return 0;
}

/*
 */
static void ub_top_sense_done(struct ub_dev *sc, struct ub_scsi_cmd *scmd)
{
	unsigned char *sense = sc->top_sense;
	struct ub_scsi_cmd *cmd;

	/*
	 * Ignoring scmd->act_len, because the buffer was pre-zeroed.
	 */
	ub_cmdtr_sense(sc, scmd, sense);

	/*
	 * Find the command which triggered the unit attention or a check,
	 * save the sense into it, and advance its state machine.
	 */
	if ((cmd = ub_cmdq_peek(sc)) == NULL) {
		printk(KERN_WARNING "%s: sense done while idle\n", sc->name);
		return;
	}
	if (cmd != scmd->back) {
		printk(KERN_WARNING "%s: "
		    "sense done for wrong command 0x%x\n",
		    sc->name, cmd->tag);
		return;
	}
	if (cmd->state != UB_CMDST_SENSE) {
		printk(KERN_WARNING "%s: "
		    "sense done with bad cmd state %d\n",
		    sc->name, cmd->state);
		return;
	}

	cmd->key = sense[2] & 0x0F;
	cmd->asc = sense[12];
	cmd->ascq = sense[13];

	ub_scsi_urb_compl(sc, cmd);
}

/*
 * Reset management
 */

static void ub_reset_enter(struct ub_dev *sc)
{

	if (sc->reset) {
		/* This happens often on multi-LUN devices. */
		return;
	}
	sc->reset = 1;

#if 0 /* Not needed because the disconnect waits for us. */
	unsigned long flags;
	spin_lock_irqsave(&ub_lock, flags);
	sc->openc++;
	spin_unlock_irqrestore(&ub_lock, flags);
#endif

#if 0 /* We let them stop themselves. */
	struct list_head *p;
	struct ub_lun *lun;
	list_for_each(p, &sc->luns) {
		lun = list_entry(p, struct ub_lun, link);
		blk_stop_queue(lun->disk->queue);
	}
#endif

	schedule_work(&sc->reset_work);
}

static void ub_reset_task(void *arg)
{
	struct ub_dev *sc = arg;
	unsigned long flags;
	struct list_head *p;
	struct ub_lun *lun;
	int lkr, rc;

	if (!sc->reset) {
		printk(KERN_WARNING "%s: Running reset unrequested\n",
		    sc->name);
		return;
	}

	if (atomic_read(&sc->poison)) {
		printk(KERN_NOTICE "%s: Not resetting disconnected device\n",
		    sc->name); /* P3 This floods. Remove soon. XXX */
	} else if (sc->dev->actconfig->desc.bNumInterfaces != 1) {
		printk(KERN_NOTICE "%s: Not resetting multi-interface device\n",
		    sc->name); /* P3 This floods. Remove soon. XXX */
	} else {
		if ((lkr = usb_lock_device_for_reset(sc->dev, sc->intf)) < 0) {
			printk(KERN_NOTICE
			    "%s: usb_lock_device_for_reset failed (%d)\n",
			    sc->name, lkr);
		} else {
			rc = usb_reset_device(sc->dev);
			if (rc < 0) {
				printk(KERN_NOTICE "%s: "
				    "usb_lock_device_for_reset failed (%d)\n",
				    sc->name, rc);
			}

			if (lkr)
				usb_unlock_device(sc->dev);
		}
	}

	/*
	 * In theory, no commands can be running while reset is active,
	 * so nobody can ask for another reset, and so we do not need any
	 * queues of resets or anything. We do need a spinlock though,
	 * to interact with block layer.
	 */
	spin_lock_irqsave(&sc->lock, flags);
	sc->reset = 0;
	tasklet_schedule(&sc->tasklet);
	list_for_each(p, &sc->luns) {
		lun = list_entry(p, struct ub_lun, link);
		blk_start_queue(lun->disk->queue);
	}
	wake_up(&sc->reset_wait);
	spin_unlock_irqrestore(&sc->lock, flags);
}

/*
 * This is called from a process context.
 */
static void ub_revalidate(struct ub_dev *sc, struct ub_lun *lun)
{

	lun->readonly = 0;	/* XXX Query this from the device */

	lun->capacity.nsec = 0;
	lun->capacity.bsize = 512;
	lun->capacity.bshift = 0;

	if (ub_sync_tur(sc, lun) != 0)
		return;			/* Not ready */
	lun->changed = 0;

	if (ub_sync_read_cap(sc, lun, &lun->capacity) != 0) {
		/*
		 * The retry here means something is wrong, either with the
		 * device, with the transport, or with our code.
		 * We keep this because sd.c has retries for capacity.
		 */
		if (ub_sync_read_cap(sc, lun, &lun->capacity) != 0) {
			lun->capacity.nsec = 0;
			lun->capacity.bsize = 512;
			lun->capacity.bshift = 0;
		}
	}
}

/*
 * The open funcion.
 * This is mostly needed to keep refcounting, but also to support
 * media checks on removable media drives.
 */
static int ub_bd_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ub_lun *lun;
	struct ub_dev *sc;
	unsigned long flags;
	int rc;

	if ((lun = disk->private_data) == NULL)
		return -ENXIO;
	sc = lun->udev;

	spin_lock_irqsave(&ub_lock, flags);
	if (atomic_read(&sc->poison)) {
		spin_unlock_irqrestore(&ub_lock, flags);
		return -ENXIO;
	}
	sc->openc++;
	spin_unlock_irqrestore(&ub_lock, flags);

	/*
	 * This is a workaround for a specific problem in our block layer.
	 * In 2.6.9, register_disk duplicates the code from rescan_partitions.
	 * However, if we do add_disk with a device which persistently reports
	 * a changed media, add_disk calls register_disk, which does do_open,
	 * which will call rescan_paritions for changed media. After that,
	 * register_disk attempts to do it all again and causes double kobject
	 * registration and a eventually an oops on module removal.
	 *
	 * The bottom line is, Al Viro says that we should not allow
	 * bdev->bd_invalidated to be set when doing add_disk no matter what.
	 */
	if (lun->first_open) {
		lun->first_open = 0;
		if (lun->changed) {
			rc = -ENOMEDIUM;
			goto err_open;
		}
	}

	if (lun->removable || lun->readonly)
		check_disk_change(inode->i_bdev);

	/*
	 * The sd.c considers ->media_present and ->changed not equivalent,
	 * under some pretty murky conditions (a failure of READ CAPACITY).
	 * We may need it one day.
	 */
	if (lun->removable && lun->changed && !(filp->f_flags & O_NDELAY)) {
		rc = -ENOMEDIUM;
		goto err_open;
	}

	if (lun->readonly && (filp->f_mode & FMODE_WRITE)) {
		rc = -EROFS;
		goto err_open;
	}

	return 0;

err_open:
	ub_put(sc);
	return rc;
}

/*
 */
static int ub_bd_release(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ub_lun *lun = disk->private_data;
	struct ub_dev *sc = lun->udev;

	ub_put(sc);
	return 0;
}

/*
 * The ioctl interface.
 */
static int ub_bd_ioctl(struct inode *inode, struct file *filp,
    unsigned int cmd, unsigned long arg)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	void __user *usermem = (void __user *) arg;

	return scsi_cmd_ioctl(filp, disk, cmd, usermem);
}

/*
 * This is called once a new disk was seen by the block layer or by ub_probe().
 * The main onjective here is to discover the features of the media such as
 * the capacity, read-only status, etc. USB storage generally does not
 * need to be spun up, but if we needed it, this would be the place.
 *
 * This call can sleep.
 *
 * The return code is not used.
 */
static int ub_bd_revalidate(struct gendisk *disk)
{
	struct ub_lun *lun = disk->private_data;

	ub_revalidate(lun->udev, lun);

	/* XXX Support sector size switching like in sr.c */
	blk_queue_hardsect_size(disk->queue, lun->capacity.bsize);
	set_capacity(disk, lun->capacity.nsec);
	// set_disk_ro(sdkp->disk, lun->readonly);

	return 0;
}

/*
 * The check is called by the block layer to verify if the media
 * is still available. It is supposed to be harmless, lightweight and
 * non-intrusive in case the media was not changed.
 *
 * This call can sleep.
 *
 * The return code is bool!
 */
static int ub_bd_media_changed(struct gendisk *disk)
{
	struct ub_lun *lun = disk->private_data;

	if (!lun->removable)
		return 0;

	/*
	 * We clean checks always after every command, so this is not
	 * as dangerous as it looks. If the TEST_UNIT_READY fails here,
	 * the device is actually not ready with operator or software
	 * intervention required. One dangerous item might be a drive which
	 * spins itself down, and come the time to write dirty pages, this
	 * will fail, then block layer discards the data. Since we never
	 * spin drives up, such devices simply cannot be used with ub anyway.
	 */
	if (ub_sync_tur(lun->udev, lun) != 0) {
		lun->changed = 1;
		return 1;
	}

	return lun->changed;
}

static struct block_device_operations ub_bd_fops = {
	.owner		= THIS_MODULE,
	.open		= ub_bd_open,
	.release	= ub_bd_release,
	.ioctl		= ub_bd_ioctl,
	.media_changed	= ub_bd_media_changed,
	.revalidate_disk = ub_bd_revalidate,
};

/*
 * Common ->done routine for commands executed synchronously.
 */
static void ub_probe_done(struct ub_dev *sc, struct ub_scsi_cmd *cmd)
{
	struct completion *cop = cmd->back;
	complete(cop);
}

/*
 * Test if the device has a check condition on it, synchronously.
 */
static int ub_sync_tur(struct ub_dev *sc, struct ub_lun *lun)
{
	struct ub_scsi_cmd *cmd;
	enum { ALLOC_SIZE = sizeof(struct ub_scsi_cmd) };
	unsigned long flags;
	struct completion compl;
	int rc;

	init_completion(&compl);

	rc = -ENOMEM;
	if ((cmd = kmalloc(ALLOC_SIZE, GFP_KERNEL)) == NULL)
		goto err_alloc;
	memset(cmd, 0, ALLOC_SIZE);

	cmd->cdb[0] = TEST_UNIT_READY;
	cmd->cdb_len = 6;
	cmd->dir = UB_DIR_NONE;
	cmd->state = UB_CMDST_INIT;
	cmd->lun = lun;			/* This may be NULL, but that's ok */
	cmd->done = ub_probe_done;
	cmd->back = &compl;

	spin_lock_irqsave(&sc->lock, flags);
	cmd->tag = sc->tagcnt++;

	rc = ub_submit_scsi(sc, cmd);
	spin_unlock_irqrestore(&sc->lock, flags);

	if (rc != 0) {
		printk("ub: testing ready: submit error (%d)\n", rc); /* P3 */
		goto err_submit;
	}

	wait_for_completion(&compl);

	rc = cmd->error;

	if (rc == -EIO && cmd->key != 0)	/* Retries for benh's key */
		rc = cmd->key;

err_submit:
	kfree(cmd);
err_alloc:
	return rc;
}

/*
 * Read the SCSI capacity synchronously (for probing).
 */
static int ub_sync_read_cap(struct ub_dev *sc, struct ub_lun *lun,
    struct ub_capacity *ret)
{
	struct ub_scsi_cmd *cmd;
	struct scatterlist *sg;
	char *p;
	enum { ALLOC_SIZE = sizeof(struct ub_scsi_cmd) + 8 };
	unsigned long flags;
	unsigned int bsize, shift;
	unsigned long nsec;
	struct completion compl;
	int rc;

	init_completion(&compl);

	rc = -ENOMEM;
	if ((cmd = kmalloc(ALLOC_SIZE, GFP_KERNEL)) == NULL)
		goto err_alloc;
	memset(cmd, 0, ALLOC_SIZE);
	p = (char *)cmd + sizeof(struct ub_scsi_cmd);

	cmd->cdb[0] = 0x25;
	cmd->cdb_len = 10;
	cmd->dir = UB_DIR_READ;
	cmd->state = UB_CMDST_INIT;
	cmd->nsg = 1;
	sg = &cmd->sgv[0];
	sg->page = virt_to_page(p);
	sg->offset = (unsigned long)p & (PAGE_SIZE-1);
	sg->length = 8;
	cmd->len = 8;
	cmd->lun = lun;
	cmd->done = ub_probe_done;
	cmd->back = &compl;

	spin_lock_irqsave(&sc->lock, flags);
	cmd->tag = sc->tagcnt++;

	rc = ub_submit_scsi(sc, cmd);
	spin_unlock_irqrestore(&sc->lock, flags);

	if (rc != 0) {
		printk("ub: reading capacity: submit error (%d)\n", rc); /* P3 */
		goto err_submit;
	}

	wait_for_completion(&compl);

	if (cmd->error != 0) {
		printk("ub: reading capacity: error %d\n", cmd->error); /* P3 */
		rc = -EIO;
		goto err_read;
	}
	if (cmd->act_len != 8) {
		printk("ub: reading capacity: size %d\n", cmd->act_len); /* P3 */
		rc = -EIO;
		goto err_read;
	}

	/* sd.c special-cases sector size of 0 to mean 512. Needed? Safe? */
	nsec = be32_to_cpu(*(__be32 *)p) + 1;
	bsize = be32_to_cpu(*(__be32 *)(p + 4));
	switch (bsize) {
	case 512:	shift = 0;	break;
	case 1024:	shift = 1;	break;
	case 2048:	shift = 2;	break;
	case 4096:	shift = 3;	break;
	default:
		printk("ub: Bad sector size %u\n", bsize); /* P3 */
		rc = -EDOM;
		goto err_inv_bsize;
	}

	ret->bsize = bsize;
	ret->bshift = shift;
	ret->nsec = nsec << shift;
	rc = 0;

err_inv_bsize:
err_read:
err_submit:
	kfree(cmd);
err_alloc:
	return rc;
}

/*
 */
static void ub_probe_urb_complete(struct urb *urb, struct pt_regs *pt)
{
	struct completion *cop = urb->context;
	complete(cop);
}

static void ub_probe_timeout(unsigned long arg)
{
	struct completion *cop = (struct completion *) arg;
	complete(cop);
}

/*
 * Get number of LUNs by the way of Bulk GetMaxLUN command.
 */
static int ub_sync_getmaxlun(struct ub_dev *sc)
{
	int ifnum = sc->intf->cur_altsetting->desc.bInterfaceNumber;
	unsigned char *p;
	enum { ALLOC_SIZE = 1 };
	struct usb_ctrlrequest *cr;
	struct completion compl;
	struct timer_list timer;
	int nluns;
	int rc;

	init_completion(&compl);

	rc = -ENOMEM;
	if ((p = kmalloc(ALLOC_SIZE, GFP_KERNEL)) == NULL)
		goto err_alloc;
	*p = 55;

	cr = &sc->work_cr;
	cr->bRequestType = USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	cr->bRequest = US_BULK_GET_MAX_LUN;
	cr->wValue = cpu_to_le16(0);
	cr->wIndex = cpu_to_le16(ifnum);
	cr->wLength = cpu_to_le16(1);

	usb_fill_control_urb(&sc->work_urb, sc->dev, sc->recv_ctrl_pipe,
	    (unsigned char*) cr, p, 1, ub_probe_urb_complete, &compl);
	sc->work_urb.actual_length = 0;
	sc->work_urb.error_count = 0;
	sc->work_urb.status = 0;

	if ((rc = usb_submit_urb(&sc->work_urb, GFP_KERNEL)) != 0) {
		if (rc == -EPIPE) {
			printk("%s: Stall submitting GetMaxLUN, using 1 LUN\n",
			     sc->name); /* P3 */
		} else {
			printk(KERN_NOTICE
			     "%s: Unable to submit GetMaxLUN (%d)\n",
			     sc->name, rc);
		}
		goto err_submit;
	}

	init_timer(&timer);
	timer.function = ub_probe_timeout;
	timer.data = (unsigned long) &compl;
	timer.expires = jiffies + UB_CTRL_TIMEOUT;
	add_timer(&timer);

	wait_for_completion(&compl);

	del_timer_sync(&timer);
	usb_kill_urb(&sc->work_urb);

	if ((rc = sc->work_urb.status) < 0) {
		if (rc == -EPIPE) {
			printk("%s: Stall at GetMaxLUN, using 1 LUN\n",
			     sc->name); /* P3 */
		} else {
			printk(KERN_NOTICE
			     "%s: Error at GetMaxLUN (%d)\n",
			     sc->name, rc);
		}
		goto err_io;
	}

	if (sc->work_urb.actual_length != 1) {
		printk("%s: GetMaxLUN returned %d bytes\n", sc->name,
		    sc->work_urb.actual_length); /* P3 */
		nluns = 0;
	} else {
		if ((nluns = *p) == 55) {
			nluns = 0;
		} else {
  			/* GetMaxLUN returns the maximum LUN number */
			nluns += 1;
			if (nluns > UB_MAX_LUNS)
				nluns = UB_MAX_LUNS;
		}
		printk("%s: GetMaxLUN returned %d, using %d LUNs\n", sc->name,
		    *p, nluns); /* P3 */
	}

	kfree(p);
	return nluns;

err_io:
err_submit:
	kfree(p);
err_alloc:
	return rc;
}

/*
 * Clear initial stalls.
 */
static int ub_probe_clear_stall(struct ub_dev *sc, int stalled_pipe)
{
	int endp;
	struct usb_ctrlrequest *cr;
	struct completion compl;
	struct timer_list timer;
	int rc;

	init_completion(&compl);

	endp = usb_pipeendpoint(stalled_pipe);
	if (usb_pipein (stalled_pipe))
		endp |= USB_DIR_IN;

	cr = &sc->work_cr;
	cr->bRequestType = USB_RECIP_ENDPOINT;
	cr->bRequest = USB_REQ_CLEAR_FEATURE;
	cr->wValue = cpu_to_le16(USB_ENDPOINT_HALT);
	cr->wIndex = cpu_to_le16(endp);
	cr->wLength = cpu_to_le16(0);

	usb_fill_control_urb(&sc->work_urb, sc->dev, sc->send_ctrl_pipe,
	    (unsigned char*) cr, NULL, 0, ub_probe_urb_complete, &compl);
	sc->work_urb.actual_length = 0;
	sc->work_urb.error_count = 0;
	sc->work_urb.status = 0;

	if ((rc = usb_submit_urb(&sc->work_urb, GFP_KERNEL)) != 0) {
		printk(KERN_WARNING
		     "%s: Unable to submit a probe clear (%d)\n", sc->name, rc);
		return rc;
	}

	init_timer(&timer);
	timer.function = ub_probe_timeout;
	timer.data = (unsigned long) &compl;
	timer.expires = jiffies + UB_CTRL_TIMEOUT;
	add_timer(&timer);

	wait_for_completion(&compl);

	del_timer_sync(&timer);
	usb_kill_urb(&sc->work_urb);

	/* reset the endpoint toggle */
	usb_settoggle(sc->dev, endp, usb_pipeout(sc->last_pipe), 0);

	return 0;
}

/*
 * Get the pipe settings.
 */
static int ub_get_pipes(struct ub_dev *sc, struct usb_device *dev,
    struct usb_interface *intf)
{
	struct usb_host_interface *altsetting = intf->cur_altsetting;
	struct usb_endpoint_descriptor *ep_in = NULL;
	struct usb_endpoint_descriptor *ep_out = NULL;
	struct usb_endpoint_descriptor *ep;
	int i;

	/*
	 * Find the endpoints we need.
	 * We are expecting a minimum of 2 endpoints - in and out (bulk).
	 * We will ignore any others.
	 */
	for (i = 0; i < altsetting->desc.bNumEndpoints; i++) {
		ep = &altsetting->endpoint[i].desc;

		/* Is it a BULK endpoint? */
		if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				== USB_ENDPOINT_XFER_BULK) {
			/* BULK in or out? */
			if (ep->bEndpointAddress & USB_DIR_IN)
				ep_in = ep;
			else
				ep_out = ep;
		}
	}

	if (ep_in == NULL || ep_out == NULL) {
		printk(KERN_NOTICE "%s: failed endpoint check\n",
		    sc->name);
		return -ENODEV;
	}

	/* Calculate and store the pipe values */
	sc->send_ctrl_pipe = usb_sndctrlpipe(dev, 0);
	sc->recv_ctrl_pipe = usb_rcvctrlpipe(dev, 0);
	sc->send_bulk_pipe = usb_sndbulkpipe(dev,
		ep_out->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	sc->recv_bulk_pipe = usb_rcvbulkpipe(dev, 
		ep_in->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);

	return 0;
}

/*
 * Probing is done in the process context, which allows us to cheat
 * and not to build a state machine for the discovery.
 */
static int ub_probe(struct usb_interface *intf,
    const struct usb_device_id *dev_id)
{
	struct ub_dev *sc;
	int nluns;
	int rc;
	int i;

	if (usb_usual_check_type(dev_id, USB_US_TYPE_UB))
		return -ENXIO;

	rc = -ENOMEM;
	if ((sc = kmalloc(sizeof(struct ub_dev), GFP_KERNEL)) == NULL)
		goto err_core;
	memset(sc, 0, sizeof(struct ub_dev));
	spin_lock_init(&sc->lock);
	INIT_LIST_HEAD(&sc->luns);
	usb_init_urb(&sc->work_urb);
	tasklet_init(&sc->tasklet, ub_scsi_action, (unsigned long)sc);
	atomic_set(&sc->poison, 0);
	INIT_WORK(&sc->reset_work, ub_reset_task, sc);
	init_waitqueue_head(&sc->reset_wait);

	init_timer(&sc->work_timer);
	sc->work_timer.data = (unsigned long) sc;
	sc->work_timer.function = ub_urb_timeout;

	ub_init_completion(&sc->work_done);
	sc->work_done.done = 1;		/* A little yuk, but oh well... */

	sc->dev = interface_to_usbdev(intf);
	sc->intf = intf;
	// sc->ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
	usb_set_intfdata(intf, sc);
	usb_get_dev(sc->dev);
	// usb_get_intf(sc->intf);	/* Do we need this? */

	snprintf(sc->name, 12, DRV_NAME "(%d.%d)",
	    sc->dev->bus->busnum, sc->dev->devnum);

	/* XXX Verify that we can handle the device (from descriptors) */

	if (ub_get_pipes(sc, sc->dev, intf) != 0)
		goto err_dev_desc;

	if (device_create_file(&sc->intf->dev, &dev_attr_diag) != 0)
		goto err_diag;

	/*
	 * At this point, all USB initialization is done, do upper layer.
	 * We really hate halfway initialized structures, so from the
	 * invariants perspective, this ub_dev is fully constructed at
	 * this point.
	 */

	/*
	 * This is needed to clear toggles. It is a problem only if we do
	 * `rmmod ub && modprobe ub` without disconnects, but we like that.
	 */
#if 0 /* iPod Mini fails if we do this (big white iPod works) */
	ub_probe_clear_stall(sc, sc->recv_bulk_pipe);
	ub_probe_clear_stall(sc, sc->send_bulk_pipe);
#endif

	/*
	 * The way this is used by the startup code is a little specific.
	 * A SCSI check causes a USB stall. Our common case code sees it
	 * and clears the check, after which the device is ready for use.
	 * But if a check was not present, any command other than
	 * TEST_UNIT_READY ends with a lockup (including REQUEST_SENSE).
	 *
	 * If we neglect to clear the SCSI check, the first real command fails
	 * (which is the capacity readout). We clear that and retry, but why
	 * causing spurious retries for no reason.
	 *
	 * Revalidation may start with its own TEST_UNIT_READY, but that one
	 * has to succeed, so we clear checks with an additional one here.
	 * In any case it's not our business how revaliadation is implemented.
	 */
	for (i = 0; i < 3; i++) {	/* Retries for benh's key */
		if ((rc = ub_sync_tur(sc, NULL)) <= 0) break;
		if (rc != 0x6) break;
		msleep(10);
	}

	nluns = 1;
	for (i = 0; i < 3; i++) {
		if ((rc = ub_sync_getmaxlun(sc)) < 0) {
			/* 
			 * This segment is taken from usb-storage. They say
			 * that ZIP-100 needs this, but my own ZIP-100 works
			 * fine without this.
			 * Still, it does not seem to hurt anything.
			 */
			if (rc == -EPIPE) {
				ub_probe_clear_stall(sc, sc->recv_bulk_pipe);
				ub_probe_clear_stall(sc, sc->send_bulk_pipe);
			}
			break;
		}
		if (rc != 0) {
			nluns = rc;
			break;
		}
		msleep(100);
	}

	for (i = 0; i < nluns; i++) {
		ub_probe_lun(sc, i);
	}
	return 0;

	/* device_remove_file(&sc->intf->dev, &dev_attr_diag); */
err_diag:
err_dev_desc:
	usb_set_intfdata(intf, NULL);
	// usb_put_intf(sc->intf);
	usb_put_dev(sc->dev);
	kfree(sc);
err_core:
	return rc;
}

static int ub_probe_lun(struct ub_dev *sc, int lnum)
{
	struct ub_lun *lun;
	request_queue_t *q;
	struct gendisk *disk;
	int rc;

	rc = -ENOMEM;
	if ((lun = kmalloc(sizeof(struct ub_lun), GFP_KERNEL)) == NULL)
		goto err_alloc;
	memset(lun, 0, sizeof(struct ub_lun));
	lun->num = lnum;

	rc = -ENOSR;
	if ((lun->id = ub_id_get()) == -1)
		goto err_id;

	lun->udev = sc;
	list_add(&lun->link, &sc->luns);

	snprintf(lun->name, 16, DRV_NAME "%c(%d.%d.%d)",
	    lun->id + 'a', sc->dev->bus->busnum, sc->dev->devnum, lun->num);

	lun->removable = 1;		/* XXX Query this from the device */
	lun->changed = 1;		/* ub_revalidate clears only */
	lun->first_open = 1;
	ub_revalidate(sc, lun);

	rc = -ENOMEM;
	if ((disk = alloc_disk(UB_PARTS_PER_LUN)) == NULL)
		goto err_diskalloc;

	lun->disk = disk;
	sprintf(disk->disk_name, DRV_NAME "%c", lun->id + 'a');
	sprintf(disk->devfs_name, DEVFS_NAME "/%c", lun->id + 'a');
	disk->major = UB_MAJOR;
	disk->first_minor = lun->id * UB_PARTS_PER_LUN;
	disk->fops = &ub_bd_fops;
	disk->private_data = lun;
	disk->driverfs_dev = &sc->intf->dev;

	rc = -ENOMEM;
	if ((q = blk_init_queue(ub_request_fn, &sc->lock)) == NULL)
		goto err_blkqinit;

	disk->queue = q;

	blk_queue_bounce_limit(q, BLK_BOUNCE_HIGH);
	blk_queue_max_hw_segments(q, UB_MAX_REQ_SG);
	blk_queue_max_phys_segments(q, UB_MAX_REQ_SG);
	blk_queue_segment_boundary(q, 0xffffffff);	/* Dubious. */
	blk_queue_max_sectors(q, UB_MAX_SECTORS);
	blk_queue_hardsect_size(q, lun->capacity.bsize);

	q->queuedata = lun;

	set_capacity(disk, lun->capacity.nsec);
	if (lun->removable)
		disk->flags |= GENHD_FL_REMOVABLE;

	add_disk(disk);

	return 0;

err_blkqinit:
	put_disk(disk);
err_diskalloc:
	list_del(&lun->link);
	ub_id_put(lun->id);
err_id:
	kfree(lun);
err_alloc:
	return rc;
}

static void ub_disconnect(struct usb_interface *intf)
{
	struct ub_dev *sc = usb_get_intfdata(intf);
	struct list_head *p;
	struct ub_lun *lun;
	struct gendisk *disk;
	unsigned long flags;

	/*
	 * Prevent ub_bd_release from pulling the rug from under us.
	 * XXX This is starting to look like a kref.
	 * XXX Why not to take this ref at probe time?
	 */
	spin_lock_irqsave(&ub_lock, flags);
	sc->openc++;
	spin_unlock_irqrestore(&ub_lock, flags);

	/*
	 * Fence stall clearnings, operations triggered by unlinkings and so on.
	 * We do not attempt to unlink any URBs, because we do not trust the
	 * unlink paths in HC drivers. Also, we get -84 upon disconnect anyway.
	 */
	atomic_set(&sc->poison, 1);

	/*
	 * Wait for reset to end, if any.
	 */
	wait_event(sc->reset_wait, !sc->reset);

	/*
	 * Blow away queued commands.
	 *
	 * Actually, this never works, because before we get here
	 * the HCD terminates outstanding URB(s). It causes our
	 * SCSI command queue to advance, commands fail to submit,
	 * and the whole queue drains. So, we just use this code to
	 * print warnings.
	 */
	spin_lock_irqsave(&sc->lock, flags);
	{
		struct ub_scsi_cmd *cmd;
		int cnt = 0;
		while ((cmd = ub_cmdq_peek(sc)) != NULL) {
			cmd->error = -ENOTCONN;
			cmd->state = UB_CMDST_DONE;
			ub_cmdtr_state(sc, cmd);
			ub_cmdq_pop(sc);
			(*cmd->done)(sc, cmd);
			cnt++;
		}
		if (cnt != 0) {
			printk(KERN_WARNING "%s: "
			    "%d was queued after shutdown\n", sc->name, cnt);
		}
	}
	spin_unlock_irqrestore(&sc->lock, flags);

	/*
	 * Unregister the upper layer.
	 */
	list_for_each (p, &sc->luns) {
		lun = list_entry(p, struct ub_lun, link);
		disk = lun->disk;
		if (disk->flags & GENHD_FL_UP)
			del_gendisk(disk);
		/*
		 * I wish I could do:
		 *    set_bit(QUEUE_FLAG_DEAD, &q->queue_flags);
		 * As it is, we rely on our internal poisoning and let
		 * the upper levels to spin furiously failing all the I/O.
		 */
	}

	/*
	 * Taking a lock on a structure which is about to be freed
	 * is very nonsensual. Here it is largely a way to do a debug freeze,
	 * and a bracket which shows where the nonsensual code segment ends.
	 *
	 * Testing for -EINPROGRESS is always a bug, so we are bending
	 * the rules a little.
	 */
	spin_lock_irqsave(&sc->lock, flags);
	if (sc->work_urb.status == -EINPROGRESS) {	/* janitors: ignore */
		printk(KERN_WARNING "%s: "
		    "URB is active after disconnect\n", sc->name);
	}
	spin_unlock_irqrestore(&sc->lock, flags);

	/*
	 * There is virtually no chance that other CPU runs times so long
	 * after ub_urb_complete should have called del_timer, but only if HCD
	 * didn't forget to deliver a callback on unlink.
	 */
	del_timer_sync(&sc->work_timer);

	/*
	 * At this point there must be no commands coming from anyone
	 * and no URBs left in transit.
	 */

	device_remove_file(&sc->intf->dev, &dev_attr_diag);
	usb_set_intfdata(intf, NULL);
	// usb_put_intf(sc->intf);
	sc->intf = NULL;
	usb_put_dev(sc->dev);
	sc->dev = NULL;

	ub_put(sc);
}

static struct usb_driver ub_driver = {
	.name =		"ub",
	.probe =	ub_probe,
	.disconnect =	ub_disconnect,
	.id_table =	ub_usb_ids,
};

static int __init ub_init(void)
{
	int rc;

	if ((rc = register_blkdev(UB_MAJOR, DRV_NAME)) != 0)
		goto err_regblkdev;
	devfs_mk_dir(DEVFS_NAME);

	if ((rc = usb_register(&ub_driver)) != 0)
		goto err_register;

	usb_usual_set_present(USB_US_TYPE_UB);
	return 0;

err_register:
	devfs_remove(DEVFS_NAME);
	unregister_blkdev(UB_MAJOR, DRV_NAME);
err_regblkdev:
	return rc;
}

static void __exit ub_exit(void)
{
	usb_deregister(&ub_driver);

	devfs_remove(DEVFS_NAME);
	unregister_blkdev(UB_MAJOR, DRV_NAME);
	usb_usual_clear_present(USB_US_TYPE_UB);
}

module_init(ub_init);
module_exit(ub_exit);

MODULE_LICENSE("GPL");
