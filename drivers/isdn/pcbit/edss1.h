/*
 * DSS.1 module definitions
 *
 * Copyright (C) 1996 Universidade de Lisboa
 * 
 * Written by Pedro Roque Marques (roque@di.fc.ul.pt)
 *
 * This software may be used and distributed according to the terms of 
 * the GNU General Public License, incorporated herein by reference.
 */

#ifndef EDSS1_H
#define EDSS1_H

/* ISDN states */

#define ST_NULL      0
#define ST_CALL_INIT 1    /* Call initiated */
#define ST_OVER_SEND 2    /* Overlap sending - Requests More Info 4 call */
#define ST_CALL_PROC 3    /* Call Proceeding */
#define ST_CALL_DELV 4
#define ST_CALL_PRES 6    /* Call Present - Received CONN.IND */
#define ST_CALL_RECV 7    /* Alerting sent */
#define ST_CONN_REQ  8    /* Answered - waiting 4 CONN.CONF */
#define ST_INCM_PROC 9
#define ST_ACTIVE    10
#define ST_DISC_REQ  11
#define ST_DISC_IND  12
#define ST_SUSP_REQ  15
#define ST_RESM_REQ  17
#define ST_RELS_REQ  19
#define ST_OVER_RECV 25

#define ST_ACTIVE_SELP 26 /* Select protocol on B-Channel */
#define ST_ACTIVE_ACTV 27 /* Activate B-channel protocol  */

#define MAX_STATE ST_ACTIVE_ACTV

#define EV_NULL               0
#define EV_USR_SETUP_REQ      1
#define EV_USR_SETUP_RESP     2
#define EV_USR_PROCED_REQ     3
#define EV_USR_RELEASE_REQ    4
#define EV_USR_REJECT_REQ     4

#define EV_NET_SETUP          16
#define EV_NET_CALL_PROC      17
#define EV_NET_SETUP_ACK      18
#define EV_NET_CONN           19
#define EV_NET_CONN_ACK       20

#define EV_NET_SELP_RESP      21
#define EV_NET_ACTV_RESP      22

#define EV_NET_DISC           23
#define EV_NET_RELEASE        24
#define EV_NET_RELEASE_COMP   25

#define EV_TIMER              26
#define EV_ERROR              32

/*
 *  Cause values
 *  only the ones we use
 */ 

#define CAUSE_NORMAL          0x10U 
#define CAUSE_NOCHAN          0x22U

struct callb_data {
	unsigned short type;
	union {
		struct ConnInfo {
			char *CalledPN;
			char *CallingPN;
		} setup;
		unsigned short cause;
	} data;
};

struct fsm_entry {
	unsigned short init;
	unsigned short final;
	unsigned short event;
	void (*callb)(struct pcbit_dev *, struct pcbit_chan *, struct callb_data*);
};

struct fsm_timer_entry {
	unsigned short init;
	unsigned long timeout;          /* in seconds */
};


extern void pcbit_fsm_event(struct pcbit_dev *, struct pcbit_chan *,
			    unsigned short event, struct callb_data *);
#endif



