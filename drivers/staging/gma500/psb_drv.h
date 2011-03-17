/**************************************************************************
 * Copyright (c) 2007-2008, Intel Corporation.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/

#ifndef _PSB_DRV_H_
#define _PSB_DRV_H_

#include <linux/version.h>

#include <drm/drmP.h>
#include "drm_global.h"
#include "psb_drm.h"
#include "psb_reg.h"
#include "psb_intel_drv.h"
#include "psb_gtt.h"
#include "psb_powermgmt.h"
#include "ttm/ttm_object.h"
#include "psb_ttm_fence_driver.h"
#include "psb_ttm_userobj_api.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_lock.h"

/*Append new drm mode definition here, align with libdrm definition*/
#define DRM_MODE_SCALE_NO_SCALE   2

extern struct ttm_bo_driver psb_ttm_bo_driver;

enum {
	CHIP_PSB_8108 = 0,
	CHIP_PSB_8109 = 1,
};

/*
 *Hardware bugfixes
 */

#define DRIVER_NAME "pvrsrvkm"
#define DRIVER_DESC "drm driver for the Intel GMA500"
#define DRIVER_AUTHOR "Intel Corporation"
#define OSPM_PROC_ENTRY "ospm"
#define RTPM_PROC_ENTRY "rtpm"
#define BLC_PROC_ENTRY "mrst_blc"
#define DISPLAY_PROC_ENTRY "display_status"

#define PSB_DRM_DRIVER_DATE "2009-03-10"
#define PSB_DRM_DRIVER_MAJOR 8
#define PSB_DRM_DRIVER_MINOR 1
#define PSB_DRM_DRIVER_PATCHLEVEL 0

/*
 *TTM driver private offsets.
 */

#define DRM_PSB_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

#define PSB_OBJECT_HASH_ORDER 13
#define PSB_FILE_OBJECT_HASH_ORDER 12
#define PSB_BO_HASH_ORDER 12

#define PSB_VDC_OFFSET		 0x00000000
#define PSB_VDC_SIZE		 0x000080000
#define MRST_MMIO_SIZE		 0x0000C0000
#define MDFLD_MMIO_SIZE          0x000100000
#define PSB_SGX_SIZE		 0x8000
#define PSB_SGX_OFFSET		 0x00040000
#define MRST_SGX_OFFSET		 0x00080000
#define PSB_MMIO_RESOURCE	 0
#define PSB_GATT_RESOURCE	 2
#define PSB_GTT_RESOURCE	 3
#define PSB_GMCH_CTRL		 0x52
#define PSB_BSM			 0x5C
#define _PSB_GMCH_ENABLED	 0x4
#define PSB_PGETBL_CTL		 0x2020
#define _PSB_PGETBL_ENABLED	 0x00000001
#define PSB_SGX_2D_SLAVE_PORT	 0x4000
#define PSB_TT_PRIV0_LIMIT	 (256*1024*1024)
#define PSB_TT_PRIV0_PLIMIT	 (PSB_TT_PRIV0_LIMIT >> PAGE_SHIFT)
#define PSB_NUM_VALIDATE_BUFFERS 2048

#define PSB_MEM_MMU_START       0x00000000
#define PSB_MEM_TT_START        0xE0000000

#define PSB_GL3_CACHE_CTL	0x2100
#define PSB_GL3_CACHE_STAT	0x2108

/*
 *Flags for external memory type field.
 */

#define MRST_MSVDX_OFFSET	0x90000	/*MSVDX Base offset */
#define PSB_MSVDX_OFFSET	0x50000	/*MSVDX Base offset */
/* MSVDX MMIO region is 0x50000 - 0x57fff ==> 32KB */
#define PSB_MSVDX_SIZE		0x10000

#define LNC_TOPAZ_OFFSET	0xA0000
#define PNW_TOPAZ_OFFSET	0xC0000
#define PNW_GL3_OFFSET		0xB0000
#define LNC_TOPAZ_SIZE		0x10000
#define PNW_TOPAZ_SIZE		0x30000 /* PNW VXE285 has two cores */
#define PSB_MMU_CACHED_MEMORY	  0x0001	/* Bind to MMU only */
#define PSB_MMU_RO_MEMORY	  0x0002	/* MMU RO memory */
#define PSB_MMU_WO_MEMORY	  0x0004	/* MMU WO memory */

/*
 *PTE's and PDE's
 */

#define PSB_PDE_MASK		  0x003FFFFF
#define PSB_PDE_SHIFT		  22
#define PSB_PTE_SHIFT		  12

#define PSB_PTE_VALID		  0x0001	/* PTE / PDE valid */
#define PSB_PTE_WO		  0x0002	/* Write only */
#define PSB_PTE_RO		  0x0004	/* Read only */
#define PSB_PTE_CACHED		  0x0008	/* CPU cache coherent */

/*
 *VDC registers and bits
 */
#define PSB_MSVDX_CLOCKGATING	  0x2064
#define PSB_TOPAZ_CLOCKGATING	  0x2068
#define PSB_HWSTAM		  0x2098
#define PSB_INSTPM		  0x20C0
#define PSB_INT_IDENTITY_R        0x20A4
#define _MDFLD_PIPEC_EVENT_FLAG   (1<<2)
#define _MDFLD_PIPEC_VBLANK_FLAG  (1<<3)
#define _PSB_DPST_PIPEB_FLAG      (1<<4)
#define _MDFLD_PIPEB_EVENT_FLAG   (1<<4)
#define _PSB_VSYNC_PIPEB_FLAG	  (1<<5)
#define _PSB_DPST_PIPEA_FLAG      (1<<6)
#define _PSB_PIPEA_EVENT_FLAG     (1<<6)
#define _PSB_VSYNC_PIPEA_FLAG	  (1<<7)
#define _MDFLD_MIPIA_FLAG	  (1<<16)
#define _MDFLD_MIPIC_FLAG	  (1<<17)
#define _PSB_IRQ_SGX_FLAG	  (1<<18)
#define _PSB_IRQ_MSVDX_FLAG	  (1<<19)
#define _LNC_IRQ_TOPAZ_FLAG	  (1<<20)

