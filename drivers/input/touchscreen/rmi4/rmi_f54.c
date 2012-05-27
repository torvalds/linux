/*
 * Copyright (c) 2011 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/rmi.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/delay.h>
#define FUNCTION_DATA rmi_fn_54_data
#define FNUM 54

#include "rmi_driver.h"

/* Set this to 1 for raw hex dump of returned data. */
#define RAW_HEX 0
/* Set this to 1 for human readable dump of returned data. */
#define HUMAN_READABLE 0
/* The watchdog timer can be useful when debugging certain firmware related
 * issues.
 */
#define F54_WATCHDOG 1

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 32)
#define KERNEL_VERSION_ABOVE_2_6_32 1
#endif

/* define fn $54 commands */
#define GET_REPORT                1
#define FORCE_CAL                 2

#define NO_AUTO_CAL_MASK 1
/* status */
#define BUSY 1
#define IDLE 0

/* Offsets for data */
#define RMI_F54_REPORT_DATA_OFFSET	3
#define RMI_F54_FIFO_OFFSET		1
#define RMI_F54_NUM_TX_OFFSET		1
#define RMI_F54_NUM_RX_OFFSET		0

/* Fixed sizes of reports */
#define RMI_54_FULL_RAW_CAP_MIN_MAX_SIZE 4
#define RMI_54_HIGH_RESISTANCE_SIZE 6

/* definitions for F54 Query Registers */
union f54_ad_query {
	struct {
		/* query 0 */
		u8 num_of_rx_electrodes;

		/* query 1 */
		u8 num_of_tx_electrodes;

		/* query2 */
		u8 f54_ad_query2_b0__1:2;
		u8 has_baseline:1;
		u8 has_image8:1;
		u8 f54_ad_query2_b4__5:2;
		u8 has_image16:1;
		u8 f54_ad_query2_b7:1;
		
		/* query 3.0 and 3.1 */
		u16 clock_rate;

		/* query 4 */
		u8 touch_controller_family;

		/* query 5 */
		u8 has_pixel_touch_threshold_adjustment:1;
		u8 f54_ad_query5_b1__7:7;

		/* query 6 */
		u8 has_sensor_assignment:1;
		u8 has_interference_metric:1;
		u8 has_sense_frequency_control:1;
		u8 has_firmware_noise_mitigation:1;
		u8 f54_ad_query6_b4:1;
		u8 has_two_byte_report_rate:1;
		u8 has_one_byte_report_rate:1;
		u8 has_relaxation_control:1;

		/* query 7 */
		u8 curve_compensation_mode:2;
		u8 f54_ad_query7_b2__7:6;

		/* query 8 */
		u8 f54_ad_query2_b0:1;
		u8 has_iir_filter:1;
		u8 has_cmn_removal:1;
		u8 has_cmn_maximum:1;
		u8 has_touch_hysteresis:1;
		u8 has_edge_compensation:1;
		u8 has_per_frequency_noise_control:1;
		u8 f54_ad_query8_b7:1;

		u8 f54_ad_query9;
		u8 f54_ad_query10;
		u8 f54_ad_query11;

		/* query 12 */
		u8 number_of_sensing_frequencies:4;
		u8 f54_ad_query12_b4__7:4;
	};
	struct {
		u8 regs[14];
		u16 address;
	};
};

/* And now for the very large amount of control registers */

/* Ctrl registers */

union f54_ad_control_0 {
	/* control 0 */
	struct {
		u8 no_relax:1;
		u8 no_scan:1;
		u8 f54_ad_ctrl0_b2__7:6;
	};
	struct {
		u8 regs[1];
		u16 address;
	};
};

union f54_ad_control_1 {
	/* control 1 */
	struct {
		/* control 1 */
		u8 bursts_per_cluster:4;
		u8 f54_ad_ctrl1_b4__7:4;
	};
	struct {
		u8 regs[1];
		u16 address;
	};
};

union f54_ad_control_2 {
	/* control 2 */
	struct {
		u16 saturation_cap;
		u16 f54_ctrl2;
	};
	struct {
		u8 regs[4];
		u16 address;
	};
};

union f54_ad_control_3 {
	/* control 3 */
	struct {
		u16 pixel_touch_threshold;
		u16 f54_ctrl3;
	};
	struct {
		u8 regs[4];
		u16 address;
	};
};

union f54_ad_control_4__6 {
	struct {
		/* control 4 */
		u8 rx_feedback_cap:2;
		u8 f54_ad_ctrl4_b2__7:6;

		/* control 5 */
		u8 low_ref_cap:2;
		u8 low_ref_feedback_cap:2;
		u8 low_ref_polarity:1;
		u8 f54_ad_ctrl5_b5__7:3;
		
		/* control 6 */
			u8 high_ref_cap:2;
		u8 high_ref_feedback_cap:2;
		u8 high_ref_polarity:1;
		u8 f54_ad_ctrl6_b5__7:3;
	};
	struct {
		u8 regs[3];
		u16 address;
	};
};

union f54_ad_control_7 {
	struct {
		/* control 7 */
		u8 cbc_cap:2;
		u8 cbc_polarity:2;
		u8 cbc_tx_carrier_selection:1;
		u8 f54_ad_ctrl6_b5__7:3;
	};
	struct {
		u8 regs[1];
		u16 address;
	};
};

union f54_ad_control_8__9 {
	struct {
		/* control 8 */
		u16 integration_duration:10;
		u16 f54_ad_ctrl8_b10__15:6;
		/* control 9 */
		u8 reset_duration;
		u8 f54_ctrl9;
	};
	struct {
		u8 regs[4];
		u16 address;
	};
};

union f54_ad_control_10 {
	struct {
		/* control 10 */
		u8 noise_sensing_bursts_per_image:4;
		u8 f54_ad_ctrl10_b4__7:4;
	};
	struct {
		u8 regs[1];
		u16 address;
	};
};

union f54_ad_control_11 {
	struct {
		/* control 11 */
		u8 reserved;
		u8 f54_ctrl11;
	};
	struct {
		u8 regs[2];
		u16 address;
	};
};

union f54_ad_control_12__13 {
	struct {
		/* control 12 */
		u8 slow_relaxation_rate;
		u8 f54_ctrl12;

		/* control 13 */
		u8 fast_relaxation_rate;
		u8 f54_ctrl13;
	};
	struct {
		u8 regs[4];
		u16 address;
	};
};

union f54_ad_control_14 {
	struct {
		/* control 14 */
			u8 rxs_on_xaxis:1;
			u8 curve_comp_on_txs:1;
			u8 f54_ad_ctrl14b2__7:6;
	};
	struct {
		u8 regs[1];
		u16 address;
	};
};

struct f54_ad_control_15n {
	/*Control 15.* */
	u8 sensor_rx_assignment;
};

struct f54_ad_control_15{
		struct f54_ad_control_15n *regs;
		u16 address;
		u8 length;
};

struct f54_ad_control_16n {
	/*Control 16.* */
	u8 sensor_tx_assignment;
};

struct f54_ad_control_16{
		struct f54_ad_control_16n *regs;
		u16 address;
		u8 length;
};


/* control 17 */
struct f54_ad_control_17n {
	u8 burst_countb10__8:3;
	u8 disable:1;
	u8 f54_ad_ctrlb4:1;
	u8 filter_bandwidth:3;
};

struct f54_ad_control_17{
		struct f54_ad_control_17n *regs;
		u16 address;
		u8 length;
};

struct f54_ad_control_18n {
	/*Control 18.* */
	u8 burst_countb7__0n;
};

struct f54_ad_control_18{
		struct f54_ad_control_18n *regs;
		u16 address;
		u8 length;
};

struct f54_ad_control_19n {
	/*Control 19.* */
	u8 stretch_duration;
};

struct f54_ad_control_19{
		struct f54_ad_control_19n *regs;
		u16 address;
		u8 length;
};

union f54_ad_control_20 {
	struct {
		/* control 20 */
		u8 disable_noise_mitigation:1;
		u8 f54_ad_ctrl20b2__7:7;
	};
	struct {
		u8 regs[1];
		u16 address;
	};
};

union f54_ad_control_21 {
	struct {
		/* control 21 */
		u16 freq_shift_noise_threshold;
		u16 f54_ctrl21;
	};
	struct {
		u8 regs[4];
		u16 address;
	};
};

union f54_ad_control_22__26 {
	struct {
		/* control 22 */
		u8 noise_density_threshold;
		u8 f54_ctrl22;

		/* control 23 */
		u16 medium_noise_threshold;
		u8 f54_ctrl23;

		/* control 24 */
		u16 high_noise_threshold;
		u16 f54_ctrl24;

		/* control 25 */
		u8 noise_density;
		u8 f54_ctrl25;

