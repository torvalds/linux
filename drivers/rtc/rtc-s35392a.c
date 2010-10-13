/*drivers/rtc/rtc-s35392a.h - driver for s35392a
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/i2c.h>
#include <linux/bitrev.h>
#include <linux/bcd.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include <mach/board.h>
#include "rtc-s35392a.h"

#define RTC_RATE	100 * 1000
#define S35392_TEST 0

#if 0
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif

struct s35392a {
	struct i2c_client *client;
	struct rtc_device *rtc;
	int twentyfourhour;
	struct work_struct work;
};



static int s35392a_set_reg(struct s35392a *s35392a, const char reg, char *buf, int len)
{
	struct i2c_client *client = s35392a->client;
	struct i2c_msg msg;
	int ret;
	int i;
	char *buff = buf;
	msg.addr = client->addr | reg;
	msg.flags = client->flags;
	msg.len = len;
	msg.buf = buff;
	msg.scl_rate = RTC_RATE;
	
	ret = i2c_transfer(client->adapter,&msg,1);
	for(i=0;i<len;i++)
		DBG("buf[%d]=0x%x\n",i,buf[i]);
	return ret;	
	
}

static int s35392a_get_reg(struct s35392a *s35392a, const char reg, char *buf, int len)
{
	struct i2c_client *client = s35392a->client;
	struct i2c_msg msg;
	int ret;

	msg.addr = client->addr | reg;
	msg.flags = client->flags | I2C_M_RD;
	msg.len = len;
	msg.buf = buf;
	msg.scl_rate = RTC_RATE;

	ret = i2c_transfer(client->adapter,&msg,1);
	return ret;
	
}

#if S35392_TEST
static int s35392_set_reg(struct s35392a *s35392a, const char reg, char *buf, int len,unsigned char head)
{
	struct i2c_client *client = s35392a->client;
	struct i2c_msg msg;
	int ret;
	
	char *buff = buf;
	msg.addr = client->addr | reg | (head << 3);
	msg.flags = client->flags;
	msg.len = len;
	msg.buf = buff;
	msg.scl_rate = RTC_RATE;	
	ret = i2c_transfer(client->adapter,&msg,1);
	return ret;	
	
}

static int s35392_get_reg(struct s35392a *s35392a, const char reg, char *buf, int len,unsigned char head)
{
	struct i2c_client *client = s35392a->client;
	struct i2c_msg msg;
	int ret;

	msg.addr = client->addr | reg |(head << 3);
	msg.flags = client->flags | I2C_M_RD;
	msg.len = len;
	msg.buf = buf;
	msg.scl_rate = RTC_RATE;

	ret = i2c_transfer(client->adapter,&msg,1);
	return ret;
	
}
static int s35392a_test(struct s35392a *s35392a)
{
	char buf[1];
	char i;
	
	i = 50;
	while(i--)
	{
	if (s35392_get_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf),6) < 0)
		return -EIO;	
	if (!(buf[0] & (S35392A_FLAG_POC | S35392A_FLAG_BLD)))
		return 0;
	
	buf[0] |= (S35392A_FLAG_RESET | S35392A_FLAG_24H);
	buf[0] &= 0xf0;
	s35392_set_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf),6);
	
	buf[0] = 0;
	s35392_get_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf),6);
	mdelay(10);	
	}
	return 0;
}
#endif
static int s35392a_init(struct s35392a *s35392a)
{
	char buf[1];

	s35392a_get_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf));	
	s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, buf, sizeof(buf));	
	s35392a_get_reg(s35392a, S35392A_CMD_INT1, buf, sizeof(buf));
	s35392a_get_reg(s35392a, S35392A_CMD_INT2, buf, sizeof(buf));
	s35392a_get_reg(s35392a, S35392A_CMD_CHECK, buf, sizeof(buf));
	s35392a_get_reg(s35392a, S35392A_CMD_FREE, buf, sizeof(buf));
	
	buf[0] |= (S35392A_FLAG_RESET | S35392A_FLAG_24H);
	buf[0] &= 0xf0;
	return s35392a_set_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf));

}


static char s35392a_hr2reg(struct s35392a *s35392a, int hour)
{
	if (s35392a->twentyfourhour)
		return bin2bcd(hour);

	if (hour < 12)
		return bin2bcd(hour);

	return 0x40 | bin2bcd(hour - 12);
}

static int s35392a_reg2hr(struct s35392a *s35392a, char reg)
{
	unsigned hour;

	if (s35392a->twentyfourhour)
		return bcd2bin(reg & 0x3f);

	hour = bcd2bin(reg & 0x3f);
	if (reg & 0x40)
		hour += 12;

	return hour;
}

static char s35392a_hour2reg(struct s35392a *s35392a, int hour)
{
	if (s35392a->twentyfourhour)
	{
		if(hour<12)
			return 0x80 | bin2bcd(hour) ;
		else
			return 0xc0| bin2bcd(hour) ;
	}		
	else
	{
		if(hour<12)
			return 0x80 | bin2bcd(hour) ;
		else
			return 0xc0 | bin2bcd(hour - 12);
	}
		
}

static int s35392a_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct s35392a	*s35392a = i2c_get_clientdata(client);
	int i, err;
	char buf[7];

	DBG("%s: tm is secs=%d, mins=%d, hours=%d mday=%d, "
		"mon=%d, year=%d, wday=%d\n", __func__, tm->tm_sec,
		tm->tm_min, tm->tm_hour, tm->tm_mday, tm->tm_mon, tm->tm_year,
		tm->tm_wday);

	buf[S35392A_BYTE_YEAR] = bin2bcd(tm->tm_year - 100);
	buf[S35392A_BYTE_MONTH] = bin2bcd(tm->tm_mon + 1);
	buf[S35392A_BYTE_DAY] = bin2bcd(tm->tm_mday);
	buf[S35392A_BYTE_WDAY] = bin2bcd(tm->tm_wday);
	buf[S35392A_BYTE_HOURS] = s35392a_hr2reg(s35392a, tm->tm_hour);
	buf[S35392A_BYTE_MINS] = bin2bcd(tm->tm_min);
	buf[S35392A_BYTE_SECS] = bin2bcd(tm->tm_sec);

	/* This chip expects the bits of each byte to be in reverse order */
	for(i=0;i<7;i++)
		DBG("buf[%d]=0x%x\n",i,buf[i]);
	for (i = 0; i < 7; ++i)
		buf[i] = bitrev8(buf[i]);
	for(i=0;i<7;i++)
		DBG("buf[%d]=0x%x\n",i,buf[i]);
	err = s35392a_set_reg(s35392a, S35392A_CMD_TIME1, buf, sizeof(buf));

	return err;
}

