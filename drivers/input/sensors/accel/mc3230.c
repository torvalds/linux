/*
 *  MCube mc3230 acceleration sensor driver
 *
 *  Copyright (C) 2011 MCube Inc.,
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <linux/sensor-dev.h>
#include <linux/mc3230.h>
#include <linux/wakelock.h>

#define MITECH_SENSOR_DBG

static int sensor_active(struct i2c_client *client, int enable, int rate);
#define MC32X0_XOUT_REG				0x00
#define MC32X0_YOUT_REG				0x01
#define MC32X0_ZOUT_REG				0x02
#define MC32X0_Tilt_Status_REG			0x03
#define MC32X0_Sampling_Rate_Status_REG		0x04
#define MC32X0_Sleep_Count_REG			0x05
#define MC32X0_Interrupt_Enable_REG		0x06
#define MC32X0_Mode_Feature_REG			0x07
#define MC32X0_Sample_Rate_REG			0x08
#define MC32X0_Tap_Detection_Enable_REG		0x09
#define MC32X0_TAP_Dwell_Reject_REG		0x0a
#define MC32X0_DROP_Control_Register_REG	0x0b
#define MC32X0_SHAKE_Debounce_REG		0x0c
#define MC32X0_XOUT_EX_L_REG			0x0d
#define MC32X0_XOUT_EX_H_REG			0x0e
#define MC32X0_YOUT_EX_L_REG			0x0f
#define MC32X0_YOUT_EX_H_REG			0x10
#define MC32X0_ZOUT_EX_L_REG			0x11
#define MC32X0_ZOUT_EX_H_REG			0x12
#define MC32X0_CHIP_ID_REG			0x18
#define MC32X0_RANGE_Control_REG		0x20
#define MC32X0_SHAKE_Threshold_REG		0x2B
#define MC32X0_UD_Z_TH_REG			0x2C
#define MC32X0_UD_X_TH_REG			0x2D
#define MC32X0_RL_Z_TH_REG			0x2E
#define MC32X0_RL_Y_TH_REG			0x2F
#define MC32X0_FB_Z_TH_REG			0x30
#define MC32X0_DROP_Threshold_REG		0x31
#define MC32X0_TAP_Threshold_REG		0x32
#define MC32X0_MODE_SLEEP			0x00
#define MC32X0_MODE_WAKEUP			0x01
#define MODE_CHANGE_DELAY_MS			100
#define MC3230_MODE_MITECH			0X58

#define MC3230_MODE_BITS			0x03

#define MC3230_PRECISION			8
#define MC3230_RANGE				1500000
#define MC3210_RANGE				8000000
#define MC3230_BOUNDARY		(0x1 << (MC3230_PRECISION - 1))
#define MC3230_GRAVITY_STEPS	(MC3230_RANGE/MC3230_BOUNDARY)

#define MC3236_RANGE				2000000
#define MC3236_GRAVITY_STEP	(MC3236_RANGE/MC3230_BOUNDARY)

/* 8bit data */
#define MC3210_PRECISION	14
#define MC3210_BOUNDARY		(0x1 << (MC3210_PRECISION - 1))
/* 110 2g full scale range */
#define MC3210_GRAVITY_STEP	(MC3210_RANGE/MC3210_BOUNDARY)

/* rate */
#define MC3230_RATE_1		0x07
#define MC3230_RATE_2		0x06
#define MC3230_RATE_4		0x05
#define MC3230_RATE_8		0x04
#define MC3230_RATE_16		0x03
#define MC3230_RATE_32		0x02
#define	MC3230_RATE_64		0x01
#define MC3230_RATE_120		0x00

#define MC32X0_AXIS_X		0
#define MC32X0_AXIS_Y		1
#define MC32X0_AXIS_Z		2
#define MC32X0_AXES_NUM		3
#define MC32X0_DATA_LEN		6
#define MC32X0_DEV_NAME		"MC32X0"
#define GRAVITY_EARTH_1000	9807
#define IS_MC3230		1
#define IS_MC3210		2
#define IS_MC2234		3
#define IS_MC3236		4

static const char backup_calib_path[] = "/data/misc/mcube-calib.txt";
static const char calib_path[] =
			"/data/data/com.mcube.acc/files/mcube-calib.txt";
static char backup_buf[64];

static GSENSOR_VECTOR3D gsensor_gain;

static struct file *fd_file;
static int load_cali_flg;
static bool READ_FROM_BACKUP;
static mm_segment_t oldfs;
static unsigned char offset_buf[9];
static signed int offset_data[3];
static s16 G_RAW_DATA[3];
static signed int gain_data[3];
static signed int enable_RBM_calibration;
static unsigned char mc32x0_type;
static int g_value;

#define mcprintkreg(x...)
#define mcprintkfunc(x...)

#define GSE_TAG			"[Gsensor] "
#define GSE_FUN(f)		pr_info(GSE_TAG"%s\n", __func__)
#define GSE_ERR(fmt, args...)	pr_info(GSE_TAG"%s %d : "fmt, \
				       __func__, __LINE__, ##args)
#define GSE_LOG(fmt, args...)	pr_info(GSE_TAG fmt, ##args)

#define MC3230_SPEED		(200 * 1000)
#define MC3230_DEVID		0x01

/* Addresses to scan -- protected by sense_data_mutex */
static struct i2c_client *this_client;

#ifdef CONFIG_HAS_EARLYSUSPEND
static struct early_suspend mc3230_early_suspend;
#endif

/* status */
#define MC3230_OPEN           1
#define MC3230_CLOSE          0

struct hwmsen_convert {
	s8 sign[3];
	u8 map[3];
};

struct mc3230_data {
	struct sensor_private_data *g_sensor_private_data;

	char status;
	char curr_rate;

	/* +1: for 4-byte alignment */
	s16 offset[MC32X0_AXES_NUM + 1];
	s16 data[MC32X0_AXES_NUM + 1];
	s16 cali_sw[MC32X0_AXES_NUM + 1];