		/* control 26 */
		u8 frame_count;
		u8 f54_ctrl26;
	};
	struct {
		u8 regs[13];
		u16 address;
	};
};

union f54_ad_control_27 {
	struct {
		/* control 27 */
		u8 iir_filter_coef;
		u8 f54_ctrl27;
	};
	struct {
		u8 regs[2];
		u16 address;
	};
};

union f54_ad_control_28 {
	struct {
		/* control 28 */
		u16 quiet_threshold;
		u16 f54_ctrl28;
	};
	struct {
		u8 regs[4];
		u16 address;
	};
};


union f54_ad_control_29 {
	struct {
		/* control 29 */
		u8 f54_ad_ctrl20b0__6:7;
		u8 cmn_filter_disable:1;
	};
	struct {
		u8 regs[1];
		u16 address;
	};
};

union f54_ad_control_30 {
	struct {
		/* control 30 */
		u8 cmn_filter_max;
		u8 f54_ctrl30;
	};
	struct {
		u8 regs[2];
		u16 address;
	};
};

union f54_ad_control_31 {
	struct {
		/* control 31 */
		u8 touch_hysteresis;
		u8 f54_ctrl31;
	};
	struct {
		u8 regs[2];
		u16 address;
	};
};

union f54_ad_control_32__35 {
	struct {
		/* control 32 */
		u16 rx_low_edge_comp;
		u16 f54_ctrl32;

		/* control 33 */
		u16 rx_high_edge_comp;
		u16 f54_ctrl33;

		/* control 34 */
		u16 tx_low_edge_comp;
		u16 f54_ctrl34;

		/* control 35 */
		u16 tx_high_edge_comp;
		u16 f54_ctrl35;
	};
	struct {
		u8 regs[16];
		u16 address;
	};
};

struct f54_ad_control_36n {
	/*Control 36.* */
	u8 axis1_comp;
};

struct f54_ad_control_36{
		struct f54_ad_control_36n *regs;
		u16 address;
		u8 length;
};

struct f54_ad_control_37n {
	/*Control 37.* */
	u8 axis2_comp;
};

struct f54_ad_control_37{
		struct f54_ad_control_37n *regs;
		u16 address;
		u8 length;
};

struct f54_ad_control_38n {
	/*Control 38.* */
	u8 noise_control_1;
};

struct f54_ad_control_38{
		struct f54_ad_control_38n *regs;
		u16 address;
		u8 length;
};

struct f54_ad_control_39n {
	/*Control 39.* */
	u8 noise_control_2;
};

struct f54_ad_control_39{
		struct f54_ad_control_39n *regs;
		u16 address;
		u8 length;
};

struct f54_ad_control_40n {
	/*Control 40.* */
	u8 noise_control_3;
};

struct f54_ad_control_40{
		struct f54_ad_control_40n *regs;
		u16 address;
		u8 length;
};

struct f54_ad_control {
	union f54_ad_control_0 *reg_0;
	union f54_ad_control_1 *reg_1;
	union f54_ad_control_2 *reg_2;
	union f54_ad_control_3 *reg_3;
	union f54_ad_control_4__6 *reg_4__6;
	union f54_ad_control_7 *reg_7;
	union f54_ad_control_8__9 *reg_8__9;
	union f54_ad_control_10 *reg_10;
	union f54_ad_control_11 *reg_11;
	union f54_ad_control_12__13 *reg_12__13;
	union f54_ad_control_14 *reg_14;
	/* control 15 */
	struct f54_ad_control_15 *reg_15;
	/* control 16 */
	struct f54_ad_control_16 *reg_16;

	/* This register is n repetitions of f54_ad_control_17 */
	struct f54_ad_control_17 *reg_17;

	/* control 18 */
	struct f54_ad_control_18 *reg_18;

	/* control 19 */
	struct f54_ad_control_19 *reg_19;
	
	union f54_ad_control_20 *reg_20;
	union f54_ad_control_21 *reg_21;
	union f54_ad_control_22__26 *reg_22__26;
	union f54_ad_control_27 *reg_27;
	union f54_ad_control_28 *reg_28;
	union f54_ad_control_29 *reg_29;
	union f54_ad_control_30 *reg_30;
	union f54_ad_control_31 *reg_31;
	union f54_ad_control_32__35 *reg_32__35;
	/* control 36 */
	struct f54_ad_control_36 *reg_36;

	/* control 37 */
	struct f54_ad_control_37 *reg_37;

	/* control 38 */
	struct f54_ad_control_38 *reg_38;

	/* control 39 */
	struct f54_ad_control_39 *reg_39;

	/* control 40 */
	struct f54_ad_control_40 *reg_40;
};

/* define report types */
enum f54_report_types {
	F54_8BIT_IMAGE = 1,
	F54_16BIT_IMAGE = 2,
	F54_RAW_16BIT_IMAGE = 3,
	F54_HIGH_RESISTANCE = 4,
	F54_TX_TO_TX_SHORT = 5,
	F54_RX_TO_RX1 = 7,
	F54_TRUE_BASELINE = 9,
	F54_FULL_RAW_CAP_MIN_MAX = 13,
	F54_RX_OPENS1 = 14,
	F54_TX_OPEN = 15,
	F54_TX_TO_GROUND = 16,
	F54_RX_TO_RX2 = 17,
	F54_RX_OPENS2 = 18,
	F54_FULL_RAW_CAP = 19,
	F54_FULL_RAW_CAP_RX_COUPLING_COMP = 20
};

/* data specific to fn $54 that needs to be kept around */
struct rmi_fn_54_data {
	union f54_ad_query query;
	struct f54_ad_control control;
	enum f54_report_types report_type;
	u16 fifoindex;
	signed char status;
	bool no_auto_cal;
	unsigned int report_size;
	unsigned char *report_data;
	unsigned int bufsize;
	struct mutex data_mutex;
	struct mutex status_mutex;
	struct mutex control_mutex;
#if F54_WATCHDOG
	struct hrtimer watchdog;
#endif
	struct rmi_function_container *fc;
	struct work_struct work;
};

/* sysfs functions */
show_store_union_struct_prototype(report_type)

store_union_struct_prototype(get_report)

store_union_struct_prototype(force_cal)

show_union_struct_prototype(status)

#ifdef KERNEL_VERSION_ABOVE_2_6_32
static ssize_t rmi_fn_54_data_read(struct file *data_file, struct kobject *kobj,
					struct bin_attribute *attributes,
					char *buf, loff_t pos, size_t count);
#else
static ssize_t rmi_fn_54_data_read(struct kobject *kobj,
					struct bin_attribute *attributes,
					char *buf, loff_t pos, size_t count);
#endif

show_union_struct_prototype(num_of_rx_electrodes)
show_union_struct_prototype(num_of_tx_electrodes)
show_union_struct_prototype(has_image16)
show_union_struct_prototype(has_image8)
show_union_struct_prototype(has_baseline)
show_union_struct_prototype(clock_rate)
show_union_struct_prototype(touch_controller_family)
show_union_struct_prototype(has_pixel_touch_threshold_adjustment)
show_union_struct_prototype(has_sensor_assignment)
show_union_struct_prototype(has_interference_metric)
show_union_struct_prototype(has_sense_frequency_control)
show_union_struct_prototype(has_firmware_noise_mitigation)
show_union_struct_prototype(has_two_byte_report_rate)
show_union_struct_prototype(has_one_byte_report_rate)
show_union_struct_prototype(has_relaxation_control)
show_union_struct_prototype(curve_compensation_mode)
show_union_struct_prototype(has_iir_filter)
show_union_struct_prototype(has_cmn_removal)
show_union_struct_prototype(has_cmn_maximum)
show_union_struct_prototype(has_touch_hysteresis)
show_union_struct_prototype(has_edge_compensation)
show_union_struct_prototype(has_per_frequency_noise_control)
show_union_struct_prototype(number_of_sensing_frequencies)
show_store_union_struct_prototype(no_auto_cal)
show_store_union_struct_prototype(fifoindex)

