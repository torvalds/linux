// SPDX-License-Identifier: GPL-2.0-only
/*
 * Helpers for the host side of a virtio ring.
 *
 * Since these may be in userspace, we use (inline) accessors.
 */
#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/vringh.h>
#include <linux/virtio_ring.h>
#include <linux/kernel.h>
#include <linux/ratelimit.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/export.h>
#if IS_REACHABLE(CONFIG_VHOST_IOTLB)
#include <linux/bvec.h>
#include <linux/highmem.h>
#include <linux/vhost_iotlb.h>
#endif
#include <uapi/linux/virtio_config.h>

static __printf(1,2) __cold void vringh_bad(const char *fmt, ...)
{
	static DEFINE_RATELIMIT_STATE(vringh_rs,
				      DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);
	if (__ratelimit(&vringh_rs)) {
		va_list ap;
		va_start(ap, fmt);
		printk(KERN_NOTICE "vringh:");
		vprintk(fmt, ap);
		va_end(ap);
	}
}

/* Returns vring->num if empty, -ve on error. */
static inline int __vringh_get_head(const struct vringh *vrh,
				    int (*getu16)(const struct vringh *vrh,
						  u16 *val, const __virtio16 *p),
				    u16 *last_avail_idx)
{
	u16 avail_idx, i, head;
	int err;

	err = getu16(vrh, &avail_idx, &vrh->vring.avail->idx);
	if (err) {
		vringh_bad("Failed to access avail idx at %p",
			   &vrh->vring.avail->idx);
		return err;
	}

	if (*last_avail_idx == avail_idx)
		return vrh->vring.num;

	/* Only get avail ring entries after they have been exposed by guest. */
	virtio_rmb(vrh->weak_barriers);

	i = *last_avail_idx & (vrh->vring.num - 1);

	err = getu16(vrh, &head, &vrh->vring.avail->ring[i]);
	if (err) {
		vringh_bad("Failed to read head: idx %d address %p",
			   *last_avail_idx, &vrh->vring.avail->ring[i]);
		return err;
	}

	if (head >= vrh->vring.num) {
		vringh_bad("Guest says index %u > %u is available",
			   head, vrh->vring.num);
		return -EINVAL;
	}

	(*last_avail_idx)++;
	return head;
}

/* Copy some bytes to/from the iovec.  Returns num copied. */
static inline ssize_t vringh_iov_xfer(struct vringh *vrh,
				      struct vringh_kiov *iov,
				      void *ptr, size_t len,
				      int (*xfer)(const struct vringh *vrh,
						  void *addr, void *ptr,
						  size_t len))
{
	int err, done = 0;

	while (len && iov->i < iov->used) {
		size_t partlen;

		partlen = min(iov->iov[iov->i].iov_len, len);
		err = xfer(vrh, iov->iov[iov->i].iov_base, ptr, partlen);
		if (err)
			return err;
		done += partlen;
		len -= partlen;
		ptr += partlen;
		iov->consumed += partlen;
		iov->iov[iov->i].iov_len -= partlen;
		iov->iov[iov->i].iov_base += partlen;

		if (!iov->iov[iov->i].iov_len) {
			/* Fix up old iov element then increment. */
			iov->iov[iov->i].iov_len = iov->consumed;
			iov->iov[iov->i].iov_base -= iov->consumed;

			
			iov->consumed = 0;
			iov->i++;
		}
	}
	return done;
}

/* May reduce *len if range is shorter. */
static inline bool range_check(struct vringh *vrh, u64 addr, size_t *len,
			       struct vringh_range *range,
			       bool (*getrange)(struct vringh *,
						u64, struct vringh_range *))
{
	if (addr < range->start || addr > range->end_incl) {
		if (!getrange(vrh, addr, range))
			return false;
	}
	BUG_ON(addr < range->start || addr > range->end_incl);

	/* To end of memory? */
	if (unlikely(addr + *len == 0)) {
		if (range->end_incl == -1ULL)
			return true;
		goto truncate;
	}

	/* Otherwise, don't wrap. */
	if (addr + *len < addr) {
		vringh_bad("Wrapping descriptor %zu@0x%llx",
			   *len, (unsigned long long)addr);
		return false;
	}

	if (unlikely(addr + *len - 1 > range->end_incl))
		goto truncate;
	return true;

truncate:
	*len = range->end_incl + 1 - addr;
	return true;
}

static inline bool no_range_check(struct vringh *vrh, u64 addr, size_t *len,
				  struct vringh_range *range,
				  bool (*getrange)(struct vringh *,
						   u64, struct vringh_range *))
{
	return true;
}

/* No reason for this code to be inline. */
static int move_to_indirect(const struct vringh *vrh,
			    int *up_next, u16 *i, void *addr,
			    const struct vring_desc *desc,
			    struct vring_desc **descs, int *desc_max)
{
	u32 len;

	/* Indirect tables can't have indirect. */
	if (*up_next != -1) {
		vringh_bad("Multilevel indirect %u->%u", *up_next, *i);
		return -EINVAL;
	}

	len = vringh32_to_cpu(vrh, desc->len);
	if (unlikely(len % sizeof(struct vring_desc))) {
		vringh_bad("Strange indirect len %u", desc->len);
		return -EINVAL;
	}

	/* We will check this when we follow it! */
	if (desc->flags & cpu_to_vringh16(vrh, VRING_DESC_F_NEXT))
		*up_next = vringh16_to_cpu(vrh, desc->next);
	else
		*up_next = -2;
	*descs = addr;
	*desc_max = len / sizeof(struct vring_desc);

	/* Now, start at the first indirect. */
	*i = 0;
	return 0;
}