static int s35392a_get_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct s35392a *s35392a = i2c_get_clientdata(client);
	char buf[7];
	int i, err;

	err = s35392a_get_reg(s35392a, S35392A_CMD_TIME1, buf, sizeof(buf));
	if (err < 0)
		return err;
	for(i=0;i<7;i++)
		DBG("buf[%d]=0x%x\n",i,buf[i]);
	/* This chip returns the bits of each byte in reverse order */
	for (i = 0; i < 7; ++i)
		buf[i] = bitrev8(buf[i]);
	for(i=0;i<7;i++)
		DBG("buf[%d]=0x%x\n",i,buf[i]);
	tm->tm_sec = bcd2bin(buf[S35392A_BYTE_SECS]);
	tm->tm_min = bcd2bin(buf[S35392A_BYTE_MINS]);
	tm->tm_hour = s35392a_reg2hr(s35392a, buf[S35392A_BYTE_HOURS]);
	tm->tm_wday = bcd2bin(buf[S35392A_BYTE_WDAY]);
	tm->tm_mday = bcd2bin(buf[S35392A_BYTE_DAY]);
	tm->tm_mon = bcd2bin(buf[S35392A_BYTE_MONTH]) - 1;
	tm->tm_year = bcd2bin(buf[S35392A_BYTE_YEAR]) + 100;
	//tm->tm_yday = rtc_year_days(tm->tm_mday, tm->tm_mon, tm->tm_year);	

	DBG( "%s: tm is secs=%d, mins=%d, hours=%d, mday=%d, "
		"mon=%d, year=%d, wday=%d\n", __func__, tm->tm_sec,
		tm->tm_min, tm->tm_hour, tm->tm_mday, tm->tm_mon, tm->tm_year,
		tm->tm_wday);

	return rtc_valid_tm(tm);
}
static int s35392a_i2c_read_alarm(struct i2c_client *client, struct rtc_wkalrm  *tm)
{
	struct s35392a	*s35392a = i2c_get_clientdata(client);
	char buf[3];
	int i,err;
	char data;
	DBG("%s:%d\n",__FUNCTION__,__LINE__);	
	err = s35392a_get_reg(s35392a, S35392A_CMD_INT2, buf, sizeof(buf));
   	if(err < 0)
        	return err;
	for(i=0;i<3;i++)
		DBG("buf[%d]=0x%x\n",i,buf[i]);
   	 for(i = 0;i < 3;++i)
       	 buf[i] = bitrev8(buf[i]);
	for(i=0;i<3;i++)
		DBG("buf[%d]=0x%x\n",i,buf[i]);
   	tm->time.tm_wday = -1;   
    	tm->time.tm_hour = -1;
	tm->time.tm_min = -1;    	

	if(buf[S35392A_ALARM_WDAYS] & S35392A_ALARM_ENABLE )
		tm->time.tm_wday = bcd2bin(buf[S35392A_ALARM_WDAYS] & S35392A_ALARM_DISABLE) ;
	
	if(buf[S35392A_ALARM_HOURS] & S35392A_ALARM_ENABLE)
		tm->time.tm_hour = s35392a_reg2hr(s35392a, buf[S35392A_ALARM_HOURS]);
	
	if(buf[S35392A_ALARM_MINS] & S35392A_ALARM_ENABLE)
		tm->time.tm_min =  bcd2bin(buf[S35392A_ALARM_MINS] & S35392A_ALARM_DISABLE) ;
	

	tm->time.tm_year = -1;
	tm->time.tm_mon = -1;
	tm->time.tm_mday = -1;
	tm->time.tm_yday = -1;
	tm->time.tm_sec = -1;
	
	DBG( "%s: tm is secs=%d, mins=%d, hours=%d, mday=%d, "
		"mon=%d, year=%d, wday=%d\n", __func__, tm->time.tm_sec,
		tm->time.tm_min, tm->time.tm_hour, tm->time.tm_mday, tm->time.tm_mon, tm->time.tm_year,
		tm->time.tm_wday);
	s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
	tm->enabled = ((data & S35392A_MASK_INT2) == S35392A_INT2_ENABLE);
	s35392a_get_reg(s35392a, S35392A_CMD_STATUS1, &data, 1);
	tm->pending = !!(data & S35392A_FLAG_INT2); 
	
	DBG("%s:%d\n",__FUNCTION__,__LINE__);
	return data;
}

