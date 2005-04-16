#ifndef _CSS_H
#define _CSS_H

#include <linux/wait.h>
#include <linux/workqueue.h>

#include <asm/cio.h>

/*
 * path grouping stuff
 */
#define SPID_FUNC_SINGLE_PATH	   0x00
#define SPID_FUNC_MULTI_PATH	   0x80
#define SPID_FUNC_ESTABLISH	   0x00
#define SPID_FUNC_RESIGN	   0x40
#define SPID_FUNC_DISBAND	   0x20

#define SNID_STATE1_RESET	   0
#define SNID_STATE1_UNGROUPED	   2
#define SNID_STATE1_GROUPED	   3

#define SNID_STATE2_NOT_RESVD	   0
#define SNID_STATE2_RESVD_ELSE	   2
#define SNID_STATE2_RESVD_SELF	   3

#define SNID_STATE3_MULTI_PATH	   1
#define SNID_STATE3_SINGLE_PATH	   0

struct path_state {
	__u8  state1 : 2;	/* path state value 1 */
	__u8  state2 : 2;	/* path state value 2 */
	__u8  state3 : 1;	/* path state value 3 */
	__u8  resvd  : 3;	/* reserved */
} __attribute__ ((packed));

struct pgid {
	union {
		__u8 fc;   	/* SPID function code */
		struct path_state ps;	/* SNID path state */
	} inf;
	__u32 cpu_addr	: 16;	/* CPU address */
	__u32 cpu_id	: 24;	/* CPU identification */
	__u32 cpu_model : 16;	/* CPU model */
	__u32 tod_high;		/* high word TOD clock */
} __attribute__ ((packed));

extern struct pgid global_pgid;

#define MAX_CIWS 8

/*
 * sense-id response buffer layout
 */
struct senseid {
	/* common part */
	__u8  reserved;     	/* always 0x'FF' */
	__u16 cu_type;	     	/* control unit type */
	__u8  cu_model;     	/* control unit model */
	__u16 dev_type;     	/* device type */
	__u8  dev_model;    	/* device model */
	__u8  unused;	     	/* padding byte */
	/* extended part */
	struct ciw ciw[MAX_CIWS];	/* variable # of CIWs */
}  __attribute__ ((packed,aligned(4)));

struct ccw_device_private {
	int state;		/* device state */
	atomic_t onoff;
	unsigned long registered;
	__u16 devno;		/* device number */
	__u16 irq;		/* subchannel number */
	__u8 imask;		/* lpm mask for SNID/SID/SPGID */
	int iretry;		/* retry counter SNID/SID/SPGID */
	struct {
		unsigned int fast:1;	/* post with "channel end" */
		unsigned int repall:1;	/* report every interrupt status */
		unsigned int pgroup:1;  /* do path grouping */
		unsigned int force:1;   /* allow forced online */
	} __attribute__ ((packed)) options;
	struct {
		unsigned int pgid_single:1; /* use single path for Set PGID */
		unsigned int esid:1;        /* Ext. SenseID supported by HW */
		unsigned int dosense:1;	    /* delayed SENSE required */
		unsigned int doverify:1;    /* delayed path verification */
		unsigned int donotify:1;    /* call notify function */
		unsigned int recog_done:1;  /* dev. recog. complete */
		unsigned int fake_irb:1;    /* deliver faked irb */
	} __attribute__((packed)) flags;
	unsigned long intparm;	/* user interruption parameter */
	struct qdio_irq *qdio_data;
	struct irb irb;		/* device status */
	struct senseid senseid;	/* SenseID info */
	struct pgid pgid;	/* path group ID */
	struct ccw1 iccws[2];	/* ccws for SNID/SID/SPGID commands */
	struct work_struct kick_work;
	wait_queue_head_t wait_q;
	struct timer_list timer;
	void *cmb;			/* measurement information */
	struct list_head cmb_list;	/* list of measured devices */
	u64 cmb_start_time;		/* clock value of cmb reset */
	void *cmb_wait;			/* deferred cmb enable/disable */
};

/*
 * A css driver handles all subchannels of one type.
 * Currently, we only care about I/O subchannels (type 0), these
 * have a ccw_device connected to them.
 */
struct css_driver {
	unsigned int subchannel_type;
	struct device_driver drv;
	void (*irq)(struct device *);
	int (*notify)(struct device *, int);
	void (*verify)(struct device *);
	void (*termination)(struct device *);
};

/*
 * all css_drivers have the css_bus_type
 */
extern struct bus_type css_bus_type;
extern struct css_driver io_subchannel_driver;

int css_probe_device(int irq);
extern struct subchannel * get_subchannel_by_schid(int irq);
extern unsigned int highest_subchannel;
extern int css_init_done;

#define __MAX_SUBCHANNELS 65536

extern struct bus_type css_bus_type;
extern struct device css_bus_device;

/* Some helper functions for disconnected state. */
int device_is_disconnected(struct subchannel *);
void device_set_disconnected(struct subchannel *);
void device_trigger_reprobe(struct subchannel *);

/* Helper functions for vary on/off. */
int device_is_online(struct subchannel *);
void device_set_waiting(struct subchannel *);

/* Machine check helper function. */
void device_kill_pending_timer(struct subchannel *);

/* Helper functions to build lists for the slow path. */
int css_enqueue_subchannel_slow(unsigned long schid);
void css_walk_subchannel_slow_list(void (*fn)(unsigned long));
void css_clear_subchannel_slow_list(void);
int css_slow_subchannels_exist(void);
extern int need_rescan;

extern struct workqueue_struct *slow_path_wq;
extern struct work_struct slow_path_work;
#endif
