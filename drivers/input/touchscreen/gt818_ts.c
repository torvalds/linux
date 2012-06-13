#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <asm/io.h>
#include <linux/platform_device.h>
//#include <mach/gpio.h>
#include <linux/irq.h>
#include <linux/irq.h>
#include <asm/irq.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/completion.h>
#include <asm/uaccess.h>
#include <mach/irqs.h>
#include <mach/system.h>
#include <mach/hardware.h>
#include <plat/sys_config.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/errno.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
    #include <linux/pm.h>
    #include <linux/earlysuspend.h>
#endif

#include "gt818_ts.h"
#include "gt818_update.h"
#include "ctp_platform_ops.h"

#define FOR_TSLIB_TEST
//#define PRINT_INT_INFO
//#define PRINT_POINT_INFO
#define PRINT_SUSPEND_INFO
#define TEST_I2C_TRANSFER
//#define DEBUG

static int reg_val;
const char *f3x_ts_name="gt818_ts";
static struct workqueue_struct *goodix_wq;


struct i2c_client * i2c_connect_client = NULL;
static struct proc_dir_entry *goodix_proc_entry;
static short  goodix_read_version(struct goodix_ts_data *ts);


#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h);
static void goodix_ts_late_resume(struct early_suspend *h);
#endif

int  gt818_downloader( struct goodix_ts_data *ts, unsigned char * data,unsigned char * path );
unsigned int oldcrc32 = 0xFFFFFFFF;
unsigned int crc32_table[256];
unsigned int ulPolynomial = 0x04c11db7;
unsigned int raw_data_ready = RAW_DATA_NON_ACTIVE;

#ifdef DEBUG
int sum = 0;
int access_count = 0;
int int_count = 0;
#endif

///////////////////////////////////////////////
//specific tp related macro: need be configured for specific tp
#define CTP_IRQ_NO			(gpio_int_info[0].port_num)

