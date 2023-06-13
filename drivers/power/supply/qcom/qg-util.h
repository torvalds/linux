/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018-2020 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QG_UTIL_H__
#define __QG_UTIL_H__

#define MAX_STEP_CHG_ENTRIES	8

int qg_read(struct qpnp_qg *chip, u32 addr, u8 *val, int len);
int qg_write(struct qpnp_qg *chip, u32 addr, u8 *val, int len);
int qg_masked_write(struct qpnp_qg *chip, int addr, u32 mask, u32 val);
int qg_read_raw_data(struct qpnp_qg *chip, int addr, u32 *data);
int get_fifo_length(struct qpnp_qg *chip, u32 *fifo_length, bool rt);
int get_sample_count(struct qpnp_qg *chip, u32 *sample_count);
int get_sample_interval(struct qpnp_qg *chip, u32 *sample_interval);
int get_fifo_done_time(struct qpnp_qg *chip, bool rt, int *time_ms);
int get_rtc_time(unsigned long *rtc_time);
bool is_usb_present(struct qpnp_qg *chip);
bool is_dc_present(struct qpnp_qg *chip);
bool is_input_present(struct qpnp_qg *chip);
bool is_parallel_enabled(struct qpnp_qg *chip);
bool is_cp_available(struct qpnp_qg *chip);
bool is_parallel_available(struct qpnp_qg *chip);
int qg_write_monotonic_soc(struct qpnp_qg *chip, int msoc);
int qg_get_battery_temp(struct qpnp_qg *chip, int *batt_temp);
int qg_get_battery_current(struct qpnp_qg *chip, int *ibat_ua);
int qg_get_battery_voltage(struct qpnp_qg *chip, int *vbat_uv);
int qg_get_vbat_avg(struct qpnp_qg *chip, int *vbat_uv);
s64 qg_iraw_to_ua(struct qpnp_qg *chip, int iraw);
int qg_get_ibat_avg(struct qpnp_qg *chip, int *ibat_ua);
bool is_chan_valid(struct qpnp_qg *chip, enum qg_ext_iio_channels chan);
int qg_read_iio_chan(struct qpnp_qg *chip,
	enum qg_ext_iio_channels chan, int *val);
int qg_write_iio_chan(struct qpnp_qg *chip,
	enum qg_ext_iio_channels chan, int val);
int qg_read_int_iio_chan(struct iio_channel *iio_chan_list, int chan_id,
			int *val);
int qg_read_range_data_from_node(struct device_node *node,
		const char *prop_str, struct range_data *ranges,
		int max_threshold, u32 max_value);
#endif
