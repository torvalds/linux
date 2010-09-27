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
#include "intel_ringbuffer.h"
#include <linux/io-mapping.h>
#include <linux/i2c.h>
#include <drm/intel-gtt.h>

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

#define I915_GEM_GPU_DOMAINS	(~(I915_GEM_DOMAIN_CPU | I915_GEM_DOMAIN_GTT))

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
#define WATCH_EXEC	0
#define WATCH_RELOC	0
#define WATCH_LISTS	0
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
	void *vbt;
};
#define OPREGION_SIZE            (8*1024)

struct intel_overlay;
struct intel_overlay_error_state;

struct drm_i915_master_private {
	drm_local_map_t *sarea;
	struct _drm_i915_sarea *sarea_priv;
};
#define I915_FENCE_REG_NONE -1

struct drm_i915_fence_reg {
	struct drm_gem_object *obj;
	struct list_head lru_list;
	bool gpu;
};

struct sdvo_device_mapping {
	u8 initialized;
	u8 dvo_port;
	u8 slave_addr;
	u8 dvo_wiring;
	u8 i2c_pin;
	u8 i2c_speed;
	u8 ddc_pin;
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
	u64 bbaddr;
	struct timeval time;
	struct drm_i915_error_object {
		int page_count;
		u32 gtt_offset;
		u32 *pages[0];
	} *ringbuffer, *batchbuffer[2];
	struct drm_i915_error_buffer {
		size_t size;
		u32 name;
		u32 seqno;
		u32 gtt_offset;
		u32 read_domains;
		u32 write_domain;
		u32 fence_reg;
		s32 pinned:2;
		u32 tiling:2;
		u32 dirty:1;
		u32 purgeable:1;
	} *active_bo;
	u32 active_bo_count;
	struct intel_overlay_error_state *overlay;
};

struct drm_i915_display_funcs {
	void (*dpms)(struct drm_crtc *crtc, int mode);
	bool (*fbc_enabled)(struct drm_device *dev);
	void (*enable_fbc)(struct drm_crtc *crtc, unsigned long interval);
	void (*disable_fbc)(struct drm_device *dev);
	int (*get_display_clock_speed)(struct drm_device *dev);
	int (*get_fifo_size)(struct drm_device *dev, int plane);
	void (*update_wm)(struct drm_device *dev, int planea_clock,
			  int planeb_clock, int sr_hdisplay, int sr_htotal,
			  int pixel_size);
	/* clock updates for mode set */
	/* cursor updates */
	/* render clock increase/decrease */
	/* display clock increase/decrease */
	/* pll clock increase/decrease */
	/* clock gating init */
};

struct intel_device_info {
	u8 gen;
	u8 is_mobile : 1;
	u8 is_i85x : 1;
	u8 is_i915g : 1;
	u8 is_i945gm : 1;
	u8 is_g33 : 1;
	u8 need_gfx_hws : 1;
	u8 is_g4x : 1;
	u8 is_pineview : 1;
	u8 is_broadwater : 1;
	u8 is_crestline : 1;
	u8 is_ironlake : 1;
	u8 has_fbc : 1;
	u8 has_rc6 : 1;
	u8 has_pipe_cxsr : 1;
	u8 has_hotplug : 1;
	u8 cursor_needs_physical : 1;
	u8 has_overlay : 1;
	u8 overlay_needs_physical : 1;
	u8 supports_tv : 1;
	u8 has_bsd_ring : 1;
};

enum no_fbc_reason {
	FBC_NO_OUTPUT, /* no outputs enabled to compress */
	FBC_STOLEN_TOO_SMALL, /* not enough space to hold compressed buffers */
	FBC_UNSUPPORTED_MODE, /* interlace or doublescanned mode */
	FBC_MODE_TOO_LARGE, /* mode too large for compression */
	FBC_BAD_PLANE, /* fbc not supported on plane */
	FBC_NOT_TILED, /* buffer not tiled */
	FBC_MULTIPLE_PIPES, /* more than one pipe active */
};

