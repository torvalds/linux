/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd
 *
 * author:
 *	Alpha Lin, alpha.lin@rock-chips.com
 *	Randy Li, randy.li@rock-chips.com
 *	Ding Wei, leo.ding@rock-chips.com
 *
 */
#ifndef __ROCKCHIP_MPP_COMMON_H__
#define __ROCKCHIP_MPP_COMMON_H__

#include <linux/cdev.h>
#include <linux/clk.h>
#include <linux/dma-buf.h>
#include <linux/kfifo.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include <linux/reset.h>
#include <linux/irqreturn.h>
#include <linux/poll.h>

#define MHZ			(1000 * 1000)

#define EXTRA_INFO_MAGIC	(0x4C4A46)
#define JPEG_IOC_EXTRA_SIZE	(48)

/* Use 'l' as magic number */
#define MPP_IOC_MAGIC			'l'

#define MPP_IOC_SET_CLIENT_TYPE		_IOW(MPP_IOC_MAGIC, 1, __u32)
#define MPP_IOC_GET_HW_FUSE_STATUS	_IOW(MPP_IOC_MAGIC, 2, unsigned long)

#define MPP_IOC_SET_REG			_IOW(MPP_IOC_MAGIC, 3, unsigned long)
#define MPP_IOC_GET_REG			_IOW(MPP_IOC_MAGIC, 4, unsigned long)

#define MPP_IOC_PROBE_IOMMU_STATUS	_IOR(MPP_IOC_MAGIC, 5, __u32)
#define MPP_IOC_SET_DRIVER_DATA		_IOW(MPP_IOC_MAGIC, 64, u32)

#define MPP_IOC_CUSTOM_BASE	(0x1000)

enum MPP_DEVICE_TYPE {
	MPP_DEVICE_ENC		= 0x0,
	MPP_DEVICE_DEC		= 0x1,
	MPP_DEVICE_PP		= 0x2,
	MPP_DEVICE_DEC_PP	= 0x3,
	MPP_DEVICE_DEC_HEVC	= 0x4,
	MPP_DEVICE_DEC_RKV	= 0x5,
	MPP_DEVICE_ENC_RKV	= 0x6,
	MPP_DEVICE_DEC_AVSPLUS	= 0x7,
	MPP_DEVICE_ENC_VEPU22	= 0x8,
	MPP_DEVICE_BUTT,
};

enum MPP_DRIVER_TYPE {
	MPP_DRIVER_NULL = 0,
	MPP_DRIVER_VDPU1,
	MPP_DRIVER_VEPU1,
	MPP_DRIVER_VDPU2,
	MPP_DRIVER_VEPU2,
	MPP_DRIVER_VEPU22,
	MPP_DRIVER_RKVDEC,
	MPP_DRIVER_RKVENC,
	MPP_DRIVER_BUTT,
};

struct mpp_request {
	__u32 *req;
	__u32 size;
};

struct mpp_grf_info {
	u32 offset;
	u32 val;
	struct regmap *grf;
};

/**
 * struct for hardware info
 */
struct mpp_hw_info {
	/* register number */
	u32 reg_num;
	/* start index of register */
	u32 regidx_start;
	/* end index of register */
	u32 regidx_end;
	/* register of enable hardware */
	int regidx_en;
};

struct mpp_trans_info {
	const int count;
	const char * const table;
};

struct extra_info_elem {
	u32 index;
	u32 offset;
};

struct extra_info_for_iommu {
	u32 magic;
	u32 cnt;
	struct extra_info_elem elem[20];
};

struct mpp_dev_var {
	enum MPP_DEVICE_TYPE device_type;

	/* info for each hardware */
	struct mpp_hw_info *hw_info;
	struct mpp_trans_info *trans_info;
	struct mpp_hw_ops *hw_ops;
	struct mpp_dev_ops *dev_ops;
};

struct mpp_mem_region {
	struct list_head srv_lnk;
	struct list_head reg_lnk;
	struct list_head session_lnk;
	/* address for iommu */
	dma_addr_t iova;
	unsigned long len;
	u32 reg_idx;
	void *hdl;
};

struct mpp_dma_session;

struct mpp_taskqueue;

struct mpp_dev {
	struct device *dev;
	const struct mpp_dev_var *var;
	struct mpp_hw_ops *hw_ops;
	struct mpp_dev_ops *dev_ops;

