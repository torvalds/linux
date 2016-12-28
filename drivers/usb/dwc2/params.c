/*
 * Copyright (C) 2004-2016 Synopsys, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the above-listed copyright holders may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>

#include "core.h"

static const struct dwc2_core_params params_hi6220 = {
	.otg_cap			= 2,	/* No HNP/SRP capable */
	.otg_ver			= 0,	/* 1.3 */
	.dma_desc_enable		= 0,
	.dma_desc_fs_enable		= 0,
	.speed				= 0,	/* High Speed */
	.enable_dynamic_fifo		= 1,
	.en_multiple_tx_fifo		= 1,
	.host_rx_fifo_size		= 512,
	.host_nperio_tx_fifo_size	= 512,
	.host_perio_tx_fifo_size	= 512,
	.max_transfer_size		= 65535,
	.max_packet_count		= 511,
	.host_channels			= 16,
	.phy_type			= 1,	/* UTMI */
	.phy_utmi_width			= 8,
	.phy_ulpi_ddr			= 0,	/* Single */
	.phy_ulpi_ext_vbus		= 0,
	.i2c_enable			= 0,
	.ulpi_fs_ls			= 0,
	.host_support_fs_ls_low_power	= 0,
	.host_ls_low_power_phy_clk	= 0,	/* 48 MHz */
	.ts_dline			= 0,
	.reload_ctl			= 0,
	.ahbcfg				= GAHBCFG_HBSTLEN_INCR16 <<
					  GAHBCFG_HBSTLEN_SHIFT,
	.uframe_sched			= 0,
	.external_id_pin_ctl		= -1,
	.hibernation			= -1,
};

static const struct dwc2_core_params params_bcm2835 = {
	.otg_cap			= 0,	/* HNP/SRP capable */
	.otg_ver			= 0,	/* 1.3 */
	.dma_desc_enable		= 0,
	.dma_desc_fs_enable		= 0,
	.speed				= 0,	/* High Speed */
	.enable_dynamic_fifo		= 1,
	.en_multiple_tx_fifo		= 1,
	.host_rx_fifo_size		= 774,	/* 774 DWORDs */
	.host_nperio_tx_fifo_size	= 256,	/* 256 DWORDs */
	.host_perio_tx_fifo_size	= 512,	/* 512 DWORDs */
	.max_transfer_size		= 65535,
	.max_packet_count		= 511,
	.host_channels			= 8,
	.phy_type			= 1,	/* UTMI */
	.phy_utmi_width			= 8,	/* 8 bits */
	.phy_ulpi_ddr			= 0,	/* Single */
	.phy_ulpi_ext_vbus		= 0,
	.i2c_enable			= 0,
	.ulpi_fs_ls			= 0,
	.host_support_fs_ls_low_power	= 0,
	.host_ls_low_power_phy_clk	= 0,	/* 48 MHz */
	.ts_dline			= 0,
	.reload_ctl			= 0,
	.ahbcfg				= 0x10,
	.uframe_sched			= 0,
	.external_id_pin_ctl		= -1,
	.hibernation			= -1,
};

static const struct dwc2_core_params params_rk3066 = {
	.otg_cap			= 2,	/* non-HNP/non-SRP */
	.otg_ver			= -1,
	.dma_desc_enable		= 0,
	.dma_desc_fs_enable		= 0,
	.speed				= -1,
	.enable_dynamic_fifo		= 1,
	.en_multiple_tx_fifo		= -1,
	.host_rx_fifo_size		= 525,	/* 525 DWORDs */
	.host_nperio_tx_fifo_size	= 128,	/* 128 DWORDs */
	.host_perio_tx_fifo_size	= 256,	/* 256 DWORDs */
	.max_transfer_size		= -1,
	.max_packet_count		= -1,
	.host_channels			= -1,
	.phy_type			= -1,
	.phy_utmi_width			= -1,
	.phy_ulpi_ddr			= -1,
	.phy_ulpi_ext_vbus		= -1,
	.i2c_enable			= -1,
	.ulpi_fs_ls			= -1,
	.host_support_fs_ls_low_power	= -1,
	.host_ls_low_power_phy_clk	= -1,
	.ts_dline			= -1,
	.reload_ctl			= -1,
	.ahbcfg				= GAHBCFG_HBSTLEN_INCR16 <<
					  GAHBCFG_HBSTLEN_SHIFT,
	.uframe_sched			= -1,
	.external_id_pin_ctl		= -1,
	.hibernation			= -1,
};

static const struct dwc2_core_params params_ltq = {
	.otg_cap			= 2,	/* non-HNP/non-SRP */
	.otg_ver			= -1,
	.dma_desc_enable		= -1,
	.dma_desc_fs_enable		= -1,
	.speed				= -1,
	.enable_dynamic_fifo		= -1,
	.en_multiple_tx_fifo		= -1,
	.host_rx_fifo_size		= 288,	/* 288 DWORDs */
	.host_nperio_tx_fifo_size	= 128,	/* 128 DWORDs */
	.host_perio_tx_fifo_size	= 96,	/* 96 DWORDs */
	.max_transfer_size		= 65535,
	.max_packet_count		= 511,
	.host_channels			= -1,
	.phy_type			= -1,
	.phy_utmi_width			= -1,
	.phy_ulpi_ddr			= -1,
	.phy_ulpi_ext_vbus		= -1,
	.i2c_enable			= -1,
	.ulpi_fs_ls			= -1,
	.host_support_fs_ls_low_power	= -1,
	.host_ls_low_power_phy_clk	= -1,
	.ts_dline			= -1,
	.reload_ctl			= -1,
	.ahbcfg				= GAHBCFG_HBSTLEN_INCR16 <<
					  GAHBCFG_HBSTLEN_SHIFT,
	.uframe_sched			= -1,
	.external_id_pin_ctl		= -1,
	.hibernation			= -1,
};

