/* $Id: capi.c,v 1.9.6.2 2001/09/23 22:24:32 kai Exp $
 *
 * ISDN lowlevel-module for the IBM ISDN-S0 Active 2000.
 * CAPI encoder/decoder
 *
 * Author       Fritz Elfert
 * Copyright    by Fritz Elfert      <fritz@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to Friedemann Baitinger and IBM Germany
 *
 */

#include "act2000.h"
#include "capi.h"

static actcapi_msgdsc valid_msg[] = {
	{{ 0x86, 0x02}, "DATA_B3_IND"},       /* DATA_B3_IND/CONF must be first because of speed!!! */
	{{ 0x86, 0x01}, "DATA_B3_CONF"},
	{{ 0x02, 0x01}, "CONNECT_CONF"},
	{{ 0x02, 0x02}, "CONNECT_IND"},
	{{ 0x09, 0x01}, "CONNECT_INFO_CONF"},
	{{ 0x03, 0x02}, "CONNECT_ACTIVE_IND"},
	{{ 0x04, 0x01}, "DISCONNECT_CONF"},
	{{ 0x04, 0x02}, "DISCONNECT_IND"},
	{{ 0x05, 0x01}, "LISTEN_CONF"},
	{{ 0x06, 0x01}, "GET_PARAMS_CONF"},
	{{ 0x07, 0x01}, "INFO_CONF"},
	{{ 0x07, 0x02}, "INFO_IND"},
	{{ 0x08, 0x01}, "DATA_CONF"},
	{{ 0x08, 0x02}, "DATA_IND"},
	{{ 0x40, 0x01}, "SELECT_B2_PROTOCOL_CONF"},
	{{ 0x80, 0x01}, "SELECT_B3_PROTOCOL_CONF"},
	{{ 0x81, 0x01}, "LISTEN_B3_CONF"},
	{{ 0x82, 0x01}, "CONNECT_B3_CONF"},
	{{ 0x82, 0x02}, "CONNECT_B3_IND"},
	{{ 0x83, 0x02}, "CONNECT_B3_ACTIVE_IND"},
	{{ 0x84, 0x01}, "DISCONNECT_B3_CONF"},
	{{ 0x84, 0x02}, "DISCONNECT_B3_IND"},
	{{ 0x85, 0x01}, "GET_B3_PARAMS_CONF"},
	{{ 0x01, 0x01}, "RESET_B3_CONF"},
	{{ 0x01, 0x02}, "RESET_B3_IND"},
	/* {{ 0x87, 0x02, "HANDSET_IND"}, not implemented */
	{{ 0xff, 0x01}, "MANUFACTURER_CONF"},
	{{ 0xff, 0x02}, "MANUFACTURER_IND"},
#ifdef DEBUG_MSG
	/* Requests */
	{{ 0x01, 0x00}, "RESET_B3_REQ"},
	{{ 0x02, 0x00}, "CONNECT_REQ"},
	{{ 0x04, 0x00}, "DISCONNECT_REQ"},
	{{ 0x05, 0x00}, "LISTEN_REQ"},
	{{ 0x06, 0x00}, "GET_PARAMS_REQ"},
	{{ 0x07, 0x00}, "INFO_REQ"},
	{{ 0x08, 0x00}, "DATA_REQ"},
	{{ 0x09, 0x00}, "CONNECT_INFO_REQ"},
	{{ 0x40, 0x00}, "SELECT_B2_PROTOCOL_REQ"},
	{{ 0x80, 0x00}, "SELECT_B3_PROTOCOL_REQ"},
	{{ 0x81, 0x00}, "LISTEN_B3_REQ"},
	{{ 0x82, 0x00}, "CONNECT_B3_REQ"},
	{{ 0x84, 0x00}, "DISCONNECT_B3_REQ"},
	{{ 0x85, 0x00}, "GET_B3_PARAMS_REQ"},
	{{ 0x86, 0x00}, "DATA_B3_REQ"},
	{{ 0xff, 0x00}, "MANUFACTURER_REQ"},
	/* Responses */
	{{ 0x01, 0x03}, "RESET_B3_RESP"},	
	{{ 0x02, 0x03}, "CONNECT_RESP"},	
	{{ 0x03, 0x03}, "CONNECT_ACTIVE_RESP"},	
	{{ 0x04, 0x03}, "DISCONNECT_RESP"},	
	{{ 0x07, 0x03}, "INFO_RESP"},	
	{{ 0x08, 0x03}, "DATA_RESP"},	
	{{ 0x82, 0x03}, "CONNECT_B3_RESP"},	
	{{ 0x83, 0x03}, "CONNECT_B3_ACTIVE_RESP"},	
	{{ 0x84, 0x03}, "DISCONNECT_B3_RESP"},
	{{ 0x86, 0x03}, "DATA_B3_RESP"},
	{{ 0xff, 0x03}, "MANUFACTURER_RESP"},
#endif
	{{ 0x00, 0x00}, NULL},
};
#define num_valid_imsg 27 /* MANUFACTURER_IND */

/*
 * Check for a valid incoming CAPI message.
 * Return:
 *   0 = Invalid message
 *   1 = Valid message, no B-Channel-data
 *   2 = Valid message, B-Channel-data
 */
int
actcapi_chkhdr(act2000_card * card, actcapi_msghdr *hdr)
{
	int i;

	if (hdr->applicationID != 1)
		return 0;
	if (hdr->len < 9)
		return 0;
	for (i = 0; i < num_valid_imsg; i++)
		if ((hdr->cmd.cmd == valid_msg[i].cmd.cmd) &&
		    (hdr->cmd.subcmd == valid_msg[i].cmd.subcmd)) {
			return (i?1:2);
		}
	return 0;
}

