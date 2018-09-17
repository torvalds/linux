/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Driver for Analog Devices ADV748X video decoder and HDMI receiver
 *
 * Copyright (C) 2017 Renesas Electronics Corp.
 *
 * Authors:
 *	Koji Matsuoka <koji.matsuoka.xm@renesas.com>
 *	Niklas Söderlund <niklas.soderlund@ragnatech.se>
 *	Kieran Bingham <kieran.bingham@ideasonboard.com>
 *
 * The ADV748x range of receivers have the following configurations:
 *
 *                  Analog   HDMI  MHL  4-Lane  1-Lane
 *                    In      In         CSI     CSI
 *       ADV7480               X    X     X
 *       ADV7481      X        X    X     X       X
 *       ADV7482      X        X          X       X
 */

#include <linux/i2c.h>

#ifndef _ADV748X_H_
#define _ADV748X_H_

enum adv748x_page {
	ADV748X_PAGE_IO,
	ADV748X_PAGE_DPLL,
	ADV748X_PAGE_CP,
	ADV748X_PAGE_HDMI,
	ADV748X_PAGE_EDID,
	ADV748X_PAGE_REPEATER,
	ADV748X_PAGE_INFOFRAME,
	ADV748X_PAGE_CBUS,
	ADV748X_PAGE_CEC,
	ADV748X_PAGE_SDP,
	ADV748X_PAGE_TXB,
	ADV748X_PAGE_TXA,
	ADV748X_PAGE_MAX,

	/* Fake pages for register sequences */
	ADV748X_PAGE_WAIT,		/* Wait x msec */
	ADV748X_PAGE_EOR,		/* End Mark */
};

/**
 * enum adv748x_ports - Device tree port number definitions
 *
 * The ADV748X ports define the mapping between subdevices
 * and the device tree specification
 */
enum adv748x_ports {
	ADV748X_PORT_AIN0 = 0,
	ADV748X_PORT_AIN1 = 1,
	ADV748X_PORT_AIN2 = 2,
	ADV748X_PORT_AIN3 = 3,
	ADV748X_PORT_AIN4 = 4,
	ADV748X_PORT_AIN5 = 5,
	ADV748X_PORT_AIN6 = 6,
	ADV748X_PORT_AIN7 = 7,
	ADV748X_PORT_HDMI = 8,
	ADV748X_PORT_TTL = 9,
	ADV748X_PORT_TXA = 10,
	ADV748X_PORT_TXB = 11,
	ADV748X_PORT_MAX = 12,
};

enum adv748x_csi2_pads {
	ADV748X_CSI2_SINK,
	ADV748X_CSI2_SOURCE,
	ADV748X_CSI2_NR_PADS,
};

/* CSI2 transmitters can have 2 internal connections, HDMI/AFE */
#define ADV748X_CSI2_MAX_SUBDEVS 2

struct adv748x_csi2 {
	struct adv748x_state *state;
	struct v4l2_mbus_framefmt format;
	unsigned int page;
	unsigned int port;

	struct media_pad pads[ADV748X_CSI2_NR_PADS];
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_subdev sd;
};

#define notifier_to_csi2(n) container_of(n, struct adv748x_csi2, notifier)
#define adv748x_sd_to_csi2(sd) container_of(sd, struct adv748x_csi2, sd)
#define is_tx_enabled(_tx) ((_tx)->state->endpoints[(_tx)->port] != NULL)
#define is_txa(_tx) ((_tx) == &(_tx)->state->txa)

enum adv748x_hdmi_pads {
	ADV748X_HDMI_SINK,
	ADV748X_HDMI_SOURCE,
	ADV748X_HDMI_NR_PADS,
};

struct adv748x_hdmi {
	struct media_pad pads[ADV748X_HDMI_NR_PADS];
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_subdev sd;
	struct v4l2_mbus_framefmt format;

	struct v4l2_dv_timings timings;
	struct v4l2_fract aspect_ratio;

	struct {
		u8 edid[512];
		u32 present;
		unsigned int blocks;
	} edid;
};

#define adv748x_ctrl_to_hdmi(ctrl) \
	container_of(ctrl->handler, struct adv748x_hdmi, ctrl_hdl)
#define adv748x_sd_to_hdmi(sd) container_of(sd, struct adv748x_hdmi, sd)

enum adv748x_afe_pads {
	ADV748X_AFE_SINK_AIN0,
	ADV748X_AFE_SINK_AIN1,
	ADV748X_AFE_SINK_AIN2,
	ADV748X_AFE_SINK_AIN3,
	ADV748X_AFE_SINK_AIN4,
	ADV748X_AFE_SINK_AIN5,
	ADV748X_AFE_SINK_AIN6,
	ADV748X_AFE_SINK_AIN7,
	ADV748X_AFE_SOURCE,
	ADV748X_AFE_NR_PADS,
};