static const struct dwc2_core_params params_amlogic = {
	.otg_cap			= DWC2_CAP_PARAM_NO_HNP_SRP_CAPABLE,
	.otg_ver			= -1,
	.dma_desc_enable		= 0,
	.dma_desc_fs_enable		= 0,
	.speed				= DWC2_SPEED_PARAM_HIGH,
	.enable_dynamic_fifo		= 1,
	.en_multiple_tx_fifo		= -1,
	.host_rx_fifo_size		= 512,
	.host_nperio_tx_fifo_size	= 500,
	.host_perio_tx_fifo_size	= 500,
	.max_transfer_size		= -1,
	.max_packet_count		= -1,
	.host_channels			= 16,
	.phy_type			= DWC2_PHY_TYPE_PARAM_UTMI,
	.phy_utmi_width			= -1,
	.phy_ulpi_ddr			= -1,
	.phy_ulpi_ext_vbus		= -1,
	.i2c_enable			= -1,
	.ulpi_fs_ls			= -1,
	.host_support_fs_ls_low_power	= -1,
	.host_ls_low_power_phy_clk	= -1,
	.ts_dline			= -1,
	.reload_ctl			= 1,
	.ahbcfg				= GAHBCFG_HBSTLEN_INCR8 <<
					  GAHBCFG_HBSTLEN_SHIFT,
	.uframe_sched			= 0,
	.external_id_pin_ctl		= -1,
	.hibernation			= -1,
};

static const struct dwc2_core_params params_default = {
	.otg_cap			= -1,
	.otg_ver			= -1,

	/*
	 * Disable descriptor dma mode by default as the HW can support
	 * it, but does not support it for SPLIT transactions.
	 * Disable it for FS devices as well.
	 */
	.dma_desc_enable		= 0,
	.dma_desc_fs_enable		= 0,

	.speed				= -1,
	.enable_dynamic_fifo		= -1,
	.en_multiple_tx_fifo		= -1,
	.host_rx_fifo_size		= -1,
	.host_nperio_tx_fifo_size	= -1,
	.host_perio_tx_fifo_size	= -1,
	.max_transfer_size		= -1,
	.max_packet_count		= -1,
	.host_channels			= -1,
	.phy_type			= -1,
	.phy_utmi_width			= -1,
	.phy_ulpi_ddr			= -1,
	.phy_ulpi_ext_vbus		= -1,
	.i2c_enable			= -1,
	.ulpi_fs_ls			= -1,
	.host_support_fs_ls_low_power	= -1,
	.host_ls_low_power_phy_clk	= -1,
	.ts_dline			= -1,
	.reload_ctl			= -1,
	.ahbcfg				= -1,
	.uframe_sched			= -1,
	.external_id_pin_ctl		= -1,
	.hibernation			= -1,
};

const struct of_device_id dwc2_of_match_table[] = {
	{ .compatible = "brcm,bcm2835-usb", .data = &params_bcm2835 },
	{ .compatible = "hisilicon,hi6220-usb", .data = &params_hi6220 },
	{ .compatible = "rockchip,rk3066-usb", .data = &params_rk3066 },
	{ .compatible = "lantiq,arx100-usb", .data = &params_ltq },
	{ .compatible = "lantiq,xrx200-usb", .data = &params_ltq },
	{ .compatible = "snps,dwc2", .data = NULL },
	{ .compatible = "samsung,s3c6400-hsotg", .data = NULL},
	{ .compatible = "amlogic,meson8b-usb", .data = &params_amlogic },
	{ .compatible = "amlogic,meson-gxbb-usb", .data = &params_amlogic },
	{ .compatible = "amcc,dwc-otg", .data = NULL },
	{},
};
MODULE_DEVICE_TABLE(of, dwc2_of_match_table);

static void dwc2_get_device_property(struct dwc2_hsotg *hsotg,
				     char *property, u8 size, u64 *value)
{
	u8 val8;
	u16 val16;
	u32 val32;

	switch (size) {
	case 0:
		*value = device_property_read_bool(hsotg->dev, property);
		break;
	case 1:
		if (device_property_read_u8(hsotg->dev, property, &val8))
			return;

		*value = val8;
		break;
	case 2:
		if (device_property_read_u16(hsotg->dev, property, &val16))
			return;

		*value = val16;
		break;
	case 4:
		if (device_property_read_u32(hsotg->dev, property, &val32))
			return;

		*value = val32;
		break;
	case 8:
		if (device_property_read_u64(hsotg->dev, property, value))
			return;

		break;
	default:
		/*
		 * The size is checked by the only function that calls
		 * this so this should never happen.
		 */
		WARN_ON(1);
		return;
	}
}

static void dwc2_set_core_param(void *param, u8 size, u64 value)
{
	switch (size) {
	case 0:
		*((bool *)param) = !!value;
		break;
	case 1:
		*((u8 *)param) = (u8)value;
		break;
	case 2:
		*((u16 *)param) = (u16)value;
		break;
	case 4:
		*((u32 *)param) = (u32)value;
		break;
	case 8:
		*((u64 *)param) = (u64)value;
		break;
	default:
		/*
		 * The size is checked by the only function that calls
		 * this so this should never happen.
		 */
		WARN_ON(1);
		return;
	}
}

/**
 * dwc2_set_param() - Set a core parameter
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @param: Pointer to the parameter to set
 * @lookup: True if the property should be looked up
 * @property: The device property to read
 * @legacy: The param value to set if @property is not available. This
 *          will typically be the legacy value set in the static
 *          params structure.
 * @def: The default value
 * @min: The minimum value
 * @max: The maximum value
 * @size: The size of the core parameter in bytes, or 0 for bool.
 *
 * This function looks up @property and sets the @param to that value.
 * If the property doesn't exist it uses the passed-in @value. It will
 * verify that the value falls between @min and @max. If it doesn't,
 * it will output an error and set the parameter to either @def or,
 * failing that, to @min.
 *
 * The @size is used to write to @param and to query the device
 * properties so that this same function can be used with different
 * types of parameters.
 */
