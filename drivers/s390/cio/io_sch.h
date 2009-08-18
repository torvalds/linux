#ifndef S390_IO_SCH_H
#define S390_IO_SCH_H

#include <asm/schid.h>

/*
 * command-mode operation request block
 */
struct cmd_orb {
	u32 intparm;	/* interruption parameter */
	u32 key  : 4;	/* flags, like key, suspend control, etc. */
	u32 spnd : 1;	/* suspend control */
	u32 res1 : 1;	/* reserved */
	u32 mod  : 1;	/* modification control */
	u32 sync : 1;	/* synchronize control */
	u32 fmt  : 1;	/* format control */
	u32 pfch : 1;	/* prefetch control */
	u32 isic : 1;	/* initial-status-interruption control */
	u32 alcc : 1;	/* address-limit-checking control */
	u32 ssic : 1;	/* suppress-suspended-interr. control */
	u32 res2 : 1;	/* reserved */
	u32 c64  : 1;	/* IDAW/QDIO 64 bit control  */
	u32 i2k  : 1;	/* IDAW 2/4kB block size control */
	u32 lpm  : 8;	/* logical path mask */
	u32 ils  : 1;	/* incorrect length */
	u32 zero : 6;	/* reserved zeros */
	u32 orbx : 1;	/* ORB extension control */
	u32 cpa;	/* channel program address */
}  __attribute__ ((packed, aligned(4)));

/*
 * transport-mode operation request block
 */
struct tm_orb {
	u32 intparm;
	u32 key:4;
	u32 :9;
	u32 b:1;
	u32 :2;
	u32 lpm:8;
	u32 :7;
	u32 x:1;
	u32 tcw;
	u32 prio:8;
	u32 :8;
	u32 rsvpgm:8;
	u32 :8;
	u32 :32;
	u32 :32;
	u32 :32;
	u32 :32;
}  __attribute__ ((packed, aligned(4)));

union orb {
	struct cmd_orb cmd;
	struct tm_orb tm;
}  __attribute__ ((packed, aligned(4)));

struct io_subchannel_private {
	union orb orb;		/* operation request block */
	struct ccw1 sense_ccw;	/* static ccw for sense command */
} __attribute__ ((aligned(8)));

#define to_io_private(n) ((struct io_subchannel_private *)n->private)
#define sch_get_cdev(n) (dev_get_drvdata(&n->dev))
#define sch_set_cdev(n, c) (dev_set_drvdata(&n->dev, c))

#define MAX_CIWS 8

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

struct ccw_device_private {
	struct ccw_device *cdev;
	struct subchannel *sch;
	int state;		/* device state */
	atomic_t onoff;
	unsigned long registered;
	struct ccw_dev_id dev_id;	/* device id */
	struct subchannel_id schid;	/* subchannel number */
	u8 imask;		/* lpm mask for SNID/SID/SPGID */
	int iretry;		/* retry counter SNID/SID/SPGID */
	struct {
		unsigned int fast:1;	/* post with "channel end" */
		unsigned int repall:1;	/* report every interrupt status */
		unsigned int pgroup:1;	/* do path grouping */
		unsigned int force:1;	/* allow forced online */
	} __attribute__ ((packed)) options;
	struct {
		unsigned int pgid_single:1; /* use single path for Set PGID */
		unsigned int esid:1;	    /* Ext. SenseID supported by HW */
		unsigned int dosense:1;	    /* delayed SENSE required */
		unsigned int doverify:1;    /* delayed path verification */
		unsigned int donotify:1;    /* call notify function */
		unsigned int recog_done:1;  /* dev. recog. complete */
		unsigned int fake_irb:1;    /* deliver faked irb */
		unsigned int intretry:1;    /* retry internal operation */
		unsigned int resuming:1;    /* recognition while resume */
	} __attribute__((packed)) flags;
	unsigned long intparm;	/* user interruption parameter */
	struct qdio_irq *qdio_data;
	struct irb irb;		/* device status */
	struct senseid senseid;	/* SenseID info */
	struct pgid pgid[8];	/* path group IDs per chpid*/
	struct ccw1 iccws[2];	/* ccws for SNID/SID/SPGID commands */
	struct work_struct kick_work;
	wait_queue_head_t wait_q;
	struct timer_list timer;
	void *cmb;			/* measurement information */
	struct list_head cmb_list;	/* list of measured devices */
	u64 cmb_start_time;		/* clock value of cmb reset */
	void *cmb_wait;			/* deferred cmb enable/disable */
};

static inline int ssch(struct subchannel_id schid, union orb *addr)
{
	register struct subchannel_id reg1 asm("1") = schid;
	int ccode = -EIO;

	asm volatile(
		"	ssch	0(%2)\n"
		"0:	ipm	%0\n"
		"	srl	%0,28\n"
		"1:\n"
		EX_TABLE(0b, 1b)
		: "+d" (ccode)
		: "d" (reg1), "a" (addr), "m" (*addr)
		: "cc", "memory");
	return ccode;
}

static inline int rsch(struct subchannel_id schid)
{
	register struct subchannel_id reg1 asm("1") = schid;
	int ccode;

	asm volatile(
		"	rsch\n"
		"	ipm	%0\n"
		"	srl	%0,28"
		: "=d" (ccode)
		: "d" (reg1)
		: "cc", "memory");
	return ccode;
}

static inline int csch(struct subchannel_id schid)
{
	register struct subchannel_id reg1 asm("1") = schid;
	int ccode;

	asm volatile(
		"	csch\n"
		"	ipm	%0\n"
		"	srl	%0,28"
		: "=d" (ccode)
		: "d" (reg1)
		: "cc");
	return ccode;
}

static inline int hsch(struct subchannel_id schid)
{
	register struct subchannel_id reg1 asm("1") = schid;
	int ccode;

	asm volatile(
		"	hsch\n"
		"	ipm	%0\n"
		"	srl	%0,28"
		: "=d" (ccode)
		: "d" (reg1)
		: "cc");
	return ccode;
}

static inline int xsch(struct subchannel_id schid)
{
	register struct subchannel_id reg1 asm("1") = schid;
	int ccode;

	asm volatile(
		"	.insn	rre,0xb2760000,%1,0\n"
		"	ipm	%0\n"
		"	srl	%0,28"
		: "=d" (ccode)
		: "d" (reg1)
		: "cc");
	return ccode;
}

#endif
