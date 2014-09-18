/*
 * NVRAM definitions and access functions.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _UAPI_ASM_POWERPC_NVRAM_H
#define _UAPI_ASM_POWERPC_NVRAM_H

/* Signatures for nvram partitions */
#define NVRAM_SIG_SP	0x02	/* support processor */
#define NVRAM_SIG_OF	0x50	/* open firmware config */
#define NVRAM_SIG_FW	0x51	/* general firmware */
#define NVRAM_SIG_HW	0x52	/* hardware (VPD) */
#define NVRAM_SIG_FLIP	0x5a	/* Apple flip/flop header */
#define NVRAM_SIG_APPL	0x5f	/* Apple "system" (???) */
#define NVRAM_SIG_SYS	0x70	/* system env vars */
#define NVRAM_SIG_CFG	0x71	/* config data */
#define NVRAM_SIG_ELOG	0x72	/* error log */
#define NVRAM_SIG_VEND	0x7e	/* vendor defined */
#define NVRAM_SIG_FREE	0x7f	/* Free space */
#define NVRAM_SIG_OS	0xa0	/* OS defined */
#define NVRAM_SIG_PANIC	0xa1	/* Apple OSX "panic" */


/* PowerMac specific nvram stuffs */

enum {
	pmac_nvram_OF,		/* Open Firmware partition */
	pmac_nvram_XPRAM,	/* MacOS XPRAM partition */
	pmac_nvram_NR		/* MacOS Name Registry partition */
};


/* Some offsets in XPRAM */
#define PMAC_XPRAM_MACHINE_LOC	0xe4
#define PMAC_XPRAM_SOUND_VOLUME	0x08

/* Machine location structure in PowerMac XPRAM */
struct pmac_machine_location {
	unsigned int	latitude;	/* 2+30 bit Fractional number */
	unsigned int	longitude;	/* 2+30 bit Fractional number */
	unsigned int	delta;		/* mix of GMT delta and DLS */
};

/*
 * /dev/nvram ioctls
 *
 * Note that PMAC_NVRAM_GET_OFFSET is still supported, but is
 * definitely obsolete. Do not use it if you can avoid it
 */

#define OBSOLETE_PMAC_NVRAM_GET_OFFSET \
				_IOWR('p', 0x40, int)

#define IOC_NVRAM_GET_OFFSET	_IOWR('p', 0x42, int)	/* Get NVRAM partition offset */
#define IOC_NVRAM_SYNC		_IO('p', 0x43)		/* Sync NVRAM image */

#endif /* _UAPI_ASM_POWERPC_NVRAM_H */
