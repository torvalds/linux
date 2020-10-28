// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for Analog Devices ADV748X HDMI receiver with AFE
 *
 * Copyright (C) 2017 Renesas Electronics Corp.
 *
 * Authors:
 *	Koji Matsuoka <koji.matsuoka.xm@renesas.com>
 *	Niklas Söderlund <niklas.soderlund@ragnatech.se>
 *	Kieran Bingham <kieran.bingham@ideasonboard.com>
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/v4l2-dv-timings.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-ioctl.h>

#include "adv748x.h"

/* -----------------------------------------------------------------------------
 * Register manipulation
 */

#define ADV748X_REGMAP_CONF(n) \
{ \
	.name = n, \
	.reg_bits = 8, \
	.val_bits = 8, \
	.max_register = 0xff, \
	.cache_type = REGCACHE_NONE, \
}

static const struct regmap_config adv748x_regmap_cnf[] = {
	ADV748X_REGMAP_CONF("io"),
	ADV748X_REGMAP_CONF("dpll"),
	ADV748X_REGMAP_CONF("cp"),
	ADV748X_REGMAP_CONF("hdmi"),
	ADV748X_REGMAP_CONF("edid"),
	ADV748X_REGMAP_CONF("repeater"),
	ADV748X_REGMAP_CONF("infoframe"),
	ADV748X_REGMAP_CONF("cbus"),
	ADV748X_REGMAP_CONF("cec"),
	ADV748X_REGMAP_CONF("sdp"),
	ADV748X_REGMAP_CONF("txa"),
	ADV748X_REGMAP_CONF("txb"),
};

static int adv748x_configure_regmap(struct adv748x_state *state, int region)
{
	int err;

	if (!state->i2c_clients[region])
		return -ENODEV;

	state->regmap[region] =
		devm_regmap_init_i2c(state->i2c_clients[region],
				     &adv748x_regmap_cnf[region]);

	if (IS_ERR(state->regmap[region])) {
		err = PTR_ERR(state->regmap[region]);
		adv_err(state,
			"Error initializing regmap %d with error %d\n",
			region, err);
		return -EINVAL;
	}

	return 0;
}
struct adv748x_register_map {
	const char *name;
	u8 default_addr;
};

static const struct adv748x_register_map adv748x_default_addresses[] = {
	[ADV748X_PAGE_IO] = { "main", 0x70 },
	[ADV748X_PAGE_DPLL] = { "dpll", 0x26 },
	[ADV748X_PAGE_CP] = { "cp", 0x22 },
	[ADV748X_PAGE_HDMI] = { "hdmi", 0x34 },
	[ADV748X_PAGE_EDID] = { "edid", 0x36 },
	[ADV748X_PAGE_REPEATER] = { "repeater", 0x32 },
	[ADV748X_PAGE_INFOFRAME] = { "infoframe", 0x31 },
	[ADV748X_PAGE_CBUS] = { "cbus", 0x30 },
	[ADV748X_PAGE_CEC] = { "cec", 0x41 },
	[ADV748X_PAGE_SDP] = { "sdp", 0x79 },
	[ADV748X_PAGE_TXB] = { "txb", 0x48 },
	[ADV748X_PAGE_TXA] = { "txa", 0x4a },
};

static int adv748x_read_check(struct adv748x_state *state,
			      int client_page, u8 reg)
{
	struct i2c_client *client = state->i2c_clients[client_page];
	int err;
	unsigned int val;

	err = regmap_read(state->regmap[client_page], reg, &val);

	if (err) {
		adv_err(state, "error reading %02x, %02x\n",
				client->addr, reg);
		return err;
	}

	return val;
}

int adv748x_read(struct adv748x_state *state, u8 page, u8 reg)
{
	return adv748x_read_check(state, page, reg);
}

int adv748x_write(struct adv748x_state *state, u8 page, u8 reg, u8 value)
{
	return regmap_write(state->regmap[page], reg, value);
}

static int adv748x_write_check(struct adv748x_state *state, u8 page, u8 reg,
			       u8 value, int *error)
{
	if (*error)
		return *error;

	*error = adv748x_write(state, page, reg, value);
	return *error;
}

