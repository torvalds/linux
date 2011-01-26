#ifndef _UVC_QUEUE_H_
#define _UVC_QUEUE_H_

#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/videodev2.h>

/* Maximum frame size in bytes, for sanity checking. */
#define UVC_MAX_FRAME_SIZE	(16*1024*1024)
/* Maximum number of video buffers. */
#define UVC_MAX_VIDEO_BUFFERS	32

/* ------------------------------------------------------------------------
 * Structures.
 */

enum uvc_buffer_state {
	UVC_BUF_STATE_IDLE	= 0,
	UVC_BUF_STATE_QUEUED	= 1,
	UVC_BUF_STATE_ACTIVE	= 2,
	UVC_BUF_STATE_DONE	= 3,
	UVC_BUF_STATE_ERROR	= 4,
};

struct uvc_buffer {
	unsigned long vma_use_count;
	struct list_head stream;

	/* Touched by interrupt handler. */
	struct v4l2_buffer buf;
	struct list_head queue;
	wait_queue_head_t wait;
	enum uvc_buffer_state state;
};

#define UVC_QUEUE_STREAMING		(1 << 0)
#define UVC_QUEUE_DISCONNECTED		(1 << 1)
#define UVC_QUEUE_DROP_INCOMPLETE	(1 << 2)
#define UVC_QUEUE_PAUSED		(1 << 3)

struct uvc_video_queue {
	enum v4l2_buf_type type;

	void *mem;
	unsigned int flags;
	__u32 sequence;

	unsigned int count;
	unsigned int buf_size;
	unsigned int buf_used;
	struct uvc_buffer buffer[UVC_MAX_VIDEO_BUFFERS];
	struct mutex mutex;	/* protects buffers and mainqueue */
	spinlock_t irqlock;	/* protects irqqueue */

	struct list_head mainqueue;
	struct list_head irqqueue;
};

static inline int uvc_queue_streaming(struct uvc_video_queue *queue)
{
	return queue->flags & UVC_QUEUE_STREAMING;
}

#endif /* __KERNEL__ */

#endif /* _UVC_QUEUE_H_ */

