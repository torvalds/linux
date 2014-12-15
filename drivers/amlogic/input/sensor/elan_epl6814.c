/* drivers/i2c/chips/elan_epl6814.c - light and proxmity sensors driver
 * Copyright (C) 2011 ELAN Corporation.
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

#include <linux/hrtimer.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/mach-types.h>
#include <asm/setup.h>
#include <linux/wakelock.h>
#include <linux/jiffies.h>
#include <linux/sensor/elan_interface.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>

#include <linux/sensor/sensor_common.h>

#define TXBYTES 					2
#define RXBYTES 					2

#define PACKAGE_SIZE 			2
#define I2C_RETRY_COUNT 		10

// TODO: to make sure ALS_POLLING_RATE is greater than als sensoring time
#define ALS_POLLING_RATE 	    600	

#define ALS_MAX_COUNT			60000
#define ALS_MIN_COUNT			15  			//ambient light mode dynamic integration time ADC low threshold
#define ALS_MAX_LUX				60000	  	//ambient light mode MAX lux
#define ALS_MIN_LUX				0

/*24 bits integer, 8 bits fraction*/
#define ALS_I_THRESHOLD		0x018F		// incandescent threshold (1.55859375)
#define ALS_F_THRESHOLD		0x0217		// fluorescent threshold (2.08984375)
#define ALS_M_THRESHOLD		0x0199 		// incandescent and fluorescent mix threshold (1.59765625)

/*24 bits integer, 8 bits fraction*/
#define ALS_AX2					0x4C433
#define ALS_BX					0x11FE3
#define ALS_C					0x4AFB3
#define ALS_AX					0x1494A
#define ALS_B					0x20028


static void polling_do_work(struct work_struct *work);
static DECLARE_DELAYED_WORK(polling_work, polling_do_work);

//integration time table (us)
static uint16_t intT_table[] =
{
    1,
    8,
    16,
    32,
    64,
    128,
    256,
    512,
    640,
    768,
    1024,
    2048,
    4096,
    6144,
    8192,
    10240
};

//selection integration time from intT_table
static uint8_t intT_selection[] =
{
    1,
    7,
};

/* primitive raw data from I2C */
typedef struct _epl_raw_data
{
    u8 raw_bytes[PACKAGE_SIZE];
    u16 als_ch0_raw;
    u16 als_ch1_raw;
    uint32_t lux;
    uint32_t ratio;
} epl_raw_data;

struct elan_epl_data
{
    struct i2c_client *client;
    struct input_dev *als_input_dev;
    struct workqueue_struct *epl_wq;
#ifdef CONFIG_HAS_EARLYSUSPEND
    struct early_suspend early_suspend;
#endif
    int (*power)(int on);
    int als_opened;
    int enable_lflag;
    int read_flag;

    uint32_t als_i_threshold;
    uint32_t als_f_threshold;
    uint32_t als_m_threshold;

    uint32_t als_ax2;
    uint32_t als_bx;
    uint32_t als_c;
    uint32_t als_ax;
    uint32_t als_b;

    uint8_t epl_adc_item;
    uint8_t epl_average_item;
    uint8_t epl_integrationtime_item;
    uint16_t epl_integrationtime;
    uint8_t epl_integrationtime_index;
    uint8_t epl_intpersistency;
    uint8_t epl_op_mode;
    uint8_t epl_sc_mode;
    uint8_t epl_sensor_gain_item;
    uint8_t epl_return_status;
    u8 mode_flag;
} ;


static struct platform_device *sensor_dev;
struct elan_epl_data *epl_data;
static struct mutex als_enable_mutex,  als_get_sensor_value_mutex;
static epl_raw_data	gRawData;
static const char ElanALsensorName[]="elan_epl6814";

#define LOG_TAG                 		 "[ELAN EPL6814] "
#define LOG_FUN(f)               		 printk(KERN_INFO LOG_TAG"%s\n", __FUNCTION__)
#define LOG_INFO(fmt, args...)    	 printk(KERN_INFO LOG_TAG fmt, ##args)
#define LOG_ERR(fmt, args...)   	 printk(KERN_ERR  LOG_TAG"%s %d : "fmt, __FUNCTION__, __LINE__, ##args)

/*
//====================I2C write operation===============//
//regaddr: ELAN Register Address.
//bytecount: How many bytes to be written to register via i2c bus.
//txbyte: I2C bus transmit byte(s). Single byte(0X01) transmit only slave address.
//data: setting value.
//
// Example: If you want to write single byte to 0x1D register address, show below
//	      elan_sensor_I2C_Write(client,0x1D,0x01,0X02,0xff);
//
*/
static int elan_sensor_I2C_Write(struct i2c_client *client, uint8_t regaddr, uint8_t bytecount, uint8_t txbyte, uint8_t data)
{
    uint8_t buffer[2];
    int ret = 0;
    int retry;

    buffer[0] = (regaddr<<3) | bytecount ;
    buffer[1] = data;

    for(retry = 0; retry < I2C_RETRY_COUNT; retry++)
    {
        ret = i2c_master_send(client, buffer, txbyte);

        if (ret == txbyte)
        {
            break;
        }
        msleep(10);
    }


    if(retry>=I2C_RETRY_COUNT)
    {
        LOG_ERR("i2c write retry over %d\n", I2C_RETRY_COUNT);
        return -EINVAL;
    }

    return ret;
}

