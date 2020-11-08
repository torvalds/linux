/*
 * cyttsp5_device_access.c
 * Parade TrueTouch(TM) Standard Product V5 Device Access Module.
 * Configuration and Test command/status user interface.
 * For use with Parade touchscreen controllers.
 * Supported parts include:
 * CYTMA5XX
 * CYTMA448
 * CYTMA445A
 * CYTT21XXX
 * CYTT31XXX
 *
 * Copyright (C) 2015 Parade Technologies
 * Copyright (C) 2012-2015 Cypress Semiconductor
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contact Parade Technologies at www.paradetech.com <ttdrivers@paradetech.com>
 *
 */

#include "cyttsp5_regs.h"
#include <linux/firmware.h>

#include <linux/timer.h>
#include <linux/timex.h>
#include <linux/rtc.h>

#define CY_CMCP_THRESHOLD_FILE_NAME "cyttsp5_thresholdfile.csv"
#define CMCP_THRESHOLD_FILE_NAME "ttdl_cmcp_thresholdfile.csv"

/* Max test case number */
#define MAX_CASE_NUM            (22)

/* ASCII */
#define ASCII_LF                (0x0A)
#define ASCII_CR                (0x0D)
#define ASCII_COMMA             (0x2C)
#define ASCII_ZERO              (0x30)
#define ASCII_NINE              (0x39)

/* Max characters of test case name */
#define NAME_SIZE_MAX           (50)

/* Max sensor and button number */
#define MAX_BUTTONS             (HID_SYSINFO_MAX_BTN)
#define MAX_SENSORS             (1024)
#define MAX_TX_SENSORS          (128)
#define MAX_RX_SENSORS          (128)

/* Multiply by 2 for double (min, max) values */
#define TABLE_BUTTON_MAX_SIZE   (MAX_BUTTONS * 2)
#define TABLE_SENSOR_MAX_SIZE   (MAX_SENSORS * 2)
#define TABLE_TX_MAX_SIZE       (MAX_TX_SENSORS*2)
#define TABLE_RX_MAX_SIZE       (MAX_RX_SENSORS*2)

#define CM_PANEL_DATA_OFFSET    (6)
#define CM_BTN_DATA_OFFSET      (6)
#define CP_PANEL_DATA_OFFSET    (6)
#define CP_BTN_DATA_OFFSET      (6)
#define MAX_BUF_LEN             (50000)

/* cmcp csv file information */
struct configuration {
	u32 cm_range_limit_row;
	u32 cm_range_limit_col;
	u32 cm_min_limit_cal;
	u32 cm_max_limit_cal;
	u32 cm_max_delta_sensor_percent;
	u32 cm_max_delta_button_percent;
	u32 min_sensor_rx;
	u32 max_sensor_rx;
	u32 min_sensor_tx;
	u32 max_sensor_tx;
	u32 min_button;
	u32 max_button;
	u32 max_delta_sensor;
	u32 cp_max_delta_sensor_rx_percent;
	u32 cp_max_delta_sensor_tx_percent;
	u32 cm_min_max_table_button[TABLE_BUTTON_MAX_SIZE];
	u32 cp_min_max_table_button[TABLE_BUTTON_MAX_SIZE];
	u32 cm_min_max_table_sensor[TABLE_SENSOR_MAX_SIZE];
	u32 cp_min_max_table_rx[TABLE_RX_MAX_SIZE];
	u32 cp_min_max_table_tx[TABLE_TX_MAX_SIZE];
	u32 cm_min_max_table_button_size;
	u32 cp_min_max_table_button_size;
	u32 cm_min_max_table_sensor_size;
	u32 cp_min_max_table_rx_size;
	u32 cp_min_max_table_tx_size;
	u32 cp_max_delta_button_percent;
	u32 cm_max_table_gradient_cols_percent[TABLE_TX_MAX_SIZE];
	u32 cm_max_table_gradient_cols_percent_size;
	u32 cm_max_table_gradient_rows_percent[TABLE_RX_MAX_SIZE];
	u32 cm_max_table_gradient_rows_percent_size;
	u32 cm_excluding_row_edge;
	u32 cm_excluding_col_edge;
	u32 rx_num;
	u32 tx_num;
	u32 btn_num;
	u32 cm_enabled;
	u32 cp_enabled;
	u32 is_valid_or_not;
};

/* Test case search definition */
struct test_case_search {
	char name[NAME_SIZE_MAX]; /* Test case name */
	u32 name_size;            /* Test case name size */
	u32 offset;               /* Test case offset */
};

/* Test case field definition */
struct test_case_field {
	char *name;     /* Test case name */
	u32 name_size;  /* Test case name size */
	u32 type;       /* Test case type */
	u32 *bufptr;    /* Buffer to store value information */
	u32 exist_or_not;/* Test case exist or not */
	u32 data_num;   /* Buffer data number */
	u32 line_num;   /* Buffer line number */
};

/* Test case type */
enum test_case_type {
	TEST_CASE_TYPE_NO,
	TEST_CASE_TYPE_ONE,
	TEST_CASE_TYPE_MUL,
	TEST_CASE_TYPE_MUL_LINES,
};

/* Test case order in test_case_field_array */
enum case_order {
	CM_TEST_INPUTS,
	CM_EXCLUDING_COL_EDGE,
	CM_EXCLUDING_ROW_EDGE,
	CM_GRADIENT_CHECK_COL,
	CM_GRADIENT_CHECK_ROW,
	CM_RANGE_LIMIT_ROW,
	CM_RANGE_LIMIT_COL,
	CM_MIN_LIMIT_CAL,
	CM_MAX_LIMIT_CAL,
	CM_MAX_DELTA_SENSOR_PERCENT,
	CM_MAX_DELTA_BUTTON_PERCENT,
	PER_ELEMENT_MIN_MAX_TABLE_BUTTON,
	PER_ELEMENT_MIN_MAX_TABLE_SENSOR,
	CP_TEST_INPUTS,
	CP_MAX_DELTA_SENSOR_RX_PERCENT,
	CP_MAX_DELTA_SENSOR_TX_PERCENT,
	CP_MAX_DELTA_BUTTON_PERCENT,
	CP_PER_ELEMENT_MIN_MAX_BUTTON,
	MIN_BUTTON,
	MAX_BUTTON,
	PER_ELEMENT_MIN_MAX_RX,
	PER_ELEMENT_MIN_MAX_TX,
	CASE_ORDER_MAX,
};

enum cmcp_test_item {
	CMCP_FULL = 0,
	CMCP_CM_PANEL,
	CMCP_CP_PANEL,
	CMCP_CM_BTN,
	CMCP_CP_BTN,
};

#define CM_ENABLED 0x10
#define CP_ENABLED 0x20
#define CM_PANEL (0x01 | CM_ENABLED)
#define CP_PANEL (0x02 | CP_ENABLED)
#define CM_BTN (0x04 | CM_ENABLED)
#define CP_BTN (0x08 | CP_ENABLED)
#define CMCP_FULL_CASE\
	(CM_PANEL | CP_PANEL | CM_BTN | CP_BTN | CM_ENABLED | CP_ENABLED)

#define CYTTSP5_DEVICE_ACCESS_NAME "cyttsp5_device_access"
#define CYTTSP5_INPUT_ELEM_SZ (sizeof("0xHH") + 1)

#define STATUS_SUCCESS	0
#define STATUS_FAIL	-1
#define PIP_CMD_MAX_LENGTH ((1 << 16) - 1)

#ifdef TTHE_TUNER_SUPPORT
struct heatmap_param {
	bool scan_start;
	enum scan_data_type_list data_type; /* raw, base, diff */
	int num_element;
};
#endif
#define ABS(x)			(((x) < 0) ? -(x) : (x))

#define CY_MAX_CONFIG_BYTES    256
#define CYTTSP5_TTHE_TUNER_GET_PANEL_DATA_FILE_NAME "get_panel_data"
#define TTHE_TUNER_MAX_BUF	(CY_MAX_PRBUF_SIZE * 3)

struct cyttsp5_device_access_data {
	struct device *dev;
	struct cyttsp5_sysinfo *si;
	struct mutex sysfs_lock;
	u8 status;
	u16 response_length;
	bool sysfs_nodes_created;
	struct kobject mfg_test;
	u8 panel_scan_data_id;
	u8 get_idac_data_id;
	u8 calibrate_sensing_mode;
	u8 calibrate_initialize_baselines;
	u8 baseline_sensing_mode;
#ifdef TTHE_TUNER_SUPPORT
	struct heatmap_param heatmap;
	struct dentry *tthe_get_panel_data_debugfs;
	struct mutex debugfs_lock;
	u8 tthe_get_panel_data_buf[TTHE_TUNER_MAX_BUF];
	u8 tthe_get_panel_data_is_open;
#endif
	struct dentry *cmcp_results_debugfs;

	struct dentry *base_dentry;
	struct dentry *mfg_test_dentry;
	u8 ic_buf[CY_MAX_PRBUF_SIZE];
	u8 response_buf[CY_MAX_PRBUF_SIZE];
	struct mutex cmcp_threshold_lock;
	u8 *cmcp_threshold_data;
	int cmcp_threshold_size;
	bool cmcp_threshold_loading;
	struct work_struct cmcp_threshold_update;
	struct completion builtin_cmcp_threshold_complete;
	int builtin_cmcp_threshold_status;
	bool is_manual_upgrade_enabled;
	struct configuration *configs;
	struct cmcp_data *cmcp_info;
	struct result *result;
	struct test_case_search *test_search_array;
	struct test_case_field *test_field_array;
	int cmcp_test_items;
	int test_executed;
	int cmcp_range_check;
	int cmcp_force_calibrate;
	int cmcp_test_in_progress;
};

struct cmcp_data {
	struct gd_sensor *gd_sensor_col;
	struct gd_sensor *gd_sensor_row;
	int32_t *cm_data_panel;
	int32_t *cp_tx_data_panel;
	int32_t *cp_rx_data_panel;
	int32_t *cp_tx_cal_data_panel;
	int32_t *cp_rx_cal_data_panel;
	int32_t cp_sensor_rx_delta;
	int32_t cp_sensor_tx_delta;
	int32_t cp_button_delta;
	int32_t *cm_btn_data;
	int32_t *cp_btn_data;
	int32_t *cm_sensor_column_delta;
	int32_t *cm_sensor_row_delta;
	int32_t cp_btn_cal;
	int32_t cm_btn_cal;
	int32_t cp_button_ave;
	int32_t cm_ave_data_panel;
	int32_t cp_tx_ave_data_panel;
	int32_t cp_rx_ave_data_panel;
	int32_t cm_cal_data_panel;
	int32_t cm_ave_data_btn;
	int32_t cm_cal_data_btn;
	int32_t cm_delta_data_btn;
	int32_t cm_sensor_delta;

	int32_t tx_num;
	int32_t rx_num;
	int32_t btn_num;
};

struct result {
	int32_t sensor_assignment;
	int32_t config_ver;
	int32_t revision_ctrl;
	int32_t device_id_high;
	int32_t device_id_low;
	bool cm_test_run;
	bool cp_test_run;
	/* Sensor Cm validation */
	bool cm_test_pass;
	bool cm_sensor_validation_pass;
	bool cm_sensor_row_delta_pass;
	bool cm_sensor_col_delta_pass;
	bool cm_sensor_gd_row_pass;
	bool cm_sensor_gd_col_pass;
	bool cm_sensor_calibration_pass;
	bool cm_sensor_delta_pass;
	bool cm_button_validation_pass;
	bool cm_button_delta_pass;

	int32_t *cm_sensor_raw_data;
	int32_t cm_sensor_calibration;
	int32_t cm_sensor_delta;
	int32_t *cm_button_raw_data;
	int32_t cm_button_delta;

	/* Sensor Cp validation */
	bool cp_test_pass;
	bool cp_sensor_delta_pass;
	bool cp_sensor_rx_delta_pass;
	bool cp_sensor_tx_delta_pass;
	bool cp_sensor_average_pass;
	bool cp_button_delta_pass;
	bool cp_button_average_pass;
	bool cp_rx_validation_pass;
	bool cp_tx_validation_pass;
	bool cp_button_validation_pass;

	int32_t *cp_sensor_rx_raw_data;
	int32_t *cp_sensor_tx_raw_data;
	int32_t cp_sensor_rx_delta;
	int32_t cp_sensor_tx_delta;
	int32_t cp_sensor_rx_calibration;
	int32_t cp_sensor_tx_calibration;
	int32_t *cp_button_raw_data;
	int32_t cp_button_delta;

	/*other validation*/
	bool short_test_pass;
	bool test_summary;
	uint8_t *cm_open_pwc;
};

static struct cyttsp5_core_commands *cmd;

static struct cyttsp5_module device_access_module;

static ssize_t cyttsp5_run_and_get_selftest_result_noprint(struct device *dev,
		char *buf, size_t buf_len, u8 test_id, u16 read_length,
		bool get_result_on_pass);

static int _cyttsp5_calibrate_idacs_cmd(struct device *dev,
		u8 sensing_mode, u8 *status);

static inline struct cyttsp5_device_access_data *cyttsp5_get_device_access_data(
		struct device *dev)
{
	return cyttsp5_get_module_data(dev, &device_access_module);
}

static ssize_t cyttsp5_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);
	u8 val;

	mutex_lock(&dad->sysfs_lock);
	val = dad->status;
	mutex_unlock(&dad->sysfs_lock);

	return scnprintf(buf, CY_MAX_PRBUF_SIZE, "%d\n", val);
}

static DEVICE_ATTR(status, S_IRUSR, cyttsp5_status_show, NULL);

static ssize_t cyttsp5_response_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);
	int i;
	ssize_t num_read;
	int index;

	mutex_lock(&dad->sysfs_lock);
	index = scnprintf(buf, CY_MAX_PRBUF_SIZE,
			"Status %d\n", dad->status);
	if (!dad->status)
		goto error;

	num_read = dad->response_length;

	for (i = 0; i < num_read; i++)
		index += scnprintf(buf + index, CY_MAX_PRBUF_SIZE - index,
				"0x%02X\n", dad->response_buf[i]);

	index += scnprintf(buf + index, CY_MAX_PRBUF_SIZE - index,
			"(%zd bytes)\n", num_read);

error:
	mutex_unlock(&dad->sysfs_lock);
	return index;
}

static DEVICE_ATTR(response, S_IRUSR, cyttsp5_response_show, NULL);

/*
 * Gets user input from sysfs and parse it
 * return size of parsed output buffer
 */
static int cyttsp5_ic_parse_input(struct device *dev, const char *buf,
		size_t buf_size, u8 *ic_buf, size_t ic_buf_size)
{
	const char *pbuf = buf;
	unsigned long value;
	char scan_buf[CYTTSP5_INPUT_ELEM_SZ];
	u32 i = 0;
	u32 j;
	int last = 0;
	int ret;

	parade_debug(dev, DEBUG_LEVEL_1,
		"%s: pbuf=%p buf=%p size=%zu %s=%zu buf=%s\n",
		__func__, pbuf, buf, buf_size, "scan buf size",
		CYTTSP5_INPUT_ELEM_SZ, buf);

	while (pbuf <= (buf + buf_size)) {
		if (i >= CY_MAX_CONFIG_BYTES) {
			dev_err(dev, "%s: %s size=%d max=%d\n", __func__,
					"Max cmd size exceeded", i,
					CY_MAX_CONFIG_BYTES);
			return -EINVAL;
		}
		if (i >= ic_buf_size) {
			dev_err(dev, "%s: %s size=%d buf_size=%zu\n", __func__,
					"Buffer size exceeded", i, ic_buf_size);
			return -EINVAL;
		}
		while (((*pbuf == ' ') || (*pbuf == ','))
				&& (pbuf < (buf + buf_size))) {
			last = *pbuf;
			pbuf++;
		}

		if (pbuf >= (buf + buf_size))
			break;

		memset(scan_buf, 0, CYTTSP5_INPUT_ELEM_SZ);
		if ((last == ',') && (*pbuf == ',')) {
			dev_err(dev, "%s: %s \",,\" not allowed.\n", __func__,
					"Invalid data format.");
			return -EINVAL;
		}
		for (j = 0; j < (CYTTSP5_INPUT_ELEM_SZ - 1)
				&& (pbuf < (buf + buf_size))
				&& (*pbuf != ' ')
				&& (*pbuf != ','); j++) {
			last = *pbuf;
			scan_buf[j] = *pbuf++;
		}

		ret = kstrtoul(scan_buf, 16, &value);
		if (ret < 0) {
			dev_err(dev, "%s: %s '%s' %s%s i=%d r=%d\n", __func__,
					"Invalid data format. ", scan_buf,
					"Use \"0xHH,...,0xHH\"", " instead.",
					i, ret);
			return ret;
		}

		ic_buf[i] = value;
		i++;
	}

	return i;
}

static ssize_t cyttsp5_command_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);
	ssize_t length;
	int rc;

	mutex_lock(&dad->sysfs_lock);
	dad->status = 0;
	dad->response_length = 0;
	length = cyttsp5_ic_parse_input(dev, buf, size, dad->ic_buf,
			CY_MAX_PRBUF_SIZE);
	if (length <= 0) {
		dev_err(dev, "%s: %s Group Data store\n", __func__,
				"Malformed input for");
		goto exit;
	}

	/* write ic_buf to log */
	cyttsp5_pr_buf(dev, dad->ic_buf, length, "ic_buf");

	pm_runtime_get_sync(dev);
	rc = cmd->nonhid_cmd->user_cmd(dev, 1, CY_MAX_PRBUF_SIZE,
			dad->response_buf, length, dad->ic_buf,
			&dad->response_length);
	pm_runtime_put(dev);
	if (rc) {
		dad->response_length = 0;
		dev_err(dev, "%s: Failed to store command\n", __func__);
	} else {
		dad->status = 1;
	}

exit:
	mutex_unlock(&dad->sysfs_lock);
	parade_debug(dev, DEBUG_LEVEL_2, "%s: return size=%zu\n",
		__func__, size);
	return size;
}

static DEVICE_ATTR(command, S_IWUSR, NULL, cyttsp5_command_store);

static int cmcp_check_config_fw_match(struct device *dev,
	struct configuration *configuration)
{
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);
	int32_t tx_num = dad->configs->tx_num;
	int32_t rx_num = dad->configs->rx_num;
	int32_t button_num = dad->configs->btn_num;
	int ret = 0;

	if (tx_num != dad->si->sensing_conf_data.tx_num) {
		dev_err(dev, "%s: TX number mismatch!\n", __func__);
		ret = -EINVAL;
	}

	if (rx_num != dad->si->sensing_conf_data.rx_num) {
		dev_err(dev, "%s: RX number mismatch!\n", __func__);
		ret = -EINVAL;
	}

	if (button_num != dad->si->num_btns) {
		dev_err(dev, "%s: Button number mismatch!\n", __func__);
		ret = -EINVAL;
	}

	return ret;
}

static int validate_cm_test_results(struct device *dev,
	struct configuration *configuration, struct cmcp_data *cmcp_info,
	struct result *result, bool *pass, int test_item)
{
	int32_t tx_num = cmcp_info->tx_num;
	int32_t rx_num = cmcp_info->rx_num;
	int32_t button_num =  cmcp_info->btn_num;
	uint32_t sensor_num = tx_num * rx_num;
	int32_t *cm_sensor_data = cmcp_info->cm_data_panel;
	int32_t cm_button_delta;
	int32_t cm_sensor_calibration;
	int32_t *cm_button_data = cmcp_info->cm_btn_data;
	struct gd_sensor *gd_sensor_col = cmcp_info->gd_sensor_col;
	struct gd_sensor *gd_sensor_row = cmcp_info->gd_sensor_row;
	int32_t *cm_sensor_column_delta = cmcp_info->cm_sensor_column_delta;
	int32_t *cm_sensor_row_delta = cmcp_info->cm_sensor_row_delta;
	int ret = 0;
	int i, j;

	parade_debug(dev, DEBUG_LEVEL_2, "%s: start\n", __func__);

