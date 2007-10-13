#ifndef B43_RFKILL_H_
#define B43_RFKILL_H_

struct b43_wldev;


#ifdef CONFIG_B43_RFKILL

#include <linux/rfkill.h>
#include <linux/input-polldev.h>


struct b43_rfkill {
	/* The RFKILL subsystem data structure */
	struct rfkill *rfkill;
	/* The poll device for the RFKILL input button */
	struct input_polled_dev *poll_dev;
	/* The unique name of this rfkill switch */
	char name[32];
};

/* All the init functions return void, because we are not interested
 * in failing the b43 init process when rfkill init failed. */
void b43_rfkill_alloc(struct b43_wldev *dev);
void b43_rfkill_free(struct b43_wldev *dev);
void b43_rfkill_init(struct b43_wldev *dev);
void b43_rfkill_exit(struct b43_wldev *dev);

char * b43_rfkill_led_name(struct b43_wldev *dev);


#else /* CONFIG_B43_RFKILL */
/* No RFKILL support. */

struct b43_rfkill {
	/* empty */
};

static inline void b43_rfkill_alloc(struct b43_wldev *dev)
{
}
static inline void b43_rfkill_free(struct b43_wldev *dev)
{
}
static inline void b43_rfkill_init(struct b43_wldev *dev)
{
}
static inline void b43_rfkill_exit(struct b43_wldev *dev)
{
}
static inline char * b43_rfkill_led_name(struct b43_wldev *dev)
{
	return NULL;
}

#endif /* CONFIG_B43_RFKILL */

#endif /* B43_RFKILL_H_ */