#define ACTCAPI_MKHDR(l, c, s) { \
	skb = alloc_skb(l + 8, GFP_ATOMIC); \
	if (skb) { \
	        m = (actcapi_msg *)skb_put(skb, l + 8); \
		m->hdr.len = l + 8; \
		m->hdr.applicationID = 1; \
	        m->hdr.cmd.cmd = c; \
	        m->hdr.cmd.subcmd = s; \
	        m->hdr.msgnum = actcapi_nextsmsg(card); \
	} else m = NULL;\
}

#define ACTCAPI_CHKSKB if (!skb) { \
	printk(KERN_WARNING "actcapi: alloc_skb failed\n"); \
	return; \
}

#define ACTCAPI_QUEUE_TX { \
	actcapi_debug_msg(skb, 1); \
	skb_queue_tail(&card->sndq, skb); \
	act2000_schedule_tx(card); \
}

int
actcapi_listen_req(act2000_card *card)
{
	__u16 eazmask = 0;
	int i;
	actcapi_msg *m;
	struct sk_buff *skb;

	for (i = 0; i < ACT2000_BCH; i++)
		eazmask |= card->bch[i].eazmask;
	ACTCAPI_MKHDR(9, 0x05, 0x00);
        if (!skb) {
                printk(KERN_WARNING "actcapi: alloc_skb failed\n");
                return -ENOMEM;
        }
	m->msg.listen_req.controller = 0;
	m->msg.listen_req.infomask = 0x3f; /* All information */
	m->msg.listen_req.eazmask = eazmask;
	m->msg.listen_req.simask = (eazmask)?0x86:0; /* All SI's  */
	ACTCAPI_QUEUE_TX;
        return 0;
}

int
actcapi_connect_req(act2000_card *card, act2000_chan *chan, char *phone,
		    char eaz, int si1, int si2)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR((11 + strlen(phone)), 0x02, 0x00);
	if (!skb) {
                printk(KERN_WARNING "actcapi: alloc_skb failed\n");
		chan->fsm_state = ACT2000_STATE_NULL;
		return -ENOMEM;
	}
	m->msg.connect_req.controller = 0;
	m->msg.connect_req.bchan = 0x83;
	m->msg.connect_req.infomask = 0x3f;
	m->msg.connect_req.si1 = si1;
	m->msg.connect_req.si2 = si2;
	m->msg.connect_req.eaz = eaz?eaz:'0';
	m->msg.connect_req.addr.len = strlen(phone) + 1;
	m->msg.connect_req.addr.tnp = 0x81;
	memcpy(m->msg.connect_req.addr.num, phone, strlen(phone));
	chan->callref = m->hdr.msgnum;
	ACTCAPI_QUEUE_TX;
	return 0;
}

static void
actcapi_connect_b3_req(act2000_card *card, act2000_chan *chan)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(17, 0x82, 0x00);
	ACTCAPI_CHKSKB;
	m->msg.connect_b3_req.plci = chan->plci;
	memset(&m->msg.connect_b3_req.ncpi, 0,
	       sizeof(m->msg.connect_b3_req.ncpi));
	m->msg.connect_b3_req.ncpi.len = 13;
	m->msg.connect_b3_req.ncpi.modulo = 8;
	ACTCAPI_QUEUE_TX;
}

/*
 * Set net type (1TR6) or (EDSS1)
 */
int
actcapi_manufacturer_req_net(act2000_card *card)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(5, 0xff, 0x00);
        if (!skb) {
                printk(KERN_WARNING "actcapi: alloc_skb failed\n");
                return -ENOMEM;
        }
	m->msg.manufacturer_req_net.manuf_msg = 0x11;
	m->msg.manufacturer_req_net.controller = 1;
	m->msg.manufacturer_req_net.nettype = (card->ptype == ISDN_PTYPE_EURO)?1:0;
	ACTCAPI_QUEUE_TX;
	printk(KERN_INFO "act2000 %s: D-channel protocol now %s\n",
	       card->interface.id, (card->ptype == ISDN_PTYPE_EURO)?"euro":"1tr6");
	card->interface.features &=
		~(ISDN_FEATURE_P_UNKNOWN | ISDN_FEATURE_P_EURO | ISDN_FEATURE_P_1TR6);
	card->interface.features |=
		((card->ptype == ISDN_PTYPE_EURO)?ISDN_FEATURE_P_EURO:ISDN_FEATURE_P_1TR6);
        return 0;
}

/*
 * Switch V.42 on or off
 */
#if 0
int
actcapi_manufacturer_req_v42(act2000_card *card, ulong arg)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(8, 0xff, 0x00);
        if (!skb) {

                printk(KERN_WARNING "actcapi: alloc_skb failed\n");
                return -ENOMEM;
        }
	m->msg.manufacturer_req_v42.manuf_msg = 0x10;
	m->msg.manufacturer_req_v42.controller = 0;
	m->msg.manufacturer_req_v42.v42control = (arg?1:0);
	ACTCAPI_QUEUE_TX;
        return 0;
}
#endif  /*  0  */

/*
 * Set error-handler
 */
int
actcapi_manufacturer_req_errh(act2000_card *card)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(4, 0xff, 0x00);
        if (!skb) {

                printk(KERN_WARNING "actcapi: alloc_skb failed\n");
                return -ENOMEM;
        }
	m->msg.manufacturer_req_err.manuf_msg = 0x03;
	m->msg.manufacturer_req_err.controller = 0;
	ACTCAPI_QUEUE_TX;
        return 0;
}

/*
 * Set MSN-Mapping.
 */
int
actcapi_manufacturer_req_msn(act2000_card *card)
{
	msn_entry *p = card->msn_list;
	actcapi_msg *m;
	struct sk_buff *skb;
	int len;

	while (p) {
		int i;

		len = strlen(p->msn);
		for (i = 0; i < 2; i++) {
			ACTCAPI_MKHDR(6 + len, 0xff, 0x00);
			if (!skb) {
				printk(KERN_WARNING "actcapi: alloc_skb failed\n");
				return -ENOMEM;
			}
			m->msg.manufacturer_req_msn.manuf_msg = 0x13 + i;
			m->msg.manufacturer_req_msn.controller = 0;
			m->msg.manufacturer_req_msn.msnmap.eaz = p->eaz;
			m->msg.manufacturer_req_msn.msnmap.len = len;
			memcpy(m->msg.manufacturer_req_msn.msnmap.msn, p->msn, len);
			ACTCAPI_QUEUE_TX;
		}
		p = p->next;
	}
        return 0;
}

