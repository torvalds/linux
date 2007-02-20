/*
 * DSS.1 Finite State Machine
 * base: ITU-T Rec Q.931
 *
 * Copyright (C) 1996 Universidade de Lisboa
 * 
 * Written by Pedro Roque Marques (roque@di.fc.ul.pt)
 *
 * This software may be used and distributed according to the terms of 
 * the GNU General Public License, incorporated herein by reference.
 */

/*
 *        TODO: complete the FSM
 *              move state/event descriptions to a user space logger
 */

#include <linux/string.h>
#include <linux/kernel.h>

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/skbuff.h>

#include <linux/timer.h>
#include <asm/io.h>

#include <linux/isdnif.h>

#include "pcbit.h"
#include "edss1.h"
#include "layer2.h"
#include "callbacks.h"


char * isdn_state_table[] = {
  "Closed",
  "Call initiated",
  "Overlap sending",
  "Outgoing call proceeding",
  "NOT DEFINED",
  "Call delivered",
  "Call present",
  "Call received",
  "Connect request",
  "Incoming call proceeding",
  "Active",
  "Disconnect request",
  "Disconnect indication",
  "NOT DEFINED",
  "NOT DEFINED",
  "Suspend request",
  "NOT DEFINED",
  "Resume request",
  "NOT DEFINED",
  "Release Request",
  "NOT DEFINED",
  "NOT DEFINED",
  "NOT DEFINED",
  "NOT DEFINED",
  "NOT DEFINED",
  "Overlap receiving",
  "Select protocol on B-Channel",
  "Activate B-channel protocol"
};

#ifdef DEBUG_ERRS
static
struct CauseValue {
  byte nr;
  char *descr;
} cvlist[]={
  {0x01,"Unallocated (unassigned) number"},
  {0x02,"No route to specified transit network"},
  {0x03,"No route to destination"},
  {0x04,"Send special information tone"},
  {0x05,"Misdialled trunk prefix"},
  {0x06,"Channel unacceptable"},
  {0x07,"Channel awarded and being delivered in an established channel"},
  {0x08,"Preemption"},
  {0x09,"Preemption - circuit reserved for reuse"},
  {0x10,"Normal call clearing"},
  {0x11,"User busy"},
  {0x12,"No user responding"},
  {0x13,"No answer from user (user alerted)"},
  {0x14,"Subscriber absent"},
  {0x15,"Call rejected"},
  {0x16,"Number changed"},
  {0x1a,"non-selected user clearing"},
  {0x1b,"Destination out of order"},
  {0x1c,"Invalid number format (address incomplete)"},
  {0x1d,"Facility rejected"},
  {0x1e,"Response to Status enquiry"},
  {0x1f,"Normal, unspecified"},
  {0x22,"No circuit/channel available"},
  {0x26,"Network out of order"},
  {0x27,"Permanent frame mode connection out-of-service"},
  {0x28,"Permanent frame mode connection operational"},
  {0x29,"Temporary failure"},
  {0x2a,"Switching equipment congestion"},
  {0x2b,"Access information discarded"},
  {0x2c,"Requested circuit/channel not available"},
  {0x2e,"Precedence call blocked"},
  {0x2f,"Resource unavailable, unspecified"},
  {0x31,"Quality of service unavailable"},
  {0x32,"Requested facility not subscribed"},
  {0x35,"Outgoing calls barred within CUG"},
  {0x37,"Incoming calls barred within CUG"},
  {0x39,"Bearer capability not authorized"},
  {0x3a,"Bearer capability not presently available"},
  {0x3e,"Inconsistency in designated outgoing access information and subscriber class"},
  {0x3f,"Service or option not available, unspecified"},
  {0x41,"Bearer capability not implemented"},
  {0x42,"Channel type not implemented"},
  {0x43,"Requested facility not implemented"},
  {0x44,"Only restricted digital information bearer capability is available"},
  {0x4f,"Service or option not implemented"},
  {0x51,"Invalid call reference value"},
  {0x52,"Identified channel does not exist"},
  {0x53,"A suspended call exists, but this call identity does not"},
  {0x54,"Call identity in use"},
  {0x55,"No call suspended"},
  {0x56,"Call having the requested call identity has been cleared"},
  {0x57,"User not member of CUG"},
  {0x58,"Incompatible destination"},
  {0x5a,"Non-existent CUG"},
  {0x5b,"Invalid transit network selection"},
  {0x5f,"Invalid message, unspecified"},
  {0x60,"Mandatory information element is missing"},
  {0x61,"Message type non-existent or not implemented"},
  {0x62,"Message not compatible with call state or message type non-existent or not implemented"},
  {0x63,"Information element/parameter non-existent or not implemented"},
  {0x64,"Invalid information element contents"},
  {0x65,"Message not compatible with call state"},
  {0x66,"Recovery on timer expiry"},
  {0x67,"Parameter non-existent or not implemented - passed on"},
  {0x6e,"Message with unrecognized parameter discarded"},
  {0x6f,"Protocol error, unspecified"},
  {0x7f,"Interworking, unspecified"}
};

