// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Rockchip Electronics Co. Ltd.
 *
 * Author: Zhang Yubing <yubing.zhang@rock-chips.com>
 */

#include "hal/cru_api.h"
#include "rkx110_x120.h"
#include "rkx110_x120_display.h"
#include "rkx110_dsi_rx.h"
#include "rkx120_dsi_tx.h"

int rk_serdes_display_route_prepare(struct rk_serdes *serdes, struct rk_serdes_route *route)
{
	u32 local_port;

	local_port = route->local_port0 ? route->local_port0 : route->local_port1;

	switch (local_port) {
	case RK_SERDES_RGB_RX:
		rkx110_rgb_rx_enable(serdes, route);
		break;
	case RK_SERDES_LVDS_RX0:
		rkx110_lvds_rx_enable(serdes, route, 0);
		break;
	case RK_SERDES_LVDS_RX1:
		rkx110_lvds_rx_enable(serdes, route, 1);
		break;
	case RK_SERDES_DUAL_LVDS_RX:
		rkx110_lvds_rx_enable(serdes, route, 0);
		rkx110_lvds_rx_enable(serdes, route, 1);
		break;
	case RK_SERDES_DSI_RX0:
		rkx110_dsi_rx_enable(serdes, route, 0);
		break;
	case RK_SERDES_DSI_RX1:
		rkx110_dsi_rx_enable(serdes, route, 1);
		break;
	default:
		dev_info(serdes->dev, "undefined local port\n");
	}

	rkx110_display_linktx_enable(serdes, route);

	if (route->local_port0) {
		rkx120_display_linkrx_enable(serdes, route, DEVICE_REMOTE0);
		if (serdes->remote_nr == 2 && serdes->route_nr != 2)
			rkx120_display_linkrx_enable(serdes, route, DEVICE_REMOTE1);

		if (route->remote0_port0 & RK_SERDES_DSI_TX0)
			rkx120_dsi_tx_pre_enable(serdes, route, DEVICE_REMOTE0);
		if (route->remote1_port0 & RK_SERDES_DSI_TX0)
			rkx120_dsi_tx_pre_enable(serdes, route, DEVICE_REMOTE1);
	}

	if (route->local_port1) {
		if (serdes->remote_nr == 2)
			rkx120_display_linkrx_enable(serdes, route, DEVICE_REMOTE1);
		else
			rkx120_display_linkrx_enable(serdes, route, DEVICE_REMOTE0);

		if (route->remote1_port0 & RK_SERDES_DSI_TX0)
			rkx120_dsi_tx_pre_enable(serdes, route, DEVICE_REMOTE1);
	}

	return 0;
}

int rk_serdes_display_video_start(struct rk_serdes *serdes,
					 struct rk_serdes_route *route, bool enable)
{
	if (route->local_port0) {
		if (route->route_flag & ROUTE_MULTI_CHANNEL) {
			if (route->route_flag & ROUTE_MULTI_REMOTE) {
				rkx120_linkrx_engine_enable(serdes, 0, DEVICE_REMOTE0, enable);
				rkx120_linkrx_engine_enable(serdes, 0, DEVICE_REMOTE1, enable);
			} else {
				rkx120_linkrx_engine_enable(serdes, 0, DEVICE_REMOTE0, enable);
				rkx120_linkrx_engine_enable(serdes, 1, DEVICE_REMOTE0, enable);
			}
			rkx110_linktx_channel_enable(serdes, 0, DEVICE_LOCAL, enable);
			rkx110_linktx_channel_enable(serdes, 1, DEVICE_LOCAL, enable);
		} else {
			rkx120_linkrx_engine_enable(serdes, 0, DEVICE_REMOTE0, enable);
			rkx110_linktx_channel_enable(serdes, 0, DEVICE_LOCAL, enable);
		}
	} else {
		if (serdes->remote_nr == 2)
			rkx120_linkrx_engine_enable(serdes, 0, DEVICE_REMOTE1, enable);
		else
			rkx120_linkrx_engine_enable(serdes, 1, DEVICE_REMOTE0, enable);

		rkx110_linktx_channel_enable(serdes, 1, DEVICE_LOCAL, enable);
	}

