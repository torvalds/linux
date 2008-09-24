/*
 *  PS3 platform declarations.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if !defined(_ASM_POWERPC_PS3_H)
#define _ASM_POWERPC_PS3_H

#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include "cell-pmu.h"

union ps3_firmware_version {
	u64 raw;
	struct {
		u16 pad;
		u16 major;
		u16 minor;
		u16 rev;
	};
};

void ps3_get_firmware_version(union ps3_firmware_version *v);
int ps3_compare_firmware_version(u16 major, u16 minor, u16 rev);

/* 'Other OS' area */

enum ps3_param_av_multi_out {
	PS3_PARAM_AV_MULTI_OUT_NTSC = 0,
	PS3_PARAM_AV_MULTI_OUT_PAL_RGB = 1,
	PS3_PARAM_AV_MULTI_OUT_PAL_YCBCR = 2,
	PS3_PARAM_AV_MULTI_OUT_SECAM = 3,
};

enum ps3_param_av_multi_out ps3_os_area_get_av_multi_out(void);

/* dma routines */

enum ps3_dma_page_size {
	PS3_DMA_4K = 12U,
	PS3_DMA_64K = 16U,
	PS3_DMA_1M = 20U,
	PS3_DMA_16M = 24U,
};

enum ps3_dma_region_type {
	PS3_DMA_OTHER = 0,
	PS3_DMA_INTERNAL = 2,
};

struct ps3_dma_region_ops;

/**
 * struct ps3_dma_region - A per device dma state variables structure
 * @did: The HV device id.
 * @page_size: The ioc pagesize.
 * @region_type: The HV region type.
 * @bus_addr: The 'translated' bus address of the region.
 * @len: The length in bytes of the region.
 * @offset: The offset from the start of memory of the region.
 * @ioid: The IOID of the device who owns this region
 * @chunk_list: Opaque variable used by the ioc page manager.
 * @region_ops: struct ps3_dma_region_ops - dma region operations
 */

struct ps3_dma_region {
	struct ps3_system_bus_device *dev;
	/* device variables */
	const struct ps3_dma_region_ops *region_ops;
	unsigned char ioid;
	enum ps3_dma_page_size page_size;
	enum ps3_dma_region_type region_type;
	unsigned long len;
	unsigned long offset;

	/* driver variables  (set by ps3_dma_region_create) */
	unsigned long bus_addr;
	struct {
		spinlock_t lock;
		struct list_head head;
	} chunk_list;
};

struct ps3_dma_region_ops {
	int (*create)(struct ps3_dma_region *);
	int (*free)(struct ps3_dma_region *);
	int (*map)(struct ps3_dma_region *,
		   unsigned long virt_addr,
		   unsigned long len,
		   unsigned long *bus_addr,
		   u64 iopte_pp);
	int (*unmap)(struct ps3_dma_region *,
		     unsigned long bus_addr,
		     unsigned long len);
};
/**
 * struct ps3_dma_region_init - Helper to initialize structure variables
 *
 * Helper to properly initialize variables prior to calling
 * ps3_system_bus_device_register.
 */

struct ps3_system_bus_device;

int ps3_dma_region_init(struct ps3_system_bus_device *dev,
	struct ps3_dma_region *r, enum ps3_dma_page_size page_size,
	enum ps3_dma_region_type region_type, void *addr, unsigned long len);
int ps3_dma_region_create(struct ps3_dma_region *r);
int ps3_dma_region_free(struct ps3_dma_region *r);
int ps3_dma_map(struct ps3_dma_region *r, unsigned long virt_addr,
	unsigned long len, unsigned long *bus_addr,
	u64 iopte_pp);
int ps3_dma_unmap(struct ps3_dma_region *r, unsigned long bus_addr,
	unsigned long len);

/* mmio routines */

enum ps3_mmio_page_size {
	PS3_MMIO_4K = 12U,
	PS3_MMIO_64K = 16U
};

struct ps3_mmio_region_ops;
/**
 * struct ps3_mmio_region - a per device mmio state variables structure
 *
 * Current systems can be supported with a single region per device.
 */

