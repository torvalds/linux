/*
 *
 * Copyright (c) 2004-2010 Atheros Communications Inc.
 * All rights reserved.
 *
 * 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
 *
 */

#ifndef __AGGR_RX_INTERNAL_H__
#define __AGGR_RX_INTERNAL_H__

#include "a_osapi.h"
#include "aggr_recv_api.h"

#define AGGR_WIN_IDX(x, y)          ((x) % (y))
#define AGGR_INCR_IDX(x, y)         AGGR_WIN_IDX(((x)+1), (y))
#define AGGR_DCRM_IDX(x, y)         AGGR_WIN_IDX(((x)-1), (y))
#define IEEE80211_MAX_SEQ_NO        0xFFF
#define IEEE80211_NEXT_SEQ_NO(x)    (((x) + 1) & IEEE80211_MAX_SEQ_NO)


#define NUM_OF_TIDS         8
#define AGGR_SZ_DEFAULT     8

#define AGGR_WIN_SZ_MIN     2
#define AGGR_WIN_SZ_MAX     8
/* TID Window sz is double of what is negotiated. Derive TID_WINDOW_SZ from win_sz, per tid */
#define TID_WINDOW_SZ(_x)   ((_x) << 1)

#define AGGR_NUM_OF_FREE_NETBUFS    16

#define AGGR_GET_RXTID_STATS(_p, _x)    (&(_p->stat[(_x)]))
#define AGGR_GET_RXTID(_p, _x)    (&(_p->RxTid[(_x)]))

/* Hold q is a function of win_sz, which is negotiated per tid */
#define HOLD_Q_SZ(_x)   (TID_WINDOW_SZ((_x))*sizeof(struct osbuf_hold_q))
/* AGGR_RX_TIMEOUT value is important as a (too) small value can cause frames to be 
 * delivered out of order and a (too) large value can cause undesirable latency in
 * certain situations. */
#define AGGR_RX_TIMEOUT     400  /* Timeout(in ms) for delivery of frames, if they are stuck */

typedef enum {
    ALL_SEQNO = 0,
    CONTIGUOUS_SEQNO = 1,
}DELIVERY_ORDER;

struct osbuf_hold_q {
    void        *osbuf;
    bool      is_amsdu;
    u16 seq_no;
};


#if 0
/* XXX: unused ? */
struct window_snapshot {
    u16 seqno_st;
    u16 seqno_end;
};
#endif

struct rxtid {
    bool              aggr;       /* is it ON or OFF */
    bool              progress;   /* true when frames have arrived after a timer start */
    bool              timerMon;   /* true if the timer started for the sake of this TID */
    u16 win_sz;     /* negotiated window size */
    u16 seq_next;   /* Next seq no, in current window */
    u32 hold_q_sz;  /* Num of frames that can be held in hold q */
    struct osbuf_hold_q        *hold_q;    /* Hold q for re-order */
#if 0    
    struct window_snapshot     old_win;    /* Sliding window snapshot - for timeout */
#endif    
    A_NETBUF_QUEUE_T    q;          /* q head for enqueuing frames for dispatch */
    A_MUTEX_T           lock;
};

struct rxtid_stats {
    u32 num_into_aggr;      /* hitting at the input of this module */
    u32 num_dups;           /* duplicate */
    u32 num_oow;            /* out of window */
    u32 num_mpdu;           /* single payload 802.3/802.11 frame */
    u32 num_amsdu;          /* AMSDU */
    u32 num_delivered;      /* frames delivered to IP stack */
    u32 num_timeouts;       /* num of timeouts, during which frames delivered */
    u32 num_hole;           /* frame not present, when window moved over */
    u32 num_bar;            /* num of resets of seq_num, via BAR */
};

struct aggr_info {
    u8 aggr_sz;            /* config value of aggregation size */
    u8 timerScheduled;
    A_TIMER             timer;              /* timer for returning held up pkts in re-order que */    
    void                *dev;               /* dev handle */
    RX_CALLBACK         rx_fn;              /* callback function to return frames; to upper layer */
    struct rxtid               RxTid[NUM_OF_TIDS]; /* Per tid window */
    ALLOC_NETBUFS       netbuf_allocator;   /* OS netbuf alloc fn */
    A_NETBUF_QUEUE_T    freeQ;              /* pre-allocated buffers - for A_MSDU slicing */
    struct rxtid_stats         stat[NUM_OF_TIDS];  /* Tid based statistics */
    PACKET_LOG          pkt_log;            /* Log info of the packets */
};

#endif /* __AGGR_RX_INTERNAL_H__ */
