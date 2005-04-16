/* $Id: l3ni1.h,v 2.3.6.2 2001/09/23 22:24:50 kai Exp $
 *
 * NI1 D-channel protocol
 *
 * Author       Matt Henderson & Guy Ellis
 * Copyright    by Traverse Technologies Pty Ltd, www.travers.com.au
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * 2000.6.6 Initial implementation of routines for US NI1 
 * Layer 3 protocol based on the EURO/DSS1 D-channel protocol 
 * driver written by Karsten Keil et al.  Thanks also for the 
 * code provided by Ragnar Paulson.
 *
 */

#ifndef l3ni1_process

#define T302	15000
#define T303	4000
#define T304	30000
#define T305	30000
#define T308	4000
/* for layer 1 certification T309 < layer1 T3 (e.g. 4000) */
/* This makes some tests easier and quicker */
#define T309	40000
#define T310	30000
#define T313	4000
#define T318	4000
#define T319	4000
#define TSPID	5000 /* was 2000 - Guy Ellis */

/*
 * Message-Types
 */

#define MT_ALERTING		0x01
#define MT_CALL_PROCEEDING	0x02
#define MT_CONNECT		0x07
#define MT_CONNECT_ACKNOWLEDGE	0x0f
#define MT_PROGRESS		0x03
#define MT_SETUP		0x05
#define MT_SETUP_ACKNOWLEDGE	0x0d
#define MT_RESUME		0x26
#define MT_RESUME_ACKNOWLEDGE	0x2e
#define MT_RESUME_REJECT	0x22
#define MT_SUSPEND		0x25
#define MT_SUSPEND_ACKNOWLEDGE	0x2d
#define MT_SUSPEND_REJECT	0x21
#define MT_USER_INFORMATION	0x20
#define MT_DISCONNECT		0x45
#define MT_RELEASE		0x4d
#define MT_RELEASE_COMPLETE	0x5a
#define MT_RESTART		0x46
#define MT_RESTART_ACKNOWLEDGE	0x4e
#define MT_SEGMENT		0x60
#define MT_CONGESTION_CONTROL	0x79
#define MT_INFORMATION		0x7b
#define MT_FACILITY		0x62
#define MT_NOTIFY		0x6e
#define MT_STATUS		0x7d
#define MT_STATUS_ENQUIRY	0x75
#define MT_DL_ESTABLISHED	0xfe

#define IE_SEGMENT	0x00
#define IE_BEARER	0x04
#define IE_CAUSE	0x08
#define IE_CALL_ID	0x10
#define IE_CALL_STATE	0x14
#define IE_CHANNEL_ID	0x18
#define IE_FACILITY	0x1c
#define IE_PROGRESS	0x1e
#define IE_NET_FAC	0x20
#define IE_NOTIFY	0x27
#define IE_DISPLAY	0x28
#define IE_DATE		0x29
#define IE_KEYPAD	0x2c
#define IE_SIGNAL	0x34
#define IE_SPID		0x3a
#define IE_ENDPOINT_ID	0x3b
#define IE_INFORATE	0x40
#define IE_E2E_TDELAY	0x42
#define IE_TDELAY_SEL	0x43
#define IE_PACK_BINPARA	0x44
#define IE_PACK_WINSIZE	0x45
#define IE_PACK_SIZE	0x46
#define IE_CUG		0x47
#define	IE_REV_CHARGE	0x4a
#define IE_CONNECT_PN	0x4c
#define IE_CONNECT_SUB	0x4d
#define IE_CALLING_PN	0x6c
#define IE_CALLING_SUB	0x6d
#define IE_CALLED_PN	0x70
#define IE_CALLED_SUB	0x71
#define IE_REDIR_NR	0x74
#define IE_TRANS_SEL	0x78
#define IE_RESTART_IND	0x79
#define IE_LLC		0x7c
#define IE_HLC		0x7d
#define IE_USER_USER	0x7e
#define IE_ESCAPE	0x7f
#define IE_SHIFT	0x90
#define IE_MORE_DATA	0xa0
#define IE_COMPLETE	0xa1
#define IE_CONGESTION	0xb0
#define IE_REPEAT	0xd0

#define IE_MANDATORY	0x0100
/* mandatory not in every case */
#define IE_MANDATORY_1	0x0200

#define ERR_IE_COMPREHENSION	 1
#define ERR_IE_UNRECOGNIZED	-1
#define ERR_IE_LENGTH		-2
#define ERR_IE_SEQUENCE		-3

#else /* only l3ni1_process */

/* l3ni1 specific data in l3 process */
typedef struct
  { unsigned char invoke_id; /* used invoke id in remote ops, 0 = not active */
    ulong ll_id; /* remebered ll id */
    u8 remote_operation; /* handled remote operation, 0 = not active */ 
    int proc; /* rememered procedure */  
    ulong remote_result; /* result of remote operation for statcallb */
    char uus1_data[35]; /* data send during alerting or disconnect */
  } ni1_proc_priv;

/* l3dni1 specific data in protocol stack */
typedef struct
  { unsigned char last_invoke_id; /* last used value for invoking */
    unsigned char invoke_used[32]; /* 256 bits for 256 values */
  } ni1_stk_priv;        

#endif /* only l3dni1_process */
