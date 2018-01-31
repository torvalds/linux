/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Export interface in /sys/class/touchpanel for calibration.
 *
 * Yongle Lai @ Rockchip - 2010-07-26
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/err.h>

#include "calibration_ts.h"

/*
 * The sys nodes for touch panel calibration depends on controller's name,
 * such as: /sys/bus/spi/drivers/xpt2046_ts/touchadc
 * If we use another TP controller (not xpt2046_ts), the above path will 
 * be unavailable which will cause calibration to be fail.
 *
 * Another choice is: 
 *   sys/devices/platform/rockchip_spi_master/spi0.0/driver/touchadc
 * this path request the TP controller will be the first device of SPI.
 *
 * To make TP calibration module in Android be universal, we create
 * a class named touchpanel as the path for calibration interfaces.
 */
 
/*
 * TPC driver depended.
 */
extern volatile struct adc_point gADPoint;
#ifdef TS_PRESSURE
extern volatile int gZvalue[3];
#endif

#if 0
#if defined(CONFIG_MACH_RK2818INFO_IT50) && defined(CONFIG_TOUCHSCREEN_XPT2046_CBN_SPI)
	int screen_x[5] = { 50, 750,  50, 750, 400};
  	int screen_y[5] = { 40,  40, 440, 440, 240};
	int uncali_x_default[5] = { 3735,  301, 3754,  290, 1993 };
	int uncali_y_default[5] = {  3442,  3497, 413, 459, 1880 };
#elif defined(CONFIG_MACH_RK2818INFO) && defined(CONFIG_TOUCHSCREEN_XPT2046_CBN_SPI) 
	int screen_x[5] = { 50, 750,  50, 750, 400};
  	int screen_y[5] = { 40,  40, 440, 440, 240};
	int uncali_x_default[5] = { 438,  565, 3507,  3631, 2105 };
	int uncali_y_default[5] = {  3756,  489, 3792, 534, 2159 };
#elif (defined(CONFIG_MACH_RAHO) || defined(CONFIG_MACH_RAHOSDK) || defined(CONFIG_MACH_RK2818INFO))&& defined(CONFIG_TOUCHSCREEN_XPT2046_320X480_CBN_SPI)
	int screen_x[5] = { 50, 270,  50, 270, 160}; 
	int screen_y[5] = { 40,  40, 440, 440, 240}; 
	int uncali_x_default[5] = { 812,  3341, 851,  3371, 2183 };
	int uncali_y_default[5] = {  442,  435, 3193, 3195, 2004 };
#elif defined(CONFIG_MACH_Z5) && defined(CONFIG_TOUCHSCREEN_XPT2046_CBN_SPI)
	int uncali_x_default[5] = {  3267,  831, 3139, 715, 1845 };
	int uncali_y_default[5] = { 3638,  3664, 564,  591, 2087 };
	int screen_x[5] = { 70,  410, 70, 410, 240};
	int screen_y[5] = { 50, 50,  740, 740, 400};
#endif
#endif
int screen_x[5] = { 0 };
int screen_y[5] = { 0 };
int uncali_x_default[5] = { 0 };
int uncali_y_default[5] = { 0 };
int uncali_x[5] = { 0 };
int uncali_y[5] = { 0 };

static ssize_t touch_mode_show(struct class *cls, char *_buf)
{
    int count;
    
	count = sprintf(_buf,"TouchCheck:%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
	                uncali_x[0], uncali_y[0],
	                uncali_x[1], uncali_y[1],
	                uncali_x[2], uncali_y[2],
	                uncali_x[3], uncali_y[3],
	                uncali_x[4], uncali_y[4]);

	printk("buf: %s", _buf);
		
	return count;
}

static ssize_t touch_mode_store(struct class *cls, const char *_buf, size_t _count)
{
    int i, j = 0;
    char temp[5];

    //printk("Read data from Android: %s\n", _buf);
    
    for (i = 0; i < 5; i++)
    {
        strncpy(temp, _buf + 5 * (j++), 4);
        uncali_x[i] = simple_strtol(temp, NULL, 10);
        strncpy(temp, _buf + 5 * (j++), 4);
        uncali_y[i] = simple_strtol(temp, NULL, 10);
        printk("SN=%d uncali_x=%d uncali_y=%d\n", 
                i, uncali_x[i], uncali_y[i]);
    }

    return _count; 
}

//This code is Touch adc simple value
static ssize_t touch_adc_show(struct class *cls,char *_buf)
{
    printk("ADC show: x=%d y=%d\n", gADPoint.x, gADPoint.y);
    
	return sprintf(_buf, "%d,%d\n", gADPoint.x, gADPoint.y);
}

