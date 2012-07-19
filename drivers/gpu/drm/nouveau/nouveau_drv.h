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

struct nouveau_fpriv {
	spinlock_t lock;
	struct list_head channels;
	struct nouveau_vm *vm;
};

static inline struct nouveau_fpriv *
nouveau_fpriv(struct drm_file *file_priv)
{
	return file_priv ? file_priv->driver_priv : NULL;
}

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

#include <nouveau_drm.h>
#include "nouveau_reg.h"
#include <nouveau_bios.h>
#include "nouveau_util.h"

struct nouveau_grctx;
struct nouveau_mem;

#include <subdev/bios/pll.h>
#include "nouveau_compat.h"

#define nouveau_gpuobj_new(d,c,s,a,f,o) \
	_nouveau_gpuobj_new((d), (c) ? ((struct nouveau_channel *)(c))->ramin : NULL, \
			    (s), (a), (f), (o))

#define nouveau_vm_new(d,o,l,m,v) \
	_nouveau_vm_new((d), (o), (l), (m), (v))

#define nv50_vm_flush_engine(d,e) \
	_nv50_vm_flush_engine((d), (e))

#define MAX_NUM_DCB_ENTRIES 16

#define NOUVEAU_MAX_CHANNEL_NR 4096
#define NOUVEAU_MAX_TILE_NR 15

#include "nouveau_bo.h"
#include "nouveau_gem.h"

/* TODO: submit equivalent to TTM generic API upstream? */
static inline void __iomem *
nvbo_kmap_obj_iovirtual(struct nouveau_bo *nvbo)
{
	bool is_iomem;
	void __iomem *ioptr = (void __force __iomem *)ttm_kmap_obj_virtual(
						&nvbo->kmap, &is_iomem);
	WARN_ON_ONCE(ioptr && !is_iomem);
	return ioptr;
}

enum nouveau_flags {
	NV_NFORCE   = 0x10000000,
	NV_NFORCE2  = 0x20000000
};

#define NVOBJ_ENGINE_SW		0
#define NVOBJ_ENGINE_GR		1
#define NVOBJ_ENGINE_CRYPT	2
#define NVOBJ_ENGINE_COPY0	3
#define NVOBJ_ENGINE_COPY1	4
#define NVOBJ_ENGINE_MPEG	5
#define NVOBJ_ENGINE_PPP	NVOBJ_ENGINE_MPEG
#define NVOBJ_ENGINE_BSP	6
#define NVOBJ_ENGINE_VP		7
#define NVOBJ_ENGINE_FIFO	14
#define NVOBJ_ENGINE_NR		16
#define NVOBJ_ENGINE_DISPLAY	(NVOBJ_ENGINE_NR + 0) /*XXX*/

struct nouveau_page_flip_state {
	struct list_head head;
	struct drm_pending_vblank_event *event;
	int crtc, bpp, pitch, x, y;
	uint64_t offset;
};

enum nouveau_channel_mutex_class {
	NOUVEAU_UCHANNEL_MUTEX,
	NOUVEAU_KCHANNEL_MUTEX
};

struct nouveau_channel {
	struct drm_device *dev;
	struct list_head list;
	int id;

	/* references to the channel data structure */
	struct kref ref;
	/* users of the hardware channel resources, the hardware
	 * context will be kicked off when it reaches zero. */
	atomic_t users;
	struct mutex mutex;

	/* owner of this fifo */
	struct drm_file *file_priv;
	/* mapping of the fifo itself */
	struct drm_local_map *map;

	/* mapping of the regs controlling the fifo */
	void __iomem *user;
	uint32_t user_get;
	uint32_t user_get_hi;
	uint32_t user_put;

	/* DMA push buffer */
	struct nouveau_gpuobj *pushbuf;
	struct nouveau_bo     *pushbuf_bo;
	struct nouveau_vma     pushbuf_vma;
	uint64_t               pushbuf_base;

	/* Notifier memory */
	struct nouveau_bo *notifier_bo;
	struct nouveau_vma notifier_vma;
	struct drm_mm notifier_heap;

	/* PFIFO context */
	struct nouveau_gpuobj *engptr;
	struct nouveau_gpuobj *ramfc;

	/* Execution engine contexts */
	void *engctx[NVOBJ_ENGINE_NR];
	void *fence;

