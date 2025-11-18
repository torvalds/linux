// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for the Skyworks Si3474 PoE PSE Controller
 *
 * Chip Architecture & Terminology:
 *
 * The Si3474 is a single-chip PoE PSE controller managing 8 physical power
 * delivery channels. Internally, it's structured into two logical "Quads".
 *
 * Quad 0: Manages physical channels ('ports' in datasheet) 0, 1, 2, 3
 * Quad 1: Manages physical channels ('ports' in datasheet) 4, 5, 6, 7
 *
 * Each Quad is accessed via a separate I2C address. The base address range is
 * set by hardware pins A1-A4, and the specific address selects Quad 0 (usually
 * the lower/even address) or Quad 1 (usually the higher/odd address).
 * See datasheet Table 2.2 for the address mapping.
 *
 * While the Quads manage channel-specific operations, the Si3474 package has
 * several resources shared across the entire chip:
 * - Single RESETb input pin.
 * - Single INTb output pin (signals interrupts from *either* Quad).
 * - Single OSS input pin (Emergency Shutdown).
 * - Global I2C Address (0x7F) used for firmware updates.
 * - Global status monitoring (Temperature, VDD/VPWR Undervoltage Lockout).
 *
 * Driver Architecture:
 *
 * To handle the mix of per-Quad access and shared resources correctly, this
 * driver treats the entire Si3474 package as one logical device. The driver
 * instance associated with the primary I2C address (Quad 0) takes ownership.
 * It discovers and manages the I2C client for the secondary address (Quad 1).
 * This primary instance handles shared resources like IRQ management and
 * registers a single PSE controller device representing all logical PIs.
 * Internal functions route I2C commands to the appropriate Quad's i2c_client
 * based on the target channel or PI.
 *
 * Terminology Mapping:
 *
 * - "PI" (Power Interface): Refers to the logical PSE port as defined by
 * IEEE 802.3 (typically corresponds to an RJ45 connector). This is the
 * `id` (0-7) used in the pse_controller_ops.
 * - "Channel": Refers to one of the 8 physical power control paths within
 * the Si3474 chip itself (hardware channels 0-7). This terminology is
 * used internally within the driver to avoid confusion with 'ports'.
 * - "Quad": One of the two internal 4-channel management units within the
 * Si3474, each accessed via its own I2C address.
 *
 * Relationship:
 * - A 2-Pair PoE PI uses 1 Channel.
 * - A 4-Pair PoE PI uses 2 Channels.
 *
 * ASCII Schematic:
 *
 * +-----------------------------------------------------+
 * |                    Si3474 Chip                      |
 * |                                                     |
 * | +---------------------+     +---------------------+ |
 * | |      Quad 0         |     |      Quad 1         | |
 * | | Channels 0, 1, 2, 3 |     | Channels 4, 5, 6, 7 | |
 * | +----------^----------+     +-------^-------------+ |
 * | I2C Addr 0 |                        | I2C Addr 1    |
 * |            +------------------------+               |
 * | (Primary Driver Instance) (Managed by Primary)      |
 * |                                                     |
 * | Shared Resources (affect whole chip):               |
 * |  - Single INTb Output -> Handled by Primary         |
 * |  - Single RESETb Input                              |
 * |  - Single OSS Input   -> Handled by Primary         |
 * |  - Global I2C Addr (0x7F) for Firmware Update       |
 * |  - Global Status (Temp, VDD/VPWR UVLO)              |
 * +-----------------------------------------------------+
 *        |   |   |   |        |   |   |   |
 *        Ch0 Ch1 Ch2 Ch3      Ch4 Ch5 Ch6 Ch7  (Physical Channels)
 *
 * Example Mapping (Logical PI to Physical Channel(s)):
 * * 2-Pair Mode (8 PIs):
 * PI 0 -> Ch 0
 * PI 1 -> Ch 1
 * ...
 * PI 7 -> Ch 7
 * * 4-Pair Mode (4 PIs):
 * PI 0 -> Ch 0 + Ch 1  (Managed via Quad 0 Addr)
 * PI 1 -> Ch 2 + Ch 3  (Managed via Quad 0 Addr)
 * PI 2 -> Ch 4 + Ch 5  (Managed via Quad 1 Addr)
 * PI 3 -> Ch 6 + Ch 7  (Managed via Quad 1 Addr)
 * (Note: Actual mapping depends on Device Tree and PORT_REMAP config)
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pse-pd/pse.h>