static int s35392a_i2c_set_alarm(struct i2c_client *client, struct rtc_wkalrm  *tm)
{
	struct s35392a	*s35392a = i2c_get_clientdata(client);
	char buf[3];
	char data;
	int i,err;
	DBG("%s:%d\n",__FUNCTION__,__LINE__);
	DBG( "%s: tm is secs=%d, mins=%d, hours=%d, mday=%d, "
		"mon=%d, year=%d, wday=%d\n", __func__, tm->time.tm_sec,
		tm->time.tm_min, tm->time.tm_hour, tm->time.tm_mday, tm->time.tm_mon, tm->time.tm_year,
		tm->time.tm_wday);
	
	buf[S35392A_ALARM_WDAYS] = tm->time.tm_wday>= 0 ? 
		(bin2bcd(tm->time.tm_wday) |S35392A_ALARM_ENABLE) : 0;	
	buf[S35392A_ALARM_WDAYS]=0;
	buf[S35392A_ALARM_HOURS] =  tm->time.tm_hour >=0 ?
		s35392a_hour2reg(s35392a, tm->time.tm_hour) : 0;
	buf[S35392A_ALARM_MINS] = tm->time.tm_min >= 0?
		bin2bcd(tm->time.tm_min) | S35392A_ALARM_ENABLE:0;	
	for(i=0;i<3;i++)
		DBG("buf[%d]=0x%x\n",i,buf[i]);
	 for(i = 0;i < 3;++i)
       	 buf[i] = bitrev8(buf[i]);
	for(i=0;i<3;i++)
		DBG("buf[%d]=0x%x\n",i,buf[i]);
	 if(tm->enabled)
	 {
        	data = 0x00;
		 s35392a_set_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);	
        	s35392a_set_reg(s35392a, S35392A_CMD_INT2, &data, 1);

		s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
		//data =  (data |S35392A_FLAG_INT2AE) & 0x2;
        	data = 0x02;
		s35392a_set_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);		
		s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
		DBG("data = 0x%x\n",data);
		err = s35392a_set_reg(s35392a, S35392A_CMD_INT2, buf, sizeof(buf));
		return err;
	 }
	 else
	 {
		//s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
		//data &=  ~S35392A_FLAG_INT1AE;
		//s35392a_set_reg( s35392a, S35392A_CMD_STATUS2, &data, 1);
	 }
	 return -1;	
	
}
static int s35392a_i2c_read_alarm0(struct i2c_client *client, struct rtc_wkalrm  *tm)
{
	struct s35392a	*s35392a = i2c_get_clientdata(client);
	char buf[3];
	int i,err;
	char data;
	DBG("%s:%d\n",__FUNCTION__,__LINE__);	
	err = s35392a_get_reg(s35392a, S35392A_CMD_INT1, buf, sizeof(buf));
   	if(err < 0)
        	return err;
	for(i=0;i<3;i++)
		DBG("buf[%d]=0x%x\n",i,buf[i]);
   	 for(i = 0;i < 3;++i)
       	 buf[i] = bitrev8(buf[i]);
	for(i=0;i<3;i++)
		DBG("buf[%d]=0x%x\n",i,buf[i]);
   	tm->time.tm_wday = -1;   
    	tm->time.tm_hour = -1;
	tm->time.tm_min = -1;    	

	if(buf[S35392A_ALARM_WDAYS] & S35392A_ALARM_ENABLE )
		tm->time.tm_wday = bcd2bin(buf[S35392A_ALARM_WDAYS] & S35392A_ALARM_DISABLE) ;
	
	if(buf[S35392A_ALARM_HOURS] & S35392A_ALARM_ENABLE)
		tm->time.tm_hour = s35392a_reg2hr(s35392a, buf[S35392A_ALARM_HOURS]);
	
	if(buf[S35392A_ALARM_MINS] & S35392A_ALARM_ENABLE)
		tm->time.tm_min =  bcd2bin(buf[S35392A_ALARM_MINS] & S35392A_ALARM_DISABLE) ;
	

	tm->time.tm_year = -1;
	tm->time.tm_mon = -1;
	tm->time.tm_mday = -1;
	tm->time.tm_yday = -1;
	tm->time.tm_sec = -1;
	
	DBG( "%s: tm is secs=%d, mins=%d, hours=%d, mday=%d, "
		"mon=%d, year=%d, wday=%d\n", __func__, tm->time.tm_sec,
		tm->time.tm_min, tm->time.tm_hour, tm->time.tm_mday, tm->time.tm_mon, tm->time.tm_year,
		tm->time.tm_wday);
	s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
	tm->enabled = ((data & S35392A_MASK_INT1) == S35392A_INT1_ENABLE);
	s35392a_get_reg(s35392a, S35392A_CMD_STATUS1, &data, 1);
	tm->pending = !!(data & S35392A_FLAG_INT1); 
	
	DBG("%s:%d\n",__FUNCTION__,__LINE__);
	return data;
}