	/* NV50 VM */
	struct nouveau_vm     *vm;
	struct nouveau_gpuobj *vm_pd;

	/* Objects */
	struct nouveau_gpuobj *ramin; /* Private instmem */
	struct nouveau_ramht  *ramht; /* Hash table */

	/* GPU object info for stuff used in-kernel (mm_enabled) */
	uint32_t m2mf_ntfy;
	uint32_t vram_handle;
	uint32_t gart_handle;
	bool accel_done;

	/* Push buffer state (only for drm's channel on !mm_enabled) */
	struct {
		int max;
		int free;
		int cur;
		int put;
		/* access via pushbuf_bo */

		int ib_base;
		int ib_max;
		int ib_free;
		int ib_put;
	} dma;

	struct {
		bool active;
		char name[32];
		struct drm_info_list info;
	} debugfs;
};

struct nouveau_exec_engine {
	void (*destroy)(struct drm_device *, int engine);
	int  (*init)(struct drm_device *, int engine);
	int  (*fini)(struct drm_device *, int engine, bool suspend);
	int  (*context_new)(struct nouveau_channel *, int engine);
	void (*context_del)(struct nouveau_channel *, int engine);
	int  (*object_new)(struct nouveau_channel *, int engine,
			   u32 handle, u16 class);
	void (*set_tile_region)(struct drm_device *dev, int i);
	void (*tlb_flush)(struct drm_device *, int engine);
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
	bool noaccel;

	void *newpriv;

	/* the card type, takes NV_* as values */
	enum nouveau_card_type card_type;
	/* exact chipset, derived from NV_PMC_BOOT_0 */
	int chipset;
	int flags;
	u32 crystal;

	struct nouveau_exec_engine *eng[NVOBJ_ENGINE_NR];

	struct list_head classes;

	struct nouveau_bo *vga_ram;

	/* interrupt handling */
	void (*irq_handler[32])(struct drm_device *);
	bool msi_enabled;

	struct {
		struct drm_global_reference mem_global_ref;
		struct ttm_bo_global_ref bo_global_ref;
		struct ttm_bo_device bdev;
		atomic_t validate_sequence;
		int (*move)(struct nouveau_channel *,
			    struct ttm_buffer_object *,
			    struct ttm_mem_reg *, struct ttm_mem_reg *);
	} ttm;

	struct {
		void *func;
		spinlock_t lock;
		struct drm_mm heap;
		struct nouveau_bo *bo;
	} fence;

	struct {
		spinlock_t lock;
		struct nouveau_channel *ptr[NOUVEAU_MAX_CHANNEL_NR];
	} channels;

	struct nouveau_engine engine;
	struct nouveau_channel *channel;

	/* For PFIFO and PGRAPH. */
	spinlock_t context_switch_lock;

	/* VM/PRAMIN flush, legacy PRAMIN aperture */
	spinlock_t vm_lock;

	/* RAMIN configuration, RAMFC, RAMHT and RAMRO offsets */
	struct nouveau_ramht  *ramht;

	struct {
		enum {
			NOUVEAU_GART_NONE = 0,
			NOUVEAU_GART_AGP,	/* AGP */
			NOUVEAU_GART_PDMA,	/* paged dma object */
			NOUVEAU_GART_HW		/* on-chip gart/vm */
		} type;
		uint64_t aper_base;
		uint64_t aper_size;
		uint64_t aper_free;

		struct ttm_backend_func *func;

		struct nouveau_gpuobj *sg_ctxdma;
	} gart_info;

	/* nv10-nv40 tiling regions */
	struct {
		struct nouveau_tile_reg reg[NOUVEAU_MAX_TILE_NR];
		spinlock_t lock;
	} tile;

	uint64_t fb_available_size;
	uint64_t fb_mappable_pages;
	uint64_t fb_aper_free;
	int fb_mtrr;

	/* G8x/G9x virtual address space */
	struct nouveau_vm *chan_vm;

	struct nvbios vbios;
	u8 *mxms;
	struct list_head i2c_ports;

	struct backlight_device *backlight;

	struct {
		struct dentry *channel_root;
	} debugfs;

	struct nouveau_fbdev *nfbdev;
	struct apertures_struct *apertures;
};