	struct hwmsen_convert cvt;
};

static int MC32X0_WriteCalibration(struct i2c_client *client,
				   int dat[MC32X0_AXES_NUM]);

static int mc3230_write_reg(struct i2c_client *client, int addr, int value);
static int mc3230_read_block(struct i2c_client *client, char reg, char *rxData,
			     int length);
static int mc3230_active(struct i2c_client *client, int enable);
static void MC32X0_rbm(struct i2c_client *client, int enable);
static int init_3230_ctl_data(struct i2c_client *client);

struct file *openFile(const char *path, int flag, int mode)
{
	struct file *fp;

	fp = filp_open(path, flag, mode);
	if (IS_ERR(fp) || !fp->f_op)
		return NULL;
	else
		return fp;
}

static int readFile(struct file *fp, char *buf, int readlen)
{
	if (fp->f_op && fp->f_op->read)
		return fp->f_op->read(fp, buf, readlen, &fp->f_pos);
	else
		return -1;
}

static int writeFile(struct file *fp, char *buf, int writelen)
{
	if (fp->f_op && fp->f_op->write)
		return fp->f_op->write(fp, buf, writelen, &fp->f_pos);
	else
		return -1;
}

static int closeFile(struct file *fp)
{
	filp_close(fp, NULL);
	return 0;
}

static void initKernelEnv(void)
{
	oldfs = get_fs();
	set_fs(KERNEL_DS);
}

static struct mc3230_data g_mc3230_data = { 0 };
static struct mc3230_data *get_3230_ctl_data(void)
{
	return &g_mc3230_data;
}

static int mcube_read_cali_file(struct i2c_client *client)
{
	int cali_data[3];
	int err = 0;

	READ_FROM_BACKUP = false;
	initKernelEnv();

	fd_file =
	    openFile("/data/data/com.mcube.acc/files/mcube-calib.txt", 0, 0);

	if (!fd_file) {
		fd_file = openFile(backup_calib_path, O_RDONLY, 0);
		if (fd_file)
			READ_FROM_BACKUP = true;
	}

	if (!fd_file) {
		cali_data[0] = 0;
		cali_data[1] = 0;
		cali_data[2] = 0;
		return 1;
	} else {
		memset(backup_buf, 0, 64);
		err = readFile(fd_file, backup_buf, 128);
		if (err > 0)
			GSE_LOG("buf:%s\n", backup_buf);
		else
			GSE_LOG("read file error %d\n", err);

		set_fs(oldfs);
		closeFile(fd_file);

		sscanf(backup_buf, "%d %d %d", &cali_data[MC32X0_AXIS_X],
		       &cali_data[MC32X0_AXIS_Y], &cali_data[MC32X0_AXIS_Z]);
		GSE_LOG("cali_data: %d %d %d\n", cali_data[MC32X0_AXIS_X],
			cali_data[MC32X0_AXIS_Y], cali_data[MC32X0_AXIS_Z]);

		MC32X0_WriteCalibration(client, cali_data);
	}
	return 0;
}

static void MC32X0_rbm(struct i2c_client *client, int enable)
{
	int err;

	if (enable == 1) {
		err = mc3230_write_reg(client, 0x07, 0x43);
		err = mc3230_write_reg(client, 0x14, 0x02);
		err = mc3230_write_reg(client, 0x07, 0x41);

		enable_RBM_calibration = 1;

		GSE_LOG("set rbm!!\n");

		msleep(220);
	} else if (enable == 0) {
		err = mc3230_write_reg(client, 0x07, 0x43);
		err = mc3230_write_reg(client, 0x14, 0x00);
		err = mc3230_write_reg(client, 0x07, 0x41);
		enable_RBM_calibration = 0;

		GSE_LOG("clear rbm!!\n");

		msleep(220);
	}
}

static int MC32X0_ReadData_RBM(struct i2c_client *client,
			       int data[MC32X0_AXES_NUM])
{
	u8 addr = 0x0d;
	u8 rbm_buf[MC32X0_DATA_LEN] = { 0 };
	int err = 0;

	if (!client) {
		err = -EINVAL;
		return err;
	}

	err = mc3230_read_block(client, addr, rbm_buf, 0x06);

	data[MC32X0_AXIS_X] = (s16)((rbm_buf[0]) | (rbm_buf[1] << 8));
	data[MC32X0_AXIS_Y] = (s16)((rbm_buf[2]) | (rbm_buf[3] << 8));
	data[MC32X0_AXIS_Z] = (s16)((rbm_buf[4]) | (rbm_buf[5] << 8));

	GSE_LOG("rbm_buf<<<<<[%02x %02x %02x %02x %02x %02x]\n", rbm_buf[0],
		rbm_buf[2], rbm_buf[2], rbm_buf[3], rbm_buf[4], rbm_buf[5]);
	GSE_LOG("RBM<<<<<[%04x %04x %04x]\n", data[MC32X0_AXIS_X],
		data[MC32X0_AXIS_Y], data[MC32X0_AXIS_Z]);
	GSE_LOG("RBM<<<<<[%04d %04d %04d]\n", data[MC32X0_AXIS_X],
		data[MC32X0_AXIS_Y], data[MC32X0_AXIS_Z]);
	return err;
}

static int mc3230_read_block(struct i2c_client *client, char reg, char *rxData,
			     int length)
{
	int ret = 0;

	*rxData = reg;
	ret = sensor_rx_data(client, rxData, length);
	return ret;
}

static int mc3230_write_reg(struct i2c_client *client, int addr, int value)
{
	char buffer[3];
	int ret = 0;

	buffer[0] = addr;
	buffer[1] = value;
	ret = sensor_tx_data(client, &buffer[0], 2);
	return ret;
}