#define CTP_IRQ_MODE			(POSITIVE_EDGE)
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
static user_gpio_set_t  gpio_int_info[1];

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
	//writel(reg_val&(1<<(CTP_IRQ_NO)),gpio_addr + PIO_INT_STAT_OFFSET);
	if((reg_val = (reg_val&(1<<(CTP_IRQ_NO))))){
		print_int_info("%s: %d. ==CTP_IRQ_NO=\n", __func__, __LINE__);
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
	memset(name, 0, I2C_NAME_SIZE);

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

/*Function as i2c_master_send */
static int i2c_read_bytes(struct i2c_client *client, uint8_t *buf, int len)
{
	struct i2c_msg msgs[2];
	int ret=-1;
	//
	msgs[0].flags=!I2C_M_RD; //
	msgs[0].addr=client->addr;
	msgs[0].len=2;
	msgs[0].buf=&buf[0];
	//
	msgs[1].flags=I2C_M_RD;//
	msgs[1].addr=client->addr;
	msgs[1].len=len-2;
	msgs[1].buf=&buf[2];

	ret=i2c_transfer(client->adapter,msgs, 2);
	return ret;
}

/*******************************************************

*******************************************************/
/*Function as i2c_master_send */
static int i2c_write_bytes(struct i2c_client *client,uint8_t *data,int len)
{
	struct i2c_msg msg;
	int ret=-1;
	//
	msg.flags=!I2C_M_RD;//
	msg.addr=client->addr;
	msg.len=len;
	msg.buf=data;

	ret=i2c_transfer(client->adapter,&msg, 1);
	return ret;
}

/*******************************************************

*******************************************************/
static int i2c_pre_cmd(struct goodix_ts_data *ts)
{
	int ret;
	uint8_t pre_cmd_data[2]={0};
	pre_cmd_data[0]=0x0f;
	pre_cmd_data[1]=0xff;
	ret=i2c_write_bytes(ts->client,pre_cmd_data,2);
//	msleep(2);
	return ret;
}

/*******************************************************

*******************************************************/
static int i2c_end_cmd(struct goodix_ts_data *ts)
{
	int ret;
	uint8_t end_cmd_data[2]={0};
	end_cmd_data[0]=0x80;
	end_cmd_data[1]=0x00;
	ret=i2c_write_bytes(ts->client,end_cmd_data,2);
//	msleep(2);
	return ret;
}

/*

*/
static short get_chip_version( unsigned int sw_ver )
{
    //pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
    if ( (sw_ver&0xff) < TPD_CHIP_VERSION_C_FIRMWARE_BASE )
	return TPD_GT818_VERSION_B;
   else if ( (sw_ver&0xff) < TPD_CHIP_VERSION_D1_FIRMWARE_BASE )
   	return TPD_GT818_VERSION_C;
   else if((sw_ver&0xff) < TPD_CHIP_VERSION_E_FIRMWARE_BASE)
   	return TPD_GT818_VERSION_D1;
   else if((sw_ver&0xff) < TPD_CHIP_VERSION_D2_FIRMWARE_BASE)
   	return TPD_GT818_VERSION_E;
   else
   	return TPD_GT818_VERSION_D2;

}
/*******************************************************
notice: init panel need to be complete within 200ms.
	so, i2c transfer clk more faster more better
	     and do not add too much print info when debug.
*******************************************************/
static int goodix_init_panel(struct goodix_ts_data *ts)
{
	short ret=-1;
    //int ic_size = 0;

       uint8_t config_info_c[] = {		//Touch key devlop board
	0x06,0xA2,
	0x00,0x02,0x04,0x06,0x08,0x0A,0x0C,0x0E,
	0x10,0x12,0xE0,0x00,0xD0,0x00,0xC0,0x00,
	0xB0,0x00,0xA0,0x00,0x90,0x00,0x80,0x00,
	0x70,0x00,0x00,0x00,0x10,0x00,0x20,0x00,
	0x30,0x00,0x40,0x00,0x50,0x00,0x60,0x00,
	0x00,0x00,0x01,0x13,0x80,0x88,0x90,0x14,
	0x15,0x40,0x0F,0x0F,0x0A,0x50,0x3C,0x0C,
	0x00,0x00,MAX_FINGER_NUM,(TOUCH_MAX_WIDTH&0xff),(TOUCH_MAX_WIDTH>>8),(TOUCH_MAX_HEIGHT&0xff),(TOUCH_MAX_HEIGHT>>8),0x00,
	0x00,0x46,0x5A,0x00,0x00,0x00,0x00,0x03,
	0x19,0x05,0x00,0x00,0x00,0x00,0x00,0x00,
	0x20,0x10,0x00,0x04,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x38,
	0x00,0x3C,0x28,0x00,0x00,0x00,0x00,0x00,
	0x00,0x01
	};

#if 0
	uint8_t config_info_d[] = {		 //Touch key devlop board
		0x06,0xA2,
		0x00,0x02,0x04,0x06,0x08,0x0A,0x0C,0x0E,
		0x10,0x12,0x02,0x22,0x12,0x22,0x22,0x22,
		0x32,0x22,0x42,0x22,0x52,0x22,0x62,0x22,
		0x72,0x22,0x82,0x22,0x92,0x22,0xA2,0x22,
		0xB2,0x22,0xC2,0x22,0xD2,0x22,0xE2,0x22,
		0xF2,0x22,0x0B,0x13,0x68,0x68,0x68,0x19,
		0x19,0x19,0x0F,0x0F,0x0A,0x35,0x25,0x49,
		0x03,0x00,0x05,0xE0,0x01,0x20,0x03,0x00,
		0x00,0x32,0x2C,0x34,0x2E,0x00,0x00,0x05,
		0x14,0x05,0x07,0x00,0x00,0x00,0x00,0x00,
		0x14,0x10,0xEC,0x01,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x01
	};
#endif

	uint8_t config_info_d[] = {		 //Touch key devlop board
		0x06,0xA2,
		0x12,0x10,0x0E,0x0C,0x0A,0x08,0x06,0x04,
		0x02,0x00,0x01,0x11,0x11,0x11,0x21,0x11,
		0x31,0x11,0x41,0x11,0x51,0x11,0x61,0x11,
		0x71,0x11,0x81,0x11,0x91,0x11,0xA1,0x11,
		0xB1,0x11,0xC1,0x11,0xD1,0x11,0xE1,0x11,
		0xF1,0x11,0x0B,0x13,0x50,0x50,0x50,0x23,
		0x23,0x23,0x0F,0x0F,0x0A,0x40,0x30,0x4D,
		0x03,0x00,0x05,0xE0,0x01,0x20,0x03,0x00,
		0x00,0x32,0x2C,0x34,0x2E,0x00,0x00,0x05,
		0x14,0x05,0x07,0x00,0x00,0x00,0x00,0x00,
		0x14,0x10,0xEC,0x01,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
		0x00,0x01
	};

#if 0
       uint8_t config_info_d[] = {		//Touch key devlop board
	0x06,0xA2,
	0x00,0x02,0x04,0x06,0x08,0x0A,0x0C,0x0E,
	0x10,0x12,0x02,0x22,0x12,0x22,0x22,0x22,
	0x32,0x22,0x42,0x22,0x52,0x22,0x62,0x22,
	0x72,0x22,0x82,0x22,0x92,0x22,0xA2,0x22,
	0xB2,0x22,0xC2,0x22,0xD2,0x22,0xE2,0x22,
	0xF2,0x22,0x0B,0x13,0x68,0x68,0x68,0x19,
	0x19,0x19,0x0F,0x0F,0x0A,0x40,0x30,0x49,
	0x03,0x00,0x05,0xE0,0x01,0x20,0x03,0x00,
	0x00,0x32,0x2C,0x34,0x2E,0x00,0x00,0x05,
	0x14,0x05,0x07,0x00,0x00,0x00,0x00,0x00,
	0x14,0x10,0xEC,0x01,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x01

	};
#endif

#if 0
       uint8_t config_info_d[] = {		//Touch key devlop board
	0x06,0xA2,
	//20111220
	0x12,0x10,0x0E,0x0C,0x0A,0x08,0x06,0x04,
	0x02,0x00,0xF2,0x22,0xE2,0x22,0xD2,0x22,
	0xC2,0x22,0xB2,0x22,0xA2,0x22,0x92,0x22,
	0x82,0x22,0x72,0x22,0x62,0x22,0x52,0x22,
	0x42,0x22,0x32,0x22,0x22,0x22,0x12,0x22,
	0x02,0x22,0x0B,0x13,0x68,0x68,0x68,0x19,
	0x19,0x19,0x0F,0x0F,0x0A,0x40,0x30,0x49,
	0x03,0x00,0x05,0xE0,0x01,0x20,0x03,0x00,
	0x00,0x32,0x2C,0x34,0x2E,0x00,0x00,0x05,
	0x14,0x05,0x07,0x00,0x00,0x00,0x00,0x00,
	0x14,0x10,0xEC,0x01,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	0x00,0x01

	/*0x00,0x02,0x04,0x06,0x08,0x0A,0x0C,0x0E,
	0x10,0x12,0xE0,0x00,0xD0,0x00,0xC0,0x00,
	0xB0,0x00,0xA0,0x00,0x90,0x00,0x80,0x00,
	0x70,0x00,0x00,0x00,0x10,0x00,0x20,0x00,
	0x30,0x00,0x40,0x00,0x50,0x00,0x60,0x00,
	0x00,0x00,0x01,0x13,0x80,0x88,0x90,0x14,
	0x15,0x40,0x0F,0x0F,0x0A,0x50,0x3C,0x0C,
	0x00,0x00,MAX_FINGER_NUM,(TOUCH_MAX_WIDTH&0xff),(TOUCH_MAX_WIDTH>>8),(TOUCH_MAX_HEIGHT&0xff),(TOUCH_MAX_HEIGHT>>8),0x00,
	0x00,0x46,0x5A,0x00,0x00,0x00,0x00,0x03,
	0x19,0x05,0x00,0x00,0x00,0x00,0x00,0x00,
	0x20,0x10,0x00,0x04,0x00,0x00,0x00,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x10,0x38,
	0x00,0x3C,0x28,0x00,0x00,0x00,0x00,0x00,
	0x00,0x01
	*/
	};

#endif

//pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
       ret = goodix_read_version(ts);
       if (ret < 0)
		return ret;

       dev_info(&ts->client->dev," Guitar Version: %d\n",ts->version);

    //pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
       if((ts->version&0xff) < TPD_CHIP_VERSION_D1_FIRMWARE_BASE)
       {//pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
            dev_info(&ts->client->dev," Guitar Version: C\n");
            config_info_c[57] = (config_info_c[57]&0xf7)|(INT_TRIGGER<<3);
            ret=i2c_write_bytes(ts->client,config_info_c, (sizeof(config_info_c)/sizeof(config_info_c[0])));
        }
       else
       {
       ////pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
           if((ts->version&0xff) < TPD_CHIP_VERSION_E_FIRMWARE_BASE)
           		 dev_info(&ts->client->dev," Guitar Version: D1\n");
           else  if((ts->version&0xff) < TPD_CHIP_VERSION_D2_FIRMWARE_BASE)
           		 dev_info(&ts->client->dev," Guitar Version: E\n");
	    else
           		 dev_info(&ts->client->dev," Guitar Version: D2\n");

            config_info_d[57] = (config_info_d[57]&0xf7)|(INT_TRIGGER<<3);
            //pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
            msleep(1500);
            ret=i2c_write_bytes(ts->client,config_info_d, (sizeof(config_info_d)/sizeof(config_info_d[0])));
        }
        //pr_info("%s: %s, %d. \n", __FILE__, __func__, __LINE__);
	if (ret < 0)
		return ret;
	msleep(10);
	return 0;

}


/*******************************************************

*******************************************************/
static short  goodix_read_version(struct goodix_ts_data *ts)
{
	short ret;
	uint8_t version_data[5]={0};	//store touchscreen version infomation
	memset(version_data, 0, 5);
	version_data[0]=0x07;
	version_data[1]=0x17;
	msleep(5);
	//   pr_info("%s: %s, %d. \n", __FILE__, __func__, __LINE__);
	//ret=i2c_read_bytes(ts->client, version_data, 4);
      //msleep(2);
      ret=i2c_read_bytes(ts->client, version_data, 4);
	if (ret < 0)
		return ret;
	dev_info(&ts->client->dev," Guitar Version: %d.%d\n",version_data[3],version_data[2]);
       ts->version = (version_data[3]<<8)+version_data[2];
	return ret;

}


/*******************************************************

********************************************************/
static void goodix_ts_work_func(struct work_struct *work)
{
	uint8_t  touch_data[3] = {READ_TOUCH_ADDR_H,READ_TOUCH_ADDR_L,0};
	uint8_t  point_data[8*MAX_FINGER_NUM+2]={ 0 };
	static uint8_t   finger_last[MAX_FINGER_NUM+1]={0};		//
	uint8_t  finger_current[MAX_FINGER_NUM+1] = {0};		//
	uint8_t  coor_data[6*MAX_FINGER_NUM] = {0};			//

	uint8_t  finger = 0;
#ifdef HAVE_TOUCH_KEY
	uint8_t  key = 0;
	static uint8_t  last_key = 0;
	uint8_t  key_data[3] ={READ_KEY_ADDR_H,READ_KEY_ADDR_L,0};
#endif
	unsigned int  count = 0;
	unsigned int position = 0;
	int ret=-1;
	int tmp = 0;
	int temp = 0;
	uint16_t *coor_point = 0;
	static int x_corrdinate = 0;
	static int y_corrdinate = 0;

	struct goodix_ts_data *ts = container_of(work, struct goodix_ts_data, work);

#ifdef DEBUG
    printk("int count :%d\n", ++int_count);
    printk("ready?:%d\n", raw_data_ready);
#endif
    if (RAW_DATA_ACTIVE == raw_data_ready)
	{
	    raw_data_ready = RAW_DATA_READY;
#ifdef DEBUG
	    printk("ready!\n");
#endif
	   // if(ts->use_irq)
	//	    enable_irq(ts->client->irq);
	 //   return;
	}


	i2c_pre_cmd(ts);
#ifndef INT_PORT
COORDINATE_POLL:
#endif
	if( tmp > 9) {
		dev_info(&(ts->client->dev), "Because of transfer error,touchscreen stop working.\n");
		goto XFER_ERROR ;
	}

	ret=i2c_read_bytes(ts->client, touch_data,sizeof(touch_data)/sizeof(touch_data[0]));  //
	if(ret <= 0) {
		dev_err(&(ts->client->dev),"I2C transfer error. Number:%d ,%s--%d\n", ret,__func__,__LINE__);
		ts->bad_data = 1;
		tmp ++;
		ts->retry++;
	#ifndef INT_PORT
		goto COORDINATE_POLL;
	#else
		goto XFER_ERROR;
	#endif
	}

#ifdef HAVE_TOUCH_KEY
	ret=i2c_read_bytes(ts->client, key_data,sizeof(key_data)/sizeof(key_data[0]));  //
	if(ret <= 0) {
		dev_err(&(ts->client->dev),"I2C transfer error. Number:%d,%s--%d\n", ret,__func__,__LINE__);
		ts->bad_data = 1;
		tmp ++;
		ts->retry++;
	#ifndef INT_PORT
		goto COORDINATE_POLL;
	#else
		goto XFER_ERROR;
	#endif
	}
	key = key_data[2]&0x0f;
#endif

	if(ts->bad_data)
		//TODO:Is sending config once again (to reset the chip) useful?
		msleep(20);

	if(touch_data[2] == 0x0f)
       {
       //pr_info("%s: %s, %d. \n", __FILE__, __func__, __LINE__);
            goodix_init_panel(ts);
            goto DATA_NO_READY;
        }

	if((touch_data[2]&0x30)!=0x20)
	{
		goto DATA_NO_READY;
	}

	ts->bad_data = 0;

	finger = touch_data[2]&0x0f;
	if(finger != 0)
	{
		point_data[0] = READ_COOR_ADDR_H;		//read coor high address
		point_data[1] = READ_COOR_ADDR_L;		//read coor low address
		ret=i2c_read_bytes(ts->client, point_data, finger*8+2);
		if(ret <= 0)
		{
			dev_err(&(ts->client->dev),"I2C transfer error. Number:%d,%s--%d\n", ret,__func__,__LINE__);
			ts->bad_data = 1;
			tmp ++;
			ts->retry++;
		#ifndef INT_PORT
			goto COORDINATE_POLL;
		#else
			goto XFER_ERROR;
		#endif
		}

		for(position=2; position<((finger-1)*8+2+1); position += 8)
		{
			temp = point_data[position];
			if(temp<(MAX_FINGER_NUM+1))
			{
				finger_current[temp] = 1;
				for(count=0; count<6; count++)
				{
					coor_data[(temp-1)*6+count] = point_data[position+1+count];	//
				}
			}
			else
			{
				dev_err(&(ts->client->dev), "Track Id error:%d\n ", temp);
				ts->bad_data = 1;
				tmp ++;
				ts->retry++;
				#ifndef INT_PORT
					goto COORDINATE_POLL;
				#else
					goto XFER_ERROR;
				#endif
			}
		}
		//coor_point = (uint16_t *)coor_data;

	}

	else
	{
		for(position=1;position < MAX_FINGER_NUM+1; position++)
		{
			finger_current[position] = 0;
		}
	}
	coor_point = (uint16_t *)coor_data;
	for(position=1;position < MAX_FINGER_NUM+1; position++)
	{
		if((finger_current[position] == 0)&&(finger_last[position] != 0))
			{
				input_report_abs(ts->input_dev, ABS_MT_POSITION_X, 0);
				input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, 0);
				input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
	//			input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
				input_mt_sync(ts->input_dev);
			}
		else if(finger_current[position])
			{
				x_corrdinate = *(coor_point+3*(position-1)+1);
				y_corrdinate = *(coor_point+3*(position-1));
				if(revert_x_flag){
					x_corrdinate = screen_max_x - x_corrdinate;
				}

				if(revert_y_flag){
					y_corrdinate = screen_max_y - y_corrdinate;
				}

				if(exchange_x_y_flag){
					swap(x_corrdinate, y_corrdinate);
				}
				input_report_abs(ts->input_dev, ABS_MT_POSITION_X, x_corrdinate);  //can change x-y!!!
				input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, y_corrdinate);

				input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR,1);
				//input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, finger_list.pointer[0].pressure);
				input_mt_sync(ts->input_dev);
			//	input_report_abs(ts->input_dev, ABS_X, finger_list.pointer[0].x);
			//	input_report_abs(ts->input_dev, ABS_Y, finger_list.pointer[0].y)
			//	input_report_abs(ts->input_dev, ABS_PRESSURE, finger_list.pointer[0].pressure);
			//	input_sync(ts->input_dev);
			//	printk("%d*",(*(coor_point+3*(position-1)))*SCREEN_MAX_HEIGHT/(TOUCH_MAX_HEIGHT));
			//	printk("%d*",(*(coor_point+3*(position-1)+1))*SCREEN_MAX_WIDTH/(TOUCH_MAX_WIDTH));
			//	printk("\n");
			}

	}
	input_sync(ts->input_dev);

	for(position=1;position<MAX_FINGER_NUM+1; position++)
	{
		finger_last[position] = finger_current[position];
	}