enum intel_pch {
	PCH_IBX,	/* Ibexpeak PCH */
	PCH_CPT,	/* Cougarpoint PCH */
};

#define QUIRK_PIPEA_FORCE (1<<0)

struct intel_fbdev;

typedef struct drm_i915_private {
	struct drm_device *dev;

	const struct intel_device_info *info;

	int has_gem;

	void __iomem *regs;

	struct intel_gmbus {
		struct i2c_adapter adapter;
		struct i2c_adapter *force_bit;
		u32 reg0;
	} *gmbus;

	struct pci_dev *bridge_dev;
	struct intel_ring_buffer render_ring;
	struct intel_ring_buffer bsd_ring;
	uint32_t next_seqno;

	drm_dma_handle_t *status_page_dmah;
	void *seqno_page;
	dma_addr_t dma_status_page;
	uint32_t counter;
	unsigned int seqno_gfx_addr;
	drm_local_map_t hws_map;
	struct drm_gem_object *seqno_obj;
	struct drm_gem_object *pwrctx;
	struct drm_gem_object *renderctx;

	struct resource mch_res;

	unsigned int cpp;
	int back_offset;
	int front_offset;
	int current_page;
	int page_flipping;
#define I915_DEBUG_READ (1<<0)
#define I915_DEBUG_WRITE (1<<1)
	unsigned long debug_flags;

	wait_queue_head_t irq_queue;
	atomic_t irq_received;
	/** Protects user_irq_refcount and irq_mask_reg */
	spinlock_t user_irq_lock;
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
	int num_pipe;

	/* For hangcheck timer */
#define DRM_I915_HANGCHECK_PERIOD 250 /* in ms */
	struct timer_list hangcheck_timer;
	int hangcheck_count;
	uint32_t last_acthd;
	uint32_t last_instdone;
	uint32_t last_instdone1;

	unsigned long cfb_size;
	unsigned long cfb_pitch;
	unsigned long cfb_offset;
	int cfb_fence;
	int cfb_plane;
	int cfb_y;

	int irq_enabled;

	struct intel_opregion opregion;

	/* overlay */
	struct intel_overlay *overlay;

	/* LVDS info */
	int backlight_level;  /* restore backlight to this value */
	struct drm_display_mode *panel_fixed_mode;
	struct drm_display_mode *lfp_lvds_vbt_mode; /* if any */
	struct drm_display_mode *sdvo_lvds_vbt_mode; /* if any */

	/* Feature bits from the VBIOS */
	unsigned int int_tv_support:1;
	unsigned int lvds_dither:1;
	unsigned int lvds_vbt:1;
	unsigned int int_crt_support:1;
	unsigned int lvds_use_ssc:1;
	int lvds_ssc_freq;

	struct {
		u8 rate:4;
		u8 lanes:4;
		u8 preemphasis:4;
		u8 vswing:4;

		u8 initialized:1;
		u8 support:1;
		u8 bpp:6;
	} edp;

	struct notifier_block lid_notifier;

	int crt_ddc_pin;
	struct drm_i915_fence_reg fence_regs[16]; /* assume 965 */
	int fence_reg_start; /* 4 if userland hasn't ioctl'd us yet */
	int num_fence_regs; /* 8 on pre-965, 16 otherwise */

	unsigned int fsb_freq, mem_freq, is_ddr3;

	spinlock_t error_lock;
	struct drm_i915_error_state *first_error;
	struct work_struct error_work;
	struct completion error_completion;
	struct workqueue_struct *wq;

	/* Display functions */
	struct drm_i915_display_funcs display;

	/* PCH chipset type */
	enum intel_pch pch_type;

	unsigned long quirks;

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
	u32 saveMCHBAR_RENDER_STANDBY;

	struct {
		/** Bridge to intel-gtt-ko */
		struct intel_gtt *gtt;
		/** Memory allocator for GTT stolen memory */
		struct drm_mm vram;
		/** Memory allocator for GTT */
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
		 * List of objects currently pending a GPU write flush.
		 *
		 * All elements on this list will belong to either the
		 * active_list or flushing_list, last_rendering_seqno can
		 * be used to differentiate between the two elements.
		 */
		struct list_head gpu_write_list;

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

		/**
		 * LRU list of objects which are not in the ringbuffer but
		 * are still pinned in the GTT.
		 */
		struct list_head pinned_list;

		/** LRU list of objects with fence regs on them. */
		struct list_head fence_list;

		/**
		 * List of objects currently pending being freed.
		 *
		 * These objects are no longer in use, but due to a signal
		 * we were prevented from freeing them at the appointed time.
		 */
		struct list_head deferred_free_list;

		/**
		 * We leave the user IRQ off as much as possible,
		 * but this means that requests will finish and never
		 * be retired once the system goes idle. Set a timer to
		 * fire periodically while the ring is running. When it
		 * fires, go retire requests.
		 */
		struct delayed_work retire_work;

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

		uint32_t flush_rings;
	} mm;
	struct sdvo_device_mapping sdvo_mappings[2];
	/* indicate whether the LVDS_BORDER should be enabled or not */
	unsigned int lvds_border_bits;
	/* Panel fitter placement and size for Ironlake+ */
	u32 pch_pf_pos, pch_pf_size;

	struct drm_crtc *plane_to_crtc_mapping[2];
	struct drm_crtc *pipe_to_crtc_mapping[2];
	wait_queue_head_t pending_flip_queue;
	bool flip_pending_is_done;

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

	bool mchbar_need_disable;

	u8 cur_delay;
	u8 min_delay;
	u8 max_delay;
	u8 fmax;
	u8 fstart;

 	u64 last_count1;
 	unsigned long last_time1;
 	u64 last_count2;
 	struct timespec last_time2;
 	unsigned long gfx_power;
 	int c_m;
 	int r_t;
 	u8 corr;
	spinlock_t *mchdev_lock;

	enum no_fbc_reason no_fbc_reason;

	struct drm_mm_node *compressed_fb;
	struct drm_mm_node *compressed_llb;

	/* list of fbdev register on this device */
	struct intel_fbdev *fbdev;
} drm_i915_private_t;

