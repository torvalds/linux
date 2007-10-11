#ifndef B43legacy_RFKILL_H_
#define B43legacy_RFKILL_H_

struct b43legacy_wldev;

#ifdef CONFIG_B43LEGACY_RFKILL

#include <linux/rfkill.h>
#include <linux/workqueue.h>


struct b43legacy_rfkill {
	/* The RFKILL subsystem data structure */
	struct rfkill *rfkill;
	/* The unique name of this rfkill switch */
	char name[32];
	/* Workqueue for asynchronous notification. */
	struct work_struct notify_work;
};

void b43legacy_rfkill_init(struct b43legacy_wldev *dev);
void b43legacy_rfkill_exit(struct b43legacy_wldev *dev);
void b43legacy_rfkill_toggled(struct b43legacy_wldev *dev, bool on);
char *b43legacy_rfkill_led_name(struct b43legacy_wldev *dev);


#else /* CONFIG_B43LEGACY_RFKILL */
/* No RFKILL support. */

struct b43legacy_rfkill {
	/* empty */
};

static inline void b43legacy_rfkill_init(struct b43legacy_wldev *dev)
{
}
static inline void b43legacy_rfkill_exit(struct b43legacy_wldev *dev)
{
}
static inline void b43legacy_rfkill_toggled(struct b43legacy_wldev *dev,
					    bool on)
{
}
static inline char *b43legacy_rfkill_led_name(struct b43legacy_wldev *dev)
{
	return NULL;
}

#endif /* CONFIG_B43LEGACY_RFKILL */

#endif /* B43legacy_RFKILL_H_ */