static void dwc2_set_param(struct dwc2_hsotg *hsotg, void *param,
			   bool lookup, char *property, u64 legacy,
			   u64 def, u64 min, u64 max, u8 size)
{
	u64 sizemax;
	u64 value;

	if (WARN_ON(!hsotg || !param || !property))
		return;

	if (WARN((size > 8) || ((size & (size - 1)) != 0),
		 "Invalid size %d for %s\n", size, property))
		return;

	dev_vdbg(hsotg->dev, "%s: Setting %s: legacy=%llu, def=%llu, min=%llu, max=%llu, size=%d\n",
		 __func__, property, legacy, def, min, max, size);

	sizemax = (1ULL << (size * 8)) - 1;
	value = legacy;

	/* Override legacy settings. */
	if (lookup)
		dwc2_get_device_property(hsotg, property, size, &value);

	/*
	 * While the value is not valid, try setting it to the default
	 * value, and failing that, set it to the minimum.
	 */
	while ((value < min) || (value > max)) {
		/* Print an error unless the value is set to auto. */
		if (value != sizemax)
			dev_err(hsotg->dev, "Invalid value %llu for param %s\n",
				value, property);

		/*
		 * If we are already the default, just set it to the
		 * minimum.
		 */
		if (value == def) {
			dev_vdbg(hsotg->dev, "%s: setting value to min=%llu\n",
				 __func__, min);
			value = min;
			break;
		}

		/* Try the default value */
		dev_vdbg(hsotg->dev, "%s: setting value to default=%llu\n",
			 __func__, def);
		value = def;
	}

	dev_dbg(hsotg->dev, "Setting %s to %llu\n", property, value);
	dwc2_set_core_param(param, size, value);
}

/**
 * dwc2_set_param_u16() - Set a u16 parameter
 *
 * See dwc2_set_param().
 */
static void dwc2_set_param_u16(struct dwc2_hsotg *hsotg, u16 *param,
			       bool lookup, char *property, u16 legacy,
			       u16 def, u16 min, u16 max)
{
	dwc2_set_param(hsotg, param, lookup, property,
		       legacy, def, min, max, 2);
}

/**
 * dwc2_set_param_bool() - Set a bool parameter
 *
 * See dwc2_set_param().
 *
 * Note: there is no 'legacy' argument here because there is no legacy
 * source of bool params.
 */
static void dwc2_set_param_bool(struct dwc2_hsotg *hsotg, bool *param,
				bool lookup, char *property,
				bool def, bool min, bool max)
{
	dwc2_set_param(hsotg, param, lookup, property,
		       def, def, min, max, 0);
}

#define DWC2_OUT_OF_BOUNDS(a, b, c)	((a) < (b) || (a) > (c))

/* Parameter access functions */
static void dwc2_set_param_otg_cap(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	switch (val) {
	case DWC2_CAP_PARAM_HNP_SRP_CAPABLE:
		if (hsotg->hw_params.op_mode != GHWCFG2_OP_MODE_HNP_SRP_CAPABLE)
			valid = 0;
		break;
	case DWC2_CAP_PARAM_SRP_ONLY_CAPABLE:
		switch (hsotg->hw_params.op_mode) {
		case GHWCFG2_OP_MODE_HNP_SRP_CAPABLE:
		case GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE:
		case GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE:
		case GHWCFG2_OP_MODE_SRP_CAPABLE_HOST:
			break;
		default:
			valid = 0;
			break;
		}
		break;
	case DWC2_CAP_PARAM_NO_HNP_SRP_CAPABLE:
		/* always valid */
		break;
	default:
		valid = 0;
		break;
	}

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for otg_cap parameter. Check HW configuration.\n",
				val);
		switch (hsotg->hw_params.op_mode) {
		case GHWCFG2_OP_MODE_HNP_SRP_CAPABLE:
			val = DWC2_CAP_PARAM_HNP_SRP_CAPABLE;
			break;
		case GHWCFG2_OP_MODE_SRP_ONLY_CAPABLE:
		case GHWCFG2_OP_MODE_SRP_CAPABLE_DEVICE:
		case GHWCFG2_OP_MODE_SRP_CAPABLE_HOST:
			val = DWC2_CAP_PARAM_SRP_ONLY_CAPABLE;
			break;
		default:
			val = DWC2_CAP_PARAM_NO_HNP_SRP_CAPABLE;
			break;
		}
		dev_dbg(hsotg->dev, "Setting otg_cap to %d\n", val);
	}

	hsotg->params.otg_cap = val;
}

static void dwc2_set_param_dma_desc_enable(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val > 0 && (hsotg->params.host_dma <= 0 ||
			!hsotg->hw_params.dma_desc_enable))
		valid = 0;
	if (val < 0)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for dma_desc_enable parameter. Check HW configuration.\n",
				val);
		val = (hsotg->params.host_dma > 0 &&
			hsotg->hw_params.dma_desc_enable);
		dev_dbg(hsotg->dev, "Setting dma_desc_enable to %d\n", val);
	}

	hsotg->params.dma_desc_enable = val;
}

static void dwc2_set_param_dma_desc_fs_enable(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val > 0 && (hsotg->params.host_dma <= 0 ||
			!hsotg->hw_params.dma_desc_enable))
		valid = 0;
	if (val < 0)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for dma_desc_fs_enable parameter. Check HW configuration.\n",
				val);
		val = (hsotg->params.host_dma > 0 &&
			hsotg->hw_params.dma_desc_enable);
	}

	hsotg->params.dma_desc_fs_enable = val;
	dev_dbg(hsotg->dev, "Setting dma_desc_fs_enable to %d\n", val);
}

static void
dwc2_set_param_host_support_fs_ls_low_power(struct dwc2_hsotg *hsotg,
					    int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"Wrong value for host_support_fs_low_power\n");
			dev_err(hsotg->dev,
				"host_support_fs_low_power must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev,
			"Setting host_support_fs_low_power to %d\n", val);
	}

	hsotg->params.host_support_fs_ls_low_power = val;
}

static void dwc2_set_param_enable_dynamic_fifo(struct dwc2_hsotg *hsotg,
					       int val)
{
	int valid = 1;

	if (val > 0 && !hsotg->hw_params.enable_dynamic_fifo)
		valid = 0;
	if (val < 0)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for enable_dynamic_fifo parameter. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.enable_dynamic_fifo;
		dev_dbg(hsotg->dev, "Setting enable_dynamic_fifo to %d\n", val);
	}

	hsotg->params.enable_dynamic_fifo = val;
}