struct adv748x_afe {
	struct media_pad pads[ADV748X_AFE_NR_PADS];
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_subdev sd;
	struct v4l2_mbus_framefmt format;

	bool streaming;
	v4l2_std_id curr_norm;
	unsigned int input;
};

#define adv748x_ctrl_to_afe(ctrl) \
	container_of(ctrl->handler, struct adv748x_afe, ctrl_hdl)
#define adv748x_sd_to_afe(sd) container_of(sd, struct adv748x_afe, sd)

/**
 * struct adv748x_state - State of ADV748X
 * @dev:		(OF) device
 * @client:		I2C client
 * @mutex:		protect global state
 *
 * @endpoints:		parsed device node endpoints for each port
 *
 * @i2c_addresses	I2C Page addresses
 * @i2c_clients		I2C clients for the page accesses
 * @regmap		regmap configuration pages.
 *
 * @hdmi:		state of HDMI receiver context
 * @afe:		state of AFE receiver context
 * @txa:		state of TXA transmitter context
 * @txb:		state of TXB transmitter context
 */
struct adv748x_state {
	struct device *dev;
	struct i2c_client *client;
	struct mutex mutex;

	struct device_node *endpoints[ADV748X_PORT_MAX];

	struct i2c_client *i2c_clients[ADV748X_PAGE_MAX];
	struct regmap *regmap[ADV748X_PAGE_MAX];

	struct adv748x_hdmi hdmi;
	struct adv748x_afe afe;
	struct adv748x_csi2 txa;
	struct adv748x_csi2 txb;
};

#define adv748x_hdmi_to_state(h) container_of(h, struct adv748x_state, hdmi)
#define adv748x_afe_to_state(a) container_of(a, struct adv748x_state, afe)

#define adv_err(a, fmt, arg...)	dev_err(a->dev, fmt, ##arg)
#define adv_info(a, fmt, arg...) dev_info(a->dev, fmt, ##arg)
#define adv_dbg(a, fmt, arg...)	dev_dbg(a->dev, fmt, ##arg)

/* Register Mappings */

/* IO Map */
#define ADV748X_IO_PD			0x00	/* power down controls */
#define ADV748X_IO_PD_RX_EN		BIT(6)

#define ADV748X_IO_REG_04		0x04
#define ADV748X_IO_REG_04_FORCE_FR	BIT(0)	/* Force CP free-run */

#define ADV748X_IO_DATAPATH		0x03	/* datapath cntrl */
#define ADV748X_IO_DATAPATH_VFREQ_M	0x70
#define ADV748X_IO_DATAPATH_VFREQ_SHIFT	4

#define ADV748X_IO_VID_STD		0x05

#define ADV748X_IO_10			0x10	/* io_reg_10 */
#define ADV748X_IO_10_CSI4_EN		BIT(7)
#define ADV748X_IO_10_CSI1_EN		BIT(6)
#define ADV748X_IO_10_PIX_OUT_EN	BIT(5)

#define ADV748X_IO_CHIP_REV_ID_1	0xdf
#define ADV748X_IO_CHIP_REV_ID_2	0xe0

#define ADV748X_IO_SLAVE_ADDR_BASE	0xf2

/* HDMI RX Map */
#define ADV748X_HDMI_LW1		0x07	/* line width_1 */
#define ADV748X_HDMI_LW1_VERT_FILTER	BIT(7)
#define ADV748X_HDMI_LW1_DE_REGEN	BIT(5)
#define ADV748X_HDMI_LW1_WIDTH_MASK	0x1fff

#define ADV748X_HDMI_F0H1		0x09	/* field0 height_1 */
#define ADV748X_HDMI_F0H1_HEIGHT_MASK	0x1fff

#define ADV748X_HDMI_F1H1		0x0b	/* field1 height_1 */
#define ADV748X_HDMI_F1H1_INTERLACED	BIT(5)

#define ADV748X_HDMI_HFRONT_PORCH	0x20	/* hsync_front_porch_1 */
#define ADV748X_HDMI_HFRONT_PORCH_MASK	0x1fff

#define ADV748X_HDMI_HSYNC_WIDTH	0x22	/* hsync_pulse_width_1 */
#define ADV748X_HDMI_HSYNC_WIDTH_MASK	0x1fff

#define ADV748X_HDMI_HBACK_PORCH	0x24	/* hsync_back_porch_1 */
#define ADV748X_HDMI_HBACK_PORCH_MASK	0x1fff

#define ADV748X_HDMI_VFRONT_PORCH	0x2a	/* field0_vs_front_porch_1 */
#define ADV748X_HDMI_VFRONT_PORCH_MASK	0x3fff

#define ADV748X_HDMI_VSYNC_WIDTH	0x2e	/* field0_vs_pulse_width_1 */
#define ADV748X_HDMI_VSYNC_WIDTH_MASK	0x3fff

