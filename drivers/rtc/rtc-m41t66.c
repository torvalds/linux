/*
 * I2C client/driver for the ST M41T62 family of i2c rtc chips.
 *
 * Author: lhh <lhh@rock-chips.com>
 *Port to rk29 by yxj
 *
 * Based on m41t00.c by Mark A. Greer <mgreer@mvista.com>
 *
 * 2010 (c) rockchip
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/bcd.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/string.h>


#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <linux/wakelock.h>
#include <mach/gpio.h>
#include <mach/iomux.h>




#define M41T62_REG_SSEC	0
#define M41T62_REG_SEC	1
#define M41T62_REG_MIN	2
#define M41T62_REG_HOUR	3
#define M41T62_REG_SQWDAY	4
#define M41T62_REG_DAY	5
#define M41T62_REG_MON	6
#define M41T62_REG_YEAR	7
#define M41T62_REG_ALARM_MON	0xa
#define M41T62_REG_ALARM_DAY	0xb
#define M41T62_REG_ALARM_HOUR	0xc
#define M41T62_REG_ALARM_MIN	0xd
#define M41T62_REG_ALARM_SEC	0xe
#define M41T62_REG_FLAGS	0xf
#define M41T62_REG_SQW	0x13

#define M41T62_REG_SEC_INDEX   			(M41T62_REG_SEC - 1)
#define M41T62_REG_MIN_INDEX   			(M41T62_REG_MIN - 1)
#define M41T62_REG_HOUR_INDEX  			(M41T62_REG_HOUR - 1)
#define M41T62_REG_SQWDAY_INDEX  			(M41T62_REG_SQWDAY - 1)
#define M41T62_REG_DAY_INDEX   			(M41T62_REG_DAY - 1)
#define M41T62_REG_MON_INDEX   			(M41T62_REG_MON - 1)
#define M41T62_REG_YEAR_INDEX  			(M41T62_REG_YEAR - 1)

#define M41T62_DATETIME_REG_SIZE		(M41T62_REG_YEAR )

#define M41T62_REG_ALARM_MON_INDEX      (M41T62_REG_ALARM_MON-0x0a)
#define M41T62_REG_ALARM_DAY_INDEX      (M41T62_REG_ALARM_DAY-0x0a)
#define M41T62_REG_ALARM_HOUR_INDEX      (M41T62_REG_ALARM_HOUR-0x0a)
#define M41T62_REG_ALARM_MIN_INDEX      (M41T62_REG_ALARM_MIN-0x0a)
#define M41T62_REG_ALARM_SEC_INDEX      (M41T62_REG_ALARM_SEC-0x0a)
#define M41T62_REG_FLAGS_INDEX      (M41T62_REG_FLAGS-0x0a)


#define M41T62_ALARM_REG_SIZE	\
	(M41T62_REG_FLAGS + 1 - M41T62_REG_ALARM_MON)
	

#define M41T62_SEC_ST		(1 << 7)	/* ST: Stop Bit */
#define M41T62_ALMON_AFE	(1 << 7)	/* AFE: alarm flag Enable Bit */
#define M41T62_ALMON_SQWE	(1 << 6)	/* SQWE: SQW Enable Bit */
//#define M41T62_ALHOUR_HT	(1 << 6)	/* HT: Halt Update Bit */
#define M41T62_FLAGS_AF		(1 << 6)	/* AF: Alarm Flag Bit */
//#define M41T62_FLAGS_BATT_LOW	(1 << 4)	/* BL: Battery Low Bit */
#define M41T62_WATCHDOG_RB2	(1 << 7)	/* RB: Watchdog resolution */
#define M41T62_WATCHDOG_RB1	(1 << 1)	/* RB: Watchdog resolution */
#define M41T62_WATCHDOG_RB0	(1 << 0)	/* RB: Watchdog resolution */

//#define M41T62_FEATURE_HT	(1 << 0)	/* Halt feature */
//#define M41T62_FEATURE_BL	(1 << 1)	/* Battery low indicator */
//#define M41T62_FEATURE_SQ	(1 << 2)	/* Squarewave feature */
//#define M41T62_FEATURE_WD	(1 << 3)	/* Extra watchdog resolution */
//#define M41T62_FEATURE_SQ_ALT	(1 << 4)	/* RSx bits are in reg 4 */

