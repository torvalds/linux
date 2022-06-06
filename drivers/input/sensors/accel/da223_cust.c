/* For RockChip android platform.
 *
 * mir3da.c - Linux kernel modules for 3-Axis Accelerometer
 *
 * Copyright (C) 2011-2013 MiraMEMS Sensing Technology Co., Ltd.
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
#include <linux/uaccess.h>
#include <asm/atomic.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/freezer.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/sensor-dev.h>
#include <linux/syscalls.h>
#include <linux/fs.h>

#include "da223_core.h"
#include "da223_cust.h"
/******************************************************************************/
#define GSENSOR_MIN        					2
#define MIR3DA_PRECISION           				11
#define MIR3DA_RANGE						16384
#define DA311_BOUNDARY            				(0x1 << (MIR3DA_PRECISION - 1))
#define DA311_GRAVITY_STEP        			(MIR3DA_RANGE/DA311_BOUNDARY)
/******************************************************************************/
#define MIR3DA_DRV_NAME                 			"mir3da"
#define MIR3DA_INPUT_DEV_NAME     			MIR3DA_DRV_NAME
/******************************************************************************/
static MIR_HANDLE                     	mir_handle;
static int is_init =0;
/******************************************************************************/
#define MI_DATA(format, ...)            if(DEBUG_DATA&mir3da_Log_level){printk(KERN_ERR MI_TAG format "\n", ## __VA_ARGS__);}
#define MI_MSG(format, ...)             if(DEBUG_MSG&mir3da_Log_level){printk(KERN_ERR MI_TAG format "\n", ## __VA_ARGS__);}
#define MI_ERR(format, ...)             if(DEBUG_ERR&mir3da_Log_level){printk(KERN_ERR MI_TAG format "\n", ## __VA_ARGS__);}
#define MI_FUN                          if(DEBUG_FUNC&mir3da_Log_level){printk(KERN_ERR MI_TAG "%s is called, line: %d\n", __FUNCTION__,__LINE__);}
#define MI_ASSERT(expr)                 \
	if (!(expr)) {\
		printk(KERN_ERR "Assertion failed! %s,%d,%s,%s\n",\
			__FILE__, __LINE__, __func__, #expr);\
	}
/******************************************************************************/
#if MIR3DA_OFFSET_TEMP_SOLUTION
static char OffsetFileName[] = "/data/misc/miraGSensorOffset.txt";
static char OffsetFolerName[] = "/data/misc/";
#define OFFSET_STRING_LEN               26
struct work_info
{
    char        tst1[20];
    char        tst2[20];
    char        buffer[OFFSET_STRING_LEN];
    struct      workqueue_struct *wq;
    struct      delayed_work read_work;
    struct      delayed_work write_work;
    struct      completion completion;
    int         len;
    int         rst;
};

static struct work_info m_work_info = {{0}};
/******************************************************************************/
static void sensor_write_work( struct work_struct *work )
{
    struct work_info*   pWorkInfo;
    struct file         *filep;
    mm_segment_t		orgfs;
    int                 ret;

    orgfs = get_fs();
    set_fs(KERNEL_DS);

    pWorkInfo = container_of((struct delayed_work*)work, struct work_info, write_work);
    if (pWorkInfo == NULL){
            MI_ERR("get pWorkInfo failed!");
            return;
    }

    filep = filp_open(OffsetFileName, O_RDWR|O_CREAT, 0600);
    if (IS_ERR(filep)){
        MI_ERR("write, sys_open %s error!!.\n", OffsetFileName);
        ret =  -1;
    }
    else
    {
        filep->f_op->write(filep, pWorkInfo->buffer, pWorkInfo->len, &filep->f_pos);
        filp_close(filep, NULL);
        ret = 0;

    set_fs(orgfs);
    pWorkInfo->rst = ret;
    complete( &pWorkInfo->completion );
}
/******************************************************************************/
static void sensor_read_work( struct work_struct *work )
{
    mm_segment_t orgfs;
    struct file *filep;
    int ret;
    struct work_info* pWorkInfo;

    orgfs = get_fs();
    set_fs(KERNEL_DS);

    pWorkInfo = container_of((struct delayed_work*)work, struct work_info, read_work);
    if (pWorkInfo == NULL){
        MI_ERR("get pWorkInfo failed!");
        return;
    }

    filep = filp_open(OffsetFileName, O_RDONLY, 0600);
    if (IS_ERR(filep)){
        MI_ERR("read, sys_open %s error!!.\n",OffsetFileName);
        set_fs(orgfs);
        ret =  -1;
    }
    else{
        filep->f_op->read(filep, pWorkInfo->buffer,  sizeof(pWorkInfo->buffer), &filep->f_pos);
        filp_close(filep, NULL);
        set_fs(orgfs);
        ret = 0;
    }

    pWorkInfo->rst = ret;
    complete( &(pWorkInfo->completion) );
}
/******************************************************************************/
static int sensor_sync_read(u8* offset)
{
    int     err;
    int     off[MIR3DA_OFFSET_LEN] = {0};
    struct work_info* pWorkInfo = &m_work_info;

    init_completion( &pWorkInfo->completion );
    queue_delayed_work( pWorkInfo->wq, &pWorkInfo->read_work, msecs_to_jiffies(0) );
    err = wait_for_completion_timeout( &pWorkInfo->completion, msecs_to_jiffies( 2000 ) );
    if ( err == 0 ){
        MI_ERR("wait_for_completion_timeout TIMEOUT");
        return -1;
    }

    if (pWorkInfo->rst != 0){
        MI_ERR("work_info.rst  not equal 0");
        return pWorkInfo->rst;
    }

    sscanf(m_work_info.buffer, "%x,%x,%x,%x,%x,%x,%x,%x,%x", &off[0], &off[1], &off[2], &off[3], &off[4], &off[5],&off[6], &off[7], &off[8]);

    offset[0] = (u8)off[0];
    offset[1] = (u8)off[1];
    offset[2] = (u8)off[2];
    offset[3] = (u8)off[3];
    offset[4] = (u8)off[4];
    offset[5] = (u8)off[5];
    offset[6] = (u8)off[6];
    offset[7] = (u8)off[7];
    offset[8] = (u8)off[8];

    return 0;
}
/******************************************************************************/
static int sensor_sync_write(u8* off)
{
    int err = 0;
    struct work_info* pWorkInfo = &m_work_info;

    init_completion( &pWorkInfo->completion );

    sprintf(m_work_info.buffer, "%x,%x,%x,%x,%x,%x,%x,%x,%x\n", off[0],off[1],off[2],off[3],off[4],off[5],off[6],off[7],off[8]);

    pWorkInfo->len = sizeof(m_work_info.buffer);

    queue_delayed_work( pWorkInfo->wq, &pWorkInfo->write_work, msecs_to_jiffies(0) );
    err = wait_for_completion_timeout( &pWorkInfo->completion, msecs_to_jiffies( 2000 ) );
    if ( err == 0 ){
        MI_ERR("wait_for_completion_timeout TIMEOUT");
        return -1;
    }

    if (pWorkInfo->rst != 0){
        MI_ERR("work_info.rst  not equal 0");
        return pWorkInfo->rst;
    }

    return 0;
}
/******************************************************************************/
static int check_califolder_exist(void)
{
    mm_segment_t     orgfs;
    struct  file *filep;

    orgfs = get_fs();
    set_fs(KERNEL_DS);

    filep = filp_open(OffsetFolerName, O_RDONLY, 0600);
    if (IS_ERR(filep)) {
        MI_ERR("%s read, sys_open %s error!!.\n",__func__,OffsetFolerName);
        set_fs(orgfs);
        return 0;
    }

    filp_close(filep, NULL);
    set_fs(orgfs);

    return 1;
}
/******************************************************************************/
static int support_fast_auto_cali(void)
{
#if MIR3DA_SUPPORT_FAST_AUTO_CALI
    return 1;
#else
    return 0;
#endif
}
#endif
/******************************************************************************/
static int get_address(PLAT_HANDLE handle)
{
    if(NULL == handle){
        MI_ERR("chip init failed !\n");
		    return -1;
    }

	return ((struct i2c_client *)handle)->addr;
}
/******************************************************************************/
static int i2c_smbus_read(PLAT_HANDLE handle, u8 addr, u8 *data)
{
    int                 res = 0;
    struct i2c_client   *client = (struct i2c_client*)handle;

    *data = i2c_smbus_read_byte_data(client, addr);

    return res;
}
/******************************************************************************/
static int i2c_smbus_read_block(PLAT_HANDLE handle, u8 addr, u8 count, u8 *data)
{
    int                 res = 0;
    struct i2c_client   *client = (struct i2c_client*)handle;

    res = i2c_smbus_read_i2c_block_data(client, addr, count, data);

    return res;
}
/******************************************************************************/
static int i2c_smbus_write(PLAT_HANDLE handle, u8 addr, u8 data)
{
    int                 res = 0;
    struct i2c_client   *client = (struct i2c_client*)handle;
    
    res = i2c_smbus_write_byte_data(client, addr, data);
    
    return res;
}
/******************************************************************************/
static void msdelay(int ms)
{
    mdelay(ms);
}
/******************************************************************************/

#if MIR3DA_OFFSET_TEMP_SOLUTION
static MIR_GENERAL_OPS_DECLARE(ops_handle, i2c_smbus_read, i2c_smbus_read_block, i2c_smbus_write, sensor_sync_write, sensor_sync_read, check_califolder_exist,get_address,support_fast_auto_cali,msdelay, printk, sprintf);
#else
static MIR_GENERAL_OPS_DECLARE(ops_handle, i2c_smbus_read, i2c_smbus_read_block, i2c_smbus_write, NULL, NULL, NULL,get_address,NULL,msdelay, printk, sprintf);
#endif

/******************************************************************************/
static ssize_t enable_show(struct device *dev,
                   struct device_attribute *attr, char *buf)
{
    int ret;
    char bEnable;

    MI_FUN;

    ret = mir3da_get_enable(mir_handle, &bEnable);
    if (ret < 0){
        ret = -EINVAL;
    }
    else{
        ret = sprintf(buf, "%d\n", bEnable);
    }

    return ret;
}
/******************************************************************************/
static ssize_t enable_store(struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    int ret;
    bool bEnable;
    unsigned long enable;

    if (buf == NULL){
        return -1;
    }

    enable = simple_strtoul(buf, NULL, 10);
    bEnable = (enable > 0) ? true : false;

    MI_MSG("%s:enable=%d\n",__func__,bEnable);

    ret = mir3da_set_enable (mir_handle, bEnable);
    if (ret < 0){
        ret = -EINVAL;
    }
    else{
        ret = count;
    }

    return ret;
}
/******************************************************************************/
static ssize_t axis_data_show(struct device *dev,
           struct device_attribute *attr, char *buf)
{
    int result;
    short x,y,z;
    int count = 0;

    result = mir3da_read_data(mir_handle, &x, &y, &z);
    if (result == 0)
        count += sprintf(buf+count, "x= %d;y=%d;z=%d\n", x,y,z);
    else
        count += sprintf(buf+count, "reading failed!");

    return count;
}
/******************************************************************************/
static ssize_t reg_data_store(struct device *dev,
           struct device_attribute *attr, const char *buf, size_t count)
{
    int                 addr, data;
    int                 result;

    sscanf(buf, "0x%x, 0x%x\n", &addr, &data);

    result = mir3da_register_write(mir_handle, addr, data);

    MI_ASSERT(result==0);

    return count;
}
/******************************************************************************/
static ssize_t reg_data_show(struct device *dev,
           struct device_attribute *attr, char *buf)
{
    MIR_HANDLE          handle = mir_handle;

    return mir3da_get_reg_data(handle, buf);
}
/******************************************************************************/

#if MIR3DA_OFFSET_TEMP_SOLUTION
static ssize_t offset_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    ssize_t count = 0;

    if(bLoad==FILE_EXIST)
		count += sprintf(buf,"%s",m_work_info.buffer);   
    else
		count += sprintf(buf,"%s","Calibration file not exist!\n");

    return count;
}
#endif
/******************************************************************************/
#if FILTER_AVERAGE_ENHANCE
static ssize_t average_enhance_show(struct device *dev,
                   struct device_attribute *attr, char *buf)
{
    int                             ret = 0;
    struct mir3da_filter_param_s    param = {0};

    ret = mir3da_get_filter_param(&param);
    ret |= sprintf(buf, "%d %d %d\n", param.filter_param_l, param.filter_param_h, param.filter_threhold);

    return ret;
}
/******************************************************************************/
static ssize_t average_enhance_store(struct device *dev,struct device_attribute *attr,const char *buf, size_t count)
{ 
	int                             ret = 0;
    struct mir3da_filter_param_s    param = {0};

    sscanf(buf, "%d %d %d\n", &param.filter_param_l, &param.filter_param_h, &param.filter_threhold);

    ret = mir3da_set_filter_param(&param);

    return count;
}
#endif
/******************************************************************************/
#if MIR3DA_OFFSET_TEMP_SOLUTION
static int bCaliResult = -1;
static ssize_t calibrate_miraGSensor_show(struct device *dev,struct device_attribute *attr,char *buf)
{
    int ret;

    ret = sprintf(buf, "%d\n", bCaliResult);
    return ret;
}
/******************************************************************************/
static ssize_t calibrate_miraGSensor_store(struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    s8      z_dir = 0;
    MIR_HANDLE      handle = mir_handle;

    z_dir = simple_strtol(buf, NULL, 10);
    bCaliResult = mir3da_calibrate(handle,z_dir);

    return count;
}
#endif
/******************************************************************************/
static ssize_t log_level_show(struct device *dev,
                   struct device_attribute *attr, char *buf)
{
    int ret;

    ret = sprintf(buf, "%d\n", mir3da_Log_level);

    return ret;
}
/******************************************************************************/
static ssize_t log_level_store(struct device *dev,
                    struct device_attribute *attr,
                    const char *buf, size_t count)
{
    mir3da_Log_level = simple_strtoul(buf, NULL, 10);

    return count;
}
/******************************************************************************/
static ssize_t primary_offset_show(struct device *dev,
                   struct device_attribute *attr, char *buf){
    MIR_HANDLE   handle = mir_handle;
    int x=0,y=0,z=0;

    mir3da_get_primary_offset(handle,&x,&y,&z);

	  return sprintf(buf, "x=%d ,y=%d ,z=%d\n",x,y,z);
}
/******************************************************************************/
static ssize_t version_show(struct device *dev,
                   struct device_attribute *attr, char *buf){

	return sprintf(buf, "%s_%s\n", DRI_VER, CORE_VER);
}
/******************************************************************************/
static ssize_t vendor_show(struct device *dev,
                   struct device_attribute *attr, char *buf){
	return sprintf(buf, "%s\n", "MiraMEMS");
}
/******************************************************************************/
static DEVICE_ATTR_RW(enable);
static DEVICE_ATTR_RO(axis_data);
static DEVICE_ATTR_RW(reg_data);
static DEVICE_ATTR_RW(log_level);
#if MIR3DA_OFFSET_TEMP_SOLUTION
static DEVICE_ATTR_RO(offset);
static DEVICE_ATTR_RW(calibrate_miraGSensor);
#endif
#if FILTER_AVERAGE_ENHANCE
static DEVICE_ATTR_RW(average_enhance);
#endif
static DEVICE_ATTR_RO(primary_offset);
static DEVICE_ATTR_RO(version);
static DEVICE_ATTR_RO(vendor);

/******************************************************************************/
static struct attribute *mir3da_attributes[] = {
    &dev_attr_enable.attr,
    &dev_attr_axis_data.attr,
    &dev_attr_reg_data.attr,
    &dev_attr_log_level.attr,
#if MIR3DA_OFFSET_TEMP_SOLUTION
    &dev_attr_offset.attr,
    &dev_attr_calibrate_miraGSensor.attr,
#endif
#if FILTER_AVERAGE_ENHANCE
    &dev_attr_average_enhance.attr,
#endif
    &dev_attr_primary_offset.attr,
    &dev_attr_version.attr,
    &dev_attr_vendor.attr,
    NULL
};

static const struct attribute_group mir3da_attr_group = {
    .attrs  = mir3da_attributes,
};
/******************************************************************************/
static int sensor_init(struct i2c_client *client)
{
    int ret = 0;
    static int withSysAttr = 1;
	unsigned char chip_id=0;
	unsigned char i=0;

    struct sensor_private_data *sensor =(struct sensor_private_data *) i2c_get_clientdata(client);

    MI_FUN;

    if(is_init)
	return 0;

    sensor->status_cur = SENSOR_OFF;

    if(mir3da_install_general_ops(&ops_handle)){
        MI_ERR("Install ops failed !\n");
        return -1;
    }

#if MIR3DA_OFFSET_TEMP_SOLUTION
    m_work_info.wq = create_singlethread_workqueue( "oo" );
    if(NULL==m_work_info.wq) {
        MI_ERR("Failed to create workqueue !");
        return -1;
    }

    INIT_DELAYED_WORK( &m_work_info.read_work, sensor_read_work );
    INIT_DELAYED_WORK( &m_work_info.write_work, sensor_write_work );
#endif
	i2c_smbus_read((PLAT_HANDLE) client, NSA_REG_WHO_AM_I, &chip_id);	
	if(chip_id != 0x13){
        for(i=0;i<5;i++){
			mdelay(5);
		    i2c_smbus_read((PLAT_HANDLE) client, NSA_REG_WHO_AM_I, &chip_id);
            if(chip_id == 0x13)
                break;
		}
		if(i == 5)
	        client->addr = 0x27;
	}

    mir_handle = mir3da_core_init(client);
    if(NULL == mir_handle){
        MI_ERR("chip init failed !\n");
        return -1;
    }

    if(withSysAttr)
    {
       struct input_dev* pInputDev;
       pInputDev = input_allocate_device();
	if (!pInputDev) {
		MI_ERR("Failed to allocate input device %s\n", sensor->input_dev->name);
		return -ENOMEM;
	}

	pInputDev->name = MIR3DA_INPUT_DEV_NAME;
	set_bit(EV_ABS, pInputDev->evbit);

	/* x-axis acceleration */
	input_set_abs_params(pInputDev, ABS_X, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
	/* y-axis acceleration */
	input_set_abs_params(pInputDev, ABS_Y, sensor->ops->range[0], sensor->ops->range[1], 0, 0);
	/* z-axis acceleration */
	input_set_abs_params(pInputDev, ABS_Z, sensor->ops->range[0], sensor->ops->range[1], 0, 0);

	ret = input_register_device(pInputDev);
	if (ret) {
		MI_ERR("Unable to register input device %s\n", pInputDev->name);
		return -ENOMEM;
	}
        MI_MSG("Sys Attribute Register here %s is called for MIR3DA.\n", __func__);

        ret = sysfs_create_group(&pInputDev->dev.kobj, &mir3da_attr_group);
        if (ret) {
            MI_ERR("mir3da_attr_group create Error err=%d..", ret);
            ret = -EINVAL;
        }

        withSysAttr = 0;
    }

     is_init =1;

      return ret;
}
/******************************************************************************/
static int sensor_active(struct i2c_client *client, int enable, int rate)
{
    int result = 0;

    MI_MSG("%s. enable=%d.\n", __func__,enable);

    if(!is_init)
	return -1;

    mdelay(10);
    if(enable){
 /*      result = mir3da_chip_resume(client);
	if(result) {
		MI_ERR("sensor_active chip resume fail!!\n");
		return result;
	}
*/
		result = mir3da_set_enable(client, true);
		if(result){
			MI_ERR("sensor_active enable  fail!!\n");
			return result;
		}
    }
    else{
	result = mir3da_set_enable(client, false);
	if(result){
		MI_ERR("sensor_active disable  fail!!\n");
		return result;
	}
    }
    mdelay(10);

    return result;
}
/******************************************************************************/
static int sensor_report_value(struct i2c_client *client)
{
    struct sensor_private_data *sensor = (struct sensor_private_data *) i2c_get_clientdata(client);
    struct sensor_platform_data *pdata = sensor->pdata;
    struct sensor_axis axis;
    int ret = 0;
    short  x=0,y=0,z=0;
    int  tmp_x=0,tmp_y=0,tmp_z=0;
	static struct sensor_axis last_axis;
	static int flag;

    if(!is_init)
	return -1;

     ret = mir3da_read_data (client,&x, &y, &z);
     if (ret){
         MI_ERR("read data failed!");
         return ret;
     }

     //MI_DATA(" x = %d, y = %d, z = %d\n", x, y, z);

     tmp_x = x*DA311_GRAVITY_STEP;
     tmp_y = y*DA311_GRAVITY_STEP;
     tmp_z = z*DA311_GRAVITY_STEP;

     //MI_DATA(" tmp_x = %d, tmp_y = %d, tmp_z = %d\n", tmp_x, tmp_y, tmp_z);

     axis.x = (pdata->orientation[0])*tmp_x + (pdata->orientation[1])*tmp_y + (pdata->orientation[2])*tmp_z;
     axis.y = (pdata->orientation[3])*tmp_x + (pdata->orientation[4])*tmp_y + (pdata->orientation[5])*tmp_z;
#if MIR3DA_STK_TEMP_SOLUTION
     axis.z = (pdata->orientation[6])*tmp_x + (pdata->orientation[7])*tmp_y + (bzstk?1:(pdata->orientation[8]))*tmp_z;
#else
     axis.z = (pdata->orientation[6])*tmp_x + (pdata->orientation[7])*tmp_y + (pdata->orientation[8])*tmp_z;
#endif
     //MI_DATA( "map: axis = %d  %d  %d \n", axis.x, axis.y, axis.z);
#if 0
     if (axis.x == last_axis.x &&    axis.y == last_axis.y &&  axis.z == last_axis.z)
                axis.x += 1;
#else
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
#endif
  //  if((abs(sensor->axis.x - axis.x) > GSENSOR_MIN) || (abs(sensor->axis.y - axis.y) > GSENSOR_MIN) || (abs(sensor->axis.z - axis.z) > GSENSOR_MIN))
    {

	/* RK3326 platform board */
#if defined (CONFIG_BOARD_RK3326_AK47)
	input_report_abs(sensor->input_dev, ABS_X, -(axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Y, -(axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined (CONFIG_BOARD_RK3326_TH700)
	input_report_abs(sensor->input_dev, ABS_X, -(axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Y, -(axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined(CONFIG_BOARD_RK3326_TH863B_10)
	input_report_abs(sensor->input_dev, ABS_X, -(axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Y, (axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined(CONFIG_BOARD_RK3326_TH863B_7)
	input_report_abs(sensor->input_dev, ABS_X, -(axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Y, (axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined(CONFIG_BOARD_RK3326_TH863B_V31_7)
	input_report_abs(sensor->input_dev, ABS_X, -(axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Y, (axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined(CONFIG_BOARD_RK3326_TH863B_8)
	input_report_abs(sensor->input_dev, ABS_X, (axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Y, (axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined(CONFIG_BOARD_RK3326_TH7926_7)
	input_report_abs(sensor->input_dev, ABS_X, -(axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Y, -(axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined(CONFIG_BOARD_RK3326_TH7926_9)
	input_report_abs(sensor->input_dev, ABS_X, -(axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Y, -(axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined(CONFIG_BOARD_RK3326_MT1011)
	input_report_abs(sensor->input_dev, ABS_X, (axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Y, (axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined(CONFIG_BOARD_RK3326_M1011QR)
	input_report_abs(sensor->input_dev, ABS_X, (axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Y, (axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined(CONFIG_BOARD_RK3326_TH1021DN)
	input_report_abs(sensor->input_dev, ABS_X, -(axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Y, (axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined(CONFIG_BOARD_RK3326_TH1021DN_V20)
	input_report_abs(sensor->input_dev, ABS_X, -(axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Y, (axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));

	/* RK3126C platform board */
#elif defined(CONFIG_BOARD_RK3126C_AK47)
	input_report_abs(sensor->input_dev, ABS_X, (axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Y, (axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined(CONFIG_BOARD_RK3126C_TH1021DN)
	input_report_abs(sensor->input_dev, ABS_X, (axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Y, -(axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined(CONFIG_BOARD_RK3126C_TH863_7)
	input_report_abs(sensor->input_dev, ABS_X, -(axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Y, (axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined(CONFIG_BOARD_RK3126C_TH863_8)
	input_report_abs(sensor->input_dev, ABS_X, (axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Y, (axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#elif defined(CONFIG_BOARD_RK3126C_TH98V)
	input_report_abs(sensor->input_dev, ABS_X, (axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Y, (axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));

	/* RK3368 platform board */
#elif defined(CONFIG_BOARD_RK3368_TH863C_10)
	input_report_abs(sensor->input_dev, ABS_X, -(axis.x/64));
	input_report_abs(sensor->input_dev, ABS_Y, (axis.y/64));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z/64));
#else
	input_report_abs(sensor->input_dev, ABS_X, -(axis.x));
	input_report_abs(sensor->input_dev, ABS_Y, (axis.y));
	input_report_abs(sensor->input_dev, ABS_Z, -(axis.z));
#endif

	input_sync(sensor->input_dev);
        last_axis = axis;

	mutex_lock(&(sensor->data_mutex) );
	sensor->axis = axis;
	mutex_unlock(&(sensor->data_mutex) );
    }

    return ret;
}
/******************************************************************************/
static int sensor_suspend(struct i2c_client *client)
{
	int result = 0;

	MI_FUN;

	mdelay(10);

	result = mir3da_set_enable(client, false);
	if (result) {
		MI_ERR("sensor_suspend disable  fail!!\n");
		return result;
	}

	mdelay(10);

	return result;
}

/******************************************************************************/
static int sensor_resume(struct i2c_client *client)
{
	int result = 0;

	MI_FUN;

	mdelay(10);

	/*
	 * result = mir3da_chip_resume(client);
	 * if(result) {
	 * MI_ERR("sensor_resume chip resume fail!!\n");
	 * return result;
	 * }
	 */
	result = mir3da_set_enable(client, true);
	if (result) {
		MI_ERR("sensor_resume enable  fail!!\n");
		return result;
	}

	mdelay(10);

	return result;
}

/******************************************************************************/
static struct sensor_operate gsensor_ops = {
	.name           = MIR3DA_DRV_NAME,
	.type           = SENSOR_TYPE_ACCEL,
	.id_i2c         = ACCEL_ID_MIR3DA,
	.read_reg       = -1,
	.read_len       = 0,
	.id_reg         =  -1,
	.id_data            = 0,
	.precision          = MIR3DA_PRECISION,
	.ctrl_reg           = -1,
	.int_status_reg     = 0x00,
	.range          = {-MIR3DA_RANGE, MIR3DA_RANGE},
	.trig           = IRQF_TRIGGER_LOW | IRQF_ONESHOT,
	.active         = sensor_active,
	.init           = sensor_init,
	.report         = sensor_report_value,
	.suspend  = sensor_suspend,
	.resume   = sensor_resume,
};

/******************************************************************************/
static int gsensor_mir3da_probe(struct i2c_client *client,
				const struct i2c_device_id *devid)
{
	MI_FUN;

	return sensor_register_device(client, NULL, devid, &gsensor_ops);
}
/******************************************************************************/
static int gsensor_mir3da_remove(struct i2c_client *client)
{
	MI_FUN;

	return sensor_unregister_device(client, NULL, &gsensor_ops);
}
/******************************************************************************/
static const struct i2c_device_id gsensor_mir3da_id[] = {
	{"gs_da223", ACCEL_ID_MIR3DA},
	{}
};

static struct i2c_driver gsensor_mir3da_driver = {
	.probe = gsensor_mir3da_probe,
	.remove = gsensor_mir3da_remove,
	.shutdown = sensor_shutdown,
	.id_table = gsensor_mir3da_id,
	.driver = {
		.name = "gsensor_mir3da",
#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
#endif
	},
};

module_i2c_driver(gsensor_mir3da_driver);

MODULE_DESCRIPTION("mir3da 3-Axis accelerometer driver");
MODULE_LICENSE("GPL");