/*
//====================I2C read operation===============//
*/
static int elan_sensor_I2C_Read(struct i2c_client *client)
{
    uint8_t buffer[RXBYTES];
    int ret = 0, i =0;
    int retry;

    for(retry = 0; retry < I2C_RETRY_COUNT; retry++)
    {

        ret = i2c_master_recv(client, buffer, RXBYTES);

        if (ret == RXBYTES)
            break;
        msleep(10);
    }

    if(retry>=I2C_RETRY_COUNT)
    {
        LOG_ERR( "i2c read retry over %d\n", I2C_RETRY_COUNT);
        return -EINVAL;
    }

    for(i=0; i<PACKAGE_SIZE; i++)
    {
        gRawData.raw_bytes[i] = buffer[i];
    }

    return ret;
}



static int sensor_power_on(struct i2c_client *client)
{
    static struct regulator *reg_l9,* reg_lvs4;
    int rc;

    LOG_INFO( "power_on Enter\n");

    reg_l9 = regulator_get(&client->dev,"vcc_l9");

    if (IS_ERR(reg_l9))
    {
        LOG_ERR("on:could not get regulator_get, rc = %ld\n", PTR_ERR(reg_l9));
        return -ENODEV;
    }

    rc = regulator_set_voltage(reg_l9, 2850000, 2850000);
    if (rc)
    {
        LOG_INFO("power_on:regulator_set_voltage\n");
        return -EINVAL;
    }

    rc = regulator_enable(reg_l9);
    if (rc)
    {
        dev_err(&client->dev, "power_on:unable to enable regulator: %d\n", rc);
        return -EINVAL;
    }

    reg_lvs4 = regulator_get(&client->dev,"vcc_lvs4");

    if (IS_ERR(reg_lvs4))
    {
        LOG_ERR("power_on:could not get regulator_get, rc = %ld\n",PTR_ERR(reg_lvs4));
        return -ENODEV;
    }

    rc= regulator_enable(reg_lvs4);
    if (rc)
    {
        dev_err(&client->dev, "power_on:unable to enable regulator: %d\n", rc);
        return -EINVAL;
    }

    LOG_INFO("power_on Exit\n");
    return rc;
}


static int elan_sensor_lsensor_enable(struct elan_epl_data *epld)
{
    int ret;
    uint8_t regdata = 0;
    struct i2c_client *client = epld->client;

    //LOG_INFO("--- ALS sensor Enable --- \n");
    mutex_lock(&als_enable_mutex);

    //set register 0
    //average cycle = 32 times, sensing mode = continuous, sensor mode = als, gain = auto
    epl_data->epl_average_item = EPL_SENSING_32_TIME;
    epl_data->epl_sc_mode = EPL_S_SENSING_MODE;
    epl_data->epl_op_mode = EPL_ALS_MODE;
    epl_data->epl_sensor_gain_item = EPL_AUTO_GAIN;
    regdata = epl_data->epl_average_item | epl_data->epl_sc_mode | epl_data->epl_op_mode | epl_data->epl_sensor_gain_item;
    ret = elan_sensor_I2C_Write(client,REG_0,W_SINGLE_BYTE,0X02,regdata);

    //set register 1
    epl_data->epl_intpersistency = EPL_PST_1_TIME;
    epl_data->epl_adc_item = EPL_10BIT_ADC;
    epld->epl_integrationtime_item = intT_selection[epld->epl_integrationtime_index];
    regdata = epld->epl_integrationtime_item << 4 | epl_data->epl_intpersistency | epl_data->epl_adc_item;
    epld->epl_integrationtime = intT_table[epld->epl_integrationtime_item];
    ret = elan_sensor_I2C_Write(client,REG_1,W_SINGLE_BYTE,0X02,regdata);

    //set register 9
    elan_sensor_I2C_Write(client,REG_9,W_SINGLE_BYTE,0x02, EPL_INT_FRAME_ENABLE);  // set frame enable

    // set register 10~11
    // this speed up the GO_MID and GO_LOW in auto gain mode
    elan_sensor_I2C_Write(client,REG_10,W_SINGLE_BYTE,0x02, EPL_GO_MID);
    elan_sensor_I2C_Write(client,REG_11,W_SINGLE_BYTE,0x02, EPL_GO_LOW);

    /*restart sensor*/
    elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0X02,EPL_C_RESET);
    ret = elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0x02,EPL_C_START_RUN);
    //LOG_INFO("--- ALS sensor setting finish --- \n");
    msleep(2);

    if(ret != 0x02)
    {
        epld->enable_lflag = 0;
        LOG_ERR("ALS-sensor i2c err\n");
    }

    mutex_unlock(&als_enable_mutex);

    return ret;

}

// TODO: change this equation accodring to ur environment (coating)
static void incandescent_lux(void)
{
    struct elan_epl_data *epld = epl_data;
    // c =  329.2890625 * ratio - 512.15625
    // lux = (channel1 * c) / integration time
    uint32_t c = ((epld->als_ax* gRawData.ratio) >> 8) - epld->als_b;
    gRawData.lux = ((gRawData.als_ch1_raw * c) / epld->epl_integrationtime) >> 8;
}

