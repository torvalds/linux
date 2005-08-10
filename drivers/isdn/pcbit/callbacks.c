/*
 * Callbacks for the FSM
 *
 * Copyright (C) 1996 Universidade de Lisboa
 * 
 * Written by Pedro Roque Marques (roque@di.fc.ul.pt)
 *
 * This software may be used and distributed according to the terms of 
 * the GNU General Public License, incorporated herein by reference.
 */

/*
 * Fix: 19981230 - Carlos Morgado <chbm@techie.com>
 * Port of Nelson Escravana's <nelson.escravana@usa.net> fix to CalledPN 
 * NULL pointer dereference in cb_in_1 (originally fixed in 2.0)
 */

#include <linux/sched.h>
#include <linux/string.h>
#include <linux/kernel.h>

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/skbuff.h>

#include <asm/io.h>

#include <linux/isdnif.h>

#include "pcbit.h"
#include "layer2.h"
#include "edss1.h"
#include "callbacks.h"
#include "capi.h"

ushort last_ref_num = 1;

/*
 *  send_conn_req
 *
 */

void cb_out_1(struct pcbit_dev * dev, struct pcbit_chan* chan, 
	      struct callb_data *cbdata) 
{
	struct sk_buff *skb;
	int len;
        ushort refnum;


#ifdef DEBUG
        printk(KERN_DEBUG "Called Party Number: %s\n", 
               cbdata->data.setup.CalledPN);
#endif
        /*
         * hdr - kmalloc in capi_conn_req
         *     - kfree   when msg has been sent
         */

        if ((len = capi_conn_req(cbdata->data.setup.CalledPN, &skb, 
				 chan->proto)) < 0)
        {
                printk("capi_conn_req failed\n");
                return;
        }


        refnum = last_ref_num++ & 0x7fffU;

        chan->callref = 0;
        chan->layer2link = 0;
        chan->snum = 0;
        chan->s_refnum = refnum;

        pcbit_l2_write(dev, MSG_CONN_REQ, refnum, skb, len);
}

/*
 *  rcv CONNECT
 *  will go into ACTIVE state
 *  send CONN_ACTIVE_RESP
 *  send Select protocol request 
 */

void cb_out_2(struct pcbit_dev * dev, struct pcbit_chan* chan, 
	      struct callb_data *data) 
{
        isdn_ctrl ictl;
 	struct sk_buff *skb;
	int len;
        ushort refnum;

        if ((len=capi_conn_active_resp(chan, &skb)) < 0)
        {
                printk("capi_conn_active_req failed\n");
                return;
        }

        refnum = last_ref_num++ & 0x7fffU;
        chan->s_refnum = refnum;

        pcbit_l2_write(dev, MSG_CONN_ACTV_RESP, refnum, skb, len);


        ictl.command = ISDN_STAT_DCONN;
        ictl.driver=dev->id;
        ictl.arg=chan->id;
        dev->dev_if->statcallb(&ictl);

        /* ACTIVE D-channel */

        /* Select protocol  */

        if ((len=capi_select_proto_req(chan, &skb, 1 /*outgoing*/)) < 0) { 
                printk("capi_select_proto_req failed\n");
                return;
        }

        refnum = last_ref_num++ & 0x7fffU;
        chan->s_refnum = refnum;

        pcbit_l2_write(dev, MSG_SELP_REQ, refnum, skb, len);
}


/*
 * Incoming call received
 * inform user
 */

void cb_in_1(struct pcbit_dev * dev, struct pcbit_chan* chan,
	     struct callb_data *cbdata) 
{
        isdn_ctrl ictl;
        unsigned short refnum;
 	struct sk_buff *skb;
	int len;


        ictl.command = ISDN_STAT_ICALL;
        ictl.driver=dev->id;
        ictl.arg=chan->id;
        
        /*
         *  ictl.num >= strlen() + strlen() + 5
         */

	if (cbdata->data.setup.CallingPN == NULL) {
		printk(KERN_DEBUG "NULL CallingPN to phone; using 0\n");
		strcpy(ictl.parm.setup.phone, "0");
	}
	else {
		strcpy(ictl.parm.setup.phone, cbdata->data.setup.CallingPN);
	}
	if (cbdata->data.setup.CalledPN == NULL) {
		printk(KERN_DEBUG "NULL CalledPN to eazmsn; using 0\n");
		strcpy(ictl.parm.setup.eazmsn, "0");
	}
	else {
		strcpy(ictl.parm.setup.eazmsn, cbdata->data.setup.CalledPN);
	}
	ictl.parm.setup.si1 = 7;
	ictl.parm.setup.si2 = 0;
	ictl.parm.setup.plan = 0;
	ictl.parm.setup.screen = 0;

#ifdef DEBUG
	printk(KERN_DEBUG "statstr: %s\n", ictl.num);
#endif

