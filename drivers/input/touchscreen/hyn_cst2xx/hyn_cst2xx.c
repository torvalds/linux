/*
 * drivers/input/touchscreen/hyn_cst2xx.c
 *
 * hynitron TouchScreen driver.
 *
 * Copyright (c) 2015  hynitron
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
 * VERSION      	DATE			AUTHOR
 *  1.0		    2015-10-12		    Tim
 *
 * note: only support mulititouch
 */

#include <linux/module.h>
#include <linux/delay.h>
//#include <linux/earlysuspend.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/async.h>
//#include <mach/iomux.h>
#include <linux/irq.h>
//#include <mach/board.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/input/mt.h>
#include <linux/gpio.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include "../tp_suspend.h"

#define HYN_DEBUG
#if defined (CONFIG_KP_AXP)
        extern int axp_gpio_set_value(int gpio, int io_state);
        extern int axp_gpio_set_io(int , int );
#if defined(CONFIG_BOARD_TYPE_ZM726CE_V12)
        #define PMU_GPIO_NUM    3
#endif
#endif

#if defined(CONFIG_TP_1680E_726_SD)
    //#define Y_POL
	//#define X_POL
	#define SWAP_X_Y
	#define SCREEN_MAX_X 		1024
	#define SCREEN_MAX_Y 		600
    //#include "CST21680_F_WGJ10276.h"

#else
    #define Y_POL
	//#define X_POL
	#define SWAP_X_Y
	#define SCREEN_MAX_X 		1280
	#define SCREEN_MAX_Y 		800
    #include "CST21680SE_S126_D863_7.h"
#endif

#define ICS_SLOT_REPORT
//#define HAVE_TOUCH_KEY
#define SLEEP_CLEAR_POINT

#define CST2XX_I2C_NAME 	"cst2xxse"
#define CST2XX_I2C_ADDR 	0x5A

//#define IRQ_PORT			RK2928_PIN1_PB0//RK30_PIN1_PB7
//#define WAKE_PORT			RK30_PIN0_PA1//RK30_PIN0_PB6

//#define TPD_PROC_DEBUG
#ifdef TPD_PROC_DEBUG
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
//static struct proc_dir_entry *hyn_config_proc = NULL;
#define HYN_CONFIG_PROC_FILE "hyn_config"
#define CONFIG_LEN 31
//static char hyn_read[CONFIG_LEN];
static u8 hyn_data_proc[8] = {0};
static u8 hyn_proc_flag = 0;
//static struct i2c_client *ts->client = NULL;
#endif

#define TRANSACTION_LENGTH_LIMITED
//#define HYN_MONITOR
#define PRESS_MAX    		255
#define MAX_FINGERS 		5
#define MAX_CONTACTS 		10
#define DMA_TRANS_LEN		0x20

#ifdef HYN_MONITOR
static struct workqueue_struct *hyn_monitor_workqueue = NULL;
static u8 int_1st[4] = {0};
static u8 int_2nd[4] = {0};
//static char dac_counter = 0;
static char b0_counter = 0;
static char bc_counter = 0;
static char i2c_lock_flag = 0;
#endif

//#define HYN_GESTURE	// if define enable this function
#ifdef HYN_GESTURE
	extern void rk_send_wakeup_key(void);
	static int gsl_lcd_flag = -1;
	static int gsl_gesture_flag = -1;
#endif

#ifdef HAVE_TOUCH_KEY
static u16 key = 0;
static int key_state_flag = 0;
struct key_data {
	u16 key;
	u16 x_min;
	u16 x_max;
	u16 y_min;
	u16 y_max;
};

static const u16 key_array[] = {
			KEY_BACK,
			KEY_HOME,
			KEY_MENU,
			KEY_SEARCH,
};
#define MAX_KEY_NUM     (sizeof(key_array)/sizeof(key_array[0]))

struct key_data hyn_key_data[MAX_KEY_NUM] = {
	{KEY_BACK, 2048, 2048, 2048, 2048},
	{KEY_HOME, 2048, 2048, 2048, 2048},
	{KEY_MENU, 2048, 2048, 2048, 2048},
	{KEY_SEARCH, 2048, 2048, 2048, 2048},
};
#endif

struct hyn_ts {
	struct i2c_client *client;
	struct input_dev *input;
	struct work_struct work;
	struct workqueue_struct *wq;
	struct hyn_ts_data *dd;
	u8 *touch_data;
	u8 device_id;
	int irq;
	int rst_pin;
	int irq_pin;
    struct delayed_work hyn_monitor_work;

#if defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif
	struct  tp_device  tp;
};
int h_wake_pin = 0;

