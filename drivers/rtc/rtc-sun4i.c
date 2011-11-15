#include <linux/module.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/clk.h>
#include <linux/log2.h>
#include <linux/delay.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/delay.h>

#define RTC_ADDR_SIZE		        0x3ff

#define RTC_BASE_ADDR_START        (0x01C20C00)
#define RTC_BASE_ADDR_END          (RTC_BASE_ADDR_START + RTC_ADDR_SIZE)


#define AW1623_TIMER_IRQ_EN_REG                        (0x00)
#define AW1623_LOSC_CTRL_REG                           (0x100)

#define AW1623_RTC_DATE_REG                            (0x104)
#define AW1623_RTC_TIME_REG                            (0x108)

#define AW1623_RTC_ALARM_DD_HH_MM_SS_REG               (0x10C)
#define AW1623_RTC_WEEK_HH_MM_SS_REG                   (0x110)

#define AW1623_ALARM_EN_REG                            (0x114)
#define AW1623_ALARM_INT_CTRL_REG                      (0x118)
#define AW1623_ALARM_INT_STATUS_REG                    (0x11C)

/*interrupt control*/
/*rtc week interrupt control*/
#define RTC_ALARM_MONDAY_INT_EN 			 			0x00000001
#define RTC_ALARM_TUESDAY_INT_EN 			 			0x00000002
#define RTC_ALARM_WEDNESDAY_INT_EN 			 			0x00000004
#define RTC_ALARM_THURSDAY_INT_EN 			 			0x00000008
#define RTC_ALARM_FRIDAY_INT_EN 			 			0x00000010
#define RTC_ALARM_SATURDAY_INT_EN 			 			0x00000020
#define RTC_ALARM_SUNDAY_INT_EN 			 			0x00000040

#define RTC_ENABLE_WK_IRQ            					0x00000002
#define RTC_DISABLE_WK_IRQ          	 				0xfffffffd

/*rtc count interrupt control*/
#define RTC_ALARM_COUNT_INT_EN 			 				0x00000100

#define RTC_ENABLE_CNT_IRQ        						0x00000001
#define RTC_DISABLE_CNT_IRQ       						0xfffffffe

/*Crystal Control*/
#define REG_LOSCCTRL_MAGIC		    					0x16aa0000
#define REG_CLK32K_AUTO_SWT_EN  						(0x00004000)
#define RTC_SOURCE_INTERNAL         					0x00000000
#define RTC_SOURCE_EXTERNAL         					0x00000001
#define ALM_DDHHMMSS_ACCESS         					0x00000200
#define RTC_HHMMSS_ACCESS           					0x00000100
#define RTC_YYMMDD_ACCESS           					0x00000080
#define EXT_LOSC_GSM                          			(0x00000008)

/*Date Value*/
#define DATE_GET_DAY_VALUE(x)       					((x) &0x0000001f)
#define DATE_GET_MON_VALUE(x)       					(((x)&0x00000f00) >> 8 )
#define DATE_GET_YEAR_VALUE(x)      					(((x)&0x003f0000) >> 16)

#define DATE_SET_DAY_VALUE(x)       					DATE_GET_DAY_VALUE(x)
#define DATE_SET_MON_VALUE(x)       					(((x)&0x0000000f) << 8 )
#define DATE_SET_YEAR_VALUE(x)      					(((x)&0x0000003f) << 16)
#define LEAP_SET_VALUE(x)           					(((x)&0x00000001) << 22)

/*Time Value*/
#define TIME_GET_SEC_VALUE(x)       					((x) &0x0000003f)
#define TIME_GET_MIN_VALUE(x)       					(((x)&0x00003f00) >> 8 )
#define TIME_GET_HOUR_VALUE(x)      					(((x)&0x001f0000) >> 16)
#define TIME_GET_WEEK_VALUE(x)							(((x)&0xf0000000) >> 29)

#define TIME_SET_SEC_VALUE(x)       					TIME_GET_SEC_VALUE(x)
#define TIME_SET_MIN_VALUE(x)       					(((x)&0x0000003f) << 8 )
#define TIME_SET_HOUR_VALUE(x)      					(((x)&0x0000001f) << 16)
#define TIME_SET_WEEK_VALUE(x)							(((x)&0xf0000000) << 29)

