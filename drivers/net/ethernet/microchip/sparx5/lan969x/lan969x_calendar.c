// SPDX-License-Identifier: GPL-2.0+
/* Microchip lan969x Switch driver
 *
 * Copyright (c) 2024 Microchip Technology Inc. and its subsidiaries.
 */

#include "lan969x.h"

#define LAN969X_DSM_CAL_DEVS_PER_TAXI 10
#define LAN969X_DSM_CAL_TAXIS 5

enum lan969x_dsm_cal_dev {
	DSM_CAL_DEV_2G5,
	DSM_CAL_DEV_5G,
	DSM_CAL_DEV_10G,
	DSM_CAL_DEV_OTHER, /* 1G or less */
	DSM_CAL_DEV_MAX
};

/* Each entry in the following struct defines properties for a given speed
 * (10G, 5G, 2.5G, or 1G or less).
 */
struct lan969x_dsm_cal_dev_speed {
	/* Number of devices that requires this speed. */
	u32 n_devs;

	/* Array of devices that requires this speed. */
	u32 devs[LAN969X_DSM_CAL_DEVS_PER_TAXI];

	/* Number of slots required for one device running this speed. */
	u32 n_slots;

	/* Gap between two slots for one device running this speed. */
	u32 gap;
};

static u32
lan969x_taxi_ports[LAN969X_DSM_CAL_TAXIS][LAN969X_DSM_CAL_DEVS_PER_TAXI] = {
	{  0,  4,  1,  2,  3,  5,  6,  7, 28, 29 },
	{  8, 12,  9, 13, 10, 11, 14, 15, 99, 99 },
	{ 16, 20, 17, 21, 18, 19, 22, 23, 99, 99 },
	{ 24, 25, 99, 99, 99, 99, 99, 99, 99, 99 },
	{ 26, 27, 99, 99, 99, 99, 99, 99, 99, 99 }
};

static int lan969x_dsm_cal_idx_get(u32 *calendar, u32 cal_len, u32 *cal_idx)
{
	if (*cal_idx >= cal_len)
		return -EINVAL;

	do {
		if (calendar[*cal_idx] == SPX5_DSM_CAL_EMPTY)
			return 0;

		(*cal_idx)++;
	} while (*cal_idx < cal_len);

	return -ENOENT;
}

static enum lan969x_dsm_cal_dev lan969x_dsm_cal_get_dev(int speed)
{
	return (speed == 10000 ? DSM_CAL_DEV_10G :
		speed == 5000  ? DSM_CAL_DEV_5G :
		speed == 2500  ? DSM_CAL_DEV_2G5 :
				 DSM_CAL_DEV_OTHER);
}

static int lan969x_dsm_cal_get_speed(enum lan969x_dsm_cal_dev dev)
{
	return (dev == DSM_CAL_DEV_10G ? 10000 :
		dev == DSM_CAL_DEV_5G  ? 5000 :
		dev == DSM_CAL_DEV_2G5 ? 2500 :
					 1000);
}

int lan969x_dsm_calendar_calc(struct sparx5 *sparx5, u32 taxi,
			      struct sparx5_calendar_data *data)
{
	struct lan969x_dsm_cal_dev_speed dev_speeds[DSM_CAL_DEV_MAX] = {};
	u32 cal_len, n_slots, taxi_bw, n_devs = 0, required_bw  = 0;
	struct lan969x_dsm_cal_dev_speed *speed;
	int err;

	/* Maximum bandwidth for this taxi */
	taxi_bw = (128 * 1000000) / sparx5_clk_period(sparx5->coreclock);

	memcpy(data->taxi_ports, &lan969x_taxi_ports[taxi],
	       LAN969X_DSM_CAL_DEVS_PER_TAXI * sizeof(u32));

	for (int i = 0; i < LAN969X_DSM_CAL_DEVS_PER_TAXI; i++) {
		u32 portno = data->taxi_ports[i];
		enum sparx5_cal_bw bw;

		bw = sparx5_get_port_cal_speed(sparx5, portno);

		if (portno < sparx5->data->consts->n_ports_all)
			data->taxi_speeds[i] = sparx5_cal_speed_to_value(bw);
		else
			data->taxi_speeds[i] = 0;
	}

	/* Determine the different port types (10G, 5G, 2.5G, <= 1G) in the
	 * this taxi map.
	 */
	for (int i = 0; i < LAN969X_DSM_CAL_DEVS_PER_TAXI; i++) {
		u32 taxi_speed = data->taxi_speeds[i];
		enum lan969x_dsm_cal_dev dev;

		if (taxi_speed == 0)
			continue;

		required_bw += taxi_speed;

		dev = lan969x_dsm_cal_get_dev(taxi_speed);
		speed = &dev_speeds[dev];
		speed->devs[speed->n_devs++] = i;
		n_devs++;
	}

	if (required_bw > taxi_bw) {
		pr_err("Required bandwidth: %u is higher than total taxi bandwidth: %u",
		       required_bw, taxi_bw);
		return -EINVAL;
	}

	if (n_devs == 0) {
		data->schedule[0] = SPX5_DSM_CAL_EMPTY;
		return 0;
	}

	cal_len = n_devs;

	/* Search for a calendar length that fits all active devices. */
	while (cal_len < SPX5_DSM_CAL_LEN) {
		u32 bw_per_slot = taxi_bw / cal_len;

		n_slots = 0;

		for (int i = 0; i < DSM_CAL_DEV_MAX; i++) {
			speed = &dev_speeds[i];

			if (speed->n_devs == 0)
				continue;

			required_bw = lan969x_dsm_cal_get_speed(i);
			speed->n_slots = DIV_ROUND_UP(required_bw, bw_per_slot);

			if (speed->n_slots)
				speed->gap = DIV_ROUND_UP(cal_len,
							  speed->n_slots);
			else
				speed->gap = 0;

			n_slots += speed->n_slots * speed->n_devs;
		}

		if (n_slots <= cal_len)
			break; /* Found a suitable calendar length. */

		/* Not good enough yet. */
		cal_len = n_slots;
	}

	if (cal_len > SPX5_DSM_CAL_LEN) {
		pr_err("Invalid length: %u for taxi: %u", cal_len, taxi);
		return -EINVAL;
	}

	for (u32 i = 0; i < SPX5_DSM_CAL_LEN; i++)
		data->schedule[i] = SPX5_DSM_CAL_EMPTY;

	/* Place the remaining devices */
	for (u32 i = 0; i < DSM_CAL_DEV_MAX; i++) {
		speed = &dev_speeds[i];
		for (u32 dev = 0; dev < speed->n_devs; dev++) {
			u32 idx = 0;

			for (n_slots = 0; n_slots < speed->n_slots; n_slots++) {
				err = lan969x_dsm_cal_idx_get(data->schedule,
							      cal_len, &idx);
				if (err)
					return err;
				data->schedule[idx] = speed->devs[dev];
				idx += speed->gap;
			}
		}
	}

	return 0;
}