#define	REPEAT_SEC		5
#define	REPEAT_MIN		4
#define	REPEAT_HOUR	3
#define	REPEAT_DAY		2
#define	REPEAT_MON	1
#define	REPEAT_YEAR	0
#define RTC_SPEED 	100 * 1000



#define DRV_VERSION "0.05"
#define DRV_NAME  "rtc-M41T66"
#if 1
#define DBG   printk//(x...)	printk(KERN_INFO  "rtc-M41T62:" x)
#else
#define DBG(x...)
#endif

//static struct semaphore rtc_sem;//Ryan


struct rock_rtc {
	int irq;
	struct i2c_client *client;
	struct work_struct work;
	struct mutex mutex;
	struct rtc_device *rtc;
	int exiting;
	struct rtc_wkalrm alarm;
	struct wake_lock wake_lock;
};


static int rtc_alarm_repeat_set(int mod)
{
	return 0;
}


static irqreturn_t rtc_wakeup_irq(int irq, void *dev_id)
{	
	struct rock_rtc *rk_rtc = (struct rock_rtc *)dev_id;
	DBG("enter %s\n",__func__);
	disable_irq_nosync(irq);
	schedule_work(&rk_rtc->work);
	return IRQ_HANDLED;
}


static int m41t62_i2c_read_regs(struct i2c_client *client, u8 reg, u8 buf[], unsigned len)
{
	int ret; 
	
	
	ret = i2c_master_reg8_recv(client, reg, buf, len, RTC_SPEED);
	if(ret < 0 )
	{
		printk("%s:rtc m41t62 read reg error\n\n\n",__func__);
	}

	return ret; 
}

static int m41t62_i2c_set_regs(struct i2c_client *client, u8 reg, u8 const buf[], __u16 len)
{
	int ret; 
	ret = i2c_master_reg8_send(client, reg, buf, (int)len, RTC_SPEED);
	if(ret < 0)
	{
		printk("%s error>>>>>\n",__func__);
	}
	return ret;
}

static int m41t62_init_device(struct i2c_client *client)
{
	//DBG("%s\n",__func__);

	u8 alarmbuf[M41T62_ALARM_REG_SIZE];
	u8 sqwdayreg;
	
	//read alarm register current value
	m41t62_i2c_read_regs(client,M41T62_REG_ALARM_MON,alarmbuf,M41T62_ALARM_REG_SIZE);
	

	/*DBG("init alarm mon=0x%x, day=0x%x, hour=0x%x, min=0x%x, sec=0x%x, flags=0x%x\n",
			alarmbuf[M41T62_REG_ALARM_MON_INDEX],
			alarmbuf[M41T62_REG_ALARM_DAY_INDEX],
			alarmbuf[M41T62_REG_ALARM_HOUR_INDEX],
			alarmbuf[M41T62_REG_ALARM_MIN_INDEX],
			alarmbuf[M41T62_REG_ALARM_SEC_INDEX],
			alarmbuf[M41T62_REG_FLAGS_INDEX]);*/

	//clear alarm register
	alarmbuf[M41T62_REG_ALARM_MON_INDEX] &= ~(0x1f | M41T62_ALMON_AFE);
	alarmbuf[M41T62_REG_ALARM_DAY_INDEX] = 0;
	alarmbuf[M41T62_REG_ALARM_HOUR_INDEX] = 0;
	alarmbuf[M41T62_REG_ALARM_MIN_INDEX] = 0;
	alarmbuf[M41T62_REG_ALARM_SEC_INDEX] = 0;
	alarmbuf[M41T62_REG_FLAGS_INDEX] = 0;

	//write alarm register
	m41t62_i2c_set_regs(client,M41T62_REG_ALARM_MON,alarmbuf,M41T62_ALARM_REG_SIZE);

	//set outclk to 32768HZ
	m41t62_i2c_read_regs(client,M41T62_REG_SQWDAY,&sqwdayreg,1);
	sqwdayreg =(sqwdayreg|0x10)&0x1f;
	m41t62_i2c_set_regs(client,M41T62_REG_SQWDAY,&sqwdayreg,1);
	//m41t62_i2c_read_regs(client,M41T62_REG_SQWDAY,&sqwdayreg,1);
	//printk("sqwdayreg:0x%x\n",sqwdayreg);
	#if 0
	sqwdayreg =0;
	m41t62_i2c_read_regs(client,M41T62_REG_FLAGS,&sqwdayreg,1);  //YLZ++
	
	printk("%s:rtc m41t2 flag_reg = 0x%x\n",__func__,sqwdayreg);
     //  if(sqwdayreg & 0x04)
       {

	   	m41t62_i2c_read_regs(client,M41T62_REG_SEC,&sqwdayreg,1);  //YLZ++
		printk("%s:rtc m41t2 sec_reg = 0x%x\n",__func__,sqwdayreg);
       	sqwdayreg |= 0x80; 
       	m41t62_i2c_set_regs(client,M41T62_REG_SEC,&sqwdayreg,1);
		sqwdayreg =0x7f; 
		m41t62_i2c_set_regs(client,M41T62_REG_SEC,&sqwdayreg,1);
		m41t62_i2c_read_regs(client,M41T62_REG_SEC,&sqwdayreg,1);  //YLZ++
		printk("%s:rtc m41t2 sec_reg = 0x%x\n",__func__,sqwdayreg);


       }
	sqwdayreg =0;
	m41t62_i2c_read_regs(client,M41T62_REG_FLAGS,&sqwdayreg,1);  //YLZ++
	printk("%s:rtc m41t2 atfer flag_reg = 0x%x\n",__func__,sqwdayreg);
      #endif

	return 0;
}

