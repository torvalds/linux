// SPDX-License-Identifier: GPL-2.0+
/*
 * Raspberry Pi driver for firmware controlled clocks
 *
 * Even though clk-bcm2835 provides an interface to the hardware registers for
 * the system clocks we've had to factor out 'pllb' as the firmware 'owns' it.
 * We're not allowed to change it directly as we might race with the
 * over-temperature and under-voltage protections provided by the firmware.
 *
 * Copyright (C) 2019 Nicolas Saenz Julienne <nsaenzjulienne@suse.de>
 */

#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <soc/bcm2835/raspberrypi-firmware.h>

static char *rpi_firmware_clk_names[] = {
	[RPI_FIRMWARE_EMMC_CLK_ID]	= "emmc",
	[RPI_FIRMWARE_UART_CLK_ID]	= "uart",
	[RPI_FIRMWARE_ARM_CLK_ID]	= "arm",
	[RPI_FIRMWARE_CORE_CLK_ID]	= "core",
	[RPI_FIRMWARE_V3D_CLK_ID]	= "v3d",
	[RPI_FIRMWARE_H264_CLK_ID]	= "h264",
	[RPI_FIRMWARE_ISP_CLK_ID]	= "isp",
	[RPI_FIRMWARE_SDRAM_CLK_ID]	= "sdram",
	[RPI_FIRMWARE_PIXEL_CLK_ID]	= "pixel",
	[RPI_FIRMWARE_PWM_CLK_ID]	= "pwm",
	[RPI_FIRMWARE_HEVC_CLK_ID]	= "hevc",
	[RPI_FIRMWARE_EMMC2_CLK_ID]	= "emmc2",
	[RPI_FIRMWARE_M2MC_CLK_ID]	= "m2mc",
	[RPI_FIRMWARE_PIXEL_BVB_CLK_ID]	= "pixel-bvb",
	[RPI_FIRMWARE_VEC_CLK_ID]	= "vec",
};

#define RPI_FIRMWARE_STATE_ENABLE_BIT	BIT(0)
#define RPI_FIRMWARE_STATE_WAIT_BIT	BIT(1)

struct raspberrypi_clk_variant;

struct raspberrypi_clk {
	struct device *dev;
	struct rpi_firmware *firmware;
	struct platform_device *cpufreq;
};

struct raspberrypi_clk_data {
	struct clk_hw hw;

	unsigned int id;
	struct raspberrypi_clk_variant *variant;

	struct raspberrypi_clk *rpi;
};

struct raspberrypi_clk_variant {
	bool		export;
	char		*clkdev;
	unsigned long	min_rate;
	bool		minimize;
};

static struct raspberrypi_clk_variant
raspberrypi_clk_variants[RPI_FIRMWARE_NUM_CLK_ID] = {
	[RPI_FIRMWARE_ARM_CLK_ID] = {
		.export = true,
		.clkdev = "cpu0",
	},
	[RPI_FIRMWARE_CORE_CLK_ID] = {
		.export = true,

		/*
		 * The clock is shared between the HVS and the CSI
		 * controllers, on the BCM2711 and will change depending
		 * on the pixels composited on the HVS and the capture
		 * resolution on Unicam.
		 *
		 * Since the rate can get quite large, and we need to
		 * coordinate between both driver instances, let's
		 * always use the minimum the drivers will let us.
		 */
		.minimize = true,
	},
	[RPI_FIRMWARE_M2MC_CLK_ID] = {
		.export = true,

		/*
		 * If we boot without any cable connected to any of the
		 * HDMI connector, the firmware will skip the HSM
		 * initialization and leave it with a rate of 0,
		 * resulting in a bus lockup when we're accessing the
		 * registers even if it's enabled.
		 *
		 * Let's put a sensible default so that we don't end up
		 * in this situation.
		 */
		.min_rate = 120000000,

		/*
		 * The clock is shared between the two HDMI controllers
		 * on the BCM2711 and will change depending on the
		 * resolution output on each. Since the rate can get
		 * quite large, and we need to coordinate between both
		 * driver instances, let's always use the minimum the
		 * drivers will let us.
		 */
		.minimize = true,
	},
	[RPI_FIRMWARE_V3D_CLK_ID] = {
		.export = true,
	},
	[RPI_FIRMWARE_PIXEL_CLK_ID] = {
		.export = true,
	},
	[RPI_FIRMWARE_HEVC_CLK_ID] = {
		.export = true,
	},
	[RPI_FIRMWARE_PIXEL_BVB_CLK_ID] = {
		.export = true,
	},
	[RPI_FIRMWARE_VEC_CLK_ID] = {
		.export = true,
	},
};

/*
 * Structure of the message passed to Raspberry Pi's firmware in order to
 * change clock rates. The 'disable_turbo' option is only available to the ARM
 * clock (pllb) which we enable by default as turbo mode will alter multiple
 * clocks at once.
 *
 * Even though we're able to access the clock registers directly we're bound to
 * use the firmware interface as the firmware ultimately takes care of
 * mitigating overheating/undervoltage situations and we would be changing
 * frequencies behind his back.
 *
 * For more information on the firmware interface check:
 * https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
 */
