/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/pm_qos.h>

static inline void device_pm_init_common(struct device *dev)
{
	if (!dev->power.early_init) {
		spin_lock_init(&dev->power.lock);
		dev->power.qos = NULL;
		dev->power.early_init = true;
	}
}

#ifdef CONFIG_PM

static inline void pm_runtime_early_init(struct device *dev)
{
	dev->power.disable_depth = 1;
	device_pm_init_common(dev);
}

extern void pm_runtime_init(struct device *dev);
extern void pm_runtime_reinit(struct device *dev);
extern void pm_runtime_remove(struct device *dev);
extern u64 pm_runtime_active_time(struct device *dev);

#define WAKE_IRQ_DEDICATED_ALLOCATED	BIT(0)
#define WAKE_IRQ_DEDICATED_MANAGED	BIT(1)
#define WAKE_IRQ_DEDICATED_MASK		(WAKE_IRQ_DEDICATED_ALLOCATED | \
					 WAKE_IRQ_DEDICATED_MANAGED)

struct wake_irq {
	struct device *dev;
	unsigned int status;
	int irq;
	const char *name;
};

extern void dev_pm_arm_wake_irq(struct wake_irq *wirq);
extern void dev_pm_disarm_wake_irq(struct wake_irq *wirq);
extern void dev_pm_enable_wake_irq_check(struct device *dev,
					 bool can_change_status);
extern void dev_pm_disable_wake_irq_check(struct device *dev);

#ifdef CONFIG_PM_SLEEP

extern void device_wakeup_attach_irq(struct device *dev, struct wake_irq *wakeirq);
extern void device_wakeup_detach_irq(struct device *dev);
extern void device_wakeup_arm_wake_irqs(void);
extern void device_wakeup_disarm_wake_irqs(void);

#else

static inline void device_wakeup_attach_irq(struct device *dev,
					    struct wake_irq *wakeirq) {}

static inline void device_wakeup_detach_irq(struct device *dev)
{
}

#endif /* CONFIG_PM_SLEEP */

/*
 * sysfs.c
 */

extern int dpm_sysfs_add(struct device *dev);
extern void dpm_sysfs_remove(struct device *dev);
extern void rpm_sysfs_remove(struct device *dev);
extern int wakeup_sysfs_add(struct device *dev);
extern void wakeup_sysfs_remove(struct device *dev);
extern int pm_qos_sysfs_add_resume_latency(struct device *dev);
extern void pm_qos_sysfs_remove_resume_latency(struct device *dev);
extern int pm_qos_sysfs_add_flags(struct device *dev);
extern void pm_qos_sysfs_remove_flags(struct device *dev);
extern int pm_qos_sysfs_add_latency_tolerance(struct device *dev);
extern void pm_qos_sysfs_remove_latency_tolerance(struct device *dev);

#else /* CONFIG_PM */

static inline void pm_runtime_early_init(struct device *dev)
{
	device_pm_init_common(dev);
}

static inline void pm_runtime_init(struct device *dev) {}
static inline void pm_runtime_reinit(struct device *dev) {}
static inline void pm_runtime_remove(struct device *dev) {}

static inline int dpm_sysfs_add(struct device *dev) { return 0; }
static inline void dpm_sysfs_remove(struct device *dev) {}

#endif

#ifdef CONFIG_PM_SLEEP

/* kernel/power/main.c */
extern int pm_async_enabled;

/* drivers/base/power/main.c */
extern struct list_head dpm_list;	/* The active device list */

static inline struct device *to_device(struct list_head *entry)
{
	return container_of(entry, struct device, power.entry);
}

extern void device_pm_sleep_init(struct device *dev);
extern void device_pm_add(struct device *);
extern void device_pm_remove(struct device *);
extern void device_pm_move_before(struct device *, struct device *);
extern void device_pm_move_after(struct device *, struct device *);
extern void device_pm_move_last(struct device *);
extern void device_pm_check_callbacks(struct device *dev);

static inline bool device_pm_initialized(struct device *dev)
{
	return dev->power.in_dpm_list;
}

#else /* !CONFIG_PM_SLEEP */

static inline void device_pm_sleep_init(struct device *dev) {}

static inline void device_pm_add(struct device *dev) {}

static inline void device_pm_remove(struct device *dev)
{
	pm_runtime_remove(dev);
}

static inline void device_pm_move_before(struct device *deva,
					 struct device *devb) {}
static inline void device_pm_move_after(struct device *deva,
					struct device *devb) {}
static inline void device_pm_move_last(struct device *dev) {}

static inline void device_pm_check_callbacks(struct device *dev) {}

static inline bool device_pm_initialized(struct device *dev)
{
	return device_is_registered(dev);
}

#endif /* !CONFIG_PM_SLEEP */

static inline void device_pm_init(struct device *dev)
{
	device_pm_init_common(dev);
	device_pm_sleep_init(dev);
	pm_runtime_init(dev);
}

#ifdef CONFIG_PM_SLEEP

/* drivers/base/power/wakeup_stats.c */
extern int wakeup_source_sysfs_add(struct device *parent,
				   struct wakeup_source *ws);
extern void wakeup_source_sysfs_remove(struct wakeup_source *ws);

extern int pm_wakeup_source_sysfs_add(struct device *parent);

#else /* !CONFIG_PM_SLEEP */

static inline int pm_wakeup_source_sysfs_add(struct device *parent)
{
	return 0;
}

#endif /* CONFIG_PM_SLEEP */
