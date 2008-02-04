/*
 * EFI Time Services Driver for Linux
 *
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1999 Stephane Eranian <eranian@hpl.hp.com>
 *
 * Based on skeleton from the drivers/char/rtc.c driver by P. Gortmaker
 *
 * This code provides an architected & portable interface to the real time
 * clock by using EFI instead of direct bit fiddling. The functionalities are 
 * quite different from the rtc.c driver. The only way to talk to the device 
 * is by using ioctl(). There is a /proc interface which provides the raw 
 * information.
 *
 * Please note that we have kept the API as close as possible to the
 * legacy RTC. The standard /sbin/hwclock program should work normally 
 * when used to get/set the time.
 *
 * NOTES:
 *	- Locking is required for safe execution of EFI calls with regards
 *	  to interrupts and SMP.
 *
 * TODO (December 1999):
 * 	- provide the API to set/get the WakeUp Alarm (different from the
 *	  rtc.c alarm).
 *	- SMP testing
 * 	- Add module support
 */


#include <linux/types.h>
#include <linux/errno.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rtc.h>
#include <linux/proc_fs.h>
#include <linux/efi.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#define EFI_RTC_VERSION		"0.4"

#define EFI_ISDST (EFI_TIME_ADJUST_DAYLIGHT|EFI_TIME_IN_DAYLIGHT)
/*
 * EFI Epoch is 1/1/1998
 */
#define EFI_RTC_EPOCH		1998

static DEFINE_SPINLOCK(efi_rtc_lock);

static int efi_rtc_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg);

#define is_leap(year) \
          ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))

