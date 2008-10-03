/*
 *  drivers/s390/cio/chpid.h
 *
 *    Copyright IBM Corp. 2007
 *    Author(s): Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#ifndef _ASM_S390_CHPID_H
#define _ASM_S390_CHPID_H _ASM_S390_CHPID_H

#include <linux/string.h>
#include <asm/types.h>

#define __MAX_CHPID 255

struct chp_id {
	u8 reserved1;
	u8 cssid;
	u8 reserved2;
	u8 id;
} __attribute__((packed));

#ifdef __KERNEL__
#include <asm/cio.h>

static inline void chp_id_init(struct chp_id *chpid)
{
	memset(chpid, 0, sizeof(struct chp_id));
}

static inline int chp_id_is_equal(struct chp_id *a, struct chp_id *b)
{
	return (a->id == b->id) && (a->cssid == b->cssid);
}

static inline void chp_id_next(struct chp_id *chpid)
{
	if (chpid->id < __MAX_CHPID)
		chpid->id++;
	else {
		chpid->id = 0;
		chpid->cssid++;
	}
}

static inline int chp_id_is_valid(struct chp_id *chpid)
{
	return (chpid->cssid <= __MAX_CSSID);
}


#define chp_id_for_each(c) \
	for (chp_id_init(c); chp_id_is_valid(c); chp_id_next(c))
#endif /* __KERNEL */

#endif /* _ASM_S390_CHPID_H */
