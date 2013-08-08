/* drivers/input/touchscreen/atmel_mXT224E.c - ATMEL Touch driver
 *
 * Copyright (C) 2008 ATMEL
 * Copyright (C) 2011 Huawei Corporation.
 *
 * Based on touchscreen code from Atmel Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/jiffies.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/bitops.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>

#include <linux/kthread.h>

#ifdef TS_ATMEL_DEBUG
#define TS_DEBUG_ATMEL(fmt, args...) pr_info(fmt, ##args)
#else
#define TS_DEBUG_ATMEL(fmt, args...)
#endif

#define ATMEL_MXT224E_NAME "atmel_mxt224e"

#define INFO_BLK_FID                            0
#define INFO_BLK_VID                            1
#define INFO_BLK_VER                            2
#define INFO_BLK_BUILD                          3
#define INFO_BLK_XSIZE                          4
#define INFO_BLK_YSIZE                          5
#define INFO_BLK_OBJS                           6

#define OBJ_TABLE_TYPE                          0
#define OBJ_TABLE_LSB                           1
#define OBJ_TABLE_MSB                           2
#define OBJ_TABLE_SIZE                          3
#define OBJ_TABLE_INSTANCES                     4
#define OBJ_TABLE_RIDS                          5

#define RESERVED_T0                               0u
#define RESERVED_T1                               1u
#define DEBUG_DELTAS_T2                           2u
#define DEBUG_REFERENCES_T3                       3u
#define DEBUG_SIGNALS_T4                          4u
#define GEN_MESSAGEPROCESSOR_T5                   5u
#define GEN_COMMANDPROCESSOR_T6                   6u
#define GEN_POWERCONFIG_T7                        7u
#define GEN_ACQUISITIONCONFIG_T8                  8u
#define TOUCH_MULTITOUCHSCREEN_T9                 9u
#define TOUCH_SINGLETOUCHSCREEN_T10               10u
#define TOUCH_XSLIDER_T11                         11u
#define TOUCH_YSLIDER_T12                         12u
#define TOUCH_XWHEEL_T13                          13u
#define TOUCH_YWHEEL_T14                          14u
#define TOUCH_KEYARRAY_T15                        15u
#define PROCG_SIGNALFILTER_T16                    16u
#define PROCI_LINEARIZATIONTABLE_T17              17u
#define SPT_COMCONFIG_T18                         18u
#define SPT_GPIOPWM_T19                           19u
#define PROCI_GRIPFACESUPPRESSION_T20             20u
#define RESERVED_T21                              21u
#define PROCG_NOISESUPPRESSION_T22                22u
#define TOUCH_PROXIMITY_T23                       23u
#define PROCI_ONETOUCHGESTUREPROCESSOR_T24        24u
#define SPT_SELFTEST_T25                          25u
#define DEBUG_CTERANGE_T26                        26u
#define PROCI_TWOTOUCHGESTUREPROCESSOR_T27        27u
#define SPT_CTECONFIG_T28                         28u
#define SPT_GPI_T29                               29u
#define SPT_GATE_T30                              30u
#define TOUCH_KEYSET_T31                          31u
#define TOUCH_XSLIDERSET_T32                      32u
#define DIAGNOSTIC_T37                            37u
#define PROCI_GRIPSUPPRESSION_T40                 40u
#define PROCI_TOUCHSUPPRESSION_T42                42u
#define SPT_CTECONFIG_T46                         46u
#define PROCI_STYLUS_T47                          47u
#define PROCG_NOISESUPPRESSION_T48                48u

#define T37_PAGE_SIZE                           128

#define T37_TCH_FLAG_SIZE                       80
#define T37_TCH_FLAG_IDX                        0
#define T37_ATCH_FLAG_IDX                       40

#define T37_MODE                                0
#define T37_PAGE                                1
#define T37_DATA                                2 /* n bytes */

#define T37_PAGE_NUM0                           0
#define T37_PAGE_NUM1                           1
#define T37_PAGE_NUM2                           2
#define T37_PAGE_NUM3                           3

#define MSG_RID                                 0

#define T6_CFG_RESET                            0
#define T6_CFG_BACKUPNV                         1
#define T6_CFG_CALIBRATE                        2
#define T6_CFG_REPORTALL                        3
/* Reserved */
#define T6_CFG_DIAG                             5

#define T6_CFG_DIAG_CMD_PAGEUP                  0x01
#define T6_CFG_DIAG_CMD_PAGEDOWN                0x02
#define T6_CFG_DIAG_CMD_DELTAS                  0x10
#define T6_CFG_DIAG_CMD_REF                     0x11
#define T6_CFG_DIAG_CMD_CTE                     0x31
#define T6_CFG_DIAG_CMD_TCH                     0xF3

#define T6_MSG_STATUS                           1
#define T6_MSG_CHECKSUM                         2 /* three bytes */

#define T6_MSG_STATUS_COMSERR                   BIT(2)
#define T6_MSG_STATUS_CFGERR                    BIT(3)
#define T6_MSG_STATUS_CAL                       BIT(4)
#define T6_MSG_STATUS_SIGERR                    BIT(5)
#define T6_MSG_STATUS_OFL                       BIT(6)
#define T6_MSG_STATUS_RESET                     BIT(7)

#define T7_CFG_IDLEACQINT                       0
#define T7_CFG_ACTVACQINT                       1
#define T7_CFG_ACTV2IDLETO                      2

#define T8_CFG_CHRGTIME                         0
/* Reserved */
#define T8_CFG_TCHDRIFT                         2
#define T8_CFG_DRIFTST                          3
#define T8_CFG_TCHAUTOCAL                       4
#define T8_CFG_SYNC                             5
#define T8_CFG_ATCHCALST                        6
#define T8_CFG_ATCHCALSTHR                      7
#define T8_CFG_ATCHFRCCALTHR                    8 /* FW v2.x */
#define T8_CFG_ATCHFRCCALRATIO                  9 /* FW v2.x */

#define T9_CFG_CTRL                             0
#define T9_CFG_XORIGIN                          1
#define T9_CFG_YORIGIN                          2
#define T9_CFG_XSIZE                            3
#define T9_CFG_YSIZE                            4
#define T9_CFG_AKSCFG                           5
#define T9_CFG_BLEN                             6
#define T9_CFG_TCHTHR                           7
#define T9_CFG_TCHDI                            8
#define T9_CFG_ORIENT                           9
#define T9_CFG_MRGTIMEOUT                       10
#define T9_CFG_MOVHYSTI                         11
#define T9_CFG_MOVHYSTN                         12
#define T9_CFG_MOVFILTER                        13
#define T9_CFG_NUMTOUCH                         14
#define T9_CFG_MRGHYST                          15
#define T9_CFG_MRGTHR                           16
#define T9_CFG_AMPHYST                          17
#define T9_CFG_XRANGE                           18 /* two bytes */
#define T9_CFG_YRANGE                           20 /* two bytes */
#define T9_CFG_XLOCLIP                          22
#define T9_CFG_XHICLIP                          23
#define T9_CFG_YLOCLIP                          24
#define T9_CFG_YHICLIP                          25
#define T9_CFG_XEDGECTRL                        26
#define T9_CFG_XEDGEDIST                        27
#define T9_CFG_YEDGECTRL                        28
#define T9_CFG_YEDGEDIST                        29
#define T9_CFG_JUMPLIMIT                        30
#define T9_CFG_TCHHYST                          31 /* FW v2.x */

