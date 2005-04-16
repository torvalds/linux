/* $Id: boardergo.c,v 1.5.6.7 2001/11/06 21:58:19 kai Exp $
 *
 * Linux driver for HYSDN cards, specific routines for ergo type boards.
 *
 * Author    Werner Cornelius (werner@titro.de) for Hypercope GmbH
 * Copyright 1999 by Werner Cornelius (werner@titro.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * As all Linux supported cards Champ2, Ergo and Metro2/4 use the same
 * DPRAM interface and layout with only minor differences all related
 * stuff is done here, not in separate modules.
 *
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <asm/io.h>

#include "hysdn_defs.h"
#include "boardergo.h"

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

/***************************************************/
/* The cards interrupt handler. Called from system */
/***************************************************/
static irqreturn_t
ergo_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	hysdn_card *card = dev_id;	/* parameter from irq */
	tErgDpram *dpr;
	ulong flags;
	uchar volatile b;

	if (!card)
		return IRQ_NONE;		/* error -> spurious interrupt */
	if (!card->irq_enabled)
		return IRQ_NONE;		/* other device interrupting or irq switched off */

	save_flags(flags);
	cli();			/* no further irqs allowed */

	if (!(bytein(card->iobase + PCI9050_INTR_REG) & PCI9050_INTR_REG_STAT1)) {
		restore_flags(flags);	/* restore old state */
		return IRQ_NONE;		/* no interrupt requested by E1 */
	}
	/* clear any pending ints on the board */
	dpr = card->dpram;
	b = dpr->ToPcInt;	/* clear for ergo */
	b |= dpr->ToPcIntMetro;	/* same for metro */
	b |= dpr->ToHyInt;	/* and for champ */

	/* start kernel task immediately after leaving all interrupts */
	if (!card->hw_lock)
		schedule_work(&card->irq_queue);
	restore_flags(flags);
	return IRQ_HANDLED;
}				/* ergo_interrupt */

/******************************************************************************/
/* ergo_irq_bh is the function called by the immediate kernel task list after */
/* being activated with queue_task and no interrupts active. This task is the */
/* only one handling data transfer from or to the card after booting. The task */
/* may be queued from everywhere (interrupts included).                       */
/******************************************************************************/
static void
ergo_irq_bh(hysdn_card * card)
{
	tErgDpram *dpr;
	int again;
	ulong flags;

	if (card->state != CARD_STATE_RUN)
		return;		/* invalid call */

	dpr = card->dpram;	/* point to DPRAM */

	save_flags(flags);
	cli();
	if (card->hw_lock) {
		restore_flags(flags);	/* hardware currently unavailable */
		return;
	}
	card->hw_lock = 1;	/* we now lock the hardware */

	do {
		sti();		/* reenable other ints */
		again = 0;	/* assume loop not to be repeated */

		if (!dpr->ToHyFlag) {
			/* we are able to send a buffer */

			if (hysdn_sched_tx(card, dpr->ToHyBuf, &dpr->ToHySize, &dpr->ToHyChannel,
					   ERG_TO_HY_BUF_SIZE)) {
				dpr->ToHyFlag = 1;	/* enable tx */
				again = 1;	/* restart loop */
			}
		}		/* we are able to send a buffer */
		if (dpr->ToPcFlag) {
			/* a message has arrived for us, handle it */

			if (hysdn_sched_rx(card, dpr->ToPcBuf, dpr->ToPcSize, dpr->ToPcChannel)) {
				dpr->ToPcFlag = 0;	/* we worked the data */
				again = 1;	/* restart loop */
			}
		}		/* a message has arrived for us */
		cli();		/* no further ints */
		if (again) {
			dpr->ToHyInt = 1;
			dpr->ToPcInt = 1;	/* interrupt to E1 for all cards */
		} else
			card->hw_lock = 0;	/* free hardware again */
	} while (again);	/* until nothing more to do */

	restore_flags(flags);
}				/* ergo_irq_bh */