/* Repeated Control Registers */
show_union_struct_prototype(sensor_rx_assignment)
show_union_struct_prototype(sensor_tx_assignment)
show_union_struct_prototype(filter_bandwidth)
show_union_struct_prototype(disable)
show_union_struct_prototype(burst_count)
show_union_struct_prototype(stretch_duration)
show_store_union_struct_prototype(axis1_comp)
show_store_union_struct_prototype(axis2_comp)
show_union_struct_prototype(noise_control_1)
show_union_struct_prototype(noise_control_2)
show_union_struct_prototype(noise_control_3)

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */
show_store_union_struct_prototype(no_relax)
show_store_union_struct_prototype(no_scan)
show_store_union_struct_prototype(bursts_per_cluster)
show_store_union_struct_prototype(saturation_cap)
show_store_union_struct_prototype(pixel_touch_threshold)
show_store_union_struct_prototype(rx_feedback_cap)
show_store_union_struct_prototype(low_ref_cap)
show_store_union_struct_prototype(low_ref_feedback_cap)
show_store_union_struct_prototype(low_ref_polarity)
show_store_union_struct_prototype(high_ref_cap)
show_store_union_struct_prototype(high_ref_feedback_cap)
show_store_union_struct_prototype(high_ref_polarity)
show_store_union_struct_prototype(cbc_cap)
show_store_union_struct_prototype(cbc_polarity)
show_store_union_struct_prototype(cbc_tx_carrier_selection)
show_store_union_struct_prototype(integration_duration)
show_store_union_struct_prototype(reset_duration)
show_store_union_struct_prototype(noise_sensing_bursts_per_image)
show_store_union_struct_prototype(slow_relaxation_rate)
show_store_union_struct_prototype(fast_relaxation_rate)
show_store_union_struct_prototype(rxs_on_xaxis)
show_store_union_struct_prototype(curve_comp_on_txs)
show_store_union_struct_prototype(disable_noise_mitigation)
show_store_union_struct_prototype(freq_shift_noise_threshold)
show_store_union_struct_prototype(noise_density_threshold)
show_store_union_struct_prototype(medium_noise_threshold)
show_store_union_struct_prototype(high_noise_threshold)
show_store_union_struct_prototype(noise_density)
show_store_union_struct_prototype(frame_count)
show_store_union_struct_prototype(iir_filter_coef)
show_store_union_struct_prototype(quiet_threshold)
show_store_union_struct_prototype(cmn_filter_disable)
show_store_union_struct_prototype(cmn_filter_max)
show_store_union_struct_prototype(touch_hysteresis)
show_store_union_struct_prototype(rx_low_edge_comp)
show_store_union_struct_prototype(rx_high_edge_comp)
show_store_union_struct_prototype(tx_low_edge_comp)
show_store_union_struct_prototype(tx_high_edge_comp)

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */

static struct attribute *attrs[] = {
	attrify(report_type),
	attrify(get_report),
	attrify(force_cal),
	attrify(status),
	attrify(num_of_rx_electrodes),
	attrify(num_of_tx_electrodes),
	attrify(has_image16),
	attrify(has_image8),
	attrify(has_baseline),
	attrify(clock_rate),
	attrify(touch_controller_family),
	attrify(has_pixel_touch_threshold_adjustment),
	attrify(has_sensor_assignment),
	attrify(has_interference_metric),
	attrify(has_sense_frequency_control),
	attrify(has_firmware_noise_mitigation),
	attrify(has_two_byte_report_rate),
	attrify(has_one_byte_report_rate),
	attrify(has_relaxation_control),
	attrify(curve_compensation_mode),
	attrify(has_iir_filter),
	attrify(has_cmn_removal),
	attrify(has_cmn_maximum),
	attrify(has_touch_hysteresis),
	attrify(has_edge_compensation),
	attrify(has_per_frequency_noise_control),
	attrify(number_of_sensing_frequencies),
	attrify(no_auto_cal),
	attrify(fifoindex),
	NULL
};

static struct attribute_group attrs_query = GROUP(attrs);

struct bin_attribute dev_rep_data = {
	.attr = {
		 .name = "rep_data",
		 .mode = RMI_RO_ATTR},
	.size = 0,
	.read = rmi_fn_54_data_read,
};

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */

static struct attribute *attrs_reg_0[] = {
	attrify(no_relax),
	attrify(no_scan),
	NULL
};

static struct attribute *attrs_reg_1[] = {
	attrify(bursts_per_cluster),
	NULL
};

static struct attribute *attrs_reg_2[] = {
	attrify(saturation_cap),
	NULL
};

static struct attribute *attrs_reg_3[] = {
	attrify(pixel_touch_threshold),
	NULL
};

static struct attribute *attrs_reg_4__6[] = {
	attrify(rx_feedback_cap),
	attrify(low_ref_cap),
	attrify(low_ref_feedback_cap),
	attrify(low_ref_polarity),
	attrify(high_ref_cap),
	attrify(high_ref_feedback_cap),
	attrify(high_ref_polarity),
	NULL
};

static struct attribute *attrs_reg_7[] = {
	attrify(cbc_cap),
	attrify(cbc_polarity),
	attrify(cbc_tx_carrier_selection),
	NULL
};

static struct attribute *attrs_reg_8__9[] = {
	attrify(integration_duration),
	attrify(reset_duration),
	NULL
};

static struct attribute *attrs_reg_10[] = {
	attrify(noise_sensing_bursts_per_image),
	NULL
};

static struct attribute *attrs_reg_12__13[] = {
	attrify(slow_relaxation_rate),
	attrify(fast_relaxation_rate),
	NULL
};

static struct attribute *attrs_reg_14__16[] = {
	attrify(rxs_on_xaxis),
	attrify(curve_comp_on_txs),
	attrify(sensor_rx_assignment),
	attrify(sensor_tx_assignment),
	NULL
};

static struct attribute *attrs_reg_17__19[] = {
	attrify(filter_bandwidth),
	attrify(disable),
	attrify(burst_count),
	attrify(stretch_duration),
};

static struct attribute *attrs_reg_20[] = {
	attrify(disable_noise_mitigation),
	NULL
};

static struct attribute *attrs_reg_21[] = {
	attrify(freq_shift_noise_threshold),
	NULL
};

static struct attribute *attrs_reg_22__26[] = {
	attrify(noise_density_threshold),
	attrify(medium_noise_threshold),
	attrify(high_noise_threshold),
	attrify(noise_density),
	attrify(frame_count),
	NULL
};

static struct attribute *attrs_reg_27[] = {
	attrify(iir_filter_coef),
	NULL
};

static struct attribute *attrs_reg_28[] = {
	attrify(quiet_threshold),
	NULL
};

static struct attribute *attrs_reg_29[] = {
	attrify(cmn_filter_disable),
	NULL
};

static struct attribute *attrs_reg_30[] = {
	attrify(cmn_filter_max),
	NULL
};

static struct attribute *attrs_reg_31[] = {
	attrify(touch_hysteresis),
	NULL
};

static struct attribute *attrs_reg_32__35[] = {
	attrify(rx_low_edge_comp),
	attrify(rx_high_edge_comp),
	attrify(tx_low_edge_comp),
	attrify(tx_high_edge_comp),
	NULL
};

static struct attribute *attrs_reg_36[] = {
	attrify(axis1_comp),
	NULL
 };
 
static struct attribute *attrs_reg_37[] = {
	attrify(axis2_comp),
	NULL
 };
 
static struct attribute *attrs_reg_38__40[] = {
	attrify(noise_control_1),
	attrify(noise_control_2),
	attrify(noise_control_3),
	NULL
 };

static struct attribute_group attrs_ctrl_regs[] = {
	GROUP(attrs_reg_0),
	GROUP(attrs_reg_1),
	GROUP(attrs_reg_2),
	GROUP(attrs_reg_3),
	GROUP(attrs_reg_4__6),
	GROUP(attrs_reg_7),
	GROUP(attrs_reg_8__9),
	GROUP(attrs_reg_10),
	GROUP(attrs_reg_12__13),
	GROUP(attrs_reg_14__16),
	GROUP(attrs_reg_17__19),
	GROUP(attrs_reg_20),
	GROUP(attrs_reg_21),
	GROUP(attrs_reg_22__26),
	GROUP(attrs_reg_27),
	GROUP(attrs_reg_28),
	GROUP(attrs_reg_29),
	GROUP(attrs_reg_30),
	GROUP(attrs_reg_31),
	GROUP(attrs_reg_32__35),
	GROUP(attrs_reg_36),
	GROUP(attrs_reg_37),
	GROUP(attrs_reg_38__40)
};

bool attrs_ctrl_regs_exist[ARRAY_SIZE(attrs_ctrl_regs)];

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */
#if F54_WATCHDOG
static enum hrtimer_restart clear_status(struct hrtimer *timer);

static void clear_status_worker(struct work_struct *work);
#endif

static int rmi_f54_alloc_memory(struct rmi_function_container *fc);

static void rmi_f54_free_memory(struct rmi_function_container *fc);

static int rmi_f54_initialize(struct rmi_function_container *fc);

static int rmi_f54_reset(struct rmi_function_container *fc);

static int rmi_f54_create_sysfs(struct rmi_function_container *fc);

