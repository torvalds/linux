/*
 * drivers\rtc\rtc-sun5i.c
 * An I2C driver for the Philips PCF8563 RTC
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * huangxin <huangxin@allwinnertech.com>
 *
 * some simple description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <plat/sys_config.h>
#define DRV_VERSION "0.4.3"

/* Control registers */
#define PCF8563_REG_ST1		0x00 /* status */
#define PCF8563_REG_ST2		0x01

/* Datetime registers */
#define PCF8563_REG_SC		0x02 /* datetime */
#define PCF8563_REG_MN		0x03
#define PCF8563_REG_HR		0x04
#define PCF8563_REG_DM		0x05
#define PCF8563_REG_DW		0x06
#define PCF8563_REG_MO		0x07
#define PCF8563_REG_YR		0x08

/* Alarm function registers */
#define PCF8563_REG_AMN		0x09 /* alarm minute */
#define PCF8563_REG_AHR		0x0A /* alarm hour */
#define PCF8563_REG_ADM		0x0B /* alarm day */
#define PCF8563_REG_ADW		0x0C /* alarm week */

#define ALARM_FLAG_BIT      (3)
#define ALARM_INT_BIT       (1)

/* Clock output register */
#define PCF8563_REG_CLKO	0x0D /* clock out */

/* Timer function register */
#define PCF8563_REG_TMRC	0x0E /* timer control */
#define PCF8563_REG_TMR		0x0F /* timer */

#define PCF8563_SC_LV		0x80 /* low voltage */
#define PCF8563_MO_C		0x80 /* century */

#define RTC_NAME	"pcf8563"
//#define F25_ALARM

static struct i2c_driver pcf8563_driver;
static __u32 twi_id = 0;

static struct i2c_client *this_client;

/* Addresses to scan */
static union{
	unsigned short dirty_addr_buf[2];
	const unsigned short normal_i2c[2];
}u_i2c_addr = {{0x00},};

struct pcf8563 {
	struct rtc_device *rtc;
	/*
	 * The meaning of MO_C bit varies by the chip type.
	 * From PCF8563 datasheet: this bit is toggled when the years
	 * register overflows from 99 to 00
	 *   0 indicates the century is 20xx
	 *   1 indicates the century is 19xx
	 * From RTC8564 datasheet: this bit indicates change of
	 * century. When the year digit data overflows from 99 to 00,
	 * this bit is set. By presetting it to 0 while still in the
	 * 20th century, it will be set in year 2000, ...
	 * There seems no reliable way to know how the system use this
	 * bit.  So let's do it heuristically, assuming we are live in
	 * 1970...2069.
	 */
	int c_polarity;	/* 0: MO_C=1 means 19xx, otherwise MO_C=1 means 20xx */
};

/**
 * rtc_fetch_sysconfig_para - get config info from sysconfig.fex file.
 * return value:
 *                    = 0; success;
 *                    < 0; err
 */
static int rtc_fetch_sysconfig_para(void)
{
	int ret = -1;
	int device_used = -1;
	__u32 twi_addr = 0;

	char name[I2C_NAME_SIZE];
	script_parser_value_type_t type = SCRIPT_PARSER_VALUE_TYPE_STRING;

	//__u32 twi_id = 0;

	printk("========RTC Inital ===================\n");
	if(SCRIPT_PARSER_OK != script_parser_fetch("rtc_para", "rtc_used", &device_used, 1)){
	                printk("rtc: script_parser_fetch err. \n");
	                goto script_parser_fetch_err;
	}
	if(1 == device_used){
		if(SCRIPT_PARSER_OK != script_parser_fetch_ex("rtc_para", "rtc_name", (int *)(&name), &type, sizeof(name)/sizeof(int))){
			pr_err("%s: script_parser_fetch err. \n", __func__);
			goto script_parser_fetch_err;
		}
		if(strcmp(RTC_NAME, name)){
			pr_err("%s: name %s does not match HV_NAME. \n", __func__, name);
			return ret;
		}
		if(SCRIPT_PARSER_OK != script_parser_fetch("rtc_para", "rtc_twi_addr", &twi_addr, sizeof(twi_addr)/sizeof(__u32))){
			pr_err("%s: script_parser_fetch err. \n", name);
			goto script_parser_fetch_err;
		}
		u_i2c_addr.dirty_addr_buf[0] = twi_addr;
		u_i2c_addr.dirty_addr_buf[1] = I2C_CLIENT_END;
		printk("%s: after: rtc_twi_addr is 0x%x, dirty_addr_buf: 0x%hx. dirty_addr_buf[1]: 0x%hx \n", \
		__func__, twi_addr, u_i2c_addr.dirty_addr_buf[0], u_i2c_addr.dirty_addr_buf[1]);

		if(SCRIPT_PARSER_OK != script_parser_fetch("rtc_para", "rtc_twi_id", &twi_id, 1)){
			pr_err("%s: script_parser_fetch err. \n", name);
			goto script_parser_fetch_err;
		}
		printk("%s: rtc_twi_id is %d. \n", __func__, twi_id);

	}else{
		pr_err("%s: rtc_unused. \n",  __func__);
		ret = -1;
	}
	printk("%s:ok\n",__func__);
	return 0;

script_parser_fetch_err:
	pr_notice("=========rtc script_parser_fetch_err============\n");
	return ret;
}

