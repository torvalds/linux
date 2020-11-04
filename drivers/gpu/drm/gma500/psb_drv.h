/* SPDX-License-Identifier: GPL-2.0-only */
/**************************************************************************
 * Copyright (c) 2007-2011, Intel Corporation.
 * All Rights Reserved.
 *
 **************************************************************************/

#ifndef _PSB_DRV_H_
#define _PSB_DRV_H_

#include <linux/kref.h>
#include <linux/mm_types.h>

#include <drm/drm_device.h>

#include "gma_display.h"
#include "gtt.h"
#include "intel_bios.h"
#include "mmu.h"
#include "oaktrail.h"
#include "opregion.h"
#include "power.h"
#include "psb_intel_drv.h"
#include "psb_reg.h"

#define DRIVER_AUTHOR "Alan Cox <alan@linux.intel.com> and others"

#define DRIVER_NAME "gma500"
#define DRIVER_DESC "DRM driver for the Intel GMA500, GMA600, GMA3600, GMA3650"
#define DRIVER_DATE "20140314"

#define DRIVER_MAJOR 1
#define DRIVER_MINOR 0
#define DRIVER_PATCHLEVEL 0

/* Append new drm mode definition here, align with libdrm definition */
#define DRM_MODE_SCALE_NO_SCALE   	2

enum {
	CHIP_PSB_8108 = 0,		/* Poulsbo */
	CHIP_PSB_8109 = 1,		/* Poulsbo */
	CHIP_MRST_4100 = 2,		/* Moorestown/Oaktrail */
	CHIP_MFLD_0130 = 3,		/* Medfield */
};

#define IS_PSB(dev) (((dev)->pdev->device & 0xfffe) == 0x8108)
#define IS_MRST(dev) (((dev)->pdev->device & 0xfff0) == 0x4100)
#define IS_MFLD(dev) (((dev)->pdev->device & 0xfff8) == 0x0130)
#define IS_CDV(dev) (((dev)->pdev->device & 0xfff0) == 0x0be0)

/* Hardware offsets */
#define PSB_VDC_OFFSET		 0x00000000
#define PSB_VDC_SIZE		 0x000080000
#define MRST_MMIO_SIZE		 0x0000C0000
#define MDFLD_MMIO_SIZE          0x000100000
#define PSB_SGX_SIZE		 0x8000
#define PSB_SGX_OFFSET		 0x00040000
#define MRST_SGX_OFFSET		 0x00080000

/* PCI resource identifiers */
#define PSB_MMIO_RESOURCE	 0
#define PSB_AUX_RESOURCE	 0
#define PSB_GATT_RESOURCE	 2
#define PSB_GTT_RESOURCE	 3

/* PCI configuration */
#define PSB_GMCH_CTRL		 0x52
#define PSB_BSM			 0x5C
#define _PSB_GMCH_ENABLED	 0x4
#define PSB_PGETBL_CTL		 0x2020
#define _PSB_PGETBL_ENABLED	 0x00000001
#define PSB_SGX_2D_SLAVE_PORT	 0x4000
#define PSB_LPC_GBA		 0x44

/* TODO: To get rid of */
#define PSB_TT_PRIV0_LIMIT	 (256*1024*1024)
#define PSB_TT_PRIV0_PLIMIT	 (PSB_TT_PRIV0_LIMIT >> PAGE_SHIFT)

/* SGX side MMU definitions (these can probably go) */

/* Flags for external memory type field */
#define PSB_MMU_CACHED_MEMORY	  0x0001	/* Bind to MMU only */
#define PSB_MMU_RO_MEMORY	  0x0002	/* MMU RO memory */
#define PSB_MMU_WO_MEMORY	  0x0004	/* MMU WO memory */

/* PTE's and PDE's */
#define PSB_PDE_MASK		  0x003FFFFF
#define PSB_PDE_SHIFT		  22
#define PSB_PTE_SHIFT		  12

/* Cache control */
#define PSB_PTE_VALID		  0x0001	/* PTE / PDE valid */
#define PSB_PTE_WO		  0x0002	/* Write only */
#define PSB_PTE_RO		  0x0004	/* Read only */
#define PSB_PTE_CACHED		  0x0008	/* CPU cache coherent */

