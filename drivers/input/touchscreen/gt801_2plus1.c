/*----------------------------------------------------------------------
 * Copyright 2010 Goodix Technology Corp.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *--------------------------------------------------------------------*/
#include <linux/i2c.h>
#include <linux/input.h>
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
#include <linux/vmalloc.h>
#include <linux/proc_fs.h>
#include <mach/irqs.h>
#include <mach/system.h>
#include <asm/uaccess.h>
#include <linux/syscalls.h>
#include <linux/reboot.h>
#include <mach/hardware.h>
#include <plat/sys_config.h>
#include "ctp_platform_ops.h"
#include "gt801_2plus1.h"

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/pm.h>
#include <linux/earlysuspend.h>
#endif

//#undef CONFIG_HAS_EARLYSUSPEND

#ifndef GUITAR_GT80X
#error The code does not match the hardware version.
#endif

const char *f3x_ts_name = "gt80x";
static struct workqueue_struct *goodix_wq;
#define X_DIFF (800)

///////////////////////////////////////////////
//specific tp related macro: need be configured for specific tp
#define CTP_IRQ_NO			(IRQ_EINT21)
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

static void *__iomem gpio_addr = NULL;
static int gpio_int_hdle = 0;
static int gpio_ph21_hdle = 0;

static int gpio_wakeup_hdle = 0;
static int gpio_reset_hdle = 0;
static int gpio_wakeup_enable = 1;
static int gpio_reset_enable = 1;

static int screen_max_x = 0;
static int screen_max_y = 0;
static int revert_x_flag = 0;
static int revert_y_flag = 0;
static int exchange_x_y_flag = 0;
static __u32 twi_addr = 0;
static __u32 twi_id = 0;

