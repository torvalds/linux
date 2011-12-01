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

//#define AGGR_DEBUG

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
#define HOLD_Q_SZ(_x)   (TID_WINDOW_SZ((_x))*sizeof(OSBUF_HOLD_Q))
/* AGGR_RX_TIMEOUT value is important as a (too) small value can cause frames to be 
 * delivered out of order and a (too) large value can cause undesirable latency in
 * certain situations. */
#define AGGR_RX_TIMEOUT     400  /* Timeout(in ms) for delivery of frames, if they are stuck */

typedef enum {
    ALL_SEQNO = 0,
    CONTIGUOUS_SEQNO = 1,
}DELIVERY_ORDER;

typedef struct {
    void        *osbuf;
    A_BOOL      is_amsdu;
    A_UINT16    seq_no;
}OSBUF_HOLD_Q;


#if 0
typedef struct {
    A_UINT16    seqno_st;
    A_UINT16    seqno_end;
}WINDOW_SNAPSHOT;
#endif

typedef struct {
    A_BOOL              aggr;       /* is it ON or OFF */
    A_BOOL              progress;   /* TRUE when frames have arrived after a timer start */
    A_BOOL              timerMon;   /* TRUE if the timer started for the sake of this TID */
    A_UINT16            win_sz;     /* negotiated window size */
    A_UINT16            seq_next;   /* Next seq no, in current window */
    A_UINT32            hold_q_sz;  /* Num of frames that can be held in hold q */
    OSBUF_HOLD_Q        *hold_q;    /* Hold q for re-order */
#if 0    
    WINDOW_SNAPSHOT     old_win;    /* Sliding window snapshot - for timeout */
#endif    
    A_NETBUF_QUEUE_T    q;          /* q head for enqueuing frames for dispatch */
    A_MUTEX_T           lock;
}RXTID;

typedef struct {
    A_UINT32    num_into_aggr;      /* hitting at the input of this module */
    A_UINT32    num_dups;           /* duplicate */
    A_UINT32    num_oow;            /* out of window */
    A_UINT32    num_mpdu;           /* single payload 802.3/802.11 frame */
    A_UINT32    num_amsdu;          /* AMSDU */
    A_UINT32    num_delivered;      /* frames delivered to IP stack */
    A_UINT32    num_timeouts;       /* num of timeouts, during which frames delivered */
    A_UINT32    num_hole;           /* frame not present, when window moved over */
    A_UINT32    num_bar;            /* num of resets of seq_num, via BAR */
}RXTID_STATS;

typedef struct {
    void                *dev;               /* dev handle */
    A_UINT8             aggr_sz;            /* config value of aggregation size */    
    A_UINT8             timerScheduled;
    A_TIMER             timer;              /* timer for returning held up pkts in re-order que */    
    RXTID               RxTid[NUM_OF_TIDS]; /* Per tid window */
#ifdef AGGR_DEBUG
    RXTID_STATS         stat[NUM_OF_TIDS];  /* Tid based statistics */
#endif
}AGGR_CONN_INFO;

typedef struct {
    RX_CALLBACK         rx_fn;              /* callback function to return frames; to upper layer */
    ALLOC_NETBUFS       netbuf_allocator;   /* OS netbuf alloc fn */
    A_NETBUF_QUEUE_T    freeQ;              /* pre-allocated buffers - for A_MSDU slicing */
#ifdef AGGR_DEBUG
    PACKET_LOG          pkt_log;            /* Log info of the packets */
#endif    
}AGGR_INFO;

#endif /* __AGGR_RX_INTERNAL_H__ */