struct ps3_mmio_region {
	struct ps3_system_bus_device *dev;
	const struct ps3_mmio_region_ops *mmio_ops;
	unsigned long bus_addr;
	unsigned long len;
	enum ps3_mmio_page_size page_size;
	unsigned long lpar_addr;
};

struct ps3_mmio_region_ops {
	int (*create)(struct ps3_mmio_region *);
	int (*free)(struct ps3_mmio_region *);
};
/**
 * struct ps3_mmio_region_init - Helper to initialize structure variables
 *
 * Helper to properly initialize variables prior to calling
 * ps3_system_bus_device_register.
 */

int ps3_mmio_region_init(struct ps3_system_bus_device *dev,
	struct ps3_mmio_region *r, unsigned long bus_addr, unsigned long len,
	enum ps3_mmio_page_size page_size);
int ps3_mmio_region_create(struct ps3_mmio_region *r);
int ps3_free_mmio_region(struct ps3_mmio_region *r);
unsigned long ps3_mm_phys_to_lpar(unsigned long phys_addr);

/* inrerrupt routines */

enum ps3_cpu_binding {
	PS3_BINDING_CPU_ANY = -1,
	PS3_BINDING_CPU_0 = 0,
	PS3_BINDING_CPU_1 = 1,
};

int ps3_irq_plug_setup(enum ps3_cpu_binding cpu, unsigned long outlet,
	unsigned int *virq);
int ps3_irq_plug_destroy(unsigned int virq);
int ps3_event_receive_port_setup(enum ps3_cpu_binding cpu, unsigned int *virq);
int ps3_event_receive_port_destroy(unsigned int virq);
int ps3_send_event_locally(unsigned int virq);

int ps3_io_irq_setup(enum ps3_cpu_binding cpu, unsigned int interrupt_id,
	unsigned int *virq);
int ps3_io_irq_destroy(unsigned int virq);
int ps3_vuart_irq_setup(enum ps3_cpu_binding cpu, void* virt_addr_bmp,
	unsigned int *virq);
int ps3_vuart_irq_destroy(unsigned int virq);
int ps3_spe_irq_setup(enum ps3_cpu_binding cpu, unsigned long spe_id,
	unsigned int class, unsigned int *virq);
int ps3_spe_irq_destroy(unsigned int virq);

int ps3_sb_event_receive_port_setup(struct ps3_system_bus_device *dev,
	enum ps3_cpu_binding cpu, unsigned int *virq);
int ps3_sb_event_receive_port_destroy(struct ps3_system_bus_device *dev,
	unsigned int virq);

/* lv1 result codes */

enum lv1_result {
	LV1_SUCCESS                     = 0,
	/* not used                       -1 */
	LV1_RESOURCE_SHORTAGE           = -2,
	LV1_NO_PRIVILEGE                = -3,
	LV1_DENIED_BY_POLICY            = -4,
	LV1_ACCESS_VIOLATION            = -5,
	LV1_NO_ENTRY                    = -6,
	LV1_DUPLICATE_ENTRY             = -7,
	LV1_TYPE_MISMATCH               = -8,
	LV1_BUSY                        = -9,
	LV1_EMPTY                       = -10,
	LV1_WRONG_STATE                 = -11,
	/* not used                       -12 */
	LV1_NO_MATCH                    = -13,
	LV1_ALREADY_CONNECTED           = -14,
	LV1_UNSUPPORTED_PARAMETER_VALUE = -15,
	LV1_CONDITION_NOT_SATISFIED     = -16,
	LV1_ILLEGAL_PARAMETER_VALUE     = -17,
	LV1_BAD_OPTION                  = -18,
	LV1_IMPLEMENTATION_LIMITATION   = -19,
	LV1_NOT_IMPLEMENTED             = -20,
	LV1_INVALID_CLASS_ID            = -21,
	LV1_CONSTRAINT_NOT_SATISFIED    = -22,
	LV1_ALIGNMENT_ERROR             = -23,
	LV1_HARDWARE_ERROR              = -24,
	LV1_INVALID_DATA_FORMAT         = -25,
	LV1_INVALID_OPERATION           = -26,
	LV1_INTERNAL_ERROR              = -32768,
};

