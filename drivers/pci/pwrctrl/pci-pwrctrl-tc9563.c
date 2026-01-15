// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/pci-pwrctrl.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/unaligned.h>

#include "../pci.h"

#define TC9563_GPIO_CONFIG		0x801208
#define TC9563_RESET_GPIO		0x801210

#define TC9563_PORT_L0S_DELAY		0x82496c
#define TC9563_PORT_L1_DELAY		0x824970

#define TC9563_EMBEDDED_ETH_DELAY	0x8200d8
#define TC9563_ETH_L1_DELAY_MASK	GENMASK(27, 18)
#define TC9563_ETH_L1_DELAY_VALUE(x)	FIELD_PREP(TC9563_ETH_L1_DELAY_MASK, x)
#define TC9563_ETH_L0S_DELAY_MASK	GENMASK(17, 13)
#define TC9563_ETH_L0S_DELAY_VALUE(x)	FIELD_PREP(TC9563_ETH_L0S_DELAY_MASK, x)

#define TC9563_NFTS_2_5_GT		0x824978
#define TC9563_NFTS_5_GT		0x82497c

#define TC9563_PORT_LANE_ACCESS_ENABLE	0x828000

#define TC9563_PHY_RATE_CHANGE_OVERRIDE	0x828040
#define TC9563_PHY_RATE_CHANGE		0x828050

#define TC9563_TX_MARGIN		0x828234

#define TC9563_DFE_ENABLE		0x828a04
#define TC9563_DFE_EQ0_MODE		0x828a08
#define TC9563_DFE_EQ1_MODE		0x828a0c
#define TC9563_DFE_EQ2_MODE		0x828a14
#define TC9563_DFE_PD_MASK		0x828254

#define TC9563_PORT_SELECT		0x82c02c
#define TC9563_PORT_ACCESS_ENABLE	0x82c030

#define TC9563_POWER_CONTROL		0x82b09c
#define TC9563_POWER_CONTROL_OVREN	0x82b2c8

#define TC9563_GPIO_MASK		0xfffffff3
#define TC9563_GPIO_DEASSERT_BITS	0xc  /* Clear to deassert GPIO */

#define TC9563_TX_MARGIN_MIN_UA		400000

/*
 * From TC9563 PORSYS rev 0.2, figure 1.1 POR boot sequence
 * wait for 10ms for the internal osc frequency to stabilize.
 */
#define TC9563_OSC_STAB_DELAY_US	(10 * USEC_PER_MSEC)

#define TC9563_L0S_L1_DELAY_UNIT_NS	256  /* Each unit represents 256 ns */

struct tc9563_pwrctrl_reg_setting {
	unsigned int offset;
	unsigned int val;
};

enum tc9563_pwrctrl_ports {
	TC9563_USP,
	TC9563_DSP1,
	TC9563_DSP2,
	TC9563_DSP3,
	TC9563_ETHERNET,
	TC9563_MAX
};

struct tc9563_pwrctrl_cfg {
	u32 l0s_delay;
	u32 l1_delay;
	u32 tx_amp;
	u8 nfts[2]; /* GEN1 & GEN2 */
	bool disable_dfe;
	bool disable_port;
};

#define TC9563_PWRCTL_MAX_SUPPLY	6

static const char *const tc9563_supply_names[TC9563_PWRCTL_MAX_SUPPLY] = {
	"vddc",
	"vdd18",
	"vdd09",
	"vddio1",
	"vddio2",
	"vddio18",
};

struct tc9563_pwrctrl {
	struct pci_pwrctrl pwrctrl;
	struct regulator_bulk_data supplies[TC9563_PWRCTL_MAX_SUPPLY];
	struct tc9563_pwrctrl_cfg cfg[TC9563_MAX];
	struct gpio_desc *reset_gpio;
	struct i2c_adapter *adapter;
	struct i2c_client *client;
};