/* Addresses to scan */
union {
	unsigned short dirty_addr_buf[2];
	const unsigned short normal_i2c[2];
} u_i2c_addr = { {
0x00},};

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
	if (!(reg_val & (1 << CTP_IRQ_NO))) {
		state = PRESS_DOWN;
		pr_debug("pen down.\n");
	} else {		//touch panel is free up
		state = FREE_UP;
		pr_debug("free up.\n");
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
	if ((reg_val = (reg_val & (1 << (CTP_IRQ_NO))))) {
		pr_debug("==CTP_IRQ_NO=\n");
		writel(reg_val, gpio_addr + PIO_INT_STAT_OFFSET);
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
static int ctp_set_irq_mode(char *major_key, char *subkey,
			    ext_int_mode int_mode)
{
	int ret = 0;
	//config gpio to int mode
	printk("%s: config gpio to int mode.\n", __func__);
#ifndef SYSCONFIG_GPIO_ENABLE
#else
	if (gpio_int_hdle) {
		gpio_release(gpio_int_hdle, 2);
	}
	gpio_int_hdle = gpio_request_ex(major_key, subkey);
	if (!gpio_int_hdle) {
		printk("request tp_int_port failed.\n");
		ret = -1;
		goto request_tp_int_port_failed;
	}
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
	printk("%s: config gpio to io mode.\n", __func__);
#ifndef SYSCONFIG_GPIO_ENABLE
#else
	if (gpio_int_hdle) {
		gpio_release(gpio_int_hdle, 2);
	}
	gpio_int_hdle = gpio_request_ex("ctp_para", "ctp_io_port");
	if (!gpio_int_hdle) {
		printk("request ctp_io_port failed.\n");
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
	if (reg_val & (1 << (CTP_IRQ_NO))) {
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
	if (gpio_addr) {
		iounmap(gpio_addr);
	}

	if (gpio_int_hdle) {
		gpio_release(gpio_int_hdle, 2);
	}

	if (gpio_wakeup_hdle) {
		gpio_release(gpio_wakeup_hdle, 2);
	}

	if (gpio_reset_hdle) {
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
	gpio_wakeup_hdle = gpio_request_ex("ctp_para", "ctp_wakeup");
	if (!gpio_wakeup_hdle) {
		pr_warning("%s: tp_wakeup request gpio fail!\n", __func__);
		gpio_wakeup_enable = 0;
	}

	gpio_reset_hdle = gpio_request_ex("ctp_para", "ctp_en");
	if (!gpio_reset_hdle) {
		pr_warning("%s: tp_reset request gpio fail!\n", __func__);
		gpio_reset_enable = 0;
	}

	return 0;
}

/**
 * ctp_fetch_sysconfig_para - get config info from sysconfig.fex file.
 * return value:
 *                    = 0; success;
 *                    < 0; err
 */
static int ctp_fetch_sysconfig_para(void)
{
	int i, found = 0;
	int ret = -1;
	int ctp_used = -1;
	char name[I2C_NAME_SIZE];
	char key[16];
	script_parser_value_type_t type = SCRIPT_PARSER_VALUE_TYPE_STRING;

	printk("%s.\n", __func__);

	for (i = 0; i < 7; i++) {
		sprintf(key, "ctp%d_used", i);
		if (SCRIPT_PARSER_OK !=
		    script_parser_fetch("ctp_para", key, &ctp_used, 1)) {
			pr_err("%s: script_parser_fetch err.\n", __func__);
			continue;
		}
		if (1 != ctp_used) {
			pr_err("%s: ctp_unused.\n", __func__);
			continue;
		}

		sprintf(key, "ctp%d_name", i);
		if (SCRIPT_PARSER_OK !=
		    script_parser_fetch_ex("ctp_para", key, (int *)(&name),
					   &type, sizeof(name) / sizeof(int))) {
			pr_err("%s: script_parser_fetch err.\n", __func__);
			continue;
		}
		if (strcmp(CTP_NAME, name)) {
			pr_err("%s: name %s does not match CTP_NAME %s.\n",
			       __func__, name, CTP_NAME);
			continue;
		}

		sprintf(key, "ctp%d_twi_addr", i);
		if (SCRIPT_PARSER_OK !=
		    script_parser_fetch("ctp_para", key, &twi_addr,
					sizeof(twi_addr) / sizeof(__u32))) {
			pr_err("%s: script_parser_fetch err.\n", name);
			continue;
		}

		found = 1;
		break;
	}

	if (!found)
		goto script_parser_fetch_err;

	//big-endian or small-endian?
	//printk("%s: before: ctp_twi_addr is 0x%x, dirty_addr_buf: 0x%hx. dirty_addr_buf[1]: 0x%hx \n", __func__, twi_addr, u_i2c_addr.dirty_addr_buf[0], u_i2c_addr.dirty_addr_buf[1]);
	u_i2c_addr.dirty_addr_buf[0] = twi_addr;
	u_i2c_addr.dirty_addr_buf[1] = I2C_CLIENT_END;
	printk
	    ("%s: after: ctp_twi_addr is 0x%x, dirty_addr_buf: 0x%hx. dirty_addr_buf[1]: 0x%hx \n",
	     __func__, twi_addr, u_i2c_addr.dirty_addr_buf[0],
	     u_i2c_addr.dirty_addr_buf[1]);
	//printk("%s: after: ctp_twi_addr is 0x%x, u32_dirty_addr_buf: 0x%hx. u32_dirty_addr_buf[1]: 0x%hx \n", __func__, twi_addr, u32_dirty_addr_buf[0],u32_dirty_addr_buf[1]);

	if (SCRIPT_PARSER_OK !=
	    script_parser_fetch("ctp_para", "ctp_twi_id", &twi_id,
				sizeof(twi_id) / sizeof(__u32))) {
		pr_err("%s: script_parser_fetch err.\n", name);
		goto script_parser_fetch_err;
	}
	printk("%s: ctp_twi_id is %d.\n", __func__, twi_id);

	if (SCRIPT_PARSER_OK !=
	    script_parser_fetch("ctp_para", "ctp_screen_max_x", &screen_max_x,
				1)) {
		pr_err("%s: script_parser_fetch err.\n", __func__);
		goto script_parser_fetch_err;
	}
	pr_info("%s: screen_max_x = %d.\n", __func__, screen_max_x);

	if (SCRIPT_PARSER_OK !=
	    script_parser_fetch("ctp_para", "ctp_screen_max_y", &screen_max_y,
				1)) {
		pr_err("%s: script_parser_fetch err.\n", __func__);
		goto script_parser_fetch_err;
	}
	pr_info("%s: screen_max_y = %d.\n", __func__, screen_max_y);

	if (SCRIPT_PARSER_OK !=
	    script_parser_fetch("ctp_para", "ctp_revert_x_flag", &revert_x_flag,
				1)) {
		pr_err("%s: script_parser_fetch err.\n", __func__);
		goto script_parser_fetch_err;
	}
	pr_info("%s: revert_x_flag = %d.\n", __func__, revert_x_flag);

	if (SCRIPT_PARSER_OK !=
	    script_parser_fetch("ctp_para", "ctp_revert_y_flag", &revert_y_flag,
				1)) {
		pr_err("%s: script_parser_fetch err.\n", __func__);
		goto script_parser_fetch_err;
	}
	pr_info("%s: revert_y_flag = %d.\n", __func__, revert_y_flag);

	if (SCRIPT_PARSER_OK !=
	    script_parser_fetch("ctp_para", "ctp_exchange_x_y_flag",
				&exchange_x_y_flag, 1)) {
		pr_err("ft5x_ts: script_parser_fetch err.\n");
		goto script_parser_fetch_err;
	}
	pr_info("%s: exchange_x_y_flag = %d.\n", __func__, exchange_x_y_flag);

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
	printk("%s.\n", __func__);
	if (gpio_reset_enable) {
		if (EGPIO_SUCCESS !=
		    gpio_write_one_pin_value(gpio_reset_hdle, 1, "ctp_en")) {
			printk("%s: err when operate gpio.\n", __func__);
		}
		mdelay(TS_RESET_LOW_PERIOD);
		if (EGPIO_SUCCESS !=
		    gpio_write_one_pin_value(gpio_reset_hdle, 0, "ctp_en")) {
			printk("%s: err when operate gpio.\n", __func__);
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
	printk("%s.\n", __func__);
	if (1 == gpio_wakeup_enable) {
		if (EGPIO_SUCCESS !=
		    gpio_write_one_pin_value(gpio_wakeup_hdle, 0,
					     "ctp_wakeup")) {
			printk("%s: err when operate gpio.\n", __func__);
		}
		mdelay(TS_WAKEUP_LOW_PERIOD);
		if (EGPIO_SUCCESS !=
		    gpio_write_one_pin_value(gpio_wakeup_hdle, 1,
					     "ctp_wakeup")) {
			printk("%s: err when operate gpio.\n", __func__);
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

	if (twi_id == adapter->nr) {
		pr_info("%s: Detected chip %s at adapter %d, address 0x%02x\n",
			__func__, CTP_NAME, i2c_adapter_id(adapter),
			client->addr);

		strlcpy(info->type, CTP_NAME, I2C_NAME_SIZE);
		return 0;
	} else {
		return -ENODEV;
	}
}

////////////////////////////////////////////////////////////////

static struct ctp_platform_ops ctp_ops = {
	.get_pendown_state = ctp_get_pendown_state,
	.clear_penirq = ctp_clear_penirq,
	.set_irq_mode = ctp_set_irq_mode,
	.set_gpio_mode = ctp_set_gpio_mode,
	.judge_int_occur = ctp_judge_int_occur,
	.init_platform_resource = ctp_init_platform_resource,
	.free_platform_resource = ctp_free_platform_resource,
	.fetch_sysconfig_para = ctp_fetch_sysconfig_para,
	.ts_reset = ctp_reset,
	.ts_wakeup = ctp_wakeup,
	.ts_detect = ctp_detect,
};

static int sun4i_ph21_free(void)
{
	if (gpio_ph21_hdle) {
		gpio_release(gpio_ph21_hdle, 2);
		gpio_ph21_hdle = 0;
	}
	//printk("<FUNC:FREE>\n");

	return 0;
}

static int sun4i_set_ph21_input(void)
{
	return 0;
	if (gpio_ph21_hdle) {
		gpio_release(gpio_ph21_hdle, 2);
		gpio_ph21_hdle = 0;
	}

	gpio_ph21_hdle = gpio_request_ex("ctp_para", "ctp_io_port_i");

	//printk("<FUNC:INPUT>gpio_ph21_hdle = %d\n", gpio_ph21_hdle);

	return gpio_ph21_hdle;
}

static int sun4i_set_ph21_output(void)
{

	return 0;
	if (gpio_ph21_hdle) {
		gpio_release(gpio_ph21_hdle, 2);
		gpio_ph21_hdle = 0;
	}

	gpio_ph21_hdle = gpio_request_ex("ctp_para", "ctp_io_port_o");

	printk("<FUNC:OUTPUT>gpio_ph21_hdle = %d\n", gpio_ph21_hdle);

	return gpio_ph21_hdle;
}

static int sun4i_set_ph21_int(void)
{
	int reg_val;

	if (gpio_ph21_hdle) {
		gpio_release(gpio_ph21_hdle, 2);
		gpio_ph21_hdle = 0;
	}

	if (!gpio_addr) {
		gpio_addr = ioremap(PIO_BASE_ADDRESS, PIO_RANGE_SIZE);
	}
	reg_val = readl(gpio_addr + PIO_INT_CFG2_OFFSET);
	reg_val &= (~(7 << 20));
	writel(reg_val, gpio_addr + PIO_INT_CFG2_OFFSET);

	reg_val = readl(gpio_addr + PIO_INT_CTRL_OFFSET);
	reg_val |= (1 << IRQ_EINT21);
	writel(reg_val, gpio_addr + PIO_INT_CTRL_OFFSET);

	gpio_ph21_hdle = gpio_request_ex("ctp_para", "ctp_int_port");

	//printk("<FUNC:INT>gpio_ph21_hdle = %d\n", gpio_ph21_hdle);

	return gpio_ph21_hdle;
}

/*used by GT80X-IAP module */
//EXPORT_SYMBOL(i2c_connect_client);
/**********************************************************************
本程序中I2C通信方式为：
	7bit从机地址｜读写位 + buf（数据地址+读写数据）
	 --------------------------------------------------------------------
	｜  从机地址   ｜ buf[0](数据地址) | buf[1]~buf[MAX-1](写入或读取到的数据)  |
	 --------------------------------------------------------------------
	移植前请根据自身主控格式修改！！
***********************************************************************/

//Function as i2c_master_receive, and return 2 if operation is successful.
static int i2c_read_bytes(struct i2c_client *client, uint8_t * buf, int len)
{
	struct i2c_msg msgs[2];
	int ret = -1;
	int retries = 0;

	msgs[0].flags = !I2C_M_RD;
	msgs[0].addr = client->addr;
	msgs[0].len = 1;
	msgs[0].buf = &buf[0];

	msgs[1].flags = I2C_M_RD;
	msgs[1].addr = client->addr;
	msgs[1].len = len - 1;
	msgs[1].buf = &buf[1];

	while (retries < 5) {
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret == 2)
			break;
		retries++;
	}
	return ret;
}

/*******************************************************
Description:
	write data to the i2c slave device.

Parameter:
	client:	i2c device.
	buf[0]:operate address.
	buf[1]~buf[len]:write data buffer.
	len:operate length.

return:
	numbers of i2c_msgs to transfer.
*********************************************************/
static int i2c_write_bytes(struct i2c_client *client, uint8_t * data, int len)
{
	struct i2c_msg msg;
	int ret = -1;
	int retries = 0;

	msg.flags = !I2C_M_RD;
	msg.addr = client->addr;
	msg.len = len;
	msg.buf = data;

	while (retries < 5) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret == 1)
			break;
		retries++;
	}
	return ret;
}

/*******************************************************
Description:
	Goodix touchscreen initialize function.

Parameter:
	ts:	i2c client private struct.

return:
	Executive outcomes.0---succeed.
*******************************************************/
static int goodix_init_panel(struct goodix_ts_data *ts)
{
	int ret = -1;
	uint8_t rd_cfg_buf[7] = { 0x66, };
#if 0
#if 0				//def DRIVER_SEND_CFG                             //for kedi 9.7(puts your config info here,if need send config info)
	uint8_t config_info[] = {
#if 0
		0x65, 0x00, (TOUCH_MAX_HEIGHT >> 8), (TOUCH_MAX_HEIGHT & 0xff),
		(TOUCH_MAX_WIDTH >> 8), (TOUCH_MAX_WIDTH & 0xff),
		MAX_FINGER_NUM, (0x2C | INT_TRIGGER),
		0x65, 0x00, 0x04, 0x00, 0x03, 0x00, 0x05, 0x6C, 0x21, 0x00,
		0x0F, 0x28, 0x02,
		0x08, 0x10, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x88, 0x10, 0x10,
		0x64, 0x13, 0x00,
		0x00, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08, 0x07, 0x06, 0x05,
		0x04, 0x03, 0x02,
		0x01, 0x00, 0xFF, 0xFF, 0x0D, 0x0C, 0x0B, 0x0A, 0x09, 0x08,
		0x07, 0x06, 0x05,
		0x04, 0x03, 0x02, 0x01, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x3C,
		0x50, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x20
#else
		0x65, 0x00, 0x04, 0x00, 0x03, 0x00, 0x0A, 0x0E, 0x1E, 0xE7,
		0x32, 0x02, 0x08,
		0x10, 0x4C, 0x41, 0x41, 0x20, 0x07, 0x00, 0x80, 0x80, 0x3C,
		0x55, 0x00, 0x01,
		0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
		0x0C, 0x0D, 0x0E,
		0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
		0x19, 0x1A, 0x1B,
		0x1C, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
#endif
	};

#else
	//printk("config GT8105 2+1, by King Law\n");
	uint8_t config_info[] = {
		0x65, 0x00, 0x04, 0x00, 0x03, 0x00, 0x0A, 0x0C, 0x1E, 0xE7,
		0x32, 0x02, 0x08, 0x10, 0x4C, 0x41, 0x41, 0x20, 0x07, 0x00,
		0x80, 0x80, 0x3C, 0x55, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
		0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
		0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
		0x1A, 0x1B, 0x1C, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	//WAKEUP GREEN MODE
	dev_info(&ts->client->dev, "gt801 2pulus1 init panel!\n");
	disable_irq(ts->client->irq);
	sun4i_set_ph21_output();	//gpio_direction_output(INT_PORT, 0);
	msleep(5);
	sun4i_set_ph21_int();	//s3c_gpio_cfgpin(INT_PORT, INT_CFG);
	enable_irq(ts->client->irq);

	ret =
	    i2c_write_bytes(ts->client, config_info,
			    (sizeof(config_info) / sizeof(config_info[0])));
	if (ret < 0)
		return ret;
#endif
#endif
	ret = i2c_read_bytes(ts->client, rd_cfg_buf, 7);
	if (ret != 2) {
		dev_info(&ts->client->dev,
			 "Read resolution & max_touch_num failed, use default value!\n");
		ts->abs_x_max = TOUCH_MAX_HEIGHT;
		ts->abs_y_max = TOUCH_MAX_WIDTH;
		ts->max_touch_num = MAX_FINGER_NUM;
		ts->int_trigger_type = INT_TRIGGER;
		return 0;
	}

	ts->abs_x_max = (rd_cfg_buf[1] << 8) + rd_cfg_buf[2];
	ts->abs_y_max = (rd_cfg_buf[3] << 8) + rd_cfg_buf[4];
	ts->max_touch_num = rd_cfg_buf[5];
	ts->int_trigger_type = rd_cfg_buf[6] & 0x03;
	if ((!ts->abs_x_max) || (!ts->abs_y_max) || (!ts->max_touch_num)) {
		dev_info(&ts->client->dev,
			 "Read invalid resolution & max_touch_num, use default value!\n");
		ts->abs_x_max = TOUCH_MAX_HEIGHT;
		ts->abs_y_max = TOUCH_MAX_WIDTH;
		ts->max_touch_num = MAX_FINGER_NUM;
	}

	dev_info(&ts->client->dev, "X_MAX = %d,Y_MAX = %d,MAX_TOUCH_NUM = %d\n",
		 ts->abs_x_max, ts->abs_y_max, ts->max_touch_num);
	//wake up mode from green mode
	rd_cfg_buf[0] = 0x6e;
	rd_cfg_buf[1] = 0x00;
	i2c_read_bytes(ts->client, rd_cfg_buf, 2);
	if ((rd_cfg_buf[1] & 0x0f) == 0x0f) {
		dev_info(&ts->client->dev,
			 "Touchscreen works in INT wake up green mode!\n");
		ts->green_wake_mode = 1;
	} else {
		dev_info(&ts->client->dev,
			 "Touchscreen works in IIC wake up green mode!\n");
		ts->green_wake_mode = 0;
	}

	msleep(10);
	return 0;
}

/*******************************************************
Description:
	Read goodix touchscreen version function.

Parameter:
	ts:	i2c client private struct.

return:
	Executive outcomes.0---succeed.
*******************************************************/
static int goodix_read_version(struct goodix_ts_data *ts, char **version)
{
	int ret = -1, count = 0;
	char *version_data;
	char *p;

	*version = (char *)vmalloc(18);
	version_data = *version;
	if (!version_data)
		return -ENOMEM;
	p = version_data;
	memset(version_data, 0, sizeof(version_data));
	version_data[0] = 240;
	if (ts->green_wake_mode)	//WAKEUP GREEN MODE
	{
		disable_irq(ts->client->irq);
		sun4i_set_ph21_output();	//gpio_direction_output(INT_PORT, 0);
		msleep(5);
		sun4i_set_ph21_int();	//s3c_gpio_cfgpin(INT_PORT, INT_CFG);
		enable_irq(ts->client->irq);
	}
	ret = i2c_read_bytes(ts->client, version_data, 17);
	if (ret < 0)
		return ret;
	version_data[17] = '\0';

	if (*p == '\0')
		return 0;
	do {
		if ((*p > 122) || (*p < 48 && *p != 32) || (*p > 57 && *p < 65)
		    || (*p > 90 && *p < 97 && *p != '_'))	//check illeqal character
			count++;
	} while (*++p != '\0');
	if (count > 2)
		return 0;
	else
		return 1;
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
	int ret = -1;
	int tmp = 0;
	uint8_t point_data[(1 - READ_COOR_ADDR) + 1 + 2 + 5 * MAX_FINGER_NUM + 1] = { 0 };	//read address(1byte)+key index(1byte)+point mask(2bytes)+5bytes*MAX_FINGER_NUM+coor checksum(1byte)
	uint8_t check_sum = 0;
	uint16_t finger_current = 0;
	uint16_t finger_bit = 0;
	unsigned int count = 0, point_count = 0;
	unsigned int position = 0;
	uint8_t track_id[MAX_FINGER_NUM] = { 0 };
	unsigned int input_x = 0;
	unsigned int input_y = 0;
	unsigned int input_w = 0;
	unsigned char index = 0;
	unsigned char touch_num = 0;

	struct goodix_ts_data *ts =
	    container_of(work, struct goodix_ts_data, work);

	if (g_enter_isp)
		return;
#if defined(INT_PORT)
COORDINATE_POLL:
	if ((ts->int_trigger_type > 1)
	    && (gpio_read_one_pin_value(gpio_ph21_hdle, "ctp_int_port") !=
		(ts->int_trigger_type & 0x01))) {
		printk("COORDINATE_POLL, goto NO_ACTION\n");

		goto NO_ACTION;
	}
#endif

	if (tmp > 9) {

		dev_info(&(ts->client->dev),
			 "I2C transfer error,touchscreen stop working.\n");
		goto XFER_ERROR;
	}

	if (ts->bad_data)
		msleep(20);

	point_data[0] = READ_COOR_ADDR;	//read coor address
	ret =
	    i2c_read_bytes(ts->client, point_data,
			   sizeof(point_data) / sizeof(point_data[0]));
	if (ret <= 0) {
		dev_err(&(ts->client->dev), "I2C transfer error. Number:%d\n ",
			ret);
		ts->bad_data = 1;
		tmp++;
		ts->retry++;
#if defined(INT_PORT)
		if (ts->int_trigger_type > 1)
			goto COORDINATE_POLL;
		else
			goto XFER_ERROR;
#endif
	}
	ts->bad_data = 0;
	finger_current =
	    (point_data[3 - READ_COOR_ADDR] << 8) + point_data[2 -
							       READ_COOR_ADDR];

	if (finger_current) {
		point_count = 0, finger_bit = finger_current;
		for (count = 0; (finger_bit != 0) && (count < ts->max_touch_num); count++)	//cal how many point touch currntly
		{
			if (finger_bit & 0x01) {
				track_id[point_count] = count;
				point_count++;
			}
			finger_bit >>= 1;
		}
		touch_num = point_count;

		check_sum = point_data[2 - READ_COOR_ADDR] + point_data[3 - READ_COOR_ADDR];	//cal coor checksum
		count = 4 - READ_COOR_ADDR;
		for (point_count *= 5; point_count > 0; point_count--)
			check_sum += point_data[count++];
		check_sum += point_data[count];
		if (check_sum != 0)	//checksum verify error
		{
#if 0
			dev_info(&ts->client->dev, "Check_sum:%d,  Data:%d\n",
				 check_sum, point_data[count]);
			printk("Finger Bit:%d\n", finger_current);
			for (; count > 0; count--)
				printk("count=%d:%d  ", count,
				       point_data[count]);
			printk("\n");
#endif
			printk("coor checksum error!\n");
#if defined(INT_PORT)
			if (ts->int_trigger_type > 1)
				goto COORDINATE_POLL;
			else
				goto XFER_ERROR;
#endif
		}
	}
	if (touch_num) {
		for (index = 0; index < touch_num; index++) {
			position = 4 - READ_COOR_ADDR + 5 * index;
			input_x =
			    (unsigned int)(point_data[position] << 8) +
			    (unsigned int)(point_data[position + 1]);
			input_y =
			    (unsigned int)(point_data[position + 2] << 8) +
			    (unsigned int)(point_data[position + 3]);
			input_w = (unsigned int)(point_data[position + 4]);

			if (1 == revert_x_flag) {
				input_x = screen_max_x - input_x;
			}
			if (1 == revert_y_flag) {
				input_y = screen_max_y - input_y;
			}
			if (0 == exchange_x_y_flag) {
				swap(input_x, input_y);
			}
			//printk("input_x = %d,input_y = %d, input_w = %d\n", input_x, input_y, input_w);

			if ((input_x > ts->abs_x_max)
			    || (input_y > ts->abs_y_max))
				continue;

			if (touch_num == 1) {
				input_report_abs(ts->input_dev, ABS_X, input_x);
				input_report_abs(ts->input_dev, ABS_Y, input_y);
				input_report_abs(ts->input_dev, ABS_PRESSURE,
						 input_w);
			}

			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID,
					 track_id[index]);
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 1);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_X,
					 input_x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y,
					 input_y);
			input_report_abs(ts->input_dev, ABS_MT_PRESSURE,
					 input_w);
			input_report_key(ts->input_dev, BTN_TOUCH, 1);
			input_mt_sync(ts->input_dev);
		}
	} else {
		input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
//              input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, 0);
		input_report_abs(ts->input_dev, ABS_PRESSURE, 0);
		input_report_key(ts->input_dev, BTN_TOUCH, 0);
		input_mt_sync(ts->input_dev);
	}

#ifdef HAVE_TOUCH_KEY
	//printk("HAVE KEY DOWN!0x%x\n",point_data[1]);
	for (count = 0; count < MAX_KEY_NUM; count++) {
		input_report_key(ts->input_dev, touch_key_array[count],
				 ! !(point_data[1] & (0x01 << count)));
	}
#endif
	input_sync(ts->input_dev);

#if defined(INT_PORT)
	if (ts->int_trigger_type > 1) {
		msleep(POLL_TIME);
		goto COORDINATE_POLL;
	}
#endif
	goto END_WORK_FUNC;

NO_ACTION:

#ifdef HAVE_TOUCH_KEY
	//printk("HAVE KEY DOWN!0x%x\n",point_data[1]);
	for (count = 0; count < MAX_KEY_NUM; count++) {
		input_report_key(ts->input_dev, touch_key_array[count],
				 ! !(point_data[1] & (0x01 << count)));
	}
	input_sync(ts->input_dev);
#endif
END_WORK_FUNC:
XFER_ERROR:
	if (ts->use_irq)
		enable_irq(ts->client->irq);

}

/*******************************************************
功能：
	中断响应函数
	由中断触发，调度触摸屏处理函数运行
********************************************************/
static irqreturn_t goodix_ts_irq_handler(int irq, void *dev_id)
{
#if 0
	struct goodix_ts_data *ts = dev_id;

	disable_irq_nosync(ts->client->irq);
	queue_work(goodix_wq, &ts->work);

	return IRQ_HANDLED;
#else
	struct goodix_ts_data *ts = dev_id;
	int reg_val;
	//printk("==========------TS Interrupt-----============ snake >>>>>>>>>>>>>>>>>>\n");

	//clear the IRQ_EINT21 interrupt pending
	reg_val = readl(gpio_addr + PIO_INT_STAT_OFFSET);

	if (reg_val & (1 << (IRQ_EINT21))) {
		//printk("==IRQ_EINT21=\n");
		writel(reg_val & (1 << (IRQ_EINT21)),
		       gpio_addr + PIO_INT_STAT_OFFSET);
		disable_irq_nosync(ts->client->irq);
		queue_work(goodix_wq, &ts->work);
	} else {
		//printk("Other Interrupt\n");
		return IRQ_NONE;
	}

	return IRQ_HANDLED;

#endif
}

/*******************************************************
功能：
	GT80X的电源管理
参数：
	on:设置GT80X运行模式，0为进入Sleep模式
return：
	是否设置成功，小于0表示设置失败
********************************************************/
static int goodix_ts_power(struct goodix_ts_data *ts, int on)
{
	int ret = -1;
	unsigned char i2c_control_buf[2] = { 80, 1 };	//suspend cmd
	int retry = 0;
	if (on != 0 && on != 1) {
		printk(KERN_DEBUG "%s: Cant't support this command.",
		       goodix_ts_name);
		return -EINVAL;
	}

	if (ts != NULL && !ts->use_irq)
		return -2;

	if (on == 0)		//suspend
	{
		if (ts->green_wake_mode) {
			disable_irq(ts->client->irq);
			sun4i_set_ph21_output();	//gpio_direction_output(INT_PORT, 0);
			msleep(5);
			sun4i_set_ph21_int();	//s3c_gpio_cfgpin(INT_PORT, INT_CFG);
			enable_irq(ts->client->irq);
		}
		while (retry < 5) {
			ret = i2c_write_bytes(ts->client, i2c_control_buf, 2);
			if (ret == 1) {
				//printk("Send suspend cmd\n");
				break;
			}
			//printk("Send cmd failed!\n");
			retry++;
			msleep(10);
		}
		if (ret > 0)
			ret = 0;
	} else if (on == 1)	//resume
	{
		//printk("Int resume\n");
		sun4i_set_ph21_output();	//gpio_direction_output(INT_PORT, 0);
		msleep(20);
		if (ts->use_irq)
			sun4i_set_ph21_int();	//s3c_gpio_cfgpin(INT_PORT, INT_CFG); //Set IO port as interrupt port
		else
			sun4i_set_ph21_input();	//gpio_direction_input(INT_PORT);
		//msleep(260);

		ret = 0;
	}
	return ret;
}

/*******************************************************
Description:
	Goodix debug sysfs cat version function.

Parameter:
	standard sysfs show param.

return:
	Executive outcomes. 0---failed.
*******************************************************/
static ssize_t goodix_debug_version_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int ret = 0;
	char *version_info = NULL;
	struct goodix_ts_data *ts;

	ts = i2c_get_clientdata(i2c_connect_client);
	if (ts == NULL)
		return 0;

	ret = goodix_read_version(ts, &version_info);
	if (ret <= 0) {
		printk("Read version data failed!\n");
		vfree(version_info);
		return 0;
	}

	printk("Goodix TouchScreen Version:%s\n", (version_info + 1));
	sprintf(buf, "Goodix TouchScreen Version:%s\n", (version_info + 1));
	vfree(version_info);
	ret = strlen(buf);
	return ret;
}

/*******************************************************
Description:
	Goodix debug sysfs cat resolution function.

Parameter:
	standard sysfs show param.

return:
	Executive outcomes. 0---failed.
*******************************************************/
static ssize_t goodix_debug_resolution_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct goodix_ts_data *ts;
	ts = i2c_get_clientdata(i2c_connect_client);
	dev_info(&ts->client->dev, "ABS_X_MAX = %d,ABS_Y_MAX = %d\n",
		 ts->abs_x_max, ts->abs_y_max);
	sprintf(buf, "ABS_X_MAX = %d,ABS_Y_MAX = %d\n", ts->abs_x_max,
		ts->abs_y_max);

	return strlen(buf);
}

/*******************************************************
Description:
	Goodix debug sysfs cat version function.

Parameter:
	standard sysfs show param.

return:
	Executive outcomes. 0---failed.
*******************************************************/
static ssize_t goodix_debug_diffdata_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	//char diff_data[300];
	unsigned char diff_data[2241] = { 00, };
	int ret = -1;
	char diff_data_cmd[2] = { 80, 202 };
	int i;
	int short_tmp;
	struct goodix_ts_data *ts;

	disable_irq(TS_INT);

	ts = i2c_get_clientdata(i2c_connect_client);
	//memset(diff_data, 0, sizeof(diff_data));
	if (ts->green_wake_mode) {
		//disable_irq(client->irq);
		sun4i_set_ph21_output();	//gpio_direction_output(INT_PORT, 0);
		msleep(5);
		sun4i_set_ph21_int();	//s3c_gpio_cfgpin(INT_PORT, INT_CFG);
		//enable_irq(client->irq);
	}
	ret = i2c_write_bytes(ts->client, diff_data_cmd, 2);
	if (ret != 1) {
		dev_info(&ts->client->dev, "Write diff data cmd failed!\n");
		enable_irq(TS_INT);
		return 0;
	}

	while (gpio_read_one_pin_value(gpio_ph21_hdle, "ctp_int_port")) ;	//(gpio_get_value(INT_PORT));
	ret = i2c_read_bytes(ts->client, diff_data, sizeof(diff_data));
	if (ret != 2) {
		dev_info(&ts->client->dev, "Read diff data failed!\n");
		enable_irq(TS_INT);
		return 0;
	}
	for (i = 1; i < sizeof(diff_data); i += 2) {
		short_tmp = diff_data[i] + (diff_data[i + 1] << 8);
		if (short_tmp & 0x8000)
			short_tmp -= 65535;
		if (short_tmp == 512)
			continue;
		sprintf(buf + strlen(buf), " %d", short_tmp);
		//printk(" %d\n", short_tmp);
	}

	diff_data_cmd[1] = 0;
	ret = i2c_write_bytes(ts->client, diff_data_cmd, 2);
	if (ret != 1) {
		dev_info(&ts->client->dev, "Write diff data cmd failed!\n");
		enable_irq(TS_INT);
		return 0;
	}
	enable_irq(TS_INT);
	/*for (i=0; i<1024; i++)
	   {
	   sprintf(buf+strlen(buf)," %d",i);
	   } */

	return strlen(buf);
}

/*******************************************************
Description:
	Goodix debug sysfs echo calibration function.

Parameter:
	standard sysfs store param.

return:
	Executive outcomes..
*******************************************************/
static ssize_t goodix_debug_calibration_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	int ret = -1;
	char cal_cmd_buf[] = { 110, 1 };
	struct goodix_ts_data *ts;

	ts = i2c_get_clientdata(i2c_connect_client);
	dev_info(&ts->client->dev, "Begin calibration......\n");
	if ((*buf == 10) || (*buf == 49)) {
		if (ts->green_wake_mode) {
			disable_irq(ts->client->irq);
			sun4i_set_ph21_output();	//gpio_direction_output(INT_PORT, 0);
			msleep(5);
			sun4i_set_ph21_int();	//s3c_gpio_cfgpin(INT_PORT, INT_CFG);
			enable_irq(ts->client->irq);
		}
		ret = i2c_write_bytes(ts->client, cal_cmd_buf, 2);
		if (ret != 1) {
			dev_info(&ts->client->dev, "Calibration failed!\n");
			return count;
		} else {
			dev_info(&ts->client->dev, "Calibration succeed!\n");
		}
	}
	return count;
}