static int s35392a_i2c_set_alarm0(struct i2c_client *client, struct rtc_wkalrm  *tm)
{
	struct s35392a	*s35392a = i2c_get_clientdata(client);
	char buf[3];
	char data;
	int i,err;
	DBG("%s:%d\n",__FUNCTION__,__LINE__);
	DBG( "%s: tm is secs=%d, mins=%d, hours=%d, mday=%d, "
		"mon=%d, year=%d, wday=%d\n", __func__, tm->time.tm_sec,
		tm->time.tm_min, tm->time.tm_hour, tm->time.tm_mday, tm->time.tm_mon, tm->time.tm_year,
		tm->time.tm_wday);
	
	//buf[S35392A_ALARM_WDAYS] = tm->time.tm_wday>= 0 ? 
	//	(bin2bcd(tm->time.tm_wday) |S35392A_ALARM_ENABLE) : 0;	
	buf[S35392A_ALARM_WDAYS]=0;
	buf[S35392A_ALARM_HOURS] =  tm->time.tm_hour >=0 ?
		s35392a_hour2reg(s35392a, tm->time.tm_hour) : 0;
	buf[S35392A_ALARM_MINS] = tm->time.tm_min >= 0?
		bin2bcd(tm->time.tm_min) | S35392A_ALARM_ENABLE:0;	
	for(i=0;i<3;i++)
		DBG("buf[%d]=0x%x\n",i,buf[i]);
	 for(i = 0;i < 3;++i)
       	 buf[i] = bitrev8(buf[i]);
	for(i=0;i<3;i++)
		DBG("buf[%d]=0x%x\n",i,buf[i]);
	 if(tm->enabled)
	 {
        	data = 0x00;
		 s35392a_set_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);	
        	s35392a_set_reg(s35392a, S35392A_CMD_INT2, &data, 1);

		s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
		//ta =  (data |S35392A_FLAG_INT1AE) & 0x20;
        	data = 0x02;
		s35392a_set_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);		
		s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
		DBG("data = 0x%x\n",data);
		err = s35392a_set_reg(s35392a, S35392A_CMD_INT1, buf, sizeof(buf));
		return err;
	 }
	 else
	 {
		//s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
		//data &=  ~S35392A_FLAG_INT1AE;
		//s35392a_set_reg( s35392a, S35392A_CMD_STATUS2, &data, 1);
	 }
	 return -1;	
	
}
static void s35392a_alarm_test(struct i2c_client *client ,struct rtc_time rtc_alarm_rtc_time)
{
	struct rtc_wkalrm rtc_alarm,tm;	
	char data;
	struct s35392a	*s35392a = i2c_get_clientdata(client);
	DBG("%s:%d\n",__FUNCTION__,__LINE__);	
	
	rtc_alarm.time.tm_sec = rtc_alarm_rtc_time.tm_sec;
	rtc_alarm.time.tm_min = (rtc_alarm_rtc_time.tm_min + 1) % 60;
	if((rtc_alarm.time.tm_min + 2)/60 > 0)
		rtc_alarm.time.tm_hour = rtc_alarm_rtc_time.tm_hour+1;
	else	
		rtc_alarm.time.tm_hour = rtc_alarm_rtc_time.tm_hour;
	if(rtc_alarm.time.tm_hour >24)
	rtc_alarm.time.tm_hour =24;
	rtc_alarm.time.tm_mday = rtc_alarm_rtc_time.tm_mday;
	rtc_alarm.time.tm_mon = rtc_alarm_rtc_time.tm_mon;
	rtc_alarm.time.tm_year = rtc_alarm_rtc_time.tm_year;
	rtc_alarm.time.tm_wday = rtc_alarm_rtc_time.tm_wday;
	rtc_alarm.time.tm_yday = rtc_alarm_rtc_time.tm_yday;
	rtc_alarm.enabled = 1;	
	DBG("set alarm  - rtc %02d:%02d:%02d %02d/%02d/%04d week=%02d\n",
				rtc_alarm.time.tm_hour, rtc_alarm.time.tm_min,
				rtc_alarm.time.tm_sec, rtc_alarm.time.tm_mon + 1,
				rtc_alarm.time.tm_mday, rtc_alarm.time.tm_year + 1900,rtc_alarm.time.tm_wday);
	s35392a_i2c_set_alarm(client,&rtc_alarm);	
	data = s35392a_i2c_read_alarm(client,&tm);
	DBG("set alarm  - rtc %02d:%02d:%02d %02d/%02d/%04d week=%02d\n",
				tm.time.tm_hour, tm.time.tm_min,
				tm.time.tm_sec, tm.time.tm_mon + 1,
				tm.time.tm_mday, tm.time.tm_year + 1900,tm.time.tm_wday);
	
	DBG("------------------first-------------------------0x%0x, 0x%0x\n",data, data&S35392A_FLAG_INT2);
	do
	{	  
        	msleep(10000);
		s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
		DBG("-----------------------------------------------0x%0x\n",data); 
		s35392a_get_reg(s35392a, S35392A_CMD_STATUS1, &data, 1);
        	DBG("-----------------------------------------------0x%0x\n",data);
    }while((data & S35392A_FLAG_INT2) == 0);
	
    msleep(20000);
    s35392a_get_reg(s35392a, S35392A_CMD_STATUS1, &data, 1);
    DBG("--------------------last-------------------------0x%0x\n",data);
    data=0x00;
    s35392a_set_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
    s35392a_get_reg(s35392a, S35392A_CMD_STATUS1, &data, 1);
}

