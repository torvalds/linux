/* i915_drv.h -- Private header for the I915 driver -*- linux-c -*-
 */
/*
 *
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _I915_DRV_H_
#define _I915_DRV_H_

#include "i915_reg.h"
#include "intel_bios.h"
#include <linux/io-mapping.h>

/* General customization:
 */

#define DRIVER_AUTHOR		"Tungsten Graphics, Inc."

#define DRIVER_NAME		"i915"
#define DRIVER_DESC		"Intel Graphics"
#define DRIVER_DATE		"20080730"

enum pipe {
	PIPE_A = 0,
	PIPE_B,
};

enum plane {
	PLANE_A = 0,
	PLANE_B,
};

#define I915_NUM_PIPE	2

/* Interface history:
 *
 * 1.1: Original.
 * 1.2: Add Power Management
 * 1.3: Add vblank support
 * 1.4: Fix cmdbuffer path, add heap destroy
 * 1.5: Add vblank pipe configuration
 * 1.6: - New ioctl for scheduling buffer swaps on vertical blank
 *      - Support vertical blank on secondary display pipe
 */
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		6
#define DRIVER_PATCHLEVEL	0

#define WATCH_COHERENCY	0
#define WATCH_BUF	0
#define WATCH_EXEC	0
#define WATCH_LRU	0
#define WATCH_RELOC	0
#define WATCH_INACTIVE	0
#define WATCH_PWRITE	0

#define I915_GEM_PHYS_CURSOR_0 1
#define I915_GEM_PHYS_CURSOR_1 2
#define I915_GEM_PHYS_OVERLAY_REGS 3
#define I915_MAX_PHYS_OBJECT (I915_GEM_PHYS_OVERLAY_REGS)

struct drm_i915_gem_phys_object {
	int id;
	struct page **page_list;
	drm_dma_handle_t *handle;
	struct drm_gem_object *cur_obj;
};

typedef struct _drm_i915_ring_buffer {
	unsigned long Size;
	u8 *virtual_start;
	int head;
	int tail;
	int space;
	drm_local_map_t map;
	struct drm_gem_object *ring_obj;
} drm_i915_ring_buffer_t;

struct mem_block {
	struct mem_block *next;
	struct mem_block *prev;
	int start;
	int size;
	struct drm_file *file_priv; /* NULL: free, -1: heap, other: real files */
};

struct opregion_header;
struct opregion_acpi;
struct opregion_swsci;
struct opregion_asle;

struct intel_opregion {
	struct opregion_header *header;
	struct opregion_acpi *acpi;
	struct opregion_swsci *swsci;
	struct opregion_asle *asle;
	int enabled;
};

struct drm_i915_master_private {
	drm_local_map_t *sarea;
	struct _drm_i915_sarea *sarea_priv;
};
#define I915_FENCE_REG_NONE -1

struct drm_i915_fence_reg {
	struct drm_gem_object *obj;
};

struct sdvo_device_mapping {
	u8 dvo_port;
	u8 slave_addr;
	u8 dvo_wiring;
	u8 initialized;
};

struct drm_i915_error_state {
	u32 eir;
	u32 pgtbl_er;
	u32 pipeastat;
	u32 pipebstat;
	u32 ipeir;
	u32 ipehr;
	u32 instdone;
	u32 acthd;
	u32 instpm;
	u32 instps;
	u32 instdone1;
	u32 seqno;
	struct timeval time;
};

struct drm_i915_display_funcs {
	void (*dpms)(struct drm_crtc *crtc, int mode);
	bool (*fbc_enabled)(struct drm_crtc *crtc);
	void (*enable_fbc)(struct drm_crtc *crtc, unsigned long interval);
	void (*disable_fbc)(struct drm_device *dev);
	int (*get_display_clock_speed)(struct drm_device *dev);
	int (*get_fifo_size)(struct drm_device *dev, int plane);
	void (*update_wm)(struct drm_device *dev, int planea_clock,
			  int planeb_clock, int sr_hdisplay, int pixel_size);
	/* clock updates for mode set */
	/* cursor updates */
	/* render clock increase/decrease */
	/* display clock increase/decrease */
	/* pll clock increase/decrease */
	/* clock gating init */
};

struct intel_overlay;

struct intel_device_info {
	u8 is_mobile : 1;
	u8 is_i8xx : 1;
	u8 is_i915g : 1;
	u8 is_i9xx : 1;
	u8 is_i945gm : 1;
	u8 is_i965g : 1;
	u8 is_i965gm : 1;
	u8 is_g33 : 1;
	u8 need_gfx_hws : 1;
	u8 is_g4x : 1;
	u8 is_pineview : 1;
	u8 is_ironlake : 1;
	u8 has_fbc : 1;
	u8 has_rc6 : 1;
	u8 has_pipe_cxsr : 1;
	u8 has_hotplug : 1;
	u8 cursor_needs_physical : 1;
};

