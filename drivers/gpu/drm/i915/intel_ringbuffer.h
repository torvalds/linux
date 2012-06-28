#ifndef _INTEL_RINGBUFFER_H_
#define _INTEL_RINGBUFFER_H_

struct  intel_hw_status_page {
	u32		*page_addr;
	unsigned int	gfx_addr;
	struct		drm_i915_gem_object *obj;
};

#define I915_READ_TAIL(ring) I915_READ(RING_TAIL((ring)->mmio_base))
#define I915_WRITE_TAIL(ring, val) I915_WRITE(RING_TAIL((ring)->mmio_base), val)

#define I915_READ_START(ring) I915_READ(RING_START((ring)->mmio_base))
#define I915_WRITE_START(ring, val) I915_WRITE(RING_START((ring)->mmio_base), val)

#define I915_READ_HEAD(ring)  I915_READ(RING_HEAD((ring)->mmio_base))
#define I915_WRITE_HEAD(ring, val) I915_WRITE(RING_HEAD((ring)->mmio_base), val)

#define I915_READ_CTL(ring) I915_READ(RING_CTL((ring)->mmio_base))
#define I915_WRITE_CTL(ring, val) I915_WRITE(RING_CTL((ring)->mmio_base), val)

#define I915_READ_IMR(ring) I915_READ(RING_IMR((ring)->mmio_base))
#define I915_WRITE_IMR(ring, val) I915_WRITE(RING_IMR((ring)->mmio_base), val)

#define I915_READ_NOPID(ring) I915_READ(RING_NOPID((ring)->mmio_base))
#define I915_READ_SYNC_0(ring) I915_READ(RING_SYNC_0((ring)->mmio_base))
#define I915_READ_SYNC_1(ring) I915_READ(RING_SYNC_1((ring)->mmio_base))

struct  intel_ring_buffer {
	const char	*name;
	enum intel_ring_id {
		RCS = 0x0,
		VCS,
		BCS,
	} id;
#define I915_NUM_RINGS 3
	u32		mmio_base;
	void		__iomem *virtual_start;
	struct		drm_device *dev;
	struct		drm_i915_gem_object *obj;

	u32		head;
	u32		tail;
	int		space;
	int		size;
	int		effective_size;
	struct intel_hw_status_page status_page;

	/** We track the position of the requests in the ring buffer, and
	 * when each is retired we increment last_retired_head as the GPU
	 * must have finished processing the request and so we know we
	 * can advance the ringbuffer up to that position.
	 *
	 * last_retired_head is set to -1 after the value is consumed so
	 * we can detect new retirements.
	 */
	u32		last_retired_head;

	u32		irq_refcount;		/* protected by dev_priv->irq_lock */
	u32		irq_enable_mask;	/* bitmask to enable ring interrupt */
	u32		trace_irq_seqno;
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
	int		(*sync_to)(struct intel_ring_buffer *ring,
				   struct intel_ring_buffer *to,
				   u32 seqno);

	u32		semaphore_register[3]; /*our mbox written by others */
	u32		signal_mbox[2]; /* mboxes this ring signals to */
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
	bool gpu_caches_dirty;

	wait_queue_head_t irq_queue;

	/**
	 * Do an explicit TLB flush before MI_SET_CONTEXT
	 */
	bool itlb_before_ctx_switch;
	struct i915_hw_context *default_context;
	struct drm_i915_gem_object *last_context_obj;

	void *private;
};

static inline bool
intel_ring_initialized(struct intel_ring_buffer *ring)
{
	return ring->obj != NULL;
}

static inline unsigned
intel_ring_flag(struct intel_ring_buffer *ring)
{
	return 1 << ring->id;
}

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
	/* Ensure that the compiler doesn't optimize away the load. */
	barrier();
	return ring->status_page.page_addr[reg];
}

/**
 * Reads a dword out of the status page, which is written to from the command
 * queue by automatic updates, MI_REPORT_HEAD, MI_STORE_DATA_INDEX, or
 * MI_STORE_DATA_IMM.
 *
 * The following dwords have a reserved meaning:
 * 0x00: ISR copy, updated when an ISR bit not set in the HWSTAM changes.
 * 0x04: ring 0 head pointer
 * 0x05: ring 1 head pointer (915-class)
 * 0x06: ring 2 head pointer (915-class)
 * 0x10-0x1b: Context status DWords (GM45)
 * 0x1f: Last written status offset. (GM45)
 *
 * The area from dword 0x20 to 0x3ff is available for driver usage.
 */
#define I915_GEM_HWS_INDEX		0x20

void intel_cleanup_ring_buffer(struct intel_ring_buffer *ring);

int __must_check intel_wait_ring_buffer(struct intel_ring_buffer *ring, int n);
static inline int intel_wait_ring_idle(struct intel_ring_buffer *ring)
{
	return intel_wait_ring_buffer(ring, ring->size - 8);
}

int __must_check intel_ring_begin(struct intel_ring_buffer *ring, int n);

static inline void intel_ring_emit(struct intel_ring_buffer *ring,
				   u32 data)
{
	iowrite32(data, ring->virtual_start + ring->tail);
	ring->tail += 4;
}

void intel_ring_advance(struct intel_ring_buffer *ring);

u32 intel_ring_get_seqno(struct intel_ring_buffer *ring);

int intel_init_render_ring_buffer(struct drm_device *dev);
int intel_init_bsd_ring_buffer(struct drm_device *dev);
int intel_init_blt_ring_buffer(struct drm_device *dev);

u32 intel_ring_get_active_head(struct intel_ring_buffer *ring);
void intel_ring_setup_status_page(struct intel_ring_buffer *ring);

static inline u32 intel_ring_get_tail(struct intel_ring_buffer *ring)
{
	return ring->tail;
}

static inline void i915_trace_irq_get(struct intel_ring_buffer *ring, u32 seqno)
{
	if (ring->trace_irq_seqno == 0 && ring->irq_get(ring))
		ring->trace_irq_seqno = seqno;
}

/* DRI warts */
int intel_render_ring_init_dri(struct drm_device *dev, u64 start, u32 size);

#endif /* _INTEL_RINGBUFFER_H_ */
