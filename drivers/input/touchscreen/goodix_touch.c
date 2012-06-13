/*---------------------------------------------------------------------------------------------------------
 * driver/input/touchscreen/goodix_touch.c
 *
 * Copyright(c) 2010 Goodix Technology Corp.
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
 * Change Date:
 *		2010.11.11, add point_queue's definiens.
 *
 * 		2011.03.09, rewrite point_queue's definiens.
 *
 * 		2011.05.12, delete point_queue for Android 2.2/Android 2.3 and so on.
 *
 *---------------------------------------------------------------------------------------------------------*/
#include <linux/i2c.h>
#include <linux/input.h>
#include "goodix_touch.h"
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <mach/irqs.h>
#include <mach/system.h>
#include <mach/hardware.h>
#include <plat/sys_config.h>
#include "ctp_platform_ops.h"

#ifdef CONFIG_HAS_EARLYSUSPEND
    #include <linux/pm.h>
    #include <linux/earlysuspend.h>
#endif

#define FOR_TSLIB_TEST
//#define PRINT_INT_INFO
//#define PRINT_POINT_INFO
#define PRINT_SUSPEND_INFO
#define TEST_I2C_TRANSFER

//#undef CONFIG_HAS_EARLYSUSPEND

#ifndef GUITAR_GT80X
#error The code does not match the hardware version.
#endif

struct goodix_ts_data {
	int retry;
	int panel_type;
	char phys[32];
	struct i2c_client *client;
	struct input_dev *input_dev;
	uint8_t use_irq;
	uint8_t use_shutdown;
	uint32_t gpio_shutdown;
	uint32_t gpio_irq;
	uint32_t screen_width;
	uint32_t screen_height;
	struct ts_event		event;
	struct hrtimer timer;
	struct work_struct  work;
	int (*power)(struct goodix_ts_data * ts, int on);
#ifdef CONFIG_HAS_EARLYSUSPEND
    struct early_suspend early_suspend;
#endif
};

const char *f3x_ts_name = "gt80x";
static struct workqueue_struct *goodix_wq;
#define X_DIFF (800)