/* This flag includes all the display IRQ bits excepts the vblank irqs. */
#define _MDFLD_DISP_ALL_IRQ_FLAG (_MDFLD_PIPEC_EVENT_FLAG | _MDFLD_PIPEB_EVENT_FLAG | \
        _PSB_PIPEA_EVENT_FLAG | _PSB_VSYNC_PIPEA_FLAG | _MDFLD_MIPIA_FLAG | _MDFLD_MIPIC_FLAG)
#define PSB_INT_IDENTITY_R	  0x20A4
#define PSB_INT_MASK_R		  0x20A8
#define PSB_INT_ENABLE_R	  0x20A0

#define _PSB_MMU_ER_MASK      0x0001FF00
#define _PSB_MMU_ER_HOST      (1 << 16)
#define GPIOA			0x5010
#define GPIOB			0x5014
#define GPIOC			0x5018
#define GPIOD			0x501c
#define GPIOE			0x5020
#define GPIOF			0x5024
#define GPIOG			0x5028
#define GPIOH			0x502c
#define GPIO_CLOCK_DIR_MASK		(1 << 0)
#define GPIO_CLOCK_DIR_IN		(0 << 1)
#define GPIO_CLOCK_DIR_OUT		(1 << 1)
#define GPIO_CLOCK_VAL_MASK		(1 << 2)
#define GPIO_CLOCK_VAL_OUT		(1 << 3)
#define GPIO_CLOCK_VAL_IN		(1 << 4)
#define GPIO_CLOCK_PULLUP_DISABLE	(1 << 5)
#define GPIO_DATA_DIR_MASK		(1 << 8)
#define GPIO_DATA_DIR_IN		(0 << 9)
#define GPIO_DATA_DIR_OUT		(1 << 9)
#define GPIO_DATA_VAL_MASK		(1 << 10)
#define GPIO_DATA_VAL_OUT		(1 << 11)
#define GPIO_DATA_VAL_IN		(1 << 12)
#define GPIO_DATA_PULLUP_DISABLE	(1 << 13)

#define VCLK_DIVISOR_VGA0   0x6000
#define VCLK_DIVISOR_VGA1   0x6004
#define VCLK_POST_DIV	    0x6010

#define PSB_COMM_2D (PSB_ENGINE_2D << 4)
#define PSB_COMM_3D (PSB_ENGINE_3D << 4)
#define PSB_COMM_TA (PSB_ENGINE_TA << 4)
#define PSB_COMM_HP (PSB_ENGINE_HP << 4)
#define PSB_COMM_USER_IRQ (1024 >> 2)
#define PSB_COMM_USER_IRQ_LOST (PSB_COMM_USER_IRQ + 1)
#define PSB_COMM_FW (2048 >> 2)

#define PSB_UIRQ_VISTEST	       1
#define PSB_UIRQ_OOM_REPLY	       2
#define PSB_UIRQ_FIRE_TA_REPLY	       3
#define PSB_UIRQ_FIRE_RASTER_REPLY     4

#define PSB_2D_SIZE (256*1024*1024)
#define PSB_MAX_RELOC_PAGES 1024

#define PSB_LOW_REG_OFFS 0x0204
#define PSB_HIGH_REG_OFFS 0x0600

#define PSB_NUM_VBLANKS 2


#define PSB_2D_SIZE (256*1024*1024)
#define PSB_MAX_RELOC_PAGES 1024

#define PSB_LOW_REG_OFFS 0x0204
#define PSB_HIGH_REG_OFFS 0x0600

#define PSB_NUM_VBLANKS 2
#define PSB_WATCHDOG_DELAY (DRM_HZ * 2)
#define PSB_LID_DELAY (DRM_HZ / 10)

#define MDFLD_PNW_A0 0x00
#define MDFLD_PNW_B0 0x04
#define MDFLD_PNW_C0 0x08

#define MDFLD_DSR_2D_3D_0 BIT0
#define MDFLD_DSR_2D_3D_2 BIT1
#define MDFLD_DSR_CURSOR_0 BIT2
#define MDFLD_DSR_CURSOR_2 BIT3
#define MDFLD_DSR_OVERLAY_0 BIT4
#define MDFLD_DSR_OVERLAY_2 BIT5
#define MDFLD_DSR_MIPI_CONTROL	BIT6
#define MDFLD_DSR_2D_3D 	(MDFLD_DSR_2D_3D_0 | MDFLD_DSR_2D_3D_2)

#define MDFLD_DSR_RR 45
#define MDFLD_DPU_ENABLE BIT31
#define MDFLD_DSR_FULLSCREEN BIT30
#define MDFLD_DSR_DELAY (DRM_HZ / MDFLD_DSR_RR)

#define PSB_PWR_STATE_ON		1
#define PSB_PWR_STATE_OFF		2

#define PSB_PMPOLICY_NOPM		0
#define PSB_PMPOLICY_CLOCKGATING	1
#define PSB_PMPOLICY_POWERDOWN		2

#define PSB_PMSTATE_POWERUP		0
#define PSB_PMSTATE_CLOCKGATED		1
#define PSB_PMSTATE_POWERDOWN		2
#define PSB_PCIx_MSI_ADDR_LOC		0x94
#define PSB_PCIx_MSI_DATA_LOC		0x98

