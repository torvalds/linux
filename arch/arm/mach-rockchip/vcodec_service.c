/**
 * Copyright (C) 2014 ROCKCHIP, Inc.
 * author: chenhengming chm@rock-chips.com
 * 	   Alpha Lin, alpha.lin@rock-chips.com
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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/compat.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/ioport.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wakelock.h>
#include <linux/cdev.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/cru.h>
#include <linux/rockchip/pmu.h>
#ifdef CONFIG_MFD_SYSCON
#include <linux/regmap.h>
#endif
#include <linux/mfd/syscon.h>

#include <asm/cacheflush.h>
#include <linux/uaccess.h>
#include <linux/rockchip/grf.h>

#if defined(CONFIG_ION_ROCKCHIP)
#include <linux/rockchip_ion.h>
#endif

#if defined(CONFIG_ROCKCHIP_IOMMU) & defined(CONFIG_ION_ROCKCHIP)
#define CONFIG_VCODEC_MMU
#endif

#ifdef CONFIG_VCODEC_MMU
#include <linux/rockchip-iovmm.h>
#include <linux/dma-buf.h>
#endif

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#endif

#if defined(CONFIG_ARCH_RK319X)
#include <mach/grf.h>
#endif

#include "vcodec_service.h"

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

#define PRINT_FUNCTION				0x80000000
#define PRINT_LINE				0x40000000

static int debug;
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug,
		 "Debug level - higher value produces more verbose messages");

#define HEVC_TEST_ENABLE	0
#define VCODEC_CLOCK_ENABLE	1

typedef enum {
	VPU_DEC_ID_9190		= 0x6731,
	VPU_ID_8270		= 0x8270,
	VPU_ID_4831		= 0x4831,
	HEVC_ID			= 0x6867,
} VPU_HW_ID;

enum VPU_HW_SPEC {
	VPU_TYPE_VPU,
	VPU_TYPE_HEVC,
	VPU_TYPE_COMBO_NOENC,
	VPU_TYPE_COMBO
};

typedef enum {
	VPU_DEC_TYPE_9190	= 0,
	VPU_ENC_TYPE_8270	= 0x100,
	VPU_ENC_TYPE_4831	,
} VPU_HW_TYPE_E;

typedef enum VPU_FREQ {
	VPU_FREQ_200M,
	VPU_FREQ_266M,
	VPU_FREQ_300M,
	VPU_FREQ_400M,
	VPU_FREQ_500M,
	VPU_FREQ_600M,
	VPU_FREQ_DEFAULT,
	VPU_FREQ_BUT,
} VPU_FREQ;

typedef struct {
	VPU_HW_ID		hw_id;
	unsigned long		hw_addr;
	unsigned long		enc_offset;
	unsigned long		enc_reg_num;
	unsigned long		enc_io_size;
	unsigned long		dec_offset;
	unsigned long		dec_reg_num;
	unsigned long		dec_io_size;
} VPU_HW_INFO_E;

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

#define MHZ					(1000*1000)

#define REG_NUM_9190_DEC			(60)
#define REG_NUM_9190_PP				(41)
#define REG_NUM_9190_DEC_PP			(REG_NUM_9190_DEC+REG_NUM_9190_PP)

#define REG_NUM_DEC_PP				(REG_NUM_9190_DEC+REG_NUM_9190_PP)

#define REG_NUM_ENC_8270			(96)
#define REG_SIZE_ENC_8270			(0x200)
#define REG_NUM_ENC_4831			(164)
#define REG_SIZE_ENC_4831			(0x400)

#define REG_NUM_HEVC_DEC			(68)

#define SIZE_REG(reg)				((reg)*4)

static VPU_HW_INFO_E vpu_hw_set[] = {
	[0] = {
		.hw_id		= VPU_ID_8270,
		.hw_addr	= 0,
		.enc_offset	= 0x0,
		.enc_reg_num	= REG_NUM_ENC_8270,
		.enc_io_size	= REG_NUM_ENC_8270 * 4,
		.dec_offset	= REG_SIZE_ENC_8270,
		.dec_reg_num	= REG_NUM_9190_DEC_PP,
		.dec_io_size	= REG_NUM_9190_DEC_PP * 4,
	},
	[1] = {
		.hw_id		= VPU_ID_4831,
		.hw_addr	= 0,
		.enc_offset	= 0x0,
		.enc_reg_num	= REG_NUM_ENC_4831,
		.enc_io_size	= REG_NUM_ENC_4831 * 4,
		.dec_offset	= REG_SIZE_ENC_4831,
		.dec_reg_num	= REG_NUM_9190_DEC_PP,
		.dec_io_size	= REG_NUM_9190_DEC_PP * 4,
	},
	[2] = {
		.hw_id		= HEVC_ID,
		.hw_addr	= 0,
		.dec_offset	= 0x0,
		.dec_reg_num	= REG_NUM_HEVC_DEC,
		.dec_io_size	= REG_NUM_HEVC_DEC * 4,
	},
	[3] = {
		.hw_id		= VPU_DEC_ID_9190,
		.hw_addr	= 0,
		.enc_offset	= 0x0,
		.enc_reg_num	= 0,
		.enc_io_size	= 0,
		.dec_offset	= 0,
		.dec_reg_num	= REG_NUM_9190_DEC_PP,
		.dec_io_size	= REG_NUM_9190_DEC_PP * 4,
	},
};

#ifndef BIT
#define BIT(x)					(1<<(x))
#endif

// interrupt and error status register
#define DEC_INTERRUPT_REGISTER			1
#define DEC_INTERRUPT_BIT			BIT(8)
#define DEC_READY_BIT				BIT(12)
#define DEC_BUS_ERROR_BIT			BIT(13)
#define DEC_BUFFER_EMPTY_BIT			BIT(14)
#define DEC_ASO_ERROR_BIT			BIT(15)
#define DEC_STREAM_ERROR_BIT			BIT(16)
#define DEC_SLICE_DONE_BIT			BIT(17)
#define DEC_TIMEOUT_BIT				BIT(18)
#define DEC_ERR_MASK				DEC_BUS_ERROR_BIT \
						|DEC_BUFFER_EMPTY_BIT \
						|DEC_STREAM_ERROR_BIT \
						|DEC_TIMEOUT_BIT

#define PP_INTERRUPT_REGISTER			60
#define PP_INTERRUPT_BIT			BIT(8)
#define PP_READY_BIT				BIT(12)
#define PP_BUS_ERROR_BIT			BIT(13)
#define PP_ERR_MASK				PP_BUS_ERROR_BIT

#define ENC_INTERRUPT_REGISTER			1
#define ENC_INTERRUPT_BIT			BIT(0)
#define ENC_READY_BIT				BIT(2)
#define ENC_BUS_ERROR_BIT			BIT(3)
#define ENC_BUFFER_FULL_BIT			BIT(5)
#define ENC_TIMEOUT_BIT				BIT(6)
#define ENC_ERR_MASK				ENC_BUS_ERROR_BIT \
						|ENC_BUFFER_FULL_BIT \
						|ENC_TIMEOUT_BIT

#define HEVC_INTERRUPT_REGISTER			1
#define HEVC_DEC_INT_RAW_BIT			BIT(9)
#define HEVC_DEC_BUS_ERROR_BIT			BIT(13)
#define HEVC_DEC_STR_ERROR_BIT			BIT(14)
#define HEVC_DEC_TIMEOUT_BIT			BIT(15)
#define HEVC_DEC_BUFFER_EMPTY_BIT		BIT(16)
#define HEVC_DEC_COLMV_ERROR_BIT		BIT(17)
#define HEVC_DEC_ERR_MASK			HEVC_DEC_BUS_ERROR_BIT \
						|HEVC_DEC_STR_ERROR_BIT \
						|HEVC_DEC_TIMEOUT_BIT \
						|HEVC_DEC_BUFFER_EMPTY_BIT \
						|HEVC_DEC_COLMV_ERROR_BIT


// gating configuration set
#define VPU_REG_EN_ENC				14
#define VPU_REG_ENC_GATE			2
#define VPU_REG_ENC_GATE_BIT			(1<<4)

#define VPU_REG_EN_DEC				1
#define VPU_REG_DEC_GATE			2
#define VPU_REG_DEC_GATE_BIT			(1<<10)
#define VPU_REG_EN_PP				0
#define VPU_REG_PP_GATE 			1
#define VPU_REG_PP_GATE_BIT 			(1<<8)
#define VPU_REG_EN_DEC_PP			1
#define VPU_REG_DEC_PP_GATE 			61
#define VPU_REG_DEC_PP_GATE_BIT 		(1<<8)

#define DEBUG
#ifdef DEBUG
#define vpu_debug_func(type, fmt, args...)			\
	do {							\
		if (unlikely(debug & type)) {			\
			pr_info("%s:%d: " fmt,	                \
				 __func__, __LINE__, ##args);	\
		}						\
	} while (0)
#define vpu_debug(type, fmt, args...)				\
	do {							\
		if (unlikely(debug & type)) {			\
			pr_info(fmt, ##args);			\
		}						\
	} while (0)
#else
#define vpu_debug_func(level, fmt, args...)
#define vpu_debug(level, fmt, args...)
#endif

#define vpu_debug_enter() vpu_debug_func(DEBUG_FUNCTION, "enter\n")
#define vpu_debug_leave() vpu_debug_func(DEBUG_FUNCTION, "leave\n")

#define vpu_err(fmt, args...)				\
		pr_err("%s:%d: " fmt, __func__, __LINE__, ##args)

#if defined(CONFIG_VCODEC_MMU)
static u8 addr_tbl_vpu_h264dec[] = {
	12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
	25, 26, 27, 28, 29, 40, 41
};

static u8 addr_tbl_vpu_vp8dec[] = {
	10, 12, 13, 14, 18, 19, 22, 23, 24, 25, 26, 27, 28, 29, 40
};

static u8 addr_tbl_vpu_vp6dec[] = {
	12, 13, 14, 18, 27, 40
};

static u8 addr_tbl_vpu_vc1dec[] = {
	12, 13, 14, 15, 16, 17, 27, 41
};

static u8 addr_tbl_vpu_jpegdec[] = {
	12, 40, 66, 67
};

static u8 addr_tbl_vpu_defaultdec[] = {
	12, 13, 14, 15, 16, 17, 40, 41
};

static u8 addr_tbl_vpu_enc[] = {
	5, 6, 7, 8, 9, 10, 11, 12, 13, 51
};

static u8 addr_tbl_hevc_dec[] = {
	4, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23, 24, 42, 43
};
#endif

enum VPU_DEC_FMT {
	VPU_DEC_FMT_H264,
	VPU_DEC_FMT_MPEG4,
	VPU_DEC_FMT_H263,
	VPU_DEC_FMT_JPEG,
	VPU_DEC_FMT_VC1,
	VPU_DEC_FMT_MPEG2,
	VPU_DEC_FMT_MPEG1,
	VPU_DEC_FMT_VP6,
	VPU_DEC_FMT_RV,
	VPU_DEC_FMT_VP7,
	VPU_DEC_FMT_VP8,
	VPU_DEC_FMT_AVS,
	VPU_DEC_FMT_SVC,
	VPU_DEC_FMT_VC2,
	VPU_DEC_FMT_MVC,
	VPU_DEC_FMT_THEORA,
	VPU_DEC_FMT_RES
};

/**
 * struct for process session which connect to vpu
 *
 * @author ChenHengming (2011-5-3)
 */
typedef struct vpu_session {
	enum VPU_CLIENT_TYPE type;
	/* a linked list of data so we can access them for debugging */
	struct list_head list_session;
	/* a linked list of register data waiting for process */
	struct list_head waiting;
	/* a linked list of register data in processing */
	struct list_head running;
	/* a linked list of register data processed */
	struct list_head done;
	wait_queue_head_t wait;
	pid_t pid;
	atomic_t task_running;
} vpu_session;

/**
 * struct for process register set
 *
 * @author ChenHengming (2011-5-4)
 */
typedef struct vpu_reg {
	enum VPU_CLIENT_TYPE type;
	VPU_FREQ freq;
	vpu_session *session;
	struct vpu_subdev_data *data;
	struct list_head session_link;		/* link to vpu service session */
	struct list_head status_link;		/* link to register set list */
	unsigned long size;
#if defined(CONFIG_VCODEC_MMU)
	struct list_head mem_region_list;
	u32 dec_base;
#endif
	u32 *reg;
} vpu_reg;

typedef struct vpu_device {
	atomic_t		irq_count_codec;
	atomic_t		irq_count_pp;
	unsigned long		iobaseaddr;
	unsigned int		iosize;
	volatile u32		*hwregs;
} vpu_device;

enum vcodec_device_id {
	VCODEC_DEVICE_ID_VPU,
	VCODEC_DEVICE_ID_HEVC,
	VCODEC_DEVICE_ID_COMBO
};

enum VCODEC_RUNNING_MODE {
	VCODEC_RUNNING_MODE_NONE = -1,
	VCODEC_RUNNING_MODE_VPU,
	VCODEC_RUNNING_MODE_HEVC,
};

struct vcodec_mem_region {
	struct list_head srv_lnk;
	struct list_head reg_lnk;
	struct list_head session_lnk;
	unsigned long iova;	/* virtual address for iommu */
	unsigned long len;
        u32 reg_idx;
	struct ion_handle *hdl;
};

enum vpu_ctx_state {
	MMU_ACTIVATED	= BIT(0)
};

struct vpu_subdev_data {
	struct cdev cdev;
	dev_t dev_t;
	struct class *cls;
	struct device *child_dev;

	int irq_enc;
	int irq_dec;
	struct vpu_service_info *pservice;

	u32 *regs;
	enum VCODEC_RUNNING_MODE mode;
	struct list_head lnk_service;

	struct device *dev;

