/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *
 * Authors: Younghwan Joo <yhwan.joo@samsung.com>
 *          Sylwester Nawrocki <s.nawrocki@samsung.com>
 */
#ifndef FIMC_IS_H_
#define FIMC_IS_H_

#include <asm/barrier.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/sizes.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-ctrls.h>

#include "fimc-isp.h"
#include "fimc-is-command.h"
#include "fimc-is-sensor.h"
#include "fimc-is-param.h"
#include "fimc-is-regs.h"

#define FIMC_IS_DRV_NAME		"exynos4-fimc-is"

#define FIMC_IS_FW_FILENAME		"exynos4_fimc_is_fw.bin"
#define FIMC_IS_SETFILE_6A3		"exynos4_s5k6a3_setfile.bin"

#define FIMC_IS_FW_LOAD_TIMEOUT		1000 /* ms */
#define FIMC_IS_POWER_ON_TIMEOUT	1000 /* us */

#define FIMC_IS_SENSORS_NUM		2

/* Memory definitions */
#define FIMC_IS_CPU_MEM_SIZE		(0xa00000)
#define FIMC_IS_CPU_BASE_MASK		((1 << 26) - 1)
#define FIMC_IS_REGION_SIZE		0x5000

#define FIMC_IS_DEBUG_REGION_OFFSET	0x0084b000
#define FIMC_IS_SHARED_REGION_OFFSET	0x008c0000
#define FIMC_IS_FW_INFO_LEN		31
#define FIMC_IS_FW_VER_LEN		7
#define FIMC_IS_FW_DESC_LEN		(FIMC_IS_FW_INFO_LEN + \
					 FIMC_IS_FW_VER_LEN)
#define FIMC_IS_SETFILE_INFO_LEN	39

#define FIMC_IS_EXTRA_MEM_SIZE		(FIMC_IS_EXTRA_FW_SIZE + \
					 FIMC_IS_EXTRA_SETFILE_SIZE + 0x1000)
#define FIMC_IS_EXTRA_FW_SIZE		0x180000
#define FIMC_IS_EXTRA_SETFILE_SIZE	0x4b000

/* TODO: revisit */
#define FIMC_IS_FW_ADDR_MASK		((1 << 26) - 1)
#define FIMC_IS_FW_SIZE_MAX		(SZ_4M)
#define FIMC_IS_FW_SIZE_MIN		(SZ_32K)

#define ATCLK_MCUISP_FREQUENCY		100000000UL
#define ACLK_AXI_FREQUENCY		100000000UL

enum {
	ISS_CLK_PPMUISPX,
	ISS_CLK_PPMUISPMX,
	ISS_CLK_LITE0,
	ISS_CLK_LITE1,
	ISS_CLK_MPLL,
	ISS_CLK_ISP,
	ISS_CLK_DRC,
	ISS_CLK_FD,
	ISS_CLK_MCUISP,
	ISS_CLK_GICISP,
	ISS_CLK_PWM_ISP,
	ISS_CLK_MCUCTL_ISP,
	ISS_CLK_UART,
	ISS_GATE_CLKS_MAX,
	ISS_CLK_ISP_DIV0 = ISS_GATE_CLKS_MAX,
	ISS_CLK_ISP_DIV1,
	ISS_CLK_MCUISP_DIV0,
	ISS_CLK_MCUISP_DIV1,
	ISS_CLK_ACLK200,
	ISS_CLK_ACLK200_DIV,
	ISS_CLK_ACLK400MCUISP,
	ISS_CLK_ACLK400MCUISP_DIV,
	ISS_CLKS_MAX
};

/* The driver's internal state flags */
enum {
	IS_ST_IDLE,
	IS_ST_PWR_ON,
	IS_ST_A5_PWR_ON,
	IS_ST_FW_LOADED,
	IS_ST_OPEN_SENSOR,
	IS_ST_SETFILE_LOADED,
	IS_ST_INIT_DONE,
	IS_ST_STREAM_ON,
	IS_ST_STREAM_OFF,
	IS_ST_CHANGE_MODE,
	IS_ST_BLOCK_CMD_CLEARED,
	IS_ST_SET_ZOOM,
	IS_ST_PWR_SUBIP_ON,
	IS_ST_END,
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
	FIMC_IS_AF_LOCKED	= 2
};

enum ae_lock_state {
	FIMC_IS_AE_UNLOCKED	= 0,
	FIMC_IS_AE_LOCKED	= 1
};

enum awb_lock_state {
	FIMC_IS_AWB_UNLOCKED	= 0,
	FIMC_IS_AWB_LOCKED	= 1
};

enum {
	IS_METERING_CONFIG_CMD,
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

struct fimc_is_firmware {
	const struct firmware *f_w;