// TODO: change this equation accodring to ur environment (coating)
static void fluorescent_lux(void)
{
    struct elan_epl_data *epld = epl_data;
    uint32_t ratio_square = (gRawData.ratio * gRawData.ratio) >> 8;

    // c = 1220.119219 * ratio - 287.8867188 * ratio ^ 2 -1199.699129
    // lux = (channel1 * c) / integration time
    uint32_t c = ((epld->als_ax2 * gRawData.ratio) >> 8) - ((epld->als_bx * ratio_square) >> 8) - epld->als_c;
    gRawData.lux = ((gRawData.als_ch1_raw * c) / epld->epl_integrationtime) >> 8;
}

////====================lux equation===============//
static void elan_epl_als_equation(void)
{
// No coating sensor lux equation
    struct elan_epl_data *epld = epl_data;
    uint32_t channel1 = gRawData.als_ch1_raw << 8;
    uint16_t channel0 = gRawData.als_ch0_raw;
    gRawData.ratio = channel1 / channel0;

    /*lighting source decision, and uses suitable formula to calculate lux*/
    if(gRawData.ratio <= epld->als_i_threshold)
    {
        gRawData.ratio = epld->als_i_threshold;
        incandescent_lux();
    }
    else if(gRawData.ratio > epld->als_i_threshold && gRawData.ratio < epld->als_m_threshold)
    {
        incandescent_lux();
    }
    else if(gRawData.ratio >= epld->als_m_threshold && gRawData.ratio < epld->als_f_threshold)
    {
        fluorescent_lux();
    }
    else
    {
        gRawData.ratio = epld->als_f_threshold;
        fluorescent_lux();
    }
   // LOG_INFO("ambient light sensor lux = %d ,ch1_raw=%x, ch0_raw=%x ,Int=%d\n\n", gRawData.lux,gRawData.als_ch1_raw ,gRawData.als_ch0_raw,epld->epl_integrationtime );
    input_report_abs(epld->als_input_dev, ABS_MISC, gRawData.lux);
    input_sync(epld->als_input_dev);
}


/*
//====================elan_epl_als_rawdata===============//
//polling method for Light sensor detect. Report Light sensor raw data.
//Report "ABS_MISC" event to HAL layer.
*/
static void elan_epl_als_rawdata(void)
{
    uint16_t maxvalue;
    uint16_t minvalue;
    uint8_t regdata;
    uint8_t maxIndex = sizeof(intT_selection)-1;
    struct elan_epl_data *epld = epl_data;
    struct i2c_client *client = epld->client;

    maxvalue = (gRawData.als_ch0_raw > gRawData.als_ch1_raw) ? gRawData.als_ch0_raw : gRawData.als_ch1_raw;
    minvalue = (gRawData.als_ch0_raw > gRawData.als_ch1_raw) ? gRawData.als_ch1_raw : gRawData.als_ch0_raw;

    if(maxvalue <= ALS_MAX_COUNT && minvalue >= ALS_MIN_COUNT)
    {
        elan_epl_als_equation();
    }
    else
    {

        if(maxvalue > ALS_MAX_COUNT)
        {
            if(epld->epl_integrationtime_index == 0)
            {
                epld->epl_integrationtime_item=intT_selection[epld->epl_integrationtime_index];
                epld->epl_integrationtime = intT_table[epld->epl_integrationtime_item];
                LOG_INFO("lgiht sensor lux max %d\n",ALS_MAX_LUX);
                input_report_abs(epld->als_input_dev, ABS_MISC, ALS_MAX_LUX);
                input_sync(epld->als_input_dev);
            }
            else
            {
                epld->epl_integrationtime_index --;
                epld->epl_integrationtime_item=intT_selection[epld->epl_integrationtime_index];
                epld->epl_integrationtime = intT_table[epld->epl_integrationtime_item];
                LOG_INFO("---- light sensor integration time down (%d us)\n", epld->epl_integrationtime);
                regdata = epld->epl_integrationtime_item << 4 | epld->epl_intpersistency | epld->epl_adc_item;
                elan_sensor_I2C_Write(client,REG_1,W_SINGLE_BYTE,0X02,regdata);
                //restart sensor
                elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0X02,EPL_C_RESET);
                elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0x02,EPL_C_START_RUN);
            }
        }

        if(minvalue < ALS_MIN_COUNT)
        {
            LOG_INFO("---- ADJ_Min value = %d,  intT_index = %d, intT = %d\n", minvalue, epld->epl_integrationtime_index, epld->epl_integrationtime);
            if(epld->epl_integrationtime_index == maxIndex)
            {
                epld->epl_integrationtime_item=intT_selection[epld->epl_integrationtime_index];
                epld->epl_integrationtime = intT_table[epld->epl_integrationtime_item];
                LOG_INFO("ambient light sensor min lux ,ch1_raw=%x, ch0_raw=%x ,IntTime=%d\n", gRawData.als_ch1_raw ,gRawData.als_ch0_raw,epld->epl_integrationtime );
                LOG_INFO("lgith sensor lux min = %d\n",ALS_MIN_LUX);
                input_report_abs(epld->als_input_dev, ABS_MISC, ALS_MIN_LUX);
                input_sync(epld->als_input_dev);
            }
            else
            {
                epld->epl_integrationtime_index ++ ;
                epld->epl_integrationtime_item=intT_selection[epld->epl_integrationtime_index];
                epld->epl_integrationtime = intT_table[epld->epl_integrationtime_item];
                LOG_INFO("---- light sensor integration time up (%d us)\n", epld->epl_integrationtime);
                regdata = epld->epl_integrationtime_item << 4 | epld->epl_intpersistency | epld->epl_adc_item;
                elan_sensor_I2C_Write(client,REG_1,W_SINGLE_BYTE,0X02,regdata);
                //restart sensor
                elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0X02,EPL_C_RESET);
                elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0x02,EPL_C_START_RUN);
            }
        }
    }
}