void
actcapi_select_b2_protocol_req(act2000_card *card, act2000_chan *chan)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(10, 0x40, 0x00);
	ACTCAPI_CHKSKB;
	m->msg.select_b2_protocol_req.plci = chan->plci;
	memset(&m->msg.select_b2_protocol_req.dlpd, 0,
	       sizeof(m->msg.select_b2_protocol_req.dlpd));
	m->msg.select_b2_protocol_req.dlpd.len = 6;
	switch (chan->l2prot) {
		case ISDN_PROTO_L2_TRANS:
			m->msg.select_b2_protocol_req.protocol = 0x03;
			m->msg.select_b2_protocol_req.dlpd.dlen = 4000;
			break;
		case ISDN_PROTO_L2_HDLC:
			m->msg.select_b2_protocol_req.protocol = 0x02;
			m->msg.select_b2_protocol_req.dlpd.dlen = 4000;
			break;
		case ISDN_PROTO_L2_X75I:
		case ISDN_PROTO_L2_X75UI:
		case ISDN_PROTO_L2_X75BUI:
			m->msg.select_b2_protocol_req.protocol = 0x01;
			m->msg.select_b2_protocol_req.dlpd.dlen = 4000;
			m->msg.select_b2_protocol_req.dlpd.laa = 3;
			m->msg.select_b2_protocol_req.dlpd.lab = 1;
			m->msg.select_b2_protocol_req.dlpd.win = 7;
			m->msg.select_b2_protocol_req.dlpd.modulo = 8;
			break;
	}
	ACTCAPI_QUEUE_TX;
}

static void
actcapi_select_b3_protocol_req(act2000_card *card, act2000_chan *chan)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(17, 0x80, 0x00);
	ACTCAPI_CHKSKB;
	m->msg.select_b3_protocol_req.plci = chan->plci;
	memset(&m->msg.select_b3_protocol_req.ncpd, 0,
	       sizeof(m->msg.select_b3_protocol_req.ncpd));
	switch (chan->l3prot) {
		case ISDN_PROTO_L3_TRANS:
			m->msg.select_b3_protocol_req.protocol = 0x04;
			m->msg.select_b3_protocol_req.ncpd.len = 13;
			m->msg.select_b3_protocol_req.ncpd.modulo = 8;
			break;
	}
	ACTCAPI_QUEUE_TX;
}

static void
actcapi_listen_b3_req(act2000_card *card, act2000_chan *chan)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(2, 0x81, 0x00);
	ACTCAPI_CHKSKB;
	m->msg.listen_b3_req.plci = chan->plci;
	ACTCAPI_QUEUE_TX;
}

static void
actcapi_disconnect_req(act2000_card *card, act2000_chan *chan)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(3, 0x04, 0x00);
	ACTCAPI_CHKSKB;
	m->msg.disconnect_req.plci = chan->plci;
	m->msg.disconnect_req.cause = 0;
	ACTCAPI_QUEUE_TX;
}

void
actcapi_disconnect_b3_req(act2000_card *card, act2000_chan *chan)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(17, 0x84, 0x00);
	ACTCAPI_CHKSKB;
	m->msg.disconnect_b3_req.ncci = chan->ncci;
	memset(&m->msg.disconnect_b3_req.ncpi, 0,
	       sizeof(m->msg.disconnect_b3_req.ncpi));
	m->msg.disconnect_b3_req.ncpi.len = 13;
	m->msg.disconnect_b3_req.ncpi.modulo = 8;
	chan->fsm_state = ACT2000_STATE_BHWAIT;
	ACTCAPI_QUEUE_TX;
}

void
actcapi_connect_resp(act2000_card *card, act2000_chan *chan, __u8 cause)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(3, 0x02, 0x03);
	ACTCAPI_CHKSKB;
	m->msg.connect_resp.plci = chan->plci;
	m->msg.connect_resp.rejectcause = cause;
	if (cause) {
		chan->fsm_state = ACT2000_STATE_NULL;
		chan->plci = 0x8000;
	} else
		chan->fsm_state = ACT2000_STATE_IWAIT;
	ACTCAPI_QUEUE_TX;
}

static void
actcapi_connect_active_resp(act2000_card *card, act2000_chan *chan)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(2, 0x03, 0x03);
	ACTCAPI_CHKSKB;
	m->msg.connect_resp.plci = chan->plci;
	if (chan->fsm_state == ACT2000_STATE_IWAIT)
		chan->fsm_state = ACT2000_STATE_IBWAIT;
	ACTCAPI_QUEUE_TX;
}

static void
actcapi_connect_b3_resp(act2000_card *card, act2000_chan *chan, __u8 rejectcause)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR((rejectcause?3:17), 0x82, 0x03);
	ACTCAPI_CHKSKB;
	m->msg.connect_b3_resp.ncci = chan->ncci;
	m->msg.connect_b3_resp.rejectcause = rejectcause;
	if (!rejectcause) {
		memset(&m->msg.connect_b3_resp.ncpi, 0,
		       sizeof(m->msg.connect_b3_resp.ncpi));
		m->msg.connect_b3_resp.ncpi.len = 13;
		m->msg.connect_b3_resp.ncpi.modulo = 8;
		chan->fsm_state = ACT2000_STATE_BWAIT;
	}
	ACTCAPI_QUEUE_TX;
}

