// SPDX-License-Identifier: GPL-2.0
/*
 * The Vendor OEM extension for System Control and Power Interface (SCMI)
 * Protocol based clock driver
 *
 * Copyright 2025 NXP
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/scmi_imx_protocol.h>
#include <linux/scmi_protocol.h>

#include "clk-scmi.h"

#define SCMI_CLOCK_CFG_IMX_SSC			0x80
#define SCMI_CLOCK_IMX_SS_PERCENTAGE_MASK	GENMASK(7, 0)
#define SCMI_CLOCK_IMX_SS_MOD_FREQ_MASK		GENMASK(23, 8)
#define SCMI_CLOCK_IMX_SS_ENABLE_MASK		BIT(24)

/*
 * Selection is based on SCMI vendor_id/sub_vendor_id and optional machine
 * compatible string, without involving impl_ver. impl_ver‑specific behavior
 * should be considered a bug and handled via SCMI Quirk framework.
 */
struct scmi_clk_oem_info {
	char *vendor_id;
	char *sub_vendor_id;
	char *compatible;
	const void *data;
};

static int
scmi_clk_imx_set_spread_spectrum(struct clk_hw *hw,
				 const struct clk_spread_spectrum *ss_conf)
{
	struct scmi_clk *clk = to_scmi_clk(hw);
	int ret;
	u32 val;

	/*
	 * extConfigValue[7:0]   - spread percentage (%)
	 * extConfigValue[23:8]  - Modulation Frequency
	 * extConfigValue[24]    - Enable/Disable
	 * extConfigValue[31:25] - Reserved
	 */
	val = FIELD_PREP(SCMI_CLOCK_IMX_SS_PERCENTAGE_MASK, ss_conf->spread_bp / 10000);
	val |= FIELD_PREP(SCMI_CLOCK_IMX_SS_MOD_FREQ_MASK, ss_conf->modfreq_hz);
	if (ss_conf->method != CLK_SPREAD_NO)
		val |= SCMI_CLOCK_IMX_SS_ENABLE_MASK;
	ret = scmi_proto_clk_ops->config_oem_set(clk->ph, clk->id,
						 SCMI_CLOCK_CFG_IMX_SSC,
						 val, false);
	if (ret)
		dev_warn(clk->dev,
			 "Failed to set spread spectrum(%u,%u,%u) for clock ID %d\n",
			 ss_conf->modfreq_hz, ss_conf->spread_bp, ss_conf->method,
			 clk->id);

	return ret;
}

static int
scmi_clk_imx_query_oem_feats(const struct scmi_protocol_handle *ph, u32 id,
			     unsigned int *feats_key)
{
	int ret;
	u32 val;

	ret = scmi_proto_clk_ops->config_oem_get(ph, id,
						 SCMI_CLOCK_CFG_IMX_SSC,
						 &val, NULL, false);
	if (!ret)
		*feats_key |= BIT(SCMI_CLK_EXT_OEM_SSC_SUPPORTED);

	return 0;
}

static const struct scmi_clk_oem scmi_clk_oem_imx = {
	.query_ext_oem_feats = scmi_clk_imx_query_oem_feats,
	.set_spread_spectrum = scmi_clk_imx_set_spread_spectrum,
};

static const struct scmi_clk_oem_info info[] = {
	{ SCMI_IMX_VENDOR, SCMI_IMX_SUBVENDOR, NULL, &scmi_clk_oem_imx },
};

int scmi_clk_oem_init(struct scmi_device *sdev)
{
	const struct scmi_handle *handle = sdev->handle;
	int i, size = ARRAY_SIZE(info);

	for (i = 0; i < size; i++) {
		if (strcmp(handle->version->vendor_id, info[i].vendor_id) ||
		    strcmp(handle->version->sub_vendor_id, info[i].sub_vendor_id))
			continue;
		if (info[i].compatible &&
		    !of_machine_is_compatible(info[i].compatible))
			continue;

		break;
	}

	if (i < size)
		dev_set_drvdata(&sdev->dev, (void *)info[i].data);

	return 0;
}
