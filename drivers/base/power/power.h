/*
 * shutdown.c
 */

extern void device_shutdown(void);


#ifdef CONFIG_PM_SLEEP

/*
 * main.c
 */

extern struct list_head dpm_active;	/* The active device list */

static inline struct device * to_device(struct list_head * entry)
{
	return container_of(entry, struct device, power.entry);
}

extern int device_pm_add(struct device *);
extern void device_pm_remove(struct device *);

/*
 * sysfs.c
 */

extern int dpm_sysfs_add(struct device *);
extern void dpm_sysfs_remove(struct device *);

#else /* CONFIG_PM_SLEEP */


static inline int device_pm_add(struct device * dev)
{
	return 0;
}
static inline void device_pm_remove(struct device * dev)
{

}

#endif
