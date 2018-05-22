/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _VHOST_H
#define _VHOST_H

#include <linux/eventfd.h>
#include <linux/vhost.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>
#include <linux/atomic.h>

struct vhost_work;
typedef void (*vhost_work_fn_t)(struct vhost_work *work);

#define VHOST_WORK_QUEUED 1
struct vhost_work {
	struct llist_node	  node;
	vhost_work_fn_t		  fn;
	unsigned long		  flags;
};

/* Poll a file (eventfd or socket) */
/* Note: there's nothing vhost specific about this structure. */
struct vhost_poll {
	poll_table                table;
	wait_queue_head_t        *wqh;
	wait_queue_entry_t              wait;
	struct vhost_work	  work;
	__poll_t		  mask;
	struct vhost_dev	 *dev;
};

void vhost_work_init(struct vhost_work *work, vhost_work_fn_t fn);
void vhost_work_queue(struct vhost_dev *dev, struct vhost_work *work);
bool vhost_has_work(struct vhost_dev *dev);

void vhost_poll_init(struct vhost_poll *poll, vhost_work_fn_t fn,
		     __poll_t mask, struct vhost_dev *dev);
int vhost_poll_start(struct vhost_poll *poll, struct file *file);
void vhost_poll_stop(struct vhost_poll *poll);
void vhost_poll_flush(struct vhost_poll *poll);
void vhost_poll_queue(struct vhost_poll *poll);
void vhost_work_flush(struct vhost_dev *dev, struct vhost_work *work);
long vhost_vring_ioctl(struct vhost_dev *d, int ioctl, void __user *argp);

struct vhost_log {
	u64 addr;
	u64 len;
};

#define START(node) ((node)->start)
#define LAST(node) ((node)->last)

struct vhost_umem_node {
	struct rb_node rb;
	struct list_head link;
	__u64 start;
	__u64 last;
	__u64 size;
	__u64 userspace_addr;
	__u32 perm;
	__u32 flags_padding;
	__u64 __subtree_last;
};

struct vhost_umem {
	struct rb_root_cached umem_tree;
	struct list_head umem_list;
	int numem;
};

enum vhost_uaddr_type {
	VHOST_ADDR_DESC = 0,
	VHOST_ADDR_AVAIL = 1,
	VHOST_ADDR_USED = 2,
	VHOST_NUM_ADDRS = 3,
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
	const struct vhost_umem_node *meta_iotlb[VHOST_NUM_ADDRS];
	struct file *kick;
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
	struct iovec iotlb_iov[64];
	struct iovec *indirect;
	struct vring_used_elem *heads;
	/* Protected by virtqueue mutex. */
	struct vhost_umem *umem;
	struct vhost_umem *iotlb;
	void *private_data;
	u64 acked_features;
	/* Log write descriptors */
	void __user *log_base;
	struct vhost_log *log;

	/* Ring endianness. Defaults to legacy native endianness.
	 * Set to true when starting a modern virtio device. */
	bool is_le;
#ifdef CONFIG_VHOST_CROSS_ENDIAN_LEGACY
	/* Ring endianness requested by userspace for cross-endian support. */
	bool user_be;
#endif
	u32 busyloop_timeout;
};

struct vhost_msg_node {
  struct vhost_msg msg;
  struct vhost_virtqueue *vq;
  struct list_head node;
};

struct vhost_dev {
	struct mm_struct *mm;
	struct mutex mutex;
	struct vhost_virtqueue **vqs;
	int nvqs;
	struct eventfd_ctx *log_ctx;
	struct llist_head work_list;
	struct task_struct *worker;
	struct vhost_umem *umem;
	struct vhost_umem *iotlb;
	spinlock_t iotlb_lock;
	struct list_head read_list;
	struct list_head pending_list;
	wait_queue_head_t wait;
};

void vhost_dev_init(struct vhost_dev *, struct vhost_virtqueue **vqs, int nvqs);
long vhost_dev_set_owner(struct vhost_dev *dev);
bool vhost_dev_has_owner(struct vhost_dev *dev);
long vhost_dev_check_owner(struct vhost_dev *);
struct vhost_umem *vhost_dev_reset_owner_prepare(void);
void vhost_dev_reset_owner(struct vhost_dev *, struct vhost_umem *);
void vhost_dev_cleanup(struct vhost_dev *);
void vhost_dev_stop(struct vhost_dev *);
long vhost_dev_ioctl(struct vhost_dev *, unsigned int ioctl, void __user *argp);
long vhost_vring_ioctl(struct vhost_dev *d, int ioctl, void __user *argp);
int vhost_vq_access_ok(struct vhost_virtqueue *vq);
int vhost_log_access_ok(struct vhost_dev *);