	int irq;
	u32 irq_status;

	void __iomem *reg_base;
	struct mpp_grf_info *grf_info;
	struct mpp_iommu_info *iommu_info;

	struct rw_semaphore rw_sem;
	/* lock for reset */
	struct mutex reset_lock;
	atomic_t reset_request;
	atomic_t total_running;
	/* task for work queue */
	struct workqueue_struct *workq;
	/* set session max buffers */
	u32 session_max_buffers;
	/* point to MPP Service */
	struct mpp_taskqueue *queue;
	struct mpp_service *srv;
};

struct mpp_task;

struct mpp_session {
	enum MPP_DEVICE_TYPE device_type;
	/* the session related device private data */
	struct mpp_service *srv;
	struct mpp_dev *mpp;
	struct mpp_dma_session *dma;

	/* session tasks list lock */
	struct mutex list_lock;
	struct list_head pending;

	DECLARE_KFIFO_PTR(done_fifo, struct mpp_task *);

	wait_queue_head_t wait;
	pid_t pid;
	atomic_t task_running;
};

/* The context for the a task */
struct mpp_task {
	/* context belong to */
	struct mpp_session *session;

	/* link to session pending */
	struct list_head session_link;
	/* link to service node pending */
	struct list_head service_link;
	/* The DMA buffer used in this task */
	struct list_head mem_region_list;

	/* record context running start time */
	struct timeval start;
};

struct mpp_taskqueue {
	/* taskqueue structure global lock */
	struct mutex lock;
	/* lock for task add and del */
	struct mutex list_lock;
	/* work for taskqueue */
	struct work_struct work;

	struct list_head pending;
	atomic_t running;
	struct mpp_task *cur_task;
	struct mpp_service *srv;
};

struct mpp_service {
	struct class *cls;
	struct device *dev;
	dev_t dev_id;
	struct cdev mpp_cdev;
	struct device *child_dev;
#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
#endif
	atomic_t shutdown_request;
	/* follows for device probe */
	struct mpp_grf_info grf_infos[MPP_DRIVER_BUTT];
	struct platform_driver *sub_drivers[MPP_DRIVER_BUTT];
	/* follows for attach service */
	struct mpp_dev *sub_devices[MPP_DEVICE_BUTT];
	struct mpp_taskqueue *task_queues[MPP_DEVICE_BUTT];
};

/*
 * struct mpp_hw_ops - context specific operations for device
 * @init	Do something when hardware probe.
 * @exit	Do something when hardware remove.
 * @power_on	Get pm and enable clks.
 * @power_off	Put pm and disable clks.
 * @get_freq	Get special freq for setting.
 * @set_freq	Set freq to hardware.
 * @reduce_freq	Reduce freq when hardware is not running.
 * @reset	When error, reset hardware.
 */
struct mpp_hw_ops {
	int (*init)(struct mpp_dev *mpp);
	int (*exit)(struct mpp_dev *mpp);
	int (*power_on)(struct mpp_dev *mpp);
	int (*power_off)(struct mpp_dev *mpp);
	int (*get_freq)(struct mpp_dev *mpp,
			struct mpp_task *mpp_task);
	int (*set_freq)(struct mpp_dev *mpp,
			struct mpp_task *mpp_task);
	int (*reduce_freq)(struct mpp_dev *mpp);
	int (*reset)(struct mpp_dev *mpp);
};

/*
 * struct mpp_dev_ops - context specific operations for task
 * @alloc_task	Alloc and set task.
 * @prepare	Check HW status for determining run next task or not.
 * @run		Start a single {en,de}coding run. Set registers to hardware.
 * @irq		Deal with hardware interrupt top-half.
 * @isr		Deal with hardware interrupt bottom-half.
 * @finish	Read back processing results and additional data from hardware.
 * @result	Read status to userspace.
 * @free_task	Release the resource allocate which alloc.
 * @ioctl	Special cammand from userspace.
 * @open	Open a instance for hardware when set client.
 * @release	Specific instance release operation for hardware.
 * @free	Specific instance free operation for hardware.
 */