static void
actcapi_connect_b3_active_resp(act2000_card *card, act2000_chan *chan)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(2, 0x83, 0x03);
	ACTCAPI_CHKSKB;
	m->msg.connect_b3_active_resp.ncci = chan->ncci;
	chan->fsm_state = ACT2000_STATE_ACTIVE;
	ACTCAPI_QUEUE_TX;
}

static void
actcapi_info_resp(act2000_card *card, act2000_chan *chan)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(2, 0x07, 0x03);
	ACTCAPI_CHKSKB;
	m->msg.info_resp.plci = chan->plci;
	ACTCAPI_QUEUE_TX;
}

static void
actcapi_disconnect_b3_resp(act2000_card *card, act2000_chan *chan)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(2, 0x84, 0x03);
	ACTCAPI_CHKSKB;
	m->msg.disconnect_b3_resp.ncci = chan->ncci;
	chan->ncci = 0x8000;
	chan->queued = 0;
	ACTCAPI_QUEUE_TX;
}

static void
actcapi_disconnect_resp(act2000_card *card, act2000_chan *chan)
{
	actcapi_msg *m;
	struct sk_buff *skb;

	ACTCAPI_MKHDR(2, 0x04, 0x03);
	ACTCAPI_CHKSKB;
	m->msg.disconnect_resp.plci = chan->plci;
	chan->plci = 0x8000;
	ACTCAPI_QUEUE_TX;
}

static int
new_plci(act2000_card *card, __u16 plci)
{
	int i;
	for (i = 0; i < ACT2000_BCH; i++)
		if (card->bch[i].plci == 0x8000) {
			card->bch[i].plci = plci;
			return i;
		}
	return -1;
}

static int
find_plci(act2000_card *card, __u16 plci)
{
	int i;
	for (i = 0; i < ACT2000_BCH; i++)
		if (card->bch[i].plci == plci)
			return i;
	return -1;
}

static int
find_ncci(act2000_card *card, __u16 ncci)
{
	int i;
	for (i = 0; i < ACT2000_BCH; i++)
		if (card->bch[i].ncci == ncci)
			return i;
	return -1;
}

static int
find_dialing(act2000_card *card, __u16 callref)
{
	int i;
	for (i = 0; i < ACT2000_BCH; i++)
		if ((card->bch[i].callref == callref) &&
		    (card->bch[i].fsm_state == ACT2000_STATE_OCALL))
			return i;
	return -1;
}

static int
actcapi_data_b3_ind(act2000_card *card, struct sk_buff *skb) {
	__u16 plci;
	__u16 ncci;
	__u16 controller;
	__u8  blocknr;
	int chan;
	actcapi_msg *msg = (actcapi_msg *)skb->data;

	EVAL_NCCI(msg->msg.data_b3_ind.fakencci, plci, controller, ncci);
	chan = find_ncci(card, ncci);
	if (chan < 0)
		return 0;
	if (card->bch[chan].fsm_state != ACT2000_STATE_ACTIVE)
		return 0;
	if (card->bch[chan].plci != plci)
		return 0;
	blocknr = msg->msg.data_b3_ind.blocknr;
	skb_pull(skb, 19);
	card->interface.rcvcallb_skb(card->myid, chan, skb);
        if (!(skb = alloc_skb(11, GFP_ATOMIC))) {
                printk(KERN_WARNING "actcapi: alloc_skb failed\n");
                return 1;
        }
	msg = (actcapi_msg *)skb_put(skb, 11);
	msg->hdr.len = 11;
	msg->hdr.applicationID = 1;
	msg->hdr.cmd.cmd = 0x86;
	msg->hdr.cmd.subcmd = 0x03;
	msg->hdr.msgnum = actcapi_nextsmsg(card);
	msg->msg.data_b3_resp.ncci = ncci;
	msg->msg.data_b3_resp.blocknr = blocknr;
	ACTCAPI_QUEUE_TX;
	return 1;
}

/*
 * Walk over ackq, unlink DATA_B3_REQ from it, if
 * ncci and blocknr are matching.
 * Decrement queued-bytes counter.
 */
static int
handle_ack(act2000_card *card, act2000_chan *chan, __u8 blocknr) {
	unsigned long flags;
	struct sk_buff *skb;
	struct sk_buff *tmp;
	struct actcapi_msg *m;
	int ret = 0;

	spin_lock_irqsave(&card->lock, flags);
	skb = skb_peek(&card->ackq);
	spin_unlock_irqrestore(&card->lock, flags);
        if (!skb) {
		printk(KERN_WARNING "act2000: handle_ack nothing found!\n");
		return 0;
	}
        tmp = skb;
        while (1) {
                m = (actcapi_msg *)tmp->data;
                if ((((m->msg.data_b3_req.fakencci >> 8) & 0xff) == chan->ncci) &&
		    (m->msg.data_b3_req.blocknr == blocknr)) {
			/* found corresponding DATA_B3_REQ */
                        skb_unlink(tmp, &card->ackq);
			chan->queued -= m->msg.data_b3_req.datalen;
			if (m->msg.data_b3_req.flags)
				ret = m->msg.data_b3_req.datalen;
			dev_kfree_skb(tmp);
			if (chan->queued < 0)
				chan->queued = 0;
                        return ret;
                }
                spin_lock_irqsave(&card->lock, flags);
                tmp = skb_peek((struct sk_buff_head *)tmp);
                spin_unlock_irqrestore(&card->lock, flags);
                if ((tmp == skb) || (tmp == NULL)) {
			/* reached end of queue */
			printk(KERN_WARNING "act2000: handle_ack nothing found!\n");
                        return 0;
		}
        }
}