	if ((test_item & CM_PANEL) == CM_PANEL) {
		parade_debug(dev, DEBUG_LEVEL_2, "Check each sensor Cm data for min max value\n ");

		/* Check each sensor Cm data for min/max values */
		result->cm_sensor_validation_pass = true;

	for (i = 0; i < sensor_num; i++) {
		int row = i % rx_num;
		int col = i / rx_num;
		int32_t cm_sensor_min =
		configuration->cm_min_max_table_sensor[(row*tx_num+col)*2];
		int32_t cm_sensor_max =
		configuration->cm_min_max_table_sensor[(row*tx_num+col)*2+1];
		if ((cm_sensor_data[i] < cm_sensor_min)
		|| (cm_sensor_data[i] > cm_sensor_max)) {
			dev_err(dev, "%s: Sensor[%d,%d]:%d (%d,%d)\n",
					"Cm sensor min/max test",
					row, col,
					cm_sensor_data[i],
					cm_sensor_min, cm_sensor_max);
			result->cm_sensor_validation_pass = false;
		}
	}

	/*check cm gradient column data*/
	result->cm_sensor_gd_col_pass = true;
	for (i = 0;
	i < configuration->cm_max_table_gradient_cols_percent_size;
	i++) {
		if ((gd_sensor_col + i)->gradient_val >
		10 * configuration->cm_max_table_gradient_cols_percent[i]){
			dev_err(dev,
		"%s: cm_max_table_gradient_cols_percent[%d]:%d, gradient_val:%d\n",
		__func__,
		i,
		configuration->cm_max_table_gradient_cols_percent[i],
		(gd_sensor_col + i)->gradient_val);
			result->cm_sensor_gd_col_pass = false;
		}
	}

	/*check cm gradient row data*/
	result->cm_sensor_gd_row_pass = true;
	for (j = 0;
	j < configuration->cm_max_table_gradient_rows_percent_size;
	j++) {
		if ((gd_sensor_row + j)->gradient_val >
		10 * configuration->cm_max_table_gradient_rows_percent[j]) {
			dev_err(dev,
		"%s: cm_max_table_gradient_rows_percent[%d]:%d, gradient_val:%d\n",
		__func__,
		j, configuration->cm_max_table_gradient_rows_percent[j],
		(gd_sensor_row + j)->gradient_val);
			result->cm_sensor_gd_row_pass = false;
		}
	}

	result->cm_sensor_row_delta_pass = true;
	result->cm_sensor_col_delta_pass = true;
	result->cm_sensor_calibration_pass = true;
	result->cm_sensor_delta_pass = true;


	/*
	 * Check each row Cm data
	 * with neighbor for difference
	 */
	for (i = 0; i < tx_num; i++) {
		for (j = 1; j < rx_num; j++) {
			int32_t cm_sensor_row_diff =
			ABS(cm_sensor_data[i * rx_num + j] -
			cm_sensor_data[i * rx_num + j - 1]);
		cm_sensor_row_delta[i * rx_num + j - 1] =
			cm_sensor_row_diff;
			if (cm_sensor_row_diff
			> configuration->cm_range_limit_row) {
				dev_err(dev,
				"%s: Sensor[%d,%d]:%d (%d)\n",
				"Cm sensor row range limit test",
				j, i,
				cm_sensor_row_diff,
			configuration->cm_range_limit_row);
		result->cm_sensor_row_delta_pass = false;
			}
		}
	}

	/*
	 * Check each column Cm data
	 * with neighbor for difference
	 */
	for (i = 1; i < tx_num; i++) {
		for (j = 0; j < rx_num; j++) {
			int32_t cm_sensor_col_diff =
		ABS((int)cm_sensor_data[i * rx_num + j] -
		(int)cm_sensor_data[(i - 1) * rx_num + j]);
		cm_sensor_column_delta[(i - 1) * rx_num + j] =
			cm_sensor_col_diff;
		if (cm_sensor_col_diff >
			configuration->cm_range_limit_col) {
			dev_err(dev,
			"%s: Sensor[%d,%d]:%d (%d)\n",
			"Cm sensor column range limit test",
			j, i,
			cm_sensor_col_diff,
			configuration->cm_range_limit_col);
		result->cm_sensor_col_delta_pass = false;
			}
		}
	}

	/* Check sensor calculated Cm for min/max values */
	cm_sensor_calibration = cmcp_info->cm_cal_data_panel;
	if (cm_sensor_calibration <
		configuration->cm_min_limit_cal
		|| cm_sensor_calibration >
		configuration->cm_max_limit_cal) {
		dev_err(dev, "%s: Cm_cal:%d (%d,%d)\n",
			"Cm sensor Cm_cal min/max test",
			cm_sensor_calibration,
			configuration->cm_min_limit_cal,
			configuration->cm_max_limit_cal);
		result->cm_sensor_calibration_pass = false;
	}

	/* Check sensor Cm delta for range limit */
	if (cmcp_info->cm_sensor_delta
		 > 10 * configuration->cm_max_delta_sensor_percent) {
		dev_err(dev,
			"%s: Cm_sensor_delta:%d (%d)\n",
			"Cm sensor delta range limit test",
			cmcp_info->cm_sensor_delta,
		configuration->cm_max_delta_sensor_percent);
		result->cm_sensor_delta_pass = false;
	}

	result->cm_test_pass = result->cm_sensor_gd_col_pass
			&& result->cm_sensor_gd_row_pass
			&& result->cm_sensor_validation_pass
			&& result->cm_sensor_row_delta_pass
			&& result->cm_sensor_col_delta_pass
			&& result->cm_sensor_calibration_pass
			&& result->cm_sensor_delta_pass;
	}

	if (((test_item & CM_BTN) == CM_BTN) && (cmcp_info->btn_num)) {
		/* Check each button Cm data for min/max values */
		result->cm_button_validation_pass = true;
		for (i = 0; i < button_num; i++) {
			int32_t  cm_button_min =
			configuration->cm_min_max_table_button[i * 2];
			int32_t  cm_button_max =
			configuration->cm_min_max_table_button[i * 2 + 1];
			if ((cm_button_data[i] <= cm_button_min)
				|| (cm_button_data[i] >= cm_button_max)) {
				dev_err(dev,
					"%s: Button[%d]:%d (%d,%d)\n",
					"Cm button min/max test",
					i,
					cm_button_data[i],
					cm_button_min, cm_button_max);
				result->cm_button_validation_pass = false;
			}
		}

		/* Check button Cm delta for range limit */
		result->cm_button_delta_pass = true;

		cm_button_delta = ABS((cmcp_info->cm_ave_data_btn -
			cmcp_info->cm_cal_data_btn) * 100 /
			cmcp_info->cm_ave_data_btn);
		if (cm_button_delta >
		configuration->cm_max_delta_button_percent) {
			dev_err(dev,
				"%s: Cm_button_delta:%d (%d)\n",
				"Cm button delta range limit test",
				cm_button_delta,
			configuration->cm_max_delta_button_percent);
			result->cm_button_delta_pass = false;
		}

		result->cm_test_pass = result->cm_test_pass
				&& result->cm_button_validation_pass
				&& result->cm_button_delta_pass;
	}

	if (pass)
		*pass = result->cm_test_pass;

	return ret;
}
static int validate_cp_test_results(struct device *dev,
	struct configuration *configuration, struct cmcp_data *cmcp_info,
	struct result *result, bool *pass, int test_item)
{
	int i = 0;
	uint32_t configuration_rx_num;
	uint32_t configuration_tx_num;
	int32_t *cp_sensor_tx_data = cmcp_info->cp_tx_data_panel;
	int32_t *cp_sensor_rx_data = cmcp_info->cp_rx_data_panel;
	int32_t cp_button_delta;
	int32_t cp_button_average;

	result->cp_test_pass = true;
	configuration_rx_num = configuration->cp_min_max_table_rx_size/2;
	configuration_tx_num = configuration->cp_min_max_table_tx_size/2;

	parade_debug(dev, DEBUG_LEVEL_2, "%s start\n", __func__);

	if ((test_item & CP_PANEL) == CP_PANEL) {
		int32_t cp_sensor_tx_delta;
		int32_t cp_sensor_rx_delta;

		/* Check Sensor Cp delta for range limit */
		result->cp_sensor_delta_pass = true;
		/*check cp_sensor_tx_delta */
		for (i = 0; i < configuration_tx_num; i++) {
			cp_sensor_tx_delta =
			ABS((cmcp_info->cp_tx_cal_data_panel[i]-
			cmcp_info->cp_tx_data_panel[i]) * 100 /
			cmcp_info->cp_tx_data_panel[i]);

			if (cp_sensor_tx_delta >
			configuration->cp_max_delta_sensor_tx_percent) {
				dev_err(dev,
				"%s: Cp_sensor_tx_delta:%d (%d)\n",
				"Cp sensor delta range limit test",
				cp_sensor_tx_delta,
			configuration->cp_max_delta_sensor_tx_percent);
				result->cp_sensor_delta_pass = false;
			}
		}

		/*check cp_sensor_rx_delta */
		for (i = 0; i < configuration_rx_num; i++) {
			cp_sensor_rx_delta =
			ABS((cmcp_info->cp_rx_cal_data_panel[i] -
			cmcp_info->cp_rx_data_panel[i]) * 100 /
			cmcp_info->cp_rx_data_panel[i]);
			if (cp_sensor_rx_delta >
			configuration->cp_max_delta_sensor_rx_percent) {
				dev_err(dev,
				"%s: Cp_sensor_rx_delta:%d(%d)\n",
				"Cp sensor delta range limit test",
				cp_sensor_rx_delta,
			configuration->cp_max_delta_sensor_rx_percent);
				result->cp_sensor_delta_pass = false;
			}
		}

		/* Check sensor Cp rx for min/max values */
		result->cp_rx_validation_pass = true;
		for (i = 0; i < configuration_rx_num; i++) {
			int32_t cp_rx_min =
				configuration->cp_min_max_table_rx[i * 2];
			int32_t cp_rx_max =
				configuration->cp_min_max_table_rx[i * 2 + 1];
			if ((cp_sensor_rx_data[i] <= cp_rx_min)
				|| (cp_sensor_rx_data[i] >= cp_rx_max)) {
				dev_err(dev,
				"%s: Cp Rx[%d]:%d (%d,%d)\n",
				"Cp Rx min/max test",
				i,
				(int)cp_sensor_rx_data[i],
				cp_rx_min, cp_rx_max);
				result->cp_rx_validation_pass = false;
			}
		}

		/* Check sensor Cp tx for min/max values */
		result->cp_tx_validation_pass = true;
		for (i = 0; i < configuration_tx_num; i++) {
			int32_t cp_tx_min =
				configuration->cp_min_max_table_tx[i * 2];
			int32_t cp_tx_max =
				configuration->cp_min_max_table_tx[i * 2 + 1];
			if ((cp_sensor_tx_data[i] <= cp_tx_min)
				|| (cp_sensor_tx_data[i] >= cp_tx_max)) {
				dev_err(dev,
				"%s: Cp Tx[%d]:%d(%d,%d)\n",
				"Cp Tx min/max test",
				i,
				cp_sensor_tx_data[i],
				cp_tx_min, cp_tx_max);
				result->cp_tx_validation_pass = false;
			}
		}

		result->cp_test_pass = result->cp_test_pass
				&& result->cp_sensor_delta_pass
				&& result->cp_rx_validation_pass
				&& result->cp_tx_validation_pass;
	}

	if (((test_item & CP_BTN) == CP_BTN) && (cmcp_info->btn_num)) {
		result->cp_button_delta_pass = true;

		/* Check button Cp delta for range limit */
		cp_button_delta = ABS((cmcp_info->cp_btn_cal
		- cmcp_info->cp_button_ave) * 100 /
		cmcp_info->cp_button_ave);
		if (cp_button_delta >
		configuration->cp_max_delta_button_percent) {
			dev_err(dev,
			"%s: Cp_button_delta:%d (%d)\n",
			"Cp button delta range limit test",
			cp_button_delta,
			configuration->cp_max_delta_button_percent);
			result->cp_button_delta_pass = false;
		}

		/* Check button Cp average for min/max values */
		result->cp_button_average_pass = true;
		cp_button_average = cmcp_info->cp_button_ave;
		if (cp_button_average < configuration->min_button
				|| cp_button_average >
					configuration->max_button) {
			dev_err(dev,
				"%s: Button Cp average fails min/max test\n",
				__func__);
			dev_err(dev,
				"%s: Cp_button_average:%d (%d,%d)\n",
				"Cp button average min/max test",
				cp_button_average,
				configuration->min_button,
				configuration->max_button);
			result->cp_button_average_pass = false;
		}

		/* Check each button Cp data for min/max values */
		result->cp_button_validation_pass = true;
		for (i = 0; i < cmcp_info->btn_num; i++) {
			int32_t  cp_button_min =
			configuration->cp_min_max_table_button[i * 2];
			int32_t  cp_button_max =
			configuration->cp_min_max_table_button[i * 2 + 1];
			if ((cmcp_info->cp_btn_data[i] <= cp_button_min)
			|| (cmcp_info->cp_btn_data[i] >= cp_button_max)) {
				dev_err(dev,
					"%s: Button[%d]:%d (%d,%d)\n",
					"Cp button min/max test",
					i,
					cmcp_info->cp_btn_data[i],
					cp_button_min, cp_button_max);
				result->cp_button_validation_pass = false;
			}
		}

		result->cp_test_pass = result->cp_test_pass
				&& result->cp_button_delta_pass
				&& result->cp_button_average_pass
				&& result->cp_button_validation_pass;
	}

	if (pass)
		*pass = result->cp_test_pass;

	return 0;
}

static void calculate_gradient_row(struct gd_sensor *gd_sensor_row_head,
		 uint16_t row_num, int exclude_row_edge, int exclude_col_edge)
{
	int i = 0;
	uint16_t cm_min_cur = 0;
	uint16_t cm_max_cur = 0;
	uint16_t cm_ave_cur = 0;
	uint16_t cm_ave_next = 0;
	uint16_t cm_ave_prev = 0;
	struct gd_sensor *p = gd_sensor_row_head;

	if (exclude_row_edge) {
		for (i = 0; i < row_num; i++) {
			if (!exclude_col_edge) {
				cm_ave_cur = (p + i)->cm_ave;
				cm_min_cur = (p + i)->cm_min;
				cm_max_cur = (p + i)->cm_max;
				if (i < (row_num-1))
					cm_ave_next = (p + i+1)->cm_ave;
				if (i > 0)
					cm_ave_prev = (p + i-1)->cm_ave;
			} else {
				cm_ave_cur = (p + i)->cm_ave_exclude_edge;
				cm_min_cur = (p + i)->cm_min_exclude_edge;
				cm_max_cur = (p + i)->cm_max_exclude_edge;
				if (i < (row_num-1))
					cm_ave_next =
					(p + i+1)->cm_ave_exclude_edge;
				if (i > 0)
					cm_ave_prev =
					(p + i-1)->cm_ave_exclude_edge;
			}

			if (cm_ave_cur == 0)
				cm_ave_cur = 1;

			/*multiple 1000 to increate accuracy*/
			if ((i == 0) || (i == (row_num-1))) {
				(p + i)->gradient_val =
				(cm_max_cur - cm_min_cur) * 1000 /
				cm_ave_cur;
			} else if (i == 1) {
				(p + i)->gradient_val = (cm_max_cur - cm_min_cur
				+ ABS(cm_ave_cur - cm_ave_next)) * 1000 /
				cm_ave_cur;
			} else {
				(p + i)->gradient_val = (cm_max_cur - cm_min_cur
				+ ABS(cm_ave_cur - cm_ave_prev)) * 1000 /
				cm_ave_cur;
			}
		}
	} else if (!exclude_row_edge) {
		for (i = 0; i < row_num; i++) {
			if (!exclude_col_edge) {
				cm_ave_cur = (p + i)->cm_ave;
				cm_min_cur = (p + i)->cm_min;
				cm_max_cur = (p + i)->cm_max;
				if (i < (row_num-1))
					cm_ave_next = (p + i+1)->cm_ave;
				if (i > 0)
					cm_ave_prev = (p + i-1)->cm_ave;
			} else {
				cm_ave_cur = (p + i)->cm_ave_exclude_edge;
				cm_min_cur = (p + i)->cm_min_exclude_edge;
				cm_max_cur = (p + i)->cm_max_exclude_edge;
				if (i < (row_num-1))
					cm_ave_next =
					(p + i+1)->cm_ave_exclude_edge;
				if (i > 0)
					cm_ave_prev =
					(p + i-1)->cm_ave_exclude_edge;
			}

			if (cm_ave_cur == 0)
				cm_ave_cur = 1;
			/*multiple 1000 to increate accuracy*/
			if (i <= 1)
				(p + i)->gradient_val = (cm_max_cur - cm_min_cur
				+ ABS(cm_ave_cur - cm_ave_next)) * 1000 /
				cm_ave_cur;
			else
				(p + i)->gradient_val = (cm_max_cur - cm_min_cur
				+ ABS(cm_ave_cur - cm_ave_prev)) * 1000 /
				cm_ave_cur;
		}
	}
}

static void calculate_gradient_col(struct gd_sensor *gd_sensor_row_head,
	uint16_t col_num, int exclude_row_edge, int exclude_col_edge)
{
	int i = 0;
	int32_t cm_min_cur = 0;
	int32_t cm_max_cur = 0;
	int32_t cm_ave_cur = 0;
	int32_t cm_ave_next = 0;
	int32_t cm_ave_prev = 0;
	struct gd_sensor *p = gd_sensor_row_head;

	if (!exclude_col_edge) {
		for (i = 0; i < col_num; i++) {
			if (!exclude_row_edge) {
				cm_ave_cur = (p + i)->cm_ave;
				cm_min_cur = (p + i)->cm_min;
				cm_max_cur = (p + i)->cm_max;
				if (i < (col_num-1))
					cm_ave_next = (p + i+1)->cm_ave;
				if (i > 0)
					cm_ave_prev = (p + i-1)->cm_ave;
			} else {
				cm_ave_cur = (p + i)->cm_ave_exclude_edge;
				cm_min_cur = (p + i)->cm_min_exclude_edge;
				cm_max_cur = (p + i)->cm_max_exclude_edge;
				if (i < (col_num-1))
					cm_ave_next =
					(p + i+1)->cm_ave_exclude_edge;
				if (i > 0)
					cm_ave_prev =
					(p + i-1)->cm_ave_exclude_edge;
			}
			if (cm_ave_cur == 0)
				cm_ave_cur = 1;
			/*multiple 1000 to increate accuracy*/
			if (i <= 1)
				(p + i)->gradient_val = (cm_max_cur - cm_min_cur
				+ ABS(cm_ave_cur - cm_ave_next)) * 1000 /
				cm_ave_cur;
			else
				(p + i)->gradient_val = (cm_max_cur - cm_min_cur
				+ ABS(cm_ave_cur - cm_ave_prev)) * 1000 /
				cm_ave_cur;
		}
	} else if (exclude_col_edge) {
		for (i = 0; i < col_num; i++) {
			if (!exclude_row_edge) {
				cm_ave_cur = (p + i)->cm_ave;
				cm_min_cur = (p + i)->cm_min;
				cm_max_cur = (p + i)->cm_max;
				if (i < (col_num-1))
					cm_ave_next = (p + i+1)->cm_ave;
				if (i > 0)
					cm_ave_prev = (p + i-1)->cm_ave;
			} else {
				cm_ave_cur = (p + i)->cm_ave_exclude_edge;
				cm_min_cur = (p + i)->cm_min_exclude_edge;
				cm_max_cur = (p + i)->cm_max_exclude_edge;
				if (i < (col_num-1))
					cm_ave_next =
					(p + i+1)->cm_ave_exclude_edge;
				if (i > 0)
					cm_ave_prev =
					(p + i-1)->cm_ave_exclude_edge;
			}

			if (cm_ave_cur == 0)
				cm_ave_cur = 1;
			/*multiple 1000 to increate accuracy*/
			if ((i == 0) || (i == (col_num - 1)))
				(p + i)->gradient_val =
					 (cm_max_cur - cm_min_cur) * 1000 /
					 cm_ave_cur;
			else if (i == 1)
				(p + i)->gradient_val =
					(cm_max_cur - cm_min_cur +
					ABS(cm_ave_cur - cm_ave_next))
					 * 1000 / cm_ave_cur;
			else
				(p + i)->gradient_val =
					(cm_max_cur - cm_min_cur +
					ABS(cm_ave_cur - cm_ave_prev))
					* 1000 / cm_ave_cur;
			}
	}
}

static void fill_gd_sensor_table(struct gd_sensor *head, int32_t index,
	int32_t cm_max, int32_t cm_min,	int32_t cm_ave,
	int32_t cm_max_exclude_edge, int32_t cm_min_exclude_edge,
	int32_t cm_ave_exclude_edge)
{
	(head + index)->cm_max = cm_max;
	(head + index)->cm_min = cm_min;
	(head + index)->cm_ave = cm_ave;
	(head + index)->cm_ave_exclude_edge = cm_ave_exclude_edge;
	(head + index)->cm_max_exclude_edge = cm_max_exclude_edge;
	(head + index)->cm_min_exclude_edge = cm_min_exclude_edge;
}

static void calculate_gd_info(struct gd_sensor *gd_sensor_col,
	struct gd_sensor *gd_sensor_row, int tx_num, int rx_num,
	int32_t *cm_sensor_data, int cm_excluding_row_edge,
	int cm_excluding_col_edge)
{
	int32_t cm_max;
	int32_t cm_min;
	int32_t cm_ave;
	int32_t cm_max_exclude_edge;
	int32_t cm_min_exclude_edge;
	int32_t cm_ave_exclude_edge;
	int32_t cm_data;
	int i;
	int j;

	if (!cm_sensor_data)
		return;

	/*calculate all the gradient related info for column*/
	for (i = 0; i < tx_num; i++) {
		/*re-initialize for a new col*/
		cm_max = cm_sensor_data[i * rx_num];
		cm_min = cm_max;
		cm_ave = 0;
		cm_max_exclude_edge = cm_sensor_data[i * rx_num + 1];
		cm_min_exclude_edge = cm_max_exclude_edge;
		cm_ave_exclude_edge = 0;

		for (j = 0; j < rx_num; j++) {
			cm_data = cm_sensor_data[i * rx_num + j];
			if (cm_data > cm_max)
				cm_max = cm_data;
			if (cm_data < cm_min)
				cm_min = cm_data;
			cm_ave += cm_data;
			/*calculate exclude edge data*/
			if ((j > 0) && (j < (rx_num-1))) {
				if (cm_data > cm_max_exclude_edge)
					cm_max_exclude_edge = cm_data;
				if (cm_data < cm_min_exclude_edge)
					cm_min_exclude_edge = cm_data;
				cm_ave_exclude_edge += cm_data;
			}
		}
		if (rx_num)
			cm_ave /= rx_num;
		if (rx_num - 2)
			cm_ave_exclude_edge /= (rx_num-2);
		fill_gd_sensor_table(gd_sensor_col, i, cm_max, cm_min, cm_ave,
		cm_max_exclude_edge, cm_min_exclude_edge, cm_ave_exclude_edge);
	}

	calculate_gradient_col(gd_sensor_col, tx_num, cm_excluding_row_edge,
		 cm_excluding_col_edge);

	/*calculate all the gradient related info for row*/
	for (j = 0; j < rx_num; j++) {
		/*re-initialize for a new row*/
		cm_max = cm_sensor_data[j];
		cm_min = cm_max;
		cm_ave = 0;
		cm_max_exclude_edge = cm_sensor_data[rx_num + j];
		cm_min_exclude_edge = cm_max_exclude_edge;
		cm_ave_exclude_edge = 0;
		for (i = 0; i < tx_num; i++) {
			cm_data = cm_sensor_data[i * rx_num + j];
			if (cm_data > cm_max)
				cm_max = cm_data;
			if (cm_data < cm_min)
				cm_min = cm_data;
			cm_ave += cm_data;
			/*calculate exclude edge data*/
			if ((i >  0) && (i < (tx_num-1))) {
				if (cm_data > cm_max_exclude_edge)
					cm_max_exclude_edge = cm_data;
				if (cm_data < cm_min_exclude_edge)
					cm_min_exclude_edge = cm_data;
				cm_ave_exclude_edge += cm_data;
			}
		}
		if (tx_num)
			cm_ave /= tx_num;
		if (tx_num - 2)
			cm_ave_exclude_edge /= (tx_num-2);
		fill_gd_sensor_table(gd_sensor_row, j, cm_max, cm_min, cm_ave,
		cm_max_exclude_edge, cm_min_exclude_edge, cm_ave_exclude_edge);
	}
	calculate_gradient_row(gd_sensor_row, rx_num, cm_excluding_row_edge,
		 cm_excluding_col_edge);
}

