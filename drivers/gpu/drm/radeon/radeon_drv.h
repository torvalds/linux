/* radeon_drv.h -- Private header for radeon driver -*- linux-c -*-
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Kevin E. Martin <martin@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#ifndef __RADEON_DRV_H__
#define __RADEON_DRV_H__

#include <linux/firmware.h>
#include <linux/platform_device.h>

#include "radeon_family.h"

/* General customization:
 */

#define DRIVER_AUTHOR		"Gareth Hughes, Keith Whitwell, others."

#define DRIVER_NAME		"radeon"
#define DRIVER_DESC		"ATI Radeon"
#define DRIVER_DATE		"20080528"

/* Interface history:
 *
 * 1.1 - ??
 * 1.2 - Add vertex2 ioctl (keith)
 *     - Add stencil capability to clear ioctl (gareth, keith)
 *     - Increase MAX_TEXTURE_LEVELS (brian)
 * 1.3 - Add cmdbuf ioctl (keith)
 *     - Add support for new radeon packets (keith)
 *     - Add getparam ioctl (keith)
 *     - Add flip-buffers ioctl, deprecate fullscreen foo (keith).
 * 1.4 - Add scratch registers to get_param ioctl.
 * 1.5 - Add r200 packets to cmdbuf ioctl
 *     - Add r200 function to init ioctl
 *     - Add 'scalar2' instruction to cmdbuf
 * 1.6 - Add static GART memory manager
 *       Add irq handler (won't be turned on unless X server knows to)
 *       Add irq ioctls and irq_active getparam.
 *       Add wait command for cmdbuf ioctl
 *       Add GART offset query for getparam
 * 1.7 - Add support for cube map registers: R200_PP_CUBIC_FACES_[0..5]
 *       and R200_PP_CUBIC_OFFSET_F1_[0..5].
 *       Added packets R200_EMIT_PP_CUBIC_FACES_[0..5] and
 *       R200_EMIT_PP_CUBIC_OFFSETS_[0..5].  (brian)
 * 1.8 - Remove need to call cleanup ioctls on last client exit (keith)
 *       Add 'GET' queries for starting additional clients on different VT's.
 * 1.9 - Add DRM_IOCTL_RADEON_CP_RESUME ioctl.
 *       Add texture rectangle support for r100.
 * 1.10- Add SETPARAM ioctl; first parameter to set is FB_LOCATION, which
 *       clients use to tell the DRM where they think the framebuffer is
 *       located in the card's address space
 * 1.11- Add packet R200_EMIT_RB3D_BLENDCOLOR to support GL_EXT_blend_color
 *       and GL_EXT_blend_[func|equation]_separate on r200
 * 1.12- Add R300 CP microcode support - this just loads the CP on r300
 *       (No 3D support yet - just microcode loading).
 * 1.13- Add packet R200_EMIT_TCL_POINT_SPRITE_CNTL for ARB_point_parameters
 *     - Add hyperz support, add hyperz flags to clear ioctl.
 * 1.14- Add support for color tiling
 *     - Add R100/R200 surface allocation/free support
 * 1.15- Add support for texture micro tiling
 *     - Add support for r100 cube maps
 * 1.16- Add R200_EMIT_PP_TRI_PERF_CNTL packet to support brilinear
 *       texture filtering on r200
 * 1.17- Add initial support for R300 (3D).
 * 1.18- Add support for GL_ATI_fragment_shader, new packets
 *       R200_EMIT_PP_AFS_0/1, R200_EMIT_PP_TXCTLALL_0-5 (replaces
 *       R200_EMIT_PP_TXFILTER_0-5, 2 more regs) and R200_EMIT_ATF_TFACTOR
 *       (replaces R200_EMIT_TFACTOR_0 (8 consts instead of 6)
 * 1.19- Add support for gart table in FB memory and PCIE r300
 * 1.20- Add support for r300 texrect
 * 1.21- Add support for card type getparam
 * 1.22- Add support for texture cache flushes (R300_TX_CNTL)
 * 1.23- Add new radeon memory map work from benh
 * 1.24- Add general-purpose packet for manipulating scratch registers (r300)
 * 1.25- Add support for r200 vertex programs (R200_EMIT_VAP_PVS_CNTL,
 *       new packet type)
 * 1.26- Add support for variable size PCI(E) gart aperture
 * 1.27- Add support for IGP GART
 * 1.28- Add support for VBL on CRTC2
 * 1.29- R500 3D cmd buffer support
 * 1.30- Add support for occlusion queries
 * 1.31- Add support for num Z pipes from GET_PARAM
 */
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		31
#define DRIVER_PATCHLEVEL	0

enum radeon_cp_microcode_version {
	UCODE_R100,
	UCODE_R200,
	UCODE_R300,
};

typedef struct drm_radeon_freelist {
	unsigned int age;
	struct drm_buf *buf;
	struct drm_radeon_freelist *next;
	struct drm_radeon_freelist *prev;
} drm_radeon_freelist_t;

typedef struct drm_radeon_ring_buffer {
	u32 *start;
	u32 *end;
	int size;
	int size_l2qw;

	int rptr_update; /* Double Words */
	int rptr_update_l2qw; /* log2 Quad Words */

	int fetch_size; /* Double Words */
	int fetch_size_l2ow; /* log2 Oct Words */

	u32 tail;
	u32 tail_mask;
	int space;

	int high_mark;
} drm_radeon_ring_buffer_t;

typedef struct drm_radeon_depth_clear_t {
	u32 rb3d_cntl;
	u32 rb3d_zstencilcntl;
	u32 se_cntl;
} drm_radeon_depth_clear_t;

struct drm_radeon_driver_file_fields {
	int64_t radeon_fb_delta;
};

struct mem_block {
	struct mem_block *next;
	struct mem_block *prev;
	int start;
	int size;
	struct drm_file *file_priv; /* NULL: free, -1: heap, other: real files */
};

struct radeon_surface {
	int refcount;
	u32 lower;
	u32 upper;
	u32 flags;
};

struct radeon_virt_surface {
	int surface_index;
	u32 lower;
	u32 upper;
	u32 flags;
	struct drm_file *file_priv;
#define PCIGART_FILE_PRIV	((void *) -1L)
};

#define RADEON_FLUSH_EMITED	(1 << 0)
#define RADEON_PURGE_EMITED	(1 << 1)

struct drm_radeon_master_private {
	drm_local_map_t *sarea;
	drm_radeon_sarea_t *sarea_priv;
};

typedef struct drm_radeon_private {
	drm_radeon_ring_buffer_t ring;

	u32 fb_location;
	u32 fb_size;
	int new_memmap;

	int gart_size;
	u32 gart_vm_start;
	unsigned long gart_buffers_offset;

	int cp_mode;
	int cp_running;

	drm_radeon_freelist_t *head;
	drm_radeon_freelist_t *tail;
	int last_buf;
	int writeback_works;

	int usec_timeout;

	int microcode_version;

	struct {
		u32 boxes;
		int freelist_timeouts;
		int freelist_loops;
		int requested_bufs;
		int last_frame_reads;
		int last_clear_reads;
		int clears;
		int texture_uploads;
	} stats;

	int do_boxes;
	int page_flipping;

	u32 color_fmt;
	unsigned int front_offset;
	unsigned int front_pitch;
	unsigned int back_offset;
	unsigned int back_pitch;

	u32 depth_fmt;
	unsigned int depth_offset;
	unsigned int depth_pitch;

	u32 front_pitch_offset;
	u32 back_pitch_offset;
	u32 depth_pitch_offset;

	drm_radeon_depth_clear_t depth_clear;

	unsigned long ring_offset;
	unsigned long ring_rptr_offset;
	unsigned long buffers_offset;
	unsigned long gart_textures_offset;

	drm_local_map_t *sarea;
	drm_local_map_t *cp_ring;
	drm_local_map_t *ring_rptr;
	drm_local_map_t *gart_textures;

	struct mem_block *gart_heap;
	struct mem_block *fb_heap;

	/* SW interrupt */
	wait_queue_head_t swi_queue;
	atomic_t swi_emitted;
	int vblank_crtc;
	uint32_t irq_enable_reg;
	uint32_t r500_disp_irq_reg;

	struct radeon_surface surfaces[RADEON_MAX_SURFACES];
	struct radeon_virt_surface virt_surfaces[2 * RADEON_MAX_SURFACES];

	unsigned long pcigart_offset;
	unsigned int pcigart_offset_set;
	struct drm_ati_pcigart_info gart_info;

	u32 scratch_ages[5];

	/* starting from here on, data is preserved accross an open */
	uint32_t flags;		/* see radeon_chip_flags */
	resource_size_t fb_aper_offset;

	int num_gb_pipes;
	int num_z_pipes;
	int track_flush;
	drm_local_map_t *mmio;

	/* r6xx/r7xx pipe/shader config */
	int r600_max_pipes;
	int r600_max_tile_pipes;
	int r600_max_simds;
	int r600_max_backends;
	int r600_max_gprs;
	int r600_max_threads;
	int r600_max_stack_entries;
	int r600_max_hw_contexts;
	int r600_max_gs_threads;
	int r600_sx_max_export_size;
	int r600_sx_max_export_pos_size;
	int r600_sx_max_export_smx_size;
	int r600_sq_num_cf_insts;
	int r700_sx_num_of_sets;
	int r700_sc_prim_fifo_size;
	int r700_sc_hiz_tile_fifo_size;
	int r700_sc_earlyz_tile_fifo_fize;

	struct mutex cs_mutex;
	u32 cs_id_scnt;
	u32 cs_id_wcnt;
	/* r6xx/r7xx drm blit vertex buffer */
	struct drm_buf *blit_vb;

	/* firmware */
	const struct firmware *me_fw, *pfp_fw;
} drm_radeon_private_t;

typedef struct drm_radeon_buf_priv {
	u32 age;
} drm_radeon_buf_priv_t;

typedef struct drm_radeon_kcmd_buffer {
	int bufsz;
	char *buf;
	int nbox;
	struct drm_clip_rect __user *boxes;
} drm_radeon_kcmd_buffer_t;

extern int radeon_no_wb;
extern struct drm_ioctl_desc radeon_ioctls[];
extern int radeon_max_ioctl;

extern u32 radeon_get_ring_head(drm_radeon_private_t *dev_priv);
extern void radeon_set_ring_head(drm_radeon_private_t *dev_priv, u32 val);

#define GET_RING_HEAD(dev_priv)	radeon_get_ring_head(dev_priv)
#define SET_RING_HEAD(dev_priv, val) radeon_set_ring_head(dev_priv, val)

/* Check whether the given hardware address is inside the framebuffer or the
 * GART area.
 */
static __inline__ int radeon_check_offset(drm_radeon_private_t *dev_priv,
					  u64 off)
{
	u32 fb_start = dev_priv->fb_location;
	u32 fb_end = fb_start + dev_priv->fb_size - 1;
	u32 gart_start = dev_priv->gart_vm_start;
	u32 gart_end = gart_start + dev_priv->gart_size - 1;

	return ((off >= fb_start && off <= fb_end) ||
		(off >= gart_start && off <= gart_end));
}

/* radeon_state.c */
extern void radeon_cp_discard_buffer(struct drm_device *dev, struct drm_master *master, struct drm_buf *buf);

				/* radeon_cp.c */