static void polling_do_work(struct work_struct *work)
{
    struct elan_epl_data *epld = epl_data;
    struct i2c_client *client = epld->client;

   // LOG_INFO("---polling do work---\n");
    cancel_delayed_work(&polling_work);

    if(epld->enable_lflag==0)
    {
        elan_sensor_I2C_Write(client,REG_9,W_SINGLE_BYTE,0x02,EPL_INT_DISABLE);
        elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0X02,EPL_C_RESET);
    }
    else
    {
        mutex_lock(&als_get_sensor_value_mutex);

        if(epld->read_flag==0)
        {
            epld->read_flag=1;
        }
        else
        {
            elan_sensor_I2C_Write(client,REG_14,R_TWO_BYTE,0x01,0x00);
            elan_sensor_I2C_Read(client);
            gRawData.als_ch0_raw = (gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0];

            elan_sensor_I2C_Write(client,REG_16,R_TWO_BYTE,0x01,0x00);
            elan_sensor_I2C_Read(client);
            gRawData.als_ch1_raw = (gRawData.raw_bytes[1]<<8) | gRawData.raw_bytes[0];

            elan_epl_als_rawdata();
        }

        queue_delayed_work(epld->epl_wq, &polling_work,msecs_to_jiffies(ALS_POLLING_RATE));
        elan_sensor_lsensor_enable(epld);
        mutex_unlock(&als_get_sensor_value_mutex);
    }
}


static ssize_t elan_ls_operationmode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    uint16_t mode=0;
    struct elan_epl_data *epld = epl_data;
    sscanf(buf, "%hu",&mode);
    LOG_INFO("==>[operation mode]=%d\n", mode);

    mutex_lock(&als_get_sensor_value_mutex);

    if(mode == 0)
    {
        epld->enable_lflag = 0;
    }
    else if(mode == 1)
    {
        epld->enable_lflag = 1;
    }
    else
    {
        LOG_INFO("0: none\n1: als only\n");
    }

    epld->read_flag = 0;
    cancel_delayed_work(&polling_work);
    queue_delayed_work(epld->epl_wq, &polling_work,msecs_to_jiffies(0));

    mutex_unlock(&als_get_sensor_value_mutex);
    return count;
}

static ssize_t elan_ls_operationmode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct elan_epl_data *epld = epl_data;
    long *tmp = (long*)buf;
    uint16_t mode =0;
    LOG_FUN();

    if(epld->enable_lflag==0)
    {
        mode = 0;
    }
    else
    {
        mode = 1;
    }
    tmp[0] = mode;

    return 2;
}


static ssize_t elan_ls_sensor_als_ch0_rawdata_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    long *tmp = (long*)buf;
    tmp[0] = gRawData.als_ch0_raw;
    return 2;
}

static ssize_t elan_ls_sensor_als_ch1_rawdata_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    long *tmp = (long*)buf;
    tmp[0] = gRawData.als_ch1_raw;
    return 2;
}


static ssize_t elan_ls_incandescent_threshold_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    uint32_t value;
    struct elan_epl_data *epld = epl_data;

    value = buf[3]<<24 | buf[2] <<16|buf[1]<<8 | buf[0];
    LOG_INFO("==>[incandescent]=%d\n", value);
    mutex_lock(&als_get_sensor_value_mutex);
    epld->als_i_threshold = value;
    mutex_unlock(&als_get_sensor_value_mutex);
    return 1;
}

static ssize_t elan_ls_incandescent_threshold_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    long *tmp = (long*)buf;
    struct elan_epl_data *epld = epl_data;
    LOG_FUN();

    tmp[0] = epld->als_i_threshold;
    return 4;
}

static ssize_t elan_ls_fluorescent_threshold_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int value;
    struct elan_epl_data *epld = epl_data;

    value = buf[3]<<24 | buf[2] <<16|buf[1]<<8 | buf[0];
    LOG_INFO("==>[fluorescent]=%d\n", value);
    mutex_lock(&als_get_sensor_value_mutex);
    epld->als_f_threshold = value;
    mutex_unlock(&als_get_sensor_value_mutex);
    return 1;
}

static ssize_t elan_ls_fluorescent_threshold_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    long *tmp = (long*)buf;
    struct elan_epl_data *epld = epl_data;
    LOG_FUN();

    tmp[0] = epld->als_f_threshold;
    return 4;
}

static ssize_t elan_ls_mix_threshold_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int value;
    struct elan_epl_data *epld = epl_data;

    value = buf[3]<<24 | buf[2] <<16|buf[1]<<8 | buf[0];
    LOG_INFO("==>[mix]=%d\n", value);
    mutex_lock(&als_get_sensor_value_mutex);
    epld->als_m_threshold = value;
    mutex_unlock(&als_get_sensor_value_mutex);
    return 1;
}