#define SI3474_MAX_CHANS 8

#define MANUFACTURER_ID 0x08
#define IC_ID 0x05
#define SI3474_DEVICE_ID (MANUFACTURER_ID << 3 | IC_ID)

/* Misc registers */
#define VENDOR_IC_ID_REG 0x1B
#define TEMPERATURE_REG 0x2C
#define FIRMWARE_REVISION_REG 0x41
#define CHIP_REVISION_REG 0x43

/* Main status registers */
#define POWER_STATUS_REG 0x10
#define PORT_MODE_REG 0x12
#define DETECT_CLASS_ENABLE_REG 0x14

/* PORTn Current */
#define PORT1_CURRENT_LSB_REG 0x30

/* PORTn Current [mA], return in [nA] */
/* 1000 * ((PORTn_CURRENT_MSB << 8) + PORTn_CURRENT_LSB) / 16384 */
#define SI3474_NA_STEP (1000 * 1000 * 1000 / 16384)

/* VPWR Voltage */
#define VPWR_LSB_REG 0x2E
#define VPWR_MSB_REG 0x2F

/* PORTn Voltage */
#define PORT1_VOLTAGE_LSB_REG 0x32

/* VPWR Voltage [V], return in [uV] */
/* 60 * (( VPWR_MSB << 8) + VPWR_LSB) / 16384 */
#define SI3474_UV_STEP (1000 * 1000 * 60 / 16384)

/* Helper macros */
#define CHAN_IDX(chan) ((chan) % 4)
#define CHAN_BIT(chan) BIT(CHAN_IDX(chan))
#define CHAN_UPPER_BIT(chan) BIT(CHAN_IDX(chan) + 4)

#define CHAN_MASK(chan) (0x03U << (2 * CHAN_IDX(chan)))
#define CHAN_REG(base, chan) ((base) + (CHAN_IDX(chan) * 4))

struct si3474_pi_desc {
	u8 chan[2];
	bool is_4p;
};

struct si3474_priv {
	struct i2c_client *client[2];
	struct pse_controller_dev pcdev;
	struct device_node *np;
	struct si3474_pi_desc pi[SI3474_MAX_CHANS];
};

static struct si3474_priv *to_si3474_priv(struct pse_controller_dev *pcdev)
{
	return container_of(pcdev, struct si3474_priv, pcdev);
}

static void si3474_get_channels(struct si3474_priv *priv, int id,
				u8 *chan0, u8 *chan1)
{
	*chan0 = priv->pi[id].chan[0];
	*chan1 = priv->pi[id].chan[1];
}

static struct i2c_client *si3474_get_chan_client(struct si3474_priv *priv,
						 u8 chan)
{
	return (chan < 4) ? priv->client[0] : priv->client[1];
}

static int si3474_pi_get_admin_state(struct pse_controller_dev *pcdev, int id,
				     struct pse_admin_state *admin_state)
{
	struct si3474_priv *priv = to_si3474_priv(pcdev);
	struct i2c_client *client;
	bool is_enabled;
	u8 chan0, chan1;
	s32 ret;

	si3474_get_channels(priv, id, &chan0, &chan1);
	client = si3474_get_chan_client(priv, chan0);

	ret = i2c_smbus_read_byte_data(client, PORT_MODE_REG);
	if (ret < 0) {
		admin_state->c33_admin_state =
			ETHTOOL_C33_PSE_ADMIN_STATE_UNKNOWN;
		return ret;
	}

	is_enabled = ret & (CHAN_MASK(chan0) | CHAN_MASK(chan1));

