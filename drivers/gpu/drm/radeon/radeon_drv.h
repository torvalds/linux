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
 */
#define DRIVER_MAJOR		1
#define DRIVER_MINOR		29
#define DRIVER_PATCHLEVEL	0

/*
 * Radeon chip families
 */
enum radeon_family {
	CHIP_R100,
	CHIP_RV100,
	CHIP_RS100,
	CHIP_RV200,
	CHIP_RS200,
	CHIP_R200,
	CHIP_RV250,
	CHIP_RS300,
	CHIP_RV280,
	CHIP_R300,
	CHIP_R350,
	CHIP_RV350,
	CHIP_RV380,
	CHIP_R420,
	CHIP_RV410,
	CHIP_RS480,
	CHIP_RS690,
	CHIP_RV515,
	CHIP_R520,
	CHIP_RV530,
	CHIP_RV560,
	CHIP_RV570,
	CHIP_R580,
	CHIP_LAST,
};

enum radeon_cp_microcode_version {
	UCODE_R100,
	UCODE_R200,
	UCODE_R300,
};

/*
 * Chip flags
 */
enum radeon_chip_flags {
	RADEON_FAMILY_MASK = 0x0000ffffUL,
	RADEON_FLAGS_MASK = 0xffff0000UL,
	RADEON_IS_MOBILITY = 0x00010000UL,
	RADEON_IS_IGP = 0x00020000UL,
	RADEON_SINGLE_CRTC = 0x00040000UL,
	RADEON_IS_AGP = 0x00080000UL,
	RADEON_HAS_HIERZ = 0x00100000UL,
	RADEON_IS_PCIE = 0x00200000UL,
	RADEON_NEW_MEMMAP = 0x00400000UL,
	RADEON_IS_PCI = 0x00800000UL,
	RADEON_IS_IGPGART = 0x01000000UL,
};

#define GET_RING_HEAD(dev_priv)	(dev_priv->writeback_works ? \
        DRM_READ32(  (dev_priv)->ring_rptr, 0 ) : RADEON_READ(RADEON_CP_RB_RPTR))
#define SET_RING_HEAD(dev_priv,val)	DRM_WRITE32( (dev_priv)->ring_rptr, 0, (val) )

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
};

#define RADEON_FLUSH_EMITED	(1 < 0)
#define RADEON_PURGE_EMITED	(1 < 1)

typedef struct drm_radeon_private {
	drm_radeon_ring_buffer_t ring;
	drm_radeon_sarea_t *sarea_priv;

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
	volatile u32 *scratch;
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
	drm_local_map_t *mmio;
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
	int irq_enabled;
	uint32_t r500_disp_irq_reg;

	struct radeon_surface surfaces[RADEON_MAX_SURFACES];
	struct radeon_virt_surface virt_surfaces[2 * RADEON_MAX_SURFACES];

	unsigned long pcigart_offset;
	unsigned int pcigart_offset_set;
	struct drm_ati_pcigart_info gart_info;

	u32 scratch_ages[5];

	/* starting from here on, data is preserved accross an open */
	uint32_t flags;		/* see radeon_chip_flags */
	unsigned long fb_aper_offset;

	int num_gb_pipes;
	int track_flush;
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

				/* radeon_irq.c */
extern int radeon_irq_emit(struct drm_device *dev, void *data, struct drm_file *file_priv);
extern int radeon_irq_wait(struct drm_device *dev, void *data, struct drm_file *file_priv);

extern void radeon_do_release(struct drm_device * dev);
extern int radeon_driver_vblank_wait(struct drm_device * dev,
				     unsigned int *sequence);
extern int radeon_driver_vblank_wait2(struct drm_device * dev,
				      unsigned int *sequence);
extern irqreturn_t radeon_driver_irq_handler(DRM_IRQ_ARGS);
extern void radeon_driver_irq_preinstall(struct drm_device * dev);
extern void radeon_driver_irq_postinstall(struct drm_device * dev);
extern void radeon_driver_irq_uninstall(struct drm_device * dev);
extern void radeon_enable_interrupt(struct drm_device *dev);
extern int radeon_vblank_crtc_get(struct drm_device *dev);
extern int radeon_vblank_crtc_set(struct drm_device *dev, int64_t value);

extern int radeon_driver_load(struct drm_device *dev, unsigned long flags);
extern int radeon_driver_unload(struct drm_device *dev);
extern int radeon_driver_firstopen(struct drm_device *dev);
extern void radeon_driver_preclose(struct drm_device * dev, struct drm_file *file_priv);
extern void radeon_driver_postclose(struct drm_device * dev, struct drm_file * filp);
extern void radeon_driver_lastclose(struct drm_device * dev);
extern int radeon_driver_open(struct drm_device * dev, struct drm_file * filp_priv);
extern long radeon_compat_ioctl(struct file *filp, unsigned int cmd,
				unsigned long arg);

/* r300_cmdbuf.c */
extern void r300_init_reg_flags(struct drm_device *dev);

extern int r300_do_cp_cmdbuf(struct drm_device * dev,
			     struct drm_file *file_priv,
			     drm_radeon_kcmd_buffer_t * cmdbuf);

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

#define RADEON_BUS_CNTL			0x0030
#	define RADEON_BUS_MASTER_DIS		(1 << 6)

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
#define R500_DYN_SCLK_PWMEM_PIPE        0x000d /* PLL */
#define R500_SU_REG_DEST                0x42c8
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

#define GET_SCRATCH( x )	(dev_priv->writeback_works			\
				? DRM_READ32( dev_priv->ring_rptr, RADEON_SCRATCHOFF(x) ) \
				: RADEON_READ( RADEON_SCRATCH_REG0 + 4*(x) ) )

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

