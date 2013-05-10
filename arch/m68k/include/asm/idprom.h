#ifndef _M68K_IDPROM_H
#define _M68K_IDPROM_H
/*
 * idprom.h: Macros and defines for idprom routines
 *
 * Copyright (C) 1995,1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/types.h>

struct idprom {
	u8		id_format;	/* Format identifier (always 0x01) */
	u8		id_machtype;	/* Machine type */
	u8		id_ethaddr[6];	/* Hardware ethernet address */
	s32		id_date;	/* Date of manufacture */
	u32		id_sernum:24;	/* Unique serial number */
	u8		id_cksum;	/* Checksum - xor of the data bytes */
	u8		reserved[16];
};

extern struct idprom *idprom;
extern void idprom_init(void);

/* Sun3: in control space */
#define SUN3_IDPROM_BASE	0x00000000

#endif /* !(_M68K_IDPROM_H) */