/*ALARM Value*/
#define ALARM_GET_SEC_VALUE(x)      					((x) &0x0000003f)
#define ALARM_GET_MIN_VALUE(x)      					(((x)&0x00003f00) >> 8 )
#define ALARM_GET_HOUR_VALUE(x)     					(((x)&0x001f0000) >> 16)
#define ALARM_GET_DAY_VALUE(x)      					(((x)&0xffe00000) >> 21)
#define ALARM_SET_SEC_VALUE(x)      					((x) &0x0000003f)
#define ALARM_SET_MIN_VALUE(x)      					(((x)&0x0000003f) << 8 )
#define ALARM_SET_HOUR_VALUE(x)     					(((x)&0x0000001f) << 16)
#define ALARM_SET_DAY_VALUE(x)      					(((x)&0x00000ffe) << 21)

#define PWM_CTRL_REG_BASE         						(0xf1c20c00+0x200)

//#define RTC_ALARM_DEBUG
/*
 * notice: IN 23 A version, operation(eg. write date, time reg)
 * that will affect losc reg, will also affect pwm reg at the same time
 * it is a ic bug needed to be fixed,
 * right now, before write date, time reg, we need to backup pwm reg
 * after writing, we should restore pwm reg.
 */
//#define BACKUP_PWM

/* record rtc device handle for platform to restore system time */
struct rtc_device   *sw_rtc_dev = NULL;

/*说明 f23最大变化为63年的时间
该驱动支持（2010～2073）年的时间*/

static void __iomem *f23_rtc_base;

static int f23_rtc_alarmno = NO_IRQ;
static int losc_err_flag   = 0;

/* IRQ Handlers, irq no. is shared with timer2 */
static irqreturn_t f23_rtc_alarmirq(int irq, void *id)
{
	struct rtc_device *rdev = id;
	u32 val;

#ifdef RTC_ALARM_DEBUG
	_dev_info(&(rdev->dev), "f23_rtc_alarmirq\n");
#endif

    //judge the int is whether ours
    val = readl(f23_rtc_base + AW1623_ALARM_INT_STATUS_REG)&(RTC_ENABLE_WK_IRQ | RTC_ENABLE_CNT_IRQ);
    if (val) {
		// Clear pending count alarm
		val = readl(f23_rtc_base + AW1623_ALARM_INT_STATUS_REG);//0x11c
		val |= (RTC_ENABLE_CNT_IRQ);	//0x00000001
		writel(val, f23_rtc_base + AW1623_ALARM_INT_STATUS_REG);

		rtc_update_irq(rdev, 1, RTC_AF | RTC_IRQF);
		return IRQ_HANDLED;
    } else {
        return IRQ_NONE;
    }
}

/* Update control registers,asynchronous interrupt enable*/
static void f23_rtc_setaie(int to)
{
	u32 alarm_irq_val;

#ifdef RTC_ALARM_DEBUG
	printk("%s: aie=%d\n", __func__, to);
#endif

	alarm_irq_val = readl(f23_rtc_base + AW1623_ALARM_EN_REG);
	switch(to){
		case 1:
		alarm_irq_val |= RTC_ALARM_COUNT_INT_EN;		//0x00000100
	    writel(alarm_irq_val, f23_rtc_base + AW1623_ALARM_EN_REG);//0x114
		break;
		case 0:
		default:
		alarm_irq_val = 0x00000000;
	    writel(alarm_irq_val, f23_rtc_base + AW1623_ALARM_EN_REG);//0x114
		break;
	}
}