static ssize_t elan_ls_mix_threshold_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    long *tmp = (long*)buf;
    struct elan_epl_data *epld = epl_data;
    LOG_FUN();

    tmp[0] = epld->als_m_threshold;
    return 4;
}


static ssize_t elan_ls_als_ax2_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int value;
    struct elan_epl_data *epld = epl_data;
    LOG_FUN();

    value = buf[3]<<24 | buf[2] <<16|buf[1]<<8 | buf[0];
    mutex_lock(&als_get_sensor_value_mutex);
    epld->als_ax2= value;
    mutex_unlock(&als_get_sensor_value_mutex);
    return 1;
}

static ssize_t elan_ls_als_ax2_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct elan_epl_data *epld = epl_data;
    long *tmp = (long*)buf;
    tmp[0] = epld->als_ax2;
    return 4;
}


static ssize_t elan_ls_als_bx_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int value;
    struct elan_epl_data *epld = epl_data;
    LOG_FUN();

    value = buf[3]<<24 | buf[2] <<16|buf[1]<<8 | buf[0];
    mutex_lock(&als_get_sensor_value_mutex);
    epld->als_bx = value;
    mutex_unlock(&als_get_sensor_value_mutex);
    return 1;
}

static ssize_t elan_ls_als_bx_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct elan_epl_data *epld = epl_data;
    long *tmp = (long*)buf;
    tmp[0] = epld->als_bx;
    return 4;
}


static ssize_t elan_ls_als_c_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int value;
    struct elan_epl_data *epld = epl_data;
    LOG_FUN();

    value = buf[3]<<24 | buf[2] <<16|buf[1]<<8 | buf[0];
    mutex_lock(&als_get_sensor_value_mutex);
    epld->als_c = value;
    mutex_unlock(&als_get_sensor_value_mutex);
    return 1;
}

static ssize_t elan_ls_als_c_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct elan_epl_data *epld = epl_data;
    long *tmp = (long*)buf;
    tmp[0] = epld->als_c;
    return 4;
}


static ssize_t elan_ls_als_ax_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int value;
    struct elan_epl_data *epld = epl_data;
    LOG_FUN();

    value = buf[3]<<24 | buf[2] <<16|buf[1]<<8 | buf[0];
    mutex_lock(&als_get_sensor_value_mutex);
    epld->als_ax = value;
    mutex_unlock(&als_get_sensor_value_mutex);
    return 1;
}

static ssize_t elan_ls_als_ax_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct elan_epl_data *epld = epl_data;
    long *tmp = (long*)buf;
    tmp[0] = epld->als_ax;
    return 4;
}


static ssize_t elan_ls_als_b_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    int value;
    struct elan_epl_data *epld = epl_data;
    LOG_FUN();

    value = buf[3]<<24 | buf[2] <<16|buf[1]<<8 | buf[0];
    mutex_lock(&als_get_sensor_value_mutex);
    epld->als_b = value;
    mutex_unlock(&als_get_sensor_value_mutex);
    return 1;
}

static ssize_t elan_ls_als_b_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct elan_epl_data *epld = epl_data;
    long *tmp = (long*)buf;
    tmp[0] = epld->als_b;
    return 4;
}

static ssize_t elan_ls_als_lux_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    long *tmp = (long*)buf;
    tmp[0] = gRawData.lux;
    return 4;
}

static DEVICE_ATTR(elan_ls_operationmode, S_IRUGO|S_IWUSR|S_IWGRP, elan_ls_operationmode_show,elan_ls_operationmode_store);

static DEVICE_ATTR(elan_ls_sensor_als_ch0_rawdata, S_IRUGO, elan_ls_sensor_als_ch0_rawdata_show,NULL);
static DEVICE_ATTR(elan_ls_sensor_als_ch1_rawdata, S_IRUGO, elan_ls_sensor_als_ch1_rawdata_show,NULL);
static DEVICE_ATTR(elan_ls_als_lux, S_IRUGO, elan_ls_als_lux_show, NULL);

static DEVICE_ATTR(elan_ls_incandescent_threshold, S_IRUGO|S_IWUSR|S_IWGRP, elan_ls_incandescent_threshold_show, elan_ls_incandescent_threshold_store);
static DEVICE_ATTR(elan_ls_fluorescent_threshold, S_IRUGO|S_IWUSR|S_IWGRP, elan_ls_fluorescent_threshold_show, elan_ls_fluorescent_threshold_store);
static DEVICE_ATTR(elan_ls_mix_threshold, S_IRUGO|S_IWUSR|S_IWGRP, elan_ls_mix_threshold_show,elan_ls_mix_threshold_store);