/* adv748x_write_block(): Write raw data with a maximum of I2C_SMBUS_BLOCK_MAX
 * size to one or more registers.
 *
 * A value of zero will be returned on success, a negative errno will
 * be returned in error cases.
 */
int adv748x_write_block(struct adv748x_state *state, int client_page,
			unsigned int init_reg, const void *val,
			size_t val_len)
{
	struct regmap *regmap = state->regmap[client_page];

	if (val_len > I2C_SMBUS_BLOCK_MAX)
		val_len = I2C_SMBUS_BLOCK_MAX;

	return regmap_raw_write(regmap, init_reg, val, val_len);
}

static int adv748x_set_slave_addresses(struct adv748x_state *state)
{
	struct i2c_client *client;
	unsigned int i;
	u8 io_reg;

	for (i = ADV748X_PAGE_DPLL; i < ADV748X_PAGE_MAX; ++i) {
		io_reg = ADV748X_IO_SLAVE_ADDR_BASE + i;
		client = state->i2c_clients[i];

		io_write(state, io_reg, client->addr << 1);
	}

	return 0;
}

static void adv748x_unregister_clients(struct adv748x_state *state)
{
	unsigned int i;

	for (i = 1; i < ARRAY_SIZE(state->i2c_clients); ++i)
		i2c_unregister_device(state->i2c_clients[i]);
}

static int adv748x_initialise_clients(struct adv748x_state *state)
{
	unsigned int i;
	int ret;

	for (i = ADV748X_PAGE_DPLL; i < ADV748X_PAGE_MAX; ++i) {
		state->i2c_clients[i] = i2c_new_ancillary_device(
				state->client,
				adv748x_default_addresses[i].name,
				adv748x_default_addresses[i].default_addr);

		if (IS_ERR(state->i2c_clients[i])) {
			adv_err(state, "failed to create i2c client %u\n", i);
			return PTR_ERR(state->i2c_clients[i]);
		}

		ret = adv748x_configure_regmap(state, i);
		if (ret)
			return ret;
	}

	return adv748x_set_slave_addresses(state);
}

/**
 * struct adv748x_reg_value - Register write instruction
 * @page:		Regmap page identifier
 * @reg:		I2C register
 * @value:		value to write to @page at @reg
 */
struct adv748x_reg_value {
	u8 page;
	u8 reg;
	u8 value;
};

static int adv748x_write_regs(struct adv748x_state *state,
			      const struct adv748x_reg_value *regs)
{
	int ret;

	for (; regs->page != ADV748X_PAGE_EOR; regs++) {
		ret = adv748x_write(state, regs->page, regs->reg, regs->value);
		if (ret < 0) {
			adv_err(state, "Error regs page: 0x%02x reg: 0x%02x\n",
				regs->page, regs->reg);
			return ret;
		}
	}

	return 0;
}

/* -----------------------------------------------------------------------------
 * TXA and TXB
 */

static int adv748x_power_up_tx(struct adv748x_csi2 *tx)
{
	struct adv748x_state *state = tx->state;
	u8 page = is_txa(tx) ? ADV748X_PAGE_TXA : ADV748X_PAGE_TXB;
	int ret = 0;

	/* Enable n-lane MIPI */
	adv748x_write_check(state, page, 0x00, 0x80 | tx->active_lanes, &ret);

	/* Set Auto DPHY Timing */
	adv748x_write_check(state, page, 0x00, 0xa0 | tx->active_lanes, &ret);

	/* ADI Required Write */
	if (tx->src == &state->hdmi.sd) {
		adv748x_write_check(state, page, 0xdb, 0x10, &ret);
		adv748x_write_check(state, page, 0xd6, 0x07, &ret);
	} else {
		adv748x_write_check(state, page, 0xd2, 0x40, &ret);
	}

	adv748x_write_check(state, page, 0xc4, 0x0a, &ret);
	adv748x_write_check(state, page, 0x71, 0x33, &ret);
	adv748x_write_check(state, page, 0x72, 0x11, &ret);

	/* i2c_dphy_pwdn - 1'b0 */
	adv748x_write_check(state, page, 0xf0, 0x00, &ret);

	/* ADI Required Writes*/
	adv748x_write_check(state, page, 0x31, 0x82, &ret);
	adv748x_write_check(state, page, 0x1e, 0x40, &ret);

	/* i2c_mipi_pll_en - 1'b1 */
	adv748x_write_check(state, page, 0xda, 0x01, &ret);
	usleep_range(2000, 2500);

	/* Power-up CSI-TX */
	adv748x_write_check(state, page, 0x00, 0x20 | tx->active_lanes, &ret);
	usleep_range(1000, 1500);

	/* ADI Required Writes */
	adv748x_write_check(state, page, 0xc1, 0x2b, &ret);
	usleep_range(1000, 1500);
	adv748x_write_check(state, page, 0x31, 0x80, &ret);

	return ret;
}

