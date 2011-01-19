/*
 * Copyright (C) 2010 Samsung Electronics
 *
 * S5P series MIPI CSI slave device support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef PLAT_S5P_CSIS_H_
#define PLAT_S5P_CSIS_H_ __FILE__

/**
 * struct s5p_platform_mipi_csis - platform data for MIPI-CSIS
 * @clk_rate: bus clock frequency
 * @lanes: number of data lanes used
 * @alignment: data alignment in bits
 * @hs_settle: HS-RX settle time
 */
struct s5p_platform_mipi_csis {
	unsigned long clk_rate;
	u8 lanes;
	u8 alignment;
	u8 hs_settle;
};

#endif /* PLAT_S5P_CSIS_H_ */
