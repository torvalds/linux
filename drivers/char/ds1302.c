/*!***************************************************************************
*!
*! FILE NAME  : ds1302.c
*!
*! DESCRIPTION: Implements an interface for the DS1302 RTC
*!
*! Functions exported: ds1302_readreg, ds1302_writereg, ds1302_init, get_rtc_status
*!
*! ---------------------------------------------------------------------------
*!
*! (C) Copyright 1999, 2000, 2001  Axis Communications AB, LUND, SWEDEN
*!
*!***************************************************************************/


#include <linux/fs.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/bcd.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/rtc.h>
#if defined(CONFIG_M32R)
#include <asm/m32r.h>
#endif

#define RTC_MAJOR_NR 121 /* local major, change later */

static const char ds1302_name[] = "ds1302";

/* Send 8 bits. */
static void
out_byte_rtc(unsigned int reg_addr, unsigned char x)
{
	//RST H
	outw(0x0001,(unsigned long)PLD_RTCRSTODT);
	//write data
	outw(((x<<8)|(reg_addr&0xff)),(unsigned long)PLD_RTCWRDATA);
	//WE
	outw(0x0002,(unsigned long)PLD_RTCCR);
	//wait
	while(inw((unsigned long)PLD_RTCCR));

	//RST L
	outw(0x0000,(unsigned long)PLD_RTCRSTODT);

}

static unsigned char
in_byte_rtc(unsigned int reg_addr)
{
	unsigned char retval;

	//RST H
	outw(0x0001,(unsigned long)PLD_RTCRSTODT);
	//write data
	outw((reg_addr&0xff),(unsigned long)PLD_RTCRDDATA);
	//RE
	outw(0x0001,(unsigned long)PLD_RTCCR);
	//wait
	while(inw((unsigned long)PLD_RTCCR));

	//read data
	retval=(inw((unsigned long)PLD_RTCRDDATA) & 0xff00)>>8;

	//RST L
	outw(0x0000,(unsigned long)PLD_RTCRSTODT);

	return retval;
}

/* Enable writing. */

static void
ds1302_wenable(void)
{
	out_byte_rtc(0x8e,0x00);
}

/* Disable writing. */

static void
ds1302_wdisable(void)
{
	out_byte_rtc(0x8e,0x80);
}



/* Read a byte from the selected register in the DS1302. */

unsigned char
ds1302_readreg(int reg)
{
	unsigned char x;

	x=in_byte_rtc((0x81 | (reg << 1))); /* read register */

	return x;
}

/* Write a byte to the selected register. */

void
ds1302_writereg(int reg, unsigned char val)
{
	ds1302_wenable();
	out_byte_rtc((0x80 | (reg << 1)),val);
	ds1302_wdisable();
}

void
get_rtc_time(struct rtc_time *rtc_tm)
{
	unsigned long flags;

	local_irq_save(flags);

	rtc_tm->tm_sec = CMOS_READ(RTC_SECONDS);
	rtc_tm->tm_min = CMOS_READ(RTC_MINUTES);
	rtc_tm->tm_hour = CMOS_READ(RTC_HOURS);
	rtc_tm->tm_mday = CMOS_READ(RTC_DAY_OF_MONTH);
	rtc_tm->tm_mon = CMOS_READ(RTC_MONTH);
	rtc_tm->tm_year = CMOS_READ(RTC_YEAR);

	local_irq_restore(flags);

	BCD_TO_BIN(rtc_tm->tm_sec);
	BCD_TO_BIN(rtc_tm->tm_min);
	BCD_TO_BIN(rtc_tm->tm_hour);
	BCD_TO_BIN(rtc_tm->tm_mday);
	BCD_TO_BIN(rtc_tm->tm_mon);
	BCD_TO_BIN(rtc_tm->tm_year);

	/*
	 * Account for differences between how the RTC uses the values
	 * and how they are defined in a struct rtc_time;
	 */

	if (rtc_tm->tm_year <= 69)
		rtc_tm->tm_year += 100;

	rtc_tm->tm_mon--;
}