static inline struct drm_nouveau_private *
nouveau_private(struct drm_device *dev)
{
	return dev->dev_private;
}

static inline struct drm_nouveau_private *
nouveau_bdev(struct ttm_bo_device *bd)
{
	return container_of(bd, struct drm_nouveau_private, ttm.bdev);
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
extern int nouveau_reg_debug;
extern int nouveau_ignorelid;
extern int nouveau_nofbaccel;
extern int nouveau_noaccel;
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
extern int  nouveau_open(struct drm_device *, struct drm_file *);
extern void nouveau_preclose(struct drm_device *dev, struct drm_file *);
extern void nouveau_postclose(struct drm_device *, struct drm_file *);
extern int  nouveau_load(struct drm_device *, unsigned long flags);
extern int  nouveau_firstopen(struct drm_device *);
extern void nouveau_lastclose(struct drm_device *);
extern int  nouveau_unload(struct drm_device *);
extern bool nouveau_wait_for_idle(struct drm_device *);
extern int  nouveau_card_init(struct drm_device *);

/* nouveau_mem.c */
extern int  nouveau_mem_vram_init(struct drm_device *);
extern void nouveau_mem_vram_fini(struct drm_device *);
extern int  nouveau_mem_gart_init(struct drm_device *);
extern void nouveau_mem_gart_fini(struct drm_device *);
extern void nouveau_mem_close(struct drm_device *);
extern bool nouveau_mem_flags_valid(struct drm_device *, u32 tile_flags);
extern int  nouveau_mem_timing_calc(struct drm_device *, u32 freq,
				    struct nouveau_pm_memtiming *);
extern void nouveau_mem_timing_read(struct drm_device *,
				    struct nouveau_pm_memtiming *);
extern int nouveau_mem_vbios_type(struct drm_device *);
extern struct nouveau_tile_reg *nv10_mem_set_tiling(
	struct drm_device *dev, uint32_t addr, uint32_t size,
	uint32_t pitch, uint32_t flags);
extern void nv10_mem_put_tile_region(struct drm_device *dev,
				     struct nouveau_tile_reg *tile,
				     struct nouveau_fence *fence);
extern const struct ttm_mem_type_manager_func nouveau_vram_manager;
extern const struct ttm_mem_type_manager_func nouveau_gart_manager;
extern const struct ttm_mem_type_manager_func nv04_gart_manager;

/* nouveau_notifier.c */
extern int  nouveau_notifier_init_channel(struct nouveau_channel *);
extern void nouveau_notifier_takedown_channel(struct nouveau_channel *);
extern int  nouveau_notifier_alloc(struct nouveau_channel *, uint32_t handle,
				   int cout, uint32_t start, uint32_t end,
				   uint32_t *offset);

/* nouveau_channel.c */
extern void nouveau_channel_cleanup(struct drm_device *, struct drm_file *);
extern int  nouveau_channel_alloc(struct drm_device *dev,
				  struct nouveau_channel **chan,
				  struct drm_file *file_priv,
				  uint32_t fb_ctxdma, uint32_t tt_ctxdma);
extern struct nouveau_channel *
nouveau_channel_get_unlocked(struct nouveau_channel *);
extern struct nouveau_channel *
nouveau_channel_get(struct drm_file *, int id);
extern void nouveau_channel_put_unlocked(struct nouveau_channel **);
extern void nouveau_channel_put(struct nouveau_channel **);
extern void nouveau_channel_ref(struct nouveau_channel *chan,
				struct nouveau_channel **pchan);
extern int  nouveau_channel_idle(struct nouveau_channel *chan);

/* nouveau_gpuobj.c */
#define NVOBJ_ENGINE_ADD(d, e, p) do {                                         \
	struct drm_nouveau_private *dev_priv = (d)->dev_private;               \
	dev_priv->eng[NVOBJ_ENGINE_##e] = (p);                                 \
} while (0)

#define NVOBJ_ENGINE_DEL(d, e) do {                                            \
	struct drm_nouveau_private *dev_priv = (d)->dev_private;               \
	dev_priv->eng[NVOBJ_ENGINE_##e] = NULL;                                \
} while (0)

#define NVOBJ_CLASS(d, c, e) do {                                              \
	int ret = nouveau_gpuobj_class_new((d), (c), NVOBJ_ENGINE_##e);        \
	if (ret)                                                               \
		return ret;                                                    \
} while (0)

#define NVOBJ_MTHD(d, c, m, e) do {                                            \
	int ret = nouveau_gpuobj_mthd_new((d), (c), (m), (e));                 \
	if (ret)                                                               \
		return ret;                                                    \
} while (0)

