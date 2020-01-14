/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_FALCON_QMGR_H__
#define __NVKM_FALCON_QMGR_H__
#include <core/falcon.h>
#include "msgqueue.h"

#define HDR_SIZE sizeof(struct nvkm_msgqueue_hdr)
#define QUEUE_ALIGNMENT 4
/* max size of the messages we can receive */
#define MSG_BUF_SIZE 128

struct nvkm_msgqueue_seq *msgqueue_seq_acquire(struct nvkm_msgqueue *);
void msgqueue_seq_release(struct nvkm_msgqueue *, struct nvkm_msgqueue_seq *);
#endif