static int resize_iovec(struct vringh_kiov *iov, gfp_t gfp)
{
	struct kvec *new;
	unsigned int flag, new_num = (iov->max_num & ~VRINGH_IOV_ALLOCATED) * 2;

	if (new_num < 8)
		new_num = 8;

	flag = (iov->max_num & VRINGH_IOV_ALLOCATED);
	if (flag)
		new = krealloc(iov->iov, new_num * sizeof(struct iovec), gfp);
	else {
		new = kmalloc_array(new_num, sizeof(struct iovec), gfp);
		if (new) {
			memcpy(new, iov->iov,
			       iov->max_num * sizeof(struct iovec));
			flag = VRINGH_IOV_ALLOCATED;
		}
	}
	if (!new)
		return -ENOMEM;
	iov->iov = new;
	iov->max_num = (new_num | flag);
	return 0;
}

static u16 __cold return_from_indirect(const struct vringh *vrh, int *up_next,
				       struct vring_desc **descs, int *desc_max)
{
	u16 i = *up_next;

	*up_next = -1;
	*descs = vrh->vring.desc;
	*desc_max = vrh->vring.num;
	return i;
}

static int slow_copy(struct vringh *vrh, void *dst, const void *src,
		     bool (*rcheck)(struct vringh *vrh, u64 addr, size_t *len,
				    struct vringh_range *range,
				    bool (*getrange)(struct vringh *vrh,
						     u64,
						     struct vringh_range *)),
		     bool (*getrange)(struct vringh *vrh,
				      u64 addr,
				      struct vringh_range *r),
		     struct vringh_range *range,
		     int (*copy)(const struct vringh *vrh,
				 void *dst, const void *src, size_t len))
{
	size_t part, len = sizeof(struct vring_desc);

	do {
		u64 addr;
		int err;

		part = len;
		addr = (u64)(unsigned long)src - range->offset;

		if (!rcheck(vrh, addr, &part, range, getrange))
			return -EINVAL;

		err = copy(vrh, dst, src, part);
		if (err)
			return err;

		dst += part;
		src += part;
		len -= part;
	} while (len);
	return 0;
}

static inline int
__vringh_iov(struct vringh *vrh, u16 i,
	     struct vringh_kiov *riov,
	     struct vringh_kiov *wiov,
	     bool (*rcheck)(struct vringh *vrh, u64 addr, size_t *len,
			    struct vringh_range *range,
			    bool (*getrange)(struct vringh *, u64,
					     struct vringh_range *)),
	     bool (*getrange)(struct vringh *, u64, struct vringh_range *),
	     gfp_t gfp,
	     int (*copy)(const struct vringh *vrh,
			 void *dst, const void *src, size_t len))
{
	int err, count = 0, up_next, desc_max;
	struct vring_desc desc, *descs;
	struct vringh_range range = { -1ULL, 0 }, slowrange;
	bool slow = false;

	/* We start traversing vring's descriptor table. */
	descs = vrh->vring.desc;
	desc_max = vrh->vring.num;
	up_next = -1;

	if (riov)
		riov->i = riov->used = 0;
	else if (wiov)
		wiov->i = wiov->used = 0;
	else
		/* You must want something! */
		BUG();

	for (;;) {
		void *addr;
		struct vringh_kiov *iov;
		size_t len;

		if (unlikely(slow))
			err = slow_copy(vrh, &desc, &descs[i], rcheck, getrange,
					&slowrange, copy);
		else
			err = copy(vrh, &desc, &descs[i], sizeof(desc));
		if (unlikely(err))
			goto fail;

		if (unlikely(desc.flags &
			     cpu_to_vringh16(vrh, VRING_DESC_F_INDIRECT))) {
			u64 a = vringh64_to_cpu(vrh, desc.addr);

			/* Make sure it's OK, and get offset. */
			len = vringh32_to_cpu(vrh, desc.len);
			if (!rcheck(vrh, a, &len, &range, getrange)) {
				err = -EINVAL;
				goto fail;
			}

			if (unlikely(len != vringh32_to_cpu(vrh, desc.len))) {
				slow = true;
				/* We need to save this range to use offset */
				slowrange = range;
			}

			addr = (void *)(long)(a + range.offset);
			err = move_to_indirect(vrh, &up_next, &i, addr, &desc,
					       &descs, &desc_max);
			if (err)
				goto fail;
			continue;
		}

		if (count++ == vrh->vring.num) {
			vringh_bad("Descriptor loop in %p", descs);
			err = -ELOOP;
			goto fail;
		}

		if (desc.flags & cpu_to_vringh16(vrh, VRING_DESC_F_WRITE))
			iov = wiov;
		else {
			iov = riov;
			if (unlikely(wiov && wiov->i)) {
				vringh_bad("Readable desc %p after writable",
					   &descs[i]);
				err = -EINVAL;
				goto fail;
			}
		}

		if (!iov) {
			vringh_bad("Unexpected %s desc",
				   !wiov ? "writable" : "readable");
			err = -EPROTO;
			goto fail;
		}

	again:
		/* Make sure it's OK, and get offset. */
		len = vringh32_to_cpu(vrh, desc.len);
		if (!rcheck(vrh, vringh64_to_cpu(vrh, desc.addr), &len, &range,
			    getrange)) {
			err = -EINVAL;
			goto fail;
		}
		addr = (void *)(unsigned long)(vringh64_to_cpu(vrh, desc.addr) +
					       range.offset);

		if (unlikely(iov->used == (iov->max_num & ~VRINGH_IOV_ALLOCATED))) {
			err = resize_iovec(iov, gfp);
			if (err)
				goto fail;
		}

		iov->iov[iov->used].iov_base = addr;
		iov->iov[iov->used].iov_len = len;
		iov->used++;

		if (unlikely(len != vringh32_to_cpu(vrh, desc.len))) {
			desc.len = cpu_to_vringh32(vrh,
				   vringh32_to_cpu(vrh, desc.len) - len);
			desc.addr = cpu_to_vringh64(vrh,
				    vringh64_to_cpu(vrh, desc.addr) + len);
			goto again;
		}

		if (desc.flags & cpu_to_vringh16(vrh, VRING_DESC_F_NEXT)) {
			i = vringh16_to_cpu(vrh, desc.next);
		} else {
			/* Just in case we need to finish traversing above. */
			if (unlikely(up_next > 0)) {
				i = return_from_indirect(vrh, &up_next,
							 &descs, &desc_max);
				slow = false;
			} else
				break;
		}

		if (i >= desc_max) {
			vringh_bad("Chained index %u > %u", i, desc_max);
			err = -EINVAL;
			goto fail;
		}
	}

	return 0;

fail:
	return err;
}