#define T9_MSG_STATUS                           1
#define T9_MSG_XPOSMSB                          2
#define T9_MSG_YPOSMSB                          3
#define T9_MSG_XYPOSLSB                         4
#define T9_MSG_TCHAREA                          5
#define T9_MSG_TCHAMPLITUDE                     6
#define T9_MSG_TCHVECTOR                        7

#define T9_MSG_STATUS_UNGRIP                    BIT(0) /* FW v2.x */
#define T9_MSG_STATUS_SUPPRESS                  BIT(1)
#define T9_MSG_STATUS_AMP                       BIT(2)
#define T9_MSG_STATUS_VECTOR                    BIT(3)
#define T9_MSG_STATUS_MOVE                      BIT(4)
#define T9_MSG_STATUS_RELEASE                   BIT(5)
#define T9_MSG_STATUS_PRESS                     BIT(6)
#define T9_MSG_STATUS_DETECT                    BIT(7)

#define T20_CFG_CTRL                            0
#define T20_CFG_XLOGRIP                         1
#define T20_CFG_XHIGRIP                         2
#define T20_CFG_YLOGRIP                         3
#define T20_CFG_YHIGRIP                         4
#define T20_CFG_MAXTCHS                         5
/* Reserved */
#define T20_CFG_SZTHR1                          7
#define T20_CFG_SZTHR2                          8
#define T20_CFG_SHPTHR1                         9
#define T20_CFG_SHPTHR2                         10
#define T20_CFG_SHPEXTTO                        11

#define T20_MSG_STATUS                          1

#define T20_MSG_STATUS_FACESUP                  BIT(0)

#define T22_CFG_CTRL                            0
/* Reserved */
#define T22_CFG_GCAFUL                          3 /* two bytes */
#define T22_CFG_GCAFLL                          5 /* two bytes */
#define T22_CFG_ACTVGCAFVALID                   7
#define T22_CFG_NOISETHR                        8
/* Reserved */
#define T22_CFG_FREQHOPSCALE                    10
#define T22_CFG_FREQ                            11 /* five bytes */
#define T22_CFG_IDLEGCAFVAILD                   16

#define T22_MSG_STATUS                          1
#define T22_MSG_GCAFDEPTH                       2
#define T22_MSG_FREQINDEX                       3

#define T22_MSG_STATUS_FHCHG                    BIT(0)
#define T22_MSG_STATUS_GCAFERR                  BIT(2)
#define T22_MSG_STATUS_FHERR                    BIT(3)
#define T22_MSG_STATUS_GCAFCHG                  BIT(4)

#define T19_CFG_CTRL                            0
#define T19_CFG_REPORTMASK                      1
#define T19_CFG_DIR                             2
#define T19_CFG_INTPULLUP                       3
#define T19_CFG_OUT                             4
#define T19_CFG_WAKE                            5
#define T19_CFG_PWM                             6
#define T19_CFG_PERIOD                          7
#define T19_CFG_DUTY0                           8
#define T19_CFG_DUTY1                           9
#define T19_CFG_DUTY2                           10
#define T19_CFG_DUTY3                           11
#define T19_CFG_TRIGGER0                        12
#define T19_CFG_TRIGGER1                        13
#define T19_CFG_TRIGGER2                        14
#define T19_CFG_TRIGGER3                        15

#define T19_CFG_CTRL_ENABLE                     BIT(0)
#define T19_CFG_CTRL_RPTEN                      BIT(1)
#define T19_CFG_CTRL_FORCERPT                   BIT(2)

#define T19_MSG_STATUS                          1

#define T25_CFG_CTRL                            0
#define T25_CFG_CMD                             1

#define T25_MSG_STATUS                          1
#define T25_MSG_INFO                            2 /* five bytes */

#define T28_CFG_CTRL                            0
#define T28_CFG_CMD                             1
#define T28_CFG_MODE                            2
#define T28_CFG_IDLEGCAFDEPTH                   3
#define T28_CFG_ACTVGCAFDEPTH                   4
#define T28_CFG_VOLTAGE                         5

#define T28_CFG_MODE0_X                         16
#define T28_CFG_MODE0_Y                         14

#define T28_MSG_STATUS                          1

#define T48_NOISESUPPRESSION_CFG	1

/* cable_config[] of atmel_i2c_platform_data */
/* config[] of atmel_config_data */
#define CB_TCHTHR                               0
#define CB_NOISETHR                             1
#define CB_IDLEGCAFDEPTH                        2
#define CB_ACTVGCAFDEPTH                        3

#define NC_TCHTHR                               0
#define NC_TCHDI                                1
#define NC_NOISETHR                             2

/* filter_level */
#define FL_XLOGRIPMIN                           0
#define FL_XLOGRIPMAX                           1
#define FL_XHIGRIPMIN                           2
#define FL_XHIGRIPMAX                           3

struct info_id_t {
    uint8_t family_id;
    uint8_t variant_id;
    uint8_t version;
    uint8_t build;
    uint8_t matrix_x_size;
    uint8_t matrix_y_size;
    uint8_t num_declared_objects;
};

struct object_t {
    uint8_t object_type;
    uint16_t i2c_address;
    uint8_t size;
    uint8_t instances;
    uint8_t num_report_ids;
    uint8_t report_ids;
};

struct atmel_virtual_key {
    int keycode;
    int range_min;
    int range_max;
};

struct atmel_finger_data {
    int x;
    int y;
    int w;
    int z;
};

struct atmel_i2c_platform_data {
    uint16_t version;
    uint16_t source;
    uint16_t abs_x_min;
    uint16_t abs_x_max;
    uint16_t abs_y_min;
    uint16_t abs_y_max;
    uint8_t abs_pressure_min;
    uint8_t abs_pressure_max;
    uint8_t abs_width_min;
    uint8_t abs_width_max;
    uint8_t abs_area_min;
    uint8_t abs_area_max;
    int gpio_irq;
    int gpio_reset;
    int (*power)(int on);
    u8 config_T6[6];
    u8 config_T7[3];
    u8 config_T8[10];
    u8 config_T9[35];
    u8 config_T15[11];
    u8 config_T19[16];
    u8 config_T20[12];
    u8 config_T22[17];
    u8 config_T23[15];
    u8 config_T24[19];
    u8 config_T25[14];
    u8 config_T27[7];
    u8 config_T28[6];
    u8 config_T40[5];
    u8 config_T42[8];
    u8 config_T46[9];
    u8 config_T47[10];
    u8 config_T48[54];
    u8 object_crc[3];
    u8 cable_config[4];
    u8 cable_config_T7[3];
    u8 cable_config_T8[10];
    u8 cable_config_T9[35];
    u8 cable_config_T22[17];
    u8 cable_config_T28[6];
    u8 cable_config_T46[9];
    u8 cable_config_T48[54];
    u8 noise_config[3];
    u16 filter_level[4];
    u8 GCAF_level[5];
    u8 ATCH_NOR[6];
    u8 ATCH_NOR_20S[6];
};

struct atmel_config_data {
    int8_t config[4];
    int8_t *config_T7;
    int8_t *config_T8;
    int8_t *config_T9;
    int8_t *config_T22;
    int8_t *config_T28;
    int8_t *config_T46;
    int8_t *config_T48;
};

#define ATMEL_I2C_RETRY_TIMES 10