#ifdef PRINT_POINT_INFO
#define print_point_info(fmt, args...)   \
        do{                              \
                printk(fmt, ##args);     \
        }while(0)
#else
#define print_point_info(fmt, args...)   //
#endif

#ifdef PRINT_INT_INFO
#define print_int_info(fmt, args...)     \
        do{                              \
                printk(fmt, ##args);     \
        }while(0)
#else
#define print_int_info(fmt, args...)   //
#endif
///////////////////////////////////////////////
//specific tp related macro: need be configured for specific tp
#define CTP_IRQ_NO			(gpio_int_info[0].port_num)
#define CTP_IRQ_MODE			(NEGATIVE_EDGE)
#define CTP_NAME			GOODIX_I2C_NAME
#define TS_RESET_LOW_PERIOD		(15)
#define TS_INITIAL_HIGH_PERIOD		(15)
#define TS_WAKEUP_LOW_PERIOD	(100)
#define TS_WAKEUP_HIGH_PERIOD	(100)
#define TS_POLL_DELAY			(10)	/* ms delay between samples */
#define TS_POLL_PERIOD			(10)	/* ms delay between samples */
#define SCREEN_MAX_HEIGHT		(screen_max_x)
#define SCREEN_MAX_WIDTH		(screen_max_y)
#define PRESS_MAX			(255)

static void* __iomem gpio_addr = NULL;
static int gpio_int_hdle = 0;
static int gpio_wakeup_hdle = 0;
static int gpio_reset_hdle = 0;
static int gpio_wakeup_enable = 1;
static int gpio_reset_enable = 1;
static user_gpio_set_t gpio_int_info[1];

static int screen_max_x = 0;
static int screen_max_y = 0;
static int revert_x_flag = 0;
static int revert_y_flag = 0;
static int exchange_x_y_flag = 0;
static __u32 twi_addr = 0;
static __u32 twi_id = 0;
static int	int_cfg_addr[]={PIO_INT_CFG0_OFFSET,PIO_INT_CFG1_OFFSET,
			PIO_INT_CFG2_OFFSET, PIO_INT_CFG3_OFFSET};
/* Addresses to scan */
union{
	unsigned short dirty_addr_buf[2];
	const unsigned short normal_i2c[2];
}u_i2c_addr = {{0x00},};

/*
 * ctp_get_pendown_state  : get the int_line data state,
 *
 * return value:
 *             return PRESS_DOWN: if down
 *             return FREE_UP: if up,
 *             return 0: do not need process, equal free up.
 */
static int ctp_get_pendown_state(void)
{
	unsigned int reg_val;
	static int state = FREE_UP;

	//get the input port state
	reg_val = readl(gpio_addr + PIOH_DATA);
	//printk("reg_val = %x\n",reg_val);
	if(!(reg_val & (1<<CTP_IRQ_NO))){
		state = PRESS_DOWN;
		print_int_info("pen down. \n");
	}else{ //touch panel is free up
		state = FREE_UP;
		print_int_info("free up. \n");
	}
	return state;
}

/**
 * ctp_clear_penirq - clear int pending
 *
 */
static void ctp_clear_penirq(void)
{
	int reg_val;
	//clear the IRQ_EINT29 interrupt pending
	//printk("clear pend irq pending\n");
	reg_val = readl(gpio_addr + PIO_INT_STAT_OFFSET);
	//writel(reg_val,gpio_addr + PIO_INT_STAT_OFFSET);
	//writel(reg_val&(1<<(IRQ_EINT21)),gpio_addr + PIO_INT_STAT_OFFSET);
	if((reg_val = (reg_val&(1<<(CTP_IRQ_NO))))){
		print_int_info("==CTP_IRQ_NO=\n");
		writel(reg_val,gpio_addr + PIO_INT_STAT_OFFSET);
	}
	return;
}

/**
 * ctp_set_irq_mode - according sysconfig's subkey "ctp_int_port" to config int port.
 *
 * return value:
 *              0:      success;
 *              others: fail;
 */
static int ctp_set_irq_mode(char *major_key , char *subkey, ext_int_mode int_mode)
{
	int ret = 0;
	__u32 reg_num = 0;
	__u32 reg_addr = 0;
	__u32 reg_val = 0;
	//config gpio to int mode
	pr_info("%s: config gpio to int mode. \n", __func__);
#ifndef SYSCONFIG_GPIO_ENABLE
#else
	if(gpio_int_hdle){
		gpio_release(gpio_int_hdle, 2);
	}
	gpio_int_hdle = gpio_request_ex(major_key, subkey);
	if(!gpio_int_hdle){
		pr_info("request tp_int_port failed. \n");
		ret = -1;
		goto request_tp_int_port_failed;
	}
	gpio_get_one_pin_status(gpio_int_hdle, gpio_int_info, subkey, 1);
	pr_info("%s, %d: gpio_int_info, port = %d, port_num = %d. \n", __func__, __LINE__, \
		gpio_int_info[0].port, gpio_int_info[0].port_num);
#endif

#ifdef AW_GPIO_INT_API_ENABLE
#else
	pr_info(" INTERRUPT CONFIG\n");
	reg_num = (gpio_int_info[0].port_num)%8;
	reg_addr = (gpio_int_info[0].port_num)/8;
	reg_val = readl(gpio_addr + int_cfg_addr[reg_addr]);
	reg_val &= (~(7 << (reg_num * 4)));
	reg_val |= (int_mode << (reg_num * 4));
	writel(reg_val,gpio_addr+int_cfg_addr[reg_addr]);

	ctp_clear_penirq();

	reg_val = readl(gpio_addr+PIO_INT_CTRL_OFFSET);
	reg_val |= (1 << (gpio_int_info[0].port_num));
	writel(reg_val,gpio_addr+PIO_INT_CTRL_OFFSET);

	udelay(1);
#endif

request_tp_int_port_failed:
	return ret;
}

/**
 * ctp_set_gpio_mode - according sysconfig's subkey "ctp_io_port" to config io port.
 *
 * return value:
 *              0:      success;
 *              others: fail;
 */
static int ctp_set_gpio_mode(void)
{
	//int reg_val;
	int ret = 0;
	//config gpio to io mode
	printk("%s: config gpio to io mode. \n", __func__);
#ifndef SYSCONFIG_GPIO_ENABLE
#else
	if(gpio_int_hdle){
		gpio_release(gpio_int_hdle, 2);
	}
	gpio_int_hdle = gpio_request_ex("ctp_para", "ctp_io_port");
	if(!gpio_int_hdle){
		printk("request ctp_io_port failed. \n");
		ret = -1;
		goto request_tp_io_port_failed;
	}
#endif
	return ret;

request_tp_io_port_failed:
	return ret;
}

/**
 * ctp_judge_int_occur - whether interrupt occur.
 *
 * return value:
 *              0:      int occur;
 *              others: no int occur;
 */
static int ctp_judge_int_occur(void)
{
	//int reg_val[3];
	int reg_val;
	int ret = -1;

	reg_val = readl(gpio_addr + PIO_INT_STAT_OFFSET);
	if(reg_val&(1<<(CTP_IRQ_NO))){
		ret = 0;
	}
	return ret;
}

/**
 * ctp_free_platform_resource - corresponding with ctp_init_platform_resource
 *
 */
static void ctp_free_platform_resource(void)
{
	printk("=======%s=========.\n", __func__);
	if(gpio_addr){
		iounmap(gpio_addr);
	}

	if(gpio_int_hdle){
		gpio_release(gpio_int_hdle, 2);
	}

	if(gpio_wakeup_hdle){
		gpio_release(gpio_wakeup_hdle, 2);
	}

	if(gpio_reset_hdle){
		gpio_release(gpio_reset_hdle, 2);
	}

	return;
}


/**
 * ctp_init_platform_resource - initialize platform related resource
 * return value: 0 : success
 *               -EIO :  i/o err.
 *
 */
static int ctp_init_platform_resource(void)
{
	int ret = 0;

	gpio_addr = ioremap(PIO_BASE_ADDRESS, PIO_RANGE_SIZE);
	//printk("%s, gpio_addr = 0x%x. \n", __func__, gpio_addr);
	if(!gpio_addr) {
		ret = -EIO;
		goto exit_ioremap_failed;
	}
	//    gpio_wakeup_enable = 1;
	gpio_wakeup_hdle = gpio_request_ex("ctp_para", "ctp_wakeup");
	if(!gpio_wakeup_hdle) {
		pr_warning("%s: tp_wakeup request gpio fail!\n", __func__);
		gpio_wakeup_enable = 0;
	}

	gpio_reset_hdle = gpio_request_ex("ctp_para", "ctp_reset");
	if(!gpio_reset_hdle) {
		pr_warning("%s: tp_reset request gpio fail!\n", __func__);
		gpio_reset_enable = 0;
	}

	return ret;

exit_ioremap_failed:
	ctp_free_platform_resource();
	return ret;
}


/**
 * ctp_fetch_sysconfig_para - get config info from sysconfig.fex file.
 * return value:
 *                    = 0; success;
 *                    < 0; err
 */
static int ctp_fetch_sysconfig_para(void)
{
	int ret = -1;
	int ctp_used = -1;
	char name[I2C_NAME_SIZE];
	script_parser_value_type_t type = SCRIPT_PARSER_VALUE_TYPE_STRING;

	printk("%s. \n", __func__);

	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_used", &ctp_used, 1)){
		pr_err("%s: script_parser_fetch err. \n", __func__);
		goto script_parser_fetch_err;
	}
	if(1 != ctp_used){
		pr_err("%s: ctp_unused. \n",  __func__);
		//ret = 1;
		return ret;
	}

	if(SCRIPT_PARSER_OK != script_parser_fetch_ex("ctp_para", "ctp_name", (int *)(&name), &type, sizeof(name)/sizeof(int))){
		pr_err("%s: script_parser_fetch err. \n", __func__);
		goto script_parser_fetch_err;
	}
	if(strcmp(CTP_NAME, name)){
		pr_err("%s: name %s does not match CTP_NAME. \n", __func__, name);
		pr_err(CTP_NAME);
		//ret = 1;
		return ret;
	}

	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_twi_addr", &twi_addr, sizeof(twi_addr)/sizeof(__u32))){
		pr_err("%s: script_parser_fetch err. \n", name);
		goto script_parser_fetch_err;
	}
	//big-endian or small-endian?
	//printk("%s: before: ctp_twi_addr is 0x%x, dirty_addr_buf: 0x%hx. dirty_addr_buf[1]: 0x%hx \n", __func__, twi_addr, u_i2c_addr.dirty_addr_buf[0], u_i2c_addr.dirty_addr_buf[1]);
	u_i2c_addr.dirty_addr_buf[0] = twi_addr;
	u_i2c_addr.dirty_addr_buf[1] = I2C_CLIENT_END;
	printk("%s: after: ctp_twi_addr is 0x%x, dirty_addr_buf: 0x%hx. dirty_addr_buf[1]: 0x%hx \n", __func__, twi_addr, u_i2c_addr.dirty_addr_buf[0], u_i2c_addr.dirty_addr_buf[1]);
	//printk("%s: after: ctp_twi_addr is 0x%x, u32_dirty_addr_buf: 0x%hx. u32_dirty_addr_buf[1]: 0x%hx \n", __func__, twi_addr, u32_dirty_addr_buf[0],u32_dirty_addr_buf[1]);

	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_twi_id", &twi_id, sizeof(twi_id)/sizeof(__u32))){
		pr_err("%s: script_parser_fetch err. \n", name);
		goto script_parser_fetch_err;
	}
	printk("%s: ctp_twi_id is %d. \n", __func__, twi_id);

	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_screen_max_x", &screen_max_x, 1)){
		pr_err("%s: script_parser_fetch err. \n", __func__);
		goto script_parser_fetch_err;
	}
	pr_info("%s: screen_max_x = %d. \n", __func__, screen_max_x);

	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_screen_max_y", &screen_max_y, 1)){
		pr_err("%s: script_parser_fetch err. \n", __func__);
		goto script_parser_fetch_err;
	}
	pr_info("%s: screen_max_y = %d. \n", __func__, screen_max_y);

	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_revert_x_flag", &revert_x_flag, 1)){
		pr_err("%s: script_parser_fetch err. \n", __func__);
		goto script_parser_fetch_err;
	}
	pr_info("%s: revert_x_flag = %d. \n", __func__, revert_x_flag);

	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_revert_y_flag", &revert_y_flag, 1)){
		pr_err("%s: script_parser_fetch err. \n", __func__);
		goto script_parser_fetch_err;
	}
	pr_info("%s: revert_y_flag = %d. \n", __func__, revert_y_flag);

	if(SCRIPT_PARSER_OK != script_parser_fetch("ctp_para", "ctp_exchange_x_y_flag", &exchange_x_y_flag, 1)){
		pr_err("ft5x_ts: script_parser_fetch err. \n");
		goto script_parser_fetch_err;
	}
	pr_info("%s: exchange_x_y_flag = %d. \n", __func__, exchange_x_y_flag);

	return 0;

