#ifdef CONFIG_PM_SLEEP

/*
 * main.c
 */

extern struct list_head dpm_active;	/* The active device list */

static inline struct device *to_device(struct list_head *entry)
{
	return container_of(entry, struct device, power.entry);
}

extern void device_pm_add(struct device *);
extern void device_pm_remove(struct device *);
extern int pm_sleep_lock(void);
extern void pm_sleep_unlock(void);

#else /* CONFIG_PM_SLEEP */


static inline void device_pm_add(struct device *dev)
{
}

static inline void device_pm_remove(struct device *dev)
{
}

static inline int pm_sleep_lock(void)
{
	return 0;
}

static inline void pm_sleep_unlock(void)
{
}

#endif

#ifdef CONFIG_PM

/*
 * sysfs.c
 */

extern int dpm_sysfs_add(struct device *);
extern void dpm_sysfs_remove(struct device *);

#else /* CONFIG_PM */

static inline int dpm_sysfs_add(struct device *dev)
{
	return 0;
}

static inline void dpm_sysfs_remove(struct device *dev)
{
}

#endif
