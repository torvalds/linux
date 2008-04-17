#ifndef _CSS_H
#define _CSS_H

#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/types.h>

#include <asm/cio.h>
#include <asm/chpid.h>

#include "schid.h"

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

/*
 * A css driver handles all subchannels of one type.
 * Currently, we only care about I/O subchannels (type 0), these
 * have a ccw_device connected to them.
 */
struct subchannel;
struct css_driver {
	struct module *owner;
	unsigned int subchannel_type;
	struct device_driver drv;
	void (*irq)(struct subchannel *);
	int (*notify)(struct subchannel *, int);
	void (*verify)(struct subchannel *);
	void (*termination)(struct subchannel *);
	int (*probe)(struct subchannel *);
	int (*remove)(struct subchannel *);
	void (*shutdown)(struct subchannel *);
	const char *name;
};

#define to_cssdriver(n) container_of(n, struct css_driver, drv)

/*
 * all css_drivers have the css_bus_type
 */
extern struct bus_type css_bus_type;

extern int css_driver_register(struct css_driver *);
extern void css_driver_unregister(struct css_driver *);

extern void css_sch_device_unregister(struct subchannel *);
extern struct subchannel * get_subchannel_by_schid(struct subchannel_id);
extern int css_init_done;
int for_each_subchannel_staged(int (*fn_known)(struct subchannel *, void *),
			       int (*fn_unknown)(struct subchannel_id,
			       void *), void *data);
extern int for_each_subchannel(int(*fn)(struct subchannel_id, void *), void *);
extern void css_process_crw(int, int);
extern void css_reiterate_subchannels(void);
void css_update_ssd_info(struct subchannel *sch);

#define __MAX_SUBCHANNEL 65535
#define __MAX_SSID 3

struct channel_subsystem {
	u8 cssid;
	int valid;
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

extern struct bus_type css_bus_type;
extern struct channel_subsystem *channel_subsystems[];

/* Some helper functions for disconnected state. */
int device_is_disconnected(struct subchannel *);
void device_set_disconnected(struct subchannel *);
void device_trigger_reprobe(struct subchannel *);

/* Helper functions for vary on/off. */
int device_is_online(struct subchannel *);
void device_kill_io(struct subchannel *);
void device_set_intretry(struct subchannel *sch);
int device_trigger_verify(struct subchannel *sch);

/* Machine check helper function. */
void device_kill_pending_timer(struct subchannel *);

/* Helper functions to build lists for the slow path. */
void css_schedule_eval(struct subchannel_id schid);
void css_schedule_eval_all(void);

int sch_is_pseudo_sch(struct subchannel *);
struct schib;
int css_sch_is_valid(struct schib *);

extern struct workqueue_struct *slow_path_wq;
void css_wait_for_slow_path(void);

extern struct attribute_group *subch_attr_groups[];
#endif