script_parser_fetch_err:
	pr_notice("=========script_parser_fetch_err============\n");
	return ret;
}

/**
 * ctp_reset - function
 *
 */
static void ctp_reset(void)
{
	printk("%s. \n", __func__);
	if(gpio_reset_enable){
		if(EGPIO_SUCCESS != gpio_write_one_pin_value(gpio_reset_hdle, 0, "ctp_reset")){
			printk("%s: err when operate gpio. \n", __func__);
		}
		mdelay(TS_RESET_LOW_PERIOD);
		if(EGPIO_SUCCESS != gpio_write_one_pin_value(gpio_reset_hdle, 1, "ctp_reset")){
			printk("%s: err when operate gpio. \n", __func__);
		}
		mdelay(TS_INITIAL_HIGH_PERIOD);
	}
}

/**
 * ctp_wakeup - function
 *
 */
static void ctp_wakeup(void)
{
	printk("%s. \n", __func__);
	if(1 == gpio_wakeup_enable){
		if(EGPIO_SUCCESS != gpio_write_one_pin_value(gpio_wakeup_hdle, 1, "ctp_wakeup")){
			printk("%s: err when operate gpio. \n", __func__);
		}
		mdelay(TS_WAKEUP_LOW_PERIOD);
		if(EGPIO_SUCCESS != gpio_write_one_pin_value(gpio_wakeup_hdle, 0, "ctp_wakeup")){
			printk("%s: err when operate gpio. \n", __func__);
		}
		mdelay(TS_WAKEUP_HIGH_PERIOD);

	}
	return;
}
/**
 * ctp_detect - Device detection callback for automatic device creation
 * return value:
 *                    = 0; success;
 *                    < 0; err
 */