	vpu_device enc_dev;
	vpu_device dec_dev;
	VPU_HW_INFO_E *hw_info;

	u32 reg_size;
	unsigned long state;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs_dir;
	struct dentry *debugfs_file_regs;
#endif

#if defined(CONFIG_VCODEC_MMU)
	struct device *mmu_dev;
#endif
};

typedef struct vpu_service_info {
	struct wake_lock wake_lock;
	struct delayed_work power_off_work;
	struct mutex lock;
	struct list_head waiting;		/* link to link_reg in struct vpu_reg */
	struct list_head running;		/* link to link_reg in struct vpu_reg */
	struct list_head done;			/* link to link_reg in struct vpu_reg */
	struct list_head session;		/* link to list_session in struct vpu_session */
	atomic_t total_running;
	atomic_t enabled;
	atomic_t power_on_cnt;
	atomic_t power_off_cnt;
	vpu_reg *reg_codec;
	vpu_reg *reg_pproc;
	vpu_reg *reg_resev;
	struct vpu_dec_config dec_config;
	struct vpu_enc_config enc_config;

	bool auto_freq;
	bool bug_dec_addr;
	atomic_t freq_status;

	struct clk *aclk_vcodec;
	struct clk *hclk_vcodec;
	struct clk *clk_core;
	struct clk *clk_cabac;
	struct clk *pd_video;

#ifdef CONFIG_RESET_CONTROLLER
	struct reset_control *rst_a;
	struct reset_control *rst_h;
	struct reset_control *rst_v;
#endif
	struct device *dev;

	u32 irq_status;
	atomic_t reset_request;
#if defined(CONFIG_VCODEC_MMU)
	struct ion_client *ion_client;
	struct list_head mem_region_list;
#endif

	enum vcodec_device_id dev_id;

	enum VCODEC_RUNNING_MODE curr_mode;
	u32 prev_mode;

	struct delayed_work simulate_work;

	u32 mode_bit;
	u32 mode_ctrl;
	u32 *reg_base;
	u32 ioaddr;
#ifdef CONFIG_MFD_SYSCON
	struct regmap *grf_base;
#else
	u32 *grf_base;
#endif
	char *name;

	u32 subcnt;
	struct list_head subdev_list;
} vpu_service_info;

struct vcodec_combo {
	struct vpu_service_info *vpu_srv;
	struct vpu_service_info *hevc_srv;
	struct list_head waiting;
	struct list_head running;
	struct mutex run_lock;
	vpu_reg *reg_codec;
        enum vcodec_device_id current_hw_mode;
};

struct vpu_request {
	u32 *req;
	u32 size;
};

#ifdef CONFIG_COMPAT
struct compat_vpu_request {
	compat_uptr_t req;
	u32 size;
};
#endif

/* debugfs root directory for all device (vpu, hevc).*/
static struct dentry *parent;

#ifdef CONFIG_DEBUG_FS
static int vcodec_debugfs_init(void);
static void vcodec_debugfs_exit(void);
static struct dentry* vcodec_debugfs_create_device_dir(char *dirname, struct dentry *parent);
static int debug_vcodec_open(struct inode *inode, struct file *file);

static const struct file_operations debug_vcodec_fops = {
	.open = debug_vcodec_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

#define VDPU_SOFT_RESET_REG	101
#define VDPU_CLEAN_CACHE_REG	516
#define VEPU_CLEAN_CACHE_REG	772
#define HEVC_CLEAN_CACHE_REG	260

#define VPU_REG_ENABLE(base, reg)	do { \
						base[reg] = 1; \
					} while (0)

#define VDPU_SOFT_RESET(base)	VPU_REG_ENABLE(base, VDPU_SOFT_RESET_REG)
#define VDPU_CLEAN_CACHE(base)	VPU_REG_ENABLE(base, VDPU_CLEAN_CACHE_REG)
#define VEPU_CLEAN_CACHE(base)	VPU_REG_ENABLE(base, VEPU_CLEAN_CACHE_REG)
#define HEVC_CLEAN_CACHE(base)	VPU_REG_ENABLE(base, HEVC_CLEAN_CACHE_REG)

#define VPU_POWER_OFF_DELAY		4*HZ /* 4s */
#define VPU_TIMEOUT_DELAY		2*HZ /* 2s */

typedef struct {
	char *name;
	struct timeval start;
	struct timeval end;
	u32 error_mask;
} task_info;

typedef enum {
	TASK_VPU_ENC,
	TASK_VPU_DEC,
	TASK_VPU_PP,
	TASK_RKDEC_HEVC,
	TASK_TYPE_BUTT,
} TASK_TYPE;

task_info tasks[TASK_TYPE_BUTT] = {
	{
		.name = "enc",
		.error_mask = ENC_ERR_MASK
	},
	{
		.name = "dec",
		.error_mask = DEC_ERR_MASK
	},
	{
		.name = "pp",
		.error_mask = PP_ERR_MASK
	},
	{
		.name = "hevc",
		.error_mask = HEVC_DEC_ERR_MASK
	},
};

static void time_record(task_info *task, int is_end)
{
	if (unlikely(debug & DEBUG_TIMING)) {
		do_gettimeofday((is_end)?(&task->end):(&task->start));
	}
}

static void time_diff(task_info *task)
{
	vpu_debug(DEBUG_TIMING, "%s task: %ld ms\n", task->name,
			(task->end.tv_sec  - task->start.tv_sec)  * 1000 +
			(task->end.tv_usec - task->start.tv_usec) / 1000);
}

static void vcodec_enter_mode(struct vpu_subdev_data *data)
{
	int bits;
	u32 raw = 0;
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_subdev_data *subdata, *n;
	if (pservice->subcnt < 2) {
#if defined(CONFIG_VCODEC_MMU)
		if (data->mmu_dev && !test_bit(MMU_ACTIVATED, &data->state)) {
			set_bit(MMU_ACTIVATED, &data->state);
			if (atomic_read(&pservice->enabled))
				rockchip_iovmm_activate(data->dev);
			else
				BUG_ON(!atomic_read(&pservice->enabled));
		}
#endif
		return;
	}

	if (pservice->curr_mode == data->mode)
		return;

	vpu_debug(DEBUG_IOMMU, "vcodec enter mode %d\n", data->mode);
#if defined(CONFIG_VCODEC_MMU)
	list_for_each_entry_safe(subdata, n, &pservice->subdev_list, lnk_service) {
		if (data != subdata && subdata->mmu_dev &&
		    test_bit(MMU_ACTIVATED, &subdata->state)) {
			clear_bit(MMU_ACTIVATED, &subdata->state);
			rockchip_iovmm_deactivate(subdata->dev);
		}
	}
#endif
	bits = 1 << pservice->mode_bit;
#ifdef CONFIG_MFD_SYSCON
	regmap_read(pservice->grf_base, pservice->mode_ctrl, &raw);

	if (data->mode == VCODEC_RUNNING_MODE_HEVC)
		regmap_write(pservice->grf_base, pservice->mode_ctrl,
			raw | bits | (bits << 16));
	else
		regmap_write(pservice->grf_base, pservice->mode_ctrl,
			(raw & (~bits)) | (bits << 16));
#else
	raw = readl_relaxed(pservice->grf_base + pservice->mode_ctrl / 4);
	if (data->mode == VCODEC_RUNNING_MODE_HEVC)
		writel_relaxed(raw | bits | (bits << 16),
			pservice->grf_base + pservice->mode_ctrl / 4);
	else
		writel_relaxed((raw & (~bits)) | (bits << 16),
			pservice->grf_base + pservice->mode_ctrl / 4);
#endif
#if defined(CONFIG_VCODEC_MMU)
	if (data->mmu_dev && !test_bit(MMU_ACTIVATED, &data->state)) {
		set_bit(MMU_ACTIVATED, &data->state);
		if (atomic_read(&pservice->enabled))
			rockchip_iovmm_activate(data->dev);
		else
			BUG_ON(!atomic_read(&pservice->enabled));
	}
#endif
	pservice->prev_mode = pservice->curr_mode;
	pservice->curr_mode = data->mode;
}

static void vcodec_exit_mode(struct vpu_subdev_data *data)
{
	if (data->mmu_dev && test_bit(MMU_ACTIVATED, &data->state)) {
		clear_bit(MMU_ACTIVATED, &data->state);
		rockchip_iovmm_deactivate(data->dev);
		data->pservice->curr_mode = VCODEC_RUNNING_MODE_NONE;
	}
}

static int vpu_get_clk(struct vpu_service_info *pservice)
{
#if VCODEC_CLOCK_ENABLE
	switch (pservice->dev_id) {
	case VCODEC_DEVICE_ID_HEVC:
		pservice->pd_video = devm_clk_get(pservice->dev, "pd_hevc");
		if (IS_ERR(pservice->pd_video)) {
			dev_err(pservice->dev, "failed on clk_get pd_hevc\n");
			return -1;
		}
	case VCODEC_DEVICE_ID_COMBO:
		pservice->clk_cabac = devm_clk_get(pservice->dev, "clk_cabac");
		if (IS_ERR(pservice->clk_cabac)) {
			dev_err(pservice->dev, "failed on clk_get clk_cabac\n");
			pservice->clk_cabac = NULL;
		}
		pservice->clk_core = devm_clk_get(pservice->dev, "clk_core");
		if (IS_ERR(pservice->clk_core)) {
			dev_err(pservice->dev, "failed on clk_get clk_core\n");
			return -1;
		}
	case VCODEC_DEVICE_ID_VPU:
		pservice->aclk_vcodec = devm_clk_get(pservice->dev, "aclk_vcodec");
		if (IS_ERR(pservice->aclk_vcodec)) {
			dev_err(pservice->dev, "failed on clk_get aclk_vcodec\n");
			return -1;
		}

		pservice->hclk_vcodec = devm_clk_get(pservice->dev, "hclk_vcodec");
		if (IS_ERR(pservice->hclk_vcodec)) {
			dev_err(pservice->dev, "failed on clk_get hclk_vcodec\n");
			return -1;
		}
		if (pservice->pd_video == NULL) {
			pservice->pd_video = devm_clk_get(pservice->dev, "pd_video");
			if (IS_ERR(pservice->pd_video)) {
				pservice->pd_video = NULL;
				dev_info(pservice->dev, "do not have pd_video\n");
			}
		}
		break;
	default:
		;
	}

	return 0;
#else
	return 0;
#endif
}

static void vpu_put_clk(struct vpu_service_info *pservice)
{
#if VCODEC_CLOCK_ENABLE
	if (pservice->pd_video)
		devm_clk_put(pservice->dev, pservice->pd_video);
	if (pservice->aclk_vcodec)
		devm_clk_put(pservice->dev, pservice->aclk_vcodec);
	if (pservice->hclk_vcodec)
		devm_clk_put(pservice->dev, pservice->hclk_vcodec);
	if (pservice->clk_core)
		devm_clk_put(pservice->dev, pservice->clk_core);
	if (pservice->clk_cabac)
		devm_clk_put(pservice->dev, pservice->clk_cabac);
#endif
}

static void vpu_reset(struct vpu_subdev_data *data)
{
	struct vpu_service_info *pservice = data->pservice;
	enum pmu_idle_req type = IDLE_REQ_VIDEO;

	if (pservice->dev_id == VCODEC_DEVICE_ID_HEVC)
		type = IDLE_REQ_HEVC;

	pr_info("%s: resetting...", dev_name(pservice->dev));

#if defined(CONFIG_ARCH_RK29)
	clk_disable(aclk_ddr_vepu);
	cru_set_soft_reset(SOFT_RST_CPU_VODEC_A2A_AHB, true);
	cru_set_soft_reset(SOFT_RST_DDR_VCODEC_PORT, true);
	cru_set_soft_reset(SOFT_RST_VCODEC_AHB_BUS, true);
	cru_set_soft_reset(SOFT_RST_VCODEC_AXI_BUS, true);
	mdelay(10);
	cru_set_soft_reset(SOFT_RST_VCODEC_AXI_BUS, false);
	cru_set_soft_reset(SOFT_RST_VCODEC_AHB_BUS, false);
	cru_set_soft_reset(SOFT_RST_DDR_VCODEC_PORT, false);
	cru_set_soft_reset(SOFT_RST_CPU_VODEC_A2A_AHB, false);
	clk_enable(aclk_ddr_vepu);
#elif defined(CONFIG_ARCH_RK30)
	pmu_set_idle_request(IDLE_REQ_VIDEO, true);
	cru_set_soft_reset(SOFT_RST_CPU_VCODEC, true);
	cru_set_soft_reset(SOFT_RST_VCODEC_NIU_AXI, true);
	cru_set_soft_reset(SOFT_RST_VCODEC_AHB, true);
	cru_set_soft_reset(SOFT_RST_VCODEC_AXI, true);
	mdelay(1);
	cru_set_soft_reset(SOFT_RST_VCODEC_AXI, false);
	cru_set_soft_reset(SOFT_RST_VCODEC_AHB, false);
	cru_set_soft_reset(SOFT_RST_VCODEC_NIU_AXI, false);
	cru_set_soft_reset(SOFT_RST_CPU_VCODEC, false);
	pmu_set_idle_request(IDLE_REQ_VIDEO, false);
#else
#endif
	WARN_ON(pservice->reg_codec != NULL);
	WARN_ON(pservice->reg_pproc != NULL);
	WARN_ON(pservice->reg_resev != NULL);
	pservice->reg_codec = NULL;
	pservice->reg_pproc = NULL;
	pservice->reg_resev = NULL;

	pr_info("for 3288/3368...");
#ifdef CONFIG_RESET_CONTROLLER
	if (pservice->rst_a && pservice->rst_h) {
		if (rockchip_pmu_ops.set_idle_request)
			rockchip_pmu_ops.set_idle_request(type, true);
		pr_info("reset in\n");
		if (pservice->rst_v)
			reset_control_assert(pservice->rst_v);
		reset_control_assert(pservice->rst_a);
		reset_control_assert(pservice->rst_h);
		usleep_range(10, 20);
		reset_control_deassert(pservice->rst_h);
		reset_control_deassert(pservice->rst_a);
		if (pservice->rst_v)
			reset_control_deassert(pservice->rst_v);
		if (rockchip_pmu_ops.set_idle_request)
			rockchip_pmu_ops.set_idle_request(type, false);
	}
#endif

#if defined(CONFIG_VCODEC_MMU)
	if (data->mmu_dev && test_bit(MMU_ACTIVATED, &data->state)) {
		clear_bit(MMU_ACTIVATED, &data->state);
		if (atomic_read(&pservice->enabled))
			rockchip_iovmm_deactivate(data->dev);
		else
			BUG_ON(!atomic_read(&pservice->enabled));
	}
#endif
	atomic_set(&pservice->reset_request, 0);
	pr_info("done\n");
}

