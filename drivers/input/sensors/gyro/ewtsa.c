/* drivers/input/sensors/gyro/Ewtsa.c
 *
 * Copyright (C) 2012-2015 ROCKCHIP.
 * Author: zhangaihui <zah@rock-chips.com>
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
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <asm/uaccess.h>
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#include <linux/of_gpio.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/sensor-dev.h>

/** This define controls compilation of the master device interface */
/*#define EWTSA_MASTER_DEVICE*/
/* configurable */
#define GYRO_MOUNT_SWAP_XY      0   /* swap X, Y */
#define GYRO_MOUNT_REVERSE_X    0   /* reverse X */
#define GYRO_MOUNT_REVERSE_Y    0   /* reverse Y */
#define GYRO_MOUNT_REVERSE_Z    0   /* reverse Z */

/* macro defines */
/*#define CHIP_ID                 0x68*/
#define DEVICE_NAME             "ewtsa"
#define EWTSA_ON                1
#define EWTSA_OFF               0
#define SLEEP_PIN               14
#define DRDY_PIN                12
#define DIAG_PIN                11
#define MAX_VALUE               32768

/* ewtsa_delay parameter */
#define DELAY_THRES_MIN         1
#define DELAY_THRES_1           4
#define DELAY_THRES_2           9   /* msec x 90% */
#define DELAY_THRES_3           18
#define DELAY_THRES_4           45
#define DELAY_THRES_5           90
#define DELAY_THRES_6           128
#define DELAY_THRES_MAX         255
#define DELAY_DLPF_2            2
#define DELAY_DLPF_3            3
#define DELAY_DLPF_4            4
#define DELAY_DLPF_5            5
#define DELAY_DLPF_6            6
#define DELAY_INTMIN_THRES      9

#define DATA_RATE_1             0x01

/* ewtsa_sleep parameter */
#define SLEEP_OFF               0
#define SLEEP_ON                1

/* event mode */
#define EWTSA_POLLING_MODE    0
#define EWTSA_INTERUPT_MODE   1

/* ewtsa register address */
#define REG_SMPL                0x15
#define REG_FS_DLPF             0x16
#define REG_INT_CFG             0x17
#define REG_INT_STATUS          0x1A
#define REG_SELF_O_C            0x29
#define REG_PWR_MGM             0x3E
#define REG_MBURST_ALL          0xFF
#define GYRO_DATA_REG            0x1D

/* ewtsa register param */
#define SELF_O_C_ENABLE         0x00
#define SELF_O_C_DISABLE        0x01
#define SLEEP_CTRL_ACTIVATE     0x40
#define SLEEP_CTRL_SLEEP        0x00
#define INT_CFG_INT_ENABLE      0x01
#define INT_CFG_INT_DISABLE     0x00

/* ewtsa interrupt control */
#define EWSTA_INT_CLEAR         0x00
#define EWSTA_INT_SKIP          0x01

/* wait time(ms)*/
#define EWTSA_BOOST_TIME_0      500

/* sleep setting range */
#define EWTSA_SLP_MIN 0
#define EWTSA_SLP_MAX 1

/* delay setting range */
#define EWTSA_DLY_MIN 1
#define EWTSA_DLY_MAX 255

/* range setting range */
#define EWTSA_RNG_MIN 0
#define EWTSA_RNG_MAX 3

/* soc setting range */
#define EWTSA_SOC_MIN 0
#define EWTSA_SOC_MAX 1

/* event setting range */
#define EWTSA_EVE_MIN 0
#define EWTSA_EVE_MAX 1

/* init param */
#define SLEEP_INIT_VAL       (SLEEP_ON)
#define DELAY_INIT_VAL       10
#define RANGE_INIT_VAL       2 /*range 1000*/
#define DLPF_INIT_VAL        (DELAY_DLPF_2)
#define CALIB_FUNC_INIT_VAL  (EWTSA_ON)

/*config store counter num*/
#define CONFIG_COUNTER_MIN (6+9)
#define CONFIG_COUNTER_MAX (32+9)

/*command name */
#define COMMAND_NAME_SOC 0
#define COMMAND_NAME_DLY 1
#define COMMAND_NAME_RNG 2
#define COMMAND_NAME_EVE 3
#define COMMAND_NAME_SLP 4
#define COMMAND_NAME_NUM 5

#define EWTSA_delay  DELAY_INIT_VAL
#define EWTSA_range    RANGE_INIT_VAL
#define EWTSA_calib    EWTSA_ON

