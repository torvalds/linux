/* SPDX-License-Identifier: GPL-2.0 */
#ifndef S390_CIO_H
#define S390_CIO_H

#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <asm/chpid.h>
#include <asm/cio.h>
#include <asm/fcx.h>
#include <asm/schid.h>
#include "chsc.h"

/*
 * path management control word
 */
struct pmcw {
	u32 intparm;		/* interruption parameter */
	u32 qf	 : 1;		/* qdio facility */
	u32 w	 : 1;
	u32 isc  : 3;		/* interruption sublass */
	u32 res5 : 3;		/* reserved zeros */
	u32 ena  : 1;		/* enabled */
	u32 lm	 : 2;		/* limit mode */
	u32 mme  : 2;		/* measurement-mode enable */
	u32 mp	 : 1;		/* multipath mode */
	u32 tf	 : 1;		/* timing facility */
	u32 dnv  : 1;		/* device number valid */
	u32 dev  : 16;		/* device number */
	u8  lpm;		/* logical path mask */
	u8  pnom;		/* path not operational mask */
	u8  lpum;		/* last path used mask */
	u8  pim;		/* path installed mask */
	u16 mbi;		/* measurement-block index */
	u8  pom;		/* path operational mask */
	u8  pam;		/* path available mask */
	u8  chpid[8];		/* CHPID 0-7 (if available) */
	u32 unused1 : 8;	/* reserved zeros */
	u32 st	    : 3;	/* subchannel type */
	u32 unused2 : 18;	/* reserved zeros */
	u32 mbfc    : 1;	/* measurement block format control */
	u32 xmwme   : 1;	/* extended measurement word mode enable */
	u32 csense  : 1;	/* concurrent sense; can be enabled ...*/
				/*  ... per MSCH, however, if facility */
				/*  ... is not installed, this results */
				/*  ... in an operand exception.       */
} __attribute__ ((packed));

/* I/O-Interruption Code as stored by TEST PENDING INTERRUPTION (TPI). */
struct tpi_info {
	struct subchannel_id schid;
	u32 intparm;
	u32 adapter_IO:1;
	u32 directed_irq:1;
	u32 isc:3;
	u32 :27;
	u32 type:3;
	u32 :12;
} __packed __aligned(4);

/* Target SCHIB configuration. */
struct schib_config {
	u64 mba;
	u32 intparm;
	u16 mbi;
	u32 isc:3;
	u32 ena:1;
	u32 mme:2;
	u32 mp:1;
	u32 csense:1;
	u32 mbfc:1;
} __attribute__ ((packed));

/*
 * subchannel information block
 */
struct schib {
	struct pmcw pmcw;	 /* path management control word */
	union scsw scsw;	 /* subchannel status word */
	__u64 mba;               /* measurement block address */
	__u8 mda[4];		 /* model dependent area */
} __attribute__ ((packed,aligned(4)));

/*
 * When rescheduled, todo's with higher values will overwrite those
 * with lower values.
 */
enum sch_todo {
	SCH_TODO_NOTHING,
	SCH_TODO_EVAL,
	SCH_TODO_UNREG,
};

/* subchannel data structure used by I/O subroutines */
struct subchannel {
	struct subchannel_id schid;
	spinlock_t *lock;	/* subchannel lock */
	struct mutex reg_mutex;
	enum {
		SUBCHANNEL_TYPE_IO = 0,
		SUBCHANNEL_TYPE_CHSC = 1,
		SUBCHANNEL_TYPE_MSG = 2,
		SUBCHANNEL_TYPE_ADM = 3,
	} st;			/* subchannel type */
	__u8 vpm;		/* verified path mask */
	__u8 lpm;		/* logical path mask */
	__u8 opm;               /* operational path mask */
	struct schib schib;	/* subchannel information block */
	int isc; /* desired interruption subclass */
	struct chsc_ssd_info ssd_info;	/* subchannel description */
	struct device dev;	/* entry in device tree */
	struct css_driver *driver;
	enum sch_todo todo;
	struct work_struct todo_work;
	struct schib_config config;
} __attribute__ ((aligned(8)));

DECLARE_PER_CPU_ALIGNED(struct irb, cio_irb);

#define to_subchannel(n) container_of(n, struct subchannel, dev)

extern int cio_enable_subchannel(struct subchannel *, u32);
extern int cio_disable_subchannel (struct subchannel *);
extern int cio_cancel (struct subchannel *);
extern int cio_clear (struct subchannel *);
extern int cio_cancel_halt_clear(struct subchannel *, int *);
extern int cio_resume (struct subchannel *);
extern int cio_halt (struct subchannel *);
extern int cio_start (struct subchannel *, struct ccw1 *, __u8);
extern int cio_start_key (struct subchannel *, struct ccw1 *, __u8, __u8);
extern int cio_set_options (struct subchannel *, int);
extern int cio_update_schib(struct subchannel *sch);
extern int cio_commit_config(struct subchannel *sch);

int cio_tm_start_key(struct subchannel *sch, struct tcw *tcw, u8 lpm, u8 key);
int cio_tm_intrg(struct subchannel *sch);

/* Use with care. */
#ifdef CONFIG_CCW_CONSOLE
extern struct subchannel *cio_probe_console(void);
extern int cio_is_console(struct subchannel_id);
extern void cio_register_early_subchannels(void);
extern void cio_tsch(struct subchannel *sch);
#else
#define cio_is_console(schid) 0
static inline void cio_register_early_subchannels(void) {}
#endif

#endif
