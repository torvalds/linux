#ifndef __BACKPORT_DEVICE_H
#define __BACKPORT_DEVICE_H
#include <linux/export.h>
#include_next <linux/device.h>

#include <linux/version.h>

/*
 * string.h is usually included from the asm/ folder in most configuration,
 * but on some older kernels it doesn't. As we're using memcpy() in the code
 * below, we need to be safe and make sure string.h is indeed there.
 */
#include <linux/string.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
/* backport
 * commit 9f3b795a626ee79574595e06d1437fe0c7d51d29
 * Author: Michał Mirosław <mirq-linux@rere.qmqm.pl>
 * Date: Fri Feb 1 20:40:17 2013 +0100
 *
 * driver-core: constify data for class_find_device()
 */
typedef int (backport_device_find_function_t)(struct device *, void *);
#define class_find_device(cls, start, idx, fun) \
	class_find_device((cls), (start), (void *)(idx),\
			  (backport_device_find_function_t *)(fun))
#endif

#ifndef module_driver
/**
 * module_driver() - Helper macro for drivers that don't do anything
 * special in module init/exit. This eliminates a lot of boilerplate.
 * Each module may only use this macro once, and calling it replaces
 * module_init() and module_exit().
 *
 * Use this macro to construct bus specific macros for registering
 * drivers, and do not use it on its own.
 */
#define module_driver(__driver, __register, __unregister) \
static int __init __driver##_init(void) \
{ \
	return __register(&(__driver)); \
} \
module_init(__driver##_init); \
static void __exit __driver##_exit(void) \
{ \
	__unregister(&(__driver)); \
} \
module_exit(__driver##_exit);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)
#define devm_ioremap_resource LINUX_BACKPORT(devm_ioremap_resource)
void __iomem *devm_ioremap_resource(struct device *dev, struct resource *res);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0) && \
    LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0)
#define devres_release LINUX_BACKPORT(devres_release)
extern int devres_release(struct device *dev, dr_release_t release,
			  dr_match_t match, void *match_data);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)
#include <linux/ratelimit.h>

#define dev_level_ratelimited(dev_level, dev, fmt, ...)			\
do {									\
	static DEFINE_RATELIMIT_STATE(_rs,				\
				      DEFAULT_RATELIMIT_INTERVAL,	\
				      DEFAULT_RATELIMIT_BURST);		\
	if (__ratelimit(&_rs))						\
		dev_level(dev, fmt, ##__VA_ARGS__);			\
} while (0)

#define dev_emerg_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_emerg, dev, fmt, ##__VA_ARGS__)
#define dev_alert_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_alert, dev, fmt, ##__VA_ARGS__)
#define dev_crit_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_crit, dev, fmt, ##__VA_ARGS__)
#define dev_err_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_err, dev, fmt, ##__VA_ARGS__)
#define dev_warn_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_warn, dev, fmt, ##__VA_ARGS__)
#define dev_notice_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_notice, dev, fmt, ##__VA_ARGS__)
#define dev_info_ratelimited(dev, fmt, ...)				\
	dev_level_ratelimited(dev_info, dev, fmt, ##__VA_ARGS__)


#if defined(CONFIG_DYNAMIC_DEBUG) || defined(DEBUG)
#define dev_dbg_ratelimited(dev, fmt, ...)				\
do {									\
	static DEFINE_RATELIMIT_STATE(_rs,				\
				      DEFAULT_RATELIMIT_INTERVAL,	\
				      DEFAULT_RATELIMIT_BURST);		\
	DEFINE_DYNAMIC_DEBUG_METADATA(descriptor, fmt);			\
	if (unlikely(descriptor.flags & _DPRINTK_FLAGS_PRINT) &&	\
	    __ratelimit(&_rs))						\
		__dynamic_pr_debug(&descriptor, pr_fmt(fmt),		\
				   ##__VA_ARGS__);			\
} while (0)
#else
#define dev_dbg_ratelimited(dev, fmt, ...)			\
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#endif /* dynamic debug */
#endif /* <= 3.5 */

#if LINUX_VERSION_CODE <= KERNEL_VERSION(3,6,0)
static inline void
backport_device_release_driver(struct device *dev)
{
	device_release_driver(dev);
	device_lock(dev);
	dev_set_drvdata(dev, NULL);
	device_unlock(dev);
}
#define device_release_driver LINUX_BACKPORT(device_release_driver)

#define kobj_to_dev LINUX_BACKPORT(kobj_to_dev)
static inline struct device *kobj_to_dev(struct kobject *kobj)
{
	return container_of(kobj, struct device, kobj);
}
#endif /* LINUX_VERSION_CODE <= KERNEL_VERSION(3,6,0) */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0) && RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(7,0)
#ifndef DEVICE_ATTR_RO
#define DEVICE_ATTR_RO(_name) \
struct device_attribute dev_attr_ ## _name = __ATTR_RO(_name);
#endif
#ifndef DEVICE_ATTR_RW
#define DEVICE_ATTR_RW(_name) \
struct device_attribute dev_attr_ ## _name = __ATTR_RW(_name)
#endif
#endif

#define ATTRIBUTE_GROUPS_BACKPORT(_name) \
static struct BP_ATTR_GRP_STRUCT _name##_dev_attrs[ARRAY_SIZE(_name##_attrs)];\
static void init_##_name##_attrs(void)				\
{									\
	int i;								\
	for (i = 0; _name##_attrs[i]; i++)				\
		_name##_dev_attrs[i] =				\
			*container_of(_name##_attrs[i],		\
				      struct BP_ATTR_GRP_STRUCT,	\
				      attr);				\
}

#ifndef __ATTRIBUTE_GROUPS
#define __ATTRIBUTE_GROUPS(_name)				\
static const struct attribute_group *_name##_groups[] = {	\
	&_name##_group,						\
	NULL,							\
}
#endif /* __ATTRIBUTE_GROUPS */

#undef ATTRIBUTE_GROUPS
#define ATTRIBUTE_GROUPS(_name)					\
static const struct attribute_group _name##_group = {		\
	.attrs = _name##_attrs,					\
};								\
static inline void init_##_name##_attrs(void) {}		\
__ATTRIBUTE_GROUPS(_name)

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
#define devm_kmalloc(dev, size, flags) devm_kzalloc(dev, size, flags) 
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)
#define devm_kstrdup LINUX_BACKPORT(devm_kstrdup)
extern char *devm_kstrdup(struct device *dev, const char *s, gfp_t gfp);
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
#define devm_kmalloc_array LINUX_BACKPORT(devm_kmalloc_array)
static inline void *devm_kmalloc_array(struct device *dev,
				       size_t n, size_t size, gfp_t flags)
{
	if (size != 0 && n > SIZE_MAX / size)
		return NULL;
	return devm_kmalloc(dev, n * size, flags);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)
#define devm_kmemdup LINUX_BACKPORT(devm_kmemdup)
static inline void *devm_kmemdup(struct device *dev, const void *src,
				 size_t len, gfp_t gfp)
{
	void *p;

	p = devm_kmalloc(dev, len, gfp);
	if (p)
		memcpy(p, src, len);

	return p;
}
#endif


#endif /* __BACKPORT_DEVICE_H */
