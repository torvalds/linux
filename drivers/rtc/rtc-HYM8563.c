/*drivers/rtc/rtc-HYM8563.c - driver for HYM8563
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
#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include "rtc-HYM8563.h"

#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif
#define RTC_SPEED 	100 * 1000

struct hym8563 {
	int irq_num;
	struct i2c_client *client;
	struct work_struct work;	
};

static int hym8563_i2c_read_regs(struct i2c_client *client, u8 reg, u8 buf[], unsigned len)
{
	int ret; 
	ret = i2c_master_reg8_recv(client, reg, buf, len, RTC_SPEED);
	return ret; 
}

static int hym8563_i2c_set_regs(struct i2c_client *client, u8 reg, u8 const buf[], __u16 len)
{
	int ret; 
	ret = i2c_master_reg8_send(client, reg, buf, (int)len, RTC_SPEED);
	return ret;
}

/*the init of the hym8563 at first time */
static int hym8563_init_device(struct i2c_client *client)	
{
	u8 regs[2];
	int sr;

	regs[0]=0;
	hym8563_i2c_set_regs(client, RTC_CTL1, regs, 1);		
	
	//disable clkout
	regs[0] = 0x80;
	hym8563_i2c_set_regs(client, RTC_CLKOUT, regs, 1);
	/*enable alarm interrupt*/
	hym8563_i2c_read_regs(client, RTC_CTL2, regs, 1);
	regs[0] = 0x0;
	regs[0] |= AIE;
	hym8563_i2c_set_regs(client, RTC_CTL2, regs, 1);
	hym8563_i2c_read_regs(client, RTC_CTL2, regs, 1);

	sr = hym8563_i2c_read_regs(client, RTC_CTL2, regs, 1);
	if(sr<0)
	{
		DBG("----hym8563_init err");
	}
	
	if(regs[0] & (AF|TF))
	{
		regs[0] &= ~(AF|TF);
		hym8563_i2c_set_regs(client, RTC_CTL2, regs, 1);
	}
	return 0;
}

static int hym8563_read_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	u8 i,regs[HYM8563_RTC_SECTION_LEN] = { 0, };
    
    for(i=0;i<HYM8563_RTC_SECTION_LEN;i++){
		hym8563_i2c_read_regs(client, RTC_SEC+i, &regs[i], 1);
	}	
	
	tm->tm_sec = bcd2bin(regs[0x00] & 0x7F);
	tm->tm_min = bcd2bin(regs[0x01] & 0x7F);
	tm->tm_hour = bcd2bin(regs[0x02] & 0x3F);
	tm->tm_mday = bcd2bin(regs[0x03] & 0x3F);
	tm->tm_wday = bcd2bin(regs[0x04] & 0x07);	
	
	tm->tm_mon = bcd2bin(regs[0x05] & 0x1F) ; 
	tm->tm_mon -= 1;			//inorder to cooperate the systerm time
	
	tm->tm_year = bcd2bin(regs[0x06] & 0xFF);
	if(regs[5] & 0x80)
        tm->tm_year += 1900;
    else
        tm->tm_year += 2000;
		
	tm->tm_yday = rtc_year_days(tm->tm_mday, tm->tm_mon, tm->tm_year);	
	tm->tm_year -= 1900;			//inorder to cooperate the systerm time	
	if(tm->tm_year < 0)
		tm->tm_year = 0;	
	tm->tm_isdst = 0;	
	DBG("%s [%d]tm_sec=%d \n",__FUNCTION__,__LINE__,tm->tm_sec);
	DBG("%s [%d]tm_min=%d \n",__FUNCTION__,__LINE__,tm->tm_min);
	DBG("%s [%d]tm_hour=%d \n",__FUNCTION__,__LINE__,tm->tm_hour);
	DBG("%s [%d]tm_mday=%d \n",__FUNCTION__,__LINE__,tm->tm_mday);
	DBG("%s [%d]tm_mon=%d \n",__FUNCTION__,__LINE__,tm->tm_mon);
	DBG("%s [%d]tm_year=%d \n",__FUNCTION__,__LINE__,tm->tm_year);
	return 0;
}