static const unsigned short int __mon_yday[2][13] =
{
	/* Normal years.  */
	{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
	/* Leap years.  */  
	{ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
};

/*
 * returns day of the year [0-365]
 */
static inline int
compute_yday(efi_time_t *eft)
{
	/* efi_time_t.month is in the [1-12] so, we need -1 */
	return  __mon_yday[is_leap(eft->year)][eft->month-1]+ eft->day -1;
}
/*
 * returns day of the week [0-6] 0=Sunday
 *
 * Don't try to provide a year that's before 1998, please !
 */
static int
compute_wday(efi_time_t *eft)
{
	int y;
	int ndays = 0;

	if ( eft->year < 1998 ) {
		printk(KERN_ERR "efirtc: EFI year < 1998, invalid date\n");
		return -1;
	}

	for(y=EFI_RTC_EPOCH; y < eft->year; y++ ) {
		ndays += 365 + (is_leap(y) ? 1 : 0);
	}
	ndays += compute_yday(eft);

	/*
	 * 4=1/1/1998 was a Thursday
	 */
	return (ndays + 4) % 7;
}

static void
convert_to_efi_time(struct rtc_time *wtime, efi_time_t *eft)
{

	eft->year	= wtime->tm_year + 1900;
	eft->month	= wtime->tm_mon + 1; 
	eft->day	= wtime->tm_mday;
	eft->hour	= wtime->tm_hour;
	eft->minute	= wtime->tm_min;
	eft->second 	= wtime->tm_sec;
	eft->nanosecond = 0; 
	eft->daylight	= wtime->tm_isdst ? EFI_ISDST: 0;
	eft->timezone	= EFI_UNSPECIFIED_TIMEZONE;
}

static void
convert_from_efi_time(efi_time_t *eft, struct rtc_time *wtime)
{
	memset(wtime, 0, sizeof(*wtime));
	wtime->tm_sec  = eft->second;
	wtime->tm_min  = eft->minute;
	wtime->tm_hour = eft->hour;
	wtime->tm_mday = eft->day;
	wtime->tm_mon  = eft->month - 1;
	wtime->tm_year = eft->year - 1900;

	/* day of the week [0-6], Sunday=0 */
	wtime->tm_wday = compute_wday(eft);

	/* day in the year [1-365]*/
	wtime->tm_yday = compute_yday(eft);


	switch (eft->daylight & EFI_ISDST) {
		case EFI_ISDST:
			wtime->tm_isdst = 1;
			break;
		case EFI_TIME_ADJUST_DAYLIGHT:
			wtime->tm_isdst = 0;
			break;
		default:
			wtime->tm_isdst = -1;
	}
}

static int
efi_rtc_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		     unsigned long arg)
{

	efi_status_t	status;
	unsigned long	flags;
	efi_time_t	eft;
	efi_time_cap_t	cap;
	struct rtc_time	wtime;
	struct rtc_wkalrm __user *ewp;
	unsigned char	enabled, pending;

	switch (cmd) {
		case RTC_UIE_ON:
		case RTC_UIE_OFF:
		case RTC_PIE_ON:
		case RTC_PIE_OFF:
		case RTC_AIE_ON:
		case RTC_AIE_OFF:
		case RTC_ALM_SET:
		case RTC_ALM_READ:
		case RTC_IRQP_READ:
		case RTC_IRQP_SET:
		case RTC_EPOCH_READ:
		case RTC_EPOCH_SET:
			return -EINVAL;

		case RTC_RD_TIME:

			spin_lock_irqsave(&efi_rtc_lock, flags);

			status = efi.get_time(&eft, &cap);

			spin_unlock_irqrestore(&efi_rtc_lock,flags);

			if (status != EFI_SUCCESS) {
				/* should never happen */
				printk(KERN_ERR "efitime: can't read time\n");
				return -EINVAL;
			}

			convert_from_efi_time(&eft, &wtime);

 			return copy_to_user((void __user *)arg, &wtime,
					    sizeof (struct rtc_time)) ? - EFAULT : 0;

		case RTC_SET_TIME:

			if (!capable(CAP_SYS_TIME)) return -EACCES;

			if (copy_from_user(&wtime, (struct rtc_time __user *)arg,
					   sizeof(struct rtc_time)) )
				return -EFAULT;

			convert_to_efi_time(&wtime, &eft);

			spin_lock_irqsave(&efi_rtc_lock, flags);

			status = efi.set_time(&eft);

			spin_unlock_irqrestore(&efi_rtc_lock,flags);

			return status == EFI_SUCCESS ? 0 : -EINVAL;

		case RTC_WKALM_SET:

			if (!capable(CAP_SYS_TIME)) return -EACCES;

			ewp = (struct rtc_wkalrm __user *)arg;

			if (  get_user(enabled, &ewp->enabled)
			   || copy_from_user(&wtime, &ewp->time, sizeof(struct rtc_time)) )
				return -EFAULT;

			convert_to_efi_time(&wtime, &eft);

			spin_lock_irqsave(&efi_rtc_lock, flags);
			/*
			 * XXX Fixme:
			 * As of EFI 0.92 with the firmware I have on my
			 * machine this call does not seem to work quite
			 * right
			 */
			status = efi.set_wakeup_time((efi_bool_t)enabled, &eft);

			spin_unlock_irqrestore(&efi_rtc_lock,flags);

			return status == EFI_SUCCESS ? 0 : -EINVAL;

		case RTC_WKALM_RD:

			spin_lock_irqsave(&efi_rtc_lock, flags);

			status = efi.get_wakeup_time((efi_bool_t *)&enabled, (efi_bool_t *)&pending, &eft);

			spin_unlock_irqrestore(&efi_rtc_lock,flags);

			if (status != EFI_SUCCESS) return -EINVAL;

			ewp = (struct rtc_wkalrm __user *)arg;

			if (  put_user(enabled, &ewp->enabled)
			   || put_user(pending, &ewp->pending)) return -EFAULT;

			convert_from_efi_time(&eft, &wtime);

			return copy_to_user(&ewp->time, &wtime,
					    sizeof(struct rtc_time)) ? -EFAULT : 0;
	}
	return -EINVAL;
}

/*
 *	We enforce only one user at a time here with the open/close.
 *	Also clear the previous interrupt data on an open, and clean
 *	up things on a close.
 */

static int
efi_rtc_open(struct inode *inode, struct file *file)
{
	/*
	 * nothing special to do here
	 * We do accept multiple open files at the same time as we
	 * synchronize on the per call operation.
	 */
	return 0;
}

static int
efi_rtc_close(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 *	The various file operations we support.
 */

static const struct file_operations efi_rtc_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= efi_rtc_ioctl,
	.open		= efi_rtc_open,
	.release	= efi_rtc_close,
};

static struct miscdevice efi_rtc_dev=
{
	EFI_RTC_MINOR,
	"efirtc",
	&efi_rtc_fops
};

/*
 *	We export RAW EFI information to /proc/driver/efirtc
 */
static int
efi_rtc_get_status(char *buf)
{
	efi_time_t 	eft, alm;
	efi_time_cap_t	cap;
	char		*p = buf;
	efi_bool_t	enabled, pending;	
	unsigned long	flags;

	memset(&eft, 0, sizeof(eft));
	memset(&alm, 0, sizeof(alm));
	memset(&cap, 0, sizeof(cap));

	spin_lock_irqsave(&efi_rtc_lock, flags);

	efi.get_time(&eft, &cap);
	efi.get_wakeup_time(&enabled, &pending, &alm);

	spin_unlock_irqrestore(&efi_rtc_lock,flags);

	p += sprintf(p,
		     "Time           : %u:%u:%u.%09u\n"
		     "Date           : %u-%u-%u\n"
		     "Daylight       : %u\n",
		     eft.hour, eft.minute, eft.second, eft.nanosecond, 
		     eft.year, eft.month, eft.day,
		     eft.daylight);

	if (eft.timezone == EFI_UNSPECIFIED_TIMEZONE)
		p += sprintf(p, "Timezone       : unspecified\n");
	else
		/* XXX fixme: convert to string? */
		p += sprintf(p, "Timezone       : %u\n", eft.timezone);
		

	p += sprintf(p,
		     "Alarm Time     : %u:%u:%u.%09u\n"
		     "Alarm Date     : %u-%u-%u\n"
		     "Alarm Daylight : %u\n"
		     "Enabled        : %s\n"
		     "Pending        : %s\n",
		     alm.hour, alm.minute, alm.second, alm.nanosecond, 
		     alm.year, alm.month, alm.day, 
		     alm.daylight,
		     enabled == 1 ? "yes" : "no",
		     pending == 1 ? "yes" : "no");

	if (eft.timezone == EFI_UNSPECIFIED_TIMEZONE)
		p += sprintf(p, "Timezone       : unspecified\n");
	else
		/* XXX fixme: convert to string? */
		p += sprintf(p, "Timezone       : %u\n", alm.timezone);

	/*
	 * now prints the capabilities
	 */
	p += sprintf(p,
		     "Resolution     : %u\n"
		     "Accuracy       : %u\n"
		     "SetstoZero     : %u\n",
		      cap.resolution, cap.accuracy, cap.sets_to_zero);

	return  p - buf;
}

static int
efi_rtc_read_proc(char *page, char **start, off_t off,
                                 int count, int *eof, void *data)
{
        int len = efi_rtc_get_status(page);
        if (len <= off+count) *eof = 1;
        *start = page + off;
        len -= off;
        if (len>count) len = count;
        if (len<0) len = 0;
        return len;
}

static int __init 
efi_rtc_init(void)
{
	int ret;
	struct proc_dir_entry *dir;

	printk(KERN_INFO "EFI Time Services Driver v%s\n", EFI_RTC_VERSION);

	ret = misc_register(&efi_rtc_dev);
	if (ret) {
		printk(KERN_ERR "efirtc: can't misc_register on minor=%d\n",
				EFI_RTC_MINOR);
		return ret;
	}

	dir = create_proc_read_entry ("driver/efirtc", 0, NULL,
			              efi_rtc_read_proc, NULL);
	if (dir == NULL) {
		printk(KERN_ERR "efirtc: can't create /proc/driver/efirtc.\n");
		misc_deregister(&efi_rtc_dev);
		return -1;
	}
	return 0;
}

static void __exit
efi_rtc_exit(void)
{
	/* not yet used */
}

module_init(efi_rtc_init);
module_exit(efi_rtc_exit);

MODULE_LICENSE("GPL");
