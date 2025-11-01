// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the TI TPS23881 PoE PSE Controller driver (I2C bus)
 *
 * Copyright (c) 2023 Bootlin, Kory Maincent <kory.maincent@bootlin.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pse-pd/pse.h>

#define TPS23881_MAX_CHANS 8
#define TPS23881_MAX_IRQ_RETRIES 10

#define TPS23881_REG_IT		0x0
#define TPS23881_REG_IT_MASK	0x1
#define TPS23881_REG_IT_DISF	BIT(2)
#define TPS23881_REG_IT_DETC	BIT(3)
#define TPS23881_REG_IT_CLASC	BIT(4)
#define TPS23881_REG_IT_IFAULT	BIT(5)
#define TPS23881_REG_IT_SUPF	BIT(7)
#define TPS23881_REG_DET_EVENT	0x5
#define TPS23881_REG_FAULT	0x7
#define TPS23881_REG_SUPF_EVENT	0xb
#define TPS23881_REG_TSD	BIT(7)
#define TPS23881_REG_DISC	0xc
#define TPS23881_REG_PW_STATUS	0x10
#define TPS23881_REG_OP_MODE	0x12
#define TPS23881_REG_DISC_EN	0x13
#define TPS23881_OP_MODE_SEMIAUTO	0xaaaa
#define TPS23881_REG_DIS_EN	0x13
#define TPS23881_REG_DET_CLA_EN	0x14
#define TPS23881_REG_GEN_MASK	0x17
#define TPS23881_REG_CLCHE	BIT(2)
#define TPS23881_REG_DECHE	BIT(3)
#define TPS23881_REG_NBITACC	BIT(5)
#define TPS23881_REG_INTEN	BIT(7)
#define TPS23881_REG_PW_EN	0x19
#define TPS23881_REG_RESET	0x1a
#define TPS23881_REG_CLRAIN	BIT(7)
#define TPS23881_REG_2PAIR_POL1	0x1e
#define TPS23881_REG_PORT_MAP	0x26
#define TPS23881_REG_PORT_POWER	0x29
#define TPS23881_REG_4PAIR_POL1	0x2a
#define TPS23881_REG_INPUT_V	0x2e
#define TPS23881_REG_CHAN1_A	0x30
#define TPS23881_REG_CHAN1_V	0x32
#define TPS23881_REG_FOLDBACK	0x40
#define TPS23881_REG_TPON	BIT(0)
#define TPS23881_REG_FWREV	0x41
#define TPS23881_REG_DEVID	0x43
#define TPS23881_REG_DEVID_MASK	0xF0
#define TPS23881_DEVICE_ID	0x02
#define TPS23881_REG_CHAN1_CLASS	0x4c
#define TPS23881_REG_SRAM_CTRL	0x60
#define TPS23881_REG_SRAM_DATA	0x61

#define TPS23881_UV_STEP	3662
#define TPS23881_NA_STEP	89500
#define TPS23881_MW_STEP	500
#define TPS23881_MIN_PI_PW_LIMIT_MW	2000

struct tps23881_port_desc {
	u8 chan[2];
	bool is_4p;
	int pw_pol;
	bool exist;
};

struct tps23881_priv {
	struct i2c_client *client;
	struct pse_controller_dev pcdev;
	struct device_node *np;
	struct tps23881_port_desc port[TPS23881_MAX_CHANS];
};

static struct tps23881_priv *to_tps23881_priv(struct pse_controller_dev *pcdev)
{
	return container_of(pcdev, struct tps23881_priv, pcdev);
}

/*
 * Helper to extract a value from a u16 register value, which is made of two
 * u8 registers. The function calculates the bit offset based on the channel
 * and extracts the relevant bits using a provided field mask.
 *
 * @param reg_val: The u16 register value (composed of two u8 registers).
 * @param chan: The channel number (0-7).
 * @param field_offset: The base bit offset to apply (e.g., 0 or 4).
 * @param field_mask: The mask to apply to extract the required bits.
 * @return: The extracted value for the specific channel.
 */
static u16 tps23881_calc_val(u16 reg_val, u8 chan, u8 field_offset,
			     u16 field_mask)
{
	if (chan >= 4)
		reg_val >>= 8;

	return (reg_val >> field_offset) & field_mask;
}

/*
 * Helper to combine individual channel values into a u16 register value.
 * The function sets the value for a specific channel in the appropriate
 * position.
 *
 * @param reg_val: The current u16 register value.
 * @param chan: The channel number (0-7).
 * @param field_offset: The base bit offset to apply (e.g., 0 or 4).
 * @param field_mask: The mask to apply for the field (e.g., 0x0F).
 * @param field_val: The value to set for the specific channel (masked by
 *                   field_mask).
 * @return: The updated u16 register value with the channel value set.
 */
static u16 tps23881_set_val(u16 reg_val, u8 chan, u8 field_offset,
			    u16 field_mask, u16 field_val)
{
	field_val &= field_mask;

	if (chan < 4) {
		reg_val &= ~(field_mask << field_offset);
		reg_val |= (field_val << field_offset);
	} else {
		reg_val &= ~(field_mask << (field_offset + 8));
		reg_val |= (field_val << (field_offset + 8));
	}

	return reg_val;
}

static int
tps23881_pi_set_pw_pol_limit(struct tps23881_priv *priv, int id, u8 pw_pol,
			     bool is_4p)
{
	struct i2c_client *client = priv->client;
	int ret, reg;
	u16 val;
	u8 chan;

	chan = priv->port[id].chan[0];
	if (!is_4p) {
		reg = TPS23881_REG_2PAIR_POL1 + (chan % 4);
	} else {
		/* One chan is enough to configure the 4p PI power limit */
		if ((chan % 4) < 2)
			reg = TPS23881_REG_4PAIR_POL1;
		else
			reg = TPS23881_REG_4PAIR_POL1 + 1;
	}

	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0)
		return ret;

	val = tps23881_set_val(ret, chan, 0, 0xff, pw_pol);
	return i2c_smbus_write_word_data(client, reg, val);
}

