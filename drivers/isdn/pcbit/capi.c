/*
 * CAPI encoder/decoder for
 * Portugal Telecom CAPI 2.0
 *
 * Copyright (C) 1996 Universidade de Lisboa
 * 
 * Written by Pedro Roque Marques (roque@di.fc.ul.pt)
 *
 * This software may be used and distributed according to the terms of 
 * the GNU General Public License, incorporated herein by reference.
 *
 * Not compatible with the AVM Gmbh. CAPI 2.0
 *
 */

/*
 *        Documentation:
 *        - "Common ISDN API - Perfil Português - Versão 2.1",
 *           Telecom Portugal, Fev 1992.
 *        - "Common ISDN API - Especificação de protocolos para 
 *           acesso aos canais B", Inesc, Jan 1994.
 */

/*
 *        TODO: better decoding of Information Elements
 *              for debug purposes mainly
 *              encode our number in CallerPN and ConnectedPN
 */

#include <linux/sched.h>
#include <linux/string.h>
#include <linux/kernel.h>

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <linux/skbuff.h>

#include <asm/io.h>
#include <asm/string.h>

#include <linux/isdnif.h>

#include "pcbit.h"
#include "edss1.h"
#include "capi.h"


/*
 *  Encoding of CAPI messages
 *
 */

int capi_conn_req(const char * calledPN, struct sk_buff **skb, int proto)
{
        ushort len;

        /*
         * length
         *   AppInfoMask - 2
         *   BC0         - 3
         *   BC1         - 1
         *   Chan        - 2
         *   Keypad      - 1
         *   CPN         - 1
         *   CPSA        - 1
         *   CalledPN    - 2 + strlen
         *   CalledPSA   - 1
         *   rest...     - 4
         *   ----------------
         *   Total        18 + strlen
         */

        len = 18 + strlen(calledPN);

	if (proto == ISDN_PROTO_L2_TRANS)
		len++;

	if ((*skb = dev_alloc_skb(len)) == NULL) {
    
	        printk(KERN_WARNING "capi_conn_req: alloc_skb failed\n");
		return -1;
	}

        /* InfoElmMask */
        *((ushort*) skb_put(*skb, 2)) = AppInfoMask; 

	if (proto == ISDN_PROTO_L2_TRANS)
	{
		/* Bearer Capability - Mandatory*/
		*(skb_put(*skb, 1)) = 3;        /* BC0.Length		*/
		*(skb_put(*skb, 1)) = 0x80;     /* Speech		*/
		*(skb_put(*skb, 1)) = 0x10;     /* Circuit Mode		*/
		*(skb_put(*skb, 1)) = 0x23;     /* A-law		*/
	}
	else
	{
		/* Bearer Capability - Mandatory*/
		*(skb_put(*skb, 1)) = 2;        /* BC0.Length		*/
		*(skb_put(*skb, 1)) = 0x88;     /* Digital Information	*/
		*(skb_put(*skb, 1)) = 0x90;     /* BC0.Octect4		*/
	}

        /* Bearer Capability - Optional*/
        *(skb_put(*skb, 1)) = 0;        /* BC1.Length = 0                    */

        *(skb_put(*skb, 1)) = 1;        /* ChannelID.Length = 1              */
        *(skb_put(*skb, 1)) = 0x83;     /* Basic Interface - Any Channel     */

        *(skb_put(*skb, 1)) = 0;        /* Keypad.Length = 0                 */
                  

        *(skb_put(*skb, 1)) = 0;        /* CallingPN.Length = 0              */
        *(skb_put(*skb, 1)) = 0;        /* CallingPSA.Length = 0             */

        /* Called Party Number */
        *(skb_put(*skb, 1)) = strlen(calledPN) + 1;
        *(skb_put(*skb, 1)) = 0x81;
        memcpy(skb_put(*skb, strlen(calledPN)), calledPN, strlen(calledPN));

        /* '#' */

        *(skb_put(*skb, 1)) = 0;       /* CalledPSA.Length = 0     */

        /* LLC.Length  = 0; */
        /* HLC0.Length = 0; */
        /* HLC1.Length = 0; */ 
        /* UTUS.Length = 0; */
        memset(skb_put(*skb, 4), 0, 4);

        return len;
}

int capi_conn_resp(struct pcbit_chan* chan, struct sk_buff **skb)
{
        
	if ((*skb = dev_alloc_skb(5)) == NULL) {
    
		printk(KERN_WARNING "capi_conn_resp: alloc_skb failed\n");
		return -1;
	}

        *((ushort*) skb_put(*skb, 2) ) = chan->callref;  
        *(skb_put(*skb, 1)) = 0x01;  /* ACCEPT_CALL */
        *(skb_put(*skb, 1)) = 0;
        *(skb_put(*skb, 1)) = 0;

        return 5;
}