#ifdef HAVE_TOUCH_KEY
	if((last_key == 0)&&(key == 0))
		;
	else
	{
		for(count = 0; count < 4; count++)
		{
			input_report_key(ts->input_dev, touch_key_array[count], !!(key&(0x01<<count)));
		}
	}
	last_key = key;
#endif

DATA_NO_READY:
XFER_ERROR:
	i2c_end_cmd(ts);
	if(ts->use_irq){
//	    enable_irq(ts->client->irq);
        reg_val = readl(gpio_addr + PIO_INT_CTRL_OFFSET);
        reg_val |=(1<<CTP_IRQ_NO);
        writel(reg_val,gpio_addr + PIO_INT_CTRL_OFFSET);
	}
}

/*******************************************************

********************************************************/
static enum hrtimer_restart goodix_ts_timer_func(struct hrtimer *timer)
{
	struct goodix_ts_data *ts = container_of(timer, struct goodix_ts_data, timer);
	queue_work(goodix_wq, &ts->work);
	hrtimer_start(&ts->timer, ktime_set(0, (POLL_TIME+6)*1000000), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

/*******************************************************

********************************************************/
static irqreturn_t goodix_ts_irq_handler(int irq, void *dev_id)
{
	struct goodix_ts_data *ts = dev_id;

	//printk(KERN_INFO"-------------------ts_irq_handler------------------\n");
	//disable_irq_nosync(ts->client->irq);
	reg_val = readl(gpio_addr + PIO_INT_STAT_OFFSET);

	if(reg_val&(1<<(CTP_IRQ_NO)))
	{
		print_int_info("%s: %d. ==CTP_IRQ_NO=\n", __func__, __LINE__);
		//clear the CTP_IRQ_NO interrupt pending
		writel(reg_val&(1<<(CTP_IRQ_NO)),gpio_addr + PIO_INT_STAT_OFFSET);
		queue_work(goodix_wq, &ts->work);
	}
	else
	{
	    print_int_info("Other Interrupt\n");
	    return IRQ_NONE;
	}


	return IRQ_HANDLED;
}

/*******************************************************

********************************************************/
//#if defined(INT_PORT)
static int goodix_ts_power(struct goodix_ts_data * ts, int on)
{
	int ret = -1;

	unsigned char i2c_control_buf[3] = {0x06,0x92,0x01};		//suspend cmd
#if 0
//#ifdef INT_PORT
	if(ts != NULL && !ts->use_irq)
		return -2;
#endif
	switch(on)
	{
		case 0:
			i2c_pre_cmd(ts);               //must
			ret = i2c_write_bytes(ts->client, i2c_control_buf, 3);
//			printk(KERN_INFO"Send suspend cmd\n");
			if(ret > 0)						//failed
				ret = 0;
			i2c_end_cmd(ts);                     //must
			return ret;

		case 1:

			#ifdef INT_PORT	                     //suggest use INT PORT to wake up !!!

				//gpio_direction_output(INT_PORT, 0);
				gpio_set_one_pin_io_status(gpio_int_hdle, 1, "ctp_int_port");
				gpio_write_one_pin_value(gpio_int_hdle, 0, "ctp_int_port");
				msleep(1);
                          // gpio_direction_output(INT_PORT, 1);
				gpio_set_one_pin_io_status(gpio_int_hdle, 1, "ctp_int_port");
				gpio_write_one_pin_value(gpio_int_hdle, 1, "ctp_int_port");
				  msleep(10);
                          // gpio_direction_output(INT_PORT, 0);
				gpio_set_one_pin_io_status(gpio_int_hdle, 1, "ctp_int_port");
				gpio_write_one_pin_value(gpio_int_hdle, 0, "ctp_int_port");

				//gpio_free(INT_PORT);
				//s3c_gpio_setpull(INT_PORT, S3C_GPIO_PULL_NONE);
				gpio_set_one_pin_pull(gpio_int_hdle, 0, "ctp_int_port");


				if(ts->use_irq) {
				//	s3c_gpio_cfgpin(INT_PORT, INT_CFG);	//Set IO port as interrupt port
					ret = ctp_ops.set_irq_mode("ctp_para", "ctp_int_port", CTP_IRQ_MODE);
					if(0 != ret){
						printk("%s:ctp_ops.set_irq_mode err. \n", __func__);
						return ret;
					}
				}
				else
				//gpio_direction_input(INT_PORT);
				//Config CTP_IRQ_NO as input
	  			gpio_set_one_pin_io_status(gpio_int_hdle,0, "ctp_int_port");

			#else
				//gpio_direction_output(SHUTDOWN_PORT,0);
				gpio_set_one_pin_io_status(gpio_wakeup_hdle, 1, "ctp_wakeup");
				gpio_write_one_pin_value(gpio_wakeup_hdle, 0, "ctp_wakeup");
				msleep(1);
				//gpio_direction_input(SHUTDOWN_PORT);
				gpio_set_one_pin_io_status(gpio_wakeup_hdle, 0, "ctp_wakeup");
			#endif
				msleep(40);
				ret = 0;
				return ret;

		default:
			printk(KERN_DEBUG "%s: Cant't support this command.", f3x_ts_name);
			return -EINVAL;
	}

}


static int goodix_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	//TODO:
	int ret = 0;
	int err = 0;
	int retry=0;

	//unsigned short version_temp = 0;
	unsigned char update_path[1] = {0};
#if defined(NO_DEFAULT_ID) && defined(INT_PORT)
	uint8_t goodix_id[3] = {0,0xff,0};
#endif
	char test_data = 1;
	//char test_data2[4]={0x6,0xA2,5,10};
	struct goodix_ts_data *ts = NULL;

	struct goodix_i2c_rmi_platform_data *pdata;
	dev_dbg(&client->dev,"Install touch driver.\n");

	printk("======goodix_gt818 probe======\n");
	//config gpio:
	//pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
	gpio_wakeup_hdle = gpio_request_ex("ctp_para", "ctp_wakeup");
	if(!gpio_wakeup_hdle) {
		pr_warning("touch panel tp_wakeup request gpio fail!\n");
		goto exit_gpio_wakeup_request_failed;
	}

	//printk("======gt818_addr=0x%x=======\n",client->addr);

	//Check I2C function
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	{
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		ret = -ENODEV;
		goto err_check_functionality_failed;
	}

	ts = kzalloc(sizeof(*ts), GFP_KERNEL);
	if (ts == NULL) {
		ret = -ENOMEM;
		goto err_alloc_data_failed;
	}

	gpio_addr = ioremap(PIO_BASE_ADDRESS, PIO_RANGE_SIZE);
	if(!gpio_addr) {
	    err = -EIO;
	    goto exit_ioremap_failed;
	}

	ts->gpio_irq = INT_PORT;

	i2c_connect_client = client;	//used by Guitar_Update


#ifdef	INT_PORT
	//gpio_direction_input(INT_PORT);
	gpio_set_one_pin_io_status(gpio_int_hdle, 0, "ctp_int_port");
	//s3c_gpio_setpull(INT_PORT, S3C_GPIO_PULL_NONE);
	gpio_set_one_pin_pull(gpio_int_hdle, 0, "ctp_int_port");
#endif

#if defined(NO_DEFAULT_ID) && defined(INT_PORT)
	for(retry=0;retry < 3; retry++)
	{
		//gpio_direction_output(SHUTDOWN_PORT,0);
		gpio_set_one_pin_io_status(gpio_wakeup_hdle, 1, "ctp_wakeup")
		gpio_write_one_pin_value(gpio_wakeup_hdle, 0, "ctp_wakeup");
		msleep(1);
		//gpio_direction_input(SHUTDOWN_PORT);
		gpio_set_one_pin_io_status(gpio_wakeup_hdle, 0, "ctp_wakeup")
		msleep(20);

		ret =i2c_write_bytes(client, &test_data, 1);	//Test I2C connection.
		if (ret > 0)
			break;
	}
	if(ret <= 0)
	{
		//gpio_direction_output(INT_PORT,0);
		gpio_set_one_pin_io_status(gpio_int_hdle, 1, "ctp_int_port");
		gpio_write_one_pin_value(gpio_int_hdle, 0, "ctp_int_port");


		msleep(1);
		//gpio_direction_output(SHUTDOWN_PORT,0);
		gpio_set_one_pin_io_status(gpio_wakeup_hdle, 1, "ctp_wakeup");
		gpio_write_one_pin_value(gpio_wakeup_hdle, 0, "ctp_wakeup");
		msleep(20);
		//gpio_direction_input(SHUTDOWN_PORT);
		gpio_set_one_pin_io_status(gpio_wakeup_hdle, 0, "ctp_wakeup");
		for(retry=0;retry < 80; retry++)
		{
			ret =i2c_write_bytes(client, &test_data, 1);	//Test I2C connection.
			if (ret > 0)
			{
				msleep(10);
				ret =i2c_read_bytes(client, goodix_id, 3);	//Test I2C connection.
				if (ret > 0)
				{
					if(goodix_id[2] == 0x55)
						{
						//gpio_direction_output(INT_PORT,1);
						 gpio_set_one_pin_io_status(gpio_int_hdle, 1, "ctp_int_port");
						 gpio_write_one_pin_value(gpio_int_hdle, 1, "ctp_int_port");


						msleep(1);
						//gpio_free(INT_PORT);
						//s3c_gpio_setpull(INT_PORT, S3C_GPIO_PULL_NONE);
						gpio_set_one_pin_pull(gpio_int_hdle, 0, "ctp_int_port");

						msleep(10);
						break;
						}
				}
			}

		}
	}
#endif

	for(retry=0;retry < 3; retry++)
	{
		//gpio_direction_output(SHUTDOWN_PORT,0);
		gpio_set_one_pin_io_status(gpio_wakeup_hdle, 1, "ctp_wakeup");
		gpio_write_one_pin_value(gpio_wakeup_hdle, 0, "ctp_wakeup");
		msleep(50);
		//gpio_direction_input(SHUTDOWN_PORT);
		gpio_set_one_pin_io_status(gpio_wakeup_hdle, 0, "ctp_wakeup");
		msleep(200);
		//output
		//gpio_set_one_pin_io_status(gpio_wakeup_hdle, 1, "ctp_wakeup");
		//gpio_write_one_pin_value(gpio_wakeup_hdle, 1, "ctp_wakeup");
		//pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
		ret =i2c_write_bytes(client, &test_data, 1);	//Test I2C connection.
		if (ret > 0)
			break;
	}


	/******
	    pr_info("========write_msg=%d=======\n", i2c_write_bytes(client,test_data2,4));
	    test_data2[2]=0;
	    test_data2[3]=0;
	    printk("=====test_data2[2]=%d,test_data2[3]=%d====\n",test_data2[2],test_data2[3]);
	    printk("=====read_msg=%d,test_data2[2]=%d,test_data2[3]=%d====\n",i2c_read_bytes(client,test_data2,4),test_data2[2],test_data2[3]);
	***/

	if(ret <= 0)
	{
		dev_err(&client->dev, "Warnning: I2C communication might be ERROR!\n");
		goto err_i2c_failed;
	}

	ts->power = goodix_ts_power;
	INIT_WORK(&ts->work, goodix_ts_work_func);		//init work_struct
	ts->client = client;
	i2c_set_clientdata(client, ts);
	pdata = client->dev.platform_data;


	//gpio_set_one_pin_io_status(gpio_wakeup_hdle, 1, "ctp_wakeup");
        //gpio_write_one_pin_value(gpio_wakeup_hdle, 0, "ctp_wakeup");
#ifdef AUTO_UPDATE_GT818
  	  pr_info("%s: %s, %d. \n", __FILE__, __func__, __LINE__);
            i2c_pre_cmd(ts);
            goodix_read_version(ts);
            i2c_end_cmd(ts);
    //pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
            ret = gt818_downloader( ts, goodix_gt818_firmware, update_path);
            if(ret < 0)
            {
                dev_err(&client->dev, "Warnning: GT818 update might be ERROR!\n");
                //goto err_input_dev_alloc_failed;
            }
#endif

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		dev_dbg(&client->dev,"goodix_ts_probe: Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}

	ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) ;
	ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	ts->input_dev->absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_PRESSURE); 			// absolute coor (x,y)
#ifdef HAVE_TOUCH_KEY
	for(retry = 0; retry < MAX_KEY_NUM; retry++)
	{
		input_set_capability(ts->input_dev,EV_KEY,touch_key_array[retry]);
	}
#endif