#define MDFLD_PLANE_MAX_WIDTH		2048
#define MDFLD_PLANE_MAX_HEIGHT		2048

struct opregion_header;
struct opregion_acpi;
struct opregion_swsci;
struct opregion_asle;

struct psb_intel_opregion {
	struct opregion_header *header;
	struct opregion_acpi *acpi;
	struct opregion_swsci *swsci;
	struct opregion_asle *asle;
	int enabled;
};

/*
 *User options.
 */

struct drm_psb_uopt {
	int pad; /*keep it here in case we use it in future*/
};

/**
 *struct psb_context
 *
 *@buffers:	 array of pre-allocated validate buffers.
 *@used_buffers: number of buffers in @buffers array currently in use.
 *@validate_buffer: buffers validated from user-space.
 *@kern_validate_buffers : buffers validated from kernel-space.
 *@fence_flags : Fence flags to be used for fence creation.
 *
 *This structure is used during execbuf validation.
 */

struct psb_context {
	struct psb_validate_buffer *buffers;
	uint32_t used_buffers;
	struct list_head validate_list;
	struct list_head kern_validate_list;
	uint32_t fence_types;
	uint32_t val_seq;
};

struct psb_validate_buffer;

/* Currently defined profiles */
enum VAProfile {
	VAProfileMPEG2Simple		= 0,
	VAProfileMPEG2Main		= 1,
	VAProfileMPEG4Simple		= 2,
	VAProfileMPEG4AdvancedSimple	= 3,
	VAProfileMPEG4Main		= 4,
	VAProfileH264Baseline		= 5,
	VAProfileH264Main		= 6,
	VAProfileH264High		= 7,
	VAProfileVC1Simple		= 8,
	VAProfileVC1Main		= 9,
	VAProfileVC1Advanced		= 10,
	VAProfileH263Baseline		= 11,
	VAProfileJPEGBaseline           = 12,
	VAProfileH264ConstrainedBaseline = 13
};

/* Currently defined entrypoints */
enum VAEntrypoint {
	VAEntrypointVLD		= 1,
	VAEntrypointIZZ		= 2,
	VAEntrypointIDCT	= 3,
	VAEntrypointMoComp	= 4,
	VAEntrypointDeblocking	= 5,
	VAEntrypointEncSlice	= 6,	/* slice level encode */
	VAEntrypointEncPicture 	= 7	/* pictuer encode, JPEG, etc */
};


struct psb_video_ctx {
	struct list_head head;
	struct file *filp; /* DRM device file pointer */
	int ctx_type; /* profile<<8|entrypoint */
	/* todo: more context specific data for multi-context support */
};

#define MODE_SETTING_IN_CRTC 	0x1
#define MODE_SETTING_IN_ENCODER 0x2
#define MODE_SETTING_ON_GOING 	0x3
#define MODE_SETTING_IN_DSR 	0x4
#define MODE_SETTING_ENCODER_DONE 0x8
#define GCT_R10_HEADER_SIZE	16
#define GCT_R10_DISPLAY_DESC_SIZE	28

struct drm_psb_private {
	/*
	 * DSI info.
	 */
	void * dbi_dsr_info;
	void * dsi_configs[2];

	/*
	 *TTM Glue.
	 */

	struct drm_global_reference mem_global_ref;
	struct ttm_bo_global_ref bo_global_ref;
	int has_global;

	struct drm_device *dev;
	struct ttm_object_device *tdev;
	struct ttm_fence_device fdev;
	struct ttm_bo_device bdev;
	struct ttm_lock ttm_lock;
	struct vm_operations_struct *ttm_vm_ops;
	int has_fence_device;
	int has_bo_device;

	unsigned long chipset;

	struct drm_psb_uopt uopt;

	struct psb_gtt *pg;

	/*GTT Memory manager*/
	struct psb_gtt_mm *gtt_mm;

	struct page *scratch_page;
	uint32_t sequence[PSB_NUM_ENGINES];
	uint32_t last_sequence[PSB_NUM_ENGINES];
	uint32_t last_submitted_seq[PSB_NUM_ENGINES];

	struct psb_mmu_driver *mmu;
	struct psb_mmu_pd *pf_pd;

	uint8_t *sgx_reg;
	uint8_t *vdc_reg;
	uint32_t gatt_free_offset;

	/* IMG video context */
	struct list_head video_ctx;



	/*
	 *Fencing / irq.
	 */

	uint32_t vdc_irq_mask;
	uint32_t pipestat[PSB_NUM_PIPE];
	bool vblanksEnabledForFlips;

	spinlock_t irqmask_lock;
	spinlock_t sequence_lock;

	/*
	 *Modesetting
	 */
	struct psb_intel_mode_device mode_dev;

	struct drm_crtc *plane_to_crtc_mapping[PSB_NUM_PIPE];
	struct drm_crtc *pipe_to_crtc_mapping[PSB_NUM_PIPE];
	uint32_t num_pipe;

	/*
	 * CI share buffer
	 */
	unsigned int ci_region_start;
	unsigned int ci_region_size;

	/*
	 * RAR share buffer;
	 */
	unsigned int rar_region_start;
	unsigned int rar_region_size;

	/*
	 *Memory managers
	 */

	int have_camera;
	int have_rar;
	int have_tt;
	int have_mem_mmu;
	struct mutex temp_mem;

	/*
	 *Relocation buffer mapping.
	 */

	spinlock_t reloc_lock;
	unsigned int rel_mapped_pages;
	wait_queue_head_t rel_mapped_queue;

	/*
	 *SAREA
	 */
	struct drm_psb_sarea *sarea_priv;

