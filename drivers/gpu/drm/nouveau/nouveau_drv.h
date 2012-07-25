/*
 * Copyright 2005 Stephane Marchesin.
 * All Rights Reserved.
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __NOUVEAU_DRV_H__
#define __NOUVEAU_DRV_H__

#define DRIVER_AUTHOR		"Stephane Marchesin"
#define DRIVER_EMAIL		"nouveau@lists.freedesktop.org"

#define DRIVER_NAME		"nouveau"
#define DRIVER_DESC		"nVidia Riva/TNT/GeForce"
#define DRIVER_DATE		"20120316"

#define DRIVER_MAJOR		1
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

#define NOUVEAU_FAMILY   0x0000FFFF
#define NOUVEAU_FLAGS    0xFFFF0000

#include "ttm/ttm_bo_api.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_placement.h"
#include "ttm/ttm_memory.h"
#include "ttm/ttm_module.h"

#define XXX_THIS_IS_A_HACK
#include <subdev/vm.h>
#include <subdev/fb.h>
#include <core/gpuobj.h>

enum blah {
	NV_MEM_TYPE_UNKNOWN = 0,
	NV_MEM_TYPE_STOLEN,
	NV_MEM_TYPE_SGRAM,
	NV_MEM_TYPE_SDRAM,
	NV_MEM_TYPE_DDR1,
	NV_MEM_TYPE_DDR2,
	NV_MEM_TYPE_DDR3,
	NV_MEM_TYPE_GDDR2,
	NV_MEM_TYPE_GDDR3,
	NV_MEM_TYPE_GDDR4,
	NV_MEM_TYPE_GDDR5
};

#include <nouveau_drm.h>
#include "nouveau_reg.h"
#include <nouveau_bios.h>

#include <subdev/bios/pll.h>
#include "nouveau_compat.h"

#define nouveau_gpuobj_new(d,c,s,a,f,o) \
	_nouveau_gpuobj_new((d), NULL, (s), (a), (f), (o))

#define nouveau_vm_new(d,o,l,m,v) \
	_nouveau_vm_new((d), (o), (l), (m), (v))

#define nv50_vm_flush_engine(d,e) \
	_nv50_vm_flush_engine((d), (e))

#include "nouveau_bo.h"
#include "nouveau_gem.h"

struct nouveau_page_flip_state {
	struct list_head head;
	struct drm_pending_vblank_event *event;
	int crtc, bpp, pitch, x, y;
	uint64_t offset;
};

struct nouveau_display_engine {
	void *priv;
	int (*early_init)(struct drm_device *);
	void (*late_takedown)(struct drm_device *);
	int (*create)(struct drm_device *);
	void (*destroy)(struct drm_device *);
	int (*init)(struct drm_device *);
	void (*fini)(struct drm_device *);

	struct drm_property *dithering_mode;
	struct drm_property *dithering_depth;
	struct drm_property *underscan_property;
	struct drm_property *underscan_hborder_property;
	struct drm_property *underscan_vborder_property;
	/* not really hue and saturation: */
	struct drm_property *vibrant_hue_property;
	struct drm_property *color_vibrance_property;
};

struct nouveau_pm_voltage_level {
	u32 voltage; /* microvolts */
	u8  vid;
};

struct nouveau_pm_voltage {
	bool supported;
	u8 version;
	u8 vid_mask;

	struct nouveau_pm_voltage_level *level;
	int nr_level;
};

/* Exclusive upper limits */
#define NV_MEM_CL_DDR2_MAX 8
#define NV_MEM_WR_DDR2_MAX 9
#define NV_MEM_CL_DDR3_MAX 17
#define NV_MEM_WR_DDR3_MAX 17
#define NV_MEM_CL_GDDR3_MAX 16
#define NV_MEM_WR_GDDR3_MAX 18
#define NV_MEM_CL_GDDR5_MAX 21
#define NV_MEM_WR_GDDR5_MAX 20