/*
 * downstream port power off sequence, hardcoding the address
 * as we don't know register names for these register offsets.
 */
static const struct tc9563_pwrctrl_reg_setting common_pwroff_seq[] = {
	{0x82900c, 0x1},
	{0x829010, 0x1},
	{0x829018, 0x0},
	{0x829020, 0x1},
	{0x82902c, 0x1},
	{0x829030, 0x1},
	{0x82903c, 0x1},
	{0x829058, 0x0},
	{0x82905c, 0x1},
	{0x829060, 0x1},
	{0x8290cc, 0x1},
	{0x8290d0, 0x1},
	{0x8290d8, 0x1},
	{0x8290e0, 0x1},
	{0x8290e8, 0x1},
	{0x8290ec, 0x1},
	{0x8290f4, 0x1},
	{0x82910c, 0x1},
	{0x829110, 0x1},
	{0x829114, 0x1},
};

static const struct tc9563_pwrctrl_reg_setting dsp1_pwroff_seq[] = {
	{TC9563_PORT_ACCESS_ENABLE, 0x2},
	{TC9563_PORT_LANE_ACCESS_ENABLE, 0x3},
	{TC9563_POWER_CONTROL, 0x014f4804},
	{TC9563_POWER_CONTROL_OVREN, 0x1},
	{TC9563_PORT_ACCESS_ENABLE, 0x4},
};

static const struct tc9563_pwrctrl_reg_setting dsp2_pwroff_seq[] = {
	{TC9563_PORT_ACCESS_ENABLE, 0x8},
	{TC9563_PORT_LANE_ACCESS_ENABLE, 0x1},
	{TC9563_POWER_CONTROL, 0x014f4804},
	{TC9563_POWER_CONTROL_OVREN, 0x1},
	{TC9563_PORT_ACCESS_ENABLE, 0x8},
};

/*
 * Since all transfers are initiated by the probe, no locks are necessary,
 * as there are no concurrent calls.
 */
static int tc9563_pwrctrl_i2c_write(struct i2c_client *client,
				    u32 reg_addr, u32 reg_val)
{
	struct i2c_msg msg;
	u8 msg_buf[7];
	int ret;

	msg.addr = client->addr;
	msg.len = 7;
	msg.flags = 0;

	/* Big Endian for reg addr */
	put_unaligned_be24(reg_addr, &msg_buf[0]);

	/* Little Endian for reg val */
	put_unaligned_le32(reg_val, &msg_buf[3]);

	msg.buf = msg_buf;
	ret = i2c_transfer(client->adapter, &msg, 1);
	return ret == 1 ? 0 : ret;
}

static int tc9563_pwrctrl_i2c_read(struct i2c_client *client,
				   u32 reg_addr, u32 *reg_val)
{
	struct i2c_msg msg[2];
	u8 wr_data[3];
	u32 rd_data;
	int ret;

	msg[0].addr = client->addr;
	msg[0].len = 3;
	msg[0].flags = 0;

	/* Big Endian for reg addr */
	put_unaligned_be24(reg_addr, &wr_data[0]);

	msg[0].buf = wr_data;

	msg[1].addr = client->addr;
	msg[1].len = 4;
	msg[1].flags = I2C_M_RD;

	msg[1].buf = (u8 *)&rd_data;

	ret = i2c_transfer(client->adapter, &msg[0], 2);
	if (ret == 2) {
		*reg_val = get_unaligned_le32(&rd_data);
		return 0;
	}

	/* If only one message successfully completed, return -EIO */
	return ret == 1 ? -EIO : ret;
}

static int tc9563_pwrctrl_i2c_bulk_write(struct i2c_client *client,
				const struct tc9563_pwrctrl_reg_setting *seq,
				int len)
{
	int ret, i;

	for (i = 0; i < len; i++) {
		ret = tc9563_pwrctrl_i2c_write(client, seq[i].offset, seq[i].val);
		if (ret)
			return ret;
	}

	return 0;
}

