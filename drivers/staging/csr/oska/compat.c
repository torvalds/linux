/*
 * Linux version compatibility functions.
 *
 * Copyright (C) 2008 Cambridge Silicon Radio Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#include "kernel-compat.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)

int dev_set_name(struct device *dev, const char *fmt, ...)
{
    va_list vargs;

    va_start(vargs, fmt);
    vsnprintf(dev->bus_id, sizeof(dev->bus_id), fmt, vargs);
    va_end(vargs);
    return 0;
}
EXPORT_SYMBOL_GPL(dev_set_name);

#endif /* Linux kernel < 2.6.26 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)

struct device *class_find_device(struct class *class, struct device *start,
                                 void *data, int (*match)(struct device *, void *))
{
    struct device *dev;

    list_for_each_entry(dev, &class->devices, node) {
        if (match(dev, data)) {
            get_device(dev);
            return dev;
        }
    }
    return NULL;
}
EXPORT_SYMBOL_GPL(class_find_device);

#endif /* Linux kernel < 2.6.25 */
