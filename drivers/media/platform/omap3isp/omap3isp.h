/*
 * omap3isp.h
 *
 * TI OMAP3 ISP - Bus Configuration
 *
 * Copyright (C) 2011 Nokia Corporation
 *
 * Contacts: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *	     Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __OMAP3ISP_H__
#define __OMAP3ISP_H__

enum isp_interface_type {
	ISP_INTERFACE_PARALLEL,
	ISP_INTERFACE_CSI2A_PHY2,
	ISP_INTERFACE_CCP2B_PHY1,
	ISP_INTERFACE_CCP2B_PHY2,
	ISP_INTERFACE_CSI2C_PHY1,
};

/**
 * struct isp_parallel_cfg - Parallel interface configuration
 * @data_lane_shift: Data lane shifter
 *		0 - CAMEXT[13:0] -> CAM[13:0]
 *		2 - CAMEXT[13:2] -> CAM[11:0]
 *		4 - CAMEXT[13:4] -> CAM[9:0]
 *		6 - CAMEXT[13:6] -> CAM[7:0]
 * @clk_pol: Pixel clock polarity
 *		0 - Sample on rising edge, 1 - Sample on falling edge
 * @hs_pol: Horizontal synchronization polarity
 *		0 - Active high, 1 - Active low
 * @vs_pol: Vertical synchronization polarity
 *		0 - Active high, 1 - Active low
 * @fld_pol: Field signal polarity
 *		0 - Positive, 1 - Negative
 * @data_pol: Data polarity
 *		0 - Normal, 1 - One's complement
 */
struct isp_parallel_cfg {
	unsigned int data_lane_shift:3;
	unsigned int clk_pol:1;
	unsigned int hs_pol:1;
	unsigned int vs_pol:1;
	unsigned int fld_pol:1;
	unsigned int data_pol:1;
};

enum {
	ISP_CCP2_PHY_DATA_CLOCK = 0,
	ISP_CCP2_PHY_DATA_STROBE = 1,
};

enum {
	ISP_CCP2_MODE_MIPI = 0,
	ISP_CCP2_MODE_CCP2 = 1,
};

/**
 * struct isp_csiphy_lane: CCP2/CSI2 lane position and polarity
 * @pos: position of the lane
 * @pol: polarity of the lane
 */
struct isp_csiphy_lane {
	u8 pos;
	u8 pol;
};

#define ISP_CSIPHY1_NUM_DATA_LANES	1
#define ISP_CSIPHY2_NUM_DATA_LANES	2

/**
 * struct isp_csiphy_lanes_cfg - CCP2/CSI2 lane configuration
 * @data: Configuration of one or two data lanes
 * @clk: Clock lane configuration
 */
struct isp_csiphy_lanes_cfg {
	struct isp_csiphy_lane data[ISP_CSIPHY2_NUM_DATA_LANES];
	struct isp_csiphy_lane clk;
};

/**
 * struct isp_ccp2_cfg - CCP2 interface configuration
 * @strobe_clk_pol: Strobe/clock polarity
 *		0 - Non Inverted, 1 - Inverted
 * @crc: Enable the cyclic redundancy check
 * @ccp2_mode: Enable CCP2 compatibility mode
 *		ISP_CCP2_MODE_MIPI - MIPI-CSI1 mode
 *		ISP_CCP2_MODE_CCP2 - CCP2 mode
 * @phy_layer: Physical layer selection
 *		ISP_CCP2_PHY_DATA_CLOCK - Data/clock physical layer
 *		ISP_CCP2_PHY_DATA_STROBE - Data/strobe physical layer
 * @vpclk_div: Video port output clock control
 */
struct isp_ccp2_cfg {
	unsigned int strobe_clk_pol:1;
	unsigned int crc:1;
	unsigned int ccp2_mode:1;
	unsigned int phy_layer:1;
	unsigned int vpclk_div:2;
	struct isp_csiphy_lanes_cfg lanecfg;
};

/**
 * struct isp_csi2_cfg - CSI2 interface configuration
 * @crc: Enable the cyclic redundancy check
 */
struct isp_csi2_cfg {
	unsigned crc:1;
	struct isp_csiphy_lanes_cfg lanecfg;
};

struct isp_bus_cfg {
	enum isp_interface_type interface;
	union {
		struct isp_parallel_cfg parallel;
		struct isp_ccp2_cfg ccp2;
		struct isp_csi2_cfg csi2;
	} bus; /* gcc < 4.6.0 chokes on anonymous union initializers */
};

#endif	/* __OMAP3ISP_H__ */