void
actcapi_dispatch(struct work_struct *work)
{
	struct act2000_card *card =
		container_of(work, struct act2000_card, rcv_tq);
	struct sk_buff *skb;
	actcapi_msg *msg;
	__u16 ccmd;
	int chan;
	int len;
	act2000_chan *ctmp;
	isdn_ctrl cmd;
	char tmp[170];

	while ((skb = skb_dequeue(&card->rcvq))) {
		actcapi_debug_msg(skb, 0);
		msg = (actcapi_msg *)skb->data;
		ccmd = ((msg->hdr.cmd.cmd << 8) | msg->hdr.cmd.subcmd);
		switch (ccmd) {
			case 0x8602:
				/* DATA_B3_IND */
				if (actcapi_data_b3_ind(card, skb))
					return;
				break;
			case 0x8601:
				/* DATA_B3_CONF */
				chan = find_ncci(card, msg->msg.data_b3_conf.ncci);
				if ((chan >= 0) && (card->bch[chan].fsm_state == ACT2000_STATE_ACTIVE)) {
					if (msg->msg.data_b3_conf.info != 0)
						printk(KERN_WARNING "act2000: DATA_B3_CONF: %04x\n",
						       msg->msg.data_b3_conf.info);
					len = handle_ack(card, &card->bch[chan],
							 msg->msg.data_b3_conf.blocknr);
					if (len) {
						cmd.driver = card->myid;
						cmd.command = ISDN_STAT_BSENT;
						cmd.arg = chan;
						cmd.parm.length = len;
						card->interface.statcallb(&cmd);
					}
				}
				break;
			case 0x0201:
				/* CONNECT_CONF */
				chan = find_dialing(card, msg->hdr.msgnum);
				if (chan >= 0) {
					if (msg->msg.connect_conf.info) {
						card->bch[chan].fsm_state = ACT2000_STATE_NULL;
						cmd.driver = card->myid;
						cmd.command = ISDN_STAT_DHUP;
						cmd.arg = chan;
						card->interface.statcallb(&cmd);
					} else {
						card->bch[chan].fsm_state = ACT2000_STATE_OWAIT;
						card->bch[chan].plci = msg->msg.connect_conf.plci;
					}
				}
				break;
			case 0x0202:
				/* CONNECT_IND */
				chan = new_plci(card, msg->msg.connect_ind.plci);
				if (chan < 0) {
					ctmp = (act2000_chan *)tmp;
					ctmp->plci = msg->msg.connect_ind.plci;
					actcapi_connect_resp(card, ctmp, 0x11); /* All Card-Cannels busy */
				} else {
					card->bch[chan].fsm_state = ACT2000_STATE_ICALL;
					cmd.driver = card->myid;
					cmd.command = ISDN_STAT_ICALL;
					cmd.arg = chan;
					cmd.parm.setup.si1 = msg->msg.connect_ind.si1;
					cmd.parm.setup.si2 = msg->msg.connect_ind.si2;
					if (card->ptype == ISDN_PTYPE_EURO)
						strcpy(cmd.parm.setup.eazmsn,
						       act2000_find_eaz(card, msg->msg.connect_ind.eaz));
					else {
						cmd.parm.setup.eazmsn[0] = msg->msg.connect_ind.eaz;
						cmd.parm.setup.eazmsn[1] = 0;
					}
					memset(cmd.parm.setup.phone, 0, sizeof(cmd.parm.setup.phone));
					memcpy(cmd.parm.setup.phone, msg->msg.connect_ind.addr.num,
					       msg->msg.connect_ind.addr.len - 1);
					cmd.parm.setup.plan = msg->msg.connect_ind.addr.tnp;
					cmd.parm.setup.screen = 0;
					if (card->interface.statcallb(&cmd) == 2)
						actcapi_connect_resp(card, &card->bch[chan], 0x15); /* Reject Call */
				}
				break;
			case 0x0302:
				/* CONNECT_ACTIVE_IND */
				chan = find_plci(card, msg->msg.connect_active_ind.plci);
				if (chan >= 0)
					switch (card->bch[chan].fsm_state) {
						case ACT2000_STATE_IWAIT:
							actcapi_connect_active_resp(card, &card->bch[chan]);
							break;
						case ACT2000_STATE_OWAIT:
							actcapi_connect_active_resp(card, &card->bch[chan]);
							actcapi_select_b2_protocol_req(card, &card->bch[chan]);
							break;
					}
				break;
			case 0x8202:
				/* CONNECT_B3_IND */
				chan = find_plci(card, msg->msg.connect_b3_ind.plci);
				if ((chan >= 0) && (card->bch[chan].fsm_state == ACT2000_STATE_IBWAIT)) {
					card->bch[chan].ncci = msg->msg.connect_b3_ind.ncci;
					actcapi_connect_b3_resp(card, &card->bch[chan], 0);
				} else {
					ctmp = (act2000_chan *)tmp;
					ctmp->ncci = msg->msg.connect_b3_ind.ncci;
					actcapi_connect_b3_resp(card, ctmp, 0x11); /* All Card-Cannels busy */
				}
				break;
			case 0x8302:
				/* CONNECT_B3_ACTIVE_IND */
				chan = find_ncci(card, msg->msg.connect_b3_active_ind.ncci);
				if ((chan >= 0) && (card->bch[chan].fsm_state == ACT2000_STATE_BWAIT)) {
					actcapi_connect_b3_active_resp(card, &card->bch[chan]);
					cmd.driver = card->myid;
					cmd.command = ISDN_STAT_BCONN;
					cmd.arg = chan;
					card->interface.statcallb(&cmd);
				}
				break;
			case 0x8402:
				/* DISCONNECT_B3_IND */
				chan = find_ncci(card, msg->msg.disconnect_b3_ind.ncci);
				if (chan >= 0) {
					ctmp = &card->bch[chan];
					actcapi_disconnect_b3_resp(card, ctmp);
					switch (ctmp->fsm_state) {
						case ACT2000_STATE_ACTIVE:
							ctmp->fsm_state = ACT2000_STATE_DHWAIT2;
							cmd.driver = card->myid;
							cmd.command = ISDN_STAT_BHUP;
							cmd.arg = chan;
							card->interface.statcallb(&cmd);
							break;
						case ACT2000_STATE_BHWAIT2:
							actcapi_disconnect_req(card, ctmp);
							ctmp->fsm_state = ACT2000_STATE_DHWAIT;
							cmd.driver = card->myid;
							cmd.command = ISDN_STAT_BHUP;
							cmd.arg = chan;
							card->interface.statcallb(&cmd);
							break;
					}
				}
				break;
			case 0x0402:
				/* DISCONNECT_IND */
				chan = find_plci(card, msg->msg.disconnect_ind.plci);
				if (chan >= 0) {
					ctmp = &card->bch[chan];
					actcapi_disconnect_resp(card, ctmp);
					ctmp->fsm_state = ACT2000_STATE_NULL;
					cmd.driver = card->myid;
					cmd.command = ISDN_STAT_DHUP;
					cmd.arg = chan;
					card->interface.statcallb(&cmd);
				} else {
					ctmp = (act2000_chan *)tmp;
					ctmp->plci = msg->msg.disconnect_ind.plci;
					actcapi_disconnect_resp(card, ctmp);
				}
				break;
			case 0x4001:
				/* SELECT_B2_PROTOCOL_CONF */
				chan = find_plci(card, msg->msg.select_b2_protocol_conf.plci);
				if (chan >= 0)
					switch (card->bch[chan].fsm_state) {
						case ACT2000_STATE_ICALL:
						case ACT2000_STATE_OWAIT:
							ctmp = &card->bch[chan];
							if (msg->msg.select_b2_protocol_conf.info == 0)
								actcapi_select_b3_protocol_req(card, ctmp);
							else {
								ctmp->fsm_state = ACT2000_STATE_NULL;
								cmd.driver = card->myid;
								cmd.command = ISDN_STAT_DHUP;
								cmd.arg = chan;
								card->interface.statcallb(&cmd);
							}
							break;
					}
				break;
			case 0x8001:
				/* SELECT_B3_PROTOCOL_CONF */
				chan = find_plci(card, msg->msg.select_b3_protocol_conf.plci);
				if (chan >= 0)
					switch (card->bch[chan].fsm_state) {
						case ACT2000_STATE_ICALL:
						case ACT2000_STATE_OWAIT:
							ctmp = &card->bch[chan];
							if (msg->msg.select_b3_protocol_conf.info == 0)
								actcapi_listen_b3_req(card, ctmp);
							else {
								ctmp->fsm_state = ACT2000_STATE_NULL;
								cmd.driver = card->myid;
								cmd.command = ISDN_STAT_DHUP;
								cmd.arg = chan;
								card->interface.statcallb(&cmd);
							}
					}
				break;
			case 0x8101:
				/* LISTEN_B3_CONF */
				chan = find_plci(card, msg->msg.listen_b3_conf.plci);
				if (chan >= 0)
					switch (card->bch[chan].fsm_state) {
						case ACT2000_STATE_ICALL:
							ctmp = &card->bch[chan];
							if (msg->msg.listen_b3_conf.info == 0)
								actcapi_connect_resp(card, ctmp, 0);
							else {
								ctmp->fsm_state = ACT2000_STATE_NULL;
								cmd.driver = card->myid;
								cmd.command = ISDN_STAT_DHUP;
								cmd.arg = chan;
								card->interface.statcallb(&cmd);
							}
							break;
						case ACT2000_STATE_OWAIT:
							ctmp = &card->bch[chan];
							if (msg->msg.listen_b3_conf.info == 0) {
								actcapi_connect_b3_req(card, ctmp);
								ctmp->fsm_state = ACT2000_STATE_OBWAIT;
								cmd.driver = card->myid;
								cmd.command = ISDN_STAT_DCONN;
								cmd.arg = chan;
								card->interface.statcallb(&cmd);
							} else {
								ctmp->fsm_state = ACT2000_STATE_NULL;
								cmd.driver = card->myid;
								cmd.command = ISDN_STAT_DHUP;
								cmd.arg = chan;
								card->interface.statcallb(&cmd);
							}
							break;
					}
				break;
			case 0x8201:
				/* CONNECT_B3_CONF */
				chan = find_plci(card, msg->msg.connect_b3_conf.plci);
				if ((chan >= 0) && (card->bch[chan].fsm_state == ACT2000_STATE_OBWAIT)) {
					ctmp = &card->bch[chan];
					if (msg->msg.connect_b3_conf.info) {
						ctmp->fsm_state = ACT2000_STATE_NULL;
						cmd.driver = card->myid;
						cmd.command = ISDN_STAT_DHUP;
						cmd.arg = chan;
						card->interface.statcallb(&cmd);
					} else {
						ctmp->ncci = msg->msg.connect_b3_conf.ncci;
						ctmp->fsm_state = ACT2000_STATE_BWAIT;
					}
				}
				break;
			case 0x8401:
				/* DISCONNECT_B3_CONF */
				chan = find_ncci(card, msg->msg.disconnect_b3_conf.ncci);
				if ((chan >= 0) && (card->bch[chan].fsm_state == ACT2000_STATE_BHWAIT))
					card->bch[chan].fsm_state = ACT2000_STATE_BHWAIT2;
				break;
			case 0x0702:
				/* INFO_IND */
				chan = find_plci(card, msg->msg.info_ind.plci);
				if (chan >= 0)
					/* TODO: Eval Charging info / cause */
					actcapi_info_resp(card, &card->bch[chan]);
				break;
			case 0x0401:
				/* LISTEN_CONF */
			case 0x0501:
				/* LISTEN_CONF */
			case 0xff01:
				/* MANUFACTURER_CONF */
				break;
			case 0xff02:
				/* MANUFACTURER_IND */
				if (msg->msg.manuf_msg == 3) {
					memset(tmp, 0, sizeof(tmp));
					strncpy(tmp,
						&msg->msg.manufacturer_ind_err.errstring,
						msg->hdr.len - 16);
					if (msg->msg.manufacturer_ind_err.errcode)
						printk(KERN_WARNING "act2000: %s\n", tmp);
					else {
						printk(KERN_DEBUG "act2000: %s\n", tmp);
						if ((!strncmp(tmp, "INFO: Trace buffer con", 22)) ||
						    (!strncmp(tmp, "INFO: Compile Date/Tim", 22))) {
							card->flags |= ACT2000_FLAGS_RUNNING;
							cmd.command = ISDN_STAT_RUN;
							cmd.driver = card->myid;
							cmd.arg = 0;
							actcapi_manufacturer_req_net(card);
							actcapi_manufacturer_req_msn(card);
							actcapi_listen_req(card);
							card->interface.statcallb(&cmd);
						}
					}
				}
				break;
			default:
				printk(KERN_WARNING "act2000: UNHANDLED Message %04x\n", ccmd);
				break;
		}
		dev_kfree_skb(skb);
	}
}