static int  cyttsp5_get_cmcp_info(struct cyttsp5_device_access_data *dad,
	struct cmcp_data *cmcp_info)
{
	struct device *dev;
	int32_t *cm_data_panel = cmcp_info->cm_data_panel;
	int32_t *cp_tx_data_panel = cmcp_info->cp_tx_data_panel;
	int32_t *cp_rx_data_panel = cmcp_info->cp_rx_data_panel;
	int32_t *cp_tx_cal_data_panel = cmcp_info->cp_tx_cal_data_panel;
	int32_t *cp_rx_cal_data_panel = cmcp_info->cp_rx_cal_data_panel;
	int32_t *cm_btn_data = cmcp_info->cm_btn_data;
	int32_t *cp_btn_data = cmcp_info->cp_btn_data;
	struct gd_sensor *gd_sensor_col = cmcp_info->gd_sensor_col;
	struct gd_sensor *gd_sensor_row = cmcp_info->gd_sensor_row;
	struct result *result = dad->result;
	int32_t cp_btn_cal = 0;
	int32_t cm_btn_cal = 0;
	int32_t cp_btn_ave = 0;
	int32_t cm_ave_data_panel = 0;
	int32_t cm_ave_data_btn = 0;
	int32_t cm_delta_data_btn = 0;
	int32_t cp_tx_ave_data_panel = 0;
	int32_t cp_rx_ave_data_panel = 0;
	u8 tmp_buf[3];
	int tx_num;
	int rx_num;
	int btn_num;
	int rc = 0;
	int i;

	dev = dad->dev;
	cmcp_info->tx_num = dad->si->sensing_conf_data.tx_num;
	cmcp_info->rx_num = dad->si->sensing_conf_data.rx_num;
	cmcp_info->btn_num = dad->si->num_btns;

	tx_num = cmcp_info->tx_num;
	rx_num = cmcp_info->rx_num;
	btn_num = cmcp_info->btn_num;
	parade_debug(dev, DEBUG_LEVEL_2, "%s tx_num=%d", __func__, tx_num);
	parade_debug(dev, DEBUG_LEVEL_2, "%s rx_num=%d", __func__, rx_num);
	parade_debug(dev, DEBUG_LEVEL_2, "%s btn_num=%d", __func__, btn_num);

	/*short test*/
	result->short_test_pass = true;
	rc = cyttsp5_run_and_get_selftest_result_noprint(
		dev, tmp_buf, sizeof(tmp_buf),
		CY_ST_ID_AUTOSHORTS, PIP_CMD_MAX_LENGTH, false);
	if (rc) {
		dev_err(dev, "short test not supported");
		goto exit;
	}
	if (dad->ic_buf[1] != 0)
		result->short_test_pass = false;

	/*Get cm_panel data*/
	rc = cyttsp5_run_and_get_selftest_result_noprint(
		dev, tmp_buf, sizeof(tmp_buf),
		CY_ST_ID_CM_PANEL, PIP_CMD_MAX_LENGTH, true);
	if (rc) {
		dev_err(dev, "Get CM Panel not supported");
		goto exit;
	}
	if (cm_data_panel != NULL) {
		for (i = 0; i < tx_num * rx_num;  i++) {
			cm_data_panel[i] =
			10*(dad->ic_buf[CM_PANEL_DATA_OFFSET+i*2] + 256
			* dad->ic_buf[CM_PANEL_DATA_OFFSET+i*2+1]);
			parade_debug(dev, DEBUG_LEVEL_2,
				"cm_data_panel[%d]=%d\n",
				i, cm_data_panel[i]);
			cm_ave_data_panel += cm_data_panel[i];
		}
		cm_ave_data_panel /= (tx_num * rx_num);
		cmcp_info->cm_ave_data_panel = cm_ave_data_panel;
		cmcp_info->cm_cal_data_panel =
		10*(dad->ic_buf[CM_PANEL_DATA_OFFSET+i*2]
		+256 * dad->ic_buf[CM_PANEL_DATA_OFFSET+i*2+1]);
		/*multiple 1000 to increate accuracy*/
		cmcp_info->cm_sensor_delta = ABS((cmcp_info->cm_ave_data_panel -
			cmcp_info->cm_cal_data_panel) * 1000 /
			cmcp_info->cm_ave_data_panel);
	}

	/*calculate gradient panel sensor column/row here*/
	calculate_gd_info(gd_sensor_col, gd_sensor_row, tx_num, rx_num,
		 cm_data_panel, 1, 1);
	for (i = 0; i < tx_num; i++) {
		parade_debug(dev, DEBUG_LEVEL_2,
			"i=%d max=%d,min=%d,ave=%d, gradient=%d",
			i, gd_sensor_col[i].cm_max, gd_sensor_col[i].cm_min,
			gd_sensor_col[i].cm_ave, gd_sensor_col[i].gradient_val);
	}

	for (i = 0; i < rx_num; i++) {
		parade_debug(dev, DEBUG_LEVEL_2,
			"i=%d max=%d,min=%d,ave=%d, gradient=%d",
			i, gd_sensor_row[i].cm_max, gd_sensor_row[i].cm_min,
			gd_sensor_row[i].cm_ave, gd_sensor_row[i].gradient_val);
	}

	/*Get cp data*/
	rc = cyttsp5_run_and_get_selftest_result_noprint(
		dev, tmp_buf, sizeof(tmp_buf),
		CY_ST_ID_CP_PANEL, PIP_CMD_MAX_LENGTH, true);
	if (rc) {
		dev_err(dev, "Get CP Panel not supported");
		goto exit;
	}
	/*Get cp_tx_data_panel*/
	if (cp_tx_data_panel != NULL) {
		for (i = 0; i < tx_num; i++) {
			cp_tx_data_panel[i] =
			10*(dad->ic_buf[CP_PANEL_DATA_OFFSET+i*2]
			+ 256 * dad->ic_buf[CP_PANEL_DATA_OFFSET+i*2+1]);
			parade_debug(dev, DEBUG_LEVEL_2,
				"cp_tx_data_panel[%d]=%d\n",
				i, cp_tx_data_panel[i]);
			cp_tx_ave_data_panel += cp_tx_data_panel[i];
		}
		if (tx_num)
			cp_tx_ave_data_panel /= tx_num;
		cmcp_info->cp_tx_ave_data_panel = cp_tx_ave_data_panel;
	}

	/*Get cp_tx_cal_data_panel*/
	if (cp_tx_cal_data_panel != NULL) {
		for (i = 0; i < tx_num; i++) {
			cp_tx_cal_data_panel[i] =
			10*(dad->ic_buf[CP_PANEL_DATA_OFFSET+tx_num*2+i*2]
		+ 256 * dad->ic_buf[CP_PANEL_DATA_OFFSET+tx_num*2+i*2+1]);
			parade_debug(dev, DEBUG_LEVEL_2, " cp_tx_cal_data_panel[%d]=%d\n",
				i, cp_tx_cal_data_panel[i]);
		}
	}

	/*get cp_sensor_tx_delta,using the first sensor cal value for temp */
	/*multiple 1000 to increase accuracy*/
	if (cp_tx_cal_data_panel != NULL) {
		cmcp_info->cp_sensor_tx_delta = ABS((cp_tx_cal_data_panel[0]
			- cp_tx_ave_data_panel) * 1000 / cp_tx_ave_data_panel);
	}

	/*Get cp_rx_data_panel*/
	if (cp_rx_data_panel != NULL) {
		for (i = 0; i < rx_num;  i++) {
			cp_rx_data_panel[i] =
			10*(dad->ic_buf[CP_PANEL_DATA_OFFSET+tx_num*4+i*2] +
			256 * dad->ic_buf[CP_PANEL_DATA_OFFSET+tx_num*4+i*2+1]);
			parade_debug(dev, DEBUG_LEVEL_2,
				"cp_rx_data_panel[%d]=%d\n", i,
				cp_rx_data_panel[i]);
			cp_rx_ave_data_panel += cp_rx_data_panel[i];
		}
		if (rx_num)
			cp_rx_ave_data_panel /= rx_num;
		cmcp_info->cp_rx_ave_data_panel = cp_rx_ave_data_panel;
	}

	/*Get cp_rx_cal_data_panel*/
	if (cp_rx_cal_data_panel != NULL) {
		for (i = 0; i < rx_num; i++) {
			cp_rx_cal_data_panel[i] =
		10 * (dad->ic_buf[CP_PANEL_DATA_OFFSET+tx_num*4+rx_num*2+i*2] +
		256 *
		dad->ic_buf[CP_PANEL_DATA_OFFSET+tx_num*4+rx_num*2+i*2+1]);
			parade_debug(dev, DEBUG_LEVEL_2,
				"cp_rx_cal_data_panel[%d]=%d\n", i,
				cp_rx_cal_data_panel[i]);
		}
	}

	/*get cp_sensor_rx_delta,using the first sensor cal value for temp */
	/*multiple 1000 to increase accuracy*/
	if (cp_rx_cal_data_panel != NULL) {
		if (cp_rx_ave_data_panel) {
			cmcp_info->cp_sensor_rx_delta = ABS((cp_rx_cal_data_panel[0]
				- cp_rx_ave_data_panel) * 1000 / cp_rx_ave_data_panel);
		}
	}
	if (btn_num == 0)
		goto skip_button_test;

	/*get cm btn data*/
	rc = cyttsp5_run_and_get_selftest_result_noprint(
		dev, tmp_buf, sizeof(tmp_buf),
		CY_ST_ID_CM_BUTTON, PIP_CMD_MAX_LENGTH, true);
	if (rc) {
		dev_err(dev, "Get CM BTN not supported");
		goto exit;
	}
	if (cm_btn_data != NULL) {
		for (i = 0; i < btn_num; i++) {
			cm_btn_data[i] =
			10 * (dad->ic_buf[CM_BTN_DATA_OFFSET+i*2] +
			256 * dad->ic_buf[CM_BTN_DATA_OFFSET+i*2+1]);
			parade_debug(dev, DEBUG_LEVEL_2,
				" cm_btn_data[%d]=%d\n",
				i, cm_btn_data[i]);
			cm_ave_data_btn += cm_btn_data[i];
		}
		cm_ave_data_btn /= btn_num;
		cm_btn_cal = 10*(dad->ic_buf[CM_BTN_DATA_OFFSET+i*2]
			 + 256 * dad->ic_buf[CM_BTN_DATA_OFFSET+i*2+1]);
		/*multiple 1000 to increase accuracy*/
		cm_delta_data_btn = ABS((cm_ave_data_btn-cm_btn_cal)
			 * 1000 / cm_ave_data_btn);
		parade_debug(dev, DEBUG_LEVEL_2, " cm_btn_cal=%d\n",
			cm_btn_cal);

		cmcp_info->cm_ave_data_btn = cm_ave_data_btn;
		cmcp_info->cm_cal_data_btn = cm_btn_cal;
		cmcp_info->cm_delta_data_btn = cm_delta_data_btn;
	}

	/*get cp btn data*/
	rc = cyttsp5_run_and_get_selftest_result_noprint(
		dev, tmp_buf, sizeof(tmp_buf),
		CY_ST_ID_CP_BUTTON, PIP_CMD_MAX_LENGTH, true);
	if (rc) {
		dev_err(dev, "Get CP BTN not supported");
		goto exit;
	}
	if (cp_btn_data != NULL) {
		for (i = 0; i < btn_num; i++) {
			cp_btn_data[i] =
			10 * (dad->ic_buf[CP_BTN_DATA_OFFSET+i*2] +
			256 * dad->ic_buf[CP_BTN_DATA_OFFSET+i*2+1]);
			cp_btn_ave += cp_btn_data[i];
			parade_debug(dev, DEBUG_LEVEL_2,
				"cp_btn_data[%d]=%d\n",
				i, cp_btn_data[i]);
		}
		cp_btn_ave /= btn_num;
		cp_btn_cal = 10*(dad->ic_buf[CP_BTN_DATA_OFFSET+i*2]
			 + 256 * dad->ic_buf[CP_BTN_DATA_OFFSET+i*2+1]);
		cmcp_info->cp_button_ave = cp_btn_ave;
		cmcp_info->cp_btn_cal = cp_btn_cal;
		/*multiple 1000 to increase accuracy*/
		cmcp_info->cp_button_delta = ABS((cp_btn_cal
		- cp_btn_ave) * 1000 / cp_btn_ave);
		parade_debug(dev, DEBUG_LEVEL_2, " cp_btn_cal=%d\n",
			cp_btn_cal);
		parade_debug(dev, DEBUG_LEVEL_2, " cp_btn_ave=%d\n",
			cp_btn_ave);
	}
skip_button_test:
exit:
	return rc;
}

static void cyttsp5_free_cmcp_buf(struct cmcp_data *cmcp_info)
{
	if (cmcp_info->gd_sensor_col != NULL)
		kfree(cmcp_info->gd_sensor_col);
	if (cmcp_info->gd_sensor_row != NULL)
		kfree(cmcp_info->gd_sensor_row);
	if (cmcp_info->cm_data_panel != NULL)
		kfree(cmcp_info->cm_data_panel);
	if (cmcp_info->cp_tx_data_panel != NULL)
		kfree(cmcp_info->cp_tx_data_panel);
	if (cmcp_info->cp_rx_data_panel != NULL)
		kfree(cmcp_info->cp_rx_data_panel);
	if (cmcp_info->cp_tx_cal_data_panel != NULL)
		kfree(cmcp_info->cp_tx_cal_data_panel);
	if (cmcp_info->cp_rx_cal_data_panel != NULL)
		kfree(cmcp_info->cp_rx_cal_data_panel);
	if (cmcp_info->cm_btn_data != NULL)
		kfree(cmcp_info->cm_btn_data);
	if (cmcp_info->cp_btn_data != NULL)
		kfree(cmcp_info->cp_btn_data);
	if (cmcp_info->cm_sensor_column_delta != NULL)
		kfree(cmcp_info->cm_sensor_column_delta);
	if (cmcp_info->cm_sensor_row_delta != NULL)
		kfree(cmcp_info->cm_sensor_row_delta);
}

static int cyttsp5_cmcp_get_test_item(int item_input)
{
	int test_item = 0;

	switch (item_input) {
	case CMCP_FULL:
		test_item = CMCP_FULL_CASE;
		break;
	case CMCP_CM_PANEL:
		test_item = CM_PANEL;
		break;
	case CMCP_CP_PANEL:
		test_item = CP_PANEL;
		break;
	case CMCP_CM_BTN:
		test_item = CM_BTN;
		break;
	case CMCP_CP_BTN:
		test_item = CP_BTN;
		break;
	}
	return test_item;
}

static ssize_t cyttsp5_cmcp_test_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);
	struct cmcp_data *cmcp_info = dad->cmcp_info;
	struct result *result = dad->result;
	struct configuration *configuration = dad->configs;
	bool final_pass = true;
	static const char * const cmcp_test_case_array[] = {"Full Cm/Cp test",
		"Cm panel test", "Cp panel test",
		"Cm button test", "Cp button test"};
	int index = 0;
	int test_item = 0;
	int no_builtin_file = 0;
	int rc;
	u8 status;
	int self_test_id_supported = 0;

	dev = dad->dev;
	if ((configuration == NULL) || (cmcp_info == NULL))
		goto exit;

	mutex_lock(&dad->sysfs_lock);

	if (dad->cmcp_test_in_progress) {
		mutex_unlock(&dad->sysfs_lock);
		goto cmcp_not_ready;
	}
	dad->cmcp_test_in_progress = 1;

	dad->test_executed = 0;
	test_item = cyttsp5_cmcp_get_test_item(dad->cmcp_test_items);

	if (dad->builtin_cmcp_threshold_status < 0) {
		dev_err(dev, "%s: No cmcp threshold file.\n", __func__);
		no_builtin_file = 1;
		mutex_unlock(&dad->sysfs_lock);
		goto start_testing;
	}

	if (dad->cmcp_test_items < 0) {
		parade_debug(dev, DEBUG_LEVEL_2,
			"%s: Invalid test item! Should be 0~4!\n", __func__);
		mutex_unlock(&dad->sysfs_lock);
		goto invalid_item;
	}

	parade_debug(dev, DEBUG_LEVEL_2, "%s: Test item is %s, %d\n",
		__func__, cmcp_test_case_array[dad->cmcp_test_items],
		test_item);

	if ((dad->si->num_btns == 0)
		&& ((dad->cmcp_test_items == CMCP_CM_BTN)
			|| (dad->cmcp_test_items == CMCP_CP_BTN))) {
		parade_debug(dev, DEBUG_LEVEL_2,
			"%s: FW doesn't support button!\n", __func__);
		mutex_unlock(&dad->sysfs_lock);
		goto invalid_item_btn;
	}

	mutex_unlock(&dad->sysfs_lock);

	if (cmcp_check_config_fw_match(dev, configuration))
		goto mismatch;

start_testing:
	parade_debug(dev, DEBUG_LEVEL_2, "%s: Start Cm/Cp test!\n", __func__);
	result->cm_test_pass = true;
	result->cp_test_pass = true;

#ifdef CYTTSP_WATCHDOG_DELAY_ENBALE
	/*stop watchdog*/
	rc = cmd->request_stop_wd(dev);
	if (rc)
		dev_err(dev, "stop watchdog failed");
#endif
	/*force single tx*/
	rc = cmd->nonhid_cmd->set_param(dev, 0, 0x1F, 1, 1);
	if (rc)
		dev_err(dev, "force single tx failed");
	/*suspend_scanning */
	rc = cmd->nonhid_cmd->suspend_scanning(dev, 0);
	if (rc)
		dev_err(dev, "suspend_scanning failed");
	/*do calibration*/
	if (!dad->cmcp_force_calibrate) {
		parade_debug(dev, DEBUG_LEVEL_2, "do calibration in single tx mode");
		rc = _cyttsp5_calibrate_idacs_cmd(dev, 0, &status);
		if (rc < 0) {
			dev_err(dev, "%s: Error on calibrate idacs for mutual r=%d\n",
					__func__, rc);
		}
		rc = _cyttsp5_calibrate_idacs_cmd(dev, 1, &status);
		if (rc < 0) {
			dev_err(dev, "%s: Error on calibrate idacs for buttons r=%d\n",
					__func__, rc);
		}
		rc = _cyttsp5_calibrate_idacs_cmd(dev, 2, &status);
		if (rc < 0) {
			dev_err(dev, "%s: Error on calibrate idacs  for self r=%d\n",
					__func__, rc);
		}
	}
	/*resume_scanning */
	rc = cmd->nonhid_cmd->resume_scanning(dev, 0);
	if (rc)
		dev_err(dev, "resume_scanning failed");

	/*get all cmcp data from FW*/
	self_test_id_supported =
		cyttsp5_get_cmcp_info(dad, cmcp_info);
	if (self_test_id_supported)
		dev_err(dev, "cyttsp5_get_cmcp_info failed");

	/*restore to multi tx*/
	rc = cmd->nonhid_cmd->set_param(dev, 0, 0x1F, 0, 1);
	if (rc)
		dev_err(dev, "restore multi tx failed");

	/*suspend_scanning */
	rc = cmd->nonhid_cmd->suspend_scanning(dev, 0);
	if (rc)
		dev_err(dev, "suspend_scanning failed");
	/*do calibration*/
	if (!dad->cmcp_force_calibrate) {
		parade_debug(dev, DEBUG_LEVEL_2, "do calibration in multi tx mode");
		rc = _cyttsp5_calibrate_idacs_cmd(dev, 0, &status);
		if (rc < 0) {
			dev_err(dev, "%s: Error on calibrate idacs for mutual r=%d\n",
					__func__, rc);
		}
		rc = _cyttsp5_calibrate_idacs_cmd(dev, 1, &status);
		if (rc < 0) {
			dev_err(dev, "%s: Error on calibrate idacs for buttons r=%d\n",
					__func__, rc);
		}
		rc = _cyttsp5_calibrate_idacs_cmd(dev, 2, &status);
		if (rc < 0) {
			dev_err(dev, "%s: Error on calibrate idacs  for self r=%d\n",
					__func__, rc);
		}

	}
	/*resume_scanning */
	rc = cmd->nonhid_cmd->resume_scanning(dev, 0);
	if (rc)
		dev_err(dev, "resume_scanning failed");

#ifdef CYTTSP_WATCHDOG_DELAY_ENBALE
	/*start  watchdog*/
	rc = cmd->request_start_wd(dev);
	if (rc)
		dev_err(dev, "start watchdog failed");
#endif
	if (self_test_id_supported)
		goto self_test_id_failed;

	if (no_builtin_file)
		goto no_builtin;

	if (test_item & CM_ENABLED)
		validate_cm_test_results(dev, configuration, cmcp_info,
		result, &final_pass, test_item);

	if (test_item & CP_ENABLED)
		validate_cp_test_results(dev, configuration, cmcp_info,
		result, &final_pass, test_item);
no_builtin:
	if ((dad->cmcp_test_items == CMCP_FULL)
	&& (dad->cmcp_range_check == 0)) {
		/*full test and full check*/
		result->test_summary =
			result->cm_test_pass
			&& result->cp_test_pass
			&& result->short_test_pass;
	} else if ((dad->cmcp_test_items == CMCP_FULL)
		&& (dad->cmcp_range_check == 1)) {
		/*full test and basic check*/
		result->test_summary =
			result->cm_sensor_gd_col_pass
			&& result->cm_sensor_gd_row_pass
			&& result->cm_sensor_validation_pass
			&& result->cp_rx_validation_pass
			&& result->cp_tx_validation_pass
			&& result->short_test_pass;
	} else if (dad->cmcp_test_items == CMCP_CM_PANEL) {
		/*cm panel test result only*/
		result->test_summary =
			result->cm_sensor_gd_col_pass
			&& result->cm_sensor_gd_row_pass
			&& result->cm_sensor_validation_pass
			&& result->cm_sensor_row_delta_pass
			&& result->cm_sensor_col_delta_pass
			&& result->cm_sensor_calibration_pass
			&& result->cm_sensor_delta_pass;
	} else if (dad->cmcp_test_items == CMCP_CP_PANEL) {
		/*cp panel test result only*/
		result->test_summary =
			result->cp_sensor_delta_pass
			&& result->cp_rx_validation_pass
			&& result->cp_tx_validation_pass;
	} else if (dad->cmcp_test_items == CMCP_CM_BTN) {
		/*cm button test result only*/
		result->test_summary =
			result->cm_button_validation_pass
			&& result->cm_button_delta_pass;
	} else if (dad->cmcp_test_items == CMCP_CP_BTN) {
		/*cp button test result only*/
		result->test_summary =
			result->cp_button_delta_pass
			&& result->cp_button_average_pass
			&& result->cp_button_validation_pass;
	}
	mutex_lock(&dad->sysfs_lock);
	dad->test_executed = 1;
	mutex_unlock(&dad->sysfs_lock);

	if (result->test_summary) {
		dev_vdbg(dev, "%s: Finish Cm/Cp test! All Test Passed\n",
				__func__);
		index = snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Status 1\n");
	} else {
		dev_vdbg(dev, "%s: Finish Cm/Cp test! Range Check Failure\n",
				__func__);
		index = snprintf(buf, CY_MAX_PRBUF_SIZE,
				"Status 6\n");
	}

	goto cmcp_ready;