/* Time read/write */
static int f23_rtc_gettime(struct device *dev, struct rtc_time *rtc_tm)
{
	unsigned int have_retried = 0;
	void __iomem *base = f23_rtc_base;
	unsigned int date_tmp = 0;
	unsigned int time_tmp = 0;

    //only for alarm losc err occur.
    if(losc_err_flag) {
		rtc_tm->tm_sec  = 0;
		rtc_tm->tm_min  = 0;
		rtc_tm->tm_hour = 0;
//		rtc_tm->tm_wday = 0;


		rtc_tm->tm_mday = 0;
		rtc_tm->tm_mon  = 0;
		rtc_tm->tm_year = 0;
		return -1;
    }

retry_get_time:
	_dev_info(dev,"f23_rtc_gettime\n");
    //first to get the date, then time, because the sec turn to 0 will effect the date;
	date_tmp = readl(base + AW1623_RTC_DATE_REG);
	time_tmp = readl(base + AW1623_RTC_TIME_REG);

	rtc_tm->tm_sec  = TIME_GET_SEC_VALUE(time_tmp);
	rtc_tm->tm_min  = TIME_GET_MIN_VALUE(time_tmp);
	rtc_tm->tm_hour = TIME_GET_HOUR_VALUE(time_tmp);

	rtc_tm->tm_mday = DATE_GET_DAY_VALUE(date_tmp);
	rtc_tm->tm_mon  = DATE_GET_MON_VALUE(date_tmp);
	rtc_tm->tm_year = DATE_GET_YEAR_VALUE(date_tmp);

	/* the only way to work out wether the system was mid-update
	 * when we read it is to check the second counter, and if it
	 * is zero, then we re-try the entire read
	 */
	if (rtc_tm->tm_sec == 0 && !have_retried) {
		have_retried = 1;
		goto retry_get_time;
	}

	rtc_tm->tm_year += 110;
	rtc_tm->tm_mon      -= 1;
	_dev_info(dev,"read time %d-%d-%d %d:%d:%d\n",
	       rtc_tm->tm_year + 1900, rtc_tm->tm_mon + 1, rtc_tm->tm_mday,
	       rtc_tm->tm_hour, rtc_tm->tm_min, rtc_tm->tm_sec);

	return 0;
}

static int f23_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	void __iomem *base = f23_rtc_base;
	unsigned int date_tmp = 0;
	unsigned int time_tmp = 0;
	unsigned int crystal_data = 0;
	unsigned int timeout = 0;
	int leap_year = 0;

#ifdef BACKUP_PWM
	unsigned int pwm_ctrl_reg_backup = 0;
	unsigned int pwm_ch0_period_backup = 0;
#endif

    /*int tm_year; years from 1900
    *int tm_mon; months since january 0-11
    *the input para tm->tm_year is the offset related 1900;
    */
	leap_year = tm->tm_year + 1900;
	if(leap_year > 2073 || leap_year < 2010) {
		dev_err(dev, "rtc only supports 63（2010～2073） years\n");
		return -EINVAL;
	}

	crystal_data = readl(base + AW1623_LOSC_CTRL_REG);

	/*Any bit of [9:7] is set, The time and date
	* register can`t be written, we re-try the entried read
	*/
	{
	    //check at most 3 times.
	    int times = 3;
	    while((crystal_data & 0x380) && (times--)){
	    	printk(KERN_INFO"[RTC]canot change rtc now!\n");
	    	msleep(500);
	    	crystal_data = readl(base + AW1623_LOSC_CTRL_REG);
	    }
	}

	/*f23 ONLY SYPPORTS 63 YEARS,hardware base time:1900*/
	tm->tm_year -= 110;
	tm->tm_mon  += 1;

	/*prevent the application seting the error time*/
	if(tm->tm_mon > 12){
		_dev_info(dev, "set time month error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
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
					_dev_info(dev, "set time day error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
				       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
				       tm->tm_hour, tm->tm_min, tm->tm_sec);
				}
				if((tm->tm_hour > 24)||(tm->tm_min > 59)||(tm->tm_sec > 59)){
						_dev_info(dev, "set time error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
				       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
				       tm->tm_hour, tm->tm_min, tm->tm_sec);
				}
				break;
			case 4:
			case 6:
			case 9:
			case 11:
				if(tm->tm_mday > 30){
					_dev_info(dev, "set time day error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
				       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
				       tm->tm_hour, tm->tm_min, tm->tm_sec);
				}
				if((tm->tm_hour > 24)||(tm->tm_min > 59)||(tm->tm_sec > 59)){
					_dev_info(dev, "set time error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
				       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
				       tm->tm_hour, tm->tm_min, tm->tm_sec);
				}
				break;
			case 2:
				if((leap_year%400==0) || ((leap_year%100!=0) && (leap_year%4==0))) {
					if(tm->tm_mday > 28){
						_dev_info(dev, "set time day error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
				       		tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
				       		tm->tm_hour, tm->tm_min, tm->tm_sec);
					}
					if((tm->tm_hour > 24)||(tm->tm_min > 59)||(tm->tm_sec > 59)){
						_dev_info(dev, "set time error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
					       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
					       tm->tm_hour, tm->tm_min, tm->tm_sec);
					}
				}else{
					if(tm->tm_mday > 29){
						_dev_info(dev, "set time day error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
					       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
					       tm->tm_hour, tm->tm_min, tm->tm_sec);
					}
					if((tm->tm_hour > 24)||(tm->tm_min > 59)||(tm->tm_sec > 59)){
						_dev_info(dev, "set time error:line:%d,%d-%d-%d %d:%d:%d\n",__LINE__,
					       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
					       tm->tm_hour, tm->tm_min, tm->tm_sec);
					}

				}
				break;
			default:
				break;
		}
		tm->tm_sec  = 0;
		tm->tm_min  = 0;
		tm->tm_hour = 0;
		tm->tm_mday = 0;
		tm->tm_mon  = 0;
		tm->tm_year = 0;
	}

	_dev_info(dev, "set time %d-%d-%d %d:%d:%d\n",
	       tm->tm_year + 2010, tm->tm_mon, tm->tm_mday,
	       tm->tm_hour, tm->tm_min, tm->tm_sec);

	date_tmp = (DATE_SET_DAY_VALUE(tm->tm_mday)|DATE_SET_MON_VALUE(tm->tm_mon)
                    |DATE_SET_YEAR_VALUE(tm->tm_year));

	time_tmp = (TIME_SET_SEC_VALUE(tm->tm_sec)|TIME_SET_MIN_VALUE(tm->tm_min)
                    |TIME_SET_HOUR_VALUE(tm->tm_hour));