static int hym8563_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return hym8563_read_datetime(to_i2c_client(dev), tm);
}

static int hym8563_set_time(struct i2c_client *client, struct rtc_time *tm)	
{
	u8 regs[HYM8563_RTC_SECTION_LEN] = { 0, };
	u8 mon_day,i;
	u8 ret = 0;
	
	DBG("%s [%d]tm_sec=%d \n",__FUNCTION__,__LINE__,tm->tm_sec);
	DBG("%s [%d]tm_min=%d \n",__FUNCTION__,__LINE__,tm->tm_min);
	DBG("%s [%d]tm_hour=%d \n",__FUNCTION__,__LINE__,tm->tm_hour);
	DBG("%s [%d]tm_mday=%d \n",__FUNCTION__,__LINE__,tm->tm_mday);
	DBG("%s [%d]tm_mon=%d \n",__FUNCTION__,__LINE__,tm->tm_mon);
	DBG("%s [%d]tm_year=%d \n",__FUNCTION__,__LINE__,tm->tm_year);
	mon_day = rtc_month_days((tm->tm_mon), tm->tm_year + 1900);
	
	if(tm->tm_sec >= 60 || tm->tm_sec < 0 )		//set  sec
		regs[0x00] = bin2bcd(0x00);
	else
		regs[0x00] = bin2bcd(tm->tm_sec);
	
	if(tm->tm_min >= 60 || tm->tm_min < 0 )		//set  min	
		regs[0x01] = bin2bcd(0x00);
	else
		regs[0x01] = bin2bcd(tm->tm_min);

	if(tm->tm_hour >= 24 || tm->tm_hour < 0 )		//set  hour
		regs[0x02] = bin2bcd(0x00);
	else
		regs[0x02] = bin2bcd(tm->tm_hour);
	
	if((tm->tm_mday) > mon_day)				//if the input month day is bigger than the biggest day of this month, set the biggest day 
		regs[0x03] = bin2bcd(mon_day);
	else if((tm->tm_mday) > 0)
		regs[0x03] = bin2bcd(tm->tm_mday);
	else if((tm->tm_mday) <= 0)
		regs[0x03] = bin2bcd(0x01);

	if( tm->tm_year >= 200)		// year >= 2100
		regs[0x06] = bin2bcd(99);	//year = 2099
	else if(tm->tm_year >= 100)			// 2000 <= year < 2100
		regs[0x06] = bin2bcd(tm->tm_year - 100);
	else if(tm->tm_year >= 0){				// 1900 <= year < 2000
		regs[0x06] = bin2bcd(tm->tm_year);	
		regs[0x05] |= 0x80;	
	}else{									// year < 1900
		regs[0x06] = bin2bcd(0);	//year = 1900	
		regs[0x05] |= 0x80;	
	}	
	regs[0x04] = bin2bcd(tm->tm_wday);		//set  the  weekday
	regs[0x05] = (regs[0x05] & 0x80)| (bin2bcd(tm->tm_mon + 1) & 0x7F);		//set  the  month
	
	for(i=0;i<HYM8563_RTC_SECTION_LEN;i++){
		ret = hym8563_i2c_set_regs(client, RTC_SEC+i, &regs[i], 1);
	}

	return 0;
}

static int hym8563_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return hym8563_set_time(to_i2c_client(dev), tm);
}

static int hym8563_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *tm)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 regs[4] = { 0, };
	
	hym8563_i2c_read_regs(client, RTC_A_MIN, regs, 4);
	regs[0] = 0x0;
	hym8563_i2c_set_regs(client, RTC_CTL2, regs, 1);
	return 0;
}

