/*
 * Copyright (C) 2008-2009 QUALCOMM Incorporated.
 */

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <media/msm_camera.h>
#include <mach/gpio.h>
#include "mt9d112.h"

/* Micron MT9D112 Registers and their values */
/* Sensor Core Registers */
#define  REG_MT9D112_MODEL_ID 0x3000
#define  MT9D112_MODEL_ID     0x1580

/*  SOC Registers Page 1  */
#define  REG_MT9D112_SENSOR_RESET     0x301A
#define  REG_MT9D112_STANDBY_CONTROL  0x3202
#define  REG_MT9D112_MCU_BOOT         0x3386

struct mt9d112_work {
	struct work_struct work;
};

static struct  mt9d112_work *mt9d112_sensorw;
static struct  i2c_client *mt9d112_client;

struct mt9d112_ctrl {
	const struct msm_camera_sensor_info *sensordata;
};


static struct mt9d112_ctrl *mt9d112_ctrl;

static DECLARE_WAIT_QUEUE_HEAD(mt9d112_wait_queue);
DECLARE_MUTEX(mt9d112_sem);


/*=============================================================
	EXTERNAL DECLARATIONS
==============================================================*/
extern struct mt9d112_reg mt9d112_regs;


/*=============================================================*/

static int mt9d112_reset(const struct msm_camera_sensor_info *dev)
{
	int rc = 0;

	rc = gpio_request(dev->sensor_reset, "mt9d112");

	if (!rc) {
		rc = gpio_direction_output(dev->sensor_reset, 0);
		mdelay(20);
		rc = gpio_direction_output(dev->sensor_reset, 1);
	}

	gpio_free(dev->sensor_reset);
	return rc;
}

static int32_t mt9d112_i2c_txdata(unsigned short saddr,
	unsigned char *txdata, int length)
{
	struct i2c_msg msg[] = {
		{
			.addr = saddr,
			.flags = 0,
			.len = length,
			.buf = txdata,
		},
	};

	if (i2c_transfer(mt9d112_client->adapter, msg, 1) < 0) {
		CDBG("mt9d112_i2c_txdata failed\n");
		return -EIO;
	}

	return 0;
}

static int32_t mt9d112_i2c_write(unsigned short saddr,
	unsigned short waddr, unsigned short wdata, enum mt9d112_width width)
{
	int32_t rc = -EIO;
	unsigned char buf[4];

	memset(buf, 0, sizeof(buf));
	switch (width) {
	case WORD_LEN: {
		buf[0] = (waddr & 0xFF00)>>8;
		buf[1] = (waddr & 0x00FF);
		buf[2] = (wdata & 0xFF00)>>8;
		buf[3] = (wdata & 0x00FF);

		rc = mt9d112_i2c_txdata(saddr, buf, 4);
	}
		break;

	case BYTE_LEN: {
		buf[0] = waddr;
		buf[1] = wdata;
		rc = mt9d112_i2c_txdata(saddr, buf, 2);
	}
		break;

	default:
		break;
	}

	if (rc < 0)
		CDBG(
		"i2c_write failed, addr = 0x%x, val = 0x%x!\n",
		waddr, wdata);

	return rc;
}

static int32_t mt9d112_i2c_write_table(
	struct mt9d112_i2c_reg_conf const *reg_conf_tbl,
	int num_of_items_in_table)
{
	int i;
	int32_t rc = -EIO;

	for (i = 0; i < num_of_items_in_table; i++) {
		rc = mt9d112_i2c_write(mt9d112_client->addr,
			reg_conf_tbl->waddr, reg_conf_tbl->wdata,
			reg_conf_tbl->width);
		if (rc < 0)
			break;
		if (reg_conf_tbl->mdelay_time != 0)
			mdelay(reg_conf_tbl->mdelay_time);
		reg_conf_tbl++;
	}

	return rc;
}