/**
 * rtc_detect - Device detection callback for automatic device creation
 * return value:
 *                    = 0; success;
 *                    < 0; err
 */
int rtc_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
printk("%s,line:%d,twi_id:%d,adapter->nr:%d\n", __func__, __LINE__,twi_id,adapter->nr);
	if(twi_id == adapter->nr)
	{
		pr_info("%s: Detected chip %s at adapter %d, address 0x%02x\n",\
			 __func__, RTC_NAME, i2c_adapter_id(adapter), client->addr);
printk("%s,line:%d\n", __func__, __LINE__);
		strlcpy(info->type, RTC_NAME, I2C_NAME_SIZE);
		return 0;
	}else{
		printk("%s,line:%d\n", __func__, __LINE__);
		return -ENODEV;
	}
}

/*
 * In the routines that deal directly with the pcf8563 hardware, we use
 * rtc_time -- month 0-11, hour 0-23, yr = calendar year-epoch.
 * Read clock steps：
 *		step1: Take the device address
 *		step2: The time to read the first byte of address (beginning
 *                     from the second reading)
 *		step3: Read information seven time
 *                     Reading seven time means to read one of (second, minute,
  *                    hour, day of month, day of week, month, year) each time.
 *		step4: Read time and put in the receive buffer
 */
static int pcf8563_get_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct pcf8563 *pcf8563 = i2c_get_clientdata(client);
	unsigned char buf[13] = { PCF8563_REG_ST1 };
	int ret;
	struct i2c_msg msgs[] = {
		{ client->addr, 0, 1, buf },	/* setup read ptr */
		{ client->addr, I2C_M_RD, 13, buf },	/* read status + date */
	};
	ret = i2c_transfer(client->adapter, msgs, 2);
	/* read registers */
	if (ret != 2) {
		dev_err(&client->dev, "%s: read error,ret:%d\n", __func__,ret);
		return -EIO;
	}

	if (buf[PCF8563_REG_SC] & PCF8563_SC_LV)
		dev_info(&client->dev,
			"low voltage detected, date/time is not reliable.\n");
	printk("%s,raw data is st1=%02x, st2=%02x, sec=%02x, min=%02x, hr=%02x, mday=%02x, wday=%02x, mon=%02x, year=%02x\n",\
	 __func__,buf[0], buf[1], buf[2], buf[3],buf[4], buf[5], buf[6], buf[7],buf[8]);

	tm->tm_sec = bcd2bin(buf[PCF8563_REG_SC] & 0x7F);
	tm->tm_min = bcd2bin(buf[PCF8563_REG_MN] & 0x7F);
	tm->tm_hour = bcd2bin(buf[PCF8563_REG_HR] & 0x3F); /* rtc hr 0-23 */
	tm->tm_mday = bcd2bin(buf[PCF8563_REG_DM] & 0x3F);
	tm->tm_wday = buf[PCF8563_REG_DW] & 0x07;
	tm->tm_mon = bcd2bin(buf[PCF8563_REG_MO] & 0x1F) - 1; /* month is 1..12 in RTC but 0..11 in linux*/
	tm->tm_year = bcd2bin(buf[PCF8563_REG_YR]);
	if (tm->tm_year < 70)
		tm->tm_year += 110;	/* assume we are in 2010...2079 */
	/* detect the polarity heuristically. see note above. */
	pcf8563->c_polarity = (buf[PCF8563_REG_MO] & PCF8563_MO_C) ?
		(tm->tm_year >= 100) : (tm->tm_year < 100);

	/*in A13,the mon read from rtc hardware is error? so set the datetime again?*/
	#if 0
	if (tm->tm_mon < 0) {
		tm->tm_mon = 1;
		ret = pcf8563_set_datetime(client, tm);
	}
	#endif

	printk("%s: tm is secs=%d, mins=%d, hours=%d,mday=%d, mon=%d, year=%d, wday=%d\n",\
		__func__,tm->tm_sec, tm->tm_min, tm->tm_hour, tm->tm_mday, tm->tm_mon, tm->tm_year, tm->tm_wday);

	/* the clock can give out invalid datetime, but we cannot return
	 * -EINVAL otherwise hwclock will refuse to set the time on bootup.
	 */
	if (rtc_valid_tm(tm) < 0)
		dev_err(&client->dev, "retrieved date/time is not valid.\n");

	return 0;
}