struct nouveau_pm_memtiming {
	int id;

	u32 reg[9];
	u32 mr[4];

	u8 tCWL;

	u8 odt;
	u8 drive_strength;
};

struct nouveau_pm_tbl_header {
	u8 version;
	u8 header_len;
	u8 entry_cnt;
	u8 entry_len;
};

struct nouveau_pm_tbl_entry {
	u8 tWR;
	u8 tWTR;
	u8 tCL;
	u8 tRC;
	u8 empty_4;
	u8 tRFC;	/* Byte 5 */
	u8 empty_6;
	u8 tRAS;	/* Byte 7 */
	u8 empty_8;
	u8 tRP;		/* Byte 9 */
	u8 tRCDRD;
	u8 tRCDWR;
	u8 tRRD;
	u8 tUNK_13;
	u8 RAM_FT1;		/* 14, a bitmask of random RAM features */
	u8 empty_15;
	u8 tUNK_16;
	u8 empty_17;
	u8 tUNK_18;
	u8 tCWL;
	u8 tUNK_20, tUNK_21;
};

struct nouveau_pm_profile;
struct nouveau_pm_profile_func {
	void (*destroy)(struct nouveau_pm_profile *);
	void (*init)(struct nouveau_pm_profile *);
	void (*fini)(struct nouveau_pm_profile *);
	struct nouveau_pm_level *(*select)(struct nouveau_pm_profile *);
};

struct nouveau_pm_profile {
	const struct nouveau_pm_profile_func *func;
	struct list_head head;
	char name[8];
};

#define NOUVEAU_PM_MAX_LEVEL 8
struct nouveau_pm_level {
	struct nouveau_pm_profile profile;
	struct device_attribute dev_attr;
	char name[32];
	int id;

	struct nouveau_pm_memtiming timing;
	u32 memory;
	u16 memscript;

	u32 core;
	u32 shader;
	u32 rop;
	u32 copy;
	u32 daemon;
	u32 vdec;
	u32 dom6;
	u32 unka0;	/* nva3:nvc0 */
	u32 hub01;	/* nvc0- */
	u32 hub06;	/* nvc0- */
	u32 hub07;	/* nvc0- */

	u32 volt_min; /* microvolts */
	u32 volt_max;
	u8  fanspeed;
};

struct nouveau_pm_temp_sensor_constants {
	u16 offset_constant;
	s16 offset_mult;
	s16 offset_div;
	s16 slope_mult;
	s16 slope_div;
};

struct nouveau_pm_threshold_temp {
	s16 critical;
	s16 down_clock;
	s16 fan_boost;
};

struct nouveau_pm_fan {
	u32 percent;
	u32 min_duty;
	u32 max_duty;
	u32 pwm_freq;
	u32 pwm_divisor;
};

struct nouveau_pm_engine {
	struct nouveau_pm_voltage voltage;
	struct nouveau_pm_level perflvl[NOUVEAU_PM_MAX_LEVEL];
	int nr_perflvl;
	struct nouveau_pm_temp_sensor_constants sensor_constants;
	struct nouveau_pm_threshold_temp threshold_temp;
	struct nouveau_pm_fan fan;

	struct nouveau_pm_profile *profile_ac;
	struct nouveau_pm_profile *profile_dc;
	struct nouveau_pm_profile *profile;
	struct list_head profiles;

	struct nouveau_pm_level boot;
	struct nouveau_pm_level *cur;

	struct device *hwmon;
	struct notifier_block acpi_nb;

	int  (*clocks_get)(struct drm_device *, struct nouveau_pm_level *);
	void *(*clocks_pre)(struct drm_device *, struct nouveau_pm_level *);
	int (*clocks_set)(struct drm_device *, void *);

