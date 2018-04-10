/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common interface for I/O on S/390
 */
#ifndef _ASM_S390_CIO_H_
#define _ASM_S390_CIO_H_

#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <asm/types.h>

#define LPM_ANYPATH 0xff
#define __MAX_CSSID 0
#define __MAX_SUBCHANNEL 65535
#define __MAX_SSID 3

#include <asm/scsw.h>

/**
 * struct ccw1 - channel command word
 * @cmd_code: command code
 * @flags: flags, like IDA addressing, etc.
 * @count: byte count
 * @cda: data address
 *
 * The ccw is the basic structure to build channel programs that perform
 * operations with the device or the control unit. Only Format-1 channel
 * command words are supported.
 */
struct ccw1 {
	__u8  cmd_code;
	__u8  flags;
	__u16 count;
	__u32 cda;
} __attribute__ ((packed,aligned(8)));

/**
 * struct ccw0 - channel command word
 * @cmd_code: command code
 * @cda: data address
 * @flags: flags, like IDA addressing, etc.
 * @reserved: will be ignored
 * @count: byte count
 *
 * The format-0 ccw structure.
 */
struct ccw0 {
	__u8 cmd_code;
	__u32 cda : 24;
	__u8  flags;
	__u8  reserved;
	__u16 count;
} __packed __aligned(8);

#define CCW_FLAG_DC		0x80
#define CCW_FLAG_CC		0x40
#define CCW_FLAG_SLI		0x20
#define CCW_FLAG_SKIP		0x10
#define CCW_FLAG_PCI		0x08
#define CCW_FLAG_IDA		0x04
#define CCW_FLAG_SUSPEND	0x02

#define CCW_CMD_READ_IPL	0x02
#define CCW_CMD_NOOP		0x03
#define CCW_CMD_BASIC_SENSE	0x04
#define CCW_CMD_TIC		0x08
#define CCW_CMD_STLCK           0x14
#define CCW_CMD_SENSE_PGID	0x34
#define CCW_CMD_SUSPEND_RECONN	0x5B
#define CCW_CMD_RDC		0x64
#define CCW_CMD_RELEASE		0x94
#define CCW_CMD_SET_PGID	0xAF
#define CCW_CMD_SENSE_ID	0xE4
#define CCW_CMD_DCTL		0xF3

#define SENSE_MAX_COUNT		0x20

/**
 * struct erw - extended report word
 * @res0: reserved
 * @auth: authorization check
 * @pvrf: path-verification-required flag
 * @cpt: channel-path timeout
 * @fsavf: failing storage address validity flag
 * @cons: concurrent sense
 * @scavf: secondary ccw address validity flag
 * @fsaf: failing storage address format
 * @scnt: sense count, if @cons == %1
 * @res16: reserved
 */
struct erw {
	__u32 res0  : 3;
	__u32 auth  : 1;
	__u32 pvrf  : 1;
	__u32 cpt   : 1;
	__u32 fsavf : 1;
	__u32 cons  : 1;
	__u32 scavf : 1;
	__u32 fsaf  : 1;
	__u32 scnt  : 6;
	__u32 res16 : 16;
} __attribute__ ((packed));

/**
 * struct erw_eadm - EADM Subchannel extended report word
 * @b: aob error
 * @r: arsb error
 */
struct erw_eadm {
	__u32 : 16;
	__u32 b : 1;
	__u32 r : 1;
	__u32  : 14;
} __packed;

/**
 * struct sublog - subchannel logout area
 * @res0: reserved
 * @esf: extended status flags
 * @lpum: last path used mask
 * @arep: ancillary report
 * @fvf: field-validity flags
 * @sacc: storage access code
 * @termc: termination code
 * @devsc: device-status check
 * @serr: secondary error
 * @ioerr: i/o-error alert
 * @seqc: sequence code
 */
struct sublog {
	__u32 res0  : 1;
	__u32 esf   : 7;
	__u32 lpum  : 8;
	__u32 arep  : 1;
	__u32 fvf   : 5;
	__u32 sacc  : 2;
	__u32 termc : 2;
	__u32 devsc : 1;
	__u32 serr  : 1;
	__u32 ioerr : 1;
	__u32 seqc  : 3;
} __attribute__ ((packed));

/**
 * struct esw0 - Format 0 Extended Status Word (ESW)
 * @sublog: subchannel logout
 * @erw: extended report word
 * @faddr: failing storage address
 * @saddr: secondary ccw address
 */
struct esw0 {
	struct sublog sublog;
	struct erw erw;
	__u32  faddr[2];
	__u32  saddr;
} __attribute__ ((packed));

/**
 * struct esw1 - Format 1 Extended Status Word (ESW)
 * @zero0: reserved zeros
 * @lpum: last path used mask
 * @zero16: reserved zeros
 * @erw: extended report word
 * @zeros: three fullwords of zeros
 */
struct esw1 {
	__u8  zero0;
	__u8  lpum;
	__u16 zero16;
	struct erw erw;
	__u32 zeros[3];
} __attribute__ ((packed));

/**
 * struct esw2 - Format 2 Extended Status Word (ESW)
 * @zero0: reserved zeros
 * @lpum: last path used mask
 * @dcti: device-connect-time interval
 * @erw: extended report word
 * @zeros: three fullwords of zeros
 */
