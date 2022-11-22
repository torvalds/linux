// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#include <asm/unaligned.h>
#include <dt-bindings/mfd/rockchip-serdes.h>
#include "rkx110_reg.h"
#include "rkx110_x120.h"
#include "rkx110_dsi_rx.h"
#include "serdes_combphy.h"

#define DSI_RX_MIPI_IDX_CTRL0(x)	(0x0100 + x * 8)
#define SW_COMMAND_MODE_EN		BIT(26)
#define SW_DT(x)			UPDATE(x, 15, 10)
#define SW_VC(x)			UPDATE(x, 9, 8)
#define SW_CAP_EN			BIT(0)
#define DSI_RX_MIPI_IDX_CTRL1(x)	(0X0104 + x * 8)
#define SW_HEIGHT(x)			UPDATE(x, 29, 16)
#define SW_WIDTH(x)			UPDATE(x, 13, 0)
#define DSI_RX_MIPI_INTEN		0x0174
#define DSI_RX_MIPI_INTSTAT		0x0178
#define DSI_RX_MIPI_SIZE_NUM(x)		(0x01c0 + x * 4)

enum {
	VSYNC_START = 0x01,
	VSYNC_END = 0x11,
	HSYNC_START = 0x21,
	HSYNC_END = 0x31,
};

static inline int dsi_rx_write(struct rk_serdes *ser, u32 reg, u32 val)
{
	struct i2c_client *client = ser->chip[DEVICE_LOCAL].client;

	return ser->i2c_write_reg(client, reg, val);
}

static inline int dsi_rx_read(struct rk_serdes *ser, u32 reg, u32 *val)
{
	struct i2c_client *client = ser->chip[DEVICE_LOCAL].client;

	return ser->i2c_read_reg(client, reg, val);
}

static inline int dsi_rx_update_bits(struct rk_serdes *ser,
				     u32 reg, u32 mask, u32 val)
{
	struct i2c_client *client = ser->chip[DEVICE_LOCAL].client;

	return ser->i2c_update_bits(client, reg, mask, val);
}

void rkx110_dsi_rx_enable(struct rk_serdes *ser, struct rk_serdes_route *route, int id)
{
	struct rkx110_dsi_rx *dsi = &ser->dsi_rx;
	const struct videomode *vm = &route->vm;
	unsigned long pixelclock;
	u32 hactive, vactive;
	u64 rate;
	u32 val = 0;
	u32 csi_base, dsirx_base;

	switch (route->frame_mode) {
	case SERDES_SP_PIXEL_INTERLEAVED:
		fallthrough;
	case SERDES_SP_LEFT_RIGHT_SPLIT:
		pixelclock = vm->pixelclock * 2;
		hactive = vm->hactive * 2;
		vactive = vm->vactive;
		break;
	case SERDES_SP_LINE_INTERLEAVED:
		pixelclock = vm->pixelclock * 2;
		vactive = vm->vactive * 2;
		hactive = vm->hactive;
		break;
	case SERDES_FRAME_NORMAL_MODE:
		fallthrough;
	default:
		pixelclock = vm->pixelclock;
		hactive = vm->hactive;
		vactive = vm->vactive;
		break;
	}

	rate = DIV_ROUND_CLOSEST_ULL(pixelclock, dsi->lanes);

	rkx110_combrxphy_set_mode(ser, COMBRX_PHY_MODE_VIDEO_MIPI);
	rkx110_combrxphy_set_rate(ser, rate * MSEC_PER_SEC);
	rkx110_combrxphy_power_on(ser, id ? COMBPHY_1 : COMBPHY_0);

	csi_base = id ? RKX110_CSI2HOST1_BASE : RKX110_CSI2HOST0_BASE;
	dsirx_base = id ? RKX110_DSI_RX1_BASE : RKX110_DSI_RX0_BASE;


	dsi_rx_write(ser, csi_base + CSI2HOST_N_LANES, dsi->lanes - 1);
	dsi_rx_write(ser, csi_base + CSI2HOST_RESETN, 0);

	val |= SW_DSI_EN | SW_DATETYPE_FE(VSYNC_END) | SW_DATETYPE_FS(VSYNC_START);
	dsi_rx_update_bits(ser, csi_base + CSI2HOST_CONTROL,
			   SW_DATETYPE_FE_MASK |
			   SW_DATETYPE_FS_MASK |
			   SW_DSI_EN, val);

	dsi_rx_write(ser, csi_base + CSI2HOST_RESETN, 1);

	val = SW_CAP_EN | SW_VC(0);
	/*
	 * video mode only support rgb888(0x3e), command mode
	 * only support DCS Long Write(0x39)
	 */
	val |= (dsi->mode_flags & SERDES_MIPI_DSI_MODE_VIDEO) ?
	       (0 | SW_DT(0x3e)) : (SW_COMMAND_MODE_EN | SW_DT(0x39));
	dsi_rx_write(ser, dsirx_base + DSI_RX_MIPI_IDX_CTRL0(0), val);
	dsi_rx_write(ser, dsirx_base + DSI_RX_MIPI_IDX_CTRL1(0),
		     SW_HEIGHT(vactive) | SW_WIDTH(hactive));
}

void rkx110_dsi_rx_disable(struct rk_serdes *ser, struct rk_serdes_route *route, int id)
{
	rkx110_combrxphy_power_off(ser, id ? COMBPHY_1 : COMBPHY_0);
}