extern int radeon_cp_init(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int radeon_cp_start(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int radeon_cp_stop(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int radeon_cp_reset(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int radeon_cp_idle(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int radeon_cp_resume(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int radeon_engine_reset(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int radeon_fullscreen(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int radeon_cp_buffers(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern u32 radeon_read_fb_location(drm_radeon_private_t *dev_priv);
extern void radeon_write_agp_location(drm_radeon_private_t *dev_priv, u32 agp_loc);
extern void radeon_write_agp_base(drm_radeon_private_t *dev_priv, u64 agp_base);
extern u32 RADEON_READ_MM(drm_radeon_private_t *dev_priv, int addr);

extern void radeon_freelist_reset(struct drm_device * dev);
extern struct drm_buf *radeon_freelist_get(struct drm_device * dev);

extern int radeon_wait_ring(drm_radeon_private_t * dev_priv, int n);

extern int radeon_do_cp_idle(drm_radeon_private_t * dev_priv);

extern int radeon_driver_preinit(struct drm_device *dev, unsigned long flags);
extern int radeon_presetup(struct drm_device *dev);
extern int radeon_driver_postcleanup(struct drm_device *dev);

extern int radeon_mem_alloc(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int radeon_mem_free(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int radeon_mem_init_heap(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern void radeon_mem_takedown(struct mem_block **heap);
extern void radeon_mem_release(struct drm_file *file_priv,
			       struct mem_block *heap);

extern void radeon_enable_bm(struct drm_radeon_private *dev_priv);
extern u32 radeon_read_ring_rptr(drm_radeon_private_t *dev_priv, u32 off);
extern void radeon_write_ring_rptr(drm_radeon_private_t *dev_priv, u32 off, u32 val);

				/* radeon_irq.c */
extern void radeon_irq_set_state(struct drm_device *dev, u32 mask, int state);
extern int radeon_irq_emit(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int radeon_irq_wait(struct drm_device *dev, void *data, struct drm_file *file_priv);

extern void radeon_do_release(struct drm_device * dev);
extern u32 radeon_get_vblank_counter(struct drm_device *dev, int crtc);
extern int radeon_enable_vblank(struct drm_device *dev, int crtc);
extern void radeon_disable_vblank(struct drm_device *dev, int crtc);
extern irqreturn_t radeon_driver_irq_handler(DRM_IRQ_ARGS);
extern void radeon_driver_irq_preinstall(struct drm_device * dev);
extern int radeon_driver_irq_postinstall(struct drm_device *dev);
extern void radeon_driver_irq_uninstall(struct drm_device * dev);
extern void radeon_enable_interrupt(struct drm_device *dev);
extern int radeon_vblank_crtc_get(struct drm_device *dev);
extern int radeon_vblank_crtc_set(struct drm_device *dev, int64_t value);

extern int radeon_driver_load(struct drm_device *dev, unsigned long flags);
extern int radeon_driver_unload(struct drm_device *dev);
extern int radeon_driver_firstopen(struct drm_device *dev);
extern void radeon_driver_preclose(struct drm_device *dev,
				   struct drm_file *file_priv);
extern void radeon_driver_postclose(struct drm_device *dev,
				    struct drm_file *file_priv);
extern void radeon_driver_lastclose(struct drm_device * dev);
extern int radeon_driver_open(struct drm_device *dev,
			      struct drm_file *file_priv);
extern long radeon_compat_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg);
extern long radeon_kms_compat_ioctl(struct file *filp, unsigned int cmd,
				    unsigned long arg);

extern int radeon_master_create(struct drm_device *dev, struct drm_master *master);
extern void radeon_master_destroy(struct drm_device *dev, struct drm_master *master);
extern void radeon_cp_dispatch_flip(struct drm_device *dev, struct drm_master *master);
/* r300_cmdbuf.c */
extern void r300_init_reg_flags(struct drm_device *dev);

extern int r300_do_cp_cmdbuf(struct drm_device *dev,
			     struct drm_file *file_priv,
			     drm_radeon_kcmd_buffer_t *cmdbuf);

/* r600_cp.c */
extern int r600_do_engine_reset(struct drm_device *dev);
extern int r600_do_cleanup_cp(struct drm_device *dev);
extern int r600_do_init_cp(struct drm_device *dev, drm_radeon_init_t *init,
			   struct drm_file *file_priv);
extern int r600_do_resume_cp(struct drm_device *dev, struct drm_file *file_priv);
extern int r600_do_cp_idle(drm_radeon_private_t *dev_priv);
extern void r600_do_cp_start(drm_radeon_private_t *dev_priv);
extern void r600_do_cp_reset(drm_radeon_private_t *dev_priv);
extern void r600_do_cp_stop(drm_radeon_private_t *dev_priv);
extern int r600_cp_dispatch_indirect(struct drm_device *dev,
				     struct drm_buf *buf, int start, int end);
extern int r600_page_table_init(struct drm_device *dev);
extern void r600_page_table_cleanup(struct drm_device *dev, struct drm_ati_pcigart_info *gart_info);
extern int r600_cs_legacy_ioctl(struct drm_device *dev, void *data, struct drm_file *fpriv);
extern void r600_cp_dispatch_swap(struct drm_device *dev, struct drm_file *file_priv);
extern int r600_cp_dispatch_texture(struct drm_device *dev,
				    struct drm_file *file_priv,
				    drm_radeon_texture_t *tex,
				    drm_radeon_tex_image_t *image);
/* r600_blit.c */
extern int r600_prepare_blit_copy(struct drm_device *dev, struct drm_file *file_priv);
extern void r600_done_blit_copy(struct drm_device *dev);
extern void r600_blit_copy(struct drm_device *dev,
			   uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
			   int size_bytes);
extern void r600_blit_swap(struct drm_device *dev,
			   uint64_t src_gpu_addr, uint64_t dst_gpu_addr,
			   int sx, int sy, int dx, int dy,
			   int w, int h, int src_pitch, int dst_pitch, int cpp);

/* Flags for stats.boxes
 */
#define RADEON_BOX_DMA_IDLE      0x1
#define RADEON_BOX_RING_FULL     0x2
#define RADEON_BOX_FLIP          0x4
#define RADEON_BOX_WAIT_IDLE     0x8
#define RADEON_BOX_TEXTURE_LOAD  0x10

/* Register definitions, register access macros and drmAddMap constants
 * for Radeon kernel driver.
 */
#define RADEON_MM_INDEX		        0x0000
#define RADEON_MM_DATA		        0x0004

#define RADEON_AGP_COMMAND		0x0f60
#define RADEON_AGP_COMMAND_PCI_CONFIG   0x0060	/* offset in PCI config */
#	define RADEON_AGP_ENABLE	(1<<8)
#define RADEON_AUX_SCISSOR_CNTL		0x26f0
#	define RADEON_EXCLUSIVE_SCISSOR_0	(1 << 24)
#	define RADEON_EXCLUSIVE_SCISSOR_1	(1 << 25)
#	define RADEON_EXCLUSIVE_SCISSOR_2	(1 << 26)
#	define RADEON_SCISSOR_0_ENABLE		(1 << 28)
#	define RADEON_SCISSOR_1_ENABLE		(1 << 29)
#	define RADEON_SCISSOR_2_ENABLE		(1 << 30)

/*
 * PCIE radeons (rv370/rv380, rv410, r423/r430/r480, r5xx)
 * don't have an explicit bus mastering disable bit.  It's handled
 * by the PCI D-states.  PMI_BM_DIS disables D-state bus master
 * handling, not bus mastering itself.
 */
#define RADEON_BUS_CNTL			0x0030
/* r1xx, r2xx, r300, r(v)350, r420/r481, rs400/rs480 */
#	define RADEON_BUS_MASTER_DIS		(1 << 6)
/* rs600/rs690/rs740 */
#	define RS600_BUS_MASTER_DIS		(1 << 14)
#	define RS600_MSI_REARM		        (1 << 20)
/* see RS400_MSI_REARM in AIC_CNTL for rs480 */

#define RADEON_BUS_CNTL1		0x0034
#	define RADEON_PMI_BM_DIS		(1 << 2)
#	define RADEON_PMI_INT_DIS		(1 << 3)

#define RV370_BUS_CNTL			0x004c
#	define RV370_PMI_BM_DIS		        (1 << 5)
#	define RV370_PMI_INT_DIS		(1 << 6)

#define RADEON_MSI_REARM_EN		0x0160
/* rv370/rv380, rv410, r423/r430/r480, r5xx */
#	define RV370_MSI_REARM_EN		(1 << 0)

#define RADEON_CLOCK_CNTL_DATA		0x000c
#	define RADEON_PLL_WR_EN			(1 << 7)
#define RADEON_CLOCK_CNTL_INDEX		0x0008
#define RADEON_CONFIG_APER_SIZE		0x0108
#define RADEON_CONFIG_MEMSIZE		0x00f8
#define RADEON_CRTC_OFFSET		0x0224
#define RADEON_CRTC_OFFSET_CNTL		0x0228
#	define RADEON_CRTC_TILE_EN		(1 << 15)
#	define RADEON_CRTC_OFFSET_FLIP_CNTL	(1 << 16)
#define RADEON_CRTC2_OFFSET		0x0324
#define RADEON_CRTC2_OFFSET_CNTL	0x0328

#define RADEON_PCIE_INDEX               0x0030
#define RADEON_PCIE_DATA                0x0034
#define RADEON_PCIE_TX_GART_CNTL	0x10
#	define RADEON_PCIE_TX_GART_EN		(1 << 0)
#	define RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_PASS_THRU (0 << 1)
#	define RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_CLAMP_LO  (1 << 1)
#	define RADEON_PCIE_TX_GART_UNMAPPED_ACCESS_DISCARD   (3 << 1)
#	define RADEON_PCIE_TX_GART_MODE_32_128_CACHE	(0 << 3)
#	define RADEON_PCIE_TX_GART_MODE_8_4_128_CACHE	(1 << 3)
#	define RADEON_PCIE_TX_GART_CHK_RW_VALID_EN      (1 << 5)
#	define RADEON_PCIE_TX_GART_INVALIDATE_TLB	(1 << 8)
#define RADEON_PCIE_TX_DISCARD_RD_ADDR_LO 0x11
#define RADEON_PCIE_TX_DISCARD_RD_ADDR_HI 0x12
#define RADEON_PCIE_TX_GART_BASE	0x13
#define RADEON_PCIE_TX_GART_START_LO	0x14
#define RADEON_PCIE_TX_GART_START_HI	0x15
#define RADEON_PCIE_TX_GART_END_LO	0x16
#define RADEON_PCIE_TX_GART_END_HI	0x17

#define RS480_NB_MC_INDEX               0x168
#	define RS480_NB_MC_IND_WR_EN	(1 << 8)
#define RS480_NB_MC_DATA                0x16c

#define RS690_MC_INDEX                  0x78
#   define RS690_MC_INDEX_MASK          0x1ff
#   define RS690_MC_INDEX_WR_EN         (1 << 9)
#   define RS690_MC_INDEX_WR_ACK        0x7f
#define RS690_MC_DATA                   0x7c

/* MC indirect registers */
#define RS480_MC_MISC_CNTL              0x18
#	define RS480_DISABLE_GTW	(1 << 1)
/* switch between MCIND GART and MM GART registers. 0 = mmgart, 1 = mcind gart */
#	define RS480_GART_INDEX_REG_EN	(1 << 12)
#	define RS690_BLOCK_GFX_D3_EN	(1 << 14)
#define RS480_K8_FB_LOCATION            0x1e
#define RS480_GART_FEATURE_ID           0x2b
#	define RS480_HANG_EN	        (1 << 11)
#	define RS480_TLB_ENABLE	        (1 << 18)
#	define RS480_P2P_ENABLE	        (1 << 19)
#	define RS480_GTW_LAC_EN	        (1 << 25)
#	define RS480_2LEVEL_GART	(0 << 30)
#	define RS480_1LEVEL_GART	(1 << 30)
#	define RS480_PDC_EN	        (1 << 31)
#define RS480_GART_BASE                 0x2c
#define RS480_GART_CACHE_CNTRL          0x2e
#	define RS480_GART_CACHE_INVALIDATE (1 << 0) /* wait for it to clear */
#define RS480_AGP_ADDRESS_SPACE_SIZE    0x38
#	define RS480_GART_EN	        (1 << 0)
#	define RS480_VA_SIZE_32MB	(0 << 1)
#	define RS480_VA_SIZE_64MB	(1 << 1)
#	define RS480_VA_SIZE_128MB	(2 << 1)
#	define RS480_VA_SIZE_256MB	(3 << 1)
#	define RS480_VA_SIZE_512MB	(4 << 1)
#	define RS480_VA_SIZE_1GB	(5 << 1)
#	define RS480_VA_SIZE_2GB	(6 << 1)
#define RS480_AGP_MODE_CNTL             0x39
#	define RS480_POST_GART_Q_SIZE	(1 << 18)
#	define RS480_NONGART_SNOOP	(1 << 19)
#	define RS480_AGP_RD_BUF_SIZE	(1 << 20)
#	define RS480_REQ_TYPE_SNOOP_SHIFT 22
#	define RS480_REQ_TYPE_SNOOP_MASK  0x3
#	define RS480_REQ_TYPE_SNOOP_DIS	(1 << 24)
#define RS480_MC_MISC_UMA_CNTL          0x5f
#define RS480_MC_MCLK_CNTL              0x7a
#define RS480_MC_UMA_DUALCH_CNTL        0x86

#define RS690_MC_FB_LOCATION            0x100
#define RS690_MC_AGP_LOCATION           0x101
#define RS690_MC_AGP_BASE               0x102
#define RS690_MC_AGP_BASE_2             0x103

#define RS600_MC_INDEX                          0x70
#       define RS600_MC_ADDR_MASK               0xffff
#       define RS600_MC_IND_SEQ_RBS_0           (1 << 16)
#       define RS600_MC_IND_SEQ_RBS_1           (1 << 17)
#       define RS600_MC_IND_SEQ_RBS_2           (1 << 18)
#       define RS600_MC_IND_SEQ_RBS_3           (1 << 19)
#       define RS600_MC_IND_AIC_RBS             (1 << 20)
#       define RS600_MC_IND_CITF_ARB0           (1 << 21)
#       define RS600_MC_IND_CITF_ARB1           (1 << 22)
#       define RS600_MC_IND_WR_EN               (1 << 23)
#define RS600_MC_DATA                           0x74

#define RS600_MC_STATUS                         0x0
#       define RS600_MC_IDLE                    (1 << 1)
#define RS600_MC_FB_LOCATION                    0x4
#define RS600_MC_AGP_LOCATION                   0x5
#define RS600_AGP_BASE                          0x6
#define RS600_AGP_BASE_2                        0x7
#define RS600_MC_CNTL1                          0x9
#       define RS600_ENABLE_PAGE_TABLES         (1 << 26)
#define RS600_MC_PT0_CNTL                       0x100
#       define RS600_ENABLE_PT                  (1 << 0)
#       define RS600_EFFECTIVE_L2_CACHE_SIZE(x) ((x) << 15)
#       define RS600_EFFECTIVE_L2_QUEUE_SIZE(x) ((x) << 21)
#       define RS600_INVALIDATE_ALL_L1_TLBS     (1 << 28)
#       define RS600_INVALIDATE_L2_CACHE        (1 << 29)
#define RS600_MC_PT0_CONTEXT0_CNTL              0x102
#       define RS600_ENABLE_PAGE_TABLE          (1 << 0)
#       define RS600_PAGE_TABLE_TYPE_FLAT       (0 << 1)
#define RS600_MC_PT0_SYSTEM_APERTURE_LOW_ADDR   0x112
#define RS600_MC_PT0_SYSTEM_APERTURE_HIGH_ADDR  0x114
#define RS600_MC_PT0_CONTEXT0_DEFAULT_READ_ADDR 0x11c
#define RS600_MC_PT0_CONTEXT0_FLAT_BASE_ADDR    0x12c
#define RS600_MC_PT0_CONTEXT0_FLAT_START_ADDR   0x13c
#define RS600_MC_PT0_CONTEXT0_FLAT_END_ADDR     0x14c
#define RS600_MC_PT0_CLIENT0_CNTL               0x16c
#       define RS600_ENABLE_TRANSLATION_MODE_OVERRIDE       (1 << 0)
#       define RS600_TRANSLATION_MODE_OVERRIDE              (1 << 1)
#       define RS600_SYSTEM_ACCESS_MODE_MASK                (3 << 8)
#       define RS600_SYSTEM_ACCESS_MODE_PA_ONLY             (0 << 8)
#       define RS600_SYSTEM_ACCESS_MODE_USE_SYS_MAP         (1 << 8)
#       define RS600_SYSTEM_ACCESS_MODE_IN_SYS              (2 << 8)
#       define RS600_SYSTEM_ACCESS_MODE_NOT_IN_SYS          (3 << 8)
#       define RS600_SYSTEM_APERTURE_UNMAPPED_ACCESS_PASSTHROUGH        (0 << 10)
#       define RS600_SYSTEM_APERTURE_UNMAPPED_ACCESS_DEFAULT_PAGE       (1 << 10)
#       define RS600_EFFECTIVE_L1_CACHE_SIZE(x) ((x) << 11)
#       define RS600_ENABLE_FRAGMENT_PROCESSING (1 << 14)
#       define RS600_EFFECTIVE_L1_QUEUE_SIZE(x) ((x) << 15)
#       define RS600_INVALIDATE_L1_TLB          (1 << 20)

#define R520_MC_IND_INDEX 0x70
#define R520_MC_IND_WR_EN (1 << 24)
#define R520_MC_IND_DATA  0x74

#define RV515_MC_FB_LOCATION 0x01
#define RV515_MC_AGP_LOCATION 0x02
#define RV515_MC_AGP_BASE     0x03
#define RV515_MC_AGP_BASE_2   0x04

#define R520_MC_FB_LOCATION 0x04
#define R520_MC_AGP_LOCATION 0x05
#define R520_MC_AGP_BASE     0x06
#define R520_MC_AGP_BASE_2   0x07

#define RADEON_MPP_TB_CONFIG		0x01c0
#define RADEON_MEM_CNTL			0x0140
#define RADEON_MEM_SDRAM_MODE_REG	0x0158
#define RADEON_AGP_BASE_2		0x015c /* r200+ only */
#define RS480_AGP_BASE_2		0x0164
#define RADEON_AGP_BASE			0x0170

/* pipe config regs */
#define R400_GB_PIPE_SELECT             0x402c
#define RV530_GB_PIPE_SELECT2           0x4124
#define R500_DYN_SCLK_PWMEM_PIPE        0x000d /* PLL */
#define R300_GB_TILE_CONFIG             0x4018
#       define R300_ENABLE_TILING       (1 << 0)
#       define R300_PIPE_COUNT_RV350    (0 << 1)
#       define R300_PIPE_COUNT_R300     (3 << 1)
#       define R300_PIPE_COUNT_R420_3P  (6 << 1)
#       define R300_PIPE_COUNT_R420     (7 << 1)
#       define R300_TILE_SIZE_8         (0 << 4)
#       define R300_TILE_SIZE_16        (1 << 4)
#       define R300_TILE_SIZE_32        (2 << 4)
#       define R300_SUBPIXEL_1_12       (0 << 16)
#       define R300_SUBPIXEL_1_16       (1 << 16)
#define R300_DST_PIPE_CONFIG            0x170c
#       define R300_PIPE_AUTO_CONFIG    (1 << 31)
#define R300_RB2D_DSTCACHE_MODE         0x3428
#       define R300_DC_AUTOFLUSH_ENABLE (1 << 8)
#       define R300_DC_DC_DISABLE_IGNORE_PE (1 << 17)

#define RADEON_RB3D_COLOROFFSET		0x1c40
#define RADEON_RB3D_COLORPITCH		0x1c48

#define	RADEON_SRC_X_Y			0x1590

#define RADEON_DP_GUI_MASTER_CNTL	0x146c
#	define RADEON_GMC_SRC_PITCH_OFFSET_CNTL	(1 << 0)
#	define RADEON_GMC_DST_PITCH_OFFSET_CNTL	(1 << 1)
#	define RADEON_GMC_BRUSH_SOLID_COLOR	(13 << 4)
#	define RADEON_GMC_BRUSH_NONE		(15 << 4)
#	define RADEON_GMC_DST_16BPP		(4 << 8)
#	define RADEON_GMC_DST_24BPP		(5 << 8)
#	define RADEON_GMC_DST_32BPP		(6 << 8)
#	define RADEON_GMC_DST_DATATYPE_SHIFT	8
#	define RADEON_GMC_SRC_DATATYPE_COLOR	(3 << 12)
#	define RADEON_DP_SRC_SOURCE_MEMORY	(2 << 24)
#	define RADEON_DP_SRC_SOURCE_HOST_DATA	(3 << 24)
#	define RADEON_GMC_CLR_CMP_CNTL_DIS	(1 << 28)
#	define RADEON_GMC_WR_MSK_DIS		(1 << 30)
#	define RADEON_ROP3_S			0x00cc0000
#	define RADEON_ROP3_P			0x00f00000
#define RADEON_DP_WRITE_MASK		0x16cc
#define RADEON_SRC_PITCH_OFFSET		0x1428
#define RADEON_DST_PITCH_OFFSET		0x142c
#define RADEON_DST_PITCH_OFFSET_C	0x1c80
#	define RADEON_DST_TILE_LINEAR		(0 << 30)
#	define RADEON_DST_TILE_MACRO		(1 << 30)
#	define RADEON_DST_TILE_MICRO		(2 << 30)
#	define RADEON_DST_TILE_BOTH		(3 << 30)

#define RADEON_SCRATCH_REG0		0x15e0
#define RADEON_SCRATCH_REG1		0x15e4
#define RADEON_SCRATCH_REG2		0x15e8
#define RADEON_SCRATCH_REG3		0x15ec
#define RADEON_SCRATCH_REG4		0x15f0
#define RADEON_SCRATCH_REG5		0x15f4
#define RADEON_SCRATCH_UMSK		0x0770
#define RADEON_SCRATCH_ADDR		0x0774

#define RADEON_SCRATCHOFF( x )		(RADEON_SCRATCH_REG_OFFSET + 4*(x))

extern u32 radeon_get_scratch(drm_radeon_private_t *dev_priv, int index);

#define GET_SCRATCH(dev_priv, x) radeon_get_scratch(dev_priv, x)

#define R600_SCRATCH_REG0		0x8500
#define R600_SCRATCH_REG1		0x8504
#define R600_SCRATCH_REG2		0x8508
#define R600_SCRATCH_REG3		0x850c
#define R600_SCRATCH_REG4		0x8510
#define R600_SCRATCH_REG5		0x8514
#define R600_SCRATCH_REG6		0x8518
#define R600_SCRATCH_REG7		0x851c
#define R600_SCRATCH_UMSK		0x8540
#define R600_SCRATCH_ADDR		0x8544

#define R600_SCRATCHOFF(x)		(R600_SCRATCH_REG_OFFSET + 4*(x))

#define RADEON_GEN_INT_CNTL		0x0040
#	define RADEON_CRTC_VBLANK_MASK		(1 << 0)
#	define RADEON_CRTC2_VBLANK_MASK		(1 << 9)
#	define RADEON_GUI_IDLE_INT_ENABLE	(1 << 19)
#	define RADEON_SW_INT_ENABLE		(1 << 25)

#define RADEON_GEN_INT_STATUS		0x0044
#	define RADEON_CRTC_VBLANK_STAT		(1 << 0)
#	define RADEON_CRTC_VBLANK_STAT_ACK	(1 << 0)
#	define RADEON_CRTC2_VBLANK_STAT		(1 << 9)
#	define RADEON_CRTC2_VBLANK_STAT_ACK	(1 << 9)
#	define RADEON_GUI_IDLE_INT_TEST_ACK     (1 << 19)
#	define RADEON_SW_INT_TEST		(1 << 25)
#	define RADEON_SW_INT_TEST_ACK		(1 << 25)
#	define RADEON_SW_INT_FIRE		(1 << 26)
#       define R500_DISPLAY_INT_STATUS          (1 << 0)

#define RADEON_HOST_PATH_CNTL		0x0130
#	define RADEON_HDP_SOFT_RESET		(1 << 26)
#	define RADEON_HDP_WC_TIMEOUT_MASK	(7 << 28)
#	define RADEON_HDP_WC_TIMEOUT_28BCLK	(7 << 28)

#define RADEON_ISYNC_CNTL		0x1724
#	define RADEON_ISYNC_ANY2D_IDLE3D	(1 << 0)
#	define RADEON_ISYNC_ANY3D_IDLE2D	(1 << 1)
#	define RADEON_ISYNC_TRIG2D_IDLE3D	(1 << 2)
#	define RADEON_ISYNC_TRIG3D_IDLE2D	(1 << 3)
#	define RADEON_ISYNC_WAIT_IDLEGUI	(1 << 4)
#	define RADEON_ISYNC_CPSCRATCH_IDLEGUI	(1 << 5)

#define RADEON_RBBM_GUICNTL		0x172c
#	define RADEON_HOST_DATA_SWAP_NONE	(0 << 0)
#	define RADEON_HOST_DATA_SWAP_16BIT	(1 << 0)
#	define RADEON_HOST_DATA_SWAP_32BIT	(2 << 0)
#	define RADEON_HOST_DATA_SWAP_HDW	(3 << 0)

#define RADEON_MC_AGP_LOCATION		0x014c
#define RADEON_MC_FB_LOCATION		0x0148
#define RADEON_MCLK_CNTL		0x0012
#	define RADEON_FORCEON_MCLKA		(1 << 16)
#	define RADEON_FORCEON_MCLKB		(1 << 17)
#	define RADEON_FORCEON_YCLKA		(1 << 18)
#	define RADEON_FORCEON_YCLKB		(1 << 19)
#	define RADEON_FORCEON_MC		(1 << 20)
#	define RADEON_FORCEON_AIC		(1 << 21)

#define RADEON_PP_BORDER_COLOR_0	0x1d40
#define RADEON_PP_BORDER_COLOR_1	0x1d44
#define RADEON_PP_BORDER_COLOR_2	0x1d48
#define RADEON_PP_CNTL			0x1c38
#	define RADEON_SCISSOR_ENABLE		(1 <<  1)
#define RADEON_PP_LUM_MATRIX		0x1d00
#define RADEON_PP_MISC			0x1c14
#define RADEON_PP_ROT_MATRIX_0		0x1d58
#define RADEON_PP_TXFILTER_0		0x1c54
#define RADEON_PP_TXOFFSET_0		0x1c5c
#define RADEON_PP_TXFILTER_1		0x1c6c
#define RADEON_PP_TXFILTER_2		0x1c84

#define R300_RB2D_DSTCACHE_CTLSTAT	0x342c /* use R300_DSTCACHE_CTLSTAT */
#define R300_DSTCACHE_CTLSTAT		0x1714
#	define R300_RB2D_DC_FLUSH		(3 << 0)
#	define R300_RB2D_DC_FREE		(3 << 2)
#	define R300_RB2D_DC_FLUSH_ALL		0xf
#	define R300_RB2D_DC_BUSY		(1 << 31)
#define RADEON_RB3D_CNTL		0x1c3c
#	define RADEON_ALPHA_BLEND_ENABLE	(1 << 0)
#	define RADEON_PLANE_MASK_ENABLE		(1 << 1)
#	define RADEON_DITHER_ENABLE		(1 << 2)
#	define RADEON_ROUND_ENABLE		(1 << 3)
#	define RADEON_SCALE_DITHER_ENABLE	(1 << 4)
#	define RADEON_DITHER_INIT		(1 << 5)
#	define RADEON_ROP_ENABLE		(1 << 6)
#	define RADEON_STENCIL_ENABLE		(1 << 7)
#	define RADEON_Z_ENABLE			(1 << 8)
#	define RADEON_ZBLOCK16			(1 << 15)
#define RADEON_RB3D_DEPTHOFFSET		0x1c24
#define RADEON_RB3D_DEPTHCLEARVALUE	0x3230
#define RADEON_RB3D_DEPTHPITCH		0x1c28
#define RADEON_RB3D_PLANEMASK		0x1d84
#define RADEON_RB3D_STENCILREFMASK	0x1d7c
#define RADEON_RB3D_ZCACHE_MODE		0x3250
#define RADEON_RB3D_ZCACHE_CTLSTAT	0x3254
#	define RADEON_RB3D_ZC_FLUSH		(1 << 0)
#	define RADEON_RB3D_ZC_FREE		(1 << 2)
#	define RADEON_RB3D_ZC_FLUSH_ALL		0x5
#	define RADEON_RB3D_ZC_BUSY		(1 << 31)
#define R300_ZB_ZCACHE_CTLSTAT                  0x4f18
#	define R300_ZC_FLUSH		        (1 << 0)
#	define R300_ZC_FREE		        (1 << 1)
#	define R300_ZC_BUSY		        (1 << 31)
#define RADEON_RB3D_DSTCACHE_CTLSTAT	0x325c
#	define RADEON_RB3D_DC_FLUSH		(3 << 0)
#	define RADEON_RB3D_DC_FREE		(3 << 2)
#	define RADEON_RB3D_DC_FLUSH_ALL		0xf
#	define RADEON_RB3D_DC_BUSY		(1 << 31)
#define R300_RB3D_DSTCACHE_CTLSTAT              0x4e4c
#	define R300_RB3D_DC_FLUSH		(2 << 0)
#	define R300_RB3D_DC_FREE		(2 << 2)
#	define R300_RB3D_DC_FINISH		(1 << 4)
#define RADEON_RB3D_ZSTENCILCNTL	0x1c2c
#	define RADEON_Z_TEST_MASK		(7 << 4)
#	define RADEON_Z_TEST_ALWAYS		(7 << 4)
#	define RADEON_Z_HIERARCHY_ENABLE	(1 << 8)
#	define RADEON_STENCIL_TEST_ALWAYS	(7 << 12)
#	define RADEON_STENCIL_S_FAIL_REPLACE	(2 << 16)
#	define RADEON_STENCIL_ZPASS_REPLACE	(2 << 20)
#	define RADEON_STENCIL_ZFAIL_REPLACE	(2 << 24)
#	define RADEON_Z_COMPRESSION_ENABLE	(1 << 28)
#	define RADEON_FORCE_Z_DIRTY		(1 << 29)
#	define RADEON_Z_WRITE_ENABLE		(1 << 30)
#	define RADEON_Z_DECOMPRESSION_ENABLE	(1 << 31)
#define RADEON_RBBM_SOFT_RESET		0x00f0
#	define RADEON_SOFT_RESET_CP		(1 <<  0)
#	define RADEON_SOFT_RESET_HI		(1 <<  1)
#	define RADEON_SOFT_RESET_SE		(1 <<  2)
#	define RADEON_SOFT_RESET_RE		(1 <<  3)
#	define RADEON_SOFT_RESET_PP		(1 <<  4)
#	define RADEON_SOFT_RESET_E2		(1 <<  5)
#	define RADEON_SOFT_RESET_RB		(1 <<  6)
#	define RADEON_SOFT_RESET_HDP		(1 <<  7)
/*
 *   6:0  Available slots in the FIFO
 *   8    Host Interface active
 *   9    CP request active
 *   10   FIFO request active
 *   11   Host Interface retry active
 *   12   CP retry active
 *   13   FIFO retry active
 *   14   FIFO pipeline busy
 *   15   Event engine busy
 *   16   CP command stream busy
 *   17   2D engine busy
 *   18   2D portion of render backend busy
 *   20   3D setup engine busy
 *   26   GA engine busy
 *   27   CBA 2D engine busy
 *   31   2D engine busy or 3D engine busy or FIFO not empty or CP busy or
 *           command stream queue not empty or Ring Buffer not empty
 */
#define RADEON_RBBM_STATUS		0x0e40
/* Same as the previous RADEON_RBBM_STATUS; this is a mirror of that register.  */
/* #define RADEON_RBBM_STATUS		0x1740 */
/* bits 6:0 are dword slots available in the cmd fifo */
#	define RADEON_RBBM_FIFOCNT_MASK		0x007f
#	define RADEON_HIRQ_ON_RBB	(1 <<  8)
#	define RADEON_CPRQ_ON_RBB	(1 <<  9)
#	define RADEON_CFRQ_ON_RBB	(1 << 10)
#	define RADEON_HIRQ_IN_RTBUF	(1 << 11)
#	define RADEON_CPRQ_IN_RTBUF	(1 << 12)
#	define RADEON_CFRQ_IN_RTBUF	(1 << 13)
#	define RADEON_PIPE_BUSY		(1 << 14)
#	define RADEON_ENG_EV_BUSY	(1 << 15)
#	define RADEON_CP_CMDSTRM_BUSY	(1 << 16)
#	define RADEON_E2_BUSY		(1 << 17)
#	define RADEON_RB2D_BUSY		(1 << 18)
#	define RADEON_RB3D_BUSY		(1 << 19) /* not used on r300 */
#	define RADEON_VAP_BUSY		(1 << 20)
#	define RADEON_RE_BUSY		(1 << 21) /* not used on r300 */
#	define RADEON_TAM_BUSY		(1 << 22) /* not used on r300 */
#	define RADEON_TDM_BUSY		(1 << 23) /* not used on r300 */
#	define RADEON_PB_BUSY		(1 << 24) /* not used on r300 */
#	define RADEON_TIM_BUSY		(1 << 25) /* not used on r300 */
#	define RADEON_GA_BUSY		(1 << 26)
#	define RADEON_CBA2D_BUSY	(1 << 27)
#	define RADEON_RBBM_ACTIVE	(1 << 31)
#define RADEON_RE_LINE_PATTERN		0x1cd0
#define RADEON_RE_MISC			0x26c4
#define RADEON_RE_TOP_LEFT		0x26c0
#define RADEON_RE_WIDTH_HEIGHT		0x1c44
#define RADEON_RE_STIPPLE_ADDR		0x1cc8
#define RADEON_RE_STIPPLE_DATA		0x1ccc

#define RADEON_SCISSOR_TL_0		0x1cd8
#define RADEON_SCISSOR_BR_0		0x1cdc
#define RADEON_SCISSOR_TL_1		0x1ce0
#define RADEON_SCISSOR_BR_1		0x1ce4
#define RADEON_SCISSOR_TL_2		0x1ce8
#define RADEON_SCISSOR_BR_2		0x1cec
#define RADEON_SE_COORD_FMT		0x1c50
#define RADEON_SE_CNTL			0x1c4c
#	define RADEON_FFACE_CULL_CW		(0 << 0)
#	define RADEON_BFACE_SOLID		(3 << 1)
#	define RADEON_FFACE_SOLID		(3 << 3)
#	define RADEON_FLAT_SHADE_VTX_LAST	(3 << 6)
#	define RADEON_DIFFUSE_SHADE_FLAT	(1 << 8)
#	define RADEON_DIFFUSE_SHADE_GOURAUD	(2 << 8)
#	define RADEON_ALPHA_SHADE_FLAT		(1 << 10)
#	define RADEON_ALPHA_SHADE_GOURAUD	(2 << 10)
#	define RADEON_SPECULAR_SHADE_FLAT	(1 << 12)
#	define RADEON_SPECULAR_SHADE_GOURAUD	(2 << 12)
#	define RADEON_FOG_SHADE_FLAT		(1 << 14)
#	define RADEON_FOG_SHADE_GOURAUD		(2 << 14)
#	define RADEON_VPORT_XY_XFORM_ENABLE	(1 << 24)
#	define RADEON_VPORT_Z_XFORM_ENABLE	(1 << 25)
#	define RADEON_VTX_PIX_CENTER_OGL	(1 << 27)
#	define RADEON_ROUND_MODE_TRUNC		(0 << 28)
#	define RADEON_ROUND_PREC_8TH_PIX	(1 << 30)
#define RADEON_SE_CNTL_STATUS		0x2140
#define RADEON_SE_LINE_WIDTH		0x1db8
#define RADEON_SE_VPORT_XSCALE		0x1d98
#define RADEON_SE_ZBIAS_FACTOR		0x1db0
#define RADEON_SE_TCL_MATERIAL_EMMISSIVE_RED 0x2210
#define RADEON_SE_TCL_OUTPUT_VTX_FMT         0x2254
#define RADEON_SE_TCL_VECTOR_INDX_REG        0x2200
#       define RADEON_VEC_INDX_OCTWORD_STRIDE_SHIFT  16
#       define RADEON_VEC_INDX_DWORD_COUNT_SHIFT     28
#define RADEON_SE_TCL_VECTOR_DATA_REG       0x2204
#define RADEON_SE_TCL_SCALAR_INDX_REG       0x2208
#       define RADEON_SCAL_INDX_DWORD_STRIDE_SHIFT  16
#define RADEON_SE_TCL_SCALAR_DATA_REG       0x220C
#define RADEON_SURFACE_ACCESS_FLAGS	0x0bf8
#define RADEON_SURFACE_ACCESS_CLR	0x0bfc
#define RADEON_SURFACE_CNTL		0x0b00
#	define RADEON_SURF_TRANSLATION_DIS	(1 << 8)
#	define RADEON_NONSURF_AP0_SWP_MASK	(3 << 20)
#	define RADEON_NONSURF_AP0_SWP_LITTLE	(0 << 20)
#	define RADEON_NONSURF_AP0_SWP_BIG16	(1 << 20)
#	define RADEON_NONSURF_AP0_SWP_BIG32	(2 << 20)
#	define RADEON_NONSURF_AP1_SWP_MASK	(3 << 22)
#	define RADEON_NONSURF_AP1_SWP_LITTLE	(0 << 22)
#	define RADEON_NONSURF_AP1_SWP_BIG16	(1 << 22)
#	define RADEON_NONSURF_AP1_SWP_BIG32	(2 << 22)
#define RADEON_SURFACE0_INFO		0x0b0c
#	define RADEON_SURF_PITCHSEL_MASK	(0x1ff << 0)
#	define RADEON_SURF_TILE_MODE_MASK	(3 << 16)
#	define RADEON_SURF_TILE_MODE_MACRO	(0 << 16)
#	define RADEON_SURF_TILE_MODE_MICRO	(1 << 16)
#	define RADEON_SURF_TILE_MODE_32BIT_Z	(2 << 16)
#	define RADEON_SURF_TILE_MODE_16BIT_Z	(3 << 16)
#define RADEON_SURFACE0_LOWER_BOUND	0x0b04
#define RADEON_SURFACE0_UPPER_BOUND	0x0b08
#	define RADEON_SURF_ADDRESS_FIXED_MASK	(0x3ff << 0)
#define RADEON_SURFACE1_INFO		0x0b1c
#define RADEON_SURFACE1_LOWER_BOUND	0x0b14
#define RADEON_SURFACE1_UPPER_BOUND	0x0b18
#define RADEON_SURFACE2_INFO		0x0b2c
#define RADEON_SURFACE2_LOWER_BOUND	0x0b24
#define RADEON_SURFACE2_UPPER_BOUND	0x0b28
#define RADEON_SURFACE3_INFO		0x0b3c
#define RADEON_SURFACE3_LOWER_BOUND	0x0b34
#define RADEON_SURFACE3_UPPER_BOUND	0x0b38
#define RADEON_SURFACE4_INFO		0x0b4c
#define RADEON_SURFACE4_LOWER_BOUND	0x0b44
#define RADEON_SURFACE4_UPPER_BOUND	0x0b48
#define RADEON_SURFACE5_INFO		0x0b5c
#define RADEON_SURFACE5_LOWER_BOUND	0x0b54
#define RADEON_SURFACE5_UPPER_BOUND	0x0b58
#define RADEON_SURFACE6_INFO		0x0b6c
#define RADEON_SURFACE6_LOWER_BOUND	0x0b64
#define RADEON_SURFACE6_UPPER_BOUND	0x0b68
#define RADEON_SURFACE7_INFO		0x0b7c
#define RADEON_SURFACE7_LOWER_BOUND	0x0b74
#define RADEON_SURFACE7_UPPER_BOUND	0x0b78
#define RADEON_SW_SEMAPHORE		0x013c

#define RADEON_WAIT_UNTIL		0x1720
#	define RADEON_WAIT_CRTC_PFLIP		(1 << 0)
#	define RADEON_WAIT_2D_IDLE		(1 << 14)
#	define RADEON_WAIT_3D_IDLE		(1 << 15)
#	define RADEON_WAIT_2D_IDLECLEAN		(1 << 16)
#	define RADEON_WAIT_3D_IDLECLEAN		(1 << 17)
#	define RADEON_WAIT_HOST_IDLECLEAN	(1 << 18)

#define RADEON_RB3D_ZMASKOFFSET		0x3234
#define RADEON_RB3D_ZSTENCILCNTL	0x1c2c
#	define RADEON_DEPTH_FORMAT_16BIT_INT_Z	(0 << 0)
#	define RADEON_DEPTH_FORMAT_24BIT_INT_Z	(2 << 0)

/* CP registers */
#define RADEON_CP_ME_RAM_ADDR		0x07d4
#define RADEON_CP_ME_RAM_RADDR		0x07d8
#define RADEON_CP_ME_RAM_DATAH		0x07dc
#define RADEON_CP_ME_RAM_DATAL		0x07e0

#define RADEON_CP_RB_BASE		0x0700
#define RADEON_CP_RB_CNTL		0x0704
#	define RADEON_BUF_SWAP_32BIT		(2 << 16)
#	define RADEON_RB_NO_UPDATE		(1 << 27)
#	define RADEON_RB_RPTR_WR_ENA		(1 << 31)
#define RADEON_CP_RB_RPTR_ADDR		0x070c
#define RADEON_CP_RB_RPTR		0x0710
#define RADEON_CP_RB_WPTR		0x0714

#define RADEON_CP_RB_WPTR_DELAY		0x0718
#	define RADEON_PRE_WRITE_TIMER_SHIFT	0
#	define RADEON_PRE_WRITE_LIMIT_SHIFT	23

#define RADEON_CP_IB_BASE		0x0738

#define RADEON_CP_CSQ_CNTL		0x0740
#	define RADEON_CSQ_CNT_PRIMARY_MASK	(0xff << 0)
#	define RADEON_CSQ_PRIDIS_INDDIS		(0 << 28)
#	define RADEON_CSQ_PRIPIO_INDDIS		(1 << 28)
#	define RADEON_CSQ_PRIBM_INDDIS		(2 << 28)
#	define RADEON_CSQ_PRIPIO_INDBM		(3 << 28)
#	define RADEON_CSQ_PRIBM_INDBM		(4 << 28)
#	define RADEON_CSQ_PRIPIO_INDPIO		(15 << 28)

#define R300_CP_RESYNC_ADDR		0x0778
#define R300_CP_RESYNC_DATA		0x077c

#define RADEON_AIC_CNTL			0x01d0
#	define RADEON_PCIGART_TRANSLATE_EN	(1 << 0)
#	define RS400_MSI_REARM	                (1 << 3)
#define RADEON_AIC_STAT			0x01d4
#define RADEON_AIC_PT_BASE		0x01d8
#define RADEON_AIC_LO_ADDR		0x01dc
#define RADEON_AIC_HI_ADDR		0x01e0
#define RADEON_AIC_TLB_ADDR		0x01e4
#define RADEON_AIC_TLB_DATA		0x01e8

/* CP command packets */
#define RADEON_CP_PACKET0		0x00000000
#	define RADEON_ONE_REG_WR		(1 << 15)
#define RADEON_CP_PACKET1		0x40000000
#define RADEON_CP_PACKET2		0x80000000
#define RADEON_CP_PACKET3		0xC0000000
#       define RADEON_CP_NOP                    0x00001000
#       define RADEON_CP_NEXT_CHAR              0x00001900
#       define RADEON_CP_PLY_NEXTSCAN           0x00001D00
#       define RADEON_CP_SET_SCISSORS           0x00001E00
	     /* GEN_INDX_PRIM is unsupported starting with R300 */
#	define RADEON_3D_RNDR_GEN_INDX_PRIM	0x00002300
#	define RADEON_WAIT_FOR_IDLE		0x00002600
#	define RADEON_3D_DRAW_VBUF		0x00002800
#	define RADEON_3D_DRAW_IMMD		0x00002900
#	define RADEON_3D_DRAW_INDX		0x00002A00
#       define RADEON_CP_LOAD_PALETTE           0x00002C00
#	define RADEON_3D_LOAD_VBPNTR		0x00002F00
#	define RADEON_MPEG_IDCT_MACROBLOCK	0x00003000
#	define RADEON_MPEG_IDCT_MACROBLOCK_REV	0x00003100
#	define RADEON_3D_CLEAR_ZMASK		0x00003200
#	define RADEON_CP_INDX_BUFFER		0x00003300
#       define RADEON_CP_3D_DRAW_VBUF_2         0x00003400
#       define RADEON_CP_3D_DRAW_IMMD_2         0x00003500
#       define RADEON_CP_3D_DRAW_INDX_2         0x00003600
#	define RADEON_3D_CLEAR_HIZ		0x00003700
#       define RADEON_CP_3D_CLEAR_CMASK         0x00003802
#	define RADEON_CNTL_HOSTDATA_BLT		0x00009400
#	define RADEON_CNTL_PAINT_MULTI		0x00009A00
#	define RADEON_CNTL_BITBLT_MULTI		0x00009B00
#	define RADEON_CNTL_SET_SCISSORS		0xC0001E00

#       define R600_IT_INDIRECT_BUFFER_END      0x00001700
#       define R600_IT_SET_PREDICATION          0x00002000
#       define R600_IT_REG_RMW                  0x00002100
#       define R600_IT_COND_EXEC                0x00002200
#       define R600_IT_PRED_EXEC                0x00002300
#       define R600_IT_START_3D_CMDBUF          0x00002400
#       define R600_IT_DRAW_INDEX_2             0x00002700
#       define R600_IT_CONTEXT_CONTROL          0x00002800
#       define R600_IT_DRAW_INDEX_IMMD_BE       0x00002900
#       define R600_IT_INDEX_TYPE               0x00002A00
#       define R600_IT_DRAW_INDEX               0x00002B00
#       define R600_IT_DRAW_INDEX_AUTO          0x00002D00
#       define R600_IT_DRAW_INDEX_IMMD          0x00002E00
#       define R600_IT_NUM_INSTANCES            0x00002F00
#       define R600_IT_STRMOUT_BUFFER_UPDATE    0x00003400
#       define R600_IT_INDIRECT_BUFFER_MP       0x00003800
#       define R600_IT_MEM_SEMAPHORE            0x00003900
#       define R600_IT_MPEG_INDEX               0x00003A00
#       define R600_IT_WAIT_REG_MEM             0x00003C00
#       define R600_IT_MEM_WRITE                0x00003D00
#       define R600_IT_INDIRECT_BUFFER          0x00003200
#       define R600_IT_CP_INTERRUPT             0x00004000
#       define R600_IT_SURFACE_SYNC             0x00004300
#              define R600_CB0_DEST_BASE_ENA    (1 << 6)
#              define R600_TC_ACTION_ENA        (1 << 23)
#              define R600_VC_ACTION_ENA        (1 << 24)
#              define R600_CB_ACTION_ENA        (1 << 25)
#              define R600_DB_ACTION_ENA        (1 << 26)
#              define R600_SH_ACTION_ENA        (1 << 27)
#              define R600_SMX_ACTION_ENA       (1 << 28)
#       define R600_IT_ME_INITIALIZE            0x00004400
#	       define R600_ME_INITIALIZE_DEVICE_ID(x) ((x) << 16)
#       define R600_IT_COND_WRITE               0x00004500
#       define R600_IT_EVENT_WRITE              0x00004600
#       define R600_IT_EVENT_WRITE_EOP          0x00004700
#       define R600_IT_ONE_REG_WRITE            0x00005700
#       define R600_IT_SET_CONFIG_REG           0x00006800
#              define R600_SET_CONFIG_REG_OFFSET 0x00008000
#              define R600_SET_CONFIG_REG_END   0x0000ac00
#       define R600_IT_SET_CONTEXT_REG          0x00006900
#              define R600_SET_CONTEXT_REG_OFFSET 0x00028000
#              define R600_SET_CONTEXT_REG_END  0x00029000
#       define R600_IT_SET_ALU_CONST            0x00006A00
#              define R600_SET_ALU_CONST_OFFSET 0x00030000
#              define R600_SET_ALU_CONST_END    0x00032000
#       define R600_IT_SET_BOOL_CONST           0x00006B00
#              define R600_SET_BOOL_CONST_OFFSET 0x0003e380
#              define R600_SET_BOOL_CONST_END   0x00040000
#       define R600_IT_SET_LOOP_CONST           0x00006C00
#              define R600_SET_LOOP_CONST_OFFSET 0x0003e200
#              define R600_SET_LOOP_CONST_END   0x0003e380
#       define R600_IT_SET_RESOURCE             0x00006D00
#              define R600_SET_RESOURCE_OFFSET  0x00038000
#              define R600_SET_RESOURCE_END     0x0003c000
#              define R600_SQ_TEX_VTX_INVALID_TEXTURE  0x0
#              define R600_SQ_TEX_VTX_INVALID_BUFFER   0x1
#              define R600_SQ_TEX_VTX_VALID_TEXTURE    0x2
#              define R600_SQ_TEX_VTX_VALID_BUFFER     0x3
#       define R600_IT_SET_SAMPLER              0x00006E00
#              define R600_SET_SAMPLER_OFFSET   0x0003c000
#              define R600_SET_SAMPLER_END      0x0003cff0
#       define R600_IT_SET_CTL_CONST            0x00006F00
#              define R600_SET_CTL_CONST_OFFSET 0x0003cff0
#              define R600_SET_CTL_CONST_END    0x0003e200
#       define R600_IT_SURFACE_BASE_UPDATE      0x00007300

#define RADEON_CP_PACKET_MASK		0xC0000000
#define RADEON_CP_PACKET_COUNT_MASK	0x3fff0000
#define RADEON_CP_PACKET0_REG_MASK	0x000007ff
#define RADEON_CP_PACKET1_REG0_MASK	0x000007ff
#define RADEON_CP_PACKET1_REG1_MASK	0x003ff800

#define RADEON_VTX_Z_PRESENT			(1 << 31)
#define RADEON_VTX_PKCOLOR_PRESENT		(1 << 3)

#define RADEON_PRIM_TYPE_NONE			(0 << 0)
#define RADEON_PRIM_TYPE_POINT			(1 << 0)
#define RADEON_PRIM_TYPE_LINE			(2 << 0)
#define RADEON_PRIM_TYPE_LINE_STRIP		(3 << 0)
#define RADEON_PRIM_TYPE_TRI_LIST		(4 << 0)
#define RADEON_PRIM_TYPE_TRI_FAN		(5 << 0)
#define RADEON_PRIM_TYPE_TRI_STRIP		(6 << 0)
#define RADEON_PRIM_TYPE_TRI_TYPE2		(7 << 0)
#define RADEON_PRIM_TYPE_RECT_LIST		(8 << 0)
#define RADEON_PRIM_TYPE_3VRT_POINT_LIST	(9 << 0)
#define RADEON_PRIM_TYPE_3VRT_LINE_LIST		(10 << 0)
#define RADEON_PRIM_TYPE_MASK                   0xf
#define RADEON_PRIM_WALK_IND			(1 << 4)
#define RADEON_PRIM_WALK_LIST			(2 << 4)
#define RADEON_PRIM_WALK_RING			(3 << 4)
#define RADEON_COLOR_ORDER_BGRA			(0 << 6)
#define RADEON_COLOR_ORDER_RGBA			(1 << 6)
#define RADEON_MAOS_ENABLE			(1 << 7)
#define RADEON_VTX_FMT_R128_MODE		(0 << 8)
#define RADEON_VTX_FMT_RADEON_MODE		(1 << 8)
#define RADEON_NUM_VERTICES_SHIFT		16

#define RADEON_COLOR_FORMAT_CI8		2
#define RADEON_COLOR_FORMAT_ARGB1555	3
#define RADEON_COLOR_FORMAT_RGB565	4
#define RADEON_COLOR_FORMAT_ARGB8888	6
#define RADEON_COLOR_FORMAT_RGB332	7
#define RADEON_COLOR_FORMAT_RGB8	9
#define RADEON_COLOR_FORMAT_ARGB4444	15

#define RADEON_TXFORMAT_I8		0
#define RADEON_TXFORMAT_AI88		1
#define RADEON_TXFORMAT_RGB332		2
#define RADEON_TXFORMAT_ARGB1555	3
#define RADEON_TXFORMAT_RGB565		4
#define RADEON_TXFORMAT_ARGB4444	5
#define RADEON_TXFORMAT_ARGB8888	6
#define RADEON_TXFORMAT_RGBA8888	7
#define RADEON_TXFORMAT_Y8		8
#define RADEON_TXFORMAT_VYUY422         10
#define RADEON_TXFORMAT_YVYU422         11
#define RADEON_TXFORMAT_DXT1            12
#define RADEON_TXFORMAT_DXT23           14
#define RADEON_TXFORMAT_DXT45           15

#define R200_PP_TXCBLEND_0                0x2f00
#define R200_PP_TXCBLEND_1                0x2f10
#define R200_PP_TXCBLEND_2                0x2f20
#define R200_PP_TXCBLEND_3                0x2f30
#define R200_PP_TXCBLEND_4                0x2f40
#define R200_PP_TXCBLEND_5                0x2f50
#define R200_PP_TXCBLEND_6                0x2f60
#define R200_PP_TXCBLEND_7                0x2f70
#define R200_SE_TCL_LIGHT_MODEL_CTL_0     0x2268
#define R200_PP_TFACTOR_0                 0x2ee0
#define R200_SE_VTX_FMT_0                 0x2088
#define R200_SE_VAP_CNTL                  0x2080
#define R200_SE_TCL_MATRIX_SEL_0          0x2230
#define R200_SE_TCL_TEX_PROC_CTL_2        0x22a8
#define R200_SE_TCL_UCP_VERT_BLEND_CTL    0x22c0
#define R200_PP_TXFILTER_5                0x2ca0
#define R200_PP_TXFILTER_4                0x2c80
#define R200_PP_TXFILTER_3                0x2c60
#define R200_PP_TXFILTER_2                0x2c40
#define R200_PP_TXFILTER_1                0x2c20
#define R200_PP_TXFILTER_0                0x2c00
#define R200_PP_TXOFFSET_5                0x2d78
#define R200_PP_TXOFFSET_4                0x2d60
#define R200_PP_TXOFFSET_3                0x2d48
#define R200_PP_TXOFFSET_2                0x2d30
#define R200_PP_TXOFFSET_1                0x2d18
#define R200_PP_TXOFFSET_0                0x2d00

#define R200_PP_CUBIC_FACES_0             0x2c18
#define R200_PP_CUBIC_FACES_1             0x2c38
#define R200_PP_CUBIC_FACES_2             0x2c58
#define R200_PP_CUBIC_FACES_3             0x2c78
#define R200_PP_CUBIC_FACES_4             0x2c98
#define R200_PP_CUBIC_FACES_5             0x2cb8
#define R200_PP_CUBIC_OFFSET_F1_0         0x2d04
#define R200_PP_CUBIC_OFFSET_F2_0         0x2d08
#define R200_PP_CUBIC_OFFSET_F3_0         0x2d0c
#define R200_PP_CUBIC_OFFSET_F4_0         0x2d10
#define R200_PP_CUBIC_OFFSET_F5_0         0x2d14
#define R200_PP_CUBIC_OFFSET_F1_1         0x2d1c
#define R200_PP_CUBIC_OFFSET_F2_1         0x2d20
#define R200_PP_CUBIC_OFFSET_F3_1         0x2d24
#define R200_PP_CUBIC_OFFSET_F4_1         0x2d28
#define R200_PP_CUBIC_OFFSET_F5_1         0x2d2c
#define R200_PP_CUBIC_OFFSET_F1_2         0x2d34
#define R200_PP_CUBIC_OFFSET_F2_2         0x2d38
#define R200_PP_CUBIC_OFFSET_F3_2         0x2d3c
#define R200_PP_CUBIC_OFFSET_F4_2         0x2d40
#define R200_PP_CUBIC_OFFSET_F5_2         0x2d44
#define R200_PP_CUBIC_OFFSET_F1_3         0x2d4c
#define R200_PP_CUBIC_OFFSET_F2_3         0x2d50
#define R200_PP_CUBIC_OFFSET_F3_3         0x2d54
#define R200_PP_CUBIC_OFFSET_F4_3         0x2d58
#define R200_PP_CUBIC_OFFSET_F5_3         0x2d5c
#define R200_PP_CUBIC_OFFSET_F1_4         0x2d64
#define R200_PP_CUBIC_OFFSET_F2_4         0x2d68
#define R200_PP_CUBIC_OFFSET_F3_4         0x2d6c
#define R200_PP_CUBIC_OFFSET_F4_4         0x2d70
#define R200_PP_CUBIC_OFFSET_F5_4         0x2d74
#define R200_PP_CUBIC_OFFSET_F1_5         0x2d7c
#define R200_PP_CUBIC_OFFSET_F2_5         0x2d80
#define R200_PP_CUBIC_OFFSET_F3_5         0x2d84
#define R200_PP_CUBIC_OFFSET_F4_5         0x2d88
#define R200_PP_CUBIC_OFFSET_F5_5         0x2d8c

#define R200_RE_AUX_SCISSOR_CNTL          0x26f0
#define R200_SE_VTE_CNTL                  0x20b0
#define R200_SE_TCL_OUTPUT_VTX_COMP_SEL   0x2250
#define R200_PP_TAM_DEBUG3                0x2d9c
#define R200_PP_CNTL_X                    0x2cc4
#define R200_SE_VAP_CNTL_STATUS           0x2140
#define R200_RE_SCISSOR_TL_0              0x1cd8
#define R200_RE_SCISSOR_TL_1              0x1ce0
#define R200_RE_SCISSOR_TL_2              0x1ce8
#define R200_RB3D_DEPTHXY_OFFSET          0x1d60
#define R200_RE_AUX_SCISSOR_CNTL          0x26f0
#define R200_SE_VTX_STATE_CNTL            0x2180
#define R200_RE_POINTSIZE                 0x2648
#define R200_SE_TCL_INPUT_VTX_VECTOR_ADDR_0 0x2254

#define RADEON_PP_TEX_SIZE_0                0x1d04	/* NPOT */
#define RADEON_PP_TEX_SIZE_1                0x1d0c
#define RADEON_PP_TEX_SIZE_2                0x1d14

#define RADEON_PP_CUBIC_FACES_0             0x1d24
#define RADEON_PP_CUBIC_FACES_1             0x1d28
#define RADEON_PP_CUBIC_FACES_2             0x1d2c
#define RADEON_PP_CUBIC_OFFSET_T0_0         0x1dd0	/* bits [31:5] */
#define RADEON_PP_CUBIC_OFFSET_T1_0         0x1e00
#define RADEON_PP_CUBIC_OFFSET_T2_0         0x1e14

#define RADEON_SE_TCL_STATE_FLUSH           0x2284

#define SE_VAP_CNTL__TCL_ENA_MASK                          0x00000001
#define SE_VAP_CNTL__FORCE_W_TO_ONE_MASK                   0x00010000
#define SE_VAP_CNTL__VF_MAX_VTX_NUM__SHIFT                 0x00000012
#define SE_VTE_CNTL__VTX_XY_FMT_MASK                       0x00000100
#define SE_VTE_CNTL__VTX_Z_FMT_MASK                        0x00000200
#define SE_VTX_FMT_0__VTX_Z0_PRESENT_MASK                  0x00000001
#define SE_VTX_FMT_0__VTX_W0_PRESENT_MASK                  0x00000002
#define SE_VTX_FMT_0__VTX_COLOR_0_FMT__SHIFT               0x0000000b
#define R200_3D_DRAW_IMMD_2      0xC0003500
#define R200_SE_VTX_FMT_1                 0x208c
#define R200_RE_CNTL                      0x1c50

#define R200_RB3D_BLENDCOLOR              0x3218

#define R200_SE_TCL_POINT_SPRITE_CNTL     0x22c4

#define R200_PP_TRI_PERF 0x2cf8

#define R200_PP_AFS_0                     0x2f80
#define R200_PP_AFS_1                     0x2f00	/* same as txcblend_0 */

#define R200_VAP_PVS_CNTL_1               0x22D0

#define RADEON_CRTC_CRNT_FRAME 0x0214
#define RADEON_CRTC2_CRNT_FRAME 0x0314

#define R500_D1CRTC_STATUS 0x609c
#define R500_D2CRTC_STATUS 0x689c
#define R500_CRTC_V_BLANK (1<<0)

#define R500_D1CRTC_FRAME_COUNT 0x60a4
#define R500_D2CRTC_FRAME_COUNT 0x68a4

#define R500_D1MODE_V_COUNTER 0x6530
#define R500_D2MODE_V_COUNTER 0x6d30

#define R500_D1MODE_VBLANK_STATUS 0x6534
#define R500_D2MODE_VBLANK_STATUS 0x6d34
#define R500_VBLANK_OCCURED (1<<0)
#define R500_VBLANK_ACK     (1<<4)
#define R500_VBLANK_STAT    (1<<12)
#define R500_VBLANK_INT     (1<<16)

#define R500_DxMODE_INT_MASK 0x6540
#define R500_D1MODE_INT_MASK (1<<0)
#define R500_D2MODE_INT_MASK (1<<8)

#define R500_DISP_INTERRUPT_STATUS 0x7edc
#define R500_D1_VBLANK_INTERRUPT (1 << 4)
#define R500_D2_VBLANK_INTERRUPT (1 << 5)

/* R6xx/R7xx registers */
#define R600_MC_VM_FB_LOCATION                                 0x2180
#define R600_MC_VM_AGP_TOP                                     0x2184
#define R600_MC_VM_AGP_BOT                                     0x2188
#define R600_MC_VM_AGP_BASE                                    0x218c
#define R600_MC_VM_SYSTEM_APERTURE_LOW_ADDR                    0x2190
#define R600_MC_VM_SYSTEM_APERTURE_HIGH_ADDR                   0x2194
#define R600_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR                0x2198

#define R700_MC_VM_FB_LOCATION                                 0x2024
#define R700_MC_VM_AGP_TOP                                     0x2028
#define R700_MC_VM_AGP_BOT                                     0x202c
#define R700_MC_VM_AGP_BASE                                    0x2030
#define R700_MC_VM_SYSTEM_APERTURE_LOW_ADDR                    0x2034
#define R700_MC_VM_SYSTEM_APERTURE_HIGH_ADDR                   0x2038
#define R700_MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR                0x203c

#define R600_MCD_RD_A_CNTL                                     0x219c
#define R600_MCD_RD_B_CNTL                                     0x21a0

#define R600_MCD_WR_A_CNTL                                     0x21a4
#define R600_MCD_WR_B_CNTL                                     0x21a8

#define R600_MCD_RD_SYS_CNTL                                   0x2200
#define R600_MCD_WR_SYS_CNTL                                   0x2214

#define R600_MCD_RD_GFX_CNTL                                   0x21fc
#define R600_MCD_RD_HDP_CNTL                                   0x2204
#define R600_MCD_RD_PDMA_CNTL                                  0x2208
#define R600_MCD_RD_SEM_CNTL                                   0x220c
#define R600_MCD_WR_GFX_CNTL                                   0x2210
#define R600_MCD_WR_HDP_CNTL                                   0x2218
#define R600_MCD_WR_PDMA_CNTL                                  0x221c
#define R600_MCD_WR_SEM_CNTL                                   0x2220

#       define R600_MCD_L1_TLB                                 (1 << 0)
#       define R600_MCD_L1_FRAG_PROC                           (1 << 1)
#       define R600_MCD_L1_STRICT_ORDERING                     (1 << 2)

#       define R600_MCD_SYSTEM_ACCESS_MODE_MASK                (3 << 6)
#       define R600_MCD_SYSTEM_ACCESS_MODE_PA_ONLY             (0 << 6)
#       define R600_MCD_SYSTEM_ACCESS_MODE_USE_SYS_MAP         (1 << 6)
#       define R600_MCD_SYSTEM_ACCESS_MODE_IN_SYS              (2 << 6)
#       define R600_MCD_SYSTEM_ACCESS_MODE_NOT_IN_SYS          (3 << 6)

#       define R600_MCD_SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU    (0 << 8)
#       define R600_MCD_SYSTEM_APERTURE_UNMAPPED_ACCESS_DEFAULT_PAGE (1 << 8)

#       define R600_MCD_SEMAPHORE_MODE                         (1 << 10)
#       define R600_MCD_WAIT_L2_QUERY                          (1 << 11)
#       define R600_MCD_EFFECTIVE_L1_TLB_SIZE(x)               ((x) << 12)
#       define R600_MCD_EFFECTIVE_L1_QUEUE_SIZE(x)             ((x) << 15)

#define R700_MC_VM_MD_L1_TLB0_CNTL                             0x2654
#define R700_MC_VM_MD_L1_TLB1_CNTL                             0x2658
#define R700_MC_VM_MD_L1_TLB2_CNTL                             0x265c

#define R700_MC_VM_MB_L1_TLB0_CNTL                             0x2234
#define R700_MC_VM_MB_L1_TLB1_CNTL                             0x2238
#define R700_MC_VM_MB_L1_TLB2_CNTL                             0x223c
#define R700_MC_VM_MB_L1_TLB3_CNTL                             0x2240

#       define R700_ENABLE_L1_TLB                              (1 << 0)
#       define R700_ENABLE_L1_FRAGMENT_PROCESSING              (1 << 1)
#       define R700_SYSTEM_ACCESS_MODE_IN_SYS                  (2 << 3)
#       define R700_SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU  (0 << 5)
#       define R700_EFFECTIVE_L1_TLB_SIZE(x)                   ((x) << 15)
#       define R700_EFFECTIVE_L1_QUEUE_SIZE(x)                 ((x) << 18)

#define R700_MC_ARB_RAMCFG                                     0x2760
#       define R700_NOOFBANK_SHIFT                             0
#       define R700_NOOFBANK_MASK                              0x3
#       define R700_NOOFRANK_SHIFT                             2
#       define R700_NOOFRANK_MASK                              0x1
#       define R700_NOOFROWS_SHIFT                             3
#       define R700_NOOFROWS_MASK                              0x7
#       define R700_NOOFCOLS_SHIFT                             6
#       define R700_NOOFCOLS_MASK                              0x3
#       define R700_CHANSIZE_SHIFT                             8
#       define R700_CHANSIZE_MASK                              0x1
#       define R700_BURSTLENGTH_SHIFT                          9
#       define R700_BURSTLENGTH_MASK                           0x1
#define R600_RAMCFG                                            0x2408
#       define R600_NOOFBANK_SHIFT                             0
#       define R600_NOOFBANK_MASK                              0x1
#       define R600_NOOFRANK_SHIFT                             1
#       define R600_NOOFRANK_MASK                              0x1
#       define R600_NOOFROWS_SHIFT                             2
#       define R600_NOOFROWS_MASK                              0x7
#       define R600_NOOFCOLS_SHIFT                             5
#       define R600_NOOFCOLS_MASK                              0x3
#       define R600_CHANSIZE_SHIFT                             7
#       define R600_CHANSIZE_MASK                              0x1
#       define R600_BURSTLENGTH_SHIFT                          8
#       define R600_BURSTLENGTH_MASK                           0x1

#define R600_VM_L2_CNTL                                        0x1400
#       define R600_VM_L2_CACHE_EN                             (1 << 0)
#       define R600_VM_L2_FRAG_PROC                            (1 << 1)
#       define R600_VM_ENABLE_PTE_CACHE_LRU_W                  (1 << 9)
#       define R600_VM_L2_CNTL_QUEUE_SIZE(x)                   ((x) << 13)
#       define R700_VM_L2_CNTL_QUEUE_SIZE(x)                   ((x) << 14)

#define R600_VM_L2_CNTL2                                       0x1404
#       define R600_VM_L2_CNTL2_INVALIDATE_ALL_L1_TLBS         (1 << 0)
#       define R600_VM_L2_CNTL2_INVALIDATE_L2_CACHE            (1 << 1)
#define R600_VM_L2_CNTL3                                       0x1408
#       define R600_VM_L2_CNTL3_BANK_SELECT_0(x)               ((x) << 0)
#       define R600_VM_L2_CNTL3_BANK_SELECT_1(x)               ((x) << 5)
#       define R600_VM_L2_CNTL3_CACHE_UPDATE_MODE(x)           ((x) << 10)
#       define R700_VM_L2_CNTL3_BANK_SELECT(x)                 ((x) << 0)
#       define R700_VM_L2_CNTL3_CACHE_UPDATE_MODE(x)           ((x) << 6)

#define R600_VM_L2_STATUS                                      0x140c

#define R600_VM_CONTEXT0_CNTL                                  0x1410
#       define R600_VM_ENABLE_CONTEXT                          (1 << 0)
#       define R600_VM_PAGE_TABLE_DEPTH_FLAT                   (0 << 1)

#define R600_VM_CONTEXT0_CNTL2                                 0x1430
#define R600_VM_CONTEXT0_REQUEST_RESPONSE                      0x1470
#define R600_VM_CONTEXT0_INVALIDATION_LOW_ADDR                 0x1490
#define R600_VM_CONTEXT0_INVALIDATION_HIGH_ADDR                0x14b0
#define R600_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR                  0x1574
#define R600_VM_CONTEXT0_PAGE_TABLE_START_ADDR                 0x1594
#define R600_VM_CONTEXT0_PAGE_TABLE_END_ADDR                   0x15b4

#define R700_VM_CONTEXT0_PAGE_TABLE_BASE_ADDR                  0x153c
#define R700_VM_CONTEXT0_PAGE_TABLE_START_ADDR                 0x155c
#define R700_VM_CONTEXT0_PAGE_TABLE_END_ADDR                   0x157c

#define R600_HDP_HOST_PATH_CNTL                                0x2c00

#define R600_GRBM_CNTL                                         0x8000
#       define R600_GRBM_READ_TIMEOUT(x)                       ((x) << 0)

#define R600_GRBM_STATUS                                       0x8010
#       define R600_CMDFIFO_AVAIL_MASK                         0x1f
#       define R700_CMDFIFO_AVAIL_MASK                         0xf
#       define R600_GUI_ACTIVE                                 (1 << 31)
#define R600_GRBM_STATUS2                                      0x8014
#define R600_GRBM_SOFT_RESET                                   0x8020
#       define R600_SOFT_RESET_CP                              (1 << 0)
#define R600_WAIT_UNTIL		                               0x8040

#define R600_CP_SEM_WAIT_TIMER                                 0x85bc
#define R600_CP_ME_CNTL                                        0x86d8
#       define R600_CP_ME_HALT                                 (1 << 28)
#define R600_CP_QUEUE_THRESHOLDS                               0x8760
#       define R600_ROQ_IB1_START(x)                           ((x) << 0)
#       define R600_ROQ_IB2_START(x)                           ((x) << 8)
#define R600_CP_MEQ_THRESHOLDS                                 0x8764
#       define R700_STQ_SPLIT(x)                               ((x) << 0)
#       define R600_MEQ_END(x)                                 ((x) << 16)
#       define R600_ROQ_END(x)                                 ((x) << 24)
#define R600_CP_PERFMON_CNTL                                   0x87fc
#define R600_CP_RB_BASE                                        0xc100
#define R600_CP_RB_CNTL                                        0xc104
#       define R600_RB_BUFSZ(x)                                ((x) << 0)
#       define R600_RB_BLKSZ(x)                                ((x) << 8)
#       define R600_RB_NO_UPDATE                               (1 << 27)
#       define R600_RB_RPTR_WR_ENA                             (1 << 31)
#define R600_CP_RB_RPTR_WR                                     0xc108
#define R600_CP_RB_RPTR_ADDR                                   0xc10c
#define R600_CP_RB_RPTR_ADDR_HI                                0xc110
#define R600_CP_RB_WPTR                                        0xc114
#define R600_CP_RB_WPTR_ADDR                                   0xc118
#define R600_CP_RB_WPTR_ADDR_HI                                0xc11c
#define R600_CP_RB_RPTR                                        0x8700
#define R600_CP_RB_WPTR_DELAY                                  0x8704
#define R600_CP_PFP_UCODE_ADDR                                 0xc150
#define R600_CP_PFP_UCODE_DATA                                 0xc154
#define R600_CP_ME_RAM_RADDR                                   0xc158
#define R600_CP_ME_RAM_WADDR                                   0xc15c
#define R600_CP_ME_RAM_DATA                                    0xc160
#define R600_CP_DEBUG                                          0xc1fc

#define R600_PA_CL_ENHANCE                                     0x8a14
#       define R600_CLIP_VTX_REORDER_ENA                       (1 << 0)
#       define R600_NUM_CLIP_SEQ(x)                            ((x) << 1)
#define R600_PA_SC_LINE_STIPPLE_STATE                          0x8b10
#define R600_PA_SC_MULTI_CHIP_CNTL                             0x8b20
#define R700_PA_SC_FORCE_EOV_MAX_CNTS                          0x8b24
#       define R700_FORCE_EOV_MAX_CLK_CNT(x)                   ((x) << 0)
#       define R700_FORCE_EOV_MAX_REZ_CNT(x)                   ((x) << 16)
#define R600_PA_SC_AA_SAMPLE_LOCS_2S                           0x8b40
#define R600_PA_SC_AA_SAMPLE_LOCS_4S                           0x8b44
#define R600_PA_SC_AA_SAMPLE_LOCS_8S_WD0                       0x8b48
#define R600_PA_SC_AA_SAMPLE_LOCS_8S_WD1                       0x8b4c
#       define R600_S0_X(x)                                    ((x) << 0)
#       define R600_S0_Y(x)                                    ((x) << 4)
#       define R600_S1_X(x)                                    ((x) << 8)
#       define R600_S1_Y(x)                                    ((x) << 12)
#       define R600_S2_X(x)                                    ((x) << 16)
#       define R600_S2_Y(x)                                    ((x) << 20)
#       define R600_S3_X(x)                                    ((x) << 24)
#       define R600_S3_Y(x)                                    ((x) << 28)
#       define R600_S4_X(x)                                    ((x) << 0)
#       define R600_S4_Y(x)                                    ((x) << 4)
#       define R600_S5_X(x)                                    ((x) << 8)
#       define R600_S5_Y(x)                                    ((x) << 12)
#       define R600_S6_X(x)                                    ((x) << 16)
#       define R600_S6_Y(x)                                    ((x) << 20)
#       define R600_S7_X(x)                                    ((x) << 24)
#       define R600_S7_Y(x)                                    ((x) << 28)
#define R600_PA_SC_FIFO_SIZE                                   0x8bd0
#       define R600_SC_PRIM_FIFO_SIZE(x)                       ((x) << 0)
#       define R600_SC_HIZ_TILE_FIFO_SIZE(x)                   ((x) << 8)
#       define R600_SC_EARLYZ_TILE_FIFO_SIZE(x)                ((x) << 16)
#define R700_PA_SC_FIFO_SIZE_R7XX                              0x8bcc
#       define R700_SC_PRIM_FIFO_SIZE(x)                       ((x) << 0)
#       define R700_SC_HIZ_TILE_FIFO_SIZE(x)                   ((x) << 12)
#       define R700_SC_EARLYZ_TILE_FIFO_SIZE(x)                ((x) << 20)
#define R600_PA_SC_ENHANCE                                     0x8bf0
#       define R600_FORCE_EOV_MAX_CLK_CNT(x)                   ((x) << 0)
#       define R600_FORCE_EOV_MAX_TILE_CNT(x)                  ((x) << 12)
#define R600_PA_SC_CLIPRECT_RULE                               0x2820c
#define R700_PA_SC_EDGERULE                                    0x28230
#define R600_PA_SC_LINE_STIPPLE                                0x28a0c
#define R600_PA_SC_MODE_CNTL                                   0x28a4c
#define R600_PA_SC_AA_CONFIG                                   0x28c04

#define R600_SX_EXPORT_BUFFER_SIZES                            0x900c
#       define R600_COLOR_BUFFER_SIZE(x)                       ((x) << 0)
#       define R600_POSITION_BUFFER_SIZE(x)                    ((x) << 8)
#       define R600_SMX_BUFFER_SIZE(x)                         ((x) << 16)
#define R600_SX_DEBUG_1                                        0x9054
#       define R600_SMX_EVENT_RELEASE                          (1 << 0)
#       define R600_ENABLE_NEW_SMX_ADDRESS                     (1 << 16)
#define R700_SX_DEBUG_1                                        0x9058
#       define R700_ENABLE_NEW_SMX_ADDRESS                     (1 << 16)
#define R600_SX_MISC                                           0x28350

#define R600_DB_DEBUG                                          0x9830
#       define R600_PREZ_MUST_WAIT_FOR_POSTZ_DONE              (1 << 31)
#define R600_DB_WATERMARKS                                     0x9838
#       define R600_DEPTH_FREE(x)                              ((x) << 0)
#       define R600_DEPTH_FLUSH(x)                             ((x) << 5)
#       define R600_DEPTH_PENDING_FREE(x)                      ((x) << 15)
#       define R600_DEPTH_CACHELINE_FREE(x)                    ((x) << 20)
#define R700_DB_DEBUG3                                         0x98b0
#       define R700_DB_CLK_OFF_DELAY(x)                        ((x) << 11)
#define RV700_DB_DEBUG4                                        0x9b8c
#       define RV700_DISABLE_TILE_COVERED_FOR_PS_ITER          (1 << 6)

#define R600_VGT_CACHE_INVALIDATION                            0x88c4
#       define R600_CACHE_INVALIDATION(x)                      ((x) << 0)
#       define R600_VC_ONLY                                    0
#       define R600_TC_ONLY                                    1
#       define R600_VC_AND_TC                                  2
#       define R700_AUTO_INVLD_EN(x)                           ((x) << 6)
#       define R700_NO_AUTO                                    0
#       define R700_ES_AUTO                                    1
#       define R700_GS_AUTO                                    2
#       define R700_ES_AND_GS_AUTO                             3
#define R600_VGT_GS_PER_ES                                     0x88c8
#define R600_VGT_ES_PER_GS                                     0x88cc
#define R600_VGT_GS_PER_VS                                     0x88e8
#define R600_VGT_GS_VERTEX_REUSE                               0x88d4
#define R600_VGT_NUM_INSTANCES                                 0x8974
#define R600_VGT_STRMOUT_EN                                    0x28ab0
#define R600_VGT_EVENT_INITIATOR                               0x28a90
#       define R600_CACHE_FLUSH_AND_INV_EVENT                  (0x16 << 0)
#define R600_VGT_VERTEX_REUSE_BLOCK_CNTL                       0x28c58
#       define R600_VTX_REUSE_DEPTH_MASK                       0xff
#define R600_VGT_OUT_DEALLOC_CNTL                              0x28c5c
#       define R600_DEALLOC_DIST_MASK                          0x7f

#define R600_CB_COLOR0_BASE                                    0x28040
#define R600_CB_COLOR1_BASE                                    0x28044
#define R600_CB_COLOR2_BASE                                    0x28048
#define R600_CB_COLOR3_BASE                                    0x2804c
#define R600_CB_COLOR4_BASE                                    0x28050
#define R600_CB_COLOR5_BASE                                    0x28054
#define R600_CB_COLOR6_BASE                                    0x28058
#define R600_CB_COLOR7_BASE                                    0x2805c
#define R600_CB_COLOR7_FRAG                                    0x280fc

#define R600_CB_COLOR0_SIZE                                    0x28060
#define R600_CB_COLOR0_VIEW                                    0x28080
#define R600_CB_COLOR0_INFO                                    0x280a0
#define R600_CB_COLOR0_TILE                                    0x280c0
#define R600_CB_COLOR0_FRAG                                    0x280e0
#define R600_CB_COLOR0_MASK                                    0x28100

#define AVIVO_D1MODE_VLINE_START_END                           0x6538
#define AVIVO_D2MODE_VLINE_START_END                           0x6d38
#define R600_CP_COHER_BASE                                     0x85f8
#define R600_DB_DEPTH_BASE                                     0x2800c
#define R600_SQ_PGM_START_FS                                   0x28894
#define R600_SQ_PGM_START_ES                                   0x28880
#define R600_SQ_PGM_START_VS                                   0x28858
#define R600_SQ_PGM_RESOURCES_VS                               0x28868
#define R600_SQ_PGM_CF_OFFSET_VS                               0x288d0
#define R600_SQ_PGM_START_GS                                   0x2886c
#define R600_SQ_PGM_START_PS                                   0x28840
#define R600_SQ_PGM_RESOURCES_PS                               0x28850
#define R600_SQ_PGM_EXPORTS_PS                                 0x28854
#define R600_SQ_PGM_CF_OFFSET_PS                               0x288cc
#define R600_VGT_DMA_BASE                                      0x287e8
#define R600_VGT_DMA_BASE_HI                                   0x287e4
#define R600_VGT_STRMOUT_BASE_OFFSET_0                         0x28b10
#define R600_VGT_STRMOUT_BASE_OFFSET_1                         0x28b14
#define R600_VGT_STRMOUT_BASE_OFFSET_2                         0x28b18
#define R600_VGT_STRMOUT_BASE_OFFSET_3                         0x28b1c
#define R600_VGT_STRMOUT_BASE_OFFSET_HI_0                      0x28b44
#define R600_VGT_STRMOUT_BASE_OFFSET_HI_1                      0x28b48
#define R600_VGT_STRMOUT_BASE_OFFSET_HI_2                      0x28b4c
#define R600_VGT_STRMOUT_BASE_OFFSET_HI_3                      0x28b50
#define R600_VGT_STRMOUT_BUFFER_BASE_0                         0x28ad8
#define R600_VGT_STRMOUT_BUFFER_BASE_1                         0x28ae8
#define R600_VGT_STRMOUT_BUFFER_BASE_2                         0x28af8
#define R600_VGT_STRMOUT_BUFFER_BASE_3                         0x28b08
#define R600_VGT_STRMOUT_BUFFER_OFFSET_0                       0x28adc
#define R600_VGT_STRMOUT_BUFFER_OFFSET_1                       0x28aec
#define R600_VGT_STRMOUT_BUFFER_OFFSET_2                       0x28afc
#define R600_VGT_STRMOUT_BUFFER_OFFSET_3                       0x28b0c

#define R600_VGT_PRIMITIVE_TYPE                                0x8958

#define R600_PA_SC_SCREEN_SCISSOR_TL                           0x28030
#define R600_PA_SC_GENERIC_SCISSOR_TL                          0x28240
#define R600_PA_SC_WINDOW_SCISSOR_TL                           0x28204

#define R600_TC_CNTL                                           0x9608
#       define R600_TC_L2_SIZE(x)                              ((x) << 5)
#       define R600_L2_DISABLE_LATE_HIT                        (1 << 9)

#define R600_ARB_POP                                           0x2418
#       define R600_ENABLE_TC128                               (1 << 30)
#define R600_ARB_GDEC_RD_CNTL                                  0x246c

#define R600_TA_CNTL_AUX                                       0x9508
#       define R600_DISABLE_CUBE_WRAP                          (1 << 0)
#       define R600_DISABLE_CUBE_ANISO                         (1 << 1)
#       define R700_GETLOD_SELECT(x)                           ((x) << 2)
#       define R600_SYNC_GRADIENT                              (1 << 24)
#       define R600_SYNC_WALKER                                (1 << 25)
#       define R600_SYNC_ALIGNER                               (1 << 26)
#       define R600_BILINEAR_PRECISION_6_BIT                   (0 << 31)
#       define R600_BILINEAR_PRECISION_8_BIT                   (1 << 31)

#define R700_TCP_CNTL                                          0x9610

#define R600_SMX_DC_CTL0                                       0xa020
#       define R700_USE_HASH_FUNCTION                          (1 << 0)
#       define R700_CACHE_DEPTH(x)                             ((x) << 1)
#       define R700_FLUSH_ALL_ON_EVENT                         (1 << 10)
#       define R700_STALL_ON_EVENT                             (1 << 11)
#define R700_SMX_EVENT_CTL                                     0xa02c
#       define R700_ES_FLUSH_CTL(x)                            ((x) << 0)
#       define R700_GS_FLUSH_CTL(x)                            ((x) << 3)
#       define R700_ACK_FLUSH_CTL(x)                           ((x) << 6)
#       define R700_SYNC_FLUSH_CTL                             (1 << 8)

#define R600_SQ_CONFIG                                         0x8c00
#       define R600_VC_ENABLE                                  (1 << 0)
#       define R600_EXPORT_SRC_C                               (1 << 1)
#       define R600_DX9_CONSTS                                 (1 << 2)
#       define R600_ALU_INST_PREFER_VECTOR                     (1 << 3)
#       define R600_DX10_CLAMP                                 (1 << 4)
#       define R600_CLAUSE_SEQ_PRIO(x)                         ((x) << 8)
#       define R600_PS_PRIO(x)                                 ((x) << 24)
#       define R600_VS_PRIO(x)                                 ((x) << 26)
#       define R600_GS_PRIO(x)                                 ((x) << 28)
#       define R600_ES_PRIO(x)                                 ((x) << 30)
#define R600_SQ_GPR_RESOURCE_MGMT_1                            0x8c04
#       define R600_NUM_PS_GPRS(x)                             ((x) << 0)
#       define R600_NUM_VS_GPRS(x)                             ((x) << 16)
#       define R700_DYN_GPR_ENABLE                             (1 << 27)
#       define R600_NUM_CLAUSE_TEMP_GPRS(x)                    ((x) << 28)
#define R600_SQ_GPR_RESOURCE_MGMT_2                            0x8c08
#       define R600_NUM_GS_GPRS(x)                             ((x) << 0)
#       define R600_NUM_ES_GPRS(x)                             ((x) << 16)
#define R600_SQ_THREAD_RESOURCE_MGMT                           0x8c0c
#       define R600_NUM_PS_THREADS(x)                          ((x) << 0)
#       define R600_NUM_VS_THREADS(x)                          ((x) << 8)
#       define R600_NUM_GS_THREADS(x)                          ((x) << 16)
#       define R600_NUM_ES_THREADS(x)                          ((x) << 24)
#define R600_SQ_STACK_RESOURCE_MGMT_1                          0x8c10
#       define R600_NUM_PS_STACK_ENTRIES(x)                    ((x) << 0)
#       define R600_NUM_VS_STACK_ENTRIES(x)                    ((x) << 16)
#define R600_SQ_STACK_RESOURCE_MGMT_2                          0x8c14
#       define R600_NUM_GS_STACK_ENTRIES(x)                    ((x) << 0)
#       define R600_NUM_ES_STACK_ENTRIES(x)                    ((x) << 16)
#define R600_SQ_MS_FIFO_SIZES                                  0x8cf0
#       define R600_CACHE_FIFO_SIZE(x)                         ((x) << 0)
#       define R600_FETCH_FIFO_HIWATER(x)                      ((x) << 8)
#       define R600_DONE_FIFO_HIWATER(x)                       ((x) << 16)
#       define R600_ALU_UPDATE_FIFO_HIWATER(x)                 ((x) << 24)
#define R700_SQ_DYN_GPR_SIZE_SIMD_AB_0                         0x8db0
#       define R700_SIMDA_RING0(x)                             ((x) << 0)
#       define R700_SIMDA_RING1(x)                             ((x) << 8)
#       define R700_SIMDB_RING0(x)                             ((x) << 16)
#       define R700_SIMDB_RING1(x)                             ((x) << 24)
#define R700_SQ_DYN_GPR_SIZE_SIMD_AB_1                         0x8db4
#define R700_SQ_DYN_GPR_SIZE_SIMD_AB_2                         0x8db8
#define R700_SQ_DYN_GPR_SIZE_SIMD_AB_3                         0x8dbc
#define R700_SQ_DYN_GPR_SIZE_SIMD_AB_4                         0x8dc0
#define R700_SQ_DYN_GPR_SIZE_SIMD_AB_5                         0x8dc4
#define R700_SQ_DYN_GPR_SIZE_SIMD_AB_6                         0x8dc8
#define R700_SQ_DYN_GPR_SIZE_SIMD_AB_7                         0x8dcc

#define R600_SPI_PS_IN_CONTROL_0                               0x286cc
#       define R600_NUM_INTERP(x)                              ((x) << 0)
#       define R600_POSITION_ENA                               (1 << 8)
#       define R600_POSITION_CENTROID                          (1 << 9)
#       define R600_POSITION_ADDR(x)                           ((x) << 10)
#       define R600_PARAM_GEN(x)                               ((x) << 15)
#       define R600_PARAM_GEN_ADDR(x)                          ((x) << 19)
#       define R600_BARYC_SAMPLE_CNTL(x)                       ((x) << 26)
#       define R600_PERSP_GRADIENT_ENA                         (1 << 28)
#       define R600_LINEAR_GRADIENT_ENA                        (1 << 29)
#       define R600_POSITION_SAMPLE                            (1 << 30)
#       define R600_BARYC_AT_SAMPLE_ENA                        (1 << 31)
#define R600_SPI_PS_IN_CONTROL_1                               0x286d0
#       define R600_GEN_INDEX_PIX                              (1 << 0)
#       define R600_GEN_INDEX_PIX_ADDR(x)                      ((x) << 1)
#       define R600_FRONT_FACE_ENA                             (1 << 8)
#       define R600_FRONT_FACE_CHAN(x)                         ((x) << 9)
#       define R600_FRONT_FACE_ALL_BITS                        (1 << 11)
#       define R600_FRONT_FACE_ADDR(x)                         ((x) << 12)
#       define R600_FOG_ADDR(x)                                ((x) << 17)
#       define R600_FIXED_PT_POSITION_ENA                      (1 << 24)
#       define R600_FIXED_PT_POSITION_ADDR(x)                  ((x) << 25)
#       define R700_POSITION_ULC                               (1 << 30)
#define R600_SPI_INPUT_Z                                       0x286d8

#define R600_SPI_CONFIG_CNTL                                   0x9100
#       define R600_GPR_WRITE_PRIORITY(x)                      ((x) << 0)
#       define R600_DISABLE_INTERP_1                           (1 << 5)
#define R600_SPI_CONFIG_CNTL_1                                 0x913c
#       define R600_VTX_DONE_DELAY(x)                          ((x) << 0)
#       define R600_INTERP_ONE_PRIM_PER_ROW                    (1 << 4)

#define R600_GB_TILING_CONFIG                                  0x98f0
#       define R600_PIPE_TILING(x)                             ((x) << 1)
#       define R600_BANK_TILING(x)                             ((x) << 4)
#       define R600_GROUP_SIZE(x)                              ((x) << 6)
#       define R600_ROW_TILING(x)                              ((x) << 8)
#       define R600_BANK_SWAPS(x)                              ((x) << 11)
#       define R600_SAMPLE_SPLIT(x)                            ((x) << 14)
#       define R600_BACKEND_MAP(x)                             ((x) << 16)
#define R600_DCP_TILING_CONFIG                                 0x6ca0
#define R600_HDP_TILING_CONFIG                                 0x2f3c

#define R600_CC_RB_BACKEND_DISABLE                             0x98f4
#define R700_CC_SYS_RB_BACKEND_DISABLE                         0x3f88
#       define R600_BACKEND_DISABLE(x)                         ((x) << 16)

#define R600_CC_GC_SHADER_PIPE_CONFIG                          0x8950
#define R600_GC_USER_SHADER_PIPE_CONFIG                        0x8954
#       define R600_INACTIVE_QD_PIPES(x)                       ((x) << 8)
#       define R600_INACTIVE_QD_PIPES_MASK                     (0xff << 8)
#       define R600_INACTIVE_SIMDS(x)                          ((x) << 16)
#       define R600_INACTIVE_SIMDS_MASK                        (0xff << 16)

#define R700_CGTS_SYS_TCC_DISABLE                              0x3f90
#define R700_CGTS_USER_SYS_TCC_DISABLE                         0x3f94
#define R700_CGTS_TCC_DISABLE                                  0x9148
#define R700_CGTS_USER_TCC_DISABLE                             0x914c

/* Constants */
#define RADEON_MAX_USEC_TIMEOUT		100000	/* 100 ms */

#define RADEON_LAST_FRAME_REG		RADEON_SCRATCH_REG0
#define RADEON_LAST_DISPATCH_REG	RADEON_SCRATCH_REG1
#define RADEON_LAST_CLEAR_REG		RADEON_SCRATCH_REG2
#define RADEON_LAST_SWI_REG		RADEON_SCRATCH_REG3
#define RADEON_LAST_DISPATCH		1

#define R600_LAST_FRAME_REG		R600_SCRATCH_REG0
#define R600_LAST_DISPATCH_REG	        R600_SCRATCH_REG1
#define R600_LAST_CLEAR_REG		R600_SCRATCH_REG2
#define R600_LAST_SWI_REG		R600_SCRATCH_REG3

#define RADEON_MAX_VB_AGE		0x7fffffff
#define RADEON_MAX_VB_VERTS		(0xffff)

#define RADEON_RING_HIGH_MARK		128

#define RADEON_PCIGART_TABLE_SIZE      (32*1024)

#define RADEON_READ(reg)	DRM_READ32(  dev_priv->mmio, (reg) )
#define RADEON_WRITE(reg, val)                                          \
do {									\
	if (reg < 0x10000) {				                \
		DRM_WRITE32(dev_priv->mmio, (reg), (val));		\
	} else {                                                        \
		DRM_WRITE32(dev_priv->mmio, RADEON_MM_INDEX, (reg));	\
		DRM_WRITE32(dev_priv->mmio, RADEON_MM_DATA, (val));	\
	}                                                               \
} while (0)
#define RADEON_READ8(reg)	DRM_READ8(  dev_priv->mmio, (reg) )
#define RADEON_WRITE8(reg,val)	DRM_WRITE8( dev_priv->mmio, (reg), (val) )

#define RADEON_WRITE_PLL(addr, val)					\
do {									\
	RADEON_WRITE8(RADEON_CLOCK_CNTL_INDEX,				\
		       ((addr) & 0x1f) | RADEON_PLL_WR_EN );		\
	RADEON_WRITE(RADEON_CLOCK_CNTL_DATA, (val));			\
} while (0)

#define RADEON_WRITE_PCIE(addr, val)					\
do {									\
	RADEON_WRITE8(RADEON_PCIE_INDEX,				\
			((addr) & 0xff));				\
	RADEON_WRITE(RADEON_PCIE_DATA, (val));			\
} while (0)

#define R500_WRITE_MCIND(addr, val)					\
do {								\
	RADEON_WRITE(R520_MC_IND_INDEX, 0xff0000 | ((addr) & 0xff));	\
	RADEON_WRITE(R520_MC_IND_DATA, (val));			\
	RADEON_WRITE(R520_MC_IND_INDEX, 0);	\
} while (0)

#define RS480_WRITE_MCIND(addr, val)				\
do {									\
	RADEON_WRITE(RS480_NB_MC_INDEX,				\
			((addr) & 0xff) | RS480_NB_MC_IND_WR_EN);	\
	RADEON_WRITE(RS480_NB_MC_DATA, (val));			\
	RADEON_WRITE(RS480_NB_MC_INDEX, 0xff);			\
} while (0)

#define RS690_WRITE_MCIND(addr, val)					\
do {								\
	RADEON_WRITE(RS690_MC_INDEX, RS690_MC_INDEX_WR_EN | ((addr) & RS690_MC_INDEX_MASK));	\
	RADEON_WRITE(RS690_MC_DATA, val);			\
	RADEON_WRITE(RS690_MC_INDEX, RS690_MC_INDEX_WR_ACK);	\
} while (0)

#define RS600_WRITE_MCIND(addr, val)				\
do {							        \
	RADEON_WRITE(RS600_MC_INDEX, RS600_MC_IND_WR_EN | RS600_MC_IND_CITF_ARB0 | ((addr) & RS600_MC_ADDR_MASK)); \
	RADEON_WRITE(RS600_MC_DATA, val);                       \
} while (0)

#define IGP_WRITE_MCIND(addr, val)				\
do {									\
	if (((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS690) ||   \
	    ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS740))      \
		RS690_WRITE_MCIND(addr, val);				\
	else if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS600)  \
		RS600_WRITE_MCIND(addr, val);				\
	else								\
		RS480_WRITE_MCIND(addr, val);				\
} while (0)

#define CP_PACKET0( reg, n )						\
	(RADEON_CP_PACKET0 | ((n) << 16) | ((reg) >> 2))
#define CP_PACKET0_TABLE( reg, n )					\
	(RADEON_CP_PACKET0 | RADEON_ONE_REG_WR | ((n) << 16) | ((reg) >> 2))
#define CP_PACKET1( reg0, reg1 )					\
	(RADEON_CP_PACKET1 | (((reg1) >> 2) << 15) | ((reg0) >> 2))
#define CP_PACKET2()							\
	(RADEON_CP_PACKET2)
#define CP_PACKET3( pkt, n )						\
	(RADEON_CP_PACKET3 | (pkt) | ((n) << 16))

/* ================================================================
 * Engine control helper macros
 */

#define RADEON_WAIT_UNTIL_2D_IDLE() do {				\
	OUT_RING( CP_PACKET0( RADEON_WAIT_UNTIL, 0 ) );			\
	OUT_RING( (RADEON_WAIT_2D_IDLECLEAN |				\
		   RADEON_WAIT_HOST_IDLECLEAN) );			\
} while (0)

#define RADEON_WAIT_UNTIL_3D_IDLE() do {				\
	OUT_RING( CP_PACKET0( RADEON_WAIT_UNTIL, 0 ) );			\
	OUT_RING( (RADEON_WAIT_3D_IDLECLEAN |				\
		   RADEON_WAIT_HOST_IDLECLEAN) );			\
} while (0)

#define RADEON_WAIT_UNTIL_IDLE() do {					\
	OUT_RING( CP_PACKET0( RADEON_WAIT_UNTIL, 0 ) );			\
	OUT_RING( (RADEON_WAIT_2D_IDLECLEAN |				\
		   RADEON_WAIT_3D_IDLECLEAN |				\
		   RADEON_WAIT_HOST_IDLECLEAN) );			\
} while (0)

#define RADEON_WAIT_UNTIL_PAGE_FLIPPED() do {				\
	OUT_RING( CP_PACKET0( RADEON_WAIT_UNTIL, 0 ) );			\
	OUT_RING( RADEON_WAIT_CRTC_PFLIP );				\
} while (0)

#define RADEON_FLUSH_CACHE() do {					\
	if ((dev_priv->flags & RADEON_FAMILY_MASK) <= CHIP_RV280) {	\
		OUT_RING(CP_PACKET0(RADEON_RB3D_DSTCACHE_CTLSTAT, 0));	\
		OUT_RING(RADEON_RB3D_DC_FLUSH);				\
	} else {                                                        \
		OUT_RING(CP_PACKET0(R300_RB3D_DSTCACHE_CTLSTAT, 0));	\
		OUT_RING(R300_RB3D_DC_FLUSH);				\
	}                                                               \
} while (0)

#define RADEON_PURGE_CACHE() do {					\
	if ((dev_priv->flags & RADEON_FAMILY_MASK) <= CHIP_RV280) {	\
		OUT_RING(CP_PACKET0(RADEON_RB3D_DSTCACHE_CTLSTAT, 0));	\
		OUT_RING(RADEON_RB3D_DC_FLUSH | RADEON_RB3D_DC_FREE);	\
	} else {                                                        \
		OUT_RING(CP_PACKET0(R300_RB3D_DSTCACHE_CTLSTAT, 0));	\
		OUT_RING(R300_RB3D_DC_FLUSH | R300_RB3D_DC_FREE);	\
	}                                                               \
} while (0)

#define RADEON_FLUSH_ZCACHE() do {					\
	if ((dev_priv->flags & RADEON_FAMILY_MASK) <= CHIP_RV280) {	\
		OUT_RING(CP_PACKET0(RADEON_RB3D_ZCACHE_CTLSTAT, 0));	\
		OUT_RING(RADEON_RB3D_ZC_FLUSH);				\
	} else {                                                        \
		OUT_RING(CP_PACKET0(R300_ZB_ZCACHE_CTLSTAT, 0));	\
		OUT_RING(R300_ZC_FLUSH);				\
	}                                                               \
} while (0)

#define RADEON_PURGE_ZCACHE() do {					\
	if ((dev_priv->flags & RADEON_FAMILY_MASK) <= CHIP_RV280) {	\
		OUT_RING(CP_PACKET0(RADEON_RB3D_ZCACHE_CTLSTAT, 0));	\
		OUT_RING(RADEON_RB3D_ZC_FLUSH | RADEON_RB3D_ZC_FREE);			\
	} else {                                                        \
		OUT_RING(CP_PACKET0(R300_ZB_ZCACHE_CTLSTAT, 0));	\
		OUT_RING(R300_ZC_FLUSH | R300_ZC_FREE);				\
	}                                                               \
} while (0)

/* ================================================================
 * Misc helper macros
 */

/* Perfbox functionality only.
 */
#define RING_SPACE_TEST_WITH_RETURN( dev_priv )				\
do {									\
	if (!(dev_priv->stats.boxes & RADEON_BOX_DMA_IDLE)) {		\
		u32 head = GET_RING_HEAD( dev_priv );			\
		if (head == dev_priv->ring.tail)			\
			dev_priv->stats.boxes |= RADEON_BOX_DMA_IDLE;	\
	}								\
} while (0)

#define VB_AGE_TEST_WITH_RETURN( dev_priv )				\
do {								\
	struct drm_radeon_master_private *master_priv = file_priv->master->driver_priv;	\
	drm_radeon_sarea_t *sarea_priv = master_priv->sarea_priv;	\
	if ( sarea_priv->last_dispatch >= RADEON_MAX_VB_AGE ) {		\
		int __ret;						\
		if ((dev_priv->flags & RADEON_FAMILY_MASK) >= CHIP_R600) \
			__ret = r600_do_cp_idle(dev_priv);		\
		else							\
			__ret = radeon_do_cp_idle(dev_priv);		\
		if ( __ret ) return __ret;				\
		sarea_priv->last_dispatch = 0;				\
		radeon_freelist_reset( dev );				\
	}								\
} while (0)

#define RADEON_DISPATCH_AGE( age ) do {					\
	OUT_RING( CP_PACKET0( RADEON_LAST_DISPATCH_REG, 0 ) );		\
	OUT_RING( age );						\
} while (0)

#define RADEON_FRAME_AGE( age ) do {					\
	OUT_RING( CP_PACKET0( RADEON_LAST_FRAME_REG, 0 ) );		\
	OUT_RING( age );						\
} while (0)

#define RADEON_CLEAR_AGE( age ) do {					\
	OUT_RING( CP_PACKET0( RADEON_LAST_CLEAR_REG, 0 ) );		\
	OUT_RING( age );						\
} while (0)