	/*
	*OSPM info
	*/
	uint32_t ospm_base;

	/*
	 * Sizes info
	 */

	struct drm_psb_sizes_arg sizes;

	uint32_t fuse_reg_value;

	/* pci revision id for B0:D2:F0 */
	uint8_t platform_rev_id;

	/*
	 *LVDS info
	 */
	int backlight_duty_cycle;	/* restore backlight to this value */
	bool panel_wants_dither;
	struct drm_display_mode *panel_fixed_mode;
	struct drm_display_mode *lfp_lvds_vbt_mode;
	struct drm_display_mode *sdvo_lvds_vbt_mode;

	struct bdb_lvds_backlight *lvds_bl; /*LVDS backlight info from VBT*/
	struct psb_intel_i2c_chan *lvds_i2c_bus;

	/* Feature bits from the VBIOS*/
	unsigned int int_tv_support:1;
	unsigned int lvds_dither:1;
	unsigned int lvds_vbt:1;
	unsigned int int_crt_support:1;
	unsigned int lvds_use_ssc:1;
	int lvds_ssc_freq;
	bool is_lvds_on;

	unsigned int core_freq;
	uint32_t iLVDS_enable;

	/*runtime PM state*/
	int rpm_enabled;

	/*
	 *Register state
	 */
	uint32_t saveDSPACNTR;
	uint32_t saveDSPBCNTR;
	uint32_t savePIPEACONF;
	uint32_t savePIPEBCONF;
	uint32_t savePIPEASRC;
	uint32_t savePIPEBSRC;
	uint32_t saveFPA0;
	uint32_t saveFPA1;
	uint32_t saveDPLL_A;
	uint32_t saveDPLL_A_MD;
	uint32_t saveHTOTAL_A;
	uint32_t saveHBLANK_A;
	uint32_t saveHSYNC_A;
	uint32_t saveVTOTAL_A;
	uint32_t saveVBLANK_A;
	uint32_t saveVSYNC_A;
	uint32_t saveDSPASTRIDE;
	uint32_t saveDSPASIZE;
	uint32_t saveDSPAPOS;
	uint32_t saveDSPABASE;
	uint32_t saveDSPASURF;
	uint32_t saveFPB0;
	uint32_t saveFPB1;
	uint32_t saveDPLL_B;
	uint32_t saveDPLL_B_MD;
	uint32_t saveHTOTAL_B;
	uint32_t saveHBLANK_B;
	uint32_t saveHSYNC_B;
	uint32_t saveVTOTAL_B;
	uint32_t saveVBLANK_B;
	uint32_t saveVSYNC_B;
	uint32_t saveDSPBSTRIDE;
	uint32_t saveDSPBSIZE;
	uint32_t saveDSPBPOS;
	uint32_t saveDSPBBASE;
	uint32_t saveDSPBSURF;
	uint32_t saveVCLK_DIVISOR_VGA0;
	uint32_t saveVCLK_DIVISOR_VGA1;
	uint32_t saveVCLK_POST_DIV;
	uint32_t saveVGACNTRL;
	uint32_t saveADPA;
	uint32_t saveLVDS;
	uint32_t saveDVOA;
	uint32_t saveDVOB;
	uint32_t saveDVOC;
	uint32_t savePP_ON;
	uint32_t savePP_OFF;
	uint32_t savePP_CONTROL;
	uint32_t savePP_CYCLE;
	uint32_t savePFIT_CONTROL;
	uint32_t savePaletteA[256];
	uint32_t savePaletteB[256];
	uint32_t saveBLC_PWM_CTL2;
	uint32_t saveBLC_PWM_CTL;
	uint32_t saveCLOCKGATING;
	uint32_t saveDSPARB;
	uint32_t saveDSPATILEOFF;
	uint32_t saveDSPBTILEOFF;
	uint32_t saveDSPAADDR;
	uint32_t saveDSPBADDR;
	uint32_t savePFIT_AUTO_RATIOS;
	uint32_t savePFIT_PGM_RATIOS;
	uint32_t savePP_ON_DELAYS;
	uint32_t savePP_OFF_DELAYS;
	uint32_t savePP_DIVISOR;
	uint32_t saveBSM;
	uint32_t saveVBT;
	uint32_t saveBCLRPAT_A;
	uint32_t saveBCLRPAT_B;
	uint32_t saveDSPALINOFF;
	uint32_t saveDSPBLINOFF;
	uint32_t savePERF_MODE;
	uint32_t saveDSPFW1;
	uint32_t saveDSPFW2;
	uint32_t saveDSPFW3;
	uint32_t saveDSPFW4;
	uint32_t saveDSPFW5;
	uint32_t saveDSPFW6;
	uint32_t saveCHICKENBIT;
	uint32_t saveDSPACURSOR_CTRL;
	uint32_t saveDSPBCURSOR_CTRL;
	uint32_t saveDSPACURSOR_BASE;
	uint32_t saveDSPBCURSOR_BASE;
	uint32_t saveDSPACURSOR_POS;
	uint32_t saveDSPBCURSOR_POS;
	uint32_t save_palette_a[256];
	uint32_t save_palette_b[256];
	uint32_t saveOV_OVADD;
	uint32_t saveOV_OGAMC0;
	uint32_t saveOV_OGAMC1;
	uint32_t saveOV_OGAMC2;
	uint32_t saveOV_OGAMC3;
	uint32_t saveOV_OGAMC4;
	uint32_t saveOV_OGAMC5;
	uint32_t saveOVC_OVADD;
	uint32_t saveOVC_OGAMC0;
	uint32_t saveOVC_OGAMC1;
	uint32_t saveOVC_OGAMC2;
	uint32_t saveOVC_OGAMC3;
	uint32_t saveOVC_OGAMC4;
	uint32_t saveOVC_OGAMC5;