        dev->dev_if->statcallb(&ictl);

        
        if ((len=capi_conn_resp(chan, &skb)) < 0) {
                printk(KERN_DEBUG "capi_conn_resp failed\n");
                return;
	}

        refnum = last_ref_num++ & 0x7fffU;
        chan->s_refnum = refnum;

        pcbit_l2_write(dev, MSG_CONN_RESP, refnum, skb, len);
}

/*
 * user has replied
 * open the channel
 * send CONNECT message CONNECT_ACTIVE_REQ in CAPI
 */

void cb_in_2(struct pcbit_dev * dev, struct pcbit_chan* chan,
	     struct callb_data *data)
{
        unsigned short refnum;
	struct sk_buff *skb;
        int len;
        
        if ((len = capi_conn_active_req(chan, &skb)) < 0) {        
                printk(KERN_DEBUG "capi_conn_active_req failed\n");
                return;
        }


        refnum = last_ref_num++ & 0x7fffU;
        chan->s_refnum = refnum;

	printk(KERN_DEBUG "sending MSG_CONN_ACTV_REQ\n");
        pcbit_l2_write(dev, MSG_CONN_ACTV_REQ, refnum, skb, len);
}

/*
 * CONN_ACK arrived
 * start b-proto selection
 *
 */

void cb_in_3(struct pcbit_dev * dev, struct pcbit_chan* chan, 
	     struct callb_data *data)
{
        unsigned short refnum;
 	struct sk_buff *skb;
	int len;
        
        if ((len = capi_select_proto_req(chan, &skb, 0 /*incoming*/)) < 0)
        {
                printk("capi_select_proto_req failed\n");
                return;
        }

        refnum = last_ref_num++ & 0x7fffU;
        chan->s_refnum = refnum;

        pcbit_l2_write(dev, MSG_SELP_REQ, refnum, skb, len);

}


/*
 * Received disconnect ind on active state
 * send disconnect resp
 * send msg to user
 */
void cb_disc_1(struct pcbit_dev * dev, struct pcbit_chan* chan, 
	       struct callb_data *data)
{
 	struct sk_buff *skb;
	int len;
        ushort refnum;
        isdn_ctrl ictl;
  
        if ((len = capi_disc_resp(chan, &skb)) < 0) {
                printk("capi_disc_resp failed\n");
                return;
        }

        refnum = last_ref_num++ & 0x7fffU;
        chan->s_refnum = refnum;

        pcbit_l2_write(dev, MSG_DISC_RESP, refnum, skb, len);    

        ictl.command = ISDN_STAT_BHUP;
        ictl.driver=dev->id;
        ictl.arg=chan->id;
        dev->dev_if->statcallb(&ictl);
}

        
/*
 *  User HANGUP on active/call proceeding state
 *  send disc.req
 */
void cb_disc_2(struct pcbit_dev * dev, struct pcbit_chan* chan, 
	       struct callb_data *data)
{
 	struct sk_buff *skb;
	int len;
        ushort refnum;

        if ((len = capi_disc_req(chan->callref, &skb, CAUSE_NORMAL)) < 0)
        {
                printk("capi_disc_req failed\n");
                return;
        }

        refnum = last_ref_num++ & 0x7fffU;
        chan->s_refnum = refnum;

        pcbit_l2_write(dev, MSG_DISC_REQ, refnum, skb, len);  
}

/*
 *  Disc confirm received send BHUP
 *  Problem: when the HL driver sends the disc req itself
 *           LL receives BHUP
 */
void cb_disc_3(struct pcbit_dev * dev, struct pcbit_chan* chan, 
	       struct callb_data *data)
{
        isdn_ctrl ictl;

        ictl.command = ISDN_STAT_BHUP;
        ictl.driver=dev->id;
        ictl.arg=chan->id;
        dev->dev_if->statcallb(&ictl);
}

void cb_notdone(struct pcbit_dev * dev, struct pcbit_chan* chan, 
		struct callb_data *data)
{
}

/*
 * send activate b-chan protocol
 */
void cb_selp_1(struct pcbit_dev * dev, struct pcbit_chan* chan, 
	       struct callb_data *data) 
{
 	struct sk_buff *skb;
	int len;
        ushort refnum;

        if ((len = capi_activate_transp_req(chan, &skb)) < 0)
        {
                printk("capi_conn_activate_transp_req failed\n");
                return;
        }

        refnum = last_ref_num++ & 0x7fffU;
        chan->s_refnum = refnum;

        pcbit_l2_write(dev, MSG_ACT_TRANSP_REQ, refnum, skb, len);
}

/*
 *  Inform User that the B-channel is available
 */
void cb_open(struct pcbit_dev * dev, struct pcbit_chan* chan, 
	     struct callb_data *data) 
{
        isdn_ctrl ictl;

        ictl.command = ISDN_STAT_BCONN;
        ictl.driver=dev->id;
        ictl.arg=chan->id;
        dev->dev_if->statcallb(&ictl);
}



