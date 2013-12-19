#ifndef B43_SDIO_H_
#define B43_SDIO_H_

#include <linux/ssb/ssb.h>

struct b43_wldev;


#ifdef CONFIG_B43_SDIO

struct b43_sdio {
	struct ssb_bus ssb;
	void *irq_handler_opaque;
	void (*irq_handler)(struct b43_wldev *dev);
};

int b43_sdio_request_irq(struct b43_wldev *dev,
			 void (*handler)(struct b43_wldev *dev));
void b43_sdio_free_irq(struct b43_wldev *dev);

int b43_sdio_init(void);
void b43_sdio_exit(void);


#else /* CONFIG_B43_SDIO */


static inline int b43_sdio_request_irq(struct b43_wldev *dev,
			 void (*handler)(struct b43_wldev *dev))
{
	return -ENODEV;
}
static inline void b43_sdio_free_irq(struct b43_wldev *dev)
{
}
static inline int b43_sdio_init(void)
{
	return 0;
}
static inline void b43_sdio_exit(void)
{
}

#endif /* CONFIG_B43_SDIO */
#endif /* B43_SDIO_H_ */