static void dwc2_set_param_host_rx_fifo_size(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val < 16 || val > hsotg->hw_params.rx_fifo_size)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_rx_fifo_size. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.rx_fifo_size;
		dev_dbg(hsotg->dev, "Setting host_rx_fifo_size to %d\n", val);
	}

	hsotg->params.host_rx_fifo_size = val;
}

static void dwc2_set_param_host_nperio_tx_fifo_size(struct dwc2_hsotg *hsotg,
						    int val)
{
	int valid = 1;

	if (val < 16 || val > hsotg->hw_params.host_nperio_tx_fifo_size)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_nperio_tx_fifo_size. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.host_nperio_tx_fifo_size;
		dev_dbg(hsotg->dev, "Setting host_nperio_tx_fifo_size to %d\n",
			val);
	}

	hsotg->params.host_nperio_tx_fifo_size = val;
}

static void dwc2_set_param_host_perio_tx_fifo_size(struct dwc2_hsotg *hsotg,
						   int val)
{
	int valid = 1;

	if (val < 16 || val > hsotg->hw_params.host_perio_tx_fifo_size)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_perio_tx_fifo_size. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.host_perio_tx_fifo_size;
		dev_dbg(hsotg->dev, "Setting host_perio_tx_fifo_size to %d\n",
			val);
	}

	hsotg->params.host_perio_tx_fifo_size = val;
}

static void dwc2_set_param_max_transfer_size(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val < 2047 || val > hsotg->hw_params.max_transfer_size)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for max_transfer_size. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.max_transfer_size;
		dev_dbg(hsotg->dev, "Setting max_transfer_size to %d\n", val);
	}

	hsotg->params.max_transfer_size = val;
}

static void dwc2_set_param_max_packet_count(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val < 15 || val > hsotg->hw_params.max_packet_count)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for max_packet_count. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.max_packet_count;
		dev_dbg(hsotg->dev, "Setting max_packet_count to %d\n", val);
	}

	hsotg->params.max_packet_count = val;
}

static void dwc2_set_param_host_channels(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (val < 1 || val > hsotg->hw_params.host_channels)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_channels. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.host_channels;
		dev_dbg(hsotg->dev, "Setting host_channels to %d\n", val);
	}

	hsotg->params.host_channels = val;
}

static void dwc2_set_param_phy_type(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 0;
	u32 hs_phy_type, fs_phy_type;

	if (DWC2_OUT_OF_BOUNDS(val, DWC2_PHY_TYPE_PARAM_FS,
			       DWC2_PHY_TYPE_PARAM_ULPI)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for phy_type\n");
			dev_err(hsotg->dev, "phy_type must be 0, 1 or 2\n");
		}

		valid = 0;
	}

	hs_phy_type = hsotg->hw_params.hs_phy_type;
	fs_phy_type = hsotg->hw_params.fs_phy_type;
	if (val == DWC2_PHY_TYPE_PARAM_UTMI &&
	    (hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI ||
	     hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI))
		valid = 1;
	else if (val == DWC2_PHY_TYPE_PARAM_ULPI &&
		 (hs_phy_type == GHWCFG2_HS_PHY_TYPE_ULPI ||
		  hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI))
		valid = 1;
	else if (val == DWC2_PHY_TYPE_PARAM_FS &&
		 fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED)
		valid = 1;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for phy_type. Check HW configuration.\n",
				val);
		val = DWC2_PHY_TYPE_PARAM_FS;
		if (hs_phy_type != GHWCFG2_HS_PHY_TYPE_NOT_SUPPORTED) {
			if (hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI ||
			    hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI)
				val = DWC2_PHY_TYPE_PARAM_UTMI;
			else
				val = DWC2_PHY_TYPE_PARAM_ULPI;
		}
		dev_dbg(hsotg->dev, "Setting phy_type to %d\n", val);
	}

	hsotg->params.phy_type = val;
}

static int dwc2_get_param_phy_type(struct dwc2_hsotg *hsotg)
{
	return hsotg->params.phy_type;
}

static void dwc2_set_param_speed(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (DWC2_OUT_OF_BOUNDS(val, 0, 2)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for speed parameter\n");
			dev_err(hsotg->dev, "max_speed parameter must be 0, 1, or 2\n");
		}
		valid = 0;
	}

	if (dwc2_is_hs_iot(hsotg) &&
	    val == DWC2_SPEED_PARAM_LOW)
		valid = 0;

	if (val == DWC2_SPEED_PARAM_HIGH &&
	    dwc2_get_param_phy_type(hsotg) == DWC2_PHY_TYPE_PARAM_FS)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for speed parameter. Check HW configuration.\n",
				val);
		val = dwc2_get_param_phy_type(hsotg) == DWC2_PHY_TYPE_PARAM_FS ?
				DWC2_SPEED_PARAM_FULL : DWC2_SPEED_PARAM_HIGH;
		dev_dbg(hsotg->dev, "Setting speed to %d\n", val);
	}

	hsotg->params.speed = val;
}

static void dwc2_set_param_host_ls_low_power_phy_clk(struct dwc2_hsotg *hsotg,
						     int val)
{
	int valid = 1;

	if (DWC2_OUT_OF_BOUNDS(val, DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ,
			       DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_6MHZ)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"Wrong value for host_ls_low_power_phy_clk parameter\n");
			dev_err(hsotg->dev,
				"host_ls_low_power_phy_clk must be 0 or 1\n");
		}
		valid = 0;
	}

	if (val == DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ &&
	    dwc2_get_param_phy_type(hsotg) == DWC2_PHY_TYPE_PARAM_FS)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for host_ls_low_power_phy_clk. Check HW configuration.\n",
				val);
		val = dwc2_get_param_phy_type(hsotg) == DWC2_PHY_TYPE_PARAM_FS
			? DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_6MHZ
			: DWC2_HOST_LS_LOW_POWER_PHY_CLK_PARAM_48MHZ;
		dev_dbg(hsotg->dev, "Setting host_ls_low_power_phy_clk to %d\n",
			val);
	}

	hsotg->params.host_ls_low_power_phy_clk = val;
}

static void dwc2_set_param_phy_ulpi_ddr(struct dwc2_hsotg *hsotg, int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for phy_ulpi_ddr\n");
			dev_err(hsotg->dev, "phy_upli_ddr must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting phy_upli_ddr to %d\n", val);
	}

	hsotg->params.phy_ulpi_ddr = val;
}