	if (is_enabled)
		admin_state->c33_admin_state =
			ETHTOOL_C33_PSE_ADMIN_STATE_ENABLED;
	else
		admin_state->c33_admin_state =
			ETHTOOL_C33_PSE_ADMIN_STATE_DISABLED;

	return 0;
}

static int si3474_pi_get_pw_status(struct pse_controller_dev *pcdev, int id,
				   struct pse_pw_status *pw_status)
{
	struct si3474_priv *priv = to_si3474_priv(pcdev);
	struct i2c_client *client;
	bool delivering;
	u8 chan0, chan1;
	s32 ret;

	si3474_get_channels(priv, id, &chan0, &chan1);
	client = si3474_get_chan_client(priv, chan0);

	ret = i2c_smbus_read_byte_data(client, POWER_STATUS_REG);
	if (ret < 0) {
		pw_status->c33_pw_status = ETHTOOL_C33_PSE_PW_D_STATUS_UNKNOWN;
		return ret;
	}

	delivering = ret & (CHAN_UPPER_BIT(chan0) | CHAN_UPPER_BIT(chan1));

	if (delivering)
		pw_status->c33_pw_status =
			ETHTOOL_C33_PSE_PW_D_STATUS_DELIVERING;
	else
		pw_status->c33_pw_status = ETHTOOL_C33_PSE_PW_D_STATUS_DISABLED;

	return 0;
}

static int si3474_get_of_channels(struct si3474_priv *priv)
{
	struct pse_pi *pi;
	u32 chan_id;
	u8 pi_no;
	s32 ret;

	for (pi_no = 0; pi_no < SI3474_MAX_CHANS; pi_no++) {
		pi = &priv->pcdev.pi[pi_no];
		bool pairset_found = false;
		u8 pairset_no;

		for (pairset_no = 0; pairset_no < 2; pairset_no++) {
			if (!pi->pairset[pairset_no].np)
				continue;

			pairset_found = true;

			ret = of_property_read_u32(pi->pairset[pairset_no].np,
						   "reg", &chan_id);
			if (ret) {
				dev_err(&priv->client[0]->dev,
					"Failed to read channel reg property\n");
				return ret;
			}
			if (chan_id > SI3474_MAX_CHANS) {
				dev_err(&priv->client[0]->dev,
					"Incorrect channel number: %d\n", chan_id);
				return -EINVAL;
			}

			priv->pi[pi_no].chan[pairset_no] = chan_id;
			/* Mark as 4-pair if second pairset is present */
			priv->pi[pi_no].is_4p = (pairset_no == 1);
		}

		if (pairset_found && !priv->pi[pi_no].is_4p) {
			dev_err(&priv->client[0]->dev,
				"Second pairset is missing for PI %pOF, only 4p configs are supported\n",
				pi->np);
			return -EINVAL;
		}
	}

	return 0;
}

static int si3474_setup_pi_matrix(struct pse_controller_dev *pcdev)
{
	struct si3474_priv *priv = to_si3474_priv(pcdev);
	s32 ret;

	ret = si3474_get_of_channels(priv);
	if (ret < 0)
		dev_warn(&priv->client[0]->dev,
			 "Unable to parse DT PSE power interface matrix\n");

	return ret;
}

static int si3474_pi_enable(struct pse_controller_dev *pcdev, int id)
{
	struct si3474_priv *priv = to_si3474_priv(pcdev);
	struct i2c_client *client;
	u8 chan0, chan1;
	s32 ret;
	u8 val;

	si3474_get_channels(priv, id, &chan0, &chan1);
	client = si3474_get_chan_client(priv, chan0);

	/* Release PI from shutdown */
	ret = i2c_smbus_read_byte_data(client, PORT_MODE_REG);
	if (ret < 0)
		return ret;

	val = (u8)ret;
	val |= CHAN_MASK(chan0);
	val |= CHAN_MASK(chan1);

	ret = i2c_smbus_write_byte_data(client, PORT_MODE_REG, val);
	if (ret)
		return ret;

	/* DETECT_CLASS_ENABLE must be set when using AUTO mode,
	 * otherwise PI does not power up - datasheet section 2.10.2
	 */
	val = CHAN_BIT(chan0) | CHAN_UPPER_BIT(chan0) |
	      CHAN_BIT(chan1) | CHAN_UPPER_BIT(chan1);

	ret = i2c_smbus_write_byte_data(client, DETECT_CLASS_ENABLE_REG, val);
	if (ret)
		return ret;

	return 0;
}