static inline int __vringh_complete(struct vringh *vrh,
				    const struct vring_used_elem *used,
				    unsigned int num_used,
				    int (*putu16)(const struct vringh *vrh,
						  __virtio16 *p, u16 val),
				    int (*putused)(const struct vringh *vrh,
						   struct vring_used_elem *dst,
						   const struct vring_used_elem
						   *src, unsigned num))
{
	struct vring_used *used_ring;
	int err;
	u16 used_idx, off;

	used_ring = vrh->vring.used;
	used_idx = vrh->last_used_idx + vrh->completed;

	off = used_idx % vrh->vring.num;

	/* Compiler knows num_used == 1 sometimes, hence extra check */
	if (num_used > 1 && unlikely(off + num_used >= vrh->vring.num)) {
		u16 part = vrh->vring.num - off;
		err = putused(vrh, &used_ring->ring[off], used, part);
		if (!err)
			err = putused(vrh, &used_ring->ring[0], used + part,
				      num_used - part);
	} else
		err = putused(vrh, &used_ring->ring[off], used, num_used);

	if (err) {
		vringh_bad("Failed to write %u used entries %u at %p",
			   num_used, off, &used_ring->ring[off]);
		return err;
	}

	/* Make sure buffer is written before we update index. */
	virtio_wmb(vrh->weak_barriers);

	err = putu16(vrh, &vrh->vring.used->idx, used_idx + num_used);
	if (err) {
		vringh_bad("Failed to update used index at %p",
			   &vrh->vring.used->idx);
		return err;
	}

	vrh->completed += num_used;
	return 0;
}


static inline int __vringh_need_notify(struct vringh *vrh,
				       int (*getu16)(const struct vringh *vrh,
						     u16 *val,
						     const __virtio16 *p))
{
	bool notify;
	u16 used_event;
	int err;

	/* Flush out used index update. This is paired with the
	 * barrier that the Guest executes when enabling
	 * interrupts. */
	virtio_mb(vrh->weak_barriers);

	/* Old-style, without event indices. */
	if (!vrh->event_indices) {
		u16 flags;
		err = getu16(vrh, &flags, &vrh->vring.avail->flags);
		if (err) {
			vringh_bad("Failed to get flags at %p",
				   &vrh->vring.avail->flags);
			return err;
		}
		return (!(flags & VRING_AVAIL_F_NO_INTERRUPT));
	}

	/* Modern: we know when other side wants to know. */
	err = getu16(vrh, &used_event, &vring_used_event(&vrh->vring));
	if (err) {
		vringh_bad("Failed to get used event idx at %p",
			   &vring_used_event(&vrh->vring));
		return err;
	}

	/* Just in case we added so many that we wrap. */
	if (unlikely(vrh->completed > 0xffff))
		notify = true;
	else
		notify = vring_need_event(used_event,
					  vrh->last_used_idx + vrh->completed,
					  vrh->last_used_idx);

	vrh->last_used_idx += vrh->completed;
	vrh->completed = 0;
	return notify;
}

static inline bool __vringh_notify_enable(struct vringh *vrh,
					  int (*getu16)(const struct vringh *vrh,
							u16 *val, const __virtio16 *p),
					  int (*putu16)(const struct vringh *vrh,
							__virtio16 *p, u16 val))
{
	u16 avail;

	if (!vrh->event_indices) {
		/* Old-school; update flags. */
		if (putu16(vrh, &vrh->vring.used->flags, 0) != 0) {
			vringh_bad("Clearing used flags %p",
				   &vrh->vring.used->flags);
			return true;
		}
	} else {
		if (putu16(vrh, &vring_avail_event(&vrh->vring),
			   vrh->last_avail_idx) != 0) {
			vringh_bad("Updating avail event index %p",
				   &vring_avail_event(&vrh->vring));
			return true;
		}
	}

	/* They could have slipped one in as we were doing that: make
	 * sure it's written, then check again. */
	virtio_mb(vrh->weak_barriers);

	if (getu16(vrh, &avail, &vrh->vring.avail->idx) != 0) {
		vringh_bad("Failed to check avail idx at %p",
			   &vrh->vring.avail->idx);
		return true;
	}

	/* This is unlikely, so we just leave notifications enabled
	 * (if we're using event_indices, we'll only get one
	 * notification anyway). */
	return avail == vrh->last_avail_idx;
}

static inline void __vringh_notify_disable(struct vringh *vrh,
					   int (*putu16)(const struct vringh *vrh,
							 __virtio16 *p, u16 val))
{
	if (!vrh->event_indices) {
		/* Old-school; update flags. */
		if (putu16(vrh, &vrh->vring.used->flags,
			   VRING_USED_F_NO_NOTIFY)) {
			vringh_bad("Setting used flags %p",
				   &vrh->vring.used->flags);
		}
	}
}

/* Userspace access helpers: in this case, addresses are really userspace. */
static inline int getu16_user(const struct vringh *vrh, u16 *val, const __virtio16 *p)
{
	__virtio16 v = 0;
	int rc = get_user(v, (__force __virtio16 __user *)p);
	*val = vringh16_to_cpu(vrh, v);
	return rc;
}

