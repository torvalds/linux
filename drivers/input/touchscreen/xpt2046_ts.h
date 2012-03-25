/*
 * drivers/input/touchscreen/xpt2046_ts.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DRIVERS_TOUCHSCREEN_XPT2046_TS_H
#define __DRIVERS_TOUCHSCREEN_XPT2046_TS_H

#define IOMUX_NAME_SIZE 40

enum xpt2046_filter {
	XPT2046_FILTER_OK,
	XPT2046_FILTER_REPEAT,
	XPT2046_FILTER_IGNORE,
};

struct xpt2046_platform_data {
	u16	model;			/* 2046. */
	bool	keep_vref_on;		/* set to keep vref on for differential
					 * measurements as well */
	bool	swap_xy;		/* swap x and y axes */

	/* If set to non-zero, after samples are taken this delay is applied
	 * and penirq is rechecked, to help avoid false events.  This value
	 * is affected by the material used to build the touch layer.
	 */
	u16	penirq_recheck_delay_usecs;

	u16	x_min, x_max;
	u16	y_min, y_max;

	u16	debounce_max;		/* max number of additional readings
					 * per sample */
	u16	debounce_tol;		/* tolerance used for filtering */
	u16	debounce_rep;		/* additional consecutive good readings
					 * required after the first two */
	int	gpio_pendown;		/* the GPIO used to decide the pendown
					 * state if get_pendown_state == NULL
					 */
	char	pendown_iomux_name[IOMUX_NAME_SIZE];	
	int		pendown_iomux_mode;	
	int 	touch_ad_top;
	int     touch_ad_bottom;
	int 	touch_ad_left;
	int 	touch_ad_right;
	int		touch_virtualkey_length;
	int	(*get_pendown_state)(void);
	int	(*filter_init)	(struct xpt2046_platform_data *pdata,
				 void **filter_data);
	int	(*filter)	(void *filter_data, int data_idx, int *val);
	void	(*filter_cleanup)(void *filter_data);
	void	(*wait_for_sync)(void);
	int (* io_init)(void);
	int (* io_deinit)(void);
};
#endif