static int tps23881_pi_enable_manual_pol(struct tps23881_priv *priv, int id)
{
	struct i2c_client *client = priv->client;
	int ret;
	u8 chan;
	u16 val;

	ret = i2c_smbus_read_byte_data(client, TPS23881_REG_FOLDBACK);
	if (ret < 0)
		return ret;

	/* No need to test if the chan is PoE4 as setting either bit for a
	 * 4P configured port disables the automatic configuration on both
	 * channels.
	 */
	chan = priv->port[id].chan[0];
	val = tps23881_set_val(ret, chan, 0, BIT(chan % 4), BIT(chan % 4));
	return i2c_smbus_write_byte_data(client, TPS23881_REG_FOLDBACK, val);
}

static int tps23881_pi_enable(struct pse_controller_dev *pcdev, int id)
{
	struct tps23881_priv *priv = to_tps23881_priv(pcdev);
	struct i2c_client *client = priv->client;
	u8 chan;
	u16 val;
	int ret;

	if (id >= TPS23881_MAX_CHANS)
		return -ERANGE;

	chan = priv->port[id].chan[0];
	val = tps23881_set_val(0, chan, 0, BIT(chan % 4), BIT(chan % 4));

	if (priv->port[id].is_4p) {
		chan = priv->port[id].chan[1];
		val = tps23881_set_val(val, chan, 0, BIT(chan % 4),
				       BIT(chan % 4));
	}

	ret = i2c_smbus_write_word_data(client, TPS23881_REG_PW_EN, val);
	if (ret)
		return ret;

	/* Enable DC disconnect*/
	chan = priv->port[id].chan[0];
	ret = i2c_smbus_read_word_data(client, TPS23881_REG_DISC_EN);
	if (ret < 0)
		return ret;

	val = tps23881_set_val(ret, chan, 0, BIT(chan % 4), BIT(chan % 4));
	ret = i2c_smbus_write_word_data(client, TPS23881_REG_DISC_EN, val);
	if (ret)
		return ret;

	return 0;
}

static int tps23881_pi_disable(struct pse_controller_dev *pcdev, int id)
{
	struct tps23881_priv *priv = to_tps23881_priv(pcdev);
	struct i2c_client *client = priv->client;
	u8 chan;
	u16 val;
	int ret;

	if (id >= TPS23881_MAX_CHANS)
		return -ERANGE;

	chan = priv->port[id].chan[0];
	val = tps23881_set_val(0, chan, 4, BIT(chan % 4), BIT(chan % 4));

	if (priv->port[id].is_4p) {
		chan = priv->port[id].chan[1];
		val = tps23881_set_val(val, chan, 4, BIT(chan % 4),
				       BIT(chan % 4));
	}

	ret = i2c_smbus_write_word_data(client, TPS23881_REG_PW_EN, val);
	if (ret)
		return ret;

	/* PWOFF command resets lots of register which need to be
	 * configured again. According to the datasheet "It may take upwards
	 * of 5ms after PWOFFn command for all register values to be updated"
	 */
	mdelay(5);

	/* Disable DC disconnect*/
	chan = priv->port[id].chan[0];
	ret = i2c_smbus_read_word_data(client, TPS23881_REG_DISC_EN);
	if (ret < 0)
		return ret;

	val = tps23881_set_val(ret, chan, 0, 0, BIT(chan % 4));
	ret = i2c_smbus_write_word_data(client, TPS23881_REG_DISC_EN, val);
	if (ret)
		return ret;

	/* Enable detection and classification */
	ret = i2c_smbus_read_word_data(client, TPS23881_REG_DET_CLA_EN);
	if (ret < 0)
		return ret;

	chan = priv->port[id].chan[0];
	val = tps23881_set_val(ret, chan, 0, BIT(chan % 4), BIT(chan % 4));
	val = tps23881_set_val(val, chan, 4, BIT(chan % 4), BIT(chan % 4));

	if (priv->port[id].is_4p) {
		chan = priv->port[id].chan[1];
		val = tps23881_set_val(ret, chan, 0, BIT(chan % 4),
				       BIT(chan % 4));
		val = tps23881_set_val(val, chan, 4, BIT(chan % 4),
				       BIT(chan % 4));
	}

	ret = i2c_smbus_write_word_data(client, TPS23881_REG_DET_CLA_EN, val);
	if (ret)
		return ret;

	/* No power policy */
	if (priv->port[id].pw_pol < 0)
		return 0;

	ret = tps23881_pi_enable_manual_pol(priv, id);
	if (ret < 0)
		return ret;

	/* Set power policy */
	return tps23881_pi_set_pw_pol_limit(priv, id, priv->port[id].pw_pol,
					    priv->port[id].is_4p);
}

static int
tps23881_pi_get_admin_state(struct pse_controller_dev *pcdev, int id,
			    struct pse_admin_state *admin_state)
{
	struct tps23881_priv *priv = to_tps23881_priv(pcdev);
	struct i2c_client *client = priv->client;
	bool enabled;
	u8 chan;
	u16 val;
	int ret;

	ret = i2c_smbus_read_word_data(client, TPS23881_REG_PW_STATUS);
	if (ret < 0)
		return ret;

	chan = priv->port[id].chan[0];
	val = tps23881_calc_val(ret, chan, 0, BIT(chan % 4));
	enabled = !!(val);

	if (priv->port[id].is_4p) {
		chan = priv->port[id].chan[1];
		val = tps23881_calc_val(ret, chan, 0, BIT(chan % 4));
		enabled &= !!(val);
	}

	/* Return enabled status only if both channel are on this state */
	if (enabled)
		admin_state->c33_admin_state =
			ETHTOOL_C33_PSE_ADMIN_STATE_ENABLED;
	else
		admin_state->c33_admin_state =
			ETHTOOL_C33_PSE_ADMIN_STATE_DISABLED;

	return 0;
}

static int
tps23881_pi_get_pw_status(struct pse_controller_dev *pcdev, int id,
			  struct pse_pw_status *pw_status)
{
	struct tps23881_priv *priv = to_tps23881_priv(pcdev);
	struct i2c_client *client = priv->client;
	bool delivering;
	u8 chan;
	u16 val;
	int ret;

	ret = i2c_smbus_read_word_data(client, TPS23881_REG_PW_STATUS);
	if (ret < 0)
		return ret;

	chan = priv->port[id].chan[0];
	val = tps23881_calc_val(ret, chan, 4, BIT(chan % 4));
	delivering = !!(val);