static int m41t62_get_datetime(struct i2c_client *client,
			       struct rtc_time *tm)
{

	
	struct rock_rtc *rk_rtc = i2c_get_clientdata(client);	
	u8 datetime[M41T62_DATETIME_REG_SIZE];
	int ret = 0;

	mutex_lock(&rk_rtc->mutex);
	ret = m41t62_i2c_read_regs(client,M41T62_REG_SEC,datetime,M41T62_DATETIME_REG_SIZE);
	if(ret < 0)
	{
		printk("%s:read date time from rtc m41t2 error\n",__func__);
	}
	else
	{
	  ret = 0;
	}
	mutex_unlock(&rk_rtc->mutex);
	/*DBG("-------M41T62_REG_SEC=%x--",datetime[M41T62_REG_SEC_INDEX]);
	DBG("-------M41T62_REG_MIN=%x--",datetime[M41T62_REG_MIN_INDEX]);
	DBG("-------M41T62_REG_HOUR=%x--",datetime[M41T62_REG_HOUR_INDEX]);
	DBG("-------M41T62_REG_SQWDAY=%x--",datetime[M41T62_REG_SQWDAY_INDEX]);
	DBG("-------M41T62_REG_DAY=%x--",datetime[M41T62_REG_DAY_INDEX]);
	DBG("-------M41T62_REG_MON=%x--",datetime[M41T62_REG_MON_INDEX]);
	DBG("-------M41T62_REG_YEAR=%x--",datetime[M41T62_REG_YEAR_INDEX]);*/
	
	tm->tm_sec = bcd2bin(datetime[M41T62_REG_SEC_INDEX]& 0x7f);
	tm->tm_min = bcd2bin(datetime[M41T62_REG_MIN_INDEX] & 0x7f);
	tm->tm_hour = bcd2bin(datetime[M41T62_REG_HOUR_INDEX] & 0x3f);
	tm->tm_mday = bcd2bin(datetime[M41T62_REG_DAY_INDEX] & 0x3f);
	tm->tm_wday = bcd2bin(datetime[M41T62_REG_SQWDAY_INDEX] & 0x07);
	tm->tm_mon = bcd2bin(datetime[M41T62_REG_MON_INDEX] & 0x1f) - 1;
	// assume 20YY not 19YY, and ignore the Century Bit 
	tm->tm_year = bcd2bin(datetime[M41T62_REG_YEAR_INDEX]) + 100;
    DBG("%s>>>>%4d-%02d-%02d>>wday:%d>>%02d:%02d:%02d>>\n",
		__func__,tm->tm_year+1900,tm->tm_mon+1,tm->tm_mday,tm->tm_wday,
		tm->tm_hour,tm->tm_min,tm->tm_sec);
	