static int mt9d112_i2c_rxdata(unsigned short saddr,
	unsigned char *rxdata, int length)
{
	struct i2c_msg msgs[] = {
	{
		.addr   = saddr,
		.flags = 0,
		.len   = 2,
		.buf   = rxdata,
	},
	{
		.addr   = saddr,
		.flags = I2C_M_RD,
		.len   = length,
		.buf   = rxdata,
	},
	};

	if (i2c_transfer(mt9d112_client->adapter, msgs, 2) < 0) {
		CDBG("mt9d112_i2c_rxdata failed!\n");
		return -EIO;
	}

	return 0;
}

static int32_t mt9d112_i2c_read(unsigned short   saddr,
	unsigned short raddr, unsigned short *rdata, enum mt9d112_width width)
{
	int32_t rc = 0;
	unsigned char buf[4];

	if (!rdata)
		return -EIO;

	memset(buf, 0, sizeof(buf));

	switch (width) {
	case WORD_LEN: {
		buf[0] = (raddr & 0xFF00)>>8;
		buf[1] = (raddr & 0x00FF);

		rc = mt9d112_i2c_rxdata(saddr, buf, 2);
		if (rc < 0)
			return rc;

		*rdata = buf[0] << 8 | buf[1];
	}
		break;

	default:
		break;
	}

	if (rc < 0)
		CDBG("mt9d112_i2c_read failed!\n");

	return rc;
}

static int32_t mt9d112_set_lens_roll_off(void)
{
	int32_t rc = 0;
	rc = mt9d112_i2c_write_table(&mt9d112_regs.rftbl[0],
								 mt9d112_regs.rftbl_size);
	return rc;
}

static long mt9d112_reg_init(void)
{
	int32_t array_length;
	int32_t i;
	long rc;

	/* PLL Setup Start */
	rc = mt9d112_i2c_write_table(&mt9d112_regs.plltbl[0],
					mt9d112_regs.plltbl_size);

	if (rc < 0)
		return rc;
	/* PLL Setup End   */

	array_length = mt9d112_regs.prev_snap_reg_settings_size;

	/* Configure sensor for Preview mode and Snapshot mode */
	for (i = 0; i < array_length; i++) {
		rc = mt9d112_i2c_write(mt9d112_client->addr,
		  mt9d112_regs.prev_snap_reg_settings[i].register_address,
		  mt9d112_regs.prev_snap_reg_settings[i].register_value,
		  WORD_LEN);

		if (rc < 0)
			return rc;
	}

	/* Configure for Noise Reduction, Saturation and Aperture Correction */
	array_length = mt9d112_regs.noise_reduction_reg_settings_size;

	for (i = 0; i < array_length; i++) {
		rc = mt9d112_i2c_write(mt9d112_client->addr,
			mt9d112_regs.noise_reduction_reg_settings[i].register_address,
			mt9d112_regs.noise_reduction_reg_settings[i].register_value,
			WORD_LEN);

		if (rc < 0)
			return rc;
	}

	/* Set Color Kill Saturation point to optimum value */
	rc =
	mt9d112_i2c_write(mt9d112_client->addr,
	0x35A4,
	0x0593,
	WORD_LEN);
	if (rc < 0)
		return rc;

	rc = mt9d112_i2c_write_table(&mt9d112_regs.stbl[0],
					mt9d112_regs.stbl_size);
	if (rc < 0)
		return rc;

	rc = mt9d112_set_lens_roll_off();
	if (rc < 0)
		return rc;

	return 0;
}