	dma_addr_t addr;
	void *vaddr;
	unsigned int size;

	char info[FIMC_IS_FW_INFO_LEN + 1];
	char version[FIMC_IS_FW_VER_LEN + 1];
	char setfile_info[FIMC_IS_SETFILE_INFO_LEN + 1];
	u8 state;
};

struct fimc_is_memory {
	/* DMA base address */
	dma_addr_t addr;
	/* virtual base address */
	void *vaddr;
	/* total length */
	unsigned int size;
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

#define FIMC_IS_DEBUG_MSG	0x3f
#define FIMC_IS_DEBUG_LEVEL	3

struct fimc_is_setfile {
	const struct firmware *info;
	unsigned int state;
	unsigned int size;
	u32 sub_index;
	u32 base;
};

struct chain_config {
	struct global_param	global;
	struct sensor_param	sensor;
	struct isp_param	isp;
	struct drc_param	drc;
	struct fd_param		fd;

	unsigned long		p_region_index[2];
};

/**
 * struct fimc_is - fimc-is data structure
 * @pdev: pointer to FIMC-IS platform device
 * @pctrl: pointer to pinctrl structure for this device
 * @v4l2_dev: pointer to top the level v4l2_device
 * @lock: mutex serializing video device and the subdev operations
 * @slock: spinlock protecting this data structure and the hw registers
 * @clocks: FIMC-LITE gate clock
 * @regs: MCUCTL mmapped registers region
 * @pmu_regs: PMU ISP mmapped registers region
 * @irq_queue: interrupt handling waitqueue
 * @lpm: low power mode flag
 * @state: internal driver's state flags
 */
struct fimc_is {
	struct platform_device		*pdev;
	struct pinctrl			*pctrl;
	struct v4l2_device		*v4l2_dev;

	struct fimc_is_firmware		fw;
	struct fimc_is_memory		memory;
	struct firmware			*f_w;

	struct fimc_isp			isp;
	struct fimc_is_sensor		sensor[FIMC_IS_SENSORS_NUM];
	struct fimc_is_setfile		setfile;

	struct v4l2_ctrl_handler	ctrl_handler;

	struct mutex			lock;
	spinlock_t			slock;

	struct clk			*clocks[ISS_CLKS_MAX];
	void __iomem			*regs;
	void __iomem			*pmu_regs;
	int				irq;
	wait_queue_head_t		irq_queue;
	u8				lpm;

	unsigned long			state;
	unsigned int			sensor_index;

	struct i2h_cmd			i2h_cmd;
	struct h2i_cmd			h2i_cmd;
	struct is_fd_result_header	fd_header;

	struct chain_config		config[IS_SC_MAX];
	unsigned			config_index;

	struct is_region		*is_p_region;
	dma_addr_t			is_dma_p_region;
	struct is_share_region		*is_shared_region;
	struct is_af_info		af;

	struct dentry			*debugfs_entry;
};

static inline struct fimc_is *fimc_isp_to_is(struct fimc_isp *isp)
{
	return container_of(isp, struct fimc_is, isp);
}

static inline struct chain_config *__get_curr_is_config(struct fimc_is *is)
{
	return &is->config[is->config_index];
}

static inline void fimc_is_mem_barrier(void)
{
	mb();
}

static inline void fimc_is_set_param_bit(struct fimc_is *is, int num)
{
	struct chain_config *cfg = &is->config[is->config_index];

	set_bit(num, &cfg->p_region_index[0]);
}

static inline void fimc_is_set_param_ctrl_cmd(struct fimc_is *is, int cmd)
{
	is->is_p_region->parameter.isp.control.cmd = cmd;
}

static inline void mcuctl_write(u32 v, struct fimc_is *is, unsigned int offset)
{
	writel(v, is->regs + offset);
}

static inline u32 mcuctl_read(struct fimc_is *is, unsigned int offset)
{
	return readl(is->regs + offset);
}

static inline void pmuisp_write(u32 v, struct fimc_is *is, unsigned int offset)
{
	writel(v, is->pmu_regs + offset);
}

static inline u32 pmuisp_read(struct fimc_is *is, unsigned int offset)
{
	return readl(is->pmu_regs + offset);
}

int fimc_is_wait_event(struct fimc_is *is, unsigned long bit,
		       unsigned int state, unsigned int timeout);
int fimc_is_cpu_set_power(struct fimc_is *is, int on);
int fimc_is_start_firmware(struct fimc_is *is);
int fimc_is_hw_initialize(struct fimc_is *is);
void fimc_is_log_dump(const char *level, const void *buf, size_t len);

#endif /* FIMC_IS_H_ */