/* VDC registers and bits */
#define PSB_MSVDX_CLOCKGATING	  0x2064
#define PSB_TOPAZ_CLOCKGATING	  0x2068
#define PSB_HWSTAM		  0x2098
#define PSB_INSTPM		  0x20C0
#define PSB_INT_IDENTITY_R        0x20A4
#define _PSB_IRQ_ASLE		  (1<<0)
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
#define _PSB_IRQ_DISP_HOTSYNC	  (1<<17)
#define _PSB_IRQ_SGX_FLAG	  (1<<18)
#define _PSB_IRQ_MSVDX_FLAG	  (1<<19)
#define _LNC_IRQ_TOPAZ_FLAG	  (1<<20)

#define _PSB_PIPE_EVENT_FLAG	(_PSB_VSYNC_PIPEA_FLAG | \
				 _PSB_VSYNC_PIPEB_FLAG)

/* This flag includes all the display IRQ bits excepts the vblank irqs. */
#define _MDFLD_DISP_ALL_IRQ_FLAG (_MDFLD_PIPEC_EVENT_FLAG | \
				  _MDFLD_PIPEB_EVENT_FLAG | \
				  _PSB_PIPEA_EVENT_FLAG | \
				  _PSB_VSYNC_PIPEA_FLAG | \
				  _MDFLD_MIPIA_FLAG | \
				  _MDFLD_MIPIC_FLAG)
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
#define PSB_WATCHDOG_DELAY (HZ * 2)
#define PSB_LID_DELAY (HZ / 10)

#define MDFLD_PNW_B0 0x04
#define MDFLD_PNW_C0 0x08

#define MDFLD_DSR_2D_3D_0 	(1 << 0)
#define MDFLD_DSR_2D_3D_2 	(1 << 1)
#define MDFLD_DSR_CURSOR_0 	(1 << 2)
#define MDFLD_DSR_CURSOR_2	(1 << 3)
#define MDFLD_DSR_OVERLAY_0 	(1 << 4)
#define MDFLD_DSR_OVERLAY_2 	(1 << 5)
#define MDFLD_DSR_MIPI_CONTROL	(1 << 6)
#define MDFLD_DSR_DAMAGE_MASK_0	((1 << 0) | (1 << 2) | (1 << 4))
#define MDFLD_DSR_DAMAGE_MASK_2	((1 << 1) | (1 << 3) | (1 << 5))
#define MDFLD_DSR_2D_3D 	(MDFLD_DSR_2D_3D_0 | MDFLD_DSR_2D_3D_2)

#define MDFLD_DSR_RR		45
#define MDFLD_DPU_ENABLE 	(1 << 31)
#define MDFLD_DSR_FULLSCREEN 	(1 << 30)
#define MDFLD_DSR_DELAY		(HZ / MDFLD_DSR_RR)

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

/* Medfield crystal settings */
#define KSEL_CRYSTAL_19 1
#define KSEL_BYPASS_19 5
#define KSEL_BYPASS_25 6
#define KSEL_BYPASS_83_100 7

struct drm_fb_helper;

struct opregion_header;
struct opregion_acpi;
struct opregion_swsci;
struct opregion_asle;

