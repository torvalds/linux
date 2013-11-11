/*
 * Copyright (C) 2012 Samsung Electronics Co.Ltd
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __PLAT_SAMSUNG_HDMI_H
#define __PLAT_SAMSUNG_HDMI_H __FILE__

extern void s5p_hdmi_set_platdata(struct i2c_board_info *hdmiphy_info,
				  struct i2c_board_info *mhl_info, int mhl_bus);

#endif /* __PLAT_SAMSUNG_HDMI_H */
