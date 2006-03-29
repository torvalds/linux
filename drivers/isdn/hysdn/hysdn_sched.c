/* $Id: hysdn_sched.c,v 1.5.6.4 2001/11/06 21:58:19 kai Exp $
 *
 * Linux driver for HYSDN cards
 * scheduler routines for handling exchange card <-> pc.
 *
 * Author    Werner Cornelius (werner@titro.de) for Hypercope GmbH
 * Copyright 1999 by Werner Cornelius (werner@titro.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <asm/io.h>

#include "hysdn_defs.h"

/*****************************************************************************/
/* hysdn_sched_rx is called from the cards handler to announce new data is   */
/* available from the card. The routine has to handle the data and return    */
/* with a nonzero code if the data could be worked (or even thrown away), if */
/* no room to buffer the data is available a zero return tells the card      */
/* to keep the data until later.                                             */
/*****************************************************************************/
int
hysdn_sched_rx(hysdn_card *card, unsigned char *buf, unsigned short len,
			unsigned short chan)
{

	switch (chan) {
		case CHAN_NDIS_DATA:
			if (hynet_enable & (1 << card->myid)) {
                          /* give packet to network handler */
				hysdn_rx_netpkt(card, buf, len);
			}
			break;

		case CHAN_ERRLOG:
			hysdn_card_errlog(card, (tErrLogEntry *) buf, len);
			if (card->err_log_state == ERRLOG_STATE_ON)
				card->err_log_state = ERRLOG_STATE_START;	/* start new fetch */
			break;
#ifdef CONFIG_HYSDN_CAPI
         	case CHAN_CAPI:
/* give packet to CAPI handler */
			if (hycapi_enable & (1 << card->myid)) {
				hycapi_rx_capipkt(card, buf, len);
			}
			break;
#endif /* CONFIG_HYSDN_CAPI */
		default:
			printk(KERN_INFO "irq message channel %d len %d unhandled \n", chan, len);
			break;

	}			/* switch rx channel */

	return (1);		/* always handled */
}				/* hysdn_sched_rx */

/*****************************************************************************/
/* hysdn_sched_tx is called from the cards handler to announce that there is */
/* room in the tx-buffer to the card and data may be sent if needed.         */
/* If the routine wants to send data it must fill buf, len and chan with the */
/* appropriate data and return a nonzero value. With a zero return no new    */
/* data to send is assumed. maxlen specifies the buffer size available for   */
/* sending.                                                                  */
/*****************************************************************************/
int
hysdn_sched_tx(hysdn_card *card, unsigned char *buf,
		unsigned short volatile *len, unsigned short volatile *chan,
		unsigned short maxlen)
{
	struct sk_buff *skb;

	if (card->net_tx_busy) {
		card->net_tx_busy = 0;	/* reset flag */
		hysdn_tx_netack(card);	/* acknowledge packet send */
	}			/* a network packet has completely been transferred */
	/* first of all async requests are handled */
	if (card->async_busy) {
		if (card->async_len <= maxlen) {
			memcpy(buf, card->async_data, card->async_len);
			*len = card->async_len;
			*chan = card->async_channel;
			card->async_busy = 0;	/* reset request */
			return (1);
		}
		card->async_busy = 0;	/* in case of length error */
	}			/* async request */
	if ((card->err_log_state == ERRLOG_STATE_START) &&
	    (maxlen >= ERRLOG_CMD_REQ_SIZE)) {
		strcpy(buf, ERRLOG_CMD_REQ);	/* copy the command */
		*len = ERRLOG_CMD_REQ_SIZE;	/* buffer length */
		*chan = CHAN_ERRLOG;	/* and channel */
		card->err_log_state = ERRLOG_STATE_ON;	/* new state is on */
		return (1);	/* tell that data should be send */
	}			/* error log start and able to send */
	if ((card->err_log_state == ERRLOG_STATE_STOP) &&
	    (maxlen >= ERRLOG_CMD_STOP_SIZE)) {
		strcpy(buf, ERRLOG_CMD_STOP);	/* copy the command */
		*len = ERRLOG_CMD_STOP_SIZE;	/* buffer length */
		*chan = CHAN_ERRLOG;	/* and channel */
		card->err_log_state = ERRLOG_STATE_OFF;		/* new state is off */
		return (1);	/* tell that data should be send */
	}			/* error log start and able to send */
	/* now handle network interface packets */
	if ((hynet_enable & (1 << card->myid)) && 
	    (skb = hysdn_tx_netget(card)) != NULL) 
	{
		if (skb->len <= maxlen) {
			memcpy(buf, skb->data, skb->len);	/* copy the packet to the buffer */
			*len = skb->len;
			*chan = CHAN_NDIS_DATA;
			card->net_tx_busy = 1;	/* we are busy sending network data */
			return (1);	/* go and send the data */
		} else
			hysdn_tx_netack(card);	/* aknowledge packet -> throw away */
	}			/* send a network packet if available */
#ifdef CONFIG_HYSDN_CAPI
	if( ((hycapi_enable & (1 << card->myid))) && 
	    ((skb = hycapi_tx_capiget(card)) != NULL) )
	{
		if (skb->len <= maxlen) {
			memcpy(buf, skb->data, skb->len);
			*len = skb->len;
			*chan = CHAN_CAPI;
			hycapi_tx_capiack(card);
			return (1);	/* go and send the data */
		}
	}
#endif /* CONFIG_HYSDN_CAPI */
	return (0);		/* nothing to send */
}				/* hysdn_sched_tx */