/** driver private structure attached to each drm_gem_object */
struct drm_i915_gem_object {
	struct drm_gem_object base;

	/** Current space allocated to this object in the GTT, if any. */
	struct drm_mm_node *gtt_space;

	/** This object's place on the active/flushing/inactive lists */
	struct list_head list;
	/** This object's place on GPU write list */
	struct list_head gpu_write_list;
	/** This object's place on eviction list */
	struct list_head evict_list;

	/**
	 * This is set if the object is on the active or flushing lists
	 * (has pending rendering), and is not set if it's on inactive (ready
	 * to be unbound).
	 */
	unsigned int active : 1;

	/**
	 * This is set if the object has been written to since last bound
	 * to the GTT
	 */
	unsigned int dirty : 1;

	/**
	 * Fence register bits (if any) for this object.  Will be set
	 * as needed when mapped into the GTT.
	 * Protected by dev->struct_mutex.
	 *
	 * Size: 4 bits for 16 fences + sign (for FENCE_REG_NONE)
	 */
	signed int fence_reg : 5;

	/**
	 * Used for checking the object doesn't appear more than once
	 * in an execbuffer object list.
	 */
	unsigned int in_execbuffer : 1;

	/**
	 * Advice: are the backing pages purgeable?
	 */
	unsigned int madv : 2;

	/**
	 * Refcount for the pages array. With the current locking scheme, there
	 * are at most two concurrent users: Binding a bo to the gtt and
	 * pwrite/pread using physical addresses. So two bits for a maximum
	 * of two users are enough.
	 */
	unsigned int pages_refcount : 2;
#define DRM_I915_GEM_OBJECT_MAX_PAGES_REFCOUNT 0x3