#ifdef BACKUP_PWM
    pwm_ctrl_reg_backup = readl(PWM_CTRL_REG_BASE + 0);
    pwm_ch0_period_backup = readl(PWM_CTRL_REG_BASE + 4);
	printk("[rtc-pwm] 1 pwm_ctrl_reg_backup = %x pwm_ch0_period_backup = %x", pwm_ctrl_reg_backup, pwm_ch0_period_backup);
#endif

	writel(time_tmp,  base + AW1623_RTC_TIME_REG);
	timeout = 0xffff;
	while((readl(base + AW1623_LOSC_CTRL_REG)&(RTC_HHMMSS_ACCESS))&&(--timeout))
	if (timeout == 0) {
        dev_err(dev, "fail to set rtc time.\n");

#ifdef BACKUP_PWM
	    writel(pwm_ctrl_reg_backup, PWM_CTRL_REG_BASE + 0);
	    writel(pwm_ch0_period_backup, PWM_CTRL_REG_BASE + 4);

		pwm_ctrl_reg_backup = readl(PWM_CTRL_REG_BASE + 0);
    	pwm_ch0_period_backup = readl(PWM_CTRL_REG_BASE + 4);
		printk("[rtc-pwm] 2 pwm_ctrl_reg_backup = %x pwm_ch0_period_backup = %x", pwm_ctrl_reg_backup, pwm_ch0_period_backup);
#endif

        return -1;
    }

	if((leap_year%400==0) || ((leap_year%100!=0) && (leap_year%4==0))) {
		/*Set Leap Year bit*/
		date_tmp |= LEAP_SET_VALUE(1);
	}

	writel(date_tmp, base + AW1623_RTC_DATE_REG);
	timeout = 0xffff;
	while((readl(base + AW1623_LOSC_CTRL_REG)&(RTC_YYMMDD_ACCESS))&&(--timeout))
	if(timeout == 0)
    {
        dev_err(dev, "fail to set rtc date.\n");

#ifdef BACKUP_PWM
        writel(pwm_ctrl_reg_backup, PWM_CTRL_REG_BASE + 0);
        writel(pwm_ch0_period_backup, PWM_CTRL_REG_BASE + 4);

		pwm_ctrl_reg_backup = readl(PWM_CTRL_REG_BASE + 0);
	    pwm_ch0_period_backup = readl(PWM_CTRL_REG_BASE + 4);
	    printk("[rtc-pwm] 5 pwm_ctrl_reg_backup = %x pwm_ch0_period_backup = %x", pwm_ctrl_reg_backup, pwm_ch0_period_backup);
#endif

        return -1;
    }

