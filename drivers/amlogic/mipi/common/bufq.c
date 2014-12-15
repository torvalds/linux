/*******************************************************************
 *
 *  Copyright C 2012 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description:
 *
 *  Author: Amlogic Software
 *  Created: 2012/3/13   19:46
 *
 *******************************************************************/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <asm/atomic.h>
#include <linux/mipi/am_mipi_csi2.h>
#include <linux/spinlock.h>
#include "bufq.h"

static inline void ptr_atomic_wrap_inc(u32 *ptr, u32 count)
{
    u32 i = *ptr;
    i++;
    if (i >= count)
        i = 0;
    *ptr = i;
}

inline bool bufq_empty(bufq_t *q)
{
    return (q->rd_index == q->wr_index);
}

inline bool bufq_full(bufq_t *q)
{
    return ((q->wr_index>q->rd_index)&&((q->wr_index-q->rd_index)>=q->count));
}

inline bool bufq_empty_free(mipi_buf_t* buff)
{
    return bufq_empty(&buff->free_q);
}

inline bool bufq_empty_available(mipi_buf_t* buff)
{
    return bufq_empty(&buff->available_q);
}

inline void bufq_push(bufq_t *q, am_csi2_frame_t *frame)
{
    u32 index = q->wr_index;
    if(bufq_full(q))
        return;
    q->pool[index%q->count] = frame;
    index ++;
    if(index>=(q->count*2)){
        index -= q->count;
        q->rd_index = q->rd_index%q->count;
    }
    q->wr_index = index;
    //ptr_atomic_wrap_inc(&q->wr_index,q->count);
}

inline void bufq_push_free(mipi_buf_t* buff, am_csi2_frame_t *frame)
{
    if(frame == NULL)
        return;
    spin_lock(&buff->q_lock);
    frame->status = AM_CSI2_BUFF_STATUS_FREE;
    bufq_push(&buff->free_q, frame);
    spin_unlock(&buff->q_lock);
}

inline void bufq_push_available(mipi_buf_t* buff, am_csi2_frame_t *frame)
{
    if(frame == NULL)
        return;
    spin_lock(&buff->q_lock);
    frame->status = AM_CSI2_BUFF_STATUS_AVAIL;
    bufq_push(&buff->available_q, frame);
    spin_unlock(&buff->q_lock);
}

inline am_csi2_frame_t *bufq_pop(bufq_t *q)
{
    am_csi2_frame_t * frame = NULL;
    if (bufq_empty(q))
        return NULL;

    frame = q->pool[q->rd_index];
    frame->status = AM_CSI2_BUFF_STATUS_BUSY;
    q->rd_index++;
    if(q->rd_index>=q->count){
        q->rd_index -= q->count;
        q->wr_index = q->wr_index%q->count;
    }
    //ptr_atomic_wrap_inc(&q->rd_index,q->count);
    return frame;
}

inline am_csi2_frame_t *bufq_pop_free(mipi_buf_t* buff)
{
    am_csi2_frame_t * frame = NULL;
    spin_lock(&buff->q_lock);
    frame = bufq_pop(&buff->free_q);
    if(frame){
        frame->w = 0;
        frame->h = 0;
        frame->read_cnt = 0;
        frame->err = 0;
    }
    spin_unlock(&buff->q_lock);
    return frame;
}

inline am_csi2_frame_t *bufq_pop_available(mipi_buf_t* buff)
{
    am_csi2_frame_t * frame = NULL;
    spin_lock(&buff->q_lock);
    frame = bufq_pop(&buff->available_q);
    spin_unlock(&buff->q_lock);
    return frame;
}


static inline am_csi2_frame_t *bufq_peek(bufq_t *q)
{
    if (bufq_empty(q))
        return NULL;
    return q->pool[q->rd_index];
}

void bufq_init(mipi_buf_t* buff, am_csi2_frame_t* frame, unsigned count)
{
    int i = 0;
    spin_lock(&buff->q_lock);
    buff->free_q.rd_index = buff->free_q.wr_index = 0;
    buff->available_q.rd_index = buff->available_q.wr_index = 0;
    buff->free_q.count = count;
    buff->available_q.count = count;
    for (i = 0; i < count ; i++){
        frame[i].status = AM_CSI2_BUFF_STATUS_FREE;
        frame[i].w = 0;
        frame[i].h = 0;
        frame[i].read_cnt = 0;
        frame[i].err = 0;
        bufq_push(&buff->free_q, &frame[i]);
    }
    spin_unlock(&buff->q_lock);
    return;
}