	input_set_abs_params(ts->input_dev, ABS_X, 0, TOUCH_MAX_HEIGHT, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, 0, TOUCH_MAX_WIDTH, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, 255, 0, 0);

#ifdef GOODIX_MULTI_TOUCH
	input_set_abs_params(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, TOUCH_MAX_HEIGHT, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, TOUCH_MAX_WIDTH, 0, 0);
#endif

	sprintf(ts->phys, "input/ts");
	ts->input_dev->name = f3x_ts_name;
	ts->input_dev->phys = ts->phys;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0xDEAD;
	ts->input_dev->id.product = 0xBEEF;
	ts->input_dev->id.version = 10427;	//screen firmware version

	ret = input_register_device(ts->input_dev);
	if (ret) {
		dev_err(&client->dev,"Probe: Unable to register %s input device\n", ts->input_dev->name);
		goto err_input_register_device_failed;
	}
	ts->bad_data = 0;
//	finger_list.length = 0;
#ifdef INT_PORT
	client->irq=INT_PORT;		//If not defined in client
	if (client->irq)
	{
		#if INT_TRIGGER==1
			#define GT801_PLUS_IRQ_TYPE IRQ_TYPE_EDGE_RISING
		#elif INT_TRIGGER==0
			#define GT801_PLUS_IRQ_TYPE IRQ_TYPE_EDGE_FALLING
	//	#elif INT_TRIGGER==2
	//		#define GT801_PLUS_IRQ_TYPE IRQ_TYPE_LEVEL_LOW
	//	#elif INT_TRIGGER==3
	//		#define GT801_PLUS_IRQ_TYPE IRQ_TYPE_LEVEL_HIGH
		#endif
		    //pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
		err = ctp_ops.set_irq_mode("ctp_para", "ctp_int_port", CTP_IRQ_MODE);
		if(0 != err){
			printk("%s:ctp_ops.set_irq_mode err. \n", __func__);
			goto exit_set_irq_mode;
		}

		err =  request_irq(SW_INT_IRQNO_PIO, goodix_ts_irq_handler, GT801_PLUS_IRQ_TYPE|IRQF_SHARED, client->name, ts);
		if (err < 0) {
			pr_info( "goodix_probe: request irq failed\n");
			goto exit_irq_request_failed;
		}
		ts->use_irq = 1;
		printk("======Request EIRQ succesd!==== \n");
		dev_dbg(&client->dev,"Reques EIRQ %d succesd on GPIO:%d\n",INT_PORT,INT_PORT);

	}
#endif

	if (!ts->use_irq)
	{
		hrtimer_init(&ts->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		ts->timer.function = goodix_ts_timer_func;
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);
	}

    	//pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
	goodix_read_version(ts);