typedef struct drm_i915_private {
	struct drm_device *dev;

	const struct intel_device_info *info;

	int has_gem;

	void __iomem *regs;

	struct pci_dev *bridge_dev;
	drm_i915_ring_buffer_t ring;

	drm_dma_handle_t *status_page_dmah;
	void *hw_status_page;
	dma_addr_t dma_status_page;
	uint32_t counter;
	unsigned int status_gfx_addr;
	drm_local_map_t hws_map;
	struct drm_gem_object *hws_obj;
	struct drm_gem_object *pwrctx;

	struct resource mch_res;

	unsigned int cpp;
	int back_offset;
	int front_offset;
	int current_page;
	int page_flipping;

	wait_queue_head_t irq_queue;
	atomic_t irq_received;
	/** Protects user_irq_refcount and irq_mask_reg */
	spinlock_t user_irq_lock;
	/** Refcount for i915_user_irq_get() versus i915_user_irq_put(). */
	int user_irq_refcount;
	u32 trace_irq_seqno;
	/** Cached value of IMR to avoid reads in updating the bitfield */
	u32 irq_mask_reg;
	u32 pipestat[2];
	/** splitted irq regs for graphics and display engine on Ironlake,
	    irq_mask_reg is still used for display irq. */
	u32 gt_irq_mask_reg;
	u32 gt_irq_enable_reg;
	u32 de_irq_enable_reg;
	u32 pch_irq_mask_reg;
	u32 pch_irq_enable_reg;

	u32 hotplug_supported_mask;
	struct work_struct hotplug_work;

	int tex_lru_log_granularity;
	int allow_batchbuffer;
	struct mem_block *agp_heap;
	unsigned int sr01, adpa, ppcr, dvob, dvoc, lvds;
	int vblank_pipe;

	/* For hangcheck timer */
#define DRM_I915_HANGCHECK_PERIOD 75 /* in jiffies */
	struct timer_list hangcheck_timer;
	int hangcheck_count;
	uint32_t last_acthd;

	struct drm_mm vram;

	unsigned long cfb_size;
	unsigned long cfb_pitch;
	int cfb_fence;
	int cfb_plane;

	int irq_enabled;

	struct intel_opregion opregion;

	/* overlay */
	struct intel_overlay *overlay;

	/* LVDS info */
	int backlight_duty_cycle;  /* restore backlight to this value */
	bool panel_wants_dither;
	struct drm_display_mode *panel_fixed_mode;
	struct drm_display_mode *lfp_lvds_vbt_mode; /* if any */
	struct drm_display_mode *sdvo_lvds_vbt_mode; /* if any */

	/* Feature bits from the VBIOS */
	unsigned int int_tv_support:1;
	unsigned int lvds_dither:1;
	unsigned int lvds_vbt:1;
	unsigned int int_crt_support:1;
	unsigned int lvds_use_ssc:1;
	unsigned int edp_support:1;
	int lvds_ssc_freq;
	int edp_bpp;

	struct notifier_block lid_notifier;

	int crt_ddc_bus; /* 0 = unknown, else GPIO to use for CRT DDC */
	struct drm_i915_fence_reg fence_regs[16]; /* assume 965 */
	int fence_reg_start; /* 4 if userland hasn't ioctl'd us yet */
	int num_fence_regs; /* 8 on pre-965, 16 otherwise */

	unsigned int fsb_freq, mem_freq;

	spinlock_t error_lock;
	struct drm_i915_error_state *first_error;
	struct work_struct error_work;
	struct workqueue_struct *wq;

	/* Display functions */
	struct drm_i915_display_funcs display;

	/* Register state */
	bool modeset_on_lid;
	u8 saveLBB;
	u32 saveDSPACNTR;
	u32 saveDSPBCNTR;
	u32 saveDSPARB;
	u32 saveHWS;
	u32 savePIPEACONF;
	u32 savePIPEBCONF;
	u32 savePIPEASRC;
	u32 savePIPEBSRC;
	u32 saveFPA0;
	u32 saveFPA1;
	u32 saveDPLL_A;
	u32 saveDPLL_A_MD;
	u32 saveHTOTAL_A;
	u32 saveHBLANK_A;
	u32 saveHSYNC_A;
	u32 saveVTOTAL_A;
	u32 saveVBLANK_A;
	u32 saveVSYNC_A;
	u32 saveBCLRPAT_A;
	u32 saveTRANSACONF;
	u32 saveTRANS_HTOTAL_A;
	u32 saveTRANS_HBLANK_A;
	u32 saveTRANS_HSYNC_A;
	u32 saveTRANS_VTOTAL_A;
	u32 saveTRANS_VBLANK_A;
	u32 saveTRANS_VSYNC_A;
	u32 savePIPEASTAT;
	u32 saveDSPASTRIDE;
	u32 saveDSPASIZE;
	u32 saveDSPAPOS;
	u32 saveDSPAADDR;
	u32 saveDSPASURF;
	u32 saveDSPATILEOFF;
	u32 savePFIT_PGM_RATIOS;
	u32 saveBLC_HIST_CTL;
	u32 saveBLC_PWM_CTL;
	u32 saveBLC_PWM_CTL2;
	u32 saveBLC_CPU_PWM_CTL;
	u32 saveBLC_CPU_PWM_CTL2;
	u32 saveFPB0;
	u32 saveFPB1;
	u32 saveDPLL_B;
	u32 saveDPLL_B_MD;
	u32 saveHTOTAL_B;
	u32 saveHBLANK_B;
	u32 saveHSYNC_B;
	u32 saveVTOTAL_B;
	u32 saveVBLANK_B;
	u32 saveVSYNC_B;
	u32 saveBCLRPAT_B;
	u32 saveTRANSBCONF;
	u32 saveTRANS_HTOTAL_B;
	u32 saveTRANS_HBLANK_B;
	u32 saveTRANS_HSYNC_B;
	u32 saveTRANS_VTOTAL_B;
	u32 saveTRANS_VBLANK_B;
	u32 saveTRANS_VSYNC_B;
	u32 savePIPEBSTAT;
	u32 saveDSPBSTRIDE;
	u32 saveDSPBSIZE;
	u32 saveDSPBPOS;
	u32 saveDSPBADDR;
	u32 saveDSPBSURF;
	u32 saveDSPBTILEOFF;
	u32 saveVGA0;
	u32 saveVGA1;
	u32 saveVGA_PD;
	u32 saveVGACNTRL;
	u32 saveADPA;
	u32 saveLVDS;
	u32 savePP_ON_DELAYS;
	u32 savePP_OFF_DELAYS;
	u32 saveDVOA;
	u32 saveDVOB;
	u32 saveDVOC;
	u32 savePP_ON;
	u32 savePP_OFF;
	u32 savePP_CONTROL;
	u32 savePP_DIVISOR;
	u32 savePFIT_CONTROL;
	u32 save_palette_a[256];
	u32 save_palette_b[256];
	u32 saveDPFC_CB_BASE;
	u32 saveFBC_CFB_BASE;
	u32 saveFBC_LL_BASE;
	u32 saveFBC_CONTROL;
	u32 saveFBC_CONTROL2;
	u32 saveIER;
	u32 saveIIR;
	u32 saveIMR;
	u32 saveDEIER;
	u32 saveDEIMR;
	u32 saveGTIER;
	u32 saveGTIMR;
	u32 saveFDI_RXA_IMR;
	u32 saveFDI_RXB_IMR;
	u32 saveCACHE_MODE_0;
	u32 saveMI_ARB_STATE;
	u32 saveSWF0[16];
	u32 saveSWF1[16];
	u32 saveSWF2[3];
	u8 saveMSR;
	u8 saveSR[8];
	u8 saveGR[25];
	u8 saveAR_INDEX;
	u8 saveAR[21];
	u8 saveDACMASK;
	u8 saveCR[37];
	uint64_t saveFENCE[16];
	u32 saveCURACNTR;
	u32 saveCURAPOS;
	u32 saveCURABASE;
	u32 saveCURBCNTR;
	u32 saveCURBPOS;
	u32 saveCURBBASE;
	u32 saveCURSIZE;
	u32 saveDP_B;
	u32 saveDP_C;
	u32 saveDP_D;
	u32 savePIPEA_GMCH_DATA_M;
	u32 savePIPEB_GMCH_DATA_M;
	u32 savePIPEA_GMCH_DATA_N;
	u32 savePIPEB_GMCH_DATA_N;
	u32 savePIPEA_DP_LINK_M;
	u32 savePIPEB_DP_LINK_M;
	u32 savePIPEA_DP_LINK_N;
	u32 savePIPEB_DP_LINK_N;
	u32 saveFDI_RXA_CTL;
	u32 saveFDI_TXA_CTL;
	u32 saveFDI_RXB_CTL;
	u32 saveFDI_TXB_CTL;
	u32 savePFA_CTL_1;
	u32 savePFB_CTL_1;
	u32 savePFA_WIN_SZ;
	u32 savePFB_WIN_SZ;
	u32 savePFA_WIN_POS;
	u32 savePFB_WIN_POS;
	u32 savePCH_DREF_CONTROL;
	u32 saveDISP_ARB_CTL;
	u32 savePIPEA_DATA_M1;
	u32 savePIPEA_DATA_N1;
	u32 savePIPEA_LINK_M1;
	u32 savePIPEA_LINK_N1;
	u32 savePIPEB_DATA_M1;
	u32 savePIPEB_DATA_N1;
	u32 savePIPEB_LINK_M1;
	u32 savePIPEB_LINK_N1;

	struct {
		struct drm_mm gtt_space;

		struct io_mapping *gtt_mapping;
		int gtt_mtrr;

		/**
		 * Membership on list of all loaded devices, used to evict
		 * inactive buffers under memory pressure.
		 *
		 * Modifications should only be done whilst holding the
		 * shrink_list_lock spinlock.
		 */
		struct list_head shrink_list;

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
		spinlock_t active_list_lock;
		struct list_head active_list;

		/**
		 * List of objects which are not in the ringbuffer but which
		 * still have a write_domain which needs to be flushed before
		 * unbinding.
		 *
		 * last_rendering_seqno is 0 while an object is in this list.
		 *
		 * A reference is held on the buffer while on this list.
		 */
		struct list_head flushing_list;

		/**
		 * LRU list of objects which are not in the ringbuffer and
		 * are ready to unbind, but are still in the GTT.
		 *
		 * last_rendering_seqno is 0 while an object is in this list.
		 *
		 * A reference is not held on the buffer while on this list,
		 * as merely being GTT-bound shouldn't prevent its being
		 * freed, and we'll pull it off the list in the free path.
		 */
		struct list_head inactive_list;

		/** LRU list of objects with fence regs on them. */
		struct list_head fence_list;

		/**
		 * List of breadcrumbs associated with GPU requests currently
		 * outstanding.
		 */
		struct list_head request_list;

		/**
		 * We leave the user IRQ off as much as possible,
		 * but this means that requests will finish and never
		 * be retired once the system goes idle. Set a timer to
		 * fire periodically while the ring is running. When it
		 * fires, go retire requests.
		 */
		struct delayed_work retire_work;

		uint32_t next_gem_seqno;

		/**
		 * Waiting sequence number, if any
		 */
		uint32_t waiting_gem_seqno;

		/**
		 * Last seq seen at irq time
		 */
		uint32_t irq_gem_seqno;

		/**
		 * Flag if the X Server, and thus DRM, is not currently in
		 * control of the device.
		 *
		 * This is set between LeaveVT and EnterVT.  It needs to be
		 * replaced with a semaphore.  It also needs to be
		 * transitioned away from for kernel modesetting.
		 */
		int suspended;

		/**
		 * Flag if the hardware appears to be wedged.
		 *
		 * This is set when attempts to idle the device timeout.
		 * It prevents command submission from occuring and makes
		 * every pending request fail
		 */
		atomic_t wedged;

		/** Bit 6 swizzling required for X tiling */
		uint32_t bit_6_swizzle_x;
		/** Bit 6 swizzling required for Y tiling */
		uint32_t bit_6_swizzle_y;

		/* storage for physical objects */
		struct drm_i915_gem_phys_object *phys_objs[I915_MAX_PHYS_OBJECT];
	} mm;
	struct sdvo_device_mapping sdvo_mappings[2];
	/* indicate whether the LVDS_BORDER should be enabled or not */
	unsigned int lvds_border_bits;

	struct drm_crtc *plane_to_crtc_mapping[2];
	struct drm_crtc *pipe_to_crtc_mapping[2];
	wait_queue_head_t pending_flip_queue;

	/* Reclocking support */
	bool render_reclock_avail;
	bool lvds_downclock_avail;
	/* indicates the reduced downclock for LVDS*/
	int lvds_downclock;
	struct work_struct idle_work;
	struct timer_list idle_timer;
	bool busy;
	u16 orig_clock;
	int child_dev_num;
	struct child_device_config *child_dev;
	struct drm_connector *int_lvds_connector;
} drm_i915_private_t;