#define RADEON_AIC_CNTL			0x01d0
#	define RADEON_PCIGART_TRANSLATE_EN	(1 << 0)
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

/* Constants */
#define RADEON_MAX_USEC_TIMEOUT		100000	/* 100 ms */

#define RADEON_LAST_FRAME_REG		RADEON_SCRATCH_REG0
#define RADEON_LAST_DISPATCH_REG	RADEON_SCRATCH_REG1
#define RADEON_LAST_CLEAR_REG		RADEON_SCRATCH_REG2
#define RADEON_LAST_SWI_REG		RADEON_SCRATCH_REG3
#define RADEON_LAST_DISPATCH		1

#define RADEON_MAX_VB_AGE		0x7fffffff
#define RADEON_MAX_VB_VERTS		(0xffff)

#define RADEON_RING_HIGH_MARK		128

#define RADEON_PCIGART_TABLE_SIZE      (32*1024)

#define RADEON_READ(reg)	DRM_READ32(  dev_priv->mmio, (reg) )
#define RADEON_WRITE(reg,val)	DRM_WRITE32( dev_priv->mmio, (reg), (val) )
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

#define IGP_WRITE_MCIND(addr, val)				\
do {									\
	if ((dev_priv->flags & RADEON_FAMILY_MASK) == CHIP_RS690)       \
		RS690_WRITE_MCIND(addr, val);				\
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
do {									\
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;		\
	if ( sarea_priv->last_dispatch >= RADEON_MAX_VB_AGE ) {		\
		int __ret = radeon_do_cp_idle( dev_priv );		\
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

/* ================================================================
 * Ring control
 */

#define RADEON_VERBOSE	0

#define RING_LOCALS	int write, _nr; unsigned int mask; u32 *ring;

#define BEGIN_RING( n ) do {						\
	if ( RADEON_VERBOSE ) {						\
		DRM_INFO( "BEGIN_RING( %d )\n", (n));			\
	}								\
	if ( dev_priv->ring.space <= (n) * sizeof(u32) ) {		\
                COMMIT_RING();						\
		radeon_wait_ring( dev_priv, (n) * sizeof(u32) );	\
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
			write, __LINE__);						\
	} else								\
		dev_priv->ring.tail = write;				\
} while (0)

#define COMMIT_RING() do {						\
	/* Flush writes to ring */					\
	DRM_MEMORYBARRIER();						\
	GET_RING_HEAD( dev_priv );					\
	RADEON_WRITE( RADEON_CP_RB_WPTR, dev_priv->ring.tail );		\
	/* read from PCI bus to ensure correct posting */		\
	RADEON_READ( RADEON_CP_RB_RPTR );				\
} while (0)

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
