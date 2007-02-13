/* $Id: command.c,v 1.4.10.1 2001/09/23 22:24:59 kai Exp $
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

#include <linux/module.h>
#include "includes.h"		/* This must be first */
#include "hardware.h"
#include "message.h"
#include "card.h"
#include "scioc.h"

static int dial(int card, unsigned long channel, setup_parm setup);
static int hangup(int card, unsigned long channel);
static int answer(int card, unsigned long channel);
static int clreaz(int card, unsigned long channel);
static int seteaz(int card, unsigned long channel, char *);
static int setl2(int card, unsigned long arg);
static int setl3(int card, unsigned long arg);
static int acceptb(int card, unsigned long channel);

#ifdef DEBUG
/*
 * Translate command codes to strings
 */
static char *commands[] = { "ISDN_CMD_IOCTL",
			    "ISDN_CMD_DIAL",
			    "ISDN_CMD_ACCEPTB",
			    "ISDN_CMD_ACCEPTB",
			    "ISDN_CMD_HANGUP",
			    "ISDN_CMD_CLREAZ",
			    "ISDN_CMD_SETEAZ",
			    NULL,
			    NULL,
			    NULL,
			    "ISDN_CMD_SETL2",
			    NULL,
			    "ISDN_CMD_SETL3",
			    NULL,
			    NULL,
			    NULL,
			    NULL,
			    NULL, };

/*
 * Translates ISDN4Linux protocol codes to strings for debug messages
 */
static char *l3protos[] = { "ISDN_PROTO_L3_TRANS" };
static char *l2protos[] = { "ISDN_PROTO_L2_X75I",
			    "ISDN_PROTO_L2_X75UI",
			    "ISDN_PROTO_L2_X75BUI",
			    "ISDN_PROTO_L2_HDLC",
			    "ISDN_PROTO_L2_TRANS" };
#endif

int get_card_from_id(int driver)
{
	int i;

	for(i = 0 ; i < cinst ; i++) {
		if(sc_adapter[i]->driverId == driver)
			return i;
	}
	return -ENODEV;
}

/* 
 * command
 */

int command(isdn_ctrl *cmd)
{
	int card;

	card = get_card_from_id(cmd->driver);
	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return -ENODEV;
	}

	/*
	 * Dispatch the command
	 */
	switch(cmd->command) {
	case ISDN_CMD_IOCTL:
	{
		unsigned long 	cmdptr;
		scs_ioctl	ioc;

		memcpy(&cmdptr, cmd->parm.num, sizeof(unsigned long));
		if (copy_from_user(&ioc, (scs_ioctl __user *)cmdptr,
				   sizeof(scs_ioctl))) {
			pr_debug("%s: Failed to verify user space 0x%lx\n",
				sc_adapter[card]->devicename, cmdptr);
			return -EFAULT;
		}
		return sc_ioctl(card, &ioc);
	}
	case ISDN_CMD_DIAL:
		return dial(card, cmd->arg, cmd->parm.setup);
	case ISDN_CMD_HANGUP:
		return hangup(card, cmd->arg);
	case ISDN_CMD_ACCEPTD:
		return answer(card, cmd->arg);
	case ISDN_CMD_ACCEPTB:
		return acceptb(card, cmd->arg);
	case ISDN_CMD_CLREAZ:
		return clreaz(card, cmd->arg);
	case ISDN_CMD_SETEAZ:
		return seteaz(card, cmd->arg, cmd->parm.num);
	case ISDN_CMD_SETL2:
		return setl2(card, cmd->arg);
	case ISDN_CMD_SETL3:
		return setl3(card, cmd->arg);
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * start the onboard firmware
 */
int startproc(int card) 
{
	int status;

	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return -ENODEV;
	}

	/*
	 * send start msg 
	 */
       	status = sendmessage(card, CMPID,cmReqType2,
			  cmReqClass0,
			  cmReqStartProc,
			  0,0,NULL);
	pr_debug("%s: Sent startProc\n", sc_adapter[card]->devicename);
	
	return status;
}


/*
 * Dials the number passed in 
 */
static int dial(int card, unsigned long channel, setup_parm setup)
{
	int status;
	char Phone[48];
  
	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return -ENODEV;
	}

	/*extract ISDN number to dial from eaz/msn string*/ 
	strcpy(Phone,setup.phone); 

	/*send the connection message*/
	status = sendmessage(card, CEPID,ceReqTypePhy,
				ceReqClass1,
				ceReqPhyConnect,
				(unsigned char) channel+1, 
				strlen(Phone),
				(unsigned int *) Phone);

	pr_debug("%s: Dialing %s on channel %lu\n",
		sc_adapter[card]->devicename, Phone, channel+1);
	
	return status;
}