static int tc9563_pwrctrl_disable_port(struct tc9563_pwrctrl *tc9563,
				       enum tc9563_pwrctrl_ports port)
{
	struct tc9563_pwrctrl_cfg *cfg = &tc9563->cfg[port];
	const struct tc9563_pwrctrl_reg_setting *seq;
	int ret, len;

	if (!cfg->disable_port)
		return 0;

	if (port == TC9563_DSP1) {
		seq = dsp1_pwroff_seq;
		len = ARRAY_SIZE(dsp1_pwroff_seq);
	} else {
		seq = dsp2_pwroff_seq;
		len = ARRAY_SIZE(dsp2_pwroff_seq);
	}

	ret = tc9563_pwrctrl_i2c_bulk_write(tc9563->client, seq, len);
	if (ret)
		return ret;

	return tc9563_pwrctrl_i2c_bulk_write(tc9563->client, common_pwroff_seq,
					     ARRAY_SIZE(common_pwroff_seq));
}

static int tc9563_pwrctrl_set_l0s_l1_entry_delay(struct tc9563_pwrctrl *tc9563,
						 enum tc9563_pwrctrl_ports port,
						 bool is_l1, u32 ns)
{
	u32 rd_val, units;
	int ret;

	if (ns < TC9563_L0S_L1_DELAY_UNIT_NS)
		return 0;

	/* convert to units of 256ns */
	units = ns / TC9563_L0S_L1_DELAY_UNIT_NS;

	if (port == TC9563_ETHERNET) {
		ret = tc9563_pwrctrl_i2c_read(tc9563->client,
					      TC9563_EMBEDDED_ETH_DELAY,
					      &rd_val);
		if (ret)
			return ret;

		if (is_l1)
			rd_val = u32_replace_bits(rd_val, units,
						  TC9563_ETH_L1_DELAY_MASK);
		else
			rd_val = u32_replace_bits(rd_val, units,
						  TC9563_ETH_L0S_DELAY_MASK);

		return tc9563_pwrctrl_i2c_write(tc9563->client,
						TC9563_EMBEDDED_ETH_DELAY,
						rd_val);
	}

	ret = tc9563_pwrctrl_i2c_write(tc9563->client, TC9563_PORT_SELECT,
				       BIT(port));
	if (ret)
		return ret;

	return tc9563_pwrctrl_i2c_write(tc9563->client,
			is_l1 ? TC9563_PORT_L1_DELAY : TC9563_PORT_L0S_DELAY,
			units);
}

static int tc9563_pwrctrl_set_tx_amplitude(struct tc9563_pwrctrl *tc9563,
					   enum tc9563_pwrctrl_ports port)
{
	u32 amp = tc9563->cfg[port].tx_amp;
	int port_access;

	if (amp < TC9563_TX_MARGIN_MIN_UA)
		return 0;

	/* txmargin = (Amp(uV) - 400000) / 3125 */
	amp = (amp - TC9563_TX_MARGIN_MIN_UA) / 3125;

	switch (port) {
	case TC9563_USP:
		port_access = 0x1;
		break;
	case TC9563_DSP1:
		port_access = 0x2;
		break;
	case TC9563_DSP2:
		port_access = 0x8;
		break;
	default:
		return -EINVAL;
	}

	struct tc9563_pwrctrl_reg_setting tx_amp_seq[] = {
		{TC9563_PORT_ACCESS_ENABLE, port_access},
		{TC9563_PORT_LANE_ACCESS_ENABLE, 0x3},
		{TC9563_TX_MARGIN, amp},
	};

	return tc9563_pwrctrl_i2c_bulk_write(tc9563->client, tx_amp_seq,
					     ARRAY_SIZE(tx_amp_seq));
}

static int tc9563_pwrctrl_disable_dfe(struct tc9563_pwrctrl *tc9563,
				      enum tc9563_pwrctrl_ports port)
{
	struct tc9563_pwrctrl_cfg *cfg = &tc9563->cfg[port];
	int port_access, lane_access = 0x3;
	u32 phy_rate = 0x21;