struct psb_intel_opregion {
	struct opregion_header *header;
	struct opregion_acpi *acpi;
	struct opregion_swsci *swsci;
	struct opregion_asle *asle;
	void *vbt;
	u32 __iomem *lid_state;
	struct work_struct asle_work;
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

struct intel_gmbus {
	struct i2c_adapter adapter;
	struct i2c_adapter *force_bit;
	u32 reg0;
};

/* Register offset maps */
struct psb_offset {
	u32	fp0;
	u32	fp1;
	u32	cntr;
	u32	conf;
	u32	src;
	u32	dpll;
	u32	dpll_md;
	u32	htotal;
	u32	hblank;
	u32	hsync;
	u32	vtotal;
	u32	vblank;
	u32	vsync;
	u32	stride;
	u32	size;
	u32	pos;
	u32	surf;
	u32	addr;
	u32	base;
	u32	status;
	u32	linoff;
	u32	tileoff;
	u32	palette;
};

/*
 *	Register save state. This is used to hold the context when the
 *	device is powered off. In the case of Oaktrail this can (but does not
 *	yet) include screen blank. Operations occuring during the save
 *	update the register cache instead.
 */

/* Common status for pipes */
struct psb_pipe {
	u32	fp0;
	u32	fp1;
	u32	cntr;
	u32	conf;
	u32	src;
	u32	dpll;
	u32	dpll_md;
	u32	htotal;
	u32	hblank;
	u32	hsync;
	u32	vtotal;
	u32	vblank;
	u32	vsync;
	u32	stride;
	u32	size;
	u32	pos;
	u32	base;
	u32	surf;
	u32	addr;
	u32	status;
	u32	linoff;
	u32	tileoff;
	u32	palette[256];
};

struct psb_state {
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
	uint32_t saveCLOCKGATING;
	uint32_t saveDSPARB;
	uint32_t savePFIT_AUTO_RATIOS;
	uint32_t savePFIT_PGM_RATIOS;
	uint32_t savePP_ON_DELAYS;
	uint32_t savePP_OFF_DELAYS;
	uint32_t savePP_DIVISOR;
	uint32_t saveBCLRPAT_A;
	uint32_t saveBCLRPAT_B;
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

	/* DPST register save */
	uint32_t saveHISTOGRAM_INT_CONTROL_REG;
	uint32_t saveHISTOGRAM_LOGIC_CONTROL_REG;
	uint32_t savePWM_CONTROL_LOGIC;
};

struct medfield_state {
	uint32_t saveMIPI;
	uint32_t saveMIPI_C;

	uint32_t savePFIT_CONTROL;
	uint32_t savePFIT_PGM_RATIOS;
	uint32_t saveHDMIPHYMISCCTL;
	uint32_t saveHDMIB_CONTROL;
};

struct cdv_state {
	uint32_t saveDSPCLK_GATE_D;
	uint32_t saveRAMCLK_GATE_D;
	uint32_t saveDSPARB;
	uint32_t saveDSPFW[6];
	uint32_t saveADPA;
	uint32_t savePP_CONTROL;
	uint32_t savePFIT_PGM_RATIOS;
	uint32_t saveLVDS;
	uint32_t savePFIT_CONTROL;
	uint32_t savePP_ON_DELAYS;
	uint32_t savePP_OFF_DELAYS;
	uint32_t savePP_CYCLE;
	uint32_t saveVGACNTRL;
	uint32_t saveIER;
	uint32_t saveIMR;
	u8	 saveLBB;
};

struct psb_save_area {
	struct psb_pipe pipe[3];
	uint32_t saveBSM;
	uint32_t saveVBT;
	union {
	        struct psb_state psb;
		struct medfield_state mdfld;
		struct cdv_state cdv;
	};
	uint32_t saveBLC_PWM_CTL2;
	uint32_t saveBLC_PWM_CTL;
};

struct psb_ops;

#define PSB_NUM_PIPE		3

struct drm_psb_private {
	struct drm_device *dev;
	struct pci_dev *aux_pdev; /* Currently only used by mrst */
	struct pci_dev *lpc_pdev; /* Currently only used by mrst */
	const struct psb_ops *ops;
	const struct psb_offset *regmap;

	struct child_device_config *child_dev;
	int child_dev_num;

	struct psb_gtt gtt;

	/* GTT Memory manager */
	struct psb_gtt_mm *gtt_mm;
	struct page *scratch_page;
	u32 __iomem *gtt_map;
	uint32_t stolen_base;
	u8 __iomem *vram_addr;
	unsigned long vram_stolen_size;
	int gtt_initialized;
	u16 gmch_ctrl;		/* Saved GTT setup */
	u32 pge_ctl;

	struct mutex gtt_mutex;
	struct resource *gtt_mem;	/* Our PCI resource */

	struct mutex mmap_mutex;

	struct psb_mmu_driver *mmu;
	struct psb_mmu_pd *pf_pd;