int ctp_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;

	if(twi_id == adapter->nr)
	{
		pr_info("%s: Detected chip %s at adapter %d, address 0x%02x\n",
			 __func__, CTP_NAME, i2c_adapter_id(adapter), client->addr);

		strlcpy(info->type, CTP_NAME, I2C_NAME_SIZE);
		return 0;
	}else{
		return -ENODEV;
	}
}
////////////////////////////////////////////////////////////////

static struct ctp_platform_ops ctp_ops = {
	.get_pendown_state = ctp_get_pendown_state,
	.clear_penirq	   = ctp_clear_penirq,
	.set_irq_mode      = ctp_set_irq_mode,
	.set_gpio_mode     = ctp_set_gpio_mode,
	.judge_int_occur   = ctp_judge_int_occur,
	.init_platform_resource = ctp_init_platform_resource,
	.free_platform_resource = ctp_free_platform_resource,
	.fetch_sysconfig_para = ctp_fetch_sysconfig_para,
	.ts_reset =          ctp_reset,
	.ts_wakeup =         ctp_wakeup,
	.ts_detect = ctp_detect,
};


//停用设备
#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_suspend(struct early_suspend *h)
{
	int ret;
	struct goodix_ts_data *ts = container_of(h, struct goodix_ts_data, early_suspend);
    struct i2c_client * client = ts->client;
    #ifdef PRINT_SUSPEND_INFO
        printk("enter earlysuspend: goodix_ts_suspend. \n");
    #endif

    //disable_irq(ts->gpio_irq);
	ret = cancel_work_sync(&ts->work);

	if (ts->power) {
		ret = ts->power(ts,0);
		if (ret < 0)
			dev_warn(&client->dev, "%s power off failed\n", f3x_ts_name);
	}
	return ;
}

//重新唤醒
static void goodix_ts_resume(struct early_suspend *h)
{
	int ret;
	struct goodix_ts_data *ts = container_of(h, struct goodix_ts_data, early_suspend);
    struct i2c_client * client = ts->client;

#ifdef PRINT_SUSPEND_INFO
        printk("enter laterresume: goodix_ts_resume. \n");
#endif

	if (ts->power) {
		ret = ts->power(ts, 1);
		if (ret < 0)
			dev_warn(&client->dev, "%s power on failed\n", f3x_ts_name);
	}

    //enable_irq(ts->gpio_irq);
	return ;
}
#else
#ifdef CONFIG_PM
//停用设备
static int goodix_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

#ifdef PRINT_SUSPEND_INFO
        printk("enter: goodix_ts_suspend. \n");
#endif
        //disable_irq(ts->gpio_irq);
	ret = cancel_work_sync(&ts->work);

	if (ts->power) {
		ret = ts->power(ts,0);
		if (ret < 0)
			dev_warn(&client->dev, "%s power off failed\n", f3x_ts_name);
	}
	return 0;
}

//重新唤醒
static int goodix_ts_resume(struct i2c_client *client)
{
	int ret;
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

#ifdef PRINT_SUSPEND_INFO
        printk("enter: goodix_ts_resume. \n");
#endif

	if (ts->power) {
		ret = ts->power(ts, 1);
		if (ret < 0)
			dev_warn(&client->dev, "%s power on failed\n", f3x_ts_name);
	}

        //enable_irq(ts->gpio_irq);
	return 0;
}
#endif

#endif


/*used by GT80X-IAP module */
struct i2c_client * i2c_connect_client = NULL;
EXPORT_SYMBOL(i2c_connect_client);
/**********************************************************************
本程序中I2C通信方式为：
	7bit从机地址｜读写位 + buf（数据地址+读写数据）
	 --------------------------------------------------------------------
	｜  从机地址   ｜ buf[0](数据地址) | buf[1]~buf[MAX-1](写入或读取到的数据)  |
	 --------------------------------------------------------------------
	移植前请根据自身主控格式修改！！
***********************************************************************/