/* config_setting */
#define NONE                                    0
#define CONNECTED                               1
struct atmel_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct atmel_i2c_platform_data *pdata;
	struct workqueue_struct *atmel_wq;
	struct work_struct work;
	int (*power) (int on);
	struct info_id_t *id;
	struct object_t *object_table;
	struct iomux_block *gpio_block;
	struct block_config *gpio_block_config;
	uint8_t finger_count;
	uint16_t abs_x_min;
	uint16_t abs_x_max;
	uint16_t abs_y_min;
	uint16_t abs_y_max;
	uint8_t abs_area_min;
	uint8_t abs_area_max;
	uint8_t abs_width_min;
	uint8_t abs_width_max;
	uint8_t abs_pressure_min;
	uint8_t abs_pressure_max;
	uint8_t first_pressed;
	struct atmel_finger_data finger_data[10];
	uint8_t finger_type;
	uint8_t finger_support;
	uint16_t finger_pressed;
	uint8_t face_suppression;
	uint8_t grip_suppression;
	uint8_t noise_status[2];
	uint16_t *filter_level;
	uint8_t calibration_confirm;
	uint64_t timestamp;
	struct atmel_config_data config_setting[2];
	int8_t noise_config[3];
	uint8_t status;
	uint8_t GCAF_sample;
	uint8_t *GCAF_level;
	uint8_t noisethr;
	uint8_t noisethr_config;
	uint8_t diag_command;
	uint8_t *ATCH_EXT;
	int8_t *ATCH_NOR;
	int8_t *ATCH_NOR_20S;
	int pre_data[11];
	/*unlock flag used to indicate calibration after unlock system*/
	int unlock_flag;

	/*For usb detect*/
	struct work_struct usb_work;
	struct notifier_block nb;
	unsigned long usb_event;
	struct mutex lock;
};

static struct atmel_ts_data *private_ts;

#define LDO_POWR_VOLTAGE 2700000 /*2.7v*/
static	struct regulator		*LDO;

int i2c_atmel_read(struct i2c_client *client, uint16_t address, uint8_t *data, uint8_t length)
{
	int retry, ret;
	uint8_t addr[2];

	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 2,
			.buf = addr,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = data,
		}
	};
	addr[0] = address & 0xFF;
	addr[1] = (address >> 8) & 0xFF;

	for (retry = 0; retry < ATMEL_I2C_RETRY_TIMES; retry++) {
		ret = i2c_transfer(client->adapter, msg, 2);
		if ((ret == 2) || (ret == -ERESTARTSYS))
			break;
		mdelay(10);
	}
	if (retry == ATMEL_I2C_RETRY_TIMES) {
		dev_err(&client->dev,  "k3ts, %s: i2c_read_block retry over %d\n", __func__,
			ATMEL_I2C_RETRY_TIMES);
		return -EIO;
	}
	return 0;
}

int i2c_atmel_write(struct i2c_client *client, uint16_t address, uint8_t *data, uint8_t length)
{
	int retry, loop_i, ret;
	uint8_t buf[length + 2];

	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = length + 2,
			.buf = buf,
		}
	};

	buf[0] = address & 0xFF;
	buf[1] = (address >> 8) & 0xFF;

	for (loop_i = 0; loop_i < length; loop_i++)
		buf[loop_i + 2] = data[loop_i];

	for (retry = 0; retry < ATMEL_I2C_RETRY_TIMES; retry++) {
		ret = i2c_transfer(client->adapter, msg, 1);
		if ((ret == 1) || (ret == -ERESTARTSYS))
			break;
		mdelay(10);
	}

	if (retry == ATMEL_I2C_RETRY_TIMES) {
		dev_err(&client->dev, "k3ts, %s: i2c_write_block retry over %d\n", __func__,
			ATMEL_I2C_RETRY_TIMES);
		return -EIO;
	}
	return 0;

}

int i2c_atmel_write_byte_data(struct i2c_client *client, uint16_t address, uint8_t value)
{
	i2c_atmel_write(client, address, &value, 1);
	return 0;
}

uint16_t get_object_address(struct atmel_ts_data *ts, uint8_t object_type)
{
	uint8_t loop_i;
	for (loop_i = 0; loop_i < ts->id->num_declared_objects; loop_i++) {
		if (ts->object_table[loop_i].object_type == object_type)
			return ts->object_table[loop_i].i2c_address;
	}
	return 0;
}
uint8_t get_object_size(struct atmel_ts_data *ts, uint8_t object_type)
{
	uint8_t loop_i;
	for (loop_i = 0; loop_i < ts->id->num_declared_objects; loop_i++) {
		if (ts->object_table[loop_i].object_type == object_type)
			return ts->object_table[loop_i].size;
	}
	return 0;
}

uint8_t get_object_size_from_address(struct atmel_ts_data *ts, int address)
{
	uint8_t loop_i;
	for (loop_i = 0; loop_i < ts->id->num_declared_objects; loop_i++) {
		if (ts->object_table[loop_i].i2c_address == address)
			return ts->object_table[loop_i].size;
	}
	return 0;
}

uint8_t get_rid(struct atmel_ts_data *ts, uint8_t object_type)
{
	uint8_t loop_i;
	for (loop_i = 0; loop_i < ts->id->num_declared_objects; loop_i++) {
		if (ts->object_table[loop_i].object_type == object_type)
			return ts->object_table[loop_i].report_ids;
	}
	return 0;
}

static void check_calibration(struct atmel_ts_data *ts)
{
	uint8_t data[T37_DATA + T37_TCH_FLAG_SIZE];
	uint8_t loop_i, loop_j, x_limit = 0, check_mask, tch_ch = 0, atch_ch = 0;

	memset(data, 0xFF, sizeof(data));
	i2c_atmel_write_byte_data(ts->client,
		get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
		T6_CFG_DIAG, T6_CFG_DIAG_CMD_TCH);

	for (loop_i = 0;
		!(data[T37_MODE] == T6_CFG_DIAG_CMD_TCH && data[T37_PAGE] == T37_PAGE_NUM0) && loop_i < 10; loop_i++) {
		msleep(5);
		i2c_atmel_read(ts->client,
			get_object_address(ts, DIAGNOSTIC_T37), data, 2);
	}

	if (loop_i == 10)
		dev_err(&ts->client->dev, "k3ts, %s: Diag data not ready\n", __func__);

	i2c_atmel_read(ts->client, get_object_address(ts, DIAGNOSTIC_T37), data,
		T37_DATA + T37_TCH_FLAG_SIZE);
	if (data[T37_MODE] == T6_CFG_DIAG_CMD_TCH &&
		data[T37_PAGE] == T37_PAGE_NUM0) {
		x_limit = T28_CFG_MODE0_X + ts->config_setting[NONE].config_T28[T28_CFG_MODE];
		x_limit = x_limit << 1;
		if (x_limit <= 40) {
			for (loop_i = 0; loop_i < x_limit; loop_i += 2) {
				for (loop_j = 0; loop_j < BITS_PER_BYTE; loop_j++) {
					check_mask = BIT_MASK(loop_j);
					if (data[T37_DATA + T37_TCH_FLAG_IDX + loop_i] &
						check_mask)
						tch_ch++;
					if (data[T37_DATA + T37_TCH_FLAG_IDX + loop_i + 1] &
						check_mask)
						tch_ch++;
					if (data[T37_DATA + T37_ATCH_FLAG_IDX + loop_i] &
						check_mask)
						atch_ch++;
					if (data[T37_DATA + T37_ATCH_FLAG_IDX + loop_i + 1] &
						check_mask)
						atch_ch++;
				}
			}
		}
	}
	i2c_atmel_write_byte_data(ts->client,
		get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
		T6_CFG_DIAG, T6_CFG_DIAG_CMD_PAGEUP);

	if (tch_ch && (atch_ch == 0)) {
		if (jiffies > (ts->timestamp + HZ/2) && (ts->calibration_confirm == 1)) {
			ts->calibration_confirm = 2;
		}
		if (ts->calibration_confirm < 2)
			ts->calibration_confirm = 1;
		ts->timestamp = jiffies;
	} else if (atch_ch > 1 || tch_ch > 8) {
		ts->calibration_confirm = 0;
		i2c_atmel_write_byte_data(ts->client,
			get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
			T6_CFG_CALIBRATE, 0x55);
	}
}