/*
* Write Clock Steps：
*	step1: A time into the transmit buffer (first address 50H)
*	step2: Take the device address
*	step3: Take the first address written to the register (from 00H to
*              write)
*	step4: Write information seven time and two control commands.
*              Writing seven time means to write one of (second, minute, hour,
*              day of month, day of week, month, year) each time.
*	step5: Write time
*/
static int pcf8563_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct pcf8563 *pcf8563 = i2c_get_clientdata(client);
	int i, err;
	unsigned char buf[9];
	int leap_year = 0;

	/*int tm_year; years from 1900
    *int tm_mon; months since january 0-11
    *the input para tm->tm_year is the offset related 1900;
    */
	leap_year = tm->tm_year + 1900;
	if(leap_year > 2073 || leap_year < 2010) {
		dev_err(&client->dev, "rtc only supports 63（2010～2073） years\n");
		return -EINVAL;
	}
	/*hardware base time:1900, but now set the default start time to 2010*/
	tm->tm_year -= 110;
	/* month is 1..12 in RTC but 0..11 in linux*/
	tm->tm_mon  += 1;

	/*prevent the application seting the error time*/
	if(tm->tm_mon > 12){
		_dev_info(&client->dev, "set time month error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
	       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
	       tm->tm_hour, tm->tm_min, tm->tm_sec);
		switch(tm->tm_mon){
			case 1:
			case 3:
			case 5:
			case 7:
			case 8:
			case 10:
			case 12:
				if(tm->tm_mday > 31){
					_dev_info(&client->dev, "set time day error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
				       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
				       tm->tm_hour, tm->tm_min, tm->tm_sec);
				}
				if((tm->tm_hour > 24)||(tm->tm_min > 59)||(tm->tm_sec > 59)){
						_dev_info(&client->dev, "set time error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
				       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
				       tm->tm_hour, tm->tm_min, tm->tm_sec);
				}
				break;
			case 4:
			case 6:
			case 9:
			case 11:
				if(tm->tm_mday > 30){
					_dev_info(&client->dev, "set time day error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
				       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
				       tm->tm_hour, tm->tm_min, tm->tm_sec);
				}
				if((tm->tm_hour > 24)||(tm->tm_min > 59)||(tm->tm_sec > 59)){
					_dev_info(&client->dev, "set time error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
				       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
				       tm->tm_hour, tm->tm_min, tm->tm_sec);
				}
				break;
			case 2:
				if((leap_year%400==0) || ((leap_year%100!=0) && (leap_year%4==0))) {
					if(tm->tm_mday > 28){
						_dev_info(&client->dev, "set time day error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
				       		tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
				       		tm->tm_hour, tm->tm_min, tm->tm_sec);
					}
					if((tm->tm_hour > 24)||(tm->tm_min > 59)||(tm->tm_sec > 59)){
						_dev_info(&client->dev, "set time error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
					       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
					       tm->tm_hour, tm->tm_min, tm->tm_sec);
					}
				}else{
					if(tm->tm_mday > 29){
						_dev_info(&client->dev, "set time day error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
					       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
					       tm->tm_hour, tm->tm_min, tm->tm_sec);
					}
					if((tm->tm_hour > 24)||(tm->tm_min > 59)||(tm->tm_sec > 59)){
						_dev_info(&client->dev, "set time error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
					       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
					       tm->tm_hour, tm->tm_min, tm->tm_sec);
					}

				}
				break;
			default:
				break;
		}
		/*if the set date error,set the default time:2010:01:01:00:00:00*/
		tm->tm_sec  = 0;
		tm->tm_min  = 0;
		tm->tm_hour = 0;
		tm->tm_mday = 1;
		tm->tm_mon  = 1;
		tm->tm_year = 110;// 2010 = 1900 + 110
	}

	printk("%s: secs=%d, mins=%d, hours=%d, mday=%d, mon=%d, year=%d\n",\
		__func__,tm->tm_sec, tm->tm_min, tm->tm_hour, tm->tm_mday, tm->tm_mon, tm->tm_year);

	/* hours, minutes and seconds */
	buf[PCF8563_REG_SC] = bin2bcd(tm->tm_sec);
	buf[PCF8563_REG_MN] = bin2bcd(tm->tm_min);
	buf[PCF8563_REG_HR] = bin2bcd(tm->tm_hour);

	buf[PCF8563_REG_DM] = bin2bcd(tm->tm_mday);

	/* month, 1 - 12 */
	buf[PCF8563_REG_MO] = bin2bcd(tm->tm_mon);

	/* year and century */
	buf[PCF8563_REG_YR] = bin2bcd(tm->tm_year % 100);
	if (pcf8563->c_polarity ? (tm->tm_year >= 0) : (tm->tm_year < 0))
		buf[PCF8563_REG_MO] |= PCF8563_MO_C;

	//buf[PCF8563_REG_DW] = tm->tm_wday & 0x07;

	/* write register's data */
	for (i = 0; i < 7; i++) {
		unsigned char data[2] = { PCF8563_REG_SC + i,
						buf[PCF8563_REG_SC + i] };

		err = i2c_master_send(client, data, sizeof(data));
		if (err != sizeof(data)) {
			dev_err(&client->dev,
				"%s: err=%d addr=%02x, data=%02x\n",
				__func__, err, data[0], data[1]);
			return -EIO;
		}
	}

	return 0;
}