#define R600_DISPATCH_AGE(age) do {					\
	OUT_RING(CP_PACKET3(R600_IT_SET_CONFIG_REG, 1));		\
	OUT_RING((R600_LAST_DISPATCH_REG - R600_SET_CONFIG_REG_OFFSET) >> 2);  \
	OUT_RING(age);							\
} while (0)

#define R600_FRAME_AGE(age) do {					\
	OUT_RING(CP_PACKET3(R600_IT_SET_CONFIG_REG, 1));		\
	OUT_RING((R600_LAST_FRAME_REG - R600_SET_CONFIG_REG_OFFSET) >> 2);  \
	OUT_RING(age);							\
} while (0)

#define R600_CLEAR_AGE(age) do {					\
	OUT_RING(CP_PACKET3(R600_IT_SET_CONFIG_REG, 1));		\
	OUT_RING((R600_LAST_CLEAR_REG - R600_SET_CONFIG_REG_OFFSET) >> 2);  \
	OUT_RING(age);							\
} while (0)

/* ================================================================
 * Ring control
 */

#define RADEON_VERBOSE	0

#define RING_LOCALS	int write, _nr, _align_nr; unsigned int mask; u32 *ring;

#define RADEON_RING_ALIGN 16

#define BEGIN_RING( n ) do {						\
	if ( RADEON_VERBOSE ) {						\
		DRM_INFO( "BEGIN_RING( %d )\n", (n));			\
	}								\
	_align_nr = RADEON_RING_ALIGN - ((dev_priv->ring.tail + n) & (RADEON_RING_ALIGN-1));	\
	_align_nr += n;							\
	if (dev_priv->ring.space <= (_align_nr * sizeof(u32))) {	\
                COMMIT_RING();						\
		radeon_wait_ring( dev_priv, _align_nr * sizeof(u32));	\
	}								\
	_nr = n; dev_priv->ring.space -= (n) * sizeof(u32);		\
	ring = dev_priv->ring.start;					\
	write = dev_priv->ring.tail;					\
	mask = dev_priv->ring.tail_mask;				\
} while (0)

