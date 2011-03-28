/*
 * Orb related data structures.
 *
 * Copyright IBM Corp. 2007, 2011
 *
 * Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 *	      Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 *	      Sebastian Ott <sebott@linux.vnet.ibm.com>
 */

#ifndef S390_ORB_H
#define S390_ORB_H

/*
 * Command-mode operation request block
 */
struct cmd_orb {
	u32 intparm;	/* interruption parameter */
	u32 key:4;	/* flags, like key, suspend control, etc. */
	u32 spnd:1;	/* suspend control */
	u32 res1:1;	/* reserved */
	u32 mod:1;	/* modification control */
	u32 sync:1;	/* synchronize control */
	u32 fmt:1;	/* format control */
	u32 pfch:1;	/* prefetch control */
	u32 isic:1;	/* initial-status-interruption control */
	u32 alcc:1;	/* address-limit-checking control */
	u32 ssic:1;	/* suppress-suspended-interr. control */
	u32 res2:1;	/* reserved */
	u32 c64:1;	/* IDAW/QDIO 64 bit control  */
	u32 i2k:1;	/* IDAW 2/4kB block size control */
	u32 lpm:8;	/* logical path mask */
	u32 ils:1;	/* incorrect length */
	u32 zero:6;	/* reserved zeros */
	u32 orbx:1;	/* ORB extension control */
	u32 cpa;	/* channel program address */
}  __packed __aligned(4);

/*
 * Transport-mode operation request block
 */
struct tm_orb {
	u32 intparm;
	u32 key:4;
	u32:9;
	u32 b:1;
	u32:2;
	u32 lpm:8;
	u32:7;
	u32 x:1;
	u32 tcw;
	u32 prio:8;
	u32:8;
	u32 rsvpgm:8;
	u32:8;
	u32:32;
	u32:32;
	u32:32;
	u32:32;
}  __packed __aligned(4);

union orb {
	struct cmd_orb cmd;
	struct tm_orb tm;
}  __packed __aligned(4);

#endif /* S390_ORB_H */
