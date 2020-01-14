/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_FALCON_QMGR_H__
#define __NVKM_FALCON_QMGR_H__
#include <core/falcon.h>
#include "msgqueue.h"

#define HDR_SIZE sizeof(struct nv_falcon_msg)
#define QUEUE_ALIGNMENT 4
/* max size of the messages we can receive */
#define MSG_BUF_SIZE 128

/**
 * struct nvkm_msgqueue_seq - keep track of ongoing commands
 *
 * Every time a command is sent, a sequence is assigned to it so the
 * corresponding message can be matched. Upon receiving the message, a callback
 * can be called and/or a completion signaled.
 *
 * @id:		sequence ID
 * @state:	current state
 * @callback:	callback to call upon receiving matching message
 * @completion:	completion to signal after callback is called
 */
struct nvkm_msgqueue_seq {
	u16 id;
	enum {
		SEQ_STATE_FREE = 0,
		SEQ_STATE_PENDING,
		SEQ_STATE_USED,
		SEQ_STATE_CANCELLED
	} state;
	bool async;
	nvkm_falcon_qmgr_callback callback;
	void *priv;
	struct completion done;
	int result;
};

/*
 * We can have an arbitrary number of sequences, but realistically we will
 * probably not use that much simultaneously.
 */
#define NVKM_MSGQUEUE_NUM_SEQUENCES 16

struct nvkm_falcon_qmgr {
	struct nvkm_falcon *falcon;

	struct mutex seq_lock;
	struct nvkm_msgqueue_seq seq[NVKM_MSGQUEUE_NUM_SEQUENCES];
	unsigned long seq_tbl[BITS_TO_LONGS(NVKM_MSGQUEUE_NUM_SEQUENCES)];
};

struct nvkm_msgqueue_seq *
nvkm_falcon_qmgr_seq_acquire(struct nvkm_falcon_qmgr *);
void nvkm_falcon_qmgr_seq_release(struct nvkm_falcon_qmgr *,
				  struct nvkm_msgqueue_seq *);

#define FLCNQ_PRINTK(t,q,f,a...)                                               \
       FLCN_PRINTK(t, (q)->qmgr->falcon, "%s: "f, (q)->name, ##a)
#define FLCNQ_DBG(q,f,a...) FLCNQ_PRINTK(debug, (q), f, ##a)
#define FLCNQ_ERR(q,f,a...) FLCNQ_PRINTK(error, (q), f, ##a)
#endif