/** driver private structure attached to each drm_gem_object */
struct drm_i915_gem_object {
	struct drm_gem_object *obj;

	/** Current space allocated to this object in the GTT, if any. */
	struct drm_mm_node *gtt_space;

	/** This object's place on the active/flushing/inactive lists */
	struct list_head list;

	/** This object's place on the fenced object LRU */
	struct list_head fence_list;

	/**
	 * This is set if the object is on the active or flushing lists
	 * (has pending rendering), and is not set if it's on inactive (ready
	 * to be unbound).
	 */
	int active;

	/**
	 * This is set if the object has been written to since last bound
	 * to the GTT
	 */
	int dirty;

	/** AGP memory structure for our GTT binding. */
	DRM_AGP_MEM *agp_mem;

	struct page **pages;
	int pages_refcount;

	/**
	 * Current offset of the object in GTT space.
	 *
	 * This is the same as gtt_space->start
	 */
	uint32_t gtt_offset;

	/**
	 * Fake offset for use by mmap(2)
	 */
	uint64_t mmap_offset;

	/**
	 * Fence register bits (if any) for this object.  Will be set
	 * as needed when mapped into the GTT.
	 * Protected by dev->struct_mutex.
	 */
	int fence_reg;

	/** How many users have pinned this object in GTT space */
	int pin_count;