static inline int putu16_user(const struct vringh *vrh, __virtio16 *p, u16 val)
{
	__virtio16 v = cpu_to_vringh16(vrh, val);
	return put_user(v, (__force __virtio16 __user *)p);
}

static inline int copydesc_user(const struct vringh *vrh,
				void *dst, const void *src, size_t len)
{
	return copy_from_user(dst, (__force void __user *)src, len) ?
		-EFAULT : 0;
}

static inline int putused_user(const struct vringh *vrh,
			       struct vring_used_elem *dst,
			       const struct vring_used_elem *src,
			       unsigned int num)
{
	return copy_to_user((__force void __user *)dst, src,
			    sizeof(*dst) * num) ? -EFAULT : 0;
}

static inline int xfer_from_user(const struct vringh *vrh, void *src,
				 void *dst, size_t len)
{
	return copy_from_user(dst, (__force void __user *)src, len) ?
		-EFAULT : 0;
}

static inline int xfer_to_user(const struct vringh *vrh,
			       void *dst, void *src, size_t len)
{
	return copy_to_user((__force void __user *)dst, src, len) ?
		-EFAULT : 0;
}

/**
 * vringh_init_user - initialize a vringh for a userspace vring.
 * @vrh: the vringh to initialize.
 * @features: the feature bits for this ring.
 * @num: the number of elements.
 * @weak_barriers: true if we only need memory barriers, not I/O.
 * @desc: the userpace descriptor pointer.
 * @avail: the userpace avail pointer.
 * @used: the userpace used pointer.
 *
 * Returns an error if num is invalid: you should check pointers
 * yourself!
 */
int vringh_init_user(struct vringh *vrh, u64 features,
		     unsigned int num, bool weak_barriers,
		     struct vring_desc __user *desc,
		     struct vring_avail __user *avail,
		     struct vring_used __user *used)
{
	/* Sane power of 2 please! */
	if (!num || num > 0xffff || (num & (num - 1))) {
		vringh_bad("Bad ring size %u", num);
		return -EINVAL;
	}

	vrh->little_endian = (features & (1ULL << VIRTIO_F_VERSION_1));
	vrh->event_indices = (features & (1 << VIRTIO_RING_F_EVENT_IDX));
	vrh->weak_barriers = weak_barriers;
	vrh->completed = 0;
	vrh->last_avail_idx = 0;
	vrh->last_used_idx = 0;
	vrh->vring.num = num;
	/* vring expects kernel addresses, but only used via accessors. */
	vrh->vring.desc = (__force struct vring_desc *)desc;
	vrh->vring.avail = (__force struct vring_avail *)avail;
	vrh->vring.used = (__force struct vring_used *)used;
	return 0;
}
EXPORT_SYMBOL(vringh_init_user);

/**
 * vringh_getdesc_user - get next available descriptor from userspace ring.
 * @vrh: the userspace vring.
 * @riov: where to put the readable descriptors (or NULL)
 * @wiov: where to put the writable descriptors (or NULL)
 * @getrange: function to call to check ranges.
 * @head: head index we received, for passing to vringh_complete_user().
 *
 * Returns 0 if there was no descriptor, 1 if there was, or -errno.
 *
 * Note that on error return, you can tell the difference between an
 * invalid ring and a single invalid descriptor: in the former case,
 * *head will be vrh->vring.num.  You may be able to ignore an invalid
 * descriptor, but there's not much you can do with an invalid ring.
 *
 * Note that you may need to clean up riov and wiov, even on error!
 */
int vringh_getdesc_user(struct vringh *vrh,
			struct vringh_iov *riov,
			struct vringh_iov *wiov,
			bool (*getrange)(struct vringh *vrh,
					 u64 addr, struct vringh_range *r),
			u16 *head)
{
	int err;

	*head = vrh->vring.num;
	err = __vringh_get_head(vrh, getu16_user, &vrh->last_avail_idx);
	if (err < 0)
		return err;

	/* Empty... */
	if (err == vrh->vring.num)
		return 0;

	/* We need the layouts to be the identical for this to work */
	BUILD_BUG_ON(sizeof(struct vringh_kiov) != sizeof(struct vringh_iov));
	BUILD_BUG_ON(offsetof(struct vringh_kiov, iov) !=
		     offsetof(struct vringh_iov, iov));
	BUILD_BUG_ON(offsetof(struct vringh_kiov, i) !=
		     offsetof(struct vringh_iov, i));
	BUILD_BUG_ON(offsetof(struct vringh_kiov, used) !=
		     offsetof(struct vringh_iov, used));
	BUILD_BUG_ON(offsetof(struct vringh_kiov, max_num) !=
		     offsetof(struct vringh_iov, max_num));
	BUILD_BUG_ON(sizeof(struct iovec) != sizeof(struct kvec));
	BUILD_BUG_ON(offsetof(struct iovec, iov_base) !=
		     offsetof(struct kvec, iov_base));
	BUILD_BUG_ON(offsetof(struct iovec, iov_len) !=
		     offsetof(struct kvec, iov_len));
	BUILD_BUG_ON(sizeof(((struct iovec *)NULL)->iov_base)
		     != sizeof(((struct kvec *)NULL)->iov_base));
	BUILD_BUG_ON(sizeof(((struct iovec *)NULL)->iov_len)
		     != sizeof(((struct kvec *)NULL)->iov_len));

	*head = err;
	err = __vringh_iov(vrh, *head, (struct vringh_kiov *)riov,
			   (struct vringh_kiov *)wiov,
			   range_check, getrange, GFP_KERNEL, copydesc_user);
	if (err)
		return err;

	return 1;
}
EXPORT_SYMBOL(vringh_getdesc_user);