	//pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
	//init panel
	for(retry=0; retry<3; retry++)
	{
		//pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
		ret=goodix_init_panel(ts);
		dev_info(&client->dev,"the config ret is :%d\n",ret);
		//msleep(2);
		msleep(100);
		if(ret != 0)	//Initiall failed
			continue;
		else
			break;
	}
	if(ret != 0) {
		ts->bad_data=1;
		goto err_init_godix_ts;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = goodix_ts_early_suspend;
	ts->early_suspend.resume = goodix_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif


#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
	goodix_proc_entry = create_proc_entry("goodix-update", 0666, NULL);
	if(goodix_proc_entry == NULL)
	{
		dev_info(&client->dev, "Couldn't create proc entry!\n");
		ret = -ENOMEM;
		goto err_create_proc_entry;
	}
	else
	{
		dev_info(&client->dev, "Create proc entry success!\n");
		goodix_proc_entry->write_proc = goodix_update_write;
		goodix_proc_entry->read_proc = goodix_update_read;
//		goodix_proc_entry->owner =THIS_MODULE;
	}
#endif

	i2c_end_cmd(ts);
	 //pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
	dev_info(&client->dev,"Start %s in %s mode\n",
		ts->input_dev->name, ts->use_irq ? "interrupt" : "polling");
	//writel(0x6666,gpio_addr+0xdc);
	return 0;

err_init_godix_ts:
	i2c_end_cmd(ts);
	if(ts->use_irq)
	{
		ts->use_irq = 0;
//		free_irq(client->irq,ts);
	#ifdef INT_PORT
		//gpio_direction_input(INT_PORT);
		//gpio_free(INT_PORT);
		gpio_set_one_pin_io_status(gpio_int_hdle, 0, "ctp_int_port");

	#endif
	}
	else
		hrtimer_cancel(&ts->timer);

exit_set_irq_mode:
exit_irq_request_failed:
err_input_register_device_failed:
	input_free_device(ts->input_dev);

err_input_dev_alloc_failed:
	i2c_set_clientdata(client, NULL);
exit_gpio_wakeup_request_failed:
exit_ioremap_failed:
	if(gpio_addr){
		iounmap(gpio_addr);
	}
err_i2c_failed:
	kfree(ts);
err_alloc_data_failed:
err_check_functionality_failed:
err_create_proc_entry:
	return ret;
}


/*******************************************************
********************************************************/
static int goodix_ts_remove(struct i2c_client *client)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ts->early_suspend);
#endif
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
	remove_proc_entry("goodix-update", NULL);
#endif
	if (ts && ts->use_irq)
	{
	#ifdef INT_PORT
		//gpio_direction_input(INT_PORT);
		//gpio_free(INT_PORT);
 		gpio_set_one_pin_io_status(gpio_int_hdle, 0, "ctp_int_port");

	#endif
//		free_irq(client->irq, ts);
	}
	else if(ts)
		hrtimer_cancel(&ts->timer);

	dev_notice(&client->dev,"The driver is removing...\n");
	i2c_set_clientdata(client, NULL);
	input_unregister_device(ts->input_dev);
	kfree(ts);
	return 0;
}

#if defined(CONFIG_PM) || defined(CONFIG_HAS_EARLYSUSPEND)
//
static int goodix_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	int ret;
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	if (ts->use_irq){
		//disable_irq(client->irq);
		reg_val = readl(gpio_addr + PIO_INT_CTRL_OFFSET);
		reg_val &=~(1<<CTP_IRQ_NO);
		writel(reg_val,gpio_addr + PIO_INT_CTRL_OFFSET);
	}
	else
		hrtimer_cancel(&ts->timer);
	//ret = cancel_work_sync(&ts->work);
	//if(ret && ts->use_irq)
		//enable_irq(client->irq);
	if (ts->power) {	/* ±ØÐëÔÚÈ¡ÏûworkºóÔÙÖ´ÐÐ£¬±ÜÃâÒòGPIOµ¼ÖÂ×ø±ê´¦Àí´úÂëËÀÑ­»·	*/
		ret = ts->power(ts, 0);
		if (ret < 0)
			printk(KERN_ERR "goodix_ts_suspend power off failed\n");
		else
			printk(KERN_ERR "goodix_ts_suspend power off success\n");
	}

	return 0;

}

//
static int goodix_ts_resume(struct i2c_client *client)
{
	int ret;
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	if (ts->power) {
		ret = ts->power(ts, 1);
		if (ret < 0)
			printk(KERN_ERR "goodix_ts_resume power on failed\n");
		else
			printk(KERN_ERR "goodix_ts_resume power on success\n");
	}

	if (ts->use_irq){
		//enable_irq(client->irq);
		reg_val = readl(gpio_addr + PIO_INT_CTRL_OFFSET);
		reg_val |=(1<<CTP_IRQ_NO);
		writel(reg_val,gpio_addr + PIO_INT_CTRL_OFFSET);
	}
	else
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);

	return 0;
}
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
static void goodix_ts_early_suspend(struct early_suspend *h)
{
	struct goodix_ts_data *ts;
	ts = container_of(h, struct goodix_ts_data, early_suspend);
	goodix_ts_suspend(ts->client, PMSG_SUSPEND);
}

static void goodix_ts_late_resume(struct early_suspend *h)
{
	struct goodix_ts_data *ts;
	ts = container_of(h, struct goodix_ts_data, early_suspend);
	goodix_ts_resume(ts->client);
}
#endif

//******************************Begin of firmware update surpport*******************************
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
/*
static int  update_read_version(struct goodix_ts_data *ts, char **version)
{
	int ret = -1, count = 0;
	//unsigned char version_data[18];
	char *version_data;
	char *p;

	*version = (char *)vmalloc(5);
	version_data = *version;
	if(!version_data)
		return -ENOMEM;
	p = version_data;
	memset(version_data, 0, sizeof(version_data));
	version_data[0]=0x07;
	version_data[1]=0x17;
	ret=i2c_read_bytes(ts->client,version_data, 4);
	if (ret < 0)
		return ret;
	version_data[5]='\0';

	if(*p == '\0')
		return 0;
	do
	{
		if((*p > 122) || (*p < 48 && *p != 32) || (*p >57 && *p  < 65)
			||(*p > 90 && *p < 97 && *p  != '_'))		//check illeqal character
			count++;
	}while(*++p != '\0' );
	if(count > 2)
		return 0;
	else
		return 1;
}
*/

#if 0
/**
@brief CRC cal proc,include : Reflect,init_crc32_table,GenerateCRC32
@param global var oldcrc32
@return states
*/
static unsigned int Reflect(unsigned long int ref, char ch)
{
	unsigned int value=0;
	int i;
	for(i = 1; i < (ch + 1); i++)
	{
		if(ref & 1)
			value |= 1 << (ch - i);
		ref >>= 1;
	}
	return value;
}
#endif

/*---------------------------------------------------------------------------------------------------------*/
/*  CRC Check Program INIT								                                           		   */
/*---------------------------------------------------------------------------------------------------------*/
/*
static void init_crc32_table(void)
{
	unsigned int temp;
	unsigned int t1,t2;
	unsigned int flag;
	int i,j;
	for(i = 0; i <= 0xFF; i++)
	{
		temp=Reflect(i, 8);
		crc32_table[i]= temp<< 24;
		for (j = 0; j < 8; j++)
		{

			flag=crc32_table[i]&0x80000000;
			t1=(crc32_table[i] << 1);
			if(flag==0)
				t2=0;
			else
				t2=ulPolynomial;
			crc32_table[i] =t1^t2 ;

		}
		crc32_table[i] = Reflect(crc32_table[i], 32);
	}
}
*/
/*---------------------------------------------------------------------------------------------------------*/
/*  CRC main Program									                                           		   */
/*---------------------------------------------------------------------------------------------------------*/
/*
static void GenerateCRC32(unsigned char * buf, unsigned int len)
{
	unsigned int i;
	unsigned int t;

	for (i = 0; i != len; ++i)
	{
		t = (oldcrc32 ^ buf[i]) & 0xFF;
		oldcrc32 = ((oldcrc32 >> 8) & 0xFFFFFF) ^ crc32_table[t];
	}
}
*/
static struct file * update_file_open(char * path, mm_segment_t * old_fs_p)
{
	struct file * filp = NULL;
	int errno = -1;

	filp = filp_open(path, O_RDONLY, 0644);

	if(!filp || IS_ERR(filp))
	{
		if(!filp)
			errno = -ENOENT;
		else
			errno = PTR_ERR(filp);
		printk(KERN_ERR "The update file for Guitar open error.\n");
		return NULL;
	}
	*old_fs_p = get_fs();
	set_fs(get_ds());

	filp->f_op->llseek(filp,0,0);
	return filp ;
}

static void update_file_close(struct file * filp, mm_segment_t old_fs)
{
	set_fs(old_fs);
	if(filp)
		filp_close(filp, NULL);
}
static int update_get_flen(char * path)
{
	struct file * file_ck = NULL;
	mm_segment_t old_fs;
	int length ;

	file_ck = update_file_open(path, &old_fs);
	if(file_ck == NULL)
		return 0;

	length = file_ck->f_op->llseek(file_ck, 0, SEEK_END);
	//printk("File length: %d\n", length);
	if(length < 0)
		length = 0;
	update_file_close(file_ck, old_fs);
	return length;
}