#ifdef HYN_DEBUG
#define print_info(fmt, args...)   \
        do{                              \
                printk(fmt, ##args);     \
        }while(0)
#else
#define print_info(fmt, args...)
#endif

#define ANDROID_TOOL_SURPORT

#ifdef ANDROID_TOOL_SURPORT

static int cst2xx_update_firmware(struct i2c_client * client, struct hyn_ts *ts,
 	unsigned char *pdata, int data_len);

static unsigned short g_unnormal_mode = 0;
static unsigned short g_cst2xx_tx = 15;
static unsigned short g_cst2xx_rx = 10;
static struct hyn_ts *hyn_global_ts=NULL;

#endif

static int cst2xx_i2c_read(struct i2c_client *client, unsigned char *buf, int len) 
{
	int ret = -1;
	int retries = 0;

    //client->timing  = 370;
    //client->addr   |= I2C_ENEXT_FLAG;

	while(retries < 2) {
		ret = i2c_master_recv(client, buf, len);
		if(ret<=0)
		    retries++;
        else
            break;
	}

	return ret;
}

static int cst2xx_i2c_write(struct i2c_client *client, unsigned char *buf, int len) 
{
	int ret = -1;
	int retries = 0;

	while(retries < 2) {
		ret = i2c_master_send(client, buf, len);
		if(ret<=0)
			retries++;
		else
			break;
	}
	return ret;
}

static int cst2xx_i2c_read_register(struct i2c_client *client, unsigned char *buf, int len) 
{
	int ret = -1;

	ret = cst2xx_i2c_write(client, buf, 2);
	ret = cst2xx_i2c_read(client, buf, len);
	return ret;
}

static int cst2xx_test_i2c(struct i2c_client *client)
{
	u8 retry = 0;
	u8 ret;
	u8 buf[4];

	buf[0] = 0xD0;
	buf[1] = 0x00;
	while(retry++ < 5) {
		ret = cst2xx_i2c_write(client, buf, 2);
		if (ret > 0)
			return ret;
		msleep(2);
	}

	if(retry==5) printk("hyn iic test error.ret:%d.\n", ret);

	return ret;
}


static void hard_reset_chip(struct hyn_ts *ts, u16 ms)
{
	int ret=0;
	int retry=0;
	unsigned char buf[4];

	buf[0] = 0xD1;
	buf[1] = 0x0e;
	while(retry++ < 3) {
		ret = cst2xx_i2c_write(ts->client, buf, 2);
		if (ret > 0) break;
		msleep(2);
	}

	msleep(ms);
}

#ifdef TPD_PROC_DEBUG
static int char_to_int(char ch)
{
	if(ch>='0' && ch<='9')
		return (ch-'0');
	else
		return (ch-'a'+10);
}

#endif

#define  CST2XX_BASE_ADDR		(0x00000000)

static int cst2xx_enter_download_mode(struct hyn_ts *ts)
{
	int ret;
	int i;
	unsigned char buf[3];

	hard_reset_chip(ts, 5);

	for(i=0; i<30; i++)
	{

		buf[0] = 0xA0;
		buf[1] = 0x01;
		buf[2] = 0xAA;
		ret = cst2xx_i2c_write(ts->client, buf, 3);
		if (ret < 0)
		{
			msleep(1);
			continue;
		}

		msleep(6); //wait enter download mode

		buf[0] = 0xA0;
		buf[1] = 0x03; //check whether into program mode
		ret = cst2xx_i2c_read_register(ts->client, buf, 1);
		if(ret < 0)
		{
			msleep(1);
			continue;
		}

		if (buf[0] == 0x55)
		{
			break;

		}

	}

	if(buf[0] != 0x55)
	{
		printk("hyn reciev 0x55 failed.\n");
		return -1;
	}
	else
	{
	    buf[0] = 0xA0;
		buf[1] = 0x06;
		buf[2] = 0x00;
		ret = cst2xx_i2c_write(ts->client, buf, 3);

	}

	return 0;
}

static int cst2xx_download_program(unsigned char *pdata, int len, struct hyn_ts *ts)
{
	int i, ret, j,retry;
	unsigned char *i2c_buf;
	unsigned char temp_buf[8];
	unsigned short eep_addr, iic_addr;
	int total_kbyte;

	i2c_buf = kmalloc(sizeof(unsigned char)*(512 + 2), GFP_KERNEL);
	if (i2c_buf == NULL)
	{
		return -1;
	}

	//make sure fwbin len is N*1K

	total_kbyte = len / 512;

	for (i=0; i<total_kbyte; i++) {
		i2c_buf[0] = 0xA0;
		i2c_buf[1] = 0x14;
		eep_addr = i << 9;		//i * 512
		i2c_buf[2] = eep_addr;
		i2c_buf[3] = eep_addr>>8;
		ret = cst2xx_i2c_write(ts->client, i2c_buf, 4);
		if (ret < 0)
			goto error_out;

	#if 0
		i2c_buf[0] = 0xA0;
		i2c_buf[1] = 0x18;
		memcpy(i2c_buf + 2, pdata + eep_addr, 512);
		ret = cst2xx_i2c_write(ts->client, i2c_buf, 514);
		if (ret < 0)
			goto error_out;
	#else

		memcpy(i2c_buf, pdata + eep_addr, 512);
		for(j=0; j<128; j++) {
			iic_addr = (j<<2);
			temp_buf[0] = (iic_addr+0xA018)>>8;
			temp_buf[1] = (iic_addr+0xA018)&0xFF;
			temp_buf[2] = i2c_buf[iic_addr+0];
			temp_buf[3] = i2c_buf[iic_addr+1];
			temp_buf[4] = i2c_buf[iic_addr+2];
			temp_buf[5] = i2c_buf[iic_addr+3];
    			ret = cst2xx_i2c_write(ts->client, temp_buf, 6);
    			if (ret < 0)
    				goto error_out;
			}
	#endif

		i2c_buf[0] = 0xA0;
		i2c_buf[1] = 0x04;
		i2c_buf[2] = 0xEE;
		ret = cst2xx_i2c_write(ts->client, i2c_buf, 3);
		if (ret < 0)
			goto error_out;

		msleep(600);

		for(retry=0;retry<10;retry++)
		{
			i2c_buf[0] = 0xA0;
			i2c_buf[1] = 0x05;
			ret = cst2xx_i2c_read_register(ts->client, i2c_buf, 1);

			if (ret < 0){

				msleep(100);
				continue;
			}
			else
			{
				if (i2c_buf[0] != 0x55){
					msleep(100);
					continue;
				}else{
	 				break;
				}

			}

		}
		if(retry==10)
		{
			goto error_out;
		}
	}

	i2c_buf[0] = 0xA0;
	i2c_buf[1] = 0x01;
	i2c_buf[2] = 0x00;
	ret = cst2xx_i2c_write(ts->client, i2c_buf, 3);
	if (ret < 0)
	goto error_out;

	i2c_buf[0] = 0xA0;
	i2c_buf[1] = 0x03;
	i2c_buf[2] = 0x00;
	ret = cst2xx_i2c_write(ts->client, i2c_buf, 3);

	if (i2c_buf != NULL) {
		kfree(i2c_buf);
		i2c_buf = NULL;
	}

	return 0;

error_out:
	if (i2c_buf != NULL) {
		kfree(i2c_buf);
		i2c_buf = NULL;
	}
	return -1;
}

static int cst2xx_read_checksum(struct hyn_ts *ts)
{
	int ret;
	int i;
	unsigned int  checksum;
	unsigned int  bin_checksum;
	unsigned char buf[4];
	const unsigned char *pData;

	for(i=0; i<10; i++)
	{
		buf[0] = 0xA0;
		buf[1] = 0x00;
		ret = cst2xx_i2c_read_register(ts->client, buf, 1);
		if(ret < 0)
		{
			msleep(2);
			continue;
		}

		if(buf[0]!=0)
			break;
		else
		msleep(2);
	}
	msleep(4);

	if(buf[0]==0x01)
	{
		buf[0] = 0xA0;
		buf[1] = 0x08;
		ret = cst2xx_i2c_read_register(ts->client, buf, 4);

		if(ret < 0)	return -1;

		//handle read data  --> checksum
		checksum = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);

        pData=(unsigned char  *)fwbin +7680-4;   //7*1024 +512
		bin_checksum = pData[0] + (pData[1]<<8) + (pData[2]<<16) + (pData[3]<<24);

	printk("hyn checksum ic:0x%x. bin:0x%x------\n", checksum, bin_checksum);

	if(checksum!=bin_checksum)
		{
			printk("hyn check sum error.\n");
			return -1;

		}

	}
	else
	{
		printk("hyn No checksum. buf[0]:%d.\n", buf[0]);
		return -1;
	}

	return 0;
}

static int cst2xx_update_firmware(struct i2c_client * client, struct hyn_ts *ts,
	unsigned char *pdata, int data_len)
{
	int ret;
	int retry;
	unsigned char buf[4];

	retry = 0;

start_flow:

	printk("hyn enter the update firmware.\n");

	disable_irq(ts->irq);

	msleep(20);
	ret = cst2xx_enter_download_mode(ts);
	if (ret < 0)
	{
		printk("hyn enter download mode failed.\n");
		goto fail_retry;
	}

	ret = cst2xx_download_program(pdata, data_len,ts);
	if (ret < 0)
	{
		printk("hyn download program failed.\n");
		goto fail_retry;
	}

	msleep(10);

	ret = cst2xx_read_checksum(ts);
	if(ret < 0){
		printk("hyn check the updating checksum error.\n");
		return ret;
	}
	else
	{
		buf[0] = 0xA0;  //exit program
		buf[1] = 0x06;
		buf[2] = 0xEE;
		ret = cst2xx_i2c_write(client, buf, 3);

		if(ret < 0)
			goto fail_retry;

	}

	printk("hyn download firmware succesfully.\n");

	msleep(100);

	hard_reset_chip(ts, 30);

	enable_irq(ts->irq);

	return 0;

fail_retry:
	if (retry < 4)
	{
		retry++;
		goto start_flow;
	}

	return -1;
}

