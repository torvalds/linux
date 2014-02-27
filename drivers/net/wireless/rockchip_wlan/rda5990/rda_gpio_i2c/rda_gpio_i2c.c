/*
 * =====================================================================================
 *
 *       Filename:  rda_gpio_i2c.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  04/19/2012 11:24:48 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Allen_Hu
 *   Organization: RDA Inc. 
 *
 * =====================================================================================
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>

#include <mach/mt6575_gpio.h>
#include <mach/mtk_rtc.h>

#define RDA5990_WIFI_32K_FLAG	0x00000001
#define RDA5990_BT_32K_FLAG	0x00000002
#define RDA5990_FM_32K_FLAG	0x00000004

//#define DELAY   10
#define DELAY   2
// FM
#define SCL     GPIO149
#define SDA     GPIO150
//
/* WIFI
#define SCL     GPIO104
#define SDA     GPIO102
*/

#define u8		unsigned char

//Data storage mode
static u8 isBigEnded = 0;

//The global variable
static unsigned int  gpioInitialized = 0;
static unsigned int rda5990_32k_state = 0;

#define GPIO_MODE_00 0
#define GPIO_DIR_OUT 1
#define GPIO_DIR_IN  0
#define GPIO_OUT_ONE 1
#define GPIO_OUT_ZERO 0

void set_SDA_output(void)
{
	mt_set_gpio_mode(SDA, GPIO_MODE_00);
	mt_set_gpio_dir(SDA, GPIO_DIR_OUT);
}

void set_SDA_input(void)
{
	mt_set_gpio_mode(SDA, GPIO_MODE_00);
	mt_set_gpio_dir(SDA, GPIO_DIR_IN);
}

void set_SCL_output(void)
{
	mt_set_gpio_mode(SCL, GPIO_MODE_00);
	mt_set_gpio_dir(SCL, GPIO_DIR_OUT);
}

void set_SDA_high(void)
{
	mt_set_gpio_out(SDA, GPIO_OUT_ONE);
}

void set_SDA_low(void)
{
	mt_set_gpio_out(SDA, GPIO_OUT_ZERO);
}

void set_SCL_high(void)
{
	mt_set_gpio_out(SCL, GPIO_OUT_ONE);
}

void set_SCL_low(void)
{
	mt_set_gpio_out(SCL, GPIO_OUT_ZERO);
}

u8 get_SDA_bit(void)
{
	return mt_get_gpio_in(SDA);
}

void i2c_delay(unsigned short cnt)
{
	udelay(cnt);
}

//如果32k不能出来，请修改以下配置使能。
void enable_32k_rtc(void)
{
	printk("enable 32k rtc called\n");
	rtc_gpio_enable_32k(RTC_GPIO_USER_GPS);
	//rtc_gpio_enable_32k(RTC_GPIO_USER_WIFI);
	//rtc_gpio_enable_32k(RTC_GPIO_USER_BT);
	//rtc_gpio_enable_32k(RTC_GPIO_USER_WIFI);
	rtc_gpio_enable_32k(RTC_GPIO_USER_FM);
	msleep(100);
}

void disable_32k_rtc(void)
{
	//rtc_gpio_disable_32k(RTC_GPIO_USER_WIFI);
	rtc_gpio_enable_32k(RTC_GPIO_USER_GPS);
	rtc_gpio_disable_32k(RTC_GPIO_USER_FM);
	msleep(50);
}

void i2c_start(void)  
{    
	set_SDA_output();
	set_SCL_output();

	set_SDA_high();
	i2c_delay(DELAY);  
	set_SCL_high();
	i2c_delay(DELAY);  

	set_SDA_low();
	i2c_delay(DELAY);
	set_SCL_low();
	i2c_delay(DELAY);
}  

void i2c_stop(void)  
{   
	set_SDA_output();
	set_SCL_output();
	set_SDA_low();
	i2c_delay(DELAY);
	set_SCL_high();
	i2c_delay(4*DELAY);
	set_SDA_high();
	i2c_delay(4*DELAY);
}


/* 
 * return value:
 *      0 ---  收到ACK
 *      1 ---  没收到ACK
 */
