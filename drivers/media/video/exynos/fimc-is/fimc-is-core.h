/*
 * Samsung Exynos4 SoC series FIMC-IS slave interface driver
 *
 * Driver internal Header
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 * Contact: Younghwan Joo, <yhwan.joo@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_CORE_H_
#define FIMC_IS_CORE_H_

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/memory.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#include <linux/videodev2.h>
#include <linux/videodev2_samsung.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-mediabus.h>
#include <media/exynos_fimc_is.h>
#include <mach/dev.h>

#include <media/videobuf2-core.h>
#include <media/videobuf2-cma-phys.h>
#define MODULE_NAME	"exynos4-fimc-is"

#define MAX_I2H_ARG 12
#define NUM_FIMC_IS_CLOCKS 1

#define FIMC_IS_MAX_BUF_NUM		8
#define FIMC_IS_MAX_BUf_PLANE_NUM	3
#define FIMC_IS_SENSOR_NUM	2

/* Time - out definitions */
#define FIMC_IS_SHUTDOWN_TIMEOUT	(3*HZ)
#define FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR	(HZ)
#define FIMC_IS_SHUTDOWN_TIMEOUT_AF	(3*HZ)
/* Memory definitions */
#define FIMC_IS_MEM_FW			"f"
#define FIMC_IS_MEM_ISP_BUF		"i"

#define FIMC_IS_A5_MEM_SIZE		0x00A00000
#define FIMC_IS_REGION_SIZE		0x5000

#define FIMC_IS_DEBUG_REGION_ADDR	0x00840000
#define FIMC_IS_SHARED_REGION_ADDR	0x008C0000
#define FIMC_IS_FW_INFO_LENGTH		32
#define FIMC_IS_FW_VERSION_LENGTH	7
#define FIMC_IS_SETFILE_INFO_LENGTH	39

#define FIMC_IS_EXTRA_MEM_SIZE		(FIMC_IS_EXTRA_FW_SIZE +\
					FIMC_IS_EXTRA_SETFILE_SIZE +\
					0x1000)
#define FIMC_IS_EXTRA_FW_SIZE		0x180000
#define FIMC_IS_EXTRA_SETFILE_SIZE	0x4B000

#define GED_FD_RANGE			1000

#define BUS_LOCK_FREQ_L0	400200
#define BUS_LOCK_FREQ_L1	267200
#define BUS_LOCK_FREQ_L2	267160
#define BUS_LOCK_FREQ_L3	160160
#define BUS_LOCK_FREQ_L4	133133
#define BUS_LOCK_FREQ_L5	100100

/* A5 debug message setting */
#define FIMC_IS_DEBUG_MSG	0x3F
#define FIMC_IS_DEBUG_LEVEL	3

#define SDCARD_FW

#ifdef SDCARD_FW
#define FIMC_IS_FW_SDCARD		"/sdcard/fimc_is_fw.bin"
#define FIMC_IS_SETFILE_SDCARD_6A3	"/sdcard/setfile.bin"
#define FIMC_IS_SETFILE_SDCARD_3H7	"/sdcard/setfile_S5K3H7.bin"
#endif
#define FIMC_IS_FW		"fimc_is_fw.bin"
#define FIMC_IS_SETFILE_6A3	"setfile.bin"
#define FIMC_IS_SETFILE_3H7	"setfile_S5K3H7.bin"

#define FIMC_IS_MSG_FILE	"/sdcard/fimc_is_msg_dump.txt"

