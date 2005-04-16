#ifndef _GAMMA_DRM_H_
#define _GAMMA_DRM_H_

typedef struct _drm_gamma_tex_region {
	unsigned char next, prev; /* indices to form a circular LRU  */
	unsigned char in_use;	/* owned by a client, or free? */
	int age;		/* tracked by clients to update local LRU's */
} drm_gamma_tex_region_t;

typedef struct {
	unsigned int	GDeltaMode;
	unsigned int	GDepthMode;
	unsigned int	GGeometryMode;
	unsigned int	GTransformMode;
} drm_gamma_context_regs_t;

typedef struct _drm_gamma_sarea {
   	drm_gamma_context_regs_t context_state;

	unsigned int dirty;


	/* Maintain an LRU of contiguous regions of texture space.  If
	 * you think you own a region of texture memory, and it has an
	 * age different to the one you set, then you are mistaken and
	 * it has been stolen by another client.  If global texAge
	 * hasn't changed, there is no need to walk the list.
	 *
	 * These regions can be used as a proxy for the fine-grained
	 * texture information of other clients - by maintaining them
	 * in the same lru which is used to age their own textures,
	 * clients have an approximate lru for the whole of global
	 * texture space, and can make informed decisions as to which
	 * areas to kick out.  There is no need to choose whether to
	 * kick out your own texture or someone else's - simply eject
	 * them all in LRU order.  
	 */
   
#define GAMMA_NR_TEX_REGIONS 64
	drm_gamma_tex_region_t texList[GAMMA_NR_TEX_REGIONS+1]; 
				/* Last elt is sentinal */
        int texAge;		/* last time texture was uploaded */
        int last_enqueue;	/* last time a buffer was enqueued */
	int last_dispatch;	/* age of the most recently dispatched buffer */
	int last_quiescent;     /*  */
	int ctxOwner;		/* last context to upload state */

	int vertex_prim;
} drm_gamma_sarea_t;

/* WARNING: If you change any of these defines, make sure to change the
 * defines in the Xserver file (xf86drmGamma.h)
 */

/* Gamma specific ioctls
 * The device specific ioctl range is 0x40 to 0x79.
 */
#define DRM_IOCTL_GAMMA_INIT		DRM_IOW( 0x40, drm_gamma_init_t)
#define DRM_IOCTL_GAMMA_COPY		DRM_IOW( 0x41, drm_gamma_copy_t)

typedef struct drm_gamma_copy {
	unsigned int	DMAOutputAddress;
	unsigned int	DMAOutputCount;
	unsigned int	DMAReadGLINTSource;
	unsigned int	DMARectangleWriteAddress;
	unsigned int	DMARectangleWriteLinePitch;
	unsigned int	DMARectangleWrite;
	unsigned int	DMARectangleReadAddress;
	unsigned int	DMARectangleReadLinePitch;
	unsigned int	DMARectangleRead;
	unsigned int	DMARectangleReadTarget;
} drm_gamma_copy_t;

typedef struct drm_gamma_init {
   	enum {
	   	GAMMA_INIT_DMA    = 0x01,
	       	GAMMA_CLEANUP_DMA = 0x02
	} func;

   	int sarea_priv_offset;
	int pcimode;
	unsigned int mmio0;
	unsigned int mmio1;
	unsigned int mmio2;
	unsigned int mmio3;
	unsigned int buffers_offset;
	int num_rast;
} drm_gamma_init_t;

#endif /* _GAMMA_DRM_H_ */
