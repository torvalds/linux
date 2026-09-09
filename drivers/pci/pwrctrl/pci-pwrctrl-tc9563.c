// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/array_size.h>
#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/pci-pwrctrl.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/soc/qcom/tc9563.h>
#include <linux/string.h>
#include <linux/types.h>

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

enum tc9563_pwrctrl_ports {
	TC9563_USP,
	TC9563_DSP1,
	TC9563_DSP2,
	TC9563_VDSP,
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
	struct tc9563_pwrctrl_cfg ep_cfg;
	struct gpio_desc *reset_gpio;
	struct i2c_adapter *adapter;
	struct regmap *regmap;
};

/*
 * downstream port power off sequence, hardcoding the address
 * as we don't know register names for these register offsets.
 */
static const struct reg_sequence common_pwroff_seq[] = {
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

static const struct reg_sequence dsp1_pwroff_seq[] = {
	{TC9563_PORT_ACCESS_ENABLE, 0x2},
	{TC9563_PORT_LANE_ACCESS_ENABLE, 0x3},
	{TC9563_POWER_CONTROL, 0x014f4804},
	{TC9563_POWER_CONTROL_OVREN, 0x1},
	{TC9563_PORT_ACCESS_ENABLE, 0x4},
};

static const struct reg_sequence dsp2_pwroff_seq[] = {
	{TC9563_PORT_ACCESS_ENABLE, 0x8},
	{TC9563_PORT_LANE_ACCESS_ENABLE, 0x1},
	{TC9563_POWER_CONTROL, 0x014f4804},
	{TC9563_POWER_CONTROL_OVREN, 0x1},
	{TC9563_PORT_ACCESS_ENABLE, 0x8},
};

static DEFINE_IDA(tc9563_pwrctrl_ida);

static int tc9563_pwrctrl_disable_port(struct tc9563_pwrctrl *tc9563,
				       enum tc9563_pwrctrl_ports port)
{
	struct tc9563_pwrctrl_cfg *cfg = &tc9563->cfg[port];
	const struct reg_sequence *seq;
	int ret, len;

	if (!cfg->disable_port)
		return 0;

	switch (port) {
	case TC9563_DSP1:
		seq = dsp1_pwroff_seq;
		len = ARRAY_SIZE(dsp1_pwroff_seq);
		break;
	case TC9563_DSP2:
		seq = dsp2_pwroff_seq;
		len = ARRAY_SIZE(dsp2_pwroff_seq);
		break;
	default:
		/* Only external downstream ports DSP1/DSP2 can be powered off */
		return 0;
	}

	ret = regmap_multi_reg_write(tc9563->regmap, seq, len);
	if (ret)
		return ret;

	return regmap_multi_reg_write(tc9563->regmap, common_pwroff_seq,
				      ARRAY_SIZE(common_pwroff_seq));
}

static int tc9563_pwrctrl_set_port_l0s_l1_entry_delay(struct tc9563_pwrctrl *tc9563,
						      enum tc9563_pwrctrl_ports port,
						      bool is_l1, u32 ns)
{
	u32 units;
	int ret;

	if (ns < TC9563_L0S_L1_DELAY_UNIT_NS)
		return 0;

	/* convert to units of 256ns */
	units = ns / TC9563_L0S_L1_DELAY_UNIT_NS;

	ret = regmap_write(tc9563->regmap, TC9563_PORT_SELECT, BIT(port));
	if (ret)
		return ret;

	return regmap_write(tc9563->regmap,
			    is_l1 ? TC9563_PORT_L1_DELAY : TC9563_PORT_L0S_DELAY,
			    units);
}

static int tc9563_pwrctrl_set_eth_l0s_l1_entry_delay(struct tc9563_pwrctrl *tc9563,
						     bool is_l1, u32 ns)
{
	u32 rd_val, units;
	int ret;

	if (ns < TC9563_L0S_L1_DELAY_UNIT_NS)
		return 0;

	/* convert to units of 256ns */
	units = ns / TC9563_L0S_L1_DELAY_UNIT_NS;

	ret = regmap_read(tc9563->regmap, TC9563_EMBEDDED_ETH_DELAY, &rd_val);
	if (ret)
		return ret;

	if (is_l1)
		rd_val = u32_replace_bits(rd_val, units,
					  TC9563_ETH_L1_DELAY_MASK);
	else
		rd_val = u32_replace_bits(rd_val, units,
					  TC9563_ETH_L0S_DELAY_MASK);

	return regmap_write(tc9563->regmap, TC9563_EMBEDDED_ETH_DELAY, rd_val);
}

static int tc9563_pwrctrl_set_tx_amplitude(struct tc9563_pwrctrl *tc9563,
					   enum tc9563_pwrctrl_ports port)
{
	struct device *dev = tc9563->pwrctrl.dev;
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
	case TC9563_VDSP:
		dev_dbg(dev, "Tx amplitude tuning not supported for VDSP\n");
		return 0;
	default:
		return -EINVAL;
	}

	struct reg_sequence tx_amp_seq[] = {
		{TC9563_PORT_ACCESS_ENABLE, port_access},
		{TC9563_PORT_LANE_ACCESS_ENABLE, 0x3},
		{TC9563_TX_MARGIN, amp},
	};

	return regmap_multi_reg_write(tc9563->regmap, tx_amp_seq,
				      ARRAY_SIZE(tx_amp_seq));
}