#define err(fmt, args...) \
	printk(KERN_ERR "%s:%d: " fmt "\n", __func__, __LINE__, ##args)

#ifdef DEBUG
#define dbg(fmt, args...) \
	printk(KERN_INFO "%s:%d: " fmt "\n", __func__, __LINE__, ##args)
#else
#define dbg(fmt, args...)
#endif

#define is_af_use(dev)		((dev->af.use_af) ? 1 : 0)

/*
Default setting values
*/
#define DEFAULT_PREVIEW_STILL_WIDTH	640
#define DEFAULT_PREVIEW_STILL_HEIGHT	480
#define DEFAULT_CAPTURE_STILL_WIDTH	640
#define DEFAULT_CAPTURE_STILL_HEIGHT	480
#define DEFAULT_PREVIEW_VIDEO_WIDTH	640
#define DEFAULT_PREVIEW_VIDEO_HEIGHT	480
#define DEFAULT_CAPTURE_VIDEO_WIDTH	640
#define DEFAULT_CAPTURE_VIDEO_HEIGHT	480

#define DEFAULT_PREVIEW_STILL_FRAMERATE	30
#define DEFAULT_CAPTURE_STILL_FRAMERATE	15
#define DEFAULT_PREVIEW_VIDEO_FRAMERATE	30
#define DEFAULT_CAPTURE_VIDEO_FRAMERATE	30

#if defined(M0)
extern struct class *camera_class;
#endif

enum fimc_is_state_flag {
	IS_ST_IDLE,
	IS_ST_FW_LOADED,
	IS_ST_A5_PWR_ON,
	IS_ST_OPEN_SENSOR,
	IS_ST_SETFILE_LOADED,
	IS_ST_INIT_DONE,
	IS_ST_STREAM_ON,
	IS_ST_STREAM_OFF,
	IS_ST_CHANGE_MODE,
	IS_ST_BLOCK_CMD_CLEARED,
	IS_ST_SET_ZOOM,
	IS_ST_END
};

enum fimc_is_power {
	IS_PWR_ST_POWEROFF,
	IS_PWR_ST_POWERON,
	IS_PWR_SUB_IP_POWER_OFF,
	IS_PWR_END
};

enum fimc_is_clk {
	FIMC_IS_CLK_BUS,
	FIMC_IS_CLK_GATE,
};

enum sensor_list {
	SENSOR_S5K3H2_CSI_A	= 1,
	SENSOR_S5K6A3_CSI_A	= 2,
	SENSOR_S5K4E5_CSI_A	= 3,
	SENSOR_S5K3H7_CSI_A	= 4,
	SENSOR_S5K3H2_CSI_B	= 101,
	SENSOR_S5K6A3_CSI_B	= 102,
	SENSOR_S5K4E5_CSI_B	= 103,
	SENSOR_S5K3H7_CSI_B	= 104,
	/* Custom mode */
	SENSOR_S5K6A3_CSI_B_CUSTOM	= 200,
};

enum sensor_name {
	SENSOR_NAME_S5K3H2	= 1,
	SENSOR_NAME_S5K6A3	= 2,
	SENSOR_NAME_S5K4E5	= 3,
	SENSOR_NAME_S5K3H7	= 4,
	SENSOR_NAME_CUSTOM	= 5,
	SENSOR_NAME_END
};

enum sensor_channel {
	SENSOR_CONTROL_I2C0	= 0,
	SENSOR_CONTROL_I2C1	= 1
};

enum af_state {
	FIMC_IS_AF_IDLE		= 0,
	FIMC_IS_AF_SETCONFIG	= 1,
	FIMC_IS_AF_RUNNING	= 2,
	FIMC_IS_AF_LOCK		= 3,
	FIMC_IS_AF_ABORT	= 4,
	FIMC_IS_AF_FAILED	= 5,
};

enum af_lock_state {
	FIMC_IS_AF_UNLOCKED	= 0,
	FIMC_IS_AF_LOCKED	= 0x02
};

enum ae_lock_state {
	FIMC_IS_AE_UNLOCKED	= 0,
	FIMC_IS_AE_LOCKED	= 1
};

enum awb_lock_state {
	FIMC_IS_AWB_UNLOCKED	= 0,
	FIMC_IS_AWB_LOCKED	= 1
};

enum fimc_is_video_dev_num {
	FIMC_IS_VIDEO_NUM_BAYER = 0,
	FIMC_IS_VIDEO_MAX_NUM,
};

enum fimc_is_video_vb2_flag {
	FIMC_IS_STATE_IDLE		= 0,
	FIMC_IS_STATE_READY,
	FIMC_IS_STATE_ISP_STREAM_ON,
	FIMC_IS_STATE_ISP_STREAM_OFF,
	FIMC_IS_STATE_ISP_BUFFER_PREPARED,
};

struct is_meminfo {
	dma_addr_t	base;		/* buffer base */
	size_t		size;		/* total length */
	dma_addr_t	vaddr_base;		/* buffer base */
	dma_addr_t	vaddr_curr;		/* current addr */

	void		*bitproc_buf;
	size_t		dvaddr;
	unsigned char	*kvaddr;
	struct vb2_buffer	vb2_buf;
	dma_addr_t	fw_ref_base;
	dma_addr_t	setfile_ref_base;
};

struct is_fw {
	const struct firmware	*info;
	int			state;
	int			ver;
	char			fw_info[FIMC_IS_FW_INFO_LENGTH];
	char			setfile_info[FIMC_IS_SETFILE_INFO_LENGTH];
	char			fw_version[FIMC_IS_FW_VERSION_LENGTH];
	size_t			size;
};

struct is_setfile {
	const struct firmware	*info;
	int			state;
	u32			sub_index;
	u32			base;
	size_t			size;
};

struct is_to_host_cmd {
	u32	cmd;
	u32	sensor_id;
	u16	num_valid_args;
	u32	arg[MAX_I2H_ARG];
};

struct host_to_is_cmd {
	u16	cmd_type;
	u32	entry_id;
};

struct is_sensor {
	int id;
	enum sensor_list sensor_type;
	u32 width_prev;
	u32 height_prev;
	u32 width_prev_cam;
	u32 height_prev_cam;
	u32 width_cap;
	u32 height_cap;
	u32 width_cam;
	u32 height_cam;
	u32 offset_x;
	u32 offset_y;
	u32 zoom_out_width;
	u32 zoom_out_height;
	u32 frametime_max_prev;
	u32 frametime_max_prev_cam;
	u32 frametime_max_cap;
	u32 frametime_max_cam;
	int framerate_update;
};

struct is_fd_result_header {
	u32 offset;
	u32 count;
	u32 index;
	u32 target_addr;
	s32 width;
	s32 height;
};

struct is_af_info {
	u16 mode;
	u32 af_state;
	u32 af_lock_state;
	u32 ae_lock_state;
	u32 awb_lock_state;
	u16 pos_x;
	u16 pos_y;
	u16 prev_pos_x;
	u16 prev_pos_y;
	u16 use_af;
};

struct fimc_is_dev;

struct fimc_is_vb2 {
	const struct vb2_mem_ops *ops;
	void *(*init)(struct fimc_is_dev *is_dev);
	void (*cleanup)(void *alloc_ctx);

	unsigned long (*plane_addr)(struct vb2_buffer *vb, u32 plane_no);

	void (*resume)(void *alloc_ctx);
	void (*suspend)(void *alloc_ctx);

	int (*cache_flush)(struct vb2_buffer *vb, u32 num_planes);
	void (*set_cacheable)(void *alloc_ctx, bool cacheable);
};

#if defined(CONFIG_VIDEO_EXYNOS_FIMC_IS_BAYER)
struct fimc_is_fmt {
	enum v4l2_mbus_pixelcode mbus_code;
	char	*name;
	u32	fourcc;
	u16	flags;
};

struct fimc_is_video_dev {
	struct video_device		vd;
	struct vb2_queue		vbq;
	struct fimc_is_dev		*dev;
	struct v4l2_device		v4l2_dev;
	unsigned int			num_buf;
	unsigned int			num_plane;
	unsigned int			buf_ref_cnt;
	unsigned long			plane_size[FIMC_IS_MAX_BUf_PLANE_NUM];
	dma_addr_t buf[FIMC_IS_MAX_BUF_NUM][FIMC_IS_MAX_BUf_PLANE_NUM];
};
#endif

struct fimc_is_dev {
	spinlock_t			slock;
	struct mutex			lock;
	unsigned long			state;
	struct platform_device		*pdev;
	struct exynos4_platform_fimc_is	*pdata;
	u32				scenario_id;
	u32				frame_count;

	struct is_sensor		sensor;
	u32				sensor_num;
	struct is_af_info		af;

	u16				num_clocks;
	struct clk			*clock[NUM_FIMC_IS_CLOCKS];
	void __iomem			*regs;
	struct resource			*regs_res;

	int				irq1;
	wait_queue_head_t		irq_queue1;
	struct is_to_host_cmd		i2h_cmd;
	struct host_to_is_cmd		h2i_cmd;

	unsigned long			power;

	struct is_fw			fw;
	struct is_setfile		setfile;
	struct vb2_alloc_ctx		*alloc_ctx;
	struct is_meminfo		mem;		/* for reserved mem */
	struct is_fd_result_header	fd_header;

	struct v4l2_subdev		sd;
#if defined(CONFIG_VIDEO_EXYNOS_FIMC_IS_BAYER)
	struct fimc_is_video_dev	video[FIMC_IS_VIDEO_MAX_NUM];
	const struct fimc_is_vb2	*vb2;
	unsigned long			vb_state;
#endif
	struct device			*bus_dev;
	int				low_power_mode;
	/* Shared parameter region */
	atomic_t			p_region_num;
	unsigned long			p_region_index1;
	unsigned long			p_region_index2;
	struct is_region		*is_p_region;
	struct is_share_region		*is_shared_region;
};

static inline void fimc_is_state_lock_set(u32 state, struct fimc_is_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->slock, flags);
	dev->state |= state;
	spin_unlock_irqrestore(&dev->slock, flags);
}

static inline bool fimc_is_state_is_set(u32 mask, struct fimc_is_dev *dev)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&dev->slock, flags);
	ret = (dev->state & mask) == mask;
	spin_unlock_irqrestore(&dev->slock, flags);
	return ret;
}