static long mt9d112_set_sensor_mode(int mode)
{
	uint16_t clock;
	long rc = 0;

	switch (mode) {
	case SENSOR_PREVIEW_MODE:
		rc =
			mt9d112_i2c_write(mt9d112_client->addr,
				0x338C, 0xA20C, WORD_LEN);
		if (rc < 0)
			return rc;

		rc =
			mt9d112_i2c_write(mt9d112_client->addr,
				0x3390, 0x0004, WORD_LEN);
		if (rc < 0)
			return rc;

		rc =
			mt9d112_i2c_write(mt9d112_client->addr,
				0x338C, 0xA215, WORD_LEN);
		if (rc < 0)
			return rc;

		rc =
			mt9d112_i2c_write(mt9d112_client->addr,
				0x3390, 0x0004, WORD_LEN);
		if (rc < 0)
			return rc;

		rc =
			mt9d112_i2c_write(mt9d112_client->addr,
				0x338C, 0xA20B, WORD_LEN);
		if (rc < 0)
			return rc;

		rc =
			mt9d112_i2c_write(mt9d112_client->addr,
				0x3390, 0x0000, WORD_LEN);
		if (rc < 0)
			return rc;

		clock = 0x0250;

		rc =
			mt9d112_i2c_write(mt9d112_client->addr,
				0x341C, clock, WORD_LEN);
		if (rc < 0)
			return rc;

		rc =
			mt9d112_i2c_write(mt9d112_client->addr,
				0x338C, 0xA103, WORD_LEN);
		if (rc < 0)
			return rc;

		rc =
			mt9d112_i2c_write(mt9d112_client->addr,
				0x3390, 0x0001, WORD_LEN);
		if (rc < 0)
			return rc;

		mdelay(5);
		break;

	case SENSOR_SNAPSHOT_MODE:
		/* Switch to lower fps for Snapshot */
		rc =
			mt9d112_i2c_write(mt9d112_client->addr,
				0x341C, 0x0120, WORD_LEN);
		if (rc < 0)
			return rc;

		rc =
			mt9d112_i2c_write(mt9d112_client->addr,
				0x338C, 0xA120, WORD_LEN);
		if (rc < 0)
			return rc;

		rc =
			mt9d112_i2c_write(mt9d112_client->addr,
				0x3390, 0x0002, WORD_LEN);
		if (rc < 0)
			return rc;

		mdelay(5);

		rc =
			mt9d112_i2c_write(mt9d112_client->addr,
				0x338C, 0xA103, WORD_LEN);
		if (rc < 0)
			return rc;

		rc =
			mt9d112_i2c_write(mt9d112_client->addr,
				0x3390, 0x0002, WORD_LEN);
		if (rc < 0)
			return rc;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static long mt9d112_set_effect(int mode, int effect)
{
	uint16_t reg_addr;
	uint16_t reg_val;
	long rc = 0;

	switch (mode) {
	case SENSOR_PREVIEW_MODE:
		/* Context A Special Effects */
		reg_addr = 0x2799;
		break;

	case SENSOR_SNAPSHOT_MODE:
		/* Context B Special Effects */
		reg_addr = 0x279B;
		break;

	default:
		reg_addr = 0x2799;
		break;
	}

	switch (effect) {
	case CAMERA_EFFECT_OFF: {
		reg_val = 0x6440;

		rc = mt9d112_i2c_write(mt9d112_client->addr,
			0x338C, reg_addr, WORD_LEN);
		if (rc < 0)
			return rc;

		rc = mt9d112_i2c_write(mt9d112_client->addr,
			0x3390, reg_val, WORD_LEN);
		if (rc < 0)
			return rc;
	}
			break;

	case CAMERA_EFFECT_MONO: {
		reg_val = 0x6441;
		rc = mt9d112_i2c_write(mt9d112_client->addr,
			0x338C, reg_addr, WORD_LEN);
		if (rc < 0)
			return rc;

		rc = mt9d112_i2c_write(mt9d112_client->addr,
			0x3390, reg_val, WORD_LEN);
		if (rc < 0)
			return rc;
	}
		break;

	case CAMERA_EFFECT_NEGATIVE: {
		reg_val = 0x6443;
		rc = mt9d112_i2c_write(mt9d112_client->addr,
			0x338C, reg_addr, WORD_LEN);
		if (rc < 0)
			return rc;

		rc = mt9d112_i2c_write(mt9d112_client->addr,
			0x3390, reg_val, WORD_LEN);
		if (rc < 0)
			return rc;
	}
		break;

	case CAMERA_EFFECT_SOLARIZE: {
		reg_val = 0x6445;
		rc = mt9d112_i2c_write(mt9d112_client->addr,
			0x338C, reg_addr, WORD_LEN);
		if (rc < 0)
			return rc;

		rc = mt9d112_i2c_write(mt9d112_client->addr,
			0x3390, reg_val, WORD_LEN);
		if (rc < 0)
			return rc;
	}
		break;

	case CAMERA_EFFECT_SEPIA: {
		reg_val = 0x6442;
		rc = mt9d112_i2c_write(mt9d112_client->addr,
			0x338C, reg_addr, WORD_LEN);
		if (rc < 0)
			return rc;

		rc = mt9d112_i2c_write(mt9d112_client->addr,
			0x3390, reg_val, WORD_LEN);
		if (rc < 0)
			return rc;
	}
		break;

	case CAMERA_EFFECT_PASTEL:
	case CAMERA_EFFECT_MOSAIC:
	case CAMERA_EFFECT_RESIZE:
		return -EINVAL;

	default: {
		reg_val = 0x6440;
		rc = mt9d112_i2c_write(mt9d112_client->addr,
			0x338C, reg_addr, WORD_LEN);
		if (rc < 0)
			return rc;

		rc = mt9d112_i2c_write(mt9d112_client->addr,
			0x3390, reg_val, WORD_LEN);
		if (rc < 0)
			return rc;

		return -EINVAL;
	}
	}

	/* Refresh Sequencer */
	rc = mt9d112_i2c_write(mt9d112_client->addr,
		0x338C, 0xA103, WORD_LEN);
	if (rc < 0)
		return rc;

	rc = mt9d112_i2c_write(mt9d112_client->addr,
		0x3390, 0x0005, WORD_LEN);

	return rc;
}

static int mt9d112_sensor_init_probe(const struct msm_camera_sensor_info *data)
{
	uint16_t model_id = 0;
	int rc = 0;

	CDBG("init entry \n");
	rc = mt9d112_reset(data);
	if (rc < 0) {
		CDBG("reset failed!\n");
		goto init_probe_fail;
	}

	mdelay(5);

	/* Micron suggested Power up block Start:
	* Put MCU into Reset - Stop MCU */
	rc = mt9d112_i2c_write(mt9d112_client->addr,
		REG_MT9D112_MCU_BOOT, 0x0501, WORD_LEN);
	if (rc < 0)
		goto init_probe_fail;

	/* Pull MCU from Reset - Start MCU */
	rc = mt9d112_i2c_write(mt9d112_client->addr,
		REG_MT9D112_MCU_BOOT, 0x0500, WORD_LEN);
	if (rc < 0)
		goto init_probe_fail;

	mdelay(5);

	/* Micron Suggested - Power up block */
	rc = mt9d112_i2c_write(mt9d112_client->addr,
		REG_MT9D112_SENSOR_RESET, 0x0ACC, WORD_LEN);
	if (rc < 0)
		goto init_probe_fail;

	rc = mt9d112_i2c_write(mt9d112_client->addr,
		REG_MT9D112_STANDBY_CONTROL, 0x0008, WORD_LEN);
	if (rc < 0)
		goto init_probe_fail;

	/* FUSED_DEFECT_CORRECTION */
	rc = mt9d112_i2c_write(mt9d112_client->addr,
		0x33F4, 0x031D, WORD_LEN);
	if (rc < 0)
		goto init_probe_fail;

	mdelay(5);

	/* Micron suggested Power up block End */
	/* Read the Model ID of the sensor */
	rc = mt9d112_i2c_read(mt9d112_client->addr,
		REG_MT9D112_MODEL_ID, &model_id, WORD_LEN);
	if (rc < 0)
		goto init_probe_fail;

	CDBG("mt9d112 model_id = 0x%x\n", model_id);

	/* Check if it matches it with the value in Datasheet */
	if (model_id != MT9D112_MODEL_ID) {
		rc = -EINVAL;
		goto init_probe_fail;
	}

	rc = mt9d112_reg_init();
	if (rc < 0)
		goto init_probe_fail;

	return rc;

init_probe_fail:
	return rc;
}

int mt9d112_sensor_init(const struct msm_camera_sensor_info *data)
{
	int rc = 0;

	mt9d112_ctrl = kzalloc(sizeof(struct mt9d112_ctrl), GFP_KERNEL);
	if (!mt9d112_ctrl) {
		CDBG("mt9d112_init failed!\n");
		rc = -ENOMEM;
		goto init_done;
	}

	if (data)
		mt9d112_ctrl->sensordata = data;

	/* Input MCLK = 24MHz */
	msm_camio_clk_rate_set(24000000);
	mdelay(5);

	msm_camio_camif_pad_reg_reset();

	rc = mt9d112_sensor_init_probe(data);
	if (rc < 0) {
		CDBG("mt9d112_sensor_init failed!\n");
		goto init_fail;
	}

init_done:
	return rc;

init_fail:
	kfree(mt9d112_ctrl);
	return rc;
}

static int mt9d112_init_client(struct i2c_client *client)
{
	/* Initialize the MSM_CAMI2C Chip */
	init_waitqueue_head(&mt9d112_wait_queue);
	return 0;
}

int mt9d112_sensor_config(void __user *argp)
{
	struct sensor_cfg_data cfg_data;
	long   rc = 0;

	if (copy_from_user(&cfg_data,
			(void *)argp,
			sizeof(struct sensor_cfg_data)))
		return -EFAULT;

	/* down(&mt9d112_sem); */

	CDBG("mt9d112_ioctl, cfgtype = %d, mode = %d\n",
		cfg_data.cfgtype, cfg_data.mode);

		switch (cfg_data.cfgtype) {
		case CFG_SET_MODE:
			rc = mt9d112_set_sensor_mode(
						cfg_data.mode);
			break;

		case CFG_SET_EFFECT:
			rc = mt9d112_set_effect(cfg_data.mode,
						cfg_data.cfg.effect);
			break;

		case CFG_GET_AF_MAX_STEPS:
		default:
			rc = -EINVAL;
			break;
		}

	/* up(&mt9d112_sem); */

	return rc;
}

int mt9d112_sensor_release(void)
{
	int rc = 0;

	/* down(&mt9d112_sem); */

	kfree(mt9d112_ctrl);
	/* up(&mt9d112_sem); */

	return rc;
}

static int mt9d112_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int rc = 0;
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		rc = -ENOTSUPP;
		goto probe_failure;
	}

	mt9d112_sensorw =
		kzalloc(sizeof(struct mt9d112_work), GFP_KERNEL);

	if (!mt9d112_sensorw) {
		rc = -ENOMEM;
		goto probe_failure;
	}

	i2c_set_clientdata(client, mt9d112_sensorw);
	mt9d112_init_client(client);
	mt9d112_client = client;

	CDBG("mt9d112_probe succeeded!\n");

	return 0;

probe_failure:
	kfree(mt9d112_sensorw);
	mt9d112_sensorw = NULL;
	CDBG("mt9d112_probe failed!\n");
	return rc;
}