/****************operate according to sensor chip:start************/
static int i2c_read_byte(struct i2c_client *thisClient, unsigned char regAddr, char *pReadData)
{
    int    ret = 0;

    ret = i2c_master_send( thisClient, (char*)&regAddr, 1);
    if(ret < 0)
    {
        printk("EWTSA send cAddress=0x%x error!\n", regAddr);
        return ret;
    }
    ret = i2c_master_recv( thisClient, (char*)pReadData, 1);
    if(ret < 0)
    {
        printk("EWTSAread *pReadData=0x%x error!\n", *pReadData);
        return ret;
    }

    return 1;
}
static int i2c_write_byte(struct i2c_client *thisClient, unsigned char regAddr, unsigned char writeData)
{
    char    write_data[2] = {0};
    int    ret=0;

    write_data[0] = regAddr;
    write_data[1] = writeData;

    ret = i2c_master_send(thisClient, write_data, 2);
    if (ret < 0)
    {
        ret = i2c_master_send(thisClient, write_data, 2);
        if (ret < 0)
	 {
	     printk("EWTSA send regAddr=0x%x error!\n", regAddr);
	     return ret;
        }
        return 1;
    }

    return 1;
}

static int ewtsa_system_restart(struct i2c_client *client)
{
    int             err;
     char   reg;
     char   smpl , dlpf;

    err = i2c_write_byte(client, ( unsigned char)REG_SELF_O_C, ( unsigned char)SELF_O_C_DISABLE);
    if (err < 0) {
        return err;
    }

    ///Set SMPL register
        if (EWTSA_delay <= ( unsigned char)DELAY_THRES_2) {
            smpl = ( unsigned char)DELAY_INTMIN_THRES;
        }else{
            smpl = ( unsigned char)(EWTSA_delay - ( unsigned char)1);
        }
    err = i2c_write_byte(client, ( unsigned char)REG_SMPL, ( unsigned char)smpl);
    if (err < 0) {
        return err;
    }

    ///Set DLPF register
    if (EWTSA_delay >= ( unsigned char)DELAY_THRES_6){
        dlpf = ( unsigned char)DELAY_DLPF_6;
    }else if (EWTSA_delay >= ( unsigned char)DELAY_THRES_5) {
        dlpf = ( unsigned char)DELAY_DLPF_5;
    }else if (EWTSA_delay >= ( unsigned char)DELAY_THRES_4){
        dlpf = ( unsigned char)DELAY_DLPF_4;
    }else if (EWTSA_delay >= ( unsigned char)DELAY_THRES_3) {
        dlpf = ( unsigned char)DELAY_DLPF_3;
    }else{
        dlpf = ( unsigned char)DELAY_DLPF_2;
    }

    reg = ( unsigned char)(( unsigned char)(EWTSA_range << 3) | dlpf | ( unsigned char)0x80 ) ;

    err = i2c_write_byte(client, REG_FS_DLPF, reg);
    if (err < 0) {
        return err;
    }

    if (EWTSA_calib==  EWTSA_ON) {
	printk("EWTSA_set_calibration() start \n");
	err =  i2c_write_byte(client,( unsigned char)REG_SELF_O_C, ( unsigned char)SELF_O_C_ENABLE);
	if (err < 0) {
		return err;
	}
	mdelay(500);
	printk("EWTSA_set_calibration() end \n");

    }

    return 0;
}

static int ewtsa_disable(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	gpio_direction_output(sensor->pdata->standby_pin, GPIO_HIGH);

	DBG("%s: end \n",__func__);

	return 0;
}

static int ewtsa_enable(struct i2c_client *client)
{
	int err;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);

	gpio_direction_output(sensor->pdata->standby_pin, GPIO_LOW);
	err = i2c_write_byte(client, ( unsigned char)REG_PWR_MGM, ( unsigned char)SLEEP_CTRL_ACTIVATE);////0x44
	if (err < 0){
		//return err;
		err = ewtsa_system_restart(client);///restart; only when i2c error
		if (err < 0){
			return err;
		}
	}

	err = i2c_write_byte(client,  ( unsigned char) REG_INT_CFG, ( unsigned char)INT_CFG_INT_ENABLE);
	if (err < 0) {
		return err;
	}
	DBG("%s: end \n",__func__);
	return 0;
}

