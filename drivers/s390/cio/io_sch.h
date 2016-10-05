#ifndef S390_IO_SCH_H
#define S390_IO_SCH_H

#include <linux/types.h>
#include <asm/schid.h>
#include <asm/ccwdev.h>
#include <asm/irq.h>
#include "css.h"
#include "orb.h"

struct io_subchannel_private {
	union orb orb;		/* operation request block */
	struct ccw1 sense_ccw;	/* static ccw for sense command */
	struct ccw_device *cdev;/* pointer to the child ccw device */
	struct {
		unsigned int suspend:1;	/* allow suspend */
		unsigned int prefetch:1;/* deny prefetch */
		unsigned int inter:1;	/* suppress intermediate interrupts */
	} __packed options;
} __aligned(8);

#define to_io_private(n) ((struct io_subchannel_private *) \
			  dev_get_drvdata(&(n)->dev))
#define set_io_private(n, p) (dev_set_drvdata(&(n)->dev, p))

static inline struct ccw_device *sch_get_cdev(struct subchannel *sch)
{
	struct io_subchannel_private *priv = to_io_private(sch);
	return priv ? priv->cdev : NULL;
}

static inline void sch_set_cdev(struct subchannel *sch,
				struct ccw_device *cdev)
{
	struct io_subchannel_private *priv = to_io_private(sch);
	if (priv)
		priv->cdev = cdev;
}

#define MAX_CIWS 8

/*
 * Possible status values for a CCW request's I/O.
 */
enum io_status {
	IO_DONE,
	IO_RUNNING,
	IO_STATUS_ERROR,
	IO_PATH_ERROR,
	IO_REJECTED,
	IO_KILLED
};

/**
 * ccw_request - Internal CCW request.
 * @cp: channel program to start
 * @timeout: maximum allowable time in jiffies between start I/O and interrupt
 * @maxretries: number of retries per I/O operation and path
 * @lpm: mask of paths to use
 * @check: optional callback that determines if results are final
 * @filter: optional callback to adjust request status based on IRB data
 * @callback: final callback
 * @data: user-defined pointer passed to all callbacks
 * @singlepath: if set, use only one path from @lpm per start I/O
 * @cancel: non-zero if request was cancelled
 * @done: non-zero if request was finished
 * @mask: current path mask
 * @retries: current number of retries
 * @drc: delayed return code
 */
struct ccw_request {
	struct ccw1 *cp;
	unsigned long timeout;
	u16 maxretries;
	u8 lpm;
	int (*check)(struct ccw_device *, void *);
	enum io_status (*filter)(struct ccw_device *, void *, struct irb *,
				 enum io_status);
	void (*callback)(struct ccw_device *, void *, int);
	void *data;
	unsigned int singlepath:1;
	/* These fields are used internally. */
	unsigned int cancel:1;
	unsigned int done:1;
	u16 mask;
	u16 retries;
	int drc;
} __attribute__((packed));

/*
 * sense-id response buffer layout
 */
struct senseid {
	/* common part */
	u8  reserved;	/* always 0x'FF' */
	u16 cu_type;	/* control unit type */
	u8  cu_model;	/* control unit model */
	u16 dev_type;	/* device type */
	u8  dev_model;	/* device model */
	u8  unused;	/* padding byte */
	/* extended part */
	struct ciw ciw[MAX_CIWS];	/* variable # of CIWs */
}  __attribute__ ((packed, aligned(4)));

enum cdev_todo {
	CDEV_TODO_NOTHING,
	CDEV_TODO_ENABLE_CMF,
	CDEV_TODO_REBIND,
	CDEV_TODO_REGISTER,
	CDEV_TODO_UNREG,
	CDEV_TODO_UNREG_EVAL,
};

#define FAKE_CMD_IRB	1
#define FAKE_TM_IRB	2

struct ccw_device_private {
	struct ccw_device *cdev;
	struct subchannel *sch;
	int state;		/* device state */
	atomic_t onoff;
	struct ccw_dev_id dev_id;	/* device id */
	struct ccw_request req;		/* internal I/O request */
	int iretry;
	u8 pgid_valid_mask;	/* mask of valid PGIDs */
	u8 pgid_todo_mask;	/* mask of PGIDs to be adjusted */
	u8 pgid_reset_mask;	/* mask of PGIDs which were reset */
	u8 path_noirq_mask;	/* mask of paths for which no irq was
				   received */
	u8 path_notoper_mask;	/* mask of paths which were found
				   not operable */
	u8 path_gone_mask;	/* mask of paths, that became unavailable */
	u8 path_new_mask;	/* mask of paths, that became available */
	struct {
		unsigned int fast:1;	/* post with "channel end" */
		unsigned int repall:1;	/* report every interrupt status */
		unsigned int pgroup:1;	/* do path grouping */
		unsigned int force:1;	/* allow forced online */
		unsigned int mpath:1;	/* do multipathing */
	} __attribute__ ((packed)) options;
	struct {
		unsigned int esid:1;	    /* Ext. SenseID supported by HW */
		unsigned int dosense:1;	    /* delayed SENSE required */
		unsigned int doverify:1;    /* delayed path verification */
		unsigned int donotify:1;    /* call notify function */
		unsigned int recog_done:1;  /* dev. recog. complete */
		unsigned int fake_irb:2;    /* deliver faked irb */
		unsigned int resuming:1;    /* recognition while resume */
		unsigned int pgroup:1;	    /* pathgroup is set up */
		unsigned int mpath:1;	    /* multipathing is set up */
		unsigned int pgid_unknown:1;/* unknown pgid state */
		unsigned int initialized:1; /* set if initial reference held */
	} __attribute__((packed)) flags;
	unsigned long intparm;	/* user interruption parameter */
	struct qdio_irq *qdio_data;
	struct irb irb;		/* device status */
	struct senseid senseid;	/* SenseID info */
	struct pgid pgid[8];	/* path group IDs per chpid*/
	struct ccw1 iccws[2];	/* ccws for SNID/SID/SPGID commands */
	struct work_struct todo_work;
	enum cdev_todo todo;
	wait_queue_head_t wait_q;
	struct timer_list timer;
	void *cmb;			/* measurement information */
	struct list_head cmb_list;	/* list of measured devices */
	u64 cmb_start_time;		/* clock value of cmb reset */
	void *cmb_wait;			/* deferred cmb enable/disable */
	enum interruption_class int_class;
};

#endif