static int cst2xx_boot_update_fw(struct i2c_client * client, struct hyn_ts *ts)
{
	return cst2xx_update_firmware(client, ts, fwbin, FW_BIN_SIZE);
}

static int cst2xx_check_code(struct hyn_ts *ts)
{
	int retry = 0;
	int ret;
	unsigned char buf[4];
	unsigned int fw_checksum,fw_version,fw_customer_id;
	unsigned int  bin_checksum,bin_version;
	const unsigned char *pData;

	buf[0] = 0xD0;
	buf[1] = 0x4C;
	while(retry++ < 3) {
		ret = cst2xx_i2c_read_register(ts->client, buf, 1);
		if (ret > 0) break;
		msleep(2);
	}
	if((buf[0]==226)||(buf[0]==237)||(buf[0]==240))
	{
		//checksum
		return 0;
	}
	else if(buf[0]==168)
	{
	    buf[0] = 0xD0;
		buf[1] = 0x49;
		while(retry++ < 3) {
			ret = cst2xx_i2c_read_register(ts->client, buf, 2);
			if (ret > 0) break;
			msleep(2);
		}
		fw_customer_id=(buf[0]<<8)+buf[1];
		printk("hyn fw_customer_id:%d. \r\n",fw_customer_id);

		//checksum
		buf[0] = 0xD2;
		buf[1] = 0x0C;
		while(retry++ < 5) {
			ret = cst2xx_i2c_read_register(ts->client, buf, 4);
			if (ret > 0) break;
			msleep(2);
		}

		fw_checksum = buf[3];
		fw_checksum <<= 8;
		fw_checksum |= buf[2];
		fw_checksum <<= 8;
		fw_checksum |= buf[1];
		fw_checksum <<= 8;
		fw_checksum |= buf[0];

		pData=(unsigned char  *)fwbin +7680-4;   //7*1024 +512
		bin_checksum = pData[0] + (pData[1]<<8) + (pData[2]<<16) + (pData[3]<<24);

 		if(fw_checksum!=bin_checksum)
		{
			 printk("hyn checksum is different******bin_checksum:0x%x, fw_checksum:0x%x. \r\n",bin_checksum,fw_checksum);

			//chip version
			buf[0] = 0xD2;
			buf[1] = 0x08;
			while(retry++ < 5) {
				ret = cst2xx_i2c_read_register(ts->client, buf, 4);
				if (ret > 0) break;
				msleep(2);
			}

			fw_version = buf[3];
			fw_version <<= 8;
			fw_version |= buf[2];
			fw_version <<= 8;
			fw_version |= buf[1];
			fw_version <<= 8;
			fw_version |= buf[0];

			pData=(unsigned char  *)fwbin +7680-8;   //7*1024 +512
			bin_version = pData[0] + (pData[1]<<8) + (pData[2]<<16) + (pData[3]<<24);

			printk("hyn bin_version is different******bin_version:0x%x, fw_version:0x%x. \r\n",bin_version,fw_version);

			if(bin_version>=fw_version)
			{
				ret = cst2xx_boot_update_fw(ts->client, ts);
				if(ret<0)
				{
					printk("hyn update firmware fail  . \r\n");
                    hard_reset_chip(ts, 20);
					return -2;
				}
				else   return  0;
			}
			else
			{
		    	printk("hyn bin_version is lower ,no need to update firmware.\n");
			return 0;
			}

		}
		else
		{
			printk("hyn checksum :0x%x is same,no need to update firmware.\n",fw_checksum);
			return 0;
		}

	}
	else
	{
		printk("hyn check code error. buf[0]:%d.\n", buf[0]);
		ret = cst2xx_boot_update_fw(ts->client, ts);
		if(ret<0) return -2;
		else      return  0;
	}
}

#ifdef HAVE_TOUCH_KEY
static void report_key(struct hyn_ts *ts, u16 x, u16 y)
{
	u16 i = 0;

	for(i = 0; i < MAX_KEY_NUM; i++) {
		if((hyn_key_data[i].x_min < x) && (x < hyn_key_data[i].x_max)&&(hyn_key_data[i].y_min < y) && (y < hyn_key_data[i].y_max)){
			key = hyn_key_data[i].key;
			input_report_key(ts->input, key, 1);
			input_sync(ts->input);
			key_state_flag = 1;
			break;
		}
	}
}
#endif

static void cst2xx_touch_down(struct input_dev *input_dev, s32 id,s32 x,s32 y,s32 w)
{
	s32 temp_w = (w>>1);

#ifdef ICS_SLOT_REPORT
	input_mt_slot(input_dev, id);
	input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 1);
	input_report_abs(input_dev, ABS_MT_TRACKING_ID, id);
	input_report_abs(input_dev, ABS_MT_POSITION_X, x);
	input_report_abs(input_dev, ABS_MT_POSITION_Y, y);
	input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, temp_w);
	input_report_abs(input_dev, ABS_MT_WIDTH_MAJOR, temp_w);
	input_report_abs(input_dev, ABS_MT_PRESSURE, temp_w);
#else
    input_report_key(input_dev, BTN_TOUCH, 1);
    input_report_abs(input_dev, ABS_MT_POSITION_X, x);
    input_report_abs(input_dev, ABS_MT_POSITION_Y, y);
    input_report_abs(input_dev, ABS_MT_TOUCH_MAJOR, temp_w);
    input_report_abs(input_dev, ABS_MT_WIDTH_MAJOR, temp_w);
    input_report_abs(input_dev, ABS_MT_TRACKING_ID, id);
    input_mt_sync(input_dev);
#endif

}

static void cst2xx_touch_up(struct input_dev *input_dev, int id)
{

#ifdef ICS_SLOT_REPORT
	input_mt_slot(input_dev, id);
	//input_report_abs(input_dev, ABS_MT_TRACKING_ID, -1);
	input_mt_report_slot_state(input_dev, MT_TOOL_FINGER, 0);
#else
	input_report_key(input_dev, BTN_TOUCH, 0);
	input_mt_sync(input_dev);
#endif

}

#ifdef ANDROID_TOOL_SURPORT   //debug tool support

#define CST2XX_PROC_DIR_NAME	"cst1xx_ts"
#define CST2XX_PROC_FILE_NAME	"cst1xx-update"

static struct proc_dir_entry *g_proc_dir, *g_update_file;
static int CMDIndex = 0;

#if 1
static struct file *cst2xx_open_fw_file(char *path)
{
	struct file * filp = NULL;
	int ret;

	//*old_fs_p = get_fs();
	//set_fs(KERNEL_DS);
	filp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(filp))
	{
		ret = PTR_ERR(filp);
		return NULL;
	}
	filp->f_op->llseek(filp, 0, 0);
    return filp;
}

static void cst2xx_close_fw_file(struct file * filp)
{
//	set_fs(old_fs);
	if(filp)
	    filp_close(filp,NULL);
}