static int adv748x_power_down_tx(struct adv748x_csi2 *tx)
{
	struct adv748x_state *state = tx->state;
	u8 page = is_txa(tx) ? ADV748X_PAGE_TXA : ADV748X_PAGE_TXB;
	int ret = 0;

	/* ADI Required Writes */
	adv748x_write_check(state, page, 0x31, 0x82, &ret);
	adv748x_write_check(state, page, 0x1e, 0x00, &ret);

	/* Enable n-lane MIPI */
	adv748x_write_check(state, page, 0x00, 0x80 | tx->active_lanes, &ret);

	/* i2c_mipi_pll_en - 1'b1 */
	adv748x_write_check(state, page, 0xda, 0x01, &ret);

	/* ADI Required Write */
	adv748x_write_check(state, page, 0xc1, 0x3b, &ret);

	return ret;
}

int adv748x_tx_power(struct adv748x_csi2 *tx, bool on)
{
	int val;

	if (!is_tx_enabled(tx))
		return 0;

	val = tx_read(tx, ADV748X_CSI_FS_AS_LS);
	if (val < 0)
		return val;

	/*
	 * This test against BIT(6) is not documented by the datasheet, but was
	 * specified in the downstream driver.
	 * Track with a WARN_ONCE to determine if it is ever set by HW.
	 */
	WARN_ONCE((on && val & ADV748X_CSI_FS_AS_LS_UNKNOWN),
			"Enabling with unknown bit set");

	return on ? adv748x_power_up_tx(tx) : adv748x_power_down_tx(tx);
}

/* -----------------------------------------------------------------------------
 * Media Operations
 */
static int adv748x_link_setup(struct media_entity *entity,
			      const struct media_pad *local,
			      const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *rsd = media_entity_to_v4l2_subdev(remote->entity);
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct adv748x_state *state = v4l2_get_subdevdata(sd);
	struct adv748x_csi2 *tx = adv748x_sd_to_csi2(sd);
	bool enable = flags & MEDIA_LNK_FL_ENABLED;
	u8 io10_mask = ADV748X_IO_10_CSI1_EN |
		       ADV748X_IO_10_CSI4_EN |
		       ADV748X_IO_10_CSI4_IN_SEL_AFE;
	u8 io10 = 0;

	/* Refuse to enable multiple links to the same TX at the same time. */
	if (enable && tx->src)
		return -EINVAL;

	/* Set or clear the source (HDMI or AFE) and the current TX. */
	if (rsd == &state->afe.sd)
		state->afe.tx = enable ? tx : NULL;
	else
		state->hdmi.tx = enable ? tx : NULL;

	tx->src = enable ? rsd : NULL;

	if (state->afe.tx) {
		/* AFE Requires TXA enabled, even when output to TXB */
		io10 |= ADV748X_IO_10_CSI4_EN;
		if (is_txa(tx)) {
			/*
			 * Output from the SD-core (480i and 576i) from the TXA
			 * interface requires reducing the number of enabled
			 * data lanes in order to guarantee a valid link
			 * frequency.
			 */
			tx->active_lanes = min(tx->num_lanes, 2U);
			io10 |= ADV748X_IO_10_CSI4_IN_SEL_AFE;
		} else {
			/* TXB has a single data lane, no need to adjust. */
			io10 |= ADV748X_IO_10_CSI1_EN;
		}
	}

	if (state->hdmi.tx) {
		/*
		 * Restore the number of active lanes, in case we have gone
		 * through an AFE->TXA streaming sessions.
		 */
		tx->active_lanes = tx->num_lanes;
		io10 |= ADV748X_IO_10_CSI4_EN;
	}

	return io_clrset(state, ADV748X_IO_10, io10_mask, io10);
}

