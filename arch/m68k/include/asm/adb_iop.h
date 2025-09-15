/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ADB through the IOP
 * Written by Joshua M. Thompson
 */

/* IOP number and channel number for ADB */

#define ADB_IOP		IOP_NUM_ISM
#define ADB_CHAN	2

/* From the A/UX headers...maybe important, maybe not */

#define ADB_IOP_LISTEN	0x01
#define ADB_IOP_TALK	0x02
#define ADB_IOP_EXISTS	0x04
#define ADB_IOP_FLUSH	0x08
#define ADB_IOP_RESET	0x10
#define ADB_IOP_INT	0x20
#define ADB_IOP_POLL	0x40
#define ADB_IOP_UNINT	0x80

#define AIF_RESET	0x00
#define AIF_FLUSH	0x01
#define AIF_LISTEN	0x08
#define AIF_TALK	0x0C

/* Flag bits in struct adb_iopmsg */

#define ADB_IOP_EXPLICIT	0x80	/* nonzero if explicit command */
#define ADB_IOP_AUTOPOLL	0x40	/* auto/SRQ polling enabled    */
#define ADB_IOP_SET_AUTOPOLL	0x20	/* set autopoll device list    */
#define ADB_IOP_SRQ		0x04	/* SRQ detected                */
#define ADB_IOP_TIMEOUT		0x02	/* nonzero if timeout          */

#ifndef __ASSEMBLER__

struct adb_iopmsg {
	__u8 flags;		/* ADB flags         */
	__u8 count;		/* no. of data bytes */
	__u8 cmd;		/* ADB command       */
	__u8 data[8];		/* ADB data          */
	__u8 spare[21];		/* spare             */
};

#endif /* __ASSEMBLER__ */