static void s35392a_alarm_test0(struct i2c_client *client ,struct rtc_time rtc_alarm_rtc_time)
{
	struct rtc_wkalrm rtc_alarm,tm;	
	char data;
	struct s35392a	*s35392a = i2c_get_clientdata(client);
	DBG("%s:%d\n",__FUNCTION__,__LINE__);	
	
	rtc_alarm.time.tm_sec = rtc_alarm_rtc_time.tm_sec;
	rtc_alarm.time.tm_min = (rtc_alarm_rtc_time.tm_min + 3) % 60;
	if((rtc_alarm.time.tm_min + 3)/60 > 0)
		rtc_alarm.time.tm_hour = rtc_alarm_rtc_time.tm_hour+1;			
	else	
		rtc_alarm.time.tm_hour = rtc_alarm_rtc_time.tm_hour;
	if(rtc_alarm.time.tm_hour >24)
	rtc_alarm.time.tm_hour =24;
	rtc_alarm.time.tm_mday = rtc_alarm_rtc_time.tm_mday;
	rtc_alarm.time.tm_mon = rtc_alarm_rtc_time.tm_mon;
	rtc_alarm.time.tm_year = rtc_alarm_rtc_time.tm_year;
	rtc_alarm.time.tm_wday = rtc_alarm_rtc_time.tm_wday;
	rtc_alarm.time.tm_yday = rtc_alarm_rtc_time.tm_yday;
	rtc_alarm.enabled = 1;	
	DBG("set alarm  - rtc %02d:%02d:%02d %02d/%02d/%04d week=%02d\n",
				rtc_alarm.time.tm_hour, rtc_alarm.time.tm_min,
				rtc_alarm.time.tm_sec, rtc_alarm.time.tm_mon + 1,
				rtc_alarm.time.tm_mday, rtc_alarm.time.tm_year + 1900,rtc_alarm.time.tm_wday);
	s35392a_i2c_set_alarm0(client,&rtc_alarm);	
	data = s35392a_i2c_read_alarm0(client,&tm);
	DBG("set alarm  - rtc %02d:%02d:%02d %02d/%02d/%04d week=%02d\n",
				tm.time.tm_hour, tm.time.tm_min,
				tm.time.tm_sec, tm.time.tm_mon + 1,
				tm.time.tm_mday, tm.time.tm_year + 1900,tm.time.tm_wday);
	
	DBG("------------------first-------------------------0x%0x, 0x%0x\n",data, data&S35392A_FLAG_INT1);
	do
	{	  
        	msleep(10000);
		s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
		DBG("-----------------------------------------------0x%0x\n",data); 
		s35392a_get_reg(s35392a, S35392A_CMD_STATUS1, &data, 1);
        	DBG("-----------------------------------------------0x%0x\n",data);
		
    }while((data & S35392A_FLAG_INT1) == 0);
     msleep(10000);
    s35392a_get_reg(s35392a, S35392A_CMD_STATUS1, &data, 1);
    DBG("--------------------last-------------------------0x%0x\n",data);
    data=0x00;
    s35392a_set_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
    s35392a_get_reg(s35392a, S35392A_CMD_STATUS1, &data, 1);
  
}