static int goodix_update_write(struct file *filp, const char __user *buff, unsigned long len, void *data)
{
	unsigned char cmd[120];
	int ret = -1;
       int retry = 0;
	static unsigned char update_path[60];
	struct goodix_ts_data *ts;

	ts = i2c_get_clientdata(i2c_connect_client);
	if(ts==NULL)
	{
            printk(KERN_INFO"goodix write to kernel via proc file!@@@@@@\n");
		return 0;
	}

	//printk(KERN_INFO"goodix write to kernel via proc file!@@@@@@\n");
	if(copy_from_user(&cmd, buff, len))
	{
            printk(KERN_INFO"goodix write to kernel via proc file!@@@@@@\n");
		return -EFAULT;
	}
	//printk(KERN_INFO"Write cmd is:%d,write len is:%ld\n",cmd[0], len);
	switch(cmd[0])
	{
            case APK_UPDATE_TP:
            printk(KERN_INFO"Write cmd is:%d,cmd arg is:%s,write len is:%ld\n",cmd[0], &cmd[1], len);
            memset(update_path, 0, 60);
            strncpy(update_path, cmd+1, 60);

            ret = gt818_downloader( ts, goodix_gt818_firmware, update_path);

             if(ret < 0)
            {
                printk(KERN_INFO"Warnning: GT818 update might be ERROR!\n");
                return 0;
            }

            i2c_pre_cmd(ts);
	     msleep(2);

	    for(retry=0; retry<3; retry++)
	    {
	    	//pr_info("%s: %s, %d. \n", _, __func__, __LINE__);
		    ret=goodix_init_panel(ts);
		    printk(KERN_INFO"the config ret is :%d\n",ret);

		    msleep(2);
		    if(ret != 0)	//Initiall failed
			    continue;
		    else
			    break;
	    }

           if(ts->use_irq){
                //s3c_gpio_cfgpin(INT_PORT, INT_CFG);	//Set IO port as interrupt port
                	reg_val = readl(gpio_addr + PHO_CFG2_OFFSET);
        		reg_val &=(~(1<<20));
        		reg_val |=(3<<21);
        		writel(reg_val,gpio_addr + PHO_CFG2_OFFSET);
                    }
	    		else
                //gpio_direction_input(INT_PORT);
	  		gpio_set_one_pin_io_status(gpio_int_hdle,0, "ctp_int_port");

           i2c_end_cmd(ts);

	    if(ret != 0)
            {
		    ts->bad_data=1;
		    return 1;
	      }
            return 1;

            case APK_READ_FUN:							//functional command
			if(cmd[1] == CMD_READ_VER)
			{
				printk(KERN_INFO"Read version!\n");
				ts->read_mode = MODE_RD_VER;
			}
                    else if(cmd[1] == CMD_READ_CFG)
			{
				printk(KERN_INFO"Read config info!\n");
				ts->read_mode = MODE_RD_CFG;
			}
			else if (cmd[1] == CMD_READ_RAW)
			{
			    printk(KERN_INFO"Read raw data!\n");
			    ts->read_mode = MODE_RD_RAW;
			}
        		else if (cmd[1] == CMD_READ_CHIP_TYPE)
			{
		    	printk(KERN_INFO"Read chip type!\n");
			ts->read_mode = MODE_RD_CHIP_TYPE;
			}
            return 1;

            case APK_WRITE_CFG:
			printk(KERN_INFO"Begin write config info!Config length:%d\n",cmd[1]);
			i2c_pre_cmd(ts);
                    ret = i2c_write_bytes(ts->client, cmd+2, cmd[1]+2);
                    i2c_end_cmd(ts);
                    if(ret != 1)
                    {
                        printk("Write Config failed!return:%d\n",ret);
                        return -1;
                    }
                    return 1;

		default:
			return 0;
	}
	return 0;
}

static int goodix_update_read( char *page, char **start, off_t off, int count, int *eof, void *data )
{
#ifdef DEBUG
	int i;
#endif
	int ret = -1;
	int len = 0;
	int read_times = 0;
	struct goodix_ts_data *ts;
//	int len = 0;
	unsigned char read_data[360] = {80, };

	ts = i2c_get_clientdata(i2c_connect_client);
	if(ts==NULL)
		return 0;

	printk("___READ__\n");
	//read version data
	if(ts->read_mode == MODE_RD_VER)	{
		i2c_pre_cmd(ts);
		ret = goodix_read_version(ts);
		i2c_end_cmd(ts);
		if(ret <= 0){
			printk(KERN_INFO"Read version data failed!\n");
			return 0;
		}

		read_data[1] = (char)(ts->version&0xff);
		read_data[0] = (char)((ts->version>>8)&0xff);

		printk(KERN_INFO"Gt818 ROM version is:%x%x\n", read_data[0],read_data[1]);
		memcpy(page, read_data, 2);
		//*eof = 1;
		return 2;
	}else if (ts->read_mode == MODE_RD_CHIP_TYPE){
		page[0] = GT818;
		return 1;
	}else if(ts->read_mode == MODE_RD_CFG){

		read_data[0] = 0x06;
		read_data[1] = 0xa2;       // cfg start address
		printk("read config addr is:%x,%x\n", read_data[0],read_data[1]);

		len = 106;
		i2c_pre_cmd(ts);
		ret = i2c_read_bytes(ts->client, read_data, len+2);
		i2c_end_cmd(ts);
		if(ret <= 0){
			printk(KERN_INFO"Read config info failed!\n");
			return 0;
		}

		memcpy(page, read_data+2, len);
		return len;
	}else if (ts->read_mode == MODE_RD_RAW){
#define TIMEOUT (-100)
		int retry = 0;
		if (raw_data_ready != RAW_DATA_READY){
			raw_data_ready = RAW_DATA_ACTIVE;
		}

RETRY:
		read_data[0] = 0x07;
		read_data[1] = 0x11;
		read_data[2] = 0x01;

		ret = i2c_write_bytes(ts->client, read_data, 3);

#ifdef DEBUG
		sum += read_times;
		printk("count :%d\n", ++access_count);
		printk("A total of try times:%d\n", sum);
#endif

		read_times = 0;
		while (RAW_DATA_READY != raw_data_ready){
			msleep(4);

			if (read_times++ > 10){
				if (retry++ > 5){
					return TIMEOUT;
				}
				goto RETRY;
			}
		}
#ifdef DEBUG
		printk("read times:%d\n", read_times);
#endif
		read_data[0] = 0x08;
		read_data[1] = 0x80;       // raw data address

		len = 160;

		// msleep(4);

		i2c_pre_cmd(ts);
		ret = i2c_read_bytes(ts->client, read_data, len+2);
		//      i2c_end_cmd(ts);

		if(ret <= 0){
			printk(KERN_INFO"Read raw data failed!\n");
			return 0;
		}

		memcpy(page, read_data+2, len);

		read_data[0] = 0x09;
		read_data[1] = 0xC0;
		//	i2c_pre_cmd(ts);
		ret = i2c_read_bytes(ts->client, read_data, len+2);
		i2c_end_cmd(ts);

		if(ret <= 0){
			printk(KERN_INFO"Read raw data failed!\n");
			return 0;
		}
		memcpy(&page[160], read_data+2, len);

#ifdef DEBUG
		//**************
		for (i = 0; i < 300; i++)
		{
			printk("%6x", page[i]);

			if ((i+1) % 10 == 0)
			{
				printk("\n");
			}
		}
		//********************/
#endif
		raw_data_ready = RAW_DATA_NON_ACTIVE;

		return (2*len);

	}

	return -1;
}
#endif

//********************************************************************************************
static u8  is_equal( u8 *src , u8 *dst , int len )
{
    int i;

 //   for( i = 0 ; i < len ; i++ )
  //  {
        //printk(KERN_INFO"[%02X:%02X]\n", src[i], dst[i]);
 //   }

    for( i = 0 ; i < len ; i++ )
    {
        if ( src[i] != dst[i] )
        {
            return 0;
        }
    }

    return 1;
}

static  u8 gt818_nvram_store( struct goodix_ts_data *ts )
{
    int ret;
    int i;
    u8 inbuf[3] = {REG_NVRCS_H,REG_NVRCS_L,0};
    //u8 outbuf[3] = {};
    ret = i2c_read_bytes( ts->client, inbuf, 3 );

    if ( ret < 0 )
    {
        return 0;
    }

    if ( ( inbuf[2] & BIT_NVRAM_LOCK ) == BIT_NVRAM_LOCK )
    {
        return 0;
    }

    inbuf[2] = (1<<BIT_NVRAM_STROE);		//store command

    for ( i = 0 ; i < 300 ; i++ )
    {
        ret = i2c_write_bytes( ts->client, inbuf, 3 );

        if ( ret < 0 )
            break;
    }

    return ret;
}

static u8  gt818_nvram_recall( struct goodix_ts_data *ts )
{
    int ret;
    u8 inbuf[3] = {REG_NVRCS_H,REG_NVRCS_L,0};

    ret = i2c_read_bytes( ts->client, inbuf, 3 );

    if ( ret < 0 )
    {
        return 0;
    }

    if ( ( inbuf[2]&BIT_NVRAM_LOCK) == BIT_NVRAM_LOCK )
    {
        return 0;
    }

    inbuf[2] = ( 1 << BIT_NVRAM_RECALL );		//recall command
    ret = i2c_write_bytes( ts->client , inbuf, 3);
    return ret;
}