	if(tm->tm_year < 100)
	{
		printk(KERN_INFO "%s:the time read from the rtc M41T62 is illegal ,\
			we will use the default time:2010.8.2\n",__func__);
		tm->tm_sec = 1;
		tm->tm_min = 7;
		tm->tm_hour = 7;
		tm->tm_mday = 2;
		tm->tm_wday = 4;
		tm->tm_mon = 7;
		tm->tm_year = 110;
	}

	
	return ret;
}

/* Sets the given date and time to the real time clock. */
static int m41t62_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	
	struct rock_rtc *rk_rtc = i2c_get_clientdata(client);	
	int ret = 0;
	u8 datetime[M41T62_DATETIME_REG_SIZE];
	
	datetime[M41T62_REG_SEC_INDEX] = bin2bcd(tm->tm_sec);
	datetime[M41T62_REG_MIN_INDEX] = bin2bcd(tm->tm_min);
	datetime[M41T62_REG_HOUR_INDEX] =bin2bcd(tm->tm_hour) ;
	datetime[M41T62_REG_SQWDAY_INDEX] =(tm->tm_wday & 0x07) |0x10;
	datetime[M41T62_REG_DAY_INDEX] = bin2bcd(tm->tm_mday);
	datetime[M41T62_REG_MON_INDEX] = bin2bcd(tm->tm_mon + 1) ;
	/* assume 20YY not 19YY */
	datetime[M41T62_REG_YEAR_INDEX] = bin2bcd(tm->tm_year % 100);
	printk(KERN_INFO "%s:set time %4d-%02d-%02d %02d:%02d:%02d to rtc \n",__func__,
		tm->tm_year+1900,tm->tm_mon+1,tm->tm_mday,tm->tm_hour,tm->tm_min,tm->tm_sec);
	
	mutex_lock(&rk_rtc->mutex);
	ret = m41t62_i2c_set_regs(client,M41T62_REG_SEC,datetime, M41T62_DATETIME_REG_SIZE);
	if(ret < 0)
	{
		printk(KERN_INFO "%s:set time to rtc m41t62 error\n",__func__);
	}
	else
	{
		ret = 0;
	}
	
	mutex_unlock(&rk_rtc->mutex);
	
	return ret;
}

static int m41t62_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	//DBG("%s>>>>>>>>>>>\n",__func__);
	return m41t62_get_datetime(to_i2c_client(dev), tm);
}

static int m41t62_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	//DBG("%s\n",__func__);
	return m41t62_set_datetime(to_i2c_client(dev), tm);
}

#if defined(CONFIG_RTC_INTF_DEV) || defined(CONFIG_RTC_INTF_DEV_MODULE)
static int m41t62_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	int rc;
	struct i2c_client *client = to_i2c_client(dev);
	
	DBG("%s>>>>>>>>>%d\n",__func__ ,cmd);
	switch (cmd) {
	case RTC_AIE_OFF:
	case RTC_AIE_ON:
		break;
	default:
		DBG("RTC func m41t62_rtc_ioctl -ENOIOCTLCMD\n");
		return -ENOIOCTLCMD;
	}
	DBG("RTC func m41t62_rtc_ioctl 1\n");
	rc = i2c_smbus_read_byte_data(client, M41T62_REG_ALARM_MON);
	if (rc < 0)
		goto err;
	switch (cmd) {
	case RTC_AIE_OFF:
		rc &= ~M41T62_ALMON_AFE;
		break;
	case RTC_AIE_ON:
		rc |= M41T62_ALMON_AFE;
		break;
	}
	DBG("\n@@@@@@@@@@@RTC func m41t62_rtc_ioctl 2@@@@@@@@@@@@@\n");
	if (i2c_smbus_write_byte_data(client, M41T62_REG_ALARM_MON, rc) < 0)
		goto err;
	DBG("\n@@@@@@@@@@@RTC func m41t62_rtc_ioctl 3@@@@@@@@@@@@@\n");
	return 0;
err:
	return -EIO;
}
#else
#define	m41t62_rtc_ioctl NULL
#endif

