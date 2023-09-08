/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_FALCON_QMGR_H__
#define __NVKM_FALCON_QMGR_H__
#include <core/falcon.h>

#define HDR_SIZE sizeof(struct nvfw_falcon_msg)
#define QUEUE_ALIGNMENT 4
/* max size of the messages we can receive */
#define MSG_BUF_SIZE 128

/**
 * struct nvkm_falcon_qmgr_seq - keep track of ongoing commands
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
struct nvkm_falcon_qmgr_seq {
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
#define NVKM_FALCON_QMGR_SEQ_NUM 16

struct nvkm_falcon_qmgr {
	struct nvkm_falcon *falcon;

	struct {
		struct mutex mutex;
		struct nvkm_falcon_qmgr_seq id[NVKM_FALCON_QMGR_SEQ_NUM];
		unsigned long tbl[BITS_TO_LONGS(NVKM_FALCON_QMGR_SEQ_NUM)];
	} seq;
};

struct nvkm_falcon_qmgr_seq *
nvkm_falcon_qmgr_seq_acquire(struct nvkm_falcon_qmgr *);
void nvkm_falcon_qmgr_seq_release(struct nvkm_falcon_qmgr *,
				  struct nvkm_falcon_qmgr_seq *);

struct nvkm_falcon_cmdq {
	struct nvkm_falcon_qmgr *qmgr;
	const char *name;
	struct mutex mutex;
	struct completion ready;

	u32 head_reg;
	u32 tail_reg;
	u32 offset;
	u32 size;

	u32 position;
};

struct nvkm_falcon_msgq {
	struct nvkm_falcon_qmgr *qmgr;
	const char *name;
	spinlock_t lock;

	u32 head_reg;
	u32 tail_reg;
	u32 offset;

	u32 position;
};

#define FLCNQ_PRINTK(q,l,p,f,a...) FLCN_PRINTK((q)->qmgr->falcon, l, p, "%s: "f, (q)->name, ##a)
#define FLCNQ_DBG(q,f,a...) FLCNQ_PRINTK((q), DEBUG, info, f, ##a)
#define FLCNQ_ERR(q,f,a...) FLCNQ_PRINTK((q), ERROR, err, f, ##a)
#endif