/**
 * vringh_iov_pull_user - copy bytes from vring_iov.
 * @riov: the riov as passed to vringh_getdesc_user() (updated as we consume)
 * @dst: the place to copy.
 * @len: the maximum length to copy.
 *
 * Returns the bytes copied <= len or a negative errno.
 */
ssize_t vringh_iov_pull_user(struct vringh_iov *riov, void *dst, size_t len)
{
	return vringh_iov_xfer(NULL, (struct vringh_kiov *)riov,
			       dst, len, xfer_from_user);
}
EXPORT_SYMBOL(vringh_iov_pull_user);

/**
 * vringh_iov_push_user - copy bytes into vring_iov.
 * @wiov: the wiov as passed to vringh_getdesc_user() (updated as we consume)
 * @dst: the place to copy.
 * @len: the maximum length to copy.
 *
 * Returns the bytes copied <= len or a negative errno.
 */
ssize_t vringh_iov_push_user(struct vringh_iov *wiov,
			     const void *src, size_t len)
{
	return vringh_iov_xfer(NULL, (struct vringh_kiov *)wiov,
			       (void *)src, len, xfer_to_user);
}
EXPORT_SYMBOL(vringh_iov_push_user);

/**
 * vringh_abandon_user - we've decided not to handle the descriptor(s).
 * @vrh: the vring.
 * @num: the number of descriptors to put back (ie. num
 *	 vringh_get_user() to undo).
 *
 * The next vringh_get_user() will return the old descriptor(s) again.
 */
void vringh_abandon_user(struct vringh *vrh, unsigned int num)
{
	/* We only update vring_avail_event(vr) when we want to be notified,
	 * so we haven't changed that yet. */
	vrh->last_avail_idx -= num;
}
EXPORT_SYMBOL(vringh_abandon_user);

/**
 * vringh_complete_user - we've finished with descriptor, publish it.
 * @vrh: the vring.
 * @head: the head as filled in by vringh_getdesc_user.
 * @len: the length of data we have written.
 *
 * You should check vringh_need_notify_user() after one or more calls
 * to this function.
 */
int vringh_complete_user(struct vringh *vrh, u16 head, u32 len)
{
	struct vring_used_elem used;

	used.id = cpu_to_vringh32(vrh, head);
	used.len = cpu_to_vringh32(vrh, len);
	return __vringh_complete(vrh, &used, 1, putu16_user, putused_user);
}
EXPORT_SYMBOL(vringh_complete_user);

/**
 * vringh_complete_multi_user - we've finished with many descriptors.
 * @vrh: the vring.
 * @used: the head, length pairs.
 * @num_used: the number of used elements.
 *
 * You should check vringh_need_notify_user() after one or more calls
 * to this function.
 */
int vringh_complete_multi_user(struct vringh *vrh,
			       const struct vring_used_elem used[],
			       unsigned num_used)
{
	return __vringh_complete(vrh, used, num_used,
				 putu16_user, putused_user);
}
EXPORT_SYMBOL(vringh_complete_multi_user);

/**
 * vringh_notify_enable_user - we want to know if something changes.
 * @vrh: the vring.
 *
 * This always enables notifications, but returns false if there are
 * now more buffers available in the vring.
 */
bool vringh_notify_enable_user(struct vringh *vrh)
{
	return __vringh_notify_enable(vrh, getu16_user, putu16_user);
}
EXPORT_SYMBOL(vringh_notify_enable_user);

/**
 * vringh_notify_disable_user - don't tell us if something changes.
 * @vrh: the vring.
 *
 * This is our normal running state: we disable and then only enable when
 * we're going to sleep.
 */
void vringh_notify_disable_user(struct vringh *vrh)
{
	__vringh_notify_disable(vrh, putu16_user);
}
EXPORT_SYMBOL(vringh_notify_disable_user);

/**
 * vringh_need_notify_user - must we tell the other side about used buffers?
 * @vrh: the vring we've called vringh_complete_user() on.
 *
 * Returns -errno or 0 if we don't need to tell the other side, 1 if we do.
 */
int vringh_need_notify_user(struct vringh *vrh)
{
	return __vringh_need_notify(vrh, getu16_user);
}
EXPORT_SYMBOL(vringh_need_notify_user);

/* Kernelspace access helpers. */
static inline int getu16_kern(const struct vringh *vrh,
			      u16 *val, const __virtio16 *p)
{
	*val = vringh16_to_cpu(vrh, READ_ONCE(*p));
	return 0;
}

static inline int putu16_kern(const struct vringh *vrh, __virtio16 *p, u16 val)
{
	WRITE_ONCE(*p, cpu_to_vringh16(vrh, val));
	return 0;
}

static inline int copydesc_kern(const struct vringh *vrh,
				void *dst, const void *src, size_t len)
{
	memcpy(dst, src, len);
	return 0;
}

static inline int putused_kern(const struct vringh *vrh,
			       struct vring_used_elem *dst,
			       const struct vring_used_elem *src,
			       unsigned int num)
{
	memcpy(dst, src, num * sizeof(*dst));
	return 0;
}

static inline int xfer_kern(const struct vringh *vrh, void *src,
			    void *dst, size_t len)
{
	memcpy(dst, src, len);
	return 0;
}

static inline int kern_xfer(const struct vringh *vrh, void *dst,
			    void *src, size_t len)
{
	memcpy(dst, src, len);
	return 0;
}

/**
 * vringh_init_kern - initialize a vringh for a kernelspace vring.
 * @vrh: the vringh to initialize.
 * @features: the feature bits for this ring.
 * @num: the number of elements.
 * @weak_barriers: true if we only need memory barriers, not I/O.
 * @desc: the userpace descriptor pointer.
 * @avail: the userpace avail pointer.
 * @used: the userpace used pointer.
 *
 * Returns an error if num is invalid.
 */