static void reg_deinit(struct vpu_subdev_data *data, vpu_reg *reg);
static void vpu_service_session_clear(struct vpu_subdev_data *data, vpu_session *session)
{
	vpu_reg *reg, *n;
	list_for_each_entry_safe(reg, n, &session->waiting, session_link) {
		reg_deinit(data, reg);
	}
	list_for_each_entry_safe(reg, n, &session->running, session_link) {
		reg_deinit(data, reg);
	}
	list_for_each_entry_safe(reg, n, &session->done, session_link) {
		reg_deinit(data, reg);
	}
}

static void vpu_service_dump(struct vpu_service_info *pservice)
{
}

static void vpu_service_power_off(struct vpu_service_info *pservice)
{
	int total_running;
	struct vpu_subdev_data *data = NULL, *n;
	int ret = atomic_add_unless(&pservice->enabled, -1, 0);
	if (!ret)
		return;

	total_running = atomic_read(&pservice->total_running);
	if (total_running) {
		pr_alert("alert: power off when %d task running!!\n", total_running);
		mdelay(50);
		pr_alert("alert: delay 50 ms for running task\n");
		vpu_service_dump(pservice);
	}

	pr_info("%s: power off...", dev_name(pservice->dev));
	udelay(10);
#if defined(CONFIG_VCODEC_MMU)
	list_for_each_entry_safe(data, n, &pservice->subdev_list, lnk_service) {
		if (data->mmu_dev && test_bit(MMU_ACTIVATED, &data->state)) {
			clear_bit(MMU_ACTIVATED, &data->state);
			rockchip_iovmm_deactivate(data->dev);
		}
	}
	pservice->curr_mode = VCODEC_RUNNING_MODE_NONE;
#endif

#if VCODEC_CLOCK_ENABLE
	if (pservice->pd_video)
		clk_disable_unprepare(pservice->pd_video);
	if (pservice->hclk_vcodec)
		clk_disable_unprepare(pservice->hclk_vcodec);
	if (pservice->aclk_vcodec)
		clk_disable_unprepare(pservice->aclk_vcodec);
	if (pservice->clk_core)
		clk_disable_unprepare(pservice->clk_core);
	if (pservice->clk_cabac)
		clk_disable_unprepare(pservice->clk_cabac);
#endif

	atomic_add(1, &pservice->power_off_cnt);
	wake_unlock(&pservice->wake_lock);
	pr_info("done\n");
}

static inline void vpu_queue_power_off_work(struct vpu_service_info *pservice)
{
	queue_delayed_work(system_nrt_wq, &pservice->power_off_work, VPU_POWER_OFF_DELAY);
}

static void vpu_power_off_work(struct work_struct *work_s)
{
	struct delayed_work *dlwork = container_of(work_s, struct delayed_work, work);
	struct vpu_service_info *pservice = container_of(dlwork, struct vpu_service_info, power_off_work);

	if (mutex_trylock(&pservice->lock)) {
		vpu_service_power_off(pservice);
		mutex_unlock(&pservice->lock);
	} else {
		/* Come back later if the device is busy... */
		vpu_queue_power_off_work(pservice);
	}
}

static void vpu_service_power_on(struct vpu_service_info *pservice)
{
	int ret;
	static ktime_t last;
	ktime_t now = ktime_get();
	if (ktime_to_ns(ktime_sub(now, last)) > NSEC_PER_SEC) {
		cancel_delayed_work_sync(&pservice->power_off_work);
		vpu_queue_power_off_work(pservice);
		last = now;
	}
	ret = atomic_add_unless(&pservice->enabled, 1, 1);
	if (!ret)
		return ;

	pr_info("%s: power on\n", dev_name(pservice->dev));

#define BIT_VCODEC_CLK_SEL	(1<<10)
	if (cpu_is_rk312x())
		writel_relaxed(readl_relaxed(RK_GRF_VIRT + RK312X_GRF_SOC_CON1) |
			BIT_VCODEC_CLK_SEL | (BIT_VCODEC_CLK_SEL << 16),
			RK_GRF_VIRT + RK312X_GRF_SOC_CON1);

#if VCODEC_CLOCK_ENABLE
	if (pservice->aclk_vcodec)
		clk_prepare_enable(pservice->aclk_vcodec);
	if (pservice->hclk_vcodec)
		clk_prepare_enable(pservice->hclk_vcodec);
	if (pservice->clk_core)
		clk_prepare_enable(pservice->clk_core);
	if (pservice->clk_cabac)
		clk_prepare_enable(pservice->clk_cabac);
	if (pservice->pd_video)
		clk_prepare_enable(pservice->pd_video);
#endif

	udelay(10);
	atomic_add(1, &pservice->power_on_cnt);
	wake_lock(&pservice->wake_lock);
}

static inline bool reg_check_rmvb_wmv(vpu_reg *reg)
{
	u32 type = (reg->reg[3] & 0xF0000000) >> 28;
	return ((type == 8) || (type == 4));
}

static inline bool reg_check_interlace(vpu_reg *reg)
{
	u32 type = (reg->reg[3] & (1 << 23));
	return (type > 0);
}

static inline enum VPU_DEC_FMT reg_check_fmt(vpu_reg *reg)
{
	enum VPU_DEC_FMT type = (enum VPU_DEC_FMT)((reg->reg[3] & 0xF0000000) >> 28);
	return type;
}

static inline int reg_probe_width(vpu_reg *reg)
{
	int width_in_mb = reg->reg[4] >> 23;
	return width_in_mb * 16;
}

#if defined(CONFIG_VCODEC_MMU)
static int vcodec_fd_to_iova(struct vpu_subdev_data *data, vpu_reg *reg,int fd)
{
	struct vpu_service_info *pservice = data->pservice;
	struct ion_handle *hdl;
	int ret = 0;
	struct vcodec_mem_region *mem_region;

	hdl = ion_import_dma_buf(pservice->ion_client, fd);
	if (IS_ERR(hdl)) {
		vpu_err("import dma-buf from fd %d failed\n", fd);
		return PTR_ERR(hdl);
	}
	mem_region = kzalloc(sizeof(struct vcodec_mem_region), GFP_KERNEL);

	if (mem_region == NULL) {
		vpu_err("allocate memory for iommu memory region failed\n");
		ion_free(pservice->ion_client, hdl);
		return -1;
	}

	mem_region->hdl = hdl;
	ret = ion_map_iommu(data->dev, pservice->ion_client,
		mem_region->hdl, &mem_region->iova, &mem_region->len);

	if (ret < 0) {
		vpu_err("ion map iommu failed\n");
		kfree(mem_region);
		ion_free(pservice->ion_client, hdl);
		return ret;
	}
	INIT_LIST_HEAD(&mem_region->reg_lnk);
	list_add_tail(&mem_region->reg_lnk, &reg->mem_region_list);
	return mem_region->iova;
}

static int vcodec_bufid_to_iova(struct vpu_subdev_data *data, u8 *tbl,
				int size, vpu_reg *reg,
				struct extra_info_for_iommu *ext_inf)
{
	struct vpu_service_info *pservice = data->pservice;
	int i;
	int usr_fd = 0;
	int offset = 0;

	if (tbl == NULL || size <= 0) {
		dev_err(pservice->dev, "input arguments invalidate\n");
		return -1;
	}

	for (i = 0; i < size; i++) {
		usr_fd = reg->reg[tbl[i]] & 0x3FF;

		if (tbl[i] == 41 && data->hw_info->hw_id != HEVC_ID &&
		    (reg->type == VPU_DEC || reg->type == VPU_DEC_PP))
			/* special for vpu dec num 41 regitster */
			offset = reg->reg[tbl[i]] >> 10 << 4;
		else
			offset = reg->reg[tbl[i]] >> 10;

		if (usr_fd != 0) {
			struct ion_handle *hdl;
			int ret = 0;
			struct vcodec_mem_region *mem_region;

			hdl = ion_import_dma_buf(pservice->ion_client, usr_fd);
			if (IS_ERR(hdl)) {
				dev_err(pservice->dev, "import dma-buf from fd %d failed, reg[%d]\n", usr_fd, tbl[i]);
				return PTR_ERR(hdl);
			}

			if (tbl[i] == 42 && data->hw_info->hw_id == HEVC_ID){
				int i = 0;
				char *pps;
				pps = (char *)ion_map_kernel(pservice->ion_client,hdl);
				for (i=0; i<64; i++) {
					u32 scaling_offset;
					u32 tmp;
					int scaling_fd= 0;
					scaling_offset = (u32)pps[i*80+74];
					scaling_offset += (u32)pps[i*80+75] << 8;
					scaling_offset += (u32)pps[i*80+76] << 16;
					scaling_offset += (u32)pps[i*80+77] << 24;
					scaling_fd = scaling_offset&0x3ff;
					scaling_offset = scaling_offset >> 10;
					if(scaling_fd > 0) {
						tmp = vcodec_fd_to_iova(data, reg, scaling_fd);
						tmp += scaling_offset;
						pps[i*80+74] = tmp & 0xff;
						pps[i*80+75] = (tmp >> 8) & 0xff;
						pps[i*80+76] = (tmp >> 16) & 0xff;
						pps[i*80+77] = (tmp >> 24) & 0xff;
					}
				}
			}

			mem_region = kzalloc(sizeof(struct vcodec_mem_region), GFP_KERNEL);

			if (mem_region == NULL) {
				dev_err(pservice->dev, "allocate memory for iommu memory region failed\n");
				ion_free(pservice->ion_client, hdl);
				return -1;
			}

			mem_region->hdl = hdl;
			mem_region->reg_idx = tbl[i];
			ret = ion_map_iommu(data->dev,
                                            pservice->ion_client,
                                            mem_region->hdl,
                                            &mem_region->iova,
                                            &mem_region->len);

			if (ret < 0) {
				dev_err(pservice->dev, "ion map iommu failed\n");
				kfree(mem_region);
				ion_free(pservice->ion_client, hdl);
				return ret;
			}

			/* special for vpu dec num 12: record decoded length
			   hacking for decoded length
			   NOTE: not a perfect fix, the fd is not recorded */
			if (tbl[i] == 12 && data->hw_info->hw_id != HEVC_ID &&
					(reg->type == VPU_DEC || reg->type == VPU_DEC_PP)) {
				reg->dec_base = mem_region->iova + offset;
				vpu_debug(DEBUG_REGISTER, "dec_set %08x\n", reg->dec_base);
			}

			reg->reg[tbl[i]] = mem_region->iova + offset;
			INIT_LIST_HEAD(&mem_region->reg_lnk);
			list_add_tail(&mem_region->reg_lnk, &reg->mem_region_list);
		}
	}

	if (ext_inf != NULL && ext_inf->magic == EXTRA_INFO_MAGIC) {
		for (i=0; i<ext_inf->cnt; i++) {
			vpu_debug(DEBUG_IOMMU, "reg[%d] + offset %d\n",
				  ext_inf->elem[i].index,
				  ext_inf->elem[i].offset);
			reg->reg[ext_inf->elem[i].index] +=
				ext_inf->elem[i].offset;
		}
	}

	return 0;
}

static int vcodec_reg_address_translate(struct vpu_subdev_data *data,
					vpu_reg *reg,
					struct extra_info_for_iommu *ext_inf)
{
	VPU_HW_ID hw_id;
	u8 *tbl;
	int size = 0;

	hw_id = data->hw_info->hw_id;

	if (hw_id == HEVC_ID) {
		tbl = addr_tbl_hevc_dec;
		size = sizeof(addr_tbl_hevc_dec);
	} else {
		if (reg->type == VPU_DEC || reg->type == VPU_DEC_PP) {
			switch (reg_check_fmt(reg)) {
			case VPU_DEC_FMT_H264:
				{
					tbl = addr_tbl_vpu_h264dec;
					size = sizeof(addr_tbl_vpu_h264dec);
					break;
				}
			case VPU_DEC_FMT_VP8:
			case VPU_DEC_FMT_VP7:
				{
					tbl = addr_tbl_vpu_vp8dec;
					size = sizeof(addr_tbl_vpu_vp8dec);
					break;
				}

			case VPU_DEC_FMT_VP6:
				{
					tbl = addr_tbl_vpu_vp6dec;
					size = sizeof(addr_tbl_vpu_vp6dec);
					break;
				}
			case VPU_DEC_FMT_VC1:
				{
					tbl = addr_tbl_vpu_vc1dec;
					size = sizeof(addr_tbl_vpu_vc1dec);
					break;
				}

			case VPU_DEC_FMT_JPEG:
				{
					tbl = addr_tbl_vpu_jpegdec;
					size = sizeof(addr_tbl_vpu_jpegdec);
					break;
				}
			default:
				tbl = addr_tbl_vpu_defaultdec;
				size = sizeof(addr_tbl_vpu_defaultdec);
				break;
			}
		} else if (reg->type == VPU_ENC) {
			tbl = addr_tbl_vpu_enc;
			size = sizeof(addr_tbl_vpu_enc);
		}
	}

