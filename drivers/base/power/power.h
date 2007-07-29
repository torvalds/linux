/*
 * shutdown.c
 */

extern void device_shutdown(void);


#ifdef CONFIG_PM_SLEEP

/*
 * main.c
 */

/*
 * Used to synchronize global power management operations.
 */
extern struct mutex dpm_mtx;

/*
 * Used to serialize changes to the dpm_* lists.
 */
extern struct mutex dpm_list_mtx;

/*
 * The PM lists.
 */
extern struct list_head dpm_active;
extern struct list_head dpm_off;
extern struct list_head dpm_off_irq;


static inline struct dev_pm_info * to_pm_info(struct list_head * entry)
{
	return container_of(entry, struct dev_pm_info, entry);
}

static inline struct device * to_device(struct list_head * entry)
{
	return container_of(to_pm_info(entry), struct device, power);
}

extern int device_pm_add(struct device *);
extern void device_pm_remove(struct device *);

/*
 * sysfs.c
 */

extern int dpm_sysfs_add(struct device *);
extern void dpm_sysfs_remove(struct device *);

/*
 * resume.c
 */

extern void dpm_resume(void);
extern void dpm_power_up(void);
extern int resume_device(struct device *);

/*
 * suspend.c
 */
extern int suspend_device(struct device *, pm_message_t);

#else /* CONFIG_PM_SLEEP */


static inline int device_pm_add(struct device * dev)
{
	return 0;
}
static inline void device_pm_remove(struct device * dev)
{

}

#endif