mismatch:
	index = snprintf(buf, CY_MAX_PRBUF_SIZE,
		 "Status 2\nInput cmcp threshold file mismatches with FW\n");
	goto cmcp_ready;
invalid_item_btn:
	index = snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Status 3\nFW doesn't support button!\n");
	goto cmcp_ready;
invalid_item:
	index = snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Status 4\nWrong test item or range check input!\nOnly support below items:\n0 - Cm/Cp Panel & Button with Gradient (Typical)\n1 - Cm Panel with Gradient\n2 - Cp Panel\n3 - Cm Button\n4 - Cp Button\nOnly support below range check:\n0 - Full Range Checking (default)\n1 - Basic Range Checking(TSG5 style)\n");
	goto cmcp_ready;
self_test_id_failed:
	index = snprintf(buf, CY_MAX_PRBUF_SIZE,
		"Status 5\nget self test ID not supported!");
	goto cmcp_ready;
cmcp_not_ready:
	index = snprintf(buf, CY_MAX_PRBUF_SIZE,
			"Status 0\n");
	goto cmcp_ready;
cmcp_ready:
	mutex_lock(&dad->sysfs_lock);
	dad->cmcp_test_in_progress = 0;
	mutex_unlock(&dad->sysfs_lock);
exit:
	return index;
}

static ssize_t cyttsp5_cmcp_test_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);
	u8 test_item = 0;
	u8 range_check = 0;
	u8 force_calibrate = 0;
	int ret = 0;
	static const char * const cmcp_test_case_array[] = {"Full Cm/Cp test",
		"Cm panel test", "Cp panel test",
		"Cm button test", "Cp button test"};
	static const char * const cmcp_test_range_check_array[] = {
		"Full (default)", "Basic"};
	static const char * const cmcp_test_force_cal_array[] =	{
		"Calibrate When Testing (default)", "No Calibration"};
	ssize_t length = 0;

	pm_runtime_get_sync(dev);
	mutex_lock(&dad->sysfs_lock);

	length = cyttsp5_ic_parse_input(dev, buf, size, dad->ic_buf,
			CY_MAX_PRBUF_SIZE);
	if (length <= 0 || length > 3) {
		dev_err(dev, "%s: Input format error!\n", __func__);
		dad->cmcp_test_items = -EINVAL;
		ret = -EINVAL;
		goto error;
	}

	/* Get test item  */
	test_item = dad->ic_buf[0];
	/* Get range check */
	if (length >= 2)
		range_check = dad->ic_buf[1];
	/* Get force calibration */
	if (length == 3)
		force_calibrate = dad->ic_buf[2];

	/*
	 * Test item limitation:
	 *	 0: Perform all Tests
	 *	 1: CM Panel with Gradient
	 *	 2: CP Panel
	 *	 3: CM Button
	 *	 4: CP Button
	 * Ranage check limitation:
	 *	 0: full check
	 *	 1: basic check
	 * Force calibrate limitation:
	 *	 0: do calibration
	 *	 1: don't do calibration
	 */
	if ((test_item < 0) || (test_item > 4) || (range_check > 1)
		|| (force_calibrate > 1)) {
		dev_err(dev,
		"%s: Test item should be 0~4; Range check should be 0~1; Force calibrate should be 0~1\n",
		__func__);
		dad->cmcp_test_items = -EINVAL;
		ret = -EINVAL;
		goto error;
	}
	/*
	 * If it is not all Test, then range_check should be 0
	 * because other test does not has concept of basic check
	 */
	if (test_item > 0 && test_item < 5)
		range_check = 0;
	dad->cmcp_test_items = test_item;
	dad->cmcp_range_check = range_check;
	dad->cmcp_force_calibrate = force_calibrate;
	parade_debug(dev, DEBUG_LEVEL_2,
		"%s: Test item is %s; Range check is %s; Force calibrate is %s.\n",
		__func__,
		cmcp_test_case_array[test_item],
		cmcp_test_range_check_array[range_check],
		cmcp_test_force_cal_array[force_calibrate]);

error:
	mutex_unlock(&dad->sysfs_lock);
	pm_runtime_put(dev);

	if (ret)
		return ret;

	return size;
}

static DEVICE_ATTR(cmcp_test, S_IRUSR | S_IWUSR,
	cyttsp5_cmcp_test_show, cyttsp5_cmcp_test_store);

static int prepare_print_string(char *out_buf, char *in_buf, int index)
{
	if ((out_buf == NULL) || (in_buf == NULL))
		return index;
	index += scnprintf(&out_buf[index], MAX_BUF_LEN - index,
			"%s", in_buf);
	return index;
}

static int prepare_print_data(char *out_buf, int32_t *in_buf, int index, int data_num)
{
	int i;

	if ((out_buf == NULL) || (in_buf == NULL))
		return index;
	for (i = 0; i < data_num; i++)
		index += scnprintf(&out_buf[index], MAX_BUF_LEN - index,
				"%d,", in_buf[i]);
	return index;
}

static int save_header(char *out_buf, int index, struct result *result)
{
	struct timex txc;
	struct rtc_time tm;
	char time_buf[100] = {0};

	do_gettimeofday(&(txc.time));
	rtc_time_to_tm(txc.time.tv_sec, &tm);
	scnprintf(time_buf, 100, "%d/%d/%d,TIME,%d:%d:%d,", tm.tm_year+1900,
		 tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

	index = prepare_print_string(out_buf, ",.header,\n", index);
	index = prepare_print_string(out_buf, ",DATE,", index);
	index = prepare_print_string(out_buf, &time_buf[0], index);
	index = prepare_print_string(out_buf, ",\n", index);
	index = prepare_print_string(out_buf, ",SW_VERSION,", index);
	index = prepare_print_string(out_buf, CY_DRIVER_VERSION, index);
	index = prepare_print_string(out_buf, ",\n", index);
	index = prepare_print_string(out_buf, ",.end,\n", index);
	index = prepare_print_string(out_buf, ",.engineering data,\n", index);

	return index;
}

static int print_silicon_id(char *out_buf, char *in_buf, int index)
{
	index = prepare_print_string(out_buf, ",1,", index);
	index = prepare_print_string(out_buf, &in_buf[0], index);
	return index;
}

static int save_engineering_data(struct device *dev, char *out_buf, int index,
	struct cmcp_data *cmcp_info, struct configuration *configuration,
	struct result *result, int test_item, int no_builtin_file)
{
	int i;
	int j;
	int tx_num = cmcp_info->tx_num;
	int rx_num = cmcp_info->rx_num;
	int btn_num = cmcp_info->btn_num;
	int tmp = 0;
	uint32_t fw_revision_control;
	uint32_t fw_config_ver;
	char device_id[20] = {0};
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);

	fw_revision_control = dad->si->cydata.revctrl;
	fw_config_ver = dad->si->cydata.fw_ver_conf;
	/*calculate silicon id*/
	result->device_id_low = 0;
	result->device_id_high = 0;

	for (i = 0; i < 4; i++)
		result->device_id_low =
		(result->device_id_low << 8) + dad->si->cydata.mfg_id[i];

	for (i = 4; i < 8; i++)
		result->device_id_high =
		(result->device_id_high << 8) + dad->si->cydata.mfg_id[i];

	scnprintf(device_id, 20, "%x%x",
		result->device_id_high, result->device_id_low);

	/*print test summary*/
	index = print_silicon_id(out_buf, &device_id[0], index);
	if (result->test_summary)
		index = prepare_print_string(out_buf, ",PASS,\n", index);
	else
		index = prepare_print_string(out_buf, ",FAIL,\n", index);

	/*revision ctrl number*/
	index = print_silicon_id(out_buf, &device_id[0], index);
	index = prepare_print_string(out_buf, ",FW revision Control,", index);
	index = prepare_print_data(out_buf, &fw_revision_control, index, 1);
	index = prepare_print_string(out_buf, "\n", index);

	/*config version*/
	index = print_silicon_id(out_buf, &device_id[0], index);
	index = prepare_print_string(out_buf, ",CONFIG_VER,", index);
	index = prepare_print_data(out_buf, &fw_config_ver, index, 1);
	index = prepare_print_string(out_buf, "\n", index);

	/*short test*/
	index = print_silicon_id(out_buf, &device_id[0], index);
	if (result->short_test_pass)
		index = prepare_print_string(out_buf, ",Shorts,PASS,\n", index);
	else
		index = prepare_print_string(out_buf, ",Shorts,FAIL,\n", index);

	if ((test_item & CM_ENABLED) == CM_ENABLED) {
		/*print BUTNS_CM_DATA_ROW00*/
		if (((test_item & CM_BTN) == CM_BTN) && (btn_num > 0)) {
			index = print_silicon_id(out_buf, &device_id[0], index);
			index = prepare_print_string(out_buf,
				",Sensor Cm Validation,BUTNS_CM_DATA_ROW00,",
				index);
			index = prepare_print_data(out_buf,
				&cmcp_info->cm_btn_data[0],
				index,
				btn_num);
			index = prepare_print_string(out_buf, "\n", index);
		}

		if ((test_item & CM_PANEL) == CM_PANEL) {
			/*print CM_DATA_ROW*/
			for (i = 0; i < rx_num; i++) {
				index = print_silicon_id(out_buf, &device_id[0],
							index);
				index = prepare_print_string(out_buf,
							",Sensor Cm Validation,CM_DATA_ROW",
							index);
				index = prepare_print_data(out_buf, &i,
							index, 1);
				for (j = 0; j < tx_num; j++)
					index = prepare_print_data(out_buf,
					&cmcp_info->cm_data_panel[j*rx_num+i],
					index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);
			}

			if (!no_builtin_file) {
				/*print CM_MAX_GRADIENT_COLS_PERCENT*/
				index = print_silicon_id(out_buf,
							&device_id[0], index);
				index = prepare_print_string(out_buf,
					 ",Sensor Cm Validation,CM_MAX_GRADIENT_COLS_PERCENT,",
					index);
				for (i = 0; i < tx_num; i++) {
					char tmp_buf[10] = {0};

					scnprintf(tmp_buf, 10, "%d.%d,",
				cmcp_info->gd_sensor_col[i].gradient_val / 10,
				cmcp_info->gd_sensor_col[i].gradient_val % 10);
					index = prepare_print_string(out_buf,
						&tmp_buf[0], index);
				}
				index = prepare_print_string(out_buf,
								"\n", index);

				/*print CM_MAX_GRADIENT_ROWS_PERCENT*/
				index = print_silicon_id(out_buf,
							&device_id[0], index);
				index = prepare_print_string(out_buf,
			",Sensor Cm Validation,CM_MAX_GRADIENT_ROWS_PERCENT,",
					index);
				for (i = 0; i < rx_num; i++) {
					char tmp_buf[10] = {0};

					scnprintf(tmp_buf, 10, "%d.%d,",
				cmcp_info->gd_sensor_row[i].gradient_val / 10,
				cmcp_info->gd_sensor_row[i].gradient_val % 10);
					index = prepare_print_string(out_buf,
						&tmp_buf[0], index);
				}
				index = prepare_print_string(out_buf,
								"\n", index);

				if (!dad->cmcp_range_check) {
					/*print CM_DELTA_COLUMN*/
					for (i = 0; i < rx_num; i++) {
						index = print_silicon_id(
							out_buf,
							&device_id[0], index);
						index = prepare_print_string(
							out_buf,
							",Sensor Cm Validation,DELTA_COLUMNS_ROW",
							index);
						index = prepare_print_data(
							out_buf,
							&i, index, 1);
						index = prepare_print_data(
							out_buf,
							&tmp, index, 1);
						for (j = 1; j < tx_num; j++)
							index = prepare_print_data(
								out_buf,
								&cmcp_info->cm_sensor_column_delta[(j-1)*rx_num+i],
								index, 1);
						index = prepare_print_string(
							out_buf,
							"\n", index);
					}

					/*print CM_DELTA_ROW*/
					index = print_silicon_id(out_buf,
								&device_id[0],
								index);
					index = prepare_print_string(out_buf,
						 ",Sensor Cm Validation,DELTA_ROWS_ROW",
								index);
					index = prepare_print_data(out_buf,
								&tmp, index, 1);
					for (j = 0; j < tx_num; j++)
						index = prepare_print_data(
								out_buf,
								&tmp, index, 1);
					index = prepare_print_string(out_buf,
								"\n", index);

					for (i = 1; i < rx_num; i++) {
						index = print_silicon_id(
								out_buf,
								&device_id[0],
								index);
						index = prepare_print_string(
								out_buf,
						",Sensor Cm Validation,DELTA_ROWS_ROW",
								index);
						index = prepare_print_data(
								out_buf, &i,
								index, 1);
						for (j = 0; j < tx_num; j++)
							index = prepare_print_data(
								out_buf,
							&cmcp_info->cm_sensor_row_delta[j*rx_num+i-1],
							index, 1);
						index = prepare_print_string(
							out_buf,
							"\n", index);
					}

				/*print pass/fail Sensor Cm Validation*/
				index = print_silicon_id(out_buf, &device_id[0],
							index);
				if (result->cm_test_pass)
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation,PASS,\n",
						index);
				else
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation,FAIL,\n",
						index);
			}
			}
		}

		if (!no_builtin_file) {
			if (((test_item & CM_BTN) == CM_BTN) && (btn_num > 0)
				&& (!dad->cmcp_range_check)) {
				char tmp_buf[10] = {0};
				/*print Button Element by Element */
				index = print_silicon_id(out_buf, &device_id[0],
					index);
				if (result->cm_button_validation_pass)
					index = prepare_print_string(out_buf,
					",Sensor Cm Validation - Button Element by Element,PASS\n",
					index);
				else
					index = prepare_print_string(out_buf,
					",Sensor Cm Validation - Button Element by Element,FAIL\n",
					index);

				/*
				*print  Sensor Cm Validation
				*- Buttons Range Buttons Range
				*/
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				index = prepare_print_string(out_buf,
					 ",Sensor Cm Validation - Buttons Range,Buttons Range,",
					 index);
				scnprintf(tmp_buf, 10, "%d.%d,",
					 cmcp_info->cm_delta_data_btn / 10,
					 cmcp_info->cm_delta_data_btn % 10);
				index = prepare_print_string(out_buf,
					&tmp_buf[0], index);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print  Sensor Cm Validation
				 *-Buttons Range Cm_button_avg
				 */
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				index = prepare_print_string(out_buf,
					 ",Sensor Cm Validation - Buttons Range,Cm_button_avg,",
					 index);
				index = prepare_print_data(out_buf,
					&cmcp_info->cm_ave_data_btn,
					 index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print  Sensor Cm Validation
				 * -Buttons Range Cm_button_avg
				 */
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				index = prepare_print_string(out_buf,
					",Sensor Cm Validation - Buttons Range,Cm_button_cal,",
					index);
				index = prepare_print_data(out_buf,
					&cmcp_info->cm_cal_data_btn,
					index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print  Sensor Cm Validation
				 *-Buttons Range pass/fail
				 */
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				if (result->cm_button_delta_pass)
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation - Buttons Range,PASS,LIMITS,",
						index);
				else
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation - Buttons Range,FAIL,LIMITS,",
						index);
				index = prepare_print_data(out_buf,
				&configuration->cm_max_delta_button_percent,
				index, 1);
				index = prepare_print_string(out_buf,
				"\n", index);
			}

			if ((test_item & CM_PANEL) == CM_PANEL &&
				!dad->cmcp_range_check) {
				char tmp_buf[10] = {0};
				/*print Cm_sensor_cal */
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				index = prepare_print_string(out_buf,
					",Sensor Cm Validation - Calibration,Cm_sensor_cal,",
					index);
				index = prepare_print_data(out_buf,
					&cmcp_info->cm_cal_data_panel,
					index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print Cm_sensor_cal limit*/
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				if (result->cm_sensor_calibration_pass)
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation - Calibration,PASS,LIMITS,",
						index);
				else
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation - Calibration,FAIL,LIMITS,",
						index);
				index = prepare_print_data(out_buf,
					&configuration->cm_min_limit_cal,
					index, 1);
				index = prepare_print_data(out_buf,
					&configuration->cm_max_limit_cal,
					index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print Columns Delta Matrix*/
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				if (result->cm_sensor_col_delta_pass)
					index = prepare_print_string(out_buf,
					",Sensor Cm Validation - Columns Delta Matrix,PASS,LIMITS,",
					index);
				else
					index = prepare_print_string(out_buf,
					",Sensor Cm Validation - Columns Delta Matrix,FAIL,LIMITS,",
					index);
				index = prepare_print_data(out_buf,
					&configuration->cm_range_limit_col,
					index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print Cm Validation - Element by Element*/
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				if (result->cm_sensor_validation_pass)
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation - Element by Element,PASS,",
						index);
				else
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation - Element by Element,FAIL,",
						index);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print Cm Validation -Gradient Cols*/
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				if (result->cm_sensor_gd_col_pass)
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation - Gradient Cols,PASS,",
						index);
				else
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation - Gradient Cols,FAIL,",
						index);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print Cm Validation -Gradient Rows*/
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				if (result->cm_sensor_gd_row_pass)
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation - Gradient Rows,PASS,",
						index);
				else
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation - Gradient Rows,FAIL,",
						index);
				index = prepare_print_string(out_buf,
					"\n", index);


				/*
				 * Print Sensor Cm Validation
				 * -Rows Delta Matrix
				 */
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				if (result->cm_sensor_row_delta_pass)
					index = prepare_print_string(out_buf,
					",Sensor Cm Validation - Rows Delta Matrix,PASS,LIMITS,",
					index);
				else
					index = prepare_print_string(out_buf,
					",Sensor Cm Validation - Rows Delta Matrix,FAIL,LIMITS,",
					index);
				index = prepare_print_data(out_buf,
					&configuration->cm_range_limit_row,
					index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print Cm_sensor_avg */
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				index = prepare_print_string(out_buf,
					",Sensor Cm Validation - Sensor Range,Cm_sensor_avg,",
					index);
				index = prepare_print_data(out_buf,
					&cmcp_info->cm_ave_data_panel,
					index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*printSensor Cm Validation -
				* Sensor Range,   Sensor Range
				*/
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				index = prepare_print_string(out_buf,
					",Sensor Cm Validation - Sensor Range,Sensor Range,",
					index);
				scnprintf(tmp_buf, 10, "%d.%d,",
					cmcp_info->cm_sensor_delta / 10,
					cmcp_info->cm_sensor_delta % 10);
				index = prepare_print_string(out_buf,
					&tmp_buf[0], index);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print Sensor Cm Validation - Sensor Range*/
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				if (result->cm_sensor_delta_pass)
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation - Sensor Range,PASS,LIMITS,",
						index);
				else
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation - Sensor Range,FAIL,LIMITS,",
						index);
				index = prepare_print_data(out_buf,
				&configuration->cm_max_delta_sensor_percent,
						index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);
			}
		}
	}

	if ((test_item & CP_ENABLED) == CP_ENABLED) {
		if (((test_item & CP_BTN) == CP_BTN) && (btn_num > 0)) {
			/*print   BUTNS_CP_DATA_ROW00  */
			index = print_silicon_id(out_buf, &device_id[0], index);
			index = prepare_print_string(out_buf,
			",Self-cap Calibration Check,BUTNS_CP_DATA_ROW00,",
				index);
			index = prepare_print_data(out_buf,
				&cmcp_info->cp_btn_data[0],
				index, btn_num);
			index = prepare_print_string(out_buf,
				"\n", index);

			if (!no_builtin_file && !dad->cmcp_range_check) {
				/*print Cp Button Element by Element */
				index = print_silicon_id(out_buf, &device_id[0],
					index);
				if (result->cp_button_validation_pass)
					index = prepare_print_string(out_buf,
					",Self-cap Calibration Check - Button Element by Element,PASS\n",
					index);
				else
					index = prepare_print_string(out_buf,
					",Self-cap Calibration Check - Button Element by Element,FAIL\n",
					index);

				/*print   cp_button_ave  */
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,Cp_button_avg,",
					index);
				index = prepare_print_data(out_buf,
					&cmcp_info->cp_button_ave,
					index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print   Cp_button_cal  */
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,Cp_button_cal,",
					index);
				index = prepare_print_data(out_buf,
					&cmcp_info->cp_btn_cal,
					index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);
			}
		}

		if ((test_item & CP_PANEL) == CP_PANEL) {
			/*print CP_DATA_RX */
			index = print_silicon_id(out_buf, &device_id[0], index);
			index = prepare_print_string(out_buf,
			",Self-cap Calibration Check,CP_DATA_RX,", index);
			index = prepare_print_data(out_buf,
				&cmcp_info->cp_rx_data_panel[0], index, rx_num);
			index = prepare_print_string(out_buf, "\n", index);

			/*print CP_DATA_TX */
			index = print_silicon_id(out_buf, &device_id[0], index);
			index = prepare_print_string(out_buf,
			",Self-cap Calibration Check,CP_DATA_TX,", index);
			index = prepare_print_data(out_buf,
				&cmcp_info->cp_tx_data_panel[0], index, tx_num);
			index = prepare_print_string(out_buf, "\n", index);
		}
		if (((test_item & CP_BTN) == CP_BTN) && (btn_num > 0)
			&& !dad->cmcp_range_check) {
			if (!no_builtin_file) {
				char tmp_buf[10] = {0};
				/*print  Cp_delta_button */
				index = print_silicon_id(out_buf, &device_id[0],
					index);
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,Cp_delta_button,",
				index);
				scnprintf(tmp_buf, 10, "%d.%d,",
				cmcp_info->cp_button_delta / 10,
				cmcp_info->cp_button_delta % 10);
				index = prepare_print_string(out_buf,
						&tmp_buf[0], index);
				index = prepare_print_string(out_buf, "\n",
					index);
			}
		}
		if ((test_item & CP_PANEL) == CP_PANEL &&
			!dad->cmcp_range_check) {
			if (!no_builtin_file) {
				char tmp_buf[10] = {0};
				/*print Cp_delta_rx */
				index = print_silicon_id(out_buf, &device_id[0],
					index);
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,Cp_delta_rx,",
				index);
				scnprintf(tmp_buf, 10, "%d.%d,",
				cmcp_info->cp_sensor_rx_delta / 10,
				cmcp_info->cp_sensor_rx_delta % 10);
				index = prepare_print_string(out_buf,
						&tmp_buf[0], index);
				index = prepare_print_string(out_buf, "\n",
					index);

				/*print Cp_delta_tx */
				index = print_silicon_id(out_buf, &device_id[0],
					index);
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,Cp_delta_tx,",
					index);
				scnprintf(tmp_buf, 10, "%d.%d,",
				cmcp_info->cp_sensor_tx_delta / 10,
				cmcp_info->cp_sensor_tx_delta % 10);
				index = prepare_print_string(out_buf,
						&tmp_buf[0], index);
				index = prepare_print_string(out_buf, "\n",
					index);

				/*print Cp_sensor_avg_rx */
				index = print_silicon_id(out_buf, &device_id[0],
					index);
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,Cp_sensor_avg_rx,",
					index);
				index = prepare_print_data(out_buf,
					&cmcp_info->cp_rx_ave_data_panel,
					index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print Cp_sensor_avg_tx */
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,Cp_sensor_avg_tx,",
					index);
				index = prepare_print_data(out_buf,
					&cmcp_info->cp_tx_ave_data_panel,
					index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print Cp_sensor_cal_rx */
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,Cp_sensor_cal_rx,",
					index);
				index = prepare_print_data(out_buf,
					&cmcp_info->cp_rx_cal_data_panel[0],
					index, rx_num);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print Cp_sensor_cal_tx */
				index = print_silicon_id(out_buf,
					&device_id[0], index);
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,Cp_sensor_cal_tx,",
					index);
				index = prepare_print_data(out_buf,
					&cmcp_info->cp_tx_cal_data_panel[0],
					index, tx_num);
				index = prepare_print_string(out_buf,
					"\n", index);
			}
		}

		if (!no_builtin_file && !dad->cmcp_range_check) {
			/*print  cp test limits  */
			index = print_silicon_id(out_buf, &device_id[0], index);
			if (result->cp_test_pass)
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,PASS, LIMITS,",
				index);
			else
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,FAIL, LIMITS,",
				index);

			index = prepare_print_string(out_buf,
				"CP_MAX_DELTA_SENSOR_RX_PERCENT,", index);
			index = prepare_print_data(out_buf,
				&configuration->cp_max_delta_sensor_rx_percent,
				index, 1);
			index = prepare_print_string(out_buf,
				"CP_MAX_DELTA_SENSOR_TX_PERCENT,", index);
			index = prepare_print_data(out_buf,
				&configuration->cp_max_delta_sensor_tx_percent,
				index, 1);
			index = prepare_print_string(out_buf,
				"CP_MAX_DELTA_BUTTON_PERCENT,", index);
			index = prepare_print_data(out_buf,
				&configuration->cp_max_delta_button_percent,
				index, 1);
			index = prepare_print_string(out_buf, "\n", index);
		}
	}

	if (!no_builtin_file) {
		if ((test_item & CM_ENABLED) == CM_ENABLED) {
			if ((test_item & CM_PANEL) == CM_PANEL) {
				/*print columns gradient limit*/
				index = prepare_print_string(out_buf,
			",Sensor Cm Validation,MAX_LIMITS,CM_MAX_GRADIENT_COLS_PERCENT,",
					index);
				index = prepare_print_data(out_buf,
			&configuration->cm_max_table_gradient_cols_percent[0],
					index,
			configuration->cm_max_table_gradient_cols_percent_size);
				index = prepare_print_string(out_buf,
					"\n", index);
				/*print rows gradient limit*/
				index = prepare_print_string(out_buf,
			",Sensor Cm Validation,MAX_LIMITS,CM_MAX_GRADIENT_ROWS_PERCENT,",
					index);
				index = prepare_print_data(out_buf,
			&configuration->cm_max_table_gradient_rows_percent[0],
					index,
			configuration->cm_max_table_gradient_rows_percent_size);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print cm max limit*/
				for (i = 0; i < rx_num; i++) {
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation,MAX_LIMITS,CM_DATA_ROW",
						index);
					index = prepare_print_data(out_buf,
						&i, index, 1);
					for (j = 0; j < tx_num; j++)
						index = prepare_print_data(
							out_buf,
		&configuration->cm_min_max_table_sensor[i*tx_num*2+j*2+1],
							index, 1);
					index = prepare_print_string(out_buf,
						"\n", index);
				}
			}

			if (((test_item & CM_BTN) == CM_BTN) && (btn_num > 0)) {
				index = prepare_print_string(out_buf,
					",Sensor Cm Validation,MAX LIMITS,M_BUTNS,",
					index);
				for (j = 0; j < btn_num; j++) {
					index = prepare_print_data(out_buf,
				&configuration->cm_min_max_table_button[2*j+1],
						index, 1);
				}
				index = prepare_print_string(out_buf,
					"\n", index);
			}

			index = prepare_print_string(out_buf,
				 ",Sensor Cm Validation MAX LIMITS\n", index);

			if ((test_item & CM_PANEL) == CM_PANEL) {
				/*print cm min limit*/
				for (i = 0; i < rx_num; i++) {
					index = prepare_print_string(out_buf,
						",Sensor Cm Validation,MIN_LIMITS,CM_DATA_ROW",
						index);
					index = prepare_print_data(out_buf, &i,
						index, 1);
					for (j = 0; j < tx_num; j++)
						index = prepare_print_data(
							out_buf,
		&configuration->cm_min_max_table_sensor[i*tx_num*2 + j*2],
							index, 1);
					index = prepare_print_string(out_buf,
						"\n", index);
				}
			}

			if (((test_item & CM_BTN) == CM_BTN) && (btn_num > 0)) {
				index = prepare_print_string(out_buf,
					",Sensor Cm Validation,MIN LIMITS,M_BUTNS,",
					index);
				for (j = 0; j < btn_num; j++) {
					index = prepare_print_data(out_buf,
				&configuration->cm_min_max_table_button[2*j],
						 index, 1);
				}
				index = prepare_print_string(out_buf,
					"\n", index);
			}
			index = prepare_print_string(out_buf,
				",Sensor Cm Validation MIN LIMITS\n", index);
		}

		if ((test_item & CP_ENABLED) == CP_ENABLED) {
			if ((test_item & CP_PANEL) == CP_PANEL) {
				/*print cp tx max limit*/
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,MAX_LIMITS,TX,",
					index);
				for (i = 0; i < tx_num; i++)
					index = prepare_print_data(out_buf,
				&configuration->cp_min_max_table_tx[i*2+1],
						index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print cp rx max limit*/
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,MAX_LIMITS,RX,",
					index);
				for (i = 0; i < rx_num; i++)
					index = prepare_print_data(out_buf,
				&configuration->cp_min_max_table_rx[i*2+1],
						index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);
			}

			/*print cp btn max limit*/
			if (((test_item & CP_BTN) == CP_BTN) && (btn_num > 0)) {
				index = prepare_print_string(out_buf,
			",Self-cap Calibration Check,MAX_LIMITS,S_BUTNS,",
					index);
				for (i = 0; i < btn_num; i++)
					index = prepare_print_data(out_buf,
				&configuration->cp_min_max_table_button[i*2+1],
						index, 1);
				index = prepare_print_string(out_buf,
						"\n", index);
			}

			if ((test_item & CP_PANEL) == CP_PANEL) {
				/*print cp tx min limit*/
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,MIN_LIMITS,TX,",
					index);
				for (i = 0; i < tx_num; i++)
					index = prepare_print_data(out_buf,
				&configuration->cp_min_max_table_tx[i*2],
						index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);

				/*print cp rx min limit*/
				index = prepare_print_string(out_buf,
				",Self-cap Calibration Check,MIN_LIMITS,RX,",
					index);
				for (i = 0; i < rx_num; i++)
					index = prepare_print_data(out_buf,
				&configuration->cp_min_max_table_rx[i*2],
						index, 1);
				index = prepare_print_string(out_buf,
					"\n", index);
			}

			/*print cp btn min limit*/
			if (((test_item & CP_BTN) == CP_BTN) && (btn_num > 0)) {
				index = prepare_print_string(out_buf,
			",Self-cap Calibration Check,MIN_LIMITS,S_BUTNS,",
					index);
				for (i = 0; i < btn_num; i++)
					index = prepare_print_data(out_buf,
				&configuration->cp_min_max_table_button[i*2],
						index, 1);
				index = prepare_print_string(out_buf,
						"\n", index);
			}
		}
	}
	return index;
}