	/** Breadcrumb of last rendering to the buffer. */
	uint32_t last_rendering_seqno;

	/** Current tiling mode for the object. */
	uint32_t tiling_mode;
	uint32_t stride;

	/** Record of address bit 17 of each page at last unbind. */
	long *bit_17;

	/** AGP mapping type (AGP_USER_MEMORY or AGP_USER_CACHED_MEMORY */
	uint32_t agp_type;

	/**
	 * If present, while GEM_DOMAIN_CPU is in the read domain this array
	 * flags which individual pages are valid.
	 */
	uint8_t *page_cpu_valid;

	/** User space pin count and filp owning the pin */
	uint32_t user_pin_count;
	struct drm_file *pin_filp;

	/** for phy allocated objects */
	struct drm_i915_gem_phys_object *phys_obj;

	/**
	 * Used for checking the object doesn't appear more than once
	 * in an execbuffer object list.
	 */
	int in_execbuffer;

	/**
	 * Advice: are the backing pages purgeable?
	 */
	int madv;

	/**
	 * Number of crtcs where this object is currently the fb, but
	 * will be page flipped away on the next vblank.  When it
	 * reaches 0, dev_priv->pending_flip_queue will be woken up.
	 */
	atomic_t pending_flip;
};

/**
 * Request queue structure.
 *
 * The request queue allows us to note sequence numbers that have been emitted
 * and may be associated with active buffers to be retired.
 *
 * By keeping this list, we can avoid having to do questionable
 * sequence-number comparisons on buffer last_rendering_seqnos, and associate
 * an emission time with seqnos for tracking how far ahead of the GPU we are.
 */
