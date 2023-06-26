// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#include "rk628.h"

#include "rk628_combtxphy.h"
#include "rk628_config.h"
#include "panel.h"

static inline void lvds_write(struct rk628 *rk628, u32 reg, u32 val)
{
	rk628_i2c_write(rk628, reg, val);
}

static inline void lvds_update_bits(struct rk628 *rk628, u32 reg,
				    u32 mask, u32 val)
{
	rk628_i2c_update_bits(rk628, reg, mask, val);
}

int rk628_lvds_parse(struct rk628 *rk628, struct device_node *lvds_np)
{
	const char *string;
	int ret;

	if (!of_device_is_available(lvds_np))
		return -EINVAL;

	rk628->output_mode = OUTPUT_MODE_LVDS;

	if (!of_property_read_string(lvds_np, "bus-format", &string)) {
		if (!strcmp(string, "jeida_24"))
			rk628->lvds.format = LVDS_FORMAT_JEIDA_24BIT;
		else if (!strcmp(string, "jeida_18"))
			rk628->lvds.format = LVDS_FORMAT_JEIDA_18BIT;
		else if (!strcmp(string, "vesa_18"))
			rk628->lvds.format = LVDS_FORMAT_VESA_18BIT;
		else
			rk628->lvds.format = LVDS_FORMAT_VESA_24BIT;
	}

	if (!of_property_read_string(lvds_np, "link-type", &string)) {
		if (!strcmp(string, "dual_link_odd_even_pixels"))
			rk628->lvds.link_type = LVDS_DUAL_LINK_ODD_EVEN_PIXELS;
		else if (!strcmp(string, "dual_link_even_odd_pixels"))
			rk628->lvds.link_type = LVDS_DUAL_LINK_EVEN_ODD_PIXELS;
		else if (!strcmp(string, "dual_link_left_right_pixels"))
			rk628->lvds.link_type = LVDS_DUAL_LINK_LEFT_RIGHT_PIXELS;
		else if (!strcmp(string, "dual_link_right_left_pixels"))
			rk628->lvds.link_type = LVDS_DUAL_LINK_RIGHT_LEFT_PIXELS;
		else
			rk628->lvds.link_type = LVDS_SINGLE_LINK;
	}

	ret = rk628_panel_info_get(rk628, lvds_np);
	if (ret)
		return ret;

	return 0;
}

void rk628_lvds_enable(struct rk628 *rk628)
{
	enum lvds_link_type link_type = rk628->lvds.link_type;
	enum lvds_format format = rk628->lvds.format;
	const struct rk628_display_mode *mode = &rk628->dst_mode;
	u32 val, bus_width;

	lvds_update_bits(rk628, GRF_SYSTEM_CON0, SW_OUTPUT_MODE_MASK,
			 SW_OUTPUT_MODE(OUTPUT_MODE_LVDS));

	switch (link_type) {
	case LVDS_DUAL_LINK_ODD_EVEN_PIXELS:
		val = SW_LVDS_CON_CHASEL(1) | SW_LVDS_CON_STARTSEL(0) |
		      SW_LVDS_CON_DUAL_SEL(0);
		bus_width = COMBTXPHY_MODULEA_EN | COMBTXPHY_MODULEB_EN;
		break;
	case LVDS_DUAL_LINK_EVEN_ODD_PIXELS:
		val = SW_LVDS_CON_CHASEL(1) | SW_LVDS_CON_STARTSEL(1) |
		      SW_LVDS_CON_DUAL_SEL(0);
		bus_width = COMBTXPHY_MODULEA_EN | COMBTXPHY_MODULEB_EN;
		break;
	case LVDS_DUAL_LINK_LEFT_RIGHT_PIXELS:
		val = SW_LVDS_CON_CHASEL(1) | SW_LVDS_CON_STARTSEL(0) |
		      SW_LVDS_CON_DUAL_SEL(1);
		lvds_update_bits(rk628, GRF_POST_PROC_CON,
				 SW_SPLIT_EN, SW_SPLIT_EN);
		bus_width = COMBTXPHY_MODULEA_EN | COMBTXPHY_MODULEB_EN;
		break;
	case LVDS_DUAL_LINK_RIGHT_LEFT_PIXELS:
		val = SW_LVDS_CON_CHASEL(1) | SW_LVDS_CON_STARTSEL(1) |
		      SW_LVDS_CON_DUAL_SEL(1);
		lvds_update_bits(rk628, GRF_POST_PROC_CON,
				 SW_SPLIT_EN, SW_SPLIT_EN);
		bus_width = COMBTXPHY_MODULEA_EN | COMBTXPHY_MODULEB_EN;
		break;
	case LVDS_SINGLE_LINK:
	default:
		val = SW_LVDS_CON_CHASEL(0) | SW_LVDS_CON_STARTSEL(0) |
		      SW_LVDS_CON_DUAL_SEL(0);
		bus_width = COMBTXPHY_MODULEA_EN;
		break;
	}

	val |= SW_LVDS_CON_SELECT(format) | SW_LVDS_CON_MSBSEL(0) |
	       SW_LVDS_CON_CLKINV(0);
	lvds_write(rk628, GRF_LVDS_TX_CON, val);

	bus_width |= (mode->clock / 1000) << 8;
	rk628_combtxphy_set_bus_width(rk628, bus_width);
	rk628_combtxphy_set_mode(rk628, PHY_MODE_VIDEO_LVDS);
	rk628_combtxphy_power_on(rk628);
	rk628_panel_prepare(rk628);
	rk628_panel_enable(rk628);
}

void rk628_lvds_disable(struct rk628 *rk628)
{
	rk628_panel_disable(rk628);
	rk628_panel_unprepare(rk628);
	rk628_combtxphy_power_off(rk628);
}
