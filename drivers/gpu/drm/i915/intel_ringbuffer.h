#ifndef _INTEL_RINGBUFFER_H_
#define _INTEL_RINGBUFFER_H_

enum {
    RCS = 0x0,
    VCS,
    BCS,
    I915_NUM_RINGS,
};

struct  intel_hw_status_page {
	u32	__iomem	*page_addr;
	unsigned int	gfx_addr;
	struct		drm_i915_gem_object *obj;
};

#define I915_RING_READ(reg) i915_safe_read(dev_priv, reg)

#define I915_READ_TAIL(ring) I915_RING_READ(RING_TAIL((ring)->mmio_base))
#define I915_WRITE_TAIL(ring, val) I915_WRITE(RING_TAIL((ring)->mmio_base), val)

#define I915_READ_START(ring) I915_RING_READ(RING_START((ring)->mmio_base))
#define I915_WRITE_START(ring, val) I915_WRITE(RING_START((ring)->mmio_base), val)

#define I915_READ_HEAD(ring)  I915_RING_READ(RING_HEAD((ring)->mmio_base))
#define I915_WRITE_HEAD(ring, val) I915_WRITE(RING_HEAD((ring)->mmio_base), val)

#define I915_READ_CTL(ring) I915_RING_READ(RING_CTL((ring)->mmio_base))
#define I915_WRITE_CTL(ring, val) I915_WRITE(RING_CTL((ring)->mmio_base), val)

#define I915_WRITE_IMR(ring, val) I915_WRITE(RING_IMR((ring)->mmio_base), val)
#define I915_READ_IMR(ring) I915_RING_READ(RING_IMR((ring)->mmio_base))

#define I915_READ_NOPID(ring) I915_RING_READ(RING_NOPID((ring)->mmio_base))
#define I915_READ_SYNC_0(ring) I915_RING_READ(RING_SYNC_0((ring)->mmio_base))
#define I915_READ_SYNC_1(ring) I915_RING_READ(RING_SYNC_1((ring)->mmio_base))

struct  intel_ring_buffer {
	const char	*name;
	enum intel_ring_id {
		RING_RENDER = 0x1,
		RING_BSD = 0x2,
		RING_BLT = 0x4,
	} id;
	u32		mmio_base;
	void		*virtual_start;
	struct		drm_device *dev;
	struct		drm_i915_gem_object *obj;

	u32		actual_head;
	u32		head;
	u32		tail;
	int		space;
	int		size;
	int		effective_size;
	struct intel_hw_status_page status_page;

	u32		irq_refcount;
	u32		irq_mask;
	u32		irq_seqno;		/* last seq seem at irq time */
	u32		waiting_seqno;
	u32		sync_seqno[I915_NUM_RINGS-1];
	bool __must_check (*irq_get)(struct intel_ring_buffer *ring);
	void		(*irq_put)(struct intel_ring_buffer *ring);

	int		(*init)(struct intel_ring_buffer *ring);

	void		(*write_tail)(struct intel_ring_buffer *ring,
				      u32 value);
	int __must_check (*flush)(struct intel_ring_buffer *ring,
				  u32	invalidate_domains,
				  u32	flush_domains);
	int		(*add_request)(struct intel_ring_buffer *ring,
				       u32 *seqno);
	u32		(*get_seqno)(struct intel_ring_buffer *ring);
	int		(*dispatch_execbuffer)(struct intel_ring_buffer *ring,
					       u32 offset, u32 length);
	void		(*cleanup)(struct intel_ring_buffer *ring);

	/**
	 * List of objects currently involved in rendering from the
	 * ringbuffer.
	 *
	 * Includes buffers having the contents of their GPU caches
	 * flushed, not necessarily primitives.  last_rendering_seqno
	 * represents when the rendering involved will be completed.
	 *
	 * A reference is held on the buffer while on this list.
	 */
	struct list_head active_list;

	/**
	 * List of breadcrumbs associated with GPU requests currently
	 * outstanding.
	 */
	struct list_head request_list;

	/**
	 * List of objects currently pending a GPU write flush.
	 *
	 * All elements on this list will belong to either the
	 * active_list or flushing_list, last_rendering_seqno can
	 * be used to differentiate between the two elements.
	 */
	struct list_head gpu_write_list;

	/**
	 * Do we have some not yet emitted requests outstanding?
	 */
	u32 outstanding_lazy_request;

	wait_queue_head_t irq_queue;
	drm_local_map_t map;

	void *private;
};

static inline u32
intel_ring_sync_index(struct intel_ring_buffer *ring,
		      struct intel_ring_buffer *other)
{
	int idx;

	/*
	 * cs -> 0 = vcs, 1 = bcs
	 * vcs -> 0 = bcs, 1 = cs,
	 * bcs -> 0 = cs, 1 = vcs.
	 */

	idx = (other - ring) - 1;
	if (idx < 0)
		idx += I915_NUM_RINGS;

	return idx;
}

static inline u32
intel_read_status_page(struct intel_ring_buffer *ring,
		       int reg)
{
	return ioread32(ring->status_page.page_addr + reg);
}

void intel_cleanup_ring_buffer(struct intel_ring_buffer *ring);
int __must_check intel_wait_ring_buffer(struct intel_ring_buffer *ring, int n);
int __must_check intel_ring_begin(struct intel_ring_buffer *ring, int n);

static inline void intel_ring_emit(struct intel_ring_buffer *ring,
				   u32 data)
{
	iowrite32(data, ring->virtual_start + ring->tail);
	ring->tail += 4;
}

void intel_ring_advance(struct intel_ring_buffer *ring);

u32 intel_ring_get_seqno(struct intel_ring_buffer *ring);
int intel_ring_sync(struct intel_ring_buffer *ring,
		    struct intel_ring_buffer *to,
		    u32 seqno);

int intel_init_render_ring_buffer(struct drm_device *dev);
int intel_init_bsd_ring_buffer(struct drm_device *dev);
int intel_init_blt_ring_buffer(struct drm_device *dev);

u32 intel_ring_get_active_head(struct intel_ring_buffer *ring);
void intel_ring_setup_status_page(struct intel_ring_buffer *ring);

#endif /* _INTEL_RINGBUFFER_H_ */