static int hym8563_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *tm)
{	
	struct i2c_client *client = to_i2c_client(dev);
	u8 regs[4] = { 0, };
	u8 mon_day;
	
	regs[0] = 0x0;
	hym8563_i2c_set_regs(client, RTC_CTL2, regs, 1);
	mon_day = rtc_month_days((tm->time.tm_mon), tm->time.tm_year + 1900);
	hym8563_i2c_read_regs(client, RTC_A_MIN, regs, 4);
	
	if(tm->time.tm_min >= 60 || tm->time.tm_min < 0 )		//set  min	
		regs[0x00] = bin2bcd(0x00) & 0x7f;
	else
		regs[0x00] = bin2bcd(tm->time.tm_min) & 0x7f;
	if(tm->time.tm_hour >= 24 || tm->time.tm_hour < 0 )		//set  hour
		regs[0x01] = bin2bcd(0x00) & 0x7f;
	else
		regs[0x01] = bin2bcd(tm->time.tm_hour) & 0x7f;
	regs[0x03] = bin2bcd (tm->time.tm_wday) & 0x7f;	
		
	if((tm->time.tm_mday) > mon_day)				//if the input month day is bigger than the biggest day of this month, set the biggest day 
		regs[0x02] = bin2bcd(mon_day) & 0x7f;
	else if((tm->time.tm_mday) > 0)
		regs[0x02] = bin2bcd(tm->time.tm_mday) & 0x7f;
	else if((tm->time.tm_mday) <= 0)
		regs[0x02] = bin2bcd(0x01) & 0x7f;

	hym8563_i2c_set_regs(client, RTC_A_MIN, regs, 4);	
	hym8563_i2c_read_regs(client, RTC_A_MIN, regs, 4);	
	hym8563_i2c_read_regs(client, RTC_CTL2, regs, 1);
	if(tm->enabled == 1)
		regs[0] |= AIE;
	else
		regs[0] &= 0x0;
	hym8563_i2c_set_regs(client, RTC_CTL2, regs, 1);
	hym8563_i2c_read_regs(client, RTC_CTL2, regs, 1);
	return 0;
}

#if defined(CONFIG_RTC_INTF_DEV) || defined(CONFIG_RTC_INTF_DEV_MODULE)
static int hym8563_i2c_open_alarm(struct i2c_client *client)
{
	u8 data;
	hym8563_i2c_read_regs(client, RTC_CTL2, &data, 1);
	data |= AIE;
	hym8563_i2c_set_regs(client, RTC_CTL2, &data, 1);

	return 0;
}

static int hym8563_i2c_close_alarm(struct i2c_client *client)
{
	u8 data;
	hym8563_i2c_read_regs(client, RTC_CTL2, &data, 1);
	data &= ~AIE;
	hym8563_i2c_set_regs(client, RTC_CTL2, &data, 1);

	return 0;
}

static int hym8563_rtc_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct i2c_client *client = to_i2c_client(dev);
	
	switch (cmd) {
	case RTC_AIE_OFF:
		if(hym8563_i2c_close_alarm(client) < 0)
			goto err;
		break;
	case RTC_AIE_ON:
		if(hym8563_i2c_open_alarm(client))
			goto err;
		break;
	default:
		return -ENOIOCTLCMD;
	}	
	return 0;
err:
	return -EIO;
}
#else
#define hym8563_rtc_ioctl NULL
#endif

#if defined(CONFIG_RTC_INTF_PROC) || defined(CONFIG_RTC_INTF_PROC_MODULE)
static int hym8563_rtc_proc(struct device *dev, struct seq_file *seq)
{
	return 0;
}
#else
#define hym8563_rtc_proc NULL
#endif

