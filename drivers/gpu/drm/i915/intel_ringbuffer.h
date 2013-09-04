#ifndef _INTEL_RINGBUFFER_H_
#define _INTEL_RINGBUFFER_H_

/*
 * Gen2 BSpec "1. Programming Environment" / 1.4.4.6 "Ring Buffer Use"
 * Gen3 BSpec "vol1c Memory Interface Functions" / 2.3.4.5 "Ring Buffer Use"
 * Gen4+ BSpec "vol1c Memory Interface and Command Stream" / 5.3.4.5 "Ring Buffer Use"
 *
 * "If the Ring Buffer Head Pointer and the Tail Pointer are on the same
 * cacheline, the Head Pointer must not be greater than the Tail
 * Pointer."
 */
#define I915_RING_FREE_SPACE 64

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

enum intel_ring_hangcheck_action {
	HANGCHECK_WAIT,
	HANGCHECK_ACTIVE,
	HANGCHECK_KICK,
	HANGCHECK_HUNG,
};

struct intel_ring_hangcheck {
	bool deadlock;
	u32 seqno;
	u32 acthd;
	int score;
	enum intel_ring_hangcheck_action action;
};

struct  intel_ring_buffer {
	const char	*name;
	enum intel_ring_id {
		RCS = 0x0,
		VCS,
		BCS,
		VECS,
	} id;
#define I915_NUM_RINGS 4
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

	unsigned irq_refcount; /* protected by dev_priv->irq_lock */
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
	int		(*add_request)(struct intel_ring_buffer *ring);
	/* Some chipsets are not quite as coherent as advertised and need
	 * an expensive kick to force a true read of the up-to-date seqno.
	 * However, the up-to-date seqno is not always required and the last
	 * seen value is good enough. Note that the seqno will always be
	 * monotonic, even if not coherent.
	 */
	u32		(*get_seqno)(struct intel_ring_buffer *ring,
				     bool lazy_coherency);
	void		(*set_seqno)(struct intel_ring_buffer *ring,
				     u32 seqno);
	int		(*dispatch_execbuffer)(struct intel_ring_buffer *ring,
					       u32 offset, u32 length,
					       unsigned flags);
#define I915_DISPATCH_SECURE 0x1
#define I915_DISPATCH_PINNED 0x2
	void		(*cleanup)(struct intel_ring_buffer *ring);
	int		(*sync_to)(struct intel_ring_buffer *ring,
				   struct intel_ring_buffer *to,
				   u32 seqno);

	/* our mbox written by others */
	u32		semaphore_register[I915_NUM_RINGS];
	/* mboxes this ring signals to */
	u32		signal_mbox[I915_NUM_RINGS];

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
	 * Do we have some not yet emitted requests outstanding?
	 */
	u32 outstanding_lazy_seqno;
	bool gpu_caches_dirty;
	bool fbc_dirty;

	wait_queue_head_t irq_queue;

	/**
	 * Do an explicit TLB flush before MI_SET_CONTEXT
	 */
	bool itlb_before_ctx_switch;
	struct i915_hw_context *default_context;
	struct i915_hw_context *last_context;

	struct intel_ring_hangcheck hangcheck;

	struct {
		struct drm_i915_gem_object *obj;
		u32 gtt_offset;
		volatile u32 *cpu_page;
	} scratch;
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

static inline void
intel_write_status_page(struct intel_ring_buffer *ring,
			int reg, u32 value)
{
	ring->status_page.page_addr[reg] = value;
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
#define I915_GEM_HWS_SCRATCH_INDEX	0x30
#define I915_GEM_HWS_SCRATCH_ADDR (I915_GEM_HWS_SCRATCH_INDEX << MI_STORE_DWORD_INDEX_SHIFT)

void intel_cleanup_ring_buffer(struct intel_ring_buffer *ring);

int __must_check intel_ring_begin(struct intel_ring_buffer *ring, int n);
static inline void intel_ring_emit(struct intel_ring_buffer *ring,
				   u32 data)
{
	iowrite32(data, ring->virtual_start + ring->tail);
	ring->tail += 4;
}
void intel_ring_advance(struct intel_ring_buffer *ring);
int __must_check intel_ring_idle(struct intel_ring_buffer *ring);
void intel_ring_init_seqno(struct intel_ring_buffer *ring, u32 seqno);
int intel_ring_flush_all_caches(struct intel_ring_buffer *ring);
int intel_ring_invalidate_all_caches(struct intel_ring_buffer *ring);

int intel_init_render_ring_buffer(struct drm_device *dev);
int intel_init_bsd_ring_buffer(struct drm_device *dev);
int intel_init_blt_ring_buffer(struct drm_device *dev);
int intel_init_vebox_ring_buffer(struct drm_device *dev);

u32 intel_ring_get_active_head(struct intel_ring_buffer *ring);
void intel_ring_setup_status_page(struct intel_ring_buffer *ring);

static inline u32 intel_ring_get_tail(struct intel_ring_buffer *ring)
{
	return ring->tail;
}

static inline u32 intel_ring_get_seqno(struct intel_ring_buffer *ring)
{
	BUG_ON(ring->outstanding_lazy_seqno == 0);
	return ring->outstanding_lazy_seqno;
}

static inline void i915_trace_irq_get(struct intel_ring_buffer *ring, u32 seqno)
{
	if (ring->trace_irq_seqno == 0 && ring->irq_get(ring))
		ring->trace_irq_seqno = seqno;
}

/* DRI warts */
int intel_render_ring_init_dri(struct drm_device *dev, u64 start, u32 size);

#endif /* _INTEL_RINGBUFFER_H_ */