static  int gt818_reset( struct goodix_ts_data *ts )
{
    int ret = 1;
    u8 retry;

    unsigned char outbuf[3] = {0,0xff,0};
    unsigned char inbuf[3] = {0,0xff,0};
    //outbuf[1] = 1;

    //gpio_direction_output(SHUTDOWN_PORT,0);
    gpio_set_one_pin_io_status(gpio_wakeup_hdle, 1, "ctp_wakeup");
    gpio_write_one_pin_value(gpio_wakeup_hdle, 0, "ctp_wakeup");
    msleep(20);
    //gpio_direction_input(SHUTDOWN_PORT);
    gpio_set_one_pin_io_status(gpio_wakeup_hdle, 0, "ctp_wakeup");

    msleep(100);
    for(retry=0;retry < 80; retry++)
    {
        ret =i2c_write_bytes(ts->client, inbuf, 0);	//Test I2C connection.
        if (ret > 0)
        {
            msleep(10);
            ret =i2c_read_bytes(ts->client, inbuf, 3);	//Test I2C connection.
            if (ret > 0)
            {
                if(inbuf[2] == 0x55)
                    {
			    ret =i2c_write_bytes(ts->client, outbuf, 3);
			    msleep(10);
			    break;
			}
				}
			}

		}
    printk(KERN_INFO"Detect address %0X\n", ts->client->addr);
    //msleep(500);
    return ret;
}

static  int gt818_reset2( struct goodix_ts_data *ts )
{
	int ret = 1;
	u8 retry;

	//unsigned char outbuf[3] = {0,0xff,0};
	unsigned char inbuf[3] = {0,0xff,0};
	//outbuf[1] = 1;

	//gpio_direction_output(SHUTDOWN_PORT,0);
	gpio_set_one_pin_io_status(gpio_wakeup_hdle, 1, "ctp_wakeup");
	gpio_write_one_pin_value(gpio_wakeup_hdle, 0, "ctp_wakeup");
	msleep(20);
	//gpio_direction_input(SHUTDOWN_PORT);
	gpio_set_one_pin_io_status(gpio_wakeup_hdle, 0, "ctp_wakeup");
	msleep(100);
	for(retry=0;retry < 80; retry++)
	{
		ret =i2c_write_bytes(ts->client, inbuf, 0);	//Test I2C connection.
		if (ret > 0)
		{
			msleep(10);
			ret =i2c_read_bytes(ts->client, inbuf, 3);	//Test I2C connection.
			if (ret > 0)
			{
				//   if(inbuf[2] == 0x55)
				//       {
				//	    ret =i2c_write_bytes(ts->client, outbuf, 3);
				//	    msleep(10);
				break;
				//		}
			}
		}

	}
	printk(KERN_INFO"Detect address %0X\n", ts->client->addr);
	//msleep(500);
	return ret;
}



static  int gt818_set_address_2( struct goodix_ts_data *ts )
{
    unsigned char inbuf[3] = {0,0,0};
    int i;

    for ( i = 0 ; i < 12 ; i++ )
    {
        if ( i2c_read_bytes( ts->client, inbuf, 3) )
        {
            printk(KERN_INFO"Got response\n");
            return 1;
        }
        printk(KERN_INFO"wait for retry\n");
        msleep(50);
    }
    return 0;
}
static u8  gt818_update_firmware( u8 *nvram, u16 length, struct goodix_ts_data *ts)
{
    u8 ret = 0;
    u8 err = 0;
    u8 retry_time = 0;
    u8 i = 0;
    u16 cur_code_addr;
    u16 cur_frame_num, total_frame_num, cur_frame_len;
    u32 gt80x_update_rate;

    unsigned char i2c_data_buf[PACK_SIZE+2] = {0,};        //
    unsigned char i2c_chk_data_buf[PACK_SIZE+2] = {0,};        //
    if( length > NVRAM_LEN - NVRAM_BOOT_SECTOR_LEN )
    {
        printk(KERN_INFO"length too big %d %d\n", length, NVRAM_LEN - NVRAM_BOOT_SECTOR_LEN );
        return 0;
    }

    total_frame_num = ( length + PACK_SIZE - 1) / PACK_SIZE;

    //gt80x_update_sta = _UPDATING;
    gt80x_update_rate = 0;

    for( cur_frame_num = 0 ; cur_frame_num < total_frame_num ; cur_frame_num++ )
    {
        retry_time = 5;

        cur_code_addr = NVRAM_UPDATE_START_ADDR + cur_frame_num * PACK_SIZE;
        i2c_data_buf[0] = (cur_code_addr>>8)&0xff;
        i2c_data_buf[1] = cur_code_addr&0xff;

        i2c_chk_data_buf[0] = i2c_data_buf[0];
        i2c_chk_data_buf[1] = i2c_data_buf[1];

        if( cur_frame_num == total_frame_num - 1 )
        {
            cur_frame_len = length - cur_frame_num * PACK_SIZE;
        }
        else
        {
            cur_frame_len = PACK_SIZE;
        }

        //strncpy(&i2c_data_buf[2], &nvram[cur_frame_num*PACK_SIZE], cur_frame_len);
        for(i=0;i<cur_frame_len;i++)
        {
            i2c_data_buf[2+i] = nvram[cur_frame_num*PACK_SIZE+i];
            }
        do
        {
            err = 0;

            //ret = gt818_i2c_write( guitar_i2c_address, cur_code_addr, &nvram[cur_frame_num*I2C_FRAME_MAX_LENGTH], cur_frame_len );
            ret = i2c_write_bytes(ts->client, i2c_data_buf, (cur_frame_len+2));

            if ( ret <= 0 )
            {
                printk(KERN_INFO"write fail\n");
                err = 1;
            }

            ret = i2c_read_bytes(ts->client, i2c_chk_data_buf, (cur_frame_len+2));
           // ret = gt818_i2c_read( guitar_i2c_address, cur_code_addr, inbuf, cur_frame_len);

            if ( ret <= 0 )
            {
                printk(KERN_INFO"read fail\n");
                err = 1;
            }

            if( is_equal( &i2c_data_buf[2], &i2c_chk_data_buf[2], cur_frame_len ) == 0 )
            {
                printk(KERN_INFO"not equal\n");
                err = 1;
            }

        } while ( err == 1 && (--retry_time) > 0 );

        if( err == 1 )
        {
            break;
        }

        gt80x_update_rate = ( cur_frame_num + 1 )*128/total_frame_num;

    }

    if( err == 1 )
    {
        printk(KERN_INFO"write nvram fail\n");
        return 0;
    }

    ret = gt818_nvram_store(ts);

    msleep( 20 );

    if( ret == 0 )
    {
        printk(KERN_INFO"nvram store fail\n");
        return 0;
    }

    ret = gt818_nvram_recall(ts);

    msleep( 20 );

    if( ret == 0 )
    {
        printk(KERN_INFO"nvram recall fail\n");
        return 0;
    }

    for ( cur_frame_num = 0 ; cur_frame_num < total_frame_num ; cur_frame_num++ )		 //	read out all the code
    {

        cur_code_addr = NVRAM_UPDATE_START_ADDR + cur_frame_num*PACK_SIZE;
        retry_time=5;
        i2c_chk_data_buf[0] = (cur_code_addr>>8)&0xff;
        i2c_chk_data_buf[1] = cur_code_addr&0xff;


        if ( cur_frame_num == total_frame_num-1 )
        {
            cur_frame_len = length - cur_frame_num*PACK_SIZE;
        }
        else
        {
            cur_frame_len = PACK_SIZE;
        }

        do
        {
            err = 0;
            //ret = gt818_i2c_read( guitar_i2c_address, cur_code_addr, inbuf, cur_frame_len);
            ret = i2c_read_bytes(ts->client, i2c_chk_data_buf, (cur_frame_len+2));

            if ( ret == 0 )
            {
                err = 1;
            }

            if( is_equal( &nvram[cur_frame_num*PACK_SIZE], &i2c_chk_data_buf[2], cur_frame_len ) == 0 )
            {
                err = 1;
            }
        } while ( err == 1 && (--retry_time) > 0 );

        if( err == 1 )
        {
            break;
        }

        gt80x_update_rate = 127 + ( cur_frame_num + 1 )*128/total_frame_num;
    }

    gt80x_update_rate = 255;
    //gt80x_update_sta = _UPDATECHKCODE;

    if( err == 1 )
    {
        printk(KERN_INFO"nvram validate fail\n");
        return 0;
    }
    //Î¹0X00FFÐ´0XCCÂ±î¾ÊÂ¼Â½â¸
//    i2c_chk_data_buf[0] = 0xff;
//    i2c_chk_data_buf[1] = 0x00;
//    i2c_chk_data_buf[2] = 0x0;
//    ret = i2c_write_bytes(ts->client, i2c_chk_data_buf, 3);

//    if( ret <= 0 )
//    {
//        printk(KERN_INFO"nvram validate fail\n");
//        return 0;
//    }

    return 1;
}