static DEVICE_ATTR(version, S_IRUGO, goodix_debug_version_show, NULL);
static DEVICE_ATTR(resolution, S_IRUGO, goodix_debug_resolution_show, NULL);
static DEVICE_ATTR(diffdata, S_IRUGO, goodix_debug_diffdata_show, NULL);
static DEVICE_ATTR(calibration, S_IWUSR, NULL, goodix_debug_calibration_store);

/*******************************************************
Description:
	Goodix debug sysfs init function.

Parameter:
	none.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int goodix_debug_sysfs_init(void)
{
	int ret;
	struct goodix_ts_data *ts;
	ts = i2c_get_clientdata(i2c_connect_client);

	goodix_debug_kobj = kobject_create_and_add("goodix_debug", NULL);
	if (goodix_debug_kobj == NULL) {
		printk(KERN_ERR "%s: subsystem_register failed\n", __func__);
		ret = -ENOMEM;
		return ret;
	}
	ret = sysfs_create_file(goodix_debug_kobj, &dev_attr_version.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_version_file failed\n",
		       __func__);
		return ret;
	}
	ret = sysfs_create_file(goodix_debug_kobj, &dev_attr_calibration.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_calibration_file failed\n",
		       __func__);
		return ret;
	}
	ret = sysfs_create_file(goodix_debug_kobj, &dev_attr_diffdata.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_diffdata_file failed\n",
		       __func__);
		return ret;
	}
	ret = sysfs_create_file(goodix_debug_kobj, &dev_attr_resolution.attr);
	if (ret) {
		printk(KERN_ERR "%s: sysfs_create_resolution_file failed\n",
		       __func__);
		return ret;
	}
	dev_info(&ts->client->dev, "Goodix debug sysfs create success!\n");
	return 0;
}

static void goodix_debug_sysfs_deinit(void)
{
	sysfs_remove_file(goodix_debug_kobj, &dev_attr_version.attr);
	sysfs_remove_file(goodix_debug_kobj, &dev_attr_resolution.attr);
	sysfs_remove_file(goodix_debug_kobj, &dev_attr_diffdata.attr);
	sysfs_remove_file(goodix_debug_kobj, &dev_attr_calibration.attr);
	kobject_del(goodix_debug_kobj);
}

static int goodix_ts_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	if (ts->use_irq)
		disable_irq(client->irq);
	else
		hrtimer_cancel(&ts->timer);
	//ret = cancel_work_sync(&ts->work);
	//if(ret && ts->use_irq)
	//enable_irq(client->irq);
//      if (ts->power) {
//              ret = ts->power(ts, 0);
//              if (ret < 0)
//                      printk(KERN_ERR "goodix_ts_resume power off failed\n");
//      }
	return 0;
}

static int goodix_ts_resume(struct i2c_client *client)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

//      if (ts->power) {
//              ret = ts->power(ts, 1);
//              if (ret < 0)
//                      printk(KERN_ERR "goodix_ts_resume power on failed\n");
//      }

	if (ts->use_irq)
		enable_irq(client->irq);
	else
		hrtimer_start(&ts->timer, ktime_set(1, 0), HRTIMER_MODE_REL);

	return 0;
}

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
static int goodix_ts_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	int ret = 0;
	int retry = 0;

	int err;
	struct goodix_ts_data *ts;
	char *version_info = NULL;
	char test_data = 1;

	struct goodix_i2c_rmi_platform_data *pdata;

	//struct goodix_i2c_platform_data *pdata;
	//dev_dbg(&client->dev,"Install touchscreen driver for guitar.\n");
	pr_info
	    ("===============================GT801 Probe===========================\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
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
	i2c_connect_client = client;	//used by Guitar Updating.

	client->irq = ts->gpio_irq;	//ts->gpio_irq = INT_PORT;
	ts->use_irq = 1;

	for (retry = 0; retry < 30; retry++) {
		disable_irq(client->irq);
		sun4i_set_ph21_output();	//gpio_direction_output(INT_PORT, 0);
		msleep(5);
		sun4i_set_ph21_int();	//s3c_gpio_cfgpin(INT_PORT, INT_CFG);
		enable_irq(client->irq);
		ret = i2c_write_bytes(client, &test_data, 1);
		if (ret > 0)
			break;
		printk("GOODiX i2c test failed!\n");
	}
	if (ret <= 0) {
		dev_err(&client->dev,
			"I2C communication ERROR! Goodix touchscreen driver become invalid\n");
		goto err_i2c_failed;
	}

	INIT_WORK(&ts->work, goodix_ts_work_func);
	ts->client = client;
	i2c_set_clientdata(client, ts);
	pdata = client->dev.platform_data;

	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		dev_dbg(&client->dev, "Failed to allocate input device\n");
		goto err_input_dev_alloc_failed;
	}

	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);

	set_bit(ABS_X, ts->input_dev->absbit);
	set_bit(ABS_Y, ts->input_dev->absbit);
	set_bit(ABS_PRESSURE, ts->input_dev->absbit);

	set_bit(BTN_TOUCH, ts->input_dev->keybit);

	input_set_abs_params(ts->input_dev, ABS_X, 0, SCREEN_MAX_HEIGHT, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_Y, 0, SCREEN_MAX_WIDTH, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_PRESSURE, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(ts->input_dev,
			     ABS_MT_PRESSURE, 0, PRESS_MAX, 0, 0);

	set_bit(ABS_MT_TOUCH_MAJOR, ts->input_dev->absbit);
	set_bit(ABS_MT_POSITION_X, ts->input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, ts->input_dev->absbit);

	input_set_abs_params(ts->input_dev,
			     ABS_MT_POSITION_X, 0, SCREEN_MAX_HEIGHT, 0, 0);
	input_set_abs_params(ts->input_dev,
			     ABS_MT_POSITION_Y, 0, SCREEN_MAX_WIDTH, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 1, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, 4, 0, 0);

	sprintf(ts->phys, "input/goodix-ts");
	ts->input_dev->name = f3x_ts_name;
	ts->input_dev->phys = ts->phys;
	ts->input_dev->id.bustype = BUS_I2C;
	ts->input_dev->id.vendor = 0xDEAD;
	ts->input_dev->id.product = 0xBEEF;
	ts->input_dev->id.version = 0x1105;

	ret = input_register_device(ts->input_dev);
	if (ret) {
		dev_err(&client->dev, "Unable to register %s input device\n",
			ts->input_dev->name);
		goto err_input_register_device_failed;
	}
	ts->bad_data = 0;

	goodix_wq = create_singlethread_workqueue("goodix_wq");
	if (!goodix_wq) {
		printk(KERN_ALERT "Creat %s workqueue failed.\n", f3x_ts_name);
		return -ENOMEM;

	}
	flush_workqueue(goodix_wq);
	ts->power = goodix_ts_power;
	msleep(30);

	for (retry = 0; retry < 3; retry++) {
		ret = goodix_init_panel(ts);
		msleep(2);
		if (ret != 0)
			continue;
		else
			break;
	}
	if (ret != 0) {
		ts->bad_data = 1;
		goto err_init_godix_ts;
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	printk("==register_early_suspend =\n");
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = goodix_ts_early_suspend;
	ts->early_suspend.resume = goodix_ts_late_resume;
	register_early_suspend(&ts->early_suspend);
#endif

	err = ctp_ops.set_irq_mode("ctp_para", "ctp_int_port", CTP_IRQ_MODE);
	if (0 != err) {
		printk("%s:ctp_ops.set_irq_mode err.\n", __func__);
		goto exit_set_irq_mode;
	}

	err =
	    request_irq(SW_INT_IRQNO_PIO, goodix_ts_irq_handler,
			IRQF_TRIGGER_RISING | IRQF_SHARED, client->name, ts);
	if (err < 0) {
		pr_info("goodix_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}

	pr_info("Read Goodix version\n");
	ret = goodix_read_version(ts, &version_info);
	if (ret <= 0) {
		printk("Read version data failed!\n");
	} else {
		printk("Goodix TouchScreen Version:%s\n", (version_info + 1));
	}
	vfree(version_info);
	//msleep(260);

#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
	goodix_proc_entry = create_proc_entry("goodix-update", 0666, NULL);
	if (goodix_proc_entry == NULL) {
		dev_info(&client->dev, "Couldn't create proc entry!\n");
		ret = -ENOMEM;
		goto err_create_proc_entry;
	} else {
		dev_info(&client->dev, "Create proc entry success!\n");
		goodix_proc_entry->write_proc = goodix_update_write;
		goodix_proc_entry->read_proc = goodix_update_read;
		goodix_proc_entry->mode = THIS_MODULE;
	}
#endif
	goodix_debug_sysfs_init();

	dev_dbg(&client->dev, "Start  %s in %s mode\n",
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
#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
err_create_proc_entry:
#endif
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
	dev_notice(&client->dev, "The driver is removing...\n");

	goodix_debug_sysfs_deinit();
	sun4i_ph21_free();

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

#ifdef CONFIG_TOUCHSCREEN_GOODIX_IAP
/**
@brief CRC cal proc,include : Reflect,init_crc32_table,GenerateCRC32
@param global var oldcrc32
@return states
*/
static unsigned int Reflect(unsigned long int ref, char ch)
{
	unsigned int value = 0;
	int i;
	for (i = 1; i < (ch + 1); i++) {
		if (ref & 1)
			value |= 1 << (ch - i);
		ref >>= 1;
	}
	return value;
}

