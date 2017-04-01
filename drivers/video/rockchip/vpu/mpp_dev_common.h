/**
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 * author: chenhengming chm@rock-chips.com
 *	   Alpha Lin, alpha.lin@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ROCKCHIP_MPP_DEV_COMMON_H
#define __ROCKCHIP_MPP_DEV_COMMON_H

#include <linux/cdev.h>
#include <linux/dma-buf.h>
#include <linux/rockchip_ion.h>
#include <linux/rockchip-iovmm.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/wakelock.h>

extern int mpp_dev_debug;

/*
 * Ioctl definitions
 */

/* Use 'l' as magic number */
#define MPP_IOC_MAGIC			'l'

#define MPP_IOC_SET_CLIENT_TYPE		_IOW(MPP_IOC_MAGIC, 1, u32)
#define MPP_IOC_GET_HW_FUSE_STATUS	_IOW(MPP_IOC_MAGIC, 2, u32)

#define MPP_IOC_SET_REG			_IOW(MPP_IOC_MAGIC, 3, u32)
#define MPP_IOC_GET_REG			_IOW(MPP_IOC_MAGIC, 4, u32)

#define MPP_IOC_PROBE_IOMMU_STATUS	_IOR(MPP_IOC_MAGIC, 5, u32)
#define MPP_IOC_CUSTOM_BASE			0x1000

/*
 * debug flag usage:
 * +------+-------------------+
 * | 8bit |      24bit        |
 * +------+-------------------+
 *  0~23 bit is for different information type
 * 24~31 bit is for information print format
 */

#define DEBUG_POWER				0x00000001
#define DEBUG_CLOCK				0x00000002
#define DEBUG_IRQ_STATUS			0x00000004
#define DEBUG_IOMMU				0x00000008
#define DEBUG_IOCTL				0x00000010
#define DEBUG_FUNCTION				0x00000020
#define DEBUG_REGISTER				0x00000040
#define DEBUG_EXTRA_INFO			0x00000080
#define DEBUG_TIMING				0x00000100
#define DEBUG_TASK_INFO				0x00000200
#define DEBUG_DUMP_ERR_REG			0x00000400

#define DEBUG_SET_REG				0x00001000
#define DEBUG_GET_REG				0x00002000
#define DEBUG_PPS_FILL				0x00004000
#define DEBUG_IRQ_CHECK				0x00008000
#define DEBUG_CACHE_32B				0x00010000

#define DEBUG_RESET				0x00020000

#define PRINT_FUNCTION				0x80000000
#define PRINT_LINE				0x40000000

