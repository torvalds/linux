#ifndef __BACKPORT_LED_DISABLED_SUPPORT
#define __BACKPORT_LED_DISABLED_SUPPORT

/*
 * LED support is strange, with the NEW_LEDS, LEDS_CLASS and LEDS_TRIGGERS
 * Kconfig symbols ... If any of them are not defined, we build our
 * "compatibility" code that really just makes it all non-working but
 * allows compilation.
 */

#ifdef CPTCFG_BPAUTO_BUILD_LEDS
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/workqueue.h>

#define led_classdev LINUX_BACKPORT(led_classdev)
#define led_trigger LINUX_BACKPORT(led_trigger)

struct led_classdev {
	const char		*name;
	int			 brightness;
	int			 max_brightness;
	int			 flags;

	/* Lower 16 bits reflect status */
#ifndef LED_SUSPENDED
#define LED_SUSPENDED		(1 << 0)
	/* Upper 16 bits reflect control information */
#define LED_CORE_SUSPENDRESUME	(1 << 16)
#define LED_BLINK_ONESHOT	(1 << 17)
#define LED_BLINK_ONESHOT_STOP	(1 << 18)
#define LED_BLINK_INVERT	(1 << 19)
#endif

	/* Set LED brightness level */
	/* Must not sleep, use a workqueue if needed */
	void		(*brightness_set)(struct led_classdev *led_cdev,
					  enum led_brightness brightness);
	/* Get LED brightness level */
	enum led_brightness (*brightness_get)(struct led_classdev *led_cdev);

	/*
	 * Activate hardware accelerated blink, delays are in milliseconds
	 * and if both are zero then a sensible default should be chosen.
	 * The call should adjust the timings in that case and if it can't
	 * match the values specified exactly.
	 * Deactivate blinking again when the brightness is set to a fixed
	 * value via the brightness_set() callback.
	 */
	int		(*blink_set)(struct led_classdev *led_cdev,
				     unsigned long *delay_on,
				     unsigned long *delay_off);

	struct device		*dev;
	struct list_head	 node;			/* LED Device list */
	const char		*default_trigger;	/* Trigger to use */

	unsigned long		 blink_delay_on, blink_delay_off;
	struct timer_list	 blink_timer;
	int			 blink_brightness;

	struct work_struct	set_brightness_work;
	int			delayed_set_value;

	/* Protects the trigger data below */
	struct rw_semaphore	 trigger_lock;

	struct led_trigger	*trigger;
	struct list_head	 trig_list;
	void			*trigger_data;
	/* true if activated - deactivate routine uses it to do cleanup */
	bool			activated;
};

struct led_trigger {
	const char *name;
	void (*activate)(struct led_classdev *led_cdev);
	void (*deactivate)(struct led_classdev *led_cdev);
	rwlock_t leddev_list_lock;
	struct list_head led_cdevs;
	struct list_head next_trig;
};

#undef led_classdev_register
#define led_classdev_register LINUX_BACKPORT(led_classdev_register)
#undef led_classdev_unregister
#define led_classdev_unregister LINUX_BACKPORT(led_classdev_unregister)
#undef led_blink_set
#define led_blink_set LINUX_BACKPORT(led_blink_set)
#undef led_set_brightness
#define led_set_brightness LINUX_BACKPORT(led_set_brightness)
#undef led_classdev_suspend
#define led_classdev_suspend LINUX_BACKPORT(led_classdev_suspend)
#undef led_classdev_resume
#define led_classdev_resume LINUX_BACKPORT(led_classdev_resume)

#undef led_trigger_register
#define led_trigger_register LINUX_BACKPORT(led_trigger_register)
#undef led_trigger_unregister
#define led_trigger_unregister LINUX_BACKPORT(led_trigger_unregister)
#undef led_trigger_register_simple
#define led_trigger_register_simple LINUX_BACKPORT(led_trigger_register_simple)
#undef led_trigger_unregister_simple
#define led_trigger_unregister_simple LINUX_BACKPORT(led_trigger_unregister_simple)
#undef led_trigger_event
#define led_trigger_event LINUX_BACKPORT(led_trigger_event)

#undef DEFINE_LED_TRIGGER
#define DEFINE_LED_TRIGGER(x) static struct led_trigger *x;

static inline int led_classdev_register(struct device *parent,
					struct led_classdev *led_cdev)
{
	return 0;
}

static inline void led_classdev_unregister(struct led_classdev *led_cdev)
{
}

static inline void led_trigger_register_simple(const char *name,
					       struct led_trigger **trigger)
{
}

static inline void led_trigger_unregister_simple(struct led_trigger *trigger)
{
}

static inline void led_blink_set(struct led_classdev *led_cdev,
				 unsigned long *delay_on,
				 unsigned long *delay_off)
{
}

static inline void led_set_brightness(struct led_classdev *led_cdev,
				      enum led_brightness brightness)
{
}

static inline void led_classdev_suspend(struct led_classdev *led_cdev)
{
}

static inline void led_classdev_resume(struct led_classdev *led_cdev)
{
}

static inline int led_trigger_register(struct led_trigger *trigger)
{
	INIT_LIST_HEAD(&trigger->led_cdevs);
	INIT_LIST_HEAD(&trigger->next_trig);
	rwlock_init(&trigger->leddev_list_lock);
	return 0;
}

static inline void led_trigger_unregister(struct led_trigger *trigger)
{
}

static inline void led_trigger_event(struct led_trigger *trigger,
				     enum led_brightness event)
{
}

static inline void led_trigger_blink(struct led_trigger *trigger,
				     unsigned long *delay_on,
				     unsigned long *delay_off)
{
}

static inline void led_trigger_blink_oneshot(struct led_trigger *trigger,
					     unsigned long *delay_on,
					     unsigned long *delay_off,
					     int invert)
{
}
#endif

#endif /* __BACKPORT_LED_DISABLED_SUPPORT */