static const struct media_entity_operations adv748x_tx_media_ops = {
	.link_setup	= adv748x_link_setup,
	.link_validate	= v4l2_subdev_link_validate,
};

static const struct media_entity_operations adv748x_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * HW setup
 */

/* Initialize CP Core with RGB888 format. */
static const struct adv748x_reg_value adv748x_init_hdmi[] = {
	/* Disable chip powerdown & Enable HDMI Rx block */
	{ADV748X_PAGE_IO, 0x00, 0x40},

	{ADV748X_PAGE_REPEATER, 0x40, 0x83}, /* Enable HDCP 1.1 */

	{ADV748X_PAGE_HDMI, 0x00, 0x08},/* Foreground Channel = A */
	{ADV748X_PAGE_HDMI, 0x98, 0xff},/* ADI Required Write */
	{ADV748X_PAGE_HDMI, 0x99, 0xa3},/* ADI Required Write */
	{ADV748X_PAGE_HDMI, 0x9a, 0x00},/* ADI Required Write */
	{ADV748X_PAGE_HDMI, 0x9b, 0x0a},/* ADI Required Write */
	{ADV748X_PAGE_HDMI, 0x9d, 0x40},/* ADI Required Write */
	{ADV748X_PAGE_HDMI, 0xcb, 0x09},/* ADI Required Write */
	{ADV748X_PAGE_HDMI, 0x3d, 0x10},/* ADI Required Write */
	{ADV748X_PAGE_HDMI, 0x3e, 0x7b},/* ADI Required Write */
	{ADV748X_PAGE_HDMI, 0x3f, 0x5e},/* ADI Required Write */
	{ADV748X_PAGE_HDMI, 0x4e, 0xfe},/* ADI Required Write */
	{ADV748X_PAGE_HDMI, 0x4f, 0x18},/* ADI Required Write */
	{ADV748X_PAGE_HDMI, 0x57, 0xa3},/* ADI Required Write */
	{ADV748X_PAGE_HDMI, 0x58, 0x04},/* ADI Required Write */
	{ADV748X_PAGE_HDMI, 0x85, 0x10},/* ADI Required Write */

	{ADV748X_PAGE_HDMI, 0x83, 0x00},/* Enable All Terminations */
	{ADV748X_PAGE_HDMI, 0xa3, 0x01},/* ADI Required Write */
	{ADV748X_PAGE_HDMI, 0xbe, 0x00},/* ADI Required Write */

	{ADV748X_PAGE_HDMI, 0x6c, 0x01},/* HPA Manual Enable */
	{ADV748X_PAGE_HDMI, 0xf8, 0x01},/* HPA Asserted */
	{ADV748X_PAGE_HDMI, 0x0f, 0x00},/* Audio Mute Speed Set to Fastest */
	/* (Smallest Step Size) */

	{ADV748X_PAGE_IO, 0x04, 0x02},	/* RGB Out of CP */
	{ADV748X_PAGE_IO, 0x12, 0xf0},	/* CSC Depends on ip Packets, SDR 444 */
	{ADV748X_PAGE_IO, 0x17, 0x80},	/* Luma & Chroma can reach 254d */
	{ADV748X_PAGE_IO, 0x03, 0x86},	/* CP-Insert_AV_Code */

	{ADV748X_PAGE_CP, 0x7c, 0x00},	/* ADI Required Write */

	{ADV748X_PAGE_IO, 0x0c, 0xe0},	/* Enable LLC_DLL & Double LLC Timing */
	{ADV748X_PAGE_IO, 0x0e, 0xdd},	/* LLC/PIX/SPI PINS TRISTATED AUD */

	{ADV748X_PAGE_EOR, 0xff, 0xff}	/* End of register table */
};