void gyro_dev_reset(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);


    DBG("%s\n",__func__);
	gpio_direction_output(sensor->pdata->standby_pin, GPIO_HIGH);
	msleep(100);
	gpio_direction_output(sensor->pdata->standby_pin, GPIO_LOW);
	msleep(100);
}

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	/*
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int status = 0;
	*/
	int result = 0;
	if(enable)
	{
		result=ewtsa_enable(client);
	}
	else
	{
		result=ewtsa_disable(client);
	}

	if(result)
		printk("%s:fail to active sensor\n",__func__);

	return result;

}

static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	int result = 0;
	/*
	unsigned char buf[5];
	unsigned char data = 0;
	int i = 0;
	char pReadData=0;
	*/
	sensor->status_cur = SENSOR_OFF;
	gyro_dev_reset(client);
	ewtsa_system_restart(client);
	return result;
}


static int gyro_report_value(struct i2c_client *client, struct sensor_axis *axis)
{
	struct sensor_private_data *sensor =
	    	(struct sensor_private_data *) i2c_get_clientdata(client);

	if (sensor->status_cur == SENSOR_ON) {
		/* Report GYRO  information */
		input_report_rel(sensor->input_dev, ABS_RX, axis->x);
		input_report_rel(sensor->input_dev, ABS_RY, axis->y);
		input_report_rel(sensor->input_dev, ABS_RZ, axis->z);
		input_sync(sensor->input_dev);
	}

	return 0;
}

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
	    	(struct sensor_private_data *) i2c_get_clientdata(client);
	struct sensor_platform_data *pdata = sensor->pdata;
	int ret = 0;
	int x = 0, y = 0, z = 0;
	struct sensor_axis axis;
	char buffer[6] = {0};
	int i = 0;
	/* int value = 0; */

	memset(buffer, 0, 6);
#if 0
	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	do {
		buffer[0] = sensor->ops->read_reg;
		ret = sensor_rx_data(client, buffer, sensor->ops->read_len);
		if (ret < 0)
		return ret;
	} while (0);
#else

	for(i=0; i<6; i++)
	{
		i2c_read_byte(client, sensor->ops->read_reg + i,&buffer[i]);
	}
#endif

	x = (short) (((buffer[0]) << 8) | buffer[1]);
	y = (short) (((buffer[2]) << 8) | buffer[3]);
	z = (short) (((buffer[4]) << 8) | buffer[5]);

	//printk("%s: x=%d  y=%d z=%d \n",__func__, x,y,z);
	if(pdata && pdata->orientation)
	{
		axis.x = (pdata->orientation[0])*x + (pdata->orientation[1])*y + (pdata->orientation[2])*z;
		axis.y = (pdata->orientation[3])*x + (pdata->orientation[4])*y + (pdata->orientation[5])*z;
		axis.z = (pdata->orientation[6])*x + (pdata->orientation[7])*y + (pdata->orientation[8])*z;
	}
	else
	{
		axis.x = x;
		axis.y = y;
		axis.z = z;
	}

	gyro_report_value(client, &axis);

	mutex_lock(&sensor->data_mutex);
	sensor->axis = axis;
	mutex_unlock(&sensor->data_mutex);

	return ret;
}


struct sensor_operate gyro_ewtsa_ops = {
	.name			= "ewtsa",
	.type			= SENSOR_TYPE_GYROSCOPE,
	.id_i2c			= GYRO_ID_EWTSA,
	.read_reg			= GYRO_DATA_REG,
	.read_len			= 6,
	.id_reg			= -1,
	.id_data			= -1,
	.precision			= 16,
	.ctrl_reg			= REG_PWR_MGM,
	.int_status_reg	= REG_INT_STATUS,
	.range			= {-32768, 32768},
	.trig				= IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
	.active			= sensor_active,
	.init				= sensor_init,
	.report			= sensor_report_value,
};

/****************operate according to sensor chip:end************/

//function name should not be changed
static struct sensor_operate *gyro_get_ops(void)
{
	return &gyro_ewtsa_ops;
}


static int __init gyro_ewtsa_init(void)
{
	struct sensor_operate *ops = gyro_get_ops();
	int result = 0;
	int type = ops->type;
	result = sensor_register_slave(type, NULL, NULL, gyro_get_ops);
	return result;
}

static void __exit gyro_ewtsa_exit(void)
{
	struct sensor_operate *ops = gyro_get_ops();
	int type = ops->type;
	sensor_unregister_slave(type, NULL, NULL, gyro_get_ops);
}


module_init(gyro_ewtsa_init);
module_exit(gyro_ewtsa_exit);