struct drm_i915_gem_request {
	/** GEM sequence number associated with this request. */
	uint32_t seqno;

	/** Time at which this request was emitted, in jiffies. */
	unsigned long emitted_jiffies;

	/** global list entry for this request */
	struct list_head list;

	/** file_priv list entry for this request */
	struct list_head client_list;
};

struct drm_i915_file_private {
	struct {
		struct list_head request_list;
	} mm;
};

enum intel_chip_family {
	CHIP_I8XX = 0x01,
	CHIP_I9XX = 0x02,
	CHIP_I915 = 0x04,
	CHIP_I965 = 0x08,
};

extern struct drm_ioctl_desc i915_ioctls[];
extern int i915_max_ioctl;
extern unsigned int i915_fbpercrtc;
extern unsigned int i915_powersave;
extern unsigned int i915_lvds_downclock;

extern void i915_save_display(struct drm_device *dev);
extern void i915_restore_display(struct drm_device *dev);
extern int i915_master_create(struct drm_device *dev, struct drm_master *master);
extern void i915_master_destroy(struct drm_device *dev, struct drm_master *master);

				/* i915_dma.c */
extern void i915_kernel_lost_context(struct drm_device * dev);
extern int i915_driver_load(struct drm_device *, unsigned long flags);
extern int i915_driver_unload(struct drm_device *);
extern int i915_driver_open(struct drm_device *dev, struct drm_file *file_priv);
extern void i915_driver_lastclose(struct drm_device * dev);
extern void i915_driver_preclose(struct drm_device *dev,
				 struct drm_file *file_priv);
extern void i915_driver_postclose(struct drm_device *dev,
				  struct drm_file *file_priv);
extern int i915_driver_device_is_agp(struct drm_device * dev);
extern long i915_compat_ioctl(struct file *filp, unsigned int cmd,
			      unsigned long arg);
extern int i915_emit_box(struct drm_device *dev,
			 struct drm_clip_rect *boxes,
			 int i, int DR1, int DR4);
extern int i965_reset(struct drm_device *dev, u8 flags);

