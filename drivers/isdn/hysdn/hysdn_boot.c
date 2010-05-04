/* $Id: hysdn_boot.c,v 1.4.6.4 2001/09/23 22:24:54 kai Exp $
 *
 * Linux driver for HYSDN cards
 * specific routines for booting and pof handling
 *
 * Author    Werner Cornelius (werner@titro.de) for Hypercope GmbH
 * Copyright 1999 by Werner Cornelius (werner@titro.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include "hysdn_defs.h"
#include "hysdn_pof.h"

/********************************/
/* defines for pof read handler */
/********************************/
#define POF_READ_FILE_HEAD  0
#define POF_READ_TAG_HEAD   1
#define POF_READ_TAG_DATA   2

/************************************************************/
/* definition of boot specific data area. This data is only */
/* needed during boot and so allocated dynamically.         */
/************************************************************/
struct boot_data {
	unsigned short Cryptor;	/* for use with Decrypt function */
	unsigned short Nrecs;	/* records remaining in file */
	unsigned char pof_state;/* actual state of read handler */
	unsigned char is_crypted;/* card data is crypted */
	int BufSize;		/* actual number of bytes bufferd */
	int last_error;		/* last occurred error */
	unsigned short pof_recid;/* actual pof recid */
	unsigned long pof_reclen;/* total length of pof record data */
	unsigned long pof_recoffset;/* actual offset inside pof record */
	union {
		unsigned char BootBuf[BOOT_BUF_SIZE];/* buffer as byte count */
		tPofRecHdr PofRecHdr;	/* header for actual record/chunk */
		tPofFileHdr PofFileHdr;		/* header from POF file */
		tPofTimeStamp PofTime;	/* time information */
	} buf;
};

/*****************************************************/
/*  start decryption of successive POF file chuncks.  */
/*                                                   */
/*  to be called at start of POF file reading,       */
/*  before starting any decryption on any POF record. */
/*****************************************************/
static void
StartDecryption(struct boot_data *boot)
{
	boot->Cryptor = CRYPT_STARTTERM;
}				/* StartDecryption */


/***************************************************************/
/* decrypt complete BootBuf                                    */
/* NOTE: decryption must be applied to all or none boot tags - */
/*       to HI and LO boot loader and (all) seq tags, because  */
/*       global Cryptor is started for whole POF.              */
/***************************************************************/
static void
DecryptBuf(struct boot_data *boot, int cnt)
{
	unsigned char *bufp = boot->buf.BootBuf;

	while (cnt--) {
		boot->Cryptor = (boot->Cryptor >> 1) ^ ((boot->Cryptor & 1U) ? CRYPT_FEEDTERM : 0);
		*bufp++ ^= (unsigned char)boot->Cryptor;
	}
}				/* DecryptBuf */

/********************************************************************************/
/* pof_handle_data executes the required actions dependent on the active record */
/* id. If successful 0 is returned, a negative value shows an error.           */
/********************************************************************************/
static int
pof_handle_data(hysdn_card * card, int datlen)
{
	struct boot_data *boot = card->boot;	/* pointer to boot specific data */
	long l;
	unsigned char *imgp;
	int img_len;

	/* handle the different record types */
	switch (boot->pof_recid) {

		case TAG_TIMESTMP:
			if (card->debug_flags & LOG_POF_RECORD)
				hysdn_addlog(card, "POF created %s", boot->buf.PofTime.DateTimeText);
			break;

		case TAG_CBOOTDTA:
			DecryptBuf(boot, datlen);	/* we need to encrypt the buffer */
		case TAG_BOOTDTA:
			if (card->debug_flags & LOG_POF_RECORD)
				hysdn_addlog(card, "POF got %s len=%d offs=0x%lx",
					     (boot->pof_recid == TAG_CBOOTDTA) ? "CBOOTDATA" : "BOOTDTA",
					     datlen, boot->pof_recoffset);

			if (boot->pof_reclen != POF_BOOT_LOADER_TOTAL_SIZE) {
				boot->last_error = EPOF_BAD_IMG_SIZE;	/* invalid length */
				return (boot->last_error);
			}
			imgp = boot->buf.BootBuf;	/* start of buffer */
			img_len = datlen;	/* maximum length to transfer */

			l = POF_BOOT_LOADER_OFF_IN_PAGE -
			    (boot->pof_recoffset & (POF_BOOT_LOADER_PAGE_SIZE - 1));
			if (l > 0) {
				/* buffer needs to be truncated */
				imgp += l;	/* advance pointer */
				img_len -= l;	/* adjust len */
			}
			/* at this point no special handling for data wrapping over buffer */
			/* is necessary, because the boot image always will be adjusted to */
			/* match a page boundary inside the buffer.                        */
			/* The buffer for the boot image on the card is filled in 2 cycles */
			/* first the 1024 hi-words are put in the buffer, then the low 1024 */
			/* word are handled in the same way with different offset.         */

			if (img_len > 0) {
				/* data available for copy */
				if ((boot->last_error =
				     card->writebootimg(card, imgp,
							(boot->pof_recoffset > POF_BOOT_LOADER_PAGE_SIZE) ? 2 : 0)) < 0)
					return (boot->last_error);
			}
			break;	/* end of case boot image hi/lo */

		case TAG_CABSDATA:
			DecryptBuf(boot, datlen);	/* we need to encrypt the buffer */
		case TAG_ABSDATA:
			if (card->debug_flags & LOG_POF_RECORD)
				hysdn_addlog(card, "POF got %s len=%d offs=0x%lx",
					     (boot->pof_recid == TAG_CABSDATA) ? "CABSDATA" : "ABSDATA",
					     datlen, boot->pof_recoffset);

			if ((boot->last_error = card->writebootseq(card, boot->buf.BootBuf, datlen)) < 0)
				return (boot->last_error);	/* error writing data */

			if (boot->pof_recoffset + datlen >= boot->pof_reclen)
				return (card->waitpofready(card));	/* data completely spooled, wait for ready */

			break;	/* end of case boot seq data */

		default:
			if (card->debug_flags & LOG_POF_RECORD)
				hysdn_addlog(card, "POF got data(id=0x%lx) len=%d offs=0x%lx", boot->pof_recid,
					     datlen, boot->pof_recoffset);

			break;	/* simply skip record */
	}			/* switch boot->pof_recid */

	return (0);
}				/* pof_handle_data */