static unsigned char days_in_mo[] =
    {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/* ioctl that supports RTC_RD_TIME and RTC_SET_TIME (read and set time/date). */

static int
rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	  unsigned long arg)
{
	unsigned long flags;

	switch(cmd) {
		case RTC_RD_TIME:	/* read the time/date from RTC	*/
		{
			struct rtc_time rtc_tm;

			memset(&rtc_tm, 0, sizeof (struct rtc_time));
			get_rtc_time(&rtc_tm);
			if (copy_to_user((struct rtc_time*)arg, &rtc_tm, sizeof(struct rtc_time)))
				return -EFAULT;
			return 0;
		}

		case RTC_SET_TIME:	/* set the RTC */
		{
			struct rtc_time rtc_tm;
			unsigned char mon, day, hrs, min, sec, leap_yr;
			unsigned int yrs;

			if (!capable(CAP_SYS_TIME))
				return -EPERM;

			if (copy_from_user(&rtc_tm, (struct rtc_time*)arg, sizeof(struct rtc_time)))
				return -EFAULT;

			yrs = rtc_tm.tm_year + 1900;
			mon = rtc_tm.tm_mon + 1;   /* tm_mon starts at zero */
			day = rtc_tm.tm_mday;
			hrs = rtc_tm.tm_hour;
			min = rtc_tm.tm_min;
			sec = rtc_tm.tm_sec;


			if ((yrs < 1970) || (yrs > 2069))
				return -EINVAL;

			leap_yr = ((!(yrs % 4) && (yrs % 100)) || !(yrs % 400));

			if ((mon > 12) || (day == 0))
				return -EINVAL;

			if (day > (days_in_mo[mon] + ((mon == 2) && leap_yr)))
				return -EINVAL;

			if ((hrs >= 24) || (min >= 60) || (sec >= 60))
				return -EINVAL;

			if (yrs >= 2000)
				yrs -= 2000;	/* RTC (0, 1, ... 69) */
			else
				yrs -= 1900;	/* RTC (70, 71, ... 99) */

			BIN_TO_BCD(sec);
			BIN_TO_BCD(min);
			BIN_TO_BCD(hrs);
			BIN_TO_BCD(day);
			BIN_TO_BCD(mon);
			BIN_TO_BCD(yrs);

			local_irq_save(flags);
			CMOS_WRITE(yrs, RTC_YEAR);
			CMOS_WRITE(mon, RTC_MONTH);
			CMOS_WRITE(day, RTC_DAY_OF_MONTH);
			CMOS_WRITE(hrs, RTC_HOURS);
			CMOS_WRITE(min, RTC_MINUTES);
			CMOS_WRITE(sec, RTC_SECONDS);
			local_irq_restore(flags);

			/* Notice that at this point, the RTC is updated but
			 * the kernel is still running with the old time.
			 * You need to set that separately with settimeofday
			 * or adjtimex.
			 */
			return 0;
		}

		case RTC_SET_CHARGE: /* set the RTC TRICKLE CHARGE register */
		{
			int tcs_val;

			if (!capable(CAP_SYS_TIME))
				return -EPERM;

			if(copy_from_user(&tcs_val, (int*)arg, sizeof(int)))
				return -EFAULT;

			tcs_val = RTC_TCR_PATTERN | (tcs_val & 0x0F);
			ds1302_writereg(RTC_TRICKLECHARGER, tcs_val);
			return 0;
		}
		default:
			return -EINVAL;
	}
}

int
get_rtc_status(char *buf)
{
	char *p;
	struct rtc_time tm;

	p = buf;

	get_rtc_time(&tm);

	/*
	 * There is no way to tell if the luser has the RTC set for local
	 * time or for Universal Standard Time (GMT). Probably local though.
	 */

	p += sprintf(p,
		"rtc_time\t: %02d:%02d:%02d\n"
		"rtc_date\t: %04d-%02d-%02d\n",
		tm.tm_hour, tm.tm_min, tm.tm_sec,
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

	return  p - buf;
}


/* The various file operations we support. */

static const struct file_operations rtc_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= rtc_ioctl,
};

/* Probe for the chip by writing something to its RAM and try reading it back. */

#define MAGIC_PATTERN 0x42

static int __init
ds1302_probe(void)
{
	int retval, res, baur;

	baur=(boot_cpu_data.bus_clock/(2*1000*1000));

	printk("%s: Set PLD_RTCBAUR = %d\n", ds1302_name,baur);

	outw(0x0000,(unsigned long)PLD_RTCCR);
	outw(0x0000,(unsigned long)PLD_RTCRSTODT);
	outw(baur,(unsigned long)PLD_RTCBAUR);

	/* Try to talk to timekeeper. */

	ds1302_wenable();
	/* write RAM byte 0 */
	/* write something magic */
	out_byte_rtc(0xc0,MAGIC_PATTERN);

	/* read RAM byte 0 */
	if((res = in_byte_rtc(0xc1)) == MAGIC_PATTERN) {
		char buf[100];
		ds1302_wdisable();
		printk("%s: RTC found.\n", ds1302_name);
		get_rtc_status(buf);
		printk(buf);
		retval = 1;
	} else {
		printk("%s: RTC not found.\n", ds1302_name);
		retval = 0;
	}

	return retval;
}


/* Just probe for the RTC and register the device to handle the ioctl needed. */

int __init
ds1302_init(void)
{
	if (!ds1302_probe()) {
		return -1;
  	}
	return 0;
}

static int __init ds1302_register(void)
{
	ds1302_init();
	if (register_chrdev(RTC_MAJOR_NR, ds1302_name, &rtc_fops)) {
		printk(KERN_INFO "%s: unable to get major %d for rtc\n",
		       ds1302_name, RTC_MAJOR_NR);
		return -1;
	}
	return 0;
}

module_init(ds1302_register);