static int rmi_f54_init(struct rmi_function_container *fc)
{
	int retval = 0;
	struct rmi_fn_54_data *f54;

	dev_info(&fc->dev, "Intializing F54.");

	retval = rmi_f54_alloc_memory(fc);
	if (retval < 0)
		goto error_exit;

	retval = rmi_f54_initialize(fc);
	if (retval < 0)
		goto error_exit;

	retval = rmi_f54_create_sysfs(fc);
	if (retval < 0)
		goto error_exit;
	f54 = fc->data;
	f54->status = IDLE;
	return retval;

error_exit:
	rmi_f54_free_memory(fc);

	return retval;
}

static int rmi_f54_alloc_memory(struct rmi_function_container *fc)
{
	struct rmi_fn_54_data *f54;

	f54 = kzalloc(sizeof(struct rmi_fn_54_data), GFP_KERNEL);
	if (!f54) {
		dev_err(&fc->dev, "Failed to allocate rmi_fn_54_data.\n");
		return -ENOMEM;
	}
	fc->data = f54;
	f54->fc = fc;


	return 0;
}

static void rmi_f54_free_memory(struct rmi_function_container *fc)
{
	int reg_num;
	struct rmi_fn_54_data *f54 = fc->data;
	sysfs_remove_group(&fc->dev.kobj, &attrs_query);
	for (reg_num = 0; reg_num < ARRAY_SIZE(attrs_ctrl_regs); reg_num++)
		sysfs_remove_group(&fc->dev.kobj, &attrs_ctrl_regs[reg_num]);
	sysfs_remove_bin_file(&fc->dev.kobj, &dev_rep_data);
	if (f54) {
		kfree(f54->report_data);
		kfree(f54);
		fc->data = NULL;
	}
}

static int rmi_f54_reset(struct rmi_function_container *fc)
{
	struct rmi_fn_54_data *data = fc->data;
	struct rmi_driver *driver = fc->rmi_dev->driver;

#if F54_WATCHDOG
	hrtimer_cancel(&data->watchdog);
#endif

	mutex_lock(&data->status_mutex);
	if (driver->restore_irq_mask) {
		dev_dbg(&fc->dev, "Restoring interupts!\n");
		driver->restore_irq_mask(fc->rmi_dev);
	} else {
		dev_err(&fc->dev, "No way to restore interrupts!\n");
	}
	data->status = -ECONNRESET;
	mutex_unlock(&data->status_mutex);

	return 0;
}

static void rmi_f54_remove(struct rmi_function_container *fc)
{
	struct rmi_fn_54_data *data = fc->data;

	dev_info(&fc->dev, "Removing F54.");

	#if F54_WATCHDOG
	/* Stop timer */
	hrtimer_cancel(&data->watchdog);
	#endif

	rmi_f54_free_memory(fc);
}

static int rmi_f54_create_sysfs(struct rmi_function_container *fc) {
	int reg_num;
	int retval;
	dev_dbg(&fc->dev, "Creating sysfs files.");
	/* Set up sysfs device attributes. */
	
	if (sysfs_create_group(&fc->dev.kobj, &attrs_query) < 0 ) {
	    dev_err(&fc->dev, "Failed to create query sysfs files.");
		return -ENODEV;
	}
	for (reg_num = 0; reg_num < ARRAY_SIZE(attrs_ctrl_regs);
								reg_num++) {
		if (attrs_ctrl_regs_exist[reg_num]) {
			if (sysfs_create_group(&fc->dev.kobj,
					&attrs_ctrl_regs[reg_num]) < 0) {
				dev_err(&fc->dev,
					"Failed to create "
					"sysfs file group for reg"
					"group %d.",
					reg_num);
				return -ENODEV;
			}
		}
	}

	/* Binary sysfs file to report the data back */
	retval = sysfs_create_bin_file(&fc->dev.kobj, &dev_rep_data);
	if (retval < 0) {
		dev_err(&fc->dev, "Failed to create sysfs file for F54 data "
					"(error = %d).\n", retval);
		return -ENODEV;
	}
	return 0;
}