/* Initialize AFE core with YUV8 format. */
static const struct adv748x_reg_value adv748x_init_afe[] = {
	{ADV748X_PAGE_IO, 0x00, 0x30},	/* Disable chip powerdown Rx */
	{ADV748X_PAGE_IO, 0xf2, 0x01},	/* Enable I2C Read Auto-Increment */

	{ADV748X_PAGE_IO, 0x0e, 0xff},	/* LLC/PIX/AUD/SPI PINS TRISTATED */

	{ADV748X_PAGE_SDP, 0x0f, 0x00},	/* Exit Power Down Mode */
	{ADV748X_PAGE_SDP, 0x52, 0xcd},	/* ADI Required Write */

	{ADV748X_PAGE_SDP, 0x0e, 0x80},	/* ADI Required Write */
	{ADV748X_PAGE_SDP, 0x9c, 0x00},	/* ADI Required Write */
	{ADV748X_PAGE_SDP, 0x9c, 0xff},	/* ADI Required Write */
	{ADV748X_PAGE_SDP, 0x0e, 0x00},	/* ADI Required Write */

	/* ADI recommended writes for improved video quality */
	{ADV748X_PAGE_SDP, 0x80, 0x51},	/* ADI Required Write */
	{ADV748X_PAGE_SDP, 0x81, 0x51},	/* ADI Required Write */
	{ADV748X_PAGE_SDP, 0x82, 0x68},	/* ADI Required Write */

	{ADV748X_PAGE_SDP, 0x03, 0x42},	/* Tri-S Output , PwrDwn 656 pads */
	{ADV748X_PAGE_SDP, 0x04, 0xb5},	/* ITU-R BT.656-4 compatible */
	{ADV748X_PAGE_SDP, 0x13, 0x00},	/* ADI Required Write */

	{ADV748X_PAGE_SDP, 0x17, 0x41},	/* Select SH1 */
	{ADV748X_PAGE_SDP, 0x31, 0x12},	/* ADI Required Write */
	{ADV748X_PAGE_SDP, 0xe6, 0x4f},  /* V bit end pos manually in NTSC */

	{ADV748X_PAGE_EOR, 0xff, 0xff}	/* End of register table */
};

static int adv748x_sw_reset(struct adv748x_state *state)
{
	int ret;

	ret = io_write(state, ADV748X_IO_REG_FF, ADV748X_IO_REG_FF_MAIN_RESET);
	if (ret)
		return ret;

	usleep_range(5000, 6000);

	/* Disable CEC Wakeup from power-down mode */
	ret = io_clrset(state, ADV748X_IO_REG_01, ADV748X_IO_REG_01_PWRDN_MASK,
			ADV748X_IO_REG_01_PWRDNB);
	if (ret)
		return ret;

	/* Enable I2C Read Auto-Increment for consecutive reads */
	return io_write(state, ADV748X_IO_REG_F2,
			ADV748X_IO_REG_F2_READ_AUTO_INC);
}

static int adv748x_reset(struct adv748x_state *state)
{
	int ret;
	u8 regval = 0;

	ret = adv748x_sw_reset(state);
	if (ret < 0)
		return ret;

	ret = adv748x_set_slave_addresses(state);
	if (ret < 0)
		return ret;

	/* Initialize CP and AFE cores. */
	ret = adv748x_write_regs(state, adv748x_init_hdmi);
	if (ret)
		return ret;

	ret = adv748x_write_regs(state, adv748x_init_afe);
	if (ret)
		return ret;

	/* Reset TXA and TXB */
	adv748x_tx_power(&state->txa, 1);
	adv748x_tx_power(&state->txa, 0);
	adv748x_tx_power(&state->txb, 1);
	adv748x_tx_power(&state->txb, 0);

	/* Disable chip powerdown & Enable HDMI Rx block */
	io_write(state, ADV748X_IO_PD, ADV748X_IO_PD_RX_EN);

	/* Conditionally enable TXa and TXb. */
	if (is_tx_enabled(&state->txa))
		regval |= ADV748X_IO_10_CSI4_EN;
	if (is_tx_enabled(&state->txb))
		regval |= ADV748X_IO_10_CSI1_EN;
	io_write(state, ADV748X_IO_10, regval);

	/* Use vid_std and v_freq as freerun resolution for CP */
	cp_clrset(state, ADV748X_CP_CLMP_POS, ADV748X_CP_CLMP_POS_DIS_AUTO,
					      ADV748X_CP_CLMP_POS_DIS_AUTO);

	return 0;
}

static int adv748x_identify_chip(struct adv748x_state *state)
{
	int msb, lsb;

	lsb = io_read(state, ADV748X_IO_CHIP_REV_ID_1);
	msb = io_read(state, ADV748X_IO_CHIP_REV_ID_2);

	if (lsb < 0 || msb < 0) {
		adv_err(state, "Failed to read chip revision\n");
		return -EIO;
	}

	adv_info(state, "chip found @ 0x%02x revision %02x%02x\n",
		 state->client->addr << 1, lsb, msb);

	return 0;
}