static DEVICE_ATTR(elan_ls_als_ax2, S_IRUGO|S_IWUSR|S_IWGRP, elan_ls_als_ax2_show, elan_ls_als_ax2_store);
static DEVICE_ATTR(elan_ls_als_bx, S_IRUGO|S_IWUSR|S_IWGRP, elan_ls_als_bx_show, elan_ls_als_bx_store);
static DEVICE_ATTR(elan_ls_als_c, S_IRUGO|S_IWUSR|S_IWGRP, elan_ls_als_c_show, elan_ls_als_c_store);
static DEVICE_ATTR(elan_ls_als_ax, S_IRUGO|S_IWUSR|S_IWGRP, elan_ls_als_ax_show, elan_ls_als_ax_store);
static DEVICE_ATTR(elan_ls_als_b, S_IRUGO|S_IWUSR|S_IWGRP, elan_ls_als_b_show, elan_ls_als_b_store);




static struct attribute *ets_attributes[] =
{
    &dev_attr_elan_ls_operationmode.attr,

    &dev_attr_elan_ls_sensor_als_ch0_rawdata.attr,
    &dev_attr_elan_ls_sensor_als_ch1_rawdata.attr,
    &dev_attr_elan_ls_als_lux.attr,

    &dev_attr_elan_ls_incandescent_threshold.attr,
    &dev_attr_elan_ls_fluorescent_threshold.attr,
    &dev_attr_elan_ls_mix_threshold.attr,

    &dev_attr_elan_ls_als_ax2.attr,
    &dev_attr_elan_ls_als_bx.attr,
    &dev_attr_elan_ls_als_c.attr,
    &dev_attr_elan_ls_als_ax.attr,
    &dev_attr_elan_ls_als_b.attr,

    NULL,
};

static struct attribute_group ets_attr_group =
{
    .attrs = ets_attributes,
};


static int elan_als_open(struct inode *inode, struct file *file)
{
    struct elan_epl_data *epld = epl_data;
    LOG_FUN();

    if (epld->als_opened)
    {
        return -EBUSY;
    }
    epld->als_opened = 1;

    return 0;
}

static int elan_als_read(struct file *file, char __user *buffer, size_t count, loff_t *ppos)
{
    struct elan_epl_data *epld = epl_data;
    int buf[3];
    if(epld->read_flag ==1)
    {
        buf[0] = gRawData.als_ch0_raw;
        buf[1] = gRawData.als_ch1_raw;
        buf[2] = epl_data->epl_integrationtime;
        if(copy_to_user(buffer, &buf , sizeof(buf)))
            return 0;
        epld->read_flag = 0;
        return 12;
    }
    else
    {
        return 0;
    }
}



static int elan_als_release(struct inode *inode, struct file *file)
{
    struct elan_epl_data *epld = epl_data;

    LOG_FUN();

    epld->als_opened = 0;

    return 0;
}


static long elan_als_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int flag;
    unsigned long buf[3];
    struct elan_epl_data *epld = epl_data;

    void __user *argp = (void __user *)arg;

    LOG_INFO("als ioctl cmd %d\n", _IOC_NR(cmd));
printk("elan_als_ioctl cmd num =%d \n",_IOC_NR(cmd));
    switch(cmd)
    {
        case ELAN_EPL6800_IOCTL_GET_LFLAG:

            LOG_INFO("elan ambient-light IOCTL Sensor get lflag \n");
            flag = epld->enable_lflag;
            if (copy_to_user(argp, &flag, sizeof(flag)))
                return -EFAULT;

            LOG_INFO("elan ambient-light Sensor get lflag %d\n",flag);
            break;

        case ELAN_EPL6800_IOCTL_ENABLE_LFLAG:

            LOG_INFO("elan ambient-light IOCTL Sensor set lflag \n");
            if (copy_from_user(&flag, argp, sizeof(flag)))
                return -EFAULT;
            if (flag < 0 || flag > 1)
                return -EINVAL;

            mutex_lock(&als_get_sensor_value_mutex);

            epld->enable_lflag = flag;
            epld->read_flag=0;
            cancel_delayed_work(&polling_work);
            queue_delayed_work(epld->epl_wq, &polling_work,msecs_to_jiffies(0));

            mutex_unlock(&als_get_sensor_value_mutex);

            LOG_INFO("elan ambient-light Sensor set lflag %d\n",flag);
            break;

        case ELAN_EPL6800_IOCTL_GETDATA:
            buf[0] = (unsigned long)gRawData.als_ch0_raw;
            buf[1] = (unsigned long)gRawData.als_ch1_raw;
            buf[2] = (unsigned long)epl_data->epl_integrationtime;
            if(copy_to_user(argp, &buf , sizeof(buf)))
                return -EFAULT;

            break;

        default:
            LOG_ERR(" invalid cmd %d\n", _IOC_NR(cmd));
            return -EINVAL;
    }

    return 0;


}


static struct file_operations elan_als_fops =
{
    .owner = THIS_MODULE,
    .open = elan_als_open,
    .read = elan_als_read,
    .release = elan_als_release,
    .unlocked_ioctl = elan_als_ioctl
};

static struct miscdevice elan_als_device =
{
    .minor = MISC_DYNAMIC_MINOR,
    .name = "elan_epl6814",
    .fops = &elan_als_fops
};