	/**
	 * Current tiling mode for the object.
	 */
	unsigned int tiling_mode : 2;

	/** How many users have pinned this object in GTT space. The following
	 * users can each hold at most one reference: pwrite/pread, pin_ioctl
	 * (via user_pin_count), execbuffer (objects are not allowed multiple
	 * times for the same batchbuffer), and the framebuffer code. When
	 * switching/pageflipping, the framebuffer code has at most two buffers
	 * pinned per crtc.
	 *
	 * In the worst case this is 1 + 1 + 1 + 2*2 = 7. That would fit into 3
	 * bits with absolutely no headroom. So use 4 bits. */
	unsigned int pin_count : 4;
#define DRM_I915_GEM_OBJECT_MAX_PIN_COUNT 0xf

	/** AGP memory structure for our GTT binding. */
	DRM_AGP_MEM *agp_mem;

	struct page **pages;

	/**
	 * Current offset of the object in GTT space.
	 *
	 * This is the same as gtt_space->start
	 */
	uint32_t gtt_offset;

	/* Which ring is refering to is this object */
	struct intel_ring_buffer *ring;

	/**
	 * Fake offset for use by mmap(2)
	 */
	uint64_t mmap_offset;

	/** Breadcrumb of last rendering to the buffer. */
	uint32_t last_rendering_seqno;

	/** Current tiling stride for the object, if it's tiled. */
	uint32_t stride;

	/** Record of address bit 17 of each page at last unbind. */
	unsigned long *bit_17;

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
	 * Number of crtcs where this object is currently the fb, but
	 * will be page flipped away on the next vblank.  When it
	 * reaches 0, dev_priv->pending_flip_queue will be woken up.
	 */
	atomic_t pending_flip;
};

#define to_intel_bo(x) container_of(x, struct drm_i915_gem_object, base)

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
	/** On Which ring this request was generated */
	struct intel_ring_buffer *ring;

	/** GEM sequence number associated with this request. */
	uint32_t seqno;

	/** Time at which this request was emitted, in jiffies. */
	unsigned long emitted_jiffies;

	/** global list entry for this request */
	struct list_head list;

	struct drm_i915_file_private *file_priv;
	/** file_priv list entry for this request */
	struct list_head client_list;
};

