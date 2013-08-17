/*
 * Samsung EXYNOS4412 FIMC-ISP driver
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 * Younghwan Joo <yhwan.joo@samsung.com>
 *
 * All rights reserved.
 */

#ifndef FIMC_IS_H_
#define FIMC_IS_H_

#include <asm/sizes.h>
#include <linux/bug.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/videodev2.h>
#include <linux/videodev2_exynos_camera.h>

#include <media/v4l2-ctrls.h>
#if defined(CONFIG_VIDEOBUF2_ION)
#include <media/videobuf2-ion.h>
#endif
#include "fimc-isp.h"
#include "fimc-is-cmd.h"
#include "fimc-is-sensor.h"
#include "fimc-is-param.h"
#include "fimc-is-regs.h"
#include "fimc-is-config.h"

#define FIMC_IS_DRV_NAME		"exynos4-fimc-is"
#define FIMC_IS_CLK_NAME		"fimc_is"

#define FIMC_IS_FW_FILENAME		"fimc_is_fw.bin"
#define FIMC_IS_SETFILE_6A3		"setfile_s5k6a3.bin"
#define FIMC_IS_SETFILE_3H7		"setfile_s5k3h7.bin"

#define FIMC_IS_MAX_CLOCKS		3
#define FIMC_IS_FW_LOAD_TIMEOUT		1000 /* ms */

#define FIMC_IS_SHUTDOWN_TIMEOUT	(3 * HZ)
#define FIMC_IS_SHUTDOWN_TIMEOUT_SENSOR	(HZ)
#define FIMC_IS_SHUTDOWN_TIMEOUT_AF	(3 * HZ)
#define FIMC_IS_SSTREAM_TIMEOUT		(6 * HZ)

#define FIMC_IS_SENSOR_NUM		2

/* Memory definitions */
#define FIMC_IS_CPU_MEM_SIZE		(0x00A00000)
#define FIMC_IS_CPU_BASE_MASK		((1 << 26) - 1)
#define FIMC_IS_REGION_SIZE		0x5000

#define FIMC_IS_DEBUG_REGION_OFFSET	0x0084B000
#define FIMC_IS_SHARED_REGION_OFFSET	0x008c0000
#define FIMC_IS_FW_INFO_LEN		31
#define FIMC_IS_FW_VER_LEN		7
#define FIMC_IS_FW_DESC_LEN		(FIMC_IS_FW_INFO_LEN + \
					 FIMC_IS_FW_VER_LEN)
#define FIMC_IS_SETFILE_INFO_LEN	39

#define FIMC_IS_EXTRA_MEM_SIZE		(FIMC_IS_EXTRA_FW_SIZE + \
					 FIMC_IS_EXTRA_SETFILE_SIZE + 0x1000)
#define FIMC_IS_EXTRA_FW_SIZE		0x180000
#define FIMC_IS_EXTRA_SETFILE_SIZE	0x4B000

/* #define VIDEOBUF2_DMA_CONTIG */

#define FIMC_IS_SD_PAD_SINK	0
#define FIMC_IS_SD_PAD_SOURCE	1
#define FIMC_IS_SD_PADS_NUM	2

enum {
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
	IS_ST_PWR_ON = 20,
	IS_ST_PWR_SUBIP_ON,
	IS_ST_END
};

#define ID_CSI_A	0
#define ID_CSI_B	1

#define ID_S5K3H2	1
#define ID_S5K6A3	2
#define ID_S5K4E5	3
#define ID_S5K3H7	4

#define SENSOR_ID(__sensor, __csi)	((__csi) * 0x64 + (__sensor))

/* MESS begin */
enum af_state {
	FIMC_IS_AF_IDLE		= 0,
	FIMC_IS_AF_SETCONFIG	= 1,
	FIMC_IS_AF_RUNNING	= 2,
	FIMC_IS_AF_LOCK		= 3,
	FIMC_IS_AF_ABORT	= 4,
	FIMC_IS_AF_FAILED	= 5,
};

enum af_lock_state {
	FIMC_IS_AF_UNLOCKED	= 0x01,
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

enum {
	IS_METERING_CONFIG_CMD = 0,
	IS_METERING_CONFIG_WIN_POS_X,
	IS_METERING_CONFIG_WIN_POS_Y,
	IS_METERING_CONFIG_WIN_WIDTH,
	IS_METERING_CONFIG_WIN_HEIGHT,
	IS_METERING_CONFIG_MAX
};

struct is_setfile {
	const struct firmware *info;
	int state;
	u32 sub_index;
	u32 base;
	size_t size;
};


struct is_fd_result_header {
	u32 offset;
	u32 count;
	u32 index;
	u32 curr_index;
	u32 width;
	u32 height;
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
/* MESS end */

struct fimc_firmware {
	dma_addr_t paddr;
	void *vaddr;
	unsigned int size;

