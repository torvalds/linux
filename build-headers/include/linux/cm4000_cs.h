/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _CM4000_H_
#define _CM4000_H_

#include <linux/types.h>
#include <linux/ioctl.h>

#define	MAX_ATR			33

#define	CM4000_MAX_DEV		4

/* those two structures are passed via ioctl() from/to userspace.  They are
 * used by existing userspace programs, so I kepth the awkward "bIFSD" naming
 * not to break compilation of userspace apps. -HW */

typedef struct atreq {
	__s32 atr_len;
	unsigned char atr[64];
	__s32 power_act;
	unsigned char bIFSD;
	unsigned char bIFSC;
} atreq_t;


/* what is particularly stupid in the original driver is the arch-dependent
 * member sizes. This leads to CONFIG_COMPAT breakage, since 32bit userspace
 * will lay out the structure members differently than the 64bit kernel.
 *
 * I've changed "ptsreq.protocol" from "unsigned long" to "__u32".
 * On 32bit this will make no difference.  With 64bit kernels, it will make
 * 32bit apps work, too.
 */

typedef struct ptsreq {
	__u32 protocol; /*T=0: 2^0, T=1:  2^1*/
 	unsigned char flags;
 	unsigned char pts1;
 	unsigned char pts2;
	unsigned char pts3;
} ptsreq_t;

#define	CM_IOC_MAGIC		'c'
#define	CM_IOC_MAXNR	        255

#define	CM_IOCGSTATUS		_IOR (CM_IOC_MAGIC, 0, unsigned char *)
#define	CM_IOCGATR		_IOWR(CM_IOC_MAGIC, 1, atreq_t *)
#define	CM_IOCSPTS		_IOW (CM_IOC_MAGIC, 2, ptsreq_t *)
#define	CM_IOCSRDR		_IO  (CM_IOC_MAGIC, 3)
#define CM_IOCARDOFF            _IO  (CM_IOC_MAGIC, 4)

#define CM_IOSDBGLVL            _IOW(CM_IOC_MAGIC, 250, int*)

/* card and device states */
#define	CM_CARD_INSERTED		0x01
#define	CM_CARD_POWERED			0x02
#define	CM_ATR_PRESENT			0x04
#define	CM_ATR_VALID	 		0x08
#define	CM_STATE_VALID			0x0f
/* extra info only from CM4000 */
#define	CM_NO_READER			0x10
#define	CM_BAD_CARD			0x20


#endif /* _CM4000_H_ */