#define DEBUG
#ifdef DEBUG
#define mpp_debug_func(type, fmt, args...)			\
	do {							\
		if (unlikely(mpp_dev_debug & type)) {		\
			pr_info("%s:%d: " fmt,			\
				 __func__, __LINE__, ##args);	\
		}						\
	} while (0)
#define mpp_debug(type, fmt, args...)				\
	do {							\
		if (unlikely(mpp_dev_debug & type)) {		\
			pr_info(fmt, ##args);			\
		}						\
	} while (0)
#else
#define mpp_debug_func(level, fmt, args...)
#define mpp_debug(level, fmt, args...)
#endif

#define mpp_debug_enter() mpp_debug_func(DEBUG_FUNCTION, "enter\n")
#define mpp_debug_leave() mpp_debug_func(DEBUG_FUNCTION, "leave\n")

#define mpp_err(fmt, args...)				\
		pr_err("%s:%d: " fmt, __func__, __LINE__, ##args)

struct mpp_trans_info {
	const int count;
	const char * const table;
};

enum RKVENC_MODE {
	RKVENC_MODE_NONE,
	RKVENC_MODE_ONEFRAME,
	RKVENC_MODE_LINKTABLE_FIX,
	RKVENC_MODE_LINKTABLE_UPDATE,
	RKVENC_MODE_NUM
};

struct rockchip_mpp_dev;
struct mpp_service;
struct mpp_ctx;

struct mpp_mem_region {
	struct list_head srv_lnk;
	struct list_head reg_lnk;
	struct list_head session_lnk;
	/* virtual address for iommu */
	unsigned long iova;
	unsigned long len;
	u32 reg_idx;
	int hdl;
};

/**
 * struct for process register set
 *
 * @author ChenHengming (2011-5-4)
 */
struct mpp_ctx {
	/* context belong to */
	struct rockchip_mpp_dev *mpp;
	struct mpp_session *session;

	/* link to service session */
	struct list_head session_link;
	/* link to service list */
	struct list_head status_link;

	struct list_head mem_region_list;

	/* record context running start time */
	struct timeval start;
};

enum vpu_ctx_state {
	MMU_ACTIVATED	= BIT(0),
};

struct extra_info_elem {
	u32 index;
	u32 offset;
};

#define EXTRA_INFO_MAGIC	0x4C4A46

struct extra_info_for_iommu {
	u32 magic;
	u32 cnt;
	struct extra_info_elem elem[20];
};

struct rockchip_mpp_dev_variant {
	u32 data_len;
	u32 reg_len;
	struct mpp_trans_info *trans_info;
	char *mmu_dev_dts_name;

	int (*hw_probe)(struct rockchip_mpp_dev *mpp);
	void (*hw_remove)(struct rockchip_mpp_dev *mpp);
	void (*power_on)(struct rockchip_mpp_dev *mpp);
	void (*power_off)(struct rockchip_mpp_dev *mpp);
	int (*reset)(struct rockchip_mpp_dev *mpp);
};

struct rockchip_mpp_dev {
	struct mpp_dev_ops *ops;

	struct cdev cdev;
	dev_t dev_t;
	struct device *child_dev;

	int irq;
	struct mpp_service *srv;

	void __iomem *reg_base;
	struct list_head lnk_service;

	struct device *dev;

	unsigned long state;
	struct vpu_iommu_info *iommu_info;

	const struct rockchip_mpp_dev_variant *variant;

	struct device *mmu_dev;
	u32 iommu_enable;

	struct wake_lock wake_lock;
	struct delayed_work power_off_work;
	/* record previous power-on time */
	ktime_t last;
	atomic_t power_on_cnt;
	atomic_t power_off_cnt;
	atomic_t total_running;
	atomic_t enabled;
	atomic_t reset_request;
};

/**
 * struct mpp_dev_ops - context specific operations for mpp_device
 *
 * @init	Prepare for registers file for specific hardware.
 * @prepare	Check HW status for determining run next task or not.
 * @run		Start a single {en,de}coding run. Set registers to hardware.
 * @done	Read back processing results and additional data from hardware.
 * @result	Read status to userspace.
 * @deinit	Release the resource allocate during init.
 * @ioctl	ioctl for special HW besides the common ioctl.
 * @irq		interrupt service for specific hardware.
 * @open	a specific instance open operation for hardware.
 * @release	a specific instance release operation for hardware.
 */
struct mpp_dev_ops {
	/* size: in bytes, data sent from userspace, length in bytes */
	struct mpp_ctx *(*init)(struct rockchip_mpp_dev *mpp,
				struct mpp_session *session,
				void __user *src, u32 size);
	int (*prepare)(struct rockchip_mpp_dev *mpp);
	int (*run)(struct rockchip_mpp_dev *mpp);
	int (*done)(struct rockchip_mpp_dev *mpp);
	int (*irq)(struct rockchip_mpp_dev *mpp);
	int (*result)(struct rockchip_mpp_dev *mpp, struct mpp_ctx *ctx,
		      u32 __user *dst);
	void (*deinit)(struct rockchip_mpp_dev *mpp);
	long (*ioctl)(struct mpp_session *isession,
		      unsigned int cmd, unsigned long arg);
	struct mpp_session *(*open)(struct rockchip_mpp_dev *mpp);
	void (*release)(struct mpp_session *session);
};

void mpp_dump_reg(void __iomem *regs, int count);
void mpp_dump_reg_mem(u32 *regs, int count);
int mpp_reg_address_translate(struct rockchip_mpp_dev *data,
			      u32 *reg,
			      struct mpp_ctx *ctx,
			      int idx);
void mpp_translate_extra_info(struct mpp_ctx *ctx,
			      struct extra_info_for_iommu *ext_inf,
			      u32 *reg);

int mpp_dev_common_ctx_init(struct rockchip_mpp_dev *mpp, struct mpp_ctx *cfg);
void mpp_dev_common_ctx_deinit(struct rockchip_mpp_dev *mpp,
			       struct mpp_ctx *ctx);
void mpp_dev_power_on(struct rockchip_mpp_dev *mpp);
void mpp_dev_power_off(struct rockchip_mpp_dev *mpp);
bool mpp_dev_is_power_on(struct rockchip_mpp_dev *mpp);

static inline void mpp_write_relaxed(struct rockchip_mpp_dev *mpp,
				     u32 val, u32 reg)
{
	mpp_debug(DEBUG_SET_REG, "MARK: set reg[%03d]: %08x\n", reg / 4, val);
	writel_relaxed(val, mpp->reg_base + reg);
}

static inline void mpp_write(struct rockchip_mpp_dev *mpp,
			     u32 val, u32 reg)
{
	mpp_debug(DEBUG_SET_REG, "MARK: set reg[%03d]: %08x\n", reg / 4, val);
	writel(val, mpp->reg_base + reg);
}

static inline u32 mpp_read(struct rockchip_mpp_dev *mpp, u32 reg)
{
	u32 val = readl(mpp->reg_base + reg);

	mpp_debug(DEBUG_GET_REG, "MARK: get reg[%03d] 0x%x: %08x\n", reg / 4,
		  reg, val);
	return val;
}

static inline void mpp_time_record(struct mpp_ctx *ctx)
{
	if (unlikely(mpp_dev_debug & DEBUG_TIMING) && ctx)
		do_gettimeofday(&ctx->start);
}

static inline void mpp_time_diff(struct mpp_ctx *ctx)
{
	struct timeval end;

	do_gettimeofday(&end);
	mpp_debug(DEBUG_TIMING, "consume: %ld us\n",
		  (end.tv_sec  - ctx->start.tv_sec)  * 1000000 +
		  (end.tv_usec - ctx->start.tv_usec));
}

extern const struct rockchip_mpp_dev_variant rkvenc_variant;
extern const struct rockchip_mpp_dev_variant vepu_variant;
extern const struct rockchip_mpp_dev_variant h265e_variant;

#endif