static u8  gt818_update_proc( u8 *nvram, u16 length, struct goodix_ts_data *ts )
{
    u8 ret;
    u8 error = 0;
    //struct tpd_info_t tpd_info;
    GT818_SET_INT_PIN( 0 );
    msleep( 20 );
    ret = gt818_reset(ts);
    if ( ret < 0 )
    {
        error = 1;
        printk(KERN_INFO"reset fail\n");
        goto end;
    }

    ret = gt818_set_address_2( ts );
    if ( ret == 0 )
    {
        error = 1;
        printk(KERN_INFO"set address fail\n");
        goto end;
    }

    ret = gt818_update_firmware( nvram, length, ts);
    if ( ret == 0 )
    {
        error=1;
        printk(KERN_INFO"firmware update fail\n");
        goto end;
    }

end:
    GT818_SET_INT_PIN( 1 );
    //gpio_free(INT_PORT);
    //s3c_gpio_setpull(INT_PORT, S3C_GPIO_PULL_NONE);
      reg_val = readl(gpio_addr + PHO_PULL1_OFFSET);
      reg_val &=(~(3<<10));
      writel(reg_val,gpio_addr + PHO_PULL1_OFFSET);

    msleep( 500 );
    ret = gt818_reset2(ts);
    if ( ret < 0 )
    {
        error=1;
        printk(KERN_INFO"final reset fail\n");
        goto end;
    }
    if ( error == 1 )
    {
        return 0;
    }

    i2c_pre_cmd(ts);
    while(goodix_read_version(ts)<0);

    i2c_end_cmd(ts);
    return 1;
}

int  gt818_downloader( struct goodix_ts_data *ts,  unsigned char * data, unsigned char * path)
{
    struct tpd_firmware_info_t *fw_info = (struct tpd_firmware_info_t *)data;
    int i;
    unsigned short checksum = 0;
    unsigned char *data_ptr = &(fw_info->data);
    int retry = 0;
    int ret = 0;
    int err = 0;

    struct file * file_data = NULL;
    mm_segment_t old_fs;
    //unsigned int rd_len;
    unsigned int file_len = 0;
    //unsigned char i2c_data_buf[PACK_SIZE] = {0,};

    const int MAGIC_NUMBER_1 = 0x4D454449;
    const int MAGIC_NUMBER_2 = 0x4154454B;

    //pr_info("%s: %s, %d. \n", _, __func__, __LINE__);

    if(path[0] == 0)
    {
        printk(KERN_INFO"%s\n", __func__ );
        printk(KERN_INFO"magic number 0x%08X 0x%08X\n", fw_info->magic_number_1, fw_info->magic_number_2 );
        printk(KERN_INFO"magic number 0x%08X 0x%08X\n", MAGIC_NUMBER_1, MAGIC_NUMBER_2 );
        printk(KERN_INFO"current version 0x%04X, target verion 0x%04X\n", ts->version, fw_info->version );
        printk(KERN_INFO"size %d\n", fw_info->length );
        printk(KERN_INFO"checksum %d\n", fw_info->checksum );

        if ( fw_info->magic_number_1 != MAGIC_NUMBER_1 && fw_info->magic_number_2 != MAGIC_NUMBER_2 )
        {
            printk(KERN_INFO"Magic number not match\n");
            err = 0;
            goto exit_downloader;
        }
//        if(((ts->version&0xff)> 0x99)||((ts->version&0xff) < 0x4a))
//        {
//            goto update_start;
//        }
//        if ( ts->version >= fw_info->version )
//        {
//            printk(KERN_INFO"No need to upgrade\n");
//            err = 0;
//            goto exit_downloader;
//        }
	if( ((ts->version&0xf000) | (fw_info->version&0xf000)) == 0)   //
  	{
 		 if ( (ts->version&0x1ff) >= (fw_info->version&0x1ff) )
  		{
  			TPD_DOWNLOADER_DEBUG("No need to upgrade\n");
			goto exit_downloader;
		}
 	 }
      else
  	{
  		if(((ts->version&0xf000) & (fw_info->version&0xf000)) == 0xf000)    //
  		{
  			if( (ts->version&0xff) > (fw_info->version&0xff))
			{
				TPD_DOWNLOADER_DEBUG("No need to upgrade\n");
				goto exit_downloader;
			}
			else if((ts->version&0xff) == (fw_info->version&0xff))
			{
				if((ts->version&0xf00) >= (fw_info->version&0xf00))
				{
					TPD_DOWNLOADER_DEBUG("No need to upgrade\n");
					goto exit_downloader;
				}
				else
				{
					TPD_DOWNLOADER_DEBUG("Need to upgrade\n");
				}
			}
			else
			{
				TPD_DOWNLOADER_DEBUG("Need to upgrade\n");
			}
	}
	else if((ts->version&0xf000) == 0)
		{
		if( (ts->version&0xff) >= (fw_info->version&0xff))
			{
			TPD_DOWNLOADER_DEBUG("No need to upgrade\n");
			goto exit_downloader;
			}
		else
			{
			TPD_DOWNLOADER_DEBUG("Need to upgrade\n");
			}
		}
	else                   //
	{
	if( (ts->version&0xff) > (fw_info->version&0xff))
		{
		TPD_DOWNLOADER_DEBUG("No need to upgrade\n");
		goto exit_downloader;
		}
	else
		{
		TPD_DOWNLOADER_DEBUG("Need to upgrade\n");
		}
	}
	}
        if ( get_chip_version( ts->version ) != get_chip_version( fw_info->version ) )
            {
                printk(KERN_INFO"Chip version incorrect");
                err = 0;
                goto exit_downloader;
            }
//update_start:
        for ( i = 0 ; i < fw_info->length ; i++ )
            checksum += data_ptr[i];

        checksum = checksum%0xFFFF;

        if ( checksum != fw_info->checksum )
        {
            printk(KERN_INFO"Checksum not match 0x%04X\n", checksum);
            err = 0;
            goto exit_downloader;
        }
    }
    else
    {
        printk(KERN_INFO"Write cmd arg is:\n");
        file_data = update_file_open(path, &old_fs);
        printk(KERN_INFO"Write cmd arg is\n");
        if(file_data == NULL)	//file_data has been opened at the last time
        {
            err = -1;
            goto exit_downloader;
        }


    file_len = (update_get_flen(path))-2;

        printk(KERN_INFO"current length:%d\n", file_len);

            ret = file_data->f_op->read(file_data, &data_ptr[0], file_len, &file_data->f_pos);

            if(ret <= 0)
            {
               err = -1;
                goto exit_downloader;
            }



            update_file_close(file_data, old_fs);


        }
    printk(KERN_INFO"STEP_0:\n");
    //adapter = client->adapter;
    //gpio_free(INT_PORT);
    //ret = gpio_request(INT_PORT, "TS_INT");	//Request IO
    if (ret < 0)
    {
        printk(KERN_INFO"Failed to request GPIO:%d, ERRNO:%d\n",(int)INT_PORT,ret);
        err = -1;
        goto exit_downloader;
    }

    printk(KERN_INFO"STEP_1:\n");
    err = -1;
    while (  retry < 3 )
        {
            ret = gt818_update_proc( data_ptr, fw_info->length, ts);
            if(ret == 1)
            {
                err = 1;
                break;
            }
            retry++;
    }

exit_downloader:
    //mt_set_gpio_mode(GPIO_CTP_EINT_PIN, GPIO_CTP_EINT_PIN_M_EINT);
   // mt_set_gpio_out(GPIO_CTP_EN_PIN, GPIO_OUT_ONE);
       // gpio_direction_output(INT_PORT,1);
       // msleep(1);
  //      gpio_free(INT_PORT);
  //      s3c_gpio_setpull(INT_PORT, S3C_GPIO_PULL_NONE);
  		reg_val = readl(gpio_addr + PHO_PULL1_OFFSET);
        reg_val &=(~(3<<10));
        writel(reg_val,gpio_addr + PHO_PULL1_OFFSET);

    return err;

}
//******************************End of firmware update surpport*******************************
//¿ÉÓÃÓÚ¸ÃÇý¶¯µÄ Éè±¸Ãû¡ªÉè±¸ID ÁÐ±í
//only one client
static const struct i2c_device_id goodix_ts_id[] = {
	{ GOODIX_I2C_NAME, 0 },
	{ }
};

//Éè±¸Çý¶¯½á¹¹Ìå
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

/*******************************************************
¹¦ÄÜ£º
	Çý¶¯¼ÓÔØº¯Êý
return£º
	Ö´ÐÐ½á¹ûÂë£¬0±íÊ¾Õý³£Ö´ÐÐ
********************************************************/
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
	//
	ctp_set_gpio_mode();

	goodix_wq = create_singlethread_workqueue("goodix_wq");
	if (!goodix_wq) {
	printk(KERN_ALERT "Creat %s workqueue failed.\n", f3x_ts_name);
	return -ENOMEM;

	}
	//reset
	ctp_ops.ts_reset();
	//wakeup
	ctp_ops.ts_wakeup();

	goodix_ts_driver.detect = ctp_ops.ts_detect;

	ret = i2c_add_driver(&goodix_ts_driver);

	return ret;
}

/*******************************************************
¹¦ÄÜ£º
	Çý¶¯Ð¶ÔØº¯Êý
²ÎÊý£º
	client£ºÉè±¸½á¹¹Ìå
********************************************************/
static void __exit goodix_ts_exit(void)
{
	printk(KERN_ALERT "Touchscreen driver of guitar exited.\n");
	i2c_del_driver(&goodix_ts_driver);
	if (goodix_wq)
		destroy_workqueue(goodix_wq);		//release our work queue
}

late_initcall(goodix_ts_init);				//×îºó³õÊ¼»¯Çý¶¯felix
module_exit(goodix_ts_exit);

MODULE_DESCRIPTION("Goodix Touchscreen Driver");
MODULE_LICENSE("GPL");