extern int  nouveau_gpuobj_class_new(struct drm_device *, u32 class, u32 eng);
extern int  nouveau_gpuobj_mthd_new(struct drm_device *, u32 class, u32 mthd,
				    int (*exec)(struct nouveau_channel *,
						u32 class, u32 mthd, u32 data));
extern int  nouveau_gpuobj_mthd_call(struct nouveau_channel *, u32, u32, u32);
extern int  nouveau_gpuobj_mthd_call2(struct drm_device *, int, u32, u32, u32);
extern int nouveau_gpuobj_channel_init(struct nouveau_channel *,
				       uint32_t vram_h, uint32_t tt_h);
extern void nouveau_gpuobj_channel_takedown(struct nouveau_channel *);
extern int nouveau_gpuobj_dma_new(struct nouveau_channel *, int class,
				  uint64_t offset, uint64_t size, int access,
				  int target, struct nouveau_gpuobj **);
extern int nouveau_gpuobj_gr_new(struct nouveau_channel *, u32 handle, int class);
extern int nv50_gpuobj_dma_new(struct nouveau_channel *, int class, u64 base,
			       u64 size, int target, int access, u32 type,
			       u32 comp, struct nouveau_gpuobj **pobj);
extern void nv50_gpuobj_dma_init(struct nouveau_gpuobj *, u32 offset,
				 int class, u64 base, u64 size, int target,
				 int access, u32 type, u32 comp);

int  nouveau_gpuobj_map_vm(struct nouveau_gpuobj *gpuobj, struct nouveau_vm *vm,
			   u32 flags, struct nouveau_vma *vma);
void nouveau_gpuobj_unmap(struct nouveau_vma *vma);

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

/* nouveau_sgdma.c */
extern int nouveau_sgdma_init(struct drm_device *);
extern void nouveau_sgdma_takedown(struct drm_device *);
extern uint32_t nouveau_sgdma_get_physical(struct drm_device *,
					   uint32_t offset);
extern struct ttm_tt *nouveau_sgdma_create_ttm(struct ttm_bo_device *bdev,
					       unsigned long size,
					       uint32_t page_flags,
					       struct page *dummy_read_page);

/* nouveau_debugfs.c */
#if defined(CONFIG_DRM_NOUVEAU_DEBUG)
extern int  nouveau_debugfs_init(struct drm_minor *);
extern void nouveau_debugfs_takedown(struct drm_minor *);
extern int  nouveau_debugfs_channel_init(struct nouveau_channel *);
extern void nouveau_debugfs_channel_fini(struct nouveau_channel *);
#else
static inline int
nouveau_debugfs_init(struct drm_minor *minor)
{
	return 0;
}

static inline void nouveau_debugfs_takedown(struct drm_minor *minor)
{
}

static inline int
nouveau_debugfs_channel_init(struct nouveau_channel *chan)
{
	return 0;
}

static inline void
nouveau_debugfs_channel_fini(struct nouveau_channel *chan)
{
}
#endif

/* nouveau_dma.c */
extern void nouveau_dma_init(struct nouveau_channel *);
extern int  nouveau_dma_wait(struct nouveau_channel *, int slots, int size);

/* nouveau_acpi.c */
#define ROM_BIOS_PAGE 4096
#if defined(CONFIG_ACPI)
void nouveau_register_dsm_handler(void);
void nouveau_unregister_dsm_handler(void);
void nouveau_switcheroo_optimus_dsm(void);
int nouveau_acpi_get_bios_chunk(uint8_t *bios, int offset, int len);
bool nouveau_acpi_rom_supported(struct pci_dev *pdev);
int nouveau_acpi_edid(struct drm_device *, struct drm_connector *);
#else
static inline void nouveau_register_dsm_handler(void) {}
static inline void nouveau_unregister_dsm_handler(void) {}
static inline void nouveau_switcheroo_optimus_dsm(void) {}
static inline bool nouveau_acpi_rom_supported(struct pci_dev *pdev) { return false; }
static inline int nouveau_acpi_get_bios_chunk(uint8_t *bios, int offset, int len) { return -EINVAL; }
static inline int nouveau_acpi_edid(struct drm_device *dev, struct drm_connector *connector) { return -EINVAL; }
#endif

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

