#ifndef __DRIVERS_USB_CHIPIDEA_HOST_H
#define __DRIVERS_USB_CHIPIDEA_HOST_H

#ifdef CONFIG_USB_CHIPIDEA_HOST

int ci_hdrc_host_init(struct ci_hdrc *ci);
void ci_hdrc_host_destroy(struct ci_hdrc *ci);
void ci_hdrc_host_driver_init(void);

#else

static inline int ci_hdrc_host_init(struct ci_hdrc *ci)
{
	return -ENXIO;
}

static inline void ci_hdrc_host_destroy(struct ci_hdrc *ci)
{

}

static void ci_hdrc_host_driver_init(void)
{

}

#endif

#endif /* __DRIVERS_USB_CHIPIDEA_HOST_H */