	if (!cfg->disable_dfe)
		return 0;

	switch (port) {
	case TC9563_USP:
		phy_rate = 0x1;
		port_access = 0x1;
		break;
	case TC9563_DSP1:
		port_access = 0x2;
		break;
	case TC9563_DSP2:
		port_access = 0x8;
		lane_access = 0x1;
		break;
	default:
		return -EINVAL;
	}

	struct tc9563_pwrctrl_reg_setting disable_dfe_seq[] = {
		{TC9563_PORT_ACCESS_ENABLE, port_access},
		{TC9563_PORT_LANE_ACCESS_ENABLE, lane_access},
		{TC9563_DFE_ENABLE, 0x0},
		{TC9563_DFE_EQ0_MODE, 0x411},
		{TC9563_DFE_EQ1_MODE, 0x11},
		{TC9563_DFE_EQ2_MODE, 0x11},
		{TC9563_DFE_PD_MASK, 0x7},
		{TC9563_PHY_RATE_CHANGE_OVERRIDE, 0x10},
		{TC9563_PHY_RATE_CHANGE, phy_rate},
		{TC9563_PHY_RATE_CHANGE, 0x0},
		{TC9563_PHY_RATE_CHANGE_OVERRIDE, 0x0},
	};

	return tc9563_pwrctrl_i2c_bulk_write(tc9563->client, disable_dfe_seq,
					     ARRAY_SIZE(disable_dfe_seq));
}

static int tc9563_pwrctrl_set_nfts(struct tc9563_pwrctrl *tc9563,
				   enum tc9563_pwrctrl_ports port)
{
	u8 *nfts = tc9563->cfg[port].nfts;
	struct tc9563_pwrctrl_reg_setting nfts_seq[] = {
		{TC9563_NFTS_2_5_GT, nfts[0]},
		{TC9563_NFTS_5_GT, nfts[1]},
	};
	int ret;

	if (!nfts[0])
		return 0;

	ret =  tc9563_pwrctrl_i2c_write(tc9563->client, TC9563_PORT_SELECT,
					BIT(port));
	if (ret)
		return ret;

	return tc9563_pwrctrl_i2c_bulk_write(tc9563->client, nfts_seq,
					     ARRAY_SIZE(nfts_seq));
}

static int tc9563_pwrctrl_assert_deassert_reset(struct tc9563_pwrctrl *tc9563,
						bool deassert)
{
	int ret, val;

	ret = tc9563_pwrctrl_i2c_write(tc9563->client, TC9563_GPIO_CONFIG,
				       TC9563_GPIO_MASK);
	if (ret)
		return ret;

	val = deassert ? TC9563_GPIO_DEASSERT_BITS : 0;

	return tc9563_pwrctrl_i2c_write(tc9563->client, TC9563_RESET_GPIO, val);
}

static int tc9563_pwrctrl_parse_device_dt(struct tc9563_pwrctrl *tc9563,
					  struct device_node *node,
					  enum tc9563_pwrctrl_ports port)
{
	struct tc9563_pwrctrl_cfg *cfg = &tc9563->cfg[port];
	int ret;

	/* Disable port if the status of the port is disabled. */
	if (!of_device_is_available(node)) {
		cfg->disable_port = true;
		return 0;
	}

	ret = of_property_read_u32(node, "aspm-l0s-entry-delay-ns", &cfg->l0s_delay);
	if (ret && ret != -EINVAL)
		return ret;

	ret = of_property_read_u32(node, "aspm-l1-entry-delay-ns", &cfg->l1_delay);
	if (ret && ret != -EINVAL)
		return ret;

	ret = of_property_read_u32(node, "toshiba,tx-amplitude-microvolt", &cfg->tx_amp);
	if (ret && ret != -EINVAL)
		return ret;