static int s35392a_set_init_time(struct s35392a *s35392a)
{	
	struct rtc_time *tm;
	struct i2c_client *client = s35392a->client;
	tm->tm_year = 110;
	tm->tm_mon	= 8;
	tm->tm_mday = 8;
	tm->tm_wday	 = 0;
	tm->tm_hour = 8;
	tm->tm_min = 8;
	tm->tm_sec = 8; 
	s35392a_set_datetime(client, tm);
	return 0;
}
static int s35392a_reset(struct s35392a *s35392a)
{
	char buf[1];
	
	if (s35392a_get_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf)) < 0)
		return -EIO;	
	if (!(buf[0] & (S35392A_FLAG_POC | S35392A_FLAG_BLD))) {
        buf[0] = 0x00;
        s35392a_set_reg(s35392a, S35392A_CMD_STATUS2, buf, 1);   
        s35392a_set_reg(s35392a, S35392A_CMD_INT2, buf, 1);
		return 0;
	}

	buf[0] |= (S35392A_FLAG_RESET | S35392A_FLAG_24H);
	buf[0] &= 0xf0;
	s35392a_set_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf));
	
	//s35392a_set_init_time( s35392a);	
	return 0;		
}

static int s35392a_disable_test_mode(struct s35392a *s35392a)
{
	char buf[1];

	if (s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, buf, sizeof(buf)) < 0)
		return -EIO;

	if (!(buf[0] & S35392A_FLAG_TEST))
		return 0;

	buf[0] &= ~S35392A_FLAG_TEST;
	return s35392a_set_reg(s35392a, S35392A_CMD_STATUS2, buf, sizeof(buf));
}
static int s35392a_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return s35392a_get_datetime(to_i2c_client(dev), tm);
}

static int s35392a_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return s35392a_set_datetime(to_i2c_client(dev), tm);
}
static int s35392a_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *tm)
{
	return s35392a_i2c_read_alarm(to_i2c_client(dev),tm);
}
static int s35392a_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *tm)
{	
	return s35392a_i2c_set_alarm(to_i2c_client(dev),tm);	
}

static int s35392a_i2c_open_alarm(struct i2c_client *client )	
{
	u8 data;
	struct s35392a *s35392a = i2c_get_clientdata(client);
	DBG("@@@@@%s:%d@@@@@\n",__FUNCTION__,__LINE__);
	s35392a_get_reg( s35392a, S35392A_CMD_STATUS2, &data, 1);
	data = (data |S35392A_FLAG_INT2AE) & 0x02;
	s35392a_set_reg( s35392a, S35392A_CMD_STATUS2, &data, 1);
	DBG("@@@@@%s:%d@@@@@\n",__FUNCTION__,__LINE__);
	return 0;
}
static int s35392a_i2c_close_alarm(struct i2c_client *client )
{
	u8 data;
	struct s35392a *s35392a = i2c_get_clientdata(client);
	DBG("@@@@@%s:%d@@@@@\n",__FUNCTION__,__LINE__);
	s35392a_get_reg( s35392a, S35392A_CMD_STATUS2, &data, 1);
	data &=  ~S35392A_FLAG_INT2AE;
	s35392a_set_reg( s35392a, S35392A_CMD_STATUS2, &data, 1);
	DBG("@@@@@%s:%d@@@@@\n",__FUNCTION__,__LINE__);
	return 0;
}