static int mc3230_active(struct i2c_client *client, int enable)
{
	int tmp;
	int ret = 0;

	if (enable)
		tmp = 0x01;
	else
		tmp = 0x03;
	mcprintkreg("mc3230_active %s (0x%x)\n", enable ? "active" : "standby",
		    tmp);
	ret = mc3230_write_reg(client, MC3230_REG_SYSMOD, tmp);
	return ret;
}

static int mc3230_reg_init(struct i2c_client *client)
{
	int ret = 0;
	int pcode = 0;

	/* 1: awake  0: standby */
	mc3230_active(client, 0);

	pcode = sensor_read_reg(client, MC3230_REG_PRODUCT_CODE);
	printk(KERN_INFO "mc3230_reg_init pcode=%x\n", pcode);
	if ((pcode == 0x19) || (pcode == 0x29)) {
		mc32x0_type = IS_MC3230;
	} else if ((pcode == 0x90) || (pcode == 0xA8) || (pcode == 0x88)) {
		mc32x0_type = IS_MC3210;
	} else if (pcode == 0x59) {
		mc32x0_type = IS_MC2234;
	}
	if ((pcode & 0xF1) == 0x60) {
		mc32x0_type = IS_MC3236;
	}

	GSE_LOG
	    ("MC3230 1, MC3210 2, MC2234 3, MC3236 4 : mc32x0_type=%d\n",
	     mc32x0_type);

	if ((mc32x0_type == IS_MC3230) || (mc32x0_type == IS_MC2234)) {
		ret = sensor_write_reg(client, 0x20, 0x32);
	} else if (mc32x0_type == IS_MC3236) {
		ret = sensor_write_reg(client, 0x20, 0x02);
	} else if (mc32x0_type == IS_MC3210) {
		ret = sensor_write_reg(client, 0x20, 0x3F);
	}

	if ((mc32x0_type == IS_MC3230) || (mc32x0_type == IS_MC2234)) {
		gsensor_gain.x = gsensor_gain.y = gsensor_gain.z = 86;
	} else if (mc32x0_type == IS_MC3236) {
		gsensor_gain.x = gsensor_gain.y = gsensor_gain.z = 64;
	} else if (mc32x0_type == IS_MC3210) {
		gsensor_gain.x = gsensor_gain.y = gsensor_gain.z = 1024;
	}

	return ret;
}
static int init_3230_ctl_data(struct i2c_client *client)
{
	int err;
	s16 tmp, x_gain, y_gain, z_gain;
	s32 x_off, y_off, z_off;
	struct mc3230_data *mc3230 = get_3230_ctl_data();

	load_cali_flg = 30;

	mcprintkfunc("%s enter\n", __func__);

	this_client = client;

	mc3230->g_sensor_private_data =
	    (struct sensor_private_data *)i2c_get_clientdata(client);
	mc3230->curr_rate = MC3230_RATE_16;
	mc3230->status = MC3230_CLOSE;
	mc3230->cvt.sign[MC32X0_AXIS_X] = 1;
	mc3230->cvt.sign[MC32X0_AXIS_Y] = 1;
	mc3230->cvt.sign[MC32X0_AXIS_Z] = 1;
	mc3230->cvt.map[MC32X0_AXIS_X] = 0;
	mc3230->cvt.map[MC32X0_AXIS_Y] = 1;
	mc3230->cvt.map[MC32X0_AXIS_Z] = 2;

	sensor_write_reg(client, 0x1b, 0x6d);
	sensor_write_reg(client, 0x1b, 0x43);
	msleep(5);

	sensor_write_reg(client, 0x07, 0x43);
	sensor_write_reg(client, 0x1C, 0x80);
	sensor_write_reg(client, 0x17, 0x80);
	msleep(5);
	sensor_write_reg(client, 0x1C, 0x00);
	sensor_write_reg(client, 0x17, 0x00);
	msleep(5);

	memset(offset_buf, 0, 9);
	offset_buf[0] = 0x21;
	err = sensor_rx_data(client, offset_buf, 9);
	if (err) {
		GSE_ERR("error: %d\n", err);
		return err;
	}

	tmp = ((offset_buf[1] & 0x3f) << 8) + offset_buf[0];
	if (tmp & 0x2000)
		tmp |= 0xc000;
	x_off = tmp;

	tmp = ((offset_buf[3] & 0x3f) << 8) + offset_buf[2];
	if (tmp & 0x2000)
		tmp |= 0xc000;
	y_off = tmp;

	tmp = ((offset_buf[5] & 0x3f) << 8) + offset_buf[4];
	if (tmp & 0x2000)
		tmp |= 0xc000;
	z_off = tmp;

	/* get x,y,z gain */
	x_gain = ((offset_buf[1] >> 7) << 8) + offset_buf[6];
	y_gain = ((offset_buf[3] >> 7) << 8) + offset_buf[7];
	z_gain = ((offset_buf[5] >> 7) << 8) + offset_buf[8];

	/* storege the cerrunt offset data with DOT format */
	offset_data[0] = x_off;
	offset_data[1] = y_off;
	offset_data[2] = z_off;

	/* storege the cerrunt Gain data with GOT format */
	gain_data[0] = 256 * 8 * 128 / 3 / (40 + x_gain);
	gain_data[1] = 256 * 8 * 128 / 3 / (40 + y_gain);
	gain_data[2] = 256 * 8 * 128 / 3 / (40 + z_gain);

	mc3230_reg_init(this_client);

	return 0;
}

static int mc3230_start_dev(struct i2c_client *client, char rate)
{
	int ret = 0;
	struct mc3230_data *mc3230 = get_3230_ctl_data();

	/* standby */
	mc3230_active(client, 0);
	mcprintkreg("mc3230 MC3230_REG_SYSMOD:%x\n",
		    mc3230_read_reg(client, MC3230_REG_SYSMOD));

	/*data rate */
	ret = mc3230_write_reg(client, MC3230_REG_RATE_SAMP, rate);
	mc3230->curr_rate = rate;
	mcprintkreg("mc3230 MC3230_REG_RATE_SAMP:%x  rate=%d\n",
		    mc3230_read_reg(client, MC3230_REG_RATE_SAMP), rate);
	/*wake */
	mc3230_active(client, 1);
	mcprintkreg("mc3230 MC3230_REG_SYSMOD:%x\n",
		    mc3230_read_reg(client, MC3230_REG_SYSMOD));

	return ret;
}