	/*
	 * extra MDFLD Register state
	 */
	uint32_t saveHDMIPHYMISCCTL;
	uint32_t saveHDMIB_CONTROL;
	uint32_t saveDSPCCNTR;
	uint32_t savePIPECCONF;
	uint32_t savePIPECSRC;
	uint32_t saveHTOTAL_C;
	uint32_t saveHBLANK_C;
	uint32_t saveHSYNC_C;
	uint32_t saveVTOTAL_C;
	uint32_t saveVBLANK_C;
	uint32_t saveVSYNC_C;
	uint32_t saveDSPCSTRIDE;
	uint32_t saveDSPCSIZE;
	uint32_t saveDSPCPOS;
	uint32_t saveDSPCSURF;
	uint32_t saveDSPCLINOFF;
	uint32_t saveDSPCTILEOFF;
	uint32_t saveDSPCCURSOR_CTRL;
	uint32_t saveDSPCCURSOR_BASE;
	uint32_t saveDSPCCURSOR_POS;
	uint32_t save_palette_c[256];
	uint32_t saveOV_OVADD_C;
	uint32_t saveOV_OGAMC0_C;
	uint32_t saveOV_OGAMC1_C;
	uint32_t saveOV_OGAMC2_C;
	uint32_t saveOV_OGAMC3_C;
	uint32_t saveOV_OGAMC4_C;
	uint32_t saveOV_OGAMC5_C;

	/* DSI reg save */
	uint32_t saveDEVICE_READY_REG;
	uint32_t saveINTR_EN_REG;
	uint32_t saveDSI_FUNC_PRG_REG;
	uint32_t saveHS_TX_TIMEOUT_REG;
	uint32_t saveLP_RX_TIMEOUT_REG;
	uint32_t saveTURN_AROUND_TIMEOUT_REG;
	uint32_t saveDEVICE_RESET_REG;
	uint32_t saveDPI_RESOLUTION_REG;
	uint32_t saveHORIZ_SYNC_PAD_COUNT_REG;
	uint32_t saveHORIZ_BACK_PORCH_COUNT_REG;
	uint32_t saveHORIZ_FRONT_PORCH_COUNT_REG;
	uint32_t saveHORIZ_ACTIVE_AREA_COUNT_REG;
	uint32_t saveVERT_SYNC_PAD_COUNT_REG;
	uint32_t saveVERT_BACK_PORCH_COUNT_REG;
	uint32_t saveVERT_FRONT_PORCH_COUNT_REG;
	uint32_t saveHIGH_LOW_SWITCH_COUNT_REG;
	uint32_t saveINIT_COUNT_REG;
	uint32_t saveMAX_RET_PAK_REG;
	uint32_t saveVIDEO_FMT_REG;
	uint32_t saveEOT_DISABLE_REG;
	uint32_t saveLP_BYTECLK_REG;
	uint32_t saveHS_LS_DBI_ENABLE_REG;
	uint32_t saveTXCLKESC_REG;
	uint32_t saveDPHY_PARAM_REG;
	uint32_t saveMIPI_CONTROL_REG;
	uint32_t saveMIPI;
	uint32_t saveMIPI_C;
	void (*init_drvIC)(struct drm_device *dev);
	void (*dsi_prePowerState)(struct drm_device *dev);
	void (*dsi_postPowerState)(struct drm_device *dev);

	/* DPST Register Save */
	uint32_t saveHISTOGRAM_INT_CONTROL_REG;
	uint32_t saveHISTOGRAM_LOGIC_CONTROL_REG;
	uint32_t savePWM_CONTROL_LOGIC;

	/* MSI reg save */

	uint32_t msi_addr;
	uint32_t msi_data;

	/*
	 *Scheduling.
	 */

	struct mutex reset_mutex;
	struct mutex cmdbuf_mutex;
	/*uint32_t ta_mem_pages;
	struct psb_ta_mem *ta_mem;
	int force_ta_mem_load;*/
	atomic_t val_seq;

	/*
	 *TODO: change this to be per drm-context.
	 */

	struct psb_context context;

	/*
	 * LID-Switch
	 */
	spinlock_t lid_lock;
	struct timer_list lid_timer;
	struct psb_intel_opregion opregion;
	u32 *lid_state;
	u32 lid_last_state;

	/*
	 *Watchdog
	 */

	int timer_available;

	uint32_t apm_reg;
	uint16_t apm_base;

	/*
	 * Used for modifying backlight from
	 * xrandr -- consider removing and using HAL instead
	 */
	struct drm_property *backlight_property;
	uint32_t blc_adj1;
	uint32_t blc_adj2;

	void * fbdev;
};


struct psb_file_data {	/* TODO: Audit this, remove the indirection and set
			   it up properly in open/postclose  ACFIXME */
	void *priv;
};

struct psb_fpriv {
	struct ttm_object_file *tfile;
};

struct psb_mmu_driver;

extern int drm_crtc_probe_output_modes(struct drm_device *dev, int, int);
extern int drm_pick_crtcs(struct drm_device *dev);

static inline struct psb_fpriv *psb_fpriv(struct drm_file *file_priv)
{
	struct psb_file_data *pvr_file_priv
			= (struct psb_file_data *)file_priv->driver_priv;
	return (struct psb_fpriv *) pvr_file_priv->priv;
}

static inline struct drm_psb_private *psb_priv(struct drm_device *dev)
{
	return (struct drm_psb_private *) dev->dev_private;
}

/*
 *TTM glue. psb_ttm_glue.c
 */