struct mpp_dev_ops {
	void *(*alloc_task)(struct mpp_session *session,
			    void __user *src, u32 size);
	int (*prepare)(struct mpp_dev *mpp, struct mpp_task *task);
	int (*run)(struct mpp_dev *mpp, struct mpp_task *task);
	int (*irq)(struct mpp_dev *mpp);
	int (*isr)(struct mpp_dev *mpp);
	int (*finish)(struct mpp_dev *mpp, struct mpp_task *task);
	int (*result)(struct mpp_dev *mpp, struct mpp_task *task,
		      u32 __user *dst, u32 size);
	int (*free_task)(struct mpp_session *session,
			 struct mpp_task *task);
	long (*ioctl)(struct mpp_session *isession,
		      unsigned int cmd, unsigned long arg);
	struct mpp_session *(*init_session)(struct mpp_dev *mpp);
	int (*release_session)(struct mpp_session *session);
};

int mpp_taskqueue_init(struct mpp_taskqueue *queue,
		       struct mpp_service *srv);

/* It can handle the default ioctl */
long mpp_dev_ioctl(struct file *filp, unsigned int cmd,
		   unsigned long arg);
#ifdef CONFIG_COMPAT
long mpp_dev_compat_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg);
#endif
int mpp_dev_open(struct inode *inode, struct file *filp);
int mpp_dev_release(struct inode *inode, struct file *filp);
unsigned int mpp_dev_poll(struct file *filp, poll_table *wait);

struct mpp_mem_region *
mpp_task_attach_fd(struct mpp_task *task, int fd);
int mpp_translate_reg_address(struct mpp_dev *data,
			      struct mpp_task *task,
			      int fmt, u32 *reg);
int mpp_translate_extra_info(struct mpp_task *task,
			     struct extra_info_for_iommu *ext_inf,
			     u32 *reg);

int mpp_task_init(struct mpp_session *session,
		  struct mpp_task *task);
int mpp_task_finish(struct mpp_session *session,
		    struct mpp_task *task);
int mpp_task_finalize(struct mpp_session *session,
		      struct mpp_task *task);

int mpp_dev_probe(struct mpp_dev *mpp,
		  struct platform_device *pdev);
int mpp_dev_remove(struct mpp_dev *mpp);

irqreturn_t mpp_dev_irq(int irq, void *param);
irqreturn_t mpp_dev_isr_sched(int irq, void *param);

int mpp_safe_reset(struct reset_control *rst);
int mpp_safe_unreset(struct reset_control *rst);

int mpp_set_grf(struct mpp_grf_info *grf_info);

int mpp_time_record(struct mpp_task *task);
int mpp_time_diff(struct mpp_task *task);

int mpp_dump_reg(u32 *regs, u32 start_idx, u32 end_idx);

static inline int mpp_write(struct mpp_dev *mpp, u32 reg, u32 val)
{
	int idx = reg / sizeof(u32);

	mpp_debug(DEBUG_SET_REG, "write reg[%d]: %08x\n", idx, val);
	writel(val, mpp->reg_base + reg);

	return 0;
}

static inline int mpp_write_relaxed(struct mpp_dev *mpp, u32 reg, u32 val)
{
	int idx = reg / sizeof(u32);

	mpp_debug(DEBUG_SET_REG, "write reg[%d]: %08x\n", idx, val);
	writel_relaxed(val, mpp->reg_base + reg);

	return 0;
}

static inline u32 mpp_read(struct mpp_dev *mpp, u32 reg)
{
	int idx = reg / sizeof(u32);
	u32 val = readl(mpp->reg_base + reg);

	mpp_debug(DEBUG_GET_REG, "read reg[%d] 0x%x: %08x\n", idx, reg, val);

	return val;
}

static inline u32 mpp_read_relaxed(struct mpp_dev *mpp, u32 reg)
{
	int idx = reg / sizeof(u32);
	u32 val = readl_relaxed(mpp->reg_base + reg);

	mpp_debug(DEBUG_GET_REG, "read reg[%d] 0x%x: %08x\n", idx, reg, val);

	return val;
}

extern struct platform_driver rockchip_rkvdec_driver;
extern struct platform_driver rockchip_rkvenc_driver;
extern struct platform_driver rockchip_vdpu1_driver;
extern struct platform_driver rockchip_vepu1_driver;
extern struct platform_driver rockchip_vdpu2_driver;
extern struct platform_driver rockchip_vepu2_driver;
extern struct platform_driver rockchip_vepu22_driver;

#endif