	if (priv->port[id].is_4p) {
		chan = priv->port[id].chan[1];
		val = tps23881_calc_val(ret, chan, 4, BIT(chan % 4));
		delivering &= !!(val);
	}

	/* Return delivering status only if both channel are on this state */
	if (delivering)
		pw_status->c33_pw_status =
			ETHTOOL_C33_PSE_PW_D_STATUS_DELIVERING;
	else
		pw_status->c33_pw_status =
			ETHTOOL_C33_PSE_PW_D_STATUS_DISABLED;

	return 0;
}

static int tps23881_pi_get_voltage(struct pse_controller_dev *pcdev, int id)
{
	struct tps23881_priv *priv = to_tps23881_priv(pcdev);
	struct i2c_client *client = priv->client;
	int ret;
	u64 uV;

	ret = i2c_smbus_read_word_data(client, TPS23881_REG_INPUT_V);
	if (ret < 0)
		return ret;

	uV = ret & 0x3fff;
	uV *= TPS23881_UV_STEP;

	return (int)uV;
}

static int
tps23881_pi_get_chan_current(struct tps23881_priv *priv, u8 chan)
{
	struct i2c_client *client = priv->client;
	int reg, ret;
	u64 tmp_64;

	/* Registers 0x30 to 0x3d */
	reg = TPS23881_REG_CHAN1_A + (chan % 4) * 4 + (chan >= 4);
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0)
		return ret;

	tmp_64 = ret & 0x3fff;
	tmp_64 *= TPS23881_NA_STEP;
	/* uA = nA / 1000 */
	tmp_64 = DIV_ROUND_CLOSEST_ULL(tmp_64, 1000);
	return (int)tmp_64;
}

static int tps23881_pi_get_pw_class(struct pse_controller_dev *pcdev,
				    int id)
{
	struct tps23881_priv *priv = to_tps23881_priv(pcdev);
	struct i2c_client *client = priv->client;
	int ret, reg;
	u8 chan;

	chan = priv->port[id].chan[0];
	reg = TPS23881_REG_CHAN1_CLASS + (chan % 4);
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0)
		return ret;

	return tps23881_calc_val(ret, chan, 4, 0x0f);
}

static int
tps23881_pi_get_actual_pw(struct pse_controller_dev *pcdev, int id)
{
	struct tps23881_priv *priv = to_tps23881_priv(pcdev);
	int ret, uV, uA;
	u64 tmp_64;
	u8 chan;

	ret = tps23881_pi_get_voltage(&priv->pcdev, id);
	if (ret < 0)
		return ret;
	uV = ret;

	chan = priv->port[id].chan[0];
	ret = tps23881_pi_get_chan_current(priv, chan);
	if (ret < 0)
		return ret;
	uA = ret;

	if (priv->port[id].is_4p) {
		chan = priv->port[id].chan[1];
		ret = tps23881_pi_get_chan_current(priv, chan);
		if (ret < 0)
			return ret;
		uA += ret;
	}

	tmp_64 = uV;
	tmp_64 *= uA;
	/* mW = uV * uA / 1000000000 */
	return DIV_ROUND_CLOSEST_ULL(tmp_64, 1000000000);
}

static int
tps23881_pi_get_pw_limit_chan(struct tps23881_priv *priv, u8 chan)
{
	struct i2c_client *client = priv->client;
	int ret, reg;
	u16 val;

	reg = TPS23881_REG_2PAIR_POL1 + (chan % 4);
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0)
		return ret;

	val = tps23881_calc_val(ret, chan, 0, 0xff);
	return val * TPS23881_MW_STEP;
}

static int tps23881_pi_get_pw_limit(struct pse_controller_dev *pcdev, int id)
{
	struct tps23881_priv *priv = to_tps23881_priv(pcdev);
	int ret, mW;
	u8 chan;

	chan = priv->port[id].chan[0];
	ret = tps23881_pi_get_pw_limit_chan(priv, chan);
	if (ret < 0)
		return ret;

	mW = ret;
	if (priv->port[id].is_4p) {
		chan = priv->port[id].chan[1];
		ret = tps23881_pi_get_pw_limit_chan(priv, chan);
		if (ret < 0)
			return ret;
		mW += ret;
	}

	return mW;
}

static int tps23881_pi_set_pw_limit(struct pse_controller_dev *pcdev,
				    int id, int max_mW)
{
	struct tps23881_priv *priv = to_tps23881_priv(pcdev);
	u8 pw_pol;
	int ret;

	if (max_mW < TPS23881_MIN_PI_PW_LIMIT_MW || MAX_PI_PW < max_mW) {
		dev_err(&priv->client->dev,
			"power limit %d out of ranges [%d,%d]",
			max_mW, TPS23881_MIN_PI_PW_LIMIT_MW, MAX_PI_PW);
		return -ERANGE;
	}

	ret = tps23881_pi_enable_manual_pol(priv, id);
	if (ret < 0)
		return ret;

	pw_pol = DIV_ROUND_CLOSEST_ULL(max_mW, TPS23881_MW_STEP);

	/* Save power policy to reconfigure it after a disabled call */
	priv->port[id].pw_pol = pw_pol;
	return tps23881_pi_set_pw_pol_limit(priv, id, pw_pol,
					    priv->port[id].is_4p);
}

static int
tps23881_pi_get_pw_limit_ranges(struct pse_controller_dev *pcdev, int id,
				struct pse_pw_limit_ranges *pw_limit_ranges)
{
	struct ethtool_c33_pse_pw_limit_range *c33_pw_limit_ranges;

	c33_pw_limit_ranges = kzalloc(sizeof(*c33_pw_limit_ranges),
				      GFP_KERNEL);
	if (!c33_pw_limit_ranges)
		return -ENOMEM;

	c33_pw_limit_ranges->min = TPS23881_MIN_PI_PW_LIMIT_MW;
	c33_pw_limit_ranges->max = MAX_PI_PW;
	pw_limit_ranges->c33_pw_limit_ranges = c33_pw_limit_ranges;

	/* Return the number of ranges */
	return 1;
}

/* Parse managers subnode into a array of device node */
static int
tps23881_get_of_channels(struct tps23881_priv *priv,
			 struct device_node *chan_node[TPS23881_MAX_CHANS])
{
	struct device_node *channels_node, *node;
	int i, ret;