#ifdef DEBUG_MSG
static void
actcapi_debug_caddr(actcapi_addr *addr)
{
	char tmp[30];

	printk(KERN_DEBUG " Alen  = %d\n", addr->len);
	if (addr->len > 0)
		printk(KERN_DEBUG " Atnp  = 0x%02x\n", addr->tnp);
	if (addr->len > 1) {
		memset(tmp, 0, 30);
		memcpy(tmp, addr->num, addr->len - 1);
		printk(KERN_DEBUG " Anum  = '%s'\n", tmp);
	}
}

static void
actcapi_debug_ncpi(actcapi_ncpi *ncpi)
{
	printk(KERN_DEBUG " ncpi.len = %d\n", ncpi->len);
	if (ncpi->len >= 2)
		printk(KERN_DEBUG " ncpi.lic = 0x%04x\n", ncpi->lic);
	if (ncpi->len >= 4)
		printk(KERN_DEBUG " ncpi.hic = 0x%04x\n", ncpi->hic);
	if (ncpi->len >= 6)
		printk(KERN_DEBUG " ncpi.ltc = 0x%04x\n", ncpi->ltc);
	if (ncpi->len >= 8)
		printk(KERN_DEBUG " ncpi.htc = 0x%04x\n", ncpi->htc);
	if (ncpi->len >= 10)
		printk(KERN_DEBUG " ncpi.loc = 0x%04x\n", ncpi->loc);
	if (ncpi->len >= 12)
		printk(KERN_DEBUG " ncpi.hoc = 0x%04x\n", ncpi->hoc);
	if (ncpi->len >= 13)
		printk(KERN_DEBUG " ncpi.mod = %d\n", ncpi->modulo);
}