static void confirm_calibration(struct atmel_ts_data *ts)
{
	i2c_atmel_write(ts->client,
		get_object_address(ts, GEN_ACQUISITIONCONFIG_T8) +
		T8_CFG_TCHAUTOCAL, ts->ATCH_NOR_20S, 6);
	ts->pre_data[0] = 2;
}

static void msg_process_finger_data_x10y10bit(struct atmel_finger_data *fdata, uint8_t *data)
{
    fdata->x = data[T9_MSG_XPOSMSB] << 2 | data[T9_MSG_XYPOSLSB] >> 6;
    fdata->y = data[T9_MSG_YPOSMSB] << 2 | (data[T9_MSG_XYPOSLSB] & 0x0C) >>2;
    fdata->w = data[T9_MSG_TCHAREA];
    fdata->z = data[T9_MSG_TCHAMPLITUDE];
}
static void msg_process_finger_data_x10y12bit(struct atmel_finger_data *fdata, uint8_t *data)
{
    fdata->x = data[T9_MSG_XPOSMSB] << 2 | data[T9_MSG_XYPOSLSB] >> 6;
    fdata->y = data[T9_MSG_YPOSMSB] << 4 | (data[T9_MSG_XYPOSLSB] & 0x0F) ;
    fdata->w = data[T9_MSG_TCHAREA];
    fdata->z = data[T9_MSG_TCHAMPLITUDE];
}

static void msg_process_multitouch(struct atmel_ts_data *ts, uint8_t *data, uint8_t idx)
{
	if (ts->calibration_confirm < 2 && ts->id->version == 0x10)
		check_calibration(ts);
    if(ts->abs_y_max >= 1024) {
        msg_process_finger_data_x10y12bit(&ts->finger_data[idx], data);
    } else {
        msg_process_finger_data_x10y10bit(&ts->finger_data[idx], data);
    }
	if (data[T9_MSG_STATUS] & T9_MSG_STATUS_RELEASE) {
		if (ts->grip_suppression & BIT(idx))
			ts->grip_suppression &= ~BIT(idx);
		if (ts->finger_pressed & BIT(idx)) {
			ts->finger_count--;
			ts->finger_pressed &= ~BIT(idx);
			if (!ts->first_pressed) {
				if (!ts->finger_count)
					ts->first_pressed = 1;
			}
			if (ts->pre_data[0] < 2 && ts->unlock_flag != 1) {

				if (ts->finger_count) {
					i2c_atmel_write_byte_data(ts->client,
						get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
						T6_CFG_CALIBRATE, 0x55);
				} else if (!ts->finger_count && ts->pre_data[0] == 1)
					ts->pre_data[0] = 0;
			}
		}
	} else if ((data[T9_MSG_STATUS] & (T9_MSG_STATUS_DETECT | T9_MSG_STATUS_PRESS))
	&& !(ts->finger_pressed & BIT(idx))) {
		if (ts->id->version >= 0x10 && ts->pre_data[0] < 2) {
			if (jiffies > (ts->timestamp + 20 * HZ)) {
				confirm_calibration(ts);
			}
		}
		if (!(ts->grip_suppression & BIT(idx))) {
			ts->finger_count++;
			ts->finger_pressed |= BIT(idx);
			if (ts->id->version >= 0x10 && ts->pre_data[0] < 2) {
				ts->pre_data[idx + 1] = ts->finger_data[idx].x;
                ts->pre_data[idx + 2] = ts->finger_data[idx].y;
				if (ts->finger_count == ts->finger_support) {
					i2c_atmel_write_byte_data(ts->client,
						get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
						T6_CFG_CALIBRATE, 0x55);
				} else if (!ts->pre_data[0] && ts->finger_count == 1)
						ts->pre_data[0] = 1;
			}
		}
	} else if ((data[T9_MSG_STATUS] & (T9_MSG_STATUS_DETECT|T9_MSG_STATUS_PRESS))
		&& ts->pre_data[0] < 2 && ts->unlock_flag != 1) {
            if (ts->finger_count == 1 && ts->pre_data[0] &&
                 (idx == 0 && ((abs(ts->finger_data[idx].y - ts->pre_data[idx + 2]) > 50)
                  || (abs(ts->finger_data[idx].x - ts->pre_data[idx + 1]) > 50))))
            {
                ts->unlock_flag = 1;
                ts->calibration_confirm = 2;
            }
      }

}

static void compatible_input_report(struct input_dev *idev,
				struct atmel_finger_data *fdata, uint8_t press, uint8_t last)
{
	if (!press) {
		input_mt_sync(idev);
		/*input_report_key(idev, BTN_TOUCH, 0);*/
		input_report_key(idev, BTN_TOUCH, 1);

	} else {
		TS_DEBUG_ATMEL("k3ts, %s: Touch report_key x = %d, y = %d, z = %d, w = %d\n ", __func__,
			fdata->x, fdata->y, fdata->z, fdata->w);
		input_report_abs(idev, ABS_MT_TOUCH_MAJOR, fdata->z);
		input_report_abs(idev, ABS_MT_WIDTH_MAJOR, fdata->w);
		input_report_abs(idev, ABS_MT_POSITION_X, fdata->x);
		input_report_abs(idev, ABS_MT_POSITION_Y, fdata->y);
		input_mt_sync(idev);
    }
}