#define ADVANCE_RING() do {						\
	if ( RADEON_VERBOSE ) {						\
		DRM_INFO( "ADVANCE_RING() wr=0x%06x tail=0x%06x\n",	\
			  write, dev_priv->ring.tail );			\
	}								\
	if (((dev_priv->ring.tail + _nr) & mask) != write) {		\
		DRM_ERROR(						\
			"ADVANCE_RING(): mismatch: nr: %x write: %x line: %d\n",	\
			((dev_priv->ring.tail + _nr) & mask),		\
			write, __LINE__);				\
	} else								\
		dev_priv->ring.tail = write;				\
} while (0)

extern void radeon_commit_ring(drm_radeon_private_t *dev_priv);

#define COMMIT_RING() do {						\
		radeon_commit_ring(dev_priv);				\
	} while(0)

#define OUT_RING( x ) do {						\
	if ( RADEON_VERBOSE ) {						\
		DRM_INFO( "   OUT_RING( 0x%08x ) at 0x%x\n",		\
			   (unsigned int)(x), write );			\
	}								\
	ring[write++] = (x);						\
	write &= mask;							\
} while (0)

#define OUT_RING_REG( reg, val ) do {					\
	OUT_RING( CP_PACKET0( reg, 0 ) );				\
	OUT_RING( val );						\
} while (0)

#define OUT_RING_TABLE( tab, sz ) do {					\
	int _size = (sz);					\
	int *_tab = (int *)(tab);				\
								\
	if (write + _size > mask) {				\
		int _i = (mask+1) - write;			\
		_size -= _i;					\
		while (_i > 0 ) {				\
			*(int *)(ring + write) = *_tab++;	\
			write++;				\
			_i--;					\
		}						\
		write = 0;					\
		_tab += _i;					\
	}							\
	while (_size > 0) {					\
		*(ring + write) = *_tab++;			\
		write++;					\
		_size--;					\
	}							\
	write &= mask;						\
} while (0)

#endif				/* __RADEON_DRV_H__ */
