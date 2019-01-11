/* SPDX-License-Identifier: GPL-2.0 */
#ifdef CONFIG_RTC_INTF_DEV

extern void __init rtc_dev_init(void);
extern void __exit rtc_dev_exit(void);
extern void rtc_dev_prepare(struct rtc_device *rtc);

#else

static inline void rtc_dev_init(void)
{
}

static inline void rtc_dev_exit(void)
{
}

static inline void rtc_dev_prepare(struct rtc_device *rtc)
{
}

#endif

#ifdef CONFIG_RTC_INTF_PROC

extern void rtc_proc_add_device(struct rtc_device *rtc);
extern void rtc_proc_del_device(struct rtc_device *rtc);

#else

static inline void rtc_proc_add_device(struct rtc_device *rtc)
{
}

static inline void rtc_proc_del_device(struct rtc_device *rtc)
{
}

#endif

#ifdef CONFIG_RTC_INTF_SYSFS
const struct attribute_group **rtc_get_dev_attribute_groups(void);
int rtc_add_group(struct rtc_device *rtc, const struct attribute_group *grp);
int rtc_add_groups(struct rtc_device *rtc, const struct attribute_group **grps);
#else
static inline const struct attribute_group **rtc_get_dev_attribute_groups(void)
{
	return NULL;
}

static inline
int rtc_add_group(struct rtc_device *rtc, const struct attribute_group *grp)
{
	return 0;
}

static inline
int rtc_add_groups(struct rtc_device *rtc, const struct attribute_group **grps)
{
	return 0;
}
#endif