//Function as i2c_master_receive, and return 2 if operation is successful.
static int i2c_read_bytes(struct i2c_client *client, uint8_t *buf, uint16_t len)
{
	struct i2c_msg msgs[2];
	int ret=-1;
	//发送写地址
	msgs[0].flags = !I2C_M_RD;
	msgs[0].addr = client->addr;
	msgs[0].len = 1;		//data address
	msgs[0].buf = buf;
	//接收数据
	msgs[1].flags = I2C_M_RD;//读消息
	msgs[1].addr = client->addr;
	msgs[1].len = len-1;
	msgs[1].buf = buf+1;

	ret=i2c_transfer(client->adapter, msgs, 2);
	return ret;
}

//Function as i2c_master_send, and return 1 if operation is successful.
static int i2c_write_bytes(struct i2c_client *client, uint8_t *data, uint16_t len)
{
	struct i2c_msg msg;
	int ret=-1;

	msg.flags = !I2C_M_RD;//写消息
	msg.addr = client->addr;
	msg.len = len;
	msg.buf = data;

	ret=i2c_transfer(client->adapter, &msg,1);
	return ret;
}

/*******************************************************
功能：
	GT80X初始化函数，用于发送配置信息
参数：
	ts:	struct goodix_ts_data
return：
	执行结果码，0表示正常执行
*******************************************************/
static bool goodix_init_panel(struct goodix_ts_data *ts)
{
	int ret = -1;
	int i =0;
	int count;
	uint8_t config_info[54];
	/*
        uint8_t config_info[54] = { 0x30,0x19,0x05,0x06,0x28,0x02,0x14,0x20,0x10,0x3C,0xB0,0x14,0x00,
                                    0x1E,0x00,0x01,0x23,0x45,0x67,0x89,0xAB,0xCD,0xE1,0x00,0x00,
                                    0x00,0x00,0x4D,0xCF,0x20,0x01,0x01,0x83,0x50,0x3C,0x1E,0xB4,
                                    0x00,0x0A,0x50,0x78,0x1E,0x00,0x50,0x32,0x71,0x00,0x00,0x00,
                                    0x00,0x00,0x00,0x00,0x01};
                                    */
    if((800 == screen_max_x) && (480 == screen_max_y)){
        uint8_t data_info[54] = {0x30,0x19,0x05,0x04,0x28,0x02,0x14,0x20,0x10,0x50,0xB2,0x14,0x00,0x1E,0x00,0x01,
                                      0x23,0x45,0x67,0x89,0xAB,0xCD,0xE1,0x00,0x00,0x2D,0x29,0x45,0xCF,0x20,0x01,0x01,
                                      0x83,0x50,0x3C,0x1E,0xB4,0x00,0x2B,0x27,0x01,0xB4,0x00,0x64,0x32,0x71,0x00,0x00,
                                      0x00,0x00,0x00,0x00,0x00,0x01};
        for(i =0; i < 54; i++){
            config_info[i] = data_info[i];
        }

    }else if((1280 == screen_max_x) && (768 == screen_max_y)){
        uint8_t data_info[54] = {0x30,0x19,0x05,0x06,0x28,0x02,0x14,0x20,0x10,0x50,0xB2,0x14,0x00,0x1E,0x00,0x01,
        	                            0x23,0x45,0x67,0x89,0xAB,0xCD,0xE1,0x00,0x00,0x2D,0x29,0x45,0xCF,0x20,0x01,
        	                            0x01,0x83,0x50,0x3C,0x1E,0xB4,0x00,0x2B,0x27,0x01,0xB4,0x00,0x64,0x32,0x71,
        	                            0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01} ;
        for(i =0; i < 54; i++){
            config_info[i] = data_info[i];
        }
    }

	for(count = 5; count > 0; count--)
	{
		ret = i2c_write_bytes(ts->client,config_info,54);;
		if(ret == 1)		//Initiall success
			break;
		else
			msleep(10);
	}


	return ret==1 ? true : false;
}

/*读取GT80X的版本号并打印*/
static int  goodix_read_version(struct goodix_ts_data *ts)
{
#define GT80X_VERSION_LENGTH	40
	int ret;
	uint8_t version[2] = {0x69,0xff};			//command of reading Guitar's version
	uint8_t version_data[GT80X_VERSION_LENGTH+1]={0x6A};	//store touchscreen version infomation
	memset(version_data+1, 0, GT80X_VERSION_LENGTH);
	ret = i2c_write_bytes(ts->client,version,2);
	if (ret != 1)
		return ret;
	msleep(50);
	ret = i2c_read_bytes(ts->client,version_data, GT80X_VERSION_LENGTH);
	if (ret != 2)
		strncpy(version_data+1, "NULL", 4);
	dev_info(&ts->client->dev,"GT80X Version: %s\n", version_data+1);
	version[1] = 0x00;				//cancel the command
	i2c_write_bytes(ts->client, version, 2);
	return 0;
}