	/* Register base */
	uint8_t __iomem *sgx_reg;
	uint8_t __iomem *vdc_reg;
	uint8_t __iomem *aux_reg; /* Auxillary vdc pipe regs */
	uint16_t lpc_gpio_base;
	uint32_t gatt_free_offset;

	/* Fencing / irq */
	uint32_t vdc_irq_mask;
	uint32_t pipestat[PSB_NUM_PIPE];

	spinlock_t irqmask_lock;

	/* Power */
	bool suspended;
	bool display_power;
	int display_count;

	/* Modesetting */
	struct psb_intel_mode_device mode_dev;
	bool modeset;	/* true if we have done the mode_device setup */

	struct drm_crtc *plane_to_crtc_mapping[PSB_NUM_PIPE];
	struct drm_crtc *pipe_to_crtc_mapping[PSB_NUM_PIPE];
	uint32_t num_pipe;

	/* OSPM info (Power management base) (TODO: can go ?) */
	uint32_t ospm_base;

	/* Sizes info */
	u32 fuse_reg_value;
	u32 video_device_fuse;

	/* PCI revision ID for B0:D2:F0 */
	uint8_t platform_rev_id;

	/* gmbus */
	struct intel_gmbus *gmbus;
	uint8_t __iomem *gmbus_reg;

	/* Used by SDVO */
	int crt_ddc_pin;
	/* FIXME: The mappings should be parsed from bios but for now we can
		  pretend there are no mappings available */
	struct sdvo_device_mapping sdvo_mappings[2];
	u32 hotplug_supported_mask;
	struct drm_property *broadcast_rgb_property;
	struct drm_property *force_audio_property;

	/* LVDS info */
	int backlight_duty_cycle;	/* restore backlight to this value */
	bool panel_wants_dither;
	struct drm_display_mode *panel_fixed_mode;
	struct drm_display_mode *lfp_lvds_vbt_mode;
	struct drm_display_mode *sdvo_lvds_vbt_mode;

	struct bdb_lvds_backlight *lvds_bl; /* LVDS backlight info from VBT */
	struct psb_intel_i2c_chan *lvds_i2c_bus; /* FIXME: Remove this? */

	/* Feature bits from the VBIOS */
	unsigned int int_tv_support:1;
	unsigned int lvds_dither:1;
	unsigned int lvds_vbt:1;
	unsigned int int_crt_support:1;
	unsigned int lvds_use_ssc:1;
	int lvds_ssc_freq;
	bool is_lvds_on;
	bool is_mipi_on;
	bool lvds_enabled_in_vbt;
	u32 mipi_ctrl_display;

	unsigned int core_freq;
	uint32_t iLVDS_enable;

	/* Runtime PM state */
	int rpm_enabled;

	/* MID specific */
	bool has_gct;
	struct oaktrail_gct_data gct_data;

	/* Oaktrail HDMI state */
	struct oaktrail_hdmi_dev *hdmi_priv;

	/* Register state */
	struct psb_save_area regs;

	/* MSI reg save */
	uint32_t msi_addr;
	uint32_t msi_data;

	/* Hotplug handling */
	struct work_struct hotplug_work;

	/* LID-Switch */
	spinlock_t lid_lock;
	struct timer_list lid_timer;
	struct psb_intel_opregion opregion;
	u32 lid_last_state;

	/* Watchdog */
	uint32_t apm_reg;
	uint16_t apm_base;

	/*
	 * Used for modifying backlight from
	 * xrandr -- consider removing and using HAL instead
	 */
	struct backlight_device *backlight_device;
	struct drm_property *backlight_property;
	bool backlight_enabled;
	int backlight_level;
	uint32_t blc_adj1;
	uint32_t blc_adj2;

	struct drm_fb_helper *fb_helper;

	/* 2D acceleration */
	spinlock_t lock_2d;

	/* Panel brightness */
	int brightness;
	int brightness_adjusted;

	bool dsr_enable;
	u32 dsr_fb_update;
	bool dpi_panel_on[3];
	void *dsi_configs[2];
	u32 bpp;
	u32 bpp2;

