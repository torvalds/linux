/* drivers/input/sensors/lsensor/ls_photoresistor.c
 *
 * Copyright (C) 2012-2015 ROCKCHIP.
 * Author:  ?<?@rock-chips.com>
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
#include <mach/gpio.h>
#include <mach/board.h> 
#include <linux/adc.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/sensor-dev.h>

static int ls_photoresistor_dbg_level = 0;
module_param_named(dbg_level, ls_photoresistor_dbg_level, int, 0644);
#if 1
#define SENSOR_DEBUG_TYPE SENSOR_TYPE_LIGHT
#define DBG( args...) \
	do { \
		if (ls_photoresistor_dbg_level) { \
			pr_info(args); \
		} \
	} while (0)
#else
#define DBG(x...) printk(x)
#endif

struct lsp_base_data {

	int adcvalue ;
	int oldadcvalue ;
	struct adc_client *adc_client ;	
};
struct lsp_base_data glspdata ;

		                                 //{1000,900,700,400,200,100,40,0};
static int LighSensorValue[8]={800,700,500,400,200,100,40,0};
static int LighSensorLevel_TAB[8]={0,1,2,3,4,5,6,7};

/****************operate according to sensor chip:start************/

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
    int ret = 0;

    DBG("light_photoresisto: sensor active is %s \n",( (enable)?"on":"off" ) );

    if( enable ){//ture on
		
    }else{//ture off
                
    }
	
    return 0;
}

static void lsp_adc_callback(struct adc_client *client, void *callback_param, int result)
{
	glspdata.adcvalue = result;
	DBG("light_photoresisto: adc callback value is %d \n",glspdata.adcvalue);
}

static int valuetoindex( int data)
{
	unsigned char index = 0;

	for( index = 0 ; index < ARRAY_SIZE(LighSensorLevel_TAB) ; index ++ )
	{
		if(data > LighSensorValue[index])		
			break;
	}

	return index;
}

static int sensor_report_value(struct i2c_client *client)
{
	int ret = 0 ;
	struct sensor_private_data *sensor =
	    (struct sensor_private_data *) i2c_get_clientdata(client);
	
	if(glspdata.adc_client)
		adc_async_read(glspdata.adc_client);

	if(glspdata.adcvalue==0||glspdata.adcvalue==1){//
		DBG("light_photoresisto: adc value is (%d)  invalid \n",glspdata.adcvalue);
	    	return ret ;
	}
	  
	if(glspdata.adcvalue != glspdata.oldadcvalue)
	{
		DBG("light_photoresisto: input report value is %d \n",glspdata.adcvalue);
		input_report_abs(sensor->input_dev, ABS_MISC, 
			valuetoindex(glspdata.adcvalue) );
	      input_sync(sensor->input_dev);
		  
		glspdata.oldadcvalue = glspdata.adcvalue;
	}
		
	return ret;
}

static int sensor_init(struct i2c_client *client)
{
    DBG("enter %s\n",__func__);
    int error = 0 ;
    struct sensor_private_data *sensor =  (struct sensor_private_data *) i2c_get_clientdata(client);	

    //adc register
    if( (sensor->pdata->address >= 0) && (sensor->pdata->address <=3) ){
	    glspdata.adc_client = adc_register(sensor->pdata->address, lsp_adc_callback, "lsp_adc");
	    if( !glspdata.adc_client ) {
			printk("%s : light_photoresisto adc_register faile !\n",__func__);
			error = -EINVAL;
			goto fail1;
	     }
    }

   return 0 ;
   
 fail1:
    adc_unregister(glspdata.adc_client);
 
    return error;
}

struct sensor_operate light_photoresistor_ops = {
	.name				= "light_photoresistor",
	.type				= SENSOR_TYPE_LIGHT,	//sensor type and it should be correct
	.id_i2c				= LIGHT_ID_PHOTORESISTOR,	//i2c id number

	.range				= {0,10},		//range
	.brightness              ={1,255},                          // brightness
	.active				= sensor_active,	
	.init				= sensor_init,
	.report				= sensor_report_value,

      /*--------- INVALID ----------*/
	.read_reg			= SENSOR_UNKNOW_DATA,	//read data INVALID
	.read_len			= SENSOR_UNKNOW_DATA,			//data length INVALID
	.id_reg				= SENSOR_UNKNOW_DATA,	//read device id from this register  INVALID
	.id_data 			= SENSOR_UNKNOW_DATA,	//device id INVALID
	.precision			= SENSOR_UNKNOW_DATA,			//8 bits INVALID
	.ctrl_reg 			= SENSOR_UNKNOW_DATA,		//enable or disable  INVALID
	.int_status_reg 		= SENSOR_UNKNOW_DATA,	//intterupt status register INVALID
	.trig				      = SENSOR_UNKNOW_DATA,		
	/*--------- INVALID end-------*/
};
/****************operate according to sensor chip:end************/

//function name should not be changed
static struct sensor_operate *light_get_ops(void)
{
	return &light_photoresistor_ops;
}


static int __init lsp_init(void)
{
	struct sensor_operate *ops = light_get_ops();
	int result = 0;
	int type = ops->type;
	result = sensor_register_slave(type, NULL, NULL, light_get_ops);
	DBG("%s\n",__func__);
	return result;
}

static void __exit lsp_exit(void)
{
	struct sensor_operate *ops = light_get_ops();
	int type = ops->type;
	sensor_unregister_slave(type, NULL, NULL, light_get_ops);
	if(glspdata.adc_client)
		adc_unregister(glspdata.adc_client);
}

module_init(lsp_init);
module_exit(lsp_exit);