static void multi_input_report(struct atmel_ts_data *ts)
{
	uint8_t loop_i, finger_report = 0;

	for (loop_i = 0; loop_i < ts->finger_support; loop_i++) {
		if (ts->finger_pressed & BIT(loop_i)) {
			compatible_input_report(ts->input_dev, &ts->finger_data[loop_i],
				1, (ts->finger_count == ++finger_report));
		}
	}
}
static irqreturn_t atmel_interrupt_fun(int irq, void *dev_id)
{
	int ret;
	struct atmel_ts_data *ts = dev_id;
	uint8_t data[7];
	int8_t report_type;
	uint8_t msg_byte_num = 7;

	memset(data, 0x0, sizeof(data));

	mutex_lock(&ts->lock);
	ret = i2c_atmel_read(ts->client, get_object_address(ts,
		GEN_MESSAGEPROCESSOR_T5), data, 7);

	report_type = data[MSG_RID] - ts->finger_type;
	if (report_type >= 0 && report_type < ts->finger_support) {
		msg_process_multitouch(ts, data, report_type);
	} else {
		if (data[MSG_RID] == get_rid(ts, GEN_COMMANDPROCESSOR_T6)) {
			if (data[1] & 0x10) {
				ts->timestamp = jiffies;
		}
		if (data[1] & 0x80) {
			msleep(100);

			i2c_atmel_write_byte_data(ts->client,
				get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
				T6_CFG_CALIBRATE, 0x55);
		}
		msg_byte_num = 5;
	}
	if (data[MSG_RID] == get_rid(ts, PROCI_TOUCHSUPPRESSION_T42)) {
			if (ts->calibration_confirm < 2 && ts->id->version == 0x10) {
				i2c_atmel_write_byte_data(ts->client,
					get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
					T6_CFG_CALIBRATE, 0x55);
			}
			ts->face_suppression = data[T20_MSG_STATUS];
			printk(KERN_INFO "Touch Face suppression %s: ",
			ts->face_suppression ? "Active" : "Inactive");
			msg_byte_num = 2;
		}
	}
    if (!ts->finger_count || ts->face_suppression) {
		ts->finger_pressed = 0;
		ts->finger_count = 0;
		compatible_input_report(ts->input_dev, NULL, 0, 1);
	} else {
		multi_input_report(ts);
    }
    input_sync(ts->input_dev);
    mutex_unlock(&ts->lock);

    return IRQ_HANDLED;
}

static int read_object_table(struct atmel_ts_data *ts)
{
	uint8_t i, type_count = 0;
	uint8_t data[6];
	memset(data, 0x0, sizeof(data));

	ts->object_table = kzalloc(sizeof(struct object_t)*ts->id->num_declared_objects, GFP_KERNEL);
	if (ts->object_table == NULL) {
		dev_err(&ts->client->dev, "k3ts, %s: allocate object_table failed\n", __func__);
		return -ENOMEM;
	}

	for (i = 0; i < ts->id->num_declared_objects; i++) {
		i2c_atmel_read(ts->client, i * 6 + 0x07, data, 6);
		ts->object_table[i].object_type = data[OBJ_TABLE_TYPE];
		ts->object_table[i].i2c_address =
			data[OBJ_TABLE_LSB] | data[OBJ_TABLE_MSB] << 8;
		ts->object_table[i].size = data[OBJ_TABLE_SIZE] + 1;
		ts->object_table[i].instances = data[OBJ_TABLE_INSTANCES];
		ts->object_table[i].num_report_ids = data[OBJ_TABLE_RIDS];
		if (data[OBJ_TABLE_RIDS]) {
			ts->object_table[i].report_ids = type_count + 1;
			type_count += data[OBJ_TABLE_RIDS];
		}
		if (data[OBJ_TABLE_TYPE] == TOUCH_MULTITOUCHSCREEN_T9)
			ts->finger_type = ts->object_table[i].report_ids;
	}

	return 0;
}

struct atmel_i2c_platform_data *atmel_ts_get_pdata(struct i2c_client *client)
{
	struct device_node *node = client->dev.of_node;
	struct atmel_i2c_platform_data *pdata = client->dev.platform_data;
	u32 data[8];

	if (pdata)
		return pdata;

	if (!node)
		return NULL;

	pdata = devm_kzalloc(&client->dev, sizeof(struct atmel_i2c_platform_data),
				GFP_KERNEL);
	pdata->gpio_irq = of_get_named_gpio(node, "atmel-ts,gpio-irq", 0);
	pdata->gpio_reset = of_get_named_gpio(node, "atmel-ts,gpio-reset", 0);

	of_property_read_u32_array(node, "atmel-ts,abs", &data[0], 8);
	pdata->abs_x_min = data[0];
	pdata->abs_x_max = data[1];
	pdata->abs_y_min = data[2];
	pdata->abs_y_max = data[3];
	pdata->abs_pressure_min = data[4];
	pdata->abs_pressure_max = data[5];
	pdata->abs_width_min = data[6];
	pdata->abs_width_max = data[7];

	of_property_read_u8_array(node, "atmel-ts,cfg_t6", &pdata->config_T6[0], 6);
	of_property_read_u8_array(node, "atmel-ts,cfg_t7", &pdata->config_T7[0], 3);
	of_property_read_u8_array(node, "atmel-ts,cfg_t8", &pdata->config_T8[0], 10);
	of_property_read_u8_array(node, "atmel-ts,cfg_t9", &pdata->config_T9[0], 35);
	of_property_read_u8_array(node, "atmel-ts,cfg_t15", &pdata->config_T15[0], 11);
	of_property_read_u8_array(node, "atmel-ts,cfg_t19", &pdata->config_T19[0], 16);
	of_property_read_u8_array(node, "atmel-ts,cfg_t23", &pdata->config_T23[0], 15);
	of_property_read_u8_array(node, "atmel-ts,cfg_t25", &pdata->config_T25[0], 14);
	of_property_read_u8_array(node, "atmel-ts,cfg_t40", &pdata->config_T40[0], 5);
	of_property_read_u8_array(node, "atmel-ts,cfg_t42", &pdata->config_T42[0], 8);
	of_property_read_u8_array(node, "atmel-ts,cfg_t46", &pdata->config_T46[0], 9);
	of_property_read_u8_array(node, "atmel-ts,cfg_t47", &pdata->config_T47[0], 10);
	of_property_read_u8_array(node, "atmel-ts,cfg_t48", &pdata->config_T48[0], 54);
	of_property_read_u8_array(node, "atmel-ts,object_crc", &pdata->object_crc[0], 3);
	of_property_read_u8_array(node, "atmel-ts,cable_config", &pdata->cable_config[0], 4);
	of_property_read_u8_array(node, "atmel-ts,cable_config_t7", &pdata->cable_config_T7[0], 3);
	of_property_read_u8_array(node, "atmel-ts,cable_config_t8", &pdata->cable_config_T8[0], 10);
	of_property_read_u8_array(node, "atmel-ts,cable_config_t46", &pdata->cable_config_T46[0], 9);
	of_property_read_u8_array(node, "atmel-ts,cable_config_t48", &pdata->cable_config_T48[0], 54);
	of_property_read_u8_array(node, "atmel-ts,noise_config", &pdata->noise_config[0], 3);
	of_property_read_u16_array(node, "atmel-ts,filter_level", &pdata->filter_level[0], 4);
	of_property_read_u8_array(node, "atmel-ts,gcaf_level", &pdata->GCAF_level[0], 5);
	of_property_read_u8_array(node, "atmel-ts,atch_nor", &pdata->ATCH_NOR[0], 6);
	of_property_read_u8_array(node, "atmel-ts,atch_nor_20s", &pdata->ATCH_NOR_20S[0], 6);

	return pdata;
}