static int result_save(struct device *dev, char *buf,
	struct configuration *configuration, struct result *result,
	struct cmcp_data *cmcp_info, loff_t *ppos, size_t count, int test_item,
	int no_builtin_file)
{
	u8 *out_buf = NULL;
	int index = 0;
	int byte_left;

	out_buf = kzalloc(MAX_BUF_LEN, GFP_KERNEL);
	if (configuration == NULL)
		dev_err(dev, "config is NULL");
	if (result == NULL)
		dev_err(dev, "result is NULL");
	if (cmcp_info == NULL)
		dev_err(dev, "cmcp_info is NULL");

	index = save_header(out_buf, index, result);
	index = save_engineering_data(dev, out_buf, index,
		cmcp_info, configuration, result,
		test_item, no_builtin_file);
	byte_left = simple_read_from_buffer(buf, count, ppos, out_buf, index);

	kfree(out_buf);
	return byte_left;
}

static int cmcp_results_debugfs_open(struct inode *inode,
		struct file *filp)
{
	filp->private_data = inode->i_private;
	return 0;
}

static int cmcp_results_debugfs_close(struct inode *inode,
		struct file *filp)
{
	filp->private_data = NULL;
	return 0;
}

static ssize_t cmcp_results_debugfs_read(struct file *filp,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_data *dad = filp->private_data;
	struct device *dev;
	struct cmcp_data *cmcp_info = dad->cmcp_info;
	struct result *result = dad->result;
	struct configuration *configuration = dad->configs;
	int ret = 0;
	int test_item;
	int no_builtin_file = 0;
	int test_executed = 0;

	dev = dad->dev;

	mutex_lock(&dad->sysfs_lock);
	test_executed = dad->test_executed;
	test_item = cyttsp5_cmcp_get_test_item(dad->cmcp_test_items);
	if (dad->builtin_cmcp_threshold_status < 0) {
		dev_err(dev, "%s: No cmcp threshold file.\n", __func__);
		no_builtin_file = 1;
	}
	mutex_unlock(&dad->sysfs_lock);

	if (test_executed)
		/*save result to buf*/
		ret = result_save(dev, buf, configuration, result, cmcp_info,
			ppos, count, test_item, no_builtin_file);
	else {
		char warning_info[] =
		"No test result available!\n";
		dev_err(dev, "%s: No test result available!\n", __func__);

		return simple_read_from_buffer(buf, count, ppos, warning_info,
			strlen(warning_info));
	}

	return ret;
}

static const struct file_operations cmcp_results_debugfs_fops = {
	.open = cmcp_results_debugfs_open,
	.release = cmcp_results_debugfs_close,
	.read = cmcp_results_debugfs_read,
	.write = NULL,
};

static ssize_t cyttsp5_cmcp_threshold_loading_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);
	bool cmcp_threshold_loading;

	mutex_lock(&dad->cmcp_threshold_lock);
	cmcp_threshold_loading = dad->cmcp_threshold_loading;
	mutex_unlock(&dad->cmcp_threshold_lock);

	return sprintf(buf, "%d\n", cmcp_threshold_loading);
}

/* Return the buffer offset of new test case */
static u32 cmcp_return_offset_of_new_case(const char *bufPtr, u32 first_time)
{
	static u32 offset, first_search;

	if (first_time == 0) {
		first_search = 0;
		offset = 0;
	}

	if (first_search != 0) {
		/* Search one case */
		for (;;) {
			/* Search ASCII_LF */
			while (*bufPtr++ != ASCII_LF)
				offset++;

			offset++;
			/*
			 * Single line: end loop
			 * Multiple lines: continue loop
			 */
			if (*bufPtr != ASCII_COMMA)
				break;
		}
	} else
		first_search = 1;

	return offset;
}

/* Get test case information from cmcp threshold file */
static u32 cmcp_get_case_info_from_threshold_file(struct device *dev, const char *buf,
		struct test_case_search *search_array, u32 file_size)
{
	u32 case_num = 0, buffer_offset = 0, name_count = 0, first_search = 0;

	parade_debug(dev, DEBUG_LEVEL_2, "%s: Search cmcp threshold file\n",
		__func__);

	/* Get all the test cases */
	for (case_num = 0; case_num < MAX_CASE_NUM; case_num++) {
		buffer_offset =
			cmcp_return_offset_of_new_case(&buf[buffer_offset],
			first_search);
		first_search = 1;

		if (buf[buffer_offset] == 0)
			break;

		for (name_count = 0; name_count < NAME_SIZE_MAX; name_count++) {
			/* File end */
			if (buf[buffer_offset + name_count] == ASCII_COMMA)
				break;

			search_array[case_num].name[name_count] =
					buf[buffer_offset + name_count];
		}

		/* Exit when buffer offset is larger than file size */
		if (buffer_offset >= file_size)
			break;

		search_array[case_num].name_size = name_count;
		search_array[case_num].offset = buffer_offset;
		/*
		 *  parade_debug(dev, DEBUG_LEVEL_2, "Find case %d: Name is %s;
		 *  Name size is %d; Case offset is %d\n",
		 *	case_num,
		 *	search_array[case_num].name,
		 *	search_array[case_num].name_size,
		 *	search_array[case_num].offset);
		 */
	}

	return case_num;
}

/* Compose one value based on data of each bit */
static int cmcp_compose_data(char *buf, u32 count)
{
	u32 base_array[] = {1, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9};
	int value = 0;
	u32 index = 0;

	for (index = 0; index < count; index++)
		value += buf[index] * base_array[count - 1 - index];

	return value;
}

/* Return one value */
static int cmcp_return_one_value(struct device *dev,
		const char *buf, u32 *offset, u32 *line_num)
{
	int value = -1;
	char tmp_buffer[10];
	u32 count = 0;
	u32 tmp_offset = *offset;
	static u32 line_count = 1;

	/* Bypass extra commas */
	while (buf[tmp_offset] == ASCII_COMMA
			&& buf[tmp_offset + 1] == ASCII_COMMA)
		tmp_offset++;

	/* Windows and Linux difference at the end of one line */
	if (buf[tmp_offset] == ASCII_COMMA
			&& buf[tmp_offset + 1] == ASCII_CR
			&& buf[tmp_offset + 2] == ASCII_LF)
		tmp_offset += 2;
	else if (buf[tmp_offset] == ASCII_COMMA
			&& buf[tmp_offset + 1] == ASCII_LF)
		tmp_offset += 1;
	else if (buf[tmp_offset] == ASCII_COMMA
			&& buf[tmp_offset + 1] == ASCII_CR)
		tmp_offset += 1;

	/* New line for multiple lines */
	if (buf[tmp_offset] == ASCII_LF && buf[tmp_offset + 1] == ASCII_COMMA) {
		tmp_offset++;
		line_count++;
		/*parade_debug(dev, DEBUG_LEVEL_2, "\n");*/
	}

	/* Beginning */
	if (buf[tmp_offset] == ASCII_COMMA) {
		tmp_offset++;
		for (;;) {
			if ((buf[tmp_offset] >= ASCII_ZERO)
			&& (buf[tmp_offset] <= ASCII_NINE)) {
				tmp_buffer[count++] =
				buf[tmp_offset] - ASCII_ZERO;
				tmp_offset++;
			} else {
				if (count != 0) {
					value = cmcp_compose_data(tmp_buffer,
					count);
					/*parade_debug(dev, DEBUG_LEVEL_2, */
					/* ",%d", value);*/
				} else {
					/* 0 indicates no data available */
					value = -1;
				}
				break;
			}
		}
	} else {
		/* Multiple line: line count */
		*line_num = line_count;
		/* Reset for next case */
		line_count = 1;
	}

	*offset = tmp_offset;

	return value;
}

/* Get configuration information */
static void cmcp_get_configuration_info(struct device *dev,
		const char *buf, struct test_case_search *search_array,
		u32 case_count, struct test_case_field *field_array,
		struct configuration *config)
{
	u32 count = 0, sub_count = 0;
	u32 exist_or_not = 0;
	u32 value_offset = 0;
	int retval = 0;
	u32 data_num = 0;
	u32 line_num = 1;

	parade_debug(dev, DEBUG_LEVEL_2,
		"%s: Fill configuration struct per cmcp threshold file\n",
		__func__);

	/* Search cases */
	for (count = 0; count < MAX_CASE_NUM; count++) {
		exist_or_not = 0;
		for (sub_count = 0; sub_count < case_count; sub_count++) {
			if (!strncmp(field_array[count].name,
					search_array[sub_count].name,
					field_array[count].name_size)) {
				exist_or_not = 1;
				break;
			}
		}

		field_array[count].exist_or_not = exist_or_not;

		/* Clear data number */
		data_num = 0;

		if (exist_or_not == 1) {
			switch (field_array[count].type) {
			case TEST_CASE_TYPE_NO:
				field_array[count].data_num = 0;
				field_array[count].line_num = 1;
				break;
			case TEST_CASE_TYPE_ONE:
				value_offset = search_array[sub_count].offset
					+ search_array[sub_count].name_size;
				*field_array[count].bufptr =
						cmcp_return_one_value(dev, buf,
						&value_offset, 0);
				field_array[count].data_num = 1;
				field_array[count].line_num = 1;
				break;
			case TEST_CASE_TYPE_MUL:
			case TEST_CASE_TYPE_MUL_LINES:
				line_num = 1;
				value_offset = search_array[sub_count].offset
					+ search_array[sub_count].name_size;
				for (;;) {
					retval = cmcp_return_one_value(dev,
						buf, &value_offset, &line_num);
					if (retval >= 0) {
						*field_array[count].bufptr++ =
						retval;
						data_num++;
					} else
						break;
				}

				field_array[count].data_num = data_num;
				field_array[count].line_num = line_num;
				break;
			default:
				break;
			}
			parade_debug(dev, DEBUG_LEVEL_2,
				"%s: %s: Data number is %d, line number is %d\n",
				__func__,
				field_array[count].name,
				field_array[count].data_num,
				field_array[count].line_num);
		} else
			parade_debug(dev, DEBUG_LEVEL_2, "%s: !!! %s doesn't exist\n",
				__func__, field_array[count].name);
	}
}

/* Get basic information, like tx, rx, button number */
static void cmcp_get_basic_info(struct device *dev,
	struct test_case_field *field_array, struct configuration *config)
{
#define CMCP_DEBUG 0
	u32 tx_num = 0;
#if CMCP_DEBUG
	u32 index = 0;
#endif

	config->is_valid_or_not = 1; /* Set to valid by default */
	config->cm_enabled = 0;
	config->cp_enabled = 0;

	if (field_array[CM_TEST_INPUTS].exist_or_not)
		config->cm_enabled = 1;
	if (field_array[CP_TEST_INPUTS].exist_or_not)
		config->cp_enabled = 1;

	/* Get basic information only when CM and CP are enabled */
	if (config->cm_enabled && config->cp_enabled) {
		parade_debug(dev, DEBUG_LEVEL_2,
			"%s: Find CM and CP thresholds\n", __func__);

		config->rx_num =
			field_array[PER_ELEMENT_MIN_MAX_TABLE_SENSOR].line_num;
		tx_num =
		(field_array[PER_ELEMENT_MIN_MAX_TABLE_SENSOR].data_num >> 1)
			/field_array[PER_ELEMENT_MIN_MAX_TABLE_SENSOR].line_num;
		config->tx_num = tx_num;

		config->btn_num =
		field_array[PER_ELEMENT_MIN_MAX_TABLE_BUTTON].data_num >> 1;

		config->cm_min_max_table_button_size =
			field_array[PER_ELEMENT_MIN_MAX_TABLE_BUTTON].data_num;
		config->cm_min_max_table_sensor_size =
			field_array[PER_ELEMENT_MIN_MAX_TABLE_SENSOR].data_num;
		config->cp_min_max_table_rx_size =
			field_array[PER_ELEMENT_MIN_MAX_RX].data_num;
		config->cp_min_max_table_tx_size =
			field_array[PER_ELEMENT_MIN_MAX_TX].data_num;
		config->cm_max_table_gradient_cols_percent_size =
			field_array[CM_GRADIENT_CHECK_COL].data_num;
		config->cm_max_table_gradient_rows_percent_size =
			field_array[CM_GRADIENT_CHECK_ROW].data_num;
		config->cp_min_max_table_button_size =
			field_array[CP_PER_ELEMENT_MIN_MAX_BUTTON].data_num;

#if CMCP_DEBUG
		parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
					config->cm_excluding_col_edge);
		parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
					config->cm_excluding_row_edge);
		for (index = 0;
		index < config->cm_max_table_gradient_cols_percent_size;
		index++)
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
			config->cm_max_table_gradient_cols_percent[index]);
		for (index = 0;
		index < config->cm_max_table_gradient_rows_percent_size;
		index++)
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
			config->cm_max_table_gradient_rows_percent[index]);
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->cm_range_limit_row);
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->cm_range_limit_col);
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->cm_min_limit_cal);
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->cm_max_limit_cal);
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->cm_max_delta_sensor_percent);
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->cm_max_delta_button_percent);
		for (index = 0;
		index < config->cm_min_max_table_button_size; index++)
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->cm_min_max_table_button[index]);
		for (index = 0;
			index < config->cm_min_max_table_sensor_size; index++)
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->cm_min_max_table_sensor[index]);
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->cp_max_delta_sensor_rx_percent);
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->cp_max_delta_sensor_tx_percent);
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->cp_max_delta_button_percent);
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->min_button);
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->max_button);

		for (index = 0;
		index < config->cp_min_max_table_button_size; index++)
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->cp_min_max_table_button[index]);
		for (index = 0;
		index < config->cp_min_max_table_rx_size; index++)
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->cp_min_max_table_rx[index]);
		for (index = 0;
		index < config->cp_min_max_table_tx_size; index++)
			parade_debug(dev, DEBUG_LEVEL_2, "%d\n",
				config->cp_min_max_table_tx[index]);