static inline const char* ps3_result(int result)
{
#if defined(DEBUG)
	switch (result) {
	case LV1_SUCCESS:
		return "LV1_SUCCESS (0)";
	case -1:
		return "** unknown result ** (-1)";
	case LV1_RESOURCE_SHORTAGE:
		return "LV1_RESOURCE_SHORTAGE (-2)";
	case LV1_NO_PRIVILEGE:
		return "LV1_NO_PRIVILEGE (-3)";
	case LV1_DENIED_BY_POLICY:
		return "LV1_DENIED_BY_POLICY (-4)";
	case LV1_ACCESS_VIOLATION:
		return "LV1_ACCESS_VIOLATION (-5)";
	case LV1_NO_ENTRY:
		return "LV1_NO_ENTRY (-6)";
	case LV1_DUPLICATE_ENTRY:
		return "LV1_DUPLICATE_ENTRY (-7)";
	case LV1_TYPE_MISMATCH:
		return "LV1_TYPE_MISMATCH (-8)";
	case LV1_BUSY:
		return "LV1_BUSY (-9)";
	case LV1_EMPTY:
		return "LV1_EMPTY (-10)";
	case LV1_WRONG_STATE:
		return "LV1_WRONG_STATE (-11)";
	case -12:
		return "** unknown result ** (-12)";
	case LV1_NO_MATCH:
		return "LV1_NO_MATCH (-13)";
	case LV1_ALREADY_CONNECTED:
		return "LV1_ALREADY_CONNECTED (-14)";
	case LV1_UNSUPPORTED_PARAMETER_VALUE:
		return "LV1_UNSUPPORTED_PARAMETER_VALUE (-15)";
	case LV1_CONDITION_NOT_SATISFIED:
		return "LV1_CONDITION_NOT_SATISFIED (-16)";
	case LV1_ILLEGAL_PARAMETER_VALUE:
		return "LV1_ILLEGAL_PARAMETER_VALUE (-17)";
	case LV1_BAD_OPTION:
		return "LV1_BAD_OPTION (-18)";
	case LV1_IMPLEMENTATION_LIMITATION:
		return "LV1_IMPLEMENTATION_LIMITATION (-19)";
	case LV1_NOT_IMPLEMENTED:
		return "LV1_NOT_IMPLEMENTED (-20)";
	case LV1_INVALID_CLASS_ID:
		return "LV1_INVALID_CLASS_ID (-21)";
	case LV1_CONSTRAINT_NOT_SATISFIED:
		return "LV1_CONSTRAINT_NOT_SATISFIED (-22)";
	case LV1_ALIGNMENT_ERROR:
		return "LV1_ALIGNMENT_ERROR (-23)";
	case LV1_HARDWARE_ERROR:
		return "LV1_HARDWARE_ERROR (-24)";
	case LV1_INVALID_DATA_FORMAT:
		return "LV1_INVALID_DATA_FORMAT (-25)";
	case LV1_INVALID_OPERATION:
		return "LV1_INVALID_OPERATION (-26)";
	case LV1_INTERNAL_ERROR:
		return "LV1_INTERNAL_ERROR (-32768)";
	default:
		BUG();
		return "** unknown result **";
	};
#else
	return "";
#endif
}

/* system bus routines */

enum ps3_match_id {
	PS3_MATCH_ID_EHCI           = 1,
	PS3_MATCH_ID_OHCI           = 2,
	PS3_MATCH_ID_GELIC          = 3,
	PS3_MATCH_ID_AV_SETTINGS    = 4,
	PS3_MATCH_ID_SYSTEM_MANAGER = 5,
	PS3_MATCH_ID_STOR_DISK      = 6,
	PS3_MATCH_ID_STOR_ROM       = 7,
	PS3_MATCH_ID_STOR_FLASH     = 8,
	PS3_MATCH_ID_SOUND          = 9,
	PS3_MATCH_ID_GRAPHICS       = 10,
	PS3_MATCH_ID_LPM            = 11,
};