	return 0;
}

int rk_serdes_display_route_init(struct  rk_serdes *serdes)
{
	rkx120_linkrx_engine_enable(serdes, 0, DEVICE_REMOTE0, false);
	if (serdes->remote_nr == 2)
		rkx120_linkrx_engine_enable(serdes, 0, DEVICE_REMOTE1, false);
	else
		rkx120_linkrx_engine_enable(serdes, 1, DEVICE_REMOTE0, false);

	rkx110_linktx_channel_enable(serdes, 0, DEVICE_LOCAL, false);
	rkx110_linktx_channel_enable(serdes, 1, DEVICE_LOCAL, false);

	return 0;
}

int rk_serdes_display_route_enable(struct rk_serdes *serdes, struct rk_serdes_route *route)
{
	if (route->remote0_port0) {
		switch (route->remote0_port0) {
		case RK_SERDES_RGB_TX:
			rkx120_rgb_tx_enable(serdes, route, DEVICE_REMOTE0);
			break;
		case RK_SERDES_LVDS_TX0:
			rkx120_lvds_tx_enable(serdes, route, DEVICE_REMOTE0, 0);
			break;
		case RK_SERDES_LVDS_TX1:
			rkx120_lvds_tx_enable(serdes, route, DEVICE_REMOTE0, 1);
			break;
		case RK_SERDES_DUAL_LVDS_TX:
			rkx120_lvds_tx_enable(serdes, route, DEVICE_REMOTE0, 0);
			rkx120_lvds_tx_enable(serdes, route, DEVICE_REMOTE0, 1);
			break;
		case RK_SERDES_DSI_TX0:
			rkx120_dsi_tx_enable(serdes, route, DEVICE_REMOTE0);
			break;
		default:
			dev_err(serdes->dev, "undefined remote0_port0\n");
			break;
		}
	}

	if (route->remote1_port0) {
		switch (route->remote1_port0) {
		case RK_SERDES_RGB_TX:
			rkx120_rgb_tx_enable(serdes, route, DEVICE_REMOTE1);
			break;
		case RK_SERDES_LVDS_TX0:
			rkx120_lvds_tx_enable(serdes, route, DEVICE_REMOTE1, 0);
			break;
		case RK_SERDES_LVDS_TX1:
			rkx120_lvds_tx_enable(serdes, route, DEVICE_REMOTE1, 1);
			break;
		case RK_SERDES_DUAL_LVDS_TX:
			rkx120_lvds_tx_enable(serdes, route, DEVICE_REMOTE1, 0);
			rkx120_lvds_tx_enable(serdes, route, DEVICE_REMOTE1, 1);
			break;
		case RK_SERDES_DSI_TX0:
			rkx120_dsi_tx_enable(serdes, route, DEVICE_REMOTE1);
			break;
		default:
			dev_err(serdes->dev, "undefined remote1_port0\n");
			break;
		}
	}

	if (route->remote0_port1) {
		switch (route->remote0_port1) {
		case RK_SERDES_LVDS_TX0:
			rkx120_lvds_tx_enable(serdes, route, DEVICE_REMOTE0, 0);
			break;
		case RK_SERDES_LVDS_TX1:
			rkx120_lvds_tx_enable(serdes, route, DEVICE_REMOTE0, 1);
			break;
		default:
			dev_err(serdes->dev, "undefined remote0_port1\n");
			break;
		}
	}

	if (serdes->version == SERDES_V1)
		rk_serdes_display_video_start(serdes, route, true);

	rkx110_linktx_video_enable(serdes, DEVICE_LOCAL, true);

	return 0;
}