static int atmel_ts_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct atmel_ts_data *ts;
	struct atmel_i2c_platform_data *pdata;
	int ret = 0, intr = 0;
	uint8_t loop_i;
	struct i2c_msg msg[2];
	uint8_t data[16];
	uint8_t CRC_check = 0;

	client->dev.init_name = "atmel-ts";
	LDO = regulator_get(&client->dev, "ldo");
	if (IS_ERR(LDO)) {
		dev_err(&client->dev, "no regulator found\n");
		LDO = NULL;
	} else {
		ret = regulator_enable(LDO);
		if (!ret)
			ret = regulator_set_voltage(LDO, LDO_POWR_VOLTAGE, LDO_POWR_VOLTAGE);
		if (ret)
			dev_err(&client->dev, "k3ts, %s: failed to set LDO\n", __func__);
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "k3ts, %s: need I2C_FUNC_I2C\n", __func__);
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	ts = kzalloc(sizeof(struct atmel_ts_data), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}

	ts->unlock_flag = 0;
	mutex_init(&ts->lock);

	ts->atmel_wq = create_singlethread_workqueue("atmel_wq");
	if (!ts->atmel_wq) {
		dev_err(&client->dev, "k3ts, %s: create workqueue failed\n", __func__);
		ret = -ENOMEM;
		goto err_cread_wq_failed;
	}

	ts->client = client;
	i2c_set_clientdata(client, ts);

	pdata = atmel_ts_get_pdata(client);
	ts->pdata = pdata;

	if (pdata) {
		ts->power = pdata->power;
		intr = pdata->gpio_irq;
		client->irq = gpio_to_irq(intr);
	}
	if (ts->power)
		ret = ts->power(1);
	ret = gpio_request(intr, "gpio_tp_intr");
	if (ret) {
		dev_err(&client->dev, "gpio_request %d failed\n", intr);
		goto err_request_gpio_failed;
	}
	ret = gpio_direction_input(intr);
	if (ret) {
		dev_err(&client->dev, "k3ts, %s: gpio_direction_input failed %d\n", __func__, intr);
		goto err_gpio_direction_failed;
	}

	ret = gpio_request(ts->pdata->gpio_reset, "gpio_tp_reset");
	if (ret) {
		dev_err(&client->dev, "k3ts, %s: gpio_request failed %d, ret = %d\n",
			__func__, ts->pdata->gpio_reset, ret);
		goto err_request_gpio_reset_failed;
	}

	gpio_direction_output(ts->pdata->gpio_reset, 1);
	mdelay(5);
	gpio_direction_output(ts->pdata->gpio_reset, 0);
	mdelay(10);
	gpio_direction_output(ts->pdata->gpio_reset, 1);
	mdelay(50);

	for (loop_i = 0; loop_i < 10; loop_i++) {
		if (!gpio_get_value(intr))
			break;
		msleep(10);
	}

	if (loop_i == 10)
		dev_err(&client->dev, "k3ts, %s: No Messages\n", __func__);

	/* read message*/
	msg[0].addr = ts->client->addr;
	msg[0].flags = I2C_M_RD;
	msg[0].len = 7;
	msg[0].buf = data;
	ret = i2c_transfer(client->adapter, msg, 1);

	if (ret < 0) {
		dev_err(&client->dev,  "k3ts, %s: No Atmel chip inside\n", __func__);
		goto err_detect_failed;
	}
	if (ts->power)
		ret = ts->power(2);

	if (data[MSG_RID] == 0x01 &&
		(data[T6_MSG_STATUS] & (T6_MSG_STATUS_SIGERR|T6_MSG_STATUS_COMSERR))) {
		dev_err(&client->dev, "k3ts, %s: init err: %x\n", __func__, data[1]);
		goto err_detect_failed;
	} else {
		for (loop_i = 0; loop_i < 10; loop_i++) {
			if (gpio_get_value(intr)) {
				dev_err(&client->dev, "k3ts, %s: No more message\n", __func__);
				break;
			}
			ret = i2c_transfer(client->adapter, msg, 1);
			msleep(10);
		}
	}

	/* Read the info block data. */
	ts->id = kzalloc(sizeof(struct info_id_t), GFP_KERNEL);
	if (ts->id == NULL) {
		dev_err(&client->dev, "k3ts, %s: allocate info_id_t failed\n", __func__);
		goto err_alloc_failed;
	}
	ret = i2c_atmel_read(client, 0x00, data, 7);

	ts->id->family_id = data[INFO_BLK_FID];
	ts->id->variant_id = data[INFO_BLK_VID];
	if (ts->id->family_id == 0x80 && ts->id->variant_id == 0x10)
		ts->id->version = data[INFO_BLK_VER] + 6;
	else
		ts->id->version = data[INFO_BLK_VER];

	ts->id->build = data[INFO_BLK_BUILD];
	ts->id->matrix_x_size = data[INFO_BLK_XSIZE];
	ts->id->matrix_y_size = data[INFO_BLK_YSIZE];
	ts->id->num_declared_objects = data[INFO_BLK_OBJS];

	/* Read object table. */
	ret = read_object_table(ts);
	if (ret < 0)
		goto err_read_table_failed;


	if (pdata) {
		ts->finger_support = pdata->config_T9[T9_CFG_NUMTOUCH];

		/* OBJECT CONFIG CRC check */
		if (pdata->object_crc[0]) {
			ret = i2c_atmel_write_byte_data(client,
						get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
						T6_CFG_CALIBRATE, 0x55);
			for (loop_i = 0; loop_i < 10; loop_i++) {
				if (!gpio_get_value(intr)) {
					ret = i2c_atmel_read(ts->client, get_object_address(ts,
								GEN_MESSAGEPROCESSOR_T5), data, 5);
					if (data[MSG_RID] == get_rid(ts, GEN_COMMANDPROCESSOR_T6))
						break;
				}
				msleep(10);
			}
			if (loop_i == 10)
				dev_err(&client->dev, "k3ts, %s: No checksum read\n", __func__);
			else {
				dev_info(&client->dev, "k3ts, %s:  CRC print : %x, %x, %x\n", __func__,
							data[T6_MSG_CHECKSUM + 0], data[T6_MSG_CHECKSUM + 1], data[T6_MSG_CHECKSUM + 2]);
				for (loop_i = 0; loop_i < 3; loop_i++) {
					if (pdata->object_crc[loop_i] != data[T6_MSG_CHECKSUM + loop_i]) {
						dev_err(&client->dev,
							"k3ts, %s: CRC Error: %x, %x\n", __func__,
							pdata->object_crc[loop_i],
							data[T6_MSG_CHECKSUM + loop_i]);
						break;
					}
				}
				if (loop_i == 3) {
					dev_info(&client->dev, "k3ts, %s: CRC passed: ", __func__);
					for (loop_i = 0; loop_i < 3; loop_i++)
						pr_info("0x%2.2X ", pdata->object_crc[loop_i]);
					pr_info("\n");
					CRC_check = 1;/*means CRC check OK*/
				}
			}
		}
		ts->abs_x_min = pdata->abs_x_min;
		ts->abs_x_max = pdata->abs_x_max;
		ts->abs_y_min = pdata->abs_y_min;
		ts->abs_y_max = pdata->abs_y_max;
		ts->abs_pressure_min = pdata->abs_pressure_min;
		ts->abs_pressure_max = pdata->abs_pressure_max;
		ts->abs_width_min = pdata->abs_width_min;
		ts->abs_width_max = pdata->abs_width_max;

		ts->GCAF_level = pdata->GCAF_level;
		if (ts->id->version >= 0x10) {
			ts->ATCH_EXT = &pdata->config_T8[6];
			ts->timestamp = jiffies + 60 * HZ;
		}
		ts->ATCH_NOR = pdata->ATCH_NOR;
		ts->ATCH_NOR_20S = pdata->ATCH_NOR_20S;
		ts->filter_level = pdata->filter_level;

		ts->config_setting[NONE].config_T7
			= ts->config_setting[CONNECTED].config_T7
			= pdata->config_T7;
		ts->config_setting[NONE].config_T8 = pdata->config_T8;
		ts->config_setting[CONNECTED].config_T8 = pdata->cable_config_T8;
		ts->config_setting[NONE].config_T9 = pdata->config_T9;
		ts->config_setting[NONE].config_T22 = pdata->config_T22;
		ts->config_setting[NONE].config_T28 = pdata->config_T28;
		ts->config_setting[NONE].config_T46 = pdata->config_T46;
		ts->config_setting[NONE].config_T48 = pdata->config_T48;
		ts->config_setting[CONNECTED].config_T46 = pdata->cable_config_T46;
		ts->config_setting[CONNECTED].config_T48 = pdata->cable_config_T48;

		if (pdata->noise_config[0])
			for (loop_i = 0; loop_i < 3; loop_i++)
				ts->noise_config[loop_i] = pdata->noise_config[loop_i];

		if (pdata->cable_config[0]) {
			ts->config_setting[NONE].config[CB_TCHTHR] =
				pdata->config_T9[T9_CFG_TCHTHR];
			ts->config_setting[NONE].config[CB_NOISETHR] =
				pdata->config_T22[T22_CFG_NOISETHR];
			ts->config_setting[NONE].config[CB_IDLEGCAFDEPTH] =
				pdata->config_T28[T28_CFG_IDLEGCAFDEPTH];
			ts->config_setting[NONE].config[CB_ACTVGCAFDEPTH] =
				pdata->config_T28[T28_CFG_ACTVGCAFDEPTH];
			for (loop_i = 0; loop_i < 4; loop_i++)
				ts->config_setting[CONNECTED].config[loop_i] =
					pdata->cable_config[loop_i];
			ts->GCAF_sample =
				ts->config_setting[CONNECTED].config[CB_ACTVGCAFDEPTH];
			if (ts->id->version >= 0x20)
				ts->noisethr = pdata->cable_config[CB_TCHTHR] -
					pdata->config_T9[T9_CFG_TCHHYST];
			else
				ts->noisethr = pdata->cable_config[CB_TCHTHR];
			ts->noisethr_config =
				ts->config_setting[CONNECTED].config[CB_NOISETHR];
		} else {
			if (pdata->cable_config_T7[0])
				ts->config_setting[CONNECTED].config_T7 =
					pdata->cable_config_T7;
			if (pdata->cable_config_T8[0])
				ts->config_setting[CONNECTED].config_T8 =
					pdata->cable_config_T8;
			if (pdata->cable_config_T9[0]) {
				ts->config_setting[CONNECTED].config_T9 =
					pdata->cable_config_T9;
				ts->config_setting[CONNECTED].config_T22 =
					pdata->cable_config_T22;
				ts->config_setting[CONNECTED].config_T28 =
					pdata->cable_config_T28;
				ts->GCAF_sample =
					ts->config_setting[CONNECTED].config_T28[T28_CFG_ACTVGCAFDEPTH];
			}
			if (ts->status == CONNECTED)
				ts->noisethr = (ts->id->version >= 0x20) ?
					pdata->cable_config_T9[T9_CFG_TCHTHR] - pdata->cable_config_T9[T9_CFG_TCHHYST] :
					pdata->cable_config_T9[T9_CFG_TCHTHR];
			else
				ts->noisethr = (ts->id->version >= 0x20) ?
					pdata->config_T9[T9_CFG_TCHTHR] - pdata->config_T9[T9_CFG_TCHHYST] :
					pdata->config_T9[T9_CFG_TCHTHR];
			ts->noisethr_config = pdata->cable_config_T22[T22_CFG_NOISETHR];

		}

		i2c_atmel_write(ts->client,
			get_object_address(ts, GEN_COMMANDPROCESSOR_T6),
			pdata->config_T6,
			get_object_size(ts, GEN_COMMANDPROCESSOR_T6));
		i2c_atmel_write(ts->client,
			get_object_address(ts, GEN_POWERCONFIG_T7),
			pdata->config_T7,
			get_object_size(ts, GEN_POWERCONFIG_T7));
		i2c_atmel_write(ts->client,
			get_object_address(ts, GEN_ACQUISITIONCONFIG_T8),
			pdata->config_T8,
			get_object_size(ts, GEN_ACQUISITIONCONFIG_T8));
		i2c_atmel_write(ts->client,
			get_object_address(ts, TOUCH_MULTITOUCHSCREEN_T9),
			pdata->config_T9,
			get_object_size(ts, TOUCH_MULTITOUCHSCREEN_T9));
		i2c_atmel_write(ts->client,
			get_object_address(ts, TOUCH_KEYARRAY_T15),
			pdata->config_T15,
			get_object_size(ts, TOUCH_KEYARRAY_T15));
		i2c_atmel_write(ts->client,
			get_object_address(ts, SPT_GPIOPWM_T19),
			pdata->config_T19,
			get_object_size(ts, SPT_GPIOPWM_T19));

		i2c_atmel_write(ts->client,
			get_object_address(ts, PROCI_GRIPSUPPRESSION_T40),
			pdata->config_T40,
			get_object_size(ts, PROCI_GRIPSUPPRESSION_T40));

		i2c_atmel_write(ts->client,
			get_object_address(ts, PROCI_TOUCHSUPPRESSION_T42),
			pdata->config_T42,
			get_object_size(ts, PROCI_TOUCHSUPPRESSION_T42));
		i2c_atmel_write(ts->client,
			get_object_address(ts, PROCG_NOISESUPPRESSION_T48),
			pdata->config_T48,
			get_object_size(ts, PROCG_NOISESUPPRESSION_T48));
		i2c_atmel_write(ts->client,
			get_object_address(ts, TOUCH_PROXIMITY_T23),
			pdata->config_T23,
			get_object_size(ts, TOUCH_PROXIMITY_T23));
		i2c_atmel_write(ts->client,
			get_object_address(ts, SPT_SELFTEST_T25),
			pdata->config_T25,
			get_object_size(ts, SPT_SELFTEST_T25));
		i2c_atmel_write(ts->client,
			get_object_address(ts, SPT_CTECONFIG_T46),
			pdata->config_T46,
			get_object_size(ts, SPT_CTECONFIG_T46));
		i2c_atmel_write(ts->client,
			get_object_address(ts, PROCI_STYLUS_T47),
			pdata->config_T47,
			get_object_size(ts, PROCI_STYLUS_T47));

		ret = i2c_atmel_write_byte_data(client,
					get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
					T6_CFG_BACKUPNV, 0x55);

		for (loop_i = 0; loop_i < 10; loop_i++) {
			if (!gpio_get_value(intr))
				break;
			dev_err(&client->dev,  "k3ts, %s: wait for Message(%d)\n", __func__, loop_i + 1);
			msleep(10);
		}

		i2c_atmel_read(client,
			get_object_address(ts, GEN_MESSAGEPROCESSOR_T5), data, 7);

		ret = i2c_atmel_write_byte_data(client,
					get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
					T6_CFG_RESET, 0x11);/*reset*/
		msleep(100);

		if (ts->status == CONNECTED) {
			if (ts->config_setting[CONNECTED].config_T8 != NULL)
				i2c_atmel_write(ts->client,
					get_object_address(ts, GEN_ACQUISITIONCONFIG_T8),
					ts->config_setting[CONNECTED].config_T8,
					get_object_size(ts, GEN_ACQUISITIONCONFIG_T8));
			if (ts->config_setting[CONNECTED].config_T46 != NULL)
		                i2c_atmel_write(ts->client,
					get_object_address(ts, SPT_CTECONFIG_T46),
					ts->config_setting[CONNECTED].config_T46,
					get_object_size(ts, SPT_CTECONFIG_T46));
			if (ts->config_setting[CONNECTED].config_T48 != NULL) {
				i2c_atmel_write(ts->client,
					get_object_address(ts, PROCG_NOISESUPPRESSION_T48),
					ts->config_setting[CONNECTED].config_T48,
					get_object_size(ts, PROCG_NOISESUPPRESSION_T48));
		}
        }
    }
	ts->calibration_confirm = 0;
	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		dev_err(&client->dev, "k3ts, %s: Failed to allocate input device\n", __func__);
		goto err_input_dev_alloc_failed;
	}
	/*Modified by z181527 for Debug Only*/
	ts->input_dev->name = "synaptics"/*"atmel-touchscreen"*/;
	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
	set_bit(BTN_TOUCH, ts->input_dev->keybit);
	set_bit(BTN_2, ts->input_dev->keybit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X,
		ts->abs_x_min, ts->abs_x_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y,
		ts->abs_y_min, ts->abs_y_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR,
		ts->abs_pressure_min, ts->abs_pressure_max,
		0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR,
		ts->abs_width_min, ts->abs_width_max, 0, 0);

	ret = input_register_device(ts->input_dev);
	if (ret) {
		dev_err(&client->dev,
			"k3ts, %s: atmel_ts_probe: Unable to register %s input device\n", __func__,
			ts->input_dev->name);
		goto err_input_register_device_failed;
	}

	ret = request_threaded_irq(client->irq, NULL, atmel_interrupt_fun,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			client->name, ts);
	if (ret)
		dev_err(&client->dev, "k3ts, %s: request_irq failed\n", __func__);

	private_ts = ts;

	dev_info(&client->dev, "k3ts, %s: probe %s successfully\n", __func__,
			ts->input_dev->name);

	return 0;