struct drm_i915_file_private {
	struct {
		struct spinlock lock;
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

extern int i915_suspend(struct drm_device *dev, pm_message_t state);
extern int i915_resume(struct drm_device *dev);
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
extern int i915_reset(struct drm_device *dev, u8 flags);
extern unsigned long i915_chipset_val(struct drm_i915_private *dev_priv);
extern unsigned long i915_mch_val(struct drm_i915_private *dev_priv);
extern unsigned long i915_gfx_val(struct drm_i915_private *dev_priv);
extern void i915_update_gfx_val(struct drm_i915_private *dev_priv);


/* i915_irq.c */
void i915_hangcheck_elapsed(unsigned long data);
extern int i915_irq_emit(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
extern int i915_irq_wait(struct drm_device *dev, void *data,
			 struct drm_file *file_priv);
void i915_trace_irq_get(struct drm_device *dev, u32 seqno);
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
extern void i915_disable_irq(drm_i915_private_t *dev_priv, u32 mask);
extern void ironlake_enable_graphics_irq(drm_i915_private_t *dev_priv,
		u32 mask);
extern void ironlake_disable_graphics_irq(drm_i915_private_t *dev_priv,
		u32 mask);

void
i915_enable_pipestat(drm_i915_private_t *dev_priv, int pipe, u32 mask);

void
i915_disable_pipestat(drm_i915_private_t *dev_priv, int pipe, u32 mask);

void intel_enable_asle (struct drm_device *dev);

#ifdef CONFIG_DEBUG_FS
extern void i915_destroy_error_state(struct drm_device *dev);
#else
#define i915_destroy_error_state(x)
#endif


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
int i915_gem_check_is_wedged(struct drm_device *dev);
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
struct drm_gem_object * i915_gem_alloc_object(struct drm_device *dev,
					      size_t size);
void i915_gem_free_object(struct drm_gem_object *obj);
int i915_gem_object_pin(struct drm_gem_object *obj, uint32_t alignment);
void i915_gem_object_unpin(struct drm_gem_object *obj);
int i915_gem_object_unbind(struct drm_gem_object *obj);
void i915_gem_release_mmap(struct drm_gem_object *obj);
void i915_gem_lastclose(struct drm_device *dev);

/**
 * Returns true if seq1 is later than seq2.
 */
static inline bool
i915_seqno_passed(uint32_t seq1, uint32_t seq2)
{
	return (int32_t)(seq1 - seq2) >= 0;
}

int i915_gem_object_get_fence_reg(struct drm_gem_object *obj,
				  bool interruptible);
int i915_gem_object_put_fence_reg(struct drm_gem_object *obj,
				  bool interruptible);
void i915_gem_retire_requests(struct drm_device *dev);
void i915_gem_reset_lists(struct drm_device *dev);
void i915_gem_clflush_object(struct drm_gem_object *obj);
int i915_gem_object_set_domain(struct drm_gem_object *obj,
			       uint32_t read_domains,
			       uint32_t write_domain);
int i915_gem_init_ringbuffer(struct drm_device *dev);
void i915_gem_cleanup_ringbuffer(struct drm_device *dev);
int i915_gem_do_init(struct drm_device *dev, unsigned long start,
		     unsigned long end);
int i915_gpu_idle(struct drm_device *dev);
int i915_gem_idle(struct drm_device *dev);
uint32_t i915_add_request(struct drm_device *dev,
			  struct drm_file *file_priv,
			  struct drm_i915_gem_request *request,
			  struct intel_ring_buffer *ring);
int i915_do_wait_request(struct drm_device *dev,
			 uint32_t seqno,
			 bool interruptible,
			 struct intel_ring_buffer *ring);
int i915_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf);
int i915_gem_object_set_to_gtt_domain(struct drm_gem_object *obj,
				      int write);
int i915_gem_object_set_to_display_plane(struct drm_gem_object *obj,
					 bool pipelined);
int i915_gem_attach_phys_object(struct drm_device *dev,
				struct drm_gem_object *obj,
				int id,
				int align);
void i915_gem_detach_phys_object(struct drm_device *dev,
				 struct drm_gem_object *obj);
void i915_gem_free_all_phys_object(struct drm_device *dev);
void i915_gem_release(struct drm_device * dev, struct drm_file *file_priv);

void i915_gem_shrinker_init(void);
void i915_gem_shrinker_exit(void);

/* i915_gem_evict.c */
int i915_gem_evict_something(struct drm_device *dev, int min_size, unsigned alignment);
int i915_gem_evict_everything(struct drm_device *dev);
int i915_gem_evict_inactive(struct drm_device *dev);

/* i915_gem_tiling.c */
void i915_gem_detect_bit_6_swizzle(struct drm_device *dev);
void i915_gem_object_do_bit_17_swizzle(struct drm_gem_object *obj);
void i915_gem_object_save_bit_17_swizzle(struct drm_gem_object *obj);
bool i915_tiling_ok(struct drm_device *dev, int stride, int size,
		    int tiling_mode);
bool i915_gem_object_fence_offset_ok(struct drm_gem_object *obj,
				     int tiling_mode);

/* i915_gem_debug.c */
void i915_gem_dump_object(struct drm_gem_object *obj, int len,
			  const char *where, uint32_t mark);
#if WATCH_LISTS
int i915_verify_lists(struct drm_device *dev);
#else
#define i915_verify_lists(dev) 0
#endif
void i915_gem_object_check_coherency(struct drm_gem_object *obj, int handle);
void i915_gem_dump_object(struct drm_gem_object *obj, int len,
			  const char *where, uint32_t mark);

/* i915_debugfs.c */
int i915_debugfs_init(struct drm_minor *minor);
void i915_debugfs_cleanup(struct drm_minor *minor);

/* i915_suspend.c */
extern int i915_save_state(struct drm_device *dev);
extern int i915_restore_state(struct drm_device *dev);

/* i915_suspend.c */
extern int i915_save_state(struct drm_device *dev);
extern int i915_restore_state(struct drm_device *dev);

/* intel_i2c.c */
extern int intel_setup_gmbus(struct drm_device *dev);
extern void intel_teardown_gmbus(struct drm_device *dev);
extern void intel_gmbus_set_speed(struct i2c_adapter *adapter, int speed);
extern void intel_gmbus_force_bit(struct i2c_adapter *adapter, bool force_bit);
extern inline bool intel_gmbus_is_forced_bit(struct i2c_adapter *adapter)
{
	return container_of(adapter, struct intel_gmbus, adapter)->force_bit;
}
extern void intel_i2c_reset(struct drm_device *dev);

/* intel_opregion.c */
extern int intel_opregion_setup(struct drm_device *dev);
#ifdef CONFIG_ACPI
extern void intel_opregion_init(struct drm_device *dev);
extern void intel_opregion_fini(struct drm_device *dev);
extern void intel_opregion_asle_intr(struct drm_device *dev);
extern void intel_opregion_gse_intr(struct drm_device *dev);
extern void intel_opregion_enable_asle(struct drm_device *dev);
#else
static inline void intel_opregion_init(struct drm_device *dev) { return; }
static inline void intel_opregion_fini(struct drm_device *dev) { return; }
static inline void intel_opregion_asle_intr(struct drm_device *dev) { return; }
static inline void intel_opregion_gse_intr(struct drm_device *dev) { return; }
static inline void intel_opregion_enable_asle(struct drm_device *dev) { return; }
#endif

/* modesetting */
extern void intel_modeset_init(struct drm_device *dev);
extern void intel_modeset_cleanup(struct drm_device *dev);
extern int intel_modeset_vga_set_state(struct drm_device *dev, bool state);
extern void i8xx_disable_fbc(struct drm_device *dev);
extern void g4x_disable_fbc(struct drm_device *dev);
extern void ironlake_disable_fbc(struct drm_device *dev);
extern void intel_disable_fbc(struct drm_device *dev);
extern void intel_enable_fbc(struct drm_crtc *crtc, unsigned long interval);
extern bool intel_fbc_enabled(struct drm_device *dev);
extern bool ironlake_set_drps(struct drm_device *dev, u8 val);
extern void intel_detect_pch (struct drm_device *dev);
extern int intel_trans_dp_port_sel (struct drm_crtc *crtc);

/* overlay */
#ifdef CONFIG_DEBUG_FS
extern struct intel_overlay_error_state *intel_overlay_capture_error_state(struct drm_device *dev);
extern void intel_overlay_print_error_state(struct seq_file *m, struct intel_overlay_error_state *error);
#endif

/**
 * Lock test for when it's just for synchronization of ring access.
 *
 * In that case, we don't need to do it when GEM is initialized as nobody else
 * has access to the ring.
 */
#define RING_LOCK_TEST_WITH_RETURN(dev, file_priv) do {			\
	if (((drm_i915_private_t *)dev->dev_private)->render_ring.gem_object \
			== NULL)					\
		LOCK_TEST_WITH_RETURN(dev, file_priv);			\
} while (0)

static inline u32 i915_read(struct drm_i915_private *dev_priv, u32 reg)
{
	u32 val;

	val = readl(dev_priv->regs + reg);
	if (dev_priv->debug_flags & I915_DEBUG_READ)
		printk(KERN_ERR "read 0x%08x from 0x%08x\n", val, reg);
	return val;
}

static inline void i915_write(struct drm_i915_private *dev_priv, u32 reg,
			      u32 val)
{
	writel(val, dev_priv->regs + reg);
	if (dev_priv->debug_flags & I915_DEBUG_WRITE)
		printk(KERN_ERR "wrote 0x%08x to 0x%08x\n", val, reg);
}

#define I915_READ(reg)          i915_read(dev_priv, (reg))
#define I915_WRITE(reg, val)    i915_write(dev_priv, (reg), (val))
#define I915_READ16(reg)	readw(dev_priv->regs + (reg))
#define I915_WRITE16(reg, val)	writel(val, dev_priv->regs + (reg))
#define I915_READ8(reg)		readb(dev_priv->regs + (reg))
#define I915_WRITE8(reg, val)	writeb(val, dev_priv->regs + (reg))
#define I915_WRITE64(reg, val)	writeq(val, dev_priv->regs + (reg))
#define I915_READ64(reg)	readq(dev_priv->regs + (reg))
#define POSTING_READ(reg)	(void)I915_READ(reg)
#define POSTING_READ16(reg)	(void)I915_READ16(reg)

#define I915_DEBUG_ENABLE_IO() (dev_priv->debug_flags |= I915_DEBUG_READ | \
				I915_DEBUG_WRITE)