/******************************************************************************/
/* pof_write_buffer is called when the buffer has been filled with the needed */
/* number of data bytes. The number delivered is additionally supplied for    */
/* verification. The functions handles the data and returns the needed number */
/* of bytes for the next action. If the returned value is 0 or less an error  */
/* occurred and booting must be aborted.                                       */
/******************************************************************************/
int
pof_write_buffer(hysdn_card * card, int datlen)
{
	struct boot_data *boot = card->boot;	/* pointer to boot specific data */

	if (!boot)
		return (-EFAULT);	/* invalid call */
	if (boot->last_error < 0)
		return (boot->last_error);	/* repeated error */

	if (card->debug_flags & LOG_POF_WRITE)
		hysdn_addlog(card, "POF write: got %d bytes ", datlen);

	switch (boot->pof_state) {
		case POF_READ_FILE_HEAD:
			if (card->debug_flags & LOG_POF_WRITE)
				hysdn_addlog(card, "POF write: checking file header");

			if (datlen != sizeof(tPofFileHdr)) {
				boot->last_error = -EPOF_INTERNAL;
				break;
			}
			if (boot->buf.PofFileHdr.Magic != TAGFILEMAGIC) {
				boot->last_error = -EPOF_BAD_MAGIC;
				break;
			}
			/* Setup the new state and vars */
			boot->Nrecs = (unsigned short)(boot->buf.PofFileHdr.N_PofRecs);	/* limited to 65535 */
			boot->pof_state = POF_READ_TAG_HEAD;	/* now start with single tags */
			boot->last_error = sizeof(tPofRecHdr);	/* new length */
			break;

		case POF_READ_TAG_HEAD:
			if (card->debug_flags & LOG_POF_WRITE)
				hysdn_addlog(card, "POF write: checking tag header");

			if (datlen != sizeof(tPofRecHdr)) {
				boot->last_error = -EPOF_INTERNAL;
				break;
			}
			boot->pof_recid = boot->buf.PofRecHdr.PofRecId;		/* actual pof recid */
			boot->pof_reclen = boot->buf.PofRecHdr.PofRecDataLen;	/* total length */
			boot->pof_recoffset = 0;	/* no starting offset */

			if (card->debug_flags & LOG_POF_RECORD)
				hysdn_addlog(card, "POF: got record id=0x%lx length=%ld ",
				      boot->pof_recid, boot->pof_reclen);

			boot->pof_state = POF_READ_TAG_DATA;	/* now start with tag data */
			if (boot->pof_reclen < BOOT_BUF_SIZE)
				boot->last_error = boot->pof_reclen;	/* limit size */
			else
				boot->last_error = BOOT_BUF_SIZE;	/* maximum */

			if (!boot->last_error) {	/* no data inside record */
				boot->pof_state = POF_READ_TAG_HEAD;	/* now start with single tags */
				boot->last_error = sizeof(tPofRecHdr);	/* new length */
			}
			break;

		case POF_READ_TAG_DATA:
			if (card->debug_flags & LOG_POF_WRITE)
				hysdn_addlog(card, "POF write: getting tag data");

			if (datlen != boot->last_error) {
				boot->last_error = -EPOF_INTERNAL;
				break;
			}
			if ((boot->last_error = pof_handle_data(card, datlen)) < 0)
				return (boot->last_error);	/* an error occurred */
			boot->pof_recoffset += datlen;
			if (boot->pof_recoffset >= boot->pof_reclen) {
				boot->pof_state = POF_READ_TAG_HEAD;	/* now start with single tags */
				boot->last_error = sizeof(tPofRecHdr);	/* new length */
			} else {
				if (boot->pof_reclen - boot->pof_recoffset < BOOT_BUF_SIZE)
					boot->last_error = boot->pof_reclen - boot->pof_recoffset;	/* limit size */
				else
					boot->last_error = BOOT_BUF_SIZE;	/* maximum */
			}
			break;

		default:
			boot->last_error = -EPOF_INTERNAL;	/* unknown state */
			break;
	}			/* switch (boot->pof_state) */

	return (boot->last_error);
}				/* pof_write_buffer */