#ifdef BACKUP_PWM
       writel(pwm_ctrl_reg_backup, PWM_CTRL_REG_BASE + 0);
       writel(pwm_ch0_period_backup, PWM_CTRL_REG_BASE + 4);

 		pwm_ctrl_reg_backup = readl(PWM_CTRL_REG_BASE + 0);
	    pwm_ch0_period_backup = readl(PWM_CTRL_REG_BASE + 4);
	    printk("[rtc-pwm] 6 pwm_ctrl_reg_backup = %x pwm_ch0_period_backup = %x", pwm_ctrl_reg_backup, pwm_ch0_period_backup);
#endif

    //wait about 70us to make sure the the time is really written into target.
    udelay(70);

	return 0;
}

static int f23_rtc_getalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rtc_time *alm_tm = &alrm->time;
	void __iomem *base = f23_rtc_base;
	unsigned int alarm_en;
	unsigned int alarm_tmp = 0;
	unsigned int date_tmp = 0;

    alarm_tmp = readl(base + AW1623_RTC_ALARM_DD_HH_MM_SS_REG);
	date_tmp = readl(base + AW1623_RTC_DATE_REG);

    alm_tm->tm_sec  = ALARM_GET_SEC_VALUE(alarm_tmp);
    alm_tm->tm_min  = ALARM_GET_MIN_VALUE(alarm_tmp);
    alm_tm->tm_hour = ALARM_GET_HOUR_VALUE(alarm_tmp);

	alm_tm->tm_mday = DATE_GET_DAY_VALUE(date_tmp);
	alm_tm->tm_mon  = DATE_GET_MON_VALUE(date_tmp);
	alm_tm->tm_year = DATE_GET_YEAR_VALUE(date_tmp);

    alm_tm->tm_year += 110;
    alm_tm->tm_mon  -= 1;

    alarm_en = readl(base + AW1623_ALARM_INT_CTRL_REG);
    if(alarm_en&&RTC_ALARM_COUNT_INT_EN)
    	alrm->enabled = 1;

	return 0;
}

static int f23_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
    struct rtc_time *tm = &alrm->time;
    void __iomem *base = f23_rtc_base;
    unsigned int alarm_tmp = 0;
    unsigned int alarm_en;
    int ret = 0;
    struct rtc_time tm_now;
    unsigned long time_now = 0;
    unsigned long time_set = 0;
    unsigned long time_gap = 0;
    unsigned long time_gap_day = 0;
    unsigned long time_gap_hour = 0;
    unsigned long time_gap_minute = 0;
    unsigned long time_gap_second = 0;