static int rmi_f54_initialize(struct rmi_function_container *fc)
{
	struct rmi_fn_54_data *instance_data= fc->data;
	struct f54_ad_control *control;
	int retval = 0;
	u8 size = 0;
	u16 next_loc;
	u8 reg_num;

	dev_info(&fc->dev, "Intializing F54.");
	
#if F54_WATCHDOG
	/* Set up watchdog timer to catch unanswered get_report commands */
	hrtimer_init(&instance_data->watchdog, CLOCK_MONOTONIC,
							HRTIMER_MODE_REL);
	instance_data->watchdog.function = clear_status;

	/* work function to do unlocking */
	INIT_WORK(&instance_data->work, clear_status_worker);
#endif

	/* Read F54 Query Data */
	retval = rmi_read_block(fc->rmi_dev, fc->fd.query_base_addr,
		(u8 *)&instance_data->query, sizeof(instance_data->query));
	if (retval < 0) {
		dev_err(&fc->dev, "Could not read query registers"
			" from 0x%04x\n", fc->fd.query_base_addr);
		return retval;
	}
/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */
	/* Initialize the control registers */
	next_loc = fc->fd.control_base_addr;
	reg_num = 0;
	control = &instance_data->control;

	attrs_ctrl_regs_exist[reg_num] = true;
	reg_num++;
	control->reg_0 = kzalloc(sizeof(union f54_ad_control_0), GFP_KERNEL);
	if (!control->reg_0) {
		dev_err(&fc->dev, "Failed to allocate control registers.");
		return -ENOMEM;
	}
	control->reg_0->address = next_loc;
	next_loc++;

	if (instance_data->query.touch_controller_family == 0
			|| instance_data->query.touch_controller_family == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_1 = kzalloc(sizeof(union f54_ad_control_1),
								GFP_KERNEL);
		if (!control->reg_1) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_1->address = next_loc;
		next_loc++;
	}
	reg_num++;

	attrs_ctrl_regs_exist[reg_num] = true;
	reg_num++;
	control->reg_2 = kzalloc(sizeof(union f54_ad_control_2), GFP_KERNEL);
	if (!control->reg_2) {
		dev_err(&fc->dev, "Failed to allocate control registers.");
		return -ENOMEM;
	}
	control->reg_2->address = next_loc += 2;

	if (instance_data->query.has_pixel_touch_threshold_adjustment == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;

		control->reg_3 = kzalloc(sizeof(union f54_ad_control_3),
								GFP_KERNEL);
		if (!control->reg_3) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_3->address = next_loc;
		next_loc++;
	}
	reg_num++;

	if (instance_data->query.touch_controller_family == 0
		|| instance_data->query.touch_controller_family == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_4__6 = kzalloc(sizeof(union f54_ad_control_4__6),
								GFP_KERNEL);
		if (!control->reg_4__6) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_4__6->address = next_loc;
		next_loc+=3;
	}
	reg_num++;

	if (instance_data->query.touch_controller_family == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_7 = kzalloc(sizeof(union f54_ad_control_7),
								GFP_KERNEL);
		if (!control->reg_7) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_7->address = next_loc;
		next_loc++;
	}
	reg_num++;

	if (instance_data->query.touch_controller_family == 0
		|| instance_data->query.touch_controller_family == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_8__9 = kzalloc(sizeof(union f54_ad_control_8__9),
								GFP_KERNEL);
		if (!control->reg_8__9) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_8__9->address = next_loc;
		next_loc+=sizeof(control->reg_8__9->regs);
	}
	reg_num++;

	if (instance_data->query.has_interference_metric == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_10 = kzalloc(sizeof(union f54_ad_control_10),
								GFP_KERNEL);
		if (!control->reg_10) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_10->address = next_loc;
		next_loc++;
	}
	reg_num++;

	/* F54 Control Register 11 is reserved */
	next_loc++;

	if (instance_data->query.has_relaxation_control == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_12__13 = kzalloc(
			sizeof(union f54_ad_control_12__13), GFP_KERNEL);
		if (!control->reg_12__13) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_12__13->address = next_loc;
		next_loc+=sizeof(control->reg_12__13->regs);
	}
	reg_num++;

	if (instance_data->query.has_sensor_assignment == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_14 = kzalloc(sizeof(union f54_ad_control_14),
								GFP_KERNEL);
		if (!control->reg_14) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		
		control->reg_15 = 
			kzalloc(sizeof(struct f54_ad_control_15), GFP_KERNEL);
		if (!control->reg_15) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_15->length = instance_data->query.num_of_rx_electrodes;
		control->reg_15->regs = 
				kzalloc(control->reg_15->length
				* sizeof(struct f54_ad_control_15n), GFP_KERNEL);
		if (!control->reg_15->regs) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		
		control->reg_16 = 
			kzalloc(sizeof(struct f54_ad_control_16), GFP_KERNEL);
		if (!control->reg_16) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_16->length = instance_data->query.num_of_tx_electrodes;
		control->reg_16->regs = 
				kzalloc(control->reg_16->length
				* sizeof(struct f54_ad_control_16n), GFP_KERNEL);
		if (!control->reg_16->regs) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		
		control->reg_14->address = next_loc;
		next_loc++;
		control->reg_15->address = next_loc;
		next_loc += control->reg_15->length;
		control->reg_16->address = next_loc;
		next_loc += control->reg_16->length;
	}
	reg_num++;

	if (instance_data->query.has_sense_frequency_control == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		size = instance_data->query.number_of_sensing_frequencies;
		
		control->reg_17 = 
			kzalloc(sizeof(struct f54_ad_control_17), GFP_KERNEL);
		if (!control->reg_17) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_17->length = size;
		control->reg_17->regs = kzalloc(size * sizeof(struct f54_ad_control_17n), GFP_KERNEL);
		if (!control->reg_17->regs) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		
		control->reg_18 = 
			kzalloc(sizeof(struct f54_ad_control_18), GFP_KERNEL);
		if (!control->reg_18) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_18->length = size;
		control->reg_18->regs = kzalloc(size * sizeof(struct f54_ad_control_18n), GFP_KERNEL);
		if (!control->reg_18->regs) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		
		control->reg_19 = 
			kzalloc(sizeof(struct f54_ad_control_19), GFP_KERNEL);
		if (!control->reg_19) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_19->length = size;
		control->reg_19->regs = kzalloc(size * sizeof(struct f54_ad_control_19n), GFP_KERNEL);
		if (!control->reg_19->regs) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		
		control->reg_17->address = next_loc;
		next_loc += size;
		control->reg_18->address = next_loc;
		next_loc += size;
		control->reg_19->address = next_loc;
		next_loc += size;
	}
	reg_num++;

	attrs_ctrl_regs_exist[reg_num] = true;
	reg_num++;
	control->reg_20 = kzalloc(sizeof(union f54_ad_control_20), GFP_KERNEL);
	control->reg_20->address = next_loc;
	next_loc++;

	if (instance_data->query.has_sense_frequency_control == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_21 = kzalloc(sizeof(union f54_ad_control_21),
								GFP_KERNEL);
		control->reg_21->address = next_loc;
		next_loc += sizeof(control->reg_21->regs);
	}
	reg_num++;

	if (instance_data->query.has_sense_frequency_control == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_22__26 = kzalloc(
			sizeof(union f54_ad_control_22__26), GFP_KERNEL);
		control->reg_22__26->address = next_loc;
		next_loc+=sizeof(control->reg_22__26->regs);
	}
	reg_num++;

	if (instance_data->query.has_iir_filter == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_27 = kzalloc(sizeof(union f54_ad_control_27),
								GFP_KERNEL);
		control->reg_27->address = next_loc;
		next_loc++;
	}
	reg_num++;

	if (instance_data->query.has_firmware_noise_mitigation == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_28 = kzalloc(sizeof(union f54_ad_control_28),
								GFP_KERNEL);
		control->reg_28->address = next_loc;
		next_loc += sizeof(control->reg_28->regs);
	}
	reg_num++;

	if (instance_data->query.has_cmn_removal == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_29 = kzalloc(sizeof(union f54_ad_control_29),
								GFP_KERNEL);
		control->reg_29->address = next_loc;
		next_loc++;
	}
	reg_num++;

	if (instance_data->query.has_cmn_maximum == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_30 = kzalloc(sizeof(union f54_ad_control_30),
								GFP_KERNEL);
		control->reg_30->address = next_loc;
		next_loc++;
	}
	reg_num++;

	if (instance_data->query.has_touch_hysteresis == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_31 = kzalloc(sizeof(union f54_ad_control_31),
								GFP_KERNEL);
		control->reg_31->address = next_loc;
		next_loc++;
	}
	reg_num++;

	if (instance_data->query.has_interference_metric == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		control->reg_32__35 = kzalloc(
			sizeof(union f54_ad_control_32__35), GFP_KERNEL);
		control->reg_32__35->address = next_loc;
		next_loc += 8;
	}
	reg_num++;

	if (instance_data->query.curve_compensation_mode == 1) {
		size = max(instance_data->query.num_of_rx_electrodes,
				instance_data->query.num_of_tx_electrodes);
	}
	if (instance_data->query.curve_compensation_mode == 2)
		size = instance_data->query.num_of_rx_electrodes;
	if (instance_data->query.curve_compensation_mode == 1
			|| instance_data->query.curve_compensation_mode == 2) {
		attrs_ctrl_regs_exist[reg_num] = true;
		
		control->reg_36 = 
			kzalloc(sizeof(struct f54_ad_control_36), GFP_KERNEL);
		if (!control->reg_36) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_36->length = size;
		control->reg_36->regs = kzalloc(size * sizeof(struct f54_ad_control_36n), GFP_KERNEL);
		if (!control->reg_36->regs) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}

		control->reg_36->address = next_loc;
		next_loc += size;
	}
	reg_num++;

	if (instance_data->query.curve_compensation_mode == 2) {
		attrs_ctrl_regs_exist[reg_num] = true;
		
		control->reg_37 = 
			kzalloc(sizeof(struct f54_ad_control_37), GFP_KERNEL);
		if (!control->reg_37) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_37->length = instance_data->query.num_of_tx_electrodes;
		control->reg_37->regs = kzalloc(control->reg_37->length
					* sizeof(struct f54_ad_control_37n), GFP_KERNEL);
		if (!control->reg_37->regs) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}

		control->reg_37->address = next_loc;
		next_loc += control->reg_37->length;
	}
	reg_num++;

	if (instance_data->query.has_per_frequency_noise_control == 1) {
		attrs_ctrl_regs_exist[reg_num] = true;
		
		control->reg_38 = 
			kzalloc(sizeof(struct f54_ad_control_38), GFP_KERNEL);
		if (!control->reg_38) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_38->length = instance_data->query.number_of_sensing_frequencies;
		control->reg_38->regs = kzalloc(control->reg_38->length
							* sizeof(struct f54_ad_control_38n), GFP_KERNEL);
		if (!control->reg_38->regs) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}

		control->reg_39 = 
			kzalloc(sizeof(struct f54_ad_control_39), GFP_KERNEL);
		if (!control->reg_39) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_39->length = instance_data->query.number_of_sensing_frequencies;
		control->reg_39->regs = kzalloc(control->reg_39->length
					* sizeof(struct f54_ad_control_39n), GFP_KERNEL);
		if (!control->reg_39->regs) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}

		control->reg_40 = 
			kzalloc(sizeof(struct f54_ad_control_40), GFP_KERNEL);
		if (!control->reg_40) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}
		control->reg_40->length = instance_data->query.number_of_sensing_frequencies;
		control->reg_40->regs = kzalloc(control->reg_40->length
					* sizeof(struct f54_ad_control_40n), GFP_KERNEL);
		if (!control->reg_40->regs) {
			dev_err(&fc->dev, "Failed to allocate control registers.");
			return -ENOMEM;
		}

		control->reg_38->address = next_loc;
		next_loc += control->reg_38->length;
		control->reg_39->address = next_loc;
		next_loc += control->reg_39->length;
		control->reg_40->address = next_loc;
		next_loc += control->reg_40->length;
	}
	reg_num++;
/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */
	mutex_init(&instance_data->data_mutex);

	mutex_init(&instance_data->status_mutex);

	mutex_init(&instance_data->control_mutex);
	
	return retval;
}

static void set_report_size(struct rmi_fn_54_data *data)
{
	u8 rx = data->query.num_of_rx_electrodes;
	u8 tx = data->query.num_of_tx_electrodes;
	switch (data->report_type) {
	case F54_8BIT_IMAGE:
		data->report_size = rx * tx;
		break;
	case F54_16BIT_IMAGE:
	case F54_RAW_16BIT_IMAGE:
	case F54_TRUE_BASELINE:
	case F54_FULL_RAW_CAP:
	case F54_FULL_RAW_CAP_RX_COUPLING_COMP:
		data->report_size = 2 * rx * tx;
		break;
	case F54_HIGH_RESISTANCE:
		data->report_size = RMI_54_HIGH_RESISTANCE_SIZE;
		break;
	case F54_FULL_RAW_CAP_MIN_MAX:
		data->report_size = RMI_54_FULL_RAW_CAP_MIN_MAX_SIZE;
		break;
	case F54_TX_TO_TX_SHORT:
	case F54_TX_OPEN:
	case F54_TX_TO_GROUND:
		data->report_size =  (tx + 7) / 8;
		break;
	case F54_RX_TO_RX1:
	case F54_RX_OPENS1:
		if (rx < tx)
			data->report_size = 2 * rx * rx;
		else
			data->report_size = 2 * rx * tx;
		break;
	case F54_RX_TO_RX2:
	case F54_RX_OPENS2:
		if (rx <= tx)
			data->report_size = 0;
		else
			data->report_size = 2 * rx * (rx - tx);
		break;
	default:
		data->report_size = 0;
	}
}