static void dwc2_set_param_phy_ulpi_ext_vbus(struct dwc2_hsotg *hsotg, int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"Wrong value for phy_ulpi_ext_vbus\n");
			dev_err(hsotg->dev,
				"phy_ulpi_ext_vbus must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting phy_ulpi_ext_vbus to %d\n", val);
	}

	hsotg->params.phy_ulpi_ext_vbus = val;
}

static void dwc2_set_param_phy_utmi_width(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 0;

	switch (hsotg->hw_params.utmi_phy_data_width) {
	case GHWCFG4_UTMI_PHY_DATA_WIDTH_8:
		valid = (val == 8);
		break;
	case GHWCFG4_UTMI_PHY_DATA_WIDTH_16:
		valid = (val == 16);
		break;
	case GHWCFG4_UTMI_PHY_DATA_WIDTH_8_OR_16:
		valid = (val == 8 || val == 16);
		break;
	}

	if (!valid) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"%d invalid for phy_utmi_width. Check HW configuration.\n",
				val);
		}
		val = (hsotg->hw_params.utmi_phy_data_width ==
		       GHWCFG4_UTMI_PHY_DATA_WIDTH_8) ? 8 : 16;
		dev_dbg(hsotg->dev, "Setting phy_utmi_width to %d\n", val);
	}

	hsotg->params.phy_utmi_width = val;
}

static void dwc2_set_param_ulpi_fs_ls(struct dwc2_hsotg *hsotg, int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for ulpi_fs_ls\n");
			dev_err(hsotg->dev, "ulpi_fs_ls must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting ulpi_fs_ls to %d\n", val);
	}

	hsotg->params.ulpi_fs_ls = val;
}

static void dwc2_set_param_ts_dline(struct dwc2_hsotg *hsotg, int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for ts_dline\n");
			dev_err(hsotg->dev, "ts_dline must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting ts_dline to %d\n", val);
	}

	hsotg->params.ts_dline = val;
}

static void dwc2_set_param_i2c_enable(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev, "Wrong value for i2c_enable\n");
			dev_err(hsotg->dev, "i2c_enable must be 0 or 1\n");
		}

		valid = 0;
	}

	if (val == 1 && !(hsotg->hw_params.i2c_enable))
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for i2c_enable. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.i2c_enable;
		dev_dbg(hsotg->dev, "Setting i2c_enable to %d\n", val);
	}

	hsotg->params.i2c_enable = val;
}

static void dwc2_set_param_en_multiple_tx_fifo(struct dwc2_hsotg *hsotg,
					       int val)
{
	int valid = 1;

	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"Wrong value for en_multiple_tx_fifo,\n");
			dev_err(hsotg->dev,
				"en_multiple_tx_fifo must be 0 or 1\n");
		}
		valid = 0;
	}

	if (val == 1 && !hsotg->hw_params.en_multiple_tx_fifo)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for parameter en_multiple_tx_fifo. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.en_multiple_tx_fifo;
		dev_dbg(hsotg->dev, "Setting en_multiple_tx_fifo to %d\n", val);
	}

	hsotg->params.en_multiple_tx_fifo = val;
}

static void dwc2_set_param_reload_ctl(struct dwc2_hsotg *hsotg, int val)
{
	int valid = 1;

	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"'%d' invalid for parameter reload_ctl\n", val);
			dev_err(hsotg->dev, "reload_ctl must be 0 or 1\n");
		}
		valid = 0;
	}

	if (val == 1 && hsotg->hw_params.snpsid < DWC2_CORE_REV_2_92a)
		valid = 0;

	if (!valid) {
		if (val >= 0)
			dev_err(hsotg->dev,
				"%d invalid for parameter reload_ctl. Check HW configuration.\n",
				val);
		val = hsotg->hw_params.snpsid >= DWC2_CORE_REV_2_92a;
		dev_dbg(hsotg->dev, "Setting reload_ctl to %d\n", val);
	}

	hsotg->params.reload_ctl = val;
}

static void dwc2_set_param_ahbcfg(struct dwc2_hsotg *hsotg, int val)
{
	if (val != -1)
		hsotg->params.ahbcfg = val;
	else
		hsotg->params.ahbcfg = GAHBCFG_HBSTLEN_INCR4 <<
						GAHBCFG_HBSTLEN_SHIFT;
}

static void dwc2_set_param_otg_ver(struct dwc2_hsotg *hsotg, int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"'%d' invalid for parameter otg_ver\n", val);
			dev_err(hsotg->dev,
				"otg_ver must be 0 (for OTG 1.3 support) or 1 (for OTG 2.0 support)\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting otg_ver to %d\n", val);
	}

	hsotg->params.otg_ver = val;
}

static void dwc2_set_param_uframe_sched(struct dwc2_hsotg *hsotg, int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"'%d' invalid for parameter uframe_sched\n",
				val);
			dev_err(hsotg->dev, "uframe_sched must be 0 or 1\n");
		}
		val = 1;
		dev_dbg(hsotg->dev, "Setting uframe_sched to %d\n", val);
	}

	hsotg->params.uframe_sched = val;
}

static void dwc2_set_param_external_id_pin_ctl(struct dwc2_hsotg *hsotg,
					       int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"'%d' invalid for parameter external_id_pin_ctl\n",
				val);
			dev_err(hsotg->dev, "external_id_pin_ctl must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting external_id_pin_ctl to %d\n", val);
	}

	hsotg->params.external_id_pin_ctl = val;
}

static void dwc2_set_param_hibernation(struct dwc2_hsotg *hsotg,
				       int val)
{
	if (DWC2_OUT_OF_BOUNDS(val, 0, 1)) {
		if (val >= 0) {
			dev_err(hsotg->dev,
				"'%d' invalid for parameter hibernation\n",
				val);
			dev_err(hsotg->dev, "hibernation must be 0 or 1\n");
		}
		val = 0;
		dev_dbg(hsotg->dev, "Setting hibernation to %d\n", val);
	}

	hsotg->params.hibernation = val;
}