#endif

static struct isdn_event_desc {
  unsigned short ev;
  char * desc;
} isdn_event_table [] = {
  {EV_USR_SETUP_REQ,     "CC->L3: Setup Request"},
  {EV_USR_SETUP_RESP,    "CC->L3: Setup Response"},
  {EV_USR_PROCED_REQ,    "CC->L3: Proceeding Request"},
  {EV_USR_RELEASE_REQ,   "CC->L3: Release Request"},

  {EV_NET_SETUP,        "NET->TE: setup "},
  {EV_NET_CALL_PROC,    "NET->TE: call proceeding"},
  {EV_NET_SETUP_ACK,    "NET->TE: setup acknowledge (more info needed)"},
  {EV_NET_CONN,         "NET->TE: connect"},
  {EV_NET_CONN_ACK,     "NET->TE: connect acknowledge"},
  {EV_NET_DISC,         "NET->TE: disconnect indication"},
  {EV_NET_RELEASE,      "NET->TE: release"},
  {EV_NET_RELEASE_COMP, "NET->TE: release complete"},
  {EV_NET_SELP_RESP,    "Board: Select B-channel protocol ack"},
  {EV_NET_ACTV_RESP,    "Board: Activate B-channel protocol ack"},
  {EV_TIMER,            "Timeout"},
  {0, "NULL"}
};

char * strisdnevent(ushort ev)
{
  struct isdn_event_desc * entry;
 
  for (entry = isdn_event_table; entry->ev; entry++)
    if (entry->ev == ev)
      break;

  return entry->desc;
}

/*
 * Euro ISDN finite state machine
 */

static struct fsm_timer_entry fsm_timers[] = {
  {ST_CALL_PROC, 10},
  {ST_DISC_REQ, 2},
  {ST_ACTIVE_SELP, 5},
  {ST_ACTIVE_ACTV, 5},
  {ST_INCM_PROC, 10},
  {ST_CONN_REQ, 2},
  {0xff, 0}
};

static struct fsm_entry fsm_table[] = {
/* Connect Phase */
  /* Outgoing */
  {ST_NULL, ST_CALL_INIT, EV_USR_SETUP_REQ, cb_out_1},

  {ST_CALL_INIT, ST_OVER_SEND, EV_NET_SETUP_ACK, cb_notdone},
  {ST_CALL_INIT, ST_CALL_PROC, EV_NET_CALL_PROC, NULL},
  {ST_CALL_INIT, ST_NULL, EV_NET_DISC, cb_out_2},

  {ST_CALL_PROC, ST_ACTIVE_SELP, EV_NET_CONN, cb_out_2},
  {ST_CALL_PROC, ST_NULL, EV_NET_DISC, cb_disc_1},
  {ST_CALL_PROC, ST_DISC_REQ, EV_USR_RELEASE_REQ, cb_disc_2},

  /* Incoming */
  {ST_NULL, ST_CALL_PRES, EV_NET_SETUP, NULL},

  {ST_CALL_PRES, ST_INCM_PROC, EV_USR_PROCED_REQ, cb_in_1},
  {ST_CALL_PRES, ST_DISC_REQ, EV_USR_RELEASE_REQ, cb_disc_2},