static ssize_t touch_cali_status(struct class *cls, char *_buf)
{
    int ret;
    
    ret = TouchPanelSetCalibration(4, screen_x, screen_y, uncali_x, uncali_y);
    if (ret == 1){
    	memcpy(uncali_x_default, uncali_x, sizeof(uncali_x));
    	memcpy(uncali_y_default, uncali_y, sizeof(uncali_y));
    	printk("touch_cali_status-0--%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
	                uncali_x_default[0], uncali_y_default[0],
	                uncali_x_default[1], uncali_y_default[1],
	                uncali_x_default[2], uncali_y_default[2],
	                uncali_x_default[3], uncali_y_default[3],
	                uncali_x_default[4], uncali_y_default[4]);
    	ret = sprintf(_buf, "successful\n");
    }
    else{
     	printk("touchpal calibration failed, use default value.\n");
    	ret = TouchPanelSetCalibration(4, screen_x, screen_y, uncali_x_default, uncali_y_default);
    	printk("touch_cali_status-1---%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
	                uncali_x_default[0], uncali_y_default[0],
	                uncali_x_default[1], uncali_y_default[1],
	                uncali_x_default[2], uncali_y_default[2],
	                uncali_x_default[3], uncali_y_default[3],
	                uncali_x_default[4], uncali_y_default[4]);
    	if (ret == 1){
    		ret = sprintf(_buf, "recovery\n");
    	}
    	else{
    		ret = sprintf(_buf, "fail\n");
   		}
    }
    
    //printk("Calibration status: _buf=<%s", _buf);
    
	return ret;
}
#ifdef TS_PRESSURE
static ssize_t touch_pressure(struct class *cls,char *_buf)
{
	printk("enter %s gADPoint.x==%d,gADPoint.y==%d\n",__FUNCTION__,gADPoint.x,gADPoint.y);
	return sprintf(_buf,"%d,%d,%d\n",gZvalue[0],gZvalue[1],gZvalue[2]);
}
#endif

static struct class *tp_class = NULL;

static CLASS_ATTR(touchcheck, 0666, touch_mode_show, touch_mode_store);
static CLASS_ATTR(touchadc, 0666, touch_adc_show, NULL);
static CLASS_ATTR(calistatus, 0666, touch_cali_status, NULL);
#ifdef TS_PRESSURE
static CLASS_ATTR(pressure, 0666, touch_pressure, NULL);
#endif

int  tp_calib_iface_init(int *x,int *y,int *uncali_x, int *uncali_y)
{
    int ret = 0;
    int err = 0;
    
    tp_class = class_create(THIS_MODULE, "touchpanel");
    if (IS_ERR(tp_class)) 
    {
        printk("Create class touchpanel failed.\n");
        return -ENOMEM;
    }
    
    memcpy(screen_x,x,5*sizeof(int));
	memcpy(screen_y,y,5*sizeof(int));
	memcpy(uncali_x_default,uncali_x,5*sizeof(int));
	memcpy(uncali_y_default,uncali_y,5*sizeof(int));
	
    err = TouchPanelSetCalibration(4, screen_x, screen_y, uncali_x_default, uncali_y_default);
    	printk("tp_calib_iface_init---%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
	                uncali_x_default[0], uncali_y_default[0],
	                uncali_x_default[1], uncali_y_default[1],
	                uncali_x_default[2], uncali_y_default[2],
	                uncali_x_default[3], uncali_y_default[3],
	                uncali_x_default[4], uncali_y_default[4]);
  	if (err == 1){
		printk("Auto set calibration successfully.\n");
	} else {
		printk("Auto set calibraion failed, reset data again please !");
	}
    
    /*
	 * Create ifaces for TP calibration.
	 */
    ret =  class_create_file(tp_class, &class_attr_touchcheck);
    ret += class_create_file(tp_class, &class_attr_touchadc);
    ret += class_create_file(tp_class, &class_attr_calistatus);
#ifdef TS_PRESSURE
   ret += class_create_file(tp_class, &class_attr_pressure);
#endif
    if (ret)
    {
        printk("Fail to class ifaces for TP calibration.\n");
    }

    return ret;
}

void tp_calib_iface_exit(void)
{
    class_remove_file(tp_class, &class_attr_touchcheck);
    class_remove_file(tp_class, &class_attr_touchadc);
    class_remove_file(tp_class, &class_attr_calistatus);
#ifdef TS_PRESSURE
    class_remove_file(tp_class, &class_attr_pressure);
#endif
    class_destroy(tp_class);
}

//module_init(tp_calib_iface_init);
//module_exit(tp_calib_iface_exit);

MODULE_AUTHOR("Yongle Lai");
MODULE_DESCRIPTION("XPT2046 TPC driver @ Rockchip");
MODULE_LICENSE("GPL");
