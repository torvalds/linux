/*
 * jsflash.h: OS Flash SIMM support for JavaStations.
 *
 * Copyright (C) 1999  Pete Zaitcev
 */

#ifndef _SPARC_JSFLASH_H
#define _SPARC_JSFLASH_H

#ifndef _SPARC_TYPES_H
#include <linux/types.h>
#endif

/*
 * Semantics of the offset is a full address.
 * Hardcode it or get it from probe ioctl.
 *
 * We use full bus address, so that we would be
 * automatically compatible with possible future systems.
 */

#define JSFLASH_IDENT   (('F'<<8)|54)
struct jsflash_ident_arg {
	__u64 off;                /* 0x20000000 is included */
	__u32 size;
	char name[32];		/* With trailing zero */
};

#define JSFLASH_ERASE   (('F'<<8)|55)
/* Put 0 as argument, may be flags or sector number... */

#define JSFLASH_PROGRAM (('F'<<8)|56)
struct jsflash_program_arg {
	__u64 data;		/* char* for sparc and sparc64 */
	__u64 off;
	__u32 size;
};

#endif /* _SPARC_JSFLASH_H */
