#ifndef S390_CIO_H
#define S390_CIO_H

#include "schid.h"
#include <linux/mutex.h>

/*
 * where we put the ssd info
 */
struct ssd_info {
	__u8  valid:1;
	__u8  type:7;		/* subchannel type */
	__u8  chpid[8];		/* chpids */
	__u16 fla[8];		/* full link addresses */
} __attribute__ ((packed));

/*
 * path management control word
 */
struct pmcw {
	__u32 intparm;		/* interruption parameter */
	__u32 qf   : 1;		/* qdio facility */
	__u32 res0 : 1;		/* reserved zeros */
	__u32 isc  : 3;		/* interruption sublass */
	__u32 res5 : 3;		/* reserved zeros */
	__u32 ena  : 1;		/* enabled */
	__u32 lm   : 2;		/* limit mode */
	__u32 mme  : 2;		/* measurement-mode enable */
	__u32 mp   : 1;		/* multipath mode */
	__u32 tf   : 1;		/* timing facility */
	__u32 dnv  : 1;		/* device number valid */
	__u32 dev  : 16;	/* device number */
	__u8  lpm;		/* logical path mask */
	__u8  pnom;		/* path not operational mask */
	__u8  lpum;		/* last path used mask */
	__u8  pim;		/* path installed mask */
	__u16 mbi;		/* measurement-block index */
	__u8  pom;		/* path operational mask */
	__u8  pam;		/* path available mask */
	__u8  chpid[8];		/* CHPID 0-7 (if available) */
	__u32 unused1 : 8;	/* reserved zeros */
	__u32 st      : 3;	/* subchannel type */
	__u32 unused2 : 18;	/* reserved zeros */
	__u32 mbfc    : 1;      /* measurement block format control */
	__u32 xmwme   : 1;      /* extended measurement word mode enable */
	__u32 csense  : 1;	/* concurrent sense; can be enabled ...*/
				/*  ... per MSCH, however, if facility */
				/*  ... is not installed, this results */
				/*  ... in an operand exception.       */
} __attribute__ ((packed));

/*
 * subchannel information block
 */
struct schib {
	struct pmcw pmcw;	 /* path management control word */
	struct scsw scsw;	 /* subchannel status word */
	__u64 mba;               /* measurement block address */
	__u8 mda[4];		 /* model dependent area */
} __attribute__ ((packed,aligned(4)));

/*
 * operation request block
 */
struct orb {
	__u32 intparm;		/* interruption parameter */
	__u32 key  : 4; 	/* flags, like key, suspend control, etc. */
	__u32 spnd : 1; 	/* suspend control */
	__u32 res1 : 1; 	/* reserved */
	__u32 mod  : 1; 	/* modification control */
	__u32 sync : 1; 	/* synchronize control */
	__u32 fmt  : 1; 	/* format control */
	__u32 pfch : 1; 	/* prefetch control */
	__u32 isic : 1; 	/* initial-status-interruption control */
	__u32 alcc : 1; 	/* address-limit-checking control */
	__u32 ssic : 1; 	/* suppress-suspended-interr. control */
	__u32 res2 : 1; 	/* reserved */
	__u32 c64  : 1; 	/* IDAW/QDIO 64 bit control  */
	__u32 i2k  : 1; 	/* IDAW 2/4kB block size control */
	__u32 lpm  : 8; 	/* logical path mask */
	__u32 ils  : 1; 	/* incorrect length */
	__u32 zero : 6; 	/* reserved zeros */
	__u32 orbx : 1; 	/* ORB extension control */
	__u32 cpa;		/* channel program address */
}  __attribute__ ((packed,aligned(4)));

/* subchannel data structure used by I/O subroutines */
struct subchannel {
	struct subchannel_id schid;
	spinlock_t *lock;	/* subchannel lock */
	struct mutex reg_mutex;
	enum {
		SUBCHANNEL_TYPE_IO = 0,
		SUBCHANNEL_TYPE_CHSC = 1,
		SUBCHANNEL_TYPE_MESSAGE = 2,
		SUBCHANNEL_TYPE_ADM = 3,
	} st;			/* subchannel type */

	struct {
		unsigned int suspend:1; /* allow suspend */
		unsigned int prefetch:1;/* deny prefetch */
		unsigned int inter:1;   /* suppress intermediate interrupts */
	} __attribute__ ((packed)) options;

	__u8 vpm;		/* verified path mask */
	__u8 lpm;		/* logical path mask */
	__u8 opm;               /* operational path mask */
	struct schib schib;	/* subchannel information block */
	struct orb orb;		/* operation request block */
	struct ccw1 sense_ccw;	/* static ccw for sense command */
	struct ssd_info ssd_info;	/* subchannel description */
	struct device dev;	/* entry in device tree */
	struct css_driver *driver;
} __attribute__ ((aligned(8)));

#define IO_INTERRUPT_TYPE	   0 /* I/O interrupt type */

#define to_subchannel(n) container_of(n, struct subchannel, dev)

extern int cio_validate_subchannel (struct subchannel *, struct subchannel_id);
extern int cio_enable_subchannel (struct subchannel *, unsigned int);
extern int cio_disable_subchannel (struct subchannel *);
extern int cio_cancel (struct subchannel *);
extern int cio_clear (struct subchannel *);
extern int cio_resume (struct subchannel *);
extern int cio_halt (struct subchannel *);
extern int cio_start (struct subchannel *, struct ccw1 *, __u8);
extern int cio_start_key (struct subchannel *, struct ccw1 *, __u8, __u8);
extern int cio_cancel (struct subchannel *);
extern int cio_set_options (struct subchannel *, int);
extern int cio_get_options (struct subchannel *);
extern int cio_modify (struct subchannel *);

/* Use with care. */
#ifdef CONFIG_CCW_CONSOLE
extern struct subchannel *cio_probe_console(void);
extern void cio_release_console(void);
extern int cio_is_console(struct subchannel_id);
extern struct subchannel *cio_get_console_subchannel(void);
extern spinlock_t * cio_get_console_lock(void);
#else
#define cio_is_console(schid) 0
#define cio_get_console_subchannel() NULL
#define cio_get_console_lock() NULL;
#endif

extern int cio_show_msg;

#endif