/* -----------------------------------------------------------------------------
 * i2c driver
 */

void adv748x_subdev_init(struct v4l2_subdev *sd, struct adv748x_state *state,
			 const struct v4l2_subdev_ops *ops, u32 function,
			 const char *ident)
{
	v4l2_subdev_init(sd, ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* the owner is the same as the i2c_client's driver owner */
	sd->owner = state->dev->driver->owner;
	sd->dev = state->dev;

	v4l2_set_subdevdata(sd, state);

	/* initialize name */
	snprintf(sd->name, sizeof(sd->name), "%s %d-%04x %s",
		state->dev->driver->name,
		i2c_adapter_id(state->client->adapter),
		state->client->addr, ident);

	sd->entity.function = function;
	sd->entity.ops = is_tx(adv748x_sd_to_csi2(sd)) ?
			 &adv748x_tx_media_ops : &adv748x_media_ops;
}

static int adv748x_parse_csi2_lanes(struct adv748x_state *state,
				    unsigned int port,
				    struct device_node *ep)
{
	struct v4l2_fwnode_endpoint vep;
	unsigned int num_lanes;
	int ret;

	if (port != ADV748X_PORT_TXA && port != ADV748X_PORT_TXB)
		return 0;

	vep.bus_type = V4L2_MBUS_CSI2_DPHY;
	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep), &vep);
	if (ret)
		return ret;

	num_lanes = vep.bus.mipi_csi2.num_data_lanes;

	if (vep.base.port == ADV748X_PORT_TXA) {
		if (num_lanes != 1 && num_lanes != 2 && num_lanes != 4) {
			adv_err(state, "TXA: Invalid number (%u) of lanes\n",
				num_lanes);
			return -EINVAL;
		}

		state->txa.num_lanes = num_lanes;
		state->txa.active_lanes = num_lanes;
		adv_dbg(state, "TXA: using %u lanes\n", state->txa.num_lanes);
	}

	if (vep.base.port == ADV748X_PORT_TXB) {
		if (num_lanes != 1) {
			adv_err(state, "TXB: Invalid number (%u) of lanes\n",
				num_lanes);
			return -EINVAL;
		}

		state->txb.num_lanes = num_lanes;
		state->txb.active_lanes = num_lanes;
		adv_dbg(state, "TXB: using %u lanes\n", state->txb.num_lanes);
	}

	return 0;
}

static int adv748x_parse_dt(struct adv748x_state *state)
{
	struct device_node *ep_np = NULL;
	struct of_endpoint ep;
	bool out_found = false;
	bool in_found = false;
	int ret;

	for_each_endpoint_of_node(state->dev->of_node, ep_np) {
		of_graph_parse_endpoint(ep_np, &ep);
		adv_info(state, "Endpoint %pOF on port %d", ep.local_node,
			 ep.port);

		if (ep.port >= ADV748X_PORT_MAX) {
			adv_err(state, "Invalid endpoint %pOF on port %d",
				ep.local_node, ep.port);

			continue;
		}

		if (state->endpoints[ep.port]) {
			adv_err(state,
				"Multiple port endpoints are not supported");
			continue;
		}

		of_node_get(ep_np);
		state->endpoints[ep.port] = ep_np;

		/*
		 * At least one input endpoint and one output endpoint shall
		 * be defined.
		 */
		if (ep.port < ADV748X_PORT_TXA)
			in_found = true;
		else
			out_found = true;

		/* Store number of CSI-2 lanes used for TXA and TXB. */
		ret = adv748x_parse_csi2_lanes(state, ep.port, ep_np);
		if (ret)
			return ret;
	}

	return in_found && out_found ? 0 : -ENODEV;
}

static void adv748x_dt_cleanup(struct adv748x_state *state)
{
	unsigned int i;

	for (i = 0; i < ADV748X_PORT_MAX; i++)
		of_node_put(state->endpoints[i]);
}