	ret = of_property_read_u8_array(node, "n-fts", cfg->nfts, ARRAY_SIZE(cfg->nfts));
	if (ret && ret != -EINVAL)
		return ret;

	cfg->disable_dfe = of_property_read_bool(node, "toshiba,no-dfe-support");

	return 0;
}

static int tc9563_pwrctrl_power_off(struct pci_pwrctrl *pwrctrl)
{
	struct tc9563_pwrctrl *tc9563 = container_of(pwrctrl,
					    struct tc9563_pwrctrl, pwrctrl);

	gpiod_set_value(tc9563->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(tc9563->supplies), tc9563->supplies);

	return 0;
}

static int tc9563_pwrctrl_power_on(struct pci_pwrctrl *pwrctrl)
{
	struct tc9563_pwrctrl *tc9563 = container_of(pwrctrl,
					    struct tc9563_pwrctrl, pwrctrl);
	struct device *dev = tc9563->pwrctrl.dev;
	struct tc9563_pwrctrl_cfg *cfg;
	int ret, i;

	ret = regulator_bulk_enable(ARRAY_SIZE(tc9563->supplies),
				    tc9563->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "cannot enable regulators\n");

	gpiod_set_value(tc9563->reset_gpio, 0);

	fsleep(TC9563_OSC_STAB_DELAY_US);

	ret = tc9563_pwrctrl_assert_deassert_reset(tc9563, false);
	if (ret)
		goto power_off;

	for (i = 0; i < TC9563_MAX; i++) {
		cfg = &tc9563->cfg[i];
		ret = tc9563_pwrctrl_disable_port(tc9563, i);
		if (ret) {
			dev_err(dev, "Disabling port failed\n");
			goto power_off;
		}

		ret = tc9563_pwrctrl_set_l0s_l1_entry_delay(tc9563, i, false, cfg->l0s_delay);
		if (ret) {
			dev_err(dev, "Setting L0s entry delay failed\n");
			goto power_off;
		}

		ret = tc9563_pwrctrl_set_l0s_l1_entry_delay(tc9563, i, true, cfg->l1_delay);
		if (ret) {
			dev_err(dev, "Setting L1 entry delay failed\n");
			goto power_off;
		}

		ret = tc9563_pwrctrl_set_tx_amplitude(tc9563, i);
		if (ret) {
			dev_err(dev, "Setting Tx amplitude failed\n");
			goto power_off;
		}

		ret = tc9563_pwrctrl_set_nfts(tc9563, i);
		if (ret) {
			dev_err(dev, "Setting N_FTS failed\n");
			goto power_off;
		}

		ret = tc9563_pwrctrl_disable_dfe(tc9563, i);
		if (ret) {
			dev_err(dev, "Disabling DFE failed\n");
			goto power_off;
		}
	}

	ret = tc9563_pwrctrl_assert_deassert_reset(tc9563, true);
	if (!ret)
		return 0;

power_off:
	tc9563_pwrctrl_power_off(&tc9563->pwrctrl);
	return ret;
}