err_input_register_device_failed:
	input_free_device(ts->input_dev);
err_input_dev_alloc_failed:
err_read_table_failed:
	kfree(ts->id);
err_alloc_failed:
err_detect_failed:
err_gpio_direction_failed:
err_request_gpio_reset_failed:
	gpio_free(ts->pdata->gpio_reset);
	gpio_free(intr);
err_request_gpio_failed:
	destroy_workqueue(ts->atmel_wq);
err_cread_wq_failed:
	kfree(ts);
err_alloc_data_failed:
err_check_functionality_failed:
	if (LDO != NULL) {
		regulator_disable(LDO);
		regulator_put(LDO);
	}
	return ret;
}

static int atmel_ts_remove(struct i2c_client *client)
{
	struct atmel_ts_data *ts = i2c_get_clientdata(client);

	free_irq(client->irq, ts);

	destroy_workqueue(ts->atmel_wq);
	input_unregister_device(ts->input_dev);
	kfree(ts);

	regulator_disable(LDO);
	regulator_put(LDO);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int atmel_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct atmel_ts_data *ts = i2c_get_clientdata(client);
	struct atmel_i2c_platform_data *pdata = ts->pdata;
	uint8_t data[7];
	int ret = 0;

	mutex_lock(&ts->lock);
	ts->finger_pressed = 0;
	ts->finger_count = 0;
	ts->first_pressed = 0;

	if (ts->id->version >= 0x10) {
		ts->pre_data[0] = 0;
		ret = i2c_atmel_write(ts->client,
				get_object_address(ts, GEN_ACQUISITIONCONFIG_T8) + T8_CFG_ATCHCALST,
				ts->ATCH_EXT, 4);
		if (ret < 0)
			pr_err("k3ts, %s: failed to write config T8\n", __func__);
	}

	ret = i2c_atmel_write_byte_data(client,
			get_object_address(ts, GEN_POWERCONFIG_T7) + T7_CFG_IDLEACQINT, 0x0);
	if (ret < 0)
		pr_err("k3ts, %s: failed to write config T7\n", __func__);

	ret = i2c_atmel_write_byte_data(client,
			get_object_address(ts, GEN_POWERCONFIG_T7) + T7_CFG_ACTVACQINT, 0x0);
	if (ret < 0)
		pr_err("k3ts, %s: failed to write config T7\n", __func__);

	/* Read T5 until gpio_irq is HIGH level */
	if (!gpio_get_value(pdata->gpio_irq)) {
		ret = i2c_atmel_read(ts->client, get_object_address(ts,
			GEN_MESSAGEPROCESSOR_T5), data, 7);
		if (ret < 0) {
			pr_err("k3ts, %s: failed to read T5\n", __func__);
		}
	}

	mutex_unlock(&ts->lock);

	pr_info("[%s]: -\n", __func__);
	return 0;
}

