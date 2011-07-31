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
#include <asm/atomic.h>

struct vhost_device;

enum {
	/* Enough place for all fragments, head, and virtio net header. */
	VHOST_NET_MAX_SG = MAX_SKB_FRAGS + 2,
};

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

	/* Log writes to used structure. */
	bool log_used;
	u64 log_addr;

	struct iovec indirect[VHOST_NET_MAX_SG];
	struct iovec iov[VHOST_NET_MAX_SG];
	struct iovec hdr[VHOST_NET_MAX_SG];
	size_t vhost_hlen;
	size_t sock_hlen;
	struct vring_used_elem heads[VHOST_NET_MAX_SG];
	/* We use a kind of RCU to access private pointer.
	 * All readers access it from worker, which makes it possible to
	 * flush the vhost_work instead of synchronize_rcu. Therefore readers do
	 * not need to call rcu_read_lock/rcu_read_unlock: the beginning of
	 * vhost_work execution acts instead of rcu_read_lock() and the end of
	 * vhost_work execution acts instead of rcu_read_lock().
	 * Writers use virtqueue mutex. */
	void *private_data;
	/* Log write descriptors */
	void __user *log_base;
	struct vhost_log log[VHOST_NET_MAX_SG];
};

struct vhost_dev {
	/* Readers use RCU to access memory table pointer
	 * log base pointer and features.
	 * Writers use mutex below.*/
	struct vhost_memory *memory;
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
void vhost_dev_cleanup(struct vhost_dev *);
long vhost_dev_ioctl(struct vhost_dev *, unsigned int ioctl, unsigned long arg);
int vhost_vq_access_ok(struct vhost_virtqueue *vq);
int vhost_log_access_ok(struct vhost_dev *);

int vhost_get_vq_desc(struct vhost_dev *, struct vhost_virtqueue *,
		      struct iovec iov[], unsigned int iov_count,
		      unsigned int *out_num, unsigned int *in_num,
		      struct vhost_log *log, unsigned int *log_num);
void vhost_discard_vq_desc(struct vhost_virtqueue *, int n);

int vhost_add_used(struct vhost_virtqueue *, unsigned int head, int len);
int vhost_add_used_n(struct vhost_virtqueue *, struct vring_used_elem *heads,
		     unsigned count);
void vhost_add_used_and_signal(struct vhost_dev *, struct vhost_virtqueue *,
			       unsigned int id, int len);
void vhost_add_used_and_signal_n(struct vhost_dev *, struct vhost_virtqueue *,
			       struct vring_used_elem *heads, unsigned count);
void vhost_signal(struct vhost_dev *, struct vhost_virtqueue *);
void vhost_disable_notify(struct vhost_virtqueue *);
bool vhost_enable_notify(struct vhost_virtqueue *);

int vhost_log_write(struct vhost_virtqueue *vq, struct vhost_log *log,
		    unsigned int log_num, u64 len);

#define vq_err(vq, fmt, ...) do {                                  \
		pr_debug(pr_fmt(fmt), ##__VA_ARGS__);       \
		if ((vq)->error_ctx)                               \
				eventfd_signal((vq)->error_ctx, 1);\
	} while (0)

enum {
	VHOST_FEATURES = (1 << VIRTIO_F_NOTIFY_ON_EMPTY) |
			 (1 << VIRTIO_RING_F_INDIRECT_DESC) |
			 (1 << VHOST_F_LOG_ALL) |
			 (1 << VHOST_NET_F_VIRTIO_NET_HDR) |
			 (1 << VIRTIO_NET_F_MRG_RXBUF),
};

static inline int vhost_has_feature(struct vhost_dev *dev, int bit)
{
	unsigned acked_features = rcu_dereference(dev->acked_features);
	return acked_features & (1 << bit);
}

#endif