#define ADV748X_HDMI_VBACK_PORCH	0x32	/* field0_vs_back_porch_1 */
#define ADV748X_HDMI_VBACK_PORCH_MASK	0x3fff

#define ADV748X_HDMI_TMDS_1		0x51	/* hdmi_reg_51 */
#define ADV748X_HDMI_TMDS_2		0x52	/* hdmi_reg_52 */

/* HDMI RX Repeater Map */
#define ADV748X_REPEATER_EDID_SZ	0x70	/* primary_edid_size */
#define ADV748X_REPEATER_EDID_SZ_SHIFT	4

#define ADV748X_REPEATER_EDID_CTL	0x74	/* hdcp edid controls */
#define ADV748X_REPEATER_EDID_CTL_EN	BIT(0)	/* man_edid_a_enable */

/* SDP Main Map */
#define ADV748X_SDP_INSEL		0x00	/* user_map_rw_reg_00 */

#define ADV748X_SDP_VID_SEL		0x02	/* user_map_rw_reg_02 */
#define ADV748X_SDP_VID_SEL_MASK	0xf0
#define ADV748X_SDP_VID_SEL_SHIFT	4

/* Contrast - Unsigned*/
#define ADV748X_SDP_CON			0x08	/* user_map_rw_reg_08 */
#define ADV748X_SDP_CON_MIN		0
#define ADV748X_SDP_CON_DEF		128
#define ADV748X_SDP_CON_MAX		255

/* Brightness - Signed */
#define ADV748X_SDP_BRI			0x0a	/* user_map_rw_reg_0a */
#define ADV748X_SDP_BRI_MIN		-128
#define ADV748X_SDP_BRI_DEF		0
#define ADV748X_SDP_BRI_MAX		127

/* Hue - Signed, inverted*/
#define ADV748X_SDP_HUE			0x0b	/* user_map_rw_reg_0b */
#define ADV748X_SDP_HUE_MIN		-127
#define ADV748X_SDP_HUE_DEF		0
#define ADV748X_SDP_HUE_MAX		128

/* Test Patterns / Default Values */
#define ADV748X_SDP_DEF			0x0c	/* user_map_rw_reg_0c */
#define ADV748X_SDP_DEF_VAL_EN		BIT(0)	/* Force free run mode */
#define ADV748X_SDP_DEF_VAL_AUTO_EN	BIT(1)	/* Free run when no signal */

#define ADV748X_SDP_MAP_SEL		0x0e	/* user_map_rw_reg_0e */
#define ADV748X_SDP_MAP_SEL_RO_MAIN	1

/* Free run pattern select */
#define ADV748X_SDP_FRP			0x14
#define ADV748X_SDP_FRP_MASK		GENMASK(3, 1)

/* Saturation */
#define ADV748X_SDP_SD_SAT_U		0xe3	/* user_map_rw_reg_e3 */
#define ADV748X_SDP_SD_SAT_V		0xe4	/* user_map_rw_reg_e4 */
#define ADV748X_SDP_SAT_MIN		0
#define ADV748X_SDP_SAT_DEF		128
#define ADV748X_SDP_SAT_MAX		255

/* SDP RO Main Map */
#define ADV748X_SDP_RO_10		0x10
#define ADV748X_SDP_RO_10_IN_LOCK	BIT(0)

/* CP Map */
#define ADV748X_CP_PAT_GEN		0x37	/* int_pat_gen_1 */
#define ADV748X_CP_PAT_GEN_EN		BIT(7)

/* Contrast Control - Unsigned */
#define ADV748X_CP_CON			0x3a	/* contrast_cntrl */
#define ADV748X_CP_CON_MIN		0	/* Minimum contrast */
#define ADV748X_CP_CON_DEF		128	/* Default */
#define ADV748X_CP_CON_MAX		255	/* Maximum contrast */

/* Saturation Control - Unsigned */
#define ADV748X_CP_SAT			0x3b	/* saturation_cntrl */
#define ADV748X_CP_SAT_MIN		0	/* Minimum saturation */
#define ADV748X_CP_SAT_DEF		128	/* Default */
#define ADV748X_CP_SAT_MAX		255	/* Maximum saturation */

/* Brightness Control - Signed */
#define ADV748X_CP_BRI			0x3c	/* brightness_cntrl */
#define ADV748X_CP_BRI_MIN		-128	/* Luma is -512d */
#define ADV748X_CP_BRI_DEF		0	/* Luma is 0 */
#define ADV748X_CP_BRI_MAX		127	/* Luma is 508d */

/* Hue Control */
#define ADV748X_CP_HUE			0x3d	/* hue_cntrl */
#define ADV748X_CP_HUE_MIN		0	/* -90 degree */
#define ADV748X_CP_HUE_DEF		0	/* -90 degree */
#define ADV748X_CP_HUE_MAX		255	/* +90 degree */