static int initial_sensor(struct elan_epl_data *epld)
{
    struct i2c_client *client = epld->client;

    int ret = 0;

    LOG_INFO("initial sensor enter!\n");


    ret = elan_sensor_I2C_Read(client);

    if(ret < 0)
        return -EINVAL;

    /*restart sensor*/
    elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0X02,EPL_C_RESET);
    ret = elan_sensor_I2C_Write(client,REG_7,W_SINGLE_BYTE,0x02,EPL_C_START_RUN);

    msleep(2);

    epld->als_ax2 = ALS_AX2;
    epld->als_bx = ALS_BX;
    epld->als_c = ALS_C;
    epld->als_ax = ALS_AX;
    epld->als_b = ALS_B;

    epld->als_f_threshold= ALS_F_THRESHOLD;
    epld->als_i_threshold = ALS_I_THRESHOLD;
    epld->als_m_threshold = ALS_M_THRESHOLD;

    epld->enable_lflag = 1;

    return ret;
}



static int lightsensor_setup(struct elan_epl_data *epld)
{
    int err = 0;
    LOG_INFO("lightsensor_setup enter.\n");

    epld->als_input_dev = input_allocate_device();
    if (!epld->als_input_dev)
    {
        pr_err(
            "[epl6800 error]%s: could not allocate ls input device\n",__func__);
        return -ENOMEM;
    }
    epld->als_input_dev->name = ElanALsensorName;
    set_bit(EV_ABS, epld->als_input_dev->evbit);
    input_set_abs_params(epld->als_input_dev, ABS_MISC, 0, 9, 0, 0);

    err = input_register_device(epld->als_input_dev);
    if (err < 0)
    {
        pr_err("[epl6800 error]%s: can not register ls input device\n",__func__);
        goto err_free_ls_input_device;
    }

    err = misc_register(&elan_als_device);
    if (err < 0)
    {
        pr_err("[epl6800 error]%s: can not register ls misc device\n",__func__);
        goto err_unregister_ls_input_device;
    }

    return err;


err_unregister_ls_input_device:
    input_unregister_device(epld->als_input_dev);
err_free_ls_input_device:
    input_free_device(epld->als_input_dev);
    return err;
}


static int sensor_setup(struct elan_epl_data *epld)
{
    int err = 0;
    msleep(5);
    err = initial_sensor(epld);
    return err;
}