u8 i2c_send_byte(u8 send_byte)
{
	u8 rc = 0;
	u8 out_mask = 0x80;
	u8 value;
	u8 count = 8;

	set_SDA_output();
	set_SCL_output();

	while(count > 0) {
		set_SCL_low();
		i2c_delay(DELAY);
		value = ((send_byte & out_mask) ? 1 : 0);
		if (value == 1) {
			set_SDA_high();
		}
		else {
			set_SDA_low();
		}
		send_byte <<= 1;
		i2c_delay(DELAY);

		set_SCL_high();
		i2c_delay(DELAY);

		count--;
	}
	set_SCL_low();
	set_SDA_input();
	i2c_delay(4*DELAY);
	set_SCL_high();
	i2c_delay(DELAY);
	rc = get_SDA_bit();
	i2c_delay(DELAY);
	set_SCL_low();

	return rc;
}

/* 
 * ack = 0 发送ACK
 * ack = 1 不发送ACK
 */
void i2c_read_byte(u8 *buffer, u8 ack)
{
	u8 count = 0x08;
	u8 data = 0x00;
	u8 temp = 0;

	set_SCL_output();
	while(count > 0) { 
		set_SCL_low();
		i2c_delay(2*DELAY);
		if(count == 8)
			set_SDA_input();
		i2c_delay(DELAY);
		set_SCL_high();
		i2c_delay(2*DELAY);
		temp = get_SDA_bit();
		data <<= 1;
		if (temp)
			data |= 0x01;

		i2c_delay(DELAY);
		count--;
	} 

	set_SCL_low();
	i2c_delay(2*DELAY);
	set_SDA_output();
	i2c_delay(DELAY);
	if(ack){
		set_SDA_high();
	}else{
		set_SDA_low();
	}
	i2c_delay(DELAY);
	set_SCL_high();
	i2c_delay(2*DELAY);

	*buffer = data;
	set_SCL_low();
}

/*     
*  write data to the I2C bus by GPIO simulated of a digital device rountine. 
* 
*  @param  
*		chipAddr:  address of the device
*		regAddr:   address of register within device
*       data:    the data to be written
*       len:	   the data length
*   
*/
int rda_gpio_i2c_write_1_addr_2_data(u8 chipAddr, u8 regAddr, unsigned short data)
{
	u8 acknowledge;
	int ret = 0;
	i2c_start();

	acknowledge = i2c_send_byte((chipAddr << 1) | 0x00);
	if(acknowledge == 1){
	//	return -1;
		ret = -1;
		goto out;
	}

	acknowledge = i2c_send_byte(regAddr);
	if(acknowledge == 1){
		ret = -1;
		goto out;
	}

	acknowledge = i2c_send_byte(data>>8);
	if(acknowledge == 1){
		ret = -1;
		goto out;
	}
	acknowledge = i2c_send_byte(data);
	ret = acknowledge;

out:
	i2c_stop();

	//return acknowledge;
	return ret;
}
/*     
*  read data from the I2C bus by GPIO simulated of a digital device rountine. 
* 
*  @param  
*		chipAddr:  address of the device
*		regAddr:   address of register within device
*       buffer:    the data to be stored
*       len:	   the data length
*   
*/
int rda_gpio_i2c_read_1_addr_2_data(u8 chipAddr, u8 regAddr, unsigned short *buffer)
{
	u8 tempdata, acknowledge;
	int ret = 0;

	i2c_start();
	acknowledge = i2c_send_byte( (chipAddr << 1) | 0x00 );
	if(acknowledge == 1){
		ret = -1;
		goto out;
	}
	acknowledge = i2c_send_byte(regAddr);
	if(acknowledge == 1){
		ret = -1;
		goto out;
	}

	i2c_start();//restart   
	acknowledge = i2c_send_byte( (chipAddr << 1) | 0x01 );
	if(acknowledge == 1){
		ret = -1;
		goto out;
	}

	i2c_read_byte(&tempdata, 0);
	*buffer = (tempdata<<8);
	i2c_read_byte(&tempdata, 1);
	*buffer |= tempdata;

out:
	i2c_stop();
//	return acknowledge;
	return ret;
}

u8 rda_gpio_i2c_write_4_addr_4_data(u8 chipAddr, unsigned int regAddr, unsigned int data)
{
	u8 acknowledge;
	i2c_start();

	acknowledge = i2c_send_byte((chipAddr << 1) | 0x00);
	acknowledge = i2c_send_byte(regAddr>>24);
	acknowledge = i2c_send_byte(regAddr>>16);
	acknowledge = i2c_send_byte(regAddr>>8);
	acknowledge = i2c_send_byte(regAddr);

	acknowledge = i2c_send_byte((chipAddr << 1) | 0x00);

	acknowledge = i2c_send_byte(data>>24);
	acknowledge = i2c_send_byte(data>>16);
	acknowledge = i2c_send_byte(data>>8);
	acknowledge = i2c_send_byte(data);

	i2c_stop();
	return acknowledge;
}