static int mc3230_start(struct i2c_client *client, char rate)
{
	struct mc3230_data *mc3230 = get_3230_ctl_data();

	if (mc3230->status == MC3230_OPEN)
		return 0;

	mc3230->status = MC3230_OPEN;
	rate = 0;
	return mc3230_start_dev(client, rate);
}

static inline int mc3230_convert_to_int(s16 value)
{
	int result;

	if ((mc32x0_type == IS_MC3230) || (mc32x0_type == IS_MC2234)) {
		result = value * 192;
	} else if (mc32x0_type == IS_MC3236) {
		result = value * 256;
	} else if (mc32x0_type == IS_MC3210) {
		result = value * 16;
	}

	return result;
}

static void mc3230_report_value(struct i2c_client *client,
				struct sensor_axis *axis)
{
	struct sensor_private_data *mc3230 = i2c_get_clientdata(client);

	if (mc3230->status_cur == SENSOR_OFF)
		return;

	if (mc32x0_type == IS_MC2234) {
		input_report_abs(mc3230->input_dev, ABS_X, (axis->x));
		input_report_abs(mc3230->input_dev, ABS_Y, -(axis->y));
		input_report_abs(mc3230->input_dev, ABS_Z, (axis->z));
	} else if (mc32x0_type == IS_MC3236) {
		input_report_abs(mc3230->input_dev, ABS_X, -(axis->x));
		input_report_abs(mc3230->input_dev, ABS_Y, (axis->y));
		input_report_abs(mc3230->input_dev, ABS_Z, -(axis->z));
	} else {
		input_report_abs(mc3230->input_dev, ABS_X, (axis->y));
		input_report_abs(mc3230->input_dev, ABS_Y, (axis->x));
		input_report_abs(mc3230->input_dev, ABS_Z, (axis->z));
	}

	input_sync(mc3230->input_dev);
}

static int MC32X0_ReadData(struct i2c_client *client,
			   s16 buffer[MC32X0_AXES_NUM]);

static int mc3230_get_data(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	struct sensor_platform_data *pdata = sensor->pdata;
	s16 buffer[6];
	int ret;
	int x, y, z;
	int value = 0;
	static int flag;
	struct sensor_axis axis;

	if (load_cali_flg > 0) {
		ret = mcube_read_cali_file(client);
		if (ret == 0)
			load_cali_flg = ret;
		else
			load_cali_flg--;
	}
	ret = MC32X0_ReadData(client, buffer);
	if (ret) {
		GSE_ERR("%s I2C error: ret value=%d", __func__, ret);
		return -EIO;
	}

	value = sensor_read_reg(client, 0x20);

	if (value == 0x00) {
		static int cnt;

		if (cnt++ >= 20) {
			sensor_active(client, 1, 0xff);
			cnt = 0;
		}
		g_value = 4;
	} else if (value == 0x01) {
		g_value = 2;
	} else
		g_value = 1;

	x = mc3230_convert_to_int(buffer[0]) * g_value;
	y = mc3230_convert_to_int(buffer[1]) * g_value;
	z = mc3230_convert_to_int(buffer[2]) * g_value;

	axis.x =
	    (pdata->orientation[0]) * x + (pdata->orientation[1]) * y +
	    (pdata->orientation[2]) * z;
	axis.y =
	    (pdata->orientation[3]) * x + (pdata->orientation[4]) * y +
	    (pdata->orientation[5]) * z;
	axis.z =
	    (pdata->orientation[6]) * x + (pdata->orientation[7]) * y +
	    (pdata->orientation[8]) * z;

	/* input dev will ignore report data if data value is the same with last_value,
		sample rate will not enough by this way, so just avoid this case */
	if ((sensor->axis.x == axis.x) && (sensor->axis.y == axis.y) && (sensor->axis.z == axis.z)) {
		if (flag) {
			flag = 0;
			axis.x += 1;
			axis.y += 1;
			axis.z += 1;
		} else {
			flag = 1;
			axis.x -= 1;
			axis.y -= 1;
			axis.z -= 1;
		}
	}

	mc3230_report_value(client, &axis);

	mutex_lock(&sensor->data_mutex);
	sensor->axis = axis;
	mutex_unlock(&sensor->data_mutex);

	return 0;
}

static int MC32X0_ReadRBMData(struct i2c_client *client, char *buf)
{
	struct mc3230_data *mc3230 =
	    (struct mc3230_data *)i2c_get_clientdata(client);
	int res = 0;
	int data[3];

	if (!buf || !client)
		return -EINVAL;

	if (mc3230->status == MC3230_CLOSE) {
		res = mc3230_start(client, 0);
		if (res)
			GSE_ERR("Power on mc32x0 error %d!\n", res);
	}
	res = MC32X0_ReadData_RBM(client, data);
	if (res) {
		GSE_ERR("%s I2C error: ret value=%d", __func__, res);
		return -EIO;
	} else {
		sprintf(buf, "%04x %04x %04x", data[MC32X0_AXIS_X],
			data[MC32X0_AXIS_Y], data[MC32X0_AXIS_Z]);
	}

	return 0;
}