static int cst2xx_read_fw_file(unsigned char *filename, unsigned char *pdata, int *plen)
{
	struct file *fp;
//	mm_segment_t old_fs;
	int size;
	int length;
	int ret = -1;

	if((pdata == NULL) || (strlen(filename) == 0))
	{
		printk("file name is null.\n");
		return ret;
	}
	fp = cst2xx_open_fw_file(filename);
	if(fp == NULL)
	{
        printk("Open bin file faild.path:%s.\n", filename);
		goto clean;
	}

	length = fp->f_op->llseek(fp, 0, SEEK_END);
	fp->f_op->llseek(fp, 0, 0);
	size = fp->f_op->read(fp, pdata, length, &fp->f_pos);
	if(size == length)
	{
		ret = 0;
		*plen = length;
	} else {
		printk("read bin file length fail****size:%d*******length:%d .\n", size,length);

	}

clean:
	cst2xx_close_fw_file(fp);
	return ret;
}
#else
static struct file *cst2xx_open_fw_file(char *path, mm_segment_t * old_fs_p)
{
	struct file * filp;
	int ret;

	*old_fs_p = get_fs();
	set_fs(KERNEL_DS);
	filp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(filp))
	{
		ret = PTR_ERR(filp);
		return NULL;
	}
	filp->f_op->llseek(filp, 0, 0);

	return filp;
}

static void cst2xx_close_fw_file(struct file * filp,mm_segment_t old_fs)
{
	set_fs(old_fs);
	if(filp)
	    filp_close(filp,NULL);
}

static int cst2xx_read_fw_file(unsigned char *filename, unsigned char *pdata, int *plen)
{
	struct file *fp;
	mm_segment_t old_fs;
	int size;
	int length;
	int ret = -1;

	if((pdata == NULL) || (strlen(filename) == 0))
		return ret;
	fp = cst2xx_open_fw_file(filename, &old_fs);
	if(fp == NULL)
	{
        printk("Open bin file faild.path:%s.\n", filename);
		goto clean;
	}

	length = fp->f_op->llseek(fp, 0, SEEK_END);
	fp->f_op->llseek(fp, 0, 0);
	size = fp->f_op->read(fp, pdata, length, &fp->f_pos);
	if(size == length)
	{
		ret = 0;
		*plen = length;
	} else {
		printk("read bin file length fail****size:%d*******length:%d .\n", size,length);

	}

clean:
	cst2xx_close_fw_file(fp, old_fs);
	return ret;
}

#endif
static int cst2xx_apk_fw_dowmload(struct i2c_client *client,
		unsigned char *pdata, int length) 
{
	int ret;

	ret = cst2xx_update_firmware(client, hyn_global_ts, pdata, FW_BIN_SIZE);
	if (ret < 0)
	{
		printk("online update fw failed.\n");
		return -1;
	}

	return 0;
}

static ssize_t cst2xx_proc_read_foobar(struct file *page,char __user *user_buf, size_t count, loff_t *data)
{
	unsigned char buf[512];
	int len = 0;
	int ret;

	printk("cst2xx_proc_read_foobar********CMDIndex:%d. \n",CMDIndex);

	disable_irq(hyn_global_ts->irq);

	if (CMDIndex == 0) {
		sprintf(buf,"Hynitron touchscreen driver 1.0.\n");
		//strcpy(page,buf);
		len = strlen(buf);
		ret = copy_to_user(user_buf,buf,len);

	}
	else if (CMDIndex == 1)
	{
		buf[0] = g_cst2xx_rx;
		buf[1] = g_cst2xx_tx;
		ret = copy_to_user(user_buf,buf,2);
		len = 2;
	}
	if(CMDIndex == 2 || CMDIndex == 3)
	{
		unsigned short rx,tx;
		int data_len;

		rx = g_cst2xx_rx;
		tx = g_cst2xx_tx;
		data_len = rx*tx*2 + 4 + (tx+rx)*2 + rx + rx; //374

		if(CMDIndex == 2)  //read diff
		{
			buf[0] = 0xD1;
			buf[1] = 0x0D;
		}
		else          //rawdata
		{
			buf[0] = 0xD1;
			buf[1] = 0x0A;
		}

		ret = cst2xx_i2c_write(hyn_global_ts->client, buf, 2);  
		if(ret < 0)
		{
			printk("Write command raw/diff mode failed.error:%d.\n", ret);
			goto END;
		}

		g_unnormal_mode = 1;
		msleep(14);

 		while(!gpio_get_value(hyn_global_ts->irq_pin ));

		buf[0] = 0x80;
		buf[1] = 0x01;
		ret = cst2xx_i2c_write(hyn_global_ts->client, buf, 2);
		if(ret < 0)
		{
			printk("Write command(0x8001) failed.error:%d.\n", ret);
			goto END;
		}
		ret = cst2xx_i2c_read(hyn_global_ts->client, &buf[2], data_len);
		if(ret < 0)
		{
			printk("Read raw/diff data failed.error:%d.\n", ret);
			goto END;
		}

		msleep(2);

		buf[0] = 0xD1;
		buf[1] = 0x08;
		ret = cst2xx_i2c_write(hyn_global_ts->client, buf, 2); 
		if(ret < 0)
		{
			printk("Write command normal mode failed.error:%d.\n", ret);
			goto END;
		}

		buf[0] = rx;
		buf[1] = tx;
    	ret = copy_to_user(user_buf,buf,data_len + 2);
    	len = data_len + 2;

		msleep(2);
	}

END:
	g_unnormal_mode = 0;

	CMDIndex = 0;
	enable_irq(hyn_global_ts->irq);

	return len;
}

static ssize_t cst2xx_proc_write_foobar(struct file *file, const char __user *buffer,size_t count, loff_t *data)
{
	unsigned char cmd[128];
	unsigned char *pdata = NULL;
	int len;
	int ret;
	int length = 6*1024;

	if (count > 128)
		len = 128;
	else
		len = count;

	if (copy_from_user(cmd, buffer, len))
	{
		printk("copy data from user space failed.\n");
		return -EFAULT;
	}

	printk("cst2xx_proc_write_foobar*********cmd:%d*****%d******len:%d .\r\n", cmd[0], cmd[1], len);

	if (cmd[0] == 0)
	{
		 pdata = kzalloc(sizeof(char)*length, GFP_KERNEL);
		if(pdata == NULL)
		{
			printk("zalloc GFP_KERNEL memory fail.\n");
			return -ENOMEM;
		}
		ret = cst2xx_read_fw_file(&cmd[1], pdata, &length);
		if(ret < 0)
	  	{
			printk("cst2xx_read_fw_file fail.\n");
			if(pdata != NULL)
			{
				kfree(pdata);
				pdata = NULL;
			}
			return -EPERM;
	  	}

		ret = cst2xx_apk_fw_dowmload(hyn_global_ts->client, pdata, length);
	  	if(ret < 0)
	  	{
	        printk("update firmware failed.\n");
			if(pdata != NULL)
			{
				kfree(pdata);
				pdata = NULL;
			}
	        return -EPERM;
		}

	}
	else if (cmd[0] == 2)
	{
		//cst2xx_touch_release();
		CMDIndex = cmd[1];
	}
	else if (cmd[0] == 3)
	{
		CMDIndex = 0;
	}

	return count;
}


