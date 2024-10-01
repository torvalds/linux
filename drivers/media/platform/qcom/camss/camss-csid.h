/* SPDX-License-Identifier: GPL-2.0 */
/*
 * camss-csid.h
 *
 * Qualcomm MSM Camera Subsystem - CSID (CSI Decoder) Module
 *
 * Copyright (c) 2011-2014, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015-2018 Linaro Ltd.
 */
#ifndef QC_MSM_CAMSS_CSID_H
#define QC_MSM_CAMSS_CSID_H

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

#define MSM_CSID_PAD_SINK 0
#define MSM_CSID_PAD_FIRST_SRC 1
#define MSM_CSID_PADS_NUM 5

#define MSM_CSID_PAD_SRC (MSM_CSID_PAD_FIRST_SRC)

/* CSID hardware can demultiplex up to 4 outputs */
#define MSM_CSID_MAX_SRC_STREAMS	4

#define DATA_TYPE_EMBEDDED_DATA_8BIT	0x12
#define DATA_TYPE_YUV420_8BIT		0x18
#define DATA_TYPE_YUV420_10BIT		0x19
#define DATA_TYPE_YUV420_8BIT_LEGACY	0x1a
#define DATA_TYPE_YUV420_8BIT_SHIFTED	0x1c /* Chroma Shifted Pixel Sampling */
#define DATA_TYPE_YUV420_10BIT_SHIFTED	0x1d /* Chroma Shifted Pixel Sampling */
#define DATA_TYPE_YUV422_8BIT		0x1e
#define DATA_TYPE_YUV422_10BIT		0x1f
#define DATA_TYPE_RGB444		0x20
#define DATA_TYPE_RGB555		0x21
#define DATA_TYPE_RGB565		0x22
#define DATA_TYPE_RGB666		0x23
#define DATA_TYPE_RGB888		0x24
#define DATA_TYPE_RAW_24BIT		0x27
#define DATA_TYPE_RAW_6BIT		0x28
#define DATA_TYPE_RAW_7BIT		0x29
#define DATA_TYPE_RAW_8BIT		0x2a
#define DATA_TYPE_RAW_10BIT		0x2b
#define DATA_TYPE_RAW_12BIT		0x2c
#define DATA_TYPE_RAW_14BIT		0x2d
#define DATA_TYPE_RAW_16BIT		0x2e
#define DATA_TYPE_RAW_20BIT		0x2f

#define CSID_RESET_TIMEOUT_MS 500

enum csid_testgen_mode {
	CSID_PAYLOAD_MODE_DISABLED = 0,
	CSID_PAYLOAD_MODE_INCREMENTING = 1,
	CSID_PAYLOAD_MODE_ALTERNATING_55_AA = 2,
	CSID_PAYLOAD_MODE_ALL_ZEROES = 3,
	CSID_PAYLOAD_MODE_ALL_ONES = 4,
	CSID_PAYLOAD_MODE_RANDOM = 5,
	CSID_PAYLOAD_MODE_USER_SPECIFIED = 6,
	CSID_PAYLOAD_MODE_NUM_SUPPORTED_GEN1 = 6, /* excluding disabled */
	CSID_PAYLOAD_MODE_COMPLEX_PATTERN = 7,
	CSID_PAYLOAD_MODE_COLOR_BOX = 8,
	CSID_PAYLOAD_MODE_COLOR_BARS = 9,
	CSID_PAYLOAD_MODE_NUM_SUPPORTED_GEN2 = 9, /* excluding disabled */
};

struct csid_format {
	u32 code;
	u8 data_type;
	u8 decode_format;
	u8 bpp;
	u8 spp; /* bus samples per pixel */
};

struct csid_testgen_config {
	enum csid_testgen_mode mode;
	const char * const*modes;
	u8 nmodes;
	u8 enabled;
};

struct csid_phy_config {
	u8 csiphy_id;
	u8 lane_cnt;
	u32 lane_assign;
	u32 en_vc;
	u8 need_vc_update;
};

struct csid_device;