#endif
		/* Invalid mutual data length */
		if ((field_array[PER_ELEMENT_MIN_MAX_TABLE_SENSOR].data_num >>
		1) % field_array[PER_ELEMENT_MIN_MAX_TABLE_SENSOR].line_num) {
			config->is_valid_or_not = 0;
			parade_debug(dev, DEBUG_LEVEL_2, "Invalid mutual data length\n");
		}
	} else {
		if (!config->cm_enabled)
			parade_debug(dev, DEBUG_LEVEL_2,
				"%s: Miss CM thresholds or CM data format is wrong!\n",
				__func__);

		if (!config->cp_enabled)
			parade_debug(dev, DEBUG_LEVEL_2,
				"%s: Miss CP thresholds or CP data format is wrong!\n",
				__func__);

		config->rx_num = 0;
		config->tx_num = 0;
		config->btn_num = 0;
		config->is_valid_or_not = 0;
	}

	parade_debug(dev, DEBUG_LEVEL_2,
		"%s:\n"
		"Input file is %s!\n"
		"CM test: %s\n"
		"CP test: %s\n"
		"rx_num is %d\n"
		"tx_num is %d\n"
		"btn_num is %d\n",
		__func__,
		config->is_valid_or_not == 1 ? "VALID" : "!!! INVALID !!!",
		config->cm_enabled == 1 ? "Found" : "Not found",
		config->cp_enabled == 1 ? "Found" : "Not found",
		config->rx_num,
		config->tx_num,
		config->btn_num);
}

static void cmcp_test_case_field_init(struct test_case_field *test_field_array,
	struct configuration *configs)
{
	struct test_case_field test_case_field_array[MAX_CASE_NUM] = {
		{"CM TEST INPUTS", 14, TEST_CASE_TYPE_NO,
				NULL, 0, 0, 0},
		{"CM_EXCLUDING_COL_EDGE", 21, TEST_CASE_TYPE_ONE,
				&configs->cm_excluding_col_edge, 0, 0, 0},
		{"CM_EXCLUDING_ROW_EDGE", 21, TEST_CASE_TYPE_ONE,
				&configs->cm_excluding_row_edge, 0, 0, 0},
		{"CM_GRADIENT_CHECK_COL", 21, TEST_CASE_TYPE_MUL,
				&configs->cm_max_table_gradient_cols_percent[0],
				0, 0, 0},
		{"CM_GRADIENT_CHECK_ROW", 21, TEST_CASE_TYPE_MUL,
				&configs->cm_max_table_gradient_rows_percent[0],
				0, 0, 0},
		{"CM_RANGE_LIMIT_ROW", 18, TEST_CASE_TYPE_ONE,
				&configs->cm_range_limit_row, 0, 0, 0},
		{"CM_RANGE_LIMIT_COL", 18, TEST_CASE_TYPE_ONE,
				&configs->cm_range_limit_col, 0, 0, 0},
		{"CM_MIN_LIMIT_CAL", 16, TEST_CASE_TYPE_ONE,
				&configs->cm_min_limit_cal, 0, 0, 0},
		{"CM_MAX_LIMIT_CAL", 16, TEST_CASE_TYPE_ONE,
				&configs->cm_max_limit_cal, 0, 0, 0},
		{"CM_MAX_DELTA_SENSOR_PERCENT", 27, TEST_CASE_TYPE_ONE,
				&configs->cm_max_delta_sensor_percent, 0, 0, 0},
		{"CM_MAX_DELTA_BUTTON_PERCENT", 27, TEST_CASE_TYPE_ONE,
				&configs->cm_max_delta_button_percent, 0, 0, 0},
		{"PER_ELEMENT_MIN_MAX_TABLE_BUTTON", 32, TEST_CASE_TYPE_MUL,
				&configs->cm_min_max_table_button[0], 0, 0, 0},
		{"PER_ELEMENT_MIN_MAX_TABLE_SENSOR", 32,
				TEST_CASE_TYPE_MUL_LINES,
				&configs->cm_min_max_table_sensor[0], 0, 0, 0},
		{"CP TEST INPUTS", 14, TEST_CASE_TYPE_NO,
				NULL, 0, 0, 0},
		{"CP_PER_ELEMENT_MIN_MAX_BUTTON", 29, TEST_CASE_TYPE_MUL,
				&configs->cp_min_max_table_button[0], 0, 0, 0},
		{"CP_MAX_DELTA_SENSOR_RX_PERCENT", 30, TEST_CASE_TYPE_ONE,
				&configs->cp_max_delta_sensor_rx_percent,
				0, 0, 0},
		{"CP_MAX_DELTA_SENSOR_TX_PERCENT", 30, TEST_CASE_TYPE_ONE,
				&configs->cp_max_delta_sensor_tx_percent,
				0, 0, 0},
		{"CP_MAX_DELTA_BUTTON_PERCENT", 27, TEST_CASE_TYPE_ONE,
				&configs->cp_max_delta_button_percent, 0, 0, 0},
		{"MIN_BUTTON", 10, TEST_CASE_TYPE_ONE,
				&configs->min_button, 0, 0, 0},
		{"MAX_BUTTON", 10, TEST_CASE_TYPE_ONE,
				&configs->max_button, 0, 0, 0},
		{"PER_ELEMENT_MIN_MAX_RX", 22, TEST_CASE_TYPE_MUL,
				&configs->cp_min_max_table_rx[0], 0, 0, 0},
		{"PER_ELEMENT_MIN_MAX_TX", 22, TEST_CASE_TYPE_MUL,
				&configs->cp_min_max_table_tx[0], 0, 0, 0},
	};

	memcpy(test_field_array, test_case_field_array,
		sizeof(struct test_case_field) * MAX_CASE_NUM);
}

static ssize_t cyttsp5_parse_cmcp_threshold_file_common(
	struct device *dev, const char *buf, u32 file_size)
{
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);
	ssize_t rc = 0;
	u32 case_count = 0;

	parade_debug(dev, DEBUG_LEVEL_2,
		"%s: Start parsing cmcp threshold file. File size is %d\n",
		__func__, file_size);

	cmcp_test_case_field_init(dad->test_field_array, dad->configs);

	/* Get all the cases from .csv file */
	case_count = cmcp_get_case_info_from_threshold_file(dev,
		buf, dad->test_search_array, file_size);

	/* Search cases */
	cmcp_get_configuration_info(dev,
		buf,
		dad->test_search_array, case_count, dad->test_field_array,
		dad->configs);

	/* Get basic information */
	cmcp_get_basic_info(dev, dad->test_field_array, dad->configs);

	return rc;
}

static ssize_t cyttsp5_cmcp_threshold_loading_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);
	long value;
	int rc;

	rc = kstrtol(buf, 10, &value);
	if (rc < 0 || value < -1 || value > 1) {
		dev_err(dev, "%s: Invalid value\n", __func__);
		return size;
	}

	mutex_lock(&dad->cmcp_threshold_lock);

	if (value == 1)
		dad->cmcp_threshold_loading = true;
	else if (value == -1)
		dad->cmcp_threshold_loading = false;
	else if (value == 0 && dad->cmcp_threshold_loading) {
		dad->cmcp_threshold_loading = false;

		if (dad->cmcp_threshold_size == 0) {
			dev_err(dev, "%s: No cmcp threshold data\n", __func__);
			goto exit_free;
		}

		/* Clear test executed flag */
		dad->test_executed = 0;

		cyttsp5_parse_cmcp_threshold_file_common(dev,
			&dad->cmcp_threshold_data[0], dad->cmcp_threshold_size);

		/* Mark valid */
		dad->builtin_cmcp_threshold_status = 0;
		/* Restore test item to default value when new file input */
		dad->cmcp_test_items = 0;
	}

exit_free:
	kfree(dad->cmcp_threshold_data);
	dad->cmcp_threshold_data = NULL;
	dad->cmcp_threshold_size = 0;

	mutex_unlock(&dad->cmcp_threshold_lock);

	if (rc)
		return rc;

	return size;
}

static DEVICE_ATTR(cmcp_threshold_loading, S_IRUSR | S_IWUSR,
	cyttsp5_cmcp_threshold_loading_show,
	cyttsp5_cmcp_threshold_loading_store);

/*
* cmcp threshold data write
*/
static ssize_t cyttsp5_cmcp_threshold_data_write(struct file *filp,
		struct kobject *kobj, struct bin_attribute *bin_attr,
		char *buf, loff_t offset, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);
	u8 *p;

	parade_debug(dev, DEBUG_LEVEL_2, "%s: offset:%lld count:%zu\n",
		__func__, offset, count);

	mutex_lock(&dad->cmcp_threshold_lock);

	if (!dad->cmcp_threshold_loading) {
		mutex_unlock(&dad->cmcp_threshold_lock);
		return -ENODEV;
	}

	p = krealloc(dad->cmcp_threshold_data, offset + count, GFP_KERNEL);
	if (!p) {
		kfree(dad->cmcp_threshold_data);
		dad->cmcp_threshold_data = NULL;
		mutex_unlock(&dad->cmcp_threshold_lock);
		return -ENOMEM;
	}
	dad->cmcp_threshold_data = p;

	memcpy(&dad->cmcp_threshold_data[offset], buf, count);
	dad->cmcp_threshold_size += count;

	mutex_unlock(&dad->cmcp_threshold_lock);

	return count;
}

static struct bin_attribute bin_attr_cmcp_threshold_data = {
	.attr = {
		.name = "cmcp_threshold_data",
		.mode = S_IWUSR,
	},
	.size = 0,
	.write = cyttsp5_cmcp_threshold_data_write,
};

/*
 * Suspend scan command
 */
static int cyttsp5_suspend_scan_cmd_(struct device *dev)
{
	int rc;

	rc = cmd->nonhid_cmd->suspend_scanning(dev, 0);
	if (rc < 0)
		dev_err(dev, "%s: Suspend scan failed r=%d\n",
			__func__, rc);
	return rc;
}

/*
 * Resume scan command
 */
static int cyttsp5_resume_scan_cmd_(struct device *dev)
{
	int rc;

	rc = cmd->nonhid_cmd->resume_scanning(dev, 0);
	if (rc < 0)
		dev_err(dev, "%s: Resume scan failed r=%d\n",
			__func__, rc);
	return rc;
}

/*
 * Execute scan command
 */
static int cyttsp5_exec_scan_cmd_(struct device *dev)
{
	int rc;

	rc = cmd->nonhid_cmd->exec_panel_scan(dev, 0);
	if (rc < 0)
		dev_err(dev, "%s: Heatmap start scan failed r=%d\n",
			__func__, rc);
	return rc;
}

/*
 * Retrieve panel data command
 */
static int cyttsp5_ret_scan_data_cmd_(struct device *dev, u16 read_offset,
		u16 read_count, u8 data_id, u8 *response, u8 *config,
		u16 *actual_read_len, u8 *return_buf)
{
	int rc;

	rc = cmd->nonhid_cmd->retrieve_panel_scan(dev, 0, read_offset,
			read_count, data_id, response, config, actual_read_len,
			return_buf);
	if (rc < 0)
		dev_err(dev, "%s: Retrieve scan data failed r=%d\n",
				__func__, rc);
	return rc;
}

/*
 * Get data structure command
 */
static int cyttsp5_get_data_structure_cmd_(struct device *dev, u16 read_offset,
		u16 read_length, u8 data_id, u8 *status, u8 *data_format,
		u16 *actual_read_len, u8 *data)
{
	int rc;

	rc = cmd->nonhid_cmd->get_data_structure(dev, 0, read_offset,
			read_length, data_id, status, data_format,
			actual_read_len, data);
	if (rc < 0)
		dev_err(dev, "%s: Get data structure failed r=%d\n",
				__func__, rc);
	return rc;
}

/*
 * Run self test command
 */
static int cyttsp5_run_selftest_cmd_(struct device *dev, u8 test_id,
		u8 write_idacs_to_flash, u8 *status, u8 *summary_result,
		u8 *results_available)
{
	int rc;

	rc = cmd->nonhid_cmd->run_selftest(dev, 0, test_id,
			write_idacs_to_flash, status, summary_result,
			results_available);
	if (rc < 0)
		dev_err(dev, "%s: Run self test failed r=%d\n",
				__func__, rc);
	return rc;
}

/*
 * Get self test result command
 */
static int cyttsp5_get_selftest_result_cmd_(struct device *dev,
		u16 read_offset, u16 read_length, u8 test_id, u8 *status,
		u16 *actual_read_len, u8 *data)
{
	int rc;

	rc = cmd->nonhid_cmd->get_selftest_result(dev, 0, read_offset,
			read_length, test_id, status, actual_read_len, data);
	if (rc < 0)
		dev_err(dev, "%s: Get self test result failed r=%d\n",
				__func__, rc);
	return rc;
}

/*
 * Calibrate IDACs command
 */
static int _cyttsp5_calibrate_idacs_cmd(struct device *dev,
		u8 sensing_mode, u8 *status)
{
	int rc;

	rc = cmd->nonhid_cmd->calibrate_idacs(dev, 0, sensing_mode, status);
	return rc;
}

/*
 * Initialize Baselines command
 */
static int _cyttsp5_initialize_baselines_cmd(struct device *dev,
		u8 sensing_mode, u8 *status)
{
	int rc;

	rc = cmd->nonhid_cmd->initialize_baselines(dev, 0, sensing_mode,
			status);
	return rc;
}

static int prepare_print_buffer(int status, u8 *in_buf, int length,
		u8 *out_buf, size_t out_buf_size)
{
	int index = 0;
	int i;

	index += scnprintf(out_buf, out_buf_size, "status %d\n", status);

	for (i = 0; i < length; i++) {
		index += scnprintf(&out_buf[index], out_buf_size - index,
				"%02X\n", in_buf[i]);
	}

	return index;
}
static ssize_t cyttsp5_run_and_get_selftest_result_noprint(struct device *dev,
		char *buf, size_t buf_len, u8 test_id, u16 read_length,
		bool get_result_on_pass)
{
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);
	int status = STATUS_FAIL;
	u8 cmd_status = 0;
	u8 summary_result = 0;
	u16 act_length = 0;
	int length = 0;
	int rc;

	mutex_lock(&dad->sysfs_lock);

	pm_runtime_get_sync(dev);

	rc = cmd->request_exclusive(dev, CY_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request exclusive r=%d\n",
				__func__, rc);
		goto put_pm_runtime;
	}

	rc = cyttsp5_suspend_scan_cmd_(dev);
	if (rc < 0) {
		dev_err(dev, "%s: Error on suspend scan r=%d\n",
				__func__, rc);
		goto release_exclusive;
	}

	rc = cyttsp5_run_selftest_cmd_(dev, test_id, 0,
			&cmd_status, &summary_result, NULL);
	if (rc < 0) {
		dev_err(dev, "%s: Error on run self test for test_id:%d r=%d\n",
				__func__, test_id, rc);
		goto resume_scan;
	}

	/* Form response buffer */
	dad->ic_buf[0] = cmd_status;
	dad->ic_buf[1] = summary_result;

	length = 2;

	/* Get data if command status is success */
	if (cmd_status != CY_CMD_STATUS_SUCCESS)
		goto status_success;

	/* Get data unless test result is pass */
	if (summary_result == CY_ST_RESULT_PASS && !get_result_on_pass)
		goto status_success;

	rc = cyttsp5_get_selftest_result_cmd_(dev, 0, read_length,
			test_id, &cmd_status, &act_length, &dad->ic_buf[6]);
	if (rc < 0) {
		dev_err(dev, "%s: Error on get self test result r=%d\n",
				__func__, rc);
		goto resume_scan;
	}

	dad->ic_buf[2] = cmd_status;
	dad->ic_buf[3] = test_id;
	dad->ic_buf[4] = LOW_BYTE(act_length);
	dad->ic_buf[5] = HI_BYTE(act_length);

	length = 6 + act_length;

status_success:
	status = STATUS_SUCCESS;

resume_scan:
	cyttsp5_resume_scan_cmd_(dev);

release_exclusive:
	cmd->release_exclusive(dev);

put_pm_runtime:
	pm_runtime_put(dev);
	mutex_unlock(&dad->sysfs_lock);

	return status;
}

static ssize_t cyttsp5_run_and_get_selftest_result(struct device *dev,
		char *buf, size_t buf_len, u8 test_id, u16 read_length,
		bool get_result_on_pass)
{
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);
	int status = STATUS_FAIL;
	u8 cmd_status = 0;
	u8 summary_result = 0;
	u16 act_length = 0;
	int length = 0;
	int size;
	int rc;

	mutex_lock(&dad->sysfs_lock);

	pm_runtime_get_sync(dev);

	rc = cmd->request_exclusive(dev, CY_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request exclusive r=%d\n",
				__func__, rc);
		goto put_pm_runtime;
	}

	rc = cyttsp5_suspend_scan_cmd_(dev);
	if (rc < 0) {
		dev_err(dev, "%s: Error on suspend scan r=%d\n",
				__func__, rc);
		goto release_exclusive;
	}

	rc = cyttsp5_run_selftest_cmd_(dev, test_id, 0,
			&cmd_status, &summary_result, NULL);
	if (rc < 0) {
		dev_err(dev, "%s: Error on run self test for test_id:%d r=%d\n",
				__func__, test_id, rc);
		goto resume_scan;
	}

	/* Form response buffer */
	dad->ic_buf[0] = cmd_status;
	dad->ic_buf[1] = summary_result;

	length = 2;

	/* Get data if command status is success */
	if (cmd_status != CY_CMD_STATUS_SUCCESS)
		goto status_success;

	/* Get data unless test result is pass */
	if (summary_result == CY_ST_RESULT_PASS && !get_result_on_pass)
		goto status_success;

	rc = cyttsp5_get_selftest_result_cmd_(dev, 0, read_length,
			test_id, &cmd_status, &act_length, &dad->ic_buf[6]);
	if (rc < 0) {
		dev_err(dev, "%s: Error on get self test result r=%d\n",
				__func__, rc);
		goto resume_scan;
	}

	dad->ic_buf[2] = cmd_status;
	dad->ic_buf[3] = test_id;
	dad->ic_buf[4] = LOW_BYTE(act_length);
	dad->ic_buf[5] = HI_BYTE(act_length);

	length = 6 + act_length;

status_success:
	status = STATUS_SUCCESS;

resume_scan:
	cyttsp5_resume_scan_cmd_(dev);

release_exclusive:
	cmd->release_exclusive(dev);

put_pm_runtime:
	pm_runtime_put(dev);

	if (status == STATUS_FAIL)
		length = 0;

	size = prepare_print_buffer(status, dad->ic_buf, length, buf, buf_len);

	mutex_unlock(&dad->sysfs_lock);

	return size;
}

struct cyttsp5_device_access_debugfs_data {
	struct cyttsp5_device_access_data *dad;
	ssize_t pr_buf_len;
	u8 pr_buf[3 * CY_MAX_PRBUF_SIZE];
};

static int cyttsp5_device_access_debugfs_open(struct inode *inode,
		struct file *filp)
{
	struct cyttsp5_device_access_data *dad = inode->i_private;
	struct cyttsp5_device_access_debugfs_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->dad = dad;

	filp->private_data = data;

	return nonseekable_open(inode, filp);
}

static int cyttsp5_device_access_debugfs_release(struct inode *inode,
		struct file *filp)
{
	kfree(filp->private_data);

	return 0;
}

#define CY_DEBUGFS_FOPS(_name, _read, _write) \
static const struct file_operations _name##_debugfs_fops = { \
	.open = cyttsp5_device_access_debugfs_open, \
	.release = cyttsp5_device_access_debugfs_release, \
	.read = _read, \
	.write = _write, \
}

static ssize_t panel_scan_debugfs_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_debugfs_data *data = filp->private_data;
	struct cyttsp5_device_access_data *dad = data->dad;
	struct device *dev = dad->dev;
	struct cyttsp5_core_data *cd = dev_get_drvdata(dev);
	int status = STATUS_FAIL;
	u8 config;
	u16 actual_read_len;
	int length = 0;
	u8 element_size = 0;
	u8 *buf_offset;
	int elem_offset = 0;
	int rc;

	if (*ppos)
		goto exit;


	mutex_lock(&dad->sysfs_lock);

	pm_runtime_get_sync(dev);

	rc = cmd->request_exclusive(dev, CY_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request exclusive r=%d\n",
				__func__, rc);
		goto put_pm_runtime;
	}

	rc = cyttsp5_suspend_scan_cmd_(dev);
	if (rc < 0) {
		dev_err(dev, "%s: Error on suspend scan r=%d\n",
				__func__, rc);
		goto release_exclusive;
	}

	rc = cyttsp5_exec_scan_cmd_(dev);
	if (rc < 0) {
		dev_err(dev, "%s: Error on execute panel scan r=%d\n",
				__func__, rc);
		goto resume_scan;
	}

	/* Set length to max to read all */
	rc = cyttsp5_ret_scan_data_cmd_(dev, 0, 0xFFFF,
			dad->panel_scan_data_id, dad->ic_buf, &config,
			&actual_read_len, NULL);
	if (rc < 0) {
		dev_err(dev, "%s: Error on retrieve panel scan r=%d\n",
				__func__, rc);
		goto resume_scan;
	}

	length = get_unaligned_le16(&dad->ic_buf[0]);
	buf_offset = dad->ic_buf + length;
	element_size = config & 0x07;
	elem_offset = actual_read_len;
	while (actual_read_len > 0) {
		rc = cyttsp5_ret_scan_data_cmd_(dev, elem_offset, 0xFFFF,
				dad->panel_scan_data_id, NULL, &config,
				&actual_read_len, buf_offset);
		if (rc < 0)
			goto resume_scan;

		length += actual_read_len * element_size;
		buf_offset = dad->ic_buf + length;
		elem_offset += actual_read_len;
	}
	/* Reconstruct cmd header */
	put_unaligned_le16(length, &dad->ic_buf[0]);
	put_unaligned_le16(elem_offset, &dad->ic_buf[7]);

	/* Do not print command header */
	length -= 5;

	status = STATUS_SUCCESS;

resume_scan:
	cyttsp5_resume_scan_cmd_(dev);

release_exclusive:
	cmd->release_exclusive(dev);

put_pm_runtime:
	pm_runtime_put(dev);

	if (status == STATUS_FAIL)
		length = 0;
	if (cd->show_timestamp) {
		int index = 0;

		index += scnprintf(data->pr_buf, sizeof(data->pr_buf),
			"[%u] ", jiffies_to_msecs(jiffies));
		data->pr_buf_len = prepare_print_buffer(status,
			&dad->ic_buf[5], length, &data->pr_buf[index],
			sizeof(data->pr_buf)-index);
	} else {
		data->pr_buf_len = prepare_print_buffer(status,
			&dad->ic_buf[5], length, data->pr_buf,
			sizeof(data->pr_buf));
	}

	mutex_unlock(&dad->sysfs_lock);

exit:
	return simple_read_from_buffer(buf, count, ppos, data->pr_buf,
			data->pr_buf_len);
}