int vringh_init_kern(struct vringh *vrh, u64 features,
		     unsigned int num, bool weak_barriers,
		     struct vring_desc *desc,
		     struct vring_avail *avail,
		     struct vring_used *used)
{
	/* Sane power of 2 please! */
	if (!num || num > 0xffff || (num & (num - 1))) {
		vringh_bad("Bad ring size %u", num);
		return -EINVAL;
	}

	vrh->little_endian = (features & (1ULL << VIRTIO_F_VERSION_1));
	vrh->event_indices = (features & (1 << VIRTIO_RING_F_EVENT_IDX));
	vrh->weak_barriers = weak_barriers;
	vrh->completed = 0;
	vrh->last_avail_idx = 0;
	vrh->last_used_idx = 0;
	vrh->vring.num = num;
	vrh->vring.desc = desc;
	vrh->vring.avail = avail;
	vrh->vring.used = used;
	return 0;
}
EXPORT_SYMBOL(vringh_init_kern);

/**
 * vringh_getdesc_kern - get next available descriptor from kernelspace ring.
 * @vrh: the kernelspace vring.
 * @riov: where to put the readable descriptors (or NULL)
 * @wiov: where to put the writable descriptors (or NULL)
 * @head: head index we received, for passing to vringh_complete_kern().
 * @gfp: flags for allocating larger riov/wiov.
 *
 * Returns 0 if there was no descriptor, 1 if there was, or -errno.
 *
 * Note that on error return, you can tell the difference between an
 * invalid ring and a single invalid descriptor: in the former case,
 * *head will be vrh->vring.num.  You may be able to ignore an invalid
 * descriptor, but there's not much you can do with an invalid ring.
 *
 * Note that you may need to clean up riov and wiov, even on error!
 */
int vringh_getdesc_kern(struct vringh *vrh,
			struct vringh_kiov *riov,
			struct vringh_kiov *wiov,
			u16 *head,
			gfp_t gfp)
{
	int err;

	err = __vringh_get_head(vrh, getu16_kern, &vrh->last_avail_idx);
	if (err < 0)
		return err;

	/* Empty... */
	if (err == vrh->vring.num)
		return 0;

	*head = err;
	err = __vringh_iov(vrh, *head, riov, wiov, no_range_check, NULL,
			   gfp, copydesc_kern);
	if (err)
		return err;

	return 1;
}
EXPORT_SYMBOL(vringh_getdesc_kern);

/**
 * vringh_iov_pull_kern - copy bytes from vring_iov.
 * @riov: the riov as passed to vringh_getdesc_kern() (updated as we consume)
 * @dst: the place to copy.
 * @len: the maximum length to copy.
 *
 * Returns the bytes copied <= len or a negative errno.
 */
ssize_t vringh_iov_pull_kern(struct vringh_kiov *riov, void *dst, size_t len)
{
	return vringh_iov_xfer(NULL, riov, dst, len, xfer_kern);
}
EXPORT_SYMBOL(vringh_iov_pull_kern);

/**
 * vringh_iov_push_kern - copy bytes into vring_iov.
 * @wiov: the wiov as passed to vringh_getdesc_kern() (updated as we consume)
 * @dst: the place to copy.
 * @len: the maximum length to copy.
 *
 * Returns the bytes copied <= len or a negative errno.
 */
ssize_t vringh_iov_push_kern(struct vringh_kiov *wiov,
			     const void *src, size_t len)
{
	return vringh_iov_xfer(NULL, wiov, (void *)src, len, kern_xfer);
}
EXPORT_SYMBOL(vringh_iov_push_kern);

/**
 * vringh_abandon_kern - we've decided not to handle the descriptor(s).
 * @vrh: the vring.
 * @num: the number of descriptors to put back (ie. num
 *	 vringh_get_kern() to undo).
 *
 * The next vringh_get_kern() will return the old descriptor(s) again.
 */
void vringh_abandon_kern(struct vringh *vrh, unsigned int num)
{
	/* We only update vring_avail_event(vr) when we want to be notified,
	 * so we haven't changed that yet. */
	vrh->last_avail_idx -= num;
}
EXPORT_SYMBOL(vringh_abandon_kern);

/**
 * vringh_complete_kern - we've finished with descriptor, publish it.
 * @vrh: the vring.
 * @head: the head as filled in by vringh_getdesc_kern.
 * @len: the length of data we have written.
 *
 * You should check vringh_need_notify_kern() after one or more calls
 * to this function.
 */
int vringh_complete_kern(struct vringh *vrh, u16 head, u32 len)
{
	struct vring_used_elem used;

	used.id = cpu_to_vringh32(vrh, head);
	used.len = cpu_to_vringh32(vrh, len);

	return __vringh_complete(vrh, &used, 1, putu16_kern, putused_kern);
}
EXPORT_SYMBOL(vringh_complete_kern);

/**
 * vringh_notify_enable_kern - we want to know if something changes.
 * @vrh: the vring.
 *
 * This always enables notifications, but returns false if there are
 * now more buffers available in the vring.
 */
bool vringh_notify_enable_kern(struct vringh *vrh)
{
	return __vringh_notify_enable(vrh, getu16_kern, putu16_kern);
}
EXPORT_SYMBOL(vringh_notify_enable_kern);

/**
 * vringh_notify_disable_kern - don't tell us if something changes.
 * @vrh: the vring.
 *
 * This is our normal running state: we disable and then only enable when
 * we're going to sleep.
 */
void vringh_notify_disable_kern(struct vringh *vrh)
{
	__vringh_notify_disable(vrh, putu16_kern);
}
EXPORT_SYMBOL(vringh_notify_disable_kern);

/**
 * vringh_need_notify_kern - must we tell the other side about used buffers?
 * @vrh: the vring we've called vringh_complete_kern() on.
 *
 * Returns -errno or 0 if we don't need to tell the other side, 1 if we do.
 */