static int atmel_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct atmel_ts_data *ts = i2c_get_clientdata(client);
	int ret = 0;

	pr_info("[%s]: +\n", __func__);

	mutex_lock(&ts->lock);
	if (ts->id->version >= 0x10)
		ts->timestamp = jiffies;

	ts->unlock_flag = 0;

	ret = i2c_atmel_write(ts->client,
			get_object_address(ts, GEN_POWERCONFIG_T7),
			ts->config_setting[ts->status].config_T7,
			get_object_size(ts, GEN_POWERCONFIG_T7));
	if (ret < 0)
		pr_err("k3ts, %s: failed to write config T7\n", __func__);

	ts->calibration_confirm = 0;
	msleep(1);

	ret = i2c_atmel_write(ts->client,
			get_object_address(ts, GEN_ACQUISITIONCONFIG_T8) +
			T8_CFG_TCHAUTOCAL, ts->ATCH_NOR, 6);
	if (ret < 0)
		pr_err("k3ts, %s: failed to write config T8\n", __func__);

	ret = i2c_atmel_write_byte_data(client,
			get_object_address(ts, GEN_COMMANDPROCESSOR_T6) +
			T6_CFG_CALIBRATE, 0x55);
	if (ret < 0)
		pr_err("k3ts, %s: failed to write config T6\n", __func__);

	mutex_unlock(&ts->lock);

	pr_info("[%s]: -\n", __func__);

	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(atmel_ts_pm_ops, atmel_ts_suspend, atmel_ts_resume);

static const struct i2c_device_id atml_ts_i2c_id[] = {
	{ ATMEL_MXT224E_NAME, 0 },
	{ }
};

#ifdef CONFIG_OF
static const struct of_device_id atmel_ts_dt_ids[] = {
	{ .compatible = "atmel,ts-mxt224e", },
	{ }
};
MODULE_DEVICE_TABLE(of, atmel_ts_dt_ids);
#endif

static struct i2c_driver atmel_ts_driver = {
	.id_table = atml_ts_i2c_id,
	.probe = atmel_ts_probe,
	.remove = atmel_ts_remove,
	.driver = {
		.of_match_table = of_match_ptr(atmel_ts_dt_ids),
		.name = ATMEL_MXT224E_NAME,
		.pm	= &atmel_ts_pm_ops,
	},
};
module_i2c_driver(atmel_ts_driver);

MODULE_DESCRIPTION("ATMEL Touch driver");
MODULE_LICENSE("GPL");
