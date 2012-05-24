/*
 * Kernel version compatibility.
 *
 * Copyright (C) 2007-2008 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * Wherever possible compatible implementations of newer APIs are
 * provided for older kernel versions.
 */
#ifndef __LINUX_KERNEL_COMPAT_H
#define __LINUX_KERNEL_COMPAT_H

#include <linux/version.h>
#include <linux/device.h>
#include <linux/workqueue.h>

#include <asm/io.h>

/*
 * linux/semaphore.h replaces asm/semaphore.h in 2.6.27.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
#  include <asm/semaphore.h>
#else
#  include <linux/semaphore.h>
#endif

/*
 * Workqueue API changes in 2.6.20
 *
 * See http://lwn.net/Articles/211279/ for details.
 *
 * We deliberately don't provide the non-automatic release (NAR)
 * variants as a simple compatible implementation is not possible.
 * This shouldn't be a problem as all usage so far is to embed the
 * struct work_struct into another struct and the NAR variants aren't
 * useful in this case (see http://lwn.net/Articles/213149/).
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)

#include <linux/workqueue.h>

#undef INIT_WORK
#define INIT_WORK(_work, _func)                                         \
    do {                                                                \
        INIT_LIST_HEAD(&(_work)->entry);                                \
        (_work)->pending = 0;                                           \
        PREPARE_WORK((_work), (_func), (_work));                        \
        init_timer(&(_work)->timer);                                    \
    } while(0)

#undef DECLARE_WORK
#define DECLARE_WORK(n, f) \
    struct work_struct n = __WORK_INITIALIZER((n), (f), &(n))

struct delayed_work {
    struct work_struct work;
};

#define INIT_DELAYED_WORK(dw, fn) \
    INIT_WORK(&(dw)->work, (fn))

#define queue_delayed_work(wq, dw, delay) \
    queue_delayed_work((wq), &(dw)->work, (delay))

#define schedule_delayed_work(dw, delay) \
    schedule_delayed_work(&(dw)->work, (delay))

#define cancel_delayed_work(dw) \
    cancel_delayed_work(&(dw)->work)

#endif  /* Linux kernel < 2.6.20 */

/*
 * device_create()/class_device_create()
 *
 * device_create() gains a drvdata parameter in 2.6.27. Since all
 * users of device_create() in CSR code don't use drvdata just ignore
 * it.
 *
 * device_create() replaces class_device_create() in 2.6.21.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)

#define device_create(class, parent, devt, drvdata, fmt, args...) \
    class_device_create((class), (parent), (devt), NULL, (fmt), ## args)
#define device_destroy(class, devt) \
    class_device_destroy(class, devt)

#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)

#define device_create(class, parent, devt, drvdata, fmt, args...) \
    device_create((class), (parent), (devt), (fmt), ## args)

#endif /* Linux kernel < 2.6.26 */

/*
 * dev_name() and dev_set_name() added in 2.6.26.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)

static inline char *dev_name(struct device *dev)
{
    return dev->bus_id;
}

int dev_set_name(struct device *dev, const char *fmt, ...);

#endif /* Linux kernel < 2.6.26 */

/*
 * class_find_device() in 2.6.25
 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)

struct device *class_find_device(struct class *class, struct device *start,
                                 void *data, int (*match)(struct device *, void *));

#endif /* Linux kernel < 2.6.25 */

/*
 * list_first_entry in 2.6.22.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)

#define list_first_entry(ptr, type, member) \
    list_entry((ptr)->next, type, member)

#endif /* Linux kernel < 2.6.22 */

/*
 * 2.6.19 adds a bool type.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)

typedef _Bool bool;
enum {
        false   = 0,
        true    = 1
};

#endif /* Linux kernel < 2.6.19 */

/*
 * Provide readq() and writeq() if unavailable.
 */
#ifndef readq
static inline __u64 readq(const volatile void __iomem *addr)
{
    const volatile u32 __iomem *p = addr;
    u32 low, high;

    low = readl(p);
    high = readl(p + 1);

    return low + ((u64)high << 32);
}
#endif

#ifndef writeq
static inline void writeq(__u64 val, volatile void __iomem *addr)
{
    writel(val, addr);
    writel(val >> 32, addr+4);
}
#endif

/*
 * get_unaligned_le16() and friends added in 2.6.26.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
#include <asm/unaligned.h>

static inline __u16 get_unaligned_le16(const void *p)
{
    return le16_to_cpu(get_unaligned((__le16 *)p));
}

static inline void put_unaligned_le16(__u16 val, const void *p)
{
    put_unaligned(cpu_to_le16(val), (__le16 *)p);
}
#endif /* Linux kernel < 2.6.26 */

/*
 * Various device or vendor IDs may not exist.
 */
#ifndef PCI_VENDOR_ID_CSR
#  define PCI_VENDOR_ID_CSR 0x18e5
#endif

#ifndef PCI_DEVICE_ID_JMICRON_JMB38X_SD
#  define PCI_DEVICE_ID_JMICRON_JMB38X_SD 0x2381
#endif

#endif /* #ifndef __LINUX_KERNEL_COMPAT_H */