static int m41t62_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	//DBG("%s>>>>>>>>>>>>\n",__func__);
	struct i2c_client *client = to_i2c_client(dev);
	struct rock_rtc *rk_rtc = i2c_get_clientdata(client);
	struct rtc_time current_time ;
	u8 alarmbuf[M41T62_ALARM_REG_SIZE];
	int ret = 0;
	

	
	mutex_lock(&rk_rtc->mutex);
	//read the current value of alarm register
	m41t62_i2c_read_regs(client,M41T62_REG_ALARM_MON,alarmbuf, M41T62_ALARM_REG_SIZE);
	mutex_unlock(&rk_rtc->mutex);

	//clear alarm register 
	alarmbuf[M41T62_REG_ALARM_MON_INDEX] &= ~(0x1f | M41T62_ALMON_AFE);
	alarmbuf[M41T62_REG_ALARM_DAY_INDEX] = 0;
	alarmbuf[M41T62_REG_ALARM_HOUR_INDEX] &= ~(0x3f | 0x80);
	alarmbuf[M41T62_REG_ALARM_MIN_INDEX] = 0;
	alarmbuf[M41T62_REG_ALARM_SEC_INDEX] = 0;


	rk_rtc->alarm = *alarm;
	DBG("time write to alarm :%4d-%02d-%02d %02d:%02d:%02d>>enable:%d\n",
			alarm->time.tm_year+1900,
			alarm->time.tm_mon,
			alarm->time.tm_mday,
			alarm->time.tm_hour,
			alarm->time.tm_min,
			alarm->time.tm_sec,
			alarm->enabled);
	//get current time
	m41t62_get_datetime(client,&current_time);
	DBG("current time   :%4d-%02d-%02d %02d:%02d:%02d>>\n",
			current_time.tm_year+1900,
			current_time.tm_mon,
			current_time.tm_mday,
			current_time.tm_hour,
			current_time.tm_min,
			current_time.tm_sec);
   
	/* offset into rtc's regs */
	alarmbuf[M41T62_REG_ALARM_SEC_INDEX] |= alarm->time.tm_sec >= 0 ?bin2bcd(alarm->time.tm_sec) : 0x80;
	alarmbuf[M41T62_REG_ALARM_MIN_INDEX] |= alarm->time.tm_min >= 0 ?bin2bcd(alarm->time.tm_min) : 0x80;
	alarmbuf[M41T62_REG_ALARM_HOUR_INDEX] |= alarm->time.tm_hour >= 0 ?bin2bcd(alarm->time.tm_hour) : 0x80;
	alarmbuf[M41T62_REG_ALARM_DAY_INDEX] |= alarm->time.tm_mday >= 0 ?bin2bcd(alarm->time.tm_mday) : 0x80;
	if (alarm->time.tm_mon >= 0)
		alarmbuf[M41T62_REG_ALARM_MON_INDEX] |= bin2bcd(alarm->time.tm_mon + 1);
	else
		alarmbuf[M41T62_REG_ALARM_DAY_INDEX] |= 0x40;

	//Ryan@...
	//DBG("enable mon day");
	alarmbuf[M41T62_REG_ALARM_MON_INDEX] |= M41T62_ALMON_AFE ;
	alarmbuf[M41T62_REG_ALARM_DAY_INDEX] |= 0xc0;//mon, day repeat
	//reg[M41T62_REG_ALARM_HOUR] |= 0x80;//hour repeat
	//reg[M41T62_REG_ALARM_MIN] |= 0x80;//min repeat
	
	
	

	//write alarm register 
	mutex_lock(&rk_rtc->mutex);
	ret = m41t62_i2c_set_regs(client,M41T62_REG_ALARM_MON,alarmbuf, M41T62_DATETIME_REG_SIZE);
	if(ret < 0)
	{
		printk(KERN_INFO "%s:set rtc m41t62 alarm error\n",__func__);
	}
	else
	{
	 ret  = 0;
	}
	
	
	mutex_unlock(&rk_rtc->mutex);
	return ret;
	
}