struct esw2 {
	__u8  zero0;
	__u8  lpum;
	__u16 dcti;
	struct erw erw;
	__u32 zeros[3];
} __attribute__ ((packed));

/**
 * struct esw3 - Format 3 Extended Status Word (ESW)
 * @zero0: reserved zeros
 * @lpum: last path used mask
 * @res: reserved
 * @erw: extended report word
 * @zeros: three fullwords of zeros
 */
struct esw3 {
	__u8  zero0;
	__u8  lpum;
	__u16 res;
	struct erw erw;
	__u32 zeros[3];
} __attribute__ ((packed));

/**
 * struct esw_eadm - EADM Subchannel Extended Status Word (ESW)
 * @sublog: subchannel logout
 * @erw: extended report word
 */
struct esw_eadm {
	__u32 sublog;
	struct erw_eadm erw;
	__u32 : 32;
	__u32 : 32;
	__u32 : 32;
} __packed;

/**
 * struct irb - interruption response block
 * @scsw: subchannel status word
 * @esw: extended status word
 * @ecw: extended control word
 *
 * The irb that is handed to the device driver when an interrupt occurs. For
 * solicited interrupts, the common I/O layer already performs checks whether
 * a field is valid; a field not being valid is always passed as %0.
 * If a unit check occurred, @ecw may contain sense data; this is retrieved
 * by the common I/O layer itself if the device doesn't support concurrent
 * sense (so that the device driver never needs to perform basic sense itself).
 * For unsolicited interrupts, the irb is passed as-is (expect for sense data,
 * if applicable).
 */
struct irb {
	union scsw scsw;
	union {
		struct esw0 esw0;
		struct esw1 esw1;
		struct esw2 esw2;
		struct esw3 esw3;
		struct esw_eadm eadm;
	} esw;
	__u8   ecw[32];
} __attribute__ ((packed,aligned(4)));

/**
 * struct ciw - command information word  (CIW) layout
 * @et: entry type
 * @reserved: reserved bits
 * @ct: command type
 * @cmd: command code
 * @count: command count
 */
struct ciw {
	__u32 et       :  2;
	__u32 reserved :  2;
	__u32 ct       :  4;
	__u32 cmd      :  8;
	__u32 count    : 16;
} __attribute__ ((packed));

#define CIW_TYPE_RCD	0x0    	/* read configuration data */
#define CIW_TYPE_SII	0x1    	/* set interface identifier */
#define CIW_TYPE_RNI	0x2    	/* read node identifier */

/*
 * Flags used as input parameters for do_IO()
 */
#define DOIO_ALLOW_SUSPEND	 0x0001 /* allow for channel prog. suspend */
#define DOIO_DENY_PREFETCH	 0x0002 /* don't allow for CCW prefetch */
#define DOIO_SUPPRESS_INTER	 0x0004 /* suppress intermediate inter. */
					/* ... for suspended CCWs */
/* Device or subchannel gone. */
#define CIO_GONE       0x0001
/* No path to device. */
#define CIO_NO_PATH    0x0002
/* Device has appeared. */
#define CIO_OPER       0x0004
/* Sick revalidation of device. */
#define CIO_REVALIDATE 0x0008
/* Device did not respond in time. */
#define CIO_BOXED      0x0010

/**
 * struct ccw_dev_id - unique identifier for ccw devices
 * @ssid: subchannel set id
 * @devno: device number
 *
 * This structure is not directly based on any hardware structure. The
 * hardware identifies a device by its device number and its subchannel,
 * which is in turn identified by its id. In order to get a unique identifier
 * for ccw devices across subchannel sets, @struct ccw_dev_id has been
 * introduced.
 */
struct ccw_dev_id {
	u8 ssid;
	u16 devno;
};

/**
 * ccw_device_id_is_equal() - compare two ccw_dev_ids
 * @dev_id1: a ccw_dev_id
 * @dev_id2: another ccw_dev_id
 * Returns:
 *  %1 if the two structures are equal field-by-field,
 *  %0 if not.
 * Context:
 *  any
 */
static inline int ccw_dev_id_is_equal(struct ccw_dev_id *dev_id1,
				      struct ccw_dev_id *dev_id2)
{
	if ((dev_id1->ssid == dev_id2->ssid) &&
	    (dev_id1->devno == dev_id2->devno))
		return 1;
	return 0;
}

/**
 * pathmask_to_pos() - find the position of the left-most bit in a pathmask
 * @mask: pathmask with at least one bit set
 */
static inline u8 pathmask_to_pos(u8 mask)
{
	return 8 - ffs(mask);
}

void channel_subsystem_reinit(void);
extern void css_schedule_reprobe(void);

extern void reipl_ccw_dev(struct ccw_dev_id *id);

struct cio_iplinfo {
	u8 ssid;
	u16 devno;
	int is_qdio;
};

extern int cio_get_iplinfo(struct cio_iplinfo *iplinfo);

/* Function from drivers/s390/cio/chsc.c */
int chsc_sstpc(void *page, unsigned int op, u16 ctrl, u64 *clock_delta);
int chsc_sstpi(void *page, void *result, size_t size);

#endif
