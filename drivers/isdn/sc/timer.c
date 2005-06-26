/* $Id: timer.c,v 1.3.6.1 2001/09/23 22:24:59 kai Exp $
 *
 * Copyright (C) 1996  SpellCaster Telecommunications Inc.
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * For more information, please contact gpl-info@spellcast.com or write:
 *
 *     SpellCaster Telecommunications Inc.
 *     5621 Finch Avenue East, Unit #3
 *     Scarborough, Ontario  Canada
 *     M1B 2T9
 *     +1 (416) 297-8565
 *     +1 (416) 297-6433 Facsimile
 */

#include "includes.h"
#include "hardware.h"
#include "message.h"
#include "card.h"

extern board *sc_adapter[];

extern void flushreadfifo(int);
extern int  startproc(int);
extern int  indicate_status(int, int, unsigned long, char *);
extern int  sendmessage(int, unsigned int, unsigned int, unsigned int,
        unsigned int, unsigned int, unsigned int, unsigned int *);


/*
 * Write the proper values into the I/O ports following a reset
 */
static void setup_ports(int card)
{

	outb((sc_adapter[card]->rambase >> 12), sc_adapter[card]->ioport[EXP_BASE]);

	/* And the IRQ */
	outb((sc_adapter[card]->interrupt | 0x80),
		sc_adapter[card]->ioport[IRQ_SELECT]);
}

/*
 * Timed function to check the status of a previous reset
 * Must be very fast as this function runs in the context of
 * an interrupt handler.
 *
 * Setup the ioports for the board that were cleared by the reset.
 * Then, check to see if the signate has been set. Next, set the
 * signature to a known value and issue a startproc if needed.
 */
void check_reset(unsigned long data)
{
	unsigned long flags;
	unsigned long sig;
	int card = (unsigned int) data;

	pr_debug("%s: check_timer timer called\n",
		sc_adapter[card]->devicename);

	/* Setup the io ports */
	setup_ports(card);

	spin_lock_irqsave(&sc_adapter[card]->lock, flags);
	outb(sc_adapter[card]->ioport[sc_adapter[card]->shmem_pgport],
		(sc_adapter[card]->shmem_magic>>14) | 0x80);
	sig = (unsigned long) *((unsigned long *)(sc_adapter[card]->rambase + SIG_OFFSET));

	/* check the signature */
	if(sig == SIGNATURE) {
		flushreadfifo(card);
		spin_unlock_irqrestore(&sc_adapter[card]->lock, flags);
		/* See if we need to do a startproc */
		if (sc_adapter[card]->StartOnReset)
			startproc(card);
	} else  {
		pr_debug("%s: No signature yet, waiting another %d jiffies.\n", 
			sc_adapter[card]->devicename, CHECKRESET_TIME);
		mod_timer(&sc_adapter[card]->reset_timer, jiffies+CHECKRESET_TIME);
		spin_unlock_irqrestore(&sc_adapter[card]->lock, flags);
	}
}

/*
 * Timed function to check the status of a previous reset
 * Must be very fast as this function runs in the context of
 * an interrupt handler.
 *
 * Send check sc_adapter->phystat to see if the channels are up
 * If they are, tell ISDN4Linux that the board is up. If not,
 * tell IADN4Linux that it is up. Always reset the timer to
 * fire again (endless loop).
 */
void check_phystat(unsigned long data)
{
	unsigned long flags;
	int card = (unsigned int) data;

	pr_debug("%s: Checking status...\n", sc_adapter[card]->devicename);
	/* 
	 * check the results of the last PhyStat and change only if
	 * has changed drastically
	 */
	if (sc_adapter[card]->nphystat && !sc_adapter[card]->phystat) {   /* All is well */
		pr_debug("PhyStat transition to RUN\n");
		pr_info("%s: Switch contacted, transmitter enabled\n", 
			sc_adapter[card]->devicename);
		indicate_status(card, ISDN_STAT_RUN, 0, NULL);
	}
	else if (!sc_adapter[card]->nphystat && sc_adapter[card]->phystat) {   /* All is not well */
		pr_debug("PhyStat transition to STOP\n");
		pr_info("%s: Switch connection lost, transmitter disabled\n", 
			sc_adapter[card]->devicename);

		indicate_status(card, ISDN_STAT_STOP, 0, NULL);
	}

	sc_adapter[card]->phystat = sc_adapter[card]->nphystat;

	/* Reinitialize the timer */
	spin_lock_irqsave(&sc_adapter[card]->lock, flags);
	mod_timer(&sc_adapter[card]->stat_timer, jiffies+CHECKSTAT_TIME);
	spin_unlock_irqrestore(&sc_adapter[card]->lock, flags);

	/* Send a new cePhyStatus message */
	sendmessage(card, CEPID,ceReqTypePhy,ceReqClass2,
		ceReqPhyStatus,0,0,NULL);
}