static int tc9563_pwrctrl_disable_dfe(struct tc9563_pwrctrl *tc9563,
				      enum tc9563_pwrctrl_ports port)
{
	struct tc9563_pwrctrl_cfg *cfg = &tc9563->cfg[port];
	struct device *dev = tc9563->pwrctrl.dev;
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
	case TC9563_VDSP:
		dev_dbg(dev, "DFE tuning not supported for VDSP\n");
		return 0;
	default:
		return -EINVAL;
	}

	struct reg_sequence disable_dfe_seq[] = {
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

	return regmap_multi_reg_write(tc9563->regmap, disable_dfe_seq,
				      ARRAY_SIZE(disable_dfe_seq));
}

static int tc9563_pwrctrl_set_nfts(struct tc9563_pwrctrl *tc9563,
				   enum tc9563_pwrctrl_ports port)
{
	u8 *nfts = tc9563->cfg[port].nfts;
	struct reg_sequence nfts_seq[] = {
		{TC9563_NFTS_2_5_GT, nfts[0]},
		{TC9563_NFTS_5_GT, nfts[1]},
	};
	struct device *dev = tc9563->pwrctrl.dev;
	int ret;

	if (!nfts[0])
		return 0;

	if (port == TC9563_VDSP) {
		dev_dbg(dev, "N_FTS tuning not supported for VDSP\n");
		return 0;
	}

	ret =  regmap_write(tc9563->regmap, TC9563_PORT_SELECT, BIT(port));
	if (ret)
		return ret;

	return regmap_multi_reg_write(tc9563->regmap, nfts_seq,
				      ARRAY_SIZE(nfts_seq));
}

static int tc9563_pwrctrl_assert_deassert_reset(struct tc9563_pwrctrl *tc9563,
						bool deassert)
{
	int ret, val;

	ret = regmap_write(tc9563->regmap, TC9563_GPIO_CONFIG,
			   TC9563_GPIO_MASK);
	if (ret)
		return ret;

	val = deassert ? TC9563_GPIO_DEASSERT_BITS : 0;

	return regmap_write(tc9563->regmap, TC9563_RESET_GPIO, val);
}