static void
actcapi_debug_dlpd(actcapi_dlpd *dlpd)
{
	printk(KERN_DEBUG " dlpd.len = %d\n", dlpd->len);
	if (dlpd->len >= 2)
		printk(KERN_DEBUG " dlpd.dlen   = 0x%04x\n", dlpd->dlen);
	if (dlpd->len >= 3)
		printk(KERN_DEBUG " dlpd.laa    = 0x%02x\n", dlpd->laa);
	if (dlpd->len >= 4)
		printk(KERN_DEBUG " dlpd.lab    = 0x%02x\n", dlpd->lab);
	if (dlpd->len >= 5)
		printk(KERN_DEBUG " dlpd.modulo = %d\n", dlpd->modulo);
	if (dlpd->len >= 6)
		printk(KERN_DEBUG " dlpd.win    = %d\n", dlpd->win);
}

#ifdef DEBUG_DUMP_SKB
static void dump_skb(struct sk_buff *skb) {
	char tmp[80];
	char *p = skb->data;
	char *t = tmp;
	int i;

	for (i = 0; i < skb->len; i++) {
		t += sprintf(t, "%02x ", *p++ & 0xff);
		if ((i & 0x0f) == 8) {
			printk(KERN_DEBUG "dump: %s\n", tmp);
			t = tmp;
		}
	}
	if (i & 0x07)
		printk(KERN_DEBUG "dump: %s\n", tmp);
}
#endif

