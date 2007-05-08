#ifdef CONFIG_RTC_INTF_DEV

extern void __init rtc_dev_init(void);
extern void __exit rtc_dev_exit(void);
extern void rtc_dev_add_device(struct rtc_device *rtc);
extern void rtc_dev_del_device(struct rtc_device *rtc);

#else

#define rtc_dev_init()		do{}while(0)
#define rtc_dev_exit()		do{}while(0)
#define rtc_dev_add_device(r)	do{}while(0)
#define rtc_dev_del_device(r)	do{}while(0)

#endif

#ifdef CONFIG_RTC_INTF_PROC

void rtc_proc_add_device(struct rtc_device *rtc);
void rtc_proc_del_device(struct rtc_device *rtc);

#else

#define rtc_proc_add_device(r)	do{}while(0)
#define rtc_proc_del_device(r)	do{}while(0)

#endif

#ifdef CONFIG_RTC_INTF_SYSFS

extern void __init rtc_sysfs_init(struct class *);
extern void rtc_sysfs_add_device(struct rtc_device *rtc);
extern void rtc_sysfs_del_device(struct rtc_device *rtc);

#else

#define rtc_sysfs_init(c)	do{}while(0)
#define rtc_sysfs_add_device(r)	do{}while(0)
#define rtc_sysfs_del_device(r)	do{}while(0)

#endif