int rmi_f54_attention(struct rmi_function_container *fc, u8 *irq_bits)
{
	struct rmi_driver *driver = fc->rmi_dev->driver;
	char fifo[2];
	struct rmi_fn_54_data *data = fc->data;
	int error = 0;

	set_report_size(data);
	if (data->report_size == 0) {
		dev_err(&fc->dev, "Invalid report type set in %s. "
				"This should never happen.\n", __func__);
		error = -EINVAL;
		goto error_exit;
	}
	/*
	 * We need to ensure the buffer is big enough. A Buffer size of 0 means
	 * that the buffer has not been allocated.
	 */
	if (data->bufsize < data->report_size) {
		mutex_lock(&data->data_mutex);
		if (data->bufsize > 0)
			kfree(data->report_data);
		data->report_data = kzalloc(data->report_size, GFP_KERNEL);
		if (!data->report_data) {
			dev_err(&fc->dev, "Failed to allocate report_data.\n");
			error = -ENOMEM;
			data->bufsize = 0;
			mutex_unlock(&data->data_mutex);
			goto error_exit;
		}
		data->bufsize = data->report_size;
		mutex_unlock(&data->data_mutex);
	}
	dev_vdbg(&fc->dev, "F54 Interrupt handler is running.\nSize: %d\n",
		 data->report_size);
	/* Write 0 to fifohi and fifolo. */
	fifo[0] = 0;
	fifo[1] = 0;
	error = rmi_write_block(fc->rmi_dev, fc->fd.data_base_addr
				+ RMI_F54_FIFO_OFFSET, fifo,	sizeof(fifo));
	if (error < 0)
		dev_err(&fc->dev, "Failed to write fifo to zero!\n");
	else
		error = rmi_read_block(fc->rmi_dev,
			fc->fd.data_base_addr + RMI_F54_REPORT_DATA_OFFSET,
			data->report_data, data->report_size);
	if (error < 0)
		dev_err(&fc->dev, "F54 data read failed. Code: %d.\n", error);
	else if (error != data->report_size) {
		error = -EINVAL;
		goto error_exit;
	}
#if RAW_HEX
	int l;
	/* Debugging: Print out the file in hex. */
	pr_info("Report data (raw hex):\n");
	for (l = 0; l < data->report_size; l += 2) {
		pr_info("%03d: 0x%02x%02x\n", l/2,
			data->report_data[l+1], data->report_data[l]);
	}
#endif
#if HUMAN_READABLE
	/* Debugging: Print out file in human understandable image */
	switch (data->report_type) {
	case F54_16BIT_IMAGE:
	case F54_RAW_16BIT_IMAGE:
	case F54_TRUE_BASELINE:
	case F54_FULL_RAW_CAP:
	case F54_FULL_RAW_CAP_RX_COUPLING_COMP:
		pr_info("Report data (Image):\n");
		int i, j, k;
		char c[2];
		short s;
		k = 0;
		for (i = 0; i < data->query.num_of_tx_electrodes;
									i++) {
			for (j = 0; j <
			     data->query.num_of_rx_electrodes; j++) {
				c[0] = data->report_data[k];
				c[1] = data->report_data[k+1];
				memcpy(&s, &c, 2);
				if (s < -64)
					printk(".");
				else if (s < 0)
					printk("-");
				else if (s > 64)
					printk("*");
				else if (s > 0)
					printk("+");
				else
					printk("0");
				k += 2;
			}
			pr_info("\n");
		}
		pr_info("EOF\n");
		break;
	default:
		pr_info("Report type %d debug image not supported",
							data->report_type);
	}
#endif
	error = IDLE;
error_exit:
	mutex_lock(&data->status_mutex);
	/* Turn back on other interupts, if it
	 * appears that we turned them off. */
	if (driver->restore_irq_mask) {
		dev_dbg(&fc->dev, "Restoring interupts!\n");
		driver->restore_irq_mask(fc->rmi_dev);
	} else {
		dev_err(&fc->dev, "No way to restore interrupts!\n");
	}
	data->status = error;
	mutex_unlock(&data->status_mutex);
	return data->status;
}


#if F54_WATCHDOG
static void clear_status_worker(struct work_struct *work)
{
	struct rmi_fn_54_data *data = container_of(work,
					struct rmi_fn_54_data, work);
	struct rmi_function_container *fc = data->fc;
	struct rmi_driver *driver = fc->rmi_dev->driver;
	char command;
	int result;

	mutex_lock(&data->status_mutex);
	if (data->status == BUSY) {
		pr_info("F54 Timout Occured: Determining status.\n");
		result = rmi_read_block(fc->rmi_dev, fc->fd.command_base_addr,
								&command, 1);
		if (result < 0) {
			dev_err(&fc->dev, "Could not read get_report register "
				"from 0x%04x\n", fc->fd.command_base_addr);
			data->status = -ETIMEDOUT;
		} else {
			if (command & GET_REPORT) {
				dev_warn(&fc->dev, "Report type unsupported!");
				data->status = -EINVAL;
			} else {
				data->status = -ETIMEDOUT;
			}
		}
		if (driver->restore_irq_mask) {
			dev_dbg(&fc->dev, "Restoring interupts!\n");
			driver->restore_irq_mask(fc->rmi_dev);
		} else {
			dev_err(&fc->dev, "No way to restore interrupts!\n");
		}
	}
	mutex_unlock(&data->status_mutex);
}

static enum hrtimer_restart clear_status(struct hrtimer *timer)
{
	struct rmi_fn_54_data *data = container_of(timer,
					struct rmi_fn_54_data, watchdog);
	schedule_work(&(data->work));
	return HRTIMER_NORESTART;
}
#endif

/* Check if report_type is valid */
static bool is_report_type_valid(enum f54_report_types reptype)
{
	/* Basic checks on report_type to ensure we write a valid type
	 * to the sensor.
	 * TODO: Check Query3 to see if some specific reports are
	 * available. This is currently listed as a reserved register.
	 */
	switch (reptype) {
	case F54_8BIT_IMAGE:
	case F54_16BIT_IMAGE:
	case F54_RAW_16BIT_IMAGE:
	case F54_HIGH_RESISTANCE:
	case F54_TX_TO_TX_SHORT:
	case F54_RX_TO_RX1:
	case F54_TRUE_BASELINE:
	case F54_FULL_RAW_CAP_MIN_MAX:
	case F54_RX_OPENS1:
	case F54_TX_OPEN:
	case F54_TX_TO_GROUND:
	case F54_RX_TO_RX2:
	case F54_RX_OPENS2:
	case F54_FULL_RAW_CAP:
	case F54_FULL_RAW_CAP_RX_COUPLING_COMP:
		return true;
		break;
	default:
		return false;
	}
}

/* SYSFS file show/store functions */
static ssize_t rmi_fn_54_report_type_show(struct device *dev,
				struct device_attribute *attr, char *buf) {
	struct rmi_function_container *fc;
	struct rmi_fn_54_data *instance_data;

	fc = to_rmi_function_container(dev);
	instance_data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%u\n", instance_data->report_type);
}

static ssize_t rmi_fn_54_report_type_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count) {
	int result;
	unsigned long val;
	unsigned char data;
	struct rmi_function_container *fc;
	struct rmi_fn_54_data *instance_data;
	fc = to_rmi_function_container(dev);
	instance_data = fc->data;

	/* need to convert the string data to an actual value */
	result = strict_strtoul(buf, 10, &val);
	if (result)
		return result;
	if (!is_report_type_valid(val)) {
		dev_err(dev, "%s : Report type %d is invalid.\n",
					__func__, (u8) val);
		return -EINVAL;
	}
	mutex_lock(&instance_data->status_mutex);
	if (instance_data->status != BUSY) {
		instance_data->report_type = (enum f54_report_types)val;
		data = (char)val;
		/* Write the Report Type back to the first Block
		 * Data registers (F54_AD_Data0). */
		result =
		    rmi_write_block(fc->rmi_dev, fc->fd.data_base_addr,
								&data, 1);
		mutex_unlock(&instance_data->status_mutex);
		if (result < 0) {
			dev_err(dev, "%s : Could not write report type to"
				" 0x%x\n", __func__, fc->fd.data_base_addr);
			return result;
		}
		return count;
	} else {
		dev_err(dev, "%s : Report type cannot be changed in the middle"
				" of command.\n", __func__);
		mutex_unlock(&instance_data->status_mutex);
		return -EINVAL;
	}
}