	if (!priv->np)
		return -EINVAL;

	channels_node = of_find_node_by_name(priv->np, "channels");
	if (!channels_node)
		return -EINVAL;

	for_each_child_of_node(channels_node, node) {
		u32 chan_id;

		if (!of_node_name_eq(node, "channel"))
			continue;

		ret = of_property_read_u32(node, "reg", &chan_id);
		if (ret) {
			ret = -EINVAL;
			goto out;
		}

		if (chan_id >= TPS23881_MAX_CHANS || chan_node[chan_id]) {
			dev_err(&priv->client->dev,
				"wrong number of port (%d)\n", chan_id);
			ret = -EINVAL;
			goto out;
		}

		of_node_get(node);
		chan_node[chan_id] = node;
	}

	of_node_put(channels_node);
	return 0;

out:
	for (i = 0; i < TPS23881_MAX_CHANS; i++) {
		of_node_put(chan_node[i]);
		chan_node[i] = NULL;
	}

	of_node_put(node);
	of_node_put(channels_node);
	return ret;
}

struct tps23881_port_matrix {
	u8 pi_id;
	u8 lgcl_chan[2];
	u8 hw_chan[2];
	bool is_4p;
	bool exist;
};

static int
tps23881_match_channel(const struct pse_pi_pairset *pairset,
		       struct device_node *chan_node[TPS23881_MAX_CHANS])
{
	int i;

	/* Look on every channels */
	for (i = 0; i < TPS23881_MAX_CHANS; i++) {
		if (pairset->np == chan_node[i])
			return i;
	}

	return -ENODEV;
}

static bool
tps23881_is_chan_free(struct tps23881_port_matrix port_matrix[TPS23881_MAX_CHANS],
		      int chan)
{
	int i;

	for (i = 0; i < TPS23881_MAX_CHANS; i++) {
		if (port_matrix[i].exist &&
		    (port_matrix[i].hw_chan[0] == chan ||
		    port_matrix[i].hw_chan[1] == chan))
			return false;
	}

	return true;
}

/* Fill port matrix with the matching channels */
static int
tps23881_match_port_matrix(struct pse_pi *pi, int pi_id,
			   struct device_node *chan_node[TPS23881_MAX_CHANS],
			   struct tps23881_port_matrix port_matrix[TPS23881_MAX_CHANS])
{
	int ret;

	if (!pi->pairset[0].np)
		return 0;

	ret = tps23881_match_channel(&pi->pairset[0], chan_node);
	if (ret < 0)
		return ret;

	if (!tps23881_is_chan_free(port_matrix, ret)) {
		pr_err("tps23881: channel %d already used\n", ret);
		return -ENODEV;
	}

	port_matrix[pi_id].hw_chan[0] = ret;
	port_matrix[pi_id].exist = true;

	if (!pi->pairset[1].np)
		return 0;

	ret = tps23881_match_channel(&pi->pairset[1], chan_node);
	if (ret < 0)
		return ret;

	if (!tps23881_is_chan_free(port_matrix, ret)) {
		pr_err("tps23881: channel %d already used\n", ret);
		return -ENODEV;
	}

	if (port_matrix[pi_id].hw_chan[0] / 4 != ret / 4) {
		pr_err("tps23881: 4-pair PSE can only be set within the same 4 ports group");
		return -ENODEV;
	}

	port_matrix[pi_id].hw_chan[1] = ret;
	port_matrix[pi_id].is_4p = true;

	return 0;
}

static int
tps23881_get_unused_chan(struct tps23881_port_matrix port_matrix[TPS23881_MAX_CHANS],
			 int port_cnt)
{
	bool used;
	int i, j;

	for (i = 0; i < TPS23881_MAX_CHANS; i++) {
		used = false;

		for (j = 0; j < port_cnt; j++) {
			if (port_matrix[j].hw_chan[0] == i) {
				used = true;
				break;
			}

			if (port_matrix[j].is_4p &&
			    port_matrix[j].hw_chan[1] == i) {
				used = true;
				break;
			}
		}

		if (!used)
			return i;
	}

	return -ENODEV;
}

/* Sort the port matrix to following particular hardware ports matrix
 * specification of the tps23881. The device has two 4-ports groups and
 * each 4-pair powered device has to be configured to use two consecutive
 * logical channel in each 4 ports group (1 and 2 or 3 and 4). Also the
 * hardware matrix has to be fully configured even with unused chan to be
 * valid.
 */