int vringh_need_notify_kern(struct vringh *vrh)
{
	return __vringh_need_notify(vrh, getu16_kern);
}
EXPORT_SYMBOL(vringh_need_notify_kern);

#if IS_REACHABLE(CONFIG_VHOST_IOTLB)

static int iotlb_translate(const struct vringh *vrh,
			   u64 addr, u64 len, struct bio_vec iov[],
			   int iov_size, u32 perm)
{
	struct vhost_iotlb_map *map;
	struct vhost_iotlb *iotlb = vrh->iotlb;
	int ret = 0;
	u64 s = 0;

	while (len > s) {
		u64 size, pa, pfn;

		if (unlikely(ret >= iov_size)) {
			ret = -ENOBUFS;
			break;
		}

		map = vhost_iotlb_itree_first(iotlb, addr,
					      addr + len - 1);
		if (!map || map->start > addr) {
			ret = -EINVAL;
			break;
		} else if (!(map->perm & perm)) {
			ret = -EPERM;
			break;
		}

		size = map->size - addr + map->start;
		pa = map->addr + addr - map->start;
		pfn = pa >> PAGE_SHIFT;
		iov[ret].bv_page = pfn_to_page(pfn);
		iov[ret].bv_len = min(len - s, size);
		iov[ret].bv_offset = pa & (PAGE_SIZE - 1);
		s += size;
		addr += size;
		++ret;
	}

	return ret;
}

static inline int copy_from_iotlb(const struct vringh *vrh, void *dst,
				  void *src, size_t len)
{
	struct iov_iter iter;
	struct bio_vec iov[16];
	int ret;

	ret = iotlb_translate(vrh, (u64)(uintptr_t)src,
			      len, iov, 16, VHOST_MAP_RO);
	if (ret < 0)
		return ret;

	iov_iter_bvec(&iter, READ, iov, ret, len);

	ret = copy_from_iter(dst, len, &iter);

	return ret;
}

static inline int copy_to_iotlb(const struct vringh *vrh, void *dst,
				void *src, size_t len)
{
	struct iov_iter iter;
	struct bio_vec iov[16];
	int ret;

	ret = iotlb_translate(vrh, (u64)(uintptr_t)dst,
			      len, iov, 16, VHOST_MAP_WO);
	if (ret < 0)
		return ret;

	iov_iter_bvec(&iter, WRITE, iov, ret, len);

	return copy_to_iter(src, len, &iter);
}

static inline int getu16_iotlb(const struct vringh *vrh,
			       u16 *val, const __virtio16 *p)
{
	struct bio_vec iov;
	void *kaddr, *from;
	int ret;

	/* Atomic read is needed for getu16 */
	ret = iotlb_translate(vrh, (u64)(uintptr_t)p, sizeof(*p),
			      &iov, 1, VHOST_MAP_RO);
	if (ret < 0)
		return ret;

	kaddr = kmap_atomic(iov.bv_page);
	from = kaddr + iov.bv_offset;
	*val = vringh16_to_cpu(vrh, READ_ONCE(*(__virtio16 *)from));
	kunmap_atomic(kaddr);

	return 0;
}

static inline int putu16_iotlb(const struct vringh *vrh,
			       __virtio16 *p, u16 val)
{
	struct bio_vec iov;
	void *kaddr, *to;
	int ret;

	/* Atomic write is needed for putu16 */
	ret = iotlb_translate(vrh, (u64)(uintptr_t)p, sizeof(*p),
			      &iov, 1, VHOST_MAP_WO);
	if (ret < 0)
		return ret;

	kaddr = kmap_atomic(iov.bv_page);
	to = kaddr + iov.bv_offset;
	WRITE_ONCE(*(__virtio16 *)to, cpu_to_vringh16(vrh, val));
	kunmap_atomic(kaddr);

	return 0;
}

static inline int copydesc_iotlb(const struct vringh *vrh,
				 void *dst, const void *src, size_t len)
{
	int ret;

	ret = copy_from_iotlb(vrh, dst, (void *)src, len);
	if (ret != len)
		return -EFAULT;

	return 0;
}

static inline int xfer_from_iotlb(const struct vringh *vrh, void *src,
				  void *dst, size_t len)
{
	int ret;

	ret = copy_from_iotlb(vrh, dst, src, len);
	if (ret != len)
		return -EFAULT;

	return 0;
}

static inline int xfer_to_iotlb(const struct vringh *vrh,
			       void *dst, void *src, size_t len)
{
	int ret;

	ret = copy_to_iotlb(vrh, dst, src, len);
	if (ret != len)
		return -EFAULT;

	return 0;
}

static inline int putused_iotlb(const struct vringh *vrh,
				struct vring_used_elem *dst,
				const struct vring_used_elem *src,
				unsigned int num)
{
	int size = num * sizeof(*dst);
	int ret;

	ret = copy_to_iotlb(vrh, dst, (void *)src, num * sizeof(*dst));
	if (ret != size)
		return -EFAULT;

	return 0;
}

/**
 * vringh_init_iotlb - initialize a vringh for a ring with IOTLB.
 * @vrh: the vringh to initialize.
 * @features: the feature bits for this ring.
 * @num: the number of elements.
 * @weak_barriers: true if we only need memory barriers, not I/O.
 * @desc: the userpace descriptor pointer.
 * @avail: the userpace avail pointer.
 * @used: the userpace used pointer.
 *
 * Returns an error if num is invalid.
 */
int vringh_init_iotlb(struct vringh *vrh, u64 features,
		      unsigned int num, bool weak_barriers,
		      struct vring_desc *desc,
		      struct vring_avail *avail,
		      struct vring_used *used)
{
	return vringh_init_kern(vrh, features, num, weak_barriers,
				desc, avail, used);
}
EXPORT_SYMBOL(vringh_init_iotlb);

/**
 * vringh_set_iotlb - initialize a vringh for a ring with IOTLB.
 * @vrh: the vring
 * @iotlb: iotlb associated with this vring
 */