#define I915_DEBUG_DISABLE_IO() (dev_priv->debug_flags &= ~(I915_DEBUG_READ | \
							    I915_DEBUG_WRITE))

#define I915_VERBOSE 0

#define BEGIN_LP_RING(n)  do { \
	drm_i915_private_t *dev_priv__ = dev->dev_private;                \
	if (I915_VERBOSE)						\
		DRM_DEBUG("   BEGIN_LP_RING %x\n", (int)(n));		\
	intel_ring_begin(dev, &dev_priv__->render_ring, (n));		\
} while (0)


#define OUT_RING(x) do {						\
	drm_i915_private_t *dev_priv__ = dev->dev_private;		\
	if (I915_VERBOSE)						\
		DRM_DEBUG("   OUT_RING %x\n", (int)(x));		\
	intel_ring_emit(dev, &dev_priv__->render_ring, x);		\
} while (0)

#define ADVANCE_LP_RING() do {						\
	drm_i915_private_t *dev_priv__ = dev->dev_private;                \
	if (I915_VERBOSE)						\
		DRM_DEBUG("ADVANCE_LP_RING %x\n",			\
				dev_priv__->render_ring.tail);		\
	intel_ring_advance(dev, &dev_priv__->render_ring);		\
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
#define READ_HWSP(dev_priv, reg)  (((volatile u32 *)\
			(dev_priv->render_ring.status_page.page_addr))[reg])