	u32 pipeconf[3];
	u32 dspcntr[3];

	int mdfld_panel_id;

	bool dplla_96mhz;	/* DPLL data from the VBT */

	struct {
		int rate;
		int lanes;
		int preemphasis;
		int vswing;

		bool initialized;
		bool support;
		int bpp;
		struct edp_power_seq pps;
	} edp;
	uint8_t panel_type;
};


/* Operations for each board type */
struct psb_ops {
	const char *name;
	unsigned int accel_2d:1;
	int pipes;		/* Number of output pipes */
	int crtcs;		/* Number of CRTCs */
	int sgx_offset;		/* Base offset of SGX device */
	int hdmi_mask;		/* Mask of HDMI CRTCs */
	int lvds_mask;		/* Mask of LVDS CRTCs */
	int sdvo_mask;		/* Mask of SDVO CRTCs */
	int cursor_needs_phys;  /* If cursor base reg need physical address */

	/* Sub functions */
	struct drm_crtc_helper_funcs const *crtc_helper;
	struct drm_crtc_funcs const *crtc_funcs;
	const struct gma_clock_funcs *clock_funcs;

	/* Setup hooks */
	int (*chip_setup)(struct drm_device *dev);
	void (*chip_teardown)(struct drm_device *dev);
	/* Optional helper caller after modeset */
	void (*errata)(struct drm_device *dev);

	/* Display management hooks */
	int (*output_init)(struct drm_device *dev);
	int (*hotplug)(struct drm_device *dev);
	void (*hotplug_enable)(struct drm_device *dev, bool on);
	/* Power management hooks */
	void (*init_pm)(struct drm_device *dev);
	int (*save_regs)(struct drm_device *dev);
	int (*restore_regs)(struct drm_device *dev);
	void (*save_crtc)(struct drm_crtc *crtc);
	void (*restore_crtc)(struct drm_crtc *crtc);
	int (*power_up)(struct drm_device *dev);
	int (*power_down)(struct drm_device *dev);
	void (*update_wm)(struct drm_device *dev, struct drm_crtc *crtc);
	void (*disable_sr)(struct drm_device *dev);