	if (size != 0) {
		return vcodec_bufid_to_iova(data, tbl, size, reg, ext_inf);
	} else {
		return -1;
	}
}
#endif

static vpu_reg *reg_init(struct vpu_subdev_data *data,
	vpu_session *session, void __user *src, u32 size)
{
	struct vpu_service_info *pservice = data->pservice;
	int extra_size = 0;
	struct extra_info_for_iommu extra_info;
	vpu_reg *reg = kmalloc(sizeof(vpu_reg) + data->reg_size, GFP_KERNEL);

	vpu_debug_enter();

	if (NULL == reg) {
		vpu_err("error: kmalloc fail in reg_init\n");
		return NULL;
	}

	if (size > data->reg_size) {
		/*printk("warning: vpu reg size %u is larger than hw reg size %u\n",
		  size, data->reg_size);*/
		extra_size = size - data->reg_size;
		size = data->reg_size;
	}
	reg->session = session;
	reg->data = data;
	reg->type = session->type;
	reg->size = size;
	reg->freq = VPU_FREQ_DEFAULT;
	reg->reg = (u32 *)&reg[1];
	INIT_LIST_HEAD(&reg->session_link);
	INIT_LIST_HEAD(&reg->status_link);

#if defined(CONFIG_VCODEC_MMU)
	if (data->mmu_dev)
		INIT_LIST_HEAD(&reg->mem_region_list);
#endif

	if (copy_from_user(&reg->reg[0], (void __user *)src, size)) {
		vpu_err("error: copy_from_user failed in reg_init\n");
		kfree(reg);
		return NULL;
	}

	if (copy_from_user(&extra_info, (u8 *)src + size, extra_size)) {
		vpu_err("error: copy_from_user failed in reg_init\n");
		kfree(reg);
		return NULL;
	}

#if defined(CONFIG_VCODEC_MMU)
	if (data->mmu_dev &&
	    0 > vcodec_reg_address_translate(data, reg, &extra_info)) {
		vpu_err("error: translate reg address failed\n");
		kfree(reg);
		return NULL;
	}
#endif

	mutex_lock(&pservice->lock);
	list_add_tail(&reg->status_link, &pservice->waiting);
	list_add_tail(&reg->session_link, &session->waiting);
	mutex_unlock(&pservice->lock);

	if (pservice->auto_freq) {
		if (!soc_is_rk2928g()) {
			if (reg->type == VPU_DEC || reg->type == VPU_DEC_PP) {
				if (reg_check_rmvb_wmv(reg)) {
					reg->freq = VPU_FREQ_200M;
				} else if (reg_check_fmt(reg) == VPU_DEC_FMT_H264) {
					if (reg_probe_width(reg) > 3200) {
						/*raise frequency for 4k avc.*/
						reg->freq = VPU_FREQ_600M;
					}
				} else {
					if (reg_check_interlace(reg)) {
						reg->freq = VPU_FREQ_400M;
					}
				}
			}
			if (reg->type == VPU_PP) {
				reg->freq = VPU_FREQ_400M;
			}
		}
	}
	vpu_debug_leave();
	return reg;
}

static void reg_deinit(struct vpu_subdev_data *data, vpu_reg *reg)
{
	struct vpu_service_info *pservice = data->pservice;
#if defined(CONFIG_VCODEC_MMU)
	struct vcodec_mem_region *mem_region = NULL, *n;
#endif

	list_del_init(&reg->session_link);
	list_del_init(&reg->status_link);
	if (reg == pservice->reg_codec)
		pservice->reg_codec = NULL;
	if (reg == pservice->reg_pproc)
		pservice->reg_pproc = NULL;

#if defined(CONFIG_VCODEC_MMU)
	/* release memory region attach to this registers table. */
	if (data->mmu_dev) {
		list_for_each_entry_safe(mem_region, n,
			&reg->mem_region_list, reg_lnk) {
			/* do not unmap iommu manually,
			   unmap will proccess when memory release */
			/*vcodec_enter_mode(data);
			ion_unmap_iommu(data->dev,
					pservice->ion_client,
					mem_region->hdl);
			vcodec_exit_mode();*/
			ion_free(pservice->ion_client, mem_region->hdl);
			list_del_init(&mem_region->reg_lnk);
			kfree(mem_region);
		}
	}
#endif

	kfree(reg);
}

static void reg_from_wait_to_run(struct vpu_service_info *pservice, vpu_reg *reg)
{
	vpu_debug_enter();
	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &pservice->running);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->running);
	vpu_debug_leave();
}

static void reg_copy_from_hw(vpu_reg *reg, volatile u32 *src, u32 count)
{
	int i;
	u32 *dst = (u32 *)&reg->reg[0];
	vpu_debug_enter();
	for (i = 0; i < count; i++)
		*dst++ = *src++;
	vpu_debug_leave();
}

static void reg_from_run_to_done(struct vpu_subdev_data *data,
	vpu_reg *reg)
{
	struct vpu_service_info *pservice = data->pservice;
	int irq_reg = -1;

	vpu_debug_enter();

	list_del_init(&reg->status_link);
	list_add_tail(&reg->status_link, &pservice->done);

	list_del_init(&reg->session_link);
	list_add_tail(&reg->session_link, &reg->session->done);

	/*vcodec_enter_mode(data);*/
	switch (reg->type) {
	case VPU_ENC : {
		pservice->reg_codec = NULL;
		reg_copy_from_hw(reg, data->enc_dev.hwregs, data->hw_info->enc_reg_num);
		irq_reg = ENC_INTERRUPT_REGISTER;
		break;
	}
	case VPU_DEC : {
		int reg_len = REG_NUM_9190_DEC;
		pservice->reg_codec = NULL;
		reg_copy_from_hw(reg, data->dec_dev.hwregs, reg_len);
#if defined(CONFIG_VCODEC_MMU)
		/* revert hack for decoded length */
		if (data->hw_info->hw_id != HEVC_ID) {
			u32 dec_get = reg->reg[12];
			s32 dec_length = dec_get - reg->dec_base;
			vpu_debug(DEBUG_REGISTER, "dec_get %08x dec_length %d\n", dec_get, dec_length);
			reg->reg[12] = dec_length << 10;
		}
#endif
		irq_reg = DEC_INTERRUPT_REGISTER;
		break;
	}
	case VPU_PP : {
		pservice->reg_pproc = NULL;
		reg_copy_from_hw(reg, data->dec_dev.hwregs + PP_INTERRUPT_REGISTER, REG_NUM_9190_PP);
		data->dec_dev.hwregs[PP_INTERRUPT_REGISTER] = 0;
		break;
	}
	case VPU_DEC_PP : {
		pservice->reg_codec = NULL;
		pservice->reg_pproc = NULL;
		reg_copy_from_hw(reg, data->dec_dev.hwregs, REG_NUM_9190_DEC_PP);
		data->dec_dev.hwregs[PP_INTERRUPT_REGISTER] = 0;
#if defined(CONFIG_VCODEC_MMU)
		/* revert hack for decoded length */
		if (data->hw_info->hw_id != HEVC_ID) {
			u32 dec_get = reg->reg[12];
			s32 dec_length = dec_get - reg->dec_base;
			vpu_debug(DEBUG_REGISTER, "dec_get %08x dec_length %d\n", dec_get, dec_length);
			reg->reg[12] = dec_length << 10;
		}
#endif
		break;
	}
	default : {
		vpu_err("error: copy reg from hw with unknown type %d\n", reg->type);
		break;
	}
	}
	vcodec_exit_mode(data);

	if (irq_reg != -1)
		reg->reg[irq_reg] = pservice->irq_status;

	atomic_sub(1, &reg->session->task_running);
	atomic_sub(1, &pservice->total_running);
	wake_up(&reg->session->wait);

	vpu_debug_leave();
}

static void vpu_service_set_freq(struct vpu_service_info *pservice, vpu_reg *reg)
{
	VPU_FREQ curr = atomic_read(&pservice->freq_status);
	if (curr == reg->freq)
		return;
	atomic_set(&pservice->freq_status, reg->freq);
	switch (reg->freq) {
	case VPU_FREQ_200M : {
		clk_set_rate(pservice->aclk_vcodec, 200*MHZ);
	} break;
	case VPU_FREQ_266M : {
		clk_set_rate(pservice->aclk_vcodec, 266*MHZ);
	} break;
	case VPU_FREQ_300M : {
		clk_set_rate(pservice->aclk_vcodec, 300*MHZ);
	} break;
	case VPU_FREQ_400M : {
		clk_set_rate(pservice->aclk_vcodec, 400*MHZ);
	} break;
	case VPU_FREQ_500M : {
		clk_set_rate(pservice->aclk_vcodec, 500*MHZ);
	} break;
	case VPU_FREQ_600M : {
		clk_set_rate(pservice->aclk_vcodec, 600*MHZ);
	} break;
	default : {
		if (soc_is_rk2928g())
			clk_set_rate(pservice->aclk_vcodec, 400*MHZ);
		else
			clk_set_rate(pservice->aclk_vcodec, 300*MHZ);
	} break;
	}
}

static void reg_copy_to_hw(struct vpu_subdev_data *data, vpu_reg *reg)
{
	struct vpu_service_info *pservice = data->pservice;
	int i;
	u32 *src = (u32 *)&reg->reg[0];
	vpu_debug_enter();

	atomic_add(1, &pservice->total_running);
	atomic_add(1, &reg->session->task_running);
	if (pservice->auto_freq)
		vpu_service_set_freq(pservice, reg);

	vcodec_enter_mode(data);

	switch (reg->type) {
	case VPU_ENC : {
		int enc_count = data->hw_info->enc_reg_num;
		u32 *dst = (u32 *)data->enc_dev.hwregs;

		pservice->reg_codec = reg;

		dst[VPU_REG_EN_ENC] = src[VPU_REG_EN_ENC] & 0x6;

		for (i = 0; i < VPU_REG_EN_ENC; i++)
			dst[i] = src[i];

		for (i = VPU_REG_EN_ENC + 1; i < enc_count; i++)
			dst[i] = src[i];

		VEPU_CLEAN_CACHE(dst);

		dsb(sy);

		dst[VPU_REG_ENC_GATE] = src[VPU_REG_ENC_GATE] | VPU_REG_ENC_GATE_BIT;
		dst[VPU_REG_EN_ENC]   = src[VPU_REG_EN_ENC];

		time_record(&tasks[TASK_VPU_ENC], 0);
	} break;
	case VPU_DEC : {
		u32 *dst = (u32 *)data->dec_dev.hwregs;

		pservice->reg_codec = reg;

		if (data->hw_info->hw_id != HEVC_ID) {
			for (i = REG_NUM_9190_DEC - 1; i > VPU_REG_DEC_GATE; i--)
				dst[i] = src[i];
			VDPU_CLEAN_CACHE(dst);
		} else {
			for (i = REG_NUM_HEVC_DEC - 1; i > VPU_REG_EN_DEC; i--)
				dst[i] = src[i];
			HEVC_CLEAN_CACHE(dst);
		}

		dsb(sy);

		if (data->hw_info->hw_id != HEVC_ID) {
			dst[VPU_REG_DEC_GATE] = src[VPU_REG_DEC_GATE] | VPU_REG_DEC_GATE_BIT;
			dst[VPU_REG_EN_DEC] = src[VPU_REG_EN_DEC];
		} else {
			dst[VPU_REG_EN_DEC] = src[VPU_REG_EN_DEC];
		}
		dsb(sy);
		dmb(sy);

		time_record(&tasks[TASK_VPU_DEC], 0);
	} break;
	case VPU_PP : {
		u32 *dst = (u32 *)data->dec_dev.hwregs + PP_INTERRUPT_REGISTER;
		pservice->reg_pproc = reg;

		dst[VPU_REG_PP_GATE] = src[VPU_REG_PP_GATE] | VPU_REG_PP_GATE_BIT;

		for (i = VPU_REG_PP_GATE + 1; i < REG_NUM_9190_PP; i++)
			dst[i] = src[i];

		dsb(sy);

		dst[VPU_REG_EN_PP] = src[VPU_REG_EN_PP];

		time_record(&tasks[TASK_VPU_PP], 0);
	} break;
	case VPU_DEC_PP : {
		u32 *dst = (u32 *)data->dec_dev.hwregs;
		pservice->reg_codec = reg;
		pservice->reg_pproc = reg;

		VDPU_SOFT_RESET(dst);
		VDPU_CLEAN_CACHE(dst);

		for (i = VPU_REG_EN_DEC_PP + 1; i < REG_NUM_9190_DEC_PP; i++)
			dst[i] = src[i];

		dst[VPU_REG_EN_DEC_PP]   = src[VPU_REG_EN_DEC_PP] | 0x2;
		dsb(sy);

		dst[VPU_REG_DEC_PP_GATE] = src[VPU_REG_DEC_PP_GATE] | VPU_REG_PP_GATE_BIT;
		dst[VPU_REG_DEC_GATE]	 = src[VPU_REG_DEC_GATE]    | VPU_REG_DEC_GATE_BIT;
		dst[VPU_REG_EN_DEC]	 = src[VPU_REG_EN_DEC];

		time_record(&tasks[TASK_VPU_DEC], 0);
	} break;
	default : {
		vpu_err("error: unsupport session type %d", reg->type);
		atomic_sub(1, &pservice->total_running);
		atomic_sub(1, &reg->session->task_running);
	} break;
	}

	/*vcodec_exit_mode(data);*/
	vpu_debug_leave();
}