static int MC32X0_ReadOffset(struct i2c_client *client,
			     s16 ofs[MC32X0_AXES_NUM])
{
	int err;
	u8 off_data[6];

	off_data[0] = MC32X0_XOUT_EX_L_REG;
	if (mc32x0_type == IS_MC3210) {
		err = sensor_rx_data(client, off_data, MC32X0_DATA_LEN);
		if (err) {
			GSE_ERR("error: %d\n", err);
			return err;
		}
		ofs[MC32X0_AXIS_X] =
		    ((s16)(off_data[0])) | ((s16)(off_data[1]) << 8);
		ofs[MC32X0_AXIS_Y] =
		    ((s16)(off_data[2])) | ((s16)(off_data[3]) << 8);
		ofs[MC32X0_AXIS_Z] =
		    ((s16)(off_data[4])) | ((s16)(off_data[5]) << 8);
	} else if ((mc32x0_type == IS_MC3230) || (mc32x0_type == IS_MC2234)) {
		err = sensor_rx_data(client, off_data, MC32X0_DATA_LEN);
		if (err) {
			GSE_ERR("error: %d\n", err);
			return err;
		}
		ofs[MC32X0_AXIS_X] = (s8)off_data[0];
		ofs[MC32X0_AXIS_Y] = (s8)off_data[1];
		ofs[MC32X0_AXIS_Z] = (s8)off_data[2];
	}
	GSE_LOG("MC32X0_ReadOffset %d %d %d \n", ofs[MC32X0_AXIS_X],
			ofs[MC32X0_AXIS_Y], ofs[MC32X0_AXIS_Z]);

	return 0;
}

static int MC32X0_ResetCalibration(struct i2c_client *client)
{
	struct mc3230_data *mc3230 = get_3230_ctl_data();
	s16 tmp, i;

	sensor_write_reg(client, 0x07, 0x43);

	for (i = 0; i < 6; i++) {
		sensor_write_reg(client, 0x21 + i, offset_buf[i]);
		msleep(10);
	}

	sensor_write_reg(client, 0x07, 0x41);

	msleep(20);

	/* add by Liang for set offset_buf as OTP value */
	tmp = ((offset_buf[1] & 0x3f) << 8) + offset_buf[0];
	if (tmp & 0x2000)
		tmp |= 0xc000;
	offset_data[0] = tmp;

	tmp = ((offset_buf[3] & 0x3f) << 8) + offset_buf[2];
	if (tmp & 0x2000)
		tmp |= 0xc000;
	offset_data[1] = tmp;

	tmp = ((offset_buf[5] & 0x3f) << 8) + offset_buf[4];
	if (tmp & 0x2000)
		tmp |= 0xc000;
	offset_data[2] = tmp;

	memset(mc3230->cali_sw, 0x00, sizeof(mc3230->cali_sw));

	return 0;
}

static int MC32X0_ReadCalibration(struct i2c_client *client,
				  int dat[MC32X0_AXES_NUM])
{
	struct mc3230_data *mc3230 = get_3230_ctl_data();
	int err;

	err = MC32X0_ReadOffset(client, mc3230->offset);
	if (err) {
		GSE_ERR("read offset fail, %d\n", err);
		return err;
	}

	dat[MC32X0_AXIS_X] = mc3230->offset[MC32X0_AXIS_X];
	dat[MC32X0_AXIS_Y] = mc3230->offset[MC32X0_AXIS_Y];
	dat[MC32X0_AXIS_Z] = mc3230->offset[MC32X0_AXIS_Z];

	return 0;
}

static int MC32X0_WriteCalibration(struct i2c_client *client,
				   int dat[MC32X0_AXES_NUM])
{
	int err;
	u8 buf[9], i;
	s16 tmp, x_gain, y_gain, z_gain;
	s32 x_off, y_off, z_off;

	GSE_LOG("UPDATE dat: (%+3d %+3d %+3d)\n",
		dat[MC32X0_AXIS_X], dat[MC32X0_AXIS_Y], dat[MC32X0_AXIS_Z]);

	/* read register 0x21~0x28 */
	buf[0] = 0x21;
	err = sensor_rx_data(client, &buf[0], 3);
	buf[3] = 0x24;
	err = sensor_rx_data(client, &buf[3], 3);
	buf[6] = 0x27;
	err = sensor_rx_data(client, &buf[6], 3);

	/* get x,y,z offset */
	tmp = ((buf[1] & 0x3f) << 8) + buf[0];
	if (tmp & 0x2000)
		tmp |= 0xc000;
	x_off = tmp;

	tmp = ((buf[3] & 0x3f) << 8) + buf[2];
	if (tmp & 0x2000)
		tmp |= 0xc000;
	y_off = tmp;

	tmp = ((buf[5] & 0x3f) << 8) + buf[4];
	if (tmp & 0x2000)
		tmp |= 0xc000;
	z_off = tmp;

	/* get x,y,z gain */
	x_gain = ((buf[1] >> 7) << 8) + buf[6];
	y_gain = ((buf[3] >> 7) << 8) + buf[7];
	z_gain = ((buf[5] >> 7) << 8) + buf[8];

	/* prepare new offset */
	x_off =
	    x_off +
	    16 * dat[MC32X0_AXIS_X] * 256 * 128 / 3 / gsensor_gain.x / (40 +
									x_gain);
	y_off =
	    y_off +
	    16 * dat[MC32X0_AXIS_Y] * 256 * 128 / 3 / gsensor_gain.y / (40 +
									y_gain);
	z_off =
	    z_off +
	    16 * dat[MC32X0_AXIS_Z] * 256 * 128 / 3 / gsensor_gain.z / (40 +
									z_gain);

	/* storege the cerrunt offset data with DOT format */
	offset_data[0] = x_off;
	offset_data[1] = y_off;
	offset_data[2] = z_off;

	/* storege the cerrunt Gain data with GOT format */
	gain_data[0] = 256 * 8 * 128 / 3 / (40 + x_gain);
	gain_data[1] = 256 * 8 * 128 / 3 / (40 + y_gain);
	gain_data[2] = 256 * 8 * 128 / 3 / (40 + z_gain);

	sensor_write_reg(client, 0x07, 0x43);

	buf[0] = x_off & 0xff;
	buf[1] = ((x_off >> 8) & 0x3f) | (x_gain & 0x0100 ? 0x80 : 0);
	buf[2] = y_off & 0xff;
	buf[3] = ((y_off >> 8) & 0x3f) | (y_gain & 0x0100 ? 0x80 : 0);
	buf[4] = z_off & 0xff;
	buf[5] = ((z_off >> 8) & 0x3f) | (z_gain & 0x0100 ? 0x80 : 0);

	for (i = 0; i < 6; i++) {
		sensor_write_reg(client, 0x21 + i, buf[i]);
		msleep(10);
	}

	sensor_write_reg(client, 0x07, 0x41);

	return err;
}