extern int psb_open(struct inode *inode, struct file *filp);
extern int psb_release(struct inode *inode, struct file *filp);
extern int psb_mmap(struct file *filp, struct vm_area_struct *vma);

extern int psb_fence_signaled_ioctl(struct drm_device *dev, void *data,
				    struct drm_file *file_priv);
extern int psb_verify_access(struct ttm_buffer_object *bo,
			     struct file *filp);
extern ssize_t psb_ttm_read(struct file *filp, char __user *buf,
			    size_t count, loff_t *f_pos);
extern ssize_t psb_ttm_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *f_pos);
extern int psb_fence_finish_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
extern int psb_fence_unref_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
extern int psb_pl_waitidle_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file_priv);
extern int psb_pl_setstatus_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
extern int psb_pl_synccpu_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
extern int psb_pl_unref_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
extern int psb_pl_reference_ioctl(struct drm_device *dev, void *data,
				  struct drm_file *file_priv);
extern int psb_pl_create_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);
extern int psb_pl_ub_create_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);
extern int psb_extension_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);
extern int psb_ttm_global_init(struct drm_psb_private *dev_priv);
extern void psb_ttm_global_release(struct drm_psb_private *dev_priv);
extern int psb_getpageaddrs_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv);
/*
 *MMU stuff.
 */

extern struct psb_mmu_driver *psb_mmu_driver_init(uint8_t __iomem * registers,
					int trap_pagefaults,
					int invalid_type,
					struct drm_psb_private *dev_priv);
extern void psb_mmu_driver_takedown(struct psb_mmu_driver *driver);
extern struct psb_mmu_pd *psb_mmu_get_default_pd(struct psb_mmu_driver
						 *driver);
extern void psb_mmu_mirror_gtt(struct psb_mmu_pd *pd, uint32_t mmu_offset,
			       uint32_t gtt_start, uint32_t gtt_pages);
extern struct psb_mmu_pd *psb_mmu_alloc_pd(struct psb_mmu_driver *driver,
					   int trap_pagefaults,
					   int invalid_type);
extern void psb_mmu_free_pagedir(struct psb_mmu_pd *pd);
extern void psb_mmu_flush(struct psb_mmu_driver *driver, int rc_prot);
extern void psb_mmu_remove_pfn_sequence(struct psb_mmu_pd *pd,
					unsigned long address,
					uint32_t num_pages);
extern int psb_mmu_insert_pfn_sequence(struct psb_mmu_pd *pd,
				       uint32_t start_pfn,
				       unsigned long address,
				       uint32_t num_pages, int type);
extern int psb_mmu_virtual_to_pfn(struct psb_mmu_pd *pd, uint32_t virtual,
				  unsigned long *pfn);

/*
 *Enable / disable MMU for different requestors.
 */


extern void psb_mmu_set_pd_context(struct psb_mmu_pd *pd, int hw_context);
extern int psb_mmu_insert_pages(struct psb_mmu_pd *pd, struct page **pages,
				unsigned long address, uint32_t num_pages,
				uint32_t desired_tile_stride,
				uint32_t hw_tile_stride, int type);
extern void psb_mmu_remove_pages(struct psb_mmu_pd *pd,
				 unsigned long address, uint32_t num_pages,
				 uint32_t desired_tile_stride,
				 uint32_t hw_tile_stride);
/*
 *psb_sgx.c
 */



extern int psb_cmdbuf_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *file_priv);
extern int psb_reg_submit(struct drm_psb_private *dev_priv,
			  uint32_t *regs, unsigned int cmds);


extern void psb_fence_or_sync(struct drm_file *file_priv,
			      uint32_t engine,
			      uint32_t fence_types,
			      uint32_t fence_flags,
			      struct list_head *list,
			      struct psb_ttm_fence_rep *fence_arg,
			      struct ttm_fence_object **fence_p);
extern int psb_validate_kernel_buffer(struct psb_context *context,
				      struct ttm_buffer_object *bo,
				      uint32_t fence_class,
				      uint64_t set_flags,
				      uint64_t clr_flags);

/*
 *psb_irq.c
 */

extern irqreturn_t psb_irq_handler(DRM_IRQ_ARGS);
extern int psb_irq_enable_dpst(struct drm_device *dev);
extern int psb_irq_disable_dpst(struct drm_device *dev);
extern void psb_irq_preinstall(struct drm_device *dev);
extern int psb_irq_postinstall(struct drm_device *dev);
extern void psb_irq_uninstall(struct drm_device *dev);
extern void psb_irq_preinstall_islands(struct drm_device *dev, int hw_islands);
extern int psb_irq_postinstall_islands(struct drm_device *dev, int hw_islands);
extern void psb_irq_turn_on_dpst(struct drm_device *dev);
extern void psb_irq_turn_off_dpst(struct drm_device *dev);

extern void psb_irq_uninstall_islands(struct drm_device *dev, int hw_islands);
extern int psb_vblank_wait2(struct drm_device *dev,unsigned int *sequence);
extern int psb_vblank_wait(struct drm_device *dev, unsigned int *sequence);
extern int psb_enable_vblank(struct drm_device *dev, int crtc);
extern void psb_disable_vblank(struct drm_device *dev, int crtc);
void
psb_enable_pipestat(struct drm_psb_private *dev_priv, int pipe, u32 mask);

void
psb_disable_pipestat(struct drm_psb_private *dev_priv, int pipe, u32 mask);

extern u32 psb_get_vblank_counter(struct drm_device *dev, int crtc);

/*
 *psb_fence.c
 */

extern void psb_fence_handler(struct drm_device *dev, uint32_t class);

extern int psb_fence_emit_sequence(struct ttm_fence_device *fdev,
				   uint32_t fence_class,
				   uint32_t flags, uint32_t *sequence,
				   unsigned long *timeout_jiffies);