/*********************************************************/
/* stop the card (hardware reset) and disable interrupts */
/*********************************************************/
static void
ergo_stopcard(hysdn_card * card)
{
	ulong flags;
	uchar val;

	hysdn_net_release(card);	/* first release the net device if existing */
#ifdef CONFIG_HYSDN_CAPI
	hycapi_capi_stop(card);
#endif /* CONFIG_HYSDN_CAPI */
	save_flags(flags);
	cli();
	val = bytein(card->iobase + PCI9050_INTR_REG);	/* get actual value */
	val &= ~(PCI9050_INTR_REG_ENPCI | PCI9050_INTR_REG_EN1);	/* mask irq */
	byteout(card->iobase + PCI9050_INTR_REG, val);
	card->irq_enabled = 0;
	byteout(card->iobase + PCI9050_USER_IO, PCI9050_E1_RESET);	/* reset E1 processor */
	card->state = CARD_STATE_UNUSED;
	card->err_log_state = ERRLOG_STATE_OFF;		/* currently no log active */

	restore_flags(flags);
}				/* ergo_stopcard */

/**************************************************************************/
/* enable or disable the cards error log. The event is queued if possible */
/**************************************************************************/
static void
ergo_set_errlog_state(hysdn_card * card, int on)
{
	ulong flags;

	if (card->state != CARD_STATE_RUN) {
		card->err_log_state = ERRLOG_STATE_OFF;		/* must be off */
		return;
	}
	save_flags(flags);
	cli();

	if (((card->err_log_state == ERRLOG_STATE_OFF) && !on) ||
	    ((card->err_log_state == ERRLOG_STATE_ON) && on)) {
		restore_flags(flags);
		return;		/* nothing to do */
	}
	if (on)
		card->err_log_state = ERRLOG_STATE_START;	/* request start */
	else
		card->err_log_state = ERRLOG_STATE_STOP;	/* request stop */

	restore_flags(flags);
	schedule_work(&card->irq_queue);
}				/* ergo_set_errlog_state */

/******************************************/
/* test the cards RAM and return 0 if ok. */
/******************************************/
static const char TestText[36] = "This Message is filler, why read it";

static int
ergo_testram(hysdn_card * card)
{
	tErgDpram *dpr = card->dpram;

	memset(dpr->TrapTable, 0, sizeof(dpr->TrapTable));	/* clear all Traps */
	dpr->ToHyInt = 1;	/* E1 INTR state forced */

	memcpy(&dpr->ToHyBuf[ERG_TO_HY_BUF_SIZE - sizeof(TestText)], TestText,
	       sizeof(TestText));
	if (memcmp(&dpr->ToHyBuf[ERG_TO_HY_BUF_SIZE - sizeof(TestText)], TestText,
		   sizeof(TestText)))
		return (-1);

	memcpy(&dpr->ToPcBuf[ERG_TO_PC_BUF_SIZE - sizeof(TestText)], TestText,
	       sizeof(TestText));
	if (memcmp(&dpr->ToPcBuf[ERG_TO_PC_BUF_SIZE - sizeof(TestText)], TestText,
		   sizeof(TestText)))
		return (-1);

	return (0);
}				/* ergo_testram */

/*****************************************************************************/
/* this function is intended to write stage 1 boot image to the cards buffer */
/* this is done in two steps. First the 1024 hi-words are written (offs=0),  */
/* then the 1024 lo-bytes are written. The remaining DPRAM is cleared, the   */
/* PCI-write-buffers flushed and the card is taken out of reset.             */
/* The function then waits for a reaction of the E1 processor or a timeout.  */
/* Negative return values are interpreted as errors.                         */
/*****************************************************************************/
static int
ergo_writebootimg(struct HYSDN_CARD *card, uchar * buf, ulong offs)
{
	uchar *dst;
	tErgDpram *dpram;
	int cnt = (BOOT_IMG_SIZE >> 2);		/* number of words to move and swap (byte order!) */
	
	if (card->debug_flags & LOG_POF_CARD)
		hysdn_addlog(card, "ERGO: write bootldr offs=0x%lx ", offs);

	dst = card->dpram;	/* pointer to start of DPRAM */
	dst += (offs + ERG_DPRAM_FILL_SIZE);	/* offset in the DPRAM */
	while (cnt--) {
		*dst++ = *(buf + 1);	/* high byte */
		*dst++ = *buf;	/* low byte */
		dst += 2;	/* point to next longword */
		buf += 2;	/* buffer only filled with words */
	}

	/* if low words (offs = 2) have been written, clear the rest of the DPRAM, */
	/* flush the PCI-write-buffer and take the E1 out of reset */
	if (offs) {
		memset(card->dpram, 0, ERG_DPRAM_FILL_SIZE);	/* fill the DPRAM still not cleared */
		dpram = card->dpram;	/* get pointer to dpram structure */
		dpram->ToHyNoDpramErrLog = 0xFF;	/* write a dpram register */
		while (!dpram->ToHyNoDpramErrLog);	/* reread volatile register to flush PCI */

		byteout(card->iobase + PCI9050_USER_IO, PCI9050_E1_RUN);	/* start E1 processor */
		/* the interrupts are still masked */

		sti();
		msleep_interruptible(20);		/* Timeout 20ms */

		if (((tDpramBootSpooler *) card->dpram)->Len != DPRAM_SPOOLER_DATA_SIZE) {
			if (card->debug_flags & LOG_POF_CARD)
				hysdn_addlog(card, "ERGO: write bootldr no answer");
			return (-ERR_BOOTIMG_FAIL);
		}
	}			/* start_boot_img */
	return (0);		/* successful */
}				/* ergo_writebootimg */