#define READ_BREADCRUMB(dev_priv) READ_HWSP(dev_priv, I915_BREADCRUMB_INDEX)
#define I915_GEM_HWS_INDEX		0x20
#define I915_BREADCRUMB_INDEX		0x21

#define INTEL_INFO(dev)	(((struct drm_i915_private *) (dev)->dev_private)->info)

#define IS_I830(dev)		((dev)->pci_device == 0x3577)
#define IS_845G(dev)		((dev)->pci_device == 0x2562)
#define IS_I85X(dev)		(INTEL_INFO(dev)->is_i85x)
#define IS_I865G(dev)		((dev)->pci_device == 0x2572)
#define IS_I915G(dev)		(INTEL_INFO(dev)->is_i915g)
#define IS_I915GM(dev)		((dev)->pci_device == 0x2592)
#define IS_I945G(dev)		((dev)->pci_device == 0x2772)
#define IS_I945GM(dev)		(INTEL_INFO(dev)->is_i945gm)
#define IS_BROADWATER(dev)	(INTEL_INFO(dev)->is_broadwater)
#define IS_CRESTLINE(dev)	(INTEL_INFO(dev)->is_crestline)
#define IS_GM45(dev)		((dev)->pci_device == 0x2A42)
#define IS_G4X(dev)		(INTEL_INFO(dev)->is_g4x)
#define IS_PINEVIEW_G(dev)	((dev)->pci_device == 0xa001)
#define IS_PINEVIEW_M(dev)	((dev)->pci_device == 0xa011)
#define IS_PINEVIEW(dev)	(INTEL_INFO(dev)->is_pineview)
#define IS_G33(dev)		(INTEL_INFO(dev)->is_g33)
#define IS_IRONLAKE_D(dev)	((dev)->pci_device == 0x0042)
#define IS_IRONLAKE_M(dev)	((dev)->pci_device == 0x0046)
#define IS_IRONLAKE(dev)	(INTEL_INFO(dev)->is_ironlake)
#define IS_MOBILE(dev)		(INTEL_INFO(dev)->is_mobile)