void
actcapi_debug_msg(struct sk_buff *skb, int direction)
{
	actcapi_msg *msg = (actcapi_msg *)skb->data;
	char *descr;
	int i;
	char tmp[170];
	
#ifndef DEBUG_DATA_MSG
	if (msg->hdr.cmd.cmd == 0x86)
		return;
#endif
	descr = "INVALID";
#ifdef DEBUG_DUMP_SKB
	dump_skb(skb);
#endif
	for (i = 0; i < ARRAY_SIZE(valid_msg); i++)
		if ((msg->hdr.cmd.cmd == valid_msg[i].cmd.cmd) &&
		    (msg->hdr.cmd.subcmd == valid_msg[i].cmd.subcmd)) {
			descr = valid_msg[i].description;
			break;
		}
	printk(KERN_DEBUG "%s %s msg\n", direction?"Outgoing":"Incoming", descr);
	printk(KERN_DEBUG " ApplID = %d\n", msg->hdr.applicationID);
	printk(KERN_DEBUG " Len    = %d\n", msg->hdr.len);
	printk(KERN_DEBUG " MsgNum = 0x%04x\n", msg->hdr.msgnum);
	printk(KERN_DEBUG " Cmd    = 0x%02x\n", msg->hdr.cmd.cmd);
	printk(KERN_DEBUG " SubCmd = 0x%02x\n", msg->hdr.cmd.subcmd);
	switch (i) {
		case 0:
			/* DATA B3 IND */
			printk(KERN_DEBUG " BLOCK = 0x%02x\n",
			       msg->msg.data_b3_ind.blocknr);
			break;
		case 2:
			/* CONNECT CONF */
			printk(KERN_DEBUG " PLCI = 0x%04x\n",
			       msg->msg.connect_conf.plci);
			printk(KERN_DEBUG " Info = 0x%04x\n",
			       msg->msg.connect_conf.info);
			break;
		case 3:
			/* CONNECT IND */
			printk(KERN_DEBUG " PLCI = 0x%04x\n",
			       msg->msg.connect_ind.plci);
			printk(KERN_DEBUG " Contr = %d\n",
			       msg->msg.connect_ind.controller);
			printk(KERN_DEBUG " SI1   = %d\n",
			       msg->msg.connect_ind.si1);
			printk(KERN_DEBUG " SI2   = %d\n",
			       msg->msg.connect_ind.si2);
			printk(KERN_DEBUG " EAZ   = '%c'\n",
			       msg->msg.connect_ind.eaz);
			actcapi_debug_caddr(&msg->msg.connect_ind.addr);
			break;
		case 5:
			/* CONNECT ACTIVE IND */
			printk(KERN_DEBUG " PLCI = 0x%04x\n",
			       msg->msg.connect_active_ind.plci);
			actcapi_debug_caddr(&msg->msg.connect_active_ind.addr);
			break;
		case 8:
			/* LISTEN CONF */
			printk(KERN_DEBUG " Contr = %d\n",
			       msg->msg.listen_conf.controller);
			printk(KERN_DEBUG " Info = 0x%04x\n",
			       msg->msg.listen_conf.info);
			break;
		case 11:
			/* INFO IND */
			printk(KERN_DEBUG " PLCI = 0x%04x\n",
			       msg->msg.info_ind.plci);
			printk(KERN_DEBUG " Imsk = 0x%04x\n",
			       msg->msg.info_ind.nr.mask);
			if (msg->hdr.len > 12) {
				int l = msg->hdr.len - 12;
				int j;
				char *p = tmp;
				for (j = 0; j < l ; j++)
					p += sprintf(p, "%02x ", msg->msg.info_ind.el.display[j]);
				printk(KERN_DEBUG " D = '%s'\n", tmp);
			}
			break;
		case 14:
			/* SELECT B2 PROTOCOL CONF */
			printk(KERN_DEBUG " PLCI = 0x%04x\n",
			       msg->msg.select_b2_protocol_conf.plci);
			printk(KERN_DEBUG " Info = 0x%04x\n",
			       msg->msg.select_b2_protocol_conf.info);
			break;
		case 15:
			/* SELECT B3 PROTOCOL CONF */
			printk(KERN_DEBUG " PLCI = 0x%04x\n",
			       msg->msg.select_b3_protocol_conf.plci);
			printk(KERN_DEBUG " Info = 0x%04x\n",
			       msg->msg.select_b3_protocol_conf.info);
			break;
		case 16:
			/* LISTEN B3 CONF */
			printk(KERN_DEBUG " PLCI = 0x%04x\n",
			       msg->msg.listen_b3_conf.plci);
			printk(KERN_DEBUG " Info = 0x%04x\n",
			       msg->msg.listen_b3_conf.info);
			break;
		case 18:
			/* CONNECT B3 IND */
			printk(KERN_DEBUG " NCCI = 0x%04x\n",
			       msg->msg.connect_b3_ind.ncci);
			printk(KERN_DEBUG " PLCI = 0x%04x\n",
			       msg->msg.connect_b3_ind.plci);
			actcapi_debug_ncpi(&msg->msg.connect_b3_ind.ncpi);
			break;
		case 19:
			/* CONNECT B3 ACTIVE IND */
			printk(KERN_DEBUG " NCCI = 0x%04x\n",
			       msg->msg.connect_b3_active_ind.ncci);
			actcapi_debug_ncpi(&msg->msg.connect_b3_active_ind.ncpi);
			break;
		case 26:
			/* MANUFACTURER IND */
			printk(KERN_DEBUG " Mmsg = 0x%02x\n",
			       msg->msg.manufacturer_ind_err.manuf_msg);
			switch (msg->msg.manufacturer_ind_err.manuf_msg) {
				case 3:
					printk(KERN_DEBUG " Contr = %d\n",
					       msg->msg.manufacturer_ind_err.controller);
					printk(KERN_DEBUG " Code = 0x%08x\n",
					       msg->msg.manufacturer_ind_err.errcode);
					memset(tmp, 0, sizeof(tmp));
					strncpy(tmp, &msg->msg.manufacturer_ind_err.errstring,
						msg->hdr.len - 16);
					printk(KERN_DEBUG " Emsg = '%s'\n", tmp);
					break;
			}
			break;
		case 30:
			/* LISTEN REQ */
			printk(KERN_DEBUG " Imsk = 0x%08x\n",
			       msg->msg.listen_req.infomask);
			printk(KERN_DEBUG " Emsk = 0x%04x\n",
			       msg->msg.listen_req.eazmask);
			printk(KERN_DEBUG " Smsk = 0x%04x\n",
			       msg->msg.listen_req.simask);
			break;
		case 35:
			/* SELECT_B2_PROTOCOL_REQ */
			printk(KERN_DEBUG " PLCI  = 0x%04x\n",
			       msg->msg.select_b2_protocol_req.plci);
			printk(KERN_DEBUG " prot  = 0x%02x\n",
			       msg->msg.select_b2_protocol_req.protocol);
			if (msg->hdr.len >= 11)
				printk(KERN_DEBUG "No dlpd\n");
			else
				actcapi_debug_dlpd(&msg->msg.select_b2_protocol_req.dlpd);
			break;
		case 44:
			/* CONNECT RESP */
			printk(KERN_DEBUG " PLCI  = 0x%04x\n",
			       msg->msg.connect_resp.plci);
			printk(KERN_DEBUG " CAUSE = 0x%02x\n",
			       msg->msg.connect_resp.rejectcause);
			break;
		case 45:
			/* CONNECT ACTIVE RESP */
			printk(KERN_DEBUG " PLCI  = 0x%04x\n",
			       msg->msg.connect_active_resp.plci);
			break;
	}
}
#endif