static int m41t62_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *t)
{
	//DBG("%s>>>>>>>>>>>>>\n",__func__);
	
	struct i2c_client *client = to_i2c_client(dev);
	struct rock_rtc *rk_rtc = i2c_get_clientdata(client);
	u8 alarmreg[M41T62_ALARM_REG_SIZE ]; // all alarm regs and flags 
	int ret = 0;
	
	
	mutex_lock(&rk_rtc->mutex);
	m41t62_i2c_read_regs(client,M41T62_REG_ALARM_MON,alarmreg, M41T62_ALARM_REG_SIZE);
	mutex_unlock(&rk_rtc->mutex);
	
	//printk("read alarm mon=0x%x, day=0x%x, hour=0x%x, min=0x%x, sec=0x%x, flags=0x%x\n",
	//		reg[M41T62_REG_ALARM_MON],
	//		reg[M41T62_REG_ALARM_DAY],
	//		reg[M41T62_REG_ALARM_HOUR],
	//		reg[M41T62_REG_ALARM_MIN],
	//		reg[M41T62_REG_ALARM_SEC],
	//		reg[M41T62_REG_FLAGS]);	
	t->time.tm_sec = -1;
	t->time.tm_min = -1;
	t->time.tm_hour = -1;
	t->time.tm_mday = -1;
	t->time.tm_mon = -1;
	if (!(alarmreg[M41T62_REG_ALARM_SEC_INDEX] & 0x80))
		t->time.tm_sec = bcd2bin(alarmreg[M41T62_REG_ALARM_SEC_INDEX] & 0x7f);
	if (!(alarmreg[M41T62_REG_ALARM_MIN_INDEX] & 0x80))
		t->time.tm_min = bcd2bin(alarmreg[M41T62_REG_ALARM_MIN_INDEX] & 0x7f);
	if (!(alarmreg[M41T62_REG_ALARM_HOUR_INDEX] & 0x80))
		t->time.tm_hour = bcd2bin(alarmreg[M41T62_REG_ALARM_HOUR_INDEX] & 0x3f);
	if (!(alarmreg[M41T62_REG_ALARM_DAY_INDEX] & 0x80))
		t->time.tm_mday = bcd2bin(alarmreg[M41T62_REG_ALARM_DAY_INDEX] & 0x3f);
	if (!(alarmreg[M41T62_REG_ALARM_DAY_INDEX] & 0x40))
		t->time.tm_mon = bcd2bin(alarmreg[M41T62_REG_ALARM_MON_INDEX] & 0x1f) - 1;
	t->time.tm_year = -1;
	t->time.tm_wday = -1;
	t->time.tm_yday = -1;
	t->time.tm_isdst = -1;
	t->enabled = !!(alarmreg[M41T62_REG_ALARM_MON_INDEX] & M41T62_ALMON_AFE);
	t->pending = !!(alarmreg[M41T62_REG_FLAGS_INDEX] & M41T62_FLAGS_AF);

	
	mutex_lock(&rk_rtc->mutex);
	ret = m41t62_i2c_read_regs(client,M41T62_REG_ALARM_MON,alarmreg, M41T62_ALARM_REG_SIZE );
	if(ret < 0)
	{
		printk(KERN_INFO "%s:read rtc m41t62 alarm error\n",__func__);
	}
	else
	{
		ret = 0;
	}
	mutex_unlock(&rk_rtc->mutex);
	//printk("read alarm2 mon=0x%x, day=0x%x, hour=0x%x, min=0x%x, sec=0x%x, flags=0x%x\n",
	//		reg[M41T62_REG_ALARM_MON],
	//		reg[M41T62_REG_ALARM_DAY],
	//		reg[M41T62_REG_ALARM_HOUR],
	//		reg[M41T62_REG_ALARM_MIN],
	//		reg[M41T62_REG_ALARM_SEC],
	//		reg[M41T62_REG_FLAGS]);	

	
	return ret;
}

static void rockrtc_work_func(struct work_struct *work)
{	
	struct rock_rtc *rk_rtc = container_of(work, struct rock_rtc, work);
	struct i2c_client *client = rk_rtc->client;
	struct rtc_time now;
	u8 flagreg;

	DBG("enter %s\n",__func__);

	mutex_lock(&rk_rtc->mutex);
	m41t62_i2c_read_regs(client,M41T62_REG_FLAGS,&flagreg, 1 );
	flagreg &=~M41T62_FLAGS_AF ;
	m41t62_i2c_set_regs(client,M41T62_REG_FLAGS,&flagreg, 1 );
	mutex_unlock(&rk_rtc->mutex);

	m41t62_get_datetime(client ,&now);
	mutex_lock(&rk_rtc->mutex);
	if (rk_rtc->alarm.enabled && rk_rtc->alarm.time.tm_sec > now.tm_sec)
	{
		long timeout = rk_rtc->alarm.time.tm_sec - now.tm_sec + 1;
		pr_info("stay awake %lds\n", timeout);
		wake_lock_timeout(&rk_rtc->wake_lock, timeout * HZ);
	}
	if (!rk_rtc->exiting)
		enable_irq(rk_rtc->irq);
	mutex_unlock(&rk_rtc->mutex);
}