static int si3474_pi_disable(struct pse_controller_dev *pcdev, int id)
{
	struct si3474_priv *priv = to_si3474_priv(pcdev);
	struct i2c_client *client;
	u8 chan0, chan1;
	s32 ret;
	u8 val;

	si3474_get_channels(priv, id, &chan0, &chan1);
	client = si3474_get_chan_client(priv, chan0);

	/* Set PI in shutdown mode */
	ret = i2c_smbus_read_byte_data(client, PORT_MODE_REG);
	if (ret < 0)
		return ret;

	val = (u8)ret;
	val &= ~CHAN_MASK(chan0);
	val &= ~CHAN_MASK(chan1);

	ret = i2c_smbus_write_byte_data(client, PORT_MODE_REG, val);
	if (ret)
		return ret;

	return 0;
}

static int si3474_pi_get_chan_current(struct si3474_priv *priv, u8 chan)
{
	struct i2c_client *client;
	u64 tmp_64;
	s32 ret;
	u8 reg;

	client = si3474_get_chan_client(priv, chan);

	/* Registers 0x30 to 0x3d */
	reg = CHAN_REG(PORT1_CURRENT_LSB_REG, chan);

	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0)
		return ret;

	tmp_64 = ret * SI3474_NA_STEP;

	/* uA = nA / 1000 */
	tmp_64 = DIV_ROUND_CLOSEST_ULL(tmp_64, 1000);
	return (int)tmp_64;
}

static int si3474_pi_get_chan_voltage(struct si3474_priv *priv, u8 chan)
{
	struct i2c_client *client;
	s32 ret;
	u32 val;
	u8 reg;

	client = si3474_get_chan_client(priv, chan);

	/* Registers 0x32 to 0x3f */
	reg = CHAN_REG(PORT1_VOLTAGE_LSB_REG, chan);

	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0)
		return ret;

	val = ret * SI3474_UV_STEP;

	return (int)val;
}

static int si3474_pi_get_voltage(struct pse_controller_dev *pcdev, int id)
{
	struct si3474_priv *priv = to_si3474_priv(pcdev);
	struct i2c_client *client;
	u8 chan0, chan1;
	s32 ret;

	si3474_get_channels(priv, id, &chan0, &chan1);
	client = si3474_get_chan_client(priv, chan0);

	/* Check which channels are enabled*/
	ret = i2c_smbus_read_byte_data(client, POWER_STATUS_REG);
	if (ret < 0)
		return ret;

	/* Take voltage from the first enabled channel */
	if (ret & CHAN_BIT(chan0))
		ret = si3474_pi_get_chan_voltage(priv, chan0);
	else if (ret & CHAN_BIT(chan1))
		ret = si3474_pi_get_chan_voltage(priv, chan1);
	else
		/* 'should' be no voltage in this case */
		return 0;

	return ret;
}

static int si3474_pi_get_actual_pw(struct pse_controller_dev *pcdev, int id)
{
	struct si3474_priv *priv = to_si3474_priv(pcdev);
	u8 chan0, chan1;
	u32 uV, uA;
	u64 tmp_64;
	s32 ret;

	ret = si3474_pi_get_voltage(&priv->pcdev, id);

	/* Do not read currents if voltage is 0 */
	if (ret <= 0)
		return ret;
	uV = ret;

	si3474_get_channels(priv, id, &chan0, &chan1);

	ret = si3474_pi_get_chan_current(priv, chan0);
	if (ret < 0)
		return ret;
	uA = ret;

	ret = si3474_pi_get_chan_current(priv, chan1);
	if (ret < 0)
		return ret;
	uA += ret;

	tmp_64 = uV;
	tmp_64 *= uA;
	/* mW = uV * uA / 1000000000 */
	return DIV_ROUND_CLOSEST_ULL(tmp_64, 1000000000);
}