int capi_conn_active_req(struct pcbit_chan* chan, struct sk_buff **skb)
{
        /*
         * 8 bytes
         */
        
	if ((*skb = dev_alloc_skb(8)) == NULL) {
    
		printk(KERN_WARNING "capi_conn_active_req: alloc_skb failed\n");
		return -1;
	}

        *((ushort*) skb_put(*skb, 2) ) = chan->callref;  

#ifdef DEBUG
	printk(KERN_DEBUG "Call Reference: %04x\n", chan->callref); 
#endif

        *(skb_put(*skb, 1)) = 0;       /*  BC.Length = 0;          */
        *(skb_put(*skb, 1)) = 0;       /*  ConnectedPN.Length = 0  */
        *(skb_put(*skb, 1)) = 0;       /*  PSA.Length              */
        *(skb_put(*skb, 1)) = 0;       /*  LLC.Length = 0;         */
        *(skb_put(*skb, 1)) = 0;       /*  HLC.Length = 0;         */
        *(skb_put(*skb, 1)) = 0;       /*  UTUS.Length = 0;        */

	return 8;
}

int capi_conn_active_resp(struct pcbit_chan* chan, struct sk_buff **skb)
{
        /*
         * 2 bytes
         */
  
	if ((*skb = dev_alloc_skb(2)) == NULL) {
    
		printk(KERN_WARNING "capi_conn_active_resp: alloc_skb failed\n");
		return -1;
	}

        *((ushort*) skb_put(*skb, 2) ) = chan->callref;  

        return 2;
}


int capi_select_proto_req(struct pcbit_chan *chan, struct sk_buff **skb, 
                          int outgoing)
{

        /*
         * 18 bytes
         */

	if ((*skb = dev_alloc_skb(18)) == NULL) {
    
		printk(KERN_WARNING "capi_select_proto_req: alloc_skb failed\n");
		return -1;
	}

        *((ushort*) skb_put(*skb, 2) ) = chan->callref;  

        /* Layer2 protocol */

        switch (chan->proto) {
        case ISDN_PROTO_L2_X75I: 
                *(skb_put(*skb, 1)) = 0x05;            /* LAPB */
                break;
        case ISDN_PROTO_L2_HDLC:
                *(skb_put(*skb, 1)) = 0x02;
                break;
	case ISDN_PROTO_L2_TRANS:
		/* 
		 *	Voice (a-law)
		 */
		*(skb_put(*skb, 1)) = 0x06;
		break;
        default:
#ifdef DEBUG 
                printk(KERN_DEBUG "Transparent\n");
#endif
                *(skb_put(*skb, 1)) = 0x03;
                break;
        }

        *(skb_put(*skb, 1)) = (outgoing ? 0x02 : 0x42);    /* Don't ask */
        *(skb_put(*skb, 1)) = 0x00;
  
        *((ushort *) skb_put(*skb, 2)) = MRU;

 
        *(skb_put(*skb, 1)) = 0x08;           /* Modulo */
        *(skb_put(*skb, 1)) = 0x07;           /* Max Window */
  
        *(skb_put(*skb, 1)) = 0x01;           /* No Layer3 Protocol */

        /*
         * 2 - layer3 MTU       [10]
         *   - Modulo           [12]
         *   - Window           
         *   - layer1 proto     [14]
         *   - bitrate
         *   - sub-channel      [16]
         *   - layer1dataformat [17]
         */

        memset(skb_put(*skb, 8), 0, 8);

        return 18;
}


int capi_activate_transp_req(struct pcbit_chan *chan, struct sk_buff **skb)
{

	if ((*skb = dev_alloc_skb(7)) == NULL) {
    
		printk(KERN_WARNING "capi_activate_transp_req: alloc_skb failed\n");
		return -1;
	}

        *((ushort*) skb_put(*skb, 2) ) = chan->callref;  

        
        *(skb_put(*skb, 1)) = chan->layer2link; /* Layer2 id */
        *(skb_put(*skb, 1)) = 0x00;             /* Transmit by default */

        *((ushort *) skb_put(*skb, 2)) = MRU;

        *(skb_put(*skb, 1)) = 0x01;             /* Enables reception*/

        return 7;
}

