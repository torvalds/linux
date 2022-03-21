/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#ifndef STF_VIN_H
#define STF_VIN_H

#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <linux/spinlock_types.h>
#include <video/stf-vin.h>
#include <linux/platform_device.h>

#include "stf_video.h"

#define STF_VIN_PAD_SINK   0
#define STF_VIN_PAD_SRC    1
#define STF_VIN_PADS_NUM   2

struct vin2_format {
	u32 code;
	u8 bpp;
};

enum vin_output_state {
	VIN_OUTPUT_OFF,
	VIN_OUTPUT_RESERVED,
	VIN_OUTPUT_SINGLE,
	VIN_OUTPUT_CONTINUOUS,
	VIN_OUTPUT_IDLE,
	VIN_OUTPUT_STOPPING
};

struct vin_output {
	int active_buf;
	struct stfcamss_buffer *buf[2];
	struct stfcamss_buffer *last_buffer;
	struct list_head pending_bufs;
	struct list_head ready_bufs;
	enum vin_output_state state;
	unsigned int sequence;
	unsigned int frame_skip;
};

enum vin_line_id {
	VIN_LINE_NONE = -1,
	VIN_LINE_WR = 0,
	VIN_LINE_ISP0 = 1,
	VIN_LINE_ISP1 = 2,
	VIN_LINE_ISP0_RAW = 3,
	VIN_LINE_ISP1_RAW = 4,
	VIN_LINE_MAX = 5
};

enum subdev_type;

struct vin_line {
	enum subdev_type sdev_type;  // must be frist
	enum vin_line_id id;
	struct v4l2_subdev subdev;
	struct media_pad pads[STF_VIN_PADS_NUM];
	struct v4l2_mbus_framefmt fmt[STF_VIN_PADS_NUM];
	struct stfcamss_video video_out;
	struct mutex stream_lock;
	int stream_count;
	struct mutex power_lock;
	int power_count;
	struct vin_output output;
	spinlock_t output_lock;
	const struct vin2_format *formats;
	unsigned int nformats;
};

struct stf_vin2_dev;

struct vin_hw_ops {
	int (*vin_top_clk_init)(struct stf_vin2_dev *vin_dev);
	int (*vin_top_clk_deinit)(struct stf_vin2_dev *vin_dev);
	int (*vin_clk_enable)(struct stf_vin2_dev *vin_dev);
	int (*vin_clk_disable)(struct stf_vin2_dev *vin_dev);
	int (*vin_config_set)(struct stf_vin2_dev *vin_dev);
	int (*vin_wr_stream_set)(struct stf_vin2_dev *vin_dev, int on);
	void (*vin_wr_irq_enable)(struct stf_vin2_dev *vin_dev, int enable);
	void (*vin_power_on)(struct stf_vin2_dev *vin_dev, int on);
	void (*wr_rd_set_addr)(struct stf_vin2_dev *vin_dev,
			dma_addr_t wr_addr, dma_addr_t rd_addr);
	void (*vin_wr_set_ping_addr)(struct stf_vin2_dev *vin_dev,
			dma_addr_t addr);
	void (*vin_wr_set_pong_addr)(struct stf_vin2_dev *vin_dev,
			dma_addr_t addr);
	void (*vin_wr_get_ping_pong_status)(struct stf_vin2_dev *vin_dev);
	void (*vin_isp_set_yuv_addr)(struct stf_vin2_dev *vin_dev,
			int isp_id,
			dma_addr_t y_addr, dma_addr_t uv_addr);
	void (*vin_isp_set_raw_addr)(struct stf_vin2_dev *vin_dev,
			int isp_id, dma_addr_t raw_addr);
	irqreturn_t (*vin_wr_irq_handler)(int irq, void *priv);
	irqreturn_t (*vin_isp_irq_handler)(int irq, void *priv);
	irqreturn_t (*vin_isp_csi_irq_handler)(int irq, void *priv);
	irqreturn_t (*vin_isp_scd_irq_handler)(int irq, void *priv);
	irqreturn_t (*vin_isp_irq_csiline_handler)(int irq, void *priv);
	void (*isr_buffer_done)(struct vin_line *line,
			struct vin_params *params);
	void (*isr_change_buffer)(struct vin_line *line);
};

struct stf_vin2_dev {
	struct stfcamss *stfcamss;
	u8 id;
	struct vin_line line[VIN_LINE_MAX];
	struct vin_hw_ops *hw_ops;
	atomic_t ref_count;
	struct mutex power_lock;
	int power_count;
};

extern int stf_vin_subdev_init(struct stfcamss *stfcamss);
extern int stf_vin_register(struct stf_vin2_dev *vin_dev,
		struct v4l2_device *v4l2_dev);
extern int stf_vin_unregister(struct stf_vin2_dev *vin_dev);

extern struct vin_hw_ops vin_ops;
extern void dump_vin_reg(void *__iomem regbase);

#endif /* STF_VIN_H */
