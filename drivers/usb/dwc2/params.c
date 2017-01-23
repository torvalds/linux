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

static void dwc2_set_param_otg_cap(struct dwc2_hsotg *hsotg)
{
	u8 val;

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

	hsotg->params.otg_cap = val;
}

static void dwc2_set_param_phy_type(struct dwc2_hsotg *hsotg)
{
	int val;
	u32 hs_phy_type = hsotg->hw_params.hs_phy_type;

	val = DWC2_PHY_TYPE_PARAM_FS;
	if (hs_phy_type != GHWCFG2_HS_PHY_TYPE_NOT_SUPPORTED) {
		if (hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI ||
		    hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI)
			val = DWC2_PHY_TYPE_PARAM_UTMI;
		else
			val = DWC2_PHY_TYPE_PARAM_ULPI;
	}

	if (dwc2_is_fs_iot(hsotg))
		hsotg->params.phy_type = DWC2_PHY_TYPE_PARAM_FS;

	hsotg->params.phy_type = val;
}

static void dwc2_set_param_speed(struct dwc2_hsotg *hsotg)
{
	int val;

	val = hsotg->params.phy_type == DWC2_PHY_TYPE_PARAM_FS ?
		DWC2_SPEED_PARAM_FULL : DWC2_SPEED_PARAM_HIGH;

	if (dwc2_is_fs_iot(hsotg))
		val = DWC2_SPEED_PARAM_FULL;

	if (dwc2_is_hs_iot(hsotg))
		val = DWC2_SPEED_PARAM_HIGH;

	hsotg->params.speed = val;
}

static void dwc2_set_param_phy_utmi_width(struct dwc2_hsotg *hsotg)
{
	int val;

	val = (hsotg->hw_params.utmi_phy_data_width ==
	       GHWCFG4_UTMI_PHY_DATA_WIDTH_8) ? 8 : 16;

	hsotg->params.phy_utmi_width = val;
}

static void dwc2_set_param_tx_fifo_sizes(struct dwc2_hsotg *hsotg)
{
	struct dwc2_core_params *p = &hsotg->params;
	u32 p_tx_fifo[] = DWC2_G_P_LEGACY_TX_FIFO_SIZE;

	memset(p->g_tx_fifo_size, 0, sizeof(p->g_tx_fifo_size));
	memcpy(&p->g_tx_fifo_size[1],
	       p_tx_fifo,
	       sizeof(p_tx_fifo));
}

/**
 * dwc2_set_default_params() - Set all core parameters to their
 * auto-detected default values.
 */
static void dwc2_set_default_params(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	struct dwc2_core_params *p = &hsotg->params;
	bool dma_capable = !(hw->arch == GHWCFG2_SLAVE_ONLY_ARCH);

	dwc2_set_param_otg_cap(hsotg);
	dwc2_set_param_phy_type(hsotg);
	dwc2_set_param_speed(hsotg);
	dwc2_set_param_phy_utmi_width(hsotg);
	p->phy_ulpi_ddr = false;
	p->phy_ulpi_ext_vbus = false;

	p->enable_dynamic_fifo = hw->enable_dynamic_fifo;
	p->en_multiple_tx_fifo = hw->en_multiple_tx_fifo;
	p->i2c_enable = hw->i2c_enable;
	p->ulpi_fs_ls = false;
	p->ts_dline = false;
	p->reload_ctl = (hw->snpsid >= DWC2_CORE_REV_2_92a);
	p->uframe_sched = true;
	p->external_id_pin_ctl = false;
	p->hibernation = false;
	p->max_packet_count = hw->max_packet_count;
	p->max_transfer_size = hw->max_transfer_size;
	p->ahbcfg = GAHBCFG_HBSTLEN_INCR4 << GAHBCFG_HBSTLEN_SHIFT;

	if ((hsotg->dr_mode == USB_DR_MODE_HOST) ||
	    (hsotg->dr_mode == USB_DR_MODE_OTG)) {
		p->host_dma = dma_capable;
		p->dma_desc_enable = false;
		p->dma_desc_fs_enable = false;
		p->host_support_fs_ls_low_power = false;
		p->host_ls_low_power_phy_clk = false;
		p->host_channels = hw->host_channels;
		p->host_rx_fifo_size = hw->rx_fifo_size;
		p->host_nperio_tx_fifo_size = hw->host_nperio_tx_fifo_size;
		p->host_perio_tx_fifo_size = hw->host_perio_tx_fifo_size;
	}

	if ((hsotg->dr_mode == USB_DR_MODE_PERIPHERAL) ||
	    (hsotg->dr_mode == USB_DR_MODE_OTG)) {
		p->g_dma = dma_capable;
		p->g_dma_desc = hw->dma_desc_enable;

		/*
		 * The values for g_rx_fifo_size (2048) and
		 * g_np_tx_fifo_size (1024) come from the legacy s3c
		 * gadget driver. These defaults have been hard-coded
		 * for some time so many platforms depend on these
		 * values. Leave them as defaults for now and only
		 * auto-detect if the hardware does not support the
		 * default.
		 */
		p->g_rx_fifo_size = 2048;
		p->g_np_tx_fifo_size = 1024;
		dwc2_set_param_tx_fifo_sizes(hsotg);
	}
}

