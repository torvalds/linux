/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _AHA1542_H_
#define _AHA1542_H_

#include <linux/types.h>

/* I/O Port interface 4.2 */
/* READ */
#define STATUS(base) base
#define STST	BIT(7)		/* Self Test in Progress */
#define DIAGF	BIT(6)		/* Internal Diagnostic Failure */
#define INIT	BIT(5)		/* Mailbox Initialization Required */
#define IDLE	BIT(4)		/* SCSI Host Adapter Idle */
#define CDF	BIT(3)		/* Command/Data Out Port Full */
#define DF	BIT(2)		/* Data In Port Full */
/* BIT(1) is reserved */
#define INVDCMD	BIT(0)		/* Invalid H A Command */
#define STATMASK (STST | DIAGF | INIT | IDLE | CDF | DF | INVDCMD)

#define INTRFLAGS(base) (STATUS(base)+2)
#define ANYINTR	BIT(7)		/* Any Interrupt */
#define SCRD	BIT(3)		/* SCSI Reset Detected */
#define HACC	BIT(2)		/* HA Command Complete */
#define MBOA	BIT(1)		/* MBO Empty */
#define MBIF	BIT(0)		/* MBI Full */
#define INTRMASK (ANYINTR | SCRD | HACC | MBOA | MBIF)

/* WRITE */
#define CONTROL(base) STATUS(base)
#define HRST	BIT(7)		/* Hard Reset */
#define SRST	BIT(6)		/* Soft Reset */
#define IRST	BIT(5)		/* Interrupt Reset */
#define SCRST	BIT(4)		/* SCSI Bus Reset */

/* READ/WRITE */
#define DATA(base) (STATUS(base)+1)
#define CMD_NOP		0x00	/* No Operation */
#define CMD_MBINIT	0x01	/* Mailbox Initialization */
#define CMD_START_SCSI	0x02	/* Start SCSI Command */
#define CMD_INQUIRY	0x04	/* Adapter Inquiry */
#define CMD_EMBOI	0x05	/* Enable MailBox Out Interrupt */
#define CMD_BUSON_TIME	0x07	/* Set Bus-On Time */
#define CMD_BUSOFF_TIME	0x08	/* Set Bus-Off Time */
#define CMD_DMASPEED	0x09	/* Set AT Bus Transfer Speed */
#define CMD_RETDEVS	0x0a	/* Return Installed Devices */
#define CMD_RETCONF	0x0b	/* Return Configuration Data */
#define CMD_RETSETUP	0x0d	/* Return Setup Data */
#define CMD_ECHO	0x1f	/* ECHO Command Data */

#define CMD_EXTBIOS     0x28    /* Return extend bios information only 1542C */
#define CMD_MBENABLE    0x29    /* Set Mailbox Interface enable only 1542C */

/* Mailbox Definition 5.2.1 and 5.2.2 */
struct mailbox {
	u8 status;	/* Command/Status */
	u8 ccbptr[3];	/* msb, .., lsb */
};

/* This is used with scatter-gather */
struct chain {
	u8 datalen[3];	/* Size of this part of chain */
	u8 dataptr[3];	/* Location of data */
};

/* These belong in scsi.h also */
static inline void any2scsi(u8 *p, u32 v)
{
	p[0] = v >> 16;
	p[1] = v >> 8;
	p[2] = v;
}

#define scsi2int(up) ( (((long)*(up)) << 16) + (((long)(up)[1]) << 8) + ((long)(up)[2]) )

#define xscsi2int(up) ( (((long)(up)[0]) << 24) + (((long)(up)[1]) << 16) \
		      + (((long)(up)[2]) <<  8) +  ((long)(up)[3]) )

#define MAX_CDB 12
#define MAX_SENSE 14

/* Command Control Block (CCB), 5.3 */
struct ccb {
	u8 op;		/* Command Control Block Operation Code: */
			/* 0x00: SCSI Initiator CCB, 0x01: SCSI Target CCB, */
			/* 0x02: SCSI Initiator CCB with Scatter/Gather, */
			/* 0x81: SCSI Bus Device Reset CCB */
	u8 idlun;	/* Address and Direction Control: */
			/* Bits 7-5: op=0, 2: Target ID, op=1: Initiator ID */
			/* Bit	4: Outbound data transfer, length is checked */
			/* Bit	3:  Inbound data transfer, length is checked */
			/* Bits 2-0: Logical Unit Number */
	u8 cdblen;	/* SCSI Command Length */
	u8 rsalen;	/* Request Sense Allocation Length/Disable Auto Sense */
	u8 datalen[3];	/* Data Length  (MSB, ..., LSB) */
	u8 dataptr[3];	/* Data Pointer (MSB, ..., LSB) */
	u8 linkptr[3];	/* Link Pointer (MSB, ..., LSB) */
	u8 commlinkid;	/* Command Linking Identifier */
	u8 hastat;	/* Host  Adapter Status (HASTAT) */
	u8 tarstat;	/* Target Device Status (TARSTAT) */
	u8 reserved[2];
	u8 cdb[MAX_CDB + MAX_SENSE];	/* SCSI Command Descriptor Block */
					/* followed by the Auto Sense data */
};

#define AHA1542_REGION_SIZE 4
#define AHA1542_MAILBOXES 8

#endif /* _AHA1542_H_ */