static void try_set_reg(struct vpu_subdev_data *data)
{
	struct vpu_service_info *pservice = data->pservice;
	vpu_debug_enter();
	if (!list_empty(&pservice->waiting)) {
		int can_set = 0;
		bool change_able = (NULL == pservice->reg_codec) && (NULL == pservice->reg_pproc);
		int reset_request = atomic_read(&pservice->reset_request);
		vpu_reg *reg = list_entry(pservice->waiting.next, vpu_reg, status_link);

		vpu_service_power_on(pservice);

		// first check can_set flag
		if (change_able || !reset_request) {
			switch (reg->type) {
			case VPU_ENC : {
				if (change_able)
					can_set = 1;
			} break;
			case VPU_DEC : {
				if (NULL == pservice->reg_codec)
					can_set = 1;
				if (pservice->auto_freq && (NULL != pservice->reg_pproc))
					can_set = 0;
			} break;
			case VPU_PP : {
				if (NULL == pservice->reg_codec) {
					if (NULL == pservice->reg_pproc)
						can_set = 1;
				} else {
					if ((VPU_DEC == pservice->reg_codec->type) && (NULL == pservice->reg_pproc))
						can_set = 1;
					/* can not charge frequency when vpu is working */
					if (pservice->auto_freq)
						can_set = 0;
				}
			} break;
			case VPU_DEC_PP : {
				if (change_able)
					can_set = 1;
				} break;
			default : {
				printk("undefined reg type %d\n", reg->type);
			} break;
			}
		}

		// then check reset request
		if (reset_request && !change_able)
			reset_request = 0;

		// do reset before setting registers
		if (reset_request)
			vpu_reset(data);

		if (can_set) {
			reg_from_wait_to_run(pservice, reg);
			reg_copy_to_hw(reg->data, reg);
		}
	}
	vpu_debug_leave();
}

static int return_reg(struct vpu_subdev_data *data,
	vpu_reg *reg, u32 __user *dst)
{
	int ret = 0;
	vpu_debug_enter();
	switch (reg->type) {
	case VPU_ENC : {
		if (copy_to_user(dst, &reg->reg[0], data->hw_info->enc_io_size))
			ret = -EFAULT;
		break;
	}
	case VPU_DEC : {
		int reg_len = data->hw_info->hw_id == HEVC_ID ? REG_NUM_HEVC_DEC : REG_NUM_9190_DEC;
		if (copy_to_user(dst, &reg->reg[0], SIZE_REG(reg_len)))
			ret = -EFAULT;
		break;
	}
	case VPU_PP : {
		if (copy_to_user(dst, &reg->reg[0], SIZE_REG(REG_NUM_9190_PP)))
			ret = -EFAULT;
		break;
	}
	case VPU_DEC_PP : {
		if (copy_to_user(dst, &reg->reg[0], SIZE_REG(REG_NUM_9190_DEC_PP)))
			ret = -EFAULT;
		break;
	}
	default : {
		ret = -EFAULT;
		vpu_err("error: copy reg to user with unknown type %d\n", reg->type);
		break;
	}
	}
	reg_deinit(data, reg);
	vpu_debug_leave();
	return ret;
}

static long vpu_service_ioctl(struct file *filp, unsigned int cmd,
	unsigned long arg)
{
	struct vpu_subdev_data *data =
		container_of(filp->f_dentry->d_inode->i_cdev,
			struct vpu_subdev_data, cdev);
	struct vpu_service_info *pservice = data->pservice;
	vpu_session *session = (vpu_session *)filp->private_data;
	vpu_debug_enter();
	if (NULL == session)
		return -EINVAL;

	switch (cmd) {
	case VPU_IOC_SET_CLIENT_TYPE : {
		session->type = (enum VPU_CLIENT_TYPE)arg;
		vpu_debug(DEBUG_IOCTL, "VPU_IOC_SET_CLIENT_TYPE %d\n", session->type);
		break;
	}
	case VPU_IOC_GET_HW_FUSE_STATUS : {
		struct vpu_request req;
		vpu_debug(DEBUG_IOCTL, "VPU_IOC_GET_HW_FUSE_STATUS type %d\n", session->type);
		if (copy_from_user(&req, (void __user *)arg, sizeof(struct vpu_request))) {
			vpu_err("error: VPU_IOC_GET_HW_FUSE_STATUS copy_from_user failed\n");
			return -EFAULT;
		} else {
			if (VPU_ENC != session->type) {
				if (copy_to_user((void __user *)req.req,
					&pservice->dec_config,
					sizeof(struct vpu_dec_config))) {
					vpu_err("error: VPU_IOC_GET_HW_FUSE_STATUS copy_to_user failed type %d\n",
						session->type);
					return -EFAULT;
				}
			} else {
				if (copy_to_user((void __user *)req.req,
					&pservice->enc_config,
					sizeof(struct vpu_enc_config ))) {
					vpu_err("error: VPU_IOC_GET_HW_FUSE_STATUS copy_to_user failed type %d\n",
						session->type);
					return -EFAULT;
				}
			}
		}

		break;
	}
	case VPU_IOC_SET_REG : {
		struct vpu_request req;
		vpu_reg *reg;
		vpu_debug(DEBUG_IOCTL, "VPU_IOC_SET_REG type %d\n", session->type);
		if (copy_from_user(&req, (void __user *)arg,
			sizeof(struct vpu_request))) {
			vpu_err("error: VPU_IOC_SET_REG copy_from_user failed\n");
			return -EFAULT;
		}
		reg = reg_init(data, session,
			(void __user *)req.req, req.size);
		if (NULL == reg) {
			return -EFAULT;
		} else {
			mutex_lock(&pservice->lock);
			try_set_reg(data);
			mutex_unlock(&pservice->lock);
		}

		break;
	}
	case VPU_IOC_GET_REG : {
		struct vpu_request req;
		vpu_reg *reg;
		vpu_debug(DEBUG_IOCTL, "VPU_IOC_GET_REG type %d\n", session->type);
		if (copy_from_user(&req, (void __user *)arg,
			sizeof(struct vpu_request))) {
			vpu_err("error: VPU_IOC_GET_REG copy_from_user failed\n");
			return -EFAULT;
		} else {
			int ret = wait_event_timeout(session->wait, !list_empty(&session->done), VPU_TIMEOUT_DELAY);
			if (!list_empty(&session->done)) {
				if (ret < 0) {
					vpu_err("warning: pid %d wait task sucess but wait_evernt ret %d\n", session->pid, ret);
				}
				ret = 0;
			} else {
				if (unlikely(ret < 0)) {
					vpu_err("error: pid %d wait task ret %d\n", session->pid, ret);
				} else if (0 == ret) {
					vpu_err("error: pid %d wait %d task done timeout\n", session->pid, atomic_read(&session->task_running));
					ret = -ETIMEDOUT;
				}
			}
			if (ret < 0) {
				int task_running = atomic_read(&session->task_running);
				mutex_lock(&pservice->lock);
				vpu_service_dump(pservice);
				if (task_running) {
					atomic_set(&session->task_running, 0);
					atomic_sub(task_running, &pservice->total_running);
					printk("%d task is running but not return, reset hardware...", task_running);
					vpu_reset(data);
					printk("done\n");
				}
				vpu_service_session_clear(data, session);
				mutex_unlock(&pservice->lock);
				return ret;
			}
		}
		mutex_lock(&pservice->lock);
		reg = list_entry(session->done.next, vpu_reg, session_link);
		return_reg(data, reg, (u32 __user *)req.req);
		mutex_unlock(&pservice->lock);
		break;
	}
	case VPU_IOC_PROBE_IOMMU_STATUS: {
		int iommu_enable = 0;

		vpu_debug(DEBUG_IOCTL, "VPU_IOC_PROBE_IOMMU_STATUS\n");

#if defined(CONFIG_VCODEC_MMU)
		iommu_enable = data->mmu_dev ? 1 : 0;
#endif

		if (copy_to_user((void __user *)arg, &iommu_enable, sizeof(int))) {
			vpu_err("error: VPU_IOC_PROBE_IOMMU_STATUS copy_to_user failed\n");
			return -EFAULT;
		}
		break;
	}
	default : {
		vpu_err("error: unknow vpu service ioctl cmd %x\n", cmd);
		break;
	}
	}
	vpu_debug_leave();
	return 0;
}

#ifdef CONFIG_COMPAT
static long compat_vpu_service_ioctl(struct file *filp, unsigned int cmd,
	unsigned long arg)
{
	struct vpu_subdev_data *data =
		container_of(filp->f_dentry->d_inode->i_cdev,
			struct vpu_subdev_data, cdev);
	struct vpu_service_info *pservice = data->pservice;
	vpu_session *session = (vpu_session *)filp->private_data;
	vpu_debug_enter();
	vpu_debug(3, "cmd %x, COMPAT_VPU_IOC_SET_CLIENT_TYPE %x\n", cmd,
		  (u32)COMPAT_VPU_IOC_SET_CLIENT_TYPE);
	if (NULL == session)
		return -EINVAL;

	switch (cmd) {
	case COMPAT_VPU_IOC_SET_CLIENT_TYPE : {
		session->type = (enum VPU_CLIENT_TYPE)arg;
		vpu_debug(DEBUG_IOCTL, "COMPAT_VPU_IOC_SET_CLIENT_TYPE type %d\n", session->type);
		break;
	}
	case COMPAT_VPU_IOC_GET_HW_FUSE_STATUS : {
		struct compat_vpu_request req;
		vpu_debug(DEBUG_IOCTL, "COMPAT_VPU_IOC_GET_HW_FUSE_STATUS type %d\n", session->type);
		if (copy_from_user(&req, compat_ptr((compat_uptr_t)arg),
				   sizeof(struct compat_vpu_request))) {
			vpu_err("error: VPU_IOC_GET_HW_FUSE_STATUS"
				" copy_from_user failed\n");
			return -EFAULT;
		} else {
			if (VPU_ENC != session->type) {
				if (copy_to_user(compat_ptr((compat_uptr_t)req.req),
						 &pservice->dec_config,
						 sizeof(struct vpu_dec_config))) {
					vpu_err("error: VPU_IOC_GET_HW_FUSE_STATUS "
						"copy_to_user failed type %d\n",
						session->type);
					return -EFAULT;
				}
			} else {
				if (copy_to_user(compat_ptr((compat_uptr_t)req.req),
						 &pservice->enc_config,
						 sizeof(struct vpu_enc_config ))) {
					vpu_err("error: VPU_IOC_GET_HW_FUSE_STATUS"
						" copy_to_user failed type %d\n",
						session->type);
					return -EFAULT;
				}
			}
		}

		break;
	}
	case COMPAT_VPU_IOC_SET_REG : {
		struct compat_vpu_request req;
		vpu_reg *reg;
		vpu_debug(DEBUG_IOCTL, "COMPAT_VPU_IOC_SET_REG type %d\n", session->type);
		if (copy_from_user(&req, compat_ptr((compat_uptr_t)arg),
				   sizeof(struct compat_vpu_request))) {
			vpu_err("VPU_IOC_SET_REG copy_from_user failed\n");
			return -EFAULT;
		}
		reg = reg_init(data, session,
			       compat_ptr((compat_uptr_t)req.req), req.size);
		if (NULL == reg) {
			return -EFAULT;
		} else {
			mutex_lock(&pservice->lock);
			try_set_reg(data);
			mutex_unlock(&pservice->lock);
		}

		break;
	}
	case COMPAT_VPU_IOC_GET_REG : {
		struct compat_vpu_request req;
		vpu_reg *reg;
		vpu_debug(DEBUG_IOCTL, "COMPAT_VPU_IOC_GET_REG type %d\n", session->type);
		if (copy_from_user(&req, compat_ptr((compat_uptr_t)arg),
				   sizeof(struct compat_vpu_request))) {
			vpu_err("VPU_IOC_GET_REG copy_from_user failed\n");
			return -EFAULT;
		} else {
			int ret = wait_event_timeout(session->wait, !list_empty(&session->done), VPU_TIMEOUT_DELAY);
			if (!list_empty(&session->done)) {
				if (ret < 0) {
					vpu_err("warning: pid %d wait task sucess but wait_evernt ret %d\n", session->pid, ret);
				}
				ret = 0;
			} else {
				if (unlikely(ret < 0)) {
					vpu_err("error: pid %d wait task ret %d\n", session->pid, ret);
				} else if (0 == ret) {
					vpu_err("error: pid %d wait %d task done timeout\n", session->pid, atomic_read(&session->task_running));
					ret = -ETIMEDOUT;
				}
			}
			if (ret < 0) {
				int task_running = atomic_read(&session->task_running);
				mutex_lock(&pservice->lock);
				vpu_service_dump(pservice);
				if (task_running) {
					atomic_set(&session->task_running, 0);
					atomic_sub(task_running, &pservice->total_running);
					printk("%d task is running but not return, reset hardware...", task_running);
					vpu_reset(data);
					printk("done\n");
				}
				vpu_service_session_clear(data, session);
				mutex_unlock(&pservice->lock);
				return ret;
			}
		}
		mutex_lock(&pservice->lock);
		reg = list_entry(session->done.next, vpu_reg, session_link);
		return_reg(data, reg, compat_ptr((compat_uptr_t)req.req));
		mutex_unlock(&pservice->lock);
		break;
	}
	case COMPAT_VPU_IOC_PROBE_IOMMU_STATUS : {
		int iommu_enable = 0;

		vpu_debug(DEBUG_IOCTL, "COMPAT_VPU_IOC_PROBE_IOMMU_STATUS\n");
#if defined(CONFIG_VCODEC_MMU)
		iommu_enable = data->mmu_dev ? 1 : 0;
#endif

		if (copy_to_user(compat_ptr((compat_uptr_t)arg), &iommu_enable, sizeof(int))) {
			vpu_err("error: VPU_IOC_PROBE_IOMMU_STATUS copy_to_user failed\n");
			return -EFAULT;
		}
		break;
	}
	default : {
		vpu_err("error: unknow vpu service ioctl cmd %x\n", cmd);
		break;
	}
	}
	vpu_debug_leave();
	return 0;
}
#endif