static void dwc2_set_param_tx_fifo_sizes(struct dwc2_hsotg *hsotg)
{
	int i;
	int num;
	char *property = "g-tx-fifo-size";
	struct dwc2_core_params *p = &hsotg->params;

	memset(p->g_tx_fifo_size, 0, sizeof(p->g_tx_fifo_size));

	/* Read tx fifo sizes */
	num = device_property_read_u32_array(hsotg->dev, property, NULL, 0);

	if (num > 0) {
		device_property_read_u32_array(hsotg->dev, property,
					       &p->g_tx_fifo_size[1],
					       num);
	} else {
		u32 p_tx_fifo[] = DWC2_G_P_LEGACY_TX_FIFO_SIZE;

		memcpy(&p->g_tx_fifo_size[1],
		       p_tx_fifo,
		       sizeof(p_tx_fifo));

		num = ARRAY_SIZE(p_tx_fifo);
	}

	for (i = 0; i < num; i++) {
		if ((i + 1) >= ARRAY_SIZE(p->g_tx_fifo_size))
			break;

		dev_dbg(hsotg->dev, "Setting %s[%d] to %d\n",
			property, i + 1, p->g_tx_fifo_size[i + 1]);
	}
}

static void dwc2_set_gadget_dma(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	struct dwc2_core_params *p = &hsotg->params;
	bool dma_capable = !(hw->arch == GHWCFG2_SLAVE_ONLY_ARCH);

	/* Buffer DMA */
	dwc2_set_param_bool(hsotg, &p->g_dma,
			    false, "gadget-dma",
			    true, false,
			    dma_capable);

	/* DMA Descriptor */
	dwc2_set_param_bool(hsotg, &p->g_dma_desc, false,
			    "gadget-dma-desc",
			    p->g_dma, false,
			    !!hw->dma_desc_enable);
}

/**
 * dwc2_set_parameters() - Set all core parameters.
 *
 * @hsotg: Programming view of the DWC_otg controller
 * @params: The parameters to set
 */
static void dwc2_set_parameters(struct dwc2_hsotg *hsotg,
				const struct dwc2_core_params *params)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	struct dwc2_core_params *p = &hsotg->params;
	bool dma_capable = !(hw->arch == GHWCFG2_SLAVE_ONLY_ARCH);

	dwc2_set_param_otg_cap(hsotg, params->otg_cap);
	if ((hsotg->dr_mode == USB_DR_MODE_HOST) ||
	    (hsotg->dr_mode == USB_DR_MODE_OTG)) {
		dev_dbg(hsotg->dev, "Setting HOST parameters\n");

		dwc2_set_param_bool(hsotg, &p->host_dma,
				    false, "host-dma",
				    true, false,
				    dma_capable);
	}
	dwc2_set_param_dma_desc_enable(hsotg, params->dma_desc_enable);
	dwc2_set_param_dma_desc_fs_enable(hsotg, params->dma_desc_fs_enable);

	dwc2_set_param_host_support_fs_ls_low_power(hsotg,
			params->host_support_fs_ls_low_power);
	dwc2_set_param_enable_dynamic_fifo(hsotg,
			params->enable_dynamic_fifo);
	dwc2_set_param_host_rx_fifo_size(hsotg,
			params->host_rx_fifo_size);
	dwc2_set_param_host_nperio_tx_fifo_size(hsotg,
			params->host_nperio_tx_fifo_size);
	dwc2_set_param_host_perio_tx_fifo_size(hsotg,
			params->host_perio_tx_fifo_size);
	dwc2_set_param_max_transfer_size(hsotg,
			params->max_transfer_size);
	dwc2_set_param_max_packet_count(hsotg,
			params->max_packet_count);
	dwc2_set_param_host_channels(hsotg, params->host_channels);
	dwc2_set_param_phy_type(hsotg, params->phy_type);
	dwc2_set_param_speed(hsotg, params->speed);
	dwc2_set_param_host_ls_low_power_phy_clk(hsotg,
			params->host_ls_low_power_phy_clk);
	dwc2_set_param_phy_ulpi_ddr(hsotg, params->phy_ulpi_ddr);
	dwc2_set_param_phy_ulpi_ext_vbus(hsotg,
			params->phy_ulpi_ext_vbus);
	dwc2_set_param_phy_utmi_width(hsotg, params->phy_utmi_width);
	dwc2_set_param_ulpi_fs_ls(hsotg, params->ulpi_fs_ls);
	dwc2_set_param_ts_dline(hsotg, params->ts_dline);
	dwc2_set_param_i2c_enable(hsotg, params->i2c_enable);
	dwc2_set_param_en_multiple_tx_fifo(hsotg,
			params->en_multiple_tx_fifo);
	dwc2_set_param_reload_ctl(hsotg, params->reload_ctl);
	dwc2_set_param_ahbcfg(hsotg, params->ahbcfg);
	dwc2_set_param_otg_ver(hsotg, params->otg_ver);
	dwc2_set_param_uframe_sched(hsotg, params->uframe_sched);
	dwc2_set_param_external_id_pin_ctl(hsotg, params->external_id_pin_ctl);
	dwc2_set_param_hibernation(hsotg, params->hibernation);

	/*
	 * Set devicetree-only parameters. These parameters do not
	 * take any values from @params.
	 */
	if ((hsotg->dr_mode == USB_DR_MODE_PERIPHERAL) ||
	    (hsotg->dr_mode == USB_DR_MODE_OTG)) {
		dev_dbg(hsotg->dev, "Setting peripheral device properties\n");

		dwc2_set_gadget_dma(hsotg);

		/*
		 * The values for g_rx_fifo_size (2048) and
		 * g_np_tx_fifo_size (1024) come from the legacy s3c
		 * gadget driver. These defaults have been hard-coded
		 * for some time so many platforms depend on these
		 * values. Leave them as defaults for now and only
		 * auto-detect if the hardware does not support the
		 * default.
		 */
		dwc2_set_param_u16(hsotg, &p->g_rx_fifo_size,
				   true, "g-rx-fifo-size", 2048,
				   hw->rx_fifo_size,
				   16, hw->rx_fifo_size);

		dwc2_set_param_u16(hsotg, &p->g_np_tx_fifo_size,
				   true, "g-np-tx-fifo-size", 1024,
				   hw->dev_nperio_tx_fifo_size,
				   16, hw->dev_nperio_tx_fifo_size);

		dwc2_set_param_tx_fifo_sizes(hsotg);
	}
}