/*---------------------------------------------------------------------------------------------------------*/
/*  CRC Check Program INIT								                                           		   */
/*---------------------------------------------------------------------------------------------------------*/
static void init_crc32_table(void)
{
	unsigned int temp;
	unsigned int t1, t2;
	unsigned int flag;
	int i, j;
	for (i = 0; i <= 0xFF; i++) {
		temp = Reflect(i, 8);
		crc32_table[i] = temp << 24;
		for (j = 0; j < 8; j++) {

			flag = crc32_table[i] & 0x80000000;
			t1 = (crc32_table[i] << 1);
			if (flag == 0)
				t2 = 0;
			else
				t2 = ulPolynomial;
			crc32_table[i] = t1 ^ t2;

		}
		crc32_table[i] = Reflect(crc32_table[i], 32);
	}
}

/*---------------------------------------------------------------------------------------------------------*/
/*  CRC main Program									                                           		   */
/*---------------------------------------------------------------------------------------------------------*/
static void GenerateCRC32(unsigned char *buf, unsigned int len)
{
	unsigned int i;
	unsigned int t;

	for (i = 0; i != len; ++i) {
		t = (oldcrc32 ^ buf[i]) & 0xFF;
		oldcrc32 = ((oldcrc32 >> 8) & 0xFFFFFF) ^ crc32_table[t];
	}
}