/*******************************************************
功能：
	触摸屏工作函数
	由中断触发，接受1组坐标数据，校验后再分析输出
参数：
	ts:	client私有数据结构体
return：
	执行结果码，0表示正常执行
********************************************************/
static void goodix_ts_work_func(struct work_struct *work)
{
	static struct point_node pointer[MAX_FINGER_NUM];
	static uint8_t finger_last = 0;	//last time fingers' state

	struct point_node * p = NULL;
	uint8_t read_position = 0;
	uint8_t point_data[READ_BYTES_NUM]={ 0 };
	uint8_t finger, finger_current;				//record which finger is changed
	uint8_t check_sum = 0;
	unsigned int x, y;
	int count = 0;
	int ret = -1;

	struct goodix_ts_data *ts = container_of(work, struct goodix_ts_data, work);

//	if (ts->use_shutdown && gpio_get_value(ts->gpio_shutdown))
//		goto NO_ACTION;					//The data is invalid.

	ret=i2c_read_bytes(ts->client, point_data, sizeof(point_data));
	if(ret <= 0)
	{
		dev_dbg(&(ts->client->dev),"I2C transfer error. ERROR Number:%d\n ", ret);
		ts->retry++;
		if(ts->retry >= 100)
		{	//It's not normal for too much i2c-error.
			dev_err(&(ts->client->dev),"Reset the chip for i2c error.\n ");
			ts->retry = 0;
			if(ts->power)
			{
				ts->power(ts, 0);
				ts->power(ts, 1);
			}
			else
			{
				goodix_init_panel(ts);
				msleep(200);
			}
		}
		goto XFER_ERROR;
	}

	switch(point_data[1]& 0x1f)
	{
		case 0:
			break;
		case 1:
			for(count=1; count<8; count++)
				check_sum += (int)point_data[count];
			read_position = 8;
			break;
		case 2:
		case 3:
			for(count=1; count<13;count++)
				check_sum += (int)point_data[count];
			read_position = 13;
			break;
		default:		//(point_data[1]& 0x1f) > 3
			for(count=1; count<34;count++)
				check_sum += (int)point_data[count];
			read_position = 34;
	}

	if(check_sum != point_data[read_position])
		goto XFER_ERROR;

	//The bits indicate which fingers pressed down
	finger_current = point_data[1] & 0x1f;
	finger = finger_current^finger_last;
	if(finger == 0 && finger_current == 0)
		goto NO_ACTION;					//no action
	else if(finger == 0)
		goto BIT_NO_CHANGE;				//the same as last time

	//check which point(s) DOWN or UP
	for(count = 0; count < MAX_FINGER_NUM;  count++)
	{
		p = &pointer[count];
		p->id = count;
		if((finger_current & FLAG_MASK) != 0)
			p->state = FLAG_DOWN;
		else
		{
			if((finger & FLAG_MASK) != 0)		//send press release.
				p->state = FLAG_UP;
			else
				p->state = FLAG_INVALID;
		}

		finger>>=1;
		finger_current>>=1;
	}
	finger_last = point_data[1] & 0x1f;	//restore last presse state.
BIT_NO_CHANGE:
	for(count = 0; count < MAX_FINGER_NUM; count++)
	{
		p = &pointer[count];
		if(p->state == FLAG_INVALID)
			continue;

		if(p->state == FLAG_UP)
		{
			x = y = 0;
			p->pressure = 0;
			continue;
		}

		if(p->id < 3)
			read_position = p->id *5 + 3;
		else
			read_position = 29;

		if(p->id != 3)
		{
			x = (unsigned int) (point_data[read_position]<<8) + (unsigned int)( point_data[read_position+1]);
			y = (unsigned int)(point_data[read_position+2]<<8) + (unsigned int) (point_data[read_position+3]);
			p->pressure = point_data[read_position+4];
		}
	#if MAX_FINGER_NUM > 3
		else
		{
			x = (unsigned int) (point_data[18]<<8) + (unsigned int)( point_data[25]);
			y = (unsigned int)(point_data[26]<<8) + (unsigned int) (point_data[27]);
			p->pressure = point_data[28];
		}
	#endif

		// 将触摸屏的坐标映射到LCD坐标上. 触摸屏短边为X轴，LCD坐标一般长边为X轴，可能需要调整原点位置
		x = (TOUCH_MAX_WIDTH - x)*SCREEN_MAX_WIDTH/TOUCH_MAX_WIDTH;//y
		y =  y*SCREEN_MAX_HEIGHT/TOUCH_MAX_HEIGHT ;					//x
		//print_point_info("RAW X = %d,Y = %d\n",800-y,x);
		if(1 == revert_x_flag){
			x = SCREEN_MAX_WIDTH - x;
			}
		if(1 == revert_y_flag){
			y = SCREEN_MAX_HEIGHT - y;
			}
		swap(x, y);
		p->x = x;
		p->y = y;
	}
#ifndef GOODIX_MULTI_TOUCH
		if(pointer[0].state == FLAG_DOWN)
		{
			input_report_abs(ts->input_dev, ABS_X, pointer[0].x);
			input_report_abs(ts->input_dev, ABS_Y, pointer[0].y);
		}
		input_report_abs(ts->input_dev, ABS_PRESSURE, pointer[0].pressure);
		input_report_key(ts->input_dev, BTN_TOUCH, pointer[0].state == FLAG_INVALID?FLAG_UP:pointer[0].state);
#else
	/* ABS_MT_TOUCH_MAJOR is used as ABS_MT_PRESSURE in android. */
	for(count = 0; count < MAX_FINGER_NUM; count++)
	{
		p = &pointer[count];

		if(p->state == FLAG_INVALID)
			continue;

		if(p->state == FLAG_DOWN)
		{
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, p->x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, p->y);
			//dev_dbg(&(ts->client->dev), "Id:%d, x:%d, y:%d\n", p->id, p->x, p->y);

			print_point_info("Id:%d, x:%d, y:%d\n", p->id, p->x, p->y);
		}
		input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, p->id);
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, p->pressure);
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, p->pressure);

		input_mt_sync(ts->input_dev);
	}