/*
 * Answer an incoming call 
 */
static int answer(int card, unsigned long channel)
{
	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return -ENODEV;
	}

	if(setup_buffers(card, channel+1)) {
		hangup(card, channel+1);
		return -ENOBUFS;
	}

	indicate_status(card, ISDN_STAT_BCONN,channel,NULL);
	pr_debug("%s: Answered incoming call on channel %lu\n",
		sc_adapter[card]->devicename, channel+1);
	return 0;
}

/*
 * Hangup up the call on specified channel
 */
static int hangup(int card, unsigned long channel)
{
	int status;

	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return -ENODEV;
	}

	status = sendmessage(card, CEPID, ceReqTypePhy,
						 ceReqClass1,
						 ceReqPhyDisconnect,
						 (unsigned char) channel+1,
						 0,
						 NULL);
	pr_debug("%s: Sent HANGUP message to channel %lu\n",
		sc_adapter[card]->devicename, channel+1);
	return status;
}

/*
 * Set the layer 2 protocol (X.25, HDLC, Raw)
 */
static int setl2(int card, unsigned long arg)
{
	int status =0;
	int protocol,channel;

	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return -ENODEV;
	}
	protocol = arg >> 8;
	channel = arg & 0xff;
	sc_adapter[card]->channel[channel].l2_proto = protocol;

	/*
	 * check that the adapter is also set to the correct protocol
	 */
	pr_debug("%s: Sending GetFrameFormat for channel %d\n",
		sc_adapter[card]->devicename, channel+1);
	status = sendmessage(card, CEPID, ceReqTypeCall,
 				ceReqClass0,
 				ceReqCallGetFrameFormat,
 				(unsigned char)channel+1,
 				1,
 				(unsigned int *) protocol);
	if(status) 
		return status;
	return 0;
}

/*
 * Set the layer 3 protocol
 */
static int setl3(int card, unsigned long channel)
{
	int protocol = channel >> 8;

	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return -ENODEV;
	}

	sc_adapter[card]->channel[channel].l3_proto = protocol;
	return 0;
}

static int acceptb(int card, unsigned long channel)
{
	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return -ENODEV;
	}

	if(setup_buffers(card, channel+1))
	{
		hangup(card, channel+1);
		return -ENOBUFS;
	}

	pr_debug("%s: B-Channel connection accepted on channel %lu\n",
		sc_adapter[card]->devicename, channel+1);
	indicate_status(card, ISDN_STAT_BCONN, channel, NULL);
	return 0;
}

static int clreaz(int card, unsigned long arg)
{
	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return -ENODEV;
	}

	strcpy(sc_adapter[card]->channel[arg].eazlist, "");
	sc_adapter[card]->channel[arg].eazclear = 1;
	pr_debug("%s: EAZ List cleared for channel %lu\n",
		sc_adapter[card]->devicename, arg+1);
	return 0;
}

static int seteaz(int card, unsigned long arg, char *num)
{
	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return -ENODEV;
	}

	strcpy(sc_adapter[card]->channel[arg].eazlist, num);
	sc_adapter[card]->channel[arg].eazclear = 0;
	pr_debug("%s: EAZ list for channel %lu set to: %s\n",
		sc_adapter[card]->devicename, arg+1,
		sc_adapter[card]->channel[arg].eazlist);
	return 0;
}

int reset(int card)
{
	unsigned long flags;

	if(!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return -ENODEV;
	}

	indicate_status(card, ISDN_STAT_STOP, 0, NULL);

	if(sc_adapter[card]->EngineUp) {
		del_timer(&sc_adapter[card]->stat_timer);
	}

	sc_adapter[card]->EngineUp = 0;

	spin_lock_irqsave(&sc_adapter[card]->lock, flags);
	init_timer(&sc_adapter[card]->reset_timer);
	sc_adapter[card]->reset_timer.function = check_reset;
	sc_adapter[card]->reset_timer.data = card;
	sc_adapter[card]->reset_timer.expires = jiffies + CHECKRESET_TIME;
	add_timer(&sc_adapter[card]->reset_timer);
	spin_unlock_irqrestore(&sc_adapter[card]->lock, flags);

	outb(0x1,sc_adapter[card]->ioport[SFT_RESET]);

	pr_debug("%s: Adapter Reset\n", sc_adapter[card]->devicename);
	return 0;
}

void flushreadfifo (int card)
{
	while(inb(sc_adapter[card]->ioport[FIFO_STATUS]) & RF_HAS_DATA)
		inb(sc_adapter[card]->ioport[FIFO_READ]);
}
