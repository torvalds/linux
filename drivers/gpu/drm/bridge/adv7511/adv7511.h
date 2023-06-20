/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Analog Devices ADV7511 HDMI transmitter driver
 *
 * Copyright 2012 Analog Devices Inc.
 */

#ifndef __DRM_I2C_ADV7511_H__
#define __DRM_I2C_ADV7511_H__

#include <linux/hdmi.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>

#define ADV7511_REG_CHIP_REVISION		0x00
#define ADV7511_REG_N0				0x01
#define ADV7511_REG_N1				0x02
#define ADV7511_REG_N2				0x03
#define ADV7511_REG_SPDIF_FREQ			0x04
#define ADV7511_REG_CTS_AUTOMATIC1		0x05
#define ADV7511_REG_CTS_AUTOMATIC2		0x06
#define ADV7511_REG_CTS_MANUAL0			0x07
#define ADV7511_REG_CTS_MANUAL1			0x08
#define ADV7511_REG_CTS_MANUAL2			0x09
#define ADV7511_REG_AUDIO_SOURCE		0x0a
#define ADV7511_REG_AUDIO_CONFIG		0x0b
#define ADV7511_REG_I2S_CONFIG			0x0c
#define ADV7511_REG_I2S_WIDTH			0x0d
#define ADV7511_REG_AUDIO_SUB_SRC0		0x0e
#define ADV7511_REG_AUDIO_SUB_SRC1		0x0f
#define ADV7511_REG_AUDIO_SUB_SRC2		0x10
#define ADV7511_REG_AUDIO_SUB_SRC3		0x11
#define ADV7511_REG_AUDIO_CFG1			0x12
#define ADV7511_REG_AUDIO_CFG2			0x13
#define ADV7511_REG_AUDIO_CFG3			0x14
#define ADV7511_REG_I2C_FREQ_ID_CFG		0x15
#define ADV7511_REG_VIDEO_INPUT_CFG1		0x16
#define ADV7511_REG_CSC_UPPER(x)		(0x18 + (x) * 2)
#define ADV7511_REG_CSC_LOWER(x)		(0x19 + (x) * 2)
#define ADV7511_REG_SYNC_DECODER(x)		(0x30 + (x))
#define ADV7511_REG_DE_GENERATOR		(0x35 + (x))
#define ADV7511_REG_PIXEL_REPETITION		0x3b
#define ADV7511_REG_VIC_MANUAL			0x3c
#define ADV7511_REG_VIC_SEND			0x3d
#define ADV7511_REG_VIC_DETECTED		0x3e
#define ADV7511_REG_AUX_VIC_DETECTED		0x3f
#define ADV7511_REG_PACKET_ENABLE0		0x40
#define ADV7511_REG_POWER			0x41
#define ADV7511_REG_STATUS			0x42
#define ADV7511_REG_EDID_I2C_ADDR		0x43
#define ADV7511_REG_PACKET_ENABLE1		0x44
#define ADV7511_REG_PACKET_I2C_ADDR		0x45
#define ADV7511_REG_DSD_ENABLE			0x46
#define ADV7511_REG_VIDEO_INPUT_CFG2		0x48
#define ADV7511_REG_INFOFRAME_UPDATE		0x4a
#define ADV7511_REG_GC(x)			(0x4b + (x)) /* 0x4b - 0x51 */
#define ADV7511_REG_AVI_INFOFRAME_VERSION	0x52
#define ADV7511_REG_AVI_INFOFRAME_LENGTH	0x53
#define ADV7511_REG_AVI_INFOFRAME_CHECKSUM	0x54
#define ADV7511_REG_AVI_INFOFRAME(x)		(0x55 + (x)) /* 0x55 - 0x6f */
#define ADV7511_REG_AUDIO_INFOFRAME_VERSION	0x70
#define ADV7511_REG_AUDIO_INFOFRAME_LENGTH	0x71
#define ADV7511_REG_AUDIO_INFOFRAME_CHECKSUM	0x72
#define ADV7511_REG_AUDIO_INFOFRAME(x)		(0x73 + (x)) /* 0x73 - 0x7c */
#define ADV7511_REG_INT_ENABLE(x)		(0x94 + (x))
#define ADV7511_REG_INT(x)			(0x96 + (x))
#define ADV7511_REG_INPUT_CLK_DIV		0x9d
#define ADV7511_REG_PLL_STATUS			0x9e
#define ADV7511_REG_HDMI_POWER			0xa1
#define ADV7511_REG_HDCP_HDMI_CFG		0xaf
#define ADV7511_REG_AN(x)			(0xb0 + (x)) /* 0xb0 - 0xb7 */
#define ADV7511_REG_HDCP_STATUS			0xb8
#define ADV7511_REG_BCAPS			0xbe
#define ADV7511_REG_BKSV(x)			(0xc0 + (x)) /* 0xc0 - 0xc3 */
#define ADV7511_REG_EDID_SEGMENT		0xc4
#define ADV7511_REG_DDC_STATUS			0xc8
#define ADV7511_REG_EDID_READ_CTRL		0xc9
#define ADV7511_REG_BSTATUS(x)			(0xca + (x)) /* 0xca - 0xcb */
#define ADV7511_REG_TIMING_GEN_SEQ		0xd0
#define ADV7511_REG_POWER2			0xd6
#define ADV7511_REG_HSYNC_PLACEMENT_MSB		0xfa