struct csid_hw_ops {
	/*
	 * configure_stream - Configures and starts CSID input stream
	 * @csid: CSID device
	 */
	void (*configure_stream)(struct csid_device *csid, u8 enable);

	/*
	 * configure_testgen_pattern - Validates and configures output pattern mode
	 * of test pattern generator
	 * @csid: CSID device
	 */
	int (*configure_testgen_pattern)(struct csid_device *csid, s32 val);

	/*
	 * hw_version - Read hardware version register from hardware
	 * @csid: CSID device
	 */
	u32 (*hw_version)(struct csid_device *csid);

	/*
	 * isr - CSID module interrupt service routine
	 * @irq: Interrupt line
	 * @dev: CSID device
	 *
	 * Return IRQ_HANDLED on success
	 */
	irqreturn_t (*isr)(int irq, void *dev);

	/*
	 * reset - Trigger reset on CSID module and wait to complete
	 * @csid: CSID device
	 *
	 * Return 0 on success or a negative error code otherwise
	 */
	int (*reset)(struct csid_device *csid);

	/*
	 * src_pad_code - Pick an output/src format based on the input/sink format
	 * @csid: CSID device
	 * @sink_code: The sink format of the input
	 * @match_format_idx: Request preferred index, as defined by subdevice csid_format.
	 *	Set @match_code to 0 if used.
	 * @match_code: Request preferred code, set @match_format_idx to 0 if used
	 *
	 * Return 0 on failure or src format code otherwise
	 */
	u32 (*src_pad_code)(struct csid_device *csid, u32 sink_code,
			    unsigned int match_format_idx, u32 match_code);

	/*
	 * subdev_init - Initialize CSID device according for hardware revision
	 * @csid: CSID device
	 */
	void (*subdev_init)(struct csid_device *csid);
};

struct csid_device {
	struct camss *camss;
	u8 id;
	struct v4l2_subdev subdev;
	struct media_pad pads[MSM_CSID_PADS_NUM];
	void __iomem *base;
	u32 irq;
	char irq_name[30];
	struct camss_clock *clock;
	int nclocks;
	struct regulator_bulk_data *supplies;
	int num_supplies;
	struct completion reset_complete;
	struct csid_testgen_config testgen;
	struct csid_phy_config phy;
	struct v4l2_mbus_framefmt fmt[MSM_CSID_PADS_NUM];
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *testgen_mode;
	const struct csid_format *formats;
	unsigned int nformats;
	const struct csid_hw_ops *ops;
};

struct resources;

/*
 * csid_find_code - Find a format code in an array using array index or format code
 * @codes: Array of format codes
 * @ncodes: Length of @code array
 * @req_format_idx: Request preferred index, as defined by subdevice csid_format.
 *	Set @match_code to 0 if used.
 * @match_code: Request preferred code, set @req_format_idx to 0 if used
 *
 * Return 0 on failure or format code otherwise
 */
u32 csid_find_code(u32 *codes, unsigned int ncode,
		   unsigned int match_format_idx, u32 match_code);

/*
 * csid_get_fmt_entry - Find csid_format entry with matching format code
 * @formats: Array of format csid_format entries
 * @nformats: Length of @nformats array
 * @code: Desired format code
 *
 * Return formats[0] on failure to find code
 */
const struct csid_format *csid_get_fmt_entry(const struct csid_format *formats,
					     unsigned int nformats,
					     u32 code);

int msm_csid_subdev_init(struct camss *camss, struct csid_device *csid,
			 const struct resources *res, u8 id);

int msm_csid_register_entity(struct csid_device *csid,
			     struct v4l2_device *v4l2_dev);

void msm_csid_unregister_entity(struct csid_device *csid);

void msm_csid_get_csid_id(struct media_entity *entity, u8 *id);

extern const char * const csid_testgen_modes[];

extern const struct csid_hw_ops csid_ops_4_1;
extern const struct csid_hw_ops csid_ops_4_7;
extern const struct csid_hw_ops csid_ops_gen2;


#endif /* QC_MSM_CAMSS_CSID_H */
