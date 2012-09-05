#ifndef _VHOST_H
#define _VHOST_H

#include <linux/eventfd.h>
#include <linux/vhost.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/file.h>
#include <linux/skbuff.h>
#include <linux/uio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>
#include <linux/atomic.h>

/* This is for zerocopy, used buffer len is set to 1 when lower device DMA
 * done */
#define VHOST_DMA_DONE_LEN	1
#define VHOST_DMA_CLEAR_LEN	0

struct vhost_device;

struct vhost_work;
typedef void (*vhost_work_fn_t)(struct vhost_work *work);

struct vhost_work {
	struct list_head	  node;
	vhost_work_fn_t		  fn;
	wait_queue_head_t	  done;
	int			  flushing;
	unsigned		  queue_seq;
	unsigned		  done_seq;
};

/* Poll a file (eventfd or socket) */
/* Note: there's nothing vhost specific about this structure. */
struct vhost_poll {
	poll_table                table;
	wait_queue_head_t        *wqh;
	wait_queue_t              wait;
	struct vhost_work	  work;
	unsigned long		  mask;
	struct vhost_dev	 *dev;
};

void vhost_work_init(struct vhost_work *work, vhost_work_fn_t fn);
void vhost_work_queue(struct vhost_dev *dev, struct vhost_work *work);

void vhost_poll_init(struct vhost_poll *poll, vhost_work_fn_t fn,
		     unsigned long mask, struct vhost_dev *dev);
void vhost_poll_start(struct vhost_poll *poll, struct file *file);
void vhost_poll_stop(struct vhost_poll *poll);
void vhost_poll_flush(struct vhost_poll *poll);
void vhost_poll_queue(struct vhost_poll *poll);

struct vhost_log {
	u64 addr;
	u64 len;
};

struct vhost_virtqueue;

struct vhost_ubuf_ref {
	struct kref kref;
	wait_queue_head_t wait;
	struct vhost_virtqueue *vq;
};

struct vhost_ubuf_ref *vhost_ubuf_alloc(struct vhost_virtqueue *, bool zcopy);
void vhost_ubuf_put(struct vhost_ubuf_ref *);
void vhost_ubuf_put_and_wait(struct vhost_ubuf_ref *);

/* The virtqueue structure describes a queue attached to a device. */
struct vhost_virtqueue {
	struct vhost_dev *dev;

	/* The actual ring of buffers. */
	struct mutex mutex;
	unsigned int num;
	struct vring_desc __user *desc;
	struct vring_avail __user *avail;
	struct vring_used __user *used;
	struct file *kick;
	struct file *call;
	struct file *error;
	struct eventfd_ctx *call_ctx;
	struct eventfd_ctx *error_ctx;
	struct eventfd_ctx *log_ctx;

	struct vhost_poll poll;

	/* The routine to call when the Guest pings us, or timeout. */
	vhost_work_fn_t handle_kick;

	/* Last available index we saw. */
	u16 last_avail_idx;

	/* Caches available index value from user. */
	u16 avail_idx;

	/* Last index we used. */
	u16 last_used_idx;

	/* Used flags */
	u16 used_flags;

	/* Last used index value we have signalled on */
	u16 signalled_used;

	/* Last used index value we have signalled on */
	bool signalled_used_valid;

	/* Log writes to used structure. */
	bool log_used;
	u64 log_addr;

	struct iovec iov[UIO_MAXIOV];
	/* hdr is used to store the virtio header.
	 * Since each iovec has >= 1 byte length, we never need more than
	 * header length entries to store the header. */
	struct iovec hdr[sizeof(struct virtio_net_hdr_mrg_rxbuf)];
	struct iovec *indirect;
	size_t vhost_hlen;
	size_t sock_hlen;
	struct vring_used_elem *heads;
	/* We use a kind of RCU to access private pointer.
	 * All readers access it from worker, which makes it possible to
	 * flush the vhost_work instead of synchronize_rcu. Therefore readers do
	 * not need to call rcu_read_lock/rcu_read_unlock: the beginning of
	 * vhost_work execution acts instead of rcu_read_lock() and the end of
	 * vhost_work execution acts instead of rcu_read_unlock().
	 * Writers use virtqueue mutex. */
	void __rcu *private_data;
	/* Log write descriptors */
	void __user *log_base;
	struct vhost_log *log;
	/* vhost zerocopy support fields below: */
	/* last used idx for outstanding DMA zerocopy buffers */
	int upend_idx;
	/* first used idx for DMA done zerocopy buffers */
	int done_idx;
	/* an array of userspace buffers info */
	struct ubuf_info *ubuf_info;
	/* Reference counting for outstanding ubufs.
	 * Protected by vq mutex. Writers must also take device mutex. */
	struct vhost_ubuf_ref *ubufs;
};