#define ADV7511_REG_SYNC_ADJUSTMENT(x)		(0xd7 + (x)) /* 0xd7 - 0xdc */
#define ADV7511_REG_TMDS_CLOCK_INV		0xde
#define ADV7511_REG_ARC_CTRL			0xdf
#define ADV7511_REG_CEC_I2C_ADDR		0xe1
#define ADV7511_REG_CEC_CTRL			0xe2
#define ADV7511_REG_CHIP_ID_HIGH		0xf5
#define ADV7511_REG_CHIP_ID_LOW			0xf6

/* Hardware defined default addresses for I2C register maps */
#define ADV7511_CEC_I2C_ADDR_DEFAULT		0x3c
#define ADV7511_EDID_I2C_ADDR_DEFAULT		0x3f
#define ADV7511_PACKET_I2C_ADDR_DEFAULT		0x38

#define ADV7511_CSC_ENABLE			BIT(7)
#define ADV7511_CSC_UPDATE_MODE			BIT(5)

#define ADV7511_INT0_HPD			BIT(7)
#define ADV7511_INT0_VSYNC			BIT(5)
#define ADV7511_INT0_AUDIO_FIFO_FULL		BIT(4)
#define ADV7511_INT0_EDID_READY			BIT(2)
#define ADV7511_INT0_HDCP_AUTHENTICATED		BIT(1)

#define ADV7511_INT1_DDC_ERROR			BIT(7)
#define ADV7511_INT1_BKSV			BIT(6)
#define ADV7511_INT1_CEC_TX_READY		BIT(5)
#define ADV7511_INT1_CEC_TX_ARBIT_LOST		BIT(4)
#define ADV7511_INT1_CEC_TX_RETRY_TIMEOUT	BIT(3)
#define ADV7511_INT1_CEC_RX_READY3		BIT(2)
#define ADV7511_INT1_CEC_RX_READY2		BIT(1)
#define ADV7511_INT1_CEC_RX_READY1		BIT(0)

#define ADV7511_ARC_CTRL_POWER_DOWN		BIT(0)

#define ADV7511_CEC_CTRL_POWER_DOWN		BIT(0)

#define ADV7511_POWER_POWER_DOWN		BIT(6)

#define ADV7511_HDMI_CFG_MODE_MASK		0x2
#define ADV7511_HDMI_CFG_MODE_DVI		0x0
#define ADV7511_HDMI_CFG_MODE_HDMI		0x2

#define ADV7511_AUDIO_SELECT_I2C		0x0
#define ADV7511_AUDIO_SELECT_SPDIF		0x1
#define ADV7511_AUDIO_SELECT_DSD		0x2
#define ADV7511_AUDIO_SELECT_HBR		0x3
#define ADV7511_AUDIO_SELECT_DST		0x4

#define ADV7511_I2S_SAMPLE_LEN_16		0x2
#define ADV7511_I2S_SAMPLE_LEN_20		0x3
#define ADV7511_I2S_SAMPLE_LEN_18		0x4
#define ADV7511_I2S_SAMPLE_LEN_22		0x5
#define ADV7511_I2S_SAMPLE_LEN_19		0x8
#define ADV7511_I2S_SAMPLE_LEN_23		0x9
#define ADV7511_I2S_SAMPLE_LEN_24		0xb
#define ADV7511_I2S_SAMPLE_LEN_17		0xc
#define ADV7511_I2S_SAMPLE_LEN_21		0xd

#define ADV7511_SAMPLE_FREQ_44100		0x0
#define ADV7511_SAMPLE_FREQ_48000		0x2
#define ADV7511_SAMPLE_FREQ_32000		0x3
#define ADV7511_SAMPLE_FREQ_88200		0x8
#define ADV7511_SAMPLE_FREQ_96000		0xa
#define ADV7511_SAMPLE_FREQ_176400		0xc
#define ADV7511_SAMPLE_FREQ_192000		0xe