static irqreturn_t hym8563_wakeup_irq(int irq, void *dev_id)
{	
	struct hym8563 *hym8563 = (struct hym8563 *)dev_id;
	DBG("Enter::%s %d\n",__FUNCTION__,__LINE__);
	disable_irq_nosync(irq);
	schedule_work(&hym8563->work);
	return IRQ_HANDLED;
}
static void hym8563_work_func(struct work_struct *work)
{	
	struct hym8563 *hym8563 = container_of(work, struct hym8563, work);
	struct i2c_client *client = hym8563->client;
	u8 data;
	DBG("Enter::%s %d\n",__FUNCTION__,__LINE__);
	hym8563_i2c_read_regs(client, RTC_CTL2, &data, 1);
	data &= ~AF;
	hym8563_i2c_set_regs(client, RTC_CTL2, &data, 1);
	enable_irq(hym8563->irq_num);
}

static const struct rtc_class_ops hym8563_rtc_ops = {
	.read_time	= hym8563_rtc_read_time,
	.set_time	= hym8563_rtc_set_time,
	.read_alarm = hym8563_rtc_read_alarm,
	.set_alarm	= hym8563_rtc_set_alarm,
	.ioctl 		= hym8563_rtc_ioctl,
	.proc		= hym8563_rtc_proc
};

static int __devinit  hym8563_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	int rc = 0;
	struct hym8563 *hym8563;
	struct rtc_device *rtc = NULL;
	struct rtc_time tm_read, tm = {
	.tm_wday = 4,
	.tm_year = 109,
	.tm_mon = 9,
	.tm_mday = 1,
	.tm_hour = 12, 
	.tm_min = 10,
	.tm_sec = 58
	};	
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;
		
	hym8563 = kzalloc(sizeof(struct hym8563), GFP_KERNEL);
	if (!hym8563) {
		return -ENOMEM;
	}
		
	rtc = rtc_device_register(client->name, &client->dev,
				  &hym8563_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		rc = PTR_ERR(rtc);
		rtc = NULL;
		goto exit;
	}
	hym8563->client = client;
	hym8563_init_device(client);
	hym8563_read_datetime(client, &tm_read);	//read time from hym8563
	
	if(((tm_read.tm_year < 70) | (tm_read.tm_year > 137 )) | (tm_read.tm_mon == -1))	//if the hym8563 haven't initialized
	{
		hym8563_set_time(client, &tm);	//initialize the hym8563 
	}	
	
	if(gpio_request(client->irq, "rtc gpio"))
	{
		dev_err(&client->dev, "gpio request fail\n");
		gpio_free(client->irq);
		goto exit;
	}
	
	hym8563->irq_num = gpio_to_irq(client->irq);
	gpio_pull_updown(client->irq,GPIOPullUp);
	if(request_irq(hym8563->irq_num, hym8563_wakeup_irq,IRQF_TRIGGER_FALLING,NULL,hym8563) <0)	
	{
		printk("unable to request rtc irq\n");
		goto exit;
	}	
	enable_irq_wake(hym8563->irq_num);
	INIT_WORK(&hym8563->work, hym8563_work_func);
	return 0;

exit:
	if (rtc)
		rtc_device_unregister(rtc);
	return rc;
}

static int __devexit hym8563_remove(struct i2c_client *client)
{
	struct rtc_device *rtc = i2c_get_clientdata(client);

	rtc_device_unregister(rtc);

	return 0;
}

static const struct i2c_device_id hym8563_id[] = {
	{ "rtc_hym8563", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hym8563_id);

static struct i2c_driver hym8563_driver = {
	.driver		= {
		.name	= "rtc_hym8563",
		.owner	= THIS_MODULE,
	},
	.probe		= hym8563_probe,
	.remove		= __devexit_p(hym8563_remove),
	.id_table	= hym8563_id,
};

static int __init hym8563_init(void)
{
	return i2c_add_driver(&hym8563_driver);
}

static void __exit hym8563_exit(void)
{
	i2c_del_driver(&hym8563_driver);
}

MODULE_AUTHOR("lhh lhh@rock-chips.com");
MODULE_DESCRIPTION("HYM8563 RTC driver");
MODULE_LICENSE("GPL");

module_init(hym8563_init);
module_exit(hym8563_exit);