struct raspberrypi_firmware_prop {
	__le32 id;
	__le32 val;
	__le32 disable_turbo;
} __packed;

static int raspberrypi_clock_property(struct rpi_firmware *firmware,
				      const struct raspberrypi_clk_data *data,
				      u32 tag, u32 *val)
{
	struct raspberrypi_firmware_prop msg = {
		.id = cpu_to_le32(data->id),
		.val = cpu_to_le32(*val),
		.disable_turbo = cpu_to_le32(1),
	};
	int ret;

	ret = rpi_firmware_property(firmware, tag, &msg, sizeof(msg));
	if (ret)
		return ret;

	*val = le32_to_cpu(msg.val);

	return 0;
}

static int raspberrypi_fw_is_prepared(struct clk_hw *hw)
{
	struct raspberrypi_clk_data *data =
		container_of(hw, struct raspberrypi_clk_data, hw);
	struct raspberrypi_clk *rpi = data->rpi;
	u32 val = 0;
	int ret;

	ret = raspberrypi_clock_property(rpi->firmware, data,
					 RPI_FIRMWARE_GET_CLOCK_STATE, &val);
	if (ret)
		return 0;

	return !!(val & RPI_FIRMWARE_STATE_ENABLE_BIT);
}


static unsigned long raspberrypi_fw_get_rate(struct clk_hw *hw,
					     unsigned long parent_rate)
{
	struct raspberrypi_clk_data *data =
		container_of(hw, struct raspberrypi_clk_data, hw);
	struct raspberrypi_clk *rpi = data->rpi;
	u32 val = 0;
	int ret;

	ret = raspberrypi_clock_property(rpi->firmware, data,
					 RPI_FIRMWARE_GET_CLOCK_RATE, &val);
	if (ret)
		return 0;

	return val;
}

static int raspberrypi_fw_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct raspberrypi_clk_data *data =
		container_of(hw, struct raspberrypi_clk_data, hw);
	struct raspberrypi_clk *rpi = data->rpi;
	u32 _rate = rate;
	int ret;

	ret = raspberrypi_clock_property(rpi->firmware, data,
					 RPI_FIRMWARE_SET_CLOCK_RATE, &_rate);
	if (ret)
		dev_err_ratelimited(rpi->dev, "Failed to change %s frequency: %d\n",
				    clk_hw_get_name(hw), ret);

	return ret;
}

static int raspberrypi_fw_dumb_determine_rate(struct clk_hw *hw,
					      struct clk_rate_request *req)
{
	struct raspberrypi_clk_data *data =
		container_of(hw, struct raspberrypi_clk_data, hw);
	struct raspberrypi_clk_variant *variant = data->variant;

	/*
	 * The firmware will do the rounding but that isn't part of
	 * the interface with the firmware, so we just do our best
	 * here.
	 */

	req->rate = clamp(req->rate, req->min_rate, req->max_rate);

	/*
	 * We want to aggressively reduce the clock rate here, so let's
	 * just ignore the requested rate and return the bare minimum
	 * rate we can get away with.
	 */
	if (variant->minimize && req->min_rate > 0)
		req->rate = req->min_rate;

	return 0;
}

static const struct clk_ops raspberrypi_firmware_clk_ops = {
	.is_prepared	= raspberrypi_fw_is_prepared,
	.recalc_rate	= raspberrypi_fw_get_rate,
	.determine_rate	= raspberrypi_fw_dumb_determine_rate,
	.set_rate	= raspberrypi_fw_set_rate,
};

static struct clk_hw *raspberrypi_clk_register(struct raspberrypi_clk *rpi,
					       unsigned int parent,
					       unsigned int id,
					       struct raspberrypi_clk_variant *variant)
{
	struct raspberrypi_clk_data *data;
	struct clk_init_data init = {};
	u32 min_rate, max_rate;
	int ret;

	data = devm_kzalloc(rpi->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return ERR_PTR(-ENOMEM);
	data->rpi = rpi;
	data->id = id;
	data->variant = variant;

	init.name = devm_kasprintf(rpi->dev, GFP_KERNEL,
				   "fw-clk-%s",
				   rpi_firmware_clk_names[id]);
	init.ops = &raspberrypi_firmware_clk_ops;
	init.flags = CLK_GET_RATE_NOCACHE;

	data->hw.init = &init;

	ret = raspberrypi_clock_property(rpi->firmware, data,
					 RPI_FIRMWARE_GET_MIN_CLOCK_RATE,
					 &min_rate);
	if (ret) {
		dev_err(rpi->dev, "Failed to get clock %d min freq: %d\n",
			id, ret);
		return ERR_PTR(ret);
	}

	ret = raspberrypi_clock_property(rpi->firmware, data,
					 RPI_FIRMWARE_GET_MAX_CLOCK_RATE,
					 &max_rate);
	if (ret) {
		dev_err(rpi->dev, "Failed to get clock %d max freq: %d\n",
			id, ret);
		return ERR_PTR(ret);
	}

	ret = devm_clk_hw_register(rpi->dev, &data->hw);
	if (ret)
		return ERR_PTR(ret);

	clk_hw_set_rate_range(&data->hw, min_rate, max_rate);

	if (variant->clkdev) {
		ret = devm_clk_hw_register_clkdev(rpi->dev, &data->hw,
						  NULL, variant->clkdev);
		if (ret) {
			dev_err(rpi->dev, "Failed to initialize clkdev\n");
			return ERR_PTR(ret);
		}
	}

	if (variant->min_rate) {
		unsigned long rate;

		clk_hw_set_rate_range(&data->hw, variant->min_rate, max_rate);

		rate = raspberrypi_fw_get_rate(&data->hw, 0);
		if (rate < variant->min_rate) {
			ret = raspberrypi_fw_set_rate(&data->hw, variant->min_rate, 0);
			if (ret)
				return ERR_PTR(ret);
		}
	}

	return &data->hw;
}

struct rpi_firmware_get_clocks_response {
	u32 parent;
	u32 id;
};

static int raspberrypi_discover_clocks(struct raspberrypi_clk *rpi,
				       struct clk_hw_onecell_data *data)
{
	struct rpi_firmware_get_clocks_response *clks;
	int ret;