/*****************************************************************************/
/* send one config line to the card and return 0 if successful, otherwise a */
/* negative error code.                                                      */
/* The function works with timeouts perhaps not giving the greatest speed    */
/* sending the line, but this should be meaningless beacuse only some lines  */
/* are to be sent and this happens very seldom.                              */
/*****************************************************************************/
int
hysdn_tx_cfgline(hysdn_card *card, unsigned char *line, unsigned short chan)
{
	int cnt = 50;		/* timeout intervalls */
	unsigned long flags;

	if (card->debug_flags & LOG_SCHED_ASYN)
		hysdn_addlog(card, "async tx-cfg chan=%d len=%d", chan, strlen(line) + 1);

	save_flags(flags);
	cli();
	while (card->async_busy) {
		sti();

		if (card->debug_flags & LOG_SCHED_ASYN)
			hysdn_addlog(card, "async tx-cfg delayed");

		msleep_interruptible(20);		/* Timeout 20ms */
		if (!--cnt) {
			restore_flags(flags);
			return (-ERR_ASYNC_TIME);	/* timed out */
		}
		cli();
	}			/* wait for buffer to become free */

	strcpy(card->async_data, line);
	card->async_len = strlen(line) + 1;
	card->async_channel = chan;
	card->async_busy = 1;	/* request transfer */

	/* now queue the task */
	schedule_work(&card->irq_queue);
	sti();

	if (card->debug_flags & LOG_SCHED_ASYN)
		hysdn_addlog(card, "async tx-cfg data queued");

	cnt++;			/* short delay */
	cli();

	while (card->async_busy) {
		sti();

		if (card->debug_flags & LOG_SCHED_ASYN)
			hysdn_addlog(card, "async tx-cfg waiting for tx-ready");

		msleep_interruptible(20);		/* Timeout 20ms */
		if (!--cnt) {
			restore_flags(flags);
			return (-ERR_ASYNC_TIME);	/* timed out */
		}
		cli();
	}			/* wait for buffer to become free again */

	restore_flags(flags);

	if (card->debug_flags & LOG_SCHED_ASYN)
		hysdn_addlog(card, "async tx-cfg data send");

	return (0);		/* line send correctly */
}				/* hysdn_tx_cfgline */