static const struct file_operations proc_tool_debug_fops = {

	.owner		= THIS_MODULE,
	.read	    = cst2xx_proc_read_foobar,
	.write		= cst2xx_proc_write_foobar,

};
static int  cst2xx_proc_fs_init(void)
{
	int ret;

	g_proc_dir = proc_mkdir(CST2XX_PROC_DIR_NAME, NULL);
	if (g_proc_dir == NULL)
	{
		ret = -ENOMEM;
		goto out;
	}

	g_update_file = proc_create(CST2XX_PROC_FILE_NAME, 0777, g_proc_dir,&proc_tool_debug_fops);

	if (g_update_file == NULL)
	{
		ret = -ENOMEM;
		printk("proc_create CST2XX_PROC_FILE_NAME failed.\n");
		goto no_foo;
	}
/**************************************
	g_update_file = create_proc_entry(CST2XX_PROC_FILE_NAME, 0666, g_proc_dir);
	if (g_update_file == NULL)
	{
		ret = -ENOMEM;
		goto no_foo;
	}

	g_update_file->read_proc = cst2xx_proc_read_foobar;
	g_update_file->write_proc = cst2xx_proc_write_foobar;

************************************************/
	return 0;

no_foo:
	remove_proc_entry(CST2XX_PROC_FILE_NAME, g_proc_dir);
out:
	return ret;
}
#endif


static int report_flag = 0;
static void cst2xx_ts_worker(struct work_struct *work)
{
	//int rc, i;
	//u8 id, touches;
	//u16 x, y;
	u8 buf[60];//30
	u8 i2c_buf[8];
	u8 key_status;
	u8 key_id, finger_id, sw;
	int  input_x = 0;
	int  input_y = 0;
	int  input_w = 0;
	int  temp;
	u8   i, cnt_up, cnt_down;
	int  ret, idx;
	int  cnt, i2c_len, len_1, len_2;


#ifdef HYN_NOID_VERSION
	u32 tmp1;
	u8 buf[4] = {0};
	struct hyn_touch_info cinfo;
#endif

	struct hyn_ts *ts = container_of(work, struct hyn_ts,work);

	//print_info("=====cst2xx_ts_worker=====\n");

#ifdef TPD_PROC_DEBUG
	if(hyn_proc_flag == 1)
		goto schedule;
#endif

	//buf[0] = 0xD1;
	//buf[1] = 0x08;
	//ret = cst2xx_i2c_write(g_i2c_client, buf, 2);
	//if (ret < 0)
	//{
	//	printk("send get finger point cmd failed.\r\n");
	//	goto END;
	//}

	buf[0] = 0xD0;
	buf[1] = 0x00;
	ret = cst2xx_i2c_read_register(ts->client, buf, 7);
	if(ret < 0) {
		printk("hyn iic read touch point data failed.\n");
		goto OUT_PROCESS;
	}

	key_status = buf[0];

	if(buf[6] != 0xAB) {
		//printk("data is not valid..\r\n");
		goto OUT_PROCESS;
	}

	cnt = buf[5] & 0x7F;
	if(cnt>MAX_FINGERS) goto OUT_PROCESS;
	else if(cnt==0)     goto CLR_POINT;

	if(buf[5] == 0x80) {
		key_status = buf[0];
		key_id = buf[1];
		goto KEY_PROCESS;
	}
	else if(cnt == 0x01) {
		goto FINGER_PROCESS;
	}
	else {
		#ifdef TRANSACTION_LENGTH_LIMITED
		if((buf[5]&0x80) == 0x80) { //key
			i2c_len = (cnt - 1)*5 + 3;
			len_1   = i2c_len;
			for(idx=0; idx<i2c_len; idx+=6) {
			    i2c_buf[0] = 0xD0;
				i2c_buf[1] = 0x07+idx;

				if(len_1>=6) {
					len_2  = 6;
					len_1 -= 6;
				}
				else {
					len_2 = len_1;
					len_1 = 0;
				}

    			ret = cst2xx_i2c_read_register(ts->client, i2c_buf, len_2);
    			if(ret < 0) goto OUT_PROCESS;

				for(i=0; i<len_2; i++) {
					buf[5+idx+i] = i2c_buf[i];
				}
			}

			i2c_len   += 5;
			key_status = buf[i2c_len - 3];
			key_id     = buf[i2c_len - 2];
		}
		else {
			i2c_len = (cnt - 1)*5 + 1;
			len_1   = i2c_len;

			for(idx=0; idx<i2c_len; idx+=6) {
				i2c_buf[0] = 0xD0;
				i2c_buf[1] = 0x07+idx;

				if(len_1>=6) {
					len_2  = 6;
					len_1 -= 6;
				}
				else {
					len_2 = len_1;
					len_1 = 0;
				}

				ret = cst2xx_i2c_read_register(ts->client, i2c_buf, len_2);
				if (ret < 0) goto OUT_PROCESS;

				for(i=0; i<len_2; i++) {
					buf[5+idx+i] = i2c_buf[i];
				}
			}
			i2c_len += 5;
		}
		#else
		if ((buf[5]&0x80) == 0x80) {
			buf[5] = 0xD0;
			buf[6] = 0x07;
			i2c_len = (cnt - 1)*5 + 3;
			ret = cst2xx_i2c_read_register(ts->client, &buf[5], i2c_len);
			if (ret < 0)
				goto OUT_PROCESS;
			i2c_len += 5;
			key_status = buf[i2c_len - 3];
			key_id = buf[i2c_len - 2];
		}
		else {
			buf[5] = 0xD0;
			buf[6] = 0x07;
			i2c_len = (cnt - 1)*5 + 1;
			ret = cst2xx_i2c_read_register(ts->client, &buf[5], i2c_len);
			if (ret < 0)
				goto OUT_PROCESS;
			i2c_len += 5;
		}
		#endif

		if (buf[i2c_len - 1] != 0xAB) {
			goto OUT_PROCESS;
		}
	}

	if((cnt > 0) && (key_status & 0x80))  //both key and point
	{
        	if(report_flag==0xA5) goto KEY_PROCESS;
	}

FINGER_PROCESS:

	i2c_buf[0] = 0xD0;
	i2c_buf[1] = 0x00;
	i2c_buf[2] = 0xAB;
	ret = cst2xx_i2c_write(ts->client, i2c_buf, 3);
	if (ret < 0) {
		printk("hyn send read touch info ending failed.\r\n"); 
		hard_reset_chip(ts, 20);
	}

	idx = 0;
	cnt_up = 0;
	cnt_down = 0;
	for (i = 0; i < cnt; i++) {
		input_x = (unsigned int)((buf[idx + 1] << 4) | ((buf[idx + 3] >> 4) & 0x0F));
		input_y = (unsigned int)((buf[idx + 2] << 4) | (buf[idx + 3] & 0x0F));
		input_w = (unsigned int)(buf[idx + 4]);
		sw = (buf[idx] & 0x0F) >> 1;
		finger_id = (buf[idx] >> 4) & 0x0F;

	#ifdef SWAP_X_Y
		temp    = input_x;
		input_x = input_y;
		input_y = temp;
	#endif

	#ifdef X_POL
		input_x = SCREEN_MAX_X - input_x;
	#endif

	#ifdef Y_POL
		input_y = SCREEN_MAX_Y - input_y;
	#endif

	//printk("Point x:%d, y:%d, id:%d, sw:%d.\r\n", input_x, input_y, finger_id, sw);

		if (sw == 0x03) {
			cst2xx_touch_down(ts->input, finger_id, input_x, input_y, input_w);
			cnt_down++;

#ifdef HYN_GESTURE
			print_info("\n gsl_lcd_flag = %d ---- gsl_gesture_flag = %d .\n\n", gsl_lcd_flag, gsl_gesture_flag);
			if(1 == gsl_lcd_flag && 1 == gsl_gesture_flag){
				print_info("auto wake up lcd\n");
				rk_send_wakeup_key();
			}else{
				gsl_gesture_flag = 0;
			}
#endif

		}
		else {
		cnt_up++;
		#ifdef ICS_SLOT_REPORT
			cst2xx_touch_up(ts->input, finger_id);
		#endif
	}
		idx += 5;
	}

	#ifndef ICS_SLOT_REPORT
	if((cnt_up>0) && (cnt_down==0))
		cst2xx_touch_up(ts->input, 0);
	#endif

	if(cnt_down==0)  report_flag = 0;
	else report_flag = 0xCA;

	input_sync(ts->input);

	goto END;

KEY_PROCESS:
	i2c_buf[0] = 0xD0;
	i2c_buf[1] = 0x00;
	i2c_buf[2] = 0xAB;
	ret = cst2xx_i2c_write(ts->client, i2c_buf, 3);
	if (ret < 0) {
		printk("hyn send read touch info ending failed.\r\n"); 
	}

    #ifdef HAVE_TOUCH_KEY
	if(key_status&0x80) {
		if((key_status&0x7F)==0x03) {
			i = (key_id>>4)-1;
			key = hyn_key_data[i].key;
			input_report_key(ts->input, key, 1);

			report_flag = 0xA5;
		}
		else {
			input_report_key(ts->input, key, 0);
			report_flag = 0;
		}
	}
	#endif

	input_sync(ts->input);

	goto END;

CLR_POINT:
#ifdef SLEEP_CLEAR_POINT
	#ifdef ICS_SLOT_REPORT
		for(i=0; i<=MAX_CONTACTS; i++) {
			input_mt_slot(ts->input, i);
			input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
		}
	#else
		input_mt_sync(ts->input);
	#endif
		input_sync(ts->input);
#endif

OUT_PROCESS:
	i2c_buf[0] = 0xD0;
	i2c_buf[1] = 0x00;
	i2c_buf[2] = 0xAB;
	ret = cst2xx_i2c_write(ts->client, i2c_buf, 3);
	if (ret < 0) {
		printk("send read touch info ending failed.\n");
		hard_reset_chip(ts, 20);
	}

END:
#ifdef HYN_MONITOR
	if(i2c_lock_flag != 0)
		goto i2c_lock_schedule;
	else
		i2c_lock_flag = 1;
#endif

#ifdef TPD_PROC_DEBUG
schedule:
#endif

#ifdef HYN_MONITOR
	i2c_lock_flag = 0;
i2c_lock_schedule:
#endif
	//enable_irq(ts->irq);
	cnt=0;
	//printk("========cst2xx_ts_worker end=========\n");
}

