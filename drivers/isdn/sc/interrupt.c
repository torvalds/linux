/* $Id: interrupt.c,v 1.4.8.3 2001/09/23 22:24:59 kai Exp $
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
#include <linux/interrupt.h>

/*
 *
 */
irqreturn_t interrupt_handler(int dummy, void *card_inst)
{

	RspMessage rcvmsg;
	int channel;
	int card = (int)(unsigned long) card_inst;

	if (!IS_VALID_CARD(card)) {
		pr_debug("Invalid param: %d is not a valid card id\n", card);
		return IRQ_NONE;
	}

	pr_debug("%s: Entered Interrupt handler\n",
		 sc_adapter[card]->devicename);

	/*
	 * Pull all of the waiting messages off the response queue
	 */
	while (!receivemessage(card, &rcvmsg)) {
		/*
		 * Push the message to the adapter structure for
		 * send_and_receive to snoop
		 */
		if (sc_adapter[card]->want_async_messages)
			memcpy(&(sc_adapter[card]->async_msg),
			       &rcvmsg, sizeof(RspMessage));

		channel = (unsigned int) rcvmsg.phy_link_no;

		/*
		 * Trap Invalid request messages
		 */
		if (IS_CM_MESSAGE(rcvmsg, 0, 0, Invalid)) {
			pr_debug("%s: Invalid request Message, rsp_status = %d\n",
				 sc_adapter[card]->devicename,
				 rcvmsg.rsp_status);
			break;
		}

		/*
		 * Check for a linkRead message
		 */
		if (IS_CE_MESSAGE(rcvmsg, Lnk, 1, Read))
		{
			pr_debug("%s: Received packet 0x%x bytes long at 0x%lx\n",
				 sc_adapter[card]->devicename,
				 rcvmsg.msg_data.response.msg_len,
				 rcvmsg.msg_data.response.buff_offset);
			rcvpkt(card, &rcvmsg);
			continue;

		}

		/*
		 * Handle a write acknoledgement
		 */
		if (IS_CE_MESSAGE(rcvmsg, Lnk, 1, Write)) {
			pr_debug("%s: Packet Send ACK on channel %d\n",
				 sc_adapter[card]->devicename,
				 rcvmsg.phy_link_no);
			sc_adapter[card]->channel[rcvmsg.phy_link_no - 1].free_sendbufs++;
			continue;
		}

		/*
		 * Handle a connection message
		 */
		if (IS_CE_MESSAGE(rcvmsg, Phy, 1, Connect))
		{
			unsigned int callid;
			setup_parm setup;
			pr_debug("%s: Connect message: line %d: status %d: cause 0x%x\n",
				 sc_adapter[card]->devicename,
				 rcvmsg.phy_link_no,
				 rcvmsg.rsp_status,
				 rcvmsg.msg_data.byte_array[2]);

			memcpy(&callid, rcvmsg.msg_data.byte_array, sizeof(int));
			if (callid >= 0x8000 && callid <= 0xFFFF)
			{
				pr_debug("%s: Got Dial-Out Rsp\n",
					 sc_adapter[card]->devicename);
				indicate_status(card, ISDN_STAT_DCONN,
						(unsigned long)rcvmsg.phy_link_no - 1, NULL);

			}
			else if (callid >= 0x0000 && callid <= 0x7FFF)
			{
				int len;

				pr_debug("%s: Got Incoming Call\n",
					 sc_adapter[card]->devicename);
				len = strlcpy(setup.phone, &(rcvmsg.msg_data.byte_array[4]),
					      sizeof(setup.phone));
				if (len >= sizeof(setup.phone))
					continue;
				len = strlcpy(setup.eazmsn,
					      sc_adapter[card]->channel[rcvmsg.phy_link_no - 1].dn,
					      sizeof(setup.eazmsn));
				if (len >= sizeof(setup.eazmsn))
					continue;
				setup.si1 = 7;
				setup.si2 = 0;
				setup.plan = 0;
				setup.screen = 0;

				indicate_status(card, ISDN_STAT_ICALL, (unsigned long)rcvmsg.phy_link_no - 1, (char *)&setup);
				indicate_status(card, ISDN_STAT_DCONN, (unsigned long)rcvmsg.phy_link_no - 1, NULL);
			}
			continue;
		}

		/*
		 * Handle a disconnection message
		 */
		if (IS_CE_MESSAGE(rcvmsg, Phy, 1, Disconnect))
		{
			pr_debug("%s: disconnect message: line %d: status %d: cause 0x%x\n",
				 sc_adapter[card]->devicename,
				 rcvmsg.phy_link_no,
				 rcvmsg.rsp_status,
				 rcvmsg.msg_data.byte_array[2]);

			indicate_status(card, ISDN_STAT_BHUP, (unsigned long)rcvmsg.phy_link_no - 1, NULL);
			indicate_status(card, ISDN_STAT_DHUP, (unsigned long)rcvmsg.phy_link_no - 1, NULL);
			continue;

		}

		/*
		 * Handle a startProc engine up message
		 */
		if (IS_CM_MESSAGE(rcvmsg, 5, 0, MiscEngineUp)) {
			pr_debug("%s: Received EngineUp message\n",
				 sc_adapter[card]->devicename);
			sc_adapter[card]->EngineUp = 1;
			sendmessage(card, CEPID, ceReqTypeCall, ceReqClass0, ceReqCallGetMyNumber, 1, 0, NULL);
			sendmessage(card, CEPID, ceReqTypeCall, ceReqClass0, ceReqCallGetMyNumber, 2, 0, NULL);
			init_timer(&sc_adapter[card]->stat_timer);
			sc_adapter[card]->stat_timer.function = check_phystat;
			sc_adapter[card]->stat_timer.data = card;
			sc_adapter[card]->stat_timer.expires = jiffies + CHECKSTAT_TIME;
			add_timer(&sc_adapter[card]->stat_timer);
			continue;
		}

		/*
		 * Start proc response
		 */
		if (IS_CM_MESSAGE(rcvmsg, 2, 0, StartProc)) {
			pr_debug("%s: StartProc Response Status %d\n",
				 sc_adapter[card]->devicename,
				 rcvmsg.rsp_status);
			continue;
		}

		/*
		 * Handle a GetMyNumber Rsp
		 */
		if (IS_CE_MESSAGE(rcvmsg, Call, 0, GetMyNumber)) {
			strlcpy(sc_adapter[card]->channel[rcvmsg.phy_link_no - 1].dn,
				rcvmsg.msg_data.byte_array,
				sizeof(rcvmsg.msg_data.byte_array));
			continue;
		}

		/*
		 * PhyStatus response
		 */
		if (IS_CE_MESSAGE(rcvmsg, Phy, 2, Status)) {
			unsigned int b1stat, b2stat;

			/*
			 * Covert the message data to the adapter->phystat code
			 */
			b1stat = (unsigned int) rcvmsg.msg_data.byte_array[0];
			b2stat = (unsigned int) rcvmsg.msg_data.byte_array[1];

			sc_adapter[card]->nphystat = (b2stat >> 8) | b1stat; /* endian?? */
			pr_debug("%s: PhyStat is 0x%2x\n",
				 sc_adapter[card]->devicename,
				 sc_adapter[card]->nphystat);
			continue;
		}


		/*
		 * Handle a GetFramFormat
		 */
		if (IS_CE_MESSAGE(rcvmsg, Call, 0, GetFrameFormat)) {
			if (rcvmsg.msg_data.byte_array[0] != HDLC_PROTO) {
				unsigned int proto = HDLC_PROTO;
				/*
				 * Set board format to HDLC if it wasn't already
				 */
				pr_debug("%s: current frame format: 0x%x, will change to HDLC\n",
					 sc_adapter[card]->devicename,
					 rcvmsg.msg_data.byte_array[0]);
				sendmessage(card, CEPID, ceReqTypeCall,
					    ceReqClass0,
					    ceReqCallSetFrameFormat,
					    (unsigned char)channel + 1,
					    1, &proto);
			}
			continue;
		}

		/*
		 * Hmm...
		 */
		pr_debug("%s: Received unhandled message (%d,%d,%d) link %d\n",
			 sc_adapter[card]->devicename,
			 rcvmsg.type, rcvmsg.class, rcvmsg.code,
			 rcvmsg.phy_link_no);

	}	/* while */

	pr_debug("%s: Exiting Interrupt Handler\n",
		 sc_adapter[card]->devicename);
	return IRQ_HANDLED;
}