#endif
	input_sync(ts->input_dev);
XFER_ERROR:
NO_ACTION:
         ret = 0;
	//enable_irq(ts->gpio_irq);

}



/*******************************************************
功能：
	中断响应函数
	由中断触发，调度触摸屏处理函数运行
********************************************************/
static irqreturn_t goodix_ts_irq_handler(int irq, void *dev_id)
{
	struct goodix_ts_data *ts = dev_id;
	int reg_val;
	print_int_info("==========------TS Interrupt-----============\n");

	//clear the IRQ_EINT21 interrupt pending
	reg_val = readl(gpio_addr + PIO_INT_STAT_OFFSET);

	if (reg_val&(1<<(CTP_IRQ_NO))) {
		print_int_info("==CTP_IRQ_NO=\n");
		writel(reg_val&(1<<(CTP_IRQ_NO)),gpio_addr + PIO_INT_STAT_OFFSET);
		queue_work(goodix_wq, &ts->work);
	} else {
	    print_int_info("Other Interrupt\n");
	    return IRQ_NONE;
	}
	return IRQ_HANDLED;
}

/*******************************************************
功能：
	GT80X的电源管理
参数：
	on:设置GT80X运行模式，0为进入Sleep模式
return：
	是否设置成功，小于0表示设置失败
********************************************************/
static int goodix_ts_power(struct goodix_ts_data * ts, int on)
{
	int ret = 0;

	switch(on)
	{
		case 0:
			gpio_write_one_pin_value(gpio_wakeup_hdle, 1, "ctp_wakeup");
			ret = 1;
			break;
		case 1:
			gpio_write_one_pin_value(gpio_wakeup_hdle, 0, "ctp_wakeup");
			ret = 1;
			break;
	}
	dev_dbg(&ts->client->dev, "Set Guitar's Shutdown %s. Ret:%d.\n", on?"LOW":"HIGH", ret);
	return ret;
}

//Test i2c to check device. Before it SHUTDOWN port Must be low state 30ms or more.
static bool goodix_i2c_test(struct i2c_client * client)
{
	int ret, retry;
	uint8_t test_data[1] = { 0 };	//only write a data address.

	for(retry=0; retry < 5; retry++)
	{
		ret =i2c_write_bytes(client, test_data, 1);	//Test i2c.
		if (ret == 1)
			break;
		msleep(5);
	}

	return ret==1 ? true : false;
}

/*******************************************************
功能：
	触摸屏探测函数
	在注册驱动时调用（要求存在对应的client）；
	用于IO,中断等资源申请；设备注册；触摸屏初始化等工作
参数：
	client：待驱动的设备结构体
	id：设备ID
return：
	执行结果码，0表示正常执行
********************************************************/
static int goodix_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct goodix_ts_data *ts;
	int ret = 0;
	int err;

	//struct goodix_i2c_platform_data *pdata;
	//dev_dbg(&client->dev,"Install touchscreen driver for guitar.\n");
	pr_info("===============================GT801 Probe===========================\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)){
		dev_err(&client->dev, "System need I2C function.\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}

	ts->gpio_irq = SW_INT_IRQNO_PIO;
	i2c_connect_client = client;				//used by Guitar Updating.

#ifdef TEST_I2C_TRANSFER
	//TODO: used to set speed of i2c transfer. Should be change as your paltform.
	pr_info("Begin goodix i2c test\n");
	ret = goodix_i2c_test(client);
	if(!ret){
		pr_info("Warnning: I2C connection might be something wrong!\n");
		goto err_i2c_failed;
	}
	pr_info("===== goodix i2c test ok=======\n");
#endif

	INIT_WORK(&ts->work, goodix_ts_work_func);
	ts->client = client;
	i2c_set_clientdata(client, ts);

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL)
	{
		ret = -ENOMEM;
		dev_dbg(&client->dev,"Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}

	ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) ;

#ifndef GOODIX_MULTI_TOUCH
	ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	ts->input_dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE);
	input_set_abs_params(ts->input_dev, ABS_X, 0, SCREEN_MAX_HEIGHT, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, 0, SCREEN_MAX_WIDTH, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);

#else
	ts->input_dev->absbit[0] = BIT_MASK(ABS_MT_TRACKING_ID) |
		BIT_MASK(ABS_MT_TOUCH_MAJOR)| BIT_MASK(ABS_MT_WIDTH_MAJOR) |
  		BIT_MASK(ABS_MT_POSITION_X) | BIT_MASK(ABS_MT_POSITION_Y); 	// for android
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, SCREEN_MAX_HEIGHT, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, SCREEN_MAX_WIDTH, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, MAX_FINGER_NUM, 0, 0);
#endif