struct vhost_dev {
	/* Readers use RCU to access memory table pointer
	 * log base pointer and features.
	 * Writers use mutex below.*/
	struct vhost_memory __rcu *memory;
	struct mm_struct *mm;
	struct mutex mutex;
	unsigned acked_features;
	struct vhost_virtqueue *vqs;
	int nvqs;
	struct file *log_file;
	struct eventfd_ctx *log_ctx;
	spinlock_t work_lock;
	struct list_head work_list;
	struct task_struct *worker;
};

long vhost_dev_init(struct vhost_dev *, struct vhost_virtqueue *vqs, int nvqs);
long vhost_dev_check_owner(struct vhost_dev *);
long vhost_dev_reset_owner(struct vhost_dev *);
void vhost_dev_cleanup(struct vhost_dev *, bool locked);
long vhost_dev_ioctl(struct vhost_dev *, unsigned int ioctl, unsigned long arg);
int vhost_vq_access_ok(struct vhost_virtqueue *vq);
int vhost_log_access_ok(struct vhost_dev *);

int vhost_get_vq_desc(struct vhost_dev *, struct vhost_virtqueue *,
		      struct iovec iov[], unsigned int iov_count,
		      unsigned int *out_num, unsigned int *in_num,
		      struct vhost_log *log, unsigned int *log_num);
void vhost_discard_vq_desc(struct vhost_virtqueue *, int n);

int vhost_init_used(struct vhost_virtqueue *);
int vhost_add_used(struct vhost_virtqueue *, unsigned int head, int len);
int vhost_add_used_n(struct vhost_virtqueue *, struct vring_used_elem *heads,
		     unsigned count);
void vhost_add_used_and_signal(struct vhost_dev *, struct vhost_virtqueue *,
			       unsigned int id, int len);
void vhost_add_used_and_signal_n(struct vhost_dev *, struct vhost_virtqueue *,
			       struct vring_used_elem *heads, unsigned count);
void vhost_signal(struct vhost_dev *, struct vhost_virtqueue *);
void vhost_disable_notify(struct vhost_dev *, struct vhost_virtqueue *);
bool vhost_enable_notify(struct vhost_dev *, struct vhost_virtqueue *);

int vhost_log_write(struct vhost_virtqueue *vq, struct vhost_log *log,
		    unsigned int log_num, u64 len);
void vhost_zerocopy_callback(struct ubuf_info *);
int vhost_zerocopy_signal_used(struct vhost_virtqueue *vq);

#define vq_err(vq, fmt, ...) do {                                  \
		pr_debug(pr_fmt(fmt), ##__VA_ARGS__);       \
		if ((vq)->error_ctx)                               \
				eventfd_signal((vq)->error_ctx, 1);\
	} while (0)

enum {
	VHOST_FEATURES = (1ULL << VIRTIO_F_NOTIFY_ON_EMPTY) |
			 (1ULL << VIRTIO_RING_F_INDIRECT_DESC) |
			 (1ULL << VIRTIO_RING_F_EVENT_IDX) |
			 (1ULL << VHOST_F_LOG_ALL),
	VHOST_NET_FEATURES = VHOST_FEATURES |
			 (1ULL << VHOST_NET_F_VIRTIO_NET_HDR) |
			 (1ULL << VIRTIO_NET_F_MRG_RXBUF),
};

static inline int vhost_has_feature(struct vhost_dev *dev, int bit)
{
	unsigned acked_features;

	/* TODO: check that we are running from vhost_worker or dev mutex is
	 * held? */
	acked_features = rcu_dereference_index_check(dev->acked_features, 1);
	return acked_features & (1 << bit);
}

void vhost_enable_zcopy(int vq);

#endif