static int
tps23881_sort_port_matrix(struct tps23881_port_matrix port_matrix[TPS23881_MAX_CHANS])
{
	struct tps23881_port_matrix tmp_port_matrix[TPS23881_MAX_CHANS] = {0};
	int i, ret, port_cnt = 0, cnt_4ch_grp1 = 0, cnt_4ch_grp2 = 4;

	/* Configure 4p port matrix */
	for (i = 0; i < TPS23881_MAX_CHANS; i++) {
		int *cnt;

		if (!port_matrix[i].exist || !port_matrix[i].is_4p)
			continue;

		if (port_matrix[i].hw_chan[0] < 4)
			cnt = &cnt_4ch_grp1;
		else
			cnt = &cnt_4ch_grp2;

		tmp_port_matrix[port_cnt].exist = true;
		tmp_port_matrix[port_cnt].is_4p = true;
		tmp_port_matrix[port_cnt].pi_id = i;
		tmp_port_matrix[port_cnt].hw_chan[0] = port_matrix[i].hw_chan[0];
		tmp_port_matrix[port_cnt].hw_chan[1] = port_matrix[i].hw_chan[1];

		/* 4-pair ports have to be configured with consecutive
		 * logical channels 0 and 1, 2 and 3.
		 */
		tmp_port_matrix[port_cnt].lgcl_chan[0] = (*cnt)++;
		tmp_port_matrix[port_cnt].lgcl_chan[1] = (*cnt)++;

		port_cnt++;
	}

	/* Configure 2p port matrix */
	for (i = 0; i < TPS23881_MAX_CHANS; i++) {
		int *cnt;

		if (!port_matrix[i].exist || port_matrix[i].is_4p)
			continue;

		if (port_matrix[i].hw_chan[0] < 4)
			cnt = &cnt_4ch_grp1;
		else
			cnt = &cnt_4ch_grp2;

		tmp_port_matrix[port_cnt].exist = true;
		tmp_port_matrix[port_cnt].pi_id = i;
		tmp_port_matrix[port_cnt].lgcl_chan[0] = (*cnt)++;
		tmp_port_matrix[port_cnt].hw_chan[0] = port_matrix[i].hw_chan[0];

		port_cnt++;
	}

	/* Complete the rest of the first 4 port group matrix even if
	 * channels are unused
	 */
	while (cnt_4ch_grp1 < 4) {
		ret = tps23881_get_unused_chan(tmp_port_matrix, port_cnt);
		if (ret < 0) {
			pr_err("tps23881: port matrix issue, no chan available\n");
			return ret;
		}

		if (port_cnt >= TPS23881_MAX_CHANS) {
			pr_err("tps23881: wrong number of channels\n");
			return -ENODEV;
		}
		tmp_port_matrix[port_cnt].lgcl_chan[0] = cnt_4ch_grp1;
		tmp_port_matrix[port_cnt].hw_chan[0] = ret;
		cnt_4ch_grp1++;
		port_cnt++;
	}

	/* Complete the rest of the second 4 port group matrix even if
	 * channels are unused
	 */
	while (cnt_4ch_grp2 < 8) {
		ret = tps23881_get_unused_chan(tmp_port_matrix, port_cnt);
		if (ret < 0) {
			pr_err("tps23881: port matrix issue, no chan available\n");
			return -ENODEV;
		}

		if (port_cnt >= TPS23881_MAX_CHANS) {
			pr_err("tps23881: wrong number of channels\n");
			return -ENODEV;
		}
		tmp_port_matrix[port_cnt].lgcl_chan[0] = cnt_4ch_grp2;
		tmp_port_matrix[port_cnt].hw_chan[0] = ret;
		cnt_4ch_grp2++;
		port_cnt++;
	}

	memcpy(port_matrix, tmp_port_matrix, sizeof(tmp_port_matrix));

	return port_cnt;
}

/* Write port matrix to the hardware port matrix and the software port
 * matrix.
 */
static int
tps23881_write_port_matrix(struct tps23881_priv *priv,
			   struct tps23881_port_matrix port_matrix[TPS23881_MAX_CHANS],
			   int port_cnt)
{
	struct i2c_client *client = priv->client;
	u8 pi_id, lgcl_chan, hw_chan;
	u16 val = 0;
	int i;

	for (i = 0; i < port_cnt; i++) {
		pi_id = port_matrix[i].pi_id;
		lgcl_chan = port_matrix[i].lgcl_chan[0];
		hw_chan = port_matrix[i].hw_chan[0] % 4;

		/* Set software port matrix for existing ports */
		if (port_matrix[i].exist) {
			priv->port[pi_id].chan[0] = lgcl_chan;
			priv->port[pi_id].exist = true;
		}

		/* Initialize power policy internal value */
		priv->port[pi_id].pw_pol = -1;

		/* Set hardware port matrix for all ports */
		val |= hw_chan << (lgcl_chan * 2);

		if (!port_matrix[i].is_4p)
			continue;

		lgcl_chan = port_matrix[i].lgcl_chan[1];
		hw_chan = port_matrix[i].hw_chan[1] % 4;

		/* Set software port matrix for existing ports */
		if (port_matrix[i].exist) {
			priv->port[pi_id].is_4p = true;
			priv->port[pi_id].chan[1] = lgcl_chan;
		}

		/* Set hardware port matrix for all ports */
		val |= hw_chan << (lgcl_chan * 2);
	}

	/* Write hardware ports matrix */
	return i2c_smbus_write_word_data(client, TPS23881_REG_PORT_MAP, val);
}

static int
tps23881_set_ports_conf(struct tps23881_priv *priv,
			struct tps23881_port_matrix port_matrix[TPS23881_MAX_CHANS])
{
	struct i2c_client *client = priv->client;
	int i, ret;
	u16 val;

	/* Set operating mode */
	ret = i2c_smbus_write_word_data(client, TPS23881_REG_OP_MODE,
					TPS23881_OP_MODE_SEMIAUTO);
	if (ret)
		return ret;

	/* Disable DC disconnect */
	ret = i2c_smbus_write_word_data(client, TPS23881_REG_DIS_EN, 0x0);
	if (ret)
		return ret;

	/* Set port power allocation */
	val = 0;
	for (i = 0; i < TPS23881_MAX_CHANS; i++) {
		if (!port_matrix[i].exist)
			continue;

		if (port_matrix[i].is_4p)
			val |= 0xf << ((port_matrix[i].lgcl_chan[0] / 2) * 4);
		else
			val |= 0x3 << ((port_matrix[i].lgcl_chan[0] / 2) * 4);
	}
	ret = i2c_smbus_write_word_data(client, TPS23881_REG_PORT_POWER, val);
	if (ret)
		return ret;

	/* Enable detection and classification */
	val = 0;
	for (i = 0; i < TPS23881_MAX_CHANS; i++) {
		if (!port_matrix[i].exist)
			continue;

		val |= BIT(port_matrix[i].lgcl_chan[0]) |
		       BIT(port_matrix[i].lgcl_chan[0] + 4);
		if (port_matrix[i].is_4p)
			val |= BIT(port_matrix[i].lgcl_chan[1]) |
			       BIT(port_matrix[i].lgcl_chan[1] + 4);
	}
	return i2c_smbus_write_word_data(client, TPS23881_REG_DET_CLA_EN, val);
}

static int
tps23881_set_ports_matrix(struct tps23881_priv *priv,
			  struct device_node *chan_node[TPS23881_MAX_CHANS])
{
	struct tps23881_port_matrix port_matrix[TPS23881_MAX_CHANS] = {0};
	int i, ret;

	/* Update with values for every PSE PIs */
	for (i = 0; i < TPS23881_MAX_CHANS; i++) {
		ret = tps23881_match_port_matrix(&priv->pcdev.pi[i], i,
						 chan_node, port_matrix);
		if (ret)
			return ret;
	}

	ret = tps23881_sort_port_matrix(port_matrix);
	if (ret < 0)
		return ret;