	int (*voltage_get)(struct drm_device *);
	int (*voltage_set)(struct drm_device *, int voltage);
	int (*pwm_get)(struct drm_device *, int line, u32*, u32*);
	int (*pwm_set)(struct drm_device *, int line, u32, u32);
	int (*temp_get)(struct drm_device *);
};

struct nouveau_engine {
	struct nouveau_display_engine display;
	struct nouveau_pm_engine      pm;
};

enum nouveau_card_type {
	NV_04      = 0x04,
	NV_10      = 0x10,
	NV_20      = 0x20,
	NV_30      = 0x30,
	NV_40      = 0x40,
	NV_50      = 0x50,
	NV_C0      = 0xc0,
	NV_D0      = 0xd0,
	NV_E0      = 0xe0,
};

struct drm_nouveau_private {
	struct drm_device *dev;

	void *newpriv;

	/* the card type, takes NV_* as values */
	enum nouveau_card_type card_type;
	/* exact chipset, derived from NV_PMC_BOOT_0 */
	int chipset;
	u32 crystal;

	/* interrupt handling */
	void (*irq_handler[32])(struct drm_device *);
	bool msi_enabled;

	struct nouveau_engine engine;

	/* For PFIFO and PGRAPH. */
	spinlock_t context_switch_lock;

	struct nvbios vbios;
};

static inline struct drm_nouveau_private *
nouveau_private(struct drm_device *dev)
{
	return dev->dev_private;
}

/* nouveau_drv.c */
extern int nouveau_modeset;
extern int nouveau_duallink;
extern int nouveau_uscript_lvds;
extern int nouveau_uscript_tmds;
extern int nouveau_vram_pushbuf;
extern int nouveau_vram_notify;
extern char *nouveau_vram_type;
extern int nouveau_fbpercrtc;
extern int nouveau_tv_disable;
extern char *nouveau_tv_norm;
extern int nouveau_ignorelid;
extern int nouveau_force_post;
extern int nouveau_override_conntype;
extern char *nouveau_perflvl;
extern int nouveau_perflvl_wr;
extern int nouveau_msi;
extern int nouveau_ctxfw;
extern int nouveau_mxmdcb;

extern int nouveau_pci_suspend(struct pci_dev *pdev, pm_message_t pm_state);
extern int nouveau_pci_resume(struct pci_dev *pdev);

/* nouveau_state.c */
extern int  nouveau_load(struct drm_device *, unsigned long flags);
extern int  nouveau_firstopen(struct drm_device *);
extern void nouveau_lastclose(struct drm_device *);
extern int  nouveau_unload(struct drm_device *);
extern int  nouveau_card_init(struct drm_device *);

/* nouveau_mem.c */
extern int  nouveau_mem_timing_calc(struct drm_device *, u32 freq,
				    struct nouveau_pm_memtiming *);
extern void nouveau_mem_timing_read(struct drm_device *,
				    struct nouveau_pm_memtiming *);

/* nouveau_irq.c */
extern int         nouveau_irq_init(struct drm_device *);
extern void        nouveau_irq_fini(struct drm_device *);
extern irqreturn_t nouveau_irq_handler(DRM_IRQ_ARGS);
extern void        nouveau_irq_register(struct drm_device *, int status_bit,
					void (*)(struct drm_device *));
extern void        nouveau_irq_unregister(struct drm_device *, int status_bit);
extern void        nouveau_irq_preinstall(struct drm_device *);
extern int         nouveau_irq_postinstall(struct drm_device *);
extern void        nouveau_irq_uninstall(struct drm_device *);

/* nouveau_backlight.c */
#ifdef CONFIG_DRM_NOUVEAU_BACKLIGHT
extern int nouveau_backlight_init(struct drm_device *);
extern void nouveau_backlight_exit(struct drm_device *);
#else
static inline int nouveau_backlight_init(struct drm_device *dev)
{
	return 0;
}

static inline void nouveau_backlight_exit(struct drm_device *dev) { }
#endif