static int tc9563_pwrctrl_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	enum tc9563_pwrctrl_ports port;
	struct tc9563_pwrctrl *tc9563;
	struct device_node *i2c_node;
	int ret, addr;

	tc9563 = devm_kzalloc(dev, sizeof(*tc9563), GFP_KERNEL);
	if (!tc9563)
		return -ENOMEM;

	ret = of_property_read_u32_index(node, "i2c-parent", 1, &addr);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read i2c-parent property\n");

	i2c_node = of_parse_phandle(dev->of_node, "i2c-parent", 0);
	tc9563->adapter = of_find_i2c_adapter_by_node(i2c_node);
	of_node_put(i2c_node);
	if (!tc9563->adapter)
		return dev_err_probe(dev, -EPROBE_DEFER, "Failed to find I2C adapter\n");

	tc9563->client = i2c_new_dummy_device(tc9563->adapter, addr);
	if (IS_ERR(tc9563->client)) {
		dev_err(dev, "Failed to create I2C client\n");
		put_device(&tc9563->adapter->dev);
		return PTR_ERR(tc9563->client);
	}

	for (int i = 0; i < ARRAY_SIZE(tc9563_supply_names); i++)
		tc9563->supplies[i].supply = tc9563_supply_names[i];

	ret = devm_regulator_bulk_get(dev, TC9563_PWRCTL_MAX_SUPPLY,
				      tc9563->supplies);
	if (ret) {
		dev_err_probe(dev, ret, "failed to get supply regulator\n");
		goto remove_i2c;
	}

	tc9563->reset_gpio = devm_gpiod_get(dev, "resx", GPIOD_OUT_HIGH);
	if (IS_ERR(tc9563->reset_gpio)) {
		ret = dev_err_probe(dev, PTR_ERR(tc9563->reset_gpio), "failed to get resx GPIO\n");
		goto remove_i2c;
	}

	pci_pwrctrl_init(&tc9563->pwrctrl, dev);

	port = TC9563_USP;
	ret = tc9563_pwrctrl_parse_device_dt(tc9563, node, port);
	if (ret) {
		dev_err(dev, "failed to parse device tree properties: %d\n", ret);
		goto remove_i2c;
	}

	/*
	 * Downstream ports are always children of the upstream port.
	 * The first node represents DSP1, the second node represents DSP2,
	 * and so on.
	 */
	for_each_child_of_node_scoped(node, child) {
		port++;
		ret = tc9563_pwrctrl_parse_device_dt(tc9563, child, port);
		if (ret)
			break;
		/* Embedded ethernet device are under DSP3 */
		if (port == TC9563_DSP3) {
			for_each_child_of_node_scoped(child, child1) {
				port++;
				ret = tc9563_pwrctrl_parse_device_dt(tc9563,
								child1, port);
				if (ret)
					break;
			}
		}
	}
	if (ret) {
		dev_err(dev, "failed to parse device tree properties: %d\n", ret);
		goto remove_i2c;
	}

	tc9563->pwrctrl.power_on = tc9563_pwrctrl_power_on;
	tc9563->pwrctrl.power_off = tc9563_pwrctrl_power_off;

	ret = devm_pci_pwrctrl_device_set_ready(dev, &tc9563->pwrctrl);
	if (ret)
		goto power_off;

	return 0;

power_off:
	tc9563_pwrctrl_power_off(&tc9563->pwrctrl);
remove_i2c:
	i2c_unregister_device(tc9563->client);
	put_device(&tc9563->adapter->dev);
	return ret;
}

static void tc9563_pwrctrl_remove(struct platform_device *pdev)
{
	struct pci_pwrctrl *pwrctrl = dev_get_drvdata(&pdev->dev);
	struct tc9563_pwrctrl *tc9563 = container_of(pwrctrl,
					struct tc9563_pwrctrl, pwrctrl);

	tc9563_pwrctrl_power_off(&tc9563->pwrctrl);
	i2c_unregister_device(tc9563->client);
	put_device(&tc9563->adapter->dev);
}

static const struct of_device_id tc9563_pwrctrl_of_match[] = {
	{ .compatible = "pci1179,0623"},
	{ }
};
MODULE_DEVICE_TABLE(of, tc9563_pwrctrl_of_match);

static struct platform_driver tc9563_pwrctrl_driver = {
	.driver = {
		.name = "pwrctrl-tc9563",
		.of_match_table = tc9563_pwrctrl_of_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = tc9563_pwrctrl_probe,
	.remove = tc9563_pwrctrl_remove,
};
module_platform_driver(tc9563_pwrctrl_driver);

MODULE_AUTHOR("Krishna chaitanya chundru <quic_krichai@quicinc.com>");
MODULE_DESCRIPTION("TC956x power control driver");
MODULE_LICENSE("GPL");