static int vpu_service_check_hw(struct vpu_subdev_data *data, u32 hw_addr)
{
	int ret = -EINVAL, i = 0;
	volatile u32 *tmp = (volatile u32 *)ioremap_nocache(hw_addr, 0x4);
	u32 enc_id = *tmp;

	enc_id = (enc_id >> 16) & 0xFFFF;
	pr_info("checking hw id %x\n", enc_id);
	data->hw_info = NULL;
	for (i = 0; i < ARRAY_SIZE(vpu_hw_set); i++) {
		if (enc_id == vpu_hw_set[i].hw_id) {
			data->hw_info = &vpu_hw_set[i];
			ret = 0;
			break;
		}
	}
	iounmap((void *)tmp);
	return ret;
}

static int vpu_service_open(struct inode *inode, struct file *filp)
{
	struct vpu_subdev_data *data = container_of(inode->i_cdev, struct vpu_subdev_data, cdev);
	struct vpu_service_info *pservice = data->pservice;
	vpu_session *session = (vpu_session *)kmalloc(sizeof(vpu_session), GFP_KERNEL);

	vpu_debug_enter();

	if (NULL == session) {
		vpu_err("error: unable to allocate memory for vpu_session.");
		return -ENOMEM;
	}

	session->type	= VPU_TYPE_BUTT;
	session->pid	= current->pid;
	INIT_LIST_HEAD(&session->waiting);
	INIT_LIST_HEAD(&session->running);
	INIT_LIST_HEAD(&session->done);
	INIT_LIST_HEAD(&session->list_session);
	init_waitqueue_head(&session->wait);
	atomic_set(&session->task_running, 0);
	mutex_lock(&pservice->lock);
	list_add_tail(&session->list_session, &pservice->session);
	filp->private_data = (void *)session;
	mutex_unlock(&pservice->lock);

	pr_debug("dev opened\n");
	vpu_debug_leave();
	return nonseekable_open(inode, filp);
}

static int vpu_service_release(struct inode *inode, struct file *filp)
{
	struct vpu_subdev_data *data = container_of(inode->i_cdev, struct vpu_subdev_data, cdev);
	struct vpu_service_info *pservice = data->pservice;
	int task_running;
	vpu_session *session = (vpu_session *)filp->private_data;
	vpu_debug_enter();
	if (NULL == session)
		return -EINVAL;

	task_running = atomic_read(&session->task_running);
	if (task_running) {
		vpu_err("error: vpu_service session %d still has %d task running when closing\n", session->pid, task_running);
		msleep(50);
	}
	wake_up(&session->wait);

	mutex_lock(&pservice->lock);
	/* remove this filp from the asynchronusly notified filp's */
	list_del_init(&session->list_session);
	vpu_service_session_clear(data, session);
	kfree(session);
	filp->private_data = NULL;
	mutex_unlock(&pservice->lock);

	pr_debug("dev closed\n");
	vpu_debug_leave();
	return 0;
}

static const struct file_operations vpu_service_fops = {
	.unlocked_ioctl = vpu_service_ioctl,
	.open		= vpu_service_open,
	.release	= vpu_service_release,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = compat_vpu_service_ioctl,
#endif
};

static irqreturn_t vdpu_irq(int irq, void *dev_id);
static irqreturn_t vdpu_isr(int irq, void *dev_id);
static irqreturn_t vepu_irq(int irq, void *dev_id);
static irqreturn_t vepu_isr(int irq, void *dev_id);
static void get_hw_info(struct vpu_subdev_data *data);

#ifdef CONFIG_VCODEC_MMU
static struct device *rockchip_get_sysmmu_dev(const char *compt)
{
	struct device_node *dn = NULL;
	struct platform_device *pd = NULL;
	struct device *ret = NULL ;

	dn = of_find_compatible_node(NULL,NULL,compt);
	if(!dn) {
		printk("can't find device node %s \r\n",compt);
		return NULL;
	}

	pd = of_find_device_by_node(dn);
	if(!pd) {
		printk("can't find platform device in device node %s\n",compt);
		return  NULL;
	}
	ret = &pd->dev;

	return ret;

}
#ifdef CONFIG_IOMMU_API
static inline void platform_set_sysmmu(struct device *iommu,
	struct device *dev)
{
	dev->archdata.iommu = iommu;
}
#else
static inline void platform_set_sysmmu(struct device *iommu,
	struct device *dev)
{
}
#endif

int vcodec_sysmmu_fault_hdl(struct device *dev,
				enum rk_iommu_inttype itype,
				unsigned long pgtable_base,
				unsigned long fault_addr, unsigned int status)
{
	struct platform_device *pdev;
	struct vpu_subdev_data *data;
	struct vpu_service_info *pservice;

	vpu_debug_enter();

	pdev = container_of(dev, struct platform_device, dev);

	data = platform_get_drvdata(pdev);
	pservice = data->pservice;

	if (pservice->reg_codec) {
		struct vcodec_mem_region *mem, *n;
		int i = 0;
		vpu_debug(DEBUG_IOMMU, "vcodec, fault addr 0x%08x\n", (u32)fault_addr);
		list_for_each_entry_safe(mem, n,
					 &pservice->reg_codec->mem_region_list,
					 reg_lnk) {
			vpu_debug(DEBUG_IOMMU, "vcodec, reg[%02u] mem region [%02d] 0x%08x %ld\n",
				mem->reg_idx, i, (u32)mem->iova, mem->len);
			i++;
		}

		pr_alert("vcodec, page fault occur, reset hw\n");
		pservice->reg_codec->reg[101] = 1;
		vpu_reset(data);
	}

	return 0;
}
#endif

#if HEVC_TEST_ENABLE
static int hevc_test_case0(vpu_service_info *pservice);
#endif
#if defined(CONFIG_ION_ROCKCHIP)
extern struct ion_client *rockchip_ion_client_create(const char * name);
#endif

static int vcodec_subdev_probe(struct platform_device *pdev,
	struct vpu_service_info *pservice)
{
	int ret = 0;
	struct resource *res = NULL;
	u32 ioaddr = 0;
	struct device *dev = &pdev->dev;
	char *name = (char*)dev_name(dev);
	struct device_node *np = pdev->dev.of_node;
	struct vpu_subdev_data *data =
		devm_kzalloc(dev, sizeof(struct vpu_subdev_data), GFP_KERNEL);
#if defined(CONFIG_VCODEC_MMU)
	u32 iommu_en = 0;
	char mmu_dev_dts_name[40];
	of_property_read_u32(np, "iommu_enabled", &iommu_en);
#endif
	pr_info("probe device %s\n", dev_name(dev));

	data->pservice = pservice;
	data->dev = dev;

	of_property_read_string(np, "name", (const char**)&name);
	of_property_read_u32(np, "dev_mode", (u32*)&data->mode);
	/*dev_set_name(dev, name);*/

	if (pservice->reg_base == 0) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		data->regs = devm_ioremap_resource(dev, res);
		if (IS_ERR(data->regs)) {
			ret = PTR_ERR(data->regs);
			goto err;
		}
		ioaddr = res->start;
	} else {
		data->regs = pservice->reg_base;
		ioaddr = pservice->ioaddr;
	}

	clear_bit(MMU_ACTIVATED, &data->state);
	vcodec_enter_mode(data);
	ret = vpu_service_check_hw(data, ioaddr);
	if (ret < 0) {
		vpu_err("error: hw info check faild\n");
		goto err;
	}

	data->dec_dev.iosize = data->hw_info->dec_io_size;
	data->dec_dev.hwregs = (volatile u32 *)((u8 *)data->regs + data->hw_info->dec_offset);
	data->reg_size = data->dec_dev.iosize;

	if (data->mode == VCODEC_RUNNING_MODE_VPU) {
		data->enc_dev.iosize = data->hw_info->enc_io_size;
		data->reg_size = data->reg_size > data->enc_dev.iosize ? data->reg_size : data->enc_dev.iosize;
		data->enc_dev.hwregs = (volatile u32 *)((u8 *)data->regs + data->hw_info->enc_offset);
	}

	data->irq_enc = platform_get_irq_byname(pdev, "irq_enc");
	if (data->irq_enc > 0) {
		ret = devm_request_threaded_irq(dev,
			data->irq_enc, vepu_irq, vepu_isr,
			IRQF_SHARED, dev_name(dev),
			(void *)data);
		if (ret) {
			dev_err(dev,
				"error: can't request vepu irq %d\n",
				data->irq_enc);
			goto err;
		}
	}
	data->irq_dec = platform_get_irq_byname(pdev, "irq_dec");
	if (data->irq_dec > 0) {
		ret = devm_request_threaded_irq(dev,
			data->irq_dec, vdpu_irq, vdpu_isr,
			IRQF_SHARED, dev_name(dev),
			(void *)data);
		if (ret) {
			dev_err(dev,
				"error: can't request vdpu irq %d\n",
				data->irq_dec);
			goto err;
		}
	}
	atomic_set(&data->dec_dev.irq_count_codec, 0);
	atomic_set(&data->dec_dev.irq_count_pp, 0);
	atomic_set(&data->enc_dev.irq_count_codec, 0);
	atomic_set(&data->enc_dev.irq_count_pp, 0);
#if defined(CONFIG_VCODEC_MMU)
	if (iommu_en) {
		if (data->mode == VCODEC_RUNNING_MODE_HEVC)
			sprintf(mmu_dev_dts_name,
				HEVC_IOMMU_COMPATIBLE_NAME);
		else
			sprintf(mmu_dev_dts_name,
				VPU_IOMMU_COMPATIBLE_NAME);

		data->mmu_dev =
			rockchip_get_sysmmu_dev(mmu_dev_dts_name);

		if (data->mmu_dev)
			platform_set_sysmmu(data->mmu_dev, dev);

		rockchip_iovmm_set_fault_handler(dev, vcodec_sysmmu_fault_hdl);
	}
#endif
	get_hw_info(data);
	pservice->auto_freq = true;

	vcodec_exit_mode(data);
	/* create device node */
	ret = alloc_chrdev_region(&data->dev_t, 0, 1, name);
	if (ret) {
		dev_err(dev, "alloc dev_t failed\n");
		goto err;
	}

	cdev_init(&data->cdev, &vpu_service_fops);

	data->cdev.owner = THIS_MODULE;
	data->cdev.ops = &vpu_service_fops;

	ret = cdev_add(&data->cdev, data->dev_t, 1);

	if (ret) {
		dev_err(dev, "add dev_t failed\n");
		goto err;
	}

	data->cls = class_create(THIS_MODULE, name);

	if (IS_ERR(data->cls)) {
		ret = PTR_ERR(data->cls);
		dev_err(dev, "class_create err:%d\n", ret);
		goto err;
	}

	data->child_dev = device_create(data->cls, dev,
		data->dev_t, NULL, name);

	platform_set_drvdata(pdev, data);

	INIT_LIST_HEAD(&data->lnk_service);
	list_add_tail(&data->lnk_service, &pservice->subdev_list);

#ifdef CONFIG_DEBUG_FS
	data->debugfs_dir =
		vcodec_debugfs_create_device_dir((char*)name, parent);
	if (data->debugfs_dir == NULL)
		vpu_err("create debugfs dir %s failed\n", name);

	data->debugfs_file_regs =
		debugfs_create_file("regs", 0664,
				    data->debugfs_dir, data,
				    &debug_vcodec_fops);
#endif
	return 0;
err:
	if (data->irq_enc > 0)
		free_irq(data->irq_enc, (void *)data);
	if (data->irq_dec > 0)
		free_irq(data->irq_dec, (void *)data);

	if (data->child_dev) {
		device_destroy(data->cls, data->dev_t);
		cdev_del(&data->cdev);
		unregister_chrdev_region(data->dev_t, 1);
	}

	if (data->cls)
		class_destroy(data->cls);
	return -1;
}

static void vcodec_subdev_remove(struct vpu_subdev_data *data)
{
	device_destroy(data->cls, data->dev_t);
	class_destroy(data->cls);
	cdev_del(&data->cdev);
	unregister_chrdev_region(data->dev_t, 1);

	free_irq(data->irq_enc, (void *)&data);
	free_irq(data->irq_dec, (void *)&data);

#ifdef CONFIG_DEBUG_FS
	debugfs_remove_recursive(data->debugfs_dir);
#endif
}

static void vcodec_read_property(struct device_node *np,
	struct vpu_service_info *pservice)
{
	pservice->mode_bit = 0;
	pservice->mode_ctrl = 0;
	pservice->subcnt = 0;

	of_property_read_u32(np, "subcnt", &pservice->subcnt);

	if (pservice->subcnt > 1) {
		of_property_read_u32(np, "mode_bit", &pservice->mode_bit);
		of_property_read_u32(np, "mode_ctrl", &pservice->mode_ctrl);
	}
#ifdef CONFIG_MFD_SYSCON
	pservice->grf_base = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
#else
	pservice->grf_base = (u32*)RK_GRF_VIRT;
#endif
	if (IS_ERR(pservice->grf_base)) {
#ifdef CONFIG_ARM
		pservice->grf_base = RK_GRF_VIRT;
#else
		vpu_err("can't find vpu grf property\n");
		return;
#endif
	}

#ifdef CONFIG_RESET_CONTROLLER
	pservice->rst_a = devm_reset_control_get(pservice->dev, "video_a");
	pservice->rst_h = devm_reset_control_get(pservice->dev, "video_h");
	pservice->rst_v = devm_reset_control_get(pservice->dev, "video");

	if (IS_ERR_OR_NULL(pservice->rst_a)) {
		pr_warn("No reset resource define\n");
		pservice->rst_a = NULL;
	}