static ssize_t rmi_fn_54_get_report_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count) {
	unsigned long val;
	int error, result;
	struct rmi_function_container *fc;
	struct rmi_fn_54_data *instance_data;
	struct rmi_driver *driver;
	u8 command;
	fc = to_rmi_function_container(dev);
	instance_data = fc->data;
	driver = fc->rmi_dev->driver;

	/* need to convert the string data to an actual value */
	error = strict_strtoul(buf, 10, &val);
	if (error)
		return error;
	/* Do nothing if not set to 1. This prevents accidental commands. */
	if (val != 1)
		return count;
	command = (unsigned char)GET_REPORT;
	/* Basic checks on report_type to ensure we write a valid type
	 * to the sensor.
	 * TODO: Check Query3 to see if some specific reports are
	 * available. This is currently listed as a reserved register.
	 */
	if (!is_report_type_valid(instance_data->report_type)) {
		dev_err(dev, "%s : Report type %d is invalid.\n",
				__func__, instance_data->report_type);
		return -EINVAL;
	}
	mutex_lock(&instance_data->status_mutex);
	if (instance_data->status != IDLE) {
		if (instance_data->status != BUSY) {
			dev_err(dev, "F54 status is in an abnormal state: 0x%x",
							instance_data->status);
		}
		mutex_unlock(&instance_data->status_mutex);
		return count;
	}
	/* Store interrupts */
	/* Do not exit if we fail to turn off interupts. We are likely
	 * to still get useful data. The report data can, however, be
	 * corrupted, and there may be unexpected behavior.
	 */
	dev_dbg(dev, "Storing and overriding interupts\n");
	if (driver->store_irq_mask)
		driver->store_irq_mask(fc->rmi_dev,
					fc->irq_mask);
	else
		dev_err(dev, "No way to store interupts!\n");
	instance_data->status = BUSY;

	/* small delay to avoid race condition in firmare. This value is a bit
	 * higher than absolutely necessary. Should be removed once issue is
	 * resolved in firmware. */

	mdelay(2);

	/* Write the command to the command register */
	result = rmi_write_block(fc->rmi_dev, fc->fd.command_base_addr,
						&command, 1);
	mutex_unlock(&instance_data->status_mutex);
	if (result < 0) {
		dev_err(dev, "%s : Could not write command to 0x%x\n",
				__func__, fc->fd.command_base_addr);
		return result;
	}
#if F54_WATCHDOG
	/* start watchdog timer */
	hrtimer_start(&instance_data->watchdog, ktime_set(1, 0),
							HRTIMER_MODE_REL);
#endif
	return count;
}

static ssize_t rmi_fn_54_force_cal_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count) {
	unsigned long val;
	int error, result;
	struct rmi_function_container *fc;
	struct rmi_fn_54_data *instance_data;
	struct rmi_driver *driver;
	u8 command;

	fc = to_rmi_function_container(dev);
	instance_data = fc->data;
	driver = fc->rmi_dev->driver;

	/* need to convert the string data to an actual value */
	error = strict_strtoul(buf, 10, &val);
	if (error)
		return error;
	/* Do nothing if not set to 1. This prevents accidental commands. */
	if (val != 1)
		return count;

	command = (unsigned char)FORCE_CAL;

	if (instance_data->status == BUSY)
		return -EBUSY;
	/* Write the command to the command register */
	result = rmi_write_block(fc->rmi_dev, fc->fd.command_base_addr,
						&command, 1);
	if (result < 0) {
		dev_err(dev, "%s : Could not write command to 0x%x\n",
				__func__, fc->fd.command_base_addr);
		return result;
	}
	return count;
}

static ssize_t rmi_fn_54_status_show(struct device *dev,
				struct device_attribute *attr, char *buf) {
	struct rmi_function_container *fc;
	struct rmi_fn_54_data *instance_data;

	fc = to_rmi_function_container(dev);
	instance_data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%d\n", instance_data->status);
}

simple_show_union_struct_unsigned(query, num_of_rx_electrodes)
simple_show_union_struct_unsigned(query, num_of_tx_electrodes)
simple_show_union_struct_unsigned(query, has_image16)
simple_show_union_struct_unsigned(query, has_image8)
simple_show_union_struct_unsigned(query, has_baseline)
simple_show_union_struct_unsigned(query, clock_rate)
simple_show_union_struct_unsigned(query, touch_controller_family)
simple_show_union_struct_unsigned(query, has_pixel_touch_threshold_adjustment)
simple_show_union_struct_unsigned(query, has_sensor_assignment)
simple_show_union_struct_unsigned(query, has_interference_metric)
simple_show_union_struct_unsigned(query, has_sense_frequency_control)
simple_show_union_struct_unsigned(query, has_firmware_noise_mitigation)
simple_show_union_struct_unsigned(query, has_two_byte_report_rate)
simple_show_union_struct_unsigned(query, has_one_byte_report_rate)
simple_show_union_struct_unsigned(query, has_relaxation_control)
simple_show_union_struct_unsigned(query, curve_compensation_mode)
simple_show_union_struct_unsigned(query, has_iir_filter)
simple_show_union_struct_unsigned(query, has_cmn_removal)
simple_show_union_struct_unsigned(query, has_cmn_maximum)
simple_show_union_struct_unsigned(query, has_touch_hysteresis)
simple_show_union_struct_unsigned(query, has_edge_compensation)
simple_show_union_struct_unsigned(query, has_per_frequency_noise_control)
simple_show_union_struct_unsigned(query, number_of_sensing_frequencies)

static ssize_t rmi_fn_54_no_auto_cal_show(struct device *dev,
				struct device_attribute *attr, char *buf) {
	struct rmi_function_container *fc;
	struct rmi_fn_54_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%u\n",
				data->no_auto_cal ? 1 : 0);
}

static ssize_t rmi_fn_54_no_auto_cal_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count) {
	int result;
	unsigned long val;
	unsigned char data;
	struct rmi_function_container *fc;
	struct rmi_fn_54_data *instance_data;

	fc = to_rmi_function_container(dev);
	instance_data = fc->data;

	/* need to convert the string data to an actual value */
	result = strict_strtoul(buf, 10, &val);

	/* if an error occured, return it */
	if (result)
		return result;
	/* Do nothing if not 0 or 1. This prevents accidental commands. */
	if (val > 1)
		return -EINVAL;
	/* Read current control values */
	result =
	    rmi_read_block(fc->rmi_dev, fc->fd.control_base_addr, &data, 1);

	/* if the current control registers are already set as we want them, do
	 * nothing to them */
	if ((data & NO_AUTO_CAL_MASK) == val)
		return count;
	/* Write the control back to the control register (F54_AD_Ctrl0)
	 * Ignores everything but bit 0 */
	data = (data & ~NO_AUTO_CAL_MASK) | (val & NO_AUTO_CAL_MASK);
	result =
	    rmi_write_block(fc->rmi_dev, fc->fd.control_base_addr, &data, 1);
	if (result < 0) {
		dev_err(dev, "%s : Could not write control to 0x%x\n",
		       __func__, fc->fd.control_base_addr);
		return result;
	}
	/* update our internal representation iff the write succeeds */
	instance_data->no_auto_cal = (val == 1);
	return count;
}

static ssize_t rmi_fn_54_fifoindex_show(struct device *dev,
				  struct device_attribute *attr, char *buf) {
	struct rmi_function_container *fc;
	struct rmi_fn_54_data *instance_data;
	struct rmi_driver *driver;
	unsigned char temp_buf[2];
	int retval;

	fc = to_rmi_function_container(dev);
	instance_data = fc->data;
	driver = fc->rmi_dev->driver;

	/* Read fifoindex from device */
	retval = rmi_read_block(fc->rmi_dev,
				fc->fd.data_base_addr + RMI_F54_FIFO_OFFSET,
				temp_buf, ARRAY_SIZE(temp_buf));

	if (retval < 0) {
		dev_err(dev, "Could not read fifoindex from 0x%04x\n",
		       fc->fd.data_base_addr + RMI_F54_FIFO_OFFSET);
		return retval;
	}
	batohs(&instance_data->fifoindex, temp_buf);
	return snprintf(buf, PAGE_SIZE, "%u\n", instance_data->fifoindex);
}
static ssize_t rmi_fn_54_fifoindex_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf,
					size_t count)
{
	int error;
	unsigned long val;
	unsigned char data[2];
	struct rmi_function_container *fc;
	struct rmi_fn_54_data *instance_data;

	fc = to_rmi_function_container(dev);
	instance_data = fc->data;

	/* need to convert the string data to an actual value */
	error = strict_strtoul(buf, 10, &val);

	if (error)
		return error;

	instance_data->fifoindex = val;

	/* Write the FifoIndex back to the first data registers. */
	hstoba(data, (unsigned short)val);

	error = rmi_write_block(fc->rmi_dev,
				fc->fd.data_base_addr + RMI_F54_FIFO_OFFSET,
				data,
				ARRAY_SIZE(data));

	if (error < 0) {
		dev_err(dev, "%s : Could not write fifoindex to 0x%x\n",
		       __func__, fc->fd.data_base_addr + RMI_F54_FIFO_OFFSET);
		return error;
	}
	return count;
}