int capi_tdata_req(struct pcbit_chan* chan, struct sk_buff *skb)
{
	ushort data_len;
	

	/*  
	 * callref      - 2  
	 * layer2link   - 1
	 * wBlockLength - 2 
	 * data         - 4
	 * sernum       - 1
	 */
	
	data_len = skb->len;

	if(skb_headroom(skb) < 10)
	{
		printk(KERN_CRIT "No headspace (%u) on headroom %p for capi header\n", skb_headroom(skb), skb);
	}
	else
	{	
		skb_push(skb, 10);
	}

	*((u16 *) (skb->data)) = chan->callref;
	skb->data[2] = chan->layer2link;
	*((u16 *) (skb->data + 3)) = data_len;

	chan->s_refnum = (chan->s_refnum + 1) % 8;
	*((u32 *) (skb->data + 5)) = chan->s_refnum;

	skb->data[9] = 0;                           /* HDLC frame number */

	return 10;
}

int capi_tdata_resp(struct pcbit_chan *chan, struct sk_buff ** skb)
		    
{
	if ((*skb = dev_alloc_skb(4)) == NULL) {
    
		printk(KERN_WARNING "capi_tdata_resp: alloc_skb failed\n");
		return -1;
	}

        *((ushort*) skb_put(*skb, 2) ) = chan->callref;  

        *(skb_put(*skb, 1)) = chan->layer2link;
        *(skb_put(*skb, 1)) = chan->r_refnum;

        return (*skb)->len;
}

int capi_disc_req(ushort callref, struct sk_buff **skb, u_char cause)
{

	if ((*skb = dev_alloc_skb(6)) == NULL) {
    
		printk(KERN_WARNING "capi_disc_req: alloc_skb failed\n");
		return -1;
	}

        *((ushort*) skb_put(*skb, 2) ) = callref;  

        *(skb_put(*skb, 1)) = 2;                  /* Cause.Length = 2; */
        *(skb_put(*skb, 1)) = 0x80;
        *(skb_put(*skb, 1)) = 0x80 | cause;           

        /* 
         * Change it: we should send 'Sic transit gloria Mundi' here ;-) 
         */

        *(skb_put(*skb, 1)) = 0;                   /* UTUS.Length = 0;  */

        return 6;
}

int capi_disc_resp(struct pcbit_chan *chan, struct sk_buff **skb)
{
	if ((*skb = dev_alloc_skb(2)) == NULL) {
    
		printk(KERN_WARNING "capi_disc_resp: alloc_skb failed\n");
		return -1;
	}

        *((ushort*) skb_put(*skb, 2)) = chan->callref;  

        return 2;
}


/*
 *  Decoding of CAPI messages
 *
 */

int capi_decode_conn_ind(struct pcbit_chan * chan, 
                         struct sk_buff *skb,
                         struct callb_data *info) 
{
        int CIlen, len;

        /* Call Reference [CAPI] */
        chan->callref = *((ushort*) skb->data);
        skb_pull(skb, 2);

#ifdef DEBUG
	printk(KERN_DEBUG "Call Reference: %04x\n", chan->callref); 
#endif

        /* Channel Identification */

        /* Expect  
           Len = 1 
           Octect 3 = 0100 10CC - [ 7 Basic, 4 , 2-1 chan ]
           */

        CIlen = skb->data[0];
#ifdef DEBUG
        if (CIlen == 1) {

                if ( ((skb->data[1]) & 0xFC) == 0x48 )
                        printk(KERN_DEBUG "decode_conn_ind: chan ok\n");
                printk(KERN_DEBUG "phyChan = %d\n", skb->data[1] & 0x03); 
        }
	else
		printk(KERN_DEBUG "conn_ind: CIlen = %d\n", CIlen);
#endif
        skb_pull(skb, CIlen + 1);

        /* Calling Party Number */
        /* An "additional service" as far as Portugal Telecom is concerned */

        len = skb->data[0];

	if (len > 0) {
		int count = 1;
		
#ifdef DEBUG
		printk(KERN_DEBUG "CPN: Octect 3 %02x\n", skb->data[1]);
#endif
		if ((skb->data[1] & 0x80) == 0)
			count = 2;
		
		if (!(info->data.setup.CallingPN = kmalloc(len - count + 1, GFP_ATOMIC)))
			return -1;
       
		memcpy(info->data.setup.CallingPN, skb->data + count + 1, 
		       len - count);
		info->data.setup.CallingPN[len - count] = 0;

	}
	else {
		info->data.setup.CallingPN = NULL;
		printk(KERN_DEBUG "NULL CallingPN\n");
	}

	skb_pull(skb, len + 1);

        /* Calling Party Subaddress */
        skb_pull(skb, skb->data[0] + 1);

        /* Called Party Number */

        len = skb->data[0];