static int MC32X0_ReadData(struct i2c_client *client,
			   s16 buffer[MC32X0_AXES_NUM])
{
	s8 buf[3];
	u8 buf1[6];
	char rbm_buf[6];
	int ret;
	int err = 0;

	int tempX = 0;
	int tempY = 0;
	int tempZ = 0;

	if (!client) {
		err = -EINVAL;
		return err;
	}
	mcprintkfunc("MC32X0_ReadData enable_RBM_calibration = %d\n",
		     enable_RBM_calibration);
	if (enable_RBM_calibration == 0) {
		;
	} else if (enable_RBM_calibration == 1) {
		memset(rbm_buf, 0, 3);
		rbm_buf[0] = MC3230_REG_RBM_DATA;
		ret = sensor_rx_data(client, &rbm_buf[0], 2);
		rbm_buf[2] = MC3230_REG_RBM_DATA + 2;
		ret = sensor_rx_data(client, &rbm_buf[2], 2);
		rbm_buf[4] = MC3230_REG_RBM_DATA + 4;
		ret = sensor_rx_data(client, &rbm_buf[4], 2);
	}

	mcprintkfunc("MC32X0_ReadData %d %d %d %d %d %d\n", rbm_buf[0],
		     rbm_buf[1], rbm_buf[2], rbm_buf[3], rbm_buf[4],
		     rbm_buf[5]);
	if (enable_RBM_calibration == 0) {
		do {
			memset(buf, 0, 3);
			buf[0] = MC3230_REG_X_OUT;
			ret = sensor_rx_data(client, &buf[0], 3);
			if (ret < 0)
				return ret;
		} while (0);

		buffer[0] = (s16)buf[0];
		buffer[1] = (s16)buf[1];
		buffer[2] = (s16)buf[2];

		if (mc32x0_type == IS_MC2234) {
			tempX = buffer[MC32X0_AXIS_X];
			tempY = buffer[MC32X0_AXIS_Y];
			tempZ = buffer[MC32X0_AXIS_Z];

			buffer[MC32X0_AXIS_Z] =
				(s8)(gsensor_gain.z - (abs(tempX) + abs(tempY)));
		}

		else if (mc32x0_type == IS_MC3210) {
			do {
				memset(buf1, 0, 6);
				buf[0] = MC32X0_XOUT_EX_L_REG;

				buf1[0] =
				    sensor_read_reg(client,
						    MC32X0_XOUT_EX_L_REG);
				buf1[1] =
				    sensor_read_reg(client,
						    MC32X0_XOUT_EX_L_REG + 1);
				buf1[2] =
				    sensor_read_reg(client,
						    MC32X0_XOUT_EX_L_REG + 2);
				buf1[3] =
				    sensor_read_reg(client,
						    MC32X0_XOUT_EX_L_REG + 3);
				buf1[4] =
				    sensor_read_reg(client,
						    MC32X0_XOUT_EX_L_REG + 4);
				buf1[5] =
				    sensor_read_reg(client,
						    MC32X0_XOUT_EX_L_REG + 5);
			} while (0);

			buffer[0] = (signed short)((buf1[0]) | (buf1[1] << 8));
			buffer[1] = (signed short)((buf1[2]) | (buf1[3] << 8));
			buffer[2] = (signed short)((buf1[4]) | (buf1[5] << 8));
		}

	} else if (enable_RBM_calibration == 1) {
		buffer[MC32X0_AXIS_X] =
			(s16)((rbm_buf[0]) | (rbm_buf[1] << 8));
		buffer[MC32X0_AXIS_Y] =
			(s16)((rbm_buf[2]) | (rbm_buf[3] << 8));
		buffer[MC32X0_AXIS_Z] =
			(s16)((rbm_buf[4]) | (rbm_buf[5] << 8));

		mcprintkfunc("%s RBM<<<<<[%08d %08d %08d]\n", __func__,
			     buffer[MC32X0_AXIS_X], buffer[MC32X0_AXIS_Y],
			     buffer[MC32X0_AXIS_Z]);
		if (gain_data[0] == 0) {
			buffer[MC32X0_AXIS_X] = 0;
			buffer[MC32X0_AXIS_Y] = 0;
			buffer[MC32X0_AXIS_Z] = 0;
			return 0;
		}
		buffer[MC32X0_AXIS_X] =
		    (buffer[MC32X0_AXIS_X] +
		     offset_data[0] / 2) * gsensor_gain.x / gain_data[0];
		buffer[MC32X0_AXIS_Y] =
		    (buffer[MC32X0_AXIS_Y] +
		     offset_data[1] / 2) * gsensor_gain.y / gain_data[1];
		buffer[MC32X0_AXIS_Z] =
		    (buffer[MC32X0_AXIS_Z] +
		     offset_data[2] / 2) * gsensor_gain.z / gain_data[2];

		if (mc32x0_type == IS_MC2234) {
			tempX = buffer[MC32X0_AXIS_X];
			tempY = buffer[MC32X0_AXIS_Y];
			tempZ = buffer[MC32X0_AXIS_Z];

			buffer[MC32X0_AXIS_Z] =
				(s16)(gsensor_gain.z - (abs(tempX) + abs(tempY)));
		}
	}

	return 0;
}