static int pcf8563_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return pcf8563_get_datetime(to_i2c_client(dev), tm);
}

static int pcf8563_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return pcf8563_set_datetime(to_i2c_client(dev), tm);
}

#ifdef F25_ALARM
int pcf8563_alarm_enable(void)
{
	int ret;
	int err;
	int i;
	unsigned char buf[13];
	struct i2c_msg msgs[] = {
		{ this_client->addr, 0, 1, buf },	/* setup read ptr */
		{ this_client->addr, I2C_M_RD, 13, buf },	/* read status + date */
	};

	ret = i2c_transfer(this_client->adapter, msgs, 2);
	/* read registers */
	if (ret != 2) {
		printk("%s: read error,ret:%d\n", __func__,ret);
		return -EIO;
	}
		/*clear alarm flag and disable alarm interrupt*/
	buf[PCF8563_REG_ST2] &= ~(1<<ALARM_FLAG_BIT);
	buf[PCF8563_REG_ST2] &= (1<<ALARM_INT_BIT);

	/* write register's data */
	for (i = 0; i < 1; i++) {
		unsigned char data[2] = { PCF8563_REG_ST2 + i,
						buf[PCF8563_REG_ST2 + i] };

		err = i2c_master_send(this_client, data, sizeof(data));
		if (err != sizeof(data)) {
			printk("%s: err=%d addr=%02x, data=%02x\n",
				__func__, err, data[0], data[1]);
			return -EIO;
		}
	}

	buf[PCF8563_REG_AMN] = (0<<7);
	buf[PCF8563_REG_AHR] = (0<<7);
	buf[PCF8563_REG_ADM] = (0<<7);
	buf[PCF8563_REG_ADW] = (1<<7);
		/* write register's data */
	for (i = 0; i < 4; i++) {
		unsigned char data[2] = { PCF8563_REG_AMN + i,
						buf[PCF8563_REG_AMN + i] };

		err = i2c_master_send(this_client, data, sizeof(data));
		if (err != sizeof(data)) {
			printk("%s: err=%d addr=%02x, data=%02x\n",
				__func__, err, data[0], data[1]);
			return -EIO;
		}
	}
	return 0;
}