#define ADV7511_STATUS_POWER_DOWN_POLARITY	BIT(7)
#define ADV7511_STATUS_HPD			BIT(6)
#define ADV7511_STATUS_MONITOR_SENSE		BIT(5)
#define ADV7511_STATUS_I2S_32BIT_MODE		BIT(3)

#define ADV7511_PACKET_ENABLE_N_CTS		BIT(8+6)
#define ADV7511_PACKET_ENABLE_AUDIO_SAMPLE	BIT(8+5)
#define ADV7511_PACKET_ENABLE_AVI_INFOFRAME	BIT(8+4)
#define ADV7511_PACKET_ENABLE_AUDIO_INFOFRAME	BIT(8+3)
#define ADV7511_PACKET_ENABLE_GC		BIT(7)
#define ADV7511_PACKET_ENABLE_SPD		BIT(6)
#define ADV7511_PACKET_ENABLE_MPEG		BIT(5)
#define ADV7511_PACKET_ENABLE_ACP		BIT(4)
#define ADV7511_PACKET_ENABLE_ISRC		BIT(3)
#define ADV7511_PACKET_ENABLE_GM		BIT(2)
#define ADV7511_PACKET_ENABLE_SPARE2		BIT(1)
#define ADV7511_PACKET_ENABLE_SPARE1		BIT(0)

#define ADV7535_REG_POWER2_HPD_OVERRIDE		BIT(6)
#define ADV7511_REG_POWER2_HPD_SRC_MASK		0xc0
#define ADV7511_REG_POWER2_HPD_SRC_BOTH		0x00
#define ADV7511_REG_POWER2_HPD_SRC_HPD		0x40
#define ADV7511_REG_POWER2_HPD_SRC_CEC		0x80
#define ADV7511_REG_POWER2_HPD_SRC_NONE		0xc0
#define ADV7511_REG_POWER2_TDMS_ENABLE		BIT(4)
#define ADV7511_REG_POWER2_GATE_INPUT_CLK	BIT(0)

#define ADV7511_LOW_REFRESH_RATE_NONE		0x0
#define ADV7511_LOW_REFRESH_RATE_24HZ		0x1
#define ADV7511_LOW_REFRESH_RATE_25HZ		0x2
#define ADV7511_LOW_REFRESH_RATE_30HZ		0x3

#define ADV7511_AUDIO_CFG3_LEN_MASK		0x0f
#define ADV7511_I2C_FREQ_ID_CFG_RATE_MASK	0xf0

#define ADV7511_AUDIO_SOURCE_I2S		0
#define ADV7511_AUDIO_SOURCE_SPDIF		1

#define ADV7511_I2S_FORMAT_I2S			0
#define ADV7511_I2S_FORMAT_RIGHT_J		1
#define ADV7511_I2S_FORMAT_LEFT_J		2
#define ADV7511_I2S_IEC958_DIRECT		3

#define ADV7511_PACKET(p, x)	    ((p) * 0x20 + (x))
#define ADV7511_PACKET_SDP(x)	    ADV7511_PACKET(0, x)
#define ADV7511_PACKET_MPEG(x)	    ADV7511_PACKET(1, x)
#define ADV7511_PACKET_ACP(x)	    ADV7511_PACKET(2, x)
#define ADV7511_PACKET_ISRC1(x)	    ADV7511_PACKET(3, x)
#define ADV7511_PACKET_ISRC2(x)	    ADV7511_PACKET(4, x)
#define ADV7511_PACKET_GM(x)	    ADV7511_PACKET(5, x)
#define ADV7511_PACKET_SPARE(x)	    ADV7511_PACKET(6, x)

#define ADV7511_REG_CEC_TX_FRAME_HDR	0x00
#define ADV7511_REG_CEC_TX_FRAME_DATA0	0x01
#define ADV7511_REG_CEC_TX_FRAME_LEN	0x10
#define ADV7511_REG_CEC_TX_ENABLE	0x11
#define ADV7511_REG_CEC_TX_RETRY	0x12
#define ADV7511_REG_CEC_TX_LOW_DRV_CNT	0x14
#define ADV7511_REG_CEC_RX1_FRAME_HDR	0x15
#define ADV7511_REG_CEC_RX1_FRAME_DATA0	0x16
#define ADV7511_REG_CEC_RX1_FRAME_LEN	0x25
#define ADV7511_REG_CEC_RX_STATUS	0x26
#define ADV7511_REG_CEC_RX2_FRAME_HDR	0x27
#define ADV7511_REG_CEC_RX2_FRAME_DATA0	0x28
#define ADV7511_REG_CEC_RX2_FRAME_LEN	0x37
#define ADV7511_REG_CEC_RX3_FRAME_HDR	0x38
#define ADV7511_REG_CEC_RX3_FRAME_DATA0	0x39
#define ADV7511_REG_CEC_RX3_FRAME_LEN	0x48
#define ADV7511_REG_CEC_RX_BUFFERS	0x4a
#define ADV7511_REG_CEC_LOG_ADDR_MASK	0x4b
#define ADV7511_REG_CEC_LOG_ADDR_0_1	0x4c
#define ADV7511_REG_CEC_LOG_ADDR_2	0x4d
#define ADV7511_REG_CEC_CLK_DIV		0x4e
#define ADV7511_REG_CEC_SOFT_RESET	0x50