void vringh_set_iotlb(struct vringh *vrh, struct vhost_iotlb *iotlb)
{
	vrh->iotlb = iotlb;
}
EXPORT_SYMBOL(vringh_set_iotlb);

/**
 * vringh_getdesc_iotlb - get next available descriptor from ring with
 * IOTLB.
 * @vrh: the kernelspace vring.
 * @riov: where to put the readable descriptors (or NULL)
 * @wiov: where to put the writable descriptors (or NULL)
 * @head: head index we received, for passing to vringh_complete_iotlb().
 * @gfp: flags for allocating larger riov/wiov.
 *
 * Returns 0 if there was no descriptor, 1 if there was, or -errno.
 *
 * Note that on error return, you can tell the difference between an
 * invalid ring and a single invalid descriptor: in the former case,
 * *head will be vrh->vring.num.  You may be able to ignore an invalid
 * descriptor, but there's not much you can do with an invalid ring.
 *
 * Note that you may need to clean up riov and wiov, even on error!
 */
int vringh_getdesc_iotlb(struct vringh *vrh,
			 struct vringh_kiov *riov,
			 struct vringh_kiov *wiov,
			 u16 *head,
			 gfp_t gfp)
{
	int err;

	err = __vringh_get_head(vrh, getu16_iotlb, &vrh->last_avail_idx);
	if (err < 0)
		return err;

	/* Empty... */
	if (err == vrh->vring.num)
		return 0;

	*head = err;
	err = __vringh_iov(vrh, *head, riov, wiov, no_range_check, NULL,
			   gfp, copydesc_iotlb);
	if (err)
		return err;

	return 1;
}
EXPORT_SYMBOL(vringh_getdesc_iotlb);

/**
 * vringh_iov_pull_iotlb - copy bytes from vring_iov.
 * @vrh: the vring.
 * @riov: the riov as passed to vringh_getdesc_iotlb() (updated as we consume)
 * @dst: the place to copy.
 * @len: the maximum length to copy.
 *
 * Returns the bytes copied <= len or a negative errno.
 */
ssize_t vringh_iov_pull_iotlb(struct vringh *vrh,
			      struct vringh_kiov *riov,
			      void *dst, size_t len)
{
	return vringh_iov_xfer(vrh, riov, dst, len, xfer_from_iotlb);
}
EXPORT_SYMBOL(vringh_iov_pull_iotlb);

/**
 * vringh_iov_push_iotlb - copy bytes into vring_iov.
 * @vrh: the vring.
 * @wiov: the wiov as passed to vringh_getdesc_iotlb() (updated as we consume)
 * @dst: the place to copy.
 * @len: the maximum length to copy.
 *
 * Returns the bytes copied <= len or a negative errno.
 */
ssize_t vringh_iov_push_iotlb(struct vringh *vrh,
			      struct vringh_kiov *wiov,
			      const void *src, size_t len)
{
	return vringh_iov_xfer(vrh, wiov, (void *)src, len, xfer_to_iotlb);
}
EXPORT_SYMBOL(vringh_iov_push_iotlb);

/**
 * vringh_abandon_iotlb - we've decided not to handle the descriptor(s).
 * @vrh: the vring.
 * @num: the number of descriptors to put back (ie. num
 *	 vringh_get_iotlb() to undo).
 *
 * The next vringh_get_iotlb() will return the old descriptor(s) again.
 */
void vringh_abandon_iotlb(struct vringh *vrh, unsigned int num)
{
	/* We only update vring_avail_event(vr) when we want to be notified,
	 * so we haven't changed that yet.
	 */
	vrh->last_avail_idx -= num;
}
EXPORT_SYMBOL(vringh_abandon_iotlb);

/**
 * vringh_complete_iotlb - we've finished with descriptor, publish it.
 * @vrh: the vring.
 * @head: the head as filled in by vringh_getdesc_iotlb.
 * @len: the length of data we have written.
 *
 * You should check vringh_need_notify_iotlb() after one or more calls
 * to this function.
 */
int vringh_complete_iotlb(struct vringh *vrh, u16 head, u32 len)
{
	struct vring_used_elem used;

	used.id = cpu_to_vringh32(vrh, head);
	used.len = cpu_to_vringh32(vrh, len);

	return __vringh_complete(vrh, &used, 1, putu16_iotlb, putused_iotlb);
}
EXPORT_SYMBOL(vringh_complete_iotlb);

/**
 * vringh_notify_enable_iotlb - we want to know if something changes.
 * @vrh: the vring.
 *
 * This always enables notifications, but returns false if there are
 * now more buffers available in the vring.
 */
bool vringh_notify_enable_iotlb(struct vringh *vrh)
{
	return __vringh_notify_enable(vrh, getu16_iotlb, putu16_iotlb);
}
EXPORT_SYMBOL(vringh_notify_enable_iotlb);

/**
 * vringh_notify_disable_iotlb - don't tell us if something changes.
 * @vrh: the vring.
 *
 * This is our normal running state: we disable and then only enable when
 * we're going to sleep.
 */
void vringh_notify_disable_iotlb(struct vringh *vrh)
{
	__vringh_notify_disable(vrh, putu16_iotlb);
}
EXPORT_SYMBOL(vringh_notify_disable_iotlb);

/**
 * vringh_need_notify_iotlb - must we tell the other side about used buffers?
 * @vrh: the vring we've called vringh_complete_iotlb() on.
 *
 * Returns -errno or 0 if we don't need to tell the other side, 1 if we do.
 */
int vringh_need_notify_iotlb(struct vringh *vrh)
{
	return __vringh_need_notify(vrh, getu16_iotlb);
}
EXPORT_SYMBOL(vringh_need_notify_iotlb);

#endif

MODULE_LICENSE("GPL");
