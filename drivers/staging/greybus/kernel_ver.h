/*
 * Greybus kernel "version" glue logic.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 *
 * Backports of newer kernel apis to allow the code to build properly on older
 * kernel versions.  Remove this file when merging to upstream, it should not be
 * needed at all
 */

#ifndef __GREYBUS_KERNEL_VER_H
#define __GREYBUS_KERNEL_VER_H

#include <linux/kernel.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
/* Commit: 297d716 power_supply: Change ownership from driver to core */
#define CORE_OWNS_PSY_STRUCT
#endif

#ifndef __ATTR_WO
#define __ATTR_WO(_name) {						\
	.attr	= { .name = __stringify(_name), .mode = S_IWUSR },	\
	.store	= _name##_store,					\
}
#endif

#ifndef __ATTR_RW
#define __ATTR_RW(_name) __ATTR(_name, (S_IWUSR | S_IRUGO),		\
				_name##_show, _name##_store)
#endif

#ifndef DEVICE_ATTR_RO
#define DEVICE_ATTR_RO(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_RO(_name)
#endif

#ifndef DEVICE_ATTR_WO
#define DEVICE_ATTR_WO(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_WO(_name)
#endif

#ifndef DEVICE_ATTR_RW
#define DEVICE_ATTR_RW(_name) \
	struct device_attribute dev_attr_##_name = __ATTR_RW(_name)
#endif

#ifndef U8_MAX
#define U8_MAX	((u8)~0U)
#endif /* ! U8_MAX */

#ifndef U16_MAX
#define U16_MAX	((u16)(~0U))
#endif /* !U16_MAX */

#ifndef U32_MAX
#define U32_MAX	((u32)(~0U))
#endif /* !U32_MAX */

#ifndef U64_MAX
#define U64_MAX	((u64)(~0U))
#endif /* !U64_MAX */

/*
 * The GPIO api sucks rocks in places, like removal, so work around their
 * explicit requirements of catching the return value for kernels older than
 * 3.17, which they explicitly changed in the 3.17 kernel.  Consistency is
 * overrated.
 */
#include <linux/gpio.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
static inline void gb_gpiochip_remove(struct gpio_chip *chip)
{
	gpiochip_remove(chip);
}
#else
static inline void gb_gpiochip_remove(struct gpio_chip *chip)
{
	int ret;

	ret = gpiochip_remove(chip);
}
#endif