  {ST_INCM_PROC, ST_CONN_REQ, EV_USR_SETUP_RESP, cb_in_2},
  {ST_INCM_PROC, ST_DISC_REQ, EV_USR_RELEASE_REQ, cb_disc_2},

  {ST_CONN_REQ, ST_ACTIVE_SELP, EV_NET_CONN_ACK, cb_in_3},

  /* Active */
  {ST_ACTIVE, ST_NULL, EV_NET_DISC, cb_disc_1},
  {ST_ACTIVE, ST_DISC_REQ, EV_USR_RELEASE_REQ, cb_disc_2},
  {ST_ACTIVE, ST_NULL, EV_NET_RELEASE, cb_disc_3},

  /* Disconnect */

  {ST_DISC_REQ, ST_NULL, EV_NET_DISC, cb_disc_1},
  {ST_DISC_REQ, ST_NULL, EV_NET_RELEASE, cb_disc_3},

  /* protocol selection */
  {ST_ACTIVE_SELP, ST_ACTIVE_ACTV, EV_NET_SELP_RESP, cb_selp_1},
  {ST_ACTIVE_SELP, ST_DISC_REQ, EV_USR_RELEASE_REQ, cb_disc_2},

  {ST_ACTIVE_ACTV, ST_ACTIVE, EV_NET_ACTV_RESP, cb_open},
  {ST_ACTIVE_ACTV, ST_DISC_REQ, EV_USR_RELEASE_REQ, cb_disc_2},

  /* Timers */
  {ST_CALL_PROC, ST_DISC_REQ, EV_TIMER, cb_disc_2},
  {ST_DISC_REQ, ST_NULL, EV_TIMER, cb_disc_3},
  {ST_ACTIVE_SELP, ST_DISC_REQ, EV_TIMER, cb_disc_2},
  {ST_ACTIVE_ACTV, ST_DISC_REQ, EV_TIMER, cb_disc_2},        
  {ST_INCM_PROC, ST_DISC_REQ, EV_TIMER, cb_disc_2},
  {ST_CONN_REQ, ST_CONN_REQ, EV_TIMER, cb_in_2},
        
  {0xff, 0, 0, NULL}
};


static void pcbit_fsm_timer(unsigned long data)
{
        struct pcbit_dev *dev;
        struct pcbit_chan *chan;

        chan = (struct pcbit_chan *) data;

        del_timer(&chan->fsm_timer);
        chan->fsm_timer.function = NULL;

        dev = chan2dev(chan);

        if (dev == NULL) {
                printk(KERN_WARNING "pcbit: timer for unknown device\n");
                return;
        }

        pcbit_fsm_event(dev, chan, EV_TIMER, NULL);
}


void pcbit_fsm_event(struct pcbit_dev *dev, struct pcbit_chan *chan,
		   unsigned short event, struct callb_data *data)
{
	struct fsm_entry * action;	
	struct fsm_timer_entry *tentry;
	unsigned long flags;

	spin_lock_irqsave(&dev->lock, flags);

        for (action = fsm_table; action->init != 0xff; action++)
                if (action->init == chan->fsm_state && action->event == event)
                        break;
  
	if (action->init == 0xff) {
		
		spin_unlock_irqrestore(&dev->lock, flags);
		printk(KERN_DEBUG "fsm error: event %x on state %x\n", 
                       event, chan->fsm_state);
		return;
	}

        if (chan->fsm_timer.function) {
                del_timer(&chan->fsm_timer);
                chan->fsm_timer.function = NULL;
        }

	chan->fsm_state = action->final;
  
	pcbit_state_change(dev, chan, action->init, event, action->final);

        for (tentry = fsm_timers; tentry->init != 0xff; tentry++)
                if (tentry->init == chan->fsm_state)
                        break;

        if (tentry->init != 0xff) {
                init_timer(&chan->fsm_timer);
                chan->fsm_timer.function = &pcbit_fsm_timer;
                chan->fsm_timer.data = (ulong) chan;
                chan->fsm_timer.expires = jiffies + tentry->timeout * HZ;
                add_timer(&chan->fsm_timer);
        }

	spin_unlock_irqrestore(&dev->lock, flags);

	if (action->callb)
		action->callb(dev, chan, data);

}