static int tc9563_pwrctrl_parse_device_dt(struct device_node *node,
					  struct tc9563_pwrctrl_cfg *cfg)
{
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

static void tc9563_pwrctrl_adev_release(struct device *dev)
{
	struct auxiliary_device *adev = to_auxiliary_dev(dev);

	ida_free(&tc9563_pwrctrl_ida, adev->id);
	of_node_put(adev->dev.of_node);
	kfree(adev);
}

static void tc9563_pwrctrl_adev_remove(void *data)
{
	struct auxiliary_device *adev = data;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

static int tc9563_pwrctrl_adev_add(struct device *dev, const char *name,
				   struct device_node *of_node,
				   void *priv_data)
{
	struct auxiliary_device *adev;
	int id, ret;

	adev = kzalloc_obj(*adev);
	if (!adev)
		return -ENOMEM;

	id = ida_alloc(&tc9563_pwrctrl_ida, GFP_KERNEL);
	if (id < 0) {
		kfree(adev);
		return id;
	}

	adev->id = id;
	adev->name = name;
	adev->dev.parent = dev;
	adev->dev.platform_data = priv_data;
	adev->dev.release = tc9563_pwrctrl_adev_release;
	adev->dev.of_node = of_node_get(of_node);
	dev_set_of_node_reused(&adev->dev);

	ret = auxiliary_device_init(adev);
	if (ret) {
		ida_free(&tc9563_pwrctrl_ida, id);
		of_node_put(adev->dev.of_node);
		kfree(adev);
		return ret;
	}

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	return devm_add_action_or_reset(dev, tc9563_pwrctrl_adev_remove, adev);
}

static int tc9563_pwrctrl_add_gpio_adev(struct tc9563_pwrctrl *tc9563)
{
	struct device *dev = tc9563->pwrctrl.dev;

	if (!of_property_read_bool(dev->of_node, "gpio-controller") ||
	    !of_property_present(dev->of_node, "#gpio-cells"))
		return 0;

	return tc9563_pwrctrl_adev_add(dev, TC9563_GPIO_DEV_NAME, dev->of_node,
				       tc9563->regmap);
}

static int tc9563_pwrctrl_power_off(struct pci_pwrctrl *pwrctrl)
{
	struct tc9563_pwrctrl *tc9563 = container_of(pwrctrl,
					    struct tc9563_pwrctrl, pwrctrl);

	gpiod_set_value_cansleep(tc9563->reset_gpio, 1);

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

	gpiod_set_value_cansleep(tc9563->reset_gpio, 0);

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

		ret = tc9563_pwrctrl_set_port_l0s_l1_entry_delay(tc9563, i, false, cfg->l0s_delay);
		if (ret) {
			dev_err(dev, "Setting L0s entry delay failed\n");
			goto power_off;
		}

		ret = tc9563_pwrctrl_set_port_l0s_l1_entry_delay(tc9563, i, true, cfg->l1_delay);
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

	/* Configure the integrated Ethernet MAC endpoint */
	ret = tc9563_pwrctrl_set_eth_l0s_l1_entry_delay(tc9563, false,
							tc9563->ep_cfg.l0s_delay);
	if (ret) {
		dev_err(dev, "Setting Ethernet L0s entry delay failed\n");
		goto power_off;
	}

	ret = tc9563_pwrctrl_set_eth_l0s_l1_entry_delay(tc9563, true,
							tc9563->ep_cfg.l1_delay);
	if (ret) {
		dev_err(dev, "Setting Ethernet L1 entry delay failed\n");
		goto power_off;
	}

	ret = tc9563_pwrctrl_assert_deassert_reset(tc9563, true);
	if (!ret)
		return 0;

power_off:
	tc9563_pwrctrl_power_off(&tc9563->pwrctrl);
	return ret;
}

static const struct regmap_config tc9563_regmap_config = {
	.reg_bits = 24,
	.val_bits = 32,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
};

static int tc9563_pwrctrl_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	enum tc9563_pwrctrl_ports port;
	struct tc9563_pwrctrl *tc9563;
	struct device_node *i2c_node;
	struct i2c_client *client;
	int ret, addr;

	tc9563 = devm_kzalloc(dev, sizeof(*tc9563), GFP_KERNEL);
	if (!tc9563)
		return -ENOMEM;

	ret = of_property_read_u32_index(node, "i2c-parent", 1, &addr);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to read i2c-parent property\n");

	i2c_node = of_parse_phandle(dev->of_node, "i2c-parent", 0);
	tc9563->adapter = of_get_i2c_adapter_by_node(i2c_node);
	of_node_put(i2c_node);
	if (!tc9563->adapter)
		return dev_err_probe(dev, -EPROBE_DEFER, "Failed to find I2C adapter\n");

	client = devm_i2c_new_dummy_device(dev, tc9563->adapter, addr);
	if (IS_ERR(client)) {
		dev_err(dev, "Failed to create I2C client\n");
		ret = PTR_ERR(client);
		goto remove_i2c;
	}

	tc9563->regmap = devm_regmap_init_i2c(client, &tc9563_regmap_config);
	if (IS_ERR(tc9563->regmap)) {
		ret = dev_err_probe(dev, PTR_ERR(tc9563->regmap),
				    "Failed to allocate register map\n");
		goto remove_i2c;
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

	ret = tc9563_pwrctrl_parse_device_dt(node, &tc9563->cfg[TC9563_USP]);
	if (ret) {
		dev_err(dev, "failed to parse device tree properties: %d\n", ret);
		goto remove_i2c;
	}

	/*
	 * Downstream ports are always children of the upstream port.
	 * The first node represents DSP1, the second node represents DSP2,
	 * and so on.
	 */
	port = TC9563_USP;
	for_each_child_of_node_scoped(node, child) {
		if (++port >= TC9563_MAX)
			break;

		ret = tc9563_pwrctrl_parse_device_dt(child, &tc9563->cfg[port]);
		if (ret)
			break;

		/*
		 * The integrated Ethernet MAC Endpoint under VDSP is a single
		 * device whose functions share the same config registers.
		 */
		if (port == TC9563_VDSP) {
			struct device_node *eth __free(device_node) =
					of_get_next_available_child(child, NULL);

			if (eth) {
				ret = tc9563_pwrctrl_parse_device_dt(eth,
							&tc9563->ep_cfg);
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

	ret = tc9563_pwrctrl_add_gpio_adev(tc9563);
	if (ret)
		goto remove_i2c;

	ret = devm_pci_pwrctrl_device_set_ready(dev, &tc9563->pwrctrl);
	if (ret)
		goto power_off;

	return 0;

power_off:
	tc9563_pwrctrl_power_off(&tc9563->pwrctrl);
remove_i2c:
	i2c_put_adapter(tc9563->adapter);
	return ret;
}

static void tc9563_pwrctrl_remove(struct platform_device *pdev)
{
	struct pci_pwrctrl *pwrctrl = dev_get_drvdata(&pdev->dev);
	struct tc9563_pwrctrl *tc9563 = container_of(pwrctrl,
					struct tc9563_pwrctrl, pwrctrl);

	tc9563_pwrctrl_power_off(&tc9563->pwrctrl);
	i2c_put_adapter(tc9563->adapter);
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