static const u8 ADV7511_REG_CEC_RX_FRAME_HDR[] = {
	ADV7511_REG_CEC_RX1_FRAME_HDR,
	ADV7511_REG_CEC_RX2_FRAME_HDR,
	ADV7511_REG_CEC_RX3_FRAME_HDR,
};

static const u8 ADV7511_REG_CEC_RX_FRAME_LEN[] = {
	ADV7511_REG_CEC_RX1_FRAME_LEN,
	ADV7511_REG_CEC_RX2_FRAME_LEN,
	ADV7511_REG_CEC_RX3_FRAME_LEN,
};

#define ADV7533_REG_CEC_OFFSET		0x70

enum adv7511_input_clock {
	ADV7511_INPUT_CLOCK_1X,
	ADV7511_INPUT_CLOCK_2X,
	ADV7511_INPUT_CLOCK_DDR,
};

enum adv7511_input_justification {
	ADV7511_INPUT_JUSTIFICATION_EVENLY = 0,
	ADV7511_INPUT_JUSTIFICATION_RIGHT = 1,
	ADV7511_INPUT_JUSTIFICATION_LEFT = 2,
};

enum adv7511_input_sync_pulse {
	ADV7511_INPUT_SYNC_PULSE_DE = 0,
	ADV7511_INPUT_SYNC_PULSE_HSYNC = 1,
	ADV7511_INPUT_SYNC_PULSE_VSYNC = 2,
	ADV7511_INPUT_SYNC_PULSE_NONE = 3,
};

/**
 * enum adv7511_sync_polarity - Polarity for the input sync signals
 * @ADV7511_SYNC_POLARITY_PASSTHROUGH:  Sync polarity matches that of
 *				       the currently configured mode.
 * @ADV7511_SYNC_POLARITY_LOW:	    Sync polarity is low
 * @ADV7511_SYNC_POLARITY_HIGH:	    Sync polarity is high
 *
 * If the polarity is set to either LOW or HIGH the driver will configure the
 * ADV7511 to internally invert the sync signal if required to match the sync
 * polarity setting for the currently selected output mode.
 *
 * If the polarity is set to PASSTHROUGH, the ADV7511 will route the signal
 * unchanged. This is used when the upstream graphics core already generates
 * the sync signals with the correct polarity.
 */
enum adv7511_sync_polarity {
	ADV7511_SYNC_POLARITY_PASSTHROUGH,
	ADV7511_SYNC_POLARITY_LOW,
	ADV7511_SYNC_POLARITY_HIGH,
};

/**
 * struct adv7511_link_config - Describes adv7511 hardware configuration
 * @input_color_depth:		Number of bits per color component (8, 10 or 12)
 * @input_colorspace:		The input colorspace (RGB, YUV444, YUV422)
 * @input_clock:		The input video clock style (1x, 2x, DDR)
 * @input_style:		The input component arrangement variant
 * @input_justification:	Video input format bit justification
 * @clock_delay:		Clock delay for the input clock (in ps)
 * @embedded_sync:		Video input uses BT.656-style embedded sync
 * @sync_pulse:			Select the sync pulse
 * @vsync_polarity:		vsync input signal configuration
 * @hsync_polarity:		hsync input signal configuration
 */
struct adv7511_link_config {
	unsigned int input_color_depth;
	enum hdmi_colorspace input_colorspace;
	enum adv7511_input_clock input_clock;
	unsigned int input_style;
	enum adv7511_input_justification input_justification;

	int clock_delay;

	bool embedded_sync;
	enum adv7511_input_sync_pulse sync_pulse;
	enum adv7511_sync_polarity vsync_polarity;
	enum adv7511_sync_polarity hsync_polarity;
};