static struct file *update_file_open(char *path, mm_segment_t * old_fs_p)
{
	struct file *filp = NULL;
	int errno = -1;

	filp = filp_open(path, O_RDONLY, 0644);

	if (!filp || IS_ERR(filp)) {
		if (!filp)
			errno = -ENOENT;
		else
			errno = PTR_ERR(filp);
		printk(KERN_ERR "The update file for Guitar open error.\n");
		return NULL;
	}
	*old_fs_p = get_fs();
	set_fs(get_ds());

	filp->f_op->llseek(filp, 0, 0);
	return filp;
}

static void update_file_close(struct file *filp, mm_segment_t old_fs)
{
	set_fs(old_fs);
	if (filp)
		filp_close(filp, NULL);
}

static int update_get_flen(char *path)
{
	struct file *file_ck = NULL;
	mm_segment_t old_fs;
	int length;

	file_ck = update_file_open(path, &old_fs);
	if (file_ck == NULL)
		return 0;

	length = file_ck->f_op->llseek(file_ck, 0, SEEK_END);
	//printk("File length: %d\n", length);
	if (length < 0)
		length = 0;
	update_file_close(file_ck, old_fs);
	return length;
}

static int update_file_check(char *path)
{
	unsigned char buffer[64] = { 0 };
	struct file *file_ck = NULL;
	mm_segment_t old_fs;
	int count, ret, length;

	file_ck = update_file_open(path, &old_fs);

	if (path != NULL)
		printk("File Path:%s\n", path);

	if (file_ck == NULL)
		return -ERROR_NO_FILE;

	length = file_ck->f_op->llseek(file_ck, 0, SEEK_END);
#ifdef GUITAR_MESSAGE
	printk("gt801 update: File length: %d\n", length);
#endif
	if (length <= 0 || (length % 4) != 0) {
		update_file_close(file_ck, old_fs);
		return -ERROR_FILE_TYPE;
	}
	//set file point to the begining of the file
	file_ck->f_op->llseek(file_ck, 0, SEEK_SET);
	oldcrc32 = 0xFFFFFFFF;
	init_crc32_table();
	while (length > 0) {
		ret =
		    file_ck->f_op->read(file_ck, buffer, sizeof(buffer),
					&file_ck->f_pos);
		if (ret > 0) {
			for (count = 0; count < ret; count++)
				GenerateCRC32(&buffer[count], 1);
		} else {
			update_file_close(file_ck, old_fs);
			return -ERROR_FILE_READ;
		}
		length -= ret;
	}
	oldcrc32 = ~oldcrc32;
#ifdef GUITAR_MESSAGE
	printk("CRC_Check: %u\n", oldcrc32);
#endif
	update_file_close(file_ck, old_fs);
	return 1;
}