	if (IS_ERR_OR_NULL(pservice->rst_h)) {
		pr_warn("No reset resource define\n");
		pservice->rst_h = NULL;
	}

	if (IS_ERR_OR_NULL(pservice->rst_v)) {
		pr_warn("No reset resource define\n");
		pservice->rst_v = NULL;
	}
#endif

	of_property_read_string(np, "name", (const char**)&pservice->name);
}

static void vcodec_init_drvdata(struct vpu_service_info *pservice)
{
	pservice->dev_id = VCODEC_DEVICE_ID_VPU;
	pservice->curr_mode = -1;

	wake_lock_init(&pservice->wake_lock, WAKE_LOCK_SUSPEND, "vpu");
	INIT_LIST_HEAD(&pservice->waiting);
	INIT_LIST_HEAD(&pservice->running);
	mutex_init(&pservice->lock);

	INIT_LIST_HEAD(&pservice->done);
	INIT_LIST_HEAD(&pservice->session);
	INIT_LIST_HEAD(&pservice->subdev_list);

	pservice->reg_pproc	= NULL;
	atomic_set(&pservice->total_running, 0);
	atomic_set(&pservice->enabled,       0);
	atomic_set(&pservice->power_on_cnt,  0);
	atomic_set(&pservice->power_off_cnt, 0);
	atomic_set(&pservice->reset_request, 0);

	INIT_DELAYED_WORK(&pservice->power_off_work, vpu_power_off_work);

	pservice->ion_client = rockchip_ion_client_create("vpu");
	if (IS_ERR(pservice->ion_client)) {
		vpu_err("failed to create ion client for vcodec ret %ld\n",
			PTR_ERR(pservice->ion_client));
	} else {
		vpu_debug(DEBUG_IOMMU, "vcodec ion client create success!\n");
	}
}

static int vcodec_probe(struct platform_device *pdev)
{
	int i;
	int ret = 0;
	struct resource *res = NULL;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct vpu_service_info *pservice =
		devm_kzalloc(dev, sizeof(struct vpu_service_info), GFP_KERNEL);

	pr_info("probe device %s\n", dev_name(dev));

	pservice->dev = dev;

	vcodec_read_property(np, pservice);
	vcodec_init_drvdata(pservice);

	if (strncmp(pservice->name, "hevc_service", 12) == 0)
		pservice->dev_id = VCODEC_DEVICE_ID_HEVC;
	else if (strncmp(pservice->name, "vpu_service", 11) == 0)
		pservice->dev_id = VCODEC_DEVICE_ID_VPU;
	else
		pservice->dev_id = VCODEC_DEVICE_ID_COMBO;

	if (0 > vpu_get_clk(pservice))
		goto err;

	vpu_service_power_on(pservice);

	if (of_property_read_bool(np, "reg")) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

		pservice->reg_base = devm_ioremap_resource(pservice->dev, res);
		if (IS_ERR(pservice->reg_base)) {
			vpu_err("ioremap registers base failed\n");
			ret = PTR_ERR(pservice->reg_base);
			goto err;
		}
		pservice->ioaddr = res->start;
	} else {
		pservice->reg_base = 0;
	}

	if (of_property_read_bool(np, "subcnt")) {
		for (i = 0; i<pservice->subcnt; i++) {
			struct device_node *sub_np;
			struct platform_device *sub_pdev;
			sub_np = of_parse_phandle(np, "rockchip,sub", i);
			sub_pdev = of_find_device_by_node(sub_np);

			vcodec_subdev_probe(sub_pdev, pservice);
		}
	} else {
		vcodec_subdev_probe(pdev, pservice);
	}
	platform_set_drvdata(pdev, pservice);

	vpu_service_power_off(pservice);

	pr_info("init success\n");

	return 0;

err:
	pr_info("init failed\n");
	vpu_service_power_off(pservice);
	vpu_put_clk(pservice);
	wake_lock_destroy(&pservice->wake_lock);

	if (res)
		devm_release_mem_region(&pdev->dev, res->start, resource_size(res));

	return ret;
}