/* nouveau_bios.c */
extern int nouveau_bios_init(struct drm_device *);
extern void nouveau_bios_takedown(struct drm_device *dev);
extern int nouveau_run_vbios_init(struct drm_device *);
extern struct dcb_connector_table_entry *
nouveau_bios_connector_entry(struct drm_device *, int index);
extern int nouveau_bios_run_display_table(struct drm_device *, u16 id, int clk,
					  struct dcb_output *, int crtc);
extern bool nouveau_bios_fp_mode(struct drm_device *, struct drm_display_mode *);
extern uint8_t *nouveau_bios_embedded_edid(struct drm_device *);
extern int nouveau_bios_parse_lvds_table(struct drm_device *, int pxclk,
					 bool *dl, bool *if_is_24bit);
extern int run_tmds_table(struct drm_device *, struct dcb_output *,
			  int head, int pxclk);
extern int call_lvds_script(struct drm_device *, struct dcb_output *, int head,
			    enum LVDS_script, int pxclk);
bool bios_encoder_match(struct dcb_output *, u32 hash);

/* nouveau_ttm.c */
int nouveau_ttm_global_init(struct drm_nouveau_private *);
void nouveau_ttm_global_release(struct drm_nouveau_private *);
int nouveau_ttm_mmap(struct file *, struct vm_area_struct *);

/* nouveau_hdmi.c */
void nouveau_hdmi_mode_set(struct drm_encoder *, struct drm_display_mode *);

/* nvd0_display.c */
extern int nvd0_display_create(struct drm_device *);
extern void nvd0_display_destroy(struct drm_device *);
extern int nvd0_display_init(struct drm_device *);
extern void nvd0_display_fini(struct drm_device *);
struct nouveau_bo *nvd0_display_crtc_sema(struct drm_device *, int crtc);
void nvd0_display_flip_stop(struct drm_crtc *);
int nvd0_display_flip_next(struct drm_crtc *, struct drm_framebuffer *,
			   struct nouveau_channel *, u32 swap_interval);


/* nouveau_display.c */
int nouveau_display_create(struct drm_device *dev);
void nouveau_display_destroy(struct drm_device *dev);
int nouveau_display_init(struct drm_device *dev);
void nouveau_display_fini(struct drm_device *dev);
int nouveau_vblank_enable(struct drm_device *dev, int crtc);
void nouveau_vblank_disable(struct drm_device *dev, int crtc);
int nouveau_crtc_page_flip(struct drm_crtc *crtc, struct drm_framebuffer *fb,
			   struct drm_pending_vblank_event *event);
int nouveau_finish_page_flip(struct nouveau_channel *,
			     struct nouveau_page_flip_state *);
int nouveau_display_dumb_create(struct drm_file *, struct drm_device *,
				struct drm_mode_create_dumb *args);
int nouveau_display_dumb_map_offset(struct drm_file *, struct drm_device *,
				    uint32_t handle, uint64_t *offset);
int nouveau_display_dumb_destroy(struct drm_file *, struct drm_device *,
				 uint32_t handle);

#ifndef ioread32_native
#ifdef __BIG_ENDIAN
#define ioread16_native ioread16be
#define iowrite16_native iowrite16be
#define ioread32_native  ioread32be
#define iowrite32_native iowrite32be
#else /* def __BIG_ENDIAN */
#define ioread16_native ioread16
#define iowrite16_native iowrite16
#define ioread32_native  ioread32
#define iowrite32_native iowrite32
#endif /* def __BIG_ENDIAN else */
#endif /* !ioread32_native */

/* register access */
#define nv_rd08 _nv_rd08
#define nv_wr08 _nv_wr08
#define nv_rd32 _nv_rd32
#define nv_wr32 _nv_wr32
#define nv_mask _nv_mask

#define nv_wait(dev, reg, mask, val) \
	nouveau_wait_eq(dev, 2000000000ULL, (reg), (mask), (val))