/********************************************************************************/
/* ergo_writebootseq writes the buffer containing len bytes to the E1 processor */
/* using the boot spool mechanism. If everything works fine 0 is returned. In   */
/* case of errors a negative error value is returned.                           */
/********************************************************************************/
static int
ergo_writebootseq(struct HYSDN_CARD *card, uchar * buf, int len)
{
	tDpramBootSpooler *sp = (tDpramBootSpooler *) card->dpram;
	uchar *dst;
	uchar buflen;
	int nr_write;
	uchar tmp_rdptr;
	uchar wr_mirror;
	int i;

	if (card->debug_flags & LOG_POF_CARD)
		hysdn_addlog(card, "ERGO: write boot seq len=%d ", len);

	dst = sp->Data;		/* point to data in spool structure */
	buflen = sp->Len;	/* maximum len of spooled data */
	wr_mirror = sp->WrPtr;	/* only once read */
	sti();

	/* try until all bytes written or error */
	i = 0x1000;		/* timeout value */
	while (len) {

		/* first determine the number of bytes that may be buffered */
		do {
			tmp_rdptr = sp->RdPtr;	/* first read the pointer */
			i--;	/* decrement timeout */
		} while (i && (tmp_rdptr != sp->RdPtr));	/* wait for stable pointer */

		if (!i) {
			if (card->debug_flags & LOG_POF_CARD)
				hysdn_addlog(card, "ERGO: write boot seq timeout");
			return (-ERR_BOOTSEQ_FAIL);	/* value not stable -> timeout */
		}
		if ((nr_write = tmp_rdptr - wr_mirror - 1) < 0)
			nr_write += buflen;	/* now we got number of free bytes - 1 in buffer */

		if (!nr_write)
			continue;	/* no free bytes in buffer */

		if (nr_write > len)
			nr_write = len;		/* limit if last few bytes */
		i = 0x1000;	/* reset timeout value */

		/* now we know how much bytes we may put in the puffer */
		len -= nr_write;	/* we savely could adjust len before output */
		while (nr_write--) {
			*(dst + wr_mirror) = *buf++;	/* output one byte */
			if (++wr_mirror >= buflen)
				wr_mirror = 0;
			sp->WrPtr = wr_mirror;	/* announce the next byte to E1 */
		}		/* while (nr_write) */

	}			/* while (len) */
	return (0);
}				/* ergo_writebootseq */

