#ifndef _AHA1542_H

/* $Id: aha1542.h,v 1.1 1992/07/24 06:27:38 root Exp root $
 *
 * Header file for the adaptec 1542 driver for Linux
 *
 * $Log: aha1542.h,v $
 * Revision 1.1  1992/07/24  06:27:38  root
 * Initial revision
 *
 * Revision 1.2  1992/07/04  18:41:49  root
 * Replaced distribution with current drivers
 *
 * Revision 1.3  1992/06/23  23:58:20  root
 * Fixes.
 *
 * Revision 1.2  1992/05/26  22:13:23  root
 * Changed bug that prevented DMA above first 2 mbytes.
 *
 * Revision 1.1  1992/05/22  21:00:29  root
 * Initial revision
 *
 * Revision 1.1  1992/04/24  18:01:50  root
 * Initial revision
 *
 * Revision 1.1  1992/04/02  03:23:13  drew
 * Initial revision
 *
 * Revision 1.3  1992/01/27  14:46:29  tthorn
 * *** empty log message ***
 *
 */

#include <linux/types.h>

/* I/O Port interface 4.2 */
/* READ */
#define STATUS(base) base
#define STST	0x80		/* Self Test in Progress */
#define DIAGF	0x40		/* Internal Diagnostic Failure */
#define INIT	0x20		/* Mailbox Initialization Required */
#define IDLE	0x10		/* SCSI Host Adapter Idle */
#define CDF	0x08		/* Command/Data Out Port Full */
#define DF	0x04		/* Data In Port Full */
#define INVDCMD	0x01		/* Invalid H A Command */
#define STATMASK 0xfd		/* 0x02 is reserved */

#define INTRFLAGS(base) (STATUS(base)+2)
#define ANYINTR	0x80		/* Any Interrupt */
#define SCRD	0x08		/* SCSI Reset Detected */
#define HACC	0x04		/* HA Command Complete */
#define MBOA	0x02		/* MBO Empty */
#define MBIF	0x01		/* MBI Full */
#define INTRMASK 0x8f

/* WRITE */
#define CONTROL(base) STATUS(base)
#define HRST	0x80		/* Hard Reset */
#define SRST	0x40		/* Soft Reset */
#define IRST	0x20		/* Interrupt Reset */
#define SCRST	0x10		/* SCSI Bus Reset */

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
  unchar status;		/* Command/Status */
  unchar ccbptr[3];		/* msb, .., lsb */
};

/* This is used with scatter-gather */
struct chain {
  unchar datalen[3];		/* Size of this part of chain */
  unchar dataptr[3];		/* Location of data */
};

/* These belong in scsi.h also */
static inline void any2scsi(u8 *p, u32 v)
{
	p[0] = v >> 16;
	p[1] = v >> 8;
	p[2] = v;
}

#define scsi2int(up) ( (((long)*(up)) << 16) + (((long)(up)[1]) << 8) + ((long)(up)[2]) )

#define xany2scsi(up, p)	\
(up)[0] = ((long)(p)) >> 24;	\
(up)[1] = ((long)(p)) >> 16;	\
(up)[2] = ((long)(p)) >> 8;	\
(up)[3] = ((long)(p));

#define xscsi2int(up) ( (((long)(up)[0]) << 24) + (((long)(up)[1]) << 16) \
		      + (((long)(up)[2]) <<  8) +  ((long)(up)[3]) )

#define MAX_CDB 12
#define MAX_SENSE 14

struct ccb {			/* Command Control Block 5.3 */
  unchar op;			/* Command Control Block Operation Code */
  unchar idlun;			/* op=0,2:Target Id, op=1:Initiator Id */
				/* Outbound data transfer, length is checked*/
				/* Inbound data transfer, length is checked */
				/* Logical Unit Number */
  unchar cdblen;		/* SCSI Command Length */
  unchar rsalen;		/* Request Sense Allocation Length/Disable */
  unchar datalen[3];		/* Data Length (msb, .., lsb) */
  unchar dataptr[3];		/* Data Pointer */
  unchar linkptr[3];		/* Link Pointer */
  unchar commlinkid;		/* Command Linking Identifier */
  unchar hastat;		/* Host Adapter Status (HASTAT) */
  unchar tarstat;		/* Target Device Status */
  unchar reserved[2];
  unchar cdb[MAX_CDB+MAX_SENSE];/* SCSI Command Descriptor Block */
				/* REQUEST SENSE */
};

static int aha1542_detect(Scsi_Host_Template *);
static int aha1542_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
static int aha1542_bus_reset(Scsi_Cmnd * SCpnt);
static int aha1542_dev_reset(Scsi_Cmnd * SCpnt);
static int aha1542_host_reset(Scsi_Cmnd * SCpnt);
#if 0
static int aha1542_old_abort(Scsi_Cmnd * SCpnt);
static int aha1542_old_reset(Scsi_Cmnd *, unsigned int);
#endif
static int aha1542_biosparam(struct scsi_device *, struct block_device *,
		sector_t, int *);

#define AHA1542_MAILBOXES 8
#define AHA1542_SCATTER 16
#define AHA1542_CMDLUN 1

#endif