#define nv_wait_ne(dev, reg, mask, val) \
	nouveau_wait_ne(dev, 2000000000ULL, (reg), (mask), (val))
#define nv_wait_cb(dev, func, data) \
	nouveau_wait_cb(dev, 2000000000ULL, (func), (data))

/*
 * Logging
 * Argument d is (struct drm_device *).
 */
#define NV_PRINTK(level, d, fmt, arg...) \
	printk(level "[" DRM_NAME "] " DRIVER_NAME " %s: " fmt, \
					pci_name(d->pdev), ##arg)
#ifndef NV_DEBUG_NOTRACE
#define NV_DEBUG(d, fmt, arg...) do {                                          \
	if (drm_debug & DRM_UT_DRIVER) {                                       \
		NV_PRINTK(KERN_DEBUG, d, "%s:%d - " fmt, __func__,             \
			  __LINE__, ##arg);                                    \
	}                                                                      \
} while (0)
#define NV_DEBUG_KMS(d, fmt, arg...) do {                                      \
	if (drm_debug & DRM_UT_KMS) {                                          \
		NV_PRINTK(KERN_DEBUG, d, "%s:%d - " fmt, __func__,             \
			  __LINE__, ##arg);                                    \
	}                                                                      \
} while (0)
#else
#define NV_DEBUG(d, fmt, arg...) do {                                          \
	if (drm_debug & DRM_UT_DRIVER)                                         \
		NV_PRINTK(KERN_DEBUG, d, fmt, ##arg);                          \
} while (0)
#define NV_DEBUG_KMS(d, fmt, arg...) do {                                      \
	if (drm_debug & DRM_UT_KMS)                                            \
		NV_PRINTK(KERN_DEBUG, d, fmt, ##arg);                          \
} while (0)
#endif
#define NV_ERROR(d, fmt, arg...) NV_PRINTK(KERN_ERR, d, fmt, ##arg)
#define NV_INFO(d, fmt, arg...) NV_PRINTK(KERN_INFO, d, fmt, ##arg)
#define NV_TRACEWARN(d, fmt, arg...) NV_PRINTK(KERN_NOTICE, d, fmt, ##arg)
#define NV_TRACE(d, fmt, arg...) NV_PRINTK(KERN_INFO, d, fmt, ##arg)
#define NV_WARN(d, fmt, arg...) NV_PRINTK(KERN_WARNING, d, fmt, ##arg)
#define NV_WARNONCE(d, fmt, arg...) do {                                       \
	static int _warned = 0;                                                \
	if (!_warned) {                                                        \
		NV_WARN(d, fmt, ##arg);                                        \
		_warned = 1;                                                   \
	}                                                                      \
} while(0)

static inline bool
nv_two_heads(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	const int impl = dev->pci_device & 0x0ff0;

	if (dev_priv->card_type >= NV_10 && impl != 0x0100 &&
	    impl != 0x0150 && impl != 0x01a0 && impl != 0x0200)
		return true;

	return false;
}

static inline bool
nv_gf4_disp_arch(struct drm_device *dev)
{
	return nv_two_heads(dev) && (dev->pci_device & 0x0ff0) != 0x0110;
}

static inline bool
nv_two_reg_pll(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	const int impl = dev->pci_device & 0x0ff0;

	if (impl == 0x0310 || impl == 0x0340 || dev_priv->card_type >= NV_40)
		return true;
	return false;
}

static inline bool
nv_match_device(struct drm_device *dev, unsigned device,
		unsigned sub_vendor, unsigned sub_device)
{
	return dev->pdev->device == device &&
		dev->pdev->subsystem_vendor == sub_vendor &&
		dev->pdev->subsystem_device == sub_device;
}

static inline struct nv04_display *
nv04_display(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	return dev_priv->engine.display.priv;
}

#endif /* __NOUVEAU_DRV_H__ */