/*
 * Gets host hardware parameters. Forces host mode if not currently in
 * host mode. Should be called immediately after a core soft reset in
 * order to get the reset values.
 */
static void dwc2_get_host_hwparams(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	u32 gnptxfsiz;
	u32 hptxfsiz;
	bool forced;

	if (hsotg->dr_mode == USB_DR_MODE_PERIPHERAL)
		return;

	forced = dwc2_force_mode_if_needed(hsotg, true);

	gnptxfsiz = dwc2_readl(hsotg->regs + GNPTXFSIZ);
	hptxfsiz = dwc2_readl(hsotg->regs + HPTXFSIZ);
	dev_dbg(hsotg->dev, "gnptxfsiz=%08x\n", gnptxfsiz);
	dev_dbg(hsotg->dev, "hptxfsiz=%08x\n", hptxfsiz);

	if (forced)
		dwc2_clear_force_mode(hsotg);

	hw->host_nperio_tx_fifo_size = (gnptxfsiz & FIFOSIZE_DEPTH_MASK) >>
				       FIFOSIZE_DEPTH_SHIFT;
	hw->host_perio_tx_fifo_size = (hptxfsiz & FIFOSIZE_DEPTH_MASK) >>
				      FIFOSIZE_DEPTH_SHIFT;
}

/*
 * Gets device hardware parameters. Forces device mode if not
 * currently in device mode. Should be called immediately after a core
 * soft reset in order to get the reset values.
 */
static void dwc2_get_dev_hwparams(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	bool forced;
	u32 gnptxfsiz;

	if (hsotg->dr_mode == USB_DR_MODE_HOST)
		return;

	forced = dwc2_force_mode_if_needed(hsotg, false);

	gnptxfsiz = dwc2_readl(hsotg->regs + GNPTXFSIZ);
	dev_dbg(hsotg->dev, "gnptxfsiz=%08x\n", gnptxfsiz);

	if (forced)
		dwc2_clear_force_mode(hsotg);

	hw->dev_nperio_tx_fifo_size = (gnptxfsiz & FIFOSIZE_DEPTH_MASK) >>
				       FIFOSIZE_DEPTH_SHIFT;
}

/**
 * During device initialization, read various hardware configuration
 * registers and interpret the contents.
 */