extern const struct v4l2_subdev_ops fimc_is_subdev_ops;
extern struct is_region is_p_region;

extern int fimc_is_fw_clear_irq2(struct fimc_is_dev *dev);
extern int fimc_is_fw_clear_irq1(struct fimc_is_dev *dev,
						unsigned int intr_pos);
extern void fimc_is_hw_set_sensor_num(struct fimc_is_dev *dev);
extern int fimc_is_hw_get_sensor_num(struct fimc_is_dev *dev);
extern void fimc_is_hw_get_setfile_addr(struct fimc_is_dev *dev);
extern void fimc_is_hw_load_setfile(struct fimc_is_dev *dev);
extern int fimc_is_hw_set_param(struct fimc_is_dev *dev);
extern int fimc_is_hw_get_param(struct fimc_is_dev *dev, u16 offset);
extern void fimc_is_hw_set_intgr0_gd0(struct fimc_is_dev *dev);
extern int fimc_is_hw_wait_intsr0_intsd0(struct fimc_is_dev *dev);
extern int fimc_is_hw_wait_intmsr0_intmsd0(struct fimc_is_dev *dev);
extern void fimc_is_hw_a5_power(struct fimc_is_dev *dev, int on);
extern int fimc_is_hw_io_init(struct fimc_is_dev *dev);
extern void fimc_is_hw_open_sensor(struct fimc_is_dev *dev,
					u32 id, u32 sensor_index);