	void (*lvds_bl_power)(struct drm_device *dev, bool on);
#ifdef CONFIG_BACKLIGHT_CLASS_DEVICE
	/* Backlight */
	int (*backlight_init)(struct drm_device *dev);
#endif
	int i2c_bus;		/* I2C bus identifier for Moorestown */
};



extern int drm_crtc_probe_output_modes(struct drm_device *dev, int, int);
extern int drm_pick_crtcs(struct drm_device *dev);

static inline struct drm_psb_private *psb_priv(struct drm_device *dev)
{
	return (struct drm_psb_private *) dev->dev_private;
}

/* psb_irq.c */
extern irqreturn_t psb_irq_handler(int irq, void *arg);
extern int psb_irq_enable_dpst(struct drm_device *dev);
extern int psb_irq_disable_dpst(struct drm_device *dev);
extern void psb_irq_preinstall(struct drm_device *dev);
extern int psb_irq_postinstall(struct drm_device *dev);
extern void psb_irq_uninstall(struct drm_device *dev);
extern void psb_irq_turn_on_dpst(struct drm_device *dev);
extern void psb_irq_turn_off_dpst(struct drm_device *dev);

extern void psb_irq_uninstall_islands(struct drm_device *dev, int hw_islands);
extern int psb_vblank_wait2(struct drm_device *dev, unsigned int *sequence);
extern int psb_vblank_wait(struct drm_device *dev, unsigned int *sequence);
extern int psb_enable_vblank(struct drm_crtc *crtc);
extern void psb_disable_vblank(struct drm_crtc *crtc);
void
psb_enable_pipestat(struct drm_psb_private *dev_priv, int pipe, u32 mask);

void
psb_disable_pipestat(struct drm_psb_private *dev_priv, int pipe, u32 mask);

extern u32 psb_get_vblank_counter(struct drm_crtc *crtc);

/* framebuffer.c */
extern int psbfb_probed(struct drm_device *dev);
extern int psbfb_remove(struct drm_device *dev,
			struct drm_framebuffer *fb);
/* accel_2d.c */
extern void psbfb_copyarea(struct fb_info *info,
					const struct fb_copyarea *region);
extern int psbfb_sync(struct fb_info *info);
extern void psb_spank(struct drm_psb_private *dev_priv);

/* psb_reset.c */
extern void psb_lid_timer_init(struct drm_psb_private *dev_priv);
extern void psb_lid_timer_takedown(struct drm_psb_private *dev_priv);
extern void psb_print_pagefault(struct drm_psb_private *dev_priv);

/* modesetting */
extern void psb_modeset_init(struct drm_device *dev);
extern void psb_modeset_cleanup(struct drm_device *dev);
extern int psb_fbdev_init(struct drm_device *dev);

/* backlight.c */
int gma_backlight_init(struct drm_device *dev);
void gma_backlight_exit(struct drm_device *dev);
void gma_backlight_disable(struct drm_device *dev);
void gma_backlight_enable(struct drm_device *dev);
void gma_backlight_set(struct drm_device *dev, int v);

/* oaktrail_crtc.c */
extern const struct drm_crtc_helper_funcs oaktrail_helper_funcs;

/* oaktrail_lvds.c */
extern void oaktrail_lvds_init(struct drm_device *dev,
		    struct psb_intel_mode_device *mode_dev);

/* psb_intel_display.c */
extern const struct drm_crtc_helper_funcs psb_intel_helper_funcs;
extern const struct drm_crtc_funcs psb_intel_crtc_funcs;

/* psb_intel_lvds.c */
extern const struct drm_connector_helper_funcs
					psb_intel_lvds_connector_helper_funcs;
extern const struct drm_connector_funcs psb_intel_lvds_connector_funcs;

/* gem.c */
extern int psb_gem_get_aperture(struct drm_device *dev, void *data,
			struct drm_file *file);
extern int psb_gem_dumb_create(struct drm_file *file, struct drm_device *dev,
			struct drm_mode_create_dumb *args);

/* psb_device.c */
extern const struct psb_ops psb_chip_ops;

/* oaktrail_device.c */
extern const struct psb_ops oaktrail_chip_ops;

/* mdlfd_device.c */
extern const struct psb_ops mdfld_chip_ops;

/* cdv_device.c */
extern const struct psb_ops cdv_chip_ops;

/* Debug print bits setting */
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

extern int drm_idle_check_interval;

/* Utilities */
static inline u32 MRST_MSG_READ32(int domain, uint port, uint offset)
{
	int mcr = (0xD0<<24) | (port << 16) | (offset << 8);
	uint32_t ret_val = 0;
	struct pci_dev *pci_root = pci_get_domain_bus_and_slot(domain, 0, 0);
	pci_write_config_dword(pci_root, 0xD0, mcr);
	pci_read_config_dword(pci_root, 0xD4, &ret_val);
	pci_dev_put(pci_root);
	return ret_val;
}
static inline void MRST_MSG_WRITE32(int domain, uint port, uint offset,
				    u32 value)
{
	int mcr = (0xE0<<24) | (port << 16) | (offset << 8) | 0xF0;
	struct pci_dev *pci_root = pci_get_domain_bus_and_slot(domain, 0, 0);
	pci_write_config_dword(pci_root, 0xD4, value);
	pci_write_config_dword(pci_root, 0xD0, mcr);
	pci_dev_put(pci_root);
}
static inline u32 MDFLD_MSG_READ32(int domain, uint port, uint offset)
{
	int mcr = (0x10<<24) | (port << 16) | (offset << 8);
	uint32_t ret_val = 0;
	struct pci_dev *pci_root = pci_get_domain_bus_and_slot(domain, 0, 0);
	pci_write_config_dword(pci_root, 0xD0, mcr);
	pci_read_config_dword(pci_root, 0xD4, &ret_val);
	pci_dev_put(pci_root);
	return ret_val;
}
static inline void MDFLD_MSG_WRITE32(int domain, uint port, uint offset,
				     u32 value)
{
	int mcr = (0x11<<24) | (port << 16) | (offset << 8) | 0xF0;
	struct pci_dev *pci_root = pci_get_domain_bus_and_slot(domain, 0, 0);
	pci_write_config_dword(pci_root, 0xD4, value);
	pci_write_config_dword(pci_root, 0xD0, mcr);
	pci_dev_put(pci_root);
}

static inline uint32_t REGISTER_READ(struct drm_device *dev, uint32_t reg)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	return ioread32(dev_priv->vdc_reg + reg);
}