#ifdef RTC_ALARM_DEBUG
    printk("*****************************\n\n");
    printk("line:%d,%s the alarm time: year:%d, month:%d, day:%d. hour:%d.minute:%d.second:%d\n",\
    __LINE__, __func__, tm->tm_year, tm->tm_mon,\
    	 tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
   	printk("*****************************\n\n");
#endif

    ret = f23_rtc_gettime(dev, &tm_now);

#ifdef RTC_ALARM_DEBUG
    printk("line:%d,%s the current time: year:%d, month:%d, day:%d. hour:%d.minute:%d.second:%d\n",\
    __LINE__, __func__, tm_now.tm_year, tm_now.tm_mon,\
    	 tm_now.tm_mday, tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
   	printk("*****************************\n\n");
#endif

    ret = rtc_tm_to_time(tm, &time_set);
    ret = rtc_tm_to_time(&tm_now, &time_now);
    if(time_set <= time_now){
    	dev_err(dev, "The time or date can`t set, The day has pass!!!\n");
    	return -EINVAL;
    }
    time_gap = time_set - time_now;
    time_gap_day = time_gap/(3600*24);//day
    time_gap_hour = (time_gap - time_gap_day*24)/3600;//hour
    time_gap_minute = (time_gap - time_gap_day*24*60 - time_gap_hour*60)/60;//minute
    time_gap_second = time_gap - time_gap_day*24*60*60 - time_gap_hour*60*60-time_gap_minute*60;//second
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
    f23_rtc_setaie(0);

    /*clear the alarm count value!!!*/
    writel(0x00000000, base + AW1623_RTC_ALARM_DD_HH_MM_SS_REG);
    __udelay(100);

    /*rewrite the alarm count value!!!*/
    alarm_tmp = ALARM_SET_SEC_VALUE(time_gap_second) | ALARM_SET_MIN_VALUE(time_gap_minute)
    	| ALARM_SET_HOUR_VALUE(time_gap_hour) | ALARM_SET_DAY_VALUE(time_gap_day);
    writel(alarm_tmp, base + AW1623_RTC_ALARM_DD_HH_MM_SS_REG);//0x10c

    /*clear the count enable alarm irq bit*/
    writel(0x00000000, base + AW1623_ALARM_INT_CTRL_REG);
	alarm_en = readl(base + AW1623_ALARM_INT_CTRL_REG);//0x118

	/*enable the counter alarm irq*/
	alarm_en = readl(base + AW1623_ALARM_INT_CTRL_REG);//0x118
	alarm_en |= RTC_ENABLE_CNT_IRQ;
    writel(alarm_en, base + AW1623_ALARM_INT_CTRL_REG);//enable the counter irq!!!

	if(alrm->enabled != 1){
		printk("warning:the rtc counter interrupt isnot enable!!!,%s,%d\n", __func__, __LINE__);
	}

	/*decided whether we should start the counter to down count*/
	f23_rtc_setaie(alrm->enabled);

#ifdef RTC_ALARM_DEBUG
	printk("------------------------------------------\n\n");
	printk("%d,10c reg val:%x\n", __LINE__, *(volatile int *)(0xf1c20c00+0x10c));
	printk("%d,114 reg val:%x\n", __LINE__, *(volatile int *)(0xf1c20c00+0x114));
	printk("%d,118 reg val:%x\n", __LINE__, *(volatile int *)(0xf1c20c00+0x118));
	printk("%d,11c reg val:%x\n", __LINE__, *(volatile int *)(0xf1c20c00+0x11c));
	printk("------------------------------------------\n\n");
#endif

	return 0;
}

static int f23_rtc_open(struct device *dev)
{
	printk ("f23_rtc_open \n");
	return 0;
}

static void f23_rtc_release(struct device *dev)
{

}

static const struct rtc_class_ops f23_rtcops = {
	.open				= f23_rtc_open,
	.release			= f23_rtc_release,
	.read_time			= f23_rtc_gettime,
	.set_time			= f23_rtc_settime,
	.read_alarm			= f23_rtc_getalarm,
	.set_alarm			= f23_rtc_setalarm,
};

static int __devexit f23_rtc_remove(struct platform_device *pdev)
{
	struct rtc_device *rtc = platform_get_drvdata(pdev);
    free_irq(f23_rtc_alarmno, rtc);
    rtc_device_unregister(rtc);
	platform_set_drvdata(pdev, NULL);

	f23_rtc_setaie(0);

	return 0;
}

static int __devinit f23_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;
	int ret;
	unsigned int tmp_data;

#ifdef BACKUP_PWM
	unsigned int pwm_ctrl_reg_backup = 0;
	unsigned int pwm_ch0_period_backup = 0;
#endif

	f23_rtc_base = (void __iomem *)(SW_VA_TIMERC_IO_BASE);
	f23_rtc_alarmno = SW_INT_IRQNO_ALARM;

	/* select RTC clock source
	* on fpga board, internal 32k clk src is the default, and can not be changed
	*/
	//RTC CLOCK SOURCE internal 32K HZ
#ifdef BACKUP_PWM
	pwm_ctrl_reg_backup = readl(PWM_CTRL_REG_BASE + 0);
	pwm_ch0_period_backup = readl(PWM_CTRL_REG_BASE + 4);
	printk("[rtc-pwm] 1 pwm_ctrl_reg_backup = %x pwm_ch0_period_backup = %x", pwm_ctrl_reg_backup, pwm_ch0_period_backup);
#endif

	//upate by kevin, 2011-9-7 18:23
	//step1: set keyfiled,选择外部晶振
	tmp_data = readl(f23_rtc_base + AW1623_LOSC_CTRL_REG);
	tmp_data &= (~REG_CLK32K_AUTO_SWT_EN);            //disable auto switch,bit-14
	tmp_data |= (RTC_SOURCE_EXTERNAL | REG_LOSCCTRL_MAGIC); //external     32768hz osc
	tmp_data |= (EXT_LOSC_GSM);                                                                 //external 32768hz osc gsm
	writel(tmp_data, f23_rtc_base + AW1623_LOSC_CTRL_REG);
	__udelay(100);
	_dev_info(&(pdev->dev),"f23_rtc_probe tmp_data = %d\n", tmp_data);

	//step2: check set result，查询是否设置成功
	tmp_data = readl(f23_rtc_base + AW1623_LOSC_CTRL_REG);
	if(!(tmp_data & RTC_SOURCE_EXTERNAL)){
		printk("[RTC] ERR: Set LOSC to external failed!!!\n");
		printk("[RTC] WARNING: Rtc time will be wrong!!\n");
		losc_err_flag = 1;
	}

#ifdef BACKUP_PWM
	writel(pwm_ctrl_reg_backup, PWM_CTRL_REG_BASE + 0);
	writel(pwm_ch0_period_backup, PWM_CTRL_REG_BASE + 4);
	pwm_ctrl_reg_backup = readl(PWM_CTRL_REG_BASE + 0);
	pwm_ch0_period_backup = readl(PWM_CTRL_REG_BASE + 4);
	printk("[rtc-pwm] 2 pwm_ctrl_reg_backup = %x pwm_ch0_period_backup = %x", pwm_ctrl_reg_backup, pwm_ch0_period_backup);
#endif

	device_init_wakeup(&pdev->dev, 1);

	/* register RTC and exit */
	rtc = rtc_device_register("rtc", &pdev->dev, &f23_rtcops, THIS_MODULE);
	if (IS_ERR(rtc)) {
		dev_err(&pdev->dev, "cannot attach rtc\n");
		ret = PTR_ERR(rtc);
		goto err_out;
	}
	ret = request_irq(f23_rtc_alarmno, f23_rtc_alarmirq,
			  IRQF_DISABLED,  "f23-rtc alarm", rtc);
	if (ret) {
		dev_err(&pdev->dev, "IRQ%d error %d\n", f23_rtc_alarmno, ret);
		rtc_device_unregister(rtc);
		return ret;
	}

	sw_rtc_dev = rtc;
	platform_set_drvdata(pdev, rtc);//设置rtc结构数据为pdev的私有数据

	return 0;

	err_out:
		return ret;
}