int dwc2_get_hwparams(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	unsigned int width;
	u32 hwcfg1, hwcfg2, hwcfg3, hwcfg4;
	u32 grxfsiz;

	/*
	 * Attempt to ensure this device is really a DWC_otg Controller.
	 * Read and verify the GSNPSID register contents. The value should be
	 * 0x45f42xxx or 0x45f43xxx, which corresponds to either "OT2" or "OT3",
	 * as in "OTG version 2.xx" or "OTG version 3.xx".
	 */
	hw->snpsid = dwc2_readl(hsotg->regs + GSNPSID);
	if ((hw->snpsid & 0xfffff000) != 0x4f542000 &&
	    (hw->snpsid & 0xfffff000) != 0x4f543000 &&
	    (hw->snpsid & 0xffff0000) != 0x55310000 &&
	    (hw->snpsid & 0xffff0000) != 0x55320000) {
		dev_err(hsotg->dev, "Bad value for GSNPSID: 0x%08x\n",
			hw->snpsid);
		return -ENODEV;
	}

	dev_dbg(hsotg->dev, "Core Release: %1x.%1x%1x%1x (snpsid=%x)\n",
		hw->snpsid >> 12 & 0xf, hw->snpsid >> 8 & 0xf,
		hw->snpsid >> 4 & 0xf, hw->snpsid & 0xf, hw->snpsid);

	hwcfg1 = dwc2_readl(hsotg->regs + GHWCFG1);
	hwcfg2 = dwc2_readl(hsotg->regs + GHWCFG2);
	hwcfg3 = dwc2_readl(hsotg->regs + GHWCFG3);
	hwcfg4 = dwc2_readl(hsotg->regs + GHWCFG4);
	grxfsiz = dwc2_readl(hsotg->regs + GRXFSIZ);

	dev_dbg(hsotg->dev, "hwcfg1=%08x\n", hwcfg1);
	dev_dbg(hsotg->dev, "hwcfg2=%08x\n", hwcfg2);
	dev_dbg(hsotg->dev, "hwcfg3=%08x\n", hwcfg3);
	dev_dbg(hsotg->dev, "hwcfg4=%08x\n", hwcfg4);
	dev_dbg(hsotg->dev, "grxfsiz=%08x\n", grxfsiz);

	/*
	 * Host specific hardware parameters. Reading these parameters
	 * requires the controller to be in host mode. The mode will
	 * be forced, if necessary, to read these values.
	 */
	dwc2_get_host_hwparams(hsotg);
	dwc2_get_dev_hwparams(hsotg);

	/* hwcfg1 */
	hw->dev_ep_dirs = hwcfg1;

	/* hwcfg2 */
	hw->op_mode = (hwcfg2 & GHWCFG2_OP_MODE_MASK) >>
		      GHWCFG2_OP_MODE_SHIFT;
	hw->arch = (hwcfg2 & GHWCFG2_ARCHITECTURE_MASK) >>
		   GHWCFG2_ARCHITECTURE_SHIFT;
	hw->enable_dynamic_fifo = !!(hwcfg2 & GHWCFG2_DYNAMIC_FIFO);
	hw->host_channels = 1 + ((hwcfg2 & GHWCFG2_NUM_HOST_CHAN_MASK) >>
				GHWCFG2_NUM_HOST_CHAN_SHIFT);
	hw->hs_phy_type = (hwcfg2 & GHWCFG2_HS_PHY_TYPE_MASK) >>
			  GHWCFG2_HS_PHY_TYPE_SHIFT;
	hw->fs_phy_type = (hwcfg2 & GHWCFG2_FS_PHY_TYPE_MASK) >>
			  GHWCFG2_FS_PHY_TYPE_SHIFT;
	hw->num_dev_ep = (hwcfg2 & GHWCFG2_NUM_DEV_EP_MASK) >>
			 GHWCFG2_NUM_DEV_EP_SHIFT;
	hw->nperio_tx_q_depth =
		(hwcfg2 & GHWCFG2_NONPERIO_TX_Q_DEPTH_MASK) >>
		GHWCFG2_NONPERIO_TX_Q_DEPTH_SHIFT << 1;
	hw->host_perio_tx_q_depth =
		(hwcfg2 & GHWCFG2_HOST_PERIO_TX_Q_DEPTH_MASK) >>
		GHWCFG2_HOST_PERIO_TX_Q_DEPTH_SHIFT << 1;
	hw->dev_token_q_depth =
		(hwcfg2 & GHWCFG2_DEV_TOKEN_Q_DEPTH_MASK) >>
		GHWCFG2_DEV_TOKEN_Q_DEPTH_SHIFT;

	/* hwcfg3 */
	width = (hwcfg3 & GHWCFG3_XFER_SIZE_CNTR_WIDTH_MASK) >>
		GHWCFG3_XFER_SIZE_CNTR_WIDTH_SHIFT;
	hw->max_transfer_size = (1 << (width + 11)) - 1;
	width = (hwcfg3 & GHWCFG3_PACKET_SIZE_CNTR_WIDTH_MASK) >>
		GHWCFG3_PACKET_SIZE_CNTR_WIDTH_SHIFT;
	hw->max_packet_count = (1 << (width + 4)) - 1;
	hw->i2c_enable = !!(hwcfg3 & GHWCFG3_I2C);
	hw->total_fifo_size = (hwcfg3 & GHWCFG3_DFIFO_DEPTH_MASK) >>
			      GHWCFG3_DFIFO_DEPTH_SHIFT;

	/* hwcfg4 */
	hw->en_multiple_tx_fifo = !!(hwcfg4 & GHWCFG4_DED_FIFO_EN);
	hw->num_dev_perio_in_ep = (hwcfg4 & GHWCFG4_NUM_DEV_PERIO_IN_EP_MASK) >>
				  GHWCFG4_NUM_DEV_PERIO_IN_EP_SHIFT;
	hw->dma_desc_enable = !!(hwcfg4 & GHWCFG4_DESC_DMA);
	hw->power_optimized = !!(hwcfg4 & GHWCFG4_POWER_OPTIMIZ);
	hw->utmi_phy_data_width = (hwcfg4 & GHWCFG4_UTMI_PHY_DATA_WIDTH_MASK) >>
				  GHWCFG4_UTMI_PHY_DATA_WIDTH_SHIFT;

	/* fifo sizes */
	hw->rx_fifo_size = (grxfsiz & GRXFSIZ_DEPTH_MASK) >>
				GRXFSIZ_DEPTH_SHIFT;

	dev_dbg(hsotg->dev, "Detected values from hardware:\n");
	dev_dbg(hsotg->dev, "  op_mode=%d\n",
		hw->op_mode);
	dev_dbg(hsotg->dev, "  arch=%d\n",
		hw->arch);
	dev_dbg(hsotg->dev, "  dma_desc_enable=%d\n",
		hw->dma_desc_enable);
	dev_dbg(hsotg->dev, "  power_optimized=%d\n",
		hw->power_optimized);
	dev_dbg(hsotg->dev, "  i2c_enable=%d\n",
		hw->i2c_enable);
	dev_dbg(hsotg->dev, "  hs_phy_type=%d\n",
		hw->hs_phy_type);
	dev_dbg(hsotg->dev, "  fs_phy_type=%d\n",
		hw->fs_phy_type);
	dev_dbg(hsotg->dev, "  utmi_phy_data_width=%d\n",
		hw->utmi_phy_data_width);
	dev_dbg(hsotg->dev, "  num_dev_ep=%d\n",
		hw->num_dev_ep);
	dev_dbg(hsotg->dev, "  num_dev_perio_in_ep=%d\n",
		hw->num_dev_perio_in_ep);
	dev_dbg(hsotg->dev, "  host_channels=%d\n",
		hw->host_channels);
	dev_dbg(hsotg->dev, "  max_transfer_size=%d\n",
		hw->max_transfer_size);
	dev_dbg(hsotg->dev, "  max_packet_count=%d\n",
		hw->max_packet_count);
	dev_dbg(hsotg->dev, "  nperio_tx_q_depth=0x%0x\n",
		hw->nperio_tx_q_depth);
	dev_dbg(hsotg->dev, "  host_perio_tx_q_depth=0x%0x\n",
		hw->host_perio_tx_q_depth);
	dev_dbg(hsotg->dev, "  dev_token_q_depth=0x%0x\n",
		hw->dev_token_q_depth);
	dev_dbg(hsotg->dev, "  enable_dynamic_fifo=%d\n",
		hw->enable_dynamic_fifo);
	dev_dbg(hsotg->dev, "  en_multiple_tx_fifo=%d\n",
		hw->en_multiple_tx_fifo);
	dev_dbg(hsotg->dev, "  total_fifo_size=%d\n",
		hw->total_fifo_size);
	dev_dbg(hsotg->dev, "  rx_fifo_size=%d\n",
		hw->rx_fifo_size);
	dev_dbg(hsotg->dev, "  host_nperio_tx_fifo_size=%d\n",
		hw->host_nperio_tx_fifo_size);
	dev_dbg(hsotg->dev, "  host_perio_tx_fifo_size=%d\n",
		hw->host_perio_tx_fifo_size);
	dev_dbg(hsotg->dev, "\n");

	return 0;
}

int dwc2_init_params(struct dwc2_hsotg *hsotg)
{
	const struct of_device_id *match;
	struct dwc2_core_params params;

	match = of_match_device(dwc2_of_match_table, hsotg->dev);
	if (match && match->data)
		params = *((struct dwc2_core_params *)match->data);
	else
		params = params_default;

	if (dwc2_is_fs_iot(hsotg)) {
		params.speed = DWC2_SPEED_PARAM_FULL;
		params.phy_type = DWC2_PHY_TYPE_PARAM_FS;
	}

	dwc2_set_parameters(hsotg, &params);

	return 0;
}
