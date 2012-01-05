#include <linux/android_alarm.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/sysdev.h>
#include <linux/wakelock.h>
#include <linux/power_supply.h>
#include <linux/suspend.h>

#include <asm/mach/time.h>

struct auto_wake
{
	struct alarm alarm;
	struct timespec timer;
	//struct notifier_block pm_nb;
};

static struct auto_wake auto_wake;
struct timespec set_atuo_wake_time( struct timespec timer)
{
	struct timespec new_time;
	struct timespec tmp_time;
	
	tmp_time =ktime_to_timespec(alarm_get_elapsed_realtime());

	printk("nowtime = %ld \n",tmp_time.tv_sec);
	
	new_time.tv_nsec = timer.tv_nsec+ tmp_time.tv_nsec;
	new_time.tv_sec =  timer.tv_sec+ tmp_time.tv_sec;

	return new_time;
}

static void auto_wake_update(struct auto_wake *auto_wake)
{

	struct timespec new_alarm_time;
	
//	printk("auto_wake_update\n");
	new_alarm_time = set_atuo_wake_time(auto_wake->timer);
	alarm_start_range(&auto_wake->alarm,
			timespec_to_ktime(new_alarm_time),
			timespec_to_ktime(new_alarm_time));
}

static void atuo_wake_trigger(struct alarm *alarm)
{

	struct auto_wake *auto_wake = container_of(alarm, struct auto_wake,
						    alarm);

	auto_wake_update(auto_wake);
}


#if 0
static void auto_wake_cancel(struct auto_wake *auto_wake)
{
	alarm_cancel(&auto_wake->alarm);
}



static int auto_wake_callback(struct notifier_block *nfb,
					unsigned long action,
					void *ignored)
{

	struct auto_wake *auto_wake = container_of(nfb, struct auto_wake,
						    pm_nb);

	switch (action)
	{
		case PM_SUSPEND_PREPARE:
		{
			printk("PM_SUSPEND_PREPARExsf \n");
			auto_wake_update(auto_wake);
			return NOTIFY_OK;
		}
		case PM_POST_SUSPEND:
		{
			printk("PM_POST_SUSPENDxsf \n");
		//	auto_wake_cancel(auto_wake);
			return NOTIFY_OK;
		}
	}

	return NOTIFY_DONE;
}


static struct notifier_block auto_wake_pm_notifier = {
	.notifier_call = auto_wake_callback,
	.priority = 0,
};

#endif

void auto_wake_init(struct auto_wake *auto_wake,struct timespec timer)
{
//	auto_wake->pm_nb = auto_wake_pm_notifier;
	auto_wake->timer = timer;
	
	alarm_init(&auto_wake->alarm,	ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP, atuo_wake_trigger);

	//register_pm_notifier(&auto_wake->pm_nb);// xsf

}

static int  __init start_auto_wake(void)
{
   
	struct timespec timer;

	printk("CONFIG_AUTO_WAKE_UP_PERIOD = %d\n", CONFIG_AUTO_WAKE_UP_PERIOD);
	timer.tv_sec =   CONFIG_AUTO_WAKE_UP_PERIOD;
	timer.tv_nsec = 0;
	
	auto_wake_init(&auto_wake,timer);
	auto_wake_update(&auto_wake);
	return 0;
} 

late_initcall_sync(start_auto_wake);