extern void psb_fence_error(struct drm_device *dev,
			    uint32_t class,
			    uint32_t sequence, uint32_t type, int error);
extern int psb_ttm_fence_device_init(struct ttm_fence_device *fdev);

/* MSVDX/Topaz stuff */
extern int psb_remove_videoctx(struct drm_psb_private *dev_priv, struct file *filp);

extern int lnc_video_frameskip(struct drm_device *dev,
			       uint64_t user_pointer);
extern int lnc_video_getparam(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);

/*
 * psb_opregion.c
 */
extern int psb_intel_opregion_init(struct drm_device *dev);

/*
 *psb_fb.c
 */
extern int psbfb_probed(struct drm_device *dev);
extern int psbfb_remove(struct drm_device *dev,
			struct drm_framebuffer *fb);
extern int psbfb_kms_off_ioctl(struct drm_device *dev, void *data,
			       struct drm_file *file_priv);
extern int psbfb_kms_on_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *file_priv);
extern void *psbfb_vdc_reg(struct drm_device* dev);

/*
 * psb_2d.c
 */
extern void psbfb_fillrect(struct fb_info *info,
					const struct fb_fillrect *rect);
extern void psbfb_copyarea(struct fb_info *info,
					const struct fb_copyarea *region);
extern void psbfb_imageblit(struct fb_info *info,
					const struct fb_image *image);
extern int psbfb_sync(struct fb_info *info);

extern void psb_spank(struct drm_psb_private *dev_priv);

/*
 *psb_reset.c
 */

extern void psb_lid_timer_init(struct drm_psb_private *dev_priv);
extern void psb_lid_timer_takedown(struct drm_psb_private *dev_priv);
extern void psb_print_pagefault(struct drm_psb_private *dev_priv);

/* modesetting */
extern void psb_modeset_init(struct drm_device *dev);
extern void psb_modeset_cleanup(struct drm_device *dev);
extern int psb_fbdev_init(struct drm_device * dev);

/* psb_bl.c */
int psb_backlight_init(struct drm_device *dev);
void psb_backlight_exit(void);
int psb_set_brightness(struct backlight_device *bd);
int psb_get_brightness(struct backlight_device *bd);
struct backlight_device * psb_get_backlight_device(void);

/*
 *Debug print bits setting
 */
#define PSB_D_GENERAL (1 << 0)
#define PSB_D_INIT    (1 << 1)
#define PSB_D_IRQ     (1 << 2)
#define PSB_D_ENTRY   (1 << 3)
/* debug the get H/V BP/FP count */
#define PSB_D_HV      (1 << 4)
#define PSB_D_DBI_BF  (1 << 5)
#define PSB_D_PM      (1 << 6)
#define PSB_D_RENDER  (1 << 7)
#define PSB_D_REG     (1 << 8)
#define PSB_D_MSVDX   (1 << 9)
#define PSB_D_TOPAZ   (1 << 10)

#ifndef DRM_DEBUG_CODE
/* To enable debug printout, set drm_psb_debug in psb_drv.c
 * to any combination of above print flags.
 */
/* #define DRM_DEBUG_CODE 2 */
#endif

extern int drm_psb_debug;
extern int drm_psb_no_fb;
extern int drm_psb_disable_vsync;
extern int drm_idle_check_interval;

