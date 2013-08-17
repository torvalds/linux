/* linux/drivers/video/s5p_mipi_dsi.h
 *
 * Header file for Samsung MIPI-DSI common driver.
 *
 * Copyright (c) 2011 Samsung Electronics
 * InKi Dae <inki.dae@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _S5P_MIPI_DSI_H
#define _S5P_MIPI_DSI_H

/* MIPI-DSIM status types. */
enum {
	DSIM_STATE_INIT,	/* should be initialized. */
	DSIM_STATE_STOP,	/* CPU and LCDC are LP mode. */
	DSIM_STATE_HSCLKEN,	/* HS clock was enabled. */
	DSIM_STATE_ULPS,
	DSIM_STATE_SUSPEND	/* DSIM is suspend state */
};

/* define DSI lane types. */
enum {
	DSIM_LANE_CLOCK	= (1 << 0),
	DSIM_LANE_DATA0	= (1 << 1),
	DSIM_LANE_DATA1	= (1 << 2),
	DSIM_LANE_DATA2	= (1 << 3),
	DSIM_LANE_DATA3	= (1 << 4),
};

#define FIN_HZ			(24 * MHZ)

#define DFIN_PLL_MIN_HZ		(6 * MHZ)
#define DFIN_PLL_MAX_HZ		(12 * MHZ)

#define DFVCO_MIN_HZ		(500 * MHZ)
#define DFVCO_MAX_HZ		(1000 * MHZ)

#define TRY_GET_FIFO_TIMEOUT	(5000 * 2)

#define DSIM_ESCCLK_ON		(0x1)
#define DSIM_ESCCLK_OFF		(0x0)

/* DSIM Interrupt Sources */
#define SFR_PL_FIFO_EMPTY	(1 << 29)
#define SFR_PH_FIFO_EMPTY	(1 << 28)
#define RX_DAT_DONE		(1 << 18)
#define ERR_RX_ECC		(1 << 15)

#define DSIM_RX_FIFO_READ_DONE	(0x30800002)
#define DSIM_MAX_RX_FIFO	(64)
int s5p_mipi_dsi_wr_data(struct mipi_dsim_device *dsim, unsigned int
	data_id, unsigned int data0, unsigned int data1);
int s5p_mipi_dsi_rd_data(struct mipi_dsim_device *dsim, u32 data_id,
	u32 addr, u32 count, u8 *buf);
#endif /* _S5P_MIPI_DSI_H */