static int adv748x_probe(struct i2c_client *client)
{
	struct adv748x_state *state;
	int ret;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;

	state = devm_kzalloc(&client->dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	mutex_init(&state->mutex);

	state->dev = &client->dev;
	state->client = client;
	state->i2c_clients[ADV748X_PAGE_IO] = client;
	i2c_set_clientdata(client, state);

	/*
	 * We can not use container_of to get back to the state with two TXs;
	 * Initialize the TXs's fields unconditionally on the endpoint
	 * presence to access them later.
	 */
	state->txa.state = state->txb.state = state;
	state->txa.page = ADV748X_PAGE_TXA;
	state->txb.page = ADV748X_PAGE_TXB;
	state->txa.port = ADV748X_PORT_TXA;
	state->txb.port = ADV748X_PORT_TXB;

	/* Discover and process ports declared by the Device tree endpoints */
	ret = adv748x_parse_dt(state);
	if (ret) {
		adv_err(state, "Failed to parse device tree");
		goto err_free_mutex;
	}

	/* Configure IO Regmap region */
	ret = adv748x_configure_regmap(state, ADV748X_PAGE_IO);
	if (ret) {
		adv_err(state, "Error configuring IO regmap region");
		goto err_cleanup_dt;
	}

	ret = adv748x_identify_chip(state);
	if (ret) {
		adv_err(state, "Failed to identify chip");
		goto err_cleanup_dt;
	}

	/* Configure remaining pages as I2C clients with regmap access */
	ret = adv748x_initialise_clients(state);
	if (ret) {
		adv_err(state, "Failed to setup client regmap pages");
		goto err_cleanup_clients;
	}

	/* SW reset ADV748X to its default values */
	ret = adv748x_reset(state);
	if (ret) {
		adv_err(state, "Failed to reset hardware");
		goto err_cleanup_clients;
	}

	/* Initialise HDMI */
	ret = adv748x_hdmi_init(&state->hdmi);
	if (ret) {
		adv_err(state, "Failed to probe HDMI");
		goto err_cleanup_clients;
	}

	/* Initialise AFE */
	ret = adv748x_afe_init(&state->afe);
	if (ret) {
		adv_err(state, "Failed to probe AFE");
		goto err_cleanup_hdmi;
	}

	/* Initialise TXA */
	ret = adv748x_csi2_init(state, &state->txa);
	if (ret) {
		adv_err(state, "Failed to probe TXA");
		goto err_cleanup_afe;
	}

	/* Initialise TXB */
	ret = adv748x_csi2_init(state, &state->txb);
	if (ret) {
		adv_err(state, "Failed to probe TXB");
		goto err_cleanup_txa;
	}

	return 0;

err_cleanup_txa:
	adv748x_csi2_cleanup(&state->txa);
err_cleanup_afe:
	adv748x_afe_cleanup(&state->afe);
err_cleanup_hdmi:
	adv748x_hdmi_cleanup(&state->hdmi);
err_cleanup_clients:
	adv748x_unregister_clients(state);
err_cleanup_dt:
	adv748x_dt_cleanup(state);
err_free_mutex:
	mutex_destroy(&state->mutex);

	return ret;
}

static int adv748x_remove(struct i2c_client *client)
{
	struct adv748x_state *state = i2c_get_clientdata(client);

	adv748x_afe_cleanup(&state->afe);
	adv748x_hdmi_cleanup(&state->hdmi);

	adv748x_csi2_cleanup(&state->txa);
	adv748x_csi2_cleanup(&state->txb);

	adv748x_unregister_clients(state);
	adv748x_dt_cleanup(state);
	mutex_destroy(&state->mutex);

	return 0;
}

static const struct of_device_id adv748x_of_table[] = {
	{ .compatible = "adi,adv7481", },
	{ .compatible = "adi,adv7482", },
	{ }
};
MODULE_DEVICE_TABLE(of, adv748x_of_table);

static struct i2c_driver adv748x_driver = {
	.driver = {
		.name = "adv748x",
		.of_match_table = adv748x_of_table,
	},
	.probe_new = adv748x_probe,
	.remove = adv748x_remove,
};

module_i2c_driver(adv748x_driver);

MODULE_AUTHOR("Kieran Bingham <kieran.bingham@ideasonboard.com>");
MODULE_DESCRIPTION("ADV748X video decoder");
MODULE_LICENSE("GPL");
