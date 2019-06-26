/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CSS_H
#define _CSS_H

#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/types.h>

#include <asm/cio.h>
#include <asm/chpid.h>
#include <asm/schid.h>

#include "cio.h"

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

struct extended_cssid {
	u8 version;
	u8 cssid;
} __attribute__ ((packed));

struct pgid {
	union {
		__u8 fc;   	/* SPID function code */
		struct path_state ps;	/* SNID path state */
	} __attribute__ ((packed)) inf;
	union {
		__u32 cpu_addr	: 16;	/* CPU address */
		struct extended_cssid ext_cssid;
	} __attribute__ ((packed)) pgid_high;
	__u32 cpu_id	: 24;	/* CPU identification */
	__u32 cpu_model : 16;	/* CPU model */
	__u32 tod_high;		/* high word TOD clock */
} __attribute__ ((packed));

struct subchannel;
struct chp_link;
/**
 * struct css_driver - device driver for subchannels
 * @subchannel_type: subchannel type supported by this driver
 * @drv: embedded device driver structure
 * @irq: called on interrupts
 * @chp_event: called for events affecting a channel path
 * @sch_event: called for events affecting the subchannel
 * @probe: function called on probe
 * @remove: function called on remove
 * @shutdown: called at device shutdown
 * @prepare: prepare for pm state transition
 * @complete: undo work done in @prepare
 * @freeze: callback for freezing during hibernation snapshotting
 * @thaw: undo work done in @freeze
 * @restore: callback for restoring after hibernation
 * @settle: wait for asynchronous work to finish
 */
struct css_driver {
	struct css_device_id *subchannel_type;
	struct device_driver drv;
	void (*irq)(struct subchannel *);
	int (*chp_event)(struct subchannel *, struct chp_link *, int);
	int (*sch_event)(struct subchannel *, int);
	int (*probe)(struct subchannel *);
	int (*remove)(struct subchannel *);
	void (*shutdown)(struct subchannel *);
	int (*prepare) (struct subchannel *);
	void (*complete) (struct subchannel *);
	int (*freeze)(struct subchannel *);
	int (*thaw) (struct subchannel *);
	int (*restore)(struct subchannel *);
	int (*settle)(void);
};

#define to_cssdriver(n) container_of(n, struct css_driver, drv)

extern int css_driver_register(struct css_driver *);
extern void css_driver_unregister(struct css_driver *);

extern void css_sch_device_unregister(struct subchannel *);
extern int css_register_subchannel(struct subchannel *);
extern struct subchannel *css_alloc_subchannel(struct subchannel_id,
					       struct schib *schib);
extern struct subchannel *get_subchannel_by_schid(struct subchannel_id);
extern int css_init_done;
extern int max_ssid;
int for_each_subchannel_staged(int (*fn_known)(struct subchannel *, void *),
			       int (*fn_unknown)(struct subchannel_id,
			       void *), void *data);
extern int for_each_subchannel(int(*fn)(struct subchannel_id, void *), void *);
void css_update_ssd_info(struct subchannel *sch);

struct channel_subsystem {
	int cssid;
	struct channel_path *chps[__MAX_CHPID + 1];
	struct device device;
	struct pgid global_pgid;
	struct mutex mutex;
	/* channel measurement related */
	int cm_enabled;
	void *cub_addr1;
	void *cub_addr2;
	/* for orphaned ccw devices */
	struct subchannel *pseudo_subchannel;
};
#define to_css(dev) container_of(dev, struct channel_subsystem, device)

extern struct channel_subsystem *channel_subsystems[];

/* Dummy helper which needs to change once we support more than one css. */
static inline struct channel_subsystem *css_by_id(u8 cssid)
{
	return channel_subsystems[0];
}

/* Dummy iterator which needs to change once we support more than one css. */
#define for_each_css(css)						\
	for ((css) = channel_subsystems[0]; (css); (css) = NULL)

/* Helper functions to build lists for the slow path. */
void css_schedule_eval(struct subchannel_id schid);
void css_schedule_eval_all(void);
void css_schedule_eval_all_unreg(unsigned long delay);
int css_complete_work(void);

int sch_is_pseudo_sch(struct subchannel *);
struct schib;
int css_sch_is_valid(struct schib *);

extern struct workqueue_struct *cio_work_q;
void css_wait_for_slow_path(void);
void css_sched_sch_todo(struct subchannel *sch, enum sch_todo todo);
#endif