int pcf8563_alarm_disable(void) {
    int ret;
    int err;
    int i;
    unsigned char buf[13];
	struct i2c_msg msgs[] = {
		{ this_client->addr, 0, 1, buf },	/* setup read ptr */
		{ this_client->addr, I2C_M_RD, 13, buf },	/* read status + date */
	};

	ret = i2c_transfer(this_client->adapter, msgs, 2);
	/* read registers */
	if (ret != 2) {
		printk("%s: read error,ret:%d\n", __func__,ret);
		return -EIO;
	}
	/*clear alarm flag and disable alarm interrupt*/
	buf[PCF8563_REG_ST2] &= ~(1<<ALARM_FLAG_BIT);
	buf[PCF8563_REG_ST2] &= ~(1<<ALARM_INT_BIT);

	/* write register's data */
	for (i = 0; i < 1; i++) {
		unsigned char data[2] = { PCF8563_REG_ST2 + i,
						buf[PCF8563_REG_ST2 + i] };

		err = i2c_master_send(this_client, data, sizeof(data));
		if (err != sizeof(data)) {
			printk("%s: err=%d addr=%02x, data=%02x\n",
				__func__, err, data[0], data[1]);
			return -EIO;
		}
	}

	buf[PCF8563_REG_AMN] = (1<<7);
	buf[PCF8563_REG_AHR] = (1<<7);
	buf[PCF8563_REG_ADM] = (1<<7);
	/* write register's data */
	for (i = 0; i < 3; i++) {
		unsigned char data[2] = { PCF8563_REG_AMN + i,
						buf[PCF8563_REG_AMN + i] };

		err = i2c_master_send(this_client, data, sizeof(data));
		if (err != sizeof(data)) {
			printk("%s: err=%d addr=%02x, data=%02x\n",
				__func__, err, data[0], data[1]);
			return -EIO;
		}
	}
	return 0;
}

static irqreturn_t pcf8563_interrupt(int irq, void *id)
{
	int ret;
	ret = pcf8563_alarm_disable();
	if(ret != 0){
		printk("err:%s,%d\n", __func__, __LINE__);
	}
	return IRQ_HANDLED;
}

static int pcf8563_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	unsigned char buf[13] = { PCF8563_REG_ST1 };
	int ret;
	struct rtc_time *alm_tm = &alrm->time;
	struct i2c_msg msgs[] = {
		{ this_client->addr, 0, 1, buf },	/* setup read ptr */
		{ this_client->addr, I2C_M_RD, 13, buf },	/* read status + date */
	};
	ret = i2c_transfer(this_client->adapter, msgs, 2);
	/* read registers */
	if (ret != 2) {
		printk("%s: read error,ret:%d\n", __func__,ret);
		return -EIO;
	}

//	if (buf[PCF8563_REG_SC] & PCF8563_SC_LV)
//		dev_info(&client->dev,
//			"low voltage detected, date/time is not reliable.\n");
	printk("%s,raw data is st1=%02x, st2=%02x, sec=%02x, min=%02x, hr=%02x, mday=%02x, wday=%02x, mon=%02x, year=%02x\n",\
	 __func__,buf[0], buf[1], buf[2], buf[3],buf[4], buf[5], buf[6], buf[7],buf[8]);

	alm_tm->tm_sec = bcd2bin(buf[PCF8563_REG_AMN] & 0x7F);
	alm_tm->tm_hour = bcd2bin(buf[PCF8563_REG_AHR] & 0x3F);
	alm_tm->tm_mday = bcd2bin(buf[PCF8563_REG_ADM] & 0x3F);
	alm_tm->tm_wday = buf[PCF8563_REG_DW] & 0x07;
	alm_tm->tm_mon = bcd2bin(buf[PCF8563_REG_MO] & 0x1F) - 1; /* month is 1..12 in RTC but 0..11 in linux*/
	alm_tm->tm_year = bcd2bin(buf[PCF8563_REG_YR]);
	if (alm_tm->tm_year < 70)
		alm_tm->tm_year += 110;	/* assume we are in 2010...2079 */
	/* detect the polarity heuristically. see note above. */