static const struct pse_controller_ops si3474_ops = {
	.setup_pi_matrix = si3474_setup_pi_matrix,
	.pi_enable = si3474_pi_enable,
	.pi_disable = si3474_pi_disable,
	.pi_get_actual_pw = si3474_pi_get_actual_pw,
	.pi_get_voltage = si3474_pi_get_voltage,
	.pi_get_admin_state = si3474_pi_get_admin_state,
	.pi_get_pw_status = si3474_pi_get_pw_status,
};

static void si3474_ancillary_i2c_remove(void *data)
{
	struct i2c_client *client = data;

	i2c_unregister_device(client);
}

static int si3474_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct si3474_priv *priv;
	u8 fw_version;
	s32 ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(dev, "i2c check functionality failed\n");
		return -ENXIO;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = i2c_smbus_read_byte_data(client, VENDOR_IC_ID_REG);
	if (ret < 0)
		return ret;

	if (ret != SI3474_DEVICE_ID) {
		dev_err(dev, "Wrong device ID: 0x%x\n", ret);
		return -ENXIO;
	}

	ret = i2c_smbus_read_byte_data(client, FIRMWARE_REVISION_REG);
	if (ret < 0)
		return ret;
	fw_version = ret;

	ret = i2c_smbus_read_byte_data(client, CHIP_REVISION_REG);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "Chip revision: 0x%x, firmware version: 0x%x\n",
		ret, fw_version);

	priv->client[0] = client;
	i2c_set_clientdata(client, priv);

	priv->client[1] = i2c_new_ancillary_device(priv->client[0], "secondary",
						   priv->client[0]->addr + 1);
	if (IS_ERR(priv->client[1]))
		return PTR_ERR(priv->client[1]);

	ret = devm_add_action_or_reset(dev, si3474_ancillary_i2c_remove, priv->client[1]);
	if (ret < 0) {
		dev_err(&priv->client[1]->dev, "Cannot register remove callback\n");
		return ret;
	}

	ret = i2c_smbus_read_byte_data(priv->client[1], VENDOR_IC_ID_REG);
	if (ret < 0) {
		dev_err(&priv->client[1]->dev, "Cannot access secondary PSE controller\n");
		return ret;
	}

	if (ret != SI3474_DEVICE_ID) {
		dev_err(&priv->client[1]->dev,
			"Wrong device ID for secondary PSE controller: 0x%x\n", ret);
		return -ENXIO;
	}

	priv->np = dev->of_node;
	priv->pcdev.owner = THIS_MODULE;
	priv->pcdev.ops = &si3474_ops;
	priv->pcdev.dev = dev;
	priv->pcdev.types = ETHTOOL_PSE_C33;
	priv->pcdev.nr_lines = SI3474_MAX_CHANS;

	ret = devm_pse_controller_register(dev, &priv->pcdev);
	if (ret) {
		dev_err(dev, "Failed to register PSE controller: 0x%x\n", ret);
		return ret;
	}

	return 0;
}

static const struct i2c_device_id si3474_id[] = {
	{ "si3474" },
	{}
};
MODULE_DEVICE_TABLE(i2c, si3474_id);

static const struct of_device_id si3474_of_match[] = {
	{
		.compatible = "skyworks,si3474",
	},
	{},
};
MODULE_DEVICE_TABLE(of, si3474_of_match);

static struct i2c_driver si3474_driver = {
	.probe = si3474_i2c_probe,
	.id_table = si3474_id,
	.driver = {
		.name = "si3474",
		.of_match_table = si3474_of_match,
	},
};
module_i2c_driver(si3474_driver);

MODULE_AUTHOR("Piotr Kubik <piotr.kubik@adtran.com>");
MODULE_DESCRIPTION("Skyworks Si3474 PoE PSE Controller driver");
MODULE_LICENSE("GPL");