/* Provide access to last report */
#ifdef KERNEL_VERSION_ABOVE_2_6_32
static ssize_t rmi_fn_54_data_read(struct file *data_file, struct kobject *kobj,
				struct bin_attribute *attributes,
				char *buf, loff_t pos, size_t count)
#else
static ssize_t rmi_fn_54_data_read(struct kobject *kobj,
				struct bin_attribute *attributes,
				char *buf, loff_t pos, size_t count)
#endif
{
	struct device *dev;
	struct rmi_function_container *fc;
	struct rmi_fn_54_data *instance_data;

	dev = container_of(kobj, struct device, kobj);
	fc = to_rmi_function_container(dev);
	instance_data = fc->data;
	mutex_lock(&instance_data->data_mutex);
	if (count < instance_data->report_size) {
		dev_err(dev,
			"%s: F54 report size too large for buffer: %d."
				" Need at least: %d for Report type: %d.\n",
			__func__, count, instance_data->report_size,
			instance_data->report_type);
		mutex_unlock(&instance_data->data_mutex);
		return -EINVAL;
	}
	if (instance_data->report_data) {
		/* Copy data from instance_data to buffer */
		memcpy(buf, instance_data->report_data,
					instance_data->report_size);
		mutex_unlock(&instance_data->data_mutex);
		dev_dbg(dev, "%s: Presumably successful.", __func__);
		return instance_data->report_size;
	} else {
		dev_err(dev, "%s: F54 report_data does not exist!\n", __func__);
		mutex_unlock(&instance_data->data_mutex);
		return -EINVAL;
	}
}

/* Repeated Register sysfs functions */
show_repeated_union_struct_unsigned(control, reg_15, sensor_rx_assignment)
show_repeated_union_struct_unsigned(control, reg_16, sensor_tx_assignment)

show_repeated_union_struct_unsigned(control, reg_17, filter_bandwidth)
show_repeated_union_struct_unsigned(control, reg_17, disable)


static ssize_t rmi_fn_54_burst_count_show(struct device *dev,
					struct device_attribute *attr,
					char *buf) {
	struct rmi_function_container *fc;
	struct rmi_fn_54_data *data;
	int result, size = 0;
	char *temp;
	int i;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	mutex_lock(&data->control_mutex);
	/* Read current control values */
	result = rmi_read_block(fc->rmi_dev, data->control.reg_17->address,
			(u8 *) data->control.reg_17->regs,
			data->control.reg_17->length * sizeof(u8));
	if (result < 0) {
		dev_err(dev, "%s : Could not read control at 0x%x\n"
					"Data may be outdated.", __func__,
					data->control.reg_17->address);
	}

	result = rmi_read_block(fc->rmi_dev, data->control.reg_18->address,
			(u8 *)data->control.reg_18->regs,
			data->control.reg_18->length * sizeof(u8));
	if (result < 0) {
		dev_err(dev, "%s : Could not read control at 0x%x\n"
					"Data may be outdated.", __func__,
					data->control.reg_18->address);
	}
	mutex_unlock(&data->control_mutex);
	temp = buf;
	for (i = 0; i < data->control.reg_17->length; i++) {
		result = snprintf(temp, PAGE_SIZE - size, "%u ",
			(1<<8) * data->control.reg_17->regs[i].burst_countb10__8
				+ data->control.reg_18->regs[i].burst_countb7__0n);
		size += result;
		temp += result;
	}
	return size + snprintf(temp, PAGE_SIZE - size, "\n");
}

show_repeated_union_struct_unsigned(control, reg_19, stretch_duration)
show_store_repeated_union_struct_unsigned(control, reg_36, axis1_comp)
show_store_repeated_union_struct_unsigned(control, reg_37, axis2_comp)

show_repeated_union_struct_unsigned(control, reg_38, noise_control_1)
show_repeated_union_struct_unsigned(control, reg_39, noise_control_2)
show_repeated_union_struct_unsigned(control, reg_40, noise_control_3)

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */

show_store_union_struct_unsigned(control, reg_0, no_relax)
show_store_union_struct_unsigned(control, reg_0, no_scan)
show_store_union_struct_unsigned(control, reg_1, bursts_per_cluster)
show_store_union_struct_unsigned(control, reg_2, saturation_cap)
show_store_union_struct_unsigned(control, reg_3, pixel_touch_threshold)
show_store_union_struct_unsigned(control, reg_4__6, rx_feedback_cap)
show_store_union_struct_unsigned(control, reg_4__6, low_ref_cap)
show_store_union_struct_unsigned(control, reg_4__6, low_ref_feedback_cap)
show_store_union_struct_unsigned(control, reg_4__6, low_ref_polarity)
show_store_union_struct_unsigned(control, reg_4__6, high_ref_cap)
show_store_union_struct_unsigned(control, reg_4__6, high_ref_feedback_cap)
show_store_union_struct_unsigned(control, reg_4__6, high_ref_polarity)
show_store_union_struct_unsigned(control, reg_7, cbc_cap)
show_store_union_struct_unsigned(control, reg_7, cbc_polarity)
show_store_union_struct_unsigned(control, reg_7, cbc_tx_carrier_selection)
show_store_union_struct_unsigned(control, reg_8__9, integration_duration)
show_store_union_struct_unsigned(control, reg_8__9, reset_duration)
show_store_union_struct_unsigned(control, reg_10, noise_sensing_bursts_per_image)
show_store_union_struct_unsigned(control, reg_12__13, slow_relaxation_rate)
show_store_union_struct_unsigned(control, reg_12__13, fast_relaxation_rate)
show_store_union_struct_unsigned(control, reg_14, rxs_on_xaxis)
show_store_union_struct_unsigned(control, reg_14, curve_comp_on_txs)
show_store_union_struct_unsigned(control, reg_20, disable_noise_mitigation)
show_store_union_struct_unsigned(control, reg_21, freq_shift_noise_threshold)
show_store_union_struct_unsigned(control, reg_22__26, noise_density_threshold)
show_store_union_struct_unsigned(control, reg_22__26, medium_noise_threshold)
show_store_union_struct_unsigned(control, reg_22__26, high_noise_threshold)
show_store_union_struct_unsigned(control, reg_22__26, noise_density)
show_store_union_struct_unsigned(control, reg_22__26, frame_count)
show_store_union_struct_unsigned(control, reg_27, iir_filter_coef)
show_store_union_struct_unsigned(control, reg_28, quiet_threshold)
show_store_union_struct_unsigned(control, reg_29, cmn_filter_disable)
show_store_union_struct_unsigned(control, reg_30, cmn_filter_max)
show_store_union_struct_unsigned(control, reg_31, touch_hysteresis)
show_store_union_struct_unsigned(control, reg_32__35, rx_low_edge_comp)
show_store_union_struct_unsigned(control, reg_32__35, rx_high_edge_comp)
show_store_union_struct_unsigned(control, reg_32__35, tx_low_edge_comp)
show_store_union_struct_unsigned(control, reg_32__35, tx_high_edge_comp)

/* !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! */
static struct rmi_function_handler function_handler = {
	.func = 0x54,
	.init = rmi_f54_init,
	.config = NULL,
	.reset = rmi_f54_reset,
	.attention = rmi_f54_attention,
	.remove = rmi_f54_remove
};

static int __init rmi_f54_module_init(void)
{
	int error;

	error = rmi_register_function_driver(&function_handler);
	if (error < 0) {
		pr_err("%s: register failed!\n", __func__);
		return error;
	}
	return 0;
}

static void rmi_f54_module_exit(void)
{
	rmi_unregister_function_driver(&function_handler);
}

module_init(rmi_f54_module_init);
module_exit(rmi_f54_module_exit);

MODULE_AUTHOR("Daniel Rosenberg <daniel.rosenberg@synaptics.com>");
MODULE_DESCRIPTION("RMI F54 module");
MODULE_LICENSE("GPL");
MODULE_VERSION(RMI_DRIVER_VERSION);

