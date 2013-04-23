#include <linux/android_alarm.h>
#include <linux/mutex.h>
#include <linux/kobject.h>
#include <linux/string.h>

#include "rk_pm_tests.h"
#include "rk_suspend_test.h"
static struct alarm alarm;
static struct timespec period;
static int alarm_status = 0;
static DEFINE_MUTEX(mutex);

static int get_alarm_status(void)
{
	return alarm_status;
}

static void alarm_update(struct alarm *alarm)
{
	struct timespec now_time;
	struct timespec new_time;

	now_time = ktime_to_timespec(alarm_get_elapsed_realtime());

	PM_DBG("now_time %ld\n",now_time.tv_sec);

	new_time.tv_sec = now_time.tv_sec + period.tv_sec;
	new_time.tv_nsec = now_time.tv_nsec + period.tv_nsec;

	alarm_start_range(alarm, timespec_to_ktime(new_time), timespec_to_ktime(new_time));
}

static void stop_auto_wakeup(void)
{
	mutex_lock(&mutex);

	if(alarm_status) {
		alarm_cancel(&alarm);
		alarm_status = 0;
	}

	mutex_unlock(&mutex);
}

static void start_auto_wakeup(long second)
{
	stop_auto_wakeup();

	mutex_lock(&mutex);

	period.tv_sec = second;
	period.tv_nsec = 0;

	alarm_init(&alarm, ANDROID_ALARM_ELAPSED_REALTIME_WAKEUP, alarm_update);	
	alarm_update(&alarm);
	alarm_status = 1;

	mutex_unlock(&mutex);
}

ssize_t auto_wakeup_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *s = buf;

	if(get_alarm_status())
		s += sprintf(s, "%s\n", "on");
	else
		s += sprintf(s, "%s\n", "off");

	return (s - buf);
}

ssize_t auto_wakeup_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	char cmd[10];
	long val;
	int len;
	int error = -EINVAL;

	sscanf(buf, "%s %ld", cmd, &val);
	len = strlen(cmd);

	if (len == strlen("on") && !strncmp(cmd,"on",len)) {
		start_auto_wakeup(val);
		error = 0;
	}
	else if(len == strlen("off") && !strncmp(cmd,"off",len)) {
		stop_auto_wakeup();
		error = 0;
	}

	return error ? error : n;
}

void  rk_soc_pm_ctr_bits_set(u32 flags);
u32  rk_soc_pm_ctr_bits_get(void);
ssize_t rk_soc_pm_helps_print(char *buf);
ssize_t suspend_test_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *s = buf;

	s += sprintf(s, "control bits is 0X%x,if bit is 1,information is following\n", rk_soc_pm_ctr_bits_get());
	s += rk_soc_pm_helps_print(s);

	return (s - buf);

}
ssize_t suspend_test_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	long val;
	int len;
	char cmd0[20];
	int bits;
	sscanf(buf,"%s %x", cmd0, &bits);
	
	//printk("auto_wakeup_store %x\n",bits);
	if (0 == strncmp(cmd0, "bits", strlen("bits"))) {
		//printk("auto_wakeup_store %x\n",bits);
		rk_soc_pm_ctr_bits_set(bits);
	} 
	return n;
}


