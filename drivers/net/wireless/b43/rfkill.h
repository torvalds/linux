#ifndef B43_RFKILL_H_
#define B43_RFKILL_H_

struct b43_wldev;


#ifdef CONFIG_B43_RFKILL

#include <linux/rfkill.h>

struct b43_rfkill {
	/* The RFKILL subsystem data structure */
	struct rfkill *rfkill;
	/* The unique name of this rfkill switch */
	char name[32];
	/* Workqueue for asynchronous notification. */
	struct work_struct notify_work;
};

void b43_rfkill_init(struct b43_wldev *dev);
void b43_rfkill_exit(struct b43_wldev *dev);
void b43_rfkill_toggled(struct b43_wldev *dev, bool on);
char * b43_rfkill_led_name(struct b43_wldev *dev);


#else /* CONFIG_B43_RFKILL */
/* No RFKILL support. */

struct b43_rfkill {
	/* empty */
};

static inline void b43_rfkill_init(struct b43_wldev *dev)
{
}
static inline void b43_rfkill_exit(struct b43_wldev *dev)
{
}
static inline void b43_rfkill_toggled(struct b43_wldev *dev, bool on)
{
}
static inline char * b43_rfkill_led_name(struct b43_wldev *dev)
{
	return NULL;
}

#endif /* CONFIG_B43_RFKILL */

#endif /* B43_RFKILL_H_ */