static int MC32X0_ReadRawData(struct i2c_client *client, char *buf)
{
	struct mc3230_data *obj = get_3230_ctl_data();
	int res = 0;
	s16 raw_buf[3];

	if (!buf || !client)
		return -EINVAL;

	if (obj->status == MC3230_CLOSE) {
		res = mc3230_start(client, 0);
		if (res)
			GSE_ERR("Power on mc32x0 error %d!\n", res);
	}
	res = MC32X0_ReadData(client, &raw_buf[0]);
	if (res) {
		GSE_LOG("%s %d\n", __func__, __LINE__);
		GSE_ERR("I2C error: ret value=%d", res);
		return -EIO;
	} else {
		GSE_LOG("UPDATE dat: (%+3d %+3d %+3d)\n",
			raw_buf[MC32X0_AXIS_X], raw_buf[MC32X0_AXIS_Y],
			raw_buf[MC32X0_AXIS_Z]);

		G_RAW_DATA[MC32X0_AXIS_X] = raw_buf[0];
		G_RAW_DATA[MC32X0_AXIS_Y] = raw_buf[1];
		G_RAW_DATA[MC32X0_AXIS_Z] = raw_buf[2];
		G_RAW_DATA[MC32X0_AXIS_Z] =
		    G_RAW_DATA[MC32X0_AXIS_Z] + gsensor_gain.z;

		sprintf(buf, "%04x %04x %04x", G_RAW_DATA[MC32X0_AXIS_X],
			G_RAW_DATA[MC32X0_AXIS_Y], G_RAW_DATA[MC32X0_AXIS_Z]);
		GSE_LOG("G_RAW_DATA: (%+3d %+3d %+3d)\n",
			G_RAW_DATA[MC32X0_AXIS_X], G_RAW_DATA[MC32X0_AXIS_Y],
			G_RAW_DATA[MC32X0_AXIS_Z]);
	}

	return 0;
}

static void mcube_copy_file(const char *dstfilepath)
{
	int err = 0;

	initKernelEnv();
	fd_file = openFile(dstfilepath, O_RDWR, 0);
	if (!fd_file) {
		GSE_LOG("open %s fail\n", dstfilepath);
		return;
	}

	err = writeFile(fd_file, backup_buf, 64);
	if (err > 0)
		GSE_LOG("buf:%s\n", backup_buf);
	else
		GSE_LOG("write file error %d\n", err);

	set_fs(oldfs);
	closeFile(fd_file);
}

long mc3230_ioctl(struct file *file, unsigned int cmd, unsigned long arg,
		  struct i2c_client *client)
{
	void __user *argp = (void __user *)arg;

	char strbuf[256];
	void __user *data;
	SENSOR_DATA sensor_data;
	int err = 0;
	int cali[3];

	struct mc3230_data *p_mc3230_data = get_3230_ctl_data();
	struct sensor_axis sense_data = { 0 };

	mcprintkreg("mc3230_ioctl cmd is %d.", cmd);

	switch (cmd) {
	case GSENSOR_IOCTL_READ_SENSORDATA:
	case GSENSOR_IOCTL_READ_RAW_DATA:
		GSE_LOG("fwq GSENSOR_IOCTL_READ_RAW_DATA\n");
		data = (void __user *)arg;
		MC32X0_ReadRawData(client, strbuf);
		if (copy_to_user(data, &strbuf, strlen(strbuf) + 1)) {
			err = -EFAULT;
			break;
		}
		break;
	case GSENSOR_IOCTL_SET_CALI:

		GSE_LOG("fwq GSENSOR_IOCTL_SET_CALI!!\n");

		break;

	case GSENSOR_MCUBE_IOCTL_SET_CALI:
		GSE_LOG("fwq GSENSOR_MCUBE_IOCTL_SET_CALI!!\n");
		data = (void __user *)arg;
		if (!data) {
			err = -EINVAL;
			break;
		}
		if (copy_from_user(&sensor_data, data, sizeof(sensor_data))) {
			err = -EFAULT;
			break;
		} else {
			cali[MC32X0_AXIS_X] = sensor_data.x;
			cali[MC32X0_AXIS_Y] = sensor_data.y;
			cali[MC32X0_AXIS_Z] = sensor_data.z;

			GSE_LOG
			    ("MCUBE_IOCTL_SET_CALI %d  %d  %d  %d  %d  %d!!\n",
			     cali[MC32X0_AXIS_X], cali[MC32X0_AXIS_Y],
			     cali[MC32X0_AXIS_Z], sensor_data.x, sensor_data.y,
			     sensor_data.z);

			err = MC32X0_WriteCalibration(client, cali);
		}
		break;
	case GSENSOR_IOCTL_CLR_CALI:
		GSE_LOG("fwq GSENSOR_IOCTL_CLR_CALI!!\n");
		err = MC32X0_ResetCalibration(client);
		break;
	case GSENSOR_IOCTL_GET_CALI:
		GSE_LOG("fwq mc32x0 GSENSOR_IOCTL_GET_CALI\n");
		data = (void __user *)arg;
		if (!data) {
			err = -EINVAL;
			break;
		}

		err = MC32X0_ReadCalibration(client, cali);
		if (err) {
			GSE_LOG
			    ("fwq mc32x0 MC32X0_ReadCalibration error!!!!\n");
			break;
		}

		sensor_data.x = p_mc3230_data->cali_sw[MC32X0_AXIS_X];
		sensor_data.y = p_mc3230_data->cali_sw[MC32X0_AXIS_Y];
		sensor_data.z = p_mc3230_data->cali_sw[MC32X0_AXIS_Z];
		if (copy_to_user(data, &sensor_data, sizeof(sensor_data))) {
			err = -EFAULT;
			break;
		}
		break;
	case GSENSOR_IOCTL_SET_CALI_MODE:
		GSE_LOG("fwq mc32x0 GSENSOR_IOCTL_SET_CALI_MODE\n");
		break;
	case GSENSOR_MCUBE_IOCTL_READ_RBM_DATA:
		GSE_LOG("fwq GSENSOR_MCUBE_IOCTL_READ_RBM_DATA\n");
		data = (void __user *)arg;
		if (!data) {
			err = -EINVAL;
			break;
		}
		MC32X0_ReadRBMData(client, (char *)&strbuf);
		if (copy_to_user(data, &strbuf, strlen(strbuf) + 1)) {
			err = -EFAULT;
			break;
		}
		break;
	case GSENSOR_MCUBE_IOCTL_SET_RBM_MODE:
		GSE_LOG("fwq GSENSOR_MCUBE_IOCTL_SET_RBM_MODE\n");
		if (READ_FROM_BACKUP == true) {
			mcube_copy_file(calib_path);
			READ_FROM_BACKUP = false;
		}
		MC32X0_rbm(client, 1);
		break;
	case GSENSOR_MCUBE_IOCTL_CLEAR_RBM_MODE:
		GSE_LOG("fwq GSENSOR_MCUBE_IOCTL_SET_RBM_MODE\n");

		MC32X0_rbm(client, 0);
		break;
	case GSENSOR_MCUBE_IOCTL_REGISTER_MAP:
		GSE_LOG("fwq GSENSOR_MCUBE_IOCTL_REGISTER_MAP\n");
		break;
	default:
		return -ENOTTY;
	}