#define IS_GEN2(dev)	(INTEL_INFO(dev)->gen == 2)
#define IS_GEN3(dev)	(INTEL_INFO(dev)->gen == 3)
#define IS_GEN4(dev)	(INTEL_INFO(dev)->gen == 4)
#define IS_GEN5(dev)	(INTEL_INFO(dev)->gen == 5)
#define IS_GEN6(dev)	(INTEL_INFO(dev)->gen == 6)

#define HAS_BSD(dev)            (INTEL_INFO(dev)->has_bsd_ring)
#define I915_NEED_GFX_HWS(dev)	(INTEL_INFO(dev)->need_gfx_hws)

#define HAS_OVERLAY(dev) 		(INTEL_INFO(dev)->has_overlay)
#define OVERLAY_NEEDS_PHYSICAL(dev)	(INTEL_INFO(dev)->overlay_needs_physical)

/* With the 945 and later, Y tiling got adjusted so that it was 32 128-byte
 * rows, which changed the alignment requirements and fence programming.
 */
#define HAS_128_BYTE_Y_TILING(dev) (!IS_GEN2(dev) && !(IS_I915G(dev) || \
						      IS_I915GM(dev)))
#define SUPPORTS_DIGITAL_OUTPUTS(dev)	(!IS_GEN2(dev) && !IS_PINEVIEW(dev))
#define SUPPORTS_INTEGRATED_HDMI(dev)	(IS_G4X(dev) || IS_IRONLAKE(dev))
#define SUPPORTS_INTEGRATED_DP(dev)	(IS_G4X(dev) || IS_IRONLAKE(dev))
#define SUPPORTS_EDP(dev)		(IS_IRONLAKE_M(dev))
#define SUPPORTS_TV(dev)		(INTEL_INFO(dev)->supports_tv)
#define I915_HAS_HOTPLUG(dev)		 (INTEL_INFO(dev)->has_hotplug)
/* dsparb controlled by hw only */
#define DSPARB_HWCONTROL(dev) (IS_G4X(dev) || IS_IRONLAKE(dev))

#define HAS_FW_BLC(dev) (INTEL_INFO(dev)->gen > 2)
#define HAS_PIPE_CXSR(dev) (INTEL_INFO(dev)->has_pipe_cxsr)
#define I915_HAS_FBC(dev) (INTEL_INFO(dev)->has_fbc)
#define I915_HAS_RC6(dev) (INTEL_INFO(dev)->has_rc6)

#define HAS_PCH_SPLIT(dev) (IS_IRONLAKE(dev) ||	\
			    IS_GEN6(dev))
#define HAS_PIPE_CONTROL(dev) (IS_IRONLAKE(dev) || IS_GEN6(dev))

#define INTEL_PCH_TYPE(dev) (((struct drm_i915_private *)(dev)->dev_private)->pch_type)
#define HAS_PCH_CPT(dev) (INTEL_PCH_TYPE(dev) == PCH_CPT)

#define PRIMARY_RINGBUFFER_SIZE         (128*1024)

#endif