static const struct i2c_device_id mt9d112_i2c_id[] = {
	{ "mt9d112", 0},
	{ },
};

static struct i2c_driver mt9d112_i2c_driver = {
	.id_table = mt9d112_i2c_id,
	.probe  = mt9d112_i2c_probe,
	.remove = __exit_p(mt9d112_i2c_remove),
	.driver = {
		.name = "mt9d112",
	},
};

static int mt9d112_sensor_probe(const struct msm_camera_sensor_info *info,
				struct msm_sensor_ctrl *s)
{
	int rc = i2c_add_driver(&mt9d112_i2c_driver);
	if (rc < 0 || mt9d112_client == NULL) {
		rc = -ENOTSUPP;
		goto probe_done;
	}

	/* Input MCLK = 24MHz */
	msm_camio_clk_rate_set(24000000);
	mdelay(5);

	rc = mt9d112_sensor_init_probe(info);
	if (rc < 0)
		goto probe_done;

	s->s_init = mt9d112_sensor_init;
	s->s_release = mt9d112_sensor_release;
	s->s_config  = mt9d112_sensor_config;

probe_done:
	CDBG("%s %s:%d\n", __FILE__, __func__, __LINE__);
	return rc;
}

static int __mt9d112_probe(struct platform_device *pdev)
{
	return msm_camera_drv_start(pdev, mt9d112_sensor_probe);
}

static struct platform_driver msm_camera_driver = {
	.probe = __mt9d112_probe,
	.driver = {
		.name = "msm_camera_mt9d112",
		.owner = THIS_MODULE,
	},
};

static int __init mt9d112_init(void)
{
	return platform_driver_register(&msm_camera_driver);
}

module_init(mt9d112_init);