int rk_serdes_display_route_disable(struct rk_serdes *serdes, struct rk_serdes_route *route)
{
	if (route->remote0_port0 & RK_SERDES_DSI_TX0)
		rkx120_dsi_tx_disable(serdes, route, DEVICE_REMOTE0);

	if (route->remote1_port0 & RK_SERDES_DSI_TX0)
		rkx120_dsi_tx_disable(serdes, route, DEVICE_REMOTE1);

	if (serdes->version == SERDES_V1) {
		rk_serdes_display_video_start(serdes, route, false);

		if (route->local_port0 == RK_SERDES_DUAL_LVDS_RX) {
			rkx110_set_stream_source(serdes, RK_SERDES_RGB_RX,
						 DEVICE_LOCAL);
			hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
				    RKX110_SRST_RESETN_2X_LVDS_RKLINK_TX);
			hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
				    RKX110_SRST_RESETN_D_LVDS0_RKLINK_TX);
			hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
				    RKX110_SRST_RESETN_D_LVDS1_RKLINK_TX);
		}

		if ((route->local_port0 == RK_SERDES_DSI_RX0) ||
		    (route->local_port1 == RK_SERDES_DSI_RX0)) {
			serdes->i2c_write_reg(serdes->chip[DEVICE_LOCAL].client, 0x0314,
					      0x1400140);
			hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
				    RKX111_SRST_RESETN_D_DSI_0_REC_RKLINK_TX);
			hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
				    RKX110_SRST_RESETN_D_DSI_0_RKLINK_TX);
		}

		if ((route->local_port0 == RK_SERDES_DSI_RX1) ||
		    (route->local_port1 == RK_SERDES_DSI_RX1)) {
			serdes->i2c_write_reg(serdes->chip[DEVICE_LOCAL].client, 0x0314,
					      0x2800280);
			hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
				    RKX111_SRST_RESETN_D_DSI_1_REC_RKLINK_TX);
			hwclk_reset(serdes->chip[DEVICE_LOCAL].hwclk,
				    RKX110_SRST_RESETN_D_DSI_1_RKLINK_TX);
		}
	}

	return 0;
}

int rk_serdes_display_route_unprepare(struct rk_serdes *serdes, struct rk_serdes_route *route)
{
	if (route->remote0_port0 & RK_SERDES_DSI_TX0)
		rkx120_dsi_tx_post_disable(serdes, route, DEVICE_REMOTE0);

	if (route->remote1_port0 & RK_SERDES_DSI_TX0)
		rkx120_dsi_tx_post_disable(serdes, route, DEVICE_REMOTE1);

	if (serdes->version == SERDES_V1) {
		if (route->local_port0 == RK_SERDES_DUAL_LVDS_RX) {
			hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
					     RKX110_SRST_RESETN_2X_LVDS_RKLINK_TX);
			hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
					     RKX110_SRST_RESETN_D_LVDS0_RKLINK_TX);
			hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
					     RKX110_SRST_RESETN_D_LVDS1_RKLINK_TX);
			rkx110_set_stream_source(serdes, RK_SERDES_DUAL_LVDS_RX,
						    DEVICE_LOCAL);
		}

		if ((route->local_port0 == RK_SERDES_DSI_RX0) ||
		    (route->local_port1 == RK_SERDES_DSI_RX0)) {
			hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
					     RKX110_SRST_RESETN_D_DSI_0_RKLINK_TX);
			hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
					     RKX111_SRST_RESETN_D_DSI_0_REC_RKLINK_TX);
			serdes->i2c_write_reg(serdes->chip[DEVICE_LOCAL].client, 0x0314,
					      0x1400000);
		}

		if ((route->local_port0 == RK_SERDES_DSI_RX1) ||
		    (route->local_port1 == RK_SERDES_DSI_RX1)) {
			hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
					     RKX110_SRST_RESETN_D_DSI_1_RKLINK_TX);
			hwclk_reset_deassert(serdes->chip[DEVICE_LOCAL].hwclk,
					     RKX111_SRST_RESETN_D_DSI_1_REC_RKLINK_TX);
			serdes->i2c_write_reg(serdes->chip[DEVICE_LOCAL].client, 0x0314,
					      0x2800000);
		}
	}

	return 0;
}