static int vcodec_remove(struct platform_device *pdev)
{
	struct vpu_service_info *pservice = platform_get_drvdata(pdev);
	struct resource *res;
	struct vpu_subdev_data *data, *n;

	list_for_each_entry_safe(data, n, &pservice->subdev_list, lnk_service) {
		vcodec_subdev_remove(data);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	devm_release_mem_region(&pdev->dev, res->start, resource_size(res));
	vpu_put_clk(pservice);
	wake_lock_destroy(&pservice->wake_lock);

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id vcodec_service_dt_ids[] = {
	{.compatible = "vpu_service",},
	{.compatible = "rockchip,hevc_service",},
	{.compatible = "rockchip,vpu_combo",},
	{},
};
#endif

static struct platform_driver vcodec_driver = {
	.probe = vcodec_probe,
	.remove = vcodec_remove,
	.driver = {
		.name = "vcodec",
		.owner = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(vcodec_service_dt_ids),
#endif
	},
};

static void get_hw_info(struct vpu_subdev_data *data)
{
	struct vpu_service_info *pservice = data->pservice;
	struct vpu_dec_config *dec = &pservice->dec_config;
	struct vpu_enc_config *enc = &pservice->enc_config;
	if (data->mode == VCODEC_RUNNING_MODE_VPU) {
		u32 configReg   = data->dec_dev.hwregs[VPU_DEC_HWCFG0];
		u32 asicID      = data->dec_dev.hwregs[0];

		dec->h264_support    = (configReg >> DWL_H264_E) & 0x3U;
		dec->jpegSupport    = (configReg >> DWL_JPEG_E) & 0x01U;
		if (dec->jpegSupport && ((configReg >> DWL_PJPEG_E) & 0x01U))
			dec->jpegSupport = JPEG_PROGRESSIVE;
		dec->mpeg4Support   = (configReg >> DWL_MPEG4_E) & 0x3U;
		dec->vc1Support     = (configReg >> DWL_VC1_E) & 0x3U;
		dec->mpeg2Support   = (configReg >> DWL_MPEG2_E) & 0x01U;
		dec->sorensonSparkSupport = (configReg >> DWL_SORENSONSPARK_E) & 0x01U;
		dec->refBufSupport  = (configReg >> DWL_REF_BUFF_E) & 0x01U;
		dec->vp6Support     = (configReg >> DWL_VP6_E) & 0x01U;

		dec->maxDecPicWidth = 4096;

		/* 2nd Config register */
		configReg   = data->dec_dev.hwregs[VPU_DEC_HWCFG1];
		if (dec->refBufSupport) {
			if ((configReg >> DWL_REF_BUFF_ILACE_E) & 0x01U)
				dec->refBufSupport |= 2;
			if ((configReg >> DWL_REF_BUFF_DOUBLE_E) & 0x01U)
				dec->refBufSupport |= 4;
		}
		dec->customMpeg4Support = (configReg >> DWL_MPEG4_CUSTOM_E) & 0x01U;
		dec->vp7Support     = (configReg >> DWL_VP7_E) & 0x01U;
		dec->vp8Support     = (configReg >> DWL_VP8_E) & 0x01U;
		dec->avsSupport     = (configReg >> DWL_AVS_E) & 0x01U;

		/* JPEG xtensions */
		if (((asicID >> 16) >= 0x8190U) || ((asicID >> 16) == 0x6731U))
			dec->jpegESupport = (configReg >> DWL_JPEG_EXT_E) & 0x01U;
		else
			dec->jpegESupport = JPEG_EXT_NOT_SUPPORTED;

		if (((asicID >> 16) >= 0x9170U) || ((asicID >> 16) == 0x6731U) )
			dec->rvSupport = (configReg >> DWL_RV_E) & 0x03U;
		else
			dec->rvSupport = RV_NOT_SUPPORTED;
		dec->mvcSupport = (configReg >> DWL_MVC_E) & 0x03U;

		if (dec->refBufSupport && (asicID >> 16) == 0x6731U )
			dec->refBufSupport |= 8; /* enable HW support for offset */

		if (!cpu_is_rk3036()) {
			configReg = data->enc_dev.hwregs[63];
			enc->maxEncodedWidth = configReg & ((1 << 11) - 1);
			enc->h264Enabled = (configReg >> 27) & 1;
			enc->mpeg4Enabled = (configReg >> 26) & 1;
			enc->jpegEnabled = (configReg >> 25) & 1;
			enc->vsEnabled = (configReg >> 24) & 1;
			enc->rgbEnabled = (configReg >> 28) & 1;
			enc->reg_size = data->reg_size;
			enc->reserv[0] = enc->reserv[1] = 0;
		}
		pservice->auto_freq = true;
		vpu_debug(DEBUG_EXTRA_INFO, "vpu_service set to auto frequency mode\n");
		atomic_set(&pservice->freq_status, VPU_FREQ_BUT);

		pservice->bug_dec_addr = cpu_is_rk30xx();
	} else {
		if (cpu_is_rk3036()  || cpu_is_rk312x())
			dec->maxDecPicWidth = 1920;
		else
			dec->maxDecPicWidth = 4096;
		/* disable frequency switch in hevc.*/
		pservice->auto_freq = false;
	}
}

static bool check_irq_err(task_info *task, u32 irq_status)
{
	return (task->error_mask & irq_status) ? true : false;
}

static irqreturn_t vdpu_irq(int irq, void *dev_id)
{
	struct vpu_subdev_data *data = (struct vpu_subdev_data*)dev_id;
	struct vpu_service_info *pservice = data->pservice;
	vpu_device *dev = &data->dec_dev;
	u32 raw_status;
	u32 dec_status;

	/*vcodec_enter_mode(data);*/

	dec_status = raw_status = readl(dev->hwregs + DEC_INTERRUPT_REGISTER);

	if (dec_status & DEC_INTERRUPT_BIT) {
		time_record(&tasks[TASK_VPU_DEC], 1);
		vpu_debug(DEBUG_IRQ_STATUS, "vdpu_irq dec status %08x\n", dec_status);
		if ((dec_status & 0x40001) == 0x40001) {
			do {
				dec_status =
					readl(dev->hwregs +
						DEC_INTERRUPT_REGISTER);
			} while ((dec_status & 0x40001) == 0x40001);
		}

		if (check_irq_err((data->hw_info->hw_id == HEVC_ID)?
					(&tasks[TASK_RKDEC_HEVC]) : (&tasks[TASK_VPU_DEC]),
					dec_status)) {
			atomic_add(1, &pservice->reset_request);
		}

		writel(0, dev->hwregs + DEC_INTERRUPT_REGISTER);
		atomic_add(1, &dev->irq_count_codec);
		time_diff(&tasks[TASK_VPU_DEC]);
	}

	if (data->hw_info->hw_id != HEVC_ID) {
		u32 pp_status = readl(dev->hwregs + PP_INTERRUPT_REGISTER);
		if (pp_status & PP_INTERRUPT_BIT) {
			time_record(&tasks[TASK_VPU_PP], 1);
			vpu_debug(DEBUG_IRQ_STATUS, "vdpu_irq pp status %08x\n", pp_status);

			if (check_irq_err(&tasks[TASK_VPU_PP], dec_status))
				atomic_add(1, &pservice->reset_request);

			/* clear pp IRQ */
			writel(pp_status & (~DEC_INTERRUPT_BIT), dev->hwregs + PP_INTERRUPT_REGISTER);
			atomic_add(1, &dev->irq_count_pp);
			time_diff(&tasks[TASK_VPU_PP]);
		}
	}

	pservice->irq_status = raw_status;

	/*vcodec_exit_mode(pservice);*/

	if (atomic_read(&dev->irq_count_pp) ||
	    atomic_read(&dev->irq_count_codec))
		return IRQ_WAKE_THREAD;
	else
		return IRQ_NONE;
}

static irqreturn_t vdpu_isr(int irq, void *dev_id)
{
	struct vpu_subdev_data *data = (struct vpu_subdev_data*)dev_id;
	struct vpu_service_info *pservice = data->pservice;
	vpu_device *dev = &data->dec_dev;

	mutex_lock(&pservice->lock);
	if (atomic_read(&dev->irq_count_codec)) {
		atomic_sub(1, &dev->irq_count_codec);
		if (NULL == pservice->reg_codec) {
			vpu_err("error: dec isr with no task waiting\n");
		} else {
			reg_from_run_to_done(data, pservice->reg_codec);
			/* avoid vpu timeout and can't recover problem */
			VDPU_SOFT_RESET(data->regs);
		}
	}

	if (atomic_read(&dev->irq_count_pp)) {
		atomic_sub(1, &dev->irq_count_pp);
		if (NULL == pservice->reg_pproc) {
			vpu_err("error: pp isr with no task waiting\n");
		} else {
			reg_from_run_to_done(data, pservice->reg_pproc);
		}
	}
	try_set_reg(data);
	mutex_unlock(&pservice->lock);
	return IRQ_HANDLED;
}

static irqreturn_t vepu_irq(int irq, void *dev_id)
{
	struct vpu_subdev_data *data = (struct vpu_subdev_data*)dev_id;
	struct vpu_service_info *pservice = data->pservice;
	vpu_device *dev = &data->enc_dev;
	u32 irq_status;

	/*vcodec_enter_mode(data);*/
	irq_status= readl(dev->hwregs + ENC_INTERRUPT_REGISTER);

	vpu_debug(DEBUG_IRQ_STATUS, "vepu_irq irq status %x\n", irq_status);

	if (likely(irq_status & ENC_INTERRUPT_BIT)) {
		time_record(&tasks[TASK_VPU_ENC], 1);

		if (check_irq_err(&tasks[TASK_VPU_ENC], irq_status))
			atomic_add(1, &pservice->reset_request);

		/* clear enc IRQ */
		writel(irq_status & (~ENC_INTERRUPT_BIT), dev->hwregs + ENC_INTERRUPT_REGISTER);
		atomic_add(1, &dev->irq_count_codec);
		time_diff(&tasks[TASK_VPU_ENC]);
	}

	pservice->irq_status = irq_status;

	/*vcodec_exit_mode(pservice);*/

	if (atomic_read(&dev->irq_count_codec))
		return IRQ_WAKE_THREAD;
	else
		return IRQ_NONE;
}

static irqreturn_t vepu_isr(int irq, void *dev_id)
{
	struct vpu_subdev_data *data = (struct vpu_subdev_data*)dev_id;
	struct vpu_service_info *pservice = data->pservice;
	vpu_device *dev = &data->enc_dev;

	mutex_lock(&pservice->lock);
	if (atomic_read(&dev->irq_count_codec)) {
		atomic_sub(1, &dev->irq_count_codec);
		if (NULL == pservice->reg_codec) {
			vpu_err("error: enc isr with no task waiting\n");
		} else {
			reg_from_run_to_done(data, pservice->reg_codec);
		}
	}
	try_set_reg(data);
	mutex_unlock(&pservice->lock);
	return IRQ_HANDLED;
}

static int __init vcodec_service_init(void)
{
	int ret;

	if ((ret = platform_driver_register(&vcodec_driver)) != 0) {
		vpu_err("Platform device register failed (%d).\n", ret);
		return ret;
	}

#ifdef CONFIG_DEBUG_FS
	vcodec_debugfs_init();
#endif

	return ret;
}

static void __exit vcodec_service_exit(void)
{
#ifdef CONFIG_DEBUG_FS
	vcodec_debugfs_exit();
#endif

	platform_driver_unregister(&vcodec_driver);
}

module_init(vcodec_service_init);
module_exit(vcodec_service_exit);

#ifdef CONFIG_DEBUG_FS
#include <linux/seq_file.h>

static int vcodec_debugfs_init()
{
	parent = debugfs_create_dir("vcodec", NULL);
	if (!parent)
		return -1;

	return 0;
}

static void vcodec_debugfs_exit()
{
	debugfs_remove(parent);
}

static struct dentry* vcodec_debugfs_create_device_dir(char *dirname, struct dentry *parent)
{
	return debugfs_create_dir(dirname, parent);
}

static int debug_vcodec_show(struct seq_file *s, void *unused)
{
	struct vpu_subdev_data *data = s->private;
	struct vpu_service_info *pservice = data->pservice;
	unsigned int i, n;
	vpu_reg *reg, *reg_tmp;
	vpu_session *session, *session_tmp;

	mutex_lock(&pservice->lock);
	vpu_service_power_on(pservice);
	if (data->hw_info->hw_id != HEVC_ID) {
		seq_printf(s, "\nENC Registers:\n");
		n = data->enc_dev.iosize >> 2;
		for (i = 0; i < n; i++)
			seq_printf(s, "\tswreg%d = %08X\n", i, readl(data->enc_dev.hwregs + i));
	}
	seq_printf(s, "\nDEC Registers:\n");
	n = data->dec_dev.iosize >> 2;
	for (i = 0; i < n; i++)
		seq_printf(s, "\tswreg%d = %08X\n", i, readl(data->dec_dev.hwregs + i));

	seq_printf(s, "\nvpu service status:\n");
	list_for_each_entry_safe(session, session_tmp, &pservice->session, list_session) {
		seq_printf(s, "session pid %d type %d:\n", session->pid, session->type);
		/*seq_printf(s, "waiting reg set %d\n");*/
		list_for_each_entry_safe(reg, reg_tmp, &session->waiting, session_link) {
			seq_printf(s, "waiting register set\n");
		}
		list_for_each_entry_safe(reg, reg_tmp, &session->running, session_link) {
			seq_printf(s, "running register set\n");
		}
		list_for_each_entry_safe(reg, reg_tmp, &session->done, session_link) {
			seq_printf(s, "done    register set\n");
		}
	}

	seq_printf(s, "\npower counter: on %d off %d\n",
			atomic_read(&pservice->power_on_cnt),
			atomic_read(&pservice->power_off_cnt));
	mutex_unlock(&pservice->lock);
	vpu_service_power_off(pservice);

	return 0;
}

static int debug_vcodec_open(struct inode *inode, struct file *file)
{
	return single_open(file, debug_vcodec_show, inode->i_private);
}

#endif

#if HEVC_TEST_ENABLE & defined(CONFIG_ION_ROCKCHIP)
#include "hevc_test_inc/pps_00.h"
#include "hevc_test_inc/register_00.h"
#include "hevc_test_inc/rps_00.h"
#include "hevc_test_inc/scaling_list_00.h"
#include "hevc_test_inc/stream_00.h"

#include "hevc_test_inc/pps_01.h"
#include "hevc_test_inc/register_01.h"
#include "hevc_test_inc/rps_01.h"
#include "hevc_test_inc/scaling_list_01.h"
#include "hevc_test_inc/stream_01.h"

#include "hevc_test_inc/cabac.h"

extern struct ion_client *rockchip_ion_client_create(const char * name);

static struct ion_client *ion_client = NULL;
u8* get_align_ptr(u8* tbl, int len, u32 *phy)
{
	int size = (len+15) & (~15);
	struct ion_handle *handle;
	u8 *ptr;

	if (ion_client == NULL)
		ion_client = rockchip_ion_client_create("vcodec");

	handle = ion_alloc(ion_client, (size_t)len, 16, ION_HEAP(ION_CMA_HEAP_ID), 0);

	ptr = ion_map_kernel(ion_client, handle);

	ion_phys(ion_client, handle, phy, &size);

	memcpy(ptr, tbl, len);

	return ptr;
}

u8* get_align_ptr_no_copy(int len, u32 *phy)
{
	int size = (len+15) & (~15);
	struct ion_handle *handle;
	u8 *ptr;

	if (ion_client == NULL)
		ion_client = rockchip_ion_client_create("vcodec");

	handle = ion_alloc(ion_client, (size_t)len, 16, ION_HEAP(ION_CMA_HEAP_ID), 0);

	ptr = ion_map_kernel(ion_client, handle);

	ion_phys(ion_client, handle, phy, &size);

	return ptr;
}

#define TEST_CNT    2
static int hevc_test_case0(vpu_service_info *pservice)
{
	vpu_session session;
	vpu_reg *reg;
	unsigned long size = 272;
	int testidx = 0;
	int ret = 0;
	u8 *pps_tbl[TEST_CNT];
	u8 *register_tbl[TEST_CNT];
	u8 *rps_tbl[TEST_CNT];
	u8 *scaling_list_tbl[TEST_CNT];
	u8 *stream_tbl[TEST_CNT];

	int stream_size[2];
	int pps_size[2];
	int rps_size[2];
	int scl_size[2];
	int cabac_size[2];

	u32 phy_pps;
	u32 phy_rps;
	u32 phy_scl;
	u32 phy_str;
	u32 phy_yuv;
	u32 phy_ref;
	u32 phy_cabac;

	volatile u8 *stream_buf;
	volatile u8 *pps_buf;
	volatile u8 *rps_buf;
	volatile u8 *scl_buf;
	volatile u8 *yuv_buf;
	volatile u8 *cabac_buf;
	volatile u8 *ref_buf;

	u8 *pps;
	u8 *yuv[2];
	int i;

	pps_tbl[0] = pps_00;
	pps_tbl[1] = pps_01;

	register_tbl[0] = register_00;
	register_tbl[1] = register_01;

	rps_tbl[0] = rps_00;
	rps_tbl[1] = rps_01;

	scaling_list_tbl[0] = scaling_list_00;
	scaling_list_tbl[1] = scaling_list_01;

	stream_tbl[0] = stream_00;
	stream_tbl[1] = stream_01;

	stream_size[0] = sizeof(stream_00);
	stream_size[1] = sizeof(stream_01);

	pps_size[0] = sizeof(pps_00);
	pps_size[1] = sizeof(pps_01);

	rps_size[0] = sizeof(rps_00);
	rps_size[1] = sizeof(rps_01);

	scl_size[0] = sizeof(scaling_list_00);
	scl_size[1] = sizeof(scaling_list_01);

	cabac_size[0] = sizeof(Cabac_table);
	cabac_size[1] = sizeof(Cabac_table);

	/* create session */
	session.pid = current->pid;
	session.type = VPU_DEC;
	INIT_LIST_HEAD(&session.waiting);
	INIT_LIST_HEAD(&session.running);
	INIT_LIST_HEAD(&session.done);
	INIT_LIST_HEAD(&session.list_session);
	init_waitqueue_head(&session.wait);
	atomic_set(&session.task_running, 0);
	list_add_tail(&session.list_session, &pservice->session);

	yuv[0] = get_align_ptr_no_copy(256*256*2, &phy_yuv);
	yuv[1] = get_align_ptr_no_copy(256*256*2, &phy_ref);

	while (testidx < TEST_CNT) {
		/* create registers */
		reg = kmalloc(sizeof(vpu_reg)+pservice->reg_size, GFP_KERNEL);
		if (NULL == reg) {
			vpu_err("error: kmalloc fail in reg_init\n");
			return -1;
		}

		if (size > pservice->reg_size) {
			printk("warning: vpu reg size %lu is larger than hw reg size %lu\n", size, pservice->reg_size);
			size = pservice->reg_size;
		}
		reg->session = &session;
		reg->type = session.type;
		reg->size = size;
		reg->freq = VPU_FREQ_DEFAULT;
		reg->reg = (unsigned long *)&reg[1];
		INIT_LIST_HEAD(&reg->session_link);
		INIT_LIST_HEAD(&reg->status_link);

		/* TODO: stuff registers */
		memcpy(&reg->reg[0], register_tbl[testidx], /*sizeof(register_00)*/ 176);

		stream_buf = get_align_ptr(stream_tbl[testidx], stream_size[testidx], &phy_str);
		pps_buf = get_align_ptr(pps_tbl[0], pps_size[0], &phy_pps);
		rps_buf = get_align_ptr(rps_tbl[testidx], rps_size[testidx], &phy_rps);
		scl_buf = get_align_ptr(scaling_list_tbl[testidx], scl_size[testidx], &phy_scl);
		cabac_buf = get_align_ptr(Cabac_table, cabac_size[testidx], &phy_cabac);

		pps = pps_buf;

		/* TODO: replace reigster address */
		for (i=0; i<64; i++) {
			u32 scaling_offset;
			u32 tmp;

			scaling_offset = (u32)pps[i*80+74];
			scaling_offset += (u32)pps[i*80+75] << 8;
			scaling_offset += (u32)pps[i*80+76] << 16;
			scaling_offset += (u32)pps[i*80+77] << 24;

			tmp = phy_scl + scaling_offset;

			pps[i*80+74] = tmp & 0xff;
			pps[i*80+75] = (tmp >> 8) & 0xff;
			pps[i*80+76] = (tmp >> 16) & 0xff;
			pps[i*80+77] = (tmp >> 24) & 0xff;
		}

		printk("%s %d, phy stream %08x, phy pps %08x, phy rps %08x\n",
			__func__, __LINE__, phy_str, phy_pps, phy_rps);

		reg->reg[1] = 0x21;
		reg->reg[4] = phy_str;
		reg->reg[5] = ((stream_size[testidx]+15)&(~15))+64;
		reg->reg[6] = phy_cabac;
		reg->reg[7] = testidx?phy_ref:phy_yuv;
		reg->reg[42] = phy_pps;
		reg->reg[43] = phy_rps;
		for (i = 10; i <= 24; i++)
			reg->reg[i] = phy_yuv;

		mutex_lock(pservice->lock);
		list_add_tail(&reg->status_link, &pservice->waiting);
		list_add_tail(&reg->session_link, &session.waiting);
		mutex_unlock(pservice->lock);

		/* stuff hardware */
		try_set_reg(data);

		/* wait for result */
		ret = wait_event_timeout(session.wait, !list_empty(&session.done), VPU_TIMEOUT_DELAY);
		if (!list_empty(&session.done)) {
			if (ret < 0)
				vpu_err("warning: pid %d wait task sucess but wait_evernt ret %d\n", session.pid, ret);
			ret = 0;
		} else {
			if (unlikely(ret < 0)) {
				vpu_err("error: pid %d wait task ret %d\n", session.pid, ret);
			} else if (0 == ret) {
				vpu_err("error: pid %d wait %d task done timeout\n", session.pid, atomic_read(&session.task_running));
				ret = -ETIMEDOUT;
			}
		}
		if (ret < 0) {
			int task_running = atomic_read(&session.task_running);
			int n;
			mutex_lock(pservice->lock);
			vpu_service_dump(pservice);
			if (task_running) {
				atomic_set(&session.task_running, 0);
				atomic_sub(task_running, &pservice->total_running);
				printk("%d task is running but not return, reset hardware...", task_running);
				vpu_reset(data);
				printk("done\n");
			}
			vpu_service_session_clear(pservice, &session);
			mutex_unlock(pservice->lock);

			printk("\nDEC Registers:\n");
			n = data->dec_dev.iosize >> 2;
			for (i=0; i<n; i++)
				printk("\tswreg%d = %08X\n", i, readl(data->dec_dev.hwregs + i));

			vpu_err("test index %d failed\n", testidx);
			break;
		} else {
			vpu_debug(DEBUG_EXTRA_INFO, "test index %d success\n", testidx);

			vpu_reg *reg = list_entry(session.done.next, vpu_reg, session_link);

			for (i=0; i<68; i++) {
				if (i % 4 == 0)
					printk("%02d: ", i);
				printk("%08x ", reg->reg[i]);
				if ((i+1) % 4 == 0)
					printk("\n");
			}

			testidx++;
		}

		reg_deinit(data, reg);
	}

	return 0;
}

#endif

