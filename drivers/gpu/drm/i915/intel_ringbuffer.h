#ifndef _INTEL_RINGBUFFER_H_
#define _INTEL_RINGBUFFER_H_

struct  intel_hw_status_page {
	void		*page_addr;
	unsigned int	gfx_addr;
	struct		drm_gem_object *obj;
};

struct drm_i915_gem_execbuffer2;
struct  intel_ring_buffer {
	const char	*name;
	struct		ring_regs {
			u32 ctl;
			u32 head;
			u32 tail;
			u32 start;
	} regs;
	unsigned int	ring_flag;
	unsigned long	size;
	unsigned int	alignment;
	void		*virtual_start;
	struct		drm_device *dev;
	struct		drm_gem_object *gem_object;

	unsigned int	head;
	unsigned int	tail;
	unsigned int	space;
	u32		next_seqno;
	struct intel_hw_status_page status_page;

	u32		irq_gem_seqno;		/* last seq seem at irq time */
	u32		waiting_gem_seqno;
	int		user_irq_refcount;
	void		(*user_irq_get)(struct drm_device *dev,
			struct intel_ring_buffer *ring);
	void		(*user_irq_put)(struct drm_device *dev,
			struct intel_ring_buffer *ring);
	void		(*setup_status_page)(struct drm_device *dev,
			struct	intel_ring_buffer *ring);

	int		(*init)(struct drm_device *dev,
			struct intel_ring_buffer *ring);

	unsigned int	(*get_head)(struct drm_device *dev,
			struct intel_ring_buffer *ring);
	unsigned int	(*get_tail)(struct drm_device *dev,
			struct intel_ring_buffer *ring);
	unsigned int	(*get_active_head)(struct drm_device *dev,
			struct intel_ring_buffer *ring);
	void		(*advance_ring)(struct drm_device *dev,
			struct intel_ring_buffer *ring);
	void		(*flush)(struct drm_device *dev,
			struct intel_ring_buffer *ring,
			u32	invalidate_domains,
			u32	flush_domains);
	u32		(*add_request)(struct drm_device *dev,
			struct intel_ring_buffer *ring,
			struct drm_file *file_priv,
			u32 flush_domains);
	u32		(*get_gem_seqno)(struct drm_device *dev,
			struct intel_ring_buffer *ring);
	int		(*dispatch_gem_execbuffer)(struct drm_device *dev,
			struct intel_ring_buffer *ring,
			struct drm_i915_gem_execbuffer2 *exec,
			struct drm_clip_rect *cliprects,
			uint64_t exec_offset);

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

	wait_queue_head_t irq_queue;
	drm_local_map_t map;
};

static inline u32
intel_read_status_page(struct intel_ring_buffer *ring,
		int reg)
{
	u32 *regs = ring->status_page.page_addr;
	return regs[reg];
}

int intel_init_ring_buffer(struct drm_device *dev,
		struct intel_ring_buffer *ring);
void intel_cleanup_ring_buffer(struct drm_device *dev,
		struct intel_ring_buffer *ring);
int intel_wait_ring_buffer(struct drm_device *dev,
		struct intel_ring_buffer *ring, int n);
int intel_wrap_ring_buffer(struct drm_device *dev,
		struct intel_ring_buffer *ring);
void intel_ring_begin(struct drm_device *dev,
		struct intel_ring_buffer *ring, int n);
void intel_ring_emit(struct drm_device *dev,
		struct intel_ring_buffer *ring, u32 data);
void intel_fill_struct(struct drm_device *dev,
		struct intel_ring_buffer *ring,
		void *data,
		unsigned int len);
void intel_ring_advance(struct drm_device *dev,
		struct intel_ring_buffer *ring);

u32 intel_ring_get_seqno(struct drm_device *dev,
		struct intel_ring_buffer *ring);

extern struct intel_ring_buffer render_ring;
extern struct intel_ring_buffer bsd_ring;

#endif /* _INTEL_RINGBUFFER_H_ */