/**
 * dwc2_get_device_properties() - Read in device properties.
 *
 * Read in the device properties and adjust core parameters if needed.
 */
static void dwc2_get_device_properties(struct dwc2_hsotg *hsotg)
{
	struct dwc2_core_params *p = &hsotg->params;
	int num;

	if ((hsotg->dr_mode == USB_DR_MODE_PERIPHERAL) ||
	    (hsotg->dr_mode == USB_DR_MODE_OTG)) {
		device_property_read_u32(hsotg->dev, "g-rx-fifo-size",
					 &p->g_rx_fifo_size);

		device_property_read_u32(hsotg->dev, "g-np-tx-fifo-size",
					 &p->g_np_tx_fifo_size);

		num = device_property_read_u32_array(hsotg->dev,
						     "g-tx-fifo-size",
						     NULL, 0);

		if (num > 0) {
			num = min(num, 15);
			memset(p->g_tx_fifo_size, 0,
			       sizeof(p->g_tx_fifo_size));
			device_property_read_u32_array(hsotg->dev,
						       "g-tx-fifo-size",
						       &p->g_tx_fifo_size[1],
						       num);
		}
	}
}

static void dwc2_check_param_otg_cap(struct dwc2_hsotg *hsotg)
{
	int valid = 1;

	switch (hsotg->params.otg_cap) {
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

	if (!valid)
		dwc2_set_param_otg_cap(hsotg);
}

static void dwc2_check_param_phy_type(struct dwc2_hsotg *hsotg)
{
	int valid = 0;
	u32 hs_phy_type;
	u32 fs_phy_type;

	hs_phy_type = hsotg->hw_params.hs_phy_type;
	fs_phy_type = hsotg->hw_params.fs_phy_type;

	switch (hsotg->params.phy_type) {
	case DWC2_PHY_TYPE_PARAM_FS:
		if (fs_phy_type == GHWCFG2_FS_PHY_TYPE_DEDICATED)
			valid = 1;
		break;
	case DWC2_PHY_TYPE_PARAM_UTMI:
		if ((hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI) ||
		    (hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI))
			valid = 1;
		break;
	case DWC2_PHY_TYPE_PARAM_ULPI:
		if ((hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI) ||
		    (hs_phy_type == GHWCFG2_HS_PHY_TYPE_UTMI_ULPI))
			valid = 1;
		break;
	default:
		break;
	}

	if (!valid)
		dwc2_set_param_phy_type(hsotg);
}

static void dwc2_check_param_speed(struct dwc2_hsotg *hsotg)
{
	int valid = 1;
	int phy_type = hsotg->params.phy_type;
	int speed = hsotg->params.speed;

	switch (speed) {
	case DWC2_SPEED_PARAM_HIGH:
		if ((hsotg->params.speed == DWC2_SPEED_PARAM_HIGH) &&
		    (phy_type == DWC2_PHY_TYPE_PARAM_FS))
			valid = 0;
		break;
	case DWC2_SPEED_PARAM_FULL:
	case DWC2_SPEED_PARAM_LOW:
		break;
	default:
		valid = 0;
		break;
	}

	if (!valid)
		dwc2_set_param_speed(hsotg);
}

static void dwc2_check_param_phy_utmi_width(struct dwc2_hsotg *hsotg)
{
	int valid = 0;
	int param = hsotg->params.phy_utmi_width;
	int width = hsotg->hw_params.utmi_phy_data_width;

	switch (width) {
	case GHWCFG4_UTMI_PHY_DATA_WIDTH_8:
		valid = (param == 8);
		break;
	case GHWCFG4_UTMI_PHY_DATA_WIDTH_16:
		valid = (param == 16);
		break;
	case GHWCFG4_UTMI_PHY_DATA_WIDTH_8_OR_16:
		valid = (param == 8 || param == 16);
		break;
	}

	if (!valid)
		dwc2_set_param_phy_utmi_width(hsotg);
}

#define CHECK_RANGE(_param, _min, _max, _def) do {			\
		if ((hsotg->params._param) < (_min) ||			\
		    (hsotg->params._param) > (_max)) {			\
			dev_warn(hsotg->dev, "%s: Invalid parameter %s=%d\n", \
				 __func__, #_param, hsotg->params._param); \
			hsotg->params._param = (_def);			\
		}							\
	} while (0)

#define CHECK_BOOL(_param, _check) do {					\
		if (hsotg->params._param && !(_check)) {		\
			dev_warn(hsotg->dev, "%s: Invalid parameter %s=%d\n", \
				 __func__, #_param, hsotg->params._param); \
			hsotg->params._param = false;			\
		}							\
	} while (0)

static void dwc2_check_params(struct dwc2_hsotg *hsotg)
{
	struct dwc2_hw_params *hw = &hsotg->hw_params;
	struct dwc2_core_params *p = &hsotg->params;
	bool dma_capable = !(hw->arch == GHWCFG2_SLAVE_ONLY_ARCH);

	dwc2_check_param_otg_cap(hsotg);
	dwc2_check_param_phy_type(hsotg);
	dwc2_check_param_speed(hsotg);
	dwc2_check_param_phy_utmi_width(hsotg);
	CHECK_BOOL(enable_dynamic_fifo, hw->enable_dynamic_fifo);
	CHECK_BOOL(en_multiple_tx_fifo, hw->en_multiple_tx_fifo);
	CHECK_BOOL(i2c_enable, hw->i2c_enable);
	CHECK_BOOL(reload_ctl, (hsotg->hw_params.snpsid > DWC2_CORE_REV_2_92a));
	CHECK_RANGE(max_packet_count,
		    15, hw->max_packet_count,
		    hw->max_packet_count);
	CHECK_RANGE(max_transfer_size,
		    2047, hw->max_transfer_size,
		    hw->max_transfer_size);

	if ((hsotg->dr_mode == USB_DR_MODE_HOST) ||
	    (hsotg->dr_mode == USB_DR_MODE_OTG)) {
		CHECK_BOOL(host_dma, dma_capable);
		CHECK_BOOL(dma_desc_enable, p->host_dma);
		CHECK_BOOL(dma_desc_fs_enable, p->dma_desc_enable);
		CHECK_BOOL(host_ls_low_power_phy_clk,
			   p->phy_type == DWC2_PHY_TYPE_PARAM_FS);
		CHECK_RANGE(host_channels,
			    1, hw->host_channels,
			    hw->host_channels);
		CHECK_RANGE(host_rx_fifo_size,
			    16, hw->rx_fifo_size,
			    hw->rx_fifo_size);
		CHECK_RANGE(host_nperio_tx_fifo_size,
			    16, hw->host_nperio_tx_fifo_size,
			    hw->host_nperio_tx_fifo_size);
		CHECK_RANGE(host_perio_tx_fifo_size,
			    16, hw->host_perio_tx_fifo_size,
			    hw->host_perio_tx_fifo_size);
	}

	if ((hsotg->dr_mode == USB_DR_MODE_PERIPHERAL) ||
	    (hsotg->dr_mode == USB_DR_MODE_OTG)) {
		CHECK_BOOL(g_dma, dma_capable);
		CHECK_BOOL(g_dma_desc, (p->g_dma && hw->dma_desc_enable));
		CHECK_RANGE(g_rx_fifo_size,
			    16, hw->rx_fifo_size,
			    hw->rx_fifo_size);
		CHECK_RANGE(g_np_tx_fifo_size,
			    16, hw->dev_nperio_tx_fifo_size,
			    hw->dev_nperio_tx_fifo_size);
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
	dwc2_set_default_params(hsotg);
	dwc2_get_device_properties(hsotg);

	dwc2_check_params(hsotg);

	return 0;
}
