/* SPDX--License-Identifier: GPL-2.0 */

#ifndef __PLATFORM_EARLY__
#define __PLATFORM_EARLY__

#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

struct sh_early_platform_driver {
	const char *class_str;
	struct platform_driver *pdrv;
	struct list_head list;
	int requested_id;
	char *buffer;
	int bufsize;
};

#define EARLY_PLATFORM_ID_UNSET -2
#define EARLY_PLATFORM_ID_ERROR -3

extern int sh_early_platform_driver_register(struct sh_early_platform_driver *epdrv,
					  char *buf);
extern void sh_early_platform_add_devices(struct platform_device **devs, int num);

static inline int is_sh_early_platform_device(struct platform_device *pdev)
{
	return !pdev->dev.driver;
}

extern void sh_early_platform_driver_register_all(char *class_str);
extern int sh_early_platform_driver_probe(char *class_str,
				       int nr_probe, int user_only);

#define sh_early_platform_init(class_string, platdrv)		\
	sh_early_platform_init_buffer(class_string, platdrv, NULL, 0)

#ifndef MODULE
#define sh_early_platform_init_buffer(class_string, platdrv, buf, bufsiz)	\
static __initdata struct sh_early_platform_driver early_driver = {		\
	.class_str = class_string,					\
	.buffer = buf,							\
	.bufsize = bufsiz,						\
	.pdrv = platdrv,						\
	.requested_id = EARLY_PLATFORM_ID_UNSET,			\
};									\
static int __init sh_early_platform_driver_setup_func(char *buffer)	\
{									\
	return sh_early_platform_driver_register(&early_driver, buffer);	\
}									\
early_param(class_string, sh_early_platform_driver_setup_func)
#else /* MODULE */
#define sh_early_platform_init_buffer(class_string, platdrv, buf, bufsiz)	\
static inline char *sh_early_platform_driver_setup_func(void)		\
{									\
	return bufsiz ? buf : NULL;					\
}
#endif /* MODULE */

#endif /* __PLATFORM_EARLY__ */
