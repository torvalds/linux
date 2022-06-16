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

#define STF_VIN_NAME "stf_vin"

#define STF_VIN_PAD_SINK   0
#define STF_VIN_PAD_SRC    1
#define STF_VIN_PADS_NUM   2

struct vin2_format {
	u32 code;
	u8 bpp;
};

struct vin2_format_table {
	const struct vin2_format *fmts;
	int nfmts;
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

/* The vin output lines include all isp controller lines,
 * and one vin_wr output line.
 */
enum vin_line_id {
	VIN_LINE_NONE = -1,
	VIN_LINE_WR = 0,
	VIN_LINE_ISP = 1,
	VIN_LINE_ISP_SS0 = 2,
	VIN_LINE_ISP_SS1 = 3,
	VIN_LINE_ISP_ITIW = 4,
	VIN_LINE_ISP_ITIR = 5,
	VIN_LINE_ISP_RAW = 6,
	VIN_LINE_ISP_SCD_Y = 7,
	VIN_LINE_MAX = 8,
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
			dma_addr_t y_addr, dma_addr_t uv_addr);
	void (*vin_isp_set_raw_addr)(struct stf_vin2_dev *vin_dev,
			dma_addr_t raw_addr);
	void (*vin_isp_set_ss0_addr)(struct stf_vin2_dev *vin_dev,
			dma_addr_t y_addr, dma_addr_t uv_addr);
	void (*vin_isp_set_ss1_addr)(struct stf_vin2_dev *vin_dev,
			dma_addr_t y_addr, dma_addr_t uv_addr);
	void (*vin_isp_set_itiw_addr)(struct stf_vin2_dev *vin_dev,
			dma_addr_t y_addr, dma_addr_t uv_addr);
	void (*vin_isp_set_itir_addr)(struct stf_vin2_dev *vin_dev,
			dma_addr_t y_addr, dma_addr_t uv_addr);
	void (*vin_isp_set_scd_addr)(struct stf_vin2_dev *vin_dev,
			dma_addr_t yhist_addr,
			dma_addr_t scd_addr, int scd_type);
	int (*vin_isp_get_scd_type)(struct stf_vin2_dev *vin_dev);
	irqreturn_t (*vin_wr_irq_handler)(int irq, void *priv);
	irqreturn_t (*vin_isp_irq_handler)(int irq, void *priv);
	irqreturn_t (*vin_isp_csi_irq_handler)(int irq, void *priv);
	irqreturn_t (*vin_isp_scd_irq_handler)(int irq, void *priv);
	irqreturn_t (*vin_isp_irq_csiline_handler)(int irq, void *priv);
	void (*isr_buffer_done)(struct vin_line *line,
			struct vin_params *params);
	void (*isr_change_buffer)(struct vin_line *line);
};

#define ISP_DUMMY_BUFFER_NUMS  STF_ISP_PAD_MAX
#define VIN_DUMMY_BUFFER_NUMS  1

enum {
	STF_DUMMY_VIN,
	STF_DUMMY_ISP,
	STF_DUMMY_MODULE_NUMS,
};

struct vin_dummy_buffer {
	dma_addr_t paddr[3];
	void *vaddr;
	u32 buffer_size;
	u32 width;
	u32 height;
	u32 mcode;
};

struct dummy_buffer {
	struct vin_dummy_buffer *buffer;
	u32 nums;
	struct mutex stream_lock;
	int stream_count;
	atomic_t frame_skip;
};

struct stf_vin2_dev {
	struct stfcamss *stfcamss;
	u8 id;
	struct vin_line line[VIN_LINE_MAX];
	struct dummy_buffer dummy_buffer[STF_DUMMY_MODULE_NUMS];
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
extern enum isp_pad_id stf_vin_map_isp_pad(enum vin_line_id line,
		enum isp_pad_id def);

#endif /* STF_VIN_H */