#define PSB_DEBUG_GENERAL(_fmt, _arg...) \
	PSB_DEBUG(PSB_D_GENERAL, _fmt, ##_arg)
#define PSB_DEBUG_INIT(_fmt, _arg...) \
	PSB_DEBUG(PSB_D_INIT, _fmt, ##_arg)
#define PSB_DEBUG_IRQ(_fmt, _arg...) \
	PSB_DEBUG(PSB_D_IRQ, _fmt, ##_arg)
#define PSB_DEBUG_ENTRY(_fmt, _arg...) \
	PSB_DEBUG(PSB_D_ENTRY, _fmt, ##_arg)
#define PSB_DEBUG_HV(_fmt, _arg...) \
	PSB_DEBUG(PSB_D_HV, _fmt, ##_arg)
#define PSB_DEBUG_DBI_BF(_fmt, _arg...) \
	PSB_DEBUG(PSB_D_DBI_BF, _fmt, ##_arg)
#define PSB_DEBUG_PM(_fmt, _arg...) \
	PSB_DEBUG(PSB_D_PM, _fmt, ##_arg)
#define PSB_DEBUG_RENDER(_fmt, _arg...) \
	PSB_DEBUG(PSB_D_RENDER, _fmt, ##_arg)
#define PSB_DEBUG_REG(_fmt, _arg...) \
	PSB_DEBUG(PSB_D_REG, _fmt, ##_arg)
#define PSB_DEBUG_MSVDX(_fmt, _arg...) \
	PSB_DEBUG(PSB_D_MSVDX, _fmt, ##_arg)
#define PSB_DEBUG_TOPAZ(_fmt, _arg...) \
	PSB_DEBUG(PSB_D_TOPAZ, _fmt, ##_arg)

#if DRM_DEBUG_CODE
#define PSB_DEBUG(_flag, _fmt, _arg...)					\
	do {								\
		if (unlikely((_flag) & drm_psb_debug))			\
			printk(KERN_DEBUG				\
			       "[psb:0x%02x:%s] " _fmt , _flag,		\
			       __func__ , ##_arg);			\
	} while (0)
#else
#define PSB_DEBUG(_fmt, _arg...)     do { } while (0)
#endif

/*
 *Utilities
 */
#define DRM_DRIVER_PRIVATE_T struct drm_psb_private

static inline u32 MRST_MSG_READ32(uint port, uint offset)
{
	int mcr = (0xD0<<24) | (port << 16) | (offset << 8);
	uint32_t ret_val = 0;
	struct pci_dev *pci_root = pci_get_bus_and_slot (0, 0);
	pci_write_config_dword (pci_root, 0xD0, mcr);
	pci_read_config_dword (pci_root, 0xD4, &ret_val);
	pci_dev_put(pci_root);
	return ret_val;
}
static inline void MRST_MSG_WRITE32(uint port, uint offset, u32 value)
{
	int mcr = (0xE0<<24) | (port << 16) | (offset << 8) | 0xF0;
	struct pci_dev *pci_root = pci_get_bus_and_slot (0, 0);
	pci_write_config_dword (pci_root, 0xD4, value);
	pci_write_config_dword (pci_root, 0xD0, mcr);
	pci_dev_put(pci_root);
}
static inline u32 MDFLD_MSG_READ32(uint port, uint offset)
{
	int mcr = (0x10<<24) | (port << 16) | (offset << 8);
	uint32_t ret_val = 0;
	struct pci_dev *pci_root = pci_get_bus_and_slot (0, 0);
	pci_write_config_dword (pci_root, 0xD0, mcr);
	pci_read_config_dword (pci_root, 0xD4, &ret_val);
	pci_dev_put(pci_root);
	return ret_val;
}
static inline void MDFLD_MSG_WRITE32(uint port, uint offset, u32 value)
{
	int mcr = (0x11<<24) | (port << 16) | (offset << 8) | 0xF0;
	struct pci_dev *pci_root = pci_get_bus_and_slot (0, 0);
	pci_write_config_dword (pci_root, 0xD4, value);
	pci_write_config_dword (pci_root, 0xD0, mcr);
	pci_dev_put(pci_root);
}

static inline uint32_t REGISTER_READ(struct drm_device *dev, uint32_t reg)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	int reg_val = ioread32(dev_priv->vdc_reg + (reg));
	PSB_DEBUG_REG("reg = 0x%x. reg_val = 0x%x. \n", reg, reg_val);
	return reg_val;
}

#define REG_READ(reg)	       REGISTER_READ(dev, (reg))
static inline void REGISTER_WRITE(struct drm_device *dev, uint32_t reg,
				      uint32_t val)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	if ((reg < 0x70084 || reg >0x70088) && (reg < 0xa000 || reg >0xa3ff))
		PSB_DEBUG_REG("reg = 0x%x, val = 0x%x. \n", reg, val);

	iowrite32((val), dev_priv->vdc_reg + (reg));
}

#define REG_WRITE(reg, val)	REGISTER_WRITE(dev, (reg), (val))

static inline void REGISTER_WRITE16(struct drm_device *dev,
					uint32_t reg, uint32_t val)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	PSB_DEBUG_REG("reg = 0x%x, val = 0x%x. \n", reg, val);

	iowrite16((val), dev_priv->vdc_reg + (reg));
}

#define REG_WRITE16(reg, val)	  REGISTER_WRITE16(dev, (reg), (val))

static inline void REGISTER_WRITE8(struct drm_device *dev,
				       uint32_t reg, uint32_t val)
{
	struct drm_psb_private *dev_priv = dev->dev_private;

	PSB_DEBUG_REG("reg = 0x%x, val = 0x%x. \n", reg, val);

	iowrite8((val), dev_priv->vdc_reg + (reg));
}

#define REG_WRITE8(reg, val)	 REGISTER_WRITE8(dev, (reg), (val))

#define PSB_ALIGN_TO(_val, _align) \
  (((_val) + ((_align) - 1)) & ~((_align) - 1))
#define PSB_WVDC32(_val, _offs) \
  iowrite32(_val, dev_priv->vdc_reg + (_offs))
#define PSB_RVDC32(_offs) \
  ioread32(dev_priv->vdc_reg + (_offs))

/* #define TRAP_SGX_PM_FAULT 1 */
#ifdef TRAP_SGX_PM_FAULT
#define PSB_RSGX32(_offs)					\
({								\
    if (inl(dev_priv->apm_base + PSB_APM_STS) & 0x3) {		\
	printk(KERN_ERR "access sgx when it's off!! (READ) %s, %d\n", \
	       __FILE__, __LINE__);				\
	mdelay(1000);						\
    }								\
    ioread32(dev_priv->sgx_reg + (_offs));			\
})
#else
#define PSB_RSGX32(_offs)					\
  ioread32(dev_priv->sgx_reg + (_offs))
#endif
#define PSB_WSGX32(_val, _offs) \
  iowrite32(_val, dev_priv->sgx_reg + (_offs))

#define MSVDX_REG_DUMP 0
#if MSVDX_REG_DUMP

#define PSB_WMSVDX32(_val, _offs) \
  printk("MSVDX: write %08x to reg 0x%08x\n", (unsigned int)(_val), (unsigned int)(_offs));\
  iowrite32(_val, dev_priv->msvdx_reg + (_offs))
#define PSB_RMSVDX32(_offs) \
  ioread32(dev_priv->msvdx_reg + (_offs))

#else

#define PSB_WMSVDX32(_val, _offs) \
  iowrite32(_val, dev_priv->msvdx_reg + (_offs))
#define PSB_RMSVDX32(_offs) \
  ioread32(dev_priv->msvdx_reg + (_offs))

#endif

#define PSB_ALPL(_val, _base)			\
  (((_val) >> (_base ## _ALIGNSHIFT)) << (_base ## _SHIFT))
#define PSB_ALPLM(_val, _base)			\
  ((((_val) >> (_base ## _ALIGNSHIFT)) << (_base ## _SHIFT)) & (_base ## _MASK))

#endif
