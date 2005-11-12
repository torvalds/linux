/*
 *	ultrastor.c	(C) 1991 David B. Gentzel
 *	Low-level scsi driver for UltraStor 14F
 *	by David B. Gentzel, Whitfield Software Services, Carnegie, PA
 *	    (gentzel@nova.enet.dec.com)
 *  scatter/gather added by Scott Taylor (n217cg@tamuts.tamu.edu)
 *  24F support by John F. Carr (jfc@athena.mit.edu)
 *    John's work modified by Caleb Epstein (cae@jpmorgan.com) and 
 *    Eric Youngdale (eric@tantalus.nrl.navy.mil).
 *	Thanks to UltraStor for providing the necessary documentation
 */

#ifndef _ULTRASTOR_H
#define _ULTRASTOR_H

static int ultrastor_detect(struct scsi_host_template *);
static const char *ultrastor_info(struct Scsi_Host * shpnt);
static int ultrastor_queuecommand(Scsi_Cmnd *, void (*done)(Scsi_Cmnd *));
static int ultrastor_abort(Scsi_Cmnd *);
static int ultrastor_host_reset(Scsi_Cmnd *);
static int ultrastor_biosparam(struct scsi_device *, struct block_device *, sector_t, int *);


#define ULTRASTOR_14F_MAX_SG 16
#define ULTRASTOR_24F_MAX_SG 33

#define ULTRASTOR_MAX_CMDS_PER_LUN 5
#define ULTRASTOR_MAX_CMDS 16

#define ULTRASTOR_24F_PORT 0xC80


#ifdef ULTRASTOR_PRIVATE

#define UD_ABORT	0x0001
#define UD_COMMAND	0x0002
#define UD_DETECT	0x0004
#define UD_INTERRUPT	0x0008
#define UD_RESET	0x0010
#define UD_MULTI_CMD	0x0020
#define UD_CSIR		0x0040
#define UD_ERROR	0x0080

/* #define PORT_OVERRIDE 0x330 */

/* Values for the PRODUCT_ID ports for the 14F */
#define US14F_PRODUCT_ID_0 0x56
#define US14F_PRODUCT_ID_1 0x40		/* NOTE: Only upper nibble is used */

#define US24F_PRODUCT_ID_0 0x56
#define US24F_PRODUCT_ID_1 0x63
#define US24F_PRODUCT_ID_2 0x02

/* Subversion values */
#define U14F 0
#define U34F 1

/* MSCP field values */

/* Opcode */
#define OP_HOST_ADAPTER 0x1
#define OP_SCSI 0x2
#define OP_RESET 0x4

/* Date Transfer Direction */
#define DTD_SCSI 0x0
#define DTD_IN 0x1
#define DTD_OUT 0x2
#define DTD_NONE 0x3

/* Host Adapter command subcodes */
#define HA_CMD_INQUIRY 0x1
#define HA_CMD_SELF_DIAG 0x2
#define HA_CMD_READ_BUFF 0x3
#define HA_CMD_WRITE_BUFF 0x4

#endif

#endif