	ret = tps23881_write_port_matrix(priv, port_matrix, ret);
	if (ret)
		return ret;

	return tps23881_set_ports_conf(priv, port_matrix);
}

static int tps23881_setup_pi_matrix(struct pse_controller_dev *pcdev)
{
	struct device_node *chan_node[TPS23881_MAX_CHANS] = {NULL};
	struct tps23881_priv *priv = to_tps23881_priv(pcdev);
	int ret, i;

	ret = tps23881_get_of_channels(priv, chan_node);
	if (ret < 0) {
		dev_warn(&priv->client->dev,
			 "Unable to parse port-matrix, default matrix will be used\n");
		return 0;
	}

	ret = tps23881_set_ports_matrix(priv, chan_node);

	for (i = 0; i < TPS23881_MAX_CHANS; i++)
		of_node_put(chan_node[i]);

	return ret;
}

static int tps23881_power_class_table[] = {
	-ERANGE,
	4000,
	7000,
	15500,
	30000,
	15500,
	15500,
	-ERANGE,
	45000,
	60000,
	75000,
	90000,
	15500,
	45000,
	-ERANGE,
	-ERANGE,
};

static int tps23881_pi_get_pw_req(struct pse_controller_dev *pcdev, int id)
{
	struct tps23881_priv *priv = to_tps23881_priv(pcdev);
	struct i2c_client *client = priv->client;
	u8 reg, chan;
	int ret;
	u16 val;

	/* For a 4-pair the classification need 5ms to be completed */
	if (priv->port[id].is_4p)
		mdelay(5);

	chan = priv->port[id].chan[0];
	reg = TPS23881_REG_DISC + (chan % 4);
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0)
		return ret;

	val = tps23881_calc_val(ret, chan, 4, 0xf);
	return tps23881_power_class_table[val];
}

static const struct pse_controller_ops tps23881_ops = {
	.setup_pi_matrix = tps23881_setup_pi_matrix,
	.pi_enable = tps23881_pi_enable,
	.pi_disable = tps23881_pi_disable,
	.pi_get_admin_state = tps23881_pi_get_admin_state,
	.pi_get_pw_status = tps23881_pi_get_pw_status,
	.pi_get_pw_class = tps23881_pi_get_pw_class,
	.pi_get_actual_pw = tps23881_pi_get_actual_pw,
	.pi_get_voltage = tps23881_pi_get_voltage,
	.pi_get_pw_limit = tps23881_pi_get_pw_limit,
	.pi_set_pw_limit = tps23881_pi_set_pw_limit,
	.pi_get_pw_limit_ranges = tps23881_pi_get_pw_limit_ranges,
	.pi_get_pw_req = tps23881_pi_get_pw_req,
};

static const char fw_parity_name[] = "ti/tps23881/tps23881-parity-14.bin";
static const char fw_sram_name[] = "ti/tps23881/tps23881-sram-14.bin";

struct tps23881_fw_conf {
	u8 reg;
	u8 val;
};

static const struct tps23881_fw_conf tps23881_fw_parity_conf[] = {
	{.reg = 0x60, .val = 0x01},
	{.reg = 0x62, .val = 0x00},
	{.reg = 0x63, .val = 0x80},
	{.reg = 0x60, .val = 0xC4},
	{.reg = 0x1D, .val = 0xBC},
	{.reg = 0xD7, .val = 0x02},
	{.reg = 0x91, .val = 0x00},
	{.reg = 0x90, .val = 0x00},
	{.reg = 0xD7, .val = 0x00},
	{.reg = 0x1D, .val = 0x00},
	{ /* sentinel */ }
};

static const struct tps23881_fw_conf tps23881_fw_sram_conf[] = {
	{.reg = 0x60, .val = 0xC5},
	{.reg = 0x62, .val = 0x00},
	{.reg = 0x63, .val = 0x80},
	{.reg = 0x60, .val = 0xC0},
	{.reg = 0x1D, .val = 0xBC},
	{.reg = 0xD7, .val = 0x02},
	{.reg = 0x91, .val = 0x00},
	{.reg = 0x90, .val = 0x00},
	{.reg = 0xD7, .val = 0x00},
	{.reg = 0x1D, .val = 0x00},
	{ /* sentinel */ }
};

static int tps23881_flash_sram_fw_part(struct i2c_client *client,
				       const char *fw_name,
				       const struct tps23881_fw_conf *fw_conf)
{
	const struct firmware *fw = NULL;
	int i, ret;

	ret = request_firmware(&fw, fw_name, &client->dev);
	if (ret)
		return ret;

	dev_dbg(&client->dev, "Flashing %s\n", fw_name);

	/* Prepare device for RAM download */
	while (fw_conf->reg) {
		ret = i2c_smbus_write_byte_data(client, fw_conf->reg,
						fw_conf->val);
		if (ret)
			goto out;

		fw_conf++;
	}

	/* Flash the firmware file */
	for (i = 0; i < fw->size; i++) {
		ret = i2c_smbus_write_byte_data(client,
						TPS23881_REG_SRAM_DATA,
						fw->data[i]);
		if (ret)
			goto out;
	}

out:
	release_firmware(fw);
	return ret;
}

static int tps23881_flash_sram_fw(struct i2c_client *client)
{
	int ret;

	ret = tps23881_flash_sram_fw_part(client, fw_parity_name,
					  tps23881_fw_parity_conf);
	if (ret)
		return ret;

	ret = tps23881_flash_sram_fw_part(client, fw_sram_name,
					  tps23881_fw_sram_conf);
	if (ret)
		return ret;

	ret = i2c_smbus_write_byte_data(client, TPS23881_REG_SRAM_CTRL, 0x18);
	if (ret)
		return ret;

	mdelay(12);

	return 0;
}

/* Convert interrupt events to 0xff to be aligned with the chan
 * number.
 */
static u8 tps23881_irq_export_chans_helper(u16 reg_val, u8 field_offset)
{
	u8 val;

	val = (reg_val >> (4 + field_offset) & 0xf0) |
	      (reg_val >> field_offset & 0x0f);

	return val;
}

/* Convert chan number to port number */
static void tps23881_set_notifs_helper(struct tps23881_priv *priv,
				       u8 chans,
				       unsigned long *notifs,
				       unsigned long *notifs_mask,
				       enum ethtool_pse_event event)
{
	u8 chan;
	int i;