static ssize_t panel_scan_debugfs_write(struct file *filp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_debugfs_data *data = filp->private_data;
	struct cyttsp5_device_access_data *dad = data->dad;
	ssize_t length;
	int rc = 0;

	rc = simple_write_to_buffer(data->pr_buf, sizeof(data->pr_buf), ppos,
			buf, count);
	if (rc < 0)
		return rc;

	count = rc;

	mutex_lock(&dad->sysfs_lock);

	length = cyttsp5_ic_parse_input(dad->dev, data->pr_buf, count,
			dad->ic_buf, CY_MAX_PRBUF_SIZE);

	if (length != 1) {
		dev_err(dad->dev, "%s: Malformed input\n", __func__);
		rc = -EINVAL;
		goto exit_unlock;
	}

	dad->panel_scan_data_id = dad->ic_buf[0];

exit_unlock:
	mutex_unlock(&dad->sysfs_lock);

	if (rc)
		return rc;

	return count;
}

CY_DEBUGFS_FOPS(panel_scan, panel_scan_debugfs_read, panel_scan_debugfs_write);

static ssize_t get_idac_debugfs_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_debugfs_data *data = filp->private_data;
	struct cyttsp5_device_access_data *dad = data->dad;
	struct device *dev = dad->dev;
	int status = STATUS_FAIL;
	u8 cmd_status = 0;
	u8 data_format = 0;
	u16 act_length = 0;
	int length = 0;
	int rc;

	if (*ppos)
		goto exit;

	mutex_lock(&dad->sysfs_lock);

	pm_runtime_get_sync(dev);

	rc = cmd->request_exclusive(dev, CY_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request exclusive r=%d\n",
				__func__, rc);
		goto put_pm_runtime;
	}

	rc = cyttsp5_suspend_scan_cmd_(dev);
	if (rc < 0) {
		dev_err(dev, "%s: Error on suspend scan r=%d\n",
				__func__, rc);
		goto release_exclusive;
	}

	rc = cyttsp5_get_data_structure_cmd_(dev, 0, PIP_CMD_MAX_LENGTH,
			dad->get_idac_data_id, &cmd_status, &data_format,
			&act_length, &dad->ic_buf[5]);
	if (rc < 0) {
		dev_err(dev, "%s: Error on get data structure r=%d\n",
				__func__, rc);
		goto resume_scan;
	}

	dad->ic_buf[0] = cmd_status;
	dad->ic_buf[1] = dad->get_idac_data_id;
	dad->ic_buf[2] = LOW_BYTE(act_length);
	dad->ic_buf[3] = HI_BYTE(act_length);
	dad->ic_buf[4] = data_format;

	length = 5 + act_length;

	status = STATUS_SUCCESS;

resume_scan:
	cyttsp5_resume_scan_cmd_(dev);

release_exclusive:
	cmd->release_exclusive(dev);

put_pm_runtime:
	pm_runtime_put(dev);

	if (status == STATUS_FAIL)
		length = 0;

	data->pr_buf_len = prepare_print_buffer(status, dad->ic_buf, length,
			data->pr_buf, sizeof(data->pr_buf));

	mutex_unlock(&dad->sysfs_lock);

exit:
	return simple_read_from_buffer(buf, count, ppos, data->pr_buf,
			data->pr_buf_len);
}

static ssize_t get_idac_debugfs_write(struct file *filp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_debugfs_data *data = filp->private_data;
	struct cyttsp5_device_access_data *dad = data->dad;
	ssize_t length;
	int rc = 0;

	rc = simple_write_to_buffer(data->pr_buf, sizeof(data->pr_buf), ppos,
			buf, count);
	if (rc < 0)
		return rc;

	count = rc;

	mutex_lock(&dad->sysfs_lock);

	length = cyttsp5_ic_parse_input(dad->dev, data->pr_buf, count,
			dad->ic_buf, CY_MAX_PRBUF_SIZE);
	if (length != 1) {
		dev_err(dad->dev, "%s: Malformed input\n", __func__);
		rc = -EINVAL;
		goto exit_unlock;
	}

	dad->get_idac_data_id = dad->ic_buf[0];

exit_unlock:
	mutex_unlock(&dad->sysfs_lock);

	if (rc)
		return rc;

	return count;
}

CY_DEBUGFS_FOPS(get_idac, get_idac_debugfs_read, get_idac_debugfs_write);

static ssize_t calibrate_debugfs_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_debugfs_data *data = filp->private_data;
	struct cyttsp5_device_access_data *dad = data->dad;
	struct device *dev = dad->dev;
	int status = STATUS_FAIL;
	int length = 0;
	int rc;

	if (*ppos)
		goto exit;

	mutex_lock(&dad->sysfs_lock);

	pm_runtime_get_sync(dev);

	rc = cmd->request_exclusive(dev, CY_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request exclusive r=%d\n",
				__func__, rc);
		goto put_pm_runtime;
	}

	rc = cyttsp5_suspend_scan_cmd_(dev);
	if (rc < 0) {
		dev_err(dev, "%s: Error on suspend scan r=%d\n",
				__func__, rc);
		goto release_exclusive;
	}

	rc = _cyttsp5_calibrate_idacs_cmd(dev, dad->calibrate_sensing_mode,
			&dad->ic_buf[0]);
	if (rc < 0) {
		dev_err(dev, "%s: Error on calibrate idacs r=%d\n",
				__func__, rc);
		goto resume_scan;
	}

	length = 1;

	/* Check if baseline initialization is requested */
	if (dad->calibrate_initialize_baselines) {
		/* Perform baseline initialization for all modes */
		rc = _cyttsp5_initialize_baselines_cmd(dev, CY_IB_SM_MUTCAP |
				CY_IB_SM_SELFCAP | CY_IB_SM_BUTTON,
				&dad->ic_buf[length]);
		if (rc < 0) {
			dev_err(dev, "%s: Error on initialize baselines r=%d\n",
					__func__, rc);
			goto resume_scan;
		}

		length++;
	}

	status = STATUS_SUCCESS;

resume_scan:
	cyttsp5_resume_scan_cmd_(dev);

release_exclusive:
	cmd->release_exclusive(dev);

put_pm_runtime:
	pm_runtime_put(dev);

	if (status == STATUS_FAIL)
		length = 0;

	data->pr_buf_len = prepare_print_buffer(status, dad->ic_buf, length,
			data->pr_buf, sizeof(data->pr_buf));

	mutex_unlock(&dad->sysfs_lock);

exit:
	return simple_read_from_buffer(buf, count, ppos, data->pr_buf,
			data->pr_buf_len);
}

static ssize_t calibrate_debugfs_write(struct file *filp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_debugfs_data *data = filp->private_data;
	struct cyttsp5_device_access_data *dad = data->dad;
	ssize_t length;
	int rc = 0;

	rc = simple_write_to_buffer(data->pr_buf, sizeof(data->pr_buf), ppos,
			buf, count);
	if (rc < 0)
		return rc;

	count = rc;

	mutex_lock(&dad->sysfs_lock);

	length = cyttsp5_ic_parse_input(dad->dev, data->pr_buf, count,
			dad->ic_buf, CY_MAX_PRBUF_SIZE);
	if (length != 2) {
		dev_err(dad->dev, "%s: Malformed input\n", __func__);
		rc = -EINVAL;
		goto exit_unlock;
	}

	dad->calibrate_sensing_mode = dad->ic_buf[0];
	dad->calibrate_initialize_baselines = dad->ic_buf[1];

exit_unlock:
	mutex_unlock(&dad->sysfs_lock);

	if (rc)
		return rc;

	return count;
}

CY_DEBUGFS_FOPS(calibrate, calibrate_debugfs_read, calibrate_debugfs_write);

static ssize_t baseline_debugfs_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_debugfs_data *data = filp->private_data;
	struct cyttsp5_device_access_data *dad = data->dad;
	struct device *dev = dad->dev;
	int status = STATUS_FAIL;
	int length = 0;
	int rc;

	if (*ppos)
		goto exit;

	mutex_lock(&dad->sysfs_lock);

	pm_runtime_get_sync(dev);

	rc = cmd->request_exclusive(dev, CY_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0) {
		dev_err(dev, "%s: Error on request exclusive r=%d\n",
				__func__, rc);
		goto put_pm_runtime;
	}

	rc = cyttsp5_suspend_scan_cmd_(dev);
	if (rc < 0) {
		dev_err(dev, "%s: Error on suspend scan r=%d\n",
				__func__, rc);
		goto release_exclusive;
	}

	rc = _cyttsp5_initialize_baselines_cmd(dev, dad->baseline_sensing_mode,
			&dad->ic_buf[0]);
	if (rc < 0) {
		dev_err(dev, "%s: Error on initialize baselines r=%d\n",
				__func__, rc);
		goto resume_scan;
	}

	length = 1;

	status = STATUS_SUCCESS;

resume_scan:
	cyttsp5_resume_scan_cmd_(dev);

release_exclusive:
	cmd->release_exclusive(dev);

put_pm_runtime:
	pm_runtime_put(dev);

	if (status == STATUS_FAIL)
		length = 0;

	data->pr_buf_len = prepare_print_buffer(status, dad->ic_buf, length,
			data->pr_buf, sizeof(data->pr_buf));

	mutex_unlock(&dad->sysfs_lock);

exit:
	return simple_read_from_buffer(buf, count, ppos, data->pr_buf,
			data->pr_buf_len);
}

static ssize_t baseline_debugfs_write(struct file *filp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_debugfs_data *data = filp->private_data;
	struct cyttsp5_device_access_data *dad = data->dad;
	ssize_t length;
	int rc = 0;

	rc = simple_write_to_buffer(data->pr_buf, sizeof(data->pr_buf), ppos,
			buf, count);
	if (rc < 0)
		return rc;

	count = rc;

	mutex_lock(&dad->sysfs_lock);

	length = cyttsp5_ic_parse_input(dad->dev, buf, count, dad->ic_buf,
			CY_MAX_PRBUF_SIZE);
	if (length != 1) {
		dev_err(dad->dev, "%s: Malformed input\n", __func__);
		rc = -EINVAL;
		goto exit_unlock;
	}

	dad->baseline_sensing_mode = dad->ic_buf[0];

exit_unlock:
	mutex_unlock(&dad->sysfs_lock);

	if (rc)
		return rc;

	return count;
}

CY_DEBUGFS_FOPS(baseline, baseline_debugfs_read, baseline_debugfs_write);

static ssize_t auto_shorts_debugfs_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_debugfs_data *data = filp->private_data;

	if (!*ppos)
		/* Set length to PIP_CMD_MAX_LENGTH to read all */
		data->pr_buf_len = cyttsp5_run_and_get_selftest_result(
			data->dad->dev, data->pr_buf, sizeof(data->pr_buf),
			CY_ST_ID_AUTOSHORTS, PIP_CMD_MAX_LENGTH, false);

	return simple_read_from_buffer(buf, count, ppos, data->pr_buf,
			data->pr_buf_len);
}

CY_DEBUGFS_FOPS(auto_shorts, auto_shorts_debugfs_read, NULL);

static ssize_t opens_debugfs_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_debugfs_data *data = filp->private_data;

	if (!*ppos)
		/* Set length to PIP_CMD_MAX_LENGTH to read all */
		data->pr_buf_len = cyttsp5_run_and_get_selftest_result(
			data->dad->dev, data->pr_buf, sizeof(data->pr_buf),
			CY_ST_ID_OPENS, PIP_CMD_MAX_LENGTH, false);

	return simple_read_from_buffer(buf, count, ppos, data->pr_buf,
			data->pr_buf_len);
}

CY_DEBUGFS_FOPS(opens, opens_debugfs_read, NULL);

static ssize_t cm_panel_debugfs_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_debugfs_data *data = filp->private_data;

	if (!*ppos)
		/* Set length to PIP_CMD_MAX_LENGTH to read all */
		data->pr_buf_len = cyttsp5_run_and_get_selftest_result(
			data->dad->dev, data->pr_buf, sizeof(data->pr_buf),
			CY_ST_ID_CM_PANEL, PIP_CMD_MAX_LENGTH, true);

	return simple_read_from_buffer(buf, count, ppos, data->pr_buf,
			data->pr_buf_len);
}

CY_DEBUGFS_FOPS(cm_panel, cm_panel_debugfs_read, NULL);

static ssize_t cp_panel_debugfs_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_debugfs_data *data = filp->private_data;

	if (!*ppos)
		/* Set length to PIP_CMD_MAX_LENGTH to read all */
		data->pr_buf_len = cyttsp5_run_and_get_selftest_result(
			data->dad->dev, data->pr_buf, sizeof(data->pr_buf),
			CY_ST_ID_CP_PANEL, PIP_CMD_MAX_LENGTH, true);

	return simple_read_from_buffer(buf, count, ppos, data->pr_buf,
			data->pr_buf_len);
}

CY_DEBUGFS_FOPS(cp_panel, cp_panel_debugfs_read, NULL);

static ssize_t cm_button_debugfs_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_debugfs_data *data = filp->private_data;

	if (!*ppos)
		/* Set length to PIP_CMD_MAX_LENGTH to read all */
		data->pr_buf_len = cyttsp5_run_and_get_selftest_result(
			data->dad->dev, data->pr_buf, sizeof(data->pr_buf),
			CY_ST_ID_CM_BUTTON, PIP_CMD_MAX_LENGTH, true);

	return simple_read_from_buffer(buf, count, ppos, data->pr_buf,
			data->pr_buf_len);
}

CY_DEBUGFS_FOPS(cm_button, cm_button_debugfs_read, NULL);

static ssize_t cp_button_debugfs_read(struct file *filp, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_debugfs_data *data = filp->private_data;

	if (!*ppos)
		/* Set length to PIP_CMD_MAX_LENGTH to read all */
		data->pr_buf_len = cyttsp5_run_and_get_selftest_result(
			data->dad->dev, data->pr_buf, sizeof(data->pr_buf),
			CY_ST_ID_CP_BUTTON, PIP_CMD_MAX_LENGTH, true);

	return simple_read_from_buffer(buf, count, ppos, data->pr_buf,
			data->pr_buf_len);
}

CY_DEBUGFS_FOPS(cp_button, cp_button_debugfs_read, NULL);

#ifdef TTHE_TUNER_SUPPORT
static ssize_t tthe_get_panel_data_debugfs_read(struct file *filp,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_data *dad = filp->private_data;
	struct device *dev;
	struct cyttsp5_core_data *cd;
	u8 config;
	u16 actual_read_len;
	u16 length = 0;
	u8 element_size = 0;
	u8 *buf_offset;
	u8 *buf_out;
	int elem;
	int elem_offset = 0;
	int print_idx = 0;
	int rc;
	int rc1;
	int i;

	mutex_lock(&dad->debugfs_lock);
	dev = dad->dev;
	cd = dev_get_drvdata(dev);
	buf_out = dad->tthe_get_panel_data_buf;
	if (!buf_out)
		goto release_mutex;

	pm_runtime_get_sync(dev);

	rc = cmd->request_exclusive(dev, CY_REQUEST_EXCLUSIVE_TIMEOUT);
	if (rc < 0)
		goto put_runtime;

	if (dad->heatmap.scan_start) {
		/*
		 * To fix CDT206291: avoid multiple scans when
		 * return data is larger than 4096 bytes in one cycle
		 */
		dad->heatmap.scan_start = 0;

		/* Start scan */
		rc = cyttsp5_exec_scan_cmd_(dev);
		if (rc < 0)
			goto release_exclusive;
	}

	elem = dad->heatmap.num_element;

#if defined(CY_ENABLE_MAX_ELEN)
	if (elem > CY_MAX_ELEN) {
		rc = cyttsp5_ret_scan_data_cmd_(dev, elem_offset,
		CY_MAX_ELEN, dad->heatmap.data_type, dad->ic_buf,
		&config, &actual_read_len, NULL);
	} else{
		rc = cyttsp5_ret_scan_data_cmd_(dev, elem_offset, elem,
			dad->heatmap.data_type, dad->ic_buf, &config,
			&actual_read_len, NULL);
	}
#else
	rc = cyttsp5_ret_scan_data_cmd_(dev, elem_offset, elem,
			dad->heatmap.data_type, dad->ic_buf, &config,
			&actual_read_len, NULL);
#endif
	if (rc < 0)
		goto release_exclusive;

	length = get_unaligned_le16(&dad->ic_buf[0]);
	buf_offset = dad->ic_buf + length;

	element_size = config & CY_CMD_RET_PANEL_ELMNT_SZ_MASK;

	elem -= actual_read_len;
	elem_offset = actual_read_len;
	while (elem > 0) {
#ifdef CY_ENABLE_MAX_ELEN
		if (elem > CY_MAX_ELEN) {
			rc = cyttsp5_ret_scan_data_cmd_(dev, elem_offset,
			CY_MAX_ELEN, dad->heatmap.data_type, NULL, &config,
			&actual_read_len, buf_offset);
		} else{
			rc = cyttsp5_ret_scan_data_cmd_(dev, elem_offset, elem,
				dad->heatmap.data_type, NULL, &config,
				&actual_read_len, buf_offset);
		}
#else

		rc = cyttsp5_ret_scan_data_cmd_(dev, elem_offset, elem,
				dad->heatmap.data_type, NULL, &config,
				&actual_read_len, buf_offset);
#endif
		if (rc < 0)
			goto release_exclusive;

		if (!actual_read_len)
			break;

		length += actual_read_len * element_size;
		buf_offset = dad->ic_buf + length;
		elem -= actual_read_len;
		elem_offset += actual_read_len;
	}

	/* Reconstruct cmd header */
	put_unaligned_le16(length, &dad->ic_buf[0]);
	put_unaligned_le16(elem_offset, &dad->ic_buf[7]);

release_exclusive:
	rc1 = cmd->release_exclusive(dev);
put_runtime:
	pm_runtime_put(dev);

	if (rc < 0)
		goto release_mutex;
	if (cd->show_timestamp)
		print_idx += scnprintf(buf_out, TTHE_TUNER_MAX_BUF,
			"[%u] CY_DATA:", jiffies_to_msecs(jiffies));
	else
		print_idx += scnprintf(buf_out, TTHE_TUNER_MAX_BUF,
			"CY_DATA:");
	for (i = 0; i < length; i++)
		print_idx += scnprintf(buf_out + print_idx,
				TTHE_TUNER_MAX_BUF - print_idx,
				"%02X ", dad->ic_buf[i]);
	print_idx += scnprintf(buf_out + print_idx,
			TTHE_TUNER_MAX_BUF - print_idx,
			":(%d bytes)\n", length);
	rc = simple_read_from_buffer(buf, count, ppos, buf_out, print_idx);
	print_idx = rc;

release_mutex:
	mutex_unlock(&dad->debugfs_lock);
	return print_idx;
}

static ssize_t tthe_get_panel_data_debugfs_write(struct file *filp,
		const char __user *buf, size_t count, loff_t *ppos)
{
	struct cyttsp5_device_access_data *dad = filp->private_data;
	struct device *dev = dad->dev;
	ssize_t length;
	int max_read;
	u8 *buf_in = dad->tthe_get_panel_data_buf;
	int ret;

	mutex_lock(&dad->debugfs_lock);
	ret = copy_from_user(buf_in + (*ppos), buf, count);
	if (ret)
		goto exit;
	buf_in[count] = 0;

	length = cyttsp5_ic_parse_input(dev, buf_in, count, dad->ic_buf,
			CY_MAX_PRBUF_SIZE);
	if (length <= 0) {
		dev_err(dev, "%s: %s Group Data store\n", __func__,
				"Malformed input for");
		goto exit;
	}

	/* update parameter value */
	dad->heatmap.num_element = get_unaligned_le16(&dad->ic_buf[3]);
	dad->heatmap.data_type = dad->ic_buf[5];

	if (dad->ic_buf[6] > 0)
		dad->heatmap.scan_start = true;
	else
		dad->heatmap.scan_start = false;

	/* elem can not be bigger then buffer size */
	max_read = CY_CMD_RET_PANEL_HDR;
	max_read += dad->heatmap.num_element * CY_CMD_RET_PANEL_ELMNT_SZ_MAX;

	if (max_read >= CY_MAX_PRBUF_SIZE) {
		dad->heatmap.num_element =
			(CY_MAX_PRBUF_SIZE - CY_CMD_RET_PANEL_HDR)
			/ CY_CMD_RET_PANEL_ELMNT_SZ_MAX;
		parade_debug(dev, DEBUG_LEVEL_2, "%s: Will get %d element\n",
			__func__, dad->heatmap.num_element);
	}

exit:
	mutex_unlock(&dad->debugfs_lock);
	parade_debug(dev, DEBUG_LEVEL_2, "%s: return count=%zu\n",
		__func__, count);
	return count;
}

static int tthe_get_panel_data_debugfs_open(struct inode *inode,
		struct file *filp)
{
	struct cyttsp5_device_access_data *dad = inode->i_private;

	mutex_lock(&dad->debugfs_lock);

	if (dad->tthe_get_panel_data_is_open) {
		mutex_unlock(&dad->debugfs_lock);
		return -EBUSY;
	}

	filp->private_data = inode->i_private;

	dad->tthe_get_panel_data_is_open = 1;
	mutex_unlock(&dad->debugfs_lock);
	return 0;
}

static int tthe_get_panel_data_debugfs_close(struct inode *inode,
		struct file *filp)
{
	struct cyttsp5_device_access_data *dad = filp->private_data;

	mutex_lock(&dad->debugfs_lock);
	filp->private_data = NULL;
	dad->tthe_get_panel_data_is_open = 0;
	mutex_unlock(&dad->debugfs_lock);

	return 0;
}

static const struct file_operations tthe_get_panel_data_fops = {
	.open = tthe_get_panel_data_debugfs_open,
	.release = tthe_get_panel_data_debugfs_close,
	.read = tthe_get_panel_data_debugfs_read,
	.write = tthe_get_panel_data_debugfs_write,
};
#endif