/* nouveau_mxm.c */
int  nouveau_mxm_init(struct drm_device *dev);
void nouveau_mxm_fini(struct drm_device *dev);

/* nouveau_ttm.c */
int nouveau_ttm_global_init(struct drm_nouveau_private *);
void nouveau_ttm_global_release(struct drm_nouveau_private *);
int nouveau_ttm_mmap(struct file *, struct vm_area_struct *);

/* nouveau_hdmi.c */
void nouveau_hdmi_mode_set(struct drm_encoder *, struct drm_display_mode *);

/* nv04_graph.c */
extern int  nv04_graph_create(struct drm_device *);
extern int  nv04_graph_object_new(struct nouveau_channel *, int, u32, u16);
extern int  nv04_graph_mthd_page_flip(struct nouveau_channel *chan,
				      u32 class, u32 mthd, u32 data);
extern struct nouveau_bitfield nv04_graph_nsource[];

/* nv10_graph.c */
extern int  nv10_graph_create(struct drm_device *);
extern struct nouveau_channel *nv10_graph_channel(struct drm_device *);
extern struct nouveau_bitfield nv10_graph_intr[];
extern struct nouveau_bitfield nv10_graph_nstatus[];

/* nv20_graph.c */
extern int  nv20_graph_create(struct drm_device *);

/* nv40_graph.c */
extern int  nv40_graph_create(struct drm_device *);
extern void nv40_grctx_init(struct drm_device *, u32 *size);
extern void nv40_grctx_fill(struct drm_device *, struct nouveau_gpuobj *);

/* nv50_graph.c */
extern int  nv50_graph_create(struct drm_device *);
extern struct nouveau_enum nv50_data_error_names[];
extern int  nv50_graph_isr_chid(struct drm_device *dev, u64 inst);
extern int  nv50_grctx_init(struct drm_device *, u32 *, u32, u32 *, u32 *);
extern void nv50_grctx_fill(struct drm_device *, struct nouveau_gpuobj *);

/* nvc0_graph.c */
extern int  nvc0_graph_create(struct drm_device *);
extern int  nvc0_graph_isr_chid(struct drm_device *dev, u64 inst);

/* nve0_graph.c */
extern int  nve0_graph_create(struct drm_device *);

/* nv84_crypt.c */
extern int  nv84_crypt_create(struct drm_device *);

/* nv98_crypt.c */
extern int  nv98_crypt_create(struct drm_device *dev);

/* nva3_copy.c */
extern int  nva3_copy_create(struct drm_device *dev);

/* nvc0_copy.c */
extern int  nvc0_copy_create(struct drm_device *dev, int engine);

/* nv31_mpeg.c */
extern int  nv31_mpeg_create(struct drm_device *dev);

/* nv50_mpeg.c */
extern int  nv50_mpeg_create(struct drm_device *dev);

/* nv84_bsp.c */
/* nv98_bsp.c */
extern int  nv84_bsp_create(struct drm_device *dev);

/* nv84_vp.c */
/* nv98_vp.c */
extern int  nv84_vp_create(struct drm_device *dev);

/* nv98_ppp.c */
extern int  nv98_ppp_create(struct drm_device *dev);

extern long nouveau_compat_ioctl(struct file *file, unsigned int cmd,
				 unsigned long arg);

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

/* channel control reg access */
static inline u32 nvchan_rd32(struct nouveau_channel *chan, unsigned reg)
{
	return ioread32_native(chan->user + reg);
}

static inline void nvchan_wr32(struct nouveau_channel *chan,
							unsigned reg, u32 val)
{
	iowrite32_native(val, chan->user + reg);
}

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