#ifdef FOR_TSLIB_TEST
	set_bit(BTN_TOUCH, ts->input_dev->keybit);
#endif

	sprintf(ts->phys, "input/goodix-ts");
	ts->input_dev->name = f3x_ts_name;
	ts->input_dev->phys = ts->phys;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0xDEAD;
	ts->input_dev->id.product = 0xBEEF;
	ts->input_dev->id.version = 0x1105;

	ret = input_register_device(ts->input_dev);
	if (ret) {
		dev_err(&client->dev,"Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}

	goodix_wq = create_singlethread_workqueue("goodix_wq");
	if (!goodix_wq) {
		printk(KERN_ALERT "Creat %s workqueue failed.\n", f3x_ts_name);
		return -ENOMEM;

	}
	flush_workqueue(goodix_wq);
	ts->power = goodix_ts_power;
	msleep(30);

	ret = goodix_init_panel(ts);
	if(!ret)
		goto err_init_godix_ts;

#ifdef CONFIG_HAS_EARLYSUSPEND
	printk("==register_early_suspend =\n");
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = goodix_ts_suspend;
	ts->early_suspend.resume	= goodix_ts_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	err = ctp_ops.set_irq_mode("ctp_para", "ctp_int_port", CTP_IRQ_MODE);
	if(0 != err){
		printk("%s:ctp_ops.set_irq_mode err. \n", __func__);
		goto exit_set_irq_mode;
	}

	err =  request_irq(SW_INT_IRQNO_PIO, goodix_ts_irq_handler, IRQF_TRIGGER_RISING | IRQF_SHARED, client->name, ts);
	if (err < 0) {
		pr_info( "goodix_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}

	pr_info("Read Goodix version\n");
	goodix_read_version(ts);
	//msleep(260);

	dev_dbg(&client->dev,"Start  %s in %s mode\n",
		ts->input_dev->name, ts->use_irq ? "Interrupt" : "Polling");

	pr_info("========Probe Ok================\n");
	return 0;

exit_set_irq_mode:
exit_irq_request_failed:
err_init_godix_ts:
err_input_register_device_failed:
	input_free_device(ts->input_dev);
err_input_dev_alloc_failed:
	i2c_set_clientdata(client, NULL);
err_i2c_failed:
err_alloc_data_failed:
err_check_functionality_failed:
	return ret;
}


/*******************************************************
功能：
	驱动资源释放
参数：
	client：设备结构体
return：
	执行结果码，0表示正常执行
********************************************************/
static int goodix_ts_remove(struct i2c_client *client)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(client);
	dev_notice(&client->dev,"The driver is removing...\n");

	free_irq(SW_INT_IRQNO_PIO, ts);
	#ifdef CONFIG_HAS_EARLYSUSPEND
		unregister_early_suspend(&ts->early_suspend);
	#endif
	flush_workqueue(goodix_wq);
	if (goodix_wq)
		destroy_workqueue(goodix_wq);
	input_unregister_device(ts->input_dev);
	input_free_device(ts->input_dev);
	i2c_set_clientdata(ts->client, NULL);
	kfree(ts);
	return 0;
}

//可用于该驱动的 设备名—设备ID 列表
//only one client
static const struct i2c_device_id goodix_ts_id[] = {
	{ GOODIX_I2C_NAME, 0 },
	{ }
};

//设备驱动结构体
static struct i2c_driver goodix_ts_driver = {
	.class = I2C_CLASS_HWMON,
	.probe		= goodix_ts_probe,
	.remove		= goodix_ts_remove,
#ifdef CONFIG_HAS_EARLYSUSPEND
#else
#ifdef CONFIG_PM
	.suspend	= goodix_ts_suspend,
	.resume		= goodix_ts_resume,
#endif
#endif
	.id_table	= goodix_ts_id,
	.driver = {
		.name	= GOODIX_I2C_NAME,
		.owner = THIS_MODULE,
	},
	.address_list	= u_i2c_addr.normal_i2c,
};


//驱动加载函数
static int __devinit goodix_ts_init(void)
{
	int ret = -1;
	int err = -1;

	printk("===========================%s=====================\n", __func__);

	if (ctp_ops.fetch_sysconfig_para)
	{
		if(ctp_ops.fetch_sysconfig_para()){
			printk("%s: err.\n", __func__);
			return -1;
		}
	}
	printk("%s: after fetch_sysconfig_para:  normal_i2c: 0x%hx. normal_i2c[1]: 0x%hx \n", \
	__func__, u_i2c_addr.normal_i2c[0], u_i2c_addr.normal_i2c[1]);

	err = ctp_ops.init_platform_resource();
	if(0 != err){
		printk("%s:ctp_ops.init_platform_resource err. \n", __func__);
	}

	//reset
	ctp_ops.ts_reset();
	//wakeup
	ctp_ops.ts_wakeup();

	goodix_ts_driver.detect = ctp_ops.ts_detect;

	ret = i2c_add_driver(&goodix_ts_driver);

	return ret;
}

//驱动卸载函数
static void __exit goodix_ts_exit(void)
{
	i2c_del_driver(&goodix_ts_driver);
	ctp_ops.free_platform_resource();

	return;
}

late_initcall(goodix_ts_init);
module_exit(goodix_ts_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Driver");
MODULE_LICENSE("GPL v2");