#define ADV748X_CP_VID_ADJ		0x3e	/* vid_adj_0 */
#define ADV748X_CP_VID_ADJ_ENABLE	BIT(7)	/* Enable colour controls */

#define ADV748X_CP_DE_POS_HIGH		0x8b	/* de_pos_adj_6 */
#define ADV748X_CP_DE_POS_HIGH_SET	BIT(6)
#define ADV748X_CP_DE_POS_END_LOW	0x8c	/* de_pos_adj_7 */
#define ADV748X_CP_DE_POS_START_LOW	0x8d	/* de_pos_adj_8 */

#define ADV748X_CP_VID_ADJ_2			0x91
#define ADV748X_CP_VID_ADJ_2_INTERLACED		BIT(6)
#define ADV748X_CP_VID_ADJ_2_INTERLACED_3D	BIT(4)

#define ADV748X_CP_CLMP_POS		0xc9	/* clmp_pos_cntrl_4 */
#define ADV748X_CP_CLMP_POS_DIS_AUTO	BIT(0)	/* dis_auto_param_buff */

/* CSI : TXA/TXB Maps */
#define ADV748X_CSI_VC_REF		0x0d	/* csi_tx_top_reg_0d */
#define ADV748X_CSI_VC_REF_SHIFT	6

#define ADV748X_CSI_FS_AS_LS		0x1e	/* csi_tx_top_reg_1e */
#define ADV748X_CSI_FS_AS_LS_UNKNOWN	BIT(6)	/* Undocumented bit */

/* Register handling */

int adv748x_read(struct adv748x_state *state, u8 addr, u8 reg);
int adv748x_write(struct adv748x_state *state, u8 page, u8 reg, u8 value);
int adv748x_write_block(struct adv748x_state *state, int client_page,
			unsigned int init_reg, const void *val,
			size_t val_len);

#define io_read(s, r) adv748x_read(s, ADV748X_PAGE_IO, r)
#define io_write(s, r, v) adv748x_write(s, ADV748X_PAGE_IO, r, v)
#define io_clrset(s, r, m, v) io_write(s, r, (io_read(s, r) & ~m) | v)

#define hdmi_read(s, r) adv748x_read(s, ADV748X_PAGE_HDMI, r)
#define hdmi_read16(s, r, m) (((hdmi_read(s, r) << 8) | hdmi_read(s, r+1)) & m)
#define hdmi_write(s, r, v) adv748x_write(s, ADV748X_PAGE_HDMI, r, v)

#define repeater_read(s, r) adv748x_read(s, ADV748X_PAGE_REPEATER, r)
#define repeater_write(s, r, v) adv748x_write(s, ADV748X_PAGE_REPEATER, r, v)

#define sdp_read(s, r) adv748x_read(s, ADV748X_PAGE_SDP, r)
#define sdp_write(s, r, v) adv748x_write(s, ADV748X_PAGE_SDP, r, v)
#define sdp_clrset(s, r, m, v) sdp_write(s, r, (sdp_read(s, r) & ~m) | v)

#define cp_read(s, r) adv748x_read(s, ADV748X_PAGE_CP, r)
#define cp_write(s, r, v) adv748x_write(s, ADV748X_PAGE_CP, r, v)
#define cp_clrset(s, r, m, v) cp_write(s, r, (cp_read(s, r) & ~m) | v)

#define tx_read(t, r) adv748x_read(t->state, t->page, r)
#define tx_write(t, r, v) adv748x_write(t->state, t->page, r, v)

static inline struct v4l2_subdev *adv748x_get_remote_sd(struct media_pad *pad)
{
	pad = media_entity_remote_pad(pad);
	if (!pad)
		return NULL;

	return media_entity_to_v4l2_subdev(pad->entity);
}

void adv748x_subdev_init(struct v4l2_subdev *sd, struct adv748x_state *state,
			 const struct v4l2_subdev_ops *ops, u32 function,
			 const char *ident);

int adv748x_register_subdevs(struct adv748x_state *state,
			     struct v4l2_device *v4l2_dev);

int adv748x_tx_power(struct adv748x_csi2 *tx, bool on);

int adv748x_afe_init(struct adv748x_afe *afe);
void adv748x_afe_cleanup(struct adv748x_afe *afe);

int adv748x_csi2_init(struct adv748x_state *state, struct adv748x_csi2 *tx);
void adv748x_csi2_cleanup(struct adv748x_csi2 *tx);
int adv748x_csi2_set_pixelrate(struct v4l2_subdev *sd, s64 rate);

int adv748x_hdmi_init(struct adv748x_hdmi *hdmi);
void adv748x_hdmi_cleanup(struct adv748x_hdmi *hdmi);

#endif /* _ADV748X_H_ */