//	pcf8563->c_polarity = (buf[PCF8563_REG_MO] & PCF8563_MO_C) ?
//		(alm_tm->tm_year >= 100) : (alm_tm->tm_year < 100);

	printk("%s: alm_tm is secs=%d, mins=%d, hours=%d,mday=%d, mon=%d, year=%d, wday=%d\n",\
		__func__,alm_tm->tm_sec, alm_tm->tm_min, alm_tm->tm_hour, alm_tm->tm_mday, alm_tm->tm_mon, alm_tm->tm_year, alm_tm->tm_wday);

	return 0;
}

static int pcf8563_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_time *tm = &alrm->time;
	int i;
    int ret = 0;
    struct rtc_time tm_now;

    unsigned long time_now = 0;
    unsigned long time_set = 0;
    unsigned long time_gap = 0;
    unsigned long time_gap_day = 0;
    unsigned long time_gap_hour = 0;
    unsigned long time_gap_minute = 0;
    unsigned long time_gap_second = 0;
    unsigned char buf[13];

    #ifdef RTC_ALARM_DEBUG
    printk("*****************************\n\n");
    printk("line:%d,%s the alarm time: year:%d, month:%d, day:%d. hour:%d.minute:%d.second:%d\n",\
    __LINE__, __func__, tm->tm_year, tm->tm_mon,\
    	 tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
   	printk("*****************************\n\n");
#endif

    ret = pcf8563_rtc_read_time(dev, &tm_now);

#ifdef RTC_ALARM_DEBUG
    printk("line:%d,%s the current time: year:%d, month:%d, day:%d. hour:%d.minute:%d.second:%d\n",\
    __LINE__, __func__, tm_now.tm_year, tm_now.tm_mon,\
    	 tm_now.tm_mday, tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
   	printk("*****************************\n\n");
#endif

    ret = rtc_tm_to_time(tm, &time_set);
    ret = rtc_tm_to_time(&tm_now, &time_now);
    if (time_set <= time_now) {
    	dev_err(dev, "The time or date can`t set, The day has pass!!!\n");
    	return -EINVAL;
    }

    time_gap = time_set - time_now;
    time_gap_day = time_gap/(3600*24);//day
    time_gap_hour = (time_gap - time_gap_day*24)/3600;//hour
    time_gap_minute = (time_gap - time_gap_day*24*60 - time_gap_hour*60)/60;//minute
    time_gap_second = time_gap - time_gap_day*24*60*60 - time_gap_hour*60*60-time_gap_minute*60;//second

    /* sometimes error adjustment occur in linux kernels, and the values set
       during that time frame can also have error(4 secs), time_gap_second
       can make sure the error is inside the 60 sec range.
       error = difference */
    if (time_gap_second >= 30) {
    	time_gap_minute = time_gap_minute + 1;
    }
    if (time_gap_minute >= 60) {
    	time_gap_hour = time_gap_hour + 1;
    	time_gap_minute = time_gap_minute - 60;
    }
    if (time_gap_hour >= 24) {
    	time_gap_day = time_gap_day + 1;
    	time_gap_hour = time_gap_hour - 24;
    }
    if(time_gap_day > 255) {
    	dev_err(dev, "The time or date can`t set, The day range of 0 to 255\n");
    	return -EINVAL;
    }

#ifdef RTC_ALARM_DEBUG
   	printk("line:%d,%s year:%d, month:%d, day:%ld. hour:%ld.minute:%ld.second:%ld\n",\
    __LINE__, __func__, tm->tm_year, tm->tm_mon,\
    	 time_gap_day, time_gap_hour, time_gap_minute, time_gap_second);
    printk("*****************************\n\n");
#endif

	/*clear the alarm counter enable bit*/
    pcf8563_alarm_disable();
    buf[PCF8563_REG_AMN] = bin2bcd(time_gap_minute);
	buf[PCF8563_REG_AHR] = bin2bcd(time_gap_hour);
	buf[PCF8563_REG_ADM] = bin2bcd(time_gap_day);

    /* write register's data */
	for (i = 0; i < 3; i++) {
		unsigned char data[2] = { PCF8563_REG_AMN + i,
						buf[PCF8563_REG_AMN + i] };

		ret = i2c_master_send(this_client, data, sizeof(data));
		if (ret != sizeof(data)) {
			dev_err(&this_client->dev,
				"%s: err=%d addr=%02x, data=%02x\n",
				__func__, ret, data[0], data[1]);
			return -EIO;
		}
	};

	/* enable or disable alarm */
	if (alrm->enabled) {
		pcf8563_alarm_enable();
    } else {
    	pcf8563_alarm_disable();
	}
	return 0;
}
#endif
static const struct rtc_class_ops pcf8563_rtc_ops = {
	.read_time	= pcf8563_rtc_read_time,
	.set_time	= pcf8563_rtc_set_time,
#ifdef F25_ALARM
	.read_alarm	= pcf8563_read_alarm,
	.set_alarm	= pcf8563_set_alarm,
#endif
};

static int pcf8563_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct pcf8563 *pcf8563;
	int err = 0;

	printk("%s,line:%d\n",__func__, __LINE__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	pcf8563 = kzalloc(sizeof(struct pcf8563), GFP_KERNEL);
	if (!pcf8563)
		return -ENOMEM;

	dev_info(&client->dev, "chip found, driver version " DRV_VERSION "\n");

	this_client = client;
	this_client->addr = client->addr;

	i2c_set_clientdata(client, pcf8563);

	pcf8563->rtc = rtc_device_register(pcf8563_driver.driver.name,
				&client->dev, &pcf8563_rtc_ops, THIS_MODULE);

	if (IS_ERR(pcf8563->rtc)) {
		err = PTR_ERR(pcf8563->rtc);
		goto exit_kfree;
	}
	#ifdef F25_ALARM
	err = request_irq(SW_INT_IRQNO_ENMI, pcf8563_interrupt, IRQF_SHARED, "pcf8563", pcf8563);

	if (err < 0) {
		dev_err(&client->dev, "pcf8563_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}
	#endif
	return 0;
#ifdef F25_ALARM
exit_irq_request_failed:
	free_irq(SW_INT_IRQNO_ENMI, pcf8563);
#endif
exit_kfree:
	kfree(pcf8563);

	return err;
}

static int pcf8563_remove(struct i2c_client *client)
{
	struct pcf8563 *pcf8563 = i2c_get_clientdata(client);

	if (pcf8563->rtc)
		rtc_device_unregister(pcf8563->rtc);

	kfree(pcf8563);

	return 0;
}

static const struct i2c_device_id pcf8563_id[] = {
	{ RTC_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcf8563_id);

static struct i2c_driver pcf8563_driver = {
	.class = I2C_CLASS_HWMON,
	.driver			= {
		.name		= RTC_NAME,
	},
	.probe			= pcf8563_probe,
	.remove			= pcf8563_remove,
	.id_table		= pcf8563_id,
	.address_list	= u_i2c_addr.normal_i2c,
};

static int __init pcf8563_init(void)
{
	if(rtc_fetch_sysconfig_para()){
		printk("%s,line:%d,err\n\n", __func__,__LINE__);
		return -1;
	}

	printk("%s: after fetch_sysconfig_para:  normal_i2c: 0x%hx. normal_i2c[1]: 0x%hx \n", \
	__func__, u_i2c_addr.normal_i2c[0], u_i2c_addr.normal_i2c[1]);

	pcf8563_driver.detect = rtc_detect;

	return i2c_add_driver(&pcf8563_driver);
}

static void __exit pcf8563_exit(void)
{
	i2c_del_driver(&pcf8563_driver);
}

MODULE_AUTHOR("huangxin");
MODULE_DESCRIPTION("allwinner RTC driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

module_init(pcf8563_init);
module_exit(pcf8563_exit);