#ifdef CONFIG_PM
/* RTC Power management control */
//rtc do not to suspend, need to keep timing.
#define f23_rtc_suspend NULL
#define f23_rtc_resume  NULL
#else
#define f23_rtc_suspend NULL
#define f23_rtc_resume  NULL
#endif

//share the irq no. with timer2
static struct resource f23_rtc_resource[] = {
	[0] = {
		.start = SW_INT_IRQNO_ALARM,
		.end   = SW_INT_IRQNO_ALARM,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device f23_device_rtc = {
	.name		    = "sun4i-rtc",
	.id		        = -1,
	.num_resources	= ARRAY_SIZE(f23_rtc_resource),
	.resource	    = f23_rtc_resource,
};


static struct platform_driver f23_rtc_driver = {
	.probe		= f23_rtc_probe,
	.remove		= __devexit_p(f23_rtc_remove),
	.suspend	= f23_rtc_suspend,
	.resume		= f23_rtc_resume,
	.driver		= {
		.name	= "sun4i-rtc",
		.owner	= THIS_MODULE,
	},
};

static int __init f23_rtc_init(void)
{
	platform_device_register(&f23_device_rtc);
	printk("sun4i RTC version 0.1 \n");
	return platform_driver_register(&f23_rtc_driver);
}

static void __exit f23_rtc_exit(void)
{
	platform_driver_unregister(&f23_rtc_driver);
}

module_init(f23_rtc_init);
module_exit(f23_rtc_exit);

MODULE_DESCRIPTION("Sochip sun4i RTC Driver");
MODULE_AUTHOR("ben");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:sun4i-rtc");
