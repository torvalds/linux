#ifndef __VFP_QUEUE_H__
#define __VFP_QUEUE_H__
#include <linux/amlogic/amports/vfp.h>

static inline bool vfq_full(vfq_t *q)
{
    return (((q->wp+1) % q->size )  == q->rp);
}
#endif