static int cyttsp5_setup_sysfs(struct device *dev)
{
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);
	int rc;

	rc = device_create_file(dev, &dev_attr_command);
	if (rc) {
		dev_err(dev, "%s: Error, could not create command\n",
				__func__);
		goto exit;
	}

	rc = device_create_file(dev, &dev_attr_status);
	if (rc) {
		dev_err(dev, "%s: Error, could not create status\n",
				__func__);
		goto unregister_command;
	}

	rc = device_create_file(dev, &dev_attr_response);
	if (rc) {
		dev_err(dev, "%s: Error, could not create response\n",
				__func__);
		goto unregister_status;
	}

	dad->base_dentry = debugfs_create_dir(dev_name(dev), NULL);
	if (IS_ERR_OR_NULL(dad->base_dentry)) {
		dev_err(dev, "%s: Error, could not create base directory\n",
				__func__);
		goto unregister_response;
	}

	dad->mfg_test_dentry = debugfs_create_dir("mfg_test",
			dad->base_dentry);
	if (IS_ERR_OR_NULL(dad->mfg_test_dentry)) {
		dev_err(dev, "%s: Error, could not create mfg_test directory\n",
				__func__);
		goto unregister_base_dir;
	}

	if (IS_ERR_OR_NULL(debugfs_create_file("panel_scan", 0600,
			dad->mfg_test_dentry, dad,
			&panel_scan_debugfs_fops))) {
		dev_err(dev, "%s: Error, could not create panel_scan\n",
				__func__);
		goto unregister_base_dir;
	}

	if (IS_ERR_OR_NULL(debugfs_create_file("get_idac", 0600,
			dad->mfg_test_dentry, dad, &get_idac_debugfs_fops))) {
		dev_err(dev, "%s: Error, could not create get_idac\n",
				__func__);
		goto unregister_base_dir;
	}

	if (IS_ERR_OR_NULL(debugfs_create_file("auto_shorts", 0400,
			dad->mfg_test_dentry, dad,
			&auto_shorts_debugfs_fops))) {
		dev_err(dev, "%s: Error, could not create auto_shorts\n",
				__func__);
		goto unregister_base_dir;
	}

	if (IS_ERR_OR_NULL(debugfs_create_file("opens", 0400,
			dad->mfg_test_dentry, dad, &opens_debugfs_fops))) {
		dev_err(dev, "%s: Error, could not create opens\n",
				__func__);
		goto unregister_base_dir;
	}

	if (IS_ERR_OR_NULL(debugfs_create_file("calibrate", 0600,
			dad->mfg_test_dentry, dad, &calibrate_debugfs_fops))) {
		dev_err(dev, "%s: Error, could not create calibrate\n",
				__func__);
		goto unregister_base_dir;
	}

	if (IS_ERR_OR_NULL(debugfs_create_file("baseline", 0600,
			dad->mfg_test_dentry, dad, &baseline_debugfs_fops))) {
		dev_err(dev, "%s: Error, could not create baseline\n",
				__func__);
		goto unregister_base_dir;
	}

	if (IS_ERR_OR_NULL(debugfs_create_file("cm_panel", 0400,
			dad->mfg_test_dentry, dad, &cm_panel_debugfs_fops))) {
		dev_err(dev, "%s: Error, could not create cm_panel\n",
				__func__);
		goto unregister_base_dir;
	}

	if (IS_ERR_OR_NULL(debugfs_create_file("cp_panel", 0400,
			dad->mfg_test_dentry, dad, &cp_panel_debugfs_fops))) {
		dev_err(dev, "%s: Error, could not create cp_panel\n",
				__func__);
		goto unregister_base_dir;
	}

	if (IS_ERR_OR_NULL(debugfs_create_file("cm_button", 0400,
			dad->mfg_test_dentry, dad, &cm_button_debugfs_fops))) {
		dev_err(dev, "%s: Error, could not create cm_button\n",
				__func__);
		goto unregister_base_dir;
	}

	if (IS_ERR_OR_NULL(debugfs_create_file("cp_button", 0400,
			dad->mfg_test_dentry, dad, &cp_button_debugfs_fops))) {
		dev_err(dev, "%s: Error, could not create cp_button\n",
				__func__);
		goto unregister_base_dir;
	}

	dad->cmcp_results_debugfs = debugfs_create_file("cmcp_results", 0644,
		dad->mfg_test_dentry, dad, &cmcp_results_debugfs_fops);
	if (IS_ERR_OR_NULL(dad->cmcp_results_debugfs)) {
		dev_err(dev, "%s: Error, could not create cmcp_results\n",
				__func__);
		dad->cmcp_results_debugfs = NULL;
		goto unregister_base_dir;
	}

#ifdef TTHE_TUNER_SUPPORT
	dad->tthe_get_panel_data_debugfs = debugfs_create_file(
			CYTTSP5_TTHE_TUNER_GET_PANEL_DATA_FILE_NAME,
			0644, NULL, dad, &tthe_get_panel_data_fops);
	if (IS_ERR_OR_NULL(dad->tthe_get_panel_data_debugfs)) {
		dev_err(dev, "%s: Error, could not create get_panel_data\n",
				__func__);
		dad->tthe_get_panel_data_debugfs = NULL;
		goto unregister_base_dir;
	}
#endif

	rc = device_create_file(dev, &dev_attr_cmcp_test);
	if (rc) {
		dev_err(dev, "%s: Error, could not create cmcp_test\n",
				__func__);
		goto unregister_base_dir;
	}

	rc = device_create_file(dev, &dev_attr_cmcp_threshold_loading);
	if (rc) {
		dev_err(dev, "%s: Error, could not create cmcp_thresold_loading\n",
				__func__);
		goto unregister_cmcp_test;
	}

	rc = device_create_bin_file(dev, &bin_attr_cmcp_threshold_data);
	if (rc) {
		dev_err(dev, "%s: Error, could not create cmcp_thresold_data\n",
				__func__);
		goto unregister_cmcp_thresold_loading;
	}

	dad->sysfs_nodes_created = true;
	return rc;

unregister_cmcp_thresold_loading:
	device_remove_file(dev, &dev_attr_cmcp_threshold_loading);
unregister_cmcp_test:
	device_remove_file(dev, &dev_attr_cmcp_test);
unregister_base_dir:
	debugfs_remove_recursive(dad->base_dentry);
unregister_response:
	device_remove_file(dev, &dev_attr_response);
unregister_status:
	device_remove_file(dev, &dev_attr_status);
unregister_command:
	device_remove_file(dev, &dev_attr_command);
exit:
	return rc;
}

static int cyttsp5_setup_sysfs_attention(struct device *dev)
{
	struct cyttsp5_device_access_data *dad
		= cyttsp5_get_device_access_data(dev);
	int rc = 0;

	dad->si = cmd->request_sysinfo(dev);
	if (!dad->si)
		return -EINVAL;

	rc = cyttsp5_setup_sysfs(dev);

	cmd->unsubscribe_attention(dev, CY_ATTEN_STARTUP,
		CYTTSP5_DEVICE_ACCESS_NAME, cyttsp5_setup_sysfs_attention,
		0);

	return rc;
}

#ifdef CONFIG_TOUCHSCREEN_CYPRESS_CYTTSP5_DEVICE_ACCESS_API
int cyttsp5_device_access_user_command(const char *core_name, u16 read_len,
		u8 *read_buf, u16 write_len, u8 *write_buf,
		u16 *actual_read_len)
{
	struct cyttsp5_core_data *cd;
	int rc;

	might_sleep();

	/* Check parameters */
	if (!read_buf || !write_buf || !actual_read_len)
		return -EINVAL;

	if (!core_name)
		core_name = CY_DEFAULT_CORE_ID;

	/* Find device */
	cd = cyttsp5_get_core_data((char *)core_name);
	if (!cd) {
		pr_err("%s: No device.\n", __func__);
		return -ENODEV;
	}

	pm_runtime_get_sync(cd->dev);
	rc = cmd->nonhid_cmd->user_cmd(cd->dev, 1, read_len, read_buf,
			write_len, write_buf, actual_read_len);
	pm_runtime_put(cd->dev);

	return rc;
}
EXPORT_SYMBOL_GPL(cyttsp5_device_access_user_command);

struct command_work {
	struct work_struct work;
	const char *core_name;
	u16 read_len;
	u8 *read_buf;
	u16 write_len;
	u8 *write_buf;

	void (*cont)(const char *core_name, u16 read_len, u8 *read_buf,
		u16 write_len, u8 *write_buf, u16 actual_read_length,
		int rc);
};

static void cyttsp5_device_access_user_command_work_func(
		struct work_struct *work)
{
	struct command_work *cmd_work =
			container_of(work, struct command_work, work);
	u16 actual_read_length;
	int rc;

	rc = cyttsp5_device_access_user_command(cmd_work->core_name,
			cmd_work->read_len, cmd_work->read_buf,
			cmd_work->write_len, cmd_work->write_buf,
			&actual_read_length);

	if (cmd_work->cont)
		cmd_work->cont(cmd_work->core_name,
			cmd_work->read_len, cmd_work->read_buf,
			cmd_work->write_len, cmd_work->write_buf,
			actual_read_length, rc);

	kfree(cmd_work);
}

int cyttsp5_device_access_user_command_async(const char *core_name,
		u16 read_len, u8 *read_buf, u16 write_len, u8 *write_buf,
		void (*cont)(const char *core_name, u16 read_len, u8 *read_buf,
			u16 write_len, u8 *write_buf, u16 actual_read_length,
			int rc))
{
	struct command_work *cmd_work;

	cmd_work = kzalloc(sizeof(*cmd_work), GFP_ATOMIC);
	if (!cmd_work)
		return -ENOMEM;

	cmd_work->core_name = core_name;
	cmd_work->read_len = read_len;
	cmd_work->read_buf = read_buf;
	cmd_work->write_len = write_len;
	cmd_work->write_buf = write_buf;
	cmd_work->cont = cont;

	INIT_WORK(&cmd_work->work,
			cyttsp5_device_access_user_command_work_func);
	schedule_work(&cmd_work->work);

	return 0;
}
EXPORT_SYMBOL_GPL(cyttsp5_device_access_user_command_async);
#endif

static void cyttsp5_cmcp_parse_threshold_file(const struct firmware *fw,
		void *context)
{
	struct device *dev = context;
	struct cyttsp5_device_access_data *dad =
		cyttsp5_get_device_access_data(dev);

	if (!fw) {
		dev_info(dev, "%s: No builtin cmcp threshold file\n", __func__);
		goto exit;
	}

	if (!fw->data || !fw->size) {
		dev_err(dev, "%s: Invalid builtin cmcp threshold file\n",
		__func__);
		goto exit;
	}

	parade_debug(dev, DEBUG_LEVEL_1, "%s: Found cmcp threshold file.\n",
		__func__);

	cyttsp5_parse_cmcp_threshold_file_common(dev, &fw->data[0], fw->size);

	dad->builtin_cmcp_threshold_status = 0;
	complete(&dad->builtin_cmcp_threshold_complete);
	return;

exit:
	release_firmware(fw);

	dad->builtin_cmcp_threshold_status = -EINVAL;
	complete(&dad->builtin_cmcp_threshold_complete);
}

static void cyttsp5_parse_cmcp_threshold_builtin(
	struct work_struct *cmcp_threshold_update)
{
	struct cyttsp5_device_access_data *dad =
		container_of(cmcp_threshold_update,
		struct cyttsp5_device_access_data,
		cmcp_threshold_update);
	struct device *dev = dad->dev;
	int retval;

	dad->si = cmd->request_sysinfo(dev);
	if (!dad->si) {
		dev_err(dev, "%s: Fail get sysinfo pointer from core\n",
			__func__);
		return;
	}

	parade_debug(dev, DEBUG_LEVEL_2,
		"%s: Enabling cmcp threshold class loader built-in\n",
		__func__);

	/* Open threshold file */
	retval = request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
			CMCP_THRESHOLD_FILE_NAME, dev, GFP_KERNEL, dev,
			cyttsp5_cmcp_parse_threshold_file);
	if (retval < 0) {
		dev_err(dev, "%s: Failed loading cmcp threshold file, attempting legacy file\n",
			__func__);
		/* Try legacy file name */
		retval = request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				CY_CMCP_THRESHOLD_FILE_NAME, dev, GFP_KERNEL,
				dev, cyttsp5_cmcp_parse_threshold_file);
		if (retval < 0) {
			dev_err(dev, "%s: Fail request cmcp threshold class file load\n",
				__func__);
			goto exit;
		}
	}

	/* wait until cmcp threshold upgrade finishes */
	wait_for_completion(&dad->builtin_cmcp_threshold_complete);

	retval = dad->builtin_cmcp_threshold_status;

exit:
	return;
}

static int cyttsp5_device_access_probe(struct device *dev, void **data)
{
	struct cyttsp5_device_access_data *dad;
	struct configuration *configurations;
	struct cmcp_data *cmcp_info;
	struct result *result;

	int tx_num = MAX_TX_SENSORS;
	int rx_num = MAX_RX_SENSORS;
	int btn_num = MAX_BUTTONS;

	struct test_case_field *test_case_field_array;
	struct test_case_search *test_case_search_array;
	int rc = 0;

	dad = kzalloc(sizeof(*dad), GFP_KERNEL);
	if (!dad) {
		rc = -ENOMEM;
		goto cyttsp5_device_access_probe_data_failed;
	}

	configurations =
		kzalloc(sizeof(*configurations), GFP_KERNEL);
	if (!configurations) {
		rc = -ENOMEM;
		goto cyttsp5_device_access_probe_configs_failed;
	}
	dad->configs = configurations;

	cmcp_info = kzalloc(sizeof(*cmcp_info), GFP_KERNEL);
	if (!cmcp_info) {
		rc = -ENOMEM;
		goto cyttsp5_device_access_probe_cmcp_info_failed;
	}
	dad->cmcp_info = cmcp_info;

	cmcp_info->tx_num = tx_num;
	cmcp_info->rx_num = rx_num;
	cmcp_info->btn_num = btn_num;

	result = kzalloc(sizeof(*result), GFP_KERNEL);
	if (!result) {
		rc = -ENOMEM;
		goto cyttsp5_device_access_probe_result_failed;
	}
	dad->result = result;

	test_case_field_array =
		kzalloc(sizeof(*test_case_field_array) * MAX_CASE_NUM,
		GFP_KERNEL);
	if (!test_case_field_array) {
		rc = -ENOMEM;
		goto cyttsp5_device_access_probe_field_array_failed;
	}

	test_case_search_array =
		kzalloc(sizeof(*test_case_search_array) * MAX_CASE_NUM,
		GFP_KERNEL);
	if (!test_case_search_array) {
		rc = -ENOMEM;
		goto cyttsp5_device_access_probe_search_array_failed;
	}

	cmcp_info->gd_sensor_col = (struct gd_sensor *)
		 kzalloc(tx_num * sizeof(struct gd_sensor), GFP_KERNEL);
	if (cmcp_info->gd_sensor_col == NULL)
		goto cyttsp5_device_access_probe_gd_sensor_col_failed;

	cmcp_info->gd_sensor_row = (struct gd_sensor *)
		 kzalloc(rx_num * sizeof(struct gd_sensor), GFP_KERNEL);
	if (cmcp_info->gd_sensor_row == NULL)
		goto cyttsp5_device_access_probe_gd_sensor_row_failed;

	cmcp_info->cm_data_panel =
		 kzalloc((tx_num * rx_num + 1) * sizeof(int32_t), GFP_KERNEL);
	if (cmcp_info->cm_data_panel == NULL)
		goto cyttsp5_device_access_probe_cm_data_panel_failed;

	cmcp_info->cp_tx_data_panel =
		kzalloc(tx_num * sizeof(int32_t), GFP_KERNEL);
	if (cmcp_info->cp_tx_data_panel == NULL)
		goto cyttsp5_device_access_probe_cp_tx_data_panel_failed;

	cmcp_info->cp_tx_cal_data_panel =
		kzalloc(tx_num * sizeof(int32_t), GFP_KERNEL);
	if (cmcp_info->cp_tx_cal_data_panel == NULL)
		goto cyttsp5_device_access_probe_cp_tx_cal_data_panel_failed;

	cmcp_info->cp_rx_data_panel =
		 kzalloc(rx_num * sizeof(int32_t), GFP_KERNEL);
	if (cmcp_info->cp_rx_data_panel == NULL)
		goto cyttsp5_device_access_probe_cp_rx_data_panel_failed;

	cmcp_info->cp_rx_cal_data_panel =
		 kzalloc(rx_num * sizeof(int32_t), GFP_KERNEL);
	if (cmcp_info->cp_rx_cal_data_panel == NULL)
		goto cyttsp5_device_access_probe_cp_rx_cal_data_panel_failed;

	cmcp_info->cm_btn_data = kcalloc(btn_num, sizeof(int32_t), GFP_KERNEL);
	if (cmcp_info->cm_btn_data == NULL)
		goto cyttsp5_device_access_probe_cm_btn_data_failed;

	cmcp_info->cp_btn_data = kcalloc(btn_num, sizeof(int32_t), GFP_KERNEL);
	if (cmcp_info->cp_btn_data == NULL)
		goto cyttsp5_device_access_probe_cp_btn_data_failed;

	cmcp_info->cm_sensor_column_delta =
		 kzalloc(rx_num * tx_num * sizeof(int32_t), GFP_KERNEL);
	if (cmcp_info->cm_sensor_column_delta == NULL)
		goto cyttsp5_device_access_probe_cm_sensor_column_delta_failed;

	cmcp_info->cm_sensor_row_delta =
		 kzalloc(tx_num * rx_num  * sizeof(int32_t), GFP_KERNEL);
	if (cmcp_info->cm_sensor_row_delta == NULL)
		goto cyttsp5_device_access_probe_cm_sensor_row_delta_failed;

	mutex_init(&dad->sysfs_lock);
	mutex_init(&dad->cmcp_threshold_lock);
	dad->dev = dev;
#ifdef TTHE_TUNER_SUPPORT
	mutex_init(&dad->debugfs_lock);
	dad->heatmap.num_element = 200;
#endif
	*data = dad;

	dad->test_field_array = test_case_field_array;
	dad->test_search_array = test_case_search_array;
	dad->test_executed = 0;

	init_completion(&dad->builtin_cmcp_threshold_complete);

	/* get sysinfo */
	dad->si = cmd->request_sysinfo(dev);
	if (dad->si) {
		rc = cyttsp5_setup_sysfs(dev);
		if (rc)
			goto cyttsp5_device_access_setup_sysfs_failed;
	} else {
		dev_err(dev, "%s: Fail get sysinfo pointer from core p=%p\n",
				__func__, dad->si);
		cmd->subscribe_attention(dev, CY_ATTEN_STARTUP,
			CYTTSP5_DEVICE_ACCESS_NAME,
			cyttsp5_setup_sysfs_attention, 0);
	}

	INIT_WORK(&dad->cmcp_threshold_update,
		cyttsp5_parse_cmcp_threshold_builtin);
	schedule_work(&dad->cmcp_threshold_update);

	return 0;

cyttsp5_device_access_setup_sysfs_failed:
	kfree(cmcp_info->cm_sensor_row_delta);
cyttsp5_device_access_probe_cm_sensor_row_delta_failed:
	kfree(cmcp_info->cm_sensor_column_delta);
cyttsp5_device_access_probe_cm_sensor_column_delta_failed:
	kfree(cmcp_info->cp_btn_data);
cyttsp5_device_access_probe_cp_btn_data_failed:
	kfree(cmcp_info->cm_btn_data);
cyttsp5_device_access_probe_cm_btn_data_failed:
	kfree(cmcp_info->cp_rx_cal_data_panel);
cyttsp5_device_access_probe_cp_rx_cal_data_panel_failed:
	kfree(cmcp_info->cp_rx_data_panel);
cyttsp5_device_access_probe_cp_rx_data_panel_failed:
	kfree(cmcp_info->cp_tx_cal_data_panel);
cyttsp5_device_access_probe_cp_tx_cal_data_panel_failed:
	kfree(cmcp_info->cp_tx_data_panel);
cyttsp5_device_access_probe_cp_tx_data_panel_failed:
	kfree(cmcp_info->cm_data_panel);
cyttsp5_device_access_probe_cm_data_panel_failed:
	kfree(cmcp_info->gd_sensor_row);
cyttsp5_device_access_probe_gd_sensor_row_failed:
	kfree(cmcp_info->gd_sensor_col);
cyttsp5_device_access_probe_gd_sensor_col_failed:
	kfree(test_case_search_array);
cyttsp5_device_access_probe_search_array_failed:
	kfree(test_case_field_array);
cyttsp5_device_access_probe_field_array_failed:
	kfree(result);
cyttsp5_device_access_probe_result_failed:
	kfree(cmcp_info);
cyttsp5_device_access_probe_cmcp_info_failed:
	kfree(configurations);
cyttsp5_device_access_probe_configs_failed:
	kfree(dad);
cyttsp5_device_access_probe_data_failed:
	dev_err(dev, "%s failed.\n", __func__);
	return rc;
}

static void cyttsp5_device_access_release(struct device *dev, void *data)
{
	struct cyttsp5_device_access_data *dad = data;

	if (dad->sysfs_nodes_created) {
		device_remove_file(dev, &dev_attr_command);
		device_remove_file(dev, &dev_attr_status);
		device_remove_file(dev, &dev_attr_response);
		debugfs_remove(dad->cmcp_results_debugfs);
		debugfs_remove_recursive(dad->base_dentry);
#ifdef TTHE_TUNER_SUPPORT
		debugfs_remove(dad->tthe_get_panel_data_debugfs);
#endif
		device_remove_file(dev, &dev_attr_cmcp_test);
		device_remove_file(dev, &dev_attr_cmcp_threshold_loading);
		device_remove_bin_file(dev, &bin_attr_cmcp_threshold_data);
		kfree(dad->cmcp_threshold_data);
	} else {
		cmd->unsubscribe_attention(dev, CY_ATTEN_STARTUP,
			CYTTSP5_DEVICE_ACCESS_NAME,
			cyttsp5_setup_sysfs_attention, 0);
	}

	kfree(dad->test_search_array);
	kfree(dad->test_field_array);
	kfree(dad->configs);
	cyttsp5_free_cmcp_buf(dad->cmcp_info);
	kfree(dad->cmcp_info);
	kfree(dad->result);
	kfree(dad);
}

static struct cyttsp5_module device_access_module = {
	.name = CYTTSP5_DEVICE_ACCESS_NAME,
	.probe = cyttsp5_device_access_probe,
	.release = cyttsp5_device_access_release,
};

static int __init cyttsp5_device_access_init(void)
{
	int rc;

	cmd = cyttsp5_get_commands();
	if (!cmd)
		return -EINVAL;

	rc = cyttsp5_register_module(&device_access_module);
	if (rc < 0) {
		pr_err("%s: Error, failed registering module\n",
			__func__);
			return rc;
	}

	pr_info("%s: Parade TTSP Device Access Driver (Built %s) rc=%d\n",
		 __func__, CY_DRIVER_VERSION, rc);
	return 0;
}
module_init(cyttsp5_device_access_init);

static void __exit cyttsp5_device_access_exit(void)
{
	cyttsp5_unregister_module(&device_access_module);
}
module_exit(cyttsp5_device_access_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Parade TrueTouch(R) Standard Product Device Access Driver");
MODULE_AUTHOR("Parade Technologies <ttdrivers@paradetech.com>");