unsigned char wait_slave_ready(struct goodix_ts_data *ts,
			       unsigned short *timeout)
{
	unsigned char i2c_state_buf[2] = { ADDR_STA, UNKNOWN_ERROR };
	int ret;
	while (*timeout < MAX_TIMEOUT) {
		ret = i2c_read_bytes(ts->client, i2c_state_buf, 2);
		if (ret <= 0)
			return ERROR_I2C_TRANSFER;
		if (i2c_state_buf[1] & SLAVE_READY) {
			return i2c_state_buf[1];
			//return 1;
		}
		msleep(10);
		*timeout += 5;
	}
	return 0;
}

static int goodix_update_write(struct file *filp, const char __user * buff,
			       unsigned long len, void *data)
{
	unsigned char cmd[220];
	int ret = -1;

	static unsigned char update_path[100];
	static unsigned short time_count = 0;
	static unsigned int file_len = 0;

	unsigned char i2c_control_buf[2] = { ADDR_CMD, 0 };
	unsigned char i2c_states_buf[2] = { ADDR_STA, 0 };
	unsigned char i2c_data_buf[PACK_SIZE + 1 + 8] = { ADDR_DAT, };
	//unsigned char i2c_rd_buf[1+4+PACK_SIZE+4];
	unsigned char i2c_rd_buf[160];
	unsigned char retries = 0;
	unsigned int rd_len;
	unsigned char i = 0;
	static unsigned char update_need_config = 0;

	unsigned char checksum_error_times = 0;
#ifdef UPDATE_NEW_PROTOCOL
	unsigned int frame_checksum = 0;
	unsigned int frame_number = 0;
#else
	unsigned char send_crc = 0;
#endif

	struct file *file_data = NULL;
	mm_segment_t old_fs;
	struct goodix_ts_data *ts;

	ts = i2c_get_clientdata(i2c_connect_client);
	if (ts == NULL)
		return 0;

	if (copy_from_user(&cmd, buff, len)) {
		return -EFAULT;
	}
	switch (cmd[0]) {
	case STEP_SET_PATH:
		printk("Write cmd is:%d,cmd arg is:%s,write len is:%ld\n",
		       cmd[0], &cmd[1], len);
		memset(update_path, 0, 100);
		strncpy(update_path, cmd + 1, 100);
		if (update_path[0] == 0)
			return 0;
		else
			return 1;
	case STEP_CHECK_FILE:
		printk("Begin to firmware update ......\n");
		ret = update_file_check(update_path);
		if (ret <= 0) {
			printk("fialed to check update file!\n");
			return ret;
		}
		msleep(500);
		printk("Update check file success!\n");
		return 1;
	case STEP_WRITE_SYN:
		printk("STEP1:Write synchronization signal!\n");
		i2c_control_buf[1] = UPDATE_START;
		if (ts->green_wake_mode) {
			//disable_irq(client->irq);
			sun4i_set_ph21_output();	//gpio_direction_output(INT_PORT, 0);
			msleep(5);
			sun4i_set_ph21_int();	//s3c_gpio_cfgpin(INT_PORT, INT_CFG);
			//enable_irq(client->irq);
		}
		ret = i2c_write_bytes(ts->client, i2c_control_buf, 2);
		if (ret <= 0) {
			ret = ERROR_I2C_TRANSFER;
			return ret;
		}
		//the time include time(APROM -> LDROM) and time(LDROM init)
		msleep(1000);
		return 1;
	case STEP_WAIT_SYN:
		printk("STEP2:Wait synchronization signal!\n");
		while (retries < MAX_I2C_RETRIES) {
			i2c_states_buf[1] = UNKNOWN_ERROR;
			ret = i2c_read_bytes(ts->client, i2c_states_buf, 2);
			printk("The read byte is:%d\n", i2c_states_buf[1]);
			if (i2c_states_buf[1] & UPDATE_START) {
				if (i2c_states_buf[1] & NEW_UPDATE_START) {
#ifdef UPDATE_NEW_PROTOCOL
					update_need_config = 1;
					return 2;
#else
					return 1;
#endif
				}
				break;
			}
			msleep(5);
			retries++;
			time_count += 10;
		}
		if ((retries >= MAX_I2C_RETRIES)
		    && (!(i2c_states_buf[1] & UPDATE_START))) {
			if (ret <= 0)
				return 0;
			else
				return -1;
		}
		return 1;
	case STEP_WRITE_LENGTH:
		printk("STEP3:Write total update file length!\n");
		file_len = update_get_flen(update_path);
		if (file_len <= 0) {
			printk("get update file length failed!\n");
			return -1;
		}
		file_len += 4;
		i2c_data_buf[1] = (file_len >> 24) & 0xff;
		i2c_data_buf[2] = (file_len >> 16) & 0xff;
		i2c_data_buf[3] = (file_len >> 8) & 0xff;
		i2c_data_buf[4] = file_len & 0xff;
		file_len -= 4;
		ret = i2c_write_bytes(ts->client, i2c_data_buf, 5);
		if (ret <= 0) {
			ret = ERROR_I2C_TRANSFER;
			return 0;
		}
		return 1;
	case STEP_WAIT_READY:
		printk("STEP4:Wait slave ready!\n");
		ret = wait_slave_ready(ts, &time_count);
		if (ret == ERROR_I2C_TRANSFER)
			return 0;
		if (!ret) {
			return -1;
		}
		printk("Slave ready!\n");
		return 1;
	case STEP_WRITE_DATA:
#ifdef UPDATE_NEW_PROTOCOL
		printk("STEP5:Begin to send file data use NEW protocol!\n");
		file_data = update_file_open(update_path, &old_fs);
		if (file_data == NULL) {
			return -1;
		}
		frame_number = 0;
		while (file_len >= 0) {
			i2c_data_buf[0] = ADDR_DAT;
			rd_len = (file_len >= PACK_SIZE) ? PACK_SIZE : file_len;
			frame_checksum = 0;
			if (file_len) {
				ret =
				    file_data->f_op->read(file_data,
							  i2c_data_buf + 1 + 4,
							  rd_len,
							  &file_data->f_pos);
				if (ret <= 0) {
					printk
					    ("[GOODiX_ISP_NEW]:Read File Data Failed!\n");
					return -1;
				}
				i2c_data_buf[1] = (frame_number >> 24) & 0xff;
				i2c_data_buf[2] = (frame_number >> 16) & 0xff;
				i2c_data_buf[3] = (frame_number >> 8) & 0xff;
				i2c_data_buf[4] = frame_number & 0xff;
				frame_number++;
				frame_checksum = 0;
				for (i = 0; i < rd_len; i++) {
					frame_checksum += i2c_data_buf[5 + i];
				}
				frame_checksum = 0 - frame_checksum;
				i2c_data_buf[5 + rd_len + 0] =
				    frame_checksum & 0xff;
				i2c_data_buf[5 + rd_len + 1] =
				    (frame_checksum >> 8) & 0xff;
				i2c_data_buf[5 + rd_len + 2] =
				    (frame_checksum >> 16) & 0xff;
				i2c_data_buf[5 + rd_len + 3] =
				    (frame_checksum >> 24) & 0xff;
			}
rewrite:
			printk("[GOODiX_ISP_NEW]:%d\n", file_len);
			ret =
			    i2c_write_bytes(ts->client, i2c_data_buf,
					    1 + 4 + rd_len + 4);
			//if(ret <= 0)
			if (ret != 1) {
				printk
				    ("[GOODiX_ISP_NEW]:Write File Data Failed!Return:%d\n",
				     ret);
				return 0;
			}

			memset(i2c_rd_buf, 0x00, 1 + 4 + rd_len + 4);
			ret =
			    i2c_read_bytes(ts->client, i2c_rd_buf,
					   1 + 4 + rd_len + 4);
			if (ret != 2) {
				printk
				    ("[GOODiX_ISP_NEW]:Read File Data Failed!Return:%d\n",
				     ret);
				return 0;
			}
			for (i = 1; i < (1 + 4 + rd_len + 4); i++)	//check communication
			{
				if (i2c_rd_buf[i] != i2c_data_buf[i]) {
					i = 0;
					break;
				}
			}
			if (!i) {
				i2c_control_buf[0] = ADDR_CMD;
				i2c_control_buf[1] = 0x03;
				i2c_write_bytes(ts->client, i2c_control_buf, 2);	//communication error
				printk
				    ("[GOODiX_ISP_NEW]:File Data Frame readback check Error!\n");
			} else {
				i2c_control_buf[1] = 0x04;	//let LDROM write flash
				i2c_write_bytes(ts->client, i2c_control_buf, 2);
			}

			//Wait for slave ready signal.and read the checksum
			ret = wait_slave_ready(ts, &time_count);
			if ((ret & CHECKSUM_ERROR) || (!i)) {
				if (i) {
					printk
					    ("[GOODiX_ISP_NEW]:File Data Frame checksum Error!\n");
				}
				checksum_error_times++;
				msleep(20);
				if (checksum_error_times > 20)	//max retry times.
					return 0;
				goto rewrite;
			}
			checksum_error_times = 0;
			if (ret & (FRAME_ERROR)) {
				printk
				    ("[GOODiX_ISP_NEW]:File Data Frame Miss!\n");
				return 0;
			}
			if (ret == ERROR_I2C_TRANSFER)
				return 0;
			if (!ret) {
				return -1;
			}
			if (file_len < PACK_SIZE) {
				update_file_close(file_data, old_fs);
				break;
			}
			file_len -= rd_len;
		}		//end of while((file_len >= 0))
		return 1;
#else
		printk("STEP5:Begin to send file data use OLD protocol!\n");
		file_data = update_file_open(update_path, &old_fs);
		if (file_data == NULL)	//file_data has been opened at the last time
		{
			return -1;
		}
		while ((file_len >= 0) && (!send_crc)) {
			printk("[GOODiX_ISP_OLD]:%d\n", file_len);
			i2c_data_buf[0] = ADDR_DAT;
			rd_len = (file_len >= PACK_SIZE) ? PACK_SIZE : file_len;
			if (file_len) {
				ret =
				    file_data->f_op->read(file_data,
							  i2c_data_buf + 1,
							  rd_len,
							  &file_data->f_pos);
				if (ret <= 0) {
					return -1;
				}
			}
			if (file_len < PACK_SIZE) {
				send_crc = 1;
				update_file_close(file_data, old_fs);
				i2c_data_buf[file_len + 1] = oldcrc32 & 0xff;
				i2c_data_buf[file_len + 2] =
				    (oldcrc32 >> 8) & 0xff;
				i2c_data_buf[file_len + 3] =
				    (oldcrc32 >> 16) & 0xff;
				i2c_data_buf[file_len + 4] =
				    (oldcrc32 >> 24) & 0xff;
				ret =
				    i2c_write_bytes(ts->client, i2c_data_buf,
						    (file_len + 1 + 4));
				//if(ret <= 0)
				if (ret != 1) {
					printk
					    ("[GOODiX_ISP_OLD]:Write File Data Failed!Return:%d\n",
					     ret);
					return 0;
				}
				break;
			} else {
				ret =
				    i2c_write_bytes(ts->client, i2c_data_buf,
						    PACK_SIZE + 1);
				//if(ret <= 0)
				if (ret != 1) {
					printk
					    ("[GOODiX_ISP_OLD]:Write File Data Failed!Return:%d\n",
					     ret);
					return 0;
				}
			}
			file_len -= rd_len;

			//Wait for slave ready signal.
			ret = wait_slave_ready(ts, &time_count);
			if (ret == ERROR_I2C_TRANSFER)
				return 0;
			if (!ret) {
				return -1;
			}
			//Slave is ready.
		}		//end of while((file_len >= 0) && (!send_crc))
		return 1;
#endif
	case STEP_READ_STATUS:
		printk("STEP6:Read update status!\n");
		while (time_count < MAX_TIMEOUT) {
			ret = i2c_read_bytes(ts->client, i2c_states_buf, 2);
			if (ret <= 0) {
				return 0;
			}
			if (i2c_states_buf[1] & SLAVE_READY) {
				if (!(i2c_states_buf[1] & 0xf0)) {
					printk
					    ("The firmware updating succeed!update state:0x%x\n",
					     i2c_states_buf[1]);
					return 1;
				} else {
					printk
					    ("The firmware updating failed!update state:0x%x\n",
					     i2c_states_buf[1]);
					return 0;

				}
			}
			msleep(1);
			time_count += 5;
		}
		return -1;
	case FUN_CLR_VAL:	//clear the static val
		time_count = 0;
		file_len = 0;
		update_need_config = 0;
		return 1;
	case FUN_CMD:		//functional command
		if (cmd[1] == CMD_DISABLE_TP) {
			printk("Disable TS int!\n");
			g_enter_isp = 1;
			if (ts->use_irq)
				disable_irq(TS_INT);
		} else if (cmd[1] == CMD_ENABLE_TP) {
			printk("Enable TS int!\n");
			g_enter_isp = 0;
			if (ts->use_irq)
				enable_irq(TS_INT);
		} else if (cmd[1] == CMD_READ_VER) {
			printk("Read version!\n");
			ts->read_mode = MODE_RD_VER;
		} else if (cmd[1] == CMD_READ_RAW) {
			printk("Read raw data!\n");
			ts->read_mode = MODE_RD_RAW;
			i2c_control_buf[1] = 201;
			ret = i2c_write_bytes(ts->client, i2c_control_buf, 2);	//read raw data cmd
			if (ret <= 0) {
				printk("Write read raw data cmd failed!\n");
				return 0;
			}
			msleep(200);
		} else if (cmd[1] == CMD_READ_DIF) {
			printk("Read diff data!\n");
			ts->read_mode = MODE_RD_DIF;
			i2c_control_buf[1] = 202;
			ret = i2c_write_bytes(ts->client, i2c_control_buf, 2);	//read diff data cmd
			if (ret <= 0) {
				printk("Write read raw data cmd failed!\n");
				return 0;
			}
			msleep(200);
		} else if (cmd[1] == CMD_READ_CFG) {
			printk("Read config info!\n");
			ts->read_mode = MODE_RD_CFG;
			rd_cfg_addr = cmd[2];
			rd_cfg_len = cmd[3];
		} else if (cmd[1] == CMD_SYS_REBOOT) {
			printk("System reboot!\n");
			//sys_sync();
			msleep(200);
			kernel_restart(NULL);
		}
		return 1;
	case FUN_WRITE_CONFIG:

		printk("Begin write config info!Config length:%d\n", cmd[1]);
		for (i = 3; i < cmd[1]; i++) {
			//if((i-3)%5 == 0)printk("\n");
			printk("(%d):0x%x ", i - 3, cmd[i]);
		}
		printk("\n");

		if ((cmd[2] > 83) && (cmd[2] < 240) && cmd[1]) {
			checksum_error_times = 0;
			if (ts->green_wake_mode)	//WAKEUP GREEN MODE
			{
				if (!update_need_config)
					disable_irq(ts->client->irq);
				sun4i_set_ph21_output();	//gpio_direction_output(INT_PORT, 0);
				msleep(5);
				sun4i_set_ph21_int();	//s3c_gpio_cfgpin(INT_PORT, INT_CFG);
				if (!update_need_config)
					enable_irq(ts->client->irq);
			}
reconfig:
			ret = i2c_write_bytes(ts->client, cmd + 2, cmd[1]);
			if (ret != 1) {
				printk("Write Config failed!return:%d\n", ret);
				return -1;
			}
			if (!update_need_config)
				return 1;

			i2c_rd_buf[0] = cmd[2];
			ret = i2c_read_bytes(ts->client, i2c_rd_buf, cmd[1]);
			if (ret != 2) {
				printk("Read Config failed!return:%d\n", ret);
				return -1;
			}
			for (i = 0; i < cmd[1]; i++) {
				if (i2c_rd_buf[i] != cmd[i + 2]) {
					printk
					    ("Config readback check failed!\n");
					i = 0;
					break;
				}
			}
			if (!i) {
				i2c_control_buf[0] = ADDR_CMD;
				i2c_control_buf[1] = 0x03;
				i2c_write_bytes(ts->client, i2c_control_buf, 2);	//communication error
				checksum_error_times++;
				msleep(20);
				if (checksum_error_times > 20)	//max retry times.
					return 0;
				goto reconfig;
			} else {
				i2c_control_buf[0] = ADDR_CMD;
				i2c_control_buf[1] = 0x04;	//let LDROM write flash
				i2c_write_bytes(ts->client, i2c_control_buf, 2);
				return 1;
			}

		} else {
			printk("Invalid config addr!\n");
			return -1;
		}
	default:
		return -ENOSYS;
	}
	return 0;
}