/* nouveau_reg_debug bitmask */
enum {
	NOUVEAU_REG_DEBUG_MC             = 0x1,
	NOUVEAU_REG_DEBUG_VIDEO          = 0x2,
	NOUVEAU_REG_DEBUG_FB             = 0x4,
	NOUVEAU_REG_DEBUG_EXTDEV         = 0x8,
	NOUVEAU_REG_DEBUG_CRTC           = 0x10,
	NOUVEAU_REG_DEBUG_RAMDAC         = 0x20,
	NOUVEAU_REG_DEBUG_VGACRTC        = 0x40,
	NOUVEAU_REG_DEBUG_RMVIO          = 0x80,
	NOUVEAU_REG_DEBUG_VGAATTR        = 0x100,
	NOUVEAU_REG_DEBUG_EVO            = 0x200,
	NOUVEAU_REG_DEBUG_AUXCH          = 0x400
};

#define NV_REG_DEBUG(type, dev, fmt, arg...) do { \
	if (nouveau_reg_debug & NOUVEAU_REG_DEBUG_##type) \
		NV_PRINTK(KERN_DEBUG, dev, "%s: " fmt, __func__, ##arg); \
} while (0)

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

static inline void *
nv_engine(struct drm_device *dev, int engine)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	return (void *)dev_priv->eng[engine];
}

/* returns 1 if device is one of the nv4x using the 0x4497 object class,
 * helpful to determine a number of other hardware features
 */
static inline int
nv44_graph_class(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if ((dev_priv->chipset & 0xf0) == 0x60)
		return 1;

	return !(0x0baf & (1 << (dev_priv->chipset & 0x0f)));
}

/* memory type/access flags, do not match hardware values */
#define NV_MEM_ACCESS_RO  1
#define NV_MEM_ACCESS_WO  2
#define NV_MEM_ACCESS_RW (NV_MEM_ACCESS_RO | NV_MEM_ACCESS_WO)
#define NV_MEM_ACCESS_SYS 4
#define NV_MEM_ACCESS_VM  8
#define NV_MEM_ACCESS_NOSNOOP 16

#define NV_MEM_TARGET_VRAM        0
#define NV_MEM_TARGET_PCI         1
#define NV_MEM_TARGET_PCI_NOSNOOP 2
#define NV_MEM_TARGET_VM          3
#define NV_MEM_TARGET_GART        4

#define NV_MEM_TYPE_VM 0x7f
#define NV_MEM_COMP_VM 0x03

/* FIFO methods */
#define NV01_SUBCHAN_OBJECT                                          0x00000000
#define NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH                          0x00000010
#define NV84_SUBCHAN_SEMAPHORE_ADDRESS_LOW                           0x00000014
#define NV84_SUBCHAN_SEMAPHORE_SEQUENCE                              0x00000018
#define NV84_SUBCHAN_SEMAPHORE_TRIGGER                               0x0000001c
#define NV84_SUBCHAN_SEMAPHORE_TRIGGER_ACQUIRE_EQUAL                 0x00000001
#define NV84_SUBCHAN_SEMAPHORE_TRIGGER_WRITE_LONG                    0x00000002
#define NV84_SUBCHAN_SEMAPHORE_TRIGGER_ACQUIRE_GEQUAL                0x00000004
#define NVC0_SUBCHAN_SEMAPHORE_TRIGGER_YIELD                         0x00001000
#define NV84_SUBCHAN_NOTIFY_INTR                                     0x00000020
#define NV84_SUBCHAN_WRCACHE_FLUSH                                   0x00000024
#define NV10_SUBCHAN_REF_CNT                                         0x00000050
#define NVSW_SUBCHAN_PAGE_FLIP                                       0x00000054
#define NV11_SUBCHAN_DMA_SEMAPHORE                                   0x00000060
#define NV11_SUBCHAN_SEMAPHORE_OFFSET                                0x00000064
#define NV11_SUBCHAN_SEMAPHORE_ACQUIRE                               0x00000068
#define NV11_SUBCHAN_SEMAPHORE_RELEASE                               0x0000006c
#define NV40_SUBCHAN_YIELD                                           0x00000080

/* NV_SW object class */
#define NV_SW                                                        0x0000506e
#define NV_SW_DMA_VBLSEM                                             0x0000018c
#define NV_SW_VBLSEM_OFFSET                                          0x00000400
#define NV_SW_VBLSEM_RELEASE_VALUE                                   0x00000404
#define NV_SW_VBLSEM_RELEASE                                         0x00000408
#define NV_SW_PAGE_FLIP                                              0x00000500

#endif /* __NOUVEAU_DRV_H__ */
