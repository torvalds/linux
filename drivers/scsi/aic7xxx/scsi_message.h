/*
 * This file is in the public domain.
 * $FreeBSD: src/sys/cam/scsi/scsi_message.h,v 1.2 2000/05/01 20:21:29 peter Exp $
 */

/* Messages (1 byte) */		     /* I/T (M)andatory or (O)ptional */
#define MSG_SAVEDATAPOINTER	0x02 /* O/O */
#define MSG_RESTOREPOINTERS	0x03 /* O/O */
#define MSG_DISCONNECT		0x04 /* O/O */
#define MSG_MESSAGE_REJECT	0x07 /* M/M */
#define MSG_NOOP		0x08 /* M/M */

/* Messages (2 byte) */
#define MSG_SIMPLE_Q_TAG	0x20 /* O/O */
#define MSG_IGN_WIDE_RESIDUE	0x23 /* O/O */

/* Identify message */		     /* M/M */	
#define MSG_IDENTIFYFLAG	0x80 
#define MSG_IDENTIFY_DISCFLAG	0x40 
#define MSG_IDENTIFY(lun, disc)	(((disc) ? 0xc0 : MSG_IDENTIFYFLAG) | (lun))
#define MSG_ISIDENTIFY(m)	((m) & MSG_IDENTIFYFLAG)
#define MSG_IDENTIFY_LUNMASK	0x3F 

/* Extended messages (opcode and length) */
#define MSG_EXT_SDTR_LEN	0x03

#define MSG_EXT_WDTR_LEN	0x02
#define MSG_EXT_WDTR_BUS_8_BIT	0x00
#define MSG_EXT_WDTR_BUS_16_BIT	0x01
#define MSG_EXT_WDTR_BUS_32_BIT	0x02 /* Deprecated in SPI3 */

#define MSG_EXT_PPR_LEN		0x06
#define	MSG_EXT_PPR_PCOMP_EN	0x80
#define	MSG_EXT_PPR_RTI		0x40
#define	MSG_EXT_PPR_RD_STRM	0x20
#define	MSG_EXT_PPR_WR_FLOW	0x10
#define	MSG_EXT_PPR_HOLD_MCS	0x08
#define	MSG_EXT_PPR_QAS_REQ	0x04
#define	MSG_EXT_PPR_DT_REQ	0x02
#define MSG_EXT_PPR_IU_REQ	0x01