static int goodix_update_read(char *page, char **start, off_t off, int count,
			      int *eof, void *data)
{
	int ret = -1;
	struct goodix_ts_data *ts;
	int len = 0;
	char *version_info = NULL;
	unsigned char read_data[1201] = { 80, };

	ts = i2c_get_clientdata(i2c_connect_client);
	if (ts == NULL)
		return 0;

	if (ts->read_mode == MODE_RD_VER)	//read version data
	{
		ret = goodix_read_version(ts, &version_info);
		if (ret <= 0) {
			printk("Read version data failed!\n");
			vfree(version_info);
			return 0;
		}

		for (len = 0; len < 100; len++) {
			if (*(version_info + len) == '\0')
				break;
		}
		printk("GOODiX Touchscreen Version is:%s\n",
		       (version_info + 1));
		strncpy(page, version_info + 1, len + 1);
		vfree(version_info);
		*eof = 1;
		return len + 1;
	} else if ((ts->read_mode == MODE_RD_RAW) || (ts->read_mode == MODE_RD_DIF))	//read raw data or diff
	{
		//printk("Read raw data\n");
		ret = i2c_read_bytes(ts->client, read_data, 1201);
		if (ret <= 0) {
			if (ts->read_mode == 2)
				printk("Read raw data failed!\n");
			if (ts->read_mode == 3)
				printk("Read diff data failed!\n");
			return 0;
		}
		memcpy(page, read_data + 1, 1200);
		*eof = 1;
		*start = NULL;
		return 1200;
	} else if (ts->read_mode == MODE_RD_CFG) {
		if ((rd_cfg_addr > 83) && (rd_cfg_addr < 240)) {
			read_data[0] = rd_cfg_addr;
			printk("read config addr is:%d\n", rd_cfg_addr);
		} else {
			read_data[0] = 101;
			printk("invalid read config addr,use default!\n");
		}
		if ((rd_cfg_len < 0) || (rd_cfg_len > 156)) {
			printk("invalid read config length,use default!\n");
			rd_cfg_len = 239 - read_data[0];
		}
		printk("read config length is:%d\n", rd_cfg_len);
		ret = i2c_read_bytes(ts->client, read_data, rd_cfg_len);
		if (ret <= 0) {
			printk("Read config info failed!\n");
			return 0;
		}
		memcpy(page, read_data + 1, rd_cfg_len);
		return rd_cfg_len;
	}
	return len;
}