/**
 * enum adv7511_csc_scaling - Scaling factor for the ADV7511 CSC
 * @ADV7511_CSC_SCALING_1: CSC results are not scaled
 * @ADV7511_CSC_SCALING_2: CSC results are scaled by a factor of two
 * @ADV7511_CSC_SCALING_4: CSC results are scalled by a factor of four
 */
enum adv7511_csc_scaling {
	ADV7511_CSC_SCALING_1 = 0,
	ADV7511_CSC_SCALING_2 = 1,
	ADV7511_CSC_SCALING_4 = 2,
};

/**
 * struct adv7511_video_config - Describes adv7511 hardware configuration
 * @csc_enable:			Whether to enable color space conversion
 * @csc_scaling_factor:		Color space conversion scaling factor
 * @csc_coefficents:		Color space conversion coefficents
 * @hdmi_mode:			Whether to use HDMI or DVI output mode
 * @avi_infoframe:		HDMI infoframe
 */
struct adv7511_video_config {
	bool csc_enable;
	enum adv7511_csc_scaling csc_scaling_factor;
	const uint16_t *csc_coefficents;

	bool hdmi_mode;
	struct hdmi_avi_infoframe avi_infoframe;
};

enum adv7511_type {
	ADV7511,
	ADV7533,
	ADV7535,
};

#define ADV7511_MAX_ADDRS 3

struct adv7511 {
	struct i2c_client *i2c_main;
	struct i2c_client *i2c_edid;
	struct i2c_client *i2c_packet;
	struct i2c_client *i2c_cec;

	struct regmap *regmap;
	struct regmap *regmap_cec;
	unsigned int reg_cec_offset;
	enum drm_connector_status status;
	bool powered;

	struct drm_display_mode curr_mode;

	unsigned int f_tmds;
	unsigned int f_audio;
	unsigned int audio_source;

	unsigned int current_edid_segment;
	uint8_t edid_buf[256];
	bool edid_read;

	wait_queue_head_t wq;
	struct work_struct hpd_work;

	struct drm_bridge bridge;
	struct drm_connector connector;

	bool embedded_sync;
	enum adv7511_sync_polarity vsync_polarity;
	enum adv7511_sync_polarity hsync_polarity;
	bool rgb;

	struct gpio_desc *gpio_pd;

	struct regulator_bulk_data *supplies;
	unsigned int num_supplies;

	/* ADV7533 DSI RX related params */
	struct device_node *host_node;
	struct mipi_dsi_device *dsi;
	u8 num_dsi_lanes;
	bool use_timing_gen;

	enum adv7511_type type;
	struct platform_device *audio_pdev;

	struct cec_adapter *cec_adap;
	u8   cec_addr[ADV7511_MAX_ADDRS];
	u8   cec_valid_addrs;
	bool cec_enabled_adap;
	struct clk *cec_clk;
	u32 cec_clk_freq;
};

#ifdef CONFIG_DRM_I2C_ADV7511_CEC
int adv7511_cec_init(struct device *dev, struct adv7511 *adv7511);
void adv7511_cec_irq_process(struct adv7511 *adv7511, unsigned int irq1);
#else
static inline int adv7511_cec_init(struct device *dev, struct adv7511 *adv7511)
{
	unsigned int offset = adv7511->type == ADV7533 ?
						ADV7533_REG_CEC_OFFSET : 0;

	regmap_write(adv7511->regmap, ADV7511_REG_CEC_CTRL + offset,
		     ADV7511_CEC_CTRL_POWER_DOWN);
	return 0;
}
#endif

void adv7533_dsi_power_on(struct adv7511 *adv);
void adv7533_dsi_power_off(struct adv7511 *adv);
void adv7533_mode_set(struct adv7511 *adv, const struct drm_display_mode *mode);
int adv7533_patch_registers(struct adv7511 *adv);
int adv7533_patch_cec_registers(struct adv7511 *adv);
int adv7533_attach_dsi(struct adv7511 *adv);
int adv7533_parse_dt(struct device_node *np, struct adv7511 *adv);

#ifdef CONFIG_DRM_I2C_ADV7511_AUDIO
int adv7511_audio_init(struct device *dev, struct adv7511 *adv7511);
void adv7511_audio_exit(struct adv7511 *adv7511);
#else /*CONFIG_DRM_I2C_ADV7511_AUDIO */
static inline int adv7511_audio_init(struct device *dev, struct adv7511 *adv7511)
{
	return 0;
}
static inline void adv7511_audio_exit(struct adv7511 *adv7511)
{
}
#endif /* CONFIG_DRM_I2C_ADV7511_AUDIO */

#endif /* __DRM_I2C_ADV7511_H__ */