/* i915_irq.c */
void i915_hangcheck_elapsed(unsigned long data);
extern int i915_irq_emit(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int i915_irq_wait(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
void i915_user_irq_get(struct drm_device *dev);
void i915_trace_irq_get(struct drm_device *dev, u32 seqno);
void i915_user_irq_put(struct drm_device *dev);
extern void i915_enable_interrupt (struct drm_device *dev);

extern irqreturn_t i915_driver_irq_handler(DRM_IRQ_ARGS);
extern void i915_driver_irq_preinstall(struct drm_device * dev);
extern int i915_driver_irq_postinstall(struct drm_device *dev);
extern void i915_driver_irq_uninstall(struct drm_device * dev);
extern int i915_vblank_pipe_set(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
extern int i915_vblank_pipe_get(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
extern int i915_enable_vblank(struct drm_device *dev, int crtc);
extern void i915_disable_vblank(struct drm_device *dev, int crtc);
extern u32 i915_get_vblank_counter(struct drm_device *dev, int crtc);
extern u32 gm45_get_vblank_counter(struct drm_device *dev, int crtc);
extern int i915_vblank_swap(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
extern void i915_enable_irq(drm_i915_private_t *dev_priv, u32 mask);

void
i915_enable_pipestat(drm_i915_private_t *dev_priv, int pipe, u32 mask);

void
i915_disable_pipestat(drm_i915_private_t *dev_priv, int pipe, u32 mask);

void intel_enable_asle (struct drm_device *dev);


/* i915_mem.c */
extern int i915_mem_alloc(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
extern int i915_mem_free(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int i915_mem_init_heap(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
extern int i915_mem_destroy_heap(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
extern void i915_mem_takedown(struct mem_block **heap);
extern void i915_mem_release(struct drm_device * dev,
			     struct drm_file *file_priv, struct mem_block *heap);
/* i915_gem.c */
int i915_gem_init_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int i915_gem_create_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
int i915_gem_pread_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int i915_gem_pwrite_ioctl(struct drm_device *dev, void *data,
			  struct drm_file *file_priv);
int i915_gem_mmap_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int i915_gem_mmap_gtt_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int i915_gem_set_domain_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
int i915_gem_sw_finish_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file_priv);
int i915_gem_execbuffer(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int i915_gem_execbuffer2(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int i915_gem_pin_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file_priv);
int i915_gem_unpin_ioctl(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
int i915_gem_busy_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int i915_gem_throttle_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
int i915_gem_madvise_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
int i915_gem_entervt_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
int i915_gem_leavevt_ioctl(struct drm_device *dev, void *data,
			   struct drm_file *file_priv);
int i915_gem_set_tiling(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int i915_gem_get_tiling(struct drm_device *dev, void *data,
			struct drm_file *file_priv);
int i915_gem_get_aperture_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
void i915_gem_load(struct drm_device *dev);
int i915_gem_init_object(struct drm_gem_object *obj);
void i915_gem_free_object(struct drm_gem_object *obj);
int i915_gem_object_pin(struct drm_gem_object *obj, uint32_t alignment);
void i915_gem_object_unpin(struct drm_gem_object *obj);
int i915_gem_object_unbind(struct drm_gem_object *obj);
void i915_gem_release_mmap(struct drm_gem_object *obj);
void i915_gem_lastclose(struct drm_device *dev);
uint32_t i915_get_gem_seqno(struct drm_device *dev);
bool i915_seqno_passed(uint32_t seq1, uint32_t seq2);
int i915_gem_object_get_fence_reg(struct drm_gem_object *obj);
int i915_gem_object_put_fence_reg(struct drm_gem_object *obj);
void i915_gem_retire_requests(struct drm_device *dev);
void i915_gem_retire_work_handler(struct work_struct *work);
void i915_gem_clflush_object(struct drm_gem_object *obj);
int i915_gem_object_set_domain(struct drm_gem_object *obj,
			       uint32_t read_domains,
			       uint32_t write_domain);
int i915_gem_init_ringbuffer(struct drm_device *dev);
void i915_gem_cleanup_ringbuffer(struct drm_device *dev);
int i915_gem_do_init(struct drm_device *dev, unsigned long start,
		     unsigned long end);
int i915_gem_idle(struct drm_device *dev);
uint32_t i915_add_request(struct drm_device *dev, struct drm_file *file_priv,
			  uint32_t flush_domains);
int i915_do_wait_request(struct drm_device *dev, uint32_t seqno, int interruptible);
int i915_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
int i915_gem_object_set_to_gtt_domain(struct drm_gem_object *obj,
				      int write);
int i915_gem_object_set_to_display_plane(struct drm_gem_object *obj);
int i915_gem_attach_phys_object(struct drm_device *dev,
				struct drm_gem_object *obj, int id);
void i915_gem_detach_phys_object(struct drm_device *dev,
				 struct drm_gem_object *obj);
void i915_gem_free_all_phys_object(struct drm_device *dev);
int i915_gem_object_get_pages(struct drm_gem_object *obj);
void i915_gem_object_put_pages(struct drm_gem_object *obj);
void i915_gem_release(struct drm_device * dev, struct drm_file *file_priv);
void i915_gem_object_flush_write_domain(struct drm_gem_object *obj);

void i915_gem_shrinker_init(void);
void i915_gem_shrinker_exit(void);

/* i915_gem_tiling.c */
void i915_gem_detect_bit_6_swizzle(struct drm_device *dev);
void i915_gem_object_do_bit_17_swizzle(struct drm_gem_object *obj);
void i915_gem_object_save_bit_17_swizzle(struct drm_gem_object *obj);
bool i915_tiling_ok(struct drm_device *dev, int stride, int size,
		    int tiling_mode);
bool i915_obj_fenceable(struct drm_device *dev, struct drm_gem_object *obj);

/* i915_gem_debug.c */
void i915_gem_dump_object(struct drm_gem_object *obj, int len,
			  const char *where, uint32_t mark);
#if WATCH_INACTIVE
void i915_verify_inactive(struct drm_device *dev, char *file, int line);
#else
#define i915_verify_inactive(dev, file, line)
#endif
void i915_gem_object_check_coherency(struct drm_gem_object *obj, int handle);
void i915_gem_dump_object(struct drm_gem_object *obj, int len,
			  const char *where, uint32_t mark);
void i915_dump_lru(struct drm_device *dev, const char *where);

/* i915_debugfs.c */
int i915_debugfs_init(struct drm_minor *minor);
void i915_debugfs_cleanup(struct drm_minor *minor);

/* i915_suspend.c */
extern int i915_save_state(struct drm_device *dev);
extern int i915_restore_state(struct drm_device *dev);

/* i915_suspend.c */
extern int i915_save_state(struct drm_device *dev);
extern int i915_restore_state(struct drm_device *dev);

#ifdef CONFIG_ACPI
/* i915_opregion.c */
extern int intel_opregion_init(struct drm_device *dev, int resume);
extern void intel_opregion_free(struct drm_device *dev, int suspend);
extern void opregion_asle_intr(struct drm_device *dev);
extern void ironlake_opregion_gse_intr(struct drm_device *dev);
extern void opregion_enable_asle(struct drm_device *dev);
#else
static inline int intel_opregion_init(struct drm_device *dev, int resume) { return 0; }
static inline void intel_opregion_free(struct drm_device *dev, int suspend) { return; }
static inline void opregion_asle_intr(struct drm_device *dev) { return; }
static inline void ironlake_opregion_gse_intr(struct drm_device *dev) { return; }
static inline void opregion_enable_asle(struct drm_device *dev) { return; }
#endif

/* modesetting */
extern void intel_modeset_init(struct drm_device *dev);
extern void intel_modeset_cleanup(struct drm_device *dev);
extern int intel_modeset_vga_set_state(struct drm_device *dev, bool state);
extern void i8xx_disable_fbc(struct drm_device *dev);
extern void g4x_disable_fbc(struct drm_device *dev);

/**
 * Lock test for when it's just for synchronization of ring access.
 *
 * In that case, we don't need to do it when GEM is initialized as nobody else
 * has access to the ring.
 */
#define RING_LOCK_TEST_WITH_RETURN(dev, file_priv) do {			\
	if (((drm_i915_private_t *)dev->dev_private)->ring.ring_obj == NULL) \
		LOCK_TEST_WITH_RETURN(dev, file_priv);			\
} while (0)

#define I915_READ(reg)          readl(dev_priv->regs + (reg))
#define I915_WRITE(reg, val)     writel(val, dev_priv->regs + (reg))
#define I915_READ16(reg)	readw(dev_priv->regs + (reg))
#define I915_WRITE16(reg, val)	writel(val, dev_priv->regs + (reg))
#define I915_READ8(reg)		readb(dev_priv->regs + (reg))
#define I915_WRITE8(reg, val)	writeb(val, dev_priv->regs + (reg))
#define I915_WRITE64(reg, val)	writeq(val, dev_priv->regs + (reg))
#define I915_READ64(reg)	readq(dev_priv->regs + (reg))
#define POSTING_READ(reg)	(void)I915_READ(reg)

#define I915_VERBOSE 0

#define RING_LOCALS	volatile unsigned int *ring_virt__;

#define BEGIN_LP_RING(n) do {						\
	int bytes__ = 4*(n);						\
	if (I915_VERBOSE) DRM_DEBUG("BEGIN_LP_RING(%d)\n", (n));	\
	/* a wrap must occur between instructions so pad beforehand */	\
	if (unlikely (dev_priv->ring.tail + bytes__ > dev_priv->ring.Size)) \
		i915_wrap_ring(dev);					\
	if (unlikely (dev_priv->ring.space < bytes__))			\
		i915_wait_ring(dev, bytes__, __func__);			\
	ring_virt__ = (unsigned int *)					\
	        (dev_priv->ring.virtual_start + dev_priv->ring.tail);	\
	dev_priv->ring.tail += bytes__;					\
	dev_priv->ring.tail &= dev_priv->ring.Size - 1;			\
	dev_priv->ring.space -= bytes__;				\
} while (0)

#define OUT_RING(n) do {						\
	if (I915_VERBOSE) DRM_DEBUG("   OUT_RING %x\n", (int)(n));	\
	*ring_virt__++ = (n);						\
} while (0)

#define ADVANCE_LP_RING() do {						\
	if (I915_VERBOSE)						\
		DRM_DEBUG("ADVANCE_LP_RING %x\n", dev_priv->ring.tail);	\
	I915_WRITE(PRB0_TAIL, dev_priv->ring.tail);			\
} while(0)

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
#define READ_HWSP(dev_priv, reg)  (((volatile u32*)(dev_priv->hw_status_page))[reg])
#define READ_BREADCRUMB(dev_priv) READ_HWSP(dev_priv, I915_BREADCRUMB_INDEX)
#define I915_GEM_HWS_INDEX		0x20
#define I915_BREADCRUMB_INDEX		0x21

extern int i915_wrap_ring(struct drm_device * dev);
extern int i915_wait_ring(struct drm_device * dev, int n, const char *caller);

#define INTEL_INFO(dev)	(((struct drm_i915_private *) (dev)->dev_private)->info)

#define IS_I830(dev)		((dev)->pci_device == 0x3577)
#define IS_845G(dev)		((dev)->pci_device == 0x2562)
#define IS_I85X(dev)		((dev)->pci_device == 0x3582)
#define IS_I865G(dev)		((dev)->pci_device == 0x2572)
#define IS_I8XX(dev)		(INTEL_INFO(dev)->is_i8xx)
#define IS_I915G(dev)		(INTEL_INFO(dev)->is_i915g)
#define IS_I915GM(dev)		((dev)->pci_device == 0x2592)
#define IS_I945G(dev)		((dev)->pci_device == 0x2772)
#define IS_I945GM(dev)		(INTEL_INFO(dev)->is_i945gm)
#define IS_I965G(dev)		(INTEL_INFO(dev)->is_i965g)
#define IS_I965GM(dev)		(INTEL_INFO(dev)->is_i965gm)
#define IS_GM45(dev)		((dev)->pci_device == 0x2A42)
#define IS_G4X(dev)		(INTEL_INFO(dev)->is_g4x)
#define IS_PINEVIEW_G(dev)	((dev)->pci_device == 0xa001)
#define IS_PINEVIEW_M(dev)	((dev)->pci_device == 0xa011)
#define IS_PINEVIEW(dev)	(INTEL_INFO(dev)->is_pineview)
#define IS_G33(dev)		(INTEL_INFO(dev)->is_g33)
#define IS_IRONLAKE_D(dev)	((dev)->pci_device == 0x0042)
#define IS_IRONLAKE_M(dev)	((dev)->pci_device == 0x0046)
#define IS_IRONLAKE(dev)	(INTEL_INFO(dev)->is_ironlake)
#define IS_I9XX(dev)		(INTEL_INFO(dev)->is_i9xx)
#define IS_MOBILE(dev)		(INTEL_INFO(dev)->is_mobile)

#define I915_NEED_GFX_HWS(dev)	(INTEL_INFO(dev)->need_gfx_hws)

/* With the 945 and later, Y tiling got adjusted so that it was 32 128-byte
 * rows, which changed the alignment requirements and fence programming.
 */
#define HAS_128_BYTE_Y_TILING(dev) (IS_I9XX(dev) && !(IS_I915G(dev) || \
						      IS_I915GM(dev)))
#define SUPPORTS_DIGITAL_OUTPUTS(dev)	(IS_I9XX(dev) && !IS_PINEVIEW(dev))
#define SUPPORTS_INTEGRATED_HDMI(dev)	(IS_G4X(dev) || IS_IRONLAKE(dev))
#define SUPPORTS_INTEGRATED_DP(dev)	(IS_G4X(dev) || IS_IRONLAKE(dev))
#define SUPPORTS_EDP(dev)		(IS_IRONLAKE_M(dev))
#define SUPPORTS_TV(dev)		(IS_I9XX(dev) && IS_MOBILE(dev) && \
					!IS_IRONLAKE(dev) && !IS_PINEVIEW(dev))
#define I915_HAS_HOTPLUG(dev)		 (INTEL_INFO(dev)->has_hotplug)
/* dsparb controlled by hw only */
#define DSPARB_HWCONTROL(dev) (IS_G4X(dev) || IS_IRONLAKE(dev))

#define HAS_FW_BLC(dev) (IS_I9XX(dev) || IS_G4X(dev) || IS_IRONLAKE(dev))
#define HAS_PIPE_CXSR(dev) (INTEL_INFO(dev)->has_pipe_cxsr)
#define I915_HAS_FBC(dev) (INTEL_INFO(dev)->has_fbc)
#define I915_HAS_RC6(dev) (INTEL_INFO(dev)->has_rc6)

#define PRIMARY_RINGBUFFER_SIZE         (128*1024)

#endif