	/*
	 * The firmware doesn't guarantee that the last element of
	 * RPI_FIRMWARE_GET_CLOCKS is zeroed. So allocate an additional
	 * zero element as sentinel.
	 */
	clks = devm_kcalloc(rpi->dev,
			    RPI_FIRMWARE_NUM_CLK_ID + 1, sizeof(*clks),
			    GFP_KERNEL);
	if (!clks)
		return -ENOMEM;

	ret = rpi_firmware_property(rpi->firmware, RPI_FIRMWARE_GET_CLOCKS,
				    clks,
				    sizeof(*clks) * RPI_FIRMWARE_NUM_CLK_ID);
	if (ret)
		return ret;

	while (clks->id) {
		struct raspberrypi_clk_variant *variant;

		if (clks->id > RPI_FIRMWARE_NUM_CLK_ID) {
			dev_err(rpi->dev, "Unknown clock id: %u (max: %u)\n",
					   clks->id, RPI_FIRMWARE_NUM_CLK_ID);
			return -EINVAL;
		}

		variant = &raspberrypi_clk_variants[clks->id];
		if (variant->export) {
			struct clk_hw *hw;

			hw = raspberrypi_clk_register(rpi, clks->parent,
						      clks->id, variant);
			if (IS_ERR(hw))
				return PTR_ERR(hw);

			data->hws[clks->id] = hw;
			data->num = clks->id + 1;
		}

		clks++;
	}

	return 0;
}

static int raspberrypi_clk_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device_node *firmware_node;
	struct device *dev = &pdev->dev;
	struct rpi_firmware *firmware;
	struct raspberrypi_clk *rpi;
	int ret;

	/*
	 * We can be probed either through the an old-fashioned
	 * platform device registration or through a DT node that is a
	 * child of the firmware node. Handle both cases.
	 */
	if (dev->of_node)
		firmware_node = of_get_parent(dev->of_node);
	else
		firmware_node = of_find_compatible_node(NULL, NULL,
							"raspberrypi,bcm2835-firmware");
	if (!firmware_node) {
		dev_err(dev, "Missing firmware node\n");
		return -ENOENT;
	}

	firmware = devm_rpi_firmware_get(&pdev->dev, firmware_node);
	of_node_put(firmware_node);
	if (!firmware)
		return -EPROBE_DEFER;

	rpi = devm_kzalloc(dev, sizeof(*rpi), GFP_KERNEL);
	if (!rpi)
		return -ENOMEM;

	rpi->dev = dev;
	rpi->firmware = firmware;
	platform_set_drvdata(pdev, rpi);

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws,
						 RPI_FIRMWARE_NUM_CLK_ID),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	ret = raspberrypi_discover_clocks(rpi, clk_data);
	if (ret)
		return ret;

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					  clk_data);
	if (ret)
		return ret;

	rpi->cpufreq = platform_device_register_data(dev, "raspberrypi-cpufreq",
						     -1, NULL, 0);

	return 0;
}

static void raspberrypi_clk_remove(struct platform_device *pdev)
{
	struct raspberrypi_clk *rpi = platform_get_drvdata(pdev);

	platform_device_unregister(rpi->cpufreq);
}

static const struct of_device_id raspberrypi_clk_match[] = {
	{ .compatible = "raspberrypi,firmware-clocks" },
	{ },
};
MODULE_DEVICE_TABLE(of, raspberrypi_clk_match);

static struct platform_driver raspberrypi_clk_driver = {
	.driver = {
		.name = "raspberrypi-clk",
		.of_match_table = raspberrypi_clk_match,
	},
	.probe          = raspberrypi_clk_probe,
	.remove_new	= raspberrypi_clk_remove,
};
module_platform_driver(raspberrypi_clk_driver);

MODULE_AUTHOR("Nicolas Saenz Julienne <nsaenzjulienne@suse.de>");
MODULE_DESCRIPTION("Raspberry Pi firmware clock driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:raspberrypi-clk");