u8 rda_gpio_i2c_read_4_addr_4_data(u8 chipAddr, unsigned int regAddr, unsigned int *buffer)
{
	u8 tempdata, acknowledge;
	u8 i = 0;

	i2c_start();
	acknowledge = i2c_send_byte( (chipAddr << 1) | 0x00 );
	acknowledge = i2c_send_byte(regAddr>>24);
	acknowledge = i2c_send_byte(regAddr>>16);
	acknowledge = i2c_send_byte(regAddr>>8);
	acknowledge = i2c_send_byte(regAddr);

	i2c_start();//restart   
	acknowledge = i2c_send_byte( (chipAddr << 1) | 0x01 );

	i2c_read_byte(&tempdata, 0);
	*buffer = (tempdata<<24);
	i2c_read_byte(&tempdata, 0);
	*buffer |= (tempdata<<16);
	i2c_read_byte(&tempdata, 0);
	*buffer |= (tempdata<<8);
	i2c_read_byte(&tempdata, 1);
	*buffer |= tempdata;

	i2c_stop();
	return acknowledge;
}

void rda_gpio_i2c_enable_32k(unsigned int flag)
{
	if(rda5990_32k_state == 0 )
	{	
		enable_32k_rtc();
	}
	rda5990_32k_state |= (flag&0x07);
}

void rda_gpio_i2c_disable_32k(unsigned int flag)
{
	rda5990_32k_state &= (~flag);
	if(rda5990_32k_state == 0)
		disable_32k_rtc();
}

/*   
* initializes I2C interface routine. 
* 
* @return value:
*		0--success; 
*		1--error. 
* 
*/
static int __init rda_gpio_i2c_init(void)
{
	if(gpioInitialized == 0){
		printk(KERN_INFO "RDA GPIO control for I2C Driver \n");

		unsigned char *temp = NULL;
		unsigned short testData = 0xaa55;
		temp = (unsigned char *)&testData;
		if(*temp == 0x55){
			isBigEnded = 0;
		}else{
			isBigEnded = 1;
		}


		gpioInitialized = 1;
		rda_gpio_i2c_enable_32k(RDA5990_FM_32K_FLAG);

		/* FM
		unsigned short regValue = 0;
		rda_gpio_i2c_read_1_addr_2_data(0x11, 0x0C, &regValue);
		printk(KERN_ALERT "####[%s, %d], Addr=%02X, value=%04X\n", __func__, __LINE__, 0x0C, regValue);
		*/
		/*WIFI
		unsigned short regValue = 0x0001;
		rda_gpio_i2c_write_1_addr_2_data(0x14, 0x3f, regValue);
		regValue = 0;
		rda_gpio_i2c_read_1_addr_2_data(0x14, 0x3f, &regValue);
		printk(KERN_ALERT "####[%s, %d], Addr=%02X, value=%04X\n", __func__, __LINE__, 0x3f, regValue);
		regValue = 0;
		rda_gpio_i2c_read_1_addr_2_data(0x14, 0x20, &regValue);
		printk(KERN_ALERT "####[%s, %d], Addr=%02X, value=%04X\n", __func__, __LINE__, 0x20, regValue);
		*/

		return 0;
	}else{
		printk("RDA GPIO control for I2C has been initialized.\n");
		return 0;
	}
}

static void __exit rda_gpio_i2c_exit(void)
{
	gpioInitialized = 0;
	rda_gpio_i2c_disable_32k(RDA5990_FM_32K_FLAG);
}

EXPORT_SYMBOL(rda_gpio_i2c_read_1_addr_2_data);
EXPORT_SYMBOL(rda_gpio_i2c_write_1_addr_2_data);
EXPORT_SYMBOL(rda_gpio_i2c_enable_32k);
EXPORT_SYMBOL(rda_gpio_i2c_disable_32k);

EXPORT_SYMBOL(rda_gpio_i2c_read_4_addr_4_data);
EXPORT_SYMBOL(rda_gpio_i2c_write_4_addr_4_data);

module_init(rda_gpio_i2c_init); /*  load the module */
module_exit(rda_gpio_i2c_exit); /*  unload the module */