	char info[FIMC_IS_FW_INFO_LEN + 1];
	char version[FIMC_IS_FW_VER_LEN + 1];
	char setfile_info[FIMC_IS_SETFILE_INFO_LEN + 1];
	u8 state;
};

struct fimc_is_memory {
	/* physical base address */
	dma_addr_t paddr;
	/* virtual base address */
	void *vaddr;
	/* total length */
	unsigned int size;
	void *fw_cookie;
	u32 dvaddr;
	u32 kvaddr;
	u32 dvaddr_fshared;
	u32 kvaddr_fshared;
	u32 dvaddr_region;
	u32 kvaddr_region;
};

#define FIMC_IS_I2H_MAX_ARGS	12

struct i2h_cmd {
	u32 cmd;
	u32 sensor_id;
	u16 num_args;
	u32 args[FIMC_IS_I2H_MAX_ARGS];
};

struct h2i_cmd {
	u16 cmd_type;
	u32 entry_id;
};


struct fimc_is_setfile {
	const struct firmware *info;
	unsigned int state;
	unsigned int size;
	u32 sub_index;
	u32 base;
};

struct is_config_param {
	struct global_param		global;
	struct sensor_param		sensor;
	struct isp_param		isp;
	struct drc_param		drc;
	struct fd_param			fd;

	atomic_t			p_region_num;
	unsigned long			p_region_index1;
	unsigned long			p_region_index2;
};

/*
 * struct fimc_is - fimc lite structure
 * @pdev: pointer to FIMC-LITE platform device
 * @variant: variant information for this IP
 * @v4l2_dev: pointer to top the level v4l2_device
 * @vfd: video device node
 * @fh: v4l2 file handle
 * @alloc_ctx: videobuf2 memory allocator context
 * @ctrl_handler: v4l2 control handler
 * @slock: spinlock protecting this data structure and the hw registers
 * @lock: mutex serializing video device and the subdev operations
 * @clocks: FIMC-LITE gate clock
 * @regs: memory mapped io registers
 * @irq_queue: interrupt handler waitqueue
 * @lpm: low power mode flag
 * @state:
 */
struct fimc_is {
	struct platform_device	*pdev;
	struct fimc_is_platform_data *pdata;
	struct v4l2_device	*v4l2_dev;

	struct fimc_firmware	fw;
	struct fimc_is_memory	memory;

	struct fimc_isp		isp;
	struct fimc_is_sensor	sensor[FIMC_IS_SENSOR_NUM];
	struct fimc_is_setfile	setfile;
	struct is_af_info		af;

	struct vb2_alloc_ctx	*alloc_ctx;
	struct v4l2_ctrl_handler ctrl_handler;

	struct mutex		lock;
	spinlock_t		slock;

	struct clk		*clocks[FIMC_IS_MAX_CLOCKS];
	void __iomem		*regs;
	wait_queue_head_t	irq_queue;
	u8			lpm;

	unsigned long		state;

	struct i2h_cmd		i2h_cmd;
	struct h2i_cmd		h2i_cmd;

	struct is_region	*is_p_region;
	struct is_share_region	*is_shared_region;
	struct is_fd_result_header	fd_header;

	struct is_config_param		cfg_param[ISS_END];

	u32			scenario_id;
	int			sensor_index;
};

void fimc_is_cpu_set_power(struct fimc_is *is, int on);
int fimc_is_clk_enable(struct fimc_is *is);
void fimc_is_clk_disable(struct fimc_is *is);

int fimc_is_request_firmware(struct fimc_is *is, const char *fw_name);
int fimc_is_hw_open_sensor(struct fimc_is *is, u32 id, u32 sensor_index);
int fimc_is_load_setfile(struct fimc_is *is);
int fimc_is_hw_io_init(struct fimc_is *is);
#ifdef VIDEOBUF2_DMA_CONTIG
void fimc_is_mem_cache_clean(const void *start_addr, unsigned long size);
#else
void fimc_is_cache_flush(struct fimc_is *is, u32 offset, u32 size);
void fimc_is_region_invalid(struct fimc_is *is);
void fimc_is_region_flush(struct fimc_is *is);
void fimc_is_shared_region_invalid(struct fimc_is *is);
void fimc_is_shared_region_flush(struct fimc_is *is);
#endif
int fimc_is_resume(struct device *dev);
int fimc_is_suspend(struct device *dev);

#endif /* FIMC_IS_H_ */