/*******************************************************************************/
/* pof_write_open is called when an open for boot on the cardlog device occurs. */
/* The function returns the needed number of bytes for the next operation. If  */
/* the returned number is less or equal 0 an error specified by this code      */
/* occurred. Additionally the pointer to the buffer data area is set on success */
/*******************************************************************************/
int
pof_write_open(hysdn_card * card, unsigned char **bufp)
{
	struct boot_data *boot;	/* pointer to boot specific data */

	if (card->boot) {
		if (card->debug_flags & LOG_POF_OPEN)
			hysdn_addlog(card, "POF open: already opened for boot");
		return (-ERR_ALREADY_BOOT);	/* boot already active */
	}
	/* error no mem available */
	if (!(boot = kzalloc(sizeof(struct boot_data), GFP_KERNEL))) {
		if (card->debug_flags & LOG_MEM_ERR)
			hysdn_addlog(card, "POF open: unable to allocate mem");
		return (-EFAULT);
	}
	card->boot = boot;
	card->state = CARD_STATE_BOOTING;

	card->stopcard(card);	/* first stop the card */
	if (card->testram(card)) {
		if (card->debug_flags & LOG_POF_OPEN)
			hysdn_addlog(card, "POF open: DPRAM test failure");
		boot->last_error = -ERR_BOARD_DPRAM;
		card->state = CARD_STATE_BOOTERR;	/* show boot error */
		return (boot->last_error);
	}
	boot->BufSize = 0;	/* Buffer is empty */
	boot->pof_state = POF_READ_FILE_HEAD;	/* read file header */
	StartDecryption(boot);	/* if POF File should be encrypted */

	if (card->debug_flags & LOG_POF_OPEN)
		hysdn_addlog(card, "POF open: success");

	*bufp = boot->buf.BootBuf;	/* point to buffer */
	return (sizeof(tPofFileHdr));
}				/* pof_write_open */

/********************************************************************************/
/* pof_write_close is called when an close of boot on the cardlog device occurs. */
/* The return value must be 0 if everything has happened as desired.            */
/********************************************************************************/
int
pof_write_close(hysdn_card * card)
{
	struct boot_data *boot = card->boot;	/* pointer to boot specific data */

	if (!boot)
		return (-EFAULT);	/* invalid call */

	card->boot = NULL;	/* no boot active */
	kfree(boot);

	if (card->state == CARD_STATE_RUN)
		card->set_errlog_state(card, 1);	/* activate error log */

	if (card->debug_flags & LOG_POF_OPEN)
		hysdn_addlog(card, "POF close: success");

	return (0);
}				/* pof_write_close */

/*********************************************************************************/
/* EvalSysrTokData checks additional records delivered with the Sysready Message */
/* when POF has been booted. A return value of 0 is used if no error occurred.    */
/*********************************************************************************/
int
EvalSysrTokData(hysdn_card *card, unsigned char *cp, int len)
{
	u_char *p;
	u_char crc;

	if (card->debug_flags & LOG_POF_RECORD)
		hysdn_addlog(card, "SysReady Token data length %d", len);

	if (len < 2) {
		hysdn_addlog(card, "SysReady Token Data to short");
		return (1);
	}
	for (p = cp, crc = 0; p < (cp + len - 2); p++)
		if ((crc & 0x80))
			crc = (((u_char) (crc << 1)) + 1) + *p;
		else
			crc = ((u_char) (crc << 1)) + *p;
	crc = ~crc;
	if (crc != *(cp + len - 1)) {
		hysdn_addlog(card, "SysReady Token Data invalid CRC");
		return (1);
	}
	len--;			/* don't check CRC byte */
	while (len > 0) {

		if (*cp == SYSR_TOK_END)
			return (0);	/* End of Token stream */

		if (len < (*(cp + 1) + 2)) {
			hysdn_addlog(card, "token 0x%x invalid length %d", *cp, *(cp + 1));
			return (1);
		}
		switch (*cp) {
			case SYSR_TOK_B_CHAN:	/* 1 */
				if (*(cp + 1) != 1)
					return (1);	/* length invalid */
				card->bchans = *(cp + 2);
				break;

			case SYSR_TOK_FAX_CHAN:	/* 2 */
				if (*(cp + 1) != 1)
					return (1);	/* length invalid */
				card->faxchans = *(cp + 2);
				break;

			case SYSR_TOK_MAC_ADDR:	/* 3 */
				if (*(cp + 1) != 6)
					return (1);	/* length invalid */
				memcpy(card->mac_addr, cp + 2, 6);
				break;

			default:
				hysdn_addlog(card, "unknown token 0x%02x length %d", *cp, *(cp + 1));
				break;
		}
		len -= (*(cp + 1) + 2);		/* adjust len */
		cp += (*(cp + 1) + 2);	/* and pointer */
	}

	hysdn_addlog(card, "no end token found");
	return (1);
}				/* EvalSysrTokData */