	if (!chans)
		return;

	for (i = 0; i < TPS23881_MAX_CHANS; i++) {
		if (!priv->port[i].exist)
			continue;
		/* No need to look at the 2nd channel in case of PoE4 as
		 * both registers are set.
		 */
		chan = priv->port[i].chan[0];

		if (BIT(chan) & chans) {
			*notifs_mask |= BIT(i);
			notifs[i] |= event;
		}
	}
}

static void tps23881_irq_event_over_temp(struct tps23881_priv *priv,
					 u16 reg_val,
					 unsigned long *notifs,
					 unsigned long *notifs_mask)
{
	int i;

	if (reg_val & TPS23881_REG_TSD) {
		for (i = 0; i < TPS23881_MAX_CHANS; i++) {
			if (!priv->port[i].exist)
				continue;

			*notifs_mask |= BIT(i);
			notifs[i] |= ETHTOOL_PSE_EVENT_OVER_TEMP;
		}
	}
}

static int tps23881_irq_event_over_current(struct tps23881_priv *priv,
					   u16 reg_val,
					   unsigned long *notifs,
					   unsigned long *notifs_mask)
{
	int i, ret;
	u8 chans;

	chans = tps23881_irq_export_chans_helper(reg_val, 0);
	if (!chans)
		return 0;

	tps23881_set_notifs_helper(priv, chans, notifs, notifs_mask,
				   ETHTOOL_PSE_EVENT_OVER_CURRENT |
				   ETHTOOL_C33_PSE_EVENT_DISCONNECTION);

	/* Over Current event resets the power limit registers so we need
	 * to configured it again.
	 */
	for_each_set_bit(i, notifs_mask, priv->pcdev.nr_lines) {
		if (priv->port[i].pw_pol < 0)
			continue;

		ret = tps23881_pi_enable_manual_pol(priv, i);
		if (ret < 0)
			return ret;

		/* Set power policy */
		ret = tps23881_pi_set_pw_pol_limit(priv, i,
						   priv->port[i].pw_pol,
						   priv->port[i].is_4p);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static void tps23881_irq_event_disconnection(struct tps23881_priv *priv,
					     u16 reg_val,
					     unsigned long *notifs,
					     unsigned long *notifs_mask)
{
	u8 chans;

	chans = tps23881_irq_export_chans_helper(reg_val, 4);
	if (chans)
		tps23881_set_notifs_helper(priv, chans, notifs, notifs_mask,
					   ETHTOOL_C33_PSE_EVENT_DISCONNECTION);
}

static int tps23881_irq_event_detection(struct tps23881_priv *priv,
					u16 reg_val,
					unsigned long *notifs,
					unsigned long *notifs_mask)
{
	enum ethtool_pse_event event;
	int reg, ret, i, val;
	unsigned long chans;

	chans = tps23881_irq_export_chans_helper(reg_val, 0);
	for_each_set_bit(i, &chans, TPS23881_MAX_CHANS) {
		reg = TPS23881_REG_DISC + (i % 4);
		ret = i2c_smbus_read_word_data(priv->client, reg);
		if (ret < 0)
			return ret;

		val = tps23881_calc_val(ret, i, 0, 0xf);
		/* If detection valid */
		if (val == 0x4)
			event = ETHTOOL_C33_PSE_EVENT_DETECTION;
		else
			event = ETHTOOL_C33_PSE_EVENT_DISCONNECTION;

		tps23881_set_notifs_helper(priv, BIT(i), notifs,
					   notifs_mask, event);
	}

	return 0;
}

static int tps23881_irq_event_classification(struct tps23881_priv *priv,
					     u16 reg_val,
					     unsigned long *notifs,
					     unsigned long *notifs_mask)
{
	int reg, ret, val, i;
	unsigned long chans;

	chans = tps23881_irq_export_chans_helper(reg_val, 4);
	for_each_set_bit(i, &chans, TPS23881_MAX_CHANS) {
		reg = TPS23881_REG_DISC + (i % 4);
		ret = i2c_smbus_read_word_data(priv->client, reg);
		if (ret < 0)
			return ret;

		val = tps23881_calc_val(ret, i, 4, 0xf);
		/* Do not report classification event for unknown class */
		if (!val || val == 0x8 || val == 0xf)
			continue;

		tps23881_set_notifs_helper(priv, BIT(i), notifs,
					   notifs_mask,
					   ETHTOOL_C33_PSE_EVENT_CLASSIFICATION);
	}

	return 0;
}

static int tps23881_irq_event_handler(struct tps23881_priv *priv, u16 reg,
				      unsigned long *notifs,
				      unsigned long *notifs_mask)
{
	struct i2c_client *client = priv->client;
	int ret, val;

	/* The Supply event bit is repeated twice so we only need to read
	 * the one from the first byte.
	 */
	if (reg & TPS23881_REG_IT_SUPF) {
		ret = i2c_smbus_read_word_data(client, TPS23881_REG_SUPF_EVENT);
		if (ret < 0)
			return ret;
		tps23881_irq_event_over_temp(priv, ret, notifs, notifs_mask);
	}

	if (reg & (TPS23881_REG_IT_IFAULT | TPS23881_REG_IT_IFAULT << 8 |
		   TPS23881_REG_IT_DISF | TPS23881_REG_IT_DISF << 8)) {
		ret = i2c_smbus_read_word_data(client, TPS23881_REG_FAULT);
		if (ret < 0)
			return ret;
		ret = tps23881_irq_event_over_current(priv, ret, notifs,
						      notifs_mask);
		if (ret)
			return ret;

		tps23881_irq_event_disconnection(priv, ret, notifs, notifs_mask);
	}

	if (reg & (TPS23881_REG_IT_DETC | TPS23881_REG_IT_DETC << 8 |
		   TPS23881_REG_IT_CLASC | TPS23881_REG_IT_CLASC << 8)) {
		ret = i2c_smbus_read_word_data(client, TPS23881_REG_DET_EVENT);
		if (ret < 0)
			return ret;

		val = ret;
		ret = tps23881_irq_event_detection(priv, val, notifs,
						   notifs_mask);
		if (ret)
			return ret;

		ret = tps23881_irq_event_classification(priv, val, notifs,
							notifs_mask);
		if (ret)
			return ret;
	}
	return 0;
}

static int tps23881_irq_handler(int irq, struct pse_controller_dev *pcdev,
				unsigned long *notifs,
				unsigned long *notifs_mask)
{
	struct tps23881_priv *priv = to_tps23881_priv(pcdev);
	struct i2c_client *client = priv->client;
	int ret, it_mask, retry;

	/* Get interruption mask */
	ret = i2c_smbus_read_word_data(client, TPS23881_REG_IT_MASK);
	if (ret < 0)
		return ret;
	it_mask = ret;

	/* Read interrupt register until it frees the interruption pin. */
	retry = 0;
	while (true) {
		if (retry > TPS23881_MAX_IRQ_RETRIES) {
			dev_err(&client->dev, "interrupt never freed");
			return -ETIMEDOUT;
		}

		ret = i2c_smbus_read_word_data(client, TPS23881_REG_IT);
		if (ret < 0)
			return ret;

		/* No more relevant interruption */
		if (!(ret & it_mask))
			return 0;

		ret = tps23881_irq_event_handler(priv, (u16)ret, notifs,
						 notifs_mask);
		if (ret)
			return ret;

		retry++;
	}
	return 0;
}

static int tps23881_setup_irq(struct tps23881_priv *priv, int irq)
{
	struct i2c_client *client = priv->client;
	struct pse_irq_desc irq_desc = {
		.name = "tps23881-irq",
		.map_event = tps23881_irq_handler,
	};
	int ret;
	u16 val;

	if (!irq) {
		dev_err(&client->dev, "interrupt is missing");
		return -EINVAL;
	}

	val = TPS23881_REG_IT_IFAULT | TPS23881_REG_IT_SUPF |
	      TPS23881_REG_IT_DETC | TPS23881_REG_IT_CLASC |
	      TPS23881_REG_IT_DISF;
	val |= val << 8;
	ret = i2c_smbus_write_word_data(client, TPS23881_REG_IT_MASK, val);
	if (ret)
		return ret;

	ret = i2c_smbus_read_word_data(client, TPS23881_REG_GEN_MASK);
	if (ret < 0)
		return ret;

	val = TPS23881_REG_INTEN | TPS23881_REG_CLCHE | TPS23881_REG_DECHE;
	val |= val << 8;
	val |= (u16)ret;
	ret = i2c_smbus_write_word_data(client, TPS23881_REG_GEN_MASK, val);
	if (ret < 0)
		return ret;

	/* Reset interrupts registers */
	ret = i2c_smbus_write_word_data(client, TPS23881_REG_RESET,
					TPS23881_REG_CLRAIN);
	if (ret < 0)
		return ret;

	return devm_pse_irq_helper(&priv->pcdev, irq, 0, &irq_desc);
}

static int tps23881_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct tps23881_priv *priv;
	struct gpio_desc *reset;
	int ret;
	u8 val;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "i2c check functionality failed\n");
		return -ENXIO;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset))
		return dev_err_probe(&client->dev, PTR_ERR(reset), "Failed to get reset GPIO\n");

	if (reset) {
		/* TPS23880 datasheet (Rev G) indicates minimum reset pulse is 5us */
		usleep_range(5, 10);
		gpiod_set_value_cansleep(reset, 0); /* De-assert reset */

		/* TPS23880 datasheet indicates the minimum time after power on reset
		 * should be 20ms, but the document describing how to load SRAM ("How
		 * to Load TPS2388x SRAM and Parity Code over I2C" (Rev E))
		 * indicates we should delay that programming by at least 50ms. So
		 * we'll wait the entire 50ms here to ensure we're safe to go to the
		 * SRAM loading proceedure.
		 */
		msleep(50);
	}

	ret = i2c_smbus_read_byte_data(client, TPS23881_REG_DEVID);
	if (ret < 0)
		return ret;

	if (FIELD_GET(TPS23881_REG_DEVID_MASK, ret) != TPS23881_DEVICE_ID) {
		dev_err(dev, "Wrong device ID\n");
		return -ENXIO;
	}

	ret = tps23881_flash_sram_fw(client);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(client, TPS23881_REG_FWREV);
	if (ret < 0)
		return ret;

	dev_info(&client->dev, "Firmware revision 0x%x\n", ret);

	/* Set configuration B, 16 bit access on a single device address */
	ret = i2c_smbus_read_byte_data(client, TPS23881_REG_GEN_MASK);
	if (ret < 0)
		return ret;

	val = ret | TPS23881_REG_NBITACC;
	ret = i2c_smbus_write_byte_data(client, TPS23881_REG_GEN_MASK, val);
	if (ret)
		return ret;

	priv->client = client;
	i2c_set_clientdata(client, priv);
	priv->np = dev->of_node;

	priv->pcdev.owner = THIS_MODULE;
	priv->pcdev.ops = &tps23881_ops;
	priv->pcdev.dev = dev;
	priv->pcdev.types = ETHTOOL_PSE_C33;
	priv->pcdev.nr_lines = TPS23881_MAX_CHANS;
	priv->pcdev.supp_budget_eval_strategies = PSE_BUDGET_EVAL_STRAT_STATIC;
	ret = devm_pse_controller_register(dev, &priv->pcdev);
	if (ret) {
		return dev_err_probe(dev, ret,
				     "failed to register PSE controller\n");
	}

	ret = tps23881_setup_irq(priv, client->irq);
	if (ret)
		return ret;

	return ret;
}

static const struct i2c_device_id tps23881_id[] = {
	{ "tps23881" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tps23881_id);

static const struct of_device_id tps23881_of_match[] = {
	{ .compatible = "ti,tps23881", },
	{ },
};
MODULE_DEVICE_TABLE(of, tps23881_of_match);

static struct i2c_driver tps23881_driver = {
	.probe		= tps23881_i2c_probe,
	.id_table	= tps23881_id,
	.driver		= {
		.name		= "tps23881",
		.of_match_table = tps23881_of_match,
	},
};
module_i2c_driver(tps23881_driver);

MODULE_AUTHOR("Kory Maincent <kory.maincent@bootlin.com>");
MODULE_DESCRIPTION("TI TPS23881 PoE PSE Controller driver");
MODULE_LICENSE("GPL");