static int s35392a_rtc_ioctl(struct device *dev,unsigned int cmd,unsigned long arg)
{
	struct i2c_client *client = to_i2c_client(dev);
	DBG("@@@@@%s:%d@@@@@\n",__FUNCTION__,__LINE__);
	switch(cmd)
	{
		case RTC_AIE_OFF:
			if(s35392a_i2c_close_alarm(client) < 0)
				goto err;
			break;
		case RTC_AIE_ON:
			if(s35392a_i2c_open_alarm(client) < 0)
				goto err;
			break;
		default:
			return -ENOIOCTLCMD;
	}
	return 0;
err:
	return -EIO;
}
static int  s35392a_rtc_proc(struct device *dev, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static const struct rtc_class_ops s35392a_rtc_ops = {
	.read_time	= s35392a_rtc_read_time,
	.set_time	       = s35392a_rtc_set_time,
	.read_alarm    = s35392a_rtc_read_alarm,
	.set_alarm      = s35392a_rtc_set_alarm,
	.ioctl               = s35392a_rtc_ioctl,
	.proc               = s35392a_rtc_proc
};



#if defined(CONFIG_RTC_INTF_SYSFS) || defined(CONFIG_RTC_INTF_SYSFS_MODULE)
static ssize_t  s35392a_sysfs_show_flags(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	DBG("\n@@@@@@@@@@@s35392a_sysfs_show_flags@@@@@@@@@@@@@\n");
	
		return -EIO;

}
static DEVICE_ATTR(flags, S_IRUGO,  s35392a_sysfs_show_flags, NULL);

static ssize_t  s35392a_sysfs_show_sqwfreq(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	DBG("\n@@@@@@@@@@@ s35392a_sysfs_show_sqwfreq@@@@@@@@@@@@@\n");
	struct i2c_client *client = to_i2c_client(dev);
	return 0;
}
static ssize_t s35392a_sysfs_set_sqwfreq(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	DBG("\n@@@@@@@@@@@s35392a_sysfs_set_sqwfreq@@@@@@@@@@@@@\n");
	
	return count;
}
static DEVICE_ATTR(sqwfreq, S_IRUGO | S_IWUSR,
		   s35392a_sysfs_show_sqwfreq, s35392a_sysfs_set_sqwfreq);

static struct attribute *attrs[] = {
	&dev_attr_flags.attr,
	&dev_attr_sqwfreq.attr,
	NULL,
};
static struct attribute_group attr_group = {
	.attrs = attrs,
};
static int  s35392a_sysfs_register(struct device *dev)
{
	DBG("\n@@@@@@@@@@@s35392a_sysfs_register@@@@@@@@@@@@@\n");
	return sysfs_create_group(&dev->kobj, &attr_group);
	
}
#else
static int  s35392a_sysfs_register(struct device *dev)
{
	DBG("\n@@@@@@@@@@@s35392a_sysfs_register@@@@@@@@@@@@@\n");
	return 0;
	
}
#endif	

static struct i2c_driver s35392a_driver;

static void s35392a_work_func(struct work_struct *work)
{
	struct s35392a *s35392a = container_of(work, struct s35392a, work);
	struct i2c_client *client = s35392a->client;
    
	DBG("\n@@@@@@@@@@@rtc_wakeup_irq@@@@@@@@@@@@@\n");
	
	char data = 0x00;
    s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
    data = 0x00;
    s35392a_set_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
    s35392a_get_reg(s35392a, S35392A_CMD_STATUS2, &data, 1);
    
	DBG("\n@@@@@@@@@@@rtc_wakeup_irq@@@@@@@@@@@@@\n");	
		
	enable_irq(client->irq);		
}

static void s35392a_wakeup_irq(int irq, void *dev_id)
{       
	struct s35392a *s35392a = (struct s35392a *)dev_id;

    disable_irq_nosync(irq);
    schedule_work(&s35392a->work);
}

static int s35392a_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct rk2818_rtc_platform_data *pdata = client->dev.platform_data;
	int err;
	unsigned int i;
	struct s35392a *s35392a;
	struct rtc_time tm;
	char buf[1];
	
	DBG("@@@@@%s:%d@@@@@\n",__FUNCTION__,__LINE__);
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		goto exit;
	}

	s35392a = kzalloc(sizeof(struct s35392a), GFP_KERNEL);
	if (!s35392a) {
		err = -ENOMEM;
		goto exit;
	}

	s35392a->client = client;
	i2c_set_clientdata(client, s35392a);
	//mdelay(500);
	//s35392a_init(s35392a);
	err = s35392a_reset(s35392a);	
	if (err < 0) {
		dev_err(&client->dev, "error resetting chip\n");
		goto exit_dummy;
	}
	
	err = s35392a_disable_test_mode(s35392a);
	if (err < 0) {
		dev_err(&client->dev, "error disabling test mode\n");
		goto exit_dummy;
	}

	err = s35392a_get_reg(s35392a, S35392A_CMD_STATUS1, buf, sizeof(buf));
	if (err < 0) {
		dev_err(&client->dev, "error checking 12/24 hour mode\n");
		goto exit_dummy;
	}
	if (buf[0] & S35392A_FLAG_24H)
		s35392a->twentyfourhour = 1;
	else
		s35392a->twentyfourhour = 0;

	if (s35392a_get_datetime(client, &tm) < 0)
		dev_warn(&client->dev, "clock needs to be set\n");
		
	s35392a->rtc = rtc_device_register(s35392a_driver.driver.name,
				&client->dev, &s35392a_rtc_ops, THIS_MODULE);
	
	if (IS_ERR(s35392a->rtc)) {
		err = PTR_ERR(s35392a->rtc);
		goto exit_dummy;
	}
	err = s35392a_sysfs_register(&client->dev);
	if(err)
	{
		dev_err(&client->dev, "error sysfs register\n");
		goto exit_dummy;
	}
	
	if(err = gpio_request(client->irq, "rtc gpio"))
	{
		dev_err(&client->dev, "gpio request fail\n");
		gpio_free(client->irq);
		goto exit_dummy;
	}

	if (pdata && (pdata->irq_type == GPIO_LOW)) {
		gpio_pull_updown(client->irq,GPIOPullUp);

		client->irq = gpio_to_irq(client->irq);

		if(err = request_irq(client->irq, s35392a_wakeup_irq,IRQF_TRIGGER_LOW,NULL,s35392a) <0)	
		{
			DBG("unable to request rtc irq\n");
			goto exit_dummy;
		}	
	}
	else {
		gpio_pull_updown(client->irq,GPIOPullDown);

		client->irq = gpio_to_irq(client->irq);

		if(err = request_irq(client->irq, s35392a_wakeup_irq,IRQF_TRIGGER_HIGH,NULL,s35392a) <0)	
		{
			DBG("unable to request rtc irq\n");
			goto exit_dummy;
		}	
	}
	
	INIT_WORK(&s35392a->work, s35392a_work_func);
	