/*
 * ATTRIBUTE_GROUPS showed up in 3.11-rc2, but we need to build on 3.10, so add
 * it here.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0)
#include <linux/sysfs.h>

#define ATTRIBUTE_GROUPS(name)					\
static const struct attribute_group name##_group = {		\
	.attrs = name##_attrs,					\
};								\
static const struct attribute_group *name##_groups[] = {	\
	&name##_group,						\
	NULL,							\
}

static inline int sysfs_create_groups(struct kobject *kobj,
				      const struct attribute_group **groups)
{
	int error = 0;
	int i;

	if (!groups)
		return 0;

	for (i = 0; groups[i]; i++) {
		error = sysfs_create_group(kobj, groups[i]);
		if (error) {
			while (--i >= 0)
				sysfs_remove_group(kobj, groups[i]);
			break;
		}
	}
	return error;
}

static inline void sysfs_remove_groups(struct kobject *kobj,
				       const struct attribute_group **groups)
{
	int i;

	if (!groups)
		return;
	for (i = 0; groups[i]; i++)
		sysfs_remove_group(kobj, groups[i]);
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 15, 0)
#define MMC_HS400_SUPPORTED
#define MMC_DDR52_DEFINED
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
#define MMC_POWER_UNDEFINED_SUPPORTED
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0)
#include <linux/scatterlist.h>
static inline bool sg_miter_get_next_page(struct sg_mapping_iter *miter)
{
	if (!miter->__remaining) {
		struct scatterlist *sg;
		unsigned long pgoffset;

		if (!__sg_page_iter_next(&miter->piter))
			return false;

		sg = miter->piter.sg;
		pgoffset = miter->piter.sg_pgoffset;

		miter->__offset = pgoffset ? 0 : sg->offset;
		miter->__remaining = sg->offset + sg->length -
				(pgoffset << PAGE_SHIFT) - miter->__offset;
		miter->__remaining = min_t(unsigned long, miter->__remaining,
					   PAGE_SIZE - miter->__offset);
	}

	return true;
}

static inline bool sg_miter_skip(struct sg_mapping_iter *miter, off_t offset)
{
	sg_miter_stop(miter);

	while (offset) {
		off_t consumed;

		if (!sg_miter_get_next_page(miter))
			return false;

		consumed = min_t(off_t, offset, miter->__remaining);
		miter->__offset += consumed;
		miter->__remaining -= consumed;
		offset -= consumed;
	}

	return true;
}

static inline size_t _sg_copy_buffer(struct scatterlist *sgl,
				     unsigned int nents, void *buf,
				     size_t buflen, off_t skip,
				     bool to_buffer)
{
	unsigned int offset = 0;
	struct sg_mapping_iter miter;
	unsigned long flags;
	unsigned int sg_flags = SG_MITER_ATOMIC;

	if (to_buffer)
		sg_flags |= SG_MITER_FROM_SG;
	else
		sg_flags |= SG_MITER_TO_SG;

	sg_miter_start(&miter, sgl, nents, sg_flags);

	if (!sg_miter_skip(&miter, skip))
		return false;

	local_irq_save(flags);

	while (sg_miter_next(&miter) && offset < buflen) {
		unsigned int len;

		len = min(miter.length, buflen - offset);

		if (to_buffer)
			memcpy(buf + offset, miter.addr, len);
		else
			memcpy(miter.addr, buf + offset, len);

		offset += len;
	}

	sg_miter_stop(&miter);

	local_irq_restore(flags);
	return offset;
}

static inline size_t sg_pcopy_to_buffer(struct scatterlist *sgl,
					unsigned int nents, void *buf,
					size_t buflen, off_t skip)
{
	return _sg_copy_buffer(sgl, nents, buf, buflen, skip, true);
}

static inline size_t sg_pcopy_from_buffer(struct scatterlist *sgl,
					  unsigned int nents, void *buf,
					  size_t buflen, off_t skip)
{
	return _sg_copy_buffer(sgl, nents, buf, buflen, skip, false);
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)
#define list_last_entry(ptr, type, member) \
	list_entry((ptr)->prev, type, member)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
/*
 * Before this version the led classdev did not support groups
 */
#define LED_HAVE_GROUPS
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
/*
 * At this time the internal API for the set brightness was changed to the async
 * version, and one sync API was added to handle cases that need immediate
 * effect. Also, the led class flash and lock for sysfs access was introduced.
 */
#define LED_HAVE_SET_SYNC
#define LED_HAVE_FLASH
#define LED_HAVE_LOCK
#include <linux/led-class-flash.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
/*
 * New change in LED api, the set_sync operation was renamed to set_blocking and
 * the workqueue is now handle by core. So, only one set operation is need.
 */
#undef LED_HAVE_SET_SYNC
#define LED_HAVE_SET_BLOCKING
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 18, 0)
/*
 * From this version upper it was introduced the possibility to disable led
 * sysfs entries to handle control of the led device to v4l2, which was
 * implemented later. So, before that this should return false.
 */
#include <linux/leds.h>
static inline bool led_sysfs_is_disabled(struct led_classdev *led_cdev)
{
	return false;
}
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 2, 0)
/*
 * New helper functions for registering/unregistering flash led devices as v4l2
 * subdevices were added.
 */
#define V4L2_HAVE_FLASH
#include <media/v4l2-flash-led-class.h>
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 1, 0)
/*
 * Power supply get by name need to drop reference after call
 */
#define PSY_HAVE_PUT
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
#define SPI_DEV_MODALIAS "spidev"
#define SPI_NOR_MODALIAS "spi-nor"
#else
#define SPI_DEV_MODALIAS "spidev"
#define SPI_NOR_MODALIAS "m25p80"
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)
/**
 * reinit_completion - reinitialize a completion structure
 * @x:  pointer to completion structure that is to be reinitialized
 *
 * This inline function should be used to reinitialize a completion structure
 * so it can be reused. This is especially important after complete_all() is
 * used.
 */
static inline void reinit_completion(struct completion *x)
{
	x->done = 0;
}
#endif

#endif	/* __GREYBUS_KERNEL_VER_H */