/***********************************************************************************/
/* ergo_waitpofready waits for a maximum of 10 seconds for the completition of the */
/* boot process. If the process has been successful 0 is returned otherwise a     */
/* negative error code is returned.                                                */
/***********************************************************************************/
static int
ergo_waitpofready(struct HYSDN_CARD *card)
{
	tErgDpram *dpr = card->dpram;	/* pointer to DPRAM structure */
	int timecnt = 10000 / 50;	/* timeout is 10 secs max. */
	ulong flags;
	int msg_size;
	int i;

	if (card->debug_flags & LOG_POF_CARD)
		hysdn_addlog(card, "ERGO: waiting for pof ready");
	while (timecnt--) {
		/* wait until timeout  */

		if (dpr->ToPcFlag) {
			/* data has arrived */

			if ((dpr->ToPcChannel != CHAN_SYSTEM) ||
			    (dpr->ToPcSize < MIN_RDY_MSG_SIZE) ||
			    (dpr->ToPcSize > MAX_RDY_MSG_SIZE) ||
			    ((*(ulong *) dpr->ToPcBuf) != RDY_MAGIC))
				break;	/* an error occurred */

			/* Check for additional data delivered during SysReady */
			msg_size = dpr->ToPcSize - RDY_MAGIC_SIZE;
			if (msg_size > 0)
				if (EvalSysrTokData(card, dpr->ToPcBuf + RDY_MAGIC_SIZE, msg_size))
					break;

			if (card->debug_flags & LOG_POF_RECORD)
				hysdn_addlog(card, "ERGO: pof boot success");
			save_flags(flags);
			cli();

			card->state = CARD_STATE_RUN;	/* now card is running */
			/* enable the cards interrupt */
			byteout(card->iobase + PCI9050_INTR_REG,
				bytein(card->iobase + PCI9050_INTR_REG) |
			(PCI9050_INTR_REG_ENPCI | PCI9050_INTR_REG_EN1));
			card->irq_enabled = 1;	/* we are ready to receive interrupts */

			dpr->ToPcFlag = 0;	/* reset data indicator */
			dpr->ToHyInt = 1;
			dpr->ToPcInt = 1;	/* interrupt to E1 for all cards */

			restore_flags(flags);
			if ((hynet_enable & (1 << card->myid)) 
			    && (i = hysdn_net_create(card))) 
			{
				ergo_stopcard(card);
				card->state = CARD_STATE_BOOTERR;
				return (i);
			}
#ifdef CONFIG_HYSDN_CAPI
			if((i = hycapi_capi_create(card))) {
				printk(KERN_WARNING "HYSDN: failed to create capi-interface.\n");
			}
#endif /* CONFIG_HYSDN_CAPI */
			return (0);	/* success */
		}		/* data has arrived */
		sti();
		msleep_interruptible(50);		/* Timeout 50ms */
	}			/* wait until timeout */

	if (card->debug_flags & LOG_POF_CARD)
		hysdn_addlog(card, "ERGO: pof boot ready timeout");
	return (-ERR_POF_TIMEOUT);
}				/* ergo_waitpofready */



/************************************************************************************/
/* release the cards hardware. Before releasing do a interrupt disable and hardware */
/* reset. Also unmap dpram.                                                         */
/* Use only during module release.                                                  */
/************************************************************************************/
static void
ergo_releasehardware(hysdn_card * card)
{
	ergo_stopcard(card);	/* first stop the card if not already done */
	free_irq(card->irq, card);	/* release interrupt */
	release_region(card->iobase + PCI9050_INTR_REG, 1);	/* release all io ports */
	release_region(card->iobase + PCI9050_USER_IO, 1);
	vfree(card->dpram);
	card->dpram = NULL;	/* release shared mem */
}				/* ergo_releasehardware */


/*********************************************************************************/
/* acquire the needed hardware ports and map dpram. If an error occurs a nonzero */
/* value is returned.                                                            */
/* Use only during module init.                                                  */
/*********************************************************************************/
int
ergo_inithardware(hysdn_card * card)
{
	if (!request_region(card->iobase + PCI9050_INTR_REG, 1, "HYSDN")) 
		return (-1);
	if (!request_region(card->iobase + PCI9050_USER_IO, 1, "HYSDN")) {
		release_region(card->iobase + PCI9050_INTR_REG, 1);
		return (-1);	/* ports already in use */
	}
	card->memend = card->membase + ERG_DPRAM_PAGE_SIZE - 1;
	if (!(card->dpram = ioremap(card->membase, ERG_DPRAM_PAGE_SIZE))) {
		release_region(card->iobase + PCI9050_INTR_REG, 1);
		release_region(card->iobase + PCI9050_USER_IO, 1);
		return (-1);
	}

	ergo_stopcard(card);	/* disable interrupts */
	if (request_irq(card->irq, ergo_interrupt, SA_SHIRQ, "HYSDN", card)) {
		ergo_releasehardware(card); /* return the acquired hardware */
		return (-1);
	}
	/* success, now setup the function pointers */
	card->stopcard = ergo_stopcard;
	card->releasehardware = ergo_releasehardware;
	card->testram = ergo_testram;
	card->writebootimg = ergo_writebootimg;
	card->writebootseq = ergo_writebootseq;
	card->waitpofready = ergo_waitpofready;
	card->set_errlog_state = ergo_set_errlog_state;
	INIT_WORK(&card->irq_queue, (void *) (void *) ergo_irq_bh, card);

	return (0);
}				/* ergo_inithardware */