#endif

//可用于该驱动的 设备名—设备ID 列表
//only one client
static const struct i2c_device_id goodix_ts_id[] = {
	{GOODIX_I2C_NAME, 0},
	{}
};

//设备驱动结构体
static struct i2c_driver goodix_ts_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = goodix_ts_probe,
	.remove = goodix_ts_remove,
#ifdef CONFIG_HAS_EARLYSUSPEND
#else
#ifdef CONFIG_PM
	.suspend = goodix_ts_suspend,
	.resume = goodix_ts_resume,
#endif
#endif
	.id_table = goodix_ts_id,
	.driver = {
		   .name = GOODIX_I2C_NAME,
		   .owner = THIS_MODULE,
		   },
	.address_list = u_i2c_addr.normal_i2c,
};

//驱动加载函数
static int __devinit goodix_ts_init(void)
{
	int ret = -1;
	int err = -1;

	printk("===========================%s=====================\n",
	       __func__);

	if (ctp_ops.fetch_sysconfig_para) {
		if (ctp_ops.fetch_sysconfig_para()) {
			printk("%s: err.\n", __func__);
			return -1;
		}
	}
	printk
	    ("%s: after fetch_sysconfig_para:  normal_i2c: 0x%hx. normal_i2c[1]: 0x%hx \n",
	     __func__, u_i2c_addr.normal_i2c[0], u_i2c_addr.normal_i2c[1]);

	err = ctp_ops.init_platform_resource();
	if (0 != err) {
		printk("%s:ctp_ops.init_platform_resource err.\n", __func__);
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