#define PS3_MODULE_ALIAS_EHCI           "ps3:1"
#define PS3_MODULE_ALIAS_OHCI           "ps3:2"
#define PS3_MODULE_ALIAS_GELIC          "ps3:3"
#define PS3_MODULE_ALIAS_AV_SETTINGS    "ps3:4"
#define PS3_MODULE_ALIAS_SYSTEM_MANAGER "ps3:5"
#define PS3_MODULE_ALIAS_STOR_DISK      "ps3:6"
#define PS3_MODULE_ALIAS_STOR_ROM       "ps3:7"
#define PS3_MODULE_ALIAS_STOR_FLASH     "ps3:8"
#define PS3_MODULE_ALIAS_SOUND          "ps3:9"
#define PS3_MODULE_ALIAS_GRAPHICS       "ps3:10"
#define PS3_MODULE_ALIAS_LPM            "ps3:11"

enum ps3_system_bus_device_type {
	PS3_DEVICE_TYPE_IOC0 = 1,
	PS3_DEVICE_TYPE_SB,
	PS3_DEVICE_TYPE_VUART,
	PS3_DEVICE_TYPE_LPM,
};

enum ps3_match_sub_id {
	/* for PS3_MATCH_ID_GRAPHICS */
	PS3_MATCH_SUB_ID_FB		= 1,
};

/**
 * struct ps3_system_bus_device - a device on the system bus
 */

struct ps3_system_bus_device {
	enum ps3_match_id match_id;
	enum ps3_match_sub_id match_sub_id;
	enum ps3_system_bus_device_type dev_type;

	u64 bus_id;                       /* SB */
	u64 dev_id;                       /* SB */
	unsigned int interrupt_id;        /* SB */
	struct ps3_dma_region *d_region;  /* SB, IOC0 */
	struct ps3_mmio_region *m_region; /* SB, IOC0*/
	unsigned int port_number;         /* VUART */
	struct {                          /* LPM */
		u64 node_id;
		u64 pu_id;
		u64 rights;
	} lpm;

/*	struct iommu_table *iommu_table; -- waiting for BenH's cleanups */
	struct device core;
	void *driver_priv; /* private driver variables */
};

int ps3_open_hv_device(struct ps3_system_bus_device *dev);
int ps3_close_hv_device(struct ps3_system_bus_device *dev);

/**
 * struct ps3_system_bus_driver - a driver for a device on the system bus
 */

struct ps3_system_bus_driver {
	enum ps3_match_id match_id;
	enum ps3_match_sub_id match_sub_id;
	struct device_driver core;
	int (*probe)(struct ps3_system_bus_device *);
	int (*remove)(struct ps3_system_bus_device *);
	int (*shutdown)(struct ps3_system_bus_device *);
/*	int (*suspend)(struct ps3_system_bus_device *, pm_message_t); */
/*	int (*resume)(struct ps3_system_bus_device *); */
};

int ps3_system_bus_device_register(struct ps3_system_bus_device *dev);
int ps3_system_bus_driver_register(struct ps3_system_bus_driver *drv);
void ps3_system_bus_driver_unregister(struct ps3_system_bus_driver *drv);

static inline struct ps3_system_bus_driver *ps3_drv_to_system_bus_drv(
	struct device_driver *_drv)
{
	return container_of(_drv, struct ps3_system_bus_driver, core);
}
static inline struct ps3_system_bus_device *ps3_dev_to_system_bus_dev(
	struct device *_dev)
{
	return container_of(_dev, struct ps3_system_bus_device, core);
}
static inline struct ps3_system_bus_driver *
	ps3_system_bus_dev_to_system_bus_drv(struct ps3_system_bus_device *_dev)
{
	BUG_ON(!_dev);
	BUG_ON(!_dev->core.driver);
	return ps3_drv_to_system_bus_drv(_dev->core.driver);
}

/**
 * ps3_system_bus_set_drvdata -
 * @dev: device structure
 * @data: Data to set
 */

static inline void ps3_system_bus_set_driver_data(
	struct ps3_system_bus_device *dev, void *data)
{
	dev->core.driver_data = data;
}
static inline void *ps3_system_bus_get_driver_data(
	struct ps3_system_bus_device *dev)
{
	return dev->core.driver_data;
}

