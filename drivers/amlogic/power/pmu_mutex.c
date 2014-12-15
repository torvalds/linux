
#include <linux/mutex.h>
#include <linux/lockdep.h>
#include <linux/slab.h>
#include <linux/printk.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/rtc.h>
#include <linux/err.h>
#include <linux/alarmtimer.h>

/*
 * some data-struct is depend on kernel's menuconfig
 * so make these calls in a single file with open source.
 * This can help compatibility of PMU driver liberary.
 */
static int mutex_cnt = 0;
const  char mutex_name[20] = {};
static int    alarm_inited = 0; 
static struct alarm battery_alarm;

void *pmu_alloc_mutex(void)
{
    struct mutex *pmutex = NULL;
    struct lock_class_key *key = NULL;

    pmutex = kzalloc(sizeof(struct mutex), GFP_KERNEL);
    if (!pmutex) {
        printk("%s, alloc mutex failed\n", __func__);
        return NULL;
    }
    key = kzalloc(sizeof(struct lock_class_key), GFP_KERNEL);
    if (!key) {
        printk("%s, alloc key failed\n", __func__);
        return NULL;
    }
    sprintf((char *)mutex_name, "pmu_mutex%d", mutex_cnt++); 
    __mutex_init(pmutex, mutex_name, key);
    return (void *)pmutex;
}
EXPORT_SYMBOL(pmu_alloc_mutex);

void pmu_mutex_lock(void *mutex)
{
    mutex_lock((struct mutex *)mutex);    
}
EXPORT_SYMBOL(pmu_mutex_lock);

void pmu_mutex_unlock(void *mutex)
{
    mutex_unlock((struct mutex *)mutex);    
}
EXPORT_SYMBOL(pmu_mutex_unlock);

static enum alarmtimer_restart pmu_battery_alarm(struct alarm *alarm, ktime_t now)
{
    printk("%s\n", __func__);
	return ALARMTIMER_NORESTART;
}

int pmu_rtc_device_init(void)
{
    if (alarm_inited) {
        return 0;    
    }
    alarm_init(&battery_alarm, ALARM_REALTIME, pmu_battery_alarm);
    alarm_inited = 1;
    return 0;
}
EXPORT_SYMBOL(pmu_rtc_device_init);

int pmu_rtc_set_alarm(unsigned long seconds) 
{
    int ret;

    if (!alarm_inited) {
        printk("%s, alarm is not inited\n", __func__);
        return -ENODEV;    
    }
	ret = alarm_start(&battery_alarm,
                      ktime_add(ktime_get_real(), ktime_set(seconds, 0)));
    printk("%s, set wake up alarm in %ld seconds, ret:%d\n", __func__, seconds, ret);
    return ret;
}
EXPORT_SYMBOL(pmu_rtc_set_alarm);