static struct rtc_class_ops m41t62_rtc_ops = {
	.read_time = m41t62_rtc_read_time,
	.set_time = m41t62_rtc_set_time,
	.read_alarm = m41t62_rtc_read_alarm,
	.set_alarm = m41t62_rtc_set_alarm,
	.ioctl = m41t62_rtc_ioctl,
};

#if defined(CONFIG_RTC_INTF_SYSFS) || defined(CONFIG_RTC_INTF_SYSFS_MODULE)
static ssize_t m41t62_sysfs_show_flags(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int val;

	val = i2c_smbus_read_byte_data(client, M41T62_REG_FLAGS);
	if (val < 0)
		return -EIO;
	return sprintf(buf, "%#x\n", val);
}
static DEVICE_ATTR(flags, S_IRUGO, m41t62_sysfs_show_flags, NULL);

static ssize_t m41t62_sysfs_show_sqwfreq(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int val;

	val = i2c_smbus_read_byte_data(client, M41T62_REG_SQW);
	if (val < 0)
		return -EIO;
	val = (val >> 4) & 0xf;
	switch (val) {
	case 0:
		break;
	case 1:
		val = 32768;
		break;
	default:
		val = 32768 >> val;
	}
	return sprintf(buf, "%d\n", val);
}
static ssize_t m41t62_sysfs_set_sqwfreq(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	int almon, sqw;
	int val = simple_strtoul(buf, NULL, 0);

	if (val) {
		if (!is_power_of_2(val))
			return -EINVAL;
		val = ilog2(val);
		if (val == 15)
			val = 1;
		else if (val < 14)
			val = 15 - val;
		else
			return -EINVAL;
	}
	/* disable SQW, set SQW frequency & re-enable */
	almon = i2c_smbus_read_byte_data(client, M41T62_REG_ALARM_MON);
	if (almon < 0)
		return -EIO;
	sqw = i2c_smbus_read_byte_data(client, M41T62_REG_SQW);
	if (sqw < 0)
		return -EIO;
	sqw = (sqw & 0x0f) | (val << 4);
	if (i2c_smbus_write_byte_data(client, M41T62_REG_ALARM_MON,
				      almon & ~M41T62_ALMON_SQWE) < 0 ||
	    i2c_smbus_write_byte_data(client, M41T62_REG_SQW, sqw) < 0)
		return -EIO;
	if (val && i2c_smbus_write_byte_data(client, M41T62_REG_ALARM_MON,
					     almon | M41T62_ALMON_SQWE) < 0)
		return -EIO;
	return count;
}
static DEVICE_ATTR(sqwfreq, S_IRUGO | S_IWUSR,
		   m41t62_sysfs_show_sqwfreq, m41t62_sysfs_set_sqwfreq);

static struct attribute *attrs[] = {
	&dev_attr_flags.attr,
	&dev_attr_sqwfreq.attr,
	NULL,
};
static struct attribute_group attr_group = {
	.attrs = attrs,
};

static int m41t62_sysfs_register(struct device *dev)
{
	DBG("\n@@@@@@@@@@@m41t62_sysfs_register@@@@@@@@@@@@@\n");
	return sysfs_create_group(&dev->kobj, &attr_group);
}
#else
static int m41t62_sysfs_register(struct device *dev)
{
	DBG("\n@@@@@@@@@@@m41t62_sysfs_register@@@@@@@@@@@@@\n");
	return 0;
}
#endif




/*
 *****************************************************************************
 *
 *	Driver Interface
 *
 *****************************************************************************
 */