extern void fimc_is_hw_close_sensor(struct fimc_is_dev *dev, u32 id);
extern void fimc_is_hw_set_stream(struct fimc_is_dev *dev, int on);
extern void fimc_is_hw_set_init(struct fimc_is_dev *dev);
extern void fimc_is_hw_change_mode(struct fimc_is_dev *dev, int val);
extern void fimc_is_hw_set_lite(struct fimc_is_dev *dev, u32 width, u32 height);
extern void fimc_is_hw_subip_poweroff(struct fimc_is_dev *dev);
extern int fimc_is_hw_get_sensor_max_framerate(struct fimc_is_dev *dev);
extern void fimc_is_hw_set_debug_level(struct fimc_is_dev *dev, int level1,
								int level2);
extern int fimc_is_hw_set_tune(struct fimc_is_dev *dev);
extern int fimc_is_hw_get_sensor_size_width(struct fimc_is_dev *dev);
extern int fimc_is_hw_get_sensor_size_height(struct fimc_is_dev *dev);
extern int fimc_is_hw_get_sensor_format(struct fimc_is_dev *dev);
extern void fimc_is_hw_set_low_poweroff(struct fimc_is_dev *dev, int on);

extern int fimc_is_af_face(struct fimc_is_dev *dev);

extern int fimc_is_s_power(struct v4l2_subdev *sd, int on);

extern void fimc_is_param_err_checker(struct fimc_is_dev *dev);
extern void fimc_is_print_err_number(u32 num_err);

extern int fimc_is_init_mem_mgr(struct fimc_is_dev *dev);
extern void *fimc_is_mem_init(struct device *dev);
extern int fimc_is_alloc_firmware(struct fimc_is_dev *dev);
extern void fimc_is_mem_resume(void *alloc_ctxes);
extern void fimc_is_mem_suspend(void *alloc_ctxes);
extern void fimc_is_mem_init_mem_cleanup(void *alloc_ctxes);
extern void fimc_is_mem_cache_clean(const void *start_addr, unsigned long size);
extern void fimc_is_mem_cache_inv(const void *start_addr, unsigned long size);

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
extern const struct fimc_is_vb2 fimc_is_vb2_cma;
#elif defined(CONFIG_VIDEOBUF2_ION)
extern const struct fimc_is_vb2 fimc_is_vb2_ion;
#endif
extern const struct vb2_ops fimc_is_isp_qops;
extern const struct v4l2_file_operations fimc_is_isp_video_fops;
extern const struct v4l2_ioctl_ops fimc_is_isp_video_ioctl_ops;
struct fimc_is_dev *to_fimc_is_dev(struct v4l2_subdev *sdev);
#endif