static inline uint32_t REGISTER_READ_AUX(struct drm_device *dev, uint32_t reg)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	return ioread32(dev_priv->aux_reg + reg);
}

#define REG_READ(reg)	       REGISTER_READ(dev, (reg))
#define REG_READ_AUX(reg)      REGISTER_READ_AUX(dev, (reg))

/* Useful for post reads */
static inline uint32_t REGISTER_READ_WITH_AUX(struct drm_device *dev,
					      uint32_t reg, int aux)
{
	uint32_t val;

	if (aux)
		val = REG_READ_AUX(reg);
	else
		val = REG_READ(reg);

	return val;
}

#define REG_READ_WITH_AUX(reg, aux) REGISTER_READ_WITH_AUX(dev, (reg), (aux))

static inline void REGISTER_WRITE(struct drm_device *dev, uint32_t reg,
				  uint32_t val)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	iowrite32((val), dev_priv->vdc_reg + (reg));
}

static inline void REGISTER_WRITE_AUX(struct drm_device *dev, uint32_t reg,
				      uint32_t val)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	iowrite32((val), dev_priv->aux_reg + (reg));
}

#define REG_WRITE(reg, val)	REGISTER_WRITE(dev, (reg), (val))
#define REG_WRITE_AUX(reg, val)	REGISTER_WRITE_AUX(dev, (reg), (val))

static inline void REGISTER_WRITE_WITH_AUX(struct drm_device *dev, uint32_t reg,
				      uint32_t val, int aux)
{
	if (aux)
		REG_WRITE_AUX(reg, val);
	else
		REG_WRITE(reg, val);
}

#define REG_WRITE_WITH_AUX(reg, val, aux) REGISTER_WRITE_WITH_AUX(dev, (reg), (val), (aux))

static inline void REGISTER_WRITE16(struct drm_device *dev,
					uint32_t reg, uint32_t val)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	iowrite16((val), dev_priv->vdc_reg + (reg));
}

#define REG_WRITE16(reg, val)	  REGISTER_WRITE16(dev, (reg), (val))

static inline void REGISTER_WRITE8(struct drm_device *dev,
				       uint32_t reg, uint32_t val)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	iowrite8((val), dev_priv->vdc_reg + (reg));
}

#define REG_WRITE8(reg, val)		REGISTER_WRITE8(dev, (reg), (val))

#define PSB_WVDC32(_val, _offs)		iowrite32(_val, dev_priv->vdc_reg + (_offs))
#define PSB_RVDC32(_offs)		ioread32(dev_priv->vdc_reg + (_offs))

/* #define TRAP_SGX_PM_FAULT 1 */
#ifdef TRAP_SGX_PM_FAULT
#define PSB_RSGX32(_offs)						\
({									\
	if (inl(dev_priv->apm_base + PSB_APM_STS) & 0x3) {		\
		pr_err("access sgx when it's off!! (READ) %s, %d\n",	\
		       __FILE__, __LINE__);				\
		melay(1000);						\
	}								\
	ioread32(dev_priv->sgx_reg + (_offs));				\
})
#else
#define PSB_RSGX32(_offs)		ioread32(dev_priv->sgx_reg + (_offs))
#endif
#define PSB_WSGX32(_val, _offs)		iowrite32(_val, dev_priv->sgx_reg + (_offs))

#define MSVDX_REG_DUMP 0

#define PSB_WMSVDX32(_val, _offs)	iowrite32(_val, dev_priv->msvdx_reg + (_offs))
#define PSB_RMSVDX32(_offs)		ioread32(dev_priv->msvdx_reg + (_offs))

#endif