#ifdef HYN_MONITOR
static void hyn_monitor_worker(struct work_struct *work)
{
	//u8 write_buf[4] = {0};
	u8 read_buf[4]  = {0};
	char init_chip_flag = 0;

//	print_info("----------------hyn_monitor_worker-----------------\n");
   struct hyn_ts *ts = container_of(work, struct hyn_ts,hyn_monitor_work.work);
	if(i2c_lock_flag != 0) {
	    //i2c_lock_flag=1;
	    goto queue_monitor_work;
	}
	else
		i2c_lock_flag = 1;

	//hyn_ts_read(ts->client, 0x80, read_buf, 4);
	//printk("======read 0x80: %x %x %x %x ======tony0geshu\n",read_buf[3], read_buf[2], read_buf[1], read_buf[0]);

	hyn_ts_read(ts->client, 0xb0, read_buf, 4);
	if(read_buf[3] != 0x5a || read_buf[2] != 0x5a || read_buf[1] != 0x5a || read_buf[0] != 0x5a)
		b0_counter ++;
	else
		b0_counter = 0;

	if(b0_counter > 1) {
		printk("======read 0xb0: %x %x %x %x ======\n",read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
		init_chip_flag = 1;
		b0_counter = 0;
		goto queue_monitor_init_chip;
	}

	hyn_ts_read(ts->client, 0xb4, read_buf, 4);
	int_2nd[3] = int_1st[3];
	int_2nd[2] = int_1st[2];
	int_2nd[1] = int_1st[1];
	int_2nd[0] = int_1st[0];
	int_1st[3] = read_buf[3];
	int_1st[2] = read_buf[2];
	int_1st[1] = read_buf[1];
	int_1st[0] = read_buf[0];

	//printk("======int_1st: %x %x %x %x , int_2nd: %x %x %x %x ======\n",int_1st[3], int_1st[2], int_1st[1], int_1st[0], int_2nd[3], int_2nd[2],int_2nd[1],int_2nd[0]);

	if(int_1st[3] == int_2nd[3] && int_1st[2] == int_2nd[2] &&int_1st[1] == int_2nd[1] && int_1st[0] == int_2nd[0])  {
		printk("======int_1st: %x %x %x %x , int_2nd: %x %x %x %x ======\n",int_1st[3], int_1st[2], int_1st[1], int_1st[0], int_2nd[3], int_2nd[2],int_2nd[1],int_2nd[0]);
		init_chip_flag = 1;
		goto queue_monitor_init_chip;
	}


	hyn_ts_read(ts->client, 0xbc, read_buf, 4);
	if(read_buf[3] != 0 || read_buf[2] != 0 || read_buf[1] != 0 || read_buf[0] != 0)
		bc_counter++;
	else
		bc_counter = 0;
	if(bc_counter > 1) {
		printk("======read 0xbc: %x %x %x %x======\n",read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
		init_chip_flag = 1;
		bc_counter = 0;
	}


/*
	write_buf[3] = 0x01;
	write_buf[2] = 0xfe;
	write_buf[1] = 0x10;
	write_buf[0] = 0x00;
	hyn_ts_write(ts->client, 0xf0, write_buf, 4);
	hyn_ts_read(ts->client, 0x10, read_buf, 4);
	hyn_ts_read(ts->client, 0x10, read_buf, 4);

	if(read_buf[3] < 10 && read_buf[2] < 10 && read_buf[1] < 10 && read_buf[0] < 10)
		dac_counter ++;
	else
		dac_counter = 0;

	if(dac_counter > 1)
	{
		printk("======read DAC1_0: %x %x %x %x ======\n",read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
		init_chip_flag = 1;
		dac_counter = 0;
	}
*/
queue_monitor_init_chip:
	if(init_chip_flag)
		init_chip(ts->client,ts);

	i2c_lock_flag = 0;

queue_monitor_work:
	queue_delayed_work(hyn_monitor_workqueue, &ts->hyn_monitor_work, 100);
}
#endif

static irqreturn_t hyn_ts_irq(int irq, void *dev_id)
{
	///struct hyn_ts *ts = dev_id;
    struct hyn_ts *ts = (struct hyn_ts*)dev_id;
	//printk("========cst2xx Interrupt=========\n");

#ifdef HYN_GESTURE
	if(1 == gsl_lcd_flag)
		gsl_gesture_flag = 1;
#endif

	//disable_irq_nosync(ts->irq);

	if (!work_pending(&ts->work))
	{
		queue_work(ts->wq, &ts->work);
	}

	return IRQ_HANDLED;
}

static int cst2xx_ts_init(struct i2c_client *client, struct hyn_ts *ts)
{
	struct input_dev *input_device;
	int rc = 0;

	printk("hyn cst2xx Enter %s\n", __func__);

	//input_device = devm_input_allocate_device(&ts->client->dev);
	input_device = input_allocate_device();
	if (!input_device) {
		rc = -ENOMEM;
		goto error_alloc_dev;
	}

	ts->input = input_device;
	input_device->name = CST2XX_I2C_NAME;
	input_device->id.bustype = BUS_I2C;
	input_device->dev.parent = &client->dev;
	input_set_drvdata(input_device, ts);

#ifdef ICS_SLOT_REPORT
	__set_bit(EV_ABS, input_device->evbit);
	__set_bit(EV_KEY, input_device->evbit);
	__set_bit(EV_REP, input_device->evbit);
	__set_bit(INPUT_PROP_DIRECT, input_device->propbit);
	input_mt_init_slots(input_device, (MAX_CONTACTS+1), 0);
#else
	input_set_abs_params(input_device,ABS_MT_TRACKING_ID, 0, (MAX_CONTACTS+1), 0, 0);
	set_bit(EV_ABS, input_device->evbit);
	set_bit(EV_KEY, input_device->evbit);
	__set_bit(INPUT_PROP_DIRECT, input_device->propbit);
	input_device->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
#endif

#ifdef HAVE_TOUCH_KEY
	input_device->evbit[0] = BIT_MASK(EV_KEY);
	//input_device->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	for (i = 0; i < MAX_KEY_NUM; i++)
		set_bit(key_array[i], input_device->keybit);
#endif

	set_bit(ABS_MT_POSITION_X, input_device->absbit);
	set_bit(ABS_MT_POSITION_Y, input_device->absbit);
	set_bit(ABS_MT_TOUCH_MAJOR, input_device->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, input_device->absbit);

	input_set_abs_params(input_device,ABS_MT_POSITION_X, 0, SCREEN_MAX_X, 0, 0);
	input_set_abs_params(input_device,ABS_MT_POSITION_Y, 0, SCREEN_MAX_Y, 0, 0);
	input_set_abs_params(input_device,ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_device,ABS_MT_WIDTH_MAJOR, 0, 200, 0, 0);

	//client->irq = IRQ_PORT;
	//ts->irq = client->irq;
	//create_workqueue

	ts->wq = create_singlethread_workqueue("kworkqueue_ts");
	if (!ts->wq) {
		dev_err(&client->dev, "hyn Could not create workqueue\n");
		goto error_wq_create;
	}
	flush_workqueue(ts->wq);

	INIT_WORK(&ts->work, cst2xx_ts_worker);

	rc = input_register_device(input_device);
	if (rc)
		goto error_unreg_device;

	return 0;

error_unreg_device:
	destroy_workqueue(ts->wq);
error_wq_create:
	input_free_device(input_device);
error_alloc_dev:
	//kfree(ts->touch_data);
	return rc;
}

static int hyn_ts_suspend(struct device *dev)
{
	struct hyn_ts *ts = dev_get_drvdata(dev);
	int i;

	printk("I'am in hyn_ts_suspend() start\n");

#ifdef HYN_MONITOR
	printk( "hyn_ts_suspend () : cancel hyn_monitor_work\n");
	cancel_delayed_work_sync(&ts->hyn_monitor_work);
#endif

#ifdef HYN_GESTURE
//	disable_irq_nosync(ts->irq);
#else
	disable_irq_nosync(ts->irq);

	if(h_wake_pin != 0) {
	    gpio_direction_output(ts->rst_pin, 0);
	}
#endif

#ifdef SLEEP_CLEAR_POINT
	msleep(10); 
	#ifdef ICS_SLOT_REPORT
	for(i=1; i<=MAX_CONTACTS; i++) {
		input_mt_slot(ts->input, i);
		input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
	}
	#else
	input_mt_sync(ts->input);
	#endif
	input_sync(ts->input);

#endif

	return 0;
}

static int hyn_ts_resume(struct device *dev)
{
	struct hyn_ts *ts = dev_get_drvdata(dev);
	int i, rc;

	printk("I'am in hyn_ts_resume() start\n");

	hard_reset_chip(ts, 30);

#ifdef SLEEP_CLEAR_POINT
	#ifdef ICS_SLOT_REPORT
	for(i=1; i<=MAX_CONTACTS; i++) {
		input_mt_slot(ts->input, i);
		input_report_abs(ts->input, ABS_MT_TRACKING_ID, -1);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
	}
	#else
	input_mt_sync(ts->input);
	#endif
	input_sync(ts->input);
#endif

#ifdef HYN_MONITOR
	printk( "hyn_ts_resume () : queue hyn_monitor_work\n");
	queue_delayed_work(hyn_monitor_workqueue, &ts->hyn_monitor_work, 300);
#endif


#ifdef HYN_GESTURE
//	enable_irq(ts->irq);
#else
	enable_irq(ts->irq);
#endif

	msleep(200);

	rc = cst2xx_check_code(ts);
	if(rc < 0){
		printk("hyn check code error.\n");
		return rc;
	}
	return 0;
}

static int hyn_ts_early_suspend(struct tp_device *tp_d)
{
	struct hyn_ts *ts = container_of(tp_d, struct hyn_ts, tp);

#ifdef HYN_GESTURE
	gsl_lcd_flag = 1;
#endif

	printk("[CST2XX] Enter %s\n", __func__);
	hyn_ts_suspend(&ts->client->dev);
	return 0;
}

static int hyn_ts_late_resume(struct tp_device *tp_d)
{
	struct hyn_ts *ts = container_of(tp_d, struct hyn_ts, tp);

#ifdef HYN_GESTURE
	gsl_lcd_flag = 0;
	gsl_gesture_flag = 0;
#endif

	printk("[CST2XX] Enter %s\n", __func__);
	hyn_ts_resume(&ts->client->dev);
	return 0;
}


static int  hyn_ts_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{

	int rc;
	struct hyn_ts *ts=NULL;
	struct device_node *np = client->dev.of_node;
	enum of_gpio_flags wake_flags;
	unsigned long irq_flags;

	printk("cst2xx enter %s\n", __func__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "hyn I2C functionality not supported\n");
		return -ENODEV;
	}

	ts = devm_kzalloc(&client->dev,sizeof(*ts), GFP_KERNEL);
	//ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;
	printk("==kzalloc success=\n");

	ts->client = client;

	i2c_set_clientdata(client, ts);
	ts->device_id = id->driver_data;

    //×¢ÒâÖÐ¶ÏµÄÃû×Ö irq
	ts->irq_pin = of_get_named_gpio_flags(np, "irq-gpio", 0, (enum of_gpio_flags *)&irq_flags);
	ts->rst_pin = of_get_named_gpio_flags(np, "wake-gpio", 0, &wake_flags);

	if (gpio_is_valid(ts->rst_pin)) {
		rc = devm_gpio_request_one(&client->dev, ts->rst_pin, (wake_flags & OF_GPIO_ACTIVE_LOW) ? GPIOF_OUT_INIT_LOW : GPIOF_OUT_INIT_HIGH, "cst21680 wake pin");
		if (rc != 0) {
			dev_err(&client->dev, "cst21680 wake pin error\n");
			//return -EIO;
		}
		else
		{
		    h_wake_pin = ts->rst_pin;
		}
		//msleep(100);
	} else {
		dev_info(&client->dev, "wake pin invalid\n");
	}

	if (gpio_is_valid(ts->irq_pin)) {
		rc = 0x00;
		//rc = devm_gpio_request_one(&client->dev, ts->irq_pin, IRQF_TRIGGER_RISING, "gslX680 irq pin");
			//printk("huang-GPIOF_OUT_INIT_LOW=%d\n\n",GPIOF_OUT_INIT_LOW);
			//	printk("huang-GPIOF_OUT_INIT_LOW=%d\n\n",IRQF_TRIGGER_RISING);
			//rc = request_irq(client->irq,gsl_ts_irq,IRQF_TRIGGER_RISING,client->name,ts);
			//printk("huang-GPIOF_OUT_INIT_LOW=%d\n\n",IRQF_TRIGGER_RISING);
		if (rc != 0) {
			dev_err(&client->dev, "cst21680 irq pin error\n");
			return -EIO;
		}
	} else {
		dev_info(&client->dev, "irq pin invalid\n");
	}


	msleep(40);	 //runing

	rc = cst2xx_test_i2c(client);
	if (rc < 0) {
		dev_err(&client->dev, "hyn cst2xx test iic error.\n");
		return rc;
	}

	msleep(20);
	rc = cst2xx_check_code(ts);
	if(rc < 0){
		printk("hyn check code error.\n");
		return rc;
	}

	rc = cst2xx_ts_init(client, ts);
	if (rc < 0) {
		printk("hyn CST2XX init failed\n");
		goto error_mutex_destroy;
	}

	ts->irq = gpio_to_irq(ts->irq_pin);

	printk("cst2xx request ts->irq is :%d\n", ts->irq);
	#if 0
	rc = request_irq(client->irq, hyn_ts_irq, IRQF_TRIGGER_RISING, client->name, ts);
	if (rc < 0) {
		printk( "hyn_probe: request irq failed\n");
		goto error_mutex_destroy;
	}
	#endif
	if(ts->irq)
	{
		rc = devm_request_threaded_irq(&client->dev, ts->irq, NULL, hyn_ts_irq, IRQF_TRIGGER_RISING | IRQF_ONESHOT, client->name, ts);
		if (rc != 0) {
			printk(KERN_ALERT "Cannot allocate ts INT!ERRNO:%d\n", rc);
			goto error_mutex_destroy;
		}
		disable_irq(ts->irq);
	}
	else
	{
		printk("cst21680 irq req fail\n");
		goto error_mutex_destroy;
	}

	/* create debug attribute */
	//rc = device_create_file(&ts->input->dev, &dev_attr_debug_enable);
	ts->tp.tp_suspend = hyn_ts_early_suspend;
	ts->tp.tp_resume = hyn_ts_late_resume;
	tp_register_fb(&ts->tp);

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	//ts->early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 1;
	ts->early_suspend.suspend = hyn_ts_early_suspend;
	ts->early_suspend.resume = hyn_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

#ifdef HYN_MONITOR
	printk( "hyn_ts_probe () : queue hyn_monitor_workqueue\n");

	INIT_DELAYED_WORK(&ts->hyn_monitor_work, hyn_monitor_worker);
	hyn_monitor_workqueue = create_singlethread_workqueue("hyn_monitor_workqueue");
	queue_delayed_work(hyn_monitor_workqueue, &ts->hyn_monitor_work, 1000);
#endif

#ifdef ANDROID_TOOL_SURPORT
	cst2xx_proc_fs_init();
	hyn_global_ts=ts;
#endif

#ifdef TPD_PROC_DEBUG

	hyn_config_proc = create_proc_entry(HYN_CONFIG_PROC_FILE, 0666, NULL);
	printk("[tp-hyn] [%s] hyn_config_proc = %x \n",__func__,hyn_config_proc);
	if (hyn_config_proc == NULL)
	{
		print_info("create_proc_entry %s failed\n", HYN_CONFIG_PROC_FILE);
	}
	else
	{
		hyn_config_proc->read_proc = hyn_config_read_proc;
		hyn_config_proc->write_proc = hyn_config_write_proc;
	}
#endif

	enable_irq(ts->irq);
	printk("[CST2XX] End %s\n", __func__);
	return 0;

error_mutex_destroy:
	input_free_device(ts->input);
	return rc;
}

static int hyn_ts_remove(struct i2c_client *client)
{
	struct hyn_ts *ts = i2c_get_clientdata(client);
	printk("==hyn_ts_remove=\n");

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif

#ifdef HYN_MONITOR
	cancel_delayed_work_sync(&ts->hyn_monitor_work);
	destroy_workqueue(hyn_monitor_workqueue);
#endif

	device_init_wakeup(&client->dev, 0);
	cancel_work_sync(&ts->work);
	destroy_workqueue(ts->wq);
	input_unregister_device(ts->input);
	//device_remove_file(&ts->input->dev, &dev_attr_debug_enable);

	//kfree(ts->touch_data);
	return 0;
}

#if 1
static struct of_device_id hyn_ts_ids[] = {
	{ .compatible = "cst2xx" },
	{ }
};
#endif

static const struct i2c_device_id hyn_ts_id[] = {
	{CST2XX_I2C_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, hyn_ts_id);

static struct i2c_driver hyn_ts_driver = {
	.driver = {
		.name = CST2XX_I2C_NAME,
		.owner = THIS_MODULE,
        .of_match_table = of_match_ptr(hyn_ts_ids),
	},

#ifndef CONFIG_HAS_EARLYSUSPEND
  //  .suspend	= hyn_ts_suspend,
  //  .resume	= hyn_ts_resume,
#endif
	.probe		= hyn_ts_probe,
	.remove		= hyn_ts_remove,
	.id_table	= hyn_ts_id,
};

static int __init hyn_ts_init(void)
{
    int ret;
	//printk("==hyn_ts_init==\n");
	ret = i2c_add_driver(&hyn_ts_driver);
	//printk("ret=%d\n",ret);
	return ret;
}
static void __exit hyn_ts_exit(void)
{
	//printk("==hyn_ts_exit==\n");
	i2c_del_driver(&hyn_ts_driver);
	return;
}

module_init(hyn_ts_init);
module_exit(hyn_ts_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HYNCST2XX touchscreen controller driver");
MODULE_AUTHOR("Tim.Tan");
MODULE_ALIAS("platform:hyn_ts");