#if 0 //S35392_TEST
	//i=2;
	while(1)
	{
	 	s35392a_get_datetime(client, &tm);
        	s35392a_alarm_test(client, tm);
		//sleep(200);
	}
#endif
	
	DBG("@@@@@%s:%d@@@@@\n",__FUNCTION__,__LINE__);
	return 0;

exit_dummy:
	rtc_device_unregister(s35392a->rtc);
	kfree(s35392a);
	i2c_set_clientdata(client, NULL);

exit:
	return err;
}

static int s35392a_remove(struct i2c_client *client)
{
	unsigned int i;

	struct s35392a *s35392a = i2c_get_clientdata(client);

		if (s35392a->client)
			i2c_unregister_device(s35392a->client);

	rtc_device_unregister(s35392a->rtc);
	kfree(s35392a);
	i2c_set_clientdata(client, NULL);

	return 0;
}

static const struct i2c_device_id s35392a_id[] = {
	{ "rtc-s35392a", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, s35392a_id);

static struct i2c_driver s35392a_driver = {
	.driver		= {
		.name	= "rtc-s35392a",
	},
	.probe		= s35392a_probe,
	.remove		= s35392a_remove,
	.id_table	= s35392a_id,
};

static int __init s35392a_rtc_init(void)
{
	DBG("@@@@@%s:%d@@@@@\n",__FUNCTION__,__LINE__);
	return i2c_add_driver(&s35392a_driver);
}

static void __exit s35392a_rtc_exit(void)
{
	i2c_del_driver(&s35392a_driver);
}

MODULE_AUTHOR("swj@rock-chips.com>");
MODULE_DESCRIPTION("S35392A RTC driver");
MODULE_LICENSE("GPL");

module_init(s35392a_rtc_init);
module_exit(s35392a_rtc_exit);