int vhost_get_vq_desc(struct vhost_virtqueue *,
		      struct iovec iov[], unsigned int iov_count,
		      unsigned int *out_num, unsigned int *in_num,
		      struct vhost_log *log, unsigned int *log_num);
void vhost_discard_vq_desc(struct vhost_virtqueue *, int n);

int vhost_vq_init_access(struct vhost_virtqueue *);
int vhost_add_used(struct vhost_virtqueue *, unsigned int head, int len);
int vhost_add_used_n(struct vhost_virtqueue *, struct vring_used_elem *heads,
		     unsigned count);
void vhost_add_used_and_signal(struct vhost_dev *, struct vhost_virtqueue *,
			       unsigned int id, int len);
void vhost_add_used_and_signal_n(struct vhost_dev *, struct vhost_virtqueue *,
			       struct vring_used_elem *heads, unsigned count);
void vhost_signal(struct vhost_dev *, struct vhost_virtqueue *);
void vhost_disable_notify(struct vhost_dev *, struct vhost_virtqueue *);
bool vhost_vq_avail_empty(struct vhost_dev *, struct vhost_virtqueue *);
bool vhost_enable_notify(struct vhost_dev *, struct vhost_virtqueue *);

int vhost_log_write(struct vhost_virtqueue *vq, struct vhost_log *log,
		    unsigned int log_num, u64 len);
int vq_iotlb_prefetch(struct vhost_virtqueue *vq);

struct vhost_msg_node *vhost_new_msg(struct vhost_virtqueue *vq, int type);
void vhost_enqueue_msg(struct vhost_dev *dev,
		       struct list_head *head,
		       struct vhost_msg_node *node);
struct vhost_msg_node *vhost_dequeue_msg(struct vhost_dev *dev,
					 struct list_head *head);
__poll_t vhost_chr_poll(struct file *file, struct vhost_dev *dev,
			    poll_table *wait);
ssize_t vhost_chr_read_iter(struct vhost_dev *dev, struct iov_iter *to,
			    int noblock);
ssize_t vhost_chr_write_iter(struct vhost_dev *dev,
			     struct iov_iter *from);
int vhost_init_device_iotlb(struct vhost_dev *d, bool enabled);

#define vq_err(vq, fmt, ...) do {                                  \
		pr_debug(pr_fmt(fmt), ##__VA_ARGS__);       \
		if ((vq)->error_ctx)                               \
				eventfd_signal((vq)->error_ctx, 1);\
	} while (0)

enum {
	VHOST_FEATURES = (1ULL << VIRTIO_F_NOTIFY_ON_EMPTY) |
			 (1ULL << VIRTIO_RING_F_INDIRECT_DESC) |
			 (1ULL << VIRTIO_RING_F_EVENT_IDX) |
			 (1ULL << VHOST_F_LOG_ALL) |
			 (1ULL << VIRTIO_F_ANY_LAYOUT) |
			 (1ULL << VIRTIO_F_VERSION_1)
};

static inline bool vhost_has_feature(struct vhost_virtqueue *vq, int bit)
{
	return vq->acked_features & (1ULL << bit);
}

#ifdef CONFIG_VHOST_CROSS_ENDIAN_LEGACY
static inline bool vhost_is_little_endian(struct vhost_virtqueue *vq)
{
	return vq->is_le;
}
#else
static inline bool vhost_is_little_endian(struct vhost_virtqueue *vq)
{
	return virtio_legacy_is_little_endian() || vq->is_le;
}
#endif

/* Memory accessors */
static inline u16 vhost16_to_cpu(struct vhost_virtqueue *vq, __virtio16 val)
{
	return __virtio16_to_cpu(vhost_is_little_endian(vq), val);
}

static inline __virtio16 cpu_to_vhost16(struct vhost_virtqueue *vq, u16 val)
{
	return __cpu_to_virtio16(vhost_is_little_endian(vq), val);
}

static inline u32 vhost32_to_cpu(struct vhost_virtqueue *vq, __virtio32 val)
{
	return __virtio32_to_cpu(vhost_is_little_endian(vq), val);
}

static inline __virtio32 cpu_to_vhost32(struct vhost_virtqueue *vq, u32 val)
{
	return __cpu_to_virtio32(vhost_is_little_endian(vq), val);
}

static inline u64 vhost64_to_cpu(struct vhost_virtqueue *vq, __virtio64 val)
{
	return __virtio64_to_cpu(vhost_is_little_endian(vq), val);
}

static inline __virtio64 cpu_to_vhost64(struct vhost_virtqueue *vq, u64 val)
{
	return __cpu_to_virtio64(vhost_is_little_endian(vq), val);
}
#endif
