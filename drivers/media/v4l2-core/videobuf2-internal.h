#ifndef _MEDIA_VIDEOBUF2_INTERNAL_H
#define _MEDIA_VIDEOBUF2_INTERNAL_H

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <media/videobuf2-core.h>

extern int vb2_debug;

#define dprintk(level, fmt, arg...)					      \
	do {								      \
		if (vb2_debug >= level)					      \
			pr_info("vb2: %s: " fmt, __func__, ## arg); \
	} while (0)

#ifdef CONFIG_VIDEO_ADV_DEBUG

/*
 * If advanced debugging is on, then count how often each op is called
 * successfully, which can either be per-buffer or per-queue.
 *
 * This makes it easy to check that the 'init' and 'cleanup'
 * (and variations thereof) stay balanced.
 */

#define log_memop(vb, op)						\
	dprintk(2, "call_memop(%p, %d, %s)%s\n",			\
		(vb)->vb2_queue, (vb)->index, #op,			\
		(vb)->vb2_queue->mem_ops->op ? "" : " (nop)")

#define call_memop(vb, op, args...)					\
({									\
	struct vb2_queue *_q = (vb)->vb2_queue;				\
	int err;							\
									\
	log_memop(vb, op);						\
	err = _q->mem_ops->op ? _q->mem_ops->op(args) : 0;		\
	if (!err)							\
		(vb)->cnt_mem_ ## op++;					\
	err;								\
})

#define call_ptr_memop(vb, op, args...)					\
({									\
	struct vb2_queue *_q = (vb)->vb2_queue;				\
	void *ptr;							\
									\
	log_memop(vb, op);						\
	ptr = _q->mem_ops->op ? _q->mem_ops->op(args) : NULL;		\
	if (!IS_ERR_OR_NULL(ptr))					\
		(vb)->cnt_mem_ ## op++;					\
	ptr;								\
})

#define call_void_memop(vb, op, args...)				\
({									\
	struct vb2_queue *_q = (vb)->vb2_queue;				\
									\
	log_memop(vb, op);						\
	if (_q->mem_ops->op)						\
		_q->mem_ops->op(args);					\
	(vb)->cnt_mem_ ## op++;						\
})

#define log_qop(q, op)							\
	dprintk(2, "call_qop(%p, %s)%s\n", q, #op,			\
		(q)->ops->op ? "" : " (nop)")

#define call_qop(q, op, args...)					\
({									\
	int err;							\
									\
	log_qop(q, op);							\
	err = (q)->ops->op ? (q)->ops->op(args) : 0;			\
	if (!err)							\
		(q)->cnt_ ## op++;					\
	err;								\
})

#define call_void_qop(q, op, args...)					\
({									\
	log_qop(q, op);							\
	if ((q)->ops->op)						\
		(q)->ops->op(args);					\
	(q)->cnt_ ## op++;						\
})

#define log_vb_qop(vb, op, args...)					\
	dprintk(2, "call_vb_qop(%p, %d, %s)%s\n",			\
		(vb)->vb2_queue, (vb)->index, #op,			\
		(vb)->vb2_queue->ops->op ? "" : " (nop)")

#define call_vb_qop(vb, op, args...)					\
({									\
	int err;							\
									\
	log_vb_qop(vb, op);						\
	err = (vb)->vb2_queue->ops->op ?				\
		(vb)->vb2_queue->ops->op(args) : 0;			\
	if (!err)							\
		(vb)->cnt_ ## op++;					\
	err;								\
})

#define call_void_vb_qop(vb, op, args...)				\
({									\
	log_vb_qop(vb, op);						\
	if ((vb)->vb2_queue->ops->op)					\
		(vb)->vb2_queue->ops->op(args);				\
	(vb)->cnt_ ## op++;						\
})

#else

#define call_memop(vb, op, args...)					\
	((vb)->vb2_queue->mem_ops->op ?					\
		(vb)->vb2_queue->mem_ops->op(args) : 0)

#define call_ptr_memop(vb, op, args...)					\
	((vb)->vb2_queue->mem_ops->op ?					\
		(vb)->vb2_queue->mem_ops->op(args) : NULL)

#define call_void_memop(vb, op, args...)				\
	do {								\
		if ((vb)->vb2_queue->mem_ops->op)			\
			(vb)->vb2_queue->mem_ops->op(args);		\
	} while (0)

#define call_qop(q, op, args...)					\
	((q)->ops->op ? (q)->ops->op(args) : 0)

#define call_void_qop(q, op, args...)					\
	do {								\
		if ((q)->ops->op)					\
			(q)->ops->op(args);				\
	} while (0)

#define call_vb_qop(vb, op, args...)					\
	((vb)->vb2_queue->ops->op ? (vb)->vb2_queue->ops->op(args) : 0)

#define call_void_vb_qop(vb, op, args...)				\
	do {								\
		if ((vb)->vb2_queue->ops->op)				\
			(vb)->vb2_queue->ops->op(args);			\
	} while (0)

#endif

#define call_bufop(q, op, args...)					\
({									\
	int ret = 0;							\
	if (q && q->buf_ops && q->buf_ops->op)				\
		ret = q->buf_ops->op(args);				\
	ret;								\
})

bool vb2_buffer_in_use(struct vb2_queue *q, struct vb2_buffer *vb);
int vb2_verify_memory_type(struct vb2_queue *q,
		enum vb2_memory memory, unsigned int type);
#endif /* _MEDIA_VIDEOBUF2_INTERNAL_H */