static int __devinit m41t62_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	printk("%s>>>>>>>>>>>>>client->flags:%d\n",__func__,client->flags);
	int rc = 0;
	struct rock_rtc *rk_rtc = NULL;
	struct rtc_device *rtc = NULL;
	struct rtc_time tm_read, tm = {
	.tm_year = 111,
	.tm_mon = 2,
	.tm_mday = 7,
	.tm_wday = 7,
	.tm_hour = 12, 
	.tm_min = 1,
	.tm_sec = 8
	};	
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C) ){
		rc = -ENODEV;
		printk("i2c_check_functionality fail\n");
		goto exit;
	}  
	
	rk_rtc = kzalloc(sizeof(struct rock_rtc), GFP_KERNEL);
	if (!rk_rtc) {
		return -ENOMEM;
	}
	
	rtc = rtc_device_register(client->name, &client->dev,
				  &m41t62_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		rc = PTR_ERR(rtc);
		rtc = NULL;
		printk("\nm41t62_probe err3\n");
		goto exit;
	}
	
	rk_rtc->client = client;
	rk_rtc->rtc = rtc;
	
	mutex_init(&rk_rtc->mutex);
	wake_lock_init(&rk_rtc->wake_lock, WAKE_LOCK_SUSPEND, "rtc_m41t62");
	INIT_WORK(&rk_rtc->work, rockrtc_work_func);
	
	i2c_set_clientdata(client, rk_rtc);
	
	rc = m41t62_sysfs_register(&client->dev);
	if (rc)
	{
		printk("\nm41t62_probe err4\n");
		goto exit;
	}

	
	m41t62_init_device(client);//0323
	m41t62_get_datetime(client, &tm_read);	
	if((tm_read.tm_year < 111 ) |(tm_read.tm_year > 120 ) |(tm_read.tm_mon > 11))	
	{
		m41t62_set_datetime(client, &tm);
		printk("%s [%d]run set time \n",__FUNCTION__,__LINE__);  	
	}

	if(gpio_request(client->irq, "rtc gpio"))
	{
		dev_err(&client->dev, "gpio request fail\n");
		gpio_free(client->irq);
		goto exit;
	}
	
	rk_rtc->irq = gpio_to_irq(client->irq);
	gpio_pull_updown(client->irq,GPIOPullUp);
	if (request_irq(rk_rtc->irq, rtc_wakeup_irq, IRQF_TRIGGER_FALLING, client->dev.driver->name, rk_rtc) < 0)
	{
		printk("unable to request rtc irq\n");
		goto exit;
	}	
	enable_irq_wake(rk_rtc->irq);
	
	return 0;
	
exit:
	if (rtc)
		rtc_device_unregister(rtc);
	if (rk_rtc)
		kfree(rk_rtc);
	return rc;

}



static int __devexit m41t62_remove(struct i2c_client *client)
{
	
	struct rock_rtc  *rk_rtc = i2c_get_clientdata(client);
	
		if (rk_rtc->irq > 0) {
			mutex_lock(&rk_rtc->mutex);
			rk_rtc->exiting = 1;
			mutex_unlock(&rk_rtc->mutex);
	
			free_irq(rk_rtc->irq, rk_rtc);
			cancel_work_sync(&rk_rtc->work);
		}
	
		rtc_device_unregister(rk_rtc->rtc);
		wake_lock_destroy(&rk_rtc->wake_lock);
		kfree(rk_rtc);
		rk_rtc = NULL;
	
	return 0;
}

static const struct i2c_device_id m41t62_id[] = {
	{ DRV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, m41t62_id);


static struct i2c_driver m41t62_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= m41t62_probe,
	.remove		= __devexit_p(m41t62_remove),
	.id_table	= m41t62_id,
	
};


static int __init m41t62_rtc_init(void)
{
	int ret;
	
	printk("%s\n",__func__);
	ret = i2c_add_driver(&m41t62_driver);
	printk("%s:return = %d\n",__func__,ret);
	return ret;
}

static void __exit m41t62_rtc_exit(void)
{
	DBG("%s>>>>>>>>>\n",__func__);
	i2c_del_driver(&m41t62_driver);
}

MODULE_AUTHOR("rockchip lhh");
MODULE_DESCRIPTION("ST Microelectronics M41T62 series RTC I2C Client Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(m41t62_rtc_init);
module_exit(m41t62_rtc_exit);