	if (len > 0) {
		int count = 1;
		
		if ((skb->data[1] & 0x80) == 0)
			count = 2;
        
		if (!(info->data.setup.CalledPN = kmalloc(len - count + 1, GFP_ATOMIC)))
			return -1;
        
		memcpy(info->data.setup.CalledPN, skb->data + count + 1, 
		       len - count); 
		info->data.setup.CalledPN[len - count] = 0;

	}
	else {
		info->data.setup.CalledPN = NULL;
		printk(KERN_DEBUG "NULL CalledPN\n");
	}

	skb_pull(skb, len + 1);

        /* Called Party Subaddress */
        skb_pull(skb, skb->data[0] + 1);

        /* LLC */
        skb_pull(skb, skb->data[0] + 1);

        /* HLC */
        skb_pull(skb, skb->data[0] + 1);

        /* U2U */
        skb_pull(skb, skb->data[0] + 1);

        return 0;
}

/*
 *  returns errcode
 */

int capi_decode_conn_conf(struct pcbit_chan * chan, struct sk_buff *skb,
			  int *complete) 
{
        int errcode;
  
        chan->callref = *((ushort *) skb->data);     /* Update CallReference */
        skb_pull(skb, 2);

        errcode = *((ushort *) skb->data);   /* read errcode */
        skb_pull(skb, 2);

        *complete = *(skb->data);
        skb_pull(skb, 1);

        /* FIX ME */
        /* This is actually a firmware bug */
        if (!*complete)
        {
                printk(KERN_DEBUG "complete=%02x\n", *complete);
                *complete = 1;
        }


        /* Optional Bearer Capability */
        skb_pull(skb, *(skb->data) + 1);
        
        /* Channel Identification */
        skb_pull(skb, *(skb->data) + 1);

        /* High Layer Compatibility follows */
        skb_pull(skb, *(skb->data) + 1);

        return errcode;
}

int capi_decode_conn_actv_ind(struct pcbit_chan * chan, struct sk_buff *skb)
{
        ushort len;
#ifdef DEBUG
        char str[32];
#endif

        /* Yet Another Bearer Capability */
        skb_pull(skb, *(skb->data) + 1);
  

        /* Connected Party Number */
        len=*(skb->data);

#ifdef DEBUG
	if (len > 1 && len < 31) {
		memcpy(str, skb->data + 2, len - 1);
		str[len] = 0;
		printk(KERN_DEBUG "Connected Party Number: %s\n", str);
	}
	else
		printk(KERN_DEBUG "actv_ind CPN len = %d\n", len);
#endif

        skb_pull(skb, len + 1);

        /* Connected Subaddress */
        skb_pull(skb, *(skb->data) + 1);

        /* Low Layer Capability */
        skb_pull(skb, *(skb->data) + 1);

        /* High Layer Capability */
        skb_pull(skb, *(skb->data) + 1);

        return 0;
}

int capi_decode_conn_actv_conf(struct pcbit_chan * chan, struct sk_buff *skb)
{
        ushort errcode;

        errcode = *((ushort*) skb->data);
        skb_pull(skb, 2);
        
        /* Channel Identification 
        skb_pull(skb, skb->data[0] + 1);
        */
        return errcode;
}


int capi_decode_sel_proto_conf(struct pcbit_chan *chan, struct sk_buff *skb)
{
        ushort errcode;
        
        chan->layer2link = *(skb->data);
        skb_pull(skb, 1);

        errcode = *((ushort*) skb->data);
        skb_pull(skb, 2);

        return errcode;
}

int capi_decode_actv_trans_conf(struct pcbit_chan *chan, struct sk_buff *skb)
{
        ushort errcode;

        if (chan->layer2link != *(skb->data) )
                printk("capi_decode_actv_trans_conf: layer2link doesn't match\n");

        skb_pull(skb, 1);

        errcode = *((ushort*) skb->data);
        skb_pull(skb, 2);

        return errcode;        
}

int capi_decode_disc_ind(struct pcbit_chan *chan, struct sk_buff *skb)
{
        ushort len;
#ifdef DEBUG
        int i;
#endif
        /* Cause */
        
        len = *(skb->data);
        skb_pull(skb, 1);

#ifdef DEBUG

        for (i=0; i<len; i++)
                printk(KERN_DEBUG "Cause Octect %d: %02x\n", i+3, 
                       *(skb->data + i));
#endif

        skb_pull(skb, len);

        return 0;
}

#ifdef DEBUG
int capi_decode_debug_188(u_char *hdr, ushort hdrlen)
{
        char str[64];
        int len;
        
        len = hdr[0];

        if (len < 64 && len == hdrlen - 1) {        
                memcpy(str, hdr + 1, hdrlen - 1);
                str[hdrlen - 1] = 0;
                printk("%s\n", str);
        }
        else
                printk("debug message incorrect\n");

        return 0;
}
#endif