#ifdef CONFIG_SUSPEND
static int elan_sensor_suspend(struct i2c_client *client, pm_message_t mesg)
{
    LOG_FUN();

    if(epl_data->enable_lflag)
    {
        cancel_delayed_work(&polling_work);
        elan_sensor_I2C_Write(client,REG_7, W_SINGLE_BYTE, 0x02, EPL_C_P_DOWN);
    }
    return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void elan_sensor_early_suspend(struct early_suspend *h)
{
    struct elan_epl_data *epld = epl_data;
    struct i2c_client *client = epld->client;
    LOG_FUN();

    if(epl_data->enable_lflag)
    {
        cancel_delayed_work(&polling_work);
        elan_sensor_I2C_Write(client,REG_7, W_SINGLE_BYTE, 0x02, EPL_C_P_DOWN);
    }

}
#endif
static int elan_sensor_resume(struct i2c_client *client)
{
    LOG_FUN();

    if(epl_data->enable_lflag)
    {
        elan_sensor_I2C_Write(client,REG_7, W_SINGLE_BYTE, 0x02, EPL_C_P_UP);
        queue_delayed_work(epl_data->epl_wq, &polling_work,msecs_to_jiffies(0));
    }
    return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void elan_sensor_late_resume(struct early_suspend *h)
{
    struct elan_epl_data *epld = epl_data;
    struct i2c_client *client = epld->client;
    LOG_FUN();

    if(epld->enable_lflag)
    {
        epld->read_flag = 0;
        elan_sensor_I2C_Write(client,REG_7, W_SINGLE_BYTE, 0x02, EPL_C_P_UP);
        queue_delayed_work(epld->epl_wq, &polling_work,msecs_to_jiffies(0));
    }

}
#endif
#endif

static int elan_sensor_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
    int err = 0;
    struct elan_epl_data *epld ;

    LOG_INFO("elan sensor probe enter.\n");
    sensor_power_on(client);
    epld = kzalloc(sizeof(struct elan_epl_data), GFP_KERNEL);
    if (!epld)
        return -ENOMEM;

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
    {
        dev_err(&client->dev,"No supported i2c func what we need?!!\n");
        err = -ENOTSUPP;
        goto i2c_fail;
    }
    LOG_INFO("chip id REG 0x00 value = %8x\n", i2c_smbus_read_byte_data(client, 0x00));
    LOG_INFO("chip id REG 0x01 value = %8x\n", i2c_smbus_read_byte_data(client, 0x08));
    LOG_INFO("chip id REG 0x02 value = %8x\n", i2c_smbus_read_byte_data(client, 0x10));
    LOG_INFO("chip id REG 0x03 value = %8x\n", i2c_smbus_read_byte_data(client, 0x18));
    LOG_INFO("chip id REG 0x04 value = %8x\n", i2c_smbus_read_byte_data(client, 0x20));
    LOG_INFO("chip id REG 0x05 value = %8x\n", i2c_smbus_read_byte_data(client, 0x28));
    LOG_INFO("chip id REG 0x06 value = %8x\n", i2c_smbus_read_byte_data(client, 0x30));
    LOG_INFO("chip id REG 0x07 value = %8x\n", i2c_smbus_read_byte_data(client, 0x38));
    LOG_INFO("chip id REG 0x09 value = %8x\n", i2c_smbus_read_byte_data(client, 0x48));
    LOG_INFO("chip id REG 0x0D value = %8x\n", i2c_smbus_read_byte_data(client, 0x68));
    LOG_INFO("chip id REG 0x0E value = %8x\n", i2c_smbus_read_byte_data(client, 0x70));
    LOG_INFO("chip id REG 0x0F value = %8x\n", i2c_smbus_read_byte_data(client, 0x71));
    LOG_INFO("chip id REG 0x10 value = %8x\n", i2c_smbus_read_byte_data(client, 0x80));
    LOG_INFO("chip id REG 0x11 value = %8x\n", i2c_smbus_read_byte_data(client, 0x88));
    LOG_INFO("chip id REG 0x13 value = %8x\n", i2c_smbus_read_byte_data(client, 0x98));

    epld->client = client;
    i2c_set_clientdata(client, epld);

    epld->epl_adc_item = 0x01;  //10 bits ADC
    epld->epl_average_item = 0x05; // 32 times sensing average
    epld->epl_integrationtime_index = 0;
    epld->epl_integrationtime_item = intT_selection[epld->epl_integrationtime_index];  //integration time (proximity mode = 144us, ambient light mode = 128 us)
    epld->epl_integrationtime = intT_table[epld->epl_integrationtime_item];
    epld->epl_intpersistency = 0x00; //Interrupt Persistence = 1 time
    epld->epl_sc_mode = 0x00; //continuous mode
    epld->epl_sensor_gain_item = 0x02; //auto gain
    epld->epl_op_mode = 0x00; //ambient light mode
    epld->mode_flag=0x00;

    epl_data = epld;

    mutex_init(&als_enable_mutex);
    mutex_init(&als_get_sensor_value_mutex);

    epld->epl_wq = create_singlethread_workqueue("sensor_wq");
    if (!epld->epl_wq)
    {
        LOG_ERR("can't create workqueue\n");
        err = -ENOMEM;
        goto err_create_singlethread_workqueue;
    }

    err = lightsensor_setup(epld);
    if (err < 0)
    {
        LOG_ERR("lightsensor_setup error!!\n");
        goto err_lightsensor_setup;
    }

    err = sensor_setup(epld);
    if (err < 0)
    {
        LOG_ERR("setup error!\n");
        goto err_sensor_setup;
    }

#ifdef CONFIG_HAS_EARLYSUSPEND
    epld->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
    epld->early_suspend.suspend = elan_sensor_early_suspend;
    epld->early_suspend.resume = elan_sensor_late_resume;
    register_early_suspend(&epld->early_suspend);
#endif

    sensor_dev = platform_device_register_simple(ELAN_LS_6814, -1, NULL, 0);
    if (IS_ERR(sensor_dev))
    {
        printk ("sensor_dev_init: error\n");
        goto err_fail;
    }

    err = sysfs_create_group(&sensor_dev->dev.kobj, &ets_attr_group);
    if (err !=0)
    {
        dev_err(&client->dev,"%s:create sysfs group error", __func__);
        goto err_fail;
    }

    LOG_INFO("elan sensor probe success.\n");

    return err;

err_fail:
    input_unregister_device(epld->als_input_dev);
    input_free_device(epld->als_input_dev);
err_lightsensor_setup:
err_sensor_setup:
    destroy_workqueue(epld->epl_wq);
    mutex_destroy(&als_enable_mutex);
    mutex_destroy(&als_get_sensor_value_mutex);
    misc_deregister(&elan_als_device);
err_create_singlethread_workqueue:
i2c_fail:
//err_platform_data_null:
    kfree(epld);
    return err;
}


static int elan_sensor_remove(struct i2c_client *client)
{
    struct elan_epl_data *epld = i2c_get_clientdata(client);

    dev_dbg(&client->dev, "%s: enter.\n", __func__);

#ifdef CONFIG_HAS_EARLYSUSPEND
    unregister_early_suspend(&epld->early_suspend);
#endif
    input_unregister_device(epld->als_input_dev);
    input_free_device(epld->als_input_dev);
    misc_deregister(&elan_als_device);
    destroy_workqueue(epld->epl_wq);
    kfree(epld);
    return 0;
}


static const struct i2c_device_id elan_sensor_id[] =
{
    { ELAN_LS_6814, 0 },
    { }
};

static struct i2c_driver elan_sensor_driver =
{
    .probe	= elan_sensor_probe,
    .remove	= elan_sensor_remove,
    .id_table	= elan_sensor_id,
    .driver	= {
        .name = ELAN_LS_6814,
        .owner = THIS_MODULE,
    },
#ifdef CONFIG_SUSPEND
    .suspend = elan_sensor_suspend,
    .resume = elan_sensor_resume,
#endif
};



static int __init elan_sensor_init(void)
{
    return i2c_add_driver(&elan_sensor_driver);
}

static void __exit  elan_sensor_exit(void)
{
    i2c_del_driver(&elan_sensor_driver);
}

module_init(elan_sensor_init);
module_exit(elan_sensor_exit);

MODULE_AUTHOR("Cheng-Wei Lin <dusonlin@emc.com.tw>");
MODULE_DESCRIPTION("ELAN epl6814 driver");
MODULE_LICENSE("GPL");