/* These two need global scope for get_dma_ops(). */

extern struct bus_type ps3_system_bus_type;

/* system manager */

struct ps3_sys_manager_ops {
	struct ps3_system_bus_device *dev;
	void (*power_off)(struct ps3_system_bus_device *dev);
	void (*restart)(struct ps3_system_bus_device *dev);
};

void ps3_sys_manager_register_ops(const struct ps3_sys_manager_ops *ops);
void __noreturn ps3_sys_manager_power_off(void);
void __noreturn ps3_sys_manager_restart(void);
void __noreturn ps3_sys_manager_halt(void);
int ps3_sys_manager_get_wol(void);
void ps3_sys_manager_set_wol(int state);

struct ps3_prealloc {
    const char *name;
    void *address;
    unsigned long size;
    unsigned long align;
};

extern struct ps3_prealloc ps3fb_videomemory;
extern struct ps3_prealloc ps3flash_bounce_buffer;

/* logical performance monitor */

/**
 * enum ps3_lpm_rights - Rigths granted by the system policy module.
 *
 * @PS3_LPM_RIGHTS_USE_LPM: The right to use the lpm.
 * @PS3_LPM_RIGHTS_USE_TB: The right to use the internal trace buffer.
 */

enum ps3_lpm_rights {
	PS3_LPM_RIGHTS_USE_LPM = 0x001,
	PS3_LPM_RIGHTS_USE_TB = 0x100,
};

/**
 * enum ps3_lpm_tb_type - Type of trace buffer lv1 should use.
 *
 * @PS3_LPM_TB_TYPE_NONE: Do not use a trace buffer.
 * @PS3_LPM_RIGHTS_USE_TB: Use the lv1 internal trace buffer.  Must have
 *  rights @PS3_LPM_RIGHTS_USE_TB.
 */

enum ps3_lpm_tb_type {
	PS3_LPM_TB_TYPE_NONE = 0,
	PS3_LPM_TB_TYPE_INTERNAL = 1,
};

int ps3_lpm_open(enum ps3_lpm_tb_type tb_type, void *tb_cache,
	u64 tb_cache_size);
int ps3_lpm_close(void);
int ps3_lpm_copy_tb(unsigned long offset, void *buf, unsigned long count,
	unsigned long *bytes_copied);
int ps3_lpm_copy_tb_to_user(unsigned long offset, void __user *buf,
	unsigned long count, unsigned long *bytes_copied);
void ps3_set_bookmark(u64 bookmark);
void ps3_set_pm_bookmark(u64 tag, u64 incident, u64 th_id);
int ps3_set_signal(u64 rtas_signal_group, u8 signal_bit, u16 sub_unit,
	u8 bus_word);

u32 ps3_read_phys_ctr(u32 cpu, u32 phys_ctr);
void ps3_write_phys_ctr(u32 cpu, u32 phys_ctr, u32 val);
u32 ps3_read_ctr(u32 cpu, u32 ctr);
void ps3_write_ctr(u32 cpu, u32 ctr, u32 val);

u32 ps3_read_pm07_control(u32 cpu, u32 ctr);
void ps3_write_pm07_control(u32 cpu, u32 ctr, u32 val);
u32 ps3_read_pm(u32 cpu, enum pm_reg_name reg);
void ps3_write_pm(u32 cpu, enum pm_reg_name reg, u32 val);

u32 ps3_get_ctr_size(u32 cpu, u32 phys_ctr);
void ps3_set_ctr_size(u32 cpu, u32 phys_ctr, u32 ctr_size);

void ps3_enable_pm(u32 cpu);
void ps3_disable_pm(u32 cpu);
void ps3_enable_pm_interrupts(u32 cpu, u32 thread, u32 mask);
void ps3_disable_pm_interrupts(u32 cpu);

u32 ps3_get_and_clear_pm_interrupts(u32 cpu);
void ps3_sync_irq(int node);
u32 ps3_get_hw_thread_id(int cpu);
u64 ps3_get_spe_id(void *arg);

#endif