	switch (cmd) {
	case MC_IOCTL_GETDATA:
		if (copy_to_user(argp, &sense_data, sizeof(sense_data))) {
			GSE_LOG("failed to copy sense data to user space.");
			return -EFAULT;
		}
		break;
	case GSENSOR_IOCTL_READ_RAW_DATA:
	case GSENSOR_IOCTL_READ_SENSORDATA:
		if (copy_to_user(argp, &strbuf, strlen(strbuf) + 1)) {
			GSE_LOG("failed to copy sense data to user space.");
			return -EFAULT;
		}
		break;
	default:
		break;
	}

	return 0;
}

/* odr table, hz */
static const int odr_table[8] = {
	1, 2, 4, 8, 16, 32, 64, 128
};

static int mc3230_select_odr(int want)
{
	int i;
	int max_index = ARRAY_SIZE(odr_table);

	for (i = 0; i < max_index; i++) {
		if (want <= odr_table[i])
			return max_index - i - 1;
	}

	return 0;
}
static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;
	int mc3230_rate = 0;

	if (rate == 0) {
		dev_err(&client->dev, "%s: rate == 0!!!\n", __func__);
		return -1;
	}

	mc3230_rate = mc3230_select_odr(1000 / rate);

	mc3230_rate = 0xf8 | (0x07 & mc3230_rate);

	if (rate != 0xff)
		result =
		    sensor_write_reg(client, MC32X0_Sample_Rate_REG,
				     mc3230_rate);
	if (result) {
		pr_info("%s:line=%d,error\n", __func__, __LINE__);
		return result;
	}

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);

	if (!enable) {
		sensor->ops->ctrl_data &= ~MC3230_MODE_BITS;
		sensor->ops->ctrl_data |= MC32X0_MODE_SLEEP;
	} else {
		sensor->ops->ctrl_data &= ~MC3230_MODE_BITS;
		sensor->ops->ctrl_data |= MC32X0_MODE_WAKEUP;
	}

	result =
	    sensor_write_reg(client, sensor->ops->ctrl_reg,
			     sensor->ops->ctrl_data);
	if (result)
		GSE_LOG("%s:fail to active sensor\n", __func__);

	return result;
}

static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;

	if (init_3230_ctl_data(client))
		return -1;

	result = sensor->ops->active(client, 0, sensor->pdata->poll_delay_ms);
	if (result) {
		GSE_LOG("%s:line=%d,error\n", __func__, __LINE__);
		return result;
	}

	sensor->status_cur = SENSOR_OFF;

	result = sensor_write_reg(client, MC32X0_Interrupt_Enable_REG, 0x10);
	if (result) {
		GSE_LOG("%s:line=%d,error\n", __func__, __LINE__);
		return result;
	}

	result = sensor->ops->active(client, 1, 31);
	if (result) {
		GSE_LOG("%s:line=%d,error\n", __func__, __LINE__);
		return result;
	}

	return result;
}

static int sensor_report_value(struct i2c_client *client)
{
	int ret = 0;

	mc3230_get_data(client);

	return ret;
}

static struct sensor_operate gsensor_ops = {
	.name = "gs_mc3230",
	/*sensor type and it should be correct */
	.type = SENSOR_TYPE_ACCEL,
	/* i2c id number */
	.id_i2c = ACCEL_ID_MC3230,
	/* read data */
	.read_reg = MC32X0_XOUT_REG,
	/* data length */
	.read_len = 3,
	/* read device id from this register, but mc3230 has no id register */
	.id_reg = SENSOR_UNKNOW_DATA,
	/* device id */
	.id_data = SENSOR_UNKNOW_DATA,
	/* 6 bits */
	.precision = 6,
	/* enable or disable */
	.ctrl_reg = MC32X0_Mode_Feature_REG,
	/* intterupt status register */
	.int_status_reg = MC32X0_Interrupt_Enable_REG,
	.range = {-32768, 32768},
	.trig = (IRQF_TRIGGER_HIGH | IRQF_ONESHOT),
	.active = sensor_active,
	.init = sensor_init,
	.report = sensor_report_value,
};

/****************operate according to sensor chip:end************/

/* function name should not be changed */
static struct sensor_operate *gsensor_get_ops(void)
{
	return &gsensor_ops;
}

static int __init gsensor_init(void)
{
	struct sensor_operate *ops = gsensor_get_ops();
	int result = 0;
	int type = ops->type;

	result = sensor_register_slave(type, NULL, NULL, gsensor_get_ops);
	GSE_LOG("  %s\n", __func__);
	return result;
}

static void __exit gsensor_exit(void)
{
	struct sensor_operate *ops = gsensor_get_ops();
	int type = ops->type;

	sensor_unregister_slave(type, NULL, NULL, gsensor_get_ops);
}

module_init(gsensor_init);
module_exit(gsensor_exit);
